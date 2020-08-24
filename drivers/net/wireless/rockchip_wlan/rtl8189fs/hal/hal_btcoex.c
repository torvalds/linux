/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2013 - 2017 Realtek Corporation.
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
#define __HAL_BTCOEX_C__

#ifdef CONFIG_BT_COEXIST

#include <hal_data.h>
#include <hal_btcoex.h>
#include "btc/mp_precomp.h"

/* ************************************
 *		Global variables
 * ************************************ */
const char *const BtProfileString[] = {
	"NONE",
	"A2DP",
	"PAN",
	"HID",
	"SCO",
};

const char *const BtSpecString[] = {
	"1.0b",
	"1.1",
	"1.2",
	"2.0+EDR",
	"2.1+EDR",
	"3.0+HS",
	"4.0",
};

const char *const BtLinkRoleString[] = {
	"Master",
	"Slave",
};

const char *const h2cStaString[] = {
	"successful",
	"h2c busy",
	"rf off",
	"fw not read",
};

const char *const ioStaString[] = {
	"success",
	"can not IO",
	"rf off",
	"fw not read",
	"wait io timeout",
	"invalid len",
	"idle Q empty",
	"insert waitQ fail",
	"unknown fail",
	"wrong level",
	"h2c stopped",
};

const char *const GLBtcWifiBwString[] = {
	"11bg",
	"HT20",
	"HT40",
	"VHT80",
	"VHT160"
};

const char *const GLBtcWifiFreqString[] = {
	"2.4G",
	"5G",
	"2.4G+5G"
};

const char *const GLBtcIotPeerString[] = {
	"UNKNOWN",
	"REALTEK",
	"REALTEK_92SE",
	"BROADCOM",
	"RALINK",
	"ATHEROS",
	"CISCO",
	"MERU",
	"MARVELL",
	"REALTEK_SOFTAP", /* peer is RealTek SOFT_AP, by Bohn, 2009.12.17 */
	"SELF_SOFTAP", /* Self is SoftAP */
	"AIRGO",
	"INTEL",
	"RTK_APCLIENT",
	"REALTEK_81XX",
	"REALTEK_WOW",
	"REALTEK_JAGUAR_BCUTAP",
	"REALTEK_JAGUAR_CCUTAP"
};

const char *const coexOpcodeString[] = {
	"Wifi status notify",
	"Wifi progress",
	"Wifi info",
	"Power state",
	"Set Control",
	"Get Control"
};

const char *const coexIndTypeString[] = {
	"bt info",
	"pstdma",
	"limited tx/rx",
	"coex table",
	"request"
};

const char *const coexH2cResultString[] = {
	"ok",
	"unknown",
	"un opcode",
	"opVer MM",
	"par Err",
	"par OoR",
	"reqNum MM",
	"halMac Fail",
	"h2c TimeOut",
	"Invalid c2h Len",
	"data overflow"
};

#define HALBTCOUTSRC_AGG_CHK_WINDOW_IN_MS	8000

struct btc_coexist GLBtCoexist;
BTC_OFFLOAD gl_coex_offload;
u8 GLBtcWiFiInScanState;
u8 GLBtcWiFiInIQKState;
u8 GLBtcWiFiInIPS;
u8 GLBtcWiFiInLPS;
u8 GLBtcBtCoexAliveRegistered;

/*
 * BT control H2C/C2H
 */
/* EXT_EID */
typedef enum _bt_ext_eid {
	C2H_WIFI_FW_ACTIVE_RSP	= 0,
	C2H_TRIG_BY_BT_FW
} BT_EXT_EID;

/* C2H_STATUS */
typedef enum _bt_c2h_status {
	BT_STATUS_OK = 0,
	BT_STATUS_VERSION_MISMATCH,
	BT_STATUS_UNKNOWN_OPCODE,
	BT_STATUS_ERROR_PARAMETER
} BT_C2H_STATUS;

/* C2H BT OP CODES */
typedef enum _bt_op_code {
	BT_OP_GET_BT_VERSION					= 0x00,
	BT_OP_WRITE_REG_ADDR					= 0x0c,
	BT_OP_WRITE_REG_VALUE					= 0x0d,

	BT_OP_READ_REG							= 0x11,

	BT_LO_OP_GET_AFH_MAP_L					= 0x1e,
	BT_LO_OP_GET_AFH_MAP_M					= 0x1f,
	BT_LO_OP_GET_AFH_MAP_H					= 0x20,

	BT_OP_GET_BT_COEX_SUPPORTED_FEATURE		= 0x2a,
	BT_OP_GET_BT_COEX_SUPPORTED_VERSION		= 0x2b,
	BT_OP_GET_BT_ANT_DET_VAL				= 0x2c,
	BT_OP_GET_BT_BLE_SCAN_TYPE				= 0x2d,
	BT_OP_GET_BT_BLE_SCAN_PARA				= 0x2e,
	BT_OP_GET_BT_DEVICE_INFO				= 0x30,
	BT_OP_GET_BT_FORBIDDEN_SLOT_VAL			= 0x31,
	BT_OP_SET_BT_LANCONSTRAIN_LEVEL			= 0x32,
	BT_OP_SET_BT_TEST_MODE_VAL				= 0x33,
	BT_OP_MAX
} BT_OP_CODE;

#define BTC_MPOPER_TIMEOUT	50	/* unit: ms */

#define C2H_MAX_SIZE		16
u8 GLBtcBtMpOperSeq;
_mutex GLBtcBtMpOperLock;
_timer GLBtcBtMpOperTimer;
_sema GLBtcBtMpRptSema;
u8 GLBtcBtMpRptSeq;
u8 GLBtcBtMpRptStatus;
u8 GLBtcBtMpRptRsp[C2H_MAX_SIZE];
u8 GLBtcBtMpRptRspSize;
u8 GLBtcBtMpRptWait;
u8 GLBtcBtMpRptWiFiOK;
u8 GLBtcBtMpRptBTOK;

/*
 * Debug
 */
u32 GLBtcDbgType[COMP_MAX];
u8 GLBtcDbgBuf[BT_TMP_BUF_SIZE];
u8	gl_btc_trace_buf[BT_TMP_BUF_SIZE];

typedef struct _btcoexdbginfo {
	u8 *info;
	u32 size; /* buffer total size */
	u32 len; /* now used length */
} BTCDBGINFO, *PBTCDBGINFO;

BTCDBGINFO GLBtcDbgInfo;

#define	BT_Operation(Adapter)						_FALSE

static void DBG_BT_INFO_INIT(PBTCDBGINFO pinfo, u8 *pbuf, u32 size)
{
	if (NULL == pinfo)
		return;

	_rtw_memset(pinfo, 0, sizeof(BTCDBGINFO));

	if (pbuf && size) {
		pinfo->info = pbuf;
		pinfo->size = size;
	}
}

void DBG_BT_INFO(u8 *dbgmsg)
{
	PBTCDBGINFO pinfo;
	u32 msglen, buflen;
	u8 *pbuf;


	pinfo = &GLBtcDbgInfo;

	if (NULL == pinfo->info)
		return;

	msglen = strlen(dbgmsg);
	if (pinfo->len + msglen > pinfo->size)
		return;

	pbuf = pinfo->info + pinfo->len;
	_rtw_memcpy(pbuf, dbgmsg, msglen);
	pinfo->len += msglen;
}

/* ************************************
 *		Debug related function
 * ************************************ */
static u8 halbtcoutsrc_IsBtCoexistAvailable(PBTC_COEXIST pBtCoexist)
{
	if (!pBtCoexist->bBinded ||
	    NULL == pBtCoexist->Adapter)
		return _FALSE;
	return _TRUE;
}

static void halbtcoutsrc_DbgInit(void)
{
	u8	i;

	for (i = 0; i < COMP_MAX; i++)
		GLBtcDbgType[i] = 0;
}

static void halbtcoutsrc_EnterPwrLock(PBTC_COEXIST pBtCoexist)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj((PADAPTER)pBtCoexist->Adapter);
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(dvobj);

	_enter_pwrlock(&pwrpriv->lock);
}

static void halbtcoutsrc_ExitPwrLock(PBTC_COEXIST pBtCoexist)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj((PADAPTER)pBtCoexist->Adapter);
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(dvobj);

	_exit_pwrlock(&pwrpriv->lock);
}

static u8 halbtcoutsrc_IsHwMailboxExist(PBTC_COEXIST pBtCoexist)
{
	if (pBtCoexist->board_info.bt_chip_type == BTC_CHIP_CSR_BC4
	    || pBtCoexist->board_info.bt_chip_type == BTC_CHIP_CSR_BC8
	   )
		return _FALSE;
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter))
		return _FALSE;
	else
		return _TRUE;
}

static u8 halbtcoutsrc_LeaveLps(PBTC_COEXIST pBtCoexist)
{
	PADAPTER padapter;


	padapter = pBtCoexist->Adapter;

	pBtCoexist->bt_info.bt_ctrl_lps = _TRUE;
	pBtCoexist->bt_info.bt_lps_on = _FALSE;

	return rtw_btcoex_LPS_Leave(padapter);
}

void halbtcoutsrc_EnterLps(PBTC_COEXIST pBtCoexist)
{
	PADAPTER padapter;


	padapter = pBtCoexist->Adapter;

	if (pBtCoexist->bdontenterLPS == _FALSE) {
		pBtCoexist->bt_info.bt_ctrl_lps = _TRUE;
		pBtCoexist->bt_info.bt_lps_on = _TRUE;

		rtw_btcoex_LPS_Enter(padapter);
	}
}

void halbtcoutsrc_NormalLps(PBTC_COEXIST pBtCoexist)
{
	PADAPTER padapter;



	padapter = pBtCoexist->Adapter;

	if (pBtCoexist->bt_info.bt_ctrl_lps) {
		pBtCoexist->bt_info.bt_lps_on = _FALSE;
		rtw_btcoex_LPS_Leave(padapter);
		pBtCoexist->bt_info.bt_ctrl_lps = _FALSE;

		/* recover the LPS state to the original */
#if 0
		padapter->hal_func.UpdateLPSStatusHandler(
			padapter,
			pPSC->RegLeisurePsMode,
			pPSC->RegPowerSaveMode);
#endif
	}
}

void halbtcoutsrc_Pre_NormalLps(PBTC_COEXIST pBtCoexist)
{
	PADAPTER padapter;

	padapter = pBtCoexist->Adapter;

	if (pBtCoexist->bt_info.bt_ctrl_lps) {
		pBtCoexist->bt_info.bt_lps_on = _FALSE;
		rtw_btcoex_LPS_Leave(padapter);
	}
}

void halbtcoutsrc_Post_NormalLps(PBTC_COEXIST pBtCoexist)
{
	if (pBtCoexist->bt_info.bt_ctrl_lps)
		pBtCoexist->bt_info.bt_ctrl_lps = _FALSE;
}

/*
 *  Constraint:
 *	   1. this function will request pwrctrl->lock
 */
void halbtcoutsrc_LeaveLowPower(PBTC_COEXIST pBtCoexist)
{
#ifdef CONFIG_LPS_LCLK
	PADAPTER padapter;
	PHAL_DATA_TYPE pHalData;
	struct pwrctrl_priv *pwrctrl;
	s32 ready;
	systime stime;
	s32 utime;
	u32 timeout; /* unit: ms */


	padapter = pBtCoexist->Adapter;
	pHalData = GET_HAL_DATA(padapter);
	pwrctrl = adapter_to_pwrctl(padapter);
	ready = _FAIL;
#ifdef LPS_RPWM_WAIT_MS
	timeout = LPS_RPWM_WAIT_MS;
#else /* !LPS_RPWM_WAIT_MS */
	timeout = 30;
#endif /* !LPS_RPWM_WAIT_MS */

	if (GLBtcBtCoexAliveRegistered == _TRUE)
		return;

	stime = rtw_get_current_time();
	do {
		ready = rtw_register_task_alive(padapter, BTCOEX_ALIVE);
		if (_SUCCESS == ready)
			break;

		utime = rtw_get_passing_time_ms(stime);
		if (utime > timeout)
			break;

		rtw_msleep_os(1);
	} while (1);

	GLBtcBtCoexAliveRegistered = _TRUE;
#endif /* CONFIG_LPS_LCLK */
}

/*
 *  Constraint:
 *	   1. this function will request pwrctrl->lock
 */
void halbtcoutsrc_NormalLowPower(PBTC_COEXIST pBtCoexist)
{
#ifdef CONFIG_LPS_LCLK
	PADAPTER padapter;

	if (GLBtcBtCoexAliveRegistered == _FALSE)
		return;

	padapter = pBtCoexist->Adapter;
	rtw_unregister_task_alive(padapter, BTCOEX_ALIVE);

	GLBtcBtCoexAliveRegistered = _FALSE;
#endif /* CONFIG_LPS_LCLK */
}

void halbtcoutsrc_DisableLowPower(PBTC_COEXIST pBtCoexist, u8 bLowPwrDisable)
{
	pBtCoexist->bt_info.bt_disable_low_pwr = bLowPwrDisable;
	if (bLowPwrDisable)
		halbtcoutsrc_LeaveLowPower(pBtCoexist);		/* leave 32k low power. */
	else
		halbtcoutsrc_NormalLowPower(pBtCoexist);	/* original 32k low power behavior. */
}

void halbtcoutsrc_AggregationCheck(PBTC_COEXIST pBtCoexist)
{
	PADAPTER padapter;
	BOOLEAN bNeedToAct = _FALSE;
	static u32 preTime = 0;
	u32 curTime = 0;

	padapter = pBtCoexist->Adapter;

	/* ===================================== */
	/* To void continuous deleteBA=>addBA=>deleteBA=>addBA */
	/* This function is not allowed to continuous called. */
	/* It can only be called after 8 seconds. */
	/* ===================================== */

	curTime = rtw_systime_to_ms(rtw_get_current_time());
	if ((curTime - preTime) < HALBTCOUTSRC_AGG_CHK_WINDOW_IN_MS)	/* over 8 seconds you can execute this function again. */
		return;
	else
		preTime = curTime;

	if (pBtCoexist->bt_info.reject_agg_pkt) {
		bNeedToAct = _TRUE;
		pBtCoexist->bt_info.pre_reject_agg_pkt = pBtCoexist->bt_info.reject_agg_pkt;
	} else {
		if (pBtCoexist->bt_info.pre_reject_agg_pkt) {
			bNeedToAct = _TRUE;
			pBtCoexist->bt_info.pre_reject_agg_pkt = pBtCoexist->bt_info.reject_agg_pkt;
		}

		if (pBtCoexist->bt_info.pre_bt_ctrl_agg_buf_size !=
		    pBtCoexist->bt_info.bt_ctrl_agg_buf_size) {
			bNeedToAct = _TRUE;
			pBtCoexist->bt_info.pre_bt_ctrl_agg_buf_size = pBtCoexist->bt_info.bt_ctrl_agg_buf_size;
		}

		if (pBtCoexist->bt_info.bt_ctrl_agg_buf_size) {
			if (pBtCoexist->bt_info.pre_agg_buf_size !=
			    pBtCoexist->bt_info.agg_buf_size)
				bNeedToAct = _TRUE;
			pBtCoexist->bt_info.pre_agg_buf_size = pBtCoexist->bt_info.agg_buf_size;
		}
	}

	if (bNeedToAct)
		rtw_btcoex_rx_ampdu_apply(padapter);
}

u8 halbtcoutsrc_is_autoload_fail(PBTC_COEXIST pBtCoexist)
{
	PADAPTER padapter;
	PHAL_DATA_TYPE pHalData;

	padapter = pBtCoexist->Adapter;
	pHalData = GET_HAL_DATA(padapter);

	return pHalData->bautoload_fail_flag;
}

u8 halbtcoutsrc_is_fw_ready(PBTC_COEXIST pBtCoexist)
{
	PADAPTER padapter;

	padapter = pBtCoexist->Adapter;

	return GET_HAL_DATA(padapter)->bFWReady;
}

u8 halbtcoutsrc_IsDualBandConnected(PADAPTER padapter)
{
	u8 ret = BTC_MULTIPORT_SCC;

#ifdef CONFIG_MCC_MODE
	if (MCC_EN(padapter) && (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC))) {
		struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
		struct mcc_obj_priv *mccobjpriv = &(dvobj->mcc_objpriv);
		u8 band0 = mccobjpriv->iface[0]->mlmeextpriv.cur_channel > 14 ? BAND_ON_5G : BAND_ON_2_4G;
		u8 band1 = mccobjpriv->iface[1]->mlmeextpriv.cur_channel > 14 ? BAND_ON_5G : BAND_ON_2_4G;

		if (band0 != band1)
			ret = BTC_MULTIPORT_MCC_DUAL_BAND;
		else
			ret = BTC_MULTIPORT_MCC_DUAL_CHANNEL;
	}
#endif

	return ret;
}

u8 halbtcoutsrc_IsWifiBusy(PADAPTER padapter)
{
	if (rtw_mi_check_status(padapter, MI_AP_ASSOC))
		return _TRUE;
	if (rtw_mi_busy_traffic_check(padapter, _FALSE))
		return _TRUE;

	return _FALSE;
}

static u32 _halbtcoutsrc_GetWifiLinkStatus(PADAPTER padapter)
{
	struct mlme_priv *pmlmepriv;
	u8 bp2p;
	u32 portConnectedStatus;


	pmlmepriv = &padapter->mlmepriv;
	bp2p = _FALSE;
	portConnectedStatus = 0;

#ifdef CONFIG_P2P
	if (!rtw_p2p_chk_state(&padapter->wdinfo, P2P_STATE_NONE))
		bp2p = _TRUE;
#endif /* CONFIG_P2P */

	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE) {
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE) {
			if (_TRUE == bp2p)
				portConnectedStatus |= WIFI_P2P_GO_CONNECTED;
			else
				portConnectedStatus |= WIFI_AP_CONNECTED;
		} else {
			if (_TRUE == bp2p)
				portConnectedStatus |= WIFI_P2P_GC_CONNECTED;
			else
				portConnectedStatus |= WIFI_STA_CONNECTED;
		}
	}

	return portConnectedStatus;
}

u32 halbtcoutsrc_GetWifiLinkStatus(PBTC_COEXIST pBtCoexist)
{
	/* ================================= */
	/* return value: */
	/* [31:16]=> connected port number */
	/* [15:0]=> port connected bit define */
	/* ================================ */

	PADAPTER padapter;
	u32 retVal;
	u32 portConnectedStatus, numOfConnectedPort;
	struct dvobj_priv *dvobj;
	_adapter *iface;
	int i;

	padapter = pBtCoexist->Adapter;
	retVal = 0;
	portConnectedStatus = 0;
	numOfConnectedPort = 0;
	dvobj = adapter_to_dvobj(padapter);

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if ((iface) && rtw_is_adapter_up(iface)) {
			retVal = _halbtcoutsrc_GetWifiLinkStatus(iface);
			if (retVal) {
				portConnectedStatus |= retVal;
				numOfConnectedPort++;
			}
		}
	}
	retVal = (numOfConnectedPort << 16) | portConnectedStatus;

	return retVal;
}

struct btc_wifi_link_info halbtcoutsrc_getwifilinkinfo(PBTC_COEXIST pBtCoexist)
{
	u8 n_assoc_iface = 0, i =0, mcc_en = _FALSE;
	PADAPTER adapter = NULL;
	PADAPTER iface = NULL;
	PADAPTER sta_iface = NULL, p2p_iface = NULL, ap_iface = NULL;
	BTC_LINK_MODE btc_link_moe = BTC_LINK_MAX;
	struct dvobj_priv *dvobj = NULL;
	struct mlme_ext_priv *mlmeext = NULL;
	struct btc_wifi_link_info wifi_link_info;

	adapter = (PADAPTER)pBtCoexist->Adapter;
	dvobj = adapter_to_dvobj(adapter);
	n_assoc_iface = rtw_mi_get_assoc_if_num(adapter);

	/* init value */
	wifi_link_info.link_mode = BTC_LINK_NONE;
	wifi_link_info.sta_center_channel = 0;
	wifi_link_info.p2p_center_channel = 0;
	wifi_link_info.bany_client_join_go = _FALSE;
	wifi_link_info.benable_noa = _FALSE;
	wifi_link_info.bhotspot = _FALSE;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (!iface)
			continue;
		
		mlmeext = &iface->mlmeextpriv;
		if (MLME_IS_GO(iface)) {
			wifi_link_info.link_mode = BTC_LINK_ONLY_GO;
			wifi_link_info.p2p_center_channel =
				rtw_get_center_ch(mlmeext->cur_channel, mlmeext->cur_bwmode, mlmeext->cur_ch_offset);
			p2p_iface	 = iface;
			if (rtw_linked_check(iface))
				wifi_link_info.bany_client_join_go = _TRUE;
		} else if (MLME_IS_GC(iface)) {
			wifi_link_info.link_mode = BTC_LINK_ONLY_GC;
			wifi_link_info.p2p_center_channel =
				rtw_get_center_ch(mlmeext->cur_channel, mlmeext->cur_bwmode, mlmeext->cur_ch_offset);
			p2p_iface = iface;
		} else if (MLME_IS_AP(iface)) {
			wifi_link_info.link_mode = BTC_LINK_ONLY_AP;
			ap_iface = iface;
			wifi_link_info.p2p_center_channel =
				rtw_get_center_ch(mlmeext->cur_channel, mlmeext->cur_bwmode, mlmeext->cur_ch_offset);
		} else if (MLME_IS_STA(iface) && rtw_linked_check(iface)) {
			wifi_link_info.link_mode = BTC_LINK_ONLY_STA;
			wifi_link_info.sta_center_channel =
				rtw_get_center_ch(mlmeext->cur_channel, mlmeext->cur_bwmode, mlmeext->cur_ch_offset);
			sta_iface = iface;
		}
	}

#ifdef CONFIG_MCC_MODE
	if (MCC_EN(adapter)) {
		if (rtw_hal_check_mcc_status(adapter, MCC_STATUS_DOING_MCC))
			mcc_en = _TRUE;
	}
#endif/* CONFIG_MCC_MODE */

	if (n_assoc_iface == 0) {
		wifi_link_info.link_mode = BTC_LINK_NONE;
	} else if (n_assoc_iface == 1) {
		/* by pass */
	} else if (n_assoc_iface == 2) {	
		if (sta_iface && p2p_iface) {
			u8 band_sta = sta_iface->mlmeextpriv.cur_channel > 14 ? BAND_ON_5G : BAND_ON_2_4G;
			u8 band_p2p = p2p_iface->mlmeextpriv.cur_channel > 14 ? BAND_ON_5G : BAND_ON_2_4G;
			if (band_sta == band_p2p) {
				switch (band_sta) {
				case BAND_ON_2_4G:
					if (MLME_IS_GO(p2p_iface))
						wifi_link_info.link_mode =
							mcc_en == _TRUE ?  BTC_LINK_2G_MCC_GO_STA : BTC_LINK_2G_SCC_GO_STA;
					else if (MLME_IS_GC(p2p_iface))
						wifi_link_info.link_mode =
							mcc_en == _TRUE ?  BTC_LINK_2G_MCC_GC_STA : BTC_LINK_2G_SCC_GC_STA;
					break;
				case BAND_ON_5G:
					if (MLME_IS_GO(p2p_iface))
						wifi_link_info.link_mode =
							mcc_en == _TRUE ?  BTC_LINK_5G_MCC_GO_STA : BTC_LINK_5G_SCC_GO_STA;
					else if (MLME_IS_GC(p2p_iface))
						wifi_link_info.link_mode =
							mcc_en == _TRUE ?  BTC_LINK_5G_MCC_GC_STA : BTC_LINK_5G_SCC_GC_STA;
					break;
				default:
					break;
				}
			} else {
				if (MLME_IS_GO(p2p_iface))
					wifi_link_info.link_mode = BTC_LINK_25G_MCC_GO_STA;
				else if (MLME_IS_GC(p2p_iface))
					wifi_link_info.link_mode = BTC_LINK_25G_MCC_GC_STA;
			}
		}
	} else {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			RTW_ERR("%s do not support n_assoc_iface > 2 (ant_num == 1)", __func__);
	}

	return wifi_link_info;
}


static void _btmpoper_timer_hdl(void *p)
{
	if (GLBtcBtMpRptWait == _TRUE) {
		GLBtcBtMpRptWait = _FALSE;
		_rtw_up_sema(&GLBtcBtMpRptSema);
	}
}

/*
 * !IMPORTANT!
 *	Before call this function, caller should acquire "GLBtcBtMpOperLock"!
 *	Othrewise there will be racing problem and something may go wrong.
 */
