/*
 * Real time clock driver for DA9055
 *
 * Copyright(c) 2012 Dialog Semiconductor Ltd.
 *
 * Author: Dajun Dajun Chen <dajun.chen@diasemi.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

#include <linux/mfd/da9055/core.h>
#include <linux/mfd/da9055/reg.h>
#include <linux/mfd/da9055/pdata.h>

struct da9055_rtc {
	struct rtc_device *rtc;
	struct da9055 *da9055;
	int alarm_enable;
};

static int da9055_rtc_enable_alarm(struct da9055_rtc *rtc, bool enable)
{
	int ret;
	if (enable) {
		ret = da9055_reg_update(rtc->da9055, DA9055_REG_ALARM_Y,
					DA9055_RTC_ALM_EN,
					DA9055_RTC_ALM_EN);
		if (ret != 0)
			dev_err(rtc->da9055->dev, "Failed to enable ALM: %d\n",
				ret);
		rtc->alarm_enable = 1;
	} else {
		ret = da9055_reg_update(rtc->da9055, DA9055_REG_ALARM_Y,
					DA9055_RTC_ALM_EN, 0);
		if (ret != 0)
			dev_err(rtc->da9055->dev,
				"Failed to disable ALM: %d\n", ret);
		rtc->alarm_enable = 0;
	}
	return ret;
}

static irqreturn_t da9055_rtc_alm_irq(int irq, void *data)
{
	struct da9055_rtc *rtc = data;

	da9055_rtc_enable_alarm(rtc, 0);
	rtc_update_irq(rtc->rtc, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}

static int da9055_read_alarm(struct da9055 *da9055, struct rtc_time *rtc_tm)
{
	int ret;
	uint8_t v[5];

	ret = da9055_group_read(da9055, DA9055_REG_ALARM_MI, 5, v);
	if (ret != 0) {
		dev_err(da9055->dev, "Failed to group read ALM: %d\n", ret);
		return ret;
	}

	rtc_tm->tm_year = (v[4] & DA9055_RTC_ALM_YEAR) + 100;
	rtc_tm->tm_mon  = (v[3] & DA9055_RTC_ALM_MONTH) - 1;
	rtc_tm->tm_mday = v[2] & DA9055_RTC_ALM_DAY;
	rtc_tm->tm_hour = v[1] & DA9055_RTC_ALM_HOUR;
	rtc_tm->tm_min  = v[0] & DA9055_RTC_ALM_MIN;
	rtc_tm->tm_sec = 0;

	return rtc_valid_tm(rtc_tm);
}

static int da9055_set_alarm(struct da9055 *da9055, struct rtc_time *rtc_tm)
{
	int ret;
	uint8_t v[2];

	rtc_tm->tm_year -= 100;
	rtc_tm->tm_mon += 1;

	ret = da9055_reg_update(da9055, DA9055_REG_ALARM_MI,
				DA9055_RTC_ALM_MIN, rtc_tm->tm_min);
	if (ret != 0) {
		dev_err(da9055->dev, "Failed to write ALRM MIN: %d\n", ret);
		return ret;
	}

	v[0] = rtc_tm->tm_hour;
	v[1] = rtc_tm->tm_mday;

	ret = da9055_group_write(da9055, DA9055_REG_ALARM_H, 2, v);
	if (ret < 0)
		return ret;

	ret = da9055_reg_update(da9055, DA9055_REG_ALARM_MO,
				DA9055_RTC_ALM_MONTH, rtc_tm->tm_mon);
	if (ret < 0)
		dev_err(da9055->dev, "Failed to write ALM Month:%d\n", ret);

	ret = da9055_reg_update(da9055, DA9055_REG_ALARM_Y,
				DA9055_RTC_ALM_YEAR, rtc_tm->tm_year);
	if (ret < 0)
		dev_err(da9055->dev, "Failed to write ALM Year:%d\n", ret);

	return ret;
}

static int da9055_rtc_get_alarm_status(struct da9055 *da9055)
{
	int ret;

	ret = da9055_reg_read(da9055, DA9055_REG_ALARM_Y);
	if (ret < 0) {
		dev_err(da9055->dev, "Failed to read ALM: %d\n", ret);
		return ret;
	}
	ret &= DA9055_RTC_ALM_EN;
	return (ret > 0) ? 1 : 0;
}

static int da9055_rtc_read_time(struct device *dev, struct rtc_time *rtc_tm)
{
	struct da9055_rtc *rtc = dev_get_drvdata(dev);
	uint8_t v[6];
	int ret;

	ret = da9055_reg_read(rtc->da9055, DA9055_REG_COUNT_S);
	if (ret < 0)
		return ret;

	/*
	 * Registers are only valid when RTC_READ
	 * status bit is asserted
	 */
	if (!(ret & DA9055_RTC_READ))
		return -EBUSY;

	ret = da9055_group_read(rtc->da9055, DA9055_REG_COUNT_S, 6, v);
	if (ret < 0) {
		dev_err(rtc->da9055->dev, "Failed to read RTC time : %d\n",
			ret);
		return ret;
	}

	rtc_tm->tm_year = (v[5] & DA9055_RTC_YEAR) + 100;
	rtc_tm->tm_mon  = (v[4] & DA9055_RTC_MONTH) - 1;
	rtc_tm->tm_mday = v[3] & DA9055_RTC_DAY;
	rtc_tm->tm_hour = v[2] & DA9055_RTC_HOUR;
	rtc_tm->tm_min  = v[1] & DA9055_RTC_MIN;
	rtc_tm->tm_sec  = v[0] & DA9055_RTC_SEC;

	return rtc_valid_tm(rtc_tm);
}

