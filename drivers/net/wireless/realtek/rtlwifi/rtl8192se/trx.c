// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../wifi.h"
#include "../pci.h"
#include "../base.h"
#include "../stats.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "fw.h"
#include "trx.h"
#include "led.h"

static u8 _rtl92se_map_hwqueue_to_fwqueue(struct sk_buff *skb,	u8 skb_queue)
{
	__le16 fc = rtl_get_fc(skb);

	if (unlikely(ieee80211_is_beacon(fc)))
		return QSLT_BEACON;
	if (ieee80211_is_mgmt(fc) || ieee80211_is_ctl(fc))
		return QSLT_MGNT;
	if (ieee80211_is_nullfunc(fc))
		return QSLT_HIGH;

	/* Kernel commit 1bf4bbb4024dcdab changed EAPOL packets to use
	 * queue V0 at priority 7; however, the RTL8192SE appears to have
	 * that queue at priority 6
	 */
	if (skb->priority == 7)
		return QSLT_VO;
	return skb->priority;
}

static void _rtl92se_query_rxphystatus(struct ieee80211_hw *hw,
				       struct rtl_stats *pstats, __le32 *pdesc,
				       struct rx_fwinfo *p_drvinfo,
				       bool packet_match_bssid,
				       bool packet_toself,
				       bool packet_beacon)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct phy_sts_cck_8192s_t *cck_buf;
	struct rtl_ps_ctl *ppsc = rtl_psc(rtlpriv);
	s8 rx_pwr_all = 0, rx_pwr[4];
	u8 rf_rx_num = 0, evm, pwdb_all;
	u8 i, max_spatial_stream;
	u32 rssi, total_rssi = 0;
	bool is_cck = pstats->is_cck;

	pstats->packet_matchbssid = packet_match_bssid;
	pstats->packet_toself = packet_toself;
	pstats->packet_beacon = packet_beacon;
	pstats->rx_mimo_sig_qual[0] = -1;
	pstats->rx_mimo_sig_qual[1] = -1;

	if (is_cck) {
		u8 report, cck_highpwr;
		cck_buf = (struct phy_sts_cck_8192s_t *)p_drvinfo;

		if (ppsc->rfpwr_state == ERFON)
			cck_highpwr = (u8) rtl_get_bbreg(hw,
						RFPGA0_XA_HSSIPARAMETER2,
						0x200);
		else
			cck_highpwr = false;

		if (!cck_highpwr) {
			u8 cck_agc_rpt = cck_buf->cck_agc_rpt;
			report = cck_buf->cck_agc_rpt & 0xc0;
			report = report >> 6;
			switch (report) {
			case 0x3:
				rx_pwr_all = -40 - (cck_agc_rpt & 0x3e);
				break;
			case 0x2:
				rx_pwr_all = -20 - (cck_agc_rpt & 0x3e);
				break;
			case 0x1:
				rx_pwr_all = -2 - (cck_agc_rpt & 0x3e);
				break;
			case 0x0:
				rx_pwr_all = 14 - (cck_agc_rpt & 0x3e);
				break;
			}
		} else {
			u8 cck_agc_rpt = cck_buf->cck_agc_rpt;
			report = p_drvinfo->cfosho[0] & 0x60;
			report = report >> 5;
			switch (report) {
			case 0x3:
				rx_pwr_all = -40 - ((cck_agc_rpt & 0x1f) << 1);
				break;
			case 0x2:
				rx_pwr_all = -20 - ((cck_agc_rpt & 0x1f) << 1);
				break;
			case 0x1:
				rx_pwr_all = -2 - ((cck_agc_rpt & 0x1f) << 1);
				break;
			case 0x0:
				rx_pwr_all = 14 - ((cck_agc_rpt & 0x1f) << 1);
				break;
			}
		}

		pwdb_all = rtl_query_rxpwrpercentage(rx_pwr_all);

		/* CCK gain is smaller than OFDM/MCS gain,  */
		/* so we add gain diff by experiences, the val is 6 */
		pwdb_all += 6;
		if (pwdb_all > 100)
			pwdb_all = 100;
		/* modify the offset to make the same gain index with OFDM. */
		if (pwdb_all > 34 && pwdb_all <= 42)
			pwdb_all -= 2;
		else if (pwdb_all > 26 && pwdb_all <= 34)
			pwdb_all -= 6;
		else if (pwdb_all > 14 && pwdb_all <= 26)
			pwdb_all -= 8;
		else if (pwdb_all > 4 && pwdb_all <= 14)
			pwdb_all -= 4;

		pstats->rx_pwdb_all = pwdb_all;
		pstats->recvsignalpower = rx_pwr_all;

		if (packet_match_bssid) {
			u8 sq;
			if (pstats->rx_pwdb_all > 40) {
				sq = 100;
			} else {
				sq = cck_buf->sq_rpt;
				if (sq > 64)
					sq = 0;
				else if (sq < 20)
					sq = 100;
				else
					sq = ((64 - sq) * 100) / 44;
			}

			pstats->signalquality = sq;
			pstats->rx_mimo_sig_qual[0] = sq;
			pstats->rx_mimo_sig_qual[1] = -1;
		}
	} else {
		rtlpriv->dm.rfpath_rxenable[0] =
		    rtlpriv->dm.rfpath_rxenable[1] = true;
		for (i = RF90_PATH_A; i < RF6052_MAX_PATH; i++) {
			if (rtlpriv->dm.rfpath_rxenable[i])
				rf_rx_num++;

			rx_pwr[i] = ((p_drvinfo->gain_trsw[i] &
				    0x3f) * 2) - 110;
			rssi = rtl_query_rxpwrpercentage(rx_pwr[i]);
			total_rssi += rssi;
			rtlpriv->stats.rx_snr_db[i] =
					 (long)(p_drvinfo->rxsnr[i] / 2);

			if (packet_match_bssid)
				pstats->rx_mimo_signalstrength[i] = (u8) rssi;
		}

		rx_pwr_all = ((p_drvinfo->pwdb_all >> 1) & 0x7f) - 110;
		pwdb_all = rtl_query_rxpwrpercentage(rx_pwr_all);
		pstats->rx_pwdb_all = pwdb_all;
		pstats->rxpower = rx_pwr_all;
		pstats->recvsignalpower = rx_pwr_all;

		if (pstats->is_ht && pstats->rate >= DESC_RATEMCS8 &&
		    pstats->rate <= DESC_RATEMCS15)
			max_spatial_stream = 2;
		else
			max_spatial_stream = 1;

		for (i = 0; i < max_spatial_stream; i++) {
			evm = rtl_evm_db_to_percentage(p_drvinfo->rxevm[i]);

			if (packet_match_bssid) {
				if (i == 0)
					pstats->signalquality = (u8)(evm &
								 0xff);
				pstats->rx_mimo_sig_qual[i] = (u8) (evm & 0xff);
			}
		}
	}

	if (is_cck)
		pstats->signalstrength = (u8)(rtl_signal_scale_mapping(hw,
					 pwdb_all));
	else if (rf_rx_num != 0)
		pstats->signalstrength = (u8) (rtl_signal_scale_mapping(hw,
				total_rssi /= rf_rx_num));
}

