/*
 * AM33XX voltage domain data
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>

#include "voltage.h"

static struct voltagedomain am33xx_voltdm_mpu = {
	.name		= "mpu",
};

static struct voltagedomain am33xx_voltdm_core = {
	.name		= "core",
};

static struct voltagedomain am33xx_voltdm_rtc = {
	.name		= "rtc",
};

static struct voltagedomain *voltagedomains_am33xx[] __initdata = {
	&am33xx_voltdm_mpu,
	&am33xx_voltdm_core,
	&am33xx_voltdm_rtc,
	NULL,
};

void __init am33xx_voltagedomains_init(void)
{
	voltdm_init(voltagedomains_am33xx);
}
