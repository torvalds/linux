/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_cnt.h
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Arkadi Sharshevsky <arkdis@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

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
