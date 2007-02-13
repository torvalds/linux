/*
 * Aic94xx SAS/SATA driver hardware registers defintions.
 *
 * Copyright (C) 2004 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2004 David Chaw <david_chaw@adaptec.com>
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * Luben Tuikov: Some register value updates to make it work with the window
 * agnostic register r/w functions.  Some register corrections, sizes,
 * etc.
 *
 * This file is licensed under GPLv2.
 *
 * This file is part of the aic94xx driver.
 *
 * The aic94xx driver is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 * The aic94xx driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the aic94xx driver; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * $Id: //depot/aic94xx/aic94xx_reg_def.h#27 $
 *
 */

#ifndef _ADP94XX_REG_DEF_H_
#define _ADP94XX_REG_DEF_H_

/*
 * Common definitions.
 */
#define CSEQ_MODE_PAGE_SIZE	0x200		/* CSEQ mode page size */
#define LmSEQ_MODE_PAGE_SIZE	0x200		/* LmSEQ mode page size */
#define LmSEQ_HOST_REG_SIZE   	0x4000		/* LmSEQ Host Register size */

/********************* COM_SAS registers definition *************************/

/* The base is REG_BASE_ADDR, defined in aic94xx_reg.h.
 */

/*
 * CHIM Registers, Address Range : (0x00-0xFF)
 */
#define COMBIST		(REG_BASE_ADDR + 0x00)

/* bits 31:24 */
#define		L7BLKRST		0x80000000
#define		L6BLKRST		0x40000000
#define		L5BLKRST		0x20000000
#define		L4BLKRST		0x10000000
#define		L3BLKRST		0x08000000
#define		L2BLKRST		0x04000000
#define		L1BLKRST		0x02000000
#define		L0BLKRST		0x01000000
#define		LmBLKRST		0xFF000000
#define LmBLKRST_COMBIST(phyid)		(1 << (24 + phyid))

#define		OCMBLKRST		0x00400000
#define		CTXMEMBLKRST		0x00200000
#define		CSEQBLKRST		0x00100000
#define		EXSIBLKRST		0x00040000
#define		DPIBLKRST		0x00020000
#define		DFIFBLKRST		0x00010000
#define		HARDRST			0x00000200
#define		COMBLKRST		0x00000100
#define		FRCDFPERR		0x00000080
#define		FRCCIOPERR		0x00000020
#define		FRCBISTERR		0x00000010
#define		COMBISTEN		0x00000004
#define		COMBISTDONE		0x00000002	/* ro */
#define 	COMBISTFAIL		0x00000001	/* ro */

#define COMSTAT		(REG_BASE_ADDR + 0x04)

#define		REQMBXREAD		0x00000040
#define 	RSPMBXAVAIL		0x00000020
#define 	CSBUFPERR		0x00000008
#define		OVLYERR			0x00000004
#define 	CSERR			0x00000002
#define		OVLYDMADONE		0x00000001

#define		COMSTAT_MASK		(REQMBXREAD | RSPMBXAVAIL | \
					 CSBUFPERR | OVLYERR | CSERR |\
					 OVLYDMADONE)

#define COMSTATEN	(REG_BASE_ADDR + 0x08)

#define		EN_REQMBXREAD		0x00000040
#define		EN_RSPMBXAVAIL		0x00000020
#define		EN_CSBUFPERR		0x00000008
#define		EN_OVLYERR		0x00000004
#define		EN_CSERR		0x00000002
#define		EN_OVLYDONE		0x00000001

#define SCBPRO		(REG_BASE_ADDR + 0x0C)

#define		SCBCONS_MASK		0xFFFF0000
#define		SCBPRO_MASK		0x0000FFFF

#define CHIMREQMBX	(REG_BASE_ADDR + 0x10)

#define CHIMRSPMBX	(REG_BASE_ADDR + 0x14)

#define CHIMINT		(REG_BASE_ADDR + 0x18)

#define		EXT_INT0		0x00000800
#define		EXT_INT1		0x00000400
#define		PORRSTDET		0x00000200
#define		HARDRSTDET		0x00000100
#define		DLAVAILQ		0x00000080	/* ro */
#define		HOSTERR			0x00000040
#define		INITERR			0x00000020
#define		DEVINT			0x00000010
#define		COMINT			0x00000008
#define		DEVTIMER2		0x00000004
#define		DEVTIMER1		0x00000002
#define		DLAVAIL			0x00000001

#define		CHIMINT_MASK		(HOSTERR | INITERR | DEVINT | COMINT |\
					 DEVTIMER2 | DEVTIMER1 | DLAVAIL)

#define 	DEVEXCEPT_MASK		(HOSTERR | INITERR | DEVINT | COMINT)

#define CHIMINTEN	(REG_BASE_ADDR + 0x1C)

#define		RST_EN_EXT_INT1		0x01000000
#define		RST_EN_EXT_INT0		0x00800000
#define		RST_EN_HOSTERR		0x00400000
#define		RST_EN_INITERR		0x00200000
#define		RST_EN_DEVINT		0x00100000
#define		RST_EN_COMINT		0x00080000
#define		RST_EN_DEVTIMER2	0x00040000
#define		RST_EN_DEVTIMER1	0x00020000
#define		RST_EN_DLAVAIL		0x00010000
#define		SET_EN_EXT_INT1		0x00000100
#define		SET_EN_EXT_INT0		0x00000080
#define		SET_EN_HOSTERR		0x00000040
#define		SET_EN_INITERR		0x00000020
#define		SET_EN_DEVINT		0x00000010
#define		SET_EN_COMINT		0x00000008
#define		SET_EN_DEVTIMER2	0x00000004
#define		SET_EN_DEVTIMER1	0x00000002
#define		SET_EN_DLAVAIL		0x00000001

#define		RST_CHIMINTEN		(RST_EN_HOSTERR | RST_EN_INITERR | \
					 RST_EN_DEVINT | RST_EN_COMINT | \
					 RST_EN_DEVTIMER2 | RST_EN_DEVTIMER1 |\
					 RST_EN_DLAVAIL)

#define		SET_CHIMINTEN		(SET_EN_HOSTERR | SET_EN_INITERR |\
					 SET_EN_DEVINT | SET_EN_COMINT |\
					 SET_EN_DLAVAIL)

#define OVLYDMACTL	(REG_BASE_ADDR + 0x20)

#define		OVLYADR_MASK		0x07FF0000
#define		OVLYLSEQ_MASK		0x0000FF00
#define		OVLYCSEQ		0x00000080
#define		OVLYHALTERR		0x00000040
#define		PIOCMODE		0x00000020
#define		RESETOVLYDMA		0x00000008	/* wo */
#define		STARTOVLYDMA		0x00000004
#define		STOPOVLYDMA		0x00000002	/* wo */
#define		OVLYDMAACT		0x00000001	/* ro */

#define OVLYDMACNT	(REG_BASE_ADDR + 0x24)

#define		OVLYDOMAIN1		0x20000000	/* ro */
#define		OVLYDOMAIN0		0x10000000
#define		OVLYBUFADR_MASK		0x007F0000
#define		OVLYDMACNT_MASK		0x00003FFF

#define OVLYDMAADR	(REG_BASE_ADDR + 0x28)

#define DMAERR		(REG_BASE_ADDR + 0x30)

#define		OVLYERRSTAT_MASK	0x0000FF00	/* ro */
#define		CSERRSTAT_MASK		0x000000FF	/* ro */

#define SPIODATA	(REG_BASE_ADDR + 0x34)

/* 0x38 - 0x3C are reserved  */

#define T1CNTRLR	(REG_BASE_ADDR + 0x40)

#define		T1DONE			0x00010000	/* ro */
#define		TIMER64			0x00000400
#define		T1ENABLE		0x00000200
#define		T1RELOAD		0x00000100
#define		T1PRESCALER_MASK	0x00000003

#define	T1CMPR		(REG_BASE_ADDR + 0x44)

#define T1CNTR		(REG_BASE_ADDR + 0x48)

#define T2CNTRLR	(REG_BASE_ADDR + 0x4C)

#define		T2DONE			0x00010000	/* ro */
#define		T2ENABLE		0x00000200
#define		T2RELOAD		0x00000100
#define		T2PRESCALER_MASK	0x00000003

#define	T2CMPR		(REG_BASE_ADDR + 0x50)

#define T2CNTR		(REG_BASE_ADDR + 0x54)

/* 0x58h - 0xFCh are reserved */

/*
 * DCH_SAS Registers, Address Range : (0x800-0xFFF)
 */
#define CMDCTXBASE	(REG_BASE_ADDR + 0x800)

#define DEVCTXBASE	(REG_BASE_ADDR + 0x808)

#define CTXDOMAIN	(REG_BASE_ADDR + 0x810)

#define		DEVCTXDOMAIN1		0x00000008	/* ro */
#define		DEVCTXDOMAIN0		0x00000004
#define		CMDCTXDOMAIN1		0x00000002	/* ro */
#define		CMDCTXDOMAIN0		0x00000001

#define DCHCTL		(REG_BASE_ADDR + 0x814)

#define		OCMBISTREPAIR		0x00080000
#define		OCMBISTEN		0x00040000
#define		OCMBISTDN		0x00020000	/* ro */
#define		OCMBISTFAIL		0x00010000	/* ro */
#define		DDBBISTEN		0x00004000
#define		DDBBISTDN		0x00002000	/* ro */
#define		DDBBISTFAIL		0x00001000	/* ro */
#define		SCBBISTEN		0x00000400
#define		SCBBISTDN		0x00000200	/* ro */
#define		SCBBISTFAIL		0x00000100	/* ro */

#define		MEMSEL_MASK		0x000000E0
#define		MEMSEL_CCM_LSEQ		0x00000000
#define		MEMSEL_CCM_IOP		0x00000020
#define		MEMSEL_CCM_SASCTL	0x00000040
#define		MEMSEL_DCM_LSEQ		0x00000060
#define		MEMSEL_DCM_IOP		0x00000080
#define		MEMSEL_OCM		0x000000A0

#define		FRCERR			0x00000010
#define		AUTORLS			0x00000001

#define DCHREVISION	(REG_BASE_ADDR + 0x818)

#define		DCHREVISION_MASK	0x000000FF

#define DCHSTATUS	(REG_BASE_ADDR + 0x81C)

#define		EN_CFIFTOERR		0x00020000
#define		CFIFTOERR		0x00000200
#define		CSEQINT			0x00000100	/* ro */
#define		LSEQ7INT		0x00000080	/* ro */
#define		LSEQ6INT		0x00000040	/* ro */
#define		LSEQ5INT		0x00000020	/* ro */
#define		LSEQ4INT		0x00000010	/* ro */
#define		LSEQ3INT		0x00000008	/* ro */
#define		LSEQ2INT		0x00000004	/* ro */
#define		LSEQ1INT		0x00000002	/* ro */
#define		LSEQ0INT		0x00000001	/* ro */

#define		LSEQINT_MASK		(LSEQ7INT | LSEQ6INT | LSEQ5INT |\
					 LSEQ4INT | LSEQ3INT | LSEQ2INT	|\
					 LSEQ1INT | LSEQ0INT)

#define DCHDFIFDEBUG	(REG_BASE_ADDR + 0x820)
#define		ENFAIRMST		0x00FF0000
#define		DISWRMST9		0x00000200
#define		DISWRMST8		0x00000100
#define		DISRDMST		0x000000FF

#define ATOMICSTATCTL	(REG_BASE_ADDR + 0x824)
/* 8 bit wide */
#define		AUTOINC			0x80
#define		ATOMICERR		0x04
#define		ATOMICWIN		0x02
#define		ATOMICDONE		0x01


#define ALTCIOADR	(REG_BASE_ADDR + 0x828)
/* 16 bit; bits 8:0 define CIO addr space of CSEQ */

#define ASCBPTR		(REG_BASE_ADDR + 0x82C)
/* 16 bit wide */

#define ADDBPTR		(REG_BASE_ADDR + 0x82E)
/* 16 bit wide */

#define ANEWDATA	(REG_BASE_ADDR + 0x830)
/* 16 bit */

#define AOLDDATA	(REG_BASE_ADDR + 0x834)
/* 16 bit */

#define CTXACCESS	(REG_BASE_ADDR + 0x838)
/* 32 bit */

/* 0x83Ch - 0xFFCh are reserved */

/*
 * ARP2 External Processor Registers, Address Range : (0x00-0x1F)
 */
#define ARP2CTL		0x00

#define		FRCSCRPERR		0x00040000
#define		FRCARP2PERR		0x00020000
#define		FRCARP2ILLOPC		0x00010000
#define		ENWAITTO		0x00008000
#define		PERRORDIS		0x00004000
#define		FAILDIS			0x00002000
#define		CIOPERRDIS		0x00001000
#define		BREAKEN3		0x00000800
#define		BREAKEN2		0x00000400
#define		BREAKEN1		0x00000200
#define		BREAKEN0		0x00000100
#define		EPAUSE			0x00000008
#define		PAUSED			0x00000004	/* ro */
#define		STEP			0x00000002
#define		ARP2RESET		0x00000001	/* wo */

#define ARP2INT		0x04

#define		HALTCODE_MASK		0x00FF0000	/* ro */
#define		ARP2WAITTO		0x00000100
#define		ARP2HALTC		0x00000080
#define		ARP2ILLOPC		0x00000040
#define		ARP2PERR		0x00000020
#define		ARP2CIOPERR		0x00000010
#define		ARP2BREAK3		0x00000008
#define		ARP2BREAK2		0x00000004
#define		ARP2BREAK1		0x00000002
#define		ARP2BREAK0		0x00000001

#define ARP2INTEN	0x08

#define		EN_ARP2WAITTO		0x00000100
#define		EN_ARP2HALTC		0x00000080
#define		EN_ARP2ILLOPC		0x00000040
#define		EN_ARP2PERR		0x00000020
#define		EN_ARP2CIOPERR		0x00000010
#define		EN_ARP2BREAK3		0x00000008
#define		EN_ARP2BREAK2		0x00000004
#define		EN_ARP2BREAK1		0x00000002
#define		EN_ARP2BREAK0		0x00000001

#define ARP2BREAKADR01	0x0C

#define		BREAKADR1_MASK		0x0FFF0000
#define		BREAKADR0_MASK		0x00000FFF

#define	ARP2BREAKADR23	0x10

#define		BREAKADR3_MASK		0x0FFF0000
#define		BREAKADR2_MASK		0x00000FFF

/* 0x14h - 0x1Ch are reserved */

/*
 * ARP2 Registers, Address Range : (0x00-0x1F)
 * The definitions have the same address offset for CSEQ and LmSEQ
 * CIO Bus Registers.
 */
#define MODEPTR		0x00

#define		DSTMODE			0xF0
#define		SRCMODE			0x0F

#define ALTMODE		0x01

#define		ALTDMODE		0xF0
#define		ALTSMODE		0x0F

