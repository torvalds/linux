// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 - Google Inc
 * Author: Andrew Scull <ascull@google.com>
 */

#include <hyp/switch.h>

#include <asm/kvm_asm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_host.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>

#include <kvm/arm_hypercalls.h>

static void handle_host_hcall(unsigned long func_id,
			      struct kvm_cpu_context *host_ctxt)
{
	unsigned long ret = 0;

	switch (func_id) {
	case KVM_HOST_SMCCC_FUNC(__kvm_vcpu_run): {
		unsigned long r1 = host_ctxt->regs.regs[1];
		struct kvm_vcpu *vcpu = (struct kvm_vcpu *)r1;

		ret = __kvm_vcpu_run(kern_hyp_va(vcpu));
		break;
	}
	case KVM_HOST_SMCCC_FUNC(__kvm_flush_vm_context):
		__kvm_flush_vm_context();
		break;
	case KVM_HOST_SMCCC_FUNC(__kvm_tlb_flush_vmid_ipa): {
		unsigned long r1 = host_ctxt->regs.regs[1];
		struct kvm_s2_mmu *mmu = (struct kvm_s2_mmu *)r1;
		phys_addr_t ipa = host_ctxt->regs.regs[2];
		int level = host_ctxt->regs.regs[3];

		__kvm_tlb_flush_vmid_ipa(kern_hyp_va(mmu), ipa, level);
		break;
	}
	case KVM_HOST_SMCCC_FUNC(__kvm_tlb_flush_vmid): {
		unsigned long r1 = host_ctxt->regs.regs[1];
		struct kvm_s2_mmu *mmu = (struct kvm_s2_mmu *)r1;

		__kvm_tlb_flush_vmid(kern_hyp_va(mmu));
		break;
	}
	case KVM_HOST_SMCCC_FUNC(__kvm_flush_cpu_context): {
		unsigned long r1 = host_ctxt->regs.regs[1];
		struct kvm_s2_mmu *mmu = (struct kvm_s2_mmu *)r1;

		__kvm_flush_cpu_context(kern_hyp_va(mmu));
		break;
	}
	case KVM_HOST_SMCCC_FUNC(__kvm_timer_set_cntvoff): {
		u64 cntvoff = host_ctxt->regs.regs[1];

		__kvm_timer_set_cntvoff(cntvoff);
		break;
	}
	case KVM_HOST_SMCCC_FUNC(__kvm_enable_ssbs):
		__kvm_enable_ssbs();
		break;
	case KVM_HOST_SMCCC_FUNC(__vgic_v3_get_ich_vtr_el2):
		ret = __vgic_v3_get_ich_vtr_el2();
		break;
	case KVM_HOST_SMCCC_FUNC(__vgic_v3_read_vmcr):
		ret = __vgic_v3_read_vmcr();
		break;
	case KVM_HOST_SMCCC_FUNC(__vgic_v3_write_vmcr): {
		u32 vmcr = host_ctxt->regs.regs[1];

		__vgic_v3_write_vmcr(vmcr);
		break;
	}
	case KVM_HOST_SMCCC_FUNC(__vgic_v3_init_lrs):
		__vgic_v3_init_lrs();
		break;
	case KVM_HOST_SMCCC_FUNC(__kvm_get_mdcr_el2):
		ret = __kvm_get_mdcr_el2();
		break;
	case KVM_HOST_SMCCC_FUNC(__vgic_v3_save_aprs): {
		unsigned long r1 = host_ctxt->regs.regs[1];
		struct vgic_v3_cpu_if *cpu_if = (struct vgic_v3_cpu_if *)r1;

		__vgic_v3_save_aprs(kern_hyp_va(cpu_if));
		break;
	}
	case KVM_HOST_SMCCC_FUNC(__vgic_v3_restore_aprs): {
		unsigned long r1 = host_ctxt->regs.regs[1];
		struct vgic_v3_cpu_if *cpu_if = (struct vgic_v3_cpu_if *)r1;

		__vgic_v3_restore_aprs(kern_hyp_va(cpu_if));
		break;
	}
	default:
		/* Invalid host HVC. */
		host_ctxt->regs.regs[0] = SMCCC_RET_NOT_SUPPORTED;
		return;
	}

	host_ctxt->regs.regs[0] = SMCCC_RET_SUCCESS;
	host_ctxt->regs.regs[1] = ret;
}

void handle_trap(struct kvm_cpu_context *host_ctxt)
{
	u64 esr = read_sysreg_el2(SYS_ESR);
	unsigned long func_id;

	if (ESR_ELx_EC(esr) != ESR_ELx_EC_HVC64)
		hyp_panic();

	func_id = host_ctxt->regs.regs[0];
	handle_host_hcall(func_id, host_ctxt);
}
