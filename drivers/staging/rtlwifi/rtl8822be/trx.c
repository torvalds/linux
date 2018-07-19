// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "../wifi.h"
#include "../pci.h"
#include "../base.h"
#include "../stats.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "trx.h"
#include "led.h"
#include "fw.h"

#include <linux/vermagic.h>

static u8 _rtl8822be_map_hwqueue_to_fwqueue(struct sk_buff *skb, u8 hw_queue)
{
	switch (hw_queue) {
	case BEACON_QUEUE:
		return QSLT_BEACON;
	case H2C_QUEUE:
		return QSLT_CMD;
	case MGNT_QUEUE:
		return QSLT_MGNT;
	case HIGH_QUEUE:
		return QSLT_HIGH;
	default:
		return skb->priority;
	}
}

static void _rtl8822be_query_rxphystatus(struct ieee80211_hw *hw, u8 *phystrpt,
					 struct ieee80211_hdr *hdr,
					 struct rtl_stats *pstatus)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->phydm.ops->phydm_query_phy_status(rtlpriv, phystrpt, hdr,
						   pstatus);

	/* UI BSS List signal strength(in percentage),
	 * make it good looking, from 0~100.
	 */
	pstatus->signalstrength =
		(u8)(rtl_signal_scale_mapping(hw, pstatus->rx_pwdb_all));
}

static void _rtl8822be_translate_rx_signal_stuff(struct ieee80211_hw *hw,
						 struct sk_buff *skb,
						 struct rtl_stats *pstatus,
						 u8 *p_phystrpt)
{
	struct ieee80211_hdr *hdr;
	u8 *tmp_buf;

	tmp_buf = skb->data + pstatus->rx_drvinfo_size + pstatus->rx_bufshift +
		  24;

	hdr = (struct ieee80211_hdr *)tmp_buf;

	/* query phy status */
	_rtl8822be_query_rxphystatus(hw, p_phystrpt, hdr, pstatus);

	/* packet statistics */
	if (pstatus->packet_beacon && pstatus->packet_matchbssid)
		rtl_priv(hw)->dm.dbginfo.num_qry_beacon_pkt++;

	if (pstatus->packet_matchbssid &&
	    ieee80211_is_data_qos(hdr->frame_control) &&
	    !is_multicast_ether_addr(ieee80211_get_DA(hdr))) {
		struct ieee80211_qos_hdr *hdr_qos =
			(struct ieee80211_qos_hdr *)tmp_buf;
		u16 tid = le16_to_cpu(hdr_qos->qos_ctrl) & 0xf;

		if (tid != 0 && tid != 3)
			rtl_priv(hw)->dm.dbginfo.num_non_be_pkt++;
	}

	/* signal statistics */
	if (p_phystrpt)
		rtl_process_phyinfo(hw, tmp_buf, pstatus);
}

static void _rtl8822be_insert_emcontent(struct rtl_tcb_desc *ptcb_desc,
					u8 *virtualaddress)
{
	u32 dwtmp = 0;

	memset(virtualaddress, 0, 8);

	SET_EARLYMODE_PKTNUM(virtualaddress, ptcb_desc->empkt_num);
	if (ptcb_desc->empkt_num == 1) {
		dwtmp = ptcb_desc->empkt_len[0];
	} else {
		dwtmp = ptcb_desc->empkt_len[0];
		dwtmp += ((dwtmp % 4) ? (4 - dwtmp % 4) : 0) + 4;
		dwtmp += ptcb_desc->empkt_len[1];
	}
	SET_EARLYMODE_LEN0(virtualaddress, dwtmp);

	if (ptcb_desc->empkt_num <= 3) {
		dwtmp = ptcb_desc->empkt_len[2];
	} else {
		dwtmp = ptcb_desc->empkt_len[2];
		dwtmp += ((dwtmp % 4) ? (4 - dwtmp % 4) : 0) + 4;
		dwtmp += ptcb_desc->empkt_len[3];
	}
	SET_EARLYMODE_LEN1(virtualaddress, dwtmp);
	if (ptcb_desc->empkt_num <= 5) {
		dwtmp = ptcb_desc->empkt_len[4];
	} else {
		dwtmp = ptcb_desc->empkt_len[4];
		dwtmp += ((dwtmp % 4) ? (4 - dwtmp % 4) : 0) + 4;
		dwtmp += ptcb_desc->empkt_len[5];
	}
	SET_EARLYMODE_LEN2_1(virtualaddress, dwtmp & 0xF);
	SET_EARLYMODE_LEN2_2(virtualaddress, dwtmp >> 4);
	if (ptcb_desc->empkt_num <= 7) {
		dwtmp = ptcb_desc->empkt_len[6];
	} else {
		dwtmp = ptcb_desc->empkt_len[6];
		dwtmp += ((dwtmp % 4) ? (4 - dwtmp % 4) : 0) + 4;
		dwtmp += ptcb_desc->empkt_len[7];
	}
	SET_EARLYMODE_LEN3(virtualaddress, dwtmp);
	if (ptcb_desc->empkt_num <= 9) {
		dwtmp = ptcb_desc->empkt_len[8];
	} else {
		dwtmp = ptcb_desc->empkt_len[8];
		dwtmp += ((dwtmp % 4) ? (4 - dwtmp % 4) : 0) + 4;
		dwtmp += ptcb_desc->empkt_len[9];
	}
	SET_EARLYMODE_LEN4(virtualaddress, dwtmp);
}

