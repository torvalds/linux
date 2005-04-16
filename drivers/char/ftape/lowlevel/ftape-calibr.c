/*
 *      Copyright (C) 1993-1996 Bas Laarhoven.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape-calibr.c,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:18:08 $
 *
 *      GP calibration routine for processor speed dependent
 *      functions.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <asm/system.h>
#include <asm/io.h>
#if defined(__alpha__)
# include <asm/hwrpb.h>
#elif defined(__x86_64__)
# include <asm/msr.h>
# include <asm/timex.h>
#elif defined(__i386__)
# include <linux/timex.h>
#endif
#include <linux/ftape.h>
#include "../lowlevel/ftape-tracing.h"
#include "../lowlevel/ftape-calibr.h"
#include "../lowlevel/fdc-io.h"

#undef DEBUG

#if !defined(__alpha__) && !defined(__i386__) && !defined(__x86_64__)
# error Ftape is not implemented for this architecture!
#endif

#if defined(__alpha__) || defined(__x86_64__)
static unsigned long ps_per_cycle = 0;
#endif

static spinlock_t calibr_lock;

/*
 * Note: On Intel PCs, the clock ticks at 100 Hz (HZ==100) which is
 * too slow for certain timeouts (and that clock doesn't even tick
 * when interrupts are disabled).  For that reason, the 8254 timer is
 * used directly to implement fine-grained timeouts.  However, on
 * Alpha PCs, the 8254 is *not* used to implement the clock tick
 * (which is 1024 Hz, normally) and the 8254 timer runs at some
 * "random" frequency (it seems to run at 18Hz, but it's not safe to
 * rely on this value).  Instead, we use the Alpha's "rpcc"
 * instruction to read cycle counts.  As this is a 32 bit counter,
 * it will overflow only once per 30 seconds (on a 200MHz machine),
 * which is plenty.
 */

unsigned int ftape_timestamp(void)
{
#if defined(__alpha__)
	unsigned long r;

	asm volatile ("rpcc %0" : "=r" (r));
	return r;
#elif defined(__x86_64__)
	unsigned long r;
	rdtscl(r);
	return r;
#elif defined(__i386__)

/*
 * Note that there is some time between counter underflowing and jiffies
 * increasing, so the code below won't always give correct output.
 * -Vojtech
 */

	unsigned long flags;
	__u16 lo;
	__u16 hi;

	spin_lock_irqsave(&calibr_lock, flags);
	outb_p(0x00, 0x43);	/* latch the count ASAP */
	lo = inb_p(0x40);	/* read the latched count */
	lo |= inb(0x40) << 8;
	hi = jiffies;
	spin_unlock_irqrestore(&calibr_lock, flags);
	return ((hi + 1) * (unsigned int) LATCH) - lo;  /* downcounter ! */
#endif
}

static unsigned int short_ftape_timestamp(void)
{
#if defined(__alpha__) || defined(__x86_64__)
	return ftape_timestamp();
#elif defined(__i386__)
	unsigned int count;
 	unsigned long flags;
 
	spin_lock_irqsave(&calibr_lock, flags);
 	outb_p(0x00, 0x43);	/* latch the count ASAP */
	count = inb_p(0x40);	/* read the latched count */
	count |= inb(0x40) << 8;
	spin_unlock_irqrestore(&calibr_lock, flags);
	return (LATCH - count);	/* normal: downcounter */
#endif
}

static unsigned int diff(unsigned int t0, unsigned int t1)
{
#if defined(__alpha__) || defined(__x86_64__)
	return (t1 - t0);
#elif defined(__i386__)
	/*
	 * This is tricky: to work for both short and full ftape_timestamps
	 * we'll have to discriminate between these.
	 * If it _looks_ like short stamps with wrapping around we'll
	 * asume it are. This will generate a small error if it really
	 * was a (very large) delta from full ftape_timestamps.
	 */
	return (t1 <= t0 && t0 <= LATCH) ? t1 + LATCH - t0 : t1 - t0;
#endif
}

static unsigned int usecs(unsigned int count)
{
#if defined(__alpha__) || defined(__x86_64__)
	return (ps_per_cycle * count) / 1000000UL;
#elif defined(__i386__)
	return (10000 * count) / ((CLOCK_TICK_RATE + 50) / 100);
#endif
}

