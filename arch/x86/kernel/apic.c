/*
 *	Local APIC handling, local APIC timers
 *
 *	(c) 1999, 2000 Ingo Molnar <mingo@redhat.com>
 *
 *	Fixes
 *	Maciej W. Rozycki	:	Bits for genuine 82489DX APICs;
 *					thanks to Eric Gilmore
 *					and Rolf G. Tews
 *					for testing these extensively.
 *	Maciej W. Rozycki	:	Various updates and fixes.
 *	Mikael Pettersson	:	Power Management for UP-APIC.
 *	Pavel Machek and
 *	Mikael Pettersson	:	PM converted to driver model.
 */

#include <linux/init.h>

#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/bootmem.h>
#include <linux/interrupt.h>
#include <linux/mc146818rtc.h>
#include <linux/kernel_stat.h>
#include <linux/sysdev.h>
#include <linux/ioport.h>
#include <linux/cpu.h>
#include <linux/clockchips.h>
#include <linux/acpi_pmtmr.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/dmar.h>
#include <linux/ftrace.h>
#include <linux/smp.h>
#include <linux/nmi.h>
#include <linux/timex.h>

#include <asm/atomic.h>
#include <asm/mtrr.h>
#include <asm/mpspec.h>
#include <asm/desc.h>
#include <asm/arch_hooks.h>
#include <asm/hpet.h>
#include <asm/pgalloc.h>
#include <asm/i8253.h>
#include <asm/idle.h>
#include <asm/proto.h>
#include <asm/apic.h>
#include <asm/i8259.h>
#include <asm/smp.h>

#include <mach_apic.h>
#include <mach_apicdef.h>
#include <mach_ipi.h>

/*
 * Sanity check
 */
#if ((SPURIOUS_APIC_VECTOR & 0x0F) != 0x0F)
# error SPURIOUS_APIC_VECTOR definition error
#endif

#ifdef CONFIG_X86_32
/*
 * Knob to control our willingness to enable the local APIC.
 *
 * +1=force-enable
 */
static int force_enable_local_apic;
/*
 * APIC command line parameters
 */
static int __init parse_lapic(char *arg)
{
	force_enable_local_apic = 1;
	return 0;
}
early_param("lapic", parse_lapic);
/* Local APIC was disabled by the BIOS and enabled by the kernel */
static int enabled_via_apicbase;

#endif

#ifdef CONFIG_X86_64
static int apic_calibrate_pmtmr __initdata;
static __init int setup_apicpmtimer(char *s)
{
	apic_calibrate_pmtmr = 1;
	notsc_setup(NULL);
	return 0;
}
__setup("apicpmtimer", setup_apicpmtimer);
#endif

#ifdef CONFIG_X86_64
#define HAVE_X2APIC
#endif

#ifdef HAVE_X2APIC
int x2apic;
/* x2apic enabled before OS handover */
static int x2apic_preenabled;
static int disable_x2apic;
static __init int setup_nox2apic(char *str)
{
	disable_x2apic = 1;
	setup_clear_cpu_cap(X86_FEATURE_X2APIC);
	return 0;
}
early_param("nox2apic", setup_nox2apic);
#endif

unsigned long mp_lapic_addr;
int disable_apic;
/* Disable local APIC timer from the kernel commandline or via dmi quirk */
static int disable_apic_timer __cpuinitdata;
/* Local APIC timer works in C2 */
int local_apic_timer_c2_ok;
EXPORT_SYMBOL_GPL(local_apic_timer_c2_ok);

int first_system_vector = 0xfe;

/*
 * Debug level, exported for io_apic.c
 */
unsigned int apic_verbosity;

int pic_mode;

/* Have we found an MP table */
int smp_found_config;

static struct resource lapic_resource = {
	.name = "Local APIC",
	.flags = IORESOURCE_MEM | IORESOURCE_BUSY,
};

static unsigned int calibration_result;

static int lapic_next_event(unsigned long delta,
			    struct clock_event_device *evt);
static void lapic_timer_setup(enum clock_event_mode mode,
			      struct clock_event_device *evt);
static void lapic_timer_broadcast(const struct cpumask *mask);
static void apic_pm_activate(void);

/*
 * The local apic timer can be used for any function which is CPU local.
 */
static struct clock_event_device lapic_clockevent = {
	.name		= "lapic",
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT
			| CLOCK_EVT_FEAT_C3STOP | CLOCK_EVT_FEAT_DUMMY,
	.shift		= 32,
	.set_mode	= lapic_timer_setup,
	.set_next_event	= lapic_next_event,
	.broadcast	= lapic_timer_broadcast,
	.rating		= 100,
	.irq		= -1,
};
static DEFINE_PER_CPU(struct clock_event_device, lapic_events);

static unsigned long apic_phys;

/*
 * Get the LAPIC version
 */
static inline int lapic_get_version(void)
{
	return GET_APIC_VERSION(apic_read(APIC_LVR));
}

/*
 * Check, if the APIC is integrated or a separate chip
 */
static inline int lapic_is_integrated(void)
{
#ifdef CONFIG_X86_64
	return 1;
#else
	return APIC_INTEGRATED(lapic_get_version());
#endif
}

/*
 * Check, whether this is a modern or a first generation APIC
 */
static int modern_apic(void)
{
	/* AMD systems use old APIC versions, so check the CPU */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD &&
	    boot_cpu_data.x86 >= 0xf)
		return 1;
	return lapic_get_version() >= 0x14;
}

/*
 * Paravirt kernels also might be using these below ops. So we still
 * use generic apic_read()/apic_write(), which might be pointing to different
 * ops in PARAVIRT case.
 */
void xapic_wait_icr_idle(void)
{
	while (apic_read(APIC_ICR) & APIC_ICR_BUSY)
		cpu_relax();
}

u32 safe_xapic_wait_icr_idle(void)
{
	u32 send_status;
	int timeout;

	timeout = 0;
	do {
		send_status = apic_read(APIC_ICR) & APIC_ICR_BUSY;
		if (!send_status)
			break;
		udelay(100);
	} while (timeout++ < 1000);

	return send_status;
}

void xapic_icr_write(u32 low, u32 id)
{
	apic_write(APIC_ICR2, SET_APIC_DEST_FIELD(id));
	apic_write(APIC_ICR, low);
}

static u64 xapic_icr_read(void)
{
	u32 icr1, icr2;

	icr2 = apic_read(APIC_ICR2);
	icr1 = apic_read(APIC_ICR);

	return icr1 | ((u64)icr2 << 32);
}

static struct apic_ops xapic_ops = {
	.read = native_apic_mem_read,
	.write = native_apic_mem_write,
	.icr_read = xapic_icr_read,
	.icr_write = xapic_icr_write,
	.wait_icr_idle = xapic_wait_icr_idle,
	.safe_wait_icr_idle = safe_xapic_wait_icr_idle,
};

struct apic_ops __read_mostly *apic_ops = &xapic_ops;
EXPORT_SYMBOL_GPL(apic_ops);

#ifdef HAVE_X2APIC
static void x2apic_wait_icr_idle(void)
{
	/* no need to wait for icr idle in x2apic */
	return;
}

static u32 safe_x2apic_wait_icr_idle(void)
{
	/* no need to wait for icr idle in x2apic */
	return 0;
}

void x2apic_icr_write(u32 low, u32 id)
{
	wrmsrl(APIC_BASE_MSR + (APIC_ICR >> 4), ((__u64) id) << 32 | low);
}

static u64 x2apic_icr_read(void)
{
	unsigned long val;

	rdmsrl(APIC_BASE_MSR + (APIC_ICR >> 4), val);
	return val;
}

static struct apic_ops x2apic_ops = {
	.read = native_apic_msr_read,
	.write = native_apic_msr_write,
	.icr_read = x2apic_icr_read,
	.icr_write = x2apic_icr_write,
	.wait_icr_idle = x2apic_wait_icr_idle,
	.safe_wait_icr_idle = safe_x2apic_wait_icr_idle,
};
#endif

/**
 * enable_NMI_through_LVT0 - enable NMI through local vector table 0
 */
void __cpuinit enable_NMI_through_LVT0(void)
{
	unsigned int v;

	/* unmask and set to NMI */
	v = APIC_DM_NMI;

	/* Level triggered for 82489DX (32bit mode) */
	if (!lapic_is_integrated())
		v |= APIC_LVT_LEVEL_TRIGGER;

	apic_write(APIC_LVT0, v);
}

#ifdef CONFIG_X86_32
/**
 * get_physical_broadcast - Get number of physical broadcast IDs
 */
int get_physical_broadcast(void)
{
	return modern_apic() ? 0xff : 0xf;
}
#endif

/**
 * lapic_get_maxlvt - get the maximum number of local vector table entries
 */
