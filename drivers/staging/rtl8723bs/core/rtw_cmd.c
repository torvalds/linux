// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _RTW_CMD_C_

#include <drv_types.h>
#include <rtw_debug.h>
#include <hal_btcoex.h>
#include <linux/jiffies.h>

static struct _cmd_callback rtw_cmd_callback[] = {
	{GEN_CMD_CODE(_Read_MACREG), NULL}, /*0*/
	{GEN_CMD_CODE(_Write_MACREG), NULL},
	{GEN_CMD_CODE(_Read_BBREG), &rtw_getbbrfreg_cmdrsp_callback},
	{GEN_CMD_CODE(_Write_BBREG), NULL},
	{GEN_CMD_CODE(_Read_RFREG), &rtw_getbbrfreg_cmdrsp_callback},
	{GEN_CMD_CODE(_Write_RFREG), NULL}, /*5*/
	{GEN_CMD_CODE(_Read_EEPROM), NULL},
	{GEN_CMD_CODE(_Write_EEPROM), NULL},
	{GEN_CMD_CODE(_Read_EFUSE), NULL},
	{GEN_CMD_CODE(_Write_EFUSE), NULL},

	{GEN_CMD_CODE(_Read_CAM),	NULL},	/*10*/
	{GEN_CMD_CODE(_Write_CAM),	 NULL},
	{GEN_CMD_CODE(_setBCNITV), NULL},
	{GEN_CMD_CODE(_setMBIDCFG), NULL},
	{GEN_CMD_CODE(_JoinBss), &rtw_joinbss_cmd_callback},  /*14*/
	{GEN_CMD_CODE(_DisConnect), &rtw_disassoc_cmd_callback}, /*15*/
	{GEN_CMD_CODE(_CreateBss), &rtw_createbss_cmd_callback},
	{GEN_CMD_CODE(_SetOpMode), NULL},
	{GEN_CMD_CODE(_SiteSurvey), &rtw_survey_cmd_callback}, /*18*/
	{GEN_CMD_CODE(_SetAuth), NULL},

	{GEN_CMD_CODE(_SetKey), NULL},	/*20*/
	{GEN_CMD_CODE(_SetStaKey), &rtw_setstaKey_cmdrsp_callback},
	{GEN_CMD_CODE(_SetAssocSta), &rtw_setassocsta_cmdrsp_callback},
	{GEN_CMD_CODE(_DelAssocSta), NULL},
	{GEN_CMD_CODE(_SetStaPwrState), NULL},
	{GEN_CMD_CODE(_SetBasicRate), NULL}, /*25*/
	{GEN_CMD_CODE(_GetBasicRate), NULL},
	{GEN_CMD_CODE(_SetDataRate), NULL},
	{GEN_CMD_CODE(_GetDataRate), NULL},
	{GEN_CMD_CODE(_SetPhyInfo), NULL},

	{GEN_CMD_CODE(_GetPhyInfo), NULL}, /*30*/
	{GEN_CMD_CODE(_SetPhy), NULL},
	{GEN_CMD_CODE(_GetPhy), NULL},
	{GEN_CMD_CODE(_readRssi), NULL},
	{GEN_CMD_CODE(_readGain), NULL},
	{GEN_CMD_CODE(_SetAtim), NULL}, /*35*/
	{GEN_CMD_CODE(_SetPwrMode), NULL},
	{GEN_CMD_CODE(_JoinbssRpt), NULL},
	{GEN_CMD_CODE(_SetRaTable), NULL},
	{GEN_CMD_CODE(_GetRaTable), NULL},

	{GEN_CMD_CODE(_GetCCXReport), NULL}, /*40*/
	{GEN_CMD_CODE(_GetDTMReport),	NULL},
	{GEN_CMD_CODE(_GetTXRateStatistics), NULL},
	{GEN_CMD_CODE(_SetUsbSuspend), NULL},
	{GEN_CMD_CODE(_SetH2cLbk), NULL},
	{GEN_CMD_CODE(_AddBAReq), NULL}, /*45*/
	{GEN_CMD_CODE(_SetChannel), NULL},		/*46*/
	{GEN_CMD_CODE(_SetTxPower), NULL},
	{GEN_CMD_CODE(_SwitchAntenna), NULL},
	{GEN_CMD_CODE(_SetCrystalCap), NULL},
	{GEN_CMD_CODE(_SetSingleCarrierTx), NULL},	/*50*/

	{GEN_CMD_CODE(_SetSingleToneTx), NULL}, /*51*/
	{GEN_CMD_CODE(_SetCarrierSuppressionTx), NULL},
	{GEN_CMD_CODE(_SetContinuousTx), NULL},
	{GEN_CMD_CODE(_SwitchBandwidth), NULL},		/*54*/
	{GEN_CMD_CODE(_TX_Beacon), NULL},/*55*/

	{GEN_CMD_CODE(_Set_MLME_EVT), NULL},/*56*/
	{GEN_CMD_CODE(_Set_Drv_Extra), NULL},/*57*/
	{GEN_CMD_CODE(_Set_H2C_MSG), NULL},/*58*/
	{GEN_CMD_CODE(_SetChannelPlan), NULL},/*59*/
	{GEN_CMD_CODE(_LedBlink), NULL},/*60*/

	{GEN_CMD_CODE(_SetChannelSwitch), NULL},/*61*/
	{GEN_CMD_CODE(_TDLS), NULL},/*62*/
	{GEN_CMD_CODE(_ChkBMCSleepq), NULL}, /*63*/

	{GEN_CMD_CODE(_RunInThreadCMD), NULL},/*64*/
};

static struct cmd_hdl wlancmds[] = {
	GEN_DRV_CMD_HANDLER(0, NULL) /*0*/
	GEN_DRV_CMD_HANDLER(0, NULL)
	GEN_DRV_CMD_HANDLER(0, NULL)
	GEN_DRV_CMD_HANDLER(0, NULL)
	GEN_DRV_CMD_HANDLER(0, NULL)
	GEN_DRV_CMD_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL) /*10*/
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct joinbss_parm), join_cmd_hdl) /*14*/
	GEN_MLME_EXT_HANDLER(sizeof(struct disconnect_parm), disconnect_hdl)
	GEN_MLME_EXT_HANDLER(sizeof(struct createbss_parm), createbss_hdl)
	GEN_MLME_EXT_HANDLER(sizeof(struct setopmode_parm), setopmode_hdl)
	GEN_MLME_EXT_HANDLER(sizeof(struct sitesurvey_parm), sitesurvey_cmd_hdl) /*18*/
	GEN_MLME_EXT_HANDLER(sizeof(struct setauth_parm), setauth_hdl)
	GEN_MLME_EXT_HANDLER(sizeof(struct setkey_parm), setkey_hdl) /*20*/
	GEN_MLME_EXT_HANDLER(sizeof(struct set_stakey_parm), set_stakey_hdl)
	GEN_MLME_EXT_HANDLER(sizeof(struct set_assocsta_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct del_assocsta_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct setstapwrstate_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct setbasicrate_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct getbasicrate_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct setdatarate_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct getdatarate_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct setphyinfo_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct getphyinfo_parm), NULL)  /*30*/
	GEN_MLME_EXT_HANDLER(sizeof(struct setphy_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct getphy_parm), NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)	/*40*/
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct addBaReq_parm), add_ba_hdl)
	GEN_MLME_EXT_HANDLER(sizeof(struct set_ch_parm), set_ch_hdl) /* 46 */
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL) /*50*/
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct Tx_Beacon_param), tx_beacon_hdl) /*55*/

	GEN_MLME_EXT_HANDLER(0, mlme_evt_hdl) /*56*/
	GEN_MLME_EXT_HANDLER(0, rtw_drvextra_cmd_hdl) /*57*/

	GEN_MLME_EXT_HANDLER(0, h2c_msg_hdl) /*58*/
	GEN_MLME_EXT_HANDLER(sizeof(struct SetChannelPlan_param), set_chplan_hdl) /*59*/
	GEN_MLME_EXT_HANDLER(sizeof(struct LedBlink_param), led_blink_hdl) /*60*/

	GEN_MLME_EXT_HANDLER(sizeof(struct SetChannelSwitch_param), set_csa_hdl) /*61*/
	GEN_MLME_EXT_HANDLER(sizeof(struct TDLSoption_param), tdls_hdl) /*62*/
	GEN_MLME_EXT_HANDLER(0, chk_bmc_sleepq_hdl) /*63*/
	GEN_MLME_EXT_HANDLER(sizeof(struct RunInThread_param), run_in_thread_hdl) /*63*/
};

/*
Caller and the rtw_cmd_thread can protect cmd_q by spin_lock.
No irqsave is necessary.
*/

