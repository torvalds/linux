/* SPDX-License-Identifier: GPL-2.0 */
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
#define _HAL_SDIO_C_

#include <drv_types.h>
#include <hal_data.h>

#ifndef RTW_HALMAC
const char *_sdio_tx_queue_str[] = {
	"H",
	"M",
	"L",
};

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

void rtw_hal_set_sdio_tx_max_length(PADAPTER padapter, u8 numHQ, u8 numNQ, u8 numLQ, u8 numPubQ, u8 div_num)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u32	page_size;
	u32	lenHQ, lenNQ, lenLQ;

	rtw_hal_get_def_var(padapter, HAL_DEF_TX_PAGE_SIZE, &page_size);

	lenHQ = ((numHQ + numPubQ) / div_num) * page_size;
	lenNQ = ((numNQ + numPubQ) / div_num) * page_size;
	lenLQ = ((numLQ + numPubQ) / div_num) * page_size;

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

#ifdef CONFIG_SDIO_TX_ENABLE_AVAL_INT
#if defined(CONFIG_RTL8188F) || defined(CONFIG_RTL8188GTV)
void rtw_hal_sdio_avail_page_threshold_init(_adapter *adapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);

	hal_data->sdio_avail_int_en_q = 0xFF;
	rtw_write32(adapter, REG_TQPNT1, 0xFFFFFFFF);
	rtw_write32(adapter, REG_TQPNT2, 0xFFFFFFFF);
}

void rtw_hal_sdio_avail_page_threshold_en(_adapter *adapter, u8 qidx)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);

	if (hal_data->sdio_avail_int_en_q != qidx) {
		u32 page_size;
		u32 tx_max_len;
		u32 threshold_reg[] = {REG_TQPNT1, REG_TQPNT1 + 2, REG_TQPNT2, REG_TQPNT2 + 2}; /* H, M, L, E */
		u8 dw_shift[] = {0, 16, 0, 16}; /* H, M, L, E */
		u32 threshold = 0;

		rtw_hal_get_def_var(adapter, HAL_DEF_TX_PAGE_SIZE, &page_size);
		tx_max_len = hal_data->sdio_tx_max_len[qidx];

		/* use same low-high threshold as page num from tx_max_len */
		threshold = PageNum(tx_max_len, page_size);
		threshold |= (threshold) << 8;

		if (hal_data->sdio_avail_int_en_q == 0xFF)
			rtw_write16(adapter, threshold_reg[qidx], threshold);
		else if (hal_data->sdio_avail_int_en_q >> 1 == qidx >> 1) {/* same dword */
			rtw_write16(adapter, threshold_reg[hal_data->sdio_avail_int_en_q], 0);
			rtw_write32(adapter, threshold_reg[qidx & 0xFE]
				, (0xFFFF << dw_shift[hal_data->sdio_avail_int_en_q]) | (threshold << dw_shift[qidx]));
		} else {
			rtw_write16(adapter, threshold_reg[hal_data->sdio_avail_int_en_q], 0);
			rtw_write16(adapter, threshold_reg[hal_data->sdio_avail_int_en_q], 0xFFFF);
			rtw_write16(adapter, threshold_reg[qidx], threshold);
		}

		hal_data->sdio_avail_int_en_q = qidx;

		#ifdef DBG_TX_FREE_PAGE
		RTW_INFO("DWQP enable avail page threshold %s:%u-%u\n", sdio_tx_queue_str(qidx)
			, threshold & 0xFF, threshold >> 8);
		#endif
	}
}
#endif
#endif /* CONFIG_SDIO_TX_ENABLE_AVAL_INT */

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

#ifdef CONFIG_SDIO_CHK_HCI_RESUME

#ifndef SDIO_HCI_RESUME_PWR_RDY_TIMEOUT_MS
	#define SDIO_HCI_RESUME_PWR_RDY_TIMEOUT_MS 200
#endif
#ifndef DBG_SDIO_CHK_HCI_RESUME
	#define DBG_SDIO_CHK_HCI_RESUME 0
#endif

