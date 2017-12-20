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
#define DFT_TAG         "[WMT-LIB]"

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "osal_typedef.h"

#include "wmt_dev.h"
#include "wmt_lib.h"
#include "wmt_conf.h"
#include "wmt_core.h"
#include "wmt_plat.h"

#include "stp_core.h"
#include "btm_core.h"
#include "psm_core.h"
#include "stp_dbg.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/* A table for translation: CMB_STUB_AIF_X=>WMT_IC_PIN_STATE */
static const WMT_IC_PIN_STATE cmb_aif2pin_stat[] = {
	[CMB_STUB_AIF_0] = WMT_IC_AIF_0,
	[CMB_STUB_AIF_1] = WMT_IC_AIF_1,
	[CMB_STUB_AIF_2] = WMT_IC_AIF_2,
	[CMB_STUB_AIF_3] = WMT_IC_AIF_3,
};

#if CFG_WMT_PS_SUPPORT
static UINT32 gPsIdleTime = STP_PSM_IDLE_TIME_SLEEP;
static UINT32 gPsEnable = 1;
static PF_WMT_SDIO_PSOP sdio_own_ctrl;
#endif

#define WMT_STP_CPUPCR_BUF_SIZE 6144
static UINT8 g_cpupcr_buf[WMT_STP_CPUPCR_BUF_SIZE] = { 0 };

static UINT32 g_quick_sleep_ctrl = 1;
/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/
DEV_WMT gDevWmt;

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

#if CFG_WMT_PS_SUPPORT
static MTK_WCN_BOOL wmt_lib_ps_action(MTKSTP_PSM_ACTION_T action);
static MTK_WCN_BOOL wmt_lib_ps_do_sleep(VOID);
static MTK_WCN_BOOL wmt_lib_ps_do_wakeup(VOID);
static MTK_WCN_BOOL wmt_lib_ps_do_host_awake(VOID);
static INT32 wmt_lib_ps_handler(MTKSTP_PSM_ACTION_T action);
#endif

static MTK_WCN_BOOL wmt_lib_put_op(P_OSAL_OP_Q pOpQ, P_OSAL_OP pLxOp);

static P_OSAL_OP wmt_lib_get_op(P_OSAL_OP_Q pOpQ);

static INT32 wmtd_thread(PVOID pvData);

static INT32 wmt_lib_pin_ctrl(WMT_IC_PIN_ID id, WMT_IC_PIN_STATE stat, UINT32 flag);
static MTK_WCN_BOOL wmt_lib_hw_state_show(VOID);

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
INT32 wmt_lib_idc_lock_aquire(VOID)
{
	return osal_lock_sleepable_lock(&gDevWmt.idc_lock);
}

VOID wmt_lib_idc_lock_release(VOID)
{
	osal_unlock_sleepable_lock(&gDevWmt.idc_lock);
}
INT32 wmt_lib_psm_lock_aquire(void)
{
	return osal_lock_sleepable_lock(&gDevWmt.psm_lock);
}

void wmt_lib_psm_lock_release(void)
{
	osal_unlock_sleepable_lock(&gDevWmt.psm_lock);
}

INT32 DISABLE_PSM_MONITOR(void)
{
	INT32 ret = 0;

	/* osal_lock_sleepable_lock(&gDevWmt.psm_lock); */
	ret = wmt_lib_psm_lock_aquire();
	if (ret) {
		WMT_ERR_FUNC("--->lock psm_lock failed, ret=%d\n", ret);
		return ret;
	}
#if CFG_WMT_PS_SUPPORT
	ret = wmt_lib_ps_disable();
	if (ret) {
		WMT_ERR_FUNC("wmt_lib_ps_disable fail, ret=%d\n", ret);
		wmt_lib_psm_lock_release();
	}
#endif

	return ret;
}

void ENABLE_PSM_MONITOR(void)
{
#if CFG_WMT_PS_SUPPORT
	wmt_lib_ps_enable();
#endif
	/* osal_unlock_sleepable_lock(&gDevWmt.psm_lock); */
	wmt_lib_psm_lock_release();
}

INT32 wmt_lib_init(VOID)
{
	INT32 iRet;
	UINT32 i;
	P_DEV_WMT pDevWmt;
	P_OSAL_THREAD pThraed;

	/* create->init->start */
	/* 1. create: static allocation with zero initialization */
	pDevWmt = &gDevWmt;
	osal_memset(&gDevWmt, 0, sizeof(gDevWmt));

	iRet = wmt_conf_read_file();
	if (iRet) {
		WMT_ERR_FUNC("read wmt config file fail(%d)\n", iRet);
		return -1;
	}

	pThraed = &gDevWmt.thread;

	/* Create mtk_wmtd thread */
	osal_strncpy(pThraed->threadName, "mtk_wmtd", sizeof(pThraed->threadName));
	pThraed->pThreadData = (VOID *) pDevWmt;
	pThraed->pThreadFunc = (VOID *) wmtd_thread;
	iRet = osal_thread_create(pThraed);
	if (iRet) {
		WMT_ERR_FUNC("osal_thread_create(0x%p) fail(%d)\n", pThraed, iRet);
		return -2;
	}

	/* 2. initialize */
	/* Initialize wmt_core */

	iRet = wmt_core_init();
	if (iRet) {
		WMT_ERR_FUNC("wmt_core_init() fail(%d)\n", iRet);
		return -1;
	}

	/* Initialize WMTd Thread Information: Thread */
	osal_event_init(&pDevWmt->rWmtdWq);
	osal_sleepable_lock_init(&pDevWmt->psm_lock);
	osal_sleepable_lock_init(&pDevWmt->idc_lock);
	osal_sleepable_lock_init(&pDevWmt->rActiveOpQ.sLock);
	osal_sleepable_lock_init(&pDevWmt->rFreeOpQ.sLock);
	pDevWmt->state.data = 0;

	/* Initialize op queue */
	RB_INIT(&pDevWmt->rFreeOpQ, WMT_OP_BUF_SIZE);
	RB_INIT(&pDevWmt->rActiveOpQ, WMT_OP_BUF_SIZE);
	/* Put all to free Q */
	for (i = 0; i < WMT_OP_BUF_SIZE; i++) {
		osal_signal_init(&(pDevWmt->arQue[i].signal));
		wmt_lib_put_op(&pDevWmt->rFreeOpQ, &(pDevWmt->arQue[i]));
	}

	/* initialize stp resources */
	osal_event_init(&pDevWmt->rWmtRxWq);

	/*function driver callback */
	for (i = 0; i < WMTDRV_TYPE_WIFI; i++)
		pDevWmt->rFdrvCb.fDrvRst[i] = NULL;

	pDevWmt->hw_ver = WMTHWVER_MAX;
	WMT_INFO_FUNC("***********Init, hw->ver = %x\n", pDevWmt->hw_ver);

	/* TODO:[FixMe][GeorgeKuo]: wmt_lib_conf_init */
	/* initialize default configurations */
	/* i4Result = wmt_lib_conf_init(VOID); */
	/* WMT_WARN_FUNC("wmt_drv_conf_init(%d)\n", i4Result); */

	osal_signal_init(&pDevWmt->cmdResp);
	osal_event_init(&pDevWmt->cmdReq);

	/* initialize platform resources */
	if (0 != gDevWmt.rWmtGenConf.cfgExist)
		iRet = wmt_plat_init(gDevWmt.rWmtGenConf.co_clock_flag & 0x0f);
	else
		iRet = wmt_plat_init(0);

	if (iRet) {
		WMT_ERR_FUNC("wmt_plat_init() fail(%d)\n", iRet);
		return -3;
	}
#if CFG_WMT_PS_SUPPORT
	iRet = wmt_lib_ps_init();
	if (iRet) {
		WMT_ERR_FUNC("wmt_lib_ps_init() fail(%d)\n", iRet);
		return -4;
	}
#endif

	/* 3. start: start running mtk_wmtd */
	iRet = osal_thread_run(pThraed);
	if (iRet) {
		WMT_ERR_FUNC("osal_thread_run(0x%p) fail(%d)\n", pThraed, iRet);
		return -5;
	}

	/*4. register irq callback to WMT-PLAT */
	wmt_plat_irq_cb_reg(wmt_lib_ps_irq_cb);

	/*5. register audio if control callback to WMT-PLAT */
	wmt_plat_aif_cb_reg(wmt_lib_set_aif);

	/*6. register function control callback to WMT-PLAT */
	wmt_plat_func_ctrl_cb_reg(mtk_wcn_wmt_func_ctrl_for_plat);

	wmt_plat_deep_idle_ctrl_cb_reg(mtk_wcn_consys_stp_btif_dpidle_ctrl);
	/*7 reset gps/bt state */

	mtk_wcn_wmt_system_state_reset();

#ifdef MTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT
	mtk_wcn_wmt_exp_init();
#endif

#if CFG_WMT_LTE_COEX_HANDLING
	wmt_idc_init();
#endif
	WMT_DBG_FUNC("init success\n");
	return 0;
}

