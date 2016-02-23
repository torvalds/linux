/*
 * include/asm/xor.h
 *
 * Optimized RAID-5 checksumming functions for 32-bit Sparc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example /usr/src/linux/COPYING); if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * High speed xor_block operation for RAID4/5 utilizing the
 * ldd/std SPARC instructions.
 *
 * Copyright (C) 1999 Jakub Jelinek (jj@ultra.linux.cz)
 */

static void
sparc_2(unsigned long bytes, unsigned long *p1, unsigned long *p2)
{
	int lines = bytes / (sizeof (long)) / 8;

	do {
		__asm__ __volatile__(
		  "ldd [%0 + 0x00], %%g2\n\t"
		  "ldd [%0 + 0x08], %%g4\n\t"
		  "ldd [%0 + 0x10], %%o0\n\t"
		  "ldd [%0 + 0x18], %%o2\n\t"
		  "ldd [%1 + 0x00], %%o4\n\t"
		  "ldd [%1 + 0x08], %%l0\n\t"
		  "ldd [%1 + 0x10], %%l2\n\t"
		  "ldd [%1 + 0x18], %%l4\n\t"
		  "xor %%g2, %%o4, %%g2\n\t"
		  "xor %%g3, %%o5, %%g3\n\t"
		  "xor %%g4, %%l0, %%g4\n\t"
		  "xor %%g5, %%l1, %%g5\n\t"
		  "xor %%o0, %%l2, %%o0\n\t"
		  "xor %%o1, %%l3, %%o1\n\t"
		  "xor %%o2, %%l4, %%o2\n\t"
		  "xor %%o3, %%l5, %%o3\n\t"
		  "std %%g2, [%0 + 0x00]\n\t"
		  "std %%g4, [%0 + 0x08]\n\t"
		  "std %%o0, [%0 + 0x10]\n\t"
		  "std %%o2, [%0 + 0x18]\n"
		:
		: "r" (p1), "r" (p2)
		: "g2", "g3", "g4", "g5",
		  "o0", "o1", "o2", "o3", "o4", "o5",
		  "l0", "l1", "l2", "l3", "l4", "l5");
		p1 += 8;
		p2 += 8;
	} while (--lines > 0);
}

static void
sparc_3(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	unsigned long *p3)
{
	int lines = bytes / (sizeof (long)) / 8;

	do {
		__asm__ __volatile__(
		  "ldd [%0 + 0x00], %%g2\n\t"
		  "ldd [%0 + 0x08], %%g4\n\t"
		  "ldd [%0 + 0x10], %%o0\n\t"
		  "ldd [%0 + 0x18], %%o2\n\t"
		  "ldd [%1 + 0x00], %%o4\n\t"
		  "ldd [%1 + 0x08], %%l0\n\t"
		  "ldd [%1 + 0x10], %%l2\n\t"
		  "ldd [%1 + 0x18], %%l4\n\t"
		  "xor %%g2, %%o4, %%g2\n\t"
		  "xor %%g3, %%o5, %%g3\n\t"
		  "ldd [%2 + 0x00], %%o4\n\t"
		  "xor %%g4, %%l0, %%g4\n\t"
		  "xor %%g5, %%l1, %%g5\n\t"
		  "ldd [%2 + 0x08], %%l0\n\t"
		  "xor %%o0, %%l2, %%o0\n\t"
		  "xor %%o1, %%l3, %%o1\n\t"
		  "ldd [%2 + 0x10], %%l2\n\t"
		  "xor %%o2, %%l4, %%o2\n\t"
		  "xor %%o3, %%l5, %%o3\n\t"
		  "ldd [%2 + 0x18], %%l4\n\t"
		  "xor %%g2, %%o4, %%g2\n\t"
		  "xor %%g3, %%o5, %%g3\n\t"
		  "xor %%g4, %%l0, %%g4\n\t"
		  "xor %%g5, %%l1, %%g5\n\t"
		  "xor %%o0, %%l2, %%o0\n\t"
		  "xor %%o1, %%l3, %%o1\n\t"
		  "xor %%o2, %%l4, %%o2\n\t"
		  "xor %%o3, %%l5, %%o3\n\t"
		  "std %%g2, [%0 + 0x00]\n\t"
		  "std %%g4, [%0 + 0x08]\n\t"
		  "std %%o0, [%0 + 0x10]\n\t"
		  "std %%o2, [%0 + 0x18]\n"
		:
		: "r" (p1), "r" (p2), "r" (p3)
		: "g2", "g3", "g4", "g5",
		  "o0", "o1", "o2", "o3", "o4", "o5",
		  "l0", "l1", "l2", "l3", "l4", "l5");
		p1 += 8;
		p2 += 8;
		p3 += 8;
	} while (--lines > 0);
}

