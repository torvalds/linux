// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015, 2016 ARM Ltd.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/list_sort.h>
#include <linux/nospec.h>

#include <asm/kvm_hyp.h>

#include "vgic.h"

#define CREATE_TRACE_POINTS
#include "trace.h"

struct vgic_global kvm_vgic_global_state __ro_after_init = {
	.gicv3_cpuif = STATIC_KEY_FALSE_INIT,
};

/*
 * Locking order is always:
 * kvm->lock (mutex)
 *   its->cmd_lock (mutex)
 *     its->its_lock (mutex)
 *       vgic_cpu->ap_list_lock		must be taken with IRQs disabled
 *         kvm->lpi_list_lock		must be taken with IRQs disabled
 *           vgic_irq->irq_lock		must be taken with IRQs disabled
 *
 * As the ap_list_lock might be taken from the timer interrupt handler,
 * we have to disable IRQs before taking this lock and everything lower
 * than it.
 *
 * If you need to take multiple locks, always take the upper lock first,
 * then the lower ones, e.g. first take the its_lock, then the irq_lock.
 * If you are already holding a lock and need to take a higher one, you
 * have to drop the lower ranking lock first and re-aquire it after having
 * taken the upper one.
 *
 * When taking more than one ap_list_lock at the same time, always take the
 * lowest numbered VCPU's ap_list_lock first, so:
 *   vcpuX->vcpu_id < vcpuY->vcpu_id:
 *     raw_spin_lock(vcpuX->arch.vgic_cpu.ap_list_lock);
 *     raw_spin_lock(vcpuY->arch.vgic_cpu.ap_list_lock);
 *
 * Since the VGIC must support injecting virtual interrupts from ISRs, we have
 * to use the raw_spin_lock_irqsave/raw_spin_unlock_irqrestore versions of outer
 * spinlocks for any lock that may be taken while injecting an interrupt.
 */

/*
 * Iterate over the VM's list of mapped LPIs to find the one with a
 * matching interrupt ID and return a reference to the IRQ structure.
 */
static struct vgic_irq *vgic_get_lpi(struct kvm *kvm, u32 intid)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	struct vgic_irq *irq = NULL;
	unsigned long flags;

	raw_spin_lock_irqsave(&dist->lpi_list_lock, flags);

	list_for_each_entry(irq, &dist->lpi_list_head, lpi_list) {
		if (irq->intid != intid)
			continue;

		/*
		 * This increases the refcount, the caller is expected to
		 * call vgic_put_irq() later once it's finished with the IRQ.
		 */
		vgic_get_irq_kref(irq);
		goto out_unlock;
	}
	irq = NULL;

out_unlock:
	raw_spin_unlock_irqrestore(&dist->lpi_list_lock, flags);

	return irq;
}

/*
 * This looks up the virtual interrupt ID to get the corresponding
 * struct vgic_irq. It also increases the refcount, so any caller is expected
 * to call vgic_put_irq() once it's finished with this IRQ.
 */
struct vgic_irq *vgic_get_irq(struct kvm *kvm, struct kvm_vcpu *vcpu,
			      u32 intid)
{
	/* SGIs and PPIs */
	if (intid <= VGIC_MAX_PRIVATE) {
		intid = array_index_nospec(intid, VGIC_MAX_PRIVATE + 1);
		return &vcpu->arch.vgic_cpu.private_irqs[intid];
	}

	/* SPIs */
	if (intid < (kvm->arch.vgic.nr_spis + VGIC_NR_PRIVATE_IRQS)) {
		intid = array_index_nospec(intid, kvm->arch.vgic.nr_spis + VGIC_NR_PRIVATE_IRQS);
		return &kvm->arch.vgic.spis[intid - VGIC_NR_PRIVATE_IRQS];
	}

	/* LPIs */
	if (intid >= VGIC_MIN_LPI)
		return vgic_get_lpi(kvm, intid);

	WARN(1, "Looking up struct vgic_irq for reserved INTID");
	return NULL;
}

/*
 * We can't do anything in here, because we lack the kvm pointer to
 * lock and remove the item from the lpi_list. So we keep this function
 * empty and use the return value of kref_put() to trigger the freeing.
 */
static void vgic_irq_release(struct kref *ref)
{
}

/*
 * Drop the refcount on the LPI. Must be called with lpi_list_lock held.
 */
