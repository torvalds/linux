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

#define MPC85XX_PCI1_LOWER_IO	0x00000000
#define MPC85XX_PCI1_UPPER_IO	0x00ffffff

#define MPC85XX_PCI1_LOWER_MEM	0x80000000
#define MPC85XX_PCI1_UPPER_MEM	0x9fffffff

#define MPC85XX_PCI1_IO_BASE	0xe2000000
#define MPC85XX_PCI1_MEM_OFFSET	0x00000000

#define MPC85XX_PCI1_IO_SIZE	0x01000000

/* PCI config */
#define PCI1_CFG_ADDR_OFFSET	(0x8000)
#define PCI1_CFG_DATA_OFFSET	(0x8004)

#define PCI2_CFG_ADDR_OFFSET	(0x9000)
#define PCI2_CFG_DATA_OFFSET	(0x9004)

/* Additional register for PCI-X configuration */
#define PCIX_NEXT_CAP	0x60
#define PCIX_CAP_ID	0x61
#define PCIX_COMMAND	0x62
#define PCIX_STATUS	0x64

/* Offset of CPM register space */
#define CPM_MAP_ADDR	(CCSRBAR + MPC85xx_CPM_OFFSET)

#endif /* __MACH_MPC8540ADS_H__ */
