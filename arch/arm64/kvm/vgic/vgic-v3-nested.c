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
#define ICH_AP0RN(n)	(ICH_AP0R0_EL2 + (n))
#define ICH_AP1RN(n)	(ICH_AP1R0_EL2 + (n))

struct mi_state {
	u16	eisr;
	u16	elrsr;
	bool	pend;
};

/*
 * The shadow registers loaded to the hardware when running a L2 guest
 * with the virtual IMO/FMO bits set.
 */
struct shadow_if {
	struct vgic_v3_cpu_if	cpuif;
	unsigned long		lr_map;
};

static DEFINE_PER_CPU(struct shadow_if, shadow_if);

/*
 * Nesting GICv3 support
 *
 * On a non-nesting VM (only running at EL0/EL1), the host hypervisor
 * completely controls the interrupts injected via the list registers.
 * Consequently, most of the state that is modified by the guest (by ACK-ing
 * and EOI-ing interrupts) is synced by KVM on each entry/exit, so that we
 * keep a semi-consistent view of the interrupts.
 *
 * This still applies for a NV guest, but only while "InHost" (either
 * running at EL2, or at EL0 with HCR_EL2.{E2H.TGE}=={1,1}.
 *
 * When running a L2 guest ("not InHost"), things are radically different,
 * as the L1 guest is in charge of provisioning the interrupts via its own
 * view of the ICH_LR*_EL2 registers, which conveniently live in the VNCR
 * page.  This means that the flow described above does work (there is no
 * state to rebuild in the L0 hypervisor), and that most things happed on L2
 * load/put:
 *
 * - on L2 load: move the in-memory L1 vGIC configuration into a shadow,
 *   per-CPU data structure that is used to populate the actual LRs. This is
 *   an extra copy that we could avoid, but life is short. In the process,
 *   we remap any interrupt that has the HW bit set to the mapped interrupt
 *   on the host, should the host consider it a HW one. This allows the HW
 *   deactivation to take its course, such as for the timer.
 *
 * - on L2 put: perform the inverse transformation, so that the result of L2
 *   running becomes visible to L1 in the VNCR-accessible registers.
 *
 * - there is nothing to do on L2 entry, as everything will have happened
 *   on load. However, this is the point where we detect that an interrupt
 *   targeting L1 and prepare the grand switcheroo.
 *
 * - on L2 exit: emulate the HW bit, and deactivate corresponding the L1
 *   interrupt. The L0 active state will be cleared by the HW if the L1
 *   interrupt was itself backed by a HW interrupt.
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

bool vgic_state_is_nested(struct kvm_vcpu *vcpu)
{
	u64 xmo;

	if (vcpu_has_nv(vcpu) && !is_hyp_ctxt(vcpu)) {
		xmo = __vcpu_sys_reg(vcpu, HCR_EL2) & (HCR_IMO | HCR_FMO);
		WARN_ONCE(xmo && xmo != (HCR_IMO | HCR_FMO),
			  "Separate virtual IRQ/FIQ settings not supported\n");

		return !!xmo;
	}

	return false;
}

static struct shadow_if *get_shadow_if(void)
{
	return this_cpu_ptr(&shadow_if);
}

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

/*
 * For LRs which have HW bit set such as timer interrupts, we modify them to
 * have the host hardware interrupt number instead of the virtual one programmed
 * by the guest hypervisor.
 */
static void vgic_v3_create_shadow_lr(struct kvm_vcpu *vcpu,
				     struct vgic_v3_cpu_if *s_cpu_if)
{
	unsigned long lr_map = 0;
	int index = 0;

	for (int i = 0; i < kvm_vgic_global_state.nr_lr; i++) {
		u64 lr = __vcpu_sys_reg(vcpu, ICH_LRN(i));
		struct vgic_irq *irq;

		if (!(lr & ICH_LR_STATE))
			lr = 0;

		if (!(lr & ICH_LR_HW))
			goto next;

		/* We have the HW bit set, check for validity of pINTID */
		irq = vgic_get_vcpu_irq(vcpu, FIELD_GET(ICH_LR_PHYS_ID_MASK, lr));
		if (!irq || !irq->hw || irq->intid > VGIC_MAX_SPI ) {
			/* There was no real mapping, so nuke the HW bit */
			lr &= ~ICH_LR_HW;
			if (irq)
				vgic_put_irq(vcpu->kvm, irq);
			goto next;
		}

		/* It is illegal to have the EOI bit set with HW */
		lr &= ~ICH_LR_EOI;

		/* Translate the virtual mapping to the real one */
		lr &= ~ICH_LR_PHYS_ID_MASK;
		lr |= FIELD_PREP(ICH_LR_PHYS_ID_MASK, (u64)irq->hwintid);

		vgic_put_irq(vcpu->kvm, irq);

next:
		s_cpu_if->vgic_lr[index] = lr;
		if (lr) {
			lr_map |= BIT(i);
			index++;
		}
	}

