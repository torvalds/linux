/* SPDX-License-Identifier: GPL-2.0-or-later */
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
 */

#ifdef __KERNEL__
#ifndef __MACH_ADS8260_DEFS
#define __MACH_ADS8260_DEFS

#include <linux/seq_file.h>

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

#endif /* __MACH_ADS8260_DEFS */
#endif /* __KERNEL__ */
