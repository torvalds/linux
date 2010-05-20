/*
 * Generic Broadcom Home Networking Division (HND) DMA engine HW interface
 * This supports the following chips: BCM42xx, 44xx, 47xx .
 *
 * Copyright (C) 1999-2009, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: sbhnddma.h,v 13.11.250.5.16.1 2009/07/21 14:04:51 Exp $
 */


#ifndef	_sbhnddma_h_
#define	_sbhnddma_h_







typedef volatile struct {
	uint32	control;		
	uint32	addr;			
	uint32	ptr;			
	uint32	status;			
} dma32regs_t;

typedef volatile struct {
	dma32regs_t	xmt;		
	dma32regs_t	rcv;		
} dma32regp_t;

typedef volatile struct {	
	uint32	fifoaddr;		
	uint32	fifodatalow;		
	uint32	fifodatahigh;		
	uint32	pad;			
} dma32diag_t;


typedef volatile struct {
	uint32	ctrl;		
	uint32	addr;		
} dma32dd_t;


#define	D32RINGALIGN_BITS	12
#define	D32MAXRINGSZ	(1 << D32RINGALIGN_BITS)
#define	D32RINGALIGN	(1 << D32RINGALIGN_BITS)
#define	D32MAXDD	(D32MAXRINGSZ / sizeof (dma32dd_t))


#define	XC_XE		((uint32)1 << 0)	
#define	XC_SE		((uint32)1 << 1)	
#define	XC_LE		((uint32)1 << 2)	
#define	XC_FL		((uint32)1 << 4)	
#define	XC_PD		((uint32)1 << 11)	
#define	XC_AE		((uint32)3 << 16)	
#define	XC_AE_SHIFT	16


#define	XP_LD_MASK	0xfff			


#define	XS_CD_MASK	0x0fff			
#define	XS_XS_MASK	0xf000			
#define	XS_XS_SHIFT	12
#define	XS_XS_DISABLED	0x0000			
#define	XS_XS_ACTIVE	0x1000			
#define	XS_XS_IDLE	0x2000			
#define	XS_XS_STOPPED	0x3000			
#define	XS_XS_SUSP	0x4000			
#define	XS_XE_MASK	0xf0000			
#define	XS_XE_SHIFT	16
#define	XS_XE_NOERR	0x00000			
#define	XS_XE_DPE	0x10000			
#define	XS_XE_DFU	0x20000			
#define	XS_XE_BEBR	0x30000			
#define	XS_XE_BEDA	0x40000			
#define	XS_AD_MASK	0xfff00000		
#define	XS_AD_SHIFT	20


#define	RC_RE		((uint32)1 << 0)	
#define	RC_RO_MASK	0xfe			
#define	RC_RO_SHIFT	1
#define	RC_FM		((uint32)1 << 8)	
#define	RC_SH		((uint32)1 << 9)	
#define	RC_OC		((uint32)1 << 10)	
#define	RC_PD		((uint32)1 << 11)	
#define	RC_AE		((uint32)3 << 16)	
#define	RC_AE_SHIFT	16


#define	RP_LD_MASK	0xfff			


#define	RS_CD_MASK	0x0fff			
#define	RS_RS_MASK	0xf000			
#define	RS_RS_SHIFT	12
#define	RS_RS_DISABLED	0x0000			
#define	RS_RS_ACTIVE	0x1000			
#define	RS_RS_IDLE	0x2000			
#define	RS_RS_STOPPED	0x3000			
#define	RS_RE_MASK	0xf0000			
#define	RS_RE_SHIFT	16
#define	RS_RE_NOERR	0x00000			
#define	RS_RE_DPE	0x10000			
#define	RS_RE_DFO	0x20000			
#define	RS_RE_BEBW	0x30000			
#define	RS_RE_BEDA	0x40000			
#define	RS_AD_MASK	0xfff00000		
#define	RS_AD_SHIFT	20


#define	FA_OFF_MASK	0xffff			
#define	FA_SEL_MASK	0xf0000			
#define	FA_SEL_SHIFT	16
#define	FA_SEL_XDD	0x00000			
#define	FA_SEL_XDP	0x10000			
#define	FA_SEL_RDD	0x40000			
#define	FA_SEL_RDP	0x50000			
#define	FA_SEL_XFD	0x80000			
#define	FA_SEL_XFP	0x90000			
#define	FA_SEL_RFD	0xc0000			
#define	FA_SEL_RFP	0xd0000			
#define	FA_SEL_RSD	0xe0000			
#define	FA_SEL_RSP	0xf0000			


#define	CTRL_BC_MASK	0x1fff			
#define	CTRL_AE		((uint32)3 << 16)	
#define	CTRL_AE_SHIFT	16
#define	CTRL_EOT	((uint32)1 << 28)	
#define	CTRL_IOC	((uint32)1 << 29)	
#define	CTRL_EOF	((uint32)1 << 30)	
#define	CTRL_SOF	((uint32)1 << 31)	


#define	CTRL_CORE_MASK	0x0ff00000




typedef volatile struct {
	uint32	control;		
	uint32	ptr;			
	uint32	addrlow;		
	uint32	addrhigh;		
	uint32	status0;		
	uint32	status1;		
} dma64regs_t;

