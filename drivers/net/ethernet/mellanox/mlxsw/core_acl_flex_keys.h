/*
 * drivers/net/ethernet/mellanox/mlxsw/core_acl_flex_keys.h
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

#ifndef _MLXSW_CORE_ACL_FLEX_KEYS_H
#define _MLXSW_CORE_ACL_FLEX_KEYS_H

#include <linux/types.h>
#include <linux/bitmap.h>

#include "item.h"

enum mlxsw_afk_element {
	MLXSW_AFK_ELEMENT_SRC_SYS_PORT,
	MLXSW_AFK_ELEMENT_DMAC,
	MLXSW_AFK_ELEMENT_SMAC,
	MLXSW_AFK_ELEMENT_ETHERTYPE,
	MLXSW_AFK_ELEMENT_IP_PROTO,
	MLXSW_AFK_ELEMENT_SRC_IP4,
	MLXSW_AFK_ELEMENT_DST_IP4,
	MLXSW_AFK_ELEMENT_SRC_IP6_HI,
	MLXSW_AFK_ELEMENT_SRC_IP6_LO,
	MLXSW_AFK_ELEMENT_DST_IP6_HI,
	MLXSW_AFK_ELEMENT_DST_IP6_LO,
	MLXSW_AFK_ELEMENT_DST_L4_PORT,
	MLXSW_AFK_ELEMENT_SRC_L4_PORT,
	MLXSW_AFK_ELEMENT_VID,
	MLXSW_AFK_ELEMENT_PCP,
	MLXSW_AFK_ELEMENT_TCP_FLAGS,
	MLXSW_AFK_ELEMENT_IP_TTL_,
	MLXSW_AFK_ELEMENT_IP_ECN,
	MLXSW_AFK_ELEMENT_IP_DSCP,
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

/* For the purpose of the driver, define an internal storage scratchpad
 * that will be used to store key/mask values. For each defined element type
 * define an internal storage geometry.
 */
static const struct mlxsw_afk_element_info mlxsw_afk_element_infos[] = {
	MLXSW_AFK_ELEMENT_INFO_U32(SRC_SYS_PORT, 0x00, 16, 16),
	MLXSW_AFK_ELEMENT_INFO_BUF(DMAC, 0x04, 6),
	MLXSW_AFK_ELEMENT_INFO_BUF(SMAC, 0x0A, 6),
	MLXSW_AFK_ELEMENT_INFO_U32(ETHERTYPE, 0x00, 0, 16),
	MLXSW_AFK_ELEMENT_INFO_U32(IP_PROTO, 0x10, 0, 8),
	MLXSW_AFK_ELEMENT_INFO_U32(VID, 0x10, 8, 12),
	MLXSW_AFK_ELEMENT_INFO_U32(PCP, 0x10, 20, 3),
	MLXSW_AFK_ELEMENT_INFO_U32(TCP_FLAGS, 0x10, 23, 9),
	MLXSW_AFK_ELEMENT_INFO_U32(IP_TTL_, 0x14, 0, 8),
	MLXSW_AFK_ELEMENT_INFO_U32(IP_ECN, 0x14, 9, 2),
	MLXSW_AFK_ELEMENT_INFO_U32(IP_DSCP, 0x14, 11, 6),
	MLXSW_AFK_ELEMENT_INFO_U32(SRC_IP4, 0x18, 0, 32),
	MLXSW_AFK_ELEMENT_INFO_U32(DST_IP4, 0x1C, 0, 32),
	MLXSW_AFK_ELEMENT_INFO_BUF(SRC_IP6_HI, 0x18, 8),
	MLXSW_AFK_ELEMENT_INFO_BUF(SRC_IP6_LO, 0x20, 8),
	MLXSW_AFK_ELEMENT_INFO_BUF(DST_IP6_HI, 0x28, 8),
	MLXSW_AFK_ELEMENT_INFO_BUF(DST_IP6_LO, 0x30, 8),
	MLXSW_AFK_ELEMENT_INFO_U32(DST_L4_PORT, 0x14, 0, 16),
	MLXSW_AFK_ELEMENT_INFO_U32(SRC_L4_PORT, 0x14, 16, 16),
};

#define MLXSW_AFK_ELEMENT_STORAGE_SIZE 0x38

struct mlxsw_afk_element_inst { /* element instance in actual block */
	const struct mlxsw_afk_element_info *info;
	enum mlxsw_afk_element_type type;
	struct mlxsw_item item; /* element geometry in block */
};

#define MLXSW_AFK_ELEMENT_INST(_type, _element, _offset, _shift, _size)		\
	{									\
		.info = &mlxsw_afk_element_infos[MLXSW_AFK_ELEMENT_##_element],	\
		.type = _type,							\
		.item = {							\
			.offset = _offset,					\
			.shift = _shift,					\
			.size = {.bits = _size},				\
			.name = #_element,					\
		},								\
	}

#define MLXSW_AFK_ELEMENT_INST_U32(_element, _offset, _shift, _size)		\
	MLXSW_AFK_ELEMENT_INST(MLXSW_AFK_ELEMENT_TYPE_U32,			\
			       _element, _offset, _shift, _size)

#define MLXSW_AFK_ELEMENT_INST_BUF(_element, _offset, _size)			\
	MLXSW_AFK_ELEMENT_INST(MLXSW_AFK_ELEMENT_TYPE_BUF,			\
			       _element, _offset, 0, _size)

struct mlxsw_afk_block {
	u16 encoding; /* block ID */
	struct mlxsw_afk_element_inst *instances;
	unsigned int instances_count;
};

#define MLXSW_AFK_BLOCK(_encoding, _instances)					\
	{									\
		.encoding = _encoding,						\
		.instances = _instances,					\
		.instances_count = ARRAY_SIZE(_instances),			\
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

struct mlxsw_afk *mlxsw_afk_create(unsigned int max_blocks,
				   const struct mlxsw_afk_block *blocks,
				   unsigned int blocks_count);
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
void mlxsw_afk_encode(struct mlxsw_afk_key_info *key_info,
		      struct mlxsw_afk_element_values *values,
		      char *key, char *mask);

#endif
