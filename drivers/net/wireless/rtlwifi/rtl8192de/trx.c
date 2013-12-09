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
#include "reg.h"
#include "def.h"
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

static u8 _rtl92d_query_rxpwrpercentage(char antpower)
{
	if ((antpower <= -100) || (antpower >= 20))
		return 0;
	else if (antpower >= 0)
		return 100;
	else
		return 100 + antpower;
}

static u8 _rtl92d_evm_db_to_percentage(char value)
{
	char ret_val = value;

	if (ret_val >= 0)
		ret_val = 0;
	if (ret_val <= -33)
		ret_val = -33;
	ret_val = 0 - ret_val;
	ret_val *= 3;
	if (ret_val == 99)
		ret_val = 100;
	return ret_val;
}

static long _rtl92de_translate_todbm(struct ieee80211_hw *hw,
				     u8 signal_strength_index)
{
	long signal_power;

	signal_power = (long)((signal_strength_index + 1) >> 1);
	signal_power -= 95;
	return signal_power;
}

static long _rtl92de_signal_scale_mapping(struct ieee80211_hw *hw, long currsig)
{
	long retsig;

	if (currsig >= 61 && currsig <= 100)
		retsig = 90 + ((currsig - 60) / 4);
	else if (currsig >= 41 && currsig <= 60)
		retsig = 78 + ((currsig - 40) / 2);
	else if (currsig >= 31 && currsig <= 40)
		retsig = 66 + (currsig - 30);
	else if (currsig >= 21 && currsig <= 30)
		retsig = 54 + (currsig - 20);
	else if (currsig >= 5 && currsig <= 20)
		retsig = 42 + (((currsig - 5) * 2) / 3);
	else if (currsig == 4)
		retsig = 36;
	else if (currsig == 3)
		retsig = 27;
	else if (currsig == 2)
		retsig = 18;
	else if (currsig == 1)
		retsig = 9;
	else
		retsig = currsig;
	return retsig;
}

static void _rtl92de_query_rxphystatus(struct ieee80211_hw *hw,
				       struct rtl_stats *pstats,
				       struct rx_desc_92d *pdesc,
				       struct rx_fwinfo_92d *p_drvinfo,
				       bool packet_match_bssid,
				       bool packet_toself,
				       bool packet_beacon)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtlpriv);
	struct phy_sts_cck_8192d *cck_buf;
	s8 rx_pwr_all, rx_pwr[4];
	u8 rf_rx_num = 0, evm, pwdb_all;
	u8 i, max_spatial_stream;
	u32 rssi, total_rssi = 0;
	bool is_cck_rate;

	is_cck_rate = RX_HAL_IS_CCK_RATE(pdesc);
	pstats->packet_matchbssid = packet_match_bssid;
	pstats->packet_toself = packet_toself;
	pstats->packet_beacon = packet_beacon;
	pstats->is_cck = is_cck_rate;
	pstats->rx_mimo_signalquality[0] = -1;
	pstats->rx_mimo_signalquality[1] = -1;

	if (is_cck_rate) {
		u8 report, cck_highpwr;
		cck_buf = (struct phy_sts_cck_8192d *)p_drvinfo;
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
		pwdb_all = _rtl92d_query_rxpwrpercentage(rx_pwr_all);
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
			pstats->rx_mimo_signalquality[0] = sq;
			pstats->rx_mimo_signalquality[1] = -1;
		}
	} else {
		rtlpriv->dm.rfpath_rxenable[0] = true;
		rtlpriv->dm.rfpath_rxenable[1] = true;
		for (i = RF90_PATH_A; i < RF6052_MAX_PATH; i++) {
			if (rtlpriv->dm.rfpath_rxenable[i])
				rf_rx_num++;
			rx_pwr[i] = ((p_drvinfo->gain_trsw[i] & 0x3f) * 2)
				    - 110;
			rssi = _rtl92d_query_rxpwrpercentage(rx_pwr[i]);
			total_rssi += rssi;
			rtlpriv->stats.rx_snr_db[i] =
					 (long)(p_drvinfo->rxsnr[i] / 2);
			if (packet_match_bssid)
				pstats->rx_mimo_signalstrength[i] = (u8) rssi;
		}
		rx_pwr_all = ((p_drvinfo->pwdb_all >> 1) & 0x7f) - 106;
		pwdb_all = _rtl92d_query_rxpwrpercentage(rx_pwr_all);
		pstats->rx_pwdb_all = pwdb_all;
		pstats->rxpower = rx_pwr_all;
		pstats->recvsignalpower = rx_pwr_all;
		if (pdesc->rxht && pdesc->rxmcs >= DESC92_RATEMCS8 &&
		    pdesc->rxmcs <= DESC92_RATEMCS15)
			max_spatial_stream = 2;
		else
			max_spatial_stream = 1;
		for (i = 0; i < max_spatial_stream; i++) {
			evm = _rtl92d_evm_db_to_percentage(p_drvinfo->rxevm[i]);
			if (packet_match_bssid) {
				if (i == 0)
					pstats->signalquality =
						 (u8)(evm & 0xff);
				pstats->rx_mimo_signalquality[i] =
						 (u8)(evm & 0xff);
			}
		}
	}
	if (is_cck_rate)
		pstats->signalstrength = (u8)(_rtl92de_signal_scale_mapping(hw,
				pwdb_all));
	else if (rf_rx_num != 0)
		pstats->signalstrength = (u8)(_rtl92de_signal_scale_mapping(hw,
				total_rssi /= rf_rx_num));
}

