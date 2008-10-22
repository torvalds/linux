#include <linux/hardirq.h>

#include <xen/interface/xen.h>
#include <xen/interface/sched.h>
#include <xen/interface/vcpu.h>

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

static void __init __xen_init_IRQ(void)
{
	int i;

	/* Create identity vector->irq map */
	for(i = 0; i < NR_VECTORS; i++) {
		int cpu;

		for_each_possible_cpu(cpu)
			per_cpu(vector_irq, cpu)[i] = i;
	}

	xen_init_IRQ();
}

static unsigned long xen_save_fl(void)
{
	struct vcpu_info *vcpu;
	unsigned long flags;

	vcpu = x86_read_percpu(xen_vcpu);

	/* flag has opposite sense of mask */
	flags = !vcpu->evtchn_upcall_mask;

	/* convert to IF type flag
	   -0 -> 0x00000000
	   -1 -> 0xffffffff
	*/
	return (-flags) & X86_EFLAGS_IF;
}

static void xen_restore_fl(unsigned long flags)
{
	struct vcpu_info *vcpu;

	/* convert from IF type flag */
	flags = !(flags & X86_EFLAGS_IF);

	/* There's a one instruction preempt window here.  We need to
	   make sure we're don't switch CPUs between getting the vcpu
	   pointer and updating the mask. */
	preempt_disable();
	vcpu = x86_read_percpu(xen_vcpu);
	vcpu->evtchn_upcall_mask = flags;
	preempt_enable_no_resched();

	/* Doesn't matter if we get preempted here, because any
	   pending event will get dealt with anyway. */

	if (flags == 0) {
		preempt_check_resched();
		barrier(); /* unmask then check (avoid races) */
		if (unlikely(vcpu->evtchn_upcall_pending))
			xen_force_evtchn_callback();
	}
}

static void xen_irq_disable(void)
{
	/* There's a one instruction preempt window here.  We need to
	   make sure we're don't switch CPUs between getting the vcpu
	   pointer and updating the mask. */
	preempt_disable();
	x86_read_percpu(xen_vcpu)->evtchn_upcall_mask = 1;
	preempt_enable_no_resched();
}

static void xen_irq_enable(void)
{
	struct vcpu_info *vcpu;

	/* We don't need to worry about being preempted here, since
	   either a) interrupts are disabled, so no preemption, or b)
	   the caller is confused and is trying to re-enable interrupts
	   on an indeterminate processor. */

	vcpu = x86_read_percpu(xen_vcpu);
	vcpu->evtchn_upcall_mask = 0;

	/* Doesn't matter if we get preempted here, because any
	   pending event will get dealt with anyway. */

	barrier(); /* unmask then check (avoid races) */
	if (unlikely(vcpu->evtchn_upcall_pending))
		xen_force_evtchn_callback();
}

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

static const struct pv_irq_ops xen_irq_ops __initdata = {
	.init_IRQ = __xen_init_IRQ,
	.save_fl = xen_save_fl,
	.restore_fl = xen_restore_fl,
	.irq_disable = xen_irq_disable,
	.irq_enable = xen_irq_enable,
	.safe_halt = xen_safe_halt,
	.halt = xen_halt,
#ifdef CONFIG_X86_64
	.adjust_exception_frame = xen_adjust_exception_frame,
#endif
};

void __init xen_init_irq_ops()
{
	pv_irq_ops = xen_irq_ops;
}
