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
 * Copyright IBM Corp. 2008
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#ifndef __POWERPC_KVM_PPC_H__
#define __POWERPC_KVM_PPC_H__

/* This file exists just so we can dereference kvm_vcpu, avoiding nested header
 * dependencies. */

#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/kvm_types.h>
#include <linux/kvm_host.h>

enum emulation_result {
	EMULATE_DONE,         /* no further processing */
	EMULATE_DO_MMIO,      /* kvm_run filled with MMIO request */
	EMULATE_DO_DCR,       /* kvm_run filled with DCR request */
	EMULATE_FAIL,         /* can't emulate this instruction */
};

extern const unsigned char exception_priority[];
extern const unsigned char priority_exception[];

extern int __kvmppc_vcpu_run(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu);
extern char kvmppc_handlers_start[];
extern unsigned long kvmppc_handler_len;

extern void kvmppc_dump_vcpu(struct kvm_vcpu *vcpu);
extern int kvmppc_handle_load(struct kvm_run *run, struct kvm_vcpu *vcpu,
                              unsigned int rt, unsigned int bytes,
                              int is_bigendian);
extern int kvmppc_handle_store(struct kvm_run *run, struct kvm_vcpu *vcpu,
                               u32 val, unsigned int bytes, int is_bigendian);

extern int kvmppc_emulate_instruction(struct kvm_run *run,
                                      struct kvm_vcpu *vcpu);
extern int kvmppc_emulate_mmio(struct kvm_run *run, struct kvm_vcpu *vcpu);
extern int kvmppc_emul_tlbwe(struct kvm_vcpu *vcpu, u8 ra, u8 rs, u8 ws);
extern int kvmppc_emul_tlbsx(struct kvm_vcpu *vcpu, u8 rt, u8 ra, u8 rb, u8 rc);

extern void kvmppc_mmu_map(struct kvm_vcpu *vcpu, u64 gvaddr, gfn_t gfn,
                           u64 asid, u32 flags);
extern void kvmppc_mmu_priv_switch(struct kvm_vcpu *vcpu, int usermode);
extern void kvmppc_mmu_switch_pid(struct kvm_vcpu *vcpu, u32 pid);

/* Helper function for "full" MSR writes. No need to call this if only EE is
 * changing. */
static inline void kvmppc_set_msr(struct kvm_vcpu *vcpu, u32 new_msr)
{
	if ((new_msr & MSR_PR) != (vcpu->arch.msr & MSR_PR))
		kvmppc_mmu_priv_switch(vcpu, new_msr & MSR_PR);

	vcpu->arch.msr = new_msr;

	if (vcpu->arch.msr & MSR_WE)
		kvm_vcpu_block(vcpu);
}

static inline void kvmppc_set_pid(struct kvm_vcpu *vcpu, u32 new_pid)
{
	if (vcpu->arch.pid != new_pid) {
		vcpu->arch.pid = new_pid;
		vcpu->arch.swap_pid = 1;
	}
}

/* Core-specific hooks */

extern int kvmppc_core_check_processor_compat(void);

extern void kvmppc_core_vcpu_load(struct kvm_vcpu *vcpu, int cpu);
extern void kvmppc_core_vcpu_put(struct kvm_vcpu *vcpu);

extern void kvmppc_core_load_guest_debugstate(struct kvm_vcpu *vcpu);
extern void kvmppc_core_load_host_debugstate(struct kvm_vcpu *vcpu);

extern void kvmppc_core_deliver_interrupts(struct kvm_vcpu *vcpu);
extern int kvmppc_core_pending_dec(struct kvm_vcpu *vcpu);
extern void kvmppc_core_queue_program(struct kvm_vcpu *vcpu);
extern void kvmppc_core_queue_dec(struct kvm_vcpu *vcpu);
extern void kvmppc_core_queue_external(struct kvm_vcpu *vcpu,
                                       struct kvm_interrupt *irq);

extern void kvmppc_core_destroy_mmu(struct kvm_vcpu *vcpu);

#endif /* __POWERPC_KVM_PPC_H__ */
