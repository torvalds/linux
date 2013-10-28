/*
 *	Real Time Clock driver for Wolfson Microelectronics WM831x
 *
 *	Copyright (C) 2009 Wolfson Microelectronics PLC.
 *
 *  Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/bcd.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/completion.h>
#include <linux/mfd/wm831x/core.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/random.h>

/*
 * R16416 (0x4020) - RTC Write Counter
 */
#define WM831X_RTC_WR_CNT_MASK                  0xFFFF  /* RTC_WR_CNT - [15:0] */
#define WM831X_RTC_WR_CNT_SHIFT                      0  /* RTC_WR_CNT - [15:0] */
#define WM831X_RTC_WR_CNT_WIDTH                     16  /* RTC_WR_CNT - [15:0] */

/*
 * R16417 (0x4021) - RTC Time 1
 */
#define WM831X_RTC_TIME_MASK                    0xFFFF  /* RTC_TIME - [15:0] */
#define WM831X_RTC_TIME_SHIFT                        0  /* RTC_TIME - [15:0] */
#define WM831X_RTC_TIME_WIDTH                       16  /* RTC_TIME - [15:0] */

/*
 * R16418 (0x4022) - RTC Time 2
 */
#define WM831X_RTC_TIME_MASK                    0xFFFF  /* RTC_TIME - [15:0] */
#define WM831X_RTC_TIME_SHIFT                        0  /* RTC_TIME - [15:0] */
#define WM831X_RTC_TIME_WIDTH                       16  /* RTC_TIME - [15:0] */

/*
 * R16419 (0x4023) - RTC Alarm 1
 */
#define WM831X_RTC_ALM_MASK                     0xFFFF  /* RTC_ALM - [15:0] */
#define WM831X_RTC_ALM_SHIFT                         0  /* RTC_ALM - [15:0] */
#define WM831X_RTC_ALM_WIDTH                        16  /* RTC_ALM - [15:0] */

/*
 * R16420 (0x4024) - RTC Alarm 2
 */
#define WM831X_RTC_ALM_MASK                     0xFFFF  /* RTC_ALM - [15:0] */
#define WM831X_RTC_ALM_SHIFT                         0  /* RTC_ALM - [15:0] */
#define WM831X_RTC_ALM_WIDTH                        16  /* RTC_ALM - [15:0] */

/*
 * R16421 (0x4025) - RTC Control
 */
#define WM831X_RTC_VALID                        0x8000  /* RTC_VALID */
#define WM831X_RTC_VALID_MASK                   0x8000  /* RTC_VALID */
#define WM831X_RTC_VALID_SHIFT                      15  /* RTC_VALID */
#define WM831X_RTC_VALID_WIDTH                       1  /* RTC_VALID */
#define WM831X_RTC_SYNC_BUSY                    0x4000  /* RTC_SYNC_BUSY */
#define WM831X_RTC_SYNC_BUSY_MASK               0x4000  /* RTC_SYNC_BUSY */
#define WM831X_RTC_SYNC_BUSY_SHIFT                  14  /* RTC_SYNC_BUSY */
#define WM831X_RTC_SYNC_BUSY_WIDTH                   1  /* RTC_SYNC_BUSY */
#define WM831X_RTC_ALM_ENA                      0x0400  /* RTC_ALM_ENA */
#define WM831X_RTC_ALM_ENA_MASK                 0x0400  /* RTC_ALM_ENA */
#define WM831X_RTC_ALM_ENA_SHIFT                    10  /* RTC_ALM_ENA */
#define WM831X_RTC_ALM_ENA_WIDTH                     1  /* RTC_ALM_ENA */
#define WM831X_RTC_PINT_FREQ_MASK               0x0070  /* RTC_PINT_FREQ - [6:4] */
#define WM831X_RTC_PINT_FREQ_SHIFT                   4  /* RTC_PINT_FREQ - [6:4] */
#define WM831X_RTC_PINT_FREQ_WIDTH                   3  /* RTC_PINT_FREQ - [6:4] */

/*
 * R16422 (0x4026) - RTC Trim
 */
#define WM831X_RTC_TRIM_MASK                    0x03FF  /* RTC_TRIM - [9:0] */
#define WM831X_RTC_TRIM_SHIFT                        0  /* RTC_TRIM - [9:0] */
#define WM831X_RTC_TRIM_WIDTH                       10  /* RTC_TRIM - [9:0] */

