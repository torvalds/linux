/*
 *  arch/s390/kernel/time.c
 *    Time of day based timer functions.
 *
 *  S390 version
 *    Copyright IBM Corp. 1999, 2008
 *    Author(s): Hartmut Penner (hp@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *
 *  Derived from "arch/i386/kernel/time.c"
 *    Copyright (C) 1991, 1992, 1995  Linus Torvalds
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
#include <linux/sysdev.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/types.h>
#include <linux/profile.h>
#include <linux/timex.h>
#include <linux/notifier.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/bootmem.h>
#include <asm/uaccess.h>
#include <asm/delay.h>
#include <asm/s390_ext.h>
#include <asm/div64.h>
#include <asm/irq.h>
#include <asm/irq_regs.h>
#include <asm/timer.h>
#include <asm/etr.h>
#include <asm/cio.h>

/* change this if you have some constant time drift */
#define USECS_PER_JIFFY     ((unsigned long) 1000000/HZ)
#define CLK_TICKS_PER_JIFFY ((unsigned long) USECS_PER_JIFFY << 12)

/* The value of the TOD clock for 1.1.1970. */
#define TOD_UNIX_EPOCH 0x7d91048bca000000ULL

/*
 * Create a small time difference between the timer interrupts
 * on the different cpus to avoid lock contention.
 */
#define CPU_DEVIATION       (smp_processor_id() << 12)

#define TICK_SIZE tick

static ext_int_info_t ext_int_info_cc;
static ext_int_info_t ext_int_etr_cc;
static u64 jiffies_timer_cc;

static DEFINE_PER_CPU(struct clock_event_device, comparators);

/*
 * Scheduler clock - returns current time in nanosec units.
 */
unsigned long long sched_clock(void)
{
	return ((get_clock_xt() - jiffies_timer_cc) * 125) >> 9;
}

/*
 * Monotonic_clock - returns # of nanoseconds passed since time_init()
 */
unsigned long long monotonic_clock(void)
{
	return sched_clock();
}
EXPORT_SYMBOL(monotonic_clock);

void tod_to_timeval(__u64 todval, struct timespec *xtime)
{
	unsigned long long sec;

	sec = todval >> 12;
	do_div(sec, 1000000);
	xtime->tv_sec = sec;
	todval -= (sec * 1000000) << 12;
	xtime->tv_nsec = ((todval * 1000) >> 12);
}

#ifdef CONFIG_PROFILING
#define s390_do_profile()	profile_tick(CPU_PROFILING)
#else
#define s390_do_profile()	do { ; } while(0)
#endif /* CONFIG_PROFILING */

void clock_comparator_work(void)
{
	struct clock_event_device *cd;

	S390_lowcore.clock_comparator = -1ULL;
	set_clock_comparator(S390_lowcore.clock_comparator);
	cd = &__get_cpu_var(comparators);
	cd->event_handler(cd);
	s390_do_profile();
}

/*
 * Fixup the clock comparator.
 */
static void fixup_clock_comparator(unsigned long long delta)
{
	/* If nobody is waiting there's nothing to fix. */
	if (S390_lowcore.clock_comparator == -1ULL)
		return;
	S390_lowcore.clock_comparator += delta;
	set_clock_comparator(S390_lowcore.clock_comparator);
}

static int s390_next_event(unsigned long delta,
			   struct clock_event_device *evt)
{
	S390_lowcore.clock_comparator = get_clock() + delta;
	set_clock_comparator(S390_lowcore.clock_comparator);
	return 0;
}

static void s390_set_mode(enum clock_event_mode mode,
			  struct clock_event_device *evt)
{
}

/*
 * Set up lowcore and control register of the current cpu to
 * enable TOD clock and clock comparator interrupts.
 */
void init_cpu_timer(void)
{
	struct clock_event_device *cd;
	int cpu;

	S390_lowcore.clock_comparator = -1ULL;
	set_clock_comparator(S390_lowcore.clock_comparator);

	cpu = smp_processor_id();
	cd = &per_cpu(comparators, cpu);
	cd->name		= "comparator";
	cd->features		= CLOCK_EVT_FEAT_ONESHOT;
	cd->mult		= 16777;
	cd->shift		= 12;
	cd->min_delta_ns	= 1;
	cd->max_delta_ns	= LONG_MAX;
	cd->rating		= 400;
	cd->cpumask		= cpumask_of_cpu(cpu);
	cd->set_next_event	= s390_next_event;
	cd->set_mode		= s390_set_mode;

	clockevents_register_device(cd);

	/* Enable clock comparator timer interrupt. */
	__ctl_set_bit(0,11);

	/* Always allow the timing alert external interrupt. */
	__ctl_set_bit(0, 4);
}

static void clock_comparator_interrupt(__u16 code)
{
}

static void etr_timing_alert(struct etr_irq_parm *);
static void stp_timing_alert(struct stp_irq_parm *);

static void timing_alert_interrupt(__u16 code)
{
	if (S390_lowcore.ext_params & 0x00c40000)
		etr_timing_alert((struct etr_irq_parm *)
				 &S390_lowcore.ext_params);
	if (S390_lowcore.ext_params & 0x00038000)
		stp_timing_alert((struct stp_irq_parm *)
				 &S390_lowcore.ext_params);
}

static void etr_reset(void);
static void stp_reset(void);

/*
 * Get the TOD clock running.
 */
static u64 __init reset_tod_clock(void)
{
	u64 time;

	etr_reset();
	stp_reset();
	if (store_clock(&time) == 0)
		return time;
	/* TOD clock not running. Set the clock to Unix Epoch. */
	if (set_clock(TOD_UNIX_EPOCH) != 0 || store_clock(&time) != 0)
		panic("TOD clock not operational.");

	return TOD_UNIX_EPOCH;
}

static cycle_t read_tod_clock(void)
{
	return get_clock();
}

static struct clocksource clocksource_tod = {
	.name		= "tod",
	.rating		= 400,
	.read		= read_tod_clock,
	.mask		= -1ULL,
	.mult		= 1000,
	.shift		= 12,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};


/*
 * Initialize the TOD clock and the CPU timer of
 * the boot cpu.
 */
void __init time_init(void)
{
	u64 init_timer_cc;

	init_timer_cc = reset_tod_clock();
	jiffies_timer_cc = init_timer_cc - jiffies_64 * CLK_TICKS_PER_JIFFY;

	/* set xtime */
	tod_to_timeval(init_timer_cc - TOD_UNIX_EPOCH, &xtime);
        set_normalized_timespec(&wall_to_monotonic,
                                -xtime.tv_sec, -xtime.tv_nsec);

	/* request the clock comparator external interrupt */
	if (register_early_external_interrupt(0x1004,
					      clock_comparator_interrupt,
					      &ext_int_info_cc) != 0)
                panic("Couldn't request external interrupt 0x1004");

	if (clocksource_register(&clocksource_tod) != 0)
		panic("Could not register TOD clock source");

	/* request the timing alert external interrupt */
	if (register_early_external_interrupt(0x1406,
					      timing_alert_interrupt,
					      &ext_int_etr_cc) != 0)
		panic("Couldn't request external interrupt 0x1406");

	/* Enable TOD clock interrupts on the boot cpu. */
	init_cpu_timer();

#ifdef CONFIG_VIRT_TIMER
	vtime_init();
#endif
}

