/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * This header file defines the register offsets and bit fields
 * of ENETC4 PF and VFs. Note that the same registers as ENETC
 * version 1.0 are defined in the enetc_hw.h file.
 *
 * Copyright 2024 NXP
 */
#ifndef __ENETC4_HW_H_
#define __ENETC4_HW_H_

#define NXP_ENETC_VENDOR_ID		0x1131
#define NXP_ENETC_PF_DEV_ID		0xe101
#define NXP_ENETC_PPM_DEV_ID		0xe110

/**********************Station interface registers************************/
/* Station interface LSO segmentation flag mask register 0/1 */
#define ENETC4_SILSOSFMR0		0x1300
#define  SILSOSFMR0_TCP_MID_SEG		GENMASK(27, 16)
#define  SILSOSFMR0_TCP_1ST_SEG		GENMASK(11, 0)
#define  SILSOSFMR0_VAL_SET(first, mid)	(FIELD_PREP(SILSOSFMR0_TCP_MID_SEG, mid) | \
					 FIELD_PREP(SILSOSFMR0_TCP_1ST_SEG, first))

#define ENETC4_SILSOSFMR1		0x1304
#define  SILSOSFMR1_TCP_LAST_SEG	GENMASK(11, 0)
#define   ENETC4_TCP_FLAGS_FIN		BIT(0)
#define   ENETC4_TCP_FLAGS_SYN		BIT(1)
#define   ENETC4_TCP_FLAGS_RST		BIT(2)
#define   ENETC4_TCP_FLAGS_PSH		BIT(3)
#define   ENETC4_TCP_FLAGS_ACK		BIT(4)
#define   ENETC4_TCP_FLAGS_URG		BIT(5)
#define   ENETC4_TCP_FLAGS_ECE		BIT(6)
#define   ENETC4_TCP_FLAGS_CWR		BIT(7)
#define   ENETC4_TCP_FLAGS_NS		BIT(8)
/* According to tso_build_hdr(), clear all special flags for not last packet. */
#define ENETC4_TCP_NL_SEG_FLAGS_DMASK	(ENETC4_TCP_FLAGS_FIN | \
					 ENETC4_TCP_FLAGS_RST | ENETC4_TCP_FLAGS_PSH)

/***************************ENETC port registers**************************/
#define ENETC4_ECAPR0			0x0
#define  ECAPR0_RFS			BIT(2)
#define  ECAPR0_TSD			BIT(5)
#define  ECAPR0_RSS			BIT(8)
#define  ECAPR0_RSC			BIT(9)
#define  ECAPR0_LSO			BIT(10)
#define  ECAPR0_WO			BIT(13)

#define ENETC4_ECAPR1			0x4
#define  ECAPR1_NUM_TCS			GENMASK(6, 4)
#define  ECAPR1_NUM_MCH			GENMASK(9, 8)
#define  ECAPR1_NUM_UCH			GENMASK(11, 10)
#define  ECAPR1_NUM_MSIX		GENMASK(22, 12)
#define  ECAPR1_NUM_VSI			GENMASK(27, 24)
#define  ECAPR1_NUM_IPV			BIT(31)

#define ENETC4_ECAPR2			0x8
#define  ECAPR2_NUM_TX_BDR		GENMASK(9, 0)
#define  ECAPR2_NUM_RX_BDR		GENMASK(25, 16)

#define ENETC4_PMR			0x10
#define  PMR_SI_EN(a)			BIT((16 + (a)))

/* Port Pause ON/OFF threshold register */
#define ENETC4_PPAUONTR			0x108
#define ENETC4_PPAUOFFTR		0x10c

/* Port ingress congestion DRa (a=0,1,2,3) discard count register */
#define ENETC4_PICDRDCR(a)		((a) * 0x10 + 0x140)

/* Port Station interface promiscuous MAC mode register */
#define ENETC4_PSIPMMR			0x200
#define  PSIPMMR_SI_MAC_UP(a)		BIT(a) /* a = SI index */
#define  PSIPMMR_SI_MAC_MP(a)		BIT((a) + 16)

/* Port Station interface promiscuous VLAN mode register */
#define ENETC4_PSIPVMR			0x204

/* Port broadcast frames dropped due to MAC filtering register */
#define ENETC4_PBFDSIR			0x208

/* Port frame drop MAC source address pruning register */
#define ENETC4_PFDMSAPR			0x20c

/* Port RSS key register n. n = 0,1,2,...,9 */
#define ENETC4_PRSSKR(n)		((n) * 0x4 + 0x250)

