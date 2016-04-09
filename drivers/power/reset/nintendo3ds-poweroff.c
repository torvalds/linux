/*
 *  nintendo3ds-poweroff.c
 *
 *  Copyright (C) 2016 Sergi Granell
 *  based on msm-poweroff.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/pm.h>
#include <linux/mfd/nintendo3ds-mcu.h>

static struct nintendo3ds_mcu_dev *mcu_dev;

static int do_nintendo3ds_restart(struct notifier_block *nb, unsigned long action,
			   void *data)
{
	u8 val = 1 << 2;
	mcu_dev->write_device(mcu_dev, NINTENDO3DS_MCU_REG_PWRCTL,
		sizeof(val), &val);
	return NOTIFY_DONE;
}

static struct notifier_block restart_nb = {
	.notifier_call = do_nintendo3ds_restart,
	.priority = 128,
};

static void do_nintendo3ds_poweroff(void)
{
	u8 val = 1;
	mcu_dev->write_device(mcu_dev, NINTENDO3DS_MCU_REG_PWRCTL,
		sizeof(val), &val);
}

static int nintendo3ds_restart_probe(struct platform_device *pdev)
{
	mcu_dev = dev_get_drvdata(pdev->dev.parent);

	register_restart_handler(&restart_nb);
	pm_power_off = do_nintendo3ds_poweroff;

	return 0;
}


static struct platform_driver nintendo3ds_restart_driver = {
	.driver = {
		.name = "nintendo3ds-powercontrol",
	},
	.probe = nintendo3ds_restart_probe,
};
module_platform_driver(nintendo3ds_restart_driver);

MODULE_DESCRIPTION("Nintendo 3DS Power control driver");
MODULE_AUTHOR("Sergi Granell, <xerpi.g.12@gmail.com>");
MODULE_LICENSE("GPL");

