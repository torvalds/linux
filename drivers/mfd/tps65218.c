/*
 * Driver for TPS65218 Integrated power management chipsets
 *
 * Copyright (C) 2014 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether expressed or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License version 2 for more details.
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
#include <linux/of_device.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>

#include <linux/mfd/core.h>
#include <linux/mfd/tps65218.h>

#define TPS65218_PASSWORD_REGS_UNLOCK   0x7D

/**
 * tps65218_reg_read: Read a single tps65218 register.
 *
 * @tps: Device to read from.
 * @reg: Register to read.
 * @val: Contians the value
 */
int tps65218_reg_read(struct tps65218 *tps, unsigned int reg,
			unsigned int *val)
{
	return regmap_read(tps->regmap, reg, val);
}
EXPORT_SYMBOL_GPL(tps65218_reg_read);

/**
 * tps65218_reg_write: Write a single tps65218 register.
 *
 * @tps65218: Device to write to.
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
 * @tps65218: Device to write to.
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

	ret = tps65218_reg_read(tps, reg, &data);
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

static struct regmap_config tps65218_regmap_config = {
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

static int tps65218_probe(struct i2c_client *client,
				const struct i2c_device_id *ids)
{
	struct tps65218 *tps;
	const struct of_device_id *match;
	int ret;

	match = of_match_device(of_tps65218_match_table, &client->dev);
	if (!match) {
		dev_err(&client->dev,
			"Failed to find matching dt id\n");
		return -EINVAL;
	}

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

	ret = regmap_add_irq_chip(tps->regmap, tps->irq,
			IRQF_ONESHOT, 0, &tps65218_irq_chip,
			&tps->irq_data);
	if (ret < 0)
		return ret;

	ret = of_platform_populate(client->dev.of_node, NULL, NULL,
				   &client->dev);
	if (ret < 0)
		goto err_irq;

	return 0;

err_irq:
	regmap_del_irq_chip(tps->irq, tps->irq_data);

	return ret;
}

static int tps65218_remove(struct i2c_client *client)
{
	struct tps65218 *tps = i2c_get_clientdata(client);

	regmap_del_irq_chip(tps->irq, tps->irq_data);

	return 0;
}

static const struct i2c_device_id tps65218_id_table[] = {
	{ "tps65218", TPS65218 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, tps65218_id_table);

static struct i2c_driver tps65218_driver = {
	.driver		= {
		.name	= "tps65218",
		.owner	= THIS_MODULE,
		.of_match_table = of_tps65218_match_table,
	},
	.probe		= tps65218_probe,
	.remove		= tps65218_remove,
	.id_table       = tps65218_id_table,
};

module_i2c_driver(tps65218_driver);

MODULE_AUTHOR("J Keerthy <j-keerthy@ti.com>");
MODULE_DESCRIPTION("TPS65218 chip family multi-function driver");
MODULE_LICENSE("GPL v2");