/* Port station interface MAC address filtering capability register */
#define ENETC4_PSIMAFCAPR		0x280
#define  PSIMAFCAPR_NUM_MAC_AFTE	GENMASK(11, 0)

/* Port unicast frames dropped due to MAC filtering register */
#define ENETC4_PUFDMFR			0x284

/* Port multicast frames dropped due to MAC filtering register */
#define ENETC4_PMFDMFR			0x288

/* Port station interface VLAN filtering capability register */
#define ENETC4_PSIVLANFCAPR		0x2c0
#define  PSIVLANFCAPR_NUM_VLAN_FTE	GENMASK(11, 0)

/* Port station interface VLAN filtering mode register */
#define ENETC4_PSIVLANFMR		0x2c4
#define  PSIVLANFMR_VS			BIT(0)

/* Port unicast frames dropped VLAN filtering register */
#define ENETC4_PUFDVFR			0x2d0

/* Port multicast frames dropped VLAN filtering register */
#define ENETC4_PMFDVFR			0x2d4

/* Port broadcast frames dropped VLAN filtering register */
#define ENETC4_PBFDVFR			0x2d8

/* Port Station interface a primary MAC address registers */
#define ENETC4_PSIPMAR0(a)		((a) * 0x80 + 0x2000)
#define ENETC4_PSIPMAR1(a)		((a) * 0x80 + 0x2004)

/* Port station interface a configuration register 0/2 */
#define ENETC4_PSICFGR0(a)		((a) * 0x80 + 0x2010)
#define  PSICFGR0_VASE			BIT(13)
#define  PSICFGR0_ASE			BIT(15)
#define  PSICFGR0_ANTI_SPOOFING		(PSICFGR0_VASE | PSICFGR0_ASE)

#define ENETC4_PSICFGR2(a)		((a) * 0x80 + 0x2018)
#define  PSICFGR2_NUM_MSIX		GENMASK(5, 0)

/* Port station interface a unicast MAC hash filter register 0/1 */
#define ENETC4_PSIUMHFR0(a)		((a) * 0x80 + 0x2050)
#define ENETC4_PSIUMHFR1(a)		((a) * 0x80 + 0x2054)

/* Port station interface a multicast MAC hash filter register 0/1 */
#define ENETC4_PSIMMHFR0(a)		((a) * 0x80 + 0x2058)
#define ENETC4_PSIMMHFR1(a)		((a) * 0x80 + 0x205c)

/* Port station interface a VLAN hash filter register 0/1 */
#define ENETC4_PSIVHFR0(a)		((a) * 0x80 + 0x2060)
#define ENETC4_PSIVHFR1(a)		((a) * 0x80 + 0x2064)

#define ENETC4_PMCAPR			0x4004
#define  PMCAPR_HD			BIT(8)
#define  PMCAPR_FP			GENMASK(10, 9)

/* Port capability register */
#define ENETC4_PCAPR			0x4000
#define  PCAPR_LINK_TYPE		BIT(4)

/* Port configuration register */
#define ENETC4_PCR			0x4010
#define  PCR_HDR_FMT			BIT(0)
#define  PCR_L2DOSE			BIT(4)
#define  PCR_TIMER_CS			BIT(8)
#define  PCR_PSPEED			GENMASK(29, 16)
#define  PCR_PSPEED_VAL(speed)		(((speed) / 10 - 1) << 16)

/* Port MAC address register 0/1 */
#define ENETC4_PMAR0			0x4020
#define ENETC4_PMAR1			0x4024

/* Port operational register */
#define ENETC4_POR			0x4100
#define  POR_TXDIS			BIT(0)
#define  POR_RXDIS			BIT(1)

/* Port status register */
#define ENETC4_PSR			0x4104
#define  PSR_RX_BUSY			BIT(1)

/* Port Rx discard count register */
#define ENETC4_PRXDCR			0x41c0

/* Port Rx discard count read-reset register */
#define ENETC4_PRXDCRRR			0x41c4

/* Port Rx discard count reason register 0 */
#define ENETC4_PRXDCRR0			0x41c8

/* Port Rx discard count reason register 1 */
#define ENETC4_PRXDCRR1			0x41cc

/* Port traffic class a transmit maximum SDU register */
#define ENETC4_PTCTMSDUR(a)		((a) * 0x20 + 0x4208)
#define  PTCTMSDUR_MAXSDU		GENMASK(15, 0)
#define  PTCTMSDUR_SDU_TYPE		GENMASK(17, 16)
#define   SDU_TYPE_PPDU			0
#define   SDU_TYPE_MPDU			1
#define   SDU_TYPE_MSDU			2

