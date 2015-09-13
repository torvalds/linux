/*
 * Core, IRQ and I2C device driver for DA9062 PMIC
 * Copyright (C) 2015  Dialog Semiconductor Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/i2c.h>
#include <linux/mfd/da9062/core.h>
#include <linux/mfd/da9062/registers.h>
#include <linux/regulator/of_regulator.h>

#define	DA9062_REG_EVENT_A_OFFSET	0
#define	DA9062_REG_EVENT_B_OFFSET	1
#define	DA9062_REG_EVENT_C_OFFSET	2

static struct regmap_irq da9062_irqs[] = {
	/* EVENT A */
	[DA9062_IRQ_ONKEY] = {
		.reg_offset = DA9062_REG_EVENT_A_OFFSET,
		.mask = DA9062AA_M_NONKEY_MASK,
	},
	[DA9062_IRQ_ALARM] = {
		.reg_offset = DA9062_REG_EVENT_A_OFFSET,
		.mask = DA9062AA_M_ALARM_MASK,
	},
	[DA9062_IRQ_TICK] = {
		.reg_offset = DA9062_REG_EVENT_A_OFFSET,
		.mask = DA9062AA_M_TICK_MASK,
	},
	[DA9062_IRQ_WDG_WARN] = {
		.reg_offset = DA9062_REG_EVENT_A_OFFSET,
		.mask = DA9062AA_M_WDG_WARN_MASK,
	},
	[DA9062_IRQ_SEQ_RDY] = {
		.reg_offset = DA9062_REG_EVENT_A_OFFSET,
		.mask = DA9062AA_M_SEQ_RDY_MASK,
	},
	/* EVENT B */
	[DA9062_IRQ_TEMP] = {
		.reg_offset = DA9062_REG_EVENT_B_OFFSET,
		.mask = DA9062AA_M_TEMP_MASK,
	},
	[DA9062_IRQ_LDO_LIM] = {
		.reg_offset = DA9062_REG_EVENT_B_OFFSET,
		.mask = DA9062AA_M_LDO_LIM_MASK,
	},
	[DA9062_IRQ_DVC_RDY] = {
		.reg_offset = DA9062_REG_EVENT_B_OFFSET,
		.mask = DA9062AA_M_DVC_RDY_MASK,
	},
	[DA9062_IRQ_VDD_WARN] = {
		.reg_offset = DA9062_REG_EVENT_B_OFFSET,
		.mask = DA9062AA_M_VDD_WARN_MASK,
	},
	/* EVENT C */
	[DA9062_IRQ_GPI0] = {
		.reg_offset = DA9062_REG_EVENT_C_OFFSET,
		.mask = DA9062AA_M_GPI0_MASK,
	},
	[DA9062_IRQ_GPI1] = {
		.reg_offset = DA9062_REG_EVENT_C_OFFSET,
		.mask = DA9062AA_M_GPI1_MASK,
	},
	[DA9062_IRQ_GPI2] = {
		.reg_offset = DA9062_REG_EVENT_C_OFFSET,
		.mask = DA9062AA_M_GPI2_MASK,
	},
	[DA9062_IRQ_GPI3] = {
		.reg_offset = DA9062_REG_EVENT_C_OFFSET,
		.mask = DA9062AA_M_GPI3_MASK,
	},
	[DA9062_IRQ_GPI4] = {
		.reg_offset = DA9062_REG_EVENT_C_OFFSET,
		.mask = DA9062AA_M_GPI4_MASK,
	},
};

static struct regmap_irq_chip da9062_irq_chip = {
	.name = "da9062-irq",
	.irqs = da9062_irqs,
	.num_irqs = DA9062_NUM_IRQ,
	.num_regs = 3,
	.status_base = DA9062AA_EVENT_A,
	.mask_base = DA9062AA_IRQ_MASK_A,
	.ack_base = DA9062AA_EVENT_A,
};

static struct resource da9062_core_resources[] = {
	DEFINE_RES_NAMED(DA9062_IRQ_VDD_WARN, 1, "VDD_WARN", IORESOURCE_IRQ),
};

