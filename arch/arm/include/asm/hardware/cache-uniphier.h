/*
 * Copyright (C) 2015 Masahiro Yamada <yamada.masahiro@socionext.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __CACHE_UNIPHIER_H
#define __CACHE_UNIPHIER_H

#include <linux/types.h>

#ifdef CONFIG_CACHE_UNIPHIER
int uniphier_cache_init(void);
int uniphier_cache_l2_is_enabled(void);
void uniphier_cache_l2_touch_range(unsigned long start, unsigned long end);
void uniphier_cache_l2_set_locked_ways(u32 way_mask);
#else
static inline int uniphier_cache_init(void)
{
	return -ENODEV;
}

static inline int uniphier_cache_l2_is_enabled(void)
{
	return 0;
}

static inline void uniphier_cache_l2_touch_range(unsigned long start,
						 unsigned long end)
{
}

static inline void uniphier_cache_l2_set_locked_ways(u32 way_mask)
{
}
#endif

#endif /* __CACHE_UNIPHIER_H */
