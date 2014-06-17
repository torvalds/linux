/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/init.h>
#include <linux/time.h>

#include <asm/gic.h>

static cycle_t gic_hpt_read(struct clocksource *cs)
{
	return gic_read_count();
}

static struct clocksource gic_clocksource = {
	.name	= "GIC",
	.read	= gic_hpt_read,
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

void __init gic_clocksource_init(unsigned int frequency)
{
	unsigned int config, bits;

	/* Calculate the clocksource mask. */
	GICREAD(GIC_REG(SHARED, GIC_SH_CONFIG), config);
	bits = 32 + ((config & GIC_SH_CONFIG_COUNTBITS_MSK) >>
		(GIC_SH_CONFIG_COUNTBITS_SHF - 2));

	/* Set clocksource mask. */
	gic_clocksource.mask = CLOCKSOURCE_MASK(bits);

	/* Calculate a somewhat reasonable rating value. */
	gic_clocksource.rating = 200 + frequency / 10000000;

	clocksource_register_hz(&gic_clocksource, frequency);
}
