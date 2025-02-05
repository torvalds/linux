// SPDX-License-Identifier: GPL-2.0
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>

#include <xen/events.h>

#include <xen/hvc-console.h>
#include "xen-ops.h"

static DEFINE_PER_CPU(struct xen_common_irq, xen_resched_irq) = { .irq = -1 };
static DEFINE_PER_CPU(struct xen_common_irq, xen_callfunc_irq) = { .irq = -1 };
static DEFINE_PER_CPU(struct xen_common_irq, xen_callfuncsingle_irq) = { .irq = -1 };
static DEFINE_PER_CPU(struct xen_common_irq, xen_debug_irq) = { .irq = -1 };

static irqreturn_t xen_call_function_interrupt(int irq, void *dev_id);
static irqreturn_t xen_call_function_single_interrupt(int irq, void *dev_id);

/*
 * Reschedule call back.
 */
static irqreturn_t xen_reschedule_interrupt(int irq, void *dev_id)
{
	inc_irq_stat(irq_resched_count);
	scheduler_ipi();

	return IRQ_HANDLED;
}

void xen_smp_intr_free(unsigned int cpu)
{
	kfree(per_cpu(xen_resched_irq, cpu).name);
	per_cpu(xen_resched_irq, cpu).name = NULL;
	if (per_cpu(xen_resched_irq, cpu).irq >= 0) {
		unbind_from_irqhandler(per_cpu(xen_resched_irq, cpu).irq, NULL);
		per_cpu(xen_resched_irq, cpu).irq = -1;
	}
	kfree(per_cpu(xen_callfunc_irq, cpu).name);
	per_cpu(xen_callfunc_irq, cpu).name = NULL;
	if (per_cpu(xen_callfunc_irq, cpu).irq >= 0) {
		unbind_from_irqhandler(per_cpu(xen_callfunc_irq, cpu).irq, NULL);
		per_cpu(xen_callfunc_irq, cpu).irq = -1;
	}
	kfree(per_cpu(xen_debug_irq, cpu).name);
	per_cpu(xen_debug_irq, cpu).name = NULL;
	if (per_cpu(xen_debug_irq, cpu).irq >= 0) {
		unbind_from_irqhandler(per_cpu(xen_debug_irq, cpu).irq, NULL);
		per_cpu(xen_debug_irq, cpu).irq = -1;
	}
	kfree(per_cpu(xen_callfuncsingle_irq, cpu).name);
	per_cpu(xen_callfuncsingle_irq, cpu).name = NULL;
	if (per_cpu(xen_callfuncsingle_irq, cpu).irq >= 0) {
		unbind_from_irqhandler(per_cpu(xen_callfuncsingle_irq, cpu).irq,
				       NULL);
		per_cpu(xen_callfuncsingle_irq, cpu).irq = -1;
	}
}

int xen_smp_intr_init(unsigned int cpu)
{
	int rc;
	char *resched_name, *callfunc_name, *debug_name;

	resched_name = kasprintf(GFP_KERNEL, "resched%d", cpu);
	if (!resched_name)
		goto fail_mem;
	per_cpu(xen_resched_irq, cpu).name = resched_name;
	rc = bind_ipi_to_irqhandler(XEN_RESCHEDULE_VECTOR,
				    cpu,
				    xen_reschedule_interrupt,
				    IRQF_PERCPU|IRQF_NOBALANCING,
				    resched_name,
				    NULL);
	if (rc < 0)
		goto fail;
	per_cpu(xen_resched_irq, cpu).irq = rc;

	callfunc_name = kasprintf(GFP_KERNEL, "callfunc%d", cpu);
	if (!callfunc_name)
		goto fail_mem;
	per_cpu(xen_callfunc_irq, cpu).name = callfunc_name;
	rc = bind_ipi_to_irqhandler(XEN_CALL_FUNCTION_VECTOR,
				    cpu,
				    xen_call_function_interrupt,
				    IRQF_PERCPU|IRQF_NOBALANCING,
				    callfunc_name,
				    NULL);
	if (rc < 0)
		goto fail;
	per_cpu(xen_callfunc_irq, cpu).irq = rc;

	if (!xen_fifo_events) {
		debug_name = kasprintf(GFP_KERNEL, "debug%d", cpu);
		if (!debug_name)
			goto fail_mem;

		per_cpu(xen_debug_irq, cpu).name = debug_name;
		rc = bind_virq_to_irqhandler(VIRQ_DEBUG, cpu,
					     xen_debug_interrupt,
					     IRQF_PERCPU | IRQF_NOBALANCING,
					     debug_name, NULL);
		if (rc < 0)
			goto fail;
		per_cpu(xen_debug_irq, cpu).irq = rc;
	}

	callfunc_name = kasprintf(GFP_KERNEL, "callfuncsingle%d", cpu);
	if (!callfunc_name)
		goto fail_mem;

	per_cpu(xen_callfuncsingle_irq, cpu).name = callfunc_name;
	rc = bind_ipi_to_irqhandler(XEN_CALL_FUNCTION_SINGLE_VECTOR,
				    cpu,
				    xen_call_function_single_interrupt,
				    IRQF_PERCPU|IRQF_NOBALANCING,
				    callfunc_name,
				    NULL);
	if (rc < 0)
		goto fail;
	per_cpu(xen_callfuncsingle_irq, cpu).irq = rc;

	return 0;

 fail_mem:
	rc = -ENOMEM;
 fail:
	xen_smp_intr_free(cpu);
	return rc;
}

