// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/errno.h>

#include "item.h"
#include "core_acl_flex_keys.h"

/* For the purpose of the driver, define an internal storage scratchpad
 * that will be used to store key/mask values. For each defined element type
 * define an internal storage geometry.
 *
 * When adding new elements, MLXSW_AFK_ELEMENT_STORAGE_SIZE must be increased
 * accordingly.
 */
static const struct mlxsw_afk_element_info mlxsw_afk_element_infos[] = {
	MLXSW_AFK_ELEMENT_INFO_U32(SRC_SYS_PORT, 0x00, 16, 16),
	MLXSW_AFK_ELEMENT_INFO_BUF(DMAC_32_47, 0x04, 2),
	MLXSW_AFK_ELEMENT_INFO_BUF(DMAC_0_31, 0x06, 4),
	MLXSW_AFK_ELEMENT_INFO_BUF(SMAC_32_47, 0x0A, 2),
	MLXSW_AFK_ELEMENT_INFO_BUF(SMAC_0_31, 0x0C, 4),
	MLXSW_AFK_ELEMENT_INFO_U32(ETHERTYPE, 0x00, 0, 16),
	MLXSW_AFK_ELEMENT_INFO_U32(IP_PROTO, 0x10, 0, 8),
	MLXSW_AFK_ELEMENT_INFO_U32(VID, 0x10, 8, 12),
	MLXSW_AFK_ELEMENT_INFO_U32(PCP, 0x10, 20, 3),
	MLXSW_AFK_ELEMENT_INFO_U32(TCP_FLAGS, 0x10, 23, 9),
	MLXSW_AFK_ELEMENT_INFO_U32(DST_L4_PORT, 0x14, 0, 16),
	MLXSW_AFK_ELEMENT_INFO_U32(SRC_L4_PORT, 0x14, 16, 16),
	MLXSW_AFK_ELEMENT_INFO_U32(IP_TTL_, 0x18, 0, 8),
	MLXSW_AFK_ELEMENT_INFO_U32(IP_ECN, 0x18, 9, 2),
	MLXSW_AFK_ELEMENT_INFO_U32(IP_DSCP, 0x18, 11, 6),
	MLXSW_AFK_ELEMENT_INFO_U32(VIRT_ROUTER_MSB, 0x18, 17, 4),
	MLXSW_AFK_ELEMENT_INFO_U32(VIRT_ROUTER_LSB, 0x18, 21, 8),
	MLXSW_AFK_ELEMENT_INFO_BUF(SRC_IP_96_127, 0x20, 4),
	MLXSW_AFK_ELEMENT_INFO_BUF(SRC_IP_64_95, 0x24, 4),
	MLXSW_AFK_ELEMENT_INFO_BUF(SRC_IP_32_63, 0x28, 4),
	MLXSW_AFK_ELEMENT_INFO_BUF(SRC_IP_0_31, 0x2C, 4),
	MLXSW_AFK_ELEMENT_INFO_BUF(DST_IP_96_127, 0x30, 4),
	MLXSW_AFK_ELEMENT_INFO_BUF(DST_IP_64_95, 0x34, 4),
	MLXSW_AFK_ELEMENT_INFO_BUF(DST_IP_32_63, 0x38, 4),
	MLXSW_AFK_ELEMENT_INFO_BUF(DST_IP_0_31, 0x3C, 4),
	MLXSW_AFK_ELEMENT_INFO_U32(FDB_MISS, 0x40, 0, 1),
};

struct mlxsw_afk {
	struct list_head key_info_list;
	unsigned int max_blocks;
	const struct mlxsw_afk_ops *ops;
	const struct mlxsw_afk_block *blocks;
	unsigned int blocks_count;
};

