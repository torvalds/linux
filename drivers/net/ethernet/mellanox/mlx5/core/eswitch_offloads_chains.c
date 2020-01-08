// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2020 Mellanox Technologies.

#include <linux/mlx5/driver.h>
#include <linux/mlx5/mlx5_ifc.h>
#include <linux/mlx5/fs.h>

#include "eswitch_offloads_chains.h"
#include "mlx5_core.h"
#include "fs_core.h"
#include "eswitch.h"
#include "en.h"

#define esw_chains_priv(esw) ((esw)->fdb_table.offloads.esw_chains_priv)
#define esw_chains_lock(esw) (esw_chains_priv(esw)->lock)
#define esw_chains_ht(esw) (esw_chains_priv(esw)->chains_ht)
#define esw_prios_ht(esw) (esw_chains_priv(esw)->prios_ht)
#define fdb_pool_left(esw) (esw_chains_priv(esw)->fdb_left)

#define ESW_OFFLOADS_NUM_GROUPS  4

/* Firmware currently has 4 pool of 4 sizes that it supports (ESW_POOLS),
 * and a virtual memory region of 16M (ESW_SIZE), this region is duplicated
 * for each flow table pool. We can allocate up to 16M of each pool,
 * and we keep track of how much we used via get_next_avail_sz_from_pool.
 * Firmware doesn't report any of this for now.
 * ESW_POOL is expected to be sorted from large to small and match firmware
 * pools.
 */
#define ESW_SIZE (16 * 1024 * 1024)
const unsigned int ESW_POOLS[] = { 4 * 1024 * 1024,
				   1 * 1024 * 1024,
				   64 * 1024,
				   4 * 1024, };

struct mlx5_esw_chains_priv {
	struct rhashtable chains_ht;
	struct rhashtable prios_ht;
	/* Protects above chains_ht and prios_ht */
	struct mutex lock;

	int fdb_left[ARRAY_SIZE(ESW_POOLS)];
};

struct fdb_chain {
	struct rhash_head node;

	u32 chain;

	int ref;

	struct mlx5_eswitch *esw;
};

struct fdb_prio_key {
	u32 chain;
	u32 prio;
	u32 level;
};

struct fdb_prio {
	struct rhash_head node;

	struct fdb_prio_key key;

	int ref;

	struct fdb_chain *fdb_chain;
	struct mlx5_flow_table *fdb;
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

u32 mlx5_esw_chains_get_chain_range(struct mlx5_eswitch *esw)
{
	if (!mlx5_esw_chains_prios_supported(esw))
		return 1;

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

	return FDB_TC_MAX_PRIO;
}

static unsigned int mlx5_esw_chains_get_level_range(struct mlx5_eswitch *esw)
{
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

	sz = mlx5_esw_chains_get_avail_sz_from_pool(esw, POOL_NEXT_SIZE);
	if (!sz)
		return ERR_PTR(-ENOSPC);

	ft_attr.max_fte = sz;
	ft_attr.level = level;
	ft_attr.prio = prio - 1;
	ft_attr.autogroup.max_num_groups = ESW_OFFLOADS_NUM_GROUPS;
	ns = mlx5_get_fdb_sub_ns(esw->dev, chain);

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

	err = rhashtable_insert_fast(&esw_chains_ht(esw), &fdb_chain->node,
				     chain_params);
	if (err)
		goto err_insert;

	return fdb_chain;

err_insert:
	kvfree(fdb_chain);
	return ERR_PTR(err);
}

static void
mlx5_esw_chains_destroy_fdb_chain(struct fdb_chain *fdb_chain)
{
	struct mlx5_eswitch *esw = fdb_chain->esw;

	rhashtable_remove_fast(&esw_chains_ht(esw), &fdb_chain->node,
			       chain_params);
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
	struct fdb_prio *fdb_prio = NULL;
	struct fdb_chain *fdb_chain;
	struct mlx5_flow_table *fdb;
	int err;

	fdb_chain = mlx5_esw_chains_get_fdb_chain(esw, chain);
	if (IS_ERR(fdb_chain))
		return ERR_CAST(fdb_chain);

	fdb_prio = kvzalloc(sizeof(*fdb_prio), GFP_KERNEL);
	if (!fdb_prio) {
		err = -ENOMEM;
		goto err_alloc;
	}

	fdb = mlx5_esw_chains_create_fdb_table(esw, fdb_chain->chain, prio,
					       level);
	if (IS_ERR(fdb)) {
		err = PTR_ERR(fdb);
		goto err_create;
	}

	fdb_prio->fdb_chain = fdb_chain;
	fdb_prio->key.chain = chain;
	fdb_prio->key.prio = prio;
	fdb_prio->key.level = level;
	fdb_prio->fdb = fdb;

	err = rhashtable_insert_fast(&esw_prios_ht(esw), &fdb_prio->node,
				     prio_params);
	if (err)
		goto err_insert;

	return fdb_prio;

err_insert:
	mlx5_esw_chains_destroy_fdb_table(esw, fdb);
err_create:
	kvfree(fdb_prio);
err_alloc:
	mlx5_esw_chains_put_fdb_chain(fdb_chain);
	return ERR_PTR(err);
}

static void
mlx5_esw_chains_destroy_fdb_prio(struct mlx5_eswitch *esw,
				 struct fdb_prio *fdb_prio)
{
	struct fdb_chain *fdb_chain = fdb_prio->fdb_chain;

