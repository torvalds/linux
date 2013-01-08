/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
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

static u8 _rtl8723ae_map_hwqueue_to_fwqueue(struct sk_buff *skb, u8 hw_queue)
{
	__le16 fc = rtl_get_fc(skb);

	if (unlikely(ieee80211_is_beacon(fc)))
		return QSLT_BEACON;
	if (ieee80211_is_mgmt(fc) || ieee80211_is_ctl(fc))
		return QSLT_MGNT;

	return skb->priority;
}

static void _rtl8723ae_query_rxphystatus(struct ieee80211_hw *hw,
			struct rtl_stats *pstatus, u8 *pdesc,
			struct rx_fwinfo_8723e *p_drvinfo,
			bool bpacket_match_bssid,
			bool bpacket_toself, bool packet_beacon)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtlpriv);
	struct phy_sts_cck_8723e_t *cck_buf;
	s8 rx_pwr_all, rx_pwr[4];
	u8 rf_rx_num = 0, evm, pwdb_all;
	u8 i, max_spatial_stream;
	u32 rssi, total_rssi = 0;
	bool is_cck = pstatus->is_cck;

	/* Record it for next packet processing */
	pstatus->packet_matchbssid = bpacket_match_bssid;
	pstatus->packet_toself = bpacket_toself;
	pstatus->packet_beacon = packet_beacon;
	pstatus->rx_mimo_sig_qual[0] = -1;
	pstatus->rx_mimo_sig_qual[1] = -1;

	if (is_cck) {
		u8 report, cck_highpwr;

		/* CCK Driver info Structure is not the same as OFDM packet. */
		cck_buf = (struct phy_sts_cck_8723e_t *)p_drvinfo;

		/* (1)Hardware does not provide RSSI for CCK
		 * (2)PWDB, Average PWDB cacluated by
		 * hardware (for rate adaptive)
		 */
		if (ppsc->rfpwr_state == ERFON)
			cck_highpwr = (u8) rtl_get_bbreg(hw,
						 RFPGA0_XA_HSSIPARAMETER2,
						 BIT(9));
		else
			cck_highpwr = false;

		if (!cck_highpwr) {
			u8 cck_agc_rpt = cck_buf->cck_agc_rpt;
			report = cck_buf->cck_agc_rpt & 0xc0;
			report = report >> 6;
			switch (report) {
			case 0x3:
				rx_pwr_all = -46 - (cck_agc_rpt & 0x3e);
				break;
			case 0x2:
				rx_pwr_all = -26 - (cck_agc_rpt & 0x3e);
				break;
			case 0x1:
				rx_pwr_all = -12 - (cck_agc_rpt & 0x3e);
				break;
			case 0x0:
				rx_pwr_all = 16 - (cck_agc_rpt & 0x3e);
				break;
			}
		} else {
			u8 cck_agc_rpt = cck_buf->cck_agc_rpt;
			report = p_drvinfo->cfosho[0] & 0x60;
			report = report >> 5;
			switch (report) {
			case 0x3:
				rx_pwr_all = -46 - ((cck_agc_rpt & 0x1f) << 1);
				break;
			case 0x2:
				rx_pwr_all = -26 - ((cck_agc_rpt & 0x1f) << 1);
				break;
			case 0x1:
				rx_pwr_all = -12 - ((cck_agc_rpt & 0x1f) << 1);
				break;
			case 0x0:
				rx_pwr_all = 16 - ((cck_agc_rpt & 0x1f) << 1);
				break;
			}
		}

		pwdb_all = rtl_query_rxpwrpercentage(rx_pwr_all);
		/* CCK gain is smaller than OFDM/MCS gain,
		 * so we add gain diff. From experience, the val is 6
		 */
		pwdb_all += 6;
		if (pwdb_all > 100)
			pwdb_all = 100;
		/* modify the offset to make the same
		 * gain index with OFDM.
		 */
		if (pwdb_all > 34 && pwdb_all <= 42)
			pwdb_all -= 2;
		else if (pwdb_all > 26 && pwdb_all <= 34)
			pwdb_all -= 6;
		else if (pwdb_all > 14 && pwdb_all <= 26)
			pwdb_all -= 8;
		else if (pwdb_all > 4 && pwdb_all <= 14)
			pwdb_all -= 4;

		pstatus->rx_pwdb_all = pwdb_all;
		pstatus->recvsignalpower = rx_pwr_all;

		/* (3) Get Signal Quality (EVM) */
		if (bpacket_match_bssid) {
			u8 sq;

			if (pstatus->rx_pwdb_all > 40) {
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

			pstatus->signalquality = sq;
			pstatus->rx_mimo_sig_qual[0] = sq;
			pstatus->rx_mimo_sig_qual[1] = -1;
		}
	} else {
		rtlpriv->dm.rfpath_rxenable[0] =
		    rtlpriv->dm.rfpath_rxenable[1] = true;

		/* (1)Get RSSI for HT rate */
		for (i = RF90_PATH_A; i < RF6052_MAX_PATH; i++) {

			/* we will judge RF RX path now. */
			if (rtlpriv->dm.rfpath_rxenable[i])
				rf_rx_num++;

			rx_pwr[i] = ((p_drvinfo->gain_trsw[i] & 0x3f)*2) - 110;

			/* Translate DBM to percentage. */
			rssi = rtl_query_rxpwrpercentage(rx_pwr[i]);
			total_rssi += rssi;

			/* Get Rx snr value in DB */
			rtlpriv->stats.rx_snr_db[i] = (p_drvinfo->rxsnr[i] / 2);

			/* Record Signal Strength for next packet */
			if (bpacket_match_bssid)
				pstatus->rx_mimo_signalstrength[i] = (u8) rssi;
		}

		/* (2)PWDB, Average PWDB cacluated by
		 * hardware (for rate adaptive)
		 */
		rx_pwr_all = ((p_drvinfo->pwdb_all >> 1) & 0x7f) - 110;

		pwdb_all = rtl_query_rxpwrpercentage(rx_pwr_all);
		pstatus->rx_pwdb_all = pwdb_all;
		pstatus->rxpower = rx_pwr_all;
		pstatus->recvsignalpower = rx_pwr_all;

		/* (3)EVM of HT rate */
		if (pstatus->is_ht && pstatus->rate >= DESC92_RATEMCS8 &&
		    pstatus->rate <= DESC92_RATEMCS15)
			max_spatial_stream = 2;
		else
			max_spatial_stream = 1;

		for (i = 0; i < max_spatial_stream; i++) {
			evm = rtl_evm_db_to_percentage(p_drvinfo->rxevm[i]);

			if (bpacket_match_bssid) {
				/* Fill value in RFD, Get the first
				 * spatial stream only
				 */
				if (i == 0)
					pstatus->signalquality = (evm & 0xff);
				pstatus->rx_mimo_sig_qual[i] = (evm & 0xff);
			}
		}
	}

	/* UI BSS List signal strength(in percentage),
	 * make it good looking, from 0~100.
	 */
	if (is_cck)
		pstatus->signalstrength = (u8)(rtl_signal_scale_mapping(hw,
			pwdb_all));
	else if (rf_rx_num != 0)
		pstatus->signalstrength = (u8)(rtl_signal_scale_mapping(hw,
			total_rssi /= rf_rx_num));
}