void __vgic_put_lpi_locked(struct kvm *kvm, struct vgic_irq *irq)
{
	struct vgic_dist *dist = &kvm->arch.vgic;

	if (!kref_put(&irq->refcount, vgic_irq_release))
		return;

	list_del(&irq->lpi_list);
	dist->lpi_list_count--;

	kfree(irq);
}

void vgic_put_irq(struct kvm *kvm, struct vgic_irq *irq)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	unsigned long flags;

	if (irq->intid < VGIC_MIN_LPI)
		return;

	raw_spin_lock_irqsave(&dist->lpi_list_lock, flags);
	__vgic_put_lpi_locked(kvm, irq);
	raw_spin_unlock_irqrestore(&dist->lpi_list_lock, flags);
}

void vgic_flush_pending_lpis(struct kvm_vcpu *vcpu)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;
	struct vgic_irq *irq, *tmp;
	unsigned long flags;

	raw_spin_lock_irqsave(&vgic_cpu->ap_list_lock, flags);

	list_for_each_entry_safe(irq, tmp, &vgic_cpu->ap_list_head, ap_list) {
		if (irq->intid >= VGIC_MIN_LPI) {
			raw_spin_lock(&irq->irq_lock);
			list_del(&irq->ap_list);
			irq->vcpu = NULL;
			raw_spin_unlock(&irq->irq_lock);
			vgic_put_irq(vcpu->kvm, irq);
		}
	}

	raw_spin_unlock_irqrestore(&vgic_cpu->ap_list_lock, flags);
}

void vgic_irq_set_phys_pending(struct vgic_irq *irq, bool pending)
{
	WARN_ON(irq_set_irqchip_state(irq->host_irq,
				      IRQCHIP_STATE_PENDING,
				      pending));
}

bool vgic_get_phys_line_level(struct vgic_irq *irq)
{
	bool line_level;

	BUG_ON(!irq->hw);

	if (irq->get_input_level)
		return irq->get_input_level(irq->intid);

	WARN_ON(irq_get_irqchip_state(irq->host_irq,
				      IRQCHIP_STATE_PENDING,
				      &line_level));
	return line_level;
}

/* Set/Clear the physical active state */
void vgic_irq_set_phys_active(struct vgic_irq *irq, bool active)
{

	BUG_ON(!irq->hw);
	WARN_ON(irq_set_irqchip_state(irq->host_irq,
				      IRQCHIP_STATE_ACTIVE,
				      active));
}

/**
 * kvm_vgic_target_oracle - compute the target vcpu for an irq
 *
 * @irq:	The irq to route. Must be already locked.
 *
 * Based on the current state of the interrupt (enabled, pending,
 * active, vcpu and target_vcpu), compute the next vcpu this should be
 * given to. Return NULL if this shouldn't be injected at all.
 *
 * Requires the IRQ lock to be held.
 */
static struct kvm_vcpu *vgic_target_oracle(struct vgic_irq *irq)
{
	lockdep_assert_held(&irq->irq_lock);

	/* If the interrupt is active, it must stay on the current vcpu */
	if (irq->active)
		return irq->vcpu ? : irq->target_vcpu;

	/*
	 * If the IRQ is not active but enabled and pending, we should direct
	 * it to its configured target VCPU.
	 * If the distributor is disabled, pending interrupts shouldn't be
	 * forwarded.
	 */
	if (irq->enabled && irq_is_pending(irq)) {
		if (unlikely(irq->target_vcpu &&
			     !irq->target_vcpu->kvm->arch.vgic.enabled))
			return NULL;

		return irq->target_vcpu;
	}

	/* If neither active nor pending and enabled, then this IRQ should not
	 * be queued to any VCPU.
	 */
	return NULL;
}

/*
 * The order of items in the ap_lists defines how we'll pack things in LRs as
 * well, the first items in the list being the first things populated in the
 * LRs.
 *
 * A hard rule is that active interrupts can never be pushed out of the LRs
 * (and therefore take priority) since we cannot reliably trap on deactivation
 * of IRQs and therefore they have to be present in the LRs.
 *
 * Otherwise things should be sorted by the priority field and the GIC
 * hardware support will take care of preemption of priority groups etc.
 *
 * Return negative if "a" sorts before "b", 0 to preserve order, and positive
 * to sort "b" before "a".
 */
