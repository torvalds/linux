/*
 *  SuperH Ethernet device driver
 *
 *  Copyright (C) 2006-2012 Nobuhiro Iwamatsu
 *  Copyright (C) 2008-2012 Renesas Solutions Corp.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms and conditions of the GNU General Public License,
 *  version 2, as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *  The full GNU General Public License is included in this distribution in
 *  the file called "COPYING".
 */

#ifndef __SH_ETH_H__
#define __SH_ETH_H__

#define CARDNAME	"sh-eth"
#define TX_TIMEOUT	(5*HZ)
#define TX_RING_SIZE	64	/* Tx ring size */
#define RX_RING_SIZE	64	/* Rx ring size */
#define ETHERSMALL		60
#define PKT_BUF_SZ		1538
#define SH_ETH_TSU_TIMEOUT_MS	500
#define SH_ETH_TSU_CAM_ENTRIES	32

enum {
	/* E-DMAC registers */
	EDSR = 0,
	EDMR,
	EDTRR,
	EDRRR,
	EESR,
	EESIPR,
	TDLAR,
	TDFAR,
	TDFXR,
	TDFFR,
	RDLAR,
	RDFAR,
	RDFXR,
	RDFFR,
	TRSCER,
	RMFCR,
	TFTR,
	FDR,
	RMCR,
	EDOCR,
	TFUCR,
	RFOCR,
	FCFTR,
	RPADIR,
	TRIMD,
	RBWAR,
	TBRAR,

	/* Ether registers */
	ECMR,
	ECSR,
	ECSIPR,
	PIR,
	PSR,
	RDMLR,
	PIPR,
	RFLR,
	IPGR,
	APR,
	MPR,
	PFTCR,
	PFRCR,
	RFCR,
	RFCF,
	TPAUSER,
	TPAUSECR,
	BCFR,
	BCFRR,
	GECMR,
	BCULR,
	MAHR,
	MALR,
	TROCR,
	CDCR,
	LCCR,
	CNDCR,
	CEFCR,
	FRECR,
	TSFRCR,
	TLFRCR,
	CERCR,
	CEECR,
	MAFCR,
	RTRATE,
	CSMR,
	RMII_MII,

	/* TSU Absolute address */
	ARSTR,
	TSU_CTRST,
	TSU_FWEN0,
	TSU_FWEN1,
	TSU_FCM,
	TSU_BSYSL0,
	TSU_BSYSL1,
	TSU_PRISL0,
	TSU_PRISL1,
	TSU_FWSL0,
	TSU_FWSL1,
	TSU_FWSLC,
	TSU_QTAG0,
	TSU_QTAG1,
	TSU_QTAGM0,
	TSU_QTAGM1,
	TSU_FWSR,
	TSU_FWINMK,
	TSU_ADQT0,
	TSU_ADQT1,
	TSU_VTAG0,
	TSU_VTAG1,
	TSU_ADSBSY,
	TSU_TEN,
	TSU_POST1,
	TSU_POST2,
	TSU_POST3,
	TSU_POST4,
	TSU_ADRH0,
	TSU_ADRL0,
	TSU_ADRH31,
	TSU_ADRL31,

	TXNLCR0,
	TXALCR0,
	RXNLCR0,
	RXALCR0,
	FWNLCR0,
	FWALCR0,
	TXNLCR1,
	TXALCR1,
	RXNLCR1,
	RXALCR1,
	FWNLCR1,
	FWALCR1,

	/* This value must be written at last. */
	SH_ETH_MAX_REGISTER_OFFSET,
};

