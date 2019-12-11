// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 ROHM Semiconductors
// ROHM BD70528MWV watchdog driver

#include <linux/bcd.h>
#include <linux/kernel.h>
#include <linux/mfd/rohm-bd70528.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/watchdog.h>

/*
 * Max time we can set is 1 hour, 59 minutes and 59 seconds
 * and Minimum time is 1 second
 */
#define WDT_MAX_MS	((2 * 60 * 60 - 1) * 1000)
#define WDT_MIN_MS	1000
#define DEFAULT_TIMEOUT	60

#define WD_CTRL_MAGIC1 0x55
#define WD_CTRL_MAGIC2 0xAA

struct wdtbd70528 {
	struct device *dev;
	struct regmap *regmap;
	struct rohm_regmap_dev *mfd;
	struct watchdog_device wdt;
};

/**
 * bd70528_wdt_set - arm or disarm watchdog timer
 *
 * @data:	device data for the PMIC instance we want to operate on
 * @enable:	new state of WDT. zero to disable, non zero to enable
 * @old_state:	previous state of WDT will be filled here
 *
 * Arm or disarm WDT on BD70528 PMIC. Expected to be called only by
 * BD70528 RTC and BD70528 WDT drivers. The rtc_timer_lock must be taken
 * by calling bd70528_wdt_lock before calling bd70528_wdt_set.
 */
int bd70528_wdt_set(struct rohm_regmap_dev *data, int enable, int *old_state)
{
	int ret, i;
	unsigned int tmp;
	struct bd70528_data *bd70528 = container_of(data, struct bd70528_data,
						 chip);
	u8 wd_ctrl_arr[3] = { WD_CTRL_MAGIC1, WD_CTRL_MAGIC2, 0 };
	u8 *wd_ctrl = &wd_ctrl_arr[2];

	ret = regmap_read(bd70528->chip.regmap, BD70528_REG_WDT_CTRL, &tmp);
	if (ret)
		return ret;

	*wd_ctrl = (u8)tmp;

	if (old_state) {
		if (*wd_ctrl & BD70528_MASK_WDT_EN)
			*old_state |= BD70528_WDT_STATE_BIT;
		else
			*old_state &= ~BD70528_WDT_STATE_BIT;
		if ((!enable) == (!(*old_state & BD70528_WDT_STATE_BIT)))
			return 0;
	}

	if (enable) {
		if (*wd_ctrl & BD70528_MASK_WDT_EN)
			return 0;
		*wd_ctrl |= BD70528_MASK_WDT_EN;
	} else {
		if (*wd_ctrl & BD70528_MASK_WDT_EN)
			*wd_ctrl &= ~BD70528_MASK_WDT_EN;
		else
			return 0;
	}

	for (i = 0; i < 3; i++) {
		ret = regmap_write(bd70528->chip.regmap, BD70528_REG_WDT_CTRL,
				   wd_ctrl_arr[i]);
		if (ret)
			return ret;
	}

	ret = regmap_read(bd70528->chip.regmap, BD70528_REG_WDT_CTRL, &tmp);
	if ((tmp & BD70528_MASK_WDT_EN) != (*wd_ctrl & BD70528_MASK_WDT_EN)) {
		dev_err(bd70528->chip.dev,
			"Watchdog ctrl mismatch (hw) 0x%x (set) 0x%x\n",
			tmp, *wd_ctrl);
		ret = -EIO;
	}

	return ret;
}
EXPORT_SYMBOL(bd70528_wdt_set);

/**
 * bd70528_wdt_lock - take WDT lock
 *
 * @data:	device data for the PMIC instance we want to operate on
 *
 * Lock WDT for arming/disarming in order to avoid race condition caused
 * by WDT state changes initiated by WDT and RTC drivers.
 */
void bd70528_wdt_lock(struct rohm_regmap_dev *data)
{
	struct bd70528_data *bd70528 = container_of(data, struct bd70528_data,
						 chip);

	mutex_lock(&bd70528->rtc_timer_lock);
}
EXPORT_SYMBOL(bd70528_wdt_lock);

/**
 * bd70528_wdt_unlock - unlock WDT lock
 *
 * @data:	device data for the PMIC instance we want to operate on
 *
 * Unlock WDT lock which has previously been taken by call to
 * bd70528_wdt_lock.
 */
void bd70528_wdt_unlock(struct rohm_regmap_dev *data)
{
	struct bd70528_data *bd70528 = container_of(data, struct bd70528_data,
						 chip);

	mutex_unlock(&bd70528->rtc_timer_lock);
}
EXPORT_SYMBOL(bd70528_wdt_unlock);

static int bd70528_wdt_set_locked(struct wdtbd70528 *w, int enable)
{
	return bd70528_wdt_set(w->mfd, enable, NULL);
}

static int bd70528_wdt_change(struct wdtbd70528 *w, int enable)
{
	int ret;

	bd70528_wdt_lock(w->mfd);
	ret = bd70528_wdt_set_locked(w, enable);
	bd70528_wdt_unlock(w->mfd);

	return ret;
}

