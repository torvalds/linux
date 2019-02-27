// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/parman.h>

#include "reg.h"
#include "core.h"
#include "spectrum.h"
#include "spectrum_acl_tcam.h"

static int
mlxsw_sp_acl_ctcam_region_resize(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_acl_tcam_region *region,
				 u16 new_size)
{
	char ptar_pl[MLXSW_REG_PTAR_LEN];

	mlxsw_reg_ptar_pack(ptar_pl, MLXSW_REG_PTAR_OP_RESIZE,
			    region->key_type, new_size, region->id,
			    region->tcam_region_info);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ptar), ptar_pl);
}

static void
mlxsw_sp_acl_ctcam_region_move(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_tcam_region *region,
			       u16 src_offset, u16 dst_offset, u16 size)
{
	char prcr_pl[MLXSW_REG_PRCR_LEN];

	mlxsw_reg_prcr_pack(prcr_pl, MLXSW_REG_PRCR_OP_MOVE,
			    region->tcam_region_info, src_offset,
			    region->tcam_region_info, dst_offset, size);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(prcr), prcr_pl);
}

static int
mlxsw_sp_acl_ctcam_region_entry_insert(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_acl_ctcam_region *cregion,
				       struct mlxsw_sp_acl_ctcam_entry *centry,
				       struct mlxsw_sp_acl_rule_info *rulei,
				       bool fillup_priority)
{
	struct mlxsw_sp_acl_tcam_region *region = cregion->region;
	struct mlxsw_afk *afk = mlxsw_sp_acl_afk(mlxsw_sp->acl);
	char ptce2_pl[MLXSW_REG_PTCE2_LEN];
	unsigned int blocks_count;
	char *act_set;
	u32 priority;
	char *mask;
	char *key;
	int err;

	err = mlxsw_sp_acl_tcam_priority_get(mlxsw_sp, rulei, &priority,
					     fillup_priority);
	if (err)
		return err;

	mlxsw_reg_ptce2_pack(ptce2_pl, true, MLXSW_REG_PTCE2_OP_WRITE_WRITE,
			     region->tcam_region_info,
			     centry->parman_item.index, priority);
	key = mlxsw_reg_ptce2_flex_key_blocks_data(ptce2_pl);
	mask = mlxsw_reg_ptce2_mask_data(ptce2_pl);
	blocks_count = mlxsw_afk_key_info_blocks_count_get(region->key_info);
	mlxsw_afk_encode(afk, region->key_info, &rulei->values, key, mask, 0,
			 blocks_count - 1);

	err = cregion->ops->entry_insert(cregion, centry, mask);
	if (err)
		return err;

	/* Only the first action set belongs here, the rest is in KVD */
	act_set = mlxsw_afa_block_first_set(rulei->act_block);
	mlxsw_reg_ptce2_flex_action_set_memcpy_to(ptce2_pl, act_set);

	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ptce2), ptce2_pl);
	if (err)
		goto err_ptce2_write;

	return 0;

err_ptce2_write:
	cregion->ops->entry_remove(cregion, centry);
	return err;
}

static void
mlxsw_sp_acl_ctcam_region_entry_remove(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_acl_ctcam_region *cregion,
				       struct mlxsw_sp_acl_ctcam_entry *centry)
{
	char ptce2_pl[MLXSW_REG_PTCE2_LEN];

	mlxsw_reg_ptce2_pack(ptce2_pl, false, MLXSW_REG_PTCE2_OP_WRITE_WRITE,
			     cregion->region->tcam_region_info,
			     centry->parman_item.index, 0);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ptce2), ptce2_pl);
	cregion->ops->entry_remove(cregion, centry);
}

