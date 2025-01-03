// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (c) 2012 Samsung Electronics Co., Ltd
//              http://www.samsung.com

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/of.h>
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
#include <linux/mfd/samsung/s2mps15.h>
#include <linux/mfd/samsung/s2mpu02.h>
#include <linux/mfd/samsung/s5m8767.h>
#include <linux/regmap.h>

static const struct mfd_cell s5m8767_devs[] = {
	{ .name = "s5m8767-pmic", },
	{ .name = "s5m-rtc", },
	{
		.name = "s5m8767-clk",
		.of_compatible = "samsung,s5m8767-clk",
	},
};

static const struct mfd_cell s2dos05_devs[] = {
	{ .name = "s2dos05-regulator", },
};

static const struct mfd_cell s2mps11_devs[] = {
	{ .name = "s2mps11-regulator", },
	{ .name = "s2mps14-rtc", },
	{
		.name = "s2mps11-clk",
		.of_compatible = "samsung,s2mps11-clk",
	},
};

static const struct mfd_cell s2mps13_devs[] = {
	{ .name = "s2mps13-regulator", },
	{ .name = "s2mps13-rtc", },
	{
		.name = "s2mps13-clk",
		.of_compatible = "samsung,s2mps13-clk",
	},
};

static const struct mfd_cell s2mps14_devs[] = {
	{ .name = "s2mps14-regulator", },
	{ .name = "s2mps14-rtc", },
	{
		.name = "s2mps14-clk",
		.of_compatible = "samsung,s2mps14-clk",
	},
};

static const struct mfd_cell s2mps15_devs[] = {
	{ .name = "s2mps15-regulator", },
	{ .name = "s2mps15-rtc", },
	{
		.name = "s2mps13-clk",
		.of_compatible = "samsung,s2mps13-clk",
	},
};

static const struct mfd_cell s2mpa01_devs[] = {
	{ .name = "s2mpa01-pmic", },
	{ .name = "s2mps14-rtc", },
};

static const struct mfd_cell s2mpu02_devs[] = {
	{ .name = "s2mpu02-regulator", },
};

