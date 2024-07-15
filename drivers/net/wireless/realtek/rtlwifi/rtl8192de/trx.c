// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../wifi.h"
#include "../pci.h"
#include "../base.h"
#include "../stats.h"
#include "../rtl8192d/reg.h"
#include "../rtl8192d/def.h"
#include "../rtl8192d/phy_common.h"
#include "../rtl8192d/trx_common.h"
#include "phy.h"
#include "trx.h"
#include "led.h"

static u8 _rtl92de_map_hwqueue_to_fwqueue(struct sk_buff *skb, u8 hw_queue)
{
	__le16 fc = rtl_get_fc(skb);

	if (unlikely(ieee80211_is_beacon(fc)))
		return QSLT_BEACON;
	if (ieee80211_is_mgmt(fc))
		return QSLT_MGNT;

	return skb->priority;
}

static void _rtl92de_insert_emcontent(struct rtl_tcb_desc *ptcb_desc,
				      u8 *virtualaddress8)
{
	__le32 *virtualaddress = (__le32 *)virtualaddress8;

	memset(virtualaddress, 0, 8);

	set_earlymode_pktnum(virtualaddress, ptcb_desc->empkt_num);
	set_earlymode_len0(virtualaddress, ptcb_desc->empkt_len[0]);
	set_earlymode_len1(virtualaddress, ptcb_desc->empkt_len[1]);
	set_earlymode_len2_1(virtualaddress, ptcb_desc->empkt_len[2] & 0xF);
	set_earlymode_len2_2(virtualaddress, ptcb_desc->empkt_len[2] >> 4);
	set_earlymode_len3(virtualaddress, ptcb_desc->empkt_len[3]);
	set_earlymode_len4(virtualaddress, ptcb_desc->empkt_len[4]);
}

