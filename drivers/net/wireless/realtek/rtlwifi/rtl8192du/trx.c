// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2024  Realtek Corporation.*/

#include "../wifi.h"
#include "../base.h"
#include "../usb.h"
#include "../rtl8192d/reg.h"
#include "../rtl8192d/def.h"
#include "../rtl8192d/trx_common.h"
#include "trx.h"

void rtl92du_tx_cleanup(struct ieee80211_hw *hw, struct sk_buff *skb)
{
}

int rtl92du_tx_post_hdl(struct ieee80211_hw *hw, struct urb *urb,
			struct sk_buff *skb)
{
	return 0;
}

struct sk_buff *rtl92du_tx_aggregate_hdl(struct ieee80211_hw *hw,
					 struct sk_buff_head *list)
{
	return skb_dequeue(list);
}

static enum rtl_desc_qsel _rtl92du_hwq_to_descq(u16 queue_index)
{
	switch (queue_index) {
	case RTL_TXQ_BCN:
		return QSLT_BEACON;
	case RTL_TXQ_MGT:
		return QSLT_MGNT;
	case RTL_TXQ_VO:
		return QSLT_VO;
	case RTL_TXQ_VI:
		return QSLT_VI;
	case RTL_TXQ_BK:
		return QSLT_BK;
	default:
	case RTL_TXQ_BE:
		return QSLT_BE;
	}
}

/* For HW recovery information */
static void _rtl92du_tx_desc_checksum(__le32 *txdesc)
{
	__le16 *ptr = (__le16 *)txdesc;
	u16 checksum = 0;
	u32 index;

	/* Clear first */
	set_tx_desc_tx_desc_checksum(txdesc, 0);
	for (index = 0; index < 16; index++)
		checksum = checksum ^ le16_to_cpu(*(ptr + index));
	set_tx_desc_tx_desc_checksum(txdesc, checksum);
}

