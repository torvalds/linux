/* AFS client file system
 *
 * Copyright (C) 2002,5 Red Hat, Inc. All Rights Reserved.
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
#include <linux/sched.h>
#include <linux/random.h>
#define CREATE_TRACE_POINTS
#include "internal.h"

MODULE_DESCRIPTION("AFS Client File System");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

unsigned afs_debug;
module_param_named(debug, afs_debug, uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(debug, "AFS debugging mask");

static char *rootcell;

module_param(rootcell, charp, 0);
MODULE_PARM_DESC(rootcell, "root AFS cell name and VL server IP addr list");

struct uuid_v1 afs_uuid;
struct workqueue_struct *afs_wq;

/*
 * initialise the AFS client FS module
 */
static int __init afs_init(void)
{
	int ret;

	printk(KERN_INFO "kAFS: Red Hat AFS client v0.1 registering.\n");

	generate_random_uuid((unsigned char *)&afs_uuid);

	/* create workqueue */
	ret = -ENOMEM;
	afs_wq = alloc_workqueue("afs", 0, 0);
	if (!afs_wq)
		return ret;

	/* register the /proc stuff */
	ret = afs_proc_init();
	if (ret < 0)
		goto error_proc;

#ifdef CONFIG_AFS_FSCACHE
	/* we want to be able to cache */
	ret = fscache_register_netfs(&afs_cache_netfs);
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
	if (ret < 0)
		goto error_callback_update_init;

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
	afs_callback_update_kill();
error_callback_update_init:
	afs_vlocation_purge();
error_vl_update_init:
	afs_cell_purge();
error_cell_init:
#ifdef CONFIG_AFS_FSCACHE
	fscache_unregister_netfs(&afs_cache_netfs);
error_cache:
#endif
	afs_proc_cleanup();
error_proc:
	destroy_workqueue(afs_wq);
	rcu_barrier();
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
	afs_kill_lock_manager();
	afs_close_socket();
	afs_purge_servers();
	afs_callback_update_kill();
	afs_vlocation_purge();
	destroy_workqueue(afs_wq);
	afs_cell_purge();
#ifdef CONFIG_AFS_FSCACHE
	fscache_unregister_netfs(&afs_cache_netfs);
#endif
	afs_proc_cleanup();
	rcu_barrier();
}

module_exit(afs_exit);
