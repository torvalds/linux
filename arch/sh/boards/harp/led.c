/*
 * linux/arch/sh/stboards/led.c
 *
 * Copyright (C) 2000 Stuart Menefy <stuart.menefy@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * This file contains ST40STB1 HARP and compatible code.
 */

#include <linux/config.h>
#include <asm/io.h>
#include <asm/harp/harp.h>

/* Harp: Flash LD10 (front pannel) connected to EPLD (IC8) */
/* Overdrive: Flash LD1 (front panel) connected to EPLD (IC4) */
/* Works for HARP and overdrive */
static void mach_led(int position, int value)
{
	if (value) {
		ctrl_outl(EPLD_LED_ON, EPLD_LED);
	} else {
		ctrl_outl(EPLD_LED_OFF, EPLD_LED);
	}
}

#ifdef CONFIG_HEARTBEAT

#include <linux/sched.h>

/* acts like an actual heart beat -- ie thump-thump-pause... */
void heartbeat_harp(void)
{
	static unsigned cnt = 0, period = 0, dist = 0;

	if (cnt == 0 || cnt == dist)
		mach_led( -1, 1);
	else if (cnt == 7 || cnt == dist+7)
		mach_led( -1, 0);

	if (++cnt > period) {
		cnt = 0;
		/* The hyperbolic function below modifies the heartbeat period
		 * length in dependency of the current (5min) load. It goes
		 * through the points f(0)=126, f(1)=86, f(5)=51,
		 * f(inf)->30. */
		period = ((672<<FSHIFT)/(5*avenrun[0]+(7<<FSHIFT))) + 30;
		dist = period / 4;
	}
}
#endif
