// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2018, Mellanox Technologies inc.  All rights reserved.
 */

#include <rdma/ib_user_verbs.h>
#include <rdma/ib_verbs.h>
#include <rdma/uverbs_types.h>
#include <rdma/uverbs_ioctl.h>
#include <rdma/mlx5_user_ioctl_cmds.h>
#include <rdma/ib_umem.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/fs.h>
#include "mlx5_ib.h"

#define UVERBS_MODULE_NAME mlx5_ib
#include <rdma/uverbs_named_ioctl.h>

static const struct uverbs_attr_spec mlx5_ib_flow_type[] = {
	[MLX5_IB_FLOW_TYPE_NORMAL] = {
		.type = UVERBS_ATTR_TYPE_PTR_IN,
		.u.ptr = {
			.len = sizeof(u16), /* data is priority */
			.min_len = sizeof(u16),
		}
	},
	[MLX5_IB_FLOW_TYPE_SNIFFER] = {
		.type = UVERBS_ATTR_TYPE_PTR_IN,
		UVERBS_ATTR_NO_DATA(),
	},
	[MLX5_IB_FLOW_TYPE_ALL_DEFAULT] = {
		.type = UVERBS_ATTR_TYPE_PTR_IN,
		UVERBS_ATTR_NO_DATA(),
	},
	[MLX5_IB_FLOW_TYPE_MC_DEFAULT] = {
		.type = UVERBS_ATTR_TYPE_PTR_IN,
		UVERBS_ATTR_NO_DATA(),
	},
};

static int flow_matcher_cleanup(struct ib_uobject *uobject,
				enum rdma_remove_reason why)
{
	struct mlx5_ib_flow_matcher *obj = uobject->object;
	int ret;

	ret = ib_destroy_usecnt(&obj->usecnt, why, uobject);
	if (ret)
		return ret;

	kfree(obj);
	return 0;
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_FLOW_MATCHER_CREATE)(
	struct ib_device *ib_dev, struct ib_uverbs_file *file,
	struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj = uverbs_attr_get_uobject(
		attrs, MLX5_IB_ATTR_FLOW_MATCHER_CREATE_HANDLE);
	struct mlx5_ib_dev *dev = to_mdev(uobj->context->device);
	struct mlx5_ib_flow_matcher *obj;
	int err;

	obj = kzalloc(sizeof(struct mlx5_ib_flow_matcher), GFP_KERNEL);
	if (!obj)
		return -ENOMEM;

	obj->mask_len = uverbs_attr_get_len(
		attrs, MLX5_IB_ATTR_FLOW_MATCHER_MATCH_MASK);
	err = uverbs_copy_from(&obj->matcher_mask,
			       attrs,
			       MLX5_IB_ATTR_FLOW_MATCHER_MATCH_MASK);
	if (err)
		goto end;

	obj->flow_type = uverbs_attr_get_enum_id(
		attrs, MLX5_IB_ATTR_FLOW_MATCHER_FLOW_TYPE);

	if (obj->flow_type == MLX5_IB_FLOW_TYPE_NORMAL) {
		err = uverbs_copy_from(&obj->priority,
				       attrs,
				       MLX5_IB_ATTR_FLOW_MATCHER_FLOW_TYPE);
		if (err)
			goto end;
	}

	err = uverbs_copy_from(&obj->match_criteria_enable,
			       attrs,
			       MLX5_IB_ATTR_FLOW_MATCHER_MATCH_CRITERIA);
	if (err)
		goto end;

	uobj->object = obj;
	obj->mdev = dev->mdev;
	atomic_set(&obj->usecnt, 0);
	return 0;

end:
	kfree(obj);
	return err;
}

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_FLOW_MATCHER_CREATE,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_FLOW_MATCHER_CREATE_HANDLE,
			MLX5_IB_OBJECT_FLOW_MATCHER,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(
		MLX5_IB_ATTR_FLOW_MATCHER_MATCH_MASK,
		UVERBS_ATTR_SIZE(1, sizeof(struct mlx5_ib_match_params)),
		UA_MANDATORY),
	UVERBS_ATTR_ENUM_IN(MLX5_IB_ATTR_FLOW_MATCHER_FLOW_TYPE,
			    mlx5_ib_flow_type,
			    UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(MLX5_IB_ATTR_FLOW_MATCHER_MATCH_CRITERIA,
			   UVERBS_ATTR_TYPE(u8),
			   UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD_DESTROY(
	MLX5_IB_METHOD_FLOW_MATCHER_DESTROY,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_FLOW_MATCHER_DESTROY_HANDLE,
			MLX5_IB_OBJECT_FLOW_MATCHER,
			UVERBS_ACCESS_DESTROY,
			UA_MANDATORY));

DECLARE_UVERBS_NAMED_OBJECT(MLX5_IB_OBJECT_FLOW_MATCHER,
			    UVERBS_TYPE_ALLOC_IDR(flow_matcher_cleanup),
			    &UVERBS_METHOD(MLX5_IB_METHOD_FLOW_MATCHER_CREATE),
			    &UVERBS_METHOD(MLX5_IB_METHOD_FLOW_MATCHER_DESTROY));

DECLARE_UVERBS_OBJECT_TREE(flow_objects,
			   &UVERBS_OBJECT(MLX5_IB_OBJECT_FLOW_MATCHER));
