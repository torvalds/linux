/*
 * Copyright (C) Atom Create Engineering Co., Ltd.
 *
 * May be copied or modified under the terms of GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * This file contains Renesas Solutions HIGHLANDER R7780RP-1 specific LED code.
 */
#include <linux/sched.h>
#include <asm/io.h>
#include <asm/r7780rp/r7780rp.h>

/* Cycle the LED's in the clasic Knightriger/Sun pattern */
void heartbeat_r7780rp(void)
{
	static unsigned int cnt = 0, period = 0;
	volatile unsigned short *p = (volatile unsigned short *)PA_OBLED;
	static unsigned bit = 0, up = 1;
	unsigned bit_pos[] = {2, 1, 0, 3, 6, 5, 4, 7};

	cnt += 1;
	if (cnt < period)
		return;

	cnt = 0;

	/* Go through the points (roughly!):
	 * f(0)=10, f(1)=16, f(2)=20, f(5)=35, f(int)->110
	 */
	period = 110 - ((300 << FSHIFT)/((avenrun[0]/5) + (3<<FSHIFT)));

	*p = 1 << bit_pos[bit];
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
