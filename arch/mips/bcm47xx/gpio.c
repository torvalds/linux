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

int bcm47xx_gpio_to_irq(unsigned gpio)
{
	if (ssb_bcm47xx.chipco.dev)
		return ssb_mips_irq(ssb_bcm47xx.chipco.dev) + 2;
	else if (ssb_bcm47xx.extif.dev)
		return ssb_mips_irq(ssb_bcm47xx.extif.dev) + 2;
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(bcm47xx_gpio_to_irq);

int bcm47xx_gpio_get_value(unsigned gpio)
{
	if (ssb_bcm47xx.chipco.dev)
		return ssb_chipco_gpio_in(&ssb_bcm47xx.chipco, 1 << gpio);
	else if (ssb_bcm47xx.extif.dev)
		return ssb_extif_gpio_in(&ssb_bcm47xx.extif, 1 << gpio);
	else
		return 0;
}
EXPORT_SYMBOL_GPL(bcm47xx_gpio_get_value);

void bcm47xx_gpio_set_value(unsigned gpio, int value)
{
	if (ssb_bcm47xx.chipco.dev)
		ssb_chipco_gpio_out(&ssb_bcm47xx.chipco,
				    1 << gpio,
				    value ? 1 << gpio : 0);
	else if (ssb_bcm47xx.extif.dev)
		ssb_extif_gpio_out(&ssb_bcm47xx.extif,
				   1 << gpio,
				   value ? 1 << gpio : 0);
}
EXPORT_SYMBOL_GPL(bcm47xx_gpio_set_value);

int bcm47xx_gpio_direction_input(unsigned gpio)
{
	if (ssb_bcm47xx.chipco.dev && (gpio < BCM47XX_CHIPCO_GPIO_LINES))
		ssb_chipco_gpio_outen(&ssb_bcm47xx.chipco,
				      1 << gpio, 0);
	else if (ssb_bcm47xx.extif.dev && (gpio < BCM47XX_EXTIF_GPIO_LINES))
		ssb_extif_gpio_outen(&ssb_bcm47xx.extif,
				     1 << gpio, 0);
	else
		return -EINVAL;
	return 0;
}
EXPORT_SYMBOL_GPL(bcm47xx_gpio_direction_input);

int bcm47xx_gpio_direction_output(unsigned gpio, int value)
{
	bcm47xx_gpio_set_value(gpio, value);

	if (ssb_bcm47xx.chipco.dev && (gpio < BCM47XX_CHIPCO_GPIO_LINES))
		ssb_chipco_gpio_outen(&ssb_bcm47xx.chipco,
				      1 << gpio, 1 << gpio);
	else if (ssb_bcm47xx.extif.dev && (gpio < BCM47XX_EXTIF_GPIO_LINES))
		ssb_extif_gpio_outen(&ssb_bcm47xx.extif,
				     1 << gpio, 1 << gpio);
	else
		return -EINVAL;
	return 0;
}
EXPORT_SYMBOL_GPL(bcm47xx_gpio_direction_output);

