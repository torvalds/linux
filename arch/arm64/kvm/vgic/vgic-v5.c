// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025, 2026 Arm Ltd.
 */

#include <kvm/arm_vgic.h>

#include <linux/bitops.h>
#include <linux/irqchip/arm-vgic-info.h>

#include "vgic.h"

static struct vgic_v5_ppi_caps ppi_caps;

/*
 * Not all PPIs are guaranteed to be implemented for GICv5. Deterermine which
 * ones are, and generate a mask.
 */
static void vgic_v5_get_implemented_ppis(void)
{
	if (!cpus_have_final_cap(ARM64_HAS_GICV5_CPUIF))
		return;

	/*
	 * If we have KVM, we have EL2, which means that we have support for the
	 * EL1 and EL2 Physical & Virtual timers.
	 */
	__assign_bit(GICV5_ARCH_PPI_CNTHP, ppi_caps.impl_ppi_mask, 1);
	__assign_bit(GICV5_ARCH_PPI_CNTV, ppi_caps.impl_ppi_mask, 1);
	__assign_bit(GICV5_ARCH_PPI_CNTHV, ppi_caps.impl_ppi_mask, 1);
	__assign_bit(GICV5_ARCH_PPI_CNTP, ppi_caps.impl_ppi_mask, 1);

	/* The SW_PPI should be available */
	__assign_bit(GICV5_ARCH_PPI_SW_PPI, ppi_caps.impl_ppi_mask, 1);

	/* The PMUIRQ is available if we have the PMU */
	__assign_bit(GICV5_ARCH_PPI_PMUIRQ, ppi_caps.impl_ppi_mask, system_supports_pmuv3());
}

/*
 * Probe for a vGICv5 compatible interrupt controller, returning 0 on success.
 * Currently only supports GICv3-based VMs on a GICv5 host, and hence only
 * registers a VGIC_V3 device.
 */
int vgic_v5_probe(const struct gic_kvm_info *info)
{
	u64 ich_vtr_el2;
	int ret;

	vgic_v5_get_implemented_ppis();

	if (!cpus_have_final_cap(ARM64_HAS_GICV5_LEGACY))
		return -ENODEV;

	kvm_vgic_global_state.type = VGIC_V5;
	kvm_vgic_global_state.has_gcie_v3_compat = true;

	/* We only support v3 compat mode - use vGICv3 limits */
	kvm_vgic_global_state.max_gic_vcpus = VGIC_V3_MAX_CPUS;

	kvm_vgic_global_state.vcpu_base = 0;
	kvm_vgic_global_state.vctrl_base = NULL;
	kvm_vgic_global_state.can_emulate_gicv2 = false;
	kvm_vgic_global_state.has_gicv4 = false;
	kvm_vgic_global_state.has_gicv4_1 = false;

	ich_vtr_el2 =  kvm_call_hyp_ret(__vgic_v3_get_gic_config);
	kvm_vgic_global_state.ich_vtr_el2 = (u32)ich_vtr_el2;

	/*
	 * The ListRegs field is 5 bits, but there is an architectural
	 * maximum of 16 list registers. Just ignore bit 4...
	 */
	kvm_vgic_global_state.nr_lr = (ich_vtr_el2 & 0xf) + 1;

	ret = kvm_register_vgic_device(KVM_DEV_TYPE_ARM_VGIC_V3);
	if (ret) {
		kvm_err("Cannot register GICv3-legacy KVM device.\n");
		return ret;
	}

	static_branch_enable(&kvm_vgic_global_state.gicv3_cpuif);
	kvm_info("GCIE legacy system register CPU interface\n");

	vgic_v3_enable_cpuif_traps();

	return 0;
}

int vgic_v5_finalize_ppi_state(struct kvm *kvm)
{
	struct kvm_vcpu *vcpu0;
	int i;

	if (!vgic_is_v5(kvm))
		return 0;

	/* The PPI state for all VCPUs should be the same. Pick the first. */
	vcpu0 = kvm_get_vcpu(kvm, 0);

	bitmap_zero(kvm->arch.vgic.gicv5_vm.vgic_ppi_mask, VGIC_V5_NR_PRIVATE_IRQS);
	bitmap_zero(kvm->arch.vgic.gicv5_vm.vgic_ppi_hmr, VGIC_V5_NR_PRIVATE_IRQS);

	for_each_set_bit(i, ppi_caps.impl_ppi_mask, VGIC_V5_NR_PRIVATE_IRQS) {
		const u32 intid = vgic_v5_make_ppi(i);
		struct vgic_irq *irq;

		irq = vgic_get_vcpu_irq(vcpu0, intid);

		/* Expose PPIs with an owner or the SW_PPI, only */
		scoped_guard(raw_spinlock_irqsave, &irq->irq_lock) {
			if (irq->owner || i == GICV5_ARCH_PPI_SW_PPI) {
				__assign_bit(i, kvm->arch.vgic.gicv5_vm.vgic_ppi_mask, 1);
				__assign_bit(i, kvm->arch.vgic.gicv5_vm.vgic_ppi_hmr,
					     irq->config == VGIC_CONFIG_LEVEL);
			}
		}

		vgic_put_irq(vcpu0->kvm, irq);
	}

	return 0;
}

void vgic_v5_load(struct kvm_vcpu *vcpu)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;

	/*
	 * On the WFI path, vgic_load is called a second time. The first is when
	 * scheduling in the vcpu thread again, and the second is when leaving
	 * WFI. Skip the second instance as it serves no purpose and just
	 * restores the same state again.
	 */
	if (cpu_if->gicv5_vpe.resident)
		return;

	kvm_call_hyp(__vgic_v5_restore_vmcr_apr, cpu_if);

	cpu_if->gicv5_vpe.resident = true;
}

void vgic_v5_put(struct kvm_vcpu *vcpu)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;

	/*
	 * Do nothing if we're not resident. This can happen in the WFI path
	 * where we do a vgic_put in the WFI path and again later when
	 * descheduling the thread. We risk losing VMCR state if we sync it
	 * twice, so instead return early in this case.
	 */
	if (!cpu_if->gicv5_vpe.resident)
		return;

	kvm_call_hyp(__vgic_v5_save_apr, cpu_if);

	cpu_if->gicv5_vpe.resident = false;
}

void vgic_v5_get_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcrp)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;
	u64 vmcr = cpu_if->vgic_vmcr;

	vmcrp->en = FIELD_GET(FEAT_GCIE_ICH_VMCR_EL2_EN, vmcr);
	vmcrp->pmr = FIELD_GET(FEAT_GCIE_ICH_VMCR_EL2_VPMR, vmcr);
}

void vgic_v5_set_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcrp)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;
	u64 vmcr;

	vmcr = FIELD_PREP(FEAT_GCIE_ICH_VMCR_EL2_VPMR, vmcrp->pmr) |
	       FIELD_PREP(FEAT_GCIE_ICH_VMCR_EL2_EN, vmcrp->en);

	cpu_if->vgic_vmcr = vmcr;
}

void vgic_v5_restore_state(struct kvm_vcpu *vcpu)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;

	__vgic_v5_restore_state(cpu_if);
	__vgic_v5_restore_ppi_state(cpu_if);
	dsb(sy);
}

void vgic_v5_save_state(struct kvm_vcpu *vcpu)
{
	struct vgic_v5_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v5;

	__vgic_v5_save_state(cpu_if);
	__vgic_v5_save_ppi_state(cpu_if);
	dsb(sy);
}
