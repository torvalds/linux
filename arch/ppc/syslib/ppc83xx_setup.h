/*
 * MPC83XX common board definitions
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2005 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __PPC_SYSLIB_PPC83XX_SETUP_H
#define __PPC_SYSLIB_PPC83XX_SETUP_H

#include <linux/init.h>

extern unsigned long mpc83xx_find_end_of_memory(void) __init;
extern long mpc83xx_time_init(void) __init;
extern void mpc83xx_calibrate_decr(void) __init;
extern void mpc83xx_early_serial_map(void) __init;
extern void mpc83xx_restart(char *cmd);
extern void mpc83xx_power_off(void);
extern void mpc83xx_halt(void);
extern void mpc83xx_setup_hose(void) __init;

/* PCI config */
#define PCI1_CFG_ADDR_OFFSET (0x8300)
#define PCI1_CFG_DATA_OFFSET (0x8304)

#define PCI2_CFG_ADDR_OFFSET (0x8380)
#define PCI2_CFG_DATA_OFFSET (0x8384)

/* Serial Config */
#ifdef CONFIG_SERIAL_MANY_PORTS
#define RS_TABLE_SIZE  64
#else
#define RS_TABLE_SIZE  2
#endif

#ifndef BASE_BAUD
#define BASE_BAUD 115200
#endif

#endif /* __PPC_SYSLIB_PPC83XX_SETUP_H */
