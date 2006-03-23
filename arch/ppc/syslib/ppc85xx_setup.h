/*
 * MPC85XX common board definitions
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

#ifndef __PPC_SYSLIB_PPC85XX_SETUP_H
#define __PPC_SYSLIB_PPC85XX_SETUP_H

#include <linux/config.h>
#include <linux/init.h>
#include <asm/ppcboot.h>

extern unsigned long mpc85xx_find_end_of_memory(void) __init;
extern void mpc85xx_calibrate_decr(void) __init;
extern void mpc85xx_early_serial_map(void) __init;
extern void mpc85xx_restart(char *cmd);
extern void mpc85xx_power_off(void);
extern void mpc85xx_halt(void);
extern void mpc85xx_setup_hose(void) __init;

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

/* Serial Config */
#ifdef CONFIG_SERIAL_MANY_PORTS
#define RS_TABLE_SIZE  64
#else
#define RS_TABLE_SIZE  2
#endif

#ifndef BASE_BAUD
#define BASE_BAUD 115200
#endif

/* Offset of CPM register space */
#define CPM_MAP_ADDR	(CCSRBAR + MPC85xx_CPM_OFFSET)

#endif /* __PPC_SYSLIB_PPC85XX_SETUP_H */