/*
 * The time is "clock". old is what we think the time is.
 * Adjust the value by a multiple of jiffies and add the delta to ntp.
 * "delay" is an approximation how long the synchronization took. If
 * the time correction is positive, then "delay" is subtracted from
 * the time difference and only the remaining part is passed to ntp.
 */
static unsigned long long adjust_time(unsigned long long old,
				      unsigned long long clock,
				      unsigned long long delay)
{
	unsigned long long delta, ticks;
	struct timex adjust;

	if (clock > old) {
		/* It is later than we thought. */
		delta = ticks = clock - old;
		delta = ticks = (delta < delay) ? 0 : delta - delay;
		delta -= do_div(ticks, CLK_TICKS_PER_JIFFY);
		adjust.offset = ticks * (1000000 / HZ);
	} else {
		/* It is earlier than we thought. */
		delta = ticks = old - clock;
		delta -= do_div(ticks, CLK_TICKS_PER_JIFFY);
		delta = -delta;
		adjust.offset = -ticks * (1000000 / HZ);
	}
	jiffies_timer_cc += delta;
	if (adjust.offset != 0) {
		printk(KERN_NOTICE "etr: time adjusted by %li micro-seconds\n",
		       adjust.offset);
		adjust.modes = ADJ_OFFSET_SINGLESHOT;
		do_adjtimex(&adjust);
	}
	return delta;
}

static DEFINE_PER_CPU(atomic_t, clock_sync_word);
static unsigned long clock_sync_flags;

#define CLOCK_SYNC_HAS_ETR	0
#define CLOCK_SYNC_HAS_STP	1
#define CLOCK_SYNC_ETR		2
#define CLOCK_SYNC_STP		3

/*
 * The synchronous get_clock function. It will write the current clock
 * value to the clock pointer and return 0 if the clock is in sync with
 * the external time source. If the clock mode is local it will return
 * -ENOSYS and -EAGAIN if the clock is not in sync with the external
 * reference.
 */
int get_sync_clock(unsigned long long *clock)
{
	atomic_t *sw_ptr;
	unsigned int sw0, sw1;

	sw_ptr = &get_cpu_var(clock_sync_word);
	sw0 = atomic_read(sw_ptr);
	*clock = get_clock();
	sw1 = atomic_read(sw_ptr);
	put_cpu_var(clock_sync_sync);
	if (sw0 == sw1 && (sw0 & 0x80000000U))
		/* Success: time is in sync. */
		return 0;
	if (!test_bit(CLOCK_SYNC_HAS_ETR, &clock_sync_flags) &&
	    !test_bit(CLOCK_SYNC_HAS_STP, &clock_sync_flags))
		return -ENOSYS;
	if (!test_bit(CLOCK_SYNC_ETR, &clock_sync_flags) &&
	    !test_bit(CLOCK_SYNC_STP, &clock_sync_flags))
		return -EACCES;
	return -EAGAIN;
}
EXPORT_SYMBOL(get_sync_clock);

/*
 * Make get_sync_clock return -EAGAIN.
 */
static void disable_sync_clock(void *dummy)
{
	atomic_t *sw_ptr = &__get_cpu_var(clock_sync_word);
	/*
	 * Clear the in-sync bit 2^31. All get_sync_clock calls will
	 * fail until the sync bit is turned back on. In addition
	 * increase the "sequence" counter to avoid the race of an
	 * etr event and the complete recovery against get_sync_clock.
	 */
	atomic_clear_mask(0x80000000, sw_ptr);
	atomic_inc(sw_ptr);
}

/*
 * Make get_sync_clock return 0 again.
 * Needs to be called from a context disabled for preemption.
 */
static void enable_sync_clock(void)
{
	atomic_t *sw_ptr = &__get_cpu_var(clock_sync_word);
	atomic_set_mask(0x80000000, sw_ptr);
}

/*
 * External Time Reference (ETR) code.
 */
static int etr_port0_online;
static int etr_port1_online;
static int etr_steai_available;

static int __init early_parse_etr(char *p)
{
	if (strncmp(p, "off", 3) == 0)
		etr_port0_online = etr_port1_online = 0;
	else if (strncmp(p, "port0", 5) == 0)
		etr_port0_online = 1;
	else if (strncmp(p, "port1", 5) == 0)
		etr_port1_online = 1;
	else if (strncmp(p, "on", 2) == 0)
		etr_port0_online = etr_port1_online = 1;
	return 0;
}
early_param("etr", early_parse_etr);

enum etr_event {
	ETR_EVENT_PORT0_CHANGE,
	ETR_EVENT_PORT1_CHANGE,
	ETR_EVENT_PORT_ALERT,
	ETR_EVENT_SYNC_CHECK,
	ETR_EVENT_SWITCH_LOCAL,
	ETR_EVENT_UPDATE,
};

/*
 * Valid bit combinations of the eacr register are (x = don't care):
 * e0 e1 dp p0 p1 ea es sl
 *  0  0  x  0	0  0  0  0  initial, disabled state
 *  0  0  x  0	1  1  0  0  port 1 online
 *  0  0  x  1	0  1  0  0  port 0 online
 *  0  0  x  1	1  1  0  0  both ports online
 *  0  1  x  0	1  1  0  0  port 1 online and usable, ETR or PPS mode
 *  0  1  x  0	1  1  0  1  port 1 online, usable and ETR mode
 *  0  1  x  0	1  1  1  0  port 1 online, usable, PPS mode, in-sync
 *  0  1  x  0	1  1  1  1  port 1 online, usable, ETR mode, in-sync
 *  0  1  x  1	1  1  0  0  both ports online, port 1 usable
 *  0  1  x  1	1  1  1  0  both ports online, port 1 usable, PPS mode, in-sync
 *  0  1  x  1	1  1  1  1  both ports online, port 1 usable, ETR mode, in-sync
 *  1  0  x  1	0  1  0  0  port 0 online and usable, ETR or PPS mode
 *  1  0  x  1	0  1  0  1  port 0 online, usable and ETR mode
 *  1  0  x  1	0  1  1  0  port 0 online, usable, PPS mode, in-sync
 *  1  0  x  1	0  1  1  1  port 0 online, usable, ETR mode, in-sync
 *  1  0  x  1	1  1  0  0  both ports online, port 0 usable
 *  1  0  x  1	1  1  1  0  both ports online, port 0 usable, PPS mode, in-sync
 *  1  0  x  1	1  1  1  1  both ports online, port 0 usable, ETR mode, in-sync
 *  1  1  x  1	1  1  1  0  both ports online & usable, ETR, in-sync
 *  1  1  x  1	1  1  1  1  both ports online & usable, ETR, in-sync
 */
