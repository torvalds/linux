// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/slab.h>

#include "reg.h"
#include "core.h"
#include "spectrum.h"
#include "spectrum_acl_tcam.h"

struct mlxsw_sp1_acl_tcam_region {
	struct mlxsw_sp_acl_ctcam_region cregion;
	struct mlxsw_sp_acl_tcam_region *region;
	struct {
		struct mlxsw_sp_acl_ctcam_chunk cchunk;
		struct mlxsw_sp_acl_ctcam_entry centry;
		struct mlxsw_sp_acl_rule_info *rulei;
	} catchall;
};

struct mlxsw_sp1_acl_tcam_chunk {
	struct mlxsw_sp_acl_ctcam_chunk cchunk;
};

struct mlxsw_sp1_acl_tcam_entry {
	struct mlxsw_sp_acl_ctcam_entry centry;
};

static int
mlxsw_sp1_acl_ctcam_region_entry_insert(struct mlxsw_sp_acl_ctcam_region *cregion,
					struct mlxsw_sp_acl_ctcam_entry *centry,
					const char *mask)
{
	return 0;
}

static void
mlxsw_sp1_acl_ctcam_region_entry_remove(struct mlxsw_sp_acl_ctcam_region *cregion,
					struct mlxsw_sp_acl_ctcam_entry *centry)
{
}

static const struct mlxsw_sp_acl_ctcam_region_ops
mlxsw_sp1_acl_ctcam_region_ops = {
	.entry_insert = mlxsw_sp1_acl_ctcam_region_entry_insert,
	.entry_remove = mlxsw_sp1_acl_ctcam_region_entry_remove,
};

static int mlxsw_sp1_acl_tcam_init(struct mlxsw_sp *mlxsw_sp, void *priv,
				   struct mlxsw_sp_acl_tcam *tcam)
{
	return 0;
}

static void mlxsw_sp1_acl_tcam_fini(struct mlxsw_sp *mlxsw_sp, void *priv)
{
}

static int
mlxsw_sp1_acl_ctcam_region_catchall_add(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp1_acl_tcam_region *region)
{
	struct mlxsw_sp_acl_rule_info *rulei;
	int err;

	mlxsw_sp_acl_ctcam_chunk_init(&region->cregion,
				      &region->catchall.cchunk,
				      MLXSW_SP_ACL_TCAM_CATCHALL_PRIO);
	rulei = mlxsw_sp_acl_rulei_create(mlxsw_sp->acl, NULL);
	if (IS_ERR(rulei)) {
		err = PTR_ERR(rulei);
		goto err_rulei_create;
	}
	err = mlxsw_sp_acl_rulei_act_continue(rulei);
	if (WARN_ON(err))
		goto err_rulei_act_continue;
	err = mlxsw_sp_acl_rulei_commit(rulei);
	if (err)
		goto err_rulei_commit;
	err = mlxsw_sp_acl_ctcam_entry_add(mlxsw_sp, &region->cregion,
					   &region->catchall.cchunk,
					   &region->catchall.centry,
					   rulei, false);
	if (err)
		goto err_entry_add;
	region->catchall.rulei = rulei;
	return 0;

err_entry_add:
err_rulei_commit:
err_rulei_act_continue:
	mlxsw_sp_acl_rulei_destroy(rulei);
err_rulei_create:
	mlxsw_sp_acl_ctcam_chunk_fini(&region->catchall.cchunk);
	return err;
}

static void
mlxsw_sp1_acl_ctcam_region_catchall_del(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp1_acl_tcam_region *region)
{
	struct mlxsw_sp_acl_rule_info *rulei = region->catchall.rulei;

	mlxsw_sp_acl_ctcam_entry_del(mlxsw_sp, &region->cregion,
				     &region->catchall.cchunk,
				     &region->catchall.centry);
	mlxsw_sp_acl_rulei_destroy(rulei);
	mlxsw_sp_acl_ctcam_chunk_fini(&region->catchall.cchunk);
}

static int
mlxsw_sp1_acl_tcam_region_init(struct mlxsw_sp *mlxsw_sp, void *region_priv,
			       void *tcam_priv,
			       struct mlxsw_sp_acl_tcam_region *_region,
			       void *hints_priv)
{
	struct mlxsw_sp1_acl_tcam_region *region = region_priv;
	int err;

	err = mlxsw_sp_acl_ctcam_region_init(mlxsw_sp, &region->cregion,
					     _region,
					     &mlxsw_sp1_acl_ctcam_region_ops);
	if (err)
		return err;
	err = mlxsw_sp1_acl_ctcam_region_catchall_add(mlxsw_sp, region);
	if (err)
		goto err_catchall_add;
	region->region = _region;
	return 0;

err_catchall_add:
	mlxsw_sp_acl_ctcam_region_fini(&region->cregion);
	return err;
}

static void
mlxsw_sp1_acl_tcam_region_fini(struct mlxsw_sp *mlxsw_sp, void *region_priv)
{
	struct mlxsw_sp1_acl_tcam_region *region = region_priv;

	mlxsw_sp1_acl_ctcam_region_catchall_del(mlxsw_sp, region);
	mlxsw_sp_acl_ctcam_region_fini(&region->cregion);
}