static struct resource da9062_regulators_resources[] = {
	DEFINE_RES_NAMED(DA9062_IRQ_LDO_LIM, 1, "LDO_LIM", IORESOURCE_IRQ),
};

static struct resource da9062_thermal_resources[] = {
	DEFINE_RES_NAMED(DA9062_IRQ_TEMP, 1, "THERMAL", IORESOURCE_IRQ),
};

static struct resource da9062_wdt_resources[] = {
	DEFINE_RES_NAMED(DA9062_IRQ_WDG_WARN, 1, "WD_WARN", IORESOURCE_IRQ),
};

static struct resource da9062_rtc_resources[] = {
	DEFINE_RES_NAMED(DA9062_IRQ_ALARM, 1, "ALARM", IORESOURCE_IRQ),
	DEFINE_RES_NAMED(DA9062_IRQ_TICK, 1, "TICK", IORESOURCE_IRQ),
};

static struct resource da9062_onkey_resources[] = {
	DEFINE_RES_NAMED(DA9062_IRQ_ONKEY, 1, "ONKEY", IORESOURCE_IRQ),
};

static const struct mfd_cell da9062_devs[] = {
	{
		.name		= "da9062-core",
		.num_resources	= ARRAY_SIZE(da9062_core_resources),
		.resources	= da9062_core_resources,
	},
	{
		.name		= "da9062-regulators",
		.num_resources	= ARRAY_SIZE(da9062_regulators_resources),
		.resources	= da9062_regulators_resources,
	},
	{
		.name		= "da9062-watchdog",
		.num_resources	= ARRAY_SIZE(da9062_wdt_resources),
		.resources	= da9062_wdt_resources,
		.of_compatible  = "dlg,da9062-wdt",
	},
	{
		.name		= "da9062-thermal",
		.num_resources	= ARRAY_SIZE(da9062_thermal_resources),
		.resources	= da9062_thermal_resources,
		.of_compatible  = "dlg,da9062-thermal",
	},
	{
		.name		= "da9062-rtc",
		.num_resources	= ARRAY_SIZE(da9062_rtc_resources),
		.resources	= da9062_rtc_resources,
		.of_compatible  = "dlg,da9062-rtc",
	},
	{
		.name		= "da9062-onkey",
		.num_resources	= ARRAY_SIZE(da9062_onkey_resources),
		.resources	= da9062_onkey_resources,
		.of_compatible = "dlg,da9062-onkey",
	},
};

static int da9062_clear_fault_log(struct da9062 *chip)
{
	int ret;
	int fault_log;

	ret = regmap_read(chip->regmap, DA9062AA_FAULT_LOG, &fault_log);
	if (ret < 0)
		return ret;

	if (fault_log) {
		if (fault_log & DA9062AA_TWD_ERROR_MASK)
			dev_dbg(chip->dev, "Fault log entry detected: TWD_ERROR\n");
		if (fault_log & DA9062AA_POR_MASK)
			dev_dbg(chip->dev, "Fault log entry detected: POR\n");
		if (fault_log & DA9062AA_VDD_FAULT_MASK)
			dev_dbg(chip->dev, "Fault log entry detected: VDD_FAULT\n");
		if (fault_log & DA9062AA_VDD_START_MASK)
			dev_dbg(chip->dev, "Fault log entry detected: VDD_START\n");
		if (fault_log & DA9062AA_TEMP_CRIT_MASK)
			dev_dbg(chip->dev, "Fault log entry detected: TEMP_CRIT\n");
		if (fault_log & DA9062AA_KEY_RESET_MASK)
			dev_dbg(chip->dev, "Fault log entry detected: KEY_RESET\n");
		if (fault_log & DA9062AA_NSHUTDOWN_MASK)
			dev_dbg(chip->dev, "Fault log entry detected: NSHUTDOWN\n");
		if (fault_log & DA9062AA_WAIT_SHUT_MASK)
			dev_dbg(chip->dev, "Fault log entry detected: WAIT_SHUT\n");

		ret = regmap_write(chip->regmap, DA9062AA_FAULT_LOG,
				   fault_log);
	}

	return ret;
}

