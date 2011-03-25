/*
 *  arch/s390/lib/uaccess_std.c
 *
 *  Standard user space access functions based on mvcp/mvcs and doing
 *  interesting things in the secondary space mode.
 *
 *    Copyright (C) IBM Corp. 2006
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *		 Gerald Schaefer (gerald.schaefer@de.ibm.com)
 */

#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <asm/futex.h>
#include "uaccess.h"

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

size_t copy_from_user_std(size_t size, const void __user *ptr, void *x)
{
	unsigned long tmp1, tmp2;

	tmp1 = -256UL;
	asm volatile(
		"0: mvcp  0(%0,%2),0(%1),%3\n"
		"10:jz    8f\n"
		"1:"ALR"  %0,%3\n"
		"   la    %1,256(%1)\n"
		"   la    %2,256(%2)\n"
		"2: mvcp  0(%0,%2),0(%1),%3\n"
		"11:jnz   1b\n"
		"   j     8f\n"
		"3: la    %4,255(%1)\n"	/* %4 = ptr + 255 */
		"  "LHI"  %3,-4096\n"
		"   nr    %4,%3\n"	/* %4 = (ptr + 255) & -4096 */
		"  "SLR"  %4,%1\n"
		"  "CLR"  %0,%4\n"	/* copy crosses next page boundary? */
		"   jnh   5f\n"
		"4: mvcp  0(%4,%2),0(%1),%3\n"
		"12:"SLR"  %0,%4\n"
		"  "ALR"  %2,%4\n"
		"5:"LHI"  %4,-1\n"
		"  "ALR"  %4,%0\n"	/* copy remaining size, subtract 1 */
		"   bras  %3,7f\n"	/* memset loop */
		"   xc    0(1,%2),0(%2)\n"
		"6: xc    0(256,%2),0(%2)\n"
		"   la    %2,256(%2)\n"
		"7:"AHI"  %4,-256\n"
		"   jnm   6b\n"
		"   ex    %4,0(%3)\n"
		"   j     9f\n"
		"8:"SLR"  %0,%0\n"
		"9: \n"
		EX_TABLE(0b,3b) EX_TABLE(2b,3b) EX_TABLE(4b,5b)
		EX_TABLE(10b,3b) EX_TABLE(11b,3b) EX_TABLE(12b,5b)
		: "+a" (size), "+a" (ptr), "+a" (x), "+a" (tmp1), "=a" (tmp2)
		: : "cc", "memory");
	return size;
}

static size_t copy_from_user_std_check(size_t size, const void __user *ptr,
				       void *x)
{
	if (size <= 1024)
		return copy_from_user_std(size, ptr, x);
	return copy_from_user_pt(size, ptr, x);
}

size_t copy_to_user_std(size_t size, void __user *ptr, const void *x)
{
	unsigned long tmp1, tmp2;

	tmp1 = -256UL;
	asm volatile(
		"0: mvcs  0(%0,%1),0(%2),%3\n"
		"7: jz    5f\n"
		"1:"ALR"  %0,%3\n"
		"   la    %1,256(%1)\n"
		"   la    %2,256(%2)\n"
		"2: mvcs  0(%0,%1),0(%2),%3\n"
		"8: jnz   1b\n"
		"   j     5f\n"
		"3: la    %4,255(%1)\n" /* %4 = ptr + 255 */
		"  "LHI"  %3,-4096\n"
		"   nr    %4,%3\n"	/* %4 = (ptr + 255) & -4096 */
		"  "SLR"  %4,%1\n"
		"  "CLR"  %0,%4\n"	/* copy crosses next page boundary? */
		"   jnh   6f\n"
		"4: mvcs  0(%4,%1),0(%2),%3\n"
		"9:"SLR"  %0,%4\n"
		"   j     6f\n"
		"5:"SLR"  %0,%0\n"
		"6: \n"
		EX_TABLE(0b,3b) EX_TABLE(2b,3b) EX_TABLE(4b,6b)
		EX_TABLE(7b,3b) EX_TABLE(8b,3b) EX_TABLE(9b,6b)
		: "+a" (size), "+a" (ptr), "+a" (x), "+a" (tmp1), "=a" (tmp2)
		: : "cc", "memory");
	return size;
}

