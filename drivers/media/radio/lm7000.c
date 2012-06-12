/* Sanyo LM7000 tuner chip driver
 *
 * Copyright 2012 Ondrej Zary <linux@rainbow-software.org>
 * based on radio-aimslab.c by M. Kirkwood
 * and radio-sf16fmi.c by M. Kirkwood and Petr Vandrovec
 */

#include <linux/delay.h>
#include <linux/module.h>
#include "lm7000.h"

MODULE_AUTHOR("Ondrej Zary <linux@rainbow-software.org>");
MODULE_DESCRIPTION("Routines for Sanyo LM7000 AM/FM radio tuner chip");
MODULE_LICENSE("GPL");

/* write the 24-bit register, starting with LSB */
static void lm7000_write(struct lm7000 *lm, u32 val)
{
	int i;
	u8 data;

	for (i = 0; i < 24; i++) {
		data = val & (1 << i) ? LM7000_DATA : 0;
		lm->set_pins(lm, data | LM7000_CE);
		udelay(2);
		lm->set_pins(lm, data | LM7000_CE | LM7000_CLK);
		udelay(2);
		lm->set_pins(lm, data | LM7000_CE);
		udelay(2);
	}
	lm->set_pins(lm, 0);
}

void lm7000_set_freq(struct lm7000 *lm, u32 freq)
{
	freq += 171200;		/* Add 10.7 MHz IF */
	freq /= 400;		/* Convert to 25 kHz units */
	lm7000_write(lm, freq | LM7000_FM_25 | LM7000_BIT_FM);
}
EXPORT_SYMBOL(lm7000_set_freq);

static int __init lm7000_module_init(void)
{
	return 0;
}

static void __exit lm7000_module_exit(void)
{
}

module_init(lm7000_module_init)
module_exit(lm7000_module_exit)
