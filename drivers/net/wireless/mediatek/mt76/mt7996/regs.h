/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#ifndef __MT7996_REGS_H
#define __MT7996_REGS_H

struct __map {
	u32 phys;
	u32 mapped;
	u32 size;
};

struct __base {
	u32 band_base[__MT_MAX_BAND];
};

/* used to differentiate between generations */
struct mt7996_reg_desc {
	const struct __base *base;
	const u32 *offs_rev;
	const struct __map *map;
	u32 map_size;
};

enum base_rev {
	WF_AGG_BASE,
	WF_ARB_BASE,
	WF_TMAC_BASE,
	WF_RMAC_BASE,
	WF_DMA_BASE,
	WF_WTBLOFF_BASE,
	WF_ETBF_BASE,
	WF_LPON_BASE,
	WF_MIB_BASE,
	WF_RATE_BASE,
	__MT_REG_BASE_MAX,
};

#define __BASE(_id, _band)			(dev->reg.base[(_id)].band_base[(_band)])

enum offs_rev {
	MIB_RVSR0,
	MIB_RVSR1,
	MIB_BTSCR5,
	MIB_BTSCR6,
	MIB_RSCR1,
	MIB_RSCR27,
	MIB_RSCR28,
	MIB_RSCR29,
	MIB_RSCR30,
	MIB_RSCR31,
	MIB_RSCR33,
	MIB_RSCR35,
	MIB_RSCR36,
	MIB_BSCR0,
	MIB_BSCR1,
	MIB_BSCR2,
	MIB_BSCR3,
	MIB_BSCR4,
	MIB_BSCR5,
	MIB_BSCR6,
	MIB_BSCR7,
	MIB_BSCR17,
	MIB_TRDR1,
	__MT_OFFS_MAX,
};

#define __OFFS(id)			(dev->reg.offs_rev[(id)])

/* RRO TOP */
#define MT_RRO_TOP_BASE				0xA000
#define MT_RRO_TOP(ofs)				(MT_RRO_TOP_BASE + (ofs))

#define MT_RRO_BA_BITMAP_BASE0			MT_RRO_TOP(0x8)
#define MT_RRO_BA_BITMAP_BASE1			MT_RRO_TOP(0xC)
#define WF_RRO_AXI_MST_CFG			MT_RRO_TOP(0xB8)
#define WF_RRO_AXI_MST_CFG_DIDX_OK		BIT(12)
#define MT_RRO_ADDR_ARRAY_BASE1			MT_RRO_TOP(0x34)
#define MT_RRO_ADDR_ARRAY_ELEM_ADDR_SEG_MODE	BIT(31)

#define MT_RRO_IND_CMD_SIGNATURE_BASE0		MT_RRO_TOP(0x38)
#define MT_RRO_IND_CMD_SIGNATURE_BASE1		MT_RRO_TOP(0x3C)
#define MT_RRO_IND_CMD_0_CTRL0			MT_RRO_TOP(0x40)
#define MT_RRO_IND_CMD_SIGNATURE_BASE1_EN	BIT(31)

#define MT_RRO_PARTICULAR_CFG0			MT_RRO_TOP(0x5C)
#define MT_RRO_PARTICULAR_CFG1			MT_RRO_TOP(0x60)
#define MT_RRO_PARTICULAR_CONFG_EN		BIT(31)
#define MT_RRO_PARTICULAR_SID			GENMASK(30, 16)

#define MT_RRO_BA_BITMAP_BASE_EXT0		MT_RRO_TOP(0x70)
#define MT_RRO_BA_BITMAP_BASE_EXT1		MT_RRO_TOP(0x74)
#define MT_RRO_HOST_INT_ENA			MT_RRO_TOP(0x204)
#define MT_RRO_HOST_INT_ENA_HOST_RRO_DONE_ENA   BIT(0)

#define MT_RRO_ADDR_ELEM_SEG_ADDR0		MT_RRO_TOP(0x400)

#define MT_RRO_ACK_SN_CTRL			MT_RRO_TOP(0x50)
#define MT_RRO_ACK_SN_CTRL_SN_MASK		GENMASK(27, 16)
#define MT_RRO_ACK_SN_CTRL_SESSION_MASK		GENMASK(11, 0)

#define MT_RRO_DBG_RD_CTRL			MT_RRO_TOP(0xe0)
#define MT_RRO_DBG_RD_ADDR			GENMASK(15, 0)
#define MT_RRO_DBG_RD_EXEC			BIT(31)

#define MT_RRO_DBG_RDAT_DW(_n)			MT_RRO_TOP(0xf0 + (_n) * 0x4)

#define MT_MCU_INT_EVENT			0x2108
#define MT_MCU_INT_EVENT_DMA_STOPPED		BIT(0)
#define MT_MCU_INT_EVENT_DMA_INIT		BIT(1)
#define MT_MCU_INT_EVENT_RESET_DONE		BIT(3)

/* PLE */
#define MT_PLE_BASE				0x820c0000
#define MT_PLE(ofs)				(MT_PLE_BASE + (ofs))

#define MT_FL_Q_EMPTY				MT_PLE(0x360)
#define MT_FL_Q0_CTRL				MT_PLE(0x3e0)
#define MT_FL_Q2_CTRL				MT_PLE(0x3e8)
#define MT_FL_Q3_CTRL				MT_PLE(0x3ec)

#define MT_PLE_FREEPG_CNT			MT_PLE(0x380)
#define MT_PLE_FREEPG_HEAD_TAIL			MT_PLE(0x384)
#define MT_PLE_PG_HIF_GROUP			MT_PLE(0x00c)
#define MT_PLE_HIF_PG_INFO			MT_PLE(0x388)

#define MT_PLE_AC_QEMPTY(ac, n)			MT_PLE(0x600 +	0x80 * (ac) + ((n) << 2))
#define MT_PLE_AMSDU_PACK_MSDU_CNT(n)		MT_PLE(0x10e0 + ((n) << 2))

