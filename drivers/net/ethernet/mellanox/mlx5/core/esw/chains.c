// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2020 Mellanox Technologies.

#include <linux/mlx5/driver.h>
#include <linux/mlx5/mlx5_ifc.h>
#include <linux/mlx5/fs.h>

#include "esw/chains.h"
#include "en/mapping.h"
#include "mlx5_core.h"
#include "fs_core.h"
#include "eswitch.h"
#include "en.h"
#include "en_tc.h"

#define esw_chains_priv(esw) ((esw)->fdb_table.offloads.esw_chains_priv)
#define esw_chains_lock(esw) (esw_chains_priv(esw)->lock)
#define esw_chains_ht(esw) (esw_chains_priv(esw)->chains_ht)
#define esw_chains_mapping(esw) (esw_chains_priv(esw)->chains_mapping)
#define esw_prios_ht(esw) (esw_chains_priv(esw)->prios_ht)
#define fdb_pool_left(esw) (esw_chains_priv(esw)->fdb_left)
#define tc_slow_fdb(esw) ((esw)->fdb_table.offloads.slow_fdb)
#define tc_end_fdb(esw) (esw_chains_priv(esw)->tc_end_fdb)
#define fdb_ignore_flow_level_supported(esw) \
	(MLX5_CAP_ESW_FLOWTABLE_FDB((esw)->dev, ignore_flow_level))
#define fdb_modify_header_fwd_to_table_supported(esw) \
	(MLX5_CAP_ESW_FLOWTABLE((esw)->dev, fdb_modify_header_fwd_to_table))

/* Firmware currently has 4 pool of 4 sizes that it supports (ESW_POOLS),
 * and a virtual memory region of 16M (ESW_SIZE), this region is duplicated
 * for each flow table pool. We can allocate up to 16M of each pool,
 * and we keep track of how much we used via get_next_avail_sz_from_pool.
 * Firmware doesn't report any of this for now.
 * ESW_POOL is expected to be sorted from large to small and match firmware
 * pools.
 */
#define ESW_SIZE (16 * 1024 * 1024)
static const unsigned int ESW_POOLS[] = { 4 * 1024 * 1024,
					  1 * 1024 * 1024,
					  64 * 1024,
					  128 };
#define ESW_FT_TBL_SZ (64 * 1024)

struct mlx5_esw_chains_priv {
	struct rhashtable chains_ht;
	struct rhashtable prios_ht;
	/* Protects above chains_ht and prios_ht */
	struct mutex lock;

	struct mlx5_flow_table *tc_end_fdb;
	struct mapping_ctx *chains_mapping;

	int fdb_left[ARRAY_SIZE(ESW_POOLS)];
};

struct fdb_chain {
	struct rhash_head node;

	u32 chain;

	int ref;
	int id;

	struct mlx5_eswitch *esw;
	struct list_head prios_list;
	struct mlx5_flow_handle *restore_rule;
	struct mlx5_modify_hdr *miss_modify_hdr;
};

struct fdb_prio_key {
	u32 chain;
	u32 prio;
	u32 level;
};

struct fdb_prio {
	struct rhash_head node;
	struct list_head list;

	struct fdb_prio_key key;

	int ref;

	struct fdb_chain *fdb_chain;
	struct mlx5_flow_table *fdb;
	struct mlx5_flow_table *next_fdb;
	struct mlx5_flow_group *miss_group;
	struct mlx5_flow_handle *miss_rule;
};

static const struct rhashtable_params chain_params = {
	.head_offset = offsetof(struct fdb_chain, node),
	.key_offset = offsetof(struct fdb_chain, chain),
	.key_len = sizeof_field(struct fdb_chain, chain),
	.automatic_shrinking = true,
};

static const struct rhashtable_params prio_params = {
	.head_offset = offsetof(struct fdb_prio, node),
	.key_offset = offsetof(struct fdb_prio, key),
	.key_len = sizeof_field(struct fdb_prio, key),
	.automatic_shrinking = true,
};

bool mlx5_esw_chains_prios_supported(struct mlx5_eswitch *esw)
{
	return esw->fdb_table.flags & ESW_FDB_CHAINS_AND_PRIOS_SUPPORTED;
}

