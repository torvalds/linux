#ifndef _ASM_X86_XOR_AVX_H
#define _ASM_X86_XOR_AVX_H

/*
 * Optimized RAID-5 checksumming functions for AVX
 *
 * Copyright (C) 2012 Intel Corporation
 * Author: Jim Kukunas <james.t.kukunas@linux.intel.com>
 *
 * Based on Ingo Molnar and Zach Brown's respective MMX and SSE routines
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#ifdef CONFIG_AS_AVX

#include <linux/compiler.h>
#include <asm/i387.h>

#define BLOCK4(i) \
		BLOCK(32 * i, 0) \
		BLOCK(32 * (i + 1), 1) \
		BLOCK(32 * (i + 2), 2) \
		BLOCK(32 * (i + 3), 3)

#define BLOCK16() \
		BLOCK4(0) \
		BLOCK4(4) \
		BLOCK4(8) \
		BLOCK4(12)

static void xor_avx_2(unsigned long bytes, unsigned long *p0, unsigned long *p1)
{
	unsigned long lines = bytes >> 9;

	kernel_fpu_begin();

	while (lines--) {
#undef BLOCK
#define BLOCK(i, reg) \
do { \
	asm volatile("vmovdqa %0, %%ymm" #reg : : "m" (p1[i / sizeof(*p1)])); \
	asm volatile("vxorps %0, %%ymm" #reg ", %%ymm"  #reg : : \
		"m" (p0[i / sizeof(*p0)])); \
	asm volatile("vmovdqa %%ymm" #reg ", %0" : \
		"=m" (p0[i / sizeof(*p0)])); \
} while (0);

		BLOCK16()

		p0 = (unsigned long *)((uintptr_t)p0 + 512);
		p1 = (unsigned long *)((uintptr_t)p1 + 512);
	}

	kernel_fpu_end();
}

static void xor_avx_3(unsigned long bytes, unsigned long *p0, unsigned long *p1,
	unsigned long *p2)
{
	unsigned long lines = bytes >> 9;

	kernel_fpu_begin();

	while (lines--) {
#undef BLOCK
#define BLOCK(i, reg) \
do { \
	asm volatile("vmovdqa %0, %%ymm" #reg : : "m" (p2[i / sizeof(*p2)])); \
	asm volatile("vxorps %0, %%ymm" #reg ", %%ymm" #reg : : \
		"m" (p1[i / sizeof(*p1)])); \
	asm volatile("vxorps %0, %%ymm" #reg ", %%ymm" #reg : : \
		"m" (p0[i / sizeof(*p0)])); \
	asm volatile("vmovdqa %%ymm" #reg ", %0" : \
		"=m" (p0[i / sizeof(*p0)])); \
} while (0);

		BLOCK16()

		p0 = (unsigned long *)((uintptr_t)p0 + 512);
		p1 = (unsigned long *)((uintptr_t)p1 + 512);
		p2 = (unsigned long *)((uintptr_t)p2 + 512);
	}

	kernel_fpu_end();
}

static void xor_avx_4(unsigned long bytes, unsigned long *p0, unsigned long *p1,
	unsigned long *p2, unsigned long *p3)
{
	unsigned long lines = bytes >> 9;

	kernel_fpu_begin();

	while (lines--) {
#undef BLOCK
#define BLOCK(i, reg) \
do { \
	asm volatile("vmovdqa %0, %%ymm" #reg : : "m" (p3[i / sizeof(*p3)])); \
	asm volatile("vxorps %0, %%ymm" #reg ", %%ymm" #reg : : \
		"m" (p2[i / sizeof(*p2)])); \
	asm volatile("vxorps %0, %%ymm" #reg ", %%ymm" #reg : : \
		"m" (p1[i / sizeof(*p1)])); \
	asm volatile("vxorps %0, %%ymm" #reg ", %%ymm" #reg : : \
		"m" (p0[i / sizeof(*p0)])); \
	asm volatile("vmovdqa %%ymm" #reg ", %0" : \
		"=m" (p0[i / sizeof(*p0)])); \
} while (0);

		BLOCK16();

		p0 = (unsigned long *)((uintptr_t)p0 + 512);
		p1 = (unsigned long *)((uintptr_t)p1 + 512);
		p2 = (unsigned long *)((uintptr_t)p2 + 512);
		p3 = (unsigned long *)((uintptr_t)p3 + 512);
	}

	kernel_fpu_end();
}

static void xor_avx_5(unsigned long bytes, unsigned long *p0, unsigned long *p1,
	unsigned long *p2, unsigned long *p3, unsigned long *p4)
{
	unsigned long lines = bytes >> 9;

	kernel_fpu_begin();

	while (lines--) {
#undef BLOCK
#define BLOCK(i, reg) \
do { \
	asm volatile("vmovdqa %0, %%ymm" #reg : : "m" (p4[i / sizeof(*p4)])); \
	asm volatile("vxorps %0, %%ymm" #reg ", %%ymm" #reg : : \
		"m" (p3[i / sizeof(*p3)])); \
	asm volatile("vxorps %0, %%ymm" #reg ", %%ymm" #reg : : \
		"m" (p2[i / sizeof(*p2)])); \
	asm volatile("vxorps %0, %%ymm" #reg ", %%ymm" #reg : : \
		"m" (p1[i / sizeof(*p1)])); \
	asm volatile("vxorps %0, %%ymm" #reg ", %%ymm" #reg : : \
		"m" (p0[i / sizeof(*p0)])); \
	asm volatile("vmovdqa %%ymm" #reg ", %0" : \
		"=m" (p0[i / sizeof(*p0)])); \
} while (0);

		BLOCK16()

		p0 = (unsigned long *)((uintptr_t)p0 + 512);
		p1 = (unsigned long *)((uintptr_t)p1 + 512);
		p2 = (unsigned long *)((uintptr_t)p2 + 512);
		p3 = (unsigned long *)((uintptr_t)p3 + 512);
		p4 = (unsigned long *)((uintptr_t)p4 + 512);
	}

	kernel_fpu_end();
}

static struct xor_block_template xor_block_avx = {
	.name = "avx",
	.do_2 = xor_avx_2,
	.do_3 = xor_avx_3,
	.do_4 = xor_avx_4,
	.do_5 = xor_avx_5,
};

#define AVX_XOR_SPEED \
do { \
	if (cpu_has_avx) \
		xor_speed(&xor_block_avx); \
} while (0)

#define AVX_SELECT(FASTEST) \
	(cpu_has_avx ? &xor_block_avx : FASTEST)

#else

#define AVX_XOR_SPEED {}

#define AVX_SELECT(FASTEST) (FASTEST)

#endif
#endif