int rtw_init_cmd_priv(struct	cmd_priv *pcmdpriv)
{
	int res = 0;

	init_completion(&pcmdpriv->cmd_queue_comp);
	init_completion(&pcmdpriv->terminate_cmdthread_comp);

	_rtw_init_queue(&(pcmdpriv->cmd_queue));

	/* allocate DMA-able/Non-Page memory for cmd_buf and rsp_buf */

	pcmdpriv->cmd_seq = 1;

	pcmdpriv->cmd_allocated_buf = rtw_zmalloc(MAX_CMDSZ + CMDBUFF_ALIGN_SZ);

	if (!pcmdpriv->cmd_allocated_buf) {
		res = -ENOMEM;
		goto exit;
	}

	pcmdpriv->cmd_buf = pcmdpriv->cmd_allocated_buf  +  CMDBUFF_ALIGN_SZ - ((SIZE_PTR)(pcmdpriv->cmd_allocated_buf) & (CMDBUFF_ALIGN_SZ-1));

	pcmdpriv->rsp_allocated_buf = rtw_zmalloc(MAX_RSPSZ + 4);

	if (!pcmdpriv->rsp_allocated_buf) {
		res = -ENOMEM;
		goto exit;
	}

	pcmdpriv->rsp_buf = pcmdpriv->rsp_allocated_buf  +  4 - ((SIZE_PTR)(pcmdpriv->rsp_allocated_buf) & 3);

	pcmdpriv->cmd_issued_cnt = 0;
	pcmdpriv->cmd_done_cnt = 0;
	pcmdpriv->rsp_cnt = 0;

	mutex_init(&pcmdpriv->sctx_mutex);
exit:
	return res;
}

static void c2h_wk_callback(_workitem * work);
int rtw_init_evt_priv(struct evt_priv *pevtpriv)
{
	/* allocate DMA-able/Non-Page memory for cmd_buf and rsp_buf */
	atomic_set(&pevtpriv->event_seq, 0);
	pevtpriv->evt_done_cnt = 0;

	_init_workitem(&pevtpriv->c2h_wk, c2h_wk_callback, NULL);
	pevtpriv->c2h_wk_alive = false;
	pevtpriv->c2h_queue = rtw_cbuf_alloc(C2H_QUEUE_MAX_LEN+1);
	if (!pevtpriv->c2h_queue)
		return -ENOMEM;

	return 0;
}

void _rtw_free_evt_priv(struct	evt_priv *pevtpriv)
{
	RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, ("+_rtw_free_evt_priv\n"));

	_cancel_workitem_sync(&pevtpriv->c2h_wk);
	while (pevtpriv->c2h_wk_alive)
		msleep(10);

	while (!rtw_cbuf_empty(pevtpriv->c2h_queue)) {
		void *c2h = rtw_cbuf_pop(pevtpriv->c2h_queue);
		if (c2h != NULL && c2h != (void *)pevtpriv) {
			kfree(c2h);
		}
	}
	kfree(pevtpriv->c2h_queue);

	RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, ("-_rtw_free_evt_priv\n"));
}

void _rtw_free_cmd_priv(struct	cmd_priv *pcmdpriv)
{
	if (pcmdpriv) {
		kfree(pcmdpriv->cmd_allocated_buf);

		kfree(pcmdpriv->rsp_allocated_buf);

		mutex_destroy(&pcmdpriv->sctx_mutex);
	}
}

/*
Calling Context:

rtw_enqueue_cmd can only be called between kernel thread,
since only spin_lock is used.

ISR/Call-Back functions can't call this sub-function.

*/

int _rtw_enqueue_cmd(struct __queue *queue, struct cmd_obj *obj)
{
	_irqL irqL;

	if (obj == NULL)
		goto exit;

	/* spin_lock_bh(&queue->lock); */
	spin_lock_irqsave(&queue->lock, irqL);

	list_add_tail(&obj->list, &queue->queue);

	/* spin_unlock_bh(&queue->lock); */
	spin_unlock_irqrestore(&queue->lock, irqL);

exit:
	return _SUCCESS;
}

struct	cmd_obj	*_rtw_dequeue_cmd(struct __queue *queue)
{
	_irqL irqL;
	struct cmd_obj *obj;

	/* spin_lock_bh(&(queue->lock)); */
	spin_lock_irqsave(&queue->lock, irqL);
	if (list_empty(&(queue->queue)))
		obj = NULL;
	else {
		obj = LIST_CONTAINOR(get_next(&(queue->queue)), struct cmd_obj, list);
		list_del_init(&obj->list);
	}

	/* spin_unlock_bh(&(queue->lock)); */
	spin_unlock_irqrestore(&queue->lock, irqL);

	return obj;
}

void rtw_free_evt_priv(struct	evt_priv *pevtpriv)
{
	RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, ("rtw_free_evt_priv\n"));
	_rtw_free_evt_priv(pevtpriv);
}

void rtw_free_cmd_priv(struct	cmd_priv *pcmdpriv)
{
	RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, ("rtw_free_cmd_priv\n"));
	_rtw_free_cmd_priv(pcmdpriv);
}

int rtw_cmd_filter(struct cmd_priv *pcmdpriv, struct cmd_obj *cmd_obj);
int rtw_cmd_filter(struct cmd_priv *pcmdpriv, struct cmd_obj *cmd_obj)
{
	u8 bAllow = false; /* set to true to allow enqueuing cmd when hw_init_completed is false */

	if (cmd_obj->cmdcode == GEN_CMD_CODE(_SetChannelPlan))
		bAllow = true;

	if ((pcmdpriv->padapter->hw_init_completed == false && bAllow == false)
		|| atomic_read(&(pcmdpriv->cmdthd_running)) == false	/* com_thread not running */
	) {
		/* DBG_871X("%s:%s: drop cmdcode:%u, hw_init_completed:%u, cmdthd_running:%u\n", caller_func, __func__, */
		/* 	cmd_obj->cmdcode, */
		/* 	pcmdpriv->padapter->hw_init_completed, */
		/* 	pcmdpriv->cmdthd_running */
		/*  */

		return _FAIL;
	}
	return _SUCCESS;
}



int rtw_enqueue_cmd(struct cmd_priv *pcmdpriv, struct cmd_obj *cmd_obj)
{
	int res = _FAIL;
	struct adapter *padapter = pcmdpriv->padapter;

	if (cmd_obj == NULL) {
		goto exit;
	}

	cmd_obj->padapter = padapter;

	res = rtw_cmd_filter(pcmdpriv, cmd_obj);
	if (_FAIL == res) {
		rtw_free_cmd_obj(cmd_obj);
		goto exit;
	}

	res = _rtw_enqueue_cmd(&pcmdpriv->cmd_queue, cmd_obj);

	if (res == _SUCCESS)
		complete(&pcmdpriv->cmd_queue_comp);

exit:
	return res;
}

struct	cmd_obj	*rtw_dequeue_cmd(struct cmd_priv *pcmdpriv)
{
	return _rtw_dequeue_cmd(&pcmdpriv->cmd_queue);
}

void rtw_free_cmd_obj(struct cmd_obj *pcmd)
{
	if ((pcmd->cmdcode != _JoinBss_CMD_) &&
	    (pcmd->cmdcode != _CreateBss_CMD_)) {
		/* free parmbuf in cmd_obj */
		kfree(pcmd->parmbuf);
	}

	if (pcmd->rsp != NULL) {
		if (pcmd->rspsz != 0) {
			/* free rsp in cmd_obj */
			kfree(pcmd->rsp);
		}
	}

	/* free cmd_obj */
	kfree(pcmd);
}


void rtw_stop_cmd_thread(struct adapter *adapter)
{
	if (adapter->cmdThread &&
		atomic_read(&(adapter->cmdpriv.cmdthd_running)) == true &&
		adapter->cmdpriv.stop_req == 0) {
		adapter->cmdpriv.stop_req = 1;
		complete(&adapter->cmdpriv.cmd_queue_comp);
		wait_for_completion(&adapter->cmdpriv.terminate_cmdthread_comp);
	}
}

int rtw_cmd_thread(void *context)
{
	u8 ret;
	struct cmd_obj *pcmd;
	u8 *pcmdbuf;
	unsigned long cmd_start_time;
	unsigned long cmd_process_time;
	u8 (*cmd_hdl)(struct adapter *padapter, u8 *pbuf);
	void (*pcmd_callback)(struct adapter *dev, struct cmd_obj *pcmd);
	struct adapter *padapter = context;
	struct cmd_priv *pcmdpriv = &(padapter->cmdpriv);
	struct drvextra_cmd_parm *extra_parm = NULL;

	thread_enter("RTW_CMD_THREAD");

	pcmdbuf = pcmdpriv->cmd_buf;

	pcmdpriv->stop_req = 0;
	atomic_set(&(pcmdpriv->cmdthd_running), true);
	complete(&pcmdpriv->terminate_cmdthread_comp);

	RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, ("start r871x rtw_cmd_thread !!!!\n"));

	while (1) {
		if (wait_for_completion_interruptible(&pcmdpriv->cmd_queue_comp)) {
			DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" wait_for_completion_interruptible(&pcmdpriv->cmd_queue_comp) return != 0, break\n", FUNC_ADPT_ARG(padapter));
			break;
		}

		if ((padapter->bDriverStopped == true) || (padapter->bSurpriseRemoved == true)) {
			DBG_871X_LEVEL(_drv_always_, "%s: DriverStopped(%d) SurpriseRemoved(%d) break at line %d\n",
				__func__, padapter->bDriverStopped, padapter->bSurpriseRemoved, __LINE__);
			break;
		}

		if (pcmdpriv->stop_req) {
			DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" stop_req:%u, break\n", FUNC_ADPT_ARG(padapter), pcmdpriv->stop_req);
			break;
		}

		if (list_empty(&(pcmdpriv->cmd_queue.queue))) {
			/* DBG_871X("%s: cmd queue is empty!\n", __func__); */
			continue;
		}

		if (rtw_register_cmd_alive(padapter) != _SUCCESS) {
			RT_TRACE(_module_hal_xmit_c_, _drv_notice_,
					 ("%s: wait to leave LPS_LCLK\n", __func__));
			continue;
		}