static const struct of_device_id sec_dt_match[] = {
	{
		.compatible = "samsung,s5m8767-pmic",
		.data = (void *)S5M8767X,
	}, {
		.compatible = "samsung,s2dos05",
		.data = (void *)S2DOS05,
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
		.compatible = "samsung,s2mps15-pmic",
		.data = (void *)S2MPS15X,
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
MODULE_DEVICE_TABLE(of, sec_dt_match);

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

static const struct regmap_config s2mps15_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = S2MPS15_REG_LDODSCH4,
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

static const struct regmap_config s5m8767_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = S5M8767_REG_LDO28CTRL,
	.volatile_reg = s2mps11_volatile,
	.cache_type = REGCACHE_FLAT,
};

static void sec_pmic_dump_rev(struct sec_pmic_dev *sec_pmic)
{
	unsigned int val;

	/* For each device type, the REG_ID is always the first register */
	if (!regmap_read(sec_pmic->regmap_pmic, S2MPS11_REG_ID, &val))
		dev_dbg(sec_pmic->dev, "Revision: 0x%x\n", val);
}

static void sec_pmic_configure(struct sec_pmic_dev *sec_pmic)
{
	int err;

	if (sec_pmic->device_type != S2MPS13X)
		return;

	if (sec_pmic->pdata->disable_wrstbi) {
		/*
		 * If WRSTBI pin is pulled down this feature must be disabled
		 * because each Suspend to RAM will trigger buck voltage reset
		 * to default values.
		 */
		err = regmap_update_bits(sec_pmic->regmap_pmic,
					 S2MPS13_REG_WRSTBI,
					 S2MPS13_REG_WRSTBI_MASK, 0x0);
		if (err)
			dev_warn(sec_pmic->dev,
				 "Cannot initialize WRSTBI config: %d\n",
				 err);
	}
}

/*
 * Only the common platform data elements for s5m8767 are parsed here from the
 * device tree. Other sub-modules of s5m8767 such as pmic, rtc , charger and
 * others have to parse their own platform data elements from device tree.
 *
 * The s5m8767 platform data structure is instantiated here and the drivers for
 * the sub-modules need not instantiate another instance while parsing their
 * platform data.
 */
static struct sec_platform_data *
sec_pmic_i2c_parse_dt_pdata(struct device *dev)
{
	struct sec_platform_data *pd;

	pd = devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return ERR_PTR(-ENOMEM);

	pd->manual_poweroff = of_property_read_bool(dev->of_node,
						"samsung,s2mps11-acokb-ground");
	pd->disable_wrstbi = of_property_read_bool(dev->of_node,
						"samsung,s2mps11-wrstbi-ground");
	return pd;
}

static int sec_pmic_probe(struct i2c_client *i2c)
{
	const struct regmap_config *regmap;
	struct sec_platform_data *pdata;
	const struct mfd_cell *sec_devs;
	struct sec_pmic_dev *sec_pmic;
	int ret, num_sec_devs;

	sec_pmic = devm_kzalloc(&i2c->dev, sizeof(struct sec_pmic_dev),
				GFP_KERNEL);
	if (sec_pmic == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, sec_pmic);
	sec_pmic->dev = &i2c->dev;
	sec_pmic->i2c = i2c;
	sec_pmic->irq = i2c->irq;

	pdata = sec_pmic_i2c_parse_dt_pdata(sec_pmic->dev);
	if (IS_ERR(pdata)) {
		ret = PTR_ERR(pdata);
		return ret;
	}

	sec_pmic->device_type = (unsigned long)of_device_get_match_data(sec_pmic->dev);
	sec_pmic->pdata = pdata;

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
	case S2MPS15X:
		regmap = &s2mps15_regmap_config;
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

	sec_irq_init(sec_pmic);

	pm_runtime_set_active(sec_pmic->dev);

	switch (sec_pmic->device_type) {
	case S5M8767X:
		sec_devs = s5m8767_devs;
		num_sec_devs = ARRAY_SIZE(s5m8767_devs);
		break;
	case S2DOS05:
		sec_devs = s2dos05_devs;
		num_sec_devs = ARRAY_SIZE(s2dos05_devs);
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
	case S2MPS15X:
		sec_devs = s2mps15_devs;
		num_sec_devs = ARRAY_SIZE(s2mps15_devs);
		break;
	case S2MPU02:
		sec_devs = s2mpu02_devs;
		num_sec_devs = ARRAY_SIZE(s2mpu02_devs);
		break;
	default:
		dev_err(&i2c->dev, "Unsupported device type (%lu)\n",
			sec_pmic->device_type);
		return -ENODEV;
	}
	ret = devm_mfd_add_devices(sec_pmic->dev, -1, sec_devs, num_sec_devs,
				   NULL, 0, NULL);
	if (ret)
		return ret;

	sec_pmic_configure(sec_pmic);
	sec_pmic_dump_rev(sec_pmic);

	return ret;
}

static void sec_pmic_shutdown(struct i2c_client *i2c)
{
	struct sec_pmic_dev *sec_pmic = i2c_get_clientdata(i2c);
	unsigned int reg, mask;

	if (!sec_pmic->pdata->manual_poweroff)
		return;

	switch (sec_pmic->device_type) {
	case S2MPS11X:
		reg = S2MPS11_REG_CTRL1;
		mask = S2MPS11_CTRL1_PWRHOLD_MASK;
		break;
	default:
		/*
		 * Currently only one board with S2MPS11 needs this, so just
		 * ignore the rest.
		 */
		dev_warn(sec_pmic->dev,
			"Unsupported device %lu for manual power off\n",
			sec_pmic->device_type);
		return;
	}

	regmap_update_bits(sec_pmic->regmap_pmic, reg, mask, 0);
}

static int sec_pmic_suspend(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
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
	struct i2c_client *i2c = to_i2c_client(dev);
	struct sec_pmic_dev *sec_pmic = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		disable_irq_wake(sec_pmic->irq);
	enable_irq(sec_pmic->irq);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(sec_pmic_pm_ops,
				sec_pmic_suspend, sec_pmic_resume);

static struct i2c_driver sec_pmic_driver = {
	.driver = {
		   .name = "sec_pmic",
		   .pm = pm_sleep_ptr(&sec_pmic_pm_ops),
		   .of_match_table = sec_dt_match,
	},
	.probe = sec_pmic_probe,
	.shutdown = sec_pmic_shutdown,
};
module_i2c_driver(sec_pmic_driver);

MODULE_AUTHOR("Sangbeom Kim <sbkim73@samsung.com>");
MODULE_DESCRIPTION("Core support for the S5M MFD");
MODULE_LICENSE("GPL");