#define ATOMICXCHG	0x02

#define FLAG		0x04

#define		INTCODE_MASK		0xF0
#define		ALTMODEV2		0x04
#define		CARRY_INT		0x02
#define		CARRY			0x01

#define ARP2INTCTL	0x05

#define 	PAUSEDIS		0x80
#define		RSTINTCTL		0x40
#define		POPALTMODE		0x08
#define		ALTMODEV		0x04
#define		INTMASK			0x02
#define		IRET			0x01

#define STACK		0x06

#define FUNCTION1	0x07

#define PRGMCNT		0x08

#define ACCUM		0x0A

#define SINDEX		0x0C

#define DINDEX		0x0E

#define ALLONES		0x10

#define ALLZEROS	0x11

#define SINDIR		0x12

#define DINDIR		0x13

#define JUMLDIR		0x14

#define ARP2HALTCODE	0x15

#define CURRADDR	0x16

#define LASTADDR	0x18

#define NXTLADDR	0x1A

#define DBGPORTPTR	0x1C

#define DBGPORT		0x1D

/*
 * CIO Registers.
 * The definitions have the same address offset for CSEQ and LmSEQ
 * CIO Bus Registers.
 */
#define MnSCBPTR      	0x20

#define MnDDBPTR      	0x22

#define SCRATCHPAGE	0x24

#define MnSCRATCHPAGE	0x25

#define SCRATCHPAGESV	0x26

#define MnSCRATCHPAGESV	0x27

#define MnDMAERRS	0x46

#define MnSGDMAERRS	0x47

#define MnSGBUF		0x53

#define MnSGDMASTAT	0x5b

#define MnDDMACTL	0x5c	/* RAZOR.rspec.fm rev 1.5 is wrong */

#define MnDDMASTAT	0x5d	/* RAZOR.rspec.fm rev 1.5 is wrong */

#define MnDDMAMODE	0x5e	/* RAZOR.rspec.fm rev 1.5 is wrong */

#define MnDMAENG	0x60

#define MnPIPECTL	0x61

#define MnSGBADR	0x65

#define MnSCB_SITE	0x100

#define MnDDB_SITE	0x180

/*
 * The common definitions below have the same address offset for both
 * CSEQ and LmSEQ.
 */
#define BISTCTL0	0x4C

#define BISTCTL1	0x50

#define MAPPEDSCR	0x800

/*
 * CSEQ Host Register, Address Range : (0x000-0xFFC)
 */
#define CSEQ_HOST_REG_BASE_ADR		0xB8001000

#define CARP2CTL			(CSEQ_HOST_REG_BASE_ADR	+ ARP2CTL)

#define CARP2INT			(CSEQ_HOST_REG_BASE_ADR	+ ARP2INT)

#define CARP2INTEN			(CSEQ_HOST_REG_BASE_ADR	+ ARP2INTEN)

#define CARP2BREAKADR01			(CSEQ_HOST_REG_BASE_ADR+ARP2BREAKADR01)

#define CARP2BREAKADR23			(CSEQ_HOST_REG_BASE_ADR+ARP2BREAKADR23)

#define CBISTCTL			(CSEQ_HOST_REG_BASE_ADR	+ BISTCTL1)

#define		CSEQRAMBISTEN		0x00000040
#define		CSEQRAMBISTDN		0x00000020	/* ro */
#define		CSEQRAMBISTFAIL		0x00000010	/* ro */
#define		CSEQSCRBISTEN		0x00000004
#define		CSEQSCRBISTDN		0x00000002	/* ro */
#define		CSEQSCRBISTFAIL		0x00000001	/* ro */

#define CMAPPEDSCR			(CSEQ_HOST_REG_BASE_ADR	+ MAPPEDSCR)

/*
 * CSEQ CIO Bus Registers, Address Range : (0x0000-0x1FFC)
 * 16 modes, each mode is 512 bytes.
 * Unless specified, the register should valid for all modes.
 */
#define CSEQ_CIO_REG_BASE_ADR		REG_BASE_ADDR_CSEQCIO

#define CSEQm_CIO_REG(Mode, Reg) \
		(CSEQ_CIO_REG_BASE_ADR  + \
		((u32) (Mode) * CSEQ_MODE_PAGE_SIZE) + (u32) (Reg))

#define CMODEPTR	(CSEQ_CIO_REG_BASE_ADR + MODEPTR)

#define CALTMODE	(CSEQ_CIO_REG_BASE_ADR + ALTMODE)

#define CATOMICXCHG	(CSEQ_CIO_REG_BASE_ADR + ATOMICXCHG)

#define CFLAG		(CSEQ_CIO_REG_BASE_ADR + FLAG)

#define CARP2INTCTL	(CSEQ_CIO_REG_BASE_ADR + ARP2INTCTL)

#define CSTACK		(CSEQ_CIO_REG_BASE_ADR + STACK)

#define CFUNCTION1	(CSEQ_CIO_REG_BASE_ADR + FUNCTION1)

#define CPRGMCNT	(CSEQ_CIO_REG_BASE_ADR + PRGMCNT)

#define CACCUM		(CSEQ_CIO_REG_BASE_ADR + ACCUM)

#define CSINDEX		(CSEQ_CIO_REG_BASE_ADR + SINDEX)

#define CDINDEX		(CSEQ_CIO_REG_BASE_ADR + DINDEX)

#define CALLONES	(CSEQ_CIO_REG_BASE_ADR + ALLONES)

#define CALLZEROS	(CSEQ_CIO_REG_BASE_ADR + ALLZEROS)

#define CSINDIR		(CSEQ_CIO_REG_BASE_ADR + SINDIR)

#define CDINDIR		(CSEQ_CIO_REG_BASE_ADR + DINDIR)

#define CJUMLDIR	(CSEQ_CIO_REG_BASE_ADR + JUMLDIR)

#define CARP2HALTCODE	(CSEQ_CIO_REG_BASE_ADR + ARP2HALTCODE)

#define CCURRADDR	(CSEQ_CIO_REG_BASE_ADR + CURRADDR)

#define CLASTADDR	(CSEQ_CIO_REG_BASE_ADR + LASTADDR)

#define CNXTLADDR	(CSEQ_CIO_REG_BASE_ADR + NXTLADDR)

#define CDBGPORTPTR	(CSEQ_CIO_REG_BASE_ADR + DBGPORTPTR)

#define CDBGPORT	(CSEQ_CIO_REG_BASE_ADR + DBGPORT)

#define CSCRATCHPAGE	(CSEQ_CIO_REG_BASE_ADR + SCRATCHPAGE)

#define CMnSCBPTR(Mode)       CSEQm_CIO_REG(Mode, MnSCBPTR)

#define CMnDDBPTR(Mode)       CSEQm_CIO_REG(Mode, MnDDBPTR)

#define CMnSCRATCHPAGE(Mode)		CSEQm_CIO_REG(Mode, MnSCRATCHPAGE)

#define CLINKCON	(CSEQ_CIO_REG_BASE_ADR + 0x28)

#define	CCIOAACESS	(CSEQ_CIO_REG_BASE_ADR + 0x2C)

/* mode 0-7 */
#define MnREQMBX 0x30
#define CMnREQMBX(Mode)			CSEQm_CIO_REG(Mode, 0x30)

/* mode 8 */
#define CSEQCON				CSEQm_CIO_REG(8, 0x30)

/* mode 0-7 */
#define MnRSPMBX 0x34
#define CMnRSPMBX(Mode)			CSEQm_CIO_REG(Mode, 0x34)

/* mode 8 */
#define CSEQCOMCTL			CSEQm_CIO_REG(8, 0x34)

/* mode 8 */
#define CSEQCOMSTAT			CSEQm_CIO_REG(8, 0x35)

/* mode 8 */
#define CSEQCOMINTEN			CSEQm_CIO_REG(8, 0x36)

/* mode 8 */
#define CSEQCOMDMACTL			CSEQm_CIO_REG(8, 0x37)

#define		CSHALTERR		0x10
#define		RESETCSDMA		0x08		/* wo */
#define		STARTCSDMA		0x04
#define		STOPCSDMA		0x02		/* wo */
#define		CSDMAACT		0x01		/* ro */

/* mode 0-7 */
#define MnINT 0x38
#define CMnINT(Mode)			CSEQm_CIO_REG(Mode, 0x38)

#define		CMnREQMBXE		0x02
#define		CMnRSPMBXF		0x01
#define		CMnINT_MASK		0x00000003

/* mode 8 */
#define CSEQREQMBX			CSEQm_CIO_REG(8, 0x38)

/* mode 0-7 */
#define MnINTEN 0x3C
#define CMnINTEN(Mode)			CSEQm_CIO_REG(Mode, 0x3C)

#define		EN_CMnRSPMBXF		0x01

/* mode 8 */
#define CSEQRSPMBX			CSEQm_CIO_REG(8, 0x3C)

/* mode 8 */
#define CSDMAADR			CSEQm_CIO_REG(8, 0x40)

/* mode 8 */
#define CSDMACNT			CSEQm_CIO_REG(8, 0x48)

/* mode 8 */
#define CSEQDLCTL			CSEQm_CIO_REG(8, 0x4D)

#define		DONELISTEND		0x10
#define 	DONELISTSIZE_MASK	0x0F
#define		DONELISTSIZE_8ELEM	0x01
#define		DONELISTSIZE_16ELEM	0x02
#define		DONELISTSIZE_32ELEM	0x03
#define		DONELISTSIZE_64ELEM	0x04
#define		DONELISTSIZE_128ELEM	0x05
#define		DONELISTSIZE_256ELEM	0x06
#define		DONELISTSIZE_512ELEM	0x07
#define		DONELISTSIZE_1024ELEM	0x08
#define		DONELISTSIZE_2048ELEM	0x09
#define		DONELISTSIZE_4096ELEM	0x0A
#define		DONELISTSIZE_8192ELEM	0x0B
#define		DONELISTSIZE_16384ELEM	0x0C

/* mode 8 */
#define CSEQDLOFFS			CSEQm_CIO_REG(8, 0x4E)

/* mode 11 */
#define CM11INTVEC0			CSEQm_CIO_REG(11, 0x50)

/* mode 11 */
#define CM11INTVEC1			CSEQm_CIO_REG(11, 0x52)

/* mode 11 */
#define CM11INTVEC2			CSEQm_CIO_REG(11, 0x54)

#define	CCONMSK	  			(CSEQ_CIO_REG_BASE_ADR + 0x60)

#define	CCONEXIST			(CSEQ_CIO_REG_BASE_ADR + 0x61)

#define	CCONMODE			(CSEQ_CIO_REG_BASE_ADR + 0x62)

#define CTIMERCALC			(CSEQ_CIO_REG_BASE_ADR + 0x64)

#define CINTDIS				(CSEQ_CIO_REG_BASE_ADR + 0x68)

/* mode 8, 32x32 bits, 128 bytes of mapped buffer */
#define CSBUFFER			CSEQm_CIO_REG(8, 0x80)

#define	CSCRATCH			(CSEQ_CIO_REG_BASE_ADR + 0x1C0)

/* mode 0-8 */
#define CMnSCRATCH(Mode)		CSEQm_CIO_REG(Mode, 0x1E0)

/*
 * CSEQ Mapped Instruction RAM Page, Address Range : (0x0000-0x1FFC)
 */
#define CSEQ_RAM_REG_BASE_ADR		0xB8004000

/*
 * The common definitions below have the same address offset for all the Link
 * sequencers.
 */
#define MODECTL		0x40

#define DBGMODE		0x44

#define CONTROL		0x48
#define LEDTIMER		0x00010000
#define LEDTIMERS_10us		0x00000000
#define LEDTIMERS_1ms		0x00000800
#define LEDTIMERS_100ms		0x00001000
#define LEDMODE_TXRX		0x00000000
#define LEDMODE_CONNECTED	0x00000200
#define LEDPOL			0x00000100

#define LSEQRAM		0x1000

/*
 * LmSEQ Host Registers, Address Range : (0x0000-0x3FFC)
 */
#define LSEQ0_HOST_REG_BASE_ADR		0xB8020000
#define LSEQ1_HOST_REG_BASE_ADR		0xB8024000
#define LSEQ2_HOST_REG_BASE_ADR		0xB8028000
#define LSEQ3_HOST_REG_BASE_ADR		0xB802C000
#define LSEQ4_HOST_REG_BASE_ADR		0xB8030000
#define LSEQ5_HOST_REG_BASE_ADR		0xB8034000
#define LSEQ6_HOST_REG_BASE_ADR		0xB8038000
#define LSEQ7_HOST_REG_BASE_ADR		0xB803C000

#define LmARP2CTL(LinkNum)		(LSEQ0_HOST_REG_BASE_ADR +	  \
					((LinkNum)*LmSEQ_HOST_REG_SIZE) + \
					ARP2CTL)

#define LmARP2INT(LinkNum)		(LSEQ0_HOST_REG_BASE_ADR +	  \
					((LinkNum)*LmSEQ_HOST_REG_SIZE) + \
					ARP2INT)

#define LmARP2INTEN(LinkNum)		(LSEQ0_HOST_REG_BASE_ADR +	  \
					((LinkNum)*LmSEQ_HOST_REG_SIZE) + \
					ARP2INTEN)

#define LmDBGMODE(LinkNum)		(LSEQ0_HOST_REG_BASE_ADR +	  \
					((LinkNum)*LmSEQ_HOST_REG_SIZE) + \
					DBGMODE)

#define LmCONTROL(LinkNum)		(LSEQ0_HOST_REG_BASE_ADR +	  \
					((LinkNum)*LmSEQ_HOST_REG_SIZE) + \
					CONTROL)

#define LmARP2BREAKADR01(LinkNum)	(LSEQ0_HOST_REG_BASE_ADR +	  \
					((LinkNum)*LmSEQ_HOST_REG_SIZE) + \
					ARP2BREAKADR01)

#define LmARP2BREAKADR23(LinkNum)	(LSEQ0_HOST_REG_BASE_ADR +	  \
					((LinkNum)*LmSEQ_HOST_REG_SIZE) + \
					ARP2BREAKADR23)

#define LmMODECTL(LinkNum)		(LSEQ0_HOST_REG_BASE_ADR +	  \
					((LinkNum)*LmSEQ_HOST_REG_SIZE) + \
					MODECTL)

#define		LmAUTODISCI		0x08000000
#define		LmDSBLBITLT		0x04000000
#define		LmDSBLANTT		0x02000000
#define		LmDSBLCRTT		0x01000000
#define		LmDSBLCONT		0x00000100
#define		LmPRIMODE		0x00000080
#define		LmDSBLHOLD		0x00000040
#define		LmDISACK		0x00000020
#define		LmBLIND48		0x00000010
#define		LmRCVMODE_MASK		0x0000000C
#define		LmRCVMODE_PLD		0x00000000
#define		LmRCVMODE_HPC		0x00000004

