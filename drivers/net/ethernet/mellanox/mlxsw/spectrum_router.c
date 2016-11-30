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
	struct net_device *dev;
	unsigned char addr[sizeof(struct in6_addr)];
	unsigned char prefix_len;
};

enum mlxsw_sp_fib_entry_type {
	MLXSW_SP_FIB_ENTRY_TYPE_REMOTE,
	MLXSW_SP_FIB_ENTRY_TYPE_LOCAL,
	MLXSW_SP_FIB_ENTRY_TYPE_TRAP,
};

struct mlxsw_sp_nexthop_group;

struct mlxsw_sp_fib_entry {
	struct rhash_head ht_node;
	struct list_head list;
	struct mlxsw_sp_fib_key key;
	enum mlxsw_sp_fib_entry_type type;
	unsigned int ref_count;
	u16 rif; /* used for action local */
	struct mlxsw_sp_vr *vr;
	struct fib_info *fi;
	struct list_head nexthop_group_node;
	struct mlxsw_sp_nexthop_group *nh_group;
};

struct mlxsw_sp_fib {
	struct rhashtable ht;
	struct list_head entry_list;
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
	list_add_tail(&fib_entry->list, &fib->entry_list);
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
	list_del(&fib_entry->list);
	rhashtable_remove_fast(&fib->ht, &fib_entry->ht_node,
			       mlxsw_sp_fib_ht_params);
}

static struct mlxsw_sp_fib_entry *
mlxsw_sp_fib_entry_create(struct mlxsw_sp_fib *fib, const void *addr,
			  size_t addr_len, unsigned char prefix_len,
			  struct net_device *dev)
{
	struct mlxsw_sp_fib_entry *fib_entry;

	fib_entry = kzalloc(sizeof(*fib_entry), GFP_KERNEL);
	if (!fib_entry)
		return NULL;
	fib_entry->key.dev = dev;
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
			  size_t addr_len, unsigned char prefix_len,
			  struct net_device *dev)
{
	struct mlxsw_sp_fib_key key;

	memset(&key, 0, sizeof(key));
	key.dev = dev;
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
	INIT_LIST_HEAD(&fib->entry_list);
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
	struct mlxsw_resources *resources;
	struct mlxsw_sp_vr *vr;
	int i;

