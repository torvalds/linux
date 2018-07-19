#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/timer.h>
#include <linux/acpi_pmtmr.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/clocksource.h>
#include <linux/percpu.h>
#include <linux/timex.h>
#include <linux/static_key.h>

#include <asm/hpet.h>
#include <asm/timer.h>
#include <asm/vgtod.h>
#include <asm/time.h>
#include <asm/delay.h>
#include <asm/hypervisor.h>
#include <asm/nmi.h>
#include <asm/x86_init.h>
#include <asm/geode.h>
#include <asm/apic.h>
#include <asm/intel-family.h>
#include <asm/i8259.h>

unsigned int __read_mostly cpu_khz;	/* TSC clocks / usec, not used here */
EXPORT_SYMBOL(cpu_khz);

unsigned int __read_mostly tsc_khz;
EXPORT_SYMBOL(tsc_khz);

#define KHZ	1000

/*
 * TSC can be unstable due to cpufreq or due to unsynced TSCs
 */
static int __read_mostly tsc_unstable;

static DEFINE_STATIC_KEY_FALSE(__use_tsc);

int tsc_clocksource_reliable;

static u32 art_to_tsc_numerator;
static u32 art_to_tsc_denominator;
static u64 art_to_tsc_offset;
struct clocksource *art_related_clocksource;

struct cyc2ns {
	struct cyc2ns_data data[2];	/*  0 + 2*16 = 32 */
	seqcount_t	   seq;		/* 32 + 4    = 36 */

}; /* fits one cacheline */

static DEFINE_PER_CPU_ALIGNED(struct cyc2ns, cyc2ns);

void cyc2ns_read_begin(struct cyc2ns_data *data)
{
	int seq, idx;

	preempt_disable_notrace();

	do {
		seq = this_cpu_read(cyc2ns.seq.sequence);
		idx = seq & 1;

		data->cyc2ns_offset = this_cpu_read(cyc2ns.data[idx].cyc2ns_offset);
		data->cyc2ns_mul    = this_cpu_read(cyc2ns.data[idx].cyc2ns_mul);
		data->cyc2ns_shift  = this_cpu_read(cyc2ns.data[idx].cyc2ns_shift);

	} while (unlikely(seq != this_cpu_read(cyc2ns.seq.sequence)));
}

void cyc2ns_read_end(void)
{
	preempt_enable_notrace();
}

/*
 * Accelerators for sched_clock()
 * convert from cycles(64bits) => nanoseconds (64bits)
 *  basic equation:
 *              ns = cycles / (freq / ns_per_sec)
 *              ns = cycles * (ns_per_sec / freq)
 *              ns = cycles * (10^9 / (cpu_khz * 10^3))
 *              ns = cycles * (10^6 / cpu_khz)
 *
 *      Then we use scaling math (suggested by george@mvista.com) to get:
 *              ns = cycles * (10^6 * SC / cpu_khz) / SC
 *              ns = cycles * cyc2ns_scale / SC
 *
 *      And since SC is a constant power of two, we can convert the div
 *  into a shift. The larger SC is, the more accurate the conversion, but
 *  cyc2ns_scale needs to be a 32-bit value so that 32-bit multiplication
 *  (64-bit result) can be used.
 *
 *  We can use khz divisor instead of mhz to keep a better precision.
 *  (mathieu.desnoyers@polymtl.ca)
 *
 *                      -johnstul@us.ibm.com "math is hard, lets go shopping!"
 */

static inline unsigned long long cycles_2_ns(unsigned long long cyc)
{
	struct cyc2ns_data data;
	unsigned long long ns;

	cyc2ns_read_begin(&data);

	ns = data.cyc2ns_offset;
	ns += mul_u64_u32_shr(cyc, data.cyc2ns_mul, data.cyc2ns_shift);

	cyc2ns_read_end();

	return ns;
}

static void __set_cyc2ns_scale(unsigned long khz, int cpu, unsigned long long tsc_now)
{
	unsigned long long ns_now;
	struct cyc2ns_data data;
	struct cyc2ns *c2n;

	ns_now = cycles_2_ns(tsc_now);

	/*
	 * Compute a new multiplier as per the above comment and ensure our
	 * time function is continuous; see the comment near struct
	 * cyc2ns_data.
	 */
	clocks_calc_mult_shift(&data.cyc2ns_mul, &data.cyc2ns_shift, khz,
			       NSEC_PER_MSEC, 0);

	/*
	 * cyc2ns_shift is exported via arch_perf_update_userpage() where it is
	 * not expected to be greater than 31 due to the original published
	 * conversion algorithm shifting a 32-bit value (now specifies a 64-bit
	 * value) - refer perf_event_mmap_page documentation in perf_event.h.
	 */
	if (data.cyc2ns_shift == 32) {
		data.cyc2ns_shift = 31;
		data.cyc2ns_mul >>= 1;
	}

	data.cyc2ns_offset = ns_now -
		mul_u64_u32_shr(tsc_now, data.cyc2ns_mul, data.cyc2ns_shift);

	c2n = per_cpu_ptr(&cyc2ns, cpu);

	raw_write_seqcount_latch(&c2n->seq);
	c2n->data[0] = data;
	raw_write_seqcount_latch(&c2n->seq);
	c2n->data[1] = data;
}

static void set_cyc2ns_scale(unsigned long khz, int cpu, unsigned long long tsc_now)
{
	unsigned long flags;

	local_irq_save(flags);
	sched_clock_idle_sleep_event();

	if (khz)
		__set_cyc2ns_scale(khz, cpu, tsc_now);

	sched_clock_idle_wakeup_event();
	local_irq_restore(flags);
}

/*
 * Initialize cyc2ns for boot cpu
 */
static void __init cyc2ns_init_boot_cpu(void)
{
	struct cyc2ns *c2n = this_cpu_ptr(&cyc2ns);

	seqcount_init(&c2n->seq);
	__set_cyc2ns_scale(tsc_khz, smp_processor_id(), rdtsc());
}

/*
 * Secondary CPUs do not run through cyc2ns_init(), so set up
 * all the scale factors for all CPUs, assuming the same
 * speed as the bootup CPU. (cpufreq notifiers will fix this
 * up if their speed diverges)
 */