INT32 wmt_lib_deinit(VOID)
{
	INT32 iRet;
	P_DEV_WMT pDevWmt;
	P_OSAL_THREAD pThraed;
	INT32 i;
	INT32 iResult;

	pDevWmt = &gDevWmt;
	pThraed = &gDevWmt.thread;
	iResult = 0;

	/* stop->deinit->destroy */

	/* 1. stop: stop running mtk_wmtd */
	iRet = osal_thread_stop(pThraed);
	if (iRet) {
		WMT_ERR_FUNC("osal_thread_stop(0x%p) fail(%d)\n", pThraed, iRet);
		iResult += 1;
	}

	/* 2. deinit: */

#if CFG_WMT_PS_SUPPORT
	iRet = wmt_lib_ps_deinit();
	if (iRet) {
		WMT_ERR_FUNC("wmt_lib_ps_deinit fail(%d)\n", iRet);
		iResult += 2;
	}
#endif

	iRet = wmt_plat_deinit();
	if (iRet) {
		WMT_ERR_FUNC("wmt_plat_deinit fail(%d)\n", iRet);
		iResult += 4;
	}

	osal_event_deinit(&pDevWmt->cmdReq);
	osal_signal_deinit(&pDevWmt->cmdResp);

	/* de-initialize stp resources */
	osal_event_deinit(&pDevWmt->rWmtRxWq);

	for (i = 0; i < WMT_OP_BUF_SIZE; i++)
		osal_signal_deinit(&(pDevWmt->arQue[i].signal));


	osal_sleepable_lock_deinit(&pDevWmt->rFreeOpQ.sLock);
	osal_sleepable_lock_deinit(&pDevWmt->rActiveOpQ.sLock);
	osal_sleepable_lock_deinit(&pDevWmt->idc_lock);
	osal_sleepable_lock_deinit(&pDevWmt->psm_lock);
	osal_event_deinit(&pDevWmt->rWmtdWq);

	iRet = wmt_core_deinit();
	if (iRet) {
		WMT_ERR_FUNC("wmt_core_deinit fail(%d)\n", iRet);
		iResult += 8;
	}

	/* 3. destroy */
	iRet = osal_thread_destroy(pThraed);
	if (iRet) {
		WMT_ERR_FUNC("osal_thread_stop(0x%p) fail(%d)\n", pThraed, iRet);
		iResult += 16;
	}
	osal_memset(&gDevWmt, 0, sizeof(gDevWmt));

#ifdef MTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT
	mtk_wcn_wmt_exp_deinit();
#endif

#if CFG_WMT_LTE_COEX_HANDLING
	wmt_idc_deinit();
#endif

	return iResult;
}

VOID wmt_lib_flush_rx(VOID)
{
	mtk_wcn_stp_flush_rx_queue(WMT_TASK_INDX);
}

INT32 wmt_lib_trigger_cmd_signal(INT32 result)
{
	P_OSAL_SIGNAL pSignal = &gDevWmt.cmdResp;

	gDevWmt.cmdResult = result;
	osal_raise_signal(pSignal);
	WMT_DBG_FUNC("wakeup cmdResp\n");
	return 0;
}

P_OSAL_EVENT wmt_lib_get_cmd_event(VOID)
{
	return &gDevWmt.cmdReq;
}

INT32 wmt_lib_set_patch_name(PUINT8 cPatchName)
{
	osal_strncpy(gDevWmt.cPatchName, cPatchName, NAME_MAX);
	return 0;
}

INT32 wmt_lib_set_hif(unsigned long hifconf)
{
	UINT32 val;
	P_WMT_HIF_CONF pHif = &gDevWmt.rWmtHifConf;

	val = hifconf & 0xF;
	if (STP_UART_FULL == val) {
		pHif->hifType = WMT_HIF_UART;
		val = (hifconf >> 8);
		pHif->au4HifConf[0] = val;
		pHif->au4HifConf[1] = val;
		mtk_wcn_stp_set_if_tx_type(STP_UART_IF_TX);
	} else if (STP_SDIO == val) {
		pHif->hifType = WMT_HIF_SDIO;
		mtk_wcn_stp_set_if_tx_type(STP_SDIO_IF_TX);
	} else if (STP_BTIF_FULL == val) {
		pHif->hifType = WMT_HIF_BTIF;
		mtk_wcn_stp_set_if_tx_type(STP_BTIF_IF_TX);
	} else {
		WMT_WARN_FUNC("invalid stp mode: %u\n", val);
		mtk_wcn_stp_set_if_tx_type(STP_MAX_IF_TX);
		return -1;
	}

	val = (hifconf & 0xF0) >> 4;
	if (WMT_FM_COMM == val) {
		pHif->au4StrapConf[0] = WMT_FM_COMM;
	} else if (WMT_FM_I2C == val) {
		pHif->au4StrapConf[0] = WMT_FM_I2C;
	} else {
		WMT_WARN_FUNC("invalid fm mode: %u\n", val);
		return -2;
	}

	WMT_WARN_FUNC("new hifType: %d, fm:%d\n", pHif->hifType, pHif->au4StrapConf[0]);
	return 0;
}

P_WMT_HIF_CONF wmt_lib_get_hif(VOID)
{
	return &gDevWmt.rWmtHifConf;
}

PUINT8 wmt_lib_get_cmd(VOID)
{
	if (osal_test_and_clear_bit(WMT_STAT_CMD, &gDevWmt.state))
		return gDevWmt.cCmd;

	return NULL;
}

MTK_WCN_BOOL wmt_lib_get_cmd_status(VOID)
{
	return osal_test_bit(WMT_STAT_CMD, &gDevWmt.state) ? MTK_WCN_BOOL_TRUE : MTK_WCN_BOOL_FALSE;
}

#if CFG_WMT_PS_SUPPORT
INT32 wmt_lib_ps_set_idle_time(UINT32 psIdleTime)
{
	gPsIdleTime = psIdleTime;
	return gPsIdleTime;
}

INT32 wmt_lib_ps_ctrl(UINT32 state)
{
	if (0 == state) {
		wmt_lib_ps_disable();
		gPsEnable = 0;
	} else {
		gPsEnable = 1;
		wmt_lib_ps_enable();
	}
	return 0;
}

INT32 wmt_lib_ps_enable(VOID)
{
	if (gPsEnable)
		mtk_wcn_stp_psm_enable(gPsIdleTime);

	return 0;
}

INT32 wmt_lib_ps_disable(VOID)
{
	if (gPsEnable)
		return mtk_wcn_stp_psm_disable();

	return 0;
}

INT32 wmt_lib_ps_init(VOID)
{
	/* mtk_wcn_stp_psm_register_wmt_cb(wmt_lib_ps_stp_cb); */
	return 0;
}

INT32 wmt_lib_ps_deinit(VOID)
{
	/* mtk_wcn_stp_psm_unregister_wmt_cb(); */
	return 0;
}

static MTK_WCN_BOOL wmt_lib_ps_action(MTKSTP_PSM_ACTION_T action)
{
	P_OSAL_OP lxop;
	MTK_WCN_BOOL bRet;
	UINT32 u4Wait;
	P_OSAL_SIGNAL pSignal;

	lxop = wmt_lib_get_free_op();
	if (!lxop) {
		WMT_WARN_FUNC("get_free_lxop fail\n");
		return MTK_WCN_BOOL_FALSE;
	}
	pSignal = &lxop->signal;
	pSignal->timeoutValue = 0;
	lxop->op.opId = WMT_OPID_PWR_SV;
	lxop->op.au4OpData[0] = action;
	lxop->op.au4OpData[1] = (SIZE_T) mtk_wcn_stp_psm_notify_stp;
	u4Wait = 0;
	bRet = wmt_lib_put_act_op(lxop);
	return bRet;
}

