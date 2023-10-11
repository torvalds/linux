/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Rivos Inc. */

#include <linux/linkage.h>
#include <asm/asm.h>

	.text

/* void __riscv_copy_words_unaligned(void *, const void *, size_t) */
/* Performs a memcpy without aligning buffers, using word loads and stores. */
/* Note: The size is truncated to a multiple of 8 * SZREG */
ENTRY(__riscv_copy_words_unaligned)
	andi  a4, a2, ~((8*SZREG)-1)
	beqz  a4, 2f
	add   a3, a1, a4
1:
	REG_L a4,       0(a1)
	REG_L a5,   SZREG(a1)
	REG_L a6, 2*SZREG(a1)
	REG_L a7, 3*SZREG(a1)
	REG_L t0, 4*SZREG(a1)
	REG_L t1, 5*SZREG(a1)
	REG_L t2, 6*SZREG(a1)
	REG_L t3, 7*SZREG(a1)
	REG_S a4,       0(a0)
	REG_S a5,   SZREG(a0)
	REG_S a6, 2*SZREG(a0)
	REG_S a7, 3*SZREG(a0)
	REG_S t0, 4*SZREG(a0)
	REG_S t1, 5*SZREG(a0)
	REG_S t2, 6*SZREG(a0)
	REG_S t3, 7*SZREG(a0)
	addi  a0, a0, 8*SZREG
	addi  a1, a1, 8*SZREG
	bltu  a1, a3, 1b

2:
	ret
END(__riscv_copy_words_unaligned)

/* void __riscv_copy_bytes_unaligned(void *, const void *, size_t) */
/* Performs a memcpy without aligning buffers, using only byte accesses. */
/* Note: The size is truncated to a multiple of 8 */
ENTRY(__riscv_copy_bytes_unaligned)
	andi a4, a2, ~(8-1)
	beqz a4, 2f
	add  a3, a1, a4
1:
	lb   a4, 0(a1)
	lb   a5, 1(a1)
	lb   a6, 2(a1)
	lb   a7, 3(a1)
	lb   t0, 4(a1)
	lb   t1, 5(a1)
	lb   t2, 6(a1)
	lb   t3, 7(a1)
	sb   a4, 0(a0)
	sb   a5, 1(a0)
	sb   a6, 2(a0)
	sb   a7, 3(a0)
	sb   t0, 4(a0)
	sb   t1, 5(a0)
	sb   t2, 6(a0)
	sb   t3, 7(a0)
	addi a0, a0, 8
	addi a1, a1, 8
	bltu a1, a3, 1b

2:
	ret
END(__riscv_copy_bytes_unaligned)
