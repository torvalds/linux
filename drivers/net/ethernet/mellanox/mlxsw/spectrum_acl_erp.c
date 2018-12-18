// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/bitmap.h>
#include <linux/errno.h>
#include <linux/genalloc.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/rhashtable.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>

#include "core.h"
#include "reg.h"
#include "spectrum.h"
#include "spectrum_acl_tcam.h"

/* gen_pool_alloc() returns 0 when allocation fails, so use an offset */
#define MLXSW_SP_ACL_ERP_GENALLOC_OFFSET 0x100
#define MLXSW_SP_ACL_ERP_MAX_PER_REGION 16

struct mlxsw_sp_acl_erp_core {
	unsigned int erpt_entries_size[MLXSW_SP_ACL_ATCAM_REGION_TYPE_MAX + 1];
	struct gen_pool *erp_tables;
	struct mlxsw_sp *mlxsw_sp;
	unsigned int num_erp_banks;
};

struct mlxsw_sp_acl_erp_key {
	char mask[MLXSW_REG_PTCEX_FLEX_KEY_BLOCKS_LEN];
	bool ctcam;
};

struct mlxsw_sp_acl_erp {
	struct mlxsw_sp_acl_erp_key key;
	u8 id;
	u8 index;
	refcount_t refcnt;
	DECLARE_BITMAP(mask_bitmap, MLXSW_SP_ACL_TCAM_MASK_LEN);
	struct list_head list;
	struct rhash_head ht_node;
	struct mlxsw_sp_acl_erp_table *erp_table;
};

struct mlxsw_sp_acl_erp_master_mask {
	DECLARE_BITMAP(bitmap, MLXSW_SP_ACL_TCAM_MASK_LEN);
	unsigned int count[MLXSW_SP_ACL_TCAM_MASK_LEN];
};

struct mlxsw_sp_acl_erp_table {
	struct mlxsw_sp_acl_erp_master_mask master_mask;
	DECLARE_BITMAP(erp_id_bitmap, MLXSW_SP_ACL_ERP_MAX_PER_REGION);
	DECLARE_BITMAP(erp_index_bitmap, MLXSW_SP_ACL_ERP_MAX_PER_REGION);
	struct list_head atcam_erps_list;
	struct rhashtable erp_ht;
	struct mlxsw_sp_acl_erp_core *erp_core;
	struct mlxsw_sp_acl_atcam_region *aregion;
	const struct mlxsw_sp_acl_erp_table_ops *ops;
	unsigned long base_index;
	unsigned int num_atcam_erps;
	unsigned int num_max_atcam_erps;
	unsigned int num_ctcam_erps;
};

static const struct rhashtable_params mlxsw_sp_acl_erp_ht_params = {
	.key_len = sizeof(struct mlxsw_sp_acl_erp_key),
	.key_offset = offsetof(struct mlxsw_sp_acl_erp, key),
	.head_offset = offsetof(struct mlxsw_sp_acl_erp, ht_node),
};

struct mlxsw_sp_acl_erp_table_ops {
	struct mlxsw_sp_acl_erp *
		(*erp_create)(struct mlxsw_sp_acl_erp_table *erp_table,
			      struct mlxsw_sp_acl_erp_key *key);
	void (*erp_destroy)(struct mlxsw_sp_acl_erp_table *erp_table,
			    struct mlxsw_sp_acl_erp *erp);
};

static struct mlxsw_sp_acl_erp *
mlxsw_sp_acl_erp_mask_create(struct mlxsw_sp_acl_erp_table *erp_table,
			     struct mlxsw_sp_acl_erp_key *key);
static void
mlxsw_sp_acl_erp_mask_destroy(struct mlxsw_sp_acl_erp_table *erp_table,
			      struct mlxsw_sp_acl_erp *erp);
static struct mlxsw_sp_acl_erp *
mlxsw_sp_acl_erp_second_mask_create(struct mlxsw_sp_acl_erp_table *erp_table,
				    struct mlxsw_sp_acl_erp_key *key);
static void
mlxsw_sp_acl_erp_second_mask_destroy(struct mlxsw_sp_acl_erp_table *erp_table,
				     struct mlxsw_sp_acl_erp *erp);
static struct mlxsw_sp_acl_erp *
mlxsw_sp_acl_erp_first_mask_create(struct mlxsw_sp_acl_erp_table *erp_table,
				   struct mlxsw_sp_acl_erp_key *key);
static void
mlxsw_sp_acl_erp_first_mask_destroy(struct mlxsw_sp_acl_erp_table *erp_table,
				    struct mlxsw_sp_acl_erp *erp);
static void
mlxsw_sp_acl_erp_no_mask_destroy(struct mlxsw_sp_acl_erp_table *erp_table,
				 struct mlxsw_sp_acl_erp *erp);

static const struct mlxsw_sp_acl_erp_table_ops erp_multiple_masks_ops = {
	.erp_create = mlxsw_sp_acl_erp_mask_create,
	.erp_destroy = mlxsw_sp_acl_erp_mask_destroy,
};

static const struct mlxsw_sp_acl_erp_table_ops erp_two_masks_ops = {
	.erp_create = mlxsw_sp_acl_erp_mask_create,
	.erp_destroy = mlxsw_sp_acl_erp_second_mask_destroy,
};

static const struct mlxsw_sp_acl_erp_table_ops erp_single_mask_ops = {
	.erp_create = mlxsw_sp_acl_erp_second_mask_create,
	.erp_destroy = mlxsw_sp_acl_erp_first_mask_destroy,
};

static const struct mlxsw_sp_acl_erp_table_ops erp_no_mask_ops = {
	.erp_create = mlxsw_sp_acl_erp_first_mask_create,
	.erp_destroy = mlxsw_sp_acl_erp_no_mask_destroy,
};

bool mlxsw_sp_acl_erp_is_ctcam_erp(const struct mlxsw_sp_acl_erp *erp)
{
	return erp->key.ctcam;
}

