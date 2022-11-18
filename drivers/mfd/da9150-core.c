// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DA9150 Core MFD Driver
 *
 * Copyright (c) 2014 Dialog Semiconductor
 *
 * Author: Adam Thomson <Adam.Thomson.Opensource@diasemi.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/mfd/da9150/core.h>
#include <linux/mfd/da9150/registers.h>

/* Raw device access, used for QIF */
static int da9150_i2c_read_device(struct i2c_client *client, u8 addr, int count,
				  u8 *buf)
{
	struct i2c_msg xfer;
	int ret;

	/*
	 * Read is split into two transfers as device expects STOP/START rather
	 * than repeated start to carry out this kind of access.
	 */

	/* Write address */
	xfer.addr = client->addr;
	xfer.flags = 0;
	xfer.len = 1;
	xfer.buf = &addr;

	ret = i2c_transfer(client->adapter, &xfer, 1);
	if (ret != 1) {
		if (ret < 0)
			return ret;
		else
			return -EIO;
	}

	/* Read data */
	xfer.addr = client->addr;
	xfer.flags = I2C_M_RD;
	xfer.len = count;
	xfer.buf = buf;

	ret = i2c_transfer(client->adapter, &xfer, 1);
	if (ret == 1)
		return 0;
	else if (ret < 0)
		return ret;
	else
		return -EIO;
}

static int da9150_i2c_write_device(struct i2c_client *client, u8 addr,
				   int count, const u8 *buf)
{
	struct i2c_msg xfer;
	u8 *reg_data;
	int ret;

	reg_data = kzalloc(1 + count, GFP_KERNEL);
	if (!reg_data)
		return -ENOMEM;

	reg_data[0] = addr;
	memcpy(&reg_data[1], buf, count);

	/* Write address & data */
	xfer.addr = client->addr;
	xfer.flags = 0;
	xfer.len = 1 + count;
	xfer.buf = reg_data;

	ret = i2c_transfer(client->adapter, &xfer, 1);
	kfree(reg_data);
	if (ret == 1)
		return 0;
	else if (ret < 0)
		return ret;
	else
		return -EIO;
}

static bool da9150_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case DA9150_PAGE_CON:
	case DA9150_STATUS_A:
	case DA9150_STATUS_B:
	case DA9150_STATUS_C:
	case DA9150_STATUS_D:
	case DA9150_STATUS_E:
	case DA9150_STATUS_F:
	case DA9150_STATUS_G:
	case DA9150_STATUS_H:
	case DA9150_STATUS_I:
	case DA9150_STATUS_J:
	case DA9150_STATUS_K:
	case DA9150_STATUS_L:
	case DA9150_STATUS_N:
	case DA9150_FAULT_LOG_A:
	case DA9150_FAULT_LOG_B:
	case DA9150_EVENT_E:
	case DA9150_EVENT_F:
	case DA9150_EVENT_G:
	case DA9150_EVENT_H:
	case DA9150_CONTROL_B:
	case DA9150_CONTROL_C:
	case DA9150_GPADC_MAN:
	case DA9150_GPADC_RES_A:
	case DA9150_GPADC_RES_B:
	case DA9150_ADETVB_CFG_C:
	case DA9150_ADETD_STAT:
	case DA9150_ADET_CMPSTAT:
	case DA9150_ADET_CTRL_A:
	case DA9150_PPR_TCTR_B:
	case DA9150_COREBTLD_STAT_A:
	case DA9150_CORE_DATA_A:
	case DA9150_CORE_DATA_B:
	case DA9150_CORE_DATA_C:
	case DA9150_CORE_DATA_D:
	case DA9150_CORE2WIRE_STAT_A:
	case DA9150_FW_CTRL_C:
	case DA9150_FG_CTRL_B:
	case DA9150_FW_CTRL_B:
	case DA9150_GPADC_CMAN:
	case DA9150_GPADC_CRES_A:
	case DA9150_GPADC_CRES_B:
	case DA9150_CC_ICHG_RES_A:
	case DA9150_CC_ICHG_RES_B:
	case DA9150_CC_IAVG_RES_A:
	case DA9150_CC_IAVG_RES_B:
	case DA9150_TAUX_CTRL_A:
	case DA9150_TAUX_VALUE_H:
	case DA9150_TAUX_VALUE_L:
	case DA9150_TBAT_RES_A:
	case DA9150_TBAT_RES_B:
		return true;
	default:
		return false;
	}
}