bool mlx5_esw_chains_backwards_supported(struct mlx5_eswitch *esw)
{
	return mlx5_esw_chains_prios_supported(esw) &&
	       fdb_ignore_flow_level_supported(esw);
}

u32 mlx5_esw_chains_get_chain_range(struct mlx5_eswitch *esw)
{
	if (!mlx5_esw_chains_prios_supported(esw))
		return 1;

	if (fdb_ignore_flow_level_supported(esw))
		return UINT_MAX - 1;

	return FDB_TC_MAX_CHAIN;
}

u32 mlx5_esw_chains_get_ft_chain(struct mlx5_eswitch *esw)
{
	return mlx5_esw_chains_get_chain_range(esw) + 1;
}

u32 mlx5_esw_chains_get_prio_range(struct mlx5_eswitch *esw)
{
	if (!mlx5_esw_chains_prios_supported(esw))
		return 1;

	if (fdb_ignore_flow_level_supported(esw))
		return UINT_MAX;

	return FDB_TC_MAX_PRIO;
}

static unsigned int mlx5_esw_chains_get_level_range(struct mlx5_eswitch *esw)
{
	if (fdb_ignore_flow_level_supported(esw))
		return UINT_MAX;

	return FDB_TC_LEVELS_PER_PRIO;
}

#define POOL_NEXT_SIZE 0
static int
mlx5_esw_chains_get_avail_sz_from_pool(struct mlx5_eswitch *esw,
				       int desired_size)
{
	int i, found_i = -1;

	for (i = ARRAY_SIZE(ESW_POOLS) - 1; i >= 0; i--) {
		if (fdb_pool_left(esw)[i] && ESW_POOLS[i] > desired_size) {
			found_i = i;
			if (desired_size != POOL_NEXT_SIZE)
				break;
		}
	}

	if (found_i != -1) {
		--fdb_pool_left(esw)[found_i];
		return ESW_POOLS[found_i];
	}

	return 0;
}

static void
mlx5_esw_chains_put_sz_to_pool(struct mlx5_eswitch *esw, int sz)
{
	int i;

	for (i = ARRAY_SIZE(ESW_POOLS) - 1; i >= 0; i--) {
		if (sz == ESW_POOLS[i]) {
			++fdb_pool_left(esw)[i];
			return;
		}
	}

	WARN_ONCE(1, "Couldn't find size %d in fdb size pool", sz);
}

static void
mlx5_esw_chains_init_sz_pool(struct mlx5_eswitch *esw)
{
	u32 fdb_max;
	int i;

	fdb_max = 1 << MLX5_CAP_ESW_FLOWTABLE_FDB(esw->dev, log_max_ft_size);

	for (i = ARRAY_SIZE(ESW_POOLS) - 1; i >= 0; i--)
		fdb_pool_left(esw)[i] =
			ESW_POOLS[i] <= fdb_max ? ESW_SIZE / ESW_POOLS[i] : 0;
}

