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
 *
 ******************************************************************************/
#define _RTW_CMD_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <cmd_osdep.h>
#include <mlme_osdep.h>
#include <rtw_cmd.h>
#include <usb_osintf.h>
/*
Caller and the rtw_cmd_thread can protect cmd_q by spin_lock.
No irqsave is necessary.
*/

int _rtw_init_cmd_priv(struct cmd_priv *pcmdpriv)
{
	int res = _SUCCESS;

	_rtw_init_sema(&(pcmdpriv->cmd_queue_sema), 0);
	_rtw_init_sema(&(pcmdpriv->terminate_cmdthread_sema), 0);

	_rtw_init_queue(&(pcmdpriv->cmd_queue));

	pcmdpriv->cmd_seq = 1;

	pcmdpriv->cmd_allocated_buf = kzalloc(MAX_CMDSZ + CMDBUFF_ALIGN_SZ, GFP_ATOMIC);

	if (pcmdpriv->cmd_allocated_buf == NULL) {
		res = _FAIL;
		goto exit;
	}

	pcmdpriv->cmd_buf = pcmdpriv->cmd_allocated_buf  +  CMDBUFF_ALIGN_SZ - ((SIZE_PTR)(pcmdpriv->cmd_allocated_buf) & (CMDBUFF_ALIGN_SZ-1));

	pcmdpriv->rsp_allocated_buf = kzalloc(MAX_RSPSZ + 4, GFP_ATOMIC);

	if (pcmdpriv->rsp_allocated_buf == NULL) {
		res = _FAIL;
		goto exit;
	}

	pcmdpriv->rsp_buf = pcmdpriv->rsp_allocated_buf  +  4 -
			    ((SIZE_PTR)(pcmdpriv->rsp_allocated_buf) & 3);

	pcmdpriv->cmd_issued_cnt = 0;
	pcmdpriv->cmd_done_cnt = 0;
	pcmdpriv->rsp_cnt = 0;

exit:
	return res;
}

#ifdef CONFIG_C2H_WK
static void c2h_wk_callback(_workitem *work);
#endif
int _rtw_init_evt_priv(struct evt_priv *pevtpriv)
{
	int res = _SUCCESS;

#ifdef CONFIG_H2CLBK
	_rtw_init_sema(&(pevtpriv->lbkevt_done), 0);
	pevtpriv->lbkevt_limit = 0;
	pevtpriv->lbkevt_num = 0;
	pevtpriv->cmdevt_parm = NULL;
#endif

	/* allocate DMA-able/Non-Page memory for cmd_buf and rsp_buf */
	ATOMIC_SET(&pevtpriv->event_seq, 0);
	pevtpriv->evt_done_cnt = 0;

#ifdef CONFIG_EVENT_THREAD_MODE

	_rtw_init_sema(&(pevtpriv->evt_notify), 0);
	_rtw_init_sema(&(pevtpriv->terminate_evtthread_sema), 0);

	pevtpriv->evt_allocated_buf = kzalloc(MAX_EVTSZ + 4, GFP_ATOMIC);
	if (pevtpriv->evt_allocated_buf == NULL) {
		res = _FAIL;
		goto exit;
		}
	pevtpriv->evt_buf = pevtpriv->evt_allocated_buf  +  4 - ((unsigned int)(pevtpriv->evt_allocated_buf) & 3);

	_rtw_init_queue(&(pevtpriv->evt_queue));

exit:

#endif /* end of CONFIG_EVENT_THREAD_MODE */

#ifdef CONFIG_C2H_WK
	_init_workitem(&pevtpriv->c2h_wk, c2h_wk_callback, NULL);
	pevtpriv->c2h_wk_alive = false;
	pevtpriv->c2h_queue = rtw_cbuf_alloc(C2H_QUEUE_MAX_LEN+1);
#endif
	return res;
}

void _rtw_free_evt_priv(struct evt_priv *pevtpriv)
{

	RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, ("+_rtw_free_evt_priv\n"));

#ifdef CONFIG_EVENT_THREAD_MODE
	_rtw_free_sema(&(pevtpriv->evt_notify));
	_rtw_free_sema(&(pevtpriv->terminate_evtthread_sema));

	kfree(pevtpriv->evt_allocated_buf);
#endif

#ifdef CONFIG_C2H_WK
	_cancel_workitem_sync(&pevtpriv->c2h_wk);
	while (pevtpriv->c2h_wk_alive)
		rtw_msleep_os(10);

	while (!rtw_cbuf_empty(pevtpriv->c2h_queue)) {
		void *c2h = rtw_cbuf_pop(pevtpriv->c2h_queue);
		if (c2h != NULL && c2h != (void *)pevtpriv)
			kfree(c2h);
	}
#endif

	RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, ("-_rtw_free_evt_priv\n"));

}

