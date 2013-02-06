/*
 * linux/arch/arm/mach-exynos/include/mach/naples-tsp.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __NAPLES_TSP_H
#define __NAPLES_TSP_H __FILE__
#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT224
#include <linux/i2c/mxt224.h>
#endif
extern bool is_cable_attached;
void naples_tsp_init(void);
void tsp_charger_infom(bool en);

#endif /* __MIDAS_TSP_H */