static struct etr_eacr etr_eacr;
static u64 etr_tolec;			/* time of last eacr update */
static struct etr_aib etr_port0;
static int etr_port0_uptodate;
static struct etr_aib etr_port1;
static int etr_port1_uptodate;
static unsigned long etr_events;
static struct timer_list etr_timer;

static void etr_timeout(unsigned long dummy);
static void etr_work_fn(struct work_struct *work);
static DECLARE_WORK(etr_work, etr_work_fn);

/*
 * Reset ETR attachment.
 */
static void etr_reset(void)
{
	etr_eacr =  (struct etr_eacr) {
		.e0 = 0, .e1 = 0, ._pad0 = 4, .dp = 0,
		.p0 = 0, .p1 = 0, ._pad1 = 0, .ea = 0,
		.es = 0, .sl = 0 };
	if (etr_setr(&etr_eacr) == 0) {
		etr_tolec = get_clock();
		set_bit(CLOCK_SYNC_HAS_ETR, &clock_sync_flags);
	} else if (etr_port0_online || etr_port1_online) {
		printk(KERN_WARNING "Running on non ETR capable "
		       "machine, only local mode available.\n");
		etr_port0_online = etr_port1_online = 0;
	}
}

static int __init etr_init(void)
{
	struct etr_aib aib;

	if (!test_bit(CLOCK_SYNC_HAS_ETR, &clock_sync_flags))
		return 0;
	/* Check if this machine has the steai instruction. */
	if (etr_steai(&aib, ETR_STEAI_STEPPING_PORT) == 0)
		etr_steai_available = 1;
	setup_timer(&etr_timer, etr_timeout, 0UL);
	if (etr_port0_online) {
		set_bit(ETR_EVENT_PORT0_CHANGE, &etr_events);
		schedule_work(&etr_work);
	}
	if (etr_port1_online) {
		set_bit(ETR_EVENT_PORT1_CHANGE, &etr_events);
		schedule_work(&etr_work);
	}
	return 0;
}

arch_initcall(etr_init);

/*
 * Two sorts of ETR machine checks. The architecture reads:
 * "When a machine-check niterruption occurs and if a switch-to-local or
 *  ETR-sync-check interrupt request is pending but disabled, this pending
 *  disabled interruption request is indicated and is cleared".
 * Which means that we can get etr_switch_to_local events from the machine
 * check handler although the interruption condition is disabled. Lovely..
 */

/*
 * Switch to local machine check. This is called when the last usable
 * ETR port goes inactive. After switch to local the clock is not in sync.
 */
void etr_switch_to_local(void)
{
	if (!etr_eacr.sl)
		return;
	if (test_bit(CLOCK_SYNC_ETR, &clock_sync_flags))
		disable_sync_clock(NULL);
	set_bit(ETR_EVENT_SWITCH_LOCAL, &etr_events);
	schedule_work(&etr_work);
}

/*
 * ETR sync check machine check. This is called when the ETR OTE and the
 * local clock OTE are farther apart than the ETR sync check tolerance.
 * After a ETR sync check the clock is not in sync. The machine check
 * is broadcasted to all cpus at the same time.
 */
void etr_sync_check(void)
{
	if (!etr_eacr.es)
		return;
	if (test_bit(CLOCK_SYNC_ETR, &clock_sync_flags))
		disable_sync_clock(NULL);
	set_bit(ETR_EVENT_SYNC_CHECK, &etr_events);
	schedule_work(&etr_work);
}

/*
 * ETR timing alert. There are two causes:
 * 1) port state change, check the usability of the port
 * 2) port alert, one of the ETR-data-validity bits (v1-v2 bits of the
 *    sldr-status word) or ETR-data word 1 (edf1) or ETR-data word 3 (edf3)
 *    or ETR-data word 4 (edf4) has changed.
 */
static void etr_timing_alert(struct etr_irq_parm *intparm)
{
	if (intparm->pc0)
		/* ETR port 0 state change. */
		set_bit(ETR_EVENT_PORT0_CHANGE, &etr_events);
	if (intparm->pc1)
		/* ETR port 1 state change. */
		set_bit(ETR_EVENT_PORT1_CHANGE, &etr_events);
	if (intparm->eai)
		/*
		 * ETR port alert on either port 0, 1 or both.
		 * Both ports are not up-to-date now.
		 */
		set_bit(ETR_EVENT_PORT_ALERT, &etr_events);
	schedule_work(&etr_work);
}

static void etr_timeout(unsigned long dummy)
{
	set_bit(ETR_EVENT_UPDATE, &etr_events);
	schedule_work(&etr_work);
}

/*
 * Check if the etr mode is pss.
 */
static inline int etr_mode_is_pps(struct etr_eacr eacr)
{
	return eacr.es && !eacr.sl;
}

/*
 * Check if the etr mode is etr.
 */
static inline int etr_mode_is_etr(struct etr_eacr eacr)
{
	return eacr.es && eacr.sl;
}

/*
 * Check if the port can be used for TOD synchronization.
 * For PPS mode the port has to receive OTEs. For ETR mode
 * the port has to receive OTEs, the ETR stepping bit has to
 * be zero and the validity bits for data frame 1, 2, and 3
 * have to be 1.
 */
static int etr_port_valid(struct etr_aib *aib, int port)
{
	unsigned int psc;

	/* Check that this port is receiving OTEs. */
	if (aib->tsp == 0)
		return 0;

	psc = port ? aib->esw.psc1 : aib->esw.psc0;
	if (psc == etr_lpsc_pps_mode)
		return 1;
	if (psc == etr_lpsc_operational_step)
		return !aib->esw.y && aib->slsw.v1 &&
			aib->slsw.v2 && aib->slsw.v3;
	return 0;
}

/*
 * Check if two ports are on the same network.
 */
static int etr_compare_network(struct etr_aib *aib1, struct etr_aib *aib2)
{
	// FIXME: any other fields we have to compare?
	return aib1->edf1.net_id == aib2->edf1.net_id;
}

/*
 * Wrapper for etr_stei that converts physical port states
 * to logical port states to be consistent with the output
 * of stetr (see etr_psc vs. etr_lpsc).
 */
static void etr_steai_cv(struct etr_aib *aib, unsigned int func)
{
	BUG_ON(etr_steai(aib, func) != 0);
	/* Convert port state to logical port state. */
	if (aib->esw.psc0 == 1)
		aib->esw.psc0 = 2;
	else if (aib->esw.psc0 == 0 && aib->esw.p == 0)
		aib->esw.psc0 = 1;
	if (aib->esw.psc1 == 1)
		aib->esw.psc1 = 2;
	else if (aib->esw.psc1 == 0 && aib->esw.p == 1)
		aib->esw.psc1 = 1;
}

/*
 * Check if the aib a2 is still connected to the same attachment as
 * aib a1, the etv values differ by one and a2 is valid.
 */
