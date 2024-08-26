/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2024  Realtek Corporation.*/

#ifndef __RTL92DU_TRX_H__
#define __RTL92DU_TRX_H__

#define TX_SELE_HQ				BIT(0)	/* High Queue */
#define TX_SELE_LQ				BIT(1)	/* Low Queue */
#define TX_SELE_NQ				BIT(2)	/* Normal Queue */

#define TX_TOTAL_PAGE_NUMBER_92DU			0xF8
#define TEST_PAGE_NUM_PUBQ_92DU				0x89
#define TX_TOTAL_PAGE_NUMBER_92D_DUAL_MAC		0x7A
#define NORMAL_PAGE_NUM_PUBQ_92D_DUAL_MAC		0x5A
#define NORMAL_PAGE_NUM_HPQ_92D_DUAL_MAC		0x10
#define NORMAL_PAGE_NUM_LPQ_92D_DUAL_MAC		0x10
#define NORMAL_PAGE_NUM_NORMALQ_92D_DUAL_MAC		0

#define WMM_NORMAL_TX_TOTAL_PAGE_NUMBER			0xF5

#define WMM_NORMAL_PAGE_NUM_PUBQ_92D			0x65
#define WMM_NORMAL_PAGE_NUM_HPQ_92D			0x30
#define WMM_NORMAL_PAGE_NUM_LPQ_92D			0x30
#define WMM_NORMAL_PAGE_NUM_NPQ_92D			0x30

#define WMM_NORMAL_PAGE_NUM_PUBQ_92D_DUAL_MAC		0x32
#define WMM_NORMAL_PAGE_NUM_HPQ_92D_DUAL_MAC		0x18
#define WMM_NORMAL_PAGE_NUM_LPQ_92D_DUAL_MAC		0x18
#define WMM_NORMAL_PAGE_NUM_NPQ_92D_DUAL_MAC		0x18

static inline void set_tx_desc_bmc(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits(__txdesc, __value, BIT(24));
}

static inline void set_tx_desc_agg_break(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 1), __value, BIT(6));
}

static inline void set_tx_desc_tx_desc_checksum(__le32 *__txdesc, u32 __value)
{
	le32p_replace_bits((__txdesc + 7), __value, GENMASK(15, 0));
}

void rtl92du_tx_fill_desc(struct ieee80211_hw *hw,
			  struct ieee80211_hdr *hdr, u8 *pdesc,
			  u8 *pbd_desc_tx, struct ieee80211_tx_info *info,
			  struct ieee80211_sta *sta,
			  struct sk_buff *skb, u8 hw_queue,
			  struct rtl_tcb_desc *ptcb_desc);
int rtl92du_endpoint_mapping(struct ieee80211_hw *hw);
u16 rtl92du_mq_to_hwq(__le16 fc, u16 mac80211_queue_index);
struct sk_buff *rtl92du_tx_aggregate_hdl(struct ieee80211_hw *hw,
					 struct sk_buff_head *list);
void rtl92du_tx_cleanup(struct ieee80211_hw *hw, struct sk_buff  *skb);
int rtl92du_tx_post_hdl(struct ieee80211_hw *hw, struct urb *urb,
			struct sk_buff *skb);

#endif