static const u16 sh_eth_offset_gigabit[SH_ETH_MAX_REGISTER_OFFSET] = {
	[EDSR]	= 0x0000,
	[EDMR]	= 0x0400,
	[EDTRR]	= 0x0408,
	[EDRRR]	= 0x0410,
	[EESR]	= 0x0428,
	[EESIPR]	= 0x0430,
	[TDLAR]	= 0x0010,
	[TDFAR]	= 0x0014,
	[TDFXR]	= 0x0018,
	[TDFFR]	= 0x001c,
	[RDLAR]	= 0x0030,
	[RDFAR]	= 0x0034,
	[RDFXR]	= 0x0038,
	[RDFFR]	= 0x003c,
	[TRSCER]	= 0x0438,
	[RMFCR]	= 0x0440,
	[TFTR]	= 0x0448,
	[FDR]	= 0x0450,
	[RMCR]	= 0x0458,
	[RPADIR]	= 0x0460,
	[FCFTR]	= 0x0468,
	[CSMR] = 0x04E4,

	[ECMR]	= 0x0500,
	[ECSR]	= 0x0510,
	[ECSIPR]	= 0x0518,
	[PIR]	= 0x0520,
	[PSR]	= 0x0528,
	[PIPR]	= 0x052c,
	[RFLR]	= 0x0508,
	[APR]	= 0x0554,
	[MPR]	= 0x0558,
	[PFTCR]	= 0x055c,
	[PFRCR]	= 0x0560,
	[TPAUSER]	= 0x0564,
	[GECMR]	= 0x05b0,
	[BCULR]	= 0x05b4,
	[MAHR]	= 0x05c0,
	[MALR]	= 0x05c8,
	[TROCR]	= 0x0700,
	[CDCR]	= 0x0708,
	[LCCR]	= 0x0710,
	[CEFCR]	= 0x0740,
	[FRECR]	= 0x0748,
	[TSFRCR]	= 0x0750,
	[TLFRCR]	= 0x0758,
	[RFCR]	= 0x0760,
	[CERCR]	= 0x0768,
	[CEECR]	= 0x0770,
	[MAFCR]	= 0x0778,
	[RMII_MII] =  0x0790,

	[ARSTR]	= 0x0000,
	[TSU_CTRST]	= 0x0004,
	[TSU_FWEN0]	= 0x0010,
	[TSU_FWEN1]	= 0x0014,
	[TSU_FCM]	= 0x0018,
	[TSU_BSYSL0]	= 0x0020,
	[TSU_BSYSL1]	= 0x0024,
	[TSU_PRISL0]	= 0x0028,
	[TSU_PRISL1]	= 0x002c,
	[TSU_FWSL0]	= 0x0030,
	[TSU_FWSL1]	= 0x0034,
	[TSU_FWSLC]	= 0x0038,
	[TSU_QTAG0]	= 0x0040,
	[TSU_QTAG1]	= 0x0044,
	[TSU_FWSR]	= 0x0050,
	[TSU_FWINMK]	= 0x0054,
	[TSU_ADQT0]	= 0x0048,
	[TSU_ADQT1]	= 0x004c,
	[TSU_VTAG0]	= 0x0058,
	[TSU_VTAG1]	= 0x005c,
	[TSU_ADSBSY]	= 0x0060,
	[TSU_TEN]	= 0x0064,
	[TSU_POST1]	= 0x0070,
	[TSU_POST2]	= 0x0074,
	[TSU_POST3]	= 0x0078,
	[TSU_POST4]	= 0x007c,
	[TSU_ADRH0]	= 0x0100,
	[TSU_ADRL0]	= 0x0104,
	[TSU_ADRH31]	= 0x01f8,
	[TSU_ADRL31]	= 0x01fc,

	[TXNLCR0]	= 0x0080,
	[TXALCR0]	= 0x0084,
	[RXNLCR0]	= 0x0088,
	[RXALCR0]	= 0x008c,
	[FWNLCR0]	= 0x0090,
	[FWALCR0]	= 0x0094,
	[TXNLCR1]	= 0x00a0,
	[TXALCR1]	= 0x00a0,
	[RXNLCR1]	= 0x00a8,
	[RXALCR1]	= 0x00ac,
	[FWNLCR1]	= 0x00b0,
	[FWALCR1]	= 0x00b4,
};