void _rtw_free_cmd_priv(struct cmd_priv *pcmdpriv)
{

	if (pcmdpriv) {
		_rtw_spinlock_free(&(pcmdpriv->cmd_queue.lock));
		_rtw_free_sema(&(pcmdpriv->cmd_queue_sema));
		_rtw_free_sema(&(pcmdpriv->terminate_cmdthread_sema));
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

int	_rtw_enqueue_cmd(struct __queue *queue, struct cmd_obj *obj)
{
	long unsigned int flags;

	if (obj == NULL)
		goto exit;

	spin_lock_irqsave(&queue->lock, flags);

	rtw_list_insert_tail(&obj->list, &queue->queue);

	spin_unlock_irqrestore(&queue->lock, flags);

exit:
	return _SUCCESS;
}

struct cmd_obj *_rtw_dequeue_cmd(struct __queue *queue)
{
	long unsigned int flags;
	struct cmd_obj *obj;

	spin_lock_irqsave(&queue->lock, flags);
	if (rtw_is_list_empty(&(queue->queue))) {
		obj = NULL;
	} else {
		obj = container_of((&queue->queue)->next, struct cmd_obj, list);
		list_del_init(&obj->list);
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

u32	rtw_init_evt_priv(struct evt_priv *pevtpriv)
{
	int	res;

	res = _rtw_init_evt_priv(pevtpriv);

	return res;
}

void rtw_free_evt_priv(struct evt_priv *pevtpriv)
{

	RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, ("rtw_free_evt_priv\n"));
	_rtw_free_evt_priv(pevtpriv);

}

void rtw_free_cmd_priv(struct cmd_priv *pcmdpriv)
{

	RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, ("rtw_free_cmd_priv\n"));
	_rtw_free_cmd_priv(pcmdpriv);

}

static int rtw_cmd_filter(struct cmd_priv *pcmdpriv, struct cmd_obj *cmd_obj)
{
	u8 allow = false; /* set to true to allow enqueuing cmd when hw_init_completed is false */

	if (cmd_obj->cmdcode == GEN_CMD_CODE(_SETCHANNELPLAN))
		allow = true;

	if ((pcmdpriv->padapter->hw_init_completed == false && allow == false) ||
	    pcmdpriv->cmdthd_running == false)	/* com_thread not running */
		return _FAIL;
	return _SUCCESS;
}

u32 rtw_enqueue_cmd(struct cmd_priv *pcmdpriv, struct cmd_obj *cmd_obj)
{
	int res = _FAIL;
	struct rtw_adapter *padapter = pcmdpriv->padapter;

	if (cmd_obj == NULL) {
		goto exit;
	}

	cmd_obj->padapter = padapter;

#ifdef CONFIG_CONCURRENT_MODE
	/* change pcmdpriv to primary's pcmdpriv */
	if (padapter->adapter_type != PRIMARY_ADAPTER && padapter->pbuddy_adapter)
		pcmdpriv = &(padapter->pbuddy_adapter->cmdpriv);
#endif

	res = rtw_cmd_filter(pcmdpriv, cmd_obj);
	if (_FAIL == res) {
		rtw_free_cmd_obj(cmd_obj);
		goto exit;
	}

	res = _rtw_enqueue_cmd(&pcmdpriv->cmd_queue, cmd_obj);

	if (res == _SUCCESS)
		_rtw_up_sema(&pcmdpriv->cmd_queue_sema);

exit:
	return res;
}

struct cmd_obj *rtw_dequeue_cmd(struct cmd_priv *pcmdpriv)
{
	struct cmd_obj *cmd_obj;

	cmd_obj = _rtw_dequeue_cmd(&pcmdpriv->cmd_queue);

	return cmd_obj;
}

void rtw_cmd_clr_isr(struct cmd_priv *pcmdpriv)
{

	pcmdpriv->cmd_done_cnt++;

}

void rtw_free_cmd_obj(struct cmd_obj *pcmd)
{

	if ((pcmd->cmdcode != _JOINBSS_CMD_) &&
	    (pcmd->cmdcode != _CREATEBSS_CMD_)) {
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

int rtw_cmd_thread(void *context)
{
	u8 ret;
	struct cmd_obj *pcmd;
	u8 *pcmdbuf;
	u8 (*cmd_hdl)(struct rtw_adapter *padapter, u8 *pbuf);
	void (*pcmd_callback)(struct rtw_adapter *dev, struct cmd_obj *pcmd);
	struct rtw_adapter *padapter = (struct rtw_adapter *)context;
	struct cmd_priv *pcmdpriv = &(padapter->cmdpriv);

	thread_enter("RTW_CMD_THREAD");

	pcmdbuf = pcmdpriv->cmd_buf;

	pcmdpriv->cmdthd_running = true;
	_rtw_up_sema(&pcmdpriv->terminate_cmdthread_sema);

	RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, ("start r871x rtw_cmd_thread !!!!\n"));

	while (1) {
		if ((_rtw_down_sema(&(pcmdpriv->cmd_queue_sema))) == _FAIL)
			break;

#ifdef CONFIG_LPS_LCLK
		if (rtw_register_cmd_alive(padapter) != _SUCCESS)
			continue;
#endif

_next:
		if ((padapter->bDriverStopped == true) ||
		    (padapter->bSurpriseRemoved == true)) {
			DBG_8192D("###> rtw_cmd_thread break.................\n");
			RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_,
				 ("rtw_cmd_thread:bDriverStopped(%d) OR bSurpriseRemoved(%d)",
				 padapter->bDriverStopped,
				 padapter->bSurpriseRemoved));
			break;
		}

		pcmd = rtw_dequeue_cmd(pcmdpriv);
		if (!pcmd) {
#ifdef CONFIG_LPS_LCLK
			rtw_unregister_cmd_alive(padapter);
#endif
			continue;
		}

		if (_FAIL == rtw_cmd_filter(pcmdpriv, pcmd)) {
			pcmd->res = H2C_DROPPED;
			goto post_process;
		}

		if (_FAIL == rtw_cmd_filter(pcmdpriv, pcmd)) {
			rtw_free_cmd_obj(pcmd);
			continue;
		}

		pcmdpriv->cmd_issued_cnt++;

		pcmd->cmdsz = _RND4((pcmd->cmdsz));/* _RND4 */

		memcpy(pcmdbuf, pcmd->parmbuf, pcmd->cmdsz);

		if (pcmd->cmdcode < (sizeof(wlancmds) / sizeof(struct cmd_hdl))) {
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
		if (pcmd->cmdcode < (sizeof(rtw_cmd_callback) / sizeof(struct _cmd_callback))) {
			pcmd_callback = rtw_cmd_callback[pcmd->cmdcode].callback;
			if (pcmd_callback == NULL) {
				RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, ("mlme_cmd_hdl(): pcmd_callback = 0x%p, cmdcode = 0x%x\n", pcmd_callback, pcmd->cmdcode));
				rtw_free_cmd_obj(pcmd);
			} else {
				/* todo: !!! fill rsp_buf to pcmd->rsp if (pcmd->rsp!= NULL) */
				pcmd_callback(pcmd->padapter, pcmd);/* need conider that free cmd_obj in rtw_cmd_callback */
			}
		}

		flush_signals_thread();

		goto _next;
	}
	pcmdpriv->cmdthd_running = false;

	/*  free all cmd_obj resources */
	do {
		pcmd = rtw_dequeue_cmd(pcmdpriv);
		if (pcmd == NULL)
			break;

		rtw_free_cmd_obj(pcmd);
	} while (1);

	_rtw_up_sema(&pcmdpriv->terminate_cmdthread_sema);

	thread_exit();
}

#ifdef CONFIG_EVENT_THREAD_MODE
u32 rtw_enqueue_evt(struct evt_priv *pevtpriv, struct evt_obj *obj)
{
	int	res;
	struct __queue *queue = &pevtpriv->evt_queue;

	res = _SUCCESS;

	if (obj == NULL) {
		res = _FAIL;
		goto exit;
	}

	spin_lock_bh(&queue->lock);

	rtw_list_insert_tail(&obj->list, &queue->queue);

	spin_unlock_bh(&queue->lock);

exit:
	return res;
}

struct evt_obj *rtw_dequeue_evt(_queue *queue)
{
	struct evt_obj *pevtobj;

	spin_lock_bh(&queue->lock);

	if (rtw_is_list_empty(&(queue->queue))) {
		pevtobj = NULL;
	} else {
		pevtobj = container_of(&(queue->queue->next; struct evt_obj, list);
		list_del_init(&pevtobj->list);
	}

	spin_unlock_bh(&queue->lock);

	return pevtobj;
}

void rtw_free_evt_obj(struct evt_obj *pevtobj)
{

	kfree(pevtobj->parmbuf);
	kfree(pevtobj);

}

void rtw_evt_notify_isr(struct evt_priv *pevtpriv)
{

	pevtpriv->evt_done_cnt++;
	_rtw_up_sema(&(pevtpriv->evt_notify));

}
#endif

u8 rtw_setstandby_cmd(struct rtw_adapter *padapter, uint action)
{
	struct cmd_obj *ph2c;
	struct usb_suspend_parm *psetusbsuspend;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;

	u8 ret = _SUCCESS;

	ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (ph2c == NULL) {
		ret = _FAIL;
		goto exit;
	}

	psetusbsuspend = (struct usb_suspend_parm *)kzalloc(sizeof(struct usb_suspend_parm), GFP_ATOMIC);
	if (psetusbsuspend == NULL) {
		kfree(ph2c);
		ret = _FAIL;
		goto exit;
	}

	psetusbsuspend->action = action;

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetusbsuspend, GEN_CMD_CODE(_SETUSBSUSPEND));

	ret = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:
	return ret;
}

/*
rtw_sitesurvey_cmd(~)
	### NOTE:#### (!!!!)
	MUST TAKE CARE THAT BEFORE CALLING THIS FUNC, YOU SHOULD HAVE LOCKED pmlmepriv->lock
*/
u8 rtw_sitesurvey_cmd(struct rtw_adapter *padapter, struct ndis_802_11_ssid *ssid, int ssid_num,
	struct rtw_ieee80211_channel *ch, int ch_num)
{
	u8 res = _FAIL;
	struct cmd_obj *ph2c;
	struct sitesurvey_parm *psurveyPara;
	struct cmd_priv		*pcmdpriv = &padapter->cmdpriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;

#ifdef CONFIG_LPS
	if (check_fwstate(pmlmepriv, _FW_LINKED) == true) {
		rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_SCAN, 1);
	}
#endif

	ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (ph2c == NULL)
		return _FAIL;

	psurveyPara = (struct sitesurvey_parm *)kzalloc(sizeof(struct sitesurvey_parm), GFP_ATOMIC);
	if (psurveyPara == NULL) {
		kfree(ph2c);
		return _FAIL;
	}

	rtw_free_network_queue(padapter, false);

	RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, ("\nflush  network queue\n\n"));

	init_h2fwcmd_w_parm_no_rsp(ph2c, psurveyPara, GEN_CMD_CODE(_SITESURVEY));

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
		pmlmepriv->scan_start_time = rtw_get_current_time();

#ifdef CONFIG_STA_MODE_SCAN_UNDER_AP_MODE
		if ((padapter->pbuddy_adapter->mlmeextpriv.mlmext_info.state&0x03) == WIFI_FW_AP_STATE)
			_set_timer(&pmlmepriv->scan_to_timer, SURVEY_TO * (38 + (38 / RTW_SCAN_NUM_OF_CH) * RTW_STAY_AP_CH_MILLISECOND) + 1000);
		else
#endif /* CONFIG_STA_MODE_SCAN_UNDER_AP_MODE */
			_set_timer(&pmlmepriv->scan_to_timer, SCANNING_TIMEOUT);

		rtw_led_control(padapter, LED_CTL_SITE_SURVEY);

		pmlmepriv->scan_interval = SCAN_INTERVAL;/*  30*2 sec = 60sec */
	} else {
		_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);
	}

	return res;
}

