/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_SPECTRUM_CNT_H
#define _MLXSW_SPECTRUM_CNT_H

#include "spectrum.h"

enum mlxsw_sp_counter_sub_pool_id {
	MLXSW_SP_COUNTER_SUB_POOL_FLOW,
	MLXSW_SP_COUNTER_SUB_POOL_RIF,
};

int mlxsw_sp_counter_alloc(struct mlxsw_sp *mlxsw_sp,
			   enum mlxsw_sp_counter_sub_pool_id sub_pool_id,
			   unsigned int *p_counter_index);
void mlxsw_sp_counter_free(struct mlxsw_sp *mlxsw_sp,
			   enum mlxsw_sp_counter_sub_pool_id sub_pool_id,
			   unsigned int counter_index);
int mlxsw_sp_counter_pool_init(struct mlxsw_sp *mlxsw_sp);
void mlxsw_sp_counter_pool_fini(struct mlxsw_sp *mlxsw_sp);

#endif
