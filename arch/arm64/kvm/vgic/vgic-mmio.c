// SPDX-License-Identifier: GPL-2.0-only
/*
 * VGIC MMIO handling functions
 */

#include <linux/bitops.h>
#include <linux/bsearch.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <kvm/iodev.h>
#include <kvm/arm_arch_timer.h>
#include <kvm/arm_vgic.h>

#include "vgic.h"
#include "vgic-mmio.h"

unsigned long vgic_mmio_read_raz(struct kvm_vcpu *vcpu,
				 gpa_t addr, unsigned int len)
{
	return 0;
}

unsigned long vgic_mmio_read_rao(struct kvm_vcpu *vcpu,
				 gpa_t addr, unsigned int len)
{
	return -1UL;
}

void vgic_mmio_write_wi(struct kvm_vcpu *vcpu, gpa_t addr,
			unsigned int len, unsigned long val)
{
	/* Ignore */
}

int vgic_mmio_uaccess_write_wi(struct kvm_vcpu *vcpu, gpa_t addr,
			       unsigned int len, unsigned long val)
{
	/* Ignore */
	return 0;
}

unsigned long vgic_mmio_read_group(struct kvm_vcpu *vcpu,
				   gpa_t addr, unsigned int len)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	u32 value = 0;
	int i;

	/* Loop over all IRQs affected by this read */
	for (i = 0; i < len * 8; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		if (irq->group)
			value |= BIT(i);

		vgic_put_irq(vcpu->kvm, irq);
	}

	return value;
}

static void vgic_update_vsgi(struct vgic_irq *irq)
{
	WARN_ON(its_prop_update_vsgi(irq->host_irq, irq->priority, irq->group));
}

void vgic_mmio_write_group(struct kvm_vcpu *vcpu, gpa_t addr,
			   unsigned int len, unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	int i;
	unsigned long flags;

	for (i = 0; i < len * 8; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		raw_spin_lock_irqsave(&irq->irq_lock, flags);
		irq->group = !!(val & BIT(i));
		if (irq->hw && vgic_irq_is_sgi(irq->intid)) {
			vgic_update_vsgi(irq);
			raw_spin_unlock_irqrestore(&irq->irq_lock, flags);
		} else {
			vgic_queue_irq_unlock(vcpu->kvm, irq, flags);
		}

		vgic_put_irq(vcpu->kvm, irq);
	}
}

/*
 * Read accesses to both GICD_ICENABLER and GICD_ISENABLER return the value
 * of the enabled bit, so there is only one function for both here.
 */
unsigned long vgic_mmio_read_enable(struct kvm_vcpu *vcpu,
				    gpa_t addr, unsigned int len)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	u32 value = 0;
	int i;

	/* Loop over all IRQs affected by this read */
	for (i = 0; i < len * 8; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		if (irq->enabled)
			value |= (1U << i);

		vgic_put_irq(vcpu->kvm, irq);
	}

	return value;
}

void vgic_mmio_write_senable(struct kvm_vcpu *vcpu,
			     gpa_t addr, unsigned int len,
			     unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	int i;
	unsigned long flags;

	for_each_set_bit(i, &val, len * 8) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		raw_spin_lock_irqsave(&irq->irq_lock, flags);
		if (irq->hw && vgic_irq_is_sgi(irq->intid)) {
			if (!irq->enabled) {
				struct irq_data *data;

				irq->enabled = true;
				data = &irq_to_desc(irq->host_irq)->irq_data;
				while (irqd_irq_disabled(data))
					enable_irq(irq->host_irq);
			}

			raw_spin_unlock_irqrestore(&irq->irq_lock, flags);
			vgic_put_irq(vcpu->kvm, irq);

			continue;
		} else if (vgic_irq_is_mapped_level(irq)) {
			bool was_high = irq->line_level;

			/*
			 * We need to update the state of the interrupt because
			 * the guest might have changed the state of the device
			 * while the interrupt was disabled at the VGIC level.
			 */
			irq->line_level = vgic_get_phys_line_level(irq);
			/*
			 * Deactivate the physical interrupt so the GIC will let
			 * us know when it is asserted again.
			 */
			if (!irq->active && was_high && !irq->line_level)
				vgic_irq_set_phys_active(irq, false);
		}
		irq->enabled = true;
		vgic_queue_irq_unlock(vcpu->kvm, irq, flags);

		vgic_put_irq(vcpu->kvm, irq);
	}
}

