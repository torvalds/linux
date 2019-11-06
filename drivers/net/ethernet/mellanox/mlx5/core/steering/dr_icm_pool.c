// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include "dr_types.h"

#define DR_ICM_MODIFY_HDR_ALIGN_BASE 64
#define DR_ICM_SYNC_THRESHOLD (64 * 1024 * 1024)

struct mlx5dr_icm_pool;

struct mlx5dr_icm_bucket {
	struct mlx5dr_icm_pool *pool;

	/* Chunks that aren't visible to HW not directly and not in cache */
	struct list_head free_list;
	unsigned int free_list_count;

	/* Used chunks, HW may be accessing this memory */
	struct list_head used_list;
	unsigned int used_list_count;

	/* HW may be accessing this memory but at some future,
	 * undetermined time, it might cease to do so. Before deciding to call
	 * sync_ste, this list is moved to sync_list
	 */
	struct list_head hot_list;
	unsigned int hot_list_count;

	/* Pending sync list, entries from the hot list are moved to this list.
	 * sync_ste is executed and then sync_list is concatenated to the free list
	 */
	struct list_head sync_list;
	unsigned int sync_list_count;

	u32 total_chunks;
	u32 num_of_entries;
	u32 entry_size;
	/* protect the ICM bucket */
	struct mutex mutex;
};

struct mlx5dr_icm_pool {
	struct mlx5dr_icm_bucket *buckets;
	enum mlx5dr_icm_type icm_type;
	enum mlx5dr_icm_chunk_size max_log_chunk_sz;
	enum mlx5dr_icm_chunk_size num_of_buckets;
	struct list_head icm_mr_list;
	/* protect the ICM MR list */
	struct mutex mr_mutex;
	struct mlx5dr_domain *dmn;
};

struct mlx5dr_icm_dm {
	u32 obj_id;
	enum mlx5_sw_icm_type type;
	phys_addr_t addr;
	size_t length;
};

struct mlx5dr_icm_mr {
	struct mlx5dr_icm_pool *pool;
	struct mlx5_core_mkey mkey;
	struct mlx5dr_icm_dm dm;
	size_t used_length;
	size_t length;
	u64 icm_start_addr;
	struct list_head mr_list;
};

static int dr_icm_create_dm_mkey(struct mlx5_core_dev *mdev,
				 u32 pd, u64 length, u64 start_addr, int mode,
				 struct mlx5_core_mkey *mkey)
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

static struct mlx5dr_icm_mr *
dr_icm_pool_mr_create(struct mlx5dr_icm_pool *pool,
		      enum mlx5_sw_icm_type type,
		      size_t align_base)
{
	struct mlx5_core_dev *mdev = pool->dmn->mdev;
	struct mlx5dr_icm_mr *icm_mr;
	size_t align_diff;
	int err;

	icm_mr = kvzalloc(sizeof(*icm_mr), GFP_KERNEL);
	if (!icm_mr)
		return NULL;

	icm_mr->pool = pool;
	INIT_LIST_HEAD(&icm_mr->mr_list);

	icm_mr->dm.type = type;

	/* 2^log_biggest_table * entry-size * double-for-alignment */
	icm_mr->dm.length = mlx5dr_icm_pool_chunk_size_to_byte(pool->max_log_chunk_sz,
							       pool->icm_type) * 2;

	err = mlx5_dm_sw_icm_alloc(mdev, icm_mr->dm.type, icm_mr->dm.length, 0,
				   &icm_mr->dm.addr, &icm_mr->dm.obj_id);
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

	/* align_base is always a power of 2 */
	align_diff = icm_mr->icm_start_addr & (align_base - 1);
	if (align_diff)
		icm_mr->used_length = align_base - align_diff;

	list_add_tail(&icm_mr->mr_list, &pool->icm_mr_list);

	return icm_mr;

free_dm:
	mlx5_dm_sw_icm_dealloc(mdev, icm_mr->dm.type, icm_mr->dm.length, 0,
			       icm_mr->dm.addr, icm_mr->dm.obj_id);
free_icm_mr:
	kvfree(icm_mr);
	return NULL;
}

