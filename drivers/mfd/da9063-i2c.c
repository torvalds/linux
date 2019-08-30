// SPDX-License-Identifier: GPL-2.0+
/* I2C support for Dialog DA9063
 *
 * Copyright 2012 Dialog Semiconductor Ltd.
 * Copyright 2013 Philipp Zabel, Pengutronix
 *
 * Author: Krystian Garbaciak, Dialog Semiconductor
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/err.h>

#include <linux/mfd/core.h>
#include <linux/mfd/da9063/core.h>
#include <linux/mfd/da9063/registers.h>

#include <linux/of.h>
#include <linux/regulator/of_regulator.h>

static const struct regmap_range da9063_ad_readable_ranges[] = {
	regmap_reg_range(DA9063_REG_PAGE_CON, DA9063_AD_REG_SECOND_D),
	regmap_reg_range(DA9063_REG_SEQ, DA9063_REG_ID_32_31),
	regmap_reg_range(DA9063_REG_SEQ_A, DA9063_REG_AUTO3_LOW),
	regmap_reg_range(DA9063_REG_T_OFFSET, DA9063_AD_REG_GP_ID_19),
	regmap_reg_range(DA9063_REG_CHIP_ID, DA9063_REG_CHIP_VARIANT),
};

static const struct regmap_range da9063_ad_writeable_ranges[] = {
	regmap_reg_range(DA9063_REG_PAGE_CON, DA9063_REG_PAGE_CON),
	regmap_reg_range(DA9063_REG_FAULT_LOG, DA9063_REG_VSYS_MON),
	regmap_reg_range(DA9063_REG_COUNT_S, DA9063_AD_REG_ALARM_Y),
	regmap_reg_range(DA9063_REG_SEQ, DA9063_REG_ID_32_31),
	regmap_reg_range(DA9063_REG_SEQ_A, DA9063_REG_AUTO3_LOW),
	regmap_reg_range(DA9063_REG_CONFIG_I, DA9063_AD_REG_MON_REG_4),
	regmap_reg_range(DA9063_AD_REG_GP_ID_0, DA9063_AD_REG_GP_ID_19),
};

static const struct regmap_range da9063_ad_volatile_ranges[] = {
	regmap_reg_range(DA9063_REG_PAGE_CON, DA9063_REG_EVENT_D),
	regmap_reg_range(DA9063_REG_CONTROL_A, DA9063_REG_CONTROL_B),
	regmap_reg_range(DA9063_REG_CONTROL_E, DA9063_REG_CONTROL_F),
	regmap_reg_range(DA9063_REG_BCORE2_CONT, DA9063_REG_LDO11_CONT),
	regmap_reg_range(DA9063_REG_DVC_1, DA9063_REG_ADC_MAN),
	regmap_reg_range(DA9063_REG_ADC_RES_L, DA9063_AD_REG_SECOND_D),
	regmap_reg_range(DA9063_REG_SEQ, DA9063_REG_SEQ),
	regmap_reg_range(DA9063_REG_EN_32K, DA9063_REG_EN_32K),
	regmap_reg_range(DA9063_AD_REG_MON_REG_5, DA9063_AD_REG_MON_REG_6),
};

static const struct regmap_access_table da9063_ad_readable_table = {
	.yes_ranges = da9063_ad_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9063_ad_readable_ranges),
};

static const struct regmap_access_table da9063_ad_writeable_table = {
	.yes_ranges = da9063_ad_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9063_ad_writeable_ranges),
};

static const struct regmap_access_table da9063_ad_volatile_table = {
	.yes_ranges = da9063_ad_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9063_ad_volatile_ranges),
};

static const struct regmap_range da9063_bb_readable_ranges[] = {
	regmap_reg_range(DA9063_REG_PAGE_CON, DA9063_BB_REG_SECOND_D),
	regmap_reg_range(DA9063_REG_SEQ, DA9063_REG_ID_32_31),
	regmap_reg_range(DA9063_REG_SEQ_A, DA9063_REG_AUTO3_LOW),
	regmap_reg_range(DA9063_REG_T_OFFSET, DA9063_BB_REG_GP_ID_19),
	regmap_reg_range(DA9063_REG_CHIP_ID, DA9063_REG_CHIP_VARIANT),
};

static const struct regmap_range da9063_bb_writeable_ranges[] = {
	regmap_reg_range(DA9063_REG_PAGE_CON, DA9063_REG_PAGE_CON),
	regmap_reg_range(DA9063_REG_FAULT_LOG, DA9063_REG_VSYS_MON),
	regmap_reg_range(DA9063_REG_COUNT_S, DA9063_BB_REG_ALARM_Y),
	regmap_reg_range(DA9063_REG_SEQ, DA9063_REG_ID_32_31),
	regmap_reg_range(DA9063_REG_SEQ_A, DA9063_REG_AUTO3_LOW),
	regmap_reg_range(DA9063_REG_CONFIG_I, DA9063_BB_REG_MON_REG_4),
	regmap_reg_range(DA9063_BB_REG_GP_ID_0, DA9063_BB_REG_GP_ID_19),
};

static const struct regmap_range da9063_bb_volatile_ranges[] = {
	regmap_reg_range(DA9063_REG_PAGE_CON, DA9063_REG_EVENT_D),
	regmap_reg_range(DA9063_REG_CONTROL_A, DA9063_REG_CONTROL_B),
	regmap_reg_range(DA9063_REG_CONTROL_E, DA9063_REG_CONTROL_F),
	regmap_reg_range(DA9063_REG_BCORE2_CONT, DA9063_REG_LDO11_CONT),
	regmap_reg_range(DA9063_REG_DVC_1, DA9063_REG_ADC_MAN),
	regmap_reg_range(DA9063_REG_ADC_RES_L, DA9063_BB_REG_SECOND_D),
	regmap_reg_range(DA9063_REG_SEQ, DA9063_REG_SEQ),
	regmap_reg_range(DA9063_REG_EN_32K, DA9063_REG_EN_32K),
	regmap_reg_range(DA9063_BB_REG_MON_REG_5, DA9063_BB_REG_MON_REG_6),
};

static const struct regmap_access_table da9063_bb_readable_table = {
	.yes_ranges = da9063_bb_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9063_bb_readable_ranges),
};

static const struct regmap_access_table da9063_bb_writeable_table = {
	.yes_ranges = da9063_bb_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9063_bb_writeable_ranges),
};

static const struct regmap_access_table da9063_bb_volatile_table = {
	.yes_ranges = da9063_bb_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9063_bb_volatile_ranges),
};

static const struct regmap_range da9063l_bb_readable_ranges[] = {
	regmap_reg_range(DA9063_REG_PAGE_CON, DA9063_REG_MON_A10_RES),
	regmap_reg_range(DA9063_REG_SEQ, DA9063_REG_ID_32_31),
	regmap_reg_range(DA9063_REG_SEQ_A, DA9063_REG_AUTO3_LOW),
	regmap_reg_range(DA9063_REG_T_OFFSET, DA9063_BB_REG_GP_ID_19),
	regmap_reg_range(DA9063_REG_CHIP_ID, DA9063_REG_CHIP_VARIANT),
};

static const struct regmap_range da9063l_bb_writeable_ranges[] = {
	regmap_reg_range(DA9063_REG_PAGE_CON, DA9063_REG_PAGE_CON),
	regmap_reg_range(DA9063_REG_FAULT_LOG, DA9063_REG_VSYS_MON),
	regmap_reg_range(DA9063_REG_SEQ, DA9063_REG_ID_32_31),
	regmap_reg_range(DA9063_REG_SEQ_A, DA9063_REG_AUTO3_LOW),
	regmap_reg_range(DA9063_REG_CONFIG_I, DA9063_BB_REG_MON_REG_4),
	regmap_reg_range(DA9063_BB_REG_GP_ID_0, DA9063_BB_REG_GP_ID_19),
};

static const struct regmap_range da9063l_bb_volatile_ranges[] = {
	regmap_reg_range(DA9063_REG_PAGE_CON, DA9063_REG_EVENT_D),
	regmap_reg_range(DA9063_REG_CONTROL_A, DA9063_REG_CONTROL_B),
	regmap_reg_range(DA9063_REG_CONTROL_E, DA9063_REG_CONTROL_F),
	regmap_reg_range(DA9063_REG_BCORE2_CONT, DA9063_REG_LDO11_CONT),
	regmap_reg_range(DA9063_REG_DVC_1, DA9063_REG_ADC_MAN),
	regmap_reg_range(DA9063_REG_ADC_RES_L, DA9063_REG_MON_A10_RES),
	regmap_reg_range(DA9063_REG_SEQ, DA9063_REG_SEQ),
	regmap_reg_range(DA9063_REG_EN_32K, DA9063_REG_EN_32K),
	regmap_reg_range(DA9063_BB_REG_MON_REG_5, DA9063_BB_REG_MON_REG_6),
};

static const struct regmap_access_table da9063l_bb_readable_table = {
	.yes_ranges = da9063l_bb_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9063l_bb_readable_ranges),
};

static const struct regmap_access_table da9063l_bb_writeable_table = {
	.yes_ranges = da9063l_bb_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9063l_bb_writeable_ranges),
};

static const struct regmap_access_table da9063l_bb_volatile_table = {
	.yes_ranges = da9063l_bb_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9063l_bb_volatile_ranges),
};

static const struct regmap_range_cfg da9063_range_cfg[] = {
	{
		.range_min = DA9063_REG_PAGE_CON,
		.range_max = DA9063_REG_CHIP_VARIANT,
		.selector_reg = DA9063_REG_PAGE_CON,
		.selector_mask = 1 << DA9063_I2C_PAGE_SEL_SHIFT,
		.selector_shift = DA9063_I2C_PAGE_SEL_SHIFT,
		.window_start = 0,
		.window_len = 256,
	}
};

static struct regmap_config da9063_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.ranges = da9063_range_cfg,
	.num_ranges = ARRAY_SIZE(da9063_range_cfg),
	.max_register = DA9063_REG_CHIP_VARIANT,

	.cache_type = REGCACHE_RBTREE,
};

static const struct of_device_id da9063_dt_ids[] = {
	{ .compatible = "dlg,da9063", },
	{ .compatible = "dlg,da9063l", },
	{ }
};
MODULE_DEVICE_TABLE(of, da9063_dt_ids);
static int da9063_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct da9063 *da9063;
	int ret;

	da9063 = devm_kzalloc(&i2c->dev, sizeof(struct da9063), GFP_KERNEL);
	if (da9063 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, da9063);
	da9063->dev = &i2c->dev;
	da9063->chip_irq = i2c->irq;
	da9063->type = id->driver_data;

	if (da9063->variant_code == PMIC_DA9063_AD) {
		da9063_regmap_config.rd_table = &da9063_ad_readable_table;
		da9063_regmap_config.wr_table = &da9063_ad_writeable_table;
		da9063_regmap_config.volatile_table = &da9063_ad_volatile_table;
	} else if (da9063->type == PMIC_TYPE_DA9063L) {
		da9063_regmap_config.rd_table = &da9063l_bb_readable_table;
		da9063_regmap_config.wr_table = &da9063l_bb_writeable_table;
		da9063_regmap_config.volatile_table = &da9063l_bb_volatile_table;
	} else {
		da9063_regmap_config.rd_table = &da9063_bb_readable_table;
		da9063_regmap_config.wr_table = &da9063_bb_writeable_table;
		da9063_regmap_config.volatile_table = &da9063_bb_volatile_table;
	}

	da9063->regmap = devm_regmap_init_i2c(i2c, &da9063_regmap_config);
	if (IS_ERR(da9063->regmap)) {
		ret = PTR_ERR(da9063->regmap);
		dev_err(da9063->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	return da9063_device_init(da9063, i2c->irq);
}

static const struct i2c_device_id da9063_i2c_id[] = {
	{ "da9063", PMIC_TYPE_DA9063 },
	{ "da9063l", PMIC_TYPE_DA9063L },
	{},
};
MODULE_DEVICE_TABLE(i2c, da9063_i2c_id);

static struct i2c_driver da9063_i2c_driver = {
	.driver = {
		.name = "da9063",
		.of_match_table = of_match_ptr(da9063_dt_ids),
	},
	.probe    = da9063_i2c_probe,
	.id_table = da9063_i2c_id,
};

module_i2c_driver(da9063_i2c_driver);
