/*
* Copyright (C) 2011-2014 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify it under the terms of the
* GNU General Public License version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/

/*! \file
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/
#ifdef DFT_TAG
#undef DFT_TAG
#endif
#define DFT_TAG         "[WMT-EXP]"

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "osal_typedef.h"

#include <wmt_exp.h>
#include <wmt_lib.h>

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
wmt_wlan_probe_cb mtk_wcn_wlan_probe = NULL;
wmt_wlan_remove_cb mtk_wcn_wlan_remove = NULL;
wmt_wlan_bus_cnt_get_cb mtk_wcn_wlan_bus_tx_cnt = NULL;
wmt_wlan_bus_cnt_clr_cb mtk_wcn_wlan_bus_tx_cnt_clr = NULL;

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/
OSAL_BIT_OP_VAR gBtWifiGpsState;
OSAL_BIT_OP_VAR gGpsFmState;
UINT32 gWifiProbed = 0;
UINT32 gWmtDbgLvl = WMT_LOG_DBG;
MTK_WCN_BOOL g_pwr_off_flag = MTK_WCN_BOOL_TRUE;
/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

static MTK_WCN_BOOL mtk_wcn_wmt_func_ctrl(ENUM_WMTDRV_TYPE_T type, ENUM_WMT_OPID_T opId);

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

static MTK_WCN_BOOL mtk_wcn_wmt_func_ctrl(ENUM_WMTDRV_TYPE_T type, ENUM_WMT_OPID_T opId)
{
	P_OSAL_OP pOp;
	MTK_WCN_BOOL bRet;
	P_OSAL_SIGNAL pSignal;

	pOp = wmt_lib_get_free_op();
	if (!pOp) {
		WMT_WARN_FUNC("get_free_lxop fail\n");
		return MTK_WCN_BOOL_FALSE;
	}

	pSignal = &pOp->signal;

	pOp->op.opId = opId;
	pOp->op.au4OpData[0] = type;
	if (WMTDRV_TYPE_WIFI == type)
		pSignal->timeoutValue = 4000;
		/*donot block system server/init/netd from longer than 5s, in case of ANR happens*/
	else
		pSignal->timeoutValue = (WMT_OPID_FUNC_ON == pOp->op.opId) ? MAX_FUNC_ON_TIME : MAX_FUNC_OFF_TIME;

	WMT_INFO_FUNC("OPID(%d) type(%d) start\n", pOp->op.opId, pOp->op.au4OpData[0]);

	/*do not check return value, we will do this either way */
	wmt_lib_host_awake_get();
	/*wake up chip first */
	if (DISABLE_PSM_MONITOR()) {
		WMT_ERR_FUNC("wake up failed,OPID(%d) type(%d) abort\n", pOp->op.opId, pOp->op.au4OpData[0]);
		wmt_lib_put_op_to_free_queue(pOp);
		wmt_lib_host_awake_put();
		return MTK_WCN_BOOL_FALSE;
	}

	bRet = wmt_lib_put_act_op(pOp);
	ENABLE_PSM_MONITOR();
	wmt_lib_host_awake_put();

	if (MTK_WCN_BOOL_FALSE == bRet)
		WMT_WARN_FUNC("OPID(%d) type(%d) fail\n", pOp->op.opId, pOp->op.au4OpData[0]);
	else
		WMT_WARN_FUNC("OPID(%d) type(%d) ok\n", pOp->op.opId, pOp->op.au4OpData[0]);

	return bRet;
}

#if WMT_EXP_HID_API_EXPORT
MTK_WCN_BOOL _mtk_wcn_wmt_func_off(ENUM_WMTDRV_TYPE_T type)
#else
MTK_WCN_BOOL mtk_wcn_wmt_func_off(ENUM_WMTDRV_TYPE_T type)
#endif
{
	MTK_WCN_BOOL ret;

	if (type == WMTDRV_TYPE_BT)
		osal_printtimeofday("############ BT OFF ====>");

	ret = mtk_wcn_wmt_func_ctrl(type, WMT_OPID_FUNC_OFF);

	if (type == WMTDRV_TYPE_BT)
		osal_printtimeofday("############ BT OFF <====");

	return ret;
}
#if !WMT_EXP_HID_API_EXPORT
EXPORT_SYMBOL(mtk_wcn_wmt_func_off);
#endif

