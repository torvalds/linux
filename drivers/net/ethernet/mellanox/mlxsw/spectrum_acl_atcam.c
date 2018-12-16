// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/refcount.h>
#include <linux/rhashtable.h>

#include "reg.h"
#include "core.h"
#include "spectrum.h"
#include "spectrum_acl_tcam.h"
#include "core_acl_flex_keys.h"

#define MLXSW_SP_ACL_ATCAM_LKEY_ID_BLOCK_CLEAR_START	0
#define MLXSW_SP_ACL_ATCAM_LKEY_ID_BLOCK_CLEAR_END	5

struct mlxsw_sp_acl_atcam_lkey_id_ht_key {
	char enc_key[MLXSW_REG_PTCEX_FLEX_KEY_BLOCKS_LEN]; /* MSB blocks */
	u8 erp_id;
};

struct mlxsw_sp_acl_atcam_lkey_id {
	struct rhash_head ht_node;
	struct mlxsw_sp_acl_atcam_lkey_id_ht_key ht_key;
	refcount_t refcnt;
	u32 id;
};

struct mlxsw_sp_acl_atcam_region_ops {
	int (*init)(struct mlxsw_sp_acl_atcam_region *aregion);
	void (*fini)(struct mlxsw_sp_acl_atcam_region *aregion);
	struct mlxsw_sp_acl_atcam_lkey_id *
		(*lkey_id_get)(struct mlxsw_sp_acl_atcam_region *aregion,
			       char *enc_key, u8 erp_id);
	void (*lkey_id_put)(struct mlxsw_sp_acl_atcam_region *aregion,
			    struct mlxsw_sp_acl_atcam_lkey_id *lkey_id);
};

struct mlxsw_sp_acl_atcam_region_generic {
	struct mlxsw_sp_acl_atcam_lkey_id dummy_lkey_id;
};

struct mlxsw_sp_acl_atcam_region_12kb {
	struct rhashtable lkey_ht;
	unsigned int max_lkey_id;
	unsigned long *used_lkey_id;
};

static const struct rhashtable_params mlxsw_sp_acl_atcam_lkey_id_ht_params = {
	.key_len = sizeof(struct mlxsw_sp_acl_atcam_lkey_id_ht_key),
	.key_offset = offsetof(struct mlxsw_sp_acl_atcam_lkey_id, ht_key),
	.head_offset = offsetof(struct mlxsw_sp_acl_atcam_lkey_id, ht_node),
};

static const struct rhashtable_params mlxsw_sp_acl_atcam_entries_ht_params = {
	.key_len = sizeof(struct mlxsw_sp_acl_atcam_entry_ht_key),
	.key_offset = offsetof(struct mlxsw_sp_acl_atcam_entry, ht_key),
	.head_offset = offsetof(struct mlxsw_sp_acl_atcam_entry, ht_node),
};

static bool
mlxsw_sp_acl_atcam_is_centry(const struct mlxsw_sp_acl_atcam_entry *aentry)
{
	return mlxsw_sp_acl_erp_mask_is_ctcam(aentry->erp_mask);
}

static int
mlxsw_sp_acl_atcam_region_generic_init(struct mlxsw_sp_acl_atcam_region *aregion)
{
	struct mlxsw_sp_acl_atcam_region_generic *region_generic;

	region_generic = kzalloc(sizeof(*region_generic), GFP_KERNEL);
	if (!region_generic)
		return -ENOMEM;

	refcount_set(&region_generic->dummy_lkey_id.refcnt, 1);
	aregion->priv = region_generic;

	return 0;
}

static void
mlxsw_sp_acl_atcam_region_generic_fini(struct mlxsw_sp_acl_atcam_region *aregion)
{
	kfree(aregion->priv);
}

