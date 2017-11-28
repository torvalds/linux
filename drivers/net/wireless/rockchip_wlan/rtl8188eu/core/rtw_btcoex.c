/******************************************************************************
 *
 * Copyright(c) 2013 Realtek Corporation. All rights reserved.
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
#ifdef CONFIG_BT_COEXIST

#include <drv_types.h>
#include <hal_btcoex.h>
#include <hal_data.h>


void rtw_btcoex_Initialize(PADAPTER padapter)
{
	hal_btcoex_Initialize(padapter);
}

void rtw_btcoex_PowerOnSetting(PADAPTER padapter)
{
	hal_btcoex_PowerOnSetting(padapter);
}

void rtw_btcoex_PreLoadFirmware(PADAPTER padapter)
{
	hal_btcoex_PreLoadFirmware(padapter);
}

void rtw_btcoex_HAL_Initialize(PADAPTER padapter, u8 bWifiOnly)
{
	hal_btcoex_InitHwConfig(padapter, bWifiOnly);
}

void rtw_btcoex_IpsNotify(PADAPTER padapter, u8 type)
{
	PHAL_DATA_TYPE	pHalData;

	pHalData = GET_HAL_DATA(padapter);
	if (_FALSE == pHalData->EEPROMBluetoothCoexist)
		return;

	hal_btcoex_IpsNotify(padapter, type);
}

void rtw_btcoex_LpsNotify(PADAPTER padapter, u8 type)
{
	PHAL_DATA_TYPE	pHalData;

	pHalData = GET_HAL_DATA(padapter);
	if (_FALSE == pHalData->EEPROMBluetoothCoexist)
		return;

	hal_btcoex_LpsNotify(padapter, type);
}

void rtw_btcoex_ScanNotify(PADAPTER padapter, u8 type)
{
	PHAL_DATA_TYPE	pHalData;
#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
	struct bt_coex_info *pcoex_info = &padapter->coex_info;
	PBT_MGNT	pBtMgnt = &pcoex_info->BtMgnt;
#endif /* CONFIG_BT_COEXIST_SOCKET_TRX */

	pHalData = GET_HAL_DATA(padapter);
	if (_FALSE == pHalData->EEPROMBluetoothCoexist)
		return;

	if (_FALSE == type) {
		#ifdef CONFIG_CONCURRENT_MODE
		if (rtw_mi_buddy_check_fwstate(padapter, WIFI_SITE_MONITOR))
			return;
		#endif

		if (DEV_MGMT_TX_NUM(adapter_to_dvobj(padapter))
			|| DEV_ROCH_NUM(adapter_to_dvobj(padapter)))
			return;
	}

#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
	if (pBtMgnt->ExtConfig.bEnableWifiScanNotify)
		rtw_btcoex_SendScanNotify(padapter, type);
#endif /* CONFIG_BT_COEXIST_SOCKET_TRX	 */

	hal_btcoex_ScanNotify(padapter, type);
}

void rtw_btcoex_ConnectNotify(PADAPTER padapter, u8 action)
{
	PHAL_DATA_TYPE	pHalData;

	pHalData = GET_HAL_DATA(padapter);
	if (_FALSE == pHalData->EEPROMBluetoothCoexist)
		return;

#ifdef DBG_CONFIG_ERROR_RESET
	if (_TRUE == rtw_hal_sreset_inprogress(padapter)) {
		RTW_INFO(FUNC_ADPT_FMT ": [BTCoex] under reset, skip notify!\n",
			 FUNC_ADPT_ARG(padapter));
		return;
	}
#endif /* DBG_CONFIG_ERROR_RESET */

#ifdef CONFIG_CONCURRENT_MODE
	if (_FALSE == action) {
		if (rtw_mi_buddy_check_fwstate(padapter, WIFI_UNDER_LINKING))
			return;
	}
#endif

	hal_btcoex_ConnectNotify(padapter, action);
}

void rtw_btcoex_MediaStatusNotify(PADAPTER padapter, u8 mediaStatus)
{
	PHAL_DATA_TYPE	pHalData;

	pHalData = GET_HAL_DATA(padapter);
	if (_FALSE == pHalData->EEPROMBluetoothCoexist)
		return;

#ifdef DBG_CONFIG_ERROR_RESET
	if (_TRUE == rtw_hal_sreset_inprogress(padapter)) {
		RTW_INFO(FUNC_ADPT_FMT ": [BTCoex] under reset, skip notify!\n",
			 FUNC_ADPT_ARG(padapter));
		return;
	}
#endif /* DBG_CONFIG_ERROR_RESET */

#ifdef CONFIG_CONCURRENT_MODE
	if (RT_MEDIA_DISCONNECT == mediaStatus) {
		if (rtw_mi_buddy_check_fwstate(padapter, WIFI_ASOC_STATE))
			return;
	}
#endif /* CONFIG_CONCURRENT_MODE */

	if ((RT_MEDIA_CONNECT == mediaStatus)
	    && (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == _TRUE))
		rtw_hal_set_hwreg(padapter, HW_VAR_DL_RSVD_PAGE, NULL);

	hal_btcoex_MediaStatusNotify(padapter, mediaStatus);
}

void rtw_btcoex_SpecialPacketNotify(PADAPTER padapter, u8 pktType)
{
	PHAL_DATA_TYPE	pHalData;

	pHalData = GET_HAL_DATA(padapter);
	if (_FALSE == pHalData->EEPROMBluetoothCoexist)
		return;

	hal_btcoex_SpecialPacketNotify(padapter, pktType);
}

void rtw_btcoex_IQKNotify(PADAPTER padapter, u8 state)
{
	PHAL_DATA_TYPE	pHalData;

	pHalData = GET_HAL_DATA(padapter);
	if (_FALSE == pHalData->EEPROMBluetoothCoexist)
		return;

	hal_btcoex_IQKNotify(padapter, state);
}

void rtw_btcoex_BtInfoNotify(PADAPTER padapter, u8 length, u8 *tmpBuf)
{
	PHAL_DATA_TYPE	pHalData;

	pHalData = GET_HAL_DATA(padapter);
	if (_FALSE == pHalData->EEPROMBluetoothCoexist)
		return;

	hal_btcoex_BtInfoNotify(padapter, length, tmpBuf);
}

void rtw_btcoex_BtMpRptNotify(PADAPTER padapter, u8 length, u8 *tmpBuf)
{
	PHAL_DATA_TYPE	pHalData;

	pHalData = GET_HAL_DATA(padapter);
	if (_FALSE == pHalData->EEPROMBluetoothCoexist)
		return;

	if (padapter->registrypriv.mp_mode == 1)
		return;

	hal_btcoex_BtMpRptNotify(padapter, length, tmpBuf);
}

void rtw_btcoex_SuspendNotify(PADAPTER padapter, u8 state)
{
	PHAL_DATA_TYPE	pHalData;

	pHalData = GET_HAL_DATA(padapter);
	if (_FALSE == pHalData->EEPROMBluetoothCoexist)
		return;

	hal_btcoex_SuspendNotify(padapter, state);
}

void rtw_btcoex_HaltNotify(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData;
	u8 do_halt = 1;

	pHalData = GET_HAL_DATA(padapter);
	if (_FALSE == pHalData->EEPROMBluetoothCoexist)
		do_halt = 0;

	if (_FALSE == padapter->bup) {
		RTW_INFO(FUNC_ADPT_FMT ": bup=%d Skip!\n",
			 FUNC_ADPT_ARG(padapter), padapter->bup);
		do_halt = 0;
	}

	if (rtw_is_surprise_removed(padapter)) {
		RTW_INFO(FUNC_ADPT_FMT ": bSurpriseRemoved=%s Skip!\n",
			FUNC_ADPT_ARG(padapter), rtw_is_surprise_removed(padapter) ? "True" : "False");
		do_halt = 0;
	}

	hal_btcoex_HaltNotify(padapter, do_halt);
}

void rtw_btcoex_switchband_notify(u8 under_scan, u8 band_type)
{
	hal_btcoex_switchband_notify(under_scan, band_type);
}

void rtw_btcoex_SwitchBtTRxMask(PADAPTER padapter)
{
	hal_btcoex_SwitchBtTRxMask(padapter);
}

void rtw_btcoex_Switch(PADAPTER padapter, u8 enable)
{
	hal_btcoex_SetBTCoexist(padapter, enable);
}

u8 rtw_btcoex_IsBtDisabled(PADAPTER padapter)
{
	return hal_btcoex_IsBtDisabled(padapter);
}

void rtw_btcoex_Handler(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData;

	pHalData = GET_HAL_DATA(padapter);

	if (_FALSE == pHalData->EEPROMBluetoothCoexist)
		return;

	hal_btcoex_Hanlder(padapter);
}