	resources = mlxsw_core_resources_get(mlxsw_sp->core);
	for (i = 0; i < resources->max_virtual_routers; i++) {
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
	struct mlxsw_resources *resources;
	struct mlxsw_sp_vr *vr;
	int i;

	tb_id = mlxsw_sp_fix_tb_id(tb_id);

	resources = mlxsw_core_resources_get(mlxsw_sp->core);
	for (i = 0; i < resources->max_virtual_routers; i++) {
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
	struct mlxsw_resources *resources;
	struct mlxsw_sp_vr *vr;
	int i;

	resources = mlxsw_core_resources_get(mlxsw_sp->core);
	if (!resources->max_virtual_routers_valid)
		return -EIO;

	mlxsw_sp->router.vrs = kcalloc(resources->max_virtual_routers,
				       sizeof(struct mlxsw_sp_vr),
				       GFP_KERNEL);
	if (!mlxsw_sp->router.vrs)
		return -ENOMEM;

	for (i = 0; i < resources->max_virtual_routers; i++) {
		vr = &mlxsw_sp->router.vrs[i];
		vr->id = i;
	}

	return 0;
}

static void mlxsw_sp_router_fib_flush(struct mlxsw_sp *mlxsw_sp);

static void mlxsw_sp_vrs_fini(struct mlxsw_sp *mlxsw_sp)
{
	mlxsw_sp_router_fib_flush(mlxsw_sp);
	kfree(mlxsw_sp->router.vrs);
}

struct mlxsw_sp_neigh_key {
	struct neighbour *n;
};

struct mlxsw_sp_neigh_entry {
	struct rhash_head ht_node;
	struct mlxsw_sp_neigh_key key;
	u16 rif;
	bool offloaded;
	struct delayed_work dw;
	struct mlxsw_sp_port *mlxsw_sp_port;
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

static void mlxsw_sp_router_neigh_update_hw(struct work_struct *work);

static struct mlxsw_sp_neigh_entry *
mlxsw_sp_neigh_entry_create(struct neighbour *n, u16 rif)
{
	struct mlxsw_sp_neigh_entry *neigh_entry;

	neigh_entry = kzalloc(sizeof(*neigh_entry), GFP_ATOMIC);
	if (!neigh_entry)
		return NULL;
	neigh_entry->key.n = n;
	neigh_entry->rif = rif;
	INIT_DELAYED_WORK(&neigh_entry->dw, mlxsw_sp_router_neigh_update_hw);
	INIT_LIST_HEAD(&neigh_entry->nexthop_list);
	return neigh_entry;
}

static void
mlxsw_sp_neigh_entry_destroy(struct mlxsw_sp_neigh_entry *neigh_entry)
{
	kfree(neigh_entry);
}

static struct mlxsw_sp_neigh_entry *
mlxsw_sp_neigh_entry_lookup(struct mlxsw_sp *mlxsw_sp, struct neighbour *n)
{
	struct mlxsw_sp_neigh_key key;

	key.n = n;
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
	int err;

	if (n->tbl != &arp_tbl)
		return 0;

	neigh_entry = mlxsw_sp_neigh_entry_lookup(mlxsw_sp, n);
	if (neigh_entry)
		return 0;

	r = mlxsw_sp_rif_find_by_dev(mlxsw_sp, n->dev);
	if (WARN_ON(!r))
		return -EINVAL;

	neigh_entry = mlxsw_sp_neigh_entry_create(n, r->rif);
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

	if (n->tbl != &arp_tbl)
		return;

	neigh_entry = mlxsw_sp_neigh_entry_lookup(mlxsw_sp, n);
	if (!neigh_entry)
		return;
	mlxsw_sp_neigh_entry_remove(mlxsw_sp, neigh_entry);
	mlxsw_sp_neigh_entry_destroy(neigh_entry);
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
			    nexthop_neighs_list_node) {
		/* If this neigh have nexthops, make the kernel think this neigh
		 * is active regardless of the traffic.
		 */
		if (!list_empty(&neigh_entry->nexthop_list))
			neigh_event_send(neigh_entry->key.n, NULL);
	}
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
			    nexthop_neighs_list_node) {
		if (!(neigh_entry->key.n->nud_state & NUD_VALID) &&
		    !list_empty(&neigh_entry->nexthop_list))
			neigh_event_send(neigh_entry->key.n, NULL);
	}
	rtnl_unlock();

	mlxsw_core_schedule_dw(&mlxsw_sp->router.nexthop_probe_dw,
			       MLXSW_SP_UNRESOLVED_NH_PROBE_INTERVAL);
}

static void
mlxsw_sp_nexthop_neigh_update(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_neigh_entry *neigh_entry,
			      bool removing);

static void mlxsw_sp_router_neigh_update_hw(struct work_struct *work)
{
	struct mlxsw_sp_neigh_entry *neigh_entry =
		container_of(work, struct mlxsw_sp_neigh_entry, dw.work);
	struct neighbour *n = neigh_entry->key.n;
	struct mlxsw_sp_port *mlxsw_sp_port = neigh_entry->mlxsw_sp_port;
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char rauht_pl[MLXSW_REG_RAUHT_LEN];
	struct net_device *dev;
	bool entry_connected;
	u8 nud_state;
	bool updating;
	bool removing;
	bool adding;
	u32 dip;
	int err;

	read_lock_bh(&n->lock);
	dip = ntohl(*((__be32 *) n->primary_key));
	memcpy(neigh_entry->ha, n->ha, sizeof(neigh_entry->ha));
	nud_state = n->nud_state;
	dev = n->dev;
	read_unlock_bh(&n->lock);

	entry_connected = nud_state & NUD_VALID;
	adding = (!neigh_entry->offloaded) && entry_connected;
	updating = neigh_entry->offloaded && entry_connected;
	removing = neigh_entry->offloaded && !entry_connected;

	if (adding || updating) {
		mlxsw_reg_rauht_pack4(rauht_pl, MLXSW_REG_RAUHT_OP_WRITE_ADD,
				      neigh_entry->rif,
				      neigh_entry->ha, dip);
		err = mlxsw_reg_write(mlxsw_sp->core,
				      MLXSW_REG(rauht), rauht_pl);
		if (err) {
			netdev_err(dev, "Could not add neigh %pI4h\n", &dip);
			neigh_entry->offloaded = false;
		} else {
			neigh_entry->offloaded = true;
		}
		mlxsw_sp_nexthop_neigh_update(mlxsw_sp, neigh_entry, false);
	} else if (removing) {
		mlxsw_reg_rauht_pack4(rauht_pl, MLXSW_REG_RAUHT_OP_WRITE_DELETE,
				      neigh_entry->rif,
				      neigh_entry->ha, dip);
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rauht),
				      rauht_pl);
		if (err) {
			netdev_err(dev, "Could not delete neigh %pI4h\n", &dip);
			neigh_entry->offloaded = true;
		} else {
			neigh_entry->offloaded = false;
		}
		mlxsw_sp_nexthop_neigh_update(mlxsw_sp, neigh_entry, true);
	}

	neigh_release(n);
	mlxsw_sp_port_dev_put(mlxsw_sp_port);
}