static struct mlxsw_sp_acl_atcam_lkey_id *
mlxsw_sp_acl_atcam_generic_lkey_id_get(struct mlxsw_sp_acl_atcam_region *aregion,
				       char *enc_key, u8 erp_id)
{
	struct mlxsw_sp_acl_atcam_region_generic *region_generic;

	region_generic = aregion->priv;
	return &region_generic->dummy_lkey_id;
}

static void
mlxsw_sp_acl_atcam_generic_lkey_id_put(struct mlxsw_sp_acl_atcam_region *aregion,
				       struct mlxsw_sp_acl_atcam_lkey_id *lkey_id)
{
}

static const struct mlxsw_sp_acl_atcam_region_ops
mlxsw_sp_acl_atcam_region_generic_ops = {
	.init		= mlxsw_sp_acl_atcam_region_generic_init,
	.fini		= mlxsw_sp_acl_atcam_region_generic_fini,
	.lkey_id_get	= mlxsw_sp_acl_atcam_generic_lkey_id_get,
	.lkey_id_put	= mlxsw_sp_acl_atcam_generic_lkey_id_put,
};

static int
mlxsw_sp_acl_atcam_region_12kb_init(struct mlxsw_sp_acl_atcam_region *aregion)
{
	struct mlxsw_sp *mlxsw_sp = aregion->region->mlxsw_sp;
	struct mlxsw_sp_acl_atcam_region_12kb *region_12kb;
	size_t alloc_size;
	u64 max_lkey_id;
	int err;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, ACL_MAX_LARGE_KEY_ID))
		return -EIO;

	max_lkey_id = MLXSW_CORE_RES_GET(mlxsw_sp->core, ACL_MAX_LARGE_KEY_ID);
	region_12kb = kzalloc(sizeof(*region_12kb), GFP_KERNEL);
	if (!region_12kb)
		return -ENOMEM;

	alloc_size = BITS_TO_LONGS(max_lkey_id) * sizeof(unsigned long);
	region_12kb->used_lkey_id = kzalloc(alloc_size, GFP_KERNEL);
	if (!region_12kb->used_lkey_id) {
		err = -ENOMEM;
		goto err_used_lkey_id_alloc;
	}

	err = rhashtable_init(&region_12kb->lkey_ht,
			      &mlxsw_sp_acl_atcam_lkey_id_ht_params);
	if (err)
		goto err_rhashtable_init;

	region_12kb->max_lkey_id = max_lkey_id;
	aregion->priv = region_12kb;

	return 0;

err_rhashtable_init:
	kfree(region_12kb->used_lkey_id);
err_used_lkey_id_alloc:
	kfree(region_12kb);
	return err;
}

static void
mlxsw_sp_acl_atcam_region_12kb_fini(struct mlxsw_sp_acl_atcam_region *aregion)
{
	struct mlxsw_sp_acl_atcam_region_12kb *region_12kb = aregion->priv;

	rhashtable_destroy(&region_12kb->lkey_ht);
	kfree(region_12kb->used_lkey_id);
	kfree(region_12kb);
}

static struct mlxsw_sp_acl_atcam_lkey_id *
mlxsw_sp_acl_atcam_lkey_id_create(struct mlxsw_sp_acl_atcam_region *aregion,
				  struct mlxsw_sp_acl_atcam_lkey_id_ht_key *ht_key)
{
	struct mlxsw_sp_acl_atcam_region_12kb *region_12kb = aregion->priv;
	struct mlxsw_sp_acl_atcam_lkey_id *lkey_id;
	u32 id;
	int err;

	id = find_first_zero_bit(region_12kb->used_lkey_id,
				 region_12kb->max_lkey_id);
	if (id < region_12kb->max_lkey_id)
		__set_bit(id, region_12kb->used_lkey_id);
	else
		return ERR_PTR(-ENOBUFS);

	lkey_id = kzalloc(sizeof(*lkey_id), GFP_KERNEL);
	if (!lkey_id) {
		err = -ENOMEM;
		goto err_lkey_id_alloc;
	}