static int vgic_irq_cmp(void *priv, struct list_head *a, struct list_head *b)
{
	struct vgic_irq *irqa = container_of(a, struct vgic_irq, ap_list);
	struct vgic_irq *irqb = container_of(b, struct vgic_irq, ap_list);
	bool penda, pendb;
	int ret;

	/*
	 * list_sort may call this function with the same element when
	 * the list is fairly long.
	 */
	if (unlikely(irqa == irqb))
		return 0;

	raw_spin_lock(&irqa->irq_lock);
	raw_spin_lock_nested(&irqb->irq_lock, SINGLE_DEPTH_NESTING);

	if (irqa->active || irqb->active) {
		ret = (int)irqb->active - (int)irqa->active;
		goto out;
	}

	penda = irqa->enabled && irq_is_pending(irqa);
	pendb = irqb->enabled && irq_is_pending(irqb);

	if (!penda || !pendb) {
		ret = (int)pendb - (int)penda;
		goto out;
	}

	/* Both pending and enabled, sort by priority */
	ret = irqa->priority - irqb->priority;
out:
	raw_spin_unlock(&irqb->irq_lock);
	raw_spin_unlock(&irqa->irq_lock);
	return ret;
}

/* Must be called with the ap_list_lock held */
static void vgic_sort_ap_list(struct kvm_vcpu *vcpu)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;

	lockdep_assert_held(&vgic_cpu->ap_list_lock);

	list_sort(NULL, &vgic_cpu->ap_list_head, vgic_irq_cmp);
}

/*
 * Only valid injection if changing level for level-triggered IRQs or for a
 * rising edge, and in-kernel connected IRQ lines can only be controlled by
 * their owner.
 */
static bool vgic_validate_injection(struct vgic_irq *irq, bool level, void *owner)
{
	if (irq->owner != owner)
		return false;

	switch (irq->config) {
	case VGIC_CONFIG_LEVEL:
		return irq->line_level != level;
	case VGIC_CONFIG_EDGE:
		return level;
	}

	return false;
}

/*
 * Check whether an IRQ needs to (and can) be queued to a VCPU's ap list.
 * Do the queuing if necessary, taking the right locks in the right order.
 * Returns true when the IRQ was queued, false otherwise.
 *
 * Needs to be entered with the IRQ lock already held, but will return
 * with all locks dropped.
 */
bool vgic_queue_irq_unlock(struct kvm *kvm, struct vgic_irq *irq,
			   unsigned long flags)
{
	struct kvm_vcpu *vcpu;

	lockdep_assert_held(&irq->irq_lock);

retry:
	vcpu = vgic_target_oracle(irq);
	if (irq->vcpu || !vcpu) {
		/*
		 * If this IRQ is already on a VCPU's ap_list, then it
		 * cannot be moved or modified and there is no more work for
		 * us to do.
		 *
		 * Otherwise, if the irq is not pending and enabled, it does
		 * not need to be inserted into an ap_list and there is also
		 * no more work for us to do.
		 */
		raw_spin_unlock_irqrestore(&irq->irq_lock, flags);

		/*
		 * We have to kick the VCPU here, because we could be
		 * queueing an edge-triggered interrupt for which we
		 * get no EOI maintenance interrupt. In that case,
		 * while the IRQ is already on the VCPU's AP list, the
		 * VCPU could have EOI'ed the original interrupt and
		 * won't see this one until it exits for some other
		 * reason.
		 */
		if (vcpu) {
			kvm_make_request(KVM_REQ_IRQ_PENDING, vcpu);
			kvm_vcpu_kick(vcpu);
		}
		return false;
	}

	/*
	 * We must unlock the irq lock to take the ap_list_lock where
	 * we are going to insert this new pending interrupt.
	 */
	raw_spin_unlock_irqrestore(&irq->irq_lock, flags);

	/* someone can do stuff here, which we re-check below */

	raw_spin_lock_irqsave(&vcpu->arch.vgic_cpu.ap_list_lock, flags);
	raw_spin_lock(&irq->irq_lock);

	/*
	 * Did something change behind our backs?
	 *
	 * There are two cases:
	 * 1) The irq lost its pending state or was disabled behind our
	 *    backs and/or it was queued to another VCPU's ap_list.
	 * 2) Someone changed the affinity on this irq behind our
	 *    backs and we are now holding the wrong ap_list_lock.
	 *
	 * In both cases, drop the locks and retry.
	 */

	if (unlikely(irq->vcpu || vcpu != vgic_target_oracle(irq))) {
		raw_spin_unlock(&irq->irq_lock);
		raw_spin_unlock_irqrestore(&vcpu->arch.vgic_cpu.ap_list_lock,
					   flags);

		raw_spin_lock_irqsave(&irq->irq_lock, flags);
		goto retry;
	}

	/*
	 * Grab a reference to the irq to reflect the fact that it is
	 * now in the ap_list.
	 */
	vgic_get_irq_kref(irq);
	list_add_tail(&irq->ap_list, &vcpu->arch.vgic_cpu.ap_list_head);
	irq->vcpu = vcpu;

	raw_spin_unlock(&irq->irq_lock);
	raw_spin_unlock_irqrestore(&vcpu->arch.vgic_cpu.ap_list_lock, flags);

	kvm_make_request(KVM_REQ_IRQ_PENDING, vcpu);
	kvm_vcpu_kick(vcpu);

	return true;
}