bool sdio_chk_hci_resume(struct intf_hdl *pintfhdl)
{
	_adapter *adapter = pintfhdl->padapter;
	u8 hci_sus_state;
	u8 sus_ctl, sus_ctl_ori = 0xEA;
	u8 do_leave = 0;
	systime start = 0, end = 0;
	u32 poll_cnt = 0;
	u8 timeout = 0;
	u8 sr = 0;
	s32 err = 0;

	rtw_hal_get_hwreg(adapter, HW_VAR_HCI_SUS_STATE, &hci_sus_state);
	if (hci_sus_state == HCI_SUS_LEAVE || hci_sus_state == HCI_SUS_ERR)
		goto no_hdl;

	err = sd_cmd52_read(pintfhdl, SDIO_LOCAL_CMD_ADDR(SDIO_REG_HSUS_CTRL), 1, &sus_ctl);
	if (err)
		goto exit;
	sus_ctl_ori = sus_ctl;

	if ((sus_ctl & HCI_RESUME_PWR_RDY) && !(sus_ctl & HCI_SUS_CTRL))
		goto exit;

	if (sus_ctl & HCI_SUS_CTRL) {
		sus_ctl &= ~(HCI_SUS_CTRL);
		err = sd_cmd52_write(pintfhdl, SDIO_LOCAL_CMD_ADDR(SDIO_REG_HSUS_CTRL), 1, &sus_ctl);
		if (err)
			goto exit;
	}

	do_leave = 1;

	/* polling for HCI_RESUME_PWR_RDY && !HCI_SUS_CTRL */
	start = rtw_get_current_time();
	while (1) {
		if (rtw_is_surprise_removed(adapter)) {
			sr = 1;
			break;
		}

		err = sd_cmd52_read(pintfhdl, SDIO_LOCAL_CMD_ADDR(SDIO_REG_HSUS_CTRL), 1, &sus_ctl);
		poll_cnt++;

		if (!err && (sus_ctl & HCI_RESUME_PWR_RDY) && !(sus_ctl & HCI_SUS_CTRL))
			break;

		if (rtw_get_passing_time_ms(start) > SDIO_HCI_RESUME_PWR_RDY_TIMEOUT_MS) {
			timeout = 1;
			break;
		}
	}
	end = rtw_get_current_time();

exit:

	if (DBG_SDIO_CHK_HCI_RESUME) {
		RTW_INFO(FUNC_ADPT_FMT" hci_sus_state:%u, sus_ctl:0x%02x(0x%02x), do_leave:%u, to:%u, err:%u\n"
			, FUNC_ADPT_ARG(adapter), hci_sus_state, sus_ctl, sus_ctl_ori, do_leave, timeout, err);
		if (start != 0 || end != 0) {
			RTW_INFO(FUNC_ADPT_FMT" polling %d ms, cnt:%u\n"
				, FUNC_ADPT_ARG(adapter), rtw_get_time_interval_ms(start, end), poll_cnt);
		}
	}

	if (timeout) {
		RTW_ERR(FUNC_ADPT_FMT" timeout(err:%d) sus_ctl:0x%02x\n"
			, FUNC_ADPT_ARG(adapter), err, sus_ctl);
	} else if (err) {
		RTW_ERR(FUNC_ADPT_FMT" err:%d\n"
			, FUNC_ADPT_ARG(adapter), err);
	}

no_hdl:
	return do_leave ? _TRUE : _FALSE;
}