void rtl92du_tx_fill_desc(struct ieee80211_hw *hw,
			  struct ieee80211_hdr *hdr, u8 *pdesc_tx,
			  u8 *pbd_desc_tx, struct ieee80211_tx_info *info,
			  struct ieee80211_sta *sta,
			  struct sk_buff *skb,
			  u8 queue_index,
			  struct rtl_tcb_desc *tcb_desc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtlpriv);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	struct rtl_sta_info *sta_entry;
	__le16 fc = hdr->frame_control;
	u8 agg_state = RTL_AGG_STOP;
	u16 pktlen = skb->len;
	u32 rts_en, hw_rts_en;
	u8 ampdu_density = 0;
	u16 seq_number;
	__le32 *txdesc;
	u8 rate_flag;
	u8 tid;

	rtl_get_tcb_desc(hw, info, sta, skb, tcb_desc);

	txdesc = (__le32 *)skb_push(skb, RTL_TX_HEADER_SIZE);
	memset(txdesc, 0, RTL_TX_HEADER_SIZE);

	set_tx_desc_pkt_size(txdesc, pktlen);
	set_tx_desc_linip(txdesc, 0);
	set_tx_desc_pkt_offset(txdesc, RTL_DUMMY_OFFSET);
	set_tx_desc_offset(txdesc, RTL_TX_HEADER_SIZE);
	/* 5G have no CCK rate */
	if (rtlhal->current_bandtype == BAND_ON_5G)
		if (tcb_desc->hw_rate < DESC_RATE6M)
			tcb_desc->hw_rate = DESC_RATE6M;

	set_tx_desc_tx_rate(txdesc, tcb_desc->hw_rate);
	if (tcb_desc->use_shortgi || tcb_desc->use_shortpreamble)
		set_tx_desc_data_shortgi(txdesc, 1);

	if (rtlhal->macphymode == DUALMAC_DUALPHY &&
	    tcb_desc->hw_rate == DESC_RATEMCS7)
		set_tx_desc_data_shortgi(txdesc, 1);

	if (sta) {
		sta_entry = (struct rtl_sta_info *)sta->drv_priv;
		tid = ieee80211_get_tid(hdr);
		agg_state = sta_entry->tids[tid].agg.agg_state;
		ampdu_density = sta->deflink.ht_cap.ampdu_density;
	}

	if (agg_state == RTL_AGG_OPERATIONAL &&
	    info->flags & IEEE80211_TX_CTL_AMPDU) {
		set_tx_desc_agg_enable(txdesc, 1);
		set_tx_desc_max_agg_num(txdesc, 0x14);
		set_tx_desc_ampdu_density(txdesc, ampdu_density);
		tcb_desc->rts_enable = 1;
		tcb_desc->rts_rate = DESC_RATE24M;
	} else {
		set_tx_desc_agg_break(txdesc, 1);
	}
	seq_number = (le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ) >> 4;
	set_tx_desc_seq(txdesc, seq_number);

	rts_en = tcb_desc->rts_enable && !tcb_desc->cts_enable;
	hw_rts_en = tcb_desc->rts_enable || tcb_desc->cts_enable;
	set_tx_desc_rts_enable(txdesc, rts_en);
	set_tx_desc_hw_rts_enable(txdesc, hw_rts_en);
	set_tx_desc_cts2self(txdesc, tcb_desc->cts_enable);
	set_tx_desc_rts_stbc(txdesc, tcb_desc->rts_stbc);
	/* 5G have no CCK rate */
	if (rtlhal->current_bandtype == BAND_ON_5G)
		if (tcb_desc->rts_rate < DESC_RATE6M)
			tcb_desc->rts_rate = DESC_RATE6M;
	set_tx_desc_rts_rate(txdesc, tcb_desc->rts_rate);
	set_tx_desc_rts_bw(txdesc, 0);
	set_tx_desc_rts_sc(txdesc, tcb_desc->rts_sc);
	set_tx_desc_rts_short(txdesc, tcb_desc->rts_use_shortpreamble);

	rate_flag = info->control.rates[0].flags;
	if (mac->bw_40) {
		if (rate_flag & IEEE80211_TX_RC_DUP_DATA) {
			set_tx_desc_data_bw(txdesc, 1);
			set_tx_desc_tx_sub_carrier(txdesc, 3);
		} else if (rate_flag & IEEE80211_TX_RC_40_MHZ_WIDTH) {
			set_tx_desc_data_bw(txdesc, 1);
			set_tx_desc_tx_sub_carrier(txdesc, mac->cur_40_prime_sc);
		} else {
			set_tx_desc_data_bw(txdesc, 0);
			set_tx_desc_tx_sub_carrier(txdesc, 0);
		}
	} else {
		set_tx_desc_data_bw(txdesc, 0);
		set_tx_desc_tx_sub_carrier(txdesc, 0);
	}

	if (info->control.hw_key) {
		struct ieee80211_key_conf *keyconf = info->control.hw_key;

		switch (keyconf->cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
		case WLAN_CIPHER_SUITE_TKIP:
			set_tx_desc_sec_type(txdesc, 0x1);
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			set_tx_desc_sec_type(txdesc, 0x3);
			break;
		default:
			set_tx_desc_sec_type(txdesc, 0x0);
			break;
		}
	}

	set_tx_desc_pkt_id(txdesc, 0);
	set_tx_desc_queue_sel(txdesc, _rtl92du_hwq_to_descq(queue_index));
	set_tx_desc_data_rate_fb_limit(txdesc, 0x1F);
	set_tx_desc_rts_rate_fb_limit(txdesc, 0xF);
	set_tx_desc_disable_fb(txdesc, 0);
	set_tx_desc_use_rate(txdesc, tcb_desc->use_driver_rate);

	if (ieee80211_is_data_qos(fc)) {
		if (mac->rdg_en) {
			rtl_dbg(rtlpriv, COMP_SEND, DBG_TRACE,
				"Enable RDG function\n");
			set_tx_desc_rdg_enable(txdesc, 1);
			set_tx_desc_htc(txdesc, 1);
		}
		set_tx_desc_qos(txdesc, 1);
	}

	if (rtlpriv->dm.useramask) {
		set_tx_desc_rate_id(txdesc, tcb_desc->ratr_index);
		set_tx_desc_macid(txdesc, tcb_desc->mac_id);
	} else {
		set_tx_desc_rate_id(txdesc, 0xC + tcb_desc->ratr_index);
		set_tx_desc_macid(txdesc, tcb_desc->ratr_index);
	}

	if (!ieee80211_is_data_qos(fc) && ppsc->leisure_ps &&
	    ppsc->fwctrl_lps) {
		set_tx_desc_hwseq_en(txdesc, 1);
		set_tx_desc_pkt_id(txdesc, 8);
	}

	if (ieee80211_has_morefrags(fc))
		set_tx_desc_more_frag(txdesc, 1);
	if (is_multicast_ether_addr(ieee80211_get_DA(hdr)) ||
	    is_broadcast_ether_addr(ieee80211_get_DA(hdr)))
		set_tx_desc_bmc(txdesc, 1);

	set_tx_desc_own(txdesc, 1);
	set_tx_desc_last_seg(txdesc, 1);
	set_tx_desc_first_seg(txdesc, 1);
	_rtl92du_tx_desc_checksum(txdesc);

	rtl_dbg(rtlpriv, COMP_SEND, DBG_TRACE, "==>\n");
}

