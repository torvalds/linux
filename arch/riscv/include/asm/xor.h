/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2021 SiFive
 */

#include <linux/hardirq.h>
#include <asm-generic/xor.h>
#ifdef CONFIG_RISCV_ISA_V
#include <asm/vector.h>
#include <asm/switch_to.h>
#include <asm/asm-prototypes.h>

static void xor_vector_2(unsigned long bytes, unsigned long *__restrict p1,
			 const unsigned long *__restrict p2)
{
	kernel_vector_begin();
	xor_regs_2_(bytes, p1, p2);
	kernel_vector_end();
}

static void xor_vector_3(unsigned long bytes, unsigned long *__restrict p1,
			 const unsigned long *__restrict p2,
			 const unsigned long *__restrict p3)
{
	kernel_vector_begin();
	xor_regs_3_(bytes, p1, p2, p3);
	kernel_vector_end();
}

static void xor_vector_4(unsigned long bytes, unsigned long *__restrict p1,
			 const unsigned long *__restrict p2,
			 const unsigned long *__restrict p3,
			 const unsigned long *__restrict p4)
{
	kernel_vector_begin();
	xor_regs_4_(bytes, p1, p2, p3, p4);
	kernel_vector_end();
}

static void xor_vector_5(unsigned long bytes, unsigned long *__restrict p1,
			 const unsigned long *__restrict p2,
			 const unsigned long *__restrict p3,
			 const unsigned long *__restrict p4,
			 const unsigned long *__restrict p5)
{
	kernel_vector_begin();
	xor_regs_5_(bytes, p1, p2, p3, p4, p5);
	kernel_vector_end();
}

static struct xor_block_template xor_block_rvv = {
	.name = "rvv",
	.do_2 = xor_vector_2,
	.do_3 = xor_vector_3,
	.do_4 = xor_vector_4,
	.do_5 = xor_vector_5
};

#undef XOR_TRY_TEMPLATES
#define XOR_TRY_TEMPLATES           \
	do {        \
		xor_speed(&xor_block_8regs);    \
		xor_speed(&xor_block_32regs);    \
		if (has_vector()) { \
			xor_speed(&xor_block_rvv);\
		} \
	} while (0)
#endif
