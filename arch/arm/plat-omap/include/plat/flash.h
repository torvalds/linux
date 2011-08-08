/*
 * Flash support for OMAP1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __OMAP_FLASH_H
#define __OMAP_FLASH_H

#include <linux/mtd/map.h>

struct platform_device;
extern void omap1_set_vpp(struct platform_device *pdev, int enable);

#endif