static void dr_icm_pool_mr_destroy(struct mlx5dr_icm_mr *icm_mr)
{
	struct mlx5_core_dev *mdev = icm_mr->pool->dmn->mdev;
	struct mlx5dr_icm_dm *dm = &icm_mr->dm;

	list_del(&icm_mr->mr_list);
	mlx5_core_destroy_mkey(mdev, &icm_mr->mkey);
	mlx5_dm_sw_icm_dealloc(mdev, dm->type, dm->length, 0,
			       dm->addr, dm->obj_id);
	kvfree(icm_mr);
}

static int dr_icm_chunk_ste_init(struct mlx5dr_icm_chunk *chunk)
{
	struct mlx5dr_icm_bucket *bucket = chunk->bucket;

	chunk->ste_arr = kvzalloc(bucket->num_of_entries *
				  sizeof(chunk->ste_arr[0]), GFP_KERNEL);
	if (!chunk->ste_arr)
		return -ENOMEM;

	chunk->hw_ste_arr = kvzalloc(bucket->num_of_entries *
				     DR_STE_SIZE_REDUCED, GFP_KERNEL);
	if (!chunk->hw_ste_arr)
		goto out_free_ste_arr;

	chunk->miss_list = kvmalloc(bucket->num_of_entries *
				    sizeof(chunk->miss_list[0]), GFP_KERNEL);
	if (!chunk->miss_list)
		goto out_free_hw_ste_arr;

	return 0;

out_free_hw_ste_arr:
	kvfree(chunk->hw_ste_arr);
out_free_ste_arr:
	kvfree(chunk->ste_arr);
	return -ENOMEM;
}

static int dr_icm_chunks_create(struct mlx5dr_icm_bucket *bucket)
{
	size_t mr_free_size, mr_req_size, mr_row_size;
	struct mlx5dr_icm_pool *pool = bucket->pool;
	struct mlx5dr_icm_mr *icm_mr = NULL;
	struct mlx5dr_icm_chunk *chunk;
	enum mlx5_sw_icm_type dm_type;
	size_t align_base;
	int i, err = 0;

	mr_req_size = bucket->num_of_entries * bucket->entry_size;
	mr_row_size = mlx5dr_icm_pool_chunk_size_to_byte(pool->max_log_chunk_sz,
							 pool->icm_type);

	if (pool->icm_type == DR_ICM_TYPE_STE) {
		dm_type = MLX5_SW_ICM_TYPE_STEERING;
		/* Align base is the biggest chunk size / row size */
		align_base = mr_row_size;
	} else {
		dm_type = MLX5_SW_ICM_TYPE_HEADER_MODIFY;
		/* Align base is 64B */
		align_base = DR_ICM_MODIFY_HDR_ALIGN_BASE;
	}

	mutex_lock(&pool->mr_mutex);
	if (!list_empty(&pool->icm_mr_list)) {
		icm_mr = list_last_entry(&pool->icm_mr_list,
					 struct mlx5dr_icm_mr, mr_list);

		if (icm_mr)
			mr_free_size = icm_mr->dm.length - icm_mr->used_length;
	}

	if (!icm_mr || mr_free_size < mr_row_size) {
		icm_mr = dr_icm_pool_mr_create(pool, dm_type, align_base);
		if (!icm_mr) {
			err = -ENOMEM;
			goto out_err;
		}
	}

	/* Create memory aligned chunks */
	for (i = 0; i < mr_row_size / mr_req_size; i++) {
		chunk = kvzalloc(sizeof(*chunk), GFP_KERNEL);
		if (!chunk) {
			err = -ENOMEM;
			goto out_err;
		}

		chunk->bucket = bucket;
		chunk->rkey = icm_mr->mkey.key;
		/* mr start addr is zero based */
		chunk->mr_addr = icm_mr->used_length;
		chunk->icm_addr = (uintptr_t)icm_mr->icm_start_addr + icm_mr->used_length;
		icm_mr->used_length += mr_req_size;
		chunk->num_of_entries = bucket->num_of_entries;
		chunk->byte_size = chunk->num_of_entries * bucket->entry_size;

		if (pool->icm_type == DR_ICM_TYPE_STE) {
			err = dr_icm_chunk_ste_init(chunk);
			if (err)
				goto out_free_chunk;
		}

		INIT_LIST_HEAD(&chunk->chunk_list);
		list_add(&chunk->chunk_list, &bucket->free_list);
		bucket->free_list_count++;
		bucket->total_chunks++;
	}
	mutex_unlock(&pool->mr_mutex);
	return 0;

out_free_chunk:
	kvfree(chunk);
out_err:
	mutex_unlock(&pool->mr_mutex);
	return err;
}

