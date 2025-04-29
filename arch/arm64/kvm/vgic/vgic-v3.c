// SPDX-License-Identifier: GPL-2.0-only

#include <linux/irqchip/arm-gic-v3.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kstrtox.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/string_choices.h>
#include <kvm/arm_vgic.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_asm.h>

#include "vgic.h"

static bool group0_trap;
static bool group1_trap;
static bool common_trap;
static bool dir_trap;
static bool gicv4_enable;

void vgic_v3_set_underflow(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpuif = &vcpu->arch.vgic_cpu.vgic_v3;

	cpuif->vgic_hcr |= ICH_HCR_EL2_UIE;
}

static bool lr_signals_eoi_mi(u64 lr_val)
{
	return !(lr_val & ICH_LR_STATE) && (lr_val & ICH_LR_EOI) &&
	       !(lr_val & ICH_LR_HW);
}

void vgic_v3_fold_lr_state(struct kvm_vcpu *vcpu)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;
	struct vgic_v3_cpu_if *cpuif = &vgic_cpu->vgic_v3;
	u32 model = vcpu->kvm->arch.vgic.vgic_model;
	int lr;

	DEBUG_SPINLOCK_BUG_ON(!irqs_disabled());

	cpuif->vgic_hcr &= ~ICH_HCR_EL2_UIE;

	for (lr = 0; lr < cpuif->used_lrs; lr++) {
		u64 val = cpuif->vgic_lr[lr];
		u32 intid, cpuid;
		struct vgic_irq *irq;
		bool is_v2_sgi = false;
		bool deactivated;

		cpuid = val & GICH_LR_PHYSID_CPUID;
		cpuid >>= GICH_LR_PHYSID_CPUID_SHIFT;

		if (model == KVM_DEV_TYPE_ARM_VGIC_V3) {
			intid = val & ICH_LR_VIRTUAL_ID_MASK;
		} else {
			intid = val & GICH_LR_VIRTUALID;
			is_v2_sgi = vgic_irq_is_sgi(intid);
		}

		/* Notify fds when the guest EOI'ed a level-triggered IRQ */
		if (lr_signals_eoi_mi(val) && vgic_valid_spi(vcpu->kvm, intid))
			kvm_notify_acked_irq(vcpu->kvm, 0,
					     intid - VGIC_NR_PRIVATE_IRQS);

		irq = vgic_get_vcpu_irq(vcpu, intid);
		if (!irq)	/* An LPI could have been unmapped. */
			continue;

		raw_spin_lock(&irq->irq_lock);

		/* Always preserve the active bit, note deactivation */
		deactivated = irq->active && !(val & ICH_LR_ACTIVE_BIT);
		irq->active = !!(val & ICH_LR_ACTIVE_BIT);

		if (irq->active && is_v2_sgi)
			irq->active_source = cpuid;

		/* Edge is the only case where we preserve the pending bit */
		if (irq->config == VGIC_CONFIG_EDGE &&
		    (val & ICH_LR_PENDING_BIT)) {
			irq->pending_latch = true;

			if (is_v2_sgi)
				irq->source |= (1 << cpuid);
		}

		/*
		 * Clear soft pending state when level irqs have been acked.
		 */
		if (irq->config == VGIC_CONFIG_LEVEL && !(val & ICH_LR_STATE))
			irq->pending_latch = false;

		/* Handle resampling for mapped interrupts if required */
		vgic_irq_handle_resampling(irq, deactivated, val & ICH_LR_PENDING_BIT);

		raw_spin_unlock(&irq->irq_lock);
		vgic_put_irq(vcpu->kvm, irq);
	}

	cpuif->used_lrs = 0;
}

