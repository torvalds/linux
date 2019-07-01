// SPDX-License-Identifier: GPL-2.0

/*
 * Clocksource driver for the synthetic counter and timers
 * provided by the Hyper-V hypervisor to guest VMs, as described
 * in the Hyper-V Top Level Functional Spec (TLFS). This driver
 * is instruction set architecture independent.
 *
 * Copyright (C) 2019, Microsoft, Inc.
 *
 * Author:  Michael Kelley <mikelley@microsoft.com>
 */

#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/clockchips.h>
#include <linux/mm.h>
#include <clocksource/hyperv_timer.h>
#include <asm/hyperv-tlfs.h>
#include <asm/mshyperv.h>

static struct clock_event_device __percpu *hv_clock_event;

/*
 * If false, we're using the old mechanism for stimer0 interrupts
 * where it sends a VMbus message when it expires. The old
 * mechanism is used when running on older versions of Hyper-V
 * that don't support Direct Mode. While Hyper-V provides
 * four stimer's per CPU, Linux uses only stimer0.
 */
static bool direct_mode_enabled;

static int stimer0_irq;
static int stimer0_vector;
static int stimer0_message_sint;

/*
 * ISR for when stimer0 is operating in Direct Mode.  Direct Mode
 * does not use VMbus or any VMbus messages, so process here and not
 * in the VMbus driver code.
 */
void hv_stimer0_isr(void)
{
	struct clock_event_device *ce;

	ce = this_cpu_ptr(hv_clock_event);
	ce->event_handler(ce);
}
EXPORT_SYMBOL_GPL(hv_stimer0_isr);

static int hv_ce_set_next_event(unsigned long delta,
				struct clock_event_device *evt)
{
	u64 current_tick;

	current_tick = hyperv_cs->read(NULL);
	current_tick += delta;
	hv_init_timer(0, current_tick);
	return 0;
}

static int hv_ce_shutdown(struct clock_event_device *evt)
{
	hv_init_timer(0, 0);
	hv_init_timer_config(0, 0);
	if (direct_mode_enabled)
		hv_disable_stimer0_percpu_irq(stimer0_irq);

	return 0;
}

static int hv_ce_set_oneshot(struct clock_event_device *evt)
{
	union hv_stimer_config timer_cfg;

	timer_cfg.as_uint64 = 0;
	timer_cfg.enable = 1;
	timer_cfg.auto_enable = 1;
	if (direct_mode_enabled) {
		/*
		 * When it expires, the timer will directly interrupt
		 * on the specified hardware vector/IRQ.
		 */
		timer_cfg.direct_mode = 1;
		timer_cfg.apic_vector = stimer0_vector;
		hv_enable_stimer0_percpu_irq(stimer0_irq);
	} else {
		/*
		 * When it expires, the timer will generate a VMbus message,
		 * to be handled by the normal VMbus interrupt handler.
		 */
		timer_cfg.direct_mode = 0;
		timer_cfg.sintx = stimer0_message_sint;
	}
	hv_init_timer_config(0, timer_cfg.as_uint64);
	return 0;
}

/*
 * hv_stimer_init - Per-cpu initialization of the clockevent
 */
void hv_stimer_init(unsigned int cpu)
{
	struct clock_event_device *ce;

	/*
	 * Synthetic timers are always available except on old versions of
	 * Hyper-V on x86.  In that case, just return as Linux will use a
	 * clocksource based on emulated PIT or LAPIC timer hardware.
	 */
	if (!(ms_hyperv.features & HV_MSR_SYNTIMER_AVAILABLE))
		return;

	ce = per_cpu_ptr(hv_clock_event, cpu);
	ce->name = "Hyper-V clockevent";
	ce->features = CLOCK_EVT_FEAT_ONESHOT;
	ce->cpumask = cpumask_of(cpu);
	ce->rating = 1000;
	ce->set_state_shutdown = hv_ce_shutdown;
	ce->set_state_oneshot = hv_ce_set_oneshot;
	ce->set_next_event = hv_ce_set_next_event;

	clockevents_config_and_register(ce,
					HV_CLOCK_HZ,
					HV_MIN_DELTA_TICKS,
					HV_MAX_MAX_DELTA_TICKS);
}
EXPORT_SYMBOL_GPL(hv_stimer_init);

/*
 * hv_stimer_cleanup - Per-cpu cleanup of the clockevent
 */
void hv_stimer_cleanup(unsigned int cpu)
{
	struct clock_event_device *ce;

	/* Turn off clockevent device */
	if (ms_hyperv.features & HV_MSR_SYNTIMER_AVAILABLE) {
		ce = per_cpu_ptr(hv_clock_event, cpu);
		hv_ce_shutdown(ce);
	}
}
EXPORT_SYMBOL_GPL(hv_stimer_cleanup);

/* hv_stimer_alloc - Global initialization of the clockevent and stimer0 */
int hv_stimer_alloc(int sint)
{
	int ret;

	hv_clock_event = alloc_percpu(struct clock_event_device);
	if (!hv_clock_event)
		return -ENOMEM;

	direct_mode_enabled = ms_hyperv.misc_features &
			HV_STIMER_DIRECT_MODE_AVAILABLE;
	if (direct_mode_enabled) {
		ret = hv_setup_stimer0_irq(&stimer0_irq, &stimer0_vector,
				hv_stimer0_isr);
		if (ret) {
			free_percpu(hv_clock_event);
			hv_clock_event = NULL;
			return ret;
		}
	}

	stimer0_message_sint = sint;
	return 0;
}
EXPORT_SYMBOL_GPL(hv_stimer_alloc);

/* hv_stimer_free - Free global resources allocated by hv_stimer_alloc() */
void hv_stimer_free(void)
{
	if (direct_mode_enabled && (stimer0_irq != 0)) {
		hv_remove_stimer0_irq(stimer0_irq);
		stimer0_irq = 0;
	}
	free_percpu(hv_clock_event);
	hv_clock_event = NULL;
}
EXPORT_SYMBOL_GPL(hv_stimer_free);

/*
 * Do a global cleanup of clockevents for the cases of kexec and
 * vmbus exit
 */
void hv_stimer_global_cleanup(void)
{
	int	cpu;
	struct clock_event_device *ce;

	if (ms_hyperv.features & HV_MSR_SYNTIMER_AVAILABLE) {
		for_each_present_cpu(cpu) {
			ce = per_cpu_ptr(hv_clock_event, cpu);
			clockevents_unbind_device(ce, cpu);
		}
	}
	hv_stimer_free();
}
EXPORT_SYMBOL_GPL(hv_stimer_global_cleanup);
