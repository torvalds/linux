// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015, 2016 ARM Ltd.
 */

#include <linux/irqchip/arm-gic.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <kvm/arm_vgic.h>
#include <asm/kvm_mmu.h>

#include "vgic-mmio.h"
#include "vgic.h"

static inline void vgic_v2_write_lr(int lr, u32 val)
{
	void __iomem *base = kvm_vgic_global_state.vctrl_base;

	writel_relaxed(val, base + GICH_LR0 + (lr * 4));
}

void vgic_v2_init_lrs(void)
{
	int i;

	for (i = 0; i < kvm_vgic_global_state.nr_lr; i++)
		vgic_v2_write_lr(i, 0);
}

void vgic_v2_configure_hcr(struct kvm_vcpu *vcpu,
			   struct ap_list_summary *als)
{
	struct vgic_v2_cpu_if *cpuif = &vcpu->arch.vgic_cpu.vgic_v2;

	cpuif->vgic_hcr = GICH_HCR_EN;

	if (irqs_pending_outside_lrs(als))
		cpuif->vgic_hcr |= GICH_HCR_NPIE;
	if (irqs_active_outside_lrs(als))
		cpuif->vgic_hcr |= GICH_HCR_LRENPIE;
	if (irqs_outside_lrs(als))
		cpuif->vgic_hcr |= GICH_HCR_UIE;

	cpuif->vgic_hcr |= (cpuif->vgic_vmcr & GICH_VMCR_ENABLE_GRP0_MASK) ?
		GICH_HCR_VGrp0DIE : GICH_HCR_VGrp0EIE;
	cpuif->vgic_hcr |= (cpuif->vgic_vmcr & GICH_VMCR_ENABLE_GRP1_MASK) ?
		GICH_HCR_VGrp1DIE : GICH_HCR_VGrp1EIE;
}

static bool lr_signals_eoi_mi(u32 lr_val)
{
	return !(lr_val & GICH_LR_STATE) && (lr_val & GICH_LR_EOI) &&
	       !(lr_val & GICH_LR_HW);
}

static void vgic_v2_fold_lr(struct kvm_vcpu *vcpu, u32 val)
{
	u32 cpuid, intid = val & GICH_LR_VIRTUALID;
	struct vgic_irq *irq;
	bool deactivated;

	/* Extract the source vCPU id from the LR */
	cpuid = FIELD_GET(GICH_LR_PHYSID_CPUID, val) & 7;

	/* Notify fds when the guest EOI'ed a level-triggered SPI */
	if (lr_signals_eoi_mi(val) && vgic_valid_spi(vcpu->kvm, intid))
		kvm_notify_acked_irq(vcpu->kvm, 0,
				     intid - VGIC_NR_PRIVATE_IRQS);

	irq = vgic_get_vcpu_irq(vcpu, intid);

	scoped_guard(raw_spinlock, &irq->irq_lock) {
		/* Always preserve the active bit, note deactivation */
		deactivated = irq->active && !(val & GICH_LR_ACTIVE_BIT);
		irq->active = !!(val & GICH_LR_ACTIVE_BIT);

		if (irq->active && vgic_irq_is_sgi(intid))
			irq->active_source = cpuid;

		/* Edge is the only case where we preserve the pending bit */
		if (irq->config == VGIC_CONFIG_EDGE &&
		    (val & GICH_LR_PENDING_BIT)) {
			irq->pending_latch = true;

			if (vgic_irq_is_sgi(intid))
				irq->source |= (1 << cpuid);
		}

		/*
		 * Clear soft pending state when level irqs have been acked.
		 */
		if (irq->config == VGIC_CONFIG_LEVEL && !(val & GICH_LR_STATE))
			irq->pending_latch = false;

		/* Handle resampling for mapped interrupts if required */
		vgic_irq_handle_resampling(irq, deactivated, val & GICH_LR_PENDING_BIT);

		irq->on_lr = false;
	}

	vgic_put_irq(vcpu->kvm, irq);
}

static u32 vgic_v2_compute_lr(struct kvm_vcpu *vcpu, struct vgic_irq *irq);

