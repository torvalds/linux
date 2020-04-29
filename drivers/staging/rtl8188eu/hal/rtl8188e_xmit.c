// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _RTL8188E_XMIT_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <rtl8188e_hal.h>

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

void _dbg_dump_tx_info(struct adapter *padapter, int frame_tag,
		       struct tx_desc *ptxdesc)
{
	u8 dmp_txpkt;
	bool dump_txdesc = false;

	rtw_hal_get_def_var(padapter, HAL_DEF_DBG_DUMP_TXPKT, &(dmp_txpkt));

	if (dmp_txpkt == 1) {/* dump txdesc for data frame */
		DBG_88E("dump tx_desc for data frame\n");
		if ((frame_tag & 0x0f) == DATA_FRAMETAG)
			dump_txdesc = true;
	} else if (dmp_txpkt == 2) {/* dump txdesc for mgnt frame */
		DBG_88E("dump tx_desc for mgnt frame\n");
		if ((frame_tag & 0x0f) == MGNT_FRAMETAG)
			dump_txdesc = true;
	}

	if (dump_txdesc) {
		DBG_88E("=====================================\n");
		DBG_88E("txdw0(0x%08x)\n", ptxdesc->txdw0);
		DBG_88E("txdw1(0x%08x)\n", ptxdesc->txdw1);
		DBG_88E("txdw2(0x%08x)\n", ptxdesc->txdw2);
		DBG_88E("txdw3(0x%08x)\n", ptxdesc->txdw3);
		DBG_88E("txdw4(0x%08x)\n", ptxdesc->txdw4);
		DBG_88E("txdw5(0x%08x)\n", ptxdesc->txdw5);
		DBG_88E("txdw6(0x%08x)\n", ptxdesc->txdw6);
		DBG_88E("txdw7(0x%08x)\n", ptxdesc->txdw7);
		DBG_88E("=====================================\n");
	}
}