int get_device_type(struct da9062 *chip)
{
	int device_id, variant_id, variant_mrc;
	int ret;

	ret = regmap_read(chip->regmap, DA9062AA_DEVICE_ID, &device_id);
	if (ret < 0) {
		dev_err(chip->dev, "Cannot read chip ID.\n");
		return -EIO;
	}
	if (device_id != DA9062_PMIC_DEVICE_ID) {
		dev_err(chip->dev, "Invalid device ID: 0x%02x\n", device_id);
		return -ENODEV;
	}

	ret = regmap_read(chip->regmap, DA9062AA_VARIANT_ID, &variant_id);
	if (ret < 0) {
		dev_err(chip->dev, "Cannot read chip variant id.\n");
		return -EIO;
	}

	dev_info(chip->dev,
		 "Device detected (device-ID: 0x%02X, var-ID: 0x%02X)\n",
		 device_id, variant_id);

	variant_mrc = (variant_id & DA9062AA_MRC_MASK) >> DA9062AA_MRC_SHIFT;

	if (variant_mrc < DA9062_PMIC_VARIANT_MRC_AA) {
		dev_err(chip->dev,
			"Cannot support variant MRC: 0x%02X\n", variant_mrc);
		return -ENODEV;
	}

	return ret;
}

static const struct regmap_range da9062_aa_readable_ranges[] = {
	{
		.range_min = DA9062AA_PAGE_CON,
		.range_max = DA9062AA_STATUS_B,
	}, {
		.range_min = DA9062AA_STATUS_D,
		.range_max = DA9062AA_EVENT_C,
	}, {
		.range_min = DA9062AA_IRQ_MASK_A,
		.range_max = DA9062AA_IRQ_MASK_C,
	}, {
		.range_min = DA9062AA_CONTROL_A,
		.range_max = DA9062AA_GPIO_4,
	}, {
		.range_min = DA9062AA_GPIO_WKUP_MODE,
		.range_max = DA9062AA_BUCK4_CONT,
	}, {
		.range_min = DA9062AA_BUCK3_CONT,
		.range_max = DA9062AA_BUCK3_CONT,
	}, {
		.range_min = DA9062AA_LDO1_CONT,
		.range_max = DA9062AA_LDO4_CONT,
	}, {
		.range_min = DA9062AA_DVC_1,
		.range_max = DA9062AA_DVC_1,
	}, {
		.range_min = DA9062AA_COUNT_S,
		.range_max = DA9062AA_SECOND_D,
	}, {
		.range_min = DA9062AA_SEQ,
		.range_max = DA9062AA_ID_4_3,
	}, {
		.range_min = DA9062AA_ID_12_11,
		.range_max = DA9062AA_ID_16_15,
	}, {
		.range_min = DA9062AA_ID_22_21,
		.range_max = DA9062AA_ID_32_31,
	}, {
		.range_min = DA9062AA_SEQ_A,
		.range_max = DA9062AA_BUCK3_CFG,
	}, {
		.range_min = DA9062AA_VBUCK2_A,
		.range_max = DA9062AA_VBUCK4_A,
	}, {
		.range_min = DA9062AA_VBUCK3_A,
		.range_max = DA9062AA_VBUCK3_A,
	}, {
		.range_min = DA9062AA_VLDO1_A,
		.range_max = DA9062AA_VLDO4_A,
	}, {
		.range_min = DA9062AA_VBUCK2_B,
		.range_max = DA9062AA_VBUCK4_B,
	}, {
		.range_min = DA9062AA_VBUCK3_B,
		.range_max = DA9062AA_VBUCK3_B,
	}, {
		.range_min = DA9062AA_VLDO1_B,
		.range_max = DA9062AA_VLDO4_B,
	}, {
		.range_min = DA9062AA_BBAT_CONT,
		.range_max = DA9062AA_BBAT_CONT,
	}, {
		.range_min = DA9062AA_INTERFACE,
		.range_max = DA9062AA_CONFIG_E,
	}, {
		.range_min = DA9062AA_CONFIG_G,
		.range_max = DA9062AA_CONFIG_K,
	}, {
		.range_min = DA9062AA_CONFIG_M,
		.range_max = DA9062AA_CONFIG_M,
	}, {
		.range_min = DA9062AA_TRIM_CLDR,
		.range_max = DA9062AA_GP_ID_19,
	}, {
		.range_min = DA9062AA_DEVICE_ID,
		.range_max = DA9062AA_CONFIG_ID,
	},
};

