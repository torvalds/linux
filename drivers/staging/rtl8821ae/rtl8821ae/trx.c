/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
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
#include "dm.h"

static u8 _rtl8821ae_map_hwqueue_to_fwqueue(struct sk_buff *skb, u8 hw_queue)
{
	u16 fc = rtl_get_fc(skb);

	if (unlikely(ieee80211_is_beacon(fc)))
		return QSLT_BEACON;
	if (ieee80211_is_mgmt(fc) || ieee80211_is_ctl(fc))
		return QSLT_MGNT;

	return skb->priority;
}

/* mac80211's rate_idx is like this:
 *
 * 2.4G band:rx_status->band == IEEE80211_BAND_2GHZ
 *
 * B/G rate:
 * (rx_status->flag & RX_FLAG_HT) = 0,
 * DESC_RATE1M-->DESC_RATE54M ==> idx is 0-->11,
 *
 * N rate:
 * (rx_status->flag & RX_FLAG_HT) = 1,
 * DESC_RATEMCS0-->DESC_RATEMCS15 ==> idx is 0-->15
 *
 * 5G band:rx_status->band == IEEE80211_BAND_5GHZ
 * A rate:
 * (rx_status->flag & RX_FLAG_HT) = 0,
 * DESC_RATE6M-->DESC_RATE54M ==> idx is 0-->7,
 *
 * N rate:
 * (rx_status->flag & RX_FLAG_HT) = 1,
 * DESC_RATEMCS0-->DESC_RATEMCS15 ==> idx is 0-->15
 */
static int _rtl8821ae_rate_mapping(struct ieee80211_hw *hw,
	bool isht, u8 desc_rate)
{
	int rate_idx;

	if (false == isht) {
		if (IEEE80211_BAND_2GHZ == hw->conf.chandef.chan->band) {
			switch (desc_rate) {
			case DESC_RATE1M:
				rate_idx = 0;
				break;
			case DESC_RATE2M:
				rate_idx = 1;
				break;
			case DESC_RATE5_5M:
				rate_idx = 2;
				break;
			case DESC_RATE11M:
				rate_idx = 3;
				break;
			case DESC_RATE6M:
				rate_idx = 4;
				break;
			case DESC_RATE9M:
				rate_idx = 5;
				break;
			case DESC_RATE12M:
				rate_idx = 6;
				break;
			case DESC_RATE18M:
				rate_idx = 7;
				break;
			case DESC_RATE24M:
				rate_idx = 8;
				break;
			case DESC_RATE36M:
				rate_idx = 9;
				break;
			case DESC_RATE48M:
				rate_idx = 10;
				break;
			case DESC_RATE54M:
				rate_idx = 11;
				break;
			default:
				rate_idx = 0;
				break;
			}
		} else {
			switch (desc_rate) {
			case DESC_RATE6M:
				rate_idx = 0;
				break;
			case DESC_RATE9M:
				rate_idx = 1;
				break;
			case DESC_RATE12M:
				rate_idx = 2;
				break;
			case DESC_RATE18M:
				rate_idx = 3;
				break;
			case DESC_RATE24M:
				rate_idx = 4;
				break;
			case DESC_RATE36M:
				rate_idx = 5;
				break;
			case DESC_RATE48M:
				rate_idx = 6;
				break;
			case DESC_RATE54M:
				rate_idx = 7;
				break;
			default:
				rate_idx = 0;
				break;
			}
		}
	} else {
		switch(desc_rate) {
		case DESC_RATEMCS0:
			rate_idx = 0;
			break;
		case DESC_RATEMCS1:
			rate_idx = 1;
			break;
		case DESC_RATEMCS2:
			rate_idx = 2;
			break;
		case DESC_RATEMCS3:
			rate_idx = 3;
			break;
		case DESC_RATEMCS4:
			rate_idx = 4;
			break;
		case DESC_RATEMCS5:
			rate_idx = 5;
			break;
		case DESC_RATEMCS6:
			rate_idx = 6;
			break;
		case DESC_RATEMCS7:
			rate_idx = 7;
			break;
		case DESC_RATEMCS8:
			rate_idx = 8;
			break;
		case DESC_RATEMCS9:
			rate_idx = 9;
			break;
		case DESC_RATEMCS10:
			rate_idx = 10;
			break;
		case DESC_RATEMCS11:
			rate_idx = 11;
			break;
		case DESC_RATEMCS12:
			rate_idx = 12;
			break;
		case DESC_RATEMCS13:
			rate_idx = 13;
			break;
		case DESC_RATEMCS14:
			rate_idx = 14;
			break;
		case DESC_RATEMCS15:
			rate_idx = 15;
			break;
		default:
			rate_idx = 0;
			break;
		}
	}
	return rate_idx;
}

