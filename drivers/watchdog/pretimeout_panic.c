/*
 * Copyright (C) 2015-2016 Mentor Graphics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/watchdog.h>

#include "watchdog_pretimeout.h"

/**
 * pretimeout_panic - Panic on watchdog pretimeout event
 * @wdd - watchdog_device
 *
 * Panic, watchdog has not been fed till pretimeout event.
 */
static void pretimeout_panic(struct watchdog_device *wdd)
{
	panic("watchdog pretimeout event\n");
}

static struct watchdog_governor watchdog_gov_panic = {
	.name		= "panic",
	.pretimeout	= pretimeout_panic,
};

static int __init watchdog_gov_panic_register(void)
{
	return watchdog_register_governor(&watchdog_gov_panic);
}

static void __exit watchdog_gov_panic_unregister(void)
{
	watchdog_unregister_governor(&watchdog_gov_panic);
}
module_init(watchdog_gov_panic_register);
module_exit(watchdog_gov_panic_unregister);

MODULE_AUTHOR("Vladimir Zapolskiy <vladimir_zapolskiy@mentor.com>");
MODULE_DESCRIPTION("Panic watchdog pretimeout governor");
MODULE_LICENSE("GPL");
