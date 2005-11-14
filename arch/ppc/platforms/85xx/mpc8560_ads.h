/*
 * arch/ppc/platforms/mpc8560_ads.h
 *
 * MPC8540ADS board definitions
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2004 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef __MACH_MPC8560ADS_H
#define __MACH_MPC8560ADS_H

#include <linux/config.h>
#include <syslib/ppc85xx_setup.h>
#include <platforms/85xx/mpc85xx_ads_common.h>

#define CPM_MAP_ADDR	(CCSRBAR + MPC85xx_CPM_OFFSET)
#define PHY_INTERRUPT	MPC85xx_IRQ_EXT7

#endif				/* __MACH_MPC8560ADS_H */