static size_t copy_to_user_std_check(size_t size, void __user *ptr,
				     const void *x)
{
	if (size <= 1024)
		return copy_to_user_std(size, ptr, x);
	return copy_to_user_pt(size, ptr, x);
}

static size_t copy_in_user_std(size_t size, void __user *to,
			       const void __user *from)
{
	unsigned long tmp1;

	asm volatile(
		"   sacf  256\n"
		"  "AHI"  %0,-1\n"
		"   jo    5f\n"
		"   bras  %3,3f\n"
		"0:"AHI"  %0,257\n"
		"1: mvc   0(1,%1),0(%2)\n"
		"   la    %1,1(%1)\n"
		"   la    %2,1(%2)\n"
		"  "AHI"  %0,-1\n"
		"   jnz   1b\n"
		"   j     5f\n"
		"2: mvc   0(256,%1),0(%2)\n"
		"   la    %1,256(%1)\n"
		"   la    %2,256(%2)\n"
		"3:"AHI"  %0,-256\n"
		"   jnm   2b\n"
		"4: ex    %0,1b-0b(%3)\n"
		"5: "SLR"  %0,%0\n"
		"6: sacf  0\n"
		EX_TABLE(1b,6b) EX_TABLE(2b,0b) EX_TABLE(4b,0b)
		: "+a" (size), "+a" (to), "+a" (from), "=a" (tmp1)
		: : "cc", "memory");
	return size;
}

static size_t clear_user_std(size_t size, void __user *to)
{
	unsigned long tmp1, tmp2;

	asm volatile(
		"   sacf  256\n"
		"  "AHI"  %0,-1\n"
		"   jo    5f\n"
		"   bras  %3,3f\n"
		"   xc    0(1,%1),0(%1)\n"
		"0:"AHI"  %0,257\n"
		"   la    %2,255(%1)\n" /* %2 = ptr + 255 */
		"   srl   %2,12\n"
		"   sll   %2,12\n"	/* %2 = (ptr + 255) & -4096 */
		"  "SLR"  %2,%1\n"
		"  "CLR"  %0,%2\n"	/* clear crosses next page boundary? */
		"   jnh   5f\n"
		"  "AHI"  %2,-1\n"
		"1: ex    %2,0(%3)\n"
		"  "AHI"  %2,1\n"
		"  "SLR"  %0,%2\n"
		"   j     5f\n"
		"2: xc    0(256,%1),0(%1)\n"
		"   la    %1,256(%1)\n"
		"3:"AHI"  %0,-256\n"
		"   jnm   2b\n"
		"4: ex    %0,0(%3)\n"
		"5: "SLR"  %0,%0\n"
		"6: sacf  0\n"
		EX_TABLE(1b,6b) EX_TABLE(2b,0b) EX_TABLE(4b,0b)
		: "+a" (size), "+a" (to), "=a" (tmp1), "=a" (tmp2)
		: : "cc", "memory");
	return size;
}

size_t strnlen_user_std(size_t size, const char __user *src)
{
	register unsigned long reg0 asm("0") = 0UL;
	unsigned long tmp1, tmp2;

	asm volatile(
		"   la    %2,0(%1)\n"
		"   la    %3,0(%0,%1)\n"
		"  "SLR"  %0,%0\n"
		"   sacf  256\n"
		"0: srst  %3,%2\n"
		"   jo    0b\n"
		"   la    %0,1(%3)\n"	/* strnlen_user results includes \0 */
		"  "SLR"  %0,%1\n"
		"1: sacf  0\n"
		EX_TABLE(0b,1b)
		: "+a" (size), "+a" (src), "=a" (tmp1), "=a" (tmp2)
		: "d" (reg0) : "cc", "memory");
	return size;
}

