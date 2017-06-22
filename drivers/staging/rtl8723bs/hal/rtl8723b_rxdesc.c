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
#define _RTL8723B_REDESC_C_

#include <rtl8723b_hal.h>

static void process_rssi(struct adapter *padapter, union recv_frame *prframe)
{
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	struct signal_stat *signal_stat = &padapter->recvpriv.signal_strength_data;

	/* DBG_8192C("process_rssi => pattrib->rssil(%d) signal_strength(%d)\n ", pattrib->RecvSignalPower, pattrib->signal_strength); */
	/* if (pRfd->Status.bPacketToSelf || pRfd->Status.bPacketBeacon) */
	{
		if (signal_stat->update_req) {
			signal_stat->total_num = 0;
			signal_stat->total_val = 0;
			signal_stat->update_req = 0;
		}

		signal_stat->total_num++;
		signal_stat->total_val  += pattrib->phy_info.SignalStrength;
		signal_stat->avg_val = signal_stat->total_val / signal_stat->total_num;
	}

} /*  Process_UI_RSSI_8192C */

static void process_link_qual(struct adapter *padapter, union recv_frame *prframe)
{
	struct rx_pkt_attrib *pattrib;
	struct signal_stat *signal_stat;

	if (prframe == NULL || padapter == NULL)
		return;

	pattrib = &prframe->u.hdr.attrib;
	signal_stat = &padapter->recvpriv.signal_qual_data;

	/* DBG_8192C("process_link_qual => pattrib->signal_qual(%d)\n ", pattrib->signal_qual); */

	if (signal_stat->update_req) {
		signal_stat->total_num = 0;
		signal_stat->total_val = 0;
		signal_stat->update_req = 0;
	}

	signal_stat->total_num++;
	signal_stat->total_val  += pattrib->phy_info.SignalQuality;
	signal_stat->avg_val = signal_stat->total_val / signal_stat->total_num;
} /*  Process_UiLinkQuality8192S */


void rtl8723b_process_phy_info(struct adapter *padapter, void *prframe)
{
	union recv_frame *precvframe = (union recv_frame *)prframe;
	/*  */
	/*  Check RSSI */
	/*  */
	process_rssi(padapter, precvframe);
	/*  */
	/*  Check PWDB. */
	/*  */
	/* process_PWDB(padapter, precvframe); */

	/* UpdateRxSignalStatistics8192C(Adapter, pRfd); */
	/*  */
	/*  Check EVM */
	/*  */
	process_link_qual(padapter,  precvframe);
	#ifdef DBG_RX_SIGNAL_DISPLAY_RAW_DATA
	rtw_store_phy_info(padapter, prframe);
	#endif

}
