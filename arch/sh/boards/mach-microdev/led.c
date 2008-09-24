/*
 * linux/arch/sh/boards/superh/microdev/led.c
 *
 * Copyright (C) 2002 Stuart Menefy <stuart.menefy@st.com>
 * Copyright (C) 2003 Richard Curnow (Richard.Curnow@superh.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 */

#include <asm/io.h>

#define LED_REGISTER 0xa6104d20

static void mach_led_d9(int value)
{
	unsigned long reg;
	reg = ctrl_inl(LED_REGISTER);
	reg &= ~1;
	reg |= (value & 1);
	ctrl_outl(reg, LED_REGISTER);
	return;
}

static void mach_led_d10(int value)
{
	unsigned long reg;
	reg = ctrl_inl(LED_REGISTER);
	reg &= ~2;
	reg |= ((value & 1) << 1);
	ctrl_outl(reg, LED_REGISTER);
	return;
}


#ifdef CONFIG_HEARTBEAT
#include <linux/sched.h>

static unsigned char banner_table[] = {
	0x11, 0x01, 0x11, 0x01, 0x11, 0x03,
	0x11, 0x01, 0x11, 0x01, 0x13, 0x03,
	0x11, 0x01, 0x13, 0x01, 0x13, 0x01, 0x11, 0x03,
	0x11, 0x03,
	0x11, 0x01, 0x13, 0x01, 0x11, 0x03,
	0x11, 0x01, 0x11, 0x01, 0x11, 0x01, 0x11, 0x07,
	0x13, 0x01, 0x13, 0x03,
	0x11, 0x01, 0x11, 0x03,
	0x13, 0x01, 0x11, 0x01, 0x13, 0x01, 0x11, 0x03,
	0x11, 0x01, 0x13, 0x01, 0x11, 0x03,
	0x13, 0x01, 0x13, 0x01, 0x13, 0x03,
	0x13, 0x01, 0x11, 0x01, 0x11, 0x03,
	0x11, 0x03,
	0x11, 0x01, 0x11, 0x01, 0x11, 0x01, 0x13, 0x07,
	0xff
};

static void banner(void)
{
	static int pos = 0;
	static int count = 0;

	if (count) {
		count--;
	} else {
		int val = banner_table[pos];
		if (val == 0xff) {
			pos = 0;
			val = banner_table[pos];
		}
		pos++;
		mach_led_d10((val >> 4) & 1);
		count = 10 * (val & 0xf);
	}
}

/* From heartbeat_harp in the stboards directory */
/* acts like an actual heart beat -- ie thump-thump-pause... */
void microdev_heartbeat(void)
{
	static unsigned cnt = 0, period = 0, dist = 0;

	if (cnt == 0 || cnt == dist)
		mach_led_d9(1);
	else if (cnt == 7 || cnt == dist+7)
		mach_led_d9(0);

	if (++cnt > period) {
		cnt = 0;
		/* The hyperbolic function below modifies the heartbeat period
		 * length in dependency of the current (5min) load. It goes
		 * through the points f(0)=126, f(1)=86, f(5)=51,
		 * f(inf)->30. */
		period = ((672<<FSHIFT)/(5*avenrun[0]+(7<<FSHIFT))) + 30;
		dist = period / 4;
	}

	banner();
}

#endif
