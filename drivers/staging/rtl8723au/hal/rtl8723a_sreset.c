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
#define _RTL8723A_SRESET_C_

#include <rtl8723a_sreset.h>
#include <rtl8723a_hal.h>
#include <usb_ops_linux.h>

void rtl8723a_sreset_xmit_status_check(struct rtw_adapter *padapter)
{
	struct hal_data_8723a	*pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;

	unsigned long current_time;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	unsigned int diff_time;
	u32 txdma_status;

	txdma_status = rtl8723au_read32(padapter, REG_TXDMA_STATUS);
	if (txdma_status != 0) {
		DBG_8723A("%s REG_TXDMA_STATUS:0x%08x\n", __func__, txdma_status);
		rtw_sreset_reset(padapter);
	}

	current_time = jiffies;

	if (0 == pxmitpriv->free_xmitbuf_cnt || 0 == pxmitpriv->free_xmit_extbuf_cnt) {

		diff_time = jiffies_to_msecs(jiffies - psrtpriv->last_tx_time);

		if (diff_time > 2000) {
			if (psrtpriv->last_tx_complete_time == 0) {
				psrtpriv->last_tx_complete_time = current_time;
			} else {
				diff_time = jiffies_to_msecs(jiffies - psrtpriv->last_tx_complete_time);
				if (diff_time > 4000) {
					DBG_8723A("%s tx hang\n", __func__);
					rtw_sreset_reset(padapter);
				}
			}
		}
	}
}

void rtl8723a_sreset_linked_status_check(struct rtw_adapter *padapter)
{
}