/* WF MDP TOP */
#define MT_MDP_BASE				0x820cc000
#define MT_MDP(ofs)				(MT_MDP_BASE + (ofs))

#define MT_MDP_DCR2				MT_MDP(0x8e8)
#define MT_MDP_DCR2_RX_TRANS_SHORT		BIT(2)

/* TMAC: band 0(0x820e4000), band 1(0x820f4000), band 2(0x830e4000) */
#define MT_WF_TMAC_BASE(_band)			__BASE(WF_TMAC_BASE, (_band))
#define MT_WF_TMAC(_band, ofs)			(MT_WF_TMAC_BASE(_band) + (ofs))

#define MT_TMAC_TCR0(_band)			MT_WF_TMAC(_band, 0)
#define MT_TMAC_TCR0_TX_BLINK			GENMASK(7, 6)

#define MT_TMAC_CDTR(_band)			MT_WF_TMAC(_band, 0x0c8)
#define MT_TMAC_ODTR(_band)			MT_WF_TMAC(_band, 0x0cc)
#define MT_TIMEOUT_VAL_PLCP			GENMASK(15, 0)
#define MT_TIMEOUT_VAL_CCA			GENMASK(31, 16)

#define MT_TMAC_ICR0(_band)			MT_WF_TMAC(_band, 0x014)
#define MT_IFS_EIFS_OFDM			GENMASK(8, 0)
#define MT_IFS_RIFS				GENMASK(14, 10)
#define MT_IFS_SIFS				GENMASK(22, 16)
#define MT_IFS_SLOT				GENMASK(30, 24)

#define MT_TMAC_ICR1(_band)			MT_WF_TMAC(_band, 0x018)
#define MT_IFS_EIFS_CCK				GENMASK(8, 0)

/* WF DMA TOP: band 0(0x820e7000), band 1(0x820f7000), band 2(0x830e7000) */
#define MT_WF_DMA_BASE(_band)			__BASE(WF_DMA_BASE, (_band))
#define MT_WF_DMA(_band, ofs)			(MT_WF_DMA_BASE(_band) + (ofs))

#define MT_DMA_DCR0(_band)			MT_WF_DMA(_band, 0x000)
#define MT_DMA_DCR0_RXD_G5_EN			BIT(23)

#define MT_DMA_TCRF1(_band)			MT_WF_DMA(_band, 0x054)
#define MT_DMA_TCRF1_QIDX			GENMASK(15, 13)

/* WTBLOFF TOP: band 0(0x820e9000), band 1(0x820f9000), band 2(0x830e9000) */
#define MT_WTBLOFF_BASE(_band)			__BASE(WF_WTBLOFF_BASE, (_band))
#define MT_WTBLOFF(_band, ofs)			(MT_WTBLOFF_BASE(_band) + (ofs))

#define MT_WTBLOFF_RSCR(_band)			MT_WTBLOFF(_band, 0x008)
#define MT_WTBLOFF_RSCR_RCPI_MODE		GENMASK(31, 30)
#define MT_WTBLOFF_RSCR_RCPI_PARAM		GENMASK(25, 24)

#define MT_WTBLOFF_ACR(_band)			MT_WTBLOFF(_band, 0x010)
#define MT_WTBLOFF_ADM_BACKOFFTIME		BIT(29)

/* ETBF: band 0(0x820ea000), band 1(0x820fa000), band 2(0x830ea000) */
#define MT_WF_ETBF_BASE(_band)			__BASE(WF_ETBF_BASE, (_band))
#define MT_WF_ETBF(_band, ofs)			(MT_WF_ETBF_BASE(_band) + (ofs))

#define MT_ETBF_RX_FB_CONT(_band)		MT_WF_ETBF(_band, 0x100)
#define MT_ETBF_RX_FB_BW			GENMASK(10, 8)
#define MT_ETBF_RX_FB_NC			GENMASK(7, 4)
#define MT_ETBF_RX_FB_NR			GENMASK(3, 0)

/* LPON: band 0(0x820eb000), band 1(0x820fb000), band 2(0x830eb000) */
#define MT_WF_LPON_BASE(_band)			__BASE(WF_LPON_BASE, (_band))
#define MT_WF_LPON(_band, ofs)			(MT_WF_LPON_BASE(_band) + (ofs))

#define MT_LPON_UTTR0(_band)			MT_WF_LPON(_band, 0x360)
#define MT_LPON_UTTR1(_band)			MT_WF_LPON(_band, 0x364)
#define MT_LPON_FRCR(_band)			MT_WF_LPON(_band, 0x37c)

#define MT_LPON_TCR(_band, n)			MT_WF_LPON(_band, 0x0a8 + (((n) * 4) << 4))
#define MT_LPON_TCR_SW_MODE			GENMASK(1, 0)
#define MT_LPON_TCR_SW_WRITE			BIT(0)
#define MT_LPON_TCR_SW_ADJUST			BIT(1)
#define MT_LPON_TCR_SW_READ			GENMASK(1, 0)

/* MIB: band 0(0x820ed000), band 1(0x820fd000), band 2(0x830ed000)*/
/* These counters are (mostly?) clear-on-read.  So, some should not
 * be read at all in case firmware is already reading them.  These
 * are commented with 'DNR' below. The DNR stats will be read by querying
 * the firmware API for the appropriate message.  For counters the driver
 * does read, the driver should accumulate the counters.
 */
#define MT_WF_MIB_BASE(_band)			__BASE(WF_MIB_BASE, (_band))
#define MT_WF_MIB(_band, ofs)			(MT_WF_MIB_BASE(_band) + (ofs))

