// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/bitops.h>

#include "spectrum.h"

#define MLXSW_SP1_KVDL_SINGLE_BASE 0
#define MLXSW_SP1_KVDL_SINGLE_SIZE 16384
#define MLXSW_SP1_KVDL_SINGLE_END \
	(MLXSW_SP1_KVDL_SINGLE_SIZE + MLXSW_SP1_KVDL_SINGLE_BASE - 1)

#define MLXSW_SP1_KVDL_CHUNKS_BASE \
	(MLXSW_SP1_KVDL_SINGLE_BASE + MLXSW_SP1_KVDL_SINGLE_SIZE)
#define MLXSW_SP1_KVDL_CHUNKS_SIZE 49152
#define MLXSW_SP1_KVDL_CHUNKS_END \
	(MLXSW_SP1_KVDL_CHUNKS_SIZE + MLXSW_SP1_KVDL_CHUNKS_BASE - 1)

#define MLXSW_SP1_KVDL_LARGE_CHUNKS_BASE \
	(MLXSW_SP1_KVDL_CHUNKS_BASE + MLXSW_SP1_KVDL_CHUNKS_SIZE)
#define MLXSW_SP1_KVDL_LARGE_CHUNKS_SIZE \
	(MLXSW_SP_KVD_LINEAR_SIZE - MLXSW_SP1_KVDL_LARGE_CHUNKS_BASE)
#define MLXSW_SP1_KVDL_LARGE_CHUNKS_END \
	(MLXSW_SP1_KVDL_LARGE_CHUNKS_SIZE + MLXSW_SP1_KVDL_LARGE_CHUNKS_BASE - 1)

#define MLXSW_SP1_KVDL_SINGLE_ALLOC_SIZE 1
#define MLXSW_SP1_KVDL_CHUNKS_ALLOC_SIZE 32
#define MLXSW_SP1_KVDL_LARGE_CHUNKS_ALLOC_SIZE 512

struct mlxsw_sp1_kvdl_part_info {
	unsigned int part_index;
	unsigned int start_index;
	unsigned int end_index;
	unsigned int alloc_size;
	enum mlxsw_sp_resource_id resource_id;
};

enum mlxsw_sp1_kvdl_part_id {
	MLXSW_SP1_KVDL_PART_ID_SINGLE,
	MLXSW_SP1_KVDL_PART_ID_CHUNKS,
	MLXSW_SP1_KVDL_PART_ID_LARGE_CHUNKS,
};

#define MLXSW_SP1_KVDL_PART_INFO(id)				\
[MLXSW_SP1_KVDL_PART_ID_##id] = {				\
	.start_index = MLXSW_SP1_KVDL_##id##_BASE,		\
	.end_index = MLXSW_SP1_KVDL_##id##_END,			\
	.alloc_size = MLXSW_SP1_KVDL_##id##_ALLOC_SIZE,		\
	.resource_id = MLXSW_SP_RESOURCE_KVD_LINEAR_##id,	\
}

static const struct mlxsw_sp1_kvdl_part_info mlxsw_sp1_kvdl_parts_info[] = {
	MLXSW_SP1_KVDL_PART_INFO(SINGLE),
	MLXSW_SP1_KVDL_PART_INFO(CHUNKS),
	MLXSW_SP1_KVDL_PART_INFO(LARGE_CHUNKS),
};

#define MLXSW_SP1_KVDL_PARTS_INFO_LEN ARRAY_SIZE(mlxsw_sp1_kvdl_parts_info)

struct mlxsw_sp1_kvdl_part {
	struct mlxsw_sp1_kvdl_part_info info;
	unsigned long usage[0];	/* Entries */
};

struct mlxsw_sp1_kvdl {
	struct mlxsw_sp1_kvdl_part *parts[MLXSW_SP1_KVDL_PARTS_INFO_LEN];
};

static struct mlxsw_sp1_kvdl_part *
mlxsw_sp1_kvdl_alloc_size_part(struct mlxsw_sp1_kvdl *kvdl,
			       unsigned int alloc_size)
{
	struct mlxsw_sp1_kvdl_part *part, *min_part = NULL;
	int i;

	for (i = 0; i < MLXSW_SP1_KVDL_PARTS_INFO_LEN; i++) {
		part = kvdl->parts[i];
		if (alloc_size <= part->info.alloc_size &&
		    (!min_part ||
		     part->info.alloc_size <= min_part->info.alloc_size))
			min_part = part;
	}

	return min_part ?: ERR_PTR(-ENOBUFS);
}

