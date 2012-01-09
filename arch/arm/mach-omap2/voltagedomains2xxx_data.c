/*
 * OMAP3 voltage domain data
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>

#include "voltage.h"

static struct voltagedomain omap2_voltdm_core = {
	.name = "core",
};

static struct voltagedomain omap2_voltdm_wkup = {
	.name = "wakeup",
};

static struct voltagedomain *voltagedomains_omap2[] __initdata = {
	&omap2_voltdm_core,
	&omap2_voltdm_wkup,
	NULL,
};

void __init omap2xxx_voltagedomains_init(void)
{
	voltdm_init(voltagedomains_omap2);
}