int lapic_get_maxlvt(void)
{
	unsigned int v;

	v = apic_read(APIC_LVR);
	/*
	 * - we always have APIC integrated on 64bit mode
	 * - 82489DXs do not report # of LVT entries
	 */
	return APIC_INTEGRATED(GET_APIC_VERSION(v)) ? GET_APIC_MAXLVT(v) : 2;
}

/*
 * Local APIC timer
 */

/* Clock divisor */
#define APIC_DIVISOR 16

/*
 * This function sets up the local APIC timer, with a timeout of
 * 'clocks' APIC bus clock. During calibration we actually call
 * this function twice on the boot CPU, once with a bogus timeout
 * value, second time for real. The other (noncalibrating) CPUs
 * call this function only once, with the real, calibrated value.
 *
 * We do reads before writes even if unnecessary, to get around the
 * P5 APIC double write bug.
 */
static void __setup_APIC_LVTT(unsigned int clocks, int oneshot, int irqen)
{
	unsigned int lvtt_value, tmp_value;

	lvtt_value = LOCAL_TIMER_VECTOR;
	if (!oneshot)
		lvtt_value |= APIC_LVT_TIMER_PERIODIC;
	if (!lapic_is_integrated())
		lvtt_value |= SET_APIC_TIMER_BASE(APIC_TIMER_BASE_DIV);

	if (!irqen)
		lvtt_value |= APIC_LVT_MASKED;

	apic_write(APIC_LVTT, lvtt_value);

	/*
	 * Divide PICLK by 16
	 */
	tmp_value = apic_read(APIC_TDCR);
	apic_write(APIC_TDCR,
		(tmp_value & ~(APIC_TDR_DIV_1 | APIC_TDR_DIV_TMBASE)) |
		APIC_TDR_DIV_16);

	if (!oneshot)
		apic_write(APIC_TMICT, clocks / APIC_DIVISOR);
}

/*
 * Setup extended LVT, AMD specific (K8, family 10h)
 *
 * Vector mappings are hard coded. On K8 only offset 0 (APIC500) and
 * MCE interrupts are supported. Thus MCE offset must be set to 0.
 *
 * If mask=1, the LVT entry does not generate interrupts while mask=0
 * enables the vector. See also the BKDGs.
 */

#define APIC_EILVT_LVTOFF_MCE 0
#define APIC_EILVT_LVTOFF_IBS 1

static void setup_APIC_eilvt(u8 lvt_off, u8 vector, u8 msg_type, u8 mask)
{
	unsigned long reg = (lvt_off << 4) + APIC_EILVT0;
	unsigned int  v   = (mask << 16) | (msg_type << 8) | vector;

	apic_write(reg, v);
}

u8 setup_APIC_eilvt_mce(u8 vector, u8 msg_type, u8 mask)
{
	setup_APIC_eilvt(APIC_EILVT_LVTOFF_MCE, vector, msg_type, mask);
	return APIC_EILVT_LVTOFF_MCE;
}

u8 setup_APIC_eilvt_ibs(u8 vector, u8 msg_type, u8 mask)
{
	setup_APIC_eilvt(APIC_EILVT_LVTOFF_IBS, vector, msg_type, mask);
	return APIC_EILVT_LVTOFF_IBS;
}
EXPORT_SYMBOL_GPL(setup_APIC_eilvt_ibs);

/*
 * Program the next event, relative to now
 */
static int lapic_next_event(unsigned long delta,
			    struct clock_event_device *evt)
{
	apic_write(APIC_TMICT, delta);
	return 0;
}

/*
 * Setup the lapic timer in periodic or oneshot mode
 */
static void lapic_timer_setup(enum clock_event_mode mode,
			      struct clock_event_device *evt)
{
	unsigned long flags;
	unsigned int v;

	/* Lapic used as dummy for broadcast ? */
	if (evt->features & CLOCK_EVT_FEAT_DUMMY)
		return;

	local_irq_save(flags);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
	case CLOCK_EVT_MODE_ONESHOT:
		__setup_APIC_LVTT(calibration_result,
				  mode != CLOCK_EVT_MODE_PERIODIC, 1);
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		v = apic_read(APIC_LVTT);
		v |= (APIC_LVT_MASKED | LOCAL_TIMER_VECTOR);
		apic_write(APIC_LVTT, v);
		apic_write(APIC_TMICT, 0xffffffff);
		break;
	case CLOCK_EVT_MODE_RESUME:
		/* Nothing to do here */
		break;
	}

	local_irq_restore(flags);
}

/*
 * Local APIC timer broadcast function
 */
static void lapic_timer_broadcast(const struct cpumask *mask)
{
#ifdef CONFIG_SMP
	send_IPI_mask(mask, LOCAL_TIMER_VECTOR);
#endif
}

/*
 * Setup the local APIC timer for this CPU. Copy the initilized values
 * of the boot CPU and register the clock event in the framework.
 */
static void __cpuinit setup_APIC_timer(void)
{
	struct clock_event_device *levt = &__get_cpu_var(lapic_events);

	memcpy(levt, &lapic_clockevent, sizeof(*levt));
	levt->cpumask = cpumask_of(smp_processor_id());

	clockevents_register_device(levt);
}

/*
 * In this functions we calibrate APIC bus clocks to the external timer.
 *
 * We want to do the calibration only once since we want to have local timer
 * irqs syncron. CPUs connected by the same APIC bus have the very same bus
 * frequency.
 *
 * This was previously done by reading the PIT/HPET and waiting for a wrap
 * around to find out, that a tick has elapsed. I have a box, where the PIT
 * readout is broken, so it never gets out of the wait loop again. This was
 * also reported by others.
 *
 * Monitoring the jiffies value is inaccurate and the clockevents
 * infrastructure allows us to do a simple substitution of the interrupt
 * handler.
 *
 * The calibration routine also uses the pm_timer when possible, as the PIT
 * happens to run way too slow (factor 2.3 on my VAIO CoreDuo, which goes
 * back to normal later in the boot process).
 */

#define LAPIC_CAL_LOOPS		(HZ/10)

static __initdata int lapic_cal_loops = -1;
static __initdata long lapic_cal_t1, lapic_cal_t2;
static __initdata unsigned long long lapic_cal_tsc1, lapic_cal_tsc2;
static __initdata unsigned long lapic_cal_pm1, lapic_cal_pm2;
static __initdata unsigned long lapic_cal_j1, lapic_cal_j2;

/*
 * Temporary interrupt handler.
 */
static void __init lapic_cal_handler(struct clock_event_device *dev)
{
	unsigned long long tsc = 0;
	long tapic = apic_read(APIC_TMCCT);
	unsigned long pm = acpi_pm_read_early();

	if (cpu_has_tsc)
		rdtscll(tsc);

	switch (lapic_cal_loops++) {
	case 0:
		lapic_cal_t1 = tapic;
		lapic_cal_tsc1 = tsc;
		lapic_cal_pm1 = pm;
		lapic_cal_j1 = jiffies;
		break;

	case LAPIC_CAL_LOOPS:
		lapic_cal_t2 = tapic;
		lapic_cal_tsc2 = tsc;
		if (pm < lapic_cal_pm1)
			pm += ACPI_PM_OVRRUN;
		lapic_cal_pm2 = pm;
		lapic_cal_j2 = jiffies;
		break;
	}
}

static int __init calibrate_by_pmtimer(long deltapm, long *delta)
{
	const long pm_100ms = PMTMR_TICKS_PER_SEC / 10;
	const long pm_thresh = pm_100ms / 100;
	unsigned long mult;
	u64 res;

#ifndef CONFIG_X86_PM_TIMER
	return -1;
#endif

	apic_printk(APIC_VERBOSE, "... PM timer delta = %ld\n", deltapm);

	/* Check, if the PM timer is available */
	if (!deltapm)
		return -1;

	mult = clocksource_hz2mult(PMTMR_TICKS_PER_SEC, 22);

	if (deltapm > (pm_100ms - pm_thresh) &&
	    deltapm < (pm_100ms + pm_thresh)) {
		apic_printk(APIC_VERBOSE, "... PM timer result ok\n");
	} else {
		res = (((u64)deltapm) *  mult) >> 22;
		do_div(res, 1000000);
		pr_warning("APIC calibration not consistent "
			"with PM Timer: %ldms instead of 100ms\n",
			(long)res);
		/* Correct the lapic counter value */
		res = (((u64)(*delta)) * pm_100ms);
		do_div(res, deltapm);
		pr_info("APIC delta adjusted to PM-Timer: "
			"%lu (%ld)\n", (unsigned long)res, *delta);
		*delta = (long)res;
	}

	return 0;
}

