// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS client file system
 *
 * Copyright (C) 2002,5 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/sched.h>
#include <linux/random.h>
#include <linux/proc_fs.h>
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
static struct proc_dir_entry *afs_proc_symlink;

#if defined(CONFIG_ALPHA)
const char afs_init_sysname[] = "alpha_linux26";
#elif defined(CONFIG_X86_64)
const char afs_init_sysname[] = "amd64_linux26";
#elif defined(CONFIG_ARM)
const char afs_init_sysname[] = "arm_linux26";
#elif defined(CONFIG_ARM64)
const char afs_init_sysname[] = "aarch64_linux26";
#elif defined(CONFIG_X86_32)
const char afs_init_sysname[] = "i386_linux26";
#elif defined(CONFIG_IA64)
const char afs_init_sysname[] = "ia64_linux26";
#elif defined(CONFIG_PPC64)
const char afs_init_sysname[] = "ppc64_linux26";
#elif defined(CONFIG_PPC32)
const char afs_init_sysname[] = "ppc_linux26";
#elif defined(CONFIG_S390)
#ifdef CONFIG_64BIT
const char afs_init_sysname[] = "s390x_linux26";
#else
const char afs_init_sysname[] = "s390_linux26";
#endif
#elif defined(CONFIG_SPARC64)
const char afs_init_sysname[] = "sparc64_linux26";
#elif defined(CONFIG_SPARC32)
const char afs_init_sysname[] = "sparc_linux26";
#else
const char afs_init_sysname[] = "unknown_linux26";
#endif

/*
 * Initialise an AFS network namespace record.
 */
static int __net_init afs_net_init(struct net *net_ns)
{
	struct afs_sysnames *sysnames;
	struct afs_net *net = afs_net(net_ns);
	int ret;

	net->net = net_ns;
	net->live = true;
	generate_random_uuid((unsigned char *)&net->uuid);

	INIT_WORK(&net->charge_preallocation_work, afs_charge_preallocation);
	mutex_init(&net->socket_mutex);

	net->cells = RB_ROOT;
	seqlock_init(&net->cells_lock);
	INIT_WORK(&net->cells_manager, afs_manage_cells);
	timer_setup(&net->cells_timer, afs_cells_timer, 0);

	mutex_init(&net->proc_cells_lock);
	INIT_HLIST_HEAD(&net->proc_cells);

	seqlock_init(&net->fs_lock);
	net->fs_servers = RB_ROOT;
	INIT_LIST_HEAD(&net->fs_updates);
	INIT_HLIST_HEAD(&net->fs_proc);

	INIT_HLIST_HEAD(&net->fs_addresses4);
	INIT_HLIST_HEAD(&net->fs_addresses6);
	seqlock_init(&net->fs_addr_lock);

	INIT_WORK(&net->fs_manager, afs_manage_servers);
	timer_setup(&net->fs_timer, afs_servers_timer, 0);

	ret = -ENOMEM;
	sysnames = kzalloc(sizeof(*sysnames), GFP_KERNEL);
	if (!sysnames)
		goto error_sysnames;
	sysnames->subs[0] = (char *)&afs_init_sysname;
	sysnames->nr = 1;
	refcount_set(&sysnames->usage, 1);
	net->sysnames = sysnames;
	rwlock_init(&net->sysnames_lock);

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
	net->live = false;
	afs_cell_purge(net);
	afs_purge_servers(net);
error_cell_init:
	net->live = false;
	afs_proc_cleanup(net);
error_proc:
	afs_put_sysnames(net->sysnames);
error_sysnames:
	net->live = false;
	return ret;
}

/*
 * Clean up and destroy an AFS network namespace record.
 */
static void __net_exit afs_net_exit(struct net *net_ns)
{
	struct afs_net *net = afs_net(net_ns);

	net->live = false;
	afs_cell_purge(net);
	afs_purge_servers(net);
	afs_close_socket(net);
	afs_proc_cleanup(net);
	afs_put_sysnames(net->sysnames);
}

static struct pernet_operations afs_net_ops = {
	.init	= afs_net_init,
	.exit	= afs_net_exit,
	.id	= &afs_net_id,
	.size	= sizeof(struct afs_net),
};

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
	afs_lock_manager = alloc_workqueue("kafs_lockd", WQ_MEM_RECLAIM, 0);
	if (!afs_lock_manager)
		goto error_lockmgr;

#ifdef CONFIG_AFS_FSCACHE
	/* we want to be able to cache */
	ret = fscache_register_netfs(&afs_cache_netfs);
	if (ret < 0)
		goto error_cache;
#endif

	ret = register_pernet_subsys(&afs_net_ops);
	if (ret < 0)
		goto error_net;

	/* register the filesystems */
	ret = afs_fs_init();
	if (ret < 0)
		goto error_fs;

	afs_proc_symlink = proc_symlink("fs/afs", NULL, "../self/net/afs");
	if (IS_ERR(afs_proc_symlink)) {
		ret = PTR_ERR(afs_proc_symlink);
		goto error_proc;
	}

	return ret;

error_proc:
	afs_fs_exit();
error_fs:
	unregister_pernet_subsys(&afs_net_ops);
error_net:
#ifdef CONFIG_AFS_FSCACHE
	fscache_unregister_netfs(&afs_cache_netfs);
error_cache:
#endif
	destroy_workqueue(afs_lock_manager);
error_lockmgr:
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

	proc_remove(afs_proc_symlink);
	afs_fs_exit();
	unregister_pernet_subsys(&afs_net_ops);
#ifdef CONFIG_AFS_FSCACHE
	fscache_unregister_netfs(&afs_cache_netfs);
#endif
	destroy_workqueue(afs_lock_manager);
	destroy_workqueue(afs_async_calls);
	destroy_workqueue(afs_wq);
	afs_clean_up_permit_cache();
	rcu_barrier();
}

module_exit(afs_exit);