int mlxsw_sp_router_netevent_event(struct notifier_block *unused,
				   unsigned long event, void *ptr)
{
	struct mlxsw_sp_neigh_entry *neigh_entry;
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct mlxsw_sp *mlxsw_sp;
	unsigned long interval;
	struct net_device *dev;
	struct neigh_parms *p;
	struct neighbour *n;
	u32 dip;

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
		dev = n->dev;

		if (n->tbl != &arp_tbl)
			return NOTIFY_DONE;

		mlxsw_sp_port = mlxsw_sp_port_lower_dev_hold(dev);
		if (!mlxsw_sp_port)
			return NOTIFY_DONE;

		mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
		dip = ntohl(*((__be32 *) n->primary_key));
		neigh_entry = mlxsw_sp_neigh_entry_lookup(mlxsw_sp, n);
		if (WARN_ON(!neigh_entry)) {
			mlxsw_sp_port_dev_put(mlxsw_sp_port);
			return NOTIFY_DONE;
		}
		neigh_entry->mlxsw_sp_port = mlxsw_sp_port;

		/* Take a reference to ensure the neighbour won't be
		 * destructed until we drop the reference in delayed
		 * work.
		 */
		neigh_clone(n);
		if (!mlxsw_core_schedule_dw(&neigh_entry->dw, 0)) {
			neigh_release(n);
			mlxsw_sp_port_dev_put(mlxsw_sp_port);
		}
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

struct mlxsw_sp_nexthop {
	struct list_head neigh_list_node; /* member of neigh entry list */
	struct mlxsw_sp_nexthop_group *nh_grp; /* pointer back to the group
						* this belongs to
						*/
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

struct mlxsw_sp_nexthop_group {
	struct list_head list; /* node in mlxsw->router.nexthop_group_list */
	struct list_head fib_list; /* list of fib entries that use this group */
	u8 adj_index_valid:1;
	u32 adj_index;
	u16 ecmp_size;
	u16 count;
	struct mlxsw_sp_nexthop nexthops[0];
};

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
		if (vr == fib_entry->vr)
			continue;
		vr = fib_entry->vr;
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
				  struct mlxsw_sp_nexthop_group *nh_grp)
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

		if (nh->update) {
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
		err = mlxsw_sp_nexthop_group_mac_update(mlxsw_sp, nh_grp);
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
	err = mlxsw_sp_nexthop_group_mac_update(mlxsw_sp, nh_grp);
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

	/* Take RTNL mutex here to prevent lists from changes */
	rtnl_lock();
	list_for_each_entry(nh, &neigh_entry->nexthop_list,
			    neigh_list_node) {
		__mlxsw_sp_nexthop_neigh_update(nh, removing);
		mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh->nh_grp);
	}
	rtnl_unlock();
}

