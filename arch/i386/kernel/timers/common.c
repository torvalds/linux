/*
 *	Common functions used across the timers go here
 */

#include <linux/init.h>
#include <linux/timex.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/module.h>

#include <asm/io.h>
#include <asm/timer.h>
#include <asm/hpet.h>

#include "mach_timer.h"

/* ------ Calibrate the TSC -------
 * Return 2^32 * (1 / (TSC clocks per usec)) for do_fast_gettimeoffset().
 * Too much 64-bit arithmetic here to do this cleanly in C, and for
 * accuracy's sake we want to keep the overhead on the CTC speaker (channel 2)
 * output busy loop as low as possible. We avoid reading the CTC registers
 * directly because of the awkward 8-bit access mechanism of the 82C54
 * device.
 */

#define CALIBRATE_TIME	(5 * 1000020/HZ)

unsigned long calibrate_tsc(void)
{
	mach_prepare_counter();

	{
		unsigned long startlow, starthigh;
		unsigned long endlow, endhigh;
		unsigned long count;

		rdtsc(startlow,starthigh);
		mach_countup(&count);
		rdtsc(endlow,endhigh);


		/* Error: ECTCNEVERSET */
		if (count <= 1)
			goto bad_ctc;

		/* 64-bit subtract - gcc just messes up with long longs */
		__asm__("subl %2,%0\n\t"
			"sbbl %3,%1"
			:"=a" (endlow), "=d" (endhigh)
			:"g" (startlow), "g" (starthigh),
			 "0" (endlow), "1" (endhigh));

		/* Error: ECPUTOOFAST */
		if (endhigh)
			goto bad_ctc;

		/* Error: ECPUTOOSLOW */
		if (endlow <= CALIBRATE_TIME)
			goto bad_ctc;

		__asm__("divl %2"
			:"=a" (endlow), "=d" (endhigh)
			:"r" (endlow), "0" (0), "1" (CALIBRATE_TIME));

		return endlow;
	}

	/*
	 * The CTC wasn't reliable: we got a hit on the very first read,
	 * or the CPU was so fast/slow that the quotient wouldn't fit in
	 * 32 bits..
	 */
bad_ctc:
	return 0;
}

#ifdef CONFIG_HPET_TIMER
/* ------ Calibrate the TSC using HPET -------
 * Return 2^32 * (1 / (TSC clocks per usec)) for getting the CPU freq.
 * Second output is parameter 1 (when non NULL)
 * Set 2^32 * (1 / (tsc per HPET clk)) for delay_hpet().
 * calibrate_tsc() calibrates the processor TSC by comparing
 * it to the HPET timer of known frequency.
 * Too much 64-bit arithmetic here to do this cleanly in C
 */
#define CALIBRATE_CNT_HPET 	(5 * hpet_tick)
#define CALIBRATE_TIME_HPET 	(5 * KERNEL_TICK_USEC)

unsigned long __devinit calibrate_tsc_hpet(unsigned long *tsc_hpet_quotient_ptr)
{
	unsigned long tsc_startlow, tsc_starthigh;
	unsigned long tsc_endlow, tsc_endhigh;
	unsigned long hpet_start, hpet_end;
	unsigned long result, remain;

	hpet_start = hpet_readl(HPET_COUNTER);
	rdtsc(tsc_startlow, tsc_starthigh);
	do {
		hpet_end = hpet_readl(HPET_COUNTER);
	} while ((hpet_end - hpet_start) < CALIBRATE_CNT_HPET);
	rdtsc(tsc_endlow, tsc_endhigh);

	/* 64-bit subtract - gcc just messes up with long longs */
	__asm__("subl %2,%0\n\t"
		"sbbl %3,%1"
		:"=a" (tsc_endlow), "=d" (tsc_endhigh)
		:"g" (tsc_startlow), "g" (tsc_starthigh),
		 "0" (tsc_endlow), "1" (tsc_endhigh));

	/* Error: ECPUTOOFAST */
	if (tsc_endhigh)
		goto bad_calibration;

	/* Error: ECPUTOOSLOW */
	if (tsc_endlow <= CALIBRATE_TIME_HPET)
		goto bad_calibration;

	ASM_DIV64_REG(result, remain, tsc_endlow, 0, CALIBRATE_TIME_HPET);
	if (remain > (tsc_endlow >> 1))
		result++; /* rounding the result */

	if (tsc_hpet_quotient_ptr) {
		unsigned long tsc_hpet_quotient;

		ASM_DIV64_REG(tsc_hpet_quotient, remain, tsc_endlow, 0,
			CALIBRATE_CNT_HPET);
		if (remain > (tsc_endlow >> 1))
			tsc_hpet_quotient++; /* rounding the result */
		*tsc_hpet_quotient_ptr = tsc_hpet_quotient;
	}

	return result;
bad_calibration:
	/*
	 * the CPU was so fast/slow that the quotient wouldn't fit in
	 * 32 bits..
	 */
	return 0;
}
#endif


unsigned long read_timer_tsc(void)
{
	unsigned long retval;
	rdtscl(retval);
	return retval;
}


/* calculate cpu_khz */
void init_cpu_khz(void)
{
	if (cpu_has_tsc) {
		unsigned long tsc_quotient = calibrate_tsc();
		if (tsc_quotient) {
			/* report CPU clock rate in Hz.
			 * The formula is (10^6 * 2^32) / (2^32 * 1 / (clocks/us)) =
			 * clock/second. Our precision is about 100 ppm.
			 */
			{	unsigned long eax=0, edx=1000;
				__asm__("divl %2"
		       		:"=a" (cpu_khz), "=d" (edx)
        	       		:"r" (tsc_quotient),
	                	"0" (eax), "1" (edx));
				printk("Detected %u.%03u MHz processor.\n",
					cpu_khz / 1000, cpu_khz % 1000);
			}
		}
	}
}

