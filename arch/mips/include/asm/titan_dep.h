/*
 * Copyright 2003 PMC-Sierra
 * Author: Manish Lachwani (lachwani@pmc-sierra.com)
 *
 * Board specific definititions for the PMC-Sierra Yosemite
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __TITAN_DEP_H__
#define __TITAN_DEP_H__

#include <asm/addrspace.h>              /* for KSEG1ADDR() */
#include <asm/byteorder.h>              /* for cpu_to_le32() */

#define TITAN_READ(ofs)							\
	(*(volatile u32 *)(ocd_base+(ofs)))
#define TITAN_READ_16(ofs)						\
	(*(volatile u16 *)(ocd_base+(ofs)))
#define TITAN_READ_8(ofs)						\
	(*(volatile u8 *)(ocd_base+(ofs)))

#define TITAN_WRITE(ofs, data)						\
	do { *(volatile u32 *)(ocd_base+(ofs)) = (data); } while (0)
#define TITAN_WRITE_16(ofs, data)					\
	do { *(volatile u16 *)(ocd_base+(ofs)) = (data); } while (0)
#define TITAN_WRITE_8(ofs, data)					\
	do { *(volatile u8 *)(ocd_base+(ofs)) = (data); } while (0)

/*
 * PCI specific defines
 */
#define	TITAN_PCI_0_CONFIG_ADDRESS	0x780
#define	TITAN_PCI_0_CONFIG_DATA		0x784

/*
 * HT specific defines
 */
#define RM9000x2_HTLINK_REG		0xbb000644
#define RM9000x2_BASE_ADDR		0xbb000000

#define OCD_BASE			0xfb000000UL
#define OCD_SIZE			0x3000UL

extern unsigned long ocd_base;

/*
 * OCD Registers
 */
#define RM9000x2_OCD_LKB5		0x0128		/* Ethernet */
#define RM9000x2_OCD_LKM5		0x012c

#define RM9000x2_OCD_LKB7		0x0138		/* HT Region 0 */
#define RM9000x2_OCD_LKM7		0x013c
#define RM9000x2_OCD_LKB8		0x0140		/* HT Region 1 */
#define RM9000x2_OCD_LKM8		0x0144

#define RM9000x2_OCD_LKB9		0x0148		/* Local Bus */
#define RM9000x2_OCD_LKM9		0x014c
#define RM9000x2_OCD_LKB10		0x0150
#define RM9000x2_OCD_LKM10		0x0154
#define RM9000x2_OCD_LKB11		0x0158
#define RM9000x2_OCD_LKM11		0x015c
#define RM9000x2_OCD_LKB12		0x0160
#define RM9000x2_OCD_LKM12		0x0164

#define RM9000x2_OCD_LKB13		0x0168		/* Scratch RAM */
#define RM9000x2_OCD_LKM13		0x016c

#define RM9000x2_OCD_LPD0		0x0200		/* Local Bus */
#define RM9000x2_OCD_LPD1		0x0210
#define RM9000x2_OCD_LPD2		0x0220
#define RM9000x2_OCD_LPD3		0x0230

#define RM9000x2_OCD_HTDVID		0x0600	/* HT Device Header */
#define RM9000x2_OCD_HTSC		0x0604
#define RM9000x2_OCD_HTCCR		0x0608
#define RM9000x2_OCD_HTBHL		0x060c
#define RM9000x2_OCD_HTBAR0		0x0610
#define RM9000x2_OCD_HTBAR1		0x0614
#define RM9000x2_OCD_HTBAR2		0x0618
#define RM9000x2_OCD_HTBAR3		0x061c
#define RM9000x2_OCD_HTBAR4		0x0620
#define RM9000x2_OCD_HTBAR5		0x0624
#define RM9000x2_OCD_HTCBCPT		0x0628
#define RM9000x2_OCD_HTSDVID		0x062c
#define RM9000x2_OCD_HTXRA		0x0630
#define RM9000x2_OCD_HTCAP1		0x0634
#define RM9000x2_OCD_HTIL		0x063c

#define RM9000x2_OCD_HTLCC		0x0640	/* HT Capability Block */
#define RM9000x2_OCD_HTLINK		0x0644
#define RM9000x2_OCD_HTFQREV		0x0648

