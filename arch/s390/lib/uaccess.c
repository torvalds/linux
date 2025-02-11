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
#include <asm/ctlreg.h>

#ifdef CONFIG_DEBUG_ENTRY
void debug_user_asce(int exit)
{
	struct ctlreg cr1, cr7;

	local_ctl_store(1, &cr1);
	local_ctl_store(7, &cr7);
	if (cr1.val == get_lowcore()->kernel_asce.val && cr7.val == get_lowcore()->user_asce.val)
		return;
	panic("incorrect ASCE on kernel %s\n"
	      "cr1:    %016lx cr7:  %016lx\n"
	      "kernel: %016lx user: %016lx\n",
	      exit ? "exit" : "entry", cr1.val, cr7.val,
	      get_lowcore()->kernel_asce.val, get_lowcore()->user_asce.val);
}
#endif /*CONFIG_DEBUG_ENTRY */

union oac {
	unsigned int val;
	struct {
		struct {
			unsigned short key : 4;
			unsigned short	   : 4;
			unsigned short as  : 2;
			unsigned short	   : 4;
			unsigned short k   : 1;
			unsigned short a   : 1;
		} oac1;
		struct {
			unsigned short key : 4;
			unsigned short	   : 4;
			unsigned short as  : 2;
			unsigned short	   : 4;
			unsigned short k   : 1;
			unsigned short a   : 1;
		} oac2;
	};
};

static uaccess_kmsan_or_inline __must_check unsigned long
raw_copy_from_user_key(void *to, const void __user *from, unsigned long size, unsigned long key)
{
	unsigned long osize;
	union oac spec = {
		.oac2.key = key,
		.oac2.as = PSW_BITS_AS_SECONDARY,
		.oac2.k = 1,
		.oac2.a = 1,
	};
	int cc;

	while (1) {
		osize = size;
		asm_inline volatile(
			"	lr	%%r0,%[spec]\n"
			"0:	mvcos	%[to],%[from],%[size]\n"
			"1:	nopr	%%r7\n"
			CC_IPM(cc)
			EX_TABLE_UA_MVCOS_FROM(0b, 0b)
			EX_TABLE_UA_MVCOS_FROM(1b, 0b)
			: CC_OUT(cc, cc), [size] "+d" (size), [to] "=Q" (*(char *)to)
			: [spec] "d" (spec.val), [from] "Q" (*(const char __user *)from)
			: CC_CLOBBER_LIST("memory", "0"));
		if (CC_TRANSFORM(cc) == 0)
			return osize - size;
		size -= 4096;
		to += 4096;
		from += 4096;
	}
}

unsigned long _copy_from_user_key(void *to, const void __user *from,
				  unsigned long n, unsigned long key)
{
	unsigned long res = n;

	might_fault();
	if (!should_fail_usercopy()) {
		instrument_copy_from_user_before(to, from, n);
		res = raw_copy_from_user_key(to, from, n, key);
		instrument_copy_from_user_after(to, from, n, res);
	}
	if (unlikely(res))
		memset(to + (n - res), 0, res);
	return res;
}
EXPORT_SYMBOL(_copy_from_user_key);

static uaccess_kmsan_or_inline __must_check unsigned long
raw_copy_to_user_key(void __user *to, const void *from, unsigned long size, unsigned long key)
{
	unsigned long osize;
	union oac spec = {
		.oac1.key = key,
		.oac1.as = PSW_BITS_AS_SECONDARY,
		.oac1.k = 1,
		.oac1.a = 1,
	};
	int cc;

	while (1) {
		osize = size;
		asm_inline volatile(
			"	lr	%%r0,%[spec]\n"
			"0:	mvcos	%[to],%[from],%[size]\n"
			"1:	nopr	%%r7\n"
			CC_IPM(cc)
			EX_TABLE_UA_MVCOS_TO(0b, 0b)
			EX_TABLE_UA_MVCOS_TO(1b, 0b)
			: CC_OUT(cc, cc), [size] "+d" (size), [to] "=Q" (*(char __user *)to)
			: [spec] "d" (spec.val), [from] "Q" (*(const char *)from)
			: CC_CLOBBER_LIST("memory", "0"));
		if (CC_TRANSFORM(cc) == 0)
			return osize - size;
		size -= 4096;
		to += 4096;
		from += 4096;
	}
}

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
	unsigned long rem;
	union oac spec = {
		.oac1.as = PSW_BITS_AS_SECONDARY,
		.oac1.a = 1,
	};

	asm volatile(
		"	lr	0,%[spec]\n"
		"0:	mvcos	0(%[to]),0(%[zeropg]),%[size]\n"
		"1:	jz	5f\n"
		"	algr	%[size],%[val]\n"
		"	slgr	%[to],%[val]\n"
		"	j	0b\n"
		"2:	la	%[rem],4095(%[to])\n"	/* rem = to + 4095 */
		"	nr	%[rem],%[val]\n"	/* rem = (to + 4095) & -4096 */
		"	slgr	%[rem],%[to]\n"
		"	clgr	%[size],%[rem]\n"	/* copy crosses next page boundary? */
		"	jnh	6f\n"
		"3:	mvcos	0(%[to]),0(%[zeropg]),%[rem]\n"
		"4:	slgr	%[size],%[rem]\n"
		"	j	6f\n"
		"5:	slgr	%[size],%[size]\n"
		"6:\n"
		EX_TABLE(0b, 2b)
		EX_TABLE(1b, 2b)
		EX_TABLE(3b, 6b)
		EX_TABLE(4b, 6b)
		: [size] "+&a" (size), [to] "+&a" (to), [rem] "=&a" (rem)
		: [val] "a" (-4096UL), [zeropg] "a" (empty_zero_page), [spec] "d" (spec.val)
		: "cc", "memory", "0");
	return size;
}
EXPORT_SYMBOL(__clear_user);
