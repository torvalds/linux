/*
 * Copyright (C) 2015, SUSE
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 */


#include <linux/module.h>
#include <linux/dlm.h>
#include <linux/sched.h>
#include "md.h"
#include "md-cluster.h"

#define LVB_SIZE	64

struct dlm_lock_resource {
	dlm_lockspace_t *ls;
	struct dlm_lksb lksb;
	char *name; /* lock name. */
	uint32_t flags; /* flags to pass to dlm_lock() */
	void (*bast)(void *arg, int mode); /* blocking AST function pointer*/
	struct completion completion; /* completion for synchronized locking */
};

static void sync_ast(void *arg)
{
	struct dlm_lock_resource *res;

	res = (struct dlm_lock_resource *) arg;
	complete(&res->completion);
}

static int dlm_lock_sync(struct dlm_lock_resource *res, int mode)
{
	int ret = 0;

	init_completion(&res->completion);
	ret = dlm_lock(res->ls, mode, &res->lksb,
			res->flags, res->name, strlen(res->name),
			0, sync_ast, res, res->bast);
	if (ret)
		return ret;
	wait_for_completion(&res->completion);
	return res->lksb.sb_status;
}

static int dlm_unlock_sync(struct dlm_lock_resource *res)
{
	return dlm_lock_sync(res, DLM_LOCK_NL);
}

static struct dlm_lock_resource *lockres_init(dlm_lockspace_t *lockspace,
		char *name, void (*bastfn)(void *arg, int mode), int with_lvb)
{
	struct dlm_lock_resource *res = NULL;
	int ret, namelen;

	res = kzalloc(sizeof(struct dlm_lock_resource), GFP_KERNEL);
	if (!res)
		return NULL;
	res->ls = lockspace;
	namelen = strlen(name);
	res->name = kzalloc(namelen + 1, GFP_KERNEL);
	if (!res->name) {
		pr_err("md-cluster: Unable to allocate resource name for resource %s\n", name);
		goto out_err;
	}
	strlcpy(res->name, name, namelen + 1);
	if (with_lvb) {
		res->lksb.sb_lvbptr = kzalloc(LVB_SIZE, GFP_KERNEL);
		if (!res->lksb.sb_lvbptr) {
			pr_err("md-cluster: Unable to allocate LVB for resource %s\n", name);
			goto out_err;
		}
		res->flags = DLM_LKF_VALBLK;
	}

	if (bastfn)
		res->bast = bastfn;

	res->flags |= DLM_LKF_EXPEDITE;

	ret = dlm_lock_sync(res, DLM_LOCK_NL);
	if (ret) {
		pr_err("md-cluster: Unable to lock NL on new lock resource %s\n", name);
		goto out_err;
	}
	res->flags &= ~DLM_LKF_EXPEDITE;
	res->flags |= DLM_LKF_CONVERT;

	return res;
out_err:
	kfree(res->lksb.sb_lvbptr);
	kfree(res->name);
	kfree(res);
	return NULL;
}

static void lockres_free(struct dlm_lock_resource *res)
{
	if (!res)
		return;

	init_completion(&res->completion);
	dlm_unlock(res->ls, res->lksb.sb_lkid, 0, &res->lksb, res);
	wait_for_completion(&res->completion);

	kfree(res->name);
	kfree(res->lksb.sb_lvbptr);
	kfree(res);
}

static int join(struct mddev *mddev, int nodes)
{
	return 0;
}

static int leave(struct mddev *mddev)
{
	return 0;
}

static struct md_cluster_operations cluster_ops = {
	.join   = join,
	.leave  = leave,
};

static int __init cluster_init(void)
{
	pr_warn("md-cluster: EXPERIMENTAL. Use with caution\n");
	pr_info("Registering Cluster MD functions\n");
	register_md_cluster_operations(&cluster_ops, THIS_MODULE);
	return 0;
}

static void cluster_exit(void)
{
	unregister_md_cluster_operations();
}

module_init(cluster_init);
module_exit(cluster_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Clustering support for MD");
