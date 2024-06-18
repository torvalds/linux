// SPDX-License-Identifier: GPL-2.0+
//
// max77693.c - mfd core driver for the MAX 77693
//
// Copyright (C) 2012 Samsung Electronics
// SangYoung Son <hello.son@samsung.com>
//
// This program is not provided / owned by Maxim Integrated Products.
//
// This driver is based on max8997.c

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max77693.h>
#include <linux/mfd/max77693-common.h>
#include <linux/mfd/max77693-private.h>
#include <linux/regulator/machine.h>
#include <linux/regmap.h>

#define I2C_ADDR_PMIC	(0xCC >> 1)	/* Charger, Flash LED */
#define I2C_ADDR_MUIC	(0x4A >> 1)
#define I2C_ADDR_HAPTIC	(0x90 >> 1)

static const struct mfd_cell max77693_devs[] = {
	{ .name = "max77693-pmic", },
	{
		.name = "max77693-charger",
		.of_compatible = "maxim,max77693-charger",
	},
	{
		.name = "max77693-muic",
		.of_compatible = "maxim,max77693-muic",
	},
	{
		.name = "max77693-haptic",
		.of_compatible = "maxim,max77693-haptic",
	},
	{
		.name = "max77693-led",
		.of_compatible = "maxim,max77693-led",
	},
};

static const struct regmap_config max77693_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX77693_PMIC_REG_END,
};

static const struct regmap_irq max77693_led_irqs[] = {
	{ .mask = LED_IRQ_FLED2_OPEN,  },
	{ .mask = LED_IRQ_FLED2_SHORT, },
	{ .mask = LED_IRQ_FLED1_OPEN,  },
	{ .mask = LED_IRQ_FLED1_SHORT, },
	{ .mask = LED_IRQ_MAX_FLASH,   },
};

static const struct regmap_irq_chip max77693_led_irq_chip = {
	.name			= "max77693-led",
	.status_base		= MAX77693_LED_REG_FLASH_INT,
	.mask_base		= MAX77693_LED_REG_FLASH_INT_MASK,
	.num_regs		= 1,
	.irqs			= max77693_led_irqs,
	.num_irqs		= ARRAY_SIZE(max77693_led_irqs),
};

static const struct regmap_irq max77693_topsys_irqs[] = {
	{ .mask = TOPSYS_IRQ_T120C_INT,  },
	{ .mask = TOPSYS_IRQ_T140C_INT,  },
	{ .mask = TOPSYS_IRQ_LOWSYS_INT, },
};

static const struct regmap_irq_chip max77693_topsys_irq_chip = {
	.name			= "max77693-topsys",
	.status_base		= MAX77693_PMIC_REG_TOPSYS_INT,
	.mask_base		= MAX77693_PMIC_REG_TOPSYS_INT_MASK,
	.num_regs		= 1,
	.irqs			= max77693_topsys_irqs,
	.num_irqs		= ARRAY_SIZE(max77693_topsys_irqs),
};

static const struct regmap_irq max77693_charger_irqs[] = {
	{ .mask = CHG_IRQ_BYP_I,   },
	{ .mask = CHG_IRQ_THM_I,   },
	{ .mask = CHG_IRQ_BAT_I,   },
	{ .mask = CHG_IRQ_CHG_I,   },
	{ .mask = CHG_IRQ_CHGIN_I, },
};

static const struct regmap_irq_chip max77693_charger_irq_chip = {
	.name			= "max77693-charger",
	.status_base		= MAX77693_CHG_REG_CHG_INT,
	.mask_base		= MAX77693_CHG_REG_CHG_INT_MASK,
	.num_regs		= 1,
	.irqs			= max77693_charger_irqs,
	.num_irqs		= ARRAY_SIZE(max77693_charger_irqs),
};

static const struct regmap_config max77693_regmap_muic_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX77693_MUIC_REG_END,
};

