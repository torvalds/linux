/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright IBM Corp. 2007
 * Copyright 2011 Freescale Semiconductor, Inc.
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#include <asm/ppc_asm.h>
#include <asm/kvm_asm.h>
#include <asm/reg.h>
#include <asm/mmu-44x.h>
#include <asm/page.h>
#include <asm/asm-offsets.h>

/* The host stack layout: */
#define HOST_R1         0 /* Implied by stwu. */
#define HOST_CALLEE_LR  4
#define HOST_RUN        8
/* r2 is special: it holds 'current', and it made nonvolatile in the
 * kernel with the -ffixed-r2 gcc option. */
#define HOST_R2         12
#define HOST_CR         16
#define HOST_NV_GPRS    20
#define __HOST_NV_GPR(n)  (HOST_NV_GPRS + ((n - 14) * 4))
#define HOST_NV_GPR(n)  __HOST_NV_GPR(__REG_##n)
#define HOST_MIN_STACK_SIZE (HOST_NV_GPR(R31) + 4)
#define HOST_STACK_SIZE (((HOST_MIN_STACK_SIZE + 15) / 16) * 16) /* Align. */
#define HOST_STACK_LR   (HOST_STACK_SIZE + 4) /* In caller stack frame. */

#define NEED_INST_MASK ((1<<BOOKE_INTERRUPT_PROGRAM) | \
                        (1<<BOOKE_INTERRUPT_DTLB_MISS) | \
                        (1<<BOOKE_INTERRUPT_DEBUG))

#define NEED_DEAR_MASK ((1<<BOOKE_INTERRUPT_DATA_STORAGE) | \
                        (1<<BOOKE_INTERRUPT_DTLB_MISS))

#define NEED_ESR_MASK ((1<<BOOKE_INTERRUPT_DATA_STORAGE) | \
                       (1<<BOOKE_INTERRUPT_INST_STORAGE) | \
                       (1<<BOOKE_INTERRUPT_PROGRAM) | \
                       (1<<BOOKE_INTERRUPT_DTLB_MISS))

.macro KVM_HANDLER ivor_nr scratch srr0
_GLOBAL(kvmppc_handler_\ivor_nr)
	/* Get pointer to vcpu and record exit number. */
	mtspr	\scratch , r4
	mfspr   r4, SPRN_SPRG_THREAD
	lwz     r4, THREAD_KVM_VCPU(r4)
	stw	r3, VCPU_GPR(R3)(r4)
	stw	r5, VCPU_GPR(R5)(r4)
	stw	r6, VCPU_GPR(R6)(r4)
	mfspr	r3, \scratch
	mfctr	r5
	stw	r3, VCPU_GPR(R4)(r4)
	stw	r5, VCPU_CTR(r4)
	mfspr	r3, \srr0
	lis	r6, kvmppc_resume_host@h
	stw	r3, VCPU_PC(r4)
	li	r5, \ivor_nr
	ori	r6, r6, kvmppc_resume_host@l
	mtctr	r6
	bctr
.endm

.macro KVM_HANDLER_ADDR ivor_nr
	.long	kvmppc_handler_\ivor_nr
.endm

.macro KVM_HANDLER_END
	.long	kvmppc_handlers_end
.endm