static int __init calibrate_APIC_clock(void)
{
	struct clock_event_device *levt = &__get_cpu_var(lapic_events);
	void (*real_handler)(struct clock_event_device *dev);
	unsigned long deltaj;
	long delta;
	int pm_referenced = 0;

	local_irq_disable();

	/* Replace the global interrupt handler */
	real_handler = global_clock_event->event_handler;
	global_clock_event->event_handler = lapic_cal_handler;

	/*
	 * Setup the APIC counter to maximum. There is no way the lapic
	 * can underflow in the 100ms detection time frame
	 */
	__setup_APIC_LVTT(0xffffffff, 0, 0);

	/* Let the interrupts run */
	local_irq_enable();

	while (lapic_cal_loops <= LAPIC_CAL_LOOPS)
		cpu_relax();

	local_irq_disable();

	/* Restore the real event handler */
	global_clock_event->event_handler = real_handler;

	/* Build delta t1-t2 as apic timer counts down */
	delta = lapic_cal_t1 - lapic_cal_t2;
	apic_printk(APIC_VERBOSE, "... lapic delta = %ld\n", delta);

	/* we trust the PM based calibration if possible */
	pm_referenced = !calibrate_by_pmtimer(lapic_cal_pm2 - lapic_cal_pm1,
					&delta);

	/* Calculate the scaled math multiplication factor */
	lapic_clockevent.mult = div_sc(delta, TICK_NSEC * LAPIC_CAL_LOOPS,
				       lapic_clockevent.shift);
	lapic_clockevent.max_delta_ns =
		clockevent_delta2ns(0x7FFFFF, &lapic_clockevent);
	lapic_clockevent.min_delta_ns =
		clockevent_delta2ns(0xF, &lapic_clockevent);

	calibration_result = (delta * APIC_DIVISOR) / LAPIC_CAL_LOOPS;

	apic_printk(APIC_VERBOSE, "..... delta %ld\n", delta);
	apic_printk(APIC_VERBOSE, "..... mult: %ld\n", lapic_clockevent.mult);
	apic_printk(APIC_VERBOSE, "..... calibration result: %u\n",
		    calibration_result);

	if (cpu_has_tsc) {
		delta = (long)(lapic_cal_tsc2 - lapic_cal_tsc1);
		apic_printk(APIC_VERBOSE, "..... CPU clock speed is "
			    "%ld.%04ld MHz.\n",
			    (delta / LAPIC_CAL_LOOPS) / (1000000 / HZ),
			    (delta / LAPIC_CAL_LOOPS) % (1000000 / HZ));
	}

	apic_printk(APIC_VERBOSE, "..... host bus clock speed is "
		    "%u.%04u MHz.\n",
		    calibration_result / (1000000 / HZ),
		    calibration_result % (1000000 / HZ));

	/*
	 * Do a sanity check on the APIC calibration result
	 */
	if (calibration_result < (1000000 / HZ)) {
		local_irq_enable();
		pr_warning("APIC frequency too slow, disabling apic timer\n");
		return -1;
	}

	levt->features &= ~CLOCK_EVT_FEAT_DUMMY;

	/*
	 * PM timer calibration failed or not turned on
	 * so lets try APIC timer based calibration
	 */
	if (!pm_referenced) {
		apic_printk(APIC_VERBOSE, "... verify APIC timer\n");

		/*
		 * Setup the apic timer manually
		 */
		levt->event_handler = lapic_cal_handler;
		lapic_timer_setup(CLOCK_EVT_MODE_PERIODIC, levt);
		lapic_cal_loops = -1;

		/* Let the interrupts run */
		local_irq_enable();

		while (lapic_cal_loops <= LAPIC_CAL_LOOPS)
			cpu_relax();

		/* Stop the lapic timer */
		lapic_timer_setup(CLOCK_EVT_MODE_SHUTDOWN, levt);

		/* Jiffies delta */
		deltaj = lapic_cal_j2 - lapic_cal_j1;
		apic_printk(APIC_VERBOSE, "... jiffies delta = %lu\n", deltaj);

		/* Check, if the jiffies result is consistent */
		if (deltaj >= LAPIC_CAL_LOOPS-2 && deltaj <= LAPIC_CAL_LOOPS+2)
			apic_printk(APIC_VERBOSE, "... jiffies result ok\n");
		else
			levt->features |= CLOCK_EVT_FEAT_DUMMY;
	} else
		local_irq_enable();

	if (levt->features & CLOCK_EVT_FEAT_DUMMY) {
		pr_warning("APIC timer disabled due to verification failure\n");
			return -1;
	}

	return 0;
}

/*
 * Setup the boot APIC
 *
 * Calibrate and verify the result.
 */
void __init setup_boot_APIC_clock(void)
{
	/*
	 * The local apic timer can be disabled via the kernel
	 * commandline or from the CPU detection code. Register the lapic
	 * timer as a dummy clock event source on SMP systems, so the
	 * broadcast mechanism is used. On UP systems simply ignore it.
	 */
	if (disable_apic_timer) {
		pr_info("Disabling APIC timer\n");
		/* No broadcast on UP ! */
		if (num_possible_cpus() > 1) {
			lapic_clockevent.mult = 1;
			setup_APIC_timer();
		}
		return;
	}

	apic_printk(APIC_VERBOSE, "Using local APIC timer interrupts.\n"
		    "calibrating APIC timer ...\n");

	if (calibrate_APIC_clock()) {
		/* No broadcast on UP ! */
		if (num_possible_cpus() > 1)
			setup_APIC_timer();
		return;
	}

	/*
	 * If nmi_watchdog is set to IO_APIC, we need the
	 * PIT/HPET going.  Otherwise register lapic as a dummy
	 * device.
	 */
	if (nmi_watchdog != NMI_IO_APIC)
		lapic_clockevent.features &= ~CLOCK_EVT_FEAT_DUMMY;
	else
		pr_warning("APIC timer registered as dummy,"
			" due to nmi_watchdog=%d!\n", nmi_watchdog);

	/* Setup the lapic or request the broadcast */
	setup_APIC_timer();
}

void __cpuinit setup_secondary_APIC_clock(void)
{
	setup_APIC_timer();
}

/*
 * The guts of the apic timer interrupt
 */
static void local_apic_timer_interrupt(void)
{
	int cpu = smp_processor_id();
	struct clock_event_device *evt = &per_cpu(lapic_events, cpu);

	/*
	 * Normally we should not be here till LAPIC has been initialized but
	 * in some cases like kdump, its possible that there is a pending LAPIC
	 * timer interrupt from previous kernel's context and is delivered in
	 * new kernel the moment interrupts are enabled.
	 *
	 * Interrupts are enabled early and LAPIC is setup much later, hence
	 * its possible that when we get here evt->event_handler is NULL.
	 * Check for event_handler being NULL and discard the interrupt as
	 * spurious.
	 */
	if (!evt->event_handler) {
		pr_warning("Spurious LAPIC timer interrupt on cpu %d\n", cpu);
		/* Switch it off */
		lapic_timer_setup(CLOCK_EVT_MODE_SHUTDOWN, evt);
		return;
	}

	/*
	 * the NMI deadlock-detector uses this.
	 */
	inc_irq_stat(apic_timer_irqs);

	evt->event_handler(evt);
}

/*
 * Local APIC timer interrupt. This is the most natural way for doing
 * local interrupts, but local timer interrupts can be emulated by
 * broadcast interrupts too. [in case the hw doesn't support APIC timers]
 *
 * [ if a single-CPU system runs an SMP kernel then we call the local
 *   interrupt as well. Thus we cannot inline the local irq ... ]
 */
void __irq_entry smp_apic_timer_interrupt(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	/*
	 * NOTE! We'd better ACK the irq immediately,
	 * because timer handling can be slow.
	 */
	ack_APIC_irq();
	/*
	 * update_process_times() expects us to have done irq_enter().
	 * Besides, if we don't timer interrupts ignore the global
	 * interrupt lock, which is the WrongThing (tm) to do.
	 */
	exit_idle();
	irq_enter();
	local_apic_timer_interrupt();
	irq_exit();

	set_irq_regs(old_regs);
}

int setup_profiling_timer(unsigned int multiplier)
{
	return -EINVAL;
}

/*
 * Local APIC start and shutdown
 */

/**
 * clear_local_APIC - shutdown the local APIC
 *
 * This is called, when a CPU is disabled and before rebooting, so the state of
 * the local APIC has no dangling leftovers. Also used to cleanout any BIOS
 * leftovers during boot.
 */