static int mlxsw_sp_acl_ctcam_region_parman_resize(void *priv,
						   unsigned long new_count)
{
	struct mlxsw_sp_acl_ctcam_region *cregion = priv;
	struct mlxsw_sp_acl_tcam_region *region = cregion->region;
	struct mlxsw_sp *mlxsw_sp = region->mlxsw_sp;
	u64 max_tcam_rules;

	max_tcam_rules = MLXSW_CORE_RES_GET(mlxsw_sp->core, ACL_MAX_TCAM_RULES);
	if (new_count > max_tcam_rules)
		return -EINVAL;
	return mlxsw_sp_acl_ctcam_region_resize(mlxsw_sp, region, new_count);
}

static void mlxsw_sp_acl_ctcam_region_parman_move(void *priv,
						  unsigned long from_index,
						  unsigned long to_index,
						  unsigned long count)
{
	struct mlxsw_sp_acl_ctcam_region *cregion = priv;
	struct mlxsw_sp_acl_tcam_region *region = cregion->region;
	struct mlxsw_sp *mlxsw_sp = region->mlxsw_sp;

	mlxsw_sp_acl_ctcam_region_move(mlxsw_sp, region,
				       from_index, to_index, count);
}

static const struct parman_ops mlxsw_sp_acl_ctcam_region_parman_ops = {
	.base_count	= MLXSW_SP_ACL_TCAM_REGION_BASE_COUNT,
	.resize_step	= MLXSW_SP_ACL_TCAM_REGION_RESIZE_STEP,
	.resize		= mlxsw_sp_acl_ctcam_region_parman_resize,
	.move		= mlxsw_sp_acl_ctcam_region_parman_move,
	.algo		= PARMAN_ALGO_TYPE_LSORT,
};

int
mlxsw_sp_acl_ctcam_region_init(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_ctcam_region *cregion,
			       struct mlxsw_sp_acl_tcam_region *region,
			       const struct mlxsw_sp_acl_ctcam_region_ops *ops)
{
	cregion->region = region;
	cregion->ops = ops;
	cregion->parman = parman_create(&mlxsw_sp_acl_ctcam_region_parman_ops,
					cregion);
	if (!cregion->parman)
		return -ENOMEM;
	return 0;
}

void mlxsw_sp_acl_ctcam_region_fini(struct mlxsw_sp_acl_ctcam_region *cregion)
{
	parman_destroy(cregion->parman);
}

void mlxsw_sp_acl_ctcam_chunk_init(struct mlxsw_sp_acl_ctcam_region *cregion,
				   struct mlxsw_sp_acl_ctcam_chunk *cchunk,
				   unsigned int priority)
{
	parman_prio_init(cregion->parman, &cchunk->parman_prio, priority);
}

void mlxsw_sp_acl_ctcam_chunk_fini(struct mlxsw_sp_acl_ctcam_chunk *cchunk)
{
	parman_prio_fini(&cchunk->parman_prio);
}

int mlxsw_sp_acl_ctcam_entry_add(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_acl_ctcam_region *cregion,
				 struct mlxsw_sp_acl_ctcam_chunk *cchunk,
				 struct mlxsw_sp_acl_ctcam_entry *centry,
				 struct mlxsw_sp_acl_rule_info *rulei,
				 bool fillup_priority)
{
	int err;

	err = parman_item_add(cregion->parman, &cchunk->parman_prio,
			      &centry->parman_item);
	if (err)
		return err;

	err = mlxsw_sp_acl_ctcam_region_entry_insert(mlxsw_sp, cregion, centry,
						     rulei, fillup_priority);
	if (err)
		goto err_rule_insert;
	return 0;

err_rule_insert:
	parman_item_remove(cregion->parman, &cchunk->parman_prio,
			   &centry->parman_item);
	return err;
}

void mlxsw_sp_acl_ctcam_entry_del(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_acl_ctcam_region *cregion,
				  struct mlxsw_sp_acl_ctcam_chunk *cchunk,
				  struct mlxsw_sp_acl_ctcam_entry *centry)
{
	mlxsw_sp_acl_ctcam_region_entry_remove(mlxsw_sp, cregion, centry);
	parman_item_remove(cregion->parman, &cchunk->parman_prio,
			   &centry->parman_item);
}
