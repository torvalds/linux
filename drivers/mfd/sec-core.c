/*
 * sec-core.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd
 *              http://www.samsung.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/mfd/samsung/core.h>
#include <linux/mfd/samsung/irq.h>
#include <linux/mfd/samsung/s2mpa01.h>
#include <linux/mfd/samsung/s2mps11.h>
#include <linux/mfd/samsung/s2mps13.h>
#include <linux/mfd/samsung/s2mps14.h>
#include <linux/mfd/samsung/s2mpu02.h>
#include <linux/mfd/samsung/s5m8763.h>
#include <linux/mfd/samsung/s5m8767.h>
#include <linux/regmap.h>

static const struct mfd_cell s5m8751_devs[] = {
	{
		.name = "s5m8751-pmic",
	}, {
		.name = "s5m-charger",
	}, {
		.name = "s5m8751-codec",
	},
};

static const struct mfd_cell s5m8763_devs[] = {
	{
		.name = "s5m8763-pmic",
	}, {
		.name = "s5m-rtc",
	}, {
		.name = "s5m-charger",
	},
};

static const struct mfd_cell s5m8767_devs[] = {
	{
		.name = "s5m8767-pmic",
	}, {
		.name = "s5m-rtc",
	}, {
		.name = "s5m8767-clk",
		.of_compatible = "samsung,s5m8767-clk",
	}
};

static const struct mfd_cell s2mps11_devs[] = {
	{
		.name = "s2mps11-pmic",
	}, {
		.name = "s2mps14-rtc",
	}, {
		.name = "s2mps11-clk",
		.of_compatible = "samsung,s2mps11-clk",
	}
};

static const struct mfd_cell s2mps13_devs[] = {
	{ .name = "s2mps13-pmic", },
	{ .name = "s2mps13-rtc", },
	{
		.name = "s2mps13-clk",
		.of_compatible = "samsung,s2mps13-clk",
	},
};

static const struct mfd_cell s2mps14_devs[] = {
	{
		.name = "s2mps14-pmic",
	}, {
		.name = "s2mps14-rtc",
	}, {
		.name = "s2mps14-clk",
		.of_compatible = "samsung,s2mps14-clk",
	}
};

static const struct mfd_cell s2mpa01_devs[] = {
	{
		.name = "s2mpa01-pmic",
	},
};

static const struct mfd_cell s2mpu02_devs[] = {
	{ .name = "s2mpu02-pmic", },
	{ .name = "s2mpu02-rtc", },
	{
		.name = "s2mpu02-clk",
		.of_compatible = "samsung,s2mpu02-clk",
	}
};

#ifdef CONFIG_OF
static const struct of_device_id sec_dt_match[] = {
	{	.compatible = "samsung,s5m8767-pmic",
		.data = (void *)S5M8767X,
	}, {
		.compatible = "samsung,s2mps11-pmic",
		.data = (void *)S2MPS11X,
	}, {
		.compatible = "samsung,s2mps13-pmic",
		.data = (void *)S2MPS13X,
	}, {
		.compatible = "samsung,s2mps14-pmic",
		.data = (void *)S2MPS14X,
	}, {
		.compatible = "samsung,s2mpa01-pmic",
		.data = (void *)S2MPA01,
	}, {
		.compatible = "samsung,s2mpu02-pmic",
		.data = (void *)S2MPU02,
	}, {
		/* Sentinel */
	},
};
#endif

static bool s2mpa01_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case S2MPA01_REG_INT1M:
	case S2MPA01_REG_INT2M:
	case S2MPA01_REG_INT3M:
		return false;
	default:
		return true;
	}
}

static bool s2mps11_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case S2MPS11_REG_INT1M:
	case S2MPS11_REG_INT2M:
	case S2MPS11_REG_INT3M:
		return false;
	default:
		return true;
	}
}

static bool s2mpu02_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case S2MPU02_REG_INT1M:
	case S2MPU02_REG_INT2M:
	case S2MPU02_REG_INT3M:
		return false;
	default:
		return true;
	}
}

static bool s5m8763_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case S5M8763_REG_IRQM1:
	case S5M8763_REG_IRQM2:
	case S5M8763_REG_IRQM3:
	case S5M8763_REG_IRQM4:
		return false;
	default:
		return true;
	}
}

static const struct regmap_config sec_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static const struct regmap_config s2mpa01_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = S2MPA01_REG_LDO_OVCB4,
	.volatile_reg = s2mpa01_volatile,
	.cache_type = REGCACHE_FLAT,
};

