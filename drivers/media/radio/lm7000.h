/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LM7000_H
#define __LM7000_H

/* Sanyo LM7000 tuner chip control
 *
 * Copyright 2012 Ondrej Zary <linux@rainbow-software.org>
 * based on radio-aimslab.c by M. Kirkwood
 * and radio-sf16fmi.c by M. Kirkwood and Petr Vandrovec
 */

#define LM7000_DATA	(1 << 0)
#define LM7000_CLK	(1 << 1)
#define LM7000_CE	(1 << 2)

#define LM7000_FM_100	(0 << 20)
#define LM7000_FM_50	(1 << 20)
#define LM7000_FM_25	(2 << 20)
#define LM7000_BIT_FM	(1 << 23)

static inline void lm7000_set_freq(u32 freq, void *handle,
				void (*set_pins)(void *handle, u8 pins))
{
	int i;
	u8 data;
	u32 val;

	freq += 171200;		/* Add 10.7 MHz IF */
	freq /= 400;		/* Convert to 25 kHz units */
	val = freq | LM7000_FM_25 | LM7000_BIT_FM;
	/* write the 24-bit register, starting with LSB */
	for (i = 0; i < 24; i++) {
		data = val & (1 << i) ? LM7000_DATA : 0;
		set_pins(handle, data | LM7000_CE);
		udelay(2);
		set_pins(handle, data | LM7000_CE | LM7000_CLK);
		udelay(2);
		set_pins(handle, data | LM7000_CE);
		udelay(2);
	}
	set_pins(handle, 0);
}

#endif /* __LM7000_H */