#define LmDBGMODE(LinkNum)		(LSEQ0_HOST_REG_BASE_ADR +	  \
					((LinkNum)*LmSEQ_HOST_REG_SIZE) + \
					DBGMODE)

#define		LmFRCPERR		0x80000000
#define		LmMEMSEL_MASK		0x30000000
#define		LmFRCRBPERR		0x00000000
#define		LmFRCTBPERR		0x10000000
#define		LmFRCSGBPERR		0x20000000
#define		LmFRCARBPERR		0x30000000
#define		LmRCVIDW		0x00080000
#define		LmINVDWERR		0x00040000
#define		LmRCVDISP		0x00004000
#define		LmDISPERR		0x00002000
#define		LmDSBLDSCR		0x00000800
#define		LmDSBLSCR		0x00000400
#define		LmFRCNAK		0x00000200
#define		LmFRCROFS		0x00000100
#define		LmFRCCRC		0x00000080
#define		LmFRMTYPE_MASK		0x00000070
#define		LmSG_DATA		0x00000000
#define		LmSG_COMMAND		0x00000010
#define		LmSG_TASK		0x00000020
#define		LmSG_TGTXFER		0x00000030
#define		LmSG_RESPONSE		0x00000040
#define		LmSG_IDENADDR		0x00000050
#define		LmSG_OPENADDR		0x00000060
#define		LmDISCRCGEN		0x00000008
#define		LmDISCRCCHK		0x00000004
#define		LmSSXMTFRM		0x00000002
#define		LmSSRCVFRM		0x00000001

#define LmCONTROL(LinkNum)		(LSEQ0_HOST_REG_BASE_ADR +	  \
					((LinkNum)*LmSEQ_HOST_REG_SIZE) + \
					CONTROL)

#define		LmSTEPXMTFRM		0x00000002
#define		LmSTEPRCVFRM		0x00000001

#define LmBISTCTL0(LinkNum)		(LSEQ0_HOST_REG_BASE_ADR +	  \
					((LinkNum)*LmSEQ_HOST_REG_SIZE) + \
					BISTCTL0)

#define		ARBBISTEN		0x40000000
#define		ARBBISTDN		0x20000000	/* ro */
#define		ARBBISTFAIL		0x10000000	/* ro */
#define		TBBISTEN		0x00000400
#define		TBBISTDN		0x00000200	/* ro */
#define		TBBISTFAIL		0x00000100	/* ro */
#define		RBBISTEN		0x00000040
#define		RBBISTDN		0x00000020	/* ro */
#define		RBBISTFAIL		0x00000010	/* ro */
#define		SGBISTEN		0x00000004
#define		SGBISTDN		0x00000002	/* ro */
#define		SGBISTFAIL		0x00000001	/* ro */

#define LmBISTCTL1(LinkNum)		(LSEQ0_HOST_REG_BASE_ADR +	 \
					((LinkNum)*LmSEQ_HOST_REG_SIZE) +\
					BISTCTL1)

#define		LmRAMPAGE1		0x00000200
#define		LmRAMPAGE0		0x00000100
#define		LmIMEMBISTEN		0x00000040
#define		LmIMEMBISTDN		0x00000020	/* ro */
#define		LmIMEMBISTFAIL		0x00000010	/* ro */
#define		LmSCRBISTEN		0x00000004
#define		LmSCRBISTDN		0x00000002	/* ro */
#define		LmSCRBISTFAIL		0x00000001	/* ro */
#define		LmRAMPAGE		(LmRAMPAGE1 + LmRAMPAGE0)
#define		LmRAMPAGE_LSHIFT	0x8

#define LmSCRATCH(LinkNum)		(LSEQ0_HOST_REG_BASE_ADR +	   \
					((LinkNum) * LmSEQ_HOST_REG_SIZE) +\
					MAPPEDSCR)

#define LmSEQRAM(LinkNum)		(LSEQ0_HOST_REG_BASE_ADR +	   \
					((LinkNum) * LmSEQ_HOST_REG_SIZE) +\
					LSEQRAM)

/*
 * LmSEQ CIO Bus Register, Address Range : (0x0000-0xFFC)
 * 8 modes, each mode is 512 bytes.
 * Unless specified, the register should valid for all modes.
 */
#define LmSEQ_CIOBUS_REG_BASE		0x2000

#define  LmSEQ_PHY_BASE(Mode, LinkNum) \
		(LSEQ0_HOST_REG_BASE_ADR + \
		(LmSEQ_HOST_REG_SIZE * (u32) (LinkNum)) + \
		LmSEQ_CIOBUS_REG_BASE + \
		((u32) (Mode) * LmSEQ_MODE_PAGE_SIZE))

#define  LmSEQ_PHY_REG(Mode, LinkNum, Reg) \
                 (LmSEQ_PHY_BASE(Mode, LinkNum) + (u32) (Reg))

#define LmMODEPTR(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, MODEPTR)

#define LmALTMODE(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, ALTMODE)

#define LmATOMICXCHG(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, ATOMICXCHG)

#define LmFLAG(LinkNum)			LmSEQ_PHY_REG(0, LinkNum, FLAG)

#define LmARP2INTCTL(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, ARP2INTCTL)

#define LmSTACK(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, STACK)

#define LmFUNCTION1(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, FUNCTION1)

#define LmPRGMCNT(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, PRGMCNT)

#define LmACCUM(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, ACCUM)

#define LmSINDEX(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, SINDEX)

#define LmDINDEX(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, DINDEX)

#define LmALLONES(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, ALLONES)

#define LmALLZEROS(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, ALLZEROS)

#define LmSINDIR(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, SINDIR)

#define LmDINDIR(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, DINDIR)

#define LmJUMLDIR(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, JUMLDIR)

#define LmARP2HALTCODE(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, ARP2HALTCODE)

#define LmCURRADDR(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, CURRADDR)

#define LmLASTADDR(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, LASTADDR)

#define LmNXTLADDR(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, NXTLADDR)

#define LmDBGPORTPTR(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, DBGPORTPTR)

#define LmDBGPORT(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, DBGPORT)

#define LmSCRATCHPAGE(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, SCRATCHPAGE)

#define LmMnSCRATCHPAGE(LinkNum, Mode)	LmSEQ_PHY_REG(Mode, LinkNum, 	\
						      MnSCRATCHPAGE)

#define LmTIMERCALC(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, 0x28)

#define LmREQMBX(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, 0x30)

#define LmRSPMBX(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, 0x34)

#define LmMnINT(LinkNum, Mode)		LmSEQ_PHY_REG(Mode, LinkNum, 0x38)

#define		CTXMEMSIZE		0x80000000	/* ro */
#define		LmACKREQ		0x08000000
#define		LmNAKREQ		0x04000000
#define		LmMnXMTERR		0x02000000
#define		LmM5OOBSVC		0x01000000
#define		LmHWTINT		0x00800000
#define		LmMnCTXDONE		0x00100000
#define		LmM2REQMBXF		0x00080000
#define		LmM2RSPMBXE		0x00040000
#define		LmMnDMAERR		0x00020000
#define		LmRCVPRIM		0x00010000
#define		LmRCVERR		0x00008000
#define		LmADDRRCV		0x00004000
#define		LmMnHDRMISS		0x00002000
#define		LmMnWAITSCB		0x00001000
#define		LmMnRLSSCB		0x00000800
#define		LmMnSAVECTX		0x00000400
#define		LmMnFETCHSG		0x00000200
#define		LmMnLOADCTX		0x00000100
#define		LmMnCFGICL		0x00000080
#define		LmMnCFGSATA		0x00000040
#define		LmMnCFGEXPSATA		0x00000020
#define		LmMnCFGCMPLT		0x00000010
#define		LmMnCFGRBUF		0x00000008
#define		LmMnSAVETTR		0x00000004
#define		LmMnCFGRDAT		0x00000002
#define		LmMnCFGHDR		0x00000001

#define LmMnINTEN(LinkNum, Mode)	LmSEQ_PHY_REG(Mode, LinkNum, 0x3C)

#define		EN_LmACKREQ		0x08000000
#define		EN_LmNAKREQ		0x04000000
#define		EN_LmMnXMTERR		0x02000000
#define		EN_LmM5OOBSVC		0x01000000
#define		EN_LmHWTINT		0x00800000
#define		EN_LmMnCTXDONE		0x00100000
#define		EN_LmM2REQMBXF		0x00080000
#define		EN_LmM2RSPMBXE		0x00040000
#define		EN_LmMnDMAERR		0x00020000
#define		EN_LmRCVPRIM		0x00010000
#define		EN_LmRCVERR		0x00008000
#define		EN_LmADDRRCV		0x00004000
#define		EN_LmMnHDRMISS		0x00002000
#define		EN_LmMnWAITSCB		0x00001000
#define		EN_LmMnRLSSCB		0x00000800
#define		EN_LmMnSAVECTX		0x00000400
#define		EN_LmMnFETCHSG		0x00000200
#define		EN_LmMnLOADCTX		0x00000100
#define		EN_LmMnCFGICL		0x00000080
#define		EN_LmMnCFGSATA		0x00000040
#define		EN_LmMnCFGEXPSATA	0x00000020
#define		EN_LmMnCFGCMPLT		0x00000010
#define		EN_LmMnCFGRBUF		0x00000008
#define		EN_LmMnSAVETTR		0x00000004
#define		EN_LmMnCFGRDAT		0x00000002
#define		EN_LmMnCFGHDR		0x00000001

#define		LmM0INTEN_MASK		(EN_LmMnCFGCMPLT | EN_LmMnCFGRBUF | \
					 EN_LmMnSAVETTR | EN_LmMnCFGRDAT | \
					 EN_LmMnCFGHDR | EN_LmRCVERR | \
					 EN_LmADDRRCV | EN_LmMnHDRMISS | \
					 EN_LmMnRLSSCB | EN_LmMnSAVECTX | \
					 EN_LmMnFETCHSG | EN_LmMnLOADCTX | \
					 EN_LmHWTINT | EN_LmMnCTXDONE | \
					 EN_LmRCVPRIM | EN_LmMnCFGSATA | \
					 EN_LmMnCFGEXPSATA | EN_LmMnDMAERR)

#define		LmM1INTEN_MASK		(EN_LmMnCFGCMPLT | EN_LmADDRRCV | \
					 EN_LmMnRLSSCB | EN_LmMnSAVECTX | \
					 EN_LmMnFETCHSG | EN_LmMnLOADCTX | \
					 EN_LmMnXMTERR | EN_LmHWTINT | \
					 EN_LmMnCTXDONE | EN_LmRCVPRIM | \
					 EN_LmRCVERR | EN_LmMnDMAERR)

#define		LmM2INTEN_MASK		(EN_LmADDRRCV | EN_LmHWTINT | \
					 EN_LmM2REQMBXF | EN_LmRCVPRIM | \
					 EN_LmRCVERR)

#define		LmM5INTEN_MASK		(EN_LmADDRRCV | EN_LmM5OOBSVC | \
					 EN_LmHWTINT | EN_LmRCVPRIM | \
					 EN_LmRCVERR)

#define LmXMTPRIMD(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, 0x40)

#define LmXMTPRIMCS(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, 0x44)

#define LmCONSTAT(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, 0x45)

#define LmMnDMAERRS(LinkNum, Mode)	LmSEQ_PHY_REG(Mode, LinkNum, 0x46)

#define LmMnSGDMAERRS(LinkNum, Mode)	LmSEQ_PHY_REG(Mode, LinkNum, 0x47)

#define LmM0EXPHDRP(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, 0x48)

#define LmM1SASALIGN(LinkNum)		LmSEQ_PHY_REG(1, LinkNum, 0x48)
#define SAS_ALIGN_DEFAULT		0xFF

#define LmM0MSKHDRP(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, 0x49)

#define LmM1STPALIGN(LinkNum)		LmSEQ_PHY_REG(1, LinkNum, 0x49)
#define STP_ALIGN_DEFAULT		0x1F

#define LmM0RCVHDRP(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, 0x4A)

#define LmM1XMTHDRP(LinkNum)		LmSEQ_PHY_REG(1, LinkNum, 0x4A)

#define LmM0ICLADR(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, 0x4B)

#define LmM1ALIGNMODE(LinkNum)		LmSEQ_PHY_REG(1, LinkNum, 0x4B)

#define		LmDISALIGN		0x20
#define		LmROTSTPALIGN		0x10
#define		LmSTPALIGN		0x08
#define		LmROTNOTIFY		0x04
#define		LmDUALALIGN		0x02
#define		LmROTALIGN		0x01

#define LmM0EXPRCVNT(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, 0x4C)

#define LmM1XMTCNT(LinkNum)		LmSEQ_PHY_REG(1, LinkNum, 0x4C)

#define LmMnBUFSTAT(LinkNum, Mode)	LmSEQ_PHY_REG(Mode, LinkNum, 0x4E)

#define		LmMnBUFPERR		0x01

/* mode 0-1 */
#define LmMnXFRLVL(LinkNum, Mode)	LmSEQ_PHY_REG(Mode, LinkNum, 0x59)

#define		LmMnXFRLVL_128		0x05
#define		LmMnXFRLVL_256		0x04
#define		LmMnXFRLVL_512		0x03
#define		LmMnXFRLVL_1024		0x02
#define		LmMnXFRLVL_1536		0x01
#define		LmMnXFRLVL_2048		0x00

 /* mode 0-1 */
#define LmMnSGDMACTL(LinkNum, Mode)	LmSEQ_PHY_REG(Mode, LinkNum, 0x5A)

#define 	LmMnRESETSG		0x04
#define 	LmMnSTOPSG		0x02
#define 	LmMnSTARTSG		0x01

/* mode 0-1 */
#define LmMnSGDMASTAT(LinkNum, Mode)	LmSEQ_PHY_REG(Mode, LinkNum, 0x5B)

/* mode 0-1 */
#define LmMnDDMACTL(LinkNum, Mode)	LmSEQ_PHY_REG(Mode, LinkNum, 0x5C)

#define 	LmMnFLUSH		0x40		/* wo */
#define 	LmMnRLSRTRY		0x20		/* wo */
#define 	LmMnDISCARD		0x10		/* wo */
#define 	LmMnRESETDAT		0x08		/* wo */
#define 	LmMnSUSDAT		0x04		/* wo */
#define 	LmMnSTOPDAT		0x02		/* wo */
#define 	LmMnSTARTDAT		0x01		/* wo */

/* mode 0-1 */
#define LmMnDDMASTAT(LinkNum, Mode)	LmSEQ_PHY_REG(Mode, LinkNum, 0x5D)

#define		LmMnDPEMPTY		0x80
#define		LmMnFLUSHING		0x40
#define		LmMnDDMAREQ		0x20
#define		LmMnHDMAREQ		0x10
#define		LmMnDATFREE		0x08
#define		LmMnDATSUS		0x04
#define		LmMnDATACT		0x02
#define		LmMnDATEN		0x01

