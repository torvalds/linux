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

#if defined(CONFIG_RTC_DRV_CMOS) || defined(CONFIG_RTC_DRV_CMOS_MODULE)
/* this needs a better home */
DEFINE_SPINLOCK(rtc_lock);

#ifdef CONFIG_RTC_DRV_CMOS_MODULE
EXPORT_SYMBOL(rtc_lock);
#endif
#endif  /* pc-style 'CMOS' RTC support */

#ifdef CONFIG_SMP
extern void smp_local_timer_interrupt(void);
#endif

#define TICK_SIZE	(tick_nsec / 1000)

/*
 * Change this if you have some constant time drift
 */

/* This is for machines which generate the exact clock. */
#define USECS_PER_JIFFY (1000000/HZ)

static unsigned long latch;

static u32 m32r_gettimeoffset(void)
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
	elapsed_time = DIV_ROUND_CLOSEST(count, latch);
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
	elapsed_time = DIV_ROUND_CLOSEST(count, latch);
	/* NOTE: LATCH is equal to the "interval" value (= reload count). */
#endif /* CONFIG_SMP */
#elif defined(CONFIG_CHIP_M32310)
#warning do_gettimeoffse not implemented
#else
#error no chip configuration
#endif

	return elapsed_time * 1000;
}

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "xtime_update()" routine every clocktick
 */
static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
#ifndef CONFIG_SMP
	profile_tick(CPU_PROFILING);
#endif
	xtime_update(1);

#ifndef CONFIG_SMP
	update_process_times(user_mode(get_irq_regs()));
#endif
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

static struct irqaction irq0 = {
	.handler = timer_interrupt,
	.name = "MFT2",
};

void read_persistent_clock(struct timespec *ts)
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

	ts->tv_sec = mktime(year, mon, day, hour, min, sec);
	ts->tv_nsec = (INITIAL_JIFFIES % HZ) * (NSEC_PER_SEC / HZ);
}


void __init time_init(void)
{
	arch_gettimeoffset = m32r_gettimeoffset;

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
		latch = DIV_ROUND_CLOSEST(bus_clock/divide, HZ);

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
