/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_router.c
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 * Copyright (c) 2016 Ido Schimmel <idosch@mellanox.com>
 * Copyright (c) 2016 Yotam Gigi <yotamg@mellanox.com>
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
#include <linux/types.h>
#include <linux/rhashtable.h>
#include <linux/bitops.h>
#include <linux/in6.h>
#include <linux/notifier.h>
#include <linux/inetdevice.h>
#include <net/netevent.h>
#include <net/neighbour.h>
#include <net/arp.h>
#include <net/ip_fib.h>

#include "spectrum.h"
#include "core.h"
#include "reg.h"

#define mlxsw_sp_prefix_usage_for_each(prefix, prefix_usage) \
	for_each_set_bit(prefix, (prefix_usage)->b, MLXSW_SP_PREFIX_COUNT)

static bool
mlxsw_sp_prefix_usage_subset(struct mlxsw_sp_prefix_usage *prefix_usage1,
			     struct mlxsw_sp_prefix_usage *prefix_usage2)
{
	unsigned char prefix;

	mlxsw_sp_prefix_usage_for_each(prefix, prefix_usage1) {
		if (!test_bit(prefix, prefix_usage2->b))
			return false;
	}
	return true;
}

static bool
mlxsw_sp_prefix_usage_eq(struct mlxsw_sp_prefix_usage *prefix_usage1,
			 struct mlxsw_sp_prefix_usage *prefix_usage2)
{
	return !memcmp(prefix_usage1, prefix_usage2, sizeof(*prefix_usage1));
}

static bool
mlxsw_sp_prefix_usage_none(struct mlxsw_sp_prefix_usage *prefix_usage)
{
	struct mlxsw_sp_prefix_usage prefix_usage_none = {{ 0 } };

	return mlxsw_sp_prefix_usage_eq(prefix_usage, &prefix_usage_none);
}

static void
mlxsw_sp_prefix_usage_cpy(struct mlxsw_sp_prefix_usage *prefix_usage1,
			  struct mlxsw_sp_prefix_usage *prefix_usage2)
{
	memcpy(prefix_usage1, prefix_usage2, sizeof(*prefix_usage1));
}

static void
mlxsw_sp_prefix_usage_zero(struct mlxsw_sp_prefix_usage *prefix_usage)
{
	memset(prefix_usage, 0, sizeof(*prefix_usage));
}

static void
mlxsw_sp_prefix_usage_set(struct mlxsw_sp_prefix_usage *prefix_usage,
			  unsigned char prefix_len)
{
	set_bit(prefix_len, prefix_usage->b);
}

static void
mlxsw_sp_prefix_usage_clear(struct mlxsw_sp_prefix_usage *prefix_usage,
			    unsigned char prefix_len)
{
	clear_bit(prefix_len, prefix_usage->b);
}

struct mlxsw_sp_fib_key {
	unsigned char addr[sizeof(struct in6_addr)];
	unsigned char prefix_len;
};

enum mlxsw_sp_fib_entry_type {
	MLXSW_SP_FIB_ENTRY_TYPE_REMOTE,
	MLXSW_SP_FIB_ENTRY_TYPE_LOCAL,
	MLXSW_SP_FIB_ENTRY_TYPE_TRAP,
};

struct mlxsw_sp_nexthop_group;

struct mlxsw_sp_fib_node {
	struct list_head entry_list;
	struct list_head list;
	struct rhash_head ht_node;
	struct mlxsw_sp_vr *vr;
	struct mlxsw_sp_fib_key key;
};

struct mlxsw_sp_fib_entry_params {
	u32 tb_id;
	u32 prio;
	u8 tos;
	u8 type;
};

struct mlxsw_sp_fib_entry {
	struct list_head list;
	struct mlxsw_sp_fib_node *fib_node;
	enum mlxsw_sp_fib_entry_type type;
	struct list_head nexthop_group_node;
	struct mlxsw_sp_nexthop_group *nh_group;
	struct mlxsw_sp_fib_entry_params params;
	bool offloaded;
};

struct mlxsw_sp_fib {
	struct rhashtable ht;
	struct list_head node_list;
	unsigned long prefix_ref_count[MLXSW_SP_PREFIX_COUNT];
	struct mlxsw_sp_prefix_usage prefix_usage;
};

static const struct rhashtable_params mlxsw_sp_fib_ht_params;

static struct mlxsw_sp_fib *mlxsw_sp_fib_create(void)
{
	struct mlxsw_sp_fib *fib;
	int err;

	fib = kzalloc(sizeof(*fib), GFP_KERNEL);
	if (!fib)
		return ERR_PTR(-ENOMEM);
	err = rhashtable_init(&fib->ht, &mlxsw_sp_fib_ht_params);
	if (err)
		goto err_rhashtable_init;
	INIT_LIST_HEAD(&fib->node_list);
	return fib;

err_rhashtable_init:
	kfree(fib);
	return ERR_PTR(err);
}

static void mlxsw_sp_fib_destroy(struct mlxsw_sp_fib *fib)
{
	WARN_ON(!list_empty(&fib->node_list));
	rhashtable_destroy(&fib->ht);
	kfree(fib);
}

static struct mlxsw_sp_lpm_tree *
mlxsw_sp_lpm_tree_find_unused(struct mlxsw_sp *mlxsw_sp, bool one_reserved)
{
	static struct mlxsw_sp_lpm_tree *lpm_tree;
	int i;

	for (i = 0; i < MLXSW_SP_LPM_TREE_COUNT; i++) {
		lpm_tree = &mlxsw_sp->router.lpm_trees[i];
		if (lpm_tree->ref_count == 0) {
			if (one_reserved)
				one_reserved = false;
			else
				return lpm_tree;
		}
	}
	return NULL;
}

static int mlxsw_sp_lpm_tree_alloc(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_lpm_tree *lpm_tree)
{
	char ralta_pl[MLXSW_REG_RALTA_LEN];

	mlxsw_reg_ralta_pack(ralta_pl, true,
			     (enum mlxsw_reg_ralxx_protocol) lpm_tree->proto,
			     lpm_tree->id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralta), ralta_pl);
}

static int mlxsw_sp_lpm_tree_free(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_lpm_tree *lpm_tree)
{
	char ralta_pl[MLXSW_REG_RALTA_LEN];

	mlxsw_reg_ralta_pack(ralta_pl, false,
			     (enum mlxsw_reg_ralxx_protocol) lpm_tree->proto,
			     lpm_tree->id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralta), ralta_pl);
}

static int
mlxsw_sp_lpm_tree_left_struct_set(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_prefix_usage *prefix_usage,
				  struct mlxsw_sp_lpm_tree *lpm_tree)
{
	char ralst_pl[MLXSW_REG_RALST_LEN];
	u8 root_bin = 0;
	u8 prefix;
	u8 last_prefix = MLXSW_REG_RALST_BIN_NO_CHILD;

	mlxsw_sp_prefix_usage_for_each(prefix, prefix_usage)
		root_bin = prefix;

	mlxsw_reg_ralst_pack(ralst_pl, root_bin, lpm_tree->id);
	mlxsw_sp_prefix_usage_for_each(prefix, prefix_usage) {
		if (prefix == 0)
			continue;
		mlxsw_reg_ralst_bin_pack(ralst_pl, prefix, last_prefix,
					 MLXSW_REG_RALST_BIN_NO_CHILD);
		last_prefix = prefix;
	}
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralst), ralst_pl);
}

static struct mlxsw_sp_lpm_tree *
mlxsw_sp_lpm_tree_create(struct mlxsw_sp *mlxsw_sp,
			 struct mlxsw_sp_prefix_usage *prefix_usage,
			 enum mlxsw_sp_l3proto proto, bool one_reserved)
{
	struct mlxsw_sp_lpm_tree *lpm_tree;
	int err;

	lpm_tree = mlxsw_sp_lpm_tree_find_unused(mlxsw_sp, one_reserved);
	if (!lpm_tree)
		return ERR_PTR(-EBUSY);
	lpm_tree->proto = proto;
	err = mlxsw_sp_lpm_tree_alloc(mlxsw_sp, lpm_tree);
	if (err)
		return ERR_PTR(err);

	err = mlxsw_sp_lpm_tree_left_struct_set(mlxsw_sp, prefix_usage,
						lpm_tree);
	if (err)
		goto err_left_struct_set;
	memcpy(&lpm_tree->prefix_usage, prefix_usage,
	       sizeof(lpm_tree->prefix_usage));
	return lpm_tree;

err_left_struct_set:
	mlxsw_sp_lpm_tree_free(mlxsw_sp, lpm_tree);
	return ERR_PTR(err);
}

static int mlxsw_sp_lpm_tree_destroy(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_lpm_tree *lpm_tree)
{
	return mlxsw_sp_lpm_tree_free(mlxsw_sp, lpm_tree);
}

static struct mlxsw_sp_lpm_tree *
mlxsw_sp_lpm_tree_get(struct mlxsw_sp *mlxsw_sp,
		      struct mlxsw_sp_prefix_usage *prefix_usage,
		      enum mlxsw_sp_l3proto proto, bool one_reserved)
{
	struct mlxsw_sp_lpm_tree *lpm_tree;
	int i;

	for (i = 0; i < MLXSW_SP_LPM_TREE_COUNT; i++) {
		lpm_tree = &mlxsw_sp->router.lpm_trees[i];
		if (lpm_tree->ref_count != 0 &&
		    lpm_tree->proto == proto &&
		    mlxsw_sp_prefix_usage_eq(&lpm_tree->prefix_usage,
					     prefix_usage))
			goto inc_ref_count;
	}
	lpm_tree = mlxsw_sp_lpm_tree_create(mlxsw_sp, prefix_usage,
					    proto, one_reserved);
	if (IS_ERR(lpm_tree))
		return lpm_tree;

inc_ref_count:
	lpm_tree->ref_count++;
	return lpm_tree;
}

static int mlxsw_sp_lpm_tree_put(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_lpm_tree *lpm_tree)
{
	if (--lpm_tree->ref_count == 0)
		return mlxsw_sp_lpm_tree_destroy(mlxsw_sp, lpm_tree);
	return 0;
}

static void mlxsw_sp_lpm_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_lpm_tree *lpm_tree;
	int i;

	for (i = 0; i < MLXSW_SP_LPM_TREE_COUNT; i++) {
		lpm_tree = &mlxsw_sp->router.lpm_trees[i];
		lpm_tree->id = i + MLXSW_SP_LPM_TREE_MIN;
	}
}

static struct mlxsw_sp_vr *mlxsw_sp_vr_find_unused(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_vr *vr;
	int i;

	for (i = 0; i < MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_VRS); i++) {
		vr = &mlxsw_sp->router.vrs[i];
		if (!vr->used)
			return vr;
	}
	return NULL;
}

static int mlxsw_sp_vr_lpm_tree_bind(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_vr *vr)
{
	char raltb_pl[MLXSW_REG_RALTB_LEN];

	mlxsw_reg_raltb_pack(raltb_pl, vr->id,
			     (enum mlxsw_reg_ralxx_protocol) vr->proto,
			     vr->lpm_tree->id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(raltb), raltb_pl);
}

static int mlxsw_sp_vr_lpm_tree_unbind(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_vr *vr)
{
	char raltb_pl[MLXSW_REG_RALTB_LEN];

	/* Bind to tree 0 which is default */
	mlxsw_reg_raltb_pack(raltb_pl, vr->id,
			     (enum mlxsw_reg_ralxx_protocol) vr->proto, 0);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(raltb), raltb_pl);
}

static u32 mlxsw_sp_fix_tb_id(u32 tb_id)
{
	/* For our purpose, squash main and local table into one */
	if (tb_id == RT_TABLE_LOCAL)
		tb_id = RT_TABLE_MAIN;
	return tb_id;
}

static struct mlxsw_sp_vr *mlxsw_sp_vr_find(struct mlxsw_sp *mlxsw_sp,
					    u32 tb_id,
					    enum mlxsw_sp_l3proto proto)
{
	struct mlxsw_sp_vr *vr;
	int i;

