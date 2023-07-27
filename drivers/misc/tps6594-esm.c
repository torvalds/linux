// SPDX-License-Identifier: GPL-2.0
/*
 * ESM (Error Signal Monitor) driver for TI TPS6594/TPS6593/LP8764 PMICs
 *
 * Copyright (C) 2023 BayLibre Incorporated - https://www.baylibre.com/
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <linux/mfd/tps6594.h>

#define TPS6594_DEV_REV_1 0x08

static irqreturn_t tps6594_esm_isr(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	int i;

	for (i = 0 ; i < pdev->num_resources ; i++) {
		if (irq == platform_get_irq_byname(pdev, pdev->resource[i].name)) {
			dev_err(pdev->dev.parent, "%s error detected\n", pdev->resource[i].name);
			return IRQ_HANDLED;
		}
	}

	return IRQ_NONE;
}

static int tps6594_esm_probe(struct platform_device *pdev)
{
	struct tps6594 *tps = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	unsigned int rev;
	int irq;
	int ret;
	int i;

	/*
	 * Due to a bug in revision 1 of the PMIC, the GPIO3 used for the
	 * SoC ESM function is used to power the load switch instead.
	 * As a consequence, ESM can not be used on those PMIC.
	 * Check the version and return an error in case of revision 1.
	 */
	ret = regmap_read(tps->regmap, TPS6594_REG_DEV_REV, &rev);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to read PMIC revision\n");
	if (rev == TPS6594_DEV_REV_1)
		return dev_err_probe(dev, -ENODEV,
			      "ESM not supported for revision 1 PMIC\n");

	for (i = 0; i < pdev->num_resources; i++) {
		irq = platform_get_irq_byname(pdev, pdev->resource[i].name);
		if (irq < 0)
			return dev_err_probe(dev, irq, "Failed to get %s irq\n",
					     pdev->resource[i].name);

		ret = devm_request_threaded_irq(dev, irq, NULL,
						tps6594_esm_isr, IRQF_ONESHOT,
						pdev->resource[i].name, pdev);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to request irq\n");
	}

	ret = regmap_set_bits(tps->regmap, TPS6594_REG_ESM_SOC_MODE_CFG,
			      TPS6594_BIT_ESM_SOC_EN | TPS6594_BIT_ESM_SOC_ENDRV);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to configure ESM\n");

	ret = regmap_set_bits(tps->regmap, TPS6594_REG_ESM_SOC_START_REG,
			      TPS6594_BIT_ESM_SOC_START);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to start ESM\n");

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	return 0;
}

static int tps6594_esm_remove(struct platform_device *pdev)
{
	struct tps6594 *tps = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	int ret;

	ret = regmap_clear_bits(tps->regmap, TPS6594_REG_ESM_SOC_START_REG,
				TPS6594_BIT_ESM_SOC_START);
	if (ret) {
		dev_err(dev, "Failed to stop ESM\n");
		goto out;
	}

	ret = regmap_clear_bits(tps->regmap, TPS6594_REG_ESM_SOC_MODE_CFG,
				TPS6594_BIT_ESM_SOC_EN | TPS6594_BIT_ESM_SOC_ENDRV);
	if (ret)
		dev_err(dev, "Failed to unconfigure ESM\n");

out:
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	return ret;
}

static int tps6594_esm_suspend(struct device *dev)
{
	struct tps6594 *tps = dev_get_drvdata(dev->parent);
	int ret;

	ret = regmap_clear_bits(tps->regmap, TPS6594_REG_ESM_SOC_START_REG,
				TPS6594_BIT_ESM_SOC_START);

	pm_runtime_put_sync(dev);

	return ret;
}

static int tps6594_esm_resume(struct device *dev)
{
	struct tps6594 *tps = dev_get_drvdata(dev->parent);

	pm_runtime_get_sync(dev);

	return regmap_set_bits(tps->regmap, TPS6594_REG_ESM_SOC_START_REG,
			       TPS6594_BIT_ESM_SOC_START);
}

static DEFINE_SIMPLE_DEV_PM_OPS(tps6594_esm_pm_ops, tps6594_esm_suspend, tps6594_esm_resume);

static struct platform_driver tps6594_esm_driver = {
	.driver	= {
		.name = "tps6594-esm",
		.pm = pm_sleep_ptr(&tps6594_esm_pm_ops),
	},
	.probe = tps6594_esm_probe,
	.remove = tps6594_esm_remove,
};

module_platform_driver(tps6594_esm_driver);

MODULE_ALIAS("platform:tps6594-esm");
MODULE_AUTHOR("Julien Panis <jpanis@baylibre.com>");
MODULE_DESCRIPTION("TPS6594 Error Signal Monitor Driver");
MODULE_LICENSE("GPL");
