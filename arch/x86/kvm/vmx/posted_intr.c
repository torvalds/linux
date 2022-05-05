// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kvm_host.h>

#include <asm/irq_remapping.h>
#include <asm/cpu.h>

#include "lapic.h"
#include "irq.h"
#include "posted_intr.h"
#include "trace.h"
#include "vmx.h"

/*
 * Maintain a per-CPU list of vCPUs that need to be awakened by wakeup_handler()
 * when a WAKEUP_VECTOR interrupted is posted.  vCPUs are added to the list when
 * the vCPU is scheduled out and is blocking (e.g. in HLT) with IRQs enabled.
 * The vCPUs posted interrupt descriptor is updated at the same time to set its
 * notification vector to WAKEUP_VECTOR, so that posted interrupt from devices
 * wake the target vCPUs.  vCPUs are removed from the list and the notification
 * vector is reset when the vCPU is scheduled in.
 */
static DEFINE_PER_CPU(struct list_head, wakeup_vcpus_on_cpu);
/*
 * Protect the per-CPU list with a per-CPU spinlock to handle task migration.
 * When a blocking vCPU is awakened _and_ migrated to a different pCPU, the
 * ->sched_in() path will need to take the vCPU off the list of the _previous_
 * CPU.  IRQs must be disabled when taking this lock, otherwise deadlock will
 * occur if a wakeup IRQ arrives and attempts to acquire the lock.
 */
static DEFINE_PER_CPU(raw_spinlock_t, wakeup_vcpus_on_cpu_lock);

static inline struct pi_desc *vcpu_to_pi_desc(struct kvm_vcpu *vcpu)
{
	return &(to_vmx(vcpu)->pi_desc);
}

static int pi_try_set_control(struct pi_desc *pi_desc, u64 old, u64 new)
{
	/*
	 * PID.ON can be set at any time by a different vCPU or by hardware,
	 * e.g. a device.  PID.control must be written atomically, and the
	 * update must be retried with a fresh snapshot an ON change causes
	 * the cmpxchg to fail.
	 */
	if (cmpxchg64(&pi_desc->control, old, new) != old)
		return -EBUSY;

	return 0;
}

void vmx_vcpu_pi_load(struct kvm_vcpu *vcpu, int cpu)
{
	struct pi_desc *pi_desc = vcpu_to_pi_desc(vcpu);
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct pi_desc old, new;
	unsigned long flags;
	unsigned int dest;

	/*
	 * To simplify hot-plug and dynamic toggling of APICv, keep PI.NDST and
	 * PI.SN up-to-date even if there is no assigned device or if APICv is
	 * deactivated due to a dynamic inhibit bit, e.g. for Hyper-V's SyncIC.
	 */
	if (!enable_apicv || !lapic_in_kernel(vcpu))
		return;

	/*
	 * If the vCPU wasn't on the wakeup list and wasn't migrated, then the
	 * full update can be skipped as neither the vector nor the destination
	 * needs to be changed.
	 */
	if (pi_desc->nv != POSTED_INTR_WAKEUP_VECTOR && vcpu->cpu == cpu) {
		/*
		 * Clear SN if it was set due to being preempted.  Again, do
		 * this even if there is no assigned device for simplicity.
		 */
		if (pi_test_and_clear_sn(pi_desc))
			goto after_clear_sn;
		return;
	}

	local_irq_save(flags);

	/*
	 * If the vCPU was waiting for wakeup, remove the vCPU from the wakeup
	 * list of the _previous_ pCPU, which will not be the same as the
	 * current pCPU if the task was migrated.
	 */
	if (pi_desc->nv == POSTED_INTR_WAKEUP_VECTOR) {
		raw_spin_lock(&per_cpu(wakeup_vcpus_on_cpu_lock, vcpu->cpu));
		list_del(&vmx->pi_wakeup_list);
		raw_spin_unlock(&per_cpu(wakeup_vcpus_on_cpu_lock, vcpu->cpu));
	}

	dest = cpu_physical_id(cpu);
	if (!x2apic_mode)
		dest = (dest << 8) & 0xFF00;

	do {
		old.control = new.control = READ_ONCE(pi_desc->control);

		/*
		 * Clear SN (as above) and refresh the destination APIC ID to
		 * handle task migration (@cpu != vcpu->cpu).
		 */
		new.ndst = dest;
		new.sn = 0;

		/*
		 * Restore the notification vector; in the blocking case, the
		 * descriptor was modified on "put" to use the wakeup vector.
		 */
		new.nv = POSTED_INTR_VECTOR;
	} while (pi_try_set_control(pi_desc, old.control, new.control));

	local_irq_restore(flags);

after_clear_sn:

	/*
	 * Clear SN before reading the bitmap.  The VT-d firmware
	 * writes the bitmap and reads SN atomically (5.2.3 in the
	 * spec), so it doesn't really have a memory barrier that
	 * pairs with this, but we cannot do that and we need one.
	 */
	smp_mb__after_atomic();

	if (!pi_is_pir_empty(pi_desc))
		pi_set_on(pi_desc);
}

