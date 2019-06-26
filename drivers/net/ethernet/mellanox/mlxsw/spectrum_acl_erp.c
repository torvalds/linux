// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/bitmap.h>
#include <linux/errno.h>
#include <linux/genalloc.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/objagg.h>
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
	struct mlxsw_sp_acl_bf *bf;
	unsigned int num_erp_banks;
};

struct mlxsw_sp_acl_erp_key {
	char mask[MLXSW_REG_PTCEX_FLEX_KEY_BLOCKS_LEN];
#define __MASK_LEN 0x38
#define __MASK_IDX(i) (__MASK_LEN - (i) - 1)
	bool ctcam;
};

struct mlxsw_sp_acl_erp {
	struct mlxsw_sp_acl_erp_key key;
	u8 id;
	u8 index;
	DECLARE_BITMAP(mask_bitmap, MLXSW_SP_ACL_TCAM_MASK_LEN);
	struct list_head list;
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
	struct mlxsw_sp_acl_erp_core *erp_core;
	struct mlxsw_sp_acl_atcam_region *aregion;
	const struct mlxsw_sp_acl_erp_table_ops *ops;
	unsigned long base_index;
	unsigned int num_atcam_erps;
	unsigned int num_max_atcam_erps;
	unsigned int num_ctcam_erps;
	unsigned int num_deltas;
	struct objagg *objagg;
	struct mutex objagg_lock; /* guards objagg manipulation */
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

static bool
mlxsw_sp_acl_erp_table_is_used(const struct mlxsw_sp_acl_erp_table *erp_table)
{
	return erp_table->ops != &erp_single_mask_ops &&
	       erp_table->ops != &erp_no_mask_ops;
}

static unsigned int
mlxsw_sp_acl_erp_bank_get(const struct mlxsw_sp_acl_erp *erp)
{
	return erp->index % erp->erp_table->erp_core->num_erp_banks;
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
				 struct mlxsw_sp_acl_erp_key *key)
{
	DECLARE_BITMAP(mask_bitmap, MLXSW_SP_ACL_TCAM_MASK_LEN);
	unsigned long bit;
	int err;

	bitmap_from_arr32(mask_bitmap, (u32 *) key->mask,
			  MLXSW_SP_ACL_TCAM_MASK_LEN);
	for_each_set_bit(bit, mask_bitmap, MLXSW_SP_ACL_TCAM_MASK_LEN)
		mlxsw_sp_acl_erp_master_mask_bit_set(bit,
						     &erp_table->master_mask);

	err = mlxsw_sp_acl_erp_master_mask_update(erp_table);
	if (err)
		goto err_master_mask_update;

	return 0;

err_master_mask_update:
	for_each_set_bit(bit, mask_bitmap, MLXSW_SP_ACL_TCAM_MASK_LEN)
		mlxsw_sp_acl_erp_master_mask_bit_clear(bit,
						       &erp_table->master_mask);
	return err;
}

static int
mlxsw_sp_acl_erp_master_mask_clear(struct mlxsw_sp_acl_erp_table *erp_table,
				   struct mlxsw_sp_acl_erp_key *key)
{
	DECLARE_BITMAP(mask_bitmap, MLXSW_SP_ACL_TCAM_MASK_LEN);
	unsigned long bit;
	int err;

	bitmap_from_arr32(mask_bitmap, (u32 *) key->mask,
			  MLXSW_SP_ACL_TCAM_MASK_LEN);
	for_each_set_bit(bit, mask_bitmap, MLXSW_SP_ACL_TCAM_MASK_LEN)
		mlxsw_sp_acl_erp_master_mask_bit_clear(bit,
						       &erp_table->master_mask);

	err = mlxsw_sp_acl_erp_master_mask_update(erp_table);
	if (err)
		goto err_master_mask_update;

	return 0;

err_master_mask_update:
	for_each_set_bit(bit, mask_bitmap, MLXSW_SP_ACL_TCAM_MASK_LEN)
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
	list_add(&erp->list, &erp_table->atcam_erps_list);
	erp_table->num_atcam_erps++;
	erp->erp_table = erp_table;

	err = mlxsw_sp_acl_erp_master_mask_set(erp_table, &erp->key);
	if (err)
		goto err_master_mask_set;