/**
 * kvm_vgic_inject_irq - Inject an IRQ from a device to the vgic
 * @kvm:     The VM structure pointer
 * @cpuid:   The CPU for PPIs
 * @intid:   The INTID to inject a new state to.
 * @level:   Edge-triggered:  true:  to trigger the interrupt
 *			      false: to ignore the call
 *	     Level-sensitive  true:  raise the input signal
 *			      false: lower the input signal
 * @owner:   The opaque pointer to the owner of the IRQ being raised to verify
 *           that the caller is allowed to inject this IRQ.  Userspace
 *           injections will have owner == NULL.
 *
 * The VGIC is not concerned with devices being active-LOW or active-HIGH for
 * level-sensitive interrupts.  You can think of the level parameter as 1
 * being HIGH and 0 being LOW and all devices being active-HIGH.
 */
int kvm_vgic_inject_irq(struct kvm *kvm, int cpuid, unsigned int intid,
			bool level, void *owner)
{
	struct kvm_vcpu *vcpu;
	struct vgic_irq *irq;
	unsigned long flags;
	int ret;

	trace_vgic_update_irq_pending(cpuid, intid, level);

	ret = vgic_lazy_init(kvm);
	if (ret)
		return ret;

	vcpu = kvm_get_vcpu(kvm, cpuid);
	if (!vcpu && intid < VGIC_NR_PRIVATE_IRQS)
		return -EINVAL;

	irq = vgic_get_irq(kvm, vcpu, intid);
	if (!irq)
		return -EINVAL;

	raw_spin_lock_irqsave(&irq->irq_lock, flags);

	if (!vgic_validate_injection(irq, level, owner)) {
		/* Nothing to see here, move along... */
		raw_spin_unlock_irqrestore(&irq->irq_lock, flags);
		vgic_put_irq(kvm, irq);
		return 0;
	}

	if (irq->config == VGIC_CONFIG_LEVEL)
		irq->line_level = level;
	else
		irq->pending_latch = true;

	vgic_queue_irq_unlock(kvm, irq, flags);
	vgic_put_irq(kvm, irq);

	return 0;
}

/* @irq->irq_lock must be held */
static int kvm_vgic_map_irq(struct kvm_vcpu *vcpu, struct vgic_irq *irq,
			    unsigned int host_irq,
			    bool (*get_input_level)(int vindid))
{
	struct irq_desc *desc;
	struct irq_data *data;

	/*
	 * Find the physical IRQ number corresponding to @host_irq
	 */
	desc = irq_to_desc(host_irq);
	if (!desc) {
		kvm_err("%s: no interrupt descriptor\n", __func__);
		return -EINVAL;
	}
	data = irq_desc_get_irq_data(desc);
	while (data->parent_data)
		data = data->parent_data;

	irq->hw = true;
	irq->host_irq = host_irq;
	irq->hwintid = data->hwirq;
	irq->get_input_level = get_input_level;
	return 0;
}

/* @irq->irq_lock must be held */
static inline void kvm_vgic_unmap_irq(struct vgic_irq *irq)
{
	irq->hw = false;
	irq->hwintid = 0;
	irq->get_input_level = NULL;
}

