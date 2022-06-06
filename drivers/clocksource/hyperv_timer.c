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
#include <linux/clocksource.h>
#include <linux/sched_clock.h>
#include <linux/mm.h>
#include <linux/cpuhotplug.h>
#include <clocksource/hyperv_timer.h>
#include <asm/hyperv-tlfs.h>
#include <asm/mshyperv.h>

static struct clock_event_device __percpu *hv_clock_event;
static u64 hv_sched_clock_offset __ro_after_init;

/*
 * If false, we're using the old mechanism for stimer0 interrupts
 * where it sends a VMbus message when it expires. The old
 * mechanism is used when running on older versions of Hyper-V
 * that don't support Direct Mode. While Hyper-V provides
 * four stimer's per CPU, Linux uses only stimer0.
 *
 * Because Direct Mode does not require processing a VMbus
 * message, stimer interrupts can be enabled earlier in the
 * process of booting a CPU, and consistent with when timer
 * interrupts are enabled for other clocksource drivers.
 * However, for legacy versions of Hyper-V when Direct Mode
 * is not enabled, setting up stimer interrupts must be
 * delayed until VMbus is initialized and can process the
 * interrupt message.
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

	current_tick = hv_read_reference_counter();
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
static int hv_stimer_init(unsigned int cpu)
{
	struct clock_event_device *ce;

	if (!hv_clock_event)
		return 0;

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
	return 0;
}

/*
 * hv_stimer_cleanup - Per-cpu cleanup of the clockevent
 */
int hv_stimer_cleanup(unsigned int cpu)
{
	struct clock_event_device *ce;

	if (!hv_clock_event)
		return 0;

	/*
	 * In the legacy case where Direct Mode is not enabled
	 * (which can only be on x86/64), stimer cleanup happens
	 * relatively early in the CPU offlining process. We
	 * must unbind the stimer-based clockevent device so
	 * that the LAPIC timer can take over until clockevents
	 * are no longer needed in the offlining process. Note
	 * that clockevents_unbind_device() eventually calls
	 * hv_ce_shutdown().
	 *
	 * The unbind should not be done when Direct Mode is
	 * enabled because we may be on an architecture where
	 * there are no other clockevent devices to fallback to.
	 */
	ce = per_cpu_ptr(hv_clock_event, cpu);
	if (direct_mode_enabled)
		hv_ce_shutdown(ce);
	else
		clockevents_unbind_device(ce, cpu);

	return 0;
}
EXPORT_SYMBOL_GPL(hv_stimer_cleanup);

/* hv_stimer_alloc - Global initialization of the clockevent and stimer0 */
int hv_stimer_alloc(void)
{
	int ret = 0;

	/*
	 * Synthetic timers are always available except on old versions of
	 * Hyper-V on x86.  In that case, return as error as Linux will use a
	 * clockevent based on emulated LAPIC timer hardware.
	 */
	if (!(ms_hyperv.features & HV_MSR_SYNTIMER_AVAILABLE))
		return -EINVAL;

	hv_clock_event = alloc_percpu(struct clock_event_device);
	if (!hv_clock_event)
		return -ENOMEM;

	direct_mode_enabled = ms_hyperv.misc_features &
			HV_STIMER_DIRECT_MODE_AVAILABLE;
	if (direct_mode_enabled) {
		ret = hv_setup_stimer0_irq(&stimer0_irq, &stimer0_vector,
				hv_stimer0_isr);
		if (ret)
			goto free_percpu;

		/*
		 * Since we are in Direct Mode, stimer initialization
		 * can be done now with a CPUHP value in the same range
		 * as other clockevent devices.
		 */
		ret = cpuhp_setup_state(CPUHP_AP_HYPERV_TIMER_STARTING,
				"clockevents/hyperv/stimer:starting",
				hv_stimer_init, hv_stimer_cleanup);
		if (ret < 0)
			goto free_stimer0_irq;
	}
	return ret;

free_stimer0_irq:
	hv_remove_stimer0_irq(stimer0_irq);
	stimer0_irq = 0;
free_percpu:
	free_percpu(hv_clock_event);
	hv_clock_event = NULL;
	return ret;
}
EXPORT_SYMBOL_GPL(hv_stimer_alloc);