u8 mlxsw_sp_acl_erp_id(const struct mlxsw_sp_acl_erp *erp)
{
	return erp->id;
}

static unsigned int
mlxsw_sp_acl_erp_table_entry_size(const struct mlxsw_sp_acl_erp_table *erp_table)
{
	struct mlxsw_sp_acl_atcam_region *aregion = erp_table->aregion;
	struct mlxsw_sp_acl_erp_core *erp_core = erp_table->erp_core;

	return erp_core->erpt_entries_size[aregion->type];
}

static int mlxsw_sp_acl_erp_id_get(struct mlxsw_sp_acl_erp_table *erp_table,
				   u8 *p_id)
{
	u8 id;

	id = find_first_zero_bit(erp_table->erp_id_bitmap,
				 MLXSW_SP_ACL_ERP_MAX_PER_REGION);
	if (id < MLXSW_SP_ACL_ERP_MAX_PER_REGION) {
		__set_bit(id, erp_table->erp_id_bitmap);
		*p_id = id;
		return 0;
	}

	return -ENOBUFS;
}

static void mlxsw_sp_acl_erp_id_put(struct mlxsw_sp_acl_erp_table *erp_table,
				    u8 id)
{
	__clear_bit(id, erp_table->erp_id_bitmap);
}

static void
mlxsw_sp_acl_erp_master_mask_bit_set(unsigned long bit,
				     struct mlxsw_sp_acl_erp_master_mask *mask)
{
	if (mask->count[bit]++ == 0)
		__set_bit(bit, mask->bitmap);
}

static void
mlxsw_sp_acl_erp_master_mask_bit_clear(unsigned long bit,
				       struct mlxsw_sp_acl_erp_master_mask *mask)
{
	if (--mask->count[bit] == 0)
		__clear_bit(bit, mask->bitmap);
}

static int
mlxsw_sp_acl_erp_master_mask_update(struct mlxsw_sp_acl_erp_table *erp_table)
{
	struct mlxsw_sp_acl_tcam_region *region = erp_table->aregion->region;
	struct mlxsw_sp *mlxsw_sp = region->mlxsw_sp;
	char percr_pl[MLXSW_REG_PERCR_LEN];
	char *master_mask;

	mlxsw_reg_percr_pack(percr_pl, region->id);
	master_mask = mlxsw_reg_percr_master_mask_data(percr_pl);
	bitmap_to_arr32((u32 *) master_mask, erp_table->master_mask.bitmap,
			MLXSW_SP_ACL_TCAM_MASK_LEN);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(percr), percr_pl);
}

static int
mlxsw_sp_acl_erp_master_mask_set(struct mlxsw_sp_acl_erp_table *erp_table,
				 const struct mlxsw_sp_acl_erp *erp)
{
	unsigned long bit;
	int err;

	for_each_set_bit(bit, erp->mask_bitmap, MLXSW_SP_ACL_TCAM_MASK_LEN)
		mlxsw_sp_acl_erp_master_mask_bit_set(bit,
						     &erp_table->master_mask);

	err = mlxsw_sp_acl_erp_master_mask_update(erp_table);
	if (err)
		goto err_master_mask_update;

	return 0;

err_master_mask_update:
	for_each_set_bit(bit, erp->mask_bitmap, MLXSW_SP_ACL_TCAM_MASK_LEN)
		mlxsw_sp_acl_erp_master_mask_bit_clear(bit,
						       &erp_table->master_mask);
	return err;
}

static int
mlxsw_sp_acl_erp_master_mask_clear(struct mlxsw_sp_acl_erp_table *erp_table,
				   const struct mlxsw_sp_acl_erp *erp)
{
	unsigned long bit;
	int err;

	for_each_set_bit(bit, erp->mask_bitmap, MLXSW_SP_ACL_TCAM_MASK_LEN)
		mlxsw_sp_acl_erp_master_mask_bit_clear(bit,
						       &erp_table->master_mask);

	err = mlxsw_sp_acl_erp_master_mask_update(erp_table);
	if (err)
		goto err_master_mask_update;

	return 0;

err_master_mask_update:
	for_each_set_bit(bit, erp->mask_bitmap, MLXSW_SP_ACL_TCAM_MASK_LEN)
		mlxsw_sp_acl_erp_master_mask_bit_set(bit,
						     &erp_table->master_mask);
	return err;
}

static struct mlxsw_sp_acl_erp *
mlxsw_sp_acl_erp_generic_create(struct mlxsw_sp_acl_erp_table *erp_table,
				struct mlxsw_sp_acl_erp_key *key)
{
	struct mlxsw_sp_acl_erp *erp;
	int err;

	erp = kzalloc(sizeof(*erp), GFP_KERNEL);
	if (!erp)
		return ERR_PTR(-ENOMEM);

	err = mlxsw_sp_acl_erp_id_get(erp_table, &erp->id);
	if (err)
		goto err_erp_id_get;

	memcpy(&erp->key, key, sizeof(*key));
	bitmap_from_arr32(erp->mask_bitmap, (u32 *) key->mask,
			  MLXSW_SP_ACL_TCAM_MASK_LEN);
	list_add(&erp->list, &erp_table->atcam_erps_list);
	refcount_set(&erp->refcnt, 1);
	erp_table->num_atcam_erps++;
	erp->erp_table = erp_table;

	err = mlxsw_sp_acl_erp_master_mask_set(erp_table, erp);
	if (err)
		goto err_master_mask_set;

	err = rhashtable_insert_fast(&erp_table->erp_ht, &erp->ht_node,
				     mlxsw_sp_acl_erp_ht_params);
	if (err)
		goto err_rhashtable_insert;

	return erp;

err_rhashtable_insert:
	mlxsw_sp_acl_erp_master_mask_clear(erp_table, erp);
err_master_mask_set:
	erp_table->num_atcam_erps--;
	list_del(&erp->list);
	mlxsw_sp_acl_erp_id_put(erp_table, erp->id);
err_erp_id_get:
	kfree(erp);
	return ERR_PTR(err);
}

