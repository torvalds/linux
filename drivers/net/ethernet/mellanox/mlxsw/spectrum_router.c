/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_router.c
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 * Copyright (c) 2016 Ido Schimmel <idosch@mellanox.com>
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
#include <net/neighbour.h>
#include <net/arp.h>

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

struct mlxsw_sp_fib_entry {
	struct rhash_head ht_node;
	struct mlxsw_sp_fib_key key;
	enum mlxsw_sp_fib_entry_type type;
	u8 added:1;
	u16 rif; /* used for action local */
	struct mlxsw_sp_vr *vr;
};

struct mlxsw_sp_fib {
	struct rhashtable ht;
	unsigned long prefix_ref_count[MLXSW_SP_PREFIX_COUNT];
	struct mlxsw_sp_prefix_usage prefix_usage;
};

static const struct rhashtable_params mlxsw_sp_fib_ht_params = {
	.key_offset = offsetof(struct mlxsw_sp_fib_entry, key),
	.head_offset = offsetof(struct mlxsw_sp_fib_entry, ht_node),
	.key_len = sizeof(struct mlxsw_sp_fib_key),
	.automatic_shrinking = true,
};

static int mlxsw_sp_fib_entry_insert(struct mlxsw_sp_fib *fib,
				     struct mlxsw_sp_fib_entry *fib_entry)
{
	unsigned char prefix_len = fib_entry->key.prefix_len;
	int err;

	err = rhashtable_insert_fast(&fib->ht, &fib_entry->ht_node,
				     mlxsw_sp_fib_ht_params);
	if (err)
		return err;
	if (fib->prefix_ref_count[prefix_len]++ == 0)
		mlxsw_sp_prefix_usage_set(&fib->prefix_usage, prefix_len);
	return 0;
}

static void mlxsw_sp_fib_entry_remove(struct mlxsw_sp_fib *fib,
				      struct mlxsw_sp_fib_entry *fib_entry)
{
	unsigned char prefix_len = fib_entry->key.prefix_len;

	if (--fib->prefix_ref_count[prefix_len] == 0)
		mlxsw_sp_prefix_usage_clear(&fib->prefix_usage, prefix_len);
	rhashtable_remove_fast(&fib->ht, &fib_entry->ht_node,
			       mlxsw_sp_fib_ht_params);
}

static struct mlxsw_sp_fib_entry *
mlxsw_sp_fib_entry_create(struct mlxsw_sp_fib *fib, const void *addr,
			  size_t addr_len, unsigned char prefix_len)
{
	struct mlxsw_sp_fib_entry *fib_entry;

	fib_entry = kzalloc(sizeof(*fib_entry), GFP_KERNEL);
	if (!fib_entry)
		return NULL;
	memcpy(fib_entry->key.addr, addr, addr_len);
	fib_entry->key.prefix_len = prefix_len;
	return fib_entry;
}

static void mlxsw_sp_fib_entry_destroy(struct mlxsw_sp_fib_entry *fib_entry)
{
	kfree(fib_entry);
}

static struct mlxsw_sp_fib_entry *
mlxsw_sp_fib_entry_lookup(struct mlxsw_sp_fib *fib, const void *addr,
			  size_t addr_len, unsigned char prefix_len)
{
	struct mlxsw_sp_fib_key key = {{ 0 } };

	memcpy(key.addr, addr, addr_len);
	key.prefix_len = prefix_len;
	return rhashtable_lookup_fast(&fib->ht, &key, mlxsw_sp_fib_ht_params);
}

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
	return fib;

err_rhashtable_init:
	kfree(fib);
	return ERR_PTR(err);
}

static void mlxsw_sp_fib_destroy(struct mlxsw_sp_fib *fib)
{
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

	mlxsw_reg_ralta_pack(ralta_pl, true, lpm_tree->proto, lpm_tree->id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralta), ralta_pl);
}