s32 rtw_btcoex_IsBTCoexRejectAMPDU(PADAPTER padapter)
{
	s32 coexctrl;

	coexctrl = hal_btcoex_IsBTCoexRejectAMPDU(padapter);

	return coexctrl;
}

s32 rtw_btcoex_IsBTCoexCtrlAMPDUSize(PADAPTER padapter)
{
	s32 coexctrl;

	coexctrl = hal_btcoex_IsBTCoexCtrlAMPDUSize(padapter);

	return coexctrl;
}

u32 rtw_btcoex_GetAMPDUSize(PADAPTER padapter)
{
	u32 size;

	size = hal_btcoex_GetAMPDUSize(padapter);

	return size;
}

void rtw_btcoex_SetManualControl(PADAPTER padapter, u8 manual)
{
	if (_TRUE == manual)
		hal_btcoex_SetManualControl(padapter, _TRUE);
	else
		hal_btcoex_SetManualControl(padapter, _FALSE);
}

u8 rtw_btcoex_1Ant(PADAPTER padapter)
{
	return hal_btcoex_1Ant(padapter);
}

u8 rtw_btcoex_IsBtControlLps(PADAPTER padapter)
{
	return hal_btcoex_IsBtControlLps(padapter);
}

u8 rtw_btcoex_IsLpsOn(PADAPTER padapter)
{
	return hal_btcoex_IsLpsOn(padapter);
}

u8 rtw_btcoex_RpwmVal(PADAPTER padapter)
{
	return hal_btcoex_RpwmVal(padapter);
}

u8 rtw_btcoex_LpsVal(PADAPTER padapter)
{
	return hal_btcoex_LpsVal(padapter);
}

u32 rtw_btcoex_GetRaMask(PADAPTER padapter)
{
	return hal_btcoex_GetRaMask(padapter);
}

void rtw_btcoex_RecordPwrMode(PADAPTER padapter, u8 *pCmdBuf, u8 cmdLen)
{
	hal_btcoex_RecordPwrMode(padapter, pCmdBuf, cmdLen);
}

void rtw_btcoex_DisplayBtCoexInfo(PADAPTER padapter, u8 *pbuf, u32 bufsize)
{
	hal_btcoex_DisplayBtCoexInfo(padapter, pbuf, bufsize);
}

void rtw_btcoex_SetDBG(PADAPTER padapter, u32 *pDbgModule)
{
	hal_btcoex_SetDBG(padapter, pDbgModule);
}

u32 rtw_btcoex_GetDBG(PADAPTER padapter, u8 *pStrBuf, u32 bufSize)
{
	return hal_btcoex_GetDBG(padapter, pStrBuf, bufSize);
}

u8 rtw_btcoex_IncreaseScanDeviceNum(PADAPTER padapter)
{
	return hal_btcoex_IncreaseScanDeviceNum(padapter);
}

u8 rtw_btcoex_IsBtLinkExist(PADAPTER padapter)
{
	return hal_btcoex_IsBtLinkExist(padapter);
}

void rtw_btcoex_SetBtPatchVersion(PADAPTER padapter, u16 btHciVer, u16 btPatchVer)
{
	hal_btcoex_SetBtPatchVersion(padapter, btHciVer, btPatchVer);
}

void rtw_btcoex_SetHciVersion(PADAPTER  padapter, u16 hciVersion)
{
	hal_btcoex_SetHciVersion(padapter, hciVersion);
}

void rtw_btcoex_StackUpdateProfileInfo(void)
{
	hal_btcoex_StackUpdateProfileInfo();
}

void rtw_btcoex_pta_off_on_notify(PADAPTER padapter, u8 bBTON)
{
	hal_btcoex_pta_off_on_notify(padapter, bBTON);
}

/* ==================================================
 * Below Functions are called by BT-Coex
 * ================================================== */
void rtw_btcoex_rx_ampdu_apply(PADAPTER padapter)
{
	rtw_rx_ampdu_apply(padapter);
}

void rtw_btcoex_LPS_Enter(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrpriv;
	u8 lpsVal;


	pwrpriv = adapter_to_pwrctl(padapter);

	pwrpriv->bpower_saving = _TRUE;
	lpsVal = rtw_btcoex_LpsVal(padapter);
	rtw_set_ps_mode(padapter, PS_MODE_MIN, 0, lpsVal, "BTCOEX");
}

void rtw_btcoex_LPS_Leave(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrpriv;


	pwrpriv = adapter_to_pwrctl(padapter);

	if (pwrpriv->pwr_mode != PS_MODE_ACTIVE) {
		rtw_set_ps_mode(padapter, PS_MODE_ACTIVE, 0, 0, "BTCOEX");
		LPS_RF_ON_check(padapter, 100);
		pwrpriv->bpower_saving = _FALSE;
	}
}

u16 rtw_btcoex_btreg_read(PADAPTER padapter, u8 type, u16 addr, u32 *data)
{
	return hal_btcoex_btreg_read(padapter, type, addr, data);
}

u16 rtw_btcoex_btreg_write(PADAPTER padapter, u8 type, u16 addr, u16 val)
{
	return hal_btcoex_btreg_write(padapter, type, addr, val);
}

u8 rtw_btcoex_get_bt_coexist(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	return pHalData->EEPROMBluetoothCoexist;
}

u8 rtw_btcoex_get_chip_type(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	return pHalData->EEPROMBluetoothType;
}

u8 rtw_btcoex_get_pg_ant_num(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	return pHalData->EEPROMBluetoothAntNum == Ant_x2 ? 2 : 1;
}

u8 rtw_btcoex_get_pg_single_ant_path(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	return pHalData->ant_path;
}

u8 rtw_btcoex_get_pg_rfe_type(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	return pHalData->rfe_type;
}

u8 rtw_btcoex_is_tfbga_package_type(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

#ifdef CONFIG_RTL8723B
	if ((pHalData->PackageType == PACKAGE_TFBGA79) || (pHalData->PackageType == PACKAGE_TFBGA80)
	    || (pHalData->PackageType == PACKAGE_TFBGA90))
		return _TRUE;
#endif

	return _FALSE;
}

u8 rtw_btcoex_get_ant_div_cfg(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;

	pHalData = GET_HAL_DATA(padapter);
	
	return (pHalData->AntDivCfg == 0) ? _FALSE : _TRUE;
}

/* ==================================================
 * Below Functions are BT-Coex socket related function
 * ================================================== */