static int etr_aib_follows(struct etr_aib *a1, struct etr_aib *a2, int p)
{
	int state_a1, state_a2;

	/* Paranoia check: e0/e1 should better be the same. */
	if (a1->esw.eacr.e0 != a2->esw.eacr.e0 ||
	    a1->esw.eacr.e1 != a2->esw.eacr.e1)
		return 0;

	/* Still connected to the same etr ? */
	state_a1 = p ? a1->esw.psc1 : a1->esw.psc0;
	state_a2 = p ? a2->esw.psc1 : a2->esw.psc0;
	if (state_a1 == etr_lpsc_operational_step) {
		if (state_a2 != etr_lpsc_operational_step ||
		    a1->edf1.net_id != a2->edf1.net_id ||
		    a1->edf1.etr_id != a2->edf1.etr_id ||
		    a1->edf1.etr_pn != a2->edf1.etr_pn)
			return 0;
	} else if (state_a2 != etr_lpsc_pps_mode)
		return 0;

	/* The ETV value of a2 needs to be ETV of a1 + 1. */
	if (a1->edf2.etv + 1 != a2->edf2.etv)
		return 0;

	if (!etr_port_valid(a2, p))
		return 0;

	return 1;
}

struct clock_sync_data {
	int in_sync;
	unsigned long long fixup_cc;
};

static void clock_sync_cpu_start(void *dummy)
{
	struct clock_sync_data *sync = dummy;

	enable_sync_clock();
	/*
	 * This looks like a busy wait loop but it isn't. etr_sync_cpus
	 * is called on all other cpus while the TOD clocks is stopped.
	 * __udelay will stop the cpu on an enabled wait psw until the
	 * TOD is running again.
	 */
	while (sync->in_sync == 0) {
		__udelay(1);
		/*
		 * A different cpu changes *in_sync. Therefore use
		 * barrier() to force memory access.
		 */
		barrier();
	}
	if (sync->in_sync != 1)
		/* Didn't work. Clear per-cpu in sync bit again. */
		disable_sync_clock(NULL);
	/*
	 * This round of TOD syncing is done. Set the clock comparator
	 * to the next tick and let the processor continue.
	 */
	fixup_clock_comparator(sync->fixup_cc);
}

static void clock_sync_cpu_end(void *dummy)
{
}

/*
 * Sync the TOD clock using the port refered to by aibp. This port
 * has to be enabled and the other port has to be disabled. The
 * last eacr update has to be more than 1.6 seconds in the past.
 */
static int etr_sync_clock(struct etr_aib *aib, int port)
{
	struct etr_aib *sync_port;
	struct clock_sync_data etr_sync;
	unsigned long long clock, old_clock, delay, delta;
	int follows;
	int rc;

	/* Check if the current aib is adjacent to the sync port aib. */
	sync_port = (port == 0) ? &etr_port0 : &etr_port1;
	follows = etr_aib_follows(sync_port, aib, port);
	memcpy(sync_port, aib, sizeof(*aib));
	if (!follows)
		return -EAGAIN;

	/*
	 * Catch all other cpus and make them wait until we have
	 * successfully synced the clock. smp_call_function will
	 * return after all other cpus are in etr_sync_cpu_start.
	 */
	memset(&etr_sync, 0, sizeof(etr_sync));
	preempt_disable();
	smp_call_function(clock_sync_cpu_start, &etr_sync, 0);
	local_irq_disable();
	enable_sync_clock();

	/* Set clock to next OTE. */
	__ctl_set_bit(14, 21);
	__ctl_set_bit(0, 29);
	clock = ((unsigned long long) (aib->edf2.etv + 1)) << 32;
	old_clock = get_clock();
	if (set_clock(clock) == 0) {
		__udelay(1);	/* Wait for the clock to start. */
		__ctl_clear_bit(0, 29);
		__ctl_clear_bit(14, 21);
		etr_stetr(aib);
		/* Adjust Linux timing variables. */
		delay = (unsigned long long)
			(aib->edf2.etv - sync_port->edf2.etv) << 32;
		delta = adjust_time(old_clock, clock, delay);
		etr_sync.fixup_cc = delta;
		fixup_clock_comparator(delta);
		/* Verify that the clock is properly set. */
		if (!etr_aib_follows(sync_port, aib, port)) {
			/* Didn't work. */
			disable_sync_clock(NULL);
			etr_sync.in_sync = -EAGAIN;
			rc = -EAGAIN;
		} else {
			etr_sync.in_sync = 1;
			rc = 0;
		}
	} else {
		/* Could not set the clock ?!? */
		__ctl_clear_bit(0, 29);
		__ctl_clear_bit(14, 21);
		disable_sync_clock(NULL);
		etr_sync.in_sync = -EAGAIN;
		rc = -EAGAIN;
	}
	local_irq_enable();
	smp_call_function(clock_sync_cpu_end, NULL, 0);
	preempt_enable();
	return rc;
}

/*
 * Handle the immediate effects of the different events.
 * The port change event is used for online/offline changes.
 */
static struct etr_eacr etr_handle_events(struct etr_eacr eacr)
{
	if (test_and_clear_bit(ETR_EVENT_SYNC_CHECK, &etr_events))
		eacr.es = 0;
	if (test_and_clear_bit(ETR_EVENT_SWITCH_LOCAL, &etr_events))
		eacr.es = eacr.sl = 0;
	if (test_and_clear_bit(ETR_EVENT_PORT_ALERT, &etr_events))
		etr_port0_uptodate = etr_port1_uptodate = 0;

	if (test_and_clear_bit(ETR_EVENT_PORT0_CHANGE, &etr_events)) {
		if (eacr.e0)
			/*
			 * Port change of an enabled port. We have to
			 * assume that this can have caused an stepping
			 * port switch.
			 */
			etr_tolec = get_clock();
		eacr.p0 = etr_port0_online;
		if (!eacr.p0)
			eacr.e0 = 0;
		etr_port0_uptodate = 0;
	}
	if (test_and_clear_bit(ETR_EVENT_PORT1_CHANGE, &etr_events)) {
		if (eacr.e1)
			/*
			 * Port change of an enabled port. We have to
			 * assume that this can have caused an stepping
			 * port switch.
			 */
			etr_tolec = get_clock();
		eacr.p1 = etr_port1_online;
		if (!eacr.p1)
			eacr.e1 = 0;
		etr_port1_uptodate = 0;
	}
	clear_bit(ETR_EVENT_UPDATE, &etr_events);
	return eacr;
}

/*
 * Set up a timer that expires after the etr_tolec + 1.6 seconds if
 * one of the ports needs an update.
 */
static void etr_set_tolec_timeout(unsigned long long now)
{
	unsigned long micros;

	if ((!etr_eacr.p0 || etr_port0_uptodate) &&
	    (!etr_eacr.p1 || etr_port1_uptodate))
		return;
	micros = (now > etr_tolec) ? ((now - etr_tolec) >> 12) : 0;
	micros = (micros > 1600000) ? 0 : 1600000 - micros;
	mod_timer(&etr_timer, jiffies + (micros * HZ) / 1000000 + 1);
}

