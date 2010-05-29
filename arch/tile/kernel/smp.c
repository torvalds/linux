/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * TILE SMP support routines.
 */

#include <linux/smp.h>
#include <linux/irq.h>
#include <asm/cacheflush.h>

HV_Topology smp_topology __write_once;


/*
 * Top-level send_IPI*() functions to send messages to other cpus.
 */

/* Set by smp_send_stop() to avoid recursive panics. */
static int stopping_cpus;

void send_IPI_single(int cpu, int tag)
{
	HV_Recipient recip = {
		.y = cpu / smp_width,
		.x = cpu % smp_width,
		.state = HV_TO_BE_SENT
	};
	int rc = hv_send_message(&recip, 1, (HV_VirtAddr)&tag, sizeof(tag));
	BUG_ON(rc <= 0);
}

void send_IPI_many(const struct cpumask *mask, int tag)
{
	HV_Recipient recip[NR_CPUS];
	int cpu, sent;
	int nrecip = 0;
	int my_cpu = smp_processor_id();
	for_each_cpu(cpu, mask) {
		HV_Recipient *r;
		BUG_ON(cpu == my_cpu);
		r = &recip[nrecip++];
		r->y = cpu / smp_width;
		r->x = cpu % smp_width;
		r->state = HV_TO_BE_SENT;
	}
	sent = 0;
	while (sent < nrecip) {
		int rc = hv_send_message(recip, nrecip,
					 (HV_VirtAddr)&tag, sizeof(tag));
		if (rc <= 0) {
			if (!stopping_cpus)  /* avoid recursive panic */
				panic("hv_send_message returned %d", rc);
			break;
		}
		sent += rc;
	}
}

void send_IPI_allbutself(int tag)
{
	struct cpumask mask;
	cpumask_copy(&mask, cpu_online_mask);
	cpumask_clear_cpu(smp_processor_id(), &mask);
	send_IPI_many(&mask, tag);
}


/*
 * Provide smp_call_function_mask, but also run function locally
 * if specified in the mask.
 */
void on_each_cpu_mask(const struct cpumask *mask, void (*func)(void *),
		      void *info, bool wait)
{
	int cpu = get_cpu();
	smp_call_function_many(mask, func, info, wait);
	if (cpumask_test_cpu(cpu, mask)) {
		local_irq_disable();
		func(info);
		local_irq_enable();
	}
	put_cpu();
}


/*
 * Functions related to starting/stopping cpus.
 */

/* Handler to start the current cpu. */
static void smp_start_cpu_interrupt(void)
{
	extern unsigned long start_cpu_function_addr;
	get_irq_regs()->pc = start_cpu_function_addr;
}

/* Handler to stop the current cpu. */
static void smp_stop_cpu_interrupt(void)
{
	set_cpu_online(smp_processor_id(), 0);
	raw_local_irq_disable_all();
	for (;;)
		asm("nap");
}

/* This function calls the 'stop' function on all other CPUs in the system. */
void smp_send_stop(void)
{
	stopping_cpus = 1;
	send_IPI_allbutself(MSG_TAG_STOP_CPU);
}


/*
 * Dispatch code called from hv_message_intr() for HV_MSG_TILE hv messages.
 */
void evaluate_message(int tag)
{
	switch (tag) {
	case MSG_TAG_START_CPU: /* Start up a cpu */
		smp_start_cpu_interrupt();
		break;

	case MSG_TAG_STOP_CPU: /* Sent to shut down slave CPU's */
		smp_stop_cpu_interrupt();
		break;

	case MSG_TAG_CALL_FUNCTION_MANY: /* Call function on cpumask */
		generic_smp_call_function_interrupt();
		break;

	case MSG_TAG_CALL_FUNCTION_SINGLE: /* Call function on one other CPU */
		generic_smp_call_function_single_interrupt();
		break;

	default:
		panic("Unknown IPI message tag %d", tag);
		break;
	}
}


/*
 * flush_icache_range() code uses smp_call_function().
 */

struct ipi_flush {
	unsigned long start;
	unsigned long end;
};

static void ipi_flush_icache_range(void *info)
{
	struct ipi_flush *flush = (struct ipi_flush *) info;
	__flush_icache_range(flush->start, flush->end);
}

void flush_icache_range(unsigned long start, unsigned long end)
{
	struct ipi_flush flush = { start, end };
	preempt_disable();
	on_each_cpu(ipi_flush_icache_range, &flush, 1);
	preempt_enable();
}


/*
 * The smp_send_reschedule() path does not use the hv_message_intr()
 * path but instead the faster tile_dev_intr() path for interrupts.
 */

irqreturn_t handle_reschedule_ipi(int irq, void *token)
{
	/*
	 * Nothing to do here; when we return from interrupt, the
	 * rescheduling will occur there. But do bump the interrupt
	 * profiler count in the meantime.
	 */
	__get_cpu_var(irq_stat).irq_resched_count++;

	return IRQ_HANDLED;
}

void smp_send_reschedule(int cpu)
{
	HV_Coord coord;

	WARN_ON(cpu_is_offline(cpu));
	coord.y = cpu / smp_width;
	coord.x = cpu % smp_width;
	hv_trigger_ipi(coord, IRQ_RESCHEDULE);
}