	container_of(s_cpu_if, struct shadow_if, cpuif)->lr_map = lr_map;
	s_cpu_if->used_lrs = index;
}

void vgic_v3_sync_nested(struct kvm_vcpu *vcpu)
{
	struct shadow_if *shadow_if = get_shadow_if();
	int i, index = 0;

	for_each_set_bit(i, &shadow_if->lr_map, kvm_vgic_global_state.nr_lr) {
		u64 lr = __vcpu_sys_reg(vcpu, ICH_LRN(i));
		struct vgic_irq *irq;

		if (!(lr & ICH_LR_HW) || !(lr & ICH_LR_STATE))
			goto next;

		/*
		 * If we had a HW lr programmed by the guest hypervisor, we
		 * need to emulate the HW effect between the guest hypervisor
		 * and the nested guest.
		 */
		irq = vgic_get_vcpu_irq(vcpu, FIELD_GET(ICH_LR_PHYS_ID_MASK, lr));
		if (WARN_ON(!irq)) /* Shouldn't happen as we check on load */
			goto next;

		lr = __gic_v3_get_lr(index);
		if (!(lr & ICH_LR_STATE))
			irq->active = false;

		vgic_put_irq(vcpu->kvm, irq);
	next:
		index++;
	}
}

static void vgic_v3_create_shadow_state(struct kvm_vcpu *vcpu,
					struct vgic_v3_cpu_if *s_cpu_if)
{
	struct vgic_v3_cpu_if *host_if = &vcpu->arch.vgic_cpu.vgic_v3;
	int i;

	s_cpu_if->vgic_hcr = __vcpu_sys_reg(vcpu, ICH_HCR_EL2);
	s_cpu_if->vgic_vmcr = __vcpu_sys_reg(vcpu, ICH_VMCR_EL2);
	s_cpu_if->vgic_sre = host_if->vgic_sre;

	for (i = 0; i < 4; i++) {
		s_cpu_if->vgic_ap0r[i] = __vcpu_sys_reg(vcpu, ICH_AP0RN(i));
		s_cpu_if->vgic_ap1r[i] = __vcpu_sys_reg(vcpu, ICH_AP1RN(i));
	}

	vgic_v3_create_shadow_lr(vcpu, s_cpu_if);
}

void vgic_v3_load_nested(struct kvm_vcpu *vcpu)
{
	struct shadow_if *shadow_if = get_shadow_if();
	struct vgic_v3_cpu_if *cpu_if = &shadow_if->cpuif;

	BUG_ON(!vgic_state_is_nested(vcpu));

	vgic_v3_create_shadow_state(vcpu, cpu_if);

	__vgic_v3_restore_vmcr_aprs(cpu_if);
	__vgic_v3_activate_traps(cpu_if);

	__vgic_v3_restore_state(cpu_if);
}

void vgic_v3_put_nested(struct kvm_vcpu *vcpu)
{
	struct shadow_if *shadow_if = get_shadow_if();
	struct vgic_v3_cpu_if *s_cpu_if = &shadow_if->cpuif;
	int i;

	__vgic_v3_save_vmcr_aprs(s_cpu_if);
	__vgic_v3_deactivate_traps(s_cpu_if);
	__vgic_v3_save_state(s_cpu_if);

	/*
	 * Translate the shadow state HW fields back to the virtual ones
	 * before copying the shadow struct back to the nested one.
	 */
	__vcpu_sys_reg(vcpu, ICH_HCR_EL2) = s_cpu_if->vgic_hcr;
	__vcpu_sys_reg(vcpu, ICH_VMCR_EL2) = s_cpu_if->vgic_vmcr;

	for (i = 0; i < 4; i++) {
		__vcpu_sys_reg(vcpu, ICH_AP0RN(i)) = s_cpu_if->vgic_ap0r[i];
		__vcpu_sys_reg(vcpu, ICH_AP1RN(i)) = s_cpu_if->vgic_ap1r[i];
	}

	for_each_set_bit(i, &shadow_if->lr_map, kvm_vgic_global_state.nr_lr) {
		u64 val = __vcpu_sys_reg(vcpu, ICH_LRN(i));

		val &= ~ICH_LR_STATE;
		val |= s_cpu_if->vgic_lr[i] & ICH_LR_STATE;

		__vcpu_sys_reg(vcpu, ICH_LRN(i)) = val;
		s_cpu_if->vgic_lr[i] = 0;
	}

	shadow_if->lr_map = 0;
}