/*
 * Set up a time that expires after 1/2 second.
 */
static void etr_set_sync_timeout(void)
{
	mod_timer(&etr_timer, jiffies + HZ/2);
}

/*
 * Update the aib information for one or both ports.
 */
static struct etr_eacr etr_handle_update(struct etr_aib *aib,
					 struct etr_eacr eacr)
{
	/* With both ports disabled the aib information is useless. */
	if (!eacr.e0 && !eacr.e1)
		return eacr;

	/* Update port0 or port1 with aib stored in etr_work_fn. */
	if (aib->esw.q == 0) {
		/* Information for port 0 stored. */
		if (eacr.p0 && !etr_port0_uptodate) {
			etr_port0 = *aib;
			if (etr_port0_online)
				etr_port0_uptodate = 1;
		}
	} else {
		/* Information for port 1 stored. */
		if (eacr.p1 && !etr_port1_uptodate) {
			etr_port1 = *aib;
			if (etr_port0_online)
				etr_port1_uptodate = 1;
		}
	}

	/*
	 * Do not try to get the alternate port aib if the clock
	 * is not in sync yet.
	 */
	if (!test_bit(CLOCK_SYNC_STP, &clock_sync_flags) && !eacr.es)
		return eacr;

	/*
	 * If steai is available we can get the information about
	 * the other port immediately. If only stetr is available the
	 * data-port bit toggle has to be used.
	 */
	if (etr_steai_available) {
		if (eacr.p0 && !etr_port0_uptodate) {
			etr_steai_cv(&etr_port0, ETR_STEAI_PORT_0);
			etr_port0_uptodate = 1;
		}
		if (eacr.p1 && !etr_port1_uptodate) {
			etr_steai_cv(&etr_port1, ETR_STEAI_PORT_1);
			etr_port1_uptodate = 1;
		}
	} else {
		/*
		 * One port was updated above, if the other
		 * port is not uptodate toggle dp bit.
		 */
		if ((eacr.p0 && !etr_port0_uptodate) ||
		    (eacr.p1 && !etr_port1_uptodate))
			eacr.dp ^= 1;
		else
			eacr.dp = 0;
	}
	return eacr;
}

/*
 * Write new etr control register if it differs from the current one.
 * Return 1 if etr_tolec has been updated as well.
 */
static void etr_update_eacr(struct etr_eacr eacr)
{
	int dp_changed;

	if (memcmp(&etr_eacr, &eacr, sizeof(eacr)) == 0)
		/* No change, return. */
		return;
	/*
	 * The disable of an active port of the change of the data port
	 * bit can/will cause a change in the data port.
	 */
	dp_changed = etr_eacr.e0 > eacr.e0 || etr_eacr.e1 > eacr.e1 ||
		(etr_eacr.dp ^ eacr.dp) != 0;
	etr_eacr = eacr;
	etr_setr(&etr_eacr);
	if (dp_changed)
		etr_tolec = get_clock();
}

/*
 * ETR tasklet. In this function you'll find the main logic. In
 * particular this is the only function that calls etr_update_eacr(),
 * it "controls" the etr control register.
 */
static void etr_work_fn(struct work_struct *work)
{
	unsigned long long now;
	struct etr_eacr eacr;
	struct etr_aib aib;
	int sync_port;

	/* Create working copy of etr_eacr. */
	eacr = etr_eacr;

	/* Check for the different events and their immediate effects. */
	eacr = etr_handle_events(eacr);

	/* Check if ETR is supposed to be active. */
	eacr.ea = eacr.p0 || eacr.p1;
	if (!eacr.ea) {
		/* Both ports offline. Reset everything. */
		eacr.dp = eacr.es = eacr.sl = 0;
		on_each_cpu(disable_sync_clock, NULL, 1);
		del_timer_sync(&etr_timer);
		etr_update_eacr(eacr);
		clear_bit(CLOCK_SYNC_ETR, &clock_sync_flags);
		return;
	}

	/* Store aib to get the current ETR status word. */
	BUG_ON(etr_stetr(&aib) != 0);
	etr_port0.esw = etr_port1.esw = aib.esw;	/* Copy status word. */
	now = get_clock();

	/*
	 * Update the port information if the last stepping port change
	 * or data port change is older than 1.6 seconds.
	 */
	if (now >= etr_tolec + (1600000 << 12))
		eacr = etr_handle_update(&aib, eacr);

	/*
	 * Select ports to enable. The prefered synchronization mode is PPS.
	 * If a port can be enabled depends on a number of things:
	 * 1) The port needs to be online and uptodate. A port is not
	 *    disabled just because it is not uptodate, but it is only
	 *    enabled if it is uptodate.
	 * 2) The port needs to have the same mode (pps / etr).
	 * 3) The port needs to be usable -> etr_port_valid() == 1
	 * 4) To enable the second port the clock needs to be in sync.
	 * 5) If both ports are useable and are ETR ports, the network id
	 *    has to be the same.
	 * The eacr.sl bit is used to indicate etr mode vs. pps mode.
	 */
	if (eacr.p0 && aib.esw.psc0 == etr_lpsc_pps_mode) {
		eacr.sl = 0;
		eacr.e0 = 1;
		if (!etr_mode_is_pps(etr_eacr))
			eacr.es = 0;
		if (!eacr.es || !eacr.p1 || aib.esw.psc1 != etr_lpsc_pps_mode)
			eacr.e1 = 0;
		// FIXME: uptodate checks ?
		else if (etr_port0_uptodate && etr_port1_uptodate)
			eacr.e1 = 1;
		sync_port = (etr_port0_uptodate &&
			     etr_port_valid(&etr_port0, 0)) ? 0 : -1;
	} else if (eacr.p1 && aib.esw.psc1 == etr_lpsc_pps_mode) {
		eacr.sl = 0;
		eacr.e0 = 0;
		eacr.e1 = 1;
		if (!etr_mode_is_pps(etr_eacr))
			eacr.es = 0;
		sync_port = (etr_port1_uptodate &&
			     etr_port_valid(&etr_port1, 1)) ? 1 : -1;
	} else if (eacr.p0 && aib.esw.psc0 == etr_lpsc_operational_step) {
		eacr.sl = 1;
		eacr.e0 = 1;
		if (!etr_mode_is_etr(etr_eacr))
			eacr.es = 0;
		if (!eacr.es || !eacr.p1 ||
		    aib.esw.psc1 != etr_lpsc_operational_alt)
			eacr.e1 = 0;
		else if (etr_port0_uptodate && etr_port1_uptodate &&
			 etr_compare_network(&etr_port0, &etr_port1))
			eacr.e1 = 1;
		sync_port = (etr_port0_uptodate &&
			     etr_port_valid(&etr_port0, 0)) ? 0 : -1;
	} else if (eacr.p1 && aib.esw.psc1 == etr_lpsc_operational_step) {
		eacr.sl = 1;
		eacr.e0 = 0;
		eacr.e1 = 1;
		if (!etr_mode_is_etr(etr_eacr))
			eacr.es = 0;
		sync_port = (etr_port1_uptodate &&
			     etr_port_valid(&etr_port1, 1)) ? 1 : -1;
	} else {
		/* Both ports not usable. */
		eacr.es = eacr.sl = 0;
		sync_port = -1;
		clear_bit(CLOCK_SYNC_ETR, &clock_sync_flags);
	}

	if (!test_bit(CLOCK_SYNC_ETR, &clock_sync_flags))
		eacr.es = 0;

	/*
	 * If the clock is in sync just update the eacr and return.
	 * If there is no valid sync port wait for a port update.
	 */
	if (test_bit(CLOCK_SYNC_STP, &clock_sync_flags) ||
	    eacr.es || sync_port < 0) {
		etr_update_eacr(eacr);
		etr_set_tolec_timeout(now);
		return;
	}

	/*
	 * Prepare control register for clock syncing
	 * (reset data port bit, set sync check control.
	 */
	eacr.dp = 0;
	eacr.es = 1;

	/*
	 * Update eacr and try to synchronize the clock. If the update
	 * of eacr caused a stepping port switch (or if we have to
	 * assume that a stepping port switch has occured) or the
	 * clock syncing failed, reset the sync check control bit
	 * and set up a timer to try again after 0.5 seconds
	 */
	etr_update_eacr(eacr);
	set_bit(CLOCK_SYNC_ETR, &clock_sync_flags);
	if (now < etr_tolec + (1600000 << 12) ||
	    etr_sync_clock(&aib, sync_port) != 0) {
		/* Sync failed. Try again in 1/2 second. */
		eacr.es = 0;
		etr_update_eacr(eacr);
		clear_bit(CLOCK_SYNC_ETR, &clock_sync_flags);
		etr_set_sync_timeout();
	} else
		etr_set_tolec_timeout(now);
}

