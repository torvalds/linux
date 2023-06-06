// SPDX-License-Identifier: GPL-2.0
/* linux/arch/sparc/kernel/time.c
 *
 * Copyright (C) 1995 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@eden.rutgers.edu)
 *
 * Chris Davis (cdavis@cois.on.ca) 03/27/1998
 * Added support for the intersil on the sun4/4200
 *
 * Gleb Raiko (rajko@mech.math.msu.su) 08/18/1998
 * Support for MicroSPARC-IIep, PCI CPU.
 *
 * This file handles the Sparc specific time handling details.
 *
 * 1997-09-10	Updated NTP code according to technical memorandum Jan '96
 *		"A Kernel Model for Precision Timekeeping" by Dave Mills
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/rtc/m48t59.h>
#include <linux/timex.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/profile.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <asm/mc146818rtc.h>
#include <asm/oplib.h>
#include <asm/timex.h>
#include <asm/timer.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/idprom.h>
#include <asm/page.h>
#include <asm/pcic.h>
#include <asm/irq_regs.h>
#include <asm/setup.h>

#include "kernel.h"
#include "irq.h"

static __cacheline_aligned_in_smp DEFINE_SEQLOCK(timer_cs_lock);
static __volatile__ u64 timer_cs_internal_counter = 0;
static char timer_cs_enabled = 0;

static struct clock_event_device timer_ce;
static char timer_ce_enabled = 0;

#ifdef CONFIG_SMP
DEFINE_PER_CPU(struct clock_event_device, sparc32_clockevent);
#endif

DEFINE_SPINLOCK(rtc_lock);
EXPORT_SYMBOL(rtc_lock);

unsigned long profile_pc(struct pt_regs *regs)
{
	extern char __copy_user_begin[], __copy_user_end[];
	extern char __bzero_begin[], __bzero_end[];

	unsigned long pc = regs->pc;

	if (in_lock_functions(pc) ||
	    (pc >= (unsigned long) __copy_user_begin &&
	     pc < (unsigned long) __copy_user_end) ||
	    (pc >= (unsigned long) __bzero_begin &&
	     pc < (unsigned long) __bzero_end))
		pc = regs->u_regs[UREG_RETPC];
	return pc;
}

EXPORT_SYMBOL(profile_pc);

volatile u32 __iomem *master_l10_counter;

irqreturn_t notrace timer_interrupt(int dummy, void *dev_id)
{
	if (timer_cs_enabled) {
		write_seqlock(&timer_cs_lock);
		timer_cs_internal_counter++;
		sparc_config.clear_clock_irq();
		write_sequnlock(&timer_cs_lock);
	} else {
		sparc_config.clear_clock_irq();
	}

	if (timer_ce_enabled)
		timer_ce.event_handler(&timer_ce);

	return IRQ_HANDLED;
}

static int timer_ce_shutdown(struct clock_event_device *evt)
{
	timer_ce_enabled = 0;
	smp_mb();
	return 0;
}

static int timer_ce_set_periodic(struct clock_event_device *evt)
{
	timer_ce_enabled = 1;
	smp_mb();
	return 0;
}

static __init void setup_timer_ce(void)
{
	struct clock_event_device *ce = &timer_ce;

	BUG_ON(smp_processor_id() != boot_cpu_id);

	ce->name     = "timer_ce";
	ce->rating   = 100;
	ce->features = CLOCK_EVT_FEAT_PERIODIC;
	ce->set_state_shutdown = timer_ce_shutdown;
	ce->set_state_periodic = timer_ce_set_periodic;
	ce->tick_resume = timer_ce_set_periodic;
	ce->cpumask  = cpu_possible_mask;
	ce->shift    = 32;
	ce->mult     = div_sc(sparc_config.clock_rate, NSEC_PER_SEC,
	                      ce->shift);
	clockevents_register_device(ce);
}

static unsigned int sbus_cycles_offset(void)
{
	u32 val, offset;

	val = sbus_readl(master_l10_counter);
	offset = (val >> TIMER_VALUE_SHIFT) & TIMER_VALUE_MASK;

	/* Limit hit? */
	if (val & TIMER_LIMIT_BIT)
		offset += sparc_config.cs_period;

	return offset;
}

static u64 timer_cs_read(struct clocksource *cs)
{
	unsigned int seq, offset;
	u64 cycles;

	do {
		seq = read_seqbegin(&timer_cs_lock);

		cycles = timer_cs_internal_counter;
		offset = sparc_config.get_cycles_offset();
	} while (read_seqretry(&timer_cs_lock, seq));

	/* Count absolute cycles */
	cycles *= sparc_config.cs_period;
	cycles += offset;

	return cycles;
}

