// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../wifi.h"
#include "../base.h"
#include "../stats.h"
#include "def.h"
#include "trx_common.h"

static long _rtl92d_translate_todbm(struct ieee80211_hw *hw,
				    u8 signal_strength_index)
{
	long signal_power;

	signal_power = (long)((signal_strength_index + 1) >> 1);
	signal_power -= 95;
	return signal_power;
}

static void _rtl92d_query_rxphystatus(struct ieee80211_hw *hw,
				      struct rtl_stats *pstats,
				      __le32 *pdesc,
				      struct rx_fwinfo_92d *p_drvinfo,
				      bool packet_match_bssid,
				      bool packet_toself,
				      bool packet_beacon)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_ps_ctl *ppsc = rtl_psc(rtlpriv);
	struct phy_sts_cck_8192d *cck_buf;
	s8 rx_pwr_all, rx_pwr[4];
	u8 rf_rx_num = 0, evm, pwdb_all;
	u8 i, max_spatial_stream;
	u32 rssi, total_rssi = 0;
	bool is_cck_rate;
	u8 rxmcs;

	rxmcs = get_rx_desc_rxmcs(pdesc);
	is_cck_rate = rxmcs <= DESC_RATE11M;
	pstats->packet_matchbssid = packet_match_bssid;
	pstats->packet_toself = packet_toself;
	pstats->packet_beacon = packet_beacon;
	pstats->is_cck = is_cck_rate;
	pstats->rx_mimo_sig_qual[0] = -1;
	pstats->rx_mimo_sig_qual[1] = -1;

	if (is_cck_rate) {
		u8 report, cck_highpwr;

		cck_buf = (struct phy_sts_cck_8192d *)p_drvinfo;
		if (ppsc->rfpwr_state == ERFON)
			cck_highpwr = rtlphy->cck_high_power;
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
		rtlpriv->dm.rfpath_rxenable[0] = true;
		rtlpriv->dm.rfpath_rxenable[1] = true;
		for (i = RF90_PATH_A; i < RF6052_MAX_PATH; i++) {
			if (rtlpriv->dm.rfpath_rxenable[i])
				rf_rx_num++;
			rx_pwr[i] = ((p_drvinfo->gain_trsw[i] & 0x3f) * 2)
				    - 110;
			rssi = rtl_query_rxpwrpercentage(rx_pwr[i]);
			total_rssi += rssi;
			rtlpriv->stats.rx_snr_db[i] =
					 (long)(p_drvinfo->rxsnr[i] / 2);
			if (packet_match_bssid)
				pstats->rx_mimo_signalstrength[i] = (u8)rssi;
		}
		rx_pwr_all = ((p_drvinfo->pwdb_all >> 1) & 0x7f) - 106;
		pwdb_all = rtl_query_rxpwrpercentage(rx_pwr_all);
		pstats->rx_pwdb_all = pwdb_all;
		pstats->rxpower = rx_pwr_all;
		pstats->recvsignalpower = rx_pwr_all;
		if (get_rx_desc_rxht(pdesc) && rxmcs >= DESC_RATEMCS8 &&
		    rxmcs <= DESC_RATEMCS15)
			max_spatial_stream = 2;
		else
			max_spatial_stream = 1;
		for (i = 0; i < max_spatial_stream; i++) {
			evm = rtl_evm_db_to_percentage(p_drvinfo->rxevm[i]);
			if (packet_match_bssid) {
				if (i == 0)
					pstats->signalquality =
						 (u8)(evm & 0xff);
				pstats->rx_mimo_sig_qual[i] =
						 (u8)(evm & 0xff);
			}
		}
	}
	if (is_cck_rate)
		pstats->signalstrength = (u8)(rtl_signal_scale_mapping(hw,
				pwdb_all));
	else if (rf_rx_num != 0)
		pstats->signalstrength = (u8)(rtl_signal_scale_mapping(hw,
				total_rssi /= rf_rx_num));
}