	return erp;

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

	mlxsw_sp_acl_erp_master_mask_clear(erp_table, &erp->key);
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
mlxsw_acl_erp_table_bf_add(struct mlxsw_sp_acl_erp_table *erp_table,
			   struct mlxsw_sp_acl_erp *erp)
{
	struct mlxsw_sp_acl_atcam_region *aregion = erp_table->aregion;
	unsigned int erp_bank = mlxsw_sp_acl_erp_bank_get(erp);
	struct mlxsw_sp_acl_atcam_entry *aentry;
	int err;

	list_for_each_entry(aentry, &aregion->entries_list, list) {
		err = mlxsw_sp_acl_bf_entry_add(aregion->region->mlxsw_sp,
						erp_table->erp_core->bf,
						aregion, erp_bank, aentry);
		if (err)
			goto bf_entry_add_err;
	}

	return 0;

bf_entry_add_err:
	list_for_each_entry_continue_reverse(aentry, &aregion->entries_list,
					     list)
		mlxsw_sp_acl_bf_entry_del(aregion->region->mlxsw_sp,
					  erp_table->erp_core->bf,
					  aregion, erp_bank, aentry);
	return err;
}

static void
mlxsw_acl_erp_table_bf_del(struct mlxsw_sp_acl_erp_table *erp_table,
			   struct mlxsw_sp_acl_erp *erp)
{
	struct mlxsw_sp_acl_atcam_region *aregion = erp_table->aregion;
	unsigned int erp_bank = mlxsw_sp_acl_erp_bank_get(erp);
	struct mlxsw_sp_acl_atcam_entry *aentry;

	list_for_each_entry_reverse(aentry, &aregion->entries_list, list)
		mlxsw_sp_acl_bf_entry_del(aregion->region->mlxsw_sp,
					  erp_table->erp_core->bf,
					  aregion, erp_bank, aentry);
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

	/* Make sure the master RP is using a valid index, as
	 * only a single eRP row is currently allocated.
	 */
	master_rp->index = 0;
	__set_bit(master_rp->index, erp_table->erp_index_bitmap);

	err = mlxsw_sp_acl_erp_table_erp_add(erp_table, master_rp);
	if (err)
		goto err_table_master_rp_add;

	/* Update Bloom filter before enabling eRP table, as rules
	 * on the master RP were not set to Bloom filter up to this
	 * point.
	 */
	err = mlxsw_acl_erp_table_bf_add(erp_table, master_rp);
	if (err)
		goto err_table_bf_add;

	err = mlxsw_sp_acl_erp_table_enable(erp_table, false);
	if (err)
		goto err_table_enable;

