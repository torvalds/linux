/* SPDX-License-Identifier: GPL-2.0-only */
#include <asm/asm-offsets.h>
#include <asm/cache.h>
#include <asm/exception-64s.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_book3s_asm.h>
#include <asm/ppc_asm.h>
#include <asm/reg.h>

/*
 * These are branched to from interrupt handlers in exception-64s.S which set
 * IKVM_REAL or IKVM_VIRT, if HSTATE_IN_GUEST was found to be non-zero.
 */
.global	kvmppc_hcall
.balign IFETCH_ALIGN_BYTES
kvmppc_hcall:
	/*
	 * This is a hcall, so register convention is as
	 * Documentation/powerpc/papr_hcalls.rst, with these additions:
	 * R13		= PACA
	 * guest R13 saved in SPRN_SCRATCH0
	 * R10		= free
	 * guest r10 saved in PACA_EXGEN
	 *
	 * This may also be a syscall from PR-KVM userspace that is to be
	 * reflected to the PR guest kernel, so registers may be set up for
	 * a system call rather than hcall. We don't currently clobber
	 * anything here, but the 0xc00 handler has already clobbered CTR
	 * and CR0, so PR-KVM can not support a guest kernel that preserves
	 * those registers across its system calls.
	 */
	 /*
	  * Save the PPR (on systems that support it) before changing to
	  * HMT_MEDIUM. That allows the KVM code to save that value into the
	  * guest state (it is the guest's PPR value).
	  */
BEGIN_FTR_SECTION
	mfspr	r10,SPRN_PPR
	std	r10,HSTATE_PPR(r13)
END_FTR_SECTION_IFSET(CPU_FTR_HAS_PPR)
	HMT_MEDIUM
	mfcr	r10
	std	r12,HSTATE_SCRATCH0(r13)
	sldi	r12,r10,32
	ori	r12,r12,0xc00
	ld	r10,PACA_EXGEN+EX_R10(r13)

.global	kvmppc_interrupt
.balign IFETCH_ALIGN_BYTES
kvmppc_interrupt:
	/*
	 * Register contents:
	 * R12		= (guest CR << 32) | interrupt vector
	 * R13		= PACA
	 * guest R12 saved in shadow VCPU SCRATCH0
	 * guest R13 saved in SPRN_SCRATCH0
	 */
	std	r9,HSTATE_SCRATCH2(r13)
	lbz	r9,HSTATE_IN_GUEST(r13)
	cmpwi	r9,KVM_GUEST_MODE_SKIP
	beq-	.Lmaybe_skip
.Lno_skip:
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	cmpwi	r9,KVM_GUEST_MODE_HOST_HV
	beq	kvmppc_bad_host_intr
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	cmpwi	r9,KVM_GUEST_MODE_GUEST
	ld	r9,HSTATE_SCRATCH2(r13)
	beq	kvmppc_interrupt_pr
#endif
	b	kvmppc_interrupt_hv
#else
	ld	r9,HSTATE_SCRATCH2(r13)
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
