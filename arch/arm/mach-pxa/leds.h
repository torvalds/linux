/*
 * include/asm-arm/arch-pxa/leds.h
 *
 * Copyright (c) 2001 Jeff Sutherland, Accelent Systems Inc.
 *
 * blinky lights for various PXA-based systems:
 *
 */

extern void idp_leds_event(led_event_t evt);
extern void lubbock_leds_event(led_event_t evt);
extern void mainstone_leds_event(led_event_t evt);
