/*
 *  arch/sh/kernel/time.c
 *
 *  Copyright (C) 1999  Tetsuya Okada & Niibe Yutaka
 *  Copyright (C) 2000  Philipp Rumpf <prumpf@tux.org>
 *  Copyright (C) 2002, 2003, 2004  Paul Mundt
 *  Copyright (C) 2002  M. R. Brown  <mrbrown@linux-sh.org>
 *
 *  Some code taken from i386 version.
 *    Copyright (C) 1991, 1992, 1995  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/profile.h>

#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/delay.h>
#include <asm/machvec.h>
#include <asm/rtc.h>
#include <asm/freq.h>
#include <asm/cpu/timer.h>
#ifdef CONFIG_SH_KGDB
#include <asm/kgdb.h>
#endif

#include <linux/timex.h>
#include <linux/irq.h>

#define TMU_TOCR_INIT	0x00
#define TMU0_TCR_INIT	0x0020
#define TMU_TSTR_INIT	1

#define TMU0_TCR_CALIB	0x0000

#ifdef CONFIG_CPU_SUBTYPE_ST40STB1
#define CLOCKGEN_MEMCLKCR 0xbb040038
#define MEMCLKCR_RATIO_MASK 0x7
#endif /* CONFIG_CPU_SUBTYPE_ST40STB1 */

extern unsigned long wall_jiffies;
#define TICK_SIZE (tick_nsec / 1000)
DEFINE_SPINLOCK(tmu0_lock);

/* XXX: Can we initialize this in a routine somewhere?  Dreamcast doesn't want
 * these routines anywhere... */
#ifdef CONFIG_SH_RTC
void (*rtc_get_time)(struct timespec *) = sh_rtc_gettimeofday;
int (*rtc_set_time)(const time_t) = sh_rtc_settimeofday;
#else
void (*rtc_get_time)(struct timespec *);
int (*rtc_set_time)(const time_t);
#endif

