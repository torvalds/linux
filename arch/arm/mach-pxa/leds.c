/*
 * linux/arch/arm/mach-pxa/leds.c
 *
 * xscale LEDs dispatcher
 *
 * Copyright (C) 2001 Nicolas Pitre
 *
 * Copyright (c) 2001 Jeff Sutherland, Accelent Systems Inc.
 */
#include <linux/compiler.h>
#include <linux/init.h>

#include <asm/leds.h>
#include <asm/mach-types.h>

#include "leds.h"

static int __init
pxa_leds_init(void)
{
	if (machine_is_lubbock())
		leds_event = lubbock_leds_event;
	if (machine_is_mainstone())
		leds_event = mainstone_leds_event;
	if (machine_is_pxa_idp())
		leds_event = idp_leds_event;

	leds_event(led_start);
	return 0;
}

core_initcall(pxa_leds_init);