static void rtl92d_loop_over_paths(struct ieee80211_hw *hw,
				   struct rtl_stats *pstats)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
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

static void _rtl92de_process_ui_rssi(struct ieee80211_hw *hw,
				     struct rtl_stats *pstats)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 last_rssi, tmpval;

	if (pstats->packet_toself || pstats->packet_beacon) {
		rtlpriv->stats.rssi_calculate_cnt++;
		if (rtlpriv->stats.ui_rssi.total_num++ >=
		    PHY_RSSI_SLID_WIN_MAX) {
			rtlpriv->stats.ui_rssi.total_num =
						 PHY_RSSI_SLID_WIN_MAX;
			last_rssi = rtlpriv->stats.ui_rssi.elements[
				rtlpriv->stats.ui_rssi.index];
			rtlpriv->stats.ui_rssi.total_val -= last_rssi;
		}
		rtlpriv->stats.ui_rssi.total_val += pstats->signalstrength;
		rtlpriv->stats.ui_rssi.elements
			[rtlpriv->stats.ui_rssi.index++] =
			pstats->signalstrength;
		if (rtlpriv->stats.ui_rssi.index >= PHY_RSSI_SLID_WIN_MAX)
			rtlpriv->stats.ui_rssi.index = 0;
		tmpval = rtlpriv->stats.ui_rssi.total_val /
			rtlpriv->stats.ui_rssi.total_num;
		rtlpriv->stats.signal_strength = _rtl92de_translate_todbm(hw,
			(u8) tmpval);
		pstats->rssi = rtlpriv->stats.signal_strength;
	}
	if (!pstats->is_cck && pstats->packet_toself)
		rtl92d_loop_over_paths(hw, pstats);
}

static void _rtl92de_update_rxsignalstatistics(struct ieee80211_hw *hw,
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

static void _rtl92de_process_pwdb(struct ieee80211_hw *hw,
				  struct rtl_stats *pstats)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	long undecorated_smoothed_pwdb;

	if (mac->opmode == NL80211_IFTYPE_ADHOC	||
		mac->opmode == NL80211_IFTYPE_AP)
		return;
	else
		undecorated_smoothed_pwdb =
		    rtlpriv->dm.undecorated_smoothed_pwdb;

	if (pstats->packet_toself || pstats->packet_beacon) {
		if (undecorated_smoothed_pwdb < 0)
			undecorated_smoothed_pwdb = pstats->rx_pwdb_all;
		if (pstats->rx_pwdb_all > (u32) undecorated_smoothed_pwdb) {
			undecorated_smoothed_pwdb =
			      (((undecorated_smoothed_pwdb) *
			      (RX_SMOOTH_FACTOR - 1)) +
			      (pstats->rx_pwdb_all)) / (RX_SMOOTH_FACTOR);
			undecorated_smoothed_pwdb =
			      undecorated_smoothed_pwdb + 1;
		} else {
			undecorated_smoothed_pwdb =
			      (((undecorated_smoothed_pwdb) *
			      (RX_SMOOTH_FACTOR - 1)) +
			      (pstats->rx_pwdb_all)) / (RX_SMOOTH_FACTOR);
		}
		rtlpriv->dm.undecorated_smoothed_pwdb =
				 undecorated_smoothed_pwdb;
		_rtl92de_update_rxsignalstatistics(hw, pstats);
	}
}