void vgic_mmio_write_cenable(struct kvm_vcpu *vcpu,
			     gpa_t addr, unsigned int len,
			     unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	int i;
	unsigned long flags;

	for_each_set_bit(i, &val, len * 8) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		raw_spin_lock_irqsave(&irq->irq_lock, flags);
		if (irq->hw && vgic_irq_is_sgi(irq->intid) && irq->enabled)
			disable_irq_nosync(irq->host_irq);

		irq->enabled = false;

		raw_spin_unlock_irqrestore(&irq->irq_lock, flags);
		vgic_put_irq(vcpu->kvm, irq);
	}
}

int vgic_uaccess_write_senable(struct kvm_vcpu *vcpu,
			       gpa_t addr, unsigned int len,
			       unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	int i;
	unsigned long flags;

	for_each_set_bit(i, &val, len * 8) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		raw_spin_lock_irqsave(&irq->irq_lock, flags);
		irq->enabled = true;
		vgic_queue_irq_unlock(vcpu->kvm, irq, flags);

		vgic_put_irq(vcpu->kvm, irq);
	}

	return 0;
}

int vgic_uaccess_write_cenable(struct kvm_vcpu *vcpu,
			       gpa_t addr, unsigned int len,
			       unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	int i;
	unsigned long flags;

	for_each_set_bit(i, &val, len * 8) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		raw_spin_lock_irqsave(&irq->irq_lock, flags);
		irq->enabled = false;
		raw_spin_unlock_irqrestore(&irq->irq_lock, flags);

		vgic_put_irq(vcpu->kvm, irq);
	}

	return 0;
}

unsigned long vgic_mmio_read_pending(struct kvm_vcpu *vcpu,
				     gpa_t addr, unsigned int len)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	u32 value = 0;
	int i;

	/* Loop over all IRQs affected by this read */
	for (i = 0; i < len * 8; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);
		unsigned long flags;
		bool val;

		raw_spin_lock_irqsave(&irq->irq_lock, flags);
		if (irq->hw && vgic_irq_is_sgi(irq->intid)) {
			int err;

			val = false;
			err = irq_get_irqchip_state(irq->host_irq,
						    IRQCHIP_STATE_PENDING,
						    &val);
			WARN_RATELIMIT(err, "IRQ %d", irq->host_irq);
		} else {
			val = irq_is_pending(irq);
		}

		value |= ((u32)val << i);
		raw_spin_unlock_irqrestore(&irq->irq_lock, flags);

		vgic_put_irq(vcpu->kvm, irq);
	}

	return value;
}

static bool is_vgic_v2_sgi(struct kvm_vcpu *vcpu, struct vgic_irq *irq)
{
	return (vgic_irq_is_sgi(irq->intid) &&
		vcpu->kvm->arch.vgic.vgic_model == KVM_DEV_TYPE_ARM_VGIC_V2);
}

void vgic_mmio_write_spending(struct kvm_vcpu *vcpu,
			      gpa_t addr, unsigned int len,
			      unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	int i;
	unsigned long flags;

	for_each_set_bit(i, &val, len * 8) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		/* GICD_ISPENDR0 SGI bits are WI */
		if (is_vgic_v2_sgi(vcpu, irq)) {
			vgic_put_irq(vcpu->kvm, irq);
			continue;
		}

		raw_spin_lock_irqsave(&irq->irq_lock, flags);

		if (irq->hw && vgic_irq_is_sgi(irq->intid)) {
			/* HW SGI? Ask the GIC to inject it */
			int err;
			err = irq_set_irqchip_state(irq->host_irq,
						    IRQCHIP_STATE_PENDING,
						    true);
			WARN_RATELIMIT(err, "IRQ %d", irq->host_irq);

			raw_spin_unlock_irqrestore(&irq->irq_lock, flags);
			vgic_put_irq(vcpu->kvm, irq);

			continue;
		}

		irq->pending_latch = true;
		if (irq->hw)
			vgic_irq_set_phys_active(irq, true);

		vgic_queue_irq_unlock(vcpu->kvm, irq, flags);
		vgic_put_irq(vcpu->kvm, irq);
	}
}