#define RM9000x2_OCD_HTERCTL		0x0668	/* HT Controller */
#define RM9000x2_OCD_HTRXDB		0x066c
#define RM9000x2_OCD_HTIMPED		0x0670
#define RM9000x2_OCD_HTSWIMP		0x0674
#define RM9000x2_OCD_HTCAL		0x0678

#define RM9000x2_OCD_HTBAA30		0x0680
#define RM9000x2_OCD_HTBAA54		0x0684
#define RM9000x2_OCD_HTMASK0		0x0688
#define RM9000x2_OCD_HTMASK1		0x068c
#define RM9000x2_OCD_HTMASK2		0x0690
#define RM9000x2_OCD_HTMASK3		0x0694
#define RM9000x2_OCD_HTMASK4		0x0698
#define RM9000x2_OCD_HTMASK5		0x069c

#define RM9000x2_OCD_HTIFCTL		0x06a0
#define RM9000x2_OCD_HTPLL		0x06a4

#define RM9000x2_OCD_HTSRI		0x06b0
#define RM9000x2_OCD_HTRXNUM		0x06b4
#define RM9000x2_OCD_HTTXNUM		0x06b8

#define RM9000x2_OCD_HTTXCNT		0x06c8

#define RM9000x2_OCD_HTERROR		0x06d8
#define RM9000x2_OCD_HTRCRCE		0x06dc
#define RM9000x2_OCD_HTEOI		0x06e0

#define RM9000x2_OCD_CRCR		0x06f0

#define RM9000x2_OCD_HTCFGA		0x06f8
#define RM9000x2_OCD_HTCFGD		0x06fc

#define RM9000x2_OCD_INTMSG		0x0a00

#define RM9000x2_OCD_INTPIN0		0x0a40
#define RM9000x2_OCD_INTPIN1		0x0a44
#define RM9000x2_OCD_INTPIN2		0x0a48
#define RM9000x2_OCD_INTPIN3		0x0a4c
#define RM9000x2_OCD_INTPIN4		0x0a50
#define RM9000x2_OCD_INTPIN5		0x0a54
#define RM9000x2_OCD_INTPIN6		0x0a58
#define RM9000x2_OCD_INTPIN7		0x0a5c
#define RM9000x2_OCD_SEM		0x0a60
#define RM9000x2_OCD_SEMSET		0x0a64
#define RM9000x2_OCD_SEMCLR		0x0a68

#define RM9000x2_OCD_TKT		0x0a70
#define RM9000x2_OCD_TKTINC		0x0a74