static const struct regmap_config s2mps11_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = S2MPS11_REG_L38CTRL,
	.volatile_reg = s2mps11_volatile,
	.cache_type = REGCACHE_FLAT,
};

static const struct regmap_config s2mps13_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = S2MPS13_REG_LDODSCH5,
	.volatile_reg = s2mps11_volatile,
	.cache_type = REGCACHE_FLAT,
};

static const struct regmap_config s2mps14_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = S2MPS14_REG_LDODSCH3,
	.volatile_reg = s2mps11_volatile,
	.cache_type = REGCACHE_FLAT,
};

static const struct regmap_config s2mpu02_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = S2MPU02_REG_DVSDATA,
	.volatile_reg = s2mpu02_volatile,
	.cache_type = REGCACHE_FLAT,
};

static const struct regmap_config s5m8763_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = S5M8763_REG_LBCNFG2,
	.volatile_reg = s5m8763_volatile,
	.cache_type = REGCACHE_FLAT,
};

static const struct regmap_config s5m8767_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = S5M8767_REG_LDO28CTRL,
	.volatile_reg = s2mps11_volatile,
	.cache_type = REGCACHE_FLAT,
};

#ifdef CONFIG_OF
/*
 * Only the common platform data elements for s5m8767 are parsed here from the
 * device tree. Other sub-modules of s5m8767 such as pmic, rtc , charger and
 * others have to parse their own platform data elements from device tree.
 *
 * The s5m8767 platform data structure is instantiated here and the drivers for
 * the sub-modules need not instantiate another instance while parsing their
 * platform data.
 */
static struct sec_platform_data *sec_pmic_i2c_parse_dt_pdata(
					struct device *dev)
{
	struct sec_platform_data *pd;

	pd = devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return ERR_PTR(-ENOMEM);

	/*
	 * ToDo: the 'wakeup' member in the platform data is more of a linux
	 * specfic information. Hence, there is no binding for that yet and
	 * not parsed here.
	 */

	return pd;
}
#else
static struct sec_platform_data *sec_pmic_i2c_parse_dt_pdata(
					struct device *dev)
{
	return NULL;
}
#endif

static inline unsigned long sec_i2c_get_driver_data(struct i2c_client *i2c,
						const struct i2c_device_id *id)
{
#ifdef CONFIG_OF
	if (i2c->dev.of_node) {
		const struct of_device_id *match;

		match = of_match_node(sec_dt_match, i2c->dev.of_node);
		return (unsigned long)match->data;
	}
#endif
	return id->driver_data;
}