int vgic_uaccess_write_spending(struct kvm_vcpu *vcpu,
				gpa_t addr, unsigned int len,
				unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	int i;
	unsigned long flags;

	for_each_set_bit(i, &val, len * 8) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		raw_spin_lock_irqsave(&irq->irq_lock, flags);
		irq->pending_latch = true;

		/*
		 * GICv2 SGIs are terribly broken. We can't restore
		 * the source of the interrupt, so just pick the vcpu
		 * itself as the source...
		 */
		if (is_vgic_v2_sgi(vcpu, irq))
			irq->source |= BIT(vcpu->vcpu_id);

		vgic_queue_irq_unlock(vcpu->kvm, irq, flags);

		vgic_put_irq(vcpu->kvm, irq);
	}

	return 0;
}

/* Must be called with irq->irq_lock held */
static void vgic_hw_irq_cpending(struct kvm_vcpu *vcpu, struct vgic_irq *irq)
{
	irq->pending_latch = false;

	/*
	 * We don't want the guest to effectively mask the physical
	 * interrupt by doing a write to SPENDR followed by a write to
	 * CPENDR for HW interrupts, so we clear the active state on
	 * the physical side if the virtual interrupt is not active.
	 * This may lead to taking an additional interrupt on the
	 * host, but that should not be a problem as the worst that
	 * can happen is an additional vgic injection.  We also clear
	 * the pending state to maintain proper semantics for edge HW
	 * interrupts.
	 */
	vgic_irq_set_phys_pending(irq, false);
	if (!irq->active)
		vgic_irq_set_phys_active(irq, false);
}

void vgic_mmio_write_cpending(struct kvm_vcpu *vcpu,
			      gpa_t addr, unsigned int len,
			      unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	int i;
	unsigned long flags;

	for_each_set_bit(i, &val, len * 8) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		/* GICD_ICPENDR0 SGI bits are WI */
		if (is_vgic_v2_sgi(vcpu, irq)) {
			vgic_put_irq(vcpu->kvm, irq);
			continue;
		}

		raw_spin_lock_irqsave(&irq->irq_lock, flags);

		if (irq->hw && vgic_irq_is_sgi(irq->intid)) {
			/* HW SGI? Ask the GIC to clear its pending bit */
			int err;
			err = irq_set_irqchip_state(irq->host_irq,
						    IRQCHIP_STATE_PENDING,
						    false);
			WARN_RATELIMIT(err, "IRQ %d", irq->host_irq);

			raw_spin_unlock_irqrestore(&irq->irq_lock, flags);
			vgic_put_irq(vcpu->kvm, irq);

			continue;
		}

		if (irq->hw)
			vgic_hw_irq_cpending(vcpu, irq);
		else
			irq->pending_latch = false;

		raw_spin_unlock_irqrestore(&irq->irq_lock, flags);
		vgic_put_irq(vcpu->kvm, irq);
	}
}

int vgic_uaccess_write_cpending(struct kvm_vcpu *vcpu,
				gpa_t addr, unsigned int len,
				unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	int i;
	unsigned long flags;

	for_each_set_bit(i, &val, len * 8) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		raw_spin_lock_irqsave(&irq->irq_lock, flags);
		/*
		 * More fun with GICv2 SGIs! If we're clearing one of them
		 * from userspace, which source vcpu to clear? Let's not
		 * even think of it, and blow the whole set.
		 */
		if (is_vgic_v2_sgi(vcpu, irq))
			irq->source = 0;

		irq->pending_latch = false;

		raw_spin_unlock_irqrestore(&irq->irq_lock, flags);

		vgic_put_irq(vcpu->kvm, irq);
	}

	return 0;
}

