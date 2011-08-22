/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 Aurelien Jarno <aurelien@aurel32.net>
 */

#include <linux/ssb/ssb.h>
#include <linux/ssb/ssb_driver_chipcommon.h>
#include <linux/ssb/ssb_driver_extif.h>
#include <asm/mach-bcm47xx/bcm47xx.h>
#include <asm/mach-bcm47xx/gpio.h>

#if (BCM47XX_CHIPCO_GPIO_LINES > BCM47XX_EXTIF_GPIO_LINES)
static DECLARE_BITMAP(gpio_in_use, BCM47XX_CHIPCO_GPIO_LINES);
#else
static DECLARE_BITMAP(gpio_in_use, BCM47XX_EXTIF_GPIO_LINES);
#endif

int gpio_request(unsigned gpio, const char *tag)
{
	switch (bcm47xx_bus_type) {
#ifdef CONFIG_BCM47XX_SSB
	case BCM47XX_BUS_TYPE_SSB:
		if (ssb_chipco_available(&bcm47xx_bus.ssb.chipco) &&
		    ((unsigned)gpio >= BCM47XX_CHIPCO_GPIO_LINES))
			return -EINVAL;

		if (ssb_extif_available(&bcm47xx_bus.ssb.extif) &&
		    ((unsigned)gpio >= BCM47XX_EXTIF_GPIO_LINES))
			return -EINVAL;

		if (test_and_set_bit(gpio, gpio_in_use))
			return -EBUSY;

		return 0;
#endif
#ifdef CONFIG_BCM47XX_BCMA
	case BCM47XX_BUS_TYPE_BCMA:
		if (gpio >= BCM47XX_CHIPCO_GPIO_LINES)
			return -EINVAL;

		if (test_and_set_bit(gpio, gpio_in_use))
			return -EBUSY;

		return 0;
#endif
	}
	return -EINVAL;
}
EXPORT_SYMBOL(gpio_request);

void gpio_free(unsigned gpio)
{
	switch (bcm47xx_bus_type) {
#ifdef CONFIG_BCM47XX_SSB
	case BCM47XX_BUS_TYPE_SSB:
		if (ssb_chipco_available(&bcm47xx_bus.ssb.chipco) &&
		    ((unsigned)gpio >= BCM47XX_CHIPCO_GPIO_LINES))
			return;

		if (ssb_extif_available(&bcm47xx_bus.ssb.extif) &&
		    ((unsigned)gpio >= BCM47XX_EXTIF_GPIO_LINES))
			return;

		clear_bit(gpio, gpio_in_use);
		return;
#endif
#ifdef CONFIG_BCM47XX_BCMA
	case BCM47XX_BUS_TYPE_BCMA:
		if (gpio >= BCM47XX_CHIPCO_GPIO_LINES)
			return;

		clear_bit(gpio, gpio_in_use);
		return;
#endif
	}
}
EXPORT_SYMBOL(gpio_free);

int gpio_to_irq(unsigned gpio)
{
	switch (bcm47xx_bus_type) {
#ifdef CONFIG_BCM47XX_SSB
	case BCM47XX_BUS_TYPE_SSB:
		if (ssb_chipco_available(&bcm47xx_bus.ssb.chipco))
			return ssb_mips_irq(bcm47xx_bus.ssb.chipco.dev) + 2;
		else if (ssb_extif_available(&bcm47xx_bus.ssb.extif))
			return ssb_mips_irq(bcm47xx_bus.ssb.extif.dev) + 2;
		else
			return -EINVAL;
#endif
#ifdef CONFIG_BCM47XX_BCMA
	case BCM47XX_BUS_TYPE_BCMA:
		return bcma_core_mips_irq(bcm47xx_bus.bcma.bus.drv_cc.core) + 2;
#endif
	}
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(gpio_to_irq);
