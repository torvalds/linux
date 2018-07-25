/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum2_acl_tcam.c
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

#include "spectrum.h"
#include "spectrum_acl_tcam.h"
#include "core_acl_flex_actions.h"

struct mlxsw_sp2_acl_tcam {
	struct mlxsw_sp_acl_atcam atcam;
	u32 kvdl_index;
	unsigned int kvdl_count;
};

struct mlxsw_sp2_acl_tcam_region {
	struct mlxsw_sp_acl_atcam_region aregion;
	struct mlxsw_sp_acl_tcam_region *region;
};

struct mlxsw_sp2_acl_tcam_chunk {
	struct mlxsw_sp_acl_atcam_chunk achunk;
};

struct mlxsw_sp2_acl_tcam_entry {
	struct mlxsw_sp_acl_atcam_entry aentry;
	struct mlxsw_afa_block *act_block;
};

static int
mlxsw_sp2_acl_ctcam_region_entry_insert(struct mlxsw_sp_acl_ctcam_region *cregion,
					struct mlxsw_sp_acl_ctcam_entry *centry,
					const char *mask)
{
	struct mlxsw_sp_acl_atcam_region *aregion;
	struct mlxsw_sp_acl_atcam_entry *aentry;
	struct mlxsw_sp_acl_erp *erp;

	aregion = mlxsw_sp_acl_tcam_cregion_aregion(cregion);
	aentry = mlxsw_sp_acl_tcam_centry_aentry(centry);

	erp = mlxsw_sp_acl_erp_get(aregion, mask, true);
	if (IS_ERR(erp))
		return PTR_ERR(erp);
	aentry->erp = erp;

	return 0;
}

static void
mlxsw_sp2_acl_ctcam_region_entry_remove(struct mlxsw_sp_acl_ctcam_region *cregion,
					struct mlxsw_sp_acl_ctcam_entry *centry)
{
	struct mlxsw_sp_acl_atcam_region *aregion;
	struct mlxsw_sp_acl_atcam_entry *aentry;

	aregion = mlxsw_sp_acl_tcam_cregion_aregion(cregion);
	aentry = mlxsw_sp_acl_tcam_centry_aentry(centry);

	mlxsw_sp_acl_erp_put(aregion, aentry->erp);
}

static const struct mlxsw_sp_acl_ctcam_region_ops
mlxsw_sp2_acl_ctcam_region_ops = {
	.entry_insert = mlxsw_sp2_acl_ctcam_region_entry_insert,
	.entry_remove = mlxsw_sp2_acl_ctcam_region_entry_remove,
};

static int mlxsw_sp2_acl_tcam_init(struct mlxsw_sp *mlxsw_sp, void *priv,
				   struct mlxsw_sp_acl_tcam *_tcam)
{
	struct mlxsw_sp2_acl_tcam *tcam = priv;
	struct mlxsw_afa_block *afa_block;
	char pefa_pl[MLXSW_REG_PEFA_LEN];
	char pgcr_pl[MLXSW_REG_PGCR_LEN];
	char *enc_actions;
	int i;
	int err;

	tcam->kvdl_count = _tcam->max_regions;
	err = mlxsw_sp_kvdl_alloc(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_ACTSET,
				  tcam->kvdl_count, &tcam->kvdl_index);
	if (err)
		return err;

	/* Create flex action block, set default action (continue)
	 * but don't commit. We need just the current set encoding
	 * to be written using PEFA register to all indexes for all regions.
	 */
	afa_block = mlxsw_afa_block_create(mlxsw_sp->afa);
	if (!afa_block) {
		err = -ENOMEM;
		goto err_afa_block;
	}
	err = mlxsw_afa_block_continue(afa_block);
	if (WARN_ON(err))
		goto err_afa_block_continue;
	enc_actions = mlxsw_afa_block_cur_set(afa_block);

	for (i = 0; i < tcam->kvdl_count; i++) {
		mlxsw_reg_pefa_pack(pefa_pl, tcam->kvdl_index + i,
				    true, enc_actions);
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pefa), pefa_pl);
		if (err)
			goto err_pefa_write;
	}
	mlxsw_reg_pgcr_pack(pgcr_pl, tcam->kvdl_index);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pgcr), pgcr_pl);
	if (err)
		goto err_pgcr_write;

	err = mlxsw_sp_acl_atcam_init(mlxsw_sp, &tcam->atcam);
	if (err)
		goto err_atcam_init;

	mlxsw_afa_block_destroy(afa_block);
	return 0;

err_atcam_init:
err_pgcr_write:
err_pefa_write:
err_afa_block_continue:
	mlxsw_afa_block_destroy(afa_block);
err_afa_block:
	mlxsw_sp_kvdl_free(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_ACTSET,
			   tcam->kvdl_count, tcam->kvdl_index);
	return err;
}

static void mlxsw_sp2_acl_tcam_fini(struct mlxsw_sp *mlxsw_sp, void *priv)
{
	struct mlxsw_sp2_acl_tcam *tcam = priv;

	mlxsw_sp_acl_atcam_fini(mlxsw_sp, &tcam->atcam);
	mlxsw_sp_kvdl_free(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_ACTSET,
			   tcam->kvdl_count, tcam->kvdl_index);
}