/*
 * If we are fiddling with an IRQ's active state, we have to make sure the IRQ
 * is not queued on some running VCPU's LRs, because then the change to the
 * active state can be overwritten when the VCPU's state is synced coming back
 * from the guest.
 *
 * For shared interrupts as well as GICv3 private interrupts, we have to
 * stop all the VCPUs because interrupts can be migrated while we don't hold
 * the IRQ locks and we don't want to be chasing moving targets.
 *
 * For GICv2 private interrupts we don't have to do anything because
 * userspace accesses to the VGIC state already require all VCPUs to be
 * stopped, and only the VCPU itself can modify its private interrupts
 * active state, which guarantees that the VCPU is not running.
 */
static void vgic_access_active_prepare(struct kvm_vcpu *vcpu, u32 intid)
{
	if (vcpu->kvm->arch.vgic.vgic_model == KVM_DEV_TYPE_ARM_VGIC_V3 ||
	    intid >= VGIC_NR_PRIVATE_IRQS)
		kvm_arm_halt_guest(vcpu->kvm);
}

/* See vgic_access_active_prepare */
static void vgic_access_active_finish(struct kvm_vcpu *vcpu, u32 intid)
{
	if (vcpu->kvm->arch.vgic.vgic_model == KVM_DEV_TYPE_ARM_VGIC_V3 ||
	    intid >= VGIC_NR_PRIVATE_IRQS)
		kvm_arm_resume_guest(vcpu->kvm);
}

static unsigned long __vgic_mmio_read_active(struct kvm_vcpu *vcpu,
					     gpa_t addr, unsigned int len)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	u32 value = 0;
	int i;

	/* Loop over all IRQs affected by this read */
	for (i = 0; i < len * 8; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		/*
		 * Even for HW interrupts, don't evaluate the HW state as
		 * all the guest is interested in is the virtual state.
		 */
		if (irq->active)
			value |= (1U << i);

		vgic_put_irq(vcpu->kvm, irq);
	}

	return value;
}

unsigned long vgic_mmio_read_active(struct kvm_vcpu *vcpu,
				    gpa_t addr, unsigned int len)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	u32 val;

	mutex_lock(&vcpu->kvm->lock);
	vgic_access_active_prepare(vcpu, intid);

	val = __vgic_mmio_read_active(vcpu, addr, len);

	vgic_access_active_finish(vcpu, intid);
	mutex_unlock(&vcpu->kvm->lock);

	return val;
}

unsigned long vgic_uaccess_read_active(struct kvm_vcpu *vcpu,
				    gpa_t addr, unsigned int len)
{
	return __vgic_mmio_read_active(vcpu, addr, len);
}

/* Must be called with irq->irq_lock held */
static void vgic_hw_irq_change_active(struct kvm_vcpu *vcpu, struct vgic_irq *irq,
				      bool active, bool is_uaccess)
{
	if (is_uaccess)
		return;

	irq->active = active;
	vgic_irq_set_phys_active(irq, active);
}

static void vgic_mmio_change_active(struct kvm_vcpu *vcpu, struct vgic_irq *irq,
				    bool active)
{
	unsigned long flags;
	struct kvm_vcpu *requester_vcpu = kvm_get_running_vcpu();

	raw_spin_lock_irqsave(&irq->irq_lock, flags);

	if (irq->hw && !vgic_irq_is_sgi(irq->intid)) {
		vgic_hw_irq_change_active(vcpu, irq, active, !requester_vcpu);
	} else if (irq->hw && vgic_irq_is_sgi(irq->intid)) {
		/*
		 * GICv4.1 VSGI feature doesn't track an active state,
		 * so let's not kid ourselves, there is nothing we can
		 * do here.
		 */
		irq->active = false;
	} else {
		u32 model = vcpu->kvm->arch.vgic.vgic_model;
		u8 active_source;

		irq->active = active;

		/*
		 * The GICv2 architecture indicates that the source CPUID for
		 * an SGI should be provided during an EOI which implies that
		 * the active state is stored somewhere, but at the same time
		 * this state is not architecturally exposed anywhere and we
		 * have no way of knowing the right source.
		 *
		 * This may lead to a VCPU not being able to receive
		 * additional instances of a particular SGI after migration
		 * for a GICv2 VM on some GIC implementations.  Oh well.
		 */
		active_source = (requester_vcpu) ? requester_vcpu->vcpu_id : 0;

		if (model == KVM_DEV_TYPE_ARM_VGIC_V2 &&
		    active && vgic_irq_is_sgi(irq->intid))
			irq->active_source = active_source;
	}

	if (irq->active)
		vgic_queue_irq_unlock(vcpu->kvm, irq, flags);
	else
		raw_spin_unlock_irqrestore(&irq->irq_lock, flags);
}