static void _rtl92se_translate_rx_signal_stuff(struct ieee80211_hw *hw,
		struct sk_buff *skb, struct rtl_stats *pstats,
		__le32 *pdesc, struct rx_fwinfo *p_drvinfo)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct ieee80211_hdr *hdr;
	u8 *tmp_buf;
	u8 *praddr;
	__le16 fc;
	u16 type, cfc;
	bool packet_matchbssid, packet_toself, packet_beacon = false;

	tmp_buf = skb->data + pstats->rx_drvinfo_size + pstats->rx_bufshift;

	hdr = (struct ieee80211_hdr *)tmp_buf;
	fc = hdr->frame_control;
	cfc = le16_to_cpu(fc);
	type = WLAN_FC_GET_TYPE(fc);
	praddr = hdr->addr1;

	packet_matchbssid = ((IEEE80211_FTYPE_CTL != type) &&
	     ether_addr_equal(mac->bssid,
			      (cfc & IEEE80211_FCTL_TODS) ? hdr->addr1 :
			      (cfc & IEEE80211_FCTL_FROMDS) ? hdr->addr2 :
			      hdr->addr3) &&
	     (!pstats->hwerror) && (!pstats->crc) && (!pstats->icv));

	packet_toself = packet_matchbssid &&
	    ether_addr_equal(praddr, rtlefuse->dev_addr);

	if (ieee80211_is_beacon(fc))
		packet_beacon = true;

	_rtl92se_query_rxphystatus(hw, pstats, pdesc, p_drvinfo,
			packet_matchbssid, packet_toself, packet_beacon);
	rtl_process_phyinfo(hw, tmp_buf, pstats);
}