/*
 * transfer the content of the LRs back into the corresponding ap_list:
 * - active bit is transferred as is
 * - pending bit is
 *   - transferred as is in case of edge sensitive IRQs
 *   - set to the line-level (resample time) for level sensitive IRQs
 */
void vgic_v2_fold_lr_state(struct kvm_vcpu *vcpu)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;
	struct vgic_v2_cpu_if *cpuif = &vgic_cpu->vgic_v2;
	u32 eoicount = FIELD_GET(GICH_HCR_EOICOUNT, cpuif->vgic_hcr);
	struct vgic_irq *irq;

	DEBUG_SPINLOCK_BUG_ON(!irqs_disabled());

	for (int lr = 0; lr < vgic_cpu->vgic_v2.used_lrs; lr++)
		vgic_v2_fold_lr(vcpu, cpuif->vgic_lr[lr]);

	/* See the GICv3 equivalent for the EOIcount handling rationale */
	list_for_each_entry(irq, &vgic_cpu->ap_list_head, ap_list) {
		u32 lr;

		if (!eoicount) {
			break;
		} else {
			guard(raw_spinlock)(&irq->irq_lock);

			if (!(likely(vgic_target_oracle(irq) == vcpu) &&
			      irq->active))
				continue;

			lr = vgic_v2_compute_lr(vcpu, irq) & ~GICH_LR_ACTIVE_BIT;
		}

		if (lr & GICH_LR_HW)
			writel_relaxed(FIELD_GET(GICH_LR_PHYSID_CPUID, lr),
				       kvm_vgic_global_state.gicc_base + GIC_CPU_DEACTIVATE);
		vgic_v2_fold_lr(vcpu, lr);
		eoicount--;
	}

	cpuif->used_lrs = 0;
}

void vgic_v2_deactivate(struct kvm_vcpu *vcpu, u32 val)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;
	struct vgic_v2_cpu_if *cpuif = &vgic_cpu->vgic_v2;
	struct kvm_vcpu *target_vcpu = NULL;
	bool mmio = false;
	struct vgic_irq *irq;
	unsigned long flags;
	u64 lr = 0;
	u8 cpuid;

	/* Snapshot CPUID, and remove it from the INTID */
	cpuid = FIELD_GET(GENMASK_ULL(12, 10), val);
	val &= ~GENMASK_ULL(12, 10);

	/* We only deal with DIR when EOIMode==1 */
	if (!(cpuif->vgic_vmcr & GICH_VMCR_EOI_MODE_MASK))
		return;

	/* Make sure we're in the same context as LR handling */
	local_irq_save(flags);

	irq = vgic_get_vcpu_irq(vcpu, val);
	if (WARN_ON_ONCE(!irq))
		goto out;

	/* See the corresponding v3 code for the rationale */
	scoped_guard(raw_spinlock, &irq->irq_lock) {
		target_vcpu = irq->vcpu;

		/* Not on any ap_list? */
		if (!target_vcpu)
			goto put;

		/*
		 * Urgh. We're deactivating something that we cannot
		 * observe yet... Big hammer time.
		 */
		if (irq->on_lr) {
			mmio = true;
			goto put;
		}

		/* SGI: check that the cpuid matches */
		if (val < VGIC_NR_SGIS && irq->active_source != cpuid) {
			target_vcpu = NULL;
			goto put;
		}

		/* (with a Dalek voice) DEACTIVATE!!!! */
		lr = vgic_v2_compute_lr(vcpu, irq) & ~GICH_LR_ACTIVE_BIT;
	}

	if (lr & GICH_LR_HW)
		writel_relaxed(FIELD_GET(GICH_LR_PHYSID_CPUID, lr),
			       kvm_vgic_global_state.gicc_base + GIC_CPU_DEACTIVATE);

	vgic_v2_fold_lr(vcpu, lr);

put:
	vgic_put_irq(vcpu->kvm, irq);

