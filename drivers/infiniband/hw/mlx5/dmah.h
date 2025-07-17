/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#ifndef _MLX5_IB_DMAH_H
#define _MLX5_IB_DMAH_H

#include "mlx5_ib.h"

extern const struct ib_device_ops mlx5_ib_dev_dmah_ops;

struct mlx5_ib_dmah {
	struct ib_dmah ibdmah;
	u16 st_index;
};

static inline struct mlx5_ib_dmah *to_mdmah(struct ib_dmah *ibdmah)
{
	return container_of(ibdmah, struct mlx5_ib_dmah, ibdmah);
}

#endif /* _MLX5_IB_DMAH_H */