static void _rtl8723ae_translate_rx_signal_stuff(struct ieee80211_hw *hw,
		struct sk_buff *skb, struct rtl_stats *pstatus,
		u8 *pdesc, struct rx_fwinfo_8723e *p_drvinfo)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct ieee80211_hdr *hdr;
	u8 *tmp_buf;
	u8 *praddr;
	u8 *psaddr;
	__le16 fc;
	u16 type;
	bool packet_matchbssid, packet_toself, packet_beacon;

	tmp_buf = skb->data + pstatus->rx_drvinfo_size + pstatus->rx_bufshift;

	hdr = (struct ieee80211_hdr *)tmp_buf;
	fc = hdr->frame_control;
	type = WLAN_FC_GET_TYPE(fc);
	praddr = hdr->addr1;
	psaddr = ieee80211_get_SA(hdr);

	packet_matchbssid = ((IEEE80211_FTYPE_CTL != type) &&
			    (!compare_ether_addr(mac->bssid,
			    (le16_to_cpu(fc) & IEEE80211_FCTL_TODS) ?
			    hdr->addr1 : (le16_to_cpu(fc) &
			    IEEE80211_FCTL_FROMDS) ?
			    hdr->addr2 : hdr->addr3)) && (!pstatus->hwerror) &&
			    (!pstatus->crc) && (!pstatus->icv));

	packet_toself = packet_matchbssid &&
	    (!compare_ether_addr(praddr, rtlefuse->dev_addr));

	if (ieee80211_is_beacon(fc))
		packet_beacon = true;

	_rtl8723ae_query_rxphystatus(hw, pstatus, pdesc, p_drvinfo,
				   packet_matchbssid, packet_toself,
				   packet_beacon);

	rtl_process_phyinfo(hw, tmp_buf, pstatus);
}

