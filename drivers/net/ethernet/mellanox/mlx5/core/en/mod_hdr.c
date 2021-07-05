// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2020 Mellanox Technologies

#include <linux/jhash.h>
#include "mod_hdr.h"

#define MLX5_MH_ACT_SZ MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto)

struct mod_hdr_key {
	int num_actions;
	void *actions;
};

struct mlx5e_mod_hdr_handle {
	/* a node of a hash table which keeps all the mod_hdr entries */
	struct hlist_node mod_hdr_hlist;

	struct mod_hdr_key key;

	struct mlx5_modify_hdr *modify_hdr;

	refcount_t refcnt;
	struct completion res_ready;
	int compl_result;
};

static u32 hash_mod_hdr_info(struct mod_hdr_key *key)
{
	return jhash(key->actions,
		     key->num_actions * MLX5_MH_ACT_SZ, 0);
}

static int cmp_mod_hdr_info(struct mod_hdr_key *a, struct mod_hdr_key *b)
{
	if (a->num_actions != b->num_actions)
		return 1;

	return memcmp(a->actions, b->actions,
		      a->num_actions * MLX5_MH_ACT_SZ);
}

void mlx5e_mod_hdr_tbl_init(struct mod_hdr_tbl *tbl)
{
	mutex_init(&tbl->lock);
	hash_init(tbl->hlist);
}

void mlx5e_mod_hdr_tbl_destroy(struct mod_hdr_tbl *tbl)
{
	mutex_destroy(&tbl->lock);
}

static struct mlx5e_mod_hdr_handle *mod_hdr_get(struct mod_hdr_tbl *tbl,
						struct mod_hdr_key *key,
						u32 hash_key)
{
	struct mlx5e_mod_hdr_handle *mh, *found = NULL;

	hash_for_each_possible(tbl->hlist, mh, mod_hdr_hlist, hash_key) {
		if (!cmp_mod_hdr_info(&mh->key, key)) {
			refcount_inc(&mh->refcnt);
			found = mh;
			break;
		}
	}

	return found;
}

struct mlx5e_mod_hdr_handle *
mlx5e_mod_hdr_attach(struct mlx5_core_dev *mdev,
		     struct mod_hdr_tbl *tbl,
		     enum mlx5_flow_namespace_type namespace,
		     struct mlx5e_tc_mod_hdr_acts *mod_hdr_acts)
{
	int num_actions, actions_size, err;
	struct mlx5e_mod_hdr_handle *mh;
	struct mod_hdr_key key;
	u32 hash_key;

	num_actions  = mod_hdr_acts->num_actions;
	actions_size = MLX5_MH_ACT_SZ * num_actions;

	key.actions = mod_hdr_acts->actions;
	key.num_actions = num_actions;

	hash_key = hash_mod_hdr_info(&key);

	mutex_lock(&tbl->lock);
	mh = mod_hdr_get(tbl, &key, hash_key);
	if (mh) {
		mutex_unlock(&tbl->lock);
		wait_for_completion(&mh->res_ready);

		if (mh->compl_result < 0) {
			err = -EREMOTEIO;
			goto attach_header_err;
		}
		goto attach_header;
	}

	mh = kzalloc(sizeof(*mh) + actions_size, GFP_KERNEL);
	if (!mh) {
		mutex_unlock(&tbl->lock);
		return ERR_PTR(-ENOMEM);
	}

	mh->key.actions = (void *)mh + sizeof(*mh);
	memcpy(mh->key.actions, key.actions, actions_size);
	mh->key.num_actions = num_actions;
	refcount_set(&mh->refcnt, 1);
	init_completion(&mh->res_ready);

	hash_add(tbl->hlist, &mh->mod_hdr_hlist, hash_key);
	mutex_unlock(&tbl->lock);

	mh->modify_hdr = mlx5_modify_header_alloc(mdev, namespace,
						  mh->key.num_actions,
						  mh->key.actions);
	if (IS_ERR(mh->modify_hdr)) {
		err = PTR_ERR(mh->modify_hdr);
		mh->compl_result = err;
		goto alloc_header_err;
	}
	mh->compl_result = 1;
	complete_all(&mh->res_ready);

attach_header:
	return mh;

alloc_header_err:
	complete_all(&mh->res_ready);
attach_header_err:
	mlx5e_mod_hdr_detach(mdev, tbl, mh);
	return ERR_PTR(err);
}

void mlx5e_mod_hdr_detach(struct mlx5_core_dev *mdev,
			  struct mod_hdr_tbl *tbl,
			  struct mlx5e_mod_hdr_handle *mh)
{
	if (!refcount_dec_and_mutex_lock(&mh->refcnt, &tbl->lock))
		return;
	hash_del(&mh->mod_hdr_hlist);
	mutex_unlock(&tbl->lock);

	if (mh->compl_result > 0)
		mlx5_modify_header_dealloc(mdev, mh->modify_hdr);

	kfree(mh);
}

struct mlx5_modify_hdr *mlx5e_mod_hdr_get(struct mlx5e_mod_hdr_handle *mh)
{
	return mh->modify_hdr;
}

char *
mlx5e_mod_hdr_alloc(struct mlx5_core_dev *mdev, int namespace,
		    struct mlx5e_tc_mod_hdr_acts *mod_hdr_acts)
{
	int new_num_actions, max_hw_actions;
	size_t new_sz, old_sz;
	void *ret;

	if (mod_hdr_acts->num_actions < mod_hdr_acts->max_actions)
		goto out;

	max_hw_actions = mlx5e_mod_hdr_max_actions(mdev, namespace);
	new_num_actions = min(max_hw_actions,
			      mod_hdr_acts->actions ?
			      mod_hdr_acts->max_actions * 2 : 1);
	if (mod_hdr_acts->max_actions == new_num_actions)
		return ERR_PTR(-ENOSPC);

	new_sz = MLX5_MH_ACT_SZ * new_num_actions;
	old_sz = mod_hdr_acts->max_actions * MLX5_MH_ACT_SZ;

	ret = krealloc(mod_hdr_acts->actions, new_sz, GFP_KERNEL);
	if (!ret)
		return ERR_PTR(-ENOMEM);

	memset(ret + old_sz, 0, new_sz - old_sz);
	mod_hdr_acts->actions = ret;
	mod_hdr_acts->max_actions = new_num_actions;

out:
	return mod_hdr_acts->actions + (mod_hdr_acts->num_actions * MLX5_MH_ACT_SZ);
}

void
mlx5e_mod_hdr_dealloc(struct mlx5e_tc_mod_hdr_acts *mod_hdr_acts)
{
	kfree(mod_hdr_acts->actions);
	mod_hdr_acts->actions = NULL;
	mod_hdr_acts->num_actions = 0;
	mod_hdr_acts->max_actions = 0;
}

char *
mlx5e_mod_hdr_get_item(struct mlx5e_tc_mod_hdr_acts *mod_hdr_acts, int pos)
{
	return mod_hdr_acts->actions + (pos * MLX5_MH_ACT_SZ);
}