static struct mlx5_flow_table *
mlx5_esw_chains_create_fdb_table(struct mlx5_eswitch *esw,
				 u32 chain, u32 prio, u32 level)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_table *fdb;
	int sz;

	if (esw->offloads.encap != DEVLINK_ESWITCH_ENCAP_MODE_NONE)
		ft_attr.flags |= (MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT |
				  MLX5_FLOW_TABLE_TUNNEL_EN_DECAP);

	sz = (chain == mlx5_esw_chains_get_ft_chain(esw)) ?
	     mlx5_esw_chains_get_avail_sz_from_pool(esw, ESW_FT_TBL_SZ) :
	     mlx5_esw_chains_get_avail_sz_from_pool(esw, POOL_NEXT_SIZE);
	if (!sz)
		return ERR_PTR(-ENOSPC);
	ft_attr.max_fte = sz;

	/* We use tc_slow_fdb(esw) as the table's next_ft till
	 * ignore_flow_level is allowed on FT creation and not just for FTEs.
	 * Instead caller should add an explicit miss rule if needed.
	 */
	ft_attr.next_ft = tc_slow_fdb(esw);

	/* The root table(chain 0, prio 1, level 0) is required to be
	 * connected to the previous prio (FDB_BYPASS_PATH if exists).
	 * We always create it, as a managed table, in order to align with
	 * fs_core logic.
	 */
	if (!fdb_ignore_flow_level_supported(esw) ||
	    (chain == 0 && prio == 1 && level == 0)) {
		ft_attr.level = level;
		ft_attr.prio = prio - 1;
		ns = mlx5_get_fdb_sub_ns(esw->dev, chain);
	} else {
		ft_attr.flags |= MLX5_FLOW_TABLE_UNMANAGED;
		ft_attr.prio = FDB_TC_OFFLOAD;
		/* Firmware doesn't allow us to create another level 0 table,
		 * so we create all unmanaged tables as level 1.
		 *
		 * To connect them, we use explicit miss rules with
		 * ignore_flow_level. Caller is responsible to create
		 * these rules (if needed).
		 */
		ft_attr.level = 1;
		ns = mlx5_get_flow_namespace(esw->dev, MLX5_FLOW_NAMESPACE_FDB);
	}

	ft_attr.autogroup.num_reserved_entries = 2;
	ft_attr.autogroup.max_num_groups = esw->params.large_group_num;
	fdb = mlx5_create_auto_grouped_flow_table(ns, &ft_attr);
	if (IS_ERR(fdb)) {
		esw_warn(esw->dev,
			 "Failed to create FDB table err %d (chain: %d, prio: %d, level: %d, size: %d)\n",
			 (int)PTR_ERR(fdb), chain, prio, level, sz);
		mlx5_esw_chains_put_sz_to_pool(esw, sz);
		return fdb;
	}

	return fdb;
}

static void
mlx5_esw_chains_destroy_fdb_table(struct mlx5_eswitch *esw,
				  struct mlx5_flow_table *fdb)
{
	mlx5_esw_chains_put_sz_to_pool(esw, fdb->max_fte);
	mlx5_destroy_flow_table(fdb);
}

static int
create_fdb_chain_restore(struct fdb_chain *fdb_chain)
{
	char modact[MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto)];
	struct mlx5_eswitch *esw = fdb_chain->esw;
	struct mlx5_modify_hdr *mod_hdr;
	u32 index;
	int err;

	if (fdb_chain->chain == mlx5_esw_chains_get_ft_chain(esw) ||
	    !mlx5_esw_chains_prios_supported(esw))
		return 0;

	err = mapping_add(esw_chains_mapping(esw), &fdb_chain->chain, &index);
	if (err)
		return err;
	if (index == MLX5_FS_DEFAULT_FLOW_TAG) {
		/* we got the special default flow tag id, so we won't know
		 * if we actually marked the packet with the restore rule
		 * we create.
		 *
		 * This case isn't possible with MLX5_FS_DEFAULT_FLOW_TAG = 0.
		 */
		err = mapping_add(esw_chains_mapping(esw),
				  &fdb_chain->chain, &index);
		mapping_remove(esw_chains_mapping(esw),
			       MLX5_FS_DEFAULT_FLOW_TAG);
		if (err)
			return err;
	}

	fdb_chain->id = index;

	MLX5_SET(set_action_in, modact, action_type, MLX5_ACTION_TYPE_SET);
	MLX5_SET(set_action_in, modact, field,
		 mlx5e_tc_attr_to_reg_mappings[CHAIN_TO_REG].mfield);
	MLX5_SET(set_action_in, modact, offset,
		 mlx5e_tc_attr_to_reg_mappings[CHAIN_TO_REG].moffset * 8);
	MLX5_SET(set_action_in, modact, length,
		 mlx5e_tc_attr_to_reg_mappings[CHAIN_TO_REG].mlen * 8);
	MLX5_SET(set_action_in, modact, data, fdb_chain->id);
	mod_hdr = mlx5_modify_header_alloc(esw->dev, MLX5_FLOW_NAMESPACE_FDB,
					   1, modact);
	if (IS_ERR(mod_hdr)) {
		err = PTR_ERR(mod_hdr);
		goto err_mod_hdr;
	}
	fdb_chain->miss_modify_hdr = mod_hdr;

	fdb_chain->restore_rule = esw_add_restore_rule(esw, fdb_chain->id);
	if (IS_ERR(fdb_chain->restore_rule)) {
		err = PTR_ERR(fdb_chain->restore_rule);
		goto err_rule;
	}

	return 0;