/* Requires the irq to be locked already */
void vgic_v3_populate_lr(struct kvm_vcpu *vcpu, struct vgic_irq *irq, int lr)
{
	u32 model = vcpu->kvm->arch.vgic.vgic_model;
	u64 val = irq->intid;
	bool allow_pending = true, is_v2_sgi;

	is_v2_sgi = (vgic_irq_is_sgi(irq->intid) &&
		     model == KVM_DEV_TYPE_ARM_VGIC_V2);

	if (irq->active) {
		val |= ICH_LR_ACTIVE_BIT;
		if (is_v2_sgi)
			val |= irq->active_source << GICH_LR_PHYSID_CPUID_SHIFT;
		if (vgic_irq_is_multi_sgi(irq)) {
			allow_pending = false;
			val |= ICH_LR_EOI;
		}
	}

	if (irq->hw && !vgic_irq_needs_resampling(irq)) {
		val |= ICH_LR_HW;
		val |= ((u64)irq->hwintid) << ICH_LR_PHYS_ID_SHIFT;
		/*
		 * Never set pending+active on a HW interrupt, as the
		 * pending state is kept at the physical distributor
		 * level.
		 */
		if (irq->active)
			allow_pending = false;
	} else {
		if (irq->config == VGIC_CONFIG_LEVEL) {
			val |= ICH_LR_EOI;

			/*
			 * Software resampling doesn't work very well
			 * if we allow P+A, so let's not do that.
			 */
			if (irq->active)
				allow_pending = false;
		}
	}

	if (allow_pending && irq_is_pending(irq)) {
		val |= ICH_LR_PENDING_BIT;

		if (irq->config == VGIC_CONFIG_EDGE)
			irq->pending_latch = false;

		if (vgic_irq_is_sgi(irq->intid) &&
		    model == KVM_DEV_TYPE_ARM_VGIC_V2) {
			u32 src = ffs(irq->source);

			if (WARN_RATELIMIT(!src, "No SGI source for INTID %d\n",
					   irq->intid))
				return;

			val |= (src - 1) << GICH_LR_PHYSID_CPUID_SHIFT;
			irq->source &= ~(1 << (src - 1));
			if (irq->source) {
				irq->pending_latch = true;
				val |= ICH_LR_EOI;
			}
		}
	}

	/*
	 * Level-triggered mapped IRQs are special because we only observe
	 * rising edges as input to the VGIC.  We therefore lower the line
	 * level here, so that we can take new virtual IRQs.  See
	 * vgic_v3_fold_lr_state for more info.
	 */
	if (vgic_irq_is_mapped_level(irq) && (val & ICH_LR_PENDING_BIT))
		irq->line_level = false;

	if (irq->group)
		val |= ICH_LR_GROUP;

	val |= (u64)irq->priority << ICH_LR_PRIORITY_SHIFT;

	vcpu->arch.vgic_cpu.vgic_v3.vgic_lr[lr] = val;
}

void vgic_v3_clear_lr(struct kvm_vcpu *vcpu, int lr)
{
	vcpu->arch.vgic_cpu.vgic_v3.vgic_lr[lr] = 0;
}

void vgic_v3_set_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcrp)
{
	struct vgic_v3_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v3;
	u32 model = vcpu->kvm->arch.vgic.vgic_model;
	u32 vmcr;

	if (model == KVM_DEV_TYPE_ARM_VGIC_V2) {
		vmcr = (vmcrp->ackctl << ICH_VMCR_ACK_CTL_SHIFT) &
			ICH_VMCR_ACK_CTL_MASK;
		vmcr |= (vmcrp->fiqen << ICH_VMCR_FIQ_EN_SHIFT) &
			ICH_VMCR_FIQ_EN_MASK;
	} else {
		/*
		 * When emulating GICv3 on GICv3 with SRE=1 on the
		 * VFIQEn bit is RES1 and the VAckCtl bit is RES0.
		 */
		vmcr = ICH_VMCR_FIQ_EN_MASK;
	}

	vmcr |= (vmcrp->cbpr << ICH_VMCR_CBPR_SHIFT) & ICH_VMCR_CBPR_MASK;
	vmcr |= (vmcrp->eoim << ICH_VMCR_EOIM_SHIFT) & ICH_VMCR_EOIM_MASK;
	vmcr |= (vmcrp->abpr << ICH_VMCR_BPR1_SHIFT) & ICH_VMCR_BPR1_MASK;
	vmcr |= (vmcrp->bpr << ICH_VMCR_BPR0_SHIFT) & ICH_VMCR_BPR0_MASK;
	vmcr |= (vmcrp->pmr << ICH_VMCR_PMR_SHIFT) & ICH_VMCR_PMR_MASK;
	vmcr |= (vmcrp->grpen0 << ICH_VMCR_ENG0_SHIFT) & ICH_VMCR_ENG0_MASK;
	vmcr |= (vmcrp->grpen1 << ICH_VMCR_ENG1_SHIFT) & ICH_VMCR_ENG1_MASK;

	cpu_if->vgic_vmcr = vmcr;
}

