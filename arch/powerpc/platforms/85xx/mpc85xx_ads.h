/*
 * MPC85xx ADS board definitions
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2004 Freescale Semiconductor Inc.
 *
 * 2006 (c) MontaVista Software, Inc.
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef __MACH_MPC85XXADS_H
#define __MACH_MPC85XXADS_H

#include <linux/initrd.h>
#include <sysdev/fsl_soc.h>

#define BCSR_ADDR		((uint)0xf8000000)
#define BCSR_SIZE		((uint)(32 * 1024))

#ifdef CONFIG_CPM2

#define MPC85xx_CPM_OFFSET	(0x80000)

#define CPM_MAP_ADDR		(get_immrbase() + MPC85xx_CPM_OFFSET)
#define CPM_IRQ_OFFSET		60

#define SIU_INT_SMC1		((uint)0x04+CPM_IRQ_OFFSET)
#define SIU_INT_SMC2		((uint)0x05+CPM_IRQ_OFFSET)
#define SIU_INT_SCC1		((uint)0x28+CPM_IRQ_OFFSET)
#define SIU_INT_SCC2		((uint)0x29+CPM_IRQ_OFFSET)
#define SIU_INT_SCC3		((uint)0x2a+CPM_IRQ_OFFSET)
#define SIU_INT_SCC4		((uint)0x2b+CPM_IRQ_OFFSET)

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

#endif	/* CONFIG_CPM2 */
#endif	/* __MACH_MPC85XXADS_H */
