/*
 * Copyright (c) 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 - System define for arch_reset() function
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __PLAT_SAMSUNG_WATCHDOG_RESET_H
#define __PLAT_SAMSUNG_WATCHDOG_RESET_H

extern void samsung_wdt_reset(void);
extern void samsung_wdt_reset_of_init(void);
extern void samsung_wdt_reset_init(void __iomem *base);

#endif /* __PLAT_SAMSUNG_WATCHDOG_RESET_H */