static const u16 sh_eth_offset_fast_sh4[SH_ETH_MAX_REGISTER_OFFSET] = {
	[ECMR]	= 0x0100,
	[RFLR]	= 0x0108,
	[ECSR]	= 0x0110,
	[ECSIPR]	= 0x0118,
	[PIR]	= 0x0120,
	[PSR]	= 0x0128,
	[RDMLR]	= 0x0140,
	[IPGR]	= 0x0150,
	[APR]	= 0x0154,
	[MPR]	= 0x0158,
	[TPAUSER]	= 0x0164,
	[RFCF]	= 0x0160,
	[TPAUSECR]	= 0x0168,
	[BCFRR]	= 0x016c,
	[MAHR]	= 0x01c0,
	[MALR]	= 0x01c8,
	[TROCR]	= 0x01d0,
	[CDCR]	= 0x01d4,
	[LCCR]	= 0x01d8,
	[CNDCR]	= 0x01dc,
	[CEFCR]	= 0x01e4,
	[FRECR]	= 0x01e8,
	[TSFRCR]	= 0x01ec,
	[TLFRCR]	= 0x01f0,
	[RFCR]	= 0x01f4,
	[MAFCR]	= 0x01f8,
	[RTRATE]	= 0x01fc,

	[EDMR]	= 0x0000,
	[EDTRR]	= 0x0008,
	[EDRRR]	= 0x0010,
	[TDLAR]	= 0x0018,
	[RDLAR]	= 0x0020,
	[EESR]	= 0x0028,
	[EESIPR]	= 0x0030,
	[TRSCER]	= 0x0038,
	[RMFCR]	= 0x0040,
	[TFTR]	= 0x0048,
	[FDR]	= 0x0050,
	[RMCR]	= 0x0058,
	[TFUCR]	= 0x0064,
	[RFOCR]	= 0x0068,
	[FCFTR]	= 0x0070,
	[RPADIR]	= 0x0078,
	[TRIMD]	= 0x007c,
	[RBWAR]	= 0x00c8,
	[RDFAR]	= 0x00cc,
	[TBRAR]	= 0x00d4,
	[TDFAR]	= 0x00d8,
};

static const u16 sh_eth_offset_fast_sh3_sh2[SH_ETH_MAX_REGISTER_OFFSET] = {
	[ECMR]	= 0x0160,
	[ECSR]	= 0x0164,
	[ECSIPR]	= 0x0168,
	[PIR]	= 0x016c,
	[MAHR]	= 0x0170,
	[MALR]	= 0x0174,
	[RFLR]	= 0x0178,
	[PSR]	= 0x017c,
	[TROCR]	= 0x0180,
	[CDCR]	= 0x0184,
	[LCCR]	= 0x0188,
	[CNDCR]	= 0x018c,
	[CEFCR]	= 0x0194,
	[FRECR]	= 0x0198,
	[TSFRCR]	= 0x019c,
	[TLFRCR]	= 0x01a0,
	[RFCR]	= 0x01a4,
	[MAFCR]	= 0x01a8,
	[IPGR]	= 0x01b4,
	[APR]	= 0x01b8,
	[MPR]	= 0x01bc,
	[TPAUSER]	= 0x01c4,
	[BCFR]	= 0x01cc,

	[ARSTR]	= 0x0000,
	[TSU_CTRST]	= 0x0004,
	[TSU_FWEN0]	= 0x0010,
	[TSU_FWEN1]	= 0x0014,
	[TSU_FCM]	= 0x0018,
	[TSU_BSYSL0]	= 0x0020,
	[TSU_BSYSL1]	= 0x0024,
	[TSU_PRISL0]	= 0x0028,
	[TSU_PRISL1]	= 0x002c,
	[TSU_FWSL0]	= 0x0030,
	[TSU_FWSL1]	= 0x0034,
	[TSU_FWSLC]	= 0x0038,
	[TSU_QTAGM0]	= 0x0040,
	[TSU_QTAGM1]	= 0x0044,
	[TSU_ADQT0]	= 0x0048,
	[TSU_ADQT1]	= 0x004c,
	[TSU_FWSR]	= 0x0050,
	[TSU_FWINMK]	= 0x0054,
	[TSU_ADSBSY]	= 0x0060,
	[TSU_TEN]	= 0x0064,
	[TSU_POST1]	= 0x0070,
	[TSU_POST2]	= 0x0074,
	[TSU_POST3]	= 0x0078,
	[TSU_POST4]	= 0x007c,

	[TXNLCR0]	= 0x0080,
	[TXALCR0]	= 0x0084,
	[RXNLCR0]	= 0x0088,
	[RXALCR0]	= 0x008c,
	[FWNLCR0]	= 0x0090,
	[FWALCR0]	= 0x0094,
	[TXNLCR1]	= 0x00a0,
	[TXALCR1]	= 0x00a0,
	[RXNLCR1]	= 0x00a8,
	[RXALCR1]	= 0x00ac,
	[FWNLCR1]	= 0x00b0,
	[FWALCR1]	= 0x00b4,

	[TSU_ADRH0]	= 0x0100,
	[TSU_ADRL0]	= 0x0104,
	[TSU_ADRL31]	= 0x01fc,

};