static const struct regmap_range_cfg da9150_range_cfg[] = {
	{
		.range_min = DA9150_PAGE_CON,
		.range_max = DA9150_TBAT_RES_B,
		.selector_reg = DA9150_PAGE_CON,
		.selector_mask = DA9150_I2C_PAGE_MASK,
		.selector_shift = DA9150_I2C_PAGE_SHIFT,
		.window_start = 0,
		.window_len = 256,
	},
};

static const struct regmap_config da9150_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.ranges = da9150_range_cfg,
	.num_ranges = ARRAY_SIZE(da9150_range_cfg),
	.max_register = DA9150_TBAT_RES_B,

	.cache_type = REGCACHE_RBTREE,

	.volatile_reg = da9150_volatile_reg,
};

void da9150_read_qif(struct da9150 *da9150, u8 addr, int count, u8 *buf)
{
	int ret;

	ret = da9150_i2c_read_device(da9150->core_qif, addr, count, buf);
	if (ret < 0)
		dev_err(da9150->dev, "Failed to read from QIF 0x%x: %d\n",
			addr, ret);
}
EXPORT_SYMBOL_GPL(da9150_read_qif);

void da9150_write_qif(struct da9150 *da9150, u8 addr, int count, const u8 *buf)
{
	int ret;

	ret = da9150_i2c_write_device(da9150->core_qif, addr, count, buf);
	if (ret < 0)
		dev_err(da9150->dev, "Failed to write to QIF 0x%x: %d\n",
			addr, ret);
}
EXPORT_SYMBOL_GPL(da9150_write_qif);

u8 da9150_reg_read(struct da9150 *da9150, u16 reg)
{
	int val, ret;

	ret = regmap_read(da9150->regmap, reg, &val);
	if (ret)
		dev_err(da9150->dev, "Failed to read from reg 0x%x: %d\n",
			reg, ret);

	return (u8) val;
}
EXPORT_SYMBOL_GPL(da9150_reg_read);

void da9150_reg_write(struct da9150 *da9150, u16 reg, u8 val)
{
	int ret;

	ret = regmap_write(da9150->regmap, reg, val);
	if (ret)
		dev_err(da9150->dev, "Failed to write to reg 0x%x: %d\n",
			reg, ret);
}
EXPORT_SYMBOL_GPL(da9150_reg_write);

void da9150_set_bits(struct da9150 *da9150, u16 reg, u8 mask, u8 val)
{
	int ret;

	ret = regmap_update_bits(da9150->regmap, reg, mask, val);
	if (ret)
		dev_err(da9150->dev, "Failed to set bits in reg 0x%x: %d\n",
			reg, ret);
}
EXPORT_SYMBOL_GPL(da9150_set_bits);

void da9150_bulk_read(struct da9150 *da9150, u16 reg, int count, u8 *buf)
{
	int ret;

	ret = regmap_bulk_read(da9150->regmap, reg, buf, count);
	if (ret)
		dev_err(da9150->dev, "Failed to bulk read from reg 0x%x: %d\n",
			reg, ret);
}
EXPORT_SYMBOL_GPL(da9150_bulk_read);

void da9150_bulk_write(struct da9150 *da9150, u16 reg, int count, const u8 *buf)
{
	int ret;

	ret = regmap_raw_write(da9150->regmap, reg, buf, count);
	if (ret)
		dev_err(da9150->dev, "Failed to bulk write to reg 0x%x %d\n",
			reg, ret);
}
EXPORT_SYMBOL_GPL(da9150_bulk_write);

