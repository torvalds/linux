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
#define _RTL8703B_REDESC_C_

#include <rtl8703b_hal.h>

void rtl8703b_query_rx_desc_status(union recv_frame *precvframe, u8 *pdesc)
{
	struct rx_pkt_attrib *pattrib;


	pattrib = &precvframe->u.hdr.attrib;
	_rtw_memset(pattrib, 0, sizeof(struct rx_pkt_attrib));

	pattrib->pkt_len = (u16)GET_RX_STATUS_DESC_PKT_LEN_8703B(pdesc);
	pattrib->pkt_rpt_type = GET_RX_STATUS_DESC_RPT_SEL_8703B(pdesc) ? C2H_PACKET : NORMAL_RX;

	if (pattrib->pkt_rpt_type == NORMAL_RX) {
		/* Offset 0 */
		pattrib->crc_err = (u8)GET_RX_STATUS_DESC_CRC32_8703B(pdesc);
		pattrib->icv_err = (u8)GET_RX_STATUS_DESC_ICV_8703B(pdesc);
		pattrib->drvinfo_sz = (u8)GET_RX_STATUS_DESC_DRVINFO_SIZE_8703B(pdesc) << 3;
		pattrib->encrypt = (u8)GET_RX_STATUS_DESC_SECURITY_8703B(pdesc);
		pattrib->qos = (u8)GET_RX_STATUS_DESC_QOS_8703B(pdesc);
		pattrib->shift_sz = (u8)GET_RX_STATUS_DESC_SHIFT_8703B(pdesc);
		pattrib->physt = (u8)GET_RX_STATUS_DESC_PHY_STATUS_8703B(pdesc);
		pattrib->bdecrypted = (u8)GET_RX_STATUS_DESC_SWDEC_8703B(pdesc) ? 0 : 1;

		/* Offset 4 */
		pattrib->priority = (u8)GET_RX_STATUS_DESC_TID_8703B(pdesc);
		pattrib->amsdu = (u8)GET_RX_STATUS_DESC_AMSDU_8703B(pdesc);
		pattrib->mdata = (u8)GET_RX_STATUS_DESC_MORE_DATA_8703B(pdesc);
		pattrib->mfrag = (u8)GET_RX_STATUS_DESC_MORE_FRAG_8703B(pdesc);

		/* Offset 8 */
		pattrib->seq_num = (u16)GET_RX_STATUS_DESC_SEQ_8703B(pdesc);
		pattrib->frag_num = (u8)GET_RX_STATUS_DESC_FRAG_8703B(pdesc);

		/* Offset 12 */
		pattrib->data_rate = (u8)GET_RX_STATUS_DESC_RX_RATE_8703B(pdesc);

		/* Offset 16 */
		pattrib->sgi = (u8)GET_RX_STATUS_DESC_SPLCP_8703B(pdesc);
		pattrib->ldpc = (u8)GET_RX_STATUS_DESC_LDPC_8703B(pdesc);
		pattrib->stbc = (u8)GET_RX_STATUS_DESC_STBC_8703B(pdesc);
		pattrib->bw = (u8)GET_RX_STATUS_DESC_BW_8703B(pdesc);

		/* Offset 20 */
		/* pattrib->tsfl=(u8)GET_RX_STATUS_DESC_TSFL_8703B(pdesc); */
	}
}
