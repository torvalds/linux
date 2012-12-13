/*
 * arch/arm/plat-omap/include/mach/fpga.h
 *
 * Interrupt handler for OMAP-1510 FPGA
 *
 * Copyright (C) 2001 RidgeRun, Inc.
 * Author: Greg Lonnon <glonnon@ridgerun.com>
 *
 * Copyright (C) 2002 MontaVista Software, Inc.
 *
 * Separated FPGA interrupts from innovator1510.c and cleaned up for 2.6
 * Copyright (C) 2004 Nokia Corporation by Tony Lindrgen <tony@atomide.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_OMAP_FPGA_H
#define __ASM_ARCH_OMAP_FPGA_H

/*
 * ---------------------------------------------------------------------------
 *  H2/P2 Debug board FPGA
 * ---------------------------------------------------------------------------
 */
/* maps in the FPGA registers and the ETHR registers */
#define H2P2_DBG_FPGA_BASE		0xE8000000		/* VA */
#define H2P2_DBG_FPGA_SIZE		SZ_4K			/* SIZE */
#define H2P2_DBG_FPGA_START		0x04000000		/* PA */

#define H2P2_DBG_FPGA_ETHR_START	(H2P2_DBG_FPGA_START + 0x300)
#define H2P2_DBG_FPGA_FPGA_REV		IOMEM(H2P2_DBG_FPGA_BASE + 0x10)	/* FPGA Revision */
#define H2P2_DBG_FPGA_BOARD_REV		IOMEM(H2P2_DBG_FPGA_BASE + 0x12)	/* Board Revision */
#define H2P2_DBG_FPGA_GPIO		IOMEM(H2P2_DBG_FPGA_BASE + 0x14)	/* GPIO outputs */
#define H2P2_DBG_FPGA_LEDS		IOMEM(H2P2_DBG_FPGA_BASE + 0x16)	/* LEDs outputs */
#define H2P2_DBG_FPGA_MISC_INPUTS	IOMEM(H2P2_DBG_FPGA_BASE + 0x18)	/* Misc inputs */
#define H2P2_DBG_FPGA_LAN_STATUS	IOMEM(H2P2_DBG_FPGA_BASE + 0x1A)	/* LAN Status line */
#define H2P2_DBG_FPGA_LAN_RESET		IOMEM(H2P2_DBG_FPGA_BASE + 0x1C)	/* LAN Reset line */

/* NOTE:  most boards don't have a static mapping for the FPGA ... */
struct h2p2_dbg_fpga {
	/* offset 0x00 */
	u16		smc91x[8];
	/* offset 0x10 */
	u16		fpga_rev;
	u16		board_rev;
	u16		gpio_outputs;
	u16		leds;
	/* offset 0x18 */
	u16		misc_inputs;
	u16		lan_status;
	u16		lan_reset;
	u16		reserved0;
	/* offset 0x20 */
	u16		ps2_data;
	u16		ps2_ctrl;
	/* plus also 4 rs232 ports ... */
};

/* LEDs definition on debug board (16 LEDs, all physically green) */
#define H2P2_DBG_FPGA_LED_GREEN		(1 << 15)
#define H2P2_DBG_FPGA_LED_AMBER		(1 << 14)
#define H2P2_DBG_FPGA_LED_RED		(1 << 13)
#define H2P2_DBG_FPGA_LED_BLUE		(1 << 12)
/*  cpu0 load-meter LEDs */
#define H2P2_DBG_FPGA_LOAD_METER	(1 << 0)	// A bit of fun on our board ...
#define H2P2_DBG_FPGA_LOAD_METER_SIZE	11
#define H2P2_DBG_FPGA_LOAD_METER_MASK	((1 << H2P2_DBG_FPGA_LOAD_METER_SIZE) - 1)

#define H2P2_DBG_FPGA_P2_LED_TIMER		(1 << 0)
#define H2P2_DBG_FPGA_P2_LED_IDLE		(1 << 1)

#endif