	tb_id = mlxsw_sp_fix_tb_id(tb_id);

	for (i = 0; i < MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_VRS); i++) {
		vr = &mlxsw_sp->router.vrs[i];
		if (vr->used && vr->proto == proto && vr->tb_id == tb_id)
			return vr;
	}
	return NULL;
}

static struct mlxsw_sp_vr *mlxsw_sp_vr_create(struct mlxsw_sp *mlxsw_sp,
					      unsigned char prefix_len,
					      u32 tb_id,
					      enum mlxsw_sp_l3proto proto)
{
	struct mlxsw_sp_prefix_usage req_prefix_usage;
	struct mlxsw_sp_lpm_tree *lpm_tree;
	struct mlxsw_sp_vr *vr;
	int err;

	vr = mlxsw_sp_vr_find_unused(mlxsw_sp);
	if (!vr)
		return ERR_PTR(-EBUSY);
	vr->fib = mlxsw_sp_fib_create();
	if (IS_ERR(vr->fib))
		return ERR_CAST(vr->fib);

	vr->proto = proto;
	vr->tb_id = tb_id;
	mlxsw_sp_prefix_usage_zero(&req_prefix_usage);
	mlxsw_sp_prefix_usage_set(&req_prefix_usage, prefix_len);
	lpm_tree = mlxsw_sp_lpm_tree_get(mlxsw_sp, &req_prefix_usage,
					 proto, true);
	if (IS_ERR(lpm_tree)) {
		err = PTR_ERR(lpm_tree);
		goto err_tree_get;
	}
	vr->lpm_tree = lpm_tree;
	err = mlxsw_sp_vr_lpm_tree_bind(mlxsw_sp, vr);
	if (err)
		goto err_tree_bind;

	vr->used = true;
	return vr;

err_tree_bind:
	mlxsw_sp_lpm_tree_put(mlxsw_sp, vr->lpm_tree);
err_tree_get:
	mlxsw_sp_fib_destroy(vr->fib);

	return ERR_PTR(err);
}

static void mlxsw_sp_vr_destroy(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_vr *vr)
{
	mlxsw_sp_vr_lpm_tree_unbind(mlxsw_sp, vr);
	mlxsw_sp_lpm_tree_put(mlxsw_sp, vr->lpm_tree);
	mlxsw_sp_fib_destroy(vr->fib);
	vr->used = false;
}

static int
mlxsw_sp_vr_lpm_tree_check(struct mlxsw_sp *mlxsw_sp, struct mlxsw_sp_vr *vr,
			   struct mlxsw_sp_prefix_usage *req_prefix_usage)
{
	struct mlxsw_sp_lpm_tree *lpm_tree;

	if (mlxsw_sp_prefix_usage_eq(req_prefix_usage,
				     &vr->lpm_tree->prefix_usage))
		return 0;

	lpm_tree = mlxsw_sp_lpm_tree_get(mlxsw_sp, req_prefix_usage,
					 vr->proto, false);
	if (IS_ERR(lpm_tree)) {
		/* We failed to get a tree according to the required
		 * prefix usage. However, the current tree might be still good
		 * for us if our requirement is subset of the prefixes used
		 * in the tree.
		 */
		if (mlxsw_sp_prefix_usage_subset(req_prefix_usage,
						 &vr->lpm_tree->prefix_usage))
			return 0;
		return PTR_ERR(lpm_tree);
	}

	mlxsw_sp_vr_lpm_tree_unbind(mlxsw_sp, vr);
	mlxsw_sp_lpm_tree_put(mlxsw_sp, vr->lpm_tree);
	vr->lpm_tree = lpm_tree;
	return mlxsw_sp_vr_lpm_tree_bind(mlxsw_sp, vr);
}

static struct mlxsw_sp_vr *mlxsw_sp_vr_get(struct mlxsw_sp *mlxsw_sp,
					   unsigned char prefix_len,
					   u32 tb_id,
					   enum mlxsw_sp_l3proto proto)
{
	struct mlxsw_sp_vr *vr;
	int err;

	tb_id = mlxsw_sp_fix_tb_id(tb_id);
	vr = mlxsw_sp_vr_find(mlxsw_sp, tb_id, proto);
	if (!vr) {
		vr = mlxsw_sp_vr_create(mlxsw_sp, prefix_len, tb_id, proto);
		if (IS_ERR(vr))
			return vr;
	} else {
		struct mlxsw_sp_prefix_usage req_prefix_usage;

		mlxsw_sp_prefix_usage_cpy(&req_prefix_usage,
					  &vr->fib->prefix_usage);
		mlxsw_sp_prefix_usage_set(&req_prefix_usage, prefix_len);
		/* Need to replace LPM tree in case new prefix is required. */
		err = mlxsw_sp_vr_lpm_tree_check(mlxsw_sp, vr,
						 &req_prefix_usage);
		if (err)
			return ERR_PTR(err);
	}
	return vr;
}

static void mlxsw_sp_vr_put(struct mlxsw_sp *mlxsw_sp, struct mlxsw_sp_vr *vr)
{
	/* Destroy virtual router entity in case the associated FIB is empty
	 * and allow it to be used for other tables in future. Otherwise,
	 * check if some prefix usage did not disappear and change tree if
	 * that is the case. Note that in case new, smaller tree cannot be
	 * allocated, the original one will be kept being used.
	 */
	if (mlxsw_sp_prefix_usage_none(&vr->fib->prefix_usage))
		mlxsw_sp_vr_destroy(mlxsw_sp, vr);
	else
		mlxsw_sp_vr_lpm_tree_check(mlxsw_sp, vr,
					   &vr->fib->prefix_usage);
}

static int mlxsw_sp_vrs_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_vr *vr;
	u64 max_vrs;
	int i;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, MAX_VRS))
		return -EIO;

	max_vrs = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_VRS);
	mlxsw_sp->router.vrs = kcalloc(max_vrs, sizeof(struct mlxsw_sp_vr),
				       GFP_KERNEL);
	if (!mlxsw_sp->router.vrs)
		return -ENOMEM;

	for (i = 0; i < max_vrs; i++) {
		vr = &mlxsw_sp->router.vrs[i];
		vr->id = i;
	}

	return 0;
}

static void mlxsw_sp_router_fib_flush(struct mlxsw_sp *mlxsw_sp);

static void mlxsw_sp_vrs_fini(struct mlxsw_sp *mlxsw_sp)
{
	/* At this stage we're guaranteed not to have new incoming
	 * FIB notifications and the work queue is free from FIBs
	 * sitting on top of mlxsw netdevs. However, we can still
	 * have other FIBs queued. Flush the queue before flushing
	 * the device's tables. No need for locks, as we're the only
	 * writer.
	 */
	mlxsw_core_flush_owq();
	mlxsw_sp_router_fib_flush(mlxsw_sp);
	kfree(mlxsw_sp->router.vrs);
}

struct mlxsw_sp_neigh_key {
	struct neighbour *n;
};

struct mlxsw_sp_neigh_entry {
	struct list_head rif_list_node;
	struct rhash_head ht_node;
	struct mlxsw_sp_neigh_key key;
	u16 rif;
	bool connected;
	unsigned char ha[ETH_ALEN];
	struct list_head nexthop_list; /* list of nexthops using
					* this neigh entry
					*/
	struct list_head nexthop_neighs_list_node;
};

static const struct rhashtable_params mlxsw_sp_neigh_ht_params = {
	.key_offset = offsetof(struct mlxsw_sp_neigh_entry, key),
	.head_offset = offsetof(struct mlxsw_sp_neigh_entry, ht_node),
	.key_len = sizeof(struct mlxsw_sp_neigh_key),
};

static struct mlxsw_sp_neigh_entry *
mlxsw_sp_neigh_entry_alloc(struct mlxsw_sp *mlxsw_sp, struct neighbour *n,
			   u16 rif)
{
	struct mlxsw_sp_neigh_entry *neigh_entry;

	neigh_entry = kzalloc(sizeof(*neigh_entry), GFP_KERNEL);
	if (!neigh_entry)
		return NULL;

	neigh_entry->key.n = n;
	neigh_entry->rif = rif;
	INIT_LIST_HEAD(&neigh_entry->nexthop_list);

	return neigh_entry;
}

static void mlxsw_sp_neigh_entry_free(struct mlxsw_sp_neigh_entry *neigh_entry)
{
	kfree(neigh_entry);
}

static int
mlxsw_sp_neigh_entry_insert(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_neigh_entry *neigh_entry)
{
	return rhashtable_insert_fast(&mlxsw_sp->router.neigh_ht,
				      &neigh_entry->ht_node,
				      mlxsw_sp_neigh_ht_params);
}

static void
mlxsw_sp_neigh_entry_remove(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_neigh_entry *neigh_entry)
{
	rhashtable_remove_fast(&mlxsw_sp->router.neigh_ht,
			       &neigh_entry->ht_node,
			       mlxsw_sp_neigh_ht_params);
}

static struct mlxsw_sp_neigh_entry *
mlxsw_sp_neigh_entry_create(struct mlxsw_sp *mlxsw_sp, struct neighbour *n)
{
	struct mlxsw_sp_neigh_entry *neigh_entry;
	struct mlxsw_sp_rif *r;
	int err;

	r = mlxsw_sp_rif_find_by_dev(mlxsw_sp, n->dev);
	if (!r)
		return ERR_PTR(-EINVAL);

	neigh_entry = mlxsw_sp_neigh_entry_alloc(mlxsw_sp, n, r->rif);
	if (!neigh_entry)
		return ERR_PTR(-ENOMEM);

	err = mlxsw_sp_neigh_entry_insert(mlxsw_sp, neigh_entry);
	if (err)
		goto err_neigh_entry_insert;

	list_add(&neigh_entry->rif_list_node, &r->neigh_list);

	return neigh_entry;

err_neigh_entry_insert:
	mlxsw_sp_neigh_entry_free(neigh_entry);
	return ERR_PTR(err);
}

static void
mlxsw_sp_neigh_entry_destroy(struct mlxsw_sp *mlxsw_sp,
			     struct mlxsw_sp_neigh_entry *neigh_entry)
{
	list_del(&neigh_entry->rif_list_node);
	mlxsw_sp_neigh_entry_remove(mlxsw_sp, neigh_entry);
	mlxsw_sp_neigh_entry_free(neigh_entry);
}

static struct mlxsw_sp_neigh_entry *
mlxsw_sp_neigh_entry_lookup(struct mlxsw_sp *mlxsw_sp, struct neighbour *n)
{
	struct mlxsw_sp_neigh_key key;

	key.n = n;
	return rhashtable_lookup_fast(&mlxsw_sp->router.neigh_ht,
				      &key, mlxsw_sp_neigh_ht_params);
}

static void
mlxsw_sp_router_neighs_update_interval_init(struct mlxsw_sp *mlxsw_sp)
{
	unsigned long interval = NEIGH_VAR(&arp_tbl.parms, DELAY_PROBE_TIME);

	mlxsw_sp->router.neighs_update.interval = jiffies_to_msecs(interval);
}

static void mlxsw_sp_router_neigh_ent_ipv4_process(struct mlxsw_sp *mlxsw_sp,
						   char *rauhtd_pl,
						   int ent_index)
{
	struct net_device *dev;
	struct neighbour *n;
	__be32 dipn;
	u32 dip;
	u16 rif;

	mlxsw_reg_rauhtd_ent_ipv4_unpack(rauhtd_pl, ent_index, &rif, &dip);

	if (!mlxsw_sp->rifs[rif]) {
		dev_err_ratelimited(mlxsw_sp->bus_info->dev, "Incorrect RIF in neighbour entry\n");
		return;
	}

	dipn = htonl(dip);
	dev = mlxsw_sp->rifs[rif]->dev;
	n = neigh_lookup(&arp_tbl, &dipn, dev);
	if (!n) {
		netdev_err(dev, "Failed to find matching neighbour for IP=%pI4h\n",
			   &dip);
		return;
	}

	netdev_dbg(dev, "Updating neighbour with IP=%pI4h\n", &dip);
	neigh_event_send(n, NULL);
	neigh_release(n);
}

