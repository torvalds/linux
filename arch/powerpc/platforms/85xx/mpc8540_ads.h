/*
 * arch/ppc/platforms/85xx/mpc8540_ads.h
 *
 * MPC8540ADS board definitions
 *
 * Maintainer: Kumar Gala <kumar.gala@freescale.com>
 *
 * Copyright 2004 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef __MACH_MPC8540ADS_H__
#define __MACH_MPC8540ADS_H__

#include <linux/config.h>
#include <linux/initrd.h>

#define BOARD_CCSRBAR		((uint)0xe0000000)
#define BCSR_ADDR		((uint)0xf8000000)
#define BCSR_SIZE		((uint)(32 * 1024))

/* PCI interrupt controller */
#define PIRQA		MPC85xx_IRQ_EXT1
#define PIRQB		MPC85xx_IRQ_EXT2
#define PIRQC		MPC85xx_IRQ_EXT3
#define PIRQD		MPC85xx_IRQ_EXT4

/* Offset of CPM register space */
#define CPM_MAP_ADDR	(CCSRBAR + MPC85xx_CPM_OFFSET)

#endif				/* __MACH_MPC8540ADS_H__ */
