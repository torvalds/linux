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

static DEFINE_STATIC_KEY_FALSE(have_mvcos);

static inline unsigned long copy_from_user_mvcos(void *x, const void __user *ptr,
						 unsigned long size)
{
	register unsigned long reg0 asm("0") = 0x81UL;
	unsigned long tmp1, tmp2;

	tmp1 = -4096UL;
	asm volatile(
		"0: .insn ss,0xc80000000000,0(%0,%2),0(%1),0\n"
		"9: jz    7f\n"
		"1: algr  %0,%3\n"
		"   slgr  %1,%3\n"
		"   slgr  %2,%3\n"
		"   j     0b\n"
		"2: la    %4,4095(%1)\n"/* %4 = ptr + 4095 */
		"   nr    %4,%3\n"	/* %4 = (ptr + 4095) & -4096 */
		"   slgr  %4,%1\n"
		"   clgr  %0,%4\n"	/* copy crosses next page boundary? */
		"   jnh   4f\n"
		"3: .insn ss,0xc80000000000,0(%4,%2),0(%1),0\n"
		"10:slgr  %0,%4\n"
		"   algr  %2,%4\n"
		"4: lghi  %4,-1\n"
		"   algr  %4,%0\n"	/* copy remaining size, subtract 1 */
		"   bras  %3,6f\n"	/* memset loop */
		"   xc    0(1,%2),0(%2)\n"
		"5: xc    0(256,%2),0(%2)\n"
		"   la    %2,256(%2)\n"
		"6: aghi  %4,-256\n"
		"   jnm   5b\n"
		"   ex    %4,0(%3)\n"
		"   j     8f\n"
		"7: slgr  %0,%0\n"
		"8:\n"
		EX_TABLE(0b,2b) EX_TABLE(3b,4b) EX_TABLE(9b,2b) EX_TABLE(10b,4b)
		: "+a" (size), "+a" (ptr), "+a" (x), "+a" (tmp1), "=a" (tmp2)
		: "d" (reg0) : "cc", "memory");
	return size;
}

static inline unsigned long copy_from_user_mvcp(void *x, const void __user *ptr,
						unsigned long size)
{
	unsigned long tmp1, tmp2;

	load_kernel_asce();
	tmp1 = -256UL;
	asm volatile(
		"   sacf  0\n"
		"0: mvcp  0(%0,%2),0(%1),%3\n"
		"10:jz    8f\n"
		"1: algr  %0,%3\n"
		"   la    %1,256(%1)\n"
		"   la    %2,256(%2)\n"
		"2: mvcp  0(%0,%2),0(%1),%3\n"
		"11:jnz   1b\n"
		"   j     8f\n"
		"3: la    %4,255(%1)\n"	/* %4 = ptr + 255 */
		"   lghi  %3,-4096\n"
		"   nr    %4,%3\n"	/* %4 = (ptr + 255) & -4096 */
		"   slgr  %4,%1\n"
		"   clgr  %0,%4\n"	/* copy crosses next page boundary? */
		"   jnh   5f\n"
		"4: mvcp  0(%4,%2),0(%1),%3\n"
		"12:slgr  %0,%4\n"
		"   algr  %2,%4\n"
		"5: lghi  %4,-1\n"
		"   algr  %4,%0\n"	/* copy remaining size, subtract 1 */
		"   bras  %3,7f\n"	/* memset loop */
		"   xc    0(1,%2),0(%2)\n"
		"6: xc    0(256,%2),0(%2)\n"
		"   la    %2,256(%2)\n"
		"7: aghi  %4,-256\n"
		"   jnm   6b\n"
		"   ex    %4,0(%3)\n"
		"   j     9f\n"
		"8: slgr  %0,%0\n"
		"9: sacf  768\n"
		EX_TABLE(0b,3b) EX_TABLE(2b,3b) EX_TABLE(4b,5b)
		EX_TABLE(10b,3b) EX_TABLE(11b,3b) EX_TABLE(12b,5b)
		: "+a" (size), "+a" (ptr), "+a" (x), "+a" (tmp1), "=a" (tmp2)
		: : "cc", "memory");
	return size;
}

unsigned long __copy_from_user(void *to, const void __user *from, unsigned long n)
{
	if (static_branch_likely(&have_mvcos))
		return copy_from_user_mvcos(to, from, n);
	return copy_from_user_mvcp(to, from, n);
}
EXPORT_SYMBOL(__copy_from_user);

static inline unsigned long copy_to_user_mvcos(void __user *ptr, const void *x,
					       unsigned long size)
{
	register unsigned long reg0 asm("0") = 0x810000UL;
	unsigned long tmp1, tmp2;

	tmp1 = -4096UL;
	asm volatile(
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
		: "d" (reg0) : "cc", "memory");
	return size;
}

static inline unsigned long copy_to_user_mvcs(void __user *ptr, const void *x,
					      unsigned long size)
{
	unsigned long tmp1, tmp2;

	load_kernel_asce();
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

unsigned long __copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (static_branch_likely(&have_mvcos))
		return copy_to_user_mvcos(to, from, n);
	return copy_to_user_mvcs(to, from, n);
}
EXPORT_SYMBOL(__copy_to_user);