static int da9055_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct da9055_rtc *rtc;
	uint8_t v[6];

	rtc = dev_get_drvdata(dev);

	v[0] = tm->tm_sec;
	v[1] = tm->tm_min;
	v[2] = tm->tm_hour;
	v[3] = tm->tm_mday;
	v[4] = tm->tm_mon + 1;
	v[5] = tm->tm_year - 100;

	return da9055_group_write(rtc->da9055, DA9055_REG_COUNT_S, 6, v);
}

static int da9055_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	int ret;
	struct rtc_time *tm = &alrm->time;
	struct da9055_rtc *rtc = dev_get_drvdata(dev);

	ret = da9055_read_alarm(rtc->da9055, tm);

	if (ret)
		return ret;

	alrm->enabled = da9055_rtc_get_alarm_status(rtc->da9055);

	return 0;
}

static int da9055_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	int ret;
	struct rtc_time *tm = &alrm->time;
	struct da9055_rtc *rtc = dev_get_drvdata(dev);

	ret = da9055_rtc_enable_alarm(rtc, 0);
	if (ret < 0)
		return ret;

	ret = da9055_set_alarm(rtc->da9055, tm);
	if (ret)
		return ret;

	ret = da9055_rtc_enable_alarm(rtc, 1);

	return ret;
}

static int da9055_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct da9055_rtc *rtc = dev_get_drvdata(dev);

	return da9055_rtc_enable_alarm(rtc, enabled);
}

static const struct rtc_class_ops da9055_rtc_ops = {
	.read_time	= da9055_rtc_read_time,
	.set_time	= da9055_rtc_set_time,
	.read_alarm	= da9055_rtc_read_alarm,
	.set_alarm	= da9055_rtc_set_alarm,
	.alarm_irq_enable = da9055_rtc_alarm_irq_enable,
};

static int da9055_rtc_device_init(struct da9055 *da9055,
					struct da9055_pdata *pdata)
{
	int ret;

	/* Enable RTC and the internal Crystal */
	ret = da9055_reg_update(da9055, DA9055_REG_CONTROL_B,
				DA9055_RTC_EN, DA9055_RTC_EN);
	if (ret < 0)
		return ret;
	ret = da9055_reg_update(da9055, DA9055_REG_EN_32K,
				DA9055_CRYSTAL_EN, DA9055_CRYSTAL_EN);
	if (ret < 0)
		return ret;

	/* Enable RTC in Power Down mode */
	ret = da9055_reg_update(da9055, DA9055_REG_CONTROL_B,
				DA9055_RTC_MODE_PD, DA9055_RTC_MODE_PD);
	if (ret < 0)
		return ret;