static bool mlxsw_afk_blocks_check(struct mlxsw_afk *mlxsw_afk)
{
	int i;
	int j;

	for (i = 0; i < mlxsw_afk->blocks_count; i++) {
		const struct mlxsw_afk_block *block = &mlxsw_afk->blocks[i];

		for (j = 0; j < block->instances_count; j++) {
			const struct mlxsw_afk_element_info *elinfo;
			struct mlxsw_afk_element_inst *elinst;

			elinst = &block->instances[j];
			elinfo = &mlxsw_afk_element_infos[elinst->element];
			if (elinst->type != elinfo->type ||
			    (!elinst->avoid_size_check &&
			     elinst->item.size.bits !=
			     elinfo->item.size.bits))
				return false;
		}
	}
	return true;
}

struct mlxsw_afk *mlxsw_afk_create(unsigned int max_blocks,
				   const struct mlxsw_afk_ops *ops)
{
	struct mlxsw_afk *mlxsw_afk;

	mlxsw_afk = kzalloc(sizeof(*mlxsw_afk), GFP_KERNEL);
	if (!mlxsw_afk)
		return NULL;
	INIT_LIST_HEAD(&mlxsw_afk->key_info_list);
	mlxsw_afk->max_blocks = max_blocks;
	mlxsw_afk->ops = ops;
	mlxsw_afk->blocks = ops->blocks;
	mlxsw_afk->blocks_count = ops->blocks_count;
	WARN_ON(!mlxsw_afk_blocks_check(mlxsw_afk));
	return mlxsw_afk;
}
EXPORT_SYMBOL(mlxsw_afk_create);

void mlxsw_afk_destroy(struct mlxsw_afk *mlxsw_afk)
{
	WARN_ON(!list_empty(&mlxsw_afk->key_info_list));
	kfree(mlxsw_afk);
}
EXPORT_SYMBOL(mlxsw_afk_destroy);

struct mlxsw_afk_key_info {
	struct list_head list;
	unsigned int ref_count;
	unsigned int blocks_count;
	int element_to_block[MLXSW_AFK_ELEMENT_MAX]; /* index is element, value
						      * is index inside "blocks"
						      */
	struct mlxsw_afk_element_usage elusage;
	const struct mlxsw_afk_block *blocks[];
};

static bool
mlxsw_afk_key_info_elements_eq(struct mlxsw_afk_key_info *key_info,
			       struct mlxsw_afk_element_usage *elusage)
{
	return memcmp(&key_info->elusage, elusage, sizeof(*elusage)) == 0;
}

static struct mlxsw_afk_key_info *
mlxsw_afk_key_info_find(struct mlxsw_afk *mlxsw_afk,
			struct mlxsw_afk_element_usage *elusage)
{
	struct mlxsw_afk_key_info *key_info;

	list_for_each_entry(key_info, &mlxsw_afk->key_info_list, list) {
		if (mlxsw_afk_key_info_elements_eq(key_info, elusage))
			return key_info;
	}
	return NULL;
}

struct mlxsw_afk_picker {
	DECLARE_BITMAP(element, MLXSW_AFK_ELEMENT_MAX);
	unsigned int total;
};

static void mlxsw_afk_picker_count_hits(struct mlxsw_afk *mlxsw_afk,
					struct mlxsw_afk_picker *picker,
					enum mlxsw_afk_element element)
{
	int i;
	int j;

	for (i = 0; i < mlxsw_afk->blocks_count; i++) {
		const struct mlxsw_afk_block *block = &mlxsw_afk->blocks[i];

		for (j = 0; j < block->instances_count; j++) {
			struct mlxsw_afk_element_inst *elinst;

			elinst = &block->instances[j];
			if (elinst->element == element) {
				__set_bit(element, picker[i].element);
				picker[i].total++;
			}
		}
	}
}

static void mlxsw_afk_picker_subtract_hits(struct mlxsw_afk *mlxsw_afk,
					   struct mlxsw_afk_picker *picker,
					   int block_index)
{
	DECLARE_BITMAP(hits_element, MLXSW_AFK_ELEMENT_MAX);
	int i;
	int j;

	memcpy(&hits_element, &picker[block_index].element,
	       sizeof(hits_element));

	for (i = 0; i < mlxsw_afk->blocks_count; i++) {
		for_each_set_bit(j, hits_element, MLXSW_AFK_ELEMENT_MAX) {
			if (__test_and_clear_bit(j, picker[i].element))
				picker[i].total--;
		}
	}
}

