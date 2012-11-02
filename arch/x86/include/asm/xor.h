#ifdef CONFIG_KMEMCHECK
/* kmemcheck doesn't handle MMX/SSE/SSE2 instructions */
# include <asm-generic/xor.h>
#elif !defined(_ASM_X86_XOR_H)
#define _ASM_X86_XOR_H

/*
 * Optimized RAID-5 checksumming functions for SSE.
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
 * Cache avoiding checksumming functions utilizing KNI instructions
 * Copyright (C) 1999 Zach Brown (with obvious credit due Ingo)
 */

/*
 * Based on
 * High-speed RAID5 checksumming functions utilizing SSE instructions.
 * Copyright (C) 1998 Ingo Molnar.
 */

/*
 * x86-64 changes / gcc fixes from Andi Kleen.
 * Copyright 2002 Andi Kleen, SuSE Labs.
 *
 * This hasn't been optimized for the hammer yet, but there are likely
 * no advantages to be gotten from x86-64 here anyways.
 */

#include <asm/i387.h>

#ifdef CONFIG_X86_32
/* reduce register pressure */
# define XOR_CONSTANT_CONSTRAINT "i"
#else
# define XOR_CONSTANT_CONSTRAINT "re"
#endif

#define OFFS(x)		"16*("#x")"
#define PF_OFFS(x)	"256+16*("#x")"
#define PF0(x)		"	prefetchnta "PF_OFFS(x)"(%[p1])		;\n"
#define LD(x, y)	"	movaps "OFFS(x)"(%[p1]), %%xmm"#y"	;\n"
#define ST(x, y)	"	movaps %%xmm"#y", "OFFS(x)"(%[p1])	;\n"
#define PF1(x)		"	prefetchnta "PF_OFFS(x)"(%[p2])		;\n"
#define PF2(x)		"	prefetchnta "PF_OFFS(x)"(%[p3])		;\n"
#define PF3(x)		"	prefetchnta "PF_OFFS(x)"(%[p4])		;\n"
#define PF4(x)		"	prefetchnta "PF_OFFS(x)"(%[p5])		;\n"
#define XO1(x, y)	"	xorps "OFFS(x)"(%[p2]), %%xmm"#y"	;\n"
#define XO2(x, y)	"	xorps "OFFS(x)"(%[p3]), %%xmm"#y"	;\n"
#define XO3(x, y)	"	xorps "OFFS(x)"(%[p4]), %%xmm"#y"	;\n"
#define XO4(x, y)	"	xorps "OFFS(x)"(%[p5]), %%xmm"#y"	;\n"

static void
xor_sse_2(unsigned long bytes, unsigned long *p1, unsigned long *p2)
{
	unsigned long lines = bytes >> 8;

	kernel_fpu_begin();

	asm volatile(
#undef BLOCK
#define BLOCK(i)					\
		LD(i, 0)				\
			LD(i + 1, 1)			\
		PF1(i)					\
				PF1(i + 2)		\
				LD(i + 2, 2)		\
					LD(i + 3, 3)	\
		PF0(i + 4)				\
				PF0(i + 6)		\
		XO1(i, 0)				\
			XO1(i + 1, 1)			\
				XO1(i + 2, 2)		\
					XO1(i + 3, 3)	\
		ST(i, 0)				\
			ST(i + 1, 1)			\
				ST(i + 2, 2)		\
					ST(i + 3, 3)	\


		PF0(0)
				PF0(2)

	" .align 32			;\n"
	" 1:                            ;\n"

		BLOCK(0)
		BLOCK(4)
		BLOCK(8)
		BLOCK(12)

	"       add %[inc], %[p1]       ;\n"
	"       add %[inc], %[p2]       ;\n"
	"       dec %[cnt]              ;\n"
	"       jnz 1b                  ;\n"
	: [cnt] "+r" (lines),
	  [p1] "+r" (p1), [p2] "+r" (p2)
	: [inc] XOR_CONSTANT_CONSTRAINT (256UL)
	: "memory");

	kernel_fpu_end();
}

static void
xor_sse_3(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	  unsigned long *p3)
{
	unsigned long lines = bytes >> 8;

	kernel_fpu_begin();

	asm volatile(
#undef BLOCK
#define BLOCK(i) \
		PF1(i)					\
				PF1(i + 2)		\
		LD(i, 0)				\
			LD(i + 1, 1)			\
				LD(i + 2, 2)		\
					LD(i + 3, 3)	\
		PF2(i)					\
				PF2(i + 2)		\
		PF0(i + 4)				\
				PF0(i + 6)		\
		XO1(i, 0)				\
			XO1(i + 1, 1)			\
				XO1(i + 2, 2)		\
					XO1(i + 3, 3)	\
		XO2(i, 0)				\
			XO2(i + 1, 1)			\
				XO2(i + 2, 2)		\
					XO2(i + 3, 3)	\
		ST(i, 0)				\
			ST(i + 1, 1)			\
				ST(i + 2, 2)		\
					ST(i + 3, 3)	\


		PF0(0)
				PF0(2)

	" .align 32			;\n"
	" 1:                            ;\n"

		BLOCK(0)
		BLOCK(4)
		BLOCK(8)
		BLOCK(12)

	"       add %[inc], %[p1]       ;\n"
	"       add %[inc], %[p2]       ;\n"
	"       add %[inc], %[p3]       ;\n"
	"       dec %[cnt]              ;\n"
	"       jnz 1b                  ;\n"
	: [cnt] "+r" (lines),
	  [p1] "+r" (p1), [p2] "+r" (p2), [p3] "+r" (p3)
	: [inc] XOR_CONSTANT_CONSTRAINT (256UL)
	: "memory");

	kernel_fpu_end();
}

