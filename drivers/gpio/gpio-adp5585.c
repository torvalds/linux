// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices ADP5585 GPIO driver
 *
 * Copyright 2022 NXP
 * Copyright 2024 Ideas on Board Oy
 * Copyright 2025 Analog Devices, Inc.
 */

#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/mfd/adp5585.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/types.h>

/*
 * Bank 0 covers pins "GPIO 1/R0" to "GPIO 6/R5", numbered 0 to 5 by the
 * driver, and bank 1 covers pins "GPIO 7/C0" to "GPIO 11/C4", numbered 6 to
 * 10. Some variants of the ADP5585 don't support "GPIO 6/R5". As the driver
 * uses identical GPIO numbering for all variants to avoid confusion, GPIO 5 is
 * marked as reserved in the device tree for variants that don't support it.
 */
#define ADP5585_BANK(n)			((n) >= 6 ? 1 : 0)
#define ADP5585_BIT(n)			((n) >= 6 ? BIT((n) - 6) : BIT(n))

/*
 * Bank 0 covers pins "GPIO 1/R0" to "GPIO 8/R7", numbered 0 to 7 by the
 * driver, bank 1 covers pins "GPIO 9/C0" to "GPIO 16/C7", numbered 8 to
 * 15 and bank 3 covers pins "GPIO 17/C8" to "GPIO 19/C10", numbered 16 to 18.
 */
#define ADP5589_BANK(n)			((n) >> 3)
#define ADP5589_BIT(n)			BIT((n) & 0x7)

struct adp5585_gpio_chip {
	int (*bank)(unsigned int off);
	int (*bit)(unsigned int off);
	unsigned int max_gpio;
	unsigned int debounce_dis_a;
	unsigned int rpull_cfg_a;
	unsigned int gpo_data_a;
	unsigned int gpo_out_a;
	unsigned int gpio_dir_a;
	unsigned int gpi_stat_a;
	bool has_bias_hole;
};

struct adp5585_gpio_dev {
	struct gpio_chip gpio_chip;
	const struct adp5585_gpio_chip *info;
	struct regmap *regmap;
};

static int adp5585_gpio_bank(unsigned int off)
{
	return ADP5585_BANK(off);
}

static int adp5585_gpio_bit(unsigned int off)
{
	return ADP5585_BIT(off);
}

static int adp5589_gpio_bank(unsigned int off)
{
	return ADP5589_BANK(off);
}

static int adp5589_gpio_bit(unsigned int off)
{
	return ADP5589_BIT(off);
}

static int adp5585_gpio_get_direction(struct gpio_chip *chip, unsigned int off)
{
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(chip);
	const struct adp5585_gpio_chip *info = adp5585_gpio->info;
	unsigned int val;

	regmap_read(adp5585_gpio->regmap, info->gpio_dir_a + info->bank(off), &val);

	return val & info->bit(off) ? GPIO_LINE_DIRECTION_OUT : GPIO_LINE_DIRECTION_IN;
}

static int adp5585_gpio_direction_input(struct gpio_chip *chip, unsigned int off)
{
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(chip);
	const struct adp5585_gpio_chip *info = adp5585_gpio->info;

	return regmap_clear_bits(adp5585_gpio->regmap, info->gpio_dir_a + info->bank(off),
				 info->bit(off));
}

static int adp5585_gpio_direction_output(struct gpio_chip *chip, unsigned int off, int val)
{
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(chip);
	const struct adp5585_gpio_chip *info = adp5585_gpio->info;
	unsigned int bank = info->bank(off);
	unsigned int bit = info->bit(off);
	int ret;

	ret = regmap_update_bits(adp5585_gpio->regmap, info->gpo_data_a + bank,
				 bit, val ? bit : 0);
	if (ret)
		return ret;

	return regmap_set_bits(adp5585_gpio->regmap, info->gpio_dir_a + bank,
			       bit);
}