/* Driver's parameters */
#if defined(CONFIG_CPU_SH4) || defined(CONFIG_ARCH_SHMOBILE)
#define SH4_SKB_RX_ALIGN	32
#else
#define SH2_SH3_SKB_RX_ALIGN	2
#endif

/*
 * Register's bits
 */
#if defined(CONFIG_CPU_SUBTYPE_SH7734) || defined(CONFIG_CPU_SUBTYPE_SH7763) ||\
    defined(CONFIG_ARCH_R8A7740)
/* EDSR */
enum EDSR_BIT {
	EDSR_ENT = 0x01, EDSR_ENR = 0x02,
};
#define EDSR_ENALL (EDSR_ENT|EDSR_ENR)

/* GECMR */
enum GECMR_BIT {
	GECMR_10 = 0x0, GECMR_100 = 0x04, GECMR_1000 = 0x01,
};
#endif

/* EDMR */
enum DMAC_M_BIT {
	EDMR_EL = 0x40, /* Litte endian */
	EDMR_DL1 = 0x20, EDMR_DL0 = 0x10,
	EDMR_SRST_GETHER = 0x03,
	EDMR_SRST_ETHER = 0x01,
};

/* EDTRR */
enum DMAC_T_BIT {
	EDTRR_TRNS_GETHER = 0x03,
	EDTRR_TRNS_ETHER = 0x01,
};

/* EDRRR*/
enum EDRRR_R_BIT {
	EDRRR_R = 0x01,
};

/* TPAUSER */
enum TPAUSER_BIT {
	TPAUSER_TPAUSE = 0x0000ffff,
	TPAUSER_UNLIMITED = 0,
};

/* BCFR */
enum BCFR_BIT {
	BCFR_RPAUSE = 0x0000ffff,
	BCFR_UNLIMITED = 0,
};

/* PIR */
enum PIR_BIT {
	PIR_MDI = 0x08, PIR_MDO = 0x04, PIR_MMD = 0x02, PIR_MDC = 0x01,
};

/* PSR */
enum PHY_STATUS_BIT { PHY_ST_LINK = 0x01, };

/* EESR */
enum EESR_BIT {
	EESR_TWB1	= 0x80000000,
	EESR_TWB	= 0x40000000,	/* same as TWB0 */
	EESR_TC1	= 0x20000000,
	EESR_TUC	= 0x10000000,
	EESR_ROC	= 0x08000000,
	EESR_TABT	= 0x04000000,
	EESR_RABT	= 0x02000000,
	EESR_RFRMER	= 0x01000000,	/* same as RFCOF */
	EESR_ADE	= 0x00800000,
	EESR_ECI	= 0x00400000,
	EESR_FTC	= 0x00200000,	/* same as TC or TC0 */
	EESR_TDE	= 0x00100000,
	EESR_TFE	= 0x00080000,	/* same as TFUF */
	EESR_FRC	= 0x00040000,	/* same as FR */
	EESR_RDE	= 0x00020000,
	EESR_RFE	= 0x00010000,
	EESR_CND	= 0x00000800,
	EESR_DLC	= 0x00000400,
	EESR_CD		= 0x00000200,
	EESR_RTO	= 0x00000100,
	EESR_RMAF	= 0x00000080,
	EESR_CEEF	= 0x00000040,
	EESR_CELF	= 0x00000020,
	EESR_RRF	= 0x00000010,
	EESR_RTLF	= 0x00000008,
	EESR_RTSF	= 0x00000004,
	EESR_PRE	= 0x00000002,
	EESR_CERF	= 0x00000001,
};

#define DEFAULT_TX_CHECK	(EESR_FTC | EESR_CND | EESR_DLC | EESR_CD | \
				 EESR_RTO)
#define DEFAULT_EESR_ERR_CHECK	(EESR_TWB | EESR_TABT | EESR_RABT | \
				 EESR_RDE | EESR_RFRMER | EESR_ADE | \
				 EESR_TFE | EESR_TDE | EESR_ECI)
#define DEFAULT_TX_ERROR_CHECK	(EESR_TWB | EESR_TABT | EESR_ADE | EESR_TDE | \
				 EESR_TFE)

