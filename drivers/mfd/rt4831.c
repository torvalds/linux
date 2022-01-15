// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2021 Richtek Technology Corp.
 *
 * Author: ChiYuan Huang <cy_huang@richtek.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/regmap.h>

#define RT4831_REG_REVISION	0x01
#define RT4831_REG_ENABLE	0x08
#define RT4831_REG_I2CPROT	0x15

#define RICHTEK_VENDOR_ID	0x03
#define RT4831_VID_MASK		GENMASK(1, 0)
#define RT4831_RESET_MASK	BIT(7)
#define RT4831_I2CSAFETMR_MASK	BIT(0)

static const struct mfd_cell rt4831_subdevs[] = {
	MFD_CELL_OF("rt4831-backlight", NULL, NULL, 0, 0, "richtek,rt4831-backlight"),
	MFD_CELL_NAME("rt4831-regulator")
};

static bool rt4831_is_accessible_reg(struct device *dev, unsigned int reg)
{
	if (reg >= RT4831_REG_REVISION && reg <= RT4831_REG_I2CPROT)
		return true;
	return false;
}

static const struct regmap_config rt4831_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RT4831_REG_I2CPROT,

	.readable_reg = rt4831_is_accessible_reg,
	.writeable_reg = rt4831_is_accessible_reg,
};

static int rt4831_probe(struct i2c_client *client)
{
	struct gpio_desc *enable_gpio;
	struct regmap *regmap;
	unsigned int chip_id;
	int ret;

	enable_gpio = devm_gpiod_get_optional(&client->dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(enable_gpio)) {
		dev_err(&client->dev, "Failed to get 'enable' GPIO\n");
		return PTR_ERR(enable_gpio);
	}

	regmap = devm_regmap_init_i2c(client, &rt4831_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to initialize regmap\n");
		return PTR_ERR(regmap);
	}

	ret = regmap_read(regmap, RT4831_REG_REVISION, &chip_id);
	if (ret) {
		dev_err(&client->dev, "Failed to get H/W revision\n");
		return ret;
	}

	if ((chip_id & RT4831_VID_MASK) != RICHTEK_VENDOR_ID) {
		dev_err(&client->dev, "Chip vendor ID 0x%02x not matched\n", chip_id);
		return -ENODEV;
	}

	/*
	 * Used to prevent the abnormal shutdown.
	 * If SCL/SDA both keep low for one second to reset HW.
	 */
	ret = regmap_update_bits(regmap, RT4831_REG_I2CPROT, RT4831_I2CSAFETMR_MASK,
				 RT4831_I2CSAFETMR_MASK);
	if (ret) {
		dev_err(&client->dev, "Failed to enable I2C safety timer\n");
		return ret;
	}

	return devm_mfd_add_devices(&client->dev, PLATFORM_DEVID_AUTO, rt4831_subdevs,
				    ARRAY_SIZE(rt4831_subdevs), NULL, 0, NULL);
}

static int rt4831_remove(struct i2c_client *client)
{
	struct regmap *regmap = dev_get_regmap(&client->dev, NULL);

	/* Disable WLED and DSV outputs */
	return regmap_update_bits(regmap, RT4831_REG_ENABLE, RT4831_RESET_MASK, RT4831_RESET_MASK);
}

static const struct of_device_id __maybe_unused rt4831_of_match[] = {
	{ .compatible = "richtek,rt4831", },
	{}
};
MODULE_DEVICE_TABLE(of, rt4831_of_match);

static struct i2c_driver rt4831_driver = {
	.driver = {
		.name = "rt4831",
		.of_match_table = rt4831_of_match,
	},
	.probe_new = rt4831_probe,
	.remove = rt4831_remove,
};
module_i2c_driver(rt4831_driver);

MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_LICENSE("GPL v2");
