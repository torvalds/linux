// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _RTL8188E_SRESET_C_

#include "../include/rtl8188e_sreset.h"
#include "../include/rtl8188e_hal.h"

void rtl8188e_sreset_xmit_status_check(struct adapter *padapter)
{
	u32 txdma_status;

	txdma_status = rtw_read32(padapter, REG_TXDMA_STATUS);
	if (txdma_status != 0x00) {
		DBG_88E("%s REG_TXDMA_STATUS:0x%08x\n", __func__, txdma_status);
		rtw_write32(padapter, REG_TXDMA_STATUS, txdma_status);
	}
	/* total xmit irp = 4 */
}

void rtl8188e_sreset_linked_status_check(struct adapter *padapter)
{
	u32 rx_dma_status = 0;
	u8 fw_status = 0;
	rx_dma_status = rtw_read32(padapter, REG_RXDMA_STATUS);
	if (rx_dma_status != 0x00) {
		DBG_88E("%s REG_RXDMA_STATUS:0x%08x\n", __func__, rx_dma_status);
		rtw_write32(padapter, REG_RXDMA_STATUS, rx_dma_status);
	}
	fw_status = rtw_read8(padapter, REG_FMETHR);
	if (fw_status != 0x00) {
		if (fw_status == 1)
			DBG_88E("%s REG_FW_STATUS (0x%02x), Read_Efuse_Fail !!\n", __func__, fw_status);
		else if (fw_status == 2)
			DBG_88E("%s REG_FW_STATUS (0x%02x), Condition_No_Match !!\n", __func__, fw_status);
	}
}
