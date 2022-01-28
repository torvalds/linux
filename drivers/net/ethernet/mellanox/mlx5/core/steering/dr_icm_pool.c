// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include "dr_types.h"

#define DR_ICM_MODIFY_HDR_ALIGN_BASE 64

struct mlx5dr_icm_pool {
	enum mlx5dr_icm_type icm_type;
	enum mlx5dr_icm_chunk_size max_log_chunk_sz;
	struct mlx5dr_domain *dmn;
	/* memory management */
	struct mutex mutex; /* protect the ICM pool and ICM buddy */
	struct list_head buddy_mem_list;
	u64 hot_memory_size;
};

struct mlx5dr_icm_dm {
	u32 obj_id;
	enum mlx5_sw_icm_type type;
	phys_addr_t addr;
	size_t length;
};

struct mlx5dr_icm_mr {
	u32 mkey;
	struct mlx5dr_icm_dm dm;
	struct mlx5dr_domain *dmn;
	size_t length;
	u64 icm_start_addr;
};

static int dr_icm_create_dm_mkey(struct mlx5_core_dev *mdev,
				 u32 pd, u64 length, u64 start_addr, int mode,
				 u32 *mkey)
{
	u32 inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	u32 in[MLX5_ST_SZ_DW(create_mkey_in)] = {};
	void *mkc;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);

	MLX5_SET(mkc, mkc, access_mode_1_0, mode);
	MLX5_SET(mkc, mkc, access_mode_4_2, (mode >> 2) & 0x7);
	MLX5_SET(mkc, mkc, lw, 1);
	MLX5_SET(mkc, mkc, lr, 1);
	if (mode == MLX5_MKC_ACCESS_MODE_SW_ICM) {
		MLX5_SET(mkc, mkc, rw, 1);
		MLX5_SET(mkc, mkc, rr, 1);
	}

	MLX5_SET64(mkc, mkc, len, length);
	MLX5_SET(mkc, mkc, pd, pd);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET64(mkc, mkc, start_addr, start_addr);

	return mlx5_core_create_mkey(mdev, mkey, in, inlen);
}

u64 mlx5dr_icm_pool_get_chunk_mr_addr(struct mlx5dr_icm_chunk *chunk)
{
	u32 offset = mlx5dr_icm_pool_dm_type_to_entry_size(chunk->buddy_mem->pool->icm_type);

	return (u64)offset * chunk->seg;
}

u32 mlx5dr_icm_pool_get_chunk_rkey(struct mlx5dr_icm_chunk *chunk)
{
	return chunk->buddy_mem->icm_mr->mkey;
}

u64 mlx5dr_icm_pool_get_chunk_icm_addr(struct mlx5dr_icm_chunk *chunk)
{
	u32 size = mlx5dr_icm_pool_dm_type_to_entry_size(chunk->buddy_mem->pool->icm_type);

	return (u64)chunk->buddy_mem->icm_mr->icm_start_addr + size * chunk->seg;
}

static struct mlx5dr_icm_mr *
dr_icm_pool_mr_create(struct mlx5dr_icm_pool *pool)
{
	struct mlx5_core_dev *mdev = pool->dmn->mdev;
	enum mlx5_sw_icm_type dm_type;
	struct mlx5dr_icm_mr *icm_mr;
	size_t log_align_base;
	int err;

	icm_mr = kvzalloc(sizeof(*icm_mr), GFP_KERNEL);
	if (!icm_mr)
		return NULL;

	icm_mr->dmn = pool->dmn;

	icm_mr->dm.length = mlx5dr_icm_pool_chunk_size_to_byte(pool->max_log_chunk_sz,
							       pool->icm_type);

	if (pool->icm_type == DR_ICM_TYPE_STE) {
		dm_type = MLX5_SW_ICM_TYPE_STEERING;
		log_align_base = ilog2(icm_mr->dm.length);
	} else {
		dm_type = MLX5_SW_ICM_TYPE_HEADER_MODIFY;
		/* Align base is 64B */
		log_align_base = ilog2(DR_ICM_MODIFY_HDR_ALIGN_BASE);
	}
	icm_mr->dm.type = dm_type;

	err = mlx5_dm_sw_icm_alloc(mdev, icm_mr->dm.type, icm_mr->dm.length,
				   log_align_base, 0, &icm_mr->dm.addr,
				   &icm_mr->dm.obj_id);
	if (err) {
		mlx5dr_err(pool->dmn, "Failed to allocate SW ICM memory, err (%d)\n", err);
		goto free_icm_mr;
	}

	/* Register device memory */
	err = dr_icm_create_dm_mkey(mdev, pool->dmn->pdn,
				    icm_mr->dm.length,
				    icm_mr->dm.addr,
				    MLX5_MKC_ACCESS_MODE_SW_ICM,
				    &icm_mr->mkey);
	if (err) {
		mlx5dr_err(pool->dmn, "Failed to create SW ICM MKEY, err (%d)\n", err);
		goto free_dm;
	}

	icm_mr->icm_start_addr = icm_mr->dm.addr;

	if (icm_mr->icm_start_addr & (BIT(log_align_base) - 1)) {
		mlx5dr_err(pool->dmn, "Failed to get Aligned ICM mem (asked: %zu)\n",
			   log_align_base);
		goto free_mkey;
	}

	return icm_mr;

free_mkey:
	mlx5_core_destroy_mkey(mdev, icm_mr->mkey);
free_dm:
	mlx5_dm_sw_icm_dealloc(mdev, icm_mr->dm.type, icm_mr->dm.length, 0,
			       icm_mr->dm.addr, icm_mr->dm.obj_id);
free_icm_mr:
	kvfree(icm_mr);
	return NULL;
}

