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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTL8812A_RXDESC_C_

//#include <drv_types.h>
#include <rtl8812a_hal.h>

static void process_rssi(_adapter *padapter,union recv_frame *prframe)
{
	u32	last_rssi, tmp_val;
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	struct signal_stat * signal_stat = &padapter->recvpriv.signal_strength_data;
#endif //CONFIG_NEW_SIGNAL_STAT_PROCESS

	//DBG_8192C("process_rssi=> pattrib->rssil(%d) signal_strength(%d)\n ",pattrib->RecvSignalPower,pattrib->signal_strength);
	//if(pRfd->Status.bPacketToSelf || pRfd->Status.bPacketBeacon)
	{
	
	#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
		if(signal_stat->update_req) {
			signal_stat->total_num = 0;
			signal_stat->total_val = 0;
			signal_stat->update_req = 0;
		}

		signal_stat->total_num++;
		signal_stat->total_val  += pattrib->phy_info.SignalStrength;
		signal_stat->avg_val = signal_stat->total_val / signal_stat->total_num;		
	#else //CONFIG_NEW_SIGNAL_STAT_PROCESS
	
		//Adapter->RxStats.RssiCalculateCnt++;	//For antenna Test
		if(padapter->recvpriv.signal_strength_data.total_num++ >= PHY_RSSI_SLID_WIN_MAX)
		{
			padapter->recvpriv.signal_strength_data.total_num = PHY_RSSI_SLID_WIN_MAX;
			last_rssi = padapter->recvpriv.signal_strength_data.elements[padapter->recvpriv.signal_strength_data.index];
			padapter->recvpriv.signal_strength_data.total_val -= last_rssi;
		}
		padapter->recvpriv.signal_strength_data.total_val  +=pattrib->phy_info.SignalStrength;

		padapter->recvpriv.signal_strength_data.elements[padapter->recvpriv.signal_strength_data.index++] = pattrib->phy_info.SignalStrength;
		if(padapter->recvpriv.signal_strength_data.index >= PHY_RSSI_SLID_WIN_MAX)
			padapter->recvpriv.signal_strength_data.index = 0;


		tmp_val = padapter->recvpriv.signal_strength_data.total_val/padapter->recvpriv.signal_strength_data.total_num;
		
		if(padapter->recvpriv.is_signal_dbg) {
			padapter->recvpriv.signal_strength= padapter->recvpriv.signal_strength_dbg;
			padapter->recvpriv.rssi=(s8)translate_percentage_to_dbm(padapter->recvpriv.signal_strength_dbg);
		} else {
			padapter->recvpriv.signal_strength= tmp_val;
			padapter->recvpriv.rssi=(s8)translate_percentage_to_dbm(tmp_val);
		}

		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("UI RSSI = %d, ui_rssi.TotalVal = %d, ui_rssi.TotalNum = %d\n", tmp_val, padapter->recvpriv.signal_strength_data.total_val,padapter->recvpriv.signal_strength_data.total_num));
	#endif //CONFIG_NEW_SIGNAL_STAT_PROCESS
	}

}// Process_UI_RSSI_8192C



static void process_link_qual(_adapter *padapter,union recv_frame *prframe)
{
	u32	last_evm=0, tmpVal;
 	struct rx_pkt_attrib *pattrib;
#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	struct signal_stat * signal_stat;
#endif //CONFIG_NEW_SIGNAL_STAT_PROCESS

	if(prframe == NULL || padapter==NULL){
		return;
	}

	pattrib = &prframe->u.hdr.attrib;
#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	signal_stat = &padapter->recvpriv.signal_qual_data;
#endif //CONFIG_NEW_SIGNAL_STAT_PROCESS

	//DBG_8192C("process_link_qual=> pattrib->signal_qual(%d)\n ",pattrib->signal_qual);

#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	if(signal_stat->update_req) {
		signal_stat->total_num = 0;
		signal_stat->total_val = 0;
		signal_stat->update_req = 0;
	}

	signal_stat->total_num++;
	signal_stat->total_val  += pattrib->phy_info.SignalQuality;
	signal_stat->avg_val = signal_stat->total_val / signal_stat->total_num;
	