void sdio_chk_hci_suspend(struct intf_hdl *pintfhdl)
{
#define SDIO_CHK_HCI_SUSPEND_POLLING 0

	_adapter *adapter = pintfhdl->padapter;
	u8 hci_sus_state;
	u8 sus_ctl, sus_ctl_ori = 0xEA;
	systime start = 0, end = 0;
	u32 poll_cnt = 0;
	u8 timeout = 0;
	u8 sr = 0;
	s32 err = 0;
	u8 device_id;
	u16 offset;

	rtw_hal_get_hwreg(adapter, HW_VAR_HCI_SUS_STATE, &hci_sus_state);
	if (hci_sus_state == HCI_SUS_LEAVE || hci_sus_state == HCI_SUS_LEAVING || hci_sus_state == HCI_SUS_ERR)
		goto no_hdl;

	err = sd_cmd52_read(pintfhdl, SDIO_LOCAL_CMD_ADDR(SDIO_REG_HSUS_CTRL), 1, &sus_ctl);
	if (err)
		goto exit;
	sus_ctl_ori = sus_ctl;

	if (!(sus_ctl & HCI_RESUME_PWR_RDY))
		goto exit;

	sus_ctl |= HCI_SUS_CTRL;
	err = sd_cmd52_write(pintfhdl, SDIO_LOCAL_CMD_ADDR(SDIO_REG_HSUS_CTRL), 1, &sus_ctl);
	if (err)
		goto exit;

#if SDIO_CHK_HCI_SUSPEND_POLLING
	/* polling for HCI_RESUME_PWR_RDY cleared */
	start = rtw_get_current_time();
	while (1) {
		if (rtw_is_surprise_removed(adapter)) {
			sr = 1;
			break;
		}

		err = sd_cmd52_read(pintfhdl, SDIO_LOCAL_CMD_ADDR(SDIO_REG_HSUS_CTRL), 1, &sus_ctl);
		poll_cnt++;

		if (!err && !(sus_ctl & HCI_RESUME_PWR_RDY))
			break;

		if (rtw_get_passing_time_ms(start) > SDIO_HCI_RESUME_PWR_RDY_TIMEOUT_MS) {
			timeout = 1;
			break;
		}
	}
	end = rtw_get_current_time();
#endif /* SDIO_CHK_HCI_SUSPEND_POLLING */

exit:

	if (DBG_SDIO_CHK_HCI_RESUME) {
		RTW_INFO(FUNC_ADPT_FMT" hci_sus_state:%u, sus_ctl:0x%02x(0x%02x), to:%u, err:%u\n"
			, FUNC_ADPT_ARG(adapter), hci_sus_state, sus_ctl, sus_ctl_ori, timeout, err);
		if (start != 0 || end != 0) {
			RTW_INFO(FUNC_ADPT_FMT" polling %d ms, cnt:%u\n"
				, FUNC_ADPT_ARG(adapter), rtw_get_time_interval_ms(start, end), poll_cnt);
		}
	}

#if SDIO_CHK_HCI_SUSPEND_POLLING
	if (timeout) {
		RTW_ERR(FUNC_ADPT_FMT" timeout(err:%d) sus_ctl:0x%02x\n"
			, FUNC_ADPT_ARG(adapter), err, sus_ctl);
	} else
#endif
		if (err) {
			RTW_ERR(FUNC_ADPT_FMT" err:%d\n"
				, FUNC_ADPT_ARG(adapter), err);
		}

no_hdl:
	return;
}
#endif /* CONFIG_SDIO_CHK_HCI_RESUME */


#ifdef CONFIG_SDIO_INDIRECT_ACCESS

/* program indirect access register in sdio local to read/write page0 registers */
#ifndef INDIRECT_ACCESS_TIMEOUT_MS
	#define INDIRECT_ACCESS_TIMEOUT_MS 200
#endif
#ifndef DBG_SDIO_INDIRECT_ACCESS
	#define DBG_SDIO_INDIRECT_ACCESS 0
#endif