static int
mlxsw_sp2_acl_tcam_region_init(struct mlxsw_sp *mlxsw_sp, void *region_priv,
			       void *tcam_priv,
			       struct mlxsw_sp_acl_tcam_region *_region)
{
	struct mlxsw_sp2_acl_tcam_region *region = region_priv;
	struct mlxsw_sp2_acl_tcam *tcam = tcam_priv;

	region->region = _region;

	return mlxsw_sp_acl_atcam_region_init(mlxsw_sp, &tcam->atcam,
					      &region->aregion, _region,
					      &mlxsw_sp2_acl_ctcam_region_ops);
}

static void
mlxsw_sp2_acl_tcam_region_fini(struct mlxsw_sp *mlxsw_sp, void *region_priv)
{
	struct mlxsw_sp2_acl_tcam_region *region = region_priv;

	mlxsw_sp_acl_atcam_region_fini(&region->aregion);
}

static int
mlxsw_sp2_acl_tcam_region_associate(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_acl_tcam_region *region)
{
	return mlxsw_sp_acl_atcam_region_associate(mlxsw_sp, region->id);
}

static void mlxsw_sp2_acl_tcam_chunk_init(void *region_priv, void *chunk_priv,
					  unsigned int priority)
{
	struct mlxsw_sp2_acl_tcam_region *region = region_priv;
	struct mlxsw_sp2_acl_tcam_chunk *chunk = chunk_priv;

	mlxsw_sp_acl_atcam_chunk_init(&region->aregion, &chunk->achunk,
				      priority);
}

static void mlxsw_sp2_acl_tcam_chunk_fini(void *chunk_priv)
{
	struct mlxsw_sp2_acl_tcam_chunk *chunk = chunk_priv;

	mlxsw_sp_acl_atcam_chunk_fini(&chunk->achunk);
}

static int mlxsw_sp2_acl_tcam_entry_add(struct mlxsw_sp *mlxsw_sp,
					void *region_priv, void *chunk_priv,
					void *entry_priv,
					struct mlxsw_sp_acl_rule_info *rulei)
{
	struct mlxsw_sp2_acl_tcam_region *region = region_priv;
	struct mlxsw_sp2_acl_tcam_chunk *chunk = chunk_priv;
	struct mlxsw_sp2_acl_tcam_entry *entry = entry_priv;

	entry->act_block = rulei->act_block;
	return mlxsw_sp_acl_atcam_entry_add(mlxsw_sp, &region->aregion,
					    &chunk->achunk, &entry->aentry,
					    rulei);
}

static void mlxsw_sp2_acl_tcam_entry_del(struct mlxsw_sp *mlxsw_sp,
					 void *region_priv, void *chunk_priv,
					 void *entry_priv)
{
	struct mlxsw_sp2_acl_tcam_region *region = region_priv;
	struct mlxsw_sp2_acl_tcam_chunk *chunk = chunk_priv;
	struct mlxsw_sp2_acl_tcam_entry *entry = entry_priv;

	mlxsw_sp_acl_atcam_entry_del(mlxsw_sp, &region->aregion, &chunk->achunk,
				     &entry->aentry);
}

static int
mlxsw_sp2_acl_tcam_entry_activity_get(struct mlxsw_sp *mlxsw_sp,
				      void *region_priv, void *entry_priv,
				      bool *activity)
{
	struct mlxsw_sp2_acl_tcam_entry *entry = entry_priv;

	return mlxsw_afa_block_activity_get(entry->act_block, activity);
}

const struct mlxsw_sp_acl_tcam_ops mlxsw_sp2_acl_tcam_ops = {
	.key_type		= MLXSW_REG_PTAR_KEY_TYPE_FLEX2,
	.priv_size		= sizeof(struct mlxsw_sp2_acl_tcam),
	.init			= mlxsw_sp2_acl_tcam_init,
	.fini			= mlxsw_sp2_acl_tcam_fini,
	.region_priv_size	= sizeof(struct mlxsw_sp2_acl_tcam_region),
	.region_init		= mlxsw_sp2_acl_tcam_region_init,
	.region_fini		= mlxsw_sp2_acl_tcam_region_fini,
	.region_associate	= mlxsw_sp2_acl_tcam_region_associate,
	.chunk_priv_size	= sizeof(struct mlxsw_sp2_acl_tcam_chunk),
	.chunk_init		= mlxsw_sp2_acl_tcam_chunk_init,
	.chunk_fini		= mlxsw_sp2_acl_tcam_chunk_fini,
	.entry_priv_size	= sizeof(struct mlxsw_sp2_acl_tcam_entry),
	.entry_add		= mlxsw_sp2_acl_tcam_entry_add,
	.entry_del		= mlxsw_sp2_acl_tcam_entry_del,
	.entry_activity_get	= mlxsw_sp2_acl_tcam_entry_activity_get,
};
