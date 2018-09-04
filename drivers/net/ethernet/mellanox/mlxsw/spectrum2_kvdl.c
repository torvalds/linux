// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/bitops.h>

#include "spectrum.h"
#include "core.h"
#include "reg.h"
#include "resources.h"

struct mlxsw_sp2_kvdl_part_info {
	u8 res_type;
	/* For each defined partititon we need to know how many
	 * usage bits we need and how many indexes there are
	 * represented by a single bit. This could be got from FW
	 * querying appropriate resources. So have the resource
	 * ids for for this purpose in partition definition.
	 */
	enum mlxsw_res_id usage_bit_count_res_id;
	enum mlxsw_res_id index_range_res_id;
};

#define MLXSW_SP2_KVDL_PART_INFO(_entry_type, _res_type,			\
				 _usage_bit_count_res_id, _index_range_res_id)	\
[MLXSW_SP_KVDL_ENTRY_TYPE_##_entry_type] = {					\
	.res_type = _res_type,							\
	.usage_bit_count_res_id = MLXSW_RES_ID_##_usage_bit_count_res_id,	\
	.index_range_res_id = MLXSW_RES_ID_##_index_range_res_id,		\
}

static const struct mlxsw_sp2_kvdl_part_info mlxsw_sp2_kvdl_parts_info[] = {
	MLXSW_SP2_KVDL_PART_INFO(ADJ, 0x21, KVD_SIZE, MAX_KVD_LINEAR_RANGE),
	MLXSW_SP2_KVDL_PART_INFO(ACTSET, 0x23, MAX_KVD_ACTION_SETS,
				 MAX_KVD_ACTION_SETS),
	MLXSW_SP2_KVDL_PART_INFO(PBS, 0x24, KVD_SIZE, KVD_SIZE),
	MLXSW_SP2_KVDL_PART_INFO(MCRIGR, 0x26, KVD_SIZE, KVD_SIZE),
};

#define MLXSW_SP2_KVDL_PARTS_INFO_LEN ARRAY_SIZE(mlxsw_sp2_kvdl_parts_info)

struct mlxsw_sp2_kvdl_part {
	const struct mlxsw_sp2_kvdl_part_info *info;
	unsigned int usage_bit_count;
	unsigned int indexes_per_usage_bit;
	unsigned int last_allocated_bit;
	unsigned long usage[0];	/* Usage bits */
};

struct mlxsw_sp2_kvdl {
	struct mlxsw_sp2_kvdl_part *parts[MLXSW_SP2_KVDL_PARTS_INFO_LEN];
};

static int mlxsw_sp2_kvdl_part_find_zero_bits(struct mlxsw_sp2_kvdl_part *part,
					      unsigned int bit_count,
					      unsigned int *p_bit)
{
	unsigned int start_bit;
	unsigned int bit;
	unsigned int i;
	bool wrap = false;

	start_bit = part->last_allocated_bit + 1;
	if (start_bit == part->usage_bit_count)
		start_bit = 0;
	bit = start_bit;
again:
	bit = find_next_zero_bit(part->usage, part->usage_bit_count, bit);
	if (!wrap && bit + bit_count >= part->usage_bit_count) {
		wrap = true;
		bit = 0;
		goto again;
	}
	if (wrap && bit + bit_count >= start_bit)
		return -ENOBUFS;
	for (i = 0; i < bit_count; i++) {
		if (test_bit(bit + i, part->usage)) {
			bit += bit_count;
			goto again;
		}
	}
	*p_bit = bit;
	return 0;
}

static int mlxsw_sp2_kvdl_part_alloc(struct mlxsw_sp2_kvdl_part *part,
				     unsigned int size,
				     u32 *p_kvdl_index)
{
	unsigned int bit_count;
	unsigned int bit;
	unsigned int i;
	int err;

	bit_count = DIV_ROUND_UP(size, part->indexes_per_usage_bit);
	err = mlxsw_sp2_kvdl_part_find_zero_bits(part, bit_count, &bit);
	if (err)
		return err;
	for (i = 0; i < bit_count; i++)
		__set_bit(bit + i, part->usage);
	*p_kvdl_index = bit * part->indexes_per_usage_bit;
	return 0;
}

static int mlxsw_sp2_kvdl_rec_del(struct mlxsw_sp *mlxsw_sp, u8 res_type,
				  u16 size, u32 kvdl_index)
{
	char *iedr_pl;
	int err;

	iedr_pl = kmalloc(MLXSW_REG_IEDR_LEN, GFP_KERNEL);
	if (!iedr_pl)
		return -ENOMEM;

	mlxsw_reg_iedr_pack(iedr_pl);
	mlxsw_reg_iedr_rec_pack(iedr_pl, 0, res_type, size, kvdl_index);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(iedr), iedr_pl);
	kfree(iedr_pl);
	return err;
}

static void mlxsw_sp2_kvdl_part_free(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp2_kvdl_part *part,
				     unsigned int size, u32 kvdl_index)
{
	unsigned int bit_count;
	unsigned int bit;
	unsigned int i;
	int err;

	/* We need to ask FW to delete previously used KVD linear index */
	err = mlxsw_sp2_kvdl_rec_del(mlxsw_sp, part->info->res_type,
				     size, kvdl_index);
	if (err)
		return;

	bit_count = DIV_ROUND_UP(size, part->indexes_per_usage_bit);
	bit = kvdl_index / part->indexes_per_usage_bit;
	for (i = 0; i < bit_count; i++)
		__clear_bit(bit + i, part->usage);
}

