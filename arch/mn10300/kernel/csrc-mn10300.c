/* MN10300 clocksource
 *
 * Copyright (C) 2010 Red Hat, Inc. All Rights Reserved.
 * Written by Mark Salter (msalter@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/clocksource.h>
#include <linux/init.h>
#include <asm/timex.h>
#include "internal.h"

static cycle_t mn10300_read(struct clocksource *cs)
{
	return read_timestamp_counter();
}

static struct clocksource clocksource_mn10300 = {
	.name	= "TSC",
	.rating	= 200,
	.read	= mn10300_read,
	.mask	= CLOCKSOURCE_MASK(32),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

int __init init_clocksource(void)
{
	startup_timestamp_counter();
	clocksource_register_hz(&clocksource_mn10300, MN10300_TSCCLK);
	return 0;
}
