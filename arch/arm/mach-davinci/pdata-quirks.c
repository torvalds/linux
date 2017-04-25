/*
 * Legacy platform_data quirks
 *
 * Copyright (C) 2016 BayLibre, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/of_platform.h>

#include <mach/common.h>

struct pdata_init {
	const char *compatible;
	void (*fn)(void);
};

static void pdata_quirks_check(struct pdata_init *quirks)
{
	while (quirks->compatible) {
		if (of_machine_is_compatible(quirks->compatible)) {
			if (quirks->fn)
				quirks->fn();
			break;
		}
		quirks++;
	}
}

static struct pdata_init pdata_quirks[] __initdata = {
	{ /* sentinel */ },
};

void __init pdata_quirks_init(void)
{
	pdata_quirks_check(pdata_quirks);
}
