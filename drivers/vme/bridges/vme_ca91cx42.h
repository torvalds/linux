/*
 * ca91c042.h
 *
 * Support for the Tundra Universe 1 and Universe II VME bridge chips
 *
 * Author: Tom Armistead
 * Updated by Ajit Prem
 * Copyright 2004 Motorola Inc.
 *
 * Further updated by Martyn Welch <martyn.welch@ge.com>
 * Copyright 2009 GE Intelligent Platforms Embedded Systems, Inc.
 *
 * Derived from ca91c042.h by Michael Wyrick
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _CA91CX42_H
#define _CA91CX42_H

#ifndef	PCI_VENDOR_ID_TUNDRA
#define	PCI_VENDOR_ID_TUNDRA 0x10e3
#endif

#ifndef	PCI_DEVICE_ID_TUNDRA_CA91C142
#define	PCI_DEVICE_ID_TUNDRA_CA91C142 0x0000
#endif

/*
 *  Define the number of each that the CA91C142 supports.
 */
#define CA91C142_MAX_MASTER		8	/* Max Master Windows */
#define CA91C142_MAX_SLAVE		8	/* Max Slave Windows */
#define CA91C142_MAX_DMA		1	/* Max DMA Controllers */
#define CA91C142_MAX_MAILBOX		4	/* Max Mail Box registers */

/* Structure used to hold driver specific information */
struct ca91cx42_driver {
	void __iomem *base;	/* Base Address of device registers */
	wait_queue_head_t dma_queue;
	wait_queue_head_t iack_queue;
	wait_queue_head_t mbox_queue;
	void (*lm_callback[4])(int);	/* Called in interrupt handler */
	void *crcsr_kernel;
	dma_addr_t crcsr_bus;
	struct mutex vme_rmw;		/* Only one RMW cycle at a time */
	struct mutex vme_int;		/*
					 * Only one VME interrupt can be
					 * generated at a time, provide locking
					 */
};

/* See Page 2-77 in the Universe User Manual */
struct ca91cx42_dma_descriptor {
	unsigned int dctl;      /* DMA Control */
	unsigned int dtbc;      /* Transfer Byte Count */
	unsigned int dla;       /* PCI Address */
	unsigned int res1;      /* Reserved */
	unsigned int dva;       /* Vme Address */
	unsigned int res2;      /* Reserved */
	unsigned int dcpp;      /* Pointer to Numed Cmd Packet with rPN */
	unsigned int res3;      /* Reserved */
};

struct ca91cx42_dma_entry {
	struct ca91cx42_dma_descriptor descriptor;
	struct list_head list;
};

/* Universe Register Offsets */
/* general PCI configuration registers */
#define CA91CX42_PCI_ID		0x000
#define CA91CX42_PCI_CSR	0x004
#define CA91CX42_PCI_CLASS	0x008
#define CA91CX42_PCI_MISC0	0x00C
#define CA91CX42_PCI_BS		0x010
#define CA91CX42_PCI_MISC1	0x03C

#define LSI0_CTL		0x0100
#define LSI0_BS			0x0104
#define LSI0_BD			0x0108
#define LSI0_TO			0x010C

#define LSI1_CTL		0x0114
#define LSI1_BS			0x0118
#define LSI1_BD			0x011C
#define LSI1_TO			0x0120

#define LSI2_CTL		0x0128
#define LSI2_BS			0x012C
#define LSI2_BD			0x0130
#define LSI2_TO			0x0134

#define LSI3_CTL		0x013C
#define LSI3_BS			0x0140
#define LSI3_BD			0x0144
#define LSI3_TO			0x0148

#define LSI4_CTL		0x01A0
#define LSI4_BS			0x01A4
#define LSI4_BD			0x01A8
#define LSI4_TO			0x01AC

#define LSI5_CTL		0x01B4
#define LSI5_BS			0x01B8
#define LSI5_BD			0x01BC
#define LSI5_TO			0x01C0

#define LSI6_CTL		0x01C8
#define LSI6_BS			0x01CC
#define LSI6_BD			0x01D0
#define LSI6_TO			0x01D4

#define LSI7_CTL		0x01DC
#define LSI7_BS			0x01E0
#define LSI7_BD			0x01E4
#define LSI7_TO			0x01E8

static const int CA91CX42_LSI_CTL[] = { LSI0_CTL, LSI1_CTL, LSI2_CTL, LSI3_CTL,
				LSI4_CTL, LSI5_CTL, LSI6_CTL, LSI7_CTL };

