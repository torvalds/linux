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
#define _HAL_SDIO_C_

#include <drv_types.h>
#include <hal_data.h>

u8 rtw_hal_sdio_max_txoqt_free_space(_adapter *padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if(pHalData->SdioTxOQTMaxFreeSpace < 8 )
		pHalData->SdioTxOQTMaxFreeSpace = 8;

	return pHalData->SdioTxOQTMaxFreeSpace;	
}

u8 rtw_hal_sdio_query_tx_freepage(_adapter *padapter, u8 PageIdx, u8 RequiredPageNum)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if ((pHalData->SdioTxFIFOFreePage[PageIdx]+pHalData->SdioTxFIFOFreePage[PUBLIC_QUEUE_IDX]) >= (RequiredPageNum))
		return _TRUE;
	else
		return _FALSE;
}

void rtw_hal_sdio_update_tx_freepage(_adapter *padapter, u8 PageIdx, u8 RequiredPageNum)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8	DedicatedPgNum = 0;
	u8	RequiredPublicFreePgNum = 0;
	//_irqL irql;

	//_enter_critical_bh(&pHalData->SdioTxFIFOFreePageLock, &irql);

	DedicatedPgNum = pHalData->SdioTxFIFOFreePage[PageIdx];
	if (RequiredPageNum <= DedicatedPgNum) {
		pHalData->SdioTxFIFOFreePage[PageIdx] -= RequiredPageNum;
	} else {
		pHalData->SdioTxFIFOFreePage[PageIdx] = 0;
		RequiredPublicFreePgNum = RequiredPageNum - DedicatedPgNum;
		pHalData->SdioTxFIFOFreePage[PUBLIC_QUEUE_IDX] -= RequiredPublicFreePgNum;
	}

	//_exit_critical_bh(&pHalData->SdioTxFIFOFreePageLock, &irql);
}

void rtw_hal_set_sdio_tx_max_length(PADAPTER padapter, u8 numHQ, u8 numNQ, u8 numLQ, u8 numPubQ)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u32	page_size;
	u32	lenHQ, lenNQ, lenLQ;

	rtw_hal_get_def_var(padapter, HAL_DEF_TX_PAGE_SIZE,&page_size);

	lenHQ = ((numHQ + numPubQ) >> 1) * page_size;
	lenNQ = ((numNQ + numPubQ) >> 1) * page_size;
	lenLQ = ((numLQ + numPubQ) >> 1) * page_size;

	pHalData->sdio_tx_max_len[HI_QUEUE_IDX] = (lenHQ > MAX_XMITBUF_SZ)? MAX_XMITBUF_SZ:lenHQ;
	pHalData->sdio_tx_max_len[MID_QUEUE_IDX] = (lenNQ > MAX_XMITBUF_SZ)? MAX_XMITBUF_SZ:lenNQ;
	pHalData->sdio_tx_max_len[LOW_QUEUE_IDX] = (lenLQ > MAX_XMITBUF_SZ)? MAX_XMITBUF_SZ:lenLQ;
}

u32 rtw_hal_get_sdio_tx_max_length(PADAPTER padapter, u8 queue_idx)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u32	deviceId, max_len;
	

	deviceId = ffaddr2deviceId(pdvobjpriv, queue_idx);
	switch (deviceId) {
		case WLAN_TX_HIQ_DEVICE_ID:
			max_len = pHalData->sdio_tx_max_len[HI_QUEUE_IDX];
			break;

		case WLAN_TX_MIQ_DEVICE_ID:
			max_len = pHalData->sdio_tx_max_len[MID_QUEUE_IDX];
			break;

		case WLAN_TX_LOQ_DEVICE_ID:
			max_len = pHalData->sdio_tx_max_len[LOW_QUEUE_IDX];
			break;

		default:
			max_len = pHalData->sdio_tx_max_len[MID_QUEUE_IDX];
			break;
	}

	return max_len;
}