#define RM9000x2_OCD_NMICONFIG		0x0ac0		/* Interrupts */
#define RM9000x2_OCD_INTP0PRI		0x1a80
#define RM9000x2_OCD_INTP1PRI		0x1a80
#define RM9000x2_OCD_INTP0STATUS0	0x1b00
#define RM9000x2_OCD_INTP0MASK0		0x1b04
#define RM9000x2_OCD_INTP0SET0		0x1b08
#define RM9000x2_OCD_INTP0CLEAR0	0x1b0c
#define RM9000x2_OCD_INTP0STATUS1	0x1b10
#define RM9000x2_OCD_INTP0MASK1		0x1b14
#define RM9000x2_OCD_INTP0SET1		0x1b18
#define RM9000x2_OCD_INTP0CLEAR1	0x1b1c
#define RM9000x2_OCD_INTP0STATUS2	0x1b20
#define RM9000x2_OCD_INTP0MASK2		0x1b24
#define RM9000x2_OCD_INTP0SET2		0x1b28
#define RM9000x2_OCD_INTP0CLEAR2	0x1b2c
#define RM9000x2_OCD_INTP0STATUS3	0x1b30
#define RM9000x2_OCD_INTP0MASK3		0x1b34
#define RM9000x2_OCD_INTP0SET3		0x1b38
#define RM9000x2_OCD_INTP0CLEAR3	0x1b3c
#define RM9000x2_OCD_INTP0STATUS4	0x1b40
#define RM9000x2_OCD_INTP0MASK4		0x1b44
#define RM9000x2_OCD_INTP0SET4		0x1b48
#define RM9000x2_OCD_INTP0CLEAR4	0x1b4c
#define RM9000x2_OCD_INTP0STATUS5	0x1b50
#define RM9000x2_OCD_INTP0MASK5		0x1b54
#define RM9000x2_OCD_INTP0SET5		0x1b58
#define RM9000x2_OCD_INTP0CLEAR5	0x1b5c
#define RM9000x2_OCD_INTP0STATUS6	0x1b60
#define RM9000x2_OCD_INTP0MASK6		0x1b64
#define RM9000x2_OCD_INTP0SET6		0x1b68
#define RM9000x2_OCD_INTP0CLEAR6	0x1b6c
#define RM9000x2_OCD_INTP0STATUS7	0x1b70
#define RM9000x2_OCD_INTP0MASK7		0x1b74
#define RM9000x2_OCD_INTP0SET7		0x1b78
#define RM9000x2_OCD_INTP0CLEAR7	0x1b7c
#define RM9000x2_OCD_INTP1STATUS0	0x2b00
#define RM9000x2_OCD_INTP1MASK0		0x2b04
#define RM9000x2_OCD_INTP1SET0		0x2b08
#define RM9000x2_OCD_INTP1CLEAR0	0x2b0c
#define RM9000x2_OCD_INTP1STATUS1	0x2b10
#define RM9000x2_OCD_INTP1MASK1		0x2b14
#define RM9000x2_OCD_INTP1SET1		0x2b18
#define RM9000x2_OCD_INTP1CLEAR1	0x2b1c
#define RM9000x2_OCD_INTP1STATUS2	0x2b20
#define RM9000x2_OCD_INTP1MASK2		0x2b24
#define RM9000x2_OCD_INTP1SET2		0x2b28
#define RM9000x2_OCD_INTP1CLEAR2	0x2b2c
#define RM9000x2_OCD_INTP1STATUS3	0x2b30
#define RM9000x2_OCD_INTP1MASK3		0x2b34
#define RM9000x2_OCD_INTP1SET3		0x2b38
#define RM9000x2_OCD_INTP1CLEAR3	0x2b3c
#define RM9000x2_OCD_INTP1STATUS4	0x2b40
#define RM9000x2_OCD_INTP1MASK4		0x2b44
#define RM9000x2_OCD_INTP1SET4		0x2b48
#define RM9000x2_OCD_INTP1CLEAR4	0x2b4c
#define RM9000x2_OCD_INTP1STATUS5	0x2b50
#define RM9000x2_OCD_INTP1MASK5		0x2b54
#define RM9000x2_OCD_INTP1SET5		0x2b58
#define RM9000x2_OCD_INTP1CLEAR5	0x2b5c
#define RM9000x2_OCD_INTP1STATUS6	0x2b60
#define RM9000x2_OCD_INTP1MASK6		0x2b64
#define RM9000x2_OCD_INTP1SET6		0x2b68
#define RM9000x2_OCD_INTP1CLEAR6	0x2b6c
#define RM9000x2_OCD_INTP1STATUS7	0x2b70
#define RM9000x2_OCD_INTP1MASK7		0x2b74
#define RM9000x2_OCD_INTP1SET7		0x2b78
#define RM9000x2_OCD_INTP1CLEAR7	0x2b7c

#define OCD_READ(reg)		(*(volatile unsigned int *)(ocd_base + (reg)))
#define OCD_WRITE(reg, val)					\
	do { *(volatile unsigned int *)(ocd_base + (reg)) = (val); } while (0)

/*
 * Hypertransport specific macros
 */
#define RM9K_WRITE(ofs, data)   *(volatile u_int32_t *)(RM9000x2_BASE_ADDR+ofs) = data
#define RM9K_WRITE_8(ofs, data) *(volatile u8 *)(RM9000x2_BASE_ADDR+ofs) = data
#define RM9K_WRITE_16(ofs, data) *(volatile u16 *)(RM9000x2_BASE_ADDR+ofs) = data

#define RM9K_READ(ofs, val)     *(val) = *(volatile u_int32_t *)(RM9000x2_BASE_ADDR+ofs)
#define RM9K_READ_8(ofs, val)   *(val) = *(volatile u8 *)(RM9000x2_BASE_ADDR+ofs)
#define RM9K_READ_16(ofs, val)  *(val) = *(volatile u16 *)(RM9000x2_BASE_ADDR+ofs)

#endif
