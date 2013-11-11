/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 by Ralf Baechle
 * Copyright (C) 2009, 2012 Cavium, Inc.
 */
#include <linux/clocksource.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/smp.h>

#include <asm/cpu-info.h>
#include <asm/time.h>

#include <asm/octeon/octeon.h>
#include <asm/octeon/cvmx-ipd-defs.h>
#include <asm/octeon/cvmx-mio-defs.h>


static u64 f;
static u64 rdiv;
static u64 sdiv;
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

	if (current_cpu_type() == CPU_CAVIUM_OCTEON2) {
		union cvmx_mio_rst_boot rst_boot;
		rst_boot.u64 = cvmx_read_csr(CVMX_MIO_RST_BOOT);
		rdiv = rst_boot.s.c_mul;	/* CPU clock */
		sdiv = rst_boot.s.pnr_mul;	/* I/O clock */
		f = (0x8000000000000000ull / sdiv) * 2;
	}
}

/*
 * Set the current core's cvmcount counter to the value of the
 * IPD_CLK_COUNT.  We do this on all cores as they are brought
 * on-line.  This allows for a read from a local cpu register to
 * access a synchronized counter.
 *
 * On CPU_CAVIUM_OCTEON2 the IPD_CLK_COUNT is scaled by rdiv/sdiv.
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
	while (loops--) {
		u64 ipd_clk_count = cvmx_read_csr(CVMX_IPD_CLK_COUNT);
		if (rdiv != 0) {
			ipd_clk_count *= rdiv;
			if (f != 0) {
				asm("dmultu\t%[cnt],%[f]\n\t"
				    "mfhi\t%[cnt]"
				    : [cnt] "+r" (ipd_clk_count)
				    : [f] "r" (f)
				    : "hi", "lo");
			}
		}
		write_c0_cvmcount(ipd_clk_count);
	}
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
	clocksource_register_hz(&clocksource_mips, octeon_get_clock_rate());
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


/**
 * octeon_io_clk_delay - wait for a given number of io clock cycles to pass.
 *
 * We scale the wait by the clock ratio, and then wait for the
 * corresponding number of core clocks.
 *
 * @count: The number of clocks to wait.
 */
void octeon_io_clk_delay(unsigned long count)
{
	u64 cur, end;

	cur = read_c0_cvmcount();
	if (rdiv != 0) {
		end = count * rdiv;
		if (f != 0) {
			asm("dmultu\t%[cnt],%[f]\n\t"
				"mfhi\t%[cnt]"
				: [cnt] "+r" (end)
				: [f] "r" (f)
				: "hi", "lo");
		}
		end = cur + end;
	} else {
		end = cur + count;
	}
	while (end > cur)
		cur = read_c0_cvmcount();
}
EXPORT_SYMBOL(octeon_io_clk_delay);
