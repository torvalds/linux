/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __EXYNOS_BTS_H_
#define __EXYNOS_BTS_H_
#ifdef CONFIG_EXYNOS5410_BTS
#include <mach/devfreq.h>

void bts_change_bustraffic(struct devfreq_info *info, unsigned long event);
void bts_initialize(char *pd_name, bool on);
void bts_set_bw(unsigned int bw);
#else
#define bts_change_bustraffic(a, b) do {} while (0)
#define bts_initialize(a, b) do {} while (0)
#define bts_set_bw(a) do {} while (0)
#endif
#endif