/*
 * Sysfs interface functions
 */
static struct sysdev_class etr_sysclass = {
	.name	= "etr",
};

static struct sys_device etr_port0_dev = {
	.id	= 0,
	.cls	= &etr_sysclass,
};

static struct sys_device etr_port1_dev = {
	.id	= 1,
	.cls	= &etr_sysclass,
};

/*
 * ETR class attributes
 */
static ssize_t etr_stepping_port_show(struct sysdev_class *class, char *buf)
{
	return sprintf(buf, "%i\n", etr_port0.esw.p);
}

static SYSDEV_CLASS_ATTR(stepping_port, 0400, etr_stepping_port_show, NULL);

static ssize_t etr_stepping_mode_show(struct sysdev_class *class, char *buf)
{
	char *mode_str;

	if (etr_mode_is_pps(etr_eacr))
		mode_str = "pps";
	else if (etr_mode_is_etr(etr_eacr))
		mode_str = "etr";
	else
		mode_str = "local";
	return sprintf(buf, "%s\n", mode_str);
}

static SYSDEV_CLASS_ATTR(stepping_mode, 0400, etr_stepping_mode_show, NULL);

/*
 * ETR port attributes
 */
static inline struct etr_aib *etr_aib_from_dev(struct sys_device *dev)
{
	if (dev == &etr_port0_dev)
		return etr_port0_online ? &etr_port0 : NULL;
	else
		return etr_port1_online ? &etr_port1 : NULL;
}

static ssize_t etr_online_show(struct sys_device *dev,
				struct sysdev_attribute *attr,
				char *buf)
{
	unsigned int online;

	online = (dev == &etr_port0_dev) ? etr_port0_online : etr_port1_online;
	return sprintf(buf, "%i\n", online);
}

static ssize_t etr_online_store(struct sys_device *dev,
				struct sysdev_attribute *attr,
				const char *buf, size_t count)
{
	unsigned int value;

	value = simple_strtoul(buf, NULL, 0);
	if (value != 0 && value != 1)
		return -EINVAL;
	if (!test_bit(CLOCK_SYNC_HAS_ETR, &clock_sync_flags))
		return -EOPNOTSUPP;
	if (dev == &etr_port0_dev) {
		if (etr_port0_online == value)
			return count;	/* Nothing to do. */
		etr_port0_online = value;
		set_bit(ETR_EVENT_PORT0_CHANGE, &etr_events);
		schedule_work(&etr_work);
	} else {
		if (etr_port1_online == value)
			return count;	/* Nothing to do. */
		etr_port1_online = value;
		set_bit(ETR_EVENT_PORT1_CHANGE, &etr_events);
		schedule_work(&etr_work);
	}
	return count;
}

static SYSDEV_ATTR(online, 0600, etr_online_show, etr_online_store);

static ssize_t etr_stepping_control_show(struct sys_device *dev,
					struct sysdev_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%i\n", (dev == &etr_port0_dev) ?
		       etr_eacr.e0 : etr_eacr.e1);
}

static SYSDEV_ATTR(stepping_control, 0400, etr_stepping_control_show, NULL);

static ssize_t etr_mode_code_show(struct sys_device *dev,
				struct sysdev_attribute *attr, char *buf)
{
	if (!etr_port0_online && !etr_port1_online)
		/* Status word is not uptodate if both ports are offline. */
		return -ENODATA;
	return sprintf(buf, "%i\n", (dev == &etr_port0_dev) ?
		       etr_port0.esw.psc0 : etr_port0.esw.psc1);
}

static SYSDEV_ATTR(state_code, 0400, etr_mode_code_show, NULL);

static ssize_t etr_untuned_show(struct sys_device *dev,
				struct sysdev_attribute *attr, char *buf)
{
	struct etr_aib *aib = etr_aib_from_dev(dev);

	if (!aib || !aib->slsw.v1)
		return -ENODATA;
	return sprintf(buf, "%i\n", aib->edf1.u);
}

static SYSDEV_ATTR(untuned, 0400, etr_untuned_show, NULL);

static ssize_t etr_network_id_show(struct sys_device *dev,
				struct sysdev_attribute *attr, char *buf)
{
	struct etr_aib *aib = etr_aib_from_dev(dev);

	if (!aib || !aib->slsw.v1)
		return -ENODATA;
	return sprintf(buf, "%i\n", aib->edf1.net_id);
}

static SYSDEV_ATTR(network, 0400, etr_network_id_show, NULL);

static ssize_t etr_id_show(struct sys_device *dev,
			struct sysdev_attribute *attr, char *buf)
{
	struct etr_aib *aib = etr_aib_from_dev(dev);

	if (!aib || !aib->slsw.v1)
		return -ENODATA;
	return sprintf(buf, "%i\n", aib->edf1.etr_id);
}

static SYSDEV_ATTR(id, 0400, etr_id_show, NULL);

static ssize_t etr_port_number_show(struct sys_device *dev,
			struct sysdev_attribute *attr, char *buf)
{
	struct etr_aib *aib = etr_aib_from_dev(dev);

