// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/netdevice.h>
#include <linux/utsname.h>

#include "ionic.h"
#include "ionic_bus.h"

MODULE_DESCRIPTION(IONIC_DRV_DESCRIPTION);
MODULE_AUTHOR("Pensando Systems, Inc");
MODULE_LICENSE("GPL");
MODULE_VERSION(IONIC_DRV_VERSION);

static int __init ionic_init_module(void)
{
	pr_info("%s %s, ver %s\n",
		IONIC_DRV_NAME, IONIC_DRV_DESCRIPTION, IONIC_DRV_VERSION);
	return ionic_bus_register_driver();
}

static void __exit ionic_cleanup_module(void)
{
	ionic_bus_unregister_driver();

	pr_info("%s removed\n", IONIC_DRV_NAME);
}

module_init(ionic_init_module);
module_exit(ionic_cleanup_module);
