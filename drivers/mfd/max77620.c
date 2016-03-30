/*
 * Maxim MAX77620 MFD Driver
 *
 * Copyright (C) 2016 NVIDIA CORPORATION. All rights reserved.
 *
 * Author:
 *	Laxman Dewangan <ldewangan@nvidia.com>
 *	Chaitanya Bandi <bandik@nvidia.com>
 *	Mallikarjun Kasoju <mkasoju@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max77620.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

static struct resource gpio_resources[] = {
	DEFINE_RES_IRQ(MAX77620_IRQ_TOP_GPIO),
};

static struct resource power_resources[] = {
	DEFINE_RES_IRQ(MAX77620_IRQ_LBT_MBATLOW),
};

static struct resource rtc_resources[] = {
	DEFINE_RES_IRQ(MAX77620_IRQ_TOP_RTC),
};

static struct resource thermal_resources[] = {
	DEFINE_RES_IRQ(MAX77620_IRQ_LBT_TJALRM1),
	DEFINE_RES_IRQ(MAX77620_IRQ_LBT_TJALRM2),
};

static const struct regmap_irq max77620_top_irqs[] = {
	REGMAP_IRQ_REG(MAX77620_IRQ_TOP_GLBL, 0, MAX77620_IRQ_TOP_GLBL_MASK),
	REGMAP_IRQ_REG(MAX77620_IRQ_TOP_SD, 0, MAX77620_IRQ_TOP_SD_MASK),
	REGMAP_IRQ_REG(MAX77620_IRQ_TOP_LDO, 0, MAX77620_IRQ_TOP_LDO_MASK),
	REGMAP_IRQ_REG(MAX77620_IRQ_TOP_GPIO, 0, MAX77620_IRQ_TOP_GPIO_MASK),
	REGMAP_IRQ_REG(MAX77620_IRQ_TOP_RTC, 0, MAX77620_IRQ_TOP_RTC_MASK),
	REGMAP_IRQ_REG(MAX77620_IRQ_TOP_32K, 0, MAX77620_IRQ_TOP_32K_MASK),
	REGMAP_IRQ_REG(MAX77620_IRQ_TOP_ONOFF, 0, MAX77620_IRQ_TOP_ONOFF_MASK),
	REGMAP_IRQ_REG(MAX77620_IRQ_LBT_MBATLOW, 1, MAX77620_IRQ_LBM_MASK),
	REGMAP_IRQ_REG(MAX77620_IRQ_LBT_TJALRM1, 1, MAX77620_IRQ_TJALRM1_MASK),
	REGMAP_IRQ_REG(MAX77620_IRQ_LBT_TJALRM2, 1, MAX77620_IRQ_TJALRM2_MASK),
};

#define MAX77620_MFD_CELL_NAME(_name)				\
	{							\
		.name = (_name),				\
	}

#define MAX77620_MFD_CELL_RES(_name, _res)			\
	{							\
		.name = (_name),				\
		.resources = (_res),				\
		.num_resources = ARRAY_SIZE((_res)),		\
	}

static struct mfd_cell max77620_children[] = {
	MAX77620_MFD_CELL_NAME("max77620-pinctrl"),
	MAX77620_MFD_CELL_RES("max77620-gpio", gpio_resources),
	MAX77620_MFD_CELL_NAME("max77620-pmic"),
	MAX77620_MFD_CELL_RES("max77620-rtc", rtc_resources),
	MAX77620_MFD_CELL_RES("max77620-power", power_resources),
	MAX77620_MFD_CELL_NAME("max77620-watchdog"),
	MAX77620_MFD_CELL_NAME("max77620-clock"),
	MAX77620_MFD_CELL_RES("max77620-thermal", thermal_resources),
};

static struct mfd_cell max20024_children[] = {
	MAX77620_MFD_CELL_NAME("max20024-pinctrl"),
	MAX77620_MFD_CELL_RES("max20024-gpio", gpio_resources),
	MAX77620_MFD_CELL_NAME("max20024-pmic"),
	MAX77620_MFD_CELL_RES("max77620-rtc", rtc_resources),
	MAX77620_MFD_CELL_RES("max20024-power", power_resources),
	MAX77620_MFD_CELL_NAME("max20024-watchdog"),
	MAX77620_MFD_CELL_NAME("max20024-clock"),
};

static struct regmap_irq_chip max77620_top_irq_chip = {
	.name = "max77620-top",
	.irqs = max77620_top_irqs,
	.num_irqs = ARRAY_SIZE(max77620_top_irqs),
	.num_regs = 2,
	.status_base = MAX77620_REG_IRQTOP,
	.mask_base = MAX77620_REG_IRQTOPM,
};

static const struct regmap_range max77620_readable_ranges[] = {
	regmap_reg_range(MAX77620_REG_CNFGGLBL1, MAX77620_REG_DVSSD4),
};

static const struct regmap_access_table max77620_readable_table = {
	.yes_ranges = max77620_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(max77620_readable_ranges),
};

static const struct regmap_range max20024_readable_ranges[] = {
	regmap_reg_range(MAX77620_REG_CNFGGLBL1, MAX77620_REG_DVSSD4),
	regmap_reg_range(MAX20024_REG_MAX_ADD, MAX20024_REG_MAX_ADD),
};

static const struct regmap_access_table max20024_readable_table = {
	.yes_ranges = max20024_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(max20024_readable_ranges),
};

static const struct regmap_range max77620_writable_ranges[] = {
	regmap_reg_range(MAX77620_REG_CNFGGLBL1, MAX77620_REG_DVSSD4),
};

static const struct regmap_access_table max77620_writable_table = {
	.yes_ranges = max77620_writable_ranges,
	.n_yes_ranges = ARRAY_SIZE(max77620_writable_ranges),
};

static const struct regmap_range max77620_cacheable_ranges[] = {
	regmap_reg_range(MAX77620_REG_SD0_CFG, MAX77620_REG_LDO_CFG3),
	regmap_reg_range(MAX77620_REG_FPS_CFG0, MAX77620_REG_FPS_SD3),
};

static const struct regmap_access_table max77620_volatile_table = {
	.no_ranges = max77620_cacheable_ranges,
	.n_no_ranges = ARRAY_SIZE(max77620_cacheable_ranges),
};

static const struct regmap_config max77620_regmap_config = {
	.name = "power-slave",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX77620_REG_DVSSD4 + 1,
	.cache_type = REGCACHE_RBTREE,
	.rd_table = &max77620_readable_table,
	.wr_table = &max77620_writable_table,
	.volatile_table = &max77620_volatile_table,
};

static const struct regmap_config max20024_regmap_config = {
	.name = "power-slave",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX20024_REG_MAX_ADD + 1,
	.cache_type = REGCACHE_RBTREE,
	.rd_table = &max20024_readable_table,
	.wr_table = &max77620_writable_table,
	.volatile_table = &max77620_volatile_table,
};

static int max77620_get_fps_period_reg_value(struct max77620_chip *chip,
					     int tperiod)
{
	int base_fps_time = (chip->chip_id == MAX20024) ? 20 : 40;
	int x, i;

	for (i = 0; i < 0x7; i++) {
		x = base_fps_time * BIT(i);
		if (x >= tperiod)
			return i;
	}

	return i;
}

static int max77620_config_fps(struct max77620_chip *chip,
			       struct device_node *fps_np)
{
	struct device *dev = chip->dev;
	unsigned int mask = 0, config = 0;
	u32 param_val;
	int tperiod, fps_id;
	int ret;
	char fps_name[10];

	for (fps_id = 0; fps_id < MAX77620_FPS_COUNT; fps_id++) {
		sprintf(fps_name, "fps%d", fps_id);
		if (!strcmp(fps_np->name, fps_name))
			break;
	}

	if (fps_id == MAX77620_FPS_COUNT) {
		dev_err(dev, "FPS node name %s is not valid\n", fps_np->name);
		return -EINVAL;
	}

	ret = of_property_read_u32(fps_np, "maxim,shutdown-fps-time-period-us",
				   &param_val);
	if (!ret) {
		mask |= MAX77620_FPS_TIME_PERIOD_MASK;
		chip->shutdown_fps_period[fps_id] = min(param_val, 5120U);
		tperiod = max77620_get_fps_period_reg_value(chip,
				chip->shutdown_fps_period[fps_id]);
		config |= tperiod << MAX77620_FPS_TIME_PERIOD_SHIFT;
	}

	ret = of_property_read_u32(fps_np, "maxim,suspend-fps-time-period-us",
				   &param_val);
	if (!ret)
		chip->suspend_fps_period[fps_id] = min(param_val, 5120U);

	ret = of_property_read_u32(fps_np, "maxim,fps-event-source",
				   &param_val);
	if (!ret) {
		if (param_val > 2) {
			dev_err(dev, "FPS%d event-source invalid\n", fps_id);
			return -EINVAL;
		}
		mask |= MAX77620_FPS_EN_SRC_MASK;
		config |= param_val << MAX77620_FPS_EN_SRC_SHIFT;
		if (param_val == 2) {
			mask |= MAX77620_FPS_ENFPS_SW_MASK;
			config |= MAX77620_FPS_ENFPS_SW;
		}
	}

	if (!chip->sleep_enable && !chip->enable_global_lpm) {
		ret = of_property_read_u32(fps_np,
				"maxim,device-state-on-disabled-event",
				&param_val);
		if (!ret) {
			if (param_val == 0)
				chip->sleep_enable = true;
			else if (param_val == 1)
				chip->enable_global_lpm = true;
		}
	}

	ret = regmap_update_bits(chip->rmap, MAX77620_REG_FPS_CFG0 + fps_id,
				 mask, config);
	if (ret < 0) {
		dev_err(dev, "Failed to Reg 0x%02x update: %d\n",
			MAX77620_REG_FPS_CFG0 + fps_id, ret);
		return ret;
	}

	return 0;
}

static int max77620_initialise_fps(struct max77620_chip *chip)
{
	struct device *dev = chip->dev;
	struct device_node *fps_np, *fps_child;
	u8 config;
	int fps_id;
	int ret;

	for (fps_id = 0; fps_id < MAX77620_FPS_COUNT; fps_id++) {
		chip->shutdown_fps_period[fps_id] = -1;
		chip->suspend_fps_period[fps_id] = -1;
	}

	fps_np = of_get_child_by_name(dev->of_node, "fps");
	if (!fps_np)
		goto skip_fps;

	for_each_child_of_node(fps_np, fps_child) {
		ret = max77620_config_fps(chip, fps_child);
		if (ret < 0)
			return ret;
	}

	config = chip->enable_global_lpm ? MAX77620_ONOFFCNFG2_SLP_LPM_MSK : 0;
	ret = regmap_update_bits(chip->rmap, MAX77620_REG_ONOFFCNFG2,
				 MAX77620_ONOFFCNFG2_SLP_LPM_MSK, config);
	if (ret < 0) {
		dev_err(dev, "Failed to reg ONOFFCNFG2 update: %d\n", ret);
		return ret;
	}

skip_fps:
	/* Enable wake on EN0 pin */
	ret = regmap_update_bits(chip->rmap, MAX77620_REG_ONOFFCNFG2,
				 MAX77620_ONOFFCNFG2_WK_EN0,
				 MAX77620_ONOFFCNFG2_WK_EN0);
	if (ret < 0) {
		dev_err(dev, "Failed to reg ONOFFCNFG2 update: %d\n", ret);
		return ret;
	}

	/* For MAX20024, SLPEN will be POR reset if CLRSE is b11 */
	if ((chip->chip_id == MAX20024) && chip->sleep_enable) {
		config = MAX77620_ONOFFCNFG1_SLPEN | MAX20024_ONOFFCNFG1_CLRSE;
		ret = regmap_update_bits(chip->rmap, MAX77620_REG_ONOFFCNFG1,
					 config, config);
		if (ret < 0) {
			dev_err(dev, "Failed to reg ONOFFCNFG1 update: %d\n",
				ret);
			return ret;
		}
	}

	return 0;
}