#define MT_MIB_BSCR0(_band)			MT_WF_MIB(_band, __OFFS(MIB_BSCR0))
#define MT_MIB_BSCR1(_band)			MT_WF_MIB(_band, __OFFS(MIB_BSCR1))
#define MT_MIB_BSCR2(_band)			MT_WF_MIB(_band, __OFFS(MIB_BSCR2))
#define MT_MIB_BSCR3(_band)			MT_WF_MIB(_band, __OFFS(MIB_BSCR3))
#define MT_MIB_BSCR4(_band)			MT_WF_MIB(_band, __OFFS(MIB_BSCR4))
#define MT_MIB_BSCR5(_band)			MT_WF_MIB(_band, __OFFS(MIB_BSCR5))
#define MT_MIB_BSCR6(_band)			MT_WF_MIB(_band, __OFFS(MIB_BSCR6))
#define MT_MIB_BSCR7(_band)			MT_WF_MIB(_band, __OFFS(MIB_BSCR7))
#define MT_MIB_BSCR17(_band)			MT_WF_MIB(_band, __OFFS(MIB_BSCR17))

#define MT_MIB_TSCR5(_band)			MT_WF_MIB(_band, 0x6c4)
#define MT_MIB_TSCR6(_band)			MT_WF_MIB(_band, 0x6c8)
#define MT_MIB_TSCR7(_band)			MT_WF_MIB(_band, 0x6d0)

#define MT_MIB_RSCR1(_band)			MT_WF_MIB(_band, __OFFS(MIB_RSCR1))
/* rx mpdu counter, full 32 bits */
#define MT_MIB_RSCR31(_band)			MT_WF_MIB(_band, __OFFS(MIB_RSCR31))
#define MT_MIB_RSCR33(_band)			MT_WF_MIB(_band, __OFFS(MIB_RSCR33))

#define MT_MIB_SDR6(_band)			MT_WF_MIB(_band, 0x020)
#define MT_MIB_SDR6_CHANNEL_IDL_CNT_MASK	GENMASK(15, 0)

#define MT_MIB_RVSR0(_band)			MT_WF_MIB(_band, __OFFS(MIB_RVSR0))

#define MT_MIB_RSCR35(_band)			MT_WF_MIB(_band, __OFFS(MIB_RSCR35))
#define MT_MIB_RSCR36(_band)			MT_WF_MIB(_band, __OFFS(MIB_RSCR36))

/* tx ampdu cnt, full 32 bits */
#define MT_MIB_TSCR0(_band)			MT_WF_MIB(_band, 0x6b0)
#define MT_MIB_TSCR2(_band)			MT_WF_MIB(_band, 0x6b8)

/* counts all mpdus in ampdu, regardless of success */
#define MT_MIB_TSCR3(_band)			MT_WF_MIB(_band, 0x6bc)

/* counts all successfully tx'd mpdus in ampdu */
#define MT_MIB_TSCR4(_band)			MT_WF_MIB(_band, 0x6c0)

/* rx ampdu count, 32-bit */
#define MT_MIB_RSCR27(_band)			MT_WF_MIB(_band, __OFFS(MIB_RSCR27))

/* rx ampdu bytes count, 32-bit */
#define MT_MIB_RSCR28(_band)			MT_WF_MIB(_band, __OFFS(MIB_RSCR28))

/* rx ampdu valid subframe count */
#define MT_MIB_RSCR29(_band)			MT_WF_MIB(_band, __OFFS(MIB_RSCR29))

/* rx ampdu valid subframe bytes count, 32bits */
#define MT_MIB_RSCR30(_band)			MT_WF_MIB(_band, __OFFS(MIB_RSCR30))

/* remaining windows protected stats */
#define MT_MIB_SDR27(_band)			MT_WF_MIB(_band, 0x080)
#define MT_MIB_SDR27_TX_RWP_FAIL_CNT		GENMASK(15, 0)

#define MT_MIB_SDR28(_band)			MT_WF_MIB(_band, 0x084)
#define MT_MIB_SDR28_TX_RWP_NEED_CNT		GENMASK(15, 0)

#define MT_MIB_RVSR1(_band)			MT_WF_MIB(_band, __OFFS(MIB_RVSR1))

/* rx blockack count, 32 bits */
#define MT_MIB_TSCR1(_band)			MT_WF_MIB(_band, 0x6b4)

#define MT_MIB_BTSCR0(_band)			MT_WF_MIB(_band, 0x5e0)
#define MT_MIB_BTSCR5(_band)			MT_WF_MIB(_band, __OFFS(MIB_BTSCR5))
#define MT_MIB_BTSCR6(_band)			MT_WF_MIB(_band, __OFFS(MIB_BTSCR6))

#define MT_MIB_BFTFCR(_band)			MT_WF_MIB(_band, 0x5d0)

#define MT_TX_AGG_CNT(_band, n)			MT_WF_MIB(_band, __OFFS(MIB_TRDR1) + ((n) << 2))
#define MT_MIB_ARNG(_band, n)			MT_WF_MIB(_band, 0x0b0 + ((n) << 2))
#define MT_MIB_ARNCR_RANGE(val, n)		(((val) >> ((n) << 4)) & GENMASK(9, 0))

/* UMIB */
#define MT_WF_UMIB_BASE				0x820cd000
#define MT_WF_UMIB(ofs)				(MT_WF_UMIB_BASE + (ofs))

#define MT_UMIB_RPDCR(_band)			(MT_WF_UMIB(0x594) + (_band) * 0x164)

/* WTBLON TOP */
#define MT_WTBLON_TOP_BASE			0x820d4000
#define MT_WTBLON_TOP(ofs)			(MT_WTBLON_TOP_BASE + (ofs))
#define MT_WTBLON_TOP_WDUCR			MT_WTBLON_TOP(0x370)
#define MT_WTBLON_TOP_WDUCR_GROUP		GENMASK(4, 0)

#define MT_WTBL_UPDATE				MT_WTBLON_TOP(0x380)
#define MT_WTBL_UPDATE_WLAN_IDX			GENMASK(11, 0)
#define MT_WTBL_UPDATE_ADM_COUNT_CLEAR		BIT(14)
#define MT_WTBL_UPDATE_BUSY			BIT(31)