err_rule:
	mlx5_modify_header_dealloc(esw->dev, fdb_chain->miss_modify_hdr);
err_mod_hdr:
	/* Datapath can't find this mapping, so we can safely remove it */
	mapping_remove(esw_chains_mapping(esw), fdb_chain->id);
	return err;
}

static void destroy_fdb_chain_restore(struct fdb_chain *fdb_chain)
{
	struct mlx5_eswitch *esw = fdb_chain->esw;

	if (!fdb_chain->miss_modify_hdr)
		return;

	mlx5_del_flow_rules(fdb_chain->restore_rule);
	mlx5_modify_header_dealloc(esw->dev, fdb_chain->miss_modify_hdr);
	mapping_remove(esw_chains_mapping(esw), fdb_chain->id);
}

static struct fdb_chain *
mlx5_esw_chains_create_fdb_chain(struct mlx5_eswitch *esw, u32 chain)
{
	struct fdb_chain *fdb_chain = NULL;
	int err;

	fdb_chain = kvzalloc(sizeof(*fdb_chain), GFP_KERNEL);
	if (!fdb_chain)
		return ERR_PTR(-ENOMEM);

	fdb_chain->esw = esw;
	fdb_chain->chain = chain;
	INIT_LIST_HEAD(&fdb_chain->prios_list);

	err = create_fdb_chain_restore(fdb_chain);
	if (err)
		goto err_restore;

	err = rhashtable_insert_fast(&esw_chains_ht(esw), &fdb_chain->node,
				     chain_params);
	if (err)
		goto err_insert;

	return fdb_chain;

err_insert:
	destroy_fdb_chain_restore(fdb_chain);
err_restore:
	kvfree(fdb_chain);
	return ERR_PTR(err);
}

static void
mlx5_esw_chains_destroy_fdb_chain(struct fdb_chain *fdb_chain)
{
	struct mlx5_eswitch *esw = fdb_chain->esw;

	rhashtable_remove_fast(&esw_chains_ht(esw), &fdb_chain->node,
			       chain_params);

	destroy_fdb_chain_restore(fdb_chain);
	kvfree(fdb_chain);
}

static struct fdb_chain *
mlx5_esw_chains_get_fdb_chain(struct mlx5_eswitch *esw, u32 chain)
{
	struct fdb_chain *fdb_chain;

	fdb_chain = rhashtable_lookup_fast(&esw_chains_ht(esw), &chain,
					   chain_params);
	if (!fdb_chain) {
		fdb_chain = mlx5_esw_chains_create_fdb_chain(esw, chain);
		if (IS_ERR(fdb_chain))
			return fdb_chain;
	}

	fdb_chain->ref++;

	return fdb_chain;
}

static struct mlx5_flow_handle *
mlx5_esw_chains_add_miss_rule(struct fdb_chain *fdb_chain,
			      struct mlx5_flow_table *fdb,
			      struct mlx5_flow_table *next_fdb)
{
	struct mlx5_eswitch *esw = fdb_chain->esw;
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_act act = {};

	act.flags  = FLOW_ACT_IGNORE_FLOW_LEVEL | FLOW_ACT_NO_APPEND;
	act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	dest.type  = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = next_fdb;

	if (next_fdb == tc_end_fdb(esw) &&
	    mlx5_esw_chains_prios_supported(esw)) {
		act.modify_hdr = fdb_chain->miss_modify_hdr;
		act.action |= MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
	}

	return mlx5_add_flow_rules(fdb, NULL, &act, &dest, 1);
}

static int
mlx5_esw_chains_update_prio_prevs(struct fdb_prio *fdb_prio,
				  struct mlx5_flow_table *next_fdb)
{
	struct mlx5_flow_handle *miss_rules[FDB_TC_LEVELS_PER_PRIO + 1] = {};
	struct fdb_chain *fdb_chain = fdb_prio->fdb_chain;
	struct fdb_prio *pos;
	int n = 0, err;

	if (fdb_prio->key.level)
		return 0;

	/* Iterate in reverse order until reaching the level 0 rule of
	 * the previous priority, adding all the miss rules first, so we can
	 * revert them if any of them fails.
	 */
	pos = fdb_prio;
	list_for_each_entry_continue_reverse(pos,
					     &fdb_chain->prios_list,
					     list) {
		miss_rules[n] = mlx5_esw_chains_add_miss_rule(fdb_chain,
							      pos->fdb,
							      next_fdb);
		if (IS_ERR(miss_rules[n])) {
			err = PTR_ERR(miss_rules[n]);
			goto err_prev_rule;
		}

		n++;
		if (!pos->key.level)
			break;
	}