/* mode 0-1 */
#define LmMnDDMAMODE(LinkNum, Mode)	LmSEQ_PHY_REG(Mode, LinkNum, 0x5E)

#define 	LmMnDMATYPE_NORMAL		0x0000
#define 	LmMnDMATYPE_HOST_ONLY_TX	0x0001
#define 	LmMnDMATYPE_DEVICE_ONLY_TX	0x0002
#define 	LmMnDMATYPE_INVALID		0x0003
#define 	LmMnDMATYPE_MASK	0x0003

#define 	LmMnDMAWRAP		0x0004
#define 	LmMnBITBUCKET		0x0008
#define 	LmMnDISHDR		0x0010
#define 	LmMnSTPCRC		0x0020
#define 	LmXTEST			0x0040
#define 	LmMnDISCRC		0x0080
#define 	LmMnENINTLK		0x0100
#define 	LmMnADDRFRM		0x0400
#define 	LmMnENXMTCRC		0x0800

/* mode 0-1 */
#define LmMnXFRCNT(LinkNum, Mode)	LmSEQ_PHY_REG(Mode, LinkNum, 0x70)

/* mode 0-1 */
#define LmMnDPSEL(LinkNum, Mode)	LmSEQ_PHY_REG(Mode, LinkNum, 0x7B)
#define 	LmMnDPSEL_MASK		0x07
#define 	LmMnEOLPRE		0x40
#define 	LmMnEOSPRE		0x80

/* Registers used in conjunction with LmMnDPSEL and LmMnDPACC registers */
/* Receive Mode n = 0 */
#define LmMnHRADDR			0x00
#define LmMnHBYTECNT			0x01
#define LmMnHREWIND			0x02
#define LmMnDWADDR			0x03
#define LmMnDSPACECNT			0x04
#define LmMnDFRMSIZE			0x05

/* Registers used in conjunction with LmMnDPSEL and LmMnDPACC registers */
/* Transmit Mode n = 1 */
#define LmMnHWADDR			0x00
#define LmMnHSPACECNT			0x01
/* #define LmMnHREWIND			0x02 */
#define LmMnDRADDR			0x03
#define LmMnDBYTECNT			0x04
/* #define LmMnDFRMSIZE			0x05 */

/* mode 0-1 */
#define LmMnDPACC(LinkNum, Mode)	LmSEQ_PHY_REG(Mode, LinkNum, 0x78)
#define 	LmMnDPACC_MASK		0x00FFFFFF

/* mode 0-1 */
#define LmMnHOLDLVL(LinkNum, Mode)	LmSEQ_PHY_REG(Mode, LinkNum, 0x7D)

#define LmPRMSTAT0(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, 0x80)
#define LmPRMSTAT0BYTE0			0x80
#define LmPRMSTAT0BYTE1			0x81
#define LmPRMSTAT0BYTE2			0x82
#define LmPRMSTAT0BYTE3			0x83

#define		LmFRAMERCVD		0x80000000
#define		LmXFRRDYRCVD		0x40000000
#define		LmUNKNOWNP		0x20000000
#define		LmBREAK			0x10000000
#define		LmDONE			0x08000000
#define		LmOPENACPT		0x04000000
#define		LmOPENRJCT		0x02000000
#define		LmOPENRTRY		0x01000000
#define		LmCLOSERV1		0x00800000
#define		LmCLOSERV0		0x00400000
#define		LmCLOSENORM		0x00200000
#define		LmCLOSECLAF		0x00100000
#define		LmNOTIFYRV2		0x00080000
#define		LmNOTIFYRV1		0x00040000
#define		LmNOTIFYRV0		0x00020000
#define		LmNOTIFYSPIN		0x00010000
#define		LmBROADRV4		0x00008000
#define		LmBROADRV3		0x00004000
#define		LmBROADRV2		0x00002000
#define		LmBROADRV1		0x00001000
#define		LmBROADSES		0x00000800
#define		LmBROADRVCH1		0x00000400
#define		LmBROADRVCH0		0x00000200
#define		LmBROADCH		0x00000100
#define		LmAIPRVWP		0x00000080
#define		LmAIPWP			0x00000040
#define		LmAIPWD			0x00000020
#define		LmAIPWC			0x00000010
#define		LmAIPRV2		0x00000008
#define		LmAIPRV1		0x00000004
#define		LmAIPRV0		0x00000002
#define		LmAIPNRML		0x00000001

#define		LmBROADCAST_MASK	(LmBROADCH | LmBROADRVCH0 | \
					 LmBROADRVCH1)

#define LmPRMSTAT1(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, 0x84)
#define LmPRMSTAT1BYTE0			0x84
#define LmPRMSTAT1BYTE1			0x85
#define LmPRMSTAT1BYTE2			0x86
#define LmPRMSTAT1BYTE3			0x87

#define		LmFRMRCVDSTAT		0x80000000
#define		LmBREAK_DET		0x04000000
#define		LmCLOSE_DET		0x02000000
#define		LmDONE_DET		0x01000000
#define		LmXRDY			0x00040000
#define 	LmSYNCSRST		0x00020000
#define 	LmSYNC			0x00010000
#define 	LmXHOLD			0x00008000
#define 	LmRRDY			0x00004000
#define 	LmHOLD			0x00002000
#define 	LmROK			0x00001000
#define 	LmRIP			0x00000800
#define 	LmCRBLK			0x00000400
#define 	LmACK			0x00000200
#define 	LmNAK			0x00000100
#define 	LmHARDRST		0x00000080
#define 	LmERROR			0x00000040
#define 	LmRERR			0x00000020
#define 	LmPMREQP		0x00000010
#define 	LmPMREQS		0x00000008
#define 	LmPMACK			0x00000004
#define 	LmPMNAK			0x00000002
#define 	LmDMAT			0x00000001

/* mode 1 */
#define	LmMnSATAFS(LinkNum, Mode)	LmSEQ_PHY_REG(Mode, LinkNum, 0x7E)
#define	LmMnXMTSIZE(LinkNum, Mode)	LmSEQ_PHY_REG(Mode, LinkNum, 0x93)

/* mode 0 */
#define LmMnFRMERR(LinkNum, Mode)	LmSEQ_PHY_REG(Mode, LinkNum, 0xB0)

#define		LmACRCERR		0x00000800
#define		LmPHYOVRN		0x00000400
#define		LmOBOVRN		0x00000200
#define 	LmMnZERODATA		0x00000100
#define		LmSATAINTLK		0x00000080
#define		LmMnCRCERR		0x00000020
#define		LmRRDYOVRN		0x00000010
#define		LmMISSSOAF		0x00000008
#define		LmMISSSOF		0x00000004
#define		LmMISSEOAF		0x00000002
#define		LmMISSEOF		0x00000001

#define LmFRMERREN(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, 0xB4)

#define 	EN_LmACRCERR		0x00000800
#define 	EN_LmPHYOVRN		0x00000400
#define 	EN_LmOBOVRN		0x00000200
#define 	EN_LmMnZERODATA		0x00000100
#define 	EN_LmSATAINTLK		0x00000080
#define 	EN_LmFRMBAD		0x00000040
#define 	EN_LmMnCRCERR		0x00000020
#define 	EN_LmRRDYOVRN		0x00000010
#define 	EN_LmMISSSOAF		0x00000008
#define 	EN_LmMISSSOF		0x00000004
#define 	EN_LmMISSEOAF		0x00000002
#define 	EN_LmMISSEOF		0x00000001

#define 	LmFRMERREN_MASK  	(EN_LmSATAINTLK | EN_LmMnCRCERR | \
					 EN_LmRRDYOVRN | EN_LmMISSSOF | \
					 EN_LmMISSEOAF | EN_LmMISSEOF | \
					 EN_LmACRCERR | LmPHYOVRN | \
					 EN_LmOBOVRN | EN_LmMnZERODATA)

#define LmHWTSTATEN(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, 0xC5)

#define		EN_LmDONETO		0x80
#define		EN_LmINVDISP		0x40
#define		EN_LmINVDW		0x20
#define		EN_LmDWSEVENT		0x08
#define		EN_LmCRTTTO		0x04
#define		EN_LmANTTTO		0x02
#define		EN_LmBITLTTO		0x01

#define		LmHWTSTATEN_MASK	(EN_LmINVDISP | EN_LmINVDW | \
					 EN_LmDWSEVENT | EN_LmCRTTTO | \
					 EN_LmANTTTO | EN_LmDONETO | \
					 EN_LmBITLTTO)

#define LmHWTSTAT(LinkNum) 		LmSEQ_PHY_REG(0, LinkNum, 0xC7)

#define		LmDONETO		0x80
#define		LmINVDISP		0x40
#define		LmINVDW			0x20
#define		LmDWSEVENT		0x08
#define		LmCRTTTO		0x04
#define		LmANTTTO		0x02
#define		LmBITLTTO		0x01

#define LmMnDATABUFADR(LinkNum, Mode)	LmSEQ_PHY_REG(Mode, LinkNum, 0xC8)
#define		LmDATABUFADR_MASK	0x0FFF

#define LmMnDATABUF(LinkNum, Mode)	LmSEQ_PHY_REG(Mode, LinkNum, 0xCA)

#define	LmPRIMSTAT0EN(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, 0xE0)

#define 	EN_LmUNKNOWNP 		0x20000000
#define 	EN_LmBREAK		0x10000000
#define 	EN_LmDONE		0x08000000
#define 	EN_LmOPENACPT		0x04000000
#define 	EN_LmOPENRJCT		0x02000000
#define 	EN_LmOPENRTRY		0x01000000
#define 	EN_LmCLOSERV1		0x00800000
#define 	EN_LmCLOSERV0		0x00400000
#define 	EN_LmCLOSENORM		0x00200000
#define 	EN_LmCLOSECLAF		0x00100000
#define 	EN_LmNOTIFYRV2		0x00080000
#define 	EN_LmNOTIFYRV1		0x00040000
#define 	EN_LmNOTIFYRV0		0x00020000
#define 	EN_LmNOTIFYSPIN		0x00010000
#define 	EN_LmBROADRV4		0x00008000
#define 	EN_LmBROADRV3		0x00004000
#define 	EN_LmBROADRV2		0x00002000
#define 	EN_LmBROADRV1		0x00001000
#define 	EN_LmBROADRV0		0x00000800
#define 	EN_LmBROADRVCH1		0x00000400
#define 	EN_LmBROADRVCH0		0x00000200
#define 	EN_LmBROADCH		0x00000100
#define 	EN_LmAIPRVWP		0x00000080
#define 	EN_LmAIPWP		0x00000040
#define 	EN_LmAIPWD		0x00000020
#define 	EN_LmAIPWC		0x00000010
#define 	EN_LmAIPRV2		0x00000008
#define 	EN_LmAIPRV1		0x00000004
#define 	EN_LmAIPRV0		0x00000002
#define 	EN_LmAIPNRML		0x00000001

#define		LmPRIMSTAT0EN_MASK	(EN_LmBREAK | \
					 EN_LmDONE | EN_LmOPENACPT | \
					 EN_LmOPENRJCT | EN_LmOPENRTRY | \
					 EN_LmCLOSERV1 | EN_LmCLOSERV0 | \
					 EN_LmCLOSENORM | EN_LmCLOSECLAF | \
					 EN_LmBROADRV4 | EN_LmBROADRV3 | \
					 EN_LmBROADRV2 | EN_LmBROADRV1 | \
					 EN_LmBROADRV0 | EN_LmBROADRVCH1 | \
					 EN_LmBROADRVCH0 | EN_LmBROADCH | \
					 EN_LmAIPRVWP | EN_LmAIPWP | \
					 EN_LmAIPWD | EN_LmAIPWC | \
					 EN_LmAIPRV2 | EN_LmAIPRV1 | \
					 EN_LmAIPRV0 | EN_LmAIPNRML)

#define LmPRIMSTAT1EN(LinkNum)		LmSEQ_PHY_REG(0, LinkNum, 0xE4)

#define		EN_LmXRDY		0x00040000
#define		EN_LmSYNCSRST		0x00020000
#define		EN_LmSYNC		0x00010000
#define 	EN_LmXHOLD		0x00008000
#define 	EN_LmRRDY		0x00004000
#define 	EN_LmHOLD		0x00002000
#define 	EN_LmROK		0x00001000
#define 	EN_LmRIP		0x00000800
#define 	EN_LmCRBLK		0x00000400
#define 	EN_LmACK		0x00000200
#define 	EN_LmNAK		0x00000100
#define 	EN_LmHARDRST		0x00000080
#define 	EN_LmERROR		0x00000040
#define 	EN_LmRERR		0x00000020
#define 	EN_LmPMREQP		0x00000010
#define 	EN_LmPMREQS		0x00000008
#define 	EN_LmPMACK		0x00000004
#define 	EN_LmPMNAK		0x00000002
#define 	EN_LmDMAT		0x00000001

#define LmPRIMSTAT1EN_MASK		(EN_LmHARDRST | \
					 EN_LmSYNCSRST | \
					 EN_LmPMREQP | EN_LmPMREQS | \
					 EN_LmPMACK | EN_LmPMNAK)

#define LmSMSTATE(LinkNum) 		LmSEQ_PHY_REG(0, LinkNum, 0xE8)

#define LmSMSTATEBRK(LinkNum) 		LmSEQ_PHY_REG(0, LinkNum, 0xEC)

#define LmSMDBGCTL(LinkNum) 		LmSEQ_PHY_REG(0, LinkNum, 0xF0)


/*
 * LmSEQ CIO Bus Mode 3 Register.
 * Mode 3: Configuration and Setup, IOP Context SCB.
 */
#define LmM3SATATIMER(LinkNum) 		LmSEQ_PHY_REG(3, LinkNum, 0x48)

#define LmM3INTVEC0(LinkNum)		LmSEQ_PHY_REG(3, LinkNum, 0x90)

#define LmM3INTVEC1(LinkNum)		LmSEQ_PHY_REG(3, LinkNum, 0x92)

#define LmM3INTVEC2(LinkNum)		LmSEQ_PHY_REG(3, LinkNum, 0x94)

#define LmM3INTVEC3(LinkNum)		LmSEQ_PHY_REG(3, LinkNum, 0x96)

#define LmM3INTVEC4(LinkNum)		LmSEQ_PHY_REG(3, LinkNum, 0x98)

#define LmM3INTVEC5(LinkNum)		LmSEQ_PHY_REG(3, LinkNum, 0x9A)

#define LmM3INTVEC6(LinkNum)		LmSEQ_PHY_REG(3, LinkNum, 0x9C)

#define LmM3INTVEC7(LinkNum)		LmSEQ_PHY_REG(3, LinkNum, 0x9E)

#define LmM3INTVEC8(LinkNum)		LmSEQ_PHY_REG(3, LinkNum, 0xA4)

