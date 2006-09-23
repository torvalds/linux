/*
 * WindRiver PowerQUICC III SBC85xx common board definitions
 *
 * Copyright 2003 Motorola Inc.
 * Copyright 2004 Red Hat, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef __PLATFORMS_85XX_SBC85XX_H__
#define __PLATFORMS_85XX_SBC85XX_H__

#include <linux/init.h>
#include <linux/seq_file.h>
#include <asm/ppcboot.h>

#define BOARD_CCSRBAR		((uint)0xff700000)
#define CCSRBAR_SIZE		((uint)1024*1024)

#define BCSR_ADDR		((uint)0xfc000000)
#define BCSR_SIZE		((uint)(16 * 1024 * 1024))

#define UARTA_ADDR		(BCSR_ADDR + 0x00700000)
#define UARTB_ADDR		(BCSR_ADDR + 0x00800000)
#define RTC_DEVICE_ADDR		(BCSR_ADDR + 0x00900000)
#define EEPROM_ADDR		(BCSR_ADDR + 0x00b00000)

extern int  sbc8560_show_cpuinfo(struct seq_file *m);
extern void sbc8560_init_IRQ(void) __init; 

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

#endif /* __PLATFORMS_85XX_SBC85XX_H__ */