static int max77620_read_es_version(struct max77620_chip *chip)
{
	unsigned int val;
	u8 cid_val[6];
	int i;
	int ret;

	for (i = MAX77620_REG_CID0; i <= MAX77620_REG_CID5; i++) {
		ret = regmap_read(chip->rmap, i, &val);
		if (ret < 0) {
			dev_err(chip->dev, "Failed to reg CID%d read: %d\n",
				i - MAX77620_REG_CID0, ret);
			return ret;
		}
		dev_dbg(chip->dev, "CID%d: 0x%02x\n",
			i - MAX77620_REG_CID0, val);
		cid_val[i - MAX77620_REG_CID0] = val;
	}

	/* CID4 is OTP Version  and CID5 is ES version */
	dev_info(chip->dev, "PMIC Version OTP:0x%02X and ES:0x%X\n",
		 cid_val[4], MAX77620_CID5_DIDM(cid_val[5]));

	return ret;
}

static int max77620_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	const struct regmap_config *rmap_config;
	struct max77620_chip *chip;
	struct mfd_cell *mfd_cells;
	int n_mfd_cells;
	int ret;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	i2c_set_clientdata(client, chip);
	chip->dev = &client->dev;
	chip->irq_base = -1;
	chip->chip_irq = client->irq;
	chip->chip_id = (enum max77620_chip_id)id->driver_data;

	switch (chip->chip_id) {
	case MAX77620:
		mfd_cells = max77620_children;
		n_mfd_cells = ARRAY_SIZE(max77620_children);
		rmap_config = &max77620_regmap_config;
		break;
	case MAX20024:
		mfd_cells = max20024_children;
		n_mfd_cells = ARRAY_SIZE(max20024_children);
		rmap_config = &max20024_regmap_config;
		break;
	default:
		dev_err(chip->dev, "ChipID is invalid %d\n", chip->chip_id);
		return -EINVAL;
	}

	chip->rmap = devm_regmap_init_i2c(client, rmap_config);
	if (IS_ERR(chip->rmap)) {
		ret = PTR_ERR(chip->rmap);
		dev_err(chip->dev, "Failed to regmap init: %d\n", ret);
		return ret;
	}

	ret = max77620_read_es_version(chip);
	if (ret < 0)
		return ret;

	ret = devm_regmap_add_irq_chip(chip->dev, chip->rmap, client->irq,
				       IRQF_ONESHOT | IRQF_SHARED,
				       chip->irq_base, &max77620_top_irq_chip,
				       &chip->top_irq_data);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to add regmap irq: %d\n", ret);
		return ret;
	}

	ret = max77620_initialise_fps(chip);
	if (ret < 0)
		return ret;

	ret =  mfd_add_devices(chip->dev, PLATFORM_DEVID_NONE,
			       mfd_cells, n_mfd_cells, NULL, 0,
			       regmap_irq_get_domain(chip->top_irq_data));
	if (ret < 0) {
		dev_err(chip->dev, "Failed to add sub devices: %d\n", ret);
		return ret;
	}

	return 0;
}

