/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Just-In-Time compiler for BPF filters on MIPS
 *
 * Copyright (c) 2014 Imagination Technologies Ltd.
 * Author: Markos Chandras <markos.chandras@imgtec.com>
 */

#ifndef BPF_JIT_MIPS_OP_H
#define BPF_JIT_MIPS_OP_H

/* Registers used by JIT */
#define MIPS_R_ZERO	0
#define MIPS_R_V0	2
#define MIPS_R_A0	4
#define MIPS_R_A1	5
#define MIPS_R_T4	12
#define MIPS_R_T5	13
#define MIPS_R_T6	14
#define MIPS_R_T7	15
#define MIPS_R_S0	16
#define MIPS_R_S1	17
#define MIPS_R_S2	18
#define MIPS_R_S3	19
#define MIPS_R_S4	20
#define MIPS_R_S5	21
#define MIPS_R_S6	22
#define MIPS_R_S7	23
#define MIPS_R_SP	29
#define MIPS_R_RA	31

/* Conditional codes */
#define MIPS_COND_EQ	0x1
#define MIPS_COND_GE	(0x1 << 1)
#define MIPS_COND_GT	(0x1 << 2)
#define MIPS_COND_NE	(0x1 << 3)
#define MIPS_COND_ALL	(0x1 << 4)
/* Conditionals on X register or K immediate */
#define MIPS_COND_X	(0x1 << 5)
#define MIPS_COND_K	(0x1 << 6)

#define r_ret	MIPS_R_V0

/*
 * Use 2 scratch registers to avoid pipeline interlocks.
 * There is no overhead during epilogue and prologue since
 * any of the $s0-$s6 registers will only be preserved if
 * they are going to actually be used.
 */
#define r_skb_hl	MIPS_R_S0 /* skb header length */
#define r_skb_data	MIPS_R_S1 /* skb actual data */
#define r_off		MIPS_R_S2
#define r_A		MIPS_R_S3
#define r_X		MIPS_R_S4
#define r_skb		MIPS_R_S5
#define r_M		MIPS_R_S6
#define r_skb_len	MIPS_R_S7
#define r_s0		MIPS_R_T4 /* scratch reg 1 */
#define r_s1		MIPS_R_T5 /* scratch reg 2 */
#define r_tmp_imm	MIPS_R_T6 /* No need to preserve this */
#define r_tmp		MIPS_R_T7 /* No need to preserve this */
#define r_zero		MIPS_R_ZERO
#define r_sp		MIPS_R_SP
#define r_ra		MIPS_R_RA

#ifndef __ASSEMBLY__

/* Declare ASM helpers */

#define DECLARE_LOAD_FUNC(func) \
	extern u8 func(unsigned long *skb, int offset); \
	extern u8 func##_negative(unsigned long *skb, int offset); \
	extern u8 func##_positive(unsigned long *skb, int offset)

DECLARE_LOAD_FUNC(sk_load_word);
DECLARE_LOAD_FUNC(sk_load_half);
DECLARE_LOAD_FUNC(sk_load_byte);

#endif

#endif /* BPF_JIT_MIPS_OP_H */
