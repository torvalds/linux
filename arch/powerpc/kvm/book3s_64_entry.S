/* SPDX-License-Identifier: GPL-2.0-only */
#include <asm/asm-offsets.h>
#include <asm/cache.h>
#include <asm/code-patching-asm.h>
#include <asm/exception-64s.h>
#include <asm/export.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_book3s_asm.h>
#include <asm/mmu.h>
#include <asm/ppc_asm.h>
#include <asm/ptrace.h>
#include <asm/reg.h>
#include <asm/ultravisor-api.h>

/*
 * These are branched to from interrupt handlers in exception-64s.S which set
 * IKVM_REAL or IKVM_VIRT, if HSTATE_IN_GUEST was found to be non-zero.
 */

/*
 * This is a hcall, so register convention is as
 * Documentation/powerpc/papr_hcalls.rst.
 *
 * This may also be a syscall from PR-KVM userspace that is to be
 * reflected to the PR guest kernel, so registers may be set up for
 * a system call rather than hcall. We don't currently clobber
 * anything here, but the 0xc00 handler has already clobbered CTR
 * and CR0, so PR-KVM can not support a guest kernel that preserves
 * those registers across its system calls.
 *
 * The state of registers is as kvmppc_interrupt, except CFAR is not
 * saved, R13 is not in SCRATCH0, and R10 does not contain the trap.
 */
.global	kvmppc_hcall
.balign IFETCH_ALIGN_BYTES
kvmppc_hcall:
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	lbz	r10,HSTATE_IN_GUEST(r13)
	cmpwi	r10,KVM_GUEST_MODE_HV_P9
	beq	kvmppc_p9_exit_hcall
#endif
	ld	r10,PACA_EXGEN+EX_R13(r13)
	SET_SCRATCH0(r10)
	li	r10,0xc00
	/* Now we look like kvmppc_interrupt */
	li	r11,PACA_EXGEN
	b	.Lgot_save_area

/*
 * KVM interrupt entry occurs after GEN_INT_ENTRY runs, and follows that
 * call convention:
 *
 * guest R9-R13, CTR, CFAR, PPR saved in PACA EX_xxx save area
 * guest (H)DAR, (H)DSISR are also in the save area for relevant interrupts
 * guest R13 also saved in SCRATCH0
 * R13		= PACA
 * R11		= (H)SRR0
 * R12		= (H)SRR1
 * R9		= guest CR
 * PPR is set to medium
 *
 * With the addition for KVM:
 * R10		= trap vector
 */
.global	kvmppc_interrupt
.balign IFETCH_ALIGN_BYTES
kvmppc_interrupt:
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	std	r10,HSTATE_SCRATCH0(r13)
	lbz	r10,HSTATE_IN_GUEST(r13)
	cmpwi	r10,KVM_GUEST_MODE_HV_P9
	beq	kvmppc_p9_exit_interrupt
	ld	r10,HSTATE_SCRATCH0(r13)
#endif
	li	r11,PACA_EXGEN
	cmpdi	r10,0x200
	bgt+	.Lgot_save_area
	li	r11,PACA_EXMC
	beq	.Lgot_save_area
	li	r11,PACA_EXNMI
.Lgot_save_area:
	add	r11,r11,r13
BEGIN_FTR_SECTION
	ld	r12,EX_CFAR(r11)
	std	r12,HSTATE_CFAR(r13)
END_FTR_SECTION_IFSET(CPU_FTR_CFAR)
	ld	r12,EX_CTR(r11)
	mtctr	r12
BEGIN_FTR_SECTION
	ld	r12,EX_PPR(r11)
	std	r12,HSTATE_PPR(r13)
