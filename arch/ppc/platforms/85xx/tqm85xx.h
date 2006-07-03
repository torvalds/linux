/*
 * TQM85xx (40/41/55/60) board definitions
 *
 * Copyright (c) 2005 DENX Software Engineering
 * Stefan Roese <sr@denx.de>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef __MACH_TQM85XX_H
#define __MACH_TQM85XX_H

#include <linux/init.h>
#include <asm/ppcboot.h>

#define BOARD_CCSRBAR		((uint)0xe0000000)
#define CCSRBAR_SIZE		((uint)1024*1024)

#define CPM_MAP_ADDR		(CCSRBAR + MPC85xx_CPM_OFFSET)

#define PCI_CFG_ADDR_OFFSET	(0x8000)
#define PCI_CFG_DATA_OFFSET	(0x8004)

/* PCI interrupt controller */
#define PIRQA			MPC85xx_IRQ_EXT2
#define PIRQB			MPC85xx_IRQ_EXT3

#define MPC85XX_PCI1_LOWER_IO	0x00000000
#define MPC85XX_PCI1_UPPER_IO	0x00ffffff

#define MPC85XX_PCI1_LOWER_MEM	0x80000000
#define MPC85XX_PCI1_UPPER_MEM	0x9fffffff

#define MPC85XX_PCI1_IO_BASE	0xe2000000
#define MPC85XX_PCI1_MEM_OFFSET	0x00000000

#define MPC85XX_PCI1_IO_SIZE	0x01000000

#define BASE_BAUD 115200

extern void mpc85xx_setup_hose(void) __init;
extern void mpc85xx_restart(char *cmd);
extern void mpc85xx_power_off(void);
extern void mpc85xx_halt(void);
extern void mpc85xx_init_IRQ(void) __init;
extern unsigned long mpc85xx_find_end_of_memory(void) __init;
extern void mpc85xx_calibrate_decr(void) __init;

#endif /* __MACH_TQM85XX_H */
