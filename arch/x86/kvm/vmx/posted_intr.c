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
static DEFINE_PER_CPU(struct list_head, blocked_vcpu_on_cpu);
/*
 * Protect the per-CPU list with a per-CPU spinlock to handle task migration.
 * When a blocking vCPU is awakened _and_ migrated to a different pCPU, the
 * ->sched_in() path will need to take the vCPU off the list of the _previous_
 * CPU.  IRQs must be disabled when taking this lock, otherwise deadlock will
 * occur if a wakeup IRQ arrives and attempts to acquire the lock.
 */
static DEFINE_PER_CPU(spinlock_t, blocked_vcpu_on_cpu_lock);

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
	struct pi_desc old, new;
	unsigned int dest;

	/*
	 * To simplify hot-plug and dynamic toggling of APICv, keep PI.NDST and
	 * PI.SN up-to-date even if there is no assigned device or if APICv is
	 * deactivated due to a dynamic inhibit bit, e.g. for Hyper-V's SyncIC.
	 */
	if (!enable_apicv || !lapic_in_kernel(vcpu))
		return;

	/* Nothing to do if PI.SN and PI.NDST both have the desired value. */
	if (!pi_test_sn(pi_desc) && vcpu->cpu == cpu)
		return;

	/*
	 * If the 'nv' field is POSTED_INTR_WAKEUP_VECTOR, do not change
	 * PI.NDST: pi_post_block is the one expected to change PID.NDST and the
	 * wakeup handler expects the vCPU to be on the blocked_vcpu_list that
	 * matches PI.NDST. Otherwise, a vcpu may not be able to be woken up
	 * correctly.
	 */
	if (pi_desc->nv == POSTED_INTR_WAKEUP_VECTOR || vcpu->cpu == cpu) {
		pi_clear_sn(pi_desc);
		goto after_clear_sn;
	}

	/* The full case.  Set the new destination and clear SN. */
	dest = cpu_physical_id(cpu);
	if (!x2apic_mode)
		dest = (dest << 8) & 0xFF00;

	do {
		old.control = new.control = READ_ONCE(pi_desc->control);

		new.ndst = dest;
		new.sn = 0;
	} while (pi_try_set_control(pi_desc, old.control, new.control));

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

void vmx_vcpu_pi_put(struct kvm_vcpu *vcpu)
{
	struct pi_desc *pi_desc = vcpu_to_pi_desc(vcpu);

	if (!vmx_can_use_vtd_pi(vcpu->kvm))
		return;

	/* Set SN when the vCPU is preempted */
	if (vcpu->preempted)
		pi_set_sn(pi_desc);
}

static void __pi_post_block(struct kvm_vcpu *vcpu)
{
	struct pi_desc *pi_desc = vcpu_to_pi_desc(vcpu);
	struct pi_desc old, new;
	unsigned int dest;

	/*
	 * Remove the vCPU from the wakeup list of the _previous_ pCPU, which
	 * will not be the same as the current pCPU if the task was migrated.
	 */
	spin_lock(&per_cpu(blocked_vcpu_on_cpu_lock, vcpu->pre_pcpu));
	list_del(&vcpu->blocked_vcpu_list);
	spin_unlock(&per_cpu(blocked_vcpu_on_cpu_lock, vcpu->pre_pcpu));

	dest = cpu_physical_id(vcpu->cpu);
	if (!x2apic_mode)
		dest = (dest << 8) & 0xFF00;

	WARN(pi_desc->nv != POSTED_INTR_WAKEUP_VECTOR,
	     "Wakeup handler not enabled while the vCPU was blocking");

	do {
		old.control = new.control = READ_ONCE(pi_desc->control);

		new.ndst = dest;

		/* set 'NV' to 'notification vector' */
		new.nv = POSTED_INTR_VECTOR;
	} while (pi_try_set_control(pi_desc, old.control, new.control));

	vcpu->pre_pcpu = -1;
}

