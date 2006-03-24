/*
 * PCI Include file the Freescale MPC52xx embedded cpu chips
 *
 *
 * Maintainer : Sylvain Munaut <tnt@246tNt.com>
 *
 * Inspired from code written by Dale Farnsworth <dfarnsworth@mvista.com>
 * for the 2.4 kernel.
 *
 * Copyright (C) 2004 Sylvain Munaut <tnt@246tNt.com>
 * Copyright (C) 2003 MontaVista, Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __SYSLIB_MPC52xx_PCI_H__
#define __SYSLIB_MPC52xx_PCI_H__

/* ======================================================================== */
/* PCI windows config                                                       */
/* ======================================================================== */

/*
 * Master windows : MPC52xx -> PCI
 *
 *  0x80000000 -> 0x9FFFFFFF       PCI Mem prefetchable          IW0BTAR
 *  0xA0000000 -> 0xAFFFFFFF       PCI Mem                       IW1BTAR
 *  0xB0000000 -> 0xB0FFFFFF       PCI IO                        IW2BTAR
 *
 * Slave windows  : PCI -> MPC52xx
 *
 *  0xF0000000 -> 0xF003FFFF       MPC52xx MBAR                  TBATR0
 *  0x00000000 -> 0x3FFFFFFF       MPC52xx local memory          TBATR1
 */

#define MPC52xx_PCI_MEM_OFFSET 	0x00000000	/* Offset for MEM MMIO */

#define MPC52xx_PCI_MEM_START	0x80000000
#define MPC52xx_PCI_MEM_SIZE	0x20000000
#define MPC52xx_PCI_MEM_STOP	(MPC52xx_PCI_MEM_START+MPC52xx_PCI_MEM_SIZE-1)

#define MPC52xx_PCI_MMIO_START	0xa0000000
#define MPC52xx_PCI_MMIO_SIZE	0x10000000
#define MPC52xx_PCI_MMIO_STOP	(MPC52xx_PCI_MMIO_START+MPC52xx_PCI_MMIO_SIZE-1)

#define MPC52xx_PCI_IO_BASE	0xb0000000

#define MPC52xx_PCI_IO_START	0x00000000
#define MPC52xx_PCI_IO_SIZE	0x01000000
#define MPC52xx_PCI_IO_STOP	(MPC52xx_PCI_IO_START+MPC52xx_PCI_IO_SIZE-1)


#define MPC52xx_PCI_TARGET_IO	MPC52xx_MBAR
#define MPC52xx_PCI_TARGET_MEM	0x00000000


/* ======================================================================== */
/* Structures mapping & Defines for PCI Unit                                */
/* ======================================================================== */

#define MPC52xx_PCI_GSCR_BM		0x40000000
#define MPC52xx_PCI_GSCR_PE		0x20000000
#define MPC52xx_PCI_GSCR_SE		0x10000000
#define MPC52xx_PCI_GSCR_XLB2PCI_MASK	0x07000000
#define MPC52xx_PCI_GSCR_XLB2PCI_SHIFT	24
#define MPC52xx_PCI_GSCR_IPG2PCI_MASK	0x00070000
#define MPC52xx_PCI_GSCR_IPG2PCI_SHIFT	16
#define MPC52xx_PCI_GSCR_BME		0x00004000
#define MPC52xx_PCI_GSCR_PEE		0x00002000
#define MPC52xx_PCI_GSCR_SEE		0x00001000
#define MPC52xx_PCI_GSCR_PR		0x00000001


#define MPC52xx_PCI_IWBTAR_TRANSLATION(proc_ad,pci_ad,size)	  \
		( ( (proc_ad) & 0xff000000 )			| \
		  ( (((size) - 1) >> 8) & 0x00ff0000 )		| \
		  ( ((pci_ad) >> 16) & 0x0000ff00 ) )

#define MPC52xx_PCI_IWCR_PACK(win0,win1,win2)	(((win0) << 24) | \
						 ((win1) << 16) | \
						 ((win2) <<  8))

#define MPC52xx_PCI_IWCR_DISABLE	0x0
#define MPC52xx_PCI_IWCR_ENABLE		0x1
#define MPC52xx_PCI_IWCR_READ		0x0
#define MPC52xx_PCI_IWCR_READ_LINE	0x2
#define MPC52xx_PCI_IWCR_READ_MULTI	0x4
#define MPC52xx_PCI_IWCR_MEM		0x0
#define MPC52xx_PCI_IWCR_IO		0x8

#define MPC52xx_PCI_TCR_P		0x01000000
#define MPC52xx_PCI_TCR_LD		0x00010000

#define MPC52xx_PCI_TBATR_DISABLE	0x0
#define MPC52xx_PCI_TBATR_ENABLE	0x1


#ifndef __ASSEMBLY__

struct mpc52xx_pci {
	u32	idr;		/* PCI + 0x00 */
	u32	scr;		/* PCI + 0x04 */
	u32	ccrir;		/* PCI + 0x08 */
	u32	cr1;		/* PCI + 0x0C */
	u32	bar0;		/* PCI + 0x10 */
	u32	bar1;		/* PCI + 0x14 */
	u8	reserved1[16];	/* PCI + 0x18 */
	u32	ccpr;		/* PCI + 0x28 */
	u32	sid;		/* PCI + 0x2C */
	u32	erbar;		/* PCI + 0x30 */
	u32	cpr;		/* PCI + 0x34 */
	u8	reserved2[4];	/* PCI + 0x38 */
	u32	cr2;		/* PCI + 0x3C */
	u8	reserved3[32];	/* PCI + 0x40 */
	u32	gscr;		/* PCI + 0x60 */
	u32	tbatr0;		/* PCI + 0x64 */
	u32	tbatr1;		/* PCI + 0x68 */
	u32	tcr;		/* PCI + 0x6C */
	u32	iw0btar;	/* PCI + 0x70 */
	u32	iw1btar;	/* PCI + 0x74 */
	u32	iw2btar;	/* PCI + 0x78 */
	u8	reserved4[4];	/* PCI + 0x7C */
	u32	iwcr;		/* PCI + 0x80 */
	u32	icr;		/* PCI + 0x84 */
	u32	isr;		/* PCI + 0x88 */
	u32	arb;		/* PCI + 0x8C */
	u8	reserved5[104];	/* PCI + 0x90 */
	u32	car;		/* PCI + 0xF8 */
	u8	reserved6[4];	/* PCI + 0xFC */
};

#endif  /* __ASSEMBLY__ */


#endif  /* __SYSLIB_MPC52xx_PCI_H__ */