_next:
		if ((padapter->bDriverStopped == true) || (padapter->bSurpriseRemoved == true)) {
			DBG_871X_LEVEL(_drv_always_, "%s: DriverStopped(%d) SurpriseRemoved(%d) break at line %d\n",
				__func__, padapter->bDriverStopped, padapter->bSurpriseRemoved, __LINE__);
			break;
		}

		pcmd = rtw_dequeue_cmd(pcmdpriv);
		if (!pcmd) {
			rtw_unregister_cmd_alive(padapter);
			continue;
		}

		cmd_start_time = jiffies;

		if (_FAIL == rtw_cmd_filter(pcmdpriv, pcmd)) {
			pcmd->res = H2C_DROPPED;
			goto post_process;
		}

		pcmdpriv->cmd_issued_cnt++;

		pcmd->cmdsz = _RND4((pcmd->cmdsz));/* _RND4 */

		memcpy(pcmdbuf, pcmd->parmbuf, pcmd->cmdsz);

		if (pcmd->cmdcode < ARRAY_SIZE(wlancmds)) {
			cmd_hdl = wlancmds[pcmd->cmdcode].h2cfuns;

			if (cmd_hdl) {
				ret = cmd_hdl(pcmd->padapter, pcmdbuf);
				pcmd->res = ret;
			}

			pcmdpriv->cmd_seq++;
		} else {
			pcmd->res = H2C_PARAMETERS_ERROR;
		}

		cmd_hdl = NULL;

post_process:

		if (mutex_lock_interruptible(&(pcmd->padapter->cmdpriv.sctx_mutex)) == 0) {
			if (pcmd->sctx) {
				DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" pcmd->sctx\n",
					       FUNC_ADPT_ARG(pcmd->padapter));

				if (pcmd->res == H2C_SUCCESS)
					rtw_sctx_done(&pcmd->sctx);
				else
					rtw_sctx_done_err(&pcmd->sctx, RTW_SCTX_DONE_CMD_ERROR);
			}
			mutex_unlock(&(pcmd->padapter->cmdpriv.sctx_mutex));
		}

		cmd_process_time = jiffies_to_msecs(jiffies - cmd_start_time);
		if (cmd_process_time > 1000) {
			DBG_871X(ADPT_FMT "cmd= %d process_time= %lu > 1 sec\n",
				 ADPT_ARG(pcmd->padapter), pcmd->cmdcode,
				 cmd_process_time);
		}

		/* call callback function for post-processed */
		if (pcmd->cmdcode < ARRAY_SIZE(rtw_cmd_callback)) {
			pcmd_callback = rtw_cmd_callback[pcmd->cmdcode].callback;
			if (pcmd_callback == NULL) {
				RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, ("mlme_cmd_hdl(): pcmd_callback = 0x%p, cmdcode = 0x%x\n", pcmd_callback, pcmd->cmdcode));
				rtw_free_cmd_obj(pcmd);
			} else {
				/* todo: !!! fill rsp_buf to pcmd->rsp if (pcmd->rsp!= NULL) */
				pcmd_callback(pcmd->padapter, pcmd);/* need consider that free cmd_obj in rtw_cmd_callback */
			}
		} else {
			RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("%s: cmdcode = 0x%x callback not defined!\n", __func__, pcmd->cmdcode));
			rtw_free_cmd_obj(pcmd);
		}

		flush_signals_thread();

		goto _next;

	}

	/*  free all cmd_obj resources */
	do {
		pcmd = rtw_dequeue_cmd(pcmdpriv);
		if (pcmd == NULL) {
			rtw_unregister_cmd_alive(padapter);
			break;
		}

		/* DBG_871X("%s: leaving... drop cmdcode:%u size:%d\n", __func__, pcmd->cmdcode, pcmd->cmdsz); */

		if (pcmd->cmdcode == GEN_CMD_CODE(_Set_Drv_Extra)) {
			extra_parm = (struct drvextra_cmd_parm *)pcmd->parmbuf;
			if (extra_parm->pbuf && extra_parm->size > 0) {
				kfree(extra_parm->pbuf);
			}
		}

		rtw_free_cmd_obj(pcmd);
	} while (1);

	complete(&pcmdpriv->terminate_cmdthread_comp);
	atomic_set(&(pcmdpriv->cmdthd_running), false);

	thread_exit();
}

/*
rtw_sitesurvey_cmd(~)
	### NOTE:#### (!!!!)
	MUST TAKE CARE THAT BEFORE CALLING THIS FUNC, YOU SHOULD HAVE LOCKED pmlmepriv->lock
*/
u8 rtw_sitesurvey_cmd(struct adapter  *padapter, struct ndis_802_11_ssid *ssid, int ssid_num,
	struct rtw_ieee80211_channel *ch, int ch_num)
{
	u8 res = _FAIL;
	struct cmd_obj		*ph2c;
	struct sitesurvey_parm	*psurveyPara;
	struct cmd_priv 	*pcmdpriv = &padapter->cmdpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (check_fwstate(pmlmepriv, _FW_LINKED) == true) {
		rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_SCAN, 1);
	}

	ph2c = rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL)
		return _FAIL;

	psurveyPara = rtw_zmalloc(sizeof(struct sitesurvey_parm));
	if (psurveyPara == NULL) {
		kfree(ph2c);
		return _FAIL;
	}

	rtw_free_network_queue(padapter, false);

	RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, ("%s: flush network queue\n", __func__));

	init_h2fwcmd_w_parm_no_rsp(ph2c, psurveyPara, GEN_CMD_CODE(_SiteSurvey));

	/* psurveyPara->bsslimit = 48; */
	psurveyPara->scan_mode = pmlmepriv->scan_mode;

	/* prepare ssid list */
	if (ssid) {
		int i;
		for (i = 0; i < ssid_num && i < RTW_SSID_SCAN_AMOUNT; i++) {
			if (ssid[i].SsidLength) {
				memcpy(&psurveyPara->ssid[i], &ssid[i], sizeof(struct ndis_802_11_ssid));
				psurveyPara->ssid_num++;

				DBG_871X(FUNC_ADPT_FMT" ssid:(%s, %d)\n", FUNC_ADPT_ARG(padapter),
					psurveyPara->ssid[i].Ssid, psurveyPara->ssid[i].SsidLength);
			}
		}
	}

	/* prepare channel list */
	if (ch) {
		int i;
		for (i = 0; i < ch_num && i < RTW_CHANNEL_SCAN_AMOUNT; i++) {
			if (ch[i].hw_value && !(ch[i].flags & RTW_IEEE80211_CHAN_DISABLED)) {
				memcpy(&psurveyPara->ch[i], &ch[i], sizeof(struct rtw_ieee80211_channel));
				psurveyPara->ch_num++;

				DBG_871X(FUNC_ADPT_FMT" ch:%u\n", FUNC_ADPT_ARG(padapter),
					psurveyPara->ch[i].hw_value);
			}
		}
	}

	set_fwstate(pmlmepriv, _FW_UNDER_SURVEY);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

	if (res == _SUCCESS) {

		pmlmepriv->scan_start_time = jiffies;
		_set_timer(&pmlmepriv->scan_to_timer, SCANNING_TIMEOUT);
	} else {
		_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);
	}
	return res;
}

u8 rtw_setdatarate_cmd(struct adapter *padapter, u8 *rateset)
{
	struct cmd_obj *ph2c;
	struct setdatarate_parm *pbsetdataratepara;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ph2c = rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pbsetdataratepara = rtw_zmalloc(sizeof(struct setdatarate_parm));
	if (pbsetdataratepara == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, pbsetdataratepara, GEN_CMD_CODE(_SetDataRate));
	pbsetdataratepara->mac_id = 5;
	memcpy(pbsetdataratepara->datarates, rateset, NumRates);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:
	return res;
}

void rtw_getbbrfreg_cmdrsp_callback(struct adapter *padapter,  struct cmd_obj *pcmd)
{
	/* rtw_free_cmd_obj(pcmd); */
	kfree(pcmd->parmbuf);
	kfree(pcmd);
}

u8 rtw_createbss_cmd(struct adapter  *padapter)
{
	struct cmd_obj *pcmd;
	struct cmd_priv 			*pcmdpriv = &padapter->cmdpriv;
	struct mlme_priv 		*pmlmepriv = &padapter->mlmepriv;
	struct wlan_bssid_ex		*pdev_network = &padapter->registrypriv.dev_network;
	u8 res = _SUCCESS;

	if (pmlmepriv->assoc_ssid.SsidLength == 0) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, (" createbss for Any SSid:%s\n", pmlmepriv->assoc_ssid.Ssid));
	} else {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, (" createbss for SSid:%s\n", pmlmepriv->assoc_ssid.Ssid));
	}

	pcmd = rtw_zmalloc(sizeof(struct cmd_obj));
	if (pcmd == NULL) {
		res = _FAIL;
		goto exit;
	}

	INIT_LIST_HEAD(&pcmd->list);
	pcmd->cmdcode = _CreateBss_CMD_;
	pcmd->parmbuf = (unsigned char *)pdev_network;
	pcmd->cmdsz = get_wlan_bssid_ex_sz((struct wlan_bssid_ex *)pdev_network);
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	pdev_network->Length = pcmd->cmdsz;

	res = rtw_enqueue_cmd(pcmdpriv, pcmd);

exit:
	return res;
}

