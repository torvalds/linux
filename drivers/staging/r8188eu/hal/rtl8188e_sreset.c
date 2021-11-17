// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _RTL8188E_SRESET_C_

#include "../include/rtl8188e_sreset.h"
#include "../include/rtl8188e_hal.h"

void rtl8188e_silentreset_for_specific_platform(struct adapter *padapter)
{
}

void rtl8188e_sreset_xmit_status_check(struct adapter *padapter)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;

	unsigned long current_time;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	unsigned int diff_time;
	u32 txdma_status;

	txdma_status = rtw_read32(padapter, REG_TXDMA_STATUS);
	if (txdma_status != 0x00) {
		DBG_88E("%s REG_TXDMA_STATUS:0x%08x\n", __func__, txdma_status);
		rtw_write32(padapter, REG_TXDMA_STATUS, txdma_status);
		rtl8188e_silentreset_for_specific_platform(padapter);
	}
	/* total xmit irp = 4 */
	current_time = jiffies;
	if (0 == pxmitpriv->free_xmitbuf_cnt) {
		diff_time = jiffies_to_msecs(current_time - psrtpriv->last_tx_time);

		if (diff_time > 2000) {
			if (psrtpriv->last_tx_complete_time == 0) {
				psrtpriv->last_tx_complete_time = current_time;
			} else {
				diff_time = jiffies_to_msecs(current_time - psrtpriv->last_tx_complete_time);
				if (diff_time > 4000) {
					DBG_88E("%s tx hang\n", __func__);
					rtl8188e_silentreset_for_specific_platform(padapter);
				}
			}
		}
	}
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
