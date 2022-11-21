// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/spinlock.h>

#include "spectrum_cnt.h"

struct mlxsw_sp_counter_sub_pool {
	u64 size;
	unsigned int base_index;
	enum mlxsw_res_id entry_size_res_id;
	const char *resource_name; /* devlink resource name */
	u64 resource_id; /* devlink resource id */
	unsigned int entry_size;
	unsigned int bank_count;
	atomic_t active_entries_count;
};

struct mlxsw_sp_counter_pool {
	u64 pool_size;
	unsigned long *usage; /* Usage bitmap */
	spinlock_t counter_pool_lock; /* Protects counter pool allocations */
	atomic_t active_entries_count;
	unsigned int sub_pools_count;
	struct mlxsw_sp_counter_sub_pool sub_pools[];
};

static const struct mlxsw_sp_counter_sub_pool mlxsw_sp_counter_sub_pools[] = {
	[MLXSW_SP_COUNTER_SUB_POOL_FLOW] = {
		.entry_size_res_id = MLXSW_RES_ID_COUNTER_SIZE_PACKETS_BYTES,
		.resource_name = MLXSW_SP_RESOURCE_NAME_COUNTERS_FLOW,
		.resource_id = MLXSW_SP_RESOURCE_COUNTERS_FLOW,
		.bank_count = 6,
	},
	[MLXSW_SP_COUNTER_SUB_POOL_RIF] = {
		.entry_size_res_id = MLXSW_RES_ID_COUNTER_SIZE_ROUTER_BASIC,
		.resource_name = MLXSW_SP_RESOURCE_NAME_COUNTERS_RIF,
		.resource_id = MLXSW_SP_RESOURCE_COUNTERS_RIF,
		.bank_count = 2,
	}
};

static u64 mlxsw_sp_counter_sub_pool_occ_get(void *priv)
{
	const struct mlxsw_sp_counter_sub_pool *sub_pool = priv;

	return atomic_read(&sub_pool->active_entries_count);
}

static int mlxsw_sp_counter_sub_pools_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_counter_pool *pool = mlxsw_sp->counter_pool;
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	struct mlxsw_sp_counter_sub_pool *sub_pool;
	unsigned int base_index = 0;
	enum mlxsw_res_id res_id;
	int err;
	int i;

	for (i = 0; i < pool->sub_pools_count; i++) {
		sub_pool = &pool->sub_pools[i];
		res_id = sub_pool->entry_size_res_id;

		if (!mlxsw_core_res_valid(mlxsw_sp->core, res_id))
			return -EIO;
		sub_pool->entry_size = mlxsw_core_res_get(mlxsw_sp->core,
							  res_id);
		err = devlink_resource_size_get(devlink,
						sub_pool->resource_id,
						&sub_pool->size);
		if (err)
			goto err_resource_size_get;

		devlink_resource_occ_get_register(devlink,
						  sub_pool->resource_id,
						  mlxsw_sp_counter_sub_pool_occ_get,
						  sub_pool);

		sub_pool->base_index = base_index;
		base_index += sub_pool->size;
		atomic_set(&sub_pool->active_entries_count, 0);
	}
	return 0;

err_resource_size_get:
	for (i--; i >= 0; i--) {
		sub_pool = &pool->sub_pools[i];

		devlink_resource_occ_get_unregister(devlink,
						    sub_pool->resource_id);
	}
	return err;
}

static void mlxsw_sp_counter_sub_pools_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_counter_pool *pool = mlxsw_sp->counter_pool;
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	struct mlxsw_sp_counter_sub_pool *sub_pool;
	int i;

	for (i = 0; i < pool->sub_pools_count; i++) {
		sub_pool = &pool->sub_pools[i];

		WARN_ON(atomic_read(&sub_pool->active_entries_count));
		devlink_resource_occ_get_unregister(devlink,
						    sub_pool->resource_id);
	}
}

static u64 mlxsw_sp_counter_pool_occ_get(void *priv)
{
	const struct mlxsw_sp_counter_pool *pool = priv;

	return atomic_read(&pool->active_entries_count);
}