static bool vmx_can_use_vtd_pi(struct kvm *kvm)
{
	return irqchip_in_kernel(kvm) && enable_apicv &&
		kvm_arch_has_assigned_device(kvm) &&
		irq_remapping_cap(IRQ_POSTING_CAP);
}

/*
 * Put the vCPU on this pCPU's list of vCPUs that needs to be awakened and set
 * WAKEUP as the notification vector in the PI descriptor.
 */
static void pi_enable_wakeup_handler(struct kvm_vcpu *vcpu)
{
	struct pi_desc *pi_desc = vcpu_to_pi_desc(vcpu);
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct pi_desc old, new;
	unsigned long flags;

	local_irq_save(flags);

	raw_spin_lock(&per_cpu(wakeup_vcpus_on_cpu_lock, vcpu->cpu));
	list_add_tail(&vmx->pi_wakeup_list,
		      &per_cpu(wakeup_vcpus_on_cpu, vcpu->cpu));
	raw_spin_unlock(&per_cpu(wakeup_vcpus_on_cpu_lock, vcpu->cpu));

	WARN(pi_desc->sn, "PI descriptor SN field set before blocking");

	do {
		old.control = new.control = READ_ONCE(pi_desc->control);

		/* set 'NV' to 'wakeup vector' */
		new.nv = POSTED_INTR_WAKEUP_VECTOR;
	} while (pi_try_set_control(pi_desc, old.control, new.control));

	/*
	 * Send a wakeup IPI to this CPU if an interrupt may have been posted
	 * before the notification vector was updated, in which case the IRQ
	 * will arrive on the non-wakeup vector.  An IPI is needed as calling
	 * try_to_wake_up() from ->sched_out() isn't allowed (IRQs are not
	 * enabled until it is safe to call try_to_wake_up() on the task being
	 * scheduled out).
	 */
	if (pi_test_on(&new))
		apic->send_IPI_self(POSTED_INTR_WAKEUP_VECTOR);

	local_irq_restore(flags);
}

void vmx_vcpu_pi_put(struct kvm_vcpu *vcpu)
{
	struct pi_desc *pi_desc = vcpu_to_pi_desc(vcpu);

	if (!vmx_can_use_vtd_pi(vcpu->kvm))
		return;

	if (kvm_vcpu_is_blocking(vcpu) && !vmx_interrupt_blocked(vcpu))
		pi_enable_wakeup_handler(vcpu);

	/*
	 * Set SN when the vCPU is preempted.  Note, the vCPU can both be seen
	 * as blocking and preempted, e.g. if it's preempted between setting
	 * its wait state and manually scheduling out.
	 */
	if (vcpu->preempted)
		pi_set_sn(pi_desc);
}

/*
 * Handler for POSTED_INTERRUPT_WAKEUP_VECTOR.
 */
void pi_wakeup_handler(void)
{
	int cpu = smp_processor_id();
	struct vcpu_vmx *vmx;

	raw_spin_lock(&per_cpu(wakeup_vcpus_on_cpu_lock, cpu));
	list_for_each_entry(vmx, &per_cpu(wakeup_vcpus_on_cpu, cpu),
			    pi_wakeup_list) {

		if (pi_test_on(&vmx->pi_desc))
			kvm_vcpu_wake_up(&vmx->vcpu);
	}
	raw_spin_unlock(&per_cpu(wakeup_vcpus_on_cpu_lock, cpu));
}