static void
sparc_4(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	unsigned long *p3, unsigned long *p4)
{
	int lines = bytes / (sizeof (long)) / 8;

	do {
		__asm__ __volatile__(
		  "ldd [%0 + 0x00], %%g2\n\t"
		  "ldd [%0 + 0x08], %%g4\n\t"
		  "ldd [%0 + 0x10], %%o0\n\t"
		  "ldd [%0 + 0x18], %%o2\n\t"
		  "ldd [%1 + 0x00], %%o4\n\t"
		  "ldd [%1 + 0x08], %%l0\n\t"
		  "ldd [%1 + 0x10], %%l2\n\t"
		  "ldd [%1 + 0x18], %%l4\n\t"
		  "xor %%g2, %%o4, %%g2\n\t"
		  "xor %%g3, %%o5, %%g3\n\t"
		  "ldd [%2 + 0x00], %%o4\n\t"
		  "xor %%g4, %%l0, %%g4\n\t"
		  "xor %%g5, %%l1, %%g5\n\t"
		  "ldd [%2 + 0x08], %%l0\n\t"
		  "xor %%o0, %%l2, %%o0\n\t"
		  "xor %%o1, %%l3, %%o1\n\t"
		  "ldd [%2 + 0x10], %%l2\n\t"
		  "xor %%o2, %%l4, %%o2\n\t"
		  "xor %%o3, %%l5, %%o3\n\t"
		  "ldd [%2 + 0x18], %%l4\n\t"
		  "xor %%g2, %%o4, %%g2\n\t"
		  "xor %%g3, %%o5, %%g3\n\t"
		  "ldd [%3 + 0x00], %%o4\n\t"
		  "xor %%g4, %%l0, %%g4\n\t"
		  "xor %%g5, %%l1, %%g5\n\t"
		  "ldd [%3 + 0x08], %%l0\n\t"
		  "xor %%o0, %%l2, %%o0\n\t"
		  "xor %%o1, %%l3, %%o1\n\t"
		  "ldd [%3 + 0x10], %%l2\n\t"
		  "xor %%o2, %%l4, %%o2\n\t"
		  "xor %%o3, %%l5, %%o3\n\t"
		  "ldd [%3 + 0x18], %%l4\n\t"
		  "xor %%g2, %%o4, %%g2\n\t"
		  "xor %%g3, %%o5, %%g3\n\t"
		  "xor %%g4, %%l0, %%g4\n\t"
		  "xor %%g5, %%l1, %%g5\n\t"
		  "xor %%o0, %%l2, %%o0\n\t"
		  "xor %%o1, %%l3, %%o1\n\t"
		  "xor %%o2, %%l4, %%o2\n\t"
		  "xor %%o3, %%l5, %%o3\n\t"
		  "std %%g2, [%0 + 0x00]\n\t"
		  "std %%g4, [%0 + 0x08]\n\t"
		  "std %%o0, [%0 + 0x10]\n\t"
		  "std %%o2, [%0 + 0x18]\n"
		:
		: "r" (p1), "r" (p2), "r" (p3), "r" (p4)
		: "g2", "g3", "g4", "g5",
		  "o0", "o1", "o2", "o3", "o4", "o5",
		  "l0", "l1", "l2", "l3", "l4", "l5");
		p1 += 8;
		p2 += 8;
		p3 += 8;
		p4 += 8;
	} while (--lines > 0);
}

