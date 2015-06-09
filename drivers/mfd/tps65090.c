/*
 * Core driver for TI TPS65090 PMIC family
 *
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.

 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.

 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tps65090.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/err.h>

#define NUM_INT_REG 2
#define TOTAL_NUM_REG 0x18

#define TPS65090_INT1_MASK_VAC_STATUS_CHANGE		1
#define TPS65090_INT1_MASK_VSYS_STATUS_CHANGE		2
#define TPS65090_INT1_MASK_BAT_STATUS_CHANGE		3
#define TPS65090_INT1_MASK_CHARGING_STATUS_CHANGE	4
#define TPS65090_INT1_MASK_CHARGING_COMPLETE		5
#define TPS65090_INT1_MASK_OVERLOAD_DCDC1		6
#define TPS65090_INT1_MASK_OVERLOAD_DCDC2		7
#define TPS65090_INT2_MASK_OVERLOAD_DCDC3		0
#define TPS65090_INT2_MASK_OVERLOAD_FET1		1
#define TPS65090_INT2_MASK_OVERLOAD_FET2		2
#define TPS65090_INT2_MASK_OVERLOAD_FET3		3
#define TPS65090_INT2_MASK_OVERLOAD_FET4		4
#define TPS65090_INT2_MASK_OVERLOAD_FET5		5
#define TPS65090_INT2_MASK_OVERLOAD_FET6		6
#define TPS65090_INT2_MASK_OVERLOAD_FET7		7

static struct resource charger_resources[] = {
	{
		.start  = TPS65090_IRQ_VAC_STATUS_CHANGE,
		.end    = TPS65090_IRQ_VAC_STATUS_CHANGE,
		.flags  = IORESOURCE_IRQ,
	}
};

enum tps65090_cells {
	PMIC = 0,
	CHARGER = 1,
};

static struct mfd_cell tps65090s[] = {
	[PMIC] = {
		.name = "tps65090-pmic",
	},
	[CHARGER] = {
		.name = "tps65090-charger",
		.num_resources = ARRAY_SIZE(charger_resources),
		.resources = &charger_resources[0],
		.of_compatible = "ti,tps65090-charger",
	},
};

static const struct regmap_irq tps65090_irqs[] = {
	/* INT1 IRQs*/
	[TPS65090_IRQ_VAC_STATUS_CHANGE] = {
		.mask = TPS65090_INT1_MASK_VAC_STATUS_CHANGE,
	},
	[TPS65090_IRQ_VSYS_STATUS_CHANGE] = {
		.mask = TPS65090_INT1_MASK_VSYS_STATUS_CHANGE,
	},
	[TPS65090_IRQ_BAT_STATUS_CHANGE] = {
		.mask = TPS65090_INT1_MASK_BAT_STATUS_CHANGE,
	},
	[TPS65090_IRQ_CHARGING_STATUS_CHANGE] = {
		.mask = TPS65090_INT1_MASK_CHARGING_STATUS_CHANGE,
	},
	[TPS65090_IRQ_CHARGING_COMPLETE] = {
		.mask = TPS65090_INT1_MASK_CHARGING_COMPLETE,
	},
	[TPS65090_IRQ_OVERLOAD_DCDC1] = {
		.mask = TPS65090_INT1_MASK_OVERLOAD_DCDC1,
	},
	[TPS65090_IRQ_OVERLOAD_DCDC2] = {
		.mask = TPS65090_INT1_MASK_OVERLOAD_DCDC2,
	},
	/* INT2 IRQs*/
	[TPS65090_IRQ_OVERLOAD_DCDC3] = {
		.reg_offset = 1,
		.mask = TPS65090_INT2_MASK_OVERLOAD_DCDC3,
	},
	[TPS65090_IRQ_OVERLOAD_FET1] = {
		.reg_offset = 1,
		.mask = TPS65090_INT2_MASK_OVERLOAD_FET1,
	},
	[TPS65090_IRQ_OVERLOAD_FET2] = {
		.reg_offset = 1,
		.mask = TPS65090_INT2_MASK_OVERLOAD_FET2,
	},
	[TPS65090_IRQ_OVERLOAD_FET3] = {
		.reg_offset = 1,
		.mask = TPS65090_INT2_MASK_OVERLOAD_FET3,
	},
	[TPS65090_IRQ_OVERLOAD_FET4] = {
		.reg_offset = 1,
		.mask = TPS65090_INT2_MASK_OVERLOAD_FET4,
	},
	[TPS65090_IRQ_OVERLOAD_FET5] = {
		.reg_offset = 1,
		.mask = TPS65090_INT2_MASK_OVERLOAD_FET5,
	},
	[TPS65090_IRQ_OVERLOAD_FET6] = {
		.reg_offset = 1,
		.mask = TPS65090_INT2_MASK_OVERLOAD_FET6,
	},
	[TPS65090_IRQ_OVERLOAD_FET7] = {
		.reg_offset = 1,
		.mask = TPS65090_INT2_MASK_OVERLOAD_FET7,
	},
};