static void __init cyc2ns_init_secondary_cpus(void)
{
	unsigned int cpu, this_cpu = smp_processor_id();
	struct cyc2ns *c2n = this_cpu_ptr(&cyc2ns);
	struct cyc2ns_data *data = c2n->data;

	for_each_possible_cpu(cpu) {
		if (cpu != this_cpu) {
			seqcount_init(&c2n->seq);
			c2n = per_cpu_ptr(&cyc2ns, cpu);
			c2n->data[0] = data[0];
			c2n->data[1] = data[1];
		}
	}
}

/*
 * Scheduler clock - returns current time in nanosec units.
 */
u64 native_sched_clock(void)
{
	if (static_branch_likely(&__use_tsc)) {
		u64 tsc_now = rdtsc();

		/* return the value in ns */
		return cycles_2_ns(tsc_now);
	}

	/*
	 * Fall back to jiffies if there's no TSC available:
	 * ( But note that we still use it if the TSC is marked
	 *   unstable. We do this because unlike Time Of Day,
	 *   the scheduler clock tolerates small errors and it's
	 *   very important for it to be as fast as the platform
	 *   can achieve it. )
	 */

	/* No locking but a rare wrong value is not a big deal: */
	return (jiffies_64 - INITIAL_JIFFIES) * (1000000000 / HZ);
}

/*
 * Generate a sched_clock if you already have a TSC value.
 */
u64 native_sched_clock_from_tsc(u64 tsc)
{
	return cycles_2_ns(tsc);
}

/* We need to define a real function for sched_clock, to override the
   weak default version */
#ifdef CONFIG_PARAVIRT
unsigned long long sched_clock(void)
{
	return paravirt_sched_clock();
}

bool using_native_sched_clock(void)
{
	return pv_time_ops.sched_clock == native_sched_clock;
}
#else
unsigned long long
sched_clock(void) __attribute__((alias("native_sched_clock")));

bool using_native_sched_clock(void) { return true; }
#endif

int check_tsc_unstable(void)
{
	return tsc_unstable;
}
EXPORT_SYMBOL_GPL(check_tsc_unstable);

#ifdef CONFIG_X86_TSC
int __init notsc_setup(char *str)
{
	mark_tsc_unstable("boot parameter notsc");
	return 1;
}
#else
/*
 * disable flag for tsc. Takes effect by clearing the TSC cpu flag
 * in cpu/common.c
 */
int __init notsc_setup(char *str)
{
	setup_clear_cpu_cap(X86_FEATURE_TSC);
	return 1;
}
#endif

__setup("notsc", notsc_setup);

static int no_sched_irq_time;

static int __init tsc_setup(char *str)
{
	if (!strcmp(str, "reliable"))
		tsc_clocksource_reliable = 1;
	if (!strncmp(str, "noirqtime", 9))
		no_sched_irq_time = 1;
	if (!strcmp(str, "unstable"))
		mark_tsc_unstable("boot parameter");
	return 1;
}

__setup("tsc=", tsc_setup);

#define MAX_RETRIES     5
#define SMI_TRESHOLD    50000

/*
 * Read TSC and the reference counters. Take care of SMI disturbance
 */
static u64 tsc_read_refs(u64 *p, int hpet)
{
	u64 t1, t2;
	int i;

	for (i = 0; i < MAX_RETRIES; i++) {
		t1 = get_cycles();
		if (hpet)
			*p = hpet_readl(HPET_COUNTER) & 0xFFFFFFFF;
		else
			*p = acpi_pm_read_early();
		t2 = get_cycles();
		if ((t2 - t1) < SMI_TRESHOLD)
			return t2;
	}
	return ULLONG_MAX;
}

/*
 * Calculate the TSC frequency from HPET reference
 */
static unsigned long calc_hpet_ref(u64 deltatsc, u64 hpet1, u64 hpet2)
{
	u64 tmp;

	if (hpet2 < hpet1)
		hpet2 += 0x100000000ULL;
	hpet2 -= hpet1;
	tmp = ((u64)hpet2 * hpet_readl(HPET_PERIOD));
	do_div(tmp, 1000000);
	deltatsc = div64_u64(deltatsc, tmp);

	return (unsigned long) deltatsc;
}

/*
 * Calculate the TSC frequency from PMTimer reference
 */
static unsigned long calc_pmtimer_ref(u64 deltatsc, u64 pm1, u64 pm2)
{
	u64 tmp;

	if (!pm1 && !pm2)
		return ULONG_MAX;

	if (pm2 < pm1)
		pm2 += (u64)ACPI_PM_OVRRUN;
	pm2 -= pm1;
	tmp = pm2 * 1000000000LL;
	do_div(tmp, PMTMR_TICKS_PER_SEC);
	do_div(deltatsc, tmp);

	return (unsigned long) deltatsc;
}

#define CAL_MS		10
#define CAL_LATCH	(PIT_TICK_RATE / (1000 / CAL_MS))
#define CAL_PIT_LOOPS	1000

#define CAL2_MS		50
#define CAL2_LATCH	(PIT_TICK_RATE / (1000 / CAL2_MS))
#define CAL2_PIT_LOOPS	5000


/*
 * Try to calibrate the TSC against the Programmable
 * Interrupt Timer and return the frequency of the TSC
 * in kHz.
 *
 * Return ULONG_MAX on failure to calibrate.
 */