u8 rtw_setdatarate_cmd(struct rtw_adapter *padapter, u8 *rateset)
{
	struct cmd_obj *ph2c;
	struct setdatarate_parm *pbsetdataratepara;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pbsetdataratepara = (struct setdatarate_parm *)kzalloc(sizeof(struct setdatarate_parm), GFP_ATOMIC);
	if (pbsetdataratepara == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, pbsetdataratepara, GEN_CMD_CODE(_SETDATARATE));
#ifdef MP_FIRMWARE_OFFLOAD
	pbsetdataratepara->curr_rateidx = *(u32 *)rateset;
#else
	pbsetdataratepara->mac_id = 5;
	memcpy(pbsetdataratepara->datarates, rateset, NUMRATES);
#endif
	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:
	return res;
}

u8 rtw_setbasicrate_cmd(struct rtw_adapter *padapter, u8 *rateset)
{
	struct cmd_obj *ph2c;
	struct setbasicrate_parm *pssetbasicratepara;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}
	pssetbasicratepara = (struct setbasicrate_parm *)kzalloc(sizeof(struct setbasicrate_parm), GFP_ATOMIC);

	if (pssetbasicratepara == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, pssetbasicratepara, _SETBASICRATE_CMD_);

	memcpy(pssetbasicratepara->basicrates, rateset, NUMRATES);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:
	return res;
}

/*
unsigned char rtw_setphy_cmd(unsigned char  *adapter)

1.  be called only after rtw_update_registrypriv_dev_network(~) or mp testing program
2.  for AdHoc/Ap mode or mp mode?

*/
u8 rtw_setphy_cmd(struct rtw_adapter *padapter, u8 modem, u8 ch)
{
	struct cmd_obj *ph2c;
	struct setphy_parm *psetphypara;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}
	psetphypara = (struct setphy_parm *)kzalloc(sizeof(struct setphy_parm), GFP_ATOMIC);

	if (psetphypara == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetphypara, _SETPHY_CMD_);

	RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, ("CH =%d, modem =%d", ch, modem));

	psetphypara->modem = modem;
	psetphypara->rfchannel = ch;

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:

	return res;
}

u8 rtw_setbbreg_cmd(struct rtw_adapter *padapter, u8 offset, u8 val)
{
	struct cmd_obj *ph2c;
	struct writeBB_parm *pwritebbparm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
		}
	pwritebbparm = (struct writeBB_parm *)kzalloc(sizeof(struct writeBB_parm), GFP_ATOMIC);

	if (pwritebbparm == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, pwritebbparm, GEN_CMD_CODE(_SETBBREG));

	pwritebbparm->offset = offset;
	pwritebbparm->value = val;

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:

	return res;
}

u8 rtw_getbbreg_cmd(struct rtw_adapter *padapter, u8 offset, u8 *pval)
{
	struct cmd_obj *ph2c;
	struct readBB_parm *prdbbparm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}
	prdbbparm = (struct readBB_parm *)kzalloc(sizeof(struct readBB_parm), GFP_ATOMIC);

	if (prdbbparm == NULL) {
		kfree(ph2c);
		return _FAIL;
	}

	INIT_LIST_HEAD(&ph2c->list);
	ph2c->cmdcode = GEN_CMD_CODE(_GETBBREG);
	ph2c->parmbuf = (unsigned char *)prdbbparm;
	ph2c->cmdsz =  sizeof(struct readBB_parm);
	ph2c->rsp = pval;
	ph2c->rspsz = sizeof(struct readBB_rsp);

	prdbbparm->offset = offset;

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:

	return res;
}

u8 rtw_setrfreg_cmd(struct rtw_adapter *padapter, u8 offset, u32 val)
{
	struct cmd_obj *ph2c;
	struct writeRF_parm *pwriterfparm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}
	pwriterfparm = (struct writeRF_parm *)kzalloc(sizeof(struct writeRF_parm), GFP_ATOMIC);

	if (pwriterfparm == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, pwriterfparm, GEN_CMD_CODE(_SETRFREG));

	pwriterfparm->offset = offset;
	pwriterfparm->value = val;

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:

	return res;
}

u8 rtw_getrfreg_cmd(struct rtw_adapter *padapter, u8 offset, u8 *pval)
{
	struct cmd_obj *ph2c;
	struct readRF_parm *prdrfparm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	prdrfparm = (struct readRF_parm *)kzalloc(sizeof(struct readRF_parm), GFP_ATOMIC);
	if (prdrfparm == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	INIT_LIST_HEAD(&ph2c->list);
	ph2c->cmdcode = GEN_CMD_CODE(_GETRFREG);
	ph2c->parmbuf = (unsigned char *)prdrfparm;
	ph2c->cmdsz =  sizeof(struct readRF_parm);
	ph2c->rsp = pval;
	ph2c->rspsz = sizeof(struct readRF_rsp);

	prdrfparm->offset = offset;

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;
}

void rtw_getbbrfreg_cmdrsp_callback(struct rtw_adapter *padapter,  struct cmd_obj *pcmd)
{

	kfree(pcmd->parmbuf);
	kfree(pcmd);

}

void rtw_readtssi_cmdrsp_callback(struct rtw_adapter *padapter,  struct cmd_obj *pcmd)
{

	kfree(pcmd->parmbuf);
	kfree(pcmd);

}

u8 rtw_createbss_cmd(struct rtw_adapter *padapter)
{
	struct cmd_obj *pcmd;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_bssid_ex *pdev_network = &padapter->registrypriv.dev_network;
	u8 res = _SUCCESS;

	rtw_led_control(padapter, LED_CTL_START_TO_LINK);

	if (pmlmepriv->assoc_ssid.SsidLength == 0) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, (" createbss for Any SSid:%s\n", pmlmepriv->assoc_ssid.Ssid));
	} else {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, (" createbss for SSid:%s\n", pmlmepriv->assoc_ssid.Ssid));
	}

	pcmd = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (pcmd == NULL) {
		res = _FAIL;
		goto exit;
	}

	INIT_LIST_HEAD(&pcmd->list);
	pcmd->cmdcode = _CREATEBSS_CMD_;
	pcmd->parmbuf = (unsigned char *)pdev_network;
	pcmd->cmdsz = get_wlan_bssid_ex_sz((struct wlan_bssid_ex *)pdev_network);
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	pdev_network->Length = pcmd->cmdsz;

	res = rtw_enqueue_cmd(pcmdpriv, pcmd);