static void __vgic_mmio_write_cactive(struct kvm_vcpu *vcpu,
				      gpa_t addr, unsigned int len,
				      unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	int i;

	for_each_set_bit(i, &val, len * 8) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);
		vgic_mmio_change_active(vcpu, irq, false);
		vgic_put_irq(vcpu->kvm, irq);
	}
}

void vgic_mmio_write_cactive(struct kvm_vcpu *vcpu,
			     gpa_t addr, unsigned int len,
			     unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);

	mutex_lock(&vcpu->kvm->lock);
	vgic_access_active_prepare(vcpu, intid);

	__vgic_mmio_write_cactive(vcpu, addr, len, val);

	vgic_access_active_finish(vcpu, intid);
	mutex_unlock(&vcpu->kvm->lock);
}

int vgic_mmio_uaccess_write_cactive(struct kvm_vcpu *vcpu,
				     gpa_t addr, unsigned int len,
				     unsigned long val)
{
	__vgic_mmio_write_cactive(vcpu, addr, len, val);
	return 0;
}

static void __vgic_mmio_write_sactive(struct kvm_vcpu *vcpu,
				      gpa_t addr, unsigned int len,
				      unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	int i;

	for_each_set_bit(i, &val, len * 8) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);
		vgic_mmio_change_active(vcpu, irq, true);
		vgic_put_irq(vcpu->kvm, irq);
	}
}

void vgic_mmio_write_sactive(struct kvm_vcpu *vcpu,
			     gpa_t addr, unsigned int len,
			     unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);

	mutex_lock(&vcpu->kvm->lock);
	vgic_access_active_prepare(vcpu, intid);

	__vgic_mmio_write_sactive(vcpu, addr, len, val);

	vgic_access_active_finish(vcpu, intid);
	mutex_unlock(&vcpu->kvm->lock);
}

int vgic_mmio_uaccess_write_sactive(struct kvm_vcpu *vcpu,
				     gpa_t addr, unsigned int len,
				     unsigned long val)
{
	__vgic_mmio_write_sactive(vcpu, addr, len, val);
	return 0;
}

unsigned long vgic_mmio_read_priority(struct kvm_vcpu *vcpu,
				      gpa_t addr, unsigned int len)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 8);
	int i;
	u64 val = 0;

	for (i = 0; i < len; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		val |= (u64)irq->priority << (i * 8);

		vgic_put_irq(vcpu->kvm, irq);
	}

	return val;
}

/*
 * We currently don't handle changing the priority of an interrupt that
 * is already pending on a VCPU. If there is a need for this, we would
 * need to make this VCPU exit and re-evaluate the priorities, potentially
 * leading to this interrupt getting presented now to the guest (if it has
 * been masked by the priority mask before).
 */
void vgic_mmio_write_priority(struct kvm_vcpu *vcpu,
			      gpa_t addr, unsigned int len,
			      unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 8);
	int i;
	unsigned long flags;

	for (i = 0; i < len; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		raw_spin_lock_irqsave(&irq->irq_lock, flags);
		/* Narrow the priority range to what we actually support */
		irq->priority = (val >> (i * 8)) & GENMASK(7, 8 - VGIC_PRI_BITS);
		if (irq->hw && vgic_irq_is_sgi(irq->intid))
			vgic_update_vsgi(irq);
		raw_spin_unlock_irqrestore(&irq->irq_lock, flags);

		vgic_put_irq(vcpu->kvm, irq);
	}
}