_GLOBAL(kvmppc_handlers_start)
KVM_HANDLER BOOKE_INTERRUPT_CRITICAL SPRN_SPRG_RSCRATCH_CRIT SPRN_CSRR0
KVM_HANDLER BOOKE_INTERRUPT_MACHINE_CHECK  SPRN_SPRG_RSCRATCH_MC SPRN_MCSRR0
KVM_HANDLER BOOKE_INTERRUPT_DATA_STORAGE SPRN_SPRG_RSCRATCH0 SPRN_SRR0
KVM_HANDLER BOOKE_INTERRUPT_INST_STORAGE SPRN_SPRG_RSCRATCH0 SPRN_SRR0
KVM_HANDLER BOOKE_INTERRUPT_EXTERNAL SPRN_SPRG_RSCRATCH0 SPRN_SRR0
KVM_HANDLER BOOKE_INTERRUPT_ALIGNMENT SPRN_SPRG_RSCRATCH0 SPRN_SRR0
KVM_HANDLER BOOKE_INTERRUPT_PROGRAM SPRN_SPRG_RSCRATCH0 SPRN_SRR0
KVM_HANDLER BOOKE_INTERRUPT_FP_UNAVAIL SPRN_SPRG_RSCRATCH0 SPRN_SRR0
KVM_HANDLER BOOKE_INTERRUPT_SYSCALL SPRN_SPRG_RSCRATCH0 SPRN_SRR0
KVM_HANDLER BOOKE_INTERRUPT_AP_UNAVAIL SPRN_SPRG_RSCRATCH0 SPRN_SRR0
KVM_HANDLER BOOKE_INTERRUPT_DECREMENTER SPRN_SPRG_RSCRATCH0 SPRN_SRR0
KVM_HANDLER BOOKE_INTERRUPT_FIT SPRN_SPRG_RSCRATCH0 SPRN_SRR0
KVM_HANDLER BOOKE_INTERRUPT_WATCHDOG SPRN_SPRG_RSCRATCH_CRIT SPRN_CSRR0
KVM_HANDLER BOOKE_INTERRUPT_DTLB_MISS SPRN_SPRG_RSCRATCH0 SPRN_SRR0
KVM_HANDLER BOOKE_INTERRUPT_ITLB_MISS SPRN_SPRG_RSCRATCH0 SPRN_SRR0
KVM_HANDLER BOOKE_INTERRUPT_DEBUG SPRN_SPRG_RSCRATCH_CRIT SPRN_CSRR0
KVM_HANDLER BOOKE_INTERRUPT_SPE_UNAVAIL SPRN_SPRG_RSCRATCH0 SPRN_SRR0
KVM_HANDLER BOOKE_INTERRUPT_SPE_FP_DATA SPRN_SPRG_RSCRATCH0 SPRN_SRR0
KVM_HANDLER BOOKE_INTERRUPT_SPE_FP_ROUND SPRN_SPRG_RSCRATCH0 SPRN_SRR0
_GLOBAL(kvmppc_handlers_end)

/* Registers:
 *  SPRG_SCRATCH0: guest r4
 *  r4: vcpu pointer
 *  r5: KVM exit number
 */
_GLOBAL(kvmppc_resume_host)
	mfcr	r3
	stw	r3, VCPU_CR(r4)
	stw	r7, VCPU_GPR(R7)(r4)
	stw	r8, VCPU_GPR(R8)(r4)
	stw	r9, VCPU_GPR(R9)(r4)

	li	r6, 1
	slw	r6, r6, r5

#ifdef CONFIG_KVM_EXIT_TIMING
	/* save exit time */
1:
	mfspr	r7, SPRN_TBRU
	mfspr	r8, SPRN_TBRL
	mfspr	r9, SPRN_TBRU
	cmpw	r9, r7
	bne	1b
	stw	r8, VCPU_TIMING_EXIT_TBL(r4)
	stw	r9, VCPU_TIMING_EXIT_TBU(r4)
#endif

	/* Save the faulting instruction and all GPRs for emulation. */
	andi.	r7, r6, NEED_INST_MASK
	beq	..skip_inst_copy
	mfspr	r9, SPRN_SRR0
	mfmsr	r8
	ori	r7, r8, MSR_DS
	mtmsr	r7
	isync
	lwz	r9, 0(r9)
	mtmsr	r8
	isync
	stw	r9, VCPU_LAST_INST(r4)

	stw	r15, VCPU_GPR(R15)(r4)
	stw	r16, VCPU_GPR(R16)(r4)
	stw	r17, VCPU_GPR(R17)(r4)
	stw	r18, VCPU_GPR(R18)(r4)
	stw	r19, VCPU_GPR(R19)(r4)
	stw	r20, VCPU_GPR(R20)(r4)
	stw	r21, VCPU_GPR(R21)(r4)
	stw	r22, VCPU_GPR(R22)(r4)
	stw	r23, VCPU_GPR(R23)(r4)
	stw	r24, VCPU_GPR(R24)(r4)
	stw	r25, VCPU_GPR(R25)(r4)
	stw	r26, VCPU_GPR(R26)(r4)
	stw	r27, VCPU_GPR(R27)(r4)
	stw	r28, VCPU_GPR(R28)(r4)
	stw	r29, VCPU_GPR(R29)(r4)
	stw	r30, VCPU_GPR(R30)(r4)
	stw	r31, VCPU_GPR(R31)(r4)
