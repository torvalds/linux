// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _RTL8188E_REDESC_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/rtl8188e_hal.h"

static void process_rssi(struct adapter *padapter, struct recv_frame *prframe)
{
	struct rx_pkt_attrib *pattrib = &prframe->attrib;
	struct signal_stat *signal_stat = &padapter->recvpriv.signal_strength_data;

	if (signal_stat->update_req) {
		signal_stat->total_num = 0;
		signal_stat->total_val = 0;
		signal_stat->update_req = 0;
	}

	signal_stat->total_num++;
	signal_stat->total_val  += pattrib->phy_info.SignalStrength;
	signal_stat->avg_val = signal_stat->total_val / signal_stat->total_num;
} /*  Process_UI_RSSI_8192C */

static void process_link_qual(struct adapter *padapter, struct recv_frame *prframe)
{
	struct rx_pkt_attrib *pattrib;
	struct signal_stat *signal_stat;

	if (!prframe || !padapter)
		return;

	pattrib = &prframe->attrib;
	signal_stat = &padapter->recvpriv.signal_qual_data;

	if (signal_stat->update_req) {
		signal_stat->total_num = 0;
		signal_stat->total_val = 0;
		signal_stat->update_req = 0;
	}

	signal_stat->total_num++;
	signal_stat->total_val  += pattrib->phy_info.SignalQuality;
	signal_stat->avg_val = signal_stat->total_val / signal_stat->total_num;
}

static void rtl8188e_process_phy_info(struct adapter *padapter, void *prframe)
{
	struct recv_frame *precvframe = (struct recv_frame *)prframe;

	/*  Check RSSI */
	process_rssi(padapter, precvframe);
	/*  Check EVM */
	process_link_qual(padapter,  precvframe);
}

void update_recvframe_attrib_88e(struct recv_frame *precvframe, struct recv_stat *prxstat)
{
	struct rx_pkt_attrib *pattrib = &precvframe->attrib;
	memset(pattrib, 0, sizeof(struct rx_pkt_attrib));

	pattrib->crc_err = (le32_to_cpu(prxstat->rxdw0) >> 14) & 0x1;

	pattrib->pkt_rpt_type = (le32_to_cpu(prxstat->rxdw3) >> 14) & 0x3;

	if (pattrib->pkt_rpt_type == NORMAL_RX) {
		pattrib->pkt_len = le32_to_cpu(prxstat->rxdw0) & 0x00003fff;
		pattrib->drvinfo_sz = ((le32_to_cpu(prxstat->rxdw0) >> 16) & 0xf) * 8;

		pattrib->physt = (le32_to_cpu(prxstat->rxdw0) >> 26) & 0x1;

		pattrib->bdecrypted = (le32_to_cpu(prxstat->rxdw0) & BIT(27)) ? 0 : 1;
		pattrib->encrypt = (le32_to_cpu(prxstat->rxdw0) >> 20) & 0x7;

		pattrib->qos = (le32_to_cpu(prxstat->rxdw0) >> 23) & 0x1;
		pattrib->priority = (le32_to_cpu(prxstat->rxdw1) >> 8) & 0xf;

		pattrib->amsdu = (le32_to_cpu(prxstat->rxdw1) >> 13) & 0x1;

		pattrib->seq_num = le32_to_cpu(prxstat->rxdw2) & 0x00000fff;
		pattrib->frag_num = (le32_to_cpu(prxstat->rxdw2) >> 12) & 0xf;
		pattrib->mfrag = (le32_to_cpu(prxstat->rxdw1) >> 27) & 0x1;
		pattrib->mdata = (le32_to_cpu(prxstat->rxdw1) >> 26) & 0x1;

		pattrib->mcs_rate = le32_to_cpu(prxstat->rxdw3) & 0x3f;
		pattrib->rxht = (le32_to_cpu(prxstat->rxdw3) >> 6) & 0x1;

		pattrib->icv_err = (le32_to_cpu(prxstat->rxdw0) >> 15) & 0x1;
		pattrib->shift_sz = (le32_to_cpu(prxstat->rxdw0) >> 24) & 0x3;
	} else if (pattrib->pkt_rpt_type == TX_REPORT1) { /* CCX */
		pattrib->pkt_len = TX_RPT1_PKT_LEN;
	} else if (pattrib->pkt_rpt_type == TX_REPORT2) {
		pattrib->pkt_len = le32_to_cpu(prxstat->rxdw0) & 0x3FF;

		pattrib->MacIDValidEntry[0] = le32_to_cpu(prxstat->rxdw4);
		pattrib->MacIDValidEntry[1] = le32_to_cpu(prxstat->rxdw5);

	} else if (pattrib->pkt_rpt_type == HIS_REPORT) {
		pattrib->pkt_len = le32_to_cpu(prxstat->rxdw0) & 0x00003fff;
	}
}

/*
 * Notice:
 *	Before calling this function,
 *	precvframe->rx_data should be ready!
 */
void update_recvframe_phyinfo_88e(struct recv_frame *precvframe, struct phy_stat *pphy_status)
{
	struct adapter *padapter = precvframe->adapter;
	struct rx_pkt_attrib *pattrib = &precvframe->attrib;
	struct hal_data_8188e *pHalData = &padapter->haldata;
	struct phy_info *pPHYInfo  = &pattrib->phy_info;
	u8 *wlanhdr = precvframe->rx_data;
	__le16 fc = *(__le16 *)wlanhdr;
	struct odm_per_pkt_info	pkt_info;
	u8 *sa = NULL;
	struct sta_priv *pstapriv;
	struct sta_info *psta;

	pkt_info.bPacketMatchBSSID = ((!ieee80211_is_ctl(fc)) &&
		!pattrib->icv_err && !pattrib->crc_err &&
		!memcmp(get_hdr_bssid(wlanhdr),
		 get_bssid(&padapter->mlmepriv), ETH_ALEN));

	pkt_info.bPacketToSelf = pkt_info.bPacketMatchBSSID &&
				 (!memcmp(get_da(wlanhdr),
				  myid(&padapter->eeprompriv), ETH_ALEN));

	pkt_info.bPacketBeacon = pkt_info.bPacketMatchBSSID &&
				 (GetFrameSubType(wlanhdr) == WIFI_BEACON);

	if (pkt_info.bPacketBeacon) {
		if (check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE))
			sa = padapter->mlmepriv.cur_network.network.MacAddress;
		/* to do Ad-hoc */
	} else {
		sa = get_sa(wlanhdr);
	}

	pstapriv = &padapter->stapriv;
	pkt_info.StationID = 0xFF;
	psta = rtw_get_stainfo(pstapriv, sa);
	if (psta)
		pkt_info.StationID = psta->mac_id;
	pkt_info.Rate = pattrib->mcs_rate;

	ODM_PhyStatusQuery(&pHalData->odmpriv, pPHYInfo, (u8 *)pphy_status, &(pkt_info), padapter);

	precvframe->psta = NULL;
	if (pkt_info.bPacketMatchBSSID &&
	    (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE))) {
		if (psta) {
			precvframe->psta = psta;
			rtl8188e_process_phy_info(padapter, precvframe);
		}
	} else if (pkt_info.bPacketToSelf || pkt_info.bPacketBeacon) {
		if (check_fwstate(&padapter->mlmepriv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE)) {
			if (psta)
				precvframe->psta = psta;
		}
		rtl8188e_process_phy_info(padapter, precvframe);
	}
}