static struct mlxsw_sp1_kvdl_part *
mlxsw_sp1_kvdl_index_part(struct mlxsw_sp1_kvdl *kvdl, u32 kvdl_index)
{
	struct mlxsw_sp1_kvdl_part *part;
	int i;

	for (i = 0; i < MLXSW_SP1_KVDL_PARTS_INFO_LEN; i++) {
		part = kvdl->parts[i];
		if (kvdl_index >= part->info.start_index &&
		    kvdl_index <= part->info.end_index)
			return part;
	}

	return ERR_PTR(-EINVAL);
}

static u32
mlxsw_sp1_kvdl_to_kvdl_index(const struct mlxsw_sp1_kvdl_part_info *info,
			     unsigned int entry_index)
{
	return info->start_index + entry_index * info->alloc_size;
}

static unsigned int
mlxsw_sp1_kvdl_to_entry_index(const struct mlxsw_sp1_kvdl_part_info *info,
			      u32 kvdl_index)
{
	return (kvdl_index - info->start_index) / info->alloc_size;
}

static int mlxsw_sp1_kvdl_part_alloc(struct mlxsw_sp1_kvdl_part *part,
				     u32 *p_kvdl_index)
{
	const struct mlxsw_sp1_kvdl_part_info *info = &part->info;
	unsigned int entry_index, nr_entries;

	nr_entries = (info->end_index - info->start_index + 1) /
		     info->alloc_size;
	entry_index = find_first_zero_bit(part->usage, nr_entries);
	if (entry_index == nr_entries)
		return -ENOBUFS;
	__set_bit(entry_index, part->usage);

	*p_kvdl_index = mlxsw_sp1_kvdl_to_kvdl_index(info, entry_index);

	return 0;
}

static void mlxsw_sp1_kvdl_part_free(struct mlxsw_sp1_kvdl_part *part,
				     u32 kvdl_index)
{
	const struct mlxsw_sp1_kvdl_part_info *info = &part->info;
	unsigned int entry_index;

	entry_index = mlxsw_sp1_kvdl_to_entry_index(info, kvdl_index);
	__clear_bit(entry_index, part->usage);
}

static int mlxsw_sp1_kvdl_alloc(struct mlxsw_sp *mlxsw_sp, void *priv,
				enum mlxsw_sp_kvdl_entry_type type,
				unsigned int entry_count,
				u32 *p_entry_index)
{
	struct mlxsw_sp1_kvdl *kvdl = priv;
	struct mlxsw_sp1_kvdl_part *part;

	/* Find partition with smallest allocation size satisfying the
	 * requested size.
	 */
	part = mlxsw_sp1_kvdl_alloc_size_part(kvdl, entry_count);
	if (IS_ERR(part))
		return PTR_ERR(part);

	return mlxsw_sp1_kvdl_part_alloc(part, p_entry_index);
}

static void mlxsw_sp1_kvdl_free(struct mlxsw_sp *mlxsw_sp, void *priv,
				enum mlxsw_sp_kvdl_entry_type type,
				unsigned int entry_count, int entry_index)
{
	struct mlxsw_sp1_kvdl *kvdl = priv;
	struct mlxsw_sp1_kvdl_part *part;

	part = mlxsw_sp1_kvdl_index_part(kvdl, entry_index);
	if (IS_ERR(part))
		return;
	mlxsw_sp1_kvdl_part_free(part, entry_index);
}

static int mlxsw_sp1_kvdl_alloc_size_query(struct mlxsw_sp *mlxsw_sp,
					   void *priv,
					   enum mlxsw_sp_kvdl_entry_type type,
					   unsigned int entry_count,
					   unsigned int *p_alloc_size)
{
	struct mlxsw_sp1_kvdl *kvdl = priv;
	struct mlxsw_sp1_kvdl_part *part;

	part = mlxsw_sp1_kvdl_alloc_size_part(kvdl, entry_count);
	if (IS_ERR(part))
		return PTR_ERR(part);

	*p_alloc_size = part->info.alloc_size;

	return 0;
}

static void mlxsw_sp1_kvdl_part_update(struct mlxsw_sp1_kvdl_part *part,
				       struct mlxsw_sp1_kvdl_part *part_prev,
				       unsigned int size)
{
	if (!part_prev) {
		part->info.end_index = size - 1;
	} else {
		part->info.start_index = part_prev->info.end_index + 1;
		part->info.end_index = part->info.start_index + size - 1;
	}
}

