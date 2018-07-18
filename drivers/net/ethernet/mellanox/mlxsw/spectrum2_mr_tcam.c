/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum2_mr_tcam.c
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2018 Jiri Pirko <jiri@mellanox.com>
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

#include "core_acl_flex_actions.h"
#include "spectrum.h"
#include "spectrum_mr.h"

static int
mlxsw_sp2_mr_tcam_route_create(struct mlxsw_sp *mlxsw_sp, void *priv,
			       void *route_priv,
			       struct mlxsw_sp_mr_route_key *key,
			       struct mlxsw_afa_block *afa_block,
			       enum mlxsw_sp_mr_route_prio prio)
{
	return 0;
}

static void
mlxsw_sp2_mr_tcam_route_destroy(struct mlxsw_sp *mlxsw_sp, void *priv,
				void *route_priv,
				struct mlxsw_sp_mr_route_key *key)
{
}

static int
mlxsw_sp2_mr_tcam_route_update(struct mlxsw_sp *mlxsw_sp,
			       void *route_priv,
			       struct mlxsw_sp_mr_route_key *key,
			       struct mlxsw_afa_block *afa_block)
{
	return 0;
}

static int mlxsw_sp2_mr_tcam_init(struct mlxsw_sp *mlxsw_sp, void *priv)
{
	return 0;
}

static void mlxsw_sp2_mr_tcam_fini(void *priv)
{
}

const struct mlxsw_sp_mr_tcam_ops mlxsw_sp2_mr_tcam_ops = {
	.init = mlxsw_sp2_mr_tcam_init,
	.fini = mlxsw_sp2_mr_tcam_fini,
	.route_create = mlxsw_sp2_mr_tcam_route_create,
	.route_destroy = mlxsw_sp2_mr_tcam_route_destroy,
	.route_update = mlxsw_sp2_mr_tcam_route_update,
};
