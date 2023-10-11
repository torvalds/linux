// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LoongArch SIMD XOR operations
 *
 * Copyright (C) 2023 WANG Xuerui <git@xen0n.name>
 */

#include <linux/export.h>
#include <linux/sched.h>
#include <asm/fpu.h>
#include <asm/xor_simd.h>
#include "xor_simd.h"

#define MAKE_XOR_GLUE_2(flavor)							\
void xor_##flavor##_2(unsigned long bytes, unsigned long * __restrict p1,	\
		      const unsigned long * __restrict p2)			\
{										\
	kernel_fpu_begin();							\
	__xor_##flavor##_2(bytes, p1, p2);					\
	kernel_fpu_end();							\
}										\
EXPORT_SYMBOL_GPL(xor_##flavor##_2)

#define MAKE_XOR_GLUE_3(flavor)							\
void xor_##flavor##_3(unsigned long bytes, unsigned long * __restrict p1,	\
		      const unsigned long * __restrict p2,			\
		      const unsigned long * __restrict p3)			\
{										\
	kernel_fpu_begin();							\
	__xor_##flavor##_3(bytes, p1, p2, p3);					\
	kernel_fpu_end();							\
}										\
EXPORT_SYMBOL_GPL(xor_##flavor##_3)

#define MAKE_XOR_GLUE_4(flavor)							\
void xor_##flavor##_4(unsigned long bytes, unsigned long * __restrict p1,	\
		      const unsigned long * __restrict p2,			\
		      const unsigned long * __restrict p3,			\
		      const unsigned long * __restrict p4)			\
{										\
	kernel_fpu_begin();							\
	__xor_##flavor##_4(bytes, p1, p2, p3, p4);				\
	kernel_fpu_end();							\
}										\
EXPORT_SYMBOL_GPL(xor_##flavor##_4)

#define MAKE_XOR_GLUE_5(flavor)							\
void xor_##flavor##_5(unsigned long bytes, unsigned long * __restrict p1,	\
		      const unsigned long * __restrict p2,			\
		      const unsigned long * __restrict p3,			\
		      const unsigned long * __restrict p4,			\
		      const unsigned long * __restrict p5)			\
{										\
	kernel_fpu_begin();							\
	__xor_##flavor##_5(bytes, p1, p2, p3, p4, p5);				\
	kernel_fpu_end();							\
}										\
EXPORT_SYMBOL_GPL(xor_##flavor##_5)

#define MAKE_XOR_GLUES(flavor)		\
	MAKE_XOR_GLUE_2(flavor);	\
	MAKE_XOR_GLUE_3(flavor);	\
	MAKE_XOR_GLUE_4(flavor);	\
	MAKE_XOR_GLUE_5(flavor)

#ifdef CONFIG_CPU_HAS_LSX
MAKE_XOR_GLUES(lsx);
#endif

#ifdef CONFIG_CPU_HAS_LASX
MAKE_XOR_GLUES(lasx);
#endif