static void dr_icm_pool_mr_destroy(struct mlx5dr_icm_mr *icm_mr)
{
	struct mlx5_core_dev *mdev = icm_mr->dmn->mdev;
	struct mlx5dr_icm_dm *dm = &icm_mr->dm;

	mlx5_core_destroy_mkey(mdev, icm_mr->mkey);
	mlx5_dm_sw_icm_dealloc(mdev, dm->type, dm->length, 0,
			       dm->addr, dm->obj_id);
	kvfree(icm_mr);
}

static int dr_icm_buddy_get_ste_size(struct mlx5dr_icm_buddy_mem *buddy)
{
	/* We support only one type of STE size, both for ConnectX-5 and later
	 * devices. Once the support for match STE which has a larger tag is
	 * added (32B instead of 16B), the STE size for devices later than
	 * ConnectX-5 needs to account for that.
	 */
	return DR_STE_SIZE_REDUCED;
}

static void dr_icm_chunk_ste_init(struct mlx5dr_icm_chunk *chunk, int offset)
{
	struct mlx5dr_icm_buddy_mem *buddy = chunk->buddy_mem;
	int index = offset / DR_STE_SIZE;

	chunk->ste_arr = &buddy->ste_arr[index];
	chunk->miss_list = &buddy->miss_list[index];
	chunk->hw_ste_arr = buddy->hw_ste_arr +
			    index * dr_icm_buddy_get_ste_size(buddy);
}

static void dr_icm_chunk_ste_cleanup(struct mlx5dr_icm_chunk *chunk)
{
	struct mlx5dr_icm_buddy_mem *buddy = chunk->buddy_mem;

	memset(chunk->hw_ste_arr, 0,
	       chunk->num_of_entries * dr_icm_buddy_get_ste_size(buddy));
	memset(chunk->ste_arr, 0,
	       chunk->num_of_entries * sizeof(chunk->ste_arr[0]));
}

static enum mlx5dr_icm_type
get_chunk_icm_type(struct mlx5dr_icm_chunk *chunk)
{
	return chunk->buddy_mem->pool->icm_type;
}

static void dr_icm_chunk_destroy(struct mlx5dr_icm_chunk *chunk,
				 struct mlx5dr_icm_buddy_mem *buddy)
{
	enum mlx5dr_icm_type icm_type = get_chunk_icm_type(chunk);

	buddy->used_memory -= chunk->byte_size;
	list_del(&chunk->chunk_list);

	if (icm_type == DR_ICM_TYPE_STE)
		dr_icm_chunk_ste_cleanup(chunk);

	kvfree(chunk);
}

static int dr_icm_buddy_init_ste_cache(struct mlx5dr_icm_buddy_mem *buddy)
{
	int num_of_entries =
		mlx5dr_icm_pool_chunk_size_to_entries(buddy->pool->max_log_chunk_sz);

	buddy->ste_arr = kvcalloc(num_of_entries,
				  sizeof(struct mlx5dr_ste), GFP_KERNEL);
	if (!buddy->ste_arr)
		return -ENOMEM;

	/* Preallocate full STE size on non-ConnectX-5 devices since
	 * we need to support both full and reduced with the same cache.
	 */
	buddy->hw_ste_arr = kvcalloc(num_of_entries,
				     dr_icm_buddy_get_ste_size(buddy), GFP_KERNEL);
	if (!buddy->hw_ste_arr)
		goto free_ste_arr;

	buddy->miss_list = kvmalloc(num_of_entries * sizeof(struct list_head), GFP_KERNEL);
	if (!buddy->miss_list)
		goto free_hw_ste_arr;

	return 0;

free_hw_ste_arr:
	kvfree(buddy->hw_ste_arr);
free_ste_arr:
	kvfree(buddy->ste_arr);
	return -ENOMEM;
}