#define WM831X_SET_TIME_RETRIES	5
#define WM831X_GET_TIME_RETRIES	5

struct wm831x_rtc {
	struct wm831x *wm831x;
	struct rtc_device *rtc;
	unsigned int alarm_enabled:1;
};

static void wm831x_rtc_add_randomness(struct wm831x *wm831x)
{
	int ret;
	u16 reg;

	/*
	 * The write counter contains a pseudo-random number which is
	 * regenerated every time we set the RTC so it should be a
	 * useful per-system source of entropy.
	 */
	ret = wm831x_reg_read(wm831x, WM831X_RTC_WRITE_COUNTER);
	if (ret >= 0) {
		reg = ret;
		add_device_randomness(&reg, sizeof(reg));
	} else {
		dev_warn(wm831x->dev, "Failed to read RTC write counter: %d\n",
			 ret);
	}
}

/*
 * Read current time and date in RTC
 */
static int wm831x_rtc_readtime(struct device *dev, struct rtc_time *tm)
{
	struct wm831x_rtc *wm831x_rtc = dev_get_drvdata(dev);
	struct wm831x *wm831x = wm831x_rtc->wm831x;
	u16 time1[2], time2[2];
	int ret;
	int count = 0;

	/* Has the RTC been programmed? */
	ret = wm831x_reg_read(wm831x, WM831X_RTC_CONTROL);
	if (ret < 0) {
		dev_err(dev, "Failed to read RTC control: %d\n", ret);
		return ret;
	}
	if (!(ret & WM831X_RTC_VALID)) {
		dev_dbg(dev, "RTC not yet configured\n");
		return -EINVAL;
	}

	/* Read twice to make sure we don't read a corrupt, partially
	 * incremented, value.
	 */
	do {
		ret = wm831x_bulk_read(wm831x, WM831X_RTC_TIME_1,
				       2, time1);
		if (ret != 0)
			continue;

		ret = wm831x_bulk_read(wm831x, WM831X_RTC_TIME_1,
				       2, time2);
		if (ret != 0)
			continue;

		if (memcmp(time1, time2, sizeof(time1)) == 0) {
			u32 time = (time1[0] << 16) | time1[1];

			rtc_time_to_tm(time, tm);
			return rtc_valid_tm(tm);
		}

	} while (++count < WM831X_GET_TIME_RETRIES);

	dev_err(dev, "Timed out reading current time\n");

	return -EIO;
}

/*
 * Set current time and date in RTC
 */
static int wm831x_rtc_set_mmss(struct device *dev, unsigned long time)
{
	struct wm831x_rtc *wm831x_rtc = dev_get_drvdata(dev);
	struct wm831x *wm831x = wm831x_rtc->wm831x;
	struct rtc_time new_tm;
	unsigned long new_time;
	int ret;
	int count = 0;

	ret = wm831x_reg_write(wm831x, WM831X_RTC_TIME_1,
			       (time >> 16) & 0xffff);
	if (ret < 0) {
		dev_err(dev, "Failed to write TIME_1: %d\n", ret);
		return ret;
	}

	ret = wm831x_reg_write(wm831x, WM831X_RTC_TIME_2, time & 0xffff);
	if (ret < 0) {
		dev_err(dev, "Failed to write TIME_2: %d\n", ret);
		return ret;
	}

	/* Wait for the update to complete - should happen first time
	 * round but be conservative.
	 */
	do {
		msleep(1);

		ret = wm831x_reg_read(wm831x, WM831X_RTC_CONTROL);
		if (ret < 0)
			ret = WM831X_RTC_SYNC_BUSY;
	} while (!(ret & WM831X_RTC_SYNC_BUSY) &&
		 ++count < WM831X_SET_TIME_RETRIES);

	if (ret & WM831X_RTC_SYNC_BUSY) {
		dev_err(dev, "Timed out writing RTC update\n");
		return -EIO;
	}

	/* Check that the update was accepted; security features may
	 * have caused the update to be ignored.
	 */
	ret = wm831x_rtc_readtime(dev, &new_tm);
	if (ret < 0)
		return ret;

	ret = rtc_tm_to_time(&new_tm, &new_time);
	if (ret < 0) {
		dev_err(dev, "Failed to convert time: %d\n", ret);
		return ret;
	}

	/* Allow a second of change in case of tick */
	if (new_time - time > 1) {
		dev_err(dev, "RTC update not permitted by hardware\n");
		return -EPERM;
	}

	return 0;
}