static void rtl92d_loop_over_streams(struct ieee80211_hw *hw,
				     struct rtl_stats *pstats)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int stream;

	for (stream = 0; stream < 2; stream++) {
		if (pstats->rx_mimo_signalquality[stream] != -1) {
			if (rtlpriv->stats.rx_evm_percentage[stream] == 0) {
				rtlpriv->stats.rx_evm_percentage[stream] =
				    pstats->rx_mimo_signalquality[stream];
			}
			rtlpriv->stats.rx_evm_percentage[stream] =
			    ((rtlpriv->stats.rx_evm_percentage[stream]
			      * (RX_SMOOTH_FACTOR - 1)) +
			     (pstats->rx_mimo_signalquality[stream] * 1)) /
			    (RX_SMOOTH_FACTOR);
		}
	}
}

static void _rtl92de_process_ui_link_quality(struct ieee80211_hw *hw,
					     struct rtl_stats *pstats)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 last_evm, tmpval;

	if (pstats->signalquality == 0)
		return;
	if (pstats->packet_toself || pstats->packet_beacon) {
		if (rtlpriv->stats.ui_link_quality.total_num++ >=
		    PHY_LINKQUALITY_SLID_WIN_MAX) {
			rtlpriv->stats.ui_link_quality.total_num =
			    PHY_LINKQUALITY_SLID_WIN_MAX;
			last_evm = rtlpriv->stats.ui_link_quality.elements[
				rtlpriv->stats.ui_link_quality.index];
			rtlpriv->stats.ui_link_quality.total_val -= last_evm;
		}
		rtlpriv->stats.ui_link_quality.total_val +=
						 pstats->signalquality;
		rtlpriv->stats.ui_link_quality.elements[
			rtlpriv->stats.ui_link_quality.index++] =
						 pstats->signalquality;
		if (rtlpriv->stats.ui_link_quality.index >=
		    PHY_LINKQUALITY_SLID_WIN_MAX)
			rtlpriv->stats.ui_link_quality.index = 0;
		tmpval = rtlpriv->stats.ui_link_quality.total_val /
		    rtlpriv->stats.ui_link_quality.total_num;
		rtlpriv->stats.signal_quality = tmpval;
		rtlpriv->stats.last_sigstrength_inpercent = tmpval;
		rtl92d_loop_over_streams(hw, pstats);
	}
}

static void _rtl92de_process_phyinfo(struct ieee80211_hw *hw,
				     u8 *buffer,
				     struct rtl_stats *pcurrent_stats)
{

	if (!pcurrent_stats->packet_matchbssid &&
	    !pcurrent_stats->packet_beacon)
		return;

	_rtl92de_process_ui_rssi(hw, pcurrent_stats);
	_rtl92de_process_pwdb(hw, pcurrent_stats);
	_rtl92de_process_ui_link_quality(hw, pcurrent_stats);
}