static void dr_icm_chunk_ste_cleanup(struct mlx5dr_icm_chunk *chunk)
{
	kvfree(chunk->miss_list);
	kvfree(chunk->hw_ste_arr);
	kvfree(chunk->ste_arr);
}

static void dr_icm_chunk_destroy(struct mlx5dr_icm_chunk *chunk)
{
	struct mlx5dr_icm_bucket *bucket = chunk->bucket;

	list_del(&chunk->chunk_list);
	bucket->total_chunks--;

	if (bucket->pool->icm_type == DR_ICM_TYPE_STE)
		dr_icm_chunk_ste_cleanup(chunk);

	kvfree(chunk);
}

static void dr_icm_bucket_init(struct mlx5dr_icm_pool *pool,
			       struct mlx5dr_icm_bucket *bucket,
			       enum mlx5dr_icm_chunk_size chunk_size)
{
	if (pool->icm_type == DR_ICM_TYPE_STE)
		bucket->entry_size = DR_STE_SIZE;
	else
		bucket->entry_size = DR_MODIFY_ACTION_SIZE;

	bucket->num_of_entries = mlx5dr_icm_pool_chunk_size_to_entries(chunk_size);
	bucket->pool = pool;
	mutex_init(&bucket->mutex);
	INIT_LIST_HEAD(&bucket->free_list);
	INIT_LIST_HEAD(&bucket->used_list);
	INIT_LIST_HEAD(&bucket->hot_list);
	INIT_LIST_HEAD(&bucket->sync_list);
}

static void dr_icm_bucket_cleanup(struct mlx5dr_icm_bucket *bucket)
{
	struct mlx5dr_icm_chunk *chunk, *next;

	mutex_destroy(&bucket->mutex);
	list_splice_tail_init(&bucket->sync_list, &bucket->free_list);
	list_splice_tail_init(&bucket->hot_list, &bucket->free_list);

	list_for_each_entry_safe(chunk, next, &bucket->free_list, chunk_list)
		dr_icm_chunk_destroy(chunk);

	WARN_ON(bucket->total_chunks != 0);

	/* Cleanup of unreturned chunks */
	list_for_each_entry_safe(chunk, next, &bucket->used_list, chunk_list)
		dr_icm_chunk_destroy(chunk);
}

static u64 dr_icm_hot_mem_size(struct mlx5dr_icm_pool *pool)
{
	u64 hot_size = 0;
	int chunk_order;

	for (chunk_order = 0; chunk_order < pool->num_of_buckets; chunk_order++)
		hot_size += pool->buckets[chunk_order].hot_list_count *
			    mlx5dr_icm_pool_chunk_size_to_byte(chunk_order, pool->icm_type);

	return hot_size;
}

static bool dr_icm_reuse_hot_entries(struct mlx5dr_icm_pool *pool,
				     struct mlx5dr_icm_bucket *bucket)
{
	u64 bytes_for_sync;