	lkey_id->id = id;
	memcpy(&lkey_id->ht_key, ht_key, sizeof(*ht_key));
	refcount_set(&lkey_id->refcnt, 1);

	err = rhashtable_insert_fast(&region_12kb->lkey_ht,
				     &lkey_id->ht_node,
				     mlxsw_sp_acl_atcam_lkey_id_ht_params);
	if (err)
		goto err_rhashtable_insert;

	return lkey_id;

err_rhashtable_insert:
	kfree(lkey_id);
err_lkey_id_alloc:
	__clear_bit(id, region_12kb->used_lkey_id);
	return ERR_PTR(err);
}

static void
mlxsw_sp_acl_atcam_lkey_id_destroy(struct mlxsw_sp_acl_atcam_region *aregion,
				   struct mlxsw_sp_acl_atcam_lkey_id *lkey_id)
{
	struct mlxsw_sp_acl_atcam_region_12kb *region_12kb = aregion->priv;
	u32 id = lkey_id->id;

	rhashtable_remove_fast(&region_12kb->lkey_ht, &lkey_id->ht_node,
			       mlxsw_sp_acl_atcam_lkey_id_ht_params);
	kfree(lkey_id);
	__clear_bit(id, region_12kb->used_lkey_id);
}

static struct mlxsw_sp_acl_atcam_lkey_id *
mlxsw_sp_acl_atcam_12kb_lkey_id_get(struct mlxsw_sp_acl_atcam_region *aregion,
				    char *enc_key, u8 erp_id)
{
	struct mlxsw_sp_acl_atcam_region_12kb *region_12kb = aregion->priv;
	struct mlxsw_sp_acl_tcam_region *region = aregion->region;
	struct mlxsw_sp_acl_atcam_lkey_id_ht_key ht_key = {{ 0 } };
	struct mlxsw_sp *mlxsw_sp = region->mlxsw_sp;
	struct mlxsw_afk *afk = mlxsw_sp_acl_afk(mlxsw_sp->acl);
	struct mlxsw_sp_acl_atcam_lkey_id *lkey_id;

	memcpy(ht_key.enc_key, enc_key, sizeof(ht_key.enc_key));
	mlxsw_afk_clear(afk, ht_key.enc_key,
			MLXSW_SP_ACL_ATCAM_LKEY_ID_BLOCK_CLEAR_START,
			MLXSW_SP_ACL_ATCAM_LKEY_ID_BLOCK_CLEAR_END);
	ht_key.erp_id = erp_id;
	lkey_id = rhashtable_lookup_fast(&region_12kb->lkey_ht, &ht_key,
					 mlxsw_sp_acl_atcam_lkey_id_ht_params);
	if (lkey_id) {
		refcount_inc(&lkey_id->refcnt);
		return lkey_id;
	}

	return mlxsw_sp_acl_atcam_lkey_id_create(aregion, &ht_key);
}

static void
mlxsw_sp_acl_atcam_12kb_lkey_id_put(struct mlxsw_sp_acl_atcam_region *aregion,
				    struct mlxsw_sp_acl_atcam_lkey_id *lkey_id)
{
	if (refcount_dec_and_test(&lkey_id->refcnt))
		mlxsw_sp_acl_atcam_lkey_id_destroy(aregion, lkey_id);
}

static const struct mlxsw_sp_acl_atcam_region_ops
mlxsw_sp_acl_atcam_region_12kb_ops = {
	.init		= mlxsw_sp_acl_atcam_region_12kb_init,
	.fini		= mlxsw_sp_acl_atcam_region_12kb_fini,
	.lkey_id_get	= mlxsw_sp_acl_atcam_12kb_lkey_id_get,
	.lkey_id_put	= mlxsw_sp_acl_atcam_12kb_lkey_id_put,
};