static void _rtl92de_translate_rx_signal_stuff(struct ieee80211_hw *hw,
					       struct sk_buff *skb,
					       struct rtl_stats *pstats,
					       struct rx_desc_92d *pdesc,
					       struct rx_fwinfo_92d *p_drvinfo)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct ieee80211_hdr *hdr;
	u8 *tmp_buf;
	u8 *praddr;
	u16 type, cfc;
	__le16 fc;
	bool packet_matchbssid, packet_toself, packet_beacon;

	tmp_buf = skb->data + pstats->rx_drvinfo_size + pstats->rx_bufshift;
	hdr = (struct ieee80211_hdr *)tmp_buf;
	fc = hdr->frame_control;
	cfc = le16_to_cpu(fc);
	type = WLAN_FC_GET_TYPE(fc);
	praddr = hdr->addr1;
	packet_matchbssid = ((IEEE80211_FTYPE_CTL != type) &&
	     (!compare_ether_addr(mac->bssid, (cfc & IEEE80211_FCTL_TODS) ?
		  hdr->addr1 : (cfc & IEEE80211_FCTL_FROMDS) ?
		  hdr->addr2 : hdr->addr3)) && (!pstats->hwerror) &&
		  (!pstats->crc) && (!pstats->icv));
	packet_toself = packet_matchbssid &&
			(!compare_ether_addr(praddr, rtlefuse->dev_addr));
	if (ieee80211_is_beacon(fc))
		packet_beacon = true;
	_rtl92de_query_rxphystatus(hw, pstats, pdesc, p_drvinfo,
				   packet_matchbssid, packet_toself,
				   packet_beacon);
	_rtl92de_process_phyinfo(hw, tmp_buf, pstats);
}

bool rtl92de_rx_query_desc(struct ieee80211_hw *hw,	struct rtl_stats *stats,
		struct ieee80211_rx_status *rx_status,
		u8 *p_desc, struct sk_buff *skb)
{
	struct rx_fwinfo_92d *p_drvinfo;
	struct rx_desc_92d *pdesc = (struct rx_desc_92d *)p_desc;
	u32 phystatus = GET_RX_DESC_PHYST(pdesc);

	stats->length = (u16) GET_RX_DESC_PKT_LEN(pdesc);
	stats->rx_drvinfo_size = (u8) GET_RX_DESC_DRV_INFO_SIZE(pdesc) *
				 RX_DRV_INFO_SIZE_UNIT;
	stats->rx_bufshift = (u8) (GET_RX_DESC_SHIFT(pdesc) & 0x03);
	stats->icv = (u16) GET_RX_DESC_ICV(pdesc);
	stats->crc = (u16) GET_RX_DESC_CRC32(pdesc);
	stats->hwerror = (stats->crc | stats->icv);
	stats->decrypted = !GET_RX_DESC_SWDEC(pdesc);
	stats->rate = (u8) GET_RX_DESC_RXMCS(pdesc);
	stats->shortpreamble = (u16) GET_RX_DESC_SPLCP(pdesc);
	stats->isampdu = (bool) (GET_RX_DESC_PAGGR(pdesc) == 1);
	stats->isfirst_ampdu = (bool) ((GET_RX_DESC_PAGGR(pdesc) == 1)
					 && (GET_RX_DESC_FAGGR(pdesc) == 1));
	stats->timestamp_low = GET_RX_DESC_TSFL(pdesc);
	stats->rx_is40Mhzpacket = (bool) GET_RX_DESC_BW(pdesc);
	rx_status->freq = hw->conf.channel->center_freq;
	rx_status->band = hw->conf.channel->band;
	if (GET_RX_DESC_CRC32(pdesc))
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;
	if (!GET_RX_DESC_SWDEC(pdesc))
		rx_status->flag |= RX_FLAG_DECRYPTED;
	if (GET_RX_DESC_BW(pdesc))
		rx_status->flag |= RX_FLAG_40MHZ;
	if (GET_RX_DESC_RXHT(pdesc))
		rx_status->flag |= RX_FLAG_HT;
	rx_status->flag |= RX_FLAG_MACTIME_MPDU;
	if (stats->decrypted)
		rx_status->flag |= RX_FLAG_DECRYPTED;
	rx_status->rate_idx = rtlwifi_rate_mapping(hw,
					(bool)GET_RX_DESC_RXHT(pdesc),
					(u8)GET_RX_DESC_RXMCS(pdesc),
					(bool)GET_RX_DESC_PAGGR(pdesc));
	rx_status->mactime = GET_RX_DESC_TSFL(pdesc);
	if (phystatus) {
		p_drvinfo = (struct rx_fwinfo_92d *)(skb->data +
						     stats->rx_bufshift);
		_rtl92de_translate_rx_signal_stuff(hw,
						   skb, stats, pdesc,
						   p_drvinfo);
	}
	/*rx_status->qual = stats->signal; */
	rx_status->signal = stats->recvsignalpower + 10;
	/*rx_status->noise = -stats->noise; */
	return true;
}

