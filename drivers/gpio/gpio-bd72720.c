// SPDX-License-Identifier: GPL-2.0
/*
 * Support to GPIOs on ROHM BD72720 and BD79300
 * Copyright 2025 ROHM Semiconductors.
 * Author: Matti Vaittinen <mazziesaccount@gmail.com>
 */

#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/mfd/rohm-bd72720.h>

#define BD72720_GPIO_OPEN_DRAIN		0
#define BD72720_GPIO_CMOS		BIT(1)
#define BD72720_INT_GPIO1_IN_SRC	4
/*
 * The BD72720 has several "one time programmable" (OTP) configurations which
 * can be set at manufacturing phase. A set of these options allow using pins
 * as GPIO. The OTP configuration can't be read at run-time, so drivers rely on
 * device-tree to advertise the correct options.
 *
 * Both DVS[0,1] pins can be configured to be used for:
 *  - OTP0: regulator RUN state control
 *  - OTP1: GPI
 *  - OTP2: GPO
 *  - OTP3: Power sequencer output
 *  Data-sheet also states that these PINs can always be used for IRQ but the
 *  driver limits this by allowing them to be used for IRQs with OTP1 only.
 *
 * Pins GPIO_EXTEN0 (GPIO3), GPIO_EXTEN1 (GPIO4), GPIO_FAULT_B (GPIO5) have OTP
 * options for a specific (non GPIO) purposes, but also an option to configure
 * them to be used as a GPO.
 *
 * OTP settings can be separately configured for each pin.
 *
 * DT properties:
 * "rohm,pin-dvs0" and "rohm,pin-dvs1" can be set to one of the values:
 * "dvs-input", "gpi", "gpo".
 *
 * "rohm,pin-exten0", "rohm,pin-exten1" and "rohm,pin-fault_b" can be set to:
 * "gpo"
 */

enum bd72720_gpio_state {
	BD72720_PIN_UNKNOWN,
	BD72720_PIN_GPI,
	BD72720_PIN_GPO,
};

enum {
	BD72720_GPIO1,
	BD72720_GPIO2,
	BD72720_GPIO3,
	BD72720_GPIO4,
	BD72720_GPIO5,
	BD72720_GPIO_EPDEN,
	BD72720_NUM_GPIOS
};

struct bd72720_gpio {
	/* chip.parent points the MFD which provides DT node and regmap */
	struct gpio_chip chip;
	/* dev points to the platform device for devm and prints */
	struct device *dev;
	struct regmap *regmap;
	int gpio_is_input;
};

static int bd72720gpi_get(struct bd72720_gpio *bdgpio, unsigned int reg_offset)
{
	int ret, val, shift;

	ret = regmap_read(bdgpio->regmap, BD72720_REG_INT_ETC1_SRC, &val);
	if (ret)
		return ret;

	shift = BD72720_INT_GPIO1_IN_SRC + reg_offset;

	return (val >> shift) & 1;
}

static int bd72720gpo_get(struct bd72720_gpio *bdgpio,
			  unsigned int offset)
{
	const int regs[] = { BD72720_REG_GPIO1_CTRL, BD72720_REG_GPIO2_CTRL,
			     BD72720_REG_GPIO3_CTRL, BD72720_REG_GPIO4_CTRL,
			     BD72720_REG_GPIO5_CTRL, BD72720_REG_EPDEN_CTRL };
	int ret, val;

	ret = regmap_read(bdgpio->regmap, regs[offset], &val);
	if (ret)
		return ret;

	return val & BD72720_GPIO_HIGH;
}

static int bd72720gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct bd72720_gpio *bdgpio = gpiochip_get_data(chip);

	if (BIT(offset) & bdgpio->gpio_is_input)
		return bd72720gpi_get(bdgpio, offset);

	return bd72720gpo_get(bdgpio, offset);
}

static int bd72720gpo_set(struct gpio_chip *chip, unsigned int offset,
			  int value)
{
	struct bd72720_gpio *bdgpio = gpiochip_get_data(chip);
	const int regs[] = { BD72720_REG_GPIO1_CTRL, BD72720_REG_GPIO2_CTRL,
			     BD72720_REG_GPIO3_CTRL, BD72720_REG_GPIO4_CTRL,
			     BD72720_REG_GPIO5_CTRL, BD72720_REG_EPDEN_CTRL };

	if (BIT(offset) & bdgpio->gpio_is_input) {
		dev_dbg(bdgpio->dev, "pin %d not output.\n", offset);
		return -EINVAL;
	}

	if (value)
		return regmap_set_bits(bdgpio->regmap, regs[offset],
				      BD72720_GPIO_HIGH);

	return regmap_clear_bits(bdgpio->regmap, regs[offset],
					BD72720_GPIO_HIGH);
}