static void
mlxsw_sp_acl_erp_generic_destroy(struct mlxsw_sp_acl_erp *erp)
{
	struct mlxsw_sp_acl_erp_table *erp_table = erp->erp_table;

	rhashtable_remove_fast(&erp_table->erp_ht, &erp->ht_node,
			       mlxsw_sp_acl_erp_ht_params);
	mlxsw_sp_acl_erp_master_mask_clear(erp_table, erp);
	erp_table->num_atcam_erps--;
	list_del(&erp->list);
	mlxsw_sp_acl_erp_id_put(erp_table, erp->id);
	kfree(erp);
}

static int
mlxsw_sp_acl_erp_table_alloc(struct mlxsw_sp_acl_erp_core *erp_core,
			     unsigned int num_erps,
			     enum mlxsw_sp_acl_atcam_region_type region_type,
			     unsigned long *p_index)
{
	unsigned int num_rows, entry_size;

	/* We only allow allocations of entire rows */
	if (num_erps % erp_core->num_erp_banks != 0)
		return -EINVAL;

	entry_size = erp_core->erpt_entries_size[region_type];
	num_rows = num_erps / erp_core->num_erp_banks;

	*p_index = gen_pool_alloc(erp_core->erp_tables, num_rows * entry_size);
	if (*p_index == 0)
		return -ENOBUFS;
	*p_index -= MLXSW_SP_ACL_ERP_GENALLOC_OFFSET;

	return 0;
}

static void
mlxsw_sp_acl_erp_table_free(struct mlxsw_sp_acl_erp_core *erp_core,
			    unsigned int num_erps,
			    enum mlxsw_sp_acl_atcam_region_type region_type,
			    unsigned long index)
{
	unsigned long base_index;
	unsigned int entry_size;
	size_t size;

	entry_size = erp_core->erpt_entries_size[region_type];
	base_index = index + MLXSW_SP_ACL_ERP_GENALLOC_OFFSET;
	size = num_erps / erp_core->num_erp_banks * entry_size;
	gen_pool_free(erp_core->erp_tables, base_index, size);
}

static struct mlxsw_sp_acl_erp *
mlxsw_sp_acl_erp_table_master_rp(struct mlxsw_sp_acl_erp_table *erp_table)
{
	if (!list_is_singular(&erp_table->atcam_erps_list))
		return NULL;

	return list_first_entry(&erp_table->atcam_erps_list,
				struct mlxsw_sp_acl_erp, list);
}

static int mlxsw_sp_acl_erp_index_get(struct mlxsw_sp_acl_erp_table *erp_table,
				      u8 *p_index)
{
	u8 index;

	index = find_first_zero_bit(erp_table->erp_index_bitmap,
				    erp_table->num_max_atcam_erps);
	if (index < erp_table->num_max_atcam_erps) {
		__set_bit(index, erp_table->erp_index_bitmap);
		*p_index = index;
		return 0;
	}

	return -ENOBUFS;
}

static void mlxsw_sp_acl_erp_index_put(struct mlxsw_sp_acl_erp_table *erp_table,
				       u8 index)
{
	__clear_bit(index, erp_table->erp_index_bitmap);
}

static void
mlxsw_sp_acl_erp_table_locate(const struct mlxsw_sp_acl_erp_table *erp_table,
			      const struct mlxsw_sp_acl_erp *erp,
			      u8 *p_erpt_bank, u8 *p_erpt_index)
{
	unsigned int entry_size = mlxsw_sp_acl_erp_table_entry_size(erp_table);
	struct mlxsw_sp_acl_erp_core *erp_core = erp_table->erp_core;
	unsigned int row;

	*p_erpt_bank = erp->index % erp_core->num_erp_banks;
	row = erp->index / erp_core->num_erp_banks;
	*p_erpt_index = erp_table->base_index + row * entry_size;
}

static int
mlxsw_sp_acl_erp_table_erp_add(struct mlxsw_sp_acl_erp_table *erp_table,
			       struct mlxsw_sp_acl_erp *erp)
{
	struct mlxsw_sp *mlxsw_sp = erp_table->erp_core->mlxsw_sp;
	enum mlxsw_reg_perpt_key_size key_size;
	char perpt_pl[MLXSW_REG_PERPT_LEN];
	u8 erpt_bank, erpt_index;

	mlxsw_sp_acl_erp_table_locate(erp_table, erp, &erpt_bank, &erpt_index);
	key_size = (enum mlxsw_reg_perpt_key_size) erp_table->aregion->type;
	mlxsw_reg_perpt_pack(perpt_pl, erpt_bank, erpt_index, key_size, erp->id,
			     0, erp_table->base_index, erp->index,
			     erp->key.mask);
	mlxsw_reg_perpt_erp_vector_pack(perpt_pl, erp_table->erp_index_bitmap,
					MLXSW_SP_ACL_ERP_MAX_PER_REGION);
	mlxsw_reg_perpt_erp_vector_set(perpt_pl, erp->index, true);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(perpt), perpt_pl);
}

static void mlxsw_sp_acl_erp_table_erp_del(struct mlxsw_sp_acl_erp *erp)
{
	char empty_mask[MLXSW_REG_PTCEX_FLEX_KEY_BLOCKS_LEN] = { 0 };
	struct mlxsw_sp_acl_erp_table *erp_table = erp->erp_table;
	struct mlxsw_sp *mlxsw_sp = erp_table->erp_core->mlxsw_sp;
	enum mlxsw_reg_perpt_key_size key_size;
	char perpt_pl[MLXSW_REG_PERPT_LEN];
	u8 erpt_bank, erpt_index;

	mlxsw_sp_acl_erp_table_locate(erp_table, erp, &erpt_bank, &erpt_index);
	key_size = (enum mlxsw_reg_perpt_key_size) erp_table->aregion->type;
	mlxsw_reg_perpt_pack(perpt_pl, erpt_bank, erpt_index, key_size, erp->id,
			     0, erp_table->base_index, erp->index, empty_mask);
	mlxsw_reg_perpt_erp_vector_pack(perpt_pl, erp_table->erp_index_bitmap,
					MLXSW_SP_ACL_ERP_MAX_PER_REGION);
	mlxsw_reg_perpt_erp_vector_set(perpt_pl, erp->index, false);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(perpt), perpt_pl);
}