#define LmM3INTVEC9(LinkNum)		LmSEQ_PHY_REG(3, LinkNum, 0xA6)

#define LmM3INTVEC10(LinkNum)		LmSEQ_PHY_REG(3, LinkNum, 0xB0)

#define LmM3FRMGAP(LinkNum)		LmSEQ_PHY_REG(3, LinkNum, 0xB4)

#define LmBITL_TIMER(LinkNum) 		LmSEQ_PHY_REG(0, LinkNum, 0xA2)

#define LmWWN(LinkNum) 			LmSEQ_PHY_REG(0, LinkNum, 0xA8)


/*
 * LmSEQ CIO Bus Mode 5 Registers.
 * Mode 5: Phy/OOB Control and Status.
 */
#define LmSEQ_OOB_REG(phy_id, reg)	LmSEQ_PHY_REG(5, (phy_id), (reg))

#define OOB_BFLTR	0x100

#define		BFLTR_THR_MASK		0xF0
#define		BFLTR_TC_MASK		0x0F

#define OOB_INIT_MIN	0x102

#define OOB_INIT_MAX	0x104

#define OOB_INIT_NEG	0x106

#define	OOB_SAS_MIN	0x108

#define OOB_SAS_MAX	0x10A

#define OOB_SAS_NEG	0x10C

#define OOB_WAKE_MIN	0x10E

#define OOB_WAKE_MAX	0x110

#define OOB_WAKE_NEG	0x112

#define OOB_IDLE_MAX	0x114

#define OOB_BURST_MAX	0x116

#define OOB_DATA_KBITS	0x126

#define OOB_ALIGN_0_DATA	0x12C

#define OOB_ALIGN_1_DATA	0x130

#define D10_2_DATA_k		0x00
#define SYNC_DATA_k		0x02
#define ALIGN_1_DATA_k		0x04
#define ALIGN_0_DATA_k		0x08
#define BURST_DATA_k		0x10

#define OOB_PHY_RESET_COUNT	0x13C

#define OOB_SIG_GEN	0x140

#define		START_OOB		0x80
#define		START_DWS		0x40
#define		ALIGN_CNT3		0x30
#define 	ALIGN_CNT2		0x20
#define 	ALIGN_CNT1		0x10
#define 	ALIGN_CNT4		0x00
#define		STOP_DWS		0x08
#define		SEND_COMSAS		0x04
#define		SEND_COMINIT		0x02
#define		SEND_COMWAKE		0x01

#define OOB_XMIT	0x141

#define		TX_ENABLE		0x80
#define		XMIT_OOB_BURST		0x10
#define		XMIT_D10_2		0x08
#define		XMIT_SYNC		0x04
#define		XMIT_ALIGN_1		0x02
#define		XMIT_ALIGN_0		0x01

#define FUNCTION_MASK	0x142

#define		SAS_MODE_DIS		0x80
#define		SATA_MODE_DIS		0x40
#define		SPINUP_HOLD_DIS		0x20
#define		HOT_PLUG_DIS		0x10
#define		SATA_PS_DIS		0x08
#define		FUNCTION_MASK_DEFAULT	(SPINUP_HOLD_DIS | SATA_PS_DIS)

#define OOB_MODE	0x143

#define		SAS_MODE		0x80
#define		SATA_MODE		0x40
#define		SLOW_CLK		0x20
#define		FORCE_XMIT_15		0x08
#define		PHY_SPEED_60		0x04
#define		PHY_SPEED_30		0x02
#define		PHY_SPEED_15		0x01

#define	CURRENT_STATUS	0x144

#define		CURRENT_OOB_DONE	0x80
#define		CURRENT_LOSS_OF_SIGNAL	0x40
#define		CURRENT_SPINUP_HOLD	0x20
#define		CURRENT_HOT_PLUG_CNCT	0x10
#define		CURRENT_GTO_TIMEOUT	0x08
#define		CURRENT_OOB_TIMEOUT	0x04
#define		CURRENT_DEVICE_PRESENT	0x02
#define		CURRENT_OOB_ERROR	0x01

#define 	CURRENT_OOB1_ERROR	(CURRENT_HOT_PLUG_CNCT | \
					 CURRENT_GTO_TIMEOUT)

#define 	CURRENT_OOB2_ERROR	(CURRENT_HOT_PLUG_CNCT | \
					 CURRENT_OOB_ERROR)

#define		DEVICE_ADDED_W_CNT	(CURRENT_OOB_DONE | \
					 CURRENT_HOT_PLUG_CNCT | \
					 CURRENT_DEVICE_PRESENT)

#define		DEVICE_ADDED_WO_CNT	(CURRENT_OOB_DONE | \
					 CURRENT_DEVICE_PRESENT)

#define 	DEVICE_REMOVED		CURRENT_LOSS_OF_SIGNAL

#define		CURRENT_PHY_MASK	(CURRENT_OOB_DONE | \
					 CURRENT_LOSS_OF_SIGNAL | \
					 CURRENT_SPINUP_HOLD | \
					 CURRENT_HOT_PLUG_CNCT | \
					 CURRENT_GTO_TIMEOUT | \
					 CURRENT_DEVICE_PRESENT | \
					 CURRENT_OOB_ERROR )

#define		CURRENT_ERR_MASK	(CURRENT_LOSS_OF_SIGNAL | \
					 CURRENT_GTO_TIMEOUT | \
					 CURRENT_OOB_TIMEOUT | \
					 CURRENT_OOB_ERROR )

#define SPEED_MASK	0x145

#define		SATA_SPEED_30_DIS	0x10
#define		SATA_SPEED_15_DIS	0x08
#define		SAS_SPEED_60_DIS	0x04
#define		SAS_SPEED_30_DIS	0x02
#define		SAS_SPEED_15_DIS	0x01
#define		SAS_SPEED_MASK_DEFAULT	0x00

#define OOB_TIMER_ENABLE	0x14D

#define		HOT_PLUG_EN		0x80
#define		RCD_EN			0x40
#define 	COMTIMER_EN		0x20
#define		SNTT_EN			0x10
#define		SNLT_EN			0x04
#define		SNWT_EN			0x02
#define		ALIGN_EN		0x01

#define OOB_STATUS		0x14E

#define		OOB_DONE		0x80
#define		LOSS_OF_SIGNAL		0x40		/* ro */
#define		SPINUP_HOLD		0x20
#define		HOT_PLUG_CNCT		0x10		/* ro */
#define		GTO_TIMEOUT		0x08		/* ro */
#define		OOB_TIMEOUT		0x04		/* ro */
#define		DEVICE_PRESENT		0x02		/* ro */
#define		OOB_ERROR		0x01		/* ro */

#define		OOB_STATUS_ERROR_MASK	(LOSS_OF_SIGNAL | GTO_TIMEOUT | \
					 OOB_TIMEOUT | OOB_ERROR)

#define OOB_STATUS_CLEAR	0x14F

#define		OOB_DONE_CLR		0x80
#define		LOSS_OF_SIGNAL_CLR 	0x40
#define		SPINUP_HOLD_CLR		0x20
#define		HOT_PLUG_CNCT_CLR     	0x10
#define		GTO_TIMEOUT_CLR		0x08
#define		OOB_TIMEOUT_CLR		0x04
#define		OOB_ERROR_CLR		0x01

#define HOT_PLUG_DELAY		0x150
/* In 5 ms units. 20 = 100 ms. */
#define	HOTPLUG_DELAY_TIMEOUT		20


#define INT_ENABLE_2		0x15A

#define		OOB_DONE_EN		0x80
#define		LOSS_OF_SIGNAL_EN	0x40
#define		SPINUP_HOLD_EN		0x20
#define		HOT_PLUG_CNCT_EN	0x10
#define		GTO_TIMEOUT_EN		0x08
#define		OOB_TIMEOUT_EN		0x04
#define		DEVICE_PRESENT_EN	0x02
#define		OOB_ERROR_EN		0x01

#define PHY_CONTROL_0		0x160

#define		PHY_LOWPWREN_TX		0x80
#define		PHY_LOWPWREN_RX		0x40
#define		SPARE_REG_160_B5	0x20
#define		OFFSET_CANCEL_RX	0x10

/* bits 3:2 */
#define		PHY_RXCOMCENTER_60V	0x00
#define		PHY_RXCOMCENTER_70V	0x04
#define		PHY_RXCOMCENTER_80V	0x08
#define		PHY_RXCOMCENTER_90V	0x0C
#define 	PHY_RXCOMCENTER_MASK	0x0C

#define		PHY_RESET		0x02
#define		SAS_DEFAULT_SEL		0x01

#define PHY_CONTROL_1		0x161

/* bits 2:0 */
#define		SATA_PHY_DETLEVEL_50mv	0x00
#define		SATA_PHY_DETLEVEL_75mv	0x01
#define		SATA_PHY_DETLEVEL_100mv	0x02
#define		SATA_PHY_DETLEVEL_125mv	0x03
#define		SATA_PHY_DETLEVEL_150mv	0x04
#define		SATA_PHY_DETLEVEL_175mv	0x05
#define		SATA_PHY_DETLEVEL_200mv	0x06
#define		SATA_PHY_DETLEVEL_225mv	0x07
#define		SATA_PHY_DETLEVEL_MASK	0x07

/* bits 5:3 */
#define		SAS_PHY_DETLEVEL_50mv	0x00
#define		SAS_PHY_DETLEVEL_75mv	0x08
#define		SAS_PHY_DETLEVEL_100mv	0x10
#define		SAS_PHY_DETLEVEL_125mv	0x11
#define		SAS_PHY_DETLEVEL_150mv	0x20
#define		SAS_PHY_DETLEVEL_175mv	0x21
#define		SAS_PHY_DETLEVEL_200mv	0x30
#define		SAS_PHY_DETLEVEL_225mv	0x31
#define		SAS_PHY_DETLEVEL_MASK	0x38

#define PHY_CONTROL_2		0x162

/* bits 7:5 */
#define 	SATA_PHY_DRV_400mv	0x00
#define 	SATA_PHY_DRV_450mv	0x20
#define 	SATA_PHY_DRV_500mv	0x40
#define 	SATA_PHY_DRV_550mv	0x60
#define 	SATA_PHY_DRV_600mv	0x80
#define 	SATA_PHY_DRV_650mv	0xA0
#define 	SATA_PHY_DRV_725mv	0xC0
#define 	SATA_PHY_DRV_800mv	0xE0
#define		SATA_PHY_DRV_MASK	0xE0

/* bits 4:3 */
#define 	SATA_PREEMP_0		0x00
#define 	SATA_PREEMP_1		0x08
#define 	SATA_PREEMP_2		0x10
#define 	SATA_PREEMP_3		0x18
#define 	SATA_PREEMP_MASK	0x18

#define 	SATA_CMSH1P5		0x04

/* bits 1:0 */
#define 	SATA_SLEW_0		0x00
#define 	SATA_SLEW_1		0x01
#define 	SATA_SLEW_2		0x02
#define 	SATA_SLEW_3		0x03
#define 	SATA_SLEW_MASK		0x03

#define PHY_CONTROL_3		0x163

/* bits 7:5 */
#define 	SAS_PHY_DRV_400mv	0x00
#define 	SAS_PHY_DRV_450mv	0x20
#define 	SAS_PHY_DRV_500mv	0x40
#define 	SAS_PHY_DRV_550mv	0x60
#define 	SAS_PHY_DRV_600mv	0x80
#define 	SAS_PHY_DRV_650mv	0xA0
#define 	SAS_PHY_DRV_725mv	0xC0
#define 	SAS_PHY_DRV_800mv	0xE0
#define		SAS_PHY_DRV_MASK	0xE0

/* bits 4:3 */
#define 	SAS_PREEMP_0		0x00
#define 	SAS_PREEMP_1		0x08
#define 	SAS_PREEMP_2		0x10
#define 	SAS_PREEMP_3		0x18
#define 	SAS_PREEMP_MASK		0x18

#define 	SAS_CMSH1P5		0x04

/* bits 1:0 */
#define 	SAS_SLEW_0		0x00
#define 	SAS_SLEW_1		0x01
#define 	SAS_SLEW_2		0x02
#define 	SAS_SLEW_3		0x03
#define 	SAS_SLEW_MASK		0x03

#define PHY_CONTROL_4		0x168

#define		PHY_DONE_CAL_TX		0x80
#define		PHY_DONE_CAL_RX		0x40
#define		RX_TERM_LOAD_DIS	0x20
#define		TX_TERM_LOAD_DIS	0x10
#define		AUTO_TERM_CAL_DIS	0x08
#define		PHY_SIGDET_FLTR_EN	0x04
#define		OSC_FREQ		0x02
#define		PHY_START_CAL		0x01

/*
 * HST_PCIX2 Registers, Addresss Range: (0x00-0xFC)
 */
#define PCIX_REG_BASE_ADR		0xB8040000

#define PCIC_VENDOR_ID	0x00

#define PCIC_DEVICE_ID	0x02

#define PCIC_COMMAND	0x04

#define		INT_DIS			0x0400
#define		FBB_EN			0x0200		/* ro */
#define		SERR_EN			0x0100
#define		STEP_EN			0x0080		/* ro */
#define		PERR_EN			0x0040
#define		VGA_EN			0x0020		/* ro */
#define		MWI_EN			0x0010
#define		SPC_EN			0x0008
#define		MST_EN			0x0004
#define		MEM_EN			0x0002
#define		IO_EN			0x0001

#define	PCIC_STATUS	0x06

#define		PERR_DET		0x8000
#define		SERR_GEN		0x4000
#define		MABT_DET		0x2000
#define		TABT_DET		0x1000
#define		TABT_GEN		0x0800
#define		DPERR_DET		0x0100
#define		CAP_LIST		0x0010
#define		INT_STAT		0x0008

#define	PCIC_DEVREV_ID	0x08

#define	PCIC_CLASS_CODE	0x09

#define	PCIC_CACHELINE_SIZE	0x0C

#define	PCIC_MBAR0	0x10

#define 	PCIC_MBAR0_OFFSET	0

#define	PCIC_MBAR1	0x18

#define 	PCIC_MBAR1_OFFSET	2

#define	PCIC_IOBAR	0x20

#define 	PCIC_IOBAR_OFFSET	4

#define	PCIC_SUBVENDOR_ID	0x2C

#define PCIC_SUBSYTEM_ID	0x2E

#define PCIX_STATUS		0x44
#define 	RCV_SCE		0x20000000
#define 	UNEXP_SC	0x00080000
#define 	SC_DISCARD	0x00040000

#define ECC_CTRL_STAT		0x48
#define 	UNCOR_ECCERR	0x00000008

#define PCIC_PM_CSR		0x5C

#define		PWR_STATE_D0		0
#define		PWR_STATE_D1		1	/* not supported */
#define		PWR_STATE_D2		2 	/* not supported */
#define		PWR_STATE_D3		3