static void mlxsw_sp_router_neigh_rec_ipv4_process(struct mlxsw_sp *mlxsw_sp,
						   char *rauhtd_pl,
						   int rec_index)
{
	u8 num_entries;
	int i;

	num_entries = mlxsw_reg_rauhtd_ipv4_rec_num_entries_get(rauhtd_pl,
								rec_index);
	/* Hardware starts counting at 0, so add 1. */
	num_entries++;

	/* Each record consists of several neighbour entries. */
	for (i = 0; i < num_entries; i++) {
		int ent_index;

		ent_index = rec_index * MLXSW_REG_RAUHTD_IPV4_ENT_PER_REC + i;
		mlxsw_sp_router_neigh_ent_ipv4_process(mlxsw_sp, rauhtd_pl,
						       ent_index);
	}

}

static void mlxsw_sp_router_neigh_rec_process(struct mlxsw_sp *mlxsw_sp,
					      char *rauhtd_pl, int rec_index)
{
	switch (mlxsw_reg_rauhtd_rec_type_get(rauhtd_pl, rec_index)) {
	case MLXSW_REG_RAUHTD_TYPE_IPV4:
		mlxsw_sp_router_neigh_rec_ipv4_process(mlxsw_sp, rauhtd_pl,
						       rec_index);
		break;
	case MLXSW_REG_RAUHTD_TYPE_IPV6:
		WARN_ON_ONCE(1);
		break;
	}
}

static bool mlxsw_sp_router_rauhtd_is_full(char *rauhtd_pl)
{
	u8 num_rec, last_rec_index, num_entries;

	num_rec = mlxsw_reg_rauhtd_num_rec_get(rauhtd_pl);
	last_rec_index = num_rec - 1;

	if (num_rec < MLXSW_REG_RAUHTD_REC_MAX_NUM)
		return false;
	if (mlxsw_reg_rauhtd_rec_type_get(rauhtd_pl, last_rec_index) ==
	    MLXSW_REG_RAUHTD_TYPE_IPV6)
		return true;

	num_entries = mlxsw_reg_rauhtd_ipv4_rec_num_entries_get(rauhtd_pl,
								last_rec_index);
	if (++num_entries == MLXSW_REG_RAUHTD_IPV4_ENT_PER_REC)
		return true;
	return false;
}

static int mlxsw_sp_router_neighs_update_rauhtd(struct mlxsw_sp *mlxsw_sp)
{
	char *rauhtd_pl;
	u8 num_rec;
	int i, err;

	rauhtd_pl = kmalloc(MLXSW_REG_RAUHTD_LEN, GFP_KERNEL);
	if (!rauhtd_pl)
		return -ENOMEM;

	/* Make sure the neighbour's netdev isn't removed in the
	 * process.
	 */
	rtnl_lock();
	do {
		mlxsw_reg_rauhtd_pack(rauhtd_pl, MLXSW_REG_RAUHTD_TYPE_IPV4);
		err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(rauhtd),
				      rauhtd_pl);
		if (err) {
			dev_err_ratelimited(mlxsw_sp->bus_info->dev, "Failed to dump neighbour talbe\n");
			break;
		}
		num_rec = mlxsw_reg_rauhtd_num_rec_get(rauhtd_pl);
		for (i = 0; i < num_rec; i++)
			mlxsw_sp_router_neigh_rec_process(mlxsw_sp, rauhtd_pl,
							  i);
	} while (mlxsw_sp_router_rauhtd_is_full(rauhtd_pl));
	rtnl_unlock();

	kfree(rauhtd_pl);
	return err;
}

static void mlxsw_sp_router_neighs_update_nh(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_neigh_entry *neigh_entry;

	/* Take RTNL mutex here to prevent lists from changes */
	rtnl_lock();
	list_for_each_entry(neigh_entry, &mlxsw_sp->router.nexthop_neighs_list,
			    nexthop_neighs_list_node)
		/* If this neigh have nexthops, make the kernel think this neigh
		 * is active regardless of the traffic.
		 */
		neigh_event_send(neigh_entry->key.n, NULL);
	rtnl_unlock();
}

static void
mlxsw_sp_router_neighs_update_work_schedule(struct mlxsw_sp *mlxsw_sp)
{
	unsigned long interval = mlxsw_sp->router.neighs_update.interval;

	mlxsw_core_schedule_dw(&mlxsw_sp->router.neighs_update.dw,
			       msecs_to_jiffies(interval));
}

static void mlxsw_sp_router_neighs_update_work(struct work_struct *work)
{
	struct mlxsw_sp *mlxsw_sp = container_of(work, struct mlxsw_sp,
						 router.neighs_update.dw.work);
	int err;

	err = mlxsw_sp_router_neighs_update_rauhtd(mlxsw_sp);
	if (err)
		dev_err(mlxsw_sp->bus_info->dev, "Could not update kernel for neigh activity");

	mlxsw_sp_router_neighs_update_nh(mlxsw_sp);

	mlxsw_sp_router_neighs_update_work_schedule(mlxsw_sp);
}

static void mlxsw_sp_router_probe_unresolved_nexthops(struct work_struct *work)
{
	struct mlxsw_sp_neigh_entry *neigh_entry;
	struct mlxsw_sp *mlxsw_sp = container_of(work, struct mlxsw_sp,
						 router.nexthop_probe_dw.work);

	/* Iterate over nexthop neighbours, find those who are unresolved and
	 * send arp on them. This solves the chicken-egg problem when
	 * the nexthop wouldn't get offloaded until the neighbor is resolved
	 * but it wouldn't get resolved ever in case traffic is flowing in HW
	 * using different nexthop.
	 *
	 * Take RTNL mutex here to prevent lists from changes.
	 */
	rtnl_lock();
	list_for_each_entry(neigh_entry, &mlxsw_sp->router.nexthop_neighs_list,
			    nexthop_neighs_list_node)
		if (!neigh_entry->connected)
			neigh_event_send(neigh_entry->key.n, NULL);
	rtnl_unlock();

	mlxsw_core_schedule_dw(&mlxsw_sp->router.nexthop_probe_dw,
			       MLXSW_SP_UNRESOLVED_NH_PROBE_INTERVAL);
}

static void
mlxsw_sp_nexthop_neigh_update(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_neigh_entry *neigh_entry,
			      bool removing);

static enum mlxsw_reg_rauht_op mlxsw_sp_rauht_op(bool adding)
{
	return adding ? MLXSW_REG_RAUHT_OP_WRITE_ADD :
			MLXSW_REG_RAUHT_OP_WRITE_DELETE;
}

static void
mlxsw_sp_router_neigh_entry_op4(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_neigh_entry *neigh_entry,
				enum mlxsw_reg_rauht_op op)
{
	struct neighbour *n = neigh_entry->key.n;
	u32 dip = ntohl(*((__be32 *) n->primary_key));
	char rauht_pl[MLXSW_REG_RAUHT_LEN];

	mlxsw_reg_rauht_pack4(rauht_pl, op, neigh_entry->rif, neigh_entry->ha,
			      dip);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rauht), rauht_pl);
}

static void
mlxsw_sp_neigh_entry_update(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_neigh_entry *neigh_entry,
			    bool adding)
{
	if (!adding && !neigh_entry->connected)
		return;
	neigh_entry->connected = adding;
	if (neigh_entry->key.n->tbl == &arp_tbl)
		mlxsw_sp_router_neigh_entry_op4(mlxsw_sp, neigh_entry,
						mlxsw_sp_rauht_op(adding));
	else
		WARN_ON_ONCE(1);
}

struct mlxsw_sp_neigh_event_work {
	struct work_struct work;
	struct mlxsw_sp *mlxsw_sp;
	struct neighbour *n;
};

static void mlxsw_sp_router_neigh_event_work(struct work_struct *work)
{
	struct mlxsw_sp_neigh_event_work *neigh_work =
		container_of(work, struct mlxsw_sp_neigh_event_work, work);
	struct mlxsw_sp *mlxsw_sp = neigh_work->mlxsw_sp;
	struct mlxsw_sp_neigh_entry *neigh_entry;
	struct neighbour *n = neigh_work->n;
	unsigned char ha[ETH_ALEN];
	bool entry_connected;
	u8 nud_state, dead;

	/* If these parameters are changed after we release the lock,
	 * then we are guaranteed to receive another event letting us
	 * know about it.
	 */
	read_lock_bh(&n->lock);
	memcpy(ha, n->ha, ETH_ALEN);
	nud_state = n->nud_state;
	dead = n->dead;
	read_unlock_bh(&n->lock);

	rtnl_lock();
	entry_connected = nud_state & NUD_VALID && !dead;
	neigh_entry = mlxsw_sp_neigh_entry_lookup(mlxsw_sp, n);
	if (!entry_connected && !neigh_entry)
		goto out;
	if (!neigh_entry) {
		neigh_entry = mlxsw_sp_neigh_entry_create(mlxsw_sp, n);
		if (IS_ERR(neigh_entry))
			goto out;
	}

	memcpy(neigh_entry->ha, ha, ETH_ALEN);
	mlxsw_sp_neigh_entry_update(mlxsw_sp, neigh_entry, entry_connected);
	mlxsw_sp_nexthop_neigh_update(mlxsw_sp, neigh_entry, !entry_connected);

	if (!neigh_entry->connected && list_empty(&neigh_entry->nexthop_list))
		mlxsw_sp_neigh_entry_destroy(mlxsw_sp, neigh_entry);

out:
	rtnl_unlock();
	neigh_release(n);
	kfree(neigh_work);
}

int mlxsw_sp_router_netevent_event(struct notifier_block *unused,
				   unsigned long event, void *ptr)
{
	struct mlxsw_sp_neigh_event_work *neigh_work;
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct mlxsw_sp *mlxsw_sp;
	unsigned long interval;
	struct neigh_parms *p;
	struct neighbour *n;

	switch (event) {
	case NETEVENT_DELAY_PROBE_TIME_UPDATE:
		p = ptr;

		/* We don't care about changes in the default table. */
		if (!p->dev || p->tbl != &arp_tbl)
			return NOTIFY_DONE;

		/* We are in atomic context and can't take RTNL mutex,
		 * so use RCU variant to walk the device chain.
		 */
		mlxsw_sp_port = mlxsw_sp_port_lower_dev_hold(p->dev);
		if (!mlxsw_sp_port)
			return NOTIFY_DONE;

		mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
		interval = jiffies_to_msecs(NEIGH_VAR(p, DELAY_PROBE_TIME));
		mlxsw_sp->router.neighs_update.interval = interval;

		mlxsw_sp_port_dev_put(mlxsw_sp_port);
		break;
	case NETEVENT_NEIGH_UPDATE:
		n = ptr;

		if (n->tbl != &arp_tbl)
			return NOTIFY_DONE;

		mlxsw_sp_port = mlxsw_sp_port_lower_dev_hold(n->dev);
		if (!mlxsw_sp_port)
			return NOTIFY_DONE;

		neigh_work = kzalloc(sizeof(*neigh_work), GFP_ATOMIC);
		if (!neigh_work) {
			mlxsw_sp_port_dev_put(mlxsw_sp_port);
			return NOTIFY_BAD;
		}

		INIT_WORK(&neigh_work->work, mlxsw_sp_router_neigh_event_work);
		neigh_work->mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
		neigh_work->n = n;

		/* Take a reference to ensure the neighbour won't be
		 * destructed until we drop the reference in delayed
		 * work.
		 */
		neigh_clone(n);
		mlxsw_core_schedule_work(&neigh_work->work);
		mlxsw_sp_port_dev_put(mlxsw_sp_port);
		break;
	}

	return NOTIFY_DONE;
}