static const struct mlxsw_sp_acl_atcam_region_ops *
mlxsw_sp_acl_atcam_region_ops_arr[] = {
	[MLXSW_SP_ACL_ATCAM_REGION_TYPE_2KB]	=
		&mlxsw_sp_acl_atcam_region_generic_ops,
	[MLXSW_SP_ACL_ATCAM_REGION_TYPE_4KB]	=
		&mlxsw_sp_acl_atcam_region_generic_ops,
	[MLXSW_SP_ACL_ATCAM_REGION_TYPE_8KB]	=
		&mlxsw_sp_acl_atcam_region_generic_ops,
	[MLXSW_SP_ACL_ATCAM_REGION_TYPE_12KB]	=
		&mlxsw_sp_acl_atcam_region_12kb_ops,
};

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

static void
mlxsw_sp_acl_atcam_region_type_init(struct mlxsw_sp_acl_atcam_region *aregion)
{
	struct mlxsw_sp_acl_tcam_region *region = aregion->region;
	enum mlxsw_sp_acl_atcam_region_type region_type;
	unsigned int blocks_count;

	/* We already know the blocks count can not exceed the maximum
	 * blocks count.
	 */
	blocks_count = mlxsw_afk_key_info_blocks_count_get(region->key_info);
	if (blocks_count <= 2)
		region_type = MLXSW_SP_ACL_ATCAM_REGION_TYPE_2KB;
	else if (blocks_count <= 4)
		region_type = MLXSW_SP_ACL_ATCAM_REGION_TYPE_4KB;
	else if (blocks_count <= 8)
		region_type = MLXSW_SP_ACL_ATCAM_REGION_TYPE_8KB;
	else
		region_type = MLXSW_SP_ACL_ATCAM_REGION_TYPE_12KB;

	aregion->type = region_type;
	aregion->ops = mlxsw_sp_acl_atcam_region_ops_arr[region_type];
}

int
mlxsw_sp_acl_atcam_region_init(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_atcam *atcam,
			       struct mlxsw_sp_acl_atcam_region *aregion,
			       struct mlxsw_sp_acl_tcam_region *region,
			       const struct mlxsw_sp_acl_ctcam_region_ops *ops)
{
	int err;

	aregion->region = region;
	aregion->atcam = atcam;
	mlxsw_sp_acl_atcam_region_type_init(aregion);
	INIT_LIST_HEAD(&aregion->entries_list);

	err = rhashtable_init(&aregion->entries_ht,
			      &mlxsw_sp_acl_atcam_entries_ht_params);
	if (err)
		return err;
	err = aregion->ops->init(aregion);
	if (err)
		goto err_ops_init;
	err = mlxsw_sp_acl_erp_region_init(aregion);
	if (err)
		goto err_erp_region_init;
	err = mlxsw_sp_acl_ctcam_region_init(mlxsw_sp, &aregion->cregion,
					     region, ops);
	if (err)
		goto err_ctcam_region_init;

	return 0;

err_ctcam_region_init:
	mlxsw_sp_acl_erp_region_fini(aregion);
err_erp_region_init:
	aregion->ops->fini(aregion);
err_ops_init:
	rhashtable_destroy(&aregion->entries_ht);
	return err;
}

void mlxsw_sp_acl_atcam_region_fini(struct mlxsw_sp_acl_atcam_region *aregion)
{
	mlxsw_sp_acl_ctcam_region_fini(&aregion->cregion);
	mlxsw_sp_acl_erp_region_fini(aregion);
	aregion->ops->fini(aregion);
	rhashtable_destroy(&aregion->entries_ht);
	WARN_ON(!list_empty(&aregion->entries_list));
}

void mlxsw_sp_acl_atcam_chunk_init(struct mlxsw_sp_acl_atcam_region *aregion,
				   struct mlxsw_sp_acl_atcam_chunk *achunk,
				   unsigned int priority)
{
	mlxsw_sp_acl_ctcam_chunk_init(&aregion->cregion, &achunk->cchunk,
				      priority);
}