	/* Success, delete old miss rules, and update the pointers. */
	n = 0;
	pos = fdb_prio;
	list_for_each_entry_continue_reverse(pos,
					     &fdb_chain->prios_list,
					     list) {
		mlx5_del_flow_rules(pos->miss_rule);

		pos->miss_rule = miss_rules[n];
		pos->next_fdb = next_fdb;

		n++;
		if (!pos->key.level)
			break;
	}

	return 0;

err_prev_rule:
	while (--n >= 0)
		mlx5_del_flow_rules(miss_rules[n]);

	return err;
}

static void
mlx5_esw_chains_put_fdb_chain(struct fdb_chain *fdb_chain)
{
	if (--fdb_chain->ref == 0)
		mlx5_esw_chains_destroy_fdb_chain(fdb_chain);
}

static struct fdb_prio *
mlx5_esw_chains_create_fdb_prio(struct mlx5_eswitch *esw,
				u32 chain, u32 prio, u32 level)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_handle *miss_rule = NULL;
	struct mlx5_flow_group *miss_group;
	struct fdb_prio *fdb_prio = NULL;
	struct mlx5_flow_table *next_fdb;
	struct fdb_chain *fdb_chain;
	struct mlx5_flow_table *fdb;
	struct list_head *pos;
	u32 *flow_group_in;
	int err;

	fdb_chain = mlx5_esw_chains_get_fdb_chain(esw, chain);
	if (IS_ERR(fdb_chain))
		return ERR_CAST(fdb_chain);

	fdb_prio = kvzalloc(sizeof(*fdb_prio), GFP_KERNEL);
	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	if (!fdb_prio || !flow_group_in) {
		err = -ENOMEM;
		goto err_alloc;
	}

	/* Chain's prio list is sorted by prio and level.
	 * And all levels of some prio point to the next prio's level 0.
	 * Example list (prio, level):
	 * (3,0)->(3,1)->(5,0)->(5,1)->(6,1)->(7,0)
	 * In hardware, we will we have the following pointers:
	 * (3,0) -> (5,0) -> (7,0) -> Slow path
	 * (3,1) -> (5,0)
	 * (5,1) -> (7,0)
	 * (6,1) -> (7,0)
	 */

	/* Default miss for each chain: */
	next_fdb = (chain == mlx5_esw_chains_get_ft_chain(esw)) ?
		    tc_slow_fdb(esw) :
		    tc_end_fdb(esw);
	list_for_each(pos, &fdb_chain->prios_list) {
		struct fdb_prio *p = list_entry(pos, struct fdb_prio, list);

		/* exit on first pos that is larger */
		if (prio < p->key.prio || (prio == p->key.prio &&
					   level < p->key.level)) {
			/* Get next level 0 table */
			next_fdb = p->key.level == 0 ? p->fdb : p->next_fdb;
			break;
		}
	}

	fdb = mlx5_esw_chains_create_fdb_table(esw, chain, prio, level);
	if (IS_ERR(fdb)) {
		err = PTR_ERR(fdb);
		goto err_create;
	}

	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index,
		 fdb->max_fte - 2);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index,
		 fdb->max_fte - 1);
	miss_group = mlx5_create_flow_group(fdb, flow_group_in);
	if (IS_ERR(miss_group)) {
		err = PTR_ERR(miss_group);
		goto err_group;
	}

	/* Add miss rule to next_fdb */
	miss_rule = mlx5_esw_chains_add_miss_rule(fdb_chain, fdb, next_fdb);
	if (IS_ERR(miss_rule)) {
		err = PTR_ERR(miss_rule);
		goto err_miss_rule;
	}

	fdb_prio->miss_group = miss_group;
	fdb_prio->miss_rule = miss_rule;
	fdb_prio->next_fdb = next_fdb;
	fdb_prio->fdb_chain = fdb_chain;
	fdb_prio->key.chain = chain;
	fdb_prio->key.prio = prio;
	fdb_prio->key.level = level;
	fdb_prio->fdb = fdb;

	err = rhashtable_insert_fast(&esw_prios_ht(esw), &fdb_prio->node,
				     prio_params);
	if (err)
		goto err_insert;

	list_add(&fdb_prio->list, pos->prev);

	/* Table is ready, connect it */
	err = mlx5_esw_chains_update_prio_prevs(fdb_prio, fdb);
	if (err)
		goto err_update;

	kvfree(flow_group_in);
	return fdb_prio;