/*
 * This routine does the following things for vCPU which is going
 * to be blocked if VT-d PI is enabled.
 * - Store the vCPU to the wakeup list, so when interrupts happen
 *   we can find the right vCPU to wake up.
 * - Change the Posted-interrupt descriptor as below:
 *      'NV' <-- POSTED_INTR_WAKEUP_VECTOR
 * - If 'ON' is set during this process, which means at least one
 *   interrupt is posted for this vCPU, we cannot block it, in
 *   this case, return 1, otherwise, return 0.
 *
 */
int pi_pre_block(struct kvm_vcpu *vcpu)
{
	struct pi_desc old, new;
	struct pi_desc *pi_desc = vcpu_to_pi_desc(vcpu);
	unsigned long flags;

	if (!vmx_can_use_vtd_pi(vcpu->kvm) ||
	    vmx_interrupt_blocked(vcpu))
		return 0;

	local_irq_save(flags);

	vcpu->pre_pcpu = vcpu->cpu;
	spin_lock(&per_cpu(blocked_vcpu_on_cpu_lock, vcpu->cpu));
	list_add_tail(&vcpu->blocked_vcpu_list,
		      &per_cpu(blocked_vcpu_on_cpu, vcpu->cpu));
	spin_unlock(&per_cpu(blocked_vcpu_on_cpu_lock, vcpu->cpu));

	WARN(pi_desc->sn == 1,
	     "Posted Interrupt Suppress Notification set before blocking");

	do {
		old.control = new.control = READ_ONCE(pi_desc->control);

		/* set 'NV' to 'wakeup vector' */
		new.nv = POSTED_INTR_WAKEUP_VECTOR;
	} while (pi_try_set_control(pi_desc, old.control, new.control));

	/* We should not block the vCPU if an interrupt is posted for it.  */
	if (pi_test_on(pi_desc))
		__pi_post_block(vcpu);

	local_irq_restore(flags);
	return (vcpu->pre_pcpu == -1);
}

void pi_post_block(struct kvm_vcpu *vcpu)
{
	unsigned long flags;

	if (vcpu->pre_pcpu == -1)
		return;

	local_irq_save(flags);
	__pi_post_block(vcpu);
	local_irq_restore(flags);
}

/*
 * Handler for POSTED_INTERRUPT_WAKEUP_VECTOR.
 */
void pi_wakeup_handler(void)
{
	struct kvm_vcpu *vcpu;
	int cpu = smp_processor_id();

	spin_lock(&per_cpu(blocked_vcpu_on_cpu_lock, cpu));
	list_for_each_entry(vcpu, &per_cpu(blocked_vcpu_on_cpu, cpu),
			blocked_vcpu_list) {
		struct pi_desc *pi_desc = vcpu_to_pi_desc(vcpu);

		if (pi_test_on(pi_desc))
			kvm_vcpu_kick(vcpu);
	}
	spin_unlock(&per_cpu(blocked_vcpu_on_cpu_lock, cpu));
}

void __init pi_init_cpu(int cpu)
{
	INIT_LIST_HEAD(&per_cpu(blocked_vcpu_on_cpu, cpu));
	spin_lock_init(&per_cpu(blocked_vcpu_on_cpu_lock, cpu));
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
 * came along after the initial check in pi_pre_block().
 */
void vmx_pi_start_assignment(struct kvm *kvm)
{
	if (!irq_remapping_cap(IRQ_POSTING_CAP))
		return;

	kvm_make_all_cpus_request(kvm, KVM_REQ_UNBLOCK);
}

/*
 * pi_update_irte - set IRTE for Posted-Interrupts
 *
 * @kvm: kvm
 * @host_irq: host irq of the interrupt
 * @guest_irq: gsi of the interrupt
 * @set: set or unset PI
 * returns 0 on success, < 0 on failure
 */
int pi_update_irte(struct kvm *kvm, unsigned int host_irq, uint32_t guest_irq,
		   bool set)
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
