/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2019 MediaTek Inc. */

#ifndef __MT7615_MAC_H
#define __MT7615_MAC_H

#define MT_CT_PARSE_LEN			72
#define MT_CT_DMA_BUF_NUM		2

#define MT_RXD0_LENGTH			GENMASK(15, 0)
#define MT_RXD0_PKT_FLAG                GENMASK(19, 16)
#define MT_RXD0_PKT_TYPE		GENMASK(31, 29)

#define MT_RXD0_NORMAL_ETH_TYPE_OFS	GENMASK(22, 16)
#define MT_RXD0_NORMAL_IP_SUM		BIT(23)
#define MT_RXD0_NORMAL_UDP_TCP_SUM	BIT(24)
#define MT_RXD0_NORMAL_GROUP_1		BIT(25)
#define MT_RXD0_NORMAL_GROUP_2		BIT(26)
#define MT_RXD0_NORMAL_GROUP_3		BIT(27)
#define MT_RXD0_NORMAL_GROUP_4		BIT(28)

enum rx_pkt_type {
	PKT_TYPE_TXS,
	PKT_TYPE_TXRXV,
	PKT_TYPE_NORMAL,
	PKT_TYPE_RX_DUP_RFB,
	PKT_TYPE_RX_TMR,
	PKT_TYPE_RETRIEVE,
	PKT_TYPE_TXRX_NOTIFY,
	PKT_TYPE_RX_EVENT,
	PKT_TYPE_NORMAL_MCU,
};

#define MT_RXD1_NORMAL_BSSID		GENMASK(31, 26)
#define MT_RXD1_NORMAL_PAYLOAD_FORMAT	GENMASK(25, 24)
#define MT_RXD1_NORMAL_HDR_TRANS	BIT(23)
#define MT_RXD1_NORMAL_HDR_OFFSET	BIT(22)
#define MT_RXD1_NORMAL_MAC_HDR_LEN	GENMASK(21, 16)
#define MT_RXD1_NORMAL_CH_FREQ		GENMASK(15, 8)
#define MT_RXD1_NORMAL_KEY_ID		GENMASK(7, 6)
#define MT_RXD1_NORMAL_BEACON_UC	BIT(5)
#define MT_RXD1_NORMAL_BEACON_MC	BIT(4)
#define MT_RXD1_NORMAL_BF_REPORT	BIT(3)
#define MT_RXD1_NORMAL_ADDR_TYPE	GENMASK(2, 1)
#define MT_RXD1_NORMAL_BCAST		GENMASK(2, 1)
#define MT_RXD1_NORMAL_MCAST		BIT(2)
#define MT_RXD1_NORMAL_U2M		BIT(1)
#define MT_RXD1_NORMAL_HTC_VLD		BIT(0)

#define MT_RXD2_NORMAL_NON_AMPDU	BIT(31)
#define MT_RXD2_NORMAL_NON_AMPDU_SUB	BIT(30)
#define MT_RXD2_NORMAL_NDATA		BIT(29)
#define MT_RXD2_NORMAL_NULL_FRAME	BIT(28)
#define MT_RXD2_NORMAL_FRAG		BIT(27)
#define MT_RXD2_NORMAL_INT_FRAME	BIT(26)
#define MT_RXD2_NORMAL_HDR_TRANS_ERROR	BIT(25)
#define MT_RXD2_NORMAL_MAX_LEN_ERROR	BIT(24)
#define MT_RXD2_NORMAL_AMSDU_ERR	BIT(23)
#define MT_RXD2_NORMAL_LEN_MISMATCH	BIT(22)
#define MT_RXD2_NORMAL_TKIP_MIC_ERR	BIT(21)
#define MT_RXD2_NORMAL_ICV_ERR		BIT(20)
#define MT_RXD2_NORMAL_CLM		BIT(19)
#define MT_RXD2_NORMAL_CM		BIT(18)
#define MT_RXD2_NORMAL_FCS_ERR		BIT(17)
#define MT_RXD2_NORMAL_SW_BIT		BIT(16)
#define MT_RXD2_NORMAL_SEC_MODE		GENMASK(15, 12)
#define MT_RXD2_NORMAL_TID		GENMASK(11, 8)
#define MT_RXD2_NORMAL_WLAN_IDX		GENMASK(7, 0)