void vgic_v3_get_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcrp)
{
	struct vgic_v3_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v3;
	u32 model = vcpu->kvm->arch.vgic.vgic_model;
	u32 vmcr;

	vmcr = cpu_if->vgic_vmcr;

	if (model == KVM_DEV_TYPE_ARM_VGIC_V2) {
		vmcrp->ackctl = (vmcr & ICH_VMCR_ACK_CTL_MASK) >>
			ICH_VMCR_ACK_CTL_SHIFT;
		vmcrp->fiqen = (vmcr & ICH_VMCR_FIQ_EN_MASK) >>
			ICH_VMCR_FIQ_EN_SHIFT;
	} else {
		/*
		 * When emulating GICv3 on GICv3 with SRE=1 on the
		 * VFIQEn bit is RES1 and the VAckCtl bit is RES0.
		 */
		vmcrp->fiqen = 1;
		vmcrp->ackctl = 0;
	}

	vmcrp->cbpr = (vmcr & ICH_VMCR_CBPR_MASK) >> ICH_VMCR_CBPR_SHIFT;
	vmcrp->eoim = (vmcr & ICH_VMCR_EOIM_MASK) >> ICH_VMCR_EOIM_SHIFT;
	vmcrp->abpr = (vmcr & ICH_VMCR_BPR1_MASK) >> ICH_VMCR_BPR1_SHIFT;
	vmcrp->bpr  = (vmcr & ICH_VMCR_BPR0_MASK) >> ICH_VMCR_BPR0_SHIFT;
	vmcrp->pmr  = (vmcr & ICH_VMCR_PMR_MASK) >> ICH_VMCR_PMR_SHIFT;
	vmcrp->grpen0 = (vmcr & ICH_VMCR_ENG0_MASK) >> ICH_VMCR_ENG0_SHIFT;
	vmcrp->grpen1 = (vmcr & ICH_VMCR_ENG1_MASK) >> ICH_VMCR_ENG1_SHIFT;
}

#define INITIAL_PENDBASER_VALUE						  \
	(GIC_BASER_CACHEABILITY(GICR_PENDBASER, INNER, RaWb)		| \
	GIC_BASER_CACHEABILITY(GICR_PENDBASER, OUTER, SameAsInner)	| \
	GIC_BASER_SHAREABILITY(GICR_PENDBASER, InnerShareable))

void vgic_v3_enable(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *vgic_v3 = &vcpu->arch.vgic_cpu.vgic_v3;

	/*
	 * By forcing VMCR to zero, the GIC will restore the binary
	 * points to their reset values. Anything else resets to zero
	 * anyway.
	 */
	vgic_v3->vgic_vmcr = 0;

	/*
	 * If we are emulating a GICv3, we do it in an non-GICv2-compatible
	 * way, so we force SRE to 1 to demonstrate this to the guest.
	 * Also, we don't support any form of IRQ/FIQ bypass.
	 * This goes with the spec allowing the value to be RAO/WI.
	 */
	if (vcpu->kvm->arch.vgic.vgic_model == KVM_DEV_TYPE_ARM_VGIC_V3) {
		vgic_v3->vgic_sre = (ICC_SRE_EL1_DIB |
				     ICC_SRE_EL1_DFB |
				     ICC_SRE_EL1_SRE);
		vcpu->arch.vgic_cpu.pendbaser = INITIAL_PENDBASER_VALUE;
	} else {
		vgic_v3->vgic_sre = 0;
	}

	vcpu->arch.vgic_cpu.num_id_bits = FIELD_GET(ICH_VTR_EL2_IDbits,
						    kvm_vgic_global_state.ich_vtr_el2);
	vcpu->arch.vgic_cpu.num_pri_bits = FIELD_GET(ICH_VTR_EL2_PRIbits,
						     kvm_vgic_global_state.ich_vtr_el2) + 1;

	/* Get the show on the road... */
	vgic_v3->vgic_hcr = ICH_HCR_EL2_En;
}

