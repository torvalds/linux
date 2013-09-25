/*
 * Legacy platform_data quirks
 *
 * Copyright (C) 2013 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include "common.h"
#include "common-board-devices.h"
#include "dss-common.h"

struct pdata_init {
	const char *compatible;
	void (*fn)(void);
};

static struct pdata_init pdata_quirks[] __initdata = {
	{ /* sentinel */ },
};

void __init pdata_quirks_init(void)
{
	struct pdata_init *quirks = pdata_quirks;

	while (quirks->compatible) {
		if (of_machine_is_compatible(quirks->compatible)) {
			if (quirks->fn)
				quirks->fn();
			break;
		}
		quirks++;
	}
}