#define MT_RXD3_NORMAL_PF_STS		GENMASK(31, 30)
#define MT_RXD3_NORMAL_PF_MODE		BIT(29)
#define MT_RXD3_NORMAL_CLS_BITMAP	GENMASK(28, 19)
#define MT_RXD3_NORMAL_WOL		GENMASK(18, 14)
#define MT_RXD3_NORMAL_MAGIC_PKT	BIT(13)
#define MT_RXD3_NORMAL_OFLD		GENMASK(12, 11)
#define MT_RXD3_NORMAL_CLS		BIT(10)
#define MT_RXD3_NORMAL_PATTERN_DROP	BIT(9)
#define MT_RXD3_NORMAL_TSF_COMPARE_LOSS	BIT(8)
#define MT_RXD3_NORMAL_RXV_SEQ		GENMASK(7, 0)

#define MT_RXV1_ACID_DET_H		BIT(31)
#define MT_RXV1_ACID_DET_L		BIT(30)
#define MT_RXV1_VHTA2_B8_B3		GENMASK(29, 24)
#define MT_RXV1_NUM_RX			GENMASK(23, 22)
#define MT_RXV1_HT_NO_SOUND		BIT(21)
#define MT_RXV1_HT_SMOOTH		BIT(20)
#define MT_RXV1_HT_SHORT_GI		BIT(19)
#define MT_RXV1_HT_AGGR			BIT(18)
#define MT_RXV1_VHTA1_B22		BIT(17)
#define MT_RXV1_FRAME_MODE		GENMASK(16, 15)
#define MT_RXV1_TX_MODE			GENMASK(14, 12)
#define MT_RXV1_HT_EXT_LTF		GENMASK(11, 10)
#define MT_RXV1_HT_AD_CODE		BIT(9)
#define MT_RXV1_HT_STBC			GENMASK(8, 7)
#define MT_RXV1_TX_RATE			GENMASK(6, 0)

#define MT_RXV2_SEL_ANT			BIT(31)
#define MT_RXV2_VALID_BIT		BIT(30)
#define MT_RXV2_NSTS			GENMASK(29, 27)
#define MT_RXV2_GROUP_ID		GENMASK(26, 21)
#define MT_RXV2_LENGTH			GENMASK(20, 0)

#define MT_RXV4_RCPI3			GENMASK(31, 24)
#define MT_RXV4_RCPI2			GENMASK(23, 16)
#define MT_RXV4_RCPI1			GENMASK(15, 8)
#define MT_RXV4_RCPI0			GENMASK(7, 0)

#define MT_RXV6_NF3			GENMASK(31, 24)
#define MT_RXV6_NF2			GENMASK(23, 16)
#define MT_RXV6_NF1			GENMASK(15, 8)
#define MT_RXV6_NF0			GENMASK(7, 0)

enum tx_header_format {
	MT_HDR_FORMAT_802_3,
	MT_HDR_FORMAT_CMD,
	MT_HDR_FORMAT_802_11,
	MT_HDR_FORMAT_802_11_EXT,
};

enum tx_pkt_type {
	MT_TX_TYPE_CT,
	MT_TX_TYPE_SF,
	MT_TX_TYPE_CMD,
	MT_TX_TYPE_FW,
};

enum tx_pkt_queue_idx {
	MT_LMAC_AC00,
	MT_LMAC_AC01,
	MT_LMAC_AC02,
	MT_LMAC_AC03,
	MT_LMAC_ALTX0 = 0x10,
	MT_LMAC_BMC0,
	MT_LMAC_BCN0,
	MT_LMAC_PSMP0,
	MT_LMAC_ALTX1,
	MT_LMAC_BMC1,
	MT_LMAC_BCN1,
	MT_LMAC_PSMP1,
};

enum tx_port_idx {
	MT_TX_PORT_IDX_LMAC,
	MT_TX_PORT_IDX_MCU
};

enum tx_mcu_port_q_idx {
	MT_TX_MCU_PORT_RX_Q0 = 0,
	MT_TX_MCU_PORT_RX_Q1,
	MT_TX_MCU_PORT_RX_Q2,
	MT_TX_MCU_PORT_RX_Q3,
	MT_TX_MCU_PORT_RX_FWDL = 0x1e
};

enum tx_phy_bandwidth {
	MT_PHY_BW_20,
	MT_PHY_BW_40,
	MT_PHY_BW_80,
	MT_PHY_BW_160,
};