static int
mlxsw_sp_acl_erp_table_enable(struct mlxsw_sp_acl_erp_table *erp_table,
			      bool ctcam_le)
{
	struct mlxsw_sp_acl_tcam_region *region = erp_table->aregion->region;
	struct mlxsw_sp *mlxsw_sp = erp_table->erp_core->mlxsw_sp;
	char pererp_pl[MLXSW_REG_PERERP_LEN];

	mlxsw_reg_pererp_pack(pererp_pl, region->id, ctcam_le, true, 0,
			      erp_table->base_index, 0);
	mlxsw_reg_pererp_erp_vector_pack(pererp_pl, erp_table->erp_index_bitmap,
					 MLXSW_SP_ACL_ERP_MAX_PER_REGION);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pererp), pererp_pl);
}

static void
mlxsw_sp_acl_erp_table_disable(struct mlxsw_sp_acl_erp_table *erp_table)
{
	struct mlxsw_sp_acl_tcam_region *region = erp_table->aregion->region;
	struct mlxsw_sp *mlxsw_sp = erp_table->erp_core->mlxsw_sp;
	char pererp_pl[MLXSW_REG_PERERP_LEN];
	struct mlxsw_sp_acl_erp *master_rp;

	master_rp = mlxsw_sp_acl_erp_table_master_rp(erp_table);
	/* It is possible we do not have a master RP when we disable the
	 * table when there are no rules in the A-TCAM and the last C-TCAM
	 * rule is deleted
	 */
	mlxsw_reg_pererp_pack(pererp_pl, region->id, false, false, 0, 0,
			      master_rp ? master_rp->id : 0);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pererp), pererp_pl);
}

static int
mlxsw_sp_acl_erp_table_relocate(struct mlxsw_sp_acl_erp_table *erp_table)
{
	struct mlxsw_sp_acl_erp *erp;
	int err;

	list_for_each_entry(erp, &erp_table->atcam_erps_list, list) {
		err = mlxsw_sp_acl_erp_table_erp_add(erp_table, erp);
		if (err)
			goto err_table_erp_add;
	}

	return 0;

err_table_erp_add:
	list_for_each_entry_continue_reverse(erp, &erp_table->atcam_erps_list,
					     list)
		mlxsw_sp_acl_erp_table_erp_del(erp);
	return err;
}

static int
mlxsw_sp_acl_erp_table_expand(struct mlxsw_sp_acl_erp_table *erp_table)
{
	unsigned int num_erps, old_num_erps = erp_table->num_max_atcam_erps;
	struct mlxsw_sp_acl_erp_core *erp_core = erp_table->erp_core;
	unsigned long old_base_index = erp_table->base_index;
	bool ctcam_le = erp_table->num_ctcam_erps > 0;
	int err;

	if (erp_table->num_atcam_erps < erp_table->num_max_atcam_erps)
		return 0;

	if (erp_table->num_max_atcam_erps == MLXSW_SP_ACL_ERP_MAX_PER_REGION)
		return -ENOBUFS;

	num_erps = old_num_erps + erp_core->num_erp_banks;
	err = mlxsw_sp_acl_erp_table_alloc(erp_core, num_erps,
					   erp_table->aregion->type,
					   &erp_table->base_index);
	if (err)
		return err;
	erp_table->num_max_atcam_erps = num_erps;

	err = mlxsw_sp_acl_erp_table_relocate(erp_table);
	if (err)
		goto err_table_relocate;

	err = mlxsw_sp_acl_erp_table_enable(erp_table, ctcam_le);
	if (err)
		goto err_table_enable;

	mlxsw_sp_acl_erp_table_free(erp_core, old_num_erps,
				    erp_table->aregion->type, old_base_index);

	return 0;

err_table_enable:
err_table_relocate:
	erp_table->num_max_atcam_erps = old_num_erps;
	mlxsw_sp_acl_erp_table_free(erp_core, num_erps,
				    erp_table->aregion->type,
				    erp_table->base_index);
	erp_table->base_index = old_base_index;
	return err;
}

static int
mlxsw_sp_acl_erp_region_table_trans(struct mlxsw_sp_acl_erp_table *erp_table)
{
	struct mlxsw_sp_acl_erp_core *erp_core = erp_table->erp_core;
	struct mlxsw_sp_acl_erp *master_rp;
	int err;

	/* Initially, allocate a single eRP row. Expand later as needed */
	err = mlxsw_sp_acl_erp_table_alloc(erp_core, erp_core->num_erp_banks,
					   erp_table->aregion->type,
					   &erp_table->base_index);
	if (err)
		return err;
	erp_table->num_max_atcam_erps = erp_core->num_erp_banks;

	/* Transition the sole RP currently configured (the master RP)
	 * to the eRP table
	 */
	master_rp = mlxsw_sp_acl_erp_table_master_rp(erp_table);
	if (!master_rp) {
		err = -EINVAL;
		goto err_table_master_rp;
	}

	/* Maintain the same eRP bank for the master RP, so that we
	 * wouldn't need to update the bloom filter
	 */
	master_rp->index = master_rp->index % erp_core->num_erp_banks;
	__set_bit(master_rp->index, erp_table->erp_index_bitmap);

	err = mlxsw_sp_acl_erp_table_erp_add(erp_table, master_rp);
	if (err)
		goto err_table_master_rp_add;

	err = mlxsw_sp_acl_erp_table_enable(erp_table, false);
	if (err)
		goto err_table_enable;

	return 0;

err_table_enable:
	mlxsw_sp_acl_erp_table_erp_del(master_rp);
err_table_master_rp_add:
	__clear_bit(master_rp->index, erp_table->erp_index_bitmap);
err_table_master_rp:
	mlxsw_sp_acl_erp_table_free(erp_core, erp_table->num_max_atcam_erps,
				    erp_table->aregion->type,
				    erp_table->base_index);
	return err;
}

