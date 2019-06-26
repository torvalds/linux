// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Mellanox Technologies. All rights reserved */

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "netdevsim.h"

static struct dentry *nsim_sdev_ddir;

static u32 nsim_sdev_id;

struct netdevsim_shared_dev *nsim_sdev_get(struct netdevsim *joinns)
{
	struct netdevsim_shared_dev *sdev;
	char sdev_ddir_name[10];
	int err;

	if (joinns) {
		if (WARN_ON(!joinns->sdev))
			return ERR_PTR(-EINVAL);
		sdev = joinns->sdev;
		sdev->refcnt++;
		return sdev;
	}

	sdev = kzalloc(sizeof(*sdev), GFP_KERNEL);
	if (!sdev)
		return ERR_PTR(-ENOMEM);
	sdev->refcnt = 1;
	sdev->switch_id = nsim_sdev_id++;

	sprintf(sdev_ddir_name, "%u", sdev->switch_id);
	sdev->ddir = debugfs_create_dir(sdev_ddir_name, nsim_sdev_ddir);
	if (IS_ERR_OR_NULL(sdev->ddir)) {
		err = PTR_ERR_OR_ZERO(sdev->ddir) ?: -EINVAL;
		goto err_sdev_free;
	}

	return sdev;

err_sdev_free:
	nsim_sdev_id--;
	kfree(sdev);
	return ERR_PTR(err);
}

void nsim_sdev_put(struct netdevsim_shared_dev *sdev)
{
	if (--sdev->refcnt)
		return;
	debugfs_remove_recursive(sdev->ddir);
	kfree(sdev);
}

int nsim_sdev_init(void)
{
	nsim_sdev_ddir = debugfs_create_dir(DRV_NAME "_sdev", NULL);
	if (IS_ERR_OR_NULL(nsim_sdev_ddir))
		return -ENOMEM;
	return 0;
}

void nsim_sdev_exit(void)
{
	debugfs_remove_recursive(nsim_sdev_ddir);
}