void vcpu_set_ich_hcr(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *vgic_v3 = &vcpu->arch.vgic_cpu.vgic_v3;

	/* Hide GICv3 sysreg if necessary */
	if (!kvm_has_gicv3(vcpu->kvm)) {
		vgic_v3->vgic_hcr |= (ICH_HCR_EL2_TALL0 | ICH_HCR_EL2_TALL1 |
				      ICH_HCR_EL2_TC);
		return;
	}

	if (group0_trap)
		vgic_v3->vgic_hcr |= ICH_HCR_EL2_TALL0;
	if (group1_trap)
		vgic_v3->vgic_hcr |= ICH_HCR_EL2_TALL1;
	if (common_trap)
		vgic_v3->vgic_hcr |= ICH_HCR_EL2_TC;
	if (dir_trap)
		vgic_v3->vgic_hcr |= ICH_HCR_EL2_TDIR;
}

int vgic_v3_lpi_sync_pending_status(struct kvm *kvm, struct vgic_irq *irq)
{
	struct kvm_vcpu *vcpu;
	int byte_offset, bit_nr;
	gpa_t pendbase, ptr;
	bool status;
	u8 val;
	int ret;
	unsigned long flags;

retry:
	vcpu = irq->target_vcpu;
	if (!vcpu)
		return 0;

	pendbase = GICR_PENDBASER_ADDRESS(vcpu->arch.vgic_cpu.pendbaser);

	byte_offset = irq->intid / BITS_PER_BYTE;
	bit_nr = irq->intid % BITS_PER_BYTE;
	ptr = pendbase + byte_offset;

	ret = kvm_read_guest_lock(kvm, ptr, &val, 1);
	if (ret)
		return ret;

	status = val & (1 << bit_nr);

	raw_spin_lock_irqsave(&irq->irq_lock, flags);
	if (irq->target_vcpu != vcpu) {
		raw_spin_unlock_irqrestore(&irq->irq_lock, flags);
		goto retry;
	}
	irq->pending_latch = status;
	vgic_queue_irq_unlock(vcpu->kvm, irq, flags);

	if (status) {
		/* clear consumed data */
		val &= ~(1 << bit_nr);
		ret = vgic_write_guest_lock(kvm, ptr, &val, 1);
		if (ret)
			return ret;
	}
	return 0;
}

/*
 * The deactivation of the doorbell interrupt will trigger the
 * unmapping of the associated vPE.
 */
static void unmap_all_vpes(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	int i;

	for (i = 0; i < dist->its_vm.nr_vpes; i++)
		free_irq(dist->its_vm.vpes[i]->irq, kvm_get_vcpu(kvm, i));
}

static void map_all_vpes(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	int i;

	for (i = 0; i < dist->its_vm.nr_vpes; i++)
		WARN_ON(vgic_v4_request_vpe_irq(kvm_get_vcpu(kvm, i),
						dist->its_vm.vpes[i]->irq));
}

/*
 * vgic_v3_save_pending_tables - Save the pending tables into guest RAM
 * kvm lock and all vcpu lock must be held
 */
int vgic_v3_save_pending_tables(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	struct vgic_irq *irq;
	gpa_t last_ptr = ~(gpa_t)0;
	bool vlpi_avail = false;
	unsigned long index;
	int ret = 0;
	u8 val;

	if (unlikely(!vgic_initialized(kvm)))
		return -ENXIO;

	/*
	 * A preparation for getting any VLPI states.
	 * The above vgic initialized check also ensures that the allocation
	 * and enabling of the doorbells have already been done.
	 */
	if (kvm_vgic_global_state.has_gicv4_1) {
		unmap_all_vpes(kvm);
		vlpi_avail = true;
	}

	xa_for_each(&dist->lpi_xa, index, irq) {
		int byte_offset, bit_nr;
		struct kvm_vcpu *vcpu;
		gpa_t pendbase, ptr;
		bool is_pending;
		bool stored;

		vcpu = irq->target_vcpu;
		if (!vcpu)
			continue;

		pendbase = GICR_PENDBASER_ADDRESS(vcpu->arch.vgic_cpu.pendbaser);

		byte_offset = irq->intid / BITS_PER_BYTE;
		bit_nr = irq->intid % BITS_PER_BYTE;
		ptr = pendbase + byte_offset;

		if (ptr != last_ptr) {
			ret = kvm_read_guest_lock(kvm, ptr, &val, 1);
			if (ret)
				goto out;
			last_ptr = ptr;
		}

		stored = val & (1U << bit_nr);

		is_pending = irq->pending_latch;

		if (irq->hw && vlpi_avail)
			vgic_v4_get_vlpi_state(irq, &is_pending);

		if (stored == is_pending)
			continue;

		if (is_pending)
			val |= 1 << bit_nr;
		else
			val &= ~(1 << bit_nr);

		ret = vgic_write_guest_lock(kvm, ptr, &val, 1);
		if (ret)
			goto out;
	}

out:
	if (vlpi_avail)
		map_all_vpes(kvm);

	return ret;
}