static int max77620_remove(struct i2c_client *client)
{
	struct max77620_chip *chip = i2c_get_clientdata(client);

	mfd_remove_devices(chip->dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int max77620_set_fps_period(struct max77620_chip *chip,
				   int fps_id, int time_period)
{
	int period = max77620_get_fps_period_reg_value(chip, time_period);
	int ret;

	ret = regmap_update_bits(chip->rmap, MAX77620_REG_FPS_CFG0 + fps_id,
				 MAX77620_FPS_TIME_PERIOD_MASK,
				 period << MAX77620_FPS_TIME_PERIOD_SHIFT);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to reg 0x%02x write: %d\n",
			MAX77620_REG_FPS_CFG0 + fps_id, ret);
		return ret;
	}

	return 0;
}

static int max77620_i2c_suspend(struct device *dev)
{
	struct max77620_chip *chip = dev_get_drvdata(dev);
	struct i2c_client *client = to_i2c_client(dev);
	unsigned int config;
	int fps;
	int ret;

	for (fps = 0; fps < MAX77620_FPS_COUNT; fps++) {
		if (chip->suspend_fps_period[fps] < 0)
			continue;

		ret = max77620_set_fps_period(chip, fps,
					      chip->suspend_fps_period[fps]);
		if (ret < 0)
			dev_err(dev, "Failed to FPS%d config: %d\n", fps, ret);
	}

	/*
	 * For MAX20024: No need to configure SLPEN on suspend as
	 * it will be configured on Init.
	 */
	if (chip->chip_id == MAX20024)
		goto out;

	config = (chip->sleep_enable) ? MAX77620_ONOFFCNFG1_SLPEN : 0;
	ret = regmap_update_bits(chip->rmap, MAX77620_REG_ONOFFCNFG1,
				 MAX77620_ONOFFCNFG1_SLPEN,
				 config);
	if (ret < 0) {
		dev_err(dev, "Failed to reg ONOFFCNFG1 update: %d\n", ret);
		return ret;
	}

	/* Disable WK_EN0 */
	ret = regmap_update_bits(chip->rmap, MAX77620_REG_ONOFFCNFG2,
				 MAX77620_ONOFFCNFG2_WK_EN0, 0);
	if (ret < 0) {
		dev_err(dev, "Failed to reg ONOFFCNFG2 update: %d\n", ret);
		return ret;
	}

out:
	disable_irq(client->irq);

	return 0;
}