static int mlxsw_afk_picker_most_hits_get(struct mlxsw_afk *mlxsw_afk,
					  struct mlxsw_afk_picker *picker)
{
	int most_index = -EINVAL; /* Should never happen to return this */
	int most_hits = 0;
	int i;

	for (i = 0; i < mlxsw_afk->blocks_count; i++) {
		if (picker[i].total > most_hits) {
			most_hits = picker[i].total;
			most_index = i;
		}
	}
	return most_index;
}

static int mlxsw_afk_picker_key_info_add(struct mlxsw_afk *mlxsw_afk,
					 struct mlxsw_afk_picker *picker,
					 int block_index,
					 struct mlxsw_afk_key_info *key_info)
{
	enum mlxsw_afk_element element;

	if (key_info->blocks_count == mlxsw_afk->max_blocks)
		return -EINVAL;

	for_each_set_bit(element, picker[block_index].element,
			 MLXSW_AFK_ELEMENT_MAX) {
		key_info->element_to_block[element] = key_info->blocks_count;
		mlxsw_afk_element_usage_add(&key_info->elusage, element);
	}

	key_info->blocks[key_info->blocks_count] =
					&mlxsw_afk->blocks[block_index];
	key_info->blocks_count++;
	return 0;
}

static int mlxsw_afk_picker(struct mlxsw_afk *mlxsw_afk,
			    struct mlxsw_afk_key_info *key_info,
			    struct mlxsw_afk_element_usage *elusage)
{
	struct mlxsw_afk_picker *picker;
	enum mlxsw_afk_element element;
	int err;

	picker = kcalloc(mlxsw_afk->blocks_count, sizeof(*picker), GFP_KERNEL);
	if (!picker)
		return -ENOMEM;

	/* Since the same elements could be present in multiple blocks,
	 * we must find out optimal block list in order to make the
	 * block count as low as possible.
	 *
	 * First, we count hits. We go over all available blocks and count
	 * how many of requested elements are covered by each.
	 *
	 * Then in loop, we find block with most hits and add it to
	 * output key_info. Then we have to subtract this block hits so
	 * the next iteration will find most suitable block for
	 * the rest of requested elements.
	 */

	mlxsw_afk_element_usage_for_each(element, elusage)
		mlxsw_afk_picker_count_hits(mlxsw_afk, picker, element);

	do {
		int block_index;

		block_index = mlxsw_afk_picker_most_hits_get(mlxsw_afk, picker);
		if (block_index < 0) {
			err = block_index;
			goto out;
		}
		err = mlxsw_afk_picker_key_info_add(mlxsw_afk, picker,
						    block_index, key_info);
		if (err)
			goto out;
		mlxsw_afk_picker_subtract_hits(mlxsw_afk, picker, block_index);
	} while (!mlxsw_afk_key_info_elements_eq(key_info, elusage));

	err = 0;
out:
	kfree(picker);
	return err;
}

static struct mlxsw_afk_key_info *
mlxsw_afk_key_info_create(struct mlxsw_afk *mlxsw_afk,
			  struct mlxsw_afk_element_usage *elusage)
{
	struct mlxsw_afk_key_info *key_info;
	int err;

	key_info = kzalloc(struct_size(key_info, blocks, mlxsw_afk->max_blocks),
			   GFP_KERNEL);
	if (!key_info)
		return ERR_PTR(-ENOMEM);
	err = mlxsw_afk_picker(mlxsw_afk, key_info, elusage);
	if (err)
		goto err_picker;
	list_add(&key_info->list, &mlxsw_afk->key_info_list);
	key_info->ref_count = 1;
	return key_info;

err_picker:
	kfree(key_info);
	return ERR_PTR(err);
}

static void mlxsw_afk_key_info_destroy(struct mlxsw_afk_key_info *key_info)
{
	list_del(&key_info->list);
	kfree(key_info);
}

struct mlxsw_afk_key_info *
mlxsw_afk_key_info_get(struct mlxsw_afk *mlxsw_afk,
		       struct mlxsw_afk_element_usage *elusage)
{
	struct mlxsw_afk_key_info *key_info;

