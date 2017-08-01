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
#define _RTL8723D_REDESC_C_

#include <rtl8723d_hal.h>

void rtl8723d_query_rx_desc_status(union recv_frame *precvframe, u8 *pdesc)
{
	struct rx_pkt_attrib *pattrib;


	pattrib = &precvframe->u.hdr.attrib;
	_rtw_memset(pattrib, 0, sizeof(struct rx_pkt_attrib));

	pattrib->pkt_len = (u16)GET_RX_STATUS_DESC_PKT_LEN_8723D(pdesc);
	pattrib->pkt_rpt_type = GET_RX_STATUS_DESC_RPT_SEL_8723D(pdesc) ? C2H_PACKET : NORMAL_RX;

	if (pattrib->pkt_rpt_type == NORMAL_RX) {
		/* Offset 0 */
		pattrib->crc_err = (u8)GET_RX_STATUS_DESC_CRC32_8723D(pdesc);
		pattrib->icv_err = (u8)GET_RX_STATUS_DESC_ICV_8723D(pdesc);
		pattrib->drvinfo_sz = (u8)GET_RX_STATUS_DESC_DRVINFO_SIZE_8723D(pdesc) << 3;
		pattrib->encrypt = (u8)GET_RX_STATUS_DESC_SECURITY_8723D(pdesc);
		pattrib->qos = (u8)GET_RX_STATUS_DESC_QOS_8723D(pdesc);
		pattrib->shift_sz = (u8)GET_RX_STATUS_DESC_SHIFT_8723D(pdesc);
		pattrib->physt = (u8)GET_RX_STATUS_DESC_PHY_STATUS_8723D(pdesc);
		pattrib->bdecrypted = (u8)GET_RX_STATUS_DESC_SWDEC_8723D(pdesc) ? 0 : 1;

		/* Offset 4 */
		pattrib->priority = (u8)GET_RX_STATUS_DESC_TID_8723D(pdesc);
		pattrib->amsdu = (u8)GET_RX_STATUS_DESC_AMSDU_8723D(pdesc);
		pattrib->mdata = (u8)GET_RX_STATUS_DESC_MORE_DATA_8723D(pdesc);
		pattrib->mfrag = (u8)GET_RX_STATUS_DESC_MORE_FRAG_8723D(pdesc);

		/* Offset 8 */
		pattrib->seq_num = (u16)GET_RX_STATUS_DESC_SEQ_8723D(pdesc);
		pattrib->frag_num = (u8)GET_RX_STATUS_DESC_FRAG_8723D(pdesc);

		/* Offset 12 */
		pattrib->data_rate = (u8)GET_RX_STATUS_DESC_RX_RATE_8723D(pdesc);

		/* Offset 20 */
		/* pattrib->tsfl=(u8)GET_RX_STATUS_DESC_TSFL_8723D(pdesc); */
	}
}