#define MT_WTBL_ITCR				MT_WTBLON_TOP(0x3b0)
#define MT_WTBL_ITCR_WR				BIT(16)
#define MT_WTBL_ITCR_EXEC			BIT(31)
#define MT_WTBL_ITDR0				MT_WTBLON_TOP(0x3b8)
#define MT_WTBL_ITDR1				MT_WTBLON_TOP(0x3bc)
#define MT_WTBL_SPE_IDX_SEL			BIT(6)

/* WTBL */
#define MT_WTBL_BASE				0x820d8000
#define MT_WTBL_LMAC_ID				GENMASK(14, 8)
#define MT_WTBL_LMAC_DW				GENMASK(7, 2)
#define MT_WTBL_LMAC_OFFS(_id, _dw)		(MT_WTBL_BASE | \
						 FIELD_PREP(MT_WTBL_LMAC_ID, _id) | \
						 FIELD_PREP(MT_WTBL_LMAC_DW, _dw))

/* AGG: band 0(0x820e2000), band 1(0x820f2000), band 2(0x830e2000) */
#define MT_WF_AGG_BASE(_band)			__BASE(WF_AGG_BASE, (_band))
#define MT_WF_AGG(_band, ofs)			(MT_WF_AGG_BASE(_band) + (ofs))

#define MT_AGG_ACR4(_band)			MT_WF_AGG(_band, 0x3c)
#define MT_AGG_ACR_PPDU_TXS2H			BIT(1)

/* ARB: band 0(0x820e3000), band 1(0x820f3000), band 2(0x830e3000) */
#define MT_WF_ARB_BASE(_band)			__BASE(WF_ARB_BASE, (_band))
#define MT_WF_ARB(_band, ofs)			(MT_WF_ARB_BASE(_band) + (ofs))

#define MT_ARB_SCR(_band)			MT_WF_ARB(_band, 0x000)
#define MT_ARB_SCR_TX_DISABLE			BIT(8)
#define MT_ARB_SCR_RX_DISABLE			BIT(9)

/* RMAC: band 0(0x820e5000), band 1(0x820f5000), band 2(0x830e5000), */
#define MT_WF_RMAC_BASE(_band)			__BASE(WF_RMAC_BASE, (_band))
#define MT_WF_RMAC(_band, ofs)			(MT_WF_RMAC_BASE(_band) + (ofs))

#define MT_WF_RFCR(_band)			MT_WF_RMAC(_band, 0x000)
#define MT_WF_RFCR_DROP_STBC_MULTI		BIT(0)
#define MT_WF_RFCR_DROP_FCSFAIL			BIT(1)
#define MT_WF_RFCR_DROP_PROBEREQ		BIT(4)
#define MT_WF_RFCR_DROP_MCAST			BIT(5)
#define MT_WF_RFCR_DROP_BCAST			BIT(6)
#define MT_WF_RFCR_DROP_MCAST_FILTERED		BIT(7)
#define MT_WF_RFCR_DROP_A3_MAC			BIT(8)
#define MT_WF_RFCR_DROP_A3_BSSID		BIT(9)
#define MT_WF_RFCR_DROP_A2_BSSID		BIT(10)
#define MT_WF_RFCR_DROP_OTHER_BEACON		BIT(11)
#define MT_WF_RFCR_DROP_FRAME_REPORT		BIT(12)
#define MT_WF_RFCR_DROP_CTL_RSV			BIT(13)
#define MT_WF_RFCR_DROP_CTS			BIT(14)
#define MT_WF_RFCR_DROP_RTS			BIT(15)
#define MT_WF_RFCR_DROP_DUPLICATE		BIT(16)
#define MT_WF_RFCR_DROP_OTHER_BSS		BIT(17)
#define MT_WF_RFCR_DROP_OTHER_UC		BIT(18)
#define MT_WF_RFCR_DROP_OTHER_TIM		BIT(19)
#define MT_WF_RFCR_DROP_NDPA			BIT(20)
#define MT_WF_RFCR_DROP_UNWANTED_CTL		BIT(21)

#define MT_WF_RFCR1(_band)			MT_WF_RMAC(_band, 0x004)
#define MT_WF_RFCR1_DROP_ACK			BIT(4)
#define MT_WF_RFCR1_DROP_BF_POLL		BIT(5)
#define MT_WF_RFCR1_DROP_BA			BIT(6)
#define MT_WF_RFCR1_DROP_CFEND			BIT(7)
#define MT_WF_RFCR1_DROP_CFACK			BIT(8)

#define MT_WF_RMAC_MIB_AIRTIME0(_band)		MT_WF_RMAC(_band, 0x0380)
#define MT_WF_RMAC_MIB_RXTIME_CLR		BIT(31)
#define MT_WF_RMAC_MIB_ED_OFFSET		GENMASK(20, 16)
#define MT_WF_RMAC_MIB_OBSS_BACKOFF		GENMASK(15, 0)

#define MT_WF_RMAC_MIB_AIRTIME1(_band)		MT_WF_RMAC(_band, 0x0384)
#define MT_WF_RMAC_MIB_NONQOSD_BACKOFF		GENMASK(31, 16)

#define MT_WF_RMAC_MIB_AIRTIME3(_band)		MT_WF_RMAC(_band, 0x038c)
#define MT_WF_RMAC_MIB_QOS01_BACKOFF		GENMASK(31, 0)

#define MT_WF_RMAC_MIB_AIRTIME4(_band)		MT_WF_RMAC(_band, 0x0390)
#define MT_WF_RMAC_MIB_QOS23_BACKOFF		GENMASK(31, 0)

#define MT_WF_RMAC_RSVD0(_band)			MT_WF_RMAC(_band, 0x03e0)
#define MT_WF_RMAC_RSVD0_EIFS_CLR		BIT(21)

/* RATE: band 0(0x820ee000), band 1(0x820fe000), band 2(0x830ee000) */
#define MT_WF_RATE_BASE(_band)			__BASE(WF_RATE_BASE, (_band))
#define MT_WF_RATE(_band, ofs)			(MT_WF_RATE_BASE(_band) + (ofs))