static const struct regmap_irq max77693_muic_irqs[] = {
	{ .reg_offset = 0, .mask = MUIC_IRQ_INT1_ADC,		},
	{ .reg_offset = 0, .mask = MUIC_IRQ_INT1_ADC_LOW,	},
	{ .reg_offset = 0, .mask = MUIC_IRQ_INT1_ADC_ERR,	},
	{ .reg_offset = 0, .mask = MUIC_IRQ_INT1_ADC1K,		},

	{ .reg_offset = 1, .mask = MUIC_IRQ_INT2_CHGTYP,	},
	{ .reg_offset = 1, .mask = MUIC_IRQ_INT2_CHGDETREUN,	},
	{ .reg_offset = 1, .mask = MUIC_IRQ_INT2_DCDTMR,	},
	{ .reg_offset = 1, .mask = MUIC_IRQ_INT2_DXOVP,		},
	{ .reg_offset = 1, .mask = MUIC_IRQ_INT2_VBVOLT,	},
	{ .reg_offset = 1, .mask = MUIC_IRQ_INT2_VIDRM,		},

	{ .reg_offset = 2, .mask = MUIC_IRQ_INT3_EOC,		},
	{ .reg_offset = 2, .mask = MUIC_IRQ_INT3_CGMBC,		},
	{ .reg_offset = 2, .mask = MUIC_IRQ_INT3_OVP,		},
	{ .reg_offset = 2, .mask = MUIC_IRQ_INT3_MBCCHG_ERR,	},
	{ .reg_offset = 2, .mask = MUIC_IRQ_INT3_CHG_ENABLED,	},
	{ .reg_offset = 2, .mask = MUIC_IRQ_INT3_BAT_DET,	},
};

static const struct regmap_irq_chip max77693_muic_irq_chip = {
	.name			= "max77693-muic",
	.status_base		= MAX77693_MUIC_REG_INT1,
	.unmask_base		= MAX77693_MUIC_REG_INTMASK1,
	.num_regs		= 3,
	.irqs			= max77693_muic_irqs,
	.num_irqs		= ARRAY_SIZE(max77693_muic_irqs),
};

static const struct regmap_config max77693_regmap_haptic_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX77693_HAPTIC_REG_END,
};

static int max77693_i2c_probe(struct i2c_client *i2c)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(i2c);
	struct max77693_dev *max77693;
	unsigned int reg_data;
	int ret = 0;

	max77693 = devm_kzalloc(&i2c->dev,
			sizeof(struct max77693_dev), GFP_KERNEL);
	if (max77693 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, max77693);
	max77693->dev = &i2c->dev;
	max77693->i2c = i2c;
	max77693->irq = i2c->irq;
	max77693->type = id->driver_data;

	max77693->regmap = devm_regmap_init_i2c(i2c, &max77693_regmap_config);
	if (IS_ERR(max77693->regmap)) {
		ret = PTR_ERR(max77693->regmap);
		dev_err(max77693->dev, "failed to allocate register map: %d\n",
				ret);
		return ret;
	}

	ret = regmap_read(max77693->regmap, MAX77693_PMIC_REG_PMIC_ID2,
				&reg_data);
	if (ret < 0) {
		dev_err(max77693->dev, "device not found on this channel\n");
		return ret;
	} else
		dev_info(max77693->dev, "device ID: 0x%x\n", reg_data);

	max77693->i2c_muic = i2c_new_dummy_device(i2c->adapter, I2C_ADDR_MUIC);
	if (IS_ERR(max77693->i2c_muic)) {
		dev_err(max77693->dev, "Failed to allocate I2C device for MUIC\n");
		return PTR_ERR(max77693->i2c_muic);
	}
	i2c_set_clientdata(max77693->i2c_muic, max77693);

	max77693->i2c_haptic = i2c_new_dummy_device(i2c->adapter, I2C_ADDR_HAPTIC);
	if (IS_ERR(max77693->i2c_haptic)) {
		dev_err(max77693->dev, "Failed to allocate I2C device for Haptic\n");
		ret = PTR_ERR(max77693->i2c_haptic);
		goto err_i2c_haptic;
	}
	i2c_set_clientdata(max77693->i2c_haptic, max77693);

	max77693->regmap_haptic = devm_regmap_init_i2c(max77693->i2c_haptic,
					&max77693_regmap_haptic_config);
	if (IS_ERR(max77693->regmap_haptic)) {
		ret = PTR_ERR(max77693->regmap_haptic);
		dev_err(max77693->dev,
			"failed to initialize haptic register map: %d\n", ret);
		goto err_regmap;
	}

	/*
	 * Initialize register map for MUIC device because use regmap-muic
	 * instance of MUIC device when irq of max77693 is initialized
	 * before call max77693-muic probe() function.
	 */
	max77693->regmap_muic = devm_regmap_init_i2c(max77693->i2c_muic,
					 &max77693_regmap_muic_config);
	if (IS_ERR(max77693->regmap_muic)) {
		ret = PTR_ERR(max77693->regmap_muic);
		dev_err(max77693->dev,
			"failed to allocate register map: %d\n", ret);
		goto err_regmap;
	}

	ret = regmap_add_irq_chip(max77693->regmap, max77693->irq,
				IRQF_ONESHOT | IRQF_SHARED, 0,
				&max77693_led_irq_chip,
				&max77693->irq_data_led);
	if (ret) {
		dev_err(max77693->dev, "failed to add irq chip: %d\n", ret);
		goto err_regmap;
	}

	ret = regmap_add_irq_chip(max77693->regmap, max77693->irq,
				IRQF_ONESHOT | IRQF_SHARED, 0,
				&max77693_topsys_irq_chip,
				&max77693->irq_data_topsys);
	if (ret) {
		dev_err(max77693->dev, "failed to add irq chip: %d\n", ret);
		goto err_irq_topsys;
	}

	ret = regmap_add_irq_chip(max77693->regmap, max77693->irq,
				IRQF_ONESHOT | IRQF_SHARED, 0,
				&max77693_charger_irq_chip,
				&max77693->irq_data_chg);
	if (ret) {
		dev_err(max77693->dev, "failed to add irq chip: %d\n", ret);
		goto err_irq_charger;
	}

	ret = regmap_add_irq_chip(max77693->regmap_muic, max77693->irq,
				IRQF_ONESHOT | IRQF_SHARED, 0,
				&max77693_muic_irq_chip,
				&max77693->irq_data_muic);
	if (ret) {
		dev_err(max77693->dev, "failed to add irq chip: %d\n", ret);
		goto err_irq_muic;
	}

	/* Unmask interrupts from all blocks in interrupt source register */
	ret = regmap_update_bits(max77693->regmap,
				MAX77693_PMIC_REG_INTSRC_MASK,
				SRC_IRQ_ALL, (unsigned int)~SRC_IRQ_ALL);
	if (ret < 0) {
		dev_err(max77693->dev,
			"Could not unmask interrupts in INTSRC: %d\n",
			ret);
		goto err_intsrc;
	}

	pm_runtime_set_active(max77693->dev);

	ret = mfd_add_devices(max77693->dev, -1, max77693_devs,
			      ARRAY_SIZE(max77693_devs), NULL, 0, NULL);
	if (ret < 0)
		goto err_mfd;

	return ret;

