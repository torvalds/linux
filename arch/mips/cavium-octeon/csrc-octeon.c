/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 by Ralf Baechle
 */
#include <linux/clocksource.h>
#include <linux/init.h>

#include <asm/time.h>

#include <asm/octeon/octeon.h>
#include <asm/octeon/cvmx-ipd-defs.h>

/*
 * Set the current core's cvmcount counter to the value of the
 * IPD_CLK_COUNT.  We do this on all cores as they are brought
 * on-line.  This allows for a read from a local cpu register to
 * access a synchronized counter.
 *
 */
void octeon_init_cvmcount(void)
{
	unsigned long flags;
	unsigned loops = 2;

	/* Clobber loops so GCC will not unroll the following while loop. */
	asm("" : "+r" (loops));

	local_irq_save(flags);
	/*
	 * Loop several times so we are executing from the cache,
	 * which should give more deterministic timing.
	 */
	while (loops--)
		write_c0_cvmcount(cvmx_read_csr(CVMX_IPD_CLK_COUNT));
	local_irq_restore(flags);
}

static cycle_t octeon_cvmcount_read(struct clocksource *cs)
{
	return read_c0_cvmcount();
}

static struct clocksource clocksource_mips = {
	.name		= "OCTEON_CVMCOUNT",
	.read		= octeon_cvmcount_read,
	.mask		= CLOCKSOURCE_MASK(64),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

unsigned long long notrace sched_clock(void)
{
	/* 64-bit arithmatic can overflow, so use 128-bit.  */
	u64 t1, t2, t3;
	unsigned long long rv;
	u64 mult = clocksource_mips.mult;
	u64 shift = clocksource_mips.shift;
	u64 cnt = read_c0_cvmcount();

	asm (
		"dmultu\t%[cnt],%[mult]\n\t"
		"nor\t%[t1],$0,%[shift]\n\t"
		"mfhi\t%[t2]\n\t"
		"mflo\t%[t3]\n\t"
		"dsll\t%[t2],%[t2],1\n\t"
		"dsrlv\t%[rv],%[t3],%[shift]\n\t"
		"dsllv\t%[t1],%[t2],%[t1]\n\t"
		"or\t%[rv],%[t1],%[rv]\n\t"
		: [rv] "=&r" (rv), [t1] "=&r" (t1), [t2] "=&r" (t2), [t3] "=&r" (t3)
		: [cnt] "r" (cnt), [mult] "r" (mult), [shift] "r" (shift)
		: "hi", "lo");
	return rv;
}

void __init plat_time_init(void)
{
	clocksource_mips.rating = 300;
	clocksource_set_clock(&clocksource_mips, mips_hpt_frequency);
	clocksource_register(&clocksource_mips);
}

static u64 octeon_udelay_factor;
static u64 octeon_ndelay_factor;

void __init octeon_setup_delays(void)
{
	octeon_udelay_factor = octeon_get_clock_rate() / 1000000;
	/*
	 * For __ndelay we divide by 2^16, so the factor is multiplied
	 * by the same amount.
	 */
	octeon_ndelay_factor = (octeon_udelay_factor * 0x10000ull) / 1000ull;

	preset_lpj = octeon_get_clock_rate() / HZ;
}

void __udelay(unsigned long us)
{
	u64 cur, end, inc;

	cur = read_c0_cvmcount();

	inc = us * octeon_udelay_factor;
	end = cur + inc;

	while (end > cur)
		cur = read_c0_cvmcount();
}
EXPORT_SYMBOL(__udelay);

void __ndelay(unsigned long ns)
{
	u64 cur, end, inc;

	cur = read_c0_cvmcount();

	inc = ((ns * octeon_ndelay_factor) >> 16);
	end = cur + inc;

	while (end > cur)
		cur = read_c0_cvmcount();
}
EXPORT_SYMBOL(__ndelay);

void __delay(unsigned long loops)
{
	u64 cur, end;

	cur = read_c0_cvmcount();
	end = cur + loops;

	while (end > cur)
		cur = read_c0_cvmcount();
}
EXPORT_SYMBOL(__delay);