s32 sdio_iread(PADAPTER padapter, u32 addr, u8 size, u8 *v)
{
	struct intf_hdl *pintfhdl = &padapter->iopriv.intf;
	_mutex *mutex = &adapter_to_dvobj(padapter)->sd_indirect_access_mutex;

	u8 val[4] = {0};
	u8 cmd[4] = {0}; /* mapping to indirect access register, little endien */
	systime start = 0, end = 0;
	u8 timeout = 0;
	u8 sr = 0;
	s32 err = 0;

	if (size == 1)
		SET_INDIRECT_REG_SIZE_1BYTE(cmd);
	else if (size == 2)
		SET_INDIRECT_REG_SIZE_2BYTE(cmd);
	else if (size == 4)
		SET_INDIRECT_REG_SIZE_4BYTE(cmd);

	SET_INDIRECT_REG_ADDR(cmd, addr);

	/* acquire indirect access lock */
	_enter_critical_mutex(mutex, NULL);

	if (DBG_SDIO_INDIRECT_ACCESS)
		RTW_INFO(FUNC_ADPT_FMT" cmd:%02x %02x %02x %02x\n", FUNC_ADPT_ARG(padapter), cmd[0], cmd[1], cmd[2], cmd[3]);

	err = sd_cmd52_write(pintfhdl, SDIO_LOCAL_CMD_ADDR(SDIO_REG_INDIRECT_REG_CFG), 3, cmd);
	if (err)
		goto exit;

	/* trigger */
	SET_INDIRECT_REG_READ(cmd);

	if (DBG_SDIO_INDIRECT_ACCESS)
		RTW_INFO(FUNC_ADPT_FMT" cmd:%02x %02x %02x %02x\n", FUNC_ADPT_ARG(padapter), cmd[0], cmd[1], cmd[2], cmd[3]);

	err = sd_cmd52_write(pintfhdl, SDIO_LOCAL_CMD_ADDR(SDIO_REG_INDIRECT_REG_CFG + 2), 1, cmd + 2);
	if (err)
		goto exit;

	/* polling for indirect access done */
	start = rtw_get_current_time();
	while (1) {
		if (rtw_is_surprise_removed(padapter)) {
			sr = 1;
			break;
		}

		err = sd_cmd52_read(pintfhdl, SDIO_LOCAL_CMD_ADDR(SDIO_REG_INDIRECT_REG_CFG + 2), 1, cmd + 2);

		if (!err && GET_INDIRECT_REG_RDY(cmd))
			break;

		if (rtw_get_passing_time_ms(start) > INDIRECT_ACCESS_TIMEOUT_MS) {
			timeout = 1;
			break;
		}
	}
	end = rtw_get_current_time();

	if (timeout || sr)
		goto exit;

	/* read result */
	err = sd_cmd52_read(pintfhdl, SDIO_LOCAL_CMD_ADDR(SDIO_REG_INDIRECT_REG_DATA), size, val);
	if (size == 2)
		*((u16 *)(val)) = le16_to_cpu(*((u16 *)(val)));
	else if (size == 4)
		*((u32 *)(val)) = le32_to_cpu(*((u32 *)(val)));

	if (DBG_SDIO_INDIRECT_ACCESS) {
		if (size == 1)
			RTW_INFO(FUNC_ADPT_FMT" val:0x%02x\n", FUNC_ADPT_ARG(padapter), *((u8 *)(val)));
		else if (size == 2)
			RTW_INFO(FUNC_ADPT_FMT" val:0x%04x\n", FUNC_ADPT_ARG(padapter), *((u16 *)(val)));
		else if (size == 4)
			RTW_INFO(FUNC_ADPT_FMT" val:0x%08x\n", FUNC_ADPT_ARG(padapter), *((u32 *)(val)));
	}

exit:
	/* release indirect access lock */
	_exit_critical_mutex(mutex, NULL);

	if (DBG_SDIO_INDIRECT_ACCESS) {
		RTW_INFO(FUNC_ADPT_FMT" addr:0x%0x size:%u, cmd:%02x %02x %02x %02x, to:%u, err:%u\n"
			, FUNC_ADPT_ARG(padapter), addr, size, cmd[0], cmd[1], cmd[2], cmd[3], timeout, err);
		if (start != 0 || end != 0) {
			RTW_INFO(FUNC_ADPT_FMT" polling %d ms\n"
				, FUNC_ADPT_ARG(padapter), rtw_get_time_interval_ms(start, end));
		}
	}

	if (timeout) {
		RTW_ERR(FUNC_ADPT_FMT" addr:0x%0x timeout(err:%d), cmd\n"
			, FUNC_ADPT_ARG(padapter), addr, err);
		if (!err)
			err = -1; /* just for return value */
	} else if (err) {
		RTW_ERR(FUNC_ADPT_FMT" addr:0x%0x err:%d\n"
			, FUNC_ADPT_ARG(padapter), addr, err);
	} else if (sr) {
		/* just for return value */
		err = -1;
	}

	if (!err && !timeout && !sr)
		_rtw_memcpy(v, val, size);

	return err;
}

