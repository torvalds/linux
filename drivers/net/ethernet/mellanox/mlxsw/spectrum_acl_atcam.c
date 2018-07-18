/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_acl_atcam.c
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2018 Jiri Pirko <jiri@mellanox.com>
 * Copyright (c) 2018 Ido Schimmel <idosch@mellanox.com>
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

#include <linux/kernel.h>
#include <linux/errno.h>

#include "reg.h"
#include "core.h"
#include "spectrum.h"
#include "spectrum_acl_tcam.h"

int mlxsw_sp_acl_atcam_region_associate(struct mlxsw_sp *mlxsw_sp,
					u16 region_id)
{
	char perar_pl[MLXSW_REG_PERAR_LEN];
	/* For now, just assume that every region has 12 key blocks */
	u16 hw_region = region_id * 3;
	u64 max_regions;

	max_regions = MLXSW_CORE_RES_GET(mlxsw_sp->core, ACL_MAX_REGIONS);
	if (hw_region >= max_regions)
		return -ENOBUFS;

	mlxsw_reg_perar_pack(perar_pl, region_id, hw_region);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(perar), perar_pl);
}

static int mlxsw_sp_acl_atcam_region_param_init(struct mlxsw_sp *mlxsw_sp,
						u16 region_id)
{
	char percr_pl[MLXSW_REG_PERCR_LEN];

	mlxsw_reg_percr_pack(percr_pl, region_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(percr), percr_pl);
}

static int
mlxsw_sp_acl_atcam_region_erp_init(struct mlxsw_sp *mlxsw_sp,
				   u16 region_id)
{
	char pererp_pl[MLXSW_REG_PERERP_LEN];

	mlxsw_reg_pererp_pack(pererp_pl, region_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pererp), pererp_pl);
}

int mlxsw_sp_acl_atcam_region_init(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_acl_tcam_region *region)
{
	int err;

	err = mlxsw_sp_acl_atcam_region_associate(mlxsw_sp, region->id);
	if (err)
		return err;
	err = mlxsw_sp_acl_atcam_region_param_init(mlxsw_sp, region->id);
	if (err)
		return err;
	err = mlxsw_sp_acl_atcam_region_erp_init(mlxsw_sp, region->id);
	if (err)
		return err;

	return 0;
}