static u8 _btmpoper_cmd(PBTC_COEXIST pBtCoexist, u8 opcode, u8 opcodever, u8 *cmd, u8 size)
{
	PADAPTER padapter;
	u8 buf[H2C_BTMP_OPER_LEN] = {0};
	u8 buflen;
	u8 seq;
	s32 ret;


	if (!cmd && size)
		size = 0;
	if ((size + 2) > H2C_BTMP_OPER_LEN)
		return BT_STATUS_H2C_LENGTH_EXCEEDED;
	buflen = size + 2;

	seq = GLBtcBtMpOperSeq & 0xF;
	GLBtcBtMpOperSeq++;

	buf[0] = (opcodever & 0xF) | (seq << 4);
	buf[1] = opcode;
	if (cmd && size)
		_rtw_memcpy(buf + 2, cmd, size);

	GLBtcBtMpRptWait = _TRUE;
	GLBtcBtMpRptWiFiOK = _FALSE;
	GLBtcBtMpRptBTOK = _FALSE;
	GLBtcBtMpRptStatus = 0;
	padapter = pBtCoexist->Adapter;
	_set_timer(&GLBtcBtMpOperTimer, BTC_MPOPER_TIMEOUT);
	if (rtw_hal_fill_h2c_cmd(padapter, H2C_BT_MP_OPER, buflen, buf) == _FAIL) {
		_cancel_timer_ex(&GLBtcBtMpOperTimer);
		ret = BT_STATUS_H2C_FAIL;
		goto exit;
	}

	_rtw_down_sema(&GLBtcBtMpRptSema);
	/* GLBtcBtMpRptWait should be _FALSE here*/

	if (GLBtcBtMpRptWiFiOK == _FALSE) {
		RTW_ERR("%s: Didn't get H2C Rsp Event!\n", __FUNCTION__);
		ret = BT_STATUS_H2C_TIMTOUT;
		goto exit;
	}
	if (GLBtcBtMpRptBTOK == _FALSE) {
		RTW_DBG("%s: Didn't get BT response!\n", __FUNCTION__);
		ret = BT_STATUS_H2C_BT_NO_RSP;
		goto exit;
	}

	if (seq != GLBtcBtMpRptSeq) {
		RTW_ERR("%s: Sequence number not match!(%d!=%d)!\n",
			 __FUNCTION__, seq, GLBtcBtMpRptSeq);
		ret = BT_STATUS_C2H_REQNUM_MISMATCH;
		goto exit;
	}

	switch (GLBtcBtMpRptStatus) {
	/* Examine the status reported from C2H */
	case BT_STATUS_OK:
		ret = BT_STATUS_BT_OP_SUCCESS;
		RTW_DBG("%s: C2H status = BT_STATUS_BT_OP_SUCCESS\n", __FUNCTION__);
		break;
	case BT_STATUS_VERSION_MISMATCH:
		ret = BT_STATUS_OPCODE_L_VERSION_MISMATCH;
		RTW_DBG("%s: C2H status = BT_STATUS_OPCODE_L_VERSION_MISMATCH\n", __FUNCTION__);
		break;
	case BT_STATUS_UNKNOWN_OPCODE:
		ret = BT_STATUS_UNKNOWN_OPCODE_L;
		RTW_DBG("%s: C2H status = MP_BT_STATUS_UNKNOWN_OPCODE_L\n", __FUNCTION__);
		break;
	case BT_STATUS_ERROR_PARAMETER:
		ret = BT_STATUS_PARAMETER_FORMAT_ERROR_L;
		RTW_DBG("%s: C2H status = MP_BT_STATUS_PARAMETER_FORMAT_ERROR_L\n", __FUNCTION__);
		break;
	default:
		ret = BT_STATUS_UNKNOWN_STATUS_L;
		RTW_DBG("%s: C2H status = MP_BT_STATUS_UNKNOWN_STATUS_L\n", __FUNCTION__);
		break;
	}

exit:
	return ret;
}

u32 halbtcoutsrc_GetBtPatchVer(PBTC_COEXIST pBtCoexist)
{
	if (pBtCoexist->bt_info.get_bt_fw_ver_cnt <= 5) {
		if (halbtcoutsrc_IsHwMailboxExist(pBtCoexist) == _TRUE) {
			_irqL irqL;
			u8 ret;

			_enter_critical_mutex(&GLBtcBtMpOperLock, &irqL);

			ret = _btmpoper_cmd(pBtCoexist, BT_OP_GET_BT_VERSION, 0, NULL, 0);
			if (BT_STATUS_BT_OP_SUCCESS == ret) {
				pBtCoexist->bt_info.bt_real_fw_ver = le16_to_cpu(*(u16 *)GLBtcBtMpRptRsp);
				pBtCoexist->bt_info.bt_fw_ver = *(GLBtcBtMpRptRsp + 2);
				pBtCoexist->bt_info.get_bt_fw_ver_cnt++;
			}

			_exit_critical_mutex(&GLBtcBtMpOperLock, &irqL);
		} else {
#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
			u8 dataLen = 2;
			u8 buf[4] = {0};

			buf[0] = 0x0;	/* OP_Code */
			buf[1] = 0x0;	/* OP_Code_Length */
			BT_SendEventExtBtCoexControl(pBtCoexist->Adapter, _FALSE, dataLen, &buf[0]);
#endif /* !CONFIG_BT_COEXIST_SOCKET_TRX */
		}
	}

	return pBtCoexist->bt_info.bt_real_fw_ver;
}

s32 halbtcoutsrc_GetWifiRssi(PADAPTER padapter)
{
	return rtw_dm_get_min_rssi(padapter);
}

u32 halbtcoutsrc_GetBtCoexSupportedFeature(void *pBtcContext)
{
	PBTC_COEXIST pBtCoexist;
	u32 ret = BT_STATUS_BT_OP_SUCCESS;
	u32 data = 0;

	pBtCoexist = (PBTC_COEXIST)pBtcContext;

	if (halbtcoutsrc_IsHwMailboxExist(pBtCoexist) == _TRUE) {
		u8 buf[3] = {0};
		_irqL irqL;
		u8 op_code;
		u8 status;

		_enter_critical_mutex(&GLBtcBtMpOperLock, &irqL);

		op_code = BT_OP_GET_BT_COEX_SUPPORTED_FEATURE;
		status = _btmpoper_cmd(pBtCoexist, op_code, 0, buf, 0);
		if (status == BT_STATUS_BT_OP_SUCCESS)
			data = le16_to_cpu(*(u16 *)GLBtcBtMpRptRsp);
		else
			ret = SET_BT_MP_OPER_RET(op_code, status);

		_exit_critical_mutex(&GLBtcBtMpOperLock, &irqL);

	} else
		ret = BT_STATUS_NOT_IMPLEMENT;

	return data;
}

u32 halbtcoutsrc_GetBtCoexSupportedVersion(void *pBtcContext)
{
	PBTC_COEXIST pBtCoexist;
	u32 ret = BT_STATUS_BT_OP_SUCCESS;
	u32 data = 0xFFFF;

	pBtCoexist = (PBTC_COEXIST)pBtcContext;

	if (halbtcoutsrc_IsHwMailboxExist(pBtCoexist) == _TRUE) {
		u8 buf[3] = {0};
		_irqL irqL;
		u8 op_code;
		u8 status;

		_enter_critical_mutex(&GLBtcBtMpOperLock, &irqL);

		op_code = BT_OP_GET_BT_COEX_SUPPORTED_VERSION;
		status = _btmpoper_cmd(pBtCoexist, op_code, 0, buf, 0);
		if (status == BT_STATUS_BT_OP_SUCCESS)
			data = le16_to_cpu(*(u16 *)GLBtcBtMpRptRsp);
		else
			ret = SET_BT_MP_OPER_RET(op_code, status);

		_exit_critical_mutex(&GLBtcBtMpOperLock, &irqL);

	} else
		ret = BT_STATUS_NOT_IMPLEMENT;

	return data;
}

u32 halbtcoutsrc_GetBtDeviceInfo(void *pBtcContext)
{
	PBTC_COEXIST pBtCoexist;
	u32 ret = BT_STATUS_BT_OP_SUCCESS;
	u32 btDeviceInfo = 0;

	pBtCoexist = (PBTC_COEXIST)pBtcContext;

	if (halbtcoutsrc_IsHwMailboxExist(pBtCoexist) == _TRUE) {
		u8 buf[3] = {0};
		_irqL irqL;
		u8 op_code;
		u8 status;

		_enter_critical_mutex(&GLBtcBtMpOperLock, &irqL);

		op_code = BT_OP_GET_BT_DEVICE_INFO;
		status = _btmpoper_cmd(pBtCoexist, op_code, 0, buf, 0);
		if (status == BT_STATUS_BT_OP_SUCCESS)
			btDeviceInfo = le32_to_cpu(*(u32 *)GLBtcBtMpRptRsp);
		else
			ret = SET_BT_MP_OPER_RET(op_code, status);

		_exit_critical_mutex(&GLBtcBtMpOperLock, &irqL);

	} else
		ret = BT_STATUS_NOT_IMPLEMENT;

	return btDeviceInfo;
}

u32 halbtcoutsrc_GetBtForbiddenSlotVal(void *pBtcContext)
{
	PBTC_COEXIST pBtCoexist;
	u32 ret = BT_STATUS_BT_OP_SUCCESS;
	u32 btForbiddenSlotVal = 0;

	pBtCoexist = (PBTC_COEXIST)pBtcContext;

	if (halbtcoutsrc_IsHwMailboxExist(pBtCoexist) == _TRUE) {
		u8 buf[3] = {0};
		_irqL irqL;
		u8 op_code;
		u8 status;

		_enter_critical_mutex(&GLBtcBtMpOperLock, &irqL);

		op_code = BT_OP_GET_BT_FORBIDDEN_SLOT_VAL;
		status = _btmpoper_cmd(pBtCoexist, op_code, 0, buf, 0);
		if (status == BT_STATUS_BT_OP_SUCCESS)
			btForbiddenSlotVal = le32_to_cpu(*(u32 *)GLBtcBtMpRptRsp);
		else
			ret = SET_BT_MP_OPER_RET(op_code, status);

		_exit_critical_mutex(&GLBtcBtMpOperLock, &irqL);

	} else
		ret = BT_STATUS_NOT_IMPLEMENT;

	return btForbiddenSlotVal;
}

static u8 halbtcoutsrc_GetWifiScanAPNum(PADAPTER padapter)
{
	struct mlme_priv *pmlmepriv;
	struct mlme_ext_priv *pmlmeext;
	static u8 scan_AP_num = 0;


	pmlmepriv = &padapter->mlmepriv;
	pmlmeext = &padapter->mlmeextpriv;

	if (GLBtcWiFiInScanState == _FALSE) {
		if (pmlmepriv->num_of_scanned > 0xFF)
			scan_AP_num = 0xFF;
		else
			scan_AP_num = (u8)pmlmepriv->num_of_scanned;
	}

	return scan_AP_num;
}

u8 halbtcoutsrc_Get(void *pBtcContext, u8 getType, void *pOutBuf)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;
	PHAL_DATA_TYPE pHalData;
	struct mlme_ext_priv *mlmeext;
	struct btc_wifi_link_info *wifi_link_info;
	u8 bSoftApExist, bVwifiExist;
	u8 *pu8;
	s32 *pS4Tmp;
	u32 *pU4Tmp;
	u8 *pU1Tmp;
	u16 *pU2Tmp;
	u8 ret;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return _FALSE;

	padapter = pBtCoexist->Adapter;
	pHalData = GET_HAL_DATA(padapter);
	mlmeext = &padapter->mlmeextpriv;
	bSoftApExist = _FALSE;
	bVwifiExist = _FALSE;
	pu8 = (u8 *)pOutBuf;
	pS4Tmp = (s32 *)pOutBuf;
	pU4Tmp = (u32 *)pOutBuf;
	pU1Tmp = (u8 *)pOutBuf;
	pU2Tmp = (u16*)pOutBuf;
	wifi_link_info = (struct btc_wifi_link_info *)pOutBuf;
	ret = _TRUE;

	switch (getType) {
	case BTC_GET_BL_HS_OPERATION:
		*pu8 = _FALSE;
		ret = _FALSE;
		break;

	case BTC_GET_BL_HS_CONNECTING:
		*pu8 = _FALSE;
		ret = _FALSE;
		break;

	case BTC_GET_BL_WIFI_FW_READY:
		*pu8 = halbtcoutsrc_is_fw_ready(pBtCoexist);
		break;

	case BTC_GET_BL_WIFI_CONNECTED:
		*pu8 = (rtw_mi_check_status(padapter, MI_LINKED)) ? _TRUE : _FALSE;
		break;

	case BTC_GET_BL_WIFI_DUAL_BAND_CONNECTED:
		*pu8 = halbtcoutsrc_IsDualBandConnected(padapter);
		break;

	case BTC_GET_BL_WIFI_BUSY:
		*pu8 = halbtcoutsrc_IsWifiBusy(padapter);
		break;

	case BTC_GET_BL_WIFI_SCAN:
#if 0
		*pu8 = (rtw_mi_check_fwstate(padapter, WIFI_SITE_MONITOR)) ? _TRUE : _FALSE;
#else
		/* Use the value of the new variable GLBtcWiFiInScanState to judge whether WiFi is in scan state or not, since the originally used flag
			WIFI_SITE_MONITOR in fwstate may not be cleared in time */
		*pu8 = GLBtcWiFiInScanState;
#endif
		break;

	case BTC_GET_BL_WIFI_LINK:
		*pu8 = (rtw_mi_check_status(padapter, MI_STA_LINKING)) ? _TRUE : _FALSE;
		break;

	case BTC_GET_BL_WIFI_ROAM:
		*pu8 = (rtw_mi_check_status(padapter, MI_STA_LINKING)) ? _TRUE : _FALSE;
		break;

	case BTC_GET_BL_WIFI_4_WAY_PROGRESS:
		*pu8 = _FALSE;
		break;

	case BTC_GET_BL_WIFI_UNDER_5G:
		*pu8 = (pHalData->current_band_type == BAND_ON_5G) ? _TRUE : _FALSE;
		break;

	case BTC_GET_BL_WIFI_AP_MODE_ENABLE:
		*pu8 = (rtw_mi_check_status(padapter, MI_AP_MODE)) ? _TRUE : _FALSE;
		break;

	case BTC_GET_BL_WIFI_ENABLE_ENCRYPTION:
		*pu8 = padapter->securitypriv.dot11PrivacyAlgrthm == 0 ? _FALSE : _TRUE;
		break;

	case BTC_GET_BL_WIFI_UNDER_B_MODE:
		if (mlmeext->cur_wireless_mode == WIRELESS_11B)
			*pu8 = _TRUE;
		else
			*pu8 = _FALSE;
		break;

	case BTC_GET_BL_WIFI_IS_IN_MP_MODE:
		if (padapter->registrypriv.mp_mode == 0)
			*pu8 = _FALSE;
		else
			*pu8 = _TRUE;
		break;

	case BTC_GET_BL_EXT_SWITCH:
		*pu8 = _FALSE;
		break;
	case BTC_GET_BL_IS_ASUS_8723B:
		/* Always return FALSE in linux driver since this case is added only for windows driver */
		*pu8 = _FALSE;
		break;

	case BTC_GET_BL_RF4CE_CONNECTED:
#ifdef CONFIG_RF4CE_COEXIST
		if (hal_btcoex_get_rf4ce_link_state() == 0)
			*pu8 = FALSE;
		else
			*pu8 = TRUE;
#else
		*pu8 = FALSE;
#endif
		break;

	case BTC_GET_BL_WIFI_LW_PWR_STATE:
		/* return false due to coex do not run during 32K */
		*pu8 = FALSE;
		break;

	case BTC_GET_S4_WIFI_RSSI:
		*pS4Tmp = halbtcoutsrc_GetWifiRssi(padapter);
		break;

	case BTC_GET_S4_HS_RSSI:
		*pS4Tmp = 0;
		ret = _FALSE;
		break;

	case BTC_GET_U4_WIFI_BW:
		if (IsLegacyOnly(mlmeext->cur_wireless_mode))
			*pU4Tmp = BTC_WIFI_BW_LEGACY;
		else {
			switch (pHalData->current_channel_bw) {
			case CHANNEL_WIDTH_20:
				*pU4Tmp = BTC_WIFI_BW_HT20;
				break;
			case CHANNEL_WIDTH_40:
				*pU4Tmp = BTC_WIFI_BW_HT40;
				break;
			case CHANNEL_WIDTH_80:
				*pU4Tmp = BTC_WIFI_BW_HT80;
				break;
			case CHANNEL_WIDTH_160:
				*pU4Tmp = BTC_WIFI_BW_HT160;
				break;
			default:
				RTW_INFO("[BTCOEX] unknown bandwidth(%d)\n", pHalData->current_channel_bw);
				*pU4Tmp = BTC_WIFI_BW_HT40;
				break;
			}

		}
		break;

	case BTC_GET_U4_WIFI_TRAFFIC_DIRECTION: {
		PRT_LINK_DETECT_T plinkinfo;
		plinkinfo = &padapter->mlmepriv.LinkDetectInfo;

		if (plinkinfo->NumTxOkInPeriod > plinkinfo->NumRxOkInPeriod)
			*pU4Tmp = BTC_WIFI_TRAFFIC_TX;
		else
			*pU4Tmp = BTC_WIFI_TRAFFIC_RX;
	}
		break;

	case BTC_GET_U4_WIFI_FW_VER:
		*pU4Tmp = pHalData->firmware_version << 16;
		*pU4Tmp |= pHalData->firmware_sub_version;
		break;

	case BTC_GET_U4_WIFI_LINK_STATUS:
		*pU4Tmp = halbtcoutsrc_GetWifiLinkStatus(pBtCoexist);
		break;
	case BTC_GET_BL_WIFI_LINK_INFO:
		*wifi_link_info = halbtcoutsrc_getwifilinkinfo(pBtCoexist);
		break;
	case BTC_GET_U4_BT_PATCH_VER:
		*pU4Tmp = halbtcoutsrc_GetBtPatchVer(pBtCoexist);
		break;

	case BTC_GET_U4_VENDOR:
		*pU4Tmp = BTC_VENDOR_OTHER;
		break;

	case BTC_GET_U4_SUPPORTED_VERSION:
		*pU4Tmp = halbtcoutsrc_GetBtCoexSupportedVersion(pBtCoexist);
		break;
	case BTC_GET_U4_SUPPORTED_FEATURE:
		*pU4Tmp = halbtcoutsrc_GetBtCoexSupportedFeature(pBtCoexist);
		break;

	case BTC_GET_U4_BT_DEVICE_INFO:
		*pU4Tmp = halbtcoutsrc_GetBtDeviceInfo(pBtCoexist);
		break;

	case BTC_GET_U4_BT_FORBIDDEN_SLOT_VAL:
		*pU4Tmp = halbtcoutsrc_GetBtForbiddenSlotVal(pBtCoexist);
		break;

	case BTC_GET_U4_WIFI_IQK_TOTAL:
		*pU4Tmp = pHalData->odmpriv.n_iqk_cnt;
		break;

	case BTC_GET_U4_WIFI_IQK_OK:
		*pU4Tmp = pHalData->odmpriv.n_iqk_ok_cnt;
		break;

	case BTC_GET_U4_WIFI_IQK_FAIL:
		*pU4Tmp = pHalData->odmpriv.n_iqk_fail_cnt;
		break;

	case BTC_GET_U1_WIFI_DOT11_CHNL:
		*pU1Tmp = padapter->mlmeextpriv.cur_channel;
		break;

	case BTC_GET_U1_WIFI_CENTRAL_CHNL:
		*pU1Tmp = pHalData->current_channel;
		break;

	case BTC_GET_U1_WIFI_HS_CHNL:
		*pU1Tmp = 0;
		ret = _FALSE;
		break;

	case BTC_GET_U1_WIFI_P2P_CHNL:
#ifdef CONFIG_P2P
		{
			struct wifidirect_info *pwdinfo = &(padapter->wdinfo);
			
			*pU1Tmp = pwdinfo->operating_channel;
		}
#else
		*pU1Tmp = 0;
#endif
		break;

	case BTC_GET_U1_MAC_PHY_MODE:
		/*			*pU1Tmp = BTC_SMSP;
		 *			*pU1Tmp = BTC_DMSP;
		 *			*pU1Tmp = BTC_DMDP;
		 *			*pU1Tmp = BTC_MP_UNKNOWN; */
		break;

	case BTC_GET_U1_AP_NUM:
		*pU1Tmp = halbtcoutsrc_GetWifiScanAPNum(padapter);
		break;
	case BTC_GET_U1_ANT_TYPE:
		switch (pHalData->bt_coexist.btAntisolation) {
		case 0:
			*pU1Tmp = (u8)BTC_ANT_TYPE_0;
			pBtCoexist->board_info.ant_type = (u8)BTC_ANT_TYPE_0;
			break;
		case 1:
			*pU1Tmp = (u8)BTC_ANT_TYPE_1;
			pBtCoexist->board_info.ant_type = (u8)BTC_ANT_TYPE_1;
			break;
		case 2:
			*pU1Tmp = (u8)BTC_ANT_TYPE_2;
			pBtCoexist->board_info.ant_type = (u8)BTC_ANT_TYPE_2;
			break;
		case 3:
			*pU1Tmp = (u8)BTC_ANT_TYPE_3;
			pBtCoexist->board_info.ant_type = (u8)BTC_ANT_TYPE_3;
			break;
		case 4:
			*pU1Tmp = (u8)BTC_ANT_TYPE_4;
			pBtCoexist->board_info.ant_type = (u8)BTC_ANT_TYPE_4;
			break;
		}
		break;
	case BTC_GET_U1_IOT_PEER:
		*pU1Tmp = mlmeext->mlmext_info.assoc_AP_vendor;
		break;

	/* =======1Ant=========== */
	case BTC_GET_U1_LPS_MODE:
		*pU1Tmp = padapter->dvobj->pwrctl_priv.pwr_mode;
		break;

	case BTC_GET_U2_BEACON_PERIOD:
		*pU2Tmp = mlmeext->mlmext_info.bcn_interval;
		break;

	default:
		ret = _FALSE;
		break;
	}

	return ret;
}

u16 halbtcoutsrc_LnaConstrainLvl(void *pBtcContext, u8 *lna_constrain_level)
{
	PBTC_COEXIST pBtCoexist;
	u16 ret = BT_STATUS_BT_OP_SUCCESS;

	pBtCoexist = (PBTC_COEXIST)pBtcContext;

	if (halbtcoutsrc_IsHwMailboxExist(pBtCoexist) == _TRUE) {
		_irqL irqL;
		u8 op_code;

		_enter_critical_mutex(&GLBtcBtMpOperLock, &irqL);

		ret = _btmpoper_cmd(pBtCoexist, BT_OP_SET_BT_LANCONSTRAIN_LEVEL, 0, lna_constrain_level, 1);

		_exit_critical_mutex(&GLBtcBtMpOperLock, &irqL);
	} else { 
		ret = BT_STATUS_NOT_IMPLEMENT;
		RTW_INFO("%s halbtcoutsrc_IsHwMailboxExist(pBtCoexist) == FALSE\n", __func__);
	}

	return ret;
}

u8 halbtcoutsrc_Set(void *pBtcContext, u8 setType, void *pInBuf)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;
	PHAL_DATA_TYPE pHalData;
	u8 *pu8;
	u8 *pU1Tmp;
	u32	*pU4Tmp;
	u8 ret;
	u8 result = _TRUE;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return _FALSE;

	padapter = pBtCoexist->Adapter;
	pHalData = GET_HAL_DATA(padapter);
	pu8 = (u8 *)pInBuf;
	pU1Tmp = (u8 *)pInBuf;
	pU4Tmp = (u32 *)pInBuf;
	ret = _TRUE;

	switch (setType) {
	/* set some u8 type variables. */
	case BTC_SET_BL_BT_DISABLE:
		pBtCoexist->bt_info.bt_disabled = *pu8;
		break;

	case BTC_SET_BL_BT_ENABLE_DISABLE_CHANGE:
		pBtCoexist->bt_info.bt_enable_disable_change = *pu8;
		break;

	case BTC_SET_BL_BT_TRAFFIC_BUSY:
		pBtCoexist->bt_info.bt_busy = *pu8;
		break;

	case BTC_SET_BL_BT_LIMITED_DIG:
		pBtCoexist->bt_info.limited_dig = *pu8;
		break;

	case BTC_SET_BL_FORCE_TO_ROAM:
		pBtCoexist->bt_info.force_to_roam = *pu8;
		break;

	case BTC_SET_BL_TO_REJ_AP_AGG_PKT:
		pBtCoexist->bt_info.reject_agg_pkt = *pu8;
		break;

	case BTC_SET_BL_BT_CTRL_AGG_SIZE:
		pBtCoexist->bt_info.bt_ctrl_agg_buf_size = *pu8;
		break;

	case BTC_SET_BL_INC_SCAN_DEV_NUM:
		pBtCoexist->bt_info.increase_scan_dev_num = *pu8;
		break;

	case BTC_SET_BL_BT_TX_RX_MASK:
		pBtCoexist->bt_info.bt_tx_rx_mask = *pu8;
		break;

	case BTC_SET_BL_MIRACAST_PLUS_BT:
		pBtCoexist->bt_info.miracast_plus_bt = *pu8;
		break;

	/* set some u8 type variables. */
	case BTC_SET_U1_RSSI_ADJ_VAL_FOR_AGC_TABLE_ON:
		pBtCoexist->bt_info.rssi_adjust_for_agc_table_on = *pU1Tmp;
		break;

	case BTC_SET_U1_AGG_BUF_SIZE:
		pBtCoexist->bt_info.agg_buf_size = *pU1Tmp;
		break;

	/* the following are some action which will be triggered */
	case BTC_SET_ACT_GET_BT_RSSI:
#if 0
		BT_SendGetBtRssiEvent(padapter);
#else
		ret = _FALSE;
#endif
		break;

	case BTC_SET_ACT_AGGREGATE_CTRL:
		halbtcoutsrc_AggregationCheck(pBtCoexist);
		break;

	/* =======1Ant=========== */
	/* set some u8 type variables. */
	case BTC_SET_U1_RSSI_ADJ_VAL_FOR_1ANT_COEX_TYPE:
		pBtCoexist->bt_info.rssi_adjust_for_1ant_coex_type = *pU1Tmp;
		break;

	case BTC_SET_U1_LPS_VAL:
		pBtCoexist->bt_info.lps_val = *pU1Tmp;
		break;

	case BTC_SET_U1_RPWM_VAL:
		pBtCoexist->bt_info.rpwm_val = *pU1Tmp;
		break;

	/* the following are some action which will be triggered */
	case BTC_SET_ACT_LEAVE_LPS:
		result = halbtcoutsrc_LeaveLps(pBtCoexist);
		break;

	case BTC_SET_ACT_ENTER_LPS:
		halbtcoutsrc_EnterLps(pBtCoexist);
		break;

	case BTC_SET_ACT_NORMAL_LPS:
		halbtcoutsrc_NormalLps(pBtCoexist);
		break;

	case BTC_SET_ACT_PRE_NORMAL_LPS:
		halbtcoutsrc_Pre_NormalLps(pBtCoexist);
		break;

	case BTC_SET_ACT_POST_NORMAL_LPS:
		halbtcoutsrc_Post_NormalLps(pBtCoexist);
		break;

	case BTC_SET_ACT_DISABLE_LOW_POWER:
		halbtcoutsrc_DisableLowPower(pBtCoexist, *pu8);
		break;

	case BTC_SET_ACT_UPDATE_RAMASK:
		/*
		pBtCoexist->bt_info.ra_mask = *pU4Tmp;

		if (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE) == _TRUE) {
			struct sta_info *psta;
			PWLAN_BSSID_EX cur_network;

			cur_network = &padapter->mlmeextpriv.mlmext_info.network;
			psta = rtw_get_stainfo(&padapter->stapriv, cur_network->MacAddress);
			rtw_hal_update_ra_mask(psta);
		}
		*/
		break;

	case BTC_SET_ACT_SEND_MIMO_PS: {
		u8 newMimoPsMode = 3;
		struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
		struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

		/* *pU1Tmp = 0 use SM_PS static type */
		/* *pU1Tmp = 1 disable SM_PS */
		if (*pU1Tmp == 0)
			newMimoPsMode = WLAN_HT_CAP_SM_PS_STATIC;
		else if (*pU1Tmp == 1)
			newMimoPsMode = WLAN_HT_CAP_SM_PS_DISABLED;

		if (check_fwstate(&padapter->mlmepriv , WIFI_ASOC_STATE) == _TRUE) {
			/* issue_action_SM_PS(padapter, get_my_bssid(&(pmlmeinfo->network)), newMimoPsMode); */
			issue_action_SM_PS_wait_ack(padapter , get_my_bssid(&(pmlmeinfo->network)) , newMimoPsMode, 3 , 1);
		}
	}
	break;

	case BTC_SET_ACT_CTRL_BT_INFO:
#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
		{
			u8 dataLen = *pU1Tmp;
			u8 tmpBuf[BTC_TMP_BUF_SHORT];
			if (dataLen)
				_rtw_memcpy(tmpBuf, pU1Tmp + 1, dataLen);
			BT_SendEventExtBtInfoControl(padapter, dataLen, &tmpBuf[0]);
		}
#else /* !CONFIG_BT_COEXIST_SOCKET_TRX */
		ret = _FALSE;
#endif /* CONFIG_BT_COEXIST_SOCKET_TRX */
		break;

	case BTC_SET_ACT_CTRL_BT_COEX:
#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
		{
			u8 dataLen = *pU1Tmp;
			u8 tmpBuf[BTC_TMP_BUF_SHORT];
			if (dataLen)
				_rtw_memcpy(tmpBuf, pU1Tmp + 1, dataLen);
			BT_SendEventExtBtCoexControl(padapter, _FALSE, dataLen, &tmpBuf[0]);
		}
#else /* !CONFIG_BT_COEXIST_SOCKET_TRX */
		ret = _FALSE;
#endif /* CONFIG_BT_COEXIST_SOCKET_TRX */
		break;
	case BTC_SET_ACT_CTRL_8723B_ANT:
#if 0
		{
			u8	dataLen = *pU1Tmp;
			u8	tmpBuf[BTC_TMP_BUF_SHORT];
			if (dataLen)
				PlatformMoveMemory(&tmpBuf[0], pU1Tmp + 1, dataLen);
			BT_Set8723bAnt(Adapter, dataLen, &tmpBuf[0]);
		}
#else
		ret = _FALSE;
#endif
		break;
	case BTC_SET_BL_BT_LNA_CONSTRAIN_LEVEL:
		halbtcoutsrc_LnaConstrainLvl(pBtCoexist, pu8);
		break;
	/* ===================== */
	default:
		ret = _FALSE;
		break;
	}

	return result;
}