#define MT_CT_INFO_APPLY_TXD		BIT(0)
#define MT_CT_INFO_COPY_HOST_TXD_ALL	BIT(1)
#define MT_CT_INFO_MGMT_FRAME		BIT(2)
#define MT_CT_INFO_NONE_CIPHER_FRAME	BIT(3)
#define MT_CT_INFO_HSR2_TX		BIT(4)

#define MT_TXD_SIZE			(8 * 4)

#define MT_USB_TXD_SIZE			(MT_TXD_SIZE + 8 * 4)
#define MT_USB_HDR_SIZE			4
#define MT_USB_TAIL_SIZE		4

#define MT_TXD0_P_IDX			BIT(31)
#define MT_TXD0_Q_IDX			GENMASK(30, 26)
#define MT_TXD0_UDP_TCP_SUM		BIT(24)
#define MT_TXD0_IP_SUM			BIT(23)
#define MT_TXD0_ETH_TYPE_OFFSET		GENMASK(22, 16)
#define MT_TXD0_TX_BYTES		GENMASK(15, 0)

#define MT_TXD1_OWN_MAC			GENMASK(31, 26)
#define MT_TXD1_PKT_FMT			GENMASK(25, 24)
#define MT_TXD1_TID			GENMASK(23, 21)
#define MT_TXD1_AMSDU			BIT(20)
#define MT_TXD1_UNXV			BIT(19)
#define MT_TXD1_HDR_PAD			GENMASK(18, 17)
#define MT_TXD1_TXD_LEN			BIT(16)
#define MT_TXD1_LONG_FORMAT		BIT(15)
#define MT_TXD1_HDR_FORMAT		GENMASK(14, 13)
#define MT_TXD1_HDR_INFO		GENMASK(12, 8)
#define MT_TXD1_WLAN_IDX		GENMASK(7, 0)

#define MT_TXD2_FIX_RATE		BIT(31)
#define MT_TXD2_TIMING_MEASURE		BIT(30)
#define MT_TXD2_BA_DISABLE		BIT(29)
#define MT_TXD2_POWER_OFFSET		GENMASK(28, 24)
#define MT_TXD2_MAX_TX_TIME		GENMASK(23, 16)
#define MT_TXD2_FRAG			GENMASK(15, 14)
#define MT_TXD2_HTC_VLD			BIT(13)
#define MT_TXD2_DURATION		BIT(12)
#define MT_TXD2_BIP			BIT(11)
#define MT_TXD2_MULTICAST		BIT(10)
#define MT_TXD2_RTS			BIT(9)
#define MT_TXD2_SOUNDING		BIT(8)
#define MT_TXD2_NDPA			BIT(7)
#define MT_TXD2_NDP			BIT(6)
#define MT_TXD2_FRAME_TYPE		GENMASK(5, 4)
#define MT_TXD2_SUB_TYPE		GENMASK(3, 0)

#define MT_TXD3_SN_VALID		BIT(31)
#define MT_TXD3_PN_VALID		BIT(30)
#define MT_TXD3_SEQ			GENMASK(27, 16)
#define MT_TXD3_REM_TX_COUNT		GENMASK(15, 11)
#define MT_TXD3_TX_COUNT		GENMASK(10, 6)
#define MT_TXD3_PROTECT_FRAME		BIT(1)
#define MT_TXD3_NO_ACK			BIT(0)

#define MT_TXD4_PN_LOW			GENMASK(31, 0)

#define MT_TXD5_PN_HIGH			GENMASK(31, 16)
#define MT_TXD5_SW_POWER_MGMT		BIT(13)
#define MT_TXD5_DA_SELECT		BIT(11)
#define MT_TXD5_TX_STATUS_HOST		BIT(10)
#define MT_TXD5_TX_STATUS_MCU		BIT(9)
#define MT_TXD5_TX_STATUS_FMT		BIT(8)
#define MT_TXD5_PID			GENMASK(7, 0)

#define MT_TXD6_FIXED_RATE		BIT(31)
#define MT_TXD6_SGI			BIT(30)
#define MT_TXD6_LDPC			BIT(29)
#define MT_TXD6_TX_BF			BIT(28)
#define MT_TXD6_TX_RATE			GENMASK(27, 16)
#define MT_TXD6_ANT_ID			GENMASK(15, 4)
#define MT_TXD6_DYN_BW			BIT(3)
#define MT_TXD6_FIXED_BW		BIT(2)
#define MT_TXD6_BW			GENMASK(1, 0)