#if WMT_EXP_HID_API_EXPORT
MTK_WCN_BOOL _mtk_wcn_wmt_func_on(ENUM_WMTDRV_TYPE_T type)
#else
MTK_WCN_BOOL mtk_wcn_wmt_func_on(ENUM_WMTDRV_TYPE_T type)
#endif
{
	MTK_WCN_BOOL ret;

	if (type == WMTDRV_TYPE_BT)
		osal_printtimeofday("############ BT ON ====>");

	ret = mtk_wcn_wmt_func_ctrl(type, WMT_OPID_FUNC_ON);

	if (type == WMTDRV_TYPE_BT)
		osal_printtimeofday(" ############BT ON <====");

	return ret;
}
#if !WMT_EXP_HID_API_EXPORT
EXPORT_SYMBOL(mtk_wcn_wmt_func_on);
#endif

VOID mtk_wcn_wmt_func_ctrl_for_plat(UINT32 on, ENUM_WMTDRV_TYPE_T type)
{
	if (on)
		mtk_wcn_wmt_func_on(type);
	else
		mtk_wcn_wmt_func_off(type);
}

/*
return value:
enable/disable thermal sensor function: true(1)/false(0)
read thermal sensor function:thermal value

*/
#if WMT_EXP_HID_API_EXPORT
INT8 _mtk_wcn_wmt_therm_ctrl(ENUM_WMTTHERM_TYPE_T eType)
#else
INT8 mtk_wcn_wmt_therm_ctrl(ENUM_WMTTHERM_TYPE_T eType)
#endif
{
	P_OSAL_OP pOp;
	P_WMT_OP pOpData;
	MTK_WCN_BOOL bRet;
	P_OSAL_SIGNAL pSignal;

	/*parameter validation check */
	if (WMTTHERM_MAX < eType || WMTTHERM_ENABLE > eType) {
		WMT_ERR_FUNC("invalid thermal control command (%d)\n", eType);
		return MTK_WCN_BOOL_FALSE;
	}

	/*check if chip support thermal control function or not */
	bRet = wmt_lib_is_therm_ctrl_support();
	if (MTK_WCN_BOOL_FALSE == bRet) {
		WMT_ERR_FUNC("thermal ctrl function not supported\n");
		return MTK_WCN_BOOL_FALSE;
	}

	pOp = wmt_lib_get_free_op();
	if (!pOp) {
		WMT_WARN_FUNC("get_free_lxop fail\n");
		return MTK_WCN_BOOL_FALSE;
	}

	pSignal = &pOp->signal;
	pOpData = &pOp->op;
	pOpData->opId = WMT_OPID_THERM_CTRL;
	/*parameter fill */
	pOpData->au4OpData[0] = eType;
	pSignal->timeoutValue = MAX_EACH_WMT_CMD;

	WMT_INFO_FUNC("OPID(%d) type(%d) start\n", pOp->op.opId, pOp->op.au4OpData[0]);

	if (DISABLE_PSM_MONITOR()) {
		WMT_ERR_FUNC("wake up failed,OPID(%d) type(%d) abort!\n", pOp->op.opId, pOp->op.au4OpData[0]);
		wmt_lib_put_op_to_free_queue(pOp);
		return -1;
	}

	bRet = wmt_lib_put_act_op(pOp);
	ENABLE_PSM_MONITOR();

	if (MTK_WCN_BOOL_FALSE == bRet) {
		WMT_WARN_FUNC("OPID(%d) type(%d) fail\n\n", pOpData->opId, pOpData->au4OpData[0]);
		/*0xFF means read error occurs */
		/*will return to function driver */
		pOpData->au4OpData[1] = (eType == WMTTHERM_READ) ? 0xFF : MTK_WCN_BOOL_FALSE;
	} else {
		WMT_INFO_FUNC("OPID(%d) type(%d) return(%d) ok\n\n",
			      pOpData->opId, pOpData->au4OpData[0], pOpData->au4OpData[1]);
	}
	/*return value will be put to lxop->op.au4OpData[1] */
	WMT_DBG_FUNC("therm ctrl type(%d), iRet(0x%08x)\n", eType, pOpData->au4OpData[1]);
	return (INT8) pOpData->au4OpData[1];
}
#if !WMT_EXP_HID_API_EXPORT
EXPORT_SYMBOL(mtk_wcn_wmt_therm_ctrl);
#endif