static bool rtl8822be_get_rxdesc_is_ht(struct ieee80211_hw *hw, u8 *pdesc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 rx_rate = 0;

	rx_rate = GET_RX_DESC_RX_RATE(pdesc);

	RT_TRACE(rtlpriv, COMP_RXDESC, DBG_LOUD, "rx_rate=0x%02x.\n", rx_rate);

	if (rx_rate >= DESC_RATEMCS0 && rx_rate <= DESC_RATEMCS15)
		return true;
	else
		return false;
}

static bool rtl8822be_get_rxdesc_is_vht(struct ieee80211_hw *hw, u8 *pdesc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 rx_rate = 0;

	rx_rate = GET_RX_DESC_RX_RATE(pdesc);

	RT_TRACE(rtlpriv, COMP_RXDESC, DBG_LOUD, "rx_rate=0x%02x.\n", rx_rate);

	if (rx_rate >= DESC_RATEVHT1SS_MCS0)
		return true;
	else
		return false;
}

static u8 rtl8822be_get_rx_vht_nss(struct ieee80211_hw *hw, u8 *pdesc)
{
	u8 rx_rate = 0;
	u8 vht_nss = 0;

	rx_rate = GET_RX_DESC_RX_RATE(pdesc);

	if (rx_rate >= DESC_RATEVHT1SS_MCS0 &&
	    rx_rate <= DESC_RATEVHT1SS_MCS9)
		vht_nss = 1;
	else if ((rx_rate >= DESC_RATEVHT2SS_MCS0) &&
		 (rx_rate <= DESC_RATEVHT2SS_MCS9))
		vht_nss = 2;

	return vht_nss;
}

bool rtl8822be_rx_query_desc(struct ieee80211_hw *hw, struct rtl_stats *status,
			     struct ieee80211_rx_status *rx_status, u8 *pdesc,
			     struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 *p_phystrpt = NULL;
	struct ieee80211_hdr *hdr;

	u32 phystatus = GET_RX_DESC_PHYST(pdesc);

	if (GET_RX_DESC_C2H(pdesc) == 0)
		status->packet_report_type = NORMAL_RX;
	else
		status->packet_report_type = C2H_PACKET;

	status->length = (u16)GET_RX_DESC_PKT_LEN(pdesc);
	status->rx_drvinfo_size =
		(u8)GET_RX_DESC_DRV_INFO_SIZE(pdesc) * RX_DRV_INFO_SIZE_UNIT;
	status->rx_bufshift = (u8)(GET_RX_DESC_SHIFT(pdesc) & 0x03);
	status->icv = (u16)GET_RX_DESC_ICV_ERR(pdesc);
	status->crc = (u16)GET_RX_DESC_CRC32(pdesc);
	status->hwerror = (status->crc | status->icv);
	status->decrypted = !GET_RX_DESC_SWDEC(pdesc);
	status->rate = (u8)GET_RX_DESC_RX_RATE(pdesc);
	status->isampdu = (bool)(GET_RX_DESC_PAGGR(pdesc) == 1);
	status->isfirst_ampdu = (bool)(GET_RX_DESC_PAGGR(pdesc) == 1);
	status->timestamp_low = GET_RX_DESC_TSFL(pdesc);
	status->is_ht = rtl8822be_get_rxdesc_is_ht(hw, pdesc);
	status->is_vht = rtl8822be_get_rxdesc_is_vht(hw, pdesc);
	status->vht_nss = rtl8822be_get_rx_vht_nss(hw, pdesc);
	status->is_cck = RX_HAL_IS_CCK_RATE(status->rate);

	status->macid = GET_RX_DESC_MACID(pdesc);
	if (GET_RX_DESC_PATTERN_MATCH(pdesc))
		status->wake_match = BIT(2);
	else if (GET_RX_DESC_MAGIC_WAKE(pdesc))
		status->wake_match = BIT(1);
	else if (GET_RX_DESC_UNICAST_WAKE(pdesc))
		status->wake_match = BIT(0);
	else
		status->wake_match = 0;
	if (status->wake_match)
		RT_TRACE(rtlpriv, COMP_RXDESC, DBG_LOUD,
			 "GGGGGGGGGGGGGet Wakeup Packet!! WakeMatch=%d\n",
			 status->wake_match);
	rx_status->freq = hw->conf.chandef.chan->center_freq;
	rx_status->band = hw->conf.chandef.chan->band;

	if (phystatus)
		p_phystrpt = (skb->data + status->rx_bufshift + 24);

	hdr = (struct ieee80211_hdr *)(skb->data + status->rx_drvinfo_size +
				       status->rx_bufshift + 24);

	if (status->crc)
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (status->is_ht)
		rx_status->encoding = RX_ENC_HT;
	if (status->is_vht)
		rx_status->encoding = RX_ENC_VHT;

	rx_status->nss = status->vht_nss;

	rx_status->flag |= RX_FLAG_MACTIME_START;

	/* hw will set status->decrypted true, if it finds the
	 * frame is open data frame or mgmt frame.
	 */
	/* So hw will not decryption robust management frame
	 * for IEEE80211w but still set status->decrypted
	 * true, so here we should set it back to undecrypted
	 * for IEEE80211w frame, and mac80211 sw will help
	 * to decrypt it
	 */
	if (status->decrypted) {
		if ((!_ieee80211_is_robust_mgmt_frame(hdr)) &&
		    (ieee80211_has_protected(hdr->frame_control)))
			rx_status->flag |= RX_FLAG_DECRYPTED;
		else
			rx_status->flag &= ~RX_FLAG_DECRYPTED;
	}

	/* rate_idx: index of data rate into band's
	 * supported rates or MCS index if HT rates
	 * are use (RX_FLAG_HT)
	 */
	/* Notice: this is diff with windows define */
	rx_status->rate_idx = rtlwifi_rate_mapping(
		hw, status->is_ht, status->is_vht, status->rate);

	rx_status->mactime = status->timestamp_low;

	_rtl8822be_translate_rx_signal_stuff(hw, skb, status, p_phystrpt);

	/* below info. are filled by _rtl8822be_translate_rx_signal_stuff() */
	if (!p_phystrpt)
		goto label_no_physt;

	rx_status->signal = status->recvsignalpower;

	if (status->rx_packet_bw == HT_CHANNEL_WIDTH_20_40)
		rx_status->bw = RATE_INFO_BW_40;
	else if (status->rx_packet_bw == HT_CHANNEL_WIDTH_80)
		rx_status->bw = RATE_INFO_BW_80;

label_no_physt:

	return true;
}