static const int CA91CX42_LSI_BS[] = { LSI0_BS, LSI1_BS, LSI2_BS, LSI3_BS,
				LSI4_BS, LSI5_BS, LSI6_BS, LSI7_BS };

static const int CA91CX42_LSI_BD[] = { LSI0_BD, LSI1_BD, LSI2_BD, LSI3_BD,
				LSI4_BD, LSI5_BD, LSI6_BD, LSI7_BD };

static const int CA91CX42_LSI_TO[] = { LSI0_TO, LSI1_TO, LSI2_TO, LSI3_TO,
				LSI4_TO, LSI5_TO, LSI6_TO, LSI7_TO };

#define SCYC_CTL		0x0170
#define SCYC_ADDR		0x0174
#define SCYC_EN			0x0178
#define SCYC_CMP		0x017C
#define SCYC_SWP		0x0180
#define LMISC			0x0184
#define SLSI		        0x0188
#define L_CMDERR		0x018C
#define LAERR		        0x0190

#define DCTL		        0x0200
#define DTBC		        0x0204
#define DLA			0x0208
#define DVA			0x0210
#define DCPP		        0x0218
#define DGCS		        0x0220
#define D_LLUE			0x0224

#define LINT_EN			0x0300
#define LINT_STAT		0x0304
#define LINT_MAP0		0x0308
#define LINT_MAP1		0x030C
#define VINT_EN			0x0310
#define VINT_STAT		0x0314
#define VINT_MAP0		0x0318
#define VINT_MAP1		0x031C
#define STATID			0x0320

#define V1_STATID		0x0324
#define V2_STATID		0x0328
#define V3_STATID		0x032C
#define V4_STATID		0x0330
#define V5_STATID		0x0334
#define V6_STATID		0x0338
#define V7_STATID		0x033C

static const int CA91CX42_V_STATID[8] = { 0, V1_STATID, V2_STATID, V3_STATID,
					V4_STATID, V5_STATID, V6_STATID,
					V7_STATID };

#define LINT_MAP2		0x0340
#define VINT_MAP2		0x0344

#define MBOX0			0x0348
#define MBOX1			0x034C
#define MBOX2			0x0350
#define MBOX3			0x0354
#define SEMA0			0x0358
#define SEMA1			0x035C

#define MAST_CTL		0x0400
#define MISC_CTL		0x0404
#define MISC_STAT		0x0408
#define USER_AM			0x040C

#define VSI0_CTL		0x0F00
#define VSI0_BS			0x0F04
#define VSI0_BD			0x0F08
#define VSI0_TO			0x0F0C

#define VSI1_CTL		0x0F14
#define VSI1_BS			0x0F18
#define VSI1_BD			0x0F1C
#define VSI1_TO			0x0F20

#define VSI2_CTL		0x0F28
#define VSI2_BS			0x0F2C
#define VSI2_BD			0x0F30
#define VSI2_TO			0x0F34

#define VSI3_CTL		0x0F3C
#define VSI3_BS			0x0F40
#define VSI3_BD			0x0F44
#define VSI3_TO			0x0F48

#define LM_CTL			0x0F64
#define LM_BS			0x0F68

#define VRAI_CTL		0x0F70

#define VRAI_BS			0x0F74
#define VCSR_CTL		0x0F80
#define VCSR_TO			0x0F84
#define V_AMERR			0x0F88
#define VAERR			0x0F8C

#define VSI4_CTL		0x0F90
#define VSI4_BS			0x0F94
#define VSI4_BD			0x0F98
#define VSI4_TO			0x0F9C

#define VSI5_CTL		0x0FA4
#define VSI5_BS			0x0FA8
#define VSI5_BD			0x0FAC
#define VSI5_TO			0x0FB0

#define VSI6_CTL		0x0FB8
#define VSI6_BS			0x0FBC
#define VSI6_BD			0x0FC0
#define VSI6_TO			0x0FC4

#define VSI7_CTL		0x0FCC
#define VSI7_BS			0x0FD0
#define VSI7_BD			0x0FD4
#define VSI7_TO			0x0FD8

static const int CA91CX42_VSI_CTL[] = { VSI0_CTL, VSI1_CTL, VSI2_CTL, VSI3_CTL,
				VSI4_CTL, VSI5_CTL, VSI6_CTL, VSI7_CTL };