static int max77620_i2c_resume(struct device *dev)
{
	struct max77620_chip *chip = dev_get_drvdata(dev);
	struct i2c_client *client = to_i2c_client(dev);
	int ret;
	int fps;

	for (fps = 0; fps < MAX77620_FPS_COUNT; fps++) {
		if (chip->shutdown_fps_period[fps] < 0)
			continue;

		ret = max77620_set_fps_period(chip, fps,
					      chip->shutdown_fps_period[fps]);
		if (ret < 0)
			dev_err(dev, "Failed to FPS%d config: %d\n", fps, ret);
	}

	/*
	 * For MAX20024: No need to configure WKEN0 on resume as
	 * it is configured on Init.
	 */
	if (chip->chip_id == MAX20024)
		goto out;

	/* Enable WK_EN0 */
	ret = regmap_update_bits(chip->rmap, MAX77620_REG_ONOFFCNFG2,
				 MAX77620_ONOFFCNFG2_WK_EN0,
		MAX77620_ONOFFCNFG2_WK_EN0);
	if (ret < 0) {
		dev_err(dev, "Failed to reg ONOFFCNFG2 WK_EN0 update: %d\n",
			ret);
		return ret;
	}

out:
	enable_irq(client->irq);

	return 0;
}
#endif

static const struct i2c_device_id max77620_id[] = {
	{"max77620", MAX77620},
	{"max20024", MAX20024},
	{},
};
MODULE_DEVICE_TABLE(i2c, max77620_id);

static const struct dev_pm_ops max77620_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(max77620_i2c_suspend, max77620_i2c_resume)
};

static struct i2c_driver max77620_driver = {
	.driver = {
		.name = "max77620",
		.pm = &max77620_pm_ops,
	},
	.probe = max77620_probe,
	.remove = max77620_remove,
	.id_table = max77620_id,
};

module_i2c_driver(max77620_driver);

MODULE_DESCRIPTION("MAX77620/MAX20024 Multi Function Device Core Driver");
MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_AUTHOR("Chaitanya Bandi <bandik@nvidia.com>");
MODULE_AUTHOR("Mallikarjun Kasoju <mkasoju@nvidia.com>");
MODULE_ALIAS("i2c:max77620");
MODULE_LICENSE("GPL v2");