/* EESIPR */
enum DMAC_IM_BIT {
	DMAC_M_TWB = 0x40000000, DMAC_M_TABT = 0x04000000,
	DMAC_M_RABT = 0x02000000,
	DMAC_M_RFRMER = 0x01000000, DMAC_M_ADF = 0x00800000,
	DMAC_M_ECI = 0x00400000, DMAC_M_FTC = 0x00200000,
	DMAC_M_TDE = 0x00100000, DMAC_M_TFE = 0x00080000,
	DMAC_M_FRC = 0x00040000, DMAC_M_RDE = 0x00020000,
	DMAC_M_RFE = 0x00010000, DMAC_M_TINT4 = 0x00000800,
	DMAC_M_TINT3 = 0x00000400, DMAC_M_TINT2 = 0x00000200,
	DMAC_M_TINT1 = 0x00000100, DMAC_M_RINT8 = 0x00000080,
	DMAC_M_RINT5 = 0x00000010, DMAC_M_RINT4 = 0x00000008,
	DMAC_M_RINT3 = 0x00000004, DMAC_M_RINT2 = 0x00000002,
	DMAC_M_RINT1 = 0x00000001,
};

/* Receive descriptor bit */
enum RD_STS_BIT {
	RD_RACT = 0x80000000, RD_RDEL = 0x40000000,
	RD_RFP1 = 0x20000000, RD_RFP0 = 0x10000000,
	RD_RFE = 0x08000000, RD_RFS10 = 0x00000200,
	RD_RFS9 = 0x00000100, RD_RFS8 = 0x00000080,
	RD_RFS7 = 0x00000040, RD_RFS6 = 0x00000020,
	RD_RFS5 = 0x00000010, RD_RFS4 = 0x00000008,
	RD_RFS3 = 0x00000004, RD_RFS2 = 0x00000002,
	RD_RFS1 = 0x00000001,
};
#define RDF1ST	RD_RFP1
#define RDFEND	RD_RFP0
#define RD_RFP	(RD_RFP1|RD_RFP0)

/* FCFTR */
enum FCFTR_BIT {
	FCFTR_RFF2 = 0x00040000, FCFTR_RFF1 = 0x00020000,
	FCFTR_RFF0 = 0x00010000, FCFTR_RFD2 = 0x00000004,
	FCFTR_RFD1 = 0x00000002, FCFTR_RFD0 = 0x00000001,
};
#define DEFAULT_FIFO_F_D_RFF	(FCFTR_RFF2 | FCFTR_RFF1 | FCFTR_RFF0)
#define DEFAULT_FIFO_F_D_RFD	(FCFTR_RFD2 | FCFTR_RFD1 | FCFTR_RFD0)

/* Transfer descriptor bit */
enum TD_STS_BIT {
	TD_TACT = 0x80000000,
	TD_TDLE = 0x40000000, TD_TFP1 = 0x20000000,
	TD_TFP0 = 0x10000000,
};
#define TDF1ST	TD_TFP1
#define TDFEND	TD_TFP0
#define TD_TFP	(TD_TFP1|TD_TFP0)

/* RMCR */
#define DEFAULT_RMCR_VALUE	0x00000000

/* ECMR */
enum FELIC_MODE_BIT {
	ECMR_TRCCM = 0x04000000, ECMR_RCSC = 0x00800000,
	ECMR_DPAD = 0x00200000, ECMR_RZPF = 0x00100000,
	ECMR_ZPF = 0x00080000, ECMR_PFR = 0x00040000, ECMR_RXF = 0x00020000,
	ECMR_TXF = 0x00010000, ECMR_MCT = 0x00002000, ECMR_PRCEF = 0x00001000,
	ECMR_PMDE = 0x00000200, ECMR_RE = 0x00000040, ECMR_TE = 0x00000020,
	ECMR_RTM = 0x00000010, ECMR_ILB = 0x00000008, ECMR_ELB = 0x00000004,
	ECMR_DM = 0x00000002, ECMR_PRM = 0x00000001,
};

/* ECSR */
enum ECSR_STATUS_BIT {
	ECSR_BRCRX = 0x20, ECSR_PSRTO = 0x10,
	ECSR_LCHNG = 0x04,
	ECSR_MPD = 0x02, ECSR_ICD = 0x01,
};