static void _rtl8821ae_query_rxphystatus(struct ieee80211_hw *hw,
			struct rtl_stats *pstatus, u8 *pdesc,
			struct rx_fwinfo_8821ae *p_drvinfo, bool bpacket_match_bssid,
			bool bpacket_toself, bool b_packet_beacon)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtlpriv);
	struct phy_sts_cck_8821ae_t *cck_buf;
	struct phy_status_rpt *p_phystRpt = (struct phy_status_rpt *)p_drvinfo;
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	char rx_pwr_all = 0, rx_pwr[4];
	u8 rf_rx_num = 0, evm, pwdb_all;
	u8 i, max_spatial_stream;
	u32 rssi, total_rssi = 0;
	bool b_is_cck = pstatus->b_is_cck;
	u8 lan_idx,vga_idx;

	/* Record it for next packet processing */
	pstatus->b_packet_matchbssid = bpacket_match_bssid;
	pstatus->b_packet_toself = bpacket_toself;
	pstatus->b_packet_beacon = b_packet_beacon;
	pstatus->rx_mimo_signalquality[0] = -1;
	pstatus->rx_mimo_signalquality[1] = -1;

	if (b_is_cck) {
		u8 cck_highpwr;
		u8 cck_agc_rpt;
		/* CCK Driver info Structure is not the same as OFDM packet. */
		cck_buf = (struct phy_sts_cck_8821ae_t *)p_drvinfo;
		cck_agc_rpt = cck_buf->cck_agc_rpt;

		/* (1)Hardware does not provide RSSI for CCK */
		/* (2)PWDB, Average PWDB calculated by
		 * hardware (for rate adaptive) */
		if (ppsc->rfpwr_state == ERFON)
			cck_highpwr = (u8) rtl_get_bbreg(hw, RFPGA0_XA_HSSIPARAMETER2,
							 BIT(9));
		else
			cck_highpwr = false;

		lan_idx = ((cck_agc_rpt & 0xE0) >> 5);
		vga_idx = (cck_agc_rpt & 0x1f);
		switch (lan_idx) {
			case 7:
				if(vga_idx <= 27)
					rx_pwr_all = -100 + 2*(27-vga_idx); /*VGA_idx = 27~2*/
				else
					rx_pwr_all = -100;
				break;
			case 6:
				rx_pwr_all = -48 + 2*(2-vga_idx); /*VGA_idx = 2~0*/
				break;
			case 5:
				rx_pwr_all = -42 + 2*(7-vga_idx); /*VGA_idx = 7~5*/
				break;
			case 4:
				rx_pwr_all = -36 + 2*(7-vga_idx); /*VGA_idx = 7~4*/
				break;
			case 3:
				rx_pwr_all = -24 + 2*(7-vga_idx); /*VGA_idx = 7~0*/
				break;
			case 2:
				if(cck_highpwr)
					rx_pwr_all = -12 + 2*(5-vga_idx); /*VGA_idx = 5~0*/
				else
					rx_pwr_all = -6+ 2*(5-vga_idx);
				break;
			case 1:
				rx_pwr_all = 8-2*vga_idx;
				break;
			case 0:
				rx_pwr_all = 14-2*vga_idx;
				break;
			default:
				break;
		}
		rx_pwr_all += 6;
		pwdb_all = rtl_query_rxpwrpercentage(rx_pwr_all);
		/* CCK gain is smaller than OFDM/MCS gain,  */
		/* so we add gain diff by experiences,
		 * the val is 6 */
		pwdb_all += 6;
		if(pwdb_all > 100)
			pwdb_all = 100;
		/* modify the offset to make the same
		 * gain index with OFDM. */
		if(pwdb_all > 34 && pwdb_all <= 42)
			pwdb_all -= 2;
		else if(pwdb_all > 26 && pwdb_all <= 34)
			pwdb_all -= 6;
		else if(pwdb_all > 14 && pwdb_all <= 26)
			pwdb_all -= 8;
		else if(pwdb_all > 4 && pwdb_all <= 14)
			pwdb_all -= 4;
		if (cck_highpwr == false){
			if (pwdb_all >= 80)
				pwdb_all =((pwdb_all-80)<<1)+((pwdb_all-80)>>1)+80;
			else if((pwdb_all <= 78) && (pwdb_all >= 20))
				pwdb_all += 3;
			if(pwdb_all>100)
				pwdb_all = 100;
		}

		pstatus->rx_pwdb_all = pwdb_all;
		pstatus->recvsignalpower = rx_pwr_all;

		/* (3) Get Signal Quality (EVM) */
		if (bpacket_match_bssid) {
			u8 sq;

			if (pstatus->rx_pwdb_all > 40)
				sq = 100;
			else {
				sq = cck_buf->sq_rpt;
				if (sq > 64)
					sq = 0;
				else if (sq < 20)
					sq = 100;
				else
					sq = ((64 - sq) * 100) / 44;
			}

			pstatus->signalquality = sq;
			pstatus->rx_mimo_signalquality[0] = sq;
			pstatus->rx_mimo_signalquality[1] = -1;
		}
	} else {
		rtlpriv->dm.brfpath_rxenable[0] =
		    rtlpriv->dm.brfpath_rxenable[1] = true;

		/* (1)Get RSSI for HT rate */
		for (i = RF90_PATH_A; i < RF6052_MAX_PATH; i++) {

			/* we will judge RF RX path now. */
			if (rtlpriv->dm.brfpath_rxenable[i])
				rf_rx_num++;

			rx_pwr[i] = ((p_drvinfo->gain_trsw[i] & 0x3f) * 2) - 110;

			/* Translate DBM to percentage. */
			rssi = rtl_query_rxpwrpercentage(rx_pwr[i]);
			total_rssi += rssi;

			/* Get Rx snr value in DB */
			rtlpriv->stats.rx_snr_db[i] = (long)(p_drvinfo->rxsnr[i] / 2);

			/* Record Signal Strength for next packet */
			if (bpacket_match_bssid)
				pstatus->rx_mimo_signalstrength[i] = (u8) rssi;
		}

		/* (2)PWDB, Average PWDB calculated by
		 * hardware (for rate adaptive) */
		rx_pwr_all = ((p_drvinfo->pwdb_all >> 1) & 0x7f) - 110;

		pwdb_all = rtl_query_rxpwrpercentage(rx_pwr_all);
		pstatus->rx_pwdb_all = pwdb_all;
		pstatus->rxpower = rx_pwr_all;
		pstatus->recvsignalpower = rx_pwr_all;

		/* (3)EVM of HT rate */
		if (pstatus->b_is_ht && pstatus->rate >= DESC_RATEMCS8 &&
		    pstatus->rate <= DESC_RATEMCS15)
			max_spatial_stream = 2;
		else
			max_spatial_stream = 1;

		for (i = 0; i < max_spatial_stream; i++) {
			evm = rtl_evm_db_to_percentage(p_drvinfo->rxevm[i]);

			if (bpacket_match_bssid) {
				/* Fill value in RFD, Get the first
				 * spatial stream only */
				if (i == 0)
					pstatus->signalquality = (u8) (evm & 0xff);
				pstatus->rx_mimo_signalquality[i] = (u8) (evm & 0xff);
			}
		}
	}

	/* UI BSS List signal strength(in percentage),
	 * make it good looking, from 0~100. */
	if (b_is_cck)
		pstatus->signalstrength = (u8)(rtl_signal_scale_mapping(hw,
			pwdb_all));
	else if (rf_rx_num != 0)
		pstatus->signalstrength = (u8)(rtl_signal_scale_mapping(hw,
			total_rssi /= rf_rx_num));
	/*HW antenna diversity*/
	rtldm->fat_table.antsel_rx_keep_0 = p_phystRpt->ant_sel;
	rtldm->fat_table.antsel_rx_keep_1 = p_phystRpt->ant_sel_b;
	rtldm->fat_table.antsel_rx_keep_2 = p_phystRpt->antsel_rx_keep_2;

}
#if 0
static void _rtl8821ae_smart_antenna(struct ieee80211_hw *hw,
	struct rtl_stats *pstatus)
{
	struct rtl_dm *rtldm= rtl_dm(rtl_priv(hw));
	struct rtl_efuse *rtlefuse =rtl_efuse(rtl_priv(hw));
	u8 antsel_tr_mux;
	struct fast_ant_trainning *pfat_table = &(rtldm->fat_table);