void rtl8822be_rx_check_dma_ok(struct ieee80211_hw *hw, u8 *header_desc,
			       u8 queue_index)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 first_seg;
	u8 last_seg;
	u16 total_len;
	u16 read_cnt = 0;

	if (!header_desc)
		return;

	do {
		total_len = (u16)GET_RX_BUFFER_DESC_TOTAL_LENGTH(header_desc);
		first_seg = (u8)GET_RX_BUFFER_DESC_FS(header_desc);
		last_seg = (u8)GET_RX_BUFFER_DESC_LS(header_desc);

		if (read_cnt++ > 20) {
			RT_TRACE(rtlpriv, COMP_RECV, DBG_DMESG,
				 "RX chk DMA over %d times\n", read_cnt);
			break;
		}

	} while (total_len == 0 && first_seg == 0 && last_seg == 0);
}

u16 rtl8822be_rx_desc_buff_remained_cnt(struct ieee80211_hw *hw, u8 queue_index)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u16 desc_idx_hw = 0, desc_idx_host = 0, remind_cnt = 0;
	u32 tmp_4byte = 0;

	u32 rw_mask = 0x1ff;

	tmp_4byte = rtl_read_dword(rtlpriv, REG_RXQ_RXBD_IDX_8822B);
	desc_idx_hw = (u16)((tmp_4byte >> 16) & rw_mask);
	desc_idx_host = (u16)(tmp_4byte & rw_mask);

	/* may be no data, donot rx */
	if (desc_idx_hw == desc_idx_host)
		return 0;

	remind_cnt =
		(desc_idx_hw > desc_idx_host) ?
			(desc_idx_hw - desc_idx_host) :
			(RX_DESC_NUM_8822BE - (desc_idx_host - desc_idx_hw));

	rtlpci->rx_ring[queue_index].next_rx_rp = desc_idx_host;

	return remind_cnt;
}

static u16 get_desc_address_from_queue_index(u16 queue_index)
{
	/*
	 * Note: Access these registers will take a lot of cost.
	 */
	u16 desc_address = REG_BEQ_TXBD_IDX_8822B;

	switch (queue_index) {
	case BK_QUEUE:
		desc_address = REG_BKQ_TXBD_IDX_8822B;
		break;
	case BE_QUEUE:
		desc_address = REG_BEQ_TXBD_IDX_8822B;
		break;
	case VI_QUEUE:
		desc_address = REG_VIQ_TXBD_IDX_8822B;
		break;
	case VO_QUEUE:
		desc_address = REG_VOQ_TXBD_IDX_8822B;
		break;
	case BEACON_QUEUE:
		desc_address = REG_BEQ_TXBD_IDX_8822B;
		break;
	case H2C_QUEUE:
		desc_address = REG_H2CQ_TXBD_IDX_8822B;
		break;
	case MGNT_QUEUE:
		desc_address = REG_MGQ_TXBD_IDX_8822B;
		break;
	case HIGH_QUEUE:
		desc_address = REG_HI0Q_TXBD_IDX_8822B;
		break;
	case HCCA_QUEUE:
		desc_address = REG_BEQ_TXBD_IDX_8822B;
		break;
	default:
		break;
	}
	return desc_address;
}

