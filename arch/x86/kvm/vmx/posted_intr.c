// SPDX-License-Identifier: GPL-2.0-only
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kvm_host.h>
#include <linux/kvm_irqfd.h>

#include <asm/irq_remapping.h>
#include <asm/cpu.h>

#include "lapic.h"
#include "irq.h"
#include "posted_intr.h"
#include "trace.h"
#include "vmx.h"
#include "tdx.h"

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

#define PI_LOCK_SCHED_OUT SINGLE_DEPTH_NESTING

static struct pi_desc *vcpu_to_pi_desc(struct kvm_vcpu *vcpu)
{
	return &(to_vt(vcpu)->pi_desc);
}

static int pi_try_set_control(struct pi_desc *pi_desc, u64 *pold, u64 new)
{
	/*
	 * PID.ON can be set at any time by a different vCPU or by hardware,
	 * e.g. a device.  PID.control must be written atomically, and the
	 * update must be retried with a fresh snapshot an ON change causes
	 * the cmpxchg to fail.
	 */
	if (!try_cmpxchg64(&pi_desc->control, pold, new))
		return -EBUSY;

	return 0;
}

void vmx_vcpu_pi_load(struct kvm_vcpu *vcpu, int cpu)
{
	struct pi_desc *pi_desc = vcpu_to_pi_desc(vcpu);
	struct vcpu_vt *vt = to_vt(vcpu);
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
	 * needs to be changed.  Clear SN even if there is no assigned device,
	 * again for simplicity.
	 */
	if (pi_desc->nv != POSTED_INTR_WAKEUP_VECTOR && vcpu->cpu == cpu) {
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
		raw_spinlock_t *spinlock = &per_cpu(wakeup_vcpus_on_cpu_lock, vcpu->cpu);

		/*
		 * In addition to taking the wakeup lock for the regular/IRQ
		 * context, tell lockdep it is being taken for the "sched out"
		 * context as well.  vCPU loads happens in task context, and
		 * this is taking the lock of the *previous* CPU, i.e. can race
		 * with both the scheduler and the wakeup handler.
		 */
		raw_spin_lock(spinlock);
		spin_acquire(&spinlock->dep_map, PI_LOCK_SCHED_OUT, 0, _RET_IP_);
		list_del(&vt->pi_wakeup_list);
		spin_release(&spinlock->dep_map, _RET_IP_);
		raw_spin_unlock(spinlock);
	}

	dest = cpu_physical_id(cpu);
	if (!x2apic_mode)
		dest = (dest << 8) & 0xFF00;

	old.control = READ_ONCE(pi_desc->control);
	do {
		new.control = old.control;

		/*
		 * Clear SN (as above) and refresh the destination APIC ID to
		 * handle task migration (@cpu != vcpu->cpu).
		 */
		new.ndst = dest;
		__pi_clear_sn(&new);

		/*
		 * Restore the notification vector; in the blocking case, the
		 * descriptor was modified on "put" to use the wakeup vector.
		 */
		new.nv = POSTED_INTR_VECTOR;
	} while (pi_try_set_control(pi_desc, &old.control, new.control));

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
	/*
	 * Note, reading the number of possible bypass IRQs can race with a
	 * bypass IRQ being attached to the VM.  vmx_pi_start_bypass() ensures
	 * blockng vCPUs will see an elevated count or get KVM_REQ_UNBLOCK.
	 */
	return irqchip_in_kernel(kvm) && kvm_arch_has_irq_bypass() &&
	       READ_ONCE(kvm->arch.nr_possible_bypass_irqs);
}

/*
 * Put the vCPU on this pCPU's list of vCPUs that needs to be awakened and set
 * WAKEUP as the notification vector in the PI descriptor.
 */
static void pi_enable_wakeup_handler(struct kvm_vcpu *vcpu)
{
	struct pi_desc *pi_desc = vcpu_to_pi_desc(vcpu);
	struct vcpu_vt *vt = to_vt(vcpu);
	struct pi_desc old, new;

	lockdep_assert_irqs_disabled();

	/*
	 * Acquire the wakeup lock using the "sched out" context to workaround
	 * a lockdep false positive.  When this is called, schedule() holds
	 * various per-CPU scheduler locks.  When the wakeup handler runs, it
	 * holds this CPU's wakeup lock while calling try_to_wake_up(), which
	 * can eventually take the aforementioned scheduler locks, which causes
	 * lockdep to assume there is deadlock.
	 *
	 * Deadlock can't actually occur because IRQs are disabled for the
	 * entirety of the sched_out critical section, i.e. the wakeup handler
	 * can't run while the scheduler locks are held.
	 */
	raw_spin_lock_nested(&per_cpu(wakeup_vcpus_on_cpu_lock, vcpu->cpu),
			     PI_LOCK_SCHED_OUT);
	list_add_tail(&vt->pi_wakeup_list,
		      &per_cpu(wakeup_vcpus_on_cpu, vcpu->cpu));
	raw_spin_unlock(&per_cpu(wakeup_vcpus_on_cpu_lock, vcpu->cpu));

	WARN(pi_test_sn(pi_desc), "PI descriptor SN field set before blocking");

	old.control = READ_ONCE(pi_desc->control);
	do {
		/* set 'NV' to 'wakeup vector' */
		new.control = old.control;
		new.nv = POSTED_INTR_WAKEUP_VECTOR;
	} while (pi_try_set_control(pi_desc, &old.control, new.control));

	/*
	 * Send a wakeup IPI to this CPU if an interrupt may have been posted
	 * before the notification vector was updated, in which case the IRQ
	 * will arrive on the non-wakeup vector.  An IPI is needed as calling
	 * try_to_wake_up() from ->sched_out() isn't allowed (IRQs are not
	 * enabled until it is safe to call try_to_wake_up() on the task being
	 * scheduled out).
	 */
	if (pi_test_on(&new))
		__apic_send_IPI_self(POSTED_INTR_WAKEUP_VECTOR);
}