void mlxsw_sp_acl_atcam_chunk_fini(struct mlxsw_sp_acl_atcam_chunk *achunk)
{
	mlxsw_sp_acl_ctcam_chunk_fini(&achunk->cchunk);
}

static int
mlxsw_sp_acl_atcam_region_entry_insert(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_acl_atcam_region *aregion,
				       struct mlxsw_sp_acl_atcam_entry *aentry,
				       struct mlxsw_sp_acl_rule_info *rulei)
{
	struct mlxsw_sp_acl_tcam_region *region = aregion->region;
	u8 erp_id = mlxsw_sp_acl_erp_mask_erp_id(aentry->erp_mask);
	struct mlxsw_sp_acl_atcam_lkey_id *lkey_id;
	char ptce3_pl[MLXSW_REG_PTCE3_LEN];
	u32 kvdl_index, priority;
	int err;

	err = mlxsw_sp_acl_tcam_priority_get(mlxsw_sp, rulei, &priority, true);
	if (err)
		return err;

	lkey_id = aregion->ops->lkey_id_get(aregion, aentry->ht_key.enc_key,
					    erp_id);
	if (IS_ERR(lkey_id))
		return PTR_ERR(lkey_id);
	aentry->lkey_id = lkey_id;

	kvdl_index = mlxsw_afa_block_first_kvdl_index(rulei->act_block);
	mlxsw_reg_ptce3_pack(ptce3_pl, true, MLXSW_REG_PTCE3_OP_WRITE_WRITE,
			     priority, region->tcam_region_info,
			     aentry->ht_key.enc_key, erp_id,
			     aentry->delta_info.start,
			     aentry->delta_info.mask,
			     aentry->delta_info.value,
			     refcount_read(&lkey_id->refcnt) != 1, lkey_id->id,
			     kvdl_index);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ptce3), ptce3_pl);
	if (err)
		goto err_ptce3_write;

	return 0;

err_ptce3_write:
	aregion->ops->lkey_id_put(aregion, lkey_id);
	return err;
}

static void
mlxsw_sp_acl_atcam_region_entry_remove(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_acl_atcam_region *aregion,
				       struct mlxsw_sp_acl_atcam_entry *aentry)
{
	struct mlxsw_sp_acl_atcam_lkey_id *lkey_id = aentry->lkey_id;
	struct mlxsw_sp_acl_tcam_region *region = aregion->region;
	u8 erp_id = mlxsw_sp_acl_erp_mask_erp_id(aentry->erp_mask);
	char *enc_key = aentry->ht_key.enc_key;
	char ptce3_pl[MLXSW_REG_PTCE3_LEN];

	mlxsw_reg_ptce3_pack(ptce3_pl, false, MLXSW_REG_PTCE3_OP_WRITE_WRITE, 0,
			     region->tcam_region_info,
			     enc_key, erp_id,
			     aentry->delta_info.start,
			     aentry->delta_info.mask,
			     aentry->delta_info.value,
			     refcount_read(&lkey_id->refcnt) != 1,
			     lkey_id->id, 0);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ptce3), ptce3_pl);
	aregion->ops->lkey_id_put(aregion, lkey_id);
}

static int
mlxsw_sp_acl_atcam_region_entry_action_replace(struct mlxsw_sp *mlxsw_sp,
					       struct mlxsw_sp_acl_atcam_region *aregion,
					       struct mlxsw_sp_acl_atcam_entry *aentry,
					       struct mlxsw_sp_acl_rule_info *rulei)
{
	struct mlxsw_sp_acl_atcam_lkey_id *lkey_id = aentry->lkey_id;
	u8 erp_id = mlxsw_sp_acl_erp_mask_erp_id(aentry->erp_mask);
	struct mlxsw_sp_acl_tcam_region *region = aregion->region;
	char ptce3_pl[MLXSW_REG_PTCE3_LEN];
	u32 kvdl_index, priority;
	int err;

