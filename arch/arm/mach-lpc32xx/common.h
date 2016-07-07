/*
 * arch/arm/mach-lpc32xx/common.h
 *
 * Author: Kevin Wells <kevin.wells@nxp.com>
 *
 * Copyright (C) 2009-2010 NXP Semiconductors
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

#ifndef __LPC32XX_COMMON_H
#define __LPC32XX_COMMON_H

#include <linux/init.h>

/*
 * Other arch specific structures and functions
 */
extern void __init lpc32xx_init_irq(void);
extern void __init lpc32xx_map_io(void);
extern void __init lpc32xx_serial_init(void);

/*
 * Returns the LPC32xx unique 128-bit chip ID
 */
extern void lpc32xx_get_uid(u32 devid[4]);

extern u32 lpc32xx_return_iram_size(void);
/*
 * Pointers used for sizing and copying suspend function data
 */
extern int lpc32xx_sys_suspend(void);
extern int lpc32xx_sys_suspend_sz;

#endif
