/*
 *  linux/arch/m32r/kernel/time.c
 *
 *  Copyright (c) 2001, 2002  Hiroyuki Kondo, Hirokazu Takata,
 *                            Hitoshi Yamamoto
 *  Taken from i386 version.
 *    Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *    Copyright (C) 1996, 1997, 1998  Ralf Baechle
 *
 *  This file contains the time handling details for PC-style clocks as
 *  found in some MIPS systems.
 *
 *  Some code taken from sh version.
 *    Copyright (C) 1999  Tetsuya Okada & Niibe Yutaka
 *    Copyright (C) 2000  Philipp Rumpf <prumpf@tux.org>
 */

#undef  DEBUG_TIMER

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/profile.h>

#include <asm/io.h>
#include <asm/m32r.h>

#include <asm/hw_irq.h>

#ifdef CONFIG_SMP
extern void send_IPI_allbutself(int, int);
extern void smp_local_timer_interrupt(void);
#endif

#define TICK_SIZE	(tick_nsec / 1000)

/*
 * Change this if you have some constant time drift
 */

/* This is for machines which generate the exact clock. */
#define USECS_PER_JIFFY (1000000/HZ)

static unsigned long latch;

static unsigned long do_gettimeoffset(void)
{
	unsigned long  elapsed_time = 0;  /* [us] */

#if defined(CONFIG_CHIP_M32102) || defined(CONFIG_CHIP_XNUX2) \
	|| defined(CONFIG_CHIP_VDEC2) || defined(CONFIG_CHIP_M32700) \
	|| defined(CONFIG_CHIP_OPSP) || defined(CONFIG_CHIP_M32104)
#ifndef CONFIG_SMP

	unsigned long count;

	/* timer count may underflow right here */
	count = inl(M32R_MFT2CUT_PORTL);

	if (inl(M32R_ICU_CR18_PORTL) & 0x00000100)	/* underflow check */
		count = 0;

	count = (latch - count) * TICK_SIZE;
	elapsed_time = (count + latch / 2) / latch;
	/* NOTE: LATCH is equal to the "interval" value (= reload count). */

#else /* CONFIG_SMP */
	unsigned long count;
	static unsigned long p_jiffies = -1;
	static unsigned long p_count = 0;

	/* timer count may underflow right here */
	count = inl(M32R_MFT2CUT_PORTL);

	if (jiffies == p_jiffies && count > p_count)
		count = 0;

	p_jiffies = jiffies;
	p_count = count;

	count = (latch - count) * TICK_SIZE;
	elapsed_time = (count + latch / 2) / latch;
	/* NOTE: LATCH is equal to the "interval" value (= reload count). */
#endif /* CONFIG_SMP */
#elif defined(CONFIG_CHIP_M32310)
#warning do_gettimeoffse not implemented
#else
#error no chip configuration
#endif

	return elapsed_time;
}

/*
 * This version of gettimeofday has near microsecond resolution.
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long seq;
	unsigned long usec, sec;
	unsigned long max_ntp_tick = tick_usec - tickadj;

	do {
		seq = read_seqbegin(&xtime_lock);

		usec = do_gettimeoffset();

		/*
		 * If time_adjust is negative then NTP is slowing the clock
		 * so make sure not to go into next possible interval.
		 * Better to lose some accuracy than have time go backwards..
		 */
		if (unlikely(time_adjust < 0))
			usec = min(usec, max_ntp_tick);

		sec = xtime.tv_sec;
		usec += (xtime.tv_nsec / 1000);
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
	nsec -= do_gettimeoffset() * NSEC_PER_USEC;

	wtm_sec = wall_to_monotonic.tv_sec + (xtime.tv_sec - sec);
	wtm_nsec = wall_to_monotonic.tv_nsec + (xtime.tv_nsec - nsec);

	set_normalized_timespec(&xtime, sec, nsec);
	set_normalized_timespec(&wall_to_monotonic, wtm_sec, wtm_nsec);

	ntp_clear();
	write_sequnlock_irq(&xtime_lock);
	clock_was_set();

	return 0;
}

EXPORT_SYMBOL(do_settimeofday);