	if (rtlefuse->antenna_div_type == CG_TRX_SMART_ANTDIV) {
		if (pfat_table->fat_state == FAT_TRAINING_STATE) {
			if (pstatus->b_packet_toself) {
				antsel_tr_mux = (pfat_table->antsel_rx_keep_2 << 2) |
					(pfat_table->antsel_rx_keep_1 << 1) | pfat_table->antsel_rx_keep_0;
				pfat_table->ant_sum_rssi[antsel_tr_mux] += pstatus->rx_pwdb_all;
				pfat_table->ant_rssi_cnt[antsel_tr_mux]++;
			}
		}
	} else if ((rtlefuse->antenna_div_type == CG_TRX_HW_ANTDIV) ||
	(rtlefuse->antenna_div_type == CGCS_RX_HW_ANTDIV)) {
		if (pstatus->b_packet_toself || pstatus->b_packet_matchbssid) {
			antsel_tr_mux = (pfat_table->antsel_rx_keep_2 << 2) |
					(pfat_table->antsel_rx_keep_1 << 1) | pfat_table->antsel_rx_keep_0;
			rtl8821ae_dm_ant_sel_statistics(hw, antsel_tr_mux, 0, pstatus->rx_pwdb_all);
		}

	}
}
#endif
static void _rtl8821ae_translate_rx_signal_stuff(struct ieee80211_hw *hw,
		struct sk_buff *skb, struct rtl_stats *pstatus,
		u8 *pdesc, struct rx_fwinfo_8821ae *p_drvinfo)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct ieee80211_hdr *hdr;
	u8 *tmp_buf;
	u8 *praddr;
	u8 *psaddr;
	u16 fc, type;
	bool b_packet_matchbssid, b_packet_toself, b_packet_beacon;

	tmp_buf = skb->data + pstatus->rx_drvinfo_size + pstatus->rx_bufshift;

	hdr = (struct ieee80211_hdr *)tmp_buf;
	fc = le16_to_cpu(hdr->frame_control);
	type = WLAN_FC_GET_TYPE(fc);
	praddr = hdr->addr1;
	psaddr = ieee80211_get_SA(hdr);
	memcpy(pstatus->psaddr, psaddr, ETH_ALEN);

	b_packet_matchbssid = ((IEEE80211_FTYPE_CTL != type) &&
	     (!ether_addr_equal(mac->bssid, (fc & IEEE80211_FCTL_TODS) ?
				  hdr->addr1 : (fc & IEEE80211_FCTL_FROMDS) ?
				  hdr->addr2 : hdr->addr3)) && (!pstatus->b_hwerror) &&
				  (!pstatus->b_crc) && (!pstatus->b_icv));

	b_packet_toself = b_packet_matchbssid &&
	    (!ether_addr_equal(praddr, rtlefuse->dev_addr));

	if (ieee80211_is_beacon(fc))
		b_packet_beacon = true;
	else
		b_packet_beacon = false;

	if (b_packet_beacon && b_packet_matchbssid)
		rtl_priv(hw)->dm.dbginfo.num_qry_beacon_pkt++;

	_rtl8821ae_query_rxphystatus(hw, pstatus, pdesc, p_drvinfo,
				   b_packet_matchbssid, b_packet_toself,
				   b_packet_beacon);
	/*_rtl8821ae_smart_antenna(hw, pstatus); */
	rtl_process_phyinfo(hw, tmp_buf, pstatus);
}

