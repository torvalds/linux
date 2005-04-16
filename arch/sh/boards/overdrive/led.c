/*
 * linux/arch/sh/overdrive/led.c
 *
 * Copyright (C) 1999 Stuart Menefy <stuart.menefy@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * This file contains an Overdrive specific LED feature.
 */

#include <linux/config.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/overdrive/overdrive.h>

static void mach_led(int position, int value)
{
	unsigned long flags;
	unsigned long reg;

	local_irq_save(flags);
	
	reg = readl(OVERDRIVE_CTRL);
	if (value) {
		reg |= (1<<3);
	} else {
		reg &= ~(1<<3);
	}
	writel(reg, OVERDRIVE_CTRL);

	local_irq_restore(flags);
}

#ifdef CONFIG_HEARTBEAT

#include <linux/sched.h>

/* acts like an actual heart beat -- ie thump-thump-pause... */
void heartbeat_od(void)
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