err_update:
	list_del(&fdb_prio->list);
	rhashtable_remove_fast(&esw_prios_ht(esw), &fdb_prio->node,
			       prio_params);
err_insert:
	mlx5_del_flow_rules(miss_rule);
err_miss_rule:
	mlx5_destroy_flow_group(miss_group);
err_group:
	mlx5_esw_chains_destroy_fdb_table(esw, fdb);
err_create:
err_alloc:
	kvfree(fdb_prio);
	kvfree(flow_group_in);
	mlx5_esw_chains_put_fdb_chain(fdb_chain);
	return ERR_PTR(err);
}

static void
mlx5_esw_chains_destroy_fdb_prio(struct mlx5_eswitch *esw,
				 struct fdb_prio *fdb_prio)
{
	struct fdb_chain *fdb_chain = fdb_prio->fdb_chain;

	WARN_ON(mlx5_esw_chains_update_prio_prevs(fdb_prio,
						  fdb_prio->next_fdb));

	list_del(&fdb_prio->list);
	rhashtable_remove_fast(&esw_prios_ht(esw), &fdb_prio->node,
			       prio_params);
	mlx5_del_flow_rules(fdb_prio->miss_rule);
	mlx5_destroy_flow_group(fdb_prio->miss_group);
	mlx5_esw_chains_destroy_fdb_table(esw, fdb_prio->fdb);
	mlx5_esw_chains_put_fdb_chain(fdb_chain);
	kvfree(fdb_prio);
}

struct mlx5_flow_table *
mlx5_esw_chains_get_table(struct mlx5_eswitch *esw, u32 chain, u32 prio,
			  u32 level)
{
	struct mlx5_flow_table *prev_fts;
	struct fdb_prio *fdb_prio;
	struct fdb_prio_key key;
	int l = 0;

	if ((chain > mlx5_esw_chains_get_chain_range(esw) &&
	     chain != mlx5_esw_chains_get_ft_chain(esw)) ||
	    prio > mlx5_esw_chains_get_prio_range(esw) ||
	    level > mlx5_esw_chains_get_level_range(esw))
		return ERR_PTR(-EOPNOTSUPP);

	/* create earlier levels for correct fs_core lookup when
	 * connecting tables.
	 */
	for (l = 0; l < level; l++) {
		prev_fts = mlx5_esw_chains_get_table(esw, chain, prio, l);
		if (IS_ERR(prev_fts)) {
			fdb_prio = ERR_CAST(prev_fts);
			goto err_get_prevs;
		}
	}

	key.chain = chain;
	key.prio = prio;
	key.level = level;

	mutex_lock(&esw_chains_lock(esw));
	fdb_prio = rhashtable_lookup_fast(&esw_prios_ht(esw), &key,
					  prio_params);
	if (!fdb_prio) {
		fdb_prio = mlx5_esw_chains_create_fdb_prio(esw, chain,
							   prio, level);
		if (IS_ERR(fdb_prio))
			goto err_create_prio;
	}

	++fdb_prio->ref;
	mutex_unlock(&esw_chains_lock(esw));

	return fdb_prio->fdb;

err_create_prio:
	mutex_unlock(&esw_chains_lock(esw));
err_get_prevs:
	while (--l >= 0)
		mlx5_esw_chains_put_table(esw, chain, prio, l);
	return ERR_CAST(fdb_prio);
}

