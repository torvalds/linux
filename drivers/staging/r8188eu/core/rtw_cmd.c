// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2012 Realtek Corporation. */

#define _RTW_CMD_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/recv_osdep.h"
#include "../include/mlme_osdep.h"
#include "../include/rtw_br_ext.h"
#include "../include/rtw_mlme_ext.h"

/*
Caller and the rtw_cmd_thread can protect cmd_q by spin_lock.
No irqsave is necessary.
*/

static int _rtw_init_cmd_priv(struct cmd_priv *pcmdpriv)
{
	int res = _SUCCESS;

	sema_init(&pcmdpriv->cmd_queue_sema, 0);
	/* sema_init(&(pcmdpriv->cmd_done_sema), 0); */
	sema_init(&pcmdpriv->terminate_cmdthread_sema, 0);

	_rtw_init_queue(&pcmdpriv->cmd_queue);

	/* allocate DMA-able/Non-Page memory for cmd_buf and rsp_buf */

	pcmdpriv->cmd_seq = 1;

	pcmdpriv->cmd_allocated_buf = kzalloc(MAX_CMDSZ + CMDBUFF_ALIGN_SZ,
					      GFP_KERNEL);

	if (!pcmdpriv->cmd_allocated_buf) {
		res = _FAIL;
		goto exit;
	}

	pcmdpriv->cmd_buf = pcmdpriv->cmd_allocated_buf  +  CMDBUFF_ALIGN_SZ - ((size_t)(pcmdpriv->cmd_allocated_buf) & (CMDBUFF_ALIGN_SZ - 1));

	pcmdpriv->rsp_allocated_buf = kzalloc(MAX_RSPSZ + 4, GFP_KERNEL);

	if (!pcmdpriv->rsp_allocated_buf) {
		res = _FAIL;
		goto exit;
	}

	pcmdpriv->rsp_buf = pcmdpriv->rsp_allocated_buf  +  4 - ((size_t)(pcmdpriv->rsp_allocated_buf) & 3);

	pcmdpriv->cmd_issued_cnt = 0;
	pcmdpriv->cmd_done_cnt = 0;
	pcmdpriv->rsp_cnt = 0;
exit:

	return res;
}

static void c2h_wk_callback(struct work_struct *work);

static int _rtw_init_evt_priv(struct evt_priv *pevtpriv)
{
	int res = _SUCCESS;

	/* allocate DMA-able/Non-Page memory for cmd_buf and rsp_buf */
	atomic_set(&pevtpriv->event_seq, 0);
	pevtpriv->evt_done_cnt = 0;

	_init_workitem(&pevtpriv->c2h_wk, c2h_wk_callback, NULL);
	pevtpriv->c2h_wk_alive = false;
	pevtpriv->c2h_queue = rtw_cbuf_alloc(C2H_QUEUE_MAX_LEN + 1);

	return res;
}

void rtw_free_evt_priv(struct	evt_priv *pevtpriv)
{
	_cancel_workitem_sync(&pevtpriv->c2h_wk);
	while (pevtpriv->c2h_wk_alive)
		msleep(10);

	while (!rtw_cbuf_empty(pevtpriv->c2h_queue)) {
		void *c2h = rtw_cbuf_pop(pevtpriv->c2h_queue);
		if (c2h && c2h != (void *)pevtpriv)
			kfree(c2h);
	}
}

static void _rtw_free_cmd_priv(struct cmd_priv *pcmdpriv)
{
	if (pcmdpriv) {
		kfree(pcmdpriv->cmd_allocated_buf);
		kfree(pcmdpriv->rsp_allocated_buf);
	}
}

/*
Calling Context:

rtw_enqueue_cmd can only be called between kernel thread,
since only spin_lock is used.

ISR/Call-Back functions can't call this sub-function.

*/

static int _rtw_enqueue_cmd(struct __queue *queue, struct cmd_obj *obj)
{
	unsigned long flags;

	if (!obj)
		goto exit;

	spin_lock_irqsave(&queue->lock, flags);

	list_add_tail(&obj->list, &queue->queue);

	spin_unlock_irqrestore(&queue->lock, flags);

exit:

	return _SUCCESS;
}

static struct cmd_obj *_rtw_dequeue_cmd(struct __queue *queue)
{
	struct cmd_obj *obj;
	unsigned long flags;

	spin_lock_irqsave(&queue->lock, flags);
	if (list_empty(&queue->queue)) {
		obj = NULL;
	} else {
		obj = container_of((&queue->queue)->next, struct cmd_obj, list);
		rtw_list_delete(&obj->list);
	}

	spin_unlock_irqrestore(&queue->lock, flags);

	return obj;
}

u32	rtw_init_cmd_priv(struct cmd_priv *pcmdpriv)
{
	u32	res;

	res = _rtw_init_cmd_priv(pcmdpriv);

	return res;
}

u32 rtw_init_evt_priv(struct evt_priv *pevtpriv)
{
	int	res;

	res = _rtw_init_evt_priv(pevtpriv);

	return res;
}

void rtw_free_cmd_priv(struct	cmd_priv *pcmdpriv)
{
	_rtw_free_cmd_priv(pcmdpriv);
}

static int rtw_cmd_filter(struct cmd_priv *pcmdpriv, struct cmd_obj *cmd_obj)
{
	u8 bAllow = false; /* set to true to allow enqueuing cmd when hw_init_completed is false */

	/* To decide allow or not */
	if ((pcmdpriv->padapter->pwrctrlpriv.bHWPwrPindetect) &&
	    (!pcmdpriv->padapter->registrypriv.usbss_enable)) {
		if (cmd_obj->cmdcode == GEN_CMD_CODE(_Set_Drv_Extra)) {
			struct drvextra_cmd_parm	*pdrvextra_cmd_parm = (struct drvextra_cmd_parm	*)cmd_obj->parmbuf;
			if (pdrvextra_cmd_parm->ec_id == POWER_SAVING_CTRL_WK_CID)
				bAllow = true;
		}
	}

	if (cmd_obj->cmdcode == GEN_CMD_CODE(_SetChannelPlan))
		bAllow = true;

	if ((!pcmdpriv->padapter->hw_init_completed && !bAllow) ||
	    !pcmdpriv->cmdthd_running)	/* com_thread not running */
		return _FAIL;
	return _SUCCESS;
}

