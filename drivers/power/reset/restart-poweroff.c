// SPDX-License-Identifier: GPL-2.0-only
/*
 * Power off by restarting and let u-boot keep hold of the machine
 * until the user presses a button for example.
 *
 * Andrew Lunn <andrew@lunn.ch>
 *
 * Copyright (C) 2012 Andrew Lunn
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/module.h>
#include <linux/reboot.h>

static int restart_poweroff_do_poweroff(struct sys_off_data *data)
{
	reboot_mode = REBOOT_HARD;
	machine_restart(NULL);
	return NOTIFY_DONE;
}

static int restart_poweroff_probe(struct platform_device *pdev)
{
	/* Set this handler to low priority to not override an existing handler */
	return devm_register_sys_off_handler(&pdev->dev,
					     SYS_OFF_MODE_POWER_OFF,
					     SYS_OFF_PRIO_LOW,
					     restart_poweroff_do_poweroff,
					     NULL);
}

static const struct of_device_id of_restart_poweroff_match[] = {
	{ .compatible = "restart-poweroff", },
	{},
};
MODULE_DEVICE_TABLE(of, of_restart_poweroff_match);

static struct platform_driver restart_poweroff_driver = {
	.probe = restart_poweroff_probe,
	.driver = {
		.name = "poweroff-restart",
		.of_match_table = of_restart_poweroff_match,
	},
};
module_platform_driver(restart_poweroff_driver);

MODULE_AUTHOR("Andrew Lunn <andrew@lunn.ch");
MODULE_DESCRIPTION("restart poweroff driver");
MODULE_ALIAS("platform:poweroff-restart");
