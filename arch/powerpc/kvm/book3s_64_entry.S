/* SPDX-License-Identifier: GPL-2.0-only */
#include <asm/asm-offsets.h>
#include <asm/cache.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_book3s_asm.h>
#include <asm/ppc_asm.h>
#include <asm/reg.h>

/*
 * This is branched to from interrupt handlers in exception-64s.S which set
 * IKVM_REAL or IKVM_VIRT, if HSTATE_IN_GUEST was found to be non-zero.
 */
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
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	std	r9,HSTATE_SCRATCH2(r13)
	lbz	r9,HSTATE_IN_GUEST(r13)
	cmpwi	r9,KVM_GUEST_MODE_HOST_HV
	beq	kvmppc_bad_host_intr
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	cmpwi	r9,KVM_GUEST_MODE_GUEST
	ld	r9,HSTATE_SCRATCH2(r13)
	beq	kvmppc_interrupt_pr
#endif
	b	kvmppc_interrupt_hv
#else
	b	kvmppc_interrupt_pr
#endif