static unsigned long pit_calibrate_tsc(u32 latch, unsigned long ms, int loopmin)
{
	u64 tsc, t1, t2, delta;
	unsigned long tscmin, tscmax;
	int pitcnt;

	if (!has_legacy_pic()) {
		/*
		 * Relies on tsc_early_delay_calibrate() to have given us semi
		 * usable udelay(), wait for the same 50ms we would have with
		 * the PIT loop below.
		 */
		udelay(10 * USEC_PER_MSEC);
		udelay(10 * USEC_PER_MSEC);
		udelay(10 * USEC_PER_MSEC);
		udelay(10 * USEC_PER_MSEC);
		udelay(10 * USEC_PER_MSEC);
		return ULONG_MAX;
	}

	/* Set the Gate high, disable speaker */
	outb((inb(0x61) & ~0x02) | 0x01, 0x61);

	/*
	 * Setup CTC channel 2* for mode 0, (interrupt on terminal
	 * count mode), binary count. Set the latch register to 50ms
	 * (LSB then MSB) to begin countdown.
	 */
	outb(0xb0, 0x43);
	outb(latch & 0xff, 0x42);
	outb(latch >> 8, 0x42);

	tsc = t1 = t2 = get_cycles();

	pitcnt = 0;
	tscmax = 0;
	tscmin = ULONG_MAX;
	while ((inb(0x61) & 0x20) == 0) {
		t2 = get_cycles();
		delta = t2 - tsc;
		tsc = t2;
		if ((unsigned long) delta < tscmin)
			tscmin = (unsigned int) delta;
		if ((unsigned long) delta > tscmax)
			tscmax = (unsigned int) delta;
		pitcnt++;
	}

	/*
	 * Sanity checks:
	 *
	 * If we were not able to read the PIT more than loopmin
	 * times, then we have been hit by a massive SMI
	 *
	 * If the maximum is 10 times larger than the minimum,
	 * then we got hit by an SMI as well.
	 */
	if (pitcnt < loopmin || tscmax > 10 * tscmin)
		return ULONG_MAX;

	/* Calculate the PIT value */
	delta = t2 - t1;
	do_div(delta, ms);
	return delta;
}

/*
 * This reads the current MSB of the PIT counter, and
 * checks if we are running on sufficiently fast and
 * non-virtualized hardware.
 *
 * Our expectations are:
 *
 *  - the PIT is running at roughly 1.19MHz
 *
 *  - each IO is going to take about 1us on real hardware,
 *    but we allow it to be much faster (by a factor of 10) or
 *    _slightly_ slower (ie we allow up to a 2us read+counter
 *    update - anything else implies a unacceptably slow CPU
 *    or PIT for the fast calibration to work.
 *
 *  - with 256 PIT ticks to read the value, we have 214us to
 *    see the same MSB (and overhead like doing a single TSC
 *    read per MSB value etc).
 *
 *  - We're doing 2 reads per loop (LSB, MSB), and we expect
 *    them each to take about a microsecond on real hardware.
 *    So we expect a count value of around 100. But we'll be
 *    generous, and accept anything over 50.
 *
 *  - if the PIT is stuck, and we see *many* more reads, we
 *    return early (and the next caller of pit_expect_msb()
 *    then consider it a failure when they don't see the
 *    next expected value).
 *
 * These expectations mean that we know that we have seen the
 * transition from one expected value to another with a fairly
 * high accuracy, and we didn't miss any events. We can thus
 * use the TSC value at the transitions to calculate a pretty
 * good value for the TSC frequencty.
 */
static inline int pit_verify_msb(unsigned char val)
{
	/* Ignore LSB */
	inb(0x42);
	return inb(0x42) == val;
}

static inline int pit_expect_msb(unsigned char val, u64 *tscp, unsigned long *deltap)
{
	int count;
	u64 tsc = 0, prev_tsc = 0;

	for (count = 0; count < 50000; count++) {
		if (!pit_verify_msb(val))
			break;
		prev_tsc = tsc;
		tsc = get_cycles();
	}
	*deltap = get_cycles() - prev_tsc;
	*tscp = tsc;

	/*
	 * We require _some_ success, but the quality control
	 * will be based on the error terms on the TSC values.
	 */
	return count > 5;
}

/*
 * How many MSB values do we want to see? We aim for
 * a maximum error rate of 500ppm (in practice the
 * real error is much smaller), but refuse to spend
 * more than 50ms on it.
 */
#define MAX_QUICK_PIT_MS 50
#define MAX_QUICK_PIT_ITERATIONS (MAX_QUICK_PIT_MS * PIT_TICK_RATE / 1000 / 256)

static unsigned long quick_pit_calibrate(void)
{
	int i;
	u64 tsc, delta;
	unsigned long d1, d2;

	if (!has_legacy_pic())
		return 0;

	/* Set the Gate high, disable speaker */
	outb((inb(0x61) & ~0x02) | 0x01, 0x61);

	/*
	 * Counter 2, mode 0 (one-shot), binary count
	 *
	 * NOTE! Mode 2 decrements by two (and then the
	 * output is flipped each time, giving the same
	 * final output frequency as a decrement-by-one),
	 * so mode 0 is much better when looking at the
	 * individual counts.
	 */
	outb(0xb0, 0x43);

	/* Start at 0xffff */
	outb(0xff, 0x42);
	outb(0xff, 0x42);

	/*
	 * The PIT starts counting at the next edge, so we
	 * need to delay for a microsecond. The easiest way
	 * to do that is to just read back the 16-bit counter
	 * once from the PIT.
	 */
	pit_verify_msb(0);

	if (pit_expect_msb(0xff, &tsc, &d1)) {
		for (i = 1; i <= MAX_QUICK_PIT_ITERATIONS; i++) {
			if (!pit_expect_msb(0xff-i, &delta, &d2))
				break;

			delta -= tsc;

			/*
			 * Extrapolate the error and fail fast if the error will
			 * never be below 500 ppm.
			 */
			if (i == 1 &&
			    d1 + d2 >= (delta * MAX_QUICK_PIT_ITERATIONS) >> 11)
				return 0;

			/*
			 * Iterate until the error is less than 500 ppm
			 */
			if (d1+d2 >= delta >> 11)
				continue;

			/*
			 * Check the PIT one more time to verify that
			 * all TSC reads were stable wrt the PIT.
			 *
			 * This also guarantees serialization of the
			 * last cycle read ('d2') in pit_expect_msb.
			 */
			if (!pit_verify_msb(0xfe - i))
				break;
			goto success;
		}
	}
	pr_info("Fast TSC calibration failed\n");
	return 0;

success:
	/*
	 * Ok, if we get here, then we've seen the
	 * MSB of the PIT decrement 'i' times, and the
	 * error has shrunk to less than 500 ppm.
	 *
	 * As a result, we can depend on there not being
	 * any odd delays anywhere, and the TSC reads are
	 * reliable (within the error).
	 *
	 * kHz = ticks / time-in-seconds / 1000;
	 * kHz = (t2 - t1) / (I * 256 / PIT_TICK_RATE) / 1000
	 * kHz = ((t2 - t1) * PIT_TICK_RATE) / (I * 256 * 1000)
	 */
	delta *= PIT_TICK_RATE;
	do_div(delta, i*256*1000);
	pr_info("Fast TSC calibration using PIT\n");
	return delta;
}

