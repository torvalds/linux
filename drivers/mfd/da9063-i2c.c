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

/*
 * Raw I2C access required for just accessing chip and variant info before we
 * know which device is present. The info read from the device using this
 * approach is then used to select the correct regmap tables.
 */

#define DA9063_REG_PAGE_SIZE		0x100
#define DA9063_REG_PAGED_ADDR_MASK	0xFF

enum da9063_page_sel_buf_fmt {
	DA9063_PAGE_SEL_BUF_PAGE_REG = 0,
	DA9063_PAGE_SEL_BUF_PAGE_VAL,
	DA9063_PAGE_SEL_BUF_SIZE,
};

enum da9063_paged_read_msgs {
	DA9063_PAGED_READ_MSG_PAGE_SEL = 0,
	DA9063_PAGED_READ_MSG_REG_SEL,
	DA9063_PAGED_READ_MSG_DATA,
	DA9063_PAGED_READ_MSG_CNT,
};

static int da9063_i2c_blockreg_read(struct i2c_client *client, u16 addr,
				    u8 *buf, int count)
{
	struct i2c_msg xfer[DA9063_PAGED_READ_MSG_CNT];
	u8 page_sel_buf[DA9063_PAGE_SEL_BUF_SIZE];
	u8 page_num, paged_addr;
	int ret;

	/* Determine page info based on register address */
	page_num = (addr / DA9063_REG_PAGE_SIZE);
	if (page_num > 1) {
		dev_err(&client->dev, "Invalid register address provided\n");
		return -EINVAL;
	}

	paged_addr = (addr % DA9063_REG_PAGE_SIZE) & DA9063_REG_PAGED_ADDR_MASK;
	page_sel_buf[DA9063_PAGE_SEL_BUF_PAGE_REG] = DA9063_REG_PAGE_CON;
	page_sel_buf[DA9063_PAGE_SEL_BUF_PAGE_VAL] =
		(page_num << DA9063_I2C_PAGE_SEL_SHIFT) & DA9063_REG_PAGE_MASK;

	/* Write reg address, page selection */
	xfer[DA9063_PAGED_READ_MSG_PAGE_SEL].addr = client->addr;
	xfer[DA9063_PAGED_READ_MSG_PAGE_SEL].flags = 0;
	xfer[DA9063_PAGED_READ_MSG_PAGE_SEL].len = DA9063_PAGE_SEL_BUF_SIZE;
	xfer[DA9063_PAGED_READ_MSG_PAGE_SEL].buf = page_sel_buf;

	/* Select register address */
	xfer[DA9063_PAGED_READ_MSG_REG_SEL].addr = client->addr;
	xfer[DA9063_PAGED_READ_MSG_REG_SEL].flags = 0;
	xfer[DA9063_PAGED_READ_MSG_REG_SEL].len = sizeof(paged_addr);
	xfer[DA9063_PAGED_READ_MSG_REG_SEL].buf = &paged_addr;

	/* Read data */
	xfer[DA9063_PAGED_READ_MSG_DATA].addr = client->addr;
	xfer[DA9063_PAGED_READ_MSG_DATA].flags = I2C_M_RD;
	xfer[DA9063_PAGED_READ_MSG_DATA].len = count;
	xfer[DA9063_PAGED_READ_MSG_DATA].buf = buf;

	ret = i2c_transfer(client->adapter, xfer, DA9063_PAGED_READ_MSG_CNT);
	if (ret < 0) {
		dev_err(&client->dev, "Paged block read failed: %d\n", ret);
		return ret;
	}

	if (ret != DA9063_PAGED_READ_MSG_CNT) {
		dev_err(&client->dev, "Paged block read failed to complete\n");
		return -EIO;
	}

	return 0;
}

enum {
	DA9063_DEV_ID_REG = 0,
	DA9063_VAR_ID_REG,
	DA9063_CHIP_ID_REGS,
};

static int da9063_get_device_type(struct i2c_client *i2c, struct da9063 *da9063)
{
	u8 buf[DA9063_CHIP_ID_REGS];
	int ret;

	ret = da9063_i2c_blockreg_read(i2c, DA9063_REG_DEVICE_ID, buf,
				       DA9063_CHIP_ID_REGS);
	if (ret)
		return ret;

	if (buf[DA9063_DEV_ID_REG] != PMIC_CHIP_ID_DA9063) {
		dev_err(da9063->dev,
			"Invalid chip device ID: 0x%02x\n",
			buf[DA9063_DEV_ID_REG]);
		return -ENODEV;
	}

	dev_info(da9063->dev,
		 "Device detected (chip-ID: 0x%02X, var-ID: 0x%02X)\n",
		 buf[DA9063_DEV_ID_REG], buf[DA9063_VAR_ID_REG]);

	da9063->variant_code =
		(buf[DA9063_VAR_ID_REG] & DA9063_VARIANT_ID_MRC_MASK)
		>> DA9063_VARIANT_ID_MRC_SHIFT;

	return 0;
}

/*
 * Variant specific regmap configs
 */

