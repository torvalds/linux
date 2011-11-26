/*
 * Support for rfkill through the OLPC XO-1 laptop embedded controller
 *
 * Copyright (C) 2010 One Laptop per Child
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rfkill.h>

#include <asm/olpc.h>

static int rfkill_set_block(void *data, bool blocked)
{
	unsigned char cmd;
	if (blocked)
		cmd = EC_WLAN_ENTER_RESET;
	else
		cmd = EC_WLAN_LEAVE_RESET;

	return olpc_ec_cmd(cmd, NULL, 0, NULL, 0);
}

static const struct rfkill_ops rfkill_ops = {
	.set_block = rfkill_set_block,
};

static int __devinit xo1_rfkill_probe(struct platform_device *pdev)
{
	struct rfkill *rfk;
	int r;

	rfk = rfkill_alloc(pdev->name, &pdev->dev, RFKILL_TYPE_WLAN,
			   &rfkill_ops, NULL);
	if (!rfk)
		return -ENOMEM;

	r = rfkill_register(rfk);
	if (r) {
		rfkill_destroy(rfk);
		return r;
	}

	platform_set_drvdata(pdev, rfk);
	return 0;
}

static int __devexit xo1_rfkill_remove(struct platform_device *pdev)
{
	struct rfkill *rfk = platform_get_drvdata(pdev);
	rfkill_unregister(rfk);
	rfkill_destroy(rfk);
	return 0;
}

static struct platform_driver xo1_rfkill_driver = {
	.driver = {
		.name = "xo1-rfkill",
		.owner = THIS_MODULE,
	},
	.probe		= xo1_rfkill_probe,
	.remove		= __devexit_p(xo1_rfkill_remove),
};

module_platform_driver(xo1_rfkill_driver);

MODULE_AUTHOR("Daniel Drake <dsd@laptop.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:xo1-rfkill");