/**
 * native_calibrate_tsc
 * Determine TSC frequency via CPUID, else return 0.
 */
unsigned long native_calibrate_tsc(void)
{
	unsigned int eax_denominator, ebx_numerator, ecx_hz, edx;
	unsigned int crystal_khz;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL)
		return 0;

	if (boot_cpu_data.cpuid_level < 0x15)
		return 0;

	eax_denominator = ebx_numerator = ecx_hz = edx = 0;

	/* CPUID 15H TSC/Crystal ratio, plus optionally Crystal Hz */
	cpuid(0x15, &eax_denominator, &ebx_numerator, &ecx_hz, &edx);

	if (ebx_numerator == 0 || eax_denominator == 0)
		return 0;

	crystal_khz = ecx_hz / 1000;

	if (crystal_khz == 0) {
		switch (boot_cpu_data.x86_model) {
		case INTEL_FAM6_SKYLAKE_MOBILE:
		case INTEL_FAM6_SKYLAKE_DESKTOP:
		case INTEL_FAM6_KABYLAKE_MOBILE:
		case INTEL_FAM6_KABYLAKE_DESKTOP:
			crystal_khz = 24000;	/* 24.0 MHz */
			break;
		case INTEL_FAM6_ATOM_DENVERTON:
			crystal_khz = 25000;	/* 25.0 MHz */
			break;
		case INTEL_FAM6_ATOM_GOLDMONT:
			crystal_khz = 19200;	/* 19.2 MHz */
			break;
		}
	}

	if (crystal_khz == 0)
		return 0;
	/*
	 * TSC frequency determined by CPUID is a "hardware reported"
	 * frequency and is the most accurate one so far we have. This
	 * is considered a known frequency.
	 */
	setup_force_cpu_cap(X86_FEATURE_TSC_KNOWN_FREQ);

	/*
	 * For Atom SoCs TSC is the only reliable clocksource.
	 * Mark TSC reliable so no watchdog on it.
	 */
	if (boot_cpu_data.x86_model == INTEL_FAM6_ATOM_GOLDMONT)
		setup_force_cpu_cap(X86_FEATURE_TSC_RELIABLE);

	return crystal_khz * ebx_numerator / eax_denominator;
}

static unsigned long cpu_khz_from_cpuid(void)
{
	unsigned int eax_base_mhz, ebx_max_mhz, ecx_bus_mhz, edx;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL)
		return 0;

	if (boot_cpu_data.cpuid_level < 0x16)
		return 0;

	eax_base_mhz = ebx_max_mhz = ecx_bus_mhz = edx = 0;

	cpuid(0x16, &eax_base_mhz, &ebx_max_mhz, &ecx_bus_mhz, &edx);

	return eax_base_mhz * 1000;
}

/*
 * calibrate cpu using pit, hpet, and ptimer methods. They are available
 * later in boot after acpi is initialized.
 */
static unsigned long pit_hpet_ptimer_calibrate_cpu(void)
{
	u64 tsc1, tsc2, delta, ref1, ref2;
	unsigned long tsc_pit_min = ULONG_MAX, tsc_ref_min = ULONG_MAX;
	unsigned long flags, latch, ms;
	int hpet = is_hpet_enabled(), i, loopmin;

	/*
	 * Run 5 calibration loops to get the lowest frequency value
	 * (the best estimate). We use two different calibration modes
	 * here:
	 *
	 * 1) PIT loop. We set the PIT Channel 2 to oneshot mode and
	 * load a timeout of 50ms. We read the time right after we
	 * started the timer and wait until the PIT count down reaches
	 * zero. In each wait loop iteration we read the TSC and check
	 * the delta to the previous read. We keep track of the min
	 * and max values of that delta. The delta is mostly defined
	 * by the IO time of the PIT access, so we can detect when a
	 * SMI/SMM disturbance happened between the two reads. If the
	 * maximum time is significantly larger than the minimum time,
	 * then we discard the result and have another try.
	 *
	 * 2) Reference counter. If available we use the HPET or the
	 * PMTIMER as a reference to check the sanity of that value.
	 * We use separate TSC readouts and check inside of the
	 * reference read for a SMI/SMM disturbance. We dicard
	 * disturbed values here as well. We do that around the PIT
	 * calibration delay loop as we have to wait for a certain
	 * amount of time anyway.
	 */

	/* Preset PIT loop values */
	latch = CAL_LATCH;
	ms = CAL_MS;
	loopmin = CAL_PIT_LOOPS;

	for (i = 0; i < 3; i++) {
		unsigned long tsc_pit_khz;

		/*
		 * Read the start value and the reference count of
		 * hpet/pmtimer when available. Then do the PIT
		 * calibration, which will take at least 50ms, and
		 * read the end value.
		 */
		local_irq_save(flags);
		tsc1 = tsc_read_refs(&ref1, hpet);
		tsc_pit_khz = pit_calibrate_tsc(latch, ms, loopmin);
		tsc2 = tsc_read_refs(&ref2, hpet);
		local_irq_restore(flags);

		/* Pick the lowest PIT TSC calibration so far */
		tsc_pit_min = min(tsc_pit_min, tsc_pit_khz);

		/* hpet or pmtimer available ? */
		if (ref1 == ref2)
			continue;

		/* Check, whether the sampling was disturbed by an SMI */
		if (tsc1 == ULLONG_MAX || tsc2 == ULLONG_MAX)
			continue;

		tsc2 = (tsc2 - tsc1) * 1000000LL;
		if (hpet)
			tsc2 = calc_hpet_ref(tsc2, ref1, ref2);
		else
			tsc2 = calc_pmtimer_ref(tsc2, ref1, ref2);

		tsc_ref_min = min(tsc_ref_min, (unsigned long) tsc2);

		/* Check the reference deviation */
		delta = ((u64) tsc_pit_min) * 100;
		do_div(delta, tsc_ref_min);

		/*
		 * If both calibration results are inside a 10% window
		 * then we can be sure, that the calibration
		 * succeeded. We break out of the loop right away. We
		 * use the reference value, as it is more precise.
		 */
		if (delta >= 90 && delta <= 110) {
			pr_info("PIT calibration matches %s. %d loops\n",
				hpet ? "HPET" : "PMTIMER", i + 1);
			return tsc_ref_min;
		}

		/*
		 * Check whether PIT failed more than once. This
		 * happens in virtualized environments. We need to
		 * give the virtual PC a slightly longer timeframe for
		 * the HPET/PMTIMER to make the result precise.
		 */
		if (i == 1 && tsc_pit_min == ULONG_MAX) {
			latch = CAL2_LATCH;
			ms = CAL2_MS;
			loopmin = CAL2_PIT_LOOPS;
		}
	}

	/*
	 * Now check the results.
	 */
	if (tsc_pit_min == ULONG_MAX) {
		/* PIT gave no useful value */
		pr_warn("Unable to calibrate against PIT\n");

		/* We don't have an alternative source, disable TSC */
		if (!hpet && !ref1 && !ref2) {
			pr_notice("No reference (HPET/PMTIMER) available\n");
			return 0;
		}

		/* The alternative source failed as well, disable TSC */
		if (tsc_ref_min == ULONG_MAX) {
			pr_warn("HPET/PMTIMER calibration failed\n");
			return 0;
		}

		/* Use the alternative source */
		pr_info("using %s reference calibration\n",
			hpet ? "HPET" : "PMTIMER");

		return tsc_ref_min;
	}

	/* We don't have an alternative source, use the PIT calibration value */
	if (!hpet && !ref1 && !ref2) {
		pr_info("Using PIT calibration value\n");
		return tsc_pit_min;
	}

	/* The alternative source failed, use the PIT calibration value */
	if (tsc_ref_min == ULONG_MAX) {
		pr_warn("HPET/PMTIMER calibration failed. Using PIT calibration.\n");
		return tsc_pit_min;
	}

	/*
	 * The calibration values differ too much. In doubt, we use
	 * the PIT value as we know that there are PMTIMERs around
	 * running at double speed. At least we let the user know:
	 */
	pr_warn("PIT calibration deviates from %s: %lu %lu\n",
		hpet ? "HPET" : "PMTIMER", tsc_pit_min, tsc_ref_min);
	pr_info("Using PIT calibration value\n");
	return tsc_pit_min;
}

