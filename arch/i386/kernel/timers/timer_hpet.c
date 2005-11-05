/*
 * This code largely moved from arch/i386/kernel/time.c.
 * See comments there for proper credits.
 */

#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/timex.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/jiffies.h>

#include <asm/timer.h>
#include <asm/io.h>
#include <asm/processor.h>

#include "io_ports.h"
#include "mach_timer.h"
#include <asm/hpet.h>

static unsigned long hpet_usec_quotient __read_mostly;	/* convert hpet clks to usec */
static unsigned long tsc_hpet_quotient __read_mostly;	/* convert tsc to hpet clks */
static unsigned long hpet_last; 	/* hpet counter value at last tick*/
static unsigned long last_tsc_low;	/* lsb 32 bits of Time Stamp Counter */
static unsigned long last_tsc_high; 	/* msb 32 bits of Time Stamp Counter */
static unsigned long long monotonic_base;
static seqlock_t monotonic_lock = SEQLOCK_UNLOCKED;

/* convert from cycles(64bits) => nanoseconds (64bits)
 *  basic equation:
 *		ns = cycles / (freq / ns_per_sec)
 *		ns = cycles * (ns_per_sec / freq)
 *		ns = cycles * (10^9 / (cpu_khz * 10^3))
 *		ns = cycles * (10^6 / cpu_khz)
 *
 *	Then we use scaling math (suggested by george@mvista.com) to get:
 *		ns = cycles * (10^6 * SC / cpu_khz) / SC
 *		ns = cycles * cyc2ns_scale / SC
 *
 *	And since SC is a constant power of two, we can convert the div
 *  into a shift.
 *
 *  We can use khz divisor instead of mhz to keep a better percision, since
 *  cyc2ns_scale is limited to 10^6 * 2^10, which fits in 32 bits.
 *  (mathieu.desnoyers@polymtl.ca)
 *
 *			-johnstul@us.ibm.com "math is hard, lets go shopping!"
 */
static unsigned long cyc2ns_scale;
#define CYC2NS_SCALE_FACTOR 10 /* 2^10, carefully chosen */

static inline void set_cyc2ns_scale(unsigned long cpu_khz)
{
	cyc2ns_scale = (1000000 << CYC2NS_SCALE_FACTOR)/cpu_khz;
}

static inline unsigned long long cycles_2_ns(unsigned long long cyc)
{
	return (cyc * cyc2ns_scale) >> CYC2NS_SCALE_FACTOR;
}

static unsigned long long monotonic_clock_hpet(void)
{
	unsigned long long last_offset, this_offset, base;
	unsigned seq;

	/* atomically read monotonic base & last_offset */
	do {
		seq = read_seqbegin(&monotonic_lock);
		last_offset = ((unsigned long long)last_tsc_high<<32)|last_tsc_low;
		base = monotonic_base;
	} while (read_seqretry(&monotonic_lock, seq));

	/* Read the Time Stamp Counter */
	rdtscll(this_offset);

	/* return the value in ns */
	return base + cycles_2_ns(this_offset - last_offset);
}

static unsigned long get_offset_hpet(void)
{
	register unsigned long eax, edx;

	eax = hpet_readl(HPET_COUNTER);
	eax -= hpet_last;	/* hpet delta */
	eax = min(hpet_tick, eax);
	/*
         * Time offset = (hpet delta) * ( usecs per HPET clock )
	 *             = (hpet delta) * ( usecs per tick / HPET clocks per tick)
	 *             = (hpet delta) * ( hpet_usec_quotient ) / (2^32)
	 *
	 * Where,
	 * hpet_usec_quotient = (2^32 * usecs per tick)/HPET clocks per tick
	 *
	 * Using a mull instead of a divl saves some cycles in critical path.
         */
	ASM_MUL64_REG(eax, edx, hpet_usec_quotient, eax);

	/* our adjusted time offset in microseconds */
	return edx;
}

