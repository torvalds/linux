/*
 *  arch/arm/include/asm/xor.h
 *
 *  Copyright (C) 2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/hardirq.h>
#include <asm-generic/xor.h>
#include <asm/hwcap.h>
#include <asm/neon.h>

#define __XOR(a1, a2) a1 ^= a2

#define GET_BLOCK_2(dst) \
	__asm__("ldmia	%0, {%1, %2}" \
		: "=r" (dst), "=r" (a1), "=r" (a2) \
		: "0" (dst))

#define GET_BLOCK_4(dst) \
	__asm__("ldmia	%0, {%1, %2, %3, %4}" \
		: "=r" (dst), "=r" (a1), "=r" (a2), "=r" (a3), "=r" (a4) \
		: "0" (dst))

#define XOR_BLOCK_2(src) \
	__asm__("ldmia	%0!, {%1, %2}" \
		: "=r" (src), "=r" (b1), "=r" (b2) \
		: "0" (src)); \
	__XOR(a1, b1); __XOR(a2, b2);

#define XOR_BLOCK_4(src) \
	__asm__("ldmia	%0!, {%1, %2, %3, %4}" \
		: "=r" (src), "=r" (b1), "=r" (b2), "=r" (b3), "=r" (b4) \
		: "0" (src)); \
	__XOR(a1, b1); __XOR(a2, b2); __XOR(a3, b3); __XOR(a4, b4)

#define PUT_BLOCK_2(dst) \
	__asm__ __volatile__("stmia	%0!, {%2, %3}" \
		: "=r" (dst) \
		: "0" (dst), "r" (a1), "r" (a2))

#define PUT_BLOCK_4(dst) \
	__asm__ __volatile__("stmia	%0!, {%2, %3, %4, %5}" \
		: "=r" (dst) \
		: "0" (dst), "r" (a1), "r" (a2), "r" (a3), "r" (a4))

static void
xor_arm4regs_2(unsigned long bytes, unsigned long *p1, unsigned long *p2)
{
	unsigned int lines = bytes / sizeof(unsigned long) / 4;
	register unsigned int a1 __asm__("r4");
	register unsigned int a2 __asm__("r5");
	register unsigned int a3 __asm__("r6");
	register unsigned int a4 __asm__("r7");
	register unsigned int b1 __asm__("r8");
	register unsigned int b2 __asm__("r9");
	register unsigned int b3 __asm__("ip");
	register unsigned int b4 __asm__("lr");

	do {
		GET_BLOCK_4(p1);
		XOR_BLOCK_4(p2);
		PUT_BLOCK_4(p1);
	} while (--lines);
}

static void
xor_arm4regs_3(unsigned long bytes, unsigned long *p1, unsigned long *p2,
		unsigned long *p3)
{
	unsigned int lines = bytes / sizeof(unsigned long) / 4;
	register unsigned int a1 __asm__("r4");
	register unsigned int a2 __asm__("r5");
	register unsigned int a3 __asm__("r6");
	register unsigned int a4 __asm__("r7");
	register unsigned int b1 __asm__("r8");
	register unsigned int b2 __asm__("r9");
	register unsigned int b3 __asm__("ip");
	register unsigned int b4 __asm__("lr");

	do {
		GET_BLOCK_4(p1);
		XOR_BLOCK_4(p2);
		XOR_BLOCK_4(p3);
		PUT_BLOCK_4(p1);
	} while (--lines);
}

static void
xor_arm4regs_4(unsigned long bytes, unsigned long *p1, unsigned long *p2,
		unsigned long *p3, unsigned long *p4)
{
	unsigned int lines = bytes / sizeof(unsigned long) / 2;
	register unsigned int a1 __asm__("r8");
	register unsigned int a2 __asm__("r9");
	register unsigned int b1 __asm__("ip");
	register unsigned int b2 __asm__("lr");

	do {
		GET_BLOCK_2(p1);
		XOR_BLOCK_2(p2);
		XOR_BLOCK_2(p3);
		XOR_BLOCK_2(p4);
		PUT_BLOCK_2(p1);
	} while (--lines);
}

static void
xor_arm4regs_5(unsigned long bytes, unsigned long *p1, unsigned long *p2,
		unsigned long *p3, unsigned long *p4, unsigned long *p5)
{
	unsigned int lines = bytes / sizeof(unsigned long) / 2;
	register unsigned int a1 __asm__("r8");
	register unsigned int a2 __asm__("r9");
	register unsigned int b1 __asm__("ip");
	register unsigned int b2 __asm__("lr");

	do {
		GET_BLOCK_2(p1);
		XOR_BLOCK_2(p2);
		XOR_BLOCK_2(p3);
		XOR_BLOCK_2(p4);
		XOR_BLOCK_2(p5);
		PUT_BLOCK_2(p1);
	} while (--lines);
}

static struct xor_block_template xor_block_arm4regs = {
	.name	= "arm4regs",
	.do_2	= xor_arm4regs_2,
	.do_3	= xor_arm4regs_3,
	.do_4	= xor_arm4regs_4,
	.do_5	= xor_arm4regs_5,
};

#undef XOR_TRY_TEMPLATES
#define XOR_TRY_TEMPLATES			\
	do {					\
		xor_speed(&xor_block_arm4regs);	\
		xor_speed(&xor_block_8regs);	\
		xor_speed(&xor_block_32regs);	\
		NEON_TEMPLATES;			\
	} while (0)

#ifdef CONFIG_KERNEL_MODE_NEON

extern struct xor_block_template const xor_block_neon_inner;

static void
xor_neon_2(unsigned long bytes, unsigned long *p1, unsigned long *p2)
{
	if (in_interrupt()) {
		xor_arm4regs_2(bytes, p1, p2);
	} else {
		kernel_neon_begin();
		xor_block_neon_inner.do_2(bytes, p1, p2);
		kernel_neon_end();
	}
}

static void
xor_neon_3(unsigned long bytes, unsigned long *p1, unsigned long *p2,
		unsigned long *p3)
{
	if (in_interrupt()) {
		xor_arm4regs_3(bytes, p1, p2, p3);
	} else {
		kernel_neon_begin();
		xor_block_neon_inner.do_3(bytes, p1, p2, p3);
		kernel_neon_end();
	}
}

static void
xor_neon_4(unsigned long bytes, unsigned long *p1, unsigned long *p2,
		unsigned long *p3, unsigned long *p4)
{
	if (in_interrupt()) {
		xor_arm4regs_4(bytes, p1, p2, p3, p4);
	} else {
		kernel_neon_begin();
		xor_block_neon_inner.do_4(bytes, p1, p2, p3, p4);
		kernel_neon_end();
	}
}

static void
xor_neon_5(unsigned long bytes, unsigned long *p1, unsigned long *p2,
		unsigned long *p3, unsigned long *p4, unsigned long *p5)
{
	if (in_interrupt()) {
		xor_arm4regs_5(bytes, p1, p2, p3, p4, p5);
	} else {
		kernel_neon_begin();
		xor_block_neon_inner.do_5(bytes, p1, p2, p3, p4, p5);
		kernel_neon_end();
	}
}

static struct xor_block_template xor_block_neon = {
	.name	= "neon",
	.do_2	= xor_neon_2,
	.do_3	= xor_neon_3,
	.do_4	= xor_neon_4,
	.do_5	= xor_neon_5
};

#define NEON_TEMPLATES	\
	do { if (cpu_has_neon()) xor_speed(&xor_block_neon); } while (0)
#else
#define NEON_TEMPLATES
#endif