bool rtl8723ae_rx_query_desc(struct ieee80211_hw *hw,
			     struct rtl_stats *status,
			     struct ieee80211_rx_status *rx_status,
			     u8 *pdesc, struct sk_buff *skb)
{
	struct rx_fwinfo_8723e *p_drvinfo;
	struct ieee80211_hdr *hdr;
	u32 phystatus = GET_RX_DESC_PHYST(pdesc);

	status->length = (u16) GET_RX_DESC_PKT_LEN(pdesc);
	status->rx_drvinfo_size = (u8) GET_RX_DESC_DRV_INFO_SIZE(pdesc) *
				   RX_DRV_INFO_SIZE_UNIT;
	status->rx_bufshift = (u8) (GET_RX_DESC_SHIFT(pdesc) & 0x03);
	status->icv = (u16) GET_RX_DESC_ICV(pdesc);
	status->crc = (u16) GET_RX_DESC_CRC32(pdesc);
	status->hwerror = (status->crc | status->icv);
	status->decrypted = !GET_RX_DESC_SWDEC(pdesc);
	status->rate = (u8) GET_RX_DESC_RXMCS(pdesc);
	status->shortpreamble = (u16) GET_RX_DESC_SPLCP(pdesc);
	status->isampdu = (bool) (GET_RX_DESC_PAGGR(pdesc) == 1);
	status->isfirst_ampdu = (bool) ((GET_RX_DESC_PAGGR(pdesc) == 1)
				 && (GET_RX_DESC_FAGGR(pdesc) == 1));
	status->timestamp_low = GET_RX_DESC_TSFL(pdesc);
	status->rx_is40Mhzpacket = (bool) GET_RX_DESC_BW(pdesc);
	status->is_ht = (bool)GET_RX_DESC_RXHT(pdesc);

	status->is_cck = RTL8723E_RX_HAL_IS_CCK_RATE(status->rate);

	rx_status->freq = hw->conf.channel->center_freq;
	rx_status->band = hw->conf.channel->band;

	hdr = (struct ieee80211_hdr *)(skb->data + status->rx_drvinfo_size
		+ status->rx_bufshift);

	if (status->crc)
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (status->rx_is40Mhzpacket)
		rx_status->flag |= RX_FLAG_40MHZ;

	if (status->is_ht)
		rx_status->flag |= RX_FLAG_HT;

	rx_status->flag |= RX_FLAG_MACTIME_START;

	/* hw will set status->decrypted true, if it finds the
	 * frame is open data frame or mgmt frame.
	 * Thus hw will not decrypt a robust managment frame
	 * for IEEE80211w but still set status->decrypted
	 * true, so here we should set it back to undecrypted
	 * for IEEE80211w frame, and mac80211 sw will help
	 * to decrypt it
	 */
	if (status->decrypted) {
		if ((ieee80211_is_robust_mgmt_frame(hdr)) &&
			(ieee80211_has_protected(hdr->frame_control)))
			rx_status->flag &= ~RX_FLAG_DECRYPTED;
		else
			rx_status->flag |= RX_FLAG_DECRYPTED;
	}

	/* rate_idx: index of data rate into band's
	 * supported rates or MCS index if HT rates
	 * are use (RX_FLAG_HT)
	 */
	rx_status->rate_idx = rtlwifi_rate_mapping(hw, status->is_ht,
						   status->rate, false);

	rx_status->mactime = status->timestamp_low;
	if (phystatus == true) {
		p_drvinfo = (struct rx_fwinfo_8723e *)(skb->data +
			     status->rx_bufshift);

		_rtl8723ae_translate_rx_signal_stuff(hw,
			   skb, status, pdesc, p_drvinfo);
	}

	/*rx_status->qual = status->signal; */
	rx_status->signal = status->recvsignalpower + 10;
	/*rx_status->noise = -status->noise; */

	return true;
}

