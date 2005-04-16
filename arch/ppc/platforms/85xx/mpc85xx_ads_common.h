/*
 * arch/ppc/platforms/85xx/mpc85xx_ads_common.h
 *
 * MPC85XX ADS common board definitions
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

#ifndef __MACH_MPC85XX_ADS_H__
#define __MACH_MPC85XX_ADS_H__

#include <linux/config.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <asm/ppcboot.h>

#define BOARD_CCSRBAR		((uint)0xe0000000)
#define BCSR_ADDR		((uint)0xf8000000)
#define BCSR_SIZE		((uint)(32 * 1024))

extern int mpc85xx_ads_show_cpuinfo(struct seq_file *m);
extern void mpc85xx_ads_init_IRQ(void) __init;
extern void mpc85xx_ads_map_io(void) __init;

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

#endif				/* __MACH_MPC85XX_ADS_H__ */