static void _rtl8821ae_insert_emcontent(struct rtl_tcb_desc *ptcb_desc,
				      u8 *virtualaddress)
{
	u32 dwtmp = 0;
	memset(virtualaddress, 0, 8);

	SET_EARLYMODE_PKTNUM(virtualaddress, ptcb_desc->empkt_num);
	if (ptcb_desc->empkt_num == 1)
		dwtmp = ptcb_desc->empkt_len[0];
	else {
		dwtmp = ptcb_desc->empkt_len[0];
		dwtmp += ((dwtmp%4)?(4-dwtmp%4):0)+4;
		dwtmp += ptcb_desc->empkt_len[1];
	}
	SET_EARLYMODE_LEN0(virtualaddress, dwtmp);

	if (ptcb_desc->empkt_num <= 3)
		dwtmp = ptcb_desc->empkt_len[2];
	else {
		dwtmp = ptcb_desc->empkt_len[2];
		dwtmp += ((dwtmp%4)?(4-dwtmp%4):0)+4;
		dwtmp += ptcb_desc->empkt_len[3];
	}
	SET_EARLYMODE_LEN1(virtualaddress, dwtmp);
	if (ptcb_desc->empkt_num <= 5)
		dwtmp = ptcb_desc->empkt_len[4];
	else {
		dwtmp = ptcb_desc->empkt_len[4];
		dwtmp += ((dwtmp%4)?(4-dwtmp%4):0)+4;
		dwtmp += ptcb_desc->empkt_len[5];
	}
	SET_EARLYMODE_LEN2_1(virtualaddress, dwtmp & 0xF);
	SET_EARLYMODE_LEN2_2(virtualaddress, dwtmp >> 4);
	if (ptcb_desc->empkt_num <= 7)
		dwtmp = ptcb_desc->empkt_len[6];
	else {
		dwtmp = ptcb_desc->empkt_len[6];
		dwtmp += ((dwtmp%4)?(4-dwtmp%4):0)+4;
		dwtmp += ptcb_desc->empkt_len[7];
	}
	SET_EARLYMODE_LEN3(virtualaddress, dwtmp);
	if (ptcb_desc->empkt_num <= 9)
		dwtmp = ptcb_desc->empkt_len[8];
	else {
		dwtmp = ptcb_desc->empkt_len[8];
		dwtmp += ((dwtmp%4)?(4-dwtmp%4):0)+4;
		dwtmp += ptcb_desc->empkt_len[9];
	}
	SET_EARLYMODE_LEN4(virtualaddress, dwtmp);
}

