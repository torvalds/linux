// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Flora Fu, MediaTek
 */

#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>
#include <linux/mfd/core.h>
#include <linux/mfd/mt6323/core.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/mfd/mt6323/registers.h>
#include <linux/mfd/mt6397/registers.h>

#define MT6323_RTC_BASE		0x8000
#define MT6323_RTC_SIZE		0x40

#define MT6397_RTC_BASE		0xe000
#define MT6397_RTC_SIZE		0x3e

#define MT6323_PWRC_BASE	0x8000
#define MT6323_PWRC_SIZE	0x40

static const struct resource mt6323_rtc_resources[] = {
	DEFINE_RES_MEM(MT6323_RTC_BASE, MT6323_RTC_SIZE),
	DEFINE_RES_IRQ(MT6323_IRQ_STATUS_RTC),
};

static const struct resource mt6397_rtc_resources[] = {
	DEFINE_RES_MEM(MT6397_RTC_BASE, MT6397_RTC_SIZE),
	DEFINE_RES_IRQ(MT6397_IRQ_RTC),
};

static const struct resource mt6323_keys_resources[] = {
	DEFINE_RES_IRQ(MT6323_IRQ_STATUS_PWRKEY),
	DEFINE_RES_IRQ(MT6323_IRQ_STATUS_FCHRKEY),
};

static const struct resource mt6397_keys_resources[] = {
	DEFINE_RES_IRQ(MT6397_IRQ_PWRKEY),
	DEFINE_RES_IRQ(MT6397_IRQ_HOMEKEY),
};

static const struct resource mt6323_pwrc_resources[] = {
	DEFINE_RES_MEM(MT6323_PWRC_BASE, MT6323_PWRC_SIZE),
};

static const struct mfd_cell mt6323_devs[] = {
	{
		.name = "mt6323-rtc",
		.num_resources = ARRAY_SIZE(mt6323_rtc_resources),
		.resources = mt6323_rtc_resources,
		.of_compatible = "mediatek,mt6323-rtc",
	}, {
		.name = "mt6323-regulator",
		.of_compatible = "mediatek,mt6323-regulator"
	}, {
		.name = "mt6323-led",
		.of_compatible = "mediatek,mt6323-led"
	}, {
		.name = "mtk-pmic-keys",
		.num_resources = ARRAY_SIZE(mt6323_keys_resources),
		.resources = mt6323_keys_resources,
		.of_compatible = "mediatek,mt6323-keys"
	}, {
		.name = "mt6323-pwrc",
		.num_resources = ARRAY_SIZE(mt6323_pwrc_resources),
		.resources = mt6323_pwrc_resources,
		.of_compatible = "mediatek,mt6323-pwrc"
	},
};

static const struct mfd_cell mt6397_devs[] = {
	{
		.name = "mt6397-rtc",
		.num_resources = ARRAY_SIZE(mt6397_rtc_resources),
		.resources = mt6397_rtc_resources,
		.of_compatible = "mediatek,mt6397-rtc",
	}, {
		.name = "mt6397-regulator",
		.of_compatible = "mediatek,mt6397-regulator",
	}, {
		.name = "mt6397-codec",
		.of_compatible = "mediatek,mt6397-codec",
	}, {
		.name = "mt6397-clk",
		.of_compatible = "mediatek,mt6397-clk",
	}, {
		.name = "mt6397-pinctrl",
		.of_compatible = "mediatek,mt6397-pinctrl",
	}, {
		.name = "mtk-pmic-keys",
		.num_resources = ARRAY_SIZE(mt6397_keys_resources),
		.resources = mt6397_keys_resources,
		.of_compatible = "mediatek,mt6397-keys"
	}
};

#ifdef CONFIG_PM_SLEEP
static int mt6397_irq_suspend(struct device *dev)
{
	struct mt6397_chip *chip = dev_get_drvdata(dev);

	regmap_write(chip->regmap, chip->int_con[0], chip->wake_mask[0]);
	regmap_write(chip->regmap, chip->int_con[1], chip->wake_mask[1]);

	enable_irq_wake(chip->irq);

	return 0;
}

