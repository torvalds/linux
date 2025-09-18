// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NCT6694 WDT driver based on USB interface.
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 */

#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/mfd/nct6694.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/watchdog.h>

#define DEVICE_NAME "nct6694-wdt"

#define NCT6694_DEFAULT_TIMEOUT		10
#define NCT6694_DEFAULT_PRETIMEOUT	0

#define NCT6694_WDT_MAX_DEVS		2

/*
 * USB command module type for NCT6694 WDT controller.
 * This defines the module type used for communication with the NCT6694
 * WDT controller over the USB interface.
 */
#define NCT6694_WDT_MOD			0x07

/* Command 00h - WDT Setup */
#define NCT6694_WDT_SETUP		0x00
#define NCT6694_WDT_SETUP_SEL(idx)	(idx ? 0x01 : 0x00)

/* Command 01h - WDT Command */
#define NCT6694_WDT_COMMAND		0x01
#define NCT6694_WDT_COMMAND_SEL(idx)	(idx ? 0x01 : 0x00)

static unsigned int timeout[NCT6694_WDT_MAX_DEVS] = {
	[0 ... (NCT6694_WDT_MAX_DEVS - 1)] = NCT6694_DEFAULT_TIMEOUT
};
module_param_array(timeout, int, NULL, 0644);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds");

static unsigned int pretimeout[NCT6694_WDT_MAX_DEVS] = {
	[0 ... (NCT6694_WDT_MAX_DEVS - 1)] = NCT6694_DEFAULT_PRETIMEOUT
};
module_param_array(pretimeout, int, NULL, 0644);
MODULE_PARM_DESC(pretimeout, "Watchdog pre-timeout in seconds");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
			   __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

enum {
	NCT6694_ACTION_NONE = 0,
	NCT6694_ACTION_SIRQ,
	NCT6694_ACTION_GPO,
};

struct __packed nct6694_wdt_setup {
	__le32 pretimeout;
	__le32 timeout;
	u8 owner;
	u8 scratch;
	u8 control;
	u8 status;
	__le32 countdown;
};

struct __packed nct6694_wdt_cmd {
	__le32 wdt_cmd;
	__le32 reserved;
};

union __packed nct6694_wdt_msg {
	struct nct6694_wdt_setup setup;
	struct nct6694_wdt_cmd cmd;
};

struct nct6694_wdt_data {
	struct watchdog_device wdev;
	struct device *dev;
	struct nct6694 *nct6694;
	union nct6694_wdt_msg *msg;
	unsigned char wdev_idx;
};

static int nct6694_wdt_setting(struct watchdog_device *wdev,
			       u32 timeout_val, u8 timeout_act,
			       u32 pretimeout_val, u8 pretimeout_act)
{
	struct nct6694_wdt_data *data = watchdog_get_drvdata(wdev);
	struct nct6694_wdt_setup *setup = &data->msg->setup;
	const struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_WDT_MOD,
		.cmd = NCT6694_WDT_SETUP,
		.sel = NCT6694_WDT_SETUP_SEL(data->wdev_idx),
		.len = cpu_to_le16(sizeof(*setup))
	};
	unsigned int timeout_fmt, pretimeout_fmt;

	if (pretimeout_val == 0)
		pretimeout_act = NCT6694_ACTION_NONE;

	timeout_fmt = (timeout_val * 1000) | (timeout_act << 24);
	pretimeout_fmt = (pretimeout_val * 1000) | (pretimeout_act << 24);

	memset(setup, 0, sizeof(*setup));
	setup->timeout = cpu_to_le32(timeout_fmt);
	setup->pretimeout = cpu_to_le32(pretimeout_fmt);

	return nct6694_write_msg(data->nct6694, &cmd_hd, setup);
}

static int nct6694_wdt_start(struct watchdog_device *wdev)
{
	struct nct6694_wdt_data *data = watchdog_get_drvdata(wdev);
	int ret;

	ret = nct6694_wdt_setting(wdev, wdev->timeout, NCT6694_ACTION_GPO,
				  wdev->pretimeout, NCT6694_ACTION_GPO);
	if (ret)
		return ret;

	dev_dbg(data->dev, "Setting WDT(%d): timeout = %d, pretimeout = %d\n",
		data->wdev_idx, wdev->timeout, wdev->pretimeout);

	return ret;
}

static int nct6694_wdt_stop(struct watchdog_device *wdev)
{
	struct nct6694_wdt_data *data = watchdog_get_drvdata(wdev);
	struct nct6694_wdt_cmd *cmd = &data->msg->cmd;
	const struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_WDT_MOD,
		.cmd = NCT6694_WDT_COMMAND,
		.sel = NCT6694_WDT_COMMAND_SEL(data->wdev_idx),
		.len = cpu_to_le16(sizeof(*cmd))
	};

	memcpy(&cmd->wdt_cmd, "WDTC", 4);
	cmd->reserved = 0;

	return nct6694_write_msg(data->nct6694, &cmd_hd, cmd);
}

