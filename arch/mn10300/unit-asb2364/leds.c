/* leds.c: ASB2364 peripheral 7seg LEDs x4 support
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/processor.h>
#include <asm/intctl-regs.h>
#include <asm/rtc-regs.h>
#include <unit/leds.h>

#if MN10300_USE_7SEGLEDS
static const u8 asb2364_led_hex_tbl[16] = {
	0x80, 0xf2, 0x48, 0x60, 0x32, 0x24, 0x04, 0xf0,
	0x00, 0x20, 0x10, 0x06, 0x8c, 0x42, 0x0c, 0x1c
};

static const u32 asb2364_led_chase_tbl[6] = {
	~0x02020202,	/* top		- segA */
	~0x04040404,	/* right top	- segB */
	~0x08080808,	/* right bottom	- segC */
	~0x10101010,	/* bottom	- segD */
	~0x20202020,	/* left bottom	- segE */
	~0x40404040,	/* left top	- segF */
};

static unsigned asb2364_led_chase;

void peripheral_leds7x4_display_dec(unsigned int val, unsigned int points)
{
	u32 leds;

	leds = asb2364_led_hex_tbl[(val/1000) % 10];
	leds <<= 8;
	leds |= asb2364_led_hex_tbl[(val/100) % 10];
	leds <<= 8;
	leds |= asb2364_led_hex_tbl[(val/10) % 10];
	leds <<= 8;
	leds |= asb2364_led_hex_tbl[val % 10];
	leds |= points^0x01010101;

	ASB2364_7SEGLEDS = leds;
}

void peripheral_leds7x4_display_hex(unsigned int val, unsigned int points)
{
	u32 leds;

	leds = asb2364_led_hex_tbl[(val/1000) % 10];
	leds <<= 8;
	leds |= asb2364_led_hex_tbl[(val/100) % 10];
	leds <<= 8;
	leds |= asb2364_led_hex_tbl[(val/10) % 10];
	leds <<= 8;
	leds |= asb2364_led_hex_tbl[val % 10];
	leds |= points^0x01010101;

	ASB2364_7SEGLEDS = leds;
}

/* display triple horizontal bar and exception code */
void peripheral_leds_display_exception(enum exception_code code)
{
	u32 leds;

	leds = asb2364_led_hex_tbl[(code/0x100) % 0x10];
	leds <<= 8;
	leds |= asb2364_led_hex_tbl[(code/0x10) % 0x10];
	leds <<= 8;
	leds |= asb2364_led_hex_tbl[code % 0x10];
	leds |= 0x6d010101;

	ASB2364_7SEGLEDS = leds;
}

void peripheral_leds_led_chase(void)
{
	ASB2364_7SEGLEDS = asb2364_led_chase_tbl[asb2364_led_chase];
	asb2364_led_chase++;
	if (asb2364_led_chase >= 6)
		asb2364_led_chase = 0;
}
#else  /* MN10300_USE_7SEGLEDS */
void peripheral_leds7x4_display_dec(unsigned int val, unsigned int points) { }
void peripheral_leds7x4_display_hex(unsigned int val, unsigned int points) { }
void peripheral_leds_display_exception(enum exception_code code) { }
void peripheral_leds_led_chase(void) { }
#endif /* MN10300_USE_7SEGLEDS */