u32 rtw_enqueue_cmd(struct cmd_priv *pcmdpriv, struct cmd_obj *cmd_obj)
{
	int res = _FAIL;
	struct adapter *padapter = pcmdpriv->padapter;

	if (!cmd_obj)
		goto exit;

	cmd_obj->padapter = padapter;

	res = rtw_cmd_filter(pcmdpriv, cmd_obj);
	if (_FAIL == res) {
		rtw_free_cmd_obj(cmd_obj);
		goto exit;
	}

	res = _rtw_enqueue_cmd(&pcmdpriv->cmd_queue, cmd_obj);

	if (res == _SUCCESS)
		up(&pcmdpriv->cmd_queue_sema);

exit:

	return res;
}

struct	cmd_obj	*rtw_dequeue_cmd(struct cmd_priv *pcmdpriv)
{
	struct cmd_obj *cmd_obj;

	cmd_obj = _rtw_dequeue_cmd(&pcmdpriv->cmd_queue);

	return cmd_obj;
}

void rtw_cmd_clr_isr(struct	cmd_priv *pcmdpriv)
{

	pcmdpriv->cmd_done_cnt++;
	/* up(&(pcmdpriv->cmd_done_sema)); */

}

void rtw_free_cmd_obj(struct cmd_obj *pcmd)
{

	if ((pcmd->cmdcode != _JoinBss_CMD_) && (pcmd->cmdcode != _CreateBss_CMD_)) {
		/* free parmbuf in cmd_obj */
		kfree(pcmd->parmbuf);
	}

	if (pcmd->rsp) {
		if (pcmd->rspsz != 0) {
			/* free rsp in cmd_obj */
			kfree(pcmd->rsp);
		}
	}

	/* free cmd_obj */
	kfree(pcmd);

}

int rtw_cmd_thread(void *context)
{
	u8 ret;
	struct cmd_obj *pcmd;
	u8 *pcmdbuf;
	u8 (*cmd_hdl)(struct adapter *padapter, u8 *pbuf);
	void (*pcmd_callback)(struct adapter *dev, struct cmd_obj *pcmd);
	struct adapter *padapter = (struct adapter *)context;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	thread_enter("RTW_CMD_THREAD");

	pcmdbuf = pcmdpriv->cmd_buf;

	pcmdpriv->cmdthd_running = true;
	up(&pcmdpriv->terminate_cmdthread_sema);

	while (1) {
		if (_rtw_down_sema(&pcmdpriv->cmd_queue_sema) == _FAIL)
			break;

		if (padapter->bDriverStopped ||
		    padapter->bSurpriseRemoved) {
			DBG_88E("%s: DriverStopped(%d) SurpriseRemoved(%d) break at line %d\n",
				__func__, padapter->bDriverStopped, padapter->bSurpriseRemoved, __LINE__);
			break;
		}
_next:
		if (padapter->bDriverStopped ||
		    padapter->bSurpriseRemoved) {
			DBG_88E("%s: DriverStopped(%d) SurpriseRemoved(%d) break at line %d\n",
				__func__, padapter->bDriverStopped, padapter->bSurpriseRemoved, __LINE__);
			break;
		}

		pcmd = rtw_dequeue_cmd(pcmdpriv);
		if (!pcmd)
			continue;

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

		/* call callback function for post-processed */
		if (pcmd->cmdcode < ARRAY_SIZE(rtw_cmd_callback)) {
			pcmd_callback = rtw_cmd_callback[pcmd->cmdcode].callback;
			if (!pcmd_callback)
				rtw_free_cmd_obj(pcmd);
			else
				/* todo: !!! fill rsp_buf to pcmd->rsp if (pcmd->rsp!= NULL) */
				pcmd_callback(pcmd->padapter, pcmd);/* need conider that free cmd_obj in rtw_cmd_callback */
		} else {
			rtw_free_cmd_obj(pcmd);
		}

		flush_signals_thread();

		goto _next;
	}
	pcmdpriv->cmdthd_running = false;

	/*  free all cmd_obj resources */
	do {
		pcmd = rtw_dequeue_cmd(pcmdpriv);
		if (!pcmd)
			break;

		/* DBG_88E("%s: leaving... drop cmdcode:%u\n", __func__, pcmd->cmdcode); */

		rtw_free_cmd_obj(pcmd);
	} while (1);

	up(&pcmdpriv->terminate_cmdthread_sema);

	thread_exit();
}

u8 rtw_setstandby_cmd(struct adapter *padapter, uint action)
{
	struct cmd_obj *ph2c;
	struct usb_suspend_parm *psetusbsuspend;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;

	u8 ret = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		ret = _FAIL;
		goto exit;
	}

	psetusbsuspend = kzalloc(sizeof(struct usb_suspend_parm), GFP_ATOMIC);
	if (!psetusbsuspend) {
		kfree(ph2c);
		ret = _FAIL;
		goto exit;
	}

	psetusbsuspend->action = action;

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetusbsuspend, GEN_CMD_CODE(_SetUsbSuspend));

	ret = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return ret;
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
	struct cmd_priv		*pcmdpriv = &padapter->cmdpriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;

	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_SCAN, 1);
	}

	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		p2p_ps_wk_cmd(padapter, P2P_PS_SCAN, 1);
	}

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c)
		return _FAIL;

	psurveyPara = kzalloc(sizeof(struct sitesurvey_parm), GFP_ATOMIC);
	if (!psurveyPara) {
		kfree(ph2c);
		return _FAIL;
	}

	rtw_free_network_queue(padapter, false);

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
			}
		}
	}

	set_fwstate(pmlmepriv, _FW_UNDER_SURVEY);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

	if (res == _SUCCESS) {
		pmlmepriv->scan_start_time = jiffies;

		_set_timer(&pmlmepriv->scan_to_timer, SCANNING_TIMEOUT);

		rtw_led_control(padapter, LED_CTL_SITE_SURVEY);

		pmlmepriv->scan_interval = SCAN_INTERVAL;/*  30*2 sec = 60sec */
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
	u8	res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}

	pbsetdataratepara = kzalloc(sizeof(struct setdatarate_parm), GFP_ATOMIC);
	if (!pbsetdataratepara) {
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

u8 rtw_setbasicrate_cmd(struct adapter *padapter, u8 *rateset)
{
	struct cmd_obj *ph2c;
	struct setbasicrate_parm *pssetbasicratepara;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}
	pssetbasicratepara = kzalloc(sizeof(struct setbasicrate_parm), GFP_ATOMIC);

	if (!pssetbasicratepara) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, pssetbasicratepara, _SetBasicRate_CMD_);

	memcpy(pssetbasicratepara->basicrates, rateset, NumRates);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:

	return res;
}