/*free  desc that can be used */
u16 rtl8822be_get_available_desc(struct ieee80211_hw *hw, u8 q_idx)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl8192_tx_ring *ring = &rtlpci->tx_ring[q_idx];

	return calc_fifo_space(ring->cur_tx_rp, ring->cur_tx_wp,
			       TX_DESC_NUM_8822B);
}

void rtl8822be_pre_fill_tx_bd_desc(struct ieee80211_hw *hw, u8 *tx_bd_desc,
				   u8 *desc, u8 queue_index,
				   struct sk_buff *skb, dma_addr_t data_addr)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u32 pkt_len = skb->len;
	u16 desc_size = 48; /*tx desc size*/
	u32 psblen = 0;
	u32 total_packet_size = 0;
	u16 current_bd_desc;
	u8 i = 0;
	/*u16 real_desc_size = 0x28;*/
	u16 append_early_mode_size = 0;
	u8 segmentnum = 1 << (RTL8822BE_SEG_NUM + 1);
	dma_addr_t desc_dma_addr;
	bool dma64 = rtlpriv->cfg->mod_params->dma64;

	current_bd_desc = rtlpci->tx_ring[queue_index].cur_tx_wp;

	total_packet_size = desc_size + pkt_len;

	if (rtlpriv->rtlhal.earlymode_enable) {
		if (queue_index < BEACON_QUEUE) {
			append_early_mode_size = 8;
			total_packet_size += append_early_mode_size;
		}
	}

	/* page number (round up) */
	psblen = (total_packet_size - 1) / 128 + 1;

	/* tx desc addr */
	desc_dma_addr = rtlpci->tx_ring[queue_index].dma +
			(current_bd_desc * TX_DESC_SIZE);

	/* Reset */
	SET_TX_BUFF_DESC_LEN_0(tx_bd_desc, 0);
	SET_TX_BUFF_DESC_PSB(tx_bd_desc, 0);
	SET_TX_BUFF_DESC_OWN(tx_bd_desc, 0);

	for (i = 1; i < segmentnum; i++) {
		SET_TXBUFFER_DESC_LEN_WITH_OFFSET(tx_bd_desc, i, 0);
		SET_TXBUFFER_DESC_AMSDU_WITH_OFFSET(tx_bd_desc, i, 0);
		SET_TXBUFFER_DESC_ADD_LOW_WITH_OFFSET(tx_bd_desc, i, 0);
		SET_TXBUFFER_DESC_ADD_HIGH_WITH_OFFSET(tx_bd_desc, i, 0, dma64);
	}

	/* Clear all status */
	CLEAR_PCI_TX_DESC_CONTENT(desc, TX_DESC_SIZE);

	if (rtlpriv->rtlhal.earlymode_enable) {
		if (queue_index < BEACON_QUEUE)
			SET_TX_BUFF_DESC_LEN_0(tx_bd_desc, desc_size + 8);
		else
			SET_TX_BUFF_DESC_LEN_0(tx_bd_desc, desc_size);
	} else {
		SET_TX_BUFF_DESC_LEN_0(tx_bd_desc, desc_size);
	}
	SET_TX_BUFF_DESC_PSB(tx_bd_desc, psblen);
	SET_TX_BUFF_DESC_ADDR_LOW_0(tx_bd_desc, desc_dma_addr);
	SET_TX_BUFF_DESC_ADDR_HIGH_0(tx_bd_desc, ((u64)desc_dma_addr >> 32),
				     dma64);

	SET_TXBUFFER_DESC_LEN_WITH_OFFSET(tx_bd_desc, 1, pkt_len);
	SET_TXBUFFER_DESC_AMSDU_WITH_OFFSET(tx_bd_desc, 1, 0);
	SET_TXBUFFER_DESC_ADD_LOW_WITH_OFFSET(tx_bd_desc, 1, data_addr);
	SET_TXBUFFER_DESC_ADD_HIGH_WITH_OFFSET(tx_bd_desc, 1,
					       ((u64)data_addr >> 32), dma64);

	SET_TX_DESC_TXPKTSIZE(desc, (u16)(pkt_len));
}

static u8 rtl8822be_bw_mapping(struct ieee80211_hw *hw,
			       struct rtl_tcb_desc *ptcb_desc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 bw_setting_of_desc = 0;

	RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE,
		 "%s, current_chan_bw %d, packet_bw %d\n", __func__,
		 rtlphy->current_chan_bw, ptcb_desc->packet_bw);

	if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_80) {
		if (ptcb_desc->packet_bw == HT_CHANNEL_WIDTH_80)
			bw_setting_of_desc = 2;
		else if (ptcb_desc->packet_bw == HT_CHANNEL_WIDTH_20_40)
			bw_setting_of_desc = 1;
		else
			bw_setting_of_desc = 0;
	} else if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40) {
		if (ptcb_desc->packet_bw == HT_CHANNEL_WIDTH_20_40 ||
		    ptcb_desc->packet_bw == HT_CHANNEL_WIDTH_80)
			bw_setting_of_desc = 1;
		else
			bw_setting_of_desc = 0;
	} else {
		bw_setting_of_desc = 0;
	}

	return bw_setting_of_desc;
}