out:
	local_irq_restore(flags);

	if (mmio)
		vgic_mmio_write_cactive(vcpu, (val / 32) * 4, 4, BIT(val % 32));

	/* Force the ap_list to be pruned */
	if (target_vcpu)
		kvm_make_request(KVM_REQ_VGIC_PROCESS_UPDATE, target_vcpu);
}

static u32 vgic_v2_compute_lr(struct kvm_vcpu *vcpu, struct vgic_irq *irq)
{
	u32 val = irq->intid;
	bool allow_pending = true;

	WARN_ON(irq->on_lr);

	if (irq->active) {
		val |= GICH_LR_ACTIVE_BIT;
		if (vgic_irq_is_sgi(irq->intid))
			val |= irq->active_source << GICH_LR_PHYSID_CPUID_SHIFT;
		if (vgic_irq_is_multi_sgi(irq)) {
			allow_pending = false;
			val |= GICH_LR_EOI;
		}
	}

	if (irq->group)
		val |= GICH_LR_GROUP1;

	if (irq->hw && !vgic_irq_needs_resampling(irq)) {
		val |= GICH_LR_HW;
		val |= irq->hwintid << GICH_LR_PHYSID_CPUID_SHIFT;
		/*
		 * Never set pending+active on a HW interrupt, as the
		 * pending state is kept at the physical distributor
		 * level.
		 */
		if (irq->active)
			allow_pending = false;
	} else {
		if (irq->config == VGIC_CONFIG_LEVEL) {
			val |= GICH_LR_EOI;

			/*
			 * Software resampling doesn't work very well
			 * if we allow P+A, so let's not do that.
			 */
			if (irq->active)
				allow_pending = false;
		}
	}

	if (allow_pending && irq_is_pending(irq)) {
		val |= GICH_LR_PENDING_BIT;

		if (vgic_irq_is_sgi(irq->intid)) {
			u32 src = ffs(irq->source);

			if (WARN_RATELIMIT(!src, "No SGI source for INTID %d\n",
					   irq->intid))
				return 0;

			val |= (src - 1) << GICH_LR_PHYSID_CPUID_SHIFT;
			if (irq->source & ~BIT(src - 1))
				val |= GICH_LR_EOI;
		}
	}

	/* The GICv2 LR only holds five bits of priority. */
	val |= (irq->priority >> 3) << GICH_LR_PRIORITY_SHIFT;

	return val;
}

/*
 * Populates the particular LR with the state of a given IRQ:
 * - for an edge sensitive IRQ the pending state is cleared in struct vgic_irq
 * - for a level sensitive IRQ the pending state value is unchanged;
 *   it is dictated directly by the input level
 *
 * If @irq describes an SGI with multiple sources, we choose the
 * lowest-numbered source VCPU and clear that bit in the source bitmap.
 *
 * The irq_lock must be held by the caller.
 */
void vgic_v2_populate_lr(struct kvm_vcpu *vcpu, struct vgic_irq *irq, int lr)
{
	u32 val = vgic_v2_compute_lr(vcpu, irq);

	vcpu->arch.vgic_cpu.vgic_v2.vgic_lr[lr] = val;

	if (val & GICH_LR_PENDING_BIT) {
		if (irq->config == VGIC_CONFIG_EDGE)
			irq->pending_latch = false;

		if (vgic_irq_is_sgi(irq->intid)) {
			u32 src = ffs(irq->source);

			irq->source &= ~BIT(src - 1);
			if (irq->source)
				irq->pending_latch = true;
		}
	}

	/*
	 * Level-triggered mapped IRQs are special because we only observe
	 * rising edges as input to the VGIC.  We therefore lower the line
	 * level here, so that we can take new virtual IRQs.  See
	 * vgic_v2_fold_lr_state for more info.
	 */
	if (vgic_irq_is_mapped_level(irq) && (val & GICH_LR_PENDING_BIT))
		irq->line_level = false;

	/* The GICv2 LR only holds five bits of priority. */
	val |= (irq->priority >> 3) << GICH_LR_PRIORITY_SHIFT;

	irq->on_lr = true;
}

void vgic_v2_clear_lr(struct kvm_vcpu *vcpu, int lr)
{
	vcpu->arch.vgic_cpu.vgic_v2.vgic_lr[lr] = 0;
}

