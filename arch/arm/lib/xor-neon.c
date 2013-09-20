/*
 * linux/arch/arm/lib/xor-neon.c
 *
 * Copyright (C) 2013 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/raid/xor.h>
#include <linux/module.h>

MODULE_LICENSE("GPL");

#ifndef __ARM_NEON__
#error You should compile this file with '-mfloat-abi=softfp -mfpu=neon'
#endif

/*
 * Pull in the reference implementations while instructing GCC (through
 * -ftree-vectorize) to attempt to exploit implicit parallelism and emit
 * NEON instructions.
 */
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
#pragma GCC optimize "tree-vectorize"
#else
/*
 * While older versions of GCC do not generate incorrect code, they fail to
 * recognize the parallel nature of these functions, and emit plain ARM code,
 * which is known to be slower than the optimized ARM code in asm-arm/xor.h.
 */
#warning This code requires at least version 4.6 of GCC
#endif

#pragma GCC diagnostic ignored "-Wunused-variable"
#include <asm-generic/xor.h>

struct xor_block_template const xor_block_neon_inner = {
	.name	= "__inner_neon__",
	.do_2	= xor_8regs_2,
	.do_3	= xor_8regs_3,
	.do_4	= xor_8regs_4,
	.do_5	= xor_8regs_5,
};
EXPORT_SYMBOL(xor_block_neon_inner);
