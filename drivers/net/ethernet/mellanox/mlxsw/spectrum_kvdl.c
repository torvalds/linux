/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_kvdl.c
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
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
#include <linux/bitops.h>

#include "spectrum.h"

#define MLXSW_SP_KVDL_SINGLE_BASE 0
#define MLXSW_SP_KVDL_SINGLE_SIZE 16384
#define MLXSW_SP_KVDL_SINGLE_END \
	(MLXSW_SP_KVDL_SINGLE_SIZE + MLXSW_SP_KVDL_SINGLE_BASE - 1)

#define MLXSW_SP_KVDL_CHUNKS_BASE \
	(MLXSW_SP_KVDL_SINGLE_BASE + MLXSW_SP_KVDL_SINGLE_SIZE)
#define MLXSW_SP_KVDL_CHUNKS_SIZE 49152
#define MLXSW_SP_KVDL_CHUNKS_END \
	(MLXSW_SP_KVDL_CHUNKS_SIZE + MLXSW_SP_KVDL_CHUNKS_BASE - 1)

#define MLXSW_SP_KVDL_LARGE_CHUNKS_BASE \
	(MLXSW_SP_KVDL_CHUNKS_BASE + MLXSW_SP_KVDL_CHUNKS_SIZE)
#define MLXSW_SP_KVDL_LARGE_CHUNKS_SIZE \
	(MLXSW_SP_KVD_LINEAR_SIZE - MLXSW_SP_KVDL_LARGE_CHUNKS_BASE)
#define MLXSW_SP_KVDL_LARGE_CHUNKS_END \
	(MLXSW_SP_KVDL_LARGE_CHUNKS_SIZE + MLXSW_SP_KVDL_LARGE_CHUNKS_BASE - 1)

#define MLXSW_SP_CHUNK_MAX 32
#define MLXSW_SP_LARGE_CHUNK_MAX 512

struct mlxsw_sp_kvdl_part_info {
	unsigned int part_index;
	unsigned int start_index;
	unsigned int end_index;
	unsigned int alloc_size;
};

struct mlxsw_sp_kvdl_part {
	struct list_head list;
	struct mlxsw_sp_kvdl_part_info *info;
	unsigned long usage[0];	/* Entries */
};

struct mlxsw_sp_kvdl {
	struct list_head parts_list;
};

static struct mlxsw_sp_kvdl_part *
mlxsw_sp_kvdl_alloc_size_part(struct mlxsw_sp_kvdl *kvdl,
			      unsigned int alloc_size)
{
	struct mlxsw_sp_kvdl_part *part, *min_part = NULL;

	list_for_each_entry(part, &kvdl->parts_list, list) {
		if (alloc_size <= part->info->alloc_size &&
		    (!min_part ||
		     part->info->alloc_size <= min_part->info->alloc_size))
			min_part = part;
	}

	return min_part ?: ERR_PTR(-ENOBUFS);
}

static struct mlxsw_sp_kvdl_part *
mlxsw_sp_kvdl_index_part(struct mlxsw_sp_kvdl *kvdl, u32 kvdl_index)
{
	struct mlxsw_sp_kvdl_part *part;

	list_for_each_entry(part, &kvdl->parts_list, list) {
		if (kvdl_index >= part->info->start_index &&
		    kvdl_index <= part->info->end_index)
			return part;
	}

	return ERR_PTR(-EINVAL);
}

static u32
mlxsw_sp_entry_index_kvdl_index(const struct mlxsw_sp_kvdl_part_info *info,
				unsigned int entry_index)
{
	return info->start_index + entry_index * info->alloc_size;
}

static unsigned int
mlxsw_sp_kvdl_index_entry_index(const struct mlxsw_sp_kvdl_part_info *info,
				u32 kvdl_index)
{
	return (kvdl_index - info->start_index) / info->alloc_size;
}

static int mlxsw_sp_kvdl_part_alloc(struct mlxsw_sp_kvdl_part *part,
				    u32 *p_kvdl_index)
{
	const struct mlxsw_sp_kvdl_part_info *info = part->info;
	unsigned int entry_index, nr_entries;

	nr_entries = (info->end_index - info->start_index + 1) /
		     info->alloc_size;
	entry_index = find_first_zero_bit(part->usage, nr_entries);
	if (entry_index == nr_entries)
		return -ENOBUFS;
	__set_bit(entry_index, part->usage);

	*p_kvdl_index = mlxsw_sp_entry_index_kvdl_index(part->info,
							entry_index);

	return 0;
}