#if CFG_WMT_LTE_COEX_HANDLING
MTK_WCN_BOOL wmt_lib_handle_idc_msg(ipc_ilm_t *idc_infor)
{
	P_OSAL_OP lxop;
	MTK_WCN_BOOL bRet = MTK_WCN_BOOL_FALSE;
	P_OSAL_SIGNAL pSignal;
	INT32 ret = 0;
	UINT16 msg_len = 0;
	static UINT8 msg_local_buffer[1300];
#if	CFG_WMT_LTE_ENABLE_MSGID_MAPPING
	MTK_WCN_BOOL unknown_msgid = MTK_WCN_BOOL_FALSE;
#endif
	WMT_DBG_FUNC("idc_infor from conn_md is 0x%p\n", idc_infor);
	ret = wmt_lib_idc_lock_aquire();
	if (ret) {
		WMT_ERR_FUNC("--->lock idc_lock failed, ret=%d\n", ret);
		return MTK_WCN_BOOL_FALSE;
	}
	msg_len = idc_infor->local_para_ptr->msg_len - osal_sizeof(local_para_struct);
	osal_memcpy(&msg_local_buffer[0], &msg_len, osal_sizeof(msg_len));
	osal_memcpy(&msg_local_buffer[osal_sizeof(msg_len)],
			&(idc_infor->local_para_ptr->data[0]), msg_len - 1);
	wmt_lib_idc_lock_release();
	lxop = wmt_lib_get_free_op();
	if (!lxop) {
		WMT_WARN_FUNC("get_free_lxop fail\n");
		return MTK_WCN_BOOL_FALSE;
	}
	pSignal = &lxop->signal;
	pSignal->timeoutValue = MAX_EACH_WMT_CMD;
	lxop->op.opId = WMT_OPID_IDC_MSG_HANDLING;
	lxop->op.au4OpData[0] = (size_t) msg_local_buffer;
	/*msg opcode fill rule is still not clrear,need scott comment */
	/***********************************************************/
	WMT_DBG_FUNC("ilm msg id is (0x%08x)\n", idc_infor->msg_id);

#if	CFG_WMT_LTE_ENABLE_MSGID_MAPPING
	switch (idc_infor->msg_id) {
	case IPC_MSG_ID_EL1_LTE_DEFAULT_PARAM_IND:
		lxop->op.au4OpData[1] = WMT_IDC_TX_OPCODE_LTE_PARA;
		break;
	case IPC_MSG_ID_EL1_LTE_OPER_FREQ_PARAM_IND:
		lxop->op.au4OpData[1] = WMT_IDC_TX_OPCODE_LTE_FREQ;
		break;
	case IPC_MSG_ID_EL1_WIFI_MAX_PWR_IND:
		lxop->op.au4OpData[1] = WMT_IDC_TX_OPCODE_WIFI_MAX_POWER;
		break;
	case IPC_MSG_ID_EL1_LTE_TX_IND:
		lxop->op.au4OpData[1] = WMT_IDC_TX_OPCODE_LTE_INDICATION;
		break;
	case IPC_MSG_ID_EL1_LTE_CONNECTION_STATUS_IND:
		lxop->op.au4OpData[1] = WMT_IDC_TX_OPCODE_LTE_CONNECTION_STAS;
		break;
	default:
		unknown_msgid = MTK_WCN_BOOL_TRUE;
		break;
	}

	if (MTK_WCN_BOOL_FALSE == unknown_msgid) {
		/*wake up chip first */
		if (DISABLE_PSM_MONITOR()) {
			WMT_ERR_FUNC("wake up failed\n");
			wmt_lib_put_op_to_free_queue(lxop);
			return MTK_WCN_BOOL_FALSE;
		}

		bRet = wmt_lib_put_act_op(lxop);
		ENABLE_PSM_MONITOR();
		if (MTK_WCN_BOOL_FALSE == bRet)
			WMT_WARN_FUNC("WMT_OPID_IDC_MSG_HANDLING fail(%d)\n", bRet);
		else
			WMT_DBG_FUNC("OPID(%d) type(%d) ok\n", lxop->op.opId, lxop->op.au4OpData[1]);
	} else {
		bRet = MTK_WCN_BOOL_FALSE;
		wmt_lib_put_op_to_free_queue(lxop);
		WMT_ERR_FUNC("unknown msgid from LTE(%d)\n", idc_infor->msg_id);
	}
#else
	if ((idc_infor->msg_id >= IPC_EL1_MSG_ID_BEGIN)
	    && (idc_infor->msg_id <= IPC_EL1_MSG_ID_BEGIN + IPC_EL1_MSG_ID_RANGE)) {
		lxop->op.au4OpData[1] = idc_infor->msg_id - IPC_EL1_MSG_ID_BEGIN + LTE_MSG_ID_OFFSET - 1;

		WMT_DBG_FUNC("LTE->CONN:(0x%x->0x%zx)\n", idc_infor->msg_id, lxop->op.au4OpData[1]);
		/*wake up chip first */
		if (DISABLE_PSM_MONITOR()) {
			WMT_ERR_FUNC("wake up failed\n");
			wmt_lib_put_op_to_free_queue(lxop);
			return MTK_WCN_BOOL_FALSE;
		}

		bRet = wmt_lib_put_act_op(lxop);
		ENABLE_PSM_MONITOR();
		if (MTK_WCN_BOOL_FALSE == bRet) {
			WMT_WARN_FUNC("WMT_OPID_IDC_MSG_HANDLING fail(%d)\n", bRet);
		} else {
			WMT_DBG_FUNC("wmt_lib_handle_idc_msg OPID(%d) type(%d) ok\n",
				     lxop->op.opId, lxop->op.au4OpData[1]);
		}
	} else {
		wmt_lib_put_op_to_free_queue(lxop);
		WMT_ERR_FUNC("msgid(%d) out of range,wmt drop it!\n", idc_infor->msg_id);
	}
#endif

	return bRet;
}
#endif

static MTK_WCN_BOOL wmt_lib_ps_do_sleep(VOID)
{
	return wmt_lib_ps_action(SLEEP);
}

static MTK_WCN_BOOL wmt_lib_ps_do_wakeup(VOID)
{
	return wmt_lib_ps_action(WAKEUP);
}

static MTK_WCN_BOOL wmt_lib_ps_do_host_awake(VOID)
{
#if 1
	return wmt_lib_ps_action(WAKEUP);
#else
	return wmt_lib_ps_action(HOST_AWAKE);
#endif
}

/* extern int g_block_tx; */
static INT32 wmt_lib_ps_handler(MTKSTP_PSM_ACTION_T action)
{
	INT32 ret;

	ret = 0;		/* TODO:[FixMe][George] initial value or compile warning? */
	/* if(g_block_tx && (action == SLEEP)) */
	if ((0 != mtk_wcn_stp_coredump_start_get()) && (action == SLEEP)) {
		mtk_wcn_stp_psm_notify_stp(SLEEP);
		return ret;
	}

	/*MT662x Not Ready */
	if (!mtk_wcn_stp_is_ready()) {
		WMT_DBG_FUNC("MT662x Not Ready, Dont Send Sleep/Wakeup Command\n");
		mtk_wcn_stp_psm_notify_stp(ROLL_BACK);
		return 0;
	}

	if (SLEEP == action) {
		WMT_DBG_FUNC("send op-----------> sleep job\n");

		if (!mtk_wcn_stp_is_sdio_mode()) {
			ret = wmt_lib_ps_do_sleep();
			WMT_DBG_FUNC("enable host eirq\n");
			wmt_plat_eirq_ctrl(PIN_BGF_EINT, PIN_STA_EINT_EN);
#if CFG_WMT_DUMP_INT_STATUS
			if (MTK_WCN_BOOL_TRUE == wmt_plat_dump_BGF_irq_status())
				wmt_plat_BGF_irq_dump_status();
#endif
		} else {
			/* ret = mtk_wcn_stp_sdio_do_own_set(); */
			if (sdio_own_ctrl) {
				ret = (*sdio_own_ctrl) (OWN_SET);
			} else {
				WMT_ERR_FUNC("sdio_own_ctrl is not registered\n");
				ret = -1;
			}

			if (!ret) {
				mtk_wcn_stp_psm_notify_stp(SLEEP);
			} else if (ret == -2) {
				mtk_wcn_stp_psm_notify_stp(ROLL_BACK);
				WMT_WARN_FUNC("===[SDIO-PS] rollback due to tx busy===%%\n");
			} else {
				mtk_wcn_stp_psm_notify_stp(SLEEP);
				WMT_ERR_FUNC("===[SDIO-PS] set own fails!===%%\n");
			}
		}

		WMT_DBG_FUNC("send op<---------- sleep job\n");
	} else if (WAKEUP == action) {
		WMT_DBG_FUNC("send op --------> wake job\n");

		if (!mtk_wcn_stp_is_sdio_mode()) {
			WMT_DBG_FUNC("disable host eirq\n");
#if CFG_WMT_DUMP_INT_STATUS
			if (MTK_WCN_BOOL_TRUE == wmt_plat_dump_BGF_irq_status())
				wmt_plat_BGF_irq_dump_status();
#endif
			wmt_plat_eirq_ctrl(PIN_BGF_EINT, PIN_STA_EINT_DIS);
			ret = wmt_lib_ps_do_wakeup();
		} else {
			/* ret = mtk_wcn_stp_sdio_do_own_clr(); */

			if (sdio_own_ctrl) {
				ret = (*sdio_own_ctrl) (OWN_CLR);
			} else {
				WMT_ERR_FUNC("sdio_own_ctrl is not registered\n");
				ret = -1;
			}

			if (!ret) {
				mtk_wcn_stp_psm_notify_stp(WAKEUP);
			} else {
				mtk_wcn_stp_psm_notify_stp(WAKEUP);
				WMT_ERR_FUNC("===[SDIO-PS] set own back fails!===%%\n");
			}
		}

		WMT_DBG_FUNC("send op<---------- wake job\n");
	} else if (HOST_AWAKE == action) {
		WMT_DBG_FUNC("send op-----------> host awake job\n");

		if (!mtk_wcn_stp_is_sdio_mode()) {
			WMT_DBG_FUNC("disable host eirq\n");
			/* IRQ already disabled */
			/* wmt_plat_eirq_ctrl(PIN_BGF_EINT, PIN_STA_EINT_DIS); */
#if 0
			if (MTK_WCN_BOOL_TRUE == wmt_plat_dump_BGF_irq_status())
				wmt_plat_BGF_irq_dump_status();
#endif
			ret = wmt_lib_ps_do_host_awake();
		} else {
			WMT_DBG_FUNC("[SDIO-PS] SDIO host awake! ####\n");

			/* ret = mtk_wcn_stp_sdio_do_own_clr(); */

			if (sdio_own_ctrl) {
				ret = (*sdio_own_ctrl) (OWN_CLR);
			} else {
				WMT_ERR_FUNC("sdio_own_ctrl is not registered\n");
				ret = -1;
			}

			/* Here we set ret to 0 directly */
			ret = 0;
			if (!ret) {
				mtk_wcn_stp_psm_notify_stp(HOST_AWAKE);
			} else {
				mtk_wcn_stp_psm_notify_stp(HOST_AWAKE);
				WMT_ERR_FUNC("===[SDIO-PS]set own back fails!===%%\n");
			}
		}

		WMT_DBG_FUNC("send op<----------- host awake job\n");
	} else if (EIRQ == action) {
		WMT_DBG_FUNC("send op -------------> eirq job\n");

		if (!mtk_wcn_stp_is_sdio_mode()) {
			WMT_DBG_FUNC("disable host eirq\n");
			/* Disable interrupt */
			/* wmt_plat_eirq_ctrl(PIN_BGF_EINT, PIN_STA_EINT_DIS); */
			ret = mtk_wcn_stp_psm_notify_stp(EIRQ);
		} else {
			WMT_ERR_FUNC("[SDIO-PS]sdio own-back eirq!######\n");
			ret = mtk_wcn_stp_psm_notify_stp(EIRQ);
		}

		WMT_DBG_FUNC("send op<----------- eirq job\n");
	}

	return ret;
}
#endif /* end of CFG_WMT_PS_SUPPORT */