#define PCIC_BASE1	0x6C	/* internal use only */

#define		BASE1_RSVD		0xFFFFFFF8

#define PCIC_BASEA	0x70	/* internal use only */

#define		BASEA_RSVD		0xFFFFFFC0
#define 	BASEA_START		0

#define PCIC_BASEB	0x74	/* internal use only */

#define		BASEB_RSVD		0xFFFFFF80
#define		BASEB_IOMAP_MASK	0x7F
#define 	BASEB_START		0x80

#define PCIC_BASEC	0x78	/* internal use only */

#define		BASEC_RSVD		0xFFFFFFFC
#define 	BASEC_MASK		0x03
#define 	BASEC_START		0x58

#define PCIC_MBAR_KEY	0x7C	/* internal use only */

#define 	MBAR_KEY_MASK		0xFFFFFFFF

#define PCIC_HSTPCIX_CNTRL	0xA0

#define 	REWIND_DIS		0x0800
#define		SC_TMR_DIS		0x04000000

#define PCIC_MBAR0_MASK	0xA8
#define		PCIC_MBAR0_SIZE_MASK 	0x1FFFE000
#define		PCIC_MBAR0_SIZE_SHIFT 	13
#define		PCIC_MBAR0_SIZE(val)	\
		    (((val) & PCIC_MBAR0_SIZE_MASK) >> PCIC_MBAR0_SIZE_SHIFT)

#define PCIC_FLASH_MBAR	0xB8

#define PCIC_INTRPT_STAT 0xD4

#define PCIC_TP_CTRL	0xFC

/*
 * EXSI Registers, Addresss Range: (0x00-0xFC)
 */
#define EXSI_REG_BASE_ADR		REG_BASE_ADDR_EXSI

#define	EXSICNFGR	(EXSI_REG_BASE_ADR + 0x00)

#define		OCMINITIALIZED		0x80000000
#define		ASIEN			0x00400000
#define		HCMODE			0x00200000
#define		PCIDEF			0x00100000
#define		COMSTOCK		0x00080000
#define		SEEPROMEND		0x00040000
#define		MSTTIMEN		0x00020000
#define		XREGEX			0x00000200
#define		NVRAMW			0x00000100
#define		NVRAMEX			0x00000080
#define		SRAMW			0x00000040
#define		SRAMEX			0x00000020
#define		FLASHW			0x00000010
#define		FLASHEX			0x00000008
#define		SEEPROMCFG		0x00000004
#define		SEEPROMTYP		0x00000002
#define		SEEPROMEX		0x00000001


#define EXSICNTRLR	(EXSI_REG_BASE_ADR + 0x04)

#define		MODINT_EN		0x00000001


#define PMSTATR		(EXSI_REG_BASE_ADR + 0x10)

#define		FLASHRST		0x00000002
#define		FLASHRDY		0x00000001


#define FLCNFGR		(EXSI_REG_BASE_ADR + 0x14)

#define		FLWEH_MASK		0x30000000
#define		FLWESU_MASK		0x0C000000
#define		FLWEPW_MASK		0x03F00000
#define		FLOEH_MASK		0x000C0000
#define 	FLOESU_MASK		0x00030000
#define 	FLOEPW_MASK		0x0000FC00
#define 	FLCSH_MASK		0x00000300
#define 	FLCSSU_MASK		0x000000C0
#define 	FLCSPW_MASK		0x0000003F

#define SRCNFGR		(EXSI_REG_BASE_ADR + 0x18)

#define		SRWEH_MASK		0x30000000
#define		SRWESU_MASK		0x0C000000
#define		SRWEPW_MASK		0x03F00000

#define		SROEH_MASK		0x000C0000
#define 	SROESU_MASK		0x00030000
#define 	SROEPW_MASK		0x0000FC00
#define		SRCSH_MASK		0x00000300
#define		SRCSSU_MASK		0x000000C0
#define		SRCSPW_MASK		0x0000003F

#define NVCNFGR		(EXSI_REG_BASE_ADR + 0x1C)

#define 	NVWEH_MASK		0x30000000
#define 	NVWESU_MASK		0x0C000000
#define 	NVWEPW_MASK		0x03F00000
#define 	NVOEH_MASK		0x000C0000
#define 	NVOESU_MASK		0x00030000
#define 	NVOEPW_MASK		0x0000FC00
#define 	NVCSH_MASK		0x00000300
#define 	NVCSSU_MASK		0x000000C0
#define 	NVCSPW_MASK		0x0000003F

#define XRCNFGR		(EXSI_REG_BASE_ADR + 0x20)

#define 	XRWEH_MASK		0x30000000
#define 	XRWESU_MASK		0x0C000000
#define 	XRWEPW_MASK		0x03F00000
#define 	XROEH_MASK		0x000C0000
#define 	XROESU_MASK		0x00030000
#define 	XROEPW_MASK		0x0000FC00
#define 	XRCSH_MASK		0x00000300
#define 	XRCSSU_MASK		0x000000C0
#define		XRCSPW_MASK		0x0000003F

#define XREGADDR	(EXSI_REG_BASE_ADR + 0x24)

#define 	XRADDRINCEN		0x80000000
#define 	XREGADD_MASK		0x007FFFFF


#define XREGDATAR	(EXSI_REG_BASE_ADR + 0x28)

#define		XREGDATA_MASK 		0x0000FFFF

#define GPIOOER		(EXSI_REG_BASE_ADR + 0x40)

#define GPIOODENR	(EXSI_REG_BASE_ADR + 0x44)

#define GPIOINVR	(EXSI_REG_BASE_ADR + 0x48)

#define GPIODATAOR	(EXSI_REG_BASE_ADR + 0x4C)

#define GPIODATAIR	(EXSI_REG_BASE_ADR + 0x50)

#define GPIOCNFGR	(EXSI_REG_BASE_ADR + 0x54)

#define		GPIO_EXTSRC		0x00000001

#define SCNTRLR		(EXSI_REG_BASE_ADR + 0xA0)

#define 	SXFERDONE		0x00000100
#define 	SXFERCNT_MASK		0x000000E0
#define 	SCMDTYP_MASK		0x0000001C
#define 	SXFERSTART		0x00000002
#define 	SXFEREN			0x00000001

#define	SRATER		(EXSI_REG_BASE_ADR + 0xA4)

#define	SADDRR		(EXSI_REG_BASE_ADR + 0xA8)

#define 	SADDR_MASK		0x0000FFFF

#define SDATAOR		(EXSI_REG_BASE_ADR + 0xAC)

#define	SDATAOR0	(EXSI_REG_BASE_ADR + 0xAC)
#define SDATAOR1	(EXSI_REG_BASE_ADR + 0xAD)
#define SDATAOR2	(EXSI_REG_BASE_ADR + 0xAE)
#define SDATAOR3	(EXSI_REG_BASE_ADR + 0xAF)

#define SDATAIR		(EXSI_REG_BASE_ADR + 0xB0)

#define SDATAIR0	(EXSI_REG_BASE_ADR + 0xB0)
#define SDATAIR1	(EXSI_REG_BASE_ADR + 0xB1)
#define SDATAIR2	(EXSI_REG_BASE_ADR + 0xB2)
#define SDATAIR3	(EXSI_REG_BASE_ADR + 0xB3)

#define ASISTAT0R	(EXSI_REG_BASE_ADR + 0xD0)
#define 	ASIFMTERR		0x00000400
#define 	ASISEECHKERR		0x00000200
#define 	ASIERR			0x00000100

#define ASISTAT1R	(EXSI_REG_BASE_ADR + 0xD4)
#define 	CHECKSUM_MASK		0x0000FFFF

#define ASIERRADDR	(EXSI_REG_BASE_ADR + 0xD8)
#define ASIERRDATAR	(EXSI_REG_BASE_ADR + 0xDC)
#define ASIERRSTATR	(EXSI_REG_BASE_ADR + 0xE0)
#define 	CPI2ASIBYTECNT_MASK	0x00070000
#define 	CPI2ASIBYTEEN_MASK      0x0000F000
#define 	CPI2ASITARGERR_MASK	0x00000F00
#define 	CPI2ASITARGMID_MASK	0x000000F0
#define 	CPI2ASIMSTERR_MASK	0x0000000F

/*
 * XSRAM, External SRAM (DWord and any BE pattern accessible)
 */
#define XSRAM_REG_BASE_ADDR             0xB8100000
#define XSRAM_SIZE                        0x100000

/*
 * NVRAM Registers, Address Range: (0x00000 - 0x3FFFF).
 */
#define		NVRAM_REG_BASE_ADR	0xBF800000
#define		NVRAM_MAX_BASE_ADR	0x003FFFFF

/* OCM base address */
#define		OCM_BASE_ADDR		0xA0000000
#define		OCM_MAX_SIZE		0x20000

/*
 * Sequencers (Central and Link) Scratch RAM page definitions.
 */

/*
 * The Central Management Sequencer (CSEQ) Scratch Memory is a 1024
 * byte memory.  It is dword accessible and has byte parity
 * protection. The CSEQ accesses it in 32 byte windows, either as mode
 * dependent or mode independent memory. Each mode has 96 bytes,
 * (three 32 byte pages 0-2, not contiguous), leaving 128 bytes of
 * Mode Independent memory (four 32 byte pages 3-7). Note that mode
 * dependent scratch memory, Mode 8, page 0-3 overlaps mode
 * independent scratch memory, pages 0-3.
 * - 896 bytes of mode dependent scratch, 96 bytes per Modes 0-7, and
 * 128 bytes in mode 8,
 * - 259 bytes of mode independent scratch, common to modes 0-15.
 *
 * Sequencer scratch RAM is 1024 bytes.  This scratch memory is
 * divided into mode dependent and mode independent scratch with this
 * memory further subdivided into pages of size 32 bytes. There are 5
 * pages (160 bytes) of mode independent scratch and 3 pages of
 * dependent scratch memory for modes 0-7 (768 bytes). Mode 8 pages
 * 0-2 dependent scratch overlap with pages 0-2 of mode independent
 * scratch memory.
 *
 * The host accesses this scratch in a different manner from the
 * central sequencer. The sequencer has to use CSEQ registers CSCRPAGE
 * and CMnSCRPAGE to access the scratch memory. A flat mapping of the
 * scratch memory is available for software convenience and to prevent
 * corruption while the sequencer is running. This memory is mapped
 * onto addresses 800h - BFFh, total of 400h bytes.
 *
 * These addresses are mapped as follows:
 *
 *        800h-83Fh   Mode Dependent Scratch Mode 0 Pages 0-1
 *        840h-87Fh   Mode Dependent Scratch Mode 1 Pages 0-1
 *        880h-8BFh   Mode Dependent Scratch Mode 2 Pages 0-1
 *        8C0h-8FFh   Mode Dependent Scratch Mode 3 Pages 0-1
 *        900h-93Fh   Mode Dependent Scratch Mode 4 Pages 0-1
 *        940h-97Fh   Mode Dependent Scratch Mode 5 Pages 0-1
 *        980h-9BFh   Mode Dependent Scratch Mode 6 Pages 0-1
 *        9C0h-9FFh   Mode Dependent Scratch Mode 7 Pages 0-1
 *        A00h-A5Fh   Mode Dependent Scratch Mode 8 Pages 0-2
 *                    Mode Independent Scratch Pages 0-2
 *        A60h-A7Fh   Mode Dependent Scratch Mode 8 Page 3
 *                    Mode Independent Scratch Page 3
 *        A80h-AFFh   Mode Independent Scratch Pages 4-7
 *        B00h-B1Fh   Mode Dependent Scratch Mode 0 Page 2
 *        B20h-B3Fh   Mode Dependent Scratch Mode 1 Page 2
 *        B40h-B5Fh   Mode Dependent Scratch Mode 2 Page 2
 *        B60h-B7Fh   Mode Dependent Scratch Mode 3 Page 2
 *        B80h-B9Fh   Mode Dependent Scratch Mode 4 Page 2
 *        BA0h-BBFh   Mode Dependent Scratch Mode 5 Page 2
 *        BC0h-BDFh   Mode Dependent Scratch Mode 6 Page 2
 *        BE0h-BFFh   Mode Dependent Scratch Mode 7 Page 2
 */

/* General macros */
#define CSEQ_PAGE_SIZE			32  /* Scratch page size (in bytes) */

/* All macros start with offsets from base + 0x800 (CMAPPEDSCR).
 * Mode dependent scratch page 0, mode 0.
 * For modes 1-7 you have to do arithmetic. */
#define CSEQ_LRM_SAVE_SINDEX		(CMAPPEDSCR + 0x0000)
#define CSEQ_LRM_SAVE_SCBPTR		(CMAPPEDSCR + 0x0002)
#define CSEQ_Q_LINK_HEAD		(CMAPPEDSCR + 0x0004)
#define CSEQ_Q_LINK_TAIL		(CMAPPEDSCR + 0x0006)
#define CSEQ_LRM_SAVE_SCRPAGE		(CMAPPEDSCR + 0x0008)

/* Mode dependent scratch page 0 mode 8 macros. */
#define CSEQ_RET_ADDR			(CMAPPEDSCR + 0x0200)
#define CSEQ_RET_SCBPTR			(CMAPPEDSCR + 0x0202)
#define CSEQ_SAVE_SCBPTR		(CMAPPEDSCR + 0x0204)
#define CSEQ_EMPTY_TRANS_CTX		(CMAPPEDSCR + 0x0206)
#define CSEQ_RESP_LEN			(CMAPPEDSCR + 0x0208)
#define CSEQ_TMF_SCBPTR			(CMAPPEDSCR + 0x020A)
#define CSEQ_GLOBAL_PREV_SCB		(CMAPPEDSCR + 0x020C)
#define CSEQ_GLOBAL_HEAD		(CMAPPEDSCR + 0x020E)
#define CSEQ_CLEAR_LU_HEAD		(CMAPPEDSCR + 0x0210)
#define CSEQ_TMF_OPCODE			(CMAPPEDSCR + 0x0212)
#define CSEQ_SCRATCH_FLAGS		(CMAPPEDSCR + 0x0213)
#define CSEQ_HSB_SITE                   (CMAPPEDSCR + 0x021A)
#define CSEQ_FIRST_INV_SCB_SITE		(CMAPPEDSCR + 0x021C)
#define CSEQ_FIRST_INV_DDB_SITE		(CMAPPEDSCR + 0x021E)

/* Mode dependent scratch page 1 mode 8 macros. */
#define CSEQ_LUN_TO_CLEAR		(CMAPPEDSCR + 0x0220)
#define CSEQ_LUN_TO_CHECK		(CMAPPEDSCR + 0x0228)

