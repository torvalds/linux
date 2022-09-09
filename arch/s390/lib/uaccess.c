// SPDX-License-Identifier: GPL-2.0
/*
 *  Standard user space access functions based on mvcp/mvcs and doing
 *  interesting things in the secondary space mode.
 *
 *    Copyright IBM Corp. 2006,2014
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *		 Gerald Schaefer (gerald.schaefer@de.ibm.com)
 */

#include <linux/uaccess.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <asm/asm-extable.h>

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

static unsigned long raw_copy_from_user_key(void *to, const void __user *from,
					    unsigned long size, unsigned long key)
{
	unsigned long tmp1, tmp2;
	union oac spec = {
		.oac2.key = key,
		.oac2.as = PSW_BITS_AS_SECONDARY,
		.oac2.k = 1,
		.oac2.a = 1,
	};

	tmp1 = -4096UL;
	asm volatile(
		"   lr	  0,%[spec]\n"
		"0: mvcos 0(%2),0(%1),%0\n"
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
		"3: mvcos 0(%2),0(%1),%4\n"
		"7: slgr  %0,%4\n"
		"   j     5f\n"
		"4: slgr  %0,%0\n"
		"5:\n"
		EX_TABLE(0b,2b) EX_TABLE(3b,5b) EX_TABLE(6b,2b) EX_TABLE(7b,5b)
		: "+a" (size), "+a" (from), "+a" (to), "+a" (tmp1), "=a" (tmp2)
		: [spec] "d" (spec.val)
		: "cc", "memory", "0");
	return size;
}

unsigned long raw_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	return raw_copy_from_user_key(to, from, n, 0);
}
EXPORT_SYMBOL(raw_copy_from_user);

unsigned long _copy_from_user_key(void *to, const void __user *from,
				  unsigned long n, unsigned long key)
{
	unsigned long res = n;

	might_fault();
	if (!should_fail_usercopy()) {
		instrument_copy_from_user(to, from, n);
		res = raw_copy_from_user_key(to, from, n, key);
	}
	if (unlikely(res))
		memset(to + (n - res), 0, res);
	return res;
}
EXPORT_SYMBOL(_copy_from_user_key);

static unsigned long raw_copy_to_user_key(void __user *to, const void *from,
					  unsigned long size, unsigned long key)
{
	unsigned long tmp1, tmp2;
	union oac spec = {
		.oac1.key = key,
		.oac1.as = PSW_BITS_AS_SECONDARY,
		.oac1.k = 1,
		.oac1.a = 1,
	};

	tmp1 = -4096UL;
	asm volatile(
		"   lr	  0,%[spec]\n"
		"0: mvcos 0(%1),0(%2),%0\n"
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
		"3: mvcos 0(%1),0(%2),%4\n"
		"7: slgr  %0,%4\n"
		"   j     5f\n"
		"4: slgr  %0,%0\n"
		"5:\n"
		EX_TABLE(0b,2b) EX_TABLE(3b,5b) EX_TABLE(6b,2b) EX_TABLE(7b,5b)
		: "+a" (size), "+a" (to), "+a" (from), "+a" (tmp1), "=a" (tmp2)
		: [spec] "d" (spec.val)
		: "cc", "memory", "0");
	return size;
}

unsigned long raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	return raw_copy_to_user_key(to, from, n, 0);
}
EXPORT_SYMBOL(raw_copy_to_user);

unsigned long _copy_to_user_key(void __user *to, const void *from,
				unsigned long n, unsigned long key)
{
	might_fault();
	if (should_fail_usercopy())
		return n;
	instrument_copy_to_user(to, from, n);
	return raw_copy_to_user_key(to, from, n, key);
}
EXPORT_SYMBOL(_copy_to_user_key);

unsigned long __clear_user(void __user *to, unsigned long size)
{
	unsigned long tmp1, tmp2;
	union oac spec = {
		.oac1.as = PSW_BITS_AS_SECONDARY,
		.oac1.a = 1,
	};

	tmp1 = -4096UL;
	asm volatile(
		"   lr	  0,%[spec]\n"
		"0: mvcos 0(%1),0(%4),%0\n"
		"   jz	  4f\n"
		"1: algr  %0,%2\n"
		"   slgr  %1,%2\n"
		"   j	  0b\n"
		"2: la	  %3,4095(%1)\n"/* %4 = to + 4095 */
		"   nr	  %3,%2\n"	/* %4 = (to + 4095) & -4096 */
		"   slgr  %3,%1\n"
		"   clgr  %0,%3\n"	/* copy crosses next page boundary? */
		"   jnh	  5f\n"
		"3: mvcos 0(%1),0(%4),%3\n"
		"   slgr  %0,%3\n"
		"   j	  5f\n"
		"4: slgr  %0,%0\n"
		"5:\n"
		EX_TABLE(0b,2b) EX_TABLE(3b,5b)
		: "+a" (size), "+a" (to), "+a" (tmp1), "=a" (tmp2)
		: "a" (empty_zero_page), [spec] "d" (spec.val)
		: "cc", "memory", "0");
	return size;
}
EXPORT_SYMBOL(__clear_user);
