/*
 * This code largely moved from arch/i386/kernel/time.c.
 * See comments there for proper credits.
 */

#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/sysdev.h>
#include <linux/timex.h>
#include <asm/delay.h>
#include <asm/mpspec.h>
#include <asm/timer.h>
#include <asm/smp.h>
#include <asm/io.h>
#include <asm/arch_hooks.h>
#include <asm/i8253.h>

#include "do_timer.h"
#include "io_ports.h"

static int count_p; /* counter in get_offset_pit() */

static int __init init_pit(char* override)
{
 	/* check clock override */
 	if (override[0] && strncmp(override,"pit",3))
 		printk(KERN_ERR "Warning: clock= override failed. Defaulting "
				"to PIT\n");
 	init_cpu_khz();
	count_p = LATCH;
	return 0;
}

static void mark_offset_pit(void)
{
	/* nothing needed */
}

static unsigned long long monotonic_clock_pit(void)
{
	return 0;
}

static void delay_pit(unsigned long loops)
{
	int d0;
	__asm__ __volatile__(
		"\tjmp 1f\n"
		".align 16\n"
		"1:\tjmp 2f\n"
		".align 16\n"
		"2:\tdecl %0\n\tjns 2b"
		:"=&a" (d0)
		:"0" (loops));
}


/* This function must be called with xtime_lock held.
 * It was inspired by Steve McCanne's microtime-i386 for BSD.  -- jrs
 * 
 * However, the pc-audio speaker driver changes the divisor so that
 * it gets interrupted rather more often - it loads 64 into the
 * counter rather than 11932! This has an adverse impact on
 * do_gettimeoffset() -- it stops working! What is also not
 * good is that the interval that our timer function gets called
 * is no longer 10.0002 ms, but 9.9767 ms. To get around this
 * would require using a different timing source. Maybe someone
 * could use the RTC - I know that this can interrupt at frequencies
 * ranging from 8192Hz to 2Hz. If I had the energy, I'd somehow fix
 * it so that at startup, the timer code in sched.c would select
 * using either the RTC or the 8253 timer. The decision would be
 * based on whether there was any other device around that needed
 * to trample on the 8253. I'd set up the RTC to interrupt at 1024 Hz,
 * and then do some jiggery to have a version of do_timer that 
 * advanced the clock by 1/1024 s. Every time that reached over 1/100
 * of a second, then do all the old code. If the time was kept correct
 * then do_gettimeoffset could just return 0 - there is no low order
 * divider that can be accessed.
 *
 * Ideally, you would be able to use the RTC for the speaker driver,
 * but it appears that the speaker driver really needs interrupt more
 * often than every 120 us or so.
 *
 * Anyway, this needs more thought....		pjsg (1993-08-28)
 * 
 * If you are really that interested, you should be reading
 * comp.protocols.time.ntp!
 */

static unsigned long get_offset_pit(void)
{
	int count;
	unsigned long flags;
	static unsigned long jiffies_p = 0;

	/*
	 * cache volatile jiffies temporarily; we have xtime_lock. 
	 */
	unsigned long jiffies_t;

	spin_lock_irqsave(&i8253_lock, flags);
	/* timer count may underflow right here */
	outb_p(0x00, PIT_MODE);	/* latch the count ASAP */

	count = inb_p(PIT_CH0);	/* read the latched count */

	/*
	 * We do this guaranteed double memory access instead of a _p 
	 * postfix in the previous port access. Wheee, hackady hack
	 */
 	jiffies_t = jiffies;

	count |= inb_p(PIT_CH0) << 8;
	
        /* VIA686a test code... reset the latch if count > max + 1 */
        if (count > LATCH) {
                outb_p(0x34, PIT_MODE);
                outb_p(LATCH & 0xff, PIT_CH0);
                outb(LATCH >> 8, PIT_CH0);
                count = LATCH - 1;
        }
	
	/*
	 * avoiding timer inconsistencies (they are rare, but they happen)...
	 * there are two kinds of problems that must be avoided here:
	 *  1. the timer counter underflows
	 *  2. hardware problem with the timer, not giving us continuous time,
	 *     the counter does small "jumps" upwards on some Pentium systems,
	 *     (see c't 95/10 page 335 for Neptun bug.)
	 */

	if( jiffies_t == jiffies_p ) {
		if( count > count_p ) {
			/* the nutcase */
			count = do_timer_overflow(count);
		}
	} else
		jiffies_p = jiffies_t;

	count_p = count;

	spin_unlock_irqrestore(&i8253_lock, flags);

	count = ((LATCH-1) - count) * TICK_SIZE;
	count = (count + LATCH/2) / LATCH;

	return count;
}


/* tsc timer_opts struct */
struct timer_opts timer_pit = {
	.name = "pit",
	.mark_offset = mark_offset_pit, 
	.get_offset = get_offset_pit,
	.monotonic_clock = monotonic_clock_pit,
	.delay = delay_pit,
};

struct init_timer_opts __initdata timer_pit_init = {
	.init = init_pit, 
	.opts = &timer_pit,
};

void setup_pit_timer(void)
{
	unsigned long flags;

	spin_lock_irqsave(&i8253_lock, flags);
	outb_p(0x34,PIT_MODE);		/* binary, mode 2, LSB/MSB, ch 0 */
	udelay(10);
	outb_p(LATCH & 0xff , PIT_CH0);	/* LSB */
	udelay(10);
	outb(LATCH >> 8 , PIT_CH0);	/* MSB */
	spin_unlock_irqrestore(&i8253_lock, flags);
}
