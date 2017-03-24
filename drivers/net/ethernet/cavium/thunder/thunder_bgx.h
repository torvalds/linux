/*
 * Copyright (C) 2015 Cavium, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

#ifndef THUNDER_BGX_H
#define THUNDER_BGX_H

/* PCI device ID */
#define	PCI_DEVICE_ID_THUNDER_BGX		0xA026
#define	PCI_DEVICE_ID_THUNDER_RGX		0xA054

/* Subsystem device IDs */
#define PCI_SUBSYS_DEVID_88XX_BGX		0xA126
#define PCI_SUBSYS_DEVID_81XX_BGX		0xA226
#define PCI_SUBSYS_DEVID_83XX_BGX		0xA326

#define    MAX_BGX_THUNDER			8 /* Max 2 nodes, 4 per node */
#define    MAX_BGX_PER_CN88XX			2
#define    MAX_BGX_PER_CN81XX			3 /* 2 BGXs + 1 RGX */
#define    MAX_BGX_PER_CN83XX			4
#define    MAX_LMAC_PER_BGX			4
#define    MAX_BGX_CHANS_PER_LMAC		16
#define    MAX_DMAC_PER_LMAC			8
#define    MAX_FRAME_SIZE			9216
#define    DEFAULT_PAUSE_TIME			0xFFFF

#define	   BGX_ID_MASK				0x3

#define    MAX_DMAC_PER_LMAC_TNS_BYPASS_MODE	2

/* Registers */
#define BGX_CMRX_CFG			0x00
#define  CMR_PKT_TX_EN				BIT_ULL(13)
#define  CMR_PKT_RX_EN				BIT_ULL(14)
#define  CMR_EN					BIT_ULL(15)
#define BGX_CMR_GLOBAL_CFG		0x08
#define  CMR_GLOBAL_CFG_FCS_STRIP		BIT_ULL(6)
#define BGX_CMRX_RX_ID_MAP		0x60
#define BGX_CMRX_RX_STAT0		0x70
#define BGX_CMRX_RX_STAT1		0x78
#define BGX_CMRX_RX_STAT2		0x80
#define BGX_CMRX_RX_STAT3		0x88
#define BGX_CMRX_RX_STAT4		0x90
#define BGX_CMRX_RX_STAT5		0x98
#define BGX_CMRX_RX_STAT6		0xA0
#define BGX_CMRX_RX_STAT7		0xA8
#define BGX_CMRX_RX_STAT8		0xB0
#define BGX_CMRX_RX_STAT9		0xB8
#define BGX_CMRX_RX_STAT10		0xC0
#define BGX_CMRX_RX_BP_DROP		0xC8
#define BGX_CMRX_RX_DMAC_CTL		0x0E8
#define BGX_CMRX_RX_FIFO_LEN		0x108
#define BGX_CMR_RX_DMACX_CAM		0x200
#define  RX_DMACX_CAM_EN			BIT_ULL(48)
#define  RX_DMACX_CAM_LMACID(x)			(x << 49)
#define  RX_DMAC_COUNT				32
#define BGX_CMR_RX_STREERING		0x300
#define  RX_TRAFFIC_STEER_RULE_COUNT		8
#define BGX_CMR_CHAN_MSK_AND		0x450
#define BGX_CMR_BIST_STATUS		0x460
#define BGX_CMR_RX_LMACS		0x468
#define BGX_CMRX_TX_FIFO_LEN		0x518
#define BGX_CMRX_TX_STAT0		0x600
#define BGX_CMRX_TX_STAT1		0x608
#define BGX_CMRX_TX_STAT2		0x610
#define BGX_CMRX_TX_STAT3		0x618
#define BGX_CMRX_TX_STAT4		0x620
#define BGX_CMRX_TX_STAT5		0x628
#define BGX_CMRX_TX_STAT6		0x630
#define BGX_CMRX_TX_STAT7		0x638
#define BGX_CMRX_TX_STAT8		0x640
#define BGX_CMRX_TX_STAT9		0x648
#define BGX_CMRX_TX_STAT10		0x650
#define BGX_CMRX_TX_STAT11		0x658
#define BGX_CMRX_TX_STAT12		0x660
#define BGX_CMRX_TX_STAT13		0x668
#define BGX_CMRX_TX_STAT14		0x670
#define BGX_CMRX_TX_STAT15		0x678
#define BGX_CMRX_TX_STAT16		0x680
#define BGX_CMRX_TX_STAT17		0x688
#define BGX_CMR_TX_LMACS		0x1000