static inline unsigned long copy_in_user_mvcos(void __user *to, const void __user *from,
					       unsigned long size)
{
	register unsigned long reg0 asm("0") = 0x810081UL;
	unsigned long tmp1, tmp2;

	tmp1 = -4096UL;
	/* FIXME: copy with reduced length. */
	asm volatile(
		"0: .insn ss,0xc80000000000,0(%0,%1),0(%2),0\n"
		"   jz	  2f\n"
		"1: algr  %0,%3\n"
		"   slgr  %1,%3\n"
		"   slgr  %2,%3\n"
		"   j	  0b\n"
		"2:slgr  %0,%0\n"
		"3: \n"
		EX_TABLE(0b,3b)
		: "+a" (size), "+a" (to), "+a" (from), "+a" (tmp1), "=a" (tmp2)
		: "d" (reg0) : "cc", "memory");
	return size;
}

static inline unsigned long copy_in_user_mvc(void __user *to, const void __user *from,
					     unsigned long size)
{
	unsigned long tmp1;

	load_kernel_asce();
	asm volatile(
		"   sacf  256\n"
		"   aghi  %0,-1\n"
		"   jo	  5f\n"
		"   bras  %3,3f\n"
		"0: aghi  %0,257\n"
		"1: mvc	  0(1,%1),0(%2)\n"
		"   la	  %1,1(%1)\n"
		"   la	  %2,1(%2)\n"
		"   aghi  %0,-1\n"
		"   jnz	  1b\n"
		"   j	  5f\n"
		"2: mvc	  0(256,%1),0(%2)\n"
		"   la	  %1,256(%1)\n"
		"   la	  %2,256(%2)\n"
		"3: aghi  %0,-256\n"
		"   jnm	  2b\n"
		"4: ex	  %0,1b-0b(%3)\n"
		"5: slgr  %0,%0\n"
		"6: sacf  768\n"
		EX_TABLE(1b,6b) EX_TABLE(2b,0b) EX_TABLE(4b,0b)
		: "+a" (size), "+a" (to), "+a" (from), "=a" (tmp1)
		: : "cc", "memory");
	return size;
}

unsigned long __copy_in_user(void __user *to, const void __user *from, unsigned long n)
{
	if (static_branch_likely(&have_mvcos))
		return copy_in_user_mvcos(to, from, n);
	return copy_in_user_mvc(to, from, n);
}
EXPORT_SYMBOL(__copy_in_user);

static inline unsigned long clear_user_mvcos(void __user *to, unsigned long size)
{
	register unsigned long reg0 asm("0") = 0x810000UL;
	unsigned long tmp1, tmp2;

	tmp1 = -4096UL;
	asm volatile(
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
		: "a" (empty_zero_page), "d" (reg0) : "cc", "memory");
	return size;
}

static inline unsigned long clear_user_xc(void __user *to, unsigned long size)
{
	unsigned long tmp1, tmp2;

	load_kernel_asce();
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
	if (static_branch_likely(&have_mvcos))
			return clear_user_mvcos(to, size);
	return clear_user_xc(to, size);
}
EXPORT_SYMBOL(__clear_user);

static inline unsigned long strnlen_user_srst(const char __user *src,
					      unsigned long size)
{
	register unsigned long reg0 asm("0") = 0;
	unsigned long tmp1, tmp2;

	asm volatile(
		"   la    %2,0(%1)\n"
		"   la    %3,0(%0,%1)\n"
		"   slgr  %0,%0\n"
		"   sacf  256\n"
		"0: srst  %3,%2\n"
		"   jo    0b\n"
		"   la    %0,1(%3)\n"	/* strnlen_user results includes \0 */
		"   slgr  %0,%1\n"
		"1: sacf  768\n"
		EX_TABLE(0b,1b)
		: "+a" (size), "+a" (src), "=a" (tmp1), "=a" (tmp2)
		: "d" (reg0) : "cc", "memory");
	return size;
}

unsigned long __strnlen_user(const char __user *src, unsigned long size)
{
	if (unlikely(!size))
		return 0;
	load_kernel_asce();
	return strnlen_user_srst(src, size);
}
EXPORT_SYMBOL(__strnlen_user);

long __strncpy_from_user(char *dst, const char __user *src, long size)
{
	size_t done, len, offset, len_str;

	if (unlikely(size <= 0))
		return 0;
	done = 0;
	do {
		offset = (size_t)src & ~PAGE_MASK;
		len = min(size - done, PAGE_SIZE - offset);
		if (copy_from_user(dst, src, len))
			return -EFAULT;
		len_str = strnlen(dst, len);
		done += len_str;
		src += len_str;
		dst += len_str;
	} while ((len_str == len) && (done < size));
	return done;
}
EXPORT_SYMBOL(__strncpy_from_user);

static int __init uaccess_init(void)
{
	if (test_facility(27))
		static_branch_enable(&have_mvcos);
	return 0;
}
early_initcall(uaccess_init);
