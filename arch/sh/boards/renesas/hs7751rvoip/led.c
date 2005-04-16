/*
 * linux/arch/sh/kernel/setup_hs7751rvoip.c
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * Renesas Technology Sales HS7751RVoIP Support.
 *
 * Modified for HS7751RVoIP by
 * Atom Create Engineering Co., Ltd. 2002.
 * Lineo uSolutions, Inc. 2003.
 */

#include <linux/config.h>
#include <asm/io.h>
#include <asm/hs7751rvoip/hs7751rvoip.h>

extern unsigned int debug_counter;

void debug_led_disp(void)
{
	unsigned short value;

	value = (unsigned char)debug_counter++;
	ctrl_outb((0xf0|value), PA_OUTPORTR);
	if (value == 0x0f)
		debug_counter = 0;
}
