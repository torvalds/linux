/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __HAL_SDIO_H_
#define __HAL_SDIO_H_

#define ffaddr2deviceId(pdvobj, addr)	(pdvobj->Queue2Pipe[addr])

u8 rtw_hal_sdio_max_txoqt_free_space(struct adapter *padapter);
u8 rtw_hal_sdio_query_tx_freepage(struct adapter *padapter, u8 PageIdx, u8 RequiredPageNum);
void rtw_hal_sdio_update_tx_freepage(struct adapter *padapter, u8 PageIdx, u8 RequiredPageNum);
void rtw_hal_set_sdio_tx_max_length(struct adapter *padapter, u8 numHQ, u8 numNQ, u8 numLQ, u8 numPubQ);
u32 rtw_hal_get_sdio_tx_max_length(struct adapter *padapter, u8 queue_idx);

#endif /* __RTW_LED_H_ */