unsigned int ftape_timediff(unsigned int t0, unsigned int t1)
{
	/*
	 *  Calculate difference in usec for ftape_timestamp results t0 & t1.
	 *  Note that on the i386 platform with short time-stamps, the
	 *  maximum allowed timespan is 1/HZ or we'll lose ticks!
	 */
	return usecs(diff(t0, t1));
}

/*      To get an indication of the I/O performance,
 *      measure the duration of the inb() function.
 */
static void time_inb(void)
{
	int i;
	int t0, t1;
	unsigned long flags;
	int status;
	TRACE_FUN(ft_t_any);

	spin_lock_irqsave(&calibr_lock, flags);
	t0 = short_ftape_timestamp();
	for (i = 0; i < 1000; ++i) {
		status = inb(fdc.msr);
	}
	t1 = short_ftape_timestamp();
	spin_unlock_irqrestore(&calibr_lock, flags);
	TRACE(ft_t_info, "inb() duration: %d nsec", ftape_timediff(t0, t1));
	TRACE_EXIT;
}

static void init_clock(void)
{
	TRACE_FUN(ft_t_any);

#if defined(__x86_64__)
	ps_per_cycle = 1000000000UL / cpu_khz;
#elif defined(__alpha__)
	extern struct hwrpb_struct *hwrpb;
	ps_per_cycle = (1000*1000*1000*1000UL) / hwrpb->cycle_freq;
#endif
	TRACE_EXIT;
}

/*
 *      Input:  function taking int count as parameter.
 *              pointers to calculated calibration variables.
 */
void ftape_calibrate(char *name,
		    void (*fun) (unsigned int), 
		    unsigned int *calibr_count, 
		    unsigned int *calibr_time)
{
	static int first_time = 1;
	int i;
	unsigned int tc = 0;
	unsigned int count;
	unsigned int time;
#if defined(__i386__)
	unsigned int old_tc = 0;
	unsigned int old_count = 1;
	unsigned int old_time = 1;
#endif
	TRACE_FUN(ft_t_flow);

	if (first_time) {             /* get idea of I/O performance */
		init_clock();
		time_inb();
		first_time = 0;
	}
	/*    value of timeout must be set so that on very slow systems
	 *    it will give a time less than one jiffy, and on
	 *    very fast systems it'll give reasonable precision.
	 */

	count = 40;
	for (i = 0; i < 15; ++i) {
		unsigned int t0;
		unsigned int t1;
		unsigned int once;
		unsigned int multiple;
		unsigned long flags;

		*calibr_count =
		*calibr_time = count;	/* set TC to 1 */
		spin_lock_irqsave(&calibr_lock, flags);
		fun(0);		/* dummy, get code into cache */
		t0 = short_ftape_timestamp();
		fun(0);		/* overhead + one test */
		t1 = short_ftape_timestamp();
		once = diff(t0, t1);
		t0 = short_ftape_timestamp();
		fun(count);		/* overhead + count tests */
		t1 = short_ftape_timestamp();
		multiple = diff(t0, t1);
		spin_unlock_irqrestore(&calibr_lock, flags);
		time = ftape_timediff(0, multiple - once);
		tc = (1000 * time) / (count - 1);
		TRACE(ft_t_any, "once:%3d us,%6d times:%6d us, TC:%5d ns",
			usecs(once), count - 1, usecs(multiple), tc);
#if defined(__alpha__) || defined(__x86_64__)
		/*
		 * Increase the calibration count exponentially until the
		 * calibration time exceeds 100 ms.
		 */
		if (time >= 100*1000) {
			break;
		}
#elif defined(__i386__)
		/*
		 * increase the count until the resulting time nears 2/HZ,
		 * then the tc will drop sharply because we lose LATCH counts.
		 */
		if (tc <= old_tc / 2) {
			time = old_time;
			count = old_count;
			break;
		}
		old_tc = tc;
		old_count = count;
		old_time = time;
#endif
		count *= 2;
	}
	*calibr_count = count - 1;
	*calibr_time  = time;
	TRACE(ft_t_info, "TC for `%s()' = %d nsec (at %d counts)",
	     name, (1000 * *calibr_time) / *calibr_count, *calibr_count);
	TRACE_EXIT;
}