	/* Enable RTC in Reset mode */
	if (pdata && pdata->reset_enable) {
		ret = da9055_reg_update(da9055, DA9055_REG_CONTROL_B,
					DA9055_RTC_MODE_SD,
					DA9055_RTC_MODE_SD <<
					DA9055_RTC_MODE_SD_SHIFT);
		if (ret < 0)
			return ret;
	}

	/* Disable the RTC TICK ALM */
	ret = da9055_reg_update(da9055, DA9055_REG_ALARM_MO,
				DA9055_RTC_TICK_WAKE_MASK, 0);
	if (ret < 0)
		return ret;

	return 0;
}

static int da9055_rtc_probe(struct platform_device *pdev)
{
	struct da9055_rtc *rtc;
	struct da9055_pdata *pdata = NULL;
	int ret, alm_irq;

	rtc = devm_kzalloc(&pdev->dev, sizeof(struct da9055_rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	rtc->da9055 = dev_get_drvdata(pdev->dev.parent);
	pdata = dev_get_platdata(rtc->da9055->dev);
	platform_set_drvdata(pdev, rtc);

	ret = da9055_rtc_device_init(rtc->da9055, pdata);
	if (ret < 0)
		goto err_rtc;

	ret = da9055_reg_read(rtc->da9055, DA9055_REG_ALARM_Y);
	if (ret < 0)
		goto err_rtc;

	if (ret & DA9055_RTC_ALM_EN)
		rtc->alarm_enable = 1;

	device_init_wakeup(&pdev->dev, 1);

	rtc->rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
					&da9055_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc->rtc)) {
		ret = PTR_ERR(rtc->rtc);
		goto err_rtc;
	}

	alm_irq = platform_get_irq_byname(pdev, "ALM");
	if (alm_irq < 0)
		return alm_irq;

	ret = devm_request_threaded_irq(&pdev->dev, alm_irq, NULL,
					da9055_rtc_alm_irq,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"ALM", rtc);
	if (ret != 0)
		dev_err(rtc->da9055->dev, "irq registration failed: %d\n", ret);

err_rtc:
	return ret;

}

#ifdef CONFIG_PM
/* Turn off the alarm if it should not be a wake source. */
static int da9055_rtc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct da9055_rtc *rtc = dev_get_drvdata(&pdev->dev);
	int ret;

	if (!device_may_wakeup(&pdev->dev)) {
		/* Disable the ALM IRQ */
		ret = da9055_rtc_enable_alarm(rtc, 0);
		if (ret < 0)
			dev_err(&pdev->dev, "Failed to disable RTC ALM\n");
	}

	return 0;
}

/* Enable the alarm if it should be enabled (in case it was disabled to
 * prevent use as a wake source).
 */
static int da9055_rtc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct da9055_rtc *rtc = dev_get_drvdata(&pdev->dev);
	int ret;

	if (!device_may_wakeup(&pdev->dev)) {
		if (rtc->alarm_enable) {
			ret = da9055_rtc_enable_alarm(rtc, 1);
			if (ret < 0)
				dev_err(&pdev->dev,
					"Failed to restart RTC ALM\n");
		}
	}

	return 0;
}

/* Unconditionally disable the alarm */
static int da9055_rtc_freeze(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct da9055_rtc *rtc = dev_get_drvdata(&pdev->dev);
	int ret;

	ret = da9055_rtc_enable_alarm(rtc, 0);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to freeze RTC ALMs\n");

	return 0;

}
#else
#define da9055_rtc_suspend NULL
#define da9055_rtc_resume NULL
#define da9055_rtc_freeze NULL
#endif

static const struct dev_pm_ops da9055_rtc_pm_ops = {
	.suspend = da9055_rtc_suspend,
	.resume = da9055_rtc_resume,

	.freeze = da9055_rtc_freeze,
	.thaw = da9055_rtc_resume,
	.restore = da9055_rtc_resume,

	.poweroff = da9055_rtc_suspend,
};

static struct platform_driver da9055_rtc_driver = {
	.probe  = da9055_rtc_probe,
	.driver = {
		.name   = "da9055-rtc",
		.pm = &da9055_rtc_pm_ops,
	},
};

module_platform_driver(da9055_rtc_driver);

MODULE_AUTHOR("David Dajun Chen <dchen@diasemi.com>");
MODULE_DESCRIPTION("RTC driver for Dialog DA9055 PMIC");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9055-rtc");