/*
unsigned char rtw_setphy_cmd(unsigned char  *adapter)

1.  be called only after rtw_update_registrypriv_dev_network(~) or mp testing program
2.  for AdHoc/Ap mode or mp mode?

*/
u8 rtw_setphy_cmd(struct adapter *padapter, u8 modem, u8 ch)
{
	struct cmd_obj *ph2c;
	struct setphy_parm *psetphypara;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
		}
	psetphypara = kzalloc(sizeof(struct setphy_parm), GFP_ATOMIC);

	if (!psetphypara) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetphypara, _SetPhy_CMD_);

	psetphypara->modem = modem;
	psetphypara->rfchannel = ch;

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:

	return res;
}

u8 rtw_setbbreg_cmd(struct adapter *padapter, u8 offset, u8 val)
{
	struct cmd_obj *ph2c;
	struct writeBB_parm *pwritebbparm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
		}
	pwritebbparm = kzalloc(sizeof(struct writeBB_parm), GFP_ATOMIC);

	if (!pwritebbparm) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, pwritebbparm, GEN_CMD_CODE(_SetBBReg));

	pwritebbparm->offset = offset;
	pwritebbparm->value = val;

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:

	return res;
}

u8 rtw_getbbreg_cmd(struct adapter  *padapter, u8 offset, u8 *pval)
{
	struct cmd_obj *ph2c;
	struct readBB_parm *prdbbparm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
		}
	prdbbparm = kzalloc(sizeof(struct readBB_parm), GFP_ATOMIC);

	if (!prdbbparm) {
		kfree(ph2c);
		return _FAIL;
	}

	INIT_LIST_HEAD(&ph2c->list);
	ph2c->cmdcode = GEN_CMD_CODE(_GetBBReg);
	ph2c->parmbuf = (unsigned char *)prdbbparm;
	ph2c->cmdsz =  sizeof(struct readBB_parm);
	ph2c->rsp = pval;
	ph2c->rspsz = sizeof(struct readBB_rsp);

	prdbbparm->offset = offset;

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:

	return res;
}

u8 rtw_setrfreg_cmd(struct adapter  *padapter, u8 offset, u32 val)
{
	struct cmd_obj *ph2c;
	struct writeRF_parm *pwriterfparm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}
	pwriterfparm = kzalloc(sizeof(struct writeRF_parm), GFP_ATOMIC);

	if (!pwriterfparm) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, pwriterfparm, GEN_CMD_CODE(_SetRFReg));

	pwriterfparm->offset = offset;
	pwriterfparm->value = val;

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:

	return res;
}

u8 rtw_getrfreg_cmd(struct adapter  *padapter, u8 offset, u8 *pval)
{
	struct cmd_obj *ph2c;
	struct readRF_parm *prdrfparm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}

	prdrfparm = kzalloc(sizeof(struct readRF_parm), GFP_ATOMIC);
	if (!prdrfparm) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	INIT_LIST_HEAD(&ph2c->list);
	ph2c->cmdcode = GEN_CMD_CODE(_GetRFReg);
	ph2c->parmbuf = (unsigned char *)prdrfparm;
	ph2c->cmdsz =  sizeof(struct readRF_parm);
	ph2c->rsp = pval;
	ph2c->rspsz = sizeof(struct readRF_rsp);

	prdrfparm->offset = offset;

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;
}

void rtw_getbbrfreg_cmdrsp_callback(struct adapter *padapter,  struct cmd_obj *pcmd)
{


	kfree(pcmd->parmbuf);
	kfree(pcmd);

	if (padapter->registrypriv.mp_mode == 1)
		padapter->mppriv.workparam.bcompleted = true;

}

void rtw_readtssi_cmdrsp_callback(struct adapter *padapter,  struct cmd_obj *pcmd)
{


	kfree(pcmd->parmbuf);
	kfree(pcmd);

	if (padapter->registrypriv.mp_mode == 1)
		padapter->mppriv.workparam.bcompleted = true;

}

u8 rtw_createbss_cmd(struct adapter  *padapter)
{
	struct cmd_obj *pcmd;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	struct wlan_bssid_ex *pdev_network = &padapter->registrypriv.dev_network;
	u8	res = _SUCCESS;

	rtw_led_control(padapter, LED_CTL_START_TO_LINK);

	pcmd = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!pcmd) {
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

u8 rtw_createbss_cmd_ex(struct adapter  *padapter, unsigned char *pbss, unsigned int sz)
{
	struct cmd_obj *pcmd;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;

	pcmd = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!pcmd) {
		res = _FAIL;
		goto exit;
	}

	INIT_LIST_HEAD(&pcmd->list);
	pcmd->cmdcode = GEN_CMD_CODE(_CreateBss);
	pcmd->parmbuf = pbss;
	pcmd->cmdsz =  sz;
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	res = rtw_enqueue_cmd(pcmdpriv, pcmd);

exit:

	return res;
}