static const struct regmap_range da9063_ad_readable_ranges[] = {
	regmap_reg_range(DA9063_REG_PAGE_CON, DA9063_AD_REG_SECOND_D),
	regmap_reg_range(DA9063_REG_SEQ, DA9063_REG_ID_32_31),
	regmap_reg_range(DA9063_REG_SEQ_A, DA9063_REG_AUTO3_LOW),
	regmap_reg_range(DA9063_REG_T_OFFSET, DA9063_AD_REG_GP_ID_19),
	regmap_reg_range(DA9063_REG_DEVICE_ID, DA9063_REG_VARIANT_ID),
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
	regmap_reg_range(DA9063_REG_DEVICE_ID, DA9063_REG_VARIANT_ID),
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

static const struct regmap_range da9063_bb_da_volatile_ranges[] = {
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

static const struct regmap_access_table da9063_bb_da_volatile_table = {
	.yes_ranges = da9063_bb_da_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9063_bb_da_volatile_ranges),
};

static const struct regmap_range da9063l_bb_readable_ranges[] = {
	regmap_reg_range(DA9063_REG_PAGE_CON, DA9063_REG_MON_A10_RES),
	regmap_reg_range(DA9063_REG_SEQ, DA9063_REG_ID_32_31),
	regmap_reg_range(DA9063_REG_SEQ_A, DA9063_REG_AUTO3_LOW),
	regmap_reg_range(DA9063_REG_T_OFFSET, DA9063_BB_REG_GP_ID_19),
	regmap_reg_range(DA9063_REG_DEVICE_ID, DA9063_REG_VARIANT_ID),
};

static const struct regmap_range da9063l_bb_writeable_ranges[] = {
	regmap_reg_range(DA9063_REG_PAGE_CON, DA9063_REG_PAGE_CON),
	regmap_reg_range(DA9063_REG_FAULT_LOG, DA9063_REG_VSYS_MON),
	regmap_reg_range(DA9063_REG_SEQ, DA9063_REG_ID_32_31),
	regmap_reg_range(DA9063_REG_SEQ_A, DA9063_REG_AUTO3_LOW),
	regmap_reg_range(DA9063_REG_CONFIG_I, DA9063_BB_REG_MON_REG_4),
	regmap_reg_range(DA9063_BB_REG_GP_ID_0, DA9063_BB_REG_GP_ID_19),
};

static const struct regmap_range da9063l_bb_da_volatile_ranges[] = {
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

static const struct regmap_access_table da9063l_bb_da_volatile_table = {
	.yes_ranges = da9063l_bb_da_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9063l_bb_da_volatile_ranges),
};

static const struct regmap_range da9063_da_readable_ranges[] = {
	regmap_reg_range(DA9063_REG_PAGE_CON, DA9063_BB_REG_SECOND_D),
	regmap_reg_range(DA9063_REG_SEQ, DA9063_REG_ID_32_31),
	regmap_reg_range(DA9063_REG_SEQ_A, DA9063_REG_AUTO3_LOW),
	regmap_reg_range(DA9063_REG_T_OFFSET, DA9063_BB_REG_GP_ID_11),
	regmap_reg_range(DA9063_REG_DEVICE_ID, DA9063_REG_VARIANT_ID),
};

static const struct regmap_range da9063_da_writeable_ranges[] = {
	regmap_reg_range(DA9063_REG_PAGE_CON, DA9063_REG_PAGE_CON),
	regmap_reg_range(DA9063_REG_FAULT_LOG, DA9063_REG_VSYS_MON),
	regmap_reg_range(DA9063_REG_COUNT_S, DA9063_BB_REG_ALARM_Y),
	regmap_reg_range(DA9063_REG_SEQ, DA9063_REG_ID_32_31),
	regmap_reg_range(DA9063_REG_SEQ_A, DA9063_REG_AUTO3_LOW),
	regmap_reg_range(DA9063_REG_CONFIG_I, DA9063_BB_REG_MON_REG_4),
	regmap_reg_range(DA9063_BB_REG_GP_ID_0, DA9063_BB_REG_GP_ID_11),
};

static const struct regmap_access_table da9063_da_readable_table = {
	.yes_ranges = da9063_da_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9063_da_readable_ranges),
};

static const struct regmap_access_table da9063_da_writeable_table = {
	.yes_ranges = da9063_da_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9063_da_writeable_ranges),
};

static const struct regmap_range da9063l_da_readable_ranges[] = {
	regmap_reg_range(DA9063_REG_PAGE_CON, DA9063_REG_MON_A10_RES),
	regmap_reg_range(DA9063_REG_SEQ, DA9063_REG_ID_32_31),
	regmap_reg_range(DA9063_REG_SEQ_A, DA9063_REG_AUTO3_LOW),
	regmap_reg_range(DA9063_REG_T_OFFSET, DA9063_BB_REG_GP_ID_11),
	regmap_reg_range(DA9063_REG_DEVICE_ID, DA9063_REG_VARIANT_ID),
};