void vgic_v2_set_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcrp)
{
	struct vgic_v2_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v2;
	u32 vmcr;

	vmcr = (vmcrp->grpen0 << GICH_VMCR_ENABLE_GRP0_SHIFT) &
		GICH_VMCR_ENABLE_GRP0_MASK;
	vmcr |= (vmcrp->grpen1 << GICH_VMCR_ENABLE_GRP1_SHIFT) &
		GICH_VMCR_ENABLE_GRP1_MASK;
	vmcr |= (vmcrp->ackctl << GICH_VMCR_ACK_CTL_SHIFT) &
		GICH_VMCR_ACK_CTL_MASK;
	vmcr |= (vmcrp->fiqen << GICH_VMCR_FIQ_EN_SHIFT) &
		GICH_VMCR_FIQ_EN_MASK;
	vmcr |= (vmcrp->cbpr << GICH_VMCR_CBPR_SHIFT) &
		GICH_VMCR_CBPR_MASK;
	vmcr |= (vmcrp->eoim << GICH_VMCR_EOI_MODE_SHIFT) &
		GICH_VMCR_EOI_MODE_MASK;
	vmcr |= (vmcrp->abpr << GICH_VMCR_ALIAS_BINPOINT_SHIFT) &
		GICH_VMCR_ALIAS_BINPOINT_MASK;
	vmcr |= (vmcrp->bpr << GICH_VMCR_BINPOINT_SHIFT) &
		GICH_VMCR_BINPOINT_MASK;
	vmcr |= ((vmcrp->pmr >> GICV_PMR_PRIORITY_SHIFT) <<
		 GICH_VMCR_PRIMASK_SHIFT) & GICH_VMCR_PRIMASK_MASK;

	cpu_if->vgic_vmcr = vmcr;
}

void vgic_v2_get_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcrp)
{
	struct vgic_v2_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v2;
	u32 vmcr;

	vmcr = cpu_if->vgic_vmcr;

	vmcrp->grpen0 = (vmcr & GICH_VMCR_ENABLE_GRP0_MASK) >>
		GICH_VMCR_ENABLE_GRP0_SHIFT;
	vmcrp->grpen1 = (vmcr & GICH_VMCR_ENABLE_GRP1_MASK) >>
		GICH_VMCR_ENABLE_GRP1_SHIFT;
	vmcrp->ackctl = (vmcr & GICH_VMCR_ACK_CTL_MASK) >>
		GICH_VMCR_ACK_CTL_SHIFT;
	vmcrp->fiqen = (vmcr & GICH_VMCR_FIQ_EN_MASK) >>
		GICH_VMCR_FIQ_EN_SHIFT;
	vmcrp->cbpr = (vmcr & GICH_VMCR_CBPR_MASK) >>
		GICH_VMCR_CBPR_SHIFT;
	vmcrp->eoim = (vmcr & GICH_VMCR_EOI_MODE_MASK) >>
		GICH_VMCR_EOI_MODE_SHIFT;

	vmcrp->abpr = (vmcr & GICH_VMCR_ALIAS_BINPOINT_MASK) >>
			GICH_VMCR_ALIAS_BINPOINT_SHIFT;
	vmcrp->bpr  = (vmcr & GICH_VMCR_BINPOINT_MASK) >>
			GICH_VMCR_BINPOINT_SHIFT;
	vmcrp->pmr  = ((vmcr & GICH_VMCR_PRIMASK_MASK) >>
			GICH_VMCR_PRIMASK_SHIFT) << GICV_PMR_PRIORITY_SHIFT;
}

void vgic_v2_reset(struct kvm_vcpu *vcpu)
{
	/*
	 * By forcing VMCR to zero, the GIC will restore the binary
	 * points to their reset values. Anything else resets to zero
	 * anyway.
	 */
	vcpu->arch.vgic_cpu.vgic_v2.vgic_vmcr = 0;
}

