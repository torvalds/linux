/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2019-2020, Mellanox Technologies inc. All rights reserved.
 */

#ifndef _MLX5_IB_DEVX_H
#define _MLX5_IB_DEVX_H

#include "mlx5_ib.h"

#define MLX5_MAX_DESTROY_INBOX_SIZE_DW MLX5_ST_SZ_DW(delete_fte_in)
struct devx_obj {
	struct mlx5_ib_dev	*ib_dev;
	u64			obj_id;
	u32			dinlen; /* destroy inbox length */
	u32			dinbox[MLX5_MAX_DESTROY_INBOX_SIZE_DW];
	u32			flags;
	union {
		struct mlx5_ib_devx_mr	devx_mr;
		struct mlx5_core_dct	core_dct;
		struct mlx5_core_cq	core_cq;
		u32			flow_counter_bulk_size;
	};
	struct list_head event_sub; /* holds devx_event_subscription entries */
};
#endif /* _MLX5_IB_DEVX_H */
