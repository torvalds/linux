/*
 * drivers/net/ethernet/mellanox/mlxsw/core_acl_flex_keys.c
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Jiri Pirko <jiri@mellanox.com>
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
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/errno.h>

#include "item.h"
#include "core_acl_flex_keys.h"

struct mlxsw_afk {
	struct list_head key_info_list;
	unsigned int max_blocks;
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
			struct mlxsw_afk_element_inst *elinst;

			elinst = &block->instances[j];
			if (elinst->type != elinst->info->type ||
			    elinst->item.size.bits !=
			    elinst->info->item.size.bits)
				return false;
		}
	}
	return true;
}

struct mlxsw_afk *mlxsw_afk_create(unsigned int max_blocks,
				   const struct mlxsw_afk_block *blocks,
				   unsigned int blocks_count)
{
	struct mlxsw_afk *mlxsw_afk;

	mlxsw_afk = kzalloc(sizeof(*mlxsw_afk), GFP_KERNEL);
	if (!mlxsw_afk)
		return NULL;
	INIT_LIST_HEAD(&mlxsw_afk->key_info_list);
	mlxsw_afk->max_blocks = max_blocks;
	mlxsw_afk->blocks = blocks;
	mlxsw_afk->blocks_count = blocks_count;
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
	const struct mlxsw_afk_block *blocks[0];
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
	struct {
		DECLARE_BITMAP(element, MLXSW_AFK_ELEMENT_MAX);
		unsigned int total;
	} hits[0];
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
			if (elinst->info->element == element) {
				__set_bit(element, picker->hits[i].element);
				picker->hits[i].total++;
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

	memcpy(&hits_element, &picker->hits[block_index].element,
	       sizeof(hits_element));

	for (i = 0; i < mlxsw_afk->blocks_count; i++) {
		for_each_set_bit(j, hits_element, MLXSW_AFK_ELEMENT_MAX) {
			if (__test_and_clear_bit(j, picker->hits[i].element))
				picker->hits[i].total--;
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
		if (picker->hits[i].total > most_hits) {
			most_hits = picker->hits[i].total;
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

	for_each_set_bit(element, picker->hits[block_index].element,
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
	size_t alloc_size;
	int err;

	alloc_size = sizeof(picker->hits[0]) * mlxsw_afk->blocks_count;
	picker = kzalloc(alloc_size, GFP_KERNEL);
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
	size_t alloc_size;
	int err;

	alloc_size = sizeof(*key_info) +
		     sizeof(key_info->blocks[0]) * mlxsw_afk->max_blocks;
	key_info = kzalloc(alloc_size, GFP_KERNEL);
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
		if (elinst->info->element == element)
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

static void mlxsw_afk_encode_u32(const struct mlxsw_item *storage_item,
				 const struct mlxsw_item *output_item,
				 char *storage, char *output_indexed)
{
	u32 value;

	value = __mlxsw_item_get32(storage, storage_item, 0);
	__mlxsw_item_set32(output_indexed, output_item, 0, value);
}

static void mlxsw_afk_encode_buf(const struct mlxsw_item *storage_item,
				 const struct mlxsw_item *output_item,
				 char *storage, char *output_indexed)
{
	char *storage_data = __mlxsw_item_data(storage, storage_item, 0);
	char *output_data = __mlxsw_item_data(output_indexed, output_item, 0);
	size_t len = output_item->size.bytes;

	memcpy(output_data, storage_data, len);
}

#define MLXSW_AFK_KEY_BLOCK_SIZE 16

static void mlxsw_afk_encode_one(const struct mlxsw_afk_element_inst *elinst,
				 int block_index, char *storage, char *output)
{
	char *output_indexed = output + block_index * MLXSW_AFK_KEY_BLOCK_SIZE;
	const struct mlxsw_item *storage_item = &elinst->info->item;
	const struct mlxsw_item *output_item = &elinst->item;

	if (elinst->type == MLXSW_AFK_ELEMENT_TYPE_U32)
		mlxsw_afk_encode_u32(storage_item, output_item,
				     storage, output_indexed);
	else if (elinst->type == MLXSW_AFK_ELEMENT_TYPE_BUF)
		mlxsw_afk_encode_buf(storage_item, output_item,
				     storage, output_indexed);
}

void mlxsw_afk_encode(struct mlxsw_afk_key_info *key_info,
		      struct mlxsw_afk_element_values *values,
		      char *key, char *mask)
{
	const struct mlxsw_afk_element_inst *elinst;
	enum mlxsw_afk_element element;
	int block_index;

	mlxsw_afk_element_usage_for_each(element, &values->elusage) {
		elinst = mlxsw_afk_key_info_elinst_get(key_info, element,
						       &block_index);
		if (!elinst)
			continue;
		mlxsw_afk_encode_one(elinst, block_index,
				     values->storage.key, key);
		mlxsw_afk_encode_one(elinst, block_index,
				     values->storage.mask, mask);
	}
}
EXPORT_SYMBOL(mlxsw_afk_encode);
