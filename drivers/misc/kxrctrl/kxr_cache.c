// SPDX-License-Identifier: GPL-2.0-only
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

#include "kxr_cache.h"
#include "kxr_aphost.h"

#ifndef CONFIG_REDUCE_JUDGE
#define CONFIG_REDUCE_JUDGE		1
#endif

static u16 kxr_cache_write_remain_dma(struct kxr_cache *cache)
{
	u16 wr_index = cache->wr_index;
	u16 rd_index = cache->rd_index;
	u16 remain;

#if CONFIG_REDUCE_JUDGE == 0
	if (wr_index < rd_index) {
		remain = rd_index - wr_index;
	} else {
		remain = KXR_CACHE_SIZE(cache) - wr_index;
		if (rd_index > 0)
			return remain;
	}

	return remain - 1;
#else
	u16 buff_size = KXR_CACHE_SIZE(cache);
	u16 wr_remain = buff_size - wr_index - 1;

	remain = (wr_remain + rd_index) % buff_size;
	return remain > wr_remain ? wr_remain + 1 : remain;
#endif
}

static u16 kxr_cache_read_remain_dma(struct kxr_cache *cache)
{
	u16 wr_index = cache->wr_index;
	u16 rd_index = cache->rd_index;

	if (rd_index <= wr_index)
		return wr_index - rd_index;

	return KXR_CACHE_SIZE(cache) - rd_index;
}

void kxr_cache_init(struct kxr_cache *cache, u16 size)
{
#if KXR_CACHE_SIZE_FIX == 0
	KXR_CACHE_SIZE(cache) = size + sizeof(cache->buff);
#endif

	kxr_cache_clear(cache);
}

void kxr_cache_clear(struct kxr_cache *cache)
{
	cache->rd_index = cache->wr_index = 0;
	cache->rd_peek = cache->wr_peek = 0;
}

bool kxr_cache_write_byte(struct kxr_cache *cache, u8 value)
{
	u16 index = kxr_cache_write_add(cache, 1);

	if (index == cache->rd_index)
		return false;

	cache->buff[cache->wr_index] = value;
	cache->wr_index = index;

	return true;
}

bool kxr_cache_read_byte(struct kxr_cache *cache, u8 *value)
{
	u16 index = cache->rd_index;

	if (index == cache->wr_index)
		return false;

	*value = cache->buff[index];
	cache->rd_index = kxr_cache_index_add(cache, index, 1);

	return true;
}

u8 *kxr_cache_read_peek(struct kxr_cache *cache)
{
	cache->rd_peek = kxr_cache_read_remain_dma(cache);
	return cache->buff + cache->rd_index;
}

void kxr_cache_read_apply(struct kxr_cache *cache)
{
	cache->rd_index = kxr_cache_read_add(cache, cache->rd_peek);
	cache->rd_peek = 0;
}

u8 *kxr_cache_write_peek(struct kxr_cache *cache)
{
	cache->wr_peek = kxr_cache_write_remain_dma(cache);
	return cache->buff + cache->wr_index;
}

void kxr_cache_write_apply(struct kxr_cache *cache)
{
	cache->wr_index = kxr_cache_write_add(cache, cache->wr_peek);
	cache->wr_peek = 0;
}

u8 *kxr_cache_write(struct kxr_cache *cache, const u8 *buff, u16 length)
{
	while (length > 0) {
		u16 remain = kxr_cache_write_remain_dma(cache);

		if (remain == 0)
			break;

		if (remain > length)
			remain = length;

		memcpy(cache->buff + cache->wr_index, buff, remain);
		cache->wr_index = kxr_cache_write_add(cache, remain);

		length -= remain;
		buff += remain;
	}

	return (u8 *) buff;
}

u8 *kxr_cache_write_user(struct kxr_cache *cache, const u8 __user *buff, u16 length)
{
	while (length > 0) {
		u16 remain = kxr_cache_write_remain_dma(cache);

		if (remain == 0)
			break;

		if (remain > length)
			remain = length;

		if (copy_from_user(cache->buff + cache->wr_index, buff, remain) > 0)
			break;

		cache->wr_index = kxr_cache_write_add(cache, remain);

		length -= remain;
		buff += remain;
	}

	return (u8 *) buff;
}

u8 *kxr_cache_read(struct kxr_cache *cache, u8 *buff, u16 length)
{
	while (length > 0) {
		u16 remain = kxr_cache_read_remain_dma(cache);

		if (remain == 0)
			break;

		if (remain > length)
			remain = length;

		memcpy(buff, cache->buff + cache->rd_index, remain);
		cache->rd_index = kxr_cache_read_add(cache, remain);

		length -= remain;
		buff += remain;
	}

	return buff;
}

u8 *kxr_cache_read_user(struct kxr_cache *cache, u8 __user *buff, u16 length)
{
	while (length > 0) {
		u16 remain = kxr_cache_read_remain_dma(cache);

		if (remain == 0)
			break;

		if (remain > length)
			remain = length;

		if (copy_to_user(buff, cache->buff + cache->rd_index, remain) > 0)
			break;

		cache->rd_index = kxr_cache_read_add(cache, remain);

		length -= remain;
		buff += remain;
	}

	return buff;
}

u16 kxr_cache_write_remain(struct kxr_cache *cache)
{
	u16 wr_index = cache->wr_index;
	u16 rd_index = cache->rd_index;

#if CONFIG_REDUCE_JUDGE == 0
	if (wr_index < rd_index)
		return rd_index - wr_index - 1;

	return KXR_CACHE_SIZE(cache) - wr_index + rd_index - 1;
#else
	u16 buff_size = KXR_CACHE_SIZE(cache);

	return (buff_size + rd_index - wr_index - 1) % buff_size;
#endif
}

u16 kxr_cache_read_remain(struct kxr_cache *cache)
{
	u16 wr_index = cache->wr_index;
	u16 rd_index = cache->rd_index;

#if CONFIG_REDUCE_JUDGE == 0
	if (rd_index <= wr_index)
		return wr_index - rd_index;

	return KXR_CACHE_SIZE(cache) - rd_index + wr_index;
#else
	u16 buff_size = KXR_CACHE_SIZE(cache);

	return (buff_size + wr_index - rd_index) % buff_size;
#endif
}