	bytes_for_sync = dr_icm_hot_mem_size(pool);
	if (bytes_for_sync < DR_ICM_SYNC_THRESHOLD || !bucket->hot_list_count)
		return false;

	return true;
}

static void dr_icm_chill_bucket_start(struct mlx5dr_icm_bucket *bucket)
{
	list_splice_tail_init(&bucket->hot_list, &bucket->sync_list);
	bucket->sync_list_count += bucket->hot_list_count;
	bucket->hot_list_count = 0;
}

static void dr_icm_chill_bucket_end(struct mlx5dr_icm_bucket *bucket)
{
	list_splice_tail_init(&bucket->sync_list, &bucket->free_list);
	bucket->free_list_count += bucket->sync_list_count;
	bucket->sync_list_count = 0;
}

static void dr_icm_chill_bucket_abort(struct mlx5dr_icm_bucket *bucket)
{
	list_splice_tail_init(&bucket->sync_list, &bucket->hot_list);
	bucket->hot_list_count += bucket->sync_list_count;
	bucket->sync_list_count = 0;
}

static void dr_icm_chill_buckets_start(struct mlx5dr_icm_pool *pool,
				       struct mlx5dr_icm_bucket *cb,
				       bool buckets[DR_CHUNK_SIZE_MAX])
{
	struct mlx5dr_icm_bucket *bucket;
	int i;

	for (i = 0; i < pool->num_of_buckets; i++) {
		bucket = &pool->buckets[i];
		if (bucket == cb) {
			dr_icm_chill_bucket_start(bucket);
			continue;
		}

		/* Freeing the mutex is done at the end of that process, after
		 * sync_ste was executed at dr_icm_chill_buckets_end func.
		 */
		if (mutex_trylock(&bucket->mutex)) {
			dr_icm_chill_bucket_start(bucket);
			buckets[i] = true;
		}
	}
}

static void dr_icm_chill_buckets_end(struct mlx5dr_icm_pool *pool,
				     struct mlx5dr_icm_bucket *cb,
				     bool buckets[DR_CHUNK_SIZE_MAX])
{
	struct mlx5dr_icm_bucket *bucket;
	int i;

	for (i = 0; i < pool->num_of_buckets; i++) {
		bucket = &pool->buckets[i];
		if (bucket == cb) {
			dr_icm_chill_bucket_end(bucket);
			continue;
		}

		if (!buckets[i])
			continue;

		dr_icm_chill_bucket_end(bucket);
		mutex_unlock(&bucket->mutex);
	}
}

static void dr_icm_chill_buckets_abort(struct mlx5dr_icm_pool *pool,
				       struct mlx5dr_icm_bucket *cb,
				       bool buckets[DR_CHUNK_SIZE_MAX])
{
	struct mlx5dr_icm_bucket *bucket;
	int i;

	for (i = 0; i < pool->num_of_buckets; i++) {
		bucket = &pool->buckets[i];
		if (bucket == cb) {
			dr_icm_chill_bucket_abort(bucket);
			continue;
		}

		if (!buckets[i])
			continue;

		dr_icm_chill_bucket_abort(bucket);
		mutex_unlock(&bucket->mutex);
	}
}

/* Allocate an ICM chunk, each chunk holds a piece of ICM memory and
 * also memory used for HW STE management for optimizations.
 */
struct mlx5dr_icm_chunk *
mlx5dr_icm_alloc_chunk(struct mlx5dr_icm_pool *pool,
		       enum mlx5dr_icm_chunk_size chunk_size)
{
	struct mlx5dr_icm_chunk *chunk = NULL; /* Fix compilation warning */
	bool buckets[DR_CHUNK_SIZE_MAX] = {};
	struct mlx5dr_icm_bucket *bucket;
	int err;

	if (chunk_size > pool->max_log_chunk_sz)
		return NULL;

	bucket = &pool->buckets[chunk_size];

	mutex_lock(&bucket->mutex);