void clear_local_APIC(void)
{
	int maxlvt;
	u32 v;

	/* APIC hasn't been mapped yet */
	if (!apic_phys)
		return;

	maxlvt = lapic_get_maxlvt();
	/*
	 * Masking an LVT entry can trigger a local APIC error
	 * if the vector is zero. Mask LVTERR first to prevent this.
	 */
	if (maxlvt >= 3) {
		v = ERROR_APIC_VECTOR; /* any non-zero vector will do */
		apic_write(APIC_LVTERR, v | APIC_LVT_MASKED);
	}
	/*
	 * Careful: we have to set masks only first to deassert
	 * any level-triggered sources.
	 */
	v = apic_read(APIC_LVTT);
	apic_write(APIC_LVTT, v | APIC_LVT_MASKED);
	v = apic_read(APIC_LVT0);
	apic_write(APIC_LVT0, v | APIC_LVT_MASKED);
	v = apic_read(APIC_LVT1);
	apic_write(APIC_LVT1, v | APIC_LVT_MASKED);
	if (maxlvt >= 4) {
		v = apic_read(APIC_LVTPC);
		apic_write(APIC_LVTPC, v | APIC_LVT_MASKED);
	}

	/* lets not touch this if we didn't frob it */
#if defined(CONFIG_X86_MCE_P4THERMAL) || defined(X86_MCE_INTEL)
	if (maxlvt >= 5) {
		v = apic_read(APIC_LVTTHMR);
		apic_write(APIC_LVTTHMR, v | APIC_LVT_MASKED);
	}
#endif
	/*
	 * Clean APIC state for other OSs:
	 */
	apic_write(APIC_LVTT, APIC_LVT_MASKED);
	apic_write(APIC_LVT0, APIC_LVT_MASKED);
	apic_write(APIC_LVT1, APIC_LVT_MASKED);
	if (maxlvt >= 3)
		apic_write(APIC_LVTERR, APIC_LVT_MASKED);
	if (maxlvt >= 4)
		apic_write(APIC_LVTPC, APIC_LVT_MASKED);

	/* Integrated APIC (!82489DX) ? */
	if (lapic_is_integrated()) {
		if (maxlvt > 3)
			/* Clear ESR due to Pentium errata 3AP and 11AP */
			apic_write(APIC_ESR, 0);
		apic_read(APIC_ESR);
	}
}

/**
 * disable_local_APIC - clear and disable the local APIC
 */
void disable_local_APIC(void)
{
	unsigned int value;

	clear_local_APIC();

	/*
	 * Disable APIC (implies clearing of registers
	 * for 82489DX!).
	 */
	value = apic_read(APIC_SPIV);
	value &= ~APIC_SPIV_APIC_ENABLED;
	apic_write(APIC_SPIV, value);

#ifdef CONFIG_X86_32
	/*
	 * When LAPIC was disabled by the BIOS and enabled by the kernel,
	 * restore the disabled state.
	 */
	if (enabled_via_apicbase) {
		unsigned int l, h;

		rdmsr(MSR_IA32_APICBASE, l, h);
		l &= ~MSR_IA32_APICBASE_ENABLE;
		wrmsr(MSR_IA32_APICBASE, l, h);
	}
#endif
}

/*
 * If Linux enabled the LAPIC against the BIOS default disable it down before
 * re-entering the BIOS on shutdown.  Otherwise the BIOS may get confused and
 * not power-off.  Additionally clear all LVT entries before disable_local_APIC
 * for the case where Linux didn't enable the LAPIC.
 */
void lapic_shutdown(void)
{
	unsigned long flags;

	if (!cpu_has_apic)
		return;

	local_irq_save(flags);

#ifdef CONFIG_X86_32
	if (!enabled_via_apicbase)
		clear_local_APIC();
	else
#endif
		disable_local_APIC();


	local_irq_restore(flags);
}

/*
 * This is to verify that we're looking at a real local APIC.
 * Check these against your board if the CPUs aren't getting
 * started for no apparent reason.
 */
int __init verify_local_APIC(void)
{
	unsigned int reg0, reg1;

	/*
	 * The version register is read-only in a real APIC.
	 */
	reg0 = apic_read(APIC_LVR);
	apic_printk(APIC_DEBUG, "Getting VERSION: %x\n", reg0);
	apic_write(APIC_LVR, reg0 ^ APIC_LVR_MASK);
	reg1 = apic_read(APIC_LVR);
	apic_printk(APIC_DEBUG, "Getting VERSION: %x\n", reg1);

	/*
	 * The two version reads above should print the same
	 * numbers.  If the second one is different, then we
	 * poke at a non-APIC.
	 */
	if (reg1 != reg0)
		return 0;

	/*
	 * Check if the version looks reasonably.
	 */
	reg1 = GET_APIC_VERSION(reg0);
	if (reg1 == 0x00 || reg1 == 0xff)
		return 0;
	reg1 = lapic_get_maxlvt();
	if (reg1 < 0x02 || reg1 == 0xff)
		return 0;

	/*
	 * The ID register is read/write in a real APIC.
	 */
	reg0 = apic_read(APIC_ID);
	apic_printk(APIC_DEBUG, "Getting ID: %x\n", reg0);
	apic_write(APIC_ID, reg0 ^ APIC_ID_MASK);
	reg1 = apic_read(APIC_ID);
	apic_printk(APIC_DEBUG, "Getting ID: %x\n", reg1);
	apic_write(APIC_ID, reg0);
	if (reg1 != (reg0 ^ APIC_ID_MASK))
		return 0;

	/*
	 * The next two are just to see if we have sane values.
	 * They're only really relevant if we're in Virtual Wire
	 * compatibility mode, but most boxes are anymore.
	 */
	reg0 = apic_read(APIC_LVT0);
	apic_printk(APIC_DEBUG, "Getting LVT0: %x\n", reg0);
	reg1 = apic_read(APIC_LVT1);
	apic_printk(APIC_DEBUG, "Getting LVT1: %x\n", reg1);

	return 1;
}

/**
 * sync_Arb_IDs - synchronize APIC bus arbitration IDs
 */
void __init sync_Arb_IDs(void)
{
	/*
	 * Unsupported on P4 - see Intel Dev. Manual Vol. 3, Ch. 8.6.1 And not
	 * needed on AMD.
	 */
	if (modern_apic() || boot_cpu_data.x86_vendor == X86_VENDOR_AMD)
		return;

	/*
	 * Wait for idle.
	 */
	apic_wait_icr_idle();

	apic_printk(APIC_DEBUG, "Synchronizing Arb IDs.\n");
	apic_write(APIC_ICR, APIC_DEST_ALLINC |
			APIC_INT_LEVELTRIG | APIC_DM_INIT);
}

/*
 * An initial setup of the virtual wire mode.
 */
void __init init_bsp_APIC(void)
{
	unsigned int value;

	/*
	 * Don't do the setup now if we have a SMP BIOS as the
	 * through-I/O-APIC virtual wire mode might be active.
	 */
	if (smp_found_config || !cpu_has_apic)
		return;

	/*
	 * Do not trust the local APIC being empty at bootup.
	 */
	clear_local_APIC();

	/*
	 * Enable APIC.
	 */
	value = apic_read(APIC_SPIV);
	value &= ~APIC_VECTOR_MASK;
	value |= APIC_SPIV_APIC_ENABLED;

#ifdef CONFIG_X86_32
	/* This bit is reserved on P4/Xeon and should be cleared */
	if ((boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) &&
	    (boot_cpu_data.x86 == 15))
		value &= ~APIC_SPIV_FOCUS_DISABLED;
	else
#endif
		value |= APIC_SPIV_FOCUS_DISABLED;
	value |= SPURIOUS_APIC_VECTOR;
	apic_write(APIC_SPIV, value);

	/*
	 * Set up the virtual wire mode.
	 */
	apic_write(APIC_LVT0, APIC_DM_EXTINT);
	value = APIC_DM_NMI;
	if (!lapic_is_integrated())		/* 82489DX */
		value |= APIC_LVT_LEVEL_TRIGGER;
	apic_write(APIC_LVT1, value);
}

static void __cpuinit lapic_setup_esr(void)
{
	unsigned int oldvalue, value, maxlvt;

	if (!lapic_is_integrated()) {
		pr_info("No ESR for 82489DX.\n");
		return;
	}

	if (esr_disable) {
		/*
		 * Something untraceable is creating bad interrupts on
		 * secondary quads ... for the moment, just leave the
		 * ESR disabled - we can't do anything useful with the
		 * errors anyway - mbligh
		 */
		pr_info("Leaving ESR disabled.\n");
		return;
	}

	maxlvt = lapic_get_maxlvt();
	if (maxlvt > 3)		/* Due to the Pentium erratum 3AP. */
		apic_write(APIC_ESR, 0);
	oldvalue = apic_read(APIC_ESR);

	/* enables sending errors */
	value = ERROR_APIC_VECTOR;
	apic_write(APIC_LVTERR, value);

	/*
	 * spec says clear errors after enabling vector.
	 */
	if (maxlvt > 3)
		apic_write(APIC_ESR, 0);
	value = apic_read(APIC_ESR);
	if (value != oldvalue)
		apic_printk(APIC_VERBOSE, "ESR value before enabling "
			"vector: 0x%08x  after: 0x%08x\n",
			oldvalue, value);
}


