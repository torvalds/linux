/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2020  Realtek Corporation
 */

#ifndef __RTW89_TXRX_H__
#define __RTW89_TXRX_H__

#include "debug.h"

#define DATA_RATE_MODE_CTRL_MASK	GENMASK(8, 7)
#define DATA_RATE_NOT_HT_IDX_MASK	GENMASK(3, 0)
#define DATA_RATE_MODE_NON_HT		0x0
#define DATA_RATE_HT_IDX_MASK		GENMASK(4, 0)
#define DATA_RATE_MODE_HT		0x1
#define DATA_RATE_VHT_HE_NSS_MASK	GENMASK(6, 4)
#define DATA_RATE_VHT_HE_IDX_MASK	GENMASK(3, 0)
#define DATA_RATE_MODE_VHT		0x2
#define DATA_RATE_MODE_HE		0x3
#define GET_DATA_RATE_MODE(r)		FIELD_GET(DATA_RATE_MODE_CTRL_MASK, r)
#define GET_DATA_RATE_NOT_HT_IDX(r)	FIELD_GET(DATA_RATE_NOT_HT_IDX_MASK, r)
#define GET_DATA_RATE_HT_IDX(r)		FIELD_GET(DATA_RATE_HT_IDX_MASK, r)
#define GET_DATA_RATE_VHT_HE_IDX(r)	FIELD_GET(DATA_RATE_VHT_HE_IDX_MASK, r)
#define GET_DATA_RATE_NSS(r)		FIELD_GET(DATA_RATE_VHT_HE_NSS_MASK, r)

/* TX WD BODY DWORD 0 */
#define RTW89_TXWD_BODY0_WP_OFFSET GENMASK(31, 24)
#define RTW89_TXWD_BODY0_MORE_DATA BIT(23)
#define RTW89_TXWD_BODY0_WD_INFO_EN BIT(22)
#define RTW89_TXWD_BODY0_FW_DL BIT(20)
#define RTW89_TXWD_BODY0_CHANNEL_DMA GENMASK(19, 16)
#define RTW89_TXWD_BODY0_HDR_LLC_LEN GENMASK(15, 11)
#define RTW89_TXWD_BODY0_WD_PAGE BIT(7)
#define RTW89_TXWD_BODY0_HW_AMSDU BIT(5)

/* TX WD BODY DWORD 1 */
#define RTW89_TXWD_BODY1_PAYLOAD_ID GENMASK(31, 16)

/* TX WD BODY DWORD 2 */
#define RTW89_TXWD_BODY2_MACID GENMASK(30, 24)
#define RTW89_TXWD_BODY2_TID_INDICATE BIT(23)
#define RTW89_TXWD_BODY2_QSEL GENMASK(22, 17)
#define RTW89_TXWD_BODY2_TXPKT_SIZE GENMASK(13, 0)

/* TX WD BODY DWORD 3 */
#define RTW89_TXWD_BODY3_BK BIT(13)
#define RTW89_TXWD_BODY3_AGG_EN BIT(12)
#define RTW89_TXWD_BODY3_SW_SEQ GENMASK(11, 0)

/* TX WD BODY DWORD 4 */

/* TX WD BODY DWORD 5 */

/* TX WD INFO DWORD 0 */
#define RTW89_TXWD_INFO0_USE_RATE BIT(30)
#define RTW89_TXWD_INFO0_DATA_BW GENMASK(29, 28)
#define RTW89_TXWD_INFO0_GI_LTF GENMASK(27, 25)
#define RTW89_TXWD_INFO0_DATA_RATE GENMASK(24, 16)
#define RTW89_TXWD_INFO0_DISDATAFB BIT(10)

/* TX WD INFO DWORD 1 */
#define RTW89_TXWD_INFO1_DATA_RTY_LOWEST_RATE GENMASK(24, 16)
#define RTW89_TXWD_INFO1_A_CTRL_BSR BIT(14)
#define RTW89_TXWD_INFO1_MAX_AGGNUM GENMASK(7, 0)

/* TX WD INFO DWORD 2 */
#define RTW89_TXWD_INFO2_AMPDU_DENSITY GENMASK(20, 18)
#define RTW89_TXWD_INFO2_SEC_TYPE GENMASK(12, 9)
#define RTW89_TXWD_INFO2_SEC_HW_ENC BIT(8)
#define RTW89_TXWD_INFO2_SEC_CAM_IDX GENMASK(7, 0)