#if WMT_EXP_HID_API_EXPORT
ENUM_WMTHWVER_TYPE_T _mtk_wcn_wmt_hwver_get(VOID)
#else
ENUM_WMTHWVER_TYPE_T mtk_wcn_wmt_hwver_get(VOID)
#endif
{
	/* TODO: [ChangeFeature][GeorgeKuo] Reconsider usage of this type */
	/* TODO: how do we extend for new chip and newer revision? */
	/* TODO: This way is hard to extend */
	return wmt_lib_get_icinfo(WMTCHIN_MAPPINGHWVER);
}
#if !WMT_EXP_HID_API_EXPORT
EXPORT_SYMBOL(mtk_wcn_wmt_hwver_get);
#endif

#if WMT_EXP_HID_API_EXPORT
MTK_WCN_BOOL _mtk_wcn_wmt_dsns_ctrl(ENUM_WMTDSNS_TYPE_T eType)
#else
MTK_WCN_BOOL mtk_wcn_wmt_dsns_ctrl(ENUM_WMTDSNS_TYPE_T eType)
#endif
{
	P_OSAL_OP pOp;
	P_WMT_OP pOpData;
	MTK_WCN_BOOL bRet;
	P_OSAL_SIGNAL pSignal;

	if (WMTDSNS_MAX <= eType) {
		WMT_ERR_FUNC("invalid desense control command (%d)\n", eType);
		return MTK_WCN_BOOL_FALSE;
	}

	/*check if chip support thermal control function or not */
	bRet = wmt_lib_is_dsns_ctrl_support();
	if (MTK_WCN_BOOL_FALSE == bRet) {
		WMT_ERR_FUNC("thermal ctrl function not supported\n");
		return MTK_WCN_BOOL_FALSE;
	}

	pOp = wmt_lib_get_free_op();
	if (!pOp) {
		WMT_WARN_FUNC("get_free_lxop fail\n");
		return MTK_WCN_BOOL_FALSE;
	}

	pSignal = &pOp->signal;
	pOpData = &pOp->op;
	pOpData->opId = WMT_OPID_DSNS;
	pSignal->timeoutValue = MAX_EACH_WMT_CMD;
	/*parameter fill */
	if ((WMTDSNS_FM_DISABLE <= eType) && (WMTDSNS_FM_GPS_ENABLE >= eType)) {
		pOpData->au4OpData[0] = WMTDRV_TYPE_FM;
		pOpData->au4OpData[1] = eType;
	}

	WMT_INFO_FUNC("OPID(%d) type(%d) start\n", pOp->op.opId, pOp->op.au4OpData[0]);

	if (DISABLE_PSM_MONITOR()) {
		WMT_ERR_FUNC("wake up failed,OPID(%d) type(%d) abort\n", pOp->op.opId, pOp->op.au4OpData[0]);
		wmt_lib_put_op_to_free_queue(pOp);
		return MTK_WCN_BOOL_FALSE;
	}

	bRet = wmt_lib_put_act_op(pOp);
	ENABLE_PSM_MONITOR();

	if (MTK_WCN_BOOL_FALSE == bRet)
		WMT_WARN_FUNC("OPID(%d) type(%d) fail\n\n", pOpData->opId, pOpData->au4OpData[0]);
	else
		WMT_INFO_FUNC("OPID(%d) type(%d) ok\n\n", pOpData->opId, pOpData->au4OpData[0]);

	return bRet;
}
#if !WMT_EXP_HID_API_EXPORT
EXPORT_SYMBOL(mtk_wcn_wmt_dsns_ctrl);
#endif

#if WMT_EXP_HID_API_EXPORT
INT32 _mtk_wcn_wmt_msgcb_reg(ENUM_WMTDRV_TYPE_T eType, PF_WMT_CB pCb)
#else
INT32 mtk_wcn_wmt_msgcb_reg(ENUM_WMTDRV_TYPE_T eType, PF_WMT_CB pCb)
#endif
{
	return (INT32) wmt_lib_msgcb_reg(eType, pCb);
}
#if !WMT_EXP_HID_API_EXPORT
EXPORT_SYMBOL(mtk_wcn_wmt_msgcb_reg);
#endif

