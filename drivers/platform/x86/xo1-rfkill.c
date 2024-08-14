// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Support for rfkill through the OLPC XO-1 laptop embedded controller
 *
 * Copyright (C) 2010 One Laptop per Child
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rfkill.h>
#include <linux/olpc-ec.h>

static bool card_blocked;

static int rfkill_set_block(void *data, bool blocked)
{
	unsigned char cmd;
	int r;

	if (blocked == card_blocked)
		return 0;

	if (blocked)
		cmd = EC_WLAN_ENTER_RESET;
	else
		cmd = EC_WLAN_LEAVE_RESET;

	r = olpc_ec_cmd(cmd, NULL, 0, NULL, 0);
	if (r == 0)
		card_blocked = blocked;

	return r;
}

static const struct rfkill_ops rfkill_ops = {
	.set_block = rfkill_set_block,
};

static int xo1_rfkill_probe(struct platform_device *pdev)
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

static void xo1_rfkill_remove(struct platform_device *pdev)
{
	struct rfkill *rfk = platform_get_drvdata(pdev);
	rfkill_unregister(rfk);
	rfkill_destroy(rfk);
}

static struct platform_driver xo1_rfkill_driver = {
	.driver = {
		.name = "xo1-rfkill",
	},
	.probe		= xo1_rfkill_probe,
	.remove_new	= xo1_rfkill_remove,
};

module_platform_driver(xo1_rfkill_driver);

MODULE_AUTHOR("Daniel Drake <dsd@laptop.org>");
MODULE_DESCRIPTION("OLPC XO-1 software RF kill switch");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:xo1-rfkill");
