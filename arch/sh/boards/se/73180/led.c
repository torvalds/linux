/*
 * arch/sh/boards/se/73180/led.c
 *
 * Derived from arch/sh/boards/se/770x/led.c
 *
 * Copyright (C) 2000 Stuart Menefy <stuart.menefy@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * This file contains Solution Engine specific LED code.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <asm/mach/se73180.h>

static void
mach_led(int position, int value)
{
	volatile unsigned short *p = (volatile unsigned short *) PA_LED;

	if (value) {
		*p |= (1 << LED_SHIFT);
	} else {
		*p &= ~(1 << LED_SHIFT);
	}
}

/* Cycle the LED's in the clasic Knightrider/Sun pattern */
void
heartbeat_73180se(void)
{
	static unsigned int cnt = 0, period = 0;
	volatile unsigned short *p = (volatile unsigned short *) PA_LED;
	static unsigned bit = 0, up = 1;

	cnt += 1;
	if (cnt < period) {
		return;
	}

	cnt = 0;

	/* Go through the points (roughly!):
	 * f(0)=10, f(1)=16, f(2)=20, f(5)=35,f(inf)->110
	 */
	period = 110 - ((300 << FSHIFT) / ((avenrun[0] / 5) + (3 << FSHIFT)));

	if (up) {
		if (bit == 7) {
			bit--;
			up = 0;
		} else {
			bit++;
		}
	} else {
		if (bit == 0) {
			bit++;
			up = 1;
		} else {
			bit--;
		}
	}
	*p = 1 << (bit + LED_SHIFT);

}
