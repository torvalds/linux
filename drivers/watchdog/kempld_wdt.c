/*
 * Kontron PLD watchdog driver
 *
 * Copyright (c) 2010-2013 Kontron Europe GmbH
 * Author: Michael Brunner <michael.brunner@kontron.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Note: From the PLD watchdog point of view timeout and pretimeout are
 *       defined differently than in the kernel.
 *       First the pretimeout stage runs out before the timeout stage gets
 *       active.
 *
 * Kernel/API:                     P-----| pretimeout
 *               |-----------------------T timeout
 * Watchdog:     |-----------------P       pretimeout_stage
 *                                 |-----T timeout_stage
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/uaccess.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/mfd/kempld.h>

#define KEMPLD_WDT_STAGE_TIMEOUT(x)	(0x1b + (x) * 4)
#define KEMPLD_WDT_STAGE_CFG(x)		(0x18 + (x))
#define STAGE_CFG_GET_PRESCALER(x)	(((x) & 0x30) >> 4)
#define STAGE_CFG_SET_PRESCALER(x)	(((x) & 0x3) << 4)
#define STAGE_CFG_PRESCALER_MASK	0x30
#define STAGE_CFG_ACTION_MASK		0x7
#define STAGE_CFG_ASSERT		(1 << 3)

#define KEMPLD_WDT_MAX_STAGES		2
#define KEMPLD_WDT_KICK			0x16
#define KEMPLD_WDT_CFG			0x17
#define KEMPLD_WDT_CFG_ENABLE		0x10
#define KEMPLD_WDT_CFG_ENABLE_LOCK	0x8
#define KEMPLD_WDT_CFG_GLOBAL_LOCK	0x80

enum {
	ACTION_NONE = 0,
	ACTION_RESET,
	ACTION_NMI,
	ACTION_SMI,
	ACTION_SCI,
	ACTION_DELAY,
};

enum {
	STAGE_TIMEOUT = 0,
	STAGE_PRETIMEOUT,
};

enum {
	PRESCALER_21 = 0,
	PRESCALER_17,
	PRESCALER_12,
};

static const u32 kempld_prescaler[] = {
	[PRESCALER_21] = (1 << 21) - 1,
	[PRESCALER_17] = (1 << 17) - 1,
	[PRESCALER_12] = (1 << 12) - 1,
	0,
};

struct kempld_wdt_stage {
	unsigned int	id;
	u32		mask;
};

struct kempld_wdt_data {
	struct kempld_device_data	*pld;
	struct watchdog_device		wdd;
	unsigned int			pretimeout;
	struct kempld_wdt_stage		stage[KEMPLD_WDT_MAX_STAGES];
#ifdef CONFIG_PM
	u8				pm_status_store;
#endif
};

#define DEFAULT_TIMEOUT		30 /* seconds */
#define DEFAULT_PRETIMEOUT	0

static unsigned int timeout = DEFAULT_TIMEOUT;
module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout,
	"Watchdog timeout in seconds. (>=0, default="
	__MODULE_STRING(DEFAULT_TIMEOUT) ")");