int rtw_startbss_cmd(struct adapter  *padapter, int flags)
{
	struct cmd_obj *pcmd;
	struct cmd_priv  *pcmdpriv = &padapter->cmdpriv;
	struct submit_ctx sctx;
	int res = _SUCCESS;

	if (flags & RTW_CMDF_DIRECTLY) {
		/* no need to enqueue, do the cmd hdl directly and free cmd parameter */
		start_bss_network(padapter, (u8 *)&(padapter->mlmepriv.cur_network.network));
	} else {
		/* need enqueue, prepare cmd_obj and enqueue */
		pcmd = rtw_zmalloc(sizeof(struct cmd_obj));
		if (pcmd == NULL) {
			res = _FAIL;
			goto exit;
		}

		INIT_LIST_HEAD(&pcmd->list);
		pcmd->cmdcode = GEN_CMD_CODE(_CreateBss);
		pcmd->parmbuf = NULL;
		pcmd->cmdsz =  0;
		pcmd->rsp = NULL;
		pcmd->rspsz = 0;

		if (flags & RTW_CMDF_WAIT_ACK) {
			pcmd->sctx = &sctx;
			rtw_sctx_init(&sctx, 2000);
		}

		res = rtw_enqueue_cmd(pcmdpriv, pcmd);

		if (res == _SUCCESS && (flags & RTW_CMDF_WAIT_ACK)) {
			rtw_sctx_wait(&sctx, __func__);
			if (mutex_lock_interruptible(&pcmdpriv->sctx_mutex) == 0) {
				if (sctx.status == RTW_SCTX_SUBMITTED)
					pcmd->sctx = NULL;
				mutex_unlock(&pcmdpriv->sctx_mutex);
			}
		}
	}

exit:
	return res;
}

u8 rtw_joinbss_cmd(struct adapter  *padapter, struct wlan_network *pnetwork)
{
	u8 res = _SUCCESS;
	uint	t_len = 0;
	struct wlan_bssid_ex		*psecnetwork;
	struct cmd_obj		*pcmd;
	struct cmd_priv 	*pcmdpriv = &padapter->cmdpriv;
	struct mlme_priv 	*pmlmepriv = &padapter->mlmepriv;
	struct qos_priv 	*pqospriv = &pmlmepriv->qospriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct ht_priv 		*phtpriv = &pmlmepriv->htpriv;
	enum NDIS_802_11_NETWORK_INFRASTRUCTURE ndis_network_mode = pnetwork->network.InfrastructureMode;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	u32 tmp_len;
	u8 *ptmp = NULL;

	if (pmlmepriv->assoc_ssid.SsidLength == 0) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, ("+Join cmd: Any SSid\n"));
	} else {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_notice_, ("+Join cmd: SSid =[%s]\n", pmlmepriv->assoc_ssid.Ssid));
	}

	pcmd = rtw_zmalloc(sizeof(struct cmd_obj));
	if (pcmd == NULL) {
		res = _FAIL;
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("rtw_joinbss_cmd: memory allocate for cmd_obj fail!!!\n"));
		goto exit;
	}
	/* for IEs is fix buf size */
	t_len = sizeof(struct wlan_bssid_ex);


	/* for hidden ap to set fw_state here */
	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE|WIFI_ADHOC_STATE) != true) {
		switch (ndis_network_mode) {
		case Ndis802_11IBSS:
			set_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
			break;

		case Ndis802_11Infrastructure:
			set_fwstate(pmlmepriv, WIFI_STATION_STATE);
			break;

		case Ndis802_11APMode:
		case Ndis802_11AutoUnknown:
		case Ndis802_11InfrastructureMax:
			break;

		}
	}

	psecnetwork = (struct wlan_bssid_ex *)&psecuritypriv->sec_bss;

	memset(psecnetwork, 0, t_len);

	memcpy(psecnetwork, &pnetwork->network, get_wlan_bssid_ex_sz(&pnetwork->network));

	psecuritypriv->authenticator_ie[0] = (unsigned char)psecnetwork->IELength;

	if ((psecnetwork->IELength-12) < (256-1)) {
		memcpy(&psecuritypriv->authenticator_ie[1], &psecnetwork->IEs[12], psecnetwork->IELength-12);
	} else {
		memcpy(&psecuritypriv->authenticator_ie[1], &psecnetwork->IEs[12], (256-1));
	}

	psecnetwork->IELength = 0;
	/*  Added by Albert 2009/02/18 */
	/*  If the driver wants to use the bssid to create the connection. */
	/*  If not,  we have to copy the connecting AP's MAC address to it so that */
	/*  the driver just has the bssid information for PMKIDList searching. */

	if (pmlmepriv->assoc_by_bssid == false) {
		memcpy(&pmlmepriv->assoc_bssid[0], &pnetwork->network.MacAddress[0], ETH_ALEN);
	}

	psecnetwork->IELength = rtw_restruct_sec_ie(padapter, &pnetwork->network.IEs[0], &psecnetwork->IEs[0], pnetwork->network.IELength);


	pqospriv->qos_option = 0;

	if (pregistrypriv->wmm_enable) {
		tmp_len = rtw_restruct_wmm_ie(padapter, &pnetwork->network.IEs[0], &psecnetwork->IEs[0], pnetwork->network.IELength, psecnetwork->IELength);

		if (psecnetwork->IELength != tmp_len) {
			psecnetwork->IELength = tmp_len;
			pqospriv->qos_option = 1; /* There is WMM IE in this corresp. beacon */
		} else {
			pqospriv->qos_option = 0;/* There is no WMM IE in this corresp. beacon */
		}
	}

	phtpriv->ht_option = false;
	ptmp = rtw_get_ie(&pnetwork->network.IEs[12], _HT_CAPABILITY_IE_, &tmp_len, pnetwork->network.IELength-12);
	if (pregistrypriv->ht_enable && ptmp && tmp_len > 0) {
		/* 	Added by Albert 2010/06/23 */
		/* 	For the WEP mode, we will use the bg mode to do the connection to avoid some IOT issue. */
		/* 	Especially for Realtek 8192u SoftAP. */
		if ((padapter->securitypriv.dot11PrivacyAlgrthm != _WEP40_) &&
			(padapter->securitypriv.dot11PrivacyAlgrthm != _WEP104_) &&
			(padapter->securitypriv.dot11PrivacyAlgrthm != _TKIP_)) {
			rtw_ht_use_default_setting(padapter);

			rtw_build_wmm_ie_ht(padapter, &psecnetwork->IEs[12], &psecnetwork->IELength);

			/* rtw_restructure_ht_ie */
			rtw_restructure_ht_ie(padapter, &pnetwork->network.IEs[12], &psecnetwork->IEs[0],
									pnetwork->network.IELength-12, &psecnetwork->IELength,
									pnetwork->network.Configuration.DSConfig);
		}
	}

	rtw_append_exented_cap(padapter, &psecnetwork->IEs[0], &psecnetwork->IELength);

	pmlmeinfo->assoc_AP_vendor = check_assoc_AP(pnetwork->network.IEs, pnetwork->network.IELength);

	pcmd->cmdsz = get_wlan_bssid_ex_sz(psecnetwork);/* get cmdsz before endian conversion */

	INIT_LIST_HEAD(&pcmd->list);
	pcmd->cmdcode = _JoinBss_CMD_;/* GEN_CMD_CODE(_JoinBss) */
	pcmd->parmbuf = (unsigned char *)psecnetwork;
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	res = rtw_enqueue_cmd(pcmdpriv, pcmd);

exit:
	return res;
}

u8 rtw_disassoc_cmd(struct adapter *padapter, u32 deauth_timeout_ms, bool enqueue) /* for sta_mode */
{
	struct cmd_obj *cmdobj = NULL;
	struct disconnect_parm *param = NULL;
	struct cmd_priv *cmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	RT_TRACE(_module_rtl871x_cmd_c_, _drv_notice_, ("+rtw_disassoc_cmd\n"));

	/* prepare cmd parameter */
	param = rtw_zmalloc(sizeof(*param));
	if (param == NULL) {
		res = _FAIL;
		goto exit;
	}
	param->deauth_timeout_ms = deauth_timeout_ms;

	if (enqueue) {
		/* need enqueue, prepare cmd_obj and enqueue */
		cmdobj = rtw_zmalloc(sizeof(*cmdobj));
		if (cmdobj == NULL) {
			res = _FAIL;
			kfree(param);
			goto exit;
		}
		init_h2fwcmd_w_parm_no_rsp(cmdobj, param, _DisConnect_CMD_);
		res = rtw_enqueue_cmd(cmdpriv, cmdobj);
	} else {
		/* no need to enqueue, do the cmd hdl directly and free cmd parameter */
		if (H2C_SUCCESS != disconnect_hdl(padapter, (u8 *)param))
			res = _FAIL;
		kfree(param);
	}

exit:
	return res;
}