void
mlx5_esw_chains_put_table(struct mlx5_eswitch *esw, u32 chain, u32 prio,
			  u32 level)
{
	struct fdb_prio *fdb_prio;
	struct fdb_prio_key key;

	key.chain = chain;
	key.prio = prio;
	key.level = level;

	mutex_lock(&esw_chains_lock(esw));
	fdb_prio = rhashtable_lookup_fast(&esw_prios_ht(esw), &key,
					  prio_params);
	if (!fdb_prio)
		goto err_get_prio;

	if (--fdb_prio->ref == 0)
		mlx5_esw_chains_destroy_fdb_prio(esw, fdb_prio);
	mutex_unlock(&esw_chains_lock(esw));

	while (level-- > 0)
		mlx5_esw_chains_put_table(esw, chain, prio, level);

	return;

err_get_prio:
	mutex_unlock(&esw_chains_lock(esw));
	WARN_ONCE(1,
		  "Couldn't find table: (chain: %d prio: %d level: %d)",
		  chain, prio, level);
}

struct mlx5_flow_table *
mlx5_esw_chains_get_tc_end_ft(struct mlx5_eswitch *esw)
{
	return tc_end_fdb(esw);
}

struct mlx5_flow_table *
mlx5_esw_chains_create_global_table(struct mlx5_eswitch *esw)
{
	u32 chain, prio, level;
	int err;

	if (!fdb_ignore_flow_level_supported(esw)) {
		err = -EOPNOTSUPP;

		esw_warn(esw->dev,
			 "Couldn't create global flow table, ignore_flow_level not supported.");
		goto err_ignore;
	}

	chain = mlx5_esw_chains_get_chain_range(esw),
	prio = mlx5_esw_chains_get_prio_range(esw);
	level = mlx5_esw_chains_get_level_range(esw);

	return mlx5_esw_chains_create_fdb_table(esw, chain, prio, level);

err_ignore:
	return ERR_PTR(err);
}

void
mlx5_esw_chains_destroy_global_table(struct mlx5_eswitch *esw,
				     struct mlx5_flow_table *ft)
{
	mlx5_esw_chains_destroy_fdb_table(esw, ft);
}

static int
mlx5_esw_chains_init(struct mlx5_eswitch *esw)
{
	struct mlx5_esw_chains_priv *chains_priv;
	struct mlx5_core_dev *dev = esw->dev;
	u32 max_flow_counter, fdb_max;
	struct mapping_ctx *mapping;
	int err;

	chains_priv = kzalloc(sizeof(*chains_priv), GFP_KERNEL);
	if (!chains_priv)
		return -ENOMEM;
	esw_chains_priv(esw) = chains_priv;

	max_flow_counter = (MLX5_CAP_GEN(dev, max_flow_counter_31_16) << 16) |
			    MLX5_CAP_GEN(dev, max_flow_counter_15_0);
	fdb_max = 1 << MLX5_CAP_ESW_FLOWTABLE_FDB(dev, log_max_ft_size);

	esw_debug(dev,
		  "Init esw offloads chains, max counters(%d), groups(%d), max flow table size(%d)\n",
		  max_flow_counter, esw->params.large_group_num, fdb_max);

	mlx5_esw_chains_init_sz_pool(esw);

	if (!MLX5_CAP_ESW_FLOWTABLE(esw->dev, multi_fdb_encap) &&
	    esw->offloads.encap != DEVLINK_ESWITCH_ENCAP_MODE_NONE) {
		esw->fdb_table.flags &= ~ESW_FDB_CHAINS_AND_PRIOS_SUPPORTED;
		esw_warn(dev, "Tc chains and priorities offload aren't supported, update firmware if needed\n");
	} else if (!mlx5_eswitch_reg_c1_loopback_enabled(esw)) {
		esw->fdb_table.flags &= ~ESW_FDB_CHAINS_AND_PRIOS_SUPPORTED;
		esw_warn(dev, "Tc chains and priorities offload aren't supported\n");
	} else if (!fdb_modify_header_fwd_to_table_supported(esw)) {
		/* Disabled when ttl workaround is needed, e.g
		 * when ESWITCH_IPV4_TTL_MODIFY_ENABLE = true in mlxconfig
		 */
		esw_warn(dev,
			 "Tc chains and priorities offload aren't supported, check firmware version, or mlxconfig settings\n");
		esw->fdb_table.flags &= ~ESW_FDB_CHAINS_AND_PRIOS_SUPPORTED;
	} else {
		esw->fdb_table.flags |= ESW_FDB_CHAINS_AND_PRIOS_SUPPORTED;
		esw_info(dev, "Supported tc offload range - chains: %u, prios: %u\n",
			 mlx5_esw_chains_get_chain_range(esw),
			 mlx5_esw_chains_get_prio_range(esw));
	}

	err = rhashtable_init(&esw_chains_ht(esw), &chain_params);
	if (err)
		goto init_chains_ht_err;

	err = rhashtable_init(&esw_prios_ht(esw), &prio_params);
	if (err)
		goto init_prios_ht_err;

	mapping = mapping_create(sizeof(u32), esw_get_max_restore_tag(esw),
				 true);
	if (IS_ERR(mapping)) {
		err = PTR_ERR(mapping);
		goto mapping_err;
	}
	esw_chains_mapping(esw) = mapping;

	mutex_init(&esw_chains_lock(esw));

	return 0;

mapping_err:
	rhashtable_destroy(&esw_prios_ht(esw));
init_prios_ht_err:
	rhashtable_destroy(&esw_chains_ht(esw));
init_chains_ht_err:
	kfree(chains_priv);
	return err;
}

