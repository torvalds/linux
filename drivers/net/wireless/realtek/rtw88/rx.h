/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTW_RX_H_
#define __RTW_RX_H_

enum rtw_rx_desc_enc {
	RX_DESC_ENC_NONE	= 0,
	RX_DESC_ENC_WEP40	= 1,
	RX_DESC_ENC_TKIP_WO_MIC	= 2,
	RX_DESC_ENC_TKIP_MIC	= 3,
	RX_DESC_ENC_AES		= 4,
	RX_DESC_ENC_WEP104	= 5,
};

#define GET_RX_DESC_PHYST(rxdesc)                                              \
	le32_get_bits(*((__le32 *)(rxdesc) + 0x00), BIT(26))
#define GET_RX_DESC_ICV_ERR(rxdesc)                                            \
	le32_get_bits(*((__le32 *)(rxdesc) + 0x00), BIT(15))
#define GET_RX_DESC_CRC32(rxdesc)                                              \
	le32_get_bits(*((__le32 *)(rxdesc) + 0x00), BIT(14))
#define GET_RX_DESC_SWDEC(rxdesc)                                              \
	le32_get_bits(*((__le32 *)(rxdesc) + 0x00), BIT(27))
#define GET_RX_DESC_C2H(rxdesc)                                                \
	le32_get_bits(*((__le32 *)(rxdesc) + 0x02), BIT(28))
#define GET_RX_DESC_PKT_LEN(rxdesc)                                            \
	le32_get_bits(*((__le32 *)(rxdesc) + 0x00), GENMASK(13, 0))
#define GET_RX_DESC_DRV_INFO_SIZE(rxdesc)                                      \
	le32_get_bits(*((__le32 *)(rxdesc) + 0x00), GENMASK(19, 16))
#define GET_RX_DESC_SHIFT(rxdesc)                                              \
	le32_get_bits(*((__le32 *)(rxdesc) + 0x00), GENMASK(25, 24))
#define GET_RX_DESC_ENC_TYPE(rxdesc)                                           \
	le32_get_bits(*((__le32 *)(rxdesc) + 0x00), GENMASK(22, 20))
#define GET_RX_DESC_RX_RATE(rxdesc)                                            \
	le32_get_bits(*((__le32 *)(rxdesc) + 0x03), GENMASK(6, 0))
#define GET_RX_DESC_MACID(rxdesc)                                              \
	le32_get_bits(*((__le32 *)(rxdesc) + 0x01), GENMASK(6, 0))
#define GET_RX_DESC_PPDU_CNT(rxdesc)                                           \
	le32_get_bits(*((__le32 *)(rxdesc) + 0x02), GENMASK(30, 29))
#define GET_RX_DESC_TSFL(rxdesc)                                               \
	le32_get_bits(*((__le32 *)(rxdesc) + 0x05), GENMASK(31, 0))
#define GET_RX_DESC_BW(rxdesc)                                                 \
	(le32_get_bits(*((__le32 *)(rxdesc) + 0x04), GENMASK(5, 4)))

void rtw_rx_stats(struct rtw_dev *rtwdev, struct ieee80211_vif *vif,
		  struct sk_buff *skb);
void rtw_rx_fill_rx_status(struct rtw_dev *rtwdev,
			   struct rtw_rx_pkt_stat *pkt_stat,
			   struct ieee80211_hdr *hdr,
			   struct ieee80211_rx_status *rx_status,
			   u8 *phy_status);
void rtw_update_rx_freq_from_ie(struct rtw_dev *rtwdev, struct sk_buff *skb,
				struct ieee80211_rx_status *rx_status,
				struct rtw_rx_pkt_stat *pkt_stat);

static inline
void rtw_update_rx_freq_for_invalid(struct rtw_dev *rtwdev, struct sk_buff *skb,
				    struct ieee80211_rx_status *rx_status,
				    struct rtw_rx_pkt_stat *pkt_stat)
{
	if (pkt_stat->channel_invalid)
		rtw_update_rx_freq_from_ie(rtwdev, skb, rx_status, pkt_stat);
}


#endif