static int mlxsw_sp2_kvdl_alloc(struct mlxsw_sp *mlxsw_sp, void *priv,
				enum mlxsw_sp_kvdl_entry_type type,
				unsigned int entry_count,
				u32 *p_entry_index)
{
	unsigned int size = entry_count * mlxsw_sp_kvdl_entry_size(type);
	struct mlxsw_sp2_kvdl *kvdl = priv;
	struct mlxsw_sp2_kvdl_part *part = kvdl->parts[type];

	return mlxsw_sp2_kvdl_part_alloc(part, size, p_entry_index);
}

static void mlxsw_sp2_kvdl_free(struct mlxsw_sp *mlxsw_sp, void *priv,
				enum mlxsw_sp_kvdl_entry_type type,
				unsigned int entry_count,
				int entry_index)
{
	unsigned int size = entry_count * mlxsw_sp_kvdl_entry_size(type);
	struct mlxsw_sp2_kvdl *kvdl = priv;
	struct mlxsw_sp2_kvdl_part *part = kvdl->parts[type];

	return mlxsw_sp2_kvdl_part_free(mlxsw_sp, part, size, entry_index);
}

static int mlxsw_sp2_kvdl_alloc_size_query(struct mlxsw_sp *mlxsw_sp,
					   void *priv,
					   enum mlxsw_sp_kvdl_entry_type type,
					   unsigned int entry_count,
					   unsigned int *p_alloc_count)
{
	*p_alloc_count = entry_count;
	return 0;
}

static struct mlxsw_sp2_kvdl_part *
mlxsw_sp2_kvdl_part_init(struct mlxsw_sp *mlxsw_sp,
			 const struct mlxsw_sp2_kvdl_part_info *info)
{
	unsigned int indexes_per_usage_bit;
	struct mlxsw_sp2_kvdl_part *part;
	unsigned int index_range;
	unsigned int usage_bit_count;
	size_t usage_size;

	if (!mlxsw_core_res_valid(mlxsw_sp->core,
				  info->usage_bit_count_res_id) ||
	    !mlxsw_core_res_valid(mlxsw_sp->core,
				  info->index_range_res_id))
		return ERR_PTR(-EIO);
	usage_bit_count = mlxsw_core_res_get(mlxsw_sp->core,
					     info->usage_bit_count_res_id);
	index_range = mlxsw_core_res_get(mlxsw_sp->core,
					 info->index_range_res_id);

	/* For some partitions, one usage bit represents a group of indexes.
	 * That's why we compute the number of indexes per usage bit here,
	 * according to queried resources.
	 */
	indexes_per_usage_bit = index_range / usage_bit_count;

	usage_size = BITS_TO_LONGS(usage_bit_count) * sizeof(unsigned long);
	part = kzalloc(sizeof(*part) + usage_size, GFP_KERNEL);
	if (!part)
		return ERR_PTR(-ENOMEM);
	part->info = info;
	part->usage_bit_count = usage_bit_count;
	part->indexes_per_usage_bit = indexes_per_usage_bit;
	part->last_allocated_bit = usage_bit_count - 1;
	return part;
}

static void mlxsw_sp2_kvdl_part_fini(struct mlxsw_sp2_kvdl_part *part)
{
	kfree(part);
}

static int mlxsw_sp2_kvdl_parts_init(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp2_kvdl *kvdl)
{
	const struct mlxsw_sp2_kvdl_part_info *info;
	int i;
	int err;

	for (i = 0; i < MLXSW_SP2_KVDL_PARTS_INFO_LEN; i++) {
		info = &mlxsw_sp2_kvdl_parts_info[i];
		kvdl->parts[i] = mlxsw_sp2_kvdl_part_init(mlxsw_sp, info);
		if (IS_ERR(kvdl->parts[i])) {
			err = PTR_ERR(kvdl->parts[i]);
			goto err_kvdl_part_init;
		}
	}
	return 0;

err_kvdl_part_init:
	for (i--; i >= 0; i--)
		mlxsw_sp2_kvdl_part_fini(kvdl->parts[i]);
	return err;
}

static void mlxsw_sp2_kvdl_parts_fini(struct mlxsw_sp2_kvdl *kvdl)
{
	int i;

	for (i = 0; i < MLXSW_SP2_KVDL_PARTS_INFO_LEN; i++)
		mlxsw_sp2_kvdl_part_fini(kvdl->parts[i]);
}

static int mlxsw_sp2_kvdl_init(struct mlxsw_sp *mlxsw_sp, void *priv)
{
	struct mlxsw_sp2_kvdl *kvdl = priv;

	return mlxsw_sp2_kvdl_parts_init(mlxsw_sp, kvdl);
}

static void mlxsw_sp2_kvdl_fini(struct mlxsw_sp *mlxsw_sp, void *priv)
{
	struct mlxsw_sp2_kvdl *kvdl = priv;

	mlxsw_sp2_kvdl_parts_fini(kvdl);
}

const struct mlxsw_sp_kvdl_ops mlxsw_sp2_kvdl_ops = {
	.priv_size = sizeof(struct mlxsw_sp2_kvdl),
	.init = mlxsw_sp2_kvdl_init,
	.fini = mlxsw_sp2_kvdl_fini,
	.alloc = mlxsw_sp2_kvdl_alloc,
	.free = mlxsw_sp2_kvdl_free,
	.alloc_size_query = mlxsw_sp2_kvdl_alloc_size_query,
};