u8 rtw_setopmode_cmd(struct adapter  *padapter, enum NDIS_802_11_NETWORK_INFRASTRUCTURE networktype, bool enqueue)
{
	struct	cmd_obj *ph2c;
	struct	setopmode_parm *psetop;

	struct	cmd_priv   *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	psetop = rtw_zmalloc(sizeof(struct setopmode_parm));

	if (psetop == NULL) {
		res = _FAIL;
		goto exit;
	}
	psetop->mode = (u8)networktype;

	if (enqueue) {
		ph2c = rtw_zmalloc(sizeof(struct cmd_obj));
		if (ph2c == NULL) {
			kfree(psetop);
			res = _FAIL;
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(ph2c, psetop, _SetOpMode_CMD_);
		res = rtw_enqueue_cmd(pcmdpriv, ph2c);
	} else {
		setopmode_hdl(padapter, (u8 *)psetop);
		kfree(psetop);
	}
exit:
	return res;
}

u8 rtw_setstakey_cmd(struct adapter *padapter, struct sta_info *sta, u8 unicast_key, bool enqueue)
{
	struct cmd_obj *ph2c;
	struct set_stakey_parm	*psetstakey_para;
	struct cmd_priv 			*pcmdpriv = &padapter->cmdpriv;
	struct set_stakey_rsp		*psetstakey_rsp = NULL;

	struct mlme_priv 		*pmlmepriv = &padapter->mlmepriv;
	struct security_priv 	*psecuritypriv = &padapter->securitypriv;
	u8 res = _SUCCESS;

	psetstakey_para = rtw_zmalloc(sizeof(struct set_stakey_parm));
	if (psetstakey_para == NULL) {
		res = _FAIL;
		goto exit;
	}

	memcpy(psetstakey_para->addr, sta->hwaddr, ETH_ALEN);

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		psetstakey_para->algorithm = (unsigned char) psecuritypriv->dot11PrivacyAlgrthm;
	} else {
		GET_ENCRY_ALGO(psecuritypriv, sta, psetstakey_para->algorithm, false);
	}

	if (unicast_key == true) {
		memcpy(&psetstakey_para->key, &sta->dot118021x_UncstKey, 16);
	} else {
		memcpy(&psetstakey_para->key, &psecuritypriv->dot118021XGrpKey[psecuritypriv->dot118021XGrpKeyid].skey, 16);
	}

	/* jeff: set this because at least sw key is ready */
	padapter->securitypriv.busetkipkey = true;

	if (enqueue) {
		ph2c = rtw_zmalloc(sizeof(struct cmd_obj));
		if (ph2c == NULL) {
			kfree(psetstakey_para);
			res = _FAIL;
			goto exit;
		}

		psetstakey_rsp = rtw_zmalloc(sizeof(struct set_stakey_rsp));
		if (psetstakey_rsp == NULL) {
			kfree(ph2c);
			kfree(psetstakey_para);
			res = _FAIL;
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(ph2c, psetstakey_para, _SetStaKey_CMD_);
		ph2c->rsp = (u8 *) psetstakey_rsp;
		ph2c->rspsz = sizeof(struct set_stakey_rsp);
		res = rtw_enqueue_cmd(pcmdpriv, ph2c);
	} else {
		set_stakey_hdl(padapter, (u8 *)psetstakey_para);
		kfree(psetstakey_para);
	}
exit:
	return res;
}

u8 rtw_clearstakey_cmd(struct adapter *padapter, struct sta_info *sta, u8 enqueue)
{
	struct cmd_obj *ph2c;
	struct set_stakey_parm	*psetstakey_para;
	struct cmd_priv 			*pcmdpriv = &padapter->cmdpriv;
	struct set_stakey_rsp		*psetstakey_rsp = NULL;
	s16 cam_id = 0;
	u8 res = _SUCCESS;

	if (!enqueue) {
		while ((cam_id = rtw_camid_search(padapter, sta->hwaddr, -1)) >= 0) {
			DBG_871X_LEVEL(_drv_always_, "clear key for addr:"MAC_FMT", camid:%d\n", MAC_ARG(sta->hwaddr), cam_id);
			clear_cam_entry(padapter, cam_id);
			rtw_camid_free(padapter, cam_id);
		}
	} else {
		ph2c = rtw_zmalloc(sizeof(struct cmd_obj));
		if (ph2c == NULL) {
			res = _FAIL;
			goto exit;
		}

		psetstakey_para = rtw_zmalloc(sizeof(struct set_stakey_parm));
		if (psetstakey_para == NULL) {
			kfree(ph2c);
			res = _FAIL;
			goto exit;
		}

		psetstakey_rsp = rtw_zmalloc(sizeof(struct set_stakey_rsp));
		if (psetstakey_rsp == NULL) {
			kfree(ph2c);
			kfree(psetstakey_para);
			res = _FAIL;
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(ph2c, psetstakey_para, _SetStaKey_CMD_);
		ph2c->rsp = (u8 *) psetstakey_rsp;
		ph2c->rspsz = sizeof(struct set_stakey_rsp);

		memcpy(psetstakey_para->addr, sta->hwaddr, ETH_ALEN);

		psetstakey_para->algorithm = _NO_PRIVACY_;

		res = rtw_enqueue_cmd(pcmdpriv, ph2c);

	}

exit:
	return res;
}

u8 rtw_addbareq_cmd(struct adapter *padapter, u8 tid, u8 *addr)
{
	struct cmd_priv 	*pcmdpriv = &padapter->cmdpriv;
	struct cmd_obj *ph2c;
	struct addBaReq_parm	*paddbareq_parm;

	u8 res = _SUCCESS;

	ph2c = rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	paddbareq_parm = rtw_zmalloc(sizeof(struct addBaReq_parm));
	if (paddbareq_parm == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	paddbareq_parm->tid = tid;
	memcpy(paddbareq_parm->addr, addr, ETH_ALEN);

	init_h2fwcmd_w_parm_no_rsp(ph2c, paddbareq_parm, GEN_CMD_CODE(_AddBAReq));

	/* DBG_871X("rtw_addbareq_cmd, tid =%d\n", tid); */

	/* rtw_enqueue_cmd(pcmdpriv, ph2c); */
	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:
	return res;
}
/* add for CONFIG_IEEE80211W, none 11w can use it */
u8 rtw_reset_securitypriv_cmd(struct adapter *padapter)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm  *pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ph2c = rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = RESET_SECURITYPRIV;
	pdrvextra_cmd_parm->type = 0;
	pdrvextra_cmd_parm->size = 0;
	pdrvextra_cmd_parm->pbuf = NULL;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));


	/* rtw_enqueue_cmd(pcmdpriv, ph2c); */
	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:
	return res;
}

u8 rtw_free_assoc_resources_cmd(struct adapter *padapter)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm  *pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ph2c = rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = FREE_ASSOC_RESOURCES;
	pdrvextra_cmd_parm->type = 0;
	pdrvextra_cmd_parm->size = 0;
	pdrvextra_cmd_parm->pbuf = NULL;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));


	/* rtw_enqueue_cmd(pcmdpriv, ph2c); */
	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:
	return res;
}

u8 rtw_dynamic_chk_wk_cmd(struct adapter *padapter)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm  *pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	/* only  primary padapter does this cmd */
	ph2c = rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = DYNAMIC_CHK_WK_CID;
	pdrvextra_cmd_parm->type = 0;
	pdrvextra_cmd_parm->size = 0;
	pdrvextra_cmd_parm->pbuf = NULL;
	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));


	/* rtw_enqueue_cmd(pcmdpriv, ph2c); */
	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:
	return res;
}

u8 rtw_set_chplan_cmd(struct adapter *padapter, u8 chplan, u8 enqueue, u8 swconfig)
{
	struct	cmd_obj *pcmdobj;
	struct	SetChannelPlan_param *setChannelPlan_param;
	struct	cmd_priv   *pcmdpriv = &padapter->cmdpriv;

	u8 res = _SUCCESS;

	RT_TRACE(_module_rtl871x_cmd_c_, _drv_notice_, ("+rtw_set_chplan_cmd\n"));

	/*  check if allow software config */
	if (swconfig && rtw_hal_is_disable_sw_channel_plan(padapter) == true) {
		res = _FAIL;
		goto exit;
	}

	/* check input parameter */
	if (!rtw_is_channel_plan_valid(chplan)) {
		res = _FAIL;
		goto exit;
	}

	/* prepare cmd parameter */
	setChannelPlan_param = rtw_zmalloc(sizeof(struct SetChannelPlan_param));
	if (setChannelPlan_param == NULL) {
		res = _FAIL;
		goto exit;
	}
	setChannelPlan_param->channel_plan = chplan;

	if (enqueue) {
		/* need enqueue, prepare cmd_obj and enqueue */
		pcmdobj = rtw_zmalloc(sizeof(struct cmd_obj));
		if (pcmdobj == NULL) {
			kfree(setChannelPlan_param);
			res = _FAIL;
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(pcmdobj, setChannelPlan_param, GEN_CMD_CODE(_SetChannelPlan));
		res = rtw_enqueue_cmd(pcmdpriv, pcmdobj);
	} else {
		/* no need to enqueue, do the cmd hdl directly and free cmd parameter */
		if (H2C_SUCCESS != set_chplan_hdl(padapter, (unsigned char *)setChannelPlan_param))
			res = _FAIL;

		kfree(setChannelPlan_param);
	}

	/* do something based on res... */
	if (res == _SUCCESS)
		padapter->mlmepriv.ChannelPlan = chplan;

exit:
	return res;
}

static void collect_traffic_statistics(struct adapter *padapter)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);

	/*  Tx */
	pdvobjpriv->traffic_stat.tx_bytes = padapter->xmitpriv.tx_bytes;
	pdvobjpriv->traffic_stat.tx_pkts = padapter->xmitpriv.tx_pkts;
	pdvobjpriv->traffic_stat.tx_drop = padapter->xmitpriv.tx_drop;

	/*  Rx */
	pdvobjpriv->traffic_stat.rx_bytes = padapter->recvpriv.rx_bytes;
	pdvobjpriv->traffic_stat.rx_pkts = padapter->recvpriv.rx_pkts;
	pdvobjpriv->traffic_stat.rx_drop = padapter->recvpriv.rx_drop;

	/*  Calculate throughput in last interval */
	pdvobjpriv->traffic_stat.cur_tx_bytes = pdvobjpriv->traffic_stat.tx_bytes - pdvobjpriv->traffic_stat.last_tx_bytes;
	pdvobjpriv->traffic_stat.cur_rx_bytes = pdvobjpriv->traffic_stat.rx_bytes - pdvobjpriv->traffic_stat.last_rx_bytes;
	pdvobjpriv->traffic_stat.last_tx_bytes = pdvobjpriv->traffic_stat.tx_bytes;
	pdvobjpriv->traffic_stat.last_rx_bytes = pdvobjpriv->traffic_stat.rx_bytes;

	pdvobjpriv->traffic_stat.cur_tx_tp = (u32)(pdvobjpriv->traffic_stat.cur_tx_bytes * 8/2/1024/1024);
	pdvobjpriv->traffic_stat.cur_rx_tp = (u32)(pdvobjpriv->traffic_stat.cur_rx_bytes * 8/2/1024/1024);
}