#else //CONFIG_NEW_SIGNAL_STAT_PROCESS
	if(pattrib->phy_info.SignalQuality != 0)
	{
			//
			// 1. Record the general EVM to the sliding window.
			//
			if(padapter->recvpriv.signal_qual_data.total_num++ >= PHY_LINKQUALITY_SLID_WIN_MAX)
			{
				padapter->recvpriv.signal_qual_data.total_num = PHY_LINKQUALITY_SLID_WIN_MAX;
				last_evm = padapter->recvpriv.signal_qual_data.elements[padapter->recvpriv.signal_qual_data.index];
				padapter->recvpriv.signal_qual_data.total_val -= last_evm;
			}
			padapter->recvpriv.signal_qual_data.total_val += pattrib->phy_info.SignalQuality;

			padapter->recvpriv.signal_qual_data.elements[padapter->recvpriv.signal_qual_data.index++] = pattrib->phy_info.SignalQuality;
			if(padapter->recvpriv.signal_qual_data.index >= PHY_LINKQUALITY_SLID_WIN_MAX)
				padapter->recvpriv.signal_qual_data.index = 0;

			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("Total SQ=%d  pattrib->signal_qual= %d\n", padapter->recvpriv.signal_qual_data.total_val, pattrib->phy_info.SignalQuality));

			// <1> Showed on UI for user, in percentage.
			tmpVal = padapter->recvpriv.signal_qual_data.total_val/padapter->recvpriv.signal_qual_data.total_num;
			padapter->recvpriv.signal_qual=(u8)tmpVal;

	}
	else
	{
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" pattrib->signal_qual =%d\n", pattrib->phy_info.SignalQuality));
	}
#endif //CONFIG_NEW_SIGNAL_STAT_PROCESS

}

static void process_phy_info(_adapter *padapter, void *prframe)
{
	union recv_frame *precvframe = (union recv_frame *)prframe;

	//
	// Check RSSI
	//
	process_rssi(padapter, precvframe);
	//
	// Check PWDB.
	//
	//process_PWDB(padapter, precvframe); 

	//UpdateRxSignalStatistics8192C(Adapter, pRfd);
	//
	// Check EVM
	//
	process_link_qual(padapter,  precvframe);
	#ifdef DBG_RX_SIGNAL_DISPLAY_RAW_DATA
	rtw_store_phy_info( padapter,prframe);
	#endif

}

void rtl8812_query_rx_desc_status(union recv_frame *precvframe, u8 *pdesc)
{
	struct rx_pkt_attrib	*pattrib = &precvframe->u.hdr.attrib;

	_rtw_memset(pattrib, 0, sizeof(struct rx_pkt_attrib));

	//Offset 0
	pattrib->pkt_len = (u16)GET_RX_STATUS_DESC_PKT_LEN_8812(pdesc);//(le32_to_cpu(pdesc->rxdw0)&0x00003fff)
	pattrib->crc_err = (u8)GET_RX_STATUS_DESC_CRC32_8812(pdesc);//((le32_to_cpu(pdesc->rxdw0) >> 14) & 0x1);
	pattrib->icv_err = (u8)GET_RX_STATUS_DESC_ICV_8812(pdesc);//((le32_to_cpu(pdesc->rxdw0) >> 15) & 0x1);
	pattrib->drvinfo_sz = (u8)GET_RX_STATUS_DESC_DRVINFO_SIZE_8812(pdesc) * 8;//((le32_to_cpu(pdesc->rxdw0) >> 16) & 0xf) * 8;//uint 2^3 = 8 bytes
	pattrib->encrypt = (u8)GET_RX_STATUS_DESC_SECURITY_8812(pdesc);//((le32_to_cpu(pdesc->rxdw0) >> 20) & 0x7);
	pattrib->qos = (u8)GET_RX_STATUS_DESC_QOS_8812(pdesc);//(( le32_to_cpu( pdesc->rxdw0 ) >> 23) & 0x1);// Qos data, wireless lan header length is 26
	pattrib->shift_sz = (u8)GET_RX_STATUS_DESC_SHIFT_8812(pdesc);//((le32_to_cpu(pdesc->rxdw0) >> 24) & 0x3);
	pattrib->physt = (u8)GET_RX_STATUS_DESC_PHY_STATUS_8812(pdesc);//((le32_to_cpu(pdesc->rxdw0) >> 26) & 0x1);
	pattrib->bdecrypted = !GET_RX_STATUS_DESC_SWDEC_8812(pdesc);//(le32_to_cpu(pdesc->rxdw0) & BIT(27))? 0:1;

	//Offset 4
	pattrib->priority = (u8)GET_RX_STATUS_DESC_TID_8812(pdesc);//((le32_to_cpu(pdesc->rxdw1) >> 8) & 0xf);
	pattrib->mdata = (u8)GET_RX_STATUS_DESC_MORE_DATA_8812(pdesc);//((le32_to_cpu(pdesc->rxdw1) >> 26) & 0x1);
	pattrib->mfrag = (u8)GET_RX_STATUS_DESC_MORE_FRAG_8812(pdesc);//((le32_to_cpu(pdesc->rxdw1) >> 27) & 0x1);//more fragment bit

	//Offset 8
	pattrib->seq_num = (u16)GET_RX_STATUS_DESC_SEQ_8812(pdesc);//(le32_to_cpu(pdesc->rxdw2) & 0x00000fff);
	pattrib->frag_num = (u8)GET_RX_STATUS_DESC_FRAG_8812(pdesc);//((le32_to_cpu(pdesc->rxdw2) >> 12) & 0xf);//fragmentation number

	if (GET_RX_STATUS_DESC_RPT_SEL_8812(pdesc))
		pattrib->pkt_rpt_type = C2H_PACKET;
	else
		pattrib->pkt_rpt_type = NORMAL_RX;

	//Offset 12
	pattrib->data_rate=(u8)GET_RX_STATUS_DESC_RX_RATE_8812(pdesc);//((le32_to_cpu(pdesc->rxdw3))&0x7f);

	//Offset 16
	//Offset 20

}


