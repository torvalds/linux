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
#define _RTL8723A_REDESC_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <rtl8723a_hal.h>

static void process_rssi(struct rtw_adapter *padapter,
			 struct recv_frame *prframe)
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
}

static void process_link_qual(struct rtw_adapter *padapter,
			      struct recv_frame *prframe)
{
	struct rx_pkt_attrib *pattrib;
	struct signal_stat *signal_stat;

	if (prframe == NULL || padapter == NULL)
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

/* void rtl8723a_process_phy_info(struct rtw_adapter *padapter, union recv_frame *prframe) */
void rtl8723a_process_phy_info(struct rtw_adapter *padapter, void *prframe)
{
	struct recv_frame *precvframe = prframe;
	/*  Check RSSI */
	process_rssi(padapter, precvframe);
	/*  Check EVM */
	process_link_qual(padapter,  precvframe);
}
