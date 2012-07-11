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

#define ALIGN32 __aligned(32)

#define YMM_SAVED_REGS 4

#define YMMS_SAVE \
do { \
	preempt_disable(); \
	cr0 = read_cr0(); \
	clts(); \
	asm volatile("vmovaps %%ymm0, %0" : "=m" (ymm_save[0]) : : "memory"); \
	asm volatile("vmovaps %%ymm1, %0" : "=m" (ymm_save[32]) : : "memory"); \
	asm volatile("vmovaps %%ymm2, %0" : "=m" (ymm_save[64]) : : "memory"); \
	asm volatile("vmovaps %%ymm3, %0" : "=m" (ymm_save[96]) : : "memory"); \
} while (0);

#define YMMS_RESTORE \
do { \
	asm volatile("sfence" : : : "memory"); \
	asm volatile("vmovaps %0, %%ymm3" : : "m" (ymm_save[96])); \
	asm volatile("vmovaps %0, %%ymm2" : : "m" (ymm_save[64])); \
	asm volatile("vmovaps %0, %%ymm1" : : "m" (ymm_save[32])); \
	asm volatile("vmovaps %0, %%ymm0" : : "m" (ymm_save[0])); \
	write_cr0(cr0); \
	preempt_enable(); \
} while (0);

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
	unsigned long cr0, lines = bytes >> 9;
	char ymm_save[32 * YMM_SAVED_REGS] ALIGN32;

	YMMS_SAVE

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

	YMMS_RESTORE
}

static void xor_avx_3(unsigned long bytes, unsigned long *p0, unsigned long *p1,
	unsigned long *p2)
{
	unsigned long cr0, lines = bytes >> 9;
	char ymm_save[32 * YMM_SAVED_REGS] ALIGN32;

	YMMS_SAVE

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

	YMMS_RESTORE
}

static void xor_avx_4(unsigned long bytes, unsigned long *p0, unsigned long *p1,
	unsigned long *p2, unsigned long *p3)
{
	unsigned long cr0, lines = bytes >> 9;
	char ymm_save[32 * YMM_SAVED_REGS] ALIGN32;

	YMMS_SAVE

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

	YMMS_RESTORE
}

static void xor_avx_5(unsigned long bytes, unsigned long *p0, unsigned long *p1,
	unsigned long *p2, unsigned long *p3, unsigned long *p4)
{
	unsigned long cr0, lines = bytes >> 9;
	char ymm_save[32 * YMM_SAVED_REGS] ALIGN32;

	YMMS_SAVE

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

	YMMS_RESTORE
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