exit:

	return res;
}

u8 rtw_createbss_cmd_ex(struct rtw_adapter *padapter, unsigned char *pbss, unsigned int sz)
{
	struct cmd_obj *pcmd;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	pcmd = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (pcmd == NULL) {
		res = _FAIL;
		goto exit;
	}

	INIT_LIST_HEAD(&pcmd->list);
	pcmd->cmdcode = GEN_CMD_CODE(_CREATEBSS);
	pcmd->parmbuf = pbss;
	pcmd->cmdsz =  sz;
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	res = rtw_enqueue_cmd(pcmdpriv, pcmd);

exit:

	return res;
}

u8 rtw_joinbss_cmd(struct rtw_adapter *padapter, struct wlan_network *pnetwork)
{
	u8 res = _SUCCESS;
	uint	t_len = 0;
	struct wlan_bssid_ex	*psecnetwork;
	struct cmd_obj *pcmd;
	struct cmd_priv		*pcmdpriv = &padapter->cmdpriv;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct qos_priv		*pqospriv = &pmlmepriv->qospriv;
	struct security_priv	*psecuritypriv = &padapter->securitypriv;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	struct ht_priv			*phtpriv = &pmlmepriv->htpriv;
	enum NDIS_802_11_NETWORK_INFRASTRUCTURE ndis_network_mode = pnetwork->network.InfrastructureMode;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	rtw_led_control(padapter, LED_CTL_START_TO_LINK);

	if (pmlmepriv->assoc_ssid.SsidLength == 0) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_, ("+Join cmd: Any SSid\n"));
	} else {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_notice_, ("+Join cmd: SSid =[%s]\n", pmlmepriv->assoc_ssid.Ssid));
	}

	pcmd = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
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
		case NDIS802_11IBSS:
			set_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
			break;
		case NDIS802_11INFRA:
			set_fwstate(pmlmepriv, WIFI_STATION_STATE);
			break;
		case NDIS802_11APMODE:
		case NDIS802_11AUTOUNK:
		case NDIS802_11INFRA_MAX:
			break;
		}
	}

	psecnetwork = (struct wlan_bssid_ex *)&psecuritypriv->sec_bss;
	if (psecnetwork == NULL) {
		kfree(pcmd);
		res = _FAIL;
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("rtw_joinbss_cmd :psecnetwork == NULL!!!\n"));
		goto exit;
	}

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
	/*  If the the driver wants to use the bssid to create the connection. */
	/*  If not,  we have to copy the connecting AP's MAC address to it so that */
	/*  the driver just has the bssid information for PMKIDList searching. */

	if (pmlmepriv->assoc_by_bssid == false)
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

#ifdef CONFIG_80211N_HT
	phtpriv->ht_option = false;
	if (pregistrypriv->ht_enable) {
		/*	Added by Albert 2010/06/23 */
		/*	For the WEP mode, we will use the bg mode to do the connection to avoid some IOT issue. */
		/*	Especially for Realtek 8192u SoftAP. */
		if ((padapter->securitypriv.dot11PrivacyAlgrthm != _WEP40_) &&
		    (padapter->securitypriv.dot11PrivacyAlgrthm != _WEP104_) &&
		    (padapter->securitypriv.dot11PrivacyAlgrthm != _TKIP_)) {
			/* rtw_restructure_ht_ie */
			rtw_restructure_ht_ie(padapter, &pnetwork->network.IEs[0],
					      &psecnetwork->IEs[0],
					      pnetwork->network.IELength,
					      &psecnetwork->IELength,
					      (u8)psecnetwork->Configuration.DSConfig);
		}
	}

#endif

	pmlmeinfo->assoc_AP_vendor = check_assoc_AP(pnetwork->network.IEs, pnetwork->network.IELength);

	pcmd->cmdsz = get_wlan_bssid_ex_sz(psecnetwork);/* get cmdsz before endian conversion */

	INIT_LIST_HEAD(&pcmd->list);
	pcmd->cmdcode = _JOINBSS_CMD_;/* GEN_CMD_CODE(_JoinBss) */
	pcmd->parmbuf = (unsigned char *)psecnetwork;
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	res = rtw_enqueue_cmd(pcmdpriv, pcmd);

exit:

	return res;
}

