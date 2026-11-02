// SPDX-License-Identifier: GPL-2.0
/*
 * Support to GPIOs on ROHM BD73800
 * Copyright 2024 ROHM Semiconductors.
 * Author: Matti Vaittinen <mazziesaccount@gmail.com>
 */

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/regmap.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/rohm-bd73800.h>

#define BD73800_GPIO_MAX_PINS 4
/*
 * The BD73800 has several "one time programmable" (OTP) configurations which
 * can be set at manufacturing phase. Some of these options allow using
 * individual pins as GPI or GPO (not both at the same time). The OTP
 * configuration can't be read at run-time, so drivers rely on device-tree to
 * advertise the OTP programmed in manufacturing.
 *
 * The pins which can be used as GPIO are:
 * GPIO1, CLKOUT (GPIO2), FAULT_B, EXTEN_OUT.
 *
 * The OTP options 2 and 3 state for all the pins:
 *  - OTP2: GPI (also IRQ source)
 *  - OTP3: GPO (NOTE: This is actually 2 different OTP options. Either a
 *    register controllable output or a power-sequence controlled output.
 *    The "gpo" referred here means only the register controllable output.)
 *    The datasheet refers to this as: "<pin> output is controlled by
 *    GPIO<N>_OUT or power on/off sequencer to control external VRs. ON/OFF
 *    sequence timing is configurable."
 *
 * The data-sheet further says that the GPI/GPO is not a default OTP
 * configuration for any of the pins. Hence the GPIO driver defaults to a pin
 * not being a GPI or GPO, but requires the pin to be explicitly marked as a
 * GPI or GPO in the device-tree.
 *
 * DT properties:
 * "rohm,pin-gpio1", "rohm,pin-clkout", "rohm,pin-fault_b", "rohm,pin-exten"
 * can be set to one of the values "gpi" or "gpo" to enable them to be used as
 * GPIO.
 *
 * The amount of GPIO lines the chip exposes to the user-space (chip.ngpio) is
 * always the same, regardless of the OTP variant in use. The lines which are
 * not usable as GPIO on a given OTP variant are marked invalid via the GPIO
 * valid_mask. This way the user-space always sees a constant amount of GPIO
 * lines, where numbering (relative to the chip's base) stays also the same.
 * Users can then use the valid_mask to find out which of the lines are usable.
 */

struct bd73800_gpio {
	/* dev points to the platform device for devm and prints */
	struct device *dev;
	struct regmap *regmap;
	/* Pins which have been OTP configured as GPI or GPO */
	DECLARE_BITMAP(valid_mask, BD73800_GPIO_MAX_PINS);
	/* Subset of valid_mask - pins which have been OTP configured as GPO */
	DECLARE_BITMAP(output_mask, BD73800_GPIO_MAX_PINS);
};

static const char * const bd73800_gpio_properties[BD73800_GPIO_MAX_PINS] = {
	"rohm,pin-gpio1",
	"rohm,pin-clkout",
	"rohm,pin-fault_b",
	"rohm,pin-exten"
};

static int bd73800_gpio_get_pins(struct bd73800_gpio *data)
{
	struct device *parent = data->dev->parent;
	const char *val;
	int i, ret;

	for (i = 0; i < BD73800_GPIO_MAX_PINS; i++) {
		ret = device_property_read_string(parent,
						  bd73800_gpio_properties[i],
						  &val);
		if (ret) {
			if (ret == -EINVAL)
				continue;

			return dev_err_probe(data->dev, ret,
					"pin %d (%s), bad configuration\n", i,
					bd73800_gpio_properties[i]);
		}

		if (!strcmp(val, "gpi")) {
			__set_bit(i, data->valid_mask);
		} else if (!strcmp(val, "gpo")) {
			__set_bit(i, data->valid_mask);
			__set_bit(i, data->output_mask);
		} else {
			dev_warn(data->dev,
				 "pin %d (%s), unknown value '%s' ignored\n", i,
				 bd73800_gpio_properties[i], val);
		}
	}

	return 0;
}

static int bd73800_gpio_init_valid_mask(struct gpio_chip *gc,
					unsigned long *valid_mask,
					unsigned int ngpios)
{
	struct gpio_regmap *gpio = gpiochip_get_data(gc);
	struct bd73800_gpio *data = gpio_regmap_get_drvdata(gpio);

	bitmap_copy(valid_mask, data->valid_mask, ngpios);

	return 0;
}

/*
 * The used register depends on OTP:
 *  - If OTP has set pin as GPO, only the GPO_OUT register is valid.
 *  - If OTP has set pin as GPI, only the INT_5_SRC register is valid.
 */
static int bd73800_gpio_reg_mask_xlate(struct gpio_regmap *gpio,
				       enum gpio_regmap_operation op,
				       unsigned int base, unsigned int offset,
				       unsigned int *reg, unsigned int *mask)
{
	struct bd73800_gpio *data = gpio_regmap_get_drvdata(gpio);
	bool is_output = test_bit(offset, data->output_mask);

	if (is_output)
		*reg = BD73800_REG_GPO_OUT;
	else
		*reg = base;

	*mask = BIT(offset);

	return 0;
}

static int gpo_bd73800_probe(struct platform_device *pdev)
{
	struct gpio_regmap_config config = { };
	struct bd73800_gpio *data;
	struct device *parent, *dev;
	int ret;

	dev = &pdev->dev;
	/* The device-tree and regmap come from MFD => use parent for that */
	parent = dev->parent;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;
	data->regmap = dev_get_regmap(parent, NULL);
	if (!data->regmap)
		return dev_err_probe(dev, -ENODEV, "no parent regmap\n");

	ret = bd73800_gpio_get_pins(data);
	if (ret)
		return ret;

	if (bitmap_empty(data->valid_mask, BD73800_GPIO_MAX_PINS)) {
		/*
		 * The BD73800 may or may not have pins allocated for GPIO
		 * depending on the OTP used at manufacturing.
		 * If there are no pins, then we have nothing to do.
		 */
		dev_dbg(dev, "no GPIO pins\n");
		return -ENODEV;
	}

	config.parent = parent;
	config.regmap = data->regmap;
	config.label = "bd73800";
	config.ngpio = BD73800_GPIO_MAX_PINS;
	config.reg_dat_base = BD73800_REG_INT_5_SRC;
	config.reg_set_base = BD73800_REG_GPO_OUT;
	config.reg_mask_xlate = bd73800_gpio_reg_mask_xlate;
	config.init_valid_mask = bd73800_gpio_init_valid_mask;
	/* All pins that are valid GPIO lines also have a fixed direction */
	config.fixed_direction_mask = data->valid_mask;
	config.fixed_direction_output = data->output_mask;
	config.drvdata = data;

	return PTR_ERR_OR_ZERO(devm_gpio_regmap_register(dev, &config));
}

static const struct platform_device_id bd73800_gpio_id[] = {
	{ "bd73800-gpio" },
	{ },
};
MODULE_DEVICE_TABLE(platform, bd73800_gpio_id);

static struct platform_driver gpo_bd73800_driver = {
	.driver = {
		.name = "bd73800-gpio",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = gpo_bd73800_probe,
	.id_table = bd73800_gpio_id,
};
module_platform_driver(gpo_bd73800_driver);

MODULE_AUTHOR("Matti Vaittinen <mazziesaccount@gmail.com>");
MODULE_DESCRIPTION("GPIO interface for BD73800");
MODULE_LICENSE("GPL");
