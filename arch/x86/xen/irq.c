// SPDX-License-Identifier: GPL-2.0
#include <linux/hardirq.h>

#include <asm/x86_init.h>

#include <xen/interface/xen.h>
#include <xen/interface/sched.h>
#include <xen/interface/vcpu.h>
#include <xen/features.h>
#include <xen/events.h>

#include <asm/xen/hypercall.h>
#include <asm/xen/hypervisor.h>

#include "xen-ops.h"

/*
 * Force a proper event-channel callback from Xen after clearing the
 * callback mask. We do this in a very simple manner, by making a call
 * down into Xen. The pending flag will be checked by Xen on return.
 */
noinstr void xen_force_evtchn_callback(void)
{
	(void)HYPERVISOR_xen_version(0, NULL);
}

static noinstr void xen_safe_halt(void)
{
	/* Blocking includes an implicit local_irq_enable(). */
	if (HYPERVISOR_sched_op(SCHEDOP_block, NULL) != 0)
		BUG();
}

static void xen_halt(void)
{
	if (irqs_disabled())
		HYPERVISOR_vcpu_op(VCPUOP_down,
				   xen_vcpu_nr(smp_processor_id()), NULL);
	else
		xen_safe_halt();
}

void __init xen_init_irq_ops(void)
{
	/* Initial interrupt flag handling only called while interrupts off. */
	pv_ops.irq.save_fl = __PV_IS_CALLEE_SAVE(paravirt_ret0);
	pv_ops.irq.irq_disable = __PV_IS_CALLEE_SAVE(paravirt_nop);
	pv_ops.irq.irq_enable = __PV_IS_CALLEE_SAVE(BUG_func);
	pv_ops.irq.safe_halt = xen_safe_halt;
	pv_ops.irq.halt = xen_halt;

	x86_init.irqs.intr_init = xen_init_IRQ;
}