#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
_adapter *pbtcoexadapter; /* = NULL; */ /* do not initialise globals to 0 or NULL */
u8 rtw_btcoex_btinfo_cmd(_adapter *adapter, u8 *buf, u16 len)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	u8 *btinfo;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	u8	res = _SUCCESS;

	ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		rtw_mfree((u8 *)ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	btinfo = rtw_zmalloc(len);
	if (btinfo == NULL) {
		rtw_mfree((u8 *)ph2c, sizeof(struct cmd_obj));
		rtw_mfree((u8 *)pdrvextra_cmd_parm, sizeof(struct drvextra_cmd_parm));
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = BTINFO_WK_CID;
	pdrvextra_cmd_parm->type = 0;
	pdrvextra_cmd_parm->size = len;
	pdrvextra_cmd_parm->pbuf = btinfo;

	_rtw_memcpy(btinfo, buf, len);

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:
	return res;
}

u8 rtw_btcoex_send_event_to_BT(_adapter *padapter, u8 status,  u8 event_code, u8 opcode_low, u8 opcode_high, u8 *dbg_msg)
{
	u8 localBuf[6] = "";
	u8 *pRetPar;
	u8	len = 0, tx_event_length = 0;
	rtw_HCI_event *pEvent;

	pEvent = (rtw_HCI_event *)(&localBuf[0]);

	pEvent->EventCode = event_code;
	pEvent->Data[0] = 0x1;	/* packet # */
	pEvent->Data[1] = opcode_low;
	pEvent->Data[2] = opcode_high;
	len = len + 3;

	/* Return parameters starts from here */
	pRetPar = &pEvent->Data[len];
	pRetPar[0] = status;		/* status */

	len++;
	pEvent->Length = len;

	/* total tx event length + EventCode length + sizeof(length) */
	tx_event_length = pEvent->Length + 2;
#if 0
	rtw_btcoex_dump_tx_msg((u8 *)pEvent, tx_event_length, dbg_msg);
#endif
	status = rtw_btcoex_sendmsgbysocket(padapter, (u8 *)pEvent, tx_event_length, _FALSE);

	return status;
}

/*
Ref:
Realtek Wi-Fi Driver
Host Controller Interface for
Bluetooth 3.0 + HS V1.4 2013/02/07

Window team code & BT team code
 */


u8 rtw_btcoex_parse_BT_info_notify_cmd(_adapter *padapter, u8 *pcmd, u16 cmdlen)
{
#define BT_INFO_LENGTH 8

	u8 curPollEnable = pcmd[0];
	u8 curPollTime = pcmd[1];
	u8 btInfoReason = pcmd[2];
	u8 btInfoLen = pcmd[3];
	u8 btinfo[BT_INFO_LENGTH];

	u8 localBuf[6] = "";
	u8 *pRetPar;
	u8	len = 0, tx_event_length = 0;
	RTW_HCI_STATUS status = HCI_STATUS_SUCCESS;
	rtw_HCI_event *pEvent;

	/* RTW_INFO("%s\n",__func__);
	RTW_INFO("current Poll Enable: %d, currrent Poll Time: %d\n",curPollEnable,curPollTime);
	RTW_INFO("BT Info reason: %d, BT Info length: %d\n",btInfoReason,btInfoLen);
	RTW_INFO("%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n"
		,pcmd[4],pcmd[5],pcmd[6],pcmd[7],pcmd[8],pcmd[9],pcmd[10],pcmd[11]);*/

	_rtw_memset(btinfo, 0, BT_INFO_LENGTH);

#if 1
	if (BT_INFO_LENGTH != btInfoLen) {
		status = HCI_STATUS_INVALID_HCI_CMD_PARA_VALUE;
		RTW_INFO("Error BT Info Length: %d\n", btInfoLen);
		/* return _FAIL; */
	} else
#endif
	{
		if (0x1 == btInfoReason || 0x2 == btInfoReason) {
			_rtw_memcpy(btinfo, &pcmd[4], btInfoLen);
			btinfo[0] = btInfoReason;
			rtw_btcoex_btinfo_cmd(padapter, btinfo, btInfoLen);
		} else
			RTW_INFO("Other BT info reason\n");
	}

	/* send complete event to BT */
	{

		pEvent = (rtw_HCI_event *)(&localBuf[0]);

		pEvent->EventCode = HCI_EVENT_COMMAND_COMPLETE;
		pEvent->Data[0] = 0x1;	/* packet # */
		pEvent->Data[1] = HCIOPCODELOW(HCI_BT_INFO_NOTIFY, OGF_EXTENSION);
		pEvent->Data[2] = HCIOPCODEHIGHT(HCI_BT_INFO_NOTIFY, OGF_EXTENSION);
		len = len + 3;

		/* Return parameters starts from here */
		pRetPar = &pEvent->Data[len];
		pRetPar[0] = status;		/* status */

		len++;
		pEvent->Length = len;

		/* total tx event length + EventCode length + sizeof(length) */
		tx_event_length = pEvent->Length + 2;
#if 0
		rtw_btcoex_dump_tx_msg((u8 *)pEvent, tx_event_length, "BT_info_event");
#endif
		status = rtw_btcoex_sendmsgbysocket(padapter, (u8 *)pEvent, tx_event_length, _FALSE);

		return status;
		/* bthci_IndicateEvent(Adapter, PPacketIrpEvent, len+2); */
	}
}

u8 rtw_btcoex_parse_BT_patch_ver_info_cmd(_adapter *padapter, u8 *pcmd, u16 cmdlen)
{
	RTW_HCI_STATUS status = HCI_STATUS_SUCCESS;
	u16		btPatchVer = 0x0, btHciVer = 0x0;
	/* u16		*pU2tmp; */

	u8 localBuf[6] = "";
	u8 *pRetPar;
	u8	len = 0, tx_event_length = 0;
	rtw_HCI_event *pEvent;

	btHciVer = pcmd[0] | pcmd[1] << 8;
	btPatchVer = pcmd[2] | pcmd[3] << 8;


	RTW_INFO("%s, cmd:%02x %02x %02x %02x\n", __func__, pcmd[0] , pcmd[1] , pcmd[2] , pcmd[3]);
	RTW_INFO("%s, HCI Ver:%d, Patch Ver:%d\n", __func__, btHciVer, btPatchVer);

	rtw_btcoex_SetBtPatchVersion(padapter, btHciVer, btPatchVer);


	/* send complete event to BT */
	{
		pEvent = (rtw_HCI_event *)(&localBuf[0]);


		pEvent->EventCode = HCI_EVENT_COMMAND_COMPLETE;
		pEvent->Data[0] = 0x1;	/* packet # */
		pEvent->Data[1] = HCIOPCODELOW(HCI_BT_PATCH_VERSION_NOTIFY, OGF_EXTENSION);
		pEvent->Data[2] = HCIOPCODEHIGHT(HCI_BT_PATCH_VERSION_NOTIFY, OGF_EXTENSION);
		len = len + 3;

		/* Return parameters starts from here */
		pRetPar = &pEvent->Data[len];
		pRetPar[0] = status;		/* status */

		len++;
		pEvent->Length = len;

		/* total tx event length + EventCode length + sizeof(length) */
		tx_event_length = pEvent->Length + 2;
#if 0
		rtw_btcoex_dump_tx_msg((u8 *)pEvent, tx_event_length, "BT_patch_event");
#endif
		status = rtw_btcoex_sendmsgbysocket(padapter, (u8 *)pEvent, tx_event_length, _FALSE);
		return status;
		/* bthci_IndicateEvent(Adapter, PPacketIrpEvent, len+2); */
	}
}

u8 rtw_btcoex_parse_HCI_Ver_notify_cmd(_adapter *padapter, u8 *pcmd, u16 cmdlen)
{
	RTW_HCI_STATUS status = HCI_STATUS_SUCCESS;
	u16 hciver = pcmd[0] | pcmd[1] << 8;

	u8 localBuf[6] = "";
	u8 *pRetPar;
	u8	len = 0, tx_event_length = 0;
	rtw_HCI_event *pEvent;

	struct bt_coex_info *pcoex_info = &padapter->coex_info;
	PBT_MGNT	pBtMgnt = &pcoex_info->BtMgnt;
	pBtMgnt->ExtConfig.HCIExtensionVer = hciver;
	RTW_INFO("%s, HCI Version: %d\n", __func__, pBtMgnt->ExtConfig.HCIExtensionVer);
	if (pBtMgnt->ExtConfig.HCIExtensionVer  < 4) {
		status = HCI_STATUS_INVALID_HCI_CMD_PARA_VALUE;
		RTW_INFO("%s, Version = %d, HCI Version < 4\n", __func__, pBtMgnt->ExtConfig.HCIExtensionVer);
	} else
		rtw_btcoex_SetHciVersion(padapter, hciver);
	/* send complete event to BT */
	{
		pEvent = (rtw_HCI_event *)(&localBuf[0]);


		pEvent->EventCode = HCI_EVENT_COMMAND_COMPLETE;
		pEvent->Data[0] = 0x1;	/* packet # */
		pEvent->Data[1] = HCIOPCODELOW(HCI_EXTENSION_VERSION_NOTIFY, OGF_EXTENSION);
		pEvent->Data[2] = HCIOPCODEHIGHT(HCI_EXTENSION_VERSION_NOTIFY, OGF_EXTENSION);
		len = len + 3;

		/* Return parameters starts from here */
		pRetPar = &pEvent->Data[len];
		pRetPar[0] = status;		/* status */

		len++;
		pEvent->Length = len;

		/* total tx event length + EventCode length + sizeof(length) */
		tx_event_length = pEvent->Length + 2;

		status = rtw_btcoex_sendmsgbysocket(padapter, (u8 *)pEvent, tx_event_length, _FALSE);
		return status;
		/* bthci_IndicateEvent(Adapter, PPacketIrpEvent, len+2); */
	}

}

u8 rtw_btcoex_parse_WIFI_scan_notify_cmd(_adapter *padapter, u8 *pcmd, u16 cmdlen)
{
	RTW_HCI_STATUS status = HCI_STATUS_SUCCESS;

	u8 localBuf[6] = "";
	u8 *pRetPar;
	u8	len = 0, tx_event_length = 0;
	rtw_HCI_event *pEvent;

	struct bt_coex_info *pcoex_info = &padapter->coex_info;
	PBT_MGNT	pBtMgnt = &pcoex_info->BtMgnt;
	pBtMgnt->ExtConfig.bEnableWifiScanNotify = pcmd[0];
	RTW_INFO("%s, bEnableWifiScanNotify: %d\n", __func__, pBtMgnt->ExtConfig.bEnableWifiScanNotify);

	/* send complete event to BT */
	{
		pEvent = (rtw_HCI_event *)(&localBuf[0]);


		pEvent->EventCode = HCI_EVENT_COMMAND_COMPLETE;
		pEvent->Data[0] = 0x1;	/* packet # */
		pEvent->Data[1] = HCIOPCODELOW(HCI_ENABLE_WIFI_SCAN_NOTIFY, OGF_EXTENSION);
		pEvent->Data[2] = HCIOPCODEHIGHT(HCI_ENABLE_WIFI_SCAN_NOTIFY, OGF_EXTENSION);
		len = len + 3;

		/* Return parameters starts from here */
		pRetPar = &pEvent->Data[len];
		pRetPar[0] = status;		/* status */

		len++;
		pEvent->Length = len;

		/* total tx event length + EventCode length + sizeof(length) */
		tx_event_length = pEvent->Length + 2;

		status = rtw_btcoex_sendmsgbysocket(padapter, (u8 *)pEvent, tx_event_length, _FALSE);
		return status;
		/* bthci_IndicateEvent(Adapter, PPacketIrpEvent, len+2); */
	}
}

u8 rtw_btcoex_parse_HCI_link_status_notify_cmd(_adapter *padapter, u8 *pcmd, u16 cmdlen)
{
	RTW_HCI_STATUS	status = HCI_STATUS_SUCCESS;
	struct bt_coex_info	*pcoex_info = &padapter->coex_info;
	PBT_MGNT	pBtMgnt = &pcoex_info->BtMgnt;
	/* PBT_DBG		pBtDbg=&padapter->MgntInfo.BtInfo.BtDbg; */
	u8		i, numOfHandle = 0, numOfAcl = 0;
	u16		conHandle;
	u8		btProfile, btCoreSpec, linkRole;
	u8		*pTriple;

	u8 localBuf[6] = "";
	u8 *pRetPar;
	u8	len = 0, tx_event_length = 0;
	rtw_HCI_event *pEvent;

	/* pBtDbg->dbgHciInfo.hciCmdCntLinkStatusNotify++; */
	/* RT_DISP_DATA(FIOCTL, IOCTL_BT_HCICMD_EXT, "LinkStatusNotify, Hex Data :\n",  */
	/*		&pHciCmd->Data[0], pHciCmd->Length); */

	RTW_INFO("BTLinkStatusNotify\n");

	/* Current only RTL8723 support this command. */
	/* pBtMgnt->bSupportProfile = TRUE; */
	pBtMgnt->bSupportProfile = _FALSE;

	pBtMgnt->ExtConfig.NumberOfACL = 0;
	pBtMgnt->ExtConfig.NumberOfSCO = 0;

	numOfHandle = pcmd[0];
	/* RT_DISP(FIOCTL, IOCTL_BT_HCICMD_EXT, ("numOfHandle = 0x%x\n", numOfHandle)); */
	/* RT_DISP(FIOCTL, IOCTL_BT_HCICMD_EXT, ("HCIExtensionVer = %d\n", pBtMgnt->ExtConfig.HCIExtensionVer)); */
	RTW_INFO("numOfHandle = 0x%x\n", numOfHandle);
	RTW_INFO("HCIExtensionVer = %d\n", pBtMgnt->ExtConfig.HCIExtensionVer);

	pTriple = &pcmd[1];
	for (i = 0; i < numOfHandle; i++) {
		if (pBtMgnt->ExtConfig.HCIExtensionVer < 1) {
			conHandle = *((u8 *)&pTriple[0]);
			btProfile = pTriple[2];
			btCoreSpec = pTriple[3];
			if (BT_PROFILE_SCO == btProfile)
				pBtMgnt->ExtConfig.NumberOfSCO++;
			else {
				pBtMgnt->ExtConfig.NumberOfACL++;
				pBtMgnt->ExtConfig.aclLink[i].ConnectHandle = conHandle;
				pBtMgnt->ExtConfig.aclLink[i].BTProfile = btProfile;
				pBtMgnt->ExtConfig.aclLink[i].BTCoreSpec = btCoreSpec;
			}
			/* RT_DISP(FIOCTL, IOCTL_BT_HCICMD_EXT, */
			/*	("Connection_Handle=0x%x, BTProfile=%d, BTSpec=%d\n", */
			/*		conHandle, btProfile, btCoreSpec)); */
			RTW_INFO("Connection_Handle=0x%x, BTProfile=%d, BTSpec=%d\n", conHandle, btProfile, btCoreSpec);
			pTriple += 4;
		} else if (pBtMgnt->ExtConfig.HCIExtensionVer >= 1) {
			conHandle = *((pu2Byte)&pTriple[0]);
			btProfile = pTriple[2];
			btCoreSpec = pTriple[3];
			linkRole = pTriple[4];
			if (BT_PROFILE_SCO == btProfile)
				pBtMgnt->ExtConfig.NumberOfSCO++;
			else {
				pBtMgnt->ExtConfig.NumberOfACL++;
				pBtMgnt->ExtConfig.aclLink[i].ConnectHandle = conHandle;
				pBtMgnt->ExtConfig.aclLink[i].BTProfile = btProfile;
				pBtMgnt->ExtConfig.aclLink[i].BTCoreSpec = btCoreSpec;
				pBtMgnt->ExtConfig.aclLink[i].linkRole = linkRole;
			}
			/* RT_DISP(FIOCTL, IOCTL_BT_HCICMD_EXT, */
			RTW_INFO("Connection_Handle=0x%x, BTProfile=%d, BTSpec=%d, LinkRole=%d\n",
				 conHandle, btProfile, btCoreSpec, linkRole);
			pTriple += 5;
		}
	}
	rtw_btcoex_StackUpdateProfileInfo();

	/* send complete event to BT */
	{
		pEvent = (rtw_HCI_event *)(&localBuf[0]);


		pEvent->EventCode = HCI_EVENT_COMMAND_COMPLETE;
		pEvent->Data[0] = 0x1;	/* packet # */
		pEvent->Data[1] = HCIOPCODELOW(HCI_LINK_STATUS_NOTIFY, OGF_EXTENSION);
		pEvent->Data[2] = HCIOPCODEHIGHT(HCI_LINK_STATUS_NOTIFY, OGF_EXTENSION);
		len = len + 3;

		/* Return parameters starts from here */
		pRetPar = &pEvent->Data[len];
		pRetPar[0] = status;		/* status */

		len++;
		pEvent->Length = len;

		/* total tx event length + EventCode length + sizeof(length) */
		tx_event_length = pEvent->Length + 2;

		status = rtw_btcoex_sendmsgbysocket(padapter, (u8 *)pEvent, tx_event_length, _FALSE);
		return status;
		/* bthci_IndicateEvent(Adapter, PPacketIrpEvent, len+2); */
	}


}

u8 rtw_btcoex_parse_HCI_BT_coex_notify_cmd(_adapter *padapter, u8 *pcmd, u16 cmdlen)
{
	u8 localBuf[6] = "";
	u8 *pRetPar;
	u8	len = 0, tx_event_length = 0;
	rtw_HCI_event *pEvent;
	RTW_HCI_STATUS	status = HCI_STATUS_SUCCESS;

	{
		pEvent = (rtw_HCI_event *)(&localBuf[0]);


		pEvent->EventCode = HCI_EVENT_COMMAND_COMPLETE;
		pEvent->Data[0] = 0x1;	/* packet # */
		pEvent->Data[1] = HCIOPCODELOW(HCI_BT_COEX_NOTIFY, OGF_EXTENSION);
		pEvent->Data[2] = HCIOPCODEHIGHT(HCI_BT_COEX_NOTIFY, OGF_EXTENSION);
		len = len + 3;

		/* Return parameters starts from here */
		pRetPar = &pEvent->Data[len];
		pRetPar[0] = status;		/* status */

		len++;
		pEvent->Length = len;

		/* total tx event length + EventCode length + sizeof(length) */
		tx_event_length = pEvent->Length + 2;

		status = rtw_btcoex_sendmsgbysocket(padapter, (u8 *)pEvent, tx_event_length, _FALSE);
		return status;
		/* bthci_IndicateEvent(Adapter, PPacketIrpEvent, len+2); */
	}
}

u8 rtw_btcoex_parse_HCI_BT_operation_notify_cmd(_adapter *padapter, u8 *pcmd, u16 cmdlen)
{
	u8 localBuf[6] = "";
	u8 *pRetPar;
	u8	len = 0, tx_event_length = 0;
	rtw_HCI_event *pEvent;
	RTW_HCI_STATUS	status = HCI_STATUS_SUCCESS;

	RTW_INFO("%s, OP code: %d\n", __func__, pcmd[0]);

	switch (pcmd[0]) {
	case HCI_BT_OP_NONE:
		RTW_INFO("[bt operation] : Operation None!!\n");
		break;
	case HCI_BT_OP_INQUIRY_START:
		RTW_INFO("[bt operation] : Inquiry start!!\n");
		break;
	case HCI_BT_OP_INQUIRY_FINISH:
		RTW_INFO("[bt operation] : Inquiry finished!!\n");
		break;
	case HCI_BT_OP_PAGING_START:
		RTW_INFO("[bt operation] : Paging is started!!\n");
		break;
	case HCI_BT_OP_PAGING_SUCCESS:
		RTW_INFO("[bt operation] : Paging complete successfully!!\n");
		break;
	case HCI_BT_OP_PAGING_UNSUCCESS:
		RTW_INFO("[bt operation] : Paging complete unsuccessfully!!\n");
		break;
	case HCI_BT_OP_PAIRING_START:
		RTW_INFO("[bt operation] : Pairing start!!\n");
		break;
	case HCI_BT_OP_PAIRING_FINISH:
		RTW_INFO("[bt operation] : Pairing finished!!\n");
		break;
	case HCI_BT_OP_BT_DEV_ENABLE:
		RTW_INFO("[bt operation] : BT Device is enabled!!\n");
		break;
	case HCI_BT_OP_BT_DEV_DISABLE:
		RTW_INFO("[bt operation] : BT Device is disabled!!\n");
		break;
	default:
		RTW_INFO("[bt operation] : Unknown, error!!\n");
		break;
	}

	/* send complete event to BT */
	{
		pEvent = (rtw_HCI_event *)(&localBuf[0]);


		pEvent->EventCode = HCI_EVENT_COMMAND_COMPLETE;
		pEvent->Data[0] = 0x1;	/* packet # */
		pEvent->Data[1] = HCIOPCODELOW(HCI_BT_OPERATION_NOTIFY, OGF_EXTENSION);
		pEvent->Data[2] = HCIOPCODEHIGHT(HCI_BT_OPERATION_NOTIFY, OGF_EXTENSION);
		len = len + 3;

		/* Return parameters starts from here */
		pRetPar = &pEvent->Data[len];
		pRetPar[0] = status;		/* status */

		len++;
		pEvent->Length = len;

		/* total tx event length + EventCode length + sizeof(length) */
		tx_event_length = pEvent->Length + 2;

		status = rtw_btcoex_sendmsgbysocket(padapter, (u8 *)pEvent, tx_event_length, _FALSE);
		return status;
		/* bthci_IndicateEvent(Adapter, PPacketIrpEvent, len+2); */
	}
}

u8 rtw_btcoex_parse_BT_AFH_MAP_notify_cmd(_adapter *padapter, u8 *pcmd, u16 cmdlen)
{
	u8 localBuf[6] = "";
	u8 *pRetPar;
	u8	len = 0, tx_event_length = 0;
	rtw_HCI_event *pEvent;
	RTW_HCI_STATUS	status = HCI_STATUS_SUCCESS;

	{
		pEvent = (rtw_HCI_event *)(&localBuf[0]);


		pEvent->EventCode = HCI_EVENT_COMMAND_COMPLETE;
		pEvent->Data[0] = 0x1;	/* packet # */
		pEvent->Data[1] = HCIOPCODELOW(HCI_BT_AFH_MAP_NOTIFY, OGF_EXTENSION);
		pEvent->Data[2] = HCIOPCODEHIGHT(HCI_BT_AFH_MAP_NOTIFY, OGF_EXTENSION);
		len = len + 3;

		/* Return parameters starts from here */
		pRetPar = &pEvent->Data[len];
		pRetPar[0] = status;		/* status */

		len++;
		pEvent->Length = len;

		/* total tx event length + EventCode length + sizeof(length) */
		tx_event_length = pEvent->Length + 2;

		status = rtw_btcoex_sendmsgbysocket(padapter, (u8 *)pEvent, tx_event_length, _FALSE);
		return status;
		/* bthci_IndicateEvent(Adapter, PPacketIrpEvent, len+2); */
	}
}

u8 rtw_btcoex_parse_BT_register_val_notify_cmd(_adapter *padapter, u8 *pcmd, u16 cmdlen)
{

	u8 localBuf[6] = "";
	u8 *pRetPar;
	u8	len = 0, tx_event_length = 0;
	rtw_HCI_event *pEvent;
	RTW_HCI_STATUS	status = HCI_STATUS_SUCCESS;

	{
		pEvent = (rtw_HCI_event *)(&localBuf[0]);


		pEvent->EventCode = HCI_EVENT_COMMAND_COMPLETE;
		pEvent->Data[0] = 0x1;	/* packet # */
		pEvent->Data[1] = HCIOPCODELOW(HCI_BT_REGISTER_VALUE_NOTIFY, OGF_EXTENSION);
		pEvent->Data[2] = HCIOPCODEHIGHT(HCI_BT_REGISTER_VALUE_NOTIFY, OGF_EXTENSION);
		len = len + 3;

		/* Return parameters starts from here */
		pRetPar = &pEvent->Data[len];
		pRetPar[0] = status;		/* status */

		len++;
		pEvent->Length = len;

		/* total tx event length + EventCode length + sizeof(length) */
		tx_event_length = pEvent->Length + 2;

		status = rtw_btcoex_sendmsgbysocket(padapter, (u8 *)pEvent, tx_event_length, _FALSE);
		return status;
		/* bthci_IndicateEvent(Adapter, PPacketIrpEvent, len+2); */
	}
}

u8 rtw_btcoex_parse_HCI_BT_abnormal_notify_cmd(_adapter *padapter, u8 *pcmd, u16 cmdlen)
{
	u8 localBuf[6] = "";
	u8 *pRetPar;
	u8	len = 0, tx_event_length = 0;
	rtw_HCI_event *pEvent;
	RTW_HCI_STATUS	status = HCI_STATUS_SUCCESS;

	{
		pEvent = (rtw_HCI_event *)(&localBuf[0]);


		pEvent->EventCode = HCI_EVENT_COMMAND_COMPLETE;
		pEvent->Data[0] = 0x1;	/* packet # */
		pEvent->Data[1] = HCIOPCODELOW(HCI_BT_ABNORMAL_NOTIFY, OGF_EXTENSION);
		pEvent->Data[2] = HCIOPCODEHIGHT(HCI_BT_ABNORMAL_NOTIFY, OGF_EXTENSION);
		len = len + 3;

		/* Return parameters starts from here */
		pRetPar = &pEvent->Data[len];
		pRetPar[0] = status;		/* status */

		len++;
		pEvent->Length = len;

		/* total tx event length + EventCode length + sizeof(length) */
		tx_event_length = pEvent->Length + 2;

		status = rtw_btcoex_sendmsgbysocket(padapter, (u8 *)pEvent, tx_event_length, _FALSE);
		return status;
		/* bthci_IndicateEvent(Adapter, PPacketIrpEvent, len+2); */
	}
}

u8 rtw_btcoex_parse_HCI_query_RF_status_cmd(_adapter *padapter, u8 *pcmd, u16 cmdlen)
{
	u8 localBuf[6] = "";
	u8 *pRetPar;
	u8	len = 0, tx_event_length = 0;
	rtw_HCI_event *pEvent;
	RTW_HCI_STATUS	status = HCI_STATUS_SUCCESS;

	{
		pEvent = (rtw_HCI_event *)(&localBuf[0]);


		pEvent->EventCode = HCI_EVENT_COMMAND_COMPLETE;
		pEvent->Data[0] = 0x1;	/* packet # */
		pEvent->Data[1] = HCIOPCODELOW(HCI_QUERY_RF_STATUS, OGF_EXTENSION);
		pEvent->Data[2] = HCIOPCODEHIGHT(HCI_QUERY_RF_STATUS, OGF_EXTENSION);
		len = len + 3;

		/* Return parameters starts from here */
		pRetPar = &pEvent->Data[len];
		pRetPar[0] = status;		/* status */

		len++;
		pEvent->Length = len;

		/* total tx event length + EventCode length + sizeof(length) */
		tx_event_length = pEvent->Length + 2;

		status = rtw_btcoex_sendmsgbysocket(padapter, (u8 *)pEvent, tx_event_length, _FALSE);
		return status;
		/* bthci_IndicateEvent(Adapter, PPacketIrpEvent, len+2); */
	}
}

/*****************************************
* HCI cmd format :
*| 15 - 0						|
*| OPcode (OCF|OGF<<10)		|
*| 15 - 8		|7 - 0			|
*|Cmd para	|Cmd para Length	|
*|Cmd para......				|
******************************************/

/* bit 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
 *	 |	OCF			             |	   OGF       | */
void rtw_btcoex_parse_hci_extend_cmd(_adapter *padapter, u8 *pcmd, u16 len, const u16 hci_OCF)
{

	RTW_INFO("%s: OCF: %x\n", __func__, hci_OCF);
	switch (hci_OCF) {
	case HCI_EXTENSION_VERSION_NOTIFY:
		RTW_INFO("HCI_EXTENSION_VERSION_NOTIFY\n");
		rtw_btcoex_parse_HCI_Ver_notify_cmd(padapter, pcmd, len);
		break;
	case HCI_LINK_STATUS_NOTIFY:
		RTW_INFO("HCI_LINK_STATUS_NOTIFY\n");
		rtw_btcoex_parse_HCI_link_status_notify_cmd(padapter, pcmd, len);
		break;
	case HCI_BT_OPERATION_NOTIFY:
		/* only for 8723a 2ant */
		RTW_INFO("HCI_BT_OPERATION_NOTIFY\n");
		rtw_btcoex_parse_HCI_BT_operation_notify_cmd(padapter, pcmd, len);
		/*  */
		break;
	case HCI_ENABLE_WIFI_SCAN_NOTIFY:
		RTW_INFO("HCI_ENABLE_WIFI_SCAN_NOTIFY\n");
		rtw_btcoex_parse_WIFI_scan_notify_cmd(padapter, pcmd, len);
		break;
	case HCI_QUERY_RF_STATUS:
		/* only for 8723b 2ant */
		RTW_INFO("HCI_QUERY_RF_STATUS\n");
		rtw_btcoex_parse_HCI_query_RF_status_cmd(padapter, pcmd, len);
		break;
	case HCI_BT_ABNORMAL_NOTIFY:
		RTW_INFO("HCI_BT_ABNORMAL_NOTIFY\n");
		rtw_btcoex_parse_HCI_BT_abnormal_notify_cmd(padapter, pcmd, len);
		break;
	case HCI_BT_INFO_NOTIFY:
		RTW_INFO("HCI_BT_INFO_NOTIFY\n");
		rtw_btcoex_parse_BT_info_notify_cmd(padapter, pcmd, len);
		break;
	case HCI_BT_COEX_NOTIFY:
		RTW_INFO("HCI_BT_COEX_NOTIFY\n");
		rtw_btcoex_parse_HCI_BT_coex_notify_cmd(padapter, pcmd, len);
		break;
	case HCI_BT_PATCH_VERSION_NOTIFY:
		RTW_INFO("HCI_BT_PATCH_VERSION_NOTIFY\n");
		rtw_btcoex_parse_BT_patch_ver_info_cmd(padapter, pcmd, len);
		break;
	case HCI_BT_AFH_MAP_NOTIFY:
		RTW_INFO("HCI_BT_AFH_MAP_NOTIFY\n");
		rtw_btcoex_parse_BT_AFH_MAP_notify_cmd(padapter, pcmd, len);
		break;
	case HCI_BT_REGISTER_VALUE_NOTIFY:
		RTW_INFO("HCI_BT_REGISTER_VALUE_NOTIFY\n");
		rtw_btcoex_parse_BT_register_val_notify_cmd(padapter, pcmd, len);
		break;
	default:
		RTW_INFO("ERROR!!! Unknown OCF: %x\n", hci_OCF);
		break;

	}
}

void rtw_btcoex_parse_hci_cmd(_adapter *padapter, u8 *pcmd, u16 len)
{
	u16 opcode = pcmd[0] | pcmd[1] << 8;
	u16 hci_OGF = HCI_OGF(opcode);
	u16 hci_OCF = HCI_OCF(opcode);
	u8 cmdlen = len - 3;
	u8 pare_len = pcmd[2];

	RTW_INFO("%s OGF: %x,OCF: %x\n", __func__, hci_OGF, hci_OCF);
	switch (hci_OGF) {
	case OGF_EXTENSION:
		RTW_INFO("HCI_EXTENSION_CMD_OGF\n");
		rtw_btcoex_parse_hci_extend_cmd(padapter, &pcmd[3], cmdlen, hci_OCF);
		break;
	default:
		RTW_INFO("Other OGF: %x\n", hci_OGF);
		break;
	}
}

u16 rtw_btcoex_parse_recv_data(u8 *msg, u8 msg_size)
{
	u8 cmp_msg1[32] = attend_ack;
	u8 cmp_msg2[32] = leave_ack;
	u8 cmp_msg3[32] = bt_leave;
	u8 cmp_msg4[32] = invite_req;
	u8 cmp_msg5[32] = attend_req;
	u8 cmp_msg6[32] = invite_rsp;
	u8 res = OTHER;

	if (_rtw_memcmp(cmp_msg1, msg, msg_size) == _TRUE) {
		/*RTW_INFO("%s, msg:%s\n",__func__,msg);*/
		res = RX_ATTEND_ACK;
	} else if (_rtw_memcmp(cmp_msg2, msg, msg_size) == _TRUE) {
		/*RTW_INFO("%s, msg:%s\n",__func__,msg);*/
		res = RX_LEAVE_ACK;
	} else if (_rtw_memcmp(cmp_msg3, msg, msg_size) == _TRUE) {
		/*RTW_INFO("%s, msg:%s\n",__func__,msg);*/
		res = RX_BT_LEAVE;
	} else if (_rtw_memcmp(cmp_msg4, msg, msg_size) == _TRUE) {
		/*RTW_INFO("%s, msg:%s\n",__func__,msg);*/
		res = RX_INVITE_REQ;
	} else if (_rtw_memcmp(cmp_msg5, msg, msg_size) == _TRUE)
		res = RX_ATTEND_REQ;
	else if (_rtw_memcmp(cmp_msg6, msg, msg_size) == _TRUE)
		res = RX_INVITE_RSP;
	else {
		/*RTW_INFO("%s, %s\n", __func__, msg);*/
		res = OTHER;
	}

	/*RTW_INFO("%s, res:%d\n", __func__, res);*/

	return res;
}

void rtw_btcoex_recvmsgbysocket(void *data)
{
	u8 recv_data[255];
	u8 tx_msg[255] = leave_ack;
	u32 len = 0;
	u16 recv_length = 0;
	u16 parse_res = 0;
#if 0
	u8 para_len = 0, polling_enable = 0, poling_interval = 0, reason = 0, btinfo_len = 0;
	u8 btinfo[BT_INFO_LEN] = {0};
#endif

	struct bt_coex_info *pcoex_info = NULL;
	struct sock *sk = NULL;
	struct sk_buff *skb = NULL;

	/*RTW_INFO("%s\n",__func__);*/

	if (pbtcoexadapter == NULL) {
		RTW_INFO("%s: btcoexadapter NULL!\n", __func__);
		return;
	}

	pcoex_info = &pbtcoexadapter->coex_info;
	sk = pcoex_info->sk_store;

	if (sk == NULL) {
		RTW_INFO("%s: critical error when receive socket data!\n", __func__);
		return;
	}

	len = skb_queue_len(&sk->sk_receive_queue);
	while (len > 0) {
		skb = skb_dequeue(&sk->sk_receive_queue);

		/*important: cut the udp header from skb->data! header length is 8 byte*/
		recv_length = skb->len - 8;
		_rtw_memset(recv_data, 0, sizeof(recv_data));
		_rtw_memcpy(recv_data, skb->data + 8, recv_length);

		parse_res = rtw_btcoex_parse_recv_data(recv_data, recv_length);
#if 0
		if (RX_ATTEND_ACK == parse_res) {
			/* attend ack */
			pcoex_info->BT_attend = _TRUE;
			RTW_INFO("RX_ATTEND_ACK!,sock_open:%d, BT_attend:%d\n", pcoex_info->sock_open, pcoex_info->BT_attend);
		} else if (RX_ATTEND_REQ == parse_res) {
			/* attend req from BT */
			pcoex_info->BT_attend = _TRUE;
			RTW_INFO("RX_BT_ATTEND_REQ!,sock_open:%d, BT_attend:%d\n", pcoex_info->sock_open, pcoex_info->BT_attend);
			rtw_btcoex_sendmsgbysocket(pbtcoexadapter, attend_ack, sizeof(attend_ack), _FALSE);
		} else if (RX_INVITE_REQ == parse_res) {
			/* invite req from BT */
			pcoex_info->BT_attend = _TRUE;
			RTW_INFO("RX_INVITE_REQ!,sock_open:%d, BT_attend:%d\n", pcoex_info->sock_open, pcoex_info->BT_attend);
			rtw_btcoex_sendmsgbysocket(pbtcoexadapter, invite_rsp, sizeof(invite_rsp), _FALSE);
		} else if (RX_INVITE_RSP == parse_res) {
			/* invite rsp */
			pcoex_info->BT_attend = _TRUE;
			RTW_INFO("RX_INVITE_RSP!,sock_open:%d, BT_attend:%d\n", pcoex_info->sock_open, pcoex_info->BT_attend);
		} else if (RX_LEAVE_ACK == parse_res) {
			/* mean BT know wifi  will leave */
			pcoex_info->BT_attend = _FALSE;
			RTW_INFO("RX_LEAVE_ACK!,sock_open:%d, BT_attend:%d\n", pcoex_info->sock_open, pcoex_info->BT_attend);
		} else if (RX_BT_LEAVE == parse_res) {
			/* BT leave */
			rtw_btcoex_sendmsgbysocket(pbtcoexadapter, leave_ack, sizeof(leave_ack), _FALSE); /*  no ack */
			pcoex_info->BT_attend = _FALSE;
			RTW_INFO("RX_BT_LEAVE!sock_open:%d, BT_attend:%d\n", pcoex_info->sock_open, pcoex_info->BT_attend);
		} else {
			/* todo: check if recv data are really hci cmds */
			if (_TRUE == pcoex_info->BT_attend)
				rtw_btcoex_parse_hci_cmd(pbtcoexadapter, recv_data, recv_length);
		}
#endif
		switch (parse_res) {
		case RX_ATTEND_ACK:
			/* attend ack */
			pcoex_info->BT_attend = _TRUE;
			RTW_INFO("RX_ATTEND_ACK!,sock_open:%d, BT_attend:%d\n", pcoex_info->sock_open, pcoex_info->BT_attend);
			rtw_btcoex_pta_off_on_notify(pbtcoexadapter, pcoex_info->BT_attend);
			break;

		case RX_ATTEND_REQ:
			pcoex_info->BT_attend = _TRUE;
			RTW_INFO("RX_BT_ATTEND_REQ!,sock_open:%d, BT_attend:%d\n", pcoex_info->sock_open, pcoex_info->BT_attend);
			rtw_btcoex_sendmsgbysocket(pbtcoexadapter, attend_ack, sizeof(attend_ack), _FALSE);
			rtw_btcoex_pta_off_on_notify(pbtcoexadapter, pcoex_info->BT_attend);
			break;

		case RX_INVITE_REQ:
			/* invite req from BT */
			pcoex_info->BT_attend = _TRUE;
			RTW_INFO("RX_INVITE_REQ!,sock_open:%d, BT_attend:%d\n", pcoex_info->sock_open, pcoex_info->BT_attend);
			rtw_btcoex_sendmsgbysocket(pbtcoexadapter, invite_rsp, sizeof(invite_rsp), _FALSE);
			rtw_btcoex_pta_off_on_notify(pbtcoexadapter, pcoex_info->BT_attend);
			break;

		case RX_INVITE_RSP:
			/*invite rsp*/
			pcoex_info->BT_attend = _TRUE;
			RTW_INFO("RX_INVITE_RSP!,sock_open:%d, BT_attend:%d\n", pcoex_info->sock_open, pcoex_info->BT_attend);
			rtw_btcoex_pta_off_on_notify(pbtcoexadapter, pcoex_info->BT_attend);
			break;

		case RX_LEAVE_ACK:
			/* mean BT know wifi  will leave */
			pcoex_info->BT_attend = _FALSE;
			RTW_INFO("RX_LEAVE_ACK!,sock_open:%d, BT_attend:%d\n", pcoex_info->sock_open, pcoex_info->BT_attend);
			rtw_btcoex_pta_off_on_notify(pbtcoexadapter, pcoex_info->BT_attend);
			break;

		case RX_BT_LEAVE:
			/* BT leave */
			rtw_btcoex_sendmsgbysocket(pbtcoexadapter, leave_ack, sizeof(leave_ack), _FALSE); /* no ack */
			pcoex_info->BT_attend = _FALSE;
			RTW_INFO("RX_BT_LEAVE!sock_open:%d, BT_attend:%d\n", pcoex_info->sock_open, pcoex_info->BT_attend);
			rtw_btcoex_pta_off_on_notify(pbtcoexadapter, pcoex_info->BT_attend);
			break;

		default:
			if (_TRUE == pcoex_info->BT_attend)
				rtw_btcoex_parse_hci_cmd(pbtcoexadapter, recv_data, recv_length);
			else
				RTW_INFO("ERROR!! BT is UP\n");
			break;

		}

		len--;
		kfree_skb(skb);
	}
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0))
	void rtw_btcoex_recvmsg_init(struct sock *sk_in, s32 bytes)