#define BGX_SPUX_CONTROL1		0x10000
#define  SPU_CTL_LOW_POWER			BIT_ULL(11)
#define  SPU_CTL_LOOPBACK			BIT_ULL(14)
#define  SPU_CTL_RESET				BIT_ULL(15)
#define BGX_SPUX_STATUS1		0x10008
#define  SPU_STATUS1_RCV_LNK			BIT_ULL(2)
#define BGX_SPUX_STATUS2		0x10020
#define  SPU_STATUS2_RCVFLT			BIT_ULL(10)
#define BGX_SPUX_BX_STATUS		0x10028
#define  SPU_BX_STATUS_RX_ALIGN			BIT_ULL(12)
#define BGX_SPUX_BR_STATUS1		0x10030
#define  SPU_BR_STATUS_BLK_LOCK			BIT_ULL(0)
#define  SPU_BR_STATUS_RCV_LNK			BIT_ULL(12)
#define BGX_SPUX_BR_PMD_CRTL		0x10068
#define  SPU_PMD_CRTL_TRAIN_EN			BIT_ULL(1)
#define BGX_SPUX_BR_PMD_LP_CUP		0x10078
#define BGX_SPUX_BR_PMD_LD_CUP		0x10088
#define BGX_SPUX_BR_PMD_LD_REP		0x10090
#define BGX_SPUX_FEC_CONTROL		0x100A0
#define  SPU_FEC_CTL_FEC_EN			BIT_ULL(0)
#define  SPU_FEC_CTL_ERR_EN			BIT_ULL(1)
#define BGX_SPUX_AN_CONTROL		0x100C8
#define  SPU_AN_CTL_AN_EN			BIT_ULL(12)
#define  SPU_AN_CTL_XNP_EN			BIT_ULL(13)
#define BGX_SPUX_AN_ADV			0x100D8
#define BGX_SPUX_MISC_CONTROL		0x10218
#define  SPU_MISC_CTL_INTLV_RDISP		BIT_ULL(10)
#define  SPU_MISC_CTL_RX_DIS			BIT_ULL(12)
#define BGX_SPUX_INT			0x10220	/* +(0..3) << 20 */
#define BGX_SPUX_INT_W1S		0x10228
#define BGX_SPUX_INT_ENA_W1C		0x10230
#define BGX_SPUX_INT_ENA_W1S		0x10238
#define BGX_SPU_DBG_CONTROL		0x10300
#define  SPU_DBG_CTL_AN_ARB_LINK_CHK_EN		BIT_ULL(18)
#define  SPU_DBG_CTL_AN_NONCE_MCT_DIS		BIT_ULL(29)

#define BGX_SMUX_RX_INT			0x20000
#define BGX_SMUX_RX_JABBER		0x20030
#define BGX_SMUX_RX_CTL			0x20048
#define  SMU_RX_CTL_STATUS			(3ull << 0)
#define BGX_SMUX_TX_APPEND		0x20100
#define  SMU_TX_APPEND_FCS_D			BIT_ULL(2)
#define BGX_SMUX_TX_PAUSE_PKT_TIME	0x20110
#define BGX_SMUX_TX_MIN_PKT		0x20118
#define BGX_SMUX_TX_PAUSE_PKT_INTERVAL	0x20120
#define BGX_SMUX_TX_PAUSE_ZERO		0x20138
#define BGX_SMUX_TX_INT			0x20140
#define BGX_SMUX_TX_CTL			0x20178
#define  SMU_TX_CTL_DIC_EN			BIT_ULL(0)
#define  SMU_TX_CTL_UNI_EN			BIT_ULL(1)
#define  SMU_TX_CTL_LNK_STATUS			(3ull << 4)
#define BGX_SMUX_TX_THRESH		0x20180
#define BGX_SMUX_CTL			0x20200
#define  SMU_CTL_RX_IDLE			BIT_ULL(0)
#define  SMU_CTL_TX_IDLE			BIT_ULL(1)
#define	BGX_SMUX_CBFC_CTL		0x20218
#define	RX_EN					BIT_ULL(0)
#define	TX_EN					BIT_ULL(1)
#define	BCK_EN					BIT_ULL(2)
#define	DRP_EN					BIT_ULL(3)

