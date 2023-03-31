// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/rwsem.h>
#include <linux/gunyah_rsc_mgr.h>

#include "rsc_mgr.h"

static struct gh_rm_platform_ops *rm_platform_ops;
static DECLARE_RWSEM(rm_platform_ops_lock);

int gh_rm_platform_pre_mem_share(struct gh_rm *rm, struct gh_rm_mem_parcel *mem_parcel)
{
	int ret = 0;

	down_read(&rm_platform_ops_lock);
	if (rm_platform_ops && rm_platform_ops->pre_mem_share)
		ret = rm_platform_ops->pre_mem_share(rm, mem_parcel);
	up_read(&rm_platform_ops_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(gh_rm_platform_pre_mem_share);

int gh_rm_platform_post_mem_reclaim(struct gh_rm *rm, struct gh_rm_mem_parcel *mem_parcel)
{
	int ret = 0;

	down_read(&rm_platform_ops_lock);
	if (rm_platform_ops && rm_platform_ops->post_mem_reclaim)
		ret = rm_platform_ops->post_mem_reclaim(rm, mem_parcel);
	up_read(&rm_platform_ops_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(gh_rm_platform_post_mem_reclaim);

int gh_rm_register_platform_ops(struct gh_rm_platform_ops *platform_ops)
{
	int ret = 0;

	down_write(&rm_platform_ops_lock);
	if (!rm_platform_ops)
		rm_platform_ops = platform_ops;
	else
		ret = -EEXIST;
	up_write(&rm_platform_ops_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(gh_rm_register_platform_ops);

void gh_rm_unregister_platform_ops(struct gh_rm_platform_ops *platform_ops)
{
	down_write(&rm_platform_ops_lock);
	if (rm_platform_ops == platform_ops)
		rm_platform_ops = NULL;
	up_write(&rm_platform_ops_lock);
}
EXPORT_SYMBOL_GPL(gh_rm_unregister_platform_ops);

static void _devm_gh_rm_unregister_platform_ops(void *data)
{
	gh_rm_unregister_platform_ops(data);
}

int devm_gh_rm_register_platform_ops(struct device *dev, struct gh_rm_platform_ops *ops)
{
	int ret;

	ret = gh_rm_register_platform_ops(ops);
	if (ret)
		return ret;

	return devm_add_action(dev, _devm_gh_rm_unregister_platform_ops, ops);
}
EXPORT_SYMBOL_GPL(devm_gh_rm_register_platform_ops);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Gunyah Platform Hooks");
