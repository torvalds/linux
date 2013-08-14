/*
 * Allwinner SUNXI "glue layer"
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

extern int register_musb_device(void);
extern void unregister_musb_device(void);

#endif
