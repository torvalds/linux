// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 *
 * Most of the M-mode (i.e. NoMMU) RISC-V systems usually have a
 * CLINT MMIO timer device.
 */

#define pr_fmt(fmt) "clint: " fmt
#include <linux/bitops.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/sched_clock.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>
#include <linux/smp.h>
#include <linux/timex.h>

#ifndef CONFIG_RISCV_M_MODE
#include <asm/clint.h>
#endif

#define CLINT_IPI_OFF		0
#define CLINT_TIMER_CMP_OFF	0x4000
#define CLINT_TIMER_VAL_OFF	0xbff8

/* CLINT manages IPI and Timer for RISC-V M-mode  */
static u32 __iomem *clint_ipi_base;
static unsigned int clint_ipi_irq;
static u64 __iomem *clint_timer_cmp;
static u64 __iomem *clint_timer_val;
static unsigned long clint_timer_freq;
static unsigned int clint_timer_irq;

#ifdef CONFIG_RISCV_M_MODE
u64 __iomem *clint_time_val;
EXPORT_SYMBOL(clint_time_val);
#endif

#ifdef CONFIG_SMP
static void clint_send_ipi(unsigned int cpu)
{
	writel(1, clint_ipi_base + cpuid_to_hartid_map(cpu));
}

static void clint_clear_ipi(void)
{
	writel(0, clint_ipi_base + cpuid_to_hartid_map(smp_processor_id()));
}

static void clint_ipi_interrupt(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);

	chained_irq_enter(chip, desc);

	clint_clear_ipi();
	ipi_mux_process();

	chained_irq_exit(chip, desc);
}
#endif

#ifdef CONFIG_64BIT
#define clint_get_cycles()	readq_relaxed(clint_timer_val)
#else
#define clint_get_cycles()	readl_relaxed(clint_timer_val)
#define clint_get_cycles_hi()	readl_relaxed(((u32 *)clint_timer_val) + 1)
#endif

#ifdef CONFIG_64BIT
static u64 notrace clint_get_cycles64(void)
{
	return clint_get_cycles();
}
#else /* CONFIG_64BIT */
static u64 notrace clint_get_cycles64(void)
{
	u32 hi, lo;

	do {
		hi = clint_get_cycles_hi();
		lo = clint_get_cycles();
	} while (hi != clint_get_cycles_hi());

	return ((u64)hi << 32) | lo;
}
#endif /* CONFIG_64BIT */

static u64 clint_rdtime(struct clocksource *cs)
{
	return clint_get_cycles64();
}

static struct clocksource clint_clocksource = {
	.name		= "clint_clocksource",
	.rating		= 300,
	.mask		= CLOCKSOURCE_MASK(64),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
	.read		= clint_rdtime,
};

static int clint_clock_next_event(unsigned long delta,
				   struct clock_event_device *ce)
{
	void __iomem *r = clint_timer_cmp +
			  cpuid_to_hartid_map(smp_processor_id());

	csr_set(CSR_IE, IE_TIE);
	writeq_relaxed(clint_get_cycles64() + delta, r);
	return 0;
}

static DEFINE_PER_CPU(struct clock_event_device, clint_clock_event) = {
	.name		= "clint_clockevent",
	.features	= CLOCK_EVT_FEAT_ONESHOT,
	.rating		= 100,
	.set_next_event	= clint_clock_next_event,
};

static int clint_timer_starting_cpu(unsigned int cpu)
{
	struct clock_event_device *ce = per_cpu_ptr(&clint_clock_event, cpu);

	ce->cpumask = cpumask_of(cpu);
	clockevents_config_and_register(ce, clint_timer_freq, 100, ULONG_MAX);

	enable_percpu_irq(clint_timer_irq,
			  irq_get_trigger_type(clint_timer_irq));
	enable_percpu_irq(clint_ipi_irq,
			  irq_get_trigger_type(clint_ipi_irq));
	return 0;
}

static int clint_timer_dying_cpu(unsigned int cpu)
{
	disable_percpu_irq(clint_timer_irq);
	/*
	 * Don't disable IPI when CPU goes offline because
	 * the masking/unmasking of virtual IPIs is done
	 * via generic IPI-Mux
	 */
	return 0;
}

static irqreturn_t clint_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evdev = this_cpu_ptr(&clint_clock_event);

	csr_clear(CSR_IE, IE_TIE);
	evdev->event_handler(evdev);

	return IRQ_HANDLED;
}