static struct regmap_irq_chip tps65090_irq_chip = {
	.name = "tps65090",
	.irqs = tps65090_irqs,
	.num_irqs = ARRAY_SIZE(tps65090_irqs),
	.num_regs = NUM_INT_REG,
	.status_base = TPS65090_REG_INTR_STS,
	.mask_base = TPS65090_REG_INTR_MASK,
	.mask_invert = true,
};

static bool is_volatile_reg(struct device *dev, unsigned int reg)
{
	/* Nearly all registers have status bits mixed in, except a few */
	switch (reg) {
	case TPS65090_REG_INTR_MASK:
	case TPS65090_REG_INTR_MASK2:
	case TPS65090_REG_CG_CTRL0:
	case TPS65090_REG_CG_CTRL1:
	case TPS65090_REG_CG_CTRL2:
	case TPS65090_REG_CG_CTRL3:
	case TPS65090_REG_CG_CTRL4:
	case TPS65090_REG_CG_CTRL5:
		return false;
	}
	return true;
}

static const struct regmap_config tps65090_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = TOTAL_NUM_REG,
	.num_reg_defaults_raw = TOTAL_NUM_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = is_volatile_reg,
};

#ifdef CONFIG_OF
static const struct of_device_id tps65090_of_match[] = {
	{ .compatible = "ti,tps65090",},
	{},
};
MODULE_DEVICE_TABLE(of, tps65090_of_match);
#endif

static int tps65090_i2c_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct tps65090_platform_data *pdata = dev_get_platdata(&client->dev);
	int irq_base = 0;
	struct tps65090 *tps65090;
	int ret;

	if (!pdata && !client->dev.of_node) {
		dev_err(&client->dev,
			"tps65090 requires platform data or of_node\n");
		return -EINVAL;
	}

	if (pdata)
		irq_base = pdata->irq_base;

	tps65090 = devm_kzalloc(&client->dev, sizeof(*tps65090), GFP_KERNEL);
	if (!tps65090) {
		dev_err(&client->dev, "mem alloc for tps65090 failed\n");
		return -ENOMEM;
	}

	tps65090->dev = &client->dev;
	i2c_set_clientdata(client, tps65090);

	tps65090->rmap = devm_regmap_init_i2c(client, &tps65090_regmap_config);
	if (IS_ERR(tps65090->rmap)) {
		ret = PTR_ERR(tps65090->rmap);
		dev_err(&client->dev, "regmap_init failed with err: %d\n", ret);
		return ret;
	}

	if (client->irq) {
		ret = regmap_add_irq_chip(tps65090->rmap, client->irq,
					  IRQF_ONESHOT | IRQF_TRIGGER_LOW, irq_base,
					  &tps65090_irq_chip, &tps65090->irq_data);
		if (ret) {
			dev_err(&client->dev,
				"IRQ init failed with err: %d\n", ret);
			return ret;
		}
	} else {
		/* Don't tell children they have an IRQ that'll never fire */
		tps65090s[CHARGER].num_resources = 0;
	}

	ret = mfd_add_devices(tps65090->dev, -1, tps65090s,
			      ARRAY_SIZE(tps65090s), NULL,
			      0, regmap_irq_get_domain(tps65090->irq_data));
	if (ret) {
		dev_err(&client->dev, "add mfd devices failed with err: %d\n",
			ret);
		goto err_irq_exit;
	}

	return 0;

err_irq_exit:
	if (client->irq)
		regmap_del_irq_chip(client->irq, tps65090->irq_data);
	return ret;
}

static int tps65090_i2c_remove(struct i2c_client *client)
{
	struct tps65090 *tps65090 = i2c_get_clientdata(client);

	mfd_remove_devices(tps65090->dev);
	if (client->irq)
		regmap_del_irq_chip(client->irq, tps65090->irq_data);

	return 0;
}

static const struct i2c_device_id tps65090_id_table[] = {
	{ "tps65090", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, tps65090_id_table);

static struct i2c_driver tps65090_driver = {
	.driver	= {
		.name	= "tps65090",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(tps65090_of_match),
	},
	.probe		= tps65090_i2c_probe,
	.remove		= tps65090_i2c_remove,
	.id_table	= tps65090_id_table,
};

static int __init tps65090_init(void)
{
	return i2c_add_driver(&tps65090_driver);
}
subsys_initcall(tps65090_init);

static void __exit tps65090_exit(void)
{
	i2c_del_driver(&tps65090_driver);
}
module_exit(tps65090_exit);

MODULE_DESCRIPTION("TPS65090 core driver");
MODULE_AUTHOR("Venu Byravarasu <vbyravarasu@nvidia.com>");
MODULE_LICENSE("GPL v2");
