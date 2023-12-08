// SPDX-License-Identifier: GPL-2.0
/*
 * Optimized xor_block operation for RAID4/5
 *
 * Copyright IBM Corp. 2016
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/types.h>
#include <linux/export.h>
#include <linux/raid/xor.h>
#include <asm/xor.h>

static void xor_xc_2(unsigned long bytes, unsigned long * __restrict p1,
		     const unsigned long * __restrict p2)
{
	asm volatile(
		"	larl	1,2f\n"
		"	aghi	%0,-1\n"
		"	jm	3f\n"
		"	srlg	0,%0,8\n"
		"	ltgr	0,0\n"
		"	jz	1f\n"
		"0:	xc	0(256,%1),0(%2)\n"
		"	la	%1,256(%1)\n"
		"	la	%2,256(%2)\n"
		"	brctg	0,0b\n"
		"1:	ex	%0,0(1)\n"
		"	j	3f\n"
		"2:	xc	0(1,%1),0(%2)\n"
		"3:\n"
		: : "d" (bytes), "a" (p1), "a" (p2)
		: "0", "1", "cc", "memory");
}

static void xor_xc_3(unsigned long bytes, unsigned long * __restrict p1,
		     const unsigned long * __restrict p2,
		     const unsigned long * __restrict p3)
{
	asm volatile(
		"	larl	1,2f\n"
		"	aghi	%0,-1\n"
		"	jm	3f\n"
		"	srlg	0,%0,8\n"
		"	ltgr	0,0\n"
		"	jz	1f\n"
		"0:	xc	0(256,%1),0(%2)\n"
		"	xc	0(256,%1),0(%3)\n"
		"	la	%1,256(%1)\n"
		"	la	%2,256(%2)\n"
		"	la	%3,256(%3)\n"
		"	brctg	0,0b\n"
		"1:	ex	%0,0(1)\n"
		"	ex	%0,6(1)\n"
		"	j	3f\n"
		"2:	xc	0(1,%1),0(%2)\n"
		"	xc	0(1,%1),0(%3)\n"
		"3:\n"
		: "+d" (bytes), "+a" (p1), "+a" (p2), "+a" (p3)
		: : "0", "1", "cc", "memory");
}

static void xor_xc_4(unsigned long bytes, unsigned long * __restrict p1,
		     const unsigned long * __restrict p2,
		     const unsigned long * __restrict p3,
		     const unsigned long * __restrict p4)
{
	asm volatile(
		"	larl	1,2f\n"
		"	aghi	%0,-1\n"
		"	jm	3f\n"
		"	srlg	0,%0,8\n"
		"	ltgr	0,0\n"
		"	jz	1f\n"
		"0:	xc	0(256,%1),0(%2)\n"
		"	xc	0(256,%1),0(%3)\n"
		"	xc	0(256,%1),0(%4)\n"
		"	la	%1,256(%1)\n"
		"	la	%2,256(%2)\n"
		"	la	%3,256(%3)\n"
		"	la	%4,256(%4)\n"
		"	brctg	0,0b\n"
		"1:	ex	%0,0(1)\n"
		"	ex	%0,6(1)\n"
		"	ex	%0,12(1)\n"
		"	j	3f\n"
		"2:	xc	0(1,%1),0(%2)\n"
		"	xc	0(1,%1),0(%3)\n"
		"	xc	0(1,%1),0(%4)\n"
		"3:\n"
		: "+d" (bytes), "+a" (p1), "+a" (p2), "+a" (p3), "+a" (p4)
		: : "0", "1", "cc", "memory");
}

static void xor_xc_5(unsigned long bytes, unsigned long * __restrict p1,
		     const unsigned long * __restrict p2,
		     const unsigned long * __restrict p3,
		     const unsigned long * __restrict p4,
		     const unsigned long * __restrict p5)
{
	asm volatile(
		"	larl	1,2f\n"
		"	aghi	%0,-1\n"
		"	jm	3f\n"
		"	srlg	0,%0,8\n"
		"	ltgr	0,0\n"
		"	jz	1f\n"
		"0:	xc	0(256,%1),0(%2)\n"
		"	xc	0(256,%1),0(%3)\n"
		"	xc	0(256,%1),0(%4)\n"
		"	xc	0(256,%1),0(%5)\n"
		"	la	%1,256(%1)\n"
		"	la	%2,256(%2)\n"
		"	la	%3,256(%3)\n"
		"	la	%4,256(%4)\n"
		"	la	%5,256(%5)\n"
		"	brctg	0,0b\n"
		"1:	ex	%0,0(1)\n"
		"	ex	%0,6(1)\n"
		"	ex	%0,12(1)\n"
		"	ex	%0,18(1)\n"
		"	j	3f\n"
		"2:	xc	0(1,%1),0(%2)\n"
		"	xc	0(1,%1),0(%3)\n"
		"	xc	0(1,%1),0(%4)\n"
		"	xc	0(1,%1),0(%5)\n"
		"3:\n"
		: "+d" (bytes), "+a" (p1), "+a" (p2), "+a" (p3), "+a" (p4),
		  "+a" (p5)
		: : "0", "1", "cc", "memory");
}

struct xor_block_template xor_block_xc = {
	.name = "xc",
	.do_2 = xor_xc_2,
	.do_3 = xor_xc_3,
	.do_4 = xor_xc_4,
	.do_5 = xor_xc_5,
};
EXPORT_SYMBOL(xor_block_xc);
