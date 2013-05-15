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
#include <linux/err.h>
#include <linux/ssbi.h>
#include <linux/mfd/core.h>
#include <linux/mfd/pm8xxx/pm8921.h>
#include <linux/mfd/pm8xxx/core.h>

#define REG_HWREV		0x002  /* PMIC4 revision */
#define REG_HWREV_2		0x0E8  /* PMIC4 revision 2 */

struct pm8921 {
	struct device			*dev;
	struct pm_irq_chip		*irq_chip;
};

static int pm8921_readb(const struct device *dev, u16 addr, u8 *val)
{
	const struct pm8xxx_drvdata *pm8921_drvdata = dev_get_drvdata(dev);
	const struct pm8921 *pmic = pm8921_drvdata->pm_chip_data;

	return ssbi_read(pmic->dev->parent, addr, val, 1);
}

static int pm8921_writeb(const struct device *dev, u16 addr, u8 val)
{
	const struct pm8xxx_drvdata *pm8921_drvdata = dev_get_drvdata(dev);
	const struct pm8921 *pmic = pm8921_drvdata->pm_chip_data;

	return ssbi_write(pmic->dev->parent, addr, &val, 1);
}

static int pm8921_read_buf(const struct device *dev, u16 addr, u8 *buf,
									int cnt)
{
	const struct pm8xxx_drvdata *pm8921_drvdata = dev_get_drvdata(dev);
	const struct pm8921 *pmic = pm8921_drvdata->pm_chip_data;

	return ssbi_read(pmic->dev->parent, addr, buf, cnt);
}

static int pm8921_write_buf(const struct device *dev, u16 addr, u8 *buf,
									int cnt)
{
	const struct pm8xxx_drvdata *pm8921_drvdata = dev_get_drvdata(dev);
	const struct pm8921 *pmic = pm8921_drvdata->pm_chip_data;

	return ssbi_write(pmic->dev->parent, addr, buf, cnt);
}

static int pm8921_read_irq_stat(const struct device *dev, int irq)
{
	const struct pm8xxx_drvdata *pm8921_drvdata = dev_get_drvdata(dev);
	const struct pm8921 *pmic = pm8921_drvdata->pm_chip_data;

	return pm8xxx_get_irq_stat(pmic->irq_chip, irq);
}

static struct pm8xxx_drvdata pm8921_drvdata = {
	.pmic_readb		= pm8921_readb,
	.pmic_writeb		= pm8921_writeb,
	.pmic_read_buf		= pm8921_read_buf,
	.pmic_write_buf		= pm8921_write_buf,
	.pmic_read_irq_stat	= pm8921_read_irq_stat,
};

static int pm8921_add_subdevices(const struct pm8921_platform_data
					   *pdata,
					   struct pm8921 *pmic,
					   u32 rev)
{
	int ret = 0, irq_base = 0;
	struct pm_irq_chip *irq_chip;

	if (pdata->irq_pdata) {
		pdata->irq_pdata->irq_cdata.nirqs = PM8921_NR_IRQS;
		pdata->irq_pdata->irq_cdata.rev = rev;
		irq_base = pdata->irq_pdata->irq_base;
		irq_chip = pm8xxx_irq_init(pmic->dev, pdata->irq_pdata);

		if (IS_ERR(irq_chip)) {
			pr_err("Failed to init interrupts ret=%ld\n",
					PTR_ERR(irq_chip));
			return PTR_ERR(irq_chip);
		}
		pmic->irq_chip = irq_chip;
	}
	return ret;
}

static int pm8921_probe(struct platform_device *pdev)
{
	const struct pm8921_platform_data *pdata = pdev->dev.platform_data;
	struct pm8921 *pmic;
	int rc;
	u8 val;
	u32 rev;

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
	rc = ssbi_read(pdev->dev.parent, REG_HWREV, &val, sizeof(val));
	if (rc) {
		pr_err("Failed to read hw rev reg %d:rc=%d\n", REG_HWREV, rc);
		goto err_read_rev;
	}
	pr_info("PMIC revision 1: %02X\n", val);
	rev = val;

	/* Read PMIC chip revision 2 */
	rc = ssbi_read(pdev->dev.parent, REG_HWREV_2, &val, sizeof(val));
	if (rc) {
		pr_err("Failed to read hw rev 2 reg %d:rc=%d\n",
			REG_HWREV_2, rc);
		goto err_read_rev;
	}
	pr_info("PMIC revision 2: %02X\n", val);
	rev |= val << BITS_PER_BYTE;

	pmic->dev = &pdev->dev;
	pm8921_drvdata.pm_chip_data = pmic;
	platform_set_drvdata(pdev, &pm8921_drvdata);

	rc = pm8921_add_subdevices(pdata, pmic, rev);
	if (rc) {
		pr_err("Cannot add subdevices rc=%d\n", rc);
		goto err;
	}

	/* gpio might not work if no irq device is found */
	WARN_ON(pmic->irq_chip == NULL);

	return 0;

err:
	mfd_remove_devices(pmic->dev);
	platform_set_drvdata(pdev, NULL);
err_read_rev:
	kfree(pmic);
	return rc;
}

static int pm8921_remove(struct platform_device *pdev)
{
	struct pm8xxx_drvdata *drvdata;
	struct pm8921 *pmic = NULL;

	drvdata = platform_get_drvdata(pdev);
	if (drvdata)
		pmic = drvdata->pm_chip_data;
	if (pmic)
		mfd_remove_devices(pmic->dev);
	if (pmic->irq_chip) {
		pm8xxx_irq_exit(pmic->irq_chip);
		pmic->irq_chip = NULL;
	}
	platform_set_drvdata(pdev, NULL);
	kfree(pmic);

	return 0;
}

static struct platform_driver pm8921_driver = {
	.probe		= pm8921_probe,
	.remove		= pm8921_remove,
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