static int mlxsw_sp_lpm_tree_free(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_lpm_tree *lpm_tree)
{
	char ralta_pl[MLXSW_REG_RALTA_LEN];

	mlxsw_reg_ralta_pack(ralta_pl, false, lpm_tree->proto, lpm_tree->id);
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
		if (lpm_tree->proto == proto &&
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

	for (i = 0; i < MLXSW_SP_VIRTUAL_ROUTER_MAX; i++) {
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

	mlxsw_reg_raltb_pack(raltb_pl, vr->id, vr->proto, vr->lpm_tree->id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(raltb), raltb_pl);
}

static int mlxsw_sp_vr_lpm_tree_unbind(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_vr *vr)
{
	char raltb_pl[MLXSW_REG_RALTB_LEN];

	/* Bind to tree 0 which is default */
	mlxsw_reg_raltb_pack(raltb_pl, vr->id, vr->proto, 0);
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
	for (i = 0; i < MLXSW_SP_VIRTUAL_ROUTER_MAX; i++) {
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

static void mlxsw_sp_vrs_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_vr *vr;
	int i;

	for (i = 0; i < MLXSW_SP_VIRTUAL_ROUTER_MAX; i++) {
		vr = &mlxsw_sp->router.vrs[i];
		vr->id = i;
	}
}

struct mlxsw_sp_neigh_key {
	unsigned char addr[sizeof(struct in6_addr)];
	struct net_device *dev;
};

struct mlxsw_sp_neigh_entry {
	struct rhash_head ht_node;
	struct mlxsw_sp_neigh_key key;
	u16 rif;
	struct neighbour *n;
};

static const struct rhashtable_params mlxsw_sp_neigh_ht_params = {
	.key_offset = offsetof(struct mlxsw_sp_neigh_entry, key),
	.head_offset = offsetof(struct mlxsw_sp_neigh_entry, ht_node),
	.key_len = sizeof(struct mlxsw_sp_neigh_key),
};

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
mlxsw_sp_neigh_entry_create(const void *addr, size_t addr_len,
			    struct net_device *dev, u16 rif,
			    struct neighbour *n)
{
	struct mlxsw_sp_neigh_entry *neigh_entry;

	neigh_entry = kzalloc(sizeof(*neigh_entry), GFP_ATOMIC);
	if (!neigh_entry)
		return NULL;
	memcpy(neigh_entry->key.addr, addr, addr_len);
	neigh_entry->key.dev = dev;
	neigh_entry->rif = rif;
	neigh_entry->n = n;
	return neigh_entry;
}

static void
mlxsw_sp_neigh_entry_destroy(struct mlxsw_sp_neigh_entry *neigh_entry)
{
	kfree(neigh_entry);
}

static struct mlxsw_sp_neigh_entry *
mlxsw_sp_neigh_entry_lookup(struct mlxsw_sp *mlxsw_sp, const void *addr,
			    size_t addr_len, struct net_device *dev)
{
	struct mlxsw_sp_neigh_key key = {{ 0 } };

	memcpy(key.addr, addr, addr_len);
	key.dev = dev;
	return rhashtable_lookup_fast(&mlxsw_sp->router.neigh_ht,
				      &key, mlxsw_sp_neigh_ht_params);
}

int mlxsw_sp_router_neigh_construct(struct net_device *dev,
				    struct neighbour *n)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_neigh_entry *neigh_entry;
	struct mlxsw_sp_rif *r;
	u32 dip;
	int err;

	if (n->tbl != &arp_tbl)
		return 0;

	dip = ntohl(*((__be32 *) n->primary_key));
	neigh_entry = mlxsw_sp_neigh_entry_lookup(mlxsw_sp, &dip, sizeof(dip),
						  n->dev);
	if (neigh_entry) {
		WARN_ON(neigh_entry->n != n);
		return 0;
	}

	r = mlxsw_sp_rif_find_by_dev(mlxsw_sp, dev);
	if (WARN_ON(!r))
		return -EINVAL;

	neigh_entry = mlxsw_sp_neigh_entry_create(&dip, sizeof(dip), n->dev,
						  r->rif, n);
	if (!neigh_entry)
		return -ENOMEM;
	err = mlxsw_sp_neigh_entry_insert(mlxsw_sp, neigh_entry);
	if (err)
		goto err_neigh_entry_insert;
	return 0;

err_neigh_entry_insert:
	mlxsw_sp_neigh_entry_destroy(neigh_entry);
	return err;
}

void mlxsw_sp_router_neigh_destroy(struct net_device *dev,
				   struct neighbour *n)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_neigh_entry *neigh_entry;
	u32 dip;

	if (n->tbl != &arp_tbl)
		return;

	dip = ntohl(*((__be32 *) n->primary_key));
	neigh_entry = mlxsw_sp_neigh_entry_lookup(mlxsw_sp, &dip, sizeof(dip),
						  n->dev);
	if (!neigh_entry)
		return;
	mlxsw_sp_neigh_entry_remove(mlxsw_sp, neigh_entry);
	mlxsw_sp_neigh_entry_destroy(neigh_entry);
}

static int mlxsw_sp_neigh_init(struct mlxsw_sp *mlxsw_sp)
{
	return rhashtable_init(&mlxsw_sp->router.neigh_ht,
			       &mlxsw_sp_neigh_ht_params);
}

static void mlxsw_sp_neigh_fini(struct mlxsw_sp *mlxsw_sp)
{
	rhashtable_destroy(&mlxsw_sp->router.neigh_ht);
}

static int __mlxsw_sp_router_init(struct mlxsw_sp *mlxsw_sp)
{
	char rgcr_pl[MLXSW_REG_RGCR_LEN];

	mlxsw_reg_rgcr_pack(rgcr_pl, true);
	mlxsw_reg_rgcr_max_router_interfaces_set(rgcr_pl, MLXSW_SP_RIF_MAX);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rgcr), rgcr_pl);
}

