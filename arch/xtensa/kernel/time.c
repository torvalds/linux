/*
 * arch/xtensa/kernel/time.c
 *
 * Timer and clock support.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005 Tensilica Inc.
 *
 * Chris Zankel <chris@zankel.net>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/profile.h>
#include <linux/delay.h>
#include <linux/irqdomain.h>
#include <linux/sched_clock.h>

#include <asm/timex.h>
#include <asm/platform.h>

unsigned long ccount_freq;		/* ccount Hz */
EXPORT_SYMBOL(ccount_freq);

static cycle_t ccount_read(struct clocksource *cs)
{
	return (cycle_t)get_ccount();
}

static u64 notrace ccount_sched_clock_read(void)
{
	return get_ccount();
}

static struct clocksource ccount_clocksource = {
	.name = "ccount",
	.rating = 200,
	.read = ccount_read,
	.mask = CLOCKSOURCE_MASK(32),
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
};

static int ccount_timer_set_next_event(unsigned long delta,
		struct clock_event_device *dev);
struct ccount_timer {
	struct clock_event_device evt;
	int irq_enabled;
	char name[24];
};
static DEFINE_PER_CPU(struct ccount_timer, ccount_timer);

static int ccount_timer_set_next_event(unsigned long delta,
		struct clock_event_device *dev)
{
	unsigned long flags, next;
	int ret = 0;

	local_irq_save(flags);
	next = get_ccount() + delta;
	set_linux_timer(next);
	if (next - get_ccount() > delta)
		ret = -ETIME;
	local_irq_restore(flags);

	return ret;
}

/*
 * There is no way to disable the timer interrupt at the device level,
 * only at the intenable register itself. Since enable_irq/disable_irq
 * calls are nested, we need to make sure that these calls are
 * balanced.
 */
static int ccount_timer_shutdown(struct clock_event_device *evt)
{
	struct ccount_timer *timer =
		container_of(evt, struct ccount_timer, evt);

	if (timer->irq_enabled) {
		disable_irq(evt->irq);
		timer->irq_enabled = 0;
	}
	return 0;
}

static int ccount_timer_set_oneshot(struct clock_event_device *evt)
{
	struct ccount_timer *timer =
		container_of(evt, struct ccount_timer, evt);

	if (!timer->irq_enabled) {
		enable_irq(evt->irq);
		timer->irq_enabled = 1;
	}
	return 0;
}

static irqreturn_t timer_interrupt(int irq, void *dev_id);
static struct irqaction timer_irqaction = {
	.handler =	timer_interrupt,
	.flags =	IRQF_TIMER,
	.name =		"timer",
};

void local_timer_setup(unsigned cpu)
{
	struct ccount_timer *timer = &per_cpu(ccount_timer, cpu);
	struct clock_event_device *clockevent = &timer->evt;

	timer->irq_enabled = 1;
	clockevent->name = timer->name;
	snprintf(timer->name, sizeof(timer->name), "ccount_clockevent_%u", cpu);
	clockevent->features = CLOCK_EVT_FEAT_ONESHOT;
	clockevent->rating = 300;
	clockevent->set_next_event = ccount_timer_set_next_event;
	clockevent->set_state_shutdown = ccount_timer_shutdown;
	clockevent->set_state_oneshot = ccount_timer_set_oneshot;
	clockevent->tick_resume = ccount_timer_set_oneshot;
	clockevent->cpumask = cpumask_of(cpu);
	clockevent->irq = irq_create_mapping(NULL, LINUX_TIMER_INT);
	if (WARN(!clockevent->irq, "error: can't map timer irq"))
		return;
	clockevents_config_and_register(clockevent, ccount_freq,
					0xf, 0xffffffff);
}

#ifdef CONFIG_XTENSA_CALIBRATE_CCOUNT
#ifdef CONFIG_OF
static void __init calibrate_ccount(void)
{
	struct device_node *cpu;
	struct clk *clk;

	cpu = of_find_compatible_node(NULL, NULL, "cdns,xtensa-cpu");
	if (cpu) {
		clk = of_clk_get(cpu, 0);
		if (!IS_ERR(clk)) {
			ccount_freq = clk_get_rate(clk);
			return;
		} else {
			pr_warn("%s: CPU input clock not found\n",
				__func__);
		}
	} else {
		pr_warn("%s: CPU node not found in the device tree\n",
			__func__);
	}

	platform_calibrate_ccount();
}
#else
static inline void calibrate_ccount(void)
{
	platform_calibrate_ccount();
}
#endif
#endif

void __init time_init(void)
{
	of_clk_init(NULL);
#ifdef CONFIG_XTENSA_CALIBRATE_CCOUNT
	pr_info("Calibrating CPU frequency ");
	calibrate_ccount();
	pr_cont("%d.%02d MHz\n",
		(int)ccount_freq / 1000000,
		(int)(ccount_freq / 10000) % 100);
#else
	ccount_freq = CONFIG_XTENSA_CPU_CLOCK*1000000UL;
#endif
	WARN(!ccount_freq,
	     "%s: CPU clock frequency is not set up correctly\n",
	     __func__);
	clocksource_register_hz(&ccount_clocksource, ccount_freq);
	local_timer_setup(0);
	setup_irq(this_cpu_ptr(&ccount_timer)->evt.irq, &timer_irqaction);
	sched_clock_register(ccount_sched_clock_read, 32, ccount_freq);
	clocksource_probe();
}

/*
 * The timer interrupt is called HZ times per second.
 */

irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &this_cpu_ptr(&ccount_timer)->evt;

	set_linux_timer(get_linux_timer());
	evt->event_handler(evt);

	/* Allow platform to do something useful (Wdog). */
	platform_heartbeat();

	return IRQ_HANDLED;
}

#ifndef CONFIG_GENERIC_CALIBRATE_DELAY
void calibrate_delay(void)
{
	loops_per_jiffy = ccount_freq / HZ;
	pr_info("Calibrating delay loop (skipped)... %lu.%02lu BogoMIPS preset\n",
		loops_per_jiffy / (1000000 / HZ),
		(loops_per_jiffy / (10000 / HZ)) % 100);
}
#endif