static const struct regmap_range da9062_aa_writeable_ranges[] = {
	{
		.range_min = DA9062AA_PAGE_CON,
		.range_max = DA9062AA_PAGE_CON,
	}, {
		.range_min = DA9062AA_FAULT_LOG,
		.range_max = DA9062AA_EVENT_C,
	}, {
		.range_min = DA9062AA_IRQ_MASK_A,
		.range_max = DA9062AA_IRQ_MASK_C,
	}, {
		.range_min = DA9062AA_CONTROL_A,
		.range_max = DA9062AA_GPIO_4,
	}, {
		.range_min = DA9062AA_GPIO_WKUP_MODE,
		.range_max = DA9062AA_BUCK4_CONT,
	}, {
		.range_min = DA9062AA_BUCK3_CONT,
		.range_max = DA9062AA_BUCK3_CONT,
	}, {
		.range_min = DA9062AA_LDO1_CONT,
		.range_max = DA9062AA_LDO4_CONT,
	}, {
		.range_min = DA9062AA_DVC_1,
		.range_max = DA9062AA_DVC_1,
	}, {
		.range_min = DA9062AA_COUNT_S,
		.range_max = DA9062AA_ALARM_Y,
	}, {
		.range_min = DA9062AA_SEQ,
		.range_max = DA9062AA_ID_4_3,
	}, {
		.range_min = DA9062AA_ID_12_11,
		.range_max = DA9062AA_ID_16_15,
	}, {
		.range_min = DA9062AA_ID_22_21,
		.range_max = DA9062AA_ID_32_31,
	}, {
		.range_min = DA9062AA_SEQ_A,
		.range_max = DA9062AA_BUCK3_CFG,
	}, {
		.range_min = DA9062AA_VBUCK2_A,
		.range_max = DA9062AA_VBUCK4_A,
	}, {
		.range_min = DA9062AA_VBUCK3_A,
		.range_max = DA9062AA_VBUCK3_A,
	}, {
		.range_min = DA9062AA_VLDO1_A,
		.range_max = DA9062AA_VLDO4_A,
	}, {
		.range_min = DA9062AA_VBUCK2_B,
		.range_max = DA9062AA_VBUCK4_B,
	}, {
		.range_min = DA9062AA_VBUCK3_B,
		.range_max = DA9062AA_VBUCK3_B,
	}, {
		.range_min = DA9062AA_VLDO1_B,
		.range_max = DA9062AA_VLDO4_B,
	}, {
		.range_min = DA9062AA_BBAT_CONT,
		.range_max = DA9062AA_BBAT_CONT,
	}, {
		.range_min = DA9062AA_GP_ID_0,
		.range_max = DA9062AA_GP_ID_19,
	},
};

static const struct regmap_range da9062_aa_volatile_ranges[] = {
	{
		.range_min = DA9062AA_PAGE_CON,
		.range_max = DA9062AA_STATUS_B,
	}, {
		.range_min = DA9062AA_STATUS_D,
		.range_max = DA9062AA_EVENT_C,
	}, {
		.range_min = DA9062AA_CONTROL_F,
		.range_max = DA9062AA_CONTROL_F,
	}, {
		.range_min = DA9062AA_COUNT_S,
		.range_max = DA9062AA_SECOND_D,
	},
};

static const struct regmap_access_table da9062_aa_readable_table = {
	.yes_ranges = da9062_aa_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9062_aa_readable_ranges),
};

static const struct regmap_access_table da9062_aa_writeable_table = {
	.yes_ranges = da9062_aa_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9062_aa_writeable_ranges),
};

