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

#ifndef RTW_HALMAC
static void dump_mac_page0(PADAPTER padapter)
{
	char str_out[128];
	char str_val[8];
	char *p = NULL;
	int index = 0, i = 0;
	u8 val8 = 0, len = 0;

	RTW_ERR("Dump MAC Page0 register:\n");
	for (index = 0 ; index < 0x100 ; index += 16) {
		p = &str_out[0];
		len = snprintf(str_val, sizeof(str_val),
			       "0x%02x: ", index);
		strncpy(str_out, str_val, len);
		p += len;

		for (i = 0 ; i < 16 ; i++) {
			len = snprintf(str_val, sizeof(str_val), "%02x ",
				       rtw_read8(padapter, index + i));
			strncpy(p, str_val, len);
			p += len;
		}
		RTW_INFO("%s\n", str_out);
		_rtw_memset(&str_out, '\0', sizeof(str_out));
	}
}

/*
 * Description:
 *	Call this function to make sure power on successfully
 *
 * Return:
 *	_SUCCESS	enable success
 *	_FAIL	enable fail
 */
bool sdio_power_on_check(PADAPTER padapter) {
	u32 val_offset0, val_offset1, val_offset2, val_offset3;
	u32 val_mix = 0;
	u32 res = 0;
	bool ret = _FAIL;
	int index = 0;

	val_offset0 = rtw_read8(padapter, REG_CR);
	val_offset1 = rtw_read8(padapter, REG_CR + 1);
	val_offset2 = rtw_read8(padapter, REG_CR + 2);
	val_offset3 = rtw_read8(padapter, REG_CR + 3);

	if (val_offset0 == 0xEA || val_offset1 == 0xEA ||
	    val_offset2 == 0xEA || val_offset3 == 0xEA) {
		RTW_INFO("%s: power on fail, do Power on again\n", __func__);
		return ret;
	}

	val_mix = val_offset3 << 24 | val_mix;
	val_mix = val_offset2 << 16 | val_mix;
	val_mix = val_offset1 << 8 | val_mix;
	val_mix = val_offset0 | val_mix;

	res = rtw_read32(padapter, REG_CR);

	RTW_INFO("%s: val_mix:0x%08x, res:0x%08x\n", __func__, val_mix, res);

	while (index < 100) {
		if (res == val_mix) {
			RTW_INFO("%s: 0x100 the result of cmd52 and cmd53 is the same.\n", __func__);
			ret = _SUCCESS;
			break;
		} else {
			RTW_INFO("%s: 0x100 cmd52 and cmd53 is not the same(index:%d).\n", __func__, index);
			res = rtw_read32(padapter, REG_CR);
			index++;
			ret = _FAIL;
		}
	}

	if (ret) {
		index = 0;
		while (index < 100) {
			rtw_write32(padapter, 0x1B8, 0x12345678);
			res = rtw_read32(padapter, 0x1B8);
			if (res == 0x12345678) {
				RTW_INFO("%s: 0x1B8 test Pass.\n", __func__);
				ret = _SUCCESS;
				break;
			} else {
				index++;
				RTW_INFO("%s: 0x1B8 test Fail(index: %d).\n", __func__, index);
				ret = _FAIL;
			}
		}
	} else
		RTW_INFO("%s: fail at cmd52, cmd53.\n", __func__);

	if (ret == _FAIL)
		dump_mac_page0(padapter);

	return ret;
}

u8 rtw_hal_sdio_max_txoqt_free_space(_adapter *padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if (pHalData->SdioTxOQTMaxFreeSpace < 8)
		pHalData->SdioTxOQTMaxFreeSpace = 8;

	return pHalData->SdioTxOQTMaxFreeSpace;
}

u8 rtw_hal_sdio_query_tx_freepage(_adapter *padapter, u8 PageIdx, u8 RequiredPageNum)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if ((pHalData->SdioTxFIFOFreePage[PageIdx] + pHalData->SdioTxFIFOFreePage[PUBLIC_QUEUE_IDX]) >= (RequiredPageNum))
		return _TRUE;
	else
		return _FALSE;
}

void rtw_hal_sdio_update_tx_freepage(_adapter *padapter, u8 PageIdx, u8 RequiredPageNum)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8	DedicatedPgNum = 0;
	u8	RequiredPublicFreePgNum = 0;
	/* _irqL irql; */

	/* _enter_critical_bh(&pHalData->SdioTxFIFOFreePageLock, &irql); */

	DedicatedPgNum = pHalData->SdioTxFIFOFreePage[PageIdx];
	if (RequiredPageNum <= DedicatedPgNum)
		pHalData->SdioTxFIFOFreePage[PageIdx] -= RequiredPageNum;
	else {
		pHalData->SdioTxFIFOFreePage[PageIdx] = 0;
		RequiredPublicFreePgNum = RequiredPageNum - DedicatedPgNum;
		pHalData->SdioTxFIFOFreePage[PUBLIC_QUEUE_IDX] -= RequiredPublicFreePgNum;
	}

	/* _exit_critical_bh(&pHalData->SdioTxFIFOFreePageLock, &irql); */
}

void rtw_hal_set_sdio_tx_max_length(PADAPTER padapter, u8 numHQ, u8 numNQ, u8 numLQ, u8 numPubQ)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u32	page_size;
	u32	lenHQ, lenNQ, lenLQ;

	rtw_hal_get_def_var(padapter, HAL_DEF_TX_PAGE_SIZE, &page_size);

	lenHQ = ((numHQ + numPubQ) >> 1) * page_size;
	lenNQ = ((numNQ + numPubQ) >> 1) * page_size;
	lenLQ = ((numLQ + numPubQ) >> 1) * page_size;

	pHalData->sdio_tx_max_len[HI_QUEUE_IDX] = (lenHQ > MAX_XMITBUF_SZ) ? MAX_XMITBUF_SZ : lenHQ;
	pHalData->sdio_tx_max_len[MID_QUEUE_IDX] = (lenNQ > MAX_XMITBUF_SZ) ? MAX_XMITBUF_SZ : lenNQ;
	pHalData->sdio_tx_max_len[LOW_QUEUE_IDX] = (lenLQ > MAX_XMITBUF_SZ) ? MAX_XMITBUF_SZ : lenLQ;
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

#ifdef CONFIG_FW_C2H_REG
void sd_c2h_hisr_hdl(_adapter *adapter)
{
	u8 c2h_evt[C2H_REG_LEN] = {0};
	u8 id, seq, plen;
	u8 *payload;

	if (rtw_hal_c2h_evt_read(adapter, c2h_evt) != _SUCCESS)
		goto exit;

	if (rtw_hal_c2h_reg_hdr_parse(adapter, c2h_evt, &id, &seq, &plen, &payload) != _SUCCESS)
		goto exit;
		
	if (rtw_hal_c2h_id_handle_directly(adapter, id, seq, plen, payload)) {
		/* Handle directly */
		rtw_hal_c2h_handler(adapter, id, seq, plen, payload);
		goto exit;
	}

	if (rtw_c2h_reg_wk_cmd(adapter, c2h_evt) != _SUCCESS)
		RTW_ERR("%s rtw_c2h_reg_wk_cmd fail\n", __func__);

exit:
	return;
}
#endif

#endif /* !RTW_HALMAC */