/*
 * In order to set the CMOS clock precisely, set_rtc_mmss has to be
 * called 500 ms after the second nowtime has started, because when
 * nowtime is written into the registers of the CMOS clock, it will
 * jump to the next second precisely 500 ms later. Check the Motorola
 * MC146818A or Dallas DS12887 data sheet for details.
 *
 * BUG: This routine does not handle hour overflow properly; it just
 *      sets the minutes. Usually you won't notice until after reboot!
 */
static inline int set_rtc_mmss(unsigned long nowtime)
{
	return 0;
}

/* last time the cmos clock got updated */
static long last_rtc_update = 0;

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
irqreturn_t timer_interrupt(int irq, void *dev_id)
{
#ifndef CONFIG_SMP
	profile_tick(CPU_PROFILING);
#endif
	do_timer(1);

#ifndef CONFIG_SMP
	update_process_times(user_mode(get_irq_regs()));
#endif
	/*
	 * If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	write_seqlock(&xtime_lock);
	if (ntp_synced()
		&& xtime.tv_sec > last_rtc_update + 660
		&& (xtime.tv_nsec / 1000) >= 500000 - ((unsigned)TICK_SIZE) / 2
		&& (xtime.tv_nsec / 1000) <= 500000 + ((unsigned)TICK_SIZE) / 2)
	{
		if (set_rtc_mmss(xtime.tv_sec) == 0)
			last_rtc_update = xtime.tv_sec;
		else	/* do it again in 60 s */
			last_rtc_update = xtime.tv_sec - 600;
	}
	write_sequnlock(&xtime_lock);
	/* As we return to user mode fire off the other CPU schedulers..
	   this is basically because we don't yet share IRQ's around.
	   This message is rigged to be safe on the 386 - basically it's
	   a hack, so don't look closely for now.. */

#ifdef CONFIG_SMP
	smp_local_timer_interrupt();
	smp_send_timer();
#endif

	return IRQ_HANDLED;
}

struct irqaction irq0 = { timer_interrupt, IRQF_DISABLED, CPU_MASK_NONE,
			  "MFT2", NULL, NULL };

void __init time_init(void)
{
	unsigned int epoch, year, mon, day, hour, min, sec;

	sec = min = hour = day = mon = year = 0;
	epoch = 0;

	year = 23;
	mon = 4;
	day = 17;

	/* Attempt to guess the epoch.  This is the same heuristic as in rtc.c
	   so no stupid things will happen to timekeeping.  Who knows, maybe
	   Ultrix also uses 1952 as epoch ...  */
	if (year > 10 && year < 44)
		epoch = 1980;
	else if (year < 96)
		epoch = 1952;
	year += epoch;

	xtime.tv_sec = mktime(year, mon, day, hour, min, sec);
	xtime.tv_nsec = (INITIAL_JIFFIES % HZ) * (NSEC_PER_SEC / HZ);
	set_normalized_timespec(&wall_to_monotonic,
		-xtime.tv_sec, -xtime.tv_nsec);

#if defined(CONFIG_CHIP_M32102) || defined(CONFIG_CHIP_XNUX2) \
	|| defined(CONFIG_CHIP_VDEC2) || defined(CONFIG_CHIP_M32700) \
	|| defined(CONFIG_CHIP_OPSP) || defined(CONFIG_CHIP_M32104)

	/* M32102 MFT setup */
	setup_irq(M32R_IRQ_MFT2, &irq0);
	{
		unsigned long bus_clock;
		unsigned short divide;

		bus_clock = boot_cpu_data.bus_clock;
		divide = boot_cpu_data.timer_divide;
		latch = (bus_clock/divide + HZ / 2) / HZ;

		printk("Timer start : latch = %ld\n", latch);

		outl((M32R_MFTMOD_CC_MASK | M32R_MFTMOD_TCCR \
			|M32R_MFTMOD_CSSEL011), M32R_MFT2MOD_PORTL);
		outl(latch, M32R_MFT2RLD_PORTL);
		outl(latch, M32R_MFT2CUT_PORTL);
		outl(0, M32R_MFT2CMPRLD_PORTL);
		outl((M32R_MFTCR_MFT2MSK|M32R_MFTCR_MFT2EN), M32R_MFTCR_PORTL);
	}

#elif defined(CONFIG_CHIP_M32310)
#warning time_init not implemented
#else
#error no chip configuration
#endif
}
