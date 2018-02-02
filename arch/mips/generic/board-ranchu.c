/*
 * Support code for virtual Ranchu board for MIPS.
 *
 * Author: Miodrag Dinic <miodrag.dinic@mips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/of_address.h>
#include <linux/types.h>

#include <asm/machine.h>
#include <asm/mipsregs.h>
#include <asm/time.h>

#define GOLDFISH_TIMER_LOW		0x00
#define GOLDFISH_TIMER_HIGH		0x04

static __init u64 read_rtc_time(void __iomem *base)
{
	u32 time_low;
	u32 time_high;

	/*
	 * Reading the low address latches the high value
	 * as well so there is no fear that we may read
	 * inaccurate high value.
	 */
	time_low = readl(base + GOLDFISH_TIMER_LOW);
	time_high = readl(base + GOLDFISH_TIMER_HIGH);

	return ((u64)time_high << 32) | time_low;
}

static __init unsigned int ranchu_measure_hpt_freq(void)
{
	u64 rtc_start, rtc_current, rtc_delta;
	unsigned int start, count;
	struct device_node *np;
	void __iomem *rtc_base;

	np = of_find_compatible_node(NULL, NULL, "google,goldfish-rtc");
	if (!np)
		panic("%s(): Failed to find 'google,goldfish-rtc' dt node!",
		      __func__);

	rtc_base = of_iomap(np, 0);
	if (!rtc_base)
		panic("%s(): Failed to ioremap Goldfish RTC base!", __func__);

	/*
	 * Poll the nanosecond resolution RTC for one
	 * second to calibrate the CPU frequency.
	 */
	rtc_start = read_rtc_time(rtc_base);
	start = read_c0_count();

	do {
		rtc_current = read_rtc_time(rtc_base);
		rtc_delta = rtc_current - rtc_start;
	} while (rtc_delta < NSEC_PER_SEC);

	count = read_c0_count() - start;

	/*
	 * Make sure the frequency will be a round number.
	 * Without this correction, the returned value may vary
	 * between subsequent emulation executions.
	 *
	 * TODO: Set this value using device tree.
	 */
	count += 5000;
	count -= count % 10000;

	iounmap(rtc_base);

	return count;
}

static const struct of_device_id ranchu_of_match[] __initconst = {
	{
		.compatible = "mti,ranchu",
	},
	{}
};

MIPS_MACHINE(ranchu) = {
	.matches = ranchu_of_match,
	.measure_hpt_freq = ranchu_measure_hpt_freq,
};