INT32 wmt_lib_ps_stp_cb(MTKSTP_PSM_ACTION_T action)
{
#if CFG_WMT_PS_SUPPORT
	return wmt_lib_ps_handler(action);
#else
	WMT_WARN_FUNC("CFG_WMT_PS_SUPPORT is not set\n");
	return 0;
#endif
}

MTK_WCN_BOOL wmt_lib_is_quick_ps_support(VOID)
{
	if ((g_quick_sleep_ctrl) && (wmt_dev_get_early_suspend_state() == MTK_WCN_BOOL_TRUE))
		return wmt_core_is_quick_ps_support();
	else
		return MTK_WCN_BOOL_FALSE;
}

VOID wmt_lib_ps_irq_cb(VOID)
{
#if CFG_WMT_PS_SUPPORT
	wmt_lib_ps_handler(EIRQ);
#else
	WMT_DBG_FUNC("CFG_WMT_PS_SUPPORT is not set\n");
	return;
#endif
}

VOID wmt_lib_ps_set_sdio_psop(PF_WMT_SDIO_PSOP own_cb)
{
#if CFG_WMT_PS_SUPPORT
	sdio_own_ctrl = own_cb;
#endif
}

UINT32 wmt_lib_wait_event_checker(P_OSAL_THREAD pThread)
{
	P_DEV_WMT pDevWmt;

	if (pThread) {
		pDevWmt = (P_DEV_WMT) (pThread->pThreadData);
		return !RB_EMPTY(&pDevWmt->rActiveOpQ);
	}
	WMT_ERR_FUNC("pThread(NULL)\n");
	return 0;
}

static INT32 wmtd_thread(void *pvData)
{
	P_DEV_WMT pWmtDev = (P_DEV_WMT) pvData;
	P_OSAL_EVENT pEvent = NULL;
	P_OSAL_OP pOp;
	INT32 iResult;

	if (NULL == pWmtDev) {
		WMT_ERR_FUNC("pWmtDev(NULL)\n");
		return -1;
	}
	WMT_INFO_FUNC("wmtd thread starts\n");

	pEvent = &(pWmtDev->rWmtdWq);

	for (;;) {
		pOp = NULL;
		pEvent->timeoutValue = 0;
/*        osal_thread_wait_for_event(&pWmtDev->thread, pEvent);*/
		osal_thread_wait_for_event(&pWmtDev->thread, pEvent, wmt_lib_wait_event_checker);

		if (osal_thread_should_stop(&pWmtDev->thread)) {
			WMT_INFO_FUNC("wmtd thread should stop now...\n");
			/* TODO: clean up active opQ */
			break;
		}

		/* get Op from activeQ */
		pOp = wmt_lib_get_op(&pWmtDev->rActiveOpQ);
		if (!pOp) {
			WMT_WARN_FUNC("get_lxop activeQ fail\n");
			continue;
		}
#if 0				/* wmt_core_opid_handler will do sanity check on opId, so no usage here */
		id = lxop_get_opid(pLxOp);
		if (id >= WMT_OPID_MAX) {
			WMT_WARN_FUNC("abnormal opid id: 0x%x\n", id);
			iResult = -1;
			goto handlerDone;
		}
#endif

		if (osal_test_bit(WMT_STAT_RST_ON, &pWmtDev->state)) {
			/* when whole chip reset, only HW RST and SW RST cmd can execute */
			if ((pOp->op.opId == WMT_OPID_HW_RST) || (pOp->op.opId == WMT_OPID_SW_RST)
			    || (pOp->op.opId == WMT_OPID_GPIO_STATE)) {
				iResult = wmt_core_opid(&pOp->op);
			} else {
				iResult = -2;
				WMT_WARN_FUNC("Whole chip resetting, opid (%d) failed, iRet(%d)\n", pOp->op.opId,
					      iResult);
			}
		} else {
			wmt_lib_set_current_op(pWmtDev, pOp);
			iResult = wmt_core_opid(&pOp->op);
			wmt_lib_set_current_op(pWmtDev, NULL);
		}

		if (iResult)
			WMT_WARN_FUNC("opid (%d) failed, iRet(%d)\n", pOp->op.opId, iResult);

		if (osal_op_is_wait_for_signal(pOp)) {
			osal_op_raise_signal(pOp, iResult);
		} else {
			/* put Op back to freeQ */
			wmt_lib_put_op(&pWmtDev->rFreeOpQ, pOp);
		}

		if (WMT_OPID_EXIT == pOp->op.opId) {
			WMT_INFO_FUNC("wmtd thread received exit signal\n");
			break;
		}
	}

	WMT_INFO_FUNC("wmtd thread exits succeed\n");

	return 0;
};

static MTK_WCN_BOOL wmt_lib_put_op(P_OSAL_OP_Q pOpQ, P_OSAL_OP pOp)
{
	INT32 iRet;

	if (!pOpQ || !pOp) {
		WMT_WARN_FUNC("invalid input param: pOpQ(0x%p), pLxOp(0x%p)\n", pOpQ, pOp);
		osal_assert(pOpQ);
		osal_assert(pOp);
		return MTK_WCN_BOOL_FALSE;
	}

	iRet = osal_lock_sleepable_lock(&pOpQ->sLock);
	if (iRet) {
		WMT_WARN_FUNC("osal_lock_sleepable_lock iRet(%d)\n", iRet);
		return MTK_WCN_BOOL_FALSE;
	}
#if 0
	if (pOpQ == &gDevWmt.rFreeOpQ)
		WMT_INFO_FUNC("current wmt free queue count is(%d),opid(%d)\n", RB_COUNT(pOpQ), pOp->op.opId);
#endif
	/* acquire lock success */
	if (!RB_FULL(pOpQ))
		RB_PUT(pOpQ, pOp);
	else
		iRet = -1;

	osal_unlock_sleepable_lock(&pOpQ->sLock);

	if (iRet) {
		WMT_WARN_FUNC("RB_FULL(0x%p)\n", pOpQ);
		return MTK_WCN_BOOL_FALSE;
	} else {
		return MTK_WCN_BOOL_TRUE;
	}
}

static P_OSAL_OP wmt_lib_get_op(P_OSAL_OP_Q pOpQ)
{
	P_OSAL_OP pOp;
	INT32 iRet;

	if (NULL == pOpQ) {
		WMT_ERR_FUNC("pOpQ = NULL\n");
		osal_assert(pOpQ);
		return NULL;
	}

	iRet = osal_lock_sleepable_lock(&pOpQ->sLock);
	if (iRet) {
		WMT_ERR_FUNC("osal_lock_sleepable_lock iRet(%d)\n", iRet);
		return NULL;
	}

	/* acquire lock success */
	RB_GET(pOpQ, pOp);
	osal_unlock_sleepable_lock(&pOpQ->sLock);

	if (NULL == pOp) {
		WMT_WARN_FUNC("RB_GET return NULL\n");
		osal_assert(pOp);
	}

	return pOp;
}

INT32 wmt_lib_put_op_to_free_queue(P_OSAL_OP pOp)
{
	P_DEV_WMT pWmtDev = &gDevWmt;

	if (MTK_WCN_BOOL_FALSE == wmt_lib_put_op(&pWmtDev->rFreeOpQ, pOp))
		return -1;
	else
		return 0;
}

P_OSAL_OP wmt_lib_get_free_op(VOID)
{
	P_OSAL_OP pOp = NULL;
	P_DEV_WMT pDevWmt = &gDevWmt;

	osal_assert(pDevWmt);

	pOp = wmt_lib_get_op(&pDevWmt->rFreeOpQ);
	if (pOp)
		osal_memset(&pOp->op, 0, osal_sizeof(pOp->op));
	return pOp;
}