static void rtl92d_loop_over_paths(struct ieee80211_hw *hw,
				   struct rtl_stats *pstats)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 rfpath;

	for (rfpath = RF90_PATH_A; rfpath < rtlphy->num_total_rfpath;
	     rfpath++) {
		if (rtlpriv->stats.rx_rssi_percentage[rfpath] == 0) {
			rtlpriv->stats.rx_rssi_percentage[rfpath] =
			    pstats->rx_mimo_signalstrength[rfpath];
		}
		if (pstats->rx_mimo_signalstrength[rfpath] >
		    rtlpriv->stats.rx_rssi_percentage[rfpath]) {
			rtlpriv->stats.rx_rssi_percentage[rfpath] =
			    ((rtlpriv->stats.rx_rssi_percentage[rfpath] *
			      (RX_SMOOTH_FACTOR - 1)) +
			     (pstats->rx_mimo_signalstrength[rfpath])) /
			    (RX_SMOOTH_FACTOR);
			rtlpriv->stats.rx_rssi_percentage[rfpath] =
			    rtlpriv->stats.rx_rssi_percentage[rfpath] + 1;
		} else {
			rtlpriv->stats.rx_rssi_percentage[rfpath] =
			    ((rtlpriv->stats.rx_rssi_percentage[rfpath] *
			      (RX_SMOOTH_FACTOR - 1)) +
			     (pstats->rx_mimo_signalstrength[rfpath])) /
			    (RX_SMOOTH_FACTOR);
		}
	}
}

static void _rtl92d_process_ui_rssi(struct ieee80211_hw *hw,
				    struct rtl_stats *pstats)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rt_smooth_data *ui_rssi;
	u32 last_rssi, tmpval;

	if (!pstats->packet_toself && !pstats->packet_beacon)
		return;

	ui_rssi = &rtlpriv->stats.ui_rssi;

	rtlpriv->stats.rssi_calculate_cnt++;
	if (ui_rssi->total_num++ >= PHY_RSSI_SLID_WIN_MAX) {
		ui_rssi->total_num = PHY_RSSI_SLID_WIN_MAX;
		last_rssi = ui_rssi->elements[ui_rssi->index];
		ui_rssi->total_val -= last_rssi;
	}
	ui_rssi->total_val += pstats->signalstrength;
	ui_rssi->elements[ui_rssi->index++] = pstats->signalstrength;
	if (ui_rssi->index >= PHY_RSSI_SLID_WIN_MAX)
		ui_rssi->index = 0;
	tmpval = ui_rssi->total_val / ui_rssi->total_num;
	rtlpriv->stats.signal_strength = _rtl92d_translate_todbm(hw, (u8)tmpval);
	pstats->rssi = rtlpriv->stats.signal_strength;

	if (!pstats->is_cck && pstats->packet_toself)
		rtl92d_loop_over_paths(hw, pstats);
}

static void _rtl92d_update_rxsignalstatistics(struct ieee80211_hw *hw,
					      struct rtl_stats *pstats)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int weighting = 0;

	if (rtlpriv->stats.recv_signal_power == 0)
		rtlpriv->stats.recv_signal_power = pstats->recvsignalpower;
	if (pstats->recvsignalpower > rtlpriv->stats.recv_signal_power)
		weighting = 5;
	else if (pstats->recvsignalpower < rtlpriv->stats.recv_signal_power)
		weighting = (-5);
	rtlpriv->stats.recv_signal_power = (rtlpriv->stats.recv_signal_power *
		5 + pstats->recvsignalpower + weighting) / 6;
}

static void _rtl92d_process_pwdb(struct ieee80211_hw *hw,
				 struct rtl_stats *pstats)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	long undec_sm_pwdb;

	if (mac->opmode == NL80211_IFTYPE_ADHOC	||
	    mac->opmode == NL80211_IFTYPE_AP)
		return;

	undec_sm_pwdb = rtlpriv->dm.undec_sm_pwdb;

	if (pstats->packet_toself || pstats->packet_beacon) {
		if (undec_sm_pwdb < 0)
			undec_sm_pwdb = pstats->rx_pwdb_all;
		if (pstats->rx_pwdb_all > (u32)undec_sm_pwdb) {
			undec_sm_pwdb = (((undec_sm_pwdb) *
			      (RX_SMOOTH_FACTOR - 1)) +
			      (pstats->rx_pwdb_all)) / (RX_SMOOTH_FACTOR);
			undec_sm_pwdb = undec_sm_pwdb + 1;
		} else {
			undec_sm_pwdb = (((undec_sm_pwdb) *
			      (RX_SMOOTH_FACTOR - 1)) +
			      (pstats->rx_pwdb_all)) / (RX_SMOOTH_FACTOR);
		}
		rtlpriv->dm.undec_sm_pwdb = undec_sm_pwdb;
		_rtl92d_update_rxsignalstatistics(hw, pstats);
	}
}

static void rtl92d_loop_over_streams(struct ieee80211_hw *hw,
				     struct rtl_stats *pstats)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int stream;

	for (stream = 0; stream < 2; stream++) {
		if (pstats->rx_mimo_sig_qual[stream] != -1) {
			if (rtlpriv->stats.rx_evm_percentage[stream] == 0) {
				rtlpriv->stats.rx_evm_percentage[stream] =
				    pstats->rx_mimo_sig_qual[stream];
			}
			rtlpriv->stats.rx_evm_percentage[stream] =
			    ((rtlpriv->stats.rx_evm_percentage[stream]
			      * (RX_SMOOTH_FACTOR - 1)) +
			     (pstats->rx_mimo_sig_qual[stream] * 1)) /
			    (RX_SMOOTH_FACTOR);
		}
	}
}

