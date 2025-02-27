/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2024 Rivos Inc. */

#include <linux/args.h>
#include <linux/linkage.h>
#include <asm/asm.h>

	.text

#define WORD_EEW 32

#define WORD_SEW CONCATENATE(e, WORD_EEW)
#define VEC_L CONCATENATE(vle, WORD_EEW).v
#define VEC_S CONCATENATE(vse, WORD_EEW).v

/* void __riscv_copy_vec_words_unaligned(void *, const void *, size_t) */
/* Performs a memcpy without aligning buffers, using word loads and stores. */
/* Note: The size is truncated to a multiple of WORD_EEW */
SYM_FUNC_START(__riscv_copy_vec_words_unaligned)
	andi  a4, a2, ~(WORD_EEW-1)
	beqz  a4, 2f
	add   a3, a1, a4
	.option push
	.option arch, +zve32x
1:
	vsetivli t0, 8, WORD_SEW, m8, ta, ma
	VEC_L v0, (a1)
	VEC_S v0, (a0)
	addi  a0, a0, WORD_EEW
	addi  a1, a1, WORD_EEW
	bltu  a1, a3, 1b

2:
	.option pop
	ret
SYM_FUNC_END(__riscv_copy_vec_words_unaligned)

/* void __riscv_copy_vec_bytes_unaligned(void *, const void *, size_t) */
/* Performs a memcpy without aligning buffers, using only byte accesses. */
/* Note: The size is truncated to a multiple of 8 */
SYM_FUNC_START(__riscv_copy_vec_bytes_unaligned)
	andi a4, a2, ~(8-1)
	beqz a4, 2f
	add  a3, a1, a4
	.option push
	.option arch, +zve32x
1:
	vsetivli t0, 8, e8, m8, ta, ma
	vle8.v v0, (a1)
	vse8.v v0, (a0)
	addi a0, a0, 8
	addi a1, a1, 8
	bltu a1, a3, 1b

2:
	.option pop
	ret
SYM_FUNC_END(__riscv_copy_vec_bytes_unaligned)