void rtl8723ae_tx_fill_desc(struct ieee80211_hw *hw,
			    struct ieee80211_hdr *hdr, u8 *pdesc_tx,
			    struct ieee80211_tx_info *info,
			    struct ieee80211_sta *sta,
			    struct sk_buff *skb, u8 hw_queue,
			    struct rtl_tcb_desc *ptcdesc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	bool defaultadapter = true;
	u8 *pdesc = (u8 *) pdesc_tx;
	u16 seq_number;
	__le16 fc = hdr->frame_control;
	u8 fw_qsel = _rtl8723ae_map_hwqueue_to_fwqueue(skb, hw_queue);
	bool firstseg = ((hdr->seq_ctrl &
			    cpu_to_le16(IEEE80211_SCTL_FRAG)) == 0);
	bool lastseg = ((hdr->frame_control &
			   cpu_to_le16(IEEE80211_FCTL_MOREFRAGS)) == 0);
	dma_addr_t mapping = pci_map_single(rtlpci->pdev,
					    skb->data, skb->len,
					    PCI_DMA_TODEVICE);
	u8 bw_40 = 0;

	if (pci_dma_mapping_error(rtlpci->pdev, mapping)) {
		RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE,
			 "DMA mapping error");
		return;
	}
	if (mac->opmode == NL80211_IFTYPE_STATION) {
		bw_40 = mac->bw_40;
	} else if (mac->opmode == NL80211_IFTYPE_AP ||
		mac->opmode == NL80211_IFTYPE_ADHOC) {
		if (sta)
			bw_40 = sta->ht_cap.cap &
				IEEE80211_HT_CAP_SUP_WIDTH_20_40;
	}

	seq_number = (le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ) >> 4;

	rtl_get_tcb_desc(hw, info, sta, skb, ptcdesc);

	CLEAR_PCI_TX_DESC_CONTENT(pdesc, sizeof(struct tx_desc_8723e));

	if (ieee80211_is_nullfunc(fc) || ieee80211_is_ctl(fc)) {
		firstseg = true;
		lastseg = true;
	}

	if (firstseg) {
		SET_TX_DESC_OFFSET(pdesc, USB_HWDESC_HEADER_LEN);

		SET_TX_DESC_TX_RATE(pdesc, ptcdesc->hw_rate);

		if (ptcdesc->use_shortgi || ptcdesc->use_shortpreamble)
			SET_TX_DESC_DATA_SHORTGI(pdesc, 1);

		if (info->flags & IEEE80211_TX_CTL_AMPDU) {
			SET_TX_DESC_AGG_BREAK(pdesc, 1);
			SET_TX_DESC_MAX_AGG_NUM(pdesc, 0x14);
		}
		SET_TX_DESC_SEQ(pdesc, seq_number);

		SET_TX_DESC_RTS_ENABLE(pdesc, ((ptcdesc->rts_enable &&
						!ptcdesc->
						cts_enable) ? 1 : 0));
		SET_TX_DESC_HW_RTS_ENABLE(pdesc,
					  ((ptcdesc->rts_enable
					    || ptcdesc->cts_enable) ? 1 : 0));
		SET_TX_DESC_CTS2SELF(pdesc, ((ptcdesc->cts_enable) ? 1 : 0));
		SET_TX_DESC_RTS_STBC(pdesc, ((ptcdesc->rts_stbc) ? 1 : 0));

		SET_TX_DESC_RTS_RATE(pdesc, ptcdesc->rts_rate);
		SET_TX_DESC_RTS_BW(pdesc, 0);
		SET_TX_DESC_RTS_SC(pdesc, ptcdesc->rts_sc);
		SET_TX_DESC_RTS_SHORT(pdesc,
				      ((ptcdesc->rts_rate <= DESC92_RATE54M) ?
				       (ptcdesc->rts_use_shortpreamble ? 1 : 0)
				       : (ptcdesc->rts_use_shortgi ? 1 : 0)));

		if (bw_40) {
			if (ptcdesc->packet_bw) {
				SET_TX_DESC_DATA_BW(pdesc, 1);
				SET_TX_DESC_TX_SUB_CARRIER(pdesc, 3);
			} else {
				SET_TX_DESC_DATA_BW(pdesc, 0);
				SET_TX_DESC_TX_SUB_CARRIER(pdesc,
							 mac->cur_40_prime_sc);
			}
		} else {
			SET_TX_DESC_DATA_BW(pdesc, 0);
			SET_TX_DESC_TX_SUB_CARRIER(pdesc, 0);
		}

		SET_TX_DESC_LINIP(pdesc, 0);
		SET_TX_DESC_PKT_SIZE(pdesc, (u16) skb->len);

		if (sta) {
			u8 ampdu_density = sta->ht_cap.ampdu_density;
			SET_TX_DESC_AMPDU_DENSITY(pdesc, ampdu_density);
		}

		if (info->control.hw_key) {
			struct ieee80211_key_conf *keyconf =
			    info->control.hw_key;

			switch (keyconf->cipher) {
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

		SET_TX_DESC_PKT_ID(pdesc, 0);
		SET_TX_DESC_QUEUE_SEL(pdesc, fw_qsel);

		SET_TX_DESC_DATA_RATE_FB_LIMIT(pdesc, 0x1F);
		SET_TX_DESC_RTS_RATE_FB_LIMIT(pdesc, 0xF);
		SET_TX_DESC_DISABLE_FB(pdesc, 0);
		SET_TX_DESC_USE_RATE(pdesc, ptcdesc->use_driver_rate ? 1 : 0);

		if (ieee80211_is_data_qos(fc)) {
			if (mac->rdg_en) {
				RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE,
					 "Enable RDG function.\n");
				SET_TX_DESC_RDG_ENABLE(pdesc, 1);
				SET_TX_DESC_HTC(pdesc, 1);
			}
		}
	}

	SET_TX_DESC_FIRST_SEG(pdesc, (firstseg ? 1 : 0));
	SET_TX_DESC_LAST_SEG(pdesc, (lastseg ? 1 : 0));

	SET_TX_DESC_TX_BUFFER_SIZE(pdesc, (u16) skb->len);

	SET_TX_DESC_TX_BUFFER_ADDRESS(pdesc, mapping);

	if (rtlpriv->dm.useramask) {
		SET_TX_DESC_RATE_ID(pdesc, ptcdesc->ratr_index);
		SET_TX_DESC_MACID(pdesc, ptcdesc->mac_id);
	} else {
		SET_TX_DESC_RATE_ID(pdesc, 0xC + ptcdesc->ratr_index);
		SET_TX_DESC_MACID(pdesc, ptcdesc->ratr_index);
	}

	if ((!ieee80211_is_data_qos(fc)) && ppsc->fwctrl_lps) {
		SET_TX_DESC_HWSEQ_EN_8723(pdesc, 1);

		if (!defaultadapter)
			SET_TX_DESC_HWSEQ_SEL_8723(pdesc, 1);
	}

	SET_TX_DESC_MORE_FRAG(pdesc, (lastseg ? 0 : 1));

	if (is_multicast_ether_addr(ieee80211_get_DA(hdr)) ||
	    is_broadcast_ether_addr(ieee80211_get_DA(hdr))) {
		SET_TX_DESC_BMC(pdesc, 1);
	}

	RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE, "\n");
}