u8 rtw_joinbss_cmd(struct adapter  *padapter, struct wlan_network *pnetwork)
{
	u8	res = _SUCCESS;
	uint	t_len = 0;
	struct wlan_bssid_ex		*psecnetwork;
	struct cmd_obj		*pcmd;
	struct cmd_priv		*pcmdpriv = &padapter->cmdpriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct qos_priv		*pqospriv = &pmlmepriv->qospriv;
	struct security_priv	*psecuritypriv = &padapter->securitypriv;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	struct ht_priv		*phtpriv = &pmlmepriv->htpriv;
	enum ndis_802_11_network_infra ndis_network_mode = pnetwork->network.InfrastructureMode;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;

	rtw_led_control(padapter, LED_CTL_START_TO_LINK);

	pcmd = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!pcmd) {
		res = _FAIL;
		goto exit;
	}
	/* for IEs is fix buf size */
	t_len = sizeof(struct wlan_bssid_ex);

	/* for hidden ap to set fw_state here */
	if (!check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_ADHOC_STATE)) {
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
	if (!psecnetwork) {
		kfree(pcmd);
		res = _FAIL;
		goto exit;
	}

	memset(psecnetwork, 0, t_len);

	memcpy(psecnetwork, &pnetwork->network, get_wlan_bssid_ex_sz(&pnetwork->network));

	psecuritypriv->authenticator_ie[0] = (unsigned char)psecnetwork->IELength;

	if (psecnetwork->IELength - 12 < 255) {
		memcpy(&psecuritypriv->authenticator_ie[1], &psecnetwork->IEs[12], psecnetwork->IELength - 12);
	} else {
		memcpy(&psecuritypriv->authenticator_ie[1], &psecnetwork->IEs[12], 255);
	}

	psecnetwork->IELength = 0;
	/*  Added by Albert 2009/02/18 */
	/*  If the the driver wants to use the bssid to create the connection. */
	/*  If not,  we have to copy the connecting AP's MAC address to it so that */
	/*  the driver just has the bssid information for PMKIDList searching. */

	if (!pmlmepriv->assoc_by_bssid)
		memcpy(&pmlmepriv->assoc_bssid[0], &pnetwork->network.MacAddress[0], ETH_ALEN);

	psecnetwork->IELength = rtw_restruct_sec_ie(padapter, &pnetwork->network.IEs[0], &psecnetwork->IEs[0], pnetwork->network.IELength);

	pqospriv->qos_option = 0;

	if (pregistrypriv->wmm_enable) {
		u32 tmp_len;

		tmp_len = rtw_restruct_wmm_ie(padapter, &pnetwork->network.IEs[0], &psecnetwork->IEs[0], pnetwork->network.IELength, psecnetwork->IELength);

		if (psecnetwork->IELength != tmp_len) {
			psecnetwork->IELength = tmp_len;
			pqospriv->qos_option = 1; /* There is WMM IE in this corresp. beacon */
		} else {
			pqospriv->qos_option = 0;/* There is no WMM IE in this corresp. beacon */
		}
	}

	phtpriv->ht_option = false;
	if (pregistrypriv->ht_enable) {
		/* 	Added by Albert 2010/06/23 */
		/* 	For the WEP mode, we will use the bg mode to do the connection to avoid some IOT issue. */
		/* 	Especially for Realtek 8192u SoftAP. */
		if ((padapter->securitypriv.dot11PrivacyAlgrthm != _WEP40_) &&
		    (padapter->securitypriv.dot11PrivacyAlgrthm != _WEP104_) &&
		    (padapter->securitypriv.dot11PrivacyAlgrthm != _TKIP_)) {
			/* rtw_restructure_ht_ie */
			rtw_restructure_ht_ie(padapter, &pnetwork->network.IEs[0], &psecnetwork->IEs[0],
									pnetwork->network.IELength, &psecnetwork->IELength);
		}
	}

	pmlmeinfo->assoc_AP_vendor = check_assoc_AP(pnetwork->network.IEs, pnetwork->network.IELength);

	if (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_TENDA)
		padapter->pwrctrlpriv.smart_ps = 0;
	else
		padapter->pwrctrlpriv.smart_ps = padapter->registrypriv.smart_ps;

	DBG_88E("%s: smart_ps =%d\n", __func__, padapter->pwrctrlpriv.smart_ps);

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

	/* prepare cmd parameter */
	param = kzalloc(sizeof(*param), GFP_ATOMIC);
	if (!param) {
		res = _FAIL;
		goto exit;
	}
	param->deauth_timeout_ms = deauth_timeout_ms;

	if (enqueue) {
		/* need enqueue, prepare cmd_obj and enqueue */
		cmdobj = kzalloc(sizeof(*cmdobj), GFP_ATOMIC);
		if (!cmdobj) {
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

u8 rtw_setopmode_cmd(struct adapter  *padapter, enum ndis_802_11_network_infra networktype)
{
	struct	cmd_obj *ph2c;
	struct	setopmode_parm *psetop;

	struct	cmd_priv   *pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
	if (!ph2c) {
		res = false;
		goto exit;
	}
	psetop = kzalloc(sizeof(struct setopmode_parm), GFP_KERNEL);

	if (!psetop) {
		kfree(ph2c);
		res = false;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetop, _SetOpMode_CMD_);
	psetop->mode = (u8)networktype;

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;
}

u8 rtw_setstakey_cmd(struct adapter *padapter, u8 *psta, u8 unicast_key)
{
	struct cmd_obj *ph2c;
	struct set_stakey_parm *psetstakey_para;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	struct set_stakey_rsp *psetstakey_rsp = NULL;

	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct sta_info *sta = (struct sta_info *)psta;
	u8	res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}

	psetstakey_para = kzalloc(sizeof(struct set_stakey_parm), GFP_KERNEL);
	if (!psetstakey_para) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	psetstakey_rsp = kzalloc(sizeof(struct set_stakey_rsp), GFP_KERNEL);
	if (!psetstakey_rsp) {
		kfree(ph2c);
		kfree(psetstakey_para);
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetstakey_para, _SetStaKey_CMD_);
	ph2c->rsp = (u8 *)psetstakey_rsp;
	ph2c->rspsz = sizeof(struct set_stakey_rsp);

	memcpy(psetstakey_para->addr, sta->hwaddr, ETH_ALEN);

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE))
		psetstakey_para->algorithm = (unsigned char)psecuritypriv->dot11PrivacyAlgrthm;
	else
		GET_ENCRY_ALGO(psecuritypriv, sta, psetstakey_para->algorithm, false);

	if (unicast_key)
		memcpy(&psetstakey_para->key, &sta->dot118021x_UncstKey, 16);
	else
		memcpy(&psetstakey_para->key, &psecuritypriv->dot118021XGrpKey[psecuritypriv->dot118021XGrpKeyid].skey, 16);

	/* jeff: set this because at least sw key is ready */
	padapter->securitypriv.busetkipkey = true;

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;
}

u8 rtw_clearstakey_cmd(struct adapter *padapter, u8 *psta, u8 entry, u8 enqueue)
{
	struct cmd_obj *ph2c;
	struct set_stakey_parm	*psetstakey_para;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	struct set_stakey_rsp *psetstakey_rsp = NULL;
	struct sta_info *sta = (struct sta_info *)psta;
	u8	res = _SUCCESS;

	if (!enqueue) {
		clear_cam_entry(padapter, entry);
	} else {
		ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
		if (!ph2c) {
			res = _FAIL;
			goto exit;
		}

		psetstakey_para = kzalloc(sizeof(struct set_stakey_parm),
					  GFP_ATOMIC);
		if (!psetstakey_para) {
			kfree(ph2c);
			res = _FAIL;
			goto exit;
		}

		psetstakey_rsp = kzalloc(sizeof(struct set_stakey_rsp),
					 GFP_ATOMIC);
		if (!psetstakey_rsp) {
			kfree(ph2c);
			kfree(psetstakey_para);
			res = _FAIL;
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(ph2c, psetstakey_para, _SetStaKey_CMD_);
		ph2c->rsp = (u8 *)psetstakey_rsp;
		ph2c->rspsz = sizeof(struct set_stakey_rsp);

		memcpy(psetstakey_para->addr, sta->hwaddr, ETH_ALEN);

		psetstakey_para->algorithm = _NO_PRIVACY_;

		psetstakey_para->id = entry;

		res = rtw_enqueue_cmd(pcmdpriv, ph2c);
	}
exit:

	return res;
}

u8 rtw_setrttbl_cmd(struct adapter  *padapter, struct setratable_parm *prate_table)
{
	struct cmd_obj *ph2c;
	struct setratable_parm *psetrttblparm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}
	psetrttblparm = kzalloc(sizeof(struct setratable_parm), GFP_KERNEL);

	if (!psetrttblparm) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetrttblparm, GEN_CMD_CODE(_SetRaTable));

	memcpy(psetrttblparm, prate_table, sizeof(struct setratable_parm));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:

	return res;
}