static int sec_pmic_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct sec_platform_data *pdata = dev_get_platdata(&i2c->dev);
	const struct regmap_config *regmap;
	const struct mfd_cell *sec_devs;
	struct sec_pmic_dev *sec_pmic;
	unsigned long device_type;
	int ret, num_sec_devs;

	sec_pmic = devm_kzalloc(&i2c->dev, sizeof(struct sec_pmic_dev),
				GFP_KERNEL);
	if (sec_pmic == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, sec_pmic);
	sec_pmic->dev = &i2c->dev;
	sec_pmic->i2c = i2c;
	sec_pmic->irq = i2c->irq;
	device_type = sec_i2c_get_driver_data(i2c, id);

	if (sec_pmic->dev->of_node) {
		pdata = sec_pmic_i2c_parse_dt_pdata(sec_pmic->dev);
		if (IS_ERR(pdata)) {
			ret = PTR_ERR(pdata);
			return ret;
		}
		pdata->device_type = device_type;
	}
	if (pdata) {
		sec_pmic->device_type = pdata->device_type;
		sec_pmic->irq_base = pdata->irq_base;
		sec_pmic->wakeup = pdata->wakeup;
		sec_pmic->pdata = pdata;
	}

	switch (sec_pmic->device_type) {
	case S2MPA01:
		regmap = &s2mpa01_regmap_config;
		break;
	case S2MPS11X:
		regmap = &s2mps11_regmap_config;
		break;
	case S2MPS13X:
		regmap = &s2mps13_regmap_config;
		break;
	case S2MPS14X:
		regmap = &s2mps14_regmap_config;
		break;
	case S5M8763X:
		regmap = &s5m8763_regmap_config;
		break;
	case S5M8767X:
		regmap = &s5m8767_regmap_config;
		break;
	case S2MPU02:
		regmap = &s2mpu02_regmap_config;
		break;
	default:
		regmap = &sec_regmap_config;
		break;
	}

	sec_pmic->regmap_pmic = devm_regmap_init_i2c(i2c, regmap);
	if (IS_ERR(sec_pmic->regmap_pmic)) {
		ret = PTR_ERR(sec_pmic->regmap_pmic);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	if (pdata && pdata->cfg_pmic_irq)
		pdata->cfg_pmic_irq();

	sec_irq_init(sec_pmic);

	pm_runtime_set_active(sec_pmic->dev);

	switch (sec_pmic->device_type) {
	case S5M8751X:
		sec_devs = s5m8751_devs;
		num_sec_devs = ARRAY_SIZE(s5m8751_devs);
		break;
	case S5M8763X:
		sec_devs = s5m8763_devs;
		num_sec_devs = ARRAY_SIZE(s5m8763_devs);
		break;
	case S5M8767X:
		sec_devs = s5m8767_devs;
		num_sec_devs = ARRAY_SIZE(s5m8767_devs);
		break;
	case S2MPA01:
		sec_devs = s2mpa01_devs;
		num_sec_devs = ARRAY_SIZE(s2mpa01_devs);
		break;
	case S2MPS11X:
		sec_devs = s2mps11_devs;
		num_sec_devs = ARRAY_SIZE(s2mps11_devs);
		break;
	case S2MPS13X:
		sec_devs = s2mps13_devs;
		num_sec_devs = ARRAY_SIZE(s2mps13_devs);
		break;
	case S2MPS14X:
		sec_devs = s2mps14_devs;
		num_sec_devs = ARRAY_SIZE(s2mps14_devs);
		break;
	case S2MPU02:
		sec_devs = s2mpu02_devs;
		num_sec_devs = ARRAY_SIZE(s2mpu02_devs);
		break;
	default:
		/* If this happens the probe function is problem */
		BUG();
	}
	ret = mfd_add_devices(sec_pmic->dev, -1, sec_devs, num_sec_devs, NULL,
			      0, NULL);
	if (ret)
		goto err_mfd;

	device_init_wakeup(sec_pmic->dev, sec_pmic->wakeup);

	return ret;

err_mfd:
	sec_irq_exit(sec_pmic);
	return ret;
}

static int sec_pmic_remove(struct i2c_client *i2c)
{
	struct sec_pmic_dev *sec_pmic = i2c_get_clientdata(i2c);

	mfd_remove_devices(sec_pmic->dev);
	sec_irq_exit(sec_pmic);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sec_pmic_suspend(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct sec_pmic_dev *sec_pmic = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		enable_irq_wake(sec_pmic->irq);
	/*
	 * PMIC IRQ must be disabled during suspend for RTC alarm
	 * to work properly.
	 * When device is woken up from suspend, an
	 * interrupt occurs before resuming I2C bus controller.
	 * The interrupt is handled by regmap_irq_thread which tries
	 * to read RTC registers. This read fails (I2C is still
	 * suspended) and RTC Alarm interrupt is disabled.
	 */
	disable_irq(sec_pmic->irq);

	return 0;
}

static int sec_pmic_resume(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct sec_pmic_dev *sec_pmic = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		disable_irq_wake(sec_pmic->irq);
	enable_irq(sec_pmic->irq);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(sec_pmic_pm_ops, sec_pmic_suspend, sec_pmic_resume);

static const struct i2c_device_id sec_pmic_id[] = {
	{ "sec_pmic", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sec_pmic_id);

static struct i2c_driver sec_pmic_driver = {
	.driver = {
		   .name = "sec_pmic",
		   .owner = THIS_MODULE,
		   .pm = &sec_pmic_pm_ops,
		   .of_match_table = of_match_ptr(sec_dt_match),
	},
	.probe = sec_pmic_probe,
	.remove = sec_pmic_remove,
	.id_table = sec_pmic_id,
};

static int __init sec_pmic_init(void)
{
	return i2c_add_driver(&sec_pmic_driver);
}

subsys_initcall(sec_pmic_init);

static void __exit sec_pmic_exit(void)
{
	i2c_del_driver(&sec_pmic_driver);
}
module_exit(sec_pmic_exit);

MODULE_AUTHOR("Sangbeom Kim <sbkim73@samsung.com>");
MODULE_DESCRIPTION("Core support for the S5M MFD");
MODULE_LICENSE("GPL");
