/*
 * Static memory controller register definitions for PXA CPUs
 *
 * Copyright (C) 2010 Marek Vasut <marek.vasut@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SMEMC_REGS_H
#define __SMEMC_REGS_H

#define PXA2XX_SMEMC_BASE	0x48000000
#define PXA3XX_SMEMC_BASE	0x4a000000
#define SMEMC_VIRT		IOMEM(0xf6000000)

#define MDCNFG		(SMEMC_VIRT + 0x00)  /* SDRAM Configuration Register 0 */
#define MDREFR		(SMEMC_VIRT + 0x04)  /* SDRAM Refresh Control Register */
#define MSC0		(SMEMC_VIRT + 0x08)  /* Static Memory Control Register 0 */
#define MSC1		(SMEMC_VIRT + 0x0C)  /* Static Memory Control Register 1 */
#define MSC2		(SMEMC_VIRT + 0x10)  /* Static Memory Control Register 2 */
#define MECR		(SMEMC_VIRT + 0x14)  /* Expansion Memory (PCMCIA/Compact Flash) Bus Configuration */
#define SXLCR		(SMEMC_VIRT + 0x18)  /* LCR value to be written to SDRAM-Timing Synchronous Flash */
#define SXCNFG		(SMEMC_VIRT + 0x1C)  /* Synchronous Static Memory Control Register */
#define SXMRS		(SMEMC_VIRT + 0x24)  /* MRS value to be written to Synchronous Flash or SMROM */
#define MCMEM0		(SMEMC_VIRT + 0x28)  /* Card interface Common Memory Space Socket 0 Timing */
#define MCMEM1		(SMEMC_VIRT + 0x2C)  /* Card interface Common Memory Space Socket 1 Timing */
#define MCATT0		(SMEMC_VIRT + 0x30)  /* Card interface Attribute Space Socket 0 Timing Configuration */
#define MCATT1		(SMEMC_VIRT + 0x34)  /* Card interface Attribute Space Socket 1 Timing Configuration */
#define MCIO0		(SMEMC_VIRT + 0x38)  /* Card interface I/O Space Socket 0 Timing Configuration */
#define MCIO1		(SMEMC_VIRT + 0x3C)  /* Card interface I/O Space Socket 1 Timing Configuration */
#define MDMRS		(SMEMC_VIRT + 0x40)  /* MRS value to be written to SDRAM */
#define BOOT_DEF	(SMEMC_VIRT + 0x44)  /* Read-Only Boot-Time Register. Contains BOOT_SEL and PKG_SEL */
#define MEMCLKCFG	(SMEMC_VIRT + 0x68)  /* Clock Configuration */
#define CSADRCFG0	(SMEMC_VIRT + 0x80)  /* Address Configuration Register for CS0 */
#define CSADRCFG1	(SMEMC_VIRT + 0x84)  /* Address Configuration Register for CS1 */
#define CSADRCFG2	(SMEMC_VIRT + 0x88)  /* Address Configuration Register for CS2 */
#define CSADRCFG3	(SMEMC_VIRT + 0x8C)  /* Address Configuration Register for CS3 */
#define CSMSADRCFG	(SMEMC_VIRT + 0xA0)  /* Chip Select Configuration Register */

/*
 * More handy macros for PCMCIA
 *
 * Arg is socket number
 */
#define MCMEM(s)	(SMEMC_VIRT + 0x28 + ((s)<<2))  /* Card interface Common Memory Space Socket s Timing */
#define MCATT(s)	(SMEMC_VIRT + 0x30 + ((s)<<2))  /* Card interface Attribute Space Socket s Timing Configuration */
#define MCIO(s)		(SMEMC_VIRT + 0x38 + ((s)<<2))  /* Card interface I/O Space Socket s Timing Configuration */

/* MECR register defines */
#define MECR_NOS	(1 << 0)	/* Number Of Sockets: 0 -> 1 sock, 1 -> 2 sock */
#define MECR_CIT	(1 << 1)	/* Card Is There: 0 -> no card, 1 -> card inserted */

#define MDCNFG_DE0	(1 << 0)	/* SDRAM Bank 0 Enable */
#define MDCNFG_DE1	(1 << 1)	/* SDRAM Bank 1 Enable */
#define MDCNFG_DE2	(1 << 16)	/* SDRAM Bank 2 Enable */
#define MDCNFG_DE3	(1 << 17)	/* SDRAM Bank 3 Enable */

#define MDREFR_K0DB4	(1 << 29)	/* SDCLK0 Divide by 4 Control/Status */
#define MDREFR_K2FREE	(1 << 25)	/* SDRAM Free-Running Control */
#define MDREFR_K1FREE	(1 << 24)	/* SDRAM Free-Running Control */
#define MDREFR_K0FREE	(1 << 23)	/* SDRAM Free-Running Control */
#define MDREFR_SLFRSH	(1 << 22)	/* SDRAM Self-Refresh Control/Status */
#define MDREFR_APD	(1 << 20)	/* SDRAM/SSRAM Auto-Power-Down Enable */
#define MDREFR_K2DB2	(1 << 19)	/* SDCLK2 Divide by 2 Control/Status */
#define MDREFR_K2RUN	(1 << 18)	/* SDCLK2 Run Control/Status */
#define MDREFR_K1DB2	(1 << 17)	/* SDCLK1 Divide by 2 Control/Status */
#define MDREFR_K1RUN	(1 << 16)	/* SDCLK1 Run Control/Status */
#define MDREFR_E1PIN	(1 << 15)	/* SDCKE1 Level Control/Status */
#define MDREFR_K0DB2	(1 << 14)	/* SDCLK0 Divide by 2 Control/Status */
#define MDREFR_K0RUN	(1 << 13)	/* SDCLK0 Run Control/Status */
#define MDREFR_E0PIN	(1 << 12)	/* SDCKE0 Level Control/Status */

#endif