/**
 * vgic_v3_rdist_overlap - check if a region overlaps with any
 * existing redistributor region
 *
 * @kvm: kvm handle
 * @base: base of the region
 * @size: size of region
 *
 * Return: true if there is an overlap
 */
bool vgic_v3_rdist_overlap(struct kvm *kvm, gpa_t base, size_t size)
{
	struct vgic_dist *d = &kvm->arch.vgic;
	struct vgic_redist_region *rdreg;

	list_for_each_entry(rdreg, &d->rd_regions, list) {
		if ((base + size > rdreg->base) &&
			(base < rdreg->base + vgic_v3_rd_region_size(kvm, rdreg)))
			return true;
	}
	return false;
}

/*
 * Check for overlapping regions and for regions crossing the end of memory
 * for base addresses which have already been set.
 */
bool vgic_v3_check_base(struct kvm *kvm)
{
	struct vgic_dist *d = &kvm->arch.vgic;
	struct vgic_redist_region *rdreg;

	if (!IS_VGIC_ADDR_UNDEF(d->vgic_dist_base) &&
	    d->vgic_dist_base + KVM_VGIC_V3_DIST_SIZE < d->vgic_dist_base)
		return false;

	list_for_each_entry(rdreg, &d->rd_regions, list) {
		size_t sz = vgic_v3_rd_region_size(kvm, rdreg);

		if (vgic_check_iorange(kvm, VGIC_ADDR_UNDEF,
				       rdreg->base, SZ_64K, sz))
			return false;
	}

	if (IS_VGIC_ADDR_UNDEF(d->vgic_dist_base))
		return true;

	return !vgic_v3_rdist_overlap(kvm, d->vgic_dist_base,
				      KVM_VGIC_V3_DIST_SIZE);
}

/**
 * vgic_v3_rdist_free_slot - Look up registered rdist regions and identify one
 * which has free space to put a new rdist region.
 *
 * @rd_regions: redistributor region list head
 *
 * A redistributor regions maps n redistributors, n = region size / (2 x 64kB).
 * Stride between redistributors is 0 and regions are filled in the index order.
 *
 * Return: the redist region handle, if any, that has space to map a new rdist
 * region.
 */
struct vgic_redist_region *vgic_v3_rdist_free_slot(struct list_head *rd_regions)
{
	struct vgic_redist_region *rdreg;

	list_for_each_entry(rdreg, rd_regions, list) {
		if (!vgic_v3_redist_region_full(rdreg))
			return rdreg;
	}
	return NULL;
}

struct vgic_redist_region *vgic_v3_rdist_region_from_index(struct kvm *kvm,
							   u32 index)
{
	struct list_head *rd_regions = &kvm->arch.vgic.rd_regions;
	struct vgic_redist_region *rdreg;

	list_for_each_entry(rdreg, rd_regions, list) {
		if (rdreg->index == index)
			return rdreg;
	}
	return NULL;
}