	if (!aib || !aib->slsw.v1)
		return -ENODATA;
	return sprintf(buf, "%i\n", aib->edf1.etr_pn);
}

static SYSDEV_ATTR(port, 0400, etr_port_number_show, NULL);

static ssize_t etr_coupled_show(struct sys_device *dev,
			struct sysdev_attribute *attr, char *buf)
{
	struct etr_aib *aib = etr_aib_from_dev(dev);

	if (!aib || !aib->slsw.v3)
		return -ENODATA;
	return sprintf(buf, "%i\n", aib->edf3.c);
}

static SYSDEV_ATTR(coupled, 0400, etr_coupled_show, NULL);

static ssize_t etr_local_time_show(struct sys_device *dev,
			struct sysdev_attribute *attr, char *buf)
{
	struct etr_aib *aib = etr_aib_from_dev(dev);

	if (!aib || !aib->slsw.v3)
		return -ENODATA;
	return sprintf(buf, "%i\n", aib->edf3.blto);
}

static SYSDEV_ATTR(local_time, 0400, etr_local_time_show, NULL);

static ssize_t etr_utc_offset_show(struct sys_device *dev,
			struct sysdev_attribute *attr, char *buf)
{
	struct etr_aib *aib = etr_aib_from_dev(dev);

	if (!aib || !aib->slsw.v3)
		return -ENODATA;
	return sprintf(buf, "%i\n", aib->edf3.buo);
}

static SYSDEV_ATTR(utc_offset, 0400, etr_utc_offset_show, NULL);

static struct sysdev_attribute *etr_port_attributes[] = {
	&attr_online,
	&attr_stepping_control,
	&attr_state_code,
	&attr_untuned,
	&attr_network,
	&attr_id,
	&attr_port,
	&attr_coupled,
	&attr_local_time,
	&attr_utc_offset,
	NULL
};

static int __init etr_register_port(struct sys_device *dev)
{
	struct sysdev_attribute **attr;
	int rc;

	rc = sysdev_register(dev);
	if (rc)
		goto out;
	for (attr = etr_port_attributes; *attr; attr++) {
		rc = sysdev_create_file(dev, *attr);
		if (rc)
			goto out_unreg;
	}
	return 0;
out_unreg:
	for (; attr >= etr_port_attributes; attr--)
		sysdev_remove_file(dev, *attr);
	sysdev_unregister(dev);
out:
	return rc;
}

static void __init etr_unregister_port(struct sys_device *dev)
{
	struct sysdev_attribute **attr;

	for (attr = etr_port_attributes; *attr; attr++)
		sysdev_remove_file(dev, *attr);
	sysdev_unregister(dev);
}

static int __init etr_init_sysfs(void)
{
	int rc;

	rc = sysdev_class_register(&etr_sysclass);
	if (rc)
		goto out;
	rc = sysdev_class_create_file(&etr_sysclass, &attr_stepping_port);
	if (rc)
		goto out_unreg_class;
	rc = sysdev_class_create_file(&etr_sysclass, &attr_stepping_mode);
	if (rc)
		goto out_remove_stepping_port;
	rc = etr_register_port(&etr_port0_dev);
	if (rc)
		goto out_remove_stepping_mode;
	rc = etr_register_port(&etr_port1_dev);
	if (rc)
		goto out_remove_port0;
	return 0;

out_remove_port0:
	etr_unregister_port(&etr_port0_dev);
out_remove_stepping_mode:
	sysdev_class_remove_file(&etr_sysclass, &attr_stepping_mode);
out_remove_stepping_port:
	sysdev_class_remove_file(&etr_sysclass, &attr_stepping_port);
out_unreg_class:
	sysdev_class_unregister(&etr_sysclass);
out:
	return rc;
}

device_initcall(etr_init_sysfs);

/*
 * Server Time Protocol (STP) code.
 */
static int stp_online;
static struct stp_sstpi stp_info;
static void *stp_page;

static void stp_work_fn(struct work_struct *work);
static DECLARE_WORK(stp_work, stp_work_fn);

static int __init early_parse_stp(char *p)
{
	if (strncmp(p, "off", 3) == 0)
		stp_online = 0;
	else if (strncmp(p, "on", 2) == 0)
		stp_online = 1;
	return 0;
}
early_param("stp", early_parse_stp);

/*
 * Reset STP attachment.
 */
static void __init stp_reset(void)
{
	int rc;

	stp_page = alloc_bootmem_pages(PAGE_SIZE);
	rc = chsc_sstpc(stp_page, STP_OP_CTRL, 0x0000);
	if (rc == 1)
		set_bit(CLOCK_SYNC_HAS_STP, &clock_sync_flags);
	else if (stp_online) {
		printk(KERN_WARNING "Running on non STP capable machine.\n");
		free_bootmem((unsigned long) stp_page, PAGE_SIZE);
		stp_page = NULL;
		stp_online = 0;
	}
}

static int __init stp_init(void)
{
	if (test_bit(CLOCK_SYNC_HAS_STP, &clock_sync_flags) && stp_online)
		schedule_work(&stp_work);
	return 0;
}

arch_initcall(stp_init);

/*
 * STP timing alert. There are three causes:
 * 1) timing status change
 * 2) link availability change
 * 3) time control parameter change
 * In all three cases we are only interested in the clock source state.
 * If a STP clock source is now available use it.
 */
static void stp_timing_alert(struct stp_irq_parm *intparm)
{
	if (intparm->tsc || intparm->lac || intparm->tcpc)
		schedule_work(&stp_work);
}

/*
 * STP sync check machine check. This is called when the timing state
 * changes from the synchronized state to the unsynchronized state.
 * After a STP sync check the clock is not in sync. The machine check
 * is broadcasted to all cpus at the same time.
 */
void stp_sync_check(void)
{
	if (!test_bit(CLOCK_SYNC_STP, &clock_sync_flags))
		return;
	disable_sync_clock(NULL);
	schedule_work(&stp_work);
}

/*
 * STP island condition machine check. This is called when an attached
 * server  attempts to communicate over an STP link and the servers
 * have matching CTN ids and have a valid stratum-1 configuration
 * but the configurations do not match.
 */
void stp_island_check(void)
{
	if (!test_bit(CLOCK_SYNC_STP, &clock_sync_flags))
		return;
	disable_sync_clock(NULL);
	schedule_work(&stp_work);
}

/*
 * STP tasklet. Check for the STP state and take over the clock
 * synchronization if the STP clock source is usable.
 */
