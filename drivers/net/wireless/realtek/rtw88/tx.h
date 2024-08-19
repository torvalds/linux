/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTW_TX_H_
#define __RTW_TX_H_

#define RTK_TX_MAX_AGG_NUM_MASK		0x1f

#define RTW_TX_PROBE_TIMEOUT		msecs_to_jiffies(500)

struct rtw_tx_desc {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
	__le32 w4;
	__le32 w5;
	__le32 w6;
	__le32 w7;
	__le32 w8;
	__le32 w9;
} __packed;

#define RTW_TX_DESC_W0_TXPKTSIZE GENMASK(15, 0)
#define RTW_TX_DESC_W0_OFFSET GENMASK(23, 16)
#define RTW_TX_DESC_W0_BMC BIT(24)
#define RTW_TX_DESC_W0_LS BIT(26)
#define RTW_TX_DESC_W0_DISQSELSEQ BIT(31)
#define RTW_TX_DESC_W1_MACID GENMASK(7, 0)
#define RTW_TX_DESC_W1_QSEL GENMASK(12, 8)
#define RTW_TX_DESC_W1_RATE_ID GENMASK(20, 16)
#define RTW_TX_DESC_W1_SEC_TYPE GENMASK(23, 22)
#define RTW_TX_DESC_W1_PKT_OFFSET GENMASK(28, 24)
#define RTW_TX_DESC_W1_MORE_DATA BIT(29)
#define RTW_TX_DESC_W2_AGG_EN BIT(12)
#define RTW_TX_DESC_W2_SPE_RPT BIT(19)
#define RTW_TX_DESC_W2_AMPDU_DEN GENMASK(22, 20)
#define RTW_TX_DESC_W2_BT_NULL BIT(23)
#define RTW_TX_DESC_W3_HW_SSN_SEL GENMASK(7, 6)
#define RTW_TX_DESC_W3_USE_RATE BIT(8)
#define RTW_TX_DESC_W3_DISDATAFB BIT(10)
#define RTW_TX_DESC_W3_USE_RTS BIT(12)
#define RTW_TX_DESC_W3_NAVUSEHDR BIT(15)
#define RTW_TX_DESC_W3_MAX_AGG_NUM GENMASK(21, 17)
#define RTW_TX_DESC_W4_DATARATE GENMASK(6, 0)
#define RTW_TX_DESC_W4_RTSRATE GENMASK(28, 24)
#define RTW_TX_DESC_W5_DATA_SHORT BIT(4)
#define RTW_TX_DESC_W5_DATA_BW GENMASK(6, 5)
#define RTW_TX_DESC_W5_DATA_LDPC BIT(7)
#define RTW_TX_DESC_W5_DATA_STBC GENMASK(9, 8)
#define RTW_TX_DESC_W5_DATA_RTS_SHORT BIT(12)
#define RTW_TX_DESC_W6_SW_DEFINE GENMASK(11, 0)
#define RTW_TX_DESC_W7_TXDESC_CHECKSUM GENMASK(15, 0)
#define RTW_TX_DESC_W7_DMA_TXAGG_NUM GENMASK(31, 24)
#define RTW_TX_DESC_W8_EN_HWSEQ BIT(15)
#define RTW_TX_DESC_W9_SW_SEQ GENMASK(23, 12)
#define RTW_TX_DESC_W9_TIM_EN BIT(7)
#define RTW_TX_DESC_W9_TIM_OFFSET GENMASK(6, 0)

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

enum rtw_rsvd_packet_type;

void rtw_tx(struct rtw_dev *rtwdev,
	    struct ieee80211_tx_control *control,
	    struct sk_buff *skb);
void rtw_txq_init(struct rtw_dev *rtwdev, struct ieee80211_txq *txq);
void rtw_txq_cleanup(struct rtw_dev *rtwdev, struct ieee80211_txq *txq);
void rtw_tx_work(struct work_struct *w);
void __rtw_tx_work(struct rtw_dev *rtwdev);
void rtw_tx_pkt_info_update(struct rtw_dev *rtwdev,
			    struct rtw_tx_pkt_info *pkt_info,
			    struct ieee80211_sta *sta,
			    struct sk_buff *skb);
void rtw_tx_fill_tx_desc(struct rtw_tx_pkt_info *pkt_info, struct sk_buff *skb);
void rtw_tx_report_enqueue(struct rtw_dev *rtwdev, struct sk_buff *skb, u8 sn);
void rtw_tx_report_handle(struct rtw_dev *rtwdev, struct sk_buff *skb, int src);
void rtw_tx_rsvd_page_pkt_info_update(struct rtw_dev *rtwdev,
				      struct rtw_tx_pkt_info *pkt_info,
				      struct sk_buff *skb,
				      enum rtw_rsvd_packet_type type);
struct sk_buff *
rtw_tx_write_data_rsvd_page_get(struct rtw_dev *rtwdev,
				struct rtw_tx_pkt_info *pkt_info,
				u8 *buf, u32 size);
struct sk_buff *
rtw_tx_write_data_h2c_get(struct rtw_dev *rtwdev,
			  struct rtw_tx_pkt_info *pkt_info,
			  u8 *buf, u32 size);

enum rtw_tx_queue_type rtw_tx_ac_to_hwq(enum ieee80211_ac_numbers ac);
enum rtw_tx_queue_type rtw_tx_queue_mapping(struct sk_buff *skb);

static inline
void fill_txdesc_checksum_common(u8 *txdesc, size_t words)
{
	__le16 chksum = 0;
	__le16 *data = (__le16 *)(txdesc);
	struct rtw_tx_desc *tx_desc = (struct rtw_tx_desc *)txdesc;

	le32p_replace_bits(&tx_desc->w7, 0, RTW_TX_DESC_W7_TXDESC_CHECKSUM);

	while (words--)
		chksum ^= *data++;

	le32p_replace_bits(&tx_desc->w7, __le16_to_cpu(chksum),
			   RTW_TX_DESC_W7_TXDESC_CHECKSUM);
}

static inline void rtw_tx_fill_txdesc_checksum(struct rtw_dev *rtwdev,
					       struct rtw_tx_pkt_info *pkt_info,
					       u8 *txdesc)
{
	const struct rtw_chip_info *chip = rtwdev->chip;

	chip->ops->fill_txdesc_checksum(rtwdev, pkt_info, txdesc);
}

#endif
