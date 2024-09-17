// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2020, Mellanox Technologies inc.  All rights reserved.
 */

#include <rdma/uverbs_ioctl.h>
#include <rdma/mlx5_user_ioctl_cmds.h>
#include <rdma/mlx5_user_ioctl_verbs.h>
#include <linux/mlx5/driver.h>
#include "mlx5_ib.h"

#define UVERBS_MODULE_NAME mlx5_ib
#include <rdma/uverbs_named_ioctl.h>

static bool pp_is_supported(struct ib_device *device)
{
	struct mlx5_ib_dev *dev = to_mdev(device);

	return (MLX5_CAP_GEN(dev->mdev, qos) &&
		MLX5_CAP_QOS(dev->mdev, packet_pacing) &&
		MLX5_CAP_QOS(dev->mdev, packet_pacing_uid));
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_PP_OBJ_ALLOC)(
	struct uverbs_attr_bundle *attrs)
{
	u8 rl_raw[MLX5_ST_SZ_BYTES(set_pp_rate_limit_context)] = {};
	struct ib_uobject *uobj = uverbs_attr_get_uobject(attrs,
		MLX5_IB_ATTR_PP_OBJ_ALLOC_HANDLE);
	struct mlx5_ib_dev *dev;
	struct mlx5_ib_ucontext *c;
	struct mlx5_ib_pp *pp_entry;
	void *in_ctx;
	u16 uid;
	int inlen;
	u32 flags;
	int err;

	c = to_mucontext(ib_uverbs_get_ucontext(attrs));
	if (IS_ERR(c))
		return PTR_ERR(c);

	/* The allocated entry can be used only by a DEVX context */
	if (!c->devx_uid)
		return -EINVAL;

	dev = to_mdev(c->ibucontext.device);
	pp_entry = kzalloc(sizeof(*pp_entry), GFP_KERNEL);
	if (!pp_entry)
		return -ENOMEM;

	in_ctx = uverbs_attr_get_alloced_ptr(attrs,
					     MLX5_IB_ATTR_PP_OBJ_ALLOC_CTX);
	inlen = uverbs_attr_get_len(attrs,
				    MLX5_IB_ATTR_PP_OBJ_ALLOC_CTX);
	memcpy(rl_raw, in_ctx, inlen);
	err = uverbs_get_flags32(&flags, attrs,
		MLX5_IB_ATTR_PP_OBJ_ALLOC_FLAGS,
		MLX5_IB_UAPI_PP_ALLOC_FLAGS_DEDICATED_INDEX);
	if (err)
		goto err;

	uid = (flags & MLX5_IB_UAPI_PP_ALLOC_FLAGS_DEDICATED_INDEX) ?
		c->devx_uid : MLX5_SHARED_RESOURCE_UID;

	err = mlx5_rl_add_rate_raw(dev->mdev, rl_raw, uid,
			(flags & MLX5_IB_UAPI_PP_ALLOC_FLAGS_DEDICATED_INDEX),
			&pp_entry->index);
	if (err)
		goto err;

	pp_entry->mdev = dev->mdev;
	uobj->object = pp_entry;
	uverbs_finalize_uobj_create(attrs, MLX5_IB_ATTR_PP_OBJ_ALLOC_HANDLE);

	err = uverbs_copy_to(attrs, MLX5_IB_ATTR_PP_OBJ_ALLOC_INDEX,
			     &pp_entry->index, sizeof(pp_entry->index));
	return err;

err:
	kfree(pp_entry);
	return err;
}

static int pp_obj_cleanup(struct ib_uobject *uobject,
			  enum rdma_remove_reason why,
			  struct uverbs_attr_bundle *attrs)
{
	struct mlx5_ib_pp *pp_entry = uobject->object;

	mlx5_rl_remove_rate_raw(pp_entry->mdev, pp_entry->index);
	kfree(pp_entry);
	return 0;
}

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_PP_OBJ_ALLOC,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_PP_OBJ_ALLOC_HANDLE,
			MLX5_IB_OBJECT_PP,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_IN(
		MLX5_IB_ATTR_PP_OBJ_ALLOC_CTX,
		UVERBS_ATTR_SIZE(1,
			MLX5_ST_SZ_BYTES(set_pp_rate_limit_context)),
		UA_MANDATORY,
		UA_ALLOC_AND_COPY),
	UVERBS_ATTR_FLAGS_IN(MLX5_IB_ATTR_PP_OBJ_ALLOC_FLAGS,
			enum mlx5_ib_uapi_pp_alloc_flags,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(MLX5_IB_ATTR_PP_OBJ_ALLOC_INDEX,
			   UVERBS_ATTR_TYPE(u16),
			   UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD_DESTROY(
	MLX5_IB_METHOD_PP_OBJ_DESTROY,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_PP_OBJ_DESTROY_HANDLE,
			MLX5_IB_OBJECT_PP,
			UVERBS_ACCESS_DESTROY,
			UA_MANDATORY));

DECLARE_UVERBS_NAMED_OBJECT(MLX5_IB_OBJECT_PP,
			    UVERBS_TYPE_ALLOC_IDR(pp_obj_cleanup),
			    &UVERBS_METHOD(MLX5_IB_METHOD_PP_OBJ_ALLOC),
			    &UVERBS_METHOD(MLX5_IB_METHOD_PP_OBJ_DESTROY));


const struct uapi_definition mlx5_ib_qos_defs[] = {
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(
		MLX5_IB_OBJECT_PP,
		UAPI_DEF_IS_OBJ_SUPPORTED(pp_is_supported)),
	{},
};