#define DEFAULT_ECSR_INIT	(ECSR_BRCRX | ECSR_PSRTO | ECSR_LCHNG | \
				 ECSR_ICD | ECSIPR_MPDIP)

/* ECSIPR */
enum ECSIPR_STATUS_MASK_BIT {
	ECSIPR_BRCRXIP = 0x20, ECSIPR_PSRTOIP = 0x10,
	ECSIPR_LCHNGIP = 0x04,
	ECSIPR_MPDIP = 0x02, ECSIPR_ICDIP = 0x01,
};

#define DEFAULT_ECSIPR_INIT	(ECSIPR_BRCRXIP | ECSIPR_PSRTOIP | \
				 ECSIPR_LCHNGIP | ECSIPR_ICDIP | ECSIPR_MPDIP)

/* APR */
enum APR_BIT {
	APR_AP = 0x00000001,
};

/* MPR */
enum MPR_BIT {
	MPR_MP = 0x00000001,
};

/* TRSCER */
enum DESC_I_BIT {
	DESC_I_TINT4 = 0x0800, DESC_I_TINT3 = 0x0400, DESC_I_TINT2 = 0x0200,
	DESC_I_TINT1 = 0x0100, DESC_I_RINT8 = 0x0080, DESC_I_RINT5 = 0x0010,
	DESC_I_RINT4 = 0x0008, DESC_I_RINT3 = 0x0004, DESC_I_RINT2 = 0x0002,
	DESC_I_RINT1 = 0x0001,
};

/* RPADIR */
enum RPADIR_BIT {
	RPADIR_PADS1 = 0x20000, RPADIR_PADS0 = 0x10000,
	RPADIR_PADR = 0x0003f,
};

/* FDR */
#define DEFAULT_FDR_INIT	0x00000707

enum phy_offsets {
	PHY_CTRL = 0, PHY_STAT = 1, PHY_IDT1 = 2, PHY_IDT2 = 3,
	PHY_ANA = 4, PHY_ANL = 5, PHY_ANE = 6,
	PHY_16 = 16,
};

/* PHY_CTRL */
enum PHY_CTRL_BIT {
	PHY_C_RESET = 0x8000, PHY_C_LOOPBK = 0x4000, PHY_C_SPEEDSL = 0x2000,
	PHY_C_ANEGEN = 0x1000, PHY_C_PWRDN = 0x0800, PHY_C_ISO = 0x0400,
	PHY_C_RANEG = 0x0200, PHY_C_DUPLEX = 0x0100, PHY_C_COLT = 0x0080,
};
#define DM9161_PHY_C_ANEGEN 0	/* auto nego special */

/* PHY_STAT */
enum PHY_STAT_BIT {
	PHY_S_100T4 = 0x8000, PHY_S_100X_F = 0x4000, PHY_S_100X_H = 0x2000,
	PHY_S_10T_F = 0x1000, PHY_S_10T_H = 0x0800, PHY_S_ANEGC = 0x0020,
	PHY_S_RFAULT = 0x0010, PHY_S_ANEGA = 0x0008, PHY_S_LINK = 0x0004,
	PHY_S_JAB = 0x0002, PHY_S_EXTD = 0x0001,
};

/* PHY_ANA */
enum PHY_ANA_BIT {
	PHY_A_NP = 0x8000, PHY_A_ACK = 0x4000, PHY_A_RF = 0x2000,
	PHY_A_FCS = 0x0400, PHY_A_T4 = 0x0200, PHY_A_FDX = 0x0100,
	PHY_A_HDX = 0x0080, PHY_A_10FDX = 0x0040, PHY_A_10HDX = 0x0020,
	PHY_A_SEL = 0x001e,
};
/* PHY_ANL */
enum PHY_ANL_BIT {
	PHY_L_NP = 0x8000, PHY_L_ACK = 0x4000, PHY_L_RF = 0x2000,
	PHY_L_FCS = 0x0400, PHY_L_T4 = 0x0200, PHY_L_FDX = 0x0100,
	PHY_L_HDX = 0x0080, PHY_L_10FDX = 0x0040, PHY_L_10HDX = 0x0020,
	PHY_L_SEL = 0x001f,
};

/* PHY_ANE */
enum PHY_ANE_BIT {
	PHY_E_PDF = 0x0010, PHY_E_LPNPA = 0x0008, PHY_E_NPA = 0x0004,
	PHY_E_PRX = 0x0002, PHY_E_LPANEGA = 0x0001,
};