static int adp5585_gpio_get_value(struct gpio_chip *chip, unsigned int off)
{
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(chip);
	const struct adp5585_gpio_chip *info = adp5585_gpio->info;
	unsigned int bank = info->bank(off);
	unsigned int bit = info->bit(off);
	unsigned int reg;
	unsigned int val;

	/*
	 * The input status register doesn't reflect the pin state when the
	 * GPIO is configured as an output. Check the direction, and read the
	 * input status from GPI_STATUS or output value from GPO_DATA_OUT
	 * accordingly.
	 *
	 * We don't need any locking, as concurrent access to the same GPIO
	 * isn't allowed by the GPIO API, so there's no risk of the
	 * .direction_input(), .direction_output() or .set() operations racing
	 * with this.
	 */
	regmap_read(adp5585_gpio->regmap, info->gpio_dir_a + bank, &val);
	reg = val & bit ? info->gpo_data_a : info->gpi_stat_a;
	regmap_read(adp5585_gpio->regmap, reg + bank, &val);

	return !!(val & bit);
}

static int adp5585_gpio_set_value(struct gpio_chip *chip, unsigned int off,
				  int val)
{
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(chip);
	const struct adp5585_gpio_chip *info = adp5585_gpio->info;
	unsigned int bit = adp5585_gpio->info->bit(off);

	return regmap_update_bits(adp5585_gpio->regmap, info->gpo_data_a + info->bank(off),
				  bit, val ? bit : 0);
}

static int adp5585_gpio_set_bias(struct adp5585_gpio_dev *adp5585_gpio,
				 unsigned int off, unsigned int bias)
{
	const struct adp5585_gpio_chip *info = adp5585_gpio->info;
	unsigned int bit, reg, mask, val;

	/*
	 * The bias configuration fields are 2 bits wide and laid down in
	 * consecutive registers ADP5585_RPULL_CONFIG_*, with a hole of 4 bits
	 * after R5.
	 */
	bit = off * 2;
	if (info->has_bias_hole)
		bit += (off > 5 ? 4 : 0);
	reg = info->rpull_cfg_a + bit / 8;
	mask = ADP5585_Rx_PULL_CFG_MASK << (bit % 8);
	val = bias << (bit % 8);

	return regmap_update_bits(adp5585_gpio->regmap, reg, mask, val);
}

static int adp5585_gpio_set_drive(struct adp5585_gpio_dev *adp5585_gpio,
				  unsigned int off, enum pin_config_param drive)
{
	const struct adp5585_gpio_chip *info = adp5585_gpio->info;
	unsigned int bit = adp5585_gpio->info->bit(off);

	return regmap_update_bits(adp5585_gpio->regmap,
				  info->gpo_out_a + info->bank(off), bit,
				  drive == PIN_CONFIG_DRIVE_OPEN_DRAIN ? bit : 0);
}

static int adp5585_gpio_set_debounce(struct adp5585_gpio_dev *adp5585_gpio,
				     unsigned int off, unsigned int debounce)
{
	const struct adp5585_gpio_chip *info = adp5585_gpio->info;
	unsigned int bit = adp5585_gpio->info->bit(off);

	return regmap_update_bits(adp5585_gpio->regmap,
				  info->debounce_dis_a + info->bank(off), bit,
				  debounce ? 0 : bit);
}

static int adp5585_gpio_set_config(struct gpio_chip *chip, unsigned int off,
				   unsigned long config)
{
	struct adp5585_gpio_dev *adp5585_gpio = gpiochip_get_data(chip);
	enum pin_config_param param = pinconf_to_config_param(config);
	u32 arg = pinconf_to_config_argument(config);

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		return adp5585_gpio_set_bias(adp5585_gpio, off,
					     ADP5585_Rx_PULL_CFG_DISABLE);

	case PIN_CONFIG_BIAS_PULL_DOWN:
		return adp5585_gpio_set_bias(adp5585_gpio, off, arg ?
					     ADP5585_Rx_PULL_CFG_PD_300K :
					     ADP5585_Rx_PULL_CFG_DISABLE);

	case PIN_CONFIG_BIAS_PULL_UP:
		return adp5585_gpio_set_bias(adp5585_gpio, off, arg ?
					     ADP5585_Rx_PULL_CFG_PU_300K :
					     ADP5585_Rx_PULL_CFG_DISABLE);

	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		return adp5585_gpio_set_drive(adp5585_gpio, off, param);

	case PIN_CONFIG_INPUT_DEBOUNCE:
		return adp5585_gpio_set_debounce(adp5585_gpio, off, arg);

	default:
		return -ENOTSUPP;
	};
}