/* Mode dependent scratch page 2 mode 8 macros */
#define CSEQ_HQ_NEW_POINTER		(CMAPPEDSCR + 0x0240)
#define CSEQ_HQ_DONE_BASE		(CMAPPEDSCR + 0x0248)
#define CSEQ_HQ_DONE_POINTER		(CMAPPEDSCR + 0x0250)
#define CSEQ_HQ_DONE_PASS		(CMAPPEDSCR + 0x0254)

/* Mode independent scratch page 4 macros. */
#define CSEQ_Q_EXE_HEAD			(CMAPPEDSCR + 0x0280)
#define CSEQ_Q_EXE_TAIL			(CMAPPEDSCR + 0x0282)
#define CSEQ_Q_DONE_HEAD                (CMAPPEDSCR + 0x0284)
#define CSEQ_Q_DONE_TAIL                (CMAPPEDSCR + 0x0286)
#define CSEQ_Q_SEND_HEAD		(CMAPPEDSCR + 0x0288)
#define CSEQ_Q_SEND_TAIL		(CMAPPEDSCR + 0x028A)
#define CSEQ_Q_DMA2CHIM_HEAD		(CMAPPEDSCR + 0x028C)
#define CSEQ_Q_DMA2CHIM_TAIL		(CMAPPEDSCR + 0x028E)
#define CSEQ_Q_COPY_HEAD		(CMAPPEDSCR + 0x0290)
#define CSEQ_Q_COPY_TAIL		(CMAPPEDSCR + 0x0292)
#define CSEQ_REG0			(CMAPPEDSCR + 0x0294)
#define CSEQ_REG1			(CMAPPEDSCR + 0x0296)
#define CSEQ_REG2			(CMAPPEDSCR + 0x0298)
#define CSEQ_LINK_CTL_Q_MAP		(CMAPPEDSCR + 0x029C)
#define CSEQ_MAX_CSEQ_MODE		(CMAPPEDSCR + 0x029D)
#define CSEQ_FREE_LIST_HACK_COUNT	(CMAPPEDSCR + 0x029E)

/* Mode independent scratch page 5 macros. */
#define CSEQ_EST_NEXUS_REQ_QUEUE	(CMAPPEDSCR + 0x02A0)
#define CSEQ_EST_NEXUS_REQ_COUNT	(CMAPPEDSCR + 0x02A8)
#define CSEQ_Q_EST_NEXUS_HEAD		(CMAPPEDSCR + 0x02B0)
#define CSEQ_Q_EST_NEXUS_TAIL		(CMAPPEDSCR + 0x02B2)
#define CSEQ_NEED_EST_NEXUS_SCB		(CMAPPEDSCR + 0x02B4)
#define CSEQ_EST_NEXUS_REQ_HEAD		(CMAPPEDSCR + 0x02B6)
#define CSEQ_EST_NEXUS_REQ_TAIL		(CMAPPEDSCR + 0x02B7)
#define CSEQ_EST_NEXUS_SCB_OFFSET	(CMAPPEDSCR + 0x02B8)

/* Mode independent scratch page 6 macros. */
#define CSEQ_INT_ROUT_RET_ADDR0		(CMAPPEDSCR + 0x02C0)
#define CSEQ_INT_ROUT_RET_ADDR1		(CMAPPEDSCR + 0x02C2)
#define CSEQ_INT_ROUT_SCBPTR		(CMAPPEDSCR + 0x02C4)
#define CSEQ_INT_ROUT_MODE		(CMAPPEDSCR + 0x02C6)
#define CSEQ_ISR_SCRATCH_FLAGS		(CMAPPEDSCR + 0x02C7)
#define CSEQ_ISR_SAVE_SINDEX		(CMAPPEDSCR + 0x02C8)
#define CSEQ_ISR_SAVE_DINDEX		(CMAPPEDSCR + 0x02CA)
#define CSEQ_Q_MONIRTT_HEAD		(CMAPPEDSCR + 0x02D0)
#define CSEQ_Q_MONIRTT_TAIL		(CMAPPEDSCR + 0x02D2)
#define CSEQ_FREE_SCB_MASK		(CMAPPEDSCR + 0x02D5)
#define CSEQ_BUILTIN_FREE_SCB_HEAD	(CMAPPEDSCR + 0x02D6)
#define CSEQ_BUILTIN_FREE_SCB_TAIL	(CMAPPEDSCR + 0x02D8)
#define CSEQ_EXTENDED_FREE_SCB_HEAD	(CMAPPEDSCR + 0x02DA)
#define CSEQ_EXTENDED_FREE_SCB_TAIL	(CMAPPEDSCR + 0x02DC)

/* Mode independent scratch page 7 macros. */
#define CSEQ_EMPTY_REQ_QUEUE		(CMAPPEDSCR + 0x02E0)
#define CSEQ_EMPTY_REQ_COUNT		(CMAPPEDSCR + 0x02E8)
#define CSEQ_Q_EMPTY_HEAD		(CMAPPEDSCR + 0x02F0)
#define CSEQ_Q_EMPTY_TAIL		(CMAPPEDSCR + 0x02F2)
#define CSEQ_NEED_EMPTY_SCB		(CMAPPEDSCR + 0x02F4)
#define CSEQ_EMPTY_REQ_HEAD		(CMAPPEDSCR + 0x02F6)
#define CSEQ_EMPTY_REQ_TAIL		(CMAPPEDSCR + 0x02F7)
#define CSEQ_EMPTY_SCB_OFFSET		(CMAPPEDSCR + 0x02F8)
#define CSEQ_PRIMITIVE_DATA		(CMAPPEDSCR + 0x02FA)
#define CSEQ_TIMEOUT_CONST		(CMAPPEDSCR + 0x02FC)

/***************************************************************************
* Link m Sequencer scratch RAM is 512 bytes.
* This scratch memory is divided into mode dependent and mode
* independent scratch with this memory further subdivided into
* pages of size 32 bytes. There are 4 pages (128 bytes) of
* mode independent scratch and 4 pages of dependent scratch
* memory for modes 0-2 (384 bytes).
*
* The host accesses this scratch in a different manner from the
* link sequencer. The sequencer has to use LSEQ registers
* LmSCRPAGE and LmMnSCRPAGE to access the scratch memory. A flat
* mapping of the scratch memory is avaliable for software
* convenience and to prevent corruption while the sequencer is
* running. This memory is mapped onto addresses 800h - 9FFh.
*
* These addresses are mapped as follows:
*
*        800h-85Fh   Mode Dependent Scratch Mode 0 Pages 0-2
*        860h-87Fh   Mode Dependent Scratch Mode 0 Page 3
*                    Mode Dependent Scratch Mode 5 Page 0
*        880h-8DFh   Mode Dependent Scratch Mode 1 Pages 0-2
*        8E0h-8FFh   Mode Dependent Scratch Mode 1 Page 3
*                    Mode Dependent Scratch Mode 5 Page 1
*        900h-95Fh   Mode Dependent Scratch Mode 2 Pages 0-2
*        960h-97Fh   Mode Dependent Scratch Mode 2 Page 3
*                    Mode Dependent Scratch Mode 5 Page 2
*        980h-9DFh   Mode Independent Scratch Pages 0-3
*        9E0h-9FFh   Mode Independent Scratch Page 3
*                    Mode Dependent Scratch Mode 5 Page 3
*
****************************************************************************/
/* General macros */
#define LSEQ_MODE_SCRATCH_SIZE		0x80 /* Size of scratch RAM per mode */
#define LSEQ_PAGE_SIZE			0x20 /* Scratch page size (in bytes) */
#define LSEQ_MODE5_PAGE0_OFFSET 	0x60

/* Common mode dependent scratch page 0 macros for modes 0,1,2, and 5 */
/* Indexed using LSEQ_MODE_SCRATCH_SIZE * mode, for modes 0,1,2. */
#define LmSEQ_RET_ADDR(LinkNum)		(LmSCRATCH(LinkNum) + 0x0000)
#define LmSEQ_REG0_MODE(LinkNum)	(LmSCRATCH(LinkNum) + 0x0002)
#define LmSEQ_MODE_FLAGS(LinkNum)	(LmSCRATCH(LinkNum) + 0x0004)

/* Mode flag macros (byte 0) */
#define		SAS_SAVECTX_OCCURRED		0x80
#define		SAS_OOBSVC_OCCURRED		0x40
#define		SAS_OOB_DEVICE_PRESENT		0x20
#define		SAS_CFGHDR_OCCURRED		0x10
#define		SAS_RCV_INTS_ARE_DISABLED	0x08
#define		SAS_OOB_HOT_PLUG_CNCT		0x04
#define		SAS_AWAIT_OPEN_CONNECTION	0x02
#define		SAS_CFGCMPLT_OCCURRED		0x01

/* Mode flag macros (byte 1) */
#define		SAS_RLSSCB_OCCURRED		0x80
#define		SAS_FORCED_HEADER_MISS		0x40

#define LmSEQ_RET_ADDR2(LinkNum)	(LmSCRATCH(LinkNum) + 0x0006)
#define LmSEQ_RET_ADDR1(LinkNum)	(LmSCRATCH(LinkNum) + 0x0008)
#define LmSEQ_OPCODE_TO_CSEQ(LinkNum)	(LmSCRATCH(LinkNum) + 0x000B)
#define LmSEQ_DATA_TO_CSEQ(LinkNum)	(LmSCRATCH(LinkNum) + 0x000C)

/* Mode dependent scratch page 0 macros for mode 0 (non-common) */
/* Absolute offsets */
#define LmSEQ_FIRST_INV_DDB_SITE(LinkNum)	(LmSCRATCH(LinkNum) + 0x000E)
#define LmSEQ_EMPTY_TRANS_CTX(LinkNum)		(LmSCRATCH(LinkNum) + 0x0010)
#define LmSEQ_RESP_LEN(LinkNum)			(LmSCRATCH(LinkNum) + 0x0012)
#define LmSEQ_FIRST_INV_SCB_SITE(LinkNum)	(LmSCRATCH(LinkNum) + 0x0014)
#define LmSEQ_INTEN_SAVE(LinkNum)		(LmSCRATCH(LinkNum) + 0x0016)
#define LmSEQ_LINK_RST_FRM_LEN(LinkNum)		(LmSCRATCH(LinkNum) + 0x001A)
#define LmSEQ_LINK_RST_PROTOCOL(LinkNum)	(LmSCRATCH(LinkNum) + 0x001B)
#define LmSEQ_RESP_STATUS(LinkNum)		(LmSCRATCH(LinkNum) + 0x001C)
#define LmSEQ_LAST_LOADED_SGE(LinkNum)		(LmSCRATCH(LinkNum) + 0x001D)
#define LmSEQ_SAVE_SCBPTR(LinkNum)		(LmSCRATCH(LinkNum) + 0x001E)

/* Mode dependent scratch page 0 macros for mode 1 (non-common) */
/* Absolute offsets */
#define LmSEQ_Q_XMIT_HEAD(LinkNum)		(LmSCRATCH(LinkNum) + 0x008E)
#define LmSEQ_M1_EMPTY_TRANS_CTX(LinkNum)	(LmSCRATCH(LinkNum) + 0x0090)
#define LmSEQ_INI_CONN_TAG(LinkNum)		(LmSCRATCH(LinkNum) + 0x0092)
#define LmSEQ_FAILED_OPEN_STATUS(LinkNum)	(LmSCRATCH(LinkNum) + 0x009A)
#define LmSEQ_XMIT_REQUEST_TYPE(LinkNum)	(LmSCRATCH(LinkNum) + 0x009B)
#define LmSEQ_M1_RESP_STATUS(LinkNum)		(LmSCRATCH(LinkNum) + 0x009C)
#define LmSEQ_M1_LAST_LOADED_SGE(LinkNum)	(LmSCRATCH(LinkNum) + 0x009D)
#define LmSEQ_M1_SAVE_SCBPTR(LinkNum)		(LmSCRATCH(LinkNum) + 0x009E)

/* Mode dependent scratch page 0 macros for mode 2 (non-common) */
#define LmSEQ_PORT_COUNTER(LinkNum)		(LmSCRATCH(LinkNum) + 0x010E)
#define LmSEQ_PM_TABLE_PTR(LinkNum)		(LmSCRATCH(LinkNum) + 0x0110)
#define LmSEQ_SATA_INTERLOCK_TMR_SAVE(LinkNum)	(LmSCRATCH(LinkNum) + 0x0112)
#define LmSEQ_IP_BITL(LinkNum)			(LmSCRATCH(LinkNum) + 0x0114)
#define LmSEQ_COPY_SMP_CONN_TAG(LinkNum)	(LmSCRATCH(LinkNum) + 0x0116)
#define LmSEQ_P0M2_OFFS1AH(LinkNum)		(LmSCRATCH(LinkNum) + 0x011A)

/* Mode dependent scratch page 0 macros for modes 4/5 (non-common) */
/* Absolute offsets */
#define LmSEQ_SAVED_OOB_STATUS(LinkNum)		(LmSCRATCH(LinkNum) + 0x006E)
#define LmSEQ_SAVED_OOB_MODE(LinkNum)		(LmSCRATCH(LinkNum) + 0x006F)
#define LmSEQ_Q_LINK_HEAD(LinkNum)		(LmSCRATCH(LinkNum) + 0x0070)
#define LmSEQ_LINK_RST_ERR(LinkNum)		(LmSCRATCH(LinkNum) + 0x0072)
#define LmSEQ_SAVED_OOB_SIGNALS(LinkNum)	(LmSCRATCH(LinkNum) + 0x0073)
#define LmSEQ_SAS_RESET_MODE(LinkNum)		(LmSCRATCH(LinkNum) + 0x0074)
#define LmSEQ_LINK_RESET_RETRY_COUNT(LinkNum)	(LmSCRATCH(LinkNum) + 0x0075)
#define LmSEQ_NUM_LINK_RESET_RETRIES(LinkNum)	(LmSCRATCH(LinkNum) + 0x0076)
#define LmSEQ_OOB_INT_ENABLES(LinkNum)		(LmSCRATCH(LinkNum) + 0x0078)
#define LmSEQ_NOTIFY_TIMER_DOWN_COUNT(LinkNum)	(LmSCRATCH(LinkNum) + 0x007A)
#define LmSEQ_NOTIFY_TIMER_TIMEOUT(LinkNum)	(LmSCRATCH(LinkNum) + 0x007C)
#define LmSEQ_NOTIFY_TIMER_INITIAL_COUNT(LinkNum) (LmSCRATCH(LinkNum) + 0x007E)

/* Mode dependent scratch page 1, mode 0 and mode 1 */
#define LmSEQ_SG_LIST_PTR_ADDR0(LinkNum)        (LmSCRATCH(LinkNum) + 0x0020)
#define LmSEQ_SG_LIST_PTR_ADDR1(LinkNum)        (LmSCRATCH(LinkNum) + 0x0030)
#define LmSEQ_M1_SG_LIST_PTR_ADDR0(LinkNum)     (LmSCRATCH(LinkNum) + 0x00A0)
#define LmSEQ_M1_SG_LIST_PTR_ADDR1(LinkNum)     (LmSCRATCH(LinkNum) + 0x00B0)

