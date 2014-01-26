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
void xen_force_evtchn_callback(void)
{
	(void)HYPERVISOR_xen_version(0, NULL);
}

static unsigned long xen_save_fl(void)
{
	struct vcpu_info *vcpu;
	unsigned long flags;

	vcpu = this_cpu_read(xen_vcpu);

	/* flag has opposite sense of mask */
	flags = !vcpu->evtchn_upcall_mask;

	/* convert to IF type flag
	   -0 -> 0x00000000
	   -1 -> 0xffffffff
	*/
	return (-flags) & X86_EFLAGS_IF;
}
PV_CALLEE_SAVE_REGS_THUNK(xen_save_fl);

static void xen_restore_fl(unsigned long flags)
{
	struct vcpu_info *vcpu;

	/* convert from IF type flag */
	flags = !(flags & X86_EFLAGS_IF);

	/* See xen_irq_enable() for why preemption must be disabled. */
	preempt_disable();
	vcpu = this_cpu_read(xen_vcpu);
	vcpu->evtchn_upcall_mask = flags;

	if (flags == 0) {
		barrier(); /* unmask then check (avoid races) */
		if (unlikely(vcpu->evtchn_upcall_pending))
			xen_force_evtchn_callback();
		preempt_enable();
	} else
		preempt_enable_no_resched();
}
PV_CALLEE_SAVE_REGS_THUNK(xen_restore_fl);

static void xen_irq_disable(void)
{
	/* There's a one instruction preempt window here.  We need to
	   make sure we're don't switch CPUs between getting the vcpu
	   pointer and updating the mask. */
	preempt_disable();
	this_cpu_read(xen_vcpu)->evtchn_upcall_mask = 1;
	preempt_enable_no_resched();
}
PV_CALLEE_SAVE_REGS_THUNK(xen_irq_disable);

static void xen_irq_enable(void)
{
	struct vcpu_info *vcpu;

	/*
	 * We may be preempted as soon as vcpu->evtchn_upcall_mask is
	 * cleared, so disable preemption to ensure we check for
	 * events on the VCPU we are still running on.
	 */
	preempt_disable();

	vcpu = this_cpu_read(xen_vcpu);
	vcpu->evtchn_upcall_mask = 0;

	/* Doesn't matter if we get preempted here, because any
	   pending event will get dealt with anyway. */

	barrier(); /* unmask then check (avoid races) */
	if (unlikely(vcpu->evtchn_upcall_pending))
		xen_force_evtchn_callback();

	preempt_enable();
}
PV_CALLEE_SAVE_REGS_THUNK(xen_irq_enable);

static void xen_safe_halt(void)
{
	/* Blocking includes an implicit local_irq_enable(). */
	if (HYPERVISOR_sched_op(SCHEDOP_block, NULL) != 0)
		BUG();
}

static void xen_halt(void)
{
	if (irqs_disabled())
		HYPERVISOR_vcpu_op(VCPUOP_down, smp_processor_id(), NULL);
	else
		xen_safe_halt();
}

static const struct pv_irq_ops xen_irq_ops __initconst = {
	.save_fl = PV_CALLEE_SAVE(xen_save_fl),
	.restore_fl = PV_CALLEE_SAVE(xen_restore_fl),
	.irq_disable = PV_CALLEE_SAVE(xen_irq_disable),
	.irq_enable = PV_CALLEE_SAVE(xen_irq_enable),

	.safe_halt = xen_safe_halt,
	.halt = xen_halt,
#ifdef CONFIG_X86_64
	.adjust_exception_frame = xen_adjust_exception_frame,
#endif
};

void __init xen_init_irq_ops(void)
{
	/* For PVH we use default pv_irq_ops settings. */
	if (!xen_feature(XENFEAT_hvm_callback_vector))
		pv_irq_ops = xen_irq_ops;
	x86_init.irqs.intr_init = xen_init_IRQ;
}