static int mlxsw_sp_neigh_init(struct mlxsw_sp *mlxsw_sp)
{
	int err;

	err = rhashtable_init(&mlxsw_sp->router.neigh_ht,
			      &mlxsw_sp_neigh_ht_params);
	if (err)
		return err;

	/* Initialize the polling interval according to the default
	 * table.
	 */
	mlxsw_sp_router_neighs_update_interval_init(mlxsw_sp);

	/* Create the delayed works for the activity_update */
	INIT_DELAYED_WORK(&mlxsw_sp->router.neighs_update.dw,
			  mlxsw_sp_router_neighs_update_work);
	INIT_DELAYED_WORK(&mlxsw_sp->router.nexthop_probe_dw,
			  mlxsw_sp_router_probe_unresolved_nexthops);
	mlxsw_core_schedule_dw(&mlxsw_sp->router.neighs_update.dw, 0);
	mlxsw_core_schedule_dw(&mlxsw_sp->router.nexthop_probe_dw, 0);
	return 0;
}

static void mlxsw_sp_neigh_fini(struct mlxsw_sp *mlxsw_sp)
{
	cancel_delayed_work_sync(&mlxsw_sp->router.neighs_update.dw);
	cancel_delayed_work_sync(&mlxsw_sp->router.nexthop_probe_dw);
	rhashtable_destroy(&mlxsw_sp->router.neigh_ht);
}

static int mlxsw_sp_neigh_rif_flush(struct mlxsw_sp *mlxsw_sp,
				    const struct mlxsw_sp_rif *r)
{
	char rauht_pl[MLXSW_REG_RAUHT_LEN];

	mlxsw_reg_rauht_pack(rauht_pl, MLXSW_REG_RAUHT_OP_WRITE_DELETE_ALL,
			     r->rif, r->addr);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rauht), rauht_pl);
}

static void mlxsw_sp_neigh_rif_gone_sync(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_rif *r)
{
	struct mlxsw_sp_neigh_entry *neigh_entry, *tmp;

	mlxsw_sp_neigh_rif_flush(mlxsw_sp, r);
	list_for_each_entry_safe(neigh_entry, tmp, &r->neigh_list,
				 rif_list_node)
		mlxsw_sp_neigh_entry_destroy(mlxsw_sp, neigh_entry);
}

struct mlxsw_sp_nexthop_key {
	struct fib_nh *fib_nh;
};

struct mlxsw_sp_nexthop {
	struct list_head neigh_list_node; /* member of neigh entry list */
	struct list_head rif_list_node;
	struct mlxsw_sp_nexthop_group *nh_grp; /* pointer back to the group
						* this belongs to
						*/
	struct rhash_head ht_node;
	struct mlxsw_sp_nexthop_key key;
	struct mlxsw_sp_rif *r;
	u8 should_offload:1, /* set indicates this neigh is connected and
			      * should be put to KVD linear area of this group.
			      */
	   offloaded:1, /* set in case the neigh is actually put into
			 * KVD linear area of this group.
			 */
	   update:1; /* set indicates that MAC of this neigh should be
		      * updated in HW
		      */
	struct mlxsw_sp_neigh_entry *neigh_entry;
};

struct mlxsw_sp_nexthop_group_key {
	struct fib_info *fi;
};

struct mlxsw_sp_nexthop_group {
	struct rhash_head ht_node;
	struct list_head fib_list; /* list of fib entries that use this group */
	struct mlxsw_sp_nexthop_group_key key;
	u8 adj_index_valid:1,
	   gateway:1; /* routes using the group use a gateway */
	u32 adj_index;
	u16 ecmp_size;
	u16 count;
	struct mlxsw_sp_nexthop nexthops[0];
#define nh_rif	nexthops[0].r
};

static const struct rhashtable_params mlxsw_sp_nexthop_group_ht_params = {
	.key_offset = offsetof(struct mlxsw_sp_nexthop_group, key),
	.head_offset = offsetof(struct mlxsw_sp_nexthop_group, ht_node),
	.key_len = sizeof(struct mlxsw_sp_nexthop_group_key),
};

static int mlxsw_sp_nexthop_group_insert(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_nexthop_group *nh_grp)
{
	return rhashtable_insert_fast(&mlxsw_sp->router.nexthop_group_ht,
				      &nh_grp->ht_node,
				      mlxsw_sp_nexthop_group_ht_params);
}

static void mlxsw_sp_nexthop_group_remove(struct mlxsw_sp *mlxsw_sp,
					  struct mlxsw_sp_nexthop_group *nh_grp)
{
	rhashtable_remove_fast(&mlxsw_sp->router.nexthop_group_ht,
			       &nh_grp->ht_node,
			       mlxsw_sp_nexthop_group_ht_params);
}

static struct mlxsw_sp_nexthop_group *
mlxsw_sp_nexthop_group_lookup(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_nexthop_group_key key)
{
	return rhashtable_lookup_fast(&mlxsw_sp->router.nexthop_group_ht, &key,
				      mlxsw_sp_nexthop_group_ht_params);
}

static const struct rhashtable_params mlxsw_sp_nexthop_ht_params = {
	.key_offset = offsetof(struct mlxsw_sp_nexthop, key),
	.head_offset = offsetof(struct mlxsw_sp_nexthop, ht_node),
	.key_len = sizeof(struct mlxsw_sp_nexthop_key),
};

static int mlxsw_sp_nexthop_insert(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_nexthop *nh)
{
	return rhashtable_insert_fast(&mlxsw_sp->router.nexthop_ht,
				      &nh->ht_node, mlxsw_sp_nexthop_ht_params);
}

static void mlxsw_sp_nexthop_remove(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_nexthop *nh)
{
	rhashtable_remove_fast(&mlxsw_sp->router.nexthop_ht, &nh->ht_node,
			       mlxsw_sp_nexthop_ht_params);
}

static struct mlxsw_sp_nexthop *
mlxsw_sp_nexthop_lookup(struct mlxsw_sp *mlxsw_sp,
			struct mlxsw_sp_nexthop_key key)
{
	return rhashtable_lookup_fast(&mlxsw_sp->router.nexthop_ht, &key,
				      mlxsw_sp_nexthop_ht_params);
}

static int mlxsw_sp_adj_index_mass_update_vr(struct mlxsw_sp *mlxsw_sp,
					     struct mlxsw_sp_vr *vr,
					     u32 adj_index, u16 ecmp_size,
					     u32 new_adj_index,
					     u16 new_ecmp_size)
{
	char raleu_pl[MLXSW_REG_RALEU_LEN];

	mlxsw_reg_raleu_pack(raleu_pl,
			     (enum mlxsw_reg_ralxx_protocol) vr->proto, vr->id,
			     adj_index, ecmp_size, new_adj_index,
			     new_ecmp_size);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(raleu), raleu_pl);
}

static int mlxsw_sp_adj_index_mass_update(struct mlxsw_sp *mlxsw_sp,
					  struct mlxsw_sp_nexthop_group *nh_grp,
					  u32 old_adj_index, u16 old_ecmp_size)
{
	struct mlxsw_sp_fib_entry *fib_entry;
	struct mlxsw_sp_vr *vr = NULL;
	int err;

	list_for_each_entry(fib_entry, &nh_grp->fib_list, nexthop_group_node) {
		if (vr == fib_entry->fib_node->vr)
			continue;
		vr = fib_entry->fib_node->vr;
		err = mlxsw_sp_adj_index_mass_update_vr(mlxsw_sp, vr,
							old_adj_index,
							old_ecmp_size,
							nh_grp->adj_index,
							nh_grp->ecmp_size);
		if (err)
			return err;
	}
	return 0;
}

static int mlxsw_sp_nexthop_mac_update(struct mlxsw_sp *mlxsw_sp, u32 adj_index,
				       struct mlxsw_sp_nexthop *nh)
{
	struct mlxsw_sp_neigh_entry *neigh_entry = nh->neigh_entry;
	char ratr_pl[MLXSW_REG_RATR_LEN];

	mlxsw_reg_ratr_pack(ratr_pl, MLXSW_REG_RATR_OP_WRITE_WRITE_ENTRY,
			    true, adj_index, neigh_entry->rif);
	mlxsw_reg_ratr_eth_entry_pack(ratr_pl, neigh_entry->ha);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ratr), ratr_pl);
}

static int
mlxsw_sp_nexthop_group_mac_update(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_nexthop_group *nh_grp,
				  bool reallocate)
{
	u32 adj_index = nh_grp->adj_index; /* base */
	struct mlxsw_sp_nexthop *nh;
	int i;
	int err;

	for (i = 0; i < nh_grp->count; i++) {
		nh = &nh_grp->nexthops[i];

		if (!nh->should_offload) {
			nh->offloaded = 0;
			continue;
		}

		if (nh->update || reallocate) {
			err = mlxsw_sp_nexthop_mac_update(mlxsw_sp,
							  adj_index, nh);
			if (err)
				return err;
			nh->update = 0;
			nh->offloaded = 1;
		}
		adj_index++;
	}
	return 0;
}

static int mlxsw_sp_fib_entry_update(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_fib_entry *fib_entry);

static int
mlxsw_sp_nexthop_fib_entries_update(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_nexthop_group *nh_grp)
{
	struct mlxsw_sp_fib_entry *fib_entry;
	int err;

	list_for_each_entry(fib_entry, &nh_grp->fib_list, nexthop_group_node) {
		err = mlxsw_sp_fib_entry_update(mlxsw_sp, fib_entry);
		if (err)
			return err;
	}
	return 0;
}

static void
mlxsw_sp_nexthop_group_refresh(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_nexthop_group *nh_grp)
{
	struct mlxsw_sp_nexthop *nh;
	bool offload_change = false;
	u32 adj_index;
	u16 ecmp_size = 0;
	bool old_adj_index_valid;
	u32 old_adj_index;
	u16 old_ecmp_size;
	int ret;
	int i;
	int err;

	if (!nh_grp->gateway) {
		mlxsw_sp_nexthop_fib_entries_update(mlxsw_sp, nh_grp);
		return;
	}

	for (i = 0; i < nh_grp->count; i++) {
		nh = &nh_grp->nexthops[i];

		if (nh->should_offload ^ nh->offloaded) {
			offload_change = true;
			if (nh->should_offload)
				nh->update = 1;
		}
		if (nh->should_offload)
			ecmp_size++;
	}
	if (!offload_change) {
		/* Nothing was added or removed, so no need to reallocate. Just
		 * update MAC on existing adjacency indexes.
		 */
		err = mlxsw_sp_nexthop_group_mac_update(mlxsw_sp, nh_grp,
							false);
		if (err) {
			dev_warn(mlxsw_sp->bus_info->dev, "Failed to update neigh MAC in adjacency table.\n");
			goto set_trap;
		}
		return;
	}
	if (!ecmp_size)
		/* No neigh of this group is connected so we just set
		 * the trap and let everthing flow through kernel.
		 */
		goto set_trap;

	ret = mlxsw_sp_kvdl_alloc(mlxsw_sp, ecmp_size);
	if (ret < 0) {
		/* We ran out of KVD linear space, just set the
		 * trap and let everything flow through kernel.
		 */
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to allocate KVD linear area for nexthop group.\n");
		goto set_trap;
	}
	adj_index = ret;
	old_adj_index_valid = nh_grp->adj_index_valid;
	old_adj_index = nh_grp->adj_index;
	old_ecmp_size = nh_grp->ecmp_size;
	nh_grp->adj_index_valid = 1;
	nh_grp->adj_index = adj_index;
	nh_grp->ecmp_size = ecmp_size;
	err = mlxsw_sp_nexthop_group_mac_update(mlxsw_sp, nh_grp, true);
	if (err) {
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to update neigh MAC in adjacency table.\n");
		goto set_trap;
	}

	if (!old_adj_index_valid) {
		/* The trap was set for fib entries, so we have to call
		 * fib entry update to unset it and use adjacency index.
		 */
		err = mlxsw_sp_nexthop_fib_entries_update(mlxsw_sp, nh_grp);
		if (err) {
			dev_warn(mlxsw_sp->bus_info->dev, "Failed to add adjacency index to fib entries.\n");
			goto set_trap;
		}
		return;
	}

	err = mlxsw_sp_adj_index_mass_update(mlxsw_sp, nh_grp,
					     old_adj_index, old_ecmp_size);
	mlxsw_sp_kvdl_free(mlxsw_sp, old_adj_index);
	if (err) {
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to mass-update adjacency index for nexthop group.\n");
		goto set_trap;
	}
	return;

set_trap:
	old_adj_index_valid = nh_grp->adj_index_valid;
	nh_grp->adj_index_valid = 0;
	for (i = 0; i < nh_grp->count; i++) {
		nh = &nh_grp->nexthops[i];
		nh->offloaded = 0;
	}
	err = mlxsw_sp_nexthop_fib_entries_update(mlxsw_sp, nh_grp);
	if (err)
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to set traps for fib entries.\n");
	if (old_adj_index_valid)
		mlxsw_sp_kvdl_free(mlxsw_sp, nh_grp->adj_index);
}