MTK_WCN_BOOL wmt_lib_put_act_op(P_OSAL_OP pOp)
{
	P_DEV_WMT pWmtDev = &gDevWmt;
	MTK_WCN_BOOL bRet = MTK_WCN_BOOL_FALSE;
	MTK_WCN_BOOL bCleanup = MTK_WCN_BOOL_FALSE;
	P_OSAL_SIGNAL pSignal = NULL;
	long waitRet = -1;
	P_OSAL_THREAD pThread;

	osal_assert(pWmtDev);
	osal_assert(pOp);

	do {
		if (!pWmtDev || !pOp) {
			WMT_ERR_FUNC("pWmtDev(0x%p), pOp(0x%p)\n", pWmtDev, pOp);
			break;
		}
		if ((0 != mtk_wcn_stp_coredump_start_get()) &&
		    (WMT_OPID_HW_RST != pOp->op.opId) &&
		    (WMT_OPID_SW_RST != pOp->op.opId) && (WMT_OPID_GPIO_STATE != pOp->op.opId)) {
			bCleanup = MTK_WCN_BOOL_TRUE;
			WMT_WARN_FUNC("block tx flag is set\n");
			break;
		}
		pSignal = &pOp->signal;
/* pOp->u4WaitMs = u4WaitMs; */
		if (pSignal->timeoutValue) {
			pOp->result = -9;
			osal_signal_init(pSignal);
		}

		/* put to active Q */
		bRet = wmt_lib_put_op(&pWmtDev->rActiveOpQ, pOp);
		if (MTK_WCN_BOOL_FALSE == bRet) {
			WMT_WARN_FUNC("put to active queue fail\n");
			bCleanup = MTK_WCN_BOOL_TRUE;
			break;
		}

		/* wake up wmtd */
		/* wake_up_interruptible(&pWmtDev->rWmtdWq); */
		osal_trigger_event(&pWmtDev->rWmtdWq);

		if (0 == pSignal->timeoutValue) {
			bRet = MTK_WCN_BOOL_TRUE;
			/* clean it in wmtd */
			break;
		}
		/* wait result, clean it here */
		bCleanup = MTK_WCN_BOOL_TRUE;

		/* check result */
		/* wait_ret = wait_for_completion_interruptible_timeout(&pOp->comp, msecs_to_jiffies(u4WaitMs)); */
		/* wait_ret = wait_for_completion_timeout(&pOp->comp, msecs_to_jiffies(u4WaitMs)); */
		waitRet = osal_wait_for_signal_timeout(pSignal);
		WMT_DBG_FUNC("osal_wait_for_signal_timeout:%ld\n", waitRet);

		/* if (unlikely(!wait_ret)) { */
		if (0 == waitRet) {
			pThread = &gDevWmt.thread;
			WMT_ERR_FUNC
				("wait completion timeout, opId(%d), show wmtd_thread stack!\n", pOp->op.opId);
			/* TODO: how to handle it? retry? */
			wcn_wmtd_timeout_collect_ftrace();	/* trigger collect SYS_FTRACE */
			osal_thread_show_stack(pThread);
		} else {
			if (pOp->result)
				WMT_WARN_FUNC("opId(%d) result:%d\n", pOp->op.opId, pOp->result);
		}
		/* op completes, check result */
		bRet = (pOp->result) ? MTK_WCN_BOOL_FALSE : MTK_WCN_BOOL_TRUE;
	} while (0);

	if (bCleanup) {
		/* put Op back to freeQ */
		wmt_lib_put_op(&pWmtDev->rFreeOpQ, pOp);
	}

	return bRet;
}

/* TODO:[ChangeFeature][George] is this function obsoleted? */
#if 0
INT32 wmt_lib_reg_rw(UINT32 isWrite, UINT32 offset, PUINT32 pvalue, UINT32 mask)
{
	P_WMT_LXOP lxop;
	MTK_WCN_BOOL bRet;
	PUINT32 plv = NULL;
	UINT32 pbuf[2];
	P_OSAL_EVENT pSignal = NULL;

	if (!pvalue) {
		WMT_WARN_FUNC("!pvalue\n");
		return -1;
	}
	lxop = wmt_lib_get_free_lxop();
	if (!lxop) {
		WMT_WARN_FUNC("get_free_lxop fail\n");

		return -1;
	}

	plv = (PUINT32) (((UINT32) pbuf + 0x3) & ~0x3UL);
	*plv = *pvalue;
	pSignal = &lxop->signal;
	WMT_DBG_FUNC("OPID_REG_RW isWrite(%d) offset(0x%x) value(0x%x) mask(0x%x)\n", isWrite, offset, *pvalue, mask);

	lxop->op.opId = WMT_OPID_REG_RW;
	lxop->op.au4OpData[0] = isWrite;
	lxop->op.au4OpData[1] = offset;
	lxop->op.au4OpData[2] = (UINT32) plv;
	lxop->op.au4OpData[3] = mask;
	pSignal->timeoutValue = MAX_EACH_WMT_CMD;

	DISABLE_PSM_MONITOR();
	bRet = wmt_lib_put_act_lxop(lxop);
	ENABLE_PSM_MONITOR();

	if (MTK_WCN_BOOL_FALSE != bRet) {
		WMT_DBG_FUNC("OPID_REG_RW isWrite(%u) offset(0x%x) value(0x%x) mask(0x%x) ok\n",
			     isWrite, offset, *plv, mask);
		if (!isWrite)
			*pvalue = *plv;
	} else {
		WMT_WARN_FUNC("OPID_REG_RW isWrite(%u) offset(0x%x) value(0x%x) mask(0x%x) bRet(%d)\n",
			      isWrite, offset, *plv, mask, bRet);
	}

	return bRet;
}
#endif

/* TODO:[ChangeFeature][George] is this function obsoleted? */
#if 0
static VOID wmt_lib_clear_chip_id(VOID)
{
/*
    gDevWmt.pChipInfo = NULL;
*/
	gDevWmt.hw_ver = WMTHWVER_INVALID;
}
#endif

/* TODO: [FixMe][GeorgeKuo]: change this API to report real chip id, hw_ver, and */
/* fw_ver instead of WMT-translated WMTHWVER */
ENUM_WMTHWVER_TYPE_T wmt_lib_get_hwver(VOID)
{
/*
    P_WMT_CMB_CHIP_INFO_S pChipInfo = NULL;
    P_DEV_WMT pWmtDev = gpDevWmt;
       pChipInfo = wmt_lib_get_chip_info(pWmtDev);
    return pChipInfo != NULL ? pChipInfo->eHwVersion : WMTHWVER_INVALID;
    */
	return gDevWmt.eWmtHwVer;
}

UINT32 wmt_lib_get_icinfo(ENUM_WMT_CHIPINFO_TYPE_T index)
{
	if (WMTCHIN_CHIPID == index)
		return gDevWmt.chip_id;
	else if (WMTCHIN_HWVER == index)
		return gDevWmt.hw_ver;
	else if (WMTCHIN_MAPPINGHWVER == index)
		return gDevWmt.eWmtHwVer;
	else if (WMTCHIN_FWVER == index)
		return gDevWmt.fw_ver;

	return 0;

}

PUINT8 wmt_lib_def_patch_name(VOID)
{
	WMT_INFO_FUNC("wmt-lib: use default patch name (%s)\n", gDevWmt.cPatchName);
	return gDevWmt.cPatchName;
}

MTK_WCN_BOOL wmt_lib_is_therm_ctrl_support(VOID)
{
	MTK_WCN_BOOL bIsSupportTherm = MTK_WCN_BOOL_TRUE;
	/* TODO:[FixMe][GeorgeKuo]: move IC-dependent checking to ic-implementation file */
	if (((0x6620 == gDevWmt.chip_id) && (WMTHWVER_E3 > gDevWmt.eWmtHwVer))
	    || (WMTHWVER_INVALID == gDevWmt.eWmtHwVer)) {
		WMT_ERR_FUNC("thermal command fail: chip version(WMTHWVER_TYPE:%d) is not valid\n", gDevWmt.eWmtHwVer);
		bIsSupportTherm = MTK_WCN_BOOL_FALSE;
	}
	if (!mtk_wcn_stp_is_ready()) {
		WMT_ERR_FUNC("thermal command can not be send: STP is not ready\n");
		bIsSupportTherm = MTK_WCN_BOOL_FALSE;
	}

	return bIsSupportTherm;
}

MTK_WCN_BOOL wmt_lib_is_dsns_ctrl_support(VOID)
{
	/* TODO:[FixMe][GeorgeKuo]: move IC-dependent checking to ic-implementation file */
	if (((0x6620 == gDevWmt.chip_id) && (WMTHWVER_E3 > gDevWmt.eWmtHwVer))
	    || (WMTHWVER_INVALID == gDevWmt.eWmtHwVer)) {
		WMT_ERR_FUNC("thermal command fail: chip version(WMTHWVER_TYPE:%d) is not valid\n", gDevWmt.eWmtHwVer);
		return MTK_WCN_BOOL_FALSE;
	}

	return MTK_WCN_BOOL_TRUE;
}

/*!
 * \brief Update combo chip pin settings (GPIO)
 *
 * An internal library function to support various settings for chip GPIO. It is
 * updated in a grouping way: configure all required pins in a single call.
 *
 * \param id desired pin ID to be controlled
 * \param stat desired pin states to be set
 * \param flag supplementary options for this operation
 *
 * \retval 0 operation success
 * \retval -1 invalid id
 * \retval -2 invalid stat
 * \retval < 0 error for operation fail
 */
static INT32 wmt_lib_pin_ctrl(WMT_IC_PIN_ID id, WMT_IC_PIN_STATE stat, UINT32 flag)
{
	P_OSAL_OP pOp;
	MTK_WCN_BOOL bRet;
	P_OSAL_SIGNAL pSignal;

	/* input sanity check */
	if (WMT_IC_PIN_MAX <= id) {
		WMT_ERR_FUNC("invalid ic pin id(%d)\n", id);
		return -1;
	}
	if (WMT_IC_PIN_STATE_MAX <= stat) {
		WMT_ERR_FUNC("invalid ic pin state (%d)\n", stat);
		return -2;
	}

	pOp = wmt_lib_get_free_op();
	if (!pOp) {
		WMT_WARN_FUNC("get_free_lxop fail\n");
		return MTK_WCN_BOOL_FALSE;
	}

	WMT_DBG_FUNC("call WMT_OPID_GPIO_CTRL (ic pin id:%d, stat:%d, flag:0x%x)\n", id, stat, flag);

	pSignal = &pOp->signal;
	pOp->op.opId = WMT_OPID_GPIO_CTRL;
	pOp->op.au4OpData[0] = id;
	pOp->op.au4OpData[1] = stat;
	pOp->op.au4OpData[2] = flag;
	pSignal->timeoutValue = MAX_EACH_WMT_CMD;

	/*wake up chip first */
	if (DISABLE_PSM_MONITOR()) {
		WMT_ERR_FUNC("wake up failed\n");
		wmt_lib_put_op_to_free_queue(pOp);
		return -1;
	}

	bRet = wmt_lib_put_act_op(pOp);
	ENABLE_PSM_MONITOR();
	if (MTK_WCN_BOOL_FALSE == bRet)
		WMT_WARN_FUNC("PIN_ID(%d) PIN_STATE(%d) flag(%d) fail\n", id, stat, flag);
	else
		WMT_DBG_FUNC("OPID(%d) type(%d) ok\n", pOp->op.opId, pOp->op.au4OpData[0]);

	return 0;
}