u8 rtw_getrttbl_cmd(struct adapter  *padapter, struct getratable_rsp *pval)
{
	struct cmd_obj *ph2c;
	struct getratable_parm *pgetrttblparm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}
	pgetrttblparm = kzalloc(sizeof(struct getratable_parm), GFP_KERNEL);

	if (!pgetrttblparm) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

/* 	init_h2fwcmd_w_parm_no_rsp(ph2c, psetrttblparm, GEN_CMD_CODE(_SetRaTable)); */

	INIT_LIST_HEAD(&ph2c->list);
	ph2c->cmdcode = GEN_CMD_CODE(_GetRaTable);
	ph2c->parmbuf = (unsigned char *)pgetrttblparm;
	ph2c->cmdsz =  sizeof(struct getratable_parm);
	ph2c->rsp = (u8 *)pval;
	ph2c->rspsz = sizeof(struct getratable_rsp);

	pgetrttblparm->rsvd = 0x0;

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:

	return res;
}

u8 rtw_setassocsta_cmd(struct adapter  *padapter, u8 *mac_addr)
{
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	struct cmd_obj *ph2c;
	struct set_assocsta_parm *psetassocsta_para;
	struct set_stakey_rsp *psetassocsta_rsp = NULL;

	u8	res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}

	psetassocsta_para = kzalloc(sizeof(struct set_assocsta_parm), GFP_ATOMIC);
	if (!psetassocsta_para) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	psetassocsta_rsp = kzalloc(sizeof(struct set_assocsta_rsp), GFP_ATOMIC);
	if (!psetassocsta_rsp) {
		kfree(ph2c);
		kfree(psetassocsta_para);
		return _FAIL;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetassocsta_para, _SetAssocSta_CMD_);
	ph2c->rsp = (u8 *)psetassocsta_rsp;
	ph2c->rspsz = sizeof(struct set_assocsta_rsp);

	memcpy(psetassocsta_para->addr, mac_addr, ETH_ALEN);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;
 }

u8 rtw_addbareq_cmd(struct adapter *padapter, u8 tid, u8 *addr)
{
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	struct cmd_obj *ph2c;
	struct addBaReq_parm *paddbareq_parm;
	u8	res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}

	paddbareq_parm = kzalloc(sizeof(struct addBaReq_parm), GFP_ATOMIC);
	if (!paddbareq_parm) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	paddbareq_parm->tid = tid;
	memcpy(paddbareq_parm->addr, addr, ETH_ALEN);

	init_h2fwcmd_w_parm_no_rsp(ph2c, paddbareq_parm, GEN_CMD_CODE(_AddBAReq));

	/* DBG_88E("rtw_addbareq_cmd, tid =%d\n", tid); */

	/* rtw_enqueue_cmd(pcmdpriv, ph2c); */
	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;
}

u8 rtw_dynamic_chk_wk_cmd(struct adapter *padapter)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = kzalloc(sizeof(struct drvextra_cmd_parm), GFP_ATOMIC);
	if (!pdrvextra_cmd_parm) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = DYNAMIC_CHK_WK_CID;
	pdrvextra_cmd_parm->type_size = 0;
	pdrvextra_cmd_parm->pbuf = (u8 *)padapter;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

	/* rtw_enqueue_cmd(pcmdpriv, ph2c); */
	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:

	return res;
}

