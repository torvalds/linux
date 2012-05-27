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
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <asm/cacheflush.h>

HV_Topology smp_topology __write_once;
EXPORT_SYMBOL(smp_topology);

#if CHIP_HAS_IPI()
static unsigned long __iomem *ipi_mappings[NR_CPUS];
#endif


/*
 * Top-level send_IPI*() functions to send messages to other cpus.
 */

/* Set by smp_send_stop() to avoid recursive panics. */
static int stopping_cpus;

static void __send_IPI_many(HV_Recipient *recip, int nrecip, int tag)
{
	int sent = 0;
	while (sent < nrecip) {
		int rc = hv_send_message(recip, nrecip,
					 (HV_VirtAddr)&tag, sizeof(tag));
		if (rc < 0) {
			if (!stopping_cpus)  /* avoid recursive panic */
				panic("hv_send_message returned %d", rc);
			break;
		}
		WARN_ONCE(rc == 0, "hv_send_message() returned zero\n");
		sent += rc;
	}
}

void send_IPI_single(int cpu, int tag)
{
	HV_Recipient recip = {
		.y = cpu / smp_width,
		.x = cpu % smp_width,
		.state = HV_TO_BE_SENT
	};
	__send_IPI_many(&recip, 1, tag);
}

void send_IPI_many(const struct cpumask *mask, int tag)
{
	HV_Recipient recip[NR_CPUS];
	int cpu;
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
	__send_IPI_many(recip, nrecip, tag);
}

void send_IPI_allbutself(int tag)
{
	struct cpumask mask;
	cpumask_copy(&mask, cpu_online_mask);
	cpumask_clear_cpu(smp_processor_id(), &mask);
	send_IPI_many(&mask, tag);
}

/*
 * Functions related to starting/stopping cpus.
 */

/* Handler to start the current cpu. */
static void smp_start_cpu_interrupt(void)
{
	get_irq_regs()->pc = start_cpu_function_addr;
}

/* Handler to stop the current cpu. */
static void smp_stop_cpu_interrupt(void)
{
	set_cpu_online(smp_processor_id(), 0);
	arch_local_irq_disable_all();
	for (;;)
		asm("nap; nop");
}

/* This function calls the 'stop' function on all other CPUs in the system. */
void smp_send_stop(void)
{
	stopping_cpus = 1;
	send_IPI_allbutself(MSG_TAG_STOP_CPU);
}

/* On panic, just wait; we may get an smp_send_stop() later on. */
void panic_smp_self_stop(void)
{
	while (1)
		asm("nap; nop");
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


/* Called when smp_send_reschedule() triggers IRQ_RESCHEDULE. */
static irqreturn_t handle_reschedule_ipi(int irq, void *token)
{
	__get_cpu_var(irq_stat).irq_resched_count++;
	scheduler_ipi();

	return IRQ_HANDLED;
}

static struct irqaction resched_action = {
	.handler = handle_reschedule_ipi,
	.name = "resched",
	.dev_id = handle_reschedule_ipi /* unique token */,
};

void __init ipi_init(void)
{
#if CHIP_HAS_IPI()
	int cpu;
	/* Map IPI trigger MMIO addresses. */
	for_each_possible_cpu(cpu) {
		HV_Coord tile;
		HV_PTE pte;
		unsigned long offset;

		tile.x = cpu_x(cpu);
		tile.y = cpu_y(cpu);
		if (hv_get_ipi_pte(tile, KERNEL_PL, &pte) != 0)
			panic("Failed to initialize IPI for cpu %d\n", cpu);

		offset = PFN_PHYS(pte_pfn(pte));
		ipi_mappings[cpu] = ioremap_prot(offset, PAGE_SIZE, pte);
	}
#endif

	/* Bind handle_reschedule_ipi() to IRQ_RESCHEDULE. */
	tile_irq_activate(IRQ_RESCHEDULE, TILE_IRQ_PERCPU);
	BUG_ON(setup_irq(IRQ_RESCHEDULE, &resched_action));
}

#if CHIP_HAS_IPI()

void smp_send_reschedule(int cpu)
{
	WARN_ON(cpu_is_offline(cpu));

	/*
	 * We just want to do an MMIO store.  The traditional writeq()
	 * functions aren't really correct here, since they're always
	 * directed at the PCI shim.  For now, just do a raw store,
	 * casting away the __iomem attribute.
	 */
	((unsigned long __force *)ipi_mappings[cpu])[IRQ_RESCHEDULE] = 0;
}

#else

void smp_send_reschedule(int cpu)
{
	HV_Coord coord;

	WARN_ON(cpu_is_offline(cpu));

	coord.y = cpu_y(cpu);
	coord.x = cpu_x(cpu);
	hv_trigger_ipi(coord, IRQ_RESCHEDULE);
}

#endif /* CHIP_HAS_IPI() */
