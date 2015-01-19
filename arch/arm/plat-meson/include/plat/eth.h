/*
 *
 * Copyright (C) 2010-2014 Amlogic, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __PLAT_MESON_ETH_H
#define __PLAT_MESON_ETH_H

#include <mach/pinmux.h>

struct aml_eth_platdata {
	pinmux_item_t *pinmux_items;
	void (*pinmux_setup)(void);
	void (*pinmux_cleanup)(void);
	void (*clock_enable)(void);
	void (*clock_disable)(void);
	void (*reset)(void);
};

extern struct platform_device meson_device_eth;
extern void meson_eth_set_platdata(struct aml_eth_platdata *pd);

#endif /* __PLAT_MESON_ETH_H */