static void mlxsw_sp_kvdl_part_free(struct mlxsw_sp_kvdl_part *part,
				    u32 kvdl_index)
{
	unsigned int entry_index;

	entry_index = mlxsw_sp_kvdl_index_entry_index(part->info,
						      kvdl_index);
	__clear_bit(entry_index, part->usage);
}

int mlxsw_sp_kvdl_alloc(struct mlxsw_sp *mlxsw_sp, unsigned int entry_count,
			u32 *p_entry_index)
{
	struct mlxsw_sp_kvdl_part *part;

	/* Find partition with smallest allocation size satisfying the
	 * requested size.
	 */
	part = mlxsw_sp_kvdl_alloc_size_part(mlxsw_sp->kvdl, entry_count);
	if (IS_ERR(part))
		return PTR_ERR(part);

	return mlxsw_sp_kvdl_part_alloc(part, p_entry_index);
}

void mlxsw_sp_kvdl_free(struct mlxsw_sp *mlxsw_sp, int entry_index)
{
	struct mlxsw_sp_kvdl_part *part;

	part = mlxsw_sp_kvdl_index_part(mlxsw_sp->kvdl, entry_index);
	if (IS_ERR(part))
		return;
	mlxsw_sp_kvdl_part_free(part, entry_index);
}

int mlxsw_sp_kvdl_alloc_size_query(struct mlxsw_sp *mlxsw_sp,
				   unsigned int entry_count,
				   unsigned int *p_alloc_size)
{
	struct mlxsw_sp_kvdl_part *part;

	part = mlxsw_sp_kvdl_alloc_size_part(mlxsw_sp->kvdl, entry_count);
	if (IS_ERR(part))
		return PTR_ERR(part);

	*p_alloc_size = part->info->alloc_size;

	return 0;
}

enum mlxsw_sp_kvdl_part_id {
	MLXSW_SP_KVDL_PART_SINGLE,
	MLXSW_SP_KVDL_PART_CHUNKS,
	MLXSW_SP_KVDL_PART_LARGE_CHUNKS,
};

static const struct mlxsw_sp_kvdl_part_info kvdl_parts_info[] = {
	{
		.part_index	= MLXSW_SP_KVDL_PART_SINGLE,
		.start_index	= MLXSW_SP_KVDL_SINGLE_BASE,
		.end_index	= MLXSW_SP_KVDL_SINGLE_END,
		.alloc_size	= 1,
	},
	{
		.part_index	= MLXSW_SP_KVDL_PART_CHUNKS,
		.start_index	= MLXSW_SP_KVDL_CHUNKS_BASE,
		.end_index	= MLXSW_SP_KVDL_CHUNKS_END,
		.alloc_size	= MLXSW_SP_CHUNK_MAX,
	},
	{
		.part_index	= MLXSW_SP_KVDL_PART_LARGE_CHUNKS,
		.start_index	= MLXSW_SP_KVDL_LARGE_CHUNKS_BASE,
		.end_index	= MLXSW_SP_KVDL_LARGE_CHUNKS_END,
		.alloc_size	= MLXSW_SP_LARGE_CHUNK_MAX,
	},
};

static struct mlxsw_sp_kvdl_part *
mlxsw_sp_kvdl_part_find(struct mlxsw_sp *mlxsw_sp, unsigned int part_index)
{
	struct mlxsw_sp_kvdl_part *part;

	list_for_each_entry(part, &mlxsw_sp->kvdl->parts_list, list) {
		if (part->info->part_index == part_index)
			return part;
	}

	return NULL;
}

static void
mlxsw_sp_kvdl_part_update(struct mlxsw_sp *mlxsw_sp,
			  struct mlxsw_sp_kvdl_part *part, unsigned int size)
{
	struct mlxsw_sp_kvdl_part_info *info = part->info;

	if (list_is_last(&part->list, &mlxsw_sp->kvdl->parts_list)) {
		info->end_index = size - 1;
	} else  {
		struct mlxsw_sp_kvdl_part *last_part;

		last_part = list_next_entry(part, list);
		info->start_index = last_part->info->end_index + 1;
		info->end_index = info->start_index + size - 1;
	}
}

