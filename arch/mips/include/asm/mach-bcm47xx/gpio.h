/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 Aurelien Jarno <aurelien@aurel32.net>
 */

#ifndef __BCM47XX_GPIO_H
#define __BCM47XX_GPIO_H

#include <linux/ssb/ssb_embedded.h>
#include <asm/mach-bcm47xx/bcm47xx.h>

#define BCM47XX_EXTIF_GPIO_LINES	5
#define BCM47XX_CHIPCO_GPIO_LINES	16

extern int gpio_request(unsigned gpio, const char *label);
extern void gpio_free(unsigned gpio);
extern int gpio_to_irq(unsigned gpio);

static inline int gpio_get_value(unsigned gpio)
{
	return ssb_gpio_in(&ssb_bcm47xx, 1 << gpio);
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	ssb_gpio_out(&ssb_bcm47xx, 1 << gpio, value ? 1 << gpio : 0);
}

static inline int gpio_direction_input(unsigned gpio)
{
	return ssb_gpio_outen(&ssb_bcm47xx, 1 << gpio, 0);
}

static inline int gpio_direction_output(unsigned gpio, int value)
{
	return ssb_gpio_outen(&ssb_bcm47xx, 1 << gpio, 1 << gpio);
}

static int gpio_intmask(unsigned gpio, int value)
{
	return ssb_gpio_intmask(&ssb_bcm47xx, 1 << gpio,
				value ? 1 << gpio : 0);
}

static int gpio_polarity(unsigned gpio, int value)
{
	return ssb_gpio_polarity(&ssb_bcm47xx, 1 << gpio,
				 value ? 1 << gpio : 0);
}


/* cansleep wrappers */
#include <asm-generic/gpio.h>

#endif /* __BCM47XX_GPIO_H */
