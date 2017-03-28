/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_dpipe.c
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Arkadi Sharshevsky <arakdis@mellanox.com>
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
#include <net/devlink.h>

#include "spectrum.h"
#include "spectrum_dpipe.h"

enum mlxsw_sp_field_metadata_id {
	MLXSW_SP_DPIPE_FIELD_METADATA_ERIF_PORT,
	MLXSW_SP_DPIPE_FIELD_METADATA_L3_FORWARD,
	MLXSW_SP_DPIPE_FIELD_METADATA_L3_DROP,
};

static struct devlink_dpipe_field mlxsw_sp_dpipe_fields_metadata[] = {
	{ .name = "erif_port",
	  .id = MLXSW_SP_DPIPE_FIELD_METADATA_ERIF_PORT,
	  .bitwidth = 32,
	  .mapping_type = DEVLINK_DPIPE_FIELD_MAPPING_TYPE_IFINDEX,
	},
	{ .name = "l3_forward",
	  .id = MLXSW_SP_DPIPE_FIELD_METADATA_L3_FORWARD,
	  .bitwidth = 1,
	},
	{ .name = "l3_drop",
	  .id = MLXSW_SP_DPIPE_FIELD_METADATA_L3_DROP,
	  .bitwidth = 1,
	},
};

enum mlxsw_sp_dpipe_header_id {
	MLXSW_SP_DPIPE_HEADER_METADATA,
};

static struct devlink_dpipe_header mlxsw_sp_dpipe_header_metadata = {
	.name = "mlxsw_meta",
	.id = MLXSW_SP_DPIPE_HEADER_METADATA,
	.fields = mlxsw_sp_dpipe_fields_metadata,
	.fields_count = ARRAY_SIZE(mlxsw_sp_dpipe_fields_metadata),
};

static struct devlink_dpipe_header *mlxsw_dpipe_headers[] = {
	&mlxsw_sp_dpipe_header_metadata,
};

static struct devlink_dpipe_headers mlxsw_sp_dpipe_headers = {
	.headers = mlxsw_dpipe_headers,
	.headers_count = ARRAY_SIZE(mlxsw_dpipe_headers),
};

static int mlxsw_sp_dpipe_table_erif_actions_dump(void *priv,
						  struct sk_buff *skb)
{
	struct devlink_dpipe_action action = {0};
	int err;

	action.type = DEVLINK_DPIPE_ACTION_TYPE_FIELD_MODIFY;
	action.header = &mlxsw_sp_dpipe_header_metadata;
	action.field_id = MLXSW_SP_DPIPE_FIELD_METADATA_L3_FORWARD;

	err = devlink_dpipe_action_put(skb, &action);
	if (err)
		return err;

	action.type = DEVLINK_DPIPE_ACTION_TYPE_FIELD_MODIFY;
	action.header = &mlxsw_sp_dpipe_header_metadata;
	action.field_id = MLXSW_SP_DPIPE_FIELD_METADATA_L3_DROP;

	return devlink_dpipe_action_put(skb, &action);
}

static int mlxsw_sp_dpipe_table_erif_matches_dump(void *priv,
						  struct sk_buff *skb)
{
	struct devlink_dpipe_match match = {0};

	match.type = DEVLINK_DPIPE_MATCH_TYPE_FIELD_EXACT;
	match.header = &mlxsw_sp_dpipe_header_metadata;
	match.field_id = MLXSW_SP_DPIPE_FIELD_METADATA_ERIF_PORT;

	return devlink_dpipe_match_put(skb, &match);
}

static struct devlink_dpipe_table_ops mlxsw_sp_erif_ops = {
	.matches_dump = mlxsw_sp_dpipe_table_erif_matches_dump,
	.actions_dump = mlxsw_sp_dpipe_table_erif_actions_dump,
};

static int mlxsw_sp_dpipe_erif_table_init(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	u64 table_size;

	table_size = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS);
	return devlink_dpipe_table_register(devlink,
					    MLXSW_SP_DPIPE_TABLE_NAME_ERIF,
					    &mlxsw_sp_erif_ops,
					    mlxsw_sp, table_size,
					    false);
}

static void mlxsw_sp_dpipe_erif_table_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);

	devlink_dpipe_table_unregister(devlink, MLXSW_SP_DPIPE_TABLE_NAME_ERIF);
}

int mlxsw_sp_dpipe_init(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	int err;

	err = devlink_dpipe_headers_register(devlink,
					     &mlxsw_sp_dpipe_headers);
	if (err)
		return err;
	err = mlxsw_sp_dpipe_erif_table_init(mlxsw_sp);
	if (err)
		goto err_erif_register;
	return 0;

err_erif_register:
	devlink_dpipe_headers_unregister(priv_to_devlink(mlxsw_sp->core));
	return err;
}

void mlxsw_sp_dpipe_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);

	mlxsw_sp_dpipe_erif_table_fini(mlxsw_sp);
	devlink_dpipe_headers_unregister(devlink);
}