static int mlxsw_sp_nexthop_init(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_nexthop_group *nh_grp,
				 struct mlxsw_sp_nexthop *nh,
				 struct fib_nh *fib_nh)
{
	struct mlxsw_sp_neigh_entry *neigh_entry;
	struct net_device *dev = fib_nh->nh_dev;
	struct neighbour *n;
	u8 nud_state;

	/* Take a reference of neigh here ensuring that neigh would
	 * not be detructed before the nexthop entry is finished.
	 * The reference is taken either in neigh_lookup() or
	 * in neith_create() in case n is not found.
	 */
	n = neigh_lookup(&arp_tbl, &fib_nh->nh_gw, dev);
	if (!n) {
		n = neigh_create(&arp_tbl, &fib_nh->nh_gw, dev);
		if (IS_ERR(n))
			return PTR_ERR(n);
		neigh_event_send(n, NULL);
	}
	neigh_entry = mlxsw_sp_neigh_entry_lookup(mlxsw_sp, n);
	if (!neigh_entry) {
		neigh_release(n);
		return -EINVAL;
	}

	/* If that is the first nexthop connected to that neigh, add to
	 * nexthop_neighs_list
	 */
	if (list_empty(&neigh_entry->nexthop_list))
		list_add_tail(&neigh_entry->nexthop_neighs_list_node,
			      &mlxsw_sp->router.nexthop_neighs_list);

	nh->nh_grp = nh_grp;
	nh->neigh_entry = neigh_entry;
	list_add_tail(&nh->neigh_list_node, &neigh_entry->nexthop_list);
	read_lock_bh(&n->lock);
	nud_state = n->nud_state;
	read_unlock_bh(&n->lock);
	__mlxsw_sp_nexthop_neigh_update(nh, !(nud_state & NUD_VALID));

	return 0;
}

static void mlxsw_sp_nexthop_fini(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_nexthop *nh)
{
	struct mlxsw_sp_neigh_entry *neigh_entry = nh->neigh_entry;

	list_del(&nh->neigh_list_node);

	/* If that is the last nexthop connected to that neigh, remove from
	 * nexthop_neighs_list
	 */
	if (list_empty(&nh->neigh_entry->nexthop_list))
		list_del(&nh->neigh_entry->nexthop_neighs_list_node);

	neigh_release(neigh_entry->key.n);
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
	nh_grp->count = fi->fib_nhs;
	for (i = 0; i < nh_grp->count; i++) {
		nh = &nh_grp->nexthops[i];
		fib_nh = &fi->fib_nh[i];
		err = mlxsw_sp_nexthop_init(mlxsw_sp, nh_grp, nh, fib_nh);
		if (err)
			goto err_nexthop_init;
	}
	list_add_tail(&nh_grp->list, &mlxsw_sp->router.nexthop_group_list);
	mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh_grp);
	return nh_grp;

err_nexthop_init:
	for (i--; i >= 0; i--)
		mlxsw_sp_nexthop_fini(mlxsw_sp, nh);
	kfree(nh_grp);
	return ERR_PTR(err);
}

static void
mlxsw_sp_nexthop_group_destroy(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_nexthop_group *nh_grp)
{
	struct mlxsw_sp_nexthop *nh;
	int i;

	list_del(&nh_grp->list);
	for (i = 0; i < nh_grp->count; i++) {
		nh = &nh_grp->nexthops[i];
		mlxsw_sp_nexthop_fini(mlxsw_sp, nh);
	}
	kfree(nh_grp);
}

static bool mlxsw_sp_nexthop_match(struct mlxsw_sp_nexthop *nh,
				   struct fib_info *fi)
{
	int i;

	for (i = 0; i < fi->fib_nhs; i++) {
		struct fib_nh *fib_nh = &fi->fib_nh[i];
		struct neighbour *n = nh->neigh_entry->key.n;

		if (memcmp(n->primary_key, &fib_nh->nh_gw,
			   sizeof(fib_nh->nh_gw)) == 0 &&
		    n->dev == fib_nh->nh_dev)
			return true;
	}
	return false;
}

