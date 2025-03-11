// SPDX-License-Identifier: GPL-2.0
/*
 * Intel BXT Whiskey Cove PMIC TMU driver
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
 *
 * This driver adds TMU (Time Management Unit) support for Intel BXT platform.
 * It enables the alarm wake-up functionality in the TMU unit of Whiskey Cove
 * PMIC.
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/mfd/intel_soc_pmic.h>

#define BXTWC_TMUIRQ		0x4fb6
#define BXTWC_MIRQLVL1		0x4e0e
#define BXTWC_MTMUIRQ_REG	0x4fb7
#define BXTWC_MIRQLVL1_MTMU	BIT(1)
#define BXTWC_TMU_WK_ALRM	BIT(1)
#define BXTWC_TMU_SYS_ALRM	BIT(2)
#define BXTWC_TMU_ALRM_MASK	(BXTWC_TMU_WK_ALRM | BXTWC_TMU_SYS_ALRM)
#define BXTWC_TMU_ALRM_IRQ	(BXTWC_TMU_WK_ALRM | BXTWC_TMU_SYS_ALRM)

struct wcove_tmu {
	int irq;
	struct device *dev;
	struct regmap *regmap;
};

static irqreturn_t bxt_wcove_tmu_irq_handler(int irq, void *data)
{
	struct wcove_tmu *wctmu = data;
	unsigned int tmu_irq;

	/* Read TMU interrupt reg */
	regmap_read(wctmu->regmap, BXTWC_TMUIRQ, &tmu_irq);
	if (tmu_irq & BXTWC_TMU_ALRM_IRQ) {
		/* clear TMU irq */
		regmap_write(wctmu->regmap, BXTWC_TMUIRQ, tmu_irq);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static int bxt_wcove_tmu_probe(struct platform_device *pdev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(pdev->dev.parent);
	struct wcove_tmu *wctmu;
	int ret;

	wctmu = devm_kzalloc(&pdev->dev, sizeof(*wctmu), GFP_KERNEL);
	if (!wctmu)
		return -ENOMEM;

	wctmu->dev = &pdev->dev;
	wctmu->regmap = pmic->regmap;

	wctmu->irq = platform_get_irq(pdev, 0);
	if (wctmu->irq < 0)
		return wctmu->irq;

	ret = devm_request_threaded_irq(&pdev->dev, wctmu->irq,
					NULL, bxt_wcove_tmu_irq_handler,
					IRQF_ONESHOT, "bxt_wcove_tmu", wctmu);
	if (ret) {
		dev_err(&pdev->dev, "request irq failed: %d,virq: %d\n",
			ret, wctmu->irq);
		return ret;
	}

	/* Unmask TMU second level Wake & System alarm */
	regmap_update_bits(wctmu->regmap, BXTWC_MTMUIRQ_REG,
				  BXTWC_TMU_ALRM_MASK, 0);

	platform_set_drvdata(pdev, wctmu);
	return 0;
}

static void bxt_wcove_tmu_remove(struct platform_device *pdev)
{
	struct wcove_tmu *wctmu = platform_get_drvdata(pdev);
	unsigned int val;

	/* Mask TMU interrupts */
	regmap_read(wctmu->regmap, BXTWC_MIRQLVL1, &val);
	regmap_write(wctmu->regmap, BXTWC_MIRQLVL1,
			val | BXTWC_MIRQLVL1_MTMU);
	regmap_read(wctmu->regmap, BXTWC_MTMUIRQ_REG, &val);
	regmap_write(wctmu->regmap, BXTWC_MTMUIRQ_REG,
			val | BXTWC_TMU_ALRM_MASK);
}

#ifdef CONFIG_PM_SLEEP
static int bxtwc_tmu_suspend(struct device *dev)
{
	struct wcove_tmu *wctmu = dev_get_drvdata(dev);

	enable_irq_wake(wctmu->irq);
	return 0;
}

static int bxtwc_tmu_resume(struct device *dev)
{
	struct wcove_tmu *wctmu = dev_get_drvdata(dev);

	disable_irq_wake(wctmu->irq);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(bxtwc_tmu_pm_ops, bxtwc_tmu_suspend, bxtwc_tmu_resume);

static const struct platform_device_id bxt_wcove_tmu_id_table[] = {
	{ .name = "bxt_wcove_tmu" },
	{},
};
MODULE_DEVICE_TABLE(platform, bxt_wcove_tmu_id_table);

static struct platform_driver bxt_wcove_tmu_driver = {
	.probe = bxt_wcove_tmu_probe,
	.remove = bxt_wcove_tmu_remove,
	.driver = {
		.name = "bxt_wcove_tmu",
		.pm     = &bxtwc_tmu_pm_ops,
	},
	.id_table = bxt_wcove_tmu_id_table,
};

module_platform_driver(bxt_wcove_tmu_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Nilesh Bacchewar <nilesh.bacchewar@intel.com>");
MODULE_DESCRIPTION("BXT Whiskey Cove TMU Driver");
