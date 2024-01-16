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
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/acpi.h>
#include <linux/hyperv.h>
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

static int stimer0_irq = -1;
static int stimer0_message_sint;
static DEFINE_PER_CPU(long, stimer0_evt);

/*
 * Common code for stimer0 interrupts coming via Direct Mode or
 * as a VMbus message.
 */
void hv_stimer0_isr(void)
{
	struct clock_event_device *ce;

	ce = this_cpu_ptr(hv_clock_event);
	ce->event_handler(ce);
}
EXPORT_SYMBOL_GPL(hv_stimer0_isr);

/*
 * stimer0 interrupt handler for architectures that support
 * per-cpu interrupts, which also implies Direct Mode.
 */
static irqreturn_t hv_stimer0_percpu_isr(int irq, void *dev_id)
{
	hv_stimer0_isr();
	return IRQ_HANDLED;
}

static int hv_ce_set_next_event(unsigned long delta,
				struct clock_event_device *evt)
{
	u64 current_tick;

	current_tick = hv_read_reference_counter();
	current_tick += delta;
	hv_set_register(HV_REGISTER_STIMER0_COUNT, current_tick);
	return 0;
}

static int hv_ce_shutdown(struct clock_event_device *evt)
{
	hv_set_register(HV_REGISTER_STIMER0_COUNT, 0);
	hv_set_register(HV_REGISTER_STIMER0_CONFIG, 0);
	if (direct_mode_enabled && stimer0_irq >= 0)
		disable_percpu_irq(stimer0_irq);

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
		timer_cfg.apic_vector = HYPERV_STIMER0_VECTOR;
		if (stimer0_irq >= 0)
			enable_percpu_irq(stimer0_irq, IRQ_TYPE_NONE);
	} else {
		/*
		 * When it expires, the timer will generate a VMbus message,
		 * to be handled by the normal VMbus interrupt handler.
		 */
		timer_cfg.direct_mode = 0;
		timer_cfg.sintx = stimer0_message_sint;
	}
	hv_set_register(HV_REGISTER_STIMER0_CONFIG, timer_cfg.as_uint64);
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

/*
 * These placeholders are overridden by arch specific code on
 * architectures that need special setup of the stimer0 IRQ because
 * they don't support per-cpu IRQs (such as x86/x64).
 */
void __weak hv_setup_stimer0_handler(void (*handler)(void))
{
};

void __weak hv_remove_stimer0_handler(void)
{
};

/* Called only on architectures with per-cpu IRQs (i.e., not x86/x64) */
static int hv_setup_stimer0_irq(void)
{
	int ret;

	ret = acpi_register_gsi(NULL, HYPERV_STIMER0_VECTOR,
			ACPI_EDGE_SENSITIVE, ACPI_ACTIVE_HIGH);
	if (ret < 0) {
		pr_err("Can't register Hyper-V stimer0 GSI. Error %d", ret);
		return ret;
	}
	stimer0_irq = ret;

	ret = request_percpu_irq(stimer0_irq, hv_stimer0_percpu_isr,
		"Hyper-V stimer0", &stimer0_evt);
	if (ret) {
		pr_err("Can't request Hyper-V stimer0 IRQ %d. Error %d",
			stimer0_irq, ret);
		acpi_unregister_gsi(stimer0_irq);
		stimer0_irq = -1;
	}
	return ret;
}

static void hv_remove_stimer0_irq(void)
{
	if (stimer0_irq == -1) {
		hv_remove_stimer0_handler();
	} else {
		free_percpu_irq(stimer0_irq, &stimer0_evt);
		acpi_unregister_gsi(stimer0_irq);
		stimer0_irq = -1;
	}
}

/* hv_stimer_alloc - Global initialization of the clockevent and stimer0 */
int hv_stimer_alloc(bool have_percpu_irqs)
{
	int ret;

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

	/*
	 * If Direct Mode isn't enabled, the remainder of the initialization
	 * is done later by hv_stimer_legacy_init()
	 */
	if (!direct_mode_enabled)
		return 0;

	if (have_percpu_irqs) {
		ret = hv_setup_stimer0_irq();
		if (ret)
			goto free_clock_event;
	} else {
		hv_setup_stimer0_handler(hv_stimer0_isr);
	}

	/*
	 * Since we are in Direct Mode, stimer initialization
	 * can be done now with a CPUHP value in the same range
	 * as other clockevent devices.
	 */
	ret = cpuhp_setup_state(CPUHP_AP_HYPERV_TIMER_STARTING,
			"clockevents/hyperv/stimer:starting",
			hv_stimer_init, hv_stimer_cleanup);
	if (ret < 0) {
		hv_remove_stimer0_irq();
		goto free_clock_event;
	}
	return ret;

free_clock_event:
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

	if (!hv_clock_event)
		return;

	if (direct_mode_enabled) {
		cpuhp_remove_state(CPUHP_AP_HYPERV_TIMER_STARTING);
		hv_remove_stimer0_irq();
		stimer0_irq = -1;
	}
	free_percpu(hv_clock_event);
	hv_clock_event = NULL;

}
EXPORT_SYMBOL_GPL(hv_stimer_global_cleanup);

/*
 * Code and definitions for the Hyper-V clocksources.  Two
 * clocksources are defined: one that reads the Hyper-V defined MSR, and
 * the other that uses the TSC reference page feature as defined in the
 * TLFS.  The MSR version is for compatibility with old versions of
 * Hyper-V and 32-bit x86.  The TSC reference page version is preferred.
 */

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
		current_tick = hv_get_register(HV_REGISTER_TIME_REF_COUNT);

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
	union hv_reference_tsc_msr tsc_msr;

	/* Disable the TSC page */
	tsc_msr.as_uint64 = hv_get_register(HV_REGISTER_REFERENCE_TSC);
	tsc_msr.enable = 0;
	hv_set_register(HV_REGISTER_REFERENCE_TSC, tsc_msr.as_uint64);
}


