// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _RTL8188E_XMIT_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/rtl8188e_hal.h"

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