/* check for overlapping regions and for regions crossing the end of memory */
static bool vgic_v2_check_base(gpa_t dist_base, gpa_t cpu_base)
{
	if (dist_base + KVM_VGIC_V2_DIST_SIZE < dist_base)
		return false;
	if (cpu_base + KVM_VGIC_V2_CPU_SIZE < cpu_base)
		return false;

	if (dist_base + KVM_VGIC_V2_DIST_SIZE <= cpu_base)
		return true;
	if (cpu_base + KVM_VGIC_V2_CPU_SIZE <= dist_base)
		return true;

	return false;
}

int vgic_v2_map_resources(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	unsigned int len;
	int ret = 0;

	if (IS_VGIC_ADDR_UNDEF(dist->vgic_dist_base) ||
	    IS_VGIC_ADDR_UNDEF(dist->vgic_cpu_base)) {
		kvm_debug("Need to set vgic cpu and dist addresses first\n");
		return -ENXIO;
	}

	if (!vgic_v2_check_base(dist->vgic_dist_base, dist->vgic_cpu_base)) {
		kvm_debug("VGIC CPU and dist frames overlap\n");
		return -EINVAL;
	}

	/*
	 * Initialize the vgic if this hasn't already been done on demand by
	 * accessing the vgic state from userspace.
	 */
	ret = vgic_init(kvm);
	if (ret) {
		kvm_err("Unable to initialize VGIC dynamic data structures\n");
		return ret;
	}

	len = vgic_v2_init_cpuif_iodev(&dist->cpuif_iodev);
	dist->cpuif_iodev.base_addr = dist->vgic_cpu_base;
	dist->cpuif_iodev.iodev_type = IODEV_CPUIF;
	dist->cpuif_iodev.redist_vcpu = NULL;

	ret = kvm_io_bus_register_dev(kvm, KVM_MMIO_BUS, dist->vgic_cpu_base,
				      len, &dist->cpuif_iodev.dev);
	if (ret)
		return ret;

	if (!static_branch_unlikely(&vgic_v2_cpuif_trap)) {
		ret = kvm_phys_addr_ioremap(kvm, dist->vgic_cpu_base,
					    kvm_vgic_global_state.vcpu_base,
					    KVM_VGIC_V2_CPU_SIZE - SZ_4K, true);
		if (ret) {
			kvm_err("Unable to remap VGIC CPU to VCPU\n");
			return ret;
		}
	}

	return 0;
}

DEFINE_STATIC_KEY_FALSE(vgic_v2_cpuif_trap);

/**
 * vgic_v2_probe - probe for a VGICv2 compatible interrupt controller
 * @info:	pointer to the GIC description
 *
 * Returns 0 if the VGICv2 has been probed successfully, returns an error code
 * otherwise
 */
int vgic_v2_probe(const struct gic_kvm_info *info)
{
	int ret;
	u32 vtr;

	if (is_protected_kvm_enabled()) {
		kvm_err("GICv2 not supported in protected mode\n");
		return -ENXIO;
	}

	if (!info->vctrl.start) {
		kvm_err("GICH not present in the firmware table\n");
		return -ENXIO;
	}

	if (!PAGE_ALIGNED(info->vcpu.start) ||
	    !PAGE_ALIGNED(resource_size(&info->vcpu))) {
		kvm_info("GICV region size/alignment is unsafe, using trapping (reduced performance)\n");

		ret = create_hyp_io_mappings(info->vcpu.start,
					     resource_size(&info->vcpu),
					     &kvm_vgic_global_state.vcpu_base_va,
					     &kvm_vgic_global_state.vcpu_hyp_va);
		if (ret) {
			kvm_err("Cannot map GICV into hyp\n");
			goto out;
		}

		static_branch_enable(&vgic_v2_cpuif_trap);
	}

	ret = create_hyp_io_mappings(info->vctrl.start,
				     resource_size(&info->vctrl),
				     &kvm_vgic_global_state.vctrl_base,
				     &kvm_vgic_global_state.vctrl_hyp);
	if (ret) {
		kvm_err("Cannot map VCTRL into hyp\n");
		goto out;
	}

	vtr = readl_relaxed(kvm_vgic_global_state.vctrl_base + GICH_VTR);
	kvm_vgic_global_state.nr_lr = (vtr & 0x3f) + 1;

	ret = kvm_register_vgic_device(KVM_DEV_TYPE_ARM_VGIC_V2);
	if (ret) {
		kvm_err("Cannot register GICv2 KVM device\n");
		goto out;
	}

	kvm_vgic_global_state.can_emulate_gicv2 = true;
	kvm_vgic_global_state.vcpu_base = info->vcpu.start;
	kvm_vgic_global_state.gicc_base = info->gicc_base;
	kvm_vgic_global_state.type = VGIC_V2;
	kvm_vgic_global_state.max_gic_vcpus = VGIC_V2_MAX_CPUS;

	kvm_debug("vgic-v2@%llx\n", info->vctrl.start);

	return 0;
out:
	if (kvm_vgic_global_state.vctrl_base)
		iounmap(kvm_vgic_global_state.vctrl_base);
	if (kvm_vgic_global_state.vcpu_base_va)
		iounmap(kvm_vgic_global_state.vcpu_base_va);

	return ret;
}