static u8 rtl8822be_sc_mapping(struct ieee80211_hw *hw,
			       struct rtl_tcb_desc *ptcb_desc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	u8 sc_setting_of_desc = 0;

	if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_80) {
		if (ptcb_desc->packet_bw == HT_CHANNEL_WIDTH_80) {
			sc_setting_of_desc = VHT_DATA_SC_DONOT_CARE;
		} else if (ptcb_desc->packet_bw == HT_CHANNEL_WIDTH_20_40) {
			if (mac->cur_80_prime_sc == HAL_PRIME_CHNL_OFFSET_LOWER)
				sc_setting_of_desc =
					VHT_DATA_SC_40_LOWER_OF_80MHZ;
			else if (mac->cur_80_prime_sc ==
				 HAL_PRIME_CHNL_OFFSET_UPPER)
				sc_setting_of_desc =
					VHT_DATA_SC_40_UPPER_OF_80MHZ;
			else
				RT_TRACE(rtlpriv, COMP_SEND, DBG_LOUD,
					 "%s: Not Correct Primary40MHz Setting\n",
					 __func__);
		} else {
			if (mac->cur_40_prime_sc ==
			     HAL_PRIME_CHNL_OFFSET_LOWER &&
			    mac->cur_80_prime_sc ==
			     HAL_PRIME_CHNL_OFFSET_LOWER)
				sc_setting_of_desc =
					VHT_DATA_SC_20_LOWEST_OF_80MHZ;
			else if ((mac->cur_40_prime_sc ==
				  HAL_PRIME_CHNL_OFFSET_UPPER) &&
				 (mac->cur_80_prime_sc ==
				  HAL_PRIME_CHNL_OFFSET_LOWER))
				sc_setting_of_desc =
					VHT_DATA_SC_20_LOWER_OF_80MHZ;
			else if ((mac->cur_40_prime_sc ==
				  HAL_PRIME_CHNL_OFFSET_LOWER) &&
				 (mac->cur_80_prime_sc ==
				  HAL_PRIME_CHNL_OFFSET_UPPER))
				sc_setting_of_desc =
					VHT_DATA_SC_20_UPPER_OF_80MHZ;
			else if ((mac->cur_40_prime_sc ==
				  HAL_PRIME_CHNL_OFFSET_UPPER) &&
				 (mac->cur_80_prime_sc ==
				  HAL_PRIME_CHNL_OFFSET_UPPER))
				sc_setting_of_desc =
					VHT_DATA_SC_20_UPPERST_OF_80MHZ;
			else
				RT_TRACE(rtlpriv, COMP_SEND, DBG_LOUD,
					 "%s: Not Correct Primary40MHz Setting\n",
					 __func__);
		}
	} else if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40) {
		if (ptcb_desc->packet_bw == HT_CHANNEL_WIDTH_20_40) {
			sc_setting_of_desc = VHT_DATA_SC_DONOT_CARE;
		} else if (ptcb_desc->packet_bw == HT_CHANNEL_WIDTH_20) {
			if (mac->cur_40_prime_sc ==
			    HAL_PRIME_CHNL_OFFSET_UPPER) {
				sc_setting_of_desc =
					VHT_DATA_SC_20_UPPER_OF_80MHZ;
			} else if (mac->cur_40_prime_sc ==
				   HAL_PRIME_CHNL_OFFSET_LOWER) {
				sc_setting_of_desc =
					VHT_DATA_SC_20_LOWER_OF_80MHZ;
			} else {
				sc_setting_of_desc = VHT_DATA_SC_DONOT_CARE;
			}
		}
	} else {
		sc_setting_of_desc = VHT_DATA_SC_DONOT_CARE;
	}

	return sc_setting_of_desc;
}