static void _rtl92du_config_out_ep(struct ieee80211_hw *hw, u8 num_out_pipe)
{
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u16 ep_cfg;

	rtlusb->out_queue_sel = 0;
	rtlusb->out_ep_nums = 0;

	if (rtlhal->interfaceindex == 0)
		ep_cfg = rtl_read_word(rtlpriv, REG_USB_Queue_Select_MAC0);
	else
		ep_cfg = rtl_read_word(rtlpriv, REG_USB_Queue_Select_MAC1);

	if (ep_cfg & 0x00f) {
		rtlusb->out_queue_sel |= TX_SELE_HQ;
		rtlusb->out_ep_nums++;
	}
	if (ep_cfg & 0x0f0) {
		rtlusb->out_queue_sel |= TX_SELE_NQ;
		rtlusb->out_ep_nums++;
	}
	if (ep_cfg & 0xf00) {
		rtlusb->out_queue_sel |= TX_SELE_LQ;
		rtlusb->out_ep_nums++;
	}

	switch (num_out_pipe) {
	case 3:
		rtlusb->out_queue_sel = TX_SELE_HQ | TX_SELE_NQ | TX_SELE_LQ;
		rtlusb->out_ep_nums = 3;
		break;
	case 2:
		rtlusb->out_queue_sel = TX_SELE_HQ | TX_SELE_NQ;
		rtlusb->out_ep_nums = 2;
		break;
	case 1:
		rtlusb->out_queue_sel = TX_SELE_HQ;
		rtlusb->out_ep_nums = 1;
		break;
	default:
		break;
	}
}

static void _rtl92du_one_out_ep_mapping(struct rtl_usb *rtlusb,
					struct rtl_ep_map *ep_map)
{
	ep_map->ep_mapping[RTL_TXQ_BE] = rtlusb->out_eps[0];
	ep_map->ep_mapping[RTL_TXQ_BK] = rtlusb->out_eps[0];
	ep_map->ep_mapping[RTL_TXQ_VI] = rtlusb->out_eps[0];
	ep_map->ep_mapping[RTL_TXQ_VO] = rtlusb->out_eps[0];
	ep_map->ep_mapping[RTL_TXQ_MGT] = rtlusb->out_eps[0];
	ep_map->ep_mapping[RTL_TXQ_BCN] = rtlusb->out_eps[0];
	ep_map->ep_mapping[RTL_TXQ_HI] = rtlusb->out_eps[0];
}