u8 halbtcoutsrc_UnderIps(PBTC_COEXIST pBtCoexist)
{
	PADAPTER padapter;
	struct pwrctrl_priv *pwrpriv;
	u8 bMacPwrCtrlOn;

	padapter = pBtCoexist->Adapter;
	pwrpriv = &padapter->dvobj->pwrctl_priv;
	bMacPwrCtrlOn = _FALSE;

	if ((_TRUE == pwrpriv->bips_processing)
	    && (IPS_NONE != pwrpriv->ips_mode_req)
	   )
		return _TRUE;

	if (rf_off == pwrpriv->rf_pwrstate)
		return _TRUE;

	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (_FALSE == bMacPwrCtrlOn)
		return _TRUE;

	return _FALSE;
}

u8 halbtcoutsrc_UnderLps(PBTC_COEXIST pBtCoexist)
{
	return GLBtcWiFiInLPS;
}

u8 halbtcoutsrc_Under32K(PBTC_COEXIST pBtCoexist)
{
	/* todo: the method to check whether wifi is under 32K or not */
	return _FALSE;
}

void halbtcoutsrc_DisplayCoexStatistics(PBTC_COEXIST pBtCoexist)
{
#if 0
	PADAPTER padapter = (PADAPTER)pBtCoexist->Adapter;
	PBT_MGNT pBtMgnt = &padapter->MgntInfo.BtInfo.BtMgnt;
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	u8 *cliBuf = pBtCoexist->cliBuf;
	u8			i, j;
	u8			tmpbuf[BTC_TMP_BUF_SHORT];


	if (gl_coex_offload.cnt_h2c_sent) {
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Coex h2c notify]============");
		CL_PRINTF(cliBuf);

		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = H2c(%d)/Ack(%d)", "Coex h2c/c2h overall statistics",
			gl_coex_offload.cnt_h2c_sent, gl_coex_offload.cnt_c2h_ack);
		for (j = 0; j < COL_STATUS_MAX; j++) {
			if (gl_coex_offload.status[j]) {
				CL_SPRINTF(tmpbuf, BTC_TMP_BUF_SHORT, ", %s:%d", coexH2cResultString[j], gl_coex_offload.status[j]);
				CL_STRNCAT(cliBuf, BT_TMP_BUF_SIZE, tmpbuf, BTC_TMP_BUF_SHORT);
			}
		}
		CL_PRINTF(cliBuf);
	}
	for (i = 0; i < COL_OP_WIFI_OPCODE_MAX; i++) {
		if (gl_coex_offload.h2c_record[i].count) {
			/*==========================================*/
			/*	H2C result statistics*/
			/*==========================================*/
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = total:%d", coexOpcodeString[i], gl_coex_offload.h2c_record[i].count);
			for (j = 0; j < COL_STATUS_MAX; j++) {
				if (gl_coex_offload.h2c_record[i].status[j]) {
					CL_SPRINTF(tmpbuf, BTC_TMP_BUF_SHORT, ", %s:%d", coexH2cResultString[j], gl_coex_offload.h2c_record[i].status[j]);
					CL_STRNCAT(cliBuf, BT_TMP_BUF_SIZE, tmpbuf, BTC_TMP_BUF_SHORT);
				}
			}
			CL_PRINTF(cliBuf);
			/*==========================================*/
			/*	H2C/C2H content*/
			/*==========================================*/
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = ", "H2C / C2H content");
			for (j = 0; j < gl_coex_offload.h2c_record[i].h2c_len; j++) {
				CL_SPRINTF(tmpbuf, BTC_TMP_BUF_SHORT, "%02x ", gl_coex_offload.h2c_record[i].h2c_buf[j]);
				CL_STRNCAT(cliBuf, BT_TMP_BUF_SIZE, tmpbuf, 3);
			}
			if (gl_coex_offload.h2c_record[i].c2h_ack_len) {
				CL_STRNCAT(cliBuf, BT_TMP_BUF_SIZE, "/ ", 2);
				for (j = 0; j < gl_coex_offload.h2c_record[i].c2h_ack_len; j++) {
					CL_SPRINTF(tmpbuf, BTC_TMP_BUF_SHORT, "%02x ", gl_coex_offload.h2c_record[i].c2h_ack_buf[j]);
					CL_STRNCAT(cliBuf, BT_TMP_BUF_SIZE, tmpbuf, 3);
				}
			}
			CL_PRINTF(cliBuf);
			/*==========================================*/
		}
	}

	if (gl_coex_offload.cnt_c2h_ind) {
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Coex c2h indication]============");
		CL_PRINTF(cliBuf);

		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = Ind(%d)", "C2H indication statistics",
			   gl_coex_offload.cnt_c2h_ind);
		for (j = 0; j < COL_STATUS_MAX; j++) {
			if (gl_coex_offload.c2h_ind_status[j]) {
				CL_SPRINTF(tmpbuf, BTC_TMP_BUF_SHORT, ", %s:%d", coexH2cResultString[j], gl_coex_offload.c2h_ind_status[j]);
				CL_STRNCAT(cliBuf, BT_TMP_BUF_SIZE, tmpbuf, BTC_TMP_BUF_SHORT);
			}
		}
		CL_PRINTF(cliBuf);
	}
	for (i = 0; i < COL_IND_MAX; i++) {
		if (gl_coex_offload.c2h_ind_record[i].count) {
			/*==========================================*/
			/*	H2C result statistics*/
			/*==========================================*/
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = total:%d", coexIndTypeString[i], gl_coex_offload.c2h_ind_record[i].count);
			for (j = 0; j < COL_STATUS_MAX; j++) {
				if (gl_coex_offload.c2h_ind_record[i].status[j]) {
					CL_SPRINTF(tmpbuf, BTC_TMP_BUF_SHORT, ", %s:%d", coexH2cResultString[j], gl_coex_offload.c2h_ind_record[i].status[j]);
					CL_STRNCAT(cliBuf, BT_TMP_BUF_SIZE, tmpbuf, BTC_TMP_BUF_SHORT);
				}
			}
			CL_PRINTF(cliBuf);
			/*==========================================*/
			/*	content*/
			/*==========================================*/
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = ", "C2H indication content");
			for (j = 0; j < gl_coex_offload.c2h_ind_record[i].ind_len; j++) {
				CL_SPRINTF(tmpbuf, BTC_TMP_BUF_SHORT, "%02x ", gl_coex_offload.c2h_ind_record[i].ind_buf[j]);
				CL_STRNCAT(cliBuf, BT_TMP_BUF_SIZE, tmpbuf, 3);
			}
			CL_PRINTF(cliBuf);
			/*==========================================*/
		}
	}

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Statistics]============");
	CL_PRINTF(cliBuf);

#if (H2C_USE_IO_THREAD != 1)
	for (i = 0; i < H2C_STATUS_MAX; i++) {
		if (pHalData->h2cStatistics[i]) {
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = [%s] = %d", "H2C statistics", \
				   h2cStaString[i], pHalData->h2cStatistics[i]);
			CL_PRINTF(cliBuf);
		}
	}
#else
	for (i = 0; i < IO_STATUS_MAX; i++) {
		if (Adapter->ioComStr.ioH2cStatistics[i]) {
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = [%s] = %d", "H2C statistics", \
				ioStaString[i], Adapter->ioComStr.ioH2cStatistics[i]);
			CL_PRINTF(cliBuf);
		}
	}
#endif
#if 0
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "lastHMEBoxNum", \
		   pHalData->LastHMEBoxNum);
	CL_PRINTF(cliBuf);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x / 0x%x", "LastOkH2c/FirstFailH2c(fwNotRead)", \
		   pHalData->lastSuccessH2cEid, pHalData->firstFailedH2cEid);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d/ %d", "c2hIsr/c2hIntr/clr1AF/noRdy/noBuf", \
		pHalData->InterruptLog.nIMR_C2HCMD, DBG_Var.c2hInterruptCnt, DBG_Var.c2hClrReadC2hCnt,
		   DBG_Var.c2hNotReadyCnt, DBG_Var.c2hBufAlloFailCnt);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "c2hPacket", \
		   DBG_Var.c2hPacketCnt);
	CL_PRINTF(cliBuf);
#endif
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d", "Periodical/ DbgCtrl", \
		pBtCoexist->statistics.cntPeriodical, pBtCoexist->statistics.cntDbgCtrl);
	CL_PRINTF(cliBuf);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d", "PowerOn/InitHw/InitCoexDm/RfStatus", \
		pBtCoexist->statistics.cntPowerOn, pBtCoexist->statistics.cntInitHwConfig, pBtCoexist->statistics.cntInitCoexDm,
		   pBtCoexist->statistics.cntRfStatusNotify);
	CL_PRINTF(cliBuf);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d/ %d", "Ips/Lps/Scan/Connect/Mstatus", \
		pBtCoexist->statistics.cntIpsNotify, pBtCoexist->statistics.cntLpsNotify,
		pBtCoexist->statistics.cntScanNotify, pBtCoexist->statistics.cntConnectNotify,
		   pBtCoexist->statistics.cntMediaStatusNotify);
	CL_PRINTF(cliBuf);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d", "Special pkt/Bt info/ bind",
		pBtCoexist->statistics.cntSpecialPacketNotify, pBtCoexist->statistics.cntBtInfoNotify,
		   pBtCoexist->statistics.cntBind);
	CL_PRINTF(cliBuf);
#endif
	PADAPTER		padapter = pBtCoexist->Adapter;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8				*cliBuf = pBtCoexist->cli_buf;

	if (pHalData->EEPROMBluetoothCoexist == 1) {
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Coex Status]============");
		CL_PRINTF(cliBuf);
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "IsBtDisabled", rtw_btcoex_IsBtDisabled(padapter));
		CL_PRINTF(cliBuf);
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "IsBtControlLps", rtw_btcoex_IsBtControlLps(padapter));
		CL_PRINTF(cliBuf);
	}
}

void halbtcoutsrc_DisplayBtLinkInfo(PBTC_COEXIST pBtCoexist)
{
#if 0
	PADAPTER padapter = (PADAPTER)pBtCoexist->Adapter;
	PBT_MGNT pBtMgnt = &padapter->MgntInfo.BtInfo.BtMgnt;
	u8 *cliBuf = pBtCoexist->cliBuf;
	u8 i;


	if (pBtCoexist->stack_info.profile_notified) {
		for (i = 0; i < pBtMgnt->ExtConfig.NumberOfACL; i++) {
			if (pBtMgnt->ExtConfig.HCIExtensionVer >= 1) {
				CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s/ %s", "Bt link type/spec/role", \
					BtProfileString[pBtMgnt->ExtConfig.aclLink[i].BTProfile],
					BtSpecString[pBtMgnt->ExtConfig.aclLink[i].BTCoreSpec],
					BtLinkRoleString[pBtMgnt->ExtConfig.aclLink[i].linkRole]);
				CL_PRINTF(cliBuf);
			} else {
				CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s", "Bt link type/spec", \
					BtProfileString[pBtMgnt->ExtConfig.aclLink[i].BTProfile],
					BtSpecString[pBtMgnt->ExtConfig.aclLink[i].BTCoreSpec]);
				CL_PRINTF(cliBuf);
			}
		}
	}
#endif
}

void halbtcoutsrc_DisplayWifiStatus(PBTC_COEXIST pBtCoexist)
{
	PADAPTER	padapter = pBtCoexist->Adapter;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	u8			*cliBuf = pBtCoexist->cli_buf;
	s32			wifiRssi = 0, btHsRssi = 0;
	BOOLEAN	bScan = _FALSE, bLink = _FALSE, bRoam = _FALSE, bWifiBusy = _FALSE, bWifiUnderBMode = _FALSE;
	u32			wifiBw = BTC_WIFI_BW_HT20, wifiTrafficDir = BTC_WIFI_TRAFFIC_TX, wifiFreq = BTC_FREQ_2_4G;
	u32			wifiLinkStatus = 0x0;
	BOOLEAN	bBtHsOn = _FALSE, bLowPower = _FALSE;
	u8			wifiChnl = 0, wifiP2PChnl = 0, nScanAPNum = 0, FwPSState;
	u32			iqk_cnt_total = 0, iqk_cnt_ok = 0, iqk_cnt_fail = 0;
	u16			wifiBcnInterval = 0;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(padapter);
	struct btc_wifi_link_info wifi_link_info;

	wifi_link_info = halbtcoutsrc_getwifilinkinfo(pBtCoexist);

	switch (wifi_link_info.link_mode) {
		case BTC_LINK_NONE:
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d/ %d/ %d", "WifiLinkMode/HotSpa/Noa/ClientJoin",
					"None", wifi_link_info.bhotspot, wifi_link_info.benable_noa, wifi_link_info.bany_client_join_go);
			wifiFreq = hal->current_channel > 14 ?  BTC_FREQ_5G : BTC_FREQ_2_4G;
			break;
		case BTC_LINK_ONLY_GO:
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d/ %d/ %d", "WifiLinkMode/HotSpa/Noa/ClientJoin",
					"ONLY_GO", wifi_link_info.bhotspot, wifi_link_info.benable_noa, wifi_link_info.bany_client_join_go);
			wifiFreq = hal->current_channel > 14 ?  BTC_FREQ_5G : BTC_FREQ_2_4G;
			break;
		case BTC_LINK_ONLY_GC:
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d/ %d/ %d", "WifiLinkMode/HotSpa/Noa/ClientJoin",
					"ONLY_GC", wifi_link_info.bhotspot, wifi_link_info.benable_noa, wifi_link_info.bany_client_join_go);
			wifiFreq = hal->current_channel > 14 ?  BTC_FREQ_5G : BTC_FREQ_2_4G;
			break;
		case BTC_LINK_ONLY_STA:
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d/ %d/ %d", "WifiLinkMode/HotSpa/Noa/ClientJoin",
					"ONLY_STA", wifi_link_info.bhotspot, wifi_link_info.benable_noa, wifi_link_info.bany_client_join_go);
			wifiFreq = hal->current_channel > 14 ?  BTC_FREQ_5G : BTC_FREQ_2_4G;
			break;
		case BTC_LINK_ONLY_AP:
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d/ %d/ %d", "WifiLinkMode/HotSpa/Noa/ClientJoin",
					"ONLY_AP", wifi_link_info.bhotspot, wifi_link_info.benable_noa, wifi_link_info.bany_client_join_go);
			wifiFreq = hal->current_channel > 14 ?  BTC_FREQ_5G : BTC_FREQ_2_4G;
			break;
		case BTC_LINK_2G_MCC_GO_STA:
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d/ %d/ %d", "WifiLinkMode/HotSpa/Noa/ClientJoin",
					"24G_MCC_GO_STA", wifi_link_info.bhotspot, wifi_link_info.benable_noa, wifi_link_info.bany_client_join_go);
			wifiFreq = BTC_FREQ_2_4G;
			break;
		case BTC_LINK_5G_MCC_GO_STA:
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d/ %d/ %d", "WifiLinkMode/HotSpa/Noa/ClientJoin",
					"5G_MCC_GO_STA", wifi_link_info.bhotspot, wifi_link_info.benable_noa, wifi_link_info.bany_client_join_go);
			wifiFreq = BTC_FREQ_5G;
			break;
		case BTC_LINK_25G_MCC_GO_STA:
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d/ %d/ %d", "WifiLinkMode/HotSpa/Noa/ClientJoin",
					"2BANDS_MCC_GO_STA", wifi_link_info.bhotspot, wifi_link_info.benable_noa, wifi_link_info.bany_client_join_go);
			wifiFreq = BTC_FREQ_25G;
			break;
		case BTC_LINK_2G_MCC_GC_STA:
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d/ %d/ %d", "WifiLinkMode/HotSpa/Noa/ClientJoin",
					"24G_MCC_GC_STA", wifi_link_info.bhotspot, wifi_link_info.benable_noa, wifi_link_info.bany_client_join_go);
			wifiFreq = BTC_FREQ_2_4G;
			break;
		case BTC_LINK_5G_MCC_GC_STA:
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d/ %d/ %d", "WifiLinkMode/HotSpa/Noa/ClientJoin",
					"5G_MCC_GC_STA", wifi_link_info.bhotspot, wifi_link_info.benable_noa, wifi_link_info.bany_client_join_go);
			wifiFreq = BTC_FREQ_5G;
			break;
		case BTC_LINK_25G_MCC_GC_STA:
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d/ %d/ %d", "WifiLinkMode/HotSpa/Noa/ClientJoin",
					"2BANDS_MCC_GC_STA", wifi_link_info.bhotspot, wifi_link_info.benable_noa, wifi_link_info.bany_client_join_go);
			wifiFreq = BTC_FREQ_25G;
			break;
		case BTC_LINK_2G_SCC_GO_STA:
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d/ %d/ %d", "WifiLinkMode/HotSpa/Noa/ClientJoin",
					"24G_SCC_GO_STA", wifi_link_info.bhotspot, wifi_link_info.benable_noa, wifi_link_info.bany_client_join_go);
			wifiFreq = BTC_FREQ_2_4G;
			break;
		case BTC_LINK_5G_SCC_GO_STA:
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d/ %d/ %d", "WifiLinkMode/HotSpa/Noa/ClientJoin",
					"5G_SCC_GO_STA", wifi_link_info.bhotspot, wifi_link_info.benable_noa, wifi_link_info.bany_client_join_go);
			wifiFreq = BTC_FREQ_5G;
			break;
		case BTC_LINK_2G_SCC_GC_STA:
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d/ %d/ %d", "WifiLinkMode/HotSpa/Noa/ClientJoin",
					"24G_SCC_GC_STA", wifi_link_info.bhotspot, wifi_link_info.benable_noa, wifi_link_info.bany_client_join_go);
			wifiFreq = BTC_FREQ_2_4G;
			break;
		case BTC_LINK_5G_SCC_GC_STA:
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d/ %d/ %d", "WifiLinkMode/HotSpa/Noa/ClientJoin",
					"5G_SCC_GC_STA", wifi_link_info.bhotspot, wifi_link_info.benable_noa, wifi_link_info.bany_client_join_go);
			wifiFreq = BTC_FREQ_5G;
			break;
		default:
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d/ %d/ %d", "WifiLinkMode/HotSpa/Noa/ClientJoin",
					"UNKNOWN", wifi_link_info.bhotspot, wifi_link_info.benable_noa, wifi_link_info.bany_client_join_go);
			wifiFreq = hal->current_channel > 14 ?  BTC_FREQ_5G : BTC_FREQ_2_4G;
			break;
	}

	CL_PRINTF(cliBuf);

	wifiLinkStatus = halbtcoutsrc_GetWifiLinkStatus(pBtCoexist);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d/ %d", "STA/vWifi/HS/p2pGo/p2pGc",
		((wifiLinkStatus & WIFI_STA_CONNECTED) ? 1 : 0), ((wifiLinkStatus & WIFI_AP_CONNECTED) ? 1 : 0),
		((wifiLinkStatus & WIFI_HS_CONNECTED) ? 1 : 0), ((wifiLinkStatus & WIFI_P2P_GO_CONNECTED) ? 1 : 0),
		((wifiLinkStatus & WIFI_P2P_GC_CONNECTED) ? 1 : 0));
	CL_PRINTF(cliBuf);

	pBtCoexist->btc_get(pBtCoexist, BTC_GET_BL_WIFI_SCAN, &bScan);
	pBtCoexist->btc_get(pBtCoexist, BTC_GET_BL_WIFI_LINK, &bLink);
	pBtCoexist->btc_get(pBtCoexist, BTC_GET_BL_WIFI_ROAM, &bRoam);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d ", "Link/ Roam/ Scan",
		bLink, bRoam, bScan);
	CL_PRINTF(cliBuf);	

	pBtCoexist->btc_get(pBtCoexist, BTC_GET_U4_WIFI_IQK_TOTAL, &iqk_cnt_total);
	pBtCoexist->btc_get(pBtCoexist, BTC_GET_U4_WIFI_IQK_OK, &iqk_cnt_ok);
	pBtCoexist->btc_get(pBtCoexist, BTC_GET_U4_WIFI_IQK_FAIL, &iqk_cnt_fail);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d %s %s",
		"IQK All/ OK/ Fail/AutoLoad/FWDL", iqk_cnt_total, iqk_cnt_ok, iqk_cnt_fail,
		((halbtcoutsrc_is_autoload_fail(pBtCoexist) == _TRUE) ? "fail":"ok"), ((halbtcoutsrc_is_fw_ready(pBtCoexist) == _TRUE) ? "ok":"fail"));
	CL_PRINTF(cliBuf);
	
	if (wifiLinkStatus & WIFI_STA_CONNECTED) {
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s", "IOT Peer", GLBtcIotPeerString[padapter->mlmeextpriv.mlmext_info.assoc_AP_vendor]);
		CL_PRINTF(cliBuf);
	}

	pBtCoexist->btc_get(pBtCoexist, BTC_GET_S4_WIFI_RSSI, &wifiRssi);
	pBtCoexist->btc_get(pBtCoexist, BTC_GET_U2_BEACON_PERIOD, &wifiBcnInterval);
	wifiChnl = wifi_link_info.sta_center_channel;
	wifiP2PChnl = wifi_link_info.p2p_center_channel;

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d dBm/ %d/ %d/ %d", "RSSI/ STA_Chnl/ P2P_Chnl/ BI",
		wifiRssi-100, wifiChnl, wifiP2PChnl, wifiBcnInterval);
	CL_PRINTF(cliBuf);

	pBtCoexist->btc_get(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	pBtCoexist->btc_get(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	pBtCoexist->btc_get(pBtCoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION, &wifiTrafficDir);
	pBtCoexist->btc_get(pBtCoexist, BTC_GET_BL_WIFI_UNDER_B_MODE, &bWifiUnderBMode);
	pBtCoexist->btc_get(pBtCoexist, BTC_GET_U1_AP_NUM, &nScanAPNum);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s / %s/ %s/ %d ", "Band/ BW/ Traffic/ APCnt",
		GLBtcWifiFreqString[wifiFreq], ((bWifiUnderBMode) ? "11b" : GLBtcWifiBwString[wifiBw]),
		((!bWifiBusy) ? "idle" : ((BTC_WIFI_TRAFFIC_TX == wifiTrafficDir) ? "uplink" : "downlink")),
		   nScanAPNum);
	CL_PRINTF(cliBuf);

	/* power status */
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s%s%s", "Power Status", \
		((halbtcoutsrc_UnderIps(pBtCoexist) == _TRUE) ? "IPS ON" : "IPS OFF"),
		((halbtcoutsrc_UnderLps(pBtCoexist) == _TRUE) ? ", LPS ON" : ", LPS OFF"),
		((halbtcoutsrc_Under32K(pBtCoexist) == _TRUE) ? ", 32k" : ""));
	CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x %02x %02x %02x (0x%x/0x%x)", "Power mode cmd(lps/rpwm)",
		   pBtCoexist->pwrModeVal[0], pBtCoexist->pwrModeVal[1],
		   pBtCoexist->pwrModeVal[2], pBtCoexist->pwrModeVal[3],
		   pBtCoexist->pwrModeVal[4], pBtCoexist->pwrModeVal[5],
		   pBtCoexist->bt_info.lps_val,
		   pBtCoexist->bt_info.rpwm_val);
	CL_PRINTF(cliBuf);
}

