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

static const typeof(pv_ops) xen_irq_ops __initconst = {
	.irq = {
		/* Initial interrupt flag handling only called while interrupts off. */
		.save_fl = __PV_IS_CALLEE_SAVE(paravirt_ret0),
		.irq_disable = __PV_IS_CALLEE_SAVE(paravirt_nop),
		.irq_enable = __PV_IS_CALLEE_SAVE(paravirt_BUG),

		.safe_halt = xen_safe_halt,
		.halt = xen_halt,
	},
};

void __init xen_init_irq_ops(void)
{
	pv_ops.irq = xen_irq_ops.irq;
	x86_init.irqs.intr_init = xen_init_IRQ;
}