u8 rtw_set_ch_cmd(struct adapter *padapter, u8 ch, u8 bw, u8 ch_offset, u8 enqueue)
{
	struct cmd_obj *pcmdobj;
	struct set_ch_parm *set_ch_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	u8 res = _SUCCESS;

	DBG_88E(FUNC_NDEV_FMT" ch:%u, bw:%u, ch_offset:%u\n",
		FUNC_NDEV_ARG(padapter->pnetdev), ch, bw, ch_offset);

	/* check input parameter */

	/* prepare cmd parameter */
	set_ch_parm = kzalloc(sizeof(*set_ch_parm), GFP_ATOMIC);
	if (!set_ch_parm) {
		res = _FAIL;
		goto exit;
	}
	set_ch_parm->ch = ch;
	set_ch_parm->bw = bw;
	set_ch_parm->ch_offset = ch_offset;

	if (enqueue) {
		/* need enqueue, prepare cmd_obj and enqueue */
		pcmdobj = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
		if (!pcmdobj) {
			kfree(set_ch_parm);
			res = _FAIL;
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(pcmdobj, set_ch_parm, GEN_CMD_CODE(_SetChannel));
		res = rtw_enqueue_cmd(pcmdpriv, pcmdobj);
	} else {
		/* no need to enqueue, do the cmd hdl directly and free cmd parameter */
		if (H2C_SUCCESS != set_ch_hdl(padapter, (u8 *)set_ch_parm))
			res = _FAIL;

		kfree(set_ch_parm);
	}

	/* do something based on res... */

exit:

	DBG_88E(FUNC_NDEV_FMT" res:%u\n", FUNC_NDEV_ARG(padapter->pnetdev), res);

	return res;
}

u8 rtw_set_chplan_cmd(struct adapter *padapter, u8 chplan, u8 enqueue)
{
	struct	cmd_obj *pcmdobj;
	struct	SetChannelPlan_param *setChannelPlan_param;
	struct	cmd_priv   *pcmdpriv = &padapter->cmdpriv;

	u8	res = _SUCCESS;

	/* check input parameter */
	if (!rtw_is_channel_plan_valid(chplan)) {
		res = _FAIL;
		goto exit;
	}

	/* prepare cmd parameter */
	setChannelPlan_param = kzalloc(sizeof(struct SetChannelPlan_param),
				       GFP_KERNEL);
	if (!setChannelPlan_param) {
		res = _FAIL;
		goto exit;
	}
	setChannelPlan_param->channel_plan = chplan;

	if (enqueue) {
		/* need enqueue, prepare cmd_obj and enqueue */
		pcmdobj = kzalloc(sizeof(struct	cmd_obj), GFP_KERNEL);
		if (!pcmdobj) {
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

u8 rtw_led_blink_cmd(struct adapter *padapter, struct LED_871x *pLed)
{
	struct	cmd_obj *pcmdobj;
	struct	LedBlink_param *ledBlink_param;
	struct	cmd_priv   *pcmdpriv = &padapter->cmdpriv;

	u8	res = _SUCCESS;

	pcmdobj = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!pcmdobj) {
		res = _FAIL;
		goto exit;
	}

	ledBlink_param = kzalloc(sizeof(struct LedBlink_param), GFP_ATOMIC);
	if (!ledBlink_param) {
		kfree(pcmdobj);
		res = _FAIL;
		goto exit;
	}

	ledBlink_param->pLed = pLed;

	init_h2fwcmd_w_parm_no_rsp(pcmdobj, ledBlink_param, GEN_CMD_CODE(_LedBlink));
	res = rtw_enqueue_cmd(pcmdpriv, pcmdobj);

exit:

	return res;
}

u8 rtw_set_csa_cmd(struct adapter *padapter, u8 new_ch_no)
{
	struct	cmd_obj *pcmdobj;
	struct	SetChannelSwitch_param *setChannelSwitch_param;
	struct	cmd_priv   *pcmdpriv = &padapter->cmdpriv;

	u8	res = _SUCCESS;

	pcmdobj = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!pcmdobj) {
		res = _FAIL;
		goto exit;
	}

	setChannelSwitch_param = kzalloc(sizeof(struct	SetChannelSwitch_param),
					 GFP_ATOMIC);
	if (!setChannelSwitch_param) {
		kfree(pcmdobj);
		res = _FAIL;
		goto exit;
	}

	setChannelSwitch_param->new_ch_no = new_ch_no;

	init_h2fwcmd_w_parm_no_rsp(pcmdobj, setChannelSwitch_param, GEN_CMD_CODE(_SetChannelSwitch));
	res = rtw_enqueue_cmd(pcmdpriv, pcmdobj);

exit:

	return res;
}

u8 rtw_tdls_cmd(struct adapter *padapter, u8 *addr, u8 option)
{
	return _SUCCESS;
}

static void traffic_status_watchdog(struct adapter *padapter)
{
	u8	bEnterPS;
	u8	bBusyTraffic = false, bTxBusyTraffic = false, bRxBusyTraffic = false;
	u8	bHigherBusyTraffic = false, bHigherBusyRxTraffic = false, bHigherBusyTxTraffic = false;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;

	/*  */
	/*  Determine if our traffic is busy now */
	/*  */
	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		if (pmlmepriv->LinkDetectInfo.NumRxOkInPeriod > 100 ||
		    pmlmepriv->LinkDetectInfo.NumTxOkInPeriod > 100) {
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
		    (pmlmepriv->LinkDetectInfo.NumRxUnicastOkInPeriod > 2))
			bEnterPS = false;
		else
			bEnterPS = true;

		/*  LeisurePS only work in infra mode. */
		if (bEnterPS)
			LPS_Enter(padapter);
		else
			LPS_Leave(padapter);
	} else {
		LPS_Leave(padapter);
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
}

static void dynamic_chk_wk_hdl(struct adapter *padapter, u8 *pbuf, int sz)
{
	struct mlme_priv *pmlmepriv;

	padapter = (struct adapter *)pbuf;
	pmlmepriv = &padapter->mlmepriv;

#ifdef CONFIG_88EU_AP_MODE
	if (check_fwstate(pmlmepriv, WIFI_AP_STATE))
		expire_timeout_chk(padapter);
#endif

	rtw_hal_sreset_xmit_status_check(padapter);

	linked_status_chk(padapter);
	traffic_status_watchdog(padapter);

	rtw_hal_dm_watchdog(padapter);
}

static void lps_ctrl_wk_hdl(struct adapter *padapter, u8 lps_ctrl_type)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8	mstatus;

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) ||
	    check_fwstate(pmlmepriv, WIFI_ADHOC_STATE))
		return;

	switch (lps_ctrl_type) {
	case LPS_CTRL_SCAN:
		if (check_fwstate(pmlmepriv, _FW_LINKED)) {
			/* connect */
			LPS_Leave(padapter);
		}
		break;
	case LPS_CTRL_JOINBSS:
		LPS_Leave(padapter);
		break;
	case LPS_CTRL_CONNECT:
		mstatus = 1;/* connect */
		/*  Reset LPS Setting */
		padapter->pwrctrlpriv.LpsIdleCount = 0;
		rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_JOINBSSRPT, (u8 *)(&mstatus));
		break;
	case LPS_CTRL_DISCONNECT:
		mstatus = 0;/* disconnect */
		LPS_Leave(padapter);
		rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_JOINBSSRPT, (u8 *)(&mstatus));
		break;
	case LPS_CTRL_SPECIAL_PACKET:
		/* DBG_88E("LPS_CTRL_SPECIAL_PACKET\n"); */
		pwrpriv->DelayLPSLastTimeStamp = jiffies;
		LPS_Leave(padapter);
		break;
	case LPS_CTRL_LEAVE:
		LPS_Leave(padapter);
		break;
	default:
		break;
	}

}

u8 rtw_lps_ctrl_wk_cmd(struct adapter *padapter, u8 lps_ctrl_type, u8 enqueue)
{
	struct cmd_obj	*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	/* struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv; */
	u8	res = _SUCCESS;

	/* if (!pwrctrlpriv->bLeisurePs) */
	/* 	return res; */

	if (enqueue) {
		ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
		if (!ph2c) {
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm = kzalloc(sizeof(struct drvextra_cmd_parm),
					     GFP_ATOMIC);
		if (!pdrvextra_cmd_parm) {
			kfree(ph2c);
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm->ec_id = LPS_CTRL_WK_CID;
		pdrvextra_cmd_parm->type_size = lps_ctrl_type;
		pdrvextra_cmd_parm->pbuf = NULL;

		init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

		res = rtw_enqueue_cmd(pcmdpriv, ph2c);
	} else {
		lps_ctrl_wk_hdl(padapter, lps_ctrl_type);
	}

exit:

	return res;
}

static void rpt_timer_setting_wk_hdl(struct adapter *padapter, u16 min_time)
{
	rtw_hal_set_hwreg(padapter, HW_VAR_RPT_TIMER_SETTING, (u8 *)(&min_time));
}

u8 rtw_rpt_timer_cfg_cmd(struct adapter *padapter, u16 min_time)
{
	struct cmd_obj		*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;

	u8	res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = kzalloc(sizeof(struct drvextra_cmd_parm),
				     GFP_ATOMIC);
	if (!pdrvextra_cmd_parm) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = RTP_TIMER_CFG_WK_CID;
	pdrvextra_cmd_parm->type_size = min_time;
	pdrvextra_cmd_parm->pbuf = NULL;
	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));
	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:

	return res;
}

