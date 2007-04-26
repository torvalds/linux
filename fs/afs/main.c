/* AFS client file system
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/completion.h>
#include "internal.h"

MODULE_DESCRIPTION("AFS Client File System");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

unsigned afs_debug;
module_param_named(debug, afs_debug, uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(afs_debug, "AFS debugging mask");

static char *rootcell;

module_param(rootcell, charp, 0);
MODULE_PARM_DESC(rootcell, "root AFS cell name and VL server IP addr list");

#ifdef AFS_CACHING_SUPPORT
static struct cachefs_netfs_operations afs_cache_ops = {
	.get_page_cookie	= afs_cache_get_page_cookie,
};

struct cachefs_netfs afs_cache_netfs = {
	.name			= "afs",
	.version		= 0,
	.ops			= &afs_cache_ops,
};
#endif

/*
 * initialise the AFS client FS module
 */
static int __init afs_init(void)
{
	int ret;

	printk(KERN_INFO "kAFS: Red Hat AFS client v0.1 registering.\n");

	/* register the /proc stuff */
	ret = afs_proc_init();
	if (ret < 0)
		return ret;

#ifdef AFS_CACHING_SUPPORT
	/* we want to be able to cache */
	ret = cachefs_register_netfs(&afs_cache_netfs,
				     &afs_cache_cell_index_def);
	if (ret < 0)
		goto error_cache;
#endif

	/* initialise the cell DB */
	ret = afs_cell_init(rootcell);
	if (ret < 0)
		goto error_cell_init;

	/* initialise the VL update process */
	ret = afs_vlocation_update_init();
	if (ret < 0)
		goto error_vl_update_init;

	/* initialise the callback update process */
	ret = afs_callback_update_init();

	/* create the RxRPC transport */
	ret = afs_open_socket();
	if (ret < 0)
		goto error_open_socket;

	/* register the filesystems */
	ret = afs_fs_init();
	if (ret < 0)
		goto error_fs;

	return ret;

error_fs:
	afs_close_socket();
error_open_socket:
error_vl_update_init:
error_cell_init:
#ifdef AFS_CACHING_SUPPORT
	cachefs_unregister_netfs(&afs_cache_netfs);
error_cache:
#endif
	afs_callback_update_kill();
	afs_vlocation_purge();
	afs_cell_purge();
	afs_proc_cleanup();
	printk(KERN_ERR "kAFS: failed to register: %d\n", ret);
	return ret;
}

/* XXX late_initcall is kludgy, but the only alternative seems to create
 * a transport upon the first mount, which is worse. Or is it?
 */
late_initcall(afs_init);	/* must be called after net/ to create socket */

/*
 * clean up on module removal
 */
static void __exit afs_exit(void)
{
	printk(KERN_INFO "kAFS: Red Hat AFS client v0.1 unregistering.\n");

	afs_fs_exit();
	afs_close_socket();
	afs_purge_servers();
	afs_callback_update_kill();
	afs_vlocation_purge();
	flush_scheduled_work();
	afs_cell_purge();
#ifdef AFS_CACHING_SUPPORT
	cachefs_unregister_netfs(&afs_cache_netfs);
#endif
	afs_proc_cleanup();
}

module_exit(afs_exit);