END_FTR_SECTION_IFSET(CPU_FTR_HAS_PPR)
	ld	r12,EX_R12(r11)
	std	r12,HSTATE_SCRATCH0(r13)
	sldi	r12,r9,32
	or	r12,r12,r10
	ld	r9,EX_R9(r11)
	ld	r10,EX_R10(r11)
	ld	r11,EX_R11(r11)

	/*
	 * Hcalls and other interrupts come here after normalising register
	 * contents and save locations:
	 *
	 * R12		= (guest CR << 32) | interrupt vector
	 * R13		= PACA
	 * guest R12 saved in shadow HSTATE_SCRATCH0
	 * guest R13 saved in SPRN_SCRATCH0
	 */
	std	r9,HSTATE_SCRATCH2(r13)
	lbz	r9,HSTATE_IN_GUEST(r13)
	cmpwi	r9,KVM_GUEST_MODE_SKIP
	beq-	.Lmaybe_skip
.Lno_skip:
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	cmpwi	r9,KVM_GUEST_MODE_GUEST
	beq	kvmppc_interrupt_pr
#endif
	b	kvmppc_interrupt_hv
#else
	b	kvmppc_interrupt_pr
#endif

/*
 * "Skip" interrupts are part of a trick KVM uses a with hash guests to load
 * the faulting instruction in guest memory from the the hypervisor without
 * walking page tables.
 *
 * When the guest takes a fault that requires the hypervisor to load the
 * instruction (e.g., MMIO emulation), KVM is running in real-mode with HV=1
 * and the guest MMU context loaded. It sets KVM_GUEST_MODE_SKIP, and sets
 * MSR[DR]=1 while leaving MSR[IR]=0, so it continues to fetch HV instructions
 * but loads and stores will access the guest context. This is used to load
 * the faulting instruction using the faulting guest effective address.
 *
 * However the guest context may not be able to translate, or it may cause a
 * machine check or other issue, which results in a fault in the host
 * (even with KVM-HV).
 *
 * These faults come here because KVM_GUEST_MODE_SKIP was set, so if they
 * are (or are likely) caused by that load, the instruction is skipped by
 * just returning with the PC advanced +4, where it is noticed the load did
 * not execute and it goes to the slow path which walks the page tables to
 * read guest memory.
 */
.Lmaybe_skip:
	cmpwi	r12,BOOK3S_INTERRUPT_MACHINE_CHECK
	beq	1f
	cmpwi	r12,BOOK3S_INTERRUPT_DATA_STORAGE
	beq	1f
	cmpwi	r12,BOOK3S_INTERRUPT_DATA_SEGMENT
	beq	1f
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	/* HSRR interrupts get 2 added to interrupt number */
	cmpwi	r12,BOOK3S_INTERRUPT_H_DATA_STORAGE | 0x2
	beq	2f
#endif
	b	.Lno_skip
1:	mfspr	r9,SPRN_SRR0
	addi	r9,r9,4
	mtspr	SPRN_SRR0,r9
	ld	r12,HSTATE_SCRATCH0(r13)
	ld	r9,HSTATE_SCRATCH2(r13)
	GET_SCRATCH0(r13)
	RFI_TO_KERNEL
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
2:	mfspr	r9,SPRN_HSRR0
	addi	r9,r9,4
	mtspr	SPRN_HSRR0,r9
	ld	r12,HSTATE_SCRATCH0(r13)
	ld	r9,HSTATE_SCRATCH2(r13)
	GET_SCRATCH0(r13)
	HRFI_TO_KERNEL
#endif

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE

/* Stack frame offsets for kvmppc_p9_enter_guest */
#define SFS			(144 + STACK_FRAME_MIN_SIZE)
#define STACK_SLOT_NVGPRS	(SFS - 144)	/* 18 gprs */

/*
 * void kvmppc_p9_enter_guest(struct vcpu *vcpu);
 *
 * Enter the guest on a ISAv3.0 or later system.
 */
.balign	IFETCH_ALIGN_BYTES
_GLOBAL(kvmppc_p9_enter_guest)
EXPORT_SYMBOL_GPL(kvmppc_p9_enter_guest)
	mflr	r0
	std	r0,PPC_LR_STKOFF(r1)
	stdu	r1,-SFS(r1)

	std	r1,HSTATE_HOST_R1(r13)

	mfcr	r4
	stw	r4,SFS+8(r1)

	reg = 14
	.rept	18
	std	reg,STACK_SLOT_NVGPRS + ((reg - 14) * 8)(r1)
	reg = reg + 1
	.endr

	ld	r4,VCPU_LR(r3)
	mtlr	r4
	ld	r4,VCPU_CTR(r3)
	mtctr	r4
	ld	r4,VCPU_XER(r3)
	mtspr	SPRN_XER,r4

	ld	r1,VCPU_CR(r3)