/* DM9161 */
enum PHY_16_BIT {
	PHY_16_BP4B45 = 0x8000, PHY_16_BPSCR = 0x4000, PHY_16_BPALIGN = 0x2000,
	PHY_16_BP_ADPOK = 0x1000, PHY_16_Repeatmode = 0x0800,
	PHY_16_TXselect = 0x0400,
	PHY_16_Rsvd = 0x0200, PHY_16_RMIIEnable = 0x0100,
	PHY_16_Force100LNK = 0x0080,
	PHY_16_APDLED_CTL = 0x0040, PHY_16_COLLED_CTL = 0x0020,
	PHY_16_RPDCTR_EN = 0x0010,
	PHY_16_ResetStMch = 0x0008, PHY_16_PreamSupr = 0x0004,
	PHY_16_Sleepmode = 0x0002,
	PHY_16_RemoteLoopOut = 0x0001,
};

#define POST_RX		0x08
#define POST_FW		0x04
#define POST0_RX	(POST_RX)
#define POST0_FW	(POST_FW)
#define POST1_RX	(POST_RX >> 2)
#define POST1_FW	(POST_FW >> 2)
#define POST_ALL	(POST0_RX | POST0_FW | POST1_RX | POST1_FW)

/* ARSTR */
enum ARSTR_BIT { ARSTR_ARSTR = 0x00000001, };

/* TSU_FWEN0 */
enum TSU_FWEN0_BIT {
	TSU_FWEN0_0 = 0x00000001,
};

/* TSU_ADSBSY */
enum TSU_ADSBSY_BIT {
	TSU_ADSBSY_0 = 0x00000001,
};

/* TSU_TEN */
enum TSU_TEN_BIT {
	TSU_TEN_0 = 0x80000000,
};

/* TSU_FWSL0 */
enum TSU_FWSL0_BIT {
	TSU_FWSL0_FW50 = 0x1000, TSU_FWSL0_FW40 = 0x0800,
	TSU_FWSL0_FW30 = 0x0400, TSU_FWSL0_FW20 = 0x0200,
	TSU_FWSL0_FW10 = 0x0100, TSU_FWSL0_RMSA0 = 0x0010,
};

/* TSU_FWSLC */
enum TSU_FWSLC_BIT {
	TSU_FWSLC_POSTENU = 0x2000, TSU_FWSLC_POSTENL = 0x1000,
	TSU_FWSLC_CAMSEL03 = 0x0080, TSU_FWSLC_CAMSEL02 = 0x0040,
	TSU_FWSLC_CAMSEL01 = 0x0020, TSU_FWSLC_CAMSEL00 = 0x0010,
	TSU_FWSLC_CAMSEL13 = 0x0008, TSU_FWSLC_CAMSEL12 = 0x0004,
	TSU_FWSLC_CAMSEL11 = 0x0002, TSU_FWSLC_CAMSEL10 = 0x0001,
};

/* TSU_VTAGn */
#define TSU_VTAG_ENABLE		0x80000000
#define TSU_VTAG_VID_MASK	0x00000fff

/*
 * The sh ether Tx buffer descriptors.
 * This structure should be 20 bytes.
 */
struct sh_eth_txdesc {
	u32 status;		/* TD0 */
#if defined(__LITTLE_ENDIAN)
	u16 pad0;		/* TD1 */
	u16 buffer_length;	/* TD1 */
#else
	u16 buffer_length;	/* TD1 */
	u16 pad0;		/* TD1 */
#endif
	u32 addr;		/* TD2 */
	u32 pad1;		/* padding data */
} __attribute__((aligned(2), packed));

/*
 * The sh ether Rx buffer descriptors.
 * This structure should be 20 bytes.
 */
struct sh_eth_rxdesc {
	u32 status;		/* RD0 */
#if defined(__LITTLE_ENDIAN)
	u16 frame_length;	/* RD1 */
	u16 buffer_length;	/* RD1 */
#else
	u16 buffer_length;	/* RD1 */
	u16 frame_length;	/* RD1 */
#endif
	u32 addr;		/* RD2 */
	u32 pad0;		/* padding data */
} __attribute__((aligned(2), packed));