int kvm_vgic_map_phys_irq(struct kvm_vcpu *vcpu, unsigned int host_irq,
			  u32 vintid, bool (*get_input_level)(int vindid))
{
	struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, vintid);
	unsigned long flags;
	int ret;

	BUG_ON(!irq);

	raw_spin_lock_irqsave(&irq->irq_lock, flags);
	ret = kvm_vgic_map_irq(vcpu, irq, host_irq, get_input_level);
	raw_spin_unlock_irqrestore(&irq->irq_lock, flags);
	vgic_put_irq(vcpu->kvm, irq);

	return ret;
}

/**
 * kvm_vgic_reset_mapped_irq - Reset a mapped IRQ
 * @vcpu: The VCPU pointer
 * @vintid: The INTID of the interrupt
 *
 * Reset the active and pending states of a mapped interrupt.  Kernel
 * subsystems injecting mapped interrupts should reset their interrupt lines
 * when we are doing a reset of the VM.
 */
void kvm_vgic_reset_mapped_irq(struct kvm_vcpu *vcpu, u32 vintid)
{
	struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, vintid);
	unsigned long flags;

	if (!irq->hw)
		goto out;

	raw_spin_lock_irqsave(&irq->irq_lock, flags);
	irq->active = false;
	irq->pending_latch = false;
	irq->line_level = false;
	raw_spin_unlock_irqrestore(&irq->irq_lock, flags);
out:
	vgic_put_irq(vcpu->kvm, irq);
}

int kvm_vgic_unmap_phys_irq(struct kvm_vcpu *vcpu, unsigned int vintid)
{
	struct vgic_irq *irq;
	unsigned long flags;

	if (!vgic_initialized(vcpu->kvm))
		return -EAGAIN;

	irq = vgic_get_irq(vcpu->kvm, vcpu, vintid);
	BUG_ON(!irq);

	raw_spin_lock_irqsave(&irq->irq_lock, flags);
	kvm_vgic_unmap_irq(irq);
	raw_spin_unlock_irqrestore(&irq->irq_lock, flags);
	vgic_put_irq(vcpu->kvm, irq);

	return 0;
}

/**
 * kvm_vgic_set_owner - Set the owner of an interrupt for a VM
 *
 * @vcpu:   Pointer to the VCPU (used for PPIs)
 * @intid:  The virtual INTID identifying the interrupt (PPI or SPI)
 * @owner:  Opaque pointer to the owner
 *
 * Returns 0 if intid is not already used by another in-kernel device and the
 * owner is set, otherwise returns an error code.
 */
int kvm_vgic_set_owner(struct kvm_vcpu *vcpu, unsigned int intid, void *owner)
{
	struct vgic_irq *irq;
	unsigned long flags;
	int ret = 0;

	if (!vgic_initialized(vcpu->kvm))
		return -EAGAIN;

	/* SGIs and LPIs cannot be wired up to any device */
	if (!irq_is_ppi(intid) && !vgic_valid_spi(vcpu->kvm, intid))
		return -EINVAL;

	irq = vgic_get_irq(vcpu->kvm, vcpu, intid);
	raw_spin_lock_irqsave(&irq->irq_lock, flags);
	if (irq->owner && irq->owner != owner)
		ret = -EEXIST;
	else
		irq->owner = owner;
	raw_spin_unlock_irqrestore(&irq->irq_lock, flags);

	return ret;
}

/**
 * vgic_prune_ap_list - Remove non-relevant interrupts from the list
 *
 * @vcpu: The VCPU pointer
 *
 * Go over the list of "interesting" interrupts, and prune those that we
 * won't have to consider in the near future.
 */
static void vgic_prune_ap_list(struct kvm_vcpu *vcpu)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;
	struct vgic_irq *irq, *tmp;

	DEBUG_SPINLOCK_BUG_ON(!irqs_disabled());