BEGIN_FTR_SECTION
	ld	r4,VCPU_CFAR(r3)
	mtspr	SPRN_CFAR,r4
END_FTR_SECTION_IFSET(CPU_FTR_CFAR)
BEGIN_FTR_SECTION
	ld	r4,VCPU_PPR(r3)
	mtspr	SPRN_PPR,r4
END_FTR_SECTION_IFSET(CPU_FTR_HAS_PPR)

	reg = 4
	.rept	28
	ld	reg,__VCPU_GPR(reg)(r3)
	reg = reg + 1
	.endr

	ld	r4,VCPU_KVM(r3)
	lbz	r4,KVM_SECURE_GUEST(r4)
	cmpdi	r4,0
	ld	r4,VCPU_GPR(R4)(r3)
	bne	.Lret_to_ultra

	mtcr	r1

	ld	r0,VCPU_GPR(R0)(r3)
	ld	r1,VCPU_GPR(R1)(r3)
	ld	r2,VCPU_GPR(R2)(r3)
	ld	r3,VCPU_GPR(R3)(r3)

	HRFI_TO_GUEST
	b	.

	/*
	 * Use UV_RETURN ultracall to return control back to the Ultravisor
	 * after processing an hypercall or interrupt that was forwarded
	 * (a.k.a. reflected) to the Hypervisor.
	 *
	 * All registers have already been reloaded except the ucall requires:
	 *   R0 = hcall result
	 *   R2 = SRR1, so UV can detect a synthesized interrupt (if any)
	 *   R3 = UV_RETURN
	 */
.Lret_to_ultra:
	mtcr	r1
	ld	r1,VCPU_GPR(R1)(r3)

	ld	r0,VCPU_GPR(R3)(r3)
	mfspr	r2,SPRN_SRR1
	LOAD_REG_IMMEDIATE(r3, UV_RETURN)
	sc	2

/*
 * kvmppc_p9_exit_hcall and kvmppc_p9_exit_interrupt are branched to from
 * above if the interrupt was taken for a guest that was entered via
 * kvmppc_p9_enter_guest().
 *
 * The exit code recovers the host stack and vcpu pointer, saves all guest GPRs
 * and CR, LR, XER as well as guest MSR and NIA into the VCPU, then re-
 * establishes the host stack and registers to return from the
 * kvmppc_p9_enter_guest() function, which saves CTR and other guest registers
 * (SPRs and FP, VEC, etc).
 */
.balign	IFETCH_ALIGN_BYTES
kvmppc_p9_exit_hcall:
	mfspr	r11,SPRN_SRR0
	mfspr	r12,SPRN_SRR1
	li	r10,0xc00
	std	r10,HSTATE_SCRATCH0(r13)

.balign	IFETCH_ALIGN_BYTES
kvmppc_p9_exit_interrupt:
	/*
	 * If set to KVM_GUEST_MODE_HV_P9 but we're still in the
	 * hypervisor, that means we can't return from the entry stack.
	 */
	rldicl. r10,r12,64-MSR_HV_LG,63
	bne-	kvmppc_p9_bad_interrupt

	std     r1,HSTATE_SCRATCH1(r13)
	std     r3,HSTATE_SCRATCH2(r13)
	ld	r1,HSTATE_HOST_R1(r13)
	ld	r3,HSTATE_KVM_VCPU(r13)

	std	r9,VCPU_CR(r3)