static void
sparc_5(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	unsigned long *p3, unsigned long *p4, unsigned long *p5)
{
	int lines = bytes / (sizeof (long)) / 8;

	do {
		__asm__ __volatile__(
		  "ldd [%0 + 0x00], %%g2\n\t"
		  "ldd [%0 + 0x08], %%g4\n\t"
		  "ldd [%0 + 0x10], %%o0\n\t"
		  "ldd [%0 + 0x18], %%o2\n\t"
		  "ldd [%1 + 0x00], %%o4\n\t"
		  "ldd [%1 + 0x08], %%l0\n\t"
		  "ldd [%1 + 0x10], %%l2\n\t"
		  "ldd [%1 + 0x18], %%l4\n\t"
		  "xor %%g2, %%o4, %%g2\n\t"
		  "xor %%g3, %%o5, %%g3\n\t"
		  "ldd [%2 + 0x00], %%o4\n\t"
		  "xor %%g4, %%l0, %%g4\n\t"
		  "xor %%g5, %%l1, %%g5\n\t"
		  "ldd [%2 + 0x08], %%l0\n\t"
		  "xor %%o0, %%l2, %%o0\n\t"
		  "xor %%o1, %%l3, %%o1\n\t"
		  "ldd [%2 + 0x10], %%l2\n\t"
		  "xor %%o2, %%l4, %%o2\n\t"
		  "xor %%o3, %%l5, %%o3\n\t"
		  "ldd [%2 + 0x18], %%l4\n\t"
		  "xor %%g2, %%o4, %%g2\n\t"
		  "xor %%g3, %%o5, %%g3\n\t"
		  "ldd [%3 + 0x00], %%o4\n\t"
		  "xor %%g4, %%l0, %%g4\n\t"
		  "xor %%g5, %%l1, %%g5\n\t"
		  "ldd [%3 + 0x08], %%l0\n\t"
		  "xor %%o0, %%l2, %%o0\n\t"
		  "xor %%o1, %%l3, %%o1\n\t"
		  "ldd [%3 + 0x10], %%l2\n\t"
		  "xor %%o2, %%l4, %%o2\n\t"
		  "xor %%o3, %%l5, %%o3\n\t"
		  "ldd [%3 + 0x18], %%l4\n\t"
		  "xor %%g2, %%o4, %%g2\n\t"
		  "xor %%g3, %%o5, %%g3\n\t"
		  "ldd [%4 + 0x00], %%o4\n\t"
		  "xor %%g4, %%l0, %%g4\n\t"
		  "xor %%g5, %%l1, %%g5\n\t"
		  "ldd [%4 + 0x08], %%l0\n\t"
		  "xor %%o0, %%l2, %%o0\n\t"
		  "xor %%o1, %%l3, %%o1\n\t"
		  "ldd [%4 + 0x10], %%l2\n\t"
		  "xor %%o2, %%l4, %%o2\n\t"
		  "xor %%o3, %%l5, %%o3\n\t"
		  "ldd [%4 + 0x18], %%l4\n\t"
		  "xor %%g2, %%o4, %%g2\n\t"
		  "xor %%g3, %%o5, %%g3\n\t"
		  "xor %%g4, %%l0, %%g4\n\t"
		  "xor %%g5, %%l1, %%g5\n\t"
		  "xor %%o0, %%l2, %%o0\n\t"
		  "xor %%o1, %%l3, %%o1\n\t"
		  "xor %%o2, %%l4, %%o2\n\t"
		  "xor %%o3, %%l5, %%o3\n\t"
		  "std %%g2, [%0 + 0x00]\n\t"
		  "std %%g4, [%0 + 0x08]\n\t"
		  "std %%o0, [%0 + 0x10]\n\t"
		  "std %%o2, [%0 + 0x18]\n"
		:
		: "r" (p1), "r" (p2), "r" (p3), "r" (p4), "r" (p5)
		: "g2", "g3", "g4", "g5",
		  "o0", "o1", "o2", "o3", "o4", "o5",
		  "l0", "l1", "l2", "l3", "l4", "l5");
		p1 += 8;
		p2 += 8;
		p3 += 8;
		p4 += 8;
		p5 += 8;
	} while (--lines > 0);
}

static struct xor_block_template xor_block_SPARC = {
	.name	= "SPARC",
	.do_2	= sparc_2,
	.do_3	= sparc_3,
	.do_4	= sparc_4,
	.do_5	= sparc_5,
};

/* For grins, also test the generic routines.  */
#include <asm-generic/xor.h>

#undef XOR_TRY_TEMPLATES
#define XOR_TRY_TEMPLATES				\
	do {						\
		xor_speed(&xor_block_8regs);		\
		xor_speed(&xor_block_32regs);		\
		xor_speed(&xor_block_SPARC);		\
	} while (0)
