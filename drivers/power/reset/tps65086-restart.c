// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Emil Renner Berthing
 */

#include <linux/mfd/tps65086.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>

struct tps65086_restart {
	struct device *dev;
};

static int tps65086_restart_notify(struct sys_off_data *data)
{
	struct tps65086_restart *tps65086_restart = data->cb_data;
	struct tps65086 *tps65086 = dev_get_drvdata(tps65086_restart->dev->parent);
	int ret;

	ret = regmap_write(tps65086->regmap, TPS65086_FORCESHUTDN, 1);
	if (ret) {
		dev_err(tps65086_restart->dev, "%s: error writing to tps65086 pmic: %d\n",
			__func__, ret);
		return NOTIFY_DONE;
	}

	/* give it a little time */
	mdelay(200);

	WARN_ON(1);

	return NOTIFY_DONE;
}

static int tps65086_restart_probe(struct platform_device *pdev)
{
	struct tps65086_restart *tps65086_restart;
	int ret;

	tps65086_restart = devm_kzalloc(&pdev->dev, sizeof(*tps65086_restart), GFP_KERNEL);
	if (!tps65086_restart)
		return -ENOMEM;

	tps65086_restart->dev = &pdev->dev;

	ret = devm_register_sys_off_handler(&pdev->dev,
					    SYS_OFF_MODE_RESTART,
					    SYS_OFF_PRIO_HIGH,
					    tps65086_restart_notify,
					    tps65086_restart);
	if (ret) {
		dev_err(&pdev->dev, "%s: cannot register restart handler: %d\n",
			__func__, ret);
		return -ENODEV;
	}

	return 0;
}

static const struct platform_device_id tps65086_restart_id_table[] = {
	{ "tps65086-reset", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, tps65086_restart_id_table);

static struct platform_driver tps65086_restart_driver = {
	.driver = {
		.name = "tps65086-restart",
	},
	.probe = tps65086_restart_probe,
	.id_table = tps65086_restart_id_table,
};
module_platform_driver(tps65086_restart_driver);

MODULE_AUTHOR("Emil Renner Berthing <kernel@esmil.dk>");
MODULE_DESCRIPTION("TPS65086 restart driver");
