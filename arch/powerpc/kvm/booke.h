/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright IBM Corp. 2008
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#ifndef __KVM_BOOKE_H__
#define __KVM_BOOKE_H__

#include <linux/types.h>
#include <linux/kvm_host.h>
#include <asm/kvm_ppc.h>
#include <asm/switch_to.h>
#include "timing.h"

/* interrupt priortity ordering */
#define BOOKE_IRQPRIO_DATA_STORAGE 0
#define BOOKE_IRQPRIO_INST_STORAGE 1
#define BOOKE_IRQPRIO_ALIGNMENT 2
#define BOOKE_IRQPRIO_PROGRAM 3
#define BOOKE_IRQPRIO_FP_UNAVAIL 4
#ifdef CONFIG_SPE_POSSIBLE
#define BOOKE_IRQPRIO_SPE_UNAVAIL 5
#define BOOKE_IRQPRIO_SPE_FP_DATA 6
#define BOOKE_IRQPRIO_SPE_FP_ROUND 7
#endif
#ifdef CONFIG_PPC_E500MC
#define BOOKE_IRQPRIO_ALTIVEC_UNAVAIL 5
#define BOOKE_IRQPRIO_ALTIVEC_ASSIST 6
#endif
#define BOOKE_IRQPRIO_SYSCALL 8
#define BOOKE_IRQPRIO_AP_UNAVAIL 9
#define BOOKE_IRQPRIO_DTLB_MISS 10
#define BOOKE_IRQPRIO_ITLB_MISS 11
#define BOOKE_IRQPRIO_MACHINE_CHECK 12
#define BOOKE_IRQPRIO_DEBUG 13
#define BOOKE_IRQPRIO_CRITICAL 14
#define BOOKE_IRQPRIO_WATCHDOG 15
#define BOOKE_IRQPRIO_EXTERNAL 16
#define BOOKE_IRQPRIO_FIT 17
#define BOOKE_IRQPRIO_DECREMENTER 18
#define BOOKE_IRQPRIO_PERFORMANCE_MONITOR 19
/* Internal pseudo-irqprio for level triggered externals */
#define BOOKE_IRQPRIO_EXTERNAL_LEVEL 20
#define BOOKE_IRQPRIO_DBELL 21
#define BOOKE_IRQPRIO_DBELL_CRIT 22
#define BOOKE_IRQPRIO_MAX 23

#define BOOKE_IRQMASK_EE ((1 << BOOKE_IRQPRIO_EXTERNAL_LEVEL) | \
			  (1 << BOOKE_IRQPRIO_PERFORMANCE_MONITOR) | \
			  (1 << BOOKE_IRQPRIO_DBELL) | \
			  (1 << BOOKE_IRQPRIO_DECREMENTER) | \
			  (1 << BOOKE_IRQPRIO_FIT) | \
			  (1 << BOOKE_IRQPRIO_EXTERNAL))

#define BOOKE_IRQMASK_CE ((1 << BOOKE_IRQPRIO_DBELL_CRIT) | \
			  (1 << BOOKE_IRQPRIO_WATCHDOG) | \
			  (1 << BOOKE_IRQPRIO_CRITICAL))

extern unsigned long kvmppc_booke_handlers;
extern unsigned long kvmppc_booke_handler_addr[];

void kvmppc_set_msr(struct kvm_vcpu *vcpu, u32 new_msr);
void kvmppc_mmu_msr_notify(struct kvm_vcpu *vcpu, u32 old_msr);

void kvmppc_set_epcr(struct kvm_vcpu *vcpu, u32 new_epcr);
void kvmppc_set_tcr(struct kvm_vcpu *vcpu, u32 new_tcr);
void kvmppc_set_tsr_bits(struct kvm_vcpu *vcpu, u32 tsr_bits);
void kvmppc_clr_tsr_bits(struct kvm_vcpu *vcpu, u32 tsr_bits);

int kvmppc_booke_emulate_op(struct kvm_run *run, struct kvm_vcpu *vcpu,
                            unsigned int inst, int *advance);
int kvmppc_booke_emulate_mfspr(struct kvm_vcpu *vcpu, int sprn, ulong *spr_val);
int kvmppc_booke_emulate_mtspr(struct kvm_vcpu *vcpu, int sprn, ulong spr_val);

/* low-level asm code to transfer guest state */
void kvmppc_load_guest_spe(struct kvm_vcpu *vcpu);
void kvmppc_save_guest_spe(struct kvm_vcpu *vcpu);

/* high-level function, manages flags, host state */
void kvmppc_vcpu_disable_spe(struct kvm_vcpu *vcpu);

void kvmppc_booke_vcpu_load(struct kvm_vcpu *vcpu, int cpu);
void kvmppc_booke_vcpu_put(struct kvm_vcpu *vcpu);

enum int_class {
	INT_CLASS_NONCRIT,
	INT_CLASS_CRIT,
	INT_CLASS_MC,
	INT_CLASS_DBG,
};

void kvmppc_set_pending_interrupt(struct kvm_vcpu *vcpu, enum int_class type);

extern int kvmppc_core_emulate_op_e500(struct kvm_run *run,
				       struct kvm_vcpu *vcpu,
				       unsigned int inst, int *advance);
extern int kvmppc_core_emulate_mtspr_e500(struct kvm_vcpu *vcpu, int sprn,
					  ulong spr_val);
extern int kvmppc_core_emulate_mfspr_e500(struct kvm_vcpu *vcpu, int sprn,
					  ulong *spr_val);
extern int kvmppc_core_emulate_op_e500(struct kvm_run *run,
				       struct kvm_vcpu *vcpu,
				       unsigned int inst, int *advance);
extern int kvmppc_core_emulate_mtspr_e500(struct kvm_vcpu *vcpu, int sprn,
					  ulong spr_val);
extern int kvmppc_core_emulate_mfspr_e500(struct kvm_vcpu *vcpu, int sprn,
					  ulong *spr_val);

static inline void kvmppc_clear_dbsr(void)
{
	mtspr(SPRN_DBSR, mfspr(SPRN_DBSR));
}
#endif /* __KVM_BOOKE_H__ */