#define MT_RATE_HRCR0(_band)			MT_WF_RATE(_band, 0x050)
#define MT_RATE_HRCR0_CFEND_RATE		GENMASK(14, 0)

/* WFDMA0 */
#define MT_WFDMA0_BASE				0xd4000
#define MT_WFDMA0(ofs)				(MT_WFDMA0_BASE + (ofs))

#define MT_WFDMA0_RST				MT_WFDMA0(0x100)
#define MT_WFDMA0_RST_LOGIC_RST			BIT(4)
#define MT_WFDMA0_RST_DMASHDL_ALL_RST		BIT(5)

#define MT_WFDMA0_BUSY_ENA			MT_WFDMA0(0x13c)
#define MT_WFDMA0_BUSY_ENA_TX_FIFO0		BIT(0)
#define MT_WFDMA0_BUSY_ENA_TX_FIFO1		BIT(1)
#define MT_WFDMA0_BUSY_ENA_RX_FIFO		BIT(2)

#define MT_WFDMA0_RX_INT_PCIE_SEL		MT_WFDMA0(0x154)
#define MT_WFDMA0_RX_INT_SEL_RING3		BIT(3)
#define MT_WFDMA0_RX_INT_SEL_RING6		BIT(6)

#define MT_WFDMA0_MCU_HOST_INT_ENA		MT_WFDMA0(0x1f4)

#define MT_WFDMA0_GLO_CFG			MT_WFDMA0(0x208)
#define MT_WFDMA0_GLO_CFG_TX_DMA_EN		BIT(0)
#define MT_WFDMA0_GLO_CFG_RX_DMA_EN		BIT(2)
#define MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2	BIT(21)
#define MT_WFDMA0_GLO_CFG_EXT_EN		BIT(26)
#define MT_WFDMA0_GLO_CFG_OMIT_RX_INFO		BIT(27)
#define MT_WFDMA0_GLO_CFG_OMIT_TX_INFO		BIT(28)

#define MT_WFDMA0_PAUSE_RX_Q_45_TH		MT_WFDMA0(0x268)
#define MT_WFDMA0_PAUSE_RX_Q_67_TH		MT_WFDMA0(0x26c)
#define MT_WFDMA0_PAUSE_RX_Q_89_TH		MT_WFDMA0(0x270)
#define MT_WFDMA0_PAUSE_RX_Q_RRO_TH		MT_WFDMA0(0x27c)

#define WF_WFDMA0_GLO_CFG_EXT0			MT_WFDMA0(0x2b0)
#define WF_WFDMA0_GLO_CFG_EXT0_RX_WB_RXD	BIT(18)
#define WF_WFDMA0_GLO_CFG_EXT0_WED_MERGE_MODE	BIT(14)

#define WF_WFDMA0_GLO_CFG_EXT1			MT_WFDMA0(0x2b4)
#define WF_WFDMA0_GLO_CFG_EXT1_CALC_MODE	BIT(31)
#define WF_WFDMA0_GLO_CFG_EXT1_TX_FCTRL_MODE	BIT(28)

#define MT_WFDMA0_RST_DTX_PTR			MT_WFDMA0(0x20c)
#define MT_WFDMA0_PRI_DLY_INT_CFG0		MT_WFDMA0(0x2f0)
#define MT_WFDMA0_PRI_DLY_INT_CFG1		MT_WFDMA0(0x2f4)
#define MT_WFDMA0_PRI_DLY_INT_CFG2		MT_WFDMA0(0x2f8)

/* WFDMA1 */
#define MT_WFDMA1_BASE				0xd5000

/* WFDMA CSR */
#define MT_WFDMA_EXT_CSR_BASE			0xd7000
#define MT_WFDMA_EXT_CSR(ofs)			(MT_WFDMA_EXT_CSR_BASE + (ofs))

#define MT_WFDMA_HOST_CONFIG			MT_WFDMA_EXT_CSR(0x30)
#define MT_WFDMA_HOST_CONFIG_PDMA_BAND		BIT(0)
#define MT_WFDMA_HOST_CONFIG_BAND2_PCIE1	BIT(22)

#define MT_WFDMA_EXT_CSR_HIF_MISC		MT_WFDMA_EXT_CSR(0x44)
#define MT_WFDMA_EXT_CSR_HIF_MISC_BUSY		BIT(0)

#define MT_WFDMA_AXI_R2A_CTRL			MT_WFDMA_EXT_CSR(0x500)
#define MT_WFDMA_AXI_R2A_CTRL_OUTSTAND_MASK	GENMASK(4, 0)

#define MT_PCIE_RECOG_ID			0xd7090
#define MT_PCIE_RECOG_ID_MASK			GENMASK(30, 0)
#define MT_PCIE_RECOG_ID_SEM			BIT(31)

/* WFDMA0 PCIE1 */
#define MT_WFDMA0_PCIE1_BASE			0xd8000
#define MT_WFDMA0_PCIE1(ofs)			(MT_WFDMA0_PCIE1_BASE + (ofs))

#define MT_INT_PCIE1_SOURCE_CSR_EXT		MT_WFDMA0_PCIE1(0x118)
#define MT_INT_PCIE1_MASK_CSR			MT_WFDMA0_PCIE1(0x11c)

#define MT_WFDMA0_PCIE1_BUSY_ENA		MT_WFDMA0_PCIE1(0x13c)
#define MT_WFDMA0_PCIE1_BUSY_ENA_TX_FIFO0	BIT(0)
#define MT_WFDMA0_PCIE1_BUSY_ENA_TX_FIFO1	BIT(1)
#define MT_WFDMA0_PCIE1_BUSY_ENA_RX_FIFO	BIT(2)

/* WFDMA COMMON */
#define __RXQ(q)				((q) + __MT_MCUQ_MAX)
#define __TXQ(q)				(__RXQ(q) + __MT_RXQ_MAX)