static int mlxsw_sp_kvdl_part_init(struct mlxsw_sp *mlxsw_sp,
				   unsigned int part_index)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	const struct mlxsw_sp_kvdl_part_info *info;
	enum mlxsw_sp_resource_id resource_id;
	struct mlxsw_sp_kvdl_part *part;
	bool need_update = true;
	unsigned int nr_entries;
	size_t usage_size;
	u64 resource_size;
	int err;

	info = &kvdl_parts_info[part_index];

	switch (part_index) {
	case MLXSW_SP_KVDL_PART_SINGLE:
		resource_id = MLXSW_SP_RESOURCE_KVD_LINEAR_SINGLE;
		break;
	case MLXSW_SP_KVDL_PART_CHUNKS:
		resource_id = MLXSW_SP_RESOURCE_KVD_LINEAR_CHUNKS;
		break;
	case MLXSW_SP_KVDL_PART_LARGE_CHUNKS:
		resource_id = MLXSW_SP_RESOURCE_KVD_LINEAR_LARGE_CHUNKS;
		break;
	default:
		return -EINVAL;
	}

	err = devlink_resource_size_get(devlink, resource_id, &resource_size);
	if (err) {
		need_update = false;
		resource_size = info->end_index - info->start_index + 1;
	}

	nr_entries = div_u64(resource_size, info->alloc_size);
	usage_size = BITS_TO_LONGS(nr_entries) * sizeof(unsigned long);
	part = kzalloc(sizeof(*part) + usage_size, GFP_KERNEL);
	if (!part)
		return -ENOMEM;

	part->info = kmemdup(info, sizeof(*part->info), GFP_KERNEL);
	if (!part->info)
		goto err_part_info_alloc;

	list_add(&part->list, &mlxsw_sp->kvdl->parts_list);
	if (need_update)
		mlxsw_sp_kvdl_part_update(mlxsw_sp, part, resource_size);
	return 0;

err_part_info_alloc:
	kfree(part);
	return -ENOMEM;
}

static void mlxsw_sp_kvdl_part_fini(struct mlxsw_sp *mlxsw_sp,
				    unsigned int part_index)
{
	struct mlxsw_sp_kvdl_part *part;

	part = mlxsw_sp_kvdl_part_find(mlxsw_sp, part_index);
	if (!part)
		return;

	list_del(&part->list);
	kfree(part->info);
	kfree(part);
}

static int mlxsw_sp_kvdl_parts_init(struct mlxsw_sp *mlxsw_sp)
{
	int err, i;

	INIT_LIST_HEAD(&mlxsw_sp->kvdl->parts_list);

	for (i = 0; i < ARRAY_SIZE(kvdl_parts_info); i++) {
		err = mlxsw_sp_kvdl_part_init(mlxsw_sp, i);
		if (err)
			goto err_kvdl_part_init;
	}

	return 0;

err_kvdl_part_init:
	for (i--; i >= 0; i--)
		mlxsw_sp_kvdl_part_fini(mlxsw_sp, i);
	return err;
}

static void mlxsw_sp_kvdl_parts_fini(struct mlxsw_sp *mlxsw_sp)
{
	int i;

	for (i = ARRAY_SIZE(kvdl_parts_info) - 1; i >= 0; i--)
		mlxsw_sp_kvdl_part_fini(mlxsw_sp, i);
}

static u64 mlxsw_sp_kvdl_part_occ(struct mlxsw_sp_kvdl_part *part)
{
	unsigned int nr_entries;
	int bit = -1;
	u64 occ = 0;

	nr_entries = (part->info->end_index -
		      part->info->start_index + 1) /
		      part->info->alloc_size;
	while ((bit = find_next_bit(part->usage, nr_entries, bit + 1))
		< nr_entries)
		occ += part->info->alloc_size;
	return occ;
}

u64 mlxsw_sp_kvdl_occ_get(const struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_kvdl_part *part;
	u64 occ = 0;

	list_for_each_entry(part, &mlxsw_sp->kvdl->parts_list, list)
		occ += mlxsw_sp_kvdl_part_occ(part);

	return occ;
}

static u64 mlxsw_sp_kvdl_single_occ_get(struct devlink *devlink)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	struct mlxsw_sp_kvdl_part *part;

	part = mlxsw_sp_kvdl_part_find(mlxsw_sp, MLXSW_SP_KVDL_PART_SINGLE);
	if (!part)
		return -EINVAL;

	return mlxsw_sp_kvdl_part_occ(part);
}

static u64 mlxsw_sp_kvdl_chunks_occ_get(struct devlink *devlink)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	struct mlxsw_sp_kvdl_part *part;

	part = mlxsw_sp_kvdl_part_find(mlxsw_sp, MLXSW_SP_KVDL_PART_CHUNKS);
	if (!part)
		return -EINVAL;

	return mlxsw_sp_kvdl_part_occ(part);
}

