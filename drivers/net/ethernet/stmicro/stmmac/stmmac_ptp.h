/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************
  PTP Header file

  Copyright (C) 2013  Vayavya Labs Pvt Ltd


  Author: Rayagond Kokatanur <rayagond@vayavyalabs.com>
******************************************************************************/

#ifndef	__STMMAC_PTP_H__
#define	__STMMAC_PTP_H__

#define PTP_XGMAC_OFFSET	0xd00
#define	PTP_GMAC4_OFFSET	0xb00
#define	PTP_GMAC3_X_OFFSET	0x700

/* IEEE 1588 PTP register offsets */
#define	PTP_TCR		0x00	/* Timestamp Control Reg */
#define	PTP_SSIR	0x04	/* Sub-Second Increment Reg */
#define	PTP_STSR	0x08	/* System Time – Seconds Regr */
#define	PTP_STNSR	0x0c	/* System Time – Nanoseconds Reg */
#define	PTP_STSUR	0x10	/* System Time – Seconds Update Reg */
#define	PTP_STNSUR	0x14	/* System Time – Nanoseconds Update Reg */
#define	PTP_TAR		0x18	/* Timestamp Addend Reg */
#define	PTP_ACR		0x40	/* Auxiliary Control Reg */
#define	PTP_ATNR	0x48	/* Auxiliary Timestamp - Nanoseconds Reg */
#define	PTP_ATSR	0x4c	/* Auxiliary Timestamp - Seconds Reg */
#define	PTP_TS_INGR_CORR_NS	0x58	/* Ingress timestamp correction nanoseconds */
#define	PTP_TS_EGR_CORR_NS	0x5C	/* Egress timestamp correction nanoseconds*/
#define	PTP_TS_INGR_CORR_SNS	0x60	/* Ingress timestamp correction subnanoseconds */
#define	PTP_TS_EGR_CORR_SNS	0x64	/* Egress timestamp correction subnanoseconds */
#define	PTP_TS_INGR_LAT	0x68	/* MAC internal Ingress Latency */
#define	PTP_TS_EGR_LAT	0x6c	/* MAC internal Egress Latency */

#define	PTP_STNSUR_ADDSUB_SHIFT	31
#define	PTP_DIGITAL_ROLLOVER_MODE	0x3B9ACA00	/* 10e9-1 ns */
#define	PTP_BINARY_ROLLOVER_MODE	0x80000000	/* ~0.466 ns */

/* PTP Timestamp control register defines */
#define	PTP_TCR_TSENA		BIT(0)	/* Timestamp Enable */
#define	PTP_TCR_TSCFUPDT	BIT(1)	/* Timestamp Fine/Coarse Update */
#define	PTP_TCR_TSINIT		BIT(2)	/* Timestamp Initialize */
#define	PTP_TCR_TSUPDT		BIT(3)	/* Timestamp Update */
#define	PTP_TCR_TSTRIG		BIT(4)	/* Timestamp Interrupt Trigger Enable */
#define	PTP_TCR_TSADDREG	BIT(5)	/* Addend Reg Update */
#define	PTP_TCR_TSENALL		BIT(8)	/* Enable Timestamp for All Frames */
#define	PTP_TCR_TSCTRLSSR	BIT(9)	/* Digital or Binary Rollover Control */
/* Enable PTP packet Processing for Version 2 Format */
#define	PTP_TCR_TSVER2ENA	BIT(10)
/* Enable Processing of PTP over Ethernet Frames */
#define	PTP_TCR_TSIPENA		BIT(11)
/* Enable Processing of PTP Frames Sent over IPv6-UDP */
#define	PTP_TCR_TSIPV6ENA	BIT(12)
/* Enable Processing of PTP Frames Sent over IPv4-UDP */
#define	PTP_TCR_TSIPV4ENA	BIT(13)
/* Enable Timestamp Snapshot for Event Messages */
#define	PTP_TCR_TSEVNTENA	BIT(14)
/* Enable Snapshot for Messages Relevant to Master */
#define	PTP_TCR_TSMSTRENA	BIT(15)
/* Select PTP packets for Taking Snapshots
 * On gmac4 specifically:
 * Enable SYNC, Pdelay_Req, Pdelay_Resp when TSEVNTENA is enabled.
 * or
 * Enable  SYNC, Follow_Up, Delay_Req, Delay_Resp, Pdelay_Req, Pdelay_Resp,
 * Pdelay_Resp_Follow_Up if TSEVNTENA is disabled
 */
#define	PTP_TCR_SNAPTYPSEL_1	BIT(16)
/* Enable MAC address for PTP Frame Filtering */
#define	PTP_TCR_TSENMACADDR	BIT(18)

/* SSIR defines */
#define	PTP_SSIR_SSINC_MAX		0xff
#define	GMAC4_PTP_SSIR_SSINC_SHIFT	16

/* Auxiliary Control defines */
#define	PTP_ACR_ATSFC		BIT(0)	/* Auxiliary Snapshot FIFO Clear */
#define	PTP_ACR_ATSEN0		BIT(4)	/* Auxiliary Snapshot 0 Enable */
#define	PTP_ACR_ATSEN1		BIT(5)	/* Auxiliary Snapshot 1 Enable */
#define	PTP_ACR_ATSEN2		BIT(6)	/* Auxiliary Snapshot 2 Enable */
#define	PTP_ACR_ATSEN3		BIT(7)	/* Auxiliary Snapshot 3 Enable */
#define	PTP_ACR_ATSEN_SHIFT	5	/* Auxiliary Snapshot shift */
#define	PTP_ACR_MASK		GENMASK(7, 4)	/* Aux Snapshot Mask */
#define	PMC_ART_VALUE0		0x01	/* PMC_ART[15:0] timer value */
#define	PMC_ART_VALUE1		0x02	/* PMC_ART[31:16] timer value */
#define	PMC_ART_VALUE2		0x03	/* PMC_ART[47:32] timer value */
#define	PMC_ART_VALUE3		0x04	/* PMC_ART[63:48] timer value */
#define	GMAC4_ART_TIME_SHIFT	16	/* ART TIME 16-bits shift */

enum aux_snapshot {
	AUX_SNAPSHOT0 = 0x10,
	AUX_SNAPSHOT1 = 0x20,
	AUX_SNAPSHOT2 = 0x40,
	AUX_SNAPSHOT3 = 0x80,
};

#endif	/* __STMMAC_PTP_H__ */