/**
 * setup_local_APIC - setup the local APIC
 */
void __cpuinit setup_local_APIC(void)
{
	unsigned int value;
	int i, j;

#ifdef CONFIG_X86_32
	/* Pound the ESR really hard over the head with a big hammer - mbligh */
	if (lapic_is_integrated() && esr_disable) {
		apic_write(APIC_ESR, 0);
		apic_write(APIC_ESR, 0);
		apic_write(APIC_ESR, 0);
		apic_write(APIC_ESR, 0);
	}
#endif

	preempt_disable();

	/*
	 * Double-check whether this APIC is really registered.
	 * This is meaningless in clustered apic mode, so we skip it.
	 */
	if (!apic_id_registered())
		BUG();

	/*
	 * Intel recommends to set DFR, LDR and TPR before enabling
	 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
	 * document number 292116).  So here it goes...
	 */
	init_apic_ldr();

	/*
	 * Set Task Priority to 'accept all'. We never change this
	 * later on.
	 */
	value = apic_read(APIC_TASKPRI);
	value &= ~APIC_TPRI_MASK;
	apic_write(APIC_TASKPRI, value);

	/*
	 * After a crash, we no longer service the interrupts and a pending
	 * interrupt from previous kernel might still have ISR bit set.
	 *
	 * Most probably by now CPU has serviced that pending interrupt and
	 * it might not have done the ack_APIC_irq() because it thought,
	 * interrupt came from i8259 as ExtInt. LAPIC did not get EOI so it
	 * does not clear the ISR bit and cpu thinks it has already serivced
	 * the interrupt. Hence a vector might get locked. It was noticed
	 * for timer irq (vector 0x31). Issue an extra EOI to clear ISR.
	 */
	for (i = APIC_ISR_NR - 1; i >= 0; i--) {
		value = apic_read(APIC_ISR + i*0x10);
		for (j = 31; j >= 0; j--) {
			if (value & (1<<j))
				ack_APIC_irq();
		}
	}

	/*
	 * Now that we are all set up, enable the APIC
	 */
	value = apic_read(APIC_SPIV);
	value &= ~APIC_VECTOR_MASK;
	/*
	 * Enable APIC
	 */
	value |= APIC_SPIV_APIC_ENABLED;

#ifdef CONFIG_X86_32
	/*
	 * Some unknown Intel IO/APIC (or APIC) errata is biting us with
	 * certain networking cards. If high frequency interrupts are
	 * happening on a particular IOAPIC pin, plus the IOAPIC routing
	 * entry is masked/unmasked at a high rate as well then sooner or
	 * later IOAPIC line gets 'stuck', no more interrupts are received
	 * from the device. If focus CPU is disabled then the hang goes
	 * away, oh well :-(
	 *
	 * [ This bug can be reproduced easily with a level-triggered
	 *   PCI Ne2000 networking cards and PII/PIII processors, dual
	 *   BX chipset. ]
	 */
	/*
	 * Actually disabling the focus CPU check just makes the hang less
	 * frequent as it makes the interrupt distributon model be more
	 * like LRU than MRU (the short-term load is more even across CPUs).
	 * See also the comment in end_level_ioapic_irq().  --macro
	 */

	/*
	 * - enable focus processor (bit==0)
	 * - 64bit mode always use processor focus
	 *   so no need to set it
	 */
	value &= ~APIC_SPIV_FOCUS_DISABLED;
#endif

	/*
	 * Set spurious IRQ vector
	 */
	value |= SPURIOUS_APIC_VECTOR;
	apic_write(APIC_SPIV, value);

	/*
	 * Set up LVT0, LVT1:
	 *
	 * set up through-local-APIC on the BP's LINT0. This is not
	 * strictly necessary in pure symmetric-IO mode, but sometimes
	 * we delegate interrupts to the 8259A.
	 */
	/*
	 * TODO: set up through-local-APIC from through-I/O-APIC? --macro
	 */
	value = apic_read(APIC_LVT0) & APIC_LVT_MASKED;
	if (!smp_processor_id() && (pic_mode || !value)) {
		value = APIC_DM_EXTINT;
		apic_printk(APIC_VERBOSE, "enabled ExtINT on CPU#%d\n",
				smp_processor_id());
	} else {
		value = APIC_DM_EXTINT | APIC_LVT_MASKED;
		apic_printk(APIC_VERBOSE, "masked ExtINT on CPU#%d\n",
				smp_processor_id());
	}
	apic_write(APIC_LVT0, value);

	/*
	 * only the BP should see the LINT1 NMI signal, obviously.
	 */
	if (!smp_processor_id())
		value = APIC_DM_NMI;
	else
		value = APIC_DM_NMI | APIC_LVT_MASKED;
	if (!lapic_is_integrated())		/* 82489DX */
		value |= APIC_LVT_LEVEL_TRIGGER;
	apic_write(APIC_LVT1, value);

	preempt_enable();
}

void __cpuinit end_local_APIC_setup(void)
{
	lapic_setup_esr();

#ifdef CONFIG_X86_32
	{
		unsigned int value;
		/* Disable the local apic timer */
		value = apic_read(APIC_LVTT);
		value |= (APIC_LVT_MASKED | LOCAL_TIMER_VECTOR);
		apic_write(APIC_LVTT, value);
	}
#endif

	setup_apic_nmi_watchdog(NULL);
	apic_pm_activate();
}

#ifdef HAVE_X2APIC
void check_x2apic(void)
{
	int msr, msr2;

	rdmsr(MSR_IA32_APICBASE, msr, msr2);

	if (msr & X2APIC_ENABLE) {
		pr_info("x2apic enabled by BIOS, switching to x2apic ops\n");
		x2apic_preenabled = x2apic = 1;
		apic_ops = &x2apic_ops;
	}
}

void enable_x2apic(void)
{
	int msr, msr2;

	rdmsr(MSR_IA32_APICBASE, msr, msr2);
	if (!(msr & X2APIC_ENABLE)) {
		pr_info("Enabling x2apic\n");
		wrmsr(MSR_IA32_APICBASE, msr | X2APIC_ENABLE, 0);
	}
}

void __init enable_IR_x2apic(void)
{
#ifdef CONFIG_INTR_REMAP
	int ret;
	unsigned long flags;

	if (!cpu_has_x2apic)
		return;

	if (!x2apic_preenabled && disable_x2apic) {
		pr_info("Skipped enabling x2apic and Interrupt-remapping "
			"because of nox2apic\n");
		return;
	}

	if (x2apic_preenabled && disable_x2apic)
		panic("Bios already enabled x2apic, can't enforce nox2apic");

	if (!x2apic_preenabled && skip_ioapic_setup) {
		pr_info("Skipped enabling x2apic and Interrupt-remapping "
			"because of skipping io-apic setup\n");
		return;
	}

	ret = dmar_table_init();
	if (ret) {
		pr_info("dmar_table_init() failed with %d:\n", ret);

		if (x2apic_preenabled)
			panic("x2apic enabled by bios. But IR enabling failed");
		else
			pr_info("Not enabling x2apic,Intr-remapping\n");
		return;
	}

	local_irq_save(flags);
	mask_8259A();

	ret = save_mask_IO_APIC_setup();
	if (ret) {
		pr_info("Saving IO-APIC state failed: %d\n", ret);
		goto end;
	}

	ret = enable_intr_remapping(1);

	if (ret && x2apic_preenabled) {
		local_irq_restore(flags);
		panic("x2apic enabled by bios. But IR enabling failed");
	}

	if (ret)
		goto end_restore;

	if (!x2apic) {
		x2apic = 1;
		apic_ops = &x2apic_ops;
		enable_x2apic();
	}

end_restore:
	if (ret)
		/*
		 * IR enabling failed
		 */
		restore_IO_APIC_setup();
	else
		reinit_intr_remapped_IO_APIC(x2apic_preenabled);

end:
	unmask_8259A();
	local_irq_restore(flags);

	if (!ret) {
		if (!x2apic_preenabled)
			pr_info("Enabled x2apic and interrupt-remapping\n");
		else
			pr_info("Enabled Interrupt-remapping\n");
	} else
		pr_err("Failed to enable Interrupt-remapping and x2apic\n");
#else
	if (!cpu_has_x2apic)
		return;

	if (x2apic_preenabled)
		panic("x2apic enabled prior OS handover,"
		      " enable CONFIG_INTR_REMAP");

	pr_info("Enable CONFIG_INTR_REMAP for enabling intr-remapping "
		" and x2apic\n");
#endif

	return;
}
#endif /* HAVE_X2APIC */