void rtl8822be_tx_fill_desc(struct ieee80211_hw *hw, struct ieee80211_hdr *hdr,
			    u8 *pdesc_tx, u8 *pbd_desc_tx,
			    struct ieee80211_tx_info *info,
			    struct ieee80211_sta *sta, struct sk_buff *skb,
			    u8 hw_queue, struct rtl_tcb_desc *ptcb_desc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 *pdesc = (u8 *)pdesc_tx;
	u16 seq_number;
	__le16 fc = hdr->frame_control;
	u8 fw_qsel = _rtl8822be_map_hwqueue_to_fwqueue(skb, hw_queue);
	bool firstseg =
		((hdr->seq_ctrl & cpu_to_le16(IEEE80211_SCTL_FRAG)) == 0);
	bool lastseg = ((hdr->frame_control &
			 cpu_to_le16(IEEE80211_FCTL_MOREFRAGS)) == 0);
	dma_addr_t mapping;
	u8 short_gi = 0;

	seq_number = (le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ) >> 4;
	rtl_get_tcb_desc(hw, info, sta, skb, ptcb_desc);
	/* reserve 8 byte for AMPDU early mode */
	if (rtlhal->earlymode_enable) {
		skb_push(skb, EM_HDR_LEN);
		memset(skb->data, 0, EM_HDR_LEN);
	}
	mapping = pci_map_single(rtlpci->pdev, skb->data, skb->len,
				 PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(rtlpci->pdev, mapping)) {
		RT_TRACE(rtlpriv, COMP_SEND, DBG_DMESG, "DMA mapping error");
		return;
	}

	if (pbd_desc_tx)
		rtl8822be_pre_fill_tx_bd_desc(hw, pbd_desc_tx, pdesc, hw_queue,
					      skb, mapping);

	if (ieee80211_is_nullfunc(fc) || ieee80211_is_ctl(fc)) {
		firstseg = true;
		lastseg = true;
	}
	if (firstseg) {
		if (rtlhal->earlymode_enable) {
			SET_TX_DESC_PKT_OFFSET(pdesc, 1);
			SET_TX_DESC_OFFSET(pdesc,
					   USB_HWDESC_HEADER_LEN + EM_HDR_LEN);
			if (ptcb_desc->empkt_num) {
				RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE,
					 "Insert 8 byte.pTcb->EMPktNum:%d\n",
					 ptcb_desc->empkt_num);
				_rtl8822be_insert_emcontent(ptcb_desc,
							    (u8 *)(skb->data));
			}
		} else {
			SET_TX_DESC_OFFSET(pdesc, USB_HWDESC_HEADER_LEN);
		}

		/* tx report */
		rtl_get_tx_report(ptcb_desc, pdesc, hw);

		if (rtlpriv->rtlhal.current_bandtype == BAND_ON_5G &&
		    ptcb_desc->hw_rate < DESC_RATE6M) {
			RT_TRACE(rtlpriv, COMP_SEND, DBG_WARNING,
				 "hw_rate=0x%X is invalid in 5G\n",
				 ptcb_desc->hw_rate);
			ptcb_desc->hw_rate = DESC_RATE6M;
		}
		SET_TX_DESC_DATARATE(pdesc, ptcb_desc->hw_rate);

		if (ptcb_desc->hw_rate > DESC_RATEMCS0)
			short_gi = (ptcb_desc->use_shortgi) ? 1 : 0;
		else
			short_gi = (ptcb_desc->use_shortpreamble) ? 1 : 0;

		if (info->flags & IEEE80211_TX_CTL_AMPDU) {
			SET_TX_DESC_AGG_EN(pdesc, 1);
			SET_TX_DESC_MAX_AGG_NUM(pdesc, 0x1F);
		}
		SET_TX_DESC_SW_SEQ(pdesc, seq_number);
		SET_TX_DESC_RTSEN(pdesc, ((ptcb_desc->rts_enable &&
					   !ptcb_desc->cts_enable) ?
						  1 :
						  0));
		SET_TX_DESC_HW_RTS_EN(pdesc, 0);
		SET_TX_DESC_CTS2SELF(pdesc, ((ptcb_desc->cts_enable) ? 1 : 0));

		SET_TX_DESC_RTSRATE(pdesc, ptcb_desc->rts_rate);
		SET_TX_DESC_RTS_SC(pdesc, ptcb_desc->rts_sc);
		SET_TX_DESC_RTS_SHORT(
			pdesc,
			((ptcb_desc->rts_rate <= DESC_RATE54M) ?
				 (ptcb_desc->rts_use_shortpreamble ? 1 : 0) :
				 (ptcb_desc->rts_use_shortgi ? 1 : 0)));

		if (ptcb_desc->tx_enable_sw_calc_duration)
			SET_TX_DESC_NAVUSEHDR(pdesc, 1);

		SET_TX_DESC_DATA_BW(pdesc, rtl8822be_bw_mapping(hw, ptcb_desc));
		SET_TX_DESC_DATA_SC(pdesc, rtl8822be_sc_mapping(hw, ptcb_desc));

		if (sta) {
			u8 ampdu_density = sta->ht_cap.ampdu_density;

			SET_TX_DESC_AMPDU_DENSITY(pdesc, ampdu_density);
		}
		if (info->control.hw_key) {
			struct ieee80211_key_conf *key = info->control.hw_key;

			switch (key->cipher) {
			case WLAN_CIPHER_SUITE_WEP40:
			case WLAN_CIPHER_SUITE_WEP104:
			case WLAN_CIPHER_SUITE_TKIP:
				SET_TX_DESC_SEC_TYPE(pdesc, 0x1);
				break;
			case WLAN_CIPHER_SUITE_CCMP:
				SET_TX_DESC_SEC_TYPE(pdesc, 0x3);
				break;
			default:
				SET_TX_DESC_SEC_TYPE(pdesc, 0x0);
				break;
			}
		}

		SET_TX_DESC_QSEL(pdesc, fw_qsel);

		if (rtlphy->current_channel > 14) {
			/* OFDM 6M */
			SET_TX_DESC_DATA_RTY_LOWEST_RATE(pdesc, 4);
			SET_TX_DESC_RTS_RTY_LOWEST_RATE(pdesc, 4);
		} else {
			/* CCK 1M */
			SET_TX_DESC_DATA_RTY_LOWEST_RATE(pdesc, 0);
			SET_TX_DESC_RTS_RTY_LOWEST_RATE(pdesc, 0);
		}
		SET_TX_DESC_DISDATAFB(pdesc,
				      ptcb_desc->disable_ratefallback ? 1 : 0);
		SET_TX_DESC_USE_RATE(pdesc, ptcb_desc->use_driver_rate ? 1 : 0);

		/*SET_TX_DESC_PWR_STATUS(pdesc, pwr_status);*/
		/* Set TxRate and RTSRate in TxDesc  */
		/* This prevent Tx initial rate of new-coming packets */
		/* from being overwritten by retried  packet rate.*/
		if (!ptcb_desc->use_driver_rate) {
			/*SET_TX_DESC_RTS_RATE(pdesc, 0x08); */
			/* SET_TX_DESC_TX_RATE(pdesc, 0x0b); */
		}
		if (ieee80211_is_data_qos(fc)) {
			if (mac->rdg_en) {
				RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE,
					 "Enable RDG function.\n");
				SET_TX_DESC_RDG_EN(pdesc, 1);
				SET_TX_DESC_HTC(pdesc, 1);
			}
		}

		SET_TX_DESC_PORT_ID(pdesc, 0);
		SET_TX_DESC_MULTIPLE_PORT(pdesc, 0);
	}

	SET_TX_DESC_LS(pdesc, (lastseg ? 1 : 0));
	if (rtlpriv->dm.useramask) {
		SET_TX_DESC_RATE_ID(pdesc, ptcb_desc->ratr_index);
		SET_TX_DESC_MACID(pdesc, ptcb_desc->mac_id);
	} else {
		SET_TX_DESC_RATE_ID(pdesc, 0xC + ptcb_desc->ratr_index);
		SET_TX_DESC_MACID(pdesc, ptcb_desc->ratr_index);
	}

	SET_TX_DESC_MOREFRAG(pdesc, (lastseg ? 0 : 1));
	if (ptcb_desc->multicast || ptcb_desc->broadcast) {
		SET_TX_DESC_BMC(pdesc, 1);
		/* BMC must be not AGG */
		SET_TX_DESC_AGG_EN(pdesc, 0);
	}
	RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE, "\n");

	/* debug purpose: used to check tx desc is correct or not */
	/*rtlpriv->halmac.ops->halmac_chk_txdesc(rtlpriv, pdesc,
	 *			skb->len + USB_HWDESC_HEADER_LEN);
	 */
}

