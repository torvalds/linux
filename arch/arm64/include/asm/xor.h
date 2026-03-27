/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm64/include/asm/xor.h
 *
 * Authors: Jackie Liu <liuyun01@kylinos.cn>
 * Copyright (C) 2018,Tianjin KYLIN Information Technology Co., Ltd.
 */

#include <linux/hardirq.h>
#include <asm-generic/xor.h>
#include <asm/hwcap.h>
#include <asm/simd.h>

#ifdef CONFIG_KERNEL_MODE_NEON

extern struct xor_block_template xor_block_inner_neon __ro_after_init;

static void
xor_neon_2(unsigned long bytes, unsigned long * __restrict p1,
	   const unsigned long * __restrict p2)
{
	scoped_ksimd()
		xor_block_inner_neon.do_2(bytes, p1, p2);
}

static void
xor_neon_3(unsigned long bytes, unsigned long * __restrict p1,
	   const unsigned long * __restrict p2,
	   const unsigned long * __restrict p3)
{
	scoped_ksimd()
		xor_block_inner_neon.do_3(bytes, p1, p2, p3);
}

static void
xor_neon_4(unsigned long bytes, unsigned long * __restrict p1,
	   const unsigned long * __restrict p2,
	   const unsigned long * __restrict p3,
	   const unsigned long * __restrict p4)
{
	scoped_ksimd()
		xor_block_inner_neon.do_4(bytes, p1, p2, p3, p4);
}

static void
xor_neon_5(unsigned long bytes, unsigned long * __restrict p1,
	   const unsigned long * __restrict p2,
	   const unsigned long * __restrict p3,
	   const unsigned long * __restrict p4,
	   const unsigned long * __restrict p5)
{
	scoped_ksimd()
		xor_block_inner_neon.do_5(bytes, p1, p2, p3, p4, p5);
}

static struct xor_block_template xor_block_arm64 = {
	.name   = "arm64_neon",
	.do_2   = xor_neon_2,
	.do_3   = xor_neon_3,
	.do_4   = xor_neon_4,
	.do_5	= xor_neon_5
};

#define arch_xor_init arch_xor_init
static __always_inline void __init arch_xor_init(void)
{
	xor_register(&xor_block_8regs);
	xor_register(&xor_block_32regs);
	if (cpu_has_neon())
		xor_register(&xor_block_arm64);
}

#endif /* ! CONFIG_KERNEL_MODE_NEON */