/* TX WD INFO DWORD 3 */

/* TX WD INFO DWORD 4 */
#define RTW89_TXWD_INFO4_RTS_EN BIT(27)
#define RTW89_TXWD_INFO4_HW_RTS_EN BIT(31)

/* TX WD INFO DWORD 5 */

/* RX DESC helpers */
/* Short Descriptor */
#define RTW89_GET_RXWD_LONG_RXD(rxdesc) \
	le32_get_bits((rxdesc)->dword0, BIT(31))
#define RTW89_GET_RXWD_DRV_INFO_SIZE(rxdesc) \
	le32_get_bits((rxdesc)->dword0, GENMASK(30, 28))
#define RTW89_GET_RXWD_RPKT_TYPE(rxdesc) \
	le32_get_bits((rxdesc)->dword0, GENMASK(27, 24))
#define RTW89_GET_RXWD_MAC_INFO_VALID(rxdesc) \
	le32_get_bits((rxdesc)->dword0, BIT(23))
#define RTW89_GET_RXWD_BB_SEL(rxdesc) \
	le32_get_bits((rxdesc)->dword0, BIT(22))
#define RTW89_GET_RXWD_HD_IV_LEN(rxdesc) \
	le32_get_bits((rxdesc)->dword0, GENMASK(21, 16))
#define RTW89_GET_RXWD_SHIFT(rxdesc) \
	le32_get_bits((rxdesc)->dword0, GENMASK(15, 14))
#define RTW89_GET_RXWD_PKT_SIZE(rxdesc) \
	le32_get_bits((rxdesc)->dword0, GENMASK(13, 0))
#define RTW89_GET_RXWD_BW(rxdesc) \
	le32_get_bits((rxdesc)->dword1, GENMASK(31, 30))
#define RTW89_GET_RXWD_GI_LTF(rxdesc) \
	le32_get_bits((rxdesc)->dword1, GENMASK(27, 25))
#define RTW89_GET_RXWD_DATA_RATE(rxdesc) \
	le32_get_bits((rxdesc)->dword1, GENMASK(24, 16))
#define RTW89_GET_RXWD_USER_ID(rxdesc) \
	le32_get_bits((rxdesc)->dword1, GENMASK(15, 8))
#define RTW89_GET_RXWD_SR_EN(rxdesc) \
	le32_get_bits((rxdesc)->dword1, BIT(7))
#define RTW89_GET_RXWD_PPDU_CNT(rxdesc) \
	le32_get_bits((rxdesc)->dword1, GENMASK(6, 4))
#define RTW89_GET_RXWD_PPDU_TYPE(rxdesc) \
	le32_get_bits((rxdesc)->dword1, GENMASK(3, 0))
#define RTW89_GET_RXWD_FREE_RUN_CNT(rxdesc) \
	le32_get_bits((rxdesc)->dword2, GENMASK(31, 0))
#define RTW89_GET_RXWD_ICV_ERR(rxdesc) \
	le32_get_bits((rxdesc)->dword3, BIT(10))
#define RTW89_GET_RXWD_CRC32_ERR(rxdesc) \
	le32_get_bits((rxdesc)->dword3, BIT(9))
#define RTW89_GET_RXWD_HW_DEC(rxdesc) \
	le32_get_bits((rxdesc)->dword3, BIT(2))
#define RTW89_GET_RXWD_SW_DEC(rxdesc) \
	le32_get_bits((rxdesc)->dword3, BIT(1))
#define RTW89_GET_RXWD_A1_MATCH(rxdesc) \
	le32_get_bits((rxdesc)->dword3, BIT(0))

/* Long Descriptor */
#define RTW89_GET_RXWD_FRAG(rxdesc) \
	le32_get_bits((rxdesc)->dword4, GENMASK(31, 28))
#define RTW89_GET_RXWD_SEQ(rxdesc) \
	le32_get_bits((rxdesc)->dword4, GENMASK(27, 16))
#define RTW89_GET_RXWD_TYPE(rxdesc) \
	le32_get_bits((rxdesc)->dword4, GENMASK(1, 0))
#define RTW89_GET_RXWD_ADDR_CAM_VLD(rxdesc) \
	le32_get_bits((rxdesc)->dword5, BIT(28))