u8 rtw_disassoc_cmd(struct rtw_adapter *padapter, u32 deauth_timeout_ms, bool enqueue) /* for sta_mode */
{
	struct cmd_obj *cmdobj = NULL;
	struct disconnect_parm *param = NULL;
	struct cmd_priv *cmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	RT_TRACE(_module_rtl871x_cmd_c_, _drv_notice_, ("+rtw_disassoc_cmd\n"));

	/* prepare cmd parameter */
	param = (struct disconnect_parm *)kzalloc(sizeof(*param), GFP_ATOMIC);
	if (param == NULL) {
		res = _FAIL;
		goto exit;
	}
	param->deauth_timeout_ms = deauth_timeout_ms;

	if (enqueue) {
		/* need enqueue, prepare cmd_obj and enqueue */
		cmdobj = (struct cmd_obj *)kzalloc(sizeof(*cmdobj), GFP_ATOMIC);
		if (cmdobj == NULL) {
			res = _FAIL;
			kfree(param);
			goto exit;
		}
		init_h2fwcmd_w_parm_no_rsp(cmdobj, param, _DISCONNECT_CMD_);
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

u8 rtw_setopmode_cmd(struct rtw_adapter *padapter, enum NDIS_802_11_NETWORK_INFRASTRUCTURE networktype)
{
	struct cmd_obj *ph2c;
	struct setopmode_parm *psetop;

	struct cmd_priv   *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (ph2c == NULL) {
		res = false;
		goto exit;
	}
	psetop = (struct setopmode_parm *)kzalloc(sizeof(struct setopmode_parm), GFP_ATOMIC);
	if (psetop == NULL) {
		kfree(ph2c);
		res = false;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetop, _SETOPMODE_CMD_);
	psetop->mode = (u8)networktype;

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;
}

u8 rtw_setstakey_cmd(struct rtw_adapter *padapter, u8 *psta, u8 unicast_key)
{
	struct cmd_obj *ph2c;
	struct set_stakey_parm *psetstakey_para;
	struct cmd_priv				*pcmdpriv = &padapter->cmdpriv;
	struct set_stakey_rsp		*psetstakey_rsp = NULL;

	struct mlme_priv			*pmlmepriv = &padapter->mlmepriv;
	struct security_priv		*psecuritypriv = &padapter->securitypriv;
	struct sta_info *sta = (struct sta_info *)psta;
	u8 res = _SUCCESS;

	ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	psetstakey_para = (struct set_stakey_parm *)kzalloc(sizeof(struct set_stakey_parm), GFP_ATOMIC);
	if (psetstakey_para == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	psetstakey_rsp = (struct set_stakey_rsp *)kzalloc(sizeof(struct set_stakey_rsp), GFP_ATOMIC);
	if (psetstakey_rsp == NULL) {
		kfree(ph2c);
		kfree(psetstakey_para);
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetstakey_para, _SETSTAKEY_CMD_);
	ph2c->rsp = (u8 *)psetstakey_rsp;
	ph2c->rspsz = sizeof(struct set_stakey_rsp);

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

	/* jeff: set this becasue at least sw key is ready */
	padapter->securitypriv.busetkipkey = true;

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;
}

u8 rtw_clearstakey_cmd(struct rtw_adapter *padapter, u8 *psta, u8 entry, u8 enqueue)
{
	struct cmd_obj *ph2c;
	struct set_stakey_parm *psetstakey_para;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	struct set_stakey_rsp *psetstakey_rsp = NULL;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct sta_info *sta = (struct sta_info *)psta;
	u8 res = _SUCCESS;

	if (!enqueue) {
		clear_cam_entry(padapter, entry);
	} else {
		ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
		if (ph2c == NULL) {
			res = _FAIL;
			goto exit;
		}

		psetstakey_para = (struct set_stakey_parm *)kzalloc(sizeof(struct set_stakey_parm), GFP_ATOMIC);
		if (psetstakey_para == NULL) {
			kfree(ph2c);
			res = _FAIL;
			goto exit;
		}

		psetstakey_rsp = (struct set_stakey_rsp *)kzalloc(sizeof(struct set_stakey_rsp), GFP_ATOMIC);
		if (psetstakey_rsp == NULL) {
			kfree(ph2c);
			kfree(psetstakey_para);
			res = _FAIL;
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(ph2c, psetstakey_para, _SETSTAKEY_CMD_);
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

u8 rtw_setrttbl_cmd(struct rtw_adapter *padapter, struct setratable_parm *prate_table)
{
	struct cmd_obj *ph2c;
	struct setratable_parm *psetrttblparm;
	struct cmd_priv				*pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
		}
	psetrttblparm = (struct setratable_parm *)kzalloc(sizeof(struct setratable_parm), GFP_ATOMIC);

	if (psetrttblparm == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetrttblparm, GEN_CMD_CODE(_SETRATABLE));

	memcpy(psetrttblparm, prate_table, sizeof(struct setratable_parm));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:

	return res;
}

u8 rtw_getrttbl_cmd(struct rtw_adapter *padapter, struct getratable_rsp *pval)
{
	struct cmd_obj *ph2c;
	struct getratable_parm *pgetrttblparm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}
	pgetrttblparm = (struct getratable_parm *)kzalloc(sizeof(struct getratable_parm), GFP_ATOMIC);

	if (pgetrttblparm == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	INIT_LIST_HEAD(&ph2c->list);
	ph2c->cmdcode = GEN_CMD_CODE(_GETRATABLE);
	ph2c->parmbuf = (unsigned char *)pgetrttblparm;
	ph2c->cmdsz =  sizeof(struct getratable_parm);
	ph2c->rsp = (u8 *)pval;
	ph2c->rspsz = sizeof(struct getratable_rsp);

	pgetrttblparm->rsvd = 0x0;

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:

	return res;
}

u8 rtw_setassocsta_cmd(struct rtw_adapter *padapter, u8 *mac_addr)
{
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	struct cmd_obj *ph2c;
	struct set_assocsta_parm *psetassocsta_para;
	struct set_stakey_rsp		*psetassocsta_rsp = NULL;

	u8 res = _SUCCESS;

	ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	psetassocsta_para = (struct set_assocsta_parm *)kzalloc(sizeof(struct set_assocsta_parm), GFP_ATOMIC);
	if (psetassocsta_para == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	psetassocsta_rsp = (struct set_stakey_rsp *)kzalloc(sizeof(struct set_assocsta_rsp), GFP_ATOMIC);
	if (psetassocsta_rsp == NULL) {
		kfree(ph2c);
		kfree(psetassocsta_para);
		return _FAIL;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetassocsta_para, _SETASSOCSTA_CMD_);
	ph2c->rsp = (u8 *)psetassocsta_rsp;
	ph2c->rspsz = sizeof(struct set_assocsta_rsp);

	memcpy(psetassocsta_para->addr, mac_addr, ETH_ALEN);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;
 }

u8 rtw_addbareq_cmd(struct rtw_adapter *padapter, u8 tid, u8 *addr)
{
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	struct cmd_obj *ph2c;
	struct addBaReq_parm *paddbareq_parm;

	u8 res = _SUCCESS;

	ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	paddbareq_parm = (struct addBaReq_parm *)kzalloc(sizeof(struct addBaReq_parm), GFP_ATOMIC);
	if (paddbareq_parm == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	paddbareq_parm->tid = tid;
	memcpy(paddbareq_parm->addr, addr, ETH_ALEN);

	init_h2fwcmd_w_parm_no_rsp(ph2c, paddbareq_parm, GEN_CMD_CODE(_ADDBAREQ));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;
}

u8 rtw_dynamic_chk_wk_cmd(struct rtw_adapter *padapter)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm  *pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	if ((padapter->bDriverStopped == true) ||
	    (padapter->bSurpriseRemoved == true))
		goto exit;

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->adapter_type != PRIMARY_ADAPTER && padapter->pbuddy_adapter)
		pcmdpriv = &(padapter->pbuddy_adapter->cmdpriv);
#endif

	ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)kzalloc(sizeof(struct drvextra_cmd_parm), GFP_ATOMIC);
	if (pdrvextra_cmd_parm == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = DYNAMIC_CHK_WK_CID;
	pdrvextra_cmd_parm->type_size = 0;
	pdrvextra_cmd_parm->pbuf = (u8 *)padapter;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_SET_DRV_EXTRA));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:

	return res;
}

u8 rtw_set_ch_cmd(struct rtw_adapter *padapter, u8 ch, u8 bw, u8 ch_offset, u8 enqueue)
{
	struct cmd_obj *pcmdobj;
	struct set_ch_parm *set_ch_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	u8 res = _SUCCESS;

	DBG_8192D(FUNC_NDEV_FMT" ch:%u, bw:%u, ch_offset:%u\n",
		  FUNC_NDEV_ARG(padapter->pnetdev), ch, bw, ch_offset);

	/* check input parameter */

	/* prepare cmd parameter */
	set_ch_parm = (struct set_ch_parm *)kzalloc(sizeof(*set_ch_parm), GFP_ATOMIC);
	if (set_ch_parm == NULL) {
		res = _FAIL;
		goto exit;
	}
	set_ch_parm->ch = ch;
	set_ch_parm->bw = bw;
	set_ch_parm->ch_offset = ch_offset;

	if (enqueue) {
		/* need enqueue, prepare cmd_obj and enqueue */
		pcmdobj = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
		if (pcmdobj == NULL) {
			kfree(set_ch_parm);
			res = _FAIL;
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(pcmdobj, set_ch_parm, GEN_CMD_CODE(_SETCHANNEL));
		res = rtw_enqueue_cmd(pcmdpriv, pcmdobj);
	} else {
		/* no need to enqueue, do the cmd hdl directly and free cmd parameter */
		if (H2C_SUCCESS != set_ch_hdl(padapter, (u8 *)set_ch_parm))
			res = _FAIL;

		kfree(set_ch_parm);
	}

	/* do something based on res... */

exit:

	DBG_8192D(FUNC_NDEV_FMT" res:%u\n", FUNC_NDEV_ARG(padapter->pnetdev), res);

	return res;
}

u8 rtw_set_chplan_cmd(struct rtw_adapter *padapter, u8 chplan, u8 enqueue)
{
	struct cmd_obj *pcmdobj;
	struct setchannelplan_param *setchannelplan_param;
	struct cmd_priv   *pcmdpriv = &padapter->cmdpriv;

	u8 res = _SUCCESS;

	RT_TRACE(_module_rtl871x_cmd_c_, _drv_notice_, ("+rtw_set_chplan_cmd\n"));

	/* check input parameter */
	if (!rtw_is_channel_plan_valid(chplan)) {
		res = _FAIL;
		goto exit;
	}

	/* prepare cmd parameter */
	setchannelplan_param = (struct setchannelplan_param *)kzalloc(sizeof(struct setchannelplan_param), GFP_ATOMIC);
	if (setchannelplan_param == NULL) {
		res = _FAIL;
		goto exit;
	}
	setchannelplan_param->channel_plan = chplan;

	if (enqueue) {
		/* need enqueue, prepare cmd_obj and enqueue */
		pcmdobj = (struct cmd_obj *)kzalloc(sizeof(struct	cmd_obj), GFP_ATOMIC);
		if (pcmdobj == NULL) {
			kfree(setchannelplan_param);
			res = _FAIL;
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(pcmdobj, setchannelplan_param, GEN_CMD_CODE(_SETCHANNELPLAN));
		res = rtw_enqueue_cmd(pcmdpriv, pcmdobj);
	} else {
		/* no need to enqueue, do the cmd hdl directly and free cmd parameter */
		if (H2C_SUCCESS != set_chplan_hdl(padapter, (unsigned char *)setchannelplan_param))
			res = _FAIL;

		kfree(setchannelplan_param);
	}

	/* do something based on res... */
	if (res == _SUCCESS)
		padapter->mlmepriv.ChannelPlan = chplan;

exit:

	return res;
}

u8 rtw_led_blink_cmd(struct rtw_adapter *padapter, struct LED_871X *pLed)
{
	struct cmd_obj *pcmdobj;
	struct ledblink_param *ledBlink_param;
	struct cmd_priv   *pcmdpriv = &padapter->cmdpriv;

	u8 res = _SUCCESS;

	RT_TRACE(_module_rtl871x_cmd_c_, _drv_notice_, ("+rtw_led_blink_cmd\n"));

	pcmdobj = (struct cmd_obj *)kzalloc(sizeof(struct	cmd_obj), GFP_ATOMIC);
	if (pcmdobj == NULL) {
		res = _FAIL;
		goto exit;
	}

	ledBlink_param = (struct ledblink_param *)kzalloc(sizeof(struct	ledblink_param), GFP_ATOMIC);
	if (ledBlink_param == NULL) {
		kfree(pcmdobj);
		res = _FAIL;
		goto exit;
	}

	ledBlink_param->pLed = pLed;

	init_h2fwcmd_w_parm_no_rsp(pcmdobj, ledBlink_param, GEN_CMD_CODE(_LEDBLINK));
	res = rtw_enqueue_cmd(pcmdpriv, pcmdobj);

exit:

	return res;
}

u8 rtw_set_csa_cmd(struct rtw_adapter *padapter, u8 new_ch_no)
{
	struct cmd_obj *pcmdobj;
	struct setchannelswitch_param *setchannelswitch_param;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct cmd_priv   *pcmdpriv = &padapter->cmdpriv;

	u8 res = _SUCCESS;

	RT_TRACE(_module_rtl871x_cmd_c_, _drv_notice_, ("+rtw_set_csa_cmd\n"));

	pcmdobj = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (pcmdobj == NULL) {
		res = _FAIL;
		goto exit;
	}

	setchannelswitch_param = (struct setchannelswitch_param *)kzalloc(sizeof(struct setchannelswitch_param), GFP_ATOMIC);
	if (setchannelswitch_param == NULL) {
		kfree(pcmdobj);
		res = _FAIL;
		goto exit;
	}

	setchannelswitch_param->new_ch_no = new_ch_no;

	init_h2fwcmd_w_parm_no_rsp(pcmdobj, setchannelswitch_param, GEN_CMD_CODE(_SETCHANNELSWITCH));
	res = rtw_enqueue_cmd(pcmdpriv, pcmdobj);

exit:

	return res;
}

u8 rtw_tdls_cmd(struct rtw_adapter *padapter, u8 *addr, u8 option)
{
	struct cmd_obj *pcmdobj;
	struct TDLSoption_param	*TDLSoption;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct cmd_priv   *pcmdpriv = &padapter->cmdpriv;

	u8 res = _SUCCESS;

	return res;
}

static void traffic_status_watchdog(struct rtw_adapter *padapter)
{
#ifdef CONFIG_LPS
	u8 enterps;
#endif
	u8 bBusyTraffic = false, bTxBusyTraffic = false, bRxBusyTraffic = false;
	u8 bHigherBusyTraffic = false, bHigherBusyRxTraffic = false, bHigherBusyTxTraffic = false;
	struct mlme_priv		*pmlmepriv = &(padapter->mlmepriv);

	/*  Determine if our traffic is busy now */
	if ((check_fwstate(pmlmepriv, _FW_LINKED) == true)) {
		if (pmlmepriv->LinkDetectInfo.NumRxOkInPeriod > 100 ||
		    pmlmepriv->LinkDetectInfo.NumTxOkInPeriod > 100) {
			bBusyTraffic = true;

			if (pmlmepriv->LinkDetectInfo.NumRxOkInPeriod > 100)
				bRxBusyTraffic = true;

			if (pmlmepriv->LinkDetectInfo.NumTxOkInPeriod > 100)
				bTxBusyTraffic = true;
		}

		/*  Higher Tx/Rx data. */
		if (pmlmepriv->LinkDetectInfo.NumRxOkInPeriod > 4000 ||
		    pmlmepriv->LinkDetectInfo.NumTxOkInPeriod > 4000) {
			bHigherBusyTraffic = true;

			/*  Extremely high Rx data. */
			if (pmlmepriv->LinkDetectInfo.NumRxOkInPeriod > 5000)
				bHigherBusyRxTraffic = true;

			/*  Extremely high Tx data. */
			if (pmlmepriv->LinkDetectInfo.NumTxOkInPeriod > 5000)
				bHigherBusyTxTraffic = true;
		}

#ifdef CONFIG_LPS
		/*  check traffic for  powersaving. */
		if (((pmlmepriv->LinkDetectInfo.NumRxUnicastOkInPeriod + pmlmepriv->LinkDetectInfo.NumTxOkInPeriod) > 8) ||
		    (pmlmepriv->LinkDetectInfo.NumRxUnicastOkInPeriod > 2))
			enterps = false;
		else
			enterps = true;

		/*  LeisurePS only work in infra mode. */
		if (enterps)
			rtw_lps_enter(padapter);
		else
			rtw_lps_leave(padapter);
#endif
	} else {
#ifdef CONFIG_LPS
		rtw_lps_leave(padapter);
#endif
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

static void dynamic_chk_wk_hdl(struct rtw_adapter *padapter, u8 *pbuf, int sz)
{
	struct mlme_priv *pmlmepriv;

	if ((padapter->bDriverStopped == true) ||
	    (padapter->bSurpriseRemoved == true))
		return;

	if ((void *)padapter != (void *)pbuf && padapter->pbuddy_adapter == NULL)
		return;

	padapter = (struct rtw_adapter *)pbuf;

	if ((padapter->bDriverStopped == true) ||
	    (padapter->bSurpriseRemoved == true))
		return;

	pmlmepriv = &(padapter->mlmepriv);

#ifdef CONFIG_92D_AP_MODE
	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true)
		expire_timeout_chk(padapter);
#endif

	#ifdef DBG_CONFIG_ERROR_DETECT
	rtw_hal_sreset_xmit_status_check(padapter);
	#endif

	linked_status_chk(padapter);
	traffic_status_watchdog(padapter);

	rtw_hal_dm_watchdog(padapter);
}

#ifdef CONFIG_LPS

static void lps_ctrl_wk_hdl(struct rtw_adapter *padapter, u8 lps_ctrl_type)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	u8 mstatus;

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true))
		return;

	switch (lps_ctrl_type) {
	case LPS_CTRL_SCAN:
		LeaveAllPowerSaveMode(padapter);
		break;
	case LPS_CTRL_JOINBSS:
		rtw_lps_leave(padapter);
		break;
	case LPS_CTRL_CONNECT:
		mstatus = 1;
		/*  Reset LPS Setting */
		padapter->pwrctrlpriv.LpsIdleCount = 0;
		rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_JOINBSSRPT, (u8 *)(&mstatus));
		break;
	case LPS_CTRL_DISCONNECT:
		mstatus = 0;
		rtw_lps_leave(padapter);
		rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_JOINBSSRPT, (u8 *)(&mstatus));
		break;
	case LPS_CTRL_SPECIAL_PACKET:
		pwrpriv->DelayLPSLastTimeStamp = rtw_get_current_time();
		rtw_lps_leave(padapter);
		break;

	default:
		break;
	}

}

u8 rtw_lps_ctrl_wk_cmd(struct rtw_adapter *padapter, u8 lps_ctrl_type, u8 enqueue)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->iface_type != IFACE_PORT0)
		return res;