/*
 * Notice:
 *	Before calling this function,
 *	precvframe->u.hdr.rx_data should be ready!
 */
void rtl8812_query_rx_phy_status(
	union recv_frame	*precvframe,
	u8 				*pphy_status)
{
	PADAPTER 			padapter = precvframe->u.hdr.adapter;
	struct rx_pkt_attrib	*pattrib = &precvframe->u.hdr.attrib;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);	
	PODM_PHY_INFO_T 	pPHYInfo  = (PODM_PHY_INFO_T)(&pattrib->phy_info);
	u8					*wlanhdr;
	ODM_PACKET_INFO_T	pkt_info;
	u8 *sa = NULL;
	struct sta_priv *pstapriv;
	struct sta_info *psta;
	//_irqL		irqL;
	
	pkt_info.bPacketMatchBSSID =_FALSE;
	pkt_info.bPacketToSelf = _FALSE;
	pkt_info.bPacketBeacon = _FALSE;
	
	wlanhdr = get_recvframe_data(precvframe);

	pkt_info.bPacketMatchBSSID = ((!IsFrameTypeCtrl(wlanhdr)) &&
		!pattrib->icv_err && !pattrib->crc_err &&
		_rtw_memcmp(get_hdr_bssid(wlanhdr), get_bssid(&padapter->mlmepriv), ETH_ALEN));

	pkt_info.bPacketToSelf = pkt_info.bPacketMatchBSSID && (_rtw_memcmp(get_ra(wlanhdr), myid(&padapter->eeprompriv), ETH_ALEN));

	pkt_info.bPacketBeacon = pkt_info.bPacketMatchBSSID && (GetFrameSubType(wlanhdr) == WIFI_BEACON);

/*
	if(pkt_info.bPacketBeacon){
		if(check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE) == _TRUE){				
			sa = padapter->mlmepriv.cur_network.network.MacAddress;
			#if 0
			{					
				DBG_8192C("==> rx beacon from AP[%02x:%02x:%02x:%02x:%02x:%02x]\n",
					sa[0],sa[1],sa[2],sa[3],sa[4],sa[5]);					
			}
			#endif
		}
		else
		{
			//to do Ad-hoc
			sa = NULL;
		}
	}
	else{	
		sa = get_sa(wlanhdr);		
	}	
*/	
	sa = get_ta(wlanhdr);
	
	pstapriv = &padapter->stapriv;
	pkt_info.StationID = 0xFF;
	psta = rtw_get_stainfo(pstapriv, sa);
	if (psta)
	{
		pkt_info.StationID = psta->mac_id;		
		//DBG_8192C("%s ==> StationID(%d)\n",__FUNCTION__,pkt_info.StationID);
	}			
	pkt_info.DataRate = pattrib->data_rate;	
	//rtl8188e_query_rx_phy_status(precvframe, pphy_status);

	//_enter_critical_bh(&pHalData->odm_stainfo_lock, &irqL);	
	ODM_PhyStatusQuery(&pHalData->odmpriv,pPHYInfo,pphy_status,&(pkt_info));
	if(psta) psta->rssi = pattrib->phy_info.RecvSignalPower;
	//_exit_critical_bh(&pHalData->odm_stainfo_lock, &irqL);

	precvframe->u.hdr.psta = NULL;
	if (pkt_info.bPacketMatchBSSID &&
		(check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == _TRUE))
	{		
		if (psta)
		{			
			precvframe->u.hdr.psta = psta;
			process_phy_info(padapter, precvframe);
			
		}		
	}
	else if (pkt_info.bPacketToSelf || pkt_info.bPacketBeacon)
	{
		if (check_fwstate(&padapter->mlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == _TRUE)
		{		
			if (psta)
			{				
				precvframe->u.hdr.psta = psta;
			}
		}
		process_phy_info(padapter, precvframe);		
	}
}

