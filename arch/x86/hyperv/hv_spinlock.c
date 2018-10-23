// SPDX-License-Identifier: GPL-2.0

/*
 * Hyper-V specific spinlock code.
 *
 * Copyright (C) 2018, Intel, Inc.
 *
 * Author : Yi Sun <yi.y.sun@intel.com>
 */

#define pr_fmt(fmt) "Hyper-V: " fmt

#include <linux/spinlock.h>

#include <asm/mshyperv.h>
#include <asm/paravirt.h>
#include <asm/apic.h>

static bool __initdata hv_pvspin = true;

static void hv_qlock_kick(int cpu)
{
	apic->send_IPI(cpu, X86_PLATFORM_IPI_VECTOR);
}

static void hv_qlock_wait(u8 *byte, u8 val)
{
	unsigned long msr_val;
	unsigned long flags;

	if (in_nmi())
		return;

	/*
	 * Reading HV_X64_MSR_GUEST_IDLE MSR tells the hypervisor that the
	 * vCPU can be put into 'idle' state. This 'idle' state is
	 * terminated by an IPI, usually from hv_qlock_kick(), even if
	 * interrupts are disabled on the vCPU.
	 *
	 * To prevent a race against the unlock path it is required to
	 * disable interrupts before accessing the HV_X64_MSR_GUEST_IDLE
	 * MSR. Otherwise, if the IPI from hv_qlock_kick() arrives between
	 * the lock value check and the rdmsrl() then the vCPU might be put
	 * into 'idle' state by the hypervisor and kept in that state for
	 * an unspecified amount of time.
	 */
	local_irq_save(flags);
	/*
	 * Only issue the rdmsrl() when the lock state has not changed.
	 */
	if (READ_ONCE(*byte) == val)
		rdmsrl(HV_X64_MSR_GUEST_IDLE, msr_val);
	local_irq_restore(flags);
}

/*
 * Hyper-V does not support this so far.
 */
bool hv_vcpu_is_preempted(int vcpu)
{
	return false;
}
PV_CALLEE_SAVE_REGS_THUNK(hv_vcpu_is_preempted);

void __init hv_init_spinlocks(void)
{
	if (!hv_pvspin || !apic ||
	    !(ms_hyperv.hints & HV_X64_CLUSTER_IPI_RECOMMENDED) ||
	    !(ms_hyperv.features & HV_X64_MSR_GUEST_IDLE_AVAILABLE)) {
		pr_info("PV spinlocks disabled\n");
		return;
	}
	pr_info("PV spinlocks enabled\n");

	__pv_init_lock_hash();
	pv_ops.lock.queued_spin_lock_slowpath = __pv_queued_spin_lock_slowpath;
	pv_ops.lock.queued_spin_unlock = PV_CALLEE_SAVE(__pv_queued_spin_unlock);
	pv_ops.lock.wait = hv_qlock_wait;
	pv_ops.lock.kick = hv_qlock_kick;
	pv_ops.lock.vcpu_is_preempted = PV_CALLEE_SAVE(hv_vcpu_is_preempted);
}

static __init int hv_parse_nopvspin(char *arg)
{
	hv_pvspin = false;
	return 0;
}
early_param("hv_nopvspin", hv_parse_nopvspin);
