/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_CORE_ACL_FLEX_KEYS_H
#define _MLXSW_CORE_ACL_FLEX_KEYS_H

#include <linux/types.h>
#include <linux/bitmap.h>

#include "item.h"

enum mlxsw_afk_element {
	MLXSW_AFK_ELEMENT_SRC_SYS_PORT,
	MLXSW_AFK_ELEMENT_DMAC_32_47,
	MLXSW_AFK_ELEMENT_DMAC_0_31,
	MLXSW_AFK_ELEMENT_SMAC_32_47,
	MLXSW_AFK_ELEMENT_SMAC_0_31,
	MLXSW_AFK_ELEMENT_ETHERTYPE,
	MLXSW_AFK_ELEMENT_IP_PROTO,
	MLXSW_AFK_ELEMENT_SRC_IP_96_127,
	MLXSW_AFK_ELEMENT_SRC_IP_64_95,
	MLXSW_AFK_ELEMENT_SRC_IP_32_63,
	MLXSW_AFK_ELEMENT_SRC_IP_0_31,
	MLXSW_AFK_ELEMENT_DST_IP_96_127,
	MLXSW_AFK_ELEMENT_DST_IP_64_95,
	MLXSW_AFK_ELEMENT_DST_IP_32_63,
	MLXSW_AFK_ELEMENT_DST_IP_0_31,
	MLXSW_AFK_ELEMENT_DST_L4_PORT,
	MLXSW_AFK_ELEMENT_SRC_L4_PORT,
	MLXSW_AFK_ELEMENT_VID,
	MLXSW_AFK_ELEMENT_PCP,
	MLXSW_AFK_ELEMENT_TCP_FLAGS,
	MLXSW_AFK_ELEMENT_IP_TTL_,
	MLXSW_AFK_ELEMENT_IP_ECN,
	MLXSW_AFK_ELEMENT_IP_DSCP,
	MLXSW_AFK_ELEMENT_VIRT_ROUTER,
	MLXSW_AFK_ELEMENT_FDB_MISS,
	MLXSW_AFK_ELEMENT_L4_PORT_RANGE,
	MLXSW_AFK_ELEMENT_VIRT_ROUTER_0_3,
	MLXSW_AFK_ELEMENT_VIRT_ROUTER_4_7,
	MLXSW_AFK_ELEMENT_VIRT_ROUTER_MSB,
	MLXSW_AFK_ELEMENT_MAX,
};

enum mlxsw_afk_element_type {
	MLXSW_AFK_ELEMENT_TYPE_U32,
	MLXSW_AFK_ELEMENT_TYPE_BUF,
};

struct mlxsw_afk_element_info {
	enum mlxsw_afk_element element; /* element ID */
	enum mlxsw_afk_element_type type;
	struct mlxsw_item item; /* element geometry in internal storage */
};

#define MLXSW_AFK_ELEMENT_INFO(_type, _element, _offset, _shift, _size)		\
	[MLXSW_AFK_ELEMENT_##_element] = {					\
		.element = MLXSW_AFK_ELEMENT_##_element,			\
		.type = _type,							\
		.item = {							\
			.offset = _offset,					\
			.shift = _shift,					\
			.size = {.bits = _size},				\
			.name = #_element,					\
		},								\
	}

#define MLXSW_AFK_ELEMENT_INFO_U32(_element, _offset, _shift, _size)		\
	MLXSW_AFK_ELEMENT_INFO(MLXSW_AFK_ELEMENT_TYPE_U32,			\
			       _element, _offset, _shift, _size)

#define MLXSW_AFK_ELEMENT_INFO_BUF(_element, _offset, _size)			\
	MLXSW_AFK_ELEMENT_INFO(MLXSW_AFK_ELEMENT_TYPE_BUF,			\
			       _element, _offset, 0, _size)

#define MLXSW_AFK_ELEMENT_STORAGE_SIZE 0x44

struct mlxsw_afk_element_inst { /* element instance in actual block */
	enum mlxsw_afk_element element;
	enum mlxsw_afk_element_type type;
	struct mlxsw_item item; /* element geometry in block */
	int u32_key_diff; /* in case value needs to be adjusted before write
			   * this diff is here to handle that
			   */
	bool avoid_size_check;
};

#define MLXSW_AFK_ELEMENT_INST(_type, _element, _offset,			\
			       _shift, _size, _u32_key_diff, _avoid_size_check)	\
	{									\
		.element = MLXSW_AFK_ELEMENT_##_element,			\
		.type = _type,							\
		.item = {							\
			.offset = _offset,					\
			.shift = _shift,					\
			.size = {.bits = _size},				\
			.name = #_element,					\
		},								\
		.u32_key_diff = _u32_key_diff,					\
		.avoid_size_check = _avoid_size_check,				\
	}

#define MLXSW_AFK_ELEMENT_INST_U32(_element, _offset, _shift, _size)		\
	MLXSW_AFK_ELEMENT_INST(MLXSW_AFK_ELEMENT_TYPE_U32,			\
			       _element, _offset, _shift, _size, 0, false)