void halbtcoutsrc_DisplayDbgMsg(void *pBtcContext, u8 dispType)
{
	PBTC_COEXIST pBtCoexist;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	switch (dispType) {
	case BTC_DBG_DISP_COEX_STATISTICS:
		halbtcoutsrc_DisplayCoexStatistics(pBtCoexist);
		break;
	case BTC_DBG_DISP_BT_LINK_INFO:
		halbtcoutsrc_DisplayBtLinkInfo(pBtCoexist);
		break;
	case BTC_DBG_DISP_WIFI_STATUS:
		halbtcoutsrc_DisplayWifiStatus(pBtCoexist);
		break;
	default:
		break;
	}
}

/* ************************************
 *		IO related function
 * ************************************ */
u8 halbtcoutsrc_Read1Byte(void *pBtcContext, u32 RegAddr)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	return rtw_read8(padapter, RegAddr);
}

u16 halbtcoutsrc_Read2Byte(void *pBtcContext, u32 RegAddr)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	return	rtw_read16(padapter, RegAddr);
}

u32 halbtcoutsrc_Read4Byte(void *pBtcContext, u32 RegAddr)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	return	rtw_read32(padapter, RegAddr);
}

void halbtcoutsrc_Write1Byte(void *pBtcContext, u32 RegAddr, u8 Data)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	rtw_write8(padapter, RegAddr, Data);
}

void halbtcoutsrc_BitMaskWrite1Byte(void *pBtcContext, u32 regAddr, u8 bitMask, u8 data1b)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;
	u8 originalValue, bitShift;
	u8 i;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;
	originalValue = 0;
	bitShift = 0;

	if (bitMask != 0xff) {
		originalValue = rtw_read8(padapter, regAddr);

		for (i = 0; i <= 7; i++) {
			if ((bitMask >> i) & 0x1)
				break;
		}
		bitShift = i;

		data1b = (originalValue & ~bitMask) | ((data1b << bitShift) & bitMask);
	}

	rtw_write8(padapter, regAddr, data1b);
}

void halbtcoutsrc_Write2Byte(void *pBtcContext, u32 RegAddr, u16 Data)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	rtw_write16(padapter, RegAddr, Data);
}

void halbtcoutsrc_Write4Byte(void *pBtcContext, u32 RegAddr, u32 Data)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	rtw_write32(padapter, RegAddr, Data);
}

void halbtcoutsrc_WriteLocalReg1Byte(void *pBtcContext, u32 RegAddr, u8 Data)
{
	PBTC_COEXIST		pBtCoexist = (PBTC_COEXIST)pBtcContext;
	PADAPTER			Adapter = pBtCoexist->Adapter;

	if (BTC_INTF_SDIO == pBtCoexist->chip_interface)
		rtw_write8(Adapter, SDIO_LOCAL_BASE | RegAddr, Data);
	else
		rtw_write8(Adapter, RegAddr, Data);
}

void halbtcoutsrc_SetBbReg(void *pBtcContext, u32 RegAddr, u32 BitMask, u32 Data)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	phy_set_bb_reg(padapter, RegAddr, BitMask, Data);
}


u32 halbtcoutsrc_GetBbReg(void *pBtcContext, u32 RegAddr, u32 BitMask)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	return phy_query_bb_reg(padapter, RegAddr, BitMask);
}

void halbtcoutsrc_SetRfReg(void *pBtcContext, enum rf_path eRFPath, u32 RegAddr, u32 BitMask, u32 Data)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	phy_set_rf_reg(padapter, eRFPath, RegAddr, BitMask, Data);
}

u32 halbtcoutsrc_GetRfReg(void *pBtcContext, enum rf_path eRFPath, u32 RegAddr, u32 BitMask)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	return phy_query_rf_reg(padapter, eRFPath, RegAddr, BitMask);
}

u16 halbtcoutsrc_SetBtReg(void *pBtcContext, u8 RegType, u32 RegAddr, u32 Data)
{
	PBTC_COEXIST pBtCoexist;
	u16 ret = BT_STATUS_BT_OP_SUCCESS;

	pBtCoexist = (PBTC_COEXIST)pBtcContext;

	if (halbtcoutsrc_IsHwMailboxExist(pBtCoexist) == _TRUE) {
		u8 buf[3] = {0};
		_irqL irqL;
		u8 op_code;
		u8 status;

		_enter_critical_mutex(&GLBtcBtMpOperLock, &irqL);

		Data = cpu_to_le32(Data);
		op_code = BT_OP_WRITE_REG_VALUE;
		status = _btmpoper_cmd(pBtCoexist, op_code, 0, (u8 *)&Data, 3);
		if (status != BT_STATUS_BT_OP_SUCCESS)
			ret = SET_BT_MP_OPER_RET(op_code, status);
		else {
			buf[0] = RegType;
			*(u16 *)(buf + 1) = cpu_to_le16((u16)RegAddr);
			op_code = BT_OP_WRITE_REG_ADDR;
			status = _btmpoper_cmd(pBtCoexist, op_code, 0, buf, 3);
			if (status != BT_STATUS_BT_OP_SUCCESS)
				ret = SET_BT_MP_OPER_RET(op_code, status);
		}

		_exit_critical_mutex(&GLBtcBtMpOperLock, &irqL);
	} else
		ret = BT_STATUS_NOT_IMPLEMENT;

	return ret;
}

u8 halbtcoutsrc_SetBtAntDetection(void *pBtcContext, u8 txTime, u8 btChnl)
{
	/* Always return _FALSE since we don't implement this yet */
#if 0
	PBTC_COEXIST		pBtCoexist = (PBTC_COEXIST)pBtcContext;
	PADAPTER			Adapter = pBtCoexist->Adapter;
	u8				btCanTx = 0;
	BOOLEAN			bStatus = FALSE;

	bStatus = NDBG_SetBtAntDetection(Adapter, txTime, btChnl, &btCanTx);
	if (bStatus && btCanTx)
		return _TRUE;
	else
		return _FALSE;
#else
	return _FALSE;
#endif
}

BOOLEAN
halbtcoutsrc_SetBtTRXMASK(
		void			*pBtcContext,
		u8			bt_trx_mask
	)
{
	/* Always return _FALSE since we don't implement this yet */
#if 0
	struct btc_coexist *pBtCoexist = (struct btc_coexist *)pBtcContext;
	PADAPTER			Adapter = pBtCoexist->Adapter;
	BOOLEAN				bStatus = FALSE;
	u8				btCanTx = 0;

	if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter) || IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)
			|| IS_HARDWARE_TYPE_8821C(pBtCoexist->Adapter)) {

	if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter))
		bStatus = NDBG_SetBtTRXMASK(Adapter, 1, bt_trx_mask, &btCanTx);
	else
		bStatus = NDBG_SetBtTRXMASK(Adapter, 2, bt_trx_mask, &btCanTx);
	}

	
	if (bStatus)
		return TRUE;
	else
		return FALSE;
#else
	return _FALSE;
#endif
}

u16 halbtcoutsrc_GetBtReg_with_status(void *pBtcContext, u8 RegType, u32 RegAddr, u32 *data)
{
	PBTC_COEXIST pBtCoexist;
	u16 ret = BT_STATUS_BT_OP_SUCCESS;

	pBtCoexist = (PBTC_COEXIST)pBtcContext;

	if (halbtcoutsrc_IsHwMailboxExist(pBtCoexist) == _TRUE) {
		u8 buf[3] = {0};
		_irqL irqL;
		u8 op_code;
		u8 status;

		buf[0] = RegType;
		*(u16 *)(buf + 1) = cpu_to_le16((u16)RegAddr);

		_enter_critical_mutex(&GLBtcBtMpOperLock, &irqL);

		op_code = BT_OP_READ_REG;
		status = _btmpoper_cmd(pBtCoexist, op_code, 0, buf, 3);
		if (status == BT_STATUS_BT_OP_SUCCESS)
			*data = le16_to_cpu(*(u16 *)GLBtcBtMpRptRsp);
		else
			ret = SET_BT_MP_OPER_RET(op_code, status);

		_exit_critical_mutex(&GLBtcBtMpOperLock, &irqL);

	} else
		ret = BT_STATUS_NOT_IMPLEMENT;

	return ret;
}

u32 halbtcoutsrc_GetBtReg(void *pBtcContext, u8 RegType, u32 RegAddr)
{
	u32 regVal;
	
	return (BT_STATUS_BT_OP_SUCCESS == halbtcoutsrc_GetBtReg_with_status(pBtcContext, RegType, RegAddr, &regVal)) ? regVal : 0xffffffff;
}

u16 halbtcoutsrc_setbttestmode(void *pBtcContext, u8 Type)
{
	PBTC_COEXIST pBtCoexist;
	u16 ret = BT_STATUS_BT_OP_SUCCESS;

	pBtCoexist = (PBTC_COEXIST)pBtcContext;

	if (halbtcoutsrc_IsHwMailboxExist(pBtCoexist) == _TRUE) {
		_irqL irqL;
		u8 op_code;
		u8 status;

		_enter_critical_mutex(&GLBtcBtMpOperLock, &irqL);

		Type = cpu_to_le32(Type);
		op_code = BT_OP_SET_BT_TEST_MODE_VAL;
		status = _btmpoper_cmd(pBtCoexist, op_code, 0, (u8 *)&Type, 3);
		if (status != BT_STATUS_BT_OP_SUCCESS)
			ret = SET_BT_MP_OPER_RET(op_code, status);

		_exit_critical_mutex(&GLBtcBtMpOperLock, &irqL);
	} else
		ret = BT_STATUS_NOT_IMPLEMENT;

	return ret;

}


void halbtcoutsrc_FillH2cCmd(void *pBtcContext, u8 elementId, u32 cmdLen, u8 *pCmdBuffer)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	rtw_hal_fill_h2c_cmd(padapter, elementId, cmdLen, pCmdBuffer);
}

static void halbtcoutsrc_coex_offload_init(void)
{
	u8	i;

	gl_coex_offload.h2c_req_num = 0;
	gl_coex_offload.cnt_h2c_sent = 0;
	gl_coex_offload.cnt_c2h_ack = 0;
	gl_coex_offload.cnt_c2h_ind = 0;

	for (i = 0; i < COL_MAX_H2C_REQ_NUM; i++)
		init_completion(&gl_coex_offload.c2h_event[i]);
}

static COL_H2C_STATUS halbtcoutsrc_send_h2c(PADAPTER Adapter, PCOL_H2C pcol_h2c, u16 h2c_cmd_len)
{
	COL_H2C_STATUS		h2c_status = COL_STATUS_C2H_OK;
	u8				i;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0))
	reinit_completion(&gl_coex_offload.c2h_event[pcol_h2c->req_num]);		/* set event to un signaled state */
#else
	INIT_COMPLETION(gl_coex_offload.c2h_event[pcol_h2c->req_num]);
#endif

	if (TRUE) {
#if 0	/*(USE_HAL_MAC_API == 1) */
		if (RT_STATUS_SUCCESS == HAL_MAC_Send_BT_COEX(&GET_HAL_MAC_INFO(Adapter), (u8 *)(pcol_h2c), (u32)h2c_cmd_len, 1)) {
			if (!wait_for_completion_timeout(&gl_coex_offload.c2h_event[pcol_h2c->req_num], 20)) {
				h2c_status = COL_STATUS_H2C_TIMTOUT;
			}
		} else {
			h2c_status = COL_STATUS_H2C_HALMAC_FAIL;
		}
#endif
	}

	return h2c_status;
}

static COL_H2C_STATUS halbtcoutsrc_check_c2h_ack(PADAPTER Adapter, PCOL_SINGLE_H2C_RECORD pH2cRecord)
{
	COL_H2C_STATUS	c2h_status = COL_STATUS_C2H_OK;
	PCOL_H2C		p_h2c_cmd = (PCOL_H2C)&pH2cRecord->h2c_buf[0];
	u8			req_num = p_h2c_cmd->req_num;
	PCOL_C2H_ACK	p_c2h_ack = (PCOL_C2H_ACK)&gl_coex_offload.c2h_ack_buf[req_num];


	if ((COL_C2H_ACK_HDR_LEN + p_c2h_ack->ret_len) > gl_coex_offload.c2h_ack_len[req_num]) {
		c2h_status = COL_STATUS_COEX_DATA_OVERFLOW;
		return c2h_status;
	}
	/* else */
	{
		_rtw_memmove(&pH2cRecord->c2h_ack_buf[0], &gl_coex_offload.c2h_ack_buf[req_num], gl_coex_offload.c2h_ack_len[req_num]);
		pH2cRecord->c2h_ack_len = gl_coex_offload.c2h_ack_len[req_num];
	}


	if (p_c2h_ack->req_num != p_h2c_cmd->req_num) {
		c2h_status = COL_STATUS_C2H_REQ_NUM_MISMATCH;
	} else if (p_c2h_ack->opcode_ver != p_h2c_cmd->opcode_ver) {
		c2h_status = COL_STATUS_C2H_OPCODE_VER_MISMATCH;
	} else {
		c2h_status = p_c2h_ack->status;
	}

	return c2h_status;
}

COL_H2C_STATUS halbtcoutsrc_CoexH2cProcess(void *pBtCoexist,
		u8 opcode, u8 opcode_ver, u8 *ph2c_par, u8 h2c_par_len)
{
	PADAPTER			Adapter = ((struct btc_coexist *)pBtCoexist)->Adapter;
	u8				H2C_Parameter[BTC_TMP_BUF_SHORT] = {0};
	PCOL_H2C			pcol_h2c = (PCOL_H2C)&H2C_Parameter[0];
	u16				paraLen = 0;
	COL_H2C_STATUS		h2c_status = COL_STATUS_C2H_OK, c2h_status = COL_STATUS_C2H_OK;
	COL_H2C_STATUS		ret_status = COL_STATUS_C2H_OK;
	u16				i, col_h2c_len = 0;

	pcol_h2c->opcode = opcode;
	pcol_h2c->opcode_ver = opcode_ver;
	pcol_h2c->req_num = gl_coex_offload.h2c_req_num;
	gl_coex_offload.h2c_req_num++;
	gl_coex_offload.h2c_req_num %= 16;

	_rtw_memmove(&pcol_h2c->buf[0], ph2c_par, h2c_par_len);


	col_h2c_len = h2c_par_len + 2;	/* 2=sizeof(OPCode, OPCode_version and  Request number) */
	BT_PrintData(Adapter, "[COL], H2C cmd: ", col_h2c_len, H2C_Parameter);

	gl_coex_offload.cnt_h2c_sent++;

	gl_coex_offload.h2c_record[opcode].count++;
	gl_coex_offload.h2c_record[opcode].h2c_len = col_h2c_len;
	_rtw_memmove((void *)&gl_coex_offload.h2c_record[opcode].h2c_buf[0], (void *)pcol_h2c, col_h2c_len);

	h2c_status = halbtcoutsrc_send_h2c(Adapter, pcol_h2c, col_h2c_len);

	gl_coex_offload.h2c_record[opcode].c2h_ack_len = 0;

	if (COL_STATUS_C2H_OK == h2c_status) {
		/* if reach here, it means H2C get the correct c2h response, */
		c2h_status = halbtcoutsrc_check_c2h_ack(Adapter, &gl_coex_offload.h2c_record[opcode]);
		ret_status = c2h_status;
	} else {
		/* check h2c status error, return error status code to upper layer. */
		ret_status = h2c_status;
	}
	gl_coex_offload.h2c_record[opcode].status[ret_status]++;
	gl_coex_offload.status[ret_status]++;

	return ret_status;
}

u8 halbtcoutsrc_GetAntDetValFromBt(void *pBtcContext)
{
	/* Always return 0 since we don't implement this yet */
#if 0
	struct btc_coexist *pBtCoexist = (struct btc_coexist *)pBtcContext;
	PADAPTER			Adapter = pBtCoexist->Adapter;
	u8				AntDetVal = 0x0;
	u8				opcodeVer = 1;
	BOOLEAN				status = false;

	status = NDBG_GetAntDetValFromBt(Adapter, opcodeVer, &AntDetVal);

	RT_TRACE(COMP_DBG, DBG_LOUD, ("$$$ halbtcoutsrc_GetAntDetValFromBt(): status = %d, feature = %x\n", status, AntDetVal));

	return AntDetVal;
#else
	return 0;
#endif
}

u8 halbtcoutsrc_GetBleScanTypeFromBt(void *pBtcContext)
{
	PBTC_COEXIST pBtCoexist;
	u32 ret = BT_STATUS_BT_OP_SUCCESS;
	u8 data = 0;

	pBtCoexist = (PBTC_COEXIST)pBtcContext;

	if (halbtcoutsrc_IsHwMailboxExist(pBtCoexist) == _TRUE) {
		u8 buf[3] = {0};
		_irqL irqL;
		u8 op_code;
		u8 status;


		_enter_critical_mutex(&GLBtcBtMpOperLock, &irqL);

		op_code = BT_OP_GET_BT_BLE_SCAN_TYPE;
		status = _btmpoper_cmd(pBtCoexist, op_code, 0, buf, 0);
		if (status == BT_STATUS_BT_OP_SUCCESS)
			data = *(u8 *)GLBtcBtMpRptRsp;
		else
			ret = SET_BT_MP_OPER_RET(op_code, status);

		_exit_critical_mutex(&GLBtcBtMpOperLock, &irqL);

	} else
		ret = BT_STATUS_NOT_IMPLEMENT;

	return data;
}

u32 halbtcoutsrc_GetBleScanParaFromBt(void *pBtcContext, u8 scanType)
{
	PBTC_COEXIST pBtCoexist;
	u32 ret = BT_STATUS_BT_OP_SUCCESS;
	u32 data = 0;

	pBtCoexist = (PBTC_COEXIST)pBtcContext;

	if (halbtcoutsrc_IsHwMailboxExist(pBtCoexist) == _TRUE) {
		u8 buf[3] = {0};
		_irqL irqL;
		u8 op_code;
		u8 status;
		
		buf[0] = scanType;

		_enter_critical_mutex(&GLBtcBtMpOperLock, &irqL);

		op_code = BT_OP_GET_BT_BLE_SCAN_PARA;
		status = _btmpoper_cmd(pBtCoexist, op_code, 0, buf, 1);
		if (status == BT_STATUS_BT_OP_SUCCESS)
			data = le32_to_cpu(*(u32 *)GLBtcBtMpRptRsp);
		else
			ret = SET_BT_MP_OPER_RET(op_code, status);

		_exit_critical_mutex(&GLBtcBtMpOperLock, &irqL);

	} else
		ret = BT_STATUS_NOT_IMPLEMENT;

	return data;
}

u8 halbtcoutsrc_GetBtAFHMapFromBt(void *pBtcContext, u8 mapType, u8 *afhMap)
{
	struct btc_coexist *pBtCoexist = (struct btc_coexist *)pBtcContext;
	u8 buf[2] = {0};
	_irqL irqL;
	u8 op_code;
	u32 *AfhMapL = (u32 *)&(afhMap[0]);
	u32 *AfhMapM = (u32 *)&(afhMap[4]);
	u16 *AfhMapH = (u16 *)&(afhMap[8]);
	u8 status;
	u32 ret = BT_STATUS_BT_OP_SUCCESS;

	if (halbtcoutsrc_IsHwMailboxExist(pBtCoexist) == _FALSE)
		return _FALSE;

	buf[0] = 0;
	buf[1] = mapType;

	_enter_critical_mutex(&GLBtcBtMpOperLock, &irqL);

	op_code = BT_LO_OP_GET_AFH_MAP_L;
	status = _btmpoper_cmd(pBtCoexist, op_code, 0, buf, 0);
	if (status == BT_STATUS_BT_OP_SUCCESS)
		*AfhMapL = le32_to_cpu(*(u32 *)GLBtcBtMpRptRsp);
	else {
		ret = SET_BT_MP_OPER_RET(op_code, status);
		goto exit;
	}

	op_code = BT_LO_OP_GET_AFH_MAP_M;
	status = _btmpoper_cmd(pBtCoexist, op_code, 0, buf, 0);
	if (status == BT_STATUS_BT_OP_SUCCESS)
		*AfhMapM = le32_to_cpu(*(u32 *)GLBtcBtMpRptRsp);
	else {
		ret = SET_BT_MP_OPER_RET(op_code, status);
		goto exit;
	}

	op_code = BT_LO_OP_GET_AFH_MAP_H;
	status = _btmpoper_cmd(pBtCoexist, op_code, 0, buf, 0);
	if (status == BT_STATUS_BT_OP_SUCCESS)
		*AfhMapH = le16_to_cpu(*(u16 *)GLBtcBtMpRptRsp);
	else {
		ret = SET_BT_MP_OPER_RET(op_code, status);
		goto exit;
	}

exit:

	_exit_critical_mutex(&GLBtcBtMpOperLock, &irqL);

	return (ret == BT_STATUS_BT_OP_SUCCESS) ? _TRUE : _FALSE;
}

u32 halbtcoutsrc_GetPhydmVersion(void *pBtcContext)
{
	struct btc_coexist *pBtCoexist = (struct btc_coexist *)pBtcContext;
	PADAPTER		Adapter = pBtCoexist->Adapter;

#ifdef CONFIG_RTL8192E
	return RELEASE_VERSION_8192E;
#endif

#ifdef CONFIG_RTL8821A
	return RELEASE_VERSION_8821A;
#endif

#ifdef CONFIG_RTL8723B
	return RELEASE_VERSION_8723B;
#endif

#ifdef CONFIG_RTL8812A
	return RELEASE_VERSION_8812A;
#endif

#ifdef CONFIG_RTL8703B
	return RELEASE_VERSION_8703B;
#endif

#ifdef CONFIG_RTL8822B
	return RELEASE_VERSION_8822B;
#endif

#ifdef CONFIG_RTL8723D
	return RELEASE_VERSION_8723D;
#endif

#ifdef CONFIG_RTL8821C
	return RELEASE_VERSION_8821C;
#endif

#ifdef CONFIG_RTL8192F
	return RELEASE_VERSION_8192F;
#endif

#ifdef CONFIG_RTL8822C
	return RELEASE_VERSION_8822C;
#endif

#ifdef CONFIG_RTL8814A
	return RELEASE_VERSION_8814A;
#endif
}

u8 halbtcoutsrc_SetTimer(void *pBtcContext, u32 type, u32 val)
{
	struct btc_coexist *pBtCoexist=(struct btc_coexist *)pBtcContext;

	if (type >= BTC_TIMER_MAX)
		return _FALSE;

	pBtCoexist->coex_sta.cnt_timer[type] = val;

	RTW_DBG("[BTC], Set Timer: type = %d, val = %d\n", type, val);

	return _TRUE;
}

u32 halbtcoutsrc_SetAtomic (void *btc_ctx, u32 *target, u32 val)
{
	*target = val;
	return _SUCCESS;
}

void halbtcoutsrc_phydm_modify_AntDiv_HwSw(void *pBtcContext, u8 is_hw)
{
	/* empty function since we don't need it */
}

void halbtcoutsrc_phydm_modify_RA_PCR_threshold(void *pBtcContext, u8 RA_offset_direction, u8 RA_threshold_offset)
{
	struct btc_coexist *pBtCoexist = (struct btc_coexist *)pBtcContext;

/* switch to #if 0 in case the phydm version does not provide the function */
#if 1
	phydm_modify_RA_PCR_threshold(pBtCoexist->odm_priv, RA_offset_direction, RA_threshold_offset);
#endif
}

u32 halbtcoutsrc_phydm_query_PHY_counter(void *pBtcContext, u8 info_type)
{
	struct btc_coexist *pBtCoexist = (struct btc_coexist *)pBtcContext;

/* switch to #if 0 in case the phydm version does not provide the function */
#if 1
	return phydm_cmn_info_query((struct dm_struct *)pBtCoexist->odm_priv, (enum phydm_info_query)info_type);
#else
	return 0;
#endif
}

void halbtcoutsrc_reduce_wl_tx_power(void *pBtcContext, s8 tx_power)
{
	struct btc_coexist *pBtCoexist = (struct btc_coexist *)pBtcContext;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA((PADAPTER)pBtCoexist->Adapter);

	/* The reduction of wl tx pwr should be processed inside the set tx pwr lvl function */
	if (IS_HARDWARE_TYPE_8822C(pBtCoexist->Adapter))
		rtw_hal_set_tx_power_level(pBtCoexist->Adapter, pHalData->current_channel);
}

#if 0
static void BT_CoexOffloadRecordErrC2hAck(PADAPTER	Adapter)
{
	PADAPTER		pDefaultAdapter = GetDefaultAdapter(Adapter);

	if (pDefaultAdapter != Adapter)
		return;

	if (!hal_btcoex_IsBtExist(Adapter))
		return;

	gl_coex_offload.cnt_c2h_ack++;

	gl_coex_offload.status[COL_STATUS_INVALID_C2H_LEN]++;
}