static int __init clint_timer_init_dt(struct device_node *np)
{
	int rc;
	u32 i, nr_irqs;
	void __iomem *base;
	struct of_phandle_args oirq;

	/*
	 * Ensure that CLINT device interrupts are either RV_IRQ_TIMER or
	 * RV_IRQ_SOFT. If it's anything else then we ignore the device.
	 */
	nr_irqs = of_irq_count(np);
	for (i = 0; i < nr_irqs; i++) {
		if (of_irq_parse_one(np, i, &oirq)) {
			pr_err("%pOFP: failed to parse irq %d.\n", np, i);
			continue;
		}

		if ((oirq.args_count != 1) ||
		    (oirq.args[0] != RV_IRQ_TIMER &&
		     oirq.args[0] != RV_IRQ_SOFT)) {
			pr_err("%pOFP: invalid irq %d (hwirq %d)\n",
			       np, i, oirq.args[0]);
			return -ENODEV;
		}

		/* Find parent irq domain and map ipi irq */
		if (!clint_ipi_irq &&
		    oirq.args[0] == RV_IRQ_SOFT &&
		    irq_find_host(oirq.np))
			clint_ipi_irq = irq_of_parse_and_map(np, i);

		/* Find parent irq domain and map timer irq */
		if (!clint_timer_irq &&
		    oirq.args[0] == RV_IRQ_TIMER &&
		    irq_find_host(oirq.np))
			clint_timer_irq = irq_of_parse_and_map(np, i);
	}

	/* If CLINT ipi or timer irq not found then fail */
	if (!clint_ipi_irq || !clint_timer_irq) {
		pr_err("%pOFP: ipi/timer irq not found\n", np);
		return -ENODEV;
	}

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("%pOFP: could not map registers\n", np);
		return -ENODEV;
	}

	clint_ipi_base = base + CLINT_IPI_OFF;
	clint_timer_cmp = base + CLINT_TIMER_CMP_OFF;
	clint_timer_val = base + CLINT_TIMER_VAL_OFF;
	clint_timer_freq = riscv_timebase;

#ifdef CONFIG_RISCV_M_MODE
	/*
	 * Yes, that's an odd naming scheme.  time_val is public, but hopefully
	 * will die in favor of something cleaner.
	 */
	clint_time_val = clint_timer_val;
#endif

	pr_info("%pOFP: timer running at %ld Hz\n", np, clint_timer_freq);

	rc = clocksource_register_hz(&clint_clocksource, clint_timer_freq);
	if (rc) {
		pr_err("%pOFP: clocksource register failed [%d]\n", np, rc);
		goto fail_iounmap;
	}

	sched_clock_register(clint_get_cycles64, 64, clint_timer_freq);

	rc = request_percpu_irq(clint_timer_irq, clint_timer_interrupt,
				 "clint-timer", &clint_clock_event);
	if (rc) {
		pr_err("registering percpu irq failed [%d]\n", rc);
		goto fail_iounmap;
	}

#ifdef CONFIG_SMP
	rc = ipi_mux_create(BITS_PER_BYTE, clint_send_ipi);
	if (rc <= 0) {
		pr_err("unable to create muxed IPIs\n");
		rc = (rc < 0) ? rc : -ENODEV;
		goto fail_free_irq;
	}

	irq_set_chained_handler(clint_ipi_irq, clint_ipi_interrupt);
	riscv_ipi_set_virq_range(rc, BITS_PER_BYTE);
	clint_clear_ipi();
#endif

	rc = cpuhp_setup_state(CPUHP_AP_CLINT_TIMER_STARTING,
				"clockevents/clint/timer:starting",
				clint_timer_starting_cpu,
				clint_timer_dying_cpu);
	if (rc) {
		pr_err("%pOFP: cpuhp setup state failed [%d]\n", np, rc);
		goto fail_free_irq;
	}

	return 0;

fail_free_irq:
	free_percpu_irq(clint_timer_irq, &clint_clock_event);
fail_iounmap:
	iounmap(base);
	return rc;
}

TIMER_OF_DECLARE(clint_timer, "riscv,clint0", clint_timer_init_dt);
TIMER_OF_DECLARE(clint_timer1, "sifive,clint0", clint_timer_init_dt);
