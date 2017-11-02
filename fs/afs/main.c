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

struct workqueue_struct *afs_wq;
struct afs_net __afs_net;

/*
 * Initialise an AFS network namespace record.
 */
static int __net_init afs_net_init(struct afs_net *net)
{
	int ret;

	net->live = true;
	generate_random_uuid((unsigned char *)&net->uuid);

	INIT_WORK(&net->charge_preallocation_work, afs_charge_preallocation);
	mutex_init(&net->socket_mutex);
	INIT_LIST_HEAD(&net->cells);
	rwlock_init(&net->cells_lock);
	init_rwsem(&net->cells_sem);
	init_waitqueue_head(&net->cells_freeable_wq);
	init_rwsem(&net->proc_cells_sem);
	INIT_LIST_HEAD(&net->proc_cells);
	INIT_LIST_HEAD(&net->vl_updates);
	INIT_LIST_HEAD(&net->vl_graveyard);
	INIT_DELAYED_WORK(&net->vl_reaper, afs_vlocation_reaper);
	INIT_DELAYED_WORK(&net->vl_updater, afs_vlocation_updater);
	spin_lock_init(&net->vl_updates_lock);
	spin_lock_init(&net->vl_graveyard_lock);
	net->servers = RB_ROOT;
	rwlock_init(&net->servers_lock);
	INIT_LIST_HEAD(&net->server_graveyard);
	spin_lock_init(&net->server_graveyard_lock);
	INIT_WORK(&net->server_reaper, afs_reap_server);
	timer_setup(&net->server_timer, afs_server_timer, 0);

	/* Register the /proc stuff */
	ret = afs_proc_init(net);
	if (ret < 0)
		goto error_proc;

	/* Initialise the cell DB */
	ret = afs_cell_init(net, rootcell);
	if (ret < 0)
		goto error_cell_init;

	/* Create the RxRPC transport */
	ret = afs_open_socket(net);
	if (ret < 0)
		goto error_open_socket;

	return 0;

error_open_socket:
	afs_vlocation_purge(net);
	afs_cell_purge(net);
error_cell_init:
	afs_proc_cleanup(net);
error_proc:
	return ret;
}

/*
 * Clean up and destroy an AFS network namespace record.
 */
static void __net_exit afs_net_exit(struct afs_net *net)
{
	net->live = false;
	afs_purge_servers(net);
	afs_vlocation_purge(net);
	afs_cell_purge(net);
	afs_close_socket(net);
	afs_proc_cleanup(net);
}

/*
 * initialise the AFS client FS module
 */
static int __init afs_init(void)
{
	int ret = -ENOMEM;

	printk(KERN_INFO "kAFS: Red Hat AFS client v0.1 registering.\n");

	afs_wq = alloc_workqueue("afs", 0, 0);
	if (!afs_wq)
		goto error_afs_wq;
	afs_async_calls = alloc_workqueue("kafsd", WQ_MEM_RECLAIM, 0);
	if (!afs_async_calls)
		goto error_async;
	afs_vlocation_update_worker =
		alloc_workqueue("kafs_vlupdated", WQ_MEM_RECLAIM, 0);
	if (!afs_vlocation_update_worker)
		goto error_vl_up;
	afs_lock_manager = alloc_workqueue("kafs_lockd", WQ_MEM_RECLAIM, 0);
	if (!afs_lock_manager)
		goto error_lockmgr;

#ifdef CONFIG_AFS_FSCACHE
	/* we want to be able to cache */
	ret = fscache_register_netfs(&afs_cache_netfs);
	if (ret < 0)
		goto error_cache;
#endif

	ret = afs_net_init(&__afs_net);
	if (ret < 0)
		goto error_net;

	/* register the filesystems */
	ret = afs_fs_init();
	if (ret < 0)
		goto error_fs;

	return ret;

error_fs:
	afs_net_exit(&__afs_net);
error_net:
#ifdef CONFIG_AFS_FSCACHE
	fscache_unregister_netfs(&afs_cache_netfs);
error_cache:
#endif
	destroy_workqueue(afs_lock_manager);
error_lockmgr:
	destroy_workqueue(afs_vlocation_update_worker);
error_vl_up:
	destroy_workqueue(afs_async_calls);
error_async:
	destroy_workqueue(afs_wq);
error_afs_wq:
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
	afs_net_exit(&__afs_net);
#ifdef CONFIG_AFS_FSCACHE
	fscache_unregister_netfs(&afs_cache_netfs);
#endif
	destroy_workqueue(afs_lock_manager);
	destroy_workqueue(afs_vlocation_update_worker);
	destroy_workqueue(afs_async_calls);
	destroy_workqueue(afs_wq);
	afs_clean_up_permit_cache();
	rcu_barrier();
}

module_exit(afs_exit);