static const struct regmap_access_table da9062_aa_volatile_table = {
	.yes_ranges = da9062_aa_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9062_aa_volatile_ranges),
};

static const struct regmap_range_cfg da9062_range_cfg[] = {
	{
		.range_min = DA9062AA_PAGE_CON,
		.range_max = DA9062AA_CONFIG_ID,
		.selector_reg = DA9062AA_PAGE_CON,
		.selector_mask = 1 << DA9062_I2C_PAGE_SEL_SHIFT,
		.selector_shift = DA9062_I2C_PAGE_SEL_SHIFT,
		.window_start = 0,
		.window_len = 256,
	}
};

static struct regmap_config da9062_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.ranges = da9062_range_cfg,
	.num_ranges = ARRAY_SIZE(da9062_range_cfg),
	.max_register = DA9062AA_CONFIG_ID,
	.cache_type = REGCACHE_RBTREE,
	.rd_table = &da9062_aa_readable_table,
	.wr_table = &da9062_aa_writeable_table,
	.volatile_table = &da9062_aa_volatile_table,
};

static int da9062_i2c_probe(struct i2c_client *i2c,
	const struct i2c_device_id *id)
{
	struct da9062 *chip;
	unsigned int irq_base;
	int ret;

	chip = devm_kzalloc(&i2c->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	i2c_set_clientdata(i2c, chip);
	chip->dev = &i2c->dev;

	if (!i2c->irq) {
		dev_err(chip->dev, "No IRQ configured\n");
		return -EINVAL;
	}

	chip->regmap = devm_regmap_init_i2c(i2c, &da9062_regmap_config);
	if (IS_ERR(chip->regmap)) {
		ret = PTR_ERR(chip->regmap);
		dev_err(chip->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	ret = da9062_clear_fault_log(chip);
	if (ret < 0)
		dev_warn(chip->dev, "Cannot clear fault log\n");

	ret = get_device_type(chip);
	if (ret)
		return ret;

	ret = regmap_add_irq_chip(chip->regmap, i2c->irq,
			IRQF_TRIGGER_LOW | IRQF_ONESHOT | IRQF_SHARED,
			-1, &da9062_irq_chip,
			&chip->regmap_irq);
	if (ret) {
		dev_err(chip->dev, "Failed to request IRQ %d: %d\n",
			i2c->irq, ret);
		return ret;
	}

	irq_base = regmap_irq_chip_get_base(chip->regmap_irq);

	ret = mfd_add_devices(chip->dev, PLATFORM_DEVID_NONE, da9062_devs,
			      ARRAY_SIZE(da9062_devs), NULL, irq_base,
			      NULL);
	if (ret) {
		dev_err(chip->dev, "Cannot register child devices\n");
		regmap_del_irq_chip(i2c->irq, chip->regmap_irq);
		return ret;
	}

	return ret;
}

static int da9062_i2c_remove(struct i2c_client *i2c)
{
	struct da9062 *chip = i2c_get_clientdata(i2c);

	mfd_remove_devices(chip->dev);
	regmap_del_irq_chip(i2c->irq, chip->regmap_irq);

	return 0;
}

static const struct i2c_device_id da9062_i2c_id[] = {
	{ "da9062", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, da9062_i2c_id);

static const struct of_device_id da9062_dt_ids[] = {
	{ .compatible = "dlg,da9062", },
	{ }
};
MODULE_DEVICE_TABLE(of, da9062_dt_ids);

static struct i2c_driver da9062_i2c_driver = {
	.driver = {
		.name = "da9062",
		.of_match_table = of_match_ptr(da9062_dt_ids),
	},
	.probe    = da9062_i2c_probe,
	.remove   = da9062_i2c_remove,
	.id_table = da9062_i2c_id,
};

module_i2c_driver(da9062_i2c_driver);

MODULE_DESCRIPTION("Core device driver for Dialog DA9062");
MODULE_AUTHOR("Steve Twiss <stwiss.opensource@diasemi.com>");
MODULE_LICENSE("GPL");