..skip_inst_copy:

	/* Also grab DEAR and ESR before the host can clobber them. */

	andi.	r7, r6, NEED_DEAR_MASK
	beq	..skip_dear
	mfspr	r9, SPRN_DEAR
	stw	r9, VCPU_FAULT_DEAR(r4)
..skip_dear:

	andi.	r7, r6, NEED_ESR_MASK
	beq	..skip_esr
	mfspr	r9, SPRN_ESR
	stw	r9, VCPU_FAULT_ESR(r4)
..skip_esr:

	/* Save remaining volatile guest register state to vcpu. */
	stw	r0, VCPU_GPR(R0)(r4)
	stw	r1, VCPU_GPR(R1)(r4)
	stw	r2, VCPU_GPR(R2)(r4)
	stw	r10, VCPU_GPR(R10)(r4)
	stw	r11, VCPU_GPR(R11)(r4)
	stw	r12, VCPU_GPR(R12)(r4)
	stw	r13, VCPU_GPR(R13)(r4)
	stw	r14, VCPU_GPR(R14)(r4) /* We need a NV GPR below. */
	mflr	r3
	stw	r3, VCPU_LR(r4)
	mfxer	r3
	stw	r3, VCPU_XER(r4)

	/* Restore host stack pointer and PID before IVPR, since the host
	 * exception handlers use them. */
	lwz	r1, VCPU_HOST_STACK(r4)
	lwz	r3, VCPU_HOST_PID(r4)
	mtspr	SPRN_PID, r3

#ifdef CONFIG_FSL_BOOKE
	/* we cheat and know that Linux doesn't use PID1 which is always 0 */
	lis	r3, 0
	mtspr	SPRN_PID1, r3
#endif

	/* Restore host IVPR before re-enabling interrupts. We cheat and know
	 * that Linux IVPR is always 0xc0000000. */
	lis	r3, 0xc000
	mtspr	SPRN_IVPR, r3

	/* Switch to kernel stack and jump to handler. */
	LOAD_REG_ADDR(r3, kvmppc_handle_exit)
	mtctr	r3
	lwz	r3, HOST_RUN(r1)
	lwz	r2, HOST_R2(r1)
	mr	r14, r4 /* Save vcpu pointer. */

	bctrl	/* kvmppc_handle_exit() */

	/* Restore vcpu pointer and the nonvolatiles we used. */
	mr	r4, r14
	lwz	r14, VCPU_GPR(R14)(r4)

	/* Sometimes instruction emulation must restore complete GPR state. */
	andi.	r5, r3, RESUME_FLAG_NV
	beq	..skip_nv_load
	lwz	r15, VCPU_GPR(R15)(r4)
	lwz	r16, VCPU_GPR(R16)(r4)
	lwz	r17, VCPU_GPR(R17)(r4)
	lwz	r18, VCPU_GPR(R18)(r4)
	lwz	r19, VCPU_GPR(R19)(r4)
	lwz	r20, VCPU_GPR(R20)(r4)
	lwz	r21, VCPU_GPR(R21)(r4)
	lwz	r22, VCPU_GPR(R22)(r4)
	lwz	r23, VCPU_GPR(R23)(r4)
	lwz	r24, VCPU_GPR(R24)(r4)
	lwz	r25, VCPU_GPR(R25)(r4)
	lwz	r26, VCPU_GPR(R26)(r4)
	lwz	r27, VCPU_GPR(R27)(r4)
	lwz	r28, VCPU_GPR(R28)(r4)
	lwz	r29, VCPU_GPR(R29)(r4)
	lwz	r30, VCPU_GPR(R30)(r4)
	lwz	r31, VCPU_GPR(R31)(r4)
..skip_nv_load:

	/* Should we return to the guest? */
	andi.	r5, r3, RESUME_FLAG_HOST
	beq	lightweight_exit

	srawi	r3, r3, 2 /* Shift -ERR back down. */

heavyweight_exit:
	/* Not returning to guest. */

#ifdef CONFIG_SPE
	/* save guest SPEFSCR and load host SPEFSCR */
	mfspr	r9, SPRN_SPEFSCR
	stw	r9, VCPU_SPEFSCR(r4)
	lwz	r9, VCPU_HOST_SPEFSCR(r4)
	mtspr	SPRN_SPEFSCR, r9