bool rtl92se_rx_query_desc(struct ieee80211_hw *hw, struct rtl_stats *stats,
			   struct ieee80211_rx_status *rx_status, u8 *pdesc8,
			   struct sk_buff *skb)
{
	struct rx_fwinfo *p_drvinfo;
	__le32 *pdesc = (__le32 *)pdesc8;
	u32 phystatus = (u32)get_rx_status_desc_phy_status(pdesc);
	struct ieee80211_hdr *hdr;

	stats->length = (u16)get_rx_status_desc_pkt_len(pdesc);
	stats->rx_drvinfo_size = (u8)get_rx_status_desc_drvinfo_size(pdesc) * 8;
	stats->rx_bufshift = (u8)(get_rx_status_desc_shift(pdesc) & 0x03);
	stats->icv = (u16)get_rx_status_desc_icv(pdesc);
	stats->crc = (u16)get_rx_status_desc_crc32(pdesc);
	stats->hwerror = (u16)(stats->crc | stats->icv);
	stats->decrypted = !get_rx_status_desc_swdec(pdesc);

	stats->rate = (u8)get_rx_status_desc_rx_mcs(pdesc);
	stats->shortpreamble = (u16)get_rx_status_desc_splcp(pdesc);
	stats->isampdu = (bool)(get_rx_status_desc_paggr(pdesc) == 1);
	stats->isfirst_ampdu = (bool)((get_rx_status_desc_paggr(pdesc) == 1) &&
				      (get_rx_status_desc_faggr(pdesc) == 1));
	stats->timestamp_low = get_rx_status_desc_tsfl(pdesc);
	stats->rx_is40mhzpacket = (bool)get_rx_status_desc_bw(pdesc);
	stats->is_ht = (bool)get_rx_status_desc_rx_ht(pdesc);
	stats->is_cck = SE_RX_HAL_IS_CCK_RATE(pdesc);

	if (stats->hwerror)
		return false;

	rx_status->freq = hw->conf.chandef.chan->center_freq;
	rx_status->band = hw->conf.chandef.chan->band;

	if (stats->crc)
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (stats->rx_is40mhzpacket)
		rx_status->bw = RATE_INFO_BW_40;

	if (stats->is_ht)
		rx_status->encoding = RX_ENC_HT;

	rx_status->flag |= RX_FLAG_MACTIME_START;

	/* hw will set stats->decrypted true, if it finds the
	 * frame is open data frame or mgmt frame,
	 * hw will not decrypt robust managment frame
	 * for IEEE80211w but still set stats->decrypted
	 * true, so here we should set it back to undecrypted
	 * for IEEE80211w frame, and mac80211 sw will help
	 * to decrypt it */
	if (stats->decrypted) {
		hdr = (struct ieee80211_hdr *)(skb->data +
		       stats->rx_drvinfo_size + stats->rx_bufshift);

		if ((_ieee80211_is_robust_mgmt_frame(hdr)) &&
			(ieee80211_has_protected(hdr->frame_control)))
			rx_status->flag &= ~RX_FLAG_DECRYPTED;
		else
			rx_status->flag |= RX_FLAG_DECRYPTED;
	}

	rx_status->rate_idx = rtlwifi_rate_mapping(hw, stats->is_ht,
						   false, stats->rate);

	rx_status->mactime = stats->timestamp_low;
	if (phystatus) {
		p_drvinfo = (struct rx_fwinfo *)(skb->data +
						 stats->rx_bufshift);
		_rtl92se_translate_rx_signal_stuff(hw, skb, stats, pdesc,
						   p_drvinfo);
	}

	/*rx_status->qual = stats->signal; */
	rx_status->signal = stats->recvsignalpower + 10;

	return true;
}