INT32 wmt_lib_reg_rw(UINT32 isWrite, UINT32 offset, PUINT32 pvalue, UINT32 mask)
{
	P_OSAL_OP pOp;
	MTK_WCN_BOOL bRet;
	UINT32 value;
	P_OSAL_SIGNAL pSignal;

	if (!pvalue) {
		WMT_WARN_FUNC("!pvalue\n");
		return -1;
	}

	pOp = wmt_lib_get_free_op();
	if (!pOp) {
		WMT_WARN_FUNC("get_free_lxop fail\n");
		return -1;
	}

	pSignal = &pOp->signal;
	pSignal->timeoutValue = MAX_EACH_WMT_CMD;
	value = *pvalue;
	WMT_DBG_FUNC("OPID_REG_RW isWrite(%u) offset(0x%x) value(0x%x) mask(0x%x)\n\n", isWrite, offset, *pvalue, mask);
	pOp->op.opId = WMT_OPID_REG_RW;
	pOp->op.au4OpData[0] = isWrite;
	pOp->op.au4OpData[1] = offset;
	pOp->op.au4OpData[2] = (SIZE_T)&value;
	pOp->op.au4OpData[3] = mask;
	if (DISABLE_PSM_MONITOR()) {
		WMT_ERR_FUNC("wake up failed\n");
		wmt_lib_put_op_to_free_queue(pOp);
		return -1;
	}

	bRet = wmt_lib_put_act_op(pOp);
	ENABLE_PSM_MONITOR();

	if (MTK_WCN_BOOL_FALSE != bRet) {
		WMT_DBG_FUNC("OPID_REG_RW isWrite(%u) offset(0x%x) value(0x%x) mask(0x%x) ok\n",
			     isWrite, offset, value, mask);
		if (!isWrite)
			*pvalue = value;

		return 0;
	}
	WMT_WARN_FUNC("OPID_REG_RW isWrite(%u) offset(0x%x) value(0x%x) mask(0x%x) bRet(%d)\n",
		      isWrite, offset, value, mask, bRet);
	return -1;
}

INT32 wmt_lib_efuse_rw(UINT32 isWrite, UINT32 offset, PUINT32 pvalue, UINT32 mask)
{
	P_OSAL_OP pOp;
	MTK_WCN_BOOL bRet;
	UINT32 value;
	P_OSAL_SIGNAL pSignal;

	if (!pvalue) {
		WMT_WARN_FUNC("!pvalue\n");
		return -1;
	}

	pOp = wmt_lib_get_free_op();
	if (!pOp) {
		WMT_WARN_FUNC("get_free_lxop fail\n");
		return -1;
	}

	pSignal = &pOp->signal;
	pSignal->timeoutValue = MAX_EACH_WMT_CMD;
	value = *pvalue;
	WMT_DBG_FUNC("OPID_EFUSE_RW isWrite(%u) offset(0x%x) value(0x%x) mask(0x%x)\n\n",
		     isWrite, offset, *pvalue, mask);
	pOp->op.opId = WMT_OPID_EFUSE_RW;
	pOp->op.au4OpData[0] = isWrite;
	pOp->op.au4OpData[1] = offset;
	pOp->op.au4OpData[2] = (SIZE_T)&value;
	pOp->op.au4OpData[3] = mask;
	if (DISABLE_PSM_MONITOR()) {
		WMT_ERR_FUNC("wake up failed\n");
		wmt_lib_put_op_to_free_queue(pOp);
		return -1;
	}

	bRet = wmt_lib_put_act_op(pOp);
	ENABLE_PSM_MONITOR();

	if (MTK_WCN_BOOL_FALSE != bRet) {
		WMT_DBG_FUNC("OPID_EFUSE_RW isWrite(%u) offset(0x%x) value(0x%x) mask(0x%x) ok\n",
			     isWrite, offset, value, mask);
		if (!isWrite)
			*pvalue = value;

		return 0;
	}
	WMT_WARN_FUNC("OPID_REG_RW isWrite(%u) offset(0x%x) value(0x%x) mask(0x%x) bRet(%d)\n",
		      isWrite, offset, value, mask, bRet);
	return -1;

}

/*!
 * \brief update combo chip AUDIO Interface (AIF) settings
 *
 * A library function to support updating chip AUDIO pin settings. A group of
 * pins is updated as a whole.
 *
 * \param aif desired audio interface state to use
 * \param flag whether audio pin is shared or not
 *
 * \retval 0 operation success
 * \retval -1 invalid aif
 * \retval < 0 error for invalid parameters or operation fail
 */
INT32 wmt_lib_set_aif(CMB_STUB_AIF_X aif, MTK_WCN_BOOL share)
{
	if (CMB_STUB_AIF_MAX <= aif) {
		WMT_ERR_FUNC("invalid aif (%d)\n", aif);
		return -1;
	}
	WMT_DBG_FUNC("call pin_ctrl for aif:%d, share:%d\n", aif, (MTK_WCN_BOOL_TRUE == share) ? 1 : 0);
	/* Translate CMB_STUB_AIF_X into WMT_IC_PIN_STATE by array */
	return wmt_lib_pin_ctrl(WMT_IC_PIN_AUDIO,
				cmb_aif2pin_stat[aif],
				(MTK_WCN_BOOL_TRUE == share) ? WMT_LIB_AIF_FLAG_SHARE : WMT_LIB_AIF_FLAG_SEPARATE);
}

INT32 wmt_lib_host_awake_get(VOID)
{
	return wmt_plat_wake_lock_ctrl(WL_OP_GET);
}

INT32 wmt_lib_host_awake_put(VOID)
{
	return wmt_plat_wake_lock_ctrl(WL_OP_PUT);
}

MTK_WCN_BOOL wmt_lib_btm_cb(MTKSTP_BTM_WMT_OP_T op)
{
	MTK_WCN_BOOL bRet = MTK_WCN_BOOL_FALSE;

	if (op == BTM_RST_OP) {
		/* high priority, not to enqueue into the queue of wmtd */
		WMT_INFO_FUNC("Invoke whole chip reset from stp_btm!!!\n");
		wmt_lib_cmb_rst(WMTRSTSRC_RESET_STP);
		bRet = MTK_WCN_BOOL_TRUE;
	} else if (op == BTM_DMP_OP) {

		WMT_WARN_FUNC("TBD!!!\n");
	} else if (op == BTM_GET_AEE_SUPPORT_FLAG) {
		bRet = wmt_core_get_aee_dump_flag();
	}
	return bRet;
}

MTK_WCN_BOOL wmt_cdev_rstmsg_snd(ENUM_WMTRSTMSG_TYPE_T msg)
{

	INT32 i = 0;
	P_DEV_WMT pDevWmt = &gDevWmt;
	PUINT8 drv_name[] = {
		"DRV_TYPE_BT",
		"DRV_TYPE_FM",
		"DRV_TYPE_GPS",
		"DRV_TYPE_WIFI"
	};

	for (i = 0; i <= WMTDRV_TYPE_WIFI; i++) {
		/* <1> check if reset callback is registered */
		if (pDevWmt->rFdrvCb.fDrvRst[i]) {
			/* <2> send the msg to this subfucntion */
			/*src, dst, msg_type, msg_data, msg_size */
			pDevWmt->rFdrvCb.fDrvRst[i] (WMTDRV_TYPE_WMT, i, WMTMSG_TYPE_RESET, &msg,
						     sizeof(ENUM_WMTRSTMSG_TYPE_T));
			WMT_INFO_FUNC("type = %s, msg sent\n", drv_name[i]);
		} else {
			WMT_DBG_FUNC("type = %s, unregistered\n", drv_name[i]);
		}
	}

	return MTK_WCN_BOOL_TRUE;
}

VOID wmt_lib_state_init(VOID)
{
	/* UINT32 i = 0; */
	P_DEV_WMT pDevWmt = &gDevWmt;
	P_OSAL_OP pOp;

	/* Initialize op queue */
	/* RB_INIT(&pDevWmt->rFreeOpQ, WMT_OP_BUF_SIZE); */
	/* RB_INIT(&pDevWmt->rActiveOpQ, WMT_OP_BUF_SIZE); */

	while (!RB_EMPTY(&pDevWmt->rActiveOpQ)) {
#if 0
		osal_signal_init(&(pOp->signal));
		wmt_lib_put_op(&pDevWmt->rFreeOpQ, pOp);
#endif
		pOp = wmt_lib_get_op(&pDevWmt->rActiveOpQ);
		if (pOp) {
			if (osal_op_is_wait_for_signal(pOp))
				osal_op_raise_signal(pOp, -1);
			else
				wmt_lib_put_op(&pDevWmt->rFreeOpQ, pOp);
		}
	}

	/* Put all to free Q */
	/*
	   for (i = 0; i < WMT_OP_BUF_SIZE; i++) {
	   osal_signal_init(&(pDevWmt->arQue[i].signal));
	   wmt_lib_put_op(&pDevWmt->rFreeOpQ, &(pDevWmt->arQue[i]));
	   } */
}