/* This structure is used by each CPU dependency handling. */
struct sh_eth_cpu_data {
	/* optional functions */
	void (*chip_reset)(struct net_device *ndev);
	void (*set_duplex)(struct net_device *ndev);
	void (*set_rate)(struct net_device *ndev);

	/* mandatory initialize value */
	unsigned long eesipr_value;

	/* optional initialize value */
	unsigned long ecsr_value;
	unsigned long ecsipr_value;
	unsigned long fdr_value;
	unsigned long fcftr_value;
	unsigned long rpadir_value;
	unsigned long rmcr_value;

	/* interrupt checking mask */
	unsigned long tx_check;
	unsigned long eesr_err_check;
	unsigned long tx_error_check;

	/* hardware features */
	unsigned no_psr:1;		/* EtherC DO NOT have PSR */
	unsigned apr:1;			/* EtherC have APR */
	unsigned mpr:1;			/* EtherC have MPR */
	unsigned tpauser:1;		/* EtherC have TPAUSER */
	unsigned bculr:1;		/* EtherC have BCULR */
	unsigned tsu:1;			/* EtherC have TSU */
	unsigned hw_swap:1;		/* E-DMAC have DE bit in EDMR */
	unsigned rpadir:1;		/* E-DMAC have RPADIR */
	unsigned no_trimd:1;		/* E-DMAC DO NOT have TRIMD */
	unsigned no_ade:1;	/* E-DMAC DO NOT have ADE bit in EESR */
	unsigned hw_crc:1;	/* E-DMAC have CSMR */
};

struct sh_eth_private {
	struct platform_device *pdev;
	struct sh_eth_cpu_data *cd;
	const u16 *reg_offset;
	void __iomem *addr;
	void __iomem *tsu_addr;
	dma_addr_t rx_desc_dma;
	dma_addr_t tx_desc_dma;
	struct sh_eth_rxdesc *rx_ring;
	struct sh_eth_txdesc *tx_ring;
	struct sk_buff **rx_skbuff;
	struct sk_buff **tx_skbuff;
	struct timer_list timer;
	spinlock_t lock;
	u32 cur_rx, dirty_rx;	/* Producer/consumer ring indices */
	u32 cur_tx, dirty_tx;
	u32 rx_buf_sz;		/* Based on MTU+slack. */
	int edmac_endian;
	/* MII transceiver section. */
	u32 phy_id;					/* PHY ID */
	struct mii_bus *mii_bus;	/* MDIO bus control */
	struct phy_device *phydev;	/* PHY device control */
	enum phy_state link;
	phy_interface_t phy_interface;
	int msg_enable;
	int speed;
	int duplex;
	u32 rx_int_var, tx_int_var;	/* interrupt control variables */
	char post_rx;		/* POST receive */
	char post_fw;		/* POST forward */
	struct net_device_stats tsu_stats;	/* TSU forward status */
	int port;		/* for TSU */
	int vlan_num_ids;	/* for VLAN tag filter */

	unsigned no_ether_link:1;
	unsigned ether_link_active_low:1;
};

static inline void sh_eth_soft_swap(char *src, int len)
{
#ifdef __LITTLE_ENDIAN__
	u32 *p = (u32 *)src;
	u32 *maxp;
	maxp = p + ((len + sizeof(u32) - 1) / sizeof(u32));

	for (; p < maxp; p++)
		*p = swab32(*p);
#endif
}

static inline void sh_eth_write(struct net_device *ndev, unsigned long data,
				int enum_index)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	iowrite32(data, mdp->addr + mdp->reg_offset[enum_index]);
}

static inline unsigned long sh_eth_read(struct net_device *ndev,
					int enum_index)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);

	return ioread32(mdp->addr + mdp->reg_offset[enum_index]);
}

static inline void *sh_eth_tsu_get_offset(struct sh_eth_private *mdp,
					  int enum_index)
{
	return mdp->tsu_addr + mdp->reg_offset[enum_index];
}

static inline void sh_eth_tsu_write(struct sh_eth_private *mdp,
				unsigned long data, int enum_index)
{
	iowrite32(data, mdp->tsu_addr + mdp->reg_offset[enum_index]);
}

static inline unsigned long sh_eth_tsu_read(struct sh_eth_private *mdp,
					int enum_index)
{
	return ioread32(mdp->tsu_addr + mdp->reg_offset[enum_index]);
}

#endif	/* #ifndef __SH_ETH_H__ */