bool rtl8821ae_rx_query_desc(struct ieee80211_hw *hw,
			   struct rtl_stats *status,
			   struct ieee80211_rx_status *rx_status,
			   u8 *pdesc, struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rx_fwinfo_8821ae *p_drvinfo;
	struct ieee80211_hdr *hdr;

	u32 phystatus = GET_RX_DESC_PHYST(pdesc);

	status->length = (u16) GET_RX_DESC_PKT_LEN(pdesc);
	status->rx_drvinfo_size = (u8) GET_RX_DESC_DRV_INFO_SIZE(pdesc) *
	    RX_DRV_INFO_SIZE_UNIT;
	status->rx_bufshift = (u8) (GET_RX_DESC_SHIFT(pdesc) & 0x03);
	status->b_icv = (u16) GET_RX_DESC_ICV(pdesc);
	status->b_crc = (u16) GET_RX_DESC_CRC32(pdesc);
	status->b_hwerror = (status->b_crc | status->b_icv);
	status->decrypted = !GET_RX_DESC_SWDEC(pdesc);
	status->rate = (u8) GET_RX_DESC_RXMCS(pdesc);
	status->b_shortpreamble = (u16) GET_RX_DESC_SPLCP(pdesc);
	status->b_isampdu = (bool) (GET_RX_DESC_PAGGR(pdesc) == 1);
	status->b_isfirst_ampdu = (bool) (GET_RX_DESC_PAGGR(pdesc) == 1);
	status->timestamp_low = GET_RX_DESC_TSFL(pdesc);
	status->rx_is40Mhzpacket = (bool) GET_RX_DESC_BW(pdesc);
	status->macid = GET_RX_DESC_MACID(pdesc);
	status->b_is_ht = (bool)GET_RX_DESC_RXHT(pdesc);

	status->b_is_cck = RX_HAL_IS_CCK_RATE(status->rate);

	if (GET_RX_STATUS_DESC_RPT_SEL(pdesc))
		status->packet_report_type = C2H_PACKET;
	else
		status->packet_report_type = NORMAL_RX;

	if (GET_RX_STATUS_DESC_PATTERN_MATCH(pdesc))
		status->wake_match = BIT(2);
	else if (GET_RX_STATUS_DESC_MAGIC_MATCH(pdesc))
		status->wake_match = BIT(1);
	else if (GET_RX_STATUS_DESC_UNICAST_MATCH(pdesc))
		status->wake_match = BIT(0);
	else
		status->wake_match = 0;

	if (status->wake_match)
		RT_TRACE(COMP_RXDESC,DBG_LOUD,
		("GGGGGGGGGGGGGet Wakeup Packet!! WakeMatch=%d\n",status->wake_match ));
	rx_status->freq = hw->conf.chandef.chan->center_freq;
	rx_status->band = hw->conf.chandef.chan->band;

	hdr = (struct ieee80211_hdr *)(skb->data + status->rx_drvinfo_size
			+ status->rx_bufshift);

	if (status->b_crc)
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (status->rx_is40Mhzpacket)
		rx_status->flag |= RX_FLAG_40MHZ;

	if (status->b_is_ht)
		rx_status->flag |= RX_FLAG_HT;

	rx_status->flag |= RX_FLAG_MACTIME_MPDU;

	/* hw will set status->decrypted true, if it finds the
	 * frame is open data frame or mgmt frame. */
	/* So hw will not decryption robust management frame
	 * for IEEE80211w but still set status->decrypted
	 * true, so here we should set it back to undecrypted
	 * for IEEE80211w frame, and mac80211 sw will help
	 * to decrypt it */
	if (status->decrypted) {
		if (!hdr) {
			WARN_ON_ONCE(true);
			pr_err("decrypted is true but hdr NULL, from skb %p\n",
				rtl_get_hdr(skb));
				return false;
		}

		if ((_ieee80211_is_robust_mgmt_frame(hdr)) &&
			(ieee80211_has_protected(hdr->frame_control)))
			rx_status->flag &= ~RX_FLAG_DECRYPTED;
		else
			rx_status->flag |= RX_FLAG_DECRYPTED;
	}

	/* rate_idx: index of data rate into band's
	 * supported rates or MCS index if HT rates
	 * are use (RX_FLAG_HT)*/
	/* Notice: this is diff with windows define */
	rx_status->rate_idx = _rtl8821ae_rate_mapping(hw,
				status->b_is_ht, status->rate);

	rx_status->mactime = status->timestamp_low;
	if (phystatus == true) {
		p_drvinfo = (struct rx_fwinfo_8821ae *)(skb->data +
						     status->rx_bufshift);

		_rtl8821ae_translate_rx_signal_stuff(hw,
						   skb, status, pdesc,
						   p_drvinfo);
	}

	/*rx_status->qual = status->signal; */
	rx_status->signal = status->recvsignalpower + 10;
	/*rx_status->noise = -status->noise; */
	if (status->packet_report_type == TX_REPORT2){
		status->macid_valid_entry[0] = GET_RX_RPT2_DESC_MACID_VALID_1(pdesc);
		status->macid_valid_entry[1] = GET_RX_RPT2_DESC_MACID_VALID_2(pdesc);
	}
	return true;
}

