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
#define _RTL8192C_XMIT_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtl8192c_hal.h>

#ifdef CONFIG_XMIT_ACK
void dump_txrpt_ccx_8192c(void *buf)
{
	struct txrpt_ccx_8192c *txrpt_ccx = buf; 

	DBG_871X("%s:\n"
		"retry_cnt:%u, rsvd_0:%u, rts_retry_cnt:%u, rsvd_1:%u\n"
		"ccx_qtime:%u, missed_pkt_num:%u, rsvd_4:%u\n"
		"mac_id:%u, des1_fragssn:%u\n"
		"rpt_pkt_num:%u, pkt_drop:%u, lifetime_over:%u, retry_over:%u\n"
		"edca_tx_queue:%u, rsvd_7:%u, bmc:%u, pkt_ok:%u, init_ccx:%u\n"
		, __func__
		, txrpt_ccx->retry_cnt, txrpt_ccx->rsvd_0, txrpt_ccx->rts_retry_cnt, txrpt_ccx->rsvd_1
		, txrpt_ccx_qtime_8192c(txrpt_ccx), txrpt_ccx->missed_pkt_num, txrpt_ccx->rsvd_4
		, txrpt_ccx->mac_id, txrpt_ccx->des1_fragssn
		, txrpt_ccx->rpt_pkt_num, txrpt_ccx->pkt_drop, txrpt_ccx->lifetime_over, txrpt_ccx->retry_over
		, txrpt_ccx->edca_tx_queue, txrpt_ccx->rsvd_7, txrpt_ccx->bmc, txrpt_ccx->pkt_ok, txrpt_ccx->int_ccx
	);
}

void handle_txrpt_ccx_8192c(_adapter *adapter, void *buf)
{
	struct txrpt_ccx_8192c *txrpt_ccx = buf; 

	#ifdef DBG_CCX
	dump_txrpt_ccx_8192c(buf);
	#endif

	if (txrpt_ccx->int_ccx) {
		if (txrpt_ccx->pkt_ok)
			rtw_ack_tx_done(&adapter->xmitpriv, RTW_SCTX_DONE_SUCCESS);
		else
			rtw_ack_tx_done(&adapter->xmitpriv, RTW_SCTX_DONE_CCX_PKT_FAIL);
	}
}
#endif //CONFIG_XMIT_ACK