typedef volatile struct {
	dma64regs_t	tx;		
	dma64regs_t	rx;		
} dma64regp_t;

typedef volatile struct {		
	uint32	fifoaddr;		
	uint32	fifodatalow;		
	uint32	fifodatahigh;		
	uint32	pad;			
} dma64diag_t;


typedef volatile struct {
	uint32	ctrl1;		
	uint32	ctrl2;		
	uint32	addrlow;	
	uint32	addrhigh;	
} dma64dd_t;


#define D64RINGALIGN_BITS 13	
#define	D64MAXRINGSZ	(1 << D64RINGALIGN_BITS)
#define	D64RINGALIGN	(1 << D64RINGALIGN_BITS)
#define	D64MAXDD	(D64MAXRINGSZ / sizeof (dma64dd_t))


#define	D64_XC_XE		0x00000001	
#define	D64_XC_SE		0x00000002	
#define	D64_XC_LE		0x00000004	
#define	D64_XC_FL		0x00000010	
#define	D64_XC_PD		0x00000800	
#define	D64_XC_AE		0x00030000	
#define	D64_XC_AE_SHIFT		16


#define	D64_XP_LD_MASK		0x00000fff	


#define	D64_XS0_CD_MASK		0x00001fff	
#define	D64_XS0_XS_MASK		0xf0000000     	
#define	D64_XS0_XS_SHIFT		28
#define	D64_XS0_XS_DISABLED	0x00000000	
#define	D64_XS0_XS_ACTIVE	0x10000000	
#define	D64_XS0_XS_IDLE		0x20000000	
#define	D64_XS0_XS_STOPPED	0x30000000	
#define	D64_XS0_XS_SUSP		0x40000000	

#define	D64_XS1_AD_MASK		0x0001ffff	
#define	D64_XS1_XE_MASK		0xf0000000     	
#define	D64_XS1_XE_SHIFT		28
#define	D64_XS1_XE_NOERR	0x00000000	
#define	D64_XS1_XE_DPE		0x10000000	
#define	D64_XS1_XE_DFU		0x20000000	
#define	D64_XS1_XE_DTE		0x30000000	
#define	D64_XS1_XE_DESRE	0x40000000	
#define	D64_XS1_XE_COREE	0x50000000	


#define	D64_RC_RE		0x00000001	
#define	D64_RC_RO_MASK		0x000000fe	
#define	D64_RC_RO_SHIFT		1
#define	D64_RC_FM		0x00000100	
#define	D64_RC_SH		0x00000200	
#define	D64_RC_OC		0x00000400	
#define	D64_RC_PD		0x00000800	
#define	D64_RC_AE		0x00030000	
#define	D64_RC_AE_SHIFT		16


#define	D64_RP_LD_MASK		0x00000fff	


#define	D64_RS0_CD_MASK		0x00001fff	
#define	D64_RS0_RS_MASK		0xf0000000     	
#define	D64_RS0_RS_SHIFT		28
#define	D64_RS0_RS_DISABLED	0x00000000	
#define	D64_RS0_RS_ACTIVE	0x10000000	
#define	D64_RS0_RS_IDLE		0x20000000	
#define	D64_RS0_RS_STOPPED	0x30000000	
#define	D64_RS0_RS_SUSP		0x40000000	

#define	D64_RS1_AD_MASK		0x0001ffff	
#define	D64_RS1_RE_MASK		0xf0000000     	
#define	D64_RS1_RE_SHIFT		28
#define	D64_RS1_RE_NOERR	0x00000000	
#define	D64_RS1_RE_DPO		0x10000000	
#define	D64_RS1_RE_DFU		0x20000000	
#define	D64_RS1_RE_DTE		0x30000000	
#define	D64_RS1_RE_DESRE	0x40000000	
#define	D64_RS1_RE_COREE	0x50000000	


#define	D64_FA_OFF_MASK		0xffff		
#define	D64_FA_SEL_MASK		0xf0000		
#define	D64_FA_SEL_SHIFT	16
#define	D64_FA_SEL_XDD		0x00000		
#define	D64_FA_SEL_XDP		0x10000		
#define	D64_FA_SEL_RDD		0x40000		
#define	D64_FA_SEL_RDP		0x50000		
#define	D64_FA_SEL_XFD		0x80000		
#define	D64_FA_SEL_XFP		0x90000		
#define	D64_FA_SEL_RFD		0xc0000		
#define	D64_FA_SEL_RFP		0xd0000		
#define	D64_FA_SEL_RSD		0xe0000		
#define	D64_FA_SEL_RSP		0xf0000		


#define	D64_CTRL1_EOT		((uint32)1 << 28)	
#define	D64_CTRL1_IOC		((uint32)1 << 29)	
#define	D64_CTRL1_EOF		((uint32)1 << 30)	
#define	D64_CTRL1_SOF		((uint32)1 << 31)	


#define	D64_CTRL2_BC_MASK	0x00007fff	
#define	D64_CTRL2_AE		0x00030000	
#define	D64_CTRL2_AE_SHIFT	16
#define D64_CTRL2_PARITY	0x00040000      


#define	D64_CTRL_CORE_MASK	0x0ff00000


#endif	