void rtl92de_tx_fill_desc(struct ieee80211_hw *hw,
			  struct ieee80211_hdr *hdr, u8 *pdesc8,
			  u8 *pbd_desc_tx, struct ieee80211_tx_info *info,
			  struct ieee80211_sta *sta,
			  struct sk_buff *skb,
			  u8 hw_queue, struct rtl_tcb_desc *ptcb_desc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	__le32 *pdesc = (__le32 *)pdesc8;
	u16 seq_number;
	__le16 fc = hdr->frame_control;
	unsigned int buf_len = 0;
	unsigned int skb_len = skb->len;
	u8 fw_qsel = _rtl92de_map_hwqueue_to_fwqueue(skb, hw_queue);
	bool firstseg = ((hdr->seq_ctrl &
			cpu_to_le16(IEEE80211_SCTL_FRAG)) == 0);
	bool lastseg = ((hdr->frame_control &
			cpu_to_le16(IEEE80211_FCTL_MOREFRAGS)) == 0);
	dma_addr_t mapping;
	u8 bw_40 = 0;

	if (mac->opmode == NL80211_IFTYPE_STATION) {
		bw_40 = mac->bw_40;
	} else if (mac->opmode == NL80211_IFTYPE_AP ||
		mac->opmode == NL80211_IFTYPE_ADHOC) {
		if (sta)
			bw_40 = sta->deflink.bandwidth >= IEEE80211_STA_RX_BW_40;
	}
	seq_number = (le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ) >> 4;
	rtl_get_tcb_desc(hw, info, sta, skb, ptcb_desc);
	/* reserve 8 byte for AMPDU early mode */
	if (rtlhal->earlymode_enable) {
		skb_push(skb, EM_HDR_LEN);
		memset(skb->data, 0, EM_HDR_LEN);
	}
	buf_len = skb->len;
	mapping = dma_map_single(&rtlpci->pdev->dev, skb->data, skb->len,
				 DMA_TO_DEVICE);
	if (dma_mapping_error(&rtlpci->pdev->dev, mapping)) {
		rtl_dbg(rtlpriv, COMP_SEND, DBG_TRACE,
			"DMA mapping error\n");
		return;
	}
	clear_pci_tx_desc_content(pdesc, sizeof(struct tx_desc_92d));
	if (ieee80211_is_nullfunc(fc) || ieee80211_is_ctl(fc)) {
		firstseg = true;
		lastseg = true;
	}
	if (firstseg) {
		if (rtlhal->earlymode_enable) {
			set_tx_desc_pkt_offset(pdesc, 1);
			set_tx_desc_offset(pdesc, USB_HWDESC_HEADER_LEN +
					   EM_HDR_LEN);
			if (ptcb_desc->empkt_num) {
				rtl_dbg(rtlpriv, COMP_SEND, DBG_LOUD,
					"Insert 8 byte.pTcb->EMPktNum:%d\n",
					ptcb_desc->empkt_num);
				_rtl92de_insert_emcontent(ptcb_desc,
							  (u8 *)(skb->data));
			}
		} else {
			set_tx_desc_offset(pdesc, USB_HWDESC_HEADER_LEN);
		}
		/* 5G have no CCK rate */
		if (rtlhal->current_bandtype == BAND_ON_5G)
			if (ptcb_desc->hw_rate < DESC_RATE6M)
				ptcb_desc->hw_rate = DESC_RATE6M;
		set_tx_desc_tx_rate(pdesc, ptcb_desc->hw_rate);
		if (ptcb_desc->use_shortgi || ptcb_desc->use_shortpreamble)
			set_tx_desc_data_shortgi(pdesc, 1);

		if (rtlhal->macphymode == DUALMAC_DUALPHY &&
			ptcb_desc->hw_rate == DESC_RATEMCS7)
			set_tx_desc_data_shortgi(pdesc, 1);

		if (info->flags & IEEE80211_TX_CTL_AMPDU) {
			set_tx_desc_agg_enable(pdesc, 1);
			set_tx_desc_max_agg_num(pdesc, 0x14);
		}
		set_tx_desc_seq(pdesc, seq_number);
		set_tx_desc_rts_enable(pdesc,
				       ((ptcb_desc->rts_enable &&
					!ptcb_desc->cts_enable) ? 1 : 0));
		set_tx_desc_hw_rts_enable(pdesc, ((ptcb_desc->rts_enable
					  || ptcb_desc->cts_enable) ? 1 : 0));
		set_tx_desc_cts2self(pdesc, ((ptcb_desc->cts_enable) ? 1 : 0));
		set_tx_desc_rts_stbc(pdesc, ((ptcb_desc->rts_stbc) ? 1 : 0));
		/* 5G have no CCK rate */
		if (rtlhal->current_bandtype == BAND_ON_5G)
			if (ptcb_desc->rts_rate < DESC_RATE6M)
				ptcb_desc->rts_rate = DESC_RATE6M;
		set_tx_desc_rts_rate(pdesc, ptcb_desc->rts_rate);
		set_tx_desc_rts_bw(pdesc, 0);
		set_tx_desc_rts_sc(pdesc, ptcb_desc->rts_sc);
		set_tx_desc_rts_short(pdesc, ((ptcb_desc->rts_rate <=
			DESC_RATE54M) ?
			(ptcb_desc->rts_use_shortpreamble ? 1 : 0) :
			(ptcb_desc->rts_use_shortgi ? 1 : 0)));
		if (bw_40) {
			if (ptcb_desc->packet_bw) {
				set_tx_desc_data_bw(pdesc, 1);
				set_tx_desc_tx_sub_carrier(pdesc, 3);
			} else {
				set_tx_desc_data_bw(pdesc, 0);
				set_tx_desc_tx_sub_carrier(pdesc,
							mac->cur_40_prime_sc);
			}
		} else {
			set_tx_desc_data_bw(pdesc, 0);
			set_tx_desc_tx_sub_carrier(pdesc, 0);
		}
		set_tx_desc_linip(pdesc, 0);
		set_tx_desc_pkt_size(pdesc, (u16)skb_len);
		if (sta) {
			u8 ampdu_density = sta->deflink.ht_cap.ampdu_density;
			set_tx_desc_ampdu_density(pdesc, ampdu_density);
		}
		if (info->control.hw_key) {
			struct ieee80211_key_conf *keyconf;

			keyconf = info->control.hw_key;
			switch (keyconf->cipher) {
			case WLAN_CIPHER_SUITE_WEP40:
			case WLAN_CIPHER_SUITE_WEP104:
			case WLAN_CIPHER_SUITE_TKIP:
				set_tx_desc_sec_type(pdesc, 0x1);
				break;
			case WLAN_CIPHER_SUITE_CCMP:
				set_tx_desc_sec_type(pdesc, 0x3);
				break;
			default:
				set_tx_desc_sec_type(pdesc, 0x0);
				break;

			}
		}
		set_tx_desc_pkt_id(pdesc, 0);
		set_tx_desc_queue_sel(pdesc, fw_qsel);
		set_tx_desc_data_rate_fb_limit(pdesc, 0x1F);
		set_tx_desc_rts_rate_fb_limit(pdesc, 0xF);
		set_tx_desc_disable_fb(pdesc, ptcb_desc->disable_ratefallback ?
				       1 : 0);
		set_tx_desc_use_rate(pdesc, ptcb_desc->use_driver_rate ? 1 : 0);

		/* Set TxRate and RTSRate in TxDesc  */
		/* This prevent Tx initial rate of new-coming packets */
		/* from being overwritten by retried  packet rate.*/
		if (!ptcb_desc->use_driver_rate) {
			set_tx_desc_rts_rate(pdesc, 0x08);
			/* set_tx_desc_tx_rate(pdesc, 0x0b); */
		}
		if (ieee80211_is_data_qos(fc)) {
			if (mac->rdg_en) {
				rtl_dbg(rtlpriv, COMP_SEND, DBG_TRACE,
					"Enable RDG function\n");
				set_tx_desc_rdg_enable(pdesc, 1);
				set_tx_desc_htc(pdesc, 1);
			}
		}
	}

	set_tx_desc_first_seg(pdesc, (firstseg ? 1 : 0));
	set_tx_desc_last_seg(pdesc, (lastseg ? 1 : 0));
	set_tx_desc_tx_buffer_size(pdesc, (u16)buf_len);
	set_tx_desc_tx_buffer_address(pdesc, mapping);
	if (rtlpriv->dm.useramask) {
		set_tx_desc_rate_id(pdesc, ptcb_desc->ratr_index);
		set_tx_desc_macid(pdesc, ptcb_desc->mac_id);
	} else {
		set_tx_desc_rate_id(pdesc, 0xC + ptcb_desc->ratr_index);
		set_tx_desc_macid(pdesc, ptcb_desc->ratr_index);
	}
	if (ieee80211_is_data_qos(fc))
		set_tx_desc_qos(pdesc, 1);

	if ((!ieee80211_is_data_qos(fc)) && ppsc->fwctrl_lps) {
		set_tx_desc_hwseq_en(pdesc, 1);
		set_tx_desc_pkt_id(pdesc, 8);
	}
	set_tx_desc_more_frag(pdesc, (lastseg ? 0 : 1));
	rtl_dbg(rtlpriv, COMP_SEND, DBG_TRACE, "\n");
}