static void BT_CoexOffloadC2hAckCheck(PADAPTER	Adapter, u8 *tmpBuf, u8 length)
{
	PADAPTER		pDefaultAdapter = GetDefaultAdapter(Adapter);
	PCOL_C2H_ACK	p_c2h_ack = NULL;
	u8			req_num = 0xff;

	if (pDefaultAdapter != Adapter)
		return;

	if (!hal_btcoex_IsBtExist(Adapter))
		return;

	gl_coex_offload.cnt_c2h_ack++;

	if (length < COL_C2H_ACK_HDR_LEN) {		/* c2h ack length must >= 3 (status, opcode_ver, req_num and ret_len) */
		gl_coex_offload.status[COL_STATUS_INVALID_C2H_LEN]++;
	} else {
		BT_PrintData(Adapter, "[COL], c2h ack:", length, tmpBuf);

		p_c2h_ack = (PCOL_C2H_ACK)tmpBuf;
		req_num = p_c2h_ack->req_num;

		_rtw_memmove(&gl_coex_offload.c2h_ack_buf[req_num][0], tmpBuf, length);
		gl_coex_offload.c2h_ack_len[req_num] = length;

		complete(&gl_coex_offload.c2h_event[req_num]);
	}
}

static void BT_CoexOffloadC2hIndCheck(PADAPTER Adapter, u8 *tmpBuf, u8 length)
{
	PADAPTER		pDefaultAdapter = GetDefaultAdapter(Adapter);
	PCOL_C2H_IND	p_c2h_ind = NULL;
	u8			ind_type = 0, ind_version = 0, ind_length = 0;

	if (pDefaultAdapter != Adapter)
		return;

	if (!hal_btcoex_IsBtExist(Adapter))
		return;

	gl_coex_offload.cnt_c2h_ind++;

	if (length < COL_C2H_IND_HDR_LEN) {		/* c2h indication length must >= 3 (type, version and length) */
		gl_coex_offload.c2h_ind_status[COL_STATUS_INVALID_C2H_LEN]++;
	} else {
		BT_PrintData(Adapter, "[COL], c2h indication:", length, tmpBuf);

		p_c2h_ind = (PCOL_C2H_IND)tmpBuf;
		ind_type = p_c2h_ind->type;
		ind_version = p_c2h_ind->version;
		ind_length = p_c2h_ind->length;

		_rtw_memmove(&gl_coex_offload.c2h_ind_buf[0], tmpBuf, length);
		gl_coex_offload.c2h_ind_len = length;

		/* log */
		gl_coex_offload.c2h_ind_record[ind_type].count++;
		gl_coex_offload.c2h_ind_record[ind_type].status[COL_STATUS_C2H_OK]++;
		_rtw_memmove(&gl_coex_offload.c2h_ind_record[ind_type].ind_buf[0], tmpBuf, length);
		gl_coex_offload.c2h_ind_record[ind_type].ind_len = length;

		gl_coex_offload.c2h_ind_status[COL_STATUS_C2H_OK]++;
		/*TODO: need to check c2h indication length*/
		/* TODO: Notification */
	}
}

void BT_CoexOffloadC2hCheck(PADAPTER Adapter, u8 *Buffer, u8 Length)
{
#if 0 /*(USE_HAL_MAC_API == 1)*/
	u8	c2hSubCmdId = 0, c2hAckLen = 0, h2cCmdId = 0, h2cSubCmdId = 0, c2hIndLen = 0;

	BT_PrintData(Adapter, "[COL], c2h packet:", Length - 2, Buffer + 2);
	c2hSubCmdId = (u8)C2H_HDR_GET_C2H_SUB_CMD_ID(Buffer);

	if (c2hSubCmdId == C2H_SUB_CMD_ID_H2C_ACK_HDR ||
	    c2hSubCmdId == C2H_SUB_CMD_ID_BT_COEX_INFO) {
		if (c2hSubCmdId == C2H_SUB_CMD_ID_H2C_ACK_HDR) {
			/* coex c2h ack */
			h2cCmdId = (u8)H2C_ACK_HDR_GET_H2C_CMD_ID(Buffer);
			h2cSubCmdId = (u8)H2C_ACK_HDR_GET_H2C_SUB_CMD_ID(Buffer);
			if (h2cCmdId == 0xff && h2cSubCmdId == 0x60) {
				c2hAckLen = (u8)C2H_HDR_GET_LEN(Buffer);
				if (c2hAckLen >= 8)
					BT_CoexOffloadC2hAckCheck(Adapter, &Buffer[12], (u8)(c2hAckLen - 8));
				else
					BT_CoexOffloadRecordErrC2hAck(Adapter);
			}
		} else if (c2hSubCmdId == C2H_SUB_CMD_ID_BT_COEX_INFO) {
			/* coex c2h indication */
			c2hIndLen = (u8)C2H_HDR_GET_LEN(Buffer);
			BT_CoexOffloadC2hIndCheck(Adapter, &Buffer[4], (u8)c2hIndLen);
		}
	}
#endif
}
#endif

/* ************************************
 *		Extern functions called by other module
 * ************************************ */
u8 EXhalbtcoutsrc_BindBtCoexWithAdapter(void *padapter)
{
	PBTC_COEXIST		pBtCoexist = &GLBtCoexist;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA((PADAPTER)padapter);

	if (pBtCoexist->bBinded)
		return _FALSE;
	else
		pBtCoexist->bBinded = _TRUE;

	pBtCoexist->statistics.cnt_bind++;

	pBtCoexist->Adapter = padapter;
	pBtCoexist->odm_priv = (void *)&(pHalData->odmpriv);

	pBtCoexist->stack_info.profile_notified = _FALSE;

	pBtCoexist->bt_info.bt_ctrl_agg_buf_size = _FALSE;
	pBtCoexist->bt_info.agg_buf_size = 5;

	pBtCoexist->bt_info.increase_scan_dev_num = _FALSE;
	pBtCoexist->bt_info.miracast_plus_bt = _FALSE;

	/* for btc common architecture, inform chip type to coex. mechanism */
	if(IS_HARDWARE_TYPE_8822C(padapter))
		pBtCoexist->chip_type = BTC_CHIP_RTL8822C;
	else if (IS_HARDWARE_TYPE_8822B(padapter))
		pBtCoexist->chip_type = BTC_CHIP_RTL8822B;
	else
		pBtCoexist->chip_type = BTC_CHIP_UNDEF;

	return _TRUE;
}

void EXhalbtcoutsrc_AntInfoSetting(void *padapter)
{
	PBTC_COEXIST		pBtCoexist = &GLBtCoexist;
	u8	antNum = 1, singleAntPath = 0;

	antNum = rtw_btcoex_get_pg_ant_num((PADAPTER)padapter);
	EXhalbtcoutsrc_SetAntNum(BT_COEX_ANT_TYPE_PG, antNum);

	if (antNum == 1) {
		singleAntPath = rtw_btcoex_get_pg_single_ant_path((PADAPTER)padapter);
		EXhalbtcoutsrc_SetSingleAntPath(singleAntPath);
	}

	pBtCoexist->board_info.customerID = RT_CID_DEFAULT;
	pBtCoexist->board_info.customer_id = RT_CID_DEFAULT;

	/* set default antenna position to main  port */
	pBtCoexist->board_info.btdm_ant_pos = BTC_ANTENNA_AT_MAIN_PORT;

	pBtCoexist->board_info.btdm_ant_det_finish = _FALSE;
	pBtCoexist->board_info.btdm_ant_num_by_ant_det = 1;

	pBtCoexist->board_info.tfbga_package = rtw_btcoex_is_tfbga_package_type((PADAPTER)padapter);

	pBtCoexist->board_info.rfe_type = rtw_btcoex_get_pg_rfe_type((PADAPTER)padapter);

	pBtCoexist->board_info.ant_div_cfg = rtw_btcoex_get_ant_div_cfg((PADAPTER)padapter);

	pBtCoexist->board_info.ant_distance = 10;
}

u8 EXhalbtcoutsrc_InitlizeVariables(void *padapter)
{
	PBTC_COEXIST pBtCoexist = &GLBtCoexist;

	/* pBtCoexist->statistics.cntBind++; */

	halbtcoutsrc_DbgInit();

	halbtcoutsrc_coex_offload_init();

#ifdef CONFIG_PCI_HCI
	pBtCoexist->chip_interface = BTC_INTF_PCI;
#elif defined(CONFIG_USB_HCI)
	pBtCoexist->chip_interface = BTC_INTF_USB;
#elif defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	pBtCoexist->chip_interface = BTC_INTF_SDIO;
#else
	pBtCoexist->chip_interface = BTC_INTF_UNKNOWN;
#endif

	EXhalbtcoutsrc_BindBtCoexWithAdapter(padapter);

	pBtCoexist->btc_read_1byte = halbtcoutsrc_Read1Byte;
	pBtCoexist->btc_write_1byte = halbtcoutsrc_Write1Byte;
	pBtCoexist->btc_write_1byte_bitmask = halbtcoutsrc_BitMaskWrite1Byte;
	pBtCoexist->btc_read_2byte = halbtcoutsrc_Read2Byte;
	pBtCoexist->btc_write_2byte = halbtcoutsrc_Write2Byte;
	pBtCoexist->btc_read_4byte = halbtcoutsrc_Read4Byte;
	pBtCoexist->btc_write_4byte = halbtcoutsrc_Write4Byte;
	pBtCoexist->btc_write_local_reg_1byte = halbtcoutsrc_WriteLocalReg1Byte;

	pBtCoexist->btc_set_bb_reg = halbtcoutsrc_SetBbReg;
	pBtCoexist->btc_get_bb_reg = halbtcoutsrc_GetBbReg;

	pBtCoexist->btc_set_rf_reg = halbtcoutsrc_SetRfReg;
	pBtCoexist->btc_get_rf_reg = halbtcoutsrc_GetRfReg;

	pBtCoexist->btc_fill_h2c = halbtcoutsrc_FillH2cCmd;
	pBtCoexist->btc_disp_dbg_msg = halbtcoutsrc_DisplayDbgMsg;

	pBtCoexist->btc_get = halbtcoutsrc_Get;
	pBtCoexist->btc_set = halbtcoutsrc_Set;
	pBtCoexist->btc_get_bt_reg = halbtcoutsrc_GetBtReg;
	pBtCoexist->btc_set_bt_reg = halbtcoutsrc_SetBtReg;
	pBtCoexist->btc_set_bt_ant_detection = halbtcoutsrc_SetBtAntDetection;
	pBtCoexist->btc_set_bt_trx_mask = halbtcoutsrc_SetBtTRXMASK;
	pBtCoexist->btc_coex_h2c_process = halbtcoutsrc_CoexH2cProcess;
	pBtCoexist->btc_get_bt_coex_supported_feature = halbtcoutsrc_GetBtCoexSupportedFeature;
	pBtCoexist->btc_get_bt_coex_supported_version= halbtcoutsrc_GetBtCoexSupportedVersion;
	pBtCoexist->btc_get_ant_det_val_from_bt = halbtcoutsrc_GetAntDetValFromBt;
	pBtCoexist->btc_get_ble_scan_type_from_bt = halbtcoutsrc_GetBleScanTypeFromBt;
	pBtCoexist->btc_get_ble_scan_para_from_bt = halbtcoutsrc_GetBleScanParaFromBt;
	pBtCoexist->btc_get_bt_afh_map_from_bt = halbtcoutsrc_GetBtAFHMapFromBt;
	pBtCoexist->btc_get_bt_phydm_version = halbtcoutsrc_GetPhydmVersion;
	pBtCoexist->btc_set_timer = halbtcoutsrc_SetTimer;
	pBtCoexist->btc_set_atomic= halbtcoutsrc_SetAtomic;
	pBtCoexist->btc_phydm_modify_RA_PCR_threshold = halbtcoutsrc_phydm_modify_RA_PCR_threshold;
	pBtCoexist->btc_phydm_query_PHY_counter = halbtcoutsrc_phydm_query_PHY_counter;
	pBtCoexist->btc_reduce_wl_tx_power = halbtcoutsrc_reduce_wl_tx_power;
	pBtCoexist->btc_phydm_modify_antdiv_hwsw = halbtcoutsrc_phydm_modify_AntDiv_HwSw;

	pBtCoexist->cli_buf = &GLBtcDbgBuf[0];

	GLBtcWiFiInScanState = _FALSE;

	GLBtcWiFiInIQKState = _FALSE;

	GLBtcWiFiInIPS = _FALSE;

	GLBtcWiFiInLPS = _FALSE;

	GLBtcBtCoexAliveRegistered = _FALSE;

	/* BT Control H2C/C2H*/
	GLBtcBtMpOperSeq = 0;
	_rtw_mutex_init(&GLBtcBtMpOperLock);
	rtw_init_timer(&GLBtcBtMpOperTimer, padapter, _btmpoper_timer_hdl, pBtCoexist);
	_rtw_init_sema(&GLBtcBtMpRptSema, 0);
	GLBtcBtMpRptSeq = 0;
	GLBtcBtMpRptStatus = 0;
	_rtw_memset(GLBtcBtMpRptRsp, 0, C2H_MAX_SIZE);
	GLBtcBtMpRptRspSize = 0;
	GLBtcBtMpRptWait = _FALSE;
	GLBtcBtMpRptWiFiOK = _FALSE;
	GLBtcBtMpRptBTOK = _FALSE;

	return _TRUE;
}