static void __mlxsw_sp_nexthop_neigh_update(struct mlxsw_sp_nexthop *nh,
					    bool removing)
{
	if (!removing && !nh->should_offload)
		nh->should_offload = 1;
	else if (removing && nh->offloaded)
		nh->should_offload = 0;
	nh->update = 1;
}

static void
mlxsw_sp_nexthop_neigh_update(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_neigh_entry *neigh_entry,
			      bool removing)
{
	struct mlxsw_sp_nexthop *nh;

	list_for_each_entry(nh, &neigh_entry->nexthop_list,
			    neigh_list_node) {
		__mlxsw_sp_nexthop_neigh_update(nh, removing);
		mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh->nh_grp);
	}
}

static void mlxsw_sp_nexthop_rif_init(struct mlxsw_sp_nexthop *nh,
				      struct mlxsw_sp_rif *r)
{
	if (nh->r)
		return;

	nh->r = r;
	list_add(&nh->rif_list_node, &r->nexthop_list);
}

static void mlxsw_sp_nexthop_rif_fini(struct mlxsw_sp_nexthop *nh)
{
	if (!nh->r)
		return;

	list_del(&nh->rif_list_node);
	nh->r = NULL;
}

static int mlxsw_sp_nexthop_neigh_init(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_nexthop *nh)
{
	struct mlxsw_sp_neigh_entry *neigh_entry;
	struct fib_nh *fib_nh = nh->key.fib_nh;
	struct neighbour *n;
	u8 nud_state, dead;
	int err;

	if (!nh->nh_grp->gateway || nh->neigh_entry)
		return 0;

	/* Take a reference of neigh here ensuring that neigh would
	 * not be detructed before the nexthop entry is finished.
	 * The reference is taken either in neigh_lookup() or
	 * in neigh_create() in case n is not found.
	 */
	n = neigh_lookup(&arp_tbl, &fib_nh->nh_gw, fib_nh->nh_dev);
	if (!n) {
		n = neigh_create(&arp_tbl, &fib_nh->nh_gw, fib_nh->nh_dev);
		if (IS_ERR(n))
			return PTR_ERR(n);
		neigh_event_send(n, NULL);
	}
	neigh_entry = mlxsw_sp_neigh_entry_lookup(mlxsw_sp, n);
	if (!neigh_entry) {
		neigh_entry = mlxsw_sp_neigh_entry_create(mlxsw_sp, n);
		if (IS_ERR(neigh_entry)) {
			err = -EINVAL;
			goto err_neigh_entry_create;
		}
	}

	/* If that is the first nexthop connected to that neigh, add to
	 * nexthop_neighs_list
	 */
	if (list_empty(&neigh_entry->nexthop_list))
		list_add_tail(&neigh_entry->nexthop_neighs_list_node,
			      &mlxsw_sp->router.nexthop_neighs_list);

	nh->neigh_entry = neigh_entry;
	list_add_tail(&nh->neigh_list_node, &neigh_entry->nexthop_list);
	read_lock_bh(&n->lock);
	nud_state = n->nud_state;
	dead = n->dead;
	read_unlock_bh(&n->lock);
	__mlxsw_sp_nexthop_neigh_update(nh, !(nud_state & NUD_VALID && !dead));

	return 0;

err_neigh_entry_create:
	neigh_release(n);
	return err;
}

static void mlxsw_sp_nexthop_neigh_fini(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_nexthop *nh)
{
	struct mlxsw_sp_neigh_entry *neigh_entry = nh->neigh_entry;
	struct neighbour *n;

	if (!neigh_entry)
		return;
	n = neigh_entry->key.n;

	__mlxsw_sp_nexthop_neigh_update(nh, true);
	list_del(&nh->neigh_list_node);
	nh->neigh_entry = NULL;

	/* If that is the last nexthop connected to that neigh, remove from
	 * nexthop_neighs_list
	 */
	if (list_empty(&neigh_entry->nexthop_list))
		list_del(&neigh_entry->nexthop_neighs_list_node);

	if (!neigh_entry->connected && list_empty(&neigh_entry->nexthop_list))
		mlxsw_sp_neigh_entry_destroy(mlxsw_sp, neigh_entry);

	neigh_release(n);
}

static int mlxsw_sp_nexthop_init(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_nexthop_group *nh_grp,
				 struct mlxsw_sp_nexthop *nh,
				 struct fib_nh *fib_nh)
{
	struct net_device *dev = fib_nh->nh_dev;
	struct in_device *in_dev;
	struct mlxsw_sp_rif *r;
	int err;

	nh->nh_grp = nh_grp;
	nh->key.fib_nh = fib_nh;
	err = mlxsw_sp_nexthop_insert(mlxsw_sp, nh);
	if (err)
		return err;

	in_dev = __in_dev_get_rtnl(dev);
	if (in_dev && IN_DEV_IGNORE_ROUTES_WITH_LINKDOWN(in_dev) &&
	    fib_nh->nh_flags & RTNH_F_LINKDOWN)
		return 0;

	r = mlxsw_sp_rif_find_by_dev(mlxsw_sp, dev);
	if (!r)
		return 0;
	mlxsw_sp_nexthop_rif_init(nh, r);

	err = mlxsw_sp_nexthop_neigh_init(mlxsw_sp, nh);
	if (err)
		goto err_nexthop_neigh_init;

	return 0;

err_nexthop_neigh_init:
	mlxsw_sp_nexthop_remove(mlxsw_sp, nh);
	return err;
}

static void mlxsw_sp_nexthop_fini(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_nexthop *nh)
{
	mlxsw_sp_nexthop_neigh_fini(mlxsw_sp, nh);
	mlxsw_sp_nexthop_rif_fini(nh);
	mlxsw_sp_nexthop_remove(mlxsw_sp, nh);
}

static void mlxsw_sp_nexthop_event(struct mlxsw_sp *mlxsw_sp,
				   unsigned long event, struct fib_nh *fib_nh)
{
	struct mlxsw_sp_nexthop_key key;
	struct mlxsw_sp_nexthop *nh;
	struct mlxsw_sp_rif *r;

	if (mlxsw_sp->router.aborted)
		return;

	key.fib_nh = fib_nh;
	nh = mlxsw_sp_nexthop_lookup(mlxsw_sp, key);
	if (WARN_ON_ONCE(!nh))
		return;

	r = mlxsw_sp_rif_find_by_dev(mlxsw_sp, fib_nh->nh_dev);
	if (!r)
		return;

	switch (event) {
	case FIB_EVENT_NH_ADD:
		mlxsw_sp_nexthop_rif_init(nh, r);
		mlxsw_sp_nexthop_neigh_init(mlxsw_sp, nh);
		break;
	case FIB_EVENT_NH_DEL:
		mlxsw_sp_nexthop_neigh_fini(mlxsw_sp, nh);
		mlxsw_sp_nexthop_rif_fini(nh);
		break;
	}

	mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh->nh_grp);
}

static void mlxsw_sp_nexthop_rif_gone_sync(struct mlxsw_sp *mlxsw_sp,
					   struct mlxsw_sp_rif *r)
{
	struct mlxsw_sp_nexthop *nh, *tmp;

	list_for_each_entry_safe(nh, tmp, &r->nexthop_list, rif_list_node) {
		mlxsw_sp_nexthop_neigh_fini(mlxsw_sp, nh);
		mlxsw_sp_nexthop_rif_fini(nh);
		mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh->nh_grp);
	}
}

static struct mlxsw_sp_nexthop_group *
mlxsw_sp_nexthop_group_create(struct mlxsw_sp *mlxsw_sp, struct fib_info *fi)
{
	struct mlxsw_sp_nexthop_group *nh_grp;
	struct mlxsw_sp_nexthop *nh;
	struct fib_nh *fib_nh;
	size_t alloc_size;
	int i;
	int err;

	alloc_size = sizeof(*nh_grp) +
		     fi->fib_nhs * sizeof(struct mlxsw_sp_nexthop);
	nh_grp = kzalloc(alloc_size, GFP_KERNEL);
	if (!nh_grp)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&nh_grp->fib_list);
	nh_grp->gateway = fi->fib_nh->nh_scope == RT_SCOPE_LINK;
	nh_grp->count = fi->fib_nhs;
	nh_grp->key.fi = fi;
	for (i = 0; i < nh_grp->count; i++) {
		nh = &nh_grp->nexthops[i];
		fib_nh = &fi->fib_nh[i];
		err = mlxsw_sp_nexthop_init(mlxsw_sp, nh_grp, nh, fib_nh);
		if (err)
			goto err_nexthop_init;
	}
	err = mlxsw_sp_nexthop_group_insert(mlxsw_sp, nh_grp);
	if (err)
		goto err_nexthop_group_insert;
	mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh_grp);
	return nh_grp;

err_nexthop_group_insert:
err_nexthop_init:
	for (i--; i >= 0; i--) {
		nh = &nh_grp->nexthops[i];
		mlxsw_sp_nexthop_fini(mlxsw_sp, nh);
	}
	kfree(nh_grp);
	return ERR_PTR(err);
}

static void
mlxsw_sp_nexthop_group_destroy(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_nexthop_group *nh_grp)
{
	struct mlxsw_sp_nexthop *nh;
	int i;

	mlxsw_sp_nexthop_group_remove(mlxsw_sp, nh_grp);
	for (i = 0; i < nh_grp->count; i++) {
		nh = &nh_grp->nexthops[i];
		mlxsw_sp_nexthop_fini(mlxsw_sp, nh);
	}
	mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh_grp);
	WARN_ON_ONCE(nh_grp->adj_index_valid);
	kfree(nh_grp);
}

static int mlxsw_sp_nexthop_group_get(struct mlxsw_sp *mlxsw_sp,
				      struct mlxsw_sp_fib_entry *fib_entry,
				      struct fib_info *fi)
{
	struct mlxsw_sp_nexthop_group_key key;
	struct mlxsw_sp_nexthop_group *nh_grp;

	key.fi = fi;
	nh_grp = mlxsw_sp_nexthop_group_lookup(mlxsw_sp, key);
	if (!nh_grp) {
		nh_grp = mlxsw_sp_nexthop_group_create(mlxsw_sp, fi);
		if (IS_ERR(nh_grp))
			return PTR_ERR(nh_grp);
	}
	list_add_tail(&fib_entry->nexthop_group_node, &nh_grp->fib_list);
	fib_entry->nh_group = nh_grp;
	return 0;
}

static void mlxsw_sp_nexthop_group_put(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_fib_entry *fib_entry)
{
	struct mlxsw_sp_nexthop_group *nh_grp = fib_entry->nh_group;

	list_del(&fib_entry->nexthop_group_node);
	if (!list_empty(&nh_grp->fib_list))
		return;
	mlxsw_sp_nexthop_group_destroy(mlxsw_sp, nh_grp);
}

static bool
mlxsw_sp_fib_entry_should_offload(const struct mlxsw_sp_fib_entry *fib_entry)
{
	struct mlxsw_sp_nexthop_group *nh_group = fib_entry->nh_group;

	if (fib_entry->params.tos)
		return false;

	switch (fib_entry->type) {
	case MLXSW_SP_FIB_ENTRY_TYPE_REMOTE:
		return !!nh_group->adj_index_valid;
	case MLXSW_SP_FIB_ENTRY_TYPE_LOCAL:
		return !!nh_group->nh_rif;
	default:
		return false;
	}
}

