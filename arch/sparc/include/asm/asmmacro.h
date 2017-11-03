/* SPDX-License-Identifier: GPL-2.0 */
/* asmmacro.h: Assembler macros.
 *
 * Copyright (C) 1996 David S. Miller (davem@caipfs.rutgers.edu)
 */

#ifndef _SPARC_ASMMACRO_H
#define _SPARC_ASMMACRO_H

/* All trap entry points _must_ begin with this macro or else you
 * lose.  It makes sure the kernel has a proper window so that
 * c-code can be called.
 */
#define SAVE_ALL_HEAD \
	sethi	%hi(trap_setup), %l4; \
	jmpl	%l4 + %lo(trap_setup), %l6;
#define SAVE_ALL \
	SAVE_ALL_HEAD \
	 nop;

/* All traps low-level code here must end with this macro. */
#define RESTORE_ALL b ret_trap_entry; clr %l6;

/* Support for run-time patching of single instructions.
 * This is used to handle the differences in the ASI for
 * MMUREGS for LEON and SUN.
 *
 * Sample:
 * LEON_PI(lda [%g0] ASI_LEON_MMUREGS, %o0
 * SUN_PI_(lda [%g0] ASI_M_MMUREGS, %o0
 * PI == Patch Instruction
 *
 * For LEON we will use the first variant,
 * and for all other we will use the SUN variant.
 * The order is important.
 */
#define LEON_PI(...)				\
662:	__VA_ARGS__

#define SUN_PI_(...)				\
	.section .leon_1insn_patch, "ax";	\
	.word 662b;				\
	__VA_ARGS__;				\
	.previous

#endif /* !(_SPARC_ASMMACRO_H) */
