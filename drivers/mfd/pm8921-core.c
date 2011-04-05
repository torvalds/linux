/*
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/msm_ssbi.h>
#include <linux/mfd/core.h>
#include <linux/mfd/pm8xxx/pm8921.h>
#include <linux/mfd/pm8xxx/core.h>

#define REG_HWREV		0x002  /* PMIC4 revision */
#define REG_HWREV_2		0x0E8  /* PMIC4 revision 2 */

struct pm8921 {
	struct device			*dev;
};

static int pm8921_readb(const struct device *dev, u16 addr, u8 *val)
{
	const struct pm8xxx_drvdata *pm8921_drvdata = dev_get_drvdata(dev);
	const struct pm8921 *pmic = pm8921_drvdata->pm_chip_data;

	return msm_ssbi_read(pmic->dev->parent, addr, val, 1);
}

static int pm8921_writeb(const struct device *dev, u16 addr, u8 val)
{
	const struct pm8xxx_drvdata *pm8921_drvdata = dev_get_drvdata(dev);
	const struct pm8921 *pmic = pm8921_drvdata->pm_chip_data;

	return msm_ssbi_write(pmic->dev->parent, addr, &val, 1);
}

static int pm8921_read_buf(const struct device *dev, u16 addr, u8 *buf,
									int cnt)
{
	const struct pm8xxx_drvdata *pm8921_drvdata = dev_get_drvdata(dev);
	const struct pm8921 *pmic = pm8921_drvdata->pm_chip_data;

	return msm_ssbi_read(pmic->dev->parent, addr, buf, cnt);
}

static int pm8921_write_buf(const struct device *dev, u16 addr, u8 *buf,
									int cnt)
{
	const struct pm8xxx_drvdata *pm8921_drvdata = dev_get_drvdata(dev);
	const struct pm8921 *pmic = pm8921_drvdata->pm_chip_data;

	return msm_ssbi_write(pmic->dev->parent, addr, buf, cnt);
}

static struct pm8xxx_drvdata pm8921_drvdata = {
	.pmic_readb		= pm8921_readb,
	.pmic_writeb		= pm8921_writeb,
	.pmic_read_buf		= pm8921_read_buf,
	.pmic_write_buf		= pm8921_write_buf,
};

static int __devinit pm8921_probe(struct platform_device *pdev)
{
	const struct pm8921_platform_data *pdata = pdev->dev.platform_data;
	struct pm8921 *pmic;
	int rc;
	u8 val;

	if (!pdata) {
		pr_err("missing platform data\n");
		return -EINVAL;
	}

	pmic = kzalloc(sizeof(struct pm8921), GFP_KERNEL);
	if (!pmic) {
		pr_err("Cannot alloc pm8921 struct\n");
		return -ENOMEM;
	}

	/* Read PMIC chip revision */
	rc = msm_ssbi_read(pdev->dev.parent, REG_HWREV, &val, sizeof(val));
	if (rc) {
		pr_err("Failed to read hw rev reg %d:rc=%d\n", REG_HWREV, rc);
		goto err_read_rev;
	}
	pr_info("PMIC revision 1: %02X\n", val);

	/* Read PMIC chip revision 2 */
	rc = msm_ssbi_read(pdev->dev.parent, REG_HWREV_2, &val, sizeof(val));
	if (rc) {
		pr_err("Failed to read hw rev 2 reg %d:rc=%d\n",
			REG_HWREV_2, rc);
		goto err_read_rev;
	}
	pr_info("PMIC revision 2: %02X\n", val);

	pmic->dev = &pdev->dev;
	pm8921_drvdata.pm_chip_data = pmic;
	platform_set_drvdata(pdev, &pm8921_drvdata);

	return 0;

err_read_rev:
	kfree(pmic);
	return rc;
}

static int __devexit pm8921_remove(struct platform_device *pdev)
{
	struct pm8xxx_drvdata *drvdata;
	struct pm8921 *pmic = NULL;

	drvdata = platform_get_drvdata(pdev);
	if (drvdata)
		pmic = drvdata->pm_chip_data;
	if (pmic)
		mfd_remove_devices(pmic->dev);
	platform_set_drvdata(pdev, NULL);
	kfree(pmic);

	return 0;
}

static struct platform_driver pm8921_driver = {
	.probe		= pm8921_probe,
	.remove		= __devexit_p(pm8921_remove),
	.driver		= {
		.name	= "pm8921-core",
		.owner	= THIS_MODULE,
	},
};

static int __init pm8921_init(void)
{
	return platform_driver_register(&pm8921_driver);
}
subsys_initcall(pm8921_init);

static void __exit pm8921_exit(void)
{
	platform_driver_unregister(&pm8921_driver);
}
module_exit(pm8921_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("PMIC 8921 core driver");
MODULE_VERSION("1.0");
MODULE_ALIAS("platform:pm8921-core");