#define RTW89_GET_RXWD_RX_PL_ID(rxdesc) \
	le32_get_bits((rxdesc)->dword5, GENMASK(27, 24))
#define RTW89_GET_RXWD_MAC_ID(rxdesc) \
	le32_get_bits((rxdesc)->dword5, GENMASK(23, 16))
#define RTW89_GET_RXWD_ADDR_CAM_ID(rxdesc) \
	le32_get_bits((rxdesc)->dword5, GENMASK(15, 8))
#define RTW89_GET_RXWD_SEC_CAM_ID(rxdesc) \
	le32_get_bits((rxdesc)->dword5, GENMASK(7, 0))

#define RTW89_GET_RXINFO_USR_NUM(rpt) \
	le32_get_bits(*((__le32 *)rpt), GENMASK(3, 0))
#define RTW89_GET_RXINFO_FW_DEFINE(rpt) \
	le32_get_bits(*((__le32 *)rpt), GENMASK(15, 8))
#define RTW89_GET_RXINFO_LSIG_LEN(rpt) \
	le32_get_bits(*((__le32 *)rpt), GENMASK(27, 16))
#define RTW89_GET_RXINFO_IS_TO_SELF(rpt) \
	le32_get_bits(*((__le32 *)rpt), BIT(28))
#define RTW89_GET_RXINFO_RX_CNT_VLD(rpt) \
	le32_get_bits(*((__le32 *)rpt), BIT(29))
#define RTW89_GET_RXINFO_LONG_RXD(rpt) \
	le32_get_bits(*((__le32 *)rpt), GENMASK(31, 30))
#define RTW89_GET_RXINFO_SERVICE(rpt) \
	le32_get_bits(*((__le32 *)(rpt) + 1), GENMASK(15, 0))
#define RTW89_GET_RXINFO_PLCP_LEN(rpt) \
	le32_get_bits(*((__le32 *)(rpt) + 1), GENMASK(23, 16))
#define RTW89_GET_RXINFO_MAC_ID_VALID(rpt, usr) \
	le32_get_bits(*((__le32 *)(rpt) + (usr) + 2), BIT(0))
#define RTW89_GET_RXINFO_DATA(rpt, usr) \
	le32_get_bits(*((__le32 *)(rpt) + (usr) + 2), BIT(1))
#define RTW89_GET_RXINFO_CTRL(rpt, usr) \
	le32_get_bits(*((__le32 *)(rpt) + (usr) + 2), BIT(2))
#define RTW89_GET_RXINFO_MGMT(rpt, usr) \
	le32_get_bits(*((__le32 *)(rpt) + (usr) + 2), BIT(3))
#define RTW89_GET_RXINFO_BCM(rpt, usr) \
	le32_get_bits(*((__le32 *)(rpt) + (usr) + 2), BIT(4))
#define RTW89_GET_RXINFO_MACID(rpt, usr) \
	le32_get_bits(*((__le32 *)(rpt) + (usr) + 2), GENMASK(15, 8))

#define RTW89_GET_PHY_STS_RSSI_A(sts) \
	le32_get_bits(*((__le32 *)(sts) + 1), GENMASK(7, 0))
#define RTW89_GET_PHY_STS_RSSI_B(sts) \
	le32_get_bits(*((__le32 *)(sts) + 1), GENMASK(15, 8))
#define RTW89_GET_PHY_STS_RSSI_C(sts) \
	le32_get_bits(*((__le32 *)(sts) + 1), GENMASK(23, 16))
#define RTW89_GET_PHY_STS_RSSI_D(sts) \
	le32_get_bits(*((__le32 *)(sts) + 1), GENMASK(31, 24))
#define RTW89_GET_PHY_STS_LEN(sts) \
	le32_get_bits(*((__le32 *)sts), GENMASK(15, 8))
#define RTW89_GET_PHY_STS_RSSI_AVG(sts) \
	le32_get_bits(*((__le32 *)sts), GENMASK(31, 24))
#define RTW89_GET_PHY_STS_IE_TYPE(ie) \
	le32_get_bits(*((__le32 *)ie), GENMASK(4, 0))
#define RTW89_GET_PHY_STS_IE_LEN(ie) \
	le32_get_bits(*((__le32 *)ie), GENMASK(11, 5))