static void
mlxsw_sp_acl_erp_region_master_mask_trans(struct mlxsw_sp_acl_erp_table *erp_table)
{
	struct mlxsw_sp_acl_erp_core *erp_core = erp_table->erp_core;
	struct mlxsw_sp_acl_erp *master_rp;

	mlxsw_sp_acl_erp_table_disable(erp_table);
	master_rp = mlxsw_sp_acl_erp_table_master_rp(erp_table);
	if (!master_rp)
		return;
	mlxsw_sp_acl_erp_table_erp_del(master_rp);
	__clear_bit(master_rp->index, erp_table->erp_index_bitmap);
	mlxsw_sp_acl_erp_table_free(erp_core, erp_table->num_max_atcam_erps,
				    erp_table->aregion->type,
				    erp_table->base_index);
}

static int
mlxsw_sp_acl_erp_region_erp_add(struct mlxsw_sp_acl_erp_table *erp_table,
				struct mlxsw_sp_acl_erp *erp)
{
	struct mlxsw_sp_acl_tcam_region *region = erp_table->aregion->region;
	struct mlxsw_sp *mlxsw_sp = erp_table->erp_core->mlxsw_sp;
	bool ctcam_le = erp_table->num_ctcam_erps > 0;
	char pererp_pl[MLXSW_REG_PERERP_LEN];

	mlxsw_reg_pererp_pack(pererp_pl, region->id, ctcam_le, true, 0,
			      erp_table->base_index, 0);
	mlxsw_reg_pererp_erp_vector_pack(pererp_pl, erp_table->erp_index_bitmap,
					 MLXSW_SP_ACL_ERP_MAX_PER_REGION);
	mlxsw_reg_pererp_erpt_vector_set(pererp_pl, erp->index, true);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pererp), pererp_pl);
}

static void mlxsw_sp_acl_erp_region_erp_del(struct mlxsw_sp_acl_erp *erp)
{
	struct mlxsw_sp_acl_erp_table *erp_table = erp->erp_table;
	struct mlxsw_sp_acl_tcam_region *region = erp_table->aregion->region;
	struct mlxsw_sp *mlxsw_sp = erp_table->erp_core->mlxsw_sp;
	bool ctcam_le = erp_table->num_ctcam_erps > 0;
	char pererp_pl[MLXSW_REG_PERERP_LEN];

	mlxsw_reg_pererp_pack(pererp_pl, region->id, ctcam_le, true, 0,
			      erp_table->base_index, 0);
	mlxsw_reg_pererp_erp_vector_pack(pererp_pl, erp_table->erp_index_bitmap,
					 MLXSW_SP_ACL_ERP_MAX_PER_REGION);
	mlxsw_reg_pererp_erpt_vector_set(pererp_pl, erp->index, false);

	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pererp), pererp_pl);
}

static int
mlxsw_sp_acl_erp_region_ctcam_enable(struct mlxsw_sp_acl_erp_table *erp_table)
{
	/* No need to re-enable lookup in the C-TCAM */
	if (erp_table->num_ctcam_erps > 1)
		return 0;

	return mlxsw_sp_acl_erp_table_enable(erp_table, true);
}

static void
mlxsw_sp_acl_erp_region_ctcam_disable(struct mlxsw_sp_acl_erp_table *erp_table)
{
	/* Only disable C-TCAM lookup when last C-TCAM eRP is deleted */
	if (erp_table->num_ctcam_erps > 1)
		return;

	mlxsw_sp_acl_erp_table_enable(erp_table, false);
}

static void
mlxsw_sp_acl_erp_ctcam_table_ops_set(struct mlxsw_sp_acl_erp_table *erp_table)
{
	switch (erp_table->num_atcam_erps) {
	case 2:
		/* Keep using the eRP table, but correctly set the
		 * operations pointer so that when an A-TCAM eRP is
		 * deleted we will transition to use the master mask
		 */
		erp_table->ops = &erp_two_masks_ops;
		break;
	case 1:
		/* We only kept the eRP table because we had C-TCAM
		 * eRPs in use. Now that the last C-TCAM eRP is gone we
		 * can stop using the table and transition to use the
		 * master mask
		 */
		mlxsw_sp_acl_erp_region_master_mask_trans(erp_table);
		erp_table->ops = &erp_single_mask_ops;
		break;
	case 0:
		/* There are no more eRPs of any kind used by the region
		 * so free its eRP table and transition to initial state
		 */
		mlxsw_sp_acl_erp_table_disable(erp_table);
		mlxsw_sp_acl_erp_table_free(erp_table->erp_core,
					    erp_table->num_max_atcam_erps,
					    erp_table->aregion->type,
					    erp_table->base_index);
		erp_table->ops = &erp_no_mask_ops;
		break;
	default:
		break;
	}
}

static struct mlxsw_sp_acl_erp *
__mlxsw_sp_acl_erp_ctcam_mask_create(struct mlxsw_sp_acl_erp_table *erp_table,
				     struct mlxsw_sp_acl_erp_key *key)
{
	struct mlxsw_sp_acl_erp *erp;
	int err;

	erp = kzalloc(sizeof(*erp), GFP_KERNEL);
	if (!erp)
		return ERR_PTR(-ENOMEM);

	memcpy(&erp->key, key, sizeof(*key));
	bitmap_from_arr32(erp->mask_bitmap, (u32 *) key->mask,
			  MLXSW_SP_ACL_TCAM_MASK_LEN);
	refcount_set(&erp->refcnt, 1);
	erp_table->num_ctcam_erps++;
	erp->erp_table = erp_table;