#define ENETC4_PMAC_OFFSET		0x400
#define ENETC4_PM_CMD_CFG(mac)		(0x5008 + (mac) * 0x400)
#define  PM_CMD_CFG_TX_EN		BIT(0)
#define  PM_CMD_CFG_RX_EN		BIT(1)
#define  PM_CMD_CFG_PAUSE_FWD		BIT(7)
#define  PM_CMD_CFG_PAUSE_IGN		BIT(8)
#define  PM_CMD_CFG_TX_ADDR_INS		BIT(9)
#define  PM_CMD_CFG_LOOP_EN		BIT(10)
#define  PM_CMD_CFG_LPBK_MODE		GENMASK(12, 11)
#define   LPBCK_MODE_EXT_TX_CLK		0
#define   LPBCK_MODE_MAC_LEVEL		1
#define   LPBCK_MODE_INT_TX_CLK		2
#define  PM_CMD_CFG_CNT_FRM_EN		BIT(13)
#define  PM_CMD_CFG_TXP			BIT(15)
#define  PM_CMD_CFG_SEND_IDLE		BIT(16)
#define  PM_CMD_CFG_HD_FCEN		BIT(18)
#define  PM_CMD_CFG_SFD			BIT(21)
#define  PM_CMD_CFG_TX_FLUSH		BIT(22)
#define  PM_CMD_CFG_TX_LOWP_EN		BIT(23)
#define  PM_CMD_CFG_RX_LOWP_EMPTY	BIT(24)
#define  PM_CMD_CFG_SWR			BIT(26)
#define  PM_CMD_CFG_TS_MODE		BIT(30)
#define  PM_CMD_CFG_MG			BIT(31)

/* Port MAC 0/1 Maximum Frame Length Register */
#define ENETC4_PM_MAXFRM(mac)		(0x5014 + (mac) * 0x400)

/* Port internal MDIO base address, use to access PCS */
#define ENETC4_PM_IMDIO_BASE		0x5030

/* Port MAC 0/1 Interrupt Event Register */
#define ENETC4_PM_IEVENT(mac)		(0x5040 + (mac) * 0x400)
#define  PM_IEVENT_TX_EMPTY		BIT(5)
#define  PM_IEVENT_RX_EMPTY		BIT(6)

/* Port MAC 0/1 Pause Quanta Register */
#define ENETC4_PM_PAUSE_QUANTA(mac)	(0x5054 + (mac) * 0x400)

/* Port MAC 0/1 Pause Quanta Threshold Register */
#define ENETC4_PM_PAUSE_THRESH(mac)	(0x5064 + (mac) * 0x400)

#define ENETC4_PM_SINGLE_STEP(mac)	(0x50c0 + (mac) * 0x400)
#define  PM_SINGLE_STEP_CH		BIT(6)
#define  PM_SINGLE_STEP_OFFSET		GENMASK(15, 7)
#define  PM_SINGLE_STEP_OFFSET_SET(o)	FIELD_PREP(PM_SINGLE_STEP_OFFSET, o)
#define  PM_SINGLE_STEP_EN		BIT(31)

/* Port MAC 0/1 Receive Ethernet Octets Counter */
#define ENETC4_PM_REOCT(mac)		(0x5100 + (mac) * 0x400)

/* Port MAC 0/1 Receive Octets Counter */
#define ENETC4_PM_ROCT(mac)		(0x5108 + (mac) * 0x400)

/* Port MAC 0/1 Receive Alignment Error Counter Register */
#define ENETC4_PM_RALN(mac)		(0x5110 + (mac) * 0x400)

/* Port MAC 0/1 Receive Valid Pause Frame Counter */
#define ENETC4_PM_RXPF(mac)		(0x5118 + (mac) * 0x400)

/* Port MAC 0/1 Receive Frame Counter */
#define ENETC4_PM_RFRM(mac)		(0x5120 + (mac) * 0x400)

/* Port MAC 0/1 Receive Frame Check Sequence Error Counter */
#define ENETC4_PM_RFCS(mac)		(0x5128 + (mac) * 0x400)

/* Port MAC 0/1 Receive VLAN Frame Counter */
#define ENETC4_PM_RVLAN(mac)		(0x5130 + (mac) * 0x400)

/* Port MAC 0/1 Receive Frame Error Counter */
#define ENETC4_PM_RERR(mac)		(0x5138 + (mac) * 0x400)

/* Port MAC 0/1 Receive Unicast Frame Counter */
#define ENETC4_PM_RUCA(mac)		(0x5140 + (mac) * 0x400)

/* Port MAC 0/1 Receive Multicast Frame Counter */
#define ENETC4_PM_RMCA(mac)		(0x5148 + (mac) * 0x400)