#define RTW89_GET_PHY_STS_IE0_CFO(ie) \
	le32_get_bits(*((__le32 *)(ie) + 1), GENMASK(31, 20))

enum rtw89_tx_channel {
	RTW89_TXCH_ACH0	= 0,
	RTW89_TXCH_ACH1	= 1,
	RTW89_TXCH_ACH2	= 2,
	RTW89_TXCH_ACH3	= 3,
	RTW89_TXCH_ACH4	= 4,
	RTW89_TXCH_ACH5	= 5,
	RTW89_TXCH_ACH6	= 6,
	RTW89_TXCH_ACH7	= 7,
	RTW89_TXCH_CH8	= 8,  /* MGMT Band 0 */
	RTW89_TXCH_CH9	= 9,  /* HI Band 0 */
	RTW89_TXCH_CH10	= 10, /* MGMT Band 1 */
	RTW89_TXCH_CH11	= 11, /* HI Band 1 */
	RTW89_TXCH_CH12	= 12, /* FW CMD */

	/* keep last */
	RTW89_TXCH_NUM,
	RTW89_TXCH_MAX = RTW89_TXCH_NUM - 1
};

enum rtw89_rx_channel {
	RTW89_RXCH_RXQ	= 0,
	RTW89_RXCH_RPQ	= 1,

	/* keep last */
	RTW89_RXCH_NUM,
	RTW89_RXCH_MAX = RTW89_RXCH_NUM - 1
};

enum rtw89_tx_qsel {
	RTW89_TX_QSEL_BE_0		= 0x00,
	RTW89_TX_QSEL_BK_0		= 0x01,
	RTW89_TX_QSEL_VI_0		= 0x02,
	RTW89_TX_QSEL_VO_0		= 0x03,
	RTW89_TX_QSEL_BE_1		= 0x04,
	RTW89_TX_QSEL_BK_1		= 0x05,
	RTW89_TX_QSEL_VI_1		= 0x06,
	RTW89_TX_QSEL_VO_1		= 0x07,
	RTW89_TX_QSEL_BE_2		= 0x08,
	RTW89_TX_QSEL_BK_2		= 0x09,
	RTW89_TX_QSEL_VI_2		= 0x0a,
	RTW89_TX_QSEL_VO_2		= 0x0b,
	RTW89_TX_QSEL_BE_3		= 0x0c,
	RTW89_TX_QSEL_BK_3		= 0x0d,
	RTW89_TX_QSEL_VI_3		= 0x0e,
	RTW89_TX_QSEL_VO_3		= 0x0f,
	RTW89_TX_QSEL_B0_BCN		= 0x10,
	RTW89_TX_QSEL_B0_HI		= 0x11,
	RTW89_TX_QSEL_B0_MGMT		= 0x12,
	RTW89_TX_QSEL_B0_NOPS		= 0x13,
	RTW89_TX_QSEL_B0_MGMT_FAST	= 0x14,
	/* reserved */
	/* reserved */
	/* reserved */
	RTW89_TX_QSEL_B1_BCN		= 0x18,
	RTW89_TX_QSEL_B1_HI		= 0x19,
	RTW89_TX_QSEL_B1_MGMT		= 0x1a,
	RTW89_TX_QSEL_B1_NOPS		= 0x1b,
	RTW89_TX_QSEL_B1_MGMT_FAST	= 0x1c,
	/* reserved */
	/* reserved */
	/* reserved */
};

