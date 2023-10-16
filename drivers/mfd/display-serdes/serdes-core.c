// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * serdes-core.c  --  Device access for different serdes chips
 *
 * Copyright (c) 2023-2028 Rockchip Electronics Co. Ltd.
 *
 * Author: luowei <lw@rock-chips.com>
 */

#include "core.h"

static const struct mfd_cell serdes_bu18tl82_devs[] = {
	{
		.name = "serdes-pinctrl",
		.of_compatible = "rohm,bu18tl82-pinctrl",
	},
	{
		.name = "serdes-bridge",
		.of_compatible = "rohm,bu18tl82-bridge",
	},
};

static const struct mfd_cell serdes_bu18rl82_devs[] = {
	{
		.name = "serdes-pinctrl",
		.of_compatible = "rohm,bu18rl82-pinctrl",
	},
	{
		.name = "serdes-bridge",
		.of_compatible = "rohm,bu18rl82-bridge",
	},
};

static const struct mfd_cell serdes_max96745_devs[] = {
	{
		.name = "serdes-pinctrl",
		.of_compatible = "maxim,max96745-pinctrl",
	},
	{
		.name = "serdes-bridge",
		.of_compatible = "maxim,max96745-bridge",
	},
};

static const struct mfd_cell serdes_max96755_devs[] = {
	{
		.name = "serdes-pinctrl",
		.of_compatible = "maxim,max96755-pinctrl",
	},
	{
		.name = "serdes-bridge",
		.of_compatible = "maxim,max96755-bridge",
	},
};

static const struct mfd_cell serdes_max96789_devs[] = {
	{
		.name = "serdes-pinctrl",
		.of_compatible = "maxim,max96789-pinctrl",
	},
	{
		.name = "serdes-bridge",
		.of_compatible = "maxim,max96789-bridge",
	},
};

static const struct mfd_cell serdes_max96752_devs[] = {
	{
		.name = "serdes-pinctrl",
		.of_compatible = "maxim,max96752-pinctrl",
	},
	{
		.name = "serdes-panel",
		.of_compatible = "maxim,max96752-panel",
	},
};

static const struct mfd_cell serdes_max96772_devs[] = {
	{
		.name = "serdes-pinctrl",
		.of_compatible = "maxim,max96772-pinctrl",
	},
	{
		.name = "serdes-panel",
		.of_compatible = "maxim,max96772-panel",
	},
};

static const struct mfd_cell serdes_rkx111_devs[] = {
	{
		.name = "serdes-pinctrl",
		.of_compatible = "rockchip,rkx111-pinctrl",
	},
	{
		.name = "serdes-bridge",
		.of_compatible = "rockchip,rkx111-bridge",
	},
};

static const struct mfd_cell serdes_rkx121_devs[] = {
	{
		.name = "serdes-pinctrl",
		.of_compatible = "rockchip,rkx121-pinctrl",
	},
	{
		.name = "serdes-bridge",
		.of_compatible = "rockchip,rkx121-bridge",
	},
};

static const struct mfd_cell serdes_nca9539_devs[] = {
	{
		.name = "serdes-pinctrl",
		.of_compatible = "novo,nca9539-pinctrl",
	},
};

/**
 * serdes_reg_read: Read a single serdes register.
 *
 * @serdes: Device to read from.
 * @reg: Register to read.
 * @val: Data from register.
 */
int serdes_reg_read(struct serdes *serdes, unsigned int reg, unsigned int *val)
{
	int ret;

	ret = regmap_read(serdes->regmap, reg, val);
	SERDES_DBG_I2C("%s %s Read Reg%04x %04x\n", __func__,
		       serdes->chip_data->name, reg, *val);
	return ret;
}
EXPORT_SYMBOL_GPL(serdes_reg_read);

/**
 * serdes_bulk_read: Read multiple serdes registers
 *
 * @serdes: Device to read from
 * @reg: First register
 * @count: Number of registers
 * @buf: Buffer to fill.
 */