#define MT_Q_ID(q)				(dev->q_id[(q)])
#define MT_Q_BASE(q)				((dev->q_wfdma_mask >> (q)) & 0x1 ?	\
						 MT_WFDMA1_BASE : MT_WFDMA0_BASE)

#define MT_MCUQ_ID(q)				MT_Q_ID(q)
#define MT_TXQ_ID(q)				MT_Q_ID(__TXQ(q))
#define MT_RXQ_ID(q)				MT_Q_ID(__RXQ(q))

#define MT_MCUQ_RING_BASE(q)			(MT_Q_BASE(q) + 0x300)
#define MT_TXQ_RING_BASE(q)			(MT_Q_BASE(__TXQ(q)) + 0x300)
#define MT_RXQ_RING_BASE(q)			(MT_Q_BASE(__RXQ(q)) + 0x500)
#define MT_RXQ_RRO_IND_RING_BASE		MT_RRO_TOP(0x40)

#define MT_MCUQ_EXT_CTRL(q)			(MT_Q_BASE(q) +	0x600 +	\
						 MT_MCUQ_ID(q) * 0x4)
#define MT_RXQ_EXT_CTRL(q)			(MT_Q_BASE(__RXQ(q)) + 0x680 +	\
						 MT_RXQ_ID(q) * 0x4)
#define MT_TXQ_EXT_CTRL(q)			(MT_Q_BASE(__TXQ(q)) + 0x600 +	\
						 MT_TXQ_ID(q) * 0x4)

#define MT_INT_SOURCE_CSR			MT_WFDMA0(0x200)
#define MT_INT_MASK_CSR				MT_WFDMA0(0x204)

#define MT_INT1_SOURCE_CSR			MT_WFDMA0_PCIE1(0x200)
#define MT_INT1_MASK_CSR			MT_WFDMA0_PCIE1(0x204)

#define MT_INT_RX_DONE_BAND0			BIT(12)
#define MT_INT_RX_DONE_BAND1			BIT(13) /* for mt7992 */
#define MT_INT_RX_DONE_BAND2			BIT(13)
#define MT_INT_RX_DONE_WM			BIT(0)
#define MT_INT_RX_DONE_WA			BIT(1)
#define MT_INT_RX_DONE_WA_MAIN			BIT(2)
#define MT_INT_RX_DONE_WA_EXT			BIT(3) /* for mt7992 */
#define MT_INT_RX_DONE_WA_TRI			BIT(3)
#define MT_INT_RX_TXFREE_MAIN			BIT(17)
#define MT_INT_RX_TXFREE_TRI			BIT(15)
#define MT_INT_RX_TXFREE_BAND0_MT7990		BIT(14)
#define MT_INT_RX_TXFREE_BAND1_MT7990		BIT(15)
#define MT_INT_RX_DONE_BAND2_EXT		BIT(23)
#define MT_INT_RX_TXFREE_EXT			BIT(26)
#define MT_INT_MCU_CMD				BIT(29)

#define MT_INT_RX_DONE_RRO_BAND0		BIT(16)
#define MT_INT_RX_DONE_RRO_BAND1		BIT(16)
#define MT_INT_RX_DONE_RRO_BAND2		BIT(14)
#define MT_INT_RX_DONE_RRO_IND			BIT(11)
#define MT_INT_RX_DONE_MSDU_PG_BAND0		BIT(18)
#define MT_INT_RX_DONE_MSDU_PG_BAND1		BIT(19)
#define MT_INT_RX_DONE_MSDU_PG_BAND2		BIT(23)

#define MT_INT_RX(q)				(dev->q_int_mask[__RXQ(q)])
#define MT_INT_TX_MCU(q)			(dev->q_int_mask[(q)])

#define MT_INT_RX_DONE_MCU			(MT_INT_RX(MT_RXQ_MCU) |	\
						 MT_INT_RX(MT_RXQ_MCU_WA))

#define MT_INT_BAND0_RX_DONE			(MT_INT_RX(MT_RXQ_MAIN) |	\
						 MT_INT_RX(MT_RXQ_MAIN_WA) |	\
						 MT_INT_RX(MT_RXQ_TXFREE_BAND0))

#define MT_INT_BAND1_RX_DONE			(MT_INT_RX(MT_RXQ_BAND1) |	\
						 MT_INT_RX(MT_RXQ_BAND1_WA) |	\
						 MT_INT_RX(MT_RXQ_MAIN_WA) |	\
						 MT_INT_RX(MT_RXQ_TXFREE_BAND0))

#define MT_INT_BAND2_RX_DONE			(MT_INT_RX(MT_RXQ_BAND2) |	\
						 MT_INT_RX(MT_RXQ_BAND2_WA) |	\
						 MT_INT_RX(MT_RXQ_MAIN_WA) |	\
						 MT_INT_RX(MT_RXQ_TXFREE_BAND0))

#define MT_INT_RRO_RX_DONE			(MT_INT_RX(MT_RXQ_RRO_BAND0) |		\
						 MT_INT_RX(MT_RXQ_RRO_BAND1) |		\
						 MT_INT_RX(MT_RXQ_RRO_BAND2) |		\
						 MT_INT_RX(MT_RXQ_MSDU_PAGE_BAND0) |	\
						 MT_INT_RX(MT_RXQ_MSDU_PAGE_BAND1) |	\
						 MT_INT_RX(MT_RXQ_MSDU_PAGE_BAND2))

#define MT_INT_RX_DONE_ALL			(MT_INT_RX_DONE_MCU |		\
						 MT_INT_BAND0_RX_DONE |		\
						 MT_INT_BAND1_RX_DONE |		\
						 MT_INT_BAND2_RX_DONE |		\
						 MT_INT_RRO_RX_DONE)

#define MT_INT_TX_DONE_FWDL			BIT(26)
#define MT_INT_TX_DONE_MCU_WM			BIT(27)
#define MT_INT_TX_DONE_MCU_WA			BIT(22)
#define MT_INT_TX_DONE_BAND0			BIT(30)
#define MT_INT_TX_DONE_BAND1			BIT(31)
#define MT_INT_TX_DONE_BAND2			BIT(15)