	err = mlxsw_sp_acl_erp_master_mask_set(erp_table, erp);
	if (err)
		goto err_master_mask_set;

	err = rhashtable_insert_fast(&erp_table->erp_ht, &erp->ht_node,
				     mlxsw_sp_acl_erp_ht_params);
	if (err)
		goto err_rhashtable_insert;

	err = mlxsw_sp_acl_erp_region_ctcam_enable(erp_table);
	if (err)
		goto err_erp_region_ctcam_enable;

	/* When C-TCAM is used, the eRP table must be used */
	erp_table->ops = &erp_multiple_masks_ops;

	return erp;

err_erp_region_ctcam_enable:
	rhashtable_remove_fast(&erp_table->erp_ht, &erp->ht_node,
			       mlxsw_sp_acl_erp_ht_params);
err_rhashtable_insert:
	mlxsw_sp_acl_erp_master_mask_clear(erp_table, erp);
err_master_mask_set:
	erp_table->num_ctcam_erps--;
	kfree(erp);
	return ERR_PTR(err);
}

static struct mlxsw_sp_acl_erp *
mlxsw_sp_acl_erp_ctcam_mask_create(struct mlxsw_sp_acl_erp_table *erp_table,
				   struct mlxsw_sp_acl_erp_key *key)
{
	struct mlxsw_sp_acl_erp *erp;
	int err;

	/* There is a special situation where we need to spill rules
	 * into the C-TCAM, yet the region is still using a master
	 * mask and thus not performing a lookup in the C-TCAM. This
	 * can happen when two rules that only differ in priority - and
	 * thus sharing the same key - are programmed. In this case
	 * we transition the region to use an eRP table
	 */
	err = mlxsw_sp_acl_erp_region_table_trans(erp_table);
	if (err)
		return ERR_PTR(err);

	erp = __mlxsw_sp_acl_erp_ctcam_mask_create(erp_table, key);
	if (IS_ERR(erp)) {
		err = PTR_ERR(erp);
		goto err_erp_create;
	}

	return erp;

err_erp_create:
	mlxsw_sp_acl_erp_region_master_mask_trans(erp_table);
	return ERR_PTR(err);
}

static void
mlxsw_sp_acl_erp_ctcam_mask_destroy(struct mlxsw_sp_acl_erp *erp)
{
	struct mlxsw_sp_acl_erp_table *erp_table = erp->erp_table;

	mlxsw_sp_acl_erp_region_ctcam_disable(erp_table);
	rhashtable_remove_fast(&erp_table->erp_ht, &erp->ht_node,
			       mlxsw_sp_acl_erp_ht_params);
	mlxsw_sp_acl_erp_master_mask_clear(erp_table, erp);
	erp_table->num_ctcam_erps--;
	kfree(erp);

	/* Once the last C-TCAM eRP was destroyed, the state we
	 * transition to depends on the number of A-TCAM eRPs currently
	 * in use
	 */
	if (erp_table->num_ctcam_erps > 0)
		return;
	mlxsw_sp_acl_erp_ctcam_table_ops_set(erp_table);
}

static struct mlxsw_sp_acl_erp *
mlxsw_sp_acl_erp_mask_create(struct mlxsw_sp_acl_erp_table *erp_table,
			     struct mlxsw_sp_acl_erp_key *key)
{
	struct mlxsw_sp_acl_erp *erp;
	int err;

	if (key->ctcam)
		return __mlxsw_sp_acl_erp_ctcam_mask_create(erp_table, key);

	/* Expand the eRP table for the new eRP, if needed */
	err = mlxsw_sp_acl_erp_table_expand(erp_table);
	if (err)
		return ERR_PTR(err);

	erp = mlxsw_sp_acl_erp_generic_create(erp_table, key);
	if (IS_ERR(erp))
		return erp;

	err = mlxsw_sp_acl_erp_index_get(erp_table, &erp->index);
	if (err)
		goto err_erp_index_get;

	err = mlxsw_sp_acl_erp_table_erp_add(erp_table, erp);
	if (err)
		goto err_table_erp_add;

	err = mlxsw_sp_acl_erp_region_erp_add(erp_table, erp);
	if (err)
		goto err_region_erp_add;

	erp_table->ops = &erp_multiple_masks_ops;

	return erp;

err_region_erp_add:
	mlxsw_sp_acl_erp_table_erp_del(erp);
err_table_erp_add:
	mlxsw_sp_acl_erp_index_put(erp_table, erp->index);
err_erp_index_get:
	mlxsw_sp_acl_erp_generic_destroy(erp);
	return ERR_PTR(err);
}

static void
mlxsw_sp_acl_erp_mask_destroy(struct mlxsw_sp_acl_erp_table *erp_table,
			      struct mlxsw_sp_acl_erp *erp)
{
	if (erp->key.ctcam)
		return mlxsw_sp_acl_erp_ctcam_mask_destroy(erp);

	mlxsw_sp_acl_erp_region_erp_del(erp);
	mlxsw_sp_acl_erp_table_erp_del(erp);
	mlxsw_sp_acl_erp_index_put(erp_table, erp->index);
	mlxsw_sp_acl_erp_generic_destroy(erp);

	if (erp_table->num_atcam_erps == 2 && erp_table->num_ctcam_erps == 0)
		erp_table->ops = &erp_two_masks_ops;
}

static struct mlxsw_sp_acl_erp *
mlxsw_sp_acl_erp_second_mask_create(struct mlxsw_sp_acl_erp_table *erp_table,
				    struct mlxsw_sp_acl_erp_key *key)
{
	struct mlxsw_sp_acl_erp *erp;
	int err;

	if (key->ctcam)
		return mlxsw_sp_acl_erp_ctcam_mask_create(erp_table, key);

	/* Transition to use eRP table instead of master mask */
	err = mlxsw_sp_acl_erp_region_table_trans(erp_table);
	if (err)
		return ERR_PTR(err);

