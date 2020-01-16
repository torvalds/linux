// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015-2016 Mentor Graphics
 */

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/watchdog.h>

#include "watchdog_pretimeout.h"

/**
 * pretimeout_yesop - No operation on watchdog pretimeout event
 * @wdd - watchdog_device
 *
 * This function prints a message about pretimeout to kernel log.
 */
static void pretimeout_yesop(struct watchdog_device *wdd)
{
	pr_alert("watchdog%d: pretimeout event\n", wdd->id);
}

static struct watchdog_goveryesr watchdog_gov_yesop = {
	.name		= "yesop",
	.pretimeout	= pretimeout_yesop,
};

static int __init watchdog_gov_yesop_register(void)
{
	return watchdog_register_goveryesr(&watchdog_gov_yesop);
}

static void __exit watchdog_gov_yesop_unregister(void)
{
	watchdog_unregister_goveryesr(&watchdog_gov_yesop);
}
module_init(watchdog_gov_yesop_register);
module_exit(watchdog_gov_yesop_unregister);

MODULE_AUTHOR("Vladimir Zapolskiy <vladimir_zapolskiy@mentor.com>");
MODULE_DESCRIPTION("Panic watchdog pretimeout goveryesr");
MODULE_LICENSE("GPL");