#endif

	/* We already saved guest volatile register state; now save the
	 * non-volatiles. */
	stw	r15, VCPU_GPR(R15)(r4)
	stw	r16, VCPU_GPR(R16)(r4)
	stw	r17, VCPU_GPR(R17)(r4)
	stw	r18, VCPU_GPR(R18)(r4)
	stw	r19, VCPU_GPR(R19)(r4)
	stw	r20, VCPU_GPR(R20)(r4)
	stw	r21, VCPU_GPR(R21)(r4)
	stw	r22, VCPU_GPR(R22)(r4)
	stw	r23, VCPU_GPR(R23)(r4)
	stw	r24, VCPU_GPR(R24)(r4)
	stw	r25, VCPU_GPR(R25)(r4)
	stw	r26, VCPU_GPR(R26)(r4)
	stw	r27, VCPU_GPR(R27)(r4)
	stw	r28, VCPU_GPR(R28)(r4)
	stw	r29, VCPU_GPR(R29)(r4)
	stw	r30, VCPU_GPR(R30)(r4)
	stw	r31, VCPU_GPR(R31)(r4)

	/* Load host non-volatile register state from host stack. */
	lwz	r14, HOST_NV_GPR(R14)(r1)
	lwz	r15, HOST_NV_GPR(R15)(r1)
	lwz	r16, HOST_NV_GPR(R16)(r1)
	lwz	r17, HOST_NV_GPR(R17)(r1)
	lwz	r18, HOST_NV_GPR(R18)(r1)
	lwz	r19, HOST_NV_GPR(R19)(r1)
	lwz	r20, HOST_NV_GPR(R20)(r1)
	lwz	r21, HOST_NV_GPR(R21)(r1)
	lwz	r22, HOST_NV_GPR(R22)(r1)
	lwz	r23, HOST_NV_GPR(R23)(r1)
	lwz	r24, HOST_NV_GPR(R24)(r1)
	lwz	r25, HOST_NV_GPR(R25)(r1)
	lwz	r26, HOST_NV_GPR(R26)(r1)
	lwz	r27, HOST_NV_GPR(R27)(r1)
	lwz	r28, HOST_NV_GPR(R28)(r1)
	lwz	r29, HOST_NV_GPR(R29)(r1)
	lwz	r30, HOST_NV_GPR(R30)(r1)
	lwz	r31, HOST_NV_GPR(R31)(r1)

	/* Return to kvm_vcpu_run(). */
	lwz	r4, HOST_STACK_LR(r1)
	lwz	r5, HOST_CR(r1)
	addi	r1, r1, HOST_STACK_SIZE
	mtlr	r4
	mtcr	r5
	/* r3 still contains the return code from kvmppc_handle_exit(). */
	blr


/* Registers:
 *  r3: kvm_run pointer
 *  r4: vcpu pointer
 */