void rtl8821ae_tx_fill_desc(struct ieee80211_hw *hw,
			  struct ieee80211_hdr *hdr, u8 *pdesc_tx, u8 *txbd,
			  struct ieee80211_tx_info *info,
			  struct ieee80211_sta *sta,
			  struct sk_buff *skb,
			  u8 hw_queue, struct rtl_tcb_desc *ptcb_desc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u8 *pdesc = (u8 *) pdesc_tx;
	u16 seq_number;
	u16 fc = le16_to_cpu(hdr->frame_control);
	unsigned int buf_len = 0;
	unsigned int skb_len = skb->len;
	u8 fw_qsel = _rtl8821ae_map_hwqueue_to_fwqueue(skb, hw_queue);
	bool b_firstseg = ((hdr->seq_ctrl &
			    cpu_to_le16(IEEE80211_SCTL_FRAG)) == 0);
	bool b_lastseg = ((hdr->frame_control &
			   cpu_to_le16(IEEE80211_FCTL_MOREFRAGS)) == 0);
	dma_addr_t mapping;
	u8 bw_40 = 0;
	u8 short_gi = 0;

	if (mac->opmode == NL80211_IFTYPE_STATION) {
		bw_40 = mac->bw_40;
	} else if (mac->opmode == NL80211_IFTYPE_AP ||
		mac->opmode == NL80211_IFTYPE_ADHOC) {
		if (sta)
			bw_40 = sta->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40;
	}
	seq_number = (le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ) >> 4;
	rtl_get_tcb_desc(hw, info, sta, skb, ptcb_desc);
	/* reserve 8 byte for AMPDU early mode */
	if (rtlhal->b_earlymode_enable) {
		skb_push(skb, EM_HDR_LEN);
		memset(skb->data, 0, EM_HDR_LEN);
	}
	buf_len = skb->len;
	mapping = pci_map_single(rtlpci->pdev, skb->data, skb->len,
				 PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(rtlpci->pdev, mapping)) {
		RT_TRACE(COMP_SEND, DBG_TRACE,
			 ("DMA mapping error"));
		return;
	}
	CLEAR_PCI_TX_DESC_CONTENT(pdesc, sizeof(struct tx_desc_8821ae));
	if (ieee80211_is_nullfunc(fc) || ieee80211_is_ctl(fc)) {
		b_firstseg = true;
		b_lastseg = true;
	}
	if (b_firstseg) {
		if (rtlhal->b_earlymode_enable) {
			SET_TX_DESC_PKT_OFFSET(pdesc, 1);
			SET_TX_DESC_OFFSET(pdesc, USB_HWDESC_HEADER_LEN + EM_HDR_LEN);
			if (ptcb_desc->empkt_num) {
				RT_TRACE(COMP_SEND, DBG_TRACE,
					 ("Insert 8 byte.pTcb->EMPktNum:%d\n",
					  ptcb_desc->empkt_num));
				_rtl8821ae_insert_emcontent(ptcb_desc, (u8 *)(skb->data));
			}
		} else {
			SET_TX_DESC_OFFSET(pdesc, USB_HWDESC_HEADER_LEN);
		}

		/* ptcb_desc->use_driver_rate = true; */
		SET_TX_DESC_TX_RATE(pdesc, ptcb_desc->hw_rate);
		if (ptcb_desc->hw_rate > DESC_RATEMCS0) {
			short_gi = (ptcb_desc->use_shortgi) ? 1 : 0;
		} else {
			short_gi = (ptcb_desc->use_shortpreamble) ? 1 :0;
		}
		SET_TX_DESC_DATA_SHORTGI(pdesc, short_gi);

		if (info->flags & IEEE80211_TX_CTL_AMPDU) {
			SET_TX_DESC_AGG_ENABLE(pdesc, 1);
			SET_TX_DESC_MAX_AGG_NUM(pdesc, 0x14);
		}
		SET_TX_DESC_SEQ(pdesc, seq_number);
		SET_TX_DESC_RTS_ENABLE(pdesc, ((ptcb_desc->b_rts_enable &&
						!ptcb_desc->b_cts_enable) ? 1 : 0));
		SET_TX_DESC_HW_RTS_ENABLE(pdesc,0);
		SET_TX_DESC_CTS2SELF(pdesc, ((ptcb_desc->b_cts_enable) ? 1 : 0));
	/*	SET_TX_DESC_RTS_STBC(pdesc, ((ptcb_desc->b_rts_stbc) ? 1 : 0));*/

		SET_TX_DESC_RTS_RATE(pdesc, ptcb_desc->rts_rate);
	/*	SET_TX_DESC_RTS_BW(pdesc, 0);*/
		SET_TX_DESC_RTS_SC(pdesc, ptcb_desc->rts_sc);
		SET_TX_DESC_RTS_SHORT(pdesc, ((ptcb_desc->rts_rate <= DESC_RATE54M) ?
			(ptcb_desc->b_rts_use_shortpreamble ? 1 : 0) :
			(ptcb_desc->b_rts_use_shortgi ? 1 : 0)));

		if(ptcb_desc->btx_enable_sw_calc_duration)
			SET_TX_DESC_NAV_USE_HDR(pdesc, 1);

		if (bw_40) {
			if (ptcb_desc->b_packet_bw) {
				SET_TX_DESC_DATA_BW(pdesc, 1);
				SET_TX_DESC_TX_SUB_CARRIER(pdesc, 3);
			} else {
				SET_TX_DESC_DATA_BW(pdesc, 0);
				SET_TX_DESC_TX_SUB_CARRIER(pdesc, mac->cur_40_prime_sc);
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
			struct ieee80211_key_conf *keyconf = info->control.hw_key;
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

		SET_TX_DESC_QUEUE_SEL(pdesc, fw_qsel);
		SET_TX_DESC_DATA_RATE_FB_LIMIT(pdesc, 0x1F);
		SET_TX_DESC_RTS_RATE_FB_LIMIT(pdesc, 0xF);
		SET_TX_DESC_DISABLE_FB(pdesc, ptcb_desc->disable_ratefallback ? 1 : 0);
		SET_TX_DESC_USE_RATE(pdesc, ptcb_desc->use_driver_rate ? 1 : 0);

#if 0
		SET_TX_DESC_USE_RATE(pdesc, 1);
		SET_TX_DESC_TX_RATE(pdesc, 0x04);

		SET_TX_DESC_RETRY_LIMIT_ENABLE(pdesc, 1);
		SET_TX_DESC_DATA_RETRY_LIMIT(pdesc, 0x3f);
#endif

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
				RT_TRACE(COMP_SEND, DBG_TRACE,
					("Enable RDG function.\n"));
				SET_TX_DESC_RDG_ENABLE(pdesc, 1);
				SET_TX_DESC_HTC(pdesc, 1);
			}
		}
	}

	SET_TX_DESC_FIRST_SEG(pdesc, (b_firstseg ? 1 : 0));
	SET_TX_DESC_LAST_SEG(pdesc, (b_lastseg ? 1 : 0));
	SET_TX_DESC_TX_BUFFER_SIZE(pdesc, (u16) buf_len);
	SET_TX_DESC_TX_BUFFER_ADDRESS(pdesc, cpu_to_le32(mapping));
	//if (rtlpriv->dm.b_useramask) {
	if(1){
		SET_TX_DESC_RATE_ID(pdesc, ptcb_desc->ratr_index);
		SET_TX_DESC_MACID(pdesc, ptcb_desc->mac_id);
	} else {
		SET_TX_DESC_RATE_ID(pdesc, 0xC + ptcb_desc->ratr_index);
		SET_TX_DESC_MACID(pdesc, ptcb_desc->mac_id);
	}
/*	if (ieee80211_is_data_qos(fc))
		SET_TX_DESC_QOS(pdesc, 1);
*/
	if (!ieee80211_is_data_qos(fc))  {
		SET_TX_DESC_HWSEQ_EN(pdesc, 1);
		SET_TX_DESC_HWSEQ_SEL(pdesc, 0);
	}
	SET_TX_DESC_MORE_FRAG(pdesc, (b_lastseg ? 0 : 1));
	if (is_multicast_ether_addr(ieee80211_get_DA(hdr)) ||
	    is_broadcast_ether_addr(ieee80211_get_DA(hdr))) {
		SET_TX_DESC_BMC(pdesc, 1);
	}

	rtl8821ae_dm_set_tx_ant_by_tx_info(hw,pdesc,ptcb_desc->mac_id);
	RT_TRACE(COMP_SEND, DBG_TRACE, ("\n"));
}

