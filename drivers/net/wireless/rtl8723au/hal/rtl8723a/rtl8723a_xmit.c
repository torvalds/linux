/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
#define _RTL8723A_XMIT_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtl8723a_hal.h>

#ifdef CONFIG_XMIT_ACK
void dump_txrpt_ccx_8723a(void *buf)
{
	struct txrpt_ccx_8723a *txrpt_ccx = buf; 

	DBG_871X("%s:\n"
		"tag1:%u, rsvd:%u, int_bt:%u, int_tri:%u, int_ccx:%u\n"
		"mac_id:%u, pkt_drop:%u, pkt_ok:%u, bmc:%u\n"
		"retry_cnt:%u, lifetime_over:%u, retry_over:%u\n"
		"ccx_qtime:%u\n"
		"final_data_rate:0x%02x\n"
		"qsel:%u, sw:0x%03x\n"
		, __func__
		, txrpt_ccx->tag1, txrpt_ccx->rsvd, txrpt_ccx->int_bt, txrpt_ccx->int_tri, txrpt_ccx->int_ccx
		, txrpt_ccx->mac_id, txrpt_ccx->pkt_drop, txrpt_ccx->pkt_ok, txrpt_ccx->bmc
		, txrpt_ccx->retry_cnt, txrpt_ccx->lifetime_over, txrpt_ccx->retry_over
		, txrpt_ccx_qtime_8723a(txrpt_ccx)
		, txrpt_ccx->final_data_rate
		, txrpt_ccx->qsel, txrpt_ccx_sw_8723a(txrpt_ccx)
	);
}

void handle_txrpt_ccx_8723a(_adapter *adapter, void *buf)
{
	struct txrpt_ccx_8723a *txrpt_ccx = buf; 

	#ifdef DBG_CCX
	dump_txrpt_ccx_8723a(buf);
	#endif

	if (txrpt_ccx->int_ccx) {
		if (txrpt_ccx->pkt_ok)
			rtw_ack_tx_done(&adapter->xmitpriv, RTW_SCTX_DONE_SUCCESS);
		else
			rtw_ack_tx_done(&adapter->xmitpriv, RTW_SCTX_DONE_CCX_PKT_FAIL);
	}
}
#endif //CONFIG_XMIT_ACK