static const int CA91CX42_VSI_BS[] = { VSI0_BS, VSI1_BS, VSI2_BS, VSI3_BS,
				VSI4_BS, VSI5_BS, VSI6_BS, VSI7_BS };

static const int CA91CX42_VSI_BD[] = { VSI0_BD, VSI1_BD, VSI2_BD, VSI3_BD,
				VSI4_BD, VSI5_BD, VSI6_BD, VSI7_BD };

static const int CA91CX42_VSI_TO[] = { VSI0_TO, VSI1_TO, VSI2_TO, VSI3_TO,
				VSI4_TO, VSI5_TO, VSI6_TO, VSI7_TO };

#define VCSR_CLR		0x0FF4
#define VCSR_SET		0x0FF8
#define VCSR_BS			0x0FFC

/*
 * PCI Class Register
 * offset 008
 */
#define CA91CX42_BM_PCI_CLASS_BASE          0xFF000000
#define CA91CX42_OF_PCI_CLASS_BASE          24
#define CA91CX42_BM_PCI_CLASS_SUB           0x00FF0000
#define CA91CX42_OF_PCI_CLASS_SUB           16
#define CA91CX42_BM_PCI_CLASS_PROG          0x0000FF00
#define CA91CX42_OF_PCI_CLASS_PROG          8
#define CA91CX42_BM_PCI_CLASS_RID           0x000000FF
#define CA91CX42_OF_PCI_CLASS_RID           0

#define CA91CX42_OF_PCI_CLASS_RID_UNIVERSE_I 0
#define CA91CX42_OF_PCI_CLASS_RID_UNIVERSE_II 1

/*
 * PCI Misc Register
 * offset 00C
 */
#define CA91CX42_BM_PCI_MISC0_BISTC         0x80000000
#define CA91CX42_BM_PCI_MISC0_SBIST         0x60000000
#define CA91CX42_BM_PCI_MISC0_CCODE         0x0F000000
#define CA91CX42_BM_PCI_MISC0_MFUNCT        0x00800000
#define CA91CX42_BM_PCI_MISC0_LAYOUT        0x007F0000
#define CA91CX42_BM_PCI_MISC0_LTIMER        0x0000FF00
#define CA91CX42_OF_PCI_MISC0_LTIMER        8


/*
 * LSI Control Register
 * offset  100
 */
#define CA91CX42_LSI_CTL_EN		(1<<31)
#define CA91CX42_LSI_CTL_PWEN		(1<<30)

#define CA91CX42_LSI_CTL_VDW_M		(3<<22)
#define CA91CX42_LSI_CTL_VDW_D8		0
#define CA91CX42_LSI_CTL_VDW_D16	(1<<22)
#define CA91CX42_LSI_CTL_VDW_D32	(1<<23)
#define CA91CX42_LSI_CTL_VDW_D64	(3<<22)

#define CA91CX42_LSI_CTL_VAS_M		(7<<16)
#define CA91CX42_LSI_CTL_VAS_A16	0
#define CA91CX42_LSI_CTL_VAS_A24	(1<<16)
#define CA91CX42_LSI_CTL_VAS_A32	(1<<17)
#define CA91CX42_LSI_CTL_VAS_CRCSR	(5<<16)
#define CA91CX42_LSI_CTL_VAS_USER1	(3<<17)
#define CA91CX42_LSI_CTL_VAS_USER2	(7<<16)

#define CA91CX42_LSI_CTL_PGM_M		(1<<14)
#define CA91CX42_LSI_CTL_PGM_DATA	0
#define CA91CX42_LSI_CTL_PGM_PGM	(1<<14)

#define CA91CX42_LSI_CTL_SUPER_M	(1<<12)
#define CA91CX42_LSI_CTL_SUPER_NPRIV	0
#define CA91CX42_LSI_CTL_SUPER_SUPR	(1<<12)

#define CA91CX42_LSI_CTL_VCT_M		(1<<8)
#define CA91CX42_LSI_CTL_VCT_BLT	(1<<8)
#define CA91CX42_LSI_CTL_VCT_MBLT	(1<<8)
#define CA91CX42_LSI_CTL_LAS		(1<<0)

/*
 * SCYC_CTL Register
 * offset 178
 */
#define CA91CX42_SCYC_CTL_LAS_PCIMEM	0
#define CA91CX42_SCYC_CTL_LAS_PCIIO	(1<<2)