	err = mlxsw_sp_acl_tcam_priority_get(mlxsw_sp, rulei, &priority, true);
	if (err)
		return err;
	kvdl_index = mlxsw_afa_block_first_kvdl_index(rulei->act_block);
	mlxsw_reg_ptce3_pack(ptce3_pl, true, MLXSW_REG_PTCE3_OP_WRITE_UPDATE,
			     priority, region->tcam_region_info,
			     aentry->ht_key.enc_key, erp_id,
			     aentry->delta_info.start,
			     aentry->delta_info.mask,
			     aentry->delta_info.value,
			     refcount_read(&lkey_id->refcnt) != 1, lkey_id->id,
			     kvdl_index);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ptce3), ptce3_pl);
}

static int
__mlxsw_sp_acl_atcam_entry_add(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_atcam_region *aregion,
			       struct mlxsw_sp_acl_atcam_entry *aentry,
			       struct mlxsw_sp_acl_rule_info *rulei)
{
	struct mlxsw_sp_acl_tcam_region *region = aregion->region;
	char mask[MLXSW_REG_PTCEX_FLEX_KEY_BLOCKS_LEN] = { 0 };
	struct mlxsw_afk *afk = mlxsw_sp_acl_afk(mlxsw_sp->acl);
	const struct mlxsw_sp_acl_erp_delta *delta;
	struct mlxsw_sp_acl_erp_mask *erp_mask;
	int err;

	mlxsw_afk_encode(afk, region->key_info, &rulei->values,
			 aentry->full_enc_key, mask);

	erp_mask = mlxsw_sp_acl_erp_mask_get(aregion, mask, false);
	if (IS_ERR(erp_mask))
		return PTR_ERR(erp_mask);
	aentry->erp_mask = erp_mask;
	aentry->ht_key.erp_id = mlxsw_sp_acl_erp_mask_erp_id(erp_mask);
	memcpy(aentry->ht_key.enc_key, aentry->full_enc_key,
	       sizeof(aentry->ht_key.enc_key));

	/* Compute all needed delta information and clear the delta bits
	 * from the encrypted key.
	 */
	delta = mlxsw_sp_acl_erp_delta(aentry->erp_mask);
	aentry->delta_info.start = mlxsw_sp_acl_erp_delta_start(delta);
	aentry->delta_info.mask = mlxsw_sp_acl_erp_delta_mask(delta);
	aentry->delta_info.value =
		mlxsw_sp_acl_erp_delta_value(delta, aentry->full_enc_key);
	mlxsw_sp_acl_erp_delta_clear(delta, aentry->ht_key.enc_key);

	/* Add rule to the list of A-TCAM rules, assuming this
	 * rule is intended to A-TCAM. In case this rule does
	 * not fit into A-TCAM it will be removed from the list.
	 */
	list_add(&aentry->list, &aregion->entries_list);

	/* We can't insert identical rules into the A-TCAM, so fail and
	 * let the rule spill into C-TCAM
	 */
	err = rhashtable_lookup_insert_fast(&aregion->entries_ht,
					    &aentry->ht_node,
					    mlxsw_sp_acl_atcam_entries_ht_params);
	if (err)
		goto err_rhashtable_insert;

	/* Bloom filter must be updated here, before inserting the rule into
	 * the A-TCAM.
	 */
	err = mlxsw_sp_acl_erp_bf_insert(mlxsw_sp, aregion, erp_mask, aentry);
	if (err)
		goto err_bf_insert;

	err = mlxsw_sp_acl_atcam_region_entry_insert(mlxsw_sp, aregion, aentry,
						     rulei);
	if (err)
		goto err_rule_insert;

	return 0;

err_rule_insert:
	mlxsw_sp_acl_erp_bf_remove(mlxsw_sp, aregion, erp_mask, aentry);
err_bf_insert:
	rhashtable_remove_fast(&aregion->entries_ht, &aentry->ht_node,
			       mlxsw_sp_acl_atcam_entries_ht_params);
err_rhashtable_insert:
	list_del(&aentry->list);
	mlxsw_sp_acl_erp_mask_put(aregion, erp_mask);
	return err;
}