/*
 * hv_stimer_legacy_init -- Called from the VMbus driver to handle
 * the case when Direct Mode is not enabled, and the stimer
 * must be initialized late in the CPU onlining process.
 *
 */
void hv_stimer_legacy_init(unsigned int cpu, int sint)
{
	if (direct_mode_enabled)
		return;

	/*
	 * This function gets called by each vCPU, so setting the
	 * global stimer_message_sint value each time is conceptually
	 * not ideal, but the value passed in is always the same and
	 * it avoids introducing yet another interface into this
	 * clocksource driver just to set the sint in the legacy case.
	 */
	stimer0_message_sint = sint;
	(void)hv_stimer_init(cpu);
}
EXPORT_SYMBOL_GPL(hv_stimer_legacy_init);

/*
 * hv_stimer_legacy_cleanup -- Called from the VMbus driver to
 * handle the case when Direct Mode is not enabled, and the
 * stimer must be cleaned up early in the CPU offlining
 * process.
 */
void hv_stimer_legacy_cleanup(unsigned int cpu)
{
	if (direct_mode_enabled)
		return;
	(void)hv_stimer_cleanup(cpu);
}
EXPORT_SYMBOL_GPL(hv_stimer_legacy_cleanup);


/* hv_stimer_free - Free global resources allocated by hv_stimer_alloc() */
void hv_stimer_free(void)
{
	if (!hv_clock_event)
		return;

	if (direct_mode_enabled) {
		cpuhp_remove_state(CPUHP_AP_HYPERV_TIMER_STARTING);
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

	/*
	 * hv_stime_legacy_cleanup() will stop the stimer if Direct
	 * Mode is not enabled, and fallback to the LAPIC timer.
	 */
	for_each_present_cpu(cpu) {
		hv_stimer_legacy_cleanup(cpu);
	}

	/*
	 * If Direct Mode is enabled, the cpuhp teardown callback
	 * (hv_stimer_cleanup) will be run on all CPUs to stop the
	 * stimers.
	 */
	hv_stimer_free();
}
EXPORT_SYMBOL_GPL(hv_stimer_global_cleanup);

/*
 * Code and definitions for the Hyper-V clocksources.  Two
 * clocksources are defined: one that reads the Hyper-V defined MSR, and
 * the other that uses the TSC reference page feature as defined in the
 * TLFS.  The MSR version is for compatibility with old versions of
 * Hyper-V and 32-bit x86.  The TSC reference page version is preferred.
 *
 * The Hyper-V clocksource ratings of 250 are chosen to be below the
 * TSC clocksource rating of 300.  In configurations where Hyper-V offers
 * an InvariantTSC, the TSC is not marked "unstable", so the TSC clocksource
 * is available and preferred.  With the higher rating, it will be the
 * default.  On older hardware and Hyper-V versions, the TSC is marked
 * "unstable", so no TSC clocksource is created and the selected Hyper-V
 * clocksource will be the default.
 */

u64 (*hv_read_reference_counter)(void);
EXPORT_SYMBOL_GPL(hv_read_reference_counter);

static union {
	struct ms_hyperv_tsc_page page;
	u8 reserved[PAGE_SIZE];
} tsc_pg __aligned(PAGE_SIZE);

struct ms_hyperv_tsc_page *hv_get_tsc_page(void)
{
	return &tsc_pg.page;
}
EXPORT_SYMBOL_GPL(hv_get_tsc_page);

static u64 notrace read_hv_clock_tsc(void)
{
	u64 current_tick = hv_read_tsc_page(hv_get_tsc_page());

	if (current_tick == U64_MAX)
		hv_get_time_ref_count(current_tick);

	return current_tick;
}

static u64 notrace read_hv_clock_tsc_cs(struct clocksource *arg)
{
	return read_hv_clock_tsc();
}

static u64 notrace read_hv_sched_clock_tsc(void)
{
	return (read_hv_clock_tsc() - hv_sched_clock_offset) *
		(NSEC_PER_SEC / HV_CLOCK_HZ);
}

static void suspend_hv_clock_tsc(struct clocksource *arg)
{
	u64 tsc_msr;

	/* Disable the TSC page */
	hv_get_reference_tsc(tsc_msr);
	tsc_msr &= ~BIT_ULL(0);
	hv_set_reference_tsc(tsc_msr);
}


static void resume_hv_clock_tsc(struct clocksource *arg)
{
	phys_addr_t phys_addr = virt_to_phys(&tsc_pg);
	u64 tsc_msr;

	/* Re-enable the TSC page */
	hv_get_reference_tsc(tsc_msr);
	tsc_msr &= GENMASK_ULL(11, 0);
	tsc_msr |= BIT_ULL(0) | (u64)phys_addr;
	hv_set_reference_tsc(tsc_msr);
}

static int hv_cs_enable(struct clocksource *cs)
{
	hv_enable_vdso_clocksource();
	return 0;
}

static struct clocksource hyperv_cs_tsc = {
	.name	= "hyperv_clocksource_tsc_page",
	.rating	= 250,
	.read	= read_hv_clock_tsc_cs,
	.mask	= CLOCKSOURCE_MASK(64),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
	.suspend= suspend_hv_clock_tsc,
	.resume	= resume_hv_clock_tsc,
	.enable = hv_cs_enable,
};

static u64 notrace read_hv_clock_msr(void)
{
	u64 current_tick;
	/*
	 * Read the partition counter to get the current tick count. This count
	 * is set to 0 when the partition is created and is incremented in
	 * 100 nanosecond units.
	 */
	hv_get_time_ref_count(current_tick);
	return current_tick;
}

static u64 notrace read_hv_clock_msr_cs(struct clocksource *arg)
{
	return read_hv_clock_msr();
}

static u64 notrace read_hv_sched_clock_msr(void)
{
	return (read_hv_clock_msr() - hv_sched_clock_offset) *
		(NSEC_PER_SEC / HV_CLOCK_HZ);
}

static struct clocksource hyperv_cs_msr = {
	.name	= "hyperv_clocksource_msr",
	.rating	= 250,
	.read	= read_hv_clock_msr_cs,
	.mask	= CLOCKSOURCE_MASK(64),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

static bool __init hv_init_tsc_clocksource(void)
{
	u64		tsc_msr;
	phys_addr_t	phys_addr;

	if (!(ms_hyperv.features & HV_MSR_REFERENCE_TSC_AVAILABLE))
		return false;

	hv_read_reference_counter = read_hv_clock_tsc;
	phys_addr = virt_to_phys(hv_get_tsc_page());

	/*
	 * The Hyper-V TLFS specifies to preserve the value of reserved
	 * bits in registers. So read the existing value, preserve the
	 * low order 12 bits, and add in the guest physical address
	 * (which already has at least the low 12 bits set to zero since
	 * it is page aligned). Also set the "enable" bit, which is bit 0.
	 */
	hv_get_reference_tsc(tsc_msr);
	tsc_msr &= GENMASK_ULL(11, 0);
	tsc_msr = tsc_msr | 0x1 | (u64)phys_addr;
	hv_set_reference_tsc(tsc_msr);

	hv_set_clocksource_vdso(hyperv_cs_tsc);
	clocksource_register_hz(&hyperv_cs_tsc, NSEC_PER_SEC/100);

	hv_sched_clock_offset = hv_read_reference_counter();
	hv_setup_sched_clock(read_hv_sched_clock_tsc);

	return true;
}

void __init hv_init_clocksource(void)
{
	/*
	 * Try to set up the TSC page clocksource. If it succeeds, we're
	 * done. Otherwise, set up the MSR clocksoruce.  At least one of
	 * these will always be available except on very old versions of
	 * Hyper-V on x86.  In that case we won't have a Hyper-V
	 * clocksource, but Linux will still run with a clocksource based
	 * on the emulated PIT or LAPIC timer.
	 */
	if (hv_init_tsc_clocksource())
		return;

	if (!(ms_hyperv.features & HV_MSR_TIME_REF_COUNT_AVAILABLE))
		return;

	hv_read_reference_counter = read_hv_clock_msr;
	clocksource_register_hz(&hyperv_cs_msr, NSEC_PER_SEC/100);

	hv_sched_clock_offset = hv_read_reference_counter();
	hv_setup_sched_clock(read_hv_sched_clock_msr);
}