static void save_lrs(struct kvm_vcpu *vcpu, void __iomem *base)
{
	struct vgic_v2_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v2;
	u64 used_lrs = cpu_if->used_lrs;
	u64 elrsr;
	int i;

	elrsr = readl_relaxed(base + GICH_ELRSR0);
	if (unlikely(used_lrs > 32))
		elrsr |= ((u64)readl_relaxed(base + GICH_ELRSR1)) << 32;

	for (i = 0; i < used_lrs; i++) {
		if (elrsr & (1UL << i))
			cpu_if->vgic_lr[i] &= ~GICH_LR_STATE;
		else
			cpu_if->vgic_lr[i] = readl_relaxed(base + GICH_LR0 + (i * 4));

		writel_relaxed(0, base + GICH_LR0 + (i * 4));
	}
}

void vgic_v2_save_state(struct kvm_vcpu *vcpu)
{
	struct vgic_v2_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v2;
	void __iomem *base = kvm_vgic_global_state.vctrl_base;
	u64 used_lrs = vcpu->arch.vgic_cpu.vgic_v2.used_lrs;

	if (!base)
		return;

	cpu_if->vgic_vmcr = readl_relaxed(kvm_vgic_global_state.vctrl_base + GICH_VMCR);

	if (used_lrs)
		save_lrs(vcpu, base);

	if (cpu_if->vgic_hcr & GICH_HCR_LRENPIE) {
		u32 val = readl_relaxed(base + GICH_HCR);

		cpu_if->vgic_hcr &= ~GICH_HCR_EOICOUNT;
		cpu_if->vgic_hcr |= val & GICH_HCR_EOICOUNT;
	}

	writel_relaxed(0, base + GICH_HCR);
}

void vgic_v2_restore_state(struct kvm_vcpu *vcpu)
{
	struct vgic_v2_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v2;
	void __iomem *base = kvm_vgic_global_state.vctrl_base;
	u64 used_lrs = cpu_if->used_lrs;
	int i;

	if (!base)
		return;

	writel_relaxed(cpu_if->vgic_hcr, base + GICH_HCR);

	for (i = 0; i < used_lrs; i++)
		writel_relaxed(cpu_if->vgic_lr[i], base + GICH_LR0 + (i * 4));
}

void vgic_v2_load(struct kvm_vcpu *vcpu)
{
	struct vgic_v2_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v2;

	writel_relaxed(cpu_if->vgic_vmcr,
		       kvm_vgic_global_state.vctrl_base + GICH_VMCR);
	writel_relaxed(cpu_if->vgic_apr,
		       kvm_vgic_global_state.vctrl_base + GICH_APR);
}

void vgic_v2_put(struct kvm_vcpu *vcpu)
{
	struct vgic_v2_cpu_if *cpu_if = &vcpu->arch.vgic_cpu.vgic_v2;

	cpu_if->vgic_apr = readl_relaxed(kvm_vgic_global_state.vctrl_base + GICH_APR);
}
