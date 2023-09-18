// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for TPS65218 Integrated power management chipsets
 *
 * Copyright (C) 2014 Texas Instruments Incorporated - https://www.ti.com/
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>

#include <linux/mfd/core.h>
#include <linux/mfd/tps65218.h>

#define TPS65218_PASSWORD_REGS_UNLOCK   0x7D

static const struct mfd_cell tps65218_cells[] = {
	{
		.name = "tps65218-pwrbutton",
		.of_compatible = "ti,tps65218-pwrbutton",
	},
	{
		.name = "tps65218-gpio",
		.of_compatible = "ti,tps65218-gpio",
	},
	{ .name = "tps65218-regulator", },
};

/**
 * tps65218_reg_write: Write a single tps65218 register.
 *
 * @tps: Device to write to.
 * @reg: Register to write to.
 * @val: Value to write.
 * @level: Password protected level
 */
int tps65218_reg_write(struct tps65218 *tps, unsigned int reg,
			unsigned int val, unsigned int level)
{
	int ret;
	unsigned int xor_reg_val;

	switch (level) {
	case TPS65218_PROTECT_NONE:
		return regmap_write(tps->regmap, reg, val);
	case TPS65218_PROTECT_L1:
		xor_reg_val = reg ^ TPS65218_PASSWORD_REGS_UNLOCK;
		ret = regmap_write(tps->regmap, TPS65218_REG_PASSWORD,
							xor_reg_val);
		if (ret < 0)
			return ret;

		return regmap_write(tps->regmap, reg, val);
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(tps65218_reg_write);

/**
 * tps65218_update_bits: Modify bits w.r.t mask, val and level.
 *
 * @tps: Device to write to.
 * @reg: Register to read-write to.
 * @mask: Mask.
 * @val: Value to write.
 * @level: Password protected level
 */
static int tps65218_update_bits(struct tps65218 *tps, unsigned int reg,
		unsigned int mask, unsigned int val, unsigned int level)
{
	int ret;
	unsigned int data;

	ret = regmap_read(tps->regmap, reg, &data);
	if (ret) {
		dev_err(tps->dev, "Read from reg 0x%x failed\n", reg);
		return ret;
	}

	data &= ~mask;
	data |= val & mask;

	mutex_lock(&tps->tps_lock);
	ret = tps65218_reg_write(tps, reg, data, level);
	if (ret)
		dev_err(tps->dev, "Write for reg 0x%x failed\n", reg);
	mutex_unlock(&tps->tps_lock);

	return ret;
}

int tps65218_set_bits(struct tps65218 *tps, unsigned int reg,
		unsigned int mask, unsigned int val, unsigned int level)
{
	return tps65218_update_bits(tps, reg, mask, val, level);
}
EXPORT_SYMBOL_GPL(tps65218_set_bits);

int tps65218_clear_bits(struct tps65218 *tps, unsigned int reg,
		unsigned int mask, unsigned int level)
{
	return tps65218_update_bits(tps, reg, mask, 0, level);
}
EXPORT_SYMBOL_GPL(tps65218_clear_bits);

static const struct regmap_range tps65218_yes_ranges[] = {
	regmap_reg_range(TPS65218_REG_INT1, TPS65218_REG_INT2),
	regmap_reg_range(TPS65218_REG_STATUS, TPS65218_REG_STATUS),
};

static const struct regmap_access_table tps65218_volatile_table = {
	.yes_ranges = tps65218_yes_ranges,
	.n_yes_ranges = ARRAY_SIZE(tps65218_yes_ranges),
};

static const struct regmap_config tps65218_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.volatile_table = &tps65218_volatile_table,
};

static const struct regmap_irq tps65218_irqs[] = {
	/* INT1 IRQs */
	[TPS65218_PRGC_IRQ] = {
		.mask = TPS65218_INT1_PRGC,
	},
	[TPS65218_CC_AQC_IRQ] = {
		.mask = TPS65218_INT1_CC_AQC,
	},
	[TPS65218_HOT_IRQ] = {
		.mask = TPS65218_INT1_HOT,
	},
	[TPS65218_PB_IRQ] = {
		.mask = TPS65218_INT1_PB,
	},
	[TPS65218_AC_IRQ] = {
		.mask = TPS65218_INT1_AC,
	},
	[TPS65218_VPRG_IRQ] = {
		.mask = TPS65218_INT1_VPRG,
	},
	[TPS65218_INVALID1_IRQ] = {
	},
	[TPS65218_INVALID2_IRQ] = {
	},
	/* INT2 IRQs*/
	[TPS65218_LS1_I_IRQ] = {
		.mask = TPS65218_INT2_LS1_I,
		.reg_offset = 1,
	},
	[TPS65218_LS2_I_IRQ] = {
		.mask = TPS65218_INT2_LS2_I,
		.reg_offset = 1,
	},
	[TPS65218_LS3_I_IRQ] = {
		.mask = TPS65218_INT2_LS3_I,
		.reg_offset = 1,
	},
	[TPS65218_LS1_F_IRQ] = {
		.mask = TPS65218_INT2_LS1_F,
		.reg_offset = 1,
	},
	[TPS65218_LS2_F_IRQ] = {
		.mask = TPS65218_INT2_LS2_F,
		.reg_offset = 1,
	},
	[TPS65218_LS3_F_IRQ] = {
		.mask = TPS65218_INT2_LS3_F,
		.reg_offset = 1,
	},
	[TPS65218_INVALID3_IRQ] = {
	},
	[TPS65218_INVALID4_IRQ] = {
	},
};

static struct regmap_irq_chip tps65218_irq_chip = {
	.name = "tps65218",
	.irqs = tps65218_irqs,
	.num_irqs = ARRAY_SIZE(tps65218_irqs),

	.num_regs = 2,
	.mask_base = TPS65218_REG_INT_MASK1,
	.status_base = TPS65218_REG_INT1,
};

static const struct of_device_id of_tps65218_match_table[] = {
	{ .compatible = "ti,tps65218", },
	{}
};
MODULE_DEVICE_TABLE(of, of_tps65218_match_table);

static int tps65218_voltage_set_strict(struct tps65218 *tps)
{
	u32 strict;

	if (of_property_read_u32(tps->dev->of_node,
				 "ti,strict-supply-voltage-supervision",
				 &strict))
		return 0;

	if (strict != 0 && strict != 1) {
		dev_err(tps->dev,
			"Invalid ti,strict-supply-voltage-supervision value\n");
		return -EINVAL;
	}

	tps65218_update_bits(tps, TPS65218_REG_CONFIG1,
			     TPS65218_CONFIG1_STRICT,
			     strict ? TPS65218_CONFIG1_STRICT : 0,
			     TPS65218_PROTECT_L1);
	return 0;
}

static int tps65218_voltage_set_uv_hyst(struct tps65218 *tps)
{
	u32 hyst;

	if (of_property_read_u32(tps->dev->of_node,
				 "ti,under-voltage-hyst-microvolt", &hyst))
		return 0;

	if (hyst != 400000 && hyst != 200000) {
		dev_err(tps->dev,
			"Invalid ti,under-voltage-hyst-microvolt value\n");
		return -EINVAL;
	}

	tps65218_update_bits(tps, TPS65218_REG_CONFIG2,
			     TPS65218_CONFIG2_UVLOHYS,
			     hyst == 400000 ? TPS65218_CONFIG2_UVLOHYS : 0,
			     TPS65218_PROTECT_L1);
	return 0;
}

static int tps65218_voltage_set_uvlo(struct tps65218 *tps)
{
	u32 uvlo;
	int uvloval;

	if (of_property_read_u32(tps->dev->of_node,
				 "ti,under-voltage-limit-microvolt", &uvlo))
		return 0;

	switch (uvlo) {
	case 2750000:
		uvloval = TPS65218_CONFIG1_UVLO_2750000;
		break;
	case 2950000:
		uvloval = TPS65218_CONFIG1_UVLO_2950000;
		break;
	case 3250000:
		uvloval = TPS65218_CONFIG1_UVLO_3250000;
		break;
	case 3350000:
		uvloval = TPS65218_CONFIG1_UVLO_3350000;
		break;
	default:
		dev_err(tps->dev,
			"Invalid ti,under-voltage-limit-microvolt value\n");
		return -EINVAL;
	}

	tps65218_update_bits(tps, TPS65218_REG_CONFIG1,
			     TPS65218_CONFIG1_UVLO_MASK, uvloval,
			     TPS65218_PROTECT_L1);
	return 0;
}

static int tps65218_probe(struct i2c_client *client)
{
	struct tps65218 *tps;
	int ret;
	unsigned int chipid;

	tps = devm_kzalloc(&client->dev, sizeof(*tps), GFP_KERNEL);
	if (!tps)
		return -ENOMEM;

	i2c_set_clientdata(client, tps);
	tps->dev = &client->dev;
	tps->irq = client->irq;
	tps->regmap = devm_regmap_init_i2c(client, &tps65218_regmap_config);
	if (IS_ERR(tps->regmap)) {
		ret = PTR_ERR(tps->regmap);
		dev_err(tps->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	mutex_init(&tps->tps_lock);

	ret = devm_regmap_add_irq_chip(&client->dev, tps->regmap, tps->irq,
				       IRQF_ONESHOT, 0, &tps65218_irq_chip,
				       &tps->irq_data);
	if (ret < 0)
		return ret;

	ret = regmap_read(tps->regmap, TPS65218_REG_CHIPID, &chipid);
	if (ret) {
		dev_err(tps->dev, "Failed to read chipid: %d\n", ret);
		return ret;
	}

	tps->rev = chipid & TPS65218_CHIPID_REV_MASK;

	ret = tps65218_voltage_set_strict(tps);
	if (ret)
		return ret;

	ret = tps65218_voltage_set_uvlo(tps);
	if (ret)
		return ret;

	ret = tps65218_voltage_set_uv_hyst(tps);
	if (ret)
		return ret;

	ret = mfd_add_devices(tps->dev, PLATFORM_DEVID_AUTO, tps65218_cells,
			      ARRAY_SIZE(tps65218_cells), NULL, 0,
			      regmap_irq_get_domain(tps->irq_data));

	return ret;
}

static const struct i2c_device_id tps65218_id_table[] = {
	{ "tps65218", TPS65218 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, tps65218_id_table);

static struct i2c_driver tps65218_driver = {
	.driver		= {
		.name	= "tps65218",
		.of_match_table = of_tps65218_match_table,
	},
	.probe		= tps65218_probe,
	.id_table       = tps65218_id_table,
};

module_i2c_driver(tps65218_driver);

MODULE_AUTHOR("J Keerthy <j-keerthy@ti.com>");
MODULE_DESCRIPTION("TPS65218 chip family multi-function driver");
MODULE_LICENSE("GPL v2");
