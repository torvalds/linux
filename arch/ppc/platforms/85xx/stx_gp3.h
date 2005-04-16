/*
 * arch/ppc/platforms/stx8560_gp3.h
 *
 * STx GP3 board definitions
 *
 * Dan Malek (dan@embeddededge.com)
 * Copyright 2004 Embedded Edge, LLC
 *
 * Ported to 2.6, Matt Porter <mporter@kernel.crashing.org>
 * Copyright 2004-2005 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef __MACH_STX_GP3_H
#define __MACH_STX_GP3_H

#include <linux/config.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <asm/ppcboot.h>

#define BOARD_CCSRBAR		((uint)0xe0000000)
#define CCSRBAR_SIZE		((uint)1024*1024)

#define CPM_MAP_ADDR		(CCSRBAR + MPC85xx_CPM_OFFSET)

#define BCSR_ADDR		((uint)0xfc000000)
#define BCSR_SIZE		((uint)(16 * 1024))

#define BCSR_TSEC1_RESET	0x00000080
#define BCSR_TSEC2_RESET	0x00000040
#define BCSR_LED1		0x00000008
#define BCSR_LED2		0x00000004
#define BCSR_LED3		0x00000002
#define BCSR_LED4		0x00000001

extern void mpc85xx_setup_hose(void) __init;
extern void mpc85xx_restart(char *cmd);
extern void mpc85xx_power_off(void);
extern void mpc85xx_halt(void);
extern int mpc85xx_show_cpuinfo(struct seq_file *m);
extern void mpc85xx_init_IRQ(void) __init;
extern unsigned long mpc85xx_find_end_of_memory(void) __init;
extern void mpc85xx_calibrate_decr(void) __init;

#define PCI_CFG_ADDR_OFFSET	(0x8000)
#define PCI_CFG_DATA_OFFSET	(0x8004)

/* PCI interrupt controller */
#define PIRQA		MPC85xx_IRQ_EXT1
#define PIRQB		MPC85xx_IRQ_EXT2
#define PIRQC		MPC85xx_IRQ_EXT3
#define PIRQD		MPC85xx_IRQ_EXT4
#define PCI_MIN_IDSEL	16
#define PCI_MAX_IDSEL	19
#define PCI_IRQ_SLOT	4

#define MPC85XX_PCI1_LOWER_IO	0x00000000
#define MPC85XX_PCI1_UPPER_IO	0x00ffffff

#define MPC85XX_PCI1_LOWER_MEM	0x80000000
#define MPC85XX_PCI1_UPPER_MEM	0x9fffffff

#define MPC85XX_PCI1_IO_BASE	0xe2000000
#define MPC85XX_PCI1_MEM_OFFSET	0x00000000

#define MPC85XX_PCI1_IO_SIZE	0x01000000

#endif /* __MACH_STX_GP3_H */