static void __mlxsw_sp_router_fini(struct mlxsw_sp *mlxsw_sp)
{
	char rgcr_pl[MLXSW_REG_RGCR_LEN];

	mlxsw_reg_rgcr_pack(rgcr_pl, false);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rgcr), rgcr_pl);
}

int mlxsw_sp_router_init(struct mlxsw_sp *mlxsw_sp)
{
	int err;

	err = __mlxsw_sp_router_init(mlxsw_sp);
	if (err)
		return err;
	mlxsw_sp_lpm_init(mlxsw_sp);
	mlxsw_sp_vrs_init(mlxsw_sp);
	return mlxsw_sp_neigh_init(mlxsw_sp);
}

void mlxsw_sp_router_fini(struct mlxsw_sp *mlxsw_sp)
{
	mlxsw_sp_neigh_fini(mlxsw_sp);
	__mlxsw_sp_router_fini(mlxsw_sp);
}

static int mlxsw_sp_fib_entry_op4_local(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_fib_entry *fib_entry,
					enum mlxsw_reg_ralue_op op)
{
	char ralue_pl[MLXSW_REG_RALUE_LEN];
	u32 *p_dip = (u32 *) fib_entry->key.addr;
	struct mlxsw_sp_vr *vr = fib_entry->vr;

	mlxsw_reg_ralue_pack4(ralue_pl, vr->proto, op, vr->id,
			      fib_entry->key.prefix_len, *p_dip);
	mlxsw_reg_ralue_act_local_pack(ralue_pl,
				       MLXSW_REG_RALUE_TRAP_ACTION_NOP, 0,
				       fib_entry->rif);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralue), ralue_pl);
}