	return 0;

err_table_enable:
	mlxsw_acl_erp_table_bf_del(erp_table, master_rp);
err_table_bf_add:
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
	mlxsw_acl_erp_table_bf_del(erp_table, master_rp);
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

static int
__mlxsw_sp_acl_erp_table_other_inc(struct mlxsw_sp_acl_erp_table *erp_table,
				   unsigned int *inc_num)
{
	int err;

	/* If there are C-TCAM eRP or deltas in use we need to transition
	 * the region to use eRP table, if it is not already done
	 */
	if (!mlxsw_sp_acl_erp_table_is_used(erp_table)) {
		err = mlxsw_sp_acl_erp_region_table_trans(erp_table);
		if (err)
			return err;
	}

	/* When C-TCAM or deltas are used, the eRP table must be used */
	if (erp_table->ops != &erp_multiple_masks_ops)
		erp_table->ops = &erp_multiple_masks_ops;

	(*inc_num)++;

	return 0;
}

static int mlxsw_sp_acl_erp_ctcam_inc(struct mlxsw_sp_acl_erp_table *erp_table)
{
	return __mlxsw_sp_acl_erp_table_other_inc(erp_table,
						  &erp_table->num_ctcam_erps);
}

static int mlxsw_sp_acl_erp_delta_inc(struct mlxsw_sp_acl_erp_table *erp_table)
{
	return __mlxsw_sp_acl_erp_table_other_inc(erp_table,
						  &erp_table->num_deltas);
}

static void
__mlxsw_sp_acl_erp_table_other_dec(struct mlxsw_sp_acl_erp_table *erp_table,
				   unsigned int *dec_num)
{
	(*dec_num)--;

	/* If there are no C-TCAM eRP or deltas in use, the state we
	 * transition to depends on the number of A-TCAM eRPs currently
	 * in use.
	 */
	if (erp_table->num_ctcam_erps > 0 || erp_table->num_deltas > 0)
		return;

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

static void mlxsw_sp_acl_erp_ctcam_dec(struct mlxsw_sp_acl_erp_table *erp_table)
{
	__mlxsw_sp_acl_erp_table_other_dec(erp_table,
					   &erp_table->num_ctcam_erps);
}

static void mlxsw_sp_acl_erp_delta_dec(struct mlxsw_sp_acl_erp_table *erp_table)
{
	__mlxsw_sp_acl_erp_table_other_dec(erp_table,
					   &erp_table->num_deltas);
}

static struct mlxsw_sp_acl_erp *
mlxsw_sp_acl_erp_ctcam_mask_create(struct mlxsw_sp_acl_erp_table *erp_table,
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

	err = mlxsw_sp_acl_erp_ctcam_inc(erp_table);
	if (err)
		goto err_erp_ctcam_inc;

	erp->erp_table = erp_table;

	err = mlxsw_sp_acl_erp_master_mask_set(erp_table, &erp->key);
	if (err)
		goto err_master_mask_set;

	err = mlxsw_sp_acl_erp_region_ctcam_enable(erp_table);
	if (err)
		goto err_erp_region_ctcam_enable;

	return erp;

err_erp_region_ctcam_enable:
	mlxsw_sp_acl_erp_master_mask_clear(erp_table, &erp->key);
err_master_mask_set:
	mlxsw_sp_acl_erp_ctcam_dec(erp_table);
err_erp_ctcam_inc:
	kfree(erp);
	return ERR_PTR(err);
}

static void
mlxsw_sp_acl_erp_ctcam_mask_destroy(struct mlxsw_sp_acl_erp *erp)
{
	struct mlxsw_sp_acl_erp_table *erp_table = erp->erp_table;

	mlxsw_sp_acl_erp_region_ctcam_disable(erp_table);
	mlxsw_sp_acl_erp_master_mask_clear(erp_table, &erp->key);
	mlxsw_sp_acl_erp_ctcam_dec(erp_table);
	kfree(erp);
}

static struct mlxsw_sp_acl_erp *
mlxsw_sp_acl_erp_mask_create(struct mlxsw_sp_acl_erp_table *erp_table,
			     struct mlxsw_sp_acl_erp_key *key)
{
	struct mlxsw_sp_acl_erp *erp;
	int err;

	if (key->ctcam)
		return mlxsw_sp_acl_erp_ctcam_mask_create(erp_table, key);

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

	if (erp_table->num_atcam_erps == 2 && erp_table->num_ctcam_erps == 0 &&
	    erp_table->num_deltas == 0)
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

struct mlxsw_sp_acl_erp_mask *
mlxsw_sp_acl_erp_mask_get(struct mlxsw_sp_acl_atcam_region *aregion,
			  const char *mask, bool ctcam)
{
	struct mlxsw_sp_acl_erp_table *erp_table = aregion->erp_table;
	struct mlxsw_sp_acl_erp_key key;
	struct objagg_obj *objagg_obj;

	memcpy(key.mask, mask, MLXSW_REG_PTCEX_FLEX_KEY_BLOCKS_LEN);
	key.ctcam = ctcam;
	mutex_lock(&erp_table->objagg_lock);
	objagg_obj = objagg_obj_get(erp_table->objagg, &key);
	mutex_unlock(&erp_table->objagg_lock);
	if (IS_ERR(objagg_obj))
		return ERR_CAST(objagg_obj);
	return (struct mlxsw_sp_acl_erp_mask *) objagg_obj;
}

void mlxsw_sp_acl_erp_mask_put(struct mlxsw_sp_acl_atcam_region *aregion,
			       struct mlxsw_sp_acl_erp_mask *erp_mask)
{
	struct objagg_obj *objagg_obj = (struct objagg_obj *) erp_mask;
	struct mlxsw_sp_acl_erp_table *erp_table = aregion->erp_table;

	mutex_lock(&erp_table->objagg_lock);
	objagg_obj_put(erp_table->objagg, objagg_obj);
	mutex_unlock(&erp_table->objagg_lock);
}

int mlxsw_sp_acl_erp_bf_insert(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_atcam_region *aregion,
			       struct mlxsw_sp_acl_erp_mask *erp_mask,
			       struct mlxsw_sp_acl_atcam_entry *aentry)
{
	struct objagg_obj *objagg_obj = (struct objagg_obj *) erp_mask;
	const struct mlxsw_sp_acl_erp *erp = objagg_obj_root_priv(objagg_obj);
	unsigned int erp_bank;

	if (!mlxsw_sp_acl_erp_table_is_used(erp->erp_table))
		return 0;

	erp_bank = mlxsw_sp_acl_erp_bank_get(erp);
	return mlxsw_sp_acl_bf_entry_add(mlxsw_sp,
					erp->erp_table->erp_core->bf,
					aregion, erp_bank, aentry);
}

void mlxsw_sp_acl_erp_bf_remove(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_acl_atcam_region *aregion,
				struct mlxsw_sp_acl_erp_mask *erp_mask,
				struct mlxsw_sp_acl_atcam_entry *aentry)
{
	struct objagg_obj *objagg_obj = (struct objagg_obj *) erp_mask;
	const struct mlxsw_sp_acl_erp *erp = objagg_obj_root_priv(objagg_obj);
	unsigned int erp_bank;

	if (!mlxsw_sp_acl_erp_table_is_used(erp->erp_table))
		return;

	erp_bank = mlxsw_sp_acl_erp_bank_get(erp);
	mlxsw_sp_acl_bf_entry_del(mlxsw_sp,
				  erp->erp_table->erp_core->bf,
				  aregion, erp_bank, aentry);
}

bool
mlxsw_sp_acl_erp_mask_is_ctcam(const struct mlxsw_sp_acl_erp_mask *erp_mask)
{
	struct objagg_obj *objagg_obj = (struct objagg_obj *) erp_mask;
	const struct mlxsw_sp_acl_erp_key *key = objagg_obj_raw(objagg_obj);

	return key->ctcam;
}

u8 mlxsw_sp_acl_erp_mask_erp_id(const struct mlxsw_sp_acl_erp_mask *erp_mask)
{
	struct objagg_obj *objagg_obj = (struct objagg_obj *) erp_mask;
	const struct mlxsw_sp_acl_erp *erp = objagg_obj_root_priv(objagg_obj);

	return erp->id;
}

struct mlxsw_sp_acl_erp_delta {
	struct mlxsw_sp_acl_erp_key key;
	u16 start;
	u8 mask;
};

u16 mlxsw_sp_acl_erp_delta_start(const struct mlxsw_sp_acl_erp_delta *delta)
{
	return delta->start;
}

u8 mlxsw_sp_acl_erp_delta_mask(const struct mlxsw_sp_acl_erp_delta *delta)
{
	return delta->mask;
}

u8 mlxsw_sp_acl_erp_delta_value(const struct mlxsw_sp_acl_erp_delta *delta,
				const char *enc_key)
{
	u16 start = delta->start;
	u8 mask = delta->mask;
	u16 tmp;

	if (!mask)
		return 0;

	tmp = (unsigned char) enc_key[__MASK_IDX(start / 8)];
	if (start / 8 + 1 < __MASK_LEN)
		tmp |= (unsigned char) enc_key[__MASK_IDX(start / 8 + 1)] << 8;
	tmp >>= start % 8;
	tmp &= mask;
	return tmp;
}

void mlxsw_sp_acl_erp_delta_clear(const struct mlxsw_sp_acl_erp_delta *delta,
				  const char *enc_key)
{
	u16 start = delta->start;
	u8 mask = delta->mask;
	unsigned char *byte;
	u16 tmp;

	tmp = mask;
	tmp <<= start % 8;
	tmp = ~tmp;

	byte = (unsigned char *) &enc_key[__MASK_IDX(start / 8)];
	*byte &= tmp & 0xff;
	if (start / 8 + 1 < __MASK_LEN) {
		byte = (unsigned char *) &enc_key[__MASK_IDX(start / 8 + 1)];
		*byte &= (tmp >> 8) & 0xff;
	}
}

static const struct mlxsw_sp_acl_erp_delta
mlxsw_sp_acl_erp_delta_default = {};

const struct mlxsw_sp_acl_erp_delta *
mlxsw_sp_acl_erp_delta(const struct mlxsw_sp_acl_erp_mask *erp_mask)
{
	struct objagg_obj *objagg_obj = (struct objagg_obj *) erp_mask;
	const struct mlxsw_sp_acl_erp_delta *delta;

	delta = objagg_obj_delta_priv(objagg_obj);
	if (!delta)
		delta = &mlxsw_sp_acl_erp_delta_default;
	return delta;
}

static int
mlxsw_sp_acl_erp_delta_fill(const struct mlxsw_sp_acl_erp_key *parent_key,
			    const struct mlxsw_sp_acl_erp_key *key,
			    u16 *delta_start, u8 *delta_mask)
{
	int offset = 0;
	int si = -1;
	u16 pmask;
	u16 mask;
	int i;

	/* The difference between 2 masks can be up to 8 consecutive bits. */
	for (i = 0; i < __MASK_LEN; i++) {
		if (parent_key->mask[__MASK_IDX(i)] == key->mask[__MASK_IDX(i)])
			continue;
		if (si == -1)
			si = i;
		else if (si != i - 1)
			return -EINVAL;
	}
	if (si == -1) {
		/* The masks are the same, this cannot happen.
		 * That means the caller is broken.
		 */
		WARN_ON(1);
		*delta_start = 0;
		*delta_mask = 0;
		return 0;
	}
	pmask = (unsigned char) parent_key->mask[__MASK_IDX(si)];
	mask = (unsigned char) key->mask[__MASK_IDX(si)];
	if (si + 1 < __MASK_LEN) {
		pmask |= (unsigned char) parent_key->mask[__MASK_IDX(si + 1)] << 8;
		mask |= (unsigned char) key->mask[__MASK_IDX(si + 1)] << 8;
	}

	if ((pmask ^ mask) & pmask)
		return -EINVAL;
	mask &= ~pmask;
	while (!(mask & (1 << offset)))
		offset++;
	while (!(mask & 1))
		mask >>= 1;
	if (mask & 0xff00)
		return -EINVAL;

	*delta_start = si * 8 + offset;
	*delta_mask = mask;

	return 0;
}

static bool mlxsw_sp_acl_erp_delta_check(void *priv, const void *parent_obj,
					 const void *obj)
{
	const struct mlxsw_sp_acl_erp_key *parent_key = parent_obj;
	const struct mlxsw_sp_acl_erp_key *key = obj;
	u16 delta_start;
	u8 delta_mask;
	int err;

	err = mlxsw_sp_acl_erp_delta_fill(parent_key, key,
					  &delta_start, &delta_mask);
	return err ? false : true;
}

static int mlxsw_sp_acl_erp_hints_obj_cmp(const void *obj1, const void *obj2)
{
	const struct mlxsw_sp_acl_erp_key *key1 = obj1;
	const struct mlxsw_sp_acl_erp_key *key2 = obj2;

	/* For hints purposes, two objects are considered equal
	 * in case the masks are the same. Does not matter what
	 * the "ctcam" value is.
	 */
	return memcmp(key1->mask, key2->mask, sizeof(key1->mask));
}

static void *mlxsw_sp_acl_erp_delta_create(void *priv, void *parent_obj,
					   void *obj)
{
	struct mlxsw_sp_acl_erp_key *parent_key = parent_obj;
	struct mlxsw_sp_acl_atcam_region *aregion = priv;
	struct mlxsw_sp_acl_erp_table *erp_table = aregion->erp_table;
	struct mlxsw_sp_acl_erp_key *key = obj;
	struct mlxsw_sp_acl_erp_delta *delta;
	u16 delta_start;
	u8 delta_mask;
	int err;

	if (parent_key->ctcam || key->ctcam)
		return ERR_PTR(-EINVAL);
	err = mlxsw_sp_acl_erp_delta_fill(parent_key, key,
					  &delta_start, &delta_mask);
	if (err)
		return ERR_PTR(-EINVAL);

	delta = kzalloc(sizeof(*delta), GFP_KERNEL);
	if (!delta)
		return ERR_PTR(-ENOMEM);
	delta->start = delta_start;
	delta->mask = delta_mask;

	err = mlxsw_sp_acl_erp_delta_inc(erp_table);
	if (err)
		goto err_erp_delta_inc;

	memcpy(&delta->key, key, sizeof(*key));
	err = mlxsw_sp_acl_erp_master_mask_set(erp_table, &delta->key);
	if (err)
		goto err_master_mask_set;

	return delta;

err_master_mask_set:
	mlxsw_sp_acl_erp_delta_dec(erp_table);
err_erp_delta_inc:
	kfree(delta);
	return ERR_PTR(err);
}

static void mlxsw_sp_acl_erp_delta_destroy(void *priv, void *delta_priv)
{
	struct mlxsw_sp_acl_erp_delta *delta = delta_priv;
	struct mlxsw_sp_acl_atcam_region *aregion = priv;
	struct mlxsw_sp_acl_erp_table *erp_table = aregion->erp_table;

	mlxsw_sp_acl_erp_master_mask_clear(erp_table, &delta->key);
	mlxsw_sp_acl_erp_delta_dec(erp_table);
	kfree(delta);
}

static void *mlxsw_sp_acl_erp_root_create(void *priv, void *obj,
					  unsigned int root_id)
{
	struct mlxsw_sp_acl_atcam_region *aregion = priv;
	struct mlxsw_sp_acl_erp_table *erp_table = aregion->erp_table;
	struct mlxsw_sp_acl_erp_key *key = obj;

	if (!key->ctcam &&
	    root_id != OBJAGG_OBJ_ROOT_ID_INVALID &&
	    root_id >= MLXSW_SP_ACL_ERP_MAX_PER_REGION)
		return ERR_PTR(-ENOBUFS);
	return erp_table->ops->erp_create(erp_table, key);
}

static void mlxsw_sp_acl_erp_root_destroy(void *priv, void *root_priv)
{
	struct mlxsw_sp_acl_atcam_region *aregion = priv;
	struct mlxsw_sp_acl_erp_table *erp_table = aregion->erp_table;

	erp_table->ops->erp_destroy(erp_table, root_priv);
}

static const struct objagg_ops mlxsw_sp_acl_erp_objagg_ops = {
	.obj_size = sizeof(struct mlxsw_sp_acl_erp_key),
	.delta_check = mlxsw_sp_acl_erp_delta_check,
	.hints_obj_cmp = mlxsw_sp_acl_erp_hints_obj_cmp,
	.delta_create = mlxsw_sp_acl_erp_delta_create,
	.delta_destroy = mlxsw_sp_acl_erp_delta_destroy,
	.root_create = mlxsw_sp_acl_erp_root_create,
	.root_destroy = mlxsw_sp_acl_erp_root_destroy,
};

static struct mlxsw_sp_acl_erp_table *
mlxsw_sp_acl_erp_table_create(struct mlxsw_sp_acl_atcam_region *aregion,
			      struct objagg_hints *hints)
{
	struct mlxsw_sp_acl_erp_table *erp_table;
	int err;

	erp_table = kzalloc(sizeof(*erp_table), GFP_KERNEL);
	if (!erp_table)
		return ERR_PTR(-ENOMEM);

	erp_table->objagg = objagg_create(&mlxsw_sp_acl_erp_objagg_ops,
					  hints, aregion);
	if (IS_ERR(erp_table->objagg)) {
		err = PTR_ERR(erp_table->objagg);
		goto err_objagg_create;
	}

	erp_table->erp_core = aregion->atcam->erp_core;
	erp_table->ops = &erp_no_mask_ops;
	INIT_LIST_HEAD(&erp_table->atcam_erps_list);
	erp_table->aregion = aregion;
	mutex_init(&erp_table->objagg_lock);

	return erp_table;

err_objagg_create:
	kfree(erp_table);
	return ERR_PTR(err);
}

static void
mlxsw_sp_acl_erp_table_destroy(struct mlxsw_sp_acl_erp_table *erp_table)
{
	WARN_ON(!list_empty(&erp_table->atcam_erps_list));
	mutex_destroy(&erp_table->objagg_lock);
	objagg_destroy(erp_table->objagg);
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

static int
mlxsw_sp_acl_erp_hints_check(struct mlxsw_sp *mlxsw_sp,
			     struct mlxsw_sp_acl_atcam_region *aregion,
			     struct objagg_hints *hints, bool *p_rehash_needed)
{
	struct mlxsw_sp_acl_erp_table *erp_table = aregion->erp_table;
	const struct objagg_stats *ostats;
	const struct objagg_stats *hstats;
	int err;

	*p_rehash_needed = false;

	mutex_lock(&erp_table->objagg_lock);
	ostats = objagg_stats_get(erp_table->objagg);
	mutex_unlock(&erp_table->objagg_lock);
	if (IS_ERR(ostats)) {
		dev_err_ratelimited(mlxsw_sp->bus_info->dev, "Failed to get ERP stats\n");
		return PTR_ERR(ostats);
	}

	hstats = objagg_hints_stats_get(hints);
	if (IS_ERR(hstats)) {
		dev_err_ratelimited(mlxsw_sp->bus_info->dev, "Failed to get ERP hints stats\n");
		err = PTR_ERR(hstats);
		goto err_hints_stats_get;
	}

	/* Very basic criterion for now. */
	if (hstats->root_count < ostats->root_count)
		*p_rehash_needed = true;

	err = 0;

	objagg_stats_put(hstats);
err_hints_stats_get:
	objagg_stats_put(ostats);
	return err;
}

void *
mlxsw_sp_acl_erp_rehash_hints_get(struct mlxsw_sp_acl_atcam_region *aregion)
{
	struct mlxsw_sp_acl_erp_table *erp_table = aregion->erp_table;
	struct mlxsw_sp *mlxsw_sp = aregion->region->mlxsw_sp;
	struct objagg_hints *hints;
	bool rehash_needed;
	int err;

	mutex_lock(&erp_table->objagg_lock);
	hints = objagg_hints_get(erp_table->objagg,
				 OBJAGG_OPT_ALGO_SIMPLE_GREEDY);
	mutex_unlock(&erp_table->objagg_lock);
	if (IS_ERR(hints)) {
		dev_err_ratelimited(mlxsw_sp->bus_info->dev, "Failed to create ERP hints\n");
		return ERR_CAST(hints);
	}
	err = mlxsw_sp_acl_erp_hints_check(mlxsw_sp, aregion, hints,
					   &rehash_needed);
	if (err)
		goto errout;

	if (!rehash_needed) {
		err = -EAGAIN;
		goto errout;
	}
	return hints;

errout:
	objagg_hints_put(hints);
	return ERR_PTR(err);
}

void mlxsw_sp_acl_erp_rehash_hints_put(void *hints_priv)
{
	struct objagg_hints *hints = hints_priv;

	objagg_hints_put(hints);
}

int mlxsw_sp_acl_erp_region_init(struct mlxsw_sp_acl_atcam_region *aregion,
				 void *hints_priv)
{
	struct mlxsw_sp_acl_erp_table *erp_table;
	struct objagg_hints *hints = hints_priv;
	int err;

	erp_table = mlxsw_sp_acl_erp_table_create(aregion, hints);
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

	erp_core->bf = mlxsw_sp_acl_bf_init(mlxsw_sp, erp_core->num_erp_banks);
	if (IS_ERR(erp_core->bf)) {
		err = PTR_ERR(erp_core->bf);
		goto err_bf_init;
	}

	/* Different regions require masks of different sizes */
	err = mlxsw_sp_acl_erp_tables_sizes_query(mlxsw_sp, erp_core);
	if (err)
		goto err_erp_tables_sizes_query;

	return 0;

err_erp_tables_sizes_query:
	mlxsw_sp_acl_bf_fini(erp_core->bf);
err_bf_init:
err_gen_pool_add:
	gen_pool_destroy(erp_core->erp_tables);
	return err;
}

static void mlxsw_sp_acl_erp_tables_fini(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_acl_erp_core *erp_core)
{
	mlxsw_sp_acl_bf_fini(erp_core->bf);
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