void rtl8822be_tx_fill_special_desc(struct ieee80211_hw *hw, u8 *pdesc,
				    u8 *pbd_desc, struct sk_buff *skb,
				    u8 hw_queue)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 fw_queue;
	u8 txdesc_len = 48;

	dma_addr_t mapping = pci_map_single(rtlpci->pdev, skb->data, skb->len,
					    PCI_DMA_TODEVICE);

	if (pci_dma_mapping_error(rtlpci->pdev, mapping)) {
		RT_TRACE(rtlpriv, COMP_SEND, DBG_DMESG, "DMA mapping error");
		return;
	}

	rtl8822be_pre_fill_tx_bd_desc(hw, pbd_desc, pdesc, hw_queue, skb,
				      mapping);

	/* it should be BEACON_QUEUE or H2C_QUEUE,
	 * so skb=NULL is safe to assert
	 */
	fw_queue = _rtl8822be_map_hwqueue_to_fwqueue(NULL, hw_queue);

	CLEAR_PCI_TX_DESC_CONTENT(pdesc, txdesc_len);

	/* common part for BEACON and H2C */
	SET_TX_DESC_TXPKTSIZE((u8 *)pdesc, (u16)(skb->len));

	SET_TX_DESC_QSEL(pdesc, fw_queue);

	if (hw_queue == H2C_QUEUE) {
		/* fill H2C */
		SET_TX_DESC_OFFSET(pdesc, 0);

	} else {
		/* fill beacon */
		SET_TX_DESC_OFFSET(pdesc, txdesc_len);

		SET_TX_DESC_DATARATE(pdesc, DESC_RATE1M);

		SET_TX_DESC_SW_SEQ(pdesc, 0);

		SET_TX_DESC_RATE_ID(pdesc, 7);
		SET_TX_DESC_MACID(pdesc, 0);

		SET_TX_DESC_LS(pdesc, 1);

		SET_TX_DESC_OFFSET(pdesc, 48);

		SET_TX_DESC_USE_RATE(pdesc, 1);
	}

	RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_LOUD, "H2C Tx Cmd Content\n",
		      pdesc, txdesc_len);
}