static struct mlxsw_sp1_kvdl_part *
mlxsw_sp1_kvdl_part_init(struct mlxsw_sp *mlxsw_sp,
			 const struct mlxsw_sp1_kvdl_part_info *info,
			 struct mlxsw_sp1_kvdl_part *part_prev)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	struct mlxsw_sp1_kvdl_part *part;
	bool need_update = true;
	unsigned int nr_entries;
	size_t usage_size;
	u64 resource_size;
	int err;

	err = devlink_resource_size_get(devlink, info->resource_id,
					&resource_size);
	if (err) {
		need_update = false;
		resource_size = info->end_index - info->start_index + 1;
	}

	nr_entries = div_u64(resource_size, info->alloc_size);
	usage_size = BITS_TO_LONGS(nr_entries) * sizeof(unsigned long);
	part = kzalloc(sizeof(*part) + usage_size, GFP_KERNEL);
	if (!part)
		return ERR_PTR(-ENOMEM);

	memcpy(&part->info, info, sizeof(part->info));

	if (need_update)
		mlxsw_sp1_kvdl_part_update(part, part_prev, resource_size);
	return part;
}

static void mlxsw_sp1_kvdl_part_fini(struct mlxsw_sp1_kvdl_part *part)
{
	kfree(part);
}

static int mlxsw_sp1_kvdl_parts_init(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp1_kvdl *kvdl)
{
	const struct mlxsw_sp1_kvdl_part_info *info;
	struct mlxsw_sp1_kvdl_part *part_prev = NULL;
	int err, i;

	for (i = 0; i < MLXSW_SP1_KVDL_PARTS_INFO_LEN; i++) {
		info = &mlxsw_sp1_kvdl_parts_info[i];
		kvdl->parts[i] = mlxsw_sp1_kvdl_part_init(mlxsw_sp, info,
							  part_prev);
		if (IS_ERR(kvdl->parts[i])) {
			err = PTR_ERR(kvdl->parts[i]);
			goto err_kvdl_part_init;
		}
		part_prev = kvdl->parts[i];
	}
	return 0;

err_kvdl_part_init:
	for (i--; i >= 0; i--)
		mlxsw_sp1_kvdl_part_fini(kvdl->parts[i]);
	return err;
}

static void mlxsw_sp1_kvdl_parts_fini(struct mlxsw_sp1_kvdl *kvdl)
{
	int i;

	for (i = 0; i < MLXSW_SP1_KVDL_PARTS_INFO_LEN; i++)
		mlxsw_sp1_kvdl_part_fini(kvdl->parts[i]);
}

static u64 mlxsw_sp1_kvdl_part_occ(struct mlxsw_sp1_kvdl_part *part)
{
	const struct mlxsw_sp1_kvdl_part_info *info = &part->info;
	unsigned int nr_entries;
	int bit = -1;
	u64 occ = 0;

	nr_entries = (info->end_index -
		      info->start_index + 1) /
		      info->alloc_size;
	while ((bit = find_next_bit(part->usage, nr_entries, bit + 1))
		< nr_entries)
		occ += info->alloc_size;
	return occ;
}

static u64 mlxsw_sp1_kvdl_occ_get(void *priv)
{
	const struct mlxsw_sp1_kvdl *kvdl = priv;
	u64 occ = 0;
	int i;

	for (i = 0; i < MLXSW_SP1_KVDL_PARTS_INFO_LEN; i++)
		occ += mlxsw_sp1_kvdl_part_occ(kvdl->parts[i]);

	return occ;
}

static u64 mlxsw_sp1_kvdl_single_occ_get(void *priv)
{
	const struct mlxsw_sp1_kvdl *kvdl = priv;
	struct mlxsw_sp1_kvdl_part *part;

	part = kvdl->parts[MLXSW_SP1_KVDL_PART_ID_SINGLE];
	return mlxsw_sp1_kvdl_part_occ(part);
}

static u64 mlxsw_sp1_kvdl_chunks_occ_get(void *priv)
{
	const struct mlxsw_sp1_kvdl *kvdl = priv;
	struct mlxsw_sp1_kvdl_part *part;

	part = kvdl->parts[MLXSW_SP1_KVDL_PART_ID_CHUNKS];
	return mlxsw_sp1_kvdl_part_occ(part);
}

static u64 mlxsw_sp1_kvdl_large_chunks_occ_get(void *priv)
{
	const struct mlxsw_sp1_kvdl *kvdl = priv;
	struct mlxsw_sp1_kvdl_part *part;

	part = kvdl->parts[MLXSW_SP1_KVDL_PART_ID_LARGE_CHUNKS];
	return mlxsw_sp1_kvdl_part_occ(part);
}