#define BGX_GMP_PCS_MRX_CTL		0x30000
#define	 PCS_MRX_CTL_RST_AN			BIT_ULL(9)
#define	 PCS_MRX_CTL_PWR_DN			BIT_ULL(11)
#define	 PCS_MRX_CTL_AN_EN			BIT_ULL(12)
#define	 PCS_MRX_CTL_LOOPBACK1			BIT_ULL(14)
#define	 PCS_MRX_CTL_RESET			BIT_ULL(15)
#define BGX_GMP_PCS_MRX_STATUS		0x30008
#define	 PCS_MRX_STATUS_LINK			BIT_ULL(2)
#define	 PCS_MRX_STATUS_AN_CPT			BIT_ULL(5)
#define BGX_GMP_PCS_ANX_ADV		0x30010
#define BGX_GMP_PCS_ANX_AN_RESULTS	0x30020
#define BGX_GMP_PCS_LINKX_TIMER		0x30040
#define PCS_LINKX_TIMER_COUNT			0x1E84
#define BGX_GMP_PCS_SGM_AN_ADV		0x30068
#define BGX_GMP_PCS_MISCX_CTL		0x30078
#define  PCS_MISC_CTL_MODE			BIT_ULL(8)
#define  PCS_MISC_CTL_DISP_EN			BIT_ULL(13)
#define  PCS_MISC_CTL_GMX_ENO			BIT_ULL(11)
#define  PCS_MISC_CTL_SAMP_PT_MASK	0x7Full
#define BGX_GMP_GMI_PRTX_CFG		0x38020
#define  GMI_PORT_CFG_SPEED			BIT_ULL(1)
#define  GMI_PORT_CFG_DUPLEX			BIT_ULL(2)
#define  GMI_PORT_CFG_SLOT_TIME			BIT_ULL(3)
#define  GMI_PORT_CFG_SPEED_MSB			BIT_ULL(8)
#define BGX_GMP_GMI_RXX_JABBER		0x38038
#define BGX_GMP_GMI_TXX_THRESH		0x38210
#define BGX_GMP_GMI_TXX_APPEND		0x38218
#define BGX_GMP_GMI_TXX_SLOT		0x38220
#define BGX_GMP_GMI_TXX_BURST		0x38228
#define BGX_GMP_GMI_TXX_MIN_PKT		0x38240
#define BGX_GMP_GMI_TXX_SGMII_CTL	0x38300

#define BGX_MSIX_VEC_0_29_ADDR		0x400000 /* +(0..29) << 4 */
#define BGX_MSIX_VEC_0_29_CTL		0x400008
#define BGX_MSIX_PBA_0			0x4F0000

/* MSI-X interrupts */
#define BGX_MSIX_VECTORS	30
#define BGX_LMAC_VEC_OFFSET	7
#define BGX_MSIX_VEC_SHIFT	4

#define CMRX_INT		0
#define SPUX_INT		1
#define SMUX_RX_INT		2
#define SMUX_TX_INT		3
#define GMPX_PCS_INT		4
#define GMPX_GMI_RX_INT		5
#define GMPX_GMI_TX_INT		6
#define CMR_MEM_INT		28
#define SPU_MEM_INT		29

#define LMAC_INTR_LINK_UP	BIT(0)
#define LMAC_INTR_LINK_DOWN	BIT(1)

/*  RX_DMAC_CTL configuration*/
enum MCAST_MODE {
		MCAST_MODE_REJECT,
		MCAST_MODE_ACCEPT,
		MCAST_MODE_CAM_FILTER,
		RSVD
};

#define BCAST_ACCEPT	1
#define CAM_ACCEPT	1

void octeon_mdiobus_force_mod_depencency(void);
void bgx_lmac_rx_tx_enable(int node, int bgx_idx, int lmacid, bool enable);
void bgx_add_dmac_addr(u64 dmac, int node, int bgx_idx, int lmac);
unsigned bgx_get_map(int node);
int bgx_get_lmac_count(int node, int bgx);
const u8 *bgx_get_lmac_mac(int node, int bgx_idx, int lmacid);
void bgx_set_lmac_mac(int node, int bgx_idx, int lmacid, const u8 *mac);
void bgx_get_lmac_link_state(int node, int bgx_idx, int lmacid, void *status);
void bgx_lmac_internal_loopback(int node, int bgx_idx,
				int lmac_idx, bool enable);
void bgx_lmac_get_pfc(int node, int bgx_idx, int lmacid, void *pause);
void bgx_lmac_set_pfc(int node, int bgx_idx, int lmacid, void *pause);

void xcv_init_hw(void);
void xcv_setup_link(bool link_up, int link_speed);

u64 bgx_get_rx_stats(int node, int bgx_idx, int lmac, int idx);
u64 bgx_get_tx_stats(int node, int bgx_idx, int lmac, int idx);
#define BGX_RX_STATS_COUNT 11
#define BGX_TX_STATS_COUNT 18

struct bgx_stats {
	u64 rx_stats[BGX_RX_STATS_COUNT];
	u64 tx_stats[BGX_TX_STATS_COUNT];
};

enum LMAC_TYPE {
	BGX_MODE_SGMII = 0, /* 1 lane, 1.250 Gbaud */
	BGX_MODE_XAUI = 1,  /* 4 lanes, 3.125 Gbaud */
	BGX_MODE_DXAUI = 1, /* 4 lanes, 6.250 Gbaud */
	BGX_MODE_RXAUI = 2, /* 2 lanes, 6.250 Gbaud */
	BGX_MODE_XFI = 3,   /* 1 lane, 10.3125 Gbaud */
	BGX_MODE_XLAUI = 4, /* 4 lanes, 10.3125 Gbaud */
	BGX_MODE_10G_KR = 3,/* 1 lane, 10.3125 Gbaud */
	BGX_MODE_40G_KR = 4,/* 4 lanes, 10.3125 Gbaud */
	BGX_MODE_RGMII = 5,
	BGX_MODE_QSGMII = 6,
	BGX_MODE_INVALID = 7,
};

#endif /* THUNDER_BGX_H */
