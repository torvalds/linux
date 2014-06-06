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
	struct completion completion; /* completion for synchronized locking */
	void (*bast)(void *arg, int mode); /* blocking AST function pointer*/
	struct mddev *mddev; /* pointing back to mddev. */
};

struct md_cluster_info {
	/* dlm lock space and resources for clustered raid. */
	dlm_lockspace_t *lockspace;
	int slot_number;
	struct completion completion;
	struct dlm_lock_resource *sb_lock;
	struct mutex sb_mutex;
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

static struct dlm_lock_resource *lockres_init(struct mddev *mddev,
		char *name, void (*bastfn)(void *arg, int mode), int with_lvb)
{
	struct dlm_lock_resource *res = NULL;
	int ret, namelen;
	struct md_cluster_info *cinfo = mddev->cluster_info;

	res = kzalloc(sizeof(struct dlm_lock_resource), GFP_KERNEL);
	if (!res)
		return NULL;
	res->ls = cinfo->lockspace;
	res->mddev = mddev;
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

static char *pretty_uuid(char *dest, char *src)
{
	int i, len = 0;

	for (i = 0; i < 16; i++) {
		if (i == 4 || i == 6 || i == 8 || i == 10)
			len += sprintf(dest + len, "-");
		len += sprintf(dest + len, "%02x", (__u8)src[i]);
	}
	return dest;
}

static void recover_prep(void *arg)
{
}

static void recover_slot(void *arg, struct dlm_slot *slot)
{
	struct mddev *mddev = arg;
	struct md_cluster_info *cinfo = mddev->cluster_info;

	pr_info("md-cluster: %s Node %d/%d down. My slot: %d. Initiating recovery.\n",
			mddev->bitmap_info.cluster_name,
			slot->nodeid, slot->slot,
			cinfo->slot_number);
}

static void recover_done(void *arg, struct dlm_slot *slots,
		int num_slots, int our_slot,
		uint32_t generation)
{
	struct mddev *mddev = arg;
	struct md_cluster_info *cinfo = mddev->cluster_info;

	cinfo->slot_number = our_slot;
	complete(&cinfo->completion);
}

static const struct dlm_lockspace_ops md_ls_ops = {
	.recover_prep = recover_prep,
	.recover_slot = recover_slot,
	.recover_done = recover_done,
};

static int join(struct mddev *mddev, int nodes)
{
	struct md_cluster_info *cinfo;
	int ret, ops_rv;
	char str[64];

	if (!try_module_get(THIS_MODULE))
		return -ENOENT;

	cinfo = kzalloc(sizeof(struct md_cluster_info), GFP_KERNEL);
	if (!cinfo)
		return -ENOMEM;

	init_completion(&cinfo->completion);

	mutex_init(&cinfo->sb_mutex);
	mddev->cluster_info = cinfo;

	memset(str, 0, 64);
	pretty_uuid(str, mddev->uuid);
	ret = dlm_new_lockspace(str, mddev->bitmap_info.cluster_name,
				DLM_LSFL_FS, LVB_SIZE,
				&md_ls_ops, mddev, &ops_rv, &cinfo->lockspace);
	if (ret)
		goto err;
	wait_for_completion(&cinfo->completion);
	if (nodes <= cinfo->slot_number) {
		pr_err("md-cluster: Slot allotted(%d) greater than available slots(%d)", cinfo->slot_number - 1,
			nodes);
		ret = -ERANGE;
		goto err;
	}
	cinfo->sb_lock = lockres_init(mddev, "cmd-super",
					NULL, 0);
	if (!cinfo->sb_lock) {
		ret = -ENOMEM;
		goto err;
	}
	return 0;
err:
	if (cinfo->lockspace)
		dlm_release_lockspace(cinfo->lockspace, 2);
	mddev->cluster_info = NULL;
	kfree(cinfo);
	module_put(THIS_MODULE);
	return ret;
}

static int leave(struct mddev *mddev)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;

	if (!cinfo)
		return 0;
	lockres_free(cinfo->sb_lock);
	dlm_release_lockspace(cinfo->lockspace, 2);
	return 0;
}

/* slot_number(): Returns the MD slot number to use
 * DLM starts the slot numbers from 1, wheras cluster-md
 * wants the number to be from zero, so we deduct one
 */
static int slot_number(struct mddev *mddev)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;

	return cinfo->slot_number - 1;
}

static struct md_cluster_operations cluster_ops = {
	.join   = join,
	.leave  = leave,
	.slot_number = slot_number,
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