#else
	void rtw_btcoex_recvmsg_init(struct sock *sk_in)
#endif
{
	struct bt_coex_info *pcoex_info = NULL;

	if (pbtcoexadapter == NULL) {
		RTW_INFO("%s: btcoexadapter NULL\n", __func__);
		return;
	}
	pcoex_info = &pbtcoexadapter->coex_info;
	pcoex_info->sk_store = sk_in;
	if (pcoex_info->btcoex_wq != NULL)
		queue_delayed_work(pcoex_info->btcoex_wq, &pcoex_info->recvmsg_work, 0);
	else
		RTW_INFO("%s: BTCOEX workqueue NULL\n", __func__);
}

u8 rtw_btcoex_sendmsgbysocket(_adapter *padapter, u8 *msg, u8 msg_size, bool force)
{
	u8 error;
	struct msghdr	udpmsg;
	mm_segment_t	oldfs;
	struct iovec	iov;
	struct bt_coex_info *pcoex_info = &padapter->coex_info;

	/* RTW_INFO("%s: msg:%s, force:%s\n", __func__, msg, force == _TRUE?"TRUE":"FALSE"); */
	if (_FALSE == force) {
		if (_FALSE == pcoex_info->BT_attend) {
			RTW_INFO("TX Blocked: WiFi-BT disconnected\n");
			return _FAIL;
		}
	}

	iov.iov_base	 = (void *)msg;
	iov.iov_len	 = msg_size;
	udpmsg.msg_name	 = &pcoex_info->bt_sockaddr;
	udpmsg.msg_namelen	= sizeof(struct sockaddr_in);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
	/* referece:sock_xmit in kernel code
	 * WRITE for sock_sendmsg, READ for sock_recvmsg
	 * third parameter for msg_iovlen
	 * last parameter for iov_len
	 */
	iov_iter_init(&udpmsg.msg_iter, WRITE, &iov, 1, msg_size);
#else
	udpmsg.msg_iov	 = &iov;
	udpmsg.msg_iovlen	= 1;
#endif
	udpmsg.msg_control	= NULL;
	udpmsg.msg_controllen = 0;
	udpmsg.msg_flags	= MSG_DONTWAIT | MSG_NOSIGNAL;
	oldfs = get_fs();
	set_fs(KERNEL_DS);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	error = sock_sendmsg(pcoex_info->udpsock, &udpmsg);
#else
	error = sock_sendmsg(pcoex_info->udpsock, &udpmsg, msg_size);
#endif
	set_fs(oldfs);
	if (error < 0) {
		RTW_INFO("Error when sendimg msg, error:%d\n", error);
		return _FAIL;
	} else
		return _SUCCESS;
}