#endif

	if (enqueue) {
		ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
		if (ph2c == NULL) {
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)kzalloc(sizeof(struct drvextra_cmd_parm), GFP_ATOMIC);
		if (pdrvextra_cmd_parm == NULL) {
			kfree(ph2c);
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm->ec_id = LPS_CTRL_WK_CID;
		pdrvextra_cmd_parm->type_size = lps_ctrl_type;
		pdrvextra_cmd_parm->pbuf = NULL;

		init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_SET_DRV_EXTRA));

		res = rtw_enqueue_cmd(pcmdpriv, ph2c);
	} else {
		lps_ctrl_wk_hdl(padapter, lps_ctrl_type);
	}

exit:

	return res;
}

#endif
#ifdef CONFIG_ANTENNA_DIVERSITY

void antenna_select_wk_hdl(struct rtw_adapter *padapter, u8 antenna)
{
	rtw_hal_set_hwreg(padapter, HW_VAR_ANTENNA_DIVERSITY_SELECT, (u8 *)(&antenna));
}

u8 rtw_antenna_select_cmd(struct rtw_adapter *padapter, u8 antenna, u8 enqueue)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8 support_ant_div = false;
	u8 res = _SUCCESS;

	rtw_hal_get_def_var(padapter, HAL_DEF_IS_SUPPORT_ANT_DIV, &(support_ant_div));
	if (false == support_ant_div)
		return res;

	if (true == enqueue) {
		ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
		if (ph2c == NULL) {
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)kzalloc(sizeof(struct drvextra_cmd_parm), GFP_ATOMIC);
		if (pdrvextra_cmd_parm == NULL) {
			kfree(ph2c);
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm->ec_id = ANT_SELECT_WK_CID;
		pdrvextra_cmd_parm->type_size = antenna;
		pdrvextra_cmd_parm->pbuf = NULL;
		init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_SET_DRV_EXTRA));

		res = rtw_enqueue_cmd(pcmdpriv, ph2c);
	} else {
		antenna_select_wk_hdl(padapter, antenna);
	}