/* Port MAC 0/1 Receive Broadcast Frame Counter */
#define ENETC4_PM_RBCA(mac)		(0x5150 + (mac) * 0x400)

/* Port MAC 0/1 Receive Dropped Packets Counter */
#define ENETC4_PM_RDRP(mac)		(0x5158 + (mac) * 0x400)

/* Port MAC 0/1 Receive Packets Counter */
#define ENETC4_PM_RPKT(mac)		(0x5160 + (mac) * 0x400)

/* Port MAC 0/1 Receive Undersized Packet Counter */
#define ENETC4_PM_RUND(mac)		(0x5168 + (mac) * 0x400)

/* Port MAC 0/1 Receive 64-Octet Packet Counter */
#define ENETC4_PM_R64(mac)		(0x5170 + (mac) * 0x400)

/* Port MAC 0/1 Receive 65 to 127-Octet Packet Counter */
#define ENETC4_PM_R127(mac)		(0x5178 + (mac) * 0x400)

/* Port MAC 0/1 Receive 128 to 255-Octet Packet Counter */
#define ENETC4_PM_R255(mac)		(0x5180 + (mac) * 0x400)

/* Port MAC 0/1 Receive 256 to 511-Octet Packet Counter */
#define ENETC4_PM_R511(mac)		(0x5188 + (mac) * 0x400)

/* Port MAC 0/1 Receive 512 to 1023-Octet Packet Counter */
#define ENETC4_PM_R1023(mac)		(0x5190 + (mac) * 0x400)

/* Port MAC 0/1 Receive 1024 to 1522-Octet Packet Counter */
#define ENETC4_PM_R1522(mac)		(0x5198 + (mac) * 0x400)

/* Port MAC 0/1 Receive 1523 to Max-Octet Packet Counter */
#define ENETC4_PM_R1523X(mac)		(0x51a0 + (mac) * 0x400)

/* Port MAC 0/1 Receive Oversized Packet Counter */
#define ENETC4_PM_ROVR(mac)		(0x51a8 + (mac) * 0x400)

/* Port MAC 0/1 Receive Jabber Packet Counter */
#define ENETC4_PM_RJBR(mac)		(0x51b0 + (mac) * 0x400)

/* Port MAC 0/1 Receive Fragment Packet Counter */
#define ENETC4_PM_RFRG(mac)		(0x51b8 + (mac) * 0x400)

/* Port MAC 0/1 Receive Control Packet Counter */
#define ENETC4_PM_RCNP(mac)		(0x51c0 + (mac) * 0x400)

/* Port MAC 0/1 Receive Dropped Not Truncated Packets Counter */
#define ENETC4_PM_RDRNTP(mac)		(0x51c8 + (mac) * 0x400)

/* Port MAC 0/1 Transmit Ethernet Octets Counter */
#define ENETC4_PM_TEOCT(mac)		(0x5200 + (mac) * 0x400)

/* Port MAC 0/1 Transmit Octets Counter */
#define ENETC4_PM_TOCT(mac)		(0x5208 + (mac) * 0x400)

/* Port MAC 0/1 Transmit Valid Pause Frame Counter */
#define ENETC4_PM_TXPF(mac)		(0x5218 + (mac) * 0x400)

/* Port MAC 0/1 Transmit Frame Counter */
#define ENETC4_PM_TFRM(mac)		(0x5220 + (mac) * 0x400)

/* Port MAC 0/1 Transmit Frame Check Sequence Error Counter */
#define ENETC4_PM_TFCS(mac)		(0x5228 + (mac) * 0x400)

/* Port MAC 0/1 Transmit VLAN Frame Counter */
#define ENETC4_PM_TVLAN(mac)		(0x5230 + (mac) * 0x400)

/* Port MAC 0/1 Transmit Frame Error Counter */
#define ENETC4_PM_TERR(mac)		(0x5238 + (mac) * 0x400)

/* Port MAC 0/1 Transmit Unicast Frame Counter */
#define ENETC4_PM_TUCA(mac)		(0x5240 + (mac) * 0x400)

/* Port MAC 0/1 Transmit Multicast Frame Counter */
#define ENETC4_PM_TMCA(mac)		(0x5248 + (mac) * 0x400)

/* Port MAC 0/1 Transmit Broadcast Frame Counter */
#define ENETC4_PM_TBCA(mac)		(0x5250 + (mac) * 0x400)

/* Port MAC 0/1 Transmit Packets Counter */
#define ENETC4_PM_TPKT(mac)		(0x5260 + (mac) * 0x400)