static void _rtl92d_process_ui_link_quality(struct ieee80211_hw *hw,
					    struct rtl_stats *pstats)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rt_smooth_data *ui_link_quality;
	u32 last_evm, tmpval;

	if (pstats->signalquality == 0)
		return;
	if (!pstats->packet_toself && !pstats->packet_beacon)
		return;

	ui_link_quality = &rtlpriv->stats.ui_link_quality;

	if (ui_link_quality->total_num++ >= PHY_LINKQUALITY_SLID_WIN_MAX) {
		ui_link_quality->total_num = PHY_LINKQUALITY_SLID_WIN_MAX;
		last_evm = ui_link_quality->elements[ui_link_quality->index];
		ui_link_quality->total_val -= last_evm;
	}
	ui_link_quality->total_val += pstats->signalquality;
	ui_link_quality->elements[ui_link_quality->index++] = pstats->signalquality;
	if (ui_link_quality->index >= PHY_LINKQUALITY_SLID_WIN_MAX)
		ui_link_quality->index = 0;
	tmpval = ui_link_quality->total_val / ui_link_quality->total_num;
	rtlpriv->stats.signal_quality = tmpval;
	rtlpriv->stats.last_sigstrength_inpercent = tmpval;
	rtl92d_loop_over_streams(hw, pstats);
}

static void _rtl92d_process_phyinfo(struct ieee80211_hw *hw,
				    u8 *buffer,
				    struct rtl_stats *pcurrent_stats)
{
	if (!pcurrent_stats->packet_matchbssid &&
	    !pcurrent_stats->packet_beacon)
		return;

	_rtl92d_process_ui_rssi(hw, pcurrent_stats);
	_rtl92d_process_pwdb(hw, pcurrent_stats);
	_rtl92d_process_ui_link_quality(hw, pcurrent_stats);
}

static void _rtl92d_translate_rx_signal_stuff(struct ieee80211_hw *hw,
					      struct sk_buff *skb,
					      struct rtl_stats *pstats,
					      __le32 *pdesc,
					      struct rx_fwinfo_92d *p_drvinfo)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct ieee80211_hdr *hdr;
	bool packet_matchbssid;
	bool packet_beacon;
	bool packet_toself;
	u16 type, cfc;
	u8 *tmp_buf;
	u8 *praddr;
	__le16 fc;

	tmp_buf = skb->data + pstats->rx_drvinfo_size + pstats->rx_bufshift;
	hdr = (struct ieee80211_hdr *)tmp_buf;
	fc = hdr->frame_control;
	cfc = le16_to_cpu(fc);
	type = WLAN_FC_GET_TYPE(fc);
	praddr = hdr->addr1;
	packet_matchbssid = ((type != IEEE80211_FTYPE_CTL) &&
	     ether_addr_equal(mac->bssid,
			      (cfc & IEEE80211_FCTL_TODS) ? hdr->addr1 :
			      (cfc & IEEE80211_FCTL_FROMDS) ? hdr->addr2 :
			      hdr->addr3) &&
	     (!pstats->hwerror) && (!pstats->crc) && (!pstats->icv));
	packet_toself = packet_matchbssid &&
			ether_addr_equal(praddr, rtlefuse->dev_addr);
	packet_beacon = ieee80211_is_beacon(fc);
	_rtl92d_query_rxphystatus(hw, pstats, pdesc, p_drvinfo,
				  packet_matchbssid, packet_toself,
				  packet_beacon);
	_rtl92d_process_phyinfo(hw, tmp_buf, pstats);
}