exit:

	return res;
}
#endif

static void power_saving_wk_hdl(struct rtw_adapter *padapter, u8 *pbuf, int sz)
{
	 rtw_ps_processor(padapter);
}

u8 rtw_ps_cmd(struct rtw_adapter *padapter)
{
	struct cmd_obj *ppscmd;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;

	u8 res = _SUCCESS;

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->adapter_type != PRIMARY_ADAPTER)
		goto exit;
#endif

	ppscmd = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (ppscmd == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)kzalloc(sizeof(struct drvextra_cmd_parm), GFP_ATOMIC);
	if (pdrvextra_cmd_parm == NULL) {
		kfree(ppscmd);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = POWER_SAVING_CTRL_WK_CID;
	pdrvextra_cmd_parm->pbuf = NULL;
	init_h2fwcmd_w_parm_no_rsp(ppscmd, pdrvextra_cmd_parm, GEN_CMD_CODE(_SET_DRV_EXTRA));

	res = rtw_enqueue_cmd(pcmdpriv, ppscmd);

exit:

	return res;
}

#ifdef CONFIG_92D_AP_MODE

static void rtw_chk_hi_queue_hdl(struct rtw_adapter *padapter)
{
	int cnt = 0;
	struct sta_info *psta_bmc;
	struct sta_priv *pstapriv = &padapter->stapriv;

	psta_bmc = rtw_get_bcmc_stainfo(padapter);
	if (!psta_bmc)
		return;

	if (psta_bmc->sleepq_len == 0) {
		while ((rtw_read32(padapter, 0x414)&0x00ffff00) != 0) {
			rtw_msleep_os(100);
			cnt++;
			if (cnt > 10)
				break;
		}

		if (cnt <= 10) {
			pstapriv->tim_bitmap &= ~BIT(0);
			pstapriv->sta_dz_bitmap &= ~BIT(0);
			update_beacon(padapter, _TIM_IE_, NULL, false);
		}
	}
}

u8 rtw_chk_hi_queue_cmd(struct rtw_adapter *padapter)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}
	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)kzalloc(sizeof(struct drvextra_cmd_parm), GFP_ATOMIC);
	if (pdrvextra_cmd_parm == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}
	pdrvextra_cmd_parm->ec_id = CHECK_HIQ_WK_CID;
	pdrvextra_cmd_parm->type_size = 0;
	pdrvextra_cmd_parm->pbuf = NULL;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_SET_DRV_EXTRA));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:
	return res;
}
#endif

u8 rtw_c2h_wk_cmd(struct rtw_adapter *padapter, u8 *c2h_evt)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ph2c = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)kzalloc(sizeof(struct drvextra_cmd_parm), GFP_ATOMIC);
	if (pdrvextra_cmd_parm == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = C2H_WK_CID;
	pdrvextra_cmd_parm->type_size = c2h_evt ? 16 : 0;
	pdrvextra_cmd_parm->pbuf = c2h_evt;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_SET_DRV_EXTRA));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;
}

s32 c2h_evt_hdl(struct rtw_adapter *adapter, struct c2h_evt_hdr *c2h_evt, c2h_id_filter filter)
{
	s32 ret = _FAIL;
	u8 buf[16];

	if (!c2h_evt) {
		/* No c2h event in cmd_obj, read c2h event before handling*/
		if (c2h_evt_read(adapter, buf) == _SUCCESS) {
			c2h_evt = (struct c2h_evt_hdr *)buf;

			if (filter && filter(c2h_evt->id) == false)
				goto exit;

			ret = rtw_hal_c2h_handler(adapter, c2h_evt);
		}
	} else {
		if (filter && filter(c2h_evt->id) == false)
			goto exit;
		ret = rtw_hal_c2h_handler(adapter, c2h_evt);
	}
exit:
	return ret;
}