enum rtw89_phy_status_ie_type {
	RTW89_PHYSTS_IE00_CMN_CCK			= 0,
	RTW89_PHYSTS_IE01_CMN_OFDM			= 1,
	RTW89_PHYSTS_IE02_CMN_EXT_AX			= 2,
	RTW89_PHYSTS_IE03_CMN_EXT_SEG_1			= 3,
	RTW89_PHYSTS_IE04_CMN_EXT_PATH_A		= 4,
	RTW89_PHYSTS_IE05_CMN_EXT_PATH_B		= 5,
	RTW89_PHYSTS_IE06_CMN_EXT_PATH_C		= 6,
	RTW89_PHYSTS_IE07_CMN_EXT_PATH_D		= 7,
	RTW89_PHYSTS_IE08_FTR_CH			= 8,
	RTW89_PHYSTS_IE09_FTR_PLCP_0			= 9,
	RTW89_PHYSTS_IE10_FTR_PLCP_EXT			= 10,
	RTW89_PHYSTS_IE11_FTR_PLCP_HISTOGRAM		= 11,
	RTW89_PHYSTS_IE12_MU_EIGEN_INFO			= 12,
	RTW89_PHYSTS_IE13_DL_MU_DEF			= 13,
	RTW89_PHYSTS_IE14_TB_UL_CQI			= 14,
	RTW89_PHYSTS_IE15_TB_UL_DEF			= 15,
	RTW89_PHYSTS_IE16_RSVD16			= 16,
	RTW89_PHYSTS_IE17_TB_UL_CTRL			= 17,
	RTW89_PHYSTS_IE18_DBG_OFDM_FD_CMN		= 18,
	RTW89_PHYSTS_IE19_DBG_OFDM_TD_CMN		= 19,
	RTW89_PHYSTS_IE20_DBG_OFDM_FD_USER_SEG_0	= 20,
	RTW89_PHYSTS_IE21_DBG_OFDM_FD_USER_SEG_1	= 21,
	RTW89_PHYSTS_IE22_DBG_OFDM_FD_USER_AGC		= 22,
	RTW89_PHYSTS_IE23_RSVD23			= 23,
	RTW89_PHYSTS_IE24_DBG_OFDM_TD_PATH_A		= 24,
	RTW89_PHYSTS_IE25_DBG_OFDM_TD_PATH_B		= 25,
	RTW89_PHYSTS_IE26_DBG_OFDM_TD_PATH_C		= 26,
	RTW89_PHYSTS_IE27_DBG_OFDM_TD_PATH_D		= 27,
	RTW89_PHYSTS_IE28_DBG_CCK_PATH_A		= 28,
	RTW89_PHYSTS_IE29_DBG_CCK_PATH_B		= 29,
	RTW89_PHYSTS_IE30_DBG_CCK_PATH_C		= 30,
	RTW89_PHYSTS_IE31_DBG_CCK_PATH_D		= 31,

	/* keep last */
	RTW89_PHYSTS_IE_NUM,
	RTW89_PHYSTS_IE_MAX = RTW89_PHYSTS_IE_NUM - 1
};

static inline u8 rtw89_core_get_qsel(struct rtw89_dev *rtwdev, u8 tid)
{
	switch (tid) {
	default:
		rtw89_warn(rtwdev, "Should use tag 1d: %d\n", tid);
		fallthrough;
	case 0:
	case 3:
		return RTW89_TX_QSEL_BE_0;
	case 1:
	case 2:
		return RTW89_TX_QSEL_BK_0;
	case 4:
	case 5:
		return RTW89_TX_QSEL_VI_0;
	case 6:
	case 7:
		return RTW89_TX_QSEL_VO_0;
	}
}

static inline u8 rtw89_core_get_ch_dma(struct rtw89_dev *rtwdev, u8 qsel)
{
	switch (qsel) {
	default:
		rtw89_warn(rtwdev, "Cannot map qsel to dma: %d\n", qsel);
		fallthrough;
	case RTW89_TX_QSEL_BE_0:
		return RTW89_TXCH_ACH0;
	case RTW89_TX_QSEL_BK_0:
		return RTW89_TXCH_ACH1;
	case RTW89_TX_QSEL_VI_0:
		return RTW89_TXCH_ACH2;
	case RTW89_TX_QSEL_VO_0:
		return RTW89_TXCH_ACH3;
	case RTW89_TX_QSEL_B0_MGMT:
		return RTW89_TXCH_CH8;
	case RTW89_TX_QSEL_B0_HI:
		return RTW89_TXCH_CH9;
	case RTW89_TX_QSEL_B1_MGMT:
		return RTW89_TXCH_CH10;
	case RTW89_TX_QSEL_B1_HI:
		return RTW89_TXCH_CH11;
	}
}

static inline u8 rtw89_core_get_tid_indicate(struct rtw89_dev *rtwdev, u8 tid)
{
	switch (tid) {
	case 3:
	case 2:
	case 5:
	case 7:
		return 1;
	default:
		rtw89_warn(rtwdev, "Should use tag 1d: %d\n", tid);
		fallthrough;
	case 0:
	case 1:
	case 4:
	case 6:
		return 0;
	}
}

#endif