static void antenna_select_wk_hdl(struct adapter *padapter, u8 antenna)
{
	rtw_hal_set_hwreg(padapter, HW_VAR_ANTENNA_DIVERSITY_SELECT, (u8 *)(&antenna));
}

u8 rtw_antenna_select_cmd(struct adapter *padapter, u8 antenna, u8 enqueue)
{
	struct cmd_obj		*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	support_ant_div;
	u8	res = _SUCCESS;

	rtw_hal_get_def_var(padapter, HAL_DEF_IS_SUPPORT_ANT_DIV, &support_ant_div);
	if (!support_ant_div)
		return res;

	if (enqueue) {
		ph2c = kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
		if (!ph2c) {
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm = kzalloc(sizeof(struct drvextra_cmd_parm),
					     GFP_KERNEL);
		if (!pdrvextra_cmd_parm) {
			kfree(ph2c);
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm->ec_id = ANT_SELECT_WK_CID;
		pdrvextra_cmd_parm->type_size = antenna;
		pdrvextra_cmd_parm->pbuf = NULL;
		init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

		res = rtw_enqueue_cmd(pcmdpriv, ph2c);
	} else {
		antenna_select_wk_hdl(padapter, antenna);
	}
exit:

	return res;
}

static void power_saving_wk_hdl(struct adapter *padapter, u8 *pbuf, int sz)
{
	 rtw_ps_processor(padapter);
}

#ifdef CONFIG_88EU_P2P
u8 p2p_protocol_wk_cmd(struct adapter *padapter, int intCmdType)
{
	struct cmd_obj	*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct wifidirect_info	*pwdinfo = &padapter->wdinfo;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return res;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = kzalloc(sizeof(struct drvextra_cmd_parm), GFP_ATOMIC);
	if (!pdrvextra_cmd_parm) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = P2P_PROTO_WK_CID;
	pdrvextra_cmd_parm->type_size = intCmdType;	/* 	As the command tppe. */
	pdrvextra_cmd_parm->pbuf = NULL;		/* 	Must be NULL here */

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;
}
#endif /* CONFIG_88EU_P2P */

u8 rtw_ps_cmd(struct adapter *padapter)
{
	struct cmd_obj		*ppscmd;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;

	u8	res = _SUCCESS;

	ppscmd = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ppscmd) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = kzalloc(sizeof(struct drvextra_cmd_parm), GFP_ATOMIC);
	if (!pdrvextra_cmd_parm) {
		kfree(ppscmd);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = POWER_SAVING_CTRL_WK_CID;
	pdrvextra_cmd_parm->pbuf = NULL;
	init_h2fwcmd_w_parm_no_rsp(ppscmd, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

	res = rtw_enqueue_cmd(pcmdpriv, ppscmd);

exit:

	return res;
}

#ifdef CONFIG_88EU_AP_MODE

static void rtw_chk_hi_queue_hdl(struct adapter *padapter)
{
	int cnt = 0;
	struct sta_info *psta_bmc;
	struct sta_priv *pstapriv = &padapter->stapriv;

	psta_bmc = rtw_get_bcmc_stainfo(padapter);
	if (!psta_bmc)
		return;

	if (psta_bmc->sleepq_len == 0) {
		u8 val = 0;

		/* while ((rtw_read32(padapter, 0x414)&0x00ffff00)!= 0) */
		/* while ((rtw_read32(padapter, 0x414)&0x0000ff00)!= 0) */

		rtw_hal_get_hwreg(padapter, HW_VAR_CHK_HI_QUEUE_EMPTY, &val);

		while (!val) {
			msleep(100);

			cnt++;

			if (cnt > 10)
				break;

			rtw_hal_get_hwreg(padapter, HW_VAR_CHK_HI_QUEUE_EMPTY, &val);
		}

		if (cnt <= 10) {
			pstapriv->tim_bitmap &= ~BIT(0);
			pstapriv->sta_dz_bitmap &= ~BIT(0);

			update_beacon(padapter, _TIM_IE_, NULL, false);
		} else { /* re check again */
			rtw_chk_hi_queue_cmd(padapter);
		}
	}
}

u8 rtw_chk_hi_queue_cmd(struct adapter *padapter)
{
	struct cmd_obj	*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = kzalloc(sizeof(struct drvextra_cmd_parm), GFP_ATOMIC);
	if (!pdrvextra_cmd_parm) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = CHECK_HIQ_WK_CID;
	pdrvextra_cmd_parm->type_size = 0;
	pdrvextra_cmd_parm->pbuf = NULL;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:
	return res;
}
#endif

u8 rtw_c2h_wk_cmd(struct adapter *padapter, u8 *c2h_evt)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = kzalloc(sizeof(struct drvextra_cmd_parm), GFP_ATOMIC);
	if (!pdrvextra_cmd_parm) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = C2H_WK_CID;
	pdrvextra_cmd_parm->type_size = c2h_evt ? 16 : 0;
	pdrvextra_cmd_parm->pbuf = c2h_evt;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;
}

static void c2h_evt_hdl(struct adapter *adapter, struct c2h_evt_hdr *c2h_evt, c2h_id_filter filter)
{
	u8 buf[16];

	if (!c2h_evt)
		c2h_evt_read(adapter, buf);
}

static void c2h_wk_callback(struct work_struct *work)
{
	struct evt_priv *evtpriv = container_of(work, struct evt_priv, c2h_wk);
	struct adapter *adapter = container_of(evtpriv, struct adapter, evtpriv);
	struct c2h_evt_hdr *c2h_evt;
	c2h_id_filter ccx_id_filter = rtw_hal_c2h_id_filter_ccx(adapter);

	evtpriv->c2h_wk_alive = true;

	while (!rtw_cbuf_empty(evtpriv->c2h_queue)) {
		if ((c2h_evt = (struct c2h_evt_hdr *)rtw_cbuf_pop(evtpriv->c2h_queue)) != NULL) {
			/* This C2H event is read, clear it */
			c2h_evt_clear(adapter);
		} else {
			c2h_evt = kmalloc(16, GFP_KERNEL);
			if (c2h_evt) {
				/* This C2H event is not read, read & clear now */
				if (c2h_evt_read(adapter, (u8 *)c2h_evt) != _SUCCESS) {
					kfree(c2h_evt);
					continue;
				}
			} else {
				return;
			}
		}

		/* Special pointer to trigger c2h_evt_clear only */
		if ((void *)c2h_evt == (void *)evtpriv)
			continue;

		if (!c2h_evt_exist(c2h_evt)) {
			kfree(c2h_evt);
			continue;
		}

		if (ccx_id_filter(c2h_evt->id)) {
			/* Handle CCX report here */
			rtw_hal_c2h_handler(adapter, c2h_evt);
			kfree(c2h_evt);
		} else {
#ifdef CONFIG_88EU_P2P
			/* Enqueue into cmd_thread for others */
			rtw_c2h_wk_cmd(adapter, (u8 *)c2h_evt);
#endif
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
	case DYNAMIC_CHK_WK_CID:
		dynamic_chk_wk_hdl(padapter, pdrvextra_cmd->pbuf, pdrvextra_cmd->type_size);
		break;
	case POWER_SAVING_CTRL_WK_CID:
		power_saving_wk_hdl(padapter, pdrvextra_cmd->pbuf, pdrvextra_cmd->type_size);
		break;
	case LPS_CTRL_WK_CID:
		lps_ctrl_wk_hdl(padapter, (u8)pdrvextra_cmd->type_size);
		break;
	case RTP_TIMER_CFG_WK_CID:
		rpt_timer_setting_wk_hdl(padapter, pdrvextra_cmd->type_size);
		break;
	case ANT_SELECT_WK_CID:
		antenna_select_wk_hdl(padapter, pdrvextra_cmd->type_size);
		break;
#ifdef CONFIG_88EU_P2P
	case P2P_PS_WK_CID:
		p2p_ps_wk_hdl(padapter, pdrvextra_cmd->type_size);
		break;
	case P2P_PROTO_WK_CID:
		/* 	Commented by Albert 2011/07/01 */
		/* 	I used the type_size as the type command */
		p2p_protocol_wk_hdl(padapter, pdrvextra_cmd->type_size);
		break;
#endif
#ifdef CONFIG_88EU_AP_MODE
	case CHECK_HIQ_WK_CID:
		rtw_chk_hi_queue_hdl(padapter);
		break;
#endif /* CONFIG_88EU_AP_MODE */
	case C2H_WK_CID:
		c2h_evt_hdl(padapter, (struct c2h_evt_hdr *)pdrvextra_cmd->pbuf, NULL);
		break;
	default:
		break;
	}

	if (pdrvextra_cmd->pbuf && pdrvextra_cmd->type_size > 0)
		kfree(pdrvextra_cmd->pbuf);

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
	}

	/*  free cmd */
	rtw_free_cmd_obj(pcmd);

}
void rtw_disassoc_cmd_callback(struct adapter *padapter, struct cmd_obj *pcmd)
{
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (pcmd->res != H2C_SUCCESS) {
		spin_lock_bh(&pmlmepriv->lock);
		set_fwstate(pmlmepriv, _FW_LINKED);
		spin_unlock_bh(&pmlmepriv->lock);

		return;
	} else /* clear bridge database */
		nat25_db_cleanup(padapter);

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
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;

	if (pcmd->res != H2C_SUCCESS)
		_set_timer(&pmlmepriv->assoc_timer, 1);

	_cancel_timer(&pmlmepriv->assoc_timer, &timer_cancelled);

	spin_lock_bh(&pmlmepriv->lock);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		psta = rtw_get_stainfo(&padapter->stapriv, pnetwork->MacAddress);
		if (!psta) {
			psta = rtw_alloc_stainfo(&padapter->stapriv, pnetwork->MacAddress);
			if (!psta)
				goto createbss_cmd_fail;
		}

		rtw_indicate_connect(padapter);
	} else {

		pwlan = _rtw_alloc_network(pmlmepriv);
		spin_lock_bh(&pmlmepriv->scanned_queue.lock);
		if (!pwlan) {
			pwlan = rtw_get_oldest_wlan_network(&pmlmepriv->scanned_queue);
			if (!pwlan) {
				spin_unlock_bh(&pmlmepriv->scanned_queue.lock);
				goto createbss_cmd_fail;
			}
			pwlan->last_scanned = jiffies;
		} else {
			list_add_tail(&pwlan->list, &pmlmepriv->scanned_queue.queue);
		}

		pnetwork->Length = get_wlan_bssid_ex_sz(pnetwork);
		memcpy(&pwlan->network, pnetwork, pnetwork->Length);

		memcpy(&tgt_network->network, pnetwork, (get_wlan_bssid_ex_sz(pnetwork)));

		_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);

		spin_unlock_bh(&pmlmepriv->scanned_queue.lock);
		/*  we will set _FW_LINKED when there is one more sat to join us (rtw_stassoc_event_callback) */
	}

createbss_cmd_fail:

	spin_unlock_bh(&pmlmepriv->lock);

	rtw_free_cmd_obj(pcmd);

}