void __init xen_smp_cpus_done(unsigned int max_cpus)
{
	if (xen_hvm_domain())
		native_smp_cpus_done(max_cpus);
}

void xen_smp_send_reschedule(int cpu)
{
	xen_send_IPI_one(cpu, XEN_RESCHEDULE_VECTOR);
}

static void __xen_send_IPI_mask(const struct cpumask *mask,
			      int vector)
{
	unsigned cpu;

	for_each_cpu_and(cpu, mask, cpu_online_mask)
		xen_send_IPI_one(cpu, vector);
}

void xen_smp_send_call_function_ipi(const struct cpumask *mask)
{
	int cpu;

	__xen_send_IPI_mask(mask, XEN_CALL_FUNCTION_VECTOR);

	/* Make sure other vcpus get a chance to run if they need to. */
	for_each_cpu(cpu, mask) {
		if (xen_vcpu_stolen(cpu)) {
			HYPERVISOR_sched_op(SCHEDOP_yield, NULL);
			break;
		}
	}
}

void xen_smp_send_call_function_single_ipi(int cpu)
{
	__xen_send_IPI_mask(cpumask_of(cpu),
			  XEN_CALL_FUNCTION_SINGLE_VECTOR);
}

static inline int xen_map_vector(int vector)
{
	int xen_vector;

	switch (vector) {
	case RESCHEDULE_VECTOR:
		xen_vector = XEN_RESCHEDULE_VECTOR;
		break;
	case CALL_FUNCTION_VECTOR:
		xen_vector = XEN_CALL_FUNCTION_VECTOR;
		break;
	case CALL_FUNCTION_SINGLE_VECTOR:
		xen_vector = XEN_CALL_FUNCTION_SINGLE_VECTOR;
		break;
	case IRQ_WORK_VECTOR:
		xen_vector = XEN_IRQ_WORK_VECTOR;
		break;
#ifdef CONFIG_X86_64
	case NMI_VECTOR:
	case APIC_DM_NMI: /* Some use that instead of NMI_VECTOR */
		xen_vector = XEN_NMI_VECTOR;
		break;
#endif
	default:
		xen_vector = -1;
		printk(KERN_ERR "xen: vector 0x%x is not implemented\n",
			vector);
	}

	return xen_vector;
}

void xen_send_IPI_mask(const struct cpumask *mask,
			      int vector)
{
	int xen_vector = xen_map_vector(vector);

	if (xen_vector >= 0)
		__xen_send_IPI_mask(mask, xen_vector);
}

void xen_send_IPI_all(int vector)
{
	int xen_vector = xen_map_vector(vector);

	if (xen_vector >= 0)
		__xen_send_IPI_mask(cpu_online_mask, xen_vector);
}

void xen_send_IPI_self(int vector)
{
	int xen_vector = xen_map_vector(vector);

	if (xen_vector >= 0)
		xen_send_IPI_one(smp_processor_id(), xen_vector);
}

void xen_send_IPI_mask_allbutself(const struct cpumask *mask,
				int vector)
{
	unsigned cpu;
	unsigned int this_cpu = smp_processor_id();
	int xen_vector = xen_map_vector(vector);

	if (!(num_online_cpus() > 1) || (xen_vector < 0))
		return;

	for_each_cpu_and(cpu, mask, cpu_online_mask) {
		if (this_cpu == cpu)
			continue;

		xen_send_IPI_one(cpu, xen_vector);
	}
}

void xen_send_IPI_allbutself(int vector)
{
	xen_send_IPI_mask_allbutself(cpu_online_mask, vector);
}

static irqreturn_t xen_call_function_interrupt(int irq, void *dev_id)
{
	generic_smp_call_function_interrupt();
	inc_irq_stat(irq_call_count);

	return IRQ_HANDLED;
}

static irqreturn_t xen_call_function_single_interrupt(int irq, void *dev_id)
{
	generic_smp_call_function_single_interrupt();
	inc_irq_stat(irq_call_count);

	return IRQ_HANDLED;
}
