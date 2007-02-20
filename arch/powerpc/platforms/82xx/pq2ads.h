/*
 * PQ2/mpc8260 board-specific stuff
 *
 * A collection of structures, addresses, and values associated with
 * the Freescale MPC8260ADS/MPC8266ADS-PCI boards.
 * Copied from the RPX-Classic and SBS8260 stuff.
 *
 * Author: Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * Originally written by Dan Malek for Motorola MPC8260 family
 *
 * Copyright (c) 2001 Dan Malek <dan@embeddedalley.com>
 * Copyright (c) 2006 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifdef __KERNEL__
#ifndef __MACH_ADS8260_DEFS
#define __MACH_ADS8260_DEFS

#include <linux/seq_file.h>
#include <asm/ppcboot.h>

/* For our show_cpuinfo hooks. */
#define CPUINFO_VENDOR		"Freescale Semiconductor"
#define CPUINFO_MACHINE		"PQ2 ADS PowerPC"

/* Backword-compatibility stuff for the drivers */
#define CPM_MAP_ADDR		((uint)0xf0000000)
#define CPM_IRQ_OFFSET 0

/* The ADS8260 has 16, 32-bit wide control/status registers, accessed
 * only on word boundaries.
 * Not all are used (yet), or are interesting to us (yet).
 */

/* Things of interest in the CSR.
 */
#define BCSR0_LED0		((uint)0x02000000)      /* 0 == on */
#define BCSR0_LED1		((uint)0x01000000)      /* 0 == on */
#define BCSR1_FETHIEN		((uint)0x08000000)      /* 0 == enable*/
#define BCSR1_FETH_RST		((uint)0x04000000)      /* 0 == reset */
#define BCSR1_RS232_EN1		((uint)0x02000000)      /* 0 ==enable */
#define BCSR1_RS232_EN2		((uint)0x01000000)      /* 0 ==enable */
#define BCSR3_FETHIEN2		((uint)0x10000000)      /* 0 == enable*/
#define BCSR3_FETH2_RST		((uint)0x80000000)      /* 0 == reset */

/* cpm serial driver works with constants below */

#define SIU_INT_SMC1		((uint)0x04+CPM_IRQ_OFFSET)
#define SIU_INT_SMC2		((uint)0x05+CPM_IRQ_OFFSET)
#define SIU_INT_SCC1		((uint)0x28+CPM_IRQ_OFFSET)
#define SIU_INT_SCC2		((uint)0x29+CPM_IRQ_OFFSET)
#define SIU_INT_SCC3		((uint)0x2a+CPM_IRQ_OFFSET)
#define SIU_INT_SCC4		((uint)0x2b+CPM_IRQ_OFFSET)

void m82xx_pci_init_irq(void);
void mpc82xx_ads_show_cpuinfo(struct seq_file*);
void m82xx_calibrate_decr(void);

#endif /* __MACH_ADS8260_DEFS */
#endif /* __KERNEL__ */