static bool mlxsw_sp_nexthop_group_match(struct mlxsw_sp_nexthop_group *nh_grp,
					 struct fib_info *fi)
{
	int i;

	if (nh_grp->count != fi->fib_nhs)
		return false;
	for (i = 0; i < nh_grp->count; i++) {
		struct mlxsw_sp_nexthop *nh = &nh_grp->nexthops[i];

		if (!mlxsw_sp_nexthop_match(nh, fi))
			return false;
	}
	return true;
}

static struct mlxsw_sp_nexthop_group *
mlxsw_sp_nexthop_group_find(struct mlxsw_sp *mlxsw_sp, struct fib_info *fi)
{
	struct mlxsw_sp_nexthop_group *nh_grp;

	list_for_each_entry(nh_grp, &mlxsw_sp->router.nexthop_group_list,
			    list) {
		if (mlxsw_sp_nexthop_group_match(nh_grp, fi))
			return nh_grp;
	}
	return NULL;
}

static int mlxsw_sp_nexthop_group_get(struct mlxsw_sp *mlxsw_sp,
				      struct mlxsw_sp_fib_entry *fib_entry,
				      struct fib_info *fi)
{
	struct mlxsw_sp_nexthop_group *nh_grp;

	nh_grp = mlxsw_sp_nexthop_group_find(mlxsw_sp, fi);
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

static int mlxsw_sp_fib_entry_op4_remote(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_fib_entry *fib_entry,
					 enum mlxsw_reg_ralue_op op)
{
	char ralue_pl[MLXSW_REG_RALUE_LEN];
	u32 *p_dip = (u32 *) fib_entry->key.addr;
	struct mlxsw_sp_vr *vr = fib_entry->vr;
	enum mlxsw_reg_ralue_trap_action trap_action;
	u16 trap_id = 0;
	u32 adjacency_index = 0;
	u16 ecmp_size = 0;

	/* In case the nexthop group adjacency index is valid, use it
	 * with provided ECMP size. Otherwise, setup trap and pass
	 * traffic to kernel.
	 */
	if (fib_entry->nh_group->adj_index_valid) {
		trap_action = MLXSW_REG_RALUE_TRAP_ACTION_NOP;
		adjacency_index = fib_entry->nh_group->adj_index;
		ecmp_size = fib_entry->nh_group->ecmp_size;
	} else {
		trap_action = MLXSW_REG_RALUE_TRAP_ACTION_TRAP;
		trap_id = MLXSW_TRAP_ID_RTR_INGRESS0;
	}

	mlxsw_reg_ralue_pack4(ralue_pl,
			      (enum mlxsw_reg_ralxx_protocol) vr->proto, op,
			      vr->id, fib_entry->key.prefix_len, *p_dip);
	mlxsw_reg_ralue_act_remote_pack(ralue_pl, trap_action, trap_id,
					adjacency_index, ecmp_size);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralue), ralue_pl);
}

static int mlxsw_sp_fib_entry_op4_local(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_fib_entry *fib_entry,
					enum mlxsw_reg_ralue_op op)
{
	char ralue_pl[MLXSW_REG_RALUE_LEN];
	u32 *p_dip = (u32 *) fib_entry->key.addr;
	struct mlxsw_sp_vr *vr = fib_entry->vr;

	mlxsw_reg_ralue_pack4(ralue_pl,
			      (enum mlxsw_reg_ralxx_protocol) vr->proto, op,
			      vr->id, fib_entry->key.prefix_len, *p_dip);
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

	mlxsw_reg_ralue_pack4(ralue_pl,
			      (enum mlxsw_reg_ralxx_protocol) vr->proto, op,
			      vr->id, fib_entry->key.prefix_len, *p_dip);
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
mlxsw_sp_router_fib4_entry_init(struct mlxsw_sp *mlxsw_sp,
				const struct fib_entry_notifier_info *fen_info,
				struct mlxsw_sp_fib_entry *fib_entry)
{
	struct fib_info *fi = fen_info->fi;
	struct mlxsw_sp_rif *r = NULL;
	int nhsel;
	int err;

