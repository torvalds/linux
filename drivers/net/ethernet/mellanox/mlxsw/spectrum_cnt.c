// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/bitops.h>

#include "spectrum_cnt.h"

#define MLXSW_SP_COUNTER_POOL_BANK_SIZE 4096

struct mlxsw_sp_counter_sub_pool {
	unsigned int base_index;
	unsigned int size;
	unsigned int entry_size;
	unsigned int bank_count;
};

struct mlxsw_sp_counter_pool {
	unsigned int pool_size;
	unsigned long *usage; /* Usage bitmap */
	struct mlxsw_sp_counter_sub_pool *sub_pools;
};

static struct mlxsw_sp_counter_sub_pool mlxsw_sp_counter_sub_pools[] = {
	[MLXSW_SP_COUNTER_SUB_POOL_FLOW] = {
		.bank_count = 6,
	},
	[MLXSW_SP_COUNTER_SUB_POOL_RIF] = {
		.bank_count = 2,
	}
};

static int mlxsw_sp_counter_pool_validate(struct mlxsw_sp *mlxsw_sp)
{
	unsigned int total_bank_config = 0;
	unsigned int pool_size;
	int i;

	pool_size = MLXSW_CORE_RES_GET(mlxsw_sp->core, COUNTER_POOL_SIZE);
	/* Check config is valid, no bank over subscription */
	for (i = 0; i < ARRAY_SIZE(mlxsw_sp_counter_sub_pools); i++)
		total_bank_config += mlxsw_sp_counter_sub_pools[i].bank_count;
	if (total_bank_config > pool_size / MLXSW_SP_COUNTER_POOL_BANK_SIZE + 1)
		return -EINVAL;
	return 0;
}

static int mlxsw_sp_counter_sub_pools_prepare(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_counter_sub_pool *sub_pool;

	/* Prepare generic flow pool*/
	sub_pool = &mlxsw_sp_counter_sub_pools[MLXSW_SP_COUNTER_SUB_POOL_FLOW];
	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, COUNTER_SIZE_PACKETS_BYTES))
		return -EIO;
	sub_pool->entry_size = MLXSW_CORE_RES_GET(mlxsw_sp->core,
						  COUNTER_SIZE_PACKETS_BYTES);
	/* Prepare erif pool*/
	sub_pool = &mlxsw_sp_counter_sub_pools[MLXSW_SP_COUNTER_SUB_POOL_RIF];
	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, COUNTER_SIZE_ROUTER_BASIC))
		return -EIO;
	sub_pool->entry_size = MLXSW_CORE_RES_GET(mlxsw_sp->core,
						  COUNTER_SIZE_ROUTER_BASIC);
	return 0;
}

int mlxsw_sp_counter_pool_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_counter_sub_pool *sub_pool;
	struct mlxsw_sp_counter_pool *pool;
	unsigned int base_index;
	unsigned int map_size;
	int i;
	int err;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, COUNTER_POOL_SIZE))
		return -EIO;

	err = mlxsw_sp_counter_pool_validate(mlxsw_sp);
	if (err)
		return err;

	err = mlxsw_sp_counter_sub_pools_prepare(mlxsw_sp);
	if (err)
		return err;

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return -ENOMEM;

	pool->pool_size = MLXSW_CORE_RES_GET(mlxsw_sp->core, COUNTER_POOL_SIZE);
	map_size = BITS_TO_LONGS(pool->pool_size) * sizeof(unsigned long);

	pool->usage = kzalloc(map_size, GFP_KERNEL);
	if (!pool->usage) {
		err = -ENOMEM;
		goto err_usage_alloc;
	}

	pool->sub_pools = mlxsw_sp_counter_sub_pools;
	/* Allocation is based on bank count which should be
	 * specified for each sub pool statically.
	 */
	base_index = 0;
	for (i = 0; i < ARRAY_SIZE(mlxsw_sp_counter_sub_pools); i++) {
		sub_pool = &pool->sub_pools[i];
		sub_pool->size = sub_pool->bank_count *
				 MLXSW_SP_COUNTER_POOL_BANK_SIZE;
		sub_pool->base_index = base_index;
		base_index += sub_pool->size;
		/* The last bank can't be fully used */
		if (sub_pool->base_index + sub_pool->size > pool->pool_size)
			sub_pool->size = pool->pool_size - sub_pool->base_index;
	}

	mlxsw_sp->counter_pool = pool;
	return 0;

err_usage_alloc:
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
	int i;

	sub_pool = &mlxsw_sp_counter_sub_pools[sub_pool_id];
	stop_index = sub_pool->base_index + sub_pool->size;
	entry_index = sub_pool->base_index;

	entry_index = find_next_zero_bit(pool->usage, stop_index, entry_index);
	if (entry_index == stop_index)
		return -ENOBUFS;
	/* The sub-pools can contain non-integer number of entries
	 * so we must check for overflow
	 */
	if (entry_index + sub_pool->entry_size > stop_index)
		return -ENOBUFS;
	for (i = 0; i < sub_pool->entry_size; i++)
		__set_bit(entry_index + i, pool->usage);

	*p_counter_index = entry_index;
	return 0;
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
	sub_pool = &mlxsw_sp_counter_sub_pools[sub_pool_id];
	for (i = 0; i < sub_pool->entry_size; i++)
		__clear_bit(counter_index + i, pool->usage);
}