retry:
	raw_spin_lock(&vgic_cpu->ap_list_lock);

	list_for_each_entry_safe(irq, tmp, &vgic_cpu->ap_list_head, ap_list) {
		struct kvm_vcpu *target_vcpu, *vcpuA, *vcpuB;
		bool target_vcpu_needs_kick = false;

		raw_spin_lock(&irq->irq_lock);

		BUG_ON(vcpu != irq->vcpu);

		target_vcpu = vgic_target_oracle(irq);

		if (!target_vcpu) {
			/*
			 * We don't need to process this interrupt any
			 * further, move it off the list.
			 */
			list_del(&irq->ap_list);
			irq->vcpu = NULL;
			raw_spin_unlock(&irq->irq_lock);

			/*
			 * This vgic_put_irq call matches the
			 * vgic_get_irq_kref in vgic_queue_irq_unlock,
			 * where we added the LPI to the ap_list. As
			 * we remove the irq from the list, we drop
			 * also drop the refcount.
			 */
			vgic_put_irq(vcpu->kvm, irq);
			continue;
		}

		if (target_vcpu == vcpu) {
			/* We're on the right CPU */
			raw_spin_unlock(&irq->irq_lock);
			continue;
		}

		/* This interrupt looks like it has to be migrated. */

		raw_spin_unlock(&irq->irq_lock);
		raw_spin_unlock(&vgic_cpu->ap_list_lock);

		/*
		 * Ensure locking order by always locking the smallest
		 * ID first.
		 */
		if (vcpu->vcpu_id < target_vcpu->vcpu_id) {
			vcpuA = vcpu;
			vcpuB = target_vcpu;
		} else {
			vcpuA = target_vcpu;
			vcpuB = vcpu;
		}

		raw_spin_lock(&vcpuA->arch.vgic_cpu.ap_list_lock);
		raw_spin_lock_nested(&vcpuB->arch.vgic_cpu.ap_list_lock,
				      SINGLE_DEPTH_NESTING);
		raw_spin_lock(&irq->irq_lock);

		/*
		 * If the affinity has been preserved, move the
		 * interrupt around. Otherwise, it means things have
		 * changed while the interrupt was unlocked, and we
		 * need to replay this.
		 *
		 * In all cases, we cannot trust the list not to have
		 * changed, so we restart from the beginning.
		 */
		if (target_vcpu == vgic_target_oracle(irq)) {
			struct vgic_cpu *new_cpu = &target_vcpu->arch.vgic_cpu;

			list_del(&irq->ap_list);
			irq->vcpu = target_vcpu;
			list_add_tail(&irq->ap_list, &new_cpu->ap_list_head);
			target_vcpu_needs_kick = true;
		}

		raw_spin_unlock(&irq->irq_lock);
		raw_spin_unlock(&vcpuB->arch.vgic_cpu.ap_list_lock);
		raw_spin_unlock(&vcpuA->arch.vgic_cpu.ap_list_lock);

		if (target_vcpu_needs_kick) {
			kvm_make_request(KVM_REQ_IRQ_PENDING, target_vcpu);
			kvm_vcpu_kick(target_vcpu);
		}

		goto retry;
	}

	raw_spin_unlock(&vgic_cpu->ap_list_lock);
}

static inline void vgic_fold_lr_state(struct kvm_vcpu *vcpu)
{
	if (kvm_vgic_global_state.type == VGIC_V2)
		vgic_v2_fold_lr_state(vcpu);
	else
		vgic_v3_fold_lr_state(vcpu);
}

/* Requires the irq_lock to be held. */
static inline void vgic_populate_lr(struct kvm_vcpu *vcpu,
				    struct vgic_irq *irq, int lr)
{
	lockdep_assert_held(&irq->irq_lock);

	if (kvm_vgic_global_state.type == VGIC_V2)
		vgic_v2_populate_lr(vcpu, irq, lr);
	else
		vgic_v3_populate_lr(vcpu, irq, lr);
}

static inline void vgic_clear_lr(struct kvm_vcpu *vcpu, int lr)
{
	if (kvm_vgic_global_state.type == VGIC_V2)
		vgic_v2_clear_lr(vcpu, lr);
	else
		vgic_v3_clear_lr(vcpu, lr);
}

static inline void vgic_set_underflow(struct kvm_vcpu *vcpu)
{
	if (kvm_vgic_global_state.type == VGIC_V2)
		vgic_v2_set_underflow(vcpu);
	else
		vgic_v3_set_underflow(vcpu);
}

/* Requires the ap_list_lock to be held. */
static int compute_ap_list_depth(struct kvm_vcpu *vcpu,
				 bool *multi_sgi)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;
	struct vgic_irq *irq;
	int count = 0;

	*multi_sgi = false;

	lockdep_assert_held(&vgic_cpu->ap_list_lock);

	list_for_each_entry(irq, &vgic_cpu->ap_list_head, ap_list) {
		int w;

		raw_spin_lock(&irq->irq_lock);
		/* GICv2 SGIs can count for more than one... */
		w = vgic_irq_get_lr_count(irq);
		raw_spin_unlock(&irq->irq_lock);

		count += w;
		*multi_sgi |= (w > 1);
	}
	return count;
}