1:
	std	r11,VCPU_PC(r3)
	std	r12,VCPU_MSR(r3)

	reg = 14
	.rept	18
	std	reg,__VCPU_GPR(reg)(r3)
	reg = reg + 1
	.endr

	/* r1, r3, r9-r13 are saved to vcpu by C code */
	std	r0,VCPU_GPR(R0)(r3)
	std	r2,VCPU_GPR(R2)(r3)
	reg = 4
	.rept	5
	std	reg,__VCPU_GPR(reg)(r3)
	reg = reg + 1
	.endr

	ld	r2,PACATOC(r13)

	mflr	r4
	std	r4,VCPU_LR(r3)
	mfspr	r4,SPRN_XER
	std	r4,VCPU_XER(r3)

	reg = 14
	.rept	18
	ld	reg,STACK_SLOT_NVGPRS + ((reg - 14) * 8)(r1)
	reg = reg + 1
	.endr

	lwz	r4,SFS+8(r1)
	mtcr	r4

	/*
	 * Flush the link stack here, before executing the first blr on the
	 * way out of the guest.
	 *
	 * The link stack won't match coming out of the guest anyway so the
	 * only cost is the flush itself. The call clobbers r0.
	 */
1:	nop
	patch_site 1b patch__call_kvm_flush_link_stack_p9

	addi	r1,r1,SFS
	ld	r0,PPC_LR_STKOFF(r1)
	mtlr	r0
	blr

/*
 * Took an interrupt somewhere right before HRFID to guest, so registers are
 * in a bad way. Return things hopefully enough to run host virtual code and
 * run the Linux interrupt handler (SRESET or MCE) to print something useful.
 *
 * We could be really clever and save all host registers in known locations
 * before setting HSTATE_IN_GUEST, then restoring them all here, and setting
 * return address to a fixup that sets them up again. But that's a lot of
 * effort for a small bit of code. Lots of other things to do first.
 */
kvmppc_p9_bad_interrupt:
BEGIN_MMU_FTR_SECTION
	/*
	 * Hash host doesn't try to recover MMU (requires host SLB reload)
	 */
	b	.
END_MMU_FTR_SECTION_IFCLR(MMU_FTR_TYPE_RADIX)
	/*
	 * Clean up guest registers to give host a chance to run.
	 */
	li	r10,0
	mtspr	SPRN_AMR,r10
	mtspr	SPRN_IAMR,r10
	mtspr	SPRN_CIABR,r10
	mtspr	SPRN_DAWRX0,r10
BEGIN_FTR_SECTION
	mtspr	SPRN_DAWRX1,r10
END_FTR_SECTION_IFSET(CPU_FTR_DAWR1)

	/*
	 * Switch to host MMU mode (don't have the real host PID but we aren't
	 * going back to userspace).
	 */
	hwsync
	isync

	mtspr	SPRN_PID,r10

	ld	r10, HSTATE_KVM_VCPU(r13)
	ld	r10, VCPU_KVM(r10)
	lwz	r10, KVM_HOST_LPID(r10)
	mtspr	SPRN_LPID,r10

	ld	r10, HSTATE_KVM_VCPU(r13)
	ld	r10, VCPU_KVM(r10)
	ld	r10, KVM_HOST_LPCR(r10)
	mtspr	SPRN_LPCR,r10

	isync

	/*
	 * Set GUEST_MODE_NONE so the handler won't branch to KVM, and clear
	 * MSR_RI in r12 ([H]SRR1) so the handler won't try to return.
	 */
	li	r10,KVM_GUEST_MODE_NONE
	stb	r10,HSTATE_IN_GUEST(r13)
	li	r10,MSR_RI
	andc	r12,r12,r10

	/*
	 * Go back to interrupt handler. MCE and SRESET have their specific
	 * PACA save area so they should be used directly. They set up their
	 * own stack. The other handlers all use EXGEN. They will use the
	 * guest r1 if it looks like a kernel stack, so just load the
	 * emergency stack and go to program check for all other interrupts.
	 */
	ld	r10,HSTATE_SCRATCH0(r13)
	cmpwi	r10,BOOK3S_INTERRUPT_MACHINE_CHECK
	beq	.Lcall_machine_check_common

	cmpwi	r10,BOOK3S_INTERRUPT_SYSTEM_RESET
	beq	.Lcall_system_reset_common

	b	.

.Lcall_machine_check_common:
	b	machine_check_common

.Lcall_system_reset_common:
	b	system_reset_common
#endif