/**
 * native_calibrate_cpu_early - can calibrate the cpu early in boot
 */
unsigned long native_calibrate_cpu_early(void)
{
	unsigned long flags, fast_calibrate = cpu_khz_from_cpuid();

	if (!fast_calibrate)
		fast_calibrate = cpu_khz_from_msr();
	if (!fast_calibrate) {
		local_irq_save(flags);
		fast_calibrate = quick_pit_calibrate();
		local_irq_restore(flags);
	}
	return fast_calibrate;
}


/**
 * native_calibrate_cpu - calibrate the cpu
 */
static unsigned long native_calibrate_cpu(void)
{
	unsigned long tsc_freq = native_calibrate_cpu_early();

	if (!tsc_freq)
		tsc_freq = pit_hpet_ptimer_calibrate_cpu();

	return tsc_freq;
}

void recalibrate_cpu_khz(void)
{
#ifndef CONFIG_SMP
	unsigned long cpu_khz_old = cpu_khz;

	if (!boot_cpu_has(X86_FEATURE_TSC))
		return;

	cpu_khz = x86_platform.calibrate_cpu();
	tsc_khz = x86_platform.calibrate_tsc();
	if (tsc_khz == 0)
		tsc_khz = cpu_khz;
	else if (abs(cpu_khz - tsc_khz) * 10 > tsc_khz)
		cpu_khz = tsc_khz;
	cpu_data(0).loops_per_jiffy = cpufreq_scale(cpu_data(0).loops_per_jiffy,
						    cpu_khz_old, cpu_khz);
#endif
}

EXPORT_SYMBOL(recalibrate_cpu_khz);


static unsigned long long cyc2ns_suspend;

void tsc_save_sched_clock_state(void)
{
	if (!sched_clock_stable())
		return;

	cyc2ns_suspend = sched_clock();
}

/*
 * Even on processors with invariant TSC, TSC gets reset in some the
 * ACPI system sleep states. And in some systems BIOS seem to reinit TSC to
 * arbitrary value (still sync'd across cpu's) during resume from such sleep
 * states. To cope up with this, recompute the cyc2ns_offset for each cpu so
 * that sched_clock() continues from the point where it was left off during
 * suspend.
 */
void tsc_restore_sched_clock_state(void)
{
	unsigned long long offset;
	unsigned long flags;
	int cpu;

	if (!sched_clock_stable())
		return;

	local_irq_save(flags);

	/*
	 * We're coming out of suspend, there's no concurrency yet; don't
	 * bother being nice about the RCU stuff, just write to both
	 * data fields.
	 */

	this_cpu_write(cyc2ns.data[0].cyc2ns_offset, 0);
	this_cpu_write(cyc2ns.data[1].cyc2ns_offset, 0);

	offset = cyc2ns_suspend - sched_clock();

	for_each_possible_cpu(cpu) {
		per_cpu(cyc2ns.data[0].cyc2ns_offset, cpu) = offset;
		per_cpu(cyc2ns.data[1].cyc2ns_offset, cpu) = offset;
	}

	local_irq_restore(flags);
}

#ifdef CONFIG_CPU_FREQ
/* Frequency scaling support. Adjust the TSC based timer when the cpu frequency
 * changes.
 *
 * RED-PEN: On SMP we assume all CPUs run with the same frequency.  It's
 * not that important because current Opteron setups do not support
 * scaling on SMP anyroads.
 *
 * Should fix up last_tsc too. Currently gettimeofday in the
 * first tick after the change will be slightly wrong.
 */

static unsigned int  ref_freq;
static unsigned long loops_per_jiffy_ref;
static unsigned long tsc_khz_ref;