void rtl92se_tx_fill_desc(struct ieee80211_hw *hw,
		struct ieee80211_hdr *hdr, u8 *pdesc8,
		u8 *pbd_desc_tx, struct ieee80211_tx_info *info,
		struct ieee80211_sta *sta,
		struct sk_buff *skb,
		u8 hw_queue, struct rtl_tcb_desc *ptcb_desc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	__le32 *pdesc = (__le32 *)pdesc8;
	u16 seq_number;
	__le16 fc = hdr->frame_control;
	u8 reserved_macid = 0;
	u8 fw_qsel = _rtl92se_map_hwqueue_to_fwqueue(skb, hw_queue);
	bool firstseg = (!(hdr->seq_ctrl & cpu_to_le16(IEEE80211_SCTL_FRAG)));
	bool lastseg = (!(hdr->frame_control &
			cpu_to_le16(IEEE80211_FCTL_MOREFRAGS)));
	dma_addr_t mapping = dma_map_single(&rtlpci->pdev->dev, skb->data,
					    skb->len, DMA_TO_DEVICE);
	u8 bw_40 = 0;

	if (dma_mapping_error(&rtlpci->pdev->dev, mapping)) {
		rtl_dbg(rtlpriv, COMP_SEND, DBG_TRACE,
			"DMA mapping error\n");
		return;
	}
	if (mac->opmode == NL80211_IFTYPE_STATION) {
		bw_40 = mac->bw_40;
	} else if (mac->opmode == NL80211_IFTYPE_AP ||
		mac->opmode == NL80211_IFTYPE_ADHOC) {
		if (sta)
			bw_40 = sta->bandwidth >= IEEE80211_STA_RX_BW_40;
	}

	seq_number = (le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ) >> 4;

	rtl_get_tcb_desc(hw, info, sta, skb, ptcb_desc);

	CLEAR_PCI_TX_DESC_CONTENT(pdesc, TX_DESC_SIZE_RTL8192S);

	if (ieee80211_is_nullfunc(fc) || ieee80211_is_ctl(fc)) {
		firstseg = true;
		lastseg = true;
	}

	if (firstseg) {
		if (rtlpriv->dm.useramask) {
			/* set txdesc macId */
			if (ptcb_desc->mac_id < 32) {
				set_tx_desc_macid(pdesc, ptcb_desc->mac_id);
				reserved_macid |= ptcb_desc->mac_id;
			}
		}
		set_tx_desc_rsvd_macid(pdesc, reserved_macid);

		set_tx_desc_txht(pdesc, ((ptcb_desc->hw_rate >=
				 DESC_RATEMCS0) ? 1 : 0));

		if (rtlhal->version == VERSION_8192S_ACUT) {
			if (ptcb_desc->hw_rate == DESC_RATE1M ||
			    ptcb_desc->hw_rate  == DESC_RATE2M ||
			    ptcb_desc->hw_rate == DESC_RATE5_5M ||
			    ptcb_desc->hw_rate == DESC_RATE11M) {
				ptcb_desc->hw_rate = DESC_RATE12M;
			}
		}

		set_tx_desc_tx_rate(pdesc, ptcb_desc->hw_rate);

		if (ptcb_desc->use_shortgi || ptcb_desc->use_shortpreamble)
			set_tx_desc_tx_short(pdesc, 0);

		/* Aggregation related */
		if (info->flags & IEEE80211_TX_CTL_AMPDU)
			set_tx_desc_agg_enable(pdesc, 1);

		/* For AMPDU, we must insert SSN into TX_DESC */
		set_tx_desc_seq(pdesc, seq_number);

		/* Protection mode related */
		/* For 92S, if RTS/CTS are set, HW will execute RTS. */
		/* We choose only one protection mode to execute */
		set_tx_desc_rts_enable(pdesc, ((ptcb_desc->rts_enable &&
						!ptcb_desc->cts_enable) ?
					       1 : 0));
		set_tx_desc_cts_enable(pdesc, ((ptcb_desc->cts_enable) ?
				       1 : 0));
		set_tx_desc_rts_stbc(pdesc, ((ptcb_desc->rts_stbc) ? 1 : 0));

		set_tx_desc_rts_rate(pdesc, ptcb_desc->rts_rate);
		set_tx_desc_rts_bandwidth(pdesc, 0);
		set_tx_desc_rts_sub_carrier(pdesc, ptcb_desc->rts_sc);
		set_tx_desc_rts_short(pdesc, ((ptcb_desc->rts_rate <=
		       DESC_RATE54M) ?
		       (ptcb_desc->rts_use_shortpreamble ? 1 : 0)
		       : (ptcb_desc->rts_use_shortgi ? 1 : 0)));


		/* Set Bandwidth and sub-channel settings. */
		if (bw_40) {
			if (ptcb_desc->packet_bw) {
				set_tx_desc_tx_bandwidth(pdesc, 1);
				/* use duplicated mode */
				set_tx_desc_tx_sub_carrier(pdesc, 0);
			} else {
				set_tx_desc_tx_bandwidth(pdesc, 0);
				set_tx_desc_tx_sub_carrier(pdesc,
						   mac->cur_40_prime_sc);
			}
		} else {
			set_tx_desc_tx_bandwidth(pdesc, 0);
			set_tx_desc_tx_sub_carrier(pdesc, 0);
		}

		/* 3 Fill necessary field in First Descriptor */
		/*DWORD 0*/
		set_tx_desc_linip(pdesc, 0);
		set_tx_desc_offset(pdesc, 32);
		set_tx_desc_pkt_size(pdesc, (u16)skb->len);

		/*DWORD 1*/
		set_tx_desc_ra_brsr_id(pdesc, ptcb_desc->ratr_index);

		/* Fill security related */
		if (info->control.hw_key) {
			struct ieee80211_key_conf *keyconf;

			keyconf = info->control.hw_key;
			switch (keyconf->cipher) {
			case WLAN_CIPHER_SUITE_WEP40:
			case WLAN_CIPHER_SUITE_WEP104:
				set_tx_desc_sec_type(pdesc, 0x1);
				break;
			case WLAN_CIPHER_SUITE_TKIP:
				set_tx_desc_sec_type(pdesc, 0x2);
				break;
			case WLAN_CIPHER_SUITE_CCMP:
				set_tx_desc_sec_type(pdesc, 0x3);
				break;
			default:
				set_tx_desc_sec_type(pdesc, 0x0);
				break;

			}
		}

		/* Set Packet ID */
		set_tx_desc_packet_id(pdesc, 0);

		/* We will assign magement queue to BK. */
		set_tx_desc_queue_sel(pdesc, fw_qsel);

		/* Alwasy enable all rate fallback range */
		set_tx_desc_data_rate_fb_limit(pdesc, 0x1F);

		/* Fix: I don't kown why hw use 6.5M to tx when set it */
		set_tx_desc_user_rate(pdesc,
				      ptcb_desc->use_driver_rate ? 1 : 0);

		/* Set NON_QOS bit. */
		if (!ieee80211_is_data_qos(fc))
			set_tx_desc_non_qos(pdesc, 1);

	}

	/* Fill fields that are required to be initialized
	 * in all of the descriptors */
	/*DWORD 0 */
	set_tx_desc_first_seg(pdesc, (firstseg ? 1 : 0));
	set_tx_desc_last_seg(pdesc, (lastseg ? 1 : 0));

	/* DWORD 7 */
	set_tx_desc_tx_buffer_size(pdesc, (u16)skb->len);

	/* DOWRD 8 */
	set_tx_desc_tx_buffer_address(pdesc, mapping);

	rtl_dbg(rtlpriv, COMP_SEND, DBG_TRACE, "\n");
}