static void mlxsw_sp_fib_entry_offload_set(struct mlxsw_sp_fib_entry *fib_entry)
{
	fib_entry->offloaded = true;

	switch (fib_entry->fib_node->vr->proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		fib_info_offload_inc(fib_entry->nh_group->key.fi);
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		WARN_ON_ONCE(1);
	}
}

static void
mlxsw_sp_fib_entry_offload_unset(struct mlxsw_sp_fib_entry *fib_entry)
{
	switch (fib_entry->fib_node->vr->proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		fib_info_offload_dec(fib_entry->nh_group->key.fi);
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		WARN_ON_ONCE(1);
	}

	fib_entry->offloaded = false;
}

static void
mlxsw_sp_fib_entry_offload_refresh(struct mlxsw_sp_fib_entry *fib_entry,
				   enum mlxsw_reg_ralue_op op, int err)
{
	switch (op) {
	case MLXSW_REG_RALUE_OP_WRITE_DELETE:
		if (!fib_entry->offloaded)
			return;
		return mlxsw_sp_fib_entry_offload_unset(fib_entry);
	case MLXSW_REG_RALUE_OP_WRITE_WRITE:
		if (err)
			return;
		if (mlxsw_sp_fib_entry_should_offload(fib_entry) &&
		    !fib_entry->offloaded)
			mlxsw_sp_fib_entry_offload_set(fib_entry);
		else if (!mlxsw_sp_fib_entry_should_offload(fib_entry) &&
			 fib_entry->offloaded)
			mlxsw_sp_fib_entry_offload_unset(fib_entry);
		return;
	default:
		return;
	}
}

static int mlxsw_sp_fib_entry_op4_remote(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_fib_entry *fib_entry,
					 enum mlxsw_reg_ralue_op op)
{
	char ralue_pl[MLXSW_REG_RALUE_LEN];
	u32 *p_dip = (u32 *) fib_entry->fib_node->key.addr;
	struct mlxsw_sp_vr *vr = fib_entry->fib_node->vr;
	enum mlxsw_reg_ralue_trap_action trap_action;
	u16 trap_id = 0;
	u32 adjacency_index = 0;
	u16 ecmp_size = 0;

	/* In case the nexthop group adjacency index is valid, use it
	 * with provided ECMP size. Otherwise, setup trap and pass
	 * traffic to kernel.
	 */
	if (mlxsw_sp_fib_entry_should_offload(fib_entry)) {
		trap_action = MLXSW_REG_RALUE_TRAP_ACTION_NOP;
		adjacency_index = fib_entry->nh_group->adj_index;
		ecmp_size = fib_entry->nh_group->ecmp_size;
	} else {
		trap_action = MLXSW_REG_RALUE_TRAP_ACTION_TRAP;
		trap_id = MLXSW_TRAP_ID_RTR_INGRESS0;
	}

	mlxsw_reg_ralue_pack4(ralue_pl,
			      (enum mlxsw_reg_ralxx_protocol) vr->proto, op,
			      vr->id, fib_entry->fib_node->key.prefix_len,
			      *p_dip);
	mlxsw_reg_ralue_act_remote_pack(ralue_pl, trap_action, trap_id,
					adjacency_index, ecmp_size);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralue), ralue_pl);
}

static int mlxsw_sp_fib_entry_op4_local(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_fib_entry *fib_entry,
					enum mlxsw_reg_ralue_op op)
{
	struct mlxsw_sp_rif *r = fib_entry->nh_group->nh_rif;
	enum mlxsw_reg_ralue_trap_action trap_action;
	char ralue_pl[MLXSW_REG_RALUE_LEN];
	u32 *p_dip = (u32 *) fib_entry->fib_node->key.addr;
	struct mlxsw_sp_vr *vr = fib_entry->fib_node->vr;
	u16 trap_id = 0;
	u16 rif = 0;

	if (mlxsw_sp_fib_entry_should_offload(fib_entry)) {
		trap_action = MLXSW_REG_RALUE_TRAP_ACTION_NOP;
		rif = r->rif;
	} else {
		trap_action = MLXSW_REG_RALUE_TRAP_ACTION_TRAP;
		trap_id = MLXSW_TRAP_ID_RTR_INGRESS0;
	}

	mlxsw_reg_ralue_pack4(ralue_pl,
			      (enum mlxsw_reg_ralxx_protocol) vr->proto, op,
			      vr->id, fib_entry->fib_node->key.prefix_len,
			      *p_dip);
	mlxsw_reg_ralue_act_local_pack(ralue_pl, trap_action, trap_id, rif);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralue), ralue_pl);
}

static int mlxsw_sp_fib_entry_op4_trap(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_fib_entry *fib_entry,
				       enum mlxsw_reg_ralue_op op)
{
	char ralue_pl[MLXSW_REG_RALUE_LEN];
	u32 *p_dip = (u32 *) fib_entry->fib_node->key.addr;
	struct mlxsw_sp_vr *vr = fib_entry->fib_node->vr;

	mlxsw_reg_ralue_pack4(ralue_pl,
			      (enum mlxsw_reg_ralxx_protocol) vr->proto, op,
			      vr->id, fib_entry->fib_node->key.prefix_len,
			      *p_dip);
	mlxsw_reg_ralue_act_ip2me_pack(ralue_pl);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralue), ralue_pl);
}

static int mlxsw_sp_fib_entry_op4(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_fib_entry *fib_entry,
				  enum mlxsw_reg_ralue_op op)
{
	switch (fib_entry->type) {
	case MLXSW_SP_FIB_ENTRY_TYPE_REMOTE:
		return mlxsw_sp_fib_entry_op4_remote(mlxsw_sp, fib_entry, op);
	case MLXSW_SP_FIB_ENTRY_TYPE_LOCAL:
		return mlxsw_sp_fib_entry_op4_local(mlxsw_sp, fib_entry, op);
	case MLXSW_SP_FIB_ENTRY_TYPE_TRAP:
		return mlxsw_sp_fib_entry_op4_trap(mlxsw_sp, fib_entry, op);
	}
	return -EINVAL;
}

static int mlxsw_sp_fib_entry_op(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_fib_entry *fib_entry,
				 enum mlxsw_reg_ralue_op op)
{
	int err = -EINVAL;

	switch (fib_entry->fib_node->vr->proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		err = mlxsw_sp_fib_entry_op4(mlxsw_sp, fib_entry, op);
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		return err;
	}
	mlxsw_sp_fib_entry_offload_refresh(fib_entry, op, err);
	return err;
}

static int mlxsw_sp_fib_entry_update(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_fib_entry *fib_entry)
{
	return mlxsw_sp_fib_entry_op(mlxsw_sp, fib_entry,
				     MLXSW_REG_RALUE_OP_WRITE_WRITE);
}

static int mlxsw_sp_fib_entry_del(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_fib_entry *fib_entry)
{
	return mlxsw_sp_fib_entry_op(mlxsw_sp, fib_entry,
				     MLXSW_REG_RALUE_OP_WRITE_DELETE);
}

static int
mlxsw_sp_fib4_entry_type_set(struct mlxsw_sp *mlxsw_sp,
			     const struct fib_entry_notifier_info *fen_info,
			     struct mlxsw_sp_fib_entry *fib_entry)
{
	struct fib_info *fi = fen_info->fi;

	if (fen_info->type == RTN_LOCAL || fen_info->type == RTN_BROADCAST) {
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_TRAP;
		return 0;
	}
	if (fen_info->type != RTN_UNICAST)
		return -EINVAL;
	if (fi->fib_nh->nh_scope != RT_SCOPE_LINK)
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_LOCAL;
	else
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_REMOTE;
	return 0;
}

static struct mlxsw_sp_fib_entry *
mlxsw_sp_fib4_entry_create(struct mlxsw_sp *mlxsw_sp,
			   struct mlxsw_sp_fib_node *fib_node,
			   const struct fib_entry_notifier_info *fen_info)
{
	struct mlxsw_sp_fib_entry *fib_entry;
	int err;

	fib_entry = kzalloc(sizeof(*fib_entry), GFP_KERNEL);
	if (!fib_entry) {
		err = -ENOMEM;
		goto err_fib_entry_alloc;
	}

	err = mlxsw_sp_fib4_entry_type_set(mlxsw_sp, fen_info, fib_entry);
	if (err)
		goto err_fib4_entry_type_set;

	err = mlxsw_sp_nexthop_group_get(mlxsw_sp, fib_entry, fen_info->fi);
	if (err)
		goto err_nexthop_group_get;

	fib_entry->params.prio = fen_info->fi->fib_priority;
	fib_entry->params.tb_id = fen_info->tb_id;
	fib_entry->params.type = fen_info->type;
	fib_entry->params.tos = fen_info->tos;

	fib_entry->fib_node = fib_node;

	return fib_entry;

err_nexthop_group_get:
err_fib4_entry_type_set:
	kfree(fib_entry);
err_fib_entry_alloc:
	return ERR_PTR(err);
}

static void mlxsw_sp_fib4_entry_destroy(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_fib_entry *fib_entry)
{
	mlxsw_sp_nexthop_group_put(mlxsw_sp, fib_entry);
	kfree(fib_entry);
}

static struct mlxsw_sp_fib_node *
mlxsw_sp_fib4_node_get(struct mlxsw_sp *mlxsw_sp,
		       const struct fib_entry_notifier_info *fen_info);

static struct mlxsw_sp_fib_entry *
mlxsw_sp_fib4_entry_lookup(struct mlxsw_sp *mlxsw_sp,
			   const struct fib_entry_notifier_info *fen_info)
{
	struct mlxsw_sp_fib_entry *fib_entry;
	struct mlxsw_sp_fib_node *fib_node;

	fib_node = mlxsw_sp_fib4_node_get(mlxsw_sp, fen_info);
	if (IS_ERR(fib_node))
		return NULL;

	list_for_each_entry(fib_entry, &fib_node->entry_list, list) {
		if (fib_entry->params.tb_id == fen_info->tb_id &&
		    fib_entry->params.tos == fen_info->tos &&
		    fib_entry->params.type == fen_info->type &&
		    fib_entry->nh_group->key.fi == fen_info->fi) {
			return fib_entry;
		}
	}

	return NULL;
}

static const struct rhashtable_params mlxsw_sp_fib_ht_params = {
	.key_offset = offsetof(struct mlxsw_sp_fib_node, key),
	.head_offset = offsetof(struct mlxsw_sp_fib_node, ht_node),
	.key_len = sizeof(struct mlxsw_sp_fib_key),
	.automatic_shrinking = true,
};

static int mlxsw_sp_fib_node_insert(struct mlxsw_sp_fib *fib,
				    struct mlxsw_sp_fib_node *fib_node)
{
	return rhashtable_insert_fast(&fib->ht, &fib_node->ht_node,
				      mlxsw_sp_fib_ht_params);
}

static void mlxsw_sp_fib_node_remove(struct mlxsw_sp_fib *fib,
				     struct mlxsw_sp_fib_node *fib_node)
{
	rhashtable_remove_fast(&fib->ht, &fib_node->ht_node,
			       mlxsw_sp_fib_ht_params);
}

static struct mlxsw_sp_fib_node *
mlxsw_sp_fib_node_lookup(struct mlxsw_sp_fib *fib, const void *addr,
			 size_t addr_len, unsigned char prefix_len)
{
	struct mlxsw_sp_fib_key key;

	memset(&key, 0, sizeof(key));
	memcpy(key.addr, addr, addr_len);
	key.prefix_len = prefix_len;
	return rhashtable_lookup_fast(&fib->ht, &key, mlxsw_sp_fib_ht_params);
}

static struct mlxsw_sp_fib_node *
mlxsw_sp_fib_node_create(struct mlxsw_sp_vr *vr, const void *addr,
			 size_t addr_len, unsigned char prefix_len)
{
	struct mlxsw_sp_fib_node *fib_node;

	fib_node = kzalloc(sizeof(*fib_node), GFP_KERNEL);
	if (!fib_node)
		return NULL;

	INIT_LIST_HEAD(&fib_node->entry_list);
	list_add(&fib_node->list, &vr->fib->node_list);
	memcpy(fib_node->key.addr, addr, addr_len);
	fib_node->key.prefix_len = prefix_len;
	mlxsw_sp_fib_node_insert(vr->fib, fib_node);
	fib_node->vr = vr;

	return fib_node;
}

