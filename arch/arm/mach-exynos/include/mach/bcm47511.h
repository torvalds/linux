/* linux/arm/arch/mach-exynos/include/mach/bcm47511.h
 *
 * Platform data  Header for BCM47511(GPS) driver.
 *
 * Copyright (c) 2011 Samsung Electronics
 * Minho Ban <mhban@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef _BCM47511_H
#define _BCM47511_H

struct bcm47511_platform_data {
	unsigned int regpu;		/* Power */
	unsigned int nrst;		/* Reset */
	unsigned int uart_rxd;		/* Start gpio number of uart */
	/* Below are machine dependant */
	unsigned int gps_cntl;		/* Request 26MHz CP clock */
	const char *reg32khz;		/* regulator id for 32KHz clk */
};

#endif /* _BCM47511_H */