/* Requires the VCPU's ap_list_lock to be held. */
static void vgic_flush_lr_state(struct kvm_vcpu *vcpu)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;
	struct vgic_irq *irq;
	int count;
	bool multi_sgi;
	u8 prio = 0xff;
	int i = 0;

	lockdep_assert_held(&vgic_cpu->ap_list_lock);

	count = compute_ap_list_depth(vcpu, &multi_sgi);
	if (count > kvm_vgic_global_state.nr_lr || multi_sgi)
		vgic_sort_ap_list(vcpu);

	count = 0;

	list_for_each_entry(irq, &vgic_cpu->ap_list_head, ap_list) {
		raw_spin_lock(&irq->irq_lock);

		/*
		 * If we have multi-SGIs in the pipeline, we need to
		 * guarantee that they are all seen before any IRQ of
		 * lower priority. In that case, we need to filter out
		 * these interrupts by exiting early. This is easy as
		 * the AP list has been sorted already.
		 */
		if (multi_sgi && irq->priority > prio) {
			_raw_spin_unlock(&irq->irq_lock);
			break;
		}

		if (likely(vgic_target_oracle(irq) == vcpu)) {
			vgic_populate_lr(vcpu, irq, count++);

			if (irq->source)
				prio = irq->priority;
		}

		raw_spin_unlock(&irq->irq_lock);

		if (count == kvm_vgic_global_state.nr_lr) {
			if (!list_is_last(&irq->ap_list,
					  &vgic_cpu->ap_list_head))
				vgic_set_underflow(vcpu);
			break;
		}
	}

	/* Nuke remaining LRs */
	for (i = count ; i < kvm_vgic_global_state.nr_lr; i++)
		vgic_clear_lr(vcpu, i);

	if (!static_branch_unlikely(&kvm_vgic_global_state.gicv3_cpuif))
		vcpu->arch.vgic_cpu.vgic_v2.used_lrs = count;
	else
		vcpu->arch.vgic_cpu.vgic_v3.used_lrs = count;
}

static inline bool can_access_vgic_from_kernel(void)
{
	/*
	 * GICv2 can always be accessed from the kernel because it is
	 * memory-mapped, and VHE systems can access GICv3 EL2 system
	 * registers.
	 */
	return !static_branch_unlikely(&kvm_vgic_global_state.gicv3_cpuif) || has_vhe();
}

static inline void vgic_save_state(struct kvm_vcpu *vcpu)
{
	if (!static_branch_unlikely(&kvm_vgic_global_state.gicv3_cpuif))
		vgic_v2_save_state(vcpu);
	else
		__vgic_v3_save_state(&vcpu->arch.vgic_cpu.vgic_v3);
}

/* Sync back the hardware VGIC state into our emulation after a guest's run. */
void kvm_vgic_sync_hwstate(struct kvm_vcpu *vcpu)
{
	int used_lrs;

	/* An empty ap_list_head implies used_lrs == 0 */
	if (list_empty(&vcpu->arch.vgic_cpu.ap_list_head))
		return;

	if (can_access_vgic_from_kernel())
		vgic_save_state(vcpu);

	if (!static_branch_unlikely(&kvm_vgic_global_state.gicv3_cpuif))
		used_lrs = vcpu->arch.vgic_cpu.vgic_v2.used_lrs;
	else
		used_lrs = vcpu->arch.vgic_cpu.vgic_v3.used_lrs;

	if (used_lrs)
		vgic_fold_lr_state(vcpu);
	vgic_prune_ap_list(vcpu);
}

static inline void vgic_restore_state(struct kvm_vcpu *vcpu)
{
	if (!static_branch_unlikely(&kvm_vgic_global_state.gicv3_cpuif))
		vgic_v2_restore_state(vcpu);
	else
		__vgic_v3_restore_state(&vcpu->arch.vgic_cpu.vgic_v3);
}

