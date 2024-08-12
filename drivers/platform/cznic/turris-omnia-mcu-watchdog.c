// SPDX-License-Identifier: GPL-2.0
/*
 * CZ.NIC's Turris Omnia MCU watchdog driver
 *
 * 2024 by Marek Behún <kabel@kernel.org>
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/units.h>
#include <linux/watchdog.h>

#include <linux/turris-omnia-mcu-interface.h>
#include "turris-omnia-mcu.h"

#define WATCHDOG_TIMEOUT		120

static unsigned int timeout;
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
			   __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int omnia_wdt_start(struct watchdog_device *wdt)
{
	struct omnia_mcu *mcu = watchdog_get_drvdata(wdt);

	return omnia_cmd_write_u8(mcu->client, OMNIA_CMD_SET_WATCHDOG_STATE, 1);
}

static int omnia_wdt_stop(struct watchdog_device *wdt)
{
	struct omnia_mcu *mcu = watchdog_get_drvdata(wdt);

	return omnia_cmd_write_u8(mcu->client, OMNIA_CMD_SET_WATCHDOG_STATE, 0);
}

static int omnia_wdt_ping(struct watchdog_device *wdt)
{
	struct omnia_mcu *mcu = watchdog_get_drvdata(wdt);

	return omnia_cmd_write_u8(mcu->client, OMNIA_CMD_SET_WATCHDOG_STATE, 1);
}

static int omnia_wdt_set_timeout(struct watchdog_device *wdt,
				 unsigned int timeout)
{
	struct omnia_mcu *mcu = watchdog_get_drvdata(wdt);

	return omnia_cmd_write_u16(mcu->client, OMNIA_CMD_SET_WDT_TIMEOUT,
				   timeout * DECI);
}

static unsigned int omnia_wdt_get_timeleft(struct watchdog_device *wdt)
{
	struct omnia_mcu *mcu = watchdog_get_drvdata(wdt);
	u16 timeleft;
	int err;

	err = omnia_cmd_read_u16(mcu->client, OMNIA_CMD_GET_WDT_TIMELEFT,
				 &timeleft);
	if (err) {
		dev_err(&mcu->client->dev, "Cannot get watchdog timeleft: %d\n",
			err);
		return 0;
	}

	return timeleft / DECI;
}

static const struct watchdog_info omnia_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "Turris Omnia MCU Watchdog",
};

static const struct watchdog_ops omnia_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= omnia_wdt_start,
	.stop		= omnia_wdt_stop,
	.ping		= omnia_wdt_ping,
	.set_timeout	= omnia_wdt_set_timeout,
	.get_timeleft	= omnia_wdt_get_timeleft,
};

int omnia_mcu_register_watchdog(struct omnia_mcu *mcu)
{
	struct device *dev = &mcu->client->dev;
	u8 state;
	int err;

	if (!(mcu->features & OMNIA_FEAT_WDT_PING))
		return 0;

	mcu->wdt.info = &omnia_wdt_info;
	mcu->wdt.ops = &omnia_wdt_ops;
	mcu->wdt.parent = dev;
	mcu->wdt.min_timeout = 1;
	mcu->wdt.max_timeout = 65535 / DECI;

	mcu->wdt.timeout = WATCHDOG_TIMEOUT;
	watchdog_init_timeout(&mcu->wdt, timeout, dev);

	watchdog_set_drvdata(&mcu->wdt, mcu);

	omnia_wdt_set_timeout(&mcu->wdt, mcu->wdt.timeout);

	err = omnia_cmd_read_u8(mcu->client, OMNIA_CMD_GET_WATCHDOG_STATE,
				&state);
	if (err)
		return dev_err_probe(dev, err,
				     "Cannot get MCU watchdog state\n");

	if (state)
		set_bit(WDOG_HW_RUNNING, &mcu->wdt.status);

	watchdog_set_nowayout(&mcu->wdt, nowayout);
	watchdog_stop_on_reboot(&mcu->wdt);
	err = devm_watchdog_register_device(dev, &mcu->wdt);
	if (err)
		return dev_err_probe(dev, err,
				     "Cannot register MCU watchdog\n");

	return 0;
}
