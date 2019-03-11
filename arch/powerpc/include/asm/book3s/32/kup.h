/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_32_KUP_H
#define _ASM_POWERPC_BOOK3S_32_KUP_H

#include <asm/book3s/32/mmu-hash.h>

#ifdef __ASSEMBLY__

.macro kuep_update_sr	gpr1, gpr2		/* NEVER use r0 as gpr2 due to addis */
101:	mtsrin	\gpr1, \gpr2
	addi	\gpr1, \gpr1, 0x111		/* next VSID */
	rlwinm	\gpr1, \gpr1, 0, 0xf0ffffff	/* clear VSID overflow */
	addis	\gpr2, \gpr2, 0x1000		/* address of next segment */
	bdnz	101b
	isync
.endm

.macro kuep_lock	gpr1, gpr2
#ifdef CONFIG_PPC_KUEP
	li	\gpr1, NUM_USER_SEGMENTS
	li	\gpr2, 0
	mtctr	\gpr1
	mfsrin	\gpr1, \gpr2
	oris	\gpr1, \gpr1, SR_NX@h		/* set Nx */
	kuep_update_sr \gpr1, \gpr2
#endif
.endm

.macro kuep_unlock	gpr1, gpr2
#ifdef CONFIG_PPC_KUEP
	li	\gpr1, NUM_USER_SEGMENTS
	li	\gpr2, 0
	mtctr	\gpr1
	mfsrin	\gpr1, \gpr2
	rlwinm	\gpr1, \gpr1, 0, ~SR_NX		/* Clear Nx */
	kuep_update_sr \gpr1, \gpr2
#endif
.endm

#endif /* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_BOOK3S_32_KUP_H */