u8 traffic_status_watchdog(struct adapter *padapter, u8 from_timer)
{
	u8 bEnterPS = false;
	u16 BusyThresholdHigh = 25;
	u16 BusyThresholdLow = 10;
	u16 BusyThreshold = BusyThresholdHigh;
	u8 bBusyTraffic = false, bTxBusyTraffic = false, bRxBusyTraffic = false;
	u8 bHigherBusyTraffic = false, bHigherBusyRxTraffic = false, bHigherBusyTxTraffic = false;

	struct mlme_priv 	*pmlmepriv = &(padapter->mlmepriv);

	collect_traffic_statistics(padapter);

	/*  */
	/*  Determine if our traffic is busy now */
	/*  */
	if ((check_fwstate(pmlmepriv, _FW_LINKED) == true)
		/*&& !MgntInitAdapterInProgress(pMgntInfo)*/) {
		/*  if we raise bBusyTraffic in last watchdog, using lower threshold. */
		if (pmlmepriv->LinkDetectInfo.bBusyTraffic)
				BusyThreshold = BusyThresholdLow;

		if (pmlmepriv->LinkDetectInfo.NumRxOkInPeriod > BusyThreshold ||
			pmlmepriv->LinkDetectInfo.NumTxOkInPeriod > BusyThreshold) {
			bBusyTraffic = true;

			if (pmlmepriv->LinkDetectInfo.NumRxOkInPeriod > pmlmepriv->LinkDetectInfo.NumTxOkInPeriod)
				bRxBusyTraffic = true;
			else
				bTxBusyTraffic = true;
		}

		/*  Higher Tx/Rx data. */
		if (pmlmepriv->LinkDetectInfo.NumRxOkInPeriod > 4000 ||
			pmlmepriv->LinkDetectInfo.NumTxOkInPeriod > 4000) {
			bHigherBusyTraffic = true;

			if (pmlmepriv->LinkDetectInfo.NumRxOkInPeriod > pmlmepriv->LinkDetectInfo.NumTxOkInPeriod)
				bHigherBusyRxTraffic = true;
			else
				bHigherBusyTxTraffic = true;
		}

		/*  check traffic for  powersaving. */
		if (((pmlmepriv->LinkDetectInfo.NumRxUnicastOkInPeriod + pmlmepriv->LinkDetectInfo.NumTxOkInPeriod) > 8) ||
			(pmlmepriv->LinkDetectInfo.NumRxUnicastOkInPeriod > 2)) {
			/* DBG_871X("(-)Tx = %d, Rx = %d\n", pmlmepriv->LinkDetectInfo.NumTxOkInPeriod, pmlmepriv->LinkDetectInfo.NumRxUnicastOkInPeriod); */
			bEnterPS = false;

			if (bBusyTraffic == true) {
				if (pmlmepriv->LinkDetectInfo.TrafficTransitionCount <= 4)
					pmlmepriv->LinkDetectInfo.TrafficTransitionCount = 4;

				pmlmepriv->LinkDetectInfo.TrafficTransitionCount++;

				/* DBG_871X("Set TrafficTransitionCount to %d\n", pmlmepriv->LinkDetectInfo.TrafficTransitionCount); */

				if (pmlmepriv->LinkDetectInfo.TrafficTransitionCount > 30/*TrafficTransitionLevel*/) {
					pmlmepriv->LinkDetectInfo.TrafficTransitionCount = 30;
				}
			}
		} else {
			/* DBG_871X("(+)Tx = %d, Rx = %d\n", pmlmepriv->LinkDetectInfo.NumTxOkInPeriod, pmlmepriv->LinkDetectInfo.NumRxUnicastOkInPeriod); */

			if (pmlmepriv->LinkDetectInfo.TrafficTransitionCount >= 2)
				pmlmepriv->LinkDetectInfo.TrafficTransitionCount -= 2;
			else
				pmlmepriv->LinkDetectInfo.TrafficTransitionCount = 0;

			if (pmlmepriv->LinkDetectInfo.TrafficTransitionCount == 0)
				bEnterPS = true;
		}

		/*  LeisurePS only work in infra mode. */
		if (bEnterPS) {
			if (!from_timer)
				LPS_Enter(padapter, "TRAFFIC_IDLE");
		} else {
			if (!from_timer)
				LPS_Leave(padapter, "TRAFFIC_BUSY");
			else
				rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_TRAFFIC_BUSY, 1);
		}
	} else {
		struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
		int n_assoc_iface = 0;

		if (check_fwstate(&(dvobj->padapters->mlmepriv), WIFI_ASOC_STATE))
			n_assoc_iface++;

		if (!from_timer && n_assoc_iface == 0)
			LPS_Leave(padapter, "NON_LINKED");
	}

	pmlmepriv->LinkDetectInfo.NumRxOkInPeriod = 0;
	pmlmepriv->LinkDetectInfo.NumTxOkInPeriod = 0;
	pmlmepriv->LinkDetectInfo.NumRxUnicastOkInPeriod = 0;
	pmlmepriv->LinkDetectInfo.bBusyTraffic = bBusyTraffic;
	pmlmepriv->LinkDetectInfo.bTxBusyTraffic = bTxBusyTraffic;
	pmlmepriv->LinkDetectInfo.bRxBusyTraffic = bRxBusyTraffic;
	pmlmepriv->LinkDetectInfo.bHigherBusyTraffic = bHigherBusyTraffic;
	pmlmepriv->LinkDetectInfo.bHigherBusyRxTraffic = bHigherBusyRxTraffic;
	pmlmepriv->LinkDetectInfo.bHigherBusyTxTraffic = bHigherBusyTxTraffic;

	return bEnterPS;

}

static void dynamic_chk_wk_hdl(struct adapter *padapter)
{
	struct mlme_priv *pmlmepriv;
	pmlmepriv = &(padapter->mlmepriv);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) {
		expire_timeout_chk(padapter);
	}

	/* for debug purpose */
	_linked_info_dump(padapter);


	/* if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING|_FW_UNDER_SURVEY) ==false) */
	{
		linked_status_chk(padapter);
		traffic_status_watchdog(padapter, 0);
	}

	rtw_hal_dm_watchdog(padapter);

	/* check_hw_pbc(padapter, pdrvextra_cmd->pbuf, pdrvextra_cmd->type); */

	/*  */
	/*  BT-Coexist */
	/*  */
	hal_btcoex_Handler(padapter);


	/* always call rtw_ps_processor() at last one. */
	if (is_primary_adapter(padapter))
		rtw_ps_processor(padapter);
}

void lps_ctrl_wk_hdl(struct adapter *padapter, u8 lps_ctrl_type);
void lps_ctrl_wk_hdl(struct adapter *padapter, u8 lps_ctrl_type)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	u8 mstatus;

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true)
		|| (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true)) {
		return;
	}

	switch (lps_ctrl_type) {
	case LPS_CTRL_SCAN:
		/* DBG_871X("LPS_CTRL_SCAN\n"); */
		hal_btcoex_ScanNotify(padapter, true);

		if (check_fwstate(pmlmepriv, _FW_LINKED) == true) {
			/*  connect */
			LPS_Leave(padapter, "LPS_CTRL_SCAN");
		}
		break;
	case LPS_CTRL_JOINBSS:
		/* DBG_871X("LPS_CTRL_JOINBSS\n"); */
		LPS_Leave(padapter, "LPS_CTRL_JOINBSS");
		break;
	case LPS_CTRL_CONNECT:
		/* DBG_871X("LPS_CTRL_CONNECT\n"); */
		mstatus = 1;/* connect */
		/*  Reset LPS Setting */
		pwrpriv->LpsIdleCount = 0;
		rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_JOINBSSRPT, (u8 *)(&mstatus));
		rtw_btcoex_MediaStatusNotify(padapter, mstatus);
		break;
	case LPS_CTRL_DISCONNECT:
		/* DBG_871X("LPS_CTRL_DISCONNECT\n"); */
		mstatus = 0;/* disconnect */
		rtw_btcoex_MediaStatusNotify(padapter, mstatus);
		LPS_Leave(padapter, "LPS_CTRL_DISCONNECT");
		rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_JOINBSSRPT, (u8 *)(&mstatus));
		break;
	case LPS_CTRL_SPECIAL_PACKET:
		/* DBG_871X("LPS_CTRL_SPECIAL_PACKET\n"); */
		pwrpriv->DelayLPSLastTimeStamp = jiffies;
		hal_btcoex_SpecialPacketNotify(padapter, PACKET_DHCP);
		LPS_Leave(padapter, "LPS_CTRL_SPECIAL_PACKET");
		break;
	case LPS_CTRL_LEAVE:
		/* DBG_871X("LPS_CTRL_LEAVE\n"); */
		LPS_Leave(padapter, "LPS_CTRL_LEAVE");
		break;
	case LPS_CTRL_TRAFFIC_BUSY:
		LPS_Leave(padapter, "LPS_CTRL_TRAFFIC_BUSY");
	default:
		break;
	}
}