int serdes_bulk_read(struct serdes *serdes, unsigned int reg,
		     int count, u16 *buf)
{
	int i = 0, ret = 0;

	ret = regmap_bulk_read(serdes->regmap, reg, buf, count);
	for (i = 0; i < count; i++) {
		SERDES_DBG_I2C("%s %s %s Read Reg%04x %04x\n", __func__, dev_name(serdes->dev),
			       serdes->chip_data->name, reg + i, buf[i]);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(serdes_bulk_read);

int serdes_bulk_write(struct serdes *serdes, unsigned int reg,
		      int count, void *src)
{
	u16 *buf = src;
	int i, ret;

	WARN_ON(count <= 0);

	mutex_lock(&serdes->io_lock);
	for (i = 0; i < count; i++) {
		SERDES_DBG_I2C("%s %s %s Write Reg%04x %04x\n", __func__, dev_name(serdes->dev),
			       serdes->chip_data->name, reg, buf[i]);
		ret = regmap_write(serdes->regmap, reg, buf[i]);
		if (ret != 0) {
			mutex_unlock(&serdes->io_lock);
			return ret;
		}
	}
	mutex_unlock(&serdes->io_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(serdes_bulk_write);

/**
 * serdes_multi_reg_write: Write many serdes register.
 *
 * @serdes: Device to write to.
 * @regs: Registers to write to.
 * @num_regs: Number of registers to write.
 */
int serdes_multi_reg_write(struct serdes *serdes, const struct reg_sequence *regs,
			   int num_regs)
{
	int i, ret;

	SERDES_DBG_I2C("%s %s %s num=%d\n", __func__, dev_name(serdes->dev),
		       serdes->chip_data->name, num_regs);
	for (i = 0; i < num_regs; i++) {
		SERDES_DBG_I2C("serdes %s Write Reg%04x %04x\n",
			       serdes->chip_data->name, regs[i].reg, regs[i].def);
	}

	ret = regmap_multi_reg_write(serdes->regmap, regs, num_regs);

	return ret;
}
EXPORT_SYMBOL_GPL(serdes_multi_reg_write);

/**
 * serdes_reg_write: Write a single serdes register.
 *
 * @serdes: Device to write to.
 * @reg: Register to write to.
 * @val: Value to write.
 */
int serdes_reg_write(struct serdes *serdes, unsigned int reg,
		     unsigned int val)
{
	int ret;

	SERDES_DBG_I2C("%s %s %s Write Reg%04x %04x)\n", __func__, dev_name(serdes->dev),
		       serdes->chip_data->name, reg, val);
	ret = regmap_write(serdes->regmap, reg, val);
	if (ret != 0)
		return ret;

	return ret;
}
EXPORT_SYMBOL_GPL(serdes_reg_write);

/**
 * serdes_set_bits: Set the value of a bitfield in a serdes register
 *
 * @serdes: Device to write to.
 * @reg: Register to write to.
 * @mask: Mask of bits to set.
 * @val: Value to set (unshifted)
 */
int serdes_set_bits(struct serdes *serdes, unsigned int reg,
		    unsigned int mask, unsigned int val)
{
	int ret;

	SERDES_DBG_I2C("%s %s %s Write Reg%04x %04x) mask=%04x\n", __func__,
		       dev_name(serdes->dev), serdes->chip_data->name, reg, val, mask);
	ret = regmap_update_bits(serdes->regmap, reg, mask, val);

	return ret;
}
EXPORT_SYMBOL_GPL(serdes_set_bits);

/*
 * Instantiate the generic non-control parts of the device.
 */
int serdes_device_init(struct serdes *serdes)
{
	struct serdes_chip_data *chip_data = serdes->chip_data;
	int ret = 0;
	const struct mfd_cell *serdes_devs = NULL;
	int mfd_num = 0;

	switch (chip_data->serdes_id) {
	case ROHM_ID_BU18TL82:
		serdes_devs = serdes_bu18tl82_devs;
		mfd_num = ARRAY_SIZE(serdes_bu18tl82_devs);
		break;
	case ROHM_ID_BU18RL82:
		serdes_devs = serdes_bu18rl82_devs;
		mfd_num = ARRAY_SIZE(serdes_bu18rl82_devs);
		break;
	case MAXIM_ID_MAX96745:
		serdes_devs = serdes_max96745_devs;
		mfd_num = ARRAY_SIZE(serdes_max96745_devs);
		break;
	case MAXIM_ID_MAX96752:
		serdes_devs = serdes_max96752_devs;
		mfd_num = ARRAY_SIZE(serdes_max96752_devs);
		break;
	case MAXIM_ID_MAX96755:
		serdes_devs = serdes_max96755_devs;
		mfd_num = ARRAY_SIZE(serdes_max96755_devs);
		break;
	case MAXIM_ID_MAX96772:
		serdes_devs = serdes_max96772_devs;
		mfd_num = ARRAY_SIZE(serdes_max96772_devs);
		break;
	case MAXIM_ID_MAX96789:
		serdes_devs = serdes_max96789_devs;
		mfd_num = ARRAY_SIZE(serdes_max96789_devs);
		break;
	case ROCKCHIP_ID_RKX111:
		serdes_devs = serdes_rkx111_devs;
		mfd_num = ARRAY_SIZE(serdes_rkx111_devs);
		break;
	case ROCKCHIP_ID_RKX121:
		serdes_devs = serdes_rkx121_devs;
		mfd_num = ARRAY_SIZE(serdes_rkx121_devs);
		break;
	case NOVO_ID_NCA9539:
		serdes_devs = serdes_nca9539_devs;
		mfd_num = ARRAY_SIZE(serdes_nca9539_devs);
		break;
	default:
		dev_info(serdes->dev, "%s: unknown device\n", __func__);
		break;
	}

	ret = devm_mfd_add_devices(serdes->dev, PLATFORM_DEVID_AUTO, serdes_devs,
				   mfd_num, NULL, 0, NULL);
	if (ret != 0) {
		dev_err(serdes->dev, "Failed to add serdes children\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(serdes_device_init);

int serdes_set_pinctrl_default(struct serdes *serdes)
{
	int ret = 0;

	if ((!IS_ERR(serdes->pinctrl_node)) && (!IS_ERR(serdes->pins_default))) {
		ret = pinctrl_select_state(serdes->pinctrl_node, serdes->pins_default);
		if (ret)
			dev_err(serdes->dev, "could not set default pins\n");
		SERDES_DBG_MFD("%s: name=%s\n", __func__, dev_name(serdes->dev));
	}

	return ret;
}
EXPORT_SYMBOL_GPL(serdes_set_pinctrl_default);

int serdes_set_pinctrl_sleep(struct serdes *serdes)
{
	int ret = 0;

	if ((!IS_ERR(serdes->pinctrl_node)) && (!IS_ERR(serdes->pins_sleep))) {
		ret = pinctrl_select_state(serdes->pinctrl_node, serdes->pins_sleep);
		if (ret)
			dev_err(serdes->dev, "could not set sleep pins\n");
		SERDES_DBG_MFD("%s: name=%s\n", __func__, dev_name(serdes->dev));
	}

	return ret;
}
EXPORT_SYMBOL_GPL(serdes_set_pinctrl_sleep);

int serdes_device_suspend(struct serdes *serdes)
{
	int ret = 0;

	if (!IS_ERR(serdes->vpower)) {
		ret = regulator_disable(serdes->vpower);
		if (ret) {
			dev_err(serdes->dev, "fail to disable vpower regulator\n");
			return ret;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(serdes_device_suspend);

int serdes_device_resume(struct serdes *serdes)
{
	int ret = 0;

	if (!IS_ERR(serdes->vpower)) {
		ret = regulator_enable(serdes->vpower);
		if (ret) {
			dev_err(serdes->dev, "fail to enable vpower regulator\n");
			return ret;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(serdes_device_resume);

void serdes_device_poweroff(struct serdes *serdes)
{
	int ret = 0;

	if ((!IS_ERR(serdes->pinctrl_node)) && (!IS_ERR(serdes->pins_sleep))) {
		ret = pinctrl_select_state(serdes->pinctrl_node, serdes->pins_sleep);
		if (ret)
			dev_err(serdes->dev, "could not set sleep pins\n");
	}

	if (!IS_ERR(serdes->vpower)) {
		ret = regulator_disable(serdes->vpower);
		if (ret)
			dev_err(serdes->dev, "fail to disable vpower regulator\n");
	}

}
EXPORT_SYMBOL_GPL(serdes_device_poweroff);

int serdes_device_shutdown(struct serdes *serdes)
{
	int ret = 0;

	if (!IS_ERR(serdes->vpower)) {
		ret = regulator_disable(serdes->vpower);
		if (ret) {
			dev_err(serdes->dev, "fail to disable vpower regulator\n");
			return ret;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(serdes_device_shutdown);

MODULE_LICENSE("GPL");
