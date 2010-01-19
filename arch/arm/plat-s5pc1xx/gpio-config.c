/* linux/arch/arm/plat-s5pc1xx/gpio-config.c
 *
 * Copyright 2009 Samsung Electronics
 *
 * S5PC1XX GPIO Configuration.
 *
 * Based on plat-s3c64xx/gpio-config.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/io.h>

#include <plat/gpio-core.h>
#include <plat/gpio-cfg-s5pc1xx.h>

s5p_gpio_drvstr_t s5p_gpio_get_drvstr(unsigned int pin, unsigned int off)
{
	struct s3c_gpio_chip *chip = s3c_gpiolib_getchip(pin);
	void __iomem *reg;
	int shift = off * 2;
	u32 drvstr;

	if (!chip)
		return -EINVAL;

	reg = chip->base + 0x0C;

	drvstr = __raw_readl(reg);
	drvstr = 0xffff & (0x3 << shift);
	drvstr = drvstr >> shift;

	return (__force s5p_gpio_drvstr_t)drvstr;
}
EXPORT_SYMBOL(s5p_gpio_get_drvstr);

int s5p_gpio_set_drvstr(unsigned int pin, unsigned int off,
			s5p_gpio_drvstr_t drvstr)
{
	struct s3c_gpio_chip *chip = s3c_gpiolib_getchip(pin);
	void __iomem *reg;
	int shift = off * 2;
	u32 tmp;

	if (!chip)
		return -EINVAL;

	reg = chip->base + 0x0C;

	tmp = __raw_readl(reg);
	tmp |= drvstr << shift;

	__raw_writel(tmp, reg);

	return 0;
}
EXPORT_SYMBOL(s5p_gpio_set_drvstr);