static const struct regmap_irq da9150_irqs[] = {
	[DA9150_IRQ_VBUS] = {
		.reg_offset = 0,
		.mask = DA9150_E_VBUS_MASK,
	},
	[DA9150_IRQ_CHG] = {
		.reg_offset = 0,
		.mask = DA9150_E_CHG_MASK,
	},
	[DA9150_IRQ_TCLASS] = {
		.reg_offset = 0,
		.mask = DA9150_E_TCLASS_MASK,
	},
	[DA9150_IRQ_TJUNC] = {
		.reg_offset = 0,
		.mask = DA9150_E_TJUNC_MASK,
	},
	[DA9150_IRQ_VFAULT] = {
		.reg_offset = 0,
		.mask = DA9150_E_VFAULT_MASK,
	},
	[DA9150_IRQ_CONF] = {
		.reg_offset = 1,
		.mask = DA9150_E_CONF_MASK,
	},
	[DA9150_IRQ_DAT] = {
		.reg_offset = 1,
		.mask = DA9150_E_DAT_MASK,
	},
	[DA9150_IRQ_DTYPE] = {
		.reg_offset = 1,
		.mask = DA9150_E_DTYPE_MASK,
	},
	[DA9150_IRQ_ID] = {
		.reg_offset = 1,
		.mask = DA9150_E_ID_MASK,
	},
	[DA9150_IRQ_ADP] = {
		.reg_offset = 1,
		.mask = DA9150_E_ADP_MASK,
	},
	[DA9150_IRQ_SESS_END] = {
		.reg_offset = 1,
		.mask = DA9150_E_SESS_END_MASK,
	},
	[DA9150_IRQ_SESS_VLD] = {
		.reg_offset = 1,
		.mask = DA9150_E_SESS_VLD_MASK,
	},
	[DA9150_IRQ_FG] = {
		.reg_offset = 2,
		.mask = DA9150_E_FG_MASK,
	},
	[DA9150_IRQ_GP] = {
		.reg_offset = 2,
		.mask = DA9150_E_GP_MASK,
	},
	[DA9150_IRQ_TBAT] = {
		.reg_offset = 2,
		.mask = DA9150_E_TBAT_MASK,
	},
	[DA9150_IRQ_GPIOA] = {
		.reg_offset = 2,
		.mask = DA9150_E_GPIOA_MASK,
	},
	[DA9150_IRQ_GPIOB] = {
		.reg_offset = 2,
		.mask = DA9150_E_GPIOB_MASK,
	},
	[DA9150_IRQ_GPIOC] = {
		.reg_offset = 2,
		.mask = DA9150_E_GPIOC_MASK,
	},
	[DA9150_IRQ_GPIOD] = {
		.reg_offset = 2,
		.mask = DA9150_E_GPIOD_MASK,
	},
	[DA9150_IRQ_GPADC] = {
		.reg_offset = 2,
		.mask = DA9150_E_GPADC_MASK,
	},
	[DA9150_IRQ_WKUP] = {
		.reg_offset = 3,
		.mask = DA9150_E_WKUP_MASK,
	},
};

static const struct regmap_irq_chip da9150_regmap_irq_chip = {
	.name = "da9150_irq",
	.status_base = DA9150_EVENT_E,
	.mask_base = DA9150_IRQ_MASK_E,
	.ack_base = DA9150_EVENT_E,
	.num_regs = DA9150_NUM_IRQ_REGS,
	.irqs = da9150_irqs,
	.num_irqs = ARRAY_SIZE(da9150_irqs),
};

static const struct resource da9150_gpadc_resources[] = {
	DEFINE_RES_IRQ_NAMED(DA9150_IRQ_GPADC, "GPADC"),
};

static const struct resource da9150_charger_resources[] = {
	DEFINE_RES_IRQ_NAMED(DA9150_IRQ_CHG, "CHG_STATUS"),
	DEFINE_RES_IRQ_NAMED(DA9150_IRQ_TJUNC, "CHG_TJUNC"),
	DEFINE_RES_IRQ_NAMED(DA9150_IRQ_VFAULT, "CHG_VFAULT"),
	DEFINE_RES_IRQ_NAMED(DA9150_IRQ_VBUS, "CHG_VBUS"),
};

static const struct resource da9150_fg_resources[] = {
	DEFINE_RES_IRQ_NAMED(DA9150_IRQ_FG, "FG"),
};

enum da9150_dev_idx {
	DA9150_GPADC_IDX = 0,
	DA9150_CHARGER_IDX,
	DA9150_FG_IDX,
};

static struct mfd_cell da9150_devs[] = {
	[DA9150_GPADC_IDX] = {
		.name = "da9150-gpadc",
		.of_compatible = "dlg,da9150-gpadc",
		.resources = da9150_gpadc_resources,
		.num_resources = ARRAY_SIZE(da9150_gpadc_resources),
	},
	[DA9150_CHARGER_IDX] = {
		.name = "da9150-charger",
		.of_compatible = "dlg,da9150-charger",
		.resources = da9150_charger_resources,
		.num_resources = ARRAY_SIZE(da9150_charger_resources),
	},
	[DA9150_FG_IDX] = {
		.name = "da9150-fuel-gauge",
		.of_compatible = "dlg,da9150-fuel-gauge",
		.resources = da9150_fg_resources,
		.num_resources = ARRAY_SIZE(da9150_fg_resources),
	},
};