void rtl8822be_set_desc(struct ieee80211_hw *hw, u8 *pdesc, bool istx,
			u8 desc_name, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 q_idx = *val;
	bool dma64 = rtlpriv->cfg->mod_params->dma64;

	if (istx) {
		switch (desc_name) {
		case HW_DESC_OWN: {
			struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
			struct rtl8192_tx_ring *ring = &rtlpci->tx_ring[q_idx];
			u16 max_tx_desc = ring->entries;

			if (q_idx == BEACON_QUEUE) {
				/* in case of beacon, pdesc is BD desc. */
				u8 *pbd_desc = pdesc;

				ring->cur_tx_wp = 0;
				ring->cur_tx_rp = 0;
				SET_TX_BUFF_DESC_OWN(pbd_desc, 1);
				return;
			}

			/* make sure tx desc is available by caller */
			ring->cur_tx_wp = ((ring->cur_tx_wp + 1) % max_tx_desc);

			rtl_write_word(
				rtlpriv,
				get_desc_address_from_queue_index(
					q_idx),
				ring->cur_tx_wp);
		} break;
		}
	} else {
		switch (desc_name) {
		case HW_DESC_RX_PREPARE:
			SET_RX_BUFFER_DESC_LS(pdesc, 0);
			SET_RX_BUFFER_DESC_FS(pdesc, 0);
			SET_RX_BUFFER_DESC_TOTAL_LENGTH(pdesc, 0);

			SET_RX_BUFFER_DESC_DATA_LENGTH(
				pdesc, MAX_RECEIVE_BUFFER_SIZE + RX_DESC_SIZE);

			SET_RX_BUFFER_PHYSICAL_LOW(
				pdesc, (*(dma_addr_t *)val) & DMA_BIT_MASK(32));
			SET_RX_BUFFER_PHYSICAL_HIGH(
				pdesc, ((u64)(*(dma_addr_t *)val) >> 32),
				dma64);
			break;
		default:
			WARN_ONCE(true, "ERR rxdesc :%d not process\n",
				  desc_name);
			break;
		}
	}
}

u64 rtl8822be_get_desc(struct ieee80211_hw *hw,
		       u8 *pdesc, bool istx, u8 desc_name)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u64 ret = 0;
	u8 *pbd_desc = pdesc;
	bool dma64 = rtlpriv->cfg->mod_params->dma64;

	if (istx) {
		switch (desc_name) {
		case HW_DESC_TXBUFF_ADDR:
			ret = GET_TXBUFFER_DESC_ADDR_LOW(pbd_desc, 1);
			ret |= (u64)GET_TXBUFFER_DESC_ADDR_HIGH(pbd_desc, 1,
								dma64) << 32;
			break;
		default:
			WARN_ONCE(true, "ERR txdesc :%d not process\n",
				  desc_name);
			break;
		}
	} else {
		switch (desc_name) {
		case HW_DESC_RXPKT_LEN:
			ret = GET_RX_DESC_PKT_LEN(pdesc);
			break;
		default:
			WARN_ONCE(true, "ERR rxdesc :%d not process\n",
				  desc_name);
			break;
		}
	}
	return ret;
}

bool rtl8822be_is_tx_desc_closed(struct ieee80211_hw *hw, u8 hw_queue,
				 u16 index)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	bool ret = false;
	struct rtl8192_tx_ring *ring = &rtlpci->tx_ring[hw_queue];
	u16 cur_tx_rp, cur_tx_wp;
	u16 tmp16;

	/*
	 * design rule:
	 *     idx <= cur_tx_rp <= hw_rp <= cur_tx_wp = hw_wp
	 */

	if (index == ring->cur_tx_rp) {
		/* update only if sw_rp reach hw_rp */
		tmp16 = rtl_read_word(
			    rtlpriv,
			    get_desc_address_from_queue_index(hw_queue) + 2);

		cur_tx_rp = tmp16 & 0x01ff;
		cur_tx_wp = ring->cur_tx_wp;

		/* don't need to update ring->cur_tx_wp */
		ring->cur_tx_rp = cur_tx_rp;
	}

	if (index == ring->cur_tx_rp)
		ret = false;	/* no more */
	else
		ret = true;	/* more */

	if (hw_queue == BEACON_QUEUE)
		ret = true;

	if (rtlpriv->rtlhal.driver_is_goingto_unload ||
	    rtlpriv->psc.rfoff_reason > RF_CHANGE_BY_PS)
		ret = true;

	return ret;
}

void rtl8822be_tx_polling(struct ieee80211_hw *hw, u8 hw_queue)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (hw_queue == BEACON_QUEUE) {
		/* kick start */
		rtl_write_byte(
			rtlpriv, REG_RX_RXBD_NUM_8822B + 1,
			rtl_read_byte(rtlpriv, REG_RX_RXBD_NUM_8822B + 1) |
				BIT(4));
	}
}

u32 rtl8822be_rx_command_packet(struct ieee80211_hw *hw,
				const struct rtl_stats *status,
				struct sk_buff *skb)
{
	u32 result = 0;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	switch (status->packet_report_type) {
	case NORMAL_RX:
		result = 0;
		break;
	case C2H_PACKET:
		rtl8822be_c2h_packet_handler(hw, skb->data, (u8)skb->len);
		result = 1;
		break;
	default:
		RT_TRACE(rtlpriv, COMP_RECV, DBG_TRACE,
			 "Unknown packet type %d\n",
			 status->packet_report_type);
		break;
	}

	return result;
}
