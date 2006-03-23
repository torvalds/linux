/*
 * Definitions for the IBM CPC710 PCI Host Bridge
 *
 * Author: Matt Porter <mporter@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __PPC_PLATFORMS_CPC710_H
#define __PPC_PLATFORMS_CPC710_H

/* General bridge and memory controller registers */
#define PIDR	0xff000008
#define	CNFR	0xff00000c
#define	RSTR	0xff000010
#define UCTL	0xff001000
#define	MPSR	0xff001010
#define	SIOC	0xff001020
#define	ABCNTL	0xff001030
#define SRST	0xff001040
#define	ERRC	0xff001050
#define	SESR	0xff001060
#define	SEAR	0xff001070
#define	SIOC1	0xff001090
#define	PGCHP	0xff001100
#define	GPDIR	0xff001130
#define	GPOUT	0xff001150
#define	ATAS	0xff001160
#define	AVDG	0xff001170
#define	MCCR	0xff001200
#define	MESR	0xff001220
#define	MEAR	0xff001230
#define	MCER0	0xff001300
#define	MCER1	0xff001310
#define	MCER2	0xff001320
#define	MCER3	0xff001330
#define	MCER4	0xff001340
#define	MCER5	0xff001350
#define	MCER6	0xff001360
#define	MCER7	0xff001370

/*
 * PCI32/64 configuration registers
 * Given as offsets from their
 * respective physical segment BAR
 */
#define PIBAR	0x000f7800
#define PMBAR	0x000f7810
#define MSIZE	0x000f7f40
#define IOSIZE	0x000f7f60
#define SMBAR	0x000f7f80
#define SIBAR	0x000f7fc0
#define PSSIZE	0x000f8100
#define PPSIZE	0x000f8110
#define BARPS	0x000f8120
#define BARPP	0x000f8130
#define PSBAR	0x000f8140
#define PPBAR	0x000f8150
#define BPMDLK	0x000f8200      /* Bottom of Peripheral Memory Space */
#define TPMDLK	0x000f8210      /* Top of Peripheral Memory Space */
#define BIODLK	0x000f8220      /* Bottom of Peripheral I/O Space */
#define TIODLK	0x000f8230      /* Top of Perioheral I/O Space */
#define DLKCTRL	0x000f8240      /* Deadlock control */
#define DLKDEV	0x000f8250      /* Deadlock device */

/* System standard configuration registers space */
#define	DCR	0xff200000
#define	DID	0xff200004
#define	BAR	0xff200018

/* Device specific configuration space */
#define	PCIENB	0xff201000

/* Configuration space registers */
#define CPC710_BUS_NUMBER	0x40
#define CPC710_SUB_BUS_NUMBER	0x41

#endif /* __PPC_PLATFORMS_CPC710_H */
