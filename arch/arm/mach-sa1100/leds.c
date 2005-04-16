/*
 * linux/arch/arm/mach-sa1100/leds.c
 *
 * SA1100 LEDs dispatcher
 * 
 * Copyright (C) 2001 Nicolas Pitre
 */
#include <linux/compiler.h>
#include <linux/init.h>

#include <asm/leds.h>
#include <asm/mach-types.h>

#include "leds.h"

static int __init
sa1100_leds_init(void)
{
	if (machine_is_assabet())
		leds_event = assabet_leds_event;
 	if (machine_is_consus())
 	        leds_event = consus_leds_event;
	if (machine_is_badge4())
	  	leds_event = badge4_leds_event;
	if (machine_is_brutus())
		leds_event = brutus_leds_event;
	if (machine_is_cerf())
		leds_event = cerf_leds_event;
	if (machine_is_flexanet())
		leds_event = flexanet_leds_event;
	if (machine_is_graphicsclient())
		leds_event = graphicsclient_leds_event;
	if (machine_is_hackkit())
		leds_event = hackkit_leds_event;
	if (machine_is_lart())
		leds_event = lart_leds_event;
	if (machine_is_pfs168())
		leds_event = pfs168_leds_event;
	if (machine_is_graphicsmaster())
		leds_event = graphicsmaster_leds_event;
	if (machine_is_adsbitsy())
		leds_event = adsbitsy_leds_event;
	if (machine_is_pt_system3())
		leds_event = system3_leds_event;
	if (machine_is_simpad())
		leds_event = simpad_leds_event; /* what about machine registry? including led, apm... -zecke */

	leds_event(led_start);
	return 0;
}

core_initcall(sa1100_leds_init);