static int mlxsw_sp1_kvdl_init(struct mlxsw_sp *mlxsw_sp, void *priv)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	struct mlxsw_sp1_kvdl *kvdl = priv;
	int err;

	err = mlxsw_sp1_kvdl_parts_init(mlxsw_sp, kvdl);
	if (err)
		return err;
	devlink_resource_occ_get_register(devlink,
					  MLXSW_SP_RESOURCE_KVD_LINEAR,
					  mlxsw_sp1_kvdl_occ_get,
					  kvdl);
	devlink_resource_occ_get_register(devlink,
					  MLXSW_SP_RESOURCE_KVD_LINEAR_SINGLE,
					  mlxsw_sp1_kvdl_single_occ_get,
					  kvdl);
	devlink_resource_occ_get_register(devlink,
					  MLXSW_SP_RESOURCE_KVD_LINEAR_CHUNKS,
					  mlxsw_sp1_kvdl_chunks_occ_get,
					  kvdl);
	devlink_resource_occ_get_register(devlink,
					  MLXSW_SP_RESOURCE_KVD_LINEAR_LARGE_CHUNKS,
					  mlxsw_sp1_kvdl_large_chunks_occ_get,
					  kvdl);
	return 0;
}

static void mlxsw_sp1_kvdl_fini(struct mlxsw_sp *mlxsw_sp, void *priv)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	struct mlxsw_sp1_kvdl *kvdl = priv;

	devlink_resource_occ_get_unregister(devlink,
					    MLXSW_SP_RESOURCE_KVD_LINEAR_LARGE_CHUNKS);
	devlink_resource_occ_get_unregister(devlink,
					    MLXSW_SP_RESOURCE_KVD_LINEAR_CHUNKS);
	devlink_resource_occ_get_unregister(devlink,
					    MLXSW_SP_RESOURCE_KVD_LINEAR_SINGLE);
	devlink_resource_occ_get_unregister(devlink,
					    MLXSW_SP_RESOURCE_KVD_LINEAR);
	mlxsw_sp1_kvdl_parts_fini(kvdl);
}

const struct mlxsw_sp_kvdl_ops mlxsw_sp1_kvdl_ops = {
	.priv_size = sizeof(struct mlxsw_sp1_kvdl),
	.init = mlxsw_sp1_kvdl_init,
	.fini = mlxsw_sp1_kvdl_fini,
	.alloc = mlxsw_sp1_kvdl_alloc,
	.free = mlxsw_sp1_kvdl_free,
	.alloc_size_query = mlxsw_sp1_kvdl_alloc_size_query,
};

int mlxsw_sp1_kvdl_resources_register(struct mlxsw_core *mlxsw_core)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_core);
	static struct devlink_resource_size_params size_params;
	u32 kvdl_max_size;
	int err;

	kvdl_max_size = MLXSW_CORE_RES_GET(mlxsw_core, KVD_SIZE) -
			MLXSW_CORE_RES_GET(mlxsw_core, KVD_SINGLE_MIN_SIZE) -
			MLXSW_CORE_RES_GET(mlxsw_core, KVD_DOUBLE_MIN_SIZE);

	devlink_resource_size_params_init(&size_params, 0, kvdl_max_size,
					  MLXSW_SP1_KVDL_SINGLE_ALLOC_SIZE,
					  DEVLINK_RESOURCE_UNIT_ENTRY);
	err = devlink_resource_register(devlink, MLXSW_SP_RESOURCE_NAME_KVD_LINEAR_SINGLES,
					MLXSW_SP1_KVDL_SINGLE_SIZE,
					MLXSW_SP_RESOURCE_KVD_LINEAR_SINGLE,
					MLXSW_SP_RESOURCE_KVD_LINEAR,
					&size_params);
	if (err)
		return err;

	devlink_resource_size_params_init(&size_params, 0, kvdl_max_size,
					  MLXSW_SP1_KVDL_CHUNKS_ALLOC_SIZE,
					  DEVLINK_RESOURCE_UNIT_ENTRY);
	err = devlink_resource_register(devlink, MLXSW_SP_RESOURCE_NAME_KVD_LINEAR_CHUNKS,
					MLXSW_SP1_KVDL_CHUNKS_SIZE,
					MLXSW_SP_RESOURCE_KVD_LINEAR_CHUNKS,
					MLXSW_SP_RESOURCE_KVD_LINEAR,
					&size_params);
	if (err)
		return err;

	devlink_resource_size_params_init(&size_params, 0, kvdl_max_size,
					  MLXSW_SP1_KVDL_LARGE_CHUNKS_ALLOC_SIZE,
					  DEVLINK_RESOURCE_UNIT_ENTRY);
	err = devlink_resource_register(devlink, MLXSW_SP_RESOURCE_NAME_KVD_LINEAR_LARGE_CHUNKS,
					MLXSW_SP1_KVDL_LARGE_CHUNKS_SIZE,
					MLXSW_SP_RESOURCE_KVD_LINEAR_LARGE_CHUNKS,
					MLXSW_SP_RESOURCE_KVD_LINEAR,
					&size_params);
	return err;
}