void rtl8723ae_tx_fill_cmddesc(struct ieee80211_hw *hw,
			      u8 *pdesc, bool firstseg,
			      bool lastseg, struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)(skb->data);
	u8 fw_queue = QSLT_BEACON;
	dma_addr_t mapping = pci_map_single(rtlpci->pdev,
					    skb->data, skb->len,
					    PCI_DMA_TODEVICE);
	__le16 fc = hdr->frame_control;

	if (pci_dma_mapping_error(rtlpci->pdev, mapping)) {
		RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE,
			 "DMA mapping error");
		return;
	}
	CLEAR_PCI_TX_DESC_CONTENT(pdesc, TX_DESC_SIZE);

	if (firstseg)
		SET_TX_DESC_OFFSET(pdesc, USB_HWDESC_HEADER_LEN);

	SET_TX_DESC_TX_RATE(pdesc, DESC92_RATE1M);

	SET_TX_DESC_SEQ(pdesc, 0);

	SET_TX_DESC_LINIP(pdesc, 0);

	SET_TX_DESC_QUEUE_SEL(pdesc, fw_queue);

	SET_TX_DESC_FIRST_SEG(pdesc, 1);
	SET_TX_DESC_LAST_SEG(pdesc, 1);

	SET_TX_DESC_TX_BUFFER_SIZE(pdesc, (u16) (skb->len));

	SET_TX_DESC_TX_BUFFER_ADDRESS(pdesc, mapping);

	SET_TX_DESC_RATE_ID(pdesc, 7);
	SET_TX_DESC_MACID(pdesc, 0);

	SET_TX_DESC_OWN(pdesc, 1);

	SET_TX_DESC_PKT_SIZE((u8 *) pdesc, (u16) (skb->len));

	SET_TX_DESC_FIRST_SEG(pdesc, 1);
	SET_TX_DESC_LAST_SEG(pdesc, 1);

	SET_TX_DESC_OFFSET(pdesc, 0x20);

	SET_TX_DESC_USE_RATE(pdesc, 1);

	if (!ieee80211_is_data_qos(fc)) {
		SET_TX_DESC_HWSEQ_EN_8723(pdesc, 1);
		/* SET_TX_DESC_HWSEQ_EN(pdesc, 1); */
		/* SET_TX_DESC_PKT_ID(pdesc, 8); */
	}

	RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_LOUD,
		      "H2C Tx Cmd Content\n",
		      pdesc, TX_DESC_SIZE);
}

