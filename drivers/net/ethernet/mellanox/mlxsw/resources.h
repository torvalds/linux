/*
 * drivers/net/ethernet/mellanox/mlxsw/resources.h
 * Copyright (c) 2016-2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016-2017 Jiri Pirko <jiri@mellanox.com>
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

#ifndef _MLXSW_RESOURCES_H
#define _MLXSW_RESOURCES_H

#include <linux/kernel.h>
#include <linux/types.h>

enum mlxsw_res_id {
	MLXSW_RES_ID_KVD_SIZE,
	MLXSW_RES_ID_KVD_SINGLE_MIN_SIZE,
	MLXSW_RES_ID_KVD_DOUBLE_MIN_SIZE,
	MLXSW_RES_ID_MAX_TRAP_GROUPS,
	MLXSW_RES_ID_MAX_SPAN,
	MLXSW_RES_ID_MAX_SYSTEM_PORT,
	MLXSW_RES_ID_MAX_LAG,
	MLXSW_RES_ID_MAX_LAG_MEMBERS,
	MLXSW_RES_ID_MAX_BUFFER_SIZE,
	MLXSW_RES_ID_ACL_MAX_TCAM_REGIONS,
	MLXSW_RES_ID_ACL_MAX_TCAM_RULES,
	MLXSW_RES_ID_ACL_MAX_REGIONS,
	MLXSW_RES_ID_ACL_MAX_GROUPS,
	MLXSW_RES_ID_ACL_MAX_GROUP_SIZE,
	MLXSW_RES_ID_ACL_FLEX_KEYS,
	MLXSW_RES_ID_ACL_MAX_ACTION_PER_RULE,
	MLXSW_RES_ID_ACL_ACTIONS_PER_SET,
	MLXSW_RES_ID_MAX_CPU_POLICERS,
	MLXSW_RES_ID_MAX_VRS,
	MLXSW_RES_ID_MAX_RIFS,

	/* Internal resources.
	 * Determined by the SW, not queried from the HW.
	 */
	MLXSW_RES_ID_KVD_SINGLE_SIZE,
	MLXSW_RES_ID_KVD_DOUBLE_SIZE,
	MLXSW_RES_ID_KVD_LINEAR_SIZE,

	__MLXSW_RES_ID_MAX,
};

static u16 mlxsw_res_ids[] = {
	[MLXSW_RES_ID_KVD_SIZE] = 0x1001,
	[MLXSW_RES_ID_KVD_SINGLE_MIN_SIZE] = 0x1002,
	[MLXSW_RES_ID_KVD_DOUBLE_MIN_SIZE] = 0x1003,
	[MLXSW_RES_ID_MAX_TRAP_GROUPS] = 0x2201,
	[MLXSW_RES_ID_MAX_SPAN] = 0x2420,
	[MLXSW_RES_ID_MAX_SYSTEM_PORT] = 0x2502,
	[MLXSW_RES_ID_MAX_LAG] = 0x2520,
	[MLXSW_RES_ID_MAX_LAG_MEMBERS] = 0x2521,
	[MLXSW_RES_ID_MAX_BUFFER_SIZE] = 0x2802,	/* Bytes */
	[MLXSW_RES_ID_ACL_MAX_TCAM_REGIONS] = 0x2901,
	[MLXSW_RES_ID_ACL_MAX_TCAM_RULES] = 0x2902,
	[MLXSW_RES_ID_ACL_MAX_REGIONS] = 0x2903,
	[MLXSW_RES_ID_ACL_MAX_GROUPS] = 0x2904,
	[MLXSW_RES_ID_ACL_MAX_GROUP_SIZE] = 0x2905,
	[MLXSW_RES_ID_ACL_FLEX_KEYS] = 0x2910,
	[MLXSW_RES_ID_ACL_MAX_ACTION_PER_RULE] = 0x2911,
	[MLXSW_RES_ID_ACL_ACTIONS_PER_SET] = 0x2912,
	[MLXSW_RES_ID_MAX_CPU_POLICERS] = 0x2A13,
	[MLXSW_RES_ID_MAX_VRS] = 0x2C01,
	[MLXSW_RES_ID_MAX_RIFS] = 0x2C02,
};

struct mlxsw_res {
	bool valid[__MLXSW_RES_ID_MAX];
	u64 values[__MLXSW_RES_ID_MAX];
};

static inline bool mlxsw_res_valid(struct mlxsw_res *res,
				   enum mlxsw_res_id res_id)
{
	return res->valid[res_id];
}

#define MLXSW_RES_VALID(res, short_res_id)			\
	mlxsw_res_valid(res, MLXSW_RES_ID_##short_res_id)

static inline u64 mlxsw_res_get(struct mlxsw_res *res,
				enum mlxsw_res_id res_id)
{
	if (WARN_ON(!res->valid[res_id]))
		return 0;
	return res->values[res_id];
}

#define MLXSW_RES_GET(res, short_res_id)			\
	mlxsw_res_get(res, MLXSW_RES_ID_##short_res_id)

static inline void mlxsw_res_set(struct mlxsw_res *res,
				 enum mlxsw_res_id res_id, u64 value)
{
	res->valid[res_id] = true;
	res->values[res_id] = value;
}

#define MLXSW_RES_SET(res, short_res_id, value)			\
	mlxsw_res_set(res, MLXSW_RES_ID_##short_res_id, value)

static inline void mlxsw_res_parse(struct mlxsw_res *res, u16 id, u64 value)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mlxsw_res_ids); i++) {
		if (mlxsw_res_ids[i] == id) {
			mlxsw_res_set(res, i, value);
			return;
		}
	}
}

#endif
