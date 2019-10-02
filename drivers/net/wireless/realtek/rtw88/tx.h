/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTW_TX_H_
#define __RTW_TX_H_

#define RTK_TX_MAX_AGG_NUM_MASK		0x1f

#define RTW_TX_PROBE_TIMEOUT		msecs_to_jiffies(500)

#define SET_TX_DESC_TXPKTSIZE(txdesc, value)                                   \
	le32p_replace_bits((__le32 *)(txdesc) + 0x00, value, GENMASK(15, 0))
#define SET_TX_DESC_OFFSET(txdesc, value)                                      \
	le32p_replace_bits((__le32 *)(txdesc) + 0x00, value, GENMASK(23, 16))
#define SET_TX_DESC_PKT_OFFSET(txdesc, value)                                  \
	le32p_replace_bits((__le32 *)(txdesc) + 0x01, value, GENMASK(28, 24))
#define SET_TX_DESC_QSEL(txdesc, value)                                        \
	le32p_replace_bits((__le32 *)(txdesc) + 0x01, value, GENMASK(12, 8))
#define SET_TX_DESC_BMC(txdesc, value)                                         \
	le32p_replace_bits((__le32 *)(txdesc) + 0x00, value, BIT(24))
#define SET_TX_DESC_RATE_ID(txdesc, value)                                     \
	le32p_replace_bits((__le32 *)(txdesc) + 0x01, value, GENMASK(20, 16))
#define SET_TX_DESC_DATARATE(txdesc, value)                                    \
	le32p_replace_bits((__le32 *)(txdesc) + 0x04, value, GENMASK(6, 0))
#define SET_TX_DESC_DISDATAFB(txdesc, value)                                   \
	le32p_replace_bits((__le32 *)(txdesc) + 0x03, value, BIT(10))
#define SET_TX_DESC_USE_RATE(txdesc, value)                                    \
	le32p_replace_bits((__le32 *)(txdesc) + 0x03, value, BIT(8))
#define SET_TX_DESC_SEC_TYPE(txdesc, value)                                    \
	le32p_replace_bits((__le32 *)(txdesc) + 0x01, value, GENMASK(23, 22))
#define SET_TX_DESC_DATA_BW(txdesc, value)                                     \
	le32p_replace_bits((__le32 *)(txdesc) + 0x05, value, GENMASK(6, 5))
#define SET_TX_DESC_SW_SEQ(txdesc, value)                                      \
	le32p_replace_bits((__le32 *)(txdesc) + 0x09, value, GENMASK(23, 12))
#define SET_TX_DESC_MAX_AGG_NUM(txdesc, value)                                 \
	le32p_replace_bits((__le32 *)(txdesc) + 0x03, value, GENMASK(21, 17))
#define SET_TX_DESC_USE_RTS(tx_desc, value)                                    \
	le32p_replace_bits((__le32 *)(txdesc) + 0x03, value, BIT(12))
#define SET_TX_DESC_AMPDU_DENSITY(txdesc, value)                               \
	le32p_replace_bits((__le32 *)(txdesc) + 0x02, value, GENMASK(22, 20))
#define SET_TX_DESC_DATA_STBC(txdesc, value)                                   \
	le32p_replace_bits((__le32 *)(txdesc) + 0x05, value, GENMASK(9, 8))
#define SET_TX_DESC_DATA_LDPC(txdesc, value)                                   \
	le32p_replace_bits((__le32 *)(txdesc) + 0x05, value, BIT(7))
#define SET_TX_DESC_AGG_EN(txdesc, value)                                      \
	le32p_replace_bits((__le32 *)(txdesc) + 0x02, value, BIT(12))
#define SET_TX_DESC_LS(txdesc, value)                                          \
	le32p_replace_bits((__le32 *)(txdesc) + 0x00, value, BIT(26))
#define SET_TX_DESC_DATA_SHORT(txdesc, value)				       \
	le32p_replace_bits((__le32 *)(txdesc) + 0x05, value, BIT(4))
#define SET_TX_DESC_SPE_RPT(tx_desc, value)                                    \
	le32p_replace_bits((__le32 *)(txdesc) + 0x02, value, BIT(19))
#define SET_TX_DESC_SW_DEFINE(tx_desc, value)                                  \
	le32p_replace_bits((__le32 *)(txdesc) + 0x06, value, GENMASK(11, 0))

enum rtw_tx_desc_queue_select {
	TX_DESC_QSEL_TID0	= 0,
	TX_DESC_QSEL_TID1	= 1,
	TX_DESC_QSEL_TID2	= 2,
	TX_DESC_QSEL_TID3	= 3,
	TX_DESC_QSEL_TID4	= 4,
	TX_DESC_QSEL_TID5	= 5,
	TX_DESC_QSEL_TID6	= 6,
	TX_DESC_QSEL_TID7	= 7,
	TX_DESC_QSEL_TID8	= 8,
	TX_DESC_QSEL_TID9	= 9,
	TX_DESC_QSEL_TID10	= 10,
	TX_DESC_QSEL_TID11	= 11,
	TX_DESC_QSEL_TID12	= 12,
	TX_DESC_QSEL_TID13	= 13,
	TX_DESC_QSEL_TID14	= 14,
	TX_DESC_QSEL_TID15	= 15,
	TX_DESC_QSEL_BEACON	= 16,
	TX_DESC_QSEL_HIGH	= 17,
	TX_DESC_QSEL_MGMT	= 18,
	TX_DESC_QSEL_H2C	= 19,
};

void rtw_tx_pkt_info_update(struct rtw_dev *rtwdev,
			    struct rtw_tx_pkt_info *pkt_info,
			    struct ieee80211_tx_control *control,
			    struct sk_buff *skb);
void rtw_tx_fill_tx_desc(struct rtw_tx_pkt_info *pkt_info, struct sk_buff *skb);
void rtw_tx_report_enqueue(struct rtw_dev *rtwdev, struct sk_buff *skb, u8 sn);
void rtw_tx_report_handle(struct rtw_dev *rtwdev, struct sk_buff *skb);
void rtw_rsvd_page_pkt_info_update(struct rtw_dev *rtwdev,
				   struct rtw_tx_pkt_info *pkt_info,
				   struct sk_buff *skb);

#endif
