/*
 * arch/ppc/platforms/hdpu.h
 *
 * Definitions for Sky Computers HDPU board.
 *
 * Brian Waite <waite@skycomputers.com>
 *
 * Based on code done by Rabeeh Khoury - rabeeh@galileo.co.il
 * Based on code done by Mark A. Greer <mgreer@mvista.com>
 * Based on code done by  Tim Montgomery <timm@artesyncp.com>
 *
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

/*
 * The MV64360 has 2 PCI buses each with 1 window from the CPU bus to
 * PCI I/O space and 4 windows from the CPU bus to PCI MEM space.
 * We'll only use one PCI MEM window on each PCI bus.
 *
 * This is the CPU physical memory map (windows must be at least 64K and start
 * on a boundary that is a multiple of the window size):
 *
 *    0x80000000-0x8fffffff	 - PCI 0 MEM
 *    0xa0000000-0xafffffff	 - PCI 1 MEM
 *    0xc0000000-0xc0ffffff	 - PCI 0 I/O
 *    0xc1000000-0xc1ffffff	 - PCI 1 I/O

 *    0xf1000000-0xf100ffff      - MV64360 Registers
 *    0xf1010000-0xfb9fffff      - HOLE
 *    0xfbfa0000-0xfbfaffff      - TBEN
 *    0xfbf00000-0xfbfbffff      - NEXUS
 *    0xfbfc0000-0xfbffffff      - Internal SRAM
 *    0xfc000000-0xffffffff      - Boot window
 */

#ifndef __PPC_PLATFORMS_HDPU_H
#define __PPC_PLATFORMS_HDPU_H

/* CPU Physical Memory Map setup. */
#define	HDPU_BRIDGE_REG_BASE		     0xf1000000

#define HDPU_TBEN_BASE                        0xfbfa0000
#define HDPU_TBEN_SIZE                        0x00010000
#define HDPU_NEXUS_ID_BASE                    0xfbfb0000
#define HDPU_NEXUS_ID_SIZE                    0x00010000
#define HDPU_INTERNAL_SRAM_BASE               0xfbfc0000
#define HDPU_INTERNAL_SRAM_SIZE               0x00040000
#define	HDPU_EMB_FLASH_BASE		      0xfc000000
#define	HDPU_EMB_FLASH_SIZE      	      0x04000000

/* PCI Mappings */

#define HDPU_PCI0_MEM_START_PROC_ADDR         0x80000000
#define HDPU_PCI0_MEM_START_PCI_HI_ADDR       0x00000000
#define HDPU_PCI0_MEM_START_PCI_LO_ADDR       HDPU_PCI0_MEM_START_PROC_ADDR
#define HDPU_PCI0_MEM_SIZE                    0x10000000

#define HDPU_PCI1_MEM_START_PROC_ADDR         0xc0000000
#define HDPU_PCI1_MEM_START_PCI_HI_ADDR       0x00000000
#define HDPU_PCI1_MEM_START_PCI_LO_ADDR       HDPU_PCI1_MEM_START_PROC_ADDR
#define HDPU_PCI1_MEM_SIZE                    0x20000000

#define HDPU_PCI0_IO_START_PROC_ADDR          0xc0000000
#define HDPU_PCI0_IO_START_PCI_ADDR           0x00000000
#define HDPU_PCI0_IO_SIZE                     0x01000000

#define HDPU_PCI1_IO_START_PROC_ADDR          0xc1000000
#define HDPU_PCI1_IO_START_PCI_ADDR           0x01000000
#define HDPU_PCI1_IO_SIZE                     0x01000000

#define HDPU_DEFAULT_BAUD 115200
#define HDPU_MPSC_CLK_SRC 8	/* TCLK */
#define HDPU_MPSC_CLK_FREQ 133000000	/* 133 Mhz */

#define	HDPU_PCI_0_IRQ		(8+64)
#define	HDPU_PCI_1_IRQ		(13+64)

#endif				/* __PPC_PLATFORMS_HDPU_H */
