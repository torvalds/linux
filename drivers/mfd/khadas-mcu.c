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
	.cache_type	= REGCACHE_MAPLE,
};

static const struct mfd_cell khadas_mcu_fan_cells[] = {
	/* VIM1/2 Rev13+ and VIM3 only */
	MFD_CELL_NAME("khadas-mcu-fan-ctrl"),
};

static const struct mfd_cell khadas_mcu_cells[] = {
	MFD_CELL_NAME("khadas-mcu-user-mem"),
};

static bool khadas_mcu_vim4_reg_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case KHADAS_MCU_PWR_OFF_CMD_REG:
	case KHADAS_MCU_VIM4_REST_CONF_REG:
	case KHADAS_MCU_WOL_INIT_START_REG:
	case KHADAS_MCU_VIM4_LED_ON_RAM_REG:
	case KHADAS_MCU_VIM4_FAN_CTRL_REG:
	case KHADAS_MCU_VIM4_WDT_EN_REG:
	case KHADAS_MCU_VIM4_SYS_RST_REG:
		return true;
	default:
		return false;
	}
}

static bool khadas_mcu_vim4_reg_writeable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case KHADAS_MCU_VERSION_0_REG:
	case KHADAS_MCU_VERSION_1_REG:
	case KHADAS_MCU_SHUTDOWN_NORMAL_STATUS_REG:
		return false;
	default:
		return true;
	}
}

static const struct regmap_config khadas_mcu_vim4_regmap_config = {
	.reg_bits	= 8,
	.reg_stride	= 1,
	.val_bits	= 8,
	.max_register	= KHADAS_MCU_VIM4_SYS_RST_REG,
	.volatile_reg	= khadas_mcu_vim4_reg_volatile,
	.writeable_reg	= khadas_mcu_vim4_reg_writeable,
	.cache_type	= REGCACHE_MAPLE,
};

static const struct mfd_cell khadas_mcu_vim4_fan_cells[] = {
	MFD_CELL_NAME("khadas-mcu-vim4-fan"),
};

static int khadas_mcu_probe(struct i2c_client *client)
{
	const struct mfd_cell *cells, *fan_cells;
	const struct regmap_config *regmap_cfg;
	struct device *dev = &client->dev;
	int ncells, nfan_cells, ret;
	struct khadas_mcu *ddata;
	const void *mcu_variant;

	ddata = devm_kzalloc(dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	mcu_variant = i2c_get_match_data(client);
	if (!mcu_variant)
		return -ENODEV;

	switch ((uintptr_t)mcu_variant) {
	case KHADAS_MCU_GENERIC:
		regmap_cfg	= &khadas_mcu_regmap_config;
		cells		= khadas_mcu_cells;
		ncells		= ARRAY_SIZE(khadas_mcu_cells);
		fan_cells	= khadas_mcu_fan_cells;
		nfan_cells	= ARRAY_SIZE(khadas_mcu_fan_cells);
		break;
	case KHADAS_MCU_VIM4:
		regmap_cfg	= &khadas_mcu_vim4_regmap_config;
		cells		= NULL;
		ncells		= 0;
		fan_cells	= khadas_mcu_vim4_fan_cells;
		nfan_cells	= ARRAY_SIZE(khadas_mcu_vim4_fan_cells);
		break;
	default:
		return -ENODEV;
	}

	i2c_set_clientdata(client, ddata);

	ddata->dev = dev;

	ddata->regmap = devm_regmap_init_i2c(client, regmap_cfg);
	if (IS_ERR(ddata->regmap)) {
		ret = PTR_ERR(ddata->regmap);
		return dev_err_probe(dev, ret, "Failed to allocate register map\n");
	}

	if (cells && ncells) {
		ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE,
					   cells,
					   ncells,
					   NULL, 0, NULL);
		if (ret)
			return ret;
	}

	if (of_property_present(dev->of_node, "#cooling-cells"))
		return devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE,
					    fan_cells,
					    nfan_cells,
					    NULL, 0, NULL);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id khadas_mcu_of_match[] = {
	{ .compatible = "khadas,mcu", .data = (void *)KHADAS_MCU_GENERIC },
	{ .compatible = "khadas,vim4-mcu", .data = (void *)KHADAS_MCU_VIM4 },
	{},
};
MODULE_DEVICE_TABLE(of, khadas_mcu_of_match);
#endif

static struct i2c_driver khadas_mcu_driver = {
	.driver = {
		.name = "khadas-mcu-core",
		.of_match_table = of_match_ptr(khadas_mcu_of_match),
	},
	.probe = khadas_mcu_probe,
};
module_i2c_driver(khadas_mcu_driver);

MODULE_DESCRIPTION("Khadas MCU core driver");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("GPL v2");