s32 sdio_iwrite(PADAPTER padapter, u32 addr, u8 size, u8 *v)
{
	struct intf_hdl *pintfhdl = &padapter->iopriv.intf;
	_mutex *mutex = &adapter_to_dvobj(padapter)->sd_indirect_access_mutex;

	u8 val[4] = {0};
	u8 cmd[4] = {0}; /* mapping to indirect access register, little endien */
	systime start = 0, end = 0;
	u8 timeout = 0;
	u8 sr = 0;
	s32 err = 0;

	if (size == 1)
		SET_INDIRECT_REG_SIZE_1BYTE(cmd);
	else if (size == 2)
		SET_INDIRECT_REG_SIZE_2BYTE(cmd);
	else if (size == 4)
		SET_INDIRECT_REG_SIZE_4BYTE(cmd);

	SET_INDIRECT_REG_ADDR(cmd, addr);

	/* acquire indirect access lock */
	_enter_critical_mutex(mutex, NULL);

	if (DBG_SDIO_INDIRECT_ACCESS)
		RTW_INFO(FUNC_ADPT_FMT" cmd:%02x %02x %02x %02x\n", FUNC_ADPT_ARG(padapter), cmd[0], cmd[1], cmd[2], cmd[3]);

	err = sd_cmd52_write(pintfhdl, SDIO_LOCAL_CMD_ADDR(SDIO_REG_INDIRECT_REG_CFG), 3, cmd);
	if (err)
		goto exit;

	/* data to write */
	_rtw_memcpy(val, v, size);

	if (DBG_SDIO_INDIRECT_ACCESS) {
		if (size == 1)
			RTW_INFO(FUNC_ADPT_FMT" val:0x%02x\n", FUNC_ADPT_ARG(padapter), *((u8 *)(val)));
		else if (size == 2)
			RTW_INFO(FUNC_ADPT_FMT" val:0x%04x\n", FUNC_ADPT_ARG(padapter), *((u16 *)(val)));
		else if (size == 4)
			RTW_INFO(FUNC_ADPT_FMT" val:0x%08x\n", FUNC_ADPT_ARG(padapter), *((u32 *)(val)));
	}

	if (size == 2)
		*((u16 *)(val)) = cpu_to_le16(*((u16 *)(val)));
	else if (size == 4)
		*((u32 *)(val)) = cpu_to_le32(*((u32 *)(val)));

	err = sd_cmd52_write(pintfhdl, SDIO_LOCAL_CMD_ADDR(SDIO_REG_INDIRECT_REG_DATA), size, val);
	if (err)
		goto exit;

	/* trigger */
	SET_INDIRECT_REG_WRITE(cmd);

	if (DBG_SDIO_INDIRECT_ACCESS)
		RTW_INFO(FUNC_ADPT_FMT" cmd:%02x %02x %02x %02x\n", FUNC_ADPT_ARG(padapter), cmd[0], cmd[1], cmd[2], cmd[3]);

	err = sd_cmd52_write(pintfhdl, SDIO_LOCAL_CMD_ADDR(SDIO_REG_INDIRECT_REG_CFG + 2), 1, cmd + 2);
	if (err)
		goto exit;

	/* polling for indirect access done */
	start = rtw_get_current_time();
	while (1) {
		if (rtw_is_surprise_removed(padapter)) {
			sr = 1;
			break;
		}

		err = sd_cmd52_read(pintfhdl, SDIO_LOCAL_CMD_ADDR(SDIO_REG_INDIRECT_REG_CFG + 2), 1, cmd + 2);

		if (!err && GET_INDIRECT_REG_RDY(cmd))
			break;

		if (rtw_get_passing_time_ms(start) > INDIRECT_ACCESS_TIMEOUT_MS) {
			timeout = 1;
			break;
		}
	}
	end = rtw_get_current_time();

	if (timeout || sr)
		goto exit;

exit:
	/* release indirect access lock */
	_exit_critical_mutex(mutex, NULL);

	if (DBG_SDIO_INDIRECT_ACCESS) {
		RTW_INFO(FUNC_ADPT_FMT" addr:0x%0x size:%u, cmd:%02x %02x %02x %02x, to:%u, err:%u\n"
			, FUNC_ADPT_ARG(padapter), addr, size, cmd[0], cmd[1], cmd[2], cmd[3], timeout, err);
		if (start != 0 || end != 0) {
			RTW_INFO(FUNC_ADPT_FMT" polling %d ms\n"
				, FUNC_ADPT_ARG(padapter), rtw_get_time_interval_ms(start, end));
		}
	}

	if (timeout) {
		RTW_ERR(FUNC_ADPT_FMT" addr:0x%0x timeout(err:%d), cmd\n"
			, FUNC_ADPT_ARG(padapter), addr, err);
		if (!err)
			err = -1; /* just for return value */
	} else if (err) {
		RTW_ERR(FUNC_ADPT_FMT" addr:0x%0x err:%d\n"
			, FUNC_ADPT_ARG(padapter), addr, err);
	} else if (sr) {
		/* just for return value */
		err = -1;
	}

	return err;
}