static void
mlx5_esw_chains_cleanup(struct mlx5_eswitch *esw)
{
	mutex_destroy(&esw_chains_lock(esw));
	mapping_destroy(esw_chains_mapping(esw));
	rhashtable_destroy(&esw_prios_ht(esw));
	rhashtable_destroy(&esw_chains_ht(esw));

	kfree(esw_chains_priv(esw));
}

static int
mlx5_esw_chains_open(struct mlx5_eswitch *esw)
{
	struct mlx5_flow_table *ft;
	int err;

	/* Create tc_end_fdb(esw) which is the always created ft chain */
	ft = mlx5_esw_chains_get_table(esw, mlx5_esw_chains_get_ft_chain(esw),
				       1, 0);
	if (IS_ERR(ft))
		return PTR_ERR(ft);

	tc_end_fdb(esw) = ft;

	/* Always open the root for fast path */
	ft = mlx5_esw_chains_get_table(esw, 0, 1, 0);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		goto level_0_err;
	}

	/* Open level 1 for split rules now if prios isn't supported  */
	if (!mlx5_esw_chains_prios_supported(esw)) {
		err = mlx5_esw_vport_tbl_get(esw);
		if (err)
			goto level_1_err;
	}

	return 0;

level_1_err:
	mlx5_esw_chains_put_table(esw, 0, 1, 0);
level_0_err:
	mlx5_esw_chains_put_table(esw, mlx5_esw_chains_get_ft_chain(esw), 1, 0);
	return err;
}

static void
mlx5_esw_chains_close(struct mlx5_eswitch *esw)
{
	if (!mlx5_esw_chains_prios_supported(esw))
		mlx5_esw_vport_tbl_put(esw);
	mlx5_esw_chains_put_table(esw, 0, 1, 0);
	mlx5_esw_chains_put_table(esw, mlx5_esw_chains_get_ft_chain(esw), 1, 0);
}

int
mlx5_esw_chains_create(struct mlx5_eswitch *esw)
{
	int err;

	err = mlx5_esw_chains_init(esw);
	if (err)
		return err;

	err = mlx5_esw_chains_open(esw);
	if (err)
		goto err_open;

	return 0;

err_open:
	mlx5_esw_chains_cleanup(esw);
	return err;
}

void
mlx5_esw_chains_destroy(struct mlx5_eswitch *esw)
{
	mlx5_esw_chains_close(esw);
	mlx5_esw_chains_cleanup(esw);
}

int
mlx5_esw_chains_get_chain_mapping(struct mlx5_eswitch *esw, u32 chain,
				  u32 *chain_mapping)
{
	return mapping_add(esw_chains_mapping(esw), &chain, chain_mapping);
}

int
mlx5_esw_chains_put_chain_mapping(struct mlx5_eswitch *esw, u32 chain_mapping)
{
	return mapping_remove(esw_chains_mapping(esw), chain_mapping);
}

int mlx5_eswitch_get_chain_for_tag(struct mlx5_eswitch *esw, u32 tag,
				   u32 *chain)
{
	int err;

	err = mapping_find(esw_chains_mapping(esw), tag, chain);
	if (err) {
		esw_warn(esw->dev, "Can't find chain for tag: %d\n", tag);
		return -ENOENT;
	}

	return 0;
}