int vgic_v3_map_resources(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	struct kvm_vcpu *vcpu;
	unsigned long c;

	kvm_for_each_vcpu(c, vcpu, kvm) {
		struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;

		if (IS_VGIC_ADDR_UNDEF(vgic_cpu->rd_iodev.base_addr)) {
			kvm_debug("vcpu %ld redistributor base not set\n", c);
			return -ENXIO;
		}
	}

	if (IS_VGIC_ADDR_UNDEF(dist->vgic_dist_base)) {
		kvm_debug("Need to set vgic distributor addresses first\n");
		return -ENXIO;
	}

	if (!vgic_v3_check_base(kvm)) {
		kvm_debug("VGIC redist and dist frames overlap\n");
		return -EINVAL;
	}

	/*
	 * For a VGICv3 we require the userland to explicitly initialize
	 * the VGIC before we need to use it.
	 */
	if (!vgic_initialized(kvm)) {
		return -EBUSY;
	}

	if (kvm_vgic_global_state.has_gicv4_1)
		vgic_v4_configure_vsgis(kvm);

	return 0;
}

DEFINE_STATIC_KEY_FALSE(vgic_v3_cpuif_trap);

static int __init early_group0_trap_cfg(char *buf)
{
	return kstrtobool(buf, &group0_trap);
}
early_param("kvm-arm.vgic_v3_group0_trap", early_group0_trap_cfg);

static int __init early_group1_trap_cfg(char *buf)
{
	return kstrtobool(buf, &group1_trap);
}
early_param("kvm-arm.vgic_v3_group1_trap", early_group1_trap_cfg);

static int __init early_common_trap_cfg(char *buf)
{
	return kstrtobool(buf, &common_trap);
}
early_param("kvm-arm.vgic_v3_common_trap", early_common_trap_cfg);

static int __init early_gicv4_enable(char *buf)
{
	return kstrtobool(buf, &gicv4_enable);
}
early_param("kvm-arm.vgic_v4_enable", early_gicv4_enable);

static const struct midr_range broken_seis[] = {
	MIDR_ALL_VERSIONS(MIDR_APPLE_M1_ICESTORM),
	MIDR_ALL_VERSIONS(MIDR_APPLE_M1_FIRESTORM),
	MIDR_ALL_VERSIONS(MIDR_APPLE_M1_ICESTORM_PRO),
	MIDR_ALL_VERSIONS(MIDR_APPLE_M1_FIRESTORM_PRO),
	MIDR_ALL_VERSIONS(MIDR_APPLE_M1_ICESTORM_MAX),
	MIDR_ALL_VERSIONS(MIDR_APPLE_M1_FIRESTORM_MAX),
	MIDR_ALL_VERSIONS(MIDR_APPLE_M2_BLIZZARD),
	MIDR_ALL_VERSIONS(MIDR_APPLE_M2_AVALANCHE),
	MIDR_ALL_VERSIONS(MIDR_APPLE_M2_BLIZZARD_PRO),
	MIDR_ALL_VERSIONS(MIDR_APPLE_M2_AVALANCHE_PRO),
	MIDR_ALL_VERSIONS(MIDR_APPLE_M2_BLIZZARD_MAX),
	MIDR_ALL_VERSIONS(MIDR_APPLE_M2_AVALANCHE_MAX),
	{},
};

static bool vgic_v3_broken_seis(void)
{
	return ((kvm_vgic_global_state.ich_vtr_el2 & ICH_VTR_EL2_SEIS) &&
		is_midr_in_range_list(broken_seis));
}

/**
 * vgic_v3_probe - probe for a VGICv3 compatible interrupt controller
 * @info:	pointer to the GIC description
 *
 * Returns 0 if the VGICv3 has been probed successfully, returns an error code
 * otherwise
 */