	if (fen_info->type == RTN_LOCAL || fen_info->type == RTN_BROADCAST) {
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_TRAP;
		return 0;
	}
	if (fen_info->type != RTN_UNICAST)
		return -EINVAL;

	for (nhsel = 0; nhsel < fi->fib_nhs; nhsel++) {
		const struct fib_nh *nh = &fi->fib_nh[nhsel];

		if (!nh->nh_dev)
			continue;
		r = mlxsw_sp_rif_find_by_dev(mlxsw_sp, nh->nh_dev);
		if (!r) {
			/* In case router interface is not found for
			 * at least one of the nexthops, that means
			 * the nexthop points to some device unrelated
			 * to us. Set trap and pass the packets for
			 * this prefix to kernel.
			 */
			break;
		}
	}

	if (!r) {
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_TRAP;
		return 0;
	}

	if (fi->fib_scope != RT_SCOPE_UNIVERSE) {
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_LOCAL;
		fib_entry->rif = r->rif;
	} else {
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_REMOTE;
		err = mlxsw_sp_nexthop_group_get(mlxsw_sp, fib_entry, fi);
		if (err)
			return err;
	}
	fib_info_offload_inc(fen_info->fi);
	return 0;
}

static void
mlxsw_sp_router_fib4_entry_fini(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_fib_entry *fib_entry)
{
	if (fib_entry->type != MLXSW_SP_FIB_ENTRY_TYPE_TRAP)
		fib_info_offload_dec(fib_entry->fi);
	if (fib_entry->type == MLXSW_SP_FIB_ENTRY_TYPE_REMOTE)
		mlxsw_sp_nexthop_group_put(mlxsw_sp, fib_entry);
}

static struct mlxsw_sp_fib_entry *
mlxsw_sp_fib_entry_get(struct mlxsw_sp *mlxsw_sp,
		       const struct fib_entry_notifier_info *fen_info)
{
	struct mlxsw_sp_fib_entry *fib_entry;
	struct fib_info *fi = fen_info->fi;
	struct mlxsw_sp_vr *vr;
	int err;

	vr = mlxsw_sp_vr_get(mlxsw_sp, fen_info->dst_len, fen_info->tb_id,
			     MLXSW_SP_L3_PROTO_IPV4);
	if (IS_ERR(vr))
		return ERR_CAST(vr);

	fib_entry = mlxsw_sp_fib_entry_lookup(vr->fib, &fen_info->dst,
					      sizeof(fen_info->dst),
					      fen_info->dst_len, fi->fib_dev);
	if (fib_entry) {
		/* Already exists, just take a reference */
		fib_entry->ref_count++;
		return fib_entry;
	}
	fib_entry = mlxsw_sp_fib_entry_create(vr->fib, &fen_info->dst,
					      sizeof(fen_info->dst),
					      fen_info->dst_len, fi->fib_dev);
	if (!fib_entry) {
		err = -ENOMEM;
		goto err_fib_entry_create;
	}
	fib_entry->vr = vr;
	fib_entry->fi = fi;
	fib_entry->ref_count = 1;

	err = mlxsw_sp_router_fib4_entry_init(mlxsw_sp, fen_info, fib_entry);
	if (err)
		goto err_fib4_entry_init;

	return fib_entry;

err_fib4_entry_init:
	mlxsw_sp_fib_entry_destroy(fib_entry);
err_fib_entry_create:
	mlxsw_sp_vr_put(mlxsw_sp, vr);

	return ERR_PTR(err);
}

static struct mlxsw_sp_fib_entry *
mlxsw_sp_fib_entry_find(struct mlxsw_sp *mlxsw_sp,
			const struct fib_entry_notifier_info *fen_info)
{
	struct mlxsw_sp_vr *vr;

	vr = mlxsw_sp_vr_find(mlxsw_sp, fen_info->tb_id,
			      MLXSW_SP_L3_PROTO_IPV4);
	if (!vr)
		return NULL;

	return mlxsw_sp_fib_entry_lookup(vr->fib, &fen_info->dst,
					 sizeof(fen_info->dst),
					 fen_info->dst_len,
					 fen_info->fi->fib_dev);
}

static void mlxsw_sp_fib_entry_put(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_fib_entry *fib_entry)
{
	struct mlxsw_sp_vr *vr = fib_entry->vr;

	if (--fib_entry->ref_count == 0) {
		mlxsw_sp_router_fib4_entry_fini(mlxsw_sp, fib_entry);
		mlxsw_sp_fib_entry_destroy(fib_entry);
	}
	mlxsw_sp_vr_put(mlxsw_sp, vr);
}

static void mlxsw_sp_fib_entry_put_all(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_fib_entry *fib_entry)
{
	unsigned int last_ref_count;

	do {
		last_ref_count = fib_entry->ref_count;
		mlxsw_sp_fib_entry_put(mlxsw_sp, fib_entry);
	} while (last_ref_count != 1);
}

static int mlxsw_sp_router_fib4_add(struct mlxsw_sp *mlxsw_sp,
				    struct fib_entry_notifier_info *fen_info)
{
	struct mlxsw_sp_fib_entry *fib_entry;
	struct mlxsw_sp_vr *vr;
	int err;

