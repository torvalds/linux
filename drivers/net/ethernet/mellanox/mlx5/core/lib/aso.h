/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_LIB_ASO_H__
#define __MLX5_LIB_ASO_H__

#include <linux/mlx5/qp.h>
#include "mlx5_core.h"

#define MLX5_ASO_WQEBBS \
	(DIV_ROUND_UP(sizeof(struct mlx5_aso_wqe), MLX5_SEND_WQE_BB))
#define MLX5_ASO_WQEBBS_DATA \
	(DIV_ROUND_UP(sizeof(struct mlx5_aso_wqe_data), MLX5_SEND_WQE_BB))
#define ASO_CTRL_READ_EN BIT(0)
#define MLX5_WQE_CTRL_WQE_OPC_MOD_SHIFT 24
#define MLX5_MACSEC_ASO_DS_CNT (DIV_ROUND_UP(sizeof(struct mlx5_aso_wqe), MLX5_SEND_WQE_DS))

struct mlx5_wqe_aso_ctrl_seg {
	__be32  va_h;
	__be32  va_l; /* include read_enable */
	__be32  l_key;
	u8      data_mask_mode;
	u8      condition_1_0_operand;
	u8      condition_1_0_offset;
	u8      data_offset_condition_operand;
	__be32  condition_0_data;
	__be32  condition_0_mask;
	__be32  condition_1_data;
	__be32  condition_1_mask;
	__be64  bitwise_data;
	__be64  data_mask;
};

struct mlx5_wqe_aso_data_seg {
	__be32  bytewise_data[16];
};

struct mlx5_aso_wqe {
	struct mlx5_wqe_ctrl_seg      ctrl;
	struct mlx5_wqe_aso_ctrl_seg  aso_ctrl;
};

struct mlx5_aso_wqe_data {
	struct mlx5_wqe_ctrl_seg      ctrl;
	struct mlx5_wqe_aso_ctrl_seg  aso_ctrl;
	struct mlx5_wqe_aso_data_seg  aso_data;
};

enum {
	MLX5_ASO_LOGICAL_AND,
	MLX5_ASO_LOGICAL_OR,
};

enum {
	MLX5_ASO_ALWAYS_FALSE,
	MLX5_ASO_ALWAYS_TRUE,
	MLX5_ASO_EQUAL,
	MLX5_ASO_NOT_EQUAL,
	MLX5_ASO_GREATER_OR_EQUAL,
	MLX5_ASO_LESSER_OR_EQUAL,
	MLX5_ASO_LESSER,
	MLX5_ASO_GREATER,
	MLX5_ASO_CYCLIC_GREATER,
	MLX5_ASO_CYCLIC_LESSER,
};

enum {
	MLX5_ASO_DATA_MASK_MODE_BITWISE_64BIT,
	MLX5_ASO_DATA_MASK_MODE_BYTEWISE_64BYTE,
	MLX5_ASO_DATA_MASK_MODE_CALCULATED_64BYTE,
};

enum {
	MLX5_ACCESS_ASO_OPC_MOD_FLOW_METER = 0x2,
	MLX5_ACCESS_ASO_OPC_MOD_MACSEC = 0x5,
};

struct mlx5_aso;

void *mlx5_aso_get_wqe(struct mlx5_aso *aso);
void mlx5_aso_build_wqe(struct mlx5_aso *aso, u8 ds_cnt,
			struct mlx5_aso_wqe *aso_wqe,
			u32 obj_id, u32 opc_mode);
void mlx5_aso_post_wqe(struct mlx5_aso *aso, bool with_data,
		       struct mlx5_wqe_ctrl_seg *doorbell_cseg);
int mlx5_aso_poll_cq(struct mlx5_aso *aso, bool with_data, u32 interval_ms);

struct mlx5_aso *mlx5_aso_create(struct mlx5_core_dev *mdev, u32 pdn);
void mlx5_aso_destroy(struct mlx5_aso *aso);
#endif /* __MLX5_LIB_ASO_H__ */
