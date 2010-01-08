/*
 * Copyright (C) 2008 Scientific-Atlanta, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
/*
 * The file comes from kernel/csrc-r4k.c
 */
#include <linux/clocksource.h>
#include <linux/init.h>

#include <asm/time.h>			/* Not included in linux/time.h */

#include <asm/mach-powertv/asic_regs.h>
#include "powertv-clock.h"

/* MIPS PLL Register Definitions */
#define PLL_GET_M(x)		(((x) >> 8) & 0x000000FF)
#define PLL_GET_N(x)		(((x) >> 16) & 0x000000FF)
#define PLL_GET_P(x)		(((x) >> 24) & 0x00000007)

/*
 * returns:  Clock frequency in kHz
 */
unsigned int __init mips_get_pll_freq(void)
{
	unsigned int pll_reg, m, n, p;
	unsigned int fin = 54000; /* Base frequency in kHz */
	unsigned int fout;

	/* Read PLL register setting */
	pll_reg = asic_read(mips_pll_setup);
	m = PLL_GET_M(pll_reg);
	n = PLL_GET_N(pll_reg);
	p = PLL_GET_P(pll_reg);
	pr_info("MIPS PLL Register:0x%x  M=%d  N=%d  P=%d\n", pll_reg, m, n, p);

	/* Calculate clock frequency = (2 * N * 54MHz) / (M * (2**P)) */
	fout = ((2 * n * fin) / (m * (0x01 << p)));

	pr_info("MIPS Clock Freq=%d kHz\n", fout);

	return fout;
}

static cycle_t c0_hpt_read(struct clocksource *cs)
{
	return read_c0_count();
}

static struct clocksource clocksource_mips = {
	.name		= "powertv-counter",
	.read		= c0_hpt_read,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init powertv_c0_hpt_clocksource_init(void)
{
	unsigned int pll_freq = mips_get_pll_freq();

	pr_info("CPU frequency %d.%02d MHz\n", pll_freq / 1000,
		(pll_freq % 1000) * 100 / 1000);

	mips_hpt_frequency = pll_freq / 2 * 1000;

	clocksource_mips.rating = 200 + mips_hpt_frequency / 10000000;

	clocksource_set_clock(&clocksource_mips, mips_hpt_frequency);

	clocksource_register(&clocksource_mips);
}

/**
 * struct tim_c - free running counter
 * @hi:	High 16 bits of the counter
 * @lo:	Low 32 bits of the counter
 *
 * Lays out the structure of the free running counter in memory. This counter
 * increments at a rate of 27 MHz/8 on all platforms.
 */
struct tim_c {
	unsigned int hi;
	unsigned int lo;
};

static struct tim_c *tim_c;

static cycle_t tim_c_read(struct clocksource *cs)
{
	unsigned int hi;
	unsigned int next_hi;
	unsigned int lo;

	hi = readl(&tim_c->hi);

	for (;;) {
		lo = readl(&tim_c->lo);
		next_hi = readl(&tim_c->hi);
		if (next_hi == hi)
			break;
		hi = next_hi;
	}

pr_crit("%s: read %llx\n", __func__, ((u64) hi << 32) | lo);
	return ((u64) hi << 32) | lo;
}

#define TIM_C_SIZE		48		/* # bits in the timer */

static struct clocksource clocksource_tim_c = {
	.name		= "powertv-tim_c",
	.read		= tim_c_read,
	.mask		= CLOCKSOURCE_MASK(TIM_C_SIZE),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

/**
 * powertv_tim_c_clocksource_init - set up a clock source for the TIM_C clock
 *
 * The hard part here is coming up with a constant k and shift s such that
 * the 48-bit TIM_C value multiplied by k doesn't overflow and that value,
 * when shifted right by s, yields the corresponding number of nanoseconds.
 * We know that TIM_C counts at 27 MHz/8, so each cycle corresponds to
 * 1 / (27,000,000/8) seconds. Multiply that by a billion and you get the
 * number of nanoseconds. Since the TIM_C value has 48 bits and the math is
 * done in 64 bits, avoiding an overflow means that k must be less than
 * 64 - 48 = 16 bits.
 */
static void __init powertv_tim_c_clocksource_init(void)
{
	int			prescale;
	unsigned long		dividend;
	unsigned long		k;
	int			s;
	const int		max_k_bits = (64 - 48) - 1;
	const unsigned long	billion = 1000000000;
	const unsigned long	counts_per_second = 27000000 / 8;

	prescale = BITS_PER_LONG - ilog2(billion) - 1;
	dividend = billion << prescale;
	k = dividend / counts_per_second;
	s = ilog2(k) - max_k_bits;

	if (s < 0)
		s = prescale;

	else {
		k >>= s;
		s += prescale;
	}

	clocksource_tim_c.mult = k;
	clocksource_tim_c.shift = s;
	clocksource_tim_c.rating = 200;

	clocksource_register(&clocksource_tim_c);
	tim_c = (struct tim_c *) asic_reg_addr(tim_ch);
}

/**
 powertv_clocksource_init - initialize all clocksources
 */
void __init powertv_clocksource_init(void)
{
	powertv_c0_hpt_clocksource_init();
	powertv_tim_c_clocksource_init();
}