static int da9150_probe(struct i2c_client *client)
{
	struct da9150 *da9150;
	struct da9150_pdata *pdata = dev_get_platdata(&client->dev);
	int qif_addr;
	int ret;

	da9150 = devm_kzalloc(&client->dev, sizeof(*da9150), GFP_KERNEL);
	if (!da9150)
		return -ENOMEM;

	da9150->dev = &client->dev;
	da9150->irq = client->irq;
	i2c_set_clientdata(client, da9150);

	da9150->regmap = devm_regmap_init_i2c(client, &da9150_regmap_config);
	if (IS_ERR(da9150->regmap)) {
		ret = PTR_ERR(da9150->regmap);
		dev_err(da9150->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	/* Setup secondary I2C interface for QIF access */
	qif_addr = da9150_reg_read(da9150, DA9150_CORE2WIRE_CTRL_A);
	qif_addr = (qif_addr & DA9150_CORE_BASE_ADDR_MASK) >> 1;
	qif_addr |= DA9150_QIF_I2C_ADDR_LSB;
	da9150->core_qif = i2c_new_dummy_device(client->adapter, qif_addr);
	if (IS_ERR(da9150->core_qif)) {
		dev_err(da9150->dev, "Failed to attach QIF client\n");
		return PTR_ERR(da9150->core_qif);
	}

	i2c_set_clientdata(da9150->core_qif, da9150);

	if (pdata) {
		da9150->irq_base = pdata->irq_base;

		da9150_devs[DA9150_FG_IDX].platform_data = pdata->fg_pdata;
		da9150_devs[DA9150_FG_IDX].pdata_size =
			sizeof(struct da9150_fg_pdata);
	} else {
		da9150->irq_base = -1;
	}

	ret = regmap_add_irq_chip(da9150->regmap, da9150->irq,
				  IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				  da9150->irq_base, &da9150_regmap_irq_chip,
				  &da9150->regmap_irq_data);
	if (ret) {
		dev_err(da9150->dev, "Failed to add regmap irq chip: %d\n",
			ret);
		goto regmap_irq_fail;
	}


	da9150->irq_base = regmap_irq_chip_get_base(da9150->regmap_irq_data);

	enable_irq_wake(da9150->irq);

	ret = mfd_add_devices(da9150->dev, -1, da9150_devs,
			      ARRAY_SIZE(da9150_devs), NULL,
			      da9150->irq_base, NULL);
	if (ret) {
		dev_err(da9150->dev, "Failed to add child devices: %d\n", ret);
		goto mfd_fail;
	}

	return 0;

mfd_fail:
	regmap_del_irq_chip(da9150->irq, da9150->regmap_irq_data);
regmap_irq_fail:
	i2c_unregister_device(da9150->core_qif);

	return ret;
}

static void da9150_remove(struct i2c_client *client)
{
	struct da9150 *da9150 = i2c_get_clientdata(client);

	regmap_del_irq_chip(da9150->irq, da9150->regmap_irq_data);
	mfd_remove_devices(da9150->dev);
	i2c_unregister_device(da9150->core_qif);
}

static void da9150_shutdown(struct i2c_client *client)
{
	struct da9150 *da9150 = i2c_get_clientdata(client);

	/* Make sure we have a wakup source for the device */
	da9150_set_bits(da9150, DA9150_CONFIG_D,
			DA9150_WKUP_PM_EN_MASK,
			DA9150_WKUP_PM_EN_MASK);

	/* Set device to DISABLED mode */
	da9150_set_bits(da9150, DA9150_CONTROL_C,
			DA9150_DISABLE_MASK, DA9150_DISABLE_MASK);
}

static const struct i2c_device_id da9150_i2c_id[] = {
	{ "da9150", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, da9150_i2c_id);

static const struct of_device_id da9150_of_match[] = {
	{ .compatible = "dlg,da9150", },
	{ }
};
MODULE_DEVICE_TABLE(of, da9150_of_match);

static struct i2c_driver da9150_driver = {
	.driver	= {
		.name	= "da9150",
		.of_match_table = da9150_of_match,
	},
	.probe_new	= da9150_probe,
	.remove		= da9150_remove,
	.shutdown	= da9150_shutdown,
	.id_table	= da9150_i2c_id,
};

module_i2c_driver(da9150_driver);

MODULE_DESCRIPTION("MFD Core Driver for DA9150");
MODULE_AUTHOR("Adam Thomson <Adam.Thomson.Opensource@diasemi.com>");
MODULE_LICENSE("GPL");