static void mlxsw_sp_fib_node_destroy(struct mlxsw_sp_fib_node *fib_node)
{
	mlxsw_sp_fib_node_remove(fib_node->vr->fib, fib_node);
	list_del(&fib_node->list);
	WARN_ON(!list_empty(&fib_node->entry_list));
	kfree(fib_node);
}

static bool
mlxsw_sp_fib_node_entry_is_first(const struct mlxsw_sp_fib_node *fib_node,
				 const struct mlxsw_sp_fib_entry *fib_entry)
{
	return list_first_entry(&fib_node->entry_list,
				struct mlxsw_sp_fib_entry, list) == fib_entry;
}

static void mlxsw_sp_fib_node_prefix_inc(struct mlxsw_sp_fib_node *fib_node)
{
	unsigned char prefix_len = fib_node->key.prefix_len;
	struct mlxsw_sp_fib *fib = fib_node->vr->fib;

	if (fib->prefix_ref_count[prefix_len]++ == 0)
		mlxsw_sp_prefix_usage_set(&fib->prefix_usage, prefix_len);
}

static void mlxsw_sp_fib_node_prefix_dec(struct mlxsw_sp_fib_node *fib_node)
{
	unsigned char prefix_len = fib_node->key.prefix_len;
	struct mlxsw_sp_fib *fib = fib_node->vr->fib;

	if (--fib->prefix_ref_count[prefix_len] == 0)
		mlxsw_sp_prefix_usage_clear(&fib->prefix_usage, prefix_len);
}

static struct mlxsw_sp_fib_node *
mlxsw_sp_fib4_node_get(struct mlxsw_sp *mlxsw_sp,
		       const struct fib_entry_notifier_info *fen_info)
{
	struct mlxsw_sp_fib_node *fib_node;
	struct mlxsw_sp_vr *vr;
	int err;

	vr = mlxsw_sp_vr_get(mlxsw_sp, fen_info->dst_len, fen_info->tb_id,
			     MLXSW_SP_L3_PROTO_IPV4);
	if (IS_ERR(vr))
		return ERR_CAST(vr);

	fib_node = mlxsw_sp_fib_node_lookup(vr->fib, &fen_info->dst,
					    sizeof(fen_info->dst),
					    fen_info->dst_len);
	if (fib_node)
		return fib_node;

	fib_node = mlxsw_sp_fib_node_create(vr, &fen_info->dst,
					    sizeof(fen_info->dst),
					    fen_info->dst_len);
	if (!fib_node) {
		err = -ENOMEM;
		goto err_fib_node_create;
	}

	return fib_node;

err_fib_node_create:
	mlxsw_sp_vr_put(mlxsw_sp, vr);
	return ERR_PTR(err);
}

static void mlxsw_sp_fib4_node_put(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_fib_node *fib_node)
{
	struct mlxsw_sp_vr *vr = fib_node->vr;

	if (!list_empty(&fib_node->entry_list))
		return;
	mlxsw_sp_fib_node_destroy(fib_node);
	mlxsw_sp_vr_put(mlxsw_sp, vr);
}

static struct mlxsw_sp_fib_entry *
mlxsw_sp_fib4_node_entry_find(const struct mlxsw_sp_fib_node *fib_node,
			      const struct mlxsw_sp_fib_entry_params *params)
{
	struct mlxsw_sp_fib_entry *fib_entry;

	list_for_each_entry(fib_entry, &fib_node->entry_list, list) {
		if (fib_entry->params.tb_id > params->tb_id)
			continue;
		if (fib_entry->params.tb_id != params->tb_id)
			break;
		if (fib_entry->params.tos > params->tos)
			continue;
		if (fib_entry->params.prio >= params->prio ||
		    fib_entry->params.tos < params->tos)
			return fib_entry;
	}

	return NULL;
}

static int mlxsw_sp_fib4_node_list_append(struct mlxsw_sp_fib_entry *fib_entry,
					  struct mlxsw_sp_fib_entry *new_entry)
{
	struct mlxsw_sp_fib_node *fib_node;

	if (WARN_ON(!fib_entry))
		return -EINVAL;

	fib_node = fib_entry->fib_node;
	list_for_each_entry_from(fib_entry, &fib_node->entry_list, list) {
		if (fib_entry->params.tb_id != new_entry->params.tb_id ||
		    fib_entry->params.tos != new_entry->params.tos ||
		    fib_entry->params.prio != new_entry->params.prio)
			break;
	}

	list_add_tail(&new_entry->list, &fib_entry->list);
	return 0;
}

static int
mlxsw_sp_fib4_node_list_insert(struct mlxsw_sp_fib_node *fib_node,
			       struct mlxsw_sp_fib_entry *new_entry,
			       bool replace, bool append)
{
	struct mlxsw_sp_fib_entry *fib_entry;

	fib_entry = mlxsw_sp_fib4_node_entry_find(fib_node, &new_entry->params);

	if (append)
		return mlxsw_sp_fib4_node_list_append(fib_entry, new_entry);
	if (replace && WARN_ON(!fib_entry))
		return -EINVAL;

	/* Insert new entry before replaced one, so that we can later
	 * remove the second.
	 */
	if (fib_entry) {
		list_add_tail(&new_entry->list, &fib_entry->list);
	} else {
		struct mlxsw_sp_fib_entry *last;

		list_for_each_entry(last, &fib_node->entry_list, list) {
			if (new_entry->params.tb_id > last->params.tb_id)
				break;
			fib_entry = last;
		}

		if (fib_entry)
			list_add(&new_entry->list, &fib_entry->list);
		else
			list_add(&new_entry->list, &fib_node->entry_list);
	}

	return 0;
}

static void
mlxsw_sp_fib4_node_list_remove(struct mlxsw_sp_fib_entry *fib_entry)
{
	list_del(&fib_entry->list);
}

static int
mlxsw_sp_fib4_node_entry_add(struct mlxsw_sp *mlxsw_sp,
			     const struct mlxsw_sp_fib_node *fib_node,
			     struct mlxsw_sp_fib_entry *fib_entry)
{
	if (!mlxsw_sp_fib_node_entry_is_first(fib_node, fib_entry))
		return 0;

	/* To prevent packet loss, overwrite the previously offloaded
	 * entry.
	 */
	if (!list_is_singular(&fib_node->entry_list)) {
		enum mlxsw_reg_ralue_op op = MLXSW_REG_RALUE_OP_WRITE_DELETE;
		struct mlxsw_sp_fib_entry *n = list_next_entry(fib_entry, list);

		mlxsw_sp_fib_entry_offload_refresh(n, op, 0);
	}

	return mlxsw_sp_fib_entry_update(mlxsw_sp, fib_entry);
}

static void
mlxsw_sp_fib4_node_entry_del(struct mlxsw_sp *mlxsw_sp,
			     const struct mlxsw_sp_fib_node *fib_node,
			     struct mlxsw_sp_fib_entry *fib_entry)
{
	if (!mlxsw_sp_fib_node_entry_is_first(fib_node, fib_entry))
		return;

	/* Promote the next entry by overwriting the deleted entry */
	if (!list_is_singular(&fib_node->entry_list)) {
		struct mlxsw_sp_fib_entry *n = list_next_entry(fib_entry, list);
		enum mlxsw_reg_ralue_op op = MLXSW_REG_RALUE_OP_WRITE_DELETE;

		mlxsw_sp_fib_entry_update(mlxsw_sp, n);
		mlxsw_sp_fib_entry_offload_refresh(fib_entry, op, 0);
		return;
	}

	mlxsw_sp_fib_entry_del(mlxsw_sp, fib_entry);
}

static int mlxsw_sp_fib4_node_entry_link(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_fib_entry *fib_entry,
					 bool replace, bool append)
{
	struct mlxsw_sp_fib_node *fib_node = fib_entry->fib_node;
	int err;

	err = mlxsw_sp_fib4_node_list_insert(fib_node, fib_entry, replace,
					     append);
	if (err)
		return err;

	err = mlxsw_sp_fib4_node_entry_add(mlxsw_sp, fib_node, fib_entry);
	if (err)
		goto err_fib4_node_entry_add;

	mlxsw_sp_fib_node_prefix_inc(fib_node);

	return 0;

err_fib4_node_entry_add:
	mlxsw_sp_fib4_node_list_remove(fib_entry);
	return err;
}

static void
mlxsw_sp_fib4_node_entry_unlink(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_fib_entry *fib_entry)
{
	struct mlxsw_sp_fib_node *fib_node = fib_entry->fib_node;

	mlxsw_sp_fib_node_prefix_dec(fib_node);
	mlxsw_sp_fib4_node_entry_del(mlxsw_sp, fib_node, fib_entry);
	mlxsw_sp_fib4_node_list_remove(fib_entry);
}

static void mlxsw_sp_fib4_entry_replace(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_fib_entry *fib_entry,
					bool replace)
{
	struct mlxsw_sp_fib_node *fib_node = fib_entry->fib_node;
	struct mlxsw_sp_fib_entry *replaced;

	if (!replace)
		return;

	/* We inserted the new entry before replaced one */
	replaced = list_next_entry(fib_entry, list);

	mlxsw_sp_fib4_node_entry_unlink(mlxsw_sp, replaced);
	mlxsw_sp_fib4_entry_destroy(mlxsw_sp, replaced);
	mlxsw_sp_fib4_node_put(mlxsw_sp, fib_node);
}

static int
mlxsw_sp_router_fib4_add(struct mlxsw_sp *mlxsw_sp,
			 const struct fib_entry_notifier_info *fen_info,
			 bool replace, bool append)
{
	struct mlxsw_sp_fib_entry *fib_entry;
	struct mlxsw_sp_fib_node *fib_node;
	int err;

	if (mlxsw_sp->router.aborted)
		return 0;

	fib_node = mlxsw_sp_fib4_node_get(mlxsw_sp, fen_info);
	if (IS_ERR(fib_node)) {
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to get FIB node\n");
		return PTR_ERR(fib_node);
	}

	fib_entry = mlxsw_sp_fib4_entry_create(mlxsw_sp, fib_node, fen_info);
	if (IS_ERR(fib_entry)) {
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to create FIB entry\n");
		err = PTR_ERR(fib_entry);
		goto err_fib4_entry_create;
	}

	err = mlxsw_sp_fib4_node_entry_link(mlxsw_sp, fib_entry, replace,
					    append);
	if (err) {
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to link FIB entry to node\n");
		goto err_fib4_node_entry_link;
	}

	mlxsw_sp_fib4_entry_replace(mlxsw_sp, fib_entry, replace);

	return 0;

err_fib4_node_entry_link:
	mlxsw_sp_fib4_entry_destroy(mlxsw_sp, fib_entry);
err_fib4_entry_create:
	mlxsw_sp_fib4_node_put(mlxsw_sp, fib_node);
	return err;
}

static void mlxsw_sp_router_fib4_del(struct mlxsw_sp *mlxsw_sp,
				     struct fib_entry_notifier_info *fen_info)
{
	struct mlxsw_sp_fib_entry *fib_entry;
	struct mlxsw_sp_fib_node *fib_node;

	if (mlxsw_sp->router.aborted)
		return;

	fib_entry = mlxsw_sp_fib4_entry_lookup(mlxsw_sp, fen_info);
	if (WARN_ON(!fib_entry))
		return;
	fib_node = fib_entry->fib_node;

	mlxsw_sp_fib4_node_entry_unlink(mlxsw_sp, fib_entry);
	mlxsw_sp_fib4_entry_destroy(mlxsw_sp, fib_entry);
	mlxsw_sp_fib4_node_put(mlxsw_sp, fib_node);
}