static bool vmx_needs_pi_wakeup(struct kvm_vcpu *vcpu)
{
	/*
	 * The default posted interrupt vector does nothing when
	 * invoked outside guest mode.   Return whether a blocked vCPU
	 * can be the target of posted interrupts, as is the case when
	 * using either IPI virtualization or VT-d PI, so that the
	 * notification vector is switched to the one that calls
	 * back to the pi_wakeup_handler() function.
	 */
	return (vmx_can_use_ipiv(vcpu) && !is_td_vcpu(vcpu)) ||
		vmx_can_use_vtd_pi(vcpu->kvm);
}

void vmx_vcpu_pi_put(struct kvm_vcpu *vcpu)
{
	struct pi_desc *pi_desc = vcpu_to_pi_desc(vcpu);

	if (!vmx_needs_pi_wakeup(vcpu))
		return;

	/*
	 * If the vCPU is blocking with IRQs enabled and ISN'T being preempted,
	 * enable the wakeup handler so that notification IRQ wakes the vCPU as
	 * expected.  There is no need to enable the wakeup handler if the vCPU
	 * is preempted between setting its wait state and manually scheduling
	 * out, as the task is still runnable, i.e. doesn't need a wake event
	 * from KVM to be scheduled in.
	 *
	 * If the wakeup handler isn't being enabled, Suppress Notifications as
	 * the cost of propagating PIR.IRR to PID.ON is negligible compared to
	 * the cost of a spurious IRQ, and vCPU put/load is a slow path.
	 */
	if (!vcpu->preempted && kvm_vcpu_is_blocking(vcpu) &&
	    ((is_td_vcpu(vcpu) && tdx_interrupt_allowed(vcpu)) ||
	     (!is_td_vcpu(vcpu) && !vmx_interrupt_blocked(vcpu))))
		pi_enable_wakeup_handler(vcpu);
	else
		pi_set_sn(pi_desc);
}

/*
 * Handler for POSTED_INTERRUPT_WAKEUP_VECTOR.
 */
void pi_wakeup_handler(void)
{
	int cpu = smp_processor_id();
	struct list_head *wakeup_list = &per_cpu(wakeup_vcpus_on_cpu, cpu);
	raw_spinlock_t *spinlock = &per_cpu(wakeup_vcpus_on_cpu_lock, cpu);
	struct vcpu_vt *vt;

	raw_spin_lock(spinlock);
	list_for_each_entry(vt, wakeup_list, pi_wakeup_list) {

		if (pi_test_on(&vt->pi_desc))
			kvm_vcpu_wake_up(vt_to_vcpu(vt));
	}
	raw_spin_unlock(spinlock);
}

void __init pi_init_cpu(int cpu)
{
	INIT_LIST_HEAD(&per_cpu(wakeup_vcpus_on_cpu, cpu));
	raw_spin_lock_init(&per_cpu(wakeup_vcpus_on_cpu_lock, cpu));
}

void pi_apicv_pre_state_restore(struct kvm_vcpu *vcpu)
{
	struct pi_desc *pi = vcpu_to_pi_desc(vcpu);

	pi_clear_on(pi);
	memset(pi->pir, 0, sizeof(pi->pir));
}

bool pi_has_pending_interrupt(struct kvm_vcpu *vcpu)
{
	struct pi_desc *pi_desc = vcpu_to_pi_desc(vcpu);

	return pi_test_on(pi_desc) ||
		(pi_test_sn(pi_desc) && !pi_is_pir_empty(pi_desc));
}


/*
 * Kick all vCPUs when the first possible bypass IRQ is attached to a VM, as
 * blocking vCPUs may scheduled out without reconfiguring PID.NV to the wakeup
 * vector, i.e. if the bypass IRQ came along after vmx_vcpu_pi_put().
 */
void vmx_pi_start_bypass(struct kvm *kvm)
{
	if (WARN_ON_ONCE(!vmx_can_use_vtd_pi(kvm)))
		return;

	kvm_make_all_cpus_request(kvm, KVM_REQ_UNBLOCK);
}

int vmx_pi_update_irte(struct kvm_kernel_irqfd *irqfd, struct kvm *kvm,
		       unsigned int host_irq, uint32_t guest_irq,
		       struct kvm_vcpu *vcpu, u32 vector)
{
	if (vcpu) {
		struct intel_iommu_pi_data pi_data = {
			.pi_desc_addr = __pa(vcpu_to_pi_desc(vcpu)),
			.vector = vector,
		};

		return irq_set_vcpu_affinity(host_irq, &pi_data);
	} else {
		return irq_set_vcpu_affinity(host_irq, NULL);
	}
}
