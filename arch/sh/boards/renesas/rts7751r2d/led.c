/*
 * linux/arch/sh/kernel/led_rts7751r2d.c
 *
 * Copyright (C) Atom Create Engineering Co., Ltd.
 *
 * May be copied or modified under the terms of GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * This file contains Renesas Technology Sales RTS7751R2D specific LED code.
 */

#include <linux/config.h>
#include <asm/io.h>
#include <asm/rts7751r2d/rts7751r2d.h>

extern unsigned int debug_counter;

#ifdef CONFIG_HEARTBEAT

#include <linux/sched.h>

/* Cycle the LED's in the clasic Knightriger/Sun pattern */
void heartbeat_rts7751r2d(void)
{
	static unsigned int cnt = 0, period = 0;
	volatile unsigned short *p = (volatile unsigned short *)PA_OUTPORT;
	static unsigned bit = 0, up = 1;

	cnt += 1;
	if (cnt < period)
		return;

	cnt = 0;

	/* Go through the points (roughly!):
	 * f(0)=10, f(1)=16, f(2)=20, f(5)=35, f(int)->110
	 */
	period = 110 - ((300 << FSHIFT)/((avenrun[0]/5) + (3<<FSHIFT)));

	*p = 1 << bit;
	if (up)
		if (bit == 7) {
			bit--;
			up = 0;
		} else
			bit++;
	else if (bit == 0)
		up = 1;
	else
		bit--;
}
#endif /* CONFIG_HEARTBEAT */

void rts7751r2d_led(unsigned short value)
{
	ctrl_outw(value, PA_OUTPORT);
}

void debug_led_disp(void)
{
	unsigned short value;

	value = (unsigned short)debug_counter++;
	rts7751r2d_led(value);
	if (value == 0xff)
		debug_counter = 0;
}
