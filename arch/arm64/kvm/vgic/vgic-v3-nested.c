// SPDX-License-Identifier: GPL-2.0-only

#include <linux/cpu.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#include <kvm/arm_vgic.h>

#include <asm/kvm_arm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_nested.h>

#include "vgic.h"

#define ICH_LRN(n)	(ICH_LR0_EL2 + (n))

struct mi_state {
	u16	eisr;
	u16	elrsr;
	bool	pend;
};

/*
 * Nesting GICv3 support
 *
 * System register emulation:
 *
 * We get two classes of registers:
 *
 * - those backed by memory (LRs, APRs, HCR, VMCR): L1 can freely access
 *   them, and L0 doesn't see a thing.
 *
 * - those that always trap (ELRSR, EISR, MISR): these are status registers
 *   that are built on the fly based on the in-memory state.
 *
 * Only L1 can access the ICH_*_EL2 registers. A non-NV L2 obviously cannot,
 * and a NV L2 would either access the VNCR page provided by L1 (memory
 * based registers), or see the access redirected to L1 (registers that
 * trap) thanks to NV being set by L1.
 */

static bool lr_triggers_eoi(u64 lr)
{
	return !(lr & (ICH_LR_STATE | ICH_LR_HW)) && (lr & ICH_LR_EOI);
}

static void vgic_compute_mi_state(struct kvm_vcpu *vcpu, struct mi_state *mi_state)
{
	u16 eisr = 0, elrsr = 0;
	bool pend = false;

	for (int i = 0; i < kvm_vgic_global_state.nr_lr; i++) {
		u64 lr = __vcpu_sys_reg(vcpu, ICH_LRN(i));

		if (lr_triggers_eoi(lr))
			eisr |= BIT(i);
		if (!(lr & ICH_LR_STATE))
			elrsr |= BIT(i);
		pend |= (lr & ICH_LR_PENDING_BIT);
	}

	mi_state->eisr	= eisr;
	mi_state->elrsr	= elrsr;
	mi_state->pend	= pend;
}

u16 vgic_v3_get_eisr(struct kvm_vcpu *vcpu)
{
	struct mi_state mi_state;

	vgic_compute_mi_state(vcpu, &mi_state);
	return mi_state.eisr;
}

u16 vgic_v3_get_elrsr(struct kvm_vcpu *vcpu)
{
	struct mi_state mi_state;

	vgic_compute_mi_state(vcpu, &mi_state);
	return mi_state.elrsr;
}

u64 vgic_v3_get_misr(struct kvm_vcpu *vcpu)
{
	struct mi_state mi_state;
	u64 reg = 0, hcr, vmcr;

	hcr = __vcpu_sys_reg(vcpu, ICH_HCR_EL2);
	vmcr = __vcpu_sys_reg(vcpu, ICH_VMCR_EL2);

	vgic_compute_mi_state(vcpu, &mi_state);

	if (mi_state.eisr)
		reg |= ICH_MISR_EL2_EOI;

	if (__vcpu_sys_reg(vcpu, ICH_HCR_EL2) & ICH_HCR_EL2_UIE) {
		int used_lrs = kvm_vgic_global_state.nr_lr;

		used_lrs -= hweight16(mi_state.elrsr);
		reg |= (used_lrs <= 1) ? ICH_MISR_EL2_U : 0;
	}

	if ((hcr & ICH_HCR_EL2_LRENPIE) && FIELD_GET(ICH_HCR_EL2_EOIcount_MASK, hcr))
		reg |= ICH_MISR_EL2_LRENP;

	if ((hcr & ICH_HCR_EL2_NPIE) && !mi_state.pend)
		reg |= ICH_MISR_EL2_NP;

	if ((hcr & ICH_HCR_EL2_VGrp0EIE) && (vmcr & ICH_VMCR_ENG0_MASK))
		reg |= ICH_MISR_EL2_VGrp0E;

	if ((hcr & ICH_HCR_EL2_VGrp0DIE) && !(vmcr & ICH_VMCR_ENG0_MASK))
		reg |= ICH_MISR_EL2_VGrp0D;

	if ((hcr & ICH_HCR_EL2_VGrp1EIE) && (vmcr & ICH_VMCR_ENG1_MASK))
		reg |= ICH_MISR_EL2_VGrp1E;

	if ((hcr & ICH_HCR_EL2_VGrp1DIE) && !(vmcr & ICH_VMCR_ENG1_MASK))
		reg |= ICH_MISR_EL2_VGrp1D;

	return reg;
}