static void _rtl92de_insert_emcontent(struct rtl_tcb_desc *ptcb_desc,
				      u8 *virtualaddress)
{
	memset(virtualaddress, 0, 8);

	SET_EARLYMODE_PKTNUM(virtualaddress, ptcb_desc->empkt_num);
	SET_EARLYMODE_LEN0(virtualaddress, ptcb_desc->empkt_len[0]);
	SET_EARLYMODE_LEN1(virtualaddress, ptcb_desc->empkt_len[1]);
	SET_EARLYMODE_LEN2_1(virtualaddress, ptcb_desc->empkt_len[2] & 0xF);
	SET_EARLYMODE_LEN2_2(virtualaddress, ptcb_desc->empkt_len[2] >> 4);
	SET_EARLYMODE_LEN3(virtualaddress, ptcb_desc->empkt_len[3]);
	SET_EARLYMODE_LEN4(virtualaddress, ptcb_desc->empkt_len[4]);
}

void rtl92de_tx_fill_desc(struct ieee80211_hw *hw,
			  struct ieee80211_hdr *hdr, u8 *pdesc_tx,
			  struct ieee80211_tx_info *info, struct sk_buff *skb,
			  u8 hw_queue, struct rtl_tcb_desc *ptcb_desc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct ieee80211_sta *sta = info->control.sta;
	u8 *pdesc = (u8 *) pdesc_tx;
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
			bw_40 = sta->ht_cap.cap &
				IEEE80211_HT_CAP_SUP_WIDTH_20_40;
	}
	seq_number = (le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ) >> 4;
	rtl_get_tcb_desc(hw, info, sta, skb, ptcb_desc);
	/* reserve 8 byte for AMPDU early mode */
	if (rtlhal->earlymode_enable) {
		skb_push(skb, EM_HDR_LEN);
		memset(skb->data, 0, EM_HDR_LEN);
	}
	buf_len = skb->len;
	mapping = pci_map_single(rtlpci->pdev, skb->data, skb->len,
				 PCI_DMA_TODEVICE);
	CLEAR_PCI_TX_DESC_CONTENT(pdesc, sizeof(struct tx_desc_92d));
	if (ieee80211_is_nullfunc(fc) || ieee80211_is_ctl(fc)) {
		firstseg = true;
		lastseg = true;
	}
	if (firstseg) {
		if (rtlhal->earlymode_enable) {
			SET_TX_DESC_PKT_OFFSET(pdesc, 1);
			SET_TX_DESC_OFFSET(pdesc, USB_HWDESC_HEADER_LEN +
					   EM_HDR_LEN);
			if (ptcb_desc->empkt_num) {
				RT_TRACE(rtlpriv, COMP_SEND, DBG_LOUD,
					 "Insert 8 byte.pTcb->EMPktNum:%d\n",
					 ptcb_desc->empkt_num);
				_rtl92de_insert_emcontent(ptcb_desc,
							  (u8 *)(skb->data));
			}
		} else {
			SET_TX_DESC_OFFSET(pdesc, USB_HWDESC_HEADER_LEN);
		}
		/* 5G have no CCK rate */
		if (rtlhal->current_bandtype == BAND_ON_5G)
			if (ptcb_desc->hw_rate < DESC92_RATE6M)
				ptcb_desc->hw_rate = DESC92_RATE6M;
		SET_TX_DESC_TX_RATE(pdesc, ptcb_desc->hw_rate);
		if (ptcb_desc->use_shortgi || ptcb_desc->use_shortpreamble)
			SET_TX_DESC_DATA_SHORTGI(pdesc, 1);

		if (rtlhal->macphymode == DUALMAC_DUALPHY &&
			ptcb_desc->hw_rate == DESC92_RATEMCS7)
			SET_TX_DESC_DATA_SHORTGI(pdesc, 1);

		if (info->flags & IEEE80211_TX_CTL_AMPDU) {
			SET_TX_DESC_AGG_ENABLE(pdesc, 1);
			SET_TX_DESC_MAX_AGG_NUM(pdesc, 0x14);
		}
		SET_TX_DESC_SEQ(pdesc, seq_number);
		SET_TX_DESC_RTS_ENABLE(pdesc, ((ptcb_desc->rts_enable &&
				       !ptcb_desc->cts_enable) ? 1 : 0));
		SET_TX_DESC_HW_RTS_ENABLE(pdesc, ((ptcb_desc->rts_enable
					  || ptcb_desc->cts_enable) ? 1 : 0));
		SET_TX_DESC_CTS2SELF(pdesc, ((ptcb_desc->cts_enable) ? 1 : 0));
		SET_TX_DESC_RTS_STBC(pdesc, ((ptcb_desc->rts_stbc) ? 1 : 0));
		/* 5G have no CCK rate */
		if (rtlhal->current_bandtype == BAND_ON_5G)
			if (ptcb_desc->rts_rate < DESC92_RATE6M)
				ptcb_desc->rts_rate = DESC92_RATE6M;
		SET_TX_DESC_RTS_RATE(pdesc, ptcb_desc->rts_rate);
		SET_TX_DESC_RTS_BW(pdesc, 0);
		SET_TX_DESC_RTS_SC(pdesc, ptcb_desc->rts_sc);
		SET_TX_DESC_RTS_SHORT(pdesc, ((ptcb_desc->rts_rate <=
			DESC92_RATE54M) ?
			(ptcb_desc->rts_use_shortpreamble ? 1 : 0) :
			(ptcb_desc->rts_use_shortgi ? 1 : 0)));
		if (bw_40) {
			if (ptcb_desc->packet_bw) {
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
		SET_TX_DESC_PKT_SIZE(pdesc, (u16) skb_len);
		if (sta) {
			u8 ampdu_density = sta->ht_cap.ampdu_density;
			SET_TX_DESC_AMPDU_DENSITY(pdesc, ampdu_density);
		}
		if (info->control.hw_key) {
			struct ieee80211_key_conf *keyconf;

			keyconf = info->control.hw_key;
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
		SET_TX_DESC_DISABLE_FB(pdesc, ptcb_desc->disable_ratefallback ?
				       1 : 0);
		SET_TX_DESC_USE_RATE(pdesc, ptcb_desc->use_driver_rate ? 1 : 0);

		/* Set TxRate and RTSRate in TxDesc  */
		/* This prevent Tx initial rate of new-coming packets */
		/* from being overwritten by retried  packet rate.*/
		if (!ptcb_desc->use_driver_rate) {
			SET_TX_DESC_RTS_RATE(pdesc, 0x08);
			/* SET_TX_DESC_TX_RATE(pdesc, 0x0b); */
		}
		if (ieee80211_is_data_qos(fc)) {
			if (mac->rdg_en) {
				RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE,
					 "Enable RDG function\n");
				SET_TX_DESC_RDG_ENABLE(pdesc, 1);
				SET_TX_DESC_HTC(pdesc, 1);
			}
		}
	}

	SET_TX_DESC_FIRST_SEG(pdesc, (firstseg ? 1 : 0));
	SET_TX_DESC_LAST_SEG(pdesc, (lastseg ? 1 : 0));
	SET_TX_DESC_TX_BUFFER_SIZE(pdesc, (u16) buf_len);
	SET_TX_DESC_TX_BUFFER_ADDRESS(pdesc, mapping);
	if (rtlpriv->dm.useramask) {
		SET_TX_DESC_RATE_ID(pdesc, ptcb_desc->ratr_index);
		SET_TX_DESC_MACID(pdesc, ptcb_desc->mac_id);
	} else {
		SET_TX_DESC_RATE_ID(pdesc, 0xC + ptcb_desc->ratr_index);
		SET_TX_DESC_MACID(pdesc, ptcb_desc->ratr_index);
	}
	if (ieee80211_is_data_qos(fc))
		SET_TX_DESC_QOS(pdesc, 1);

	if ((!ieee80211_is_data_qos(fc)) && ppsc->fwctrl_lps) {
		SET_TX_DESC_HWSEQ_EN(pdesc, 1);
		SET_TX_DESC_PKT_ID(pdesc, 8);
	}
	SET_TX_DESC_MORE_FRAG(pdesc, (lastseg ? 0 : 1));
	RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE, "\n");
}

