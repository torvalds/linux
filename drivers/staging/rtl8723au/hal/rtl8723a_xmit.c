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
 ******************************************************************************/
#define _RTL8723A_XMIT_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <rtl8723a_hal.h>

void handle_txrpt_ccx_8723a(struct rtw_adapter *adapter, void *buf)
{
	struct txrpt_ccx_8723a *txrpt_ccx = buf;

	if (txrpt_ccx->int_ccx) {
		if (txrpt_ccx->pkt_ok)
			rtw_ack_tx_done23a(&adapter->xmitpriv, RTW_SCTX_DONE_SUCCESS);
		else
			rtw_ack_tx_done23a(&adapter->xmitpriv, RTW_SCTX_DONE_CCX_PKT_FAIL);
	}
}
