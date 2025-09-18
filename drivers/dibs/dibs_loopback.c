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

/* global loopback device */
static struct dibs_lo_dev *lo_dev;

static void dibs_lo_dev_exit(struct dibs_lo_dev *ldev)
{
	dibs_dev_del(ldev->dibs);
}

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

	ret = dibs_dev_add(dibs);
	if (ret)
		goto err_reg;
	lo_dev = ldev;
	return 0;

err_reg:
	/* pairs with dibs_dev_alloc() */
	kfree(dibs);
	kfree(ldev);

	return ret;
}

static void dibs_lo_dev_remove(void)
{
	if (!lo_dev)
		return;

	dibs_lo_dev_exit(lo_dev);
	/* pairs with dibs_dev_alloc() */
	kfree(lo_dev->dibs);
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