#define CA91CX42_SCYC_CTL_CYC_M		(3<<0)
#define CA91CX42_SCYC_CTL_CYC_RMW	(1<<0)
#define CA91CX42_SCYC_CTL_CYC_ADOH	(1<<1)

/*
 * LMISC Register
 * offset  184
 */
#define CA91CX42_BM_LMISC_CRT               0xF0000000
#define CA91CX42_OF_LMISC_CRT               28
#define CA91CX42_BM_LMISC_CWT               0x0F000000
#define CA91CX42_OF_LMISC_CWT               24

/*
 * SLSI Register
 * offset  188
 */
#define CA91CX42_BM_SLSI_EN                 0x80000000
#define CA91CX42_BM_SLSI_PWEN               0x40000000
#define CA91CX42_BM_SLSI_VDW                0x00F00000
#define CA91CX42_OF_SLSI_VDW                20
#define CA91CX42_BM_SLSI_PGM                0x0000F000
#define CA91CX42_OF_SLSI_PGM                12
#define CA91CX42_BM_SLSI_SUPER              0x00000F00
#define CA91CX42_OF_SLSI_SUPER              8
#define CA91CX42_BM_SLSI_BS                 0x000000F6
#define CA91CX42_OF_SLSI_BS                 2
#define CA91CX42_BM_SLSI_LAS                0x00000003
#define CA91CX42_OF_SLSI_LAS                0
#define CA91CX42_BM_SLSI_RESERVED           0x3F0F0000

/*
 * DCTL Register
 * offset 200
 */
#define CA91CX42_DCTL_L2V		(1<<31)
#define CA91CX42_DCTL_VDW_M		(3<<22)
#define CA91CX42_DCTL_VDW_D8		0
#define CA91CX42_DCTL_VDW_D16		(1<<22)
#define CA91CX42_DCTL_VDW_D32		(1<<23)
#define CA91CX42_DCTL_VDW_D64		(3<<22)

#define CA91CX42_DCTL_VAS_M		(7<<16)
#define CA91CX42_DCTL_VAS_A16		0
#define CA91CX42_DCTL_VAS_A24		(1<<16)
#define CA91CX42_DCTL_VAS_A32		(1<<17)
#define CA91CX42_DCTL_VAS_USER1		(3<<17)
#define CA91CX42_DCTL_VAS_USER2		(7<<16)

#define CA91CX42_DCTL_PGM_M		(1<<14)
#define CA91CX42_DCTL_PGM_DATA		0
#define CA91CX42_DCTL_PGM_PGM		(1<<14)

#define CA91CX42_DCTL_SUPER_M		(1<<12)
#define CA91CX42_DCTL_SUPER_NPRIV	0
#define CA91CX42_DCTL_SUPER_SUPR	(1<<12)

#define CA91CX42_DCTL_VCT_M		(1<<8)
#define CA91CX42_DCTL_VCT_BLT		(1<<8)
#define CA91CX42_DCTL_LD64EN		(1<<7)

/*
 * DCPP Register
 * offset 218
 */
#define CA91CX42_DCPP_M			0xf
#define CA91CX42_DCPP_NULL		(1<<0)

/*
 * DMA General Control/Status Register (DGCS)
 * offset 220
 */
#define CA91CX42_DGCS_GO		(1<<31)
#define CA91CX42_DGCS_STOP_REQ		(1<<30)
#define CA91CX42_DGCS_HALT_REQ		(1<<29)
#define CA91CX42_DGCS_CHAIN		(1<<27)

#define CA91CX42_DGCS_VON_M		(7<<20)

#define CA91CX42_DGCS_VOFF_M		(0xf<<16)

#define CA91CX42_DGCS_ACT		(1<<15)
#define CA91CX42_DGCS_STOP		(1<<14)
#define CA91CX42_DGCS_HALT		(1<<13)
#define CA91CX42_DGCS_DONE		(1<<11)
#define CA91CX42_DGCS_LERR		(1<<10)
#define CA91CX42_DGCS_VERR		(1<<9)
#define CA91CX42_DGCS_PERR		(1<<8)
#define CA91CX42_DGCS_INT_STOP		(1<<6)
#define CA91CX42_DGCS_INT_HALT		(1<<5)
#define CA91CX42_DGCS_INT_DONE		(1<<3)
#define CA91CX42_DGCS_INT_LERR		(1<<2)
#define CA91CX42_DGCS_INT_VERR		(1<<1)
#define CA91CX42_DGCS_INT_PERR		(1<<0)