static int mt6397_irq_resume(struct device *dev)
{
	struct mt6397_chip *chip = dev_get_drvdata(dev);

	regmap_write(chip->regmap, chip->int_con[0], chip->irq_masks_cur[0]);
	regmap_write(chip->regmap, chip->int_con[1], chip->irq_masks_cur[1]);

	disable_irq_wake(chip->irq);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(mt6397_pm_ops, mt6397_irq_suspend,
			mt6397_irq_resume);

static int mt6397_probe(struct platform_device *pdev)
{
	int ret;
	unsigned int id;
	struct mt6397_chip *pmic;

	pmic = devm_kzalloc(&pdev->dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	pmic->dev = &pdev->dev;

	/*
	 * mt6397 MFD is child device of soc pmic wrapper.
	 * Regmap is set from its parent.
	 */
	pmic->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!pmic->regmap)
		return -ENODEV;

	platform_set_drvdata(pdev, pmic);

	ret = regmap_read(pmic->regmap, MT6397_CID, &id);
	if (ret) {
		dev_err(pmic->dev, "Failed to read chip id: %d\n", ret);
		return ret;
	}

	pmic->irq = platform_get_irq(pdev, 0);
	if (pmic->irq <= 0)
		return pmic->irq;

	switch (id & 0xff) {
	case MT6323_CHIP_ID:
		pmic->int_con[0] = MT6323_INT_CON0;
		pmic->int_con[1] = MT6323_INT_CON1;
		pmic->int_status[0] = MT6323_INT_STATUS0;
		pmic->int_status[1] = MT6323_INT_STATUS1;
		ret = mt6397_irq_init(pmic);
		if (ret)
			return ret;

		ret = devm_mfd_add_devices(&pdev->dev, -1, mt6323_devs,
					   ARRAY_SIZE(mt6323_devs), NULL,
					   0, pmic->irq_domain);
		break;

	case MT6391_CHIP_ID:
	case MT6397_CHIP_ID:
		pmic->int_con[0] = MT6397_INT_CON0;
		pmic->int_con[1] = MT6397_INT_CON1;
		pmic->int_status[0] = MT6397_INT_STATUS0;
		pmic->int_status[1] = MT6397_INT_STATUS1;
		ret = mt6397_irq_init(pmic);
		if (ret)
			return ret;

		ret = devm_mfd_add_devices(&pdev->dev, -1, mt6397_devs,
					   ARRAY_SIZE(mt6397_devs), NULL,
					   0, pmic->irq_domain);
		break;

	default:
		dev_err(&pdev->dev, "unsupported chip: %d\n", id);
		return -ENODEV;
	}

	if (ret) {
		irq_domain_remove(pmic->irq_domain);
		dev_err(&pdev->dev, "failed to add child devices: %d\n", ret);
	}

	return ret;
}

static const struct of_device_id mt6397_of_match[] = {
	{ .compatible = "mediatek,mt6397" },
	{ .compatible = "mediatek,mt6323" },
	{ }
};
MODULE_DEVICE_TABLE(of, mt6397_of_match);

static const struct platform_device_id mt6397_id[] = {
	{ "mt6397", 0 },
	{ },
};
MODULE_DEVICE_TABLE(platform, mt6397_id);

static struct platform_driver mt6397_driver = {
	.probe = mt6397_probe,
	.driver = {
		.name = "mt6397",
		.of_match_table = of_match_ptr(mt6397_of_match),
		.pm = &mt6397_pm_ops,
	},
	.id_table = mt6397_id,
};

module_platform_driver(mt6397_driver);

MODULE_AUTHOR("Flora Fu, MediaTek");
MODULE_DESCRIPTION("Driver for MediaTek MT6397 PMIC");
MODULE_LICENSE("GPL");