void EXhalbtcoutsrc_PowerOnSetting(PBTC_COEXIST pBtCoexist)
{
	HAL_DATA_TYPE	*pHalData = NULL;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pHalData = GET_HAL_DATA((PADAPTER)pBtCoexist->Adapter);

#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
	rtw_btc_ex_power_on_setting(pBtCoexist);

#else
	if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8723B
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_power_on_setting(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_power_on_setting(pBtCoexist);
#endif
	}

#ifdef CONFIG_RTL8703B
	else if (IS_HARDWARE_TYPE_8703B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8703b1ant_power_on_setting(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8723D
	else if (IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723d2ant_power_on_setting(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723d1ant_power_on_setting(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8821A
	else if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821a1ant_power_on_setting(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821a2ant_power_on_setting(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8822B
	else if ((IS_HARDWARE_TYPE_8822B(pBtCoexist->Adapter)) && (pHalData->EEPROMBluetoothCoexist == _TRUE)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8822b1ant_power_on_setting(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8822b2ant_power_on_setting(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8821C
	else if ((IS_HARDWARE_TYPE_8821C(pBtCoexist->Adapter)) && (pHalData->EEPROMBluetoothCoexist == _TRUE)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821c2ant_power_on_setting(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821c1ant_power_on_setting(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8814A
	if (IS_HARDWARE_TYPE_8814A(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8814a2ant_power_on_setting(pBtCoexist);
		/* else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8814a1ant_power_on_setting(pBtCoexist); */
	}
#endif

#endif
}

void EXhalbtcoutsrc_PreLoadFirmware(PBTC_COEXIST pBtCoexist)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cnt_pre_load_firmware++;

	if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8723B
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_pre_load_firmware(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_pre_load_firmware(pBtCoexist);
#endif
	}

#ifdef CONFIG_RTL8723D
	else if (IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723d2ant_pre_load_firmware(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723d1ant_pre_load_firmware(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8821C
	else if (IS_HARDWARE_TYPE_8821C(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821c2ant_pre_load_firmware(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821c1ant_pre_load_firmware(pBtCoexist);
	}
#endif
}

void EXhalbtcoutsrc_init_hw_config(PBTC_COEXIST pBtCoexist, u8 bWifiOnly)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cnt_init_hw_config++;

#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
	rtw_btc_ex_init_hw_config(pBtCoexist, bWifiOnly);
#else

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8821A
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821a2ant_init_hw_config(pBtCoexist, bWifiOnly);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821a1ant_init_hw_config(pBtCoexist, bWifiOnly);
#endif
	}

#ifdef CONFIG_RTL8723B
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_init_hw_config(pBtCoexist, bWifiOnly);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_init_hw_config(pBtCoexist, bWifiOnly);
	}
#endif

#ifdef CONFIG_RTL8703B
	else if (IS_HARDWARE_TYPE_8703B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8703b1ant_init_hw_config(pBtCoexist, bWifiOnly);
	}
#endif

#ifdef CONFIG_RTL8723D
	else if (IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723d2ant_init_hw_config(pBtCoexist, bWifiOnly);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723d1ant_init_hw_config(pBtCoexist, bWifiOnly);
	}
#endif

#ifdef CONFIG_RTL8192E
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8192e2ant_init_hw_config(pBtCoexist, bWifiOnly);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8192e1ant_init_hw_config(pBtCoexist, bWifiOnly);
	}
#endif

#ifdef CONFIG_RTL8812A
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8812a2ant_init_hw_config(pBtCoexist, bWifiOnly);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8812a1ant_init_hw_config(pBtCoexist, bWifiOnly);
	}
#endif

#ifdef CONFIG_RTL8822B
	else if (IS_HARDWARE_TYPE_8822B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8822b1ant_init_hw_config(pBtCoexist, bWifiOnly);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8822b2ant_init_hw_config(pBtCoexist, bWifiOnly);
	}
#endif

#ifdef CONFIG_RTL8821C
	else if (IS_HARDWARE_TYPE_8821C(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821c2ant_init_hw_config(pBtCoexist, bWifiOnly);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821c1ant_init_hw_config(pBtCoexist, bWifiOnly);
	}
#endif

#ifdef CONFIG_RTL8814A
	else if (IS_HARDWARE_TYPE_8814A(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8814a2ant_init_hw_config(pBtCoexist, bWifiOnly);
	}
#endif

#endif
}

void EXhalbtcoutsrc_init_coex_dm(PBTC_COEXIST pBtCoexist)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cnt_init_coex_dm++;

#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
	rtw_btc_ex_init_coex_dm(pBtCoexist);
#else

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8821A
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821a2ant_init_coex_dm(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821a1ant_init_coex_dm(pBtCoexist);
#endif
	}

#ifdef CONFIG_RTL8723B
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_init_coex_dm(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_init_coex_dm(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8703B
	else if (IS_HARDWARE_TYPE_8703B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8703b1ant_init_coex_dm(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8723D
	else if (IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723d2ant_init_coex_dm(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723d1ant_init_coex_dm(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8192E
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8192e2ant_init_coex_dm(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8192e1ant_init_coex_dm(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8812A
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8812a2ant_init_coex_dm(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8812a1ant_init_coex_dm(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8822B
	else if (IS_HARDWARE_TYPE_8822B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8822b1ant_init_coex_dm(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8822b2ant_init_coex_dm(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8821C
	else if (IS_HARDWARE_TYPE_8821C(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821c2ant_init_coex_dm(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821c1ant_init_coex_dm(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8814A
	else if (IS_HARDWARE_TYPE_8814A(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8814a2ant_init_coex_dm(pBtCoexist);
	}
#endif

#endif

	pBtCoexist->initilized = _TRUE;
}

void EXhalbtcoutsrc_ips_notify(PBTC_COEXIST pBtCoexist, u8 type)
{
	u8	ipsType;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cnt_ips_notify++;
	if (pBtCoexist->manual_control)
		return;

	if (IPS_NONE == type) {
		ipsType = BTC_IPS_LEAVE;
		GLBtcWiFiInIPS = _FALSE;
	} else {
		ipsType = BTC_IPS_ENTER;
		GLBtcWiFiInIPS = _TRUE;
	}

	/* All notify is called in cmd thread, don't need to leave low power again
	*	halbtcoutsrc_LeaveLowPower(pBtCoexist); */

#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
	rtw_btc_ex_ips_notify(pBtCoexist, ipsType);
#else

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8821A
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821a2ant_ips_notify(pBtCoexist, ipsType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821a1ant_ips_notify(pBtCoexist, ipsType);
#endif
	}

#ifdef CONFIG_RTL8723B
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_ips_notify(pBtCoexist, ipsType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_ips_notify(pBtCoexist, ipsType);
	}
#endif

#ifdef CONFIG_RTL8703B
	else if (IS_HARDWARE_TYPE_8703B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8703b1ant_ips_notify(pBtCoexist, ipsType);
	}
#endif

#ifdef CONFIG_RTL8723D
	else if (IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723d2ant_ips_notify(pBtCoexist, ipsType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723d1ant_ips_notify(pBtCoexist, ipsType);
	}
#endif

#ifdef CONFIG_RTL8192E
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8192e2ant_ips_notify(pBtCoexist, ipsType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8192e1ant_ips_notify(pBtCoexist, ipsType);
	}
#endif

#ifdef CONFIG_RTL8812A
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8812a2ant_ips_notify(pBtCoexist, ipsType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8812a1ant_ips_notify(pBtCoexist, ipsType);
	}
#endif

#ifdef CONFIG_RTL8822B
	else if (IS_HARDWARE_TYPE_8822B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8822b1ant_ips_notify(pBtCoexist, ipsType);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8822b2ant_ips_notify(pBtCoexist, ipsType);
	}
#endif

#ifdef CONFIG_RTL8821C
	else if (IS_HARDWARE_TYPE_8821C(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821c2ant_ips_notify(pBtCoexist, ipsType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821c1ant_ips_notify(pBtCoexist, ipsType);
	}
#endif

#ifdef CONFIG_RTL8814A
	else if (IS_HARDWARE_TYPE_8814A(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8814a2ant_ips_notify(pBtCoexist, ipsType);
	}
#endif

#endif
	/*	halbtcoutsrc_NormalLowPower(pBtCoexist); */
}

void EXhalbtcoutsrc_lps_notify(PBTC_COEXIST pBtCoexist, u8 type)
{
	u8 lpsType;


	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cnt_lps_notify++;
	if (pBtCoexist->manual_control)
		return;

	if (PS_MODE_ACTIVE == type) {
		lpsType = BTC_LPS_DISABLE;
		GLBtcWiFiInLPS = _FALSE;
	} else {
		lpsType = BTC_LPS_ENABLE;
		GLBtcWiFiInLPS = _TRUE;
	}

#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
	rtw_btc_ex_lps_notify(pBtCoexist, lpsType);
#else

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8821A
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821a2ant_lps_notify(pBtCoexist, lpsType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821a1ant_lps_notify(pBtCoexist, lpsType);
#endif
	}

#ifdef CONFIG_RTL8723B
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_lps_notify(pBtCoexist, lpsType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_lps_notify(pBtCoexist, lpsType);
	}
#endif

#ifdef CONFIG_RTL8703B
	else if (IS_HARDWARE_TYPE_8703B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8703b1ant_lps_notify(pBtCoexist, lpsType);
	}
#endif

#ifdef CONFIG_RTL8723D
	else if (IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723d2ant_lps_notify(pBtCoexist, lpsType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723d1ant_lps_notify(pBtCoexist, lpsType);
	}
#endif

#ifdef CONFIG_RTL8192E
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8192e2ant_lps_notify(pBtCoexist, lpsType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8192e1ant_lps_notify(pBtCoexist, lpsType);
	}
#endif

#ifdef CONFIG_RTL8812A
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8812a2ant_lps_notify(pBtCoexist, lpsType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8812a1ant_lps_notify(pBtCoexist, lpsType);
	}
#endif

#ifdef CONFIG_RTL8822B
	else if (IS_HARDWARE_TYPE_8822B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8822b1ant_lps_notify(pBtCoexist, lpsType);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8822b2ant_lps_notify(pBtCoexist, lpsType);
	}
#endif

#ifdef CONFIG_RTL8821C
	else if (IS_HARDWARE_TYPE_8821C(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821c2ant_lps_notify(pBtCoexist, lpsType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821c1ant_lps_notify(pBtCoexist, lpsType);
	}
#endif

#ifdef CONFIG_RTL8814A
	else if (IS_HARDWARE_TYPE_8814A(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8814a2ant_lps_notify(pBtCoexist, lpsType);
	}
#endif

#endif
}

void EXhalbtcoutsrc_scan_notify(PBTC_COEXIST pBtCoexist, u8 type)
{
	u8	scanType;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;
	pBtCoexist->statistics.cnt_scan_notify++;
	if (pBtCoexist->manual_control)
		return;

	if (type) {
		scanType = BTC_SCAN_START;
		GLBtcWiFiInScanState = _TRUE;
	} else {
		scanType = BTC_SCAN_FINISH;
		GLBtcWiFiInScanState = _FALSE;
	}

	/* All notify is called in cmd thread, don't need to leave low power again
	*	halbtcoutsrc_LeaveLowPower(pBtCoexist); */

#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
	rtw_btc_ex_scan_notify(pBtCoexist, scanType);
#else

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8821A
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821a2ant_scan_notify(pBtCoexist, scanType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821a1ant_scan_notify(pBtCoexist, scanType);
#endif
	}

#ifdef CONFIG_RTL8723B
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_scan_notify(pBtCoexist, scanType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_scan_notify(pBtCoexist, scanType);
	}
#endif

#ifdef CONFIG_RTL8703B
	else if (IS_HARDWARE_TYPE_8703B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8703b1ant_scan_notify(pBtCoexist, scanType);
	}
#endif

#ifdef CONFIG_RTL8723D
	else if (IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723d2ant_scan_notify(pBtCoexist, scanType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723d1ant_scan_notify(pBtCoexist, scanType);
	}
#endif

#ifdef CONFIG_RTL8192E
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8192e2ant_scan_notify(pBtCoexist, scanType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8192e1ant_scan_notify(pBtCoexist, scanType);
	}
#endif

#ifdef CONFIG_RTL8812A
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8812a2ant_scan_notify(pBtCoexist, scanType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8812a1ant_scan_notify(pBtCoexist, scanType);
	}
#endif

#ifdef CONFIG_RTL8822B
	else if (IS_HARDWARE_TYPE_8822B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8822b1ant_scan_notify(pBtCoexist, scanType);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8822b2ant_scan_notify(pBtCoexist, scanType);
	}
#endif

#ifdef CONFIG_RTL8821C
	else if (IS_HARDWARE_TYPE_8821C(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821c2ant_scan_notify(pBtCoexist, scanType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821c1ant_scan_notify(pBtCoexist, scanType);
	}
#endif

#ifdef CONFIG_RTL8814A
	else if (IS_HARDWARE_TYPE_8814A(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8814a2ant_scan_notify(pBtCoexist, scanType);
	}
#endif

#endif

	/*	halbtcoutsrc_NormalLowPower(pBtCoexist); */
}

void EXhalbtcoutsrc_SetAntennaPathNotify(PBTC_COEXIST pBtCoexist, u8 type)
{
#if 0
	u8	switchType;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	if (pBtCoexist->manual_control)
		return;

	halbtcoutsrc_LeaveLowPower(pBtCoexist);

	switchType = type;

	if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_set_antenna_notify(pBtCoexist, type);
	}
	if (IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723d1ant_set_antenna_notify(pBtCoexist, type);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723d2ant_set_antenna_notify(pBtCoexist, type);
	}

	halbtcoutsrc_NormalLowPower(pBtCoexist);
#endif
}

void EXhalbtcoutsrc_connect_notify(PBTC_COEXIST pBtCoexist, u8 assoType)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;
	pBtCoexist->statistics.cnt_connect_notify++;
	if (pBtCoexist->manual_control)
		return;
	
	/* All notify is called in cmd thread, don't need to leave low power again
	*	halbtcoutsrc_LeaveLowPower(pBtCoexist); */
#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
	rtw_btc_ex_connect_notify(pBtCoexist, assoType);
#else

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8821A
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821a2ant_connect_notify(pBtCoexist, assoType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821a1ant_connect_notify(pBtCoexist, assoType);
#endif
	}

#ifdef CONFIG_RTL8723B
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_connect_notify(pBtCoexist, assoType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_connect_notify(pBtCoexist, assoType);
	}
#endif

#ifdef CONFIG_RTL8703B
	else if (IS_HARDWARE_TYPE_8703B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8703b1ant_connect_notify(pBtCoexist, assoType);
	}
#endif

#ifdef CONFIG_RTL8723D
	else if (IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723d2ant_connect_notify(pBtCoexist, assoType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723d1ant_connect_notify(pBtCoexist, assoType);
	}
#endif

#ifdef CONFIG_RTL8192E
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8192e2ant_connect_notify(pBtCoexist, assoType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8192e1ant_connect_notify(pBtCoexist, assoType);
	}
#endif

#ifdef CONFIG_RTL8812A
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8812a2ant_connect_notify(pBtCoexist, assoType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8812a1ant_connect_notify(pBtCoexist, assoType);
	}
#endif

#ifdef CONFIG_RTL8822B
	else if (IS_HARDWARE_TYPE_8822B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8822b1ant_connect_notify(pBtCoexist, assoType);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8822b2ant_connect_notify(pBtCoexist, assoType);
	}
#endif

#ifdef CONFIG_RTL8821C
	else if (IS_HARDWARE_TYPE_8821C(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821c2ant_connect_notify(pBtCoexist, assoType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821c1ant_connect_notify(pBtCoexist, assoType);
	}
#endif

#ifdef CONFIG_RTL8814A
	else if (IS_HARDWARE_TYPE_8814A(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8814a2ant_connect_notify(pBtCoexist, assoType);
	}
#endif

#endif

	/*	halbtcoutsrc_NormalLowPower(pBtCoexist); */
}

void EXhalbtcoutsrc_media_status_notify(PBTC_COEXIST pBtCoexist, RT_MEDIA_STATUS mediaStatus)
{
	u8 mStatus = BTC_MEDIA_MAX;
	PADAPTER adapter = NULL;
	HAL_DATA_TYPE *hal = NULL;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	if (pBtCoexist->manual_control)
		return;

	pBtCoexist->statistics.cnt_media_status_notify++;
	adapter = (PADAPTER)pBtCoexist->Adapter;
	hal = GET_HAL_DATA(adapter);

	if (RT_MEDIA_CONNECT == mediaStatus) {
		if (hal->current_band_type == BAND_ON_2_4G)
			mStatus = BTC_MEDIA_CONNECT;
		else if (hal->current_band_type == BAND_ON_5G)
			mStatus = BTC_MEDIA_CONNECT_5G;
		else {
			mStatus = BTC_MEDIA_CONNECT;
			RTW_ERR("%s unknow band type\n", __func__);
		}
	} else
		mStatus = BTC_MEDIA_DISCONNECT;

	/* All notify is called in cmd thread, don't need to leave low power again
	*	halbtcoutsrc_LeaveLowPower(pBtCoexist); */
#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
	rtw_btc_ex_media_status_notify(pBtCoexist, mStatus);
#else

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8821A
		/* compatible for 8821A */
		if (mStatus == BTC_MEDIA_CONNECT_5G)
			mStatus = BTC_MEDIA_CONNECT;
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821a2ant_media_status_notify(pBtCoexist, mStatus);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821a1ant_media_status_notify(pBtCoexist, mStatus);
#endif
	}

#ifdef CONFIG_RTL8723B
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_media_status_notify(pBtCoexist, mStatus);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_media_status_notify(pBtCoexist, mStatus);
	}
#endif

#ifdef CONFIG_RTL8703B
	else if (IS_HARDWARE_TYPE_8703B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8703b1ant_media_status_notify(pBtCoexist, mStatus);
	}
#endif

#ifdef CONFIG_RTL8723D
	else if (IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723d2ant_media_status_notify(pBtCoexist, mStatus);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723d1ant_media_status_notify(pBtCoexist, mStatus);
	}
#endif

#ifdef CONFIG_RTL8192E
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8192e2ant_media_status_notify(pBtCoexist, mStatus);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8192e1ant_media_status_notify(pBtCoexist, mStatus);
	}
#endif

#ifdef CONFIG_RTL8812A
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter)) {
		/* compatible for 8812A */
		if (mStatus == BTC_MEDIA_CONNECT_5G)
			mStatus = BTC_MEDIA_CONNECT;
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8812a2ant_media_status_notify(pBtCoexist, mStatus);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8812a1ant_media_status_notify(pBtCoexist, mStatus);
	}
#endif

#ifdef CONFIG_RTL8822B
	else if (IS_HARDWARE_TYPE_8822B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8822b1ant_media_status_notify(pBtCoexist, mStatus);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8822b2ant_media_status_notify(pBtCoexist, mStatus);
	}
#endif

#ifdef CONFIG_RTL8821C
	else if (IS_HARDWARE_TYPE_8821C(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821c2ant_media_status_notify(pBtCoexist, mStatus);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821c1ant_media_status_notify(pBtCoexist, mStatus);
	}
#endif

#ifdef CONFIG_RTL8814A
	else if (IS_HARDWARE_TYPE_8814A(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8814a2ant_media_status_notify(pBtCoexist, mStatus);
	}
#endif

#endif

	/*	halbtcoutsrc_NormalLowPower(pBtCoexist); */
}

void EXhalbtcoutsrc_specific_packet_notify(PBTC_COEXIST pBtCoexist, u8 pktType)
{
	u8 packetType;
	PADAPTER adapter = NULL;
	HAL_DATA_TYPE *hal = NULL;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;
	
	if (pBtCoexist->manual_control)
		return;

	pBtCoexist->statistics.cnt_specific_packet_notify++;
	adapter = (PADAPTER)pBtCoexist->Adapter;
	hal = GET_HAL_DATA(adapter);

	if (PACKET_DHCP == pktType)
		packetType = BTC_PACKET_DHCP;
	else if (PACKET_EAPOL == pktType)
		packetType = BTC_PACKET_EAPOL;
	else if (PACKET_ARP == pktType)
		packetType = BTC_PACKET_ARP;
	else {
		packetType = BTC_PACKET_UNKNOWN;
		return;
	}

	if (hal->current_band_type == BAND_ON_5G)
		packetType |=  BTC_5G_BAND;

	/* All notify is called in cmd thread, don't need to leave low power again
	*	halbtcoutsrc_LeaveLowPower(pBtCoexist); */
#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
	rtw_btc_ex_specific_packet_notify(pBtCoexist, packetType);
#else

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8821A
		/* compatible for 8821A */
		if (hal->current_band_type == BAND_ON_5G)
			packetType &= ~BTC_5G_BAND;

		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821a2ant_specific_packet_notify(pBtCoexist, packetType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821a1ant_specific_packet_notify(pBtCoexist, packetType);
#endif
	}

#ifdef CONFIG_RTL8723B
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_specific_packet_notify(pBtCoexist, packetType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_specific_packet_notify(pBtCoexist, packetType);
	}
#endif

#ifdef CONFIG_RTL8703B
	else if (IS_HARDWARE_TYPE_8703B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8703b1ant_specific_packet_notify(pBtCoexist, packetType);
	}
#endif

#ifdef CONFIG_RTL8723D
	else if (IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723d2ant_specific_packet_notify(pBtCoexist, packetType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723d1ant_specific_packet_notify(pBtCoexist, packetType);
	}
#endif

#ifdef CONFIG_RTL8192E
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8192e2ant_specific_packet_notify(pBtCoexist, packetType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8192e1ant_specific_packet_notify(pBtCoexist, packetType);
	}
#endif

#ifdef CONFIG_RTL8812A
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter)) {
		/* compatible for 8812A */
		if (hal->current_band_type == BAND_ON_5G)
			packetType &= ~BTC_5G_BAND;
		
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8812a2ant_specific_packet_notify(pBtCoexist, packetType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8812a1ant_specific_packet_notify(pBtCoexist, packetType);
	}
#endif

#ifdef CONFIG_RTL8822B
	else if (IS_HARDWARE_TYPE_8822B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8822b1ant_specific_packet_notify(pBtCoexist, packetType);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8822b2ant_specific_packet_notify(pBtCoexist, packetType);
	}
#endif

#ifdef CONFIG_RTL8821C
	else if (IS_HARDWARE_TYPE_8821C(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821c2ant_specific_packet_notify(pBtCoexist, packetType);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821c1ant_specific_packet_notify(pBtCoexist, packetType);
	}
#endif

#ifdef CONFIG_RTL8814A
	else if (IS_HARDWARE_TYPE_8814A(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8814a2ant_specific_packet_notify(pBtCoexist, packetType);
	}
#endif

#endif

	/*	halbtcoutsrc_NormalLowPower(pBtCoexist); */
}

void EXhalbtcoutsrc_bt_info_notify(PBTC_COEXIST pBtCoexist, u8 *tmpBuf, u8 length)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cnt_bt_info_notify++;

	/* All notify is called in cmd thread, don't need to leave low power again
	*	halbtcoutsrc_LeaveLowPower(pBtCoexist); */
#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
	rtw_btc_ex_bt_info_notify(pBtCoexist, tmpBuf, length);
#else

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8821A
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821a2ant_bt_info_notify(pBtCoexist, tmpBuf, length);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821a1ant_bt_info_notify(pBtCoexist, tmpBuf, length);
#endif
	}

#ifdef CONFIG_RTL8723B
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_bt_info_notify(pBtCoexist, tmpBuf, length);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_bt_info_notify(pBtCoexist, tmpBuf, length);
	}
#endif

#ifdef CONFIG_RTL8703B
	else if (IS_HARDWARE_TYPE_8703B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8703b1ant_bt_info_notify(pBtCoexist, tmpBuf, length);
	}
#endif

#ifdef CONFIG_RTL8723D
	else if (IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723d2ant_bt_info_notify(pBtCoexist, tmpBuf, length);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723d1ant_bt_info_notify(pBtCoexist, tmpBuf, length);
	}
#endif

#ifdef CONFIG_RTL8192E
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8192e2ant_bt_info_notify(pBtCoexist, tmpBuf, length);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8192e1ant_bt_info_notify(pBtCoexist, tmpBuf, length);
	}
#endif

#ifdef CONFIG_RTL8812A
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8812a2ant_bt_info_notify(pBtCoexist, tmpBuf, length);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8812a1ant_bt_info_notify(pBtCoexist, tmpBuf, length);
	}
#endif

#ifdef CONFIG_RTL8822B
	else if (IS_HARDWARE_TYPE_8822B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8822b1ant_bt_info_notify(pBtCoexist, tmpBuf, length);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8822b2ant_bt_info_notify(pBtCoexist, tmpBuf, length);
	}
#endif

#ifdef CONFIG_RTL8821C
	else if (IS_HARDWARE_TYPE_8821C(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821c2ant_bt_info_notify(pBtCoexist, tmpBuf, length);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821c1ant_bt_info_notify(pBtCoexist, tmpBuf, length);
	}
#endif

#ifdef CONFIG_RTL8814A
	else if (IS_HARDWARE_TYPE_8814A(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8814a2ant_bt_info_notify(pBtCoexist, tmpBuf, length);
	}
#endif

#endif

	/*	halbtcoutsrc_NormalLowPower(pBtCoexist); */
}

void EXhalbtcoutsrc_WlFwDbgInfoNotify(PBTC_COEXIST pBtCoexist, u8* tmpBuf, u8 length)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
	rtw_btc_ex_wl_fwdbginfo_notify(pBtCoexist, tmpBuf, length);
#else

	if (IS_HARDWARE_TYPE_8703B(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8703B
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8703b1ant_wl_fwdbginfo_notify(pBtCoexist, tmpBuf, length);
#endif
	}

#ifdef CONFIG_RTL8723D
	else if (IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723d1ant_wl_fwdbginfo_notify(pBtCoexist, tmpBuf, length);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723d2ant_wl_fwdbginfo_notify(pBtCoexist, tmpBuf, length);
	}
#endif

#ifdef CONFIG_RTL8821C
	else if (IS_HARDWARE_TYPE_8821C(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821c2ant_wl_fwdbginfo_notify(pBtCoexist, tmpBuf, length);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821c1ant_wl_fwdbginfo_notify(pBtCoexist, tmpBuf, length);
	}
#endif

#endif
}

void EXhalbtcoutsrc_rx_rate_change_notify(PBTC_COEXIST pBtCoexist, u8 is_data_frame, u8 btc_rate_id)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cnt_rate_id_notify++;

#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
	rtw_btc_ex_rx_rate_change_notify(pBtCoexist, is_data_frame, btc_rate_id);
#else

	if (IS_HARDWARE_TYPE_8703B(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8703B
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8703b1ant_rx_rate_change_notify(pBtCoexist, is_data_frame, btc_rate_id);
#endif
	}

#ifdef CONFIG_RTL8723D
	else if (IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723d1ant_rx_rate_change_notify(pBtCoexist, is_data_frame, btc_rate_id);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723d2ant_rx_rate_change_notify(pBtCoexist, is_data_frame, btc_rate_id);
	}
#endif

#ifdef CONFIG_RTL8821C
	else if (IS_HARDWARE_TYPE_8821C(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821c1ant_rx_rate_change_notify(pBtCoexist, is_data_frame, btc_rate_id);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821c2ant_rx_rate_change_notify(pBtCoexist, is_data_frame, btc_rate_id);
	}
#endif

#endif
}

void
EXhalbtcoutsrc_RfStatusNotify(
		PBTC_COEXIST		pBtCoexist,
		u8				type
)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;
	pBtCoexist->statistics.cnt_rf_status_notify++;

#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
	rtw_btc_ex_rf_status_notify(pBtCoexist, type);
#else

	if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8723B
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_rf_status_notify(pBtCoexist, type);
#endif
	}

#ifdef CONFIG_RTL8703B
	else if (IS_HARDWARE_TYPE_8703B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8703b1ant_rf_status_notify(pBtCoexist, type);
	}
#endif

#ifdef CONFIG_RTL8723D
	else if (IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723d1ant_rf_status_notify(pBtCoexist, type);
	}
#endif

#ifdef CONFIG_RTL8822B
	else if (IS_HARDWARE_TYPE_8822B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8822b1ant_rf_status_notify(pBtCoexist, type);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8822b2ant_rf_status_notify(pBtCoexist, type);
	}
#endif

#ifdef CONFIG_RTL8821C
	else if (IS_HARDWARE_TYPE_8821C(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821c2ant_rf_status_notify(pBtCoexist, type);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821c1ant_rf_status_notify(pBtCoexist, type);
	}
#endif

#ifdef CONFIG_RTL8814A
	else if (IS_HARDWARE_TYPE_8814A(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8814a2ant_rf_status_notify(pBtCoexist, type);
	}
#endif

#endif
}

void EXhalbtcoutsrc_StackOperationNotify(PBTC_COEXIST pBtCoexist, u8 type)
{
#if 0
	u8	stackOpType;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;
	pBtCoexist->statistics.cntStackOperationNotify++;
	if (pBtCoexist->manual_control)
		return;

	if ((HCI_BT_OP_INQUIRY_START == type) ||
	    (HCI_BT_OP_PAGING_START == type) ||
	    (HCI_BT_OP_PAIRING_START == type))
		stackOpType = BTC_STACK_OP_INQ_PAGE_PAIR_START;
	else if ((HCI_BT_OP_INQUIRY_FINISH == type) ||
		 (HCI_BT_OP_PAGING_SUCCESS == type) ||
		 (HCI_BT_OP_PAGING_UNSUCCESS == type) ||
		 (HCI_BT_OP_PAIRING_FINISH == type))
		stackOpType = BTC_STACK_OP_INQ_PAGE_PAIR_FINISH;
	else
		stackOpType = BTC_STACK_OP_NONE;

#endif
}

void EXhalbtcoutsrc_halt_notify(PBTC_COEXIST pBtCoexist)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cnt_halt_notify++;

#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
	rtw_btc_ex_halt_notify(pBtCoexist);
#else

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8821A
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821a2ant_halt_notify(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821a1ant_halt_notify(pBtCoexist);
#endif
	}

#ifdef CONFIG_RTL8723B
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_halt_notify(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_halt_notify(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8703B
	else if (IS_HARDWARE_TYPE_8703B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8703b1ant_halt_notify(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8723D
	else if (IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723d2ant_halt_notify(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723d1ant_halt_notify(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8192E
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8192e2ant_halt_notify(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8192e1ant_halt_notify(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8812A
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8812a2ant_halt_notify(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8812a1ant_halt_notify(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8822B
	else if (IS_HARDWARE_TYPE_8822B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8822b1ant_halt_notify(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8822b2ant_halt_notify(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8821C
	else if (IS_HARDWARE_TYPE_8821C(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821c2ant_halt_notify(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821c1ant_halt_notify(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8814A
	else if (IS_HARDWARE_TYPE_8814A(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8814a2ant_halt_notify(pBtCoexist);
	}
#endif

#endif
}

void EXhalbtcoutsrc_SwitchBtTRxMask(PBTC_COEXIST pBtCoexist)
{
	if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2) {
			halbtcoutsrc_SetBtReg(pBtCoexist, 0, 0x3c, 0x01); /* BT goto standby while GNT_BT 1-->0 */
		} else if (pBtCoexist->board_info.btdm_ant_num == 1) {
			halbtcoutsrc_SetBtReg(pBtCoexist, 0, 0x3c, 0x15); /* BT goto standby while GNT_BT 1-->0 */
		}
	}
}

void EXhalbtcoutsrc_pnp_notify(PBTC_COEXIST pBtCoexist, u8 pnpState)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cnt_pnp_notify++;

	/*  */
	/* currently only 1ant we have to do the notification, */
	/* once pnp is notified to sleep state, we have to leave LPS that we can sleep normally. */
	/*  */
#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
	rtw_btc_ex_pnp_notify(pBtCoexist, pnpState);
#else

	if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8723B
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_pnp_notify(pBtCoexist, pnpState);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_pnp_notify(pBtCoexist, pnpState);
#endif
	}

#ifdef CONFIG_RTL8703B
	else if (IS_HARDWARE_TYPE_8703B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8703b1ant_pnp_notify(pBtCoexist, pnpState);
	}
#endif

#ifdef CONFIG_RTL8723D
	else if (IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723d1ant_pnp_notify(pBtCoexist, pnpState);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723d2ant_pnp_notify(pBtCoexist, pnpState);
	}
#endif

#ifdef CONFIG_RTL8821A
	else if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821a1ant_pnp_notify(pBtCoexist, pnpState);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821a2ant_pnp_notify(pBtCoexist, pnpState);
	}
#endif

#ifdef CONFIG_RTL8192E
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8192e1ant_pnp_notify(pBtCoexist, pnpState);
	}
#endif

#ifdef CONFIG_RTL8812A
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8812a1ant_pnp_notify(pBtCoexist, pnpState);
	}
#endif

#ifdef CONFIG_RTL8822B
	else if (IS_HARDWARE_TYPE_8822B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8822b1ant_pnp_notify(pBtCoexist, pnpState);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8822b2ant_pnp_notify(pBtCoexist, pnpState);
	}
#endif

#ifdef CONFIG_RTL8821C
	else if (IS_HARDWARE_TYPE_8821C(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821c2ant_pnp_notify(pBtCoexist, pnpState);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821c1ant_pnp_notify(pBtCoexist, pnpState);
	}
#endif

#ifdef CONFIG_RTL8814A
	else if (IS_HARDWARE_TYPE_8814A(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8814a2ant_pnp_notify(pBtCoexist, pnpState);
	}
#endif

#endif
}

void EXhalbtcoutsrc_CoexDmSwitch(PBTC_COEXIST pBtCoexist)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;
	pBtCoexist->statistics.cnt_coex_dm_switch++;

	halbtcoutsrc_LeaveLowPower(pBtCoexist);

	if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8723B
		if (pBtCoexist->board_info.btdm_ant_num == 1) {
			pBtCoexist->stop_coex_dm = TRUE;
			ex_halbtc8723b1ant_coex_dm_reset(pBtCoexist);
			EXhalbtcoutsrc_SetAntNum(BT_COEX_ANT_TYPE_DETECTED, 2);
			ex_halbtc8723b2ant_init_hw_config(pBtCoexist, FALSE);
			ex_halbtc8723b2ant_init_coex_dm(pBtCoexist);
			pBtCoexist->stop_coex_dm = FALSE;
		}
#endif
	}

#ifdef CONFIG_RTL8723D
	else if (IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1) {
			pBtCoexist->stop_coex_dm = TRUE;
			ex_halbtc8723d1ant_coex_dm_reset(pBtCoexist);
			EXhalbtcoutsrc_SetAntNum(BT_COEX_ANT_TYPE_DETECTED, 2);
			ex_halbtc8723d2ant_init_hw_config(pBtCoexist, FALSE);
			ex_halbtc8723d2ant_init_coex_dm(pBtCoexist);
			pBtCoexist->stop_coex_dm = FALSE;
		}
	}
#endif

	halbtcoutsrc_NormalLowPower(pBtCoexist);
}

#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
void EXhalbtcoutsrc_TimerNotify(PBTC_COEXIST pBtCoexist, u32 timer_type)
{
	rtw_btc_ex_timerup_notify(pBtCoexist, timer_type);
}

void EXhalbtcoutsrc_WLStatusChangeNotify(PBTC_COEXIST pBtCoexist, u32 change_type)
{
	rtw_btc_ex_wl_status_change_notify(pBtCoexist, change_type);
}

u32 EXhalbtcoutsrc_CoexTimerCheck(PBTC_COEXIST pBtCoexist)
{
	u32 i, timer_map = 0;

	for (i = 0; i < BTC_TIMER_MAX; i++) {
		if (pBtCoexist->coex_sta.cnt_timer[i] > 0) {
			if (pBtCoexist->coex_sta.cnt_timer[i] == 1) {
				timer_map |= BIT(i);
				RTW_DBG("[BTC], %s(): timer_map = 0x%x\n", __func__, timer_map);
			}

			pBtCoexist->coex_sta.cnt_timer[i]--;
		}
	}

	return timer_map;
}

u32 EXhalbtcoutsrc_WLStatusCheck(PBTC_COEXIST pBtCoexist)
{
	struct btc_wifi_link_info link_info;
	const struct btc_chip_para *chip_para = pBtCoexist->chip_para;
	u32 change_map = 0;
	static bool wl_busy_pre;
	bool	wl_busy = _FALSE;
	s32 wl_rssi;
	u32 traffic_dir;
	u8 i, tmp;
	static u8 rssi_step_pre = 4;

	/* WL busy to idle or idle to busy */
	pBtCoexist->btc_get(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &wl_busy);
	if (wl_busy != wl_busy_pre) {
		if (wl_busy)
			change_map |=  BIT(BTC_WLSTATUS_CHANGE_TOBUSY);
		else
			change_map |=  BIT(BTC_WLSTATUS_CHANGE_TOIDLE);

		wl_busy_pre = wl_busy;
	}

	/* WL RSSI */
	pBtCoexist->btc_get(pBtCoexist, BTC_GET_S4_WIFI_RSSI, &wl_rssi);
	tmp = (u8)(wl_rssi & 0xff);
	for (i = 0; i < 4; i++) {
		if (tmp >= chip_para->wl_rssi_step[i]) {
			if (rssi_step_pre != i) {
				rssi_step_pre = i;
				change_map |=  BIT(BTC_WLSTATUS_CHANGE_RSSI);
			}
			break;
		} else if (i == 3)
			rssi_step_pre = 4;
	}

	/* WL Link info */
	pBtCoexist->btc_get(pBtCoexist, BTC_GET_BL_WIFI_LINK_INFO, &link_info);
	if (link_info.link_mode != pBtCoexist->wifi_link_info.link_mode ||
	    link_info.sta_center_channel !=
	    		pBtCoexist->wifi_link_info.sta_center_channel ||
	    link_info.p2p_center_channel !=
	    		pBtCoexist->wifi_link_info.p2p_center_channel ||
	    link_info.bany_client_join_go !=
	    		pBtCoexist->wifi_link_info.bany_client_join_go) {
		change_map |=  BIT(BTC_WLSTATUS_CHANGE_LINKINFO);
		pBtCoexist->wifi_link_info = link_info;
	}

	/* WL Traffic Direction */
	pBtCoexist->btc_get(pBtCoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION,
			    &traffic_dir);
	if (wl_busy && traffic_dir !=
			pBtCoexist->wifi_link_info_ext.traffic_dir) {
		change_map |=  BIT(BTC_WLSTATUS_CHANGE_DIRECTION);
		pBtCoexist->wifi_link_info_ext.traffic_dir = traffic_dir;
	}

	RTW_DBG("[BTC], %s(): change_map = 0x%x\n", __func__, change_map);

	return change_map;
}

void EXhalbtcoutsrc_status_monitor(PBTC_COEXIST pBtCoexist)
{
	u32 timer_up_type = 0, wl_status_change_type = 0;

	timer_up_type = EXhalbtcoutsrc_CoexTimerCheck(pBtCoexist);
	if (timer_up_type != 0)
		EXhalbtcoutsrc_TimerNotify(pBtCoexist, timer_up_type);

	wl_status_change_type =  EXhalbtcoutsrc_WLStatusCheck(pBtCoexist);
	if (wl_status_change_type != 0)
		EXhalbtcoutsrc_WLStatusChangeNotify(pBtCoexist, wl_status_change_type);

	rtw_btc_ex_periodical(pBtCoexist);
}
#endif

void EXhalbtcoutsrc_periodical(PBTC_COEXIST pBtCoexist)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;
	pBtCoexist->statistics.cnt_periodical++;

	/* Periodical should be called in cmd thread, */
	/* don't need to leave low power again
	*	halbtcoutsrc_LeaveLowPower(pBtCoexist); */
#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
	EXhalbtcoutsrc_status_monitor(pBtCoexist);
#else

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8821A
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821a2ant_periodical(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1) {
			if (!halbtcoutsrc_UnderIps(pBtCoexist))
				ex_halbtc8821a1ant_periodical(pBtCoexist);
		}
#endif
	}

#ifdef CONFIG_RTL8723B
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_periodical(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_periodical(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8723D
	else if (IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723d2ant_periodical(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723d1ant_periodical(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8703B
	else if (IS_HARDWARE_TYPE_8703B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8703b1ant_periodical(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8192E
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8192e2ant_periodical(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8192e1ant_periodical(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8812A
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8812a2ant_periodical(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8812a1ant_periodical(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8822B
	else if (IS_HARDWARE_TYPE_8822B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8822b1ant_periodical(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8822b2ant_periodical(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8821C
	else if (IS_HARDWARE_TYPE_8821C(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821c2ant_periodical(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821c1ant_periodical(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8814A
	else if (IS_HARDWARE_TYPE_8814A(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8814a2ant_periodical(pBtCoexist);
	}
#endif

#endif

	/*	halbtcoutsrc_NormalLowPower(pBtCoexist); */
}

void EXhalbtcoutsrc_dbg_control(PBTC_COEXIST pBtCoexist, u8 opCode, u8 opLen, u8 *pData)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cnt_dbg_ctrl++;

	/* This function doesn't be called yet, */
	/* default no need to leave low power to avoid deadlock
	*	halbtcoutsrc_LeaveLowPower(pBtCoexist); */
#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
	/* rtw_btc_ex_dbg_control(pBtCoexist, opCode, opLen, pData); */
#else

	if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8192E
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8192e1ant_dbg_control(pBtCoexist, opCode, opLen, pData);
#endif
	}

#ifdef CONFIG_RTL8812A
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8812a2ant_dbg_control(pBtCoexist, opCode, opLen, pData);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8812a1ant_dbg_control(pBtCoexist, opCode, opLen, pData);
	}
#endif

#ifdef CONFIG_RTL8822B
	else if (IS_HARDWARE_TYPE_8822B(pBtCoexist->Adapter))
		if(pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8822b1ant_dbg_control(pBtCoexist, opCode, opLen, pData);
#endif

#endif

	/*	halbtcoutsrc_NormalLowPower(pBtCoexist); */
}

#if 0
void
EXhalbtcoutsrc_AntennaDetection(
		PBTC_COEXIST			pBtCoexist,
		u32					centFreq,
		u32					offset,
		u32					span,
		u32					seconds
)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	/* Need to refine the following power save operations to enable this function in the future */
#if 0
	IPSDisable(pBtCoexist->Adapter, FALSE, 0);
	LeisurePSLeave(pBtCoexist->Adapter, LPS_DISABLE_BT_COEX);
#endif

	if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_AntennaDetection(pBtCoexist, centFreq, offset, span, seconds);
	}

	/* IPSReturn(pBtCoexist->Adapter, 0xff); */
}
#endif

void EXhalbtcoutsrc_StackUpdateProfileInfo(void)
{
#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
	PBTC_COEXIST pBtCoexist = &GLBtCoexist;
	PADAPTER padapter = NULL;
	PBT_MGNT pBtMgnt = NULL;
	u8 i;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	padapter = (PADAPTER)pBtCoexist->Adapter;
	pBtMgnt = &padapter->coex_info.BtMgnt;

	pBtCoexist->stack_info.profile_notified = _TRUE;

	pBtCoexist->stack_info.num_of_link =
		pBtMgnt->ExtConfig.NumberOfACL + pBtMgnt->ExtConfig.NumberOfSCO;

	/* reset first */
	pBtCoexist->stack_info.bt_link_exist = _FALSE;
	pBtCoexist->stack_info.sco_exist = _FALSE;
	pBtCoexist->stack_info.acl_exist = _FALSE;
	pBtCoexist->stack_info.a2dp_exist = _FALSE;
	pBtCoexist->stack_info.hid_exist = _FALSE;
	pBtCoexist->stack_info.num_of_hid = 0;
	pBtCoexist->stack_info.pan_exist = _FALSE;

	if (!pBtMgnt->ExtConfig.NumberOfACL)
		pBtCoexist->stack_info.min_bt_rssi = 0;

	if (pBtCoexist->stack_info.num_of_link) {
		pBtCoexist->stack_info.bt_link_exist = _TRUE;
		if (pBtMgnt->ExtConfig.NumberOfSCO)
			pBtCoexist->stack_info.sco_exist = _TRUE;
		if (pBtMgnt->ExtConfig.NumberOfACL)
			pBtCoexist->stack_info.acl_exist = _TRUE;
	}

	for (i = 0; i < pBtMgnt->ExtConfig.NumberOfACL; i++) {
		if (BT_PROFILE_A2DP == pBtMgnt->ExtConfig.aclLink[i].BTProfile)
			pBtCoexist->stack_info.a2dp_exist = _TRUE;
		else if (BT_PROFILE_PAN == pBtMgnt->ExtConfig.aclLink[i].BTProfile)
			pBtCoexist->stack_info.pan_exist = _TRUE;
		else if (BT_PROFILE_HID == pBtMgnt->ExtConfig.aclLink[i].BTProfile) {
			pBtCoexist->stack_info.hid_exist = _TRUE;
			pBtCoexist->stack_info.num_of_hid++;
		} else
			pBtCoexist->stack_info.unknown_acl_exist = _TRUE;
	}
#endif /* CONFIG_BT_COEXIST_SOCKET_TRX */
}

void EXhalbtcoutsrc_UpdateMinBtRssi(s8 btRssi)
{
	PBTC_COEXIST pBtCoexist = &GLBtCoexist;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->stack_info.min_bt_rssi = btRssi;
}

void EXhalbtcoutsrc_SetHciVersion(u16 hciVersion)
{
	PBTC_COEXIST pBtCoexist = &GLBtCoexist;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->stack_info.hci_version = hciVersion;
}

void EXhalbtcoutsrc_SetBtPatchVersion(u16 btHciVersion, u16 btPatchVersion)
{
	PBTC_COEXIST pBtCoexist = &GLBtCoexist;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->bt_info.bt_real_fw_ver = btPatchVersion;
	pBtCoexist->bt_info.bt_hci_ver = btHciVersion;
}

#if 0
void EXhalbtcoutsrc_SetBtExist(u8 bBtExist)
{
	GLBtCoexist.boardInfo.bBtExist = bBtExist;
}
#endif
void EXhalbtcoutsrc_SetChipType(u8 chipType)
{
	switch (chipType) {
	default:
	case BT_2WIRE:
	case BT_ISSC_3WIRE:
	case BT_ACCEL:
	case BT_RTL8756:
		GLBtCoexist.board_info.bt_chip_type = BTC_CHIP_UNDEF;
		break;
	case BT_CSR_BC4:
		GLBtCoexist.board_info.bt_chip_type = BTC_CHIP_CSR_BC4;
		break;
	case BT_CSR_BC8:
		GLBtCoexist.board_info.bt_chip_type = BTC_CHIP_CSR_BC8;
		break;
	case BT_RTL8723A:
		GLBtCoexist.board_info.bt_chip_type = BTC_CHIP_RTL8723A;
		break;
	case BT_RTL8821:
		GLBtCoexist.board_info.bt_chip_type = BTC_CHIP_RTL8821;
		break;
	case BT_RTL8723B:
		GLBtCoexist.board_info.bt_chip_type = BTC_CHIP_RTL8723B;
		break;
	}
}

void EXhalbtcoutsrc_SetAntNum(u8 type, u8 antNum)
{
	if (BT_COEX_ANT_TYPE_PG == type) {
		GLBtCoexist.board_info.pg_ant_num = antNum;
		GLBtCoexist.board_info.btdm_ant_num = antNum;
#if 0
		/* The antenna position: Main (default) or Aux for pgAntNum=2 && btdmAntNum =1 */
		/* The antenna position should be determined by auto-detect mechanism */
		/* The following is assumed to main, and those must be modified if y auto-detect mechanism is ready */
		if ((GLBtCoexist.board_info.pg_ant_num == 2) && (GLBtCoexist.board_info.btdm_ant_num == 1))
			GLBtCoexist.board_info.btdm_ant_pos = BTC_ANTENNA_AT_MAIN_PORT;
		else
			GLBtCoexist.board_info.btdm_ant_pos = BTC_ANTENNA_AT_MAIN_PORT;
#endif
	} else if (BT_COEX_ANT_TYPE_ANTDIV == type) {
		GLBtCoexist.board_info.btdm_ant_num = antNum;
		/* GLBtCoexist.boardInfo.btdmAntPos = BTC_ANTENNA_AT_MAIN_PORT;	 */
	} else if (BT_COEX_ANT_TYPE_DETECTED == type) {
		GLBtCoexist.board_info.btdm_ant_num = antNum;
		/* GLBtCoexist.boardInfo.btdmAntPos = BTC_ANTENNA_AT_MAIN_PORT; */
	}
}

/*
 * Currently used by 8723b only, S0 or S1
 *   */
void EXhalbtcoutsrc_SetSingleAntPath(u8 singleAntPath)
{
	GLBtCoexist.board_info.single_ant_path = singleAntPath;
}

void EXhalbtcoutsrc_DisplayBtCoexInfo(PBTC_COEXIST pBtCoexist)
{
	HAL_DATA_TYPE	*pHalData = NULL;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	halbtcoutsrc_LeaveLowPower(pBtCoexist);

	/* To prevent the racing with IPS enter */
	halbtcoutsrc_EnterPwrLock(pBtCoexist);

#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
	pHalData = GET_HAL_DATA((PADAPTER)pBtCoexist->Adapter);

	if (pHalData->EEPROMBluetoothCoexist == _TRUE)
		rtw_btc_ex_display_coex_info(pBtCoexist);
#else

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8821A
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821a2ant_display_coex_info(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821a1ant_display_coex_info(pBtCoexist);
#endif
	}

#ifdef CONFIG_RTL8723B
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_display_coex_info(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_display_coex_info(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8703B
	else if (IS_HARDWARE_TYPE_8703B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8703b1ant_display_coex_info(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8723D
	else if (IS_HARDWARE_TYPE_8723D(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723d2ant_display_coex_info(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723d1ant_display_coex_info(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8192E
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8192e2ant_display_coex_info(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8192e1ant_display_coex_info(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8812A
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8812a2ant_display_coex_info(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8812a1ant_display_coex_info(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8822B
	else if (IS_HARDWARE_TYPE_8822B(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8822b1ant_display_coex_info(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8822b2ant_display_coex_info(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8821C
	else if (IS_HARDWARE_TYPE_8821C(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821c2ant_display_coex_info(pBtCoexist);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821c1ant_display_coex_info(pBtCoexist);
	}
#endif

#ifdef CONFIG_RTL8814A
	else if (IS_HARDWARE_TYPE_8814A(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8814a2ant_display_coex_info(pBtCoexist);
	}
#endif

#endif

	halbtcoutsrc_ExitPwrLock(pBtCoexist);

	halbtcoutsrc_NormalLowPower(pBtCoexist);
}

void EXhalbtcoutsrc_DisplayAntDetection(PBTC_COEXIST pBtCoexist)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	halbtcoutsrc_LeaveLowPower(pBtCoexist);

	if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8723B
		if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_display_ant_detection(pBtCoexist);
#endif
	}

	halbtcoutsrc_NormalLowPower(pBtCoexist);
}

void ex_halbtcoutsrc_pta_off_on_notify(PBTC_COEXIST pBtCoexist, u8 bBTON)
{
	if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8812A
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8812a2ant_pta_off_on_notify(pBtCoexist, (bBTON == _TRUE) ? BTC_BT_ON : BTC_BT_OFF);
#endif
	}

#ifdef CONFIG_RTL8814A
	else if (IS_HARDWARE_TYPE_8814A(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8814a2ant_pta_off_on_notify(pBtCoexist, (bBTON == _TRUE) ? BTC_BT_ON : BTC_BT_OFF);
	}
#endif
}

void EXhalbtcoutsrc_set_rfe_type(u8 type)
{
	GLBtCoexist.board_info.rfe_type= type;
}

#ifdef CONFIG_RF4CE_COEXIST
void EXhalbtcoutsrc_set_rf4ce_link_state(u8 state)
{
	GLBtCoexist.rf4ce_info.link_state = state;
}

u8 EXhalbtcoutsrc_get_rf4ce_link_state(void)
{
	return GLBtCoexist.rf4ce_info.link_state;
}
#endif

void EXhalbtcoutsrc_switchband_notify(struct btc_coexist *pBtCoexist, u8 type)
{
	if(!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;
	
	if(pBtCoexist->manual_control)
		return;

	/* Driver should guarantee that the HW status isn't in low power mode */
	/* halbtcoutsrc_LeaveLowPower(pBtCoexist); */
#if (CONFIG_BTCOEX_SUPPORT_BTC_CMN == 1)
	rtw_btc_ex_switchband_notify(pBtCoexist, type);
#else

	if(IS_HARDWARE_TYPE_8822B(pBtCoexist->Adapter)) {
#ifdef CONFIG_RTL8822B
		if(pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8822b1ant_switchband_notify(pBtCoexist, type);
		else if(pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8822b2ant_switchband_notify(pBtCoexist, type);
#endif
	}

#ifdef CONFIG_RTL8821C
	else if (IS_HARDWARE_TYPE_8821C(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8821c2ant_switchband_notify(pBtCoexist, type);
		else if (pBtCoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8821c1ant_switchband_notify(pBtCoexist, type);
	}
#endif

#ifdef CONFIG_RTL8814A
	else if (IS_HARDWARE_TYPE_8814A(pBtCoexist->Adapter)) {
		if (pBtCoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8814a2ant_switchband_notify(pBtCoexist, type);
	}
#endif

#endif

	/* halbtcoutsrc_NormalLowPower(pBtCoexist); */
}

u8 EXhalbtcoutsrc_rate_id_to_btc_rate_id(u8 rate_id)
{
	u8 btc_rate_id = BTC_UNKNOWN;

	switch (rate_id) {
		/* CCK rates */
		case DESC_RATE1M:
			btc_rate_id = BTC_CCK_1;
			break;
		case DESC_RATE2M:
			btc_rate_id = BTC_CCK_2;
			break;
		case DESC_RATE5_5M:
			btc_rate_id = BTC_CCK_5_5;
			break;
		case DESC_RATE11M:
			btc_rate_id = BTC_CCK_11;
			break;

		/* OFDM rates */
		case DESC_RATE6M:
			btc_rate_id = BTC_OFDM_6;
			break;
		case DESC_RATE9M:
			btc_rate_id = BTC_OFDM_9;
			break;
		case DESC_RATE12M:
			btc_rate_id = BTC_OFDM_12;
			break;
		case DESC_RATE18M:
			btc_rate_id = BTC_OFDM_18;
			break;
		case DESC_RATE24M:
			btc_rate_id = BTC_OFDM_24;
			break;
		case DESC_RATE36M:
			btc_rate_id = BTC_OFDM_36;
			break;
		case DESC_RATE48M:
			btc_rate_id = BTC_OFDM_48;
			break;
		case DESC_RATE54M:
			btc_rate_id = BTC_OFDM_54;
			break;

		/* MCS rates */
		case DESC_RATEMCS0:
			btc_rate_id = BTC_MCS_0;
			break;
		case DESC_RATEMCS1:
			btc_rate_id = BTC_MCS_1;
			break;
		case DESC_RATEMCS2:
			btc_rate_id = BTC_MCS_2;
			break;
		case DESC_RATEMCS3:
			btc_rate_id = BTC_MCS_3;
			break;
		case DESC_RATEMCS4:
			btc_rate_id = BTC_MCS_4;
			break;
		case DESC_RATEMCS5:
			btc_rate_id = BTC_MCS_5;
			break;
		case DESC_RATEMCS6:
			btc_rate_id = BTC_MCS_6;
			break;
		case DESC_RATEMCS7:
			btc_rate_id = BTC_MCS_7;
			break;
		case DESC_RATEMCS8:
			btc_rate_id = BTC_MCS_8;
			break;
		case DESC_RATEMCS9:
			btc_rate_id = BTC_MCS_9;
			break;
		case DESC_RATEMCS10:
			btc_rate_id = BTC_MCS_10;
			break;
		case DESC_RATEMCS11:
			btc_rate_id = BTC_MCS_11;
			break;
		case DESC_RATEMCS12:
			btc_rate_id = BTC_MCS_12;
			break;
		case DESC_RATEMCS13:
			btc_rate_id = BTC_MCS_13;
			break;
		case DESC_RATEMCS14:
			btc_rate_id = BTC_MCS_14;
			break;
		case DESC_RATEMCS15:
			btc_rate_id = BTC_MCS_15;
			break;
		case DESC_RATEMCS16:
			btc_rate_id = BTC_MCS_16;
			break;
		case DESC_RATEMCS17:
			btc_rate_id = BTC_MCS_17;
			break;
		case DESC_RATEMCS18:
			btc_rate_id = BTC_MCS_18;
			break;
		case DESC_RATEMCS19:
			btc_rate_id = BTC_MCS_19;
			break;
		case DESC_RATEMCS20:
			btc_rate_id = BTC_MCS_20;
			break;
		case DESC_RATEMCS21:
			btc_rate_id = BTC_MCS_21;
			break;
		case DESC_RATEMCS22:
			btc_rate_id = BTC_MCS_22;
			break;
		case DESC_RATEMCS23:
			btc_rate_id = BTC_MCS_23;
			break;
		case DESC_RATEMCS24:
			btc_rate_id = BTC_MCS_24;
			break;
		case DESC_RATEMCS25:
			btc_rate_id = BTC_MCS_25;
			break;
		case DESC_RATEMCS26:
			btc_rate_id = BTC_MCS_26;
			break;
		case DESC_RATEMCS27:
			btc_rate_id = BTC_MCS_27;
			break;
		case DESC_RATEMCS28:
			btc_rate_id = BTC_MCS_28;
			break;
		case DESC_RATEMCS29:
			btc_rate_id = BTC_MCS_29;
			break;
		case DESC_RATEMCS30:
			btc_rate_id = BTC_MCS_30;
			break;
		case DESC_RATEMCS31:
			btc_rate_id = BTC_MCS_31;
			break;
			
		case DESC_RATEVHTSS1MCS0:
			btc_rate_id = BTC_VHT_1SS_MCS_0;
			break;
		case DESC_RATEVHTSS1MCS1:
			btc_rate_id = BTC_VHT_1SS_MCS_1;
			break;
		case DESC_RATEVHTSS1MCS2:
			btc_rate_id = BTC_VHT_1SS_MCS_2;
			break;
		case DESC_RATEVHTSS1MCS3:
			btc_rate_id = BTC_VHT_1SS_MCS_3;
			break;
		case DESC_RATEVHTSS1MCS4:
			btc_rate_id = BTC_VHT_1SS_MCS_4;
			break;
		case DESC_RATEVHTSS1MCS5:
			btc_rate_id = BTC_VHT_1SS_MCS_5;
			break;
		case DESC_RATEVHTSS1MCS6:
			btc_rate_id = BTC_VHT_1SS_MCS_6;
			break;
		case DESC_RATEVHTSS1MCS7:
			btc_rate_id = BTC_VHT_1SS_MCS_7;
			break;
		case DESC_RATEVHTSS1MCS8:
			btc_rate_id = BTC_VHT_1SS_MCS_8;
			break;
		case DESC_RATEVHTSS1MCS9:
			btc_rate_id = BTC_VHT_1SS_MCS_9;
			break;

		case DESC_RATEVHTSS2MCS0:
			btc_rate_id = BTC_VHT_2SS_MCS_0;
			break;
		case DESC_RATEVHTSS2MCS1:
			btc_rate_id = BTC_VHT_2SS_MCS_1;
			break;
		case DESC_RATEVHTSS2MCS2:
			btc_rate_id = BTC_VHT_2SS_MCS_2;
			break;
		case DESC_RATEVHTSS2MCS3:
			btc_rate_id = BTC_VHT_2SS_MCS_3;
			break;
		case DESC_RATEVHTSS2MCS4:
			btc_rate_id = BTC_VHT_2SS_MCS_4;
			break;
		case DESC_RATEVHTSS2MCS5:
			btc_rate_id = BTC_VHT_2SS_MCS_5;
			break;
		case DESC_RATEVHTSS2MCS6:
			btc_rate_id = BTC_VHT_2SS_MCS_6;
			break;
		case DESC_RATEVHTSS2MCS7:
			btc_rate_id = BTC_VHT_2SS_MCS_7;
			break;
		case DESC_RATEVHTSS2MCS8:
			btc_rate_id = BTC_VHT_2SS_MCS_8;
			break;
		case DESC_RATEVHTSS2MCS9:
			btc_rate_id = BTC_VHT_2SS_MCS_9;
			break;

		case DESC_RATEVHTSS3MCS0:
			btc_rate_id = BTC_VHT_3SS_MCS_0;
			break;
		case DESC_RATEVHTSS3MCS1:
			btc_rate_id = BTC_VHT_3SS_MCS_1;
			break;
		case DESC_RATEVHTSS3MCS2:
			btc_rate_id = BTC_VHT_3SS_MCS_2;
			break;
		case DESC_RATEVHTSS3MCS3:
			btc_rate_id = BTC_VHT_3SS_MCS_3;
			break;
		case DESC_RATEVHTSS3MCS4:
			btc_rate_id = BTC_VHT_3SS_MCS_4;
			break;
		case DESC_RATEVHTSS3MCS5:
			btc_rate_id = BTC_VHT_3SS_MCS_5;
			break;
		case DESC_RATEVHTSS3MCS6:
			btc_rate_id = BTC_VHT_3SS_MCS_6;
			break;
		case DESC_RATEVHTSS3MCS7:
			btc_rate_id = BTC_VHT_3SS_MCS_7;
			break;
		case DESC_RATEVHTSS3MCS8:
			btc_rate_id = BTC_VHT_3SS_MCS_8;
			break;
		case DESC_RATEVHTSS3MCS9:
			btc_rate_id = BTC_VHT_3SS_MCS_9;
			break;

		case DESC_RATEVHTSS4MCS0:
			btc_rate_id = BTC_VHT_4SS_MCS_0;
			break;
		case DESC_RATEVHTSS4MCS1:
			btc_rate_id = BTC_VHT_4SS_MCS_1;
			break;
		case DESC_RATEVHTSS4MCS2:
			btc_rate_id = BTC_VHT_4SS_MCS_2;
			break;
		case DESC_RATEVHTSS4MCS3:
			btc_rate_id = BTC_VHT_4SS_MCS_3;
			break;
		case DESC_RATEVHTSS4MCS4:
			btc_rate_id = BTC_VHT_4SS_MCS_4;
			break;
		case DESC_RATEVHTSS4MCS5:
			btc_rate_id = BTC_VHT_4SS_MCS_5;
			break;
		case DESC_RATEVHTSS4MCS6:
			btc_rate_id = BTC_VHT_4SS_MCS_6;
			break;
		case DESC_RATEVHTSS4MCS7:
			btc_rate_id = BTC_VHT_4SS_MCS_7;
			break;
		case DESC_RATEVHTSS4MCS8:
			btc_rate_id = BTC_VHT_4SS_MCS_8;
			break;
		case DESC_RATEVHTSS4MCS9:
			btc_rate_id = BTC_VHT_4SS_MCS_9;
			break;
	}
	
	return btc_rate_id;
}

/*
 * Description:
 *	Run BT-Coexist mechansim or not
 *
 */
void hal_btcoex_SetBTCoexist(PADAPTER padapter, u8 bBtExist)
{
	PHAL_DATA_TYPE	pHalData;


	pHalData = GET_HAL_DATA(padapter);
	pHalData->bt_coexist.bBtExist = bBtExist;
}

/*
 * Dewcription:
 *	Check is co-exist mechanism enabled or not
 *
 * Return:
 *	_TRUE	Enable BT co-exist mechanism
 *	_FALSE	Disable BT co-exist mechanism
 */
u8 hal_btcoex_IsBtExist(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData;


	pHalData = GET_HAL_DATA(padapter);
	return pHalData->bt_coexist.bBtExist;
}

u8 hal_btcoex_IsBtDisabled(PADAPTER padapter)
{
	if (!hal_btcoex_IsBtExist(padapter))
		return _TRUE;

	if (GLBtCoexist.bt_info.bt_disabled)
		return _TRUE;
	else
		return _FALSE;
}

void hal_btcoex_SetChipType(PADAPTER padapter, u8 chipType)
{
	PHAL_DATA_TYPE	pHalData;

	pHalData = GET_HAL_DATA(padapter);
	pHalData->bt_coexist.btChipType = chipType;
}

void hal_btcoex_SetPgAntNum(PADAPTER padapter, u8 antNum)
{
	PHAL_DATA_TYPE	pHalData;

	pHalData = GET_HAL_DATA(padapter);

	pHalData->bt_coexist.btTotalAntNum = antNum;
}

u8 hal_btcoex_Initialize(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8 ret;

	_rtw_memset(&GLBtCoexist, 0, sizeof(GLBtCoexist));

	ret = EXhalbtcoutsrc_InitlizeVariables((void *)padapter);

	return ret;
}

void hal_btcoex_PowerOnSetting(PADAPTER padapter)
{
	EXhalbtcoutsrc_PowerOnSetting(&GLBtCoexist);
}

void hal_btcoex_AntInfoSetting(PADAPTER padapter)
{
	hal_btcoex_SetBTCoexist(padapter, rtw_btcoex_get_bt_coexist(padapter));
	hal_btcoex_SetChipType(padapter, rtw_btcoex_get_chip_type(padapter));
	hal_btcoex_SetPgAntNum(padapter, rtw_btcoex_get_pg_ant_num(padapter));

	EXhalbtcoutsrc_AntInfoSetting(padapter);
}

void hal_btcoex_PowerOffSetting(PADAPTER padapter)
{
	/* Clear the WiFi on/off bit in scoreboard reg. if necessary */
	if (IS_HARDWARE_TYPE_8703B(padapter) || IS_HARDWARE_TYPE_8723D(padapter)
		|| IS_HARDWARE_TYPE_8821C(padapter) || IS_HARDWARE_TYPE_8822B(padapter)
		|| IS_HARDWARE_TYPE_8822C(padapter))
		rtw_write16(padapter, 0xaa, 0x8000);
}

void hal_btcoex_PreLoadFirmware(PADAPTER padapter)
{
	EXhalbtcoutsrc_PreLoadFirmware(&GLBtCoexist);
}

void hal_btcoex_InitHwConfig(PADAPTER padapter, u8 bWifiOnly)
{
	if (!hal_btcoex_IsBtExist(padapter))
		return;

	EXhalbtcoutsrc_init_hw_config(&GLBtCoexist, bWifiOnly);
	EXhalbtcoutsrc_init_coex_dm(&GLBtCoexist);
}

void hal_btcoex_IpsNotify(PADAPTER padapter, u8 type)
{
	EXhalbtcoutsrc_ips_notify(&GLBtCoexist, type);
}

void hal_btcoex_LpsNotify(PADAPTER padapter, u8 type)
{
	EXhalbtcoutsrc_lps_notify(&GLBtCoexist, type);
}

void hal_btcoex_ScanNotify(PADAPTER padapter, u8 type)
{
	EXhalbtcoutsrc_scan_notify(&GLBtCoexist, type);
}

void hal_btcoex_ConnectNotify(PADAPTER padapter, u8 action)
{
	u8 assoType = 0;
	u8 is_5g_band = _FALSE;

	is_5g_band = (padapter->mlmeextpriv.cur_channel > 14) ? _TRUE : _FALSE;

	if (action == _TRUE) {
		if (is_5g_band == _TRUE)
			assoType = BTC_ASSOCIATE_5G_START;
		else
			assoType = BTC_ASSOCIATE_START;
	}
	else {
		if (is_5g_band == _TRUE)
			assoType = BTC_ASSOCIATE_5G_FINISH;
		else
			assoType = BTC_ASSOCIATE_FINISH;
	}
	
	EXhalbtcoutsrc_connect_notify(&GLBtCoexist, assoType);
}

void hal_btcoex_MediaStatusNotify(PADAPTER padapter, u8 mediaStatus)
{
	EXhalbtcoutsrc_media_status_notify(&GLBtCoexist, mediaStatus);
}

void hal_btcoex_SpecialPacketNotify(PADAPTER padapter, u8 pktType)
{
	EXhalbtcoutsrc_specific_packet_notify(&GLBtCoexist, pktType);
}

void hal_btcoex_IQKNotify(PADAPTER padapter, u8 state)
{
	GLBtcWiFiInIQKState = state;
}

void hal_btcoex_BtInfoNotify(PADAPTER padapter, u8 length, u8 *tmpBuf)
{
	if (GLBtcWiFiInIQKState == _TRUE)
		return;

	EXhalbtcoutsrc_bt_info_notify(&GLBtCoexist, tmpBuf, length);
}

void hal_btcoex_BtMpRptNotify(PADAPTER padapter, u8 length, u8 *tmpBuf)
{
	u8 extid, status, len, seq;


	if (GLBtcBtMpRptWait == _FALSE)
		return;

	if ((length < 3) || (!tmpBuf))
		return;

	extid = tmpBuf[0];
	/* not response from BT FW then exit*/
	switch (extid) {
	case C2H_WIFI_FW_ACTIVE_RSP:
		GLBtcBtMpRptWiFiOK = _TRUE;
		break;

	case C2H_TRIG_BY_BT_FW:
		GLBtcBtMpRptBTOK = _TRUE;

		status = tmpBuf[1] & 0xF;
		len = length - 3;
		seq = tmpBuf[2] >> 4;

		GLBtcBtMpRptSeq = seq;
		GLBtcBtMpRptStatus = status;
		_rtw_memcpy(GLBtcBtMpRptRsp, tmpBuf + 3, len);
		GLBtcBtMpRptRspSize = len;

		break;

	default:
		return;
	}

	if ((GLBtcBtMpRptWiFiOK == _TRUE) && (GLBtcBtMpRptBTOK == _TRUE)) {
		GLBtcBtMpRptWait = _FALSE;
		_cancel_timer_ex(&GLBtcBtMpOperTimer);
		_rtw_up_sema(&GLBtcBtMpRptSema);
	}
}

void hal_btcoex_SuspendNotify(PADAPTER padapter, u8 state)
{
	switch (state) {
	case BTCOEX_SUSPEND_STATE_SUSPEND:
		EXhalbtcoutsrc_pnp_notify(&GLBtCoexist, BTC_WIFI_PNP_SLEEP);
		break;
	case BTCOEX_SUSPEND_STATE_SUSPEND_KEEP_ANT:
		/* should switch to "#if 1" once all ICs' coex. revision are upgraded to support the KEEP_ANT case */
#if 0
		EXhalbtcoutsrc_pnp_notify(&GLBtCoexist, BTC_WIFI_PNP_SLEEP_KEEP_ANT);
#else
		EXhalbtcoutsrc_pnp_notify(&GLBtCoexist, BTC_WIFI_PNP_SLEEP);
		EXhalbtcoutsrc_pnp_notify(&GLBtCoexist, BTC_WIFI_PNP_SLEEP_KEEP_ANT);
#endif
		break;
	case BTCOEX_SUSPEND_STATE_RESUME:
#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
		/* re-download FW after resume, inform WL FW port number */
		rtw_hal_set_wifi_btc_port_id_cmd(GLBtCoexist.Adapter);
#endif
		EXhalbtcoutsrc_pnp_notify(&GLBtCoexist, BTC_WIFI_PNP_WAKE_UP);
		break;
	}
}

void hal_btcoex_HaltNotify(PADAPTER padapter, u8 do_halt)
{
	if (do_halt == 1)
		EXhalbtcoutsrc_halt_notify(&GLBtCoexist);

	GLBtCoexist.bBinded = _FALSE;
	GLBtCoexist.Adapter = NULL;
}

void hal_btcoex_SwitchBtTRxMask(PADAPTER padapter)
{
	EXhalbtcoutsrc_SwitchBtTRxMask(&GLBtCoexist);
}

void hal_btcoex_Hanlder(PADAPTER padapter)
{
	u32	bt_patch_ver;

	EXhalbtcoutsrc_periodical(&GLBtCoexist);

	if (GLBtCoexist.bt_info.bt_get_fw_ver == 0) {
		GLBtCoexist.btc_get(&GLBtCoexist, BTC_GET_U4_BT_PATCH_VER, &bt_patch_ver);
		GLBtCoexist.bt_info.bt_get_fw_ver = bt_patch_ver;
	}
}

s32 hal_btcoex_IsBTCoexRejectAMPDU(PADAPTER padapter)
{
	return (s32)GLBtCoexist.bt_info.reject_agg_pkt;
}

s32 hal_btcoex_IsBTCoexCtrlAMPDUSize(PADAPTER padapter)
{
	return (s32)GLBtCoexist.bt_info.bt_ctrl_agg_buf_size;
}

u32 hal_btcoex_GetAMPDUSize(PADAPTER padapter)
{
	return (u32)GLBtCoexist.bt_info.agg_buf_size;
}

void hal_btcoex_SetManualControl(PADAPTER padapter, u8 bmanual)
{
	GLBtCoexist.manual_control = bmanual;
}

u8 hal_btcoex_1Ant(PADAPTER padapter)
{
	if (hal_btcoex_IsBtExist(padapter) == _FALSE)
		return _FALSE;

	if (GLBtCoexist.board_info.btdm_ant_num == 1)
		return _TRUE;

	return _FALSE;
}

u8 hal_btcoex_IsBtControlLps(PADAPTER padapter)
{
	if (GLBtCoexist.bdontenterLPS == _TRUE)
		return _TRUE;
	
	if (hal_btcoex_IsBtExist(padapter) == _FALSE)
		return _FALSE;

	if (GLBtCoexist.bt_info.bt_disabled)
		return _FALSE;

	if (GLBtCoexist.bt_info.bt_ctrl_lps)
		return _TRUE;

	return _FALSE;
}

u8 hal_btcoex_IsLpsOn(PADAPTER padapter)
{
	if (GLBtCoexist.bdontenterLPS == _TRUE)
		return _FALSE;
	
	if (hal_btcoex_IsBtExist(padapter) == _FALSE)
		return _FALSE;

	if (GLBtCoexist.bt_info.bt_disabled)
		return _FALSE;

	if (GLBtCoexist.bt_info.bt_lps_on)
		return _TRUE;

	return _FALSE;
}

u8 hal_btcoex_RpwmVal(PADAPTER padapter)
{
	return GLBtCoexist.bt_info.rpwm_val;
}

u8 hal_btcoex_LpsVal(PADAPTER padapter)
{
	return GLBtCoexist.bt_info.lps_val;
}

u32 hal_btcoex_GetRaMask(PADAPTER padapter)
{
	if (!hal_btcoex_IsBtExist(padapter))
		return 0;

	if (GLBtCoexist.bt_info.bt_disabled)
		return 0;

	/* Modify by YiWei , suggest by Cosa and Jenyu
	 * Remove the limit antenna number , because 2 antenna case (ex: 8192eu)also want to get BT coex report rate mask.
	 */
	/*if (GLBtCoexist.board_info.btdm_ant_num != 1)
		return 0;*/

	return GLBtCoexist.bt_info.ra_mask;
}

u8 hal_btcoex_query_reduced_wl_pwr_lvl(PADAPTER padapter)
{
	return GLBtCoexist.coex_dm.cur_wl_pwr_lvl;
}

void hal_btcoex_set_reduced_wl_pwr_lvl(PADAPTER padapter, u8 val)
{
	GLBtCoexist.coex_dm.cur_wl_pwr_lvl = val;
}

void hal_btcoex_do_reduce_wl_pwr_lvl(PADAPTER padapter)
{
	halbtcoutsrc_reduce_wl_tx_power(&GLBtCoexist, 0);
}

void hal_btcoex_RecordPwrMode(PADAPTER padapter, u8 *pCmdBuf, u8 cmdLen)
{

	_rtw_memcpy(GLBtCoexist.pwrModeVal, pCmdBuf, cmdLen);
}

void hal_btcoex_DisplayBtCoexInfo(PADAPTER padapter, u8 *pbuf, u32 bufsize)
{
	PBTCDBGINFO pinfo;


	pinfo = &GLBtcDbgInfo;
	DBG_BT_INFO_INIT(pinfo, pbuf, bufsize);
	EXhalbtcoutsrc_DisplayBtCoexInfo(&GLBtCoexist);
	DBG_BT_INFO_INIT(pinfo, NULL, 0);
}

void hal_btcoex_SetDBG(PADAPTER padapter, u32 *pDbgModule)
{
	u32 i;


	if (NULL == pDbgModule)
		return;

	for (i = 0; i < COMP_MAX; i++)
		GLBtcDbgType[i] = pDbgModule[i];
}

u32 hal_btcoex_GetDBG(PADAPTER padapter, u8 *pStrBuf, u32 bufSize)
{
	s32 count;
	u8 *pstr;
	u32 leftSize;


	if ((NULL == pStrBuf) || (0 == bufSize))
		return 0;

	count = 0;
	pstr = pStrBuf;
	leftSize = bufSize;
	/*	RTW_INFO(FUNC_ADPT_FMT ": bufsize=%d\n", FUNC_ADPT_ARG(padapter), bufSize); */

	count = rtw_sprintf(pstr, leftSize, "#define DBG\t%d\n", DBG);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;

	count = rtw_sprintf(pstr, leftSize, "BTCOEX Debug Setting:\n");
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;

	count = rtw_sprintf(pstr, leftSize,
			    "COMP_COEX: 0x%08X\n\n",
			    GLBtcDbgType[COMP_COEX]);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;

#if 0
	count = rtw_sprintf(pstr, leftSize, "INTERFACE Debug Setting Definition:\n");
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[0]=%d for INTF_INIT\n",
		    GLBtcDbgType[BTC_MSG_INTERFACE] & INTF_INIT ? 1 : 0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[2]=%d for INTF_NOTIFY\n\n",
		    GLBtcDbgType[BTC_MSG_INTERFACE] & INTF_NOTIFY ? 1 : 0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;

	count = rtw_sprintf(pstr, leftSize, "ALGORITHM Debug Setting Definition:\n");
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[0]=%d for BT_RSSI_STATE\n",
		GLBtcDbgType[BTC_MSG_ALGORITHM] & ALGO_BT_RSSI_STATE ? 1 : 0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[1]=%d for WIFI_RSSI_STATE\n",
		GLBtcDbgType[BTC_MSG_ALGORITHM] & ALGO_WIFI_RSSI_STATE ? 1 : 0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[2]=%d for BT_MONITOR\n",
		    GLBtcDbgType[BTC_MSG_ALGORITHM] & ALGO_BT_MONITOR ? 1 : 0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[3]=%d for TRACE\n",
		    GLBtcDbgType[BTC_MSG_ALGORITHM] & ALGO_TRACE ? 1 : 0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[4]=%d for TRACE_FW\n",
		    GLBtcDbgType[BTC_MSG_ALGORITHM] & ALGO_TRACE_FW ? 1 : 0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[5]=%d for TRACE_FW_DETAIL\n",
		GLBtcDbgType[BTC_MSG_ALGORITHM] & ALGO_TRACE_FW_DETAIL ? 1 : 0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[6]=%d for TRACE_FW_EXEC\n",
		GLBtcDbgType[BTC_MSG_ALGORITHM] & ALGO_TRACE_FW_EXEC ? 1 : 0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[7]=%d for TRACE_SW\n",
		    GLBtcDbgType[BTC_MSG_ALGORITHM] & ALGO_TRACE_SW ? 1 : 0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[8]=%d for TRACE_SW_DETAIL\n",
		GLBtcDbgType[BTC_MSG_ALGORITHM] & ALGO_TRACE_SW_DETAIL ? 1 : 0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[9]=%d for TRACE_SW_EXEC\n",
		GLBtcDbgType[BTC_MSG_ALGORITHM] & ALGO_TRACE_SW_EXEC ? 1 : 0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
#endif

exit:
	count = pstr - pStrBuf;
	/*	RTW_INFO(FUNC_ADPT_FMT ": usedsize=%d\n", FUNC_ADPT_ARG(padapter), count); */

	return count;
}

u8 hal_btcoex_IncreaseScanDeviceNum(PADAPTER padapter)
{
	if (!hal_btcoex_IsBtExist(padapter))
		return _FALSE;

	if (GLBtCoexist.bt_info.increase_scan_dev_num)
		return _TRUE;

	return _FALSE;
}

u8 hal_btcoex_IsBtLinkExist(PADAPTER padapter)
{
	if (GLBtCoexist.bt_link_info.bt_link_exist)
		return _TRUE;

	return _FALSE;
}

void hal_btcoex_SetBtPatchVersion(PADAPTER padapter, u16 btHciVer, u16 btPatchVer)
{
	EXhalbtcoutsrc_SetBtPatchVersion(btHciVer, btPatchVer);
}

void hal_btcoex_SetHciVersion(PADAPTER padapter, u16 hciVersion)
{
	EXhalbtcoutsrc_SetHciVersion(hciVersion);
}

void hal_btcoex_StackUpdateProfileInfo(void)
{
	EXhalbtcoutsrc_StackUpdateProfileInfo();
}

void hal_btcoex_pta_off_on_notify(PADAPTER padapter, u8 bBTON)
{
	ex_halbtcoutsrc_pta_off_on_notify(&GLBtCoexist, bBTON);
}

/*
 *	Description:
 *	Setting BT coex antenna isolation type .
 *	coex mechanisn/ spital stream/ best throughput
 *	anttype = 0	,	PSTDMA	/	2SS	/	0.5T	,	bad isolation , WiFi/BT ANT Distance<15cm , (<20dB) for 2,3 antenna
 *	anttype = 1	,	PSTDMA	/	1SS	/	0.5T	,	normal isolaiton , 50cm>WiFi/BT ANT Distance>15cm , (>20dB) for 2 antenna
 *	anttype = 2	,	TDMA	/	2SS	/	T ,		normal isolaiton , 50cm>WiFi/BT ANT Distance>15cm , (>20dB) for 3 antenna
 *	anttype = 3	,	no TDMA	/	1SS	/	0.5T	,	good isolation , WiFi/BT ANT Distance >50cm , (>40dB) for 2 antenna
 *	anttype = 4	,	no TDMA	/	2SS	/	T ,		good isolation , WiFi/BT ANT Distance >50cm , (>40dB) for 3 antenna
 *	wifi only throughput ~ T
 *	wifi/BT share one antenna with SPDT
 */
void hal_btcoex_SetAntIsolationType(PADAPTER padapter, u8 anttype)
{
	PHAL_DATA_TYPE pHalData;
	PBTC_COEXIST	pBtCoexist = &GLBtCoexist;

	/*RTW_INFO("####%s , anttype = %d  , %d\n" , __func__ , anttype , __LINE__); */
	pHalData = GET_HAL_DATA(padapter);


	pHalData->bt_coexist.btAntisolation = anttype;

	switch (pHalData->bt_coexist.btAntisolation) {
	case 0:
		pBtCoexist->board_info.ant_type = (u8)BTC_ANT_TYPE_0;
		break;
	case 1:
		pBtCoexist->board_info.ant_type = (u8)BTC_ANT_TYPE_1;
		break;
	case 2:
		pBtCoexist->board_info.ant_type = (u8)BTC_ANT_TYPE_2;
		break;
	case 3:
		pBtCoexist->board_info.ant_type = (u8)BTC_ANT_TYPE_3;
		break;
	case 4:
		pBtCoexist->board_info.ant_type = (u8)BTC_ANT_TYPE_4;
		break;
	}

}

#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
int
hal_btcoex_ParseAntIsolationConfigFile(
	PADAPTER		Adapter,
	char			*buffer
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32	i = 0 , j = 0;
	char	*szLine , *ptmp;
	int rtStatus = _SUCCESS;
	char param_value_string[10];
	u8 param_value;
	u8 anttype = 4;

	u8 ant_num = 3 , ant_distance = 50 , rfe_type = 1;

	typedef struct ant_isolation {
		char *param_name;  /* antenna isolation config parameter name */
		u8 *value; /* antenna isolation config parameter value */
	} ANT_ISOLATION;

	ANT_ISOLATION ant_isolation_param[] = {
		{"ANT_NUMBER" , &ant_num},
		{"ANT_DISTANCE" , &ant_distance},
		{"RFE_TYPE" , &rfe_type},
		{NULL , 0}
	};



	/* RTW_INFO("===>Hal_ParseAntIsolationConfigFile()\n" ); */

	ptmp = buffer;
	for (szLine = GetLineFromBuffer(ptmp) ; szLine != NULL; szLine = GetLineFromBuffer(ptmp)) {
		/* skip comment */
		if (IsCommentString(szLine))
			continue;

		/* RTW_INFO("%s : szLine = %s , strlen(szLine) = %d\n" , __func__ , szLine , strlen(szLine));*/
		for (j = 0 ; ant_isolation_param[j].param_name != NULL ; j++) {
			if (strstr(szLine , ant_isolation_param[j].param_name) != NULL) {
				i = 0;
				while (i < strlen(szLine)) {
					if (szLine[i] != '"')
						++i;
					else {
						/* skip only has one " */
						if (strpbrk(szLine , "\"") == strrchr(szLine , '"')) {
							RTW_INFO("Fail to parse parameters , format error!\n");
							break;
						}
						_rtw_memset((void *)param_value_string , 0 , 10);
						if (!ParseQualifiedString(szLine , &i , param_value_string , '"' , '"')) {
							RTW_INFO("Fail to parse parameters\n");
							return _FAIL;
						} else if (!GetU1ByteIntegerFromStringInDecimal(param_value_string , ant_isolation_param[j].value))
							RTW_INFO("Fail to GetU1ByteIntegerFromStringInDecimal\n");

						break;
					}
				}
			}
		}
	}

	/* YiWei 20140716 , for BT coex antenna isolation control */
	/* rfe_type = 0 was SPDT , rfe_type = 1 was coupler */
	if (ant_num == 3 && ant_distance >= 50)
		anttype = 3;
	else if (ant_num == 2 && ant_distance >= 50 && rfe_type == 1)
		anttype = 2;
	else if (ant_num == 3 && ant_distance >= 15 && ant_distance < 50)
		anttype = 2;
	else if (ant_num == 2 && ant_distance >= 15 && ant_distance < 50 && rfe_type == 1)
		anttype = 2;
	else if ((ant_num == 2 && ant_distance < 15 && rfe_type == 1) || (ant_num == 3 && ant_distance < 15))
		anttype = 1;
	else if (ant_num == 2 && rfe_type == 0)
		anttype = 0;
	else
		anttype = 0;

	hal_btcoex_SetAntIsolationType(Adapter, anttype);

	RTW_INFO("%s : ant_num = %d\n" , __func__ , ant_num);
	RTW_INFO("%s : ant_distance = %d\n" , __func__ , ant_distance);
	RTW_INFO("%s : rfe_type = %d\n" , __func__ , rfe_type);
	/* RTW_INFO("<===Hal_ParseAntIsolationConfigFile()\n"); */
	return rtStatus;
}


int
hal_btcoex_AntIsolationConfig_ParaFile(
		PADAPTER	Adapter,
		char		*pFileName
)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	int	rlen = 0 , rtStatus = _FAIL;

	_rtw_memset(pHalData->para_file_buf , 0 , MAX_PARA_FILE_BUF_LEN);

	rtw_get_phy_file_path(Adapter, pFileName);
	if (rtw_is_file_readable(rtw_phy_para_file_path) == _TRUE) {
		rlen = rtw_retrieve_from_file(rtw_phy_para_file_path, pHalData->para_file_buf, MAX_PARA_FILE_BUF_LEN);
		if (rlen > 0)
			rtStatus = _SUCCESS;
	}


	if (rtStatus == _SUCCESS) {
		/*RTW_INFO("%s(): read %s ok\n", __func__ , pFileName);*/
		rtStatus = hal_btcoex_ParseAntIsolationConfigFile(Adapter , pHalData->para_file_buf);
	} else
		RTW_INFO("%s(): No File %s, Load from *** Array!\n" , __func__ , pFileName);

	return rtStatus;
}
#endif /* CONFIG_LOAD_PHY_PARA_FROM_FILE */

u16 hal_btcoex_btreg_read(PADAPTER padapter, u8 type, u16 addr, u32 *data)
{
	u16 ret = 0;

	halbtcoutsrc_LeaveLowPower(&GLBtCoexist);

	ret = halbtcoutsrc_GetBtReg_with_status(&GLBtCoexist, type, addr, data);

	halbtcoutsrc_NormalLowPower(&GLBtCoexist);

	return ret;
}

u16 hal_btcoex_btreg_write(PADAPTER padapter, u8 type, u16 addr, u16 val)
{
	u16 ret = 0;

	halbtcoutsrc_LeaveLowPower(&GLBtCoexist);

	ret = halbtcoutsrc_SetBtReg(&GLBtCoexist, type, addr, val);

	halbtcoutsrc_NormalLowPower(&GLBtCoexist);

	return ret;
}

void hal_btcoex_set_rfe_type(u8 type)
{
	EXhalbtcoutsrc_set_rfe_type(type);
}

#ifdef CONFIG_RF4CE_COEXIST
void hal_btcoex_set_rf4ce_link_state(u8 state)
{
	EXhalbtcoutsrc_set_rf4ce_link_state(state);
}

u8 hal_btcoex_get_rf4ce_link_state(void)
{
	return EXhalbtcoutsrc_get_rf4ce_link_state();
}
#endif /* CONFIG_RF4CE_COEXIST */

void hal_btcoex_switchband_notify(u8 under_scan, u8 band_type)
{
	switch (band_type) {
	case BAND_ON_2_4G:
		if (under_scan)
			EXhalbtcoutsrc_switchband_notify(&GLBtCoexist, BTC_SWITCH_TO_24G);
		else
			EXhalbtcoutsrc_switchband_notify(&GLBtCoexist, BTC_SWITCH_TO_24G_NOFORSCAN);
		break;
	case BAND_ON_5G:
		EXhalbtcoutsrc_switchband_notify(&GLBtCoexist, BTC_SWITCH_TO_5G);
		break;
	default:
		RTW_INFO("[BTCOEX] unkown switch band type\n");
		break;
	}
}

void hal_btcoex_WlFwDbgInfoNotify(PADAPTER padapter, u8* tmpBuf, u8 length)
{
	EXhalbtcoutsrc_WlFwDbgInfoNotify(&GLBtCoexist, tmpBuf, length);
}

void hal_btcoex_rx_rate_change_notify(PADAPTER padapter, u8 is_data_frame, u8 rate_id)
{
	EXhalbtcoutsrc_rx_rate_change_notify(&GLBtCoexist, is_data_frame, EXhalbtcoutsrc_rate_id_to_btc_rate_id(rate_id));
}

u16 hal_btcoex_btset_testode(PADAPTER padapter, u8 type)
{
	u16 ret = 0;

	halbtcoutsrc_LeaveLowPower(&GLBtCoexist);

	ret = halbtcoutsrc_setbttestmode(&GLBtCoexist, type);

	halbtcoutsrc_NormalLowPower(&GLBtCoexist);

	return ret;
}

#endif /* CONFIG_BT_COEXIST */
