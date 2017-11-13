/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef __HAL_GSPI_H_
#define __HAL_GSPI_H_

#define ffaddr2deviceId(pdvobj, addr)	(pdvobj->Queue2Pipe[addr])

u8 rtw_hal_gspi_max_txoqt_free_space(_adapter *padapter);
u8 rtw_hal_gspi_query_tx_freepage(_adapter *padapter, u8 PageIdx, u8 RequiredPageNum);
void rtw_hal_gspi_update_tx_freepage(_adapter *padapter, u8 PageIdx, u8 RequiredPageNum);
void rtw_hal_set_gspi_tx_max_length(PADAPTER padapter, u8 numHQ, u8 numNQ, u8 numLQ, u8 numPubQ);
u32 rtw_hal_get_gspi_tx_max_length(PADAPTER padapter, u8 queue_idx);

#endif