/* MT7663 DW7 HW-AMSDU */
#define MT_TXD7_HW_AMSDU_CAP		BIT(30)
#define MT_TXD7_TYPE			GENMASK(21, 20)
#define MT_TXD7_SUB_TYPE		GENMASK(19, 16)
#define MT_TXD7_SPE_IDX			GENMASK(15, 11)
#define MT_TXD7_SPE_IDX_SLE		BIT(10)

#define MT_TXD8_L_TYPE			GENMASK(5, 4)
#define MT_TXD8_L_SUB_TYPE		GENMASK(3, 0)

#define MT_TX_RATE_STBC			BIT(11)
#define MT_TX_RATE_NSS			GENMASK(10, 9)
#define MT_TX_RATE_MODE			GENMASK(8, 6)
#define MT_TX_RATE_IDX			GENMASK(5, 0)

#define MT_TXP_MAX_BUF_NUM		6
#define MT_HW_TXP_MAX_MSDU_NUM		4
#define MT_HW_TXP_MAX_BUF_NUM		4

#define MT_MSDU_ID_VALID		BIT(15)

#define MT_TXD_LEN_MASK			GENMASK(11, 0)
#define MT_TXD_LEN_MSDU_LAST		BIT(14)
#define MT_TXD_LEN_AMSDU_LAST		BIT(15)
/* mt7663 */
#define MT_TXD_LEN_LAST			BIT(15)

struct mt7615_txp_ptr {
	__le32 buf0;
	__le16 len0;
	__le16 len1;
	__le32 buf1;
} __packed __aligned(4);

struct mt7615_hw_txp {
	__le16 msdu_id[MT_HW_TXP_MAX_MSDU_NUM];
	struct mt7615_txp_ptr ptr[MT_HW_TXP_MAX_BUF_NUM / 2];
} __packed __aligned(4);

struct mt7615_fw_txp {
	__le16 flags;
	__le16 token;
	u8 bss_idx;
	u8 rept_wds_wcid;
	u8 rsv;
	u8 nbuf;
	__le32 buf[MT_TXP_MAX_BUF_NUM];
	__le16 len[MT_TXP_MAX_BUF_NUM];
} __packed __aligned(4);

struct mt7615_txp_common {
	union {
		struct mt7615_fw_txp fw;
		struct mt7615_hw_txp hw;
	};
};

struct mt7615_tx_free {
	__le16 rx_byte_cnt;
	__le16 ctrl;
	u8 txd_cnt;
	u8 rsv[3];
	__le16 token[];
} __packed __aligned(4);

#define MT_TX_FREE_MSDU_ID_CNT		GENMASK(6, 0)

#define MT_TXS0_PID			GENMASK(31, 24)
#define MT_TXS0_BA_ERROR		BIT(22)
#define MT_TXS0_PS_FLAG			BIT(21)
#define MT_TXS0_TXOP_TIMEOUT		BIT(20)
#define MT_TXS0_BIP_ERROR		BIT(19)

#define MT_TXS0_QUEUE_TIMEOUT		BIT(18)
#define MT_TXS0_RTS_TIMEOUT		BIT(17)
#define MT_TXS0_ACK_TIMEOUT		BIT(16)
#define MT_TXS0_ACK_ERROR_MASK		GENMASK(18, 16)

#define MT_TXS0_TX_STATUS_HOST		BIT(15)
#define MT_TXS0_TX_STATUS_MCU		BIT(14)
#define MT_TXS0_TXS_FORMAT		BIT(13)
#define MT_TXS0_FIXED_RATE		BIT(12)
#define MT_TXS0_TX_RATE			GENMASK(11, 0)

#define MT_TXS1_ANT_ID			GENMASK(31, 20)
#define MT_TXS1_RESP_RATE		GENMASK(19, 16)
#define MT_TXS1_BW			GENMASK(15, 14)
#define MT_TXS1_I_TXBF			BIT(13)
#define MT_TXS1_E_TXBF			BIT(12)
#define MT_TXS1_TID			GENMASK(11, 9)
#define MT_TXS1_AMPDU			BIT(8)
#define MT_TXS1_ACKED_MPDU		BIT(7)
#define MT_TXS1_TX_POWER_DBM		GENMASK(6, 0)

#define MT_TXS2_WCID			GENMASK(31, 24)
#define MT_TXS2_RXV_SEQNO		GENMASK(23, 16)
#define MT_TXS2_TX_DELAY		GENMASK(15, 0)