static void dr_icm_buddy_cleanup_ste_cache(struct mlx5dr_icm_buddy_mem *buddy)
{
	kvfree(buddy->ste_arr);
	kvfree(buddy->hw_ste_arr);
	kvfree(buddy->miss_list);
}

static int dr_icm_buddy_create(struct mlx5dr_icm_pool *pool)
{
	struct mlx5dr_icm_buddy_mem *buddy;
	struct mlx5dr_icm_mr *icm_mr;

	icm_mr = dr_icm_pool_mr_create(pool);
	if (!icm_mr)
		return -ENOMEM;

	buddy = kvzalloc(sizeof(*buddy), GFP_KERNEL);
	if (!buddy)
		goto free_mr;

	if (mlx5dr_buddy_init(buddy, pool->max_log_chunk_sz))
		goto err_free_buddy;

	buddy->icm_mr = icm_mr;
	buddy->pool = pool;

	if (pool->icm_type == DR_ICM_TYPE_STE) {
		/* Reduce allocations by preallocating and reusing the STE structures */
		if (dr_icm_buddy_init_ste_cache(buddy))
			goto err_cleanup_buddy;
	}

	/* add it to the -start- of the list in order to search in it first */
	list_add(&buddy->list_node, &pool->buddy_mem_list);

	return 0;

err_cleanup_buddy:
	mlx5dr_buddy_cleanup(buddy);
err_free_buddy:
	kvfree(buddy);
free_mr:
	dr_icm_pool_mr_destroy(icm_mr);
	return -ENOMEM;
}

static void dr_icm_buddy_destroy(struct mlx5dr_icm_buddy_mem *buddy)
{
	struct mlx5dr_icm_chunk *chunk, *next;

	list_for_each_entry_safe(chunk, next, &buddy->hot_list, chunk_list)
		dr_icm_chunk_destroy(chunk, buddy);

	list_for_each_entry_safe(chunk, next, &buddy->used_list, chunk_list)
		dr_icm_chunk_destroy(chunk, buddy);

	dr_icm_pool_mr_destroy(buddy->icm_mr);

	mlx5dr_buddy_cleanup(buddy);

	if (buddy->pool->icm_type == DR_ICM_TYPE_STE)
		dr_icm_buddy_cleanup_ste_cache(buddy);

	kvfree(buddy);
}

static struct mlx5dr_icm_chunk *
dr_icm_chunk_create(struct mlx5dr_icm_pool *pool,
		    enum mlx5dr_icm_chunk_size chunk_size,
		    struct mlx5dr_icm_buddy_mem *buddy_mem_pool,
		    unsigned int seg)
{
	struct mlx5dr_icm_chunk *chunk;
	int offset;

	chunk = kvzalloc(sizeof(*chunk), GFP_KERNEL);
	if (!chunk)
		return NULL;

	offset = mlx5dr_icm_pool_dm_type_to_entry_size(pool->icm_type) * seg;

	chunk->num_of_entries =
		mlx5dr_icm_pool_chunk_size_to_entries(chunk_size);
	chunk->byte_size =
		mlx5dr_icm_pool_chunk_size_to_byte(chunk_size, pool->icm_type);
	chunk->seg = seg;
	chunk->buddy_mem = buddy_mem_pool;

	if (pool->icm_type == DR_ICM_TYPE_STE)
		dr_icm_chunk_ste_init(chunk, offset);

	buddy_mem_pool->used_memory += chunk->byte_size;
	INIT_LIST_HEAD(&chunk->chunk_list);

	/* chunk now is part of the used_list */
	list_add_tail(&chunk->chunk_list, &buddy_mem_pool->used_list);

	return chunk;
}

static bool dr_icm_pool_is_sync_required(struct mlx5dr_icm_pool *pool)
{
	int allow_hot_size;

	/* sync when hot memory reaches half of the pool size */
	allow_hot_size =
		mlx5dr_icm_pool_chunk_size_to_byte(pool->max_log_chunk_sz,
						   pool->icm_type) / 2;

	return pool->hot_memory_size > allow_hot_size;
}

static int dr_icm_pool_sync_all_buddy_pools(struct mlx5dr_icm_pool *pool)
{
	struct mlx5dr_icm_buddy_mem *buddy, *tmp_buddy;
	int err;

	err = mlx5dr_cmd_sync_steering(pool->dmn->mdev);
	if (err) {
		mlx5dr_err(pool->dmn, "Failed to sync to HW (err: %d)\n", err);
		return err;
	}

	list_for_each_entry_safe(buddy, tmp_buddy, &pool->buddy_mem_list, list_node) {
		struct mlx5dr_icm_chunk *chunk, *tmp_chunk;

		list_for_each_entry_safe(chunk, tmp_chunk, &buddy->hot_list, chunk_list) {
			mlx5dr_buddy_free_mem(buddy, chunk->seg,
					      ilog2(chunk->num_of_entries));
			pool->hot_memory_size -= chunk->byte_size;
			dr_icm_chunk_destroy(chunk, buddy);
		}

		if (!buddy->used_memory && pool->icm_type == DR_ICM_TYPE_STE)
			dr_icm_buddy_destroy(buddy);
	}

	return 0;
}