#ifdef CONFIG_X86_64
/*
 * Detect and enable local APICs on non-SMP boards.
 * Original code written by Keir Fraser.
 * On AMD64 we trust the BIOS - if it says no APIC it is likely
 * not correctly set up (usually the APIC timer won't work etc.)
 */
static int __init detect_init_APIC(void)
{
	if (!cpu_has_apic) {
		pr_info("No local APIC present\n");
		return -1;
	}

	mp_lapic_addr = APIC_DEFAULT_PHYS_BASE;
	boot_cpu_physical_apicid = 0;
	return 0;
}
#else
/*
 * Detect and initialize APIC
 */
static int __init detect_init_APIC(void)
{
	u32 h, l, features;

	/* Disabled by kernel option? */
	if (disable_apic)
		return -1;

	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_AMD:
		if ((boot_cpu_data.x86 == 6 && boot_cpu_data.x86_model > 1) ||
		    (boot_cpu_data.x86 == 15))
			break;
		goto no_apic;
	case X86_VENDOR_INTEL:
		if (boot_cpu_data.x86 == 6 || boot_cpu_data.x86 == 15 ||
		    (boot_cpu_data.x86 == 5 && cpu_has_apic))
			break;
		goto no_apic;
	default:
		goto no_apic;
	}

	if (!cpu_has_apic) {
		/*
		 * Over-ride BIOS and try to enable the local APIC only if
		 * "lapic" specified.
		 */
		if (!force_enable_local_apic) {
			pr_info("Local APIC disabled by BIOS -- "
				"you can enable it with \"lapic\"\n");
			return -1;
		}
		/*
		 * Some BIOSes disable the local APIC in the APIC_BASE
		 * MSR. This can only be done in software for Intel P6 or later
		 * and AMD K7 (Model > 1) or later.
		 */
		rdmsr(MSR_IA32_APICBASE, l, h);
		if (!(l & MSR_IA32_APICBASE_ENABLE)) {
			pr_info("Local APIC disabled by BIOS -- reenabling.\n");
			l &= ~MSR_IA32_APICBASE_BASE;
			l |= MSR_IA32_APICBASE_ENABLE | APIC_DEFAULT_PHYS_BASE;
			wrmsr(MSR_IA32_APICBASE, l, h);
			enabled_via_apicbase = 1;
		}
	}
	/*
	 * The APIC feature bit should now be enabled
	 * in `cpuid'
	 */
	features = cpuid_edx(1);
	if (!(features & (1 << X86_FEATURE_APIC))) {
		pr_warning("Could not enable APIC!\n");
		return -1;
	}
	set_cpu_cap(&boot_cpu_data, X86_FEATURE_APIC);
	mp_lapic_addr = APIC_DEFAULT_PHYS_BASE;

	/* The BIOS may have set up the APIC at some other address */
	rdmsr(MSR_IA32_APICBASE, l, h);
	if (l & MSR_IA32_APICBASE_ENABLE)
		mp_lapic_addr = l & MSR_IA32_APICBASE_BASE;

	pr_info("Found and enabled local APIC!\n");

	apic_pm_activate();

	return 0;

no_apic:
	pr_info("No local APIC present or hardware disabled\n");
	return -1;
}
#endif

#ifdef CONFIG_X86_64
void __init early_init_lapic_mapping(void)
{
	unsigned long phys_addr;

	/*
	 * If no local APIC can be found then go out
	 * : it means there is no mpatable and MADT
	 */
	if (!smp_found_config)
		return;

	phys_addr = mp_lapic_addr;

	set_fixmap_nocache(FIX_APIC_BASE, phys_addr);
	apic_printk(APIC_VERBOSE, "mapped APIC to %16lx (%16lx)\n",
		    APIC_BASE, phys_addr);

	/*
	 * Fetch the APIC ID of the BSP in case we have a
	 * default configuration (or the MP table is broken).
	 */
	boot_cpu_physical_apicid = read_apic_id();
}
#endif

/**
 * init_apic_mappings - initialize APIC mappings
 */
void __init init_apic_mappings(void)
{
#ifdef HAVE_X2APIC
	if (x2apic) {
		boot_cpu_physical_apicid = read_apic_id();
		return;
	}
#endif

	/*
	 * If no local APIC can be found then set up a fake all
	 * zeroes page to simulate the local APIC and another
	 * one for the IO-APIC.
	 */
	if (!smp_found_config && detect_init_APIC()) {
		apic_phys = (unsigned long) alloc_bootmem_pages(PAGE_SIZE);
		apic_phys = __pa(apic_phys);
	} else
		apic_phys = mp_lapic_addr;

	set_fixmap_nocache(FIX_APIC_BASE, apic_phys);
	apic_printk(APIC_VERBOSE, "mapped APIC to %08lx (%08lx)\n",
				APIC_BASE, apic_phys);

	/*
	 * Fetch the APIC ID of the BSP in case we have a
	 * default configuration (or the MP table is broken).
	 */
	if (boot_cpu_physical_apicid == -1U)
		boot_cpu_physical_apicid = read_apic_id();
}

/*
 * This initializes the IO-APIC and APIC hardware if this is
 * a UP kernel.
 */
int apic_version[MAX_APICS];

int __init APIC_init_uniprocessor(void)
{
#ifdef CONFIG_X86_64
	if (disable_apic) {
		pr_info("Apic disabled\n");
		return -1;
	}
	if (!cpu_has_apic) {
		disable_apic = 1;
		pr_info("Apic disabled by BIOS\n");
		return -1;
	}
#else
	if (!smp_found_config && !cpu_has_apic)
		return -1;

	/*
	 * Complain if the BIOS pretends there is one.
	 */
	if (!cpu_has_apic &&
	    APIC_INTEGRATED(apic_version[boot_cpu_physical_apicid])) {
		pr_err("BIOS bug, local APIC 0x%x not detected!...\n",
			boot_cpu_physical_apicid);
		clear_cpu_cap(&boot_cpu_data, X86_FEATURE_APIC);
		return -1;
	}
#endif

#ifdef HAVE_X2APIC
	enable_IR_x2apic();
#endif
#ifdef CONFIG_X86_64
	setup_apic_routing();
#endif

	verify_local_APIC();
	connect_bsp_APIC();

#ifdef CONFIG_X86_64
	apic_write(APIC_ID, SET_APIC_ID(boot_cpu_physical_apicid));
#else
	/*
	 * Hack: In case of kdump, after a crash, kernel might be booting
	 * on a cpu with non-zero lapic id. But boot_cpu_physical_apicid
	 * might be zero if read from MP tables. Get it from LAPIC.
	 */
# ifdef CONFIG_CRASH_DUMP
	boot_cpu_physical_apicid = read_apic_id();
# endif
#endif
	physid_set_mask_of_physid(boot_cpu_physical_apicid, &phys_cpu_present_map);
	setup_local_APIC();

#ifdef CONFIG_X86_64
	/*
	 * Now enable IO-APICs, actually call clear_IO_APIC
	 * We need clear_IO_APIC before enabling vector on BP
	 */
	if (!skip_ioapic_setup && nr_ioapics)
		enable_IO_APIC();
#endif

#ifdef CONFIG_X86_IO_APIC
	if (!smp_found_config || skip_ioapic_setup || !nr_ioapics)
#endif
		localise_nmi_watchdog();
	end_local_APIC_setup();

#ifdef CONFIG_X86_IO_APIC
	if (smp_found_config && !skip_ioapic_setup && nr_ioapics)
		setup_IO_APIC();
# ifdef CONFIG_X86_64
	else
		nr_ioapics = 0;
# endif
#endif

#ifdef CONFIG_X86_64
	setup_boot_APIC_clock();
	check_nmi_watchdog();
#else
	setup_boot_clock();
#endif

	return 0;
}

/*
 * Local APIC interrupts
 */

/*
 * This interrupt should _never_ happen with our APIC/SMP architecture
 */
void smp_spurious_interrupt(struct pt_regs *regs)
{
	u32 v;

	exit_idle();
	irq_enter();
	/*
	 * Check if this really is a spurious interrupt and ACK it
	 * if it is a vectored one.  Just in case...
	 * Spurious interrupts should not be ACKed.
	 */
	v = apic_read(APIC_ISR + ((SPURIOUS_APIC_VECTOR & ~0x1f) >> 1));
	if (v & (1 << (SPURIOUS_APIC_VECTOR & 0x1f)))
		ack_APIC_irq();

	inc_irq_stat(irq_spurious_count);

	/* see sw-dev-man vol 3, chapter 7.4.13.5 */
	pr_info("spurious APIC interrupt on CPU#%d, "
		"should never happen.\n", smp_processor_id());
	irq_exit();
}

/*
 * This interrupt should never happen with our APIC/SMP architecture
 */