/* Mode dependent scratch page 1 macros for mode 2 */
/* Absolute offsets */
#define LmSEQ_INVALID_DWORD_COUNT(LinkNum)	(LmSCRATCH(LinkNum) + 0x0120)
#define LmSEQ_DISPARITY_ERROR_COUNT(LinkNum) 	(LmSCRATCH(LinkNum) + 0x0124)
#define LmSEQ_LOSS_OF_SYNC_COUNT(LinkNum)	(LmSCRATCH(LinkNum) + 0x0128)

/* Mode dependent scratch page 1 macros for mode 4/5 */
#define LmSEQ_FRAME_TYPE_MASK(LinkNum)	      (LmSCRATCH(LinkNum) + 0x00E0)
#define LmSEQ_HASHED_DEST_ADDR_MASK(LinkNum)  (LmSCRATCH(LinkNum) + 0x00E1)
#define LmSEQ_HASHED_SRC_ADDR_MASK_PRINT(LinkNum) (LmSCRATCH(LinkNum) + 0x00E4)
#define LmSEQ_HASHED_SRC_ADDR_MASK(LinkNum)   (LmSCRATCH(LinkNum) + 0x00E5)
#define LmSEQ_NUM_FILL_BYTES_MASK(LinkNum)    (LmSCRATCH(LinkNum) + 0x00EB)
#define LmSEQ_TAG_MASK(LinkNum)		      (LmSCRATCH(LinkNum) + 0x00F0)
#define LmSEQ_TARGET_PORT_XFER_TAG(LinkNum)   (LmSCRATCH(LinkNum) + 0x00F2)
#define LmSEQ_DATA_OFFSET(LinkNum)	      (LmSCRATCH(LinkNum) + 0x00F4)

/* Mode dependent scratch page 2 macros for mode 0 */
/* Absolute offsets */
#define LmSEQ_SMP_RCV_TIMER_TERM_TS(LinkNum)	(LmSCRATCH(LinkNum) + 0x0040)
#define LmSEQ_DEVICE_BITS(LinkNum)		(LmSCRATCH(LinkNum) + 0x005B)
#define LmSEQ_SDB_DDB(LinkNum)			(LmSCRATCH(LinkNum) + 0x005C)
#define LmSEQ_SDB_NUM_TAGS(LinkNum)		(LmSCRATCH(LinkNum) + 0x005E)
#define LmSEQ_SDB_CURR_TAG(LinkNum)		(LmSCRATCH(LinkNum) + 0x005F)

/* Mode dependent scratch page 2 macros for mode 1 */
/* Absolute offsets */
/* byte 0 bits 1-0 are domain select. */
#define LmSEQ_TX_ID_ADDR_FRAME(LinkNum)		(LmSCRATCH(LinkNum) + 0x00C0)
#define LmSEQ_OPEN_TIMER_TERM_TS(LinkNum)	(LmSCRATCH(LinkNum) + 0x00C8)
#define LmSEQ_SRST_AS_TIMER_TERM_TS(LinkNum)	(LmSCRATCH(LinkNum) + 0x00CC)
#define LmSEQ_LAST_LOADED_SG_EL(LinkNum)	(LmSCRATCH(LinkNum) + 0x00D4)

/* Mode dependent scratch page 2 macros for mode 2 */
/* Absolute offsets */
#define LmSEQ_STP_SHUTDOWN_TIMER_TERM_TS(LinkNum) (LmSCRATCH(LinkNum) + 0x0140)
#define LmSEQ_CLOSE_TIMER_TERM_TS(LinkNum)	(LmSCRATCH(LinkNum) + 0x0144)
#define LmSEQ_BREAK_TIMER_TERM_TS(LinkNum)	(LmSCRATCH(LinkNum) + 0x0148)
#define LmSEQ_DWS_RESET_TIMER_TERM_TS(LinkNum)	(LmSCRATCH(LinkNum) + 0x014C)
#define LmSEQ_SATA_INTERLOCK_TIMER_TERM_TS(LinkNum) \
						(LmSCRATCH(LinkNum) + 0x0150)
#define LmSEQ_MCTL_TIMER_TERM_TS(LinkNum)	(LmSCRATCH(LinkNum) + 0x0154)

/* Mode dependent scratch page 2 macros for mode 5 */
#define LmSEQ_COMINIT_TIMER_TERM_TS(LinkNum)	(LmSCRATCH(LinkNum) + 0x0160)
#define LmSEQ_RCV_ID_TIMER_TERM_TS(LinkNum)	(LmSCRATCH(LinkNum) + 0x0164)
#define LmSEQ_RCV_FIS_TIMER_TERM_TS(LinkNum)	(LmSCRATCH(LinkNum) + 0x0168)
#define LmSEQ_DEV_PRES_TIMER_TERM_TS(LinkNum)	(LmSCRATCH(LinkNum) + 0x016C)

/* Mode dependent scratch page 3 macros for modes 0 and 1 */
/* None defined */

/* Mode dependent scratch page 3 macros for modes 2 and 5 */
/* None defined */

/* Mode Independent Scratch page 0 macros. */
#define LmSEQ_Q_TGTXFR_HEAD(LinkNum)	(LmSCRATCH(LinkNum) + 0x0180)
#define LmSEQ_Q_TGTXFR_TAIL(LinkNum)	(LmSCRATCH(LinkNum) + 0x0182)
#define LmSEQ_LINK_NUMBER(LinkNum)	(LmSCRATCH(LinkNum) + 0x0186)
#define LmSEQ_SCRATCH_FLAGS(LinkNum)	(LmSCRATCH(LinkNum) + 0x0187)
/*
 * Currently only bit 0, SAS_DWSAQD, is used.
 */
#define		SAS_DWSAQD			0x01  /*
						       * DWSSTATUS: DWSAQD
						       * bit las read in ISR.
						       */
#define  LmSEQ_CONNECTION_STATE(LinkNum) (LmSCRATCH(LinkNum) + 0x0188)
/* Connection states (byte 0) */
#define		SAS_WE_OPENED_CS		0x01
#define		SAS_DEVICE_OPENED_CS		0x02
#define		SAS_WE_SENT_DONE_CS		0x04
#define		SAS_DEVICE_SENT_DONE_CS		0x08
#define		SAS_WE_SENT_CLOSE_CS		0x10
#define		SAS_DEVICE_SENT_CLOSE_CS	0x20
#define		SAS_WE_SENT_BREAK_CS		0x40
#define		SAS_DEVICE_SENT_BREAK_CS	0x80
/* Connection states (byte 1) */
#define		SAS_OPN_TIMEOUT_OR_OPN_RJCT_CS	0x01
#define		SAS_AIP_RECEIVED_CS		0x02
#define		SAS_CREDIT_TIMEOUT_OCCURRED_CS	0x04
#define		SAS_ACKNAK_TIMEOUT_OCCURRED_CS	0x08
#define		SAS_SMPRSP_TIMEOUT_OCCURRED_CS	0x10
#define		SAS_DONE_TIMEOUT_OCCURRED_CS	0x20
/* Connection states (byte 2) */
#define		SAS_SMP_RESPONSE_RECEIVED_CS	0x01
#define		SAS_INTLK_TIMEOUT_OCCURRED_CS	0x02
#define		SAS_DEVICE_SENT_DMAT_CS		0x04
#define		SAS_DEVICE_SENT_SYNCSRST_CS	0x08
#define		SAS_CLEARING_AFFILIATION_CS	0x20
#define		SAS_RXTASK_ACTIVE_CS		0x40
#define		SAS_TXTASK_ACTIVE_CS		0x80
/* Connection states (byte 3) */
#define		SAS_PHY_LOSS_OF_SIGNAL_CS	0x01
#define		SAS_DWS_TIMER_EXPIRED_CS	0x02
#define		SAS_LINK_RESET_NOT_COMPLETE_CS	0x04
#define		SAS_PHY_DISABLED_CS		0x08
#define		SAS_LINK_CTL_TASK_ACTIVE_CS	0x10
#define		SAS_PHY_EVENT_TASK_ACTIVE_CS	0x20
#define		SAS_DEVICE_SENT_ID_FRAME_CS	0x40
#define		SAS_DEVICE_SENT_REG_FIS_CS	0x40
#define		SAS_DEVICE_SENT_HARD_RESET_CS	0x80
#define  	SAS_PHY_IS_DOWN_FLAGS	(SAS_PHY_LOSS_OF_SIGNAL_CS|\
					 SAS_DWS_TIMER_EXPIRED_CS |\
					 SAS_LINK_RESET_NOT_COMPLETE_CS|\
					 SAS_PHY_DISABLED_CS)

#define		SAS_LINK_CTL_PHY_EVENT_FLAGS   (SAS_LINK_CTL_TASK_ACTIVE_CS |\
						SAS_PHY_EVENT_TASK_ACTIVE_CS |\
						SAS_DEVICE_SENT_ID_FRAME_CS  |\
						SAS_DEVICE_SENT_HARD_RESET_CS)

#define LmSEQ_CONCTL(LinkNum)		(LmSCRATCH(LinkNum) + 0x018C)
#define LmSEQ_CONSTAT(LinkNum)		(LmSCRATCH(LinkNum) + 0x018E)
#define LmSEQ_CONNECTION_MODES(LinkNum)	(LmSCRATCH(LinkNum) + 0x018F)
#define LmSEQ_REG1_ISR(LinkNum)		(LmSCRATCH(LinkNum) + 0x0192)
#define LmSEQ_REG2_ISR(LinkNum)		(LmSCRATCH(LinkNum) + 0x0194)
#define LmSEQ_REG3_ISR(LinkNum)		(LmSCRATCH(LinkNum) + 0x0196)
#define LmSEQ_REG0_ISR(LinkNum)		(LmSCRATCH(LinkNum) + 0x0198)

/* Mode independent scratch page 1 macros. */
#define LmSEQ_EST_NEXUS_SCBPTR0(LinkNum)	(LmSCRATCH(LinkNum) + 0x01A0)
#define LmSEQ_EST_NEXUS_SCBPTR1(LinkNum)	(LmSCRATCH(LinkNum) + 0x01A2)
#define LmSEQ_EST_NEXUS_SCBPTR2(LinkNum)	(LmSCRATCH(LinkNum) + 0x01A4)
#define LmSEQ_EST_NEXUS_SCBPTR3(LinkNum)	(LmSCRATCH(LinkNum) + 0x01A6)
#define LmSEQ_EST_NEXUS_SCB_OPCODE0(LinkNum)	(LmSCRATCH(LinkNum) + 0x01A8)
#define LmSEQ_EST_NEXUS_SCB_OPCODE1(LinkNum)	(LmSCRATCH(LinkNum) + 0x01A9)
#define LmSEQ_EST_NEXUS_SCB_OPCODE2(LinkNum)	(LmSCRATCH(LinkNum) + 0x01AA)
#define LmSEQ_EST_NEXUS_SCB_OPCODE3(LinkNum)	(LmSCRATCH(LinkNum) + 0x01AB)
#define LmSEQ_EST_NEXUS_SCB_HEAD(LinkNum)	(LmSCRATCH(LinkNum) + 0x01AC)
#define LmSEQ_EST_NEXUS_SCB_TAIL(LinkNum)	(LmSCRATCH(LinkNum) + 0x01AD)
#define LmSEQ_EST_NEXUS_BUF_AVAIL(LinkNum)	(LmSCRATCH(LinkNum) + 0x01AE)
#define LmSEQ_TIMEOUT_CONST(LinkNum)		(LmSCRATCH(LinkNum) + 0x01B8)
#define LmSEQ_ISR_SAVE_SINDEX(LinkNum)	        (LmSCRATCH(LinkNum) + 0x01BC)
#define LmSEQ_ISR_SAVE_DINDEX(LinkNum)	        (LmSCRATCH(LinkNum) + 0x01BE)

/* Mode independent scratch page 2 macros. */
#define LmSEQ_EMPTY_SCB_PTR0(LinkNum)	(LmSCRATCH(LinkNum) + 0x01C0)
#define LmSEQ_EMPTY_SCB_PTR1(LinkNum)	(LmSCRATCH(LinkNum) + 0x01C2)
#define LmSEQ_EMPTY_SCB_PTR2(LinkNum)	(LmSCRATCH(LinkNum) + 0x01C4)
#define LmSEQ_EMPTY_SCB_PTR3(LinkNum)	(LmSCRATCH(LinkNum) + 0x01C6)
#define LmSEQ_EMPTY_SCB_OPCD0(LinkNum)	(LmSCRATCH(LinkNum) + 0x01C8)
#define LmSEQ_EMPTY_SCB_OPCD1(LinkNum)	(LmSCRATCH(LinkNum) + 0x01C9)
#define LmSEQ_EMPTY_SCB_OPCD2(LinkNum)	(LmSCRATCH(LinkNum) + 0x01CA)
#define LmSEQ_EMPTY_SCB_OPCD3(LinkNum)	(LmSCRATCH(LinkNum) + 0x01CB)
#define LmSEQ_EMPTY_SCB_HEAD(LinkNum)	(LmSCRATCH(LinkNum) + 0x01CC)
#define LmSEQ_EMPTY_SCB_TAIL(LinkNum)	(LmSCRATCH(LinkNum) + 0x01CD)
#define LmSEQ_EMPTY_BUFS_AVAIL(LinkNum)	(LmSCRATCH(LinkNum) + 0x01CE)
#define LmSEQ_ATA_SCR_REGS(LinkNum)	(LmSCRATCH(LinkNum) + 0x01D4)

/* Mode independent scratch page 3 macros. */
#define LmSEQ_DEV_PRES_TMR_TOUT_CONST(LinkNum)	(LmSCRATCH(LinkNum) + 0x01E0)
#define LmSEQ_SATA_INTERLOCK_TIMEOUT(LinkNum)	(LmSCRATCH(LinkNum) + 0x01E4)
#define LmSEQ_STP_SHUTDOWN_TIMEOUT(LinkNum)	(LmSCRATCH(LinkNum) + 0x01E8)
#define LmSEQ_SRST_ASSERT_TIMEOUT(LinkNum)	(LmSCRATCH(LinkNum) + 0x01EC)
#define LmSEQ_RCV_FIS_TIMEOUT(LinkNum)		(LmSCRATCH(LinkNum) + 0x01F0)
#define LmSEQ_ONE_MILLISEC_TIMEOUT(LinkNum)	(LmSCRATCH(LinkNum) + 0x01F4)
#define LmSEQ_TEN_MS_COMINIT_TIMEOUT(LinkNum)	(LmSCRATCH(LinkNum) + 0x01F8)
#define LmSEQ_SMP_RCV_TIMEOUT(LinkNum)		(LmSCRATCH(LinkNum) + 0x01FC)

#endif