static int bd72720_gpio_set_config(struct gpio_chip *chip, unsigned int offset,
				   unsigned long config)
{
	struct bd72720_gpio *bdgpio = gpiochip_get_data(chip);
	const int regs[] = { BD72720_REG_GPIO1_CTRL, BD72720_REG_GPIO2_CTRL,
			     BD72720_REG_GPIO3_CTRL, BD72720_REG_GPIO4_CTRL,
			     BD72720_REG_GPIO5_CTRL, BD72720_REG_EPDEN_CTRL };

	/*
	 * We can only set the output mode, which makes sense only when output
	 * OTP configuration is used.
	 */
	if (BIT(offset) & bdgpio->gpio_is_input)
		return -ENOTSUPP;

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		return regmap_update_bits(bdgpio->regmap,
					  regs[offset],
					  BD72720_GPIO_DRIVE_MASK,
					  BD72720_GPIO_OPEN_DRAIN);
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		return regmap_update_bits(bdgpio->regmap,
					  regs[offset],
					  BD72720_GPIO_DRIVE_MASK,
					  BD72720_GPIO_CMOS);
	default:
		break;
	}

	return -ENOTSUPP;
}

static int bd72720gpo_direction_get(struct gpio_chip *chip,
				    unsigned int offset)
{
	struct bd72720_gpio *bdgpio = gpiochip_get_data(chip);

	if (BIT(offset) & bdgpio->gpio_is_input)
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}

static int bd72720_valid_mask(struct gpio_chip *gc,
			      unsigned long *valid_mask,
			      unsigned int ngpios)
{
	static const char * const properties[] = {
		"rohm,pin-dvs0", "rohm,pin-dvs1", "rohm,pin-exten0",
		"rohm,pin-exten1", "rohm,pin-fault_b"
	};
	struct bd72720_gpio *g = gpiochip_get_data(gc);
	const char *val;
	int i, ret;

	*valid_mask = BIT(BD72720_GPIO_EPDEN);

	if (!gc->parent)
		return 0;

	for (i = 0; i < ARRAY_SIZE(properties); i++) {
		ret = fwnode_property_read_string(dev_fwnode(gc->parent),
						  properties[i], &val);

		if (ret) {
			if (ret == -EINVAL)
				continue;

			dev_err(g->dev, "pin %d (%s), bad configuration\n", i,
				properties[i]);

			return ret;
		}

		if (strcmp(val, "gpi") == 0) {
			if (i != BD72720_GPIO1 && i != BD72720_GPIO2) {
				dev_warn(g->dev,
					 "pin %d (%s) does not support INPUT mode",
					 i, properties[i]);
				continue;
			}

			*valid_mask |= BIT(i);
			g->gpio_is_input |= BIT(i);
		} else if (strcmp(val, "gpo") == 0) {
			*valid_mask |= BIT(i);
		}
	}

	return 0;
}

/* Template for GPIO chip */
static const struct gpio_chip bd72720gpo_chip = {
	.label			= "bd72720",
	.owner			= THIS_MODULE,
	.get			= bd72720gpio_get,
	.get_direction		= bd72720gpo_direction_get,
	.set			= bd72720gpo_set,
	.set_config		= bd72720_gpio_set_config,
	.init_valid_mask	= bd72720_valid_mask,
	.can_sleep		= true,
	.ngpio			= BD72720_NUM_GPIOS,
	.base			= -1,
};

static int gpo_bd72720_probe(struct platform_device *pdev)
{
	struct bd72720_gpio *g;
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

	g->chip = bd72720gpo_chip;
	g->dev = dev;
	g->chip.parent = parent;
	g->regmap = dev_get_regmap(parent, NULL);

	return devm_gpiochip_add_data(dev, &g->chip, g);
}

static const struct platform_device_id bd72720_gpio_id[] = {
	{ "bd72720-gpio" },
	{ },
};
MODULE_DEVICE_TABLE(platform, bd72720_gpio_id);

static struct platform_driver gpo_bd72720_driver = {
	.driver = {
		.name = "bd72720-gpio",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = gpo_bd72720_probe,
	.id_table = bd72720_gpio_id,
};
module_platform_driver(gpo_bd72720_driver);

MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("GPIO interface for BD72720 and BD73900");
MODULE_LICENSE("GPL");