static int time_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
				void *data)
{
	struct cpufreq_freqs *freq = data;
	unsigned long *lpj;

	lpj = &boot_cpu_data.loops_per_jiffy;
#ifdef CONFIG_SMP
	if (!(freq->flags & CPUFREQ_CONST_LOOPS))
		lpj = &cpu_data(freq->cpu).loops_per_jiffy;
#endif

	if (!ref_freq) {
		ref_freq = freq->old;
		loops_per_jiffy_ref = *lpj;
		tsc_khz_ref = tsc_khz;
	}
	if ((val == CPUFREQ_PRECHANGE  && freq->old < freq->new) ||
			(val == CPUFREQ_POSTCHANGE && freq->old > freq->new)) {
		*lpj = cpufreq_scale(loops_per_jiffy_ref, ref_freq, freq->new);

		tsc_khz = cpufreq_scale(tsc_khz_ref, ref_freq, freq->new);
		if (!(freq->flags & CPUFREQ_CONST_LOOPS))
			mark_tsc_unstable("cpufreq changes");

		set_cyc2ns_scale(tsc_khz, freq->cpu, rdtsc());
	}

	return 0;
}

static struct notifier_block time_cpufreq_notifier_block = {
	.notifier_call  = time_cpufreq_notifier
};

static int __init cpufreq_register_tsc_scaling(void)
{
	if (!boot_cpu_has(X86_FEATURE_TSC))
		return 0;
	if (boot_cpu_has(X86_FEATURE_CONSTANT_TSC))
		return 0;
	cpufreq_register_notifier(&time_cpufreq_notifier_block,
				CPUFREQ_TRANSITION_NOTIFIER);
	return 0;
}

core_initcall(cpufreq_register_tsc_scaling);

#endif /* CONFIG_CPU_FREQ */

#define ART_CPUID_LEAF (0x15)
#define ART_MIN_DENOMINATOR (1)


/*
 * If ART is present detect the numerator:denominator to convert to TSC
 */
static void __init detect_art(void)
{
	unsigned int unused[2];

	if (boot_cpu_data.cpuid_level < ART_CPUID_LEAF)
		return;

	/*
	 * Don't enable ART in a VM, non-stop TSC and TSC_ADJUST required,
	 * and the TSC counter resets must not occur asynchronously.
	 */
	if (boot_cpu_has(X86_FEATURE_HYPERVISOR) ||
	    !boot_cpu_has(X86_FEATURE_NONSTOP_TSC) ||
	    !boot_cpu_has(X86_FEATURE_TSC_ADJUST) ||
	    tsc_async_resets)
		return;

	cpuid(ART_CPUID_LEAF, &art_to_tsc_denominator,
	      &art_to_tsc_numerator, unused, unused+1);

	if (art_to_tsc_denominator < ART_MIN_DENOMINATOR)
		return;

	rdmsrl(MSR_IA32_TSC_ADJUST, art_to_tsc_offset);

	/* Make this sticky over multiple CPU init calls */
	setup_force_cpu_cap(X86_FEATURE_ART);
}


/* clocksource code */

static void tsc_resume(struct clocksource *cs)
{
	tsc_verify_tsc_adjust(true);
}

/*
 * We used to compare the TSC to the cycle_last value in the clocksource
 * structure to avoid a nasty time-warp. This can be observed in a
 * very small window right after one CPU updated cycle_last under
 * xtime/vsyscall_gtod lock and the other CPU reads a TSC value which
 * is smaller than the cycle_last reference value due to a TSC which
 * is slighty behind. This delta is nowhere else observable, but in
 * that case it results in a forward time jump in the range of hours
 * due to the unsigned delta calculation of the time keeping core
 * code, which is necessary to support wrapping clocksources like pm
 * timer.
 *
 * This sanity check is now done in the core timekeeping code.
 * checking the result of read_tsc() - cycle_last for being negative.
 * That works because CLOCKSOURCE_MASK(64) does not mask out any bit.
 */
static u64 read_tsc(struct clocksource *cs)
{
	return (u64)rdtsc_ordered();
}

static void tsc_cs_mark_unstable(struct clocksource *cs)
{
	if (tsc_unstable)
		return;

	tsc_unstable = 1;
	if (using_native_sched_clock())
		clear_sched_clock_stable();
	disable_sched_clock_irqtime();
	pr_info("Marking TSC unstable due to clocksource watchdog\n");
}

static void tsc_cs_tick_stable(struct clocksource *cs)
{
	if (tsc_unstable)
		return;

	if (using_native_sched_clock())
		sched_clock_tick_stable();
}

/*
 * .mask MUST be CLOCKSOURCE_MASK(64). See comment above read_tsc()
 */
static struct clocksource clocksource_tsc_early = {
	.name                   = "tsc-early",
	.rating                 = 299,
	.read                   = read_tsc,
	.mask                   = CLOCKSOURCE_MASK(64),
	.flags                  = CLOCK_SOURCE_IS_CONTINUOUS |
				  CLOCK_SOURCE_MUST_VERIFY,
	.archdata               = { .vclock_mode = VCLOCK_TSC },
	.resume			= tsc_resume,
	.mark_unstable		= tsc_cs_mark_unstable,
	.tick_stable		= tsc_cs_tick_stable,
	.list			= LIST_HEAD_INIT(clocksource_tsc_early.list),
};

/*
 * Must mark VALID_FOR_HRES early such that when we unregister tsc_early
 * this one will immediately take over. We will only register if TSC has
 * been found good.
 */
static struct clocksource clocksource_tsc = {
	.name                   = "tsc",
	.rating                 = 300,
	.read                   = read_tsc,
	.mask                   = CLOCKSOURCE_MASK(64),
	.flags                  = CLOCK_SOURCE_IS_CONTINUOUS |
				  CLOCK_SOURCE_VALID_FOR_HRES |
				  CLOCK_SOURCE_MUST_VERIFY,
	.archdata               = { .vclock_mode = VCLOCK_TSC },
	.resume			= tsc_resume,
	.mark_unstable		= tsc_cs_mark_unstable,
	.tick_stable		= tsc_cs_tick_stable,
	.list			= LIST_HEAD_INIT(clocksource_tsc.list),
};