	if (mlxsw_sp->router.aborted)
		return 0;

	fib_entry = mlxsw_sp_fib_entry_get(mlxsw_sp, fen_info);
	if (IS_ERR(fib_entry)) {
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to get FIB4 entry being added.\n");
		return PTR_ERR(fib_entry);
	}

	if (fib_entry->ref_count != 1)
		return 0;

	vr = fib_entry->vr;
	err = mlxsw_sp_fib_entry_insert(vr->fib, fib_entry);
	if (err) {
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to insert FIB4 entry being added.\n");
		goto err_fib_entry_insert;
	}
	err = mlxsw_sp_fib_entry_update(mlxsw_sp, fib_entry);
	if (err)
		goto err_fib_entry_add;
	return 0;

err_fib_entry_add:
	mlxsw_sp_fib_entry_remove(vr->fib, fib_entry);
err_fib_entry_insert:
	mlxsw_sp_fib_entry_put(mlxsw_sp, fib_entry);
	return err;
}

static void mlxsw_sp_router_fib4_del(struct mlxsw_sp *mlxsw_sp,
				     struct fib_entry_notifier_info *fen_info)
{
	struct mlxsw_sp_fib_entry *fib_entry;

	if (mlxsw_sp->router.aborted)
		return;

	fib_entry = mlxsw_sp_fib_entry_find(mlxsw_sp, fen_info);
	if (!fib_entry)
		return;

	if (fib_entry->ref_count == 1) {
		mlxsw_sp_fib_entry_del(mlxsw_sp, fib_entry);
		mlxsw_sp_fib_entry_remove(fib_entry->vr->fib, fib_entry);
	}

	mlxsw_sp_fib_entry_put(mlxsw_sp, fib_entry);
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

static void mlxsw_sp_router_fib_flush(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_resources *resources;
	struct mlxsw_sp_fib_entry *fib_entry;
	struct mlxsw_sp_fib_entry *tmp;
	struct mlxsw_sp_vr *vr;
	int i;

	resources = mlxsw_core_resources_get(mlxsw_sp->core);
	for (i = 0; i < resources->max_virtual_routers; i++) {
		vr = &mlxsw_sp->router.vrs[i];

		if (!vr->used)
			continue;

		list_for_each_entry_safe(fib_entry, tmp,
					 &vr->fib->entry_list, list) {
			bool do_break = &tmp->list == &vr->fib->entry_list;

			mlxsw_sp_fib_entry_del(mlxsw_sp, fib_entry);
			mlxsw_sp_fib_entry_remove(fib_entry->vr->fib,
						  fib_entry);
			mlxsw_sp_fib_entry_put_all(mlxsw_sp, fib_entry);
			if (do_break)
				break;
		}
	}
}

static void mlxsw_sp_router_fib4_abort(struct mlxsw_sp *mlxsw_sp)
{
	int err;

	mlxsw_sp_router_fib_flush(mlxsw_sp);
	mlxsw_sp->router.aborted = true;
	err = mlxsw_sp_router_set_abort_trap(mlxsw_sp);
	if (err)
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to set abort trap.\n");
}

static int __mlxsw_sp_router_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_resources *resources;
	char rgcr_pl[MLXSW_REG_RGCR_LEN];
	int err;