static void mark_offset_hpet(void)
{
	unsigned long long this_offset, last_offset;
	unsigned long offset;

	write_seqlock(&monotonic_lock);
	last_offset = ((unsigned long long)last_tsc_high<<32)|last_tsc_low;
	rdtsc(last_tsc_low, last_tsc_high);

	if (hpet_use_timer)
		offset = hpet_readl(HPET_T0_CMP) - hpet_tick;
	else
		offset = hpet_readl(HPET_COUNTER);
	if (unlikely(((offset - hpet_last) >= (2*hpet_tick)) && (hpet_last != 0))) {
		int lost_ticks = ((offset - hpet_last) / hpet_tick) - 1;
		jiffies_64 += lost_ticks;
	}
	hpet_last = offset;

	/* update the monotonic base value */
	this_offset = ((unsigned long long)last_tsc_high<<32)|last_tsc_low;
	monotonic_base += cycles_2_ns(this_offset - last_offset);
	write_sequnlock(&monotonic_lock);
}

static void delay_hpet(unsigned long loops)
{
	unsigned long hpet_start, hpet_end;
	unsigned long eax;

	/* loops is the number of cpu cycles. Convert it to hpet clocks */
	ASM_MUL64_REG(eax, loops, tsc_hpet_quotient, loops);

	hpet_start = hpet_readl(HPET_COUNTER);
	do {
		rep_nop();
		hpet_end = hpet_readl(HPET_COUNTER);
	} while ((hpet_end - hpet_start) < (loops));
}

static struct timer_opts timer_hpet;

static int __init init_hpet(char* override)
{
	unsigned long result, remain;

	/* check clock override */
	if (override[0] && strncmp(override,"hpet",4))
		return -ENODEV;

	if (!is_hpet_enabled())
		return -ENODEV;

	printk("Using HPET for gettimeofday\n");
	if (cpu_has_tsc) {
		unsigned long tsc_quotient = calibrate_tsc_hpet(&tsc_hpet_quotient);
		if (tsc_quotient) {
			/* report CPU clock rate in Hz.
			 * The formula is (10^6 * 2^32) / (2^32 * 1 / (clocks/us)) =
			 * clock/second. Our precision is about 100 ppm.
			 */
			{	unsigned long eax=0, edx=1000;
				ASM_DIV64_REG(cpu_khz, edx, tsc_quotient,
						eax, edx);
				printk("Detected %u.%03u MHz processor.\n",
					cpu_khz / 1000, cpu_khz % 1000);
			}
			set_cyc2ns_scale(cpu_khz);
		}
		/* set this only when cpu_has_tsc */
		timer_hpet.read_timer = read_timer_tsc;
	}

	/*
	 * Math to calculate hpet to usec multiplier
	 * Look for the comments at get_offset_hpet()
	 */
	ASM_DIV64_REG(result, remain, hpet_tick, 0, KERNEL_TICK_USEC);
	if (remain > (hpet_tick >> 1))
		result++; /* rounding the result */
	hpet_usec_quotient = result;

	return 0;
}

static int hpet_resume(void)
{
	write_seqlock(&monotonic_lock);
	/* Assume this is the last mark offset time */
	rdtsc(last_tsc_low, last_tsc_high);

	if (hpet_use_timer)
		hpet_last = hpet_readl(HPET_T0_CMP) - hpet_tick;
	else
		hpet_last = hpet_readl(HPET_COUNTER);
	write_sequnlock(&monotonic_lock);
	return 0;
}
/************************************************************/

/* tsc timer_opts struct */
static struct timer_opts timer_hpet __read_mostly = {
	.name = 		"hpet",
	.mark_offset =		mark_offset_hpet,
	.get_offset =		get_offset_hpet,
	.monotonic_clock =	monotonic_clock_hpet,
	.delay = 		delay_hpet,
	.resume	=		hpet_resume,
};

struct init_timer_opts __initdata timer_hpet_init = {
	.init =	init_hpet,
	.opts = &timer_hpet,
};