/*
 * Read alarm time and date in RTC
 */
static int wm831x_rtc_readalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct wm831x_rtc *wm831x_rtc = dev_get_drvdata(dev);
	int ret;
	u16 data[2];
	u32 time;

	ret = wm831x_bulk_read(wm831x_rtc->wm831x, WM831X_RTC_ALARM_1,
			       2, data);
	if (ret != 0) {
		dev_err(dev, "Failed to read alarm time: %d\n", ret);
		return ret;
	}

	time = (data[0] << 16) | data[1];

	rtc_time_to_tm(time, &alrm->time);

	ret = wm831x_reg_read(wm831x_rtc->wm831x, WM831X_RTC_CONTROL);
	if (ret < 0) {
		dev_err(dev, "Failed to read RTC control: %d\n", ret);
		return ret;
	}

	if (ret & WM831X_RTC_ALM_ENA)
		alrm->enabled = 1;
	else
		alrm->enabled = 0;

	return 0;
}

static int wm831x_rtc_stop_alarm(struct wm831x_rtc *wm831x_rtc)
{
	wm831x_rtc->alarm_enabled = 0;

	return wm831x_set_bits(wm831x_rtc->wm831x, WM831X_RTC_CONTROL,
			       WM831X_RTC_ALM_ENA, 0);
}

static int wm831x_rtc_start_alarm(struct wm831x_rtc *wm831x_rtc)
{
	wm831x_rtc->alarm_enabled = 1;

	return wm831x_set_bits(wm831x_rtc->wm831x, WM831X_RTC_CONTROL,
			       WM831X_RTC_ALM_ENA, WM831X_RTC_ALM_ENA);
}