	key_info = mlxsw_afk_key_info_find(mlxsw_afk, elusage);
	if (key_info) {
		key_info->ref_count++;
		return key_info;
	}
	return mlxsw_afk_key_info_create(mlxsw_afk, elusage);
}
EXPORT_SYMBOL(mlxsw_afk_key_info_get);

void mlxsw_afk_key_info_put(struct mlxsw_afk_key_info *key_info)
{
	if (--key_info->ref_count)
		return;
	mlxsw_afk_key_info_destroy(key_info);
}
EXPORT_SYMBOL(mlxsw_afk_key_info_put);

bool mlxsw_afk_key_info_subset(struct mlxsw_afk_key_info *key_info,
			       struct mlxsw_afk_element_usage *elusage)
{
	return mlxsw_afk_element_usage_subset(elusage, &key_info->elusage);
}
EXPORT_SYMBOL(mlxsw_afk_key_info_subset);

static const struct mlxsw_afk_element_inst *
mlxsw_afk_block_elinst_get(const struct mlxsw_afk_block *block,
			   enum mlxsw_afk_element element)
{
	int i;

	for (i = 0; i < block->instances_count; i++) {
		struct mlxsw_afk_element_inst *elinst;

		elinst = &block->instances[i];
		if (elinst->element == element)
			return elinst;
	}
	return NULL;
}

static const struct mlxsw_afk_element_inst *
mlxsw_afk_key_info_elinst_get(struct mlxsw_afk_key_info *key_info,
			      enum mlxsw_afk_element element,
			      int *p_block_index)
{
	const struct mlxsw_afk_element_inst *elinst;
	const struct mlxsw_afk_block *block;
	int block_index;

	if (WARN_ON(!test_bit(element, key_info->elusage.usage)))
		return NULL;
	block_index = key_info->element_to_block[element];
	block = key_info->blocks[block_index];

	elinst = mlxsw_afk_block_elinst_get(block, element);
	if (WARN_ON(!elinst))
		return NULL;

	*p_block_index = block_index;
	return elinst;
}

u16
mlxsw_afk_key_info_block_encoding_get(const struct mlxsw_afk_key_info *key_info,
				      int block_index)
{
	return key_info->blocks[block_index]->encoding;
}
EXPORT_SYMBOL(mlxsw_afk_key_info_block_encoding_get);

unsigned int
mlxsw_afk_key_info_blocks_count_get(const struct mlxsw_afk_key_info *key_info)
{
	return key_info->blocks_count;
}
EXPORT_SYMBOL(mlxsw_afk_key_info_blocks_count_get);

void mlxsw_afk_values_add_u32(struct mlxsw_afk_element_values *values,
			      enum mlxsw_afk_element element,
			      u32 key_value, u32 mask_value)
{
	const struct mlxsw_afk_element_info *elinfo =
				&mlxsw_afk_element_infos[element];
	const struct mlxsw_item *storage_item = &elinfo->item;

	if (!mask_value)
		return;
	if (WARN_ON(elinfo->type != MLXSW_AFK_ELEMENT_TYPE_U32))
		return;
	__mlxsw_item_set32(values->storage.key, storage_item, 0, key_value);
	__mlxsw_item_set32(values->storage.mask, storage_item, 0, mask_value);
	mlxsw_afk_element_usage_add(&values->elusage, element);
}
EXPORT_SYMBOL(mlxsw_afk_values_add_u32);

void mlxsw_afk_values_add_buf(struct mlxsw_afk_element_values *values,
			      enum mlxsw_afk_element element,
			      const char *key_value, const char *mask_value,
			      unsigned int len)
{
	const struct mlxsw_afk_element_info *elinfo =
				&mlxsw_afk_element_infos[element];
	const struct mlxsw_item *storage_item = &elinfo->item;

	if (!memchr_inv(mask_value, 0, len)) /* If mask is zero */
		return;
	if (WARN_ON(elinfo->type != MLXSW_AFK_ELEMENT_TYPE_BUF) ||
	    WARN_ON(elinfo->item.size.bytes != len))
		return;
	__mlxsw_item_memcpy_to(values->storage.key, key_value,
			       storage_item, 0);
	__mlxsw_item_memcpy_to(values->storage.mask, mask_value,
			       storage_item, 0);
	mlxsw_afk_element_usage_add(&values->elusage, element);
}
EXPORT_SYMBOL(mlxsw_afk_values_add_buf);