static struct clocksource timer_cs = {
	.name	= "timer_cs",
	.rating	= 100,
	.read	= timer_cs_read,
	.mask	= CLOCKSOURCE_MASK(64),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

static __init int setup_timer_cs(void)
{
	timer_cs_enabled = 1;
	return clocksource_register_hz(&timer_cs, sparc_config.clock_rate);
}

#ifdef CONFIG_SMP
static int percpu_ce_shutdown(struct clock_event_device *evt)
{
	int cpu = cpumask_first(evt->cpumask);

	sparc_config.load_profile_irq(cpu, 0);
	return 0;
}

static int percpu_ce_set_periodic(struct clock_event_device *evt)
{
	int cpu = cpumask_first(evt->cpumask);

	sparc_config.load_profile_irq(cpu, SBUS_CLOCK_RATE / HZ);
	return 0;
}

static int percpu_ce_set_next_event(unsigned long delta,
				    struct clock_event_device *evt)
{
	int cpu = cpumask_first(evt->cpumask);
	unsigned int next = (unsigned int)delta;

	sparc_config.load_profile_irq(cpu, next);
	return 0;
}

void register_percpu_ce(int cpu)
{
	struct clock_event_device *ce = &per_cpu(sparc32_clockevent, cpu);
	unsigned int features = CLOCK_EVT_FEAT_PERIODIC;

	if (sparc_config.features & FEAT_L14_ONESHOT)
		features |= CLOCK_EVT_FEAT_ONESHOT;

	ce->name           = "percpu_ce";
	ce->rating         = 200;
	ce->features       = features;
	ce->set_state_shutdown = percpu_ce_shutdown;
	ce->set_state_periodic = percpu_ce_set_periodic;
	ce->set_state_oneshot = percpu_ce_shutdown;
	ce->set_next_event = percpu_ce_set_next_event;
	ce->cpumask        = cpumask_of(cpu);
	ce->shift          = 32;
	ce->mult           = div_sc(sparc_config.clock_rate, NSEC_PER_SEC,
	                            ce->shift);
	ce->max_delta_ns   = clockevent_delta2ns(sparc_config.clock_rate, ce);
	ce->max_delta_ticks = (unsigned long)sparc_config.clock_rate;
	ce->min_delta_ns   = clockevent_delta2ns(100, ce);
	ce->min_delta_ticks = 100;

	clockevents_register_device(ce);
}
#endif

static unsigned char mostek_read_byte(struct device *dev, u32 ofs)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct m48t59_plat_data *pdata = pdev->dev.platform_data;

	return readb(pdata->ioaddr + ofs);
}

static void mostek_write_byte(struct device *dev, u32 ofs, u8 val)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct m48t59_plat_data *pdata = pdev->dev.platform_data;

	writeb(val, pdata->ioaddr + ofs);
}

static struct m48t59_plat_data m48t59_data = {
	.read_byte = mostek_read_byte,
	.write_byte = mostek_write_byte,
};

/* resource is set at runtime */
static struct platform_device m48t59_rtc = {
	.name		= "rtc-m48t59",
	.id		= 0,
	.num_resources	= 1,
	.dev	= {
		.platform_data = &m48t59_data,
	},
};

static int clock_probe(struct platform_device *op)
{
	struct device_node *dp = op->dev.of_node;
	const char *model = of_get_property(dp, "model", NULL);

	if (!model)
		return -ENODEV;

	/* Only the primary RTC has an address property */
	if (!of_property_present(dp, "address"))
		return -ENODEV;

	m48t59_rtc.resource = &op->resource[0];
	if (!strcmp(model, "mk48t02")) {
		/* Map the clock register io area read-only */
		m48t59_data.ioaddr = of_ioremap(&op->resource[0], 0,
						2048, "rtc-m48t59");
		m48t59_data.type = M48T59RTC_TYPE_M48T02;
	} else if (!strcmp(model, "mk48t08")) {
		m48t59_data.ioaddr = of_ioremap(&op->resource[0], 0,
						8192, "rtc-m48t59");
		m48t59_data.type = M48T59RTC_TYPE_M48T08;
	} else
		return -ENODEV;

	if (platform_device_register(&m48t59_rtc) < 0)
		printk(KERN_ERR "Registering RTC device failed\n");

	return 0;
}

static const struct of_device_id clock_match[] = {
	{
		.name = "eeprom",
	},
	{},
};

static struct platform_driver clock_driver = {
	.probe		= clock_probe,
	.driver = {
		.name = "rtc",
		.of_match_table = clock_match,
	},
};


/* Probe for the mostek real time clock chip. */
static int __init clock_init(void)
{
	return platform_driver_register(&clock_driver);
}
/* Must be after subsys_initcall() so that busses are probed.  Must
 * be before device_initcall() because things like the RTC driver
 * need to see the clock registers.
 */
fs_initcall(clock_init);

static void __init sparc32_late_time_init(void)
{
	if (sparc_config.features & FEAT_L10_CLOCKEVENT)
		setup_timer_ce();
	if (sparc_config.features & FEAT_L10_CLOCKSOURCE)
		setup_timer_cs();
#ifdef CONFIG_SMP
	register_percpu_ce(smp_processor_id());
#endif
}

static void __init sbus_time_init(void)
{
	sparc_config.get_cycles_offset = sbus_cycles_offset;
	sparc_config.init_timers();
}

void __init time_init(void)
{
	sparc_config.features = 0;
	late_time_init = sparc32_late_time_init;

	if (pcic_present())
		pci_time_init();
	else
		sbus_time_init();
}

