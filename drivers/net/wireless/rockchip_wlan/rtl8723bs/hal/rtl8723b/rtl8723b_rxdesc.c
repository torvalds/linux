/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#define _RTL8723B_REDESC_C_

#include <rtl8723b_hal.h>

void rtl8723b_query_rx_desc_status(union recv_frame *precvframe, u8 *pdesc)
{
	struct rx_pkt_attrib *pattrib;


	pattrib = &precvframe->u.hdr.attrib;
	_rtw_memset(pattrib, 0, sizeof(struct rx_pkt_attrib));

	pattrib->pkt_len = (u16)GET_RX_STATUS_DESC_PKT_LEN_8723B(pdesc);
	pattrib->pkt_rpt_type = GET_RX_STATUS_DESC_RPT_SEL_8723B(pdesc) ? C2H_PACKET : NORMAL_RX;

	if (pattrib->pkt_rpt_type == NORMAL_RX) {
		/* Offset 0 */
		pattrib->crc_err = (u8)GET_RX_STATUS_DESC_CRC32_8723B(pdesc);
		pattrib->icv_err = (u8)GET_RX_STATUS_DESC_ICV_8723B(pdesc);
		pattrib->drvinfo_sz = (u8)GET_RX_STATUS_DESC_DRVINFO_SIZE_8723B(pdesc) << 3;
		pattrib->encrypt = (u8)GET_RX_STATUS_DESC_SECURITY_8723B(pdesc);
		pattrib->qos = (u8)GET_RX_STATUS_DESC_QOS_8723B(pdesc);
		pattrib->shift_sz = (u8)GET_RX_STATUS_DESC_SHIFT_8723B(pdesc);
		pattrib->physt = (u8)GET_RX_STATUS_DESC_PHY_STATUS_8723B(pdesc);
		pattrib->bdecrypted = (u8)GET_RX_STATUS_DESC_SWDEC_8723B(pdesc) ? 0 : 1;

		/* Offset 4 */
		pattrib->priority = (u8)GET_RX_STATUS_DESC_TID_8723B(pdesc);
		pattrib->amsdu = (u8)GET_RX_STATUS_DESC_AMSDU_8723B(pdesc);
		pattrib->mdata = (u8)GET_RX_STATUS_DESC_MORE_DATA_8723B(pdesc);
		pattrib->mfrag = (u8)GET_RX_STATUS_DESC_MORE_FRAG_8723B(pdesc);

		/* Offset 8 */
		pattrib->seq_num = (u16)GET_RX_STATUS_DESC_SEQ_8723B(pdesc);
		pattrib->frag_num = (u8)GET_RX_STATUS_DESC_FRAG_8723B(pdesc);

		/* Offset 12 */
		pattrib->data_rate = (u8)GET_RX_STATUS_DESC_RX_RATE_8723B(pdesc);

		/* Offset 16 */
		pattrib->sgi = (u8)GET_RX_STATUS_DESC_SPLCP_8723B(pdesc);
		pattrib->ldpc = (u8)GET_RX_STATUS_DESC_LDPC_8723B(pdesc);
		pattrib->stbc = (u8)GET_RX_STATUS_DESC_STBC_8723B(pdesc);
		pattrib->bw = (u8)GET_RX_STATUS_DESC_BW_8723B(pdesc);

		/* Offset 20 */
		/* pattrib->tsfl=(u8)GET_RX_STATUS_DESC_TSFL_8723B(pdesc); */
	}
}