static void mlxsw_sp_afk_encode_u32(const struct mlxsw_item *storage_item,
				    const struct mlxsw_item *output_item,
				    char *storage, char *output, int diff)
{
	u32 value;

	value = __mlxsw_item_get32(storage, storage_item, 0);
	__mlxsw_item_set32(output, output_item, 0, value + diff);
}

static void mlxsw_sp_afk_encode_buf(const struct mlxsw_item *storage_item,
				    const struct mlxsw_item *output_item,
				    char *storage, char *output)
{
	char *storage_data = __mlxsw_item_data(storage, storage_item, 0);
	char *output_data = __mlxsw_item_data(output, output_item, 0);
	size_t len = output_item->size.bytes;

	memcpy(output_data, storage_data, len);
}

static void
mlxsw_sp_afk_encode_one(const struct mlxsw_afk_element_inst *elinst,
			char *output, char *storage, int u32_diff)
{
	const struct mlxsw_item *output_item = &elinst->item;
	const struct mlxsw_afk_element_info *elinfo;
	const struct mlxsw_item *storage_item;

	elinfo = &mlxsw_afk_element_infos[elinst->element];
	storage_item = &elinfo->item;
	if (elinst->type == MLXSW_AFK_ELEMENT_TYPE_U32)
		mlxsw_sp_afk_encode_u32(storage_item, output_item,
					storage, output, u32_diff);
	else if (elinst->type == MLXSW_AFK_ELEMENT_TYPE_BUF)
		mlxsw_sp_afk_encode_buf(storage_item, output_item,
					storage, output);
}

#define MLXSW_SP_AFK_KEY_BLOCK_MAX_SIZE 16

void mlxsw_afk_encode(struct mlxsw_afk *mlxsw_afk,
		      struct mlxsw_afk_key_info *key_info,
		      struct mlxsw_afk_element_values *values,
		      char *key, char *mask)
{
	unsigned int blocks_count =
			mlxsw_afk_key_info_blocks_count_get(key_info);
	char block_mask[MLXSW_SP_AFK_KEY_BLOCK_MAX_SIZE];
	char block_key[MLXSW_SP_AFK_KEY_BLOCK_MAX_SIZE];
	const struct mlxsw_afk_element_inst *elinst;
	enum mlxsw_afk_element element;
	int block_index, i;

	for (i = 0; i < blocks_count; i++) {
		memset(block_key, 0, MLXSW_SP_AFK_KEY_BLOCK_MAX_SIZE);
		memset(block_mask, 0, MLXSW_SP_AFK_KEY_BLOCK_MAX_SIZE);

		mlxsw_afk_element_usage_for_each(element, &values->elusage) {
			elinst = mlxsw_afk_key_info_elinst_get(key_info,
							       element,
							       &block_index);
			if (!elinst || block_index != i)
				continue;

			mlxsw_sp_afk_encode_one(elinst, block_key,
						values->storage.key,
						elinst->u32_key_diff);
			mlxsw_sp_afk_encode_one(elinst, block_mask,
						values->storage.mask, 0);
		}

		mlxsw_afk->ops->encode_block(key, i, block_key);
		mlxsw_afk->ops->encode_block(mask, i, block_mask);
	}
}
EXPORT_SYMBOL(mlxsw_afk_encode);

void mlxsw_afk_clear(struct mlxsw_afk *mlxsw_afk, char *key,
		     int block_start, int block_end)
{
	int i;

	for (i = block_start; i <= block_end; i++)
		mlxsw_afk->ops->clear_block(key, i);
}
EXPORT_SYMBOL(mlxsw_afk_clear);