_GLOBAL(__kvmppc_vcpu_run)
	stwu	r1, -HOST_STACK_SIZE(r1)
	stw	r1, VCPU_HOST_STACK(r4)	/* Save stack pointer to vcpu. */

	/* Save host state to stack. */
	stw	r3, HOST_RUN(r1)
	mflr	r3
	stw	r3, HOST_STACK_LR(r1)
	mfcr	r5
	stw	r5, HOST_CR(r1)

	/* Save host non-volatile register state to stack. */
	stw	r14, HOST_NV_GPR(R14)(r1)
	stw	r15, HOST_NV_GPR(R15)(r1)
	stw	r16, HOST_NV_GPR(R16)(r1)
	stw	r17, HOST_NV_GPR(R17)(r1)
	stw	r18, HOST_NV_GPR(R18)(r1)
	stw	r19, HOST_NV_GPR(R19)(r1)
	stw	r20, HOST_NV_GPR(R20)(r1)
	stw	r21, HOST_NV_GPR(R21)(r1)
	stw	r22, HOST_NV_GPR(R22)(r1)
	stw	r23, HOST_NV_GPR(R23)(r1)
	stw	r24, HOST_NV_GPR(R24)(r1)
	stw	r25, HOST_NV_GPR(R25)(r1)
	stw	r26, HOST_NV_GPR(R26)(r1)
	stw	r27, HOST_NV_GPR(R27)(r1)
	stw	r28, HOST_NV_GPR(R28)(r1)
	stw	r29, HOST_NV_GPR(R29)(r1)
	stw	r30, HOST_NV_GPR(R30)(r1)
	stw	r31, HOST_NV_GPR(R31)(r1)

	/* Load guest non-volatiles. */
	lwz	r14, VCPU_GPR(R14)(r4)
	lwz	r15, VCPU_GPR(R15)(r4)
	lwz	r16, VCPU_GPR(R16)(r4)
	lwz	r17, VCPU_GPR(R17)(r4)
	lwz	r18, VCPU_GPR(R18)(r4)
	lwz	r19, VCPU_GPR(R19)(r4)
	lwz	r20, VCPU_GPR(R20)(r4)
	lwz	r21, VCPU_GPR(R21)(r4)
	lwz	r22, VCPU_GPR(R22)(r4)
	lwz	r23, VCPU_GPR(R23)(r4)
	lwz	r24, VCPU_GPR(R24)(r4)
	lwz	r25, VCPU_GPR(R25)(r4)
	lwz	r26, VCPU_GPR(R26)(r4)
	lwz	r27, VCPU_GPR(R27)(r4)
	lwz	r28, VCPU_GPR(R28)(r4)
	lwz	r29, VCPU_GPR(R29)(r4)
	lwz	r30, VCPU_GPR(R30)(r4)
	lwz	r31, VCPU_GPR(R31)(r4)

#ifdef CONFIG_SPE
	/* save host SPEFSCR and load guest SPEFSCR */
	mfspr	r3, SPRN_SPEFSCR
	stw	r3, VCPU_HOST_SPEFSCR(r4)
	lwz	r3, VCPU_SPEFSCR(r4)
	mtspr	SPRN_SPEFSCR, r3
#endif

lightweight_exit:
	stw	r2, HOST_R2(r1)

	mfspr	r3, SPRN_PID
	stw	r3, VCPU_HOST_PID(r4)
	lwz	r3, VCPU_SHADOW_PID(r4)
	mtspr	SPRN_PID, r3

#ifdef CONFIG_FSL_BOOKE
	lwz	r3, VCPU_SHADOW_PID1(r4)
	mtspr	SPRN_PID1, r3
#endif

#ifdef CONFIG_44x
	iccci	0, 0 /* XXX hack */
#endif

	/* Load some guest volatiles. */
	lwz	r0, VCPU_GPR(R0)(r4)
	lwz	r2, VCPU_GPR(R2)(r4)
	lwz	r9, VCPU_GPR(R9)(r4)
	lwz	r10, VCPU_GPR(R10)(r4)
	lwz	r11, VCPU_GPR(R11)(r4)
	lwz	r12, VCPU_GPR(R12)(r4)
	lwz	r13, VCPU_GPR(R13)(r4)
	lwz	r3, VCPU_LR(r4)
	mtlr	r3
	lwz	r3, VCPU_XER(r4)
	mtxer	r3

	/* Switch the IVPR. XXX If we take a TLB miss after this we're screwed,
	 * so how do we make sure vcpu won't fault? */
	lis	r8, kvmppc_booke_handlers@ha
	lwz	r8, kvmppc_booke_handlers@l(r8)
	mtspr	SPRN_IVPR, r8

	lwz	r5, VCPU_SHARED(r4)

	/* Can't switch the stack pointer until after IVPR is switched,
	 * because host interrupt handlers would get confused. */
	lwz	r1, VCPU_GPR(R1)(r4)

	/*
	 * Host interrupt handlers may have clobbered these
	 * guest-readable SPRGs, or the guest kernel may have
	 * written directly to the shared area, so we
	 * need to reload them here with the guest's values.
	 */
	PPC_LD(r3, VCPU_SHARED_SPRG4, r5)
	mtspr	SPRN_SPRG4W, r3
	PPC_LD(r3, VCPU_SHARED_SPRG5, r5)
	mtspr	SPRN_SPRG5W, r3
	PPC_LD(r3, VCPU_SHARED_SPRG6, r5)
	mtspr	SPRN_SPRG6W, r3
	PPC_LD(r3, VCPU_SHARED_SPRG7, r5)
	mtspr	SPRN_SPRG7W, r3