void rtl8723ae_set_desc(u8 *pdesc, bool istx, u8 desc_name, u8 *val)
{
	if (istx == true) {
		switch (desc_name) {
		case HW_DESC_OWN:
			SET_TX_DESC_OWN(pdesc, 1);
			break;
		case HW_DESC_TX_NEXTDESC_ADDR:
			SET_TX_DESC_NEXT_DESC_ADDRESS(pdesc, *(u32 *) val);
			break;
		default:
			RT_ASSERT(false, "ERR txdesc :%d not process\n",
				  desc_name);
			break;
		}
	} else {
		switch (desc_name) {
		case HW_DESC_RXOWN:
			SET_RX_DESC_OWN(pdesc, 1);
			break;
		case HW_DESC_RXBUFF_ADDR:
			SET_RX_DESC_BUFF_ADDR(pdesc, *(u32 *) val);
			break;
		case HW_DESC_RXPKT_LEN:
			SET_RX_DESC_PKT_LEN(pdesc, *(u32 *) val);
			break;
		case HW_DESC_RXERO:
			SET_RX_DESC_EOR(pdesc, 1);
			break;
		default:
			RT_ASSERT(false, "ERR rxdesc :%d not process\n",
				  desc_name);
			break;
		}
	}
}

u32 rtl8723ae_get_desc(u8 *pdesc, bool istx, u8 desc_name)
{
	u32 ret = 0;

	if (istx == true) {
		switch (desc_name) {
		case HW_DESC_OWN:
			ret = GET_TX_DESC_OWN(pdesc);
			break;
		case HW_DESC_TXBUFF_ADDR:
			ret = GET_TX_DESC_TX_BUFFER_ADDRESS(pdesc);
			break;
		default:
			RT_ASSERT(false, "ERR txdesc :%d not process\n",
				  desc_name);
			break;
		}
	} else {
		switch (desc_name) {
		case HW_DESC_OWN:
			ret = GET_RX_DESC_OWN(pdesc);
			break;
		case HW_DESC_RXPKT_LEN:
			ret = GET_RX_DESC_PKT_LEN(pdesc);
			break;
		default:
			RT_ASSERT(false, "ERR rxdesc :%d not process\n",
				  desc_name);
			break;
		}
	}
	return ret;
}

void rtl8723ae_tx_polling(struct ieee80211_hw *hw, u8 hw_queue)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	if (hw_queue == BEACON_QUEUE) {
		rtl_write_word(rtlpriv, REG_PCIE_CTRL_REG, BIT(4));
	} else {
		rtl_write_word(rtlpriv, REG_PCIE_CTRL_REG,
			       BIT(0) << (hw_queue));
	}
}
