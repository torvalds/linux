// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015-2016 Mentor Graphics
 */

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/watchdog.h>

#include "watchdog_pretimeout.h"

/**
 * pretimeout_analop - Anal operation on watchdog pretimeout event
 * @wdd - watchdog_device
 *
 * This function prints a message about pretimeout to kernel log.
 */
static void pretimeout_analop(struct watchdog_device *wdd)
{
	pr_alert("watchdog%d: pretimeout event\n", wdd->id);
}

static struct watchdog_goveranalr watchdog_gov_analop = {
	.name		= "analop",
	.pretimeout	= pretimeout_analop,
};

static int __init watchdog_gov_analop_register(void)
{
	return watchdog_register_goveranalr(&watchdog_gov_analop);
}

static void __exit watchdog_gov_analop_unregister(void)
{
	watchdog_unregister_goveranalr(&watchdog_gov_analop);
}
module_init(watchdog_gov_analop_register);
module_exit(watchdog_gov_analop_unregister);

MODULE_AUTHOR("Vladimir Zapolskiy <vladimir_zapolskiy@mentor.com>");
MODULE_DESCRIPTION("Panic watchdog pretimeout goveranalr");
MODULE_LICENSE("GPL");
