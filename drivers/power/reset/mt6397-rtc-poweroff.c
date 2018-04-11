// SPDX-License-Identifier: GPL-2.0
/*
 * Power-off using MediaTek PMIC RTC device
 *
 * Copyright (C) 2018 MediaTek Inc.
 *
 * Author: Sean Wang <sean.wang@mediatek.com>
 *
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc/mt6397.h>

struct mt6397_rtc_powercon {
	struct device *dev;
	struct mt6397_rtc *rtc;
};

static struct mt6397_rtc_powercon *mt_powercon;

static void mt6397_rtc_do_poweroff(void)
{
	struct mt6397_rtc_powercon *powercon = mt_powercon;
	struct mt6397_rtc *rtc = powercon->rtc;
	unsigned int val;
	int ret;

	regmap_write(rtc->regmap, rtc->addr_base + RTC_BBPU, RTC_BBPU_KEY);
	regmap_write(rtc->regmap, rtc->addr_base + RTC_WRTGR, 1);

	ret = regmap_read_poll_timeout(rtc->regmap,
				       rtc->addr_base + RTC_BBPU, val,
				       !(val & RTC_BBPU_CBUSY),
				       MTK_RTC_POLL_DELAY_US,
				       MTK_RTC_POLL_TIMEOUT);
	if (ret)
		dev_err(powercon->dev, "failed to write BBPU: %d\n", ret);

	/* Wait some time until system down, otherwise, notice with a warn */
	mdelay(1000);

	WARN_ONCE(1, "Unable to poweroff system\n");
}

static int mt6397_rtc_poweroff_probe(struct platform_device *pdev)
{
	struct mt6397_rtc *rtc = dev_get_drvdata(pdev->dev.parent);
	struct mt6397_rtc_powercon *powercon;

	if (!rtc) {
		dev_err(&pdev->dev, "Can't find RTC as the parent\n");
		return -ENODEV;
	}

	powercon = devm_kzalloc(&pdev->dev, sizeof(*powercon), GFP_KERNEL);
	if (!powercon)
		return -ENOMEM;

	powercon->dev = &pdev->dev;
	powercon->rtc = rtc;
	mt_powercon = powercon;

	pm_power_off = &mt6397_rtc_do_poweroff;

	return 0;
}

static int mt6397_rtc_poweroff_remove(struct platform_device *pdev)
{
	if (pm_power_off == &mt6397_rtc_do_poweroff)
		pm_power_off = NULL;

	return 0;
}

static const struct of_device_id mt6397_rtc_poweroff_dt_match[] = {
	{ .compatible = "mediatek,mt6323-rtc-poweroff" },
	{ .compatible = "mediatek,mt6397-rtc-poweroff" },
	{},
};
MODULE_DEVICE_TABLE(of, mt6397_rtc_poweroff_dt_match);

static struct platform_driver mt6397_rtc_poweroff_driver = {
	.probe		= mt6397_rtc_poweroff_probe,
	.remove		= mt6397_rtc_poweroff_remove,
	.driver		= {
		.name	= "mt6397-rtc-poweroff",
		.of_match_table = mt6397_rtc_poweroff_dt_match,
	},
};

module_platform_driver(mt6397_rtc_poweroff_driver);

MODULE_DESCRIPTION("Poweroff driver using MediaTek PMIC RTC");
MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:mt6397-rtc-poweroff");