int vgic_v3_probe(const struct gic_kvm_info *info)
{
	u64 ich_vtr_el2 = kvm_call_hyp_ret(__vgic_v3_get_gic_config);
	bool has_v2;
	int ret;

	has_v2 = ich_vtr_el2 >> 63;
	ich_vtr_el2 = (u32)ich_vtr_el2;

	/*
	 * The ListRegs field is 5 bits, but there is an architectural
	 * maximum of 16 list registers. Just ignore bit 4...
	 */
	kvm_vgic_global_state.nr_lr = (ich_vtr_el2 & 0xf) + 1;
	kvm_vgic_global_state.can_emulate_gicv2 = false;
	kvm_vgic_global_state.ich_vtr_el2 = ich_vtr_el2;

	/* GICv4 support? */
	if (info->has_v4) {
		kvm_vgic_global_state.has_gicv4 = gicv4_enable;
		kvm_vgic_global_state.has_gicv4_1 = info->has_v4_1 && gicv4_enable;
		kvm_info("GICv4%s support %s\n",
			 kvm_vgic_global_state.has_gicv4_1 ? ".1" : "",
			 str_enabled_disabled(gicv4_enable));
	}

	kvm_vgic_global_state.vcpu_base = 0;

	if (!info->vcpu.start) {
		kvm_info("GICv3: no GICV resource entry\n");
	} else if (!has_v2) {
		pr_warn(FW_BUG "CPU interface incapable of MMIO access\n");
	} else if (!PAGE_ALIGNED(info->vcpu.start)) {
		pr_warn("GICV physical address 0x%llx not page aligned\n",
			(unsigned long long)info->vcpu.start);
	} else if (kvm_get_mode() != KVM_MODE_PROTECTED) {
		kvm_vgic_global_state.vcpu_base = info->vcpu.start;
		kvm_vgic_global_state.can_emulate_gicv2 = true;
		ret = kvm_register_vgic_device(KVM_DEV_TYPE_ARM_VGIC_V2);
		if (ret) {
			kvm_err("Cannot register GICv2 KVM device.\n");
			return ret;
		}
		kvm_info("vgic-v2@%llx\n", info->vcpu.start);
	}
	ret = kvm_register_vgic_device(KVM_DEV_TYPE_ARM_VGIC_V3);
	if (ret) {
		kvm_err("Cannot register GICv3 KVM device.\n");
		kvm_unregister_device_ops(KVM_DEV_TYPE_ARM_VGIC_V2);
		return ret;
	}

	if (kvm_vgic_global_state.vcpu_base == 0)
		kvm_info("disabling GICv2 emulation\n");

	if (cpus_have_final_cap(ARM64_WORKAROUND_CAVIUM_30115)) {
		group0_trap = true;
		group1_trap = true;
	}

	if (vgic_v3_broken_seis()) {
		kvm_info("GICv3 with broken locally generated SEI\n");

		kvm_vgic_global_state.ich_vtr_el2 &= ~ICH_VTR_EL2_SEIS;
		group0_trap = true;
		group1_trap = true;
		if (ich_vtr_el2 & ICH_VTR_EL2_TDS)
			dir_trap = true;
		else
			common_trap = true;
	}

	if (group0_trap || group1_trap || common_trap | dir_trap) {
		kvm_info("GICv3 sysreg trapping enabled ([%s%s%s%s], reduced performance)\n",
			 group0_trap ? "G0" : "",
			 group1_trap ? "G1" : "",
			 common_trap ? "C"  : "",
			 dir_trap    ? "D"  : "");
		static_branch_enable(&vgic_v3_cpuif_trap);
	}

	kvm_vgic_global_state.vctrl_base = NULL;
	kvm_vgic_global_state.type = VGIC_V3;
	kvm_vgic_global_state.max_gic_vcpus = VGIC_V3_MAX_CPUS;

	return 0;
}

void vgic_v3_load(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v3;

	/* If the vgic is nested, perform the full state loading */
	if (vgic_state_is_nested(vcpu)) {
		vgic_v3_load_nested(vcpu);
		return;
	}

	if (likely(!is_protected_kvm_enabled()))
		kvm_call_hyp(__vgic_v3_restore_vmcr_aprs, cpu_if);

	if (has_vhe())
		__vgic_v3_activate_traps(cpu_if);

	WARN_ON(vgic_v4_load(vcpu));
}

void vgic_v3_put(struct kvm_vcpu *vcpu)
{
	struct vgic_v3_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v3;

	if (vgic_state_is_nested(vcpu)) {
		vgic_v3_put_nested(vcpu);
		return;
	}

	if (likely(!is_protected_kvm_enabled()))
		kvm_call_hyp(__vgic_v3_save_vmcr_aprs, cpu_if);
	WARN_ON(vgic_v4_put(vcpu));

	if (has_vhe())
		__vgic_v3_deactivate_traps(cpu_if);
}
