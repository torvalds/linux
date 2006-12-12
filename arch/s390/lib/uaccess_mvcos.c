/*
 *  arch/s390/lib/uaccess_mvcos.c
 *
 *  Optimized user space space access functions based on mvcos.
 *
 *    Copyright (C) IBM Corp. 2006
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *		 Gerald Schaefer (gerald.schaefer@de.ibm.com)
 */

#include <linux/errno.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <asm/futex.h>

#ifndef __s390x__
#define AHI	"ahi"
#define ALR	"alr"
#define CLR	"clr"
#define LHI	"lhi"
#define SLR	"slr"
#else
#define AHI	"aghi"
#define ALR	"algr"
#define CLR	"clgr"
#define LHI	"lghi"
#define SLR	"slgr"
#endif

extern size_t copy_from_user_std(size_t, const void __user *, void *);
extern size_t copy_to_user_std(size_t, void __user *, const void *);

size_t copy_from_user_mvcos(size_t size, const void __user *ptr, void *x)
{
	register unsigned long reg0 asm("0") = 0x81UL;
	unsigned long tmp1, tmp2;

	tmp1 = -4096UL;
	asm volatile(
		"0: .insn ss,0xc80000000000,0(%0,%2),0(%1),0\n"
		"   jz    7f\n"
		"1:"ALR"  %0,%3\n"
		"  "SLR"  %1,%3\n"
		"  "SLR"  %2,%3\n"
		"   j     0b\n"
		"2: la    %4,4095(%1)\n"/* %4 = ptr + 4095 */
		"   nr    %4,%3\n"	/* %4 = (ptr + 4095) & -4096 */
		"  "SLR"  %4,%1\n"
		"  "CLR"  %0,%4\n"	/* copy crosses next page boundary? */
		"   jnh   4f\n"
		"3: .insn ss,0xc80000000000,0(%4,%2),0(%1),0\n"
		"  "SLR"  %0,%4\n"
		"  "ALR"  %2,%4\n"
		"4:"LHI"  %4,-1\n"
		"  "ALR"  %4,%0\n"	/* copy remaining size, subtract 1 */
		"   bras  %3,6f\n"	/* memset loop */
		"   xc    0(1,%2),0(%2)\n"
		"5: xc    0(256,%2),0(%2)\n"
		"   la    %2,256(%2)\n"
		"6:"AHI"  %4,-256\n"
		"   jnm   5b\n"
		"   ex    %4,0(%3)\n"
		"   j     8f\n"
		"7:"SLR"  %0,%0\n"
		"8: \n"
		EX_TABLE(0b,2b) EX_TABLE(3b,4b)
		: "+a" (size), "+a" (ptr), "+a" (x), "+a" (tmp1), "=a" (tmp2)
		: "d" (reg0) : "cc", "memory");
	return size;
}

size_t copy_from_user_mvcos_check(size_t size, const void __user *ptr, void *x)
{
	if (size <= 256)
		return copy_from_user_std(size, ptr, x);
	return copy_from_user_mvcos(size, ptr, x);
}

size_t copy_to_user_mvcos(size_t size, void __user *ptr, const void *x)
{
	register unsigned long reg0 asm("0") = 0x810000UL;
	unsigned long tmp1, tmp2;

	tmp1 = -4096UL;
	asm volatile(
		"0: .insn ss,0xc80000000000,0(%0,%1),0(%2),0\n"
		"   jz    4f\n"
		"1:"ALR"  %0,%3\n"
		"  "SLR"  %1,%3\n"
		"  "SLR"  %2,%3\n"
		"   j     0b\n"
		"2: la    %4,4095(%1)\n"/* %4 = ptr + 4095 */
		"   nr    %4,%3\n"	/* %4 = (ptr + 4095) & -4096 */
		"  "SLR"  %4,%1\n"
		"  "CLR"  %0,%4\n"	/* copy crosses next page boundary? */
		"   jnh   5f\n"
		"3: .insn ss,0xc80000000000,0(%4,%1),0(%2),0\n"
		"  "SLR"  %0,%4\n"
		"   j     5f\n"
		"4:"SLR"  %0,%0\n"
		"5: \n"
		EX_TABLE(0b,2b) EX_TABLE(3b,5b)
		: "+a" (size), "+a" (ptr), "+a" (x), "+a" (tmp1), "=a" (tmp2)
		: "d" (reg0) : "cc", "memory");
	return size;
}

size_t copy_to_user_mvcos_check(size_t size, void __user *ptr, const void *x)
{
	if (size <= 256)
		return copy_to_user_std(size, ptr, x);
	return copy_to_user_mvcos(size, ptr, x);
}

size_t copy_in_user_mvcos(size_t size, void __user *to, const void __user *from)
{
	register unsigned long reg0 asm("0") = 0x810081UL;
	unsigned long tmp1, tmp2;

	tmp1 = -4096UL;
	/* FIXME: copy with reduced length. */
	asm volatile(
		"0: .insn ss,0xc80000000000,0(%0,%1),0(%2),0\n"
		"   jz    2f\n"
		"1:"ALR"  %0,%3\n"
		"  "SLR"  %1,%3\n"
		"  "SLR"  %2,%3\n"
		"   j     0b\n"
		"2:"SLR"  %0,%0\n"
		"3: \n"
		EX_TABLE(0b,3b)
		: "+a" (size), "+a" (to), "+a" (from), "+a" (tmp1), "=a" (tmp2)
		: "d" (reg0) : "cc", "memory");
	return size;
}

size_t clear_user_mvcos(size_t size, void __user *to)
{
	register unsigned long reg0 asm("0") = 0x810000UL;
	unsigned long tmp1, tmp2;

	tmp1 = -4096UL;
	asm volatile(
		"0: .insn ss,0xc80000000000,0(%0,%1),0(%4),0\n"
		"   jz    4f\n"
		"1:"ALR"  %0,%2\n"
		"  "SLR"  %1,%2\n"
		"   j     0b\n"
		"2: la    %3,4095(%1)\n"/* %4 = to + 4095 */
		"   nr    %3,%2\n"	/* %4 = (to + 4095) & -4096 */
		"  "SLR"  %3,%1\n"
		"  "CLR"  %0,%3\n"	/* copy crosses next page boundary? */
		"   jnh   5f\n"
		"3: .insn ss,0xc80000000000,0(%3,%1),0(%4),0\n"
		"  "SLR"  %0,%3\n"
		"   j     5f\n"
		"4:"SLR"  %0,%0\n"
		"5: \n"
		EX_TABLE(0b,2b) EX_TABLE(3b,5b)
		: "+a" (size), "+a" (to), "+a" (tmp1), "=a" (tmp2)
		: "a" (empty_zero_page), "d" (reg0) : "cc", "memory");
	return size;
}

extern size_t strnlen_user_std(size_t, const char __user *);
extern size_t strncpy_from_user_std(size_t, const char __user *, char *);
extern int futex_atomic_op(int, int __user *, int, int *);
extern int futex_atomic_cmpxchg(int __user *, int, int);

struct uaccess_ops uaccess_mvcos = {
	.copy_from_user = copy_from_user_mvcos_check,
	.copy_from_user_small = copy_from_user_std,
	.copy_to_user = copy_to_user_mvcos_check,
	.copy_to_user_small = copy_to_user_std,
	.copy_in_user = copy_in_user_mvcos,
	.clear_user = clear_user_mvcos,
	.strnlen_user = strnlen_user_std,
	.strncpy_from_user = strncpy_from_user_std,
	.futex_atomic_op = futex_atomic_op,
	.futex_atomic_cmpxchg = futex_atomic_cmpxchg,
};