#if 0
INT32 wmt_lib_sdio_ctrl(UINT32 on)
{

	P_OSAL_OP pOp;
	MTK_WCN_BOOL bRet;
	P_OSAL_SIGNAL pSignal;

	pOp = wmt_lib_get_free_op();
	if (!pOp) {
		WMT_WARN_FUNC("get_free_lxop fail\n");
		return MTK_WCN_BOOL_FALSE;
	}

	WMT_DBG_FUNC("call WMT_OPID_SDIO_CTRL\n");

	pSignal = &pOp->signal;
	pOp->op.opId = WMT_OPID_SDIO_CTRL;
	pOp->op.au4OpData[0] = on;
	pSignal->timeoutValue = MAX_GPIO_CTRL_TIME;

	bRet = wmt_lib_put_act_op(pOp);
	if (MTK_WCN_BOOL_FALSE == bRet) {
		WMT_WARN_FUNC("WMT_OPID_SDIO_CTRL failed\n");
		return -1;
	}
	WMT_DBG_FUNC("OPID(WMT_OPID_SDIO_CTRL)ok\n");

	return 0;
}
#endif

MTK_WCN_BOOL wmt_lib_hw_state_show(VOID)
{
	P_OSAL_OP pOp;
	MTK_WCN_BOOL bRet;
	P_OSAL_SIGNAL pSignal;

	pOp = wmt_lib_get_free_op();
	if (!pOp) {
		WMT_WARN_FUNC("get_free_lxop fail\n");
		return MTK_WCN_BOOL_FALSE;
	}

	WMT_DBG_FUNC("call WMT_OPID_HW_STATE_SHOW\n");

	pSignal = &pOp->signal;
	pOp->op.opId = WMT_OPID_GPIO_STATE;
	pSignal->timeoutValue = MAX_GPIO_CTRL_TIME;

	bRet = wmt_lib_put_act_op(pOp);
	if (MTK_WCN_BOOL_FALSE == bRet) {
		WMT_WARN_FUNC("WMT_OPID_HW_STATE_SHOW failed\n");
		return MTK_WCN_BOOL_FALSE;
	}
	WMT_DBG_FUNC("OPID(WMT_OPID_HW_STATE_SHOW)ok\n");
	return MTK_WCN_BOOL_TRUE;
}

MTK_WCN_BOOL wmt_lib_hw_rst(VOID)
{

	P_OSAL_OP pOp;
	MTK_WCN_BOOL bRet;
	P_OSAL_SIGNAL pSignal;
	P_DEV_WMT pDevWmt = &gDevWmt;

	wmt_lib_state_init();

	osal_clear_bit(WMT_STAT_STP_REG, &pDevWmt->state);
	osal_clear_bit(WMT_STAT_STP_OPEN, &pDevWmt->state);
	osal_clear_bit(WMT_STAT_STP_EN, &pDevWmt->state);
	osal_clear_bit(WMT_STAT_STP_RDY, &pDevWmt->state);
	osal_clear_bit(WMT_STAT_RX, &pDevWmt->state);
	osal_clear_bit(WMT_STAT_CMD, &pDevWmt->state);

	/*Before do hardware reset, we show GPIO state to check if others modified our pin state accidentially */
	wmt_lib_hw_state_show();
	pOp = wmt_lib_get_free_op();
	if (!pOp) {
		WMT_WARN_FUNC("get_free_lxop fail\n");
		return MTK_WCN_BOOL_FALSE;
	}

	WMT_DBG_FUNC("call WMT_OPID_HW_RST\n");

	pSignal = &pOp->signal;
	pOp->op.opId = WMT_OPID_HW_RST;
	pSignal->timeoutValue = MAX_GPIO_CTRL_TIME;

	bRet = wmt_lib_put_act_op(pOp);
	if (MTK_WCN_BOOL_FALSE == bRet) {
		WMT_WARN_FUNC("WMT_OPID_HW_RST failed\n");
		return MTK_WCN_BOOL_FALSE;
	}
	WMT_DBG_FUNC("OPID(WMT_OPID_HW_RST)ok\n");
	return MTK_WCN_BOOL_TRUE;
}

MTK_WCN_BOOL wmt_lib_sw_rst(INT32 baudRst)
{

	P_OSAL_OP pOp;
	MTK_WCN_BOOL bRet;
	P_OSAL_SIGNAL pSignal;

	/* <1> wmt state reset */
	wmt_lib_state_init();

	/* <2> Reset STP data structure */
	WMT_DBG_FUNC("Cleanup STP context\n");
	mtk_wcn_stp_flush_context();
	/* <3> Reset STP-PSM data structure */
	WMT_DBG_FUNC("Cleanup STP-PSM context\n");
	mtk_wcn_stp_psm_reset();

	/* <4> do sw reset in wmt-core */
	pOp = wmt_lib_get_free_op();
	if (!pOp) {
		WMT_WARN_FUNC("get_free_lxop fail\n");
		return MTK_WCN_BOOL_FALSE;
	}

	WMT_DBG_FUNC("call WMT_OPID_SW_RST\n");

	pSignal = &pOp->signal;
	pSignal->timeoutValue = MAX_FUNC_ON_TIME;

	pOp->op.opId = WMT_OPID_SW_RST;
	pOp->op.au4OpData[0] = baudRst;

	bRet = wmt_lib_put_act_op(pOp);
	if (MTK_WCN_BOOL_FALSE == bRet) {
		WMT_WARN_FUNC("WMT_OPID_SW_RST failed\n");
		return MTK_WCN_BOOL_FALSE;
	}
	WMT_DBG_FUNC("OPID(WMT_OPID_SW_RST)ok\n");
	return MTK_WCN_BOOL_TRUE;
}

ENUM_WMTRSTRET_TYPE_T wmt_lib_cmb_rst(ENUM_WMTRSTSRC_TYPE_T src)
{
#define RETRYTIMES 10
	MTK_WCN_BOOL bRet;
	ENUM_WMTRSTRET_TYPE_T retval = WMTRSTRET_MAX;
	ENUM_WMTRSTMSG_TYPE_T rstMsg = WMTRSTMSG_RESET_MAX;
	INT32 retries = RETRYTIMES;
	P_DEV_WMT pDevWmt = &gDevWmt;
	P_OSAL_OP pOp;
	PUINT8 srcName[] = { "WMTRSTSRC_RESET_BT",
		"WMTRSTSRC_RESET_FM",
		"WMTRSTSRC_RESET_GPS",
		"WMTRSTSRC_RESET_WIFI",
		"WMTRSTSRC_RESET_STP",
		"WMTRSTSRC_RESET_TEST"
	};

	if (src < WMTRSTSRC_RESET_MAX)
		WMT_INFO_FUNC("reset source = %s\n", srcName[src]);

	if (WMTRSTSRC_RESET_TEST == src) {
		pOp = wmt_lib_get_current_op(pDevWmt);
		if (pOp && ((WMT_OPID_FUNC_ON == pOp->op.opId)
			    || (WMT_OPID_FUNC_OFF == pOp->op.opId))) {
			WMT_INFO_FUNC("can't do reset by test src when func on/off\n");
			return -1;
		}
	}
	/* <1> Consider the multi-context combo_rst case. */
	if (osal_test_and_set_bit(WMT_STAT_RST_ON, &pDevWmt->state)) {
		retval = WMTRSTRET_ONGOING;
		goto rstDone;
	}
	/* <2> Block all STP request */
	mtk_wcn_stp_enable(0);

	/* <3> RESET_START notification */
	bRet = wmt_cdev_rstmsg_snd(WMTRSTMSG_RESET_START);
	if (bRet == MTK_WCN_BOOL_FALSE) {
		WMT_ERR_FUNC("[whole chip reset] fail at wmt_lib_rstmsg_snd!\n");
		retval = WMTRSTRET_FAIL;
		goto rstDone;
	}
	/* wakeup blocked opid */
	pOp = wmt_lib_get_current_op(pDevWmt);
	if (osal_op_is_wait_for_signal(pOp))
		osal_op_raise_signal(pOp, -1);

	/* wakeup blocked cmd */
	wmt_dev_rx_event_cb();

	/* <4> retry until reset flow successful */
	while (retries > 0) {
		/* <4.1> reset combo hw */
		bRet = wmt_lib_hw_rst();
		if (bRet == MTK_WCN_BOOL_FALSE) {
			WMT_ERR_FUNC("[whole chip reset] fail at wmt_lib_hw_rst!\n");
			retries--;
			continue;
		}
		/* <4.2> reset driver/combo sw */
		bRet = wmt_lib_sw_rst(1);
		if (bRet == MTK_WCN_BOOL_FALSE) {
			WMT_ERR_FUNC("[whole chip reset] fail at wmt_lib_sw_rst!\n");
			retries--;
			continue;
		}
		break;
	}

	osal_clear_bit(WMT_STAT_RST_ON, &pDevWmt->state);

	if (bRet == MTK_WCN_BOOL_FALSE) {
		rstMsg = WMTRSTMSG_RESET_END_FAIL;
		WMT_WARN_FUNC("[whole chip reset] fail! retries = %d\n", RETRYTIMES - retries);
	} else {
		rstMsg = WMTRSTMSG_RESET_END;
		WMT_INFO_FUNC("[whole chip reset] ok! retries = %d\n", RETRYTIMES - retries);
	}

	/* <5> RESET_END notification */
	bRet = wmt_cdev_rstmsg_snd(rstMsg);
	if (bRet == MTK_WCN_BOOL_FALSE) {
		WMT_ERR_FUNC("[whole chip reset] fail at wmt_lib_rstmsg_snd!\n");
		retval = WMTRSTRET_FAIL;
	} else {
		retval = WMTRSTMSG_RESET_END == rstMsg ? WMTRSTRET_SUCCESS : WMTRSTRET_FAIL;
	}
	mtk_wcn_stp_coredump_start_ctrl(0);
	mtk_wcn_stp_set_wmt_evt_err_trg_assert(0);
rstDone:
	if (osal_test_and_clear_bit(WMT_STAT_RST_ON, &pDevWmt->state))
		WMT_WARN_FUNC("[whole chip reset] retval = %d\n", retval);

	return retval;
}