static const struct regmap_range da9063l_da_writeable_ranges[] = {
	regmap_reg_range(DA9063_REG_PAGE_CON, DA9063_REG_PAGE_CON),
	regmap_reg_range(DA9063_REG_FAULT_LOG, DA9063_REG_VSYS_MON),
	regmap_reg_range(DA9063_REG_SEQ, DA9063_REG_ID_32_31),
	regmap_reg_range(DA9063_REG_SEQ_A, DA9063_REG_AUTO3_LOW),
	regmap_reg_range(DA9063_REG_CONFIG_I, DA9063_BB_REG_MON_REG_4),
	regmap_reg_range(DA9063_BB_REG_GP_ID_0, DA9063_BB_REG_GP_ID_11),
};

static const struct regmap_access_table da9063l_da_readable_table = {
	.yes_ranges = da9063l_da_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9063l_da_readable_ranges),
};

static const struct regmap_access_table da9063l_da_writeable_table = {
	.yes_ranges = da9063l_da_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9063l_da_writeable_ranges),
};

static const struct regmap_range_cfg da9063_range_cfg[] = {
	{
		.range_min = DA9063_REG_PAGE_CON,
		.range_max = DA9063_REG_CONFIG_ID,
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
	.max_register = DA9063_REG_CONFIG_ID,

	.cache_type = REGCACHE_RBTREE,
};

static const struct of_device_id da9063_dt_ids[] = {
	{ .compatible = "dlg,da9063", },
	{ .compatible = "dlg,da9063l", },
	{ }
};
MODULE_DEVICE_TABLE(of, da9063_dt_ids);
static int da9063_i2c_probe(struct i2c_client *i2c)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(i2c);
	struct da9063 *da9063;
	int ret;

	da9063 = devm_kzalloc(&i2c->dev, sizeof(struct da9063), GFP_KERNEL);
	if (da9063 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, da9063);
	da9063->dev = &i2c->dev;
	da9063->chip_irq = i2c->irq;
	da9063->type = id->driver_data;

	ret = da9063_get_device_type(i2c, da9063);
	if (ret)
		return ret;

	switch (da9063->type) {
	case PMIC_TYPE_DA9063:
		switch (da9063->variant_code) {
		case PMIC_DA9063_AD:
			da9063_regmap_config.rd_table =
				&da9063_ad_readable_table;
			da9063_regmap_config.wr_table =
				&da9063_ad_writeable_table;
			da9063_regmap_config.volatile_table =
				&da9063_ad_volatile_table;
			break;
		case PMIC_DA9063_BB:
		case PMIC_DA9063_CA:
			da9063_regmap_config.rd_table =
				&da9063_bb_readable_table;
			da9063_regmap_config.wr_table =
				&da9063_bb_writeable_table;
			da9063_regmap_config.volatile_table =
				&da9063_bb_da_volatile_table;
			break;
		case PMIC_DA9063_DA:
		case PMIC_DA9063_EA:
			da9063_regmap_config.rd_table =
				&da9063_da_readable_table;
			da9063_regmap_config.wr_table =
				&da9063_da_writeable_table;
			da9063_regmap_config.volatile_table =
				&da9063_bb_da_volatile_table;
			break;
		default:
			dev_err(da9063->dev,
				"Chip variant not supported for DA9063\n");
			return -ENODEV;
		}
		break;
	case PMIC_TYPE_DA9063L:
		switch (da9063->variant_code) {
		case PMIC_DA9063_BB:
		case PMIC_DA9063_CA:
			da9063_regmap_config.rd_table =
				&da9063l_bb_readable_table;
			da9063_regmap_config.wr_table =
				&da9063l_bb_writeable_table;
			da9063_regmap_config.volatile_table =
				&da9063l_bb_da_volatile_table;
			break;
		case PMIC_DA9063_DA:
		case PMIC_DA9063_EA:
			da9063_regmap_config.rd_table =
				&da9063l_da_readable_table;
			da9063_regmap_config.wr_table =
				&da9063l_da_writeable_table;
			da9063_regmap_config.volatile_table =
				&da9063l_bb_da_volatile_table;
			break;
		default:
			dev_err(da9063->dev,
				"Chip variant not supported for DA9063L\n");
			return -ENODEV;
		}
		break;
	default:
		dev_err(da9063->dev, "Chip type not supported\n");
		return -ENODEV;
	}

	da9063->regmap = devm_regmap_init_i2c(i2c, &da9063_regmap_config);
	if (IS_ERR(da9063->regmap)) {
		ret = PTR_ERR(da9063->regmap);
		dev_err(da9063->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	/* If SMBus is not available and only I2C is possible, enter I2C mode */
	if (i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C)) {
		ret = regmap_clear_bits(da9063->regmap, DA9063_REG_CONFIG_J,
					DA9063_TWOWIRE_TO);
		if (ret < 0) {
			dev_err(da9063->dev, "Failed to set Two-Wire Bus Mode.\n");
			return ret;
		}
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
		.of_match_table = da9063_dt_ids,
	},
	.probe_new = da9063_i2c_probe,
	.id_table = da9063_i2c_id,
};

module_i2c_driver(da9063_i2c_driver);