size_t strncpy_from_user_std(size_t size, const char __user *src, char *dst)
{
	register unsigned long reg0 asm("0") = 0UL;
	unsigned long tmp1, tmp2;

	asm volatile(
		"   la    %3,0(%1)\n"
		"   la    %4,0(%0,%1)\n"
		"   sacf  256\n"
		"0: srst  %4,%3\n"
		"   jo    0b\n"
		"   sacf  0\n"
		"   la    %0,0(%4)\n"
		"   jh    1f\n"		/* found \0 in string ? */
		"  "AHI"  %4,1\n"	/* include \0 in copy */
		"1:"SLR"  %0,%1\n"	/* %0 = return length (without \0) */
		"  "SLR"  %4,%1\n"	/* %4 = copy length (including \0) */
		"2: mvcp  0(%4,%2),0(%1),%5\n"
		"   jz    9f\n"
		"3:"AHI"  %4,-256\n"
		"   la    %1,256(%1)\n"
		"   la    %2,256(%2)\n"
		"4: mvcp  0(%4,%2),0(%1),%5\n"
		"   jnz   3b\n"
		"   j     9f\n"
		"7: sacf  0\n"
		"8:"LHI"  %0,%6\n"
		"9:\n"
		EX_TABLE(0b,7b) EX_TABLE(2b,8b) EX_TABLE(4b,8b)
		: "+a" (size), "+a" (src), "+d" (dst), "=a" (tmp1), "=a" (tmp2)
		: "d" (reg0), "K" (-EFAULT) : "cc", "memory");
	return size;
}

#define __futex_atomic_op(insn, ret, oldval, newval, uaddr, oparg)	\
	asm volatile(							\
		"   sacf  256\n"					\
		"0: l     %1,0(%6)\n"					\
		"1:"insn						\
		"2: cs    %1,%2,0(%6)\n"				\
		"3: jl    1b\n"						\
		"   lhi   %0,0\n"					\
		"4: sacf  0\n"						\
		EX_TABLE(0b,4b) EX_TABLE(2b,4b) EX_TABLE(3b,4b)		\
		: "=d" (ret), "=&d" (oldval), "=&d" (newval),		\
		  "=m" (*uaddr)						\
		: "0" (-EFAULT), "d" (oparg), "a" (uaddr),		\
		  "m" (*uaddr) : "cc");

int futex_atomic_op_std(int op, u32 __user *uaddr, int oparg, int *old)
{
	int oldval = 0, newval, ret;

	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op("lr %2,%5\n",
				  ret, oldval, newval, uaddr, oparg);
		break;
	case FUTEX_OP_ADD:
		__futex_atomic_op("lr %2,%1\nar %2,%5\n",
				  ret, oldval, newval, uaddr, oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("lr %2,%1\nor %2,%5\n",
				  ret, oldval, newval, uaddr, oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("lr %2,%1\nnr %2,%5\n",
				  ret, oldval, newval, uaddr, oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("lr %2,%1\nxr %2,%5\n",
				  ret, oldval, newval, uaddr, oparg);
		break;
	default:
		ret = -ENOSYS;
	}
	*old = oldval;
	return ret;
}

int futex_atomic_cmpxchg_std(u32 *uval, u32 __user *uaddr,
			     u32 oldval, u32 newval)
{
	int ret;

	asm volatile(
		"   sacf 256\n"
		"0: cs   %1,%4,0(%5)\n"
		"1: la   %0,0\n"
		"2: sacf 0\n"
		EX_TABLE(0b,2b) EX_TABLE(1b,2b)
		: "=d" (ret), "+d" (oldval), "=m" (*uaddr)
		: "0" (-EFAULT), "d" (newval), "a" (uaddr), "m" (*uaddr)
		: "cc", "memory" );
	*uval = oldval;
	return ret;
}

struct uaccess_ops uaccess_std = {
	.copy_from_user = copy_from_user_std_check,
	.copy_from_user_small = copy_from_user_std,
	.copy_to_user = copy_to_user_std_check,
	.copy_to_user_small = copy_to_user_std,
	.copy_in_user = copy_in_user_std,
	.clear_user = clear_user_std,
	.strnlen_user = strnlen_user_std,
	.strncpy_from_user = strncpy_from_user_std,
	.futex_atomic_op = futex_atomic_op_std,
	.futex_atomic_cmpxchg = futex_atomic_cmpxchg_std,
};
