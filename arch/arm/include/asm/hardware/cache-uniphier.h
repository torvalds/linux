/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2015-2016 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
 */

#ifndef __CACHE_UNIPHIER_H
#define __CACHE_UNIPHIER_H

#include <linux/errno.h>

#ifdef CONFIG_CACHE_UNIPHIER
int uniphier_cache_init(void);
#else
static inline int uniphier_cache_init(void)
{
	return -ENODEV;
}
#endif

#endif /* __CACHE_UNIPHIER_H */