void rtl92se_tx_fill_cmddesc(struct ieee80211_hw *hw, u8 *pdesc8,
			     bool firstseg, bool lastseg, struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_tcb_desc *tcb_desc = (struct rtl_tcb_desc *)(skb->cb);
	__le32 *pdesc = (__le32 *)pdesc8;

	dma_addr_t mapping = dma_map_single(&rtlpci->pdev->dev, skb->data,
					    skb->len, DMA_TO_DEVICE);

	if (dma_mapping_error(&rtlpci->pdev->dev, mapping)) {
		rtl_dbg(rtlpriv, COMP_SEND, DBG_TRACE,
			"DMA mapping error\n");
		return;
	}
	/* Clear all status	*/
	CLEAR_PCI_TX_DESC_CONTENT(pdesc, TX_CMDDESC_SIZE_RTL8192S);

	/* This bit indicate this packet is used for FW download. */
	if (tcb_desc->cmd_or_init == DESC_PACKET_TYPE_INIT) {
		/* For firmware downlaod we only need to set LINIP */
		set_tx_desc_linip(pdesc, tcb_desc->last_inipkt);

		/* 92SE must set as 1 for firmware download HW DMA error */
		set_tx_desc_first_seg(pdesc, 1);
		set_tx_desc_last_seg(pdesc, 1);

		/* 92SE need not to set TX packet size when firmware download */
		set_tx_desc_pkt_size(pdesc, (u16)(skb->len));
		set_tx_desc_tx_buffer_size(pdesc, (u16)(skb->len));
		set_tx_desc_tx_buffer_address(pdesc, mapping);

		wmb();
		set_tx_desc_own(pdesc, 1);
	} else { /* H2C Command Desc format (Host TXCMD) */
		/* 92SE must set as 1 for firmware download HW DMA error */
		set_tx_desc_first_seg(pdesc, 1);
		set_tx_desc_last_seg(pdesc, 1);

		set_tx_desc_offset(pdesc, 0x20);

		/* Buffer size + command header */
		set_tx_desc_pkt_size(pdesc, (u16)(skb->len));
		/* Fixed queue of H2C command */
		set_tx_desc_queue_sel(pdesc, 0x13);

		le32p_replace_bits((__le32 *)skb->data, rtlhal->h2c_txcmd_seq,
				   GENMASK(30, 24));
		set_tx_desc_tx_buffer_size(pdesc, (u16)(skb->len));
		set_tx_desc_tx_buffer_address(pdesc, mapping);

		wmb();
		set_tx_desc_own(pdesc, 1);

	}
}