u8 rtw_lps_ctrl_wk_cmd(struct adapter *padapter, u8 lps_ctrl_type, u8 enqueue)
{
	struct cmd_obj	*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	/* struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter); */
	u8 res = _SUCCESS;

	/* if (!pwrctrlpriv->bLeisurePs) */
	/* 	return res; */

	if (enqueue) {
		ph2c = rtw_zmalloc(sizeof(struct cmd_obj));
		if (ph2c == NULL) {
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm = rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
		if (pdrvextra_cmd_parm == NULL) {
			kfree(ph2c);
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm->ec_id = LPS_CTRL_WK_CID;
		pdrvextra_cmd_parm->type = lps_ctrl_type;
		pdrvextra_cmd_parm->size = 0;
		pdrvextra_cmd_parm->pbuf = NULL;

		init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

		res = rtw_enqueue_cmd(pcmdpriv, ph2c);
	} else {
		lps_ctrl_wk_hdl(padapter, lps_ctrl_type);
	}

exit:
	return res;
}

static void rtw_dm_in_lps_hdl(struct adapter *padapter)
{
	rtw_hal_set_hwreg(padapter, HW_VAR_DM_IN_LPS, NULL);
}

u8 rtw_dm_in_lps_wk_cmd(struct adapter *padapter)
{
	struct cmd_obj	*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;


	ph2c = rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = DM_IN_LPS_WK_CID;
	pdrvextra_cmd_parm->type = 0;
	pdrvextra_cmd_parm->size = 0;
	pdrvextra_cmd_parm->pbuf = NULL;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;

}

static void rtw_lps_change_dtim_hdl(struct adapter *padapter, u8 dtim)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);

	if (dtim <= 0 || dtim > 16)
		return;

	if (hal_btcoex_IsBtControlLps(padapter) == true)
		return;

	mutex_lock(&pwrpriv->lock);

	if (pwrpriv->dtim != dtim) {
		DBG_871X("change DTIM from %d to %d, bFwCurrentInPSMode =%d, ps_mode =%d\n", pwrpriv->dtim, dtim,
			pwrpriv->bFwCurrentInPSMode, pwrpriv->pwr_mode);

		pwrpriv->dtim = dtim;
	}

	if ((pwrpriv->bFwCurrentInPSMode == true) && (pwrpriv->pwr_mode > PS_MODE_ACTIVE)) {
		u8 ps_mode = pwrpriv->pwr_mode;

		/* DBG_871X("change DTIM from %d to %d, ps_mode =%d\n", pwrpriv->dtim, dtim, ps_mode); */

		rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_PWRMODE, (u8 *)(&ps_mode));
	}

	mutex_unlock(&pwrpriv->lock);
}

static void rtw_dm_ra_mask_hdl(struct adapter *padapter, struct sta_info *psta)
{
	if (psta) {
		set_sta_rate(padapter, psta);
	}
}

u8 rtw_dm_ra_mask_wk_cmd(struct adapter *padapter, u8 *psta)
{
	struct cmd_obj	*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;


	ph2c = rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = DM_RA_MSK_WK_CID;
	pdrvextra_cmd_parm->type = 0;
	pdrvextra_cmd_parm->size = 0;
	pdrvextra_cmd_parm->pbuf = psta;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;

}

u8 rtw_ps_cmd(struct adapter *padapter)
{
	struct cmd_obj		*ppscmd;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ppscmd = rtw_zmalloc(sizeof(struct cmd_obj));
	if (ppscmd == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		kfree(ppscmd);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = POWER_SAVING_CTRL_WK_CID;
	pdrvextra_cmd_parm->type = 0;
	pdrvextra_cmd_parm->size = 0;
	pdrvextra_cmd_parm->pbuf = NULL;
	init_h2fwcmd_w_parm_no_rsp(ppscmd, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

	res = rtw_enqueue_cmd(pcmdpriv, ppscmd);

exit:
	return res;
}

u32 g_wait_hiq_empty;

static void rtw_chk_hi_queue_hdl(struct adapter *padapter)
{
	struct sta_info *psta_bmc;
	struct sta_priv *pstapriv = &padapter->stapriv;
	unsigned long start = jiffies;
	u8 empty = false;

	psta_bmc = rtw_get_bcmc_stainfo(padapter);
	if (!psta_bmc)
		return;

	rtw_hal_get_hwreg(padapter, HW_VAR_CHK_HI_QUEUE_EMPTY, &empty);

	while (false == empty && jiffies_to_msecs(jiffies - start) < g_wait_hiq_empty) {
		msleep(100);
		rtw_hal_get_hwreg(padapter, HW_VAR_CHK_HI_QUEUE_EMPTY, &empty);
	}

	if (psta_bmc->sleepq_len == 0) {
		if (empty == _SUCCESS) {
			bool update_tim = false;

			if (pstapriv->tim_bitmap & BIT(0))
				update_tim = true;

			pstapriv->tim_bitmap &= ~BIT(0);
			pstapriv->sta_dz_bitmap &= ~BIT(0);

			if (update_tim)
				update_beacon(padapter, _TIM_IE_, NULL, true);
		} else {/* re check again */
			rtw_chk_hi_queue_cmd(padapter);
		}

	}

}

u8 rtw_chk_hi_queue_cmd(struct adapter *padapter)
{
	struct cmd_obj	*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ph2c = rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = CHECK_HIQ_WK_CID;
	pdrvextra_cmd_parm->type = 0;
	pdrvextra_cmd_parm->size = 0;
	pdrvextra_cmd_parm->pbuf = NULL;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;

}

struct btinfo {
	u8 cid;
	u8 len;

	u8 bConnection:1;
	u8 bSCOeSCO:1;
	u8 bInQPage:1;
	u8 bACLBusy:1;
	u8 bSCOBusy:1;
	u8 bHID:1;
	u8 bA2DP:1;
	u8 bFTP:1;

	u8 retry_cnt:4;
	u8 rsvd_34:1;
	u8 rsvd_35:1;
	u8 rsvd_36:1;
	u8 rsvd_37:1;

	u8 rssi;

	u8 rsvd_50:1;
	u8 rsvd_51:1;
	u8 rsvd_52:1;
	u8 rsvd_53:1;
	u8 rsvd_54:1;
	u8 rsvd_55:1;
	u8 eSCO_SCO:1;
	u8 Master_Slave:1;

	u8 rsvd_6;
	u8 rsvd_7;
};

static void rtw_btinfo_hdl(struct adapter *adapter, u8 *buf, u16 buf_len)
{
	#define BTINFO_WIFI_FETCH 0x23
	#define BTINFO_BT_AUTO_RPT 0x27
	struct btinfo *info = (struct btinfo *)buf;
	u8 cmd_idx;
	u8 len;

	cmd_idx = info->cid;

	if (info->len > buf_len-2) {
		rtw_warn_on(1);
		len = buf_len-2;
	} else {
		len = info->len;
	}

	/* transform BT-FW btinfo to WiFI-FW C2H format and notify */
	if (cmd_idx == BTINFO_WIFI_FETCH)
		buf[1] = 0;
	else if (cmd_idx == BTINFO_BT_AUTO_RPT)
		buf[1] = 2;
	hal_btcoex_BtInfoNotify(adapter, len+1, &buf[1]);
}

u8 rtw_c2h_packet_wk_cmd(struct adapter *padapter, u8 *pbuf, u16 length)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ph2c = rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = C2H_WK_CID;
	pdrvextra_cmd_parm->type = 0;
	pdrvextra_cmd_parm->size = length;
	pdrvextra_cmd_parm->pbuf = pbuf;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:
	return res;
}

/* dont call R/W in this function, beucase SDIO interrupt have claim host */
/* or deadlock will happen and cause special-systemserver-died in android */
u8 rtw_c2h_wk_cmd(struct adapter *padapter, u8 *c2h_evt)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ph2c = rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = C2H_WK_CID;
	pdrvextra_cmd_parm->type = 0;
	pdrvextra_cmd_parm->size =  c2h_evt?16:0;
	pdrvextra_cmd_parm->pbuf = c2h_evt;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;
}