static int mlxsw_sp_router_set_abort_trap(struct mlxsw_sp *mlxsw_sp)
{
	char ralta_pl[MLXSW_REG_RALTA_LEN];
	char ralst_pl[MLXSW_REG_RALST_LEN];
	char raltb_pl[MLXSW_REG_RALTB_LEN];
	char ralue_pl[MLXSW_REG_RALUE_LEN];
	int err;

	mlxsw_reg_ralta_pack(ralta_pl, true, MLXSW_REG_RALXX_PROTOCOL_IPV4,
			     MLXSW_SP_LPM_TREE_MIN);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralta), ralta_pl);
	if (err)
		return err;

	mlxsw_reg_ralst_pack(ralst_pl, 0xff, MLXSW_SP_LPM_TREE_MIN);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralst), ralst_pl);
	if (err)
		return err;

	mlxsw_reg_raltb_pack(raltb_pl, 0, MLXSW_REG_RALXX_PROTOCOL_IPV4,
			     MLXSW_SP_LPM_TREE_MIN);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(raltb), raltb_pl);
	if (err)
		return err;

	mlxsw_reg_ralue_pack4(ralue_pl, MLXSW_SP_L3_PROTO_IPV4,
			      MLXSW_REG_RALUE_OP_WRITE_WRITE, 0, 0, 0);
	mlxsw_reg_ralue_act_ip2me_pack(ralue_pl);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralue), ralue_pl);
}

static void mlxsw_sp_fib4_node_flush(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_fib_node *fib_node)
{
	struct mlxsw_sp_fib_entry *fib_entry, *tmp;

	list_for_each_entry_safe(fib_entry, tmp, &fib_node->entry_list, list) {
		bool do_break = &tmp->list == &fib_node->entry_list;

		mlxsw_sp_fib4_node_entry_unlink(mlxsw_sp, fib_entry);
		mlxsw_sp_fib4_entry_destroy(mlxsw_sp, fib_entry);
		mlxsw_sp_fib4_node_put(mlxsw_sp, fib_node);
		/* Break when entry list is empty and node was freed.
		 * Otherwise, we'll access freed memory in the next
		 * iteration.
		 */
		if (do_break)
			break;
	}
}

static void mlxsw_sp_fib_node_flush(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_fib_node *fib_node)
{
	switch (fib_node->vr->proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		mlxsw_sp_fib4_node_flush(mlxsw_sp, fib_node);
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		WARN_ON_ONCE(1);
		break;
	}
}

static void mlxsw_sp_router_fib_flush(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_fib_node *fib_node, *tmp;
	struct mlxsw_sp_vr *vr;
	int i;

	for (i = 0; i < MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_VRS); i++) {
		vr = &mlxsw_sp->router.vrs[i];

		if (!vr->used)
			continue;

		list_for_each_entry_safe(fib_node, tmp, &vr->fib->node_list,
					 list) {
			bool do_break = &tmp->list == &vr->fib->node_list;

			mlxsw_sp_fib_node_flush(mlxsw_sp, fib_node);
			if (do_break)
				break;
		}
	}
}

static void mlxsw_sp_router_fib4_abort(struct mlxsw_sp *mlxsw_sp)
{
	int err;

	if (mlxsw_sp->router.aborted)
		return;
	dev_warn(mlxsw_sp->bus_info->dev, "FIB abort triggered. Note that FIB entries are no longer being offloaded to this device.\n");
	mlxsw_sp_router_fib_flush(mlxsw_sp);
	mlxsw_sp->router.aborted = true;
	err = mlxsw_sp_router_set_abort_trap(mlxsw_sp);
	if (err)
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to set abort trap.\n");
}

static int mlxsw_sp_router_rif_disable(struct mlxsw_sp *mlxsw_sp, u16 rif)
{
	char ritr_pl[MLXSW_REG_RITR_LEN];
	int err;

	mlxsw_reg_ritr_rif_pack(ritr_pl, rif);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
	if (WARN_ON_ONCE(err))
		return err;

	mlxsw_reg_ritr_enable_set(ritr_pl, false);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
}

void mlxsw_sp_router_rif_gone_sync(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_rif *r)
{
	mlxsw_sp_router_rif_disable(mlxsw_sp, r->rif);
	mlxsw_sp_nexthop_rif_gone_sync(mlxsw_sp, r);
	mlxsw_sp_neigh_rif_gone_sync(mlxsw_sp, r);
}

static int __mlxsw_sp_router_init(struct mlxsw_sp *mlxsw_sp)
{
	char rgcr_pl[MLXSW_REG_RGCR_LEN];
	u64 max_rifs;
	int err;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, MAX_RIFS))
		return -EIO;

	max_rifs = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS);
	mlxsw_sp->rifs = kcalloc(max_rifs, sizeof(struct mlxsw_sp_rif *),
				 GFP_KERNEL);
	if (!mlxsw_sp->rifs)
		return -ENOMEM;

	mlxsw_reg_rgcr_pack(rgcr_pl, true);
	mlxsw_reg_rgcr_max_router_interfaces_set(rgcr_pl, max_rifs);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rgcr), rgcr_pl);
	if (err)
		goto err_rgcr_fail;

	return 0;

err_rgcr_fail:
	kfree(mlxsw_sp->rifs);
	return err;
}

static void __mlxsw_sp_router_fini(struct mlxsw_sp *mlxsw_sp)
{
	char rgcr_pl[MLXSW_REG_RGCR_LEN];
	int i;

	mlxsw_reg_rgcr_pack(rgcr_pl, false);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rgcr), rgcr_pl);

	for (i = 0; i < MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS); i++)
		WARN_ON_ONCE(mlxsw_sp->rifs[i]);

	kfree(mlxsw_sp->rifs);
}

struct mlxsw_sp_fib_event_work {
	struct work_struct work;
	union {
		struct fib_entry_notifier_info fen_info;
		struct fib_nh_notifier_info fnh_info;
	};
	struct mlxsw_sp *mlxsw_sp;
	unsigned long event;
};

static void mlxsw_sp_router_fib_event_work(struct work_struct *work)
{
	struct mlxsw_sp_fib_event_work *fib_work =
		container_of(work, struct mlxsw_sp_fib_event_work, work);
	struct mlxsw_sp *mlxsw_sp = fib_work->mlxsw_sp;
	bool replace, append;
	int err;

	/* Protect internal structures from changes */
	rtnl_lock();
	switch (fib_work->event) {
	case FIB_EVENT_ENTRY_REPLACE: /* fall through */
	case FIB_EVENT_ENTRY_APPEND: /* fall through */
	case FIB_EVENT_ENTRY_ADD:
		replace = fib_work->event == FIB_EVENT_ENTRY_REPLACE;
		append = fib_work->event == FIB_EVENT_ENTRY_APPEND;
		err = mlxsw_sp_router_fib4_add(mlxsw_sp, &fib_work->fen_info,
					       replace, append);
		if (err)
			mlxsw_sp_router_fib4_abort(mlxsw_sp);
		fib_info_put(fib_work->fen_info.fi);
		break;
	case FIB_EVENT_ENTRY_DEL:
		mlxsw_sp_router_fib4_del(mlxsw_sp, &fib_work->fen_info);
		fib_info_put(fib_work->fen_info.fi);
		break;
	case FIB_EVENT_RULE_ADD: /* fall through */
	case FIB_EVENT_RULE_DEL:
		mlxsw_sp_router_fib4_abort(mlxsw_sp);
		break;
	case FIB_EVENT_NH_ADD: /* fall through */
	case FIB_EVENT_NH_DEL:
		mlxsw_sp_nexthop_event(mlxsw_sp, fib_work->event,
				       fib_work->fnh_info.fib_nh);
		fib_info_put(fib_work->fnh_info.fib_nh->nh_parent);
		break;
	}
	rtnl_unlock();
	kfree(fib_work);
}

/* Called with rcu_read_lock() */
static int mlxsw_sp_router_fib_event(struct notifier_block *nb,
				     unsigned long event, void *ptr)
{
	struct mlxsw_sp *mlxsw_sp = container_of(nb, struct mlxsw_sp, fib_nb);
	struct mlxsw_sp_fib_event_work *fib_work;
	struct fib_notifier_info *info = ptr;

	if (!net_eq(info->net, &init_net))
		return NOTIFY_DONE;

	fib_work = kzalloc(sizeof(*fib_work), GFP_ATOMIC);
	if (WARN_ON(!fib_work))
		return NOTIFY_BAD;

	INIT_WORK(&fib_work->work, mlxsw_sp_router_fib_event_work);
	fib_work->mlxsw_sp = mlxsw_sp;
	fib_work->event = event;

	switch (event) {
	case FIB_EVENT_ENTRY_REPLACE: /* fall through */
	case FIB_EVENT_ENTRY_APPEND: /* fall through */
	case FIB_EVENT_ENTRY_ADD: /* fall through */
	case FIB_EVENT_ENTRY_DEL:
		memcpy(&fib_work->fen_info, ptr, sizeof(fib_work->fen_info));
		/* Take referece on fib_info to prevent it from being
		 * freed while work is queued. Release it afterwards.
		 */
		fib_info_hold(fib_work->fen_info.fi);
		break;
	case FIB_EVENT_NH_ADD: /* fall through */
	case FIB_EVENT_NH_DEL:
		memcpy(&fib_work->fnh_info, ptr, sizeof(fib_work->fnh_info));
		fib_info_hold(fib_work->fnh_info.fib_nh->nh_parent);
		break;
	}

	mlxsw_core_schedule_work(&fib_work->work);

	return NOTIFY_DONE;
}

static void mlxsw_sp_router_fib_dump_flush(struct notifier_block *nb)
{
	struct mlxsw_sp *mlxsw_sp = container_of(nb, struct mlxsw_sp, fib_nb);

	/* Flush pending FIB notifications and then flush the device's
	 * table before requesting another dump. The FIB notification
	 * block is unregistered, so no need to take RTNL.
	 */
	mlxsw_core_flush_owq();
	mlxsw_sp_router_fib_flush(mlxsw_sp);
}

int mlxsw_sp_router_init(struct mlxsw_sp *mlxsw_sp)
{
	int err;

	INIT_LIST_HEAD(&mlxsw_sp->router.nexthop_neighs_list);
	err = __mlxsw_sp_router_init(mlxsw_sp);
	if (err)
		return err;

	err = rhashtable_init(&mlxsw_sp->router.nexthop_ht,
			      &mlxsw_sp_nexthop_ht_params);
	if (err)
		goto err_nexthop_ht_init;

	err = rhashtable_init(&mlxsw_sp->router.nexthop_group_ht,
			      &mlxsw_sp_nexthop_group_ht_params);
	if (err)
		goto err_nexthop_group_ht_init;

	mlxsw_sp_lpm_init(mlxsw_sp);
	err = mlxsw_sp_vrs_init(mlxsw_sp);
	if (err)
		goto err_vrs_init;

	err = mlxsw_sp_neigh_init(mlxsw_sp);
	if (err)
		goto err_neigh_init;

	mlxsw_sp->fib_nb.notifier_call = mlxsw_sp_router_fib_event;
	err = register_fib_notifier(&mlxsw_sp->fib_nb,
				    mlxsw_sp_router_fib_dump_flush);
	if (err)
		goto err_register_fib_notifier;

	return 0;

err_register_fib_notifier:
	mlxsw_sp_neigh_fini(mlxsw_sp);
err_neigh_init:
	mlxsw_sp_vrs_fini(mlxsw_sp);
err_vrs_init:
	rhashtable_destroy(&mlxsw_sp->router.nexthop_group_ht);
err_nexthop_group_ht_init:
	rhashtable_destroy(&mlxsw_sp->router.nexthop_ht);
err_nexthop_ht_init:
	__mlxsw_sp_router_fini(mlxsw_sp);
	return err;
}

void mlxsw_sp_router_fini(struct mlxsw_sp *mlxsw_sp)
{
	unregister_fib_notifier(&mlxsw_sp->fib_nb);
	mlxsw_sp_neigh_fini(mlxsw_sp);
	mlxsw_sp_vrs_fini(mlxsw_sp);
	rhashtable_destroy(&mlxsw_sp->router.nexthop_group_ht);
	rhashtable_destroy(&mlxsw_sp->router.nexthop_ht);
	__mlxsw_sp_router_fini(mlxsw_sp);
}