void mark_tsc_unstable(char *reason)
{
	if (tsc_unstable)
		return;

	tsc_unstable = 1;
	if (using_native_sched_clock())
		clear_sched_clock_stable();
	disable_sched_clock_irqtime();
	pr_info("Marking TSC unstable due to %s\n", reason);

	clocksource_mark_unstable(&clocksource_tsc_early);
	clocksource_mark_unstable(&clocksource_tsc);
}

EXPORT_SYMBOL_GPL(mark_tsc_unstable);

static void __init check_system_tsc_reliable(void)
{
#if defined(CONFIG_MGEODEGX1) || defined(CONFIG_MGEODE_LX) || defined(CONFIG_X86_GENERIC)
	if (is_geode_lx()) {
		/* RTSC counts during suspend */
#define RTSC_SUSP 0x100
		unsigned long res_low, res_high;

		rdmsr_safe(MSR_GEODE_BUSCONT_CONF0, &res_low, &res_high);
		/* Geode_LX - the OLPC CPU has a very reliable TSC */
		if (res_low & RTSC_SUSP)
			tsc_clocksource_reliable = 1;
	}
#endif
	if (boot_cpu_has(X86_FEATURE_TSC_RELIABLE))
		tsc_clocksource_reliable = 1;
}

/*
 * Make an educated guess if the TSC is trustworthy and synchronized
 * over all CPUs.
 */
int unsynchronized_tsc(void)
{
	if (!boot_cpu_has(X86_FEATURE_TSC) || tsc_unstable)
		return 1;

#ifdef CONFIG_SMP
	if (apic_is_clustered_box())
		return 1;
#endif

	if (boot_cpu_has(X86_FEATURE_CONSTANT_TSC))
		return 0;

	if (tsc_clocksource_reliable)
		return 0;
	/*
	 * Intel systems are normally all synchronized.
	 * Exceptions must mark TSC as unstable:
	 */
	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL) {
		/* assume multi socket systems are not synchronized: */
		if (num_possible_cpus() > 1)
			return 1;
	}

	return 0;
}

/*
 * Convert ART to TSC given numerator/denominator found in detect_art()
 */
struct system_counterval_t convert_art_to_tsc(u64 art)
{
	u64 tmp, res, rem;

	rem = do_div(art, art_to_tsc_denominator);

	res = art * art_to_tsc_numerator;
	tmp = rem * art_to_tsc_numerator;

	do_div(tmp, art_to_tsc_denominator);
	res += tmp + art_to_tsc_offset;

	return (struct system_counterval_t) {.cs = art_related_clocksource,
			.cycles = res};
}
EXPORT_SYMBOL(convert_art_to_tsc);

/**
 * convert_art_ns_to_tsc() - Convert ART in nanoseconds to TSC.
 * @art_ns: ART (Always Running Timer) in unit of nanoseconds
 *
 * PTM requires all timestamps to be in units of nanoseconds. When user
 * software requests a cross-timestamp, this function converts system timestamp
 * to TSC.
 *
 * This is valid when CPU feature flag X86_FEATURE_TSC_KNOWN_FREQ is set
 * indicating the tsc_khz is derived from CPUID[15H]. Drivers should check
 * that this flag is set before conversion to TSC is attempted.
 *
 * Return:
 * struct system_counterval_t - system counter value with the pointer to the
 *	corresponding clocksource
 *	@cycles:	System counter value
 *	@cs:		Clocksource corresponding to system counter value. Used
 *			by timekeeping code to verify comparibility of two cycle
 *			values.
 */

struct system_counterval_t convert_art_ns_to_tsc(u64 art_ns)
{
	u64 tmp, res, rem;

	rem = do_div(art_ns, USEC_PER_SEC);

	res = art_ns * tsc_khz;
	tmp = rem * tsc_khz;

	do_div(tmp, USEC_PER_SEC);
	res += tmp;

	return (struct system_counterval_t) { .cs = art_related_clocksource,
					      .cycles = res};
}
EXPORT_SYMBOL(convert_art_ns_to_tsc);


static void tsc_refine_calibration_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(tsc_irqwork, tsc_refine_calibration_work);
/**
 * tsc_refine_calibration_work - Further refine tsc freq calibration
 * @work - ignored.
 *
 * This functions uses delayed work over a period of a
 * second to further refine the TSC freq value. Since this is
 * timer based, instead of loop based, we don't block the boot
 * process while this longer calibration is done.
 *
 * If there are any calibration anomalies (too many SMIs, etc),
 * or the refined calibration is off by 1% of the fast early
 * calibration, we throw out the new calibration and use the
 * early calibration.
 */
static void tsc_refine_calibration_work(struct work_struct *work)
{
	static u64 tsc_start = -1, ref_start;
	static int hpet;
	u64 tsc_stop, ref_stop, delta;
	unsigned long freq;
	int cpu;

	/* Don't bother refining TSC on unstable systems */
	if (tsc_unstable)
		goto unreg;

	/*
	 * Since the work is started early in boot, we may be
	 * delayed the first time we expire. So set the workqueue
	 * again once we know timers are working.
	 */
	if (tsc_start == -1) {
		/*
		 * Only set hpet once, to avoid mixing hardware
		 * if the hpet becomes enabled later.
		 */
		hpet = is_hpet_enabled();
		schedule_delayed_work(&tsc_irqwork, HZ);
		tsc_start = tsc_read_refs(&ref_start, hpet);
		return;
	}

	tsc_stop = tsc_read_refs(&ref_stop, hpet);

	/* hpet or pmtimer available ? */
	if (ref_start == ref_stop)
		goto out;

	/* Check, whether the sampling was disturbed by an SMI */
	if (tsc_start == ULLONG_MAX || tsc_stop == ULLONG_MAX)
		goto out;

	delta = tsc_stop - tsc_start;
	delta *= 1000000LL;
	if (hpet)
		freq = calc_hpet_ref(delta, ref_start, ref_stop);
	else
		freq = calc_pmtimer_ref(delta, ref_start, ref_stop);

	/* Make sure we're within 1% */
	if (abs(tsc_khz - freq) > tsc_khz/100)
		goto out;

	tsc_khz = freq;
	pr_info("Refined TSC clocksource calibration: %lu.%03lu MHz\n",
		(unsigned long)tsc_khz / 1000,
		(unsigned long)tsc_khz % 1000);

	/* Inform the TSC deadline clockevent devices about the recalibration */
	lapic_update_tsc_freq();

	/* Update the sched_clock() rate to match the clocksource one */
	for_each_possible_cpu(cpu)
		set_cyc2ns_scale(tsc_khz, cpu, tsc_stop);

out:
	if (tsc_unstable)
		goto unreg;

	if (boot_cpu_has(X86_FEATURE_ART))
		art_related_clocksource = &clocksource_tsc;
	clocksource_register_khz(&clocksource_tsc, tsc_khz);
unreg:
	clocksource_unregister(&clocksource_tsc_early);
}