#if WMT_EXP_HID_API_EXPORT
INT32 _mtk_wcn_wmt_msgcb_unreg(ENUM_WMTDRV_TYPE_T eType)
#else
INT32 mtk_wcn_wmt_msgcb_unreg(ENUM_WMTDRV_TYPE_T eType)
#endif
{
	return (INT32) wmt_lib_msgcb_unreg(eType);
}
#if !WMT_EXP_HID_API_EXPORT
EXPORT_SYMBOL(mtk_wcn_wmt_msgcb_unreg);
#endif

#if WMT_EXP_HID_API_EXPORT
INT32 _mtk_wcn_stp_wmt_sdio_op_reg(PF_WMT_SDIO_PSOP own_cb)
#else
INT32 mtk_wcn_stp_wmt_sdio_op_reg(PF_WMT_SDIO_PSOP own_cb)
#endif
{
	wmt_lib_ps_set_sdio_psop(own_cb);
	return 0;
}
#if !WMT_EXP_HID_API_EXPORT
EXPORT_SYMBOL(mtk_wcn_stp_wmt_sdio_op_reg);
#endif

#if WMT_EXP_HID_API_EXPORT
INT32 _mtk_wcn_stp_wmt_sdio_host_awake(VOID)
#else
INT32 mtk_wcn_stp_wmt_sdio_host_awake(VOID)
#endif
{
	wmt_lib_ps_irq_cb();
	return 0;
}
#if !WMT_EXP_HID_API_EXPORT
EXPORT_SYMBOL(mtk_wcn_stp_wmt_sdio_host_awake);
#endif

#if WMT_EXP_HID_API_EXPORT
MTK_WCN_BOOL _mtk_wcn_wmt_assert(ENUM_WMTDRV_TYPE_T type, UINT32 reason)
#else
MTK_WCN_BOOL mtk_wcn_wmt_assert(ENUM_WMTDRV_TYPE_T type, UINT32 reason)
#endif
{
	P_OSAL_OP pOp = NULL;
	MTK_WCN_BOOL bRet = MTK_WCN_BOOL_FALSE;
	P_OSAL_SIGNAL pSignal;

	pOp = wmt_lib_get_free_op();
	if (!pOp) {
		WMT_WARN_FUNC("get_free_lxop fail\n");
		return MTK_WCN_BOOL_FALSE;
	}

	wmt_lib_set_host_assert_info(type, reason, 1);

	pSignal = &pOp->signal;

	pOp->op.opId = WMT_OPID_CMD_TEST;

	pSignal->timeoutValue = MAX_EACH_WMT_CMD;
	/*this test command should be run with usb cable connected, so no host awake is needed */
	/* wmt_lib_host_awake_get(); */
	pOp->op.au4OpData[0] = 0;

	/*wake up chip first */
	if (DISABLE_PSM_MONITOR()) {
		WMT_ERR_FUNC("wake up failed,assert flow abort\n");
		wmt_lib_put_op_to_free_queue(pOp);
		return MTK_WCN_BOOL_FALSE;
	}

	bRet = wmt_lib_put_act_op(pOp);
	ENABLE_PSM_MONITOR();

	/* wmt_lib_host_awake_put(); */
	WMT_INFO_FUNC("CMD_TEST, opid (%d), par(%d, %d), ret(%d), result(%s)\n",
		      pOp->op.opId,
		      pOp->op.au4OpData[0],
		      pOp->op.au4OpData[1], bRet, MTK_WCN_BOOL_FALSE == bRet ? "failed" : "succeed");

	return bRet;
}
#if !WMT_EXP_HID_API_EXPORT
EXPORT_SYMBOL(mtk_wcn_wmt_assert);
#endif

INT8 mtk_wcn_wmt_co_clock_flag_get(void)
{
	return wmt_lib_co_clock_get();
}
EXPORT_SYMBOL(mtk_wcn_wmt_co_clock_flag_get);

