/*
 * Generic timers support
 *
 * Copyright (C) 2012 ARM Ltd.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/clockchips.h>
#include <linux/of_irq.h>
#include <linux/io.h>

#include <clocksource/arm_generic.h>

#include <asm/arm_generic.h>

static u32 arch_timer_rate;
static u64 sched_clock_mult __read_mostly;
static DEFINE_PER_CPU(struct clock_event_device, arch_timer_evt);
static int arch_timer_ppi;

static irqreturn_t arch_timer_handle_irq(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;
	unsigned long ctrl;

	ctrl = arch_timer_reg_read(ARCH_TIMER_REG_CTRL);
	if (ctrl & ARCH_TIMER_CTRL_ISTATUS) {
		ctrl |= ARCH_TIMER_CTRL_IMASK;
		arch_timer_reg_write(ARCH_TIMER_REG_CTRL, ctrl);
		evt->event_handler(evt);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static void arch_timer_stop(void)
{
	unsigned long ctrl;

	ctrl = arch_timer_reg_read(ARCH_TIMER_REG_CTRL);
	ctrl &= ~ARCH_TIMER_CTRL_ENABLE;
	arch_timer_reg_write(ARCH_TIMER_REG_CTRL, ctrl);
}

static void arch_timer_set_mode(enum clock_event_mode mode,
				struct clock_event_device *clk)
{
	switch (mode) {
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		arch_timer_stop();
		break;
	default:
		break;
	}
}

static int arch_timer_set_next_event(unsigned long evt,
				     struct clock_event_device *unused)
{
	unsigned long ctrl;

	ctrl = arch_timer_reg_read(ARCH_TIMER_REG_CTRL);
	ctrl |= ARCH_TIMER_CTRL_ENABLE;
	ctrl &= ~ARCH_TIMER_CTRL_IMASK;

	arch_timer_reg_write(ARCH_TIMER_REG_TVAL, evt);
	arch_timer_reg_write(ARCH_TIMER_REG_CTRL, ctrl);

	return 0;
}

static void __cpuinit arch_timer_setup(struct clock_event_device *clk)
{
	/* Let's make sure the timer is off before doing anything else */
	arch_timer_stop();

	clk->features = CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_C3STOP;
	clk->name = "arch_sys_timer";
	clk->rating = 400;
	clk->set_mode = arch_timer_set_mode;
	clk->set_next_event = arch_timer_set_next_event;
	clk->irq = arch_timer_ppi;
	clk->cpumask = cpumask_of(smp_processor_id());

	clockevents_config_and_register(clk, arch_timer_rate,
					0xf, 0x7fffffff);

	enable_percpu_irq(clk->irq, 0);

	/* Ensure the physical counter is visible to userspace for the vDSO. */
	arch_counter_enable_user_access();
}

static void __init arch_timer_calibrate(void)
{
	if (arch_timer_rate == 0) {
		arch_timer_reg_write(ARCH_TIMER_REG_CTRL, 0);
		arch_timer_rate = arch_timer_reg_read(ARCH_TIMER_REG_FREQ);

		/* Check the timer frequency. */
		if (arch_timer_rate == 0)
			panic("Architected timer frequency is set to zero.\n"
			      "You must set this in your .dts file\n");
	}

	/* Cache the sched_clock multiplier to save a divide in the hot path. */

	sched_clock_mult = DIV_ROUND_CLOSEST(NSEC_PER_SEC, arch_timer_rate);

	pr_info("Architected local timer running at %u.%02uMHz.\n",
		 arch_timer_rate / 1000000, (arch_timer_rate / 10000) % 100);
}

static cycle_t arch_counter_read(struct clocksource *cs)
{
	return arch_counter_get_cntpct();
}

static struct clocksource clocksource_counter = {
	.name	= "arch_sys_counter",
	.rating	= 400,
	.read	= arch_counter_read,
	.mask	= CLOCKSOURCE_MASK(56),
	.flags	= (CLOCK_SOURCE_IS_CONTINUOUS | CLOCK_SOURCE_VALID_FOR_HRES),
};

int read_current_timer(unsigned long *timer_value)
{
	*timer_value = arch_counter_get_cntpct();
	return 0;
}

unsigned long long notrace sched_clock(void)
{
	return arch_counter_get_cntvct() * sched_clock_mult;
}

static int __cpuinit arch_timer_cpu_notify(struct notifier_block *self,
					   unsigned long action, void *hcpu)
{
	int cpu = (long)hcpu;
	struct clock_event_device *clk = per_cpu_ptr(&arch_timer_evt, cpu);

	switch(action) {
	case CPU_STARTING:
	case CPU_STARTING_FROZEN:
		arch_timer_setup(clk);
		break;

	case CPU_DYING:
	case CPU_DYING_FROZEN:
		pr_debug("arch_timer_teardown disable IRQ%d cpu #%d\n",
			 clk->irq, cpu);
		disable_percpu_irq(clk->irq);
		arch_timer_set_mode(CLOCK_EVT_MODE_UNUSED, clk);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata arch_timer_cpu_nb = {
	.notifier_call = arch_timer_cpu_notify,
};

static const struct of_device_id arch_timer_of_match[] __initconst = {
	{ .compatible = "arm,armv8-timer" },
	{},
};

int __init arm_generic_timer_init(void)
{
	struct device_node *np;
	int err;
	u32 freq;

	np = of_find_matching_node(NULL, arch_timer_of_match);
	if (!np) {
		pr_err("arch_timer: can't find DT node\n");
		return -ENODEV;
	}

	/* Try to determine the frequency from the device tree or CNTFRQ */
	if (!of_property_read_u32(np, "clock-frequency", &freq))
		arch_timer_rate = freq;
	arch_timer_calibrate();

	arch_timer_ppi = irq_of_parse_and_map(np, 0);
	pr_info("arch_timer: found %s irq %d\n", np->name, arch_timer_ppi);

	err = request_percpu_irq(arch_timer_ppi, arch_timer_handle_irq,
				 np->name, &arch_timer_evt);
	if (err) {
		pr_err("arch_timer: can't register interrupt %d (%d)\n",
		       arch_timer_ppi, err);
		return err;
	}

	clocksource_register_hz(&clocksource_counter, arch_timer_rate);

	/* Calibrate the delay loop directly */
	lpj_fine = DIV_ROUND_CLOSEST(arch_timer_rate, HZ);

	/* Immediately configure the timer on the boot CPU */
	arch_timer_setup(this_cpu_ptr(&arch_timer_evt));

	register_cpu_notifier(&arch_timer_cpu_nb);

	return 0;
}
