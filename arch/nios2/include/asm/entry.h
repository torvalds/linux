/*
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *
 * Based on m68knommu asm/entry.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_ENTRY_H
#define _ASM_NIOS2_ENTRY_H

#ifdef __ASSEMBLY__

#include <asm/processor.h>
#include <asm/registers.h>
#include <asm/asm-offsets.h>

/*
 * Stack layout in 'ret_from_exception':
 *
 * This allows access to the syscall arguments in registers r4-r8
 *
 *	 0(sp) - r8
 *	 4(sp) - r9
 *	 8(sp) - r10
 *	 C(sp) - r11
 *	10(sp) - r12
 *	14(sp) - r13
 *	18(sp) - r14
 *	1C(sp) - r15
 *	20(sp) - r1
 *	24(sp) - r2
 *	28(sp) - r3
 *	2C(sp) - r4
 *	30(sp) - r5
 *	34(sp) - r6
 *	38(sp) - r7
 *	3C(sp) - orig_r2
 *	40(sp) - ra
 *	44(sp) - fp
 *	48(sp) - sp
 *	4C(sp) - gp
 *	50(sp) - estatus
 *	54(sp) - status_extension (NOMMU only)
 *	58(sp) - ea
 */

/*
 * Standard Nios2 interrupt entry and exit macros.
 * Must be called with interrupts disabled.
 */
.macro SAVE_ALL
#ifdef CONFIG_MMU
	rdctl	r24, estatus
	andi	r24, r24, ESTATUS_EU
	beq	r24, r0, 1f /* In supervisor mode, already on kernel stack */
#else
	movia	r24, status_extension	/* Read status extension */
	ldw	r24, 0(r24)
	andi	r24, r24, PS_S
	bne	r24, r0, 1f /* In supervisor mode, already on kernel stack */
#endif /* CONFIG_MMU */

	movia	r24, _current_thread	/* Switch to current kernel stack */
	ldw	r24, 0(r24)		/* using the thread_info */
	addi	r24, r24, THREAD_SIZE-PT_REGS_SIZE
	stw	sp, PT_SP(r24)		/* Save user stack before changing */
	mov	sp, r24
	br	2f

1 :	mov	r24, sp
	addi	sp, sp, -PT_REGS_SIZE	/* Backup the kernel stack pointer */
	stw	r24, PT_SP(sp)
2 :	stw	r1, PT_R1(sp)
	stw	r2, PT_R2(sp)
	stw	r3, PT_R3(sp)
	stw	r4, PT_R4(sp)
	stw	r5, PT_R5(sp)
	stw	r6, PT_R6(sp)
	stw	r7, PT_R7(sp)
	stw	r8, PT_R8(sp)
	stw	r9, PT_R9(sp)
	stw	r10, PT_R10(sp)
	stw	r11, PT_R11(sp)
	stw	r12, PT_R12(sp)
	stw	r13, PT_R13(sp)
	stw	r14, PT_R14(sp)
	stw	r15, PT_R15(sp)
	stw	r2, PT_ORIG_R2(sp)
#ifdef CONFIG_MMU
	stw	r7, PT_ORIG_R7(sp)
#endif
	stw	ra, PT_RA(sp)
	stw	fp, PT_FP(sp)
	stw	gp, PT_GP(sp)
	rdctl	r24, estatus
	stw	r24, PT_ESTATUS(sp)
#ifndef CONFIG_MMU
	movia	r24, status_extension		/* Read status extension */
	ldw	r1, 0(r24)
	stw	r1, PT_STATUS_EXTENSION(sp) /* Store user/supervisor status */
	ORI32	r1, r1, PS_S			/* Set supervisor mode */
	stw	r1, 0(r24)
#endif /* CONFIG_MMU */
	stw	ea, PT_EA(sp)
.endm

.macro RESTORE_ALL
#ifdef CONFIG_MMU
	ldw	r1, PT_R1(sp)		/* Restore registers */
#else
	ldw	r1, PT_STATUS_EXTENSION(sp) /* Restore user/supervisor status */
	movia	r24, status_extension
	stw	r1, 0(r24)
	ldw	r1, PT_R1(sp)		/* Restore registers */
#endif /* CONFIG_MMU */
	ldw	r2, PT_R2(sp)
	ldw	r3, PT_R3(sp)
	ldw	r4, PT_R4(sp)
	ldw	r5, PT_R5(sp)
	ldw	r6, PT_R6(sp)
	ldw	r7, PT_R7(sp)
	ldw	r8, PT_R8(sp)
	ldw	r9, PT_R9(sp)
	ldw	r10, PT_R10(sp)
	ldw	r11, PT_R11(sp)
	ldw	r12, PT_R12(sp)
	ldw	r13, PT_R13(sp)
	ldw	r14, PT_R14(sp)
	ldw	r15, PT_R15(sp)
	ldw	ra, PT_RA(sp)
	ldw	fp, PT_FP(sp)
	ldw	gp, PT_GP(sp)
	ldw	r24, PT_ESTATUS(sp)
	wrctl	estatus, r24
	ldw	ea, PT_EA(sp)
	ldw	sp, PT_SP(sp)		/* Restore sp last */
.endm

.macro	SAVE_SWITCH_STACK
	addi	sp, sp, -SWITCH_STACK_SIZE
	stw	r16, SW_R16(sp)
	stw	r17, SW_R17(sp)
	stw	r18, SW_R18(sp)
	stw	r19, SW_R19(sp)
	stw	r20, SW_R20(sp)
	stw	r21, SW_R21(sp)
	stw	r22, SW_R22(sp)
	stw	r23, SW_R23(sp)
	stw	fp, SW_FP(sp)
	stw	gp, SW_GP(sp)
	stw	ra, SW_RA(sp)
.endm

.macro	RESTORE_SWITCH_STACK
	ldw	r16, SW_R16(sp)
	ldw	r17, SW_R17(sp)
	ldw	r18, SW_R18(sp)
	ldw	r19, SW_R19(sp)
	ldw	r20, SW_R20(sp)
	ldw	r21, SW_R21(sp)
	ldw	r22, SW_R22(sp)
	ldw	r23, SW_R23(sp)
	ldw	fp, SW_FP(sp)
	ldw	gp, SW_GP(sp)
	ldw	ra, SW_RA(sp)
	addi	sp, sp, SWITCH_STACK_SIZE
.endm

#endif /* __ASSEMBLY__ */
#endif /* _ASM_NIOS2_ENTRY_H */
