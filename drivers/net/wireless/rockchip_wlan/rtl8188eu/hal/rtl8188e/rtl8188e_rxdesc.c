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
#define _RTL8188E_REDESC_C_

#include <drv_types.h>
#include <rtl8188e_hal.h>

void rtl8188e_query_rx_desc_status(
	union recv_frame *precvframe,
	struct recv_stat *prxstat)
{
	struct rx_pkt_attrib	*pattrib;
	struct recv_stat	report;
	PRXREPORT		prxreport;
	//struct recv_frame_hdr	*phdr;

	//phdr = &precvframe->u.hdr;

	report.rxdw0 = le32_to_cpu(prxstat->rxdw0);
	report.rxdw1 = le32_to_cpu(prxstat->rxdw1);
	report.rxdw2 = le32_to_cpu(prxstat->rxdw2);
	report.rxdw3 = le32_to_cpu(prxstat->rxdw3);
	report.rxdw4 = le32_to_cpu(prxstat->rxdw4);
	report.rxdw5 = le32_to_cpu(prxstat->rxdw5);

	prxreport = (PRXREPORT)&report;

	pattrib = &precvframe->u.hdr.attrib;
	_rtw_memset(pattrib, 0, sizeof(struct rx_pkt_attrib));

	pattrib->crc_err = (u8)((report.rxdw0 >> 14) & 0x1);;//(u8)prxreport->crc32;	
	
	// update rx report to recv_frame attribute
	pattrib->pkt_rpt_type = (u8)((report.rxdw3 >> 14) & 0x3);//prxreport->rpt_sel;
	
	if(pattrib->pkt_rpt_type == NORMAL_RX)//Normal rx packet	
	{
		pattrib->pkt_len = (u16)(report.rxdw0 &0x00003fff);//(u16)prxreport->pktlen;
		pattrib->drvinfo_sz = (u8)((report.rxdw0 >> 16) & 0xf) * 8;//(u8)(prxreport->drvinfosize << 3);
			
		pattrib->physt =  (u8)((report.rxdw0 >> 26) & 0x1);//(u8)prxreport->physt;	

		pattrib->bdecrypted = (report.rxdw0 & BIT(27))? 0:1;//(u8)(prxreport->swdec ? 0 : 1);
		pattrib->encrypt = (u8)((report.rxdw0 >> 20) & 0x7);//(u8)prxreport->security;

		pattrib->qos = (u8)((report.rxdw0 >> 23) & 0x1);//(u8)prxreport->qos;
		pattrib->priority = (u8)((report.rxdw1 >> 8) & 0xf);//(u8)prxreport->tid;

		pattrib->amsdu = (u8)((report.rxdw1 >> 13) & 0x1);//(u8)prxreport->amsdu;

		pattrib->seq_num = (u16)(report.rxdw2 & 0x00000fff);//(u16)prxreport->seq;
		pattrib->frag_num = (u8)((report.rxdw2 >> 12) & 0xf);//(u8)prxreport->frag;
		pattrib->mfrag = (u8)((report.rxdw1 >> 27) & 0x1);//(u8)prxreport->mf;
		pattrib->mdata = (u8)((report.rxdw1 >> 26) & 0x1);//(u8)prxreport->md;

		pattrib->data_rate = (u8)(report.rxdw3 & 0x3f);//(u8)prxreport->rxmcs;
		
		pattrib->icv_err = (u8)((report.rxdw0 >> 15) & 0x1);//(u8)prxreport->icverr;
		pattrib->shift_sz = (u8)((report.rxdw0 >> 24) & 0x3);
	
	}
	else if(pattrib->pkt_rpt_type == TX_REPORT1)//CCX
	{
		pattrib->pkt_len = TX_RPT1_PKT_LEN;
		pattrib->drvinfo_sz = 0;
	}
	else if(pattrib->pkt_rpt_type == TX_REPORT2)// TX RPT
	{
		pattrib->pkt_len =(u16)(report.rxdw0 & 0x3FF);//Rx length[9:0]
		pattrib->drvinfo_sz = 0;

		//
		// Get TX report MAC ID valid.
		//
		pattrib->MacIDValidEntry[0] = report.rxdw4;
		pattrib->MacIDValidEntry[1] = report.rxdw5;
		
	}
	else if(pattrib->pkt_rpt_type == HIS_REPORT)// USB HISR RPT
	{
		pattrib->pkt_len = (u16)(report.rxdw0 &0x00003fff);//(u16)prxreport->pktlen;
	}	
	
}