static void
xor_sse_4(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	  unsigned long *p3, unsigned long *p4)
{
	unsigned long lines = bytes >> 8;

	kernel_fpu_begin();

	asm volatile(
#undef BLOCK
#define BLOCK(i) \
		PF1(i)					\
				PF1(i + 2)		\
		LD(i, 0)				\
			LD(i + 1, 1)			\
				LD(i + 2, 2)		\
					LD(i + 3, 3)	\
		PF2(i)					\
				PF2(i + 2)		\
		XO1(i, 0)				\
			XO1(i + 1, 1)			\
				XO1(i + 2, 2)		\
					XO1(i + 3, 3)	\
		PF3(i)					\
				PF3(i + 2)		\
		PF0(i + 4)				\
				PF0(i + 6)		\
		XO2(i, 0)				\
			XO2(i + 1, 1)			\
				XO2(i + 2, 2)		\
					XO2(i + 3, 3)	\
		XO3(i, 0)				\
			XO3(i + 1, 1)			\
				XO3(i + 2, 2)		\
					XO3(i + 3, 3)	\
		ST(i, 0)				\
			ST(i + 1, 1)			\
				ST(i + 2, 2)		\
					ST(i + 3, 3)	\


		PF0(0)
				PF0(2)

	" .align 32			;\n"
	" 1:                            ;\n"

		BLOCK(0)
		BLOCK(4)
		BLOCK(8)
		BLOCK(12)

	"       add %[inc], %[p1]       ;\n"
	"       add %[inc], %[p2]       ;\n"
	"       add %[inc], %[p3]       ;\n"
	"       add %[inc], %[p4]       ;\n"
	"       dec %[cnt]              ;\n"
	"       jnz 1b                  ;\n"
	: [cnt] "+r" (lines), [p1] "+r" (p1),
	  [p2] "+r" (p2), [p3] "+r" (p3), [p4] "+r" (p4)
	: [inc] XOR_CONSTANT_CONSTRAINT (256UL)
	: "memory");

	kernel_fpu_end();
}

static void
xor_sse_5(unsigned long bytes, unsigned long *p1, unsigned long *p2,
	  unsigned long *p3, unsigned long *p4, unsigned long *p5)
{
	unsigned long lines = bytes >> 8;

	kernel_fpu_begin();

	asm volatile(
#undef BLOCK
#define BLOCK(i) \
		PF1(i)					\
				PF1(i + 2)		\
		LD(i, 0)				\
			LD(i + 1, 1)			\
				LD(i + 2, 2)		\
					LD(i + 3, 3)	\
		PF2(i)					\
				PF2(i + 2)		\
		XO1(i, 0)				\
			XO1(i + 1, 1)			\
				XO1(i + 2, 2)		\
					XO1(i + 3, 3)	\
		PF3(i)					\
				PF3(i + 2)		\
		XO2(i, 0)				\
			XO2(i + 1, 1)			\
				XO2(i + 2, 2)		\
					XO2(i + 3, 3)	\
		PF4(i)					\
				PF4(i + 2)		\
		PF0(i + 4)				\
				PF0(i + 6)		\
		XO3(i, 0)				\
			XO3(i + 1, 1)			\
				XO3(i + 2, 2)		\
					XO3(i + 3, 3)	\
		XO4(i, 0)				\
			XO4(i + 1, 1)			\
				XO4(i + 2, 2)		\
					XO4(i + 3, 3)	\
		ST(i, 0)				\
			ST(i + 1, 1)			\
				ST(i + 2, 2)		\
					ST(i + 3, 3)	\


		PF0(0)
				PF0(2)

	" .align 32			;\n"
	" 1:                            ;\n"

		BLOCK(0)
		BLOCK(4)
		BLOCK(8)
		BLOCK(12)

	"       add %[inc], %[p1]       ;\n"
	"       add %[inc], %[p2]       ;\n"
	"       add %[inc], %[p3]       ;\n"
	"       add %[inc], %[p4]       ;\n"
	"       add %[inc], %[p5]       ;\n"
	"       dec %[cnt]              ;\n"
	"       jnz 1b                  ;\n"
	: [cnt] "+r" (lines), [p1] "+r" (p1), [p2] "+r" (p2),
	  [p3] "+r" (p3), [p4] "+r" (p4), [p5] "+r" (p5)
	: [inc] XOR_CONSTANT_CONSTRAINT (256UL)
	: "memory");

	kernel_fpu_end();
}

#undef LD
#undef XO1
#undef XO2
#undef XO3
#undef XO4
#undef ST
#undef BLOCK

#undef XOR_CONSTANT_CONSTRAINT

#ifdef CONFIG_X86_32
# include <asm/xor_32.h>
#else
# include <asm/xor_64.h>
#endif

#endif /* _ASM_X86_XOR_H */