int mlxsw_sp_counter_pool_init(struct mlxsw_sp *mlxsw_sp)
{
	unsigned int sub_pools_count = ARRAY_SIZE(mlxsw_sp_counter_sub_pools);
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	struct mlxsw_sp_counter_pool *pool;
	int err;

	pool = kzalloc(struct_size(pool, sub_pools, sub_pools_count),
		       GFP_KERNEL);
	if (!pool)
		return -ENOMEM;
	mlxsw_sp->counter_pool = pool;
	pool->sub_pools_count = sub_pools_count;
	memcpy(pool->sub_pools, mlxsw_sp_counter_sub_pools,
	       flex_array_size(pool, sub_pools, pool->sub_pools_count));
	spin_lock_init(&pool->counter_pool_lock);
	atomic_set(&pool->active_entries_count, 0);

	err = devlink_resource_size_get(devlink, MLXSW_SP_RESOURCE_COUNTERS,
					&pool->pool_size);
	if (err)
		goto err_pool_resource_size_get;
	devlink_resource_occ_get_register(devlink, MLXSW_SP_RESOURCE_COUNTERS,
					  mlxsw_sp_counter_pool_occ_get, pool);

	pool->usage = bitmap_zalloc(pool->pool_size, GFP_KERNEL);
	if (!pool->usage) {
		err = -ENOMEM;
		goto err_usage_alloc;
	}

	err = mlxsw_sp_counter_sub_pools_init(mlxsw_sp);
	if (err)
		goto err_sub_pools_init;

	return 0;

err_sub_pools_init:
	bitmap_free(pool->usage);
err_usage_alloc:
	devlink_resource_occ_get_unregister(devlink,
					    MLXSW_SP_RESOURCE_COUNTERS);
err_pool_resource_size_get:
	kfree(pool);
	return err;
}

void mlxsw_sp_counter_pool_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_counter_pool *pool = mlxsw_sp->counter_pool;
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);

	mlxsw_sp_counter_sub_pools_fini(mlxsw_sp);
	WARN_ON(find_first_bit(pool->usage, pool->pool_size) !=
			       pool->pool_size);
	WARN_ON(atomic_read(&pool->active_entries_count));
	bitmap_free(pool->usage);
	devlink_resource_occ_get_unregister(devlink,
					    MLXSW_SP_RESOURCE_COUNTERS);
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
	atomic_add(sub_pool->entry_size, &sub_pool->active_entries_count);
	atomic_add(sub_pool->entry_size, &pool->active_entries_count);
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
	atomic_sub(sub_pool->entry_size, &sub_pool->active_entries_count);
	atomic_sub(sub_pool->entry_size, &pool->active_entries_count);
}

int mlxsw_sp_counter_resources_register(struct mlxsw_core *mlxsw_core)
{
	static struct devlink_resource_size_params size_params;
	struct devlink *devlink = priv_to_devlink(mlxsw_core);
	const struct mlxsw_sp_counter_sub_pool *sub_pool;
	unsigned int total_bank_config;
	u64 sub_pool_size;
	u64 base_index;
	u64 pool_size;
	u64 bank_size;
	int err;
	int i;

	if (!MLXSW_CORE_RES_VALID(mlxsw_core, COUNTER_POOL_SIZE) ||
	    !MLXSW_CORE_RES_VALID(mlxsw_core, COUNTER_BANK_SIZE))
		return -EIO;

	pool_size = MLXSW_CORE_RES_GET(mlxsw_core, COUNTER_POOL_SIZE);
	bank_size = MLXSW_CORE_RES_GET(mlxsw_core, COUNTER_BANK_SIZE);

	devlink_resource_size_params_init(&size_params, pool_size,
					  pool_size, bank_size,
					  DEVLINK_RESOURCE_UNIT_ENTRY);
	err = devlink_resource_register(devlink,
					MLXSW_SP_RESOURCE_NAME_COUNTERS,
					pool_size,
					MLXSW_SP_RESOURCE_COUNTERS,
					DEVLINK_RESOURCE_ID_PARENT_TOP,
					&size_params);
	if (err)
		return err;

	/* Allocation is based on bank count which should be
	 * specified for each sub pool statically.
	 */
	total_bank_config = 0;
	base_index = 0;
	for (i = 0; i < ARRAY_SIZE(mlxsw_sp_counter_sub_pools); i++) {
		sub_pool = &mlxsw_sp_counter_sub_pools[i];
		sub_pool_size = sub_pool->bank_count * bank_size;
		/* The last bank can't be fully used */
		if (base_index + sub_pool_size > pool_size)
			sub_pool_size = pool_size - base_index;
		base_index += sub_pool_size;

		devlink_resource_size_params_init(&size_params, sub_pool_size,
						  sub_pool_size, bank_size,
						  DEVLINK_RESOURCE_UNIT_ENTRY);
		err = devlink_resource_register(devlink,
						sub_pool->resource_name,
						sub_pool_size,
						sub_pool->resource_id,
						MLXSW_SP_RESOURCE_COUNTERS,
						&size_params);
		if (err)
			return err;
		total_bank_config += sub_pool->bank_count;
	}

	/* Check config is valid, no bank over subscription */
	if (WARN_ON(total_bank_config > div64_u64(pool_size, bank_size) + 1))
		return -EINVAL;

	return 0;
}
