// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2012 Samsung Electronics Co., Ltd
 *                http://www.samsung.com
 * Copyright 2025 Linaro Ltd.
 *
 * Samsung SxM core driver
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/mfd/samsung/core.h>
#include <linux/mfd/samsung/irq.h>
#include <linux/mfd/samsung/s2mps11.h>
#include <linux/mfd/samsung/s2mps13.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include "sec-core.h"

static const struct mfd_cell s5m8767_devs[] = {
	MFD_CELL_NAME("s5m8767-pmic"),
	MFD_CELL_NAME("s5m-rtc"),
	MFD_CELL_OF("s5m8767-clk", NULL, NULL, 0, 0, "samsung,s5m8767-clk"),
};

static const struct mfd_cell s2dos05_devs[] = {
	MFD_CELL_NAME("s2dos05-regulator"),
};

static const struct mfd_cell s2mpg10_devs[] = {
	MFD_CELL_NAME("s2mpg10-meter"),
	MFD_CELL_NAME("s2mpg10-regulator"),
	MFD_CELL_NAME("s2mpg10-rtc"),
	MFD_CELL_OF("s2mpg10-clk", NULL, NULL, 0, 0, "samsung,s2mpg10-clk"),
	MFD_CELL_OF("s2mpg10-gpio", NULL, NULL, 0, 0, "samsung,s2mpg10-gpio"),
};

static const struct mfd_cell s2mps11_devs[] = {
	MFD_CELL_NAME("s2mps11-regulator"),
	MFD_CELL_NAME("s2mps14-rtc"),
	MFD_CELL_OF("s2mps11-clk", NULL, NULL, 0, 0, "samsung,s2mps11-clk"),
};

static const struct mfd_cell s2mps13_devs[] = {
	MFD_CELL_NAME("s2mps13-regulator"),
	MFD_CELL_NAME("s2mps13-rtc"),
	MFD_CELL_OF("s2mps13-clk", NULL, NULL, 0, 0, "samsung,s2mps13-clk"),
};

static const struct mfd_cell s2mps14_devs[] = {
	MFD_CELL_NAME("s2mps14-regulator"),
	MFD_CELL_NAME("s2mps14-rtc"),
	MFD_CELL_OF("s2mps14-clk", NULL, NULL, 0, 0, "samsung,s2mps14-clk"),
};

static const struct mfd_cell s2mps15_devs[] = {
	MFD_CELL_NAME("s2mps15-regulator"),
	MFD_CELL_NAME("s2mps15-rtc"),
	MFD_CELL_OF("s2mps13-clk", NULL, NULL, 0, 0, "samsung,s2mps13-clk"),
};

static const struct mfd_cell s2mpa01_devs[] = {
	MFD_CELL_NAME("s2mpa01-pmic"),
	MFD_CELL_NAME("s2mps14-rtc"),
};

static const struct mfd_cell s2mpu02_devs[] = {
	MFD_CELL_NAME("s2mpu02-regulator"),
};

static const struct mfd_cell s2mpu05_devs[] = {
	MFD_CELL_NAME("s2mpu05-regulator"),
	MFD_CELL_NAME("s2mps15-rtc"),
};

static void sec_pmic_dump_rev(struct sec_pmic_dev *sec_pmic)
{
	unsigned int val;

	/* For s2mpg1x, the revision is in a different regmap */
	if (sec_pmic->device_type == S2MPG10)
		return;

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
sec_pmic_parse_dt_pdata(struct device *dev)
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

int sec_pmic_probe(struct device *dev, int device_type, unsigned int irq,
		   struct regmap *regmap, struct i2c_client *client)
{
	struct sec_platform_data *pdata;
	const struct mfd_cell *sec_devs;
	struct sec_pmic_dev *sec_pmic;
	int ret, num_sec_devs;

	sec_pmic = devm_kzalloc(dev, sizeof(*sec_pmic), GFP_KERNEL);
	if (!sec_pmic)
		return -ENOMEM;

	dev_set_drvdata(dev, sec_pmic);
	sec_pmic->dev = dev;
	sec_pmic->device_type = device_type;
	sec_pmic->i2c = client;
	sec_pmic->irq = irq;
	sec_pmic->regmap_pmic = regmap;

	pdata = sec_pmic_parse_dt_pdata(sec_pmic->dev);
	if (IS_ERR(pdata)) {
		ret = PTR_ERR(pdata);
		return ret;
	}

	sec_pmic->pdata = pdata;

	ret = sec_irq_init(sec_pmic);
	if (ret)
		return ret;

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
	case S2MPG10:
		sec_devs = s2mpg10_devs;
		num_sec_devs = ARRAY_SIZE(s2mpg10_devs);
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
	case S2MPU05:
		sec_devs = s2mpu05_devs;
		num_sec_devs = ARRAY_SIZE(s2mpu05_devs);
		break;
	default:
		return dev_err_probe(sec_pmic->dev, -EINVAL,
				     "Unsupported device type %d\n",
				     sec_pmic->device_type);
	}
	ret = devm_mfd_add_devices(sec_pmic->dev, -1, sec_devs, num_sec_devs,
				   NULL, 0, NULL);
	if (ret)
		return ret;

	sec_pmic_configure(sec_pmic);
	sec_pmic_dump_rev(sec_pmic);

	return ret;
}
EXPORT_SYMBOL_GPL(sec_pmic_probe);

void sec_pmic_shutdown(struct device *dev)
{
	struct sec_pmic_dev *sec_pmic = dev_get_drvdata(dev);
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
			 "Unsupported device %d for manual power off\n",
			 sec_pmic->device_type);
		return;
	}

	regmap_update_bits(sec_pmic->regmap_pmic, reg, mask, 0);
}
EXPORT_SYMBOL_GPL(sec_pmic_shutdown);

static int sec_pmic_suspend(struct device *dev)
{
	struct sec_pmic_dev *sec_pmic = dev_get_drvdata(dev);

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
	struct sec_pmic_dev *sec_pmic = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(sec_pmic->irq);
	enable_irq(sec_pmic->irq);

	return 0;
}

DEFINE_SIMPLE_DEV_PM_OPS(sec_pmic_pm_ops, sec_pmic_suspend, sec_pmic_resume);
EXPORT_SYMBOL_GPL(sec_pmic_pm_ops);

MODULE_AUTHOR("Chanwoo Choi <cw00.choi@samsung.com>");
MODULE_AUTHOR("Krzysztof Kozlowski <krzk@kernel.org>");
MODULE_AUTHOR("Sangbeom Kim <sbkim73@samsung.com>");
MODULE_AUTHOR("André Draszik <andre.draszik@linaro.org>");
MODULE_DESCRIPTION("Core driver for the Samsung S5M");
MODULE_LICENSE("GPL");