static void stp_work_fn(struct work_struct *work)
{
	struct clock_sync_data stp_sync;
	unsigned long long old_clock, delta;
	int rc;

	if (!stp_online) {
		chsc_sstpc(stp_page, STP_OP_CTRL, 0x0000);
		return;
	}

	rc = chsc_sstpc(stp_page, STP_OP_CTRL, 0xb0e0);
	if (rc)
		return;

	rc = chsc_sstpi(stp_page, &stp_info, sizeof(struct stp_sstpi));
	if (rc || stp_info.c == 0)
		return;

	/*
	 * Catch all other cpus and make them wait until we have
	 * successfully synced the clock. smp_call_function will
	 * return after all other cpus are in clock_sync_cpu_start.
	 */
	memset(&stp_sync, 0, sizeof(stp_sync));
	preempt_disable();
	smp_call_function(clock_sync_cpu_start, &stp_sync, 0);
	local_irq_disable();
	enable_sync_clock();

	set_bit(CLOCK_SYNC_STP, &clock_sync_flags);
	if (test_and_clear_bit(CLOCK_SYNC_ETR, &clock_sync_flags))
		schedule_work(&etr_work);

	rc = 0;
	if (stp_info.todoff[0] || stp_info.todoff[1] ||
	    stp_info.todoff[2] || stp_info.todoff[3] ||
	    stp_info.tmd != 2) {
		old_clock = get_clock();
		rc = chsc_sstpc(stp_page, STP_OP_SYNC, 0);
		if (rc == 0) {
			delta = adjust_time(old_clock, get_clock(), 0);
			fixup_clock_comparator(delta);
			rc = chsc_sstpi(stp_page, &stp_info,
					sizeof(struct stp_sstpi));
			if (rc == 0 && stp_info.tmd != 2)
				rc = -EAGAIN;
		}
	}
	if (rc) {
		disable_sync_clock(NULL);
		stp_sync.in_sync = -EAGAIN;
		clear_bit(CLOCK_SYNC_STP, &clock_sync_flags);
		if (etr_port0_online || etr_port1_online)
			schedule_work(&etr_work);
	} else
		stp_sync.in_sync = 1;

	local_irq_enable();
	smp_call_function(clock_sync_cpu_end, NULL, 0);
	preempt_enable();
}

/*
 * STP class sysfs interface functions
 */
static struct sysdev_class stp_sysclass = {
	.name	= "stp",
};

static ssize_t stp_ctn_id_show(struct sysdev_class *class, char *buf)
{
	if (!stp_online)
		return -ENODATA;
	return sprintf(buf, "%016llx\n",
		       *(unsigned long long *) stp_info.ctnid);
}

static SYSDEV_CLASS_ATTR(ctn_id, 0400, stp_ctn_id_show, NULL);

static ssize_t stp_ctn_type_show(struct sysdev_class *class, char *buf)
{
	if (!stp_online)
		return -ENODATA;
	return sprintf(buf, "%i\n", stp_info.ctn);
}

static SYSDEV_CLASS_ATTR(ctn_type, 0400, stp_ctn_type_show, NULL);

static ssize_t stp_dst_offset_show(struct sysdev_class *class, char *buf)
{
	if (!stp_online || !(stp_info.vbits & 0x2000))
		return -ENODATA;
	return sprintf(buf, "%i\n", (int)(s16) stp_info.dsto);
}

static SYSDEV_CLASS_ATTR(dst_offset, 0400, stp_dst_offset_show, NULL);

static ssize_t stp_leap_seconds_show(struct sysdev_class *class, char *buf)
{
	if (!stp_online || !(stp_info.vbits & 0x8000))
		return -ENODATA;
	return sprintf(buf, "%i\n", (int)(s16) stp_info.leaps);
}

static SYSDEV_CLASS_ATTR(leap_seconds, 0400, stp_leap_seconds_show, NULL);

static ssize_t stp_stratum_show(struct sysdev_class *class, char *buf)
{
	if (!stp_online)
		return -ENODATA;
	return sprintf(buf, "%i\n", (int)(s16) stp_info.stratum);
}

static SYSDEV_CLASS_ATTR(stratum, 0400, stp_stratum_show, NULL);

static ssize_t stp_time_offset_show(struct sysdev_class *class, char *buf)
{
	if (!stp_online || !(stp_info.vbits & 0x0800))
		return -ENODATA;
	return sprintf(buf, "%i\n", (int) stp_info.tto);
}

static SYSDEV_CLASS_ATTR(time_offset, 0400, stp_time_offset_show, NULL);

static ssize_t stp_time_zone_offset_show(struct sysdev_class *class, char *buf)
{
	if (!stp_online || !(stp_info.vbits & 0x4000))
		return -ENODATA;
	return sprintf(buf, "%i\n", (int)(s16) stp_info.tzo);
}

static SYSDEV_CLASS_ATTR(time_zone_offset, 0400,
			 stp_time_zone_offset_show, NULL);

static ssize_t stp_timing_mode_show(struct sysdev_class *class, char *buf)
{
	if (!stp_online)
		return -ENODATA;
	return sprintf(buf, "%i\n", stp_info.tmd);
}

static SYSDEV_CLASS_ATTR(timing_mode, 0400, stp_timing_mode_show, NULL);

static ssize_t stp_timing_state_show(struct sysdev_class *class, char *buf)
{
	if (!stp_online)
		return -ENODATA;
	return sprintf(buf, "%i\n", stp_info.tst);
}

static SYSDEV_CLASS_ATTR(timing_state, 0400, stp_timing_state_show, NULL);

static ssize_t stp_online_show(struct sysdev_class *class, char *buf)
{
	return sprintf(buf, "%i\n", stp_online);
}

static ssize_t stp_online_store(struct sysdev_class *class,
				const char *buf, size_t count)
{
	unsigned int value;

	value = simple_strtoul(buf, NULL, 0);
	if (value != 0 && value != 1)
		return -EINVAL;
	if (!test_bit(CLOCK_SYNC_HAS_STP, &clock_sync_flags))
		return -EOPNOTSUPP;
	stp_online = value;
	schedule_work(&stp_work);
	return count;
}

/*
 * Can't use SYSDEV_CLASS_ATTR because the attribute should be named
 * stp/online but attr_online already exists in this file ..
 */
static struct sysdev_class_attribute attr_stp_online = {
	.attr = { .name = "online", .mode = 0600 },
	.show	= stp_online_show,
	.store	= stp_online_store,
};

static struct sysdev_class_attribute *stp_attributes[] = {
	&attr_ctn_id,
	&attr_ctn_type,
	&attr_dst_offset,
	&attr_leap_seconds,
	&attr_stp_online,
	&attr_stratum,
	&attr_time_offset,
	&attr_time_zone_offset,
	&attr_timing_mode,
	&attr_timing_state,
	NULL
};

static int __init stp_init_sysfs(void)
{
	struct sysdev_class_attribute **attr;
	int rc;

	rc = sysdev_class_register(&stp_sysclass);
	if (rc)
		goto out;
	for (attr = stp_attributes; *attr; attr++) {
		rc = sysdev_class_create_file(&stp_sysclass, *attr);
		if (rc)
			goto out_unreg;
	}
	return 0;
out_unreg:
	for (; attr >= stp_attributes; attr--)
		sysdev_class_remove_file(&stp_sysclass, *attr);
	sysdev_class_unregister(&stp_sysclass);
out:
	return rc;
}

device_initcall(stp_init_sysfs);