	resources = mlxsw_core_resources_get(mlxsw_sp->core);
	if (!resources->max_rif_valid)
		return -EIO;

	mlxsw_sp->rifs = kcalloc(resources->max_rif,
				 sizeof(struct mlxsw_sp_rif *), GFP_KERNEL);
	if (!mlxsw_sp->rifs)
		return -ENOMEM;

	mlxsw_reg_rgcr_pack(rgcr_pl, true);
	mlxsw_reg_rgcr_max_router_interfaces_set(rgcr_pl, resources->max_rif);
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
	struct mlxsw_resources *resources;
	char rgcr_pl[MLXSW_REG_RGCR_LEN];
	int i;

	mlxsw_reg_rgcr_pack(rgcr_pl, false);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rgcr), rgcr_pl);

	resources = mlxsw_core_resources_get(mlxsw_sp->core);
	for (i = 0; i < resources->max_rif; i++)
		WARN_ON_ONCE(mlxsw_sp->rifs[i]);

	kfree(mlxsw_sp->rifs);
}

static int mlxsw_sp_router_fib_event(struct notifier_block *nb,
				     unsigned long event, void *ptr)
{
	struct mlxsw_sp *mlxsw_sp = container_of(nb, struct mlxsw_sp, fib_nb);
	struct fib_entry_notifier_info *fen_info = ptr;
	int err;

	if (!net_eq(fen_info->info.net, &init_net))
		return NOTIFY_DONE;

	switch (event) {
	case FIB_EVENT_ENTRY_ADD:
		err = mlxsw_sp_router_fib4_add(mlxsw_sp, fen_info);
		if (err)
			mlxsw_sp_router_fib4_abort(mlxsw_sp);
		break;
	case FIB_EVENT_ENTRY_DEL:
		mlxsw_sp_router_fib4_del(mlxsw_sp, fen_info);
		break;
	case FIB_EVENT_RULE_ADD: /* fall through */
	case FIB_EVENT_RULE_DEL:
		mlxsw_sp_router_fib4_abort(mlxsw_sp);
		break;
	}
	return NOTIFY_DONE;
}

int mlxsw_sp_router_init(struct mlxsw_sp *mlxsw_sp)
{
	int err;

	INIT_LIST_HEAD(&mlxsw_sp->router.nexthop_neighs_list);
	INIT_LIST_HEAD(&mlxsw_sp->router.nexthop_group_list);
	err = __mlxsw_sp_router_init(mlxsw_sp);
	if (err)
		return err;

	mlxsw_sp_lpm_init(mlxsw_sp);
	err = mlxsw_sp_vrs_init(mlxsw_sp);
	if (err)
		goto err_vrs_init;

	err =  mlxsw_sp_neigh_init(mlxsw_sp);
	if (err)
		goto err_neigh_init;

	mlxsw_sp->fib_nb.notifier_call = mlxsw_sp_router_fib_event;
	register_fib_notifier(&mlxsw_sp->fib_nb);
	return 0;

err_neigh_init:
	mlxsw_sp_vrs_fini(mlxsw_sp);
err_vrs_init:
	__mlxsw_sp_router_fini(mlxsw_sp);
	return err;
}

void mlxsw_sp_router_fini(struct mlxsw_sp *mlxsw_sp)
{
	unregister_fib_notifier(&mlxsw_sp->fib_nb);
	mlxsw_sp_neigh_fini(mlxsw_sp);
	mlxsw_sp_vrs_fini(mlxsw_sp);
	__mlxsw_sp_router_fini(mlxsw_sp);
}
