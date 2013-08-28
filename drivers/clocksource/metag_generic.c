/*
 * Copyright (C) 2005-2013 Imagination Technologies Ltd.
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
 *
 *
 * Support for Meta per-thread timers.
 *
 * Meta hardware threads have 2 timers. The background timer (TXTIMER) is used
 * as a free-running time base (hz clocksource), and the interrupt timer
 * (TXTIMERI) is used for the timer interrupt (clock event). Both counters
 * traditionally count at approximately 1MHz.
 */

#include <clocksource/metag_generic.h>
#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>

#include <asm/clock.h>
#include <asm/hwthread.h>
#include <asm/core_reg.h>
#include <asm/metag_mem.h>
#include <asm/tbx.h>

#define HARDWARE_FREQ		1000000	/* 1MHz */
#define HARDWARE_DIV		1	/* divide by 1 = 1MHz clock */
#define HARDWARE_TO_NS_SHIFT	10	/* convert ticks to ns */

static unsigned int hwtimer_freq = HARDWARE_FREQ;
static DEFINE_PER_CPU(struct clock_event_device, local_clockevent);
static DEFINE_PER_CPU(char [11], local_clockevent_name);

static int metag_timer_set_next_event(unsigned long delta,
				      struct clock_event_device *dev)
{
	__core_reg_set(TXTIMERI, -delta);
	return 0;
}

static void metag_timer_set_mode(enum clock_event_mode mode,
				 struct clock_event_device *evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_RESUME:
		break;

	case CLOCK_EVT_MODE_SHUTDOWN:
		/* We should disable the IRQ here */
		break;

	case CLOCK_EVT_MODE_PERIODIC:
	case CLOCK_EVT_MODE_UNUSED:
		WARN_ON(1);
		break;
	};
}

static cycle_t metag_clocksource_read(struct clocksource *cs)
{
	return __core_reg_get(TXTIMER);
}

static struct clocksource clocksource_metag = {
	.name = "META",
	.rating = 200,
	.mask = CLOCKSOURCE_MASK(32),
	.read = metag_clocksource_read,
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
};

static irqreturn_t metag_timer_interrupt(int irq, void *dummy)
{
	struct clock_event_device *evt = &__get_cpu_var(local_clockevent);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction metag_timer_irq = {
	.name = "META core timer",
	.handler = metag_timer_interrupt,
	.flags = IRQF_TIMER | IRQF_IRQPOLL | IRQF_PERCPU,
};

unsigned long long sched_clock(void)
{
	unsigned long long ticks = __core_reg_get(TXTIMER);
	return ticks << HARDWARE_TO_NS_SHIFT;
}

static void __cpuinit arch_timer_setup(unsigned int cpu)
{
	unsigned int txdivtime;
	struct clock_event_device *clk = &per_cpu(local_clockevent, cpu);
	char *name = per_cpu(local_clockevent_name, cpu);

	txdivtime = __core_reg_get(TXDIVTIME);

	txdivtime &= ~TXDIVTIME_DIV_BITS;
	txdivtime |= (HARDWARE_DIV & TXDIVTIME_DIV_BITS);

	__core_reg_set(TXDIVTIME, txdivtime);

	sprintf(name, "META %d", cpu);
	clk->name = name;
	clk->features = CLOCK_EVT_FEAT_ONESHOT,

	clk->rating = 200,
	clk->shift = 12,
	clk->irq = tbisig_map(TBID_SIGNUM_TRT),
	clk->set_mode = metag_timer_set_mode,
	clk->set_next_event = metag_timer_set_next_event,

	clk->mult = div_sc(hwtimer_freq, NSEC_PER_SEC, clk->shift);
	clk->max_delta_ns = clockevent_delta2ns(0x7fffffff, clk);
	clk->min_delta_ns = clockevent_delta2ns(0xf, clk);
	clk->cpumask = cpumask_of(cpu);

	clockevents_register_device(clk);

	/*
	 * For all non-boot CPUs we need to synchronize our free
	 * running clock (TXTIMER) with the boot CPU's clock.
	 *
	 * While this won't be accurate, it should be close enough.
	 */
	if (cpu) {
		unsigned int thread0 = cpu_2_hwthread_id[0];
		unsigned long val;

		val = core_reg_read(TXUCT_ID, TXTIMER_REGNUM, thread0);
		__core_reg_set(TXTIMER, val);
	}
}

static int __cpuinit arch_timer_cpu_notify(struct notifier_block *self,
					   unsigned long action, void *hcpu)
{
	int cpu = (long)hcpu;

	switch (action) {
	case CPU_STARTING:
	case CPU_STARTING_FROZEN:
		arch_timer_setup(cpu);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata arch_timer_cpu_nb = {
	.notifier_call = arch_timer_cpu_notify,
};

int __init metag_generic_timer_init(void)
{
	/*
	 * On Meta 2 SoCs, the actual frequency of the timer is based on the
	 * Meta core clock speed divided by an integer, so it is only
	 * approximately 1MHz. Calculating the real frequency here drastically
	 * reduces clock skew on these SoCs.
	 */
#ifdef CONFIG_METAG_META21
	hwtimer_freq = get_coreclock() / (metag_in32(EXPAND_TIMER_DIV) + 1);
#endif
	pr_info("Timer frequency: %u Hz\n", hwtimer_freq);

	clocksource_register_hz(&clocksource_metag, hwtimer_freq);

	setup_irq(tbisig_map(TBID_SIGNUM_TRT), &metag_timer_irq);

	/* Configure timer on boot CPU */
	arch_timer_setup(smp_processor_id());

	/* Hook cpu boot to configure other CPU's timers */
	register_cpu_notifier(&arch_timer_cpu_nb);

	return 0;
}