#ifdef CONFIG_C2H_WK
static void c2h_wk_callback(_workitem *work)
{
	struct evt_priv *evtpriv = container_of(work, struct evt_priv, c2h_wk);
	struct rtw_adapter *adapter = container_of(evtpriv, struct rtw_adapter, evtpriv);
	struct c2h_evt_hdr *c2h_evt;
	c2h_id_filter ccx_id_filter = rtw_hal_c2h_id_filter_ccx(adapter);

	evtpriv->c2h_wk_alive = true;

	while (!rtw_cbuf_empty(evtpriv->c2h_queue)) {
		c2h_evt = (struct c2h_evt_hdr *)rtw_cbuf_pop(evtpriv->c2h_queue);
		if (c2h_evt != NULL) {
			/* This C2H event is read, clear it */
			c2h_evt_clear(adapter);
		} else if ((c2h_evt = (struct c2h_evt_hdr *)kmalloc(16, GFP_ATOMIC)) != NULL) {
			/* This C2H event is not read, read & clear now */
			if (c2h_evt_read(adapter, (u8 *)c2h_evt) != _SUCCESS)
				continue;
		}

		/* Special pointer to trigger c2h_evt_clear only */
		if ((void *)c2h_evt == (void *)evtpriv)
			continue;

		if (!c2h_evt_exist(c2h_evt)) {
			kfree(c2h_evt);
			continue;
		}

		if (ccx_id_filter(c2h_evt->id) == true) {
			/* Handle CCX report here */
			rtw_hal_c2h_handler(adapter, c2h_evt);
			kfree(c2h_evt);
		} else {
			/* Enqueue into cmd_thread for others */
			rtw_c2h_wk_cmd(adapter, (u8 *)c2h_evt);
		}
	}

	evtpriv->c2h_wk_alive = false;
}
#endif

u8 rtw_drvextra_cmd_hdl(struct rtw_adapter *padapter, unsigned char *pbuf)
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
#ifdef CONFIG_LPS
	case LPS_CTRL_WK_CID:
		lps_ctrl_wk_hdl(padapter, (u8)pdrvextra_cmd->type_size);
		break;
#endif
#ifdef CONFIG_ANTENNA_DIVERSITY
	case ANT_SELECT_WK_CID:
		antenna_select_wk_hdl(padapter, pdrvextra_cmd->type_size);
		break;
#endif
	case P2P_PROTO_WK_CID:
		/*	Commented by Albert 2011/07/01 */
		/*	I used the type_size as the type command */
		p2p_protocol_wk_hdl(padapter, pdrvextra_cmd->type_size);
		break;
#ifdef CONFIG_92D_AP_MODE
	case CHECK_HIQ_WK_CID:
		rtw_chk_hi_queue_hdl(padapter);
		break;
#endif /* CONFIG_92D_AP_MODE */
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

void rtw_survey_cmd_callback(struct rtw_adapter *padapter ,  struct cmd_obj *pcmd)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

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

void rtw_disassoc_cmd_callback(struct rtw_adapter *padapter,  struct cmd_obj *pcmd)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

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

void rtw_joinbss_cmd_callback(struct rtw_adapter *padapter,  struct cmd_obj *pcmd)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (pcmd->res == H2C_DROPPED) {
		/* TODO: cancel timer and do timeout handler directly... */
		/* need to make timeout handlerOS independent */
		_set_timer(&pmlmepriv->assoc_timer, 1);
	} else if (pcmd->res != H2C_SUCCESS) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("********Error:rtw_select_and_join_from_scanned_queue Wait Sema  Fail ************\n"));
		_set_timer(&pmlmepriv->assoc_timer, 1);
	}

	rtw_free_cmd_obj(pcmd);

}

void rtw_createbss_cmd_callback(struct rtw_adapter *padapter, struct cmd_obj *pcmd)
{
	u8 timer_cancelled;
	struct sta_info *psta = NULL;
	struct wlan_network *pwlan = NULL;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_bssid_ex *pnetwork = (struct wlan_bssid_ex *)pcmd->parmbuf;
	struct wlan_network *tgt_network = &(pmlmepriv->cur_network);

	if ((pcmd->res != H2C_SUCCESS)) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("\n ********Error: rtw_createbss_cmd_callback  Fail ************\n\n."));
		_set_timer(&pmlmepriv->assoc_timer, 1);
	}
	_cancel_timer(&pmlmepriv->assoc_timer, &timer_cancelled);

#ifdef CONFIG_FW_MLMLE
       /* endian_convert */
	pnetwork->Length = le32_to_cpu(pnetwork->Length);
	pnetwork->Ssid.SsidLength = le32_to_cpu(pnetwork->Ssid.SsidLength);
	pnetwork->Privacy = le32_to_cpu(pnetwork->Privacy);
	pnetwork->Rssi = le32_to_cpu(pnetwork->Rssi);
	pnetwork->NetworkTypeInUse = le32_to_cpu(pnetwork->NetworkTypeInUse);
	pnetwork->Configuration.ATIMWindow = le32_to_cpu(pnetwork->Configuration.ATIMWindow);
	pnetwork->Configuration.DSConfig = le32_to_cpu(pnetwork->Configuration.DSConfig);
	pnetwork->Configuration.FHConfig.DwellTime = le32_to_cpu(pnetwork->Configuration.FHConfig.DwellTime);
	pnetwork->Configuration.FHConfig.HopPattern = le32_to_cpu(pnetwork->Configuration.FHConfig.HopPattern);
	pnetwork->Configuration.FHConfig.HopSet = le32_to_cpu(pnetwork->Configuration.FHConfig.HopSet);
	pnetwork->Configuration.FHConfig.Length = le32_to_cpu(pnetwork->Configuration.FHConfig.Length);
	pnetwork->Configuration.Length = le32_to_cpu(pnetwork->Configuration.Length);
	pnetwork->InfrastructureMode = le32_to_cpu(pnetwork->InfrastructureMode);
	pnetwork->IELength = le32_to_cpu(pnetwork->IELength);
#endif
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
		pwlan = _rtw_alloc_network(pmlmepriv);
		spin_lock_bh(&(pmlmepriv->scanned_queue.lock));
		if (pwlan == NULL) {
			pwlan = rtw_get_oldest_wlan_network(&pmlmepriv->scanned_queue);
			if (pwlan == NULL) {
				RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("\n Error:  can't get pwlan in rtw_joinbss_event_callback\n"));
				spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
				goto createbss_cmd_fail;
			}
			pwlan->last_scanned = rtw_get_current_time();
		} else {
			rtw_list_insert_tail(&(pwlan->list), &pmlmepriv->scanned_queue.queue);
		}

		pnetwork->Length = get_wlan_bssid_ex_sz(pnetwork);
		memcpy(&(pwlan->network), pnetwork, pnetwork->Length);

		/*  copy pdev_network information to	pmlmepriv->cur_network */
		memcpy(&tgt_network->network, pnetwork, (get_wlan_bssid_ex_sz(pnetwork)));

		_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);

		spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
		/*  we will set _FW_LINKED when there is one more sat to join us (rtw_stassoc_event_callback) */
	}

createbss_cmd_fail:

	spin_unlock_bh(&pmlmepriv->lock);

	rtw_free_cmd_obj(pcmd);

}

void rtw_setstakey_cmdrsp_callback(struct rtw_adapter *padapter, struct cmd_obj *pcmd)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct set_stakey_rsp *psetstakey_rsp = (struct set_stakey_rsp *)(pcmd->rsp);
	struct sta_info *psta = rtw_get_stainfo(pstapriv, psetstakey_rsp->addr);

	if (psta == NULL) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_, ("\nERROR: rtw_setstaKey_cmdrsp_callback => can't get sta_info\n\n"));
		goto exit;
	}
exit:
	rtw_free_cmd_obj(pcmd);

}

void rtw_setassocsta_cmdrsp_callback(struct rtw_adapter *padapter,  struct cmd_obj *pcmd)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct set_assocsta_parm *passocsta_parm = (struct set_assocsta_parm *)(pcmd->parmbuf);
	struct set_assocsta_rsp *passocsta_rsp = (struct set_assocsta_rsp *)(pcmd->rsp);
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

static void rtw_getrttbl_cmd_cmdrsp_callback(struct rtw_adapter *padapter,  struct cmd_obj *pcmd)
{

	rtw_free_cmd_obj(pcmd);

}