#define MT_INT_TX_RX_DONE_EXT			(MT_INT_TX_DONE_BAND2 |		\
						 MT_INT_RX_DONE_BAND2_EXT |	\
						 MT_INT_RX_TXFREE_EXT)

#define MT_INT_TX_DONE_MCU			(MT_INT_TX_MCU(MT_MCUQ_WA) |	\
						 MT_INT_TX_MCU(MT_MCUQ_WM) |	\
						 MT_INT_TX_MCU(MT_MCUQ_FWDL))

#define MT_MCU_CMD				MT_WFDMA0(0x1f0)
#define MT_MCU_CMD_STOP_DMA			BIT(2)
#define MT_MCU_CMD_RESET_DONE			BIT(3)
#define MT_MCU_CMD_RECOVERY_DONE		BIT(4)
#define MT_MCU_CMD_NORMAL_STATE			BIT(5)
#define MT_MCU_CMD_ERROR_MASK			GENMASK(5, 1)

#define MT_MCU_CMD_WA_WDT			BIT(31)
#define MT_MCU_CMD_WM_WDT			BIT(30)
#define MT_MCU_CMD_WDT_MASK			GENMASK(31, 30)

/* l1/l2 remap */
#define MT_HIF_REMAP_L1				0x155024
#define MT_HIF_REMAP_L1_MASK			GENMASK(31, 16)
#define MT_HIF_REMAP_L1_OFFSET			GENMASK(15, 0)
#define MT_HIF_REMAP_L1_BASE			GENMASK(31, 16)
#define MT_HIF_REMAP_BASE_L1			0x130000

#define MT_HIF_REMAP_L2				0x1b4
#define MT_HIF_REMAP_L2_MASK			GENMASK(19, 0)
#define MT_HIF_REMAP_L2_OFFSET			GENMASK(11, 0)
#define MT_HIF_REMAP_L2_BASE			GENMASK(31, 12)
#define MT_HIF_REMAP_BASE_L2			0x1000

#define MT_INFRA_BASE				0x18000000
#define MT_WFSYS0_PHY_START			0x18400000
#define MT_WFSYS1_PHY_START			0x18800000
#define MT_WFSYS1_PHY_END			0x18bfffff
#define MT_CBTOP1_PHY_START			0x70000000
#define MT_CBTOP1_PHY_END			0x77ffffff
#define MT_CBTOP2_PHY_START			0xf0000000
#define MT_INFRA_MCU_START			0x7c000000
#define MT_INFRA_MCU_END			0x7c3fffff

/* FW MODE SYNC */
#define MT_FW_ASSERT_CNT			0x02208274
#define MT_FW_DUMP_STATE			0x02209e90

#define MT_SWDEF_BASE				0x00401400

#define MT_SWDEF(ofs)				(MT_SWDEF_BASE + (ofs))
#define MT_SWDEF_MODE				MT_SWDEF(0x3c)
#define MT_SWDEF_NORMAL_MODE			0

#define MT_SWDEF_SER_STATS			MT_SWDEF(0x040)
#define MT_SWDEF_PLE_STATS			MT_SWDEF(0x044)
#define MT_SWDEF_PLE1_STATS			MT_SWDEF(0x048)
#define MT_SWDEF_PLE_AMSDU_STATS		MT_SWDEF(0x04c)
#define MT_SWDEF_PSE_STATS			MT_SWDEF(0x050)
#define MT_SWDEF_PSE1_STATS			MT_SWDEF(0x054)
#define MT_SWDEF_LAMC_WISR6_BN0_STATS		MT_SWDEF(0x058)
#define MT_SWDEF_LAMC_WISR6_BN1_STATS		MT_SWDEF(0x05c)
#define MT_SWDEF_LAMC_WISR6_BN2_STATS		MT_SWDEF(0x060)
#define MT_SWDEF_LAMC_WISR7_BN0_STATS		MT_SWDEF(0x064)
#define MT_SWDEF_LAMC_WISR7_BN1_STATS		MT_SWDEF(0x068)
#define MT_SWDEF_LAMC_WISR7_BN2_STATS		MT_SWDEF(0x06c)

/* LED */
#define MT_LED_TOP_BASE				0x18013000
#define MT_LED_PHYS(_n)				(MT_LED_TOP_BASE + (_n))

#define MT_LED_CTRL(_n)				MT_LED_PHYS(0x00 + ((_n) * 4))
#define MT_LED_CTRL_KICK			BIT(7)
#define MT_LED_CTRL_BLINK_BAND_SEL		BIT(4)
#define MT_LED_CTRL_BLINK_MODE			BIT(2)
#define MT_LED_CTRL_POLARITY			BIT(1)

#define MT_LED_TX_BLINK(_n)			MT_LED_PHYS(0x10 + ((_n) * 4))
#define MT_LED_TX_BLINK_ON_MASK			GENMASK(7, 0)
#define MT_LED_TX_BLINK_OFF_MASK		GENMASK(15, 8)

#define MT_LED_EN(_n)				MT_LED_PHYS(0x40 + ((_n) * 4))

/* CONN DBG */
#define MT_CONN_DBG_CTL_BASE			0x18023000
#define MT_CONN_DBG_CTL(ofs)			(MT_CONN_DBG_CTL_BASE + (ofs))
#define MT_CONN_DBG_CTL_OUT_SEL			MT_CONN_DBG_CTL(0x604)
#define MT_CONN_DBG_CTL_PC_LOG_SEL		MT_CONN_DBG_CTL(0x60c)
#define MT_CONN_DBG_CTL_PC_LOG			MT_CONN_DBG_CTL(0x610)

#define MT_LED_GPIO_MUX2			0x70005058 /* GPIO 18 */
#define MT_LED_GPIO_MUX3			0x7000505C /* GPIO 26 */
#define MT_LED_GPIO_SEL_MASK			GENMASK(11, 8)