u8 rtw_btcoex_create_kernel_socket(_adapter *padapter)
{
	s8 kernel_socket_err;
	u8 tx_msg[255] = attend_req;
	struct bt_coex_info *pcoex_info = &padapter->coex_info;
	s32 sock_reuse = 1;
	u8 status = _FAIL;

	RTW_INFO("%s CONNECT_PORT %d\n", __func__, CONNECT_PORT);

	if (NULL == pcoex_info) {
		RTW_INFO("coex_info: NULL\n");
		status =  _FAIL;
	}

	kernel_socket_err = sock_create(PF_INET, SOCK_DGRAM, 0, &pcoex_info->udpsock);

	if (kernel_socket_err < 0) {
		RTW_INFO("Error during creation of socket error:%d\n", kernel_socket_err);
		status = _FAIL;
	} else {
		_rtw_memset(&(pcoex_info->wifi_sockaddr), 0, sizeof(pcoex_info->wifi_sockaddr));
		pcoex_info->wifi_sockaddr.sin_family = AF_INET;
		pcoex_info->wifi_sockaddr.sin_port = htons(CONNECT_PORT);
		pcoex_info->wifi_sockaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

		_rtw_memset(&(pcoex_info->bt_sockaddr), 0, sizeof(pcoex_info->bt_sockaddr));
		pcoex_info->bt_sockaddr.sin_family = AF_INET;
		pcoex_info->bt_sockaddr.sin_port = htons(CONNECT_PORT_BT);
		pcoex_info->bt_sockaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

		pcoex_info->sk_store = NULL;
		kernel_socket_err = pcoex_info->udpsock->ops->bind(pcoex_info->udpsock, (struct sockaddr *)&pcoex_info->wifi_sockaddr,
				    sizeof(pcoex_info->wifi_sockaddr));
		if (kernel_socket_err == 0) {
			RTW_INFO("binding socket success\n");
			pcoex_info->udpsock->sk->sk_data_ready = rtw_btcoex_recvmsg_init;
			pcoex_info->sock_open |=  KERNEL_SOCKET_OK;
			pcoex_info->BT_attend = _FALSE;
			RTW_INFO("WIFI sending attend_req\n");
			rtw_btcoex_sendmsgbysocket(padapter, attend_req, sizeof(attend_req), _TRUE);
			status = _SUCCESS;
		} else {
			pcoex_info->BT_attend = _FALSE;
			sock_release(pcoex_info->udpsock); /* bind fail release socket */
			RTW_INFO("Error binding socket: %d\n", kernel_socket_err);
			status = _FAIL;
		}

	}

	return status;
}