	erp = mlxsw_sp_acl_erp_generic_create(erp_table, key);
	if (IS_ERR(erp)) {
		err = PTR_ERR(erp);
		goto err_erp_create;
	}

	err = mlxsw_sp_acl_erp_index_get(erp_table, &erp->index);
	if (err)
		goto err_erp_index_get;

	err = mlxsw_sp_acl_erp_table_erp_add(erp_table, erp);
	if (err)
		goto err_table_erp_add;

	err = mlxsw_sp_acl_erp_region_erp_add(erp_table, erp);
	if (err)
		goto err_region_erp_add;

	erp_table->ops = &erp_two_masks_ops;

	return erp;

err_region_erp_add:
	mlxsw_sp_acl_erp_table_erp_del(erp);
err_table_erp_add:
	mlxsw_sp_acl_erp_index_put(erp_table, erp->index);
err_erp_index_get:
	mlxsw_sp_acl_erp_generic_destroy(erp);
err_erp_create:
	mlxsw_sp_acl_erp_region_master_mask_trans(erp_table);
	return ERR_PTR(err);
}

static void
mlxsw_sp_acl_erp_second_mask_destroy(struct mlxsw_sp_acl_erp_table *erp_table,
				     struct mlxsw_sp_acl_erp *erp)
{
	if (erp->key.ctcam)
		return mlxsw_sp_acl_erp_ctcam_mask_destroy(erp);

	mlxsw_sp_acl_erp_region_erp_del(erp);
	mlxsw_sp_acl_erp_table_erp_del(erp);
	mlxsw_sp_acl_erp_index_put(erp_table, erp->index);
	mlxsw_sp_acl_erp_generic_destroy(erp);
	/* Transition to use master mask instead of eRP table */
	mlxsw_sp_acl_erp_region_master_mask_trans(erp_table);

	erp_table->ops = &erp_single_mask_ops;
}

static struct mlxsw_sp_acl_erp *
mlxsw_sp_acl_erp_first_mask_create(struct mlxsw_sp_acl_erp_table *erp_table,
				   struct mlxsw_sp_acl_erp_key *key)
{
	struct mlxsw_sp_acl_erp *erp;

	if (key->ctcam)
		return ERR_PTR(-EINVAL);

	erp = mlxsw_sp_acl_erp_generic_create(erp_table, key);
	if (IS_ERR(erp))
		return erp;

	erp_table->ops = &erp_single_mask_ops;

	return erp;
}

static void
mlxsw_sp_acl_erp_first_mask_destroy(struct mlxsw_sp_acl_erp_table *erp_table,
				    struct mlxsw_sp_acl_erp *erp)
{
	mlxsw_sp_acl_erp_generic_destroy(erp);
	erp_table->ops = &erp_no_mask_ops;
}

static void
mlxsw_sp_acl_erp_no_mask_destroy(struct mlxsw_sp_acl_erp_table *erp_table,
				 struct mlxsw_sp_acl_erp *erp)
{
	WARN_ON(1);
}

struct mlxsw_sp_acl_erp *
mlxsw_sp_acl_erp_get(struct mlxsw_sp_acl_atcam_region *aregion,
		     const char *mask, bool ctcam)
{
	struct mlxsw_sp_acl_erp_table *erp_table = aregion->erp_table;
	struct mlxsw_sp_acl_erp_key key;
	struct mlxsw_sp_acl_erp *erp;

	/* eRPs are allocated from a shared resource, but currently all
	 * allocations are done under RTNL.
	 */
	ASSERT_RTNL();

	memcpy(key.mask, mask, MLXSW_REG_PTCEX_FLEX_KEY_BLOCKS_LEN);
	key.ctcam = ctcam;
	erp = rhashtable_lookup_fast(&erp_table->erp_ht, &key,
				     mlxsw_sp_acl_erp_ht_params);
	if (erp) {
		refcount_inc(&erp->refcnt);
		return erp;
	}

	return erp_table->ops->erp_create(erp_table, &key);
}

void mlxsw_sp_acl_erp_put(struct mlxsw_sp_acl_atcam_region *aregion,
			  struct mlxsw_sp_acl_erp *erp)
{
	struct mlxsw_sp_acl_erp_table *erp_table = aregion->erp_table;

	ASSERT_RTNL();

	if (!refcount_dec_and_test(&erp->refcnt))
		return;

	erp_table->ops->erp_destroy(erp_table, erp);
}

static struct mlxsw_sp_acl_erp_table *
mlxsw_sp_acl_erp_table_create(struct mlxsw_sp_acl_atcam_region *aregion)
{
	struct mlxsw_sp_acl_erp_table *erp_table;
	int err;

	erp_table = kzalloc(sizeof(*erp_table), GFP_KERNEL);
	if (!erp_table)
		return ERR_PTR(-ENOMEM);

	err = rhashtable_init(&erp_table->erp_ht, &mlxsw_sp_acl_erp_ht_params);
	if (err)
		goto err_rhashtable_init;

	erp_table->erp_core = aregion->atcam->erp_core;
	erp_table->ops = &erp_no_mask_ops;
	INIT_LIST_HEAD(&erp_table->atcam_erps_list);
	erp_table->aregion = aregion;

	return erp_table;

err_rhashtable_init:
	kfree(erp_table);
	return ERR_PTR(err);
}

static void
mlxsw_sp_acl_erp_table_destroy(struct mlxsw_sp_acl_erp_table *erp_table)
{
	WARN_ON(!list_empty(&erp_table->atcam_erps_list));
	rhashtable_destroy(&erp_table->erp_ht);
	kfree(erp_table);
}

static int
mlxsw_sp_acl_erp_master_mask_init(struct mlxsw_sp_acl_atcam_region *aregion)
{
	struct mlxsw_sp *mlxsw_sp = aregion->region->mlxsw_sp;
	char percr_pl[MLXSW_REG_PERCR_LEN];

	mlxsw_reg_percr_pack(percr_pl, aregion->region->id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(percr), percr_pl);
}

