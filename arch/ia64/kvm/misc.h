#ifndef __KVM_IA64_MISC_H
#define __KVM_IA64_MISC_H

#include <linux/kvm_host.h>
/*
 * misc.h
 * 	Copyright (C) 2007, Intel Corporation.
 *  	Xiantao Zhang  (xiantao.zhang@intel.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */

/*
 *Return p2m base address at host side!
 */
static inline uint64_t *kvm_host_get_pmt(struct kvm *kvm)
{
	return (uint64_t *)(kvm->arch.vm_base +
				offsetof(struct kvm_vm_data, kvm_p2m));
}

static inline void kvm_set_pmt_entry(struct kvm *kvm, gfn_t gfn,
		u64 paddr, u64 mem_flags)
{
	uint64_t *pmt_base = kvm_host_get_pmt(kvm);
	unsigned long pte;

	pte = PAGE_ALIGN(paddr) | mem_flags;
	pmt_base[gfn] = pte;
}

/*Function for translating host address to guest address*/

static inline void *to_guest(struct kvm *kvm, void *addr)
{
	return (void *)((unsigned long)(addr) - kvm->arch.vm_base +
			KVM_VM_DATA_BASE);
}

/*Function for translating guest address to host address*/

static inline void *to_host(struct kvm *kvm, void *addr)
{
	return (void *)((unsigned long)addr - KVM_VM_DATA_BASE
			+ kvm->arch.vm_base);
}

/* Get host context of the vcpu */
static inline union context *kvm_get_host_context(struct kvm_vcpu *vcpu)
{
	union context *ctx = &vcpu->arch.host;
	return to_guest(vcpu->kvm, ctx);
}

/* Get guest context of the vcpu */
static inline union context *kvm_get_guest_context(struct kvm_vcpu *vcpu)
{
	union context *ctx = &vcpu->arch.guest;
	return  to_guest(vcpu->kvm, ctx);
}

/* kvm get exit data from gvmm! */
static inline struct exit_ctl_data *kvm_get_exit_data(struct kvm_vcpu *vcpu)
{
	return &vcpu->arch.exit_data;
}

/*kvm get vcpu ioreq for kvm module!*/
static inline struct kvm_mmio_req *kvm_get_vcpu_ioreq(struct kvm_vcpu *vcpu)
{
	struct exit_ctl_data *p_ctl_data;

	if (vcpu) {
		p_ctl_data = kvm_get_exit_data(vcpu);
		if (p_ctl_data->exit_reason == EXIT_REASON_MMIO_INSTRUCTION)
			return &p_ctl_data->u.ioreq;
	}

	return NULL;
}

#endif