static unsigned int pretimeout = DEFAULT_PRETIMEOUT;
module_param(pretimeout, uint, 0);
MODULE_PARM_DESC(pretimeout,
	"Watchdog pretimeout in seconds. (>=0, default="
	__MODULE_STRING(DEFAULT_PRETIMEOUT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
	"Watchdog cannot be stopped once started (default="
	__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int kempld_wdt_set_stage_action(struct kempld_wdt_data *wdt_data,
					struct kempld_wdt_stage *stage,
					u8 action)
{
	struct kempld_device_data *pld = wdt_data->pld;
	u8 stage_cfg;

	if (!stage || !stage->mask)
		return -EINVAL;

	kempld_get_mutex(pld);
	stage_cfg = kempld_read8(pld, KEMPLD_WDT_STAGE_CFG(stage->id));
	stage_cfg &= ~STAGE_CFG_ACTION_MASK;
	stage_cfg |= (action & STAGE_CFG_ACTION_MASK);

	if (action == ACTION_RESET)
		stage_cfg |= STAGE_CFG_ASSERT;
	else
		stage_cfg &= ~STAGE_CFG_ASSERT;

	kempld_write8(pld, KEMPLD_WDT_STAGE_CFG(stage->id), stage_cfg);
	kempld_release_mutex(pld);

	return 0;
}

static int kempld_wdt_set_stage_timeout(struct kempld_wdt_data *wdt_data,
					struct kempld_wdt_stage *stage,
					unsigned int timeout)
{
	struct kempld_device_data *pld = wdt_data->pld;
	u32 prescaler = kempld_prescaler[PRESCALER_21];
	u64 stage_timeout64;
	u32 stage_timeout;
	u32 remainder;
	u8 stage_cfg;

	if (!stage)
		return -EINVAL;

	stage_timeout64 = (u64)timeout * pld->pld_clock;
	remainder = do_div(stage_timeout64, prescaler);
	if (remainder)
		stage_timeout64++;

	if (stage_timeout64 > stage->mask)
		return -EINVAL;

	stage_timeout = stage_timeout64 & stage->mask;

	kempld_get_mutex(pld);
	stage_cfg = kempld_read8(pld, KEMPLD_WDT_STAGE_CFG(stage->id));
	stage_cfg &= ~STAGE_CFG_PRESCALER_MASK;
	stage_cfg |= STAGE_CFG_SET_PRESCALER(PRESCALER_21);
	kempld_write8(pld, KEMPLD_WDT_STAGE_CFG(stage->id), stage_cfg);
	kempld_write32(pld, KEMPLD_WDT_STAGE_TIMEOUT(stage->id),
			stage_timeout);
	kempld_release_mutex(pld);

	return 0;
}

/*
 * kempld_get_mutex must be called prior to calling this function.
 */
static unsigned int kempld_wdt_get_timeout(struct kempld_wdt_data *wdt_data,
						struct kempld_wdt_stage *stage)
{
	struct kempld_device_data *pld = wdt_data->pld;
	unsigned int timeout;
	u64 stage_timeout;
	u32 prescaler;
	u32 remainder;
	u8 stage_cfg;

	if (!stage->mask)
		return 0;

	stage_cfg = kempld_read8(pld, KEMPLD_WDT_STAGE_CFG(stage->id));
	stage_timeout = kempld_read32(pld, KEMPLD_WDT_STAGE_TIMEOUT(stage->id));
	prescaler = kempld_prescaler[STAGE_CFG_GET_PRESCALER(stage_cfg)];

	stage_timeout = (stage_timeout & stage->mask) * prescaler;
	remainder = do_div(stage_timeout, pld->pld_clock);
	if (remainder)
		stage_timeout++;

	timeout = stage_timeout;
	WARN_ON_ONCE(timeout != stage_timeout);

	return timeout;
}

static int kempld_wdt_set_timeout(struct watchdog_device *wdd,
					unsigned int timeout)
{
	struct kempld_wdt_data *wdt_data = watchdog_get_drvdata(wdd);
	struct kempld_wdt_stage *pretimeout_stage;
	struct kempld_wdt_stage *timeout_stage;
	int ret;

	timeout_stage = &wdt_data->stage[STAGE_TIMEOUT];
	pretimeout_stage = &wdt_data->stage[STAGE_PRETIMEOUT];

	if (pretimeout_stage->mask && wdt_data->pretimeout > 0)
		timeout = wdt_data->pretimeout;

	ret = kempld_wdt_set_stage_action(wdt_data, timeout_stage,
						ACTION_RESET);
	if (ret)
		return ret;
	ret = kempld_wdt_set_stage_timeout(wdt_data, timeout_stage,
						timeout);
	if (ret)
		return ret;

	wdd->timeout = timeout;
	return 0;
}

static int kempld_wdt_set_pretimeout(struct watchdog_device *wdd,
					unsigned int pretimeout)
{
	struct kempld_wdt_data *wdt_data = watchdog_get_drvdata(wdd);
	struct kempld_wdt_stage *pretimeout_stage;
	u8 action = ACTION_NONE;
	int ret;

	pretimeout_stage = &wdt_data->stage[STAGE_PRETIMEOUT];

	if (!pretimeout_stage->mask)
		return -ENXIO;

	if (pretimeout > wdd->timeout)
		return -EINVAL;

	if (pretimeout > 0)
		action = ACTION_NMI;

	ret = kempld_wdt_set_stage_action(wdt_data, pretimeout_stage,
						action);
	if (ret)
		return ret;
	ret = kempld_wdt_set_stage_timeout(wdt_data, pretimeout_stage,
						wdd->timeout - pretimeout);
	if (ret)
		return ret;

	wdt_data->pretimeout = pretimeout;
	return 0;
}

static void kempld_wdt_update_timeouts(struct kempld_wdt_data *wdt_data)
{
	struct kempld_device_data *pld = wdt_data->pld;
	struct kempld_wdt_stage *pretimeout_stage;
	struct kempld_wdt_stage *timeout_stage;
	unsigned int pretimeout, timeout;

	pretimeout_stage = &wdt_data->stage[STAGE_PRETIMEOUT];
	timeout_stage = &wdt_data->stage[STAGE_TIMEOUT];

	kempld_get_mutex(pld);
	pretimeout = kempld_wdt_get_timeout(wdt_data, pretimeout_stage);
	timeout = kempld_wdt_get_timeout(wdt_data, timeout_stage);
	kempld_release_mutex(pld);

	if (pretimeout)
		wdt_data->pretimeout = timeout;
	else
		wdt_data->pretimeout = 0;

	wdt_data->wdd.timeout = pretimeout + timeout;
}

static int kempld_wdt_start(struct watchdog_device *wdd)
{
	struct kempld_wdt_data *wdt_data = watchdog_get_drvdata(wdd);
	struct kempld_device_data *pld = wdt_data->pld;
	u8 status;
	int ret;

	ret = kempld_wdt_set_timeout(wdd, wdd->timeout);
	if (ret)
		return ret;

	kempld_get_mutex(pld);
	status = kempld_read8(pld, KEMPLD_WDT_CFG);
	status |= KEMPLD_WDT_CFG_ENABLE;
	kempld_write8(pld, KEMPLD_WDT_CFG, status);
	status = kempld_read8(pld, KEMPLD_WDT_CFG);
	kempld_release_mutex(pld);

	/* Check if the watchdog was enabled */
	if (!(status & KEMPLD_WDT_CFG_ENABLE))
		return -EACCES;

	return 0;
}

static int kempld_wdt_stop(struct watchdog_device *wdd)
{
	struct kempld_wdt_data *wdt_data = watchdog_get_drvdata(wdd);
	struct kempld_device_data *pld = wdt_data->pld;
	u8 status;

	kempld_get_mutex(pld);
	status = kempld_read8(pld, KEMPLD_WDT_CFG);
	status &= ~KEMPLD_WDT_CFG_ENABLE;
	kempld_write8(pld, KEMPLD_WDT_CFG, status);
	status = kempld_read8(pld, KEMPLD_WDT_CFG);
	kempld_release_mutex(pld);

	/* Check if the watchdog was disabled */
	if (status & KEMPLD_WDT_CFG_ENABLE)
		return -EACCES;

	return 0;
}

static int kempld_wdt_keepalive(struct watchdog_device *wdd)
{
	struct kempld_wdt_data *wdt_data = watchdog_get_drvdata(wdd);
	struct kempld_device_data *pld = wdt_data->pld;

	kempld_get_mutex(pld);
	kempld_write8(pld, KEMPLD_WDT_KICK, 'K');
	kempld_release_mutex(pld);

	return 0;
}

static long kempld_wdt_ioctl(struct watchdog_device *wdd, unsigned int cmd,
				unsigned long arg)
{
	struct kempld_wdt_data *wdt_data = watchdog_get_drvdata(wdd);
	void __user *argp = (void __user *)arg;
	int ret = -ENOIOCTLCMD;
	int __user *p = argp;
	int new_value;

	switch (cmd) {
	case WDIOC_SETPRETIMEOUT:
		if (get_user(new_value, p))
			return -EFAULT;
		ret = kempld_wdt_set_pretimeout(wdd, new_value);
		if (ret)
			return ret;
		ret = kempld_wdt_keepalive(wdd);
		break;
	case WDIOC_GETPRETIMEOUT:
		ret = put_user(wdt_data->pretimeout, (int __user *)arg);
		break;
	}

	return ret;
}

static int kempld_wdt_probe_stages(struct watchdog_device *wdd)
{
	struct kempld_wdt_data *wdt_data = watchdog_get_drvdata(wdd);
	struct kempld_device_data *pld = wdt_data->pld;
	struct kempld_wdt_stage *pretimeout_stage;
	struct kempld_wdt_stage *timeout_stage;
	u8 index, data, data_orig;
	u32 mask;
	int i, j;

	pretimeout_stage = &wdt_data->stage[STAGE_PRETIMEOUT];
	timeout_stage = &wdt_data->stage[STAGE_TIMEOUT];

	pretimeout_stage->mask = 0;
	timeout_stage->mask = 0;

	for (i = 0; i < 3; i++) {
		index = KEMPLD_WDT_STAGE_TIMEOUT(i);
		mask = 0;

		kempld_get_mutex(pld);
		/* Probe each byte individually. */
		for (j = 0; j < 4; j++) {
			data_orig = kempld_read8(pld, index + j);
			kempld_write8(pld, index + j, 0x00);
			data = kempld_read8(pld, index + j);
			/* A failed write means this byte is reserved */
			if (data != 0x00)
				break;
			kempld_write8(pld, index + j, data_orig);
			mask |= 0xff << (j * 8);
		}
		kempld_release_mutex(pld);

		/* Assign available stages to timeout and pretimeout */
		if (!timeout_stage->mask) {
			timeout_stage->mask = mask;
			timeout_stage->id = i;
		} else {
			if (pld->feature_mask & KEMPLD_FEATURE_BIT_NMI) {
				pretimeout_stage->mask = timeout_stage->mask;
				timeout_stage->mask = mask;
				pretimeout_stage->id = timeout_stage->id;
				timeout_stage->id = i;
			}
			break;
		}
	}

	if (!timeout_stage->mask)
		return -ENODEV;

	return 0;
}

static struct watchdog_info kempld_wdt_info = {
	.identity	= "KEMPLD Watchdog",
	.options	= WDIOF_SETTIMEOUT |
			WDIOF_KEEPALIVEPING |
			WDIOF_MAGICCLOSE |
			WDIOF_PRETIMEOUT
};

static struct watchdog_ops kempld_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= kempld_wdt_start,
	.stop		= kempld_wdt_stop,
	.ping		= kempld_wdt_keepalive,
	.set_timeout	= kempld_wdt_set_timeout,
	.ioctl		= kempld_wdt_ioctl,
};

static int kempld_wdt_probe(struct platform_device *pdev)
{
	struct kempld_device_data *pld = dev_get_drvdata(pdev->dev.parent);
	struct kempld_wdt_data *wdt_data;
	struct device *dev = &pdev->dev;
	struct watchdog_device *wdd;
	u8 status;
	int ret = 0;

	wdt_data = devm_kzalloc(dev, sizeof(*wdt_data), GFP_KERNEL);
	if (!wdt_data)
		return -ENOMEM;

	wdt_data->pld = pld;
	wdd = &wdt_data->wdd;
	wdd->parent = dev;

	kempld_get_mutex(pld);
	status = kempld_read8(pld, KEMPLD_WDT_CFG);
	kempld_release_mutex(pld);

	/* Enable nowayout if watchdog is already locked */
	if (status & (KEMPLD_WDT_CFG_ENABLE_LOCK |
			KEMPLD_WDT_CFG_GLOBAL_LOCK)) {
		if (!nowayout)
			dev_warn(dev,
				"Forcing nowayout - watchdog lock enabled!\n");
		nowayout = true;
	}

	wdd->info = &kempld_wdt_info;
	wdd->ops = &kempld_wdt_ops;

	watchdog_set_drvdata(wdd, wdt_data);
	watchdog_set_nowayout(wdd, nowayout);

	ret = kempld_wdt_probe_stages(wdd);
	if (ret)
		return ret;

	kempld_wdt_set_timeout(wdd, timeout);
	kempld_wdt_set_pretimeout(wdd, pretimeout);

	/* Check if watchdog is already enabled */
	if (status & KEMPLD_WDT_CFG_ENABLE) {
		/* Get current watchdog settings */
		kempld_wdt_update_timeouts(wdt_data);
		dev_info(dev, "Watchdog was already enabled\n");
	}

	platform_set_drvdata(pdev, wdt_data);
	ret = watchdog_register_device(wdd);
	if (ret)
		return ret;

	dev_info(dev, "Watchdog registered with %ds timeout\n", wdd->timeout);

	return 0;
}

static void kempld_wdt_shutdown(struct platform_device *pdev)
{
	struct kempld_wdt_data *wdt_data = platform_get_drvdata(pdev);

	kempld_wdt_stop(&wdt_data->wdd);
}

static int kempld_wdt_remove(struct platform_device *pdev)
{
	struct kempld_wdt_data *wdt_data = platform_get_drvdata(pdev);
	struct watchdog_device *wdd = &wdt_data->wdd;
	int ret = 0;

	if (!nowayout)
		ret = kempld_wdt_stop(wdd);
	watchdog_unregister_device(wdd);

	return ret;
}

#ifdef CONFIG_PM
/* Disable watchdog if it is active during suspend */
static int kempld_wdt_suspend(struct platform_device *pdev,
				pm_message_t message)
{
	struct kempld_wdt_data *wdt_data = platform_get_drvdata(pdev);
	struct kempld_device_data *pld = wdt_data->pld;
	struct watchdog_device *wdd = &wdt_data->wdd;

	kempld_get_mutex(pld);
	wdt_data->pm_status_store = kempld_read8(pld, KEMPLD_WDT_CFG);
	kempld_release_mutex(pld);

	kempld_wdt_update_timeouts(wdt_data);

	if (wdt_data->pm_status_store & KEMPLD_WDT_CFG_ENABLE)
		return kempld_wdt_stop(wdd);

	return 0;
}

/* Enable watchdog and configure it if necessary */
static int kempld_wdt_resume(struct platform_device *pdev)
{
	struct kempld_wdt_data *wdt_data = platform_get_drvdata(pdev);
	struct watchdog_device *wdd = &wdt_data->wdd;

	/*
	 * If watchdog was stopped before suspend be sure it gets disabled
	 * again, for the case BIOS has enabled it during resume
	 */
	if (wdt_data->pm_status_store & KEMPLD_WDT_CFG_ENABLE)
		return kempld_wdt_start(wdd);
	else
		return kempld_wdt_stop(wdd);
}
#else
#define kempld_wdt_suspend	NULL
#define kempld_wdt_resume	NULL
#endif

static struct platform_driver kempld_wdt_driver = {
	.driver		= {
		.name	= "kempld-wdt",
	},
	.probe		= kempld_wdt_probe,
	.remove		= kempld_wdt_remove,
	.shutdown	= kempld_wdt_shutdown,
	.suspend	= kempld_wdt_suspend,
	.resume		= kempld_wdt_resume,
};

module_platform_driver(kempld_wdt_driver);

MODULE_DESCRIPTION("KEM PLD Watchdog Driver");
MODULE_AUTHOR("Michael Brunner <michael.brunner@kontron.com>");
MODULE_LICENSE("GPL");
