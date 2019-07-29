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
#include <asm/neon.h>

#ifdef CONFIG_KERNEL_MODE_NEON

extern struct xor_block_template const xor_block_inner_neon;

static void
xor_neon_2(unsigned long bytes, unsigned long *p1, unsigned long *p2)
{
	kernel_neon_begin();
	xor_block_inner_neon.do_2(bytes, p1, p2);
	kernel_neon_end();
}

static void
xor_neon_3(unsigned long bytes, unsigned long *p1, unsigned long *p2,
		unsigned long *p3)
{
	kernel_neon_begin();
	xor_block_inner_neon.do_3(bytes, p1, p2, p3);
	kernel_neon_end();
}

static void
xor_neon_4(unsigned long bytes, unsigned long *p1, unsigned long *p2,
		unsigned long *p3, unsigned long *p4)
{
	kernel_neon_begin();
	xor_block_inner_neon.do_4(bytes, p1, p2, p3, p4);
	kernel_neon_end();
}

static void
xor_neon_5(unsigned long bytes, unsigned long *p1, unsigned long *p2,
		unsigned long *p3, unsigned long *p4, unsigned long *p5)
{
	kernel_neon_begin();
	xor_block_inner_neon.do_5(bytes, p1, p2, p3, p4, p5);
	kernel_neon_end();
}

static struct xor_block_template xor_block_arm64 = {
	.name   = "arm64_neon",
	.do_2   = xor_neon_2,
	.do_3   = xor_neon_3,
	.do_4   = xor_neon_4,
	.do_5	= xor_neon_5
};
#undef XOR_TRY_TEMPLATES
#define XOR_TRY_TEMPLATES           \
	do {        \
		xor_speed(&xor_block_8regs);    \
		xor_speed(&xor_block_32regs);    \
		if (cpu_has_neon()) { \
			xor_speed(&xor_block_arm64);\
		} \
	} while (0)

#endif /* ! CONFIG_KERNEL_MODE_NEON */