void rtl8821ae_tx_fill_cmddesc(struct ieee80211_hw *hw,
			     u8 *pdesc, bool b_firstseg,
			     bool b_lastseg, struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 fw_queue = QSLT_BEACON;

	dma_addr_t mapping = pci_map_single(rtlpci->pdev,
					    skb->data, skb->len,
					    PCI_DMA_TODEVICE);

	if (pci_dma_mapping_error(rtlpci->pdev, mapping)) {
		RT_TRACE(COMP_SEND, DBG_TRACE,
			 ("DMA mapping error"));
		return;
	}
	CLEAR_PCI_TX_DESC_CONTENT(pdesc, TX_DESC_SIZE);

	SET_TX_DESC_FIRST_SEG(pdesc, 1);
	SET_TX_DESC_LAST_SEG(pdesc, 1);

	SET_TX_DESC_PKT_SIZE((u8 *) pdesc, (u16) (skb->len));

	SET_TX_DESC_OFFSET(pdesc, USB_HWDESC_HEADER_LEN);

	SET_TX_DESC_USE_RATE(pdesc, 1);
	SET_TX_DESC_TX_RATE(pdesc, DESC_RATE1M);
	SET_TX_DESC_DISABLE_FB(pdesc, 1);

	SET_TX_DESC_DATA_BW(pdesc, 0);

	SET_TX_DESC_HWSEQ_EN(pdesc, 1);

	SET_TX_DESC_QUEUE_SEL(pdesc, fw_queue);
/*
	if(IsCtrlNDPA(VirtualAddress) || IsMgntNDPA(VirtualAddress))
	{
		SET_TX_DESC_DATA_RETRY_LIMIT_8812(pDesc, 5);
		SET_TX_DESC_RETRY_LIMIT_ENABLE_8812(pDesc, 1);

		if(IsMgntNDPA(VirtualAddress))
		{
			SET_TX_DESC_NDPA_8812(pDesc, 1);
			SET_TX_DESC_RTS_SC_8812(pDesc, SCMapping_8812(Adapter, pTcb));
		}
		else
		{
			SET_TX_DESC_NDPA_8812(pDesc, 2);
			SET_TX_DESC_RTS_SC_8812(pDesc, SCMapping_8812(Adapter, pTcb));
		}
	}*/

	SET_TX_DESC_TX_BUFFER_SIZE(pdesc, (u16) (skb->len));

	SET_TX_DESC_TX_BUFFER_ADDRESS(pdesc, cpu_to_le32(mapping));

	SET_TX_DESC_MACID(pdesc, 0);

	SET_TX_DESC_OWN(pdesc, 1);

	RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_LOUD,
		      "H2C Tx Cmd Content\n",
		      pdesc, TX_DESC_SIZE);
}