#if defined(CONFIG_CPU_SUBTYPE_SH7300)
static int md_table[] = { 1, 2, 3, 4, 6, 8, 12 };
#endif
#if defined(CONFIG_CPU_SH3)
static int stc_multipliers[] = { 1, 2, 3, 4, 6, 1, 1, 1 };
static int stc_values[]      = { 0, 1, 4, 2, 5, 0, 0, 0 };
#define bfc_divisors stc_multipliers
#define bfc_values stc_values
static int ifc_divisors[]    = { 1, 2, 3, 4, 1, 1, 1, 1 };
static int ifc_values[]      = { 0, 1, 4, 2, 0, 0, 0, 0 };
static int pfc_divisors[]    = { 1, 2, 3, 4, 6, 1, 1, 1 };
static int pfc_values[]      = { 0, 1, 4, 2, 5, 0, 0, 0 };
#elif defined(CONFIG_CPU_SH4)
#if defined(CONFIG_CPU_SUBTYPE_SH73180)
static int ifc_divisors[] = { 1, 2, 3, 4, 6, 8, 12, 16 };
static int ifc_values[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
#define bfc_divisors ifc_divisors	/* Same */
#define bfc_values ifc_values
#define pfc_divisors ifc_divisors	/* Same */
#define pfc_values ifc_values
#else
static int ifc_divisors[] = { 1, 2, 3, 4, 6, 8, 1, 1 };
static int ifc_values[]   = { 0, 1, 2, 3, 0, 4, 0, 5 };
#define bfc_divisors ifc_divisors	/* Same */
#define bfc_values ifc_values
static int pfc_divisors[] = { 2, 3, 4, 6, 8, 2, 2, 2 };
static int pfc_values[]   = { 0, 0, 1, 2, 0, 3, 0, 4 };
#endif
#else
#error "Unknown ifc/bfc/pfc/stc values for this processor"
#endif

/*
 * Scheduler clock - returns current time in nanosec units.
 */
unsigned long long sched_clock(void)
{
	return (unsigned long long)jiffies * (1000000000 / HZ);
}

static unsigned long do_gettimeoffset(void)
{
	int count;
	unsigned long flags;

	static int count_p = 0x7fffffff;    /* for the first call after boot */
	static unsigned long jiffies_p = 0;

	/*
	 * cache volatile jiffies temporarily; we have IRQs turned off.
	 */
	unsigned long jiffies_t;

	spin_lock_irqsave(&tmu0_lock, flags);
	/* timer count may underflow right here */
	count = ctrl_inl(TMU0_TCNT);	/* read the latched count */

	jiffies_t = jiffies;

	/*
	 * avoiding timer inconsistencies (they are rare, but they happen)...
	 * there is one kind of problem that must be avoided here:
	 *  1. the timer counter underflows
	 */

	if( jiffies_t == jiffies_p ) {
		if( count > count_p ) {
			/* the nutcase */

			if(ctrl_inw(TMU0_TCR) & 0x100) { /* Check UNF bit */
				/*
				 * We cannot detect lost timer interrupts ...
				 * well, that's why we call them lost, don't we? :)
				 * [hmm, on the Pentium and Alpha we can ... sort of]
				 */
				count -= LATCH;
			} else {
				printk("do_slow_gettimeoffset(): hardware timer problem?\n");
			}
		}
	} else
		jiffies_p = jiffies_t;

	count_p = count;
	spin_unlock_irqrestore(&tmu0_lock, flags);

	count = ((LATCH-1) - count) * TICK_SIZE;
	count = (count + LATCH/2) / LATCH;

	return count;
}

void do_gettimeofday(struct timeval *tv)
{
	unsigned long seq;
	unsigned long usec, sec;
	unsigned long lost;

	do {
		seq = read_seqbegin(&xtime_lock);
		usec = do_gettimeoffset();

		lost = jiffies - wall_jiffies;
		if (lost)
			usec += lost * (1000000 / HZ);

		sec = xtime.tv_sec;
		usec += xtime.tv_nsec / 1000;
	} while (read_seqretry(&xtime_lock, seq));

	while (usec >= 1000000) {
		usec -= 1000000;
		sec++;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

EXPORT_SYMBOL(do_gettimeofday);

int do_settimeofday(struct timespec *tv)
{
	time_t wtm_sec, sec = tv->tv_sec;
	long wtm_nsec, nsec = tv->tv_nsec;

	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	write_seqlock_irq(&xtime_lock);
	/*
	 * This is revolting. We need to set "xtime" correctly. However, the
	 * value in this location is the value at the most recent update of
	 * wall time.  Discover what correction gettimeofday() would have
	 * made, and then undo it!
	 */
	nsec -= 1000 * (do_gettimeoffset() +
				(jiffies - wall_jiffies) * (1000000 / HZ));

	wtm_sec  = wall_to_monotonic.tv_sec + (xtime.tv_sec - sec);
	wtm_nsec = wall_to_monotonic.tv_nsec + (xtime.tv_nsec - nsec);

	set_normalized_timespec(&xtime, sec, nsec);
	set_normalized_timespec(&wall_to_monotonic, wtm_sec, wtm_nsec);

	ntp_clear();
	write_sequnlock_irq(&xtime_lock);
	clock_was_set();

	return 0;
}

EXPORT_SYMBOL(do_settimeofday);

/* last time the RTC clock got updated */
static long last_rtc_update;

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
static inline void do_timer_interrupt(int irq, struct pt_regs *regs)
{
	do_timer(regs);
#ifndef CONFIG_SMP
	update_process_times(user_mode(regs));
#endif
	profile_tick(CPU_PROFILING, regs);

#ifdef CONFIG_HEARTBEAT
	if (sh_mv.mv_heartbeat != NULL)
		sh_mv.mv_heartbeat();
#endif

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * RTC clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if (ntp_synced() &&
	    xtime.tv_sec > last_rtc_update + 660 &&
	    (xtime.tv_nsec / 1000) >= 500000 - ((unsigned) TICK_SIZE) / 2 &&
	    (xtime.tv_nsec / 1000) <= 500000 + ((unsigned) TICK_SIZE) / 2) {
		if (rtc_set_time(xtime.tv_sec) == 0)
			last_rtc_update = xtime.tv_sec;
		else
			last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */
	}
}

/*
 * This is the same as the above, except we _also_ save the current
 * Time Stamp Counter value at the time of the timer interrupt, so that
 * we later on can estimate the time of day more exactly.
 */
static irqreturn_t timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long timer_status;

	/* Clear UNF bit */
	timer_status = ctrl_inw(TMU0_TCR);
	timer_status &= ~0x100;
	ctrl_outw(timer_status, TMU0_TCR);

	/*
	 * Here we are in the timer irq handler. We just have irqs locally
	 * disabled but we don't know if the timer_bh is running on the other
	 * CPU. We need to avoid to SMP race with it. NOTE: we don' t need
	 * the irq version of write_lock because as just said we have irq
	 * locally disabled. -arca
	 */
	write_seqlock(&xtime_lock);
	do_timer_interrupt(irq, regs);
	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

/*
 * Hah!  We'll see if this works (switching from usecs to nsecs).
 */
static unsigned int __init get_timer_frequency(void)
{
	u32 freq;
	struct timespec ts1, ts2;
	unsigned long diff_nsec;
	unsigned long factor;

	/* Setup the timer:  We don't want to generate interrupts, just
	 * have it count down at its natural rate.
	 */
	ctrl_outb(0, TMU_TSTR);
#if !defined(CONFIG_CPU_SUBTYPE_SH7300)
	ctrl_outb(TMU_TOCR_INIT, TMU_TOCR);
#endif
	ctrl_outw(TMU0_TCR_CALIB, TMU0_TCR);
	ctrl_outl(0xffffffff, TMU0_TCOR);
	ctrl_outl(0xffffffff, TMU0_TCNT);

	rtc_get_time(&ts2);

	do {
		rtc_get_time(&ts1);
	} while (ts1.tv_nsec == ts2.tv_nsec && ts1.tv_sec == ts2.tv_sec);

	/* actually start the timer */
	ctrl_outb(TMU_TSTR_INIT, TMU_TSTR);

	do {
		rtc_get_time(&ts2);
	} while (ts1.tv_nsec == ts2.tv_nsec && ts1.tv_sec == ts2.tv_sec);

	freq = 0xffffffff - ctrl_inl(TMU0_TCNT);
	if (ts2.tv_nsec < ts1.tv_nsec) {
		ts2.tv_nsec += 1000000000;
		ts2.tv_sec--;
	}

	diff_nsec = (ts2.tv_sec - ts1.tv_sec) * 1000000000 + (ts2.tv_nsec - ts1.tv_nsec);

	/* this should work well if the RTC has a precision of n Hz, where
	 * n is an integer.  I don't think we have to worry about the other
	 * cases. */
	factor = (1000000000 + diff_nsec/2) / diff_nsec;

	if (factor * diff_nsec > 1100000000 ||
	    factor * diff_nsec <  900000000)
		panic("weird RTC (diff_nsec %ld)", diff_nsec);

	return freq * factor;
}

void (*board_time_init)(void);
void (*board_timer_setup)(struct irqaction *irq);

static unsigned int sh_pclk_freq __initdata = CONFIG_SH_PCLK_FREQ;

static int __init sh_pclk_setup(char *str)
{
        unsigned int freq;

	if (get_option(&str, &freq))
		sh_pclk_freq = freq;

	return 1;
}
__setup("sh_pclk=", sh_pclk_setup);

static struct irqaction irq0  = { timer_interrupt, SA_INTERRUPT, CPU_MASK_NONE, "timer", NULL, NULL};

void get_current_frequency_divisors(unsigned int *ifc, unsigned int *bfc, unsigned int *pfc)
{
	unsigned int frqcr = ctrl_inw(FRQCR);

#if defined(CONFIG_CPU_SH3)
#if defined(CONFIG_CPU_SUBTYPE_SH7300)
	*ifc = md_table[((frqcr & 0x0070) >> 4)];
	*bfc = md_table[((frqcr & 0x0700) >> 8)];
	*pfc = md_table[frqcr & 0x0007];
#elif defined(CONFIG_CPU_SUBTYPE_SH7705)
	*bfc = stc_multipliers[(frqcr & 0x0300) >> 8];
	*ifc = ifc_divisors[(frqcr & 0x0030) >> 4];
	*pfc = pfc_divisors[frqcr & 0x0003];
#else
	unsigned int tmp;

	tmp  = (frqcr & 0x8000) >> 13;
	tmp |= (frqcr & 0x0030) >>  4;
	*bfc = stc_multipliers[tmp];
	tmp  = (frqcr & 0x4000)  >> 12;
	tmp |= (frqcr & 0x000c) >> 2;
	*ifc = ifc_divisors[tmp];
	tmp  = (frqcr & 0x2000) >> 11;
	tmp |= frqcr & 0x0003;
	*pfc = pfc_divisors[tmp];
#endif
#elif defined(CONFIG_CPU_SH4)
#if defined(CONFIG_CPU_SUBTYPE_SH73180)
	*ifc = ifc_divisors[(frqcr>> 20) & 0x0007];
	*bfc = bfc_divisors[(frqcr>> 12) & 0x0007];
	*pfc = pfc_divisors[frqcr & 0x0007];
#else
	*ifc = ifc_divisors[(frqcr >> 6) & 0x0007];
	*bfc = bfc_divisors[(frqcr >> 3) & 0x0007];
	*pfc = pfc_divisors[frqcr & 0x0007];
#endif
#endif
}

/*
 * This bit of ugliness builds up accessor routines to get at both
 * the divisors and the physical values.
 */
#define _FREQ_TABLE(x) \
	unsigned int get_##x##_divisor(unsigned int value)	\
		{ return x##_divisors[value]; }			\
								\
	unsigned int get_##x##_value(unsigned int divisor)	\
		{ return x##_values[(divisor - 1)]; }

_FREQ_TABLE(ifc);
_FREQ_TABLE(bfc);
_FREQ_TABLE(pfc);

#ifdef CONFIG_CPU_SUBTYPE_ST40STB1

/*
 * The ST40 divisors are totally different so we set the cpu data
 * clocks using a different algorithm
 *
 * I've just plugged this from the 2.4 code
 *	- Alex Bennee <kernel-hacker@bennee.com>
 */
#define CCN_PVR_CHIP_SHIFT 24
#define CCN_PVR_CHIP_MASK  0xff
#define CCN_PVR_CHIP_ST40STB1 0x4


struct frqcr_data {
	unsigned short frqcr;

	struct {
		unsigned char multiplier;
		unsigned char divisor;
	} factor[3];
};

static struct frqcr_data st40_frqcr_table[] = {
	{ 0x000, {{1,1}, {1,1}, {1,2}}},
	{ 0x002, {{1,1}, {1,1}, {1,4}}},
	{ 0x004, {{1,1}, {1,1}, {1,8}}},
	{ 0x008, {{1,1}, {1,2}, {1,2}}},
	{ 0x00A, {{1,1}, {1,2}, {1,4}}},
	{ 0x00C, {{1,1}, {1,2}, {1,8}}},
	{ 0x011, {{1,1}, {2,3}, {1,6}}},
	{ 0x013, {{1,1}, {2,3}, {1,3}}},
	{ 0x01A, {{1,1}, {1,2}, {1,4}}},
	{ 0x01C, {{1,1}, {1,2}, {1,8}}},
	{ 0x023, {{1,1}, {2,3}, {1,3}}},
	{ 0x02C, {{1,1}, {1,2}, {1,8}}},
	{ 0x048, {{1,2}, {1,2}, {1,4}}},
	{ 0x04A, {{1,2}, {1,2}, {1,6}}},
	{ 0x04C, {{1,2}, {1,2}, {1,8}}},
	{ 0x05A, {{1,2}, {1,3}, {1,6}}},
	{ 0x05C, {{1,2}, {1,3}, {1,6}}},
	{ 0x063, {{1,2}, {1,4}, {1,4}}},
	{ 0x06C, {{1,2}, {1,4}, {1,8}}},
	{ 0x091, {{1,3}, {1,3}, {1,6}}},
	{ 0x093, {{1,3}, {1,3}, {1,6}}},
	{ 0x0A3, {{1,3}, {1,6}, {1,6}}},
	{ 0x0DA, {{1,4}, {1,4}, {1,8}}},
	{ 0x0DC, {{1,4}, {1,4}, {1,8}}},
	{ 0x0EC, {{1,4}, {1,8}, {1,8}}},
	{ 0x123, {{1,4}, {1,4}, {1,8}}},
	{ 0x16C, {{1,4}, {1,8}, {1,8}}},
};

struct memclk_data {
	unsigned char multiplier;
	unsigned char divisor;
};

static struct memclk_data st40_memclk_table[8] = {
	{1,1},	// 000
	{1,2},	// 001
	{1,3},	// 010
	{2,3},	// 011
	{1,4},	// 100
	{1,6},	// 101
	{1,8},	// 110
	{1,8}	// 111
};

static void st40_specific_time_init(unsigned int module_clock, unsigned short frqcr)
{
	unsigned int cpu_clock, master_clock, bus_clock, memory_clock;
	struct frqcr_data *d;
	int a;
	unsigned long memclkcr;
	struct memclk_data *e;

	for (a = 0; a < ARRAY_SIZE(st40_frqcr_table); a++) {
		d = &st40_frqcr_table[a];

		if (d->frqcr == (frqcr & 0x1ff))
			break;
	}

	if (a == ARRAY_SIZE(st40_frqcr_table)) {
		d = st40_frqcr_table;

		printk("ERROR: Unrecognised FRQCR value (0x%x), "
		       "using default multipliers\n", frqcr);
	}

	memclkcr = ctrl_inl(CLOCKGEN_MEMCLKCR);
	e = &st40_memclk_table[memclkcr & MEMCLKCR_RATIO_MASK];

	printk(KERN_INFO "Clock multipliers: CPU: %d/%d Bus: %d/%d "
	       "Mem: %d/%d Periph: %d/%d\n",
	       d->factor[0].multiplier, d->factor[0].divisor,
	       d->factor[1].multiplier, d->factor[1].divisor,
	       e->multiplier,           e->divisor,
	       d->factor[2].multiplier, d->factor[2].divisor);

	master_clock = module_clock * d->factor[2].divisor
				    / d->factor[2].multiplier;
	bus_clock    = master_clock * d->factor[1].multiplier
				    / d->factor[1].divisor;
	memory_clock = master_clock * e->multiplier
				    / e->divisor;
	cpu_clock    = master_clock * d->factor[0].multiplier
				    / d->factor[0].divisor;

	current_cpu_data.cpu_clock    = cpu_clock;
	current_cpu_data.master_clock = master_clock;
	current_cpu_data.bus_clock    = bus_clock;
	current_cpu_data.memory_clock = memory_clock;
	current_cpu_data.module_clock = module_clock;
}
#endif

void __init time_init(void)
{
	unsigned int timer_freq = 0;
	unsigned int ifc, pfc, bfc;
	unsigned long interval;
#ifdef CONFIG_CPU_SUBTYPE_ST40STB1
	unsigned long pvr;
	unsigned short frqcr;
#endif

	if (board_time_init)
		board_time_init();

	/*
	 * If we don't have an RTC (such as with the SH7300), don't attempt to
	 * probe the timer frequency. Rely on an either hardcoded peripheral
	 * clock value, or on the sh_pclk command line option. Note that we
	 * still need to have CONFIG_SH_PCLK_FREQ set in order for things like
	 * CLOCK_TICK_RATE to be sane.
	 */
	current_cpu_data.module_clock = sh_pclk_freq;

#ifdef CONFIG_SH_PCLK_CALC
	/* XXX: Switch this over to a more generic test. */
	{
		unsigned int freq;

		/*
		 * If we've specified a peripheral clock frequency, and we have
		 * an RTC, compare it against the autodetected value. Complain
		 * if there's a mismatch.
		 */
		timer_freq = get_timer_frequency();
		freq = timer_freq * 4;

		if (sh_pclk_freq && (sh_pclk_freq/100*99 > freq || sh_pclk_freq/100*101 < freq)) {
			printk(KERN_NOTICE "Calculated peripheral clock value "
			       "%d differs from sh_pclk value %d, fixing..\n",
			       freq, sh_pclk_freq);
			current_cpu_data.module_clock = freq;
		}
	}
#endif

#ifdef CONFIG_CPU_SUBTYPE_ST40STB1
	/* XXX: Update ST40 code to use board_time_init() */
	pvr = ctrl_inl(CCN_PVR);
	frqcr = ctrl_inw(FRQCR);
	printk("time.c ST40 Probe: PVR %08lx, FRQCR %04hx\n", pvr, frqcr);

	if (((pvr >> CCN_PVR_CHIP_SHIFT) & CCN_PVR_CHIP_MASK) == CCN_PVR_CHIP_ST40STB1)
		st40_specific_time_init(current_cpu_data.module_clock, frqcr);
	else
#endif
		get_current_frequency_divisors(&ifc, &bfc, &pfc);

	if (rtc_get_time) {
		rtc_get_time(&xtime);
	} else {
		xtime.tv_sec = mktime(2000, 1, 1, 0, 0, 0);
		xtime.tv_nsec = 0;
	}

        set_normalized_timespec(&wall_to_monotonic,
                                -xtime.tv_sec, -xtime.tv_nsec);

	if (board_timer_setup) {
		board_timer_setup(&irq0);
	} else {
		setup_irq(TIMER_IRQ, &irq0);
	}

	/*
	 * for ST40 chips the current_cpu_data should already be set
	 * so not having valid pfc/bfc/ifc shouldn't be a problem
	 */
	if (!current_cpu_data.master_clock)
		current_cpu_data.master_clock = current_cpu_data.module_clock * pfc;
	if (!current_cpu_data.bus_clock)
		current_cpu_data.bus_clock = current_cpu_data.master_clock / bfc;
	if (!current_cpu_data.cpu_clock)
		current_cpu_data.cpu_clock = current_cpu_data.master_clock / ifc;

	printk("CPU clock: %d.%02dMHz\n",
	       (current_cpu_data.cpu_clock / 1000000),
	       (current_cpu_data.cpu_clock % 1000000)/10000);
	printk("Bus clock: %d.%02dMHz\n",
	       (current_cpu_data.bus_clock / 1000000),
	       (current_cpu_data.bus_clock % 1000000)/10000);
#ifdef CONFIG_CPU_SUBTYPE_ST40STB1
	printk("Memory clock: %d.%02dMHz\n",
	       (current_cpu_data.memory_clock / 1000000),
	       (current_cpu_data.memory_clock % 1000000)/10000);
#endif
	printk("Module clock: %d.%02dMHz\n",
	       (current_cpu_data.module_clock / 1000000),
	       (current_cpu_data.module_clock % 1000000)/10000);

	interval = (current_cpu_data.module_clock/4 + HZ/2) / HZ;

	printk("Interval = %ld\n", interval);

	/* Start TMU0 */
	ctrl_outb(0, TMU_TSTR);
#if !defined(CONFIG_CPU_SUBTYPE_SH7300)
	ctrl_outb(TMU_TOCR_INIT, TMU_TOCR);
#endif
	ctrl_outw(TMU0_TCR_INIT, TMU0_TCR);
	ctrl_outl(interval, TMU0_TCOR);
	ctrl_outl(interval, TMU0_TCNT);
	ctrl_outb(TMU_TSTR_INIT, TMU_TSTR);

#if defined(CONFIG_SH_KGDB)
	/*
	 * Set up kgdb as requested. We do it here because the serial
	 * init uses the timer vars we just set up for figuring baud.
	 */
	kgdb_init();
#endif
}