void rtw_btcoex_close_kernel_socket(_adapter *padapter)
{
	struct bt_coex_info *pcoex_info = &padapter->coex_info;
	if (pcoex_info->sock_open & KERNEL_SOCKET_OK) {
		RTW_INFO("release kernel socket\n");
		sock_release(pcoex_info->udpsock);
		pcoex_info->sock_open &= ~(KERNEL_SOCKET_OK);
		if (_TRUE == pcoex_info->BT_attend)
			pcoex_info->BT_attend = _FALSE;

		RTW_INFO("sock_open:%d, BT_attend:%d\n", pcoex_info->sock_open, pcoex_info->BT_attend);
	}
}

void rtw_btcoex_init_socket(_adapter *padapter)
{

	u8 is_invite = _FALSE;
	struct bt_coex_info *pcoex_info = &padapter->coex_info;
	RTW_INFO("%s\n", __func__);
	if (_FALSE == pcoex_info->is_exist) {
		_rtw_memset(pcoex_info, 0, sizeof(struct bt_coex_info));
		pcoex_info->btcoex_wq = create_workqueue("BTCOEX");
		INIT_DELAYED_WORK(&pcoex_info->recvmsg_work,
				  (void *)rtw_btcoex_recvmsgbysocket);
		pbtcoexadapter = padapter;
		/* We expect BT is off if BT don't send ack to wifi */
		RTW_INFO("We expect BT is off if BT send ack to wifi\n");
		rtw_btcoex_pta_off_on_notify(pbtcoexadapter, _FALSE);
		if (rtw_btcoex_create_kernel_socket(padapter) == _SUCCESS)
			pcoex_info->is_exist = _TRUE;
		else {
			pcoex_info->is_exist = _FALSE;
			pbtcoexadapter = NULL;
		}

		RTW_INFO("%s: pbtcoexadapter:%p, coex_info->is_exist: %s\n"
			, __func__, pbtcoexadapter, pcoex_info->is_exist == _TRUE ? "TRUE" : "FALSE");
	}
}