void rtl92de_tx_fill_cmddesc(struct ieee80211_hw *hw,
			     u8 *pdesc, bool firstseg,
			     bool lastseg, struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtlpriv);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u8 fw_queue = QSLT_BEACON;
	dma_addr_t mapping = pci_map_single(rtlpci->pdev,
		    skb->data, skb->len, PCI_DMA_TODEVICE);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)(skb->data);
	__le16 fc = hdr->frame_control;

	CLEAR_PCI_TX_DESC_CONTENT(pdesc, TX_DESC_SIZE);
	if (firstseg)
		SET_TX_DESC_OFFSET(pdesc, USB_HWDESC_HEADER_LEN);
	/* 5G have no CCK rate
	 * Caution: The macros below are multi-line expansions.
	 * The braces are needed no matter what checkpatch says
	 */
	if (rtlhal->current_bandtype == BAND_ON_5G) {
		SET_TX_DESC_TX_RATE(pdesc, DESC92_RATE6M);
	} else {
		SET_TX_DESC_TX_RATE(pdesc, DESC92_RATE1M);
	}
	SET_TX_DESC_SEQ(pdesc, 0);
	SET_TX_DESC_LINIP(pdesc, 0);
	SET_TX_DESC_QUEUE_SEL(pdesc, fw_queue);
	SET_TX_DESC_FIRST_SEG(pdesc, 1);
	SET_TX_DESC_LAST_SEG(pdesc, 1);
	SET_TX_DESC_TX_BUFFER_SIZE(pdesc, (u16) (skb->len));
	SET_TX_DESC_TX_BUFFER_ADDRESS(pdesc, mapping);
	SET_TX_DESC_RATE_ID(pdesc, 7);
	SET_TX_DESC_MACID(pdesc, 0);
	SET_TX_DESC_PKT_SIZE((u8 *) pdesc, (u16) (skb->len));
	SET_TX_DESC_FIRST_SEG(pdesc, 1);
	SET_TX_DESC_LAST_SEG(pdesc, 1);
	SET_TX_DESC_OFFSET(pdesc, 0x20);
	SET_TX_DESC_USE_RATE(pdesc, 1);

	if (!ieee80211_is_data_qos(fc) && ppsc->fwctrl_lps) {
		SET_TX_DESC_HWSEQ_EN(pdesc, 1);
		SET_TX_DESC_PKT_ID(pdesc, 8);
	}

	RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_LOUD,
		      "H2C Tx Cmd Content", pdesc, TX_DESC_SIZE);
	wmb();
	SET_TX_DESC_OWN(pdesc, 1);
}

void rtl92de_set_desc(u8 *pdesc, bool istx, u8 desc_name, u8 *val)
{
	if (istx) {
		switch (desc_name) {
		case HW_DESC_OWN:
			wmb();
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
			wmb();
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

u32 rtl92de_get_desc(u8 *p_desc, bool istx, u8 desc_name)
{
	u32 ret = 0;

	if (istx) {
		switch (desc_name) {
		case HW_DESC_OWN:
			ret = GET_TX_DESC_OWN(p_desc);
			break;
		case HW_DESC_TXBUFF_ADDR:
			ret = GET_TX_DESC_TX_BUFFER_ADDRESS(p_desc);
			break;
		default:
			RT_ASSERT(false, "ERR txdesc :%d not process\n",
				  desc_name);
			break;
		}
	} else {
		struct rx_desc_92c *pdesc = (struct rx_desc_92c *)p_desc;
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

void rtl92de_tx_polling(struct ieee80211_hw *hw, u8 hw_queue)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	if (hw_queue == BEACON_QUEUE)
		rtl_write_word(rtlpriv, REG_PCIE_CTRL_REG, BIT(4));
	else
		rtl_write_word(rtlpriv, REG_PCIE_CTRL_REG,
			       BIT(0) << (hw_queue));
}