static int adp5585_gpio_probe(struct platform_device *pdev)
{
	struct adp5585_dev *adp5585 = dev_get_drvdata(pdev->dev.parent);
	const struct platform_device_id *id = platform_get_device_id(pdev);
	struct adp5585_gpio_dev *adp5585_gpio;
	struct device *dev = &pdev->dev;
	struct gpio_chip *gc;
	int ret;

	adp5585_gpio = devm_kzalloc(dev, sizeof(*adp5585_gpio), GFP_KERNEL);
	if (!adp5585_gpio)
		return -ENOMEM;

	adp5585_gpio->regmap = adp5585->regmap;

	adp5585_gpio->info = (const struct adp5585_gpio_chip *)id->driver_data;
	if (!adp5585_gpio->info)
		return -ENODEV;

	device_set_of_node_from_dev(dev, dev->parent);

	gc = &adp5585_gpio->gpio_chip;
	gc->parent = dev;
	gc->get_direction = adp5585_gpio_get_direction;
	gc->direction_input = adp5585_gpio_direction_input;
	gc->direction_output = adp5585_gpio_direction_output;
	gc->get = adp5585_gpio_get_value;
	gc->set_rv = adp5585_gpio_set_value;
	gc->set_config = adp5585_gpio_set_config;
	gc->can_sleep = true;

	gc->base = -1;
	gc->ngpio = adp5585_gpio->info->max_gpio;
	gc->label = pdev->name;
	gc->owner = THIS_MODULE;

	ret = devm_gpiochip_add_data(dev, &adp5585_gpio->gpio_chip,
				     adp5585_gpio);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add GPIO chip\n");

	return 0;
}

static const struct adp5585_gpio_chip adp5585_gpio_chip_info = {
	.bank = adp5585_gpio_bank,
	.bit = adp5585_gpio_bit,
	.debounce_dis_a = ADP5585_DEBOUNCE_DIS_A,
	.rpull_cfg_a = ADP5585_RPULL_CONFIG_A,
	.gpo_data_a = ADP5585_GPO_DATA_OUT_A,
	.gpo_out_a = ADP5585_GPO_OUT_MODE_A,
	.gpio_dir_a = ADP5585_GPIO_DIRECTION_A,
	.gpi_stat_a = ADP5585_GPI_STATUS_A,
	.max_gpio = ADP5585_PIN_MAX,
	.has_bias_hole = true,
};

static const struct adp5585_gpio_chip adp5589_gpio_chip_info = {
	.bank = adp5589_gpio_bank,
	.bit = adp5589_gpio_bit,
	.debounce_dis_a = ADP5589_DEBOUNCE_DIS_A,
	.rpull_cfg_a = ADP5589_RPULL_CONFIG_A,
	.gpo_data_a = ADP5589_GPO_DATA_OUT_A,
	.gpo_out_a = ADP5589_GPO_OUT_MODE_A,
	.gpio_dir_a = ADP5589_GPIO_DIRECTION_A,
	.gpi_stat_a = ADP5589_GPI_STATUS_A,
	.max_gpio = ADP5589_PIN_MAX,
};

static const struct platform_device_id adp5585_gpio_id_table[] = {
	{ "adp5585-gpio", (kernel_ulong_t)&adp5585_gpio_chip_info },
	{ "adp5589-gpio", (kernel_ulong_t)&adp5589_gpio_chip_info },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(platform, adp5585_gpio_id_table);

static struct platform_driver adp5585_gpio_driver = {
	.driver	= {
		.name = "adp5585-gpio",
	},
	.probe = adp5585_gpio_probe,
	.id_table = adp5585_gpio_id_table,
};
module_platform_driver(adp5585_gpio_driver);

MODULE_AUTHOR("Haibo Chen <haibo.chen@nxp.com>");
MODULE_DESCRIPTION("GPIO ADP5585 Driver");
MODULE_LICENSE("GPL");