u8 sdio_iread8(struct intf_hdl *pintfhdl, u32 addr)
{
	u8 val;

	if (sdio_iread(pintfhdl->padapter, addr, 1, (u8 *)&val) != 0)
		val = SDIO_ERR_VAL8;

	return val;
}

u16 sdio_iread16(struct intf_hdl *pintfhdl, u32 addr)
{
	u16 val;

	if (sdio_iread(pintfhdl->padapter, addr, 2, (u8 *)&val) != 0)
		val = SDIO_ERR_VAL16;

	return val;
}

u32 sdio_iread32(struct intf_hdl *pintfhdl, u32 addr)
{
	u32 val;

	if (sdio_iread(pintfhdl->padapter, addr, 4, (u8 *)&val) != 0)
		val = SDIO_ERR_VAL32;

	return val;
}

s32 sdio_iwrite8(struct intf_hdl *pintfhdl, u32 addr, u8 val)
{
	return sdio_iwrite(pintfhdl->padapter, addr, 1, (u8 *)&val);
}

s32 sdio_iwrite16(struct intf_hdl *pintfhdl, u32 addr, u16 val)
{
	return sdio_iwrite(pintfhdl->padapter, addr, 2, (u8 *)&val);
}

s32 sdio_iwrite32(struct intf_hdl *pintfhdl, u32 addr, u32 val)
{
	return sdio_iwrite(pintfhdl->padapter, addr, 4, (u8 *)&val);
}
#endif /* CONFIG_SDIO_INDIRECT_ACCESS */
u32 cmd53_4byte_alignment(struct intf_hdl *pintfhdl, u32 addr)
{
	u32 addr_rdr;
	u32 value;

	value = 0;
	addr_rdr = addr % 4;

	if (addr_rdr) {
		u8 shift_bit;

		shift_bit = addr_rdr * 8;
		value = (sd_read32(pintfhdl, (addr - addr_rdr), NULL)) >> shift_bit;
	} else
		value = sd_read32(pintfhdl, addr, NULL);
	
	return value;
}

#endif /* !RTW_HALMAC */

#ifndef CONFIG_SDIO_TX_TASKLET
#ifdef SDIO_FREE_XMIT_BUF_SEMA
void _rtw_sdio_free_xmitbuf_sema_up(struct xmit_priv *xmit)
{
	_rtw_up_sema(&xmit->sdio_free_xmitbuf_sema);
}

void _rtw_sdio_free_xmitbuf_sema_down(struct xmit_priv *xmit)
{
	_rtw_down_sema(&xmit->sdio_free_xmitbuf_sema);
}

#ifdef DBG_SDIO_FREE_XMIT_BUF_SEMA
void dbg_rtw_sdio_free_xmitbuf_sema_up(struct xmit_priv *xmit, const char *caller)
{
	/* just for debug usage only, pleae take care for the different of count implementaton */
	RTW_INFO("%s("ADPT_FMT") before up sdio_free_xmitbuf_sema, count:%u\n"
		, caller, ADPT_ARG(xmit->adapter), xmit->sdio_free_xmitbuf_sema.count);
	_rtw_sdio_free_xmitbuf_sema_up(xmit);
}

void dbg_rtw_sdio_free_xmitbuf_sema_down(struct xmit_priv *xmit, const char *caller)
{
	/* just for debug usage only, pleae take care for the different of count implementaton */
	RTW_INFO("%s("ADPT_FMT") before down sdio_free_xmitbuf_sema, count:%u\n"
		, caller, ADPT_ARG(xmit->adapter), xmit->sdio_free_xmitbuf_sema.count);
	_rtw_sdio_free_xmitbuf_sema_down(xmit);
}
#endif /* DBG_SDIO_FREE_XMIT_BUF_SEMA */

#endif /* SDIO_FREE_XMIT_BUF_SEMA */
#endif /* !CONFIG_SDIO_TX_TASKLET */