void rtw_btcoex_close_socket(_adapter *padapter)
{
	struct bt_coex_info *pcoex_info = &padapter->coex_info;

	RTW_INFO("%s--coex_info->is_exist: %s, pcoex_info->BT_attend:%s\n"
		, __func__, pcoex_info->is_exist == _TRUE ? "TRUE" : "FALSE", pcoex_info->BT_attend == _TRUE ? "TRUE" : "FALSE");

	if (_TRUE == pcoex_info->is_exist) {
		if (_TRUE == pcoex_info->BT_attend) {
			/*inform BT wifi leave*/
			rtw_btcoex_sendmsgbysocket(padapter, wifi_leave, sizeof(wifi_leave), _FALSE);
			msleep(50);
		}

		if (pcoex_info->btcoex_wq != NULL) {
			flush_workqueue(pcoex_info->btcoex_wq);
			destroy_workqueue(pcoex_info->btcoex_wq);
		}

		rtw_btcoex_close_kernel_socket(padapter);
		pbtcoexadapter = NULL;
		pcoex_info->is_exist = _FALSE;
	}
}

void rtw_btcoex_dump_tx_msg(u8 *tx_msg, u8 len, u8 *msg_name)
{
	u8	i = 0;
	RTW_INFO("======> Msg name: %s\n", msg_name);
	for (i = 0; i < len; i++)
		printk("%02x ", tx_msg[i]);
	printk("\n");
	RTW_INFO("Msg name: %s <======\n", msg_name);
}