#ifdef CONFIG_KVM_EXIT_TIMING
	/* save enter time */
1:
	mfspr	r6, SPRN_TBRU
	mfspr	r7, SPRN_TBRL
	mfspr	r8, SPRN_TBRU
	cmpw	r8, r6
	bne	1b
	stw	r7, VCPU_TIMING_LAST_ENTER_TBL(r4)
	stw	r8, VCPU_TIMING_LAST_ENTER_TBU(r4)
#endif

	/* Finish loading guest volatiles and jump to guest. */
	lwz	r3, VCPU_CTR(r4)
	lwz	r5, VCPU_CR(r4)
	lwz	r6, VCPU_PC(r4)
	lwz	r7, VCPU_SHADOW_MSR(r4)
	mtctr	r3
	mtcr	r5
	mtsrr0	r6
	mtsrr1	r7
	lwz	r5, VCPU_GPR(R5)(r4)
	lwz	r6, VCPU_GPR(R6)(r4)
	lwz	r7, VCPU_GPR(R7)(r4)
	lwz	r8, VCPU_GPR(R8)(r4)

	/* Clear any debug events which occurred since we disabled MSR[DE].
	 * XXX This gives us a 3-instruction window in which a breakpoint
	 * intended for guest context could fire in the host instead. */
	lis	r3, 0xffff
	ori	r3, r3, 0xffff
	mtspr	SPRN_DBSR, r3

	lwz	r3, VCPU_GPR(R3)(r4)
	lwz	r4, VCPU_GPR(R4)(r4)
	rfi

	.data
	.align	4
	.globl	kvmppc_booke_handler_addr
kvmppc_booke_handler_addr:
KVM_HANDLER_ADDR BOOKE_INTERRUPT_CRITICAL
KVM_HANDLER_ADDR BOOKE_INTERRUPT_MACHINE_CHECK
KVM_HANDLER_ADDR BOOKE_INTERRUPT_DATA_STORAGE
KVM_HANDLER_ADDR BOOKE_INTERRUPT_INST_STORAGE
KVM_HANDLER_ADDR BOOKE_INTERRUPT_EXTERNAL
KVM_HANDLER_ADDR BOOKE_INTERRUPT_ALIGNMENT
KVM_HANDLER_ADDR BOOKE_INTERRUPT_PROGRAM
KVM_HANDLER_ADDR BOOKE_INTERRUPT_FP_UNAVAIL
KVM_HANDLER_ADDR BOOKE_INTERRUPT_SYSCALL
KVM_HANDLER_ADDR BOOKE_INTERRUPT_AP_UNAVAIL
KVM_HANDLER_ADDR BOOKE_INTERRUPT_DECREMENTER
KVM_HANDLER_ADDR BOOKE_INTERRUPT_FIT
KVM_HANDLER_ADDR BOOKE_INTERRUPT_WATCHDOG
KVM_HANDLER_ADDR BOOKE_INTERRUPT_DTLB_MISS
KVM_HANDLER_ADDR BOOKE_INTERRUPT_ITLB_MISS
KVM_HANDLER_ADDR BOOKE_INTERRUPT_DEBUG
KVM_HANDLER_ADDR BOOKE_INTERRUPT_SPE_UNAVAIL
KVM_HANDLER_ADDR BOOKE_INTERRUPT_SPE_FP_DATA
KVM_HANDLER_ADDR BOOKE_INTERRUPT_SPE_FP_ROUND
KVM_HANDLER_END /*Always keep this in end*/

#ifdef CONFIG_SPE
_GLOBAL(kvmppc_save_guest_spe)
	cmpi	0,r3,0
	beqlr-
	SAVE_32EVRS(0, r4, r3, VCPU_EVR)
	evxor   evr6, evr6, evr6
	evmwumiaa evr6, evr6, evr6
	li	r4,VCPU_ACC
	evstddx evr6, r4, r3		/* save acc */
	blr

_GLOBAL(kvmppc_load_guest_spe)
	cmpi	0,r3,0
	beqlr-
	li      r4,VCPU_ACC
	evlddx  evr6,r4,r3
	evmra   evr6,evr6		/* load acc */
	REST_32EVRS(0, r4, r3, VCPU_EVR)
	blr
#endif