static int __init init_tsc_clocksource(void)
{
	if (!boot_cpu_has(X86_FEATURE_TSC) || !tsc_khz)
		return 0;

	if (tsc_unstable)
		goto unreg;

	if (tsc_clocksource_reliable)
		clocksource_tsc.flags &= ~CLOCK_SOURCE_MUST_VERIFY;

	if (boot_cpu_has(X86_FEATURE_NONSTOP_TSC_S3))
		clocksource_tsc.flags |= CLOCK_SOURCE_SUSPEND_NONSTOP;

	/*
	 * When TSC frequency is known (retrieved via MSR or CPUID), we skip
	 * the refined calibration and directly register it as a clocksource.
	 */
	if (boot_cpu_has(X86_FEATURE_TSC_KNOWN_FREQ)) {
		if (boot_cpu_has(X86_FEATURE_ART))
			art_related_clocksource = &clocksource_tsc;
		clocksource_register_khz(&clocksource_tsc, tsc_khz);
unreg:
		clocksource_unregister(&clocksource_tsc_early);
		return 0;
	}

	schedule_delayed_work(&tsc_irqwork, 0);
	return 0;
}
/*
 * We use device_initcall here, to ensure we run after the hpet
 * is fully initialized, which may occur at fs_initcall time.
 */
device_initcall(init_tsc_clocksource);

static bool __init determine_cpu_tsc_frequencies(bool early)
{
	/* Make sure that cpu and tsc are not already calibrated */
	WARN_ON(cpu_khz || tsc_khz);

	if (early) {
		cpu_khz = x86_platform.calibrate_cpu();
		tsc_khz = x86_platform.calibrate_tsc();
	} else {
		/* We should not be here with non-native cpu calibration */
		WARN_ON(x86_platform.calibrate_cpu != native_calibrate_cpu);
		cpu_khz = pit_hpet_ptimer_calibrate_cpu();
	}

	/*
	 * Trust non-zero tsc_khz as authorative,
	 * and use it to sanity check cpu_khz,
	 * which will be off if system timer is off.
	 */
	if (tsc_khz == 0)
		tsc_khz = cpu_khz;
	else if (abs(cpu_khz - tsc_khz) * 10 > tsc_khz)
		cpu_khz = tsc_khz;

	if (tsc_khz == 0)
		return false;

	pr_info("Detected %lu.%03lu MHz processor\n",
		(unsigned long)cpu_khz / KHZ,
		(unsigned long)cpu_khz % KHZ);

	if (cpu_khz != tsc_khz) {
		pr_info("Detected %lu.%03lu MHz TSC",
			(unsigned long)tsc_khz / KHZ,
			(unsigned long)tsc_khz % KHZ);
	}
	return true;
}

static unsigned long __init get_loops_per_jiffy(void)
{
	unsigned long lpj = tsc_khz * KHZ;

	do_div(lpj, HZ);
	return lpj;
}

void __init tsc_early_init(void)
{
	if (!boot_cpu_has(X86_FEATURE_TSC))
		return;
	if (!determine_cpu_tsc_frequencies(true))
		return;
	loops_per_jiffy = get_loops_per_jiffy();

	/* Sanitize TSC ADJUST before cyc2ns gets initialized */
	tsc_store_and_check_tsc_adjust(true);
	cyc2ns_init_boot_cpu();
	static_branch_enable(&__use_tsc);
}

void __init tsc_init(void)
{
	/*
	 * native_calibrate_cpu_early can only calibrate using methods that are
	 * available early in boot.
	 */
	if (x86_platform.calibrate_cpu == native_calibrate_cpu_early)
		x86_platform.calibrate_cpu = native_calibrate_cpu;

	if (!boot_cpu_has(X86_FEATURE_TSC)) {
		setup_clear_cpu_cap(X86_FEATURE_TSC_DEADLINE_TIMER);
		return;
	}

	if (!tsc_khz) {
		/* We failed to determine frequencies earlier, try again */
		if (!determine_cpu_tsc_frequencies(false)) {
			mark_tsc_unstable("could not calculate TSC khz");
			setup_clear_cpu_cap(X86_FEATURE_TSC_DEADLINE_TIMER);
			return;
		}
		/* Sanitize TSC ADJUST before cyc2ns gets initialized */
		tsc_store_and_check_tsc_adjust(true);
		cyc2ns_init_boot_cpu();
	}

	cyc2ns_init_secondary_cpus();
	static_branch_enable(&__use_tsc);

	if (!no_sched_irq_time)
		enable_sched_clock_irqtime();

	lpj_fine = get_loops_per_jiffy();
	use_tsc_delay();

	check_system_tsc_reliable();

	if (unsynchronized_tsc()) {
		mark_tsc_unstable("TSCs unsynchronized");
		return;
	}

	clocksource_register_khz(&clocksource_tsc_early, tsc_khz);
	detect_art();
}

#ifdef CONFIG_SMP
/*
 * If we have a constant TSC and are using the TSC for the delay loop,
 * we can skip clock calibration if another cpu in the same socket has already
 * been calibrated. This assumes that CONSTANT_TSC applies to all
 * cpus in the socket - this should be a safe assumption.
 */
unsigned long calibrate_delay_is_known(void)
{
	int sibling, cpu = smp_processor_id();
	int constant_tsc = cpu_has(&cpu_data(cpu), X86_FEATURE_CONSTANT_TSC);
	const struct cpumask *mask = topology_core_cpumask(cpu);

	if (!constant_tsc || !mask)
		return 0;

	sibling = cpumask_any_but(mask, cpu);
	if (sibling < nr_cpu_ids)
		return cpu_data(sibling).loops_per_jiffy;
	return 0;
}
#endif