err_mfd:
	mfd_remove_devices(max77693->dev);
err_intsrc:
	regmap_del_irq_chip(max77693->irq, max77693->irq_data_muic);
err_irq_muic:
	regmap_del_irq_chip(max77693->irq, max77693->irq_data_chg);
err_irq_charger:
	regmap_del_irq_chip(max77693->irq, max77693->irq_data_topsys);
err_irq_topsys:
	regmap_del_irq_chip(max77693->irq, max77693->irq_data_led);
err_regmap:
	i2c_unregister_device(max77693->i2c_haptic);
err_i2c_haptic:
	i2c_unregister_device(max77693->i2c_muic);
	return ret;
}

static void max77693_i2c_remove(struct i2c_client *i2c)
{
	struct max77693_dev *max77693 = i2c_get_clientdata(i2c);

	mfd_remove_devices(max77693->dev);

	regmap_del_irq_chip(max77693->irq, max77693->irq_data_muic);
	regmap_del_irq_chip(max77693->irq, max77693->irq_data_chg);
	regmap_del_irq_chip(max77693->irq, max77693->irq_data_topsys);
	regmap_del_irq_chip(max77693->irq, max77693->irq_data_led);

	i2c_unregister_device(max77693->i2c_muic);
	i2c_unregister_device(max77693->i2c_haptic);
}

static const struct i2c_device_id max77693_i2c_id[] = {
	{ "max77693", TYPE_MAX77693 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max77693_i2c_id);

static int max77693_suspend(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct max77693_dev *max77693 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev)) {
		enable_irq_wake(max77693->irq);
		disable_irq(max77693->irq);
	}

	return 0;
}

static int max77693_resume(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct max77693_dev *max77693 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev)) {
		disable_irq_wake(max77693->irq);
		enable_irq(max77693->irq);
	}

	return 0;
}

static const struct dev_pm_ops max77693_pm = {
	.suspend = max77693_suspend,
	.resume = max77693_resume,
};

#ifdef CONFIG_OF
static const struct of_device_id max77693_dt_match[] = {
	{ .compatible = "maxim,max77693" },
	{},
};
MODULE_DEVICE_TABLE(of, max77693_dt_match);
#endif

static struct i2c_driver max77693_i2c_driver = {
	.driver = {
		   .name = "max77693",
		   .pm = &max77693_pm,
		   .of_match_table = of_match_ptr(max77693_dt_match),
	},
	.probe = max77693_i2c_probe,
	.remove = max77693_i2c_remove,
	.id_table = max77693_i2c_id,
};

module_i2c_driver(max77693_i2c_driver);

MODULE_DESCRIPTION("MAXIM 77693 multi-function core driver");
MODULE_AUTHOR("SangYoung, Son <hello.son@samsung.com>");
MODULE_LICENSE("GPL");
