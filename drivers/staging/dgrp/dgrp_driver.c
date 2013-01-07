/*
 *
 * Copyright 1999-2003 Digi International (www.digi.com)
 *     Jeff Randall
 *     James Puzzo  <jamesp at digi dot com>
 *     Scott Kilau  <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 */

/*
 *	Driver specific includes
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/init.h>

/*
 *  PortServer includes
 */
#include "dgrp_common.h"


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Digi International, http://www.digi.com");
MODULE_DESCRIPTION("RealPort driver for Digi's ethernet-based serial connectivity product line");
MODULE_VERSION(DIGI_VERSION);

struct list_head nd_struct_list;
struct dgrp_poll_data dgrp_poll_data;

int dgrp_register_cudevices = 1;/* Turn on/off registering legacy cu devices */
int dgrp_register_prdevices = 1;/* Turn on/off registering transparent print */
int dgrp_poll_tick = 20;	/* Poll interval - in ms */

module_param_named(register_cudevices, dgrp_register_cudevices, int, 0644);
MODULE_PARM_DESC(register_cudevices, "Turn on/off registering legacy cu devices");

module_param_named(register_prdevices, dgrp_register_prdevices, int, 0644);
MODULE_PARM_DESC(register_prdevices, "Turn on/off registering transparent print devices");

module_param_named(pollrate, dgrp_poll_tick, int, 0644);
MODULE_PARM_DESC(pollrate, "Poll interval in ms");

/* Driver load/unload functions */
static int dgrp_init_module(void);
static void dgrp_cleanup_module(void);

module_init(dgrp_init_module);
module_exit(dgrp_cleanup_module);

/*
 * init_module()
 *
 * Module load.  This is where it all starts.
 */
static int dgrp_init_module(void)
{
	INIT_LIST_HEAD(&nd_struct_list);

	spin_lock_init(&dgrp_poll_data.poll_lock);
	init_timer(&dgrp_poll_data.timer);
	dgrp_poll_data.poll_tick = dgrp_poll_tick;
	dgrp_poll_data.timer.function = dgrp_poll_handler;
	dgrp_poll_data.timer.data = (unsigned long) &dgrp_poll_data;

	dgrp_create_class_sysfs_files();

	dgrp_register_proc();

	return 0;
}


/*
 *	Module unload.  This is where it all ends.
 */
static void dgrp_cleanup_module(void)
{
	struct nd_struct *nd, *next;

	/*
	 *	Attempting to free resources in backwards
	 *	order of allocation, in case that helps
	 *	memory pool fragmentation.
	 */
	dgrp_unregister_proc();

	dgrp_remove_class_sysfs_files();


	list_for_each_entry_safe(nd, next, &nd_struct_list, list) {
		dgrp_tty_uninit(nd);
		kfree(nd);
	}
}