static int
mlxsw_sp_acl_erp_region_param_init(struct mlxsw_sp_acl_atcam_region *aregion)
{
	struct mlxsw_sp *mlxsw_sp = aregion->region->mlxsw_sp;
	char pererp_pl[MLXSW_REG_PERERP_LEN];

	mlxsw_reg_pererp_pack(pererp_pl, aregion->region->id, false, false, 0,
			      0, 0);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pererp), pererp_pl);
}

int mlxsw_sp_acl_erp_region_init(struct mlxsw_sp_acl_atcam_region *aregion)
{
	struct mlxsw_sp_acl_erp_table *erp_table;
	int err;

	erp_table = mlxsw_sp_acl_erp_table_create(aregion);
	if (IS_ERR(erp_table))
		return PTR_ERR(erp_table);
	aregion->erp_table = erp_table;

	/* Initialize the region's master mask to all zeroes */
	err = mlxsw_sp_acl_erp_master_mask_init(aregion);
	if (err)
		goto err_erp_master_mask_init;

	/* Initialize the region to not use the eRP table */
	err = mlxsw_sp_acl_erp_region_param_init(aregion);
	if (err)
		goto err_erp_region_param_init;

	return 0;

err_erp_region_param_init:
err_erp_master_mask_init:
	mlxsw_sp_acl_erp_table_destroy(erp_table);
	return err;
}

void mlxsw_sp_acl_erp_region_fini(struct mlxsw_sp_acl_atcam_region *aregion)
{
	mlxsw_sp_acl_erp_table_destroy(aregion->erp_table);
}

static int
mlxsw_sp_acl_erp_tables_sizes_query(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_acl_erp_core *erp_core)
{
	unsigned int size;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, ACL_ERPT_ENTRIES_2KB) ||
	    !MLXSW_CORE_RES_VALID(mlxsw_sp->core, ACL_ERPT_ENTRIES_4KB) ||
	    !MLXSW_CORE_RES_VALID(mlxsw_sp->core, ACL_ERPT_ENTRIES_8KB) ||
	    !MLXSW_CORE_RES_VALID(mlxsw_sp->core, ACL_ERPT_ENTRIES_12KB))
		return -EIO;

	size = MLXSW_CORE_RES_GET(mlxsw_sp->core, ACL_ERPT_ENTRIES_2KB);
	erp_core->erpt_entries_size[MLXSW_SP_ACL_ATCAM_REGION_TYPE_2KB] = size;

	size = MLXSW_CORE_RES_GET(mlxsw_sp->core, ACL_ERPT_ENTRIES_4KB);
	erp_core->erpt_entries_size[MLXSW_SP_ACL_ATCAM_REGION_TYPE_4KB] = size;

	size = MLXSW_CORE_RES_GET(mlxsw_sp->core, ACL_ERPT_ENTRIES_8KB);
	erp_core->erpt_entries_size[MLXSW_SP_ACL_ATCAM_REGION_TYPE_8KB] = size;

	size = MLXSW_CORE_RES_GET(mlxsw_sp->core, ACL_ERPT_ENTRIES_12KB);
	erp_core->erpt_entries_size[MLXSW_SP_ACL_ATCAM_REGION_TYPE_12KB] = size;

	return 0;
}

static int mlxsw_sp_acl_erp_tables_init(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_acl_erp_core *erp_core)
{
	unsigned int erpt_bank_size;
	int err;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, ACL_MAX_ERPT_BANK_SIZE) ||
	    !MLXSW_CORE_RES_VALID(mlxsw_sp->core, ACL_MAX_ERPT_BANKS))
		return -EIO;
	erpt_bank_size = MLXSW_CORE_RES_GET(mlxsw_sp->core,
					    ACL_MAX_ERPT_BANK_SIZE);
	erp_core->num_erp_banks = MLXSW_CORE_RES_GET(mlxsw_sp->core,
						     ACL_MAX_ERPT_BANKS);

	erp_core->erp_tables = gen_pool_create(0, -1);
	if (!erp_core->erp_tables)
		return -ENOMEM;
	gen_pool_set_algo(erp_core->erp_tables, gen_pool_best_fit, NULL);

	err = gen_pool_add(erp_core->erp_tables,
			   MLXSW_SP_ACL_ERP_GENALLOC_OFFSET, erpt_bank_size,
			   -1);
	if (err)
		goto err_gen_pool_add;

	/* Different regions require masks of different sizes */
	err = mlxsw_sp_acl_erp_tables_sizes_query(mlxsw_sp, erp_core);
	if (err)
		goto err_erp_tables_sizes_query;

	return 0;

err_erp_tables_sizes_query:
err_gen_pool_add:
	gen_pool_destroy(erp_core->erp_tables);
	return err;
}

static void mlxsw_sp_acl_erp_tables_fini(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_acl_erp_core *erp_core)
{
	gen_pool_destroy(erp_core->erp_tables);
}

int mlxsw_sp_acl_erps_init(struct mlxsw_sp *mlxsw_sp,
			   struct mlxsw_sp_acl_atcam *atcam)
{
	struct mlxsw_sp_acl_erp_core *erp_core;
	int err;

	erp_core = kzalloc(sizeof(*erp_core), GFP_KERNEL);
	if (!erp_core)
		return -ENOMEM;
	erp_core->mlxsw_sp = mlxsw_sp;
	atcam->erp_core = erp_core;

	err = mlxsw_sp_acl_erp_tables_init(mlxsw_sp, erp_core);
	if (err)
		goto err_erp_tables_init;

	return 0;

err_erp_tables_init:
	kfree(erp_core);
	return err;
}

void mlxsw_sp_acl_erps_fini(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_acl_atcam *atcam)
{
	mlxsw_sp_acl_erp_tables_fini(mlxsw_sp, atcam->erp_core);
	kfree(atcam->erp_core);
}