void smp_error_interrupt(struct pt_regs *regs)
{
	u32 v, v1;

	exit_idle();
	irq_enter();
	/* First tickle the hardware, only then report what went on. -- REW */
	v = apic_read(APIC_ESR);
	apic_write(APIC_ESR, 0);
	v1 = apic_read(APIC_ESR);
	ack_APIC_irq();
	atomic_inc(&irq_err_count);

	/*
	 * Here is what the APIC error bits mean:
	 * 0: Send CS error
	 * 1: Receive CS error
	 * 2: Send accept error
	 * 3: Receive accept error
	 * 4: Reserved
	 * 5: Send illegal vector
	 * 6: Received illegal vector
	 * 7: Illegal register address
	 */
	pr_debug("APIC error on CPU%d: %02x(%02x)\n",
		smp_processor_id(), v , v1);
	irq_exit();
}

/**
 * connect_bsp_APIC - attach the APIC to the interrupt system
 */
void __init connect_bsp_APIC(void)
{
#ifdef CONFIG_X86_32
	if (pic_mode) {
		/*
		 * Do not trust the local APIC being empty at bootup.
		 */
		clear_local_APIC();
		/*
		 * PIC mode, enable APIC mode in the IMCR, i.e.  connect BSP's
		 * local APIC to INT and NMI lines.
		 */
		apic_printk(APIC_VERBOSE, "leaving PIC mode, "
				"enabling APIC mode.\n");
		outb(0x70, 0x22);
		outb(0x01, 0x23);
	}
#endif
	enable_apic_mode();
}

/**
 * disconnect_bsp_APIC - detach the APIC from the interrupt system
 * @virt_wire_setup:	indicates, whether virtual wire mode is selected
 *
 * Virtual wire mode is necessary to deliver legacy interrupts even when the
 * APIC is disabled.
 */
void disconnect_bsp_APIC(int virt_wire_setup)
{
	unsigned int value;

#ifdef CONFIG_X86_32
	if (pic_mode) {
		/*
		 * Put the board back into PIC mode (has an effect only on
		 * certain older boards).  Note that APIC interrupts, including
		 * IPIs, won't work beyond this point!  The only exception are
		 * INIT IPIs.
		 */
		apic_printk(APIC_VERBOSE, "disabling APIC mode, "
				"entering PIC mode.\n");
		outb(0x70, 0x22);
		outb(0x00, 0x23);
		return;
	}
#endif

	/* Go back to Virtual Wire compatibility mode */

	/* For the spurious interrupt use vector F, and enable it */
	value = apic_read(APIC_SPIV);
	value &= ~APIC_VECTOR_MASK;
	value |= APIC_SPIV_APIC_ENABLED;
	value |= 0xf;
	apic_write(APIC_SPIV, value);

	if (!virt_wire_setup) {
		/*
		 * For LVT0 make it edge triggered, active high,
		 * external and enabled
		 */
		value = apic_read(APIC_LVT0);
		value &= ~(APIC_MODE_MASK | APIC_SEND_PENDING |
			APIC_INPUT_POLARITY | APIC_LVT_REMOTE_IRR |
			APIC_LVT_LEVEL_TRIGGER | APIC_LVT_MASKED);
		value |= APIC_LVT_REMOTE_IRR | APIC_SEND_PENDING;
		value = SET_APIC_DELIVERY_MODE(value, APIC_MODE_EXTINT);
		apic_write(APIC_LVT0, value);
	} else {
		/* Disable LVT0 */
		apic_write(APIC_LVT0, APIC_LVT_MASKED);
	}

	/*
	 * For LVT1 make it edge triggered, active high,
	 * nmi and enabled
	 */
	value = apic_read(APIC_LVT1);
	value &= ~(APIC_MODE_MASK | APIC_SEND_PENDING |
			APIC_INPUT_POLARITY | APIC_LVT_REMOTE_IRR |
			APIC_LVT_LEVEL_TRIGGER | APIC_LVT_MASKED);
	value |= APIC_LVT_REMOTE_IRR | APIC_SEND_PENDING;
	value = SET_APIC_DELIVERY_MODE(value, APIC_MODE_NMI);
	apic_write(APIC_LVT1, value);
}

void __cpuinit generic_processor_info(int apicid, int version)
{
	int cpu;

	/*
	 * Validate version
	 */
	if (version == 0x0) {
		pr_warning("BIOS bug, APIC version is 0 for CPU#%d! "
			   "fixing up to 0x10. (tell your hw vendor)\n",
				version);
		version = 0x10;
	}
	apic_version[apicid] = version;

	if (num_processors >= nr_cpu_ids) {
		int max = nr_cpu_ids;
		int thiscpu = max + disabled_cpus;

		pr_warning(
			"ACPI: NR_CPUS/possible_cpus limit of %i reached."
			"  Processor %d/0x%x ignored.\n", max, thiscpu, apicid);

		disabled_cpus++;
		return;
	}

	num_processors++;
	cpu = cpumask_next_zero(-1, cpu_present_mask);

	physid_set(apicid, phys_cpu_present_map);
	if (apicid == boot_cpu_physical_apicid) {
		/*
		 * x86_bios_cpu_apicid is required to have processors listed
		 * in same order as logical cpu numbers. Hence the first
		 * entry is BSP, and so on.
		 */
		cpu = 0;
	}
	if (apicid > max_physical_apicid)
		max_physical_apicid = apicid;

#ifdef CONFIG_X86_32
	/*
	 * Would be preferable to switch to bigsmp when CONFIG_HOTPLUG_CPU=y
	 * but we need to work other dependencies like SMP_SUSPEND etc
	 * before this can be done without some confusion.
	 * if (CPU_HOTPLUG_ENABLED || num_processors > 8)
	 *       - Ashok Raj <ashok.raj@intel.com>
	 */
	if (max_physical_apicid >= 8) {
		switch (boot_cpu_data.x86_vendor) {
		case X86_VENDOR_INTEL:
			if (!APIC_XAPIC(version)) {
				def_to_bigsmp = 0;
				break;
			}
			/* If P4 and above fall through */
		case X86_VENDOR_AMD:
			def_to_bigsmp = 1;
		}
	}
#endif

#if defined(CONFIG_X86_SMP) || defined(CONFIG_X86_64)
	/* are we being called early in kernel startup? */
	if (early_per_cpu_ptr(x86_cpu_to_apicid)) {
		u16 *cpu_to_apicid = early_per_cpu_ptr(x86_cpu_to_apicid);
		u16 *bios_cpu_apicid = early_per_cpu_ptr(x86_bios_cpu_apicid);

		cpu_to_apicid[cpu] = apicid;
		bios_cpu_apicid[cpu] = apicid;
	} else {
		per_cpu(x86_cpu_to_apicid, cpu) = apicid;
		per_cpu(x86_bios_cpu_apicid, cpu) = apicid;
	}
#endif

	set_cpu_possible(cpu, true);
	set_cpu_present(cpu, true);
}

#ifdef CONFIG_X86_64
int hard_smp_processor_id(void)
{
	return read_apic_id();
}
#endif

/*
 * Power management
 */
#ifdef CONFIG_PM

static struct {
	/*
	 * 'active' is true if the local APIC was enabled by us and
	 * not the BIOS; this signifies that we are also responsible
	 * for disabling it before entering apm/acpi suspend
	 */
	int active;
	/* r/w apic fields */
	unsigned int apic_id;
	unsigned int apic_taskpri;
	unsigned int apic_ldr;
	unsigned int apic_dfr;
	unsigned int apic_spiv;
	unsigned int apic_lvtt;
	unsigned int apic_lvtpc;
	unsigned int apic_lvt0;
	unsigned int apic_lvt1;
	unsigned int apic_lvterr;
	unsigned int apic_tmict;
	unsigned int apic_tdcr;
	unsigned int apic_thmr;
} apic_pm_state;

static int lapic_suspend(struct sys_device *dev, pm_message_t state)
{
	unsigned long flags;
	int maxlvt;

	if (!apic_pm_state.active)
		return 0;

	maxlvt = lapic_get_maxlvt();

	apic_pm_state.apic_id = apic_read(APIC_ID);
	apic_pm_state.apic_taskpri = apic_read(APIC_TASKPRI);
	apic_pm_state.apic_ldr = apic_read(APIC_LDR);
	apic_pm_state.apic_dfr = apic_read(APIC_DFR);
	apic_pm_state.apic_spiv = apic_read(APIC_SPIV);
	apic_pm_state.apic_lvtt = apic_read(APIC_LVTT);
	if (maxlvt >= 4)
		apic_pm_state.apic_lvtpc = apic_read(APIC_LVTPC);
	apic_pm_state.apic_lvt0 = apic_read(APIC_LVT0);
	apic_pm_state.apic_lvt1 = apic_read(APIC_LVT1);
	apic_pm_state.apic_lvterr = apic_read(APIC_LVTERR);
	apic_pm_state.apic_tmict = apic_read(APIC_TMICT);
	apic_pm_state.apic_tdcr = apic_read(APIC_TDCR);
#if defined(CONFIG_X86_MCE_P4THERMAL) || defined(CONFIG_X86_MCE_INTEL)
	if (maxlvt >= 5)
		apic_pm_state.apic_thmr = apic_read(APIC_LVTTHMR);
#endif

	local_irq_save(flags);
	disable_local_APIC();
	local_irq_restore(flags);
	return 0;
}