static int
mlxsw_sp1_acl_tcam_region_associate(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_acl_tcam_region *region)
{
	return 0;
}

static void mlxsw_sp1_acl_tcam_chunk_init(void *region_priv, void *chunk_priv,
					  unsigned int priority)
{
	struct mlxsw_sp1_acl_tcam_region *region = region_priv;
	struct mlxsw_sp1_acl_tcam_chunk *chunk = chunk_priv;

	mlxsw_sp_acl_ctcam_chunk_init(&region->cregion, &chunk->cchunk,
				      priority);
}

static void mlxsw_sp1_acl_tcam_chunk_fini(void *chunk_priv)
{
	struct mlxsw_sp1_acl_tcam_chunk *chunk = chunk_priv;

	mlxsw_sp_acl_ctcam_chunk_fini(&chunk->cchunk);
}

static int mlxsw_sp1_acl_tcam_entry_add(struct mlxsw_sp *mlxsw_sp,
					void *region_priv, void *chunk_priv,
					void *entry_priv,
					struct mlxsw_sp_acl_rule_info *rulei)
{
	struct mlxsw_sp1_acl_tcam_region *region = region_priv;
	struct mlxsw_sp1_acl_tcam_chunk *chunk = chunk_priv;
	struct mlxsw_sp1_acl_tcam_entry *entry = entry_priv;

	return mlxsw_sp_acl_ctcam_entry_add(mlxsw_sp, &region->cregion,
					    &chunk->cchunk, &entry->centry,
					    rulei, false);
}

static void mlxsw_sp1_acl_tcam_entry_del(struct mlxsw_sp *mlxsw_sp,
					 void *region_priv, void *chunk_priv,
					 void *entry_priv)
{
	struct mlxsw_sp1_acl_tcam_region *region = region_priv;
	struct mlxsw_sp1_acl_tcam_chunk *chunk = chunk_priv;
	struct mlxsw_sp1_acl_tcam_entry *entry = entry_priv;

	mlxsw_sp_acl_ctcam_entry_del(mlxsw_sp, &region->cregion,
				     &chunk->cchunk, &entry->centry);
}

static int
mlxsw_sp1_acl_tcam_entry_action_replace(struct mlxsw_sp *mlxsw_sp,
					void *region_priv, void *entry_priv,
					struct mlxsw_sp_acl_rule_info *rulei)
{
	return -EOPNOTSUPP;
}

static int
mlxsw_sp1_acl_tcam_region_entry_activity_get(struct mlxsw_sp *mlxsw_sp,
					     struct mlxsw_sp_acl_tcam_region *_region,
					     unsigned int offset,
					     bool *activity)
{
	char ptce2_pl[MLXSW_REG_PTCE2_LEN];
	int err;

	mlxsw_reg_ptce2_pack(ptce2_pl, true, MLXSW_REG_PTCE2_OP_QUERY_CLEAR_ON_READ,
			     _region->tcam_region_info, offset, 0);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ptce2), ptce2_pl);
	if (err)
		return err;
	*activity = mlxsw_reg_ptce2_a_get(ptce2_pl);
	return 0;
}

static int
mlxsw_sp1_acl_tcam_entry_activity_get(struct mlxsw_sp *mlxsw_sp,
				      void *region_priv, void *entry_priv,
				      bool *activity)
{
	struct mlxsw_sp1_acl_tcam_region *region = region_priv;
	struct mlxsw_sp1_acl_tcam_entry *entry = entry_priv;
	unsigned int offset;

	offset = mlxsw_sp_acl_ctcam_entry_offset(&entry->centry);
	return mlxsw_sp1_acl_tcam_region_entry_activity_get(mlxsw_sp,
							    region->region,
							    offset, activity);
}

const struct mlxsw_sp_acl_tcam_ops mlxsw_sp1_acl_tcam_ops = {
	.key_type		= MLXSW_REG_PTAR_KEY_TYPE_FLEX,
	.priv_size		= 0,
	.init			= mlxsw_sp1_acl_tcam_init,
	.fini			= mlxsw_sp1_acl_tcam_fini,
	.region_priv_size	= sizeof(struct mlxsw_sp1_acl_tcam_region),
	.region_init		= mlxsw_sp1_acl_tcam_region_init,
	.region_fini		= mlxsw_sp1_acl_tcam_region_fini,
	.region_associate	= mlxsw_sp1_acl_tcam_region_associate,
	.chunk_priv_size	= sizeof(struct mlxsw_sp1_acl_tcam_chunk),
	.chunk_init		= mlxsw_sp1_acl_tcam_chunk_init,
	.chunk_fini		= mlxsw_sp1_acl_tcam_chunk_fini,
	.entry_priv_size	= sizeof(struct mlxsw_sp1_acl_tcam_entry),
	.entry_add		= mlxsw_sp1_acl_tcam_entry_add,
	.entry_del		= mlxsw_sp1_acl_tcam_entry_del,
	.entry_action_replace	= mlxsw_sp1_acl_tcam_entry_action_replace,
	.entry_activity_get	= mlxsw_sp1_acl_tcam_entry_activity_get,
};