/*
 * PCI Interrupt Enable Register
 * offset  300
 */
#define CA91CX42_LINT_LM3		0x00800000
#define CA91CX42_LINT_LM2		0x00400000
#define CA91CX42_LINT_LM1		0x00200000
#define CA91CX42_LINT_LM0		0x00100000
#define CA91CX42_LINT_MBOX3		0x00080000
#define CA91CX42_LINT_MBOX2		0x00040000
#define CA91CX42_LINT_MBOX1		0x00020000
#define CA91CX42_LINT_MBOX0		0x00010000
#define CA91CX42_LINT_ACFAIL		0x00008000
#define CA91CX42_LINT_SYSFAIL		0x00004000
#define CA91CX42_LINT_SW_INT		0x00002000
#define CA91CX42_LINT_SW_IACK		0x00001000

#define CA91CX42_LINT_VERR		0x00000400
#define CA91CX42_LINT_LERR		0x00000200
#define CA91CX42_LINT_DMA		0x00000100
#define CA91CX42_LINT_VIRQ7		0x00000080
#define CA91CX42_LINT_VIRQ6		0x00000040
#define CA91CX42_LINT_VIRQ5		0x00000020
#define CA91CX42_LINT_VIRQ4		0x00000010
#define CA91CX42_LINT_VIRQ3		0x00000008
#define CA91CX42_LINT_VIRQ2		0x00000004
#define CA91CX42_LINT_VIRQ1		0x00000002
#define CA91CX42_LINT_VOWN		0x00000001

static const int CA91CX42_LINT_VIRQ[] = { 0, CA91CX42_LINT_VIRQ1,
				CA91CX42_LINT_VIRQ2, CA91CX42_LINT_VIRQ3,
				CA91CX42_LINT_VIRQ4, CA91CX42_LINT_VIRQ5,
				CA91CX42_LINT_VIRQ6, CA91CX42_LINT_VIRQ7 };

#define CA91CX42_LINT_MBOX		0x000F0000

static const int CA91CX42_LINT_LM[] = { CA91CX42_LINT_LM0, CA91CX42_LINT_LM1,
					CA91CX42_LINT_LM2, CA91CX42_LINT_LM3 };

/*
 * MAST_CTL Register
 * offset  400
 */
#define CA91CX42_BM_MAST_CTL_MAXRTRY        0xF0000000
#define CA91CX42_OF_MAST_CTL_MAXRTRY        28
#define CA91CX42_BM_MAST_CTL_PWON           0x0F000000
#define CA91CX42_OF_MAST_CTL_PWON           24
#define CA91CX42_BM_MAST_CTL_VRL            0x00C00000
#define CA91CX42_OF_MAST_CTL_VRL            22
#define CA91CX42_BM_MAST_CTL_VRM            0x00200000
#define CA91CX42_BM_MAST_CTL_VREL           0x00100000
#define CA91CX42_BM_MAST_CTL_VOWN           0x00080000
#define CA91CX42_BM_MAST_CTL_VOWN_ACK       0x00040000
#define CA91CX42_BM_MAST_CTL_PABS           0x00001000
#define CA91CX42_BM_MAST_CTL_BUS_NO         0x0000000F
#define CA91CX42_OF_MAST_CTL_BUS_NO         0

/*
 * MISC_CTL Register
 * offset  404
 */
#define CA91CX42_MISC_CTL_VBTO           0xF0000000
#define CA91CX42_MISC_CTL_VARB           0x04000000
#define CA91CX42_MISC_CTL_VARBTO         0x03000000
#define CA91CX42_MISC_CTL_SW_LRST        0x00800000
#define CA91CX42_MISC_CTL_SW_SRST        0x00400000
#define CA91CX42_MISC_CTL_BI             0x00100000
#define CA91CX42_MISC_CTL_ENGBI          0x00080000
#define CA91CX42_MISC_CTL_RESCIND        0x00040000
#define CA91CX42_MISC_CTL_SYSCON         0x00020000
#define CA91CX42_MISC_CTL_V64AUTO        0x00010000
#define CA91CX42_MISC_CTL_RESERVED       0x0820FFFF

#define CA91CX42_OF_MISC_CTL_VARBTO         24
#define CA91CX42_OF_MISC_CTL_VBTO           28