MTK_WCN_BOOL wmt_lib_msgcb_reg(ENUM_WMTDRV_TYPE_T eType, PF_WMT_CB pCb)
{

	MTK_WCN_BOOL bRet = MTK_WCN_BOOL_FALSE;
	P_DEV_WMT pWmtDev = &gDevWmt;

	if (eType >= 0 && eType <= WMTDRV_TYPE_WIFI) {
		WMT_DBG_FUNC("reg ok!\n");
		pWmtDev->rFdrvCb.fDrvRst[eType] = pCb;
		bRet = MTK_WCN_BOOL_TRUE;
	} else {
		WMT_WARN_FUNC("reg fail!\n");
	}

	return bRet;
}

MTK_WCN_BOOL wmt_lib_msgcb_unreg(ENUM_WMTDRV_TYPE_T eType)
{
	MTK_WCN_BOOL bRet = MTK_WCN_BOOL_FALSE;
	P_DEV_WMT pWmtDev = &gDevWmt;

	if (eType >= 0 && eType <= WMTDRV_TYPE_WIFI) {
		WMT_DBG_FUNC("unreg ok!\n");
		pWmtDev->rFdrvCb.fDrvRst[eType] = NULL;
		bRet = MTK_WCN_BOOL_TRUE;
	} else {
		WMT_WARN_FUNC("unreg fail!\n");
	}

	return bRet;
}

UINT32 wmt_lib_dbg_level_set(UINT32 level)
{
	gWmtDbgLvl = level > WMT_LOG_LOUD ? WMT_LOG_LOUD : level;
	return 0;
}

INT32 wmt_lib_set_stp_wmt_last_close(UINT32 value)
{
	return mtk_wcn_stp_set_wmt_last_close(value);
}

INT32 wmt_lib_notify_stp_sleep(void)
{
	INT32 iRet = 0x0;

	iRet = wmt_lib_psm_lock_aquire();
	if (iRet) {
		WMT_ERR_FUNC("--->lock psm_lock failed, iRet=%d\n", iRet);
		return iRet;
	}

	iRet = mtk_wcn_stp_notify_sleep_for_thermal();
	wmt_lib_psm_lock_release();

	return iRet;
}

VOID wmt_lib_set_patch_num(UINT32 num)
{
	P_DEV_WMT pWmtDev = &gDevWmt;

	pWmtDev->patchNum = num;
}

VOID wmt_lib_set_patch_info(P_WMT_PATCH_INFO pPatchinfo)
{
	P_DEV_WMT pWmtDev = &gDevWmt;

	if (pPatchinfo)
		pWmtDev->pWmtPatchInfo = pPatchinfo;

}

INT32 wmt_lib_set_current_op(P_DEV_WMT pWmtDev, P_OSAL_OP pOp)
{
	if (pWmtDev) {
		pWmtDev->pCurOP = pOp;
		WMT_DBG_FUNC("pOp=0x%p\n", pOp);
		return 0;
	}
	WMT_ERR_FUNC("Invalid pointer\n");
	return -1;
}

P_OSAL_OP wmt_lib_get_current_op(P_DEV_WMT pWmtDev)
{
	if (pWmtDev)
		return pWmtDev->pCurOP;

	WMT_ERR_FUNC("Invalid pointer\n");
	return NULL;
}

UINT8 *wmt_lib_get_fwinfor_from_emi(UINT8 section, UINT32 offset, UINT8 *buf, UINT32 len)
{
	UINT8 *pAddr = NULL;
	UINT32 sublen1 = 0;
	UINT32 sublen2 = 0;
	P_CONSYS_EMI_ADDR_INFO p_consys_info;

	p_consys_info = wmt_plat_get_emi_phy_add();
	osal_assert(p_consys_info);

	if (section == 0) {
		pAddr = wmt_plat_get_emi_virt_add(0x0);
		if (len > 1024)
			len = 1024;
		if (!pAddr) {
			WMT_ERR_FUNC("wmt-lib: get EMI virtual base address fail\n");
		} else {
			WMT_INFO_FUNC("vir addr(0x%p)\n", pAddr);
			osal_memcpy(&buf[0], pAddr, len);
		}
	} else {
		if (offset >= 0x7fff)
			offset = 0x0;

		if (offset + len > 32768) {
			pAddr = wmt_plat_get_emi_virt_add(offset + p_consys_info->paged_trace_off);
			if (!pAddr) {
				WMT_ERR_FUNC("wmt-lib: get part1 EMI virtual base address fail\n");
			} else {
				WMT_INFO_FUNC("part1 vir addr(0x%p)\n", pAddr);
				sublen1 = 0x7fff - offset;
				osal_memcpy(&buf[0], pAddr, sublen1);
			}
			pAddr = wmt_plat_get_emi_virt_add(p_consys_info->paged_trace_off);
			if (!pAddr) {
				WMT_ERR_FUNC("wmt-lib: get part2 EMI virtual base address fail\n");
			} else {
				WMT_INFO_FUNC("part2 vir addr(0x%p)\n", pAddr);
				sublen2 = len - sublen1;
				osal_memcpy(&buf[sublen1], pAddr, sublen2);
			}
		} else {
			pAddr = wmt_plat_get_emi_virt_add(offset + p_consys_info->paged_trace_off);
			if (!pAddr) {
				WMT_ERR_FUNC("wmt-lib: get EMI virtual base address fail\n");
			} else {
				WMT_INFO_FUNC("vir addr(0x%p)\n", pAddr);
				osal_memcpy(&buf[0], pAddr, len);
			}
		}
	}

	return 0;
}

INT32 wmt_lib_poll_cpupcr(UINT32 count, UINT16 sleep, UINT16 toAee)
{
	ENUM_STP_FW_ISSUE_TYPE issue_type;

	issue_type = STP_DBG_PROC_TEST;

	stp_dbg_poll_cpupcr(count, sleep, 1);

	if (toAee) {
		stp_dbg_set_fw_info("STP ProcTest", osal_strlen("STP ProcTest"), issue_type);
		osal_dbg_assert_aee("[SOC_CONSYS]ProcTest",
				    "**[WCN_ISSUE_INFO]STP Tx Timeout**\n Polling CPUPCR for FW debug usage\n");
	} else {
		WMT_INFO_FUNC("wmt_lib:do not pass cpupcr to AEE\n");
	}
	return 0;
}

UINT8 *wmt_lib_get_cpupcr_xml_format(UINT32 *len)
{
	PUINT8 temp;
	UINT32 i = 0;

	osal_memset(&g_cpupcr_buf[0], 0, WMT_STP_CPUPCR_BUF_SIZE);
	temp = g_cpupcr_buf;
	stp_dbg_cpupcr_infor_format(&temp, len);

	pr_debug("print xml buffer,len(%d):\n\n", *len);
	for (i = 0; i < *len; i++)
		pr_cont("%c", g_cpupcr_buf[i]);

	return &g_cpupcr_buf[0];
}

UINT32 wmt_lib_set_host_assert_info(UINT32 type, UINT32 reason, UINT32 en)
{
	return stp_dbg_set_host_assert_info(type, reason, en);
}

INT32 wmt_lib_register_thermal_ctrl_cb(thermal_query_ctrl_cb thermal_ctrl)
{
	wmt_plat_thermal_ctrl_cb_reg(thermal_ctrl);
	return 0;
}

INT8 wmt_lib_co_clock_get(void)
{
	if (gDevWmt.rWmtGenConf.cfgExist)
		return gDevWmt.rWmtGenConf.co_clock_flag;
	else
		return -1;
}

#if CFG_WMT_PS_SUPPORT
UINT32 wmt_lib_quick_sleep_ctrl(UINT32 en)
{
	WMT_WARN_FUNC("%s quick sleep mode\n", en ? "enable" : "disable");
	g_quick_sleep_ctrl = en;
	return 0;
}
#endif

#if CONSYS_ENALBE_SET_JTAG
UINT32 wmt_lib_jtag_flag_set(UINT32 en)
{
	return wmt_plat_jtag_flag_ctrl(en);
}
#endif

UINT32 wmt_lib_soc_set_wifiver(UINT32 wifiver)
{
	return stp_dbg_set_wifiver(wifiver);
}