unsigned long vgic_mmio_read_config(struct kvm_vcpu *vcpu,
				    gpa_t addr, unsigned int len)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 2);
	u32 value = 0;
	int i;

	for (i = 0; i < len * 4; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		if (irq->config == VGIC_CONFIG_EDGE)
			value |= (2U << (i * 2));

		vgic_put_irq(vcpu->kvm, irq);
	}

	return value;
}

void vgic_mmio_write_config(struct kvm_vcpu *vcpu,
			    gpa_t addr, unsigned int len,
			    unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 2);
	int i;
	unsigned long flags;

	for (i = 0; i < len * 4; i++) {
		struct vgic_irq *irq;

		/*
		 * The configuration cannot be changed for SGIs in general,
		 * for PPIs this is IMPLEMENTATION DEFINED. The arch timer
		 * code relies on PPIs being level triggered, so we also
		 * make them read-only here.
		 */
		if (intid + i < VGIC_NR_PRIVATE_IRQS)
			continue;

		irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);
		raw_spin_lock_irqsave(&irq->irq_lock, flags);

		if (test_bit(i * 2 + 1, &val))
			irq->config = VGIC_CONFIG_EDGE;
		else
			irq->config = VGIC_CONFIG_LEVEL;

		raw_spin_unlock_irqrestore(&irq->irq_lock, flags);
		vgic_put_irq(vcpu->kvm, irq);
	}
}

u64 vgic_read_irq_line_level_info(struct kvm_vcpu *vcpu, u32 intid)
{
	int i;
	u64 val = 0;
	int nr_irqs = vcpu->kvm->arch.vgic.nr_spis + VGIC_NR_PRIVATE_IRQS;

	for (i = 0; i < 32; i++) {
		struct vgic_irq *irq;

		if ((intid + i) < VGIC_NR_SGIS || (intid + i) >= nr_irqs)
			continue;

		irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);
		if (irq->config == VGIC_CONFIG_LEVEL && irq->line_level)
			val |= (1U << i);

		vgic_put_irq(vcpu->kvm, irq);
	}

	return val;
}

void vgic_write_irq_line_level_info(struct kvm_vcpu *vcpu, u32 intid,
				    const u64 val)
{
	int i;
	int nr_irqs = vcpu->kvm->arch.vgic.nr_spis + VGIC_NR_PRIVATE_IRQS;
	unsigned long flags;

	for (i = 0; i < 32; i++) {
		struct vgic_irq *irq;
		bool new_level;

		if ((intid + i) < VGIC_NR_SGIS || (intid + i) >= nr_irqs)
			continue;

		irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		/*
		 * Line level is set irrespective of irq type
		 * (level or edge) to avoid dependency that VM should
		 * restore irq config before line level.
		 */
		new_level = !!(val & (1U << i));
		raw_spin_lock_irqsave(&irq->irq_lock, flags);
		irq->line_level = new_level;
		if (new_level)
			vgic_queue_irq_unlock(vcpu->kvm, irq, flags);
		else
			raw_spin_unlock_irqrestore(&irq->irq_lock, flags);

		vgic_put_irq(vcpu->kvm, irq);
	}
}

static int match_region(const void *key, const void *elt)
{
	const unsigned int offset = (unsigned long)key;
	const struct vgic_register_region *region = elt;

	if (offset < region->reg_offset)
		return -1;

	if (offset >= region->reg_offset + region->len)
		return 1;

	return 0;
}

const struct vgic_register_region *
vgic_find_mmio_region(const struct vgic_register_region *regions,
		      int nr_regions, unsigned int offset)
{
	return bsearch((void *)(uintptr_t)offset, regions, nr_regions,
		       sizeof(regions[0]), match_region);
}

