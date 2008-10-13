/*
 * arch/arm/plat-omap/include/mach/board-palmz71.h
 *
 * Hardware definitions for the Palm Zire71 device.
 *
 * Maintainters :	Marek Vasut <marek.vasut@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __OMAP_BOARD_PALMZ71_H
#define __OMAP_BOARD_PALMZ71_H

#define PALMZ71_USBDETECT_GPIO	0
#define PALMZ71_PENIRQ_GPIO	6
#define PALMZ71_MMC_WP_GPIO	8
#define PALMZ71_HDQ_GPIO 	11

#define PALMZ71_HOTSYNC_GPIO	OMAP_MPUIO(1)
#define PALMZ71_CABLE_GPIO	OMAP_MPUIO(2)
#define PALMZ71_SLIDER_GPIO	OMAP_MPUIO(3)
#define PALMZ71_MMC_IN_GPIO	OMAP_MPUIO(4)

#endif	/* __OMAP_BOARD_PALMZ71_H */