	rhashtable_remove_fast(&esw_prios_ht(esw), &fdb_prio->node,
			       prio_params);
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

static int
mlx5_esw_chains_init(struct mlx5_eswitch *esw)
{
	struct mlx5_esw_chains_priv *chains_priv;
	struct mlx5_core_dev *dev = esw->dev;
	u32 max_flow_counter, fdb_max;
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
		  max_flow_counter, ESW_OFFLOADS_NUM_GROUPS, fdb_max);

	mlx5_esw_chains_init_sz_pool(esw);

	if (!MLX5_CAP_ESW_FLOWTABLE(esw->dev, multi_fdb_encap) &&
	    esw->offloads.encap != DEVLINK_ESWITCH_ENCAP_MODE_NONE) {
		esw->fdb_table.flags &= ~ESW_FDB_CHAINS_AND_PRIOS_SUPPORTED;
		esw_warn(dev, "Tc chains and priorities offload aren't supported, update firmware if needed\n");
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

	mutex_init(&esw_chains_lock(esw));

	return 0;

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
	rhashtable_destroy(&esw_prios_ht(esw));
	rhashtable_destroy(&esw_chains_ht(esw));

	kfree(esw_chains_priv(esw));
}

static int
mlx5_esw_chains_open(struct mlx5_eswitch *esw)
{
	struct mlx5_flow_table *ft;
	int err;

	/* Always open the root for fast path */
	ft = mlx5_esw_chains_get_table(esw, 0, 1, 0);
	if (IS_ERR(ft))
		return PTR_ERR(ft);

	/* Open level 1 for split rules now if prios isn't supported  */
	if (!mlx5_esw_chains_prios_supported(esw)) {
		ft = mlx5_esw_chains_get_table(esw, 0, 1, 1);

		if (IS_ERR(ft)) {
			err = PTR_ERR(ft);
			goto level_1_err;
		}
	}

	return 0;

level_1_err:
	mlx5_esw_chains_put_table(esw, 0, 1, 0);
	return err;
}

static void
mlx5_esw_chains_close(struct mlx5_eswitch *esw)
{
	if (!mlx5_esw_chains_prios_supported(esw))
		mlx5_esw_chains_put_table(esw, 0, 1, 1);
	mlx5_esw_chains_put_table(esw, 0, 1, 0);
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
