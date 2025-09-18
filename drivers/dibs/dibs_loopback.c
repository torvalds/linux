// SPDX-License-Identifier: GPL-2.0
/*
 *  Functions for dibs loopback/loopback-ism device.
 *
 *  Copyright (c) 2024, Alibaba Inc.
 *
 *  Author: Wen Gu <guwen@linux.alibaba.com>
 *          Tony Lu <tonylu@linux.alibaba.com>
 *
 */

#include <linux/dibs.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "dibs_loopback.h"

static const char dibs_lo_dev_name[] = "lo";
/* global loopback device */
static struct dibs_lo_dev *lo_dev;

static u16 dibs_lo_get_fabric_id(struct dibs_dev *dibs)
{
	return DIBS_LOOPBACK_FABRIC;
}

static const struct dibs_dev_ops dibs_lo_ops = {
	.get_fabric_id = dibs_lo_get_fabric_id,
};

static int dibs_lo_dev_probe(void)
{
	struct dibs_lo_dev *ldev;
	struct dibs_dev *dibs;
	int ret;

	ldev = kzalloc(sizeof(*ldev), GFP_KERNEL);
	if (!ldev)
		return -ENOMEM;

	dibs = dibs_dev_alloc();
	if (!dibs) {
		kfree(ldev);
		return -ENOMEM;
	}

	ldev->dibs = dibs;
	dibs->drv_priv = ldev;
	uuid_gen(&dibs->gid);
	dibs->ops = &dibs_lo_ops;

	dibs->dev.parent = NULL;
	dev_set_name(&dibs->dev, "%s", dibs_lo_dev_name);

	ret = dibs_dev_add(dibs);
	if (ret)
		goto err_reg;
	lo_dev = ldev;
	return 0;

err_reg:
	/* pairs with dibs_dev_alloc() */
	put_device(&dibs->dev);
	kfree(ldev);

	return ret;
}

static void dibs_lo_dev_remove(void)
{
	if (!lo_dev)
		return;

	dibs_dev_del(lo_dev->dibs);
	/* pairs with dibs_dev_alloc() */
	put_device(&lo_dev->dibs->dev);
	kfree(lo_dev);
	lo_dev = NULL;
}

int dibs_loopback_init(void)
{
	return dibs_lo_dev_probe();
}

void dibs_loopback_exit(void)
{
	dibs_lo_dev_remove();
}