#define MT_TXS3_LAST_TX_RATE		GENMASK(31, 29)
#define MT_TXS3_TX_COUNT		GENMASK(28, 24)
#define MT_TXS3_F1_TSSI1		GENMASK(23, 12)
#define MT_TXS3_F1_TSSI0		GENMASK(11, 0)
#define MT_TXS3_F0_SEQNO		GENMASK(11, 0)

#define MT_TXS4_F0_TIMESTAMP		GENMASK(31, 0)
#define MT_TXS4_F1_TSSI3		GENMASK(23, 12)
#define MT_TXS4_F1_TSSI2		GENMASK(11, 0)

#define MT_TXS5_F0_FRONT_TIME		GENMASK(24, 0)
#define MT_TXS5_F1_NOISE_2		GENMASK(23, 16)
#define MT_TXS5_F1_NOISE_1		GENMASK(15, 8)
#define MT_TXS5_F1_NOISE_0		GENMASK(7, 0)

#define MT_TXS6_F1_RCPI_3		GENMASK(31, 24)
#define MT_TXS6_F1_RCPI_2		GENMASK(23, 16)
#define MT_TXS6_F1_RCPI_1		GENMASK(15, 8)
#define MT_TXS6_F1_RCPI_0		GENMASK(7, 0)

struct mt7615_dfs_pulse {
	u32 max_width;		/* us */
	int max_pwr;		/* dbm */
	int min_pwr;		/* dbm */
	u32 min_stgr_pri;	/* us */
	u32 max_stgr_pri;	/* us */
	u32 min_cr_pri;		/* us */
	u32 max_cr_pri;		/* us */
};

struct mt7615_dfs_pattern {
	u8 enb;
	u8 stgr;
	u8 min_crpn;
	u8 max_crpn;
	u8 min_crpr;
	u8 min_pw;
	u8 max_pw;
	u32 min_pri;
	u32 max_pri;
	u8 min_crbn;
	u8 max_crbn;
	u8 min_stgpn;
	u8 max_stgpn;
	u8 min_stgpr;
};

struct mt7615_dfs_radar_spec {
	struct mt7615_dfs_pulse pulse_th;
	struct mt7615_dfs_pattern radar_pattern[16];
};

enum mt7615_cipher_type {
	MT_CIPHER_NONE,
	MT_CIPHER_WEP40,
	MT_CIPHER_TKIP,
	MT_CIPHER_TKIP_NO_MIC,
	MT_CIPHER_AES_CCMP,
	MT_CIPHER_WEP104,
	MT_CIPHER_BIP_CMAC_128,
	MT_CIPHER_WEP128,
	MT_CIPHER_WAPI,
	MT_CIPHER_CCMP_256 = 10,
	MT_CIPHER_GCMP,
	MT_CIPHER_GCMP_256,
};

static inline enum mt7615_cipher_type
mt7615_mac_get_cipher(int cipher)
{
	switch (cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		return MT_CIPHER_WEP40;
	case WLAN_CIPHER_SUITE_WEP104:
		return MT_CIPHER_WEP104;
	case WLAN_CIPHER_SUITE_TKIP:
		return MT_CIPHER_TKIP;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		return MT_CIPHER_BIP_CMAC_128;
	case WLAN_CIPHER_SUITE_CCMP:
		return MT_CIPHER_AES_CCMP;
	case WLAN_CIPHER_SUITE_CCMP_256:
		return MT_CIPHER_CCMP_256;
	case WLAN_CIPHER_SUITE_GCMP:
		return MT_CIPHER_GCMP;
	case WLAN_CIPHER_SUITE_GCMP_256:
		return MT_CIPHER_GCMP_256;
	case WLAN_CIPHER_SUITE_SMS4:
		return MT_CIPHER_WAPI;
	default:
		return MT_CIPHER_NONE;
	}
}

static inline struct mt7615_txp_common *
mt7615_txwi_to_txp(struct mt76_dev *dev, struct mt76_txwi_cache *t)
{
	u8 *txwi;

	if (!t)
		return NULL;

	txwi = mt76_get_txwi_ptr(dev, t);

	return (struct mt7615_txp_common *)(txwi + MT_TXD_SIZE);
}

static inline u32 mt7615_mac_wtbl_addr(struct mt7615_dev *dev, int wcid)
{
	return MT_WTBL_BASE(dev) + wcid * MT_WTBL_ENTRY_SIZE;
}

#endif