static void c2h_wk_callback(_workitem *work)
{
	struct evt_priv *evtpriv = container_of(work, struct evt_priv, c2h_wk);
	struct adapter *adapter = container_of(evtpriv, struct adapter, evtpriv);
	u8 *c2h_evt;
	c2h_id_filter ccx_id_filter = rtw_hal_c2h_id_filter_ccx(adapter);

	evtpriv->c2h_wk_alive = true;

	while (!rtw_cbuf_empty(evtpriv->c2h_queue)) {
		c2h_evt = (u8 *)rtw_cbuf_pop(evtpriv->c2h_queue);
		if (c2h_evt != NULL) {
			/* This C2H event is read, clear it */
			c2h_evt_clear(adapter);
		} else {
			c2h_evt = rtw_malloc(16);
			if (c2h_evt != NULL) {
				/* This C2H event is not read, read & clear now */
				if (c2h_evt_read_88xx(adapter, c2h_evt) != _SUCCESS) {
					kfree(c2h_evt);
					continue;
				}
			}
		}

		/* Special pointer to trigger c2h_evt_clear only */
		if ((void *)c2h_evt == (void *)evtpriv)
			continue;

		if (!rtw_hal_c2h_valid(adapter, c2h_evt)) {
			kfree(c2h_evt);
			continue;
		}

		if (ccx_id_filter(c2h_evt) == true) {
			/* Handle CCX report here */
			rtw_hal_c2h_handler(adapter, c2h_evt);
			kfree(c2h_evt);
		} else {
			/* Enqueue into cmd_thread for others */
			rtw_c2h_wk_cmd(adapter, c2h_evt);
		}
	}

	evtpriv->c2h_wk_alive = false;
}

u8 rtw_drvextra_cmd_hdl(struct adapter *padapter, unsigned char *pbuf)
{
	struct drvextra_cmd_parm *pdrvextra_cmd;

	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	pdrvextra_cmd = (struct drvextra_cmd_parm *)pbuf;

	switch (pdrvextra_cmd->ec_id) {
	case DYNAMIC_CHK_WK_CID:/* only  primary padapter go to this cmd, but execute dynamic_chk_wk_hdl() for two interfaces */
		dynamic_chk_wk_hdl(padapter);
		break;
	case POWER_SAVING_CTRL_WK_CID:
		rtw_ps_processor(padapter);
		break;
	case LPS_CTRL_WK_CID:
		lps_ctrl_wk_hdl(padapter, (u8)pdrvextra_cmd->type);
		break;
	case DM_IN_LPS_WK_CID:
		rtw_dm_in_lps_hdl(padapter);
		break;
	case LPS_CHANGE_DTIM_CID:
		rtw_lps_change_dtim_hdl(padapter, (u8)pdrvextra_cmd->type);
		break;
	case CHECK_HIQ_WK_CID:
		rtw_chk_hi_queue_hdl(padapter);
		break;
	/* add for CONFIG_IEEE80211W, none 11w can use it */
	case RESET_SECURITYPRIV:
		rtw_reset_securitypriv(padapter);
		break;
	case FREE_ASSOC_RESOURCES:
		rtw_free_assoc_resources(padapter, 1);
		break;
	case C2H_WK_CID:
		rtw_hal_set_hwreg_with_buf(padapter, HW_VAR_C2H_HANDLE, pdrvextra_cmd->pbuf, pdrvextra_cmd->size);
		break;
	case DM_RA_MSK_WK_CID:
		rtw_dm_ra_mask_hdl(padapter, (struct sta_info *)pdrvextra_cmd->pbuf);
		break;
	case BTINFO_WK_CID:
		rtw_btinfo_hdl(padapter, pdrvextra_cmd->pbuf, pdrvextra_cmd->size);
		break;
	default:
		break;
	}

	if (pdrvextra_cmd->pbuf && pdrvextra_cmd->size > 0) {
		kfree(pdrvextra_cmd->pbuf);
	}

	return H2C_SUCCESS;
}

void rtw_survey_cmd_callback(struct adapter *padapter,  struct cmd_obj *pcmd)
{
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (pcmd->res == H2C_DROPPED) {
		/* TODO: cancel timer and do timeout handler directly... */
		/* need to make timeout handlerOS independent */
		_set_timer(&pmlmepriv->scan_to_timer, 1);
	} else if (pcmd->res != H2C_SUCCESS) {
		_set_timer(&pmlmepriv->scan_to_timer, 1);
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("\n ********Error: MgntActrtw_set_802_11_bssid_LIST_SCAN Fail ************\n\n."));
	}

	/*  free cmd */
	rtw_free_cmd_obj(pcmd);
}

void rtw_disassoc_cmd_callback(struct adapter *padapter,  struct cmd_obj *pcmd)
{
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (pcmd->res != H2C_SUCCESS) {
		spin_lock_bh(&pmlmepriv->lock);
		set_fwstate(pmlmepriv, _FW_LINKED);
		spin_unlock_bh(&pmlmepriv->lock);

		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("\n ***Error: disconnect_cmd_callback Fail ***\n."));
		return;
	}
	/*  free cmd */
	rtw_free_cmd_obj(pcmd);
}

void rtw_joinbss_cmd_callback(struct adapter *padapter,  struct cmd_obj *pcmd)
{
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (pcmd->res == H2C_DROPPED) {
		/* TODO: cancel timer and do timeout handler directly... */
		/* need to make timeout handlerOS independent */
		_set_timer(&pmlmepriv->assoc_timer, 1);
	} else if (pcmd->res != H2C_SUCCESS) {
		_set_timer(&pmlmepriv->assoc_timer, 1);
	}

	rtw_free_cmd_obj(pcmd);
}

void rtw_createbss_cmd_callback(struct adapter *padapter, struct cmd_obj *pcmd)
{
	u8 timer_cancelled;
	struct sta_info *psta = NULL;
	struct wlan_network *pwlan = NULL;
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_bssid_ex *pnetwork = (struct wlan_bssid_ex *)pcmd->parmbuf;
	struct wlan_network *tgt_network = &(pmlmepriv->cur_network);

	if (pcmd->parmbuf == NULL)
		goto exit;

	if ((pcmd->res != H2C_SUCCESS)) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("\n ********Error: rtw_createbss_cmd_callback  Fail ************\n\n."));
		_set_timer(&pmlmepriv->assoc_timer, 1);
	}

	_cancel_timer(&pmlmepriv->assoc_timer, &timer_cancelled);

	spin_lock_bh(&pmlmepriv->lock);


	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		psta = rtw_get_stainfo(&padapter->stapriv, pnetwork->MacAddress);
		if (!psta) {
			psta = rtw_alloc_stainfo(&padapter->stapriv, pnetwork->MacAddress);
			if (psta == NULL) {
				RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("\nCan't alloc sta_info when createbss_cmd_callback\n"));
				goto createbss_cmd_fail;
			}
		}

		rtw_indicate_connect(padapter);
	} else {
		pwlan = rtw_alloc_network(pmlmepriv);
		spin_lock_bh(&(pmlmepriv->scanned_queue.lock));
		if (pwlan == NULL) {
			pwlan = rtw_get_oldest_wlan_network(&pmlmepriv->scanned_queue);
			if (pwlan == NULL) {
				RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("\n Error:  can't get pwlan in rtw_joinbss_event_callback\n"));
				spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
				goto createbss_cmd_fail;
			}
			pwlan->last_scanned = jiffies;
		} else {
			list_add_tail(&(pwlan->list), &pmlmepriv->scanned_queue.queue);
		}

		pnetwork->Length = get_wlan_bssid_ex_sz(pnetwork);
		memcpy(&(pwlan->network), pnetwork, pnetwork->Length);
		/* pwlan->fixed = true; */

		/* list_add_tail(&(pwlan->list), &pmlmepriv->scanned_queue.queue); */

		/*  copy pdev_network information to	pmlmepriv->cur_network */
		memcpy(&tgt_network->network, pnetwork, (get_wlan_bssid_ex_sz(pnetwork)));

		/*  reset DSConfig */
		/* tgt_network->network.Configuration.DSConfig = (u32)rtw_ch2freq(pnetwork->Configuration.DSConfig); */

		_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);

		spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
		/*  we will set _FW_LINKED when there is one more sat to join us (rtw_stassoc_event_callback) */

	}

createbss_cmd_fail:

	spin_unlock_bh(&pmlmepriv->lock);
exit:
	rtw_free_cmd_obj(pcmd);
}



void rtw_setstaKey_cmdrsp_callback(struct adapter *padapter,  struct cmd_obj *pcmd)
{

	struct sta_priv *pstapriv = &padapter->stapriv;
	struct set_stakey_rsp *psetstakey_rsp = (struct set_stakey_rsp *) (pcmd->rsp);
	struct sta_info *psta = rtw_get_stainfo(pstapriv, psetstakey_rsp->addr);

	if (psta == NULL) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("\nERROR: rtw_setstaKey_cmdrsp_callback => can't get sta_info\n\n"));
		goto exit;
	}
exit:
	rtw_free_cmd_obj(pcmd);
}

void rtw_setassocsta_cmdrsp_callback(struct adapter *padapter,  struct cmd_obj *pcmd)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct set_assocsta_parm *passocsta_parm = (struct set_assocsta_parm *)(pcmd->parmbuf);
	struct set_assocsta_rsp *passocsta_rsp = (struct set_assocsta_rsp *) (pcmd->rsp);
	struct sta_info *psta = rtw_get_stainfo(pstapriv, passocsta_parm->addr);

	if (psta == NULL) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("\nERROR: setassocsta_cmdrsp_callbac => can't get sta_info\n\n"));
		goto exit;
	}

	psta->aid = passocsta_rsp->cam_id;
	psta->mac_id = passocsta_rsp->cam_id;

	spin_lock_bh(&pmlmepriv->lock);

	if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == true) && (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == true))
		_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);

	set_fwstate(pmlmepriv, _FW_LINKED);
	spin_unlock_bh(&pmlmepriv->lock);

exit:
	rtw_free_cmd_obj(pcmd);
}
