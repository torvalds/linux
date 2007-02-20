/*
 *  linux/arch/arm26/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *  Modifications for ARM (C) 1994-2001 Russell King
 *  Mods for ARM26 (C) 2003 Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This file contains the ARM-specific time handling details:
 *  reading the RTC at bootup, etc...
 *
 *  1994-07-02  Alan Modra
 *              fixed set_rtc_mmss, fixed time.year for >= 2000, new mktime
 *  1998-12-20  Updated NTP code according to technical memorandum Jan '96
 *              "A Kernel Model for Precision Timekeeping" by Dave Mills
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/timex.h>
#include <linux/errno.h>
#include <linux/profile.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/ioc.h>

/* this needs a better home */
DEFINE_SPINLOCK(rtc_lock);

/* change this if you have some constant time drift */
#define USECS_PER_JIFFY	(1000000/HZ)

static int dummy_set_rtc(void)
{
	return 0;
}

/*
 * hook for setting the RTC's idea of the current time.
 */
int (*set_rtc)(void) = dummy_set_rtc;

/*
 * Get time offset based on IOCs timer.
 * FIXME - if this is called with interrutps off, why the shennanigans
 * below ?
 */
static unsigned long gettimeoffset(void)
{
        unsigned int count1, count2, status;
        long offset;

        ioc_writeb (0, IOC_T0LATCH);
        barrier ();
        count1 = ioc_readb(IOC_T0CNTL) | (ioc_readb(IOC_T0CNTH) << 8);
        barrier ();
        status = ioc_readb(IOC_IRQREQA);
        barrier ();
        ioc_writeb (0, IOC_T0LATCH);
        barrier ();
        count2 = ioc_readb(IOC_T0CNTL) | (ioc_readb(IOC_T0CNTH) << 8);

        offset = count2;
        if (count2 < count1) {
                /*
                 * We have not had an interrupt between reading count1
                 * and count2.
                 */
                if (status & (1 << 5))
                        offset -= LATCH;
        } else if (count2 > count1) {
                /*
                 * We have just had another interrupt between reading
                 * count1 and count2.
                 */
                offset -= LATCH;
        }

        offset = (LATCH - offset) * (tick_nsec / 1000);
        return (offset + LATCH/2) / LATCH;
}

static unsigned long next_rtc_update;

/*
 * If we have an externally synchronized linux clock, then update
 * CMOS clock accordingly every ~11 minutes.  set_rtc() has to be
 * called as close as possible to 500 ms before the new second
 * starts.
 */
static inline void do_set_rtc(void)
{
	if (!ntp_synced() || set_rtc == NULL)
		return;

//FIXME - timespec.tv_sec is a time_t not unsigned long
	if (next_rtc_update &&
	    time_before((unsigned long)xtime.tv_sec, next_rtc_update))
		return;

	if (xtime.tv_nsec < 500000000 - ((unsigned) tick_nsec >> 1) &&
	    xtime.tv_nsec >= 500000000 + ((unsigned) tick_nsec >> 1))
		return;

	if (set_rtc())
		/*
		 * rtc update failed.  Try again in 60s
		 */
		next_rtc_update = xtime.tv_sec + 60;
	else
		next_rtc_update = xtime.tv_sec + 660;
}

#define do_leds()

void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;
	unsigned long seq;
	unsigned long usec, sec;

	do {
		seq = read_seqbegin_irqsave(&xtime_lock, flags);
		usec = gettimeoffset();
		sec = xtime.tv_sec;
		usec += xtime.tv_nsec / 1000;
	} while (read_seqretry_irqrestore(&xtime_lock, seq, flags));

	/* usec may have gone up a lot: be safe */
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
	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	write_seqlock_irq(&xtime_lock);
	/*
	 * This is revolting. We need to set "xtime" correctly. However, the
	 * value in this location is the value at the most recent update of
	 * wall time.  Discover what correction gettimeofday() would have
	 * done, and then undo it!
	 */
	tv->tv_nsec -= 1000 * gettimeoffset();

	while (tv->tv_nsec < 0) {
		tv->tv_nsec += NSEC_PER_SEC;
		tv->tv_sec--;
	}

	xtime.tv_sec = tv->tv_sec;
	xtime.tv_nsec = tv->tv_nsec;
	ntp_clear();
	write_sequnlock_irq(&xtime_lock);
	clock_was_set();
	return 0;
}

EXPORT_SYMBOL(do_settimeofday);

static irqreturn_t timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
        do_timer(1);
#ifndef CONFIG_SMP
	update_process_times(user_mode(regs));
#endif
        do_set_rtc(); //FIME - EVERY timer IRQ?
        profile_tick(CPU_PROFILING, regs);
	return IRQ_HANDLED; //FIXME - is this right?
}

static struct irqaction timer_irq = {
	.name	= "timer",
	.flags	= IRQF_DISABLED,
	.handler = timer_interrupt,
};

extern void ioctime_init(void);

/*
 * Set up timer interrupt.
 */
void __init time_init(void)
{
	ioc_writeb(LATCH & 255, IOC_T0LTCHL);
        ioc_writeb(LATCH >> 8, IOC_T0LTCHH);
        ioc_writeb(0, IOC_T0GO);


        setup_irq(IRQ_TIMER, &timer_irq);
}

