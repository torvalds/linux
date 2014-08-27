/*
 *  MEN 14F021P00 Board Management Controller (BMC) Watchdog Driver.
 *
 *  Copyright (C) 2014 MEN Mikro Elektronik Nuernberg GmbH
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>

#define DEVNAME "menf21bmc_wdt"

#define BMC_CMD_WD_ON		0x11
#define BMC_CMD_WD_OFF		0x12
#define BMC_CMD_WD_TRIG		0x13
#define BMC_CMD_WD_TIME		0x14
#define BMC_CMD_WD_STATE	0x17
#define BMC_WD_OFF_VAL		0x69
#define BMC_CMD_RST_RSN		0x92

#define BMC_WD_TIMEOUT_MIN	1	/* in sec */
#define BMC_WD_TIMEOUT_MAX	6553	/* in sec */

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct menf21bmc_wdt {
	struct watchdog_device wdt;
	struct i2c_client *i2c_client;
};

static int menf21bmc_wdt_set_bootstatus(struct menf21bmc_wdt *data)
{
	int rst_rsn;

	rst_rsn = i2c_smbus_read_byte_data(data->i2c_client, BMC_CMD_RST_RSN);
	if (rst_rsn < 0)
		return rst_rsn;

	if (rst_rsn == 0x02)
		data->wdt.bootstatus |= WDIOF_CARDRESET;
	else if (rst_rsn == 0x05)
		data->wdt.bootstatus |= WDIOF_EXTERN1;
	else if (rst_rsn == 0x06)
		data->wdt.bootstatus |= WDIOF_EXTERN2;
	else if (rst_rsn == 0x0A)
		data->wdt.bootstatus |= WDIOF_POWERUNDER;

	return 0;
}

static int menf21bmc_wdt_start(struct watchdog_device *wdt)
{
	struct menf21bmc_wdt *drv_data = watchdog_get_drvdata(wdt);

	return i2c_smbus_write_byte(drv_data->i2c_client, BMC_CMD_WD_ON);
}

static int menf21bmc_wdt_stop(struct watchdog_device *wdt)
{
	struct menf21bmc_wdt *drv_data = watchdog_get_drvdata(wdt);

	return i2c_smbus_write_byte_data(drv_data->i2c_client,
					 BMC_CMD_WD_OFF, BMC_WD_OFF_VAL);
}

static int
menf21bmc_wdt_settimeout(struct watchdog_device *wdt, unsigned int timeout)
{
	int ret;
	struct menf21bmc_wdt *drv_data = watchdog_get_drvdata(wdt);

	/*
	 *  BMC Watchdog does have a resolution of 100ms.
	 *  Watchdog API defines the timeout in seconds, so we have to
	 *  multiply the value.
	 */
	ret = i2c_smbus_write_word_data(drv_data->i2c_client,
					BMC_CMD_WD_TIME, timeout * 10);
	if (ret < 0)
		return ret;

	wdt->timeout = timeout;

	return 0;
}

static int menf21bmc_wdt_ping(struct watchdog_device *wdt)
{
	struct menf21bmc_wdt *drv_data = watchdog_get_drvdata(wdt);

	return i2c_smbus_write_byte(drv_data->i2c_client, BMC_CMD_WD_TRIG);
}

static const struct watchdog_info menf21bmc_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity = DEVNAME,
};

static const struct watchdog_ops menf21bmc_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= menf21bmc_wdt_start,
	.stop		= menf21bmc_wdt_stop,
	.ping		= menf21bmc_wdt_ping,
	.set_timeout	= menf21bmc_wdt_settimeout,
};

static int menf21bmc_wdt_probe(struct platform_device *pdev)
{
	int ret, bmc_timeout;
	struct menf21bmc_wdt *drv_data;
	struct i2c_client *i2c_client = to_i2c_client(pdev->dev.parent);

	drv_data = devm_kzalloc(&pdev->dev,
				sizeof(struct menf21bmc_wdt), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	drv_data->wdt.ops = &menf21bmc_wdt_ops;
	drv_data->wdt.info = &menf21bmc_wdt_info;
	drv_data->wdt.min_timeout = BMC_WD_TIMEOUT_MIN;
	drv_data->wdt.max_timeout = BMC_WD_TIMEOUT_MAX;
	drv_data->i2c_client = i2c_client;

	/*
	 * Get the current wdt timeout value from the BMC because
	 * the BMC will save the value set before if the system restarts.
	 */
	bmc_timeout = i2c_smbus_read_word_data(drv_data->i2c_client,
					       BMC_CMD_WD_TIME);
	if (bmc_timeout < 0) {
		dev_err(&pdev->dev, "failed to get current WDT timeout\n");
		return bmc_timeout;
	}

	watchdog_init_timeout(&drv_data->wdt, bmc_timeout / 10, &pdev->dev);
	watchdog_set_nowayout(&drv_data->wdt, nowayout);
	watchdog_set_drvdata(&drv_data->wdt, drv_data);
	platform_set_drvdata(pdev, drv_data);

	ret = menf21bmc_wdt_set_bootstatus(drv_data);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to set Watchdog bootstatus\n");
		return ret;
	}

	ret = watchdog_register_device(&drv_data->wdt);
	if (ret) {
		dev_err(&pdev->dev, "failed to register Watchdog device\n");
		return ret;
	}

	dev_info(&pdev->dev, "MEN 14F021P00 BMC Watchdog device enabled\n");

	return 0;
}

static int menf21bmc_wdt_remove(struct platform_device *pdev)
{
	struct menf21bmc_wdt *drv_data = platform_get_drvdata(pdev);

	dev_warn(&pdev->dev,
		 "Unregister MEN 14F021P00 BMC Watchdog device, board may reset\n");

	watchdog_unregister_device(&drv_data->wdt);

	return 0;
}

static void menf21bmc_wdt_shutdown(struct platform_device *pdev)
{
	struct menf21bmc_wdt *drv_data = platform_get_drvdata(pdev);

	i2c_smbus_write_word_data(drv_data->i2c_client,
				  BMC_CMD_WD_OFF, BMC_WD_OFF_VAL);
}

static struct  platform_driver menf21bmc_wdt = {
	.driver		= {
		.owner = THIS_MODULE,
		.name	= DEVNAME,
	},
	.probe		= menf21bmc_wdt_probe,
	.remove		= menf21bmc_wdt_remove,
	.shutdown	= menf21bmc_wdt_shutdown,
};

module_platform_driver(menf21bmc_wdt);

MODULE_DESCRIPTION("MEN 14F021P00 BMC Watchdog driver");
MODULE_AUTHOR("Andreas Werner <andreas.werner@men.de>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:menf21bmc_wdt");