void rtl92de_tx_fill_cmddesc(struct ieee80211_hw *hw, u8 *pdesc8,
			     struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtlpriv);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u8 fw_queue = QSLT_BEACON;

	struct ieee80211_hdr *hdr = rtl_get_hdr(skb);
	__le16 fc = hdr->frame_control;
	__le32 *pdesc = (__le32 *)pdesc8;

	dma_addr_t mapping = dma_map_single(&rtlpci->pdev->dev, skb->data,
					    skb->len, DMA_TO_DEVICE);

	if (dma_mapping_error(&rtlpci->pdev->dev, mapping)) {
		rtl_dbg(rtlpriv, COMP_SEND, DBG_TRACE,
			"DMA mapping error\n");
		return;
	}
	clear_pci_tx_desc_content(pdesc, TX_DESC_SIZE);
	set_tx_desc_offset(pdesc, USB_HWDESC_HEADER_LEN);
	/* 5G have no CCK rate
	 * Caution: The macros below are multi-line expansions.
	 * The braces are needed no matter what checkpatch says
	 */
	if (rtlhal->current_bandtype == BAND_ON_5G) {
		set_tx_desc_tx_rate(pdesc, DESC_RATE6M);
	} else {
		set_tx_desc_tx_rate(pdesc, DESC_RATE1M);
	}
	set_tx_desc_seq(pdesc, 0);
	set_tx_desc_linip(pdesc, 0);
	set_tx_desc_queue_sel(pdesc, fw_queue);
	set_tx_desc_first_seg(pdesc, 1);
	set_tx_desc_last_seg(pdesc, 1);
	set_tx_desc_tx_buffer_size(pdesc, (u16)skb->len);
	set_tx_desc_tx_buffer_address(pdesc, mapping);
	set_tx_desc_rate_id(pdesc, 7);
	set_tx_desc_macid(pdesc, 0);
	set_tx_desc_pkt_size(pdesc, (u16)(skb->len));
	set_tx_desc_first_seg(pdesc, 1);
	set_tx_desc_last_seg(pdesc, 1);
	set_tx_desc_offset(pdesc, 0x20);
	set_tx_desc_use_rate(pdesc, 1);

	if (!ieee80211_is_data_qos(fc) && ppsc->fwctrl_lps) {
		set_tx_desc_hwseq_en(pdesc, 1);
		set_tx_desc_pkt_id(pdesc, 8);
	}

	RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_LOUD,
		      "H2C Tx Cmd Content", pdesc, TX_DESC_SIZE);
	wmb();
	set_tx_desc_own(pdesc, 1);
}

bool rtl92de_is_tx_desc_closed(struct ieee80211_hw *hw,
			       u8 hw_queue, u16 index)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl8192_tx_ring *ring = &rtlpci->tx_ring[hw_queue];
	u8 *entry = (u8 *)(&ring->desc[ring->idx]);
	u8 own = (u8)rtl92de_get_desc(hw, entry, true, HW_DESC_OWN);

	/* a beacon packet will only use the first
	 * descriptor by defaut, and the own bit may not
	 * be cleared by the hardware
	 */
	if (own)
		return false;
	return true;
}

void rtl92de_tx_polling(struct ieee80211_hw *hw, u8 hw_queue)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	if (hw_queue == BEACON_QUEUE)
		rtl_write_word(rtlpriv, REG_PCIE_CTRL_REG, BIT(4));
	else
		rtl_write_word(rtlpriv, REG_PCIE_CTRL_REG,
			       BIT(0) << (hw_queue));
}
