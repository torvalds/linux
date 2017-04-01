/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/
#define _RTL8188E_REDESC_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <rtl8188e_hal.h>

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

static void process_link_qual(struct adapter *padapter,
			      struct recv_frame *prframe)
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

void rtl8188e_process_phy_info(struct adapter *padapter,
		               struct recv_frame *precvframe)
{
	/*  Check RSSI */
	process_rssi(padapter, precvframe);
	/*  Check EVM */
	process_link_qual(padapter,  precvframe);
}

void update_recvframe_attrib_88e(struct recv_frame *precvframe,
				 struct recv_stat *prxstat)
{
	struct rx_pkt_attrib	*pattrib;
	struct recv_stat	report;

	report.rxdw0 = prxstat->rxdw0;
	report.rxdw1 = prxstat->rxdw1;
	report.rxdw2 = prxstat->rxdw2;
	report.rxdw3 = prxstat->rxdw3;
	report.rxdw4 = prxstat->rxdw4;
	report.rxdw5 = prxstat->rxdw5;

	pattrib = &precvframe->attrib;
	memset(pattrib, 0, sizeof(struct rx_pkt_attrib));

	pattrib->crc_err = (u8)((le32_to_cpu(report.rxdw0) >> 14) & 0x1);/* u8)prxreport->crc32; */

	/*  update rx report to recv_frame attribute */
	pattrib->pkt_rpt_type = (u8)((le32_to_cpu(report.rxdw3) >> 14) & 0x3);/* prxreport->rpt_sel; */

	if (pattrib->pkt_rpt_type == NORMAL_RX) { /* Normal rx packet */
		pattrib->pkt_len = (u16)(le32_to_cpu(report.rxdw0) & 0x00003fff);/* u16)prxreport->pktlen; */
		pattrib->drvinfo_sz = (u8)((le32_to_cpu(report.rxdw0) >> 16) & 0xf) * 8;/* u8)(prxreport->drvinfosize << 3); */

		pattrib->physt =  (u8)((le32_to_cpu(report.rxdw0) >> 26) & 0x1);/* u8)prxreport->physt; */

		pattrib->bdecrypted = (le32_to_cpu(report.rxdw0) & BIT(27)) ? 0 : 1;/* u8)(prxreport->swdec ? 0 : 1); */
		pattrib->encrypt = (u8)((le32_to_cpu(report.rxdw0) >> 20) & 0x7);/* u8)prxreport->security; */

		pattrib->qos = (u8)((le32_to_cpu(report.rxdw0) >> 23) & 0x1);/* u8)prxreport->qos; */
		pattrib->priority = (u8)((le32_to_cpu(report.rxdw1) >> 8) & 0xf);/* u8)prxreport->tid; */

		pattrib->amsdu = (u8)((le32_to_cpu(report.rxdw1) >> 13) & 0x1);/* u8)prxreport->amsdu; */

		pattrib->seq_num = (u16)(le32_to_cpu(report.rxdw2) & 0x00000fff);/* u16)prxreport->seq; */
		pattrib->frag_num = (u8)((le32_to_cpu(report.rxdw2) >> 12) & 0xf);/* u8)prxreport->frag; */
		pattrib->mfrag = (u8)((le32_to_cpu(report.rxdw1) >> 27) & 0x1);/* u8)prxreport->mf; */
		pattrib->mdata = (u8)((le32_to_cpu(report.rxdw1) >> 26) & 0x1);/* u8)prxreport->md; */

		pattrib->mcs_rate = (u8)(le32_to_cpu(report.rxdw3) & 0x3f);/* u8)prxreport->rxmcs; */
		pattrib->rxht = (u8)((le32_to_cpu(report.rxdw3) >> 6) & 0x1);/* u8)prxreport->rxht; */

		pattrib->icv_err = (u8)((le32_to_cpu(report.rxdw0) >> 15) & 0x1);/* u8)prxreport->icverr; */
		pattrib->shift_sz = (u8)((le32_to_cpu(report.rxdw0) >> 24) & 0x3);
	} else if (pattrib->pkt_rpt_type == TX_REPORT1) { /* CCX */
		pattrib->pkt_len = TX_RPT1_PKT_LEN;
		pattrib->drvinfo_sz = 0;
	} else if (pattrib->pkt_rpt_type == TX_REPORT2) { /*  TX RPT */
		pattrib->pkt_len = (u16)(le32_to_cpu(report.rxdw0) & 0x3FF);/* Rx length[9:0] */
		pattrib->drvinfo_sz = 0;

		/*  */
		/*  Get TX report MAC ID valid. */
		/*  */
		pattrib->MacIDValidEntry[0] = le32_to_cpu(report.rxdw4);
		pattrib->MacIDValidEntry[1] = le32_to_cpu(report.rxdw5);

	} else if (pattrib->pkt_rpt_type == HIS_REPORT) { /*  USB HISR RPT */
		pattrib->pkt_len = (u16)(le32_to_cpu(report.rxdw0) & 0x00003fff);/* u16)prxreport->pktlen; */
	}
}

/*
 * Notice:
 *	Before calling this function,
 *	precvframe->rx_data should be ready!
 */
void update_recvframe_phyinfo_88e(struct recv_frame *precvframe,
				  struct phy_stat *pphy_status)
{
	struct adapter *padapter = precvframe->adapter;
	struct rx_pkt_attrib *pattrib = &precvframe->attrib;
	struct odm_phy_status_info *pPHYInfo  = (struct odm_phy_status_info *)(&pattrib->phy_info);
	u8 *wlanhdr;
	struct odm_per_pkt_info	pkt_info;
	u8 *sa = NULL;
	struct sta_priv *pstapriv;
	struct sta_info *psta;

	pkt_info.bPacketMatchBSSID = false;
	pkt_info.bPacketToSelf = false;
	pkt_info.bPacketBeacon = false;

	wlanhdr = precvframe->pkt->data;

	pkt_info.bPacketMatchBSSID = ((!IsFrameTypeCtrl(wlanhdr)) &&
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

	ODM_PhyStatusQuery(&padapter->HalData->odmpriv, pPHYInfo,
			   (u8 *)pphy_status, &(pkt_info));

	precvframe->psta = NULL;
	if (pkt_info.bPacketMatchBSSID &&
	    (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE))) {
		if (psta) {
			precvframe->psta = psta;
			rtl8188e_process_phy_info(padapter, precvframe);
		}
	} else if (pkt_info.bPacketToSelf || pkt_info.bPacketBeacon) {
		if (check_fwstate(&padapter->mlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE)) {
			if (psta)
				precvframe->psta = psta;
		}
		rtl8188e_process_phy_info(padapter, precvframe);
	}
}
