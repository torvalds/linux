// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2026 Google LLC
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nmi.h>
#include <linux/watchdog.h>

#include "watchdog_pretimeout.h"

/**
 * pretimeout_dump - Dump on watchdog pretimeout event
 * @wdd: watchdog_device
 *
 * Dump all cpu backtrace on pretimeout event.
 */
static void pretimeout_dump(struct watchdog_device *wdd)
{
	pr_alert("watchdog%d: pretimeout event\n", wdd->id);
	if (!trigger_all_cpu_backtrace())
		pr_alert("trigger_all_cpu_backtrace() isn't available\n");
}

static struct watchdog_governor watchdog_gov_dump = {
	.name		= "dump",
	.pretimeout	= pretimeout_dump,
};

static int __init watchdog_gov_dump_register(void)
{
	return watchdog_register_governor(&watchdog_gov_dump);
}

static void __exit watchdog_gov_dump_unregister(void)
{
	watchdog_unregister_governor(&watchdog_gov_dump);
}
module_init(watchdog_gov_dump_register);
module_exit(watchdog_gov_dump_unregister);

MODULE_AUTHOR("Tzung-Bi Shih <tzungbi@kernel.org>");
MODULE_DESCRIPTION("Dump watchdog pretimeout governor");
MODULE_LICENSE("GPL");
