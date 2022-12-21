// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Khadas System control Microcontroller
 *
 * Copyright (C) 2020 BayLibre SAS
 *
 * Author(s): Neil Armstrong <narmstrong@baylibre.com>
 */
#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/mfd/khadas-mcu.h>
#include <linux/module.h>
#include <linux/regmap.h>

static bool khadas_mcu_reg_volatile(struct device *dev, unsigned int reg)
{
	if (reg >= KHADAS_MCU_USER_DATA_0_REG &&
	    reg < KHADAS_MCU_PWR_OFF_CMD_REG)
		return true;

	switch (reg) {
	case KHADAS_MCU_PWR_OFF_CMD_REG:
	case KHADAS_MCU_PASSWD_START_REG:
	case KHADAS_MCU_CHECK_VEN_PASSWD_REG:
	case KHADAS_MCU_CHECK_USER_PASSWD_REG:
	case KHADAS_MCU_WOL_INIT_START_REG:
	case KHADAS_MCU_CMD_FAN_STATUS_CTRL_REG:
		return true;
	default:
		return false;
	}
}

static bool khadas_mcu_reg_writeable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case KHADAS_MCU_PASSWD_VEN_0_REG:
	case KHADAS_MCU_PASSWD_VEN_1_REG:
	case KHADAS_MCU_PASSWD_VEN_2_REG:
	case KHADAS_MCU_PASSWD_VEN_3_REG:
	case KHADAS_MCU_PASSWD_VEN_4_REG:
	case KHADAS_MCU_PASSWD_VEN_5_REG:
	case KHADAS_MCU_MAC_0_REG:
	case KHADAS_MCU_MAC_1_REG:
	case KHADAS_MCU_MAC_2_REG:
	case KHADAS_MCU_MAC_3_REG:
	case KHADAS_MCU_MAC_4_REG:
	case KHADAS_MCU_MAC_5_REG:
	case KHADAS_MCU_USID_0_REG:
	case KHADAS_MCU_USID_1_REG:
	case KHADAS_MCU_USID_2_REG:
	case KHADAS_MCU_USID_3_REG:
	case KHADAS_MCU_USID_4_REG:
	case KHADAS_MCU_USID_5_REG:
	case KHADAS_MCU_VERSION_0_REG:
	case KHADAS_MCU_VERSION_1_REG:
	case KHADAS_MCU_DEVICE_NO_0_REG:
	case KHADAS_MCU_DEVICE_NO_1_REG:
	case KHADAS_MCU_FACTORY_TEST_REG:
	case KHADAS_MCU_SHUTDOWN_NORMAL_STATUS_REG:
		return false;
	default:
		return true;
	}
}

static const struct regmap_config khadas_mcu_regmap_config = {
	.reg_bits	= 8,
	.reg_stride	= 1,
	.val_bits	= 8,
	.max_register	= KHADAS_MCU_CMD_FAN_STATUS_CTRL_REG,
	.volatile_reg	= khadas_mcu_reg_volatile,
	.writeable_reg	= khadas_mcu_reg_writeable,
	.cache_type	= REGCACHE_RBTREE,
};

static struct mfd_cell khadas_mcu_fan_cells[] = {
	/* VIM1/2 Rev13+ and VIM3 only */
	{ .name = "khadas-mcu-fan-ctrl", },
};

static struct mfd_cell khadas_mcu_cells[] = {
	{ .name = "khadas-mcu-user-mem", },
};

static int khadas_mcu_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct khadas_mcu *ddata;
	int ret;

	ddata = devm_kzalloc(dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	i2c_set_clientdata(client, ddata);

	ddata->dev = dev;

	ddata->regmap = devm_regmap_init_i2c(client, &khadas_mcu_regmap_config);
	if (IS_ERR(ddata->regmap)) {
		ret = PTR_ERR(ddata->regmap);
		dev_err(dev, "Failed to allocate register map: %d\n", ret);
		return ret;
	}

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE,
				   khadas_mcu_cells,
				   ARRAY_SIZE(khadas_mcu_cells),
				   NULL, 0, NULL);
	if (ret)
		return ret;

	if (of_find_property(dev->of_node, "#cooling-cells", NULL))
		return devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE,
					    khadas_mcu_fan_cells,
					    ARRAY_SIZE(khadas_mcu_fan_cells),
					    NULL, 0, NULL);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id khadas_mcu_of_match[] = {
	{ .compatible = "khadas,mcu", },
	{},
};
MODULE_DEVICE_TABLE(of, khadas_mcu_of_match);
#endif

static struct i2c_driver khadas_mcu_driver = {
	.driver = {
		.name = "khadas-mcu-core",
		.of_match_table = of_match_ptr(khadas_mcu_of_match),
	},
	.probe_new = khadas_mcu_probe,
};
module_i2c_driver(khadas_mcu_driver);

MODULE_DESCRIPTION("Khadas MCU core driver");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("GPL v2");
