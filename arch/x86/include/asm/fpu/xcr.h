/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_FPU_XCR_H
#define _ASM_X86_FPU_XCR_H

/*
 * MXCSR and XCR definitions:
 */

static inline void ldmxcsr(u32 mxcsr)
{
	asm volatile("ldmxcsr %0" :: "m" (mxcsr));
}

extern unsigned int mxcsr_feature_mask;

#define XCR_XFEATURE_ENABLED_MASK	0x00000000

static inline u64 xgetbv(u32 index)
{
	u32 eax, edx;

	asm volatile("xgetbv" : "=a" (eax), "=d" (edx) : "c" (index));
	return eax + ((u64)edx << 32);
}

static inline void xsetbv(u32 index, u64 value)
{
	u32 eax = value;
	u32 edx = value >> 32;

	asm volatile("xsetbv" :: "a" (eax), "d" (edx), "c" (index));
}

#endif /* _ASM_X86_FPU_XCR_H */
