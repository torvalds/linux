// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/spinlock.h>

#include "spectrum_cnt.h"

struct mlxsw_sp_counter_sub_pool {
	unsigned int base_index;
	unsigned int size;
	enum mlxsw_res_id entry_size_res_id;
	unsigned int entry_size;
	unsigned int bank_count;
};

struct mlxsw_sp_counter_pool {
	unsigned int pool_size;
	unsigned long *usage; /* Usage bitmap */
	spinlock_t counter_pool_lock; /* Protects counter pool allocations */
	unsigned int sub_pools_count;
	struct mlxsw_sp_counter_sub_pool sub_pools[];
};

static const struct mlxsw_sp_counter_sub_pool mlxsw_sp_counter_sub_pools[] = {
	[MLXSW_SP_COUNTER_SUB_POOL_FLOW] = {
		.entry_size_res_id = MLXSW_RES_ID_COUNTER_SIZE_PACKETS_BYTES,
		.bank_count = 6,
	},
	[MLXSW_SP_COUNTER_SUB_POOL_RIF] = {
		.entry_size_res_id = MLXSW_RES_ID_COUNTER_SIZE_ROUTER_BASIC,
		.bank_count = 2,
	}
};

static int mlxsw_sp_counter_pool_validate(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_counter_pool *pool = mlxsw_sp->counter_pool;
	unsigned int total_bank_config = 0;
	unsigned int pool_size;
	unsigned int bank_size;
	int i;

	pool_size = MLXSW_CORE_RES_GET(mlxsw_sp->core, COUNTER_POOL_SIZE);
	bank_size = MLXSW_CORE_RES_GET(mlxsw_sp->core, COUNTER_BANK_SIZE);
	/* Check config is valid, no bank over subscription */
	for (i = 0; i < pool->sub_pools_count; i++)
		total_bank_config += pool->sub_pools[i].bank_count;
	if (total_bank_config > pool_size / bank_size + 1)
		return -EINVAL;
	return 0;
}

static int mlxsw_sp_counter_sub_pools_prepare(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_counter_pool *pool = mlxsw_sp->counter_pool;
	struct mlxsw_sp_counter_sub_pool *sub_pool;
	enum mlxsw_res_id res_id;
	int i;

	for (i = 0; i < pool->sub_pools_count; i++) {
		sub_pool = &pool->sub_pools[i];
		res_id = sub_pool->entry_size_res_id;

		if (!mlxsw_core_res_valid(mlxsw_sp->core, res_id))
			return -EIO;
		sub_pool->entry_size = mlxsw_core_res_get(mlxsw_sp->core,
							  res_id);
	}
	return 0;
}

int mlxsw_sp_counter_pool_init(struct mlxsw_sp *mlxsw_sp)
{
	unsigned int sub_pools_count = ARRAY_SIZE(mlxsw_sp_counter_sub_pools);
	struct mlxsw_sp_counter_sub_pool *sub_pool;
	struct mlxsw_sp_counter_pool *pool;
	unsigned int base_index;
	unsigned int bank_size;
	unsigned int map_size;
	int i;
	int err;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, COUNTER_POOL_SIZE) ||
	    !MLXSW_CORE_RES_VALID(mlxsw_sp->core, COUNTER_BANK_SIZE))
		return -EIO;

	pool = kzalloc(struct_size(pool, sub_pools, sub_pools_count),
		       GFP_KERNEL);
	if (!pool)
		return -ENOMEM;
	mlxsw_sp->counter_pool = pool;
	memcpy(pool->sub_pools, mlxsw_sp_counter_sub_pools,
	       sub_pools_count * sizeof(*sub_pool));
	pool->sub_pools_count = sub_pools_count;
	spin_lock_init(&pool->counter_pool_lock);

	err = mlxsw_sp_counter_pool_validate(mlxsw_sp);
	if (err)
		goto err_pool_validate;

	err = mlxsw_sp_counter_sub_pools_prepare(mlxsw_sp);
	if (err)
		goto err_sub_pools_prepare;

	pool->pool_size = MLXSW_CORE_RES_GET(mlxsw_sp->core, COUNTER_POOL_SIZE);
	map_size = BITS_TO_LONGS(pool->pool_size) * sizeof(unsigned long);

	bank_size = MLXSW_CORE_RES_GET(mlxsw_sp->core, COUNTER_BANK_SIZE);

	pool->usage = kzalloc(map_size, GFP_KERNEL);
	if (!pool->usage) {
		err = -ENOMEM;
		goto err_usage_alloc;
	}

	/* Allocation is based on bank count which should be
	 * specified for each sub pool statically.
	 */
	base_index = 0;
	for (i = 0; i < pool->sub_pools_count; i++) {
		sub_pool = &pool->sub_pools[i];
		sub_pool->size = sub_pool->bank_count * bank_size;
		sub_pool->base_index = base_index;
		base_index += sub_pool->size;
		/* The last bank can't be fully used */
		if (sub_pool->base_index + sub_pool->size > pool->pool_size)
			sub_pool->size = pool->pool_size - sub_pool->base_index;
	}

	return 0;

err_usage_alloc:
err_sub_pools_prepare:
err_pool_validate:
	kfree(pool);
	return err;
}

void mlxsw_sp_counter_pool_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_counter_pool *pool = mlxsw_sp->counter_pool;

	WARN_ON(find_first_bit(pool->usage, pool->pool_size) !=
			       pool->pool_size);
	kfree(pool->usage);
	kfree(pool);
}

int mlxsw_sp_counter_alloc(struct mlxsw_sp *mlxsw_sp,
			   enum mlxsw_sp_counter_sub_pool_id sub_pool_id,
			   unsigned int *p_counter_index)
{
	struct mlxsw_sp_counter_pool *pool = mlxsw_sp->counter_pool;
	struct mlxsw_sp_counter_sub_pool *sub_pool;
	unsigned int entry_index;
	unsigned int stop_index;
	int i, err;

	sub_pool = &pool->sub_pools[sub_pool_id];
	stop_index = sub_pool->base_index + sub_pool->size;
	entry_index = sub_pool->base_index;

	spin_lock(&pool->counter_pool_lock);
	entry_index = find_next_zero_bit(pool->usage, stop_index, entry_index);
	if (entry_index == stop_index) {
		err = -ENOBUFS;
		goto err_alloc;
	}
	/* The sub-pools can contain non-integer number of entries
	 * so we must check for overflow
	 */
	if (entry_index + sub_pool->entry_size > stop_index) {
		err = -ENOBUFS;
		goto err_alloc;
	}
	for (i = 0; i < sub_pool->entry_size; i++)
		__set_bit(entry_index + i, pool->usage);
	spin_unlock(&pool->counter_pool_lock);

	*p_counter_index = entry_index;
	return 0;

err_alloc:
	spin_unlock(&pool->counter_pool_lock);
	return err;
}

void mlxsw_sp_counter_free(struct mlxsw_sp *mlxsw_sp,
			   enum mlxsw_sp_counter_sub_pool_id sub_pool_id,
			   unsigned int counter_index)
{
	struct mlxsw_sp_counter_pool *pool = mlxsw_sp->counter_pool;
	struct mlxsw_sp_counter_sub_pool *sub_pool;
	int i;

	if (WARN_ON(counter_index >= pool->pool_size))
		return;
	sub_pool = &pool->sub_pools[sub_pool_id];
	spin_lock(&pool->counter_pool_lock);
	for (i = 0; i < sub_pool->entry_size; i++)
		__clear_bit(counter_index + i, pool->usage);
	spin_unlock(&pool->counter_pool_lock);
}