INT32 mtk_wcn_wmt_system_state_reset(void)
{
	osal_memset(&gBtWifiGpsState, 0, osal_sizeof(gBtWifiGpsState));
	osal_memset(&gGpsFmState, 0, osal_sizeof(gGpsFmState));

	return 0;
}

INT32 mtk_wcn_wmt_wlan_reg(P_MTK_WCN_WMT_WLAN_CB_INFO pWmtWlanCbInfo)
{
	INT32 iRet = -1;

	if (!pWmtWlanCbInfo) {
		WMT_ERR_FUNC("wlan cb info in null!\n");
		return -1;
	}

	WMT_INFO_FUNC("wmt wlan cb register\n");
	mtk_wcn_wlan_probe = pWmtWlanCbInfo->wlan_probe_cb;
	mtk_wcn_wlan_remove = pWmtWlanCbInfo->wlan_remove_cb;
	mtk_wcn_wlan_bus_tx_cnt = pWmtWlanCbInfo->wlan_bus_cnt_get_cb;
	mtk_wcn_wlan_bus_tx_cnt_clr = pWmtWlanCbInfo->wlan_bus_cnt_clr_cb;

	if (gWifiProbed) {
		WMT_INFO_FUNC("wlan has been done power on,call probe directly\n");
		iRet = (*mtk_wcn_wlan_probe) ();
		if (!iRet) {
			WMT_INFO_FUNC("call wlan probe OK when do wlan register to wmt\n");
			gWifiProbed = 0;
		} else {
			WMT_ERR_FUNC("call wlan probe fail(%d) when do wlan register to wmt\n", iRet);
			return -2;
		}
	}
	return 0;
}
EXPORT_SYMBOL(mtk_wcn_wmt_wlan_reg);

INT32 mtk_wcn_wmt_wlan_unreg(void)
{
	WMT_INFO_FUNC("wmt wlan cb unregister\n");
	mtk_wcn_wlan_probe = NULL;
	mtk_wcn_wlan_remove = NULL;
	mtk_wcn_wlan_bus_tx_cnt = NULL;
	mtk_wcn_wlan_bus_tx_cnt_clr = NULL;

	return 0;
}
EXPORT_SYMBOL(mtk_wcn_wmt_wlan_unreg);

MTK_WCN_BOOL mtk_wcn_set_connsys_power_off_flag(MTK_WCN_BOOL value)
{
	g_pwr_off_flag = value;
	if (g_pwr_off_flag)
		WMT_DBG_FUNC("enable connsys power off flag\n");
	else
		WMT_INFO_FUNC("disable connsys power off, maybe need trigger coredump!\n");
	return g_pwr_off_flag;
}
EXPORT_SYMBOL(mtk_wcn_set_connsys_power_off_flag);

#ifdef MTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT
VOID mtk_wcn_wmt_exp_init(void)
{
	MTK_WCN_WMT_EXP_CB_INFO wmtExpCb = {

		.wmt_func_on_cb = _mtk_wcn_wmt_func_on,
		.wmt_func_off_cb = _mtk_wcn_wmt_func_off,
		.wmt_therm_ctrl_cb = _mtk_wcn_wmt_therm_ctrl,
		.wmt_hwver_get_cb = _mtk_wcn_wmt_hwver_get,
		.wmt_dsns_ctrl_cb = _mtk_wcn_wmt_dsns_ctrl,
		.wmt_msgcb_reg_cb = _mtk_wcn_wmt_msgcb_reg,
		.wmt_msgcb_unreg_cb = _mtk_wcn_wmt_msgcb_unreg,
		.wmt_sdio_op_reg_cb = _mtk_wcn_stp_wmt_sdio_op_reg,
		.wmt_sdio_host_awake_cb = _mtk_wcn_stp_wmt_sdio_host_awake,
		.wmt_assert_cb = _mtk_wcn_wmt_assert
	};

	mtk_wcn_wmt_exp_cb_reg(&wmtExpCb);
}