/* Flush our emulation state into the GIC hardware before entering the guest. */
void kvm_vgic_flush_hwstate(struct kvm_vcpu *vcpu)
{
	/*
	 * If there are no virtual interrupts active or pending for this
	 * VCPU, then there is no work to do and we can bail out without
	 * taking any lock.  There is a potential race with someone injecting
	 * interrupts to the VCPU, but it is a benign race as the VCPU will
	 * either observe the new interrupt before or after doing this check,
	 * and introducing additional synchronization mechanism doesn't change
	 * this.
	 *
	 * Note that we still need to go through the whole thing if anything
	 * can be directly injected (GICv4).
	 */
	if (list_empty(&vcpu->arch.vgic_cpu.ap_list_head) &&
	    !vgic_supports_direct_msis(vcpu->kvm))
		return;

	DEBUG_SPINLOCK_BUG_ON(!irqs_disabled());

	if (!list_empty(&vcpu->arch.vgic_cpu.ap_list_head)) {
		raw_spin_lock(&vcpu->arch.vgic_cpu.ap_list_lock);
		vgic_flush_lr_state(vcpu);
		raw_spin_unlock(&vcpu->arch.vgic_cpu.ap_list_lock);
	}

	if (can_access_vgic_from_kernel())
		vgic_restore_state(vcpu);
}

void kvm_vgic_load(struct kvm_vcpu *vcpu)
{
	if (unlikely(!vgic_initialized(vcpu->kvm)))
		return;

	if (kvm_vgic_global_state.type == VGIC_V2)
		vgic_v2_load(vcpu);
	else
		vgic_v3_load(vcpu);
}

void kvm_vgic_put(struct kvm_vcpu *vcpu)
{
	if (unlikely(!vgic_initialized(vcpu->kvm)))
		return;

	if (kvm_vgic_global_state.type == VGIC_V2)
		vgic_v2_put(vcpu);
	else
		vgic_v3_put(vcpu);
}

void kvm_vgic_vmcr_sync(struct kvm_vcpu *vcpu)
{
	if (unlikely(!irqchip_in_kernel(vcpu->kvm)))
		return;

	if (kvm_vgic_global_state.type == VGIC_V2)
		vgic_v2_vmcr_sync(vcpu);
	else
		vgic_v3_vmcr_sync(vcpu);
}

int kvm_vgic_vcpu_pending_irq(struct kvm_vcpu *vcpu)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;
	struct vgic_irq *irq;
	bool pending = false;
	unsigned long flags;
	struct vgic_vmcr vmcr;

	if (!vcpu->kvm->arch.vgic.enabled)
		return false;

	if (vcpu->arch.vgic_cpu.vgic_v3.its_vpe.pending_last)
		return true;

	vgic_get_vmcr(vcpu, &vmcr);

	raw_spin_lock_irqsave(&vgic_cpu->ap_list_lock, flags);

	list_for_each_entry(irq, &vgic_cpu->ap_list_head, ap_list) {
		raw_spin_lock(&irq->irq_lock);
		pending = irq_is_pending(irq) && irq->enabled &&
			  !irq->active &&
			  irq->priority < vmcr.pmr;
		raw_spin_unlock(&irq->irq_lock);

		if (pending)
			break;
	}

	raw_spin_unlock_irqrestore(&vgic_cpu->ap_list_lock, flags);

	return pending;
}

void vgic_kick_vcpus(struct kvm *kvm)
{
	struct kvm_vcpu *vcpu;
	int c;

	/*
	 * We've injected an interrupt, time to find out who deserves
	 * a good kick...
	 */
	kvm_for_each_vcpu(c, vcpu, kvm) {
		if (kvm_vgic_vcpu_pending_irq(vcpu)) {
			kvm_make_request(KVM_REQ_IRQ_PENDING, vcpu);
			kvm_vcpu_kick(vcpu);
		}
	}
}

bool kvm_vgic_map_is_active(struct kvm_vcpu *vcpu, unsigned int vintid)
{
	struct vgic_irq *irq;
	bool map_is_active;
	unsigned long flags;

	if (!vgic_initialized(vcpu->kvm))
		return false;

	irq = vgic_get_irq(vcpu->kvm, vcpu, vintid);
	raw_spin_lock_irqsave(&irq->irq_lock, flags);
	map_is_active = irq->hw && irq->active;
	raw_spin_unlock_irqrestore(&irq->irq_lock, flags);
	vgic_put_irq(vcpu->kvm, irq);

	return map_is_active;
}
