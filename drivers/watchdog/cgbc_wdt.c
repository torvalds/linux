// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Congatec Board Controller watchdog driver
 *
 * Copyright (C) 2024 Bootlin
 * Author: Thomas Richard <thomas.richard@bootlin.com>
 */

#include <linux/build_bug.h>
#include <linux/device.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#include <linux/mfd/cgbc.h>

#define CGBC_WDT_CMD_TRIGGER	0x27
#define CGBC_WDT_CMD_INIT	0x28
#define CGBC_WDT_DISABLE	0x00

#define CGBC_WDT_MODE_SINGLE_EVENT 0x02

#define CGBC_WDT_MIN_TIMEOUT	1
#define CGBC_WDT_MAX_TIMEOUT	((U32_MAX >> 8) / 1000)

#define CGBC_WDT_DEFAULT_TIMEOUT	30
#define CGBC_WDT_DEFAULT_PRETIMEOUT	0

enum action {
	ACTION_INT = 0,
	ACTION_SMI,
	ACTION_RESET,
	ACTION_BUTTON,
};

static unsigned int timeout;
module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout,
		 "Watchdog timeout in seconds. (>=0, default="
		 __MODULE_STRING(CGBC_WDT_DEFAULT_TIMEOUT) ")");

static unsigned int pretimeout = CGBC_WDT_DEFAULT_PRETIMEOUT;
module_param(pretimeout, uint, 0);
MODULE_PARM_DESC(pretimeout,
		 "Watchdog pretimeout in seconds. (>=0, default="
		 __MODULE_STRING(CGBC_WDT_DEFAULT_PRETIMEOUT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct cgbc_wdt_data {
	struct cgbc_device_data	*cgbc;
	struct watchdog_device	wdd;
};

struct cgbc_wdt_cmd_cfg {
	u8 cmd;
	u8 mode;
	u8 action;
	u8 timeout1[3];
	u8 timeout2[3];
	u8 reserved[3];
	u8 delay[3];
} __packed;

static_assert(sizeof(struct cgbc_wdt_cmd_cfg) == 15);

static int cgbc_wdt_start(struct watchdog_device *wdd)
{
	struct cgbc_wdt_data *wdt_data = watchdog_get_drvdata(wdd);
	struct cgbc_device_data *cgbc = wdt_data->cgbc;
	unsigned int timeout1 = (wdd->timeout - wdd->pretimeout) * 1000;
	unsigned int timeout2 = wdd->pretimeout * 1000;
	u8 action;

	struct cgbc_wdt_cmd_cfg cmd_start = {
		.cmd = CGBC_WDT_CMD_INIT,
		.mode = CGBC_WDT_MODE_SINGLE_EVENT,
		.timeout1[0] = (u8)timeout1,
		.timeout1[1] = (u8)(timeout1 >> 8),
		.timeout1[2] = (u8)(timeout1 >> 16),
		.timeout2[0] = (u8)timeout2,
		.timeout2[1] = (u8)(timeout2 >> 8),
		.timeout2[2] = (u8)(timeout2 >> 16),
	};

	if (wdd->pretimeout) {
		action = 2;
		action |= ACTION_SMI << 2;
		action |= ACTION_RESET << 4;
	} else {
		action = 1;
		action |= ACTION_RESET << 2;
	}

	cmd_start.action = action;

	return cgbc_command(cgbc, &cmd_start, sizeof(cmd_start), NULL, 0, NULL);
}

static int cgbc_wdt_stop(struct watchdog_device *wdd)
{
	struct cgbc_wdt_data *wdt_data = watchdog_get_drvdata(wdd);
	struct cgbc_device_data *cgbc = wdt_data->cgbc;
	struct cgbc_wdt_cmd_cfg cmd_stop = {
		.cmd = CGBC_WDT_CMD_INIT,
		.mode = CGBC_WDT_DISABLE,
	};

	return cgbc_command(cgbc, &cmd_stop, sizeof(cmd_stop), NULL, 0, NULL);
}

static int cgbc_wdt_keepalive(struct watchdog_device *wdd)
{
	struct cgbc_wdt_data *wdt_data = watchdog_get_drvdata(wdd);
	struct cgbc_device_data *cgbc = wdt_data->cgbc;
	u8 cmd_ping = CGBC_WDT_CMD_TRIGGER;

	return cgbc_command(cgbc, &cmd_ping, sizeof(cmd_ping), NULL, 0, NULL);
}

static int cgbc_wdt_set_pretimeout(struct watchdog_device *wdd,
				   unsigned int pretimeout)
{
	wdd->pretimeout = pretimeout;

	if (watchdog_active(wdd))
		return cgbc_wdt_start(wdd);

	return 0;
}

static int cgbc_wdt_set_timeout(struct watchdog_device *wdd,
				unsigned int timeout)
{
	if (timeout < wdd->pretimeout)
		wdd->pretimeout = 0;

	wdd->timeout = timeout;

	if (watchdog_active(wdd))
		return cgbc_wdt_start(wdd);

	return 0;
}

static const struct watchdog_info cgbc_wdt_info = {
	.identity	= "CGBC Watchdog",
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING |
		WDIOF_MAGICCLOSE | WDIOF_PRETIMEOUT
};

static const struct watchdog_ops cgbc_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= cgbc_wdt_start,
	.stop		= cgbc_wdt_stop,
	.ping		= cgbc_wdt_keepalive,
	.set_timeout	= cgbc_wdt_set_timeout,
	.set_pretimeout = cgbc_wdt_set_pretimeout,
};

static int cgbc_wdt_probe(struct platform_device *pdev)
{
	struct cgbc_device_data *cgbc = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct cgbc_wdt_data *wdt_data;
	struct watchdog_device *wdd;

	wdt_data = devm_kzalloc(dev, sizeof(*wdt_data), GFP_KERNEL);
	if (!wdt_data)
		return -ENOMEM;

	wdt_data->cgbc = cgbc;
	wdd = &wdt_data->wdd;
	wdd->parent = dev;

	wdd->info = &cgbc_wdt_info;
	wdd->ops = &cgbc_wdt_ops;
	wdd->max_timeout = CGBC_WDT_MAX_TIMEOUT;
	wdd->min_timeout = CGBC_WDT_MIN_TIMEOUT;

	watchdog_set_drvdata(wdd, wdt_data);
	watchdog_set_nowayout(wdd, nowayout);

	wdd->timeout = CGBC_WDT_DEFAULT_TIMEOUT;
	watchdog_init_timeout(wdd, timeout, dev);
	cgbc_wdt_set_pretimeout(wdd, pretimeout);

	platform_set_drvdata(pdev, wdt_data);
	watchdog_stop_on_reboot(wdd);
	watchdog_stop_on_unregister(wdd);

	return devm_watchdog_register_device(dev, wdd);
}

static struct platform_driver cgbc_wdt_driver = {
	.driver		= {
		.name	= "cgbc-wdt",
	},
	.probe		= cgbc_wdt_probe,
};

module_platform_driver(cgbc_wdt_driver);

MODULE_DESCRIPTION("Congatec Board Controller Watchdog Driver");
MODULE_AUTHOR("Thomas Richard <thomas.richard@bootlin.com>");
MODULE_LICENSE("GPL");