static int lapic_resume(struct sys_device *dev)
{
	unsigned int l, h;
	unsigned long flags;
	int maxlvt;

	if (!apic_pm_state.active)
		return 0;

	maxlvt = lapic_get_maxlvt();

	local_irq_save(flags);

#ifdef HAVE_X2APIC
	if (x2apic)
		enable_x2apic();
	else
#endif
	{
		/*
		 * Make sure the APICBASE points to the right address
		 *
		 * FIXME! This will be wrong if we ever support suspend on
		 * SMP! We'll need to do this as part of the CPU restore!
		 */
		rdmsr(MSR_IA32_APICBASE, l, h);
		l &= ~MSR_IA32_APICBASE_BASE;
		l |= MSR_IA32_APICBASE_ENABLE | mp_lapic_addr;
		wrmsr(MSR_IA32_APICBASE, l, h);
	}

	apic_write(APIC_LVTERR, ERROR_APIC_VECTOR | APIC_LVT_MASKED);
	apic_write(APIC_ID, apic_pm_state.apic_id);
	apic_write(APIC_DFR, apic_pm_state.apic_dfr);
	apic_write(APIC_LDR, apic_pm_state.apic_ldr);
	apic_write(APIC_TASKPRI, apic_pm_state.apic_taskpri);
	apic_write(APIC_SPIV, apic_pm_state.apic_spiv);
	apic_write(APIC_LVT0, apic_pm_state.apic_lvt0);
	apic_write(APIC_LVT1, apic_pm_state.apic_lvt1);
#if defined(CONFIG_X86_MCE_P4THERMAL) || defined(CONFIG_X86_MCE_INTEL)
	if (maxlvt >= 5)
		apic_write(APIC_LVTTHMR, apic_pm_state.apic_thmr);
#endif
	if (maxlvt >= 4)
		apic_write(APIC_LVTPC, apic_pm_state.apic_lvtpc);
	apic_write(APIC_LVTT, apic_pm_state.apic_lvtt);
	apic_write(APIC_TDCR, apic_pm_state.apic_tdcr);
	apic_write(APIC_TMICT, apic_pm_state.apic_tmict);
	apic_write(APIC_ESR, 0);
	apic_read(APIC_ESR);
	apic_write(APIC_LVTERR, apic_pm_state.apic_lvterr);
	apic_write(APIC_ESR, 0);
	apic_read(APIC_ESR);

	local_irq_restore(flags);

	return 0;
}

/*
 * This device has no shutdown method - fully functioning local APICs
 * are needed on every CPU up until machine_halt/restart/poweroff.
 */

static struct sysdev_class lapic_sysclass = {
	.name		= "lapic",
	.resume		= lapic_resume,
	.suspend	= lapic_suspend,
};

static struct sys_device device_lapic = {
	.id	= 0,
	.cls	= &lapic_sysclass,
};

static void __cpuinit apic_pm_activate(void)
{
	apic_pm_state.active = 1;
}

static int __init init_lapic_sysfs(void)
{
	int error;

	if (!cpu_has_apic)
		return 0;
	/* XXX: remove suspend/resume procs if !apic_pm_state.active? */

	error = sysdev_class_register(&lapic_sysclass);
	if (!error)
		error = sysdev_register(&device_lapic);
	return error;
}
device_initcall(init_lapic_sysfs);

#else	/* CONFIG_PM */

static void apic_pm_activate(void) { }

#endif	/* CONFIG_PM */

#ifdef CONFIG_X86_64
/*
 * apic_is_clustered_box() -- Check if we can expect good TSC
 *
 * Thus far, the major user of this is IBM's Summit2 series:
 *
 * Clustered boxes may have unsynced TSC problems if they are
 * multi-chassis. Use available data to take a good guess.
 * If in doubt, go HPET.
 */
__cpuinit int apic_is_clustered_box(void)
{
	int i, clusters, zeros;
	unsigned id;
	u16 *bios_cpu_apicid;
	DECLARE_BITMAP(clustermap, NUM_APIC_CLUSTERS);

	/*
	 * there is not this kind of box with AMD CPU yet.
	 * Some AMD box with quadcore cpu and 8 sockets apicid
	 * will be [4, 0x23] or [8, 0x27] could be thought to
	 * vsmp box still need checking...
	 */
	if ((boot_cpu_data.x86_vendor == X86_VENDOR_AMD) && !is_vsmp_box())
		return 0;

	bios_cpu_apicid = early_per_cpu_ptr(x86_bios_cpu_apicid);
	bitmap_zero(clustermap, NUM_APIC_CLUSTERS);

	for (i = 0; i < nr_cpu_ids; i++) {
		/* are we being called early in kernel startup? */
		if (bios_cpu_apicid) {
			id = bios_cpu_apicid[i];
		} else if (i < nr_cpu_ids) {
			if (cpu_present(i))
				id = per_cpu(x86_bios_cpu_apicid, i);
			else
				continue;
		} else
			break;

		if (id != BAD_APICID)
			__set_bit(APIC_CLUSTERID(id), clustermap);
	}

	/* Problem:  Partially populated chassis may not have CPUs in some of
	 * the APIC clusters they have been allocated.  Only present CPUs have
	 * x86_bios_cpu_apicid entries, thus causing zeroes in the bitmap.
	 * Since clusters are allocated sequentially, count zeros only if
	 * they are bounded by ones.
	 */
	clusters = 0;
	zeros = 0;
	for (i = 0; i < NUM_APIC_CLUSTERS; i++) {
		if (test_bit(i, clustermap)) {
			clusters += 1 + zeros;
			zeros = 0;
		} else
			++zeros;
	}

	/* ScaleMP vSMPowered boxes have one cluster per board and TSCs are
	 * not guaranteed to be synced between boards
	 */
	if (is_vsmp_box() && clusters > 1)
		return 1;

	/*
	 * If clusters > 2, then should be multi-chassis.
	 * May have to revisit this when multi-core + hyperthreaded CPUs come
	 * out, but AFAIK this will work even for them.
	 */
	return (clusters > 2);
}
#endif

/*
 * APIC command line parameters
 */
static int __init setup_disableapic(char *arg)
{
	disable_apic = 1;
	setup_clear_cpu_cap(X86_FEATURE_APIC);
	return 0;
}
early_param("disableapic", setup_disableapic);

/* same as disableapic, for compatibility */
static int __init setup_nolapic(char *arg)
{
	return setup_disableapic(arg);
}
early_param("nolapic", setup_nolapic);

static int __init parse_lapic_timer_c2_ok(char *arg)
{
	local_apic_timer_c2_ok = 1;
	return 0;
}
early_param("lapic_timer_c2_ok", parse_lapic_timer_c2_ok);

static int __init parse_disable_apic_timer(char *arg)
{
	disable_apic_timer = 1;
	return 0;
}
early_param("noapictimer", parse_disable_apic_timer);

static int __init parse_nolapic_timer(char *arg)
{
	disable_apic_timer = 1;
	return 0;
}
early_param("nolapic_timer", parse_nolapic_timer);

static int __init apic_set_verbosity(char *arg)
{
	if (!arg)  {
#ifdef CONFIG_X86_64
		skip_ioapic_setup = 0;
		return 0;
#endif
		return -EINVAL;
	}

	if (strcmp("debug", arg) == 0)
		apic_verbosity = APIC_DEBUG;
	else if (strcmp("verbose", arg) == 0)
		apic_verbosity = APIC_VERBOSE;
	else {
		pr_warning("APIC Verbosity level %s not recognised"
			" use apic=verbose or apic=debug\n", arg);
		return -EINVAL;
	}

	return 0;
}
early_param("apic", apic_set_verbosity);

static int __init lapic_insert_resource(void)
{
	if (!apic_phys)
		return -1;

	/* Put local APIC into the resource map. */
	lapic_resource.start = apic_phys;
	lapic_resource.end = lapic_resource.start + PAGE_SIZE - 1;
	insert_resource(&iomem_resource, &lapic_resource);

	return 0;
}

/*
 * need call insert after e820_reserve_resources()
 * that is using request_resource
 */
late_initcall(lapic_insert_resource);