static int dr_icm_handle_buddies_get_mem(struct mlx5dr_icm_pool *pool,
					 enum mlx5dr_icm_chunk_size chunk_size,
					 struct mlx5dr_icm_buddy_mem **buddy,
					 unsigned int *seg)
{
	struct mlx5dr_icm_buddy_mem *buddy_mem_pool;
	bool new_mem = false;
	int err;

alloc_buddy_mem:
	/* find the next free place from the buddy list */
	list_for_each_entry(buddy_mem_pool, &pool->buddy_mem_list, list_node) {
		err = mlx5dr_buddy_alloc_mem(buddy_mem_pool,
					     chunk_size, seg);
		if (!err)
			goto found;

		if (WARN_ON(new_mem)) {
			/* We have new memory pool, first in the list */
			mlx5dr_err(pool->dmn,
				   "No memory for order: %d\n",
				   chunk_size);
			goto out;
		}
	}

	/* no more available allocators in that pool, create new */
	err = dr_icm_buddy_create(pool);
	if (err) {
		mlx5dr_err(pool->dmn,
			   "Failed creating buddy for order %d\n",
			   chunk_size);
		goto out;
	}

	/* mark we have new memory, first in list */
	new_mem = true;
	goto alloc_buddy_mem;

found:
	*buddy = buddy_mem_pool;
out:
	return err;
}

/* Allocate an ICM chunk, each chunk holds a piece of ICM memory and
 * also memory used for HW STE management for optimizations.
 */
struct mlx5dr_icm_chunk *
mlx5dr_icm_alloc_chunk(struct mlx5dr_icm_pool *pool,
		       enum mlx5dr_icm_chunk_size chunk_size)
{
	struct mlx5dr_icm_chunk *chunk = NULL;
	struct mlx5dr_icm_buddy_mem *buddy;
	unsigned int seg;
	int ret;

	if (chunk_size > pool->max_log_chunk_sz)
		return NULL;

	mutex_lock(&pool->mutex);
	/* find mem, get back the relevant buddy pool and seg in that mem */
	ret = dr_icm_handle_buddies_get_mem(pool, chunk_size, &buddy, &seg);
	if (ret)
		goto out;

	chunk = dr_icm_chunk_create(pool, chunk_size, buddy, seg);
	if (!chunk)
		goto out_err;

	goto out;

out_err:
	mlx5dr_buddy_free_mem(buddy, seg, chunk_size);
out:
	mutex_unlock(&pool->mutex);
	return chunk;
}

void mlx5dr_icm_free_chunk(struct mlx5dr_icm_chunk *chunk)
{
	struct mlx5dr_icm_buddy_mem *buddy = chunk->buddy_mem;
	struct mlx5dr_icm_pool *pool = buddy->pool;

	/* move the memory to the waiting list AKA "hot" */
	mutex_lock(&pool->mutex);
	list_move_tail(&chunk->chunk_list, &buddy->hot_list);
	pool->hot_memory_size += chunk->byte_size;

	/* Check if we have chunks that are waiting for sync-ste */
	if (dr_icm_pool_is_sync_required(pool))
		dr_icm_pool_sync_all_buddy_pools(pool);

	mutex_unlock(&pool->mutex);
}

struct mlx5dr_icm_pool *mlx5dr_icm_pool_create(struct mlx5dr_domain *dmn,
					       enum mlx5dr_icm_type icm_type)
{
	enum mlx5dr_icm_chunk_size max_log_chunk_sz;
	struct mlx5dr_icm_pool *pool;

	if (icm_type == DR_ICM_TYPE_STE)
		max_log_chunk_sz = dmn->info.max_log_sw_icm_sz;
	else
		max_log_chunk_sz = dmn->info.max_log_action_icm_sz;

	pool = kvzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return NULL;

	pool->dmn = dmn;
	pool->icm_type = icm_type;
	pool->max_log_chunk_sz = max_log_chunk_sz;

	INIT_LIST_HEAD(&pool->buddy_mem_list);

	mutex_init(&pool->mutex);

	return pool;
}

void mlx5dr_icm_pool_destroy(struct mlx5dr_icm_pool *pool)
{
	struct mlx5dr_icm_buddy_mem *buddy, *tmp_buddy;

	list_for_each_entry_safe(buddy, tmp_buddy, &pool->buddy_mem_list, list_node)
		dr_icm_buddy_destroy(buddy);

	mutex_destroy(&pool->mutex);
	kvfree(pool);
}