static int nct6694_wdt_ping(struct watchdog_device *wdev)
{
	struct nct6694_wdt_data *data = watchdog_get_drvdata(wdev);
	struct nct6694_wdt_cmd *cmd = &data->msg->cmd;
	const struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_WDT_MOD,
		.cmd = NCT6694_WDT_COMMAND,
		.sel = NCT6694_WDT_COMMAND_SEL(data->wdev_idx),
		.len = cpu_to_le16(sizeof(*cmd))
	};

	memcpy(&cmd->wdt_cmd, "WDTS", 4);
	cmd->reserved = 0;

	return nct6694_write_msg(data->nct6694, &cmd_hd, cmd);
}

static int nct6694_wdt_set_timeout(struct watchdog_device *wdev,
				   unsigned int new_timeout)
{
	int ret;

	ret = nct6694_wdt_setting(wdev, new_timeout, NCT6694_ACTION_GPO,
				  wdev->pretimeout, NCT6694_ACTION_GPO);
	if (ret)
		return ret;

	wdev->timeout = new_timeout;

	return 0;
}

static int nct6694_wdt_set_pretimeout(struct watchdog_device *wdev,
				      unsigned int new_pretimeout)
{
	int ret;

	ret = nct6694_wdt_setting(wdev, wdev->timeout, NCT6694_ACTION_GPO,
				  new_pretimeout, NCT6694_ACTION_GPO);
	if (ret)
		return ret;

	wdev->pretimeout = new_pretimeout;

	return 0;
}

static unsigned int nct6694_wdt_get_time(struct watchdog_device *wdev)
{
	struct nct6694_wdt_data *data = watchdog_get_drvdata(wdev);
	struct nct6694_wdt_setup *setup = &data->msg->setup;
	const struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_WDT_MOD,
		.cmd = NCT6694_WDT_SETUP,
		.sel = NCT6694_WDT_SETUP_SEL(data->wdev_idx),
		.len = cpu_to_le16(sizeof(*setup))
	};
	unsigned int timeleft_ms;
	int ret;

	ret = nct6694_read_msg(data->nct6694, &cmd_hd, setup);
	if (ret)
		return 0;

	timeleft_ms = le32_to_cpu(setup->countdown);

	return timeleft_ms / 1000;
}

static const struct watchdog_info nct6694_wdt_info = {
	.options = WDIOF_SETTIMEOUT	|
		   WDIOF_KEEPALIVEPING	|
		   WDIOF_MAGICCLOSE	|
		   WDIOF_PRETIMEOUT,
	.identity = DEVICE_NAME,
};

static const struct watchdog_ops nct6694_wdt_ops = {
	.owner = THIS_MODULE,
	.start = nct6694_wdt_start,
	.stop = nct6694_wdt_stop,
	.set_timeout = nct6694_wdt_set_timeout,
	.set_pretimeout = nct6694_wdt_set_pretimeout,
	.get_timeleft = nct6694_wdt_get_time,
	.ping = nct6694_wdt_ping,
};

static void nct6694_wdt_ida_free(void *d)
{
	struct nct6694_wdt_data *data = d;
	struct nct6694 *nct6694 = data->nct6694;

	ida_free(&nct6694->wdt_ida, data->wdev_idx);
}

static int nct6694_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nct6694 *nct6694 = dev_get_drvdata(dev->parent);
	struct nct6694_wdt_data *data;
	struct watchdog_device *wdev;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->msg = devm_kzalloc(dev, sizeof(union nct6694_wdt_msg),
				 GFP_KERNEL);
	if (!data->msg)
		return -ENOMEM;

	data->dev = dev;
	data->nct6694 = nct6694;

	ret = ida_alloc(&nct6694->wdt_ida, GFP_KERNEL);
	if (ret < 0)
		return ret;
	data->wdev_idx = ret;

	ret = devm_add_action_or_reset(dev, nct6694_wdt_ida_free, data);
	if (ret)
		return ret;

	wdev = &data->wdev;
	wdev->info = &nct6694_wdt_info;
	wdev->ops = &nct6694_wdt_ops;
	wdev->timeout = timeout[data->wdev_idx];
	wdev->pretimeout = pretimeout[data->wdev_idx];
	if (timeout[data->wdev_idx] < pretimeout[data->wdev_idx]) {
		dev_warn(data->dev, "pretimeout < timeout. Setting to zero\n");
		wdev->pretimeout = 0;
	}

	wdev->min_timeout = 1;
	wdev->max_timeout = 255;

	platform_set_drvdata(pdev, data);

	watchdog_set_drvdata(&data->wdev, data);
	watchdog_set_nowayout(&data->wdev, nowayout);
	watchdog_stop_on_reboot(&data->wdev);

	return devm_watchdog_register_device(dev, &data->wdev);
}

static struct platform_driver nct6694_wdt_driver = {
	.driver = {
		.name	= DEVICE_NAME,
	},
	.probe		= nct6694_wdt_probe,
};

module_platform_driver(nct6694_wdt_driver);

MODULE_DESCRIPTION("USB-WDT driver for NCT6694");
MODULE_AUTHOR("Ming Yu <tmyu0@nuvoton.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:nct6694-wdt");