static void resume_hv_clock_tsc(struct clocksource *arg)
{
	phys_addr_t phys_addr = virt_to_phys(&tsc_pg);
	union hv_reference_tsc_msr tsc_msr;

	/* Re-enable the TSC page */
	tsc_msr.as_uint64 = hv_get_register(HV_REGISTER_REFERENCE_TSC);
	tsc_msr.enable = 1;
	tsc_msr.pfn = HVPFN_DOWN(phys_addr);
	hv_set_register(HV_REGISTER_REFERENCE_TSC, tsc_msr.as_uint64);
}

#ifdef HAVE_VDSO_CLOCKMODE_HVCLOCK
static int hv_cs_enable(struct clocksource *cs)
{
	vclocks_set_used(VDSO_CLOCKMODE_HVCLOCK);
	return 0;
}
#endif

static struct clocksource hyperv_cs_tsc = {
	.name	= "hyperv_clocksource_tsc_page",
	.rating	= 500,
	.read	= read_hv_clock_tsc_cs,
	.mask	= CLOCKSOURCE_MASK(64),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
	.suspend= suspend_hv_clock_tsc,
	.resume	= resume_hv_clock_tsc,
#ifdef HAVE_VDSO_CLOCKMODE_HVCLOCK
	.enable = hv_cs_enable,
	.vdso_clock_mode = VDSO_CLOCKMODE_HVCLOCK,
#else
	.vdso_clock_mode = VDSO_CLOCKMODE_NONE,
#endif
};

static u64 notrace read_hv_clock_msr(void)
{
	/*
	 * Read the partition counter to get the current tick count. This count
	 * is set to 0 when the partition is created and is incremented in
	 * 100 nanosecond units.
	 */
	return hv_get_register(HV_REGISTER_TIME_REF_COUNT);
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
	.rating	= 500,
	.read	= read_hv_clock_msr_cs,
	.mask	= CLOCKSOURCE_MASK(64),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

/*
 * Reference to pv_ops must be inline so objtool
 * detection of noinstr violations can work correctly.
 */
#ifdef CONFIG_GENERIC_SCHED_CLOCK
static __always_inline void hv_setup_sched_clock(void *sched_clock)
{
	/*
	 * We're on an architecture with generic sched clock (not x86/x64).
	 * The Hyper-V sched clock read function returns nanoseconds, not
	 * the normal 100ns units of the Hyper-V synthetic clock.
	 */
	sched_clock_register(sched_clock, 64, NSEC_PER_SEC);
}
#elif defined CONFIG_PARAVIRT
static __always_inline void hv_setup_sched_clock(void *sched_clock)
{
	/* We're on x86/x64 *and* using PV ops */
	paravirt_set_sched_clock(sched_clock);
}
#else /* !CONFIG_GENERIC_SCHED_CLOCK && !CONFIG_PARAVIRT */
static __always_inline void hv_setup_sched_clock(void *sched_clock) {}
#endif /* CONFIG_GENERIC_SCHED_CLOCK */

static bool __init hv_init_tsc_clocksource(void)
{
	union hv_reference_tsc_msr tsc_msr;
	phys_addr_t	phys_addr;

	if (!(ms_hyperv.features & HV_MSR_REFERENCE_TSC_AVAILABLE))
		return false;

	if (hv_root_partition)
		return false;

	/*
	 * If Hyper-V offers TSC_INVARIANT, then the virtualized TSC correctly
	 * handles frequency and offset changes due to live migration,
	 * pause/resume, and other VM management operations.  So lower the
	 * Hyper-V Reference TSC rating, causing the generic TSC to be used.
	 * TSC_INVARIANT is not offered on ARM64, so the Hyper-V Reference
	 * TSC will be preferred over the virtualized ARM64 arch counter.
	 * While the Hyper-V MSR clocksource won't be used since the
	 * Reference TSC clocksource is present, change its rating as
	 * well for consistency.
	 */
	if (ms_hyperv.features & HV_ACCESS_TSC_INVARIANT) {
		hyperv_cs_tsc.rating = 250;
		hyperv_cs_msr.rating = 250;
	}

	hv_read_reference_counter = read_hv_clock_tsc;
	phys_addr = virt_to_phys(hv_get_tsc_page());

	/*
	 * The Hyper-V TLFS specifies to preserve the value of reserved
	 * bits in registers. So read the existing value, preserve the
	 * low order 12 bits, and add in the guest physical address
	 * (which already has at least the low 12 bits set to zero since
	 * it is page aligned). Also set the "enable" bit, which is bit 0.
	 */
	tsc_msr.as_uint64 = hv_get_register(HV_REGISTER_REFERENCE_TSC);
	tsc_msr.enable = 1;
	tsc_msr.pfn = HVPFN_DOWN(phys_addr);
	hv_set_register(HV_REGISTER_REFERENCE_TSC, tsc_msr.as_uint64);

	clocksource_register_hz(&hyperv_cs_tsc, NSEC_PER_SEC/100);

	hv_sched_clock_offset = hv_read_reference_counter();
	hv_setup_sched_clock(read_hv_sched_clock_tsc);

	return true;
}

void __init hv_init_clocksource(void)
{
	/*
	 * Try to set up the TSC page clocksource. If it succeeds, we're
	 * done. Otherwise, set up the MSR clocksource.  At least one of
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