static u64 mlxsw_sp_kvdl_large_chunks_occ_get(struct devlink *devlink)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	struct mlxsw_sp_kvdl_part *part;

	part = mlxsw_sp_kvdl_part_find(mlxsw_sp,
				       MLXSW_SP_KVDL_PART_LARGE_CHUNKS);
	if (!part)
		return -EINVAL;

	return mlxsw_sp_kvdl_part_occ(part);
}

static const struct devlink_resource_ops mlxsw_sp_kvdl_single_ops = {
	.occ_get = mlxsw_sp_kvdl_single_occ_get,
};

static const struct devlink_resource_ops mlxsw_sp_kvdl_chunks_ops = {
	.occ_get = mlxsw_sp_kvdl_chunks_occ_get,
};

static const struct devlink_resource_ops mlxsw_sp_kvdl_chunks_large_ops = {
	.occ_get = mlxsw_sp_kvdl_large_chunks_occ_get,
};

int mlxsw_sp_kvdl_resources_register(struct devlink *devlink)
{
	struct mlxsw_core *mlxsw_core = devlink_priv(devlink);
	static struct devlink_resource_size_params size_params;
	u32 kvdl_max_size;
	int err;

	kvdl_max_size = MLXSW_CORE_RES_GET(mlxsw_core, KVD_SIZE) -
			MLXSW_CORE_RES_GET(mlxsw_core, KVD_SINGLE_MIN_SIZE) -
			MLXSW_CORE_RES_GET(mlxsw_core, KVD_DOUBLE_MIN_SIZE);

	devlink_resource_size_params_init(&size_params, 0, kvdl_max_size, 1,
					  DEVLINK_RESOURCE_UNIT_ENTRY);
	err = devlink_resource_register(devlink, MLXSW_SP_RESOURCE_NAME_KVD_LINEAR_SINGLES,
					MLXSW_SP_KVDL_SINGLE_SIZE,
					MLXSW_SP_RESOURCE_KVD_LINEAR_SINGLE,
					MLXSW_SP_RESOURCE_KVD_LINEAR,
					&size_params,
					&mlxsw_sp_kvdl_single_ops);
	if (err)
		return err;

	devlink_resource_size_params_init(&size_params, 0, kvdl_max_size,
					  MLXSW_SP_CHUNK_MAX,
					  DEVLINK_RESOURCE_UNIT_ENTRY);
	err = devlink_resource_register(devlink, MLXSW_SP_RESOURCE_NAME_KVD_LINEAR_CHUNKS,
					MLXSW_SP_KVDL_CHUNKS_SIZE,
					MLXSW_SP_RESOURCE_KVD_LINEAR_CHUNKS,
					MLXSW_SP_RESOURCE_KVD_LINEAR,
					&size_params,
					&mlxsw_sp_kvdl_chunks_ops);
	if (err)
		return err;

	devlink_resource_size_params_init(&size_params, 0, kvdl_max_size,
					  MLXSW_SP_LARGE_CHUNK_MAX,
					  DEVLINK_RESOURCE_UNIT_ENTRY);
	err = devlink_resource_register(devlink, MLXSW_SP_RESOURCE_NAME_KVD_LINEAR_LARGE_CHUNKS,
					MLXSW_SP_KVDL_LARGE_CHUNKS_SIZE,
					MLXSW_SP_RESOURCE_KVD_LINEAR_LARGE_CHUNKS,
					MLXSW_SP_RESOURCE_KVD_LINEAR,
					&size_params,
					&mlxsw_sp_kvdl_chunks_large_ops);
	return err;
}

int mlxsw_sp_kvdl_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_kvdl *kvdl;
	int err;

	kvdl = kzalloc(sizeof(*mlxsw_sp->kvdl), GFP_KERNEL);
	if (!kvdl)
		return -ENOMEM;
	mlxsw_sp->kvdl = kvdl;

	err = mlxsw_sp_kvdl_parts_init(mlxsw_sp);
	if (err)
		goto err_kvdl_parts_init;

	return 0;

err_kvdl_parts_init:
	kfree(mlxsw_sp->kvdl);
	return err;
}

void mlxsw_sp_kvdl_fini(struct mlxsw_sp *mlxsw_sp)
{
	mlxsw_sp_kvdl_parts_fini(mlxsw_sp);
	kfree(mlxsw_sp->kvdl);
}