/* Port MAC 0/1 Transmit Undersized Packet Counter */
#define ENETC4_PM_TUND(mac)		(0x5268 + (mac) * 0x400)

/* Port MAC 0/1 Transmit 64-Octet Packet Counter */
#define ENETC4_PM_T64(mac)		(0x5270 + (mac) * 0x400)

/* Port MAC 0/1 Transmit 65 to 127-Octet Packet Counter */
#define ENETC4_PM_T127(mac)		(0x5278 + (mac) * 0x400)

/* Port MAC 0/1 Transmit 128 to 255-Octet Packet Counter */
#define ENETC4_PM_T255(mac)		(0x5280 + (mac) * 0x400)

/* Port MAC 0/1 Transmit 256 to 511-Octet Packet Counter */
#define ENETC4_PM_T511(mac)		(0x5288 + (mac) * 0x400)

/* Port MAC 0/1 Transmit 512 to 1023-Octet Packet Counter */
#define ENETC4_PM_T1023(mac)		(0x5290 + (mac) * 0x400)

/* Port MAC 0/1 Transmit 1024 to 1522-Octet Packet Counter */
#define ENETC4_PM_T1522(mac)		(0x5298 + (mac) * 0x400)

/* Port MAC 0/1 Transmit 1523 to TX_MTU-Octet Packet Counter */
#define ENETC4_PM_T1523X(mac)		(0x52a0 + (mac) * 0x400)

/* Port MAC 0/1 Transmit Control Packet Counter */
#define ENETC4_PM_TCNP(mac)		(0x52c0 + (mac) * 0x400)

/* Port MAC 0/1 Transmit Deferred Packet Counter */
#define ENETC4_PM_TDFR(mac)		(0x52d0 + (mac) * 0x400)

/* Port MAC 0/1 Transmit Multiple Collisions Counter */
#define ENETC4_PM_TMCOL(mac)		(0x52d8 + (mac) * 0x400)

/* Port MAC 0/1 Transmit Single Collision */
#define ENETC4_PM_TSCOL(mac)		(0x52e0 + (mac) * 0x400)

/* Port MAC 0/1 Transmit Late Collision Counter */
#define ENETC4_PM_TLCOL(mac)		(0x52e8 + (mac) * 0x400)

/* Port MAC 0/1 Transmit Excessive Collisions Counter */
#define ENETC4_PM_TECOL(mac)		(0x52f0 + (mac) * 0x400)

/* Port MAC 0/1 Transmit Invalid Octets Counter */
#define ENETC4_PM_TIOCT(mac)		(0x52f8 + (mac) * 0x400)

/* Port MAC 0 Interface Mode Control Register */
#define ENETC4_PM_IF_MODE(mac)		(0x5300 + (mac) * 0x400)
#define  PM_IF_MODE_IFMODE		GENMASK(2, 0)
#define   IFMODE_XGMII			0
#define   IFMODE_RMII			3
#define   IFMODE_RGMII			4
#define   IFMODE_SGMII			5
#define  PM_IF_MODE_REVMII		BIT(3)
#define  PM_IF_MODE_M10			BIT(4)
#define  PM_IF_MODE_HD			BIT(6)
#define  PM_IF_MODE_SSP			GENMASK(14, 13)
#define   SSP_100M			0
#define   SSP_10M			1
#define   SSP_1G			2
#define  PM_IF_MODE_ENA			BIT(15)

/* Port external MDIO Base address, use to access off-chip PHY */
#define ENETC4_EMDIO_BASE		0x5c00

/**********************ENETC Pseudo MAC port registers************************/
/* Port pseudo MAC receive octets counter (64-bit) */
#define ENETC4_PPMROCR			0x5080

/* Port pseudo MAC receive unicast frame counter register (64-bit) */
#define ENETC4_PPMRUFCR			0x5088

/* Port pseudo MAC receive multicast frame counter register (64-bit) */
#define ENETC4_PPMRMFCR			0x5090

/* Port pseudo MAC receive broadcast frame counter register (64-bit) */
#define ENETC4_PPMRBFCR			0x5098

/* Port pseudo MAC transmit octets counter (64-bit) */
#define ENETC4_PPMTOCR			0x50c0

/* Port pseudo MAC transmit unicast frame counter register (64-bit) */
#define ENETC4_PPMTUFCR			0x50c8

/* Port pseudo MAC transmit multicast frame counter register (64-bit) */
#define ENETC4_PPMTMFCR			0x50d0

/* Port pseudo MAC transmit broadcast frame counter register (64-bit) */
#define ENETC4_PPMTBFCR			0x50d8

#endif