void vgic_set_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcr)
{
	if (kvm_vgic_global_state.type == VGIC_V2)
		vgic_v2_set_vmcr(vcpu, vmcr);
	else
		vgic_v3_set_vmcr(vcpu, vmcr);
}

void vgic_get_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcr)
{
	if (kvm_vgic_global_state.type == VGIC_V2)
		vgic_v2_get_vmcr(vcpu, vmcr);
	else
		vgic_v3_get_vmcr(vcpu, vmcr);
}

/*
 * kvm_mmio_read_buf() returns a value in a format where it can be converted
 * to a byte array and be directly observed as the guest wanted it to appear
 * in memory if it had done the store itself, which is LE for the GIC, as the
 * guest knows the GIC is always LE.
 *
 * We convert this value to the CPUs native format to deal with it as a data
 * value.
 */
unsigned long vgic_data_mmio_bus_to_host(const void *val, unsigned int len)
{
	unsigned long data = kvm_mmio_read_buf(val, len);

	switch (len) {
	case 1:
		return data;
	case 2:
		return le16_to_cpu(data);
	case 4:
		return le32_to_cpu(data);
	default:
		return le64_to_cpu(data);
	}
}

/*
 * kvm_mmio_write_buf() expects a value in a format such that if converted to
 * a byte array it is observed as the guest would see it if it could perform
 * the load directly.  Since the GIC is LE, and the guest knows this, the
 * guest expects a value in little endian format.
 *
 * We convert the data value from the CPUs native format to LE so that the
 * value is returned in the proper format.
 */
void vgic_data_host_to_mmio_bus(void *buf, unsigned int len,
				unsigned long data)
{
	switch (len) {
	case 1:
		break;
	case 2:
		data = cpu_to_le16(data);
		break;
	case 4:
		data = cpu_to_le32(data);
		break;
	default:
		data = cpu_to_le64(data);
	}

	kvm_mmio_write_buf(buf, len, data);
}

static
struct vgic_io_device *kvm_to_vgic_iodev(const struct kvm_io_device *dev)
{
	return container_of(dev, struct vgic_io_device, dev);
}

static bool check_region(const struct kvm *kvm,
			 const struct vgic_register_region *region,
			 gpa_t addr, int len)
{
	int flags, nr_irqs = kvm->arch.vgic.nr_spis + VGIC_NR_PRIVATE_IRQS;

	switch (len) {
	case sizeof(u8):
		flags = VGIC_ACCESS_8bit;
		break;
	case sizeof(u32):
		flags = VGIC_ACCESS_32bit;
		break;
	case sizeof(u64):
		flags = VGIC_ACCESS_64bit;
		break;
	default:
		return false;
	}

	if ((region->access_flags & flags) && IS_ALIGNED(addr, len)) {
		if (!region->bits_per_irq)
			return true;

		/* Do we access a non-allocated IRQ? */
		return VGIC_ADDR_TO_INTID(addr, region->bits_per_irq) < nr_irqs;
	}

	return false;
}

const struct vgic_register_region *
vgic_get_mmio_region(struct kvm_vcpu *vcpu, struct vgic_io_device *iodev,
		     gpa_t addr, int len)
{
	const struct vgic_register_region *region;

	region = vgic_find_mmio_region(iodev->regions, iodev->nr_regions,
				       addr - iodev->base_addr);
	if (!region || !check_region(vcpu->kvm, region, addr, len))
		return NULL;

	return region;
}

static int vgic_uaccess_read(struct kvm_vcpu *vcpu, struct vgic_io_device *iodev,
			     gpa_t addr, u32 *val)
{
	const struct vgic_register_region *region;
	struct kvm_vcpu *r_vcpu;

	region = vgic_get_mmio_region(vcpu, iodev, addr, sizeof(u32));
	if (!region) {
		*val = 0;
		return 0;
	}

	r_vcpu = iodev->redist_vcpu ? iodev->redist_vcpu : vcpu;
	if (region->uaccess_read)
		*val = region->uaccess_read(r_vcpu, addr, sizeof(u32));
	else
		*val = region->read(r_vcpu, addr, sizeof(u32));

	return 0;
}