static int wm831x_rtc_setalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct wm831x_rtc *wm831x_rtc = dev_get_drvdata(dev);
	struct wm831x *wm831x = wm831x_rtc->wm831x;
	int ret;
	unsigned long time;

	ret = rtc_tm_to_time(&alrm->time, &time);
	if (ret < 0) {
		dev_err(dev, "Failed to convert time: %d\n", ret);
		return ret;
	}

	ret = wm831x_rtc_stop_alarm(wm831x_rtc);
	if (ret < 0) {
		dev_err(dev, "Failed to stop alarm: %d\n", ret);
		return ret;
	}

	ret = wm831x_reg_write(wm831x, WM831X_RTC_ALARM_1,
			       (time >> 16) & 0xffff);
	if (ret < 0) {
		dev_err(dev, "Failed to write ALARM_1: %d\n", ret);
		return ret;
	}

	ret = wm831x_reg_write(wm831x, WM831X_RTC_ALARM_2, time & 0xffff);
	if (ret < 0) {
		dev_err(dev, "Failed to write ALARM_2: %d\n", ret);
		return ret;
	}

	if (alrm->enabled) {
		ret = wm831x_rtc_start_alarm(wm831x_rtc);
		if (ret < 0) {
			dev_err(dev, "Failed to start alarm: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int wm831x_rtc_alarm_irq_enable(struct device *dev,
				       unsigned int enabled)
{
	struct wm831x_rtc *wm831x_rtc = dev_get_drvdata(dev);

	if (enabled)
		return wm831x_rtc_start_alarm(wm831x_rtc);
	else
		return wm831x_rtc_stop_alarm(wm831x_rtc);
}

static irqreturn_t wm831x_alm_irq(int irq, void *data)
{
	struct wm831x_rtc *wm831x_rtc = data;

	rtc_update_irq(wm831x_rtc->rtc, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops wm831x_rtc_ops = {
	.read_time = wm831x_rtc_readtime,
	.set_mmss = wm831x_rtc_set_mmss,
	.read_alarm = wm831x_rtc_readalarm,
	.set_alarm = wm831x_rtc_setalarm,
	.alarm_irq_enable = wm831x_rtc_alarm_irq_enable,
};

#ifdef CONFIG_PM
/* Turn off the alarm if it should not be a wake source. */
static int wm831x_rtc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct wm831x_rtc *wm831x_rtc = dev_get_drvdata(&pdev->dev);
	int ret, enable;

	if (wm831x_rtc->alarm_enabled && device_may_wakeup(&pdev->dev))
		enable = WM831X_RTC_ALM_ENA;
	else
		enable = 0;

	ret = wm831x_set_bits(wm831x_rtc->wm831x, WM831X_RTC_CONTROL,
			      WM831X_RTC_ALM_ENA, enable);
	if (ret != 0)
		dev_err(&pdev->dev, "Failed to update RTC alarm: %d\n", ret);

	return 0;
}

/* Enable the alarm if it should be enabled (in case it was disabled to
 * prevent use as a wake source).
 */
static int wm831x_rtc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct wm831x_rtc *wm831x_rtc = dev_get_drvdata(&pdev->dev);
	int ret;

	if (wm831x_rtc->alarm_enabled) {
		ret = wm831x_rtc_start_alarm(wm831x_rtc);
		if (ret != 0)
			dev_err(&pdev->dev,
				"Failed to restart RTC alarm: %d\n", ret);
	}

	return 0;
}

/* Unconditionally disable the alarm */
static int wm831x_rtc_freeze(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct wm831x_rtc *wm831x_rtc = dev_get_drvdata(&pdev->dev);
	int ret;

	ret = wm831x_set_bits(wm831x_rtc->wm831x, WM831X_RTC_CONTROL,
			      WM831X_RTC_ALM_ENA, 0);
	if (ret != 0)
		dev_err(&pdev->dev, "Failed to stop RTC alarm: %d\n", ret);

	return 0;
}
#else
#define wm831x_rtc_suspend NULL
#define wm831x_rtc_resume NULL
#define wm831x_rtc_freeze NULL
#endif

static int wm831x_rtc_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_rtc *wm831x_rtc;
	int alm_irq = platform_get_irq_byname(pdev, "ALM");
	int ret = 0;

	wm831x_rtc = devm_kzalloc(&pdev->dev, sizeof(*wm831x_rtc), GFP_KERNEL);
	if (wm831x_rtc == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, wm831x_rtc);
	wm831x_rtc->wm831x = wm831x;

	ret = wm831x_reg_read(wm831x, WM831X_RTC_CONTROL);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to read RTC control: %d\n", ret);
		goto err;
	}
	if (ret & WM831X_RTC_ALM_ENA)
		wm831x_rtc->alarm_enabled = 1;

	device_init_wakeup(&pdev->dev, 1);

	wm831x_rtc->rtc = rtc_device_register("wm831x", &pdev->dev,
					      &wm831x_rtc_ops, THIS_MODULE);
	if (IS_ERR(wm831x_rtc->rtc)) {
		ret = PTR_ERR(wm831x_rtc->rtc);
		goto err;
	}

	ret = request_threaded_irq(alm_irq, NULL, wm831x_alm_irq,
				   IRQF_TRIGGER_RISING, "RTC alarm",
				   wm831x_rtc);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request alarm IRQ %d: %d\n",
			alm_irq, ret);
	}

	wm831x_rtc_add_randomness(wm831x);

	return 0;

err:
	return ret;
}

static int __devexit wm831x_rtc_remove(struct platform_device *pdev)
{
	struct wm831x_rtc *wm831x_rtc = platform_get_drvdata(pdev);
	int alm_irq = platform_get_irq_byname(pdev, "ALM");

	free_irq(alm_irq, wm831x_rtc);
	rtc_device_unregister(wm831x_rtc->rtc);

	return 0;
}

static const struct dev_pm_ops wm831x_rtc_pm_ops = {
	.suspend = wm831x_rtc_suspend,
	.resume = wm831x_rtc_resume,

	.freeze = wm831x_rtc_freeze,
	.thaw = wm831x_rtc_resume,
	.restore = wm831x_rtc_resume,

	.poweroff = wm831x_rtc_suspend,
};

static struct platform_driver wm831x_rtc_driver = {
	.probe = wm831x_rtc_probe,
	.remove = __devexit_p(wm831x_rtc_remove),
	.driver = {
		.name = "wm831x-rtc",
		.pm = &wm831x_rtc_pm_ops,
	},
};

module_platform_driver(wm831x_rtc_driver);

MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("RTC driver for the WM831x series PMICs");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm831x-rtc");