/* Porting from Windows team */
void rtw_btcoex_SendEventExtBtCoexControl(PADAPTER padapter, u8 bNeedDbgRsp, u8 dataLen, void *pData)
{
	u8			len = 0, tx_event_length = 0;
	u8 			localBuf[32] = "";
	u8			*pRetPar;
	u8			opCode = 0;
	u8			*pInBuf = (pu1Byte)pData;
	u8			*pOpCodeContent;
	rtw_HCI_event *pEvent;

	opCode = pInBuf[0];

	RTW_INFO("%s, OPCode:%02x\n", __func__, opCode);

	pEvent = (rtw_HCI_event *)(&localBuf[0]);

	/* len += bthci_ExtensionEventHeaderRtk(&localBuf[0], */
	/*	HCI_EVENT_EXT_BT_COEX_CONTROL); */
	pEvent->EventCode = HCI_EVENT_EXTENSION_RTK;
	pEvent->Data[0] = HCI_EVENT_EXT_BT_COEX_CONTROL;	/* extension event code */
	len++;

	/* Return parameters starts from here */
	pRetPar = &pEvent->Data[len];
	_rtw_memcpy(&pRetPar[0], pData, dataLen);

	len += dataLen;

	pEvent->Length = len;

	/* total tx event length + EventCode length + sizeof(length) */
	tx_event_length = pEvent->Length + 2;
#if 0
	rtw_btcoex_dump_tx_msg((u8 *)pEvent, tx_event_length, "BT COEX CONTROL", _FALSE);
#endif
	rtw_btcoex_sendmsgbysocket(padapter, (u8 *)pEvent, tx_event_length, _FALSE);

}

/* Porting from Windows team */
void rtw_btcoex_SendEventExtBtInfoControl(PADAPTER padapter, u8 dataLen, void *pData)
{
	rtw_HCI_event *pEvent;
	u8			*pRetPar;
	u8			len = 0, tx_event_length = 0;
	u8 			localBuf[32] = "";

	struct bt_coex_info *pcoex_info = &padapter->coex_info;
	PBT_MGNT		pBtMgnt = &pcoex_info->BtMgnt;

	/* RTW_INFO("%s\n",__func__);*/
	if (pBtMgnt->ExtConfig.HCIExtensionVer < 4) { /* not support */
		RTW_INFO("ERROR: HCIExtensionVer = %d, HCIExtensionVer<4 !!!!\n", pBtMgnt->ExtConfig.HCIExtensionVer);
		return;
	}

	pEvent = (rtw_HCI_event *)(&localBuf[0]);

	/* len += bthci_ExtensionEventHeaderRtk(&localBuf[0], */
	/*		HCI_EVENT_EXT_BT_INFO_CONTROL); */
	pEvent->EventCode = HCI_EVENT_EXTENSION_RTK;
	pEvent->Data[0] = HCI_EVENT_EXT_BT_INFO_CONTROL;		/* extension event code */
	len++;

	/* Return parameters starts from here */
	pRetPar = &pEvent->Data[len];
	_rtw_memcpy(&pRetPar[0], pData, dataLen);

	len += dataLen;

	pEvent->Length = len;

	/* total tx event length + EventCode length + sizeof(length) */
	tx_event_length = pEvent->Length + 2;
#if 0
	rtw_btcoex_dump_tx_msg((u8 *)pEvent, tx_event_length, "BT INFO CONTROL");
#endif
	rtw_btcoex_sendmsgbysocket(padapter, (u8 *)pEvent, tx_event_length, _FALSE);

}

void rtw_btcoex_SendScanNotify(PADAPTER padapter, u8 scanType)
{
	u8	len = 0, tx_event_length = 0;
	u8 	localBuf[7] = "";
	u8	*pRetPar;
	u8	*pu1Temp;
	rtw_HCI_event *pEvent;
	struct bt_coex_info *pcoex_info = &padapter->coex_info;
	PBT_MGNT		pBtMgnt = &pcoex_info->BtMgnt;

	/*	if(!pBtMgnt->BtOperationOn)
	 *		return; */

	pEvent = (rtw_HCI_event *)(&localBuf[0]);

	/*	len += bthci_ExtensionEventHeaderRtk(&localBuf[0],
	 *			HCI_EVENT_EXT_WIFI_SCAN_NOTIFY); */

	pEvent->EventCode = HCI_EVENT_EXTENSION_RTK;
	pEvent->Data[0] = HCI_EVENT_EXT_WIFI_SCAN_NOTIFY;		/* extension event code */
	len++;

	/* Return parameters starts from here */
	/* pRetPar = &PPacketIrpEvent->Data[len]; */
	/* pu1Temp = (u8 *)&pRetPar[0]; */
	/* *pu1Temp = scanType; */
	pEvent->Data[len] = scanType;
	len += 1;

	pEvent->Length = len;

	/* total tx event length + EventCode length + sizeof(length) */
	tx_event_length = pEvent->Length + 2;
#if 0
	rtw_btcoex_dump_tx_msg((u8 *)pEvent, tx_event_length, "WIFI SCAN OPERATION");
#endif
	rtw_btcoex_sendmsgbysocket(padapter, (u8 *)pEvent, tx_event_length, _FALSE);
}
#endif /* CONFIG_BT_COEXIST_SOCKET_TRX */
#endif /* CONFIG_BT_COEXIST */