static int vgic_uaccess_write(struct kvm_vcpu *vcpu, struct vgic_io_device *iodev,
			      gpa_t addr, const u32 *val)
{
	const struct vgic_register_region *region;
	struct kvm_vcpu *r_vcpu;

	region = vgic_get_mmio_region(vcpu, iodev, addr, sizeof(u32));
	if (!region)
		return 0;

	r_vcpu = iodev->redist_vcpu ? iodev->redist_vcpu : vcpu;
	if (region->uaccess_write)
		return region->uaccess_write(r_vcpu, addr, sizeof(u32), *val);

	region->write(r_vcpu, addr, sizeof(u32), *val);
	return 0;
}

/*
 * Userland access to VGIC registers.
 */
int vgic_uaccess(struct kvm_vcpu *vcpu, struct vgic_io_device *dev,
		 bool is_write, int offset, u32 *val)
{
	if (is_write)
		return vgic_uaccess_write(vcpu, dev, offset, val);
	else
		return vgic_uaccess_read(vcpu, dev, offset, val);
}

static int dispatch_mmio_read(struct kvm_vcpu *vcpu, struct kvm_io_device *dev,
			      gpa_t addr, int len, void *val)
{
	struct vgic_io_device *iodev = kvm_to_vgic_iodev(dev);
	const struct vgic_register_region *region;
	unsigned long data = 0;

	region = vgic_get_mmio_region(vcpu, iodev, addr, len);
	if (!region) {
		memset(val, 0, len);
		return 0;
	}

	switch (iodev->iodev_type) {
	case IODEV_CPUIF:
		data = region->read(vcpu, addr, len);
		break;
	case IODEV_DIST:
		data = region->read(vcpu, addr, len);
		break;
	case IODEV_REDIST:
		data = region->read(iodev->redist_vcpu, addr, len);
		break;
	case IODEV_ITS:
		data = region->its_read(vcpu->kvm, iodev->its, addr, len);
		break;
	}

	vgic_data_host_to_mmio_bus(val, len, data);
	return 0;
}

static int dispatch_mmio_write(struct kvm_vcpu *vcpu, struct kvm_io_device *dev,
			       gpa_t addr, int len, const void *val)
{
	struct vgic_io_device *iodev = kvm_to_vgic_iodev(dev);
	const struct vgic_register_region *region;
	unsigned long data = vgic_data_mmio_bus_to_host(val, len);

	region = vgic_get_mmio_region(vcpu, iodev, addr, len);
	if (!region)
		return 0;

	switch (iodev->iodev_type) {
	case IODEV_CPUIF:
		region->write(vcpu, addr, len, data);
		break;
	case IODEV_DIST:
		region->write(vcpu, addr, len, data);
		break;
	case IODEV_REDIST:
		region->write(iodev->redist_vcpu, addr, len, data);
		break;
	case IODEV_ITS:
		region->its_write(vcpu->kvm, iodev->its, addr, len, data);
		break;
	}

	return 0;
}

struct kvm_io_device_ops kvm_io_gic_ops = {
	.read = dispatch_mmio_read,
	.write = dispatch_mmio_write,
};

int vgic_register_dist_iodev(struct kvm *kvm, gpa_t dist_base_address,
			     enum vgic_type type)
{
	struct vgic_io_device *io_device = &kvm->arch.vgic.dist_iodev;
	int ret = 0;
	unsigned int len;

	switch (type) {
	case VGIC_V2:
		len = vgic_v2_init_dist_iodev(io_device);
		break;
	case VGIC_V3:
		len = vgic_v3_init_dist_iodev(io_device);
		break;
	default:
		BUG_ON(1);
	}

	io_device->base_addr = dist_base_address;
	io_device->iodev_type = IODEV_DIST;
	io_device->redist_vcpu = NULL;

	mutex_lock(&kvm->slots_lock);
	ret = kvm_io_bus_register_dev(kvm, KVM_MMIO_BUS, dist_base_address,
				      len, &io_device->dev);
	mutex_unlock(&kvm->slots_lock);

	return ret;
}
