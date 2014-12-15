/*
 *
 * arch/arm/plat-meson/include/plat/platform_data.h
 *
 * Copyright (C) 2010-2014 Amlogic, Inc.
 *
 * License terms: GNU General Public License (GPL) version 2
 * Basic platform init and mapping functions.
 */

#ifndef __PLAT_MESON_PLATFORM_DATA_H
#define __PLAT_MESON_PLATFORM_DATA_H

#include <linux/platform_device.h>
#include <mach/pinmux.h>

typedef struct {
	int32_t (*setup)(void);
	int32_t (*clear) (void);
} pinmux_cfg_t;

typedef struct {
	struct clk  *clk_src;
	pinmux_cfg_t pinmux_cfg;
	/*add common attribute here*/
	pinmux_set_t  pinmux_set;
} plat_data_public_t;

extern void __init *meson_set_platdata(void *, size_t ,struct platform_device *) ;
#endif
