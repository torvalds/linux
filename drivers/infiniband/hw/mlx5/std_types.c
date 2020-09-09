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

static int UVERBS_HANDLER(MLX5_IB_METHOD_PD_QUERY)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_pd *pd =
		uverbs_attr_get_obj(attrs, MLX5_IB_ATTR_QUERY_PD_HANDLE);
	struct mlx5_ib_pd *mpd = to_mpd(pd);

	return uverbs_copy_to(attrs, MLX5_IB_ATTR_QUERY_PD_RESP_PDN,
			      &mpd->pdn, sizeof(mpd->pdn));
}

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_PD_QUERY,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_QUERY_PD_HANDLE,
			UVERBS_OBJECT_PD,
			UVERBS_ACCESS_READ,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(MLX5_IB_ATTR_QUERY_PD_RESP_PDN,
			   UVERBS_ATTR_TYPE(u32),
			   UA_MANDATORY));

ADD_UVERBS_METHODS(mlx5_ib_pd,
		   UVERBS_OBJECT_PD,
		   &UVERBS_METHOD(MLX5_IB_METHOD_PD_QUERY));

const struct uapi_definition mlx5_ib_std_types_defs[] = {
	UAPI_DEF_CHAIN_OBJ_TREE(
		UVERBS_OBJECT_PD,
		&mlx5_ib_pd),
	{},
};