void rtl92se_set_desc(struct ieee80211_hw *hw, u8 *pdesc8, bool istx,
		      u8 desc_name, u8 *val)
{
	__le32 *pdesc = (__le32 *)pdesc8;

	if (istx) {
		switch (desc_name) {
		case HW_DESC_OWN:
			wmb();
			set_tx_desc_own(pdesc, 1);
			break;
		case HW_DESC_TX_NEXTDESC_ADDR:
			set_tx_desc_next_desc_address(pdesc, *(u32 *)val);
			break;
		default:
			WARN_ONCE(true, "rtl8192se: ERR txdesc :%d not processed\n",
				  desc_name);
			break;
		}
	} else {
		switch (desc_name) {
		case HW_DESC_RXOWN:
			wmb();
			set_rx_status_desc_own(pdesc, 1);
			break;
		case HW_DESC_RXBUFF_ADDR:
			set_rx_status__desc_buff_addr(pdesc, *(u32 *)val);
			break;
		case HW_DESC_RXPKT_LEN:
			set_rx_status_desc_pkt_len(pdesc, *(u32 *)val);
			break;
		case HW_DESC_RXERO:
			set_rx_status_desc_eor(pdesc, 1);
			break;
		default:
			WARN_ONCE(true, "rtl8192se: ERR rxdesc :%d not processed\n",
				  desc_name);
			break;
		}
	}
}

u64 rtl92se_get_desc(struct ieee80211_hw *hw,
		     u8 *desc8, bool istx, u8 desc_name)
{
	u32 ret = 0;
	__le32 *desc = (__le32 *)desc8;

	if (istx) {
		switch (desc_name) {
		case HW_DESC_OWN:
			ret = get_tx_desc_own(desc);
			break;
		case HW_DESC_TXBUFF_ADDR:
			ret = get_tx_desc_tx_buffer_address(desc);
			break;
		default:
			WARN_ONCE(true, "rtl8192se: ERR txdesc :%d not processed\n",
				  desc_name);
			break;
		}
	} else {
		switch (desc_name) {
		case HW_DESC_OWN:
			ret = get_rx_status_desc_own(desc);
			break;
		case HW_DESC_RXPKT_LEN:
			ret = get_rx_status_desc_pkt_len(desc);
			break;
		case HW_DESC_RXBUFF_ADDR:
			ret = get_rx_status_desc_buff_addr(desc);
			break;
		default:
			WARN_ONCE(true, "rtl8192se: ERR rxdesc :%d not processed\n",
				  desc_name);
			break;
		}
	}
	return ret;
}

void rtl92se_tx_polling(struct ieee80211_hw *hw, u8 hw_queue)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	rtl_write_word(rtlpriv, TP_POLL, BIT(0) << (hw_queue));
}