void rtw_setstaKey_cmdrsp_callback(struct adapter *padapter,  struct cmd_obj *pcmd)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct set_stakey_rsp *psetstakey_rsp = (struct set_stakey_rsp *)(pcmd->rsp);
	struct sta_info *psta = rtw_get_stainfo(pstapriv, psetstakey_rsp->addr);

	if (!psta)
		goto exit;
exit:
	rtw_free_cmd_obj(pcmd);

}

void rtw_setassocsta_cmdrsp_callback(struct adapter *padapter,  struct cmd_obj *pcmd)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct set_assocsta_parm *passocsta_parm = (struct set_assocsta_parm *)(pcmd->parmbuf);
	struct set_assocsta_rsp *passocsta_rsp = (struct set_assocsta_rsp *)(pcmd->rsp);
	struct sta_info *psta = rtw_get_stainfo(pstapriv, passocsta_parm->addr);

	if (!psta)
		goto exit;

	psta->aid = passocsta_rsp->cam_id;
	psta->mac_id = passocsta_rsp->cam_id;

	spin_lock_bh(&pmlmepriv->lock);

	if (check_fwstate(pmlmepriv, WIFI_MP_STATE) && check_fwstate(pmlmepriv, _FW_UNDER_LINKING))
		_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);

	set_fwstate(pmlmepriv, _FW_LINKED);
	spin_unlock_bh(&pmlmepriv->lock);

exit:
	rtw_free_cmd_obj(pcmd);

}
