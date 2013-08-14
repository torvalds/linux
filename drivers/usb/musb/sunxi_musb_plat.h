/*
 * Allwinner SUNXI MUSB platform setup
 *
 * Copyright © 2013 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#ifndef __ASM_ARCH_SUNXI_PLAT_MUSB_H
#define __ASM_ARCH_SUNXI_PLAT_MUSB_H

#include <linux/platform_device.h>

struct sunxi_musb_board_priv;

struct sunxi_musb_board_data {
	struct sunxi_musb_board_priv *(*init)(struct device *dev);
	void (*exit)(struct sunxi_musb_board_priv *priv);
	int (*set_phy_power)(struct sunxi_musb_board_priv *priv, int on);
};

extern int register_musb_device(void);
extern void unregister_musb_device(void);

#endif