VOID mtk_wcn_wmt_exp_deinit(void)
{
	mtk_wcn_wmt_exp_cb_unreg();
}
#ifdef CONFIG_MTK_COMBO_ANT
/*
	ctrlId: get ram code status opId or ram code download opId
	pBuf: pointer to ANT ram code
	length: total length of ANT ram code
*/
ENUM_WMT_ANT_RAM_STATUS mtk_wcn_wmt_ant_ram_ctrl(ENUM_WMT_ANT_RAM_CTRL ctrlId, PUINT8 pBuf,
						 UINT32 length, ENUM_WMT_ANT_RAM_SEQ seq)
{
	ENUM_WMT_ANT_RAM_STATUS eRet = 0;
	P_OSAL_OP pOp = NULL;
	MTK_WCN_BOOL bRet = MTK_WCN_BOOL_FALSE;
	P_OSAL_SIGNAL pSignal;

	/*1. parameter validation check */
	/*for WMT_ANT_RAM_GET_STATUS, ignore pBuf and length */
	/*for WMT_ANT_RAM_DOWNLOAD,
	   pBuf must not be NULL, kernel space memory pointer
	   length must be large than 0 */

	if ((WMT_ANT_RAM_GET_STATUS > ctrlId) || (WMT_ANT_RAM_CTRL_MAX <= ctrlId)) {
		WMT_ERR_FUNC("error ctrlId:%d detected.\n", ctrlId);
		eRet = WMT_ANT_RAM_PARA_ERR;
		return eRet;
	}

	if ((WMT_ANT_RAM_DOWNLOAD == ctrlId) &&
	    ((NULL == pBuf) ||
	     (0 >= length) ||
	     (1000 < length) || (seq >= WMT_ANT_RAM_SEQ_MAX) || (seq < WMT_ANT_RAM_START_PKT))) {
		eRet = WMT_ANT_RAM_PARA_ERR;
		WMT_ERR_FUNC
		    ("error parameter detected, ctrlId:%d, pBuf:%p,length(0x%x),seq(%d) .\n",
		     ctrlId, pBuf, length, seq);
		return eRet;
	}
	/*get WMT opId */
	pOp = wmt_lib_get_free_op();
	if (!pOp) {
		WMT_DBG_FUNC("get_free_lxop fail\n");
		return MTK_WCN_BOOL_FALSE;
	}

	pSignal = &pOp->signal;
	pSignal->timeoutValue =
	    (WMT_ANT_RAM_DOWNLOAD == ctrlId) ? MAX_FUNC_ON_TIME : MAX_EACH_WMT_CMD;

	pOp->op.opId =
	    (WMT_ANT_RAM_DOWNLOAD == ctrlId) ? WMT_OPID_ANT_RAM_DOWN : WMT_OPID_ANT_RAM_STA_GET;
	pOp->op.au4OpData[0] = (size_t) pBuf;
	pOp->op.au4OpData[1] = length;
	pOp->op.au4OpData[2] = seq;


	/*disable PSM monitor */
	if (DISABLE_PSM_MONITOR()) {
		WMT_ERR_FUNC("wake up failed\n");
		wmt_lib_put_op_to_free_queue(pOp);
		return MTK_WCN_BOOL_FALSE;
	}
	/*wakeup wmtd thread */
	bRet = wmt_lib_put_act_op(pOp);

	/*enable PSM monitor */
	ENABLE_PSM_MONITOR();

	WMT_DBG_FUNC("CMD_TEST, opid (%d), ret(%d),retVal(%zu) result(%s)\n",
		      pOp->op.opId,
		      bRet,
		      pOp->op.au4OpData[2], MTK_WCN_BOOL_FALSE == bRet ? "failed" : "succeed");

	/*check return value and return result */
	if (MTK_WCN_BOOL_FALSE == bRet) {
		eRet = WMT_ANT_RAM_OP_ERR;
	} else {
		eRet = (WMT_ANT_RAM_DOWNLOAD == ctrlId) ?
		    WMT_ANT_RAM_DOWN_OK :
		    ((1 == pOp->op.au4OpData[2]) ? WMT_ANT_RAM_EXIST : WMT_ANT_RAM_NOT_EXIST);
	}

	return eRet;

}
EXPORT_SYMBOL(mtk_wcn_wmt_ant_ram_ctrl);
#endif

#endif
VOID mtk_wcn_wmt_set_wifi_ver(UINT32 Value)
{
	wmt_lib_soc_set_wifiver(Value);
}
EXPORT_SYMBOL(mtk_wcn_wmt_set_wifi_ver);
