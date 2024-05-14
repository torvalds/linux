/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2012-2014 Broadcom Corporation */

#ifdef CONFIG_ARCH_BCM_MOBILE_L2_CACHE
void	kona_l2_cache_init(void);
#else
#define kona_l2_cache_init() ((void)0)
#endif
