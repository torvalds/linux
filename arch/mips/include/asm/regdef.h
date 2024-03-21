/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1985 MIPS Computer Systems, Inc.
 * Copyright (C) 1994, 95, 99, 2003 by Ralf Baechle
 * Copyright (C) 1990 - 1992, 1999 Silicon Graphics, Inc.
 * Copyright (C) 2011 Wind River Systems,
 *   written by Ralf Baechle <ralf@linux-mips.org>
 */
#ifndef _ASM_REGDEF_H
#define _ASM_REGDEF_H

#include <asm/sgidefs.h>

#if _MIPS_SIM == _MIPS_SIM_ABI32

/*
 * General purpose register numbers for 32 bit ABI
 */
#define GPR_ZERO	0	/* wired zero */
#define GPR_AT	1	/* assembler temp */
#define GPR_V0	2	/* return value */
#define GPR_V1	3
#define GPR_A0	4	/* argument registers */
#define GPR_A1	5
#define GPR_A2	6
#define GPR_A3	7
#define GPR_T0	8	/* caller saved */
#define GPR_T1	9
#define GPR_T2	10
#define GPR_T3	11
#define GPR_T4	12
#define GPR_TA0	12
#define GPR_T5	13
#define GPR_TA1	13
#define GPR_T6	14
#define GPR_TA2	14
#define GPR_T7	15
#define GPR_TA3	15
#define GPR_S0	16	/* callee saved */
#define GPR_S1	17
#define GPR_S2	18
#define GPR_S3	19
#define GPR_S4	20
#define GPR_S5	21
#define GPR_S6	22
#define GPR_S7	23
#define GPR_T8	24	/* caller saved */
#define GPR_T9	25
#define GPR_JP	25	/* PIC jump register */
#define GPR_K0	26	/* kernel scratch */
#define GPR_K1	27
#define GPR_GP	28	/* global pointer */
#define GPR_SP	29	/* stack pointer */
#define GPR_FP	30	/* frame pointer */
#define GPR_S8	30	/* same like fp! */
#define GPR_RA	31	/* return address */

#endif /* _MIPS_SIM == _MIPS_SIM_ABI32 */

#if _MIPS_SIM == _MIPS_SIM_ABI64 || _MIPS_SIM == _MIPS_SIM_NABI32

#define GPR_ZERO	0	/* wired zero */
#define GPR_AT	1	/* assembler temp */
#define GPR_V0	2	/* return value - caller saved */
#define GPR_V1	3
#define GPR_A0	4	/* argument registers */
#define GPR_A1	5
#define GPR_A2	6
#define GPR_A3	7
#define GPR_A4	8	/* arg reg 64 bit; caller saved in 32 bit */
#define GPR_TA0	8
#define GPR_A5	9
#define GPR_TA1	9
#define GPR_A6	10
#define GPR_TA2	10
#define GPR_A7	11
#define GPR_TA3	11
#define GPR_T0	12	/* caller saved */
#define GPR_T1	13
#define GPR_T2	14
#define GPR_T3	15
#define GPR_S0	16	/* callee saved */
#define GPR_S1	17
#define GPR_S2	18
#define GPR_S3	19
#define GPR_S4	20
#define GPR_S5	21
#define GPR_S6	22
#define GPR_S7	23
#define GPR_T8	24	/* caller saved */
#define GPR_T9	25	/* callee address for PIC/temp */
#define GPR_JP	25	/* PIC jump register */
#define GPR_K0	26	/* kernel temporary */
#define GPR_K1	27
#define GPR_GP	28	/* global pointer - caller saved for PIC */
#define GPR_SP	29	/* stack pointer */
#define GPR_FP	30	/* frame pointer */
#define GPR_S8	30	/* callee saved */
#define GPR_RA	31	/* return address */

#endif /* _MIPS_SIM == _MIPS_SIM_ABI64 || _MIPS_SIM == _MIPS_SIM_NABI32 */

#ifdef __ASSEMBLY__
#if _MIPS_SIM == _MIPS_SIM_ABI32

/*
 * Symbolic register names for 32 bit ABI
 */
#define zero	$0	/* wired zero */
#define AT	$1	/* assembler temp  - uppercase because of ".set at" */
#define v0	$2	/* return value */
#define v1	$3
#define a0	$4	/* argument registers */
#define a1	$5
#define a2	$6
#define a3	$7
#define t0	$8	/* caller saved */
#define t1	$9
#define t2	$10
#define t3	$11
#define t4	$12
#define ta0	$12
#define t5	$13
#define ta1	$13
#define t6	$14
#define ta2	$14
#define t7	$15
#define ta3	$15
#define s0	$16	/* callee saved */
#define s1	$17
#define s2	$18
#define s3	$19
#define s4	$20
#define s5	$21
#define s6	$22
#define s7	$23
#define t8	$24	/* caller saved */
#define t9	$25
#define jp	$25	/* PIC jump register */
#define k0	$26	/* kernel scratch */
#define k1	$27
#define gp	$28	/* global pointer */
#define sp	$29	/* stack pointer */
#define fp	$30	/* frame pointer */
#define s8	$30	/* same like fp! */
#define ra	$31	/* return address */

#endif /* _MIPS_SIM == _MIPS_SIM_ABI32 */

#if _MIPS_SIM == _MIPS_SIM_ABI64 || _MIPS_SIM == _MIPS_SIM_NABI32

#define zero	$0	/* wired zero */
#define AT	$at	/* assembler temp - uppercase because of ".set at" */
#define v0	$2	/* return value - caller saved */
#define v1	$3
#define a0	$4	/* argument registers */
#define a1	$5
#define a2	$6
#define a3	$7
#define a4	$8	/* arg reg 64 bit; caller saved in 32 bit */
#define ta0	$8
#define a5	$9
#define ta1	$9
#define a6	$10
#define ta2	$10
#define a7	$11
#define ta3	$11
#define t0	$12	/* caller saved */
#define t1	$13
#define t2	$14
#define t3	$15
#define s0	$16	/* callee saved */
#define s1	$17
#define s2	$18
#define s3	$19
#define s4	$20
#define s5	$21
#define s6	$22
#define s7	$23
#define t8	$24	/* caller saved */
#define t9	$25	/* callee address for PIC/temp */
#define jp	$25	/* PIC jump register */
#define k0	$26	/* kernel temporary */
#define k1	$27
#define gp	$28	/* global pointer - caller saved for PIC */
#define sp	$29	/* stack pointer */
#define fp	$30	/* frame pointer */
#define s8	$30	/* callee saved */
#define ra	$31	/* return address */

#endif /* _MIPS_SIM == _MIPS_SIM_ABI64 || _MIPS_SIM == _MIPS_SIM_NABI32 */
#endif /* __ASSEMBLY__ */

#endif /* _ASM_REGDEF_H */
