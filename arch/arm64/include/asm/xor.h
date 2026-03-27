/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Authors: Jackie Liu <liuyun01@kylinos.cn>
 * Copyright (C) 2018,Tianjin KYLIN Information Technology Co., Ltd.
 */

#include <asm-generic/xor.h>
#include <asm/simd.h>

extern struct xor_block_template xor_block_arm64;
void __init xor_neon_init(void);

#define arch_xor_init arch_xor_init
static __always_inline void __init arch_xor_init(void)
{
	xor_neon_init();
	xor_register(&xor_block_8regs);
	xor_register(&xor_block_32regs);
	if (cpu_has_neon())
		xor_register(&xor_block_arm64);
}