static int bd70528_wdt_start(struct watchdog_device *wdt)
{
	struct wdtbd70528 *w = watchdog_get_drvdata(wdt);

	dev_dbg(w->dev, "WDT ping...\n");
	return bd70528_wdt_change(w, 1);
}

static int bd70528_wdt_stop(struct watchdog_device *wdt)
{
	struct wdtbd70528 *w = watchdog_get_drvdata(wdt);

	dev_dbg(w->dev, "WDT stopping...\n");
	return bd70528_wdt_change(w, 0);
}

static int bd70528_wdt_set_timeout(struct watchdog_device *wdt,
				   unsigned int timeout)
{
	unsigned int hours;
	unsigned int minutes;
	unsigned int seconds;
	int ret;
	struct wdtbd70528 *w = watchdog_get_drvdata(wdt);

	seconds = timeout;
	hours = timeout / (60 * 60);
	/* Maximum timeout is 1h 59m 59s => hours is 1 or 0 */
	if (hours)
		seconds -= (60 * 60);
	minutes = seconds / 60;
	seconds = seconds % 60;

	bd70528_wdt_lock(w->mfd);

	ret = bd70528_wdt_set_locked(w, 0);
	if (ret)
		goto out_unlock;

	ret = regmap_update_bits(w->regmap, BD70528_REG_WDT_HOUR,
				 BD70528_MASK_WDT_HOUR, hours);
	if (ret) {
		dev_err(w->dev, "Failed to set WDT hours\n");
		goto out_en_unlock;
	}
	ret = regmap_update_bits(w->regmap, BD70528_REG_WDT_MINUTE,
				 BD70528_MASK_WDT_MINUTE, bin2bcd(minutes));
	if (ret) {
		dev_err(w->dev, "Failed to set WDT minutes\n");
		goto out_en_unlock;
	}
	ret = regmap_update_bits(w->regmap, BD70528_REG_WDT_SEC,
				 BD70528_MASK_WDT_SEC, bin2bcd(seconds));
	if (ret)
		dev_err(w->dev, "Failed to set WDT seconds\n");
	else
		dev_dbg(w->dev, "WDT tmo set to %u\n", timeout);

out_en_unlock:
	ret = bd70528_wdt_set_locked(w, 1);
out_unlock:
	bd70528_wdt_unlock(w->mfd);

	return ret;
}

static const struct watchdog_info bd70528_wdt_info = {
	.identity = "bd70528-wdt",
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops bd70528_wdt_ops = {
	.start		= bd70528_wdt_start,
	.stop		= bd70528_wdt_stop,
	.set_timeout	= bd70528_wdt_set_timeout,
};

static int bd70528_wdt_probe(struct platform_device *pdev)
{
	struct rohm_regmap_dev *bd70528;
	struct wdtbd70528 *w;
	int ret;
	unsigned int reg;

	bd70528 = dev_get_drvdata(pdev->dev.parent);
	if (!bd70528) {
		dev_err(&pdev->dev, "No MFD driver data\n");
		return -EINVAL;
	}
	w = devm_kzalloc(&pdev->dev, sizeof(*w), GFP_KERNEL);
	if (!w)
		return -ENOMEM;

	w->regmap = bd70528->regmap;
	w->mfd = bd70528;
	w->dev = &pdev->dev;

	w->wdt.info = &bd70528_wdt_info;
	w->wdt.ops =  &bd70528_wdt_ops;
	w->wdt.min_hw_heartbeat_ms = WDT_MIN_MS;
	w->wdt.max_hw_heartbeat_ms = WDT_MAX_MS;
	w->wdt.parent = pdev->dev.parent;
	w->wdt.timeout = DEFAULT_TIMEOUT;
	watchdog_set_drvdata(&w->wdt, w);
	watchdog_init_timeout(&w->wdt, 0, pdev->dev.parent);

	ret = bd70528_wdt_set_timeout(&w->wdt, w->wdt.timeout);
	if (ret) {
		dev_err(&pdev->dev, "Failed to set the watchdog timeout\n");
		return ret;
	}

	bd70528_wdt_lock(w->mfd);
	ret = regmap_read(w->regmap, BD70528_REG_WDT_CTRL, &reg);
	bd70528_wdt_unlock(w->mfd);

	if (ret) {
		dev_err(&pdev->dev, "Failed to get the watchdog state\n");
		return ret;
	}
	if (reg & BD70528_MASK_WDT_EN) {
		dev_dbg(&pdev->dev, "watchdog was running during probe\n");
		set_bit(WDOG_HW_RUNNING, &w->wdt.status);
	}

	ret = devm_watchdog_register_device(&pdev->dev, &w->wdt);
	if (ret < 0)
		dev_err(&pdev->dev, "watchdog registration failed: %d\n", ret);

	return ret;
}

static struct platform_driver bd70528_wdt = {
	.driver = {
		.name = "bd70528-wdt"
	},
	.probe = bd70528_wdt_probe,
};

module_platform_driver(bd70528_wdt);

MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("BD70528 watchdog driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:bd70528-wdt");