static int mlxsw_sp_fib_entry_op4_trap(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_fib_entry *fib_entry,
				       enum mlxsw_reg_ralue_op op)
{
	char ralue_pl[MLXSW_REG_RALUE_LEN];
	u32 *p_dip = (u32 *) fib_entry->key.addr;
	struct mlxsw_sp_vr *vr = fib_entry->vr;

	mlxsw_reg_ralue_pack4(ralue_pl, vr->proto, op, vr->id,
			      fib_entry->key.prefix_len, *p_dip);
	mlxsw_reg_ralue_act_ip2me_pack(ralue_pl);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralue), ralue_pl);
}

static int mlxsw_sp_fib_entry_op4(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_fib_entry *fib_entry,
				  enum mlxsw_reg_ralue_op op)
{
	switch (fib_entry->type) {
	case MLXSW_SP_FIB_ENTRY_TYPE_REMOTE:
		return -EINVAL;
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
	switch (fib_entry->vr->proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		return mlxsw_sp_fib_entry_op4(mlxsw_sp, fib_entry, op);
	case MLXSW_SP_L3_PROTO_IPV6:
		return -EINVAL;
	}
	return -EINVAL;
}

static int mlxsw_sp_fib_entry_update(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_fib_entry *fib_entry)
{
	enum mlxsw_reg_ralue_op op;

	op = !fib_entry->added ? MLXSW_REG_RALUE_OP_WRITE_WRITE :
				 MLXSW_REG_RALUE_OP_WRITE_UPDATE;
	return mlxsw_sp_fib_entry_op(mlxsw_sp, fib_entry, op);
}

static int mlxsw_sp_fib_entry_del(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_fib_entry *fib_entry)
{
	return mlxsw_sp_fib_entry_op(mlxsw_sp, fib_entry,
				     MLXSW_REG_RALUE_OP_WRITE_DELETE);
}

struct mlxsw_sp_router_fib4_add_info {
	struct switchdev_trans_item tritem;
	struct mlxsw_sp *mlxsw_sp;
	struct mlxsw_sp_fib_entry *fib_entry;
};

static void mlxsw_sp_router_fib4_add_info_destroy(void const *data)
{
	const struct mlxsw_sp_router_fib4_add_info *info = data;
	struct mlxsw_sp_fib_entry *fib_entry = info->fib_entry;
	struct mlxsw_sp *mlxsw_sp = info->mlxsw_sp;

	mlxsw_sp_fib_entry_destroy(fib_entry);
	mlxsw_sp_vr_put(mlxsw_sp, fib_entry->vr);
	kfree(info);
}

static int
mlxsw_sp_router_fib4_entry_init(struct mlxsw_sp *mlxsw_sp,
				const struct switchdev_obj_ipv4_fib *fib4,
				struct mlxsw_sp_fib_entry *fib_entry)
{
	struct fib_info *fi = fib4->fi;

	if (fib4->type == RTN_LOCAL || fib4->type == RTN_BROADCAST) {
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_TRAP;
		return 0;
	}
	if (fib4->type != RTN_UNICAST)
		return -EINVAL;

	if (fi->fib_scope != RT_SCOPE_UNIVERSE) {
		struct mlxsw_sp_rif *r;

		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_LOCAL;
		r = mlxsw_sp_rif_find_by_dev(mlxsw_sp, fi->fib_dev);
		if (!r)
			return -EINVAL;
		fib_entry->rif = r->rif;
		return 0;
	}
	return -EINVAL;
}

static int
mlxsw_sp_router_fib4_add_prepare(struct mlxsw_sp_port *mlxsw_sp_port,
				 const struct switchdev_obj_ipv4_fib *fib4,
				 struct switchdev_trans *trans)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_router_fib4_add_info *info;
	struct mlxsw_sp_fib_entry *fib_entry;
	struct mlxsw_sp_vr *vr;
	int err;

	vr = mlxsw_sp_vr_get(mlxsw_sp, fib4->dst_len, fib4->tb_id,
			     MLXSW_SP_L3_PROTO_IPV4);
	if (IS_ERR(vr))
		return PTR_ERR(vr);

