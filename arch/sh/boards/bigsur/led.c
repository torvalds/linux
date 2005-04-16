/*
 * linux/arch/sh/kernel/led_bigsur.c
 *
 * By Dustin McIntire (dustin@sensoria.com) (c)2001
 * Derived from led_se.c and led.c, which bore the message:
 * Copyright (C) 2000 Stuart Menefy <stuart.menefy@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * This file contains Big Sur specific LED code.
 */

#include <linux/config.h>
#include <asm/io.h>
#include <asm/bigsur/bigsur.h>

static void mach_led(int position, int value)
{
	int word;
	
	word = bigsur_inl(BIGSUR_CSLR);
	if (value) {
		bigsur_outl(word & ~BIGSUR_LED, BIGSUR_CSLR);
	} else {
		bigsur_outl(word | BIGSUR_LED, BIGSUR_CSLR);
	}
}

#ifdef CONFIG_HEARTBEAT

#include <linux/sched.h>

/* Cycle the LED on/off */
void heartbeat_bigsur(void)
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
#endif /* CONFIG_HEARTBEAT */

