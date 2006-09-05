/*
 * MPC85XX ADS common board definitions
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

#ifndef __MACH_MPC85XX_ADS_H__
#define __MACH_MPC85XX_ADS_H__

#include <linux/init.h>
#include <asm/ppcboot.h>

#define BOARD_CCSRBAR		((uint)0xe0000000)
#define BCSR_ADDR		((uint)0xf8000000)
#define BCSR_SIZE		((uint)(32 * 1024))

struct seq_file;

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

/* FCC1 Clock Source Configuration.  These can be
 * redefined in the board specific file.
 *    Can only choose from CLK9-12 */
#define F1_RXCLK       12
#define F1_TXCLK       11

/* FCC2 Clock Source Configuration.  These can be
 * redefined in the board specific file.
 *    Can only choose from CLK13-16 */
#define F2_RXCLK       13
#define F2_TXCLK       14

/* FCC3 Clock Source Configuration.  These can be
 * redefined in the board specific file.
 *    Can only choose from CLK13-16 */
#define F3_RXCLK       15
#define F3_TXCLK       16


#endif				/* __MACH_MPC85XX_ADS_H__ */
