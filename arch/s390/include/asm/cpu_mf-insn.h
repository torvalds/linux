/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for CPU-MF instructions
 *
 * Copyright IBM Corp. 2019
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */
#ifndef _ASM_S390_CPU_MF_INSN_H
#define _ASM_S390_CPU_MF_INSN_H

#ifdef __ASSEMBLY__

/* Macro to generate the STCCTM instruction with a customized
 * M3 field designating the counter set.
 */
.macro	STCCTM	r1 m3 db2
	.insn	rsy,0xeb0000000017,\r1,\m3 & 0xf,\db2
.endm

#endif /* __ASSEMBLY__ */

#endif