static void _rtl92du_two_out_ep_mapping(struct rtl_usb *rtlusb,
					struct rtl_ep_map *ep_map)
{
	ep_map->ep_mapping[RTL_TXQ_BE] = rtlusb->out_eps[1];
	ep_map->ep_mapping[RTL_TXQ_BK] = rtlusb->out_eps[1];
	ep_map->ep_mapping[RTL_TXQ_VI] = rtlusb->out_eps[0];
	ep_map->ep_mapping[RTL_TXQ_VO] = rtlusb->out_eps[0];
	ep_map->ep_mapping[RTL_TXQ_MGT] = rtlusb->out_eps[0];
	ep_map->ep_mapping[RTL_TXQ_BCN] = rtlusb->out_eps[0];
	ep_map->ep_mapping[RTL_TXQ_HI] = rtlusb->out_eps[0];
}

static void _rtl92du_three_out_ep_mapping(struct rtl_usb *rtlusb,
					  struct rtl_ep_map *ep_map)
{
	ep_map->ep_mapping[RTL_TXQ_BE] = rtlusb->out_eps[2];
	ep_map->ep_mapping[RTL_TXQ_BK] = rtlusb->out_eps[2];
	ep_map->ep_mapping[RTL_TXQ_VI] = rtlusb->out_eps[1];
	ep_map->ep_mapping[RTL_TXQ_VO] = rtlusb->out_eps[0];
	ep_map->ep_mapping[RTL_TXQ_MGT] = rtlusb->out_eps[0];
	ep_map->ep_mapping[RTL_TXQ_BCN] = rtlusb->out_eps[0];
	ep_map->ep_mapping[RTL_TXQ_HI] = rtlusb->out_eps[0];
}

static int _rtl92du_out_ep_mapping(struct ieee80211_hw *hw)
{
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	struct rtl_ep_map *ep_map = &rtlusb->ep_map;

	switch (rtlusb->out_ep_nums) {
	case 1:
		_rtl92du_one_out_ep_mapping(rtlusb, ep_map);
		break;
	case 2:
		_rtl92du_two_out_ep_mapping(rtlusb, ep_map);
		break;
	case 3:
		_rtl92du_three_out_ep_mapping(rtlusb, ep_map);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int rtl92du_endpoint_mapping(struct ieee80211_hw *hw)
{
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));

	_rtl92du_config_out_ep(hw, rtlusb->out_ep_nums);

	/* Normal chip with one IN and one OUT doesn't have interrupt IN EP. */
	if (rtlusb->out_ep_nums == 1 && rtlusb->in_ep_nums != 1)
		return -EINVAL;

	return _rtl92du_out_ep_mapping(hw);
}

u16 rtl92du_mq_to_hwq(__le16 fc, u16 mac80211_queue_index)
{
	u16 hw_queue_index;

	if (unlikely(ieee80211_is_beacon(fc))) {
		hw_queue_index = RTL_TXQ_BCN;
		goto out;
	}
	if (ieee80211_is_mgmt(fc)) {
		hw_queue_index = RTL_TXQ_MGT;
		goto out;
	}

	switch (mac80211_queue_index) {
	case 0:
		hw_queue_index = RTL_TXQ_VO;
		break;
	case 1:
		hw_queue_index = RTL_TXQ_VI;
		break;
	case 2:
		hw_queue_index = RTL_TXQ_BE;
		break;
	case 3:
		hw_queue_index = RTL_TXQ_BK;
		break;
	default:
		hw_queue_index = RTL_TXQ_BE;
		WARN_ONCE(true, "rtl8192du: QSLT_BE queue, skb_queue:%d\n",
			  mac80211_queue_index);
		break;
	}
out:
	return hw_queue_index;
}