static void
__mlxsw_sp_acl_atcam_entry_del(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_atcam_region *aregion,
			       struct mlxsw_sp_acl_atcam_entry *aentry)
{
	mlxsw_sp_acl_atcam_region_entry_remove(mlxsw_sp, aregion, aentry);
	mlxsw_sp_acl_erp_bf_remove(mlxsw_sp, aregion, aentry->erp_mask, aentry);
	rhashtable_remove_fast(&aregion->entries_ht, &aentry->ht_node,
			       mlxsw_sp_acl_atcam_entries_ht_params);
	list_del(&aentry->list);
	mlxsw_sp_acl_erp_mask_put(aregion, aentry->erp_mask);
}

static int
__mlxsw_sp_acl_atcam_entry_action_replace(struct mlxsw_sp *mlxsw_sp,
					  struct mlxsw_sp_acl_atcam_region *aregion,
					  struct mlxsw_sp_acl_atcam_entry *aentry,
					  struct mlxsw_sp_acl_rule_info *rulei)
{
	return mlxsw_sp_acl_atcam_region_entry_action_replace(mlxsw_sp, aregion,
							      aentry, rulei);
}

int mlxsw_sp_acl_atcam_entry_add(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_acl_atcam_region *aregion,
				 struct mlxsw_sp_acl_atcam_chunk *achunk,
				 struct mlxsw_sp_acl_atcam_entry *aentry,
				 struct mlxsw_sp_acl_rule_info *rulei)
{
	int err;

	err = __mlxsw_sp_acl_atcam_entry_add(mlxsw_sp, aregion, aentry, rulei);
	if (!err)
		return 0;

	/* It is possible we failed to add the rule to the A-TCAM due to
	 * exceeded number of masks. Try to spill into C-TCAM.
	 */
	err = mlxsw_sp_acl_ctcam_entry_add(mlxsw_sp, &aregion->cregion,
					   &achunk->cchunk, &aentry->centry,
					   rulei, true);
	if (!err)
		return 0;

	return err;
}

void mlxsw_sp_acl_atcam_entry_del(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_acl_atcam_region *aregion,
				  struct mlxsw_sp_acl_atcam_chunk *achunk,
				  struct mlxsw_sp_acl_atcam_entry *aentry)
{
	if (mlxsw_sp_acl_atcam_is_centry(aentry))
		mlxsw_sp_acl_ctcam_entry_del(mlxsw_sp, &aregion->cregion,
					     &achunk->cchunk, &aentry->centry);
	else
		__mlxsw_sp_acl_atcam_entry_del(mlxsw_sp, aregion, aentry);
}

int
mlxsw_sp_acl_atcam_entry_action_replace(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_acl_atcam_region *aregion,
					struct mlxsw_sp_acl_atcam_chunk *achunk,
					struct mlxsw_sp_acl_atcam_entry *aentry,
					struct mlxsw_sp_acl_rule_info *rulei)
{
	int err;

	if (mlxsw_sp_acl_atcam_is_centry(aentry))
		err = mlxsw_sp_acl_ctcam_entry_action_replace(mlxsw_sp,
							      &aregion->cregion,
							      &achunk->cchunk,
							      &aentry->centry,
							      rulei);
	else
		err = __mlxsw_sp_acl_atcam_entry_action_replace(mlxsw_sp,
								aregion, aentry,
								rulei);

	return err;
}

int mlxsw_sp_acl_atcam_init(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_acl_atcam *atcam)
{
	return mlxsw_sp_acl_erps_init(mlxsw_sp, atcam);
}

void mlxsw_sp_acl_atcam_fini(struct mlxsw_sp *mlxsw_sp,
			     struct mlxsw_sp_acl_atcam *atcam)
{
	mlxsw_sp_acl_erps_fini(mlxsw_sp, atcam);
}
