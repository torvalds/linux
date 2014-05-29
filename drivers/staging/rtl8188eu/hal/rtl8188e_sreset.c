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
#define _RTL8188E_SRESET_C_

#include <rtl8188e_sreset.h>
#include <rtl8188e_hal.h>

void rtl8188e_silentreset_for_specific_platform(struct adapter *padapter)
{
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