/* MT TOP */
#define MT_TOP_BASE				0xe0000
#define MT_TOP(ofs)				(MT_TOP_BASE + (ofs))

#define MT_TOP_LPCR_HOST_BAND(_band)		MT_TOP(0x10 + ((_band) * 0x10))
#define MT_TOP_LPCR_HOST_FW_OWN			BIT(0)
#define MT_TOP_LPCR_HOST_DRV_OWN		BIT(1)
#define MT_TOP_LPCR_HOST_FW_OWN_STAT		BIT(2)

#define MT_TOP_LPCR_HOST_BAND_IRQ_STAT(_band)	MT_TOP(0x14 + ((_band) * 0x10))
#define MT_TOP_LPCR_HOST_BAND_STAT		BIT(0)

#define MT_TOP_MISC				MT_TOP(0xf0)
#define MT_TOP_MISC_FW_STATE			GENMASK(2, 0)

/* ADIE */
#define MT_ADIE_CHIP_ID(_idx)			(0x0f00002c + ((_idx) << 28))
#define MT_ADIE_VERSION_MASK			GENMASK(15, 0)
#define MT_ADIE_CHIP_ID_MASK			GENMASK(31, 16)

#define MT_PAD_GPIO				0x700056f0
#define MT_PAD_GPIO_ADIE_COMB			GENMASK(16, 15)
#define MT_PAD_GPIO_2ADIE_TBTC			BIT(19)
/* for mt7992 */
#define MT_PAD_GPIO_ADIE_COMB_7992		GENMASK(17, 16)
#define MT_PAD_GPIO_ADIE_SINGLE			BIT(15)

#define MT_HW_REV				0x70010204
#define MT_HW_REV1				0x8a00

#define MT_WF_SUBSYS_RST			0x70028600

/* PCIE MAC */
#define MT_PCIE_MAC_BASE			0x74030000
#define MT_PCIE_MAC(ofs)			(MT_PCIE_MAC_BASE + (ofs))
#define MT_PCIE_MAC_INT_ENABLE			MT_PCIE_MAC(0x188)

#define MT_PCIE1_MAC_BASE			0x74090000
#define MT_PCIE1_MAC(ofs)			(MT_PCIE1_MAC_BASE + (ofs))

#define MT_PCIE1_MAC_INT_ENABLE			MT_PCIE1_MAC(0x188)

/* PHYRX CSD */
#define MT_WF_PHYRX_CSD_BASE			0x83000000
#define MT_WF_PHYRX_CSD(_band, _wf, ofs)	(MT_WF_PHYRX_CSD_BASE + \
						 ((_band) << 20) + \
						 ((_wf) << 16) + (ofs))
#define MT_WF_PHYRX_CSD_IRPI(_band, _wf)	MT_WF_PHYRX_CSD(_band, _wf, 0x1000)

/* PHYRX CTRL */
#define MT_WF_PHYRX_BAND_BASE			0x83080000
#define MT_WF_PHYRX_BAND(_band, ofs)		(MT_WF_PHYRX_BAND_BASE + \
						 ((_band) << 20) + (ofs))

#define MT_WF_PHYRX_BAND_GID_TAB_VLD0(_band)	MT_WF_PHYRX_BAND(_band, 0x1054)
#define MT_WF_PHYRX_BAND_GID_TAB_VLD1(_band)	MT_WF_PHYRX_BAND(_band, 0x1058)
#define MT_WF_PHYRX_BAND_GID_TAB_POS0(_band)	MT_WF_PHYRX_BAND(_band, 0x105c)
#define MT_WF_PHYRX_BAND_GID_TAB_POS1(_band)	MT_WF_PHYRX_BAND(_band, 0x1060)
#define MT_WF_PHYRX_BAND_GID_TAB_POS2(_band)	MT_WF_PHYRX_BAND(_band, 0x1064)
#define MT_WF_PHYRX_BAND_GID_TAB_POS3(_band)	MT_WF_PHYRX_BAND(_band, 0x1068)

#define MT_WF_PHYRX_BAND_RX_CTRL1(_band)	MT_WF_PHYRX_BAND(_band, 0x2004)
#define MT_WF_PHYRX_BAND_RX_CTRL1_IPI_EN	GENMASK(2, 0)
#define MT_WF_PHYRX_BAND_RX_CTRL1_STSCNT_EN	GENMASK(11, 9)

/* PHYRX CSD BAND */
#define MT_WF_PHYRX_CSD_BAND_RXTD12(_band)		MT_WF_PHYRX_BAND(_band, 0x8230)
#define MT_WF_PHYRX_CSD_BAND_RXTD12_IRPI_SW_CLR_ONLY	BIT(18)
#define MT_WF_PHYRX_CSD_BAND_RXTD12_IRPI_SW_CLR		BIT(29)

/* CONN MCU EXCP CON */
#define MT_MCU_WM_EXCP_BASE			0x89050000
#define MT_MCU_WM_EXCP(ofs)			(MT_MCU_WM_EXCP_BASE + (ofs))
#define MT_MCU_WM_EXCP_PC_CTRL			MT_MCU_WM_EXCP(0x100)
#define MT_MCU_WM_EXCP_PC_LOG			MT_MCU_WM_EXCP(0x104)
#define MT_MCU_WM_EXCP_LR_CTRL			MT_MCU_WM_EXCP(0x200)
#define MT_MCU_WM_EXCP_LR_LOG			MT_MCU_WM_EXCP(0x204)

/* CONN AFE CTL CON */
#define MT_AFE_CTL_BASE				0x18043000
#define MT_AFE_CTL_BAND(_band, ofs)		(MT_AFE_CTL_BASE + \
						 ((_band) * 0x1000) + (ofs))
#define MT_AFE_CTL_BAND_PLL_03(_band)		MT_AFE_CTL_BAND(_band, 0x2c)
#define MT_AFE_CTL_BAND_PLL_03_MSB_EN		BIT(1)

#endif
