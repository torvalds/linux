// SPDX-License-Identifier: GPL-2.0
/*
 *  Standard user space access functions based on mvcp/mvcs and doing
 *  interesting things in the secondary space mode.
 *
 *    Copyright IBM Corp. 2006,2014
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *		 Gerald Schaefer (gerald.schaefer@de.ibm.com)
 */

#include <linux/jump_label.h>
#include <linux/uaccess.h>
#include <linux/export.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <asm/mmu_context.h>
#include <asm/facility.h>

#ifdef CONFIG_DEBUG_ENTRY
void debug_user_asce(int exit)
{
	unsigned long cr1, cr7;

	__ctl_store(cr1, 1, 1);
	__ctl_store(cr7, 7, 7);
	if (cr1 == S390_lowcore.kernel_asce && cr7 == S390_lowcore.user_asce)
		return;
	panic("incorrect ASCE on kernel %s\n"
	      "cr1:    %016lx cr7:  %016lx\n"
	      "kernel: %016llx user: %016llx\n",
	      exit ? "exit" : "entry", cr1, cr7,
	      S390_lowcore.kernel_asce, S390_lowcore.user_asce);

}
#endif /*CONFIG_DEBUG_ENTRY */

#ifndef CONFIG_HAVE_MARCH_Z10_FEATURES
static DEFINE_STATIC_KEY_FALSE(have_mvcos);

static int __init uaccess_init(void)
{
	if (test_facility(27))
		static_branch_enable(&have_mvcos);
	return 0;
}
early_initcall(uaccess_init);

static inline int copy_with_mvcos(void)
{
	if (static_branch_likely(&have_mvcos))
		return 1;
	return 0;
}
#else
static inline int copy_with_mvcos(void)
{
	return 1;
}
#endif

static inline unsigned long copy_from_user_mvcos(void *x, const void __user *ptr,
						 unsigned long size)
{
	unsigned long tmp1, tmp2;

	tmp1 = -4096UL;
	asm volatile(
		"   lghi  0,%[spec]\n"
		"0: .insn ss,0xc80000000000,0(%0,%2),0(%1),0\n"
		"6: jz    4f\n"
		"1: algr  %0,%3\n"
		"   slgr  %1,%3\n"
		"   slgr  %2,%3\n"
		"   j     0b\n"
		"2: la    %4,4095(%1)\n"/* %4 = ptr + 4095 */
		"   nr    %4,%3\n"	/* %4 = (ptr + 4095) & -4096 */
		"   slgr  %4,%1\n"
		"   clgr  %0,%4\n"	/* copy crosses next page boundary? */
		"   jnh   5f\n"
		"3: .insn ss,0xc80000000000,0(%4,%2),0(%1),0\n"
		"7: slgr  %0,%4\n"
		"   j     5f\n"
		"4: slgr  %0,%0\n"
		"5:\n"
		EX_TABLE(0b,2b) EX_TABLE(3b,5b) EX_TABLE(6b,2b) EX_TABLE(7b,5b)
		: "+a" (size), "+a" (ptr), "+a" (x), "+a" (tmp1), "=a" (tmp2)
		: [spec] "K" (0x81UL)
		: "cc", "memory", "0");
	return size;
}

static inline unsigned long copy_from_user_mvcp(void *x, const void __user *ptr,
						unsigned long size)
{
	unsigned long tmp1, tmp2;

	tmp1 = -256UL;
	asm volatile(
		"   sacf  0\n"
		"0: mvcp  0(%0,%2),0(%1),%3\n"
		"7: jz    5f\n"
		"1: algr  %0,%3\n"
		"   la    %1,256(%1)\n"
		"   la    %2,256(%2)\n"
		"2: mvcp  0(%0,%2),0(%1),%3\n"
		"8: jnz   1b\n"
		"   j     5f\n"
		"3: la    %4,255(%1)\n"	/* %4 = ptr + 255 */
		"   lghi  %3,-4096\n"
		"   nr    %4,%3\n"	/* %4 = (ptr + 255) & -4096 */
		"   slgr  %4,%1\n"
		"   clgr  %0,%4\n"	/* copy crosses next page boundary? */
		"   jnh   6f\n"
		"4: mvcp  0(%4,%2),0(%1),%3\n"
		"9: slgr  %0,%4\n"
		"   j     6f\n"
		"5: slgr  %0,%0\n"
		"6: sacf  768\n"
		EX_TABLE(0b,3b) EX_TABLE(2b,3b) EX_TABLE(4b,6b)
		EX_TABLE(7b,3b) EX_TABLE(8b,3b) EX_TABLE(9b,6b)
		: "+a" (size), "+a" (ptr), "+a" (x), "+a" (tmp1), "=a" (tmp2)
		: : "cc", "memory");
	return size;
}

unsigned long raw_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	if (copy_with_mvcos())
		return copy_from_user_mvcos(to, from, n);
	return copy_from_user_mvcp(to, from, n);
}
EXPORT_SYMBOL(raw_copy_from_user);