void __init pi_init_cpu(int cpu)
{
	INIT_LIST_HEAD(&per_cpu(wakeup_vcpus_on_cpu, cpu));
	raw_spin_lock_init(&per_cpu(wakeup_vcpus_on_cpu_lock, cpu));
}

bool pi_has_pending_interrupt(struct kvm_vcpu *vcpu)
{
	struct pi_desc *pi_desc = vcpu_to_pi_desc(vcpu);

	return pi_test_on(pi_desc) ||
		(pi_test_sn(pi_desc) && !pi_is_pir_empty(pi_desc));
}


/*
 * Bail out of the block loop if the VM has an assigned
 * device, but the blocking vCPU didn't reconfigure the
 * PI.NV to the wakeup vector, i.e. the assigned device
 * came along after the initial check in vmx_vcpu_pi_put().
 */
void vmx_pi_start_assignment(struct kvm *kvm)
{
	if (!irq_remapping_cap(IRQ_POSTING_CAP))
		return;

	kvm_make_all_cpus_request(kvm, KVM_REQ_UNBLOCK);
}

/*
 * vmx_pi_update_irte - set IRTE for Posted-Interrupts
 *
 * @kvm: kvm
 * @host_irq: host irq of the interrupt
 * @guest_irq: gsi of the interrupt
 * @set: set or unset PI
 * returns 0 on success, < 0 on failure
 */
int vmx_pi_update_irte(struct kvm *kvm, unsigned int host_irq,
		       uint32_t guest_irq, bool set)
{
	struct kvm_kernel_irq_routing_entry *e;
	struct kvm_irq_routing_table *irq_rt;
	struct kvm_lapic_irq irq;
	struct kvm_vcpu *vcpu;
	struct vcpu_data vcpu_info;
	int idx, ret = 0;

	if (!vmx_can_use_vtd_pi(kvm))
		return 0;

	idx = srcu_read_lock(&kvm->irq_srcu);
	irq_rt = srcu_dereference(kvm->irq_routing, &kvm->irq_srcu);
	if (guest_irq >= irq_rt->nr_rt_entries ||
	    hlist_empty(&irq_rt->map[guest_irq])) {
		pr_warn_once("no route for guest_irq %u/%u (broken user space?)\n",
			     guest_irq, irq_rt->nr_rt_entries);
		goto out;
	}

	hlist_for_each_entry(e, &irq_rt->map[guest_irq], link) {
		if (e->type != KVM_IRQ_ROUTING_MSI)
			continue;
		/*
		 * VT-d PI cannot support posting multicast/broadcast
		 * interrupts to a vCPU, we still use interrupt remapping
		 * for these kind of interrupts.
		 *
		 * For lowest-priority interrupts, we only support
		 * those with single CPU as the destination, e.g. user
		 * configures the interrupts via /proc/irq or uses
		 * irqbalance to make the interrupts single-CPU.
		 *
		 * We will support full lowest-priority interrupt later.
		 *
		 * In addition, we can only inject generic interrupts using
		 * the PI mechanism, refuse to route others through it.
		 */

		kvm_set_msi_irq(kvm, e, &irq);
		if (!kvm_intr_is_single_vcpu(kvm, &irq, &vcpu) ||
		    !kvm_irq_is_postable(&irq)) {
			/*
			 * Make sure the IRTE is in remapped mode if
			 * we don't handle it in posted mode.
			 */
			ret = irq_set_vcpu_affinity(host_irq, NULL);
			if (ret < 0) {
				printk(KERN_INFO
				   "failed to back to remapped mode, irq: %u\n",
				   host_irq);
				goto out;
			}

			continue;
		}

		vcpu_info.pi_desc_addr = __pa(&to_vmx(vcpu)->pi_desc);
		vcpu_info.vector = irq.vector;

		trace_kvm_pi_irte_update(host_irq, vcpu->vcpu_id, e->gsi,
				vcpu_info.vector, vcpu_info.pi_desc_addr, set);

		if (set)
			ret = irq_set_vcpu_affinity(host_irq, &vcpu_info);
		else
			ret = irq_set_vcpu_affinity(host_irq, NULL);

		if (ret < 0) {
			printk(KERN_INFO "%s: failed to update PI IRTE\n",
					__func__);
			goto out;
		}
	}

	ret = 0;
out:
	srcu_read_unlock(&kvm->irq_srcu, idx);
	return ret;
}
