// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _RTL8188E_XMIT_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/rtl8188e_hal.h"

void dump_txrpt_ccx_88e(void *buf)
{
	struct txrpt_ccx_88e *txrpt_ccx = (struct txrpt_ccx_88e *)buf;

	DBG_88E("%s:\n"
		"tag1:%u, pkt_num:%u, txdma_underflow:%u, int_bt:%u, int_tri:%u, int_ccx:%u\n"
		"mac_id:%u, pkt_ok:%u, bmc:%u\n"
		"retry_cnt:%u, lifetime_over:%u, retry_over:%u\n"
		"ccx_qtime:%u\n"
		"final_data_rate:0x%02x\n"
		"qsel:%u, sw:0x%03x\n",
		__func__, txrpt_ccx->tag1, txrpt_ccx->pkt_num,
		txrpt_ccx->txdma_underflow, txrpt_ccx->int_bt,
		txrpt_ccx->int_tri, txrpt_ccx->int_ccx,
		txrpt_ccx->mac_id, txrpt_ccx->pkt_ok, txrpt_ccx->bmc,
		txrpt_ccx->retry_cnt, txrpt_ccx->lifetime_over,
		txrpt_ccx->retry_over, txrpt_ccx_qtime_88e(txrpt_ccx),
		txrpt_ccx->final_data_rate, txrpt_ccx->qsel,
		txrpt_ccx_sw_88e(txrpt_ccx)
	);
}

void handle_txrpt_ccx_88e(struct adapter *adapter, u8 *buf)
{
	struct txrpt_ccx_88e *txrpt_ccx = (struct txrpt_ccx_88e *)buf;

	if (txrpt_ccx->int_ccx) {
		if (txrpt_ccx->pkt_ok)
			rtw_ack_tx_done(&adapter->xmitpriv,
					RTW_SCTX_DONE_SUCCESS);
		else
			rtw_ack_tx_done(&adapter->xmitpriv,
					RTW_SCTX_DONE_CCX_PKT_FAIL);
	}
}
