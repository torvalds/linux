/*
 * Just-In-Time compiler for BPF filters on MIPS
 *
 * Copyright (c) 2014 Imagination Technologies Ltd.
 * Author: Markos Chandras <markos.chandras@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 */

#ifndef BPF_JIT_MIPS_OP_H
#define BPF_JIT_MIPS_OP_H

/* Registers used by JIT */
#define MIPS_R_ZERO	0
#define MIPS_R_V0	2
#define MIPS_R_V1	3
#define MIPS_R_A0	4
#define MIPS_R_A1	5
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

#endif /* BPF_JIT_MIPS_OP_H */
