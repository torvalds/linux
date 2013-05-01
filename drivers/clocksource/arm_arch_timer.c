/*
 *  linux/drivers/clocksource/arm_arch_timer.c
 *
 *  Copyright (C) 2011 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/io.h>

#include <asm/arch_timer.h>
#include <asm/virt.h>

#include <clocksource/arm_arch_timer.h>

static u32 arch_timer_rate;

enum ppi_nr {
	PHYS_SECURE_PPI,
	PHYS_NONSECURE_PPI,
	VIRT_PPI,
	HYP_PPI,
	MAX_TIMER_PPI
};

static int arch_timer_ppi[MAX_TIMER_PPI];

static struct clock_event_device __percpu *arch_timer_evt;

static bool arch_timer_use_virtual = true;

/*
 * Architected system timer support.
 */

static inline irqreturn_t timer_handler(const int access,
					struct clock_event_device *evt)
{
	unsigned long ctrl;
	ctrl = arch_timer_reg_read(access, ARCH_TIMER_REG_CTRL);
	if (ctrl & ARCH_TIMER_CTRL_IT_STAT) {
		ctrl |= ARCH_TIMER_CTRL_IT_MASK;
		arch_timer_reg_write(access, ARCH_TIMER_REG_CTRL, ctrl);
		evt->event_handler(evt);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static irqreturn_t arch_timer_handler_virt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	return timer_handler(ARCH_TIMER_VIRT_ACCESS, evt);
}

static irqreturn_t arch_timer_handler_phys(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	return timer_handler(ARCH_TIMER_PHYS_ACCESS, evt);
}

static inline void timer_set_mode(const int access, int mode)
{
	unsigned long ctrl;
	switch (mode) {
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		ctrl = arch_timer_reg_read(access, ARCH_TIMER_REG_CTRL);
		ctrl &= ~ARCH_TIMER_CTRL_ENABLE;
		arch_timer_reg_write(access, ARCH_TIMER_REG_CTRL, ctrl);
		break;
	default:
		break;
	}
}

static void arch_timer_set_mode_virt(enum clock_event_mode mode,
				     struct clock_event_device *clk)
{
	timer_set_mode(ARCH_TIMER_VIRT_ACCESS, mode);
}

static void arch_timer_set_mode_phys(enum clock_event_mode mode,
				     struct clock_event_device *clk)
{
	timer_set_mode(ARCH_TIMER_PHYS_ACCESS, mode);
}

static inline void set_next_event(const int access, unsigned long evt)
{
	unsigned long ctrl;
	ctrl = arch_timer_reg_read(access, ARCH_TIMER_REG_CTRL);
	ctrl |= ARCH_TIMER_CTRL_ENABLE;
	ctrl &= ~ARCH_TIMER_CTRL_IT_MASK;
	arch_timer_reg_write(access, ARCH_TIMER_REG_TVAL, evt);
	arch_timer_reg_write(access, ARCH_TIMER_REG_CTRL, ctrl);
}

static int arch_timer_set_next_event_virt(unsigned long evt,
					  struct clock_event_device *unused)
{
	set_next_event(ARCH_TIMER_VIRT_ACCESS, evt);
	return 0;
}

static int arch_timer_set_next_event_phys(unsigned long evt,
					  struct clock_event_device *unused)
{
	set_next_event(ARCH_TIMER_PHYS_ACCESS, evt);
	return 0;
}

static int __cpuinit arch_timer_setup(struct clock_event_device *clk)
{
	clk->features = CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_C3STOP;
	clk->name = "arch_sys_timer";
	clk->rating = 450;
	if (arch_timer_use_virtual) {
		clk->irq = arch_timer_ppi[VIRT_PPI];
		clk->set_mode = arch_timer_set_mode_virt;
		clk->set_next_event = arch_timer_set_next_event_virt;
	} else {
		clk->irq = arch_timer_ppi[PHYS_SECURE_PPI];
		clk->set_mode = arch_timer_set_mode_phys;
		clk->set_next_event = arch_timer_set_next_event_phys;
	}

	clk->cpumask = cpumask_of(smp_processor_id());

	clk->set_mode(CLOCK_EVT_MODE_SHUTDOWN, NULL);

	clockevents_config_and_register(clk, arch_timer_rate,
					0xf, 0x7fffffff);

	if (arch_timer_use_virtual)
		enable_percpu_irq(arch_timer_ppi[VIRT_PPI], 0);
	else {
		enable_percpu_irq(arch_timer_ppi[PHYS_SECURE_PPI], 0);
		if (arch_timer_ppi[PHYS_NONSECURE_PPI])
			enable_percpu_irq(arch_timer_ppi[PHYS_NONSECURE_PPI], 0);
	}

	arch_counter_set_user_access();

	return 0;
}

static int arch_timer_available(void)
{
	u32 freq;

	if (arch_timer_rate == 0) {
		freq = arch_timer_get_cntfrq();

		/* Check the timer frequency. */
		if (freq == 0) {
			pr_warn("Architected timer frequency not available\n");
			return -EINVAL;
		}

		arch_timer_rate = freq;
	}

	pr_info_once("Architected local timer running at %lu.%02luMHz (%s).\n",
		     (unsigned long)arch_timer_rate / 1000000,
		     (unsigned long)(arch_timer_rate / 10000) % 100,
		     arch_timer_use_virtual ? "virt" : "phys");
	return 0;
}

u32 arch_timer_get_rate(void)
{
	return arch_timer_rate;
}

/*
 * Some external users of arch_timer_read_counter (e.g. sched_clock) may try to
 * call it before it has been initialised. Rather than incur a performance
 * penalty checking for initialisation, provide a default implementation that
 * won't lead to time appearing to jump backwards.
 */
static u64 arch_timer_read_zero(void)
{
	return 0;
}

u64 (*arch_timer_read_counter)(void) = arch_timer_read_zero;

static cycle_t arch_counter_read(struct clocksource *cs)
{
	return arch_timer_read_counter();
}

static cycle_t arch_counter_read_cc(const struct cyclecounter *cc)
{
	return arch_timer_read_counter();
}

static struct clocksource clocksource_counter = {
	.name	= "arch_sys_counter",
	.rating	= 400,
	.read	= arch_counter_read,
	.mask	= CLOCKSOURCE_MASK(56),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

static struct cyclecounter cyclecounter = {
	.read	= arch_counter_read_cc,
	.mask	= CLOCKSOURCE_MASK(56),
};

static struct timecounter timecounter;

struct timecounter *arch_timer_get_timecounter(void)
{
	return &timecounter;
}

static void __cpuinit arch_timer_stop(struct clock_event_device *clk)
{
	pr_debug("arch_timer_teardown disable IRQ%d cpu #%d\n",
		 clk->irq, smp_processor_id());

	if (arch_timer_use_virtual)
		disable_percpu_irq(arch_timer_ppi[VIRT_PPI]);
	else {
		disable_percpu_irq(arch_timer_ppi[PHYS_SECURE_PPI]);
		if (arch_timer_ppi[PHYS_NONSECURE_PPI])
			disable_percpu_irq(arch_timer_ppi[PHYS_NONSECURE_PPI]);
	}

	clk->set_mode(CLOCK_EVT_MODE_UNUSED, clk);
}

static int __cpuinit arch_timer_cpu_notify(struct notifier_block *self,
					   unsigned long action, void *hcpu)
{
	struct clock_event_device *evt = this_cpu_ptr(arch_timer_evt);

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_STARTING:
		arch_timer_setup(evt);
		break;
	case CPU_DYING:
		arch_timer_stop(evt);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block arch_timer_cpu_nb __cpuinitdata = {
	.notifier_call = arch_timer_cpu_notify,
};

static int __init arch_timer_register(void)
{
	int err;
	int ppi;

	err = arch_timer_available();
	if (err)
		goto out;

	arch_timer_evt = alloc_percpu(struct clock_event_device);
	if (!arch_timer_evt) {
		err = -ENOMEM;
		goto out;
	}

	clocksource_register_hz(&clocksource_counter, arch_timer_rate);
	cyclecounter.mult = clocksource_counter.mult;
	cyclecounter.shift = clocksource_counter.shift;
	timecounter_init(&timecounter, &cyclecounter,
			 arch_counter_get_cntpct());

	if (arch_timer_use_virtual) {
		ppi = arch_timer_ppi[VIRT_PPI];
		err = request_percpu_irq(ppi, arch_timer_handler_virt,
					 "arch_timer", arch_timer_evt);
	} else {
		ppi = arch_timer_ppi[PHYS_SECURE_PPI];
		err = request_percpu_irq(ppi, arch_timer_handler_phys,
					 "arch_timer", arch_timer_evt);
		if (!err && arch_timer_ppi[PHYS_NONSECURE_PPI]) {
			ppi = arch_timer_ppi[PHYS_NONSECURE_PPI];
			err = request_percpu_irq(ppi, arch_timer_handler_phys,
						 "arch_timer", arch_timer_evt);
			if (err)
				free_percpu_irq(arch_timer_ppi[PHYS_SECURE_PPI],
						arch_timer_evt);
		}
	}

	if (err) {
		pr_err("arch_timer: can't register interrupt %d (%d)\n",
		       ppi, err);
		goto out_free;
	}

	err = register_cpu_notifier(&arch_timer_cpu_nb);
	if (err)
		goto out_free_irq;

	/* Immediately configure the timer on the boot CPU */
	arch_timer_setup(this_cpu_ptr(arch_timer_evt));

	return 0;

out_free_irq:
	if (arch_timer_use_virtual)
		free_percpu_irq(arch_timer_ppi[VIRT_PPI], arch_timer_evt);
	else {
		free_percpu_irq(arch_timer_ppi[PHYS_SECURE_PPI],
				arch_timer_evt);
		if (arch_timer_ppi[PHYS_NONSECURE_PPI])
			free_percpu_irq(arch_timer_ppi[PHYS_NONSECURE_PPI],
					arch_timer_evt);
	}

out_free:
	free_percpu(arch_timer_evt);
out:
	return err;
}

static const struct of_device_id arch_timer_of_match[] __initconst = {
	{ .compatible	= "arm,armv7-timer",	},
	{ .compatible	= "arm,armv8-timer",	},
	{},
};

int __init arch_timer_init(void)
{
	struct device_node *np;
	u32 freq;
	int i;

	np = of_find_matching_node(NULL, arch_timer_of_match);
	if (!np) {
		pr_err("arch_timer: can't find DT node\n");
		return -ENODEV;
	}

	/* Try to determine the frequency from the device tree or CNTFRQ */
	if (!of_property_read_u32(np, "clock-frequency", &freq))
		arch_timer_rate = freq;

	for (i = PHYS_SECURE_PPI; i < MAX_TIMER_PPI; i++)
		arch_timer_ppi[i] = irq_of_parse_and_map(np, i);

	of_node_put(np);

	/*
	 * If HYP mode is available, we know that the physical timer
	 * has been configured to be accessible from PL1. Use it, so
	 * that a guest can use the virtual timer instead.
	 *
	 * If no interrupt provided for virtual timer, we'll have to
	 * stick to the physical timer. It'd better be accessible...
	 */
	if (is_hyp_mode_available() || !arch_timer_ppi[VIRT_PPI]) {
		arch_timer_use_virtual = false;

		if (!arch_timer_ppi[PHYS_SECURE_PPI] ||
		    !arch_timer_ppi[PHYS_NONSECURE_PPI]) {
			pr_warn("arch_timer: No interrupt available, giving up\n");
			return -EINVAL;
		}
	}

	if (arch_timer_use_virtual)
		arch_timer_read_counter = arch_counter_get_cntvct;
	else
		arch_timer_read_counter = arch_counter_get_cntpct;

	return arch_timer_register();
}