	fib_entry = mlxsw_sp_fib_entry_create(vr->fib, &fib4->dst,
					      sizeof(fib4->dst), fib4->dst_len);
	if (!fib_entry) {
		err = -ENOMEM;
		goto err_fib_entry_create;
	}
	fib_entry->vr = vr;

	err = mlxsw_sp_router_fib4_entry_init(mlxsw_sp, fib4, fib_entry);
	if (err)
		goto err_fib4_entry_init;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		err = -ENOMEM;
		goto err_alloc_info;
	}
	info->mlxsw_sp = mlxsw_sp;
	info->fib_entry = fib_entry;
	switchdev_trans_item_enqueue(trans, info,
				     mlxsw_sp_router_fib4_add_info_destroy,
				     &info->tritem);
	return 0;

err_alloc_info:
err_fib4_entry_init:
	mlxsw_sp_fib_entry_destroy(fib_entry);
err_fib_entry_create:
	mlxsw_sp_vr_put(mlxsw_sp, vr);
	return err;
}

static int
mlxsw_sp_router_fib4_add_commit(struct mlxsw_sp_port *mlxsw_sp_port,
				const struct switchdev_obj_ipv4_fib *fib4,
				struct switchdev_trans *trans)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_router_fib4_add_info *info;
	struct mlxsw_sp_fib_entry *fib_entry;
	struct mlxsw_sp_vr *vr;
	int err;

	info = switchdev_trans_item_dequeue(trans);
	fib_entry = info->fib_entry;
	kfree(info);

	vr = fib_entry->vr;
	err = mlxsw_sp_fib_entry_insert(fib_entry->vr->fib, fib_entry);
	if (err)
		goto err_fib_entry_insert;
	err = mlxsw_sp_fib_entry_update(mlxsw_sp, fib_entry);
	if (err)
		goto err_fib_entry_add;
	return 0;

err_fib_entry_add:
	mlxsw_sp_fib_entry_remove(vr->fib, fib_entry);
err_fib_entry_insert:
	mlxsw_sp_fib_entry_destroy(fib_entry);
	mlxsw_sp_vr_put(mlxsw_sp, vr);
	return err;
}

int mlxsw_sp_router_fib4_add(struct mlxsw_sp_port *mlxsw_sp_port,
			     const struct switchdev_obj_ipv4_fib *fib4,
			     struct switchdev_trans *trans)
{
	if (switchdev_trans_ph_prepare(trans))
		return mlxsw_sp_router_fib4_add_prepare(mlxsw_sp_port,
							fib4, trans);
	return mlxsw_sp_router_fib4_add_commit(mlxsw_sp_port,
					       fib4, trans);
}

int mlxsw_sp_router_fib4_del(struct mlxsw_sp_port *mlxsw_sp_port,
			     const struct switchdev_obj_ipv4_fib *fib4)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_fib_entry *fib_entry;
	struct mlxsw_sp_vr *vr;

	vr = mlxsw_sp_vr_find(mlxsw_sp, fib4->tb_id, MLXSW_SP_L3_PROTO_IPV4);
	if (!vr) {
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to find virtual router for FIB4 entry being removed.\n");
		return -ENOENT;
	}
	fib_entry = mlxsw_sp_fib_entry_lookup(vr->fib, &fib4->dst,
					      sizeof(fib4->dst), fib4->dst_len);
	if (!fib_entry) {
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to find FIB4 entry being removed.\n");
		return PTR_ERR(vr);
	}
	mlxsw_sp_fib_entry_del(mlxsw_sp_port->mlxsw_sp, fib_entry);
	mlxsw_sp_fib_entry_remove(vr->fib, fib_entry);
	mlxsw_sp_fib_entry_destroy(fib_entry);
	mlxsw_sp_vr_put(mlxsw_sp, vr);
	return 0;
}