static inline unsigned long copy_to_user_mvcos(void __user *ptr, const void *x,
					       unsigned long size)
{
	unsigned long tmp1, tmp2;

	tmp1 = -4096UL;
	asm volatile(
		"   llilh 0,%[spec]\n"
		"0: .insn ss,0xc80000000000,0(%0,%1),0(%2),0\n"
		"6: jz    4f\n"
		"1: algr  %0,%3\n"
		"   slgr  %1,%3\n"
		"   slgr  %2,%3\n"
		"   j     0b\n"
		"2: la    %4,4095(%1)\n"/* %4 = ptr + 4095 */
		"   nr    %4,%3\n"	/* %4 = (ptr + 4095) & -4096 */
		"   slgr  %4,%1\n"
		"   clgr  %0,%4\n"	/* copy crosses next page boundary? */
		"   jnh   5f\n"
		"3: .insn ss,0xc80000000000,0(%4,%1),0(%2),0\n"
		"7: slgr  %0,%4\n"
		"   j     5f\n"
		"4: slgr  %0,%0\n"
		"5:\n"
		EX_TABLE(0b,2b) EX_TABLE(3b,5b) EX_TABLE(6b,2b) EX_TABLE(7b,5b)
		: "+a" (size), "+a" (ptr), "+a" (x), "+a" (tmp1), "=a" (tmp2)
		: [spec] "K" (0x81UL)
		: "cc", "memory", "0");
	return size;
}

static inline unsigned long copy_to_user_mvcs(void __user *ptr, const void *x,
					      unsigned long size)
{
	unsigned long tmp1, tmp2;

	tmp1 = -256UL;
	asm volatile(
		"   sacf  0\n"
		"0: mvcs  0(%0,%1),0(%2),%3\n"
		"7: jz    5f\n"
		"1: algr  %0,%3\n"
		"   la    %1,256(%1)\n"
		"   la    %2,256(%2)\n"
		"2: mvcs  0(%0,%1),0(%2),%3\n"
		"8: jnz   1b\n"
		"   j     5f\n"
		"3: la    %4,255(%1)\n" /* %4 = ptr + 255 */
		"   lghi  %3,-4096\n"
		"   nr    %4,%3\n"	/* %4 = (ptr + 255) & -4096 */
		"   slgr  %4,%1\n"
		"   clgr  %0,%4\n"	/* copy crosses next page boundary? */
		"   jnh   6f\n"
		"4: mvcs  0(%4,%1),0(%2),%3\n"
		"9: slgr  %0,%4\n"
		"   j     6f\n"
		"5: slgr  %0,%0\n"
		"6: sacf  768\n"
		EX_TABLE(0b,3b) EX_TABLE(2b,3b) EX_TABLE(4b,6b)
		EX_TABLE(7b,3b) EX_TABLE(8b,3b) EX_TABLE(9b,6b)
		: "+a" (size), "+a" (ptr), "+a" (x), "+a" (tmp1), "=a" (tmp2)
		: : "cc", "memory");
	return size;
}

unsigned long raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (copy_with_mvcos())
		return copy_to_user_mvcos(to, from, n);
	return copy_to_user_mvcs(to, from, n);
}
EXPORT_SYMBOL(raw_copy_to_user);

static inline unsigned long clear_user_mvcos(void __user *to, unsigned long size)
{
	unsigned long tmp1, tmp2;

	tmp1 = -4096UL;
	asm volatile(
		"   llilh 0,%[spec]\n"
		"0: .insn ss,0xc80000000000,0(%0,%1),0(%4),0\n"
		"   jz	  4f\n"
		"1: algr  %0,%2\n"
		"   slgr  %1,%2\n"
		"   j	  0b\n"
		"2: la	  %3,4095(%1)\n"/* %4 = to + 4095 */
		"   nr	  %3,%2\n"	/* %4 = (to + 4095) & -4096 */
		"   slgr  %3,%1\n"
		"   clgr  %0,%3\n"	/* copy crosses next page boundary? */
		"   jnh	  5f\n"
		"3: .insn ss,0xc80000000000,0(%3,%1),0(%4),0\n"
		"   slgr  %0,%3\n"
		"   j	  5f\n"
		"4: slgr  %0,%0\n"
		"5:\n"
		EX_TABLE(0b,2b) EX_TABLE(3b,5b)
		: "+a" (size), "+a" (to), "+a" (tmp1), "=a" (tmp2)
		: "a" (empty_zero_page), [spec] "K" (0x81UL)
		: "cc", "memory", "0");
	return size;
}

static inline unsigned long clear_user_xc(void __user *to, unsigned long size)
{
	unsigned long tmp1, tmp2;

	asm volatile(
		"   sacf  256\n"
		"   aghi  %0,-1\n"
		"   jo    5f\n"
		"   bras  %3,3f\n"
		"   xc    0(1,%1),0(%1)\n"
		"0: aghi  %0,257\n"
		"   la    %2,255(%1)\n" /* %2 = ptr + 255 */
		"   srl   %2,12\n"
		"   sll   %2,12\n"	/* %2 = (ptr + 255) & -4096 */
		"   slgr  %2,%1\n"
		"   clgr  %0,%2\n"	/* clear crosses next page boundary? */
		"   jnh   5f\n"
		"   aghi  %2,-1\n"
		"1: ex    %2,0(%3)\n"
		"   aghi  %2,1\n"
		"   slgr  %0,%2\n"
		"   j     5f\n"
		"2: xc    0(256,%1),0(%1)\n"
		"   la    %1,256(%1)\n"
		"3: aghi  %0,-256\n"
		"   jnm   2b\n"
		"4: ex    %0,0(%3)\n"
		"5: slgr  %0,%0\n"
		"6: sacf  768\n"
		EX_TABLE(1b,6b) EX_TABLE(2b,0b) EX_TABLE(4b,0b)
		: "+a" (size), "+a" (to), "=a" (tmp1), "=a" (tmp2)
		: : "cc", "memory");
	return size;
}

unsigned long __clear_user(void __user *to, unsigned long size)
{
	if (copy_with_mvcos())
			return clear_user_mvcos(to, size);
	return clear_user_xc(to, size);
}
EXPORT_SYMBOL(__clear_user);