bool rtl92d_rx_query_desc(struct ieee80211_hw *hw, struct rtl_stats *stats,
			  struct ieee80211_rx_status *rx_status,
			  u8 *pdesc8, struct sk_buff *skb)
{
	__le32 *pdesc = (__le32 *)pdesc8;
	struct rx_fwinfo_92d *p_drvinfo;
	u32 phystatus = get_rx_desc_physt(pdesc);

	stats->length = (u16)get_rx_desc_pkt_len(pdesc);
	stats->rx_drvinfo_size = (u8)get_rx_desc_drv_info_size(pdesc) *
				 RX_DRV_INFO_SIZE_UNIT;
	stats->rx_bufshift = (u8)(get_rx_desc_shift(pdesc) & 0x03);
	stats->icv = (u16)get_rx_desc_icv(pdesc);
	stats->crc = (u16)get_rx_desc_crc32(pdesc);
	stats->hwerror = (stats->crc | stats->icv);
	stats->decrypted = !get_rx_desc_swdec(pdesc) &&
			   get_rx_desc_enc_type(pdesc) != RX_DESC_ENC_NONE;
	stats->rate = (u8)get_rx_desc_rxmcs(pdesc);
	stats->shortpreamble = (u16)get_rx_desc_splcp(pdesc);
	stats->isampdu = (bool)(get_rx_desc_paggr(pdesc) == 1);
	stats->isfirst_ampdu = (bool)((get_rx_desc_paggr(pdesc) == 1) &&
				      (get_rx_desc_faggr(pdesc) == 1));
	stats->timestamp_low = get_rx_desc_tsfl(pdesc);
	stats->rx_is40mhzpacket = (bool)get_rx_desc_bw(pdesc);
	stats->is_ht = (bool)get_rx_desc_rxht(pdesc);
	rx_status->freq = hw->conf.chandef.chan->center_freq;
	rx_status->band = hw->conf.chandef.chan->band;
	if (get_rx_desc_crc32(pdesc))
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;
	if (get_rx_desc_bw(pdesc))
		rx_status->bw = RATE_INFO_BW_40;
	if (get_rx_desc_rxht(pdesc))
		rx_status->encoding = RX_ENC_HT;
	rx_status->flag |= RX_FLAG_MACTIME_START;
	if (stats->decrypted)
		rx_status->flag |= RX_FLAG_DECRYPTED;
	rx_status->rate_idx = rtlwifi_rate_mapping(hw, stats->is_ht,
						   false, stats->rate);
	rx_status->mactime = get_rx_desc_tsfl(pdesc);
	if (phystatus) {
		p_drvinfo = (struct rx_fwinfo_92d *)(skb->data +
						     stats->rx_bufshift);
		_rtl92d_translate_rx_signal_stuff(hw, skb, stats, pdesc,
						  p_drvinfo);
	}
	/*rx_status->qual = stats->signal; */
	rx_status->signal = stats->recvsignalpower + 10;
	return true;
}
EXPORT_SYMBOL_GPL(rtl92d_rx_query_desc);

void rtl92d_set_desc(struct ieee80211_hw *hw, u8 *pdesc8, bool istx,
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
			WARN_ONCE(true, "rtl8192de: ERR txdesc :%d not processed\n",
				  desc_name);
			break;
		}
	} else {
		switch (desc_name) {
		case HW_DESC_RXOWN:
			wmb();
			set_rx_desc_own(pdesc, 1);
			break;
		case HW_DESC_RXBUFF_ADDR:
			set_rx_desc_buff_addr(pdesc, *(u32 *)val);
			break;
		case HW_DESC_RXPKT_LEN:
			set_rx_desc_pkt_len(pdesc, *(u32 *)val);
			break;
		case HW_DESC_RXERO:
			set_rx_desc_eor(pdesc, 1);
			break;
		default:
			WARN_ONCE(true, "rtl8192de: ERR rxdesc :%d not processed\n",
				  desc_name);
			break;
		}
	}
}
EXPORT_SYMBOL_GPL(rtl92d_set_desc);

u64 rtl92d_get_desc(struct ieee80211_hw *hw,
		    u8 *p_desc8, bool istx, u8 desc_name)
{
	__le32 *p_desc = (__le32 *)p_desc8;
	u32 ret = 0;

	if (istx) {
		switch (desc_name) {
		case HW_DESC_OWN:
			ret = get_tx_desc_own(p_desc);
			break;
		case HW_DESC_TXBUFF_ADDR:
			ret = get_tx_desc_tx_buffer_address(p_desc);
			break;
		default:
			WARN_ONCE(true, "rtl8192de: ERR txdesc :%d not processed\n",
				  desc_name);
			break;
		}
	} else {
		switch (desc_name) {
		case HW_DESC_OWN:
			ret = get_rx_desc_own(p_desc);
			break;
		case HW_DESC_RXPKT_LEN:
			ret = get_rx_desc_pkt_len(p_desc);
		break;
		case HW_DESC_RXBUFF_ADDR:
			ret = get_rx_desc_buff_addr(p_desc);
			break;
		default:
			WARN_ONCE(true, "rtl8192de: ERR rxdesc :%d not processed\n",
				  desc_name);
			break;
		}
	}
	return ret;
}
EXPORT_SYMBOL_GPL(rtl92d_get_desc);