/*
 * MISC_STAT Register
 * offset  408
 */
#define CA91CX42_BM_MISC_STAT_ENDIAN        0x80000000
#define CA91CX42_BM_MISC_STAT_LCLSIZE       0x40000000
#define CA91CX42_BM_MISC_STAT_DY4AUTO       0x08000000
#define CA91CX42_BM_MISC_STAT_MYBBSY        0x00200000
#define CA91CX42_BM_MISC_STAT_DY4DONE       0x00080000
#define CA91CX42_BM_MISC_STAT_TXFE          0x00040000
#define CA91CX42_BM_MISC_STAT_RXFE          0x00020000
#define CA91CX42_BM_MISC_STAT_DY4AUTOID     0x0000FF00
#define CA91CX42_OF_MISC_STAT_DY4AUTOID     8

/*
 * VSI Control Register
 * offset  F00
 */
#define CA91CX42_VSI_CTL_EN		(1<<31)
#define CA91CX42_VSI_CTL_PWEN		(1<<30)
#define CA91CX42_VSI_CTL_PREN		(1<<29)

#define CA91CX42_VSI_CTL_PGM_M		(3<<22)
#define CA91CX42_VSI_CTL_PGM_DATA	(1<<22)
#define CA91CX42_VSI_CTL_PGM_PGM	(1<<23)

#define CA91CX42_VSI_CTL_SUPER_M	(3<<20)
#define CA91CX42_VSI_CTL_SUPER_NPRIV	(1<<20)
#define CA91CX42_VSI_CTL_SUPER_SUPR	(1<<21)

#define CA91CX42_VSI_CTL_VAS_M		(7<<16)
#define CA91CX42_VSI_CTL_VAS_A16	0
#define CA91CX42_VSI_CTL_VAS_A24	(1<<16)
#define CA91CX42_VSI_CTL_VAS_A32	(1<<17)
#define CA91CX42_VSI_CTL_VAS_USER1	(3<<17)
#define CA91CX42_VSI_CTL_VAS_USER2	(7<<16)

#define CA91CX42_VSI_CTL_LD64EN		(1<<7)
#define CA91CX42_VSI_CTL_LLRMW		(1<<6)

#define CA91CX42_VSI_CTL_LAS_M		(3<<0)
#define CA91CX42_VSI_CTL_LAS_PCI_MS	0
#define CA91CX42_VSI_CTL_LAS_PCI_IO	(1<<0)
#define CA91CX42_VSI_CTL_LAS_PCI_CONF	(1<<1)

/* LM_CTL Register
 * offset  F64
 */
#define CA91CX42_LM_CTL_EN		(1<<31)
#define CA91CX42_LM_CTL_PGM		(1<<23)
#define CA91CX42_LM_CTL_DATA		(1<<22)
#define CA91CX42_LM_CTL_SUPR		(1<<21)
#define CA91CX42_LM_CTL_NPRIV		(1<<20)
#define CA91CX42_LM_CTL_AS_M		(7<<16)
#define CA91CX42_LM_CTL_AS_A16		0
#define CA91CX42_LM_CTL_AS_A24		(1<<16)
#define CA91CX42_LM_CTL_AS_A32		(1<<17)

/*
 * VRAI_CTL Register
 * offset  F70
 */
#define CA91CX42_BM_VRAI_CTL_EN             0x80000000
#define CA91CX42_BM_VRAI_CTL_PGM            0x00C00000
#define CA91CX42_OF_VRAI_CTL_PGM            22
#define CA91CX42_BM_VRAI_CTL_SUPER          0x00300000
#define CA91CX42_OF_VRAI_CTL_SUPER          20
#define CA91CX42_BM_VRAI_CTL_VAS            0x00030000
#define CA91CX42_OF_VRAI_CTL_VAS            16

/* VCSR_CTL Register
 * offset F80
 */
#define CA91CX42_VCSR_CTL_EN		(1<<31)

#define CA91CX42_VCSR_CTL_LAS_M		(3<<0)
#define CA91CX42_VCSR_CTL_LAS_PCI_MS	0
#define CA91CX42_VCSR_CTL_LAS_PCI_IO	(1<<0)
#define CA91CX42_VCSR_CTL_LAS_PCI_CONF	(1<<1)

/* VCSR_BS Register
 * offset FFC
 */
#define CA91CX42_VCSR_BS_SLOT_M		(0x1F<<27)

#endif /* _CA91CX42_H */