	/* Take chunk from pool if available, otherwise allocate new chunks */
	if (list_empty(&bucket->free_list)) {
		if (dr_icm_reuse_hot_entries(pool, bucket)) {
			dr_icm_chill_buckets_start(pool, bucket, buckets);
			err = mlx5dr_cmd_sync_steering(pool->dmn->mdev);
			if (err) {
				dr_icm_chill_buckets_abort(pool, bucket, buckets);
				mlx5dr_dbg(pool->dmn, "Sync_steering failed\n");
				chunk = NULL;
				goto out;
			}
			dr_icm_chill_buckets_end(pool, bucket, buckets);
		} else {
			dr_icm_chunks_create(bucket);
		}
	}

	if (!list_empty(&bucket->free_list)) {
		chunk = list_last_entry(&bucket->free_list,
					struct mlx5dr_icm_chunk,
					chunk_list);
		if (chunk) {
			list_del_init(&chunk->chunk_list);
			list_add_tail(&chunk->chunk_list, &bucket->used_list);
			bucket->free_list_count--;
			bucket->used_list_count++;
		}
	}
out:
	mutex_unlock(&bucket->mutex);
	return chunk;
}

void mlx5dr_icm_free_chunk(struct mlx5dr_icm_chunk *chunk)
{
	struct mlx5dr_icm_bucket *bucket = chunk->bucket;

	if (bucket->pool->icm_type == DR_ICM_TYPE_STE) {
		memset(chunk->ste_arr, 0,
		       bucket->num_of_entries * sizeof(chunk->ste_arr[0]));
		memset(chunk->hw_ste_arr, 0,
		       bucket->num_of_entries * DR_STE_SIZE_REDUCED);
	}

	mutex_lock(&bucket->mutex);
	list_del_init(&chunk->chunk_list);
	list_add_tail(&chunk->chunk_list, &bucket->hot_list);
	bucket->hot_list_count++;
	bucket->used_list_count--;
	mutex_unlock(&bucket->mutex);
}

struct mlx5dr_icm_pool *mlx5dr_icm_pool_create(struct mlx5dr_domain *dmn,
					       enum mlx5dr_icm_type icm_type)
{
	enum mlx5dr_icm_chunk_size max_log_chunk_sz;
	struct mlx5dr_icm_pool *pool;
	int i;

	if (icm_type == DR_ICM_TYPE_STE)
		max_log_chunk_sz = dmn->info.max_log_sw_icm_sz;
	else
		max_log_chunk_sz = dmn->info.max_log_action_icm_sz;

	pool = kvzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return NULL;

	pool->buckets = kcalloc(max_log_chunk_sz + 1,
				sizeof(pool->buckets[0]),
				GFP_KERNEL);
	if (!pool->buckets)
		goto free_pool;

	pool->dmn = dmn;
	pool->icm_type = icm_type;
	pool->max_log_chunk_sz = max_log_chunk_sz;
	pool->num_of_buckets = max_log_chunk_sz + 1;
	INIT_LIST_HEAD(&pool->icm_mr_list);

	for (i = 0; i < pool->num_of_buckets; i++)
		dr_icm_bucket_init(pool, &pool->buckets[i], i);

	mutex_init(&pool->mr_mutex);

	return pool;

free_pool:
	kvfree(pool);
	return NULL;
}

void mlx5dr_icm_pool_destroy(struct mlx5dr_icm_pool *pool)
{
	struct mlx5dr_icm_mr *icm_mr, *next;
	int i;

	mutex_destroy(&pool->mr_mutex);

	list_for_each_entry_safe(icm_mr, next, &pool->icm_mr_list, mr_list)
		dr_icm_pool_mr_destroy(icm_mr);

	for (i = 0; i < pool->num_of_buckets; i++)
		dr_icm_bucket_cleanup(&pool->buckets[i]);

	kfree(pool->buckets);
	kvfree(pool);
}
