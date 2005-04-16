/*
 * linux/arch/arm/mach-imx/leds.h
 *
 * Copyright (C) 2004 Sascha Hauer <sascha@saschahauer.de>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/leds.h>
#include <asm/mach-types.h>

#include "leds.h"

static int __init
leds_init(void)
{
	if (machine_is_mx1ads()) {
		leds_event = mx1ads_leds_event;
	}

	return 0;
}

__initcall(leds_init);