#define MLXSW_AFK_ELEMENT_INST_EXT_U32(_element, _offset,			\
				       _shift, _size, _key_diff,		\
				       _avoid_size_check)			\
	MLXSW_AFK_ELEMENT_INST(MLXSW_AFK_ELEMENT_TYPE_U32,			\
			       _element, _offset, _shift, _size,		\
			       _key_diff, _avoid_size_check)

#define MLXSW_AFK_ELEMENT_INST_BUF(_element, _offset, _size)			\
	MLXSW_AFK_ELEMENT_INST(MLXSW_AFK_ELEMENT_TYPE_BUF,			\
			       _element, _offset, 0, _size, 0, false)

struct mlxsw_afk_block {
	u16 encoding; /* block ID */
	struct mlxsw_afk_element_inst *instances;
	unsigned int instances_count;
	bool high_entropy;
};

#define MLXSW_AFK_BLOCK(_encoding, _instances)					\
	{									\
		.encoding = _encoding,						\
		.instances = _instances,					\
		.instances_count = ARRAY_SIZE(_instances),			\
	}

#define MLXSW_AFK_BLOCK_HIGH_ENTROPY(_encoding, _instances)			\
	{									\
		.encoding = _encoding,						\
		.instances = _instances,					\
		.instances_count = ARRAY_SIZE(_instances),			\
		.high_entropy = true,						\
	}

struct mlxsw_afk_element_usage {
	DECLARE_BITMAP(usage, MLXSW_AFK_ELEMENT_MAX);
};

#define mlxsw_afk_element_usage_for_each(element, elusage)			\
	for_each_set_bit(element, (elusage)->usage, MLXSW_AFK_ELEMENT_MAX)

static inline void
mlxsw_afk_element_usage_add(struct mlxsw_afk_element_usage *elusage,
			    enum mlxsw_afk_element element)
{
	__set_bit(element, elusage->usage);
}

static inline void
mlxsw_afk_element_usage_zero(struct mlxsw_afk_element_usage *elusage)
{
	bitmap_zero(elusage->usage, MLXSW_AFK_ELEMENT_MAX);
}

static inline void
mlxsw_afk_element_usage_fill(struct mlxsw_afk_element_usage *elusage,
			     const enum mlxsw_afk_element *elements,
			     unsigned int elements_count)
{
	int i;

	mlxsw_afk_element_usage_zero(elusage);
	for (i = 0; i < elements_count; i++)
		mlxsw_afk_element_usage_add(elusage, elements[i]);
}

static inline bool
mlxsw_afk_element_usage_subset(struct mlxsw_afk_element_usage *elusage_small,
			       struct mlxsw_afk_element_usage *elusage_big)
{
	int i;

	for (i = 0; i < MLXSW_AFK_ELEMENT_MAX; i++)
		if (test_bit(i, elusage_small->usage) &&
		    !test_bit(i, elusage_big->usage))
			return false;
	return true;
}

struct mlxsw_afk;

struct mlxsw_afk_ops {
	const struct mlxsw_afk_block *blocks;
	unsigned int blocks_count;
	void (*encode_block)(char *output, int block_index, char *block);
	void (*clear_block)(char *output, int block_index);
};

struct mlxsw_afk *mlxsw_afk_create(unsigned int max_blocks,
				   const struct mlxsw_afk_ops *ops);
void mlxsw_afk_destroy(struct mlxsw_afk *mlxsw_afk);

struct mlxsw_afk_key_info;

struct mlxsw_afk_key_info *
mlxsw_afk_key_info_get(struct mlxsw_afk *mlxsw_afk,
		       struct mlxsw_afk_element_usage *elusage);
void mlxsw_afk_key_info_put(struct mlxsw_afk_key_info *key_info);
bool mlxsw_afk_key_info_subset(struct mlxsw_afk_key_info *key_info,
			       struct mlxsw_afk_element_usage *elusage);

u16
mlxsw_afk_key_info_block_encoding_get(const struct mlxsw_afk_key_info *key_info,
				      int block_index);
unsigned int
mlxsw_afk_key_info_blocks_count_get(const struct mlxsw_afk_key_info *key_info);

struct mlxsw_afk_element_values {
	struct mlxsw_afk_element_usage elusage;
	struct {
		char key[MLXSW_AFK_ELEMENT_STORAGE_SIZE];
		char mask[MLXSW_AFK_ELEMENT_STORAGE_SIZE];
	} storage;
};

void mlxsw_afk_values_add_u32(struct mlxsw_afk_element_values *values,
			      enum mlxsw_afk_element element,
			      u32 key_value, u32 mask_value);
void mlxsw_afk_values_add_buf(struct mlxsw_afk_element_values *values,
			      enum mlxsw_afk_element element,
			      const char *key_value, const char *mask_value,
			      unsigned int len);
void mlxsw_afk_encode(struct mlxsw_afk *mlxsw_afk,
		      struct mlxsw_afk_key_info *key_info,
		      struct mlxsw_afk_element_values *values,
		      char *key, char *mask);
void mlxsw_afk_clear(struct mlxsw_afk *mlxsw_afk, char *key,
		     int block_start, int block_end);

#endif
