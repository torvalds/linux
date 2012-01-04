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
#include <linux/bcma/bcma.h>
#include <asm/mach-bcm47xx/bcm47xx.h>

#define BCM47XX_EXTIF_GPIO_LINES	5
#define BCM47XX_CHIPCO_GPIO_LINES	16

extern int gpio_request(unsigned gpio, const char *label);
extern void gpio_free(unsigned gpio);
extern int gpio_to_irq(unsigned gpio);

static inline int gpio_get_value(unsigned gpio)
{
	switch (bcm47xx_bus_type) {
#ifdef CONFIG_BCM47XX_SSB
	case BCM47XX_BUS_TYPE_SSB:
		return ssb_gpio_in(&bcm47xx_bus.ssb, 1 << gpio);
#endif
#ifdef CONFIG_BCM47XX_BCMA
	case BCM47XX_BUS_TYPE_BCMA:
		return bcma_chipco_gpio_in(&bcm47xx_bus.bcma.bus.drv_cc,
					   1 << gpio);
#endif
	}
	return -EINVAL;
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	switch (bcm47xx_bus_type) {
#ifdef CONFIG_BCM47XX_SSB
	case BCM47XX_BUS_TYPE_SSB:
		ssb_gpio_out(&bcm47xx_bus.ssb, 1 << gpio,
			     value ? 1 << gpio : 0);
		return;
#endif
#ifdef CONFIG_BCM47XX_BCMA
	case BCM47XX_BUS_TYPE_BCMA:
		bcma_chipco_gpio_out(&bcm47xx_bus.bcma.bus.drv_cc, 1 << gpio,
				     value ? 1 << gpio : 0);
		return;
#endif
	}
}

static inline int gpio_direction_input(unsigned gpio)
{
	switch (bcm47xx_bus_type) {
#ifdef CONFIG_BCM47XX_SSB
	case BCM47XX_BUS_TYPE_SSB:
		ssb_gpio_outen(&bcm47xx_bus.ssb, 1 << gpio, 0);
		return 0;
#endif
#ifdef CONFIG_BCM47XX_BCMA
	case BCM47XX_BUS_TYPE_BCMA:
		bcma_chipco_gpio_outen(&bcm47xx_bus.bcma.bus.drv_cc, 1 << gpio,
				       0);
		return 0;
#endif
	}
	return -EINVAL;
}

static inline int gpio_direction_output(unsigned gpio, int value)
{
	switch (bcm47xx_bus_type) {
#ifdef CONFIG_BCM47XX_SSB
	case BCM47XX_BUS_TYPE_SSB:
		/* first set the gpio out value */
		ssb_gpio_out(&bcm47xx_bus.ssb, 1 << gpio,
			     value ? 1 << gpio : 0);
		/* then set the gpio mode */
		ssb_gpio_outen(&bcm47xx_bus.ssb, 1 << gpio, 1 << gpio);
		return 0;
#endif
#ifdef CONFIG_BCM47XX_BCMA
	case BCM47XX_BUS_TYPE_BCMA:
		/* first set the gpio out value */
		bcma_chipco_gpio_out(&bcm47xx_bus.bcma.bus.drv_cc, 1 << gpio,
				     value ? 1 << gpio : 0);
		/* then set the gpio mode */
		bcma_chipco_gpio_outen(&bcm47xx_bus.bcma.bus.drv_cc, 1 << gpio,
				       1 << gpio);
		return 0;
#endif
	}
	return -EINVAL;
}

static inline int gpio_intmask(unsigned gpio, int value)
{
	switch (bcm47xx_bus_type) {
#ifdef CONFIG_BCM47XX_SSB
	case BCM47XX_BUS_TYPE_SSB:
		ssb_gpio_intmask(&bcm47xx_bus.ssb, 1 << gpio,
				 value ? 1 << gpio : 0);
		return 0;
#endif
#ifdef CONFIG_BCM47XX_BCMA
	case BCM47XX_BUS_TYPE_BCMA:
		bcma_chipco_gpio_intmask(&bcm47xx_bus.bcma.bus.drv_cc,
					 1 << gpio, value ? 1 << gpio : 0);
		return 0;
#endif
	}
	return -EINVAL;
}

static inline int gpio_polarity(unsigned gpio, int value)
{
	switch (bcm47xx_bus_type) {
#ifdef CONFIG_BCM47XX_SSB
	case BCM47XX_BUS_TYPE_SSB:
		ssb_gpio_polarity(&bcm47xx_bus.ssb, 1 << gpio,
				  value ? 1 << gpio : 0);
		return 0;
#endif
#ifdef CONFIG_BCM47XX_BCMA
	case BCM47XX_BUS_TYPE_BCMA:
		bcma_chipco_gpio_polarity(&bcm47xx_bus.bcma.bus.drv_cc,
					  1 << gpio, value ? 1 << gpio : 0);
		return 0;
#endif
	}
	return -EINVAL;
}


/* cansleep wrappers */
#include <asm-generic/gpio.h>

#endif /* __BCM47XX_GPIO_H */
