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

#endif /* _ASM_REGDEF_H */