void rtl8821ae_set_desc(struct ieee80211_hw * hw, u8 *pdesc, bool istx, u8 desc_name, u8 *val)
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
			RT_ASSERT(false, ("ERR txdesc :%d"
					  " not process\n", desc_name));
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
			RT_ASSERT(false, ("ERR rxdesc :%d "
					  "not process\n", desc_name));
			break;
		}
	}
}

u32 rtl8821ae_get_desc(u8 *pdesc, bool istx, u8 desc_name)
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
			RT_ASSERT(false, ("ERR txdesc :%d "
					  "not process\n", desc_name));
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
			RT_ASSERT(false, ("ERR rxdesc :%d "
					  "not process\n", desc_name));
			break;
		}
	}
	return ret;
}

bool rtl8821ae_is_tx_desc_closed(struct ieee80211_hw *hw,
				 u8 hw_queue, u16 index)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl8192_tx_ring *ring = &rtlpci->tx_ring[hw_queue];
	u8 *entry = (u8 *)(&ring->desc[ring->idx]);
	u8 own = (u8) rtl8821ae_get_desc(entry, true, HW_DESC_OWN);

	/*
	 *beacon packet will only use the first
	 *descriptor defautly,and the own may not
	 *be cleared by the hardware
	 */
	if (own)
		return false;
	else
		return true;
}


void rtl8821ae_tx_polling(struct ieee80211_hw *hw, u8 hw_queue)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (hw_queue == BEACON_QUEUE) {
		rtl_write_word(rtlpriv, REG_PCIE_CTRL_REG, BIT(4));
	} else {
		rtl_write_word(rtlpriv, REG_PCIE_CTRL_REG,
			       BIT(0) << (hw_queue));
	}
}
