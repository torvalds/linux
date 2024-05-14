// SPDX-License-Identifier: GPL-2.0
/*
 * Support to GPOs on ROHM BD71815
 * Copyright 2021 ROHM Semiconductors.
 * Author: Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>
 *
 * Copyright 2014 Embest Technology Co. Ltd. Inc.
 * Author: yanglsh@embest-tech.com
 */

#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
/* For the BD71815 register definitions */
#include <linux/mfd/rohm-bd71815.h>

struct bd71815_gpio {
	/* chip.parent points the MFD which provides DT node and regmap */
	struct gpio_chip chip;
	/* dev points to the platform device for devm and prints */
	struct device *dev;
	struct regmap *regmap;
};

static int bd71815gpo_get(struct gpio_chip *chip, unsigned int offset)
{
	struct bd71815_gpio *bd71815 = gpiochip_get_data(chip);
	int ret, val;

	ret = regmap_read(bd71815->regmap, BD71815_REG_GPO, &val);
	if (ret)
		return ret;

	return (val >> offset) & 1;
}

static void bd71815gpo_set(struct gpio_chip *chip, unsigned int offset,
			   int value)
{
	struct bd71815_gpio *bd71815 = gpiochip_get_data(chip);
	int ret, bit;

	bit = BIT(offset);

	if (value)
		ret = regmap_set_bits(bd71815->regmap, BD71815_REG_GPO, bit);
	else
		ret = regmap_clear_bits(bd71815->regmap, BD71815_REG_GPO, bit);

	if (ret)
		dev_warn(bd71815->dev, "failed to toggle GPO\n");
}

static int bd71815_gpio_set_config(struct gpio_chip *chip, unsigned int offset,
				   unsigned long config)
{
	struct bd71815_gpio *bdgpio = gpiochip_get_data(chip);

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		return regmap_update_bits(bdgpio->regmap,
					  BD71815_REG_GPO,
					  BD71815_GPIO_DRIVE_MASK << offset,
					  BD71815_GPIO_OPEN_DRAIN << offset);
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		return regmap_update_bits(bdgpio->regmap,
					  BD71815_REG_GPO,
					  BD71815_GPIO_DRIVE_MASK << offset,
					  BD71815_GPIO_CMOS << offset);
	default:
		break;
	}
	return -ENOTSUPP;
}

/* BD71815 GPIO is actually GPO */
static int bd71815gpo_direction_get(struct gpio_chip *gc, unsigned int offset)
{
	return GPIO_LINE_DIRECTION_OUT;
}

/* Template for GPIO chip */
static const struct gpio_chip bd71815gpo_chip = {
	.label			= "bd71815",
	.owner			= THIS_MODULE,
	.get			= bd71815gpo_get,
	.get_direction		= bd71815gpo_direction_get,
	.set			= bd71815gpo_set,
	.set_config		= bd71815_gpio_set_config,
	.can_sleep		= true,
};

#define BD71815_TWO_GPIOS	GENMASK(1, 0)
#define BD71815_ONE_GPIO	BIT(0)

/*
 * Sigh. The BD71815 and BD71817 were originally designed to support two GPO
 * pins. At some point it was noticed the second GPO pin which is the E5 pin
 * located at the center of IC is hard to use on PCB (due to the location). It
 * was decided to not promote this second GPO and the pin is marked as GND in
 * the datasheet. The functionality is still there though! I guess driving a GPO
 * connected to the ground is a bad idea. Thus we do not support it by default.
 * OTOH - the original driver written by colleagues at Embest did support
 * controlling this second GPO. It is thus possible this is used in some of the
 * products.
 *
 * This driver does not by default support configuring this second GPO
 * but allows using it by providing the DT property
 * "rohm,enable-hidden-gpo".
 */
static int bd71815_init_valid_mask(struct gpio_chip *gc,
				   unsigned long *valid_mask,
				   unsigned int ngpios)
{
	if (ngpios != 2)
		return 0;

	if (gc->parent && device_property_present(gc->parent,
						  "rohm,enable-hidden-gpo"))
		*valid_mask = BD71815_TWO_GPIOS;
	else
		*valid_mask = BD71815_ONE_GPIO;

	return 0;
}

static int gpo_bd71815_probe(struct platform_device *pdev)
{
	struct bd71815_gpio *g;
	struct device *parent, *dev;

	/*
	 * Bind devm lifetime to this platform device => use dev for devm.
	 * also the prints should originate from this device.
	 */
	dev = &pdev->dev;
	/* The device-tree and regmap come from MFD => use parent for that */
	parent = dev->parent;

	g = devm_kzalloc(dev, sizeof(*g), GFP_KERNEL);
	if (!g)
		return -ENOMEM;

	g->chip = bd71815gpo_chip;

	/*
	 * FIXME: As writing of this the sysfs interface for GPIO control does
	 * not respect the valid_mask. Do not trust it but rather set the ngpios
	 * to 1 if "rohm,enable-hidden-gpo" is not given.
	 *
	 * This check can be removed later if the sysfs export is fixed and
	 * if the fix is backported.
	 *
	 * For now it is safest to just set the ngpios though.
	 */
	if (device_property_present(parent, "rohm,enable-hidden-gpo"))
		g->chip.ngpio = 2;
	else
		g->chip.ngpio = 1;

	g->chip.init_valid_mask = bd71815_init_valid_mask;
	g->chip.base = -1;
	g->chip.parent = parent;
	g->regmap = dev_get_regmap(parent, NULL);
	g->dev = dev;

	return devm_gpiochip_add_data(dev, &g->chip, g);
}

static struct platform_driver gpo_bd71815_driver = {
	.driver = {
		.name	= "bd71815-gpo",
	},
	.probe		= gpo_bd71815_probe,
};
module_platform_driver(gpo_bd71815_driver);

MODULE_ALIAS("platform:bd71815-gpo");
MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_AUTHOR("Peter Yang <yanglsh@embest-tech.com>");
MODULE_DESCRIPTION("GPO interface for BD71815");
MODULE_LICENSE("GPL");
