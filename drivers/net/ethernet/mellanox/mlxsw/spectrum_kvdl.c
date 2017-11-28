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
	const struct mlxsw_sp_kvdl_part_info *info;
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

static const struct mlxsw_sp_kvdl_part_info kvdl_parts_info[] = {
	{
		.part_index	= 0,
		.start_index	= MLXSW_SP_KVDL_SINGLE_BASE,
		.end_index	= MLXSW_SP_KVDL_SINGLE_END,
		.alloc_size	= 1,
	},
	{
		.part_index	= 1,
		.start_index	= MLXSW_SP_KVDL_CHUNKS_BASE,
		.end_index	= MLXSW_SP_KVDL_CHUNKS_END,
		.alloc_size	= MLXSW_SP_CHUNK_MAX,
	},
	{
		.part_index	= 2,
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

static int mlxsw_sp_kvdl_part_init(struct mlxsw_sp *mlxsw_sp,
				   unsigned int part_index)
{
	const struct mlxsw_sp_kvdl_part_info *info;
	struct mlxsw_sp_kvdl_part *part;
	unsigned int nr_entries;
	size_t usage_size;

	info = &kvdl_parts_info[part_index];

	nr_entries = (info->end_index - info->start_index + 1) /
		     info->alloc_size;
	usage_size = BITS_TO_LONGS(nr_entries) * sizeof(unsigned long);
	part = kzalloc(sizeof(*part) + usage_size, GFP_KERNEL);
	if (!part)
		return -ENOMEM;

	part->info = info;
	list_add(&part->list, &mlxsw_sp->kvdl->parts_list);

	return 0;
}

static void mlxsw_sp_kvdl_part_fini(struct mlxsw_sp *mlxsw_sp,
				    unsigned int part_index)
{
	struct mlxsw_sp_kvdl_part *part;

	part = mlxsw_sp_kvdl_part_find(mlxsw_sp, part_index);
	if (!part)
		return;

	list_del(&part->list);
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
