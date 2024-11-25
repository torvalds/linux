/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPI controller driver for the nordic52832 SoCs
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>

#define KXR_CACHE_SIZE_MIN			1024
#define KXR_CACHE_SIZE_FIX			8192

#if KXR_CACHE_SIZE_FIX > 0
#define KXR_CACHE_INIT(cache, size) \
	{ 0, 0, 0, 0 }

#define KXR_CACHE_SIZE(cache) \
	sizeof((cache)->buff)
#else
#define KXR_CACHE_INIT(cache, size) \
	{ 0, 0, 0, 0, (size) + KXR_CACHE_SIZE_MIN }

#define KXR_CACHE_SIZE(cache) \
	((cache)->buff_size)
#endif

#pragma once

struct kxr_cache {
	u16 wr_index;
	u16 wr_peek;
	u16 rd_index;
	u16 rd_peek;
#if KXR_CACHE_SIZE_FIX > 0
	u8 buff[KXR_CACHE_SIZE_FIX];
#else
	u16 buff_size;
	u8 buff[KXR_CACHE_SIZE_MIN];
#endif
};

void kxr_cache_init(struct kxr_cache *cache, u16 size);
void kxr_cache_clear(struct kxr_cache *cache);
bool kxr_cache_write_byte(struct kxr_cache *cache, u8 value);
bool kxr_cache_read_byte(struct kxr_cache *cache, u8 *value);
u8 *kxr_cache_read_peek(struct kxr_cache *cache);
void kxr_cache_read_apply(struct kxr_cache *cache);
u8 *kxr_cache_write_peek(struct kxr_cache *cache);
void kxr_cache_write_apply(struct kxr_cache *cache);
u8 *kxr_cache_write(struct kxr_cache *cache, const u8 *buff, u16 length);
u8 *kxr_cache_write_user(struct kxr_cache *cache, const u8 __user *buff, u16 length);
u8 *kxr_cache_read(struct kxr_cache *cache, u8 *buff, u16 length);
u8 *kxr_cache_read_user(struct kxr_cache *cache, u8 __user *buff, u16 length);
u16 kxr_cache_write_remain(struct kxr_cache *cache);
u16 kxr_cache_read_remain(struct kxr_cache *cache);

static inline u16 kxr_cache_index_add(struct kxr_cache *cache, u16 index, u16 value)
{
	return (index + value) % KXR_CACHE_SIZE(cache);
}

static inline u16 kxr_cache_write_add(struct kxr_cache *cache, u16 value)
{
	return kxr_cache_index_add(cache, cache->wr_index, value);
}

static inline u16 kxr_cache_read_add(struct kxr_cache *cache, u16 value)
{
	return kxr_cache_index_add(cache, cache->rd_index, value);
}

static inline bool kxr_cache_is_empty(struct kxr_cache *cache)
{
	return cache->rd_index == cache->wr_index;
}

static inline bool kxr_cache_not_empty(struct kxr_cache *cache)
{
	return cache->rd_index != cache->wr_index;
}

static inline bool kxr_cache_is_full(struct kxr_cache *cache)
{
	return kxr_cache_write_add(cache, 1) == cache->rd_index;
}

static inline bool kxr_cache_not_full(struct kxr_cache *cache)
{
	return kxr_cache_write_add(cache, 1) != cache->rd_index;
}
