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
#define _RTW_CMD_C_

#include <drv_types.h>
#include <hal_data.h>

#ifndef DBG_CMD_EXECUTE
	#define DBG_CMD_EXECUTE 0
#endif

/*
Caller and the rtw_cmd_thread can protect cmd_q by spin_lock.
No irqsave is necessary.
*/

sint	_rtw_init_cmd_priv(struct	cmd_priv *pcmdpriv)
{
	sint res = _SUCCESS;


	_rtw_init_sema(&(pcmdpriv->cmd_queue_sema), 0);
	/* _rtw_init_sema(&(pcmdpriv->cmd_done_sema), 0); */
	_rtw_init_sema(&(pcmdpriv->start_cmdthread_sema), 0);

	_rtw_init_queue(&(pcmdpriv->cmd_queue));

	/* allocate DMA-able/Non-Page memory for cmd_buf and rsp_buf */

	pcmdpriv->cmd_seq = 1;

	pcmdpriv->cmd_allocated_buf = rtw_zmalloc(MAX_CMDSZ + CMDBUFF_ALIGN_SZ);

	if (pcmdpriv->cmd_allocated_buf == NULL) {
		res = _FAIL;
		goto exit;
	}

	pcmdpriv->cmd_buf = pcmdpriv->cmd_allocated_buf  +  CMDBUFF_ALIGN_SZ - ((SIZE_PTR)(pcmdpriv->cmd_allocated_buf) & (CMDBUFF_ALIGN_SZ - 1));

	pcmdpriv->rsp_allocated_buf = rtw_zmalloc(MAX_RSPSZ + 4);

	if (pcmdpriv->rsp_allocated_buf == NULL) {
		res = _FAIL;
		goto exit;
	}

	pcmdpriv->rsp_buf = pcmdpriv->rsp_allocated_buf  +  4 - ((SIZE_PTR)(pcmdpriv->rsp_allocated_buf) & 3);

	pcmdpriv->cmd_issued_cnt = pcmdpriv->cmd_done_cnt = pcmdpriv->rsp_cnt = 0;

	_rtw_mutex_init(&pcmdpriv->sctx_mutex);
exit:


	return res;

}

#ifdef CONFIG_C2H_WK
static void c2h_wk_callback(_workitem *work)
{
	struct evt_priv *evtpriv = container_of(work, struct evt_priv, c2h_wk);
	_adapter *adapter = container_of(evtpriv, _adapter, evtpriv);
	u8 *c2h_evt;
	c2h_id_filter direct_hdl_filter = rtw_hal_c2h_id_handle_directly;
	u8 id, seq, plen;
	u8 *payload;

	evtpriv->c2h_wk_alive = _TRUE;

	while (!rtw_cbuf_empty(evtpriv->c2h_queue)) {
		c2h_evt = (u8 *)rtw_cbuf_pop(evtpriv->c2h_queue);
		if (c2h_evt != NULL) {
			/* This C2H event is read, clear it */
			c2h_evt_clear(adapter);
		} else {
			c2h_evt = (u8 *)rtw_malloc(C2H_REG_LEN);
			if (c2h_evt == NULL) {
				rtw_warn_on(1);
				continue;
			}

			/* This C2H event is not read, read & clear now */
			if (rtw_hal_c2h_evt_read(adapter, c2h_evt) != _SUCCESS) {
				rtw_mfree(c2h_evt, C2H_REG_LEN);
				continue;
			}
		}

		/* Special pointer to trigger c2h_evt_clear only */
		if ((void *)c2h_evt == (void *)evtpriv)
			continue;

		if (!rtw_hal_c2h_valid(adapter, c2h_evt)
			|| rtw_hal_c2h_reg_hdr_parse(adapter, c2h_evt, &id, &seq, &plen, &payload) != _SUCCESS
		) {
			rtw_mfree(c2h_evt, C2H_REG_LEN);
			continue;
		}

		if (direct_hdl_filter(adapter, id, seq, plen, payload) == _TRUE) {
			/* Handle directly */
			rtw_hal_c2h_handler(adapter, id, seq, plen, payload);
			rtw_mfree(c2h_evt, C2H_REG_LEN);
		} else {
			/* Enqueue into cmd_thread for others */
			rtw_c2h_reg_wk_cmd(adapter, c2h_evt);
			rtw_mfree(c2h_evt, C2H_REG_LEN);
		}
	}

	evtpriv->c2h_wk_alive = _FALSE;
}
#endif /* CONFIG_C2H_WK */

sint _rtw_init_evt_priv(struct evt_priv *pevtpriv)
{
	sint res = _SUCCESS;


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

	pevtpriv->evt_allocated_buf = rtw_zmalloc(MAX_EVTSZ + 4);
	if (pevtpriv->evt_allocated_buf == NULL) {
		res = _FAIL;
		goto exit;
	}
	pevtpriv->evt_buf = pevtpriv->evt_allocated_buf  +  4 - ((unsigned int)(pevtpriv->evt_allocated_buf) & 3);


#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	pevtpriv->allocated_c2h_mem = rtw_zmalloc(C2H_MEM_SZ + 4);

	if (pevtpriv->allocated_c2h_mem == NULL) {
		res = _FAIL;
		goto exit;
	}

	pevtpriv->c2h_mem = pevtpriv->allocated_c2h_mem +  4\
			    - ((u32)(pevtpriv->allocated_c2h_mem) & 3);
#endif /* end of CONFIG_SDIO_HCI */

	_rtw_init_queue(&(pevtpriv->evt_queue));

exit:

#endif /* end of CONFIG_EVENT_THREAD_MODE */

#ifdef CONFIG_C2H_WK
	_init_workitem(&pevtpriv->c2h_wk, c2h_wk_callback, NULL);
	pevtpriv->c2h_wk_alive = _FALSE;
	pevtpriv->c2h_queue = rtw_cbuf_alloc(C2H_QUEUE_MAX_LEN + 1);
#endif


	return res;
}

void _rtw_free_evt_priv(struct	evt_priv *pevtpriv)
{


#ifdef CONFIG_EVENT_THREAD_MODE
	_rtw_free_sema(&(pevtpriv->evt_notify));

	if (pevtpriv->evt_allocated_buf)
		rtw_mfree(pevtpriv->evt_allocated_buf, MAX_EVTSZ + 4);
#endif

#ifdef CONFIG_C2H_WK
	_cancel_workitem_sync(&pevtpriv->c2h_wk);
	while (pevtpriv->c2h_wk_alive)
		rtw_msleep_os(10);

	while (!rtw_cbuf_empty(pevtpriv->c2h_queue)) {
		void *c2h;
		c2h = rtw_cbuf_pop(pevtpriv->c2h_queue);
		if (c2h != NULL && c2h != (void *)pevtpriv)
			rtw_mfree(c2h, 16);
	}
	rtw_cbuf_free(pevtpriv->c2h_queue);
#endif



}

void _rtw_free_cmd_priv(struct	cmd_priv *pcmdpriv)
{

	if (pcmdpriv) {
		_rtw_spinlock_free(&(pcmdpriv->cmd_queue.lock));
		_rtw_free_sema(&(pcmdpriv->cmd_queue_sema));
		/* _rtw_free_sema(&(pcmdpriv->cmd_done_sema)); */
		_rtw_free_sema(&(pcmdpriv->start_cmdthread_sema));

		if (pcmdpriv->cmd_allocated_buf)
			rtw_mfree(pcmdpriv->cmd_allocated_buf, MAX_CMDSZ + CMDBUFF_ALIGN_SZ);

		if (pcmdpriv->rsp_allocated_buf)
			rtw_mfree(pcmdpriv->rsp_allocated_buf, MAX_RSPSZ + 4);

		_rtw_mutex_free(&pcmdpriv->sctx_mutex);
	}
}

/*
Calling Context:

rtw_enqueue_cmd can only be called between kernel thread,
since only spin_lock is used.

ISR/Call-Back functions can't call this sub-function.

*/
#ifdef DBG_CMD_QUEUE
extern u8 dump_cmd_id;
#endif

sint _rtw_enqueue_cmd(_queue *queue, struct cmd_obj *obj, bool to_head)
{
	_irqL irqL;


	if (obj == NULL)
		goto exit;

	/* _enter_critical_bh(&queue->lock, &irqL); */
	_enter_critical(&queue->lock, &irqL);

	if (to_head)
		rtw_list_insert_head(&obj->list, &queue->queue);
	else
		rtw_list_insert_tail(&obj->list, &queue->queue);

#ifdef DBG_CMD_QUEUE
	if (dump_cmd_id) {
		RTW_INFO("%s===> cmdcode:0x%02x\n", __FUNCTION__, obj->cmdcode);
		if (obj->cmdcode == CMD_SET_MLME_EVT) {
			if (obj->parmbuf) {
				struct rtw_evt_header *evt_hdr = (struct rtw_evt_header *)(obj->parmbuf);
				RTW_INFO("evt_hdr->id:%d\n", evt_hdr->id);
			}
		}
		if (obj->cmdcode == CMD_SET_DRV_EXTRA) {
			if (obj->parmbuf) {
				struct drvextra_cmd_parm *pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)(obj->parmbuf);
				RTW_INFO("pdrvextra_cmd_parm->ec_id:0x%02x\n", pdrvextra_cmd_parm->ec_id);
			}
		}
	}

	if (queue->queue.prev->next != &queue->queue) {
		RTW_INFO("[%d] head %p, tail %p, tail->prev->next %p[tail], tail->next %p[head]\n", __LINE__,
			&queue->queue, queue->queue.prev, queue->queue.prev->prev->next, queue->queue.prev->next);

		RTW_INFO("==========%s============\n", __FUNCTION__);
		RTW_INFO("head:%p,obj_addr:%p\n", &queue->queue, obj);
		RTW_INFO("padapter: %p\n", obj->padapter);
		RTW_INFO("cmdcode: 0x%02x\n", obj->cmdcode);
		RTW_INFO("res: %d\n", obj->res);
		RTW_INFO("parmbuf: %p\n", obj->parmbuf);
		RTW_INFO("cmdsz: %d\n", obj->cmdsz);
		RTW_INFO("rsp: %p\n", obj->rsp);
		RTW_INFO("rspsz: %d\n", obj->rspsz);
		RTW_INFO("sctx: %p\n", obj->sctx);
		RTW_INFO("list->next: %p\n", obj->list.next);
		RTW_INFO("list->prev: %p\n", obj->list.prev);
	}
#endif /* DBG_CMD_QUEUE */

	/* _exit_critical_bh(&queue->lock, &irqL);	 */
	_exit_critical(&queue->lock, &irqL);

exit:


	return _SUCCESS;
}

struct	cmd_obj	*_rtw_dequeue_cmd(_queue *queue)
{
	_irqL irqL;
	struct cmd_obj *obj;


	/* _enter_critical_bh(&(queue->lock), &irqL); */
	_enter_critical(&queue->lock, &irqL);

#ifdef DBG_CMD_QUEUE
	if (queue->queue.prev->next != &queue->queue) {
		RTW_INFO("[%d] head %p, tail %p, tail->prev->next %p[tail], tail->next %p[head]\n", __LINE__,
			&queue->queue, queue->queue.prev, queue->queue.prev->prev->next, queue->queue.prev->next);
	}
#endif /* DBG_CMD_QUEUE */


	if (rtw_is_list_empty(&(queue->queue)))
		obj = NULL;
	else {
		obj = LIST_CONTAINOR(get_next(&(queue->queue)), struct cmd_obj, list);

#ifdef DBG_CMD_QUEUE
		if (queue->queue.prev->next != &queue->queue) {
			RTW_INFO("==========%s============\n", __FUNCTION__);
			RTW_INFO("head:%p,obj_addr:%p\n", &queue->queue, obj);
			RTW_INFO("padapter: %p\n", obj->padapter);
			RTW_INFO("cmdcode: 0x%02x\n", obj->cmdcode);
			RTW_INFO("res: %d\n", obj->res);
			RTW_INFO("parmbuf: %p\n", obj->parmbuf);
			RTW_INFO("cmdsz: %d\n", obj->cmdsz);
			RTW_INFO("rsp: %p\n", obj->rsp);
			RTW_INFO("rspsz: %d\n", obj->rspsz);
			RTW_INFO("sctx: %p\n", obj->sctx);
			RTW_INFO("list->next: %p\n", obj->list.next);
			RTW_INFO("list->prev: %p\n", obj->list.prev);
		}

		if (dump_cmd_id) {
			RTW_INFO("%s===> cmdcode:0x%02x\n", __FUNCTION__, obj->cmdcode);
			if (obj->cmdcode == CMD_SET_DRV_EXTRA) {
				if (obj->parmbuf) {
					struct drvextra_cmd_parm *pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)(obj->parmbuf);
					printk("pdrvextra_cmd_parm->ec_id:0x%02x\n", pdrvextra_cmd_parm->ec_id);
				}
			}

		}
#endif /* DBG_CMD_QUEUE */

		rtw_list_delete(&obj->list);
	}

	/* _exit_critical_bh(&(queue->lock), &irqL); */
	_exit_critical(&queue->lock, &irqL);


	return obj;
}

u32	rtw_init_cmd_priv(struct cmd_priv *pcmdpriv)
{
	u32	res;
	res = _rtw_init_cmd_priv(pcmdpriv);
	return res;
}

u32	rtw_init_evt_priv(struct	evt_priv *pevtpriv)
{
	int	res;
	res = _rtw_init_evt_priv(pevtpriv);
	return res;
}

void rtw_free_evt_priv(struct	evt_priv *pevtpriv)
{
	_rtw_free_evt_priv(pevtpriv);
}

void rtw_free_cmd_priv(struct	cmd_priv *pcmdpriv)
{
	_rtw_free_cmd_priv(pcmdpriv);
}

int rtw_cmd_filter(struct cmd_priv *pcmdpriv, struct cmd_obj *cmd_obj);
int rtw_cmd_filter(struct cmd_priv *pcmdpriv, struct cmd_obj *cmd_obj)
{
	u8 bAllow = _FALSE; /* set to _TRUE to allow enqueuing cmd when hw_init_completed is _FALSE */

#ifdef SUPPORT_HW_RFOFF_DETECTED
	/* To decide allow or not */
	if ((adapter_to_pwrctl(pcmdpriv->padapter)->bHWPwrPindetect)
	    && (!pcmdpriv->padapter->registrypriv.usbss_enable)
	   ) {
		if (cmd_obj->cmdcode == CMD_SET_DRV_EXTRA) {
			struct drvextra_cmd_parm	*pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)cmd_obj->parmbuf;
			if (pdrvextra_cmd_parm->ec_id == POWER_SAVING_CTRL_WK_CID) {
				/* RTW_INFO("==>enqueue POWER_SAVING_CTRL_WK_CID\n"); */
				bAllow = _TRUE;
			}
		}
	}
#endif

	if (cmd_obj->cmdcode == CMD_SET_CHANPLAN)
		bAllow = _TRUE;

	if (cmd_obj->no_io)
		bAllow = _TRUE;

	if ((!rtw_is_hw_init_completed(pcmdpriv->padapter) && (bAllow == _FALSE))
	    || ATOMIC_READ(&(pcmdpriv->cmdthd_running)) == _FALSE	/* com_thread not running */
	   ) {
		if (DBG_CMD_EXECUTE)
			RTW_INFO(ADPT_FMT" drop "CMD_FMT" hw_init_completed:%u, cmdthd_running:%u\n", ADPT_ARG(cmd_obj->padapter)
				, CMD_ARG(cmd_obj), rtw_get_hw_init_completed(cmd_obj->padapter), ATOMIC_READ(&pcmdpriv->cmdthd_running));
		if (0)
			rtw_warn_on(1);

		return _FAIL;
	}
	return _SUCCESS;
}



u32 rtw_enqueue_cmd(struct cmd_priv *pcmdpriv, struct cmd_obj *cmd_obj)
{
	int res = _FAIL;
	PADAPTER padapter = pcmdpriv->padapter;


	if (cmd_obj == NULL)
		goto exit;

	cmd_obj->padapter = padapter;

#ifdef CONFIG_CONCURRENT_MODE
	/* change pcmdpriv to primary's pcmdpriv */
	if (!is_primary_adapter(padapter))
		pcmdpriv = &(GET_PRIMARY_ADAPTER(padapter)->cmdpriv);
#endif

	res = rtw_cmd_filter(pcmdpriv, cmd_obj);
	if ((_FAIL == res) || (cmd_obj->cmdsz > MAX_CMDSZ)) {
		if (cmd_obj->cmdsz > MAX_CMDSZ) {
			RTW_INFO("%s failed due to obj->cmdsz(%d) > MAX_CMDSZ(%d)\n", __func__, cmd_obj->cmdsz, MAX_CMDSZ);
			rtw_warn_on(1);
		}

		if (cmd_obj->cmdcode == CMD_SET_DRV_EXTRA) {
			struct drvextra_cmd_parm *extra_parm = (struct drvextra_cmd_parm *)cmd_obj->parmbuf;

			if (extra_parm->pbuf && extra_parm->size > 0)
				rtw_mfree(extra_parm->pbuf, extra_parm->size);
		}
		rtw_free_cmd_obj(cmd_obj);
		goto exit;
	}

	res = _rtw_enqueue_cmd(&pcmdpriv->cmd_queue, cmd_obj, 0);

	if (res == _SUCCESS)
		_rtw_up_sema(&pcmdpriv->cmd_queue_sema);

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
	/* _rtw_up_sema(&(pcmdpriv->cmd_done_sema)); */
}

void rtw_free_cmd_obj(struct cmd_obj *pcmd)
{
	if (pcmd->parmbuf != NULL) {
		/* free parmbuf in cmd_obj */
		rtw_mfree((unsigned char *)pcmd->parmbuf, pcmd->cmdsz);
	}
	if (pcmd->rsp != NULL) {
		if (pcmd->rspsz != 0) {
			/* free rsp in cmd_obj */
			rtw_mfree((unsigned char *)pcmd->rsp, pcmd->rspsz);
		}
	}

	/* free cmd_obj */
	rtw_mfree((unsigned char *)pcmd, sizeof(struct cmd_obj));
}


void rtw_stop_cmd_thread(_adapter *adapter)
{
	if (adapter->cmdThread) {
		_rtw_up_sema(&adapter->cmdpriv.cmd_queue_sema);
		rtw_thread_stop(adapter->cmdThread);
		adapter->cmdThread = NULL;
	}
}

thread_return rtw_cmd_thread(thread_context context)
{
	u8 ret;
	struct cmd_obj *pcmd;
	u8 *pcmdbuf, *prspbuf;
	systime cmd_start_time;
	u32 cmd_process_time;
	u8(*cmd_hdl)(_adapter *padapter, u8 *pbuf);
	void (*pcmd_callback)(_adapter *dev, struct cmd_obj *pcmd);
	PADAPTER padapter = (PADAPTER)context;
	struct cmd_priv *pcmdpriv = &(padapter->cmdpriv);
	struct drvextra_cmd_parm *extra_parm = NULL;
	_irqL irqL;

	thread_enter("RTW_CMD_THREAD");

	pcmdbuf = pcmdpriv->cmd_buf;
	prspbuf = pcmdpriv->rsp_buf;
	ATOMIC_SET(&(pcmdpriv->cmdthd_running), _TRUE);
	_rtw_up_sema(&pcmdpriv->start_cmdthread_sema);


	while (1) {
		if (_rtw_down_sema(&pcmdpriv->cmd_queue_sema) == _FAIL) {
			RTW_PRINT(FUNC_ADPT_FMT" _rtw_down_sema(&pcmdpriv->cmd_queue_sema) return _FAIL, break\n", FUNC_ADPT_ARG(padapter));
			break;
		}

		if (RTW_CANNOT_RUN(padapter)) {
			RTW_DBG(FUNC_ADPT_FMT "- bDriverStopped(%s) bSurpriseRemoved(%s)\n",
				FUNC_ADPT_ARG(padapter),
				rtw_is_drv_stopped(padapter) ? "True" : "False",
				rtw_is_surprise_removed(padapter) ? "True" : "False");
			break;
		}

		_enter_critical(&pcmdpriv->cmd_queue.lock, &irqL);
		if (rtw_is_list_empty(&(pcmdpriv->cmd_queue.queue))) {
			/* RTW_INFO("%s: cmd queue is empty!\n", __func__); */
			_exit_critical(&pcmdpriv->cmd_queue.lock, &irqL);
			continue;
		}
		_exit_critical(&pcmdpriv->cmd_queue.lock, &irqL);

_next:
		if (RTW_CANNOT_RUN(padapter)) {
			RTW_PRINT("%s: DriverStopped(%s) SurpriseRemoved(%s) break at line %d\n",
				  __func__
				, rtw_is_drv_stopped(padapter) ? "True" : "False"
				, rtw_is_surprise_removed(padapter) ? "True" : "False"
				  , __LINE__);
			break;
		}

		pcmd = rtw_dequeue_cmd(pcmdpriv);
		if (!pcmd) {
#ifdef CONFIG_LPS_LCLK
			rtw_unregister_cmd_alive(padapter);
#endif
			continue;
		}

		cmd_start_time = rtw_get_current_time();
		pcmdpriv->cmd_issued_cnt++;

		if (pcmd->cmdsz > MAX_CMDSZ) {
			RTW_ERR("%s cmdsz:%d > MAX_CMDSZ:%d\n", __func__, pcmd->cmdsz, MAX_CMDSZ);
			pcmd->res = H2C_PARAMETERS_ERROR;
			goto post_process;
		}

		if (pcmd->cmdcode >= (sizeof(wlancmds) / sizeof(struct rtw_cmd))) {
			RTW_ERR("%s undefined cmdcode:%d\n", __func__, pcmd->cmdcode);
			pcmd->res = H2C_PARAMETERS_ERROR;
			goto post_process;
		}

		cmd_hdl = wlancmds[pcmd->cmdcode].cmd_hdl;
		if (!cmd_hdl) {
			RTW_ERR("%s no cmd_hdl for cmdcode:%d\n", __func__, pcmd->cmdcode);
			pcmd->res = H2C_PARAMETERS_ERROR;
			goto post_process;
		}

		if (_FAIL == rtw_cmd_filter(pcmdpriv, pcmd)) {
			pcmd->res = H2C_DROPPED;
			if (pcmd->cmdcode == CMD_SET_DRV_EXTRA) {
				extra_parm = (struct drvextra_cmd_parm *)pcmd->parmbuf;
				if (extra_parm && extra_parm->pbuf && extra_parm->size > 0)
					rtw_mfree(extra_parm->pbuf, extra_parm->size);
			}
			#if CONFIG_DFS
			else if (pcmd->cmdcode == CMD_SET_CHANSWITCH)
				adapter_to_rfctl(padapter)->csa_ch = 0;
			#endif
			goto post_process;
		}

#ifdef CONFIG_LPS_LCLK
		if (pcmd->no_io)
			rtw_unregister_cmd_alive(padapter);
		else {
			if (rtw_register_cmd_alive(padapter) != _SUCCESS) {
				if (DBG_CMD_EXECUTE)
					RTW_PRINT("%s: wait to leave LPS_LCLK\n", __func__);

				pcmd->res = H2C_ENQ_HEAD;
				ret = _rtw_enqueue_cmd(&pcmdpriv->cmd_queue, pcmd, 1);
				if (ret == _SUCCESS) {
					if (DBG_CMD_EXECUTE)
						RTW_INFO(ADPT_FMT" "CMD_FMT" ENQ_HEAD\n", ADPT_ARG(pcmd->padapter), CMD_ARG(pcmd));
					continue;
				}

				RTW_INFO(ADPT_FMT" "CMD_FMT" ENQ_HEAD_FAIL\n", ADPT_ARG(pcmd->padapter), CMD_ARG(pcmd));
				pcmd->res = H2C_ENQ_HEAD_FAIL;
				rtw_warn_on(1);
			}
		}
#endif /* CONFIG_LPS_LCLK */

		if (DBG_CMD_EXECUTE)
			RTW_INFO(ADPT_FMT" "CMD_FMT" %sexecute\n", ADPT_ARG(pcmd->padapter), CMD_ARG(pcmd)
				, pcmd->res == H2C_ENQ_HEAD ? "ENQ_HEAD " : (pcmd->res == H2C_ENQ_HEAD_FAIL ? "ENQ_HEAD_FAIL " : ""));

		_rtw_memcpy(pcmdbuf, pcmd->parmbuf, pcmd->cmdsz);
		ret = cmd_hdl(pcmd->padapter, pcmdbuf);
		pcmd->res = ret;

		pcmdpriv->cmd_seq++;

post_process:

		_enter_critical_mutex(&(pcmd->padapter->cmdpriv.sctx_mutex), NULL);
		if (pcmd->sctx) {
			if (0)
				RTW_PRINT(FUNC_ADPT_FMT" pcmd->sctx\n", FUNC_ADPT_ARG(pcmd->padapter));
			if (pcmd->res == H2C_SUCCESS)
				rtw_sctx_done(&pcmd->sctx);
			else
				rtw_sctx_done_err(&pcmd->sctx, RTW_SCTX_DONE_CMD_ERROR);
		}
		_exit_critical_mutex(&(pcmd->padapter->cmdpriv.sctx_mutex), NULL);

		cmd_process_time = rtw_get_passing_time_ms(cmd_start_time);
		if (cmd_process_time > 1000) {
			RTW_INFO(ADPT_FMT" "CMD_FMT" process_time=%d\n", ADPT_ARG(pcmd->padapter), CMD_ARG(pcmd), cmd_process_time);
			if (0)
				rtw_warn_on(1);
		}

		/* call callback function for post-processed */
		if (pcmd->cmdcode < (sizeof(wlancmds) / sizeof(struct rtw_cmd)))
			pcmd_callback = wlancmds[pcmd->cmdcode].callback;
		else
			pcmd_callback = NULL;

		if (pcmd_callback == NULL) {
			rtw_free_cmd_obj(pcmd);
		} else {
			/* todo: !!! fill rsp_buf to pcmd->rsp if (pcmd->rsp!=NULL) */
			pcmd_callback(pcmd->padapter, pcmd);/* need conider that free cmd_obj in rtw_cmd_callback */
		}

		flush_signals_thread();

		goto _next;

	}

#ifdef CONFIG_LPS_LCLK
	rtw_unregister_cmd_alive(padapter);
#endif

	/* to avoid enqueue cmd after free all cmd_obj */
	ATOMIC_SET(&(pcmdpriv->cmdthd_running), _FALSE);

	/* free all cmd_obj resources */
	do {
		pcmd = rtw_dequeue_cmd(pcmdpriv);
		if (pcmd == NULL)
			break;

		if (0)
			RTW_INFO("%s: leaving... drop "CMD_FMT"\n", __func__, CMD_ARG(pcmd));

		if (pcmd->cmdcode == CMD_SET_DRV_EXTRA) {
			extra_parm = (struct drvextra_cmd_parm *)pcmd->parmbuf;
			if (extra_parm->pbuf && extra_parm->size > 0)
				rtw_mfree(extra_parm->pbuf, extra_parm->size);
		}
		#if CONFIG_DFS
		else if (pcmd->cmdcode == CMD_SET_CHANSWITCH)
			adapter_to_rfctl(padapter)->csa_ch = 0;
		#endif

		_enter_critical_mutex(&(pcmd->padapter->cmdpriv.sctx_mutex), NULL);
		if (pcmd->sctx) {
			if (0)
				RTW_PRINT(FUNC_ADPT_FMT" pcmd->sctx\n", FUNC_ADPT_ARG(pcmd->padapter));
			rtw_sctx_done_err(&pcmd->sctx, RTW_SCTX_DONE_CMD_DROP);
		}
		_exit_critical_mutex(&(pcmd->padapter->cmdpriv.sctx_mutex), NULL);

		rtw_free_cmd_obj(pcmd);
	} while (1);

	RTW_INFO(FUNC_ADPT_FMT " Exit\n", FUNC_ADPT_ARG(padapter));

	rtw_thread_wait_stop();

	return 0;
}


#ifdef CONFIG_EVENT_THREAD_MODE
u32 rtw_enqueue_evt(struct evt_priv *pevtpriv, struct evt_obj *obj)
{
	_irqL irqL;
	int	res;
	_queue *queue = &pevtpriv->evt_queue;


	res = _SUCCESS;

	if (obj == NULL) {
		res = _FAIL;
		goto exit;
	}

	_enter_critical_bh(&queue->lock, &irqL);

	rtw_list_insert_tail(&obj->list, &queue->queue);

	_exit_critical_bh(&queue->lock, &irqL);

	/* rtw_evt_notify_isr(pevtpriv); */

exit:


	return res;
}

struct evt_obj *rtw_dequeue_evt(_queue *queue)
{
	_irqL irqL;
	struct	evt_obj	*pevtobj;


	_enter_critical_bh(&queue->lock, &irqL);

	if (rtw_is_list_empty(&(queue->queue)))
		pevtobj = NULL;
	else {
		pevtobj = LIST_CONTAINOR(get_next(&(queue->queue)), struct evt_obj, list);
		rtw_list_delete(&pevtobj->list);
	}

	_exit_critical_bh(&queue->lock, &irqL);


	return pevtobj;
}

void rtw_free_evt_obj(struct evt_obj *pevtobj)
{

	if (pevtobj->parmbuf)
		rtw_mfree((unsigned char *)pevtobj->parmbuf, pevtobj->evtsz);

	rtw_mfree((unsigned char *)pevtobj, sizeof(struct evt_obj));

}

void rtw_evt_notify_isr(struct evt_priv *pevtpriv)
{
	pevtpriv->evt_done_cnt++;
	_rtw_up_sema(&(pevtpriv->evt_notify));
}
#endif

void rtw_init_sitesurvey_parm(_adapter *padapter, struct sitesurvey_parm *pparm)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;


	_rtw_memset(pparm, 0, sizeof(struct sitesurvey_parm));
	pparm->scan_mode = pmlmepriv->scan_mode;
}

/*
rtw_sitesurvey_cmd(~)
	### NOTE:#### (!!!!)
	MUST TAKE CARE THAT BEFORE CALLING THIS FUNC, YOU SHOULD HAVE LOCKED pmlmepriv->lock
*/
u8 rtw_sitesurvey_cmd(_adapter *padapter, struct sitesurvey_parm *pparm)
{
	u8 res = _FAIL;
	struct cmd_obj		*ph2c;
	struct sitesurvey_parm	*psurveyPara;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;

#ifdef CONFIG_LPS
	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
		rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_SCAN, 0);
#endif

#ifdef CONFIG_P2P_PS
	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
		p2p_ps_wk_cmd(padapter, P2P_PS_SCAN, 1);
#endif /* CONFIG_P2P_PS */

	ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL)
		return _FAIL;

	psurveyPara = (struct sitesurvey_parm *)rtw_zmalloc(sizeof(struct sitesurvey_parm));
	if (psurveyPara == NULL) {
		rtw_mfree((unsigned char *) ph2c, sizeof(struct cmd_obj));
		return _FAIL;
	}

	if (pparm)
		_rtw_memcpy(psurveyPara, pparm, sizeof(struct sitesurvey_parm));
	else
		psurveyPara->scan_mode = pmlmepriv->scan_mode;

	rtw_free_network_queue(padapter, _FALSE);

	init_h2fwcmd_w_parm_no_rsp(ph2c, psurveyPara, CMD_SITE_SURVEY);

	set_fwstate(pmlmepriv, WIFI_UNDER_SURVEY);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

	if (res == _SUCCESS) {
		u32 scan_timeout_ms;

		pmlmepriv->scan_start_time = rtw_get_current_time();
		scan_timeout_ms = rtw_scan_timeout_decision(padapter);
		mlme_set_scan_to_timer(pmlmepriv,scan_timeout_ms);

		rtw_led_control(padapter, LED_CTL_SITE_SURVEY);
	} else
		_clr_fwstate_(pmlmepriv, WIFI_UNDER_SURVEY);


	return res;
}

void rtw_readtssi_cmdrsp_callback(_adapter	*padapter,  struct cmd_obj *pcmd)
{

	rtw_mfree((unsigned char *) pcmd->parmbuf, pcmd->cmdsz);
	rtw_mfree((unsigned char *) pcmd, sizeof(struct cmd_obj));

#ifdef CONFIG_MP_INCLUDED
	if (padapter->registrypriv.mp_mode == 1)
		padapter->mppriv.workparam.bcompleted = _TRUE;
#endif

}

static u8 rtw_createbss_cmd(_adapter  *adapter, int flags, bool adhoc
	, u8 ifbmp, u8 excl_ifbmp, s16 req_ch, s8 req_bw, s8 req_offset)
{
	struct cmd_obj *cmdobj;
	struct createbss_parm *parm;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	struct submit_ctx sctx;
	u8 res = _SUCCESS;

	if (req_ch > 0 && req_bw >= 0 && req_offset >= 0) {
		if (!rtw_chset_is_chbw_valid(adapter_to_chset(adapter), req_ch, req_bw, req_offset, 0, 0)) {
			res = _FAIL;
			goto exit;
		}
	}

	/* prepare cmd parameter */
	parm = (struct createbss_parm *)rtw_zmalloc(sizeof(*parm));
	if (parm == NULL) {
		res = _FAIL;
		goto exit;
	}

	if (adhoc) {
		/* for now, adhoc doesn't support ch,bw,offset request */
		parm->adhoc = 1;
	} else {
		parm->adhoc = 0;
		parm->ifbmp = ifbmp;
		parm->excl_ifbmp = excl_ifbmp;
		parm->req_ch = req_ch;
		parm->req_bw = req_bw;
		parm->req_offset = req_offset;
	}

	if (flags & RTW_CMDF_DIRECTLY) {
		/* no need to enqueue, do the cmd hdl directly and free cmd parameter */
		if (H2C_SUCCESS != createbss_hdl(adapter, (u8 *)parm))
			res = _FAIL;
		rtw_mfree((u8 *)parm, sizeof(*parm));
	} else {
		/* need enqueue, prepare cmd_obj and enqueue */
		cmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(*cmdobj));
		if (cmdobj == NULL) {
			res = _FAIL;
			rtw_mfree((u8 *)parm, sizeof(*parm));
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(cmdobj, parm, CMD_CREATE_BSS);

		if (flags & RTW_CMDF_WAIT_ACK) {
			cmdobj->sctx = &sctx;
			rtw_sctx_init(&sctx, 2000);
		}

		res = rtw_enqueue_cmd(pcmdpriv, cmdobj);

		if (res == _SUCCESS && (flags & RTW_CMDF_WAIT_ACK)) {
			rtw_sctx_wait(&sctx, __func__);
			_enter_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
			if (sctx.status == RTW_SCTX_SUBMITTED)
				cmdobj->sctx = NULL;
			_exit_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
		}
	}

exit:
	return res;
}

inline u8 rtw_create_ibss_cmd(_adapter *adapter, int flags)
{
	return rtw_createbss_cmd(adapter, flags
		, 1
		, 0, 0
		, 0, REQ_BW_NONE, REQ_OFFSET_NONE /* for now, adhoc doesn't support ch,bw,offset request */
	);
}

inline u8 rtw_startbss_cmd(_adapter *adapter, int flags)
{
	return rtw_createbss_cmd(adapter, flags
		, 0
		, BIT(adapter->iface_id), 0
		, 0, REQ_BW_NONE, REQ_OFFSET_NONE /* excute entire AP setup cmd */
	);
}

inline u8 rtw_change_bss_chbw_cmd(_adapter *adapter, int flags
	, u8 ifbmp, u8 excl_ifbmp, s16 req_ch, s8 req_bw, s8 req_offset)
{
	return rtw_createbss_cmd(adapter, flags
		, 0
		, ifbmp, excl_ifbmp
		, req_ch, req_bw, req_offset
	);
}

u8 rtw_joinbss_cmd(_adapter  *padapter, struct wlan_network *pnetwork)
{
	u8	*auth, res = _SUCCESS;
	uint	t_len = 0;
	WLAN_BSSID_EX		*psecnetwork;
	struct cmd_obj		*pcmd;
	struct cmd_priv		*pcmdpriv = &padapter->cmdpriv;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct qos_priv		*pqospriv = &pmlmepriv->qospriv;
	struct security_priv	*psecuritypriv = &padapter->securitypriv;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
#ifdef CONFIG_80211N_HT
	struct ht_priv			*phtpriv = &pmlmepriv->htpriv;
#endif /* CONFIG_80211N_HT */
#ifdef CONFIG_80211AC_VHT
	struct vht_priv		*pvhtpriv = &pmlmepriv->vhtpriv;
#endif /* CONFIG_80211AC_VHT */
	NDIS_802_11_NETWORK_INFRASTRUCTURE ndis_network_mode = pnetwork->network.InfrastructureMode;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);
	u32 tmp_len;
	u8 *ptmp = NULL;

	rtw_led_control(padapter, LED_CTL_START_TO_LINK);

	pcmd = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (pcmd == NULL) {
		res = _FAIL;
		goto exit;
	}
#if 0
	/*  for IEs is pointer */
	t_len = sizeof(u32) + sizeof(NDIS_802_11_MAC_ADDRESS) + 2 +
		sizeof(NDIS_802_11_SSID) + sizeof(u32) +
		sizeof(NDIS_802_11_RSSI) + sizeof(NDIS_802_11_NETWORK_TYPE) +
		sizeof(NDIS_802_11_CONFIGURATION) +
		sizeof(NDIS_802_11_NETWORK_INFRASTRUCTURE) +
		sizeof(NDIS_802_11_RATES_EX) + sizeof(WLAN_PHY_INFO) + sizeof(u32) + MAX_IE_SZ;
#endif
	/* for IEs is fix buf size */
	t_len = sizeof(WLAN_BSSID_EX);


	/* for hidden ap to set fw_state here */
	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_ADHOC_STATE) != _TRUE) {
		switch (ndis_network_mode) {
		case Ndis802_11IBSS:
			set_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
			break;

		case Ndis802_11Infrastructure:
			set_fwstate(pmlmepriv, WIFI_STATION_STATE);
			break;

		default:
			rtw_warn_on(1);
			break;
		}
	}

	pmlmeinfo->assoc_AP_vendor = check_assoc_AP(pnetwork->network.IEs, pnetwork->network.IELength);

#ifdef CONFIG_80211AC_VHT
	/* save AP beamform_cap info for BCM IOT issue */
	if (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_BROADCOM)
		get_vht_bf_cap(pnetwork->network.IEs,
			pnetwork->network.IELength,
			&pvhtpriv->ap_bf_cap);
#endif
	/*
		Modified by Arvin 2015/05/13
		Solution for allocating a new WLAN_BSSID_EX to avoid race condition issue between disconnect and joinbss
	*/
	psecnetwork = (WLAN_BSSID_EX *)rtw_zmalloc(sizeof(WLAN_BSSID_EX));
	if (psecnetwork == NULL) {
		if (pcmd != NULL)
			rtw_mfree((unsigned char *)pcmd, sizeof(struct	cmd_obj));

		res = _FAIL;


		goto exit;
	}

	_rtw_memset(psecnetwork, 0, t_len);

	_rtw_memcpy(psecnetwork, &pnetwork->network, get_WLAN_BSSID_EX_sz(&pnetwork->network));

	auth = &psecuritypriv->authenticator_ie[0];
	psecuritypriv->authenticator_ie[0] = (unsigned char)psecnetwork->IELength;

	if ((psecnetwork->IELength - 12) < (256 - 1))
		_rtw_memcpy(&psecuritypriv->authenticator_ie[1], &psecnetwork->IEs[12], psecnetwork->IELength - 12);
	else
		_rtw_memcpy(&psecuritypriv->authenticator_ie[1], &psecnetwork->IEs[12], (256 - 1));

	psecnetwork->IELength = 0;
	/* Added by Albert 2009/02/18 */
	/* If the the driver wants to use the bssid to create the connection. */
	/* If not,  we have to copy the connecting AP's MAC address to it so that */
	/* the driver just has the bssid information for PMKIDList searching. */

	if (pmlmepriv->assoc_by_bssid == _FALSE)
		_rtw_memcpy(&pmlmepriv->assoc_bssid[0], &pnetwork->network.MacAddress[0], ETH_ALEN);

	/* copy fixed ie */
	_rtw_memcpy(psecnetwork->IEs, pnetwork->network.IEs, 12);
	psecnetwork->IELength = 12;

	psecnetwork->IELength += rtw_restruct_sec_ie(padapter, psecnetwork->IEs + psecnetwork->IELength);


	pqospriv->qos_option = 0;

	if (pregistrypriv->wmm_enable) {
#ifdef CONFIG_WMMPS_STA	
		rtw_uapsd_use_default_setting(padapter);
#endif /* CONFIG_WMMPS_STA */		
		tmp_len = rtw_restruct_wmm_ie(padapter, &pnetwork->network.IEs[0], &psecnetwork->IEs[0], pnetwork->network.IELength, psecnetwork->IELength);

		if (psecnetwork->IELength != tmp_len) {
			psecnetwork->IELength = tmp_len;
			pqospriv->qos_option = 1; /* There is WMM IE in this corresp. beacon */
		} else {
			pqospriv->qos_option = 0;/* There is no WMM IE in this corresp. beacon */
		}
	}

#ifdef CONFIG_80211N_HT
	phtpriv->ht_option = _FALSE;
	if (pregistrypriv->ht_enable && is_supported_ht(pregistrypriv->wireless_mode)) {
		ptmp = rtw_get_ie(&pnetwork->network.IEs[12], _HT_CAPABILITY_IE_, &tmp_len, pnetwork->network.IELength - 12);
		if (ptmp && tmp_len > 0) {
			/*	Added by Albert 2010/06/23 */
			/*	For the WEP mode, we will use the bg mode to do the connection to avoid some IOT issue. */
			/*	Especially for Realtek 8192u SoftAP. */
			if ((padapter->securitypriv.dot11PrivacyAlgrthm != _WEP40_) &&
			    (padapter->securitypriv.dot11PrivacyAlgrthm != _WEP104_) &&
			    (padapter->securitypriv.dot11PrivacyAlgrthm != _TKIP_)) {
				rtw_ht_use_default_setting(padapter);

				/* rtw_restructure_ht_ie */
				rtw_restructure_ht_ie(padapter, &pnetwork->network.IEs[12], &psecnetwork->IEs[0],
					pnetwork->network.IELength - 12, &psecnetwork->IELength,
					pnetwork->network.Configuration.DSConfig);
			}
		}
	}

#ifdef CONFIG_80211AC_VHT
	pvhtpriv->vht_option = _FALSE;
	if (phtpriv->ht_option
		&& REGSTY_IS_11AC_ENABLE(pregistrypriv)
		&& is_supported_vht(pregistrypriv->wireless_mode)
		&& (!rfctl->country_ent || COUNTRY_CHPLAN_EN_11AC(rfctl->country_ent))
		&& ((padapter->registrypriv.wifi_spec == 0) || (pnetwork->network.Configuration.DSConfig > 14))
	) {
		rtw_restructure_vht_ie(padapter, &pnetwork->network.IEs[0], &psecnetwork->IEs[0],
			pnetwork->network.IELength, &psecnetwork->IELength);
	}
#endif
#endif /* CONFIG_80211N_HT */

	rtw_append_exented_cap(padapter, &psecnetwork->IEs[0], &psecnetwork->IELength);

#ifdef CONFIG_RTW_80211R
	rtw_ft_validate_akm_type(padapter, pnetwork);
#endif

#if 0
	psecuritypriv->supplicant_ie[0] = (u8)psecnetwork->IELength;

	if (psecnetwork->IELength < (256 - 1))
		_rtw_memcpy(&psecuritypriv->supplicant_ie[1], &psecnetwork->IEs[0], psecnetwork->IELength);
	else
		_rtw_memcpy(&psecuritypriv->supplicant_ie[1], &psecnetwork->IEs[0], (256 - 1));
#endif

	pcmd->cmdsz = sizeof(WLAN_BSSID_EX);

	_rtw_init_listhead(&pcmd->list);
	pcmd->cmdcode = CMD_JOINBSS;/* _JoinBss_CMD_ */
	pcmd->parmbuf = (unsigned char *)psecnetwork;
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	res = rtw_enqueue_cmd(pcmdpriv, pcmd);

exit:


	return res;
}

u8 rtw_disassoc_cmd(_adapter *padapter, u32 deauth_timeout_ms, int flags) /* for sta_mode */
{
	struct cmd_obj *cmdobj = NULL;
	struct disconnect_parm *param = NULL;
	struct cmd_priv *cmdpriv = &padapter->cmdpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	struct submit_ctx sctx;
	u8 res = _SUCCESS;

	/* prepare cmd parameter */
	param = (struct disconnect_parm *)rtw_zmalloc(sizeof(*param));
	if (param == NULL) {
		res = _FAIL;
		goto exit;
	}
	param->deauth_timeout_ms = deauth_timeout_ms;

	if (flags & RTW_CMDF_DIRECTLY) {
		/* no need to enqueue, do the cmd hdl directly and free cmd parameter */
		if (disconnect_hdl(padapter, (u8 *)param) != H2C_SUCCESS)
			res = _FAIL;
		rtw_mfree((u8 *)param, sizeof(*param));

	} else {
		cmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(*cmdobj));
		if (cmdobj == NULL) {
			res = _FAIL;
			rtw_mfree((u8 *)param, sizeof(*param));
			goto exit;
		}
		init_h2fwcmd_w_parm_no_rsp(cmdobj, param, CMD_DISCONNECT);
		if (flags & RTW_CMDF_WAIT_ACK) {
			cmdobj->sctx = &sctx;
			rtw_sctx_init(&sctx, 2000);
		}
		res = rtw_enqueue_cmd(cmdpriv, cmdobj);
		if (res == _SUCCESS && (flags & RTW_CMDF_WAIT_ACK)) {
			rtw_sctx_wait(&sctx, __func__);
			_enter_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
			if (sctx.status == RTW_SCTX_SUBMITTED)
				cmdobj->sctx = NULL;
			_exit_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
		}
	}

exit:


	return res;
}


u8 rtw_stop_ap_cmd(_adapter  *adapter, u8 flags)
{
#ifdef CONFIG_AP_MODE
	struct cmd_obj *cmdobj;
	struct drvextra_cmd_parm *parm;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	struct submit_ctx sctx;
	u8 res = _SUCCESS;

	if (flags & RTW_CMDF_DIRECTLY) {
		/* no need to enqueue, do the cmd hdl directly and free cmd parameter */
		if (H2C_SUCCESS != stop_ap_hdl(adapter))
			res = _FAIL;
	} else {
		parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
		if (parm == NULL) {
			res = _FAIL;
			goto exit;
		}

		parm->ec_id = STOP_AP_WK_CID;
		parm->type = 0;
		parm->size = 0;
		parm->pbuf = NULL;
		
		/* need enqueue, prepare cmd_obj and enqueue */
		cmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(*cmdobj));
		if (cmdobj == NULL) {
			res = _FAIL;
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(cmdobj, parm, CMD_SET_DRV_EXTRA);

		if (flags & RTW_CMDF_WAIT_ACK) {
			cmdobj->sctx = &sctx;
			rtw_sctx_init(&sctx, 2000);
		}

		res = rtw_enqueue_cmd(pcmdpriv, cmdobj);

		if (res == _SUCCESS && (flags & RTW_CMDF_WAIT_ACK)) {
			rtw_sctx_wait(&sctx, __func__);
			_enter_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
			if (sctx.status == RTW_SCTX_SUBMITTED)
				cmdobj->sctx = NULL;
			_exit_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
		}
	}

exit:
	return res;
#endif
}

#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
u8 rtw_tx_control_cmd(_adapter *adapter)
{
	struct cmd_obj		*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;

	u8 res = _SUCCESS;
	
	ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL){
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = TBTX_CONTROL_TX_WK_CID;
	pdrvextra_cmd_parm->type = 0;
	pdrvextra_cmd_parm->size = 0;
	pdrvextra_cmd_parm->pbuf = NULL;
	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:
	return res;	
}
#endif

u8 rtw_setopmode_cmd(_adapter  *adapter, NDIS_802_11_NETWORK_INFRASTRUCTURE networktype, u8 flags)
{
	struct cmd_obj *cmdobj;
	struct setopmode_parm *parm;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	struct submit_ctx sctx;
	u8 res = _SUCCESS;

	/* prepare cmd parameter */
	parm = (struct setopmode_parm *)rtw_zmalloc(sizeof(*parm));
	if (parm == NULL) {
		res = _FAIL;
		goto exit;
	}
	parm->mode = (u8)networktype;

	if (flags & RTW_CMDF_DIRECTLY) {
		/* no need to enqueue, do the cmd hdl directly and free cmd parameter */
		if (H2C_SUCCESS != setopmode_hdl(adapter, (u8 *)parm))
			res = _FAIL;
		rtw_mfree((u8 *)parm, sizeof(*parm));
	} else {
		/* need enqueue, prepare cmd_obj and enqueue */
		cmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(*cmdobj));
		if (cmdobj == NULL) {
			res = _FAIL;
			rtw_mfree((u8 *)parm, sizeof(*parm));
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(cmdobj, parm, CMD_SET_OPMODE);

		if (flags & RTW_CMDF_WAIT_ACK) {
			cmdobj->sctx = &sctx;
			rtw_sctx_init(&sctx, 2000);
		}

		res = rtw_enqueue_cmd(pcmdpriv, cmdobj);

		if (res == _SUCCESS && (flags & RTW_CMDF_WAIT_ACK)) {
			rtw_sctx_wait(&sctx, __func__);
			_enter_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
			if (sctx.status == RTW_SCTX_SUBMITTED)
				cmdobj->sctx = NULL;
			_exit_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
		}
	}

exit:
	return res;
}

u8 rtw_setstakey_cmd(_adapter *padapter, struct sta_info *sta, u8 key_type, bool enqueue)
{
	struct cmd_obj			*ph2c;
	struct set_stakey_parm	*psetstakey_para;
	struct cmd_priv			*pcmdpriv = &padapter->cmdpriv;
	struct set_stakey_rsp		*psetstakey_rsp = NULL;

	struct mlme_priv			*pmlmepriv = &padapter->mlmepriv;
	struct security_priv		*psecuritypriv = &padapter->securitypriv;
	u8 key_len =16;
	u8	res = _SUCCESS;


	psetstakey_para = (struct set_stakey_parm *)rtw_zmalloc(sizeof(struct set_stakey_parm));
	if (psetstakey_para == NULL) {
		res = _FAIL;
		goto exit;
	}

	_rtw_memcpy(psetstakey_para->addr, sta->cmn.mac_addr, ETH_ALEN);

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE))
		psetstakey_para->algorithm = (unsigned char) psecuritypriv->dot11PrivacyAlgrthm;
	else
		GET_ENCRY_ALGO(psecuritypriv, sta, psetstakey_para->algorithm, _FALSE);

	if ((psetstakey_para->algorithm == _GCMP_256_) || (psetstakey_para->algorithm == _CCMP_256_)) 
		key_len = 32;

	if (key_type == GROUP_KEY) {
		_rtw_memcpy(&psetstakey_para->key, &psecuritypriv->dot118021XGrpKey[psecuritypriv->dot118021XGrpKeyid].skey, key_len);
		psetstakey_para->gk = 1;
	} else if (key_type == UNICAST_KEY)
		_rtw_memcpy(&psetstakey_para->key, &sta->dot118021x_UncstKey, key_len);
#ifdef CONFIG_TDLS
	else if (key_type == TDLS_KEY) {
		_rtw_memcpy(&psetstakey_para->key, sta->tpk.tk, key_len);
		psetstakey_para->algorithm = (u8)sta->dot118021XPrivacy;
	}
#endif /* CONFIG_TDLS */

	/* jeff: set this becasue at least sw key is ready */
	padapter->securitypriv.busetkipkey = _TRUE;

	if (enqueue) {
		ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
		if (ph2c == NULL) {
			rtw_mfree((u8 *) psetstakey_para, sizeof(struct set_stakey_parm));
			res = _FAIL;
			goto exit;
		}

		psetstakey_rsp = (struct set_stakey_rsp *)rtw_zmalloc(sizeof(struct set_stakey_rsp));
		if (psetstakey_rsp == NULL) {
			rtw_mfree((u8 *) ph2c, sizeof(struct cmd_obj));
			rtw_mfree((u8 *) psetstakey_para, sizeof(struct set_stakey_parm));
			res = _FAIL;
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(ph2c, psetstakey_para, CMD_SET_STAKEY);
		ph2c->rsp = (u8 *) psetstakey_rsp;
		ph2c->rspsz = sizeof(struct set_stakey_rsp);
		res = rtw_enqueue_cmd(pcmdpriv, ph2c);
	} else {
		set_stakey_hdl(padapter, (u8 *)psetstakey_para);
		rtw_mfree((u8 *) psetstakey_para, sizeof(struct set_stakey_parm));
	}
exit:


	return res;
}

u8 rtw_clearstakey_cmd(_adapter *padapter, struct sta_info *sta, u8 enqueue)
{
	struct cmd_obj			*ph2c;
	struct set_stakey_parm	*psetstakey_para;
	struct cmd_priv			*pcmdpriv = &padapter->cmdpriv;
	struct set_stakey_rsp		*psetstakey_rsp = NULL;
	s16 cam_id = 0;
	u8	res = _SUCCESS;

	if (!sta) {
		RTW_ERR("%s sta == NULL\n", __func__);
		goto exit;
	}

	if (!enqueue) {
		while ((cam_id = rtw_camid_search(padapter, sta->cmn.mac_addr, -1, -1)) >= 0) {
			RTW_PRINT("clear key for addr:"MAC_FMT", camid:%d\n", MAC_ARG(sta->cmn.mac_addr), cam_id);
			clear_cam_entry(padapter, cam_id);
			rtw_camid_free(padapter, cam_id);
		}
	} else {
		ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
		if (ph2c == NULL) {
			res = _FAIL;
			goto exit;
		}

		psetstakey_para = (struct set_stakey_parm *)rtw_zmalloc(sizeof(struct set_stakey_parm));
		if (psetstakey_para == NULL) {
			rtw_mfree((u8 *) ph2c, sizeof(struct	cmd_obj));
			res = _FAIL;
			goto exit;
		}

		psetstakey_rsp = (struct set_stakey_rsp *)rtw_zmalloc(sizeof(struct set_stakey_rsp));
		if (psetstakey_rsp == NULL) {
			rtw_mfree((u8 *) ph2c, sizeof(struct	cmd_obj));
			rtw_mfree((u8 *) psetstakey_para, sizeof(struct set_stakey_parm));
			res = _FAIL;
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(ph2c, psetstakey_para, CMD_SET_STAKEY);
		ph2c->rsp = (u8 *) psetstakey_rsp;
		ph2c->rspsz = sizeof(struct set_stakey_rsp);

		_rtw_memcpy(psetstakey_para->addr, sta->cmn.mac_addr, ETH_ALEN);

		psetstakey_para->algorithm = _NO_PRIVACY_;

		res = rtw_enqueue_cmd(pcmdpriv, ph2c);

	}

exit:


	return res;
}

u8 rtw_addbareq_cmd(_adapter *padapter, u8 tid, u8 *addr)
{
	struct cmd_priv		*pcmdpriv = &padapter->cmdpriv;
	struct cmd_obj		*ph2c;
	struct addBaReq_parm	*paddbareq_parm;

	u8	res = _SUCCESS;


	ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	paddbareq_parm = (struct addBaReq_parm *)rtw_zmalloc(sizeof(struct addBaReq_parm));
	if (paddbareq_parm == NULL) {
		rtw_mfree((unsigned char *)ph2c, sizeof(struct	cmd_obj));
		res = _FAIL;
		goto exit;
	}

	paddbareq_parm->tid = tid;
	_rtw_memcpy(paddbareq_parm->addr, addr, ETH_ALEN);

	init_h2fwcmd_w_parm_no_rsp(ph2c, paddbareq_parm, CMD_ADD_BAREQ);

	/* RTW_INFO("rtw_addbareq_cmd, tid=%d\n", tid); */

	/* rtw_enqueue_cmd(pcmdpriv, ph2c);	 */
	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:


	return res;
}

u8 rtw_addbarsp_cmd(_adapter *padapter, u8 *addr, u16 tid, u8 status, u8 size, u16 start_seq)
{
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	struct cmd_obj *ph2c;
	struct addBaRsp_parm *paddBaRsp_parm;
	u8 res = _SUCCESS;


	ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	paddBaRsp_parm = (struct addBaRsp_parm *)rtw_zmalloc(sizeof(struct addBaRsp_parm));

	if (paddBaRsp_parm == NULL) {
		rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	_rtw_memcpy(paddBaRsp_parm->addr, addr, ETH_ALEN);
	paddBaRsp_parm->tid = tid;
	paddBaRsp_parm->status = status;
	paddBaRsp_parm->size = size;
	paddBaRsp_parm->start_seq = start_seq;

	init_h2fwcmd_w_parm_no_rsp(ph2c, paddBaRsp_parm, CMD_ADD_BARSP);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:


	return res;
}
/* add for CONFIG_IEEE80211W, none 11w can use it */
u8 rtw_reset_securitypriv_cmd(_adapter *padapter)
{
	struct cmd_obj		*ph2c;
	struct drvextra_cmd_parm  *pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;


	ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = RESET_SECURITYPRIV;
	pdrvextra_cmd_parm->type = 0;
	pdrvextra_cmd_parm->size = 0;
	pdrvextra_cmd_parm->pbuf = NULL;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);


	/* rtw_enqueue_cmd(pcmdpriv, ph2c);	 */
	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:


	return res;

}

void free_assoc_resources_hdl(_adapter *padapter, u8 lock_scanned_queue)
{
	rtw_free_assoc_resources(padapter, lock_scanned_queue);
}

u8 rtw_free_assoc_resources_cmd(_adapter *padapter, u8 lock_scanned_queue, int flags)
{
	struct cmd_obj *cmd;
	struct drvextra_cmd_parm  *pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	struct submit_ctx sctx;
	u8	res = _SUCCESS;

	if (flags & RTW_CMDF_DIRECTLY) {
		free_assoc_resources_hdl(padapter, lock_scanned_queue);
	}
	else {
		cmd = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
		if (cmd == NULL) {
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
		if (pdrvextra_cmd_parm == NULL) {
			rtw_mfree((unsigned char *)cmd, sizeof(struct cmd_obj));
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm->ec_id = FREE_ASSOC_RESOURCES;
		pdrvextra_cmd_parm->type = lock_scanned_queue;
		pdrvextra_cmd_parm->size = 0;
		pdrvextra_cmd_parm->pbuf = NULL;

		init_h2fwcmd_w_parm_no_rsp(cmd, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);
		if (flags & RTW_CMDF_WAIT_ACK) {
			cmd->sctx = &sctx;
			rtw_sctx_init(&sctx, 2000);
		}

		res = rtw_enqueue_cmd(pcmdpriv, cmd);

		if (res == _SUCCESS && (flags & RTW_CMDF_WAIT_ACK)) {
			rtw_sctx_wait(&sctx, __func__);
			_enter_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
			if (sctx.status == RTW_SCTX_SUBMITTED)
				cmd->sctx = NULL;
			_exit_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
		}
	}
exit:
	return res;

}

u8 rtw_dynamic_chk_wk_cmd(_adapter *padapter)
{
	struct cmd_obj		*ph2c;
	struct drvextra_cmd_parm  *pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;


	/* only  primary padapter does this cmd */

	ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = DYNAMIC_CHK_WK_CID;
	pdrvextra_cmd_parm->type = 0;
	pdrvextra_cmd_parm->size = 0;
	pdrvextra_cmd_parm->pbuf = NULL;
	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);


	/* rtw_enqueue_cmd(pcmdpriv, ph2c);	 */
	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:


	return res;

}

u8 rtw_set_chbw_cmd(_adapter *padapter, u8 ch, u8 bw, u8 ch_offset, u8 flags)
{
	struct cmd_obj *pcmdobj;
	struct set_ch_parm *set_ch_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	struct submit_ctx sctx;
	u8 res = _SUCCESS;


	RTW_INFO(FUNC_NDEV_FMT" ch:%u, bw:%u, ch_offset:%u\n",
		 FUNC_NDEV_ARG(padapter->pnetdev), ch, bw, ch_offset);

	/* check input parameter */

	/* prepare cmd parameter */
	set_ch_parm = (struct set_ch_parm *)rtw_zmalloc(sizeof(*set_ch_parm));
	if (set_ch_parm == NULL) {
		res = _FAIL;
		goto exit;
	}
	set_ch_parm->ch = ch;
	set_ch_parm->bw = bw;
	set_ch_parm->ch_offset = ch_offset;

	if (flags & RTW_CMDF_DIRECTLY) {
		/* no need to enqueue, do the cmd hdl directly and free cmd parameter */
		if (H2C_SUCCESS != rtw_set_chbw_hdl(padapter, (u8 *)set_ch_parm))
			res = _FAIL;

		rtw_mfree((u8 *)set_ch_parm, sizeof(*set_ch_parm));
	} else {
		/* need enqueue, prepare cmd_obj and enqueue */
		pcmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct	cmd_obj));
		if (pcmdobj == NULL) {
			rtw_mfree((u8 *)set_ch_parm, sizeof(*set_ch_parm));
			res = _FAIL;
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(pcmdobj, set_ch_parm, CMD_SET_CHANNEL);

		if (flags & RTW_CMDF_WAIT_ACK) {
			pcmdobj->sctx = &sctx;
			rtw_sctx_init(&sctx, 10 * 1000);
		}

		res = rtw_enqueue_cmd(pcmdpriv, pcmdobj);

		if (res == _SUCCESS && (flags & RTW_CMDF_WAIT_ACK)) {
			rtw_sctx_wait(&sctx, __func__);
			_enter_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
			if (sctx.status == RTW_SCTX_SUBMITTED)
				pcmdobj->sctx = NULL;
			_exit_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
		}
	}

	/* do something based on res... */

exit:

	RTW_INFO(FUNC_NDEV_FMT" res:%u\n", FUNC_NDEV_ARG(padapter->pnetdev), res);


	return res;
}

u8 _rtw_set_chplan_cmd(_adapter *adapter, int flags, u8 chplan, const struct country_chplan *country_ent, u8 swconfig)
{
	struct cmd_obj *cmdobj;
	struct	SetChannelPlan_param *parm;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	struct submit_ctx sctx;
	u8 res = _SUCCESS;


	/* check if allow software config */
	if (swconfig && rtw_hal_is_disable_sw_channel_plan(adapter) == _TRUE) {
		res = _FAIL;
		goto exit;
	}

	/* if country_entry is provided, replace chplan */
	if (country_ent)
		chplan = country_ent->chplan;

	/* check input parameter */
	if (!rtw_is_channel_plan_valid(chplan)) {
		res = _FAIL;
		goto exit;
	}

	/* prepare cmd parameter */
	parm = (struct SetChannelPlan_param *)rtw_zmalloc(sizeof(*parm));
	if (parm == NULL) {
		res = _FAIL;
		goto exit;
	}
	parm->country_ent = country_ent;
	parm->channel_plan = chplan;

	if (flags & RTW_CMDF_DIRECTLY) {
		/* no need to enqueue, do the cmd hdl directly and free cmd parameter */
		if (H2C_SUCCESS != set_chplan_hdl(adapter, (u8 *)parm))
			res = _FAIL;
		rtw_mfree((u8 *)parm, sizeof(*parm));
	} else {
		/* need enqueue, prepare cmd_obj and enqueue */
		cmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(*cmdobj));
		if (cmdobj == NULL) {
			res = _FAIL;
			rtw_mfree((u8 *)parm, sizeof(*parm));
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(cmdobj, parm, CMD_SET_CHANPLAN);

		if (flags & RTW_CMDF_WAIT_ACK) {
			cmdobj->sctx = &sctx;
			rtw_sctx_init(&sctx, 2000);
		}

		res = rtw_enqueue_cmd(pcmdpriv, cmdobj);

		if (res == _SUCCESS && (flags & RTW_CMDF_WAIT_ACK)) {
			rtw_sctx_wait(&sctx, __func__);
			_enter_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
			if (sctx.status == RTW_SCTX_SUBMITTED)
				cmdobj->sctx = NULL;
			_exit_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
			if (sctx.status != RTW_SCTX_DONE_SUCCESS)
				res = _FAIL;
		}

		/* allow set channel plan when cmd_thread is not running */
		if (res != _SUCCESS && (flags & RTW_CMDF_WAIT_ACK)) {
			parm = (struct SetChannelPlan_param *)rtw_zmalloc(sizeof(*parm));
			if (parm == NULL) {
				res = _FAIL;
				goto exit;
			}
			parm->country_ent = country_ent;
			parm->channel_plan = chplan;

			if (H2C_SUCCESS != set_chplan_hdl(adapter, (u8 *)parm))
				res = _FAIL;
			else
				res = _SUCCESS;
			rtw_mfree((u8 *)parm, sizeof(*parm));
		}
	}

exit:
	return res;
}

inline u8 rtw_set_chplan_cmd(_adapter *adapter, int flags, u8 chplan, u8 swconfig)
{
	return _rtw_set_chplan_cmd(adapter, flags, chplan, NULL, swconfig);
}

inline u8 rtw_set_country_cmd(_adapter *adapter, int flags, const char *country_code, u8 swconfig)
{
	const struct country_chplan *ent;

	if (is_alpha(country_code[0]) == _FALSE
	    || is_alpha(country_code[1]) == _FALSE
	   ) {
		RTW_PRINT("%s input country_code is not alpha2\n", __func__);
		return _FAIL;
	}

	ent = rtw_get_chplan_from_country(country_code);

	if (ent == NULL) {
		RTW_PRINT("%s unsupported country_code:\"%c%c\"\n", __func__, country_code[0], country_code[1]);
		return _FAIL;
	}

	RTW_PRINT("%s country_code:\"%c%c\" mapping to chplan:0x%02x\n", __func__, country_code[0], country_code[1], ent->chplan);

	return _rtw_set_chplan_cmd(adapter, flags, RTW_CHPLAN_UNSPECIFIED, ent, swconfig);
}

u8 rtw_led_blink_cmd(_adapter *padapter, void *pLed)
{
	struct	cmd_obj	*pcmdobj;
	struct	LedBlink_param *ledBlink_param;
	struct	cmd_priv   *pcmdpriv = &padapter->cmdpriv;

	u8	res = _SUCCESS;



	pcmdobj = (struct	cmd_obj *)rtw_zmalloc(sizeof(struct	cmd_obj));
	if (pcmdobj == NULL) {
		res = _FAIL;
		goto exit;
	}

	ledBlink_param = (struct	LedBlink_param *)rtw_zmalloc(sizeof(struct	LedBlink_param));
	if (ledBlink_param == NULL) {
		rtw_mfree((u8 *)pcmdobj, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	ledBlink_param->pLed = pLed;

	init_h2fwcmd_w_parm_no_rsp(pcmdobj, ledBlink_param, CMD_LEDBLINK);
	res = rtw_enqueue_cmd(pcmdpriv, pcmdobj);

exit:


	return res;
}

u8 rtw_set_csa_cmd(_adapter *adapter)
{
	struct cmd_obj *cmdobj;
	struct cmd_priv *cmdpriv = &adapter->cmdpriv;
	u8	res = _SUCCESS;

	cmdobj = rtw_zmalloc(sizeof(struct cmd_obj));
	if (cmdobj == NULL) {
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_parm_rsp(cmdobj, CMD_SET_CHANSWITCH);
	res = rtw_enqueue_cmd(cmdpriv, cmdobj);

exit:
	return res;
}

u8 rtw_tdls_cmd(_adapter *padapter, u8 *addr, u8 option)
{
	u8 res = _SUCCESS;
#ifdef CONFIG_TDLS
	struct	cmd_obj	*pcmdobj;
	struct	TDLSoption_param	*TDLSoption;
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct	cmd_priv   *pcmdpriv = &padapter->cmdpriv;

	pcmdobj = (struct	cmd_obj *)rtw_zmalloc(sizeof(struct	cmd_obj));
	if (pcmdobj == NULL) {
		res = _FAIL;
		goto exit;
	}

	TDLSoption = (struct TDLSoption_param *)rtw_zmalloc(sizeof(struct TDLSoption_param));
	if (TDLSoption == NULL) {
		rtw_mfree((u8 *)pcmdobj, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	_rtw_spinlock(&(padapter->tdlsinfo.cmd_lock));
	if (addr != NULL)
		_rtw_memcpy(TDLSoption->addr, addr, 6);
	TDLSoption->option = option;
	_rtw_spinunlock(&(padapter->tdlsinfo.cmd_lock));
	init_h2fwcmd_w_parm_no_rsp(pcmdobj, TDLSoption, CMD_TDLS);
	res = rtw_enqueue_cmd(pcmdpriv, pcmdobj);

exit:
#endif /* CONFIG_TDLS */

	return res;
}

u8 rtw_enable_hw_update_tsf_cmd(_adapter *padapter)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;


	ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = EN_HW_UPDATE_TSF_WK_CID;
	pdrvextra_cmd_parm->type = 0;
	pdrvextra_cmd_parm->size = 0;
	pdrvextra_cmd_parm->pbuf = NULL;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:
	return res;
}

u8 rtw_periodic_tsf_update_end_cmd(_adapter *adapter)
{
	struct cmd_obj *cmdobj;
	struct drvextra_cmd_parm *parm;
	struct cmd_priv *cmdpriv = &adapter->cmdpriv;
	u8 res = _SUCCESS;

	cmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (cmdobj == NULL) {
		res = _FAIL;
		goto exit;
	}

	parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (parm == NULL) {
		rtw_mfree((unsigned char *)cmdobj, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	parm->ec_id = PERIOD_TSF_UPDATE_END_WK_CID;
	parm->type = 0;
	parm->size = 0;
	parm->pbuf = NULL;

	init_h2fwcmd_w_parm_no_rsp(cmdobj, parm, CMD_SET_DRV_EXTRA);

	res = rtw_enqueue_cmd(cmdpriv, cmdobj);

exit:
	return res;
}
u8 rtw_ssmps_wk_hdl(_adapter *adapter, struct ssmps_cmd_parm *ssmp_param)
{
	u8 res = _SUCCESS;
	struct sta_info *sta = ssmp_param->sta;
	u8 smps = ssmp_param->smps;

	if (sta == NULL)
		return _FALSE;

	if (smps)
		rtw_ssmps_enter(adapter, sta);
	else
		rtw_ssmps_leave(adapter, sta);
	return res;
}

u8 rtw_ssmps_wk_cmd(_adapter *adapter, struct sta_info *sta, u8 smps, u8 enqueue)
{
	struct cmd_obj *cmdobj;
	struct drvextra_cmd_parm *cmd_parm;
	struct ssmps_cmd_parm *ssmp_param;
	struct cmd_priv	*pcmdpriv = &adapter->cmdpriv;
	u8	res = _SUCCESS;

	if (enqueue) {
		cmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
		if (cmdobj == NULL) {
			res = _FAIL;
			goto exit;
		}

		cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
		if (cmd_parm == NULL) {
			rtw_mfree((unsigned char *)cmdobj, sizeof(struct cmd_obj));
			res = _FAIL;
			goto exit;
		}

		ssmp_param = (struct ssmps_cmd_parm *)rtw_zmalloc(sizeof(struct ssmps_cmd_parm));
		if (ssmp_param == NULL) {
			rtw_mfree((u8 *)cmdobj, sizeof(struct cmd_obj));
			rtw_mfree((u8 *)cmd_parm, sizeof(struct drvextra_cmd_parm));
			res = _FAIL;
			goto exit;
		}

		ssmp_param->smps = smps;
		ssmp_param->sta = sta;

		cmd_parm->ec_id = SSMPS_WK_CID;
		cmd_parm->type = 0;
		cmd_parm->size = sizeof(struct ssmps_cmd_parm);
		cmd_parm->pbuf = (u8 *)ssmp_param;

		init_h2fwcmd_w_parm_no_rsp(cmdobj, cmd_parm, CMD_SET_DRV_EXTRA);

		res = rtw_enqueue_cmd(pcmdpriv, cmdobj);
	} else {
		struct ssmps_cmd_parm tmp_ssmp_param;

		tmp_ssmp_param.smps = smps;
		tmp_ssmp_param.sta = sta;
		rtw_ssmps_wk_hdl(adapter, &tmp_ssmp_param);
	}

exit:
	return res;
}

#ifdef CONFIG_SUPPORT_STATIC_SMPS
u8 _ssmps_chk_by_tp(_adapter *adapter, u8 from_timer)
{
	u8 enter_smps = _FALSE;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &(adapter->mlmeextpriv);
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct sta_info *psta;
	u32 tx_tp_mbits, rx_tp_mbits;

	if (!MLME_IS_STA(adapter) ||
		!hal_is_mimo_support(adapter) ||
		!pmlmeext->ssmps_en ||
		(pmlmeext->cur_channel > 14)
	)
		return enter_smps;

	psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
	if (psta == NULL) {
		RTW_ERR(ADPT_FMT" sta == NULL\n", ADPT_ARG(adapter));
		rtw_warn_on(1);
		return enter_smps;
	}

	if (psta->cmn.mimo_type == RF_1T1R)
		return enter_smps;

	tx_tp_mbits = psta->sta_stats.tx_tp_kbits >> 10;
	rx_tp_mbits = psta->sta_stats.rx_tp_kbits >> 10;

	#ifdef DBG_STATIC_SMPS
	if (pmlmeext->ssmps_test) {
		enter_smps = (pmlmeext->ssmps_test_en == 1) ? _TRUE : _FALSE;
	}
	else
	#endif
	{
		if ((tx_tp_mbits <= pmlmeext->ssmps_tx_tp_th) &&
			(rx_tp_mbits <= pmlmeext->ssmps_rx_tp_th))
			enter_smps = _TRUE;
		else
			enter_smps = _FALSE;
	}

	if (1) {
		RTW_INFO(FUNC_ADPT_FMT" tx_tp:%d [%d], rx_tp:%d [%d] , SSMPS enter :%s\n",
			FUNC_ADPT_ARG(adapter),
			tx_tp_mbits, pmlmeext->ssmps_tx_tp_th,
			rx_tp_mbits, pmlmeext->ssmps_rx_tp_th,
			(enter_smps == _TRUE) ? "True" : "False");
		#ifdef DBG_STATIC_SMPS
		RTW_INFO(FUNC_ADPT_FMT" test:%d test_en:%d\n",
			FUNC_ADPT_ARG(adapter),
			pmlmeext->ssmps_test,
			pmlmeext->ssmps_test_en);
		#endif
	}

	if (enter_smps) {
		if (!from_timer && psta->cmn.sm_ps != SM_PS_STATIC)
			rtw_ssmps_enter(adapter, psta);
	} else {
		if (!from_timer && psta->cmn.sm_ps != SM_PS_DISABLE)
			rtw_ssmps_leave(adapter, psta);
		else {
			u8 ps_change = _FALSE;

			if (enter_smps && psta->cmn.sm_ps != SM_PS_STATIC)
				ps_change = _TRUE;
			else if (!enter_smps && psta->cmn.sm_ps != SM_PS_DISABLE)
				ps_change = _TRUE;

			if (ps_change)
				rtw_ssmps_wk_cmd(adapter, psta, enter_smps, 1);
		}
	}

	return enter_smps;
}
#endif /*CONFIG_SUPPORT_STATIC_SMPS*/

#ifdef CONFIG_CTRL_TXSS_BY_TP
void rtw_ctrl_txss_update_mimo_type(_adapter *adapter, struct sta_info *sta)
{
	struct mlme_ext_priv *pmlmeext = &(adapter->mlmeextpriv);

	pmlmeext->txss_momi_type_bk = sta->cmn.mimo_type;
}

u8 rtw_ctrl_txss(_adapter *adapter, struct sta_info *sta, bool tx_1ss)
{
	struct mlme_ext_priv *pmlmeext = &(adapter->mlmeextpriv);
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	u8 lps_changed = _FALSE;
	u8 rst = _SUCCESS;

	if (pmlmeext->txss_1ss == tx_1ss)
		return _FALSE;

	if (pwrpriv->bLeisurePs && pwrpriv->pwr_mode != PS_MODE_ACTIVE) {
		lps_changed = _TRUE;
		LPS_Leave(adapter, "LPS_CTRL_TXSS");
	}

	RTW_INFO(ADPT_FMT" STA [" MAC_FMT "] set tx to %d ss\n",
		ADPT_ARG(adapter), MAC_ARG(sta->cmn.mac_addr),
		(tx_1ss) ? 1 : rtw_get_sta_tx_nss(adapter, sta));

	/*ra re-registed*/
	sta->cmn.mimo_type = (tx_1ss) ? RF_1T1R : pmlmeext->txss_momi_type_bk;
	rtw_phydm_ra_registed(adapter, sta);

	/*configure trx mode*/
	rtw_phydm_trx_cfg(adapter, tx_1ss);
	pmlmeext->txss_1ss = tx_1ss;

	if (lps_changed)
		LPS_Enter(adapter, "LPS_CTRL_TXSS");

	return rst;
}

u8 rtw_ctrl_txss_wk_hdl(_adapter *adapter, struct txss_cmd_parm *txss_param)
{
	if (!txss_param->sta)
		return _FALSE;

	return rtw_ctrl_txss(adapter, txss_param->sta, txss_param->tx_1ss);
}

u8 rtw_ctrl_txss_wk_cmd(_adapter *adapter, struct sta_info *sta, bool tx_1ss, u8 flag)
{
	struct cmd_obj *cmdobj;
	struct drvextra_cmd_parm *cmd_parm;
	struct txss_cmd_parm *txss_param;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	struct submit_ctx sctx;
	u8	res = _SUCCESS;

	txss_param = (struct txss_cmd_parm *)rtw_zmalloc(sizeof(struct txss_cmd_parm));
	if (txss_param == NULL) {
		res = _FAIL;
		goto exit;
	}

	txss_param->tx_1ss = tx_1ss;
	txss_param->sta = sta;

	if (flag & RTW_CMDF_DIRECTLY) {
		res = rtw_ctrl_txss_wk_hdl(adapter, txss_param);
		rtw_mfree((u8 *)txss_param, sizeof(*txss_param));
	} else {
		cmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
		if (cmdobj == NULL) {
			res = _FAIL;
			goto exit;
		}

		cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
		if (cmd_parm == NULL) {
			rtw_mfree((u8 *)cmdobj, sizeof(struct cmd_obj));
			res = _FAIL;
			goto exit;
		}

		cmd_parm->ec_id = TXSS_WK_CID;
		cmd_parm->type = 0;
		cmd_parm->size = sizeof(struct txss_cmd_parm);
		cmd_parm->pbuf = (u8 *)txss_param;

		init_h2fwcmd_w_parm_no_rsp(cmdobj, cmd_parm, CMD_SET_DRV_EXTRA);

		if (flag & RTW_CMDF_WAIT_ACK) {
			cmdobj->sctx = &sctx;
			rtw_sctx_init(&sctx, 10 * 1000);
		}

		res = rtw_enqueue_cmd(pcmdpriv, cmdobj);
		if (res == _SUCCESS && (flag & RTW_CMDF_WAIT_ACK)) {
			rtw_sctx_wait(&sctx, __func__);
			_enter_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
			if (sctx.status == RTW_SCTX_SUBMITTED)
				cmdobj->sctx = NULL;
			_exit_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
			if (sctx.status != RTW_SCTX_DONE_SUCCESS)
				res = _FAIL;
		}
	}

exit:
	return res;
}

void rtw_ctrl_tx_ss_by_tp(_adapter *adapter, u8 from_timer)
{
	bool tx_1ss  = _FALSE; /*change tx from 2ss to 1ss*/
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &(adapter->mlmeextpriv);
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct sta_info *psta;
	u32 tx_tp_mbits;

	if (!MLME_IS_STA(adapter) ||
		!hal_is_mimo_support(adapter) ||
		!pmlmeext->txss_ctrl_en
	)
		return;

	psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
	if (psta == NULL) {
		RTW_ERR(ADPT_FMT" sta == NULL\n", ADPT_ARG(adapter));
		rtw_warn_on(1);
		return;
	}

	tx_tp_mbits = psta->sta_stats.tx_tp_kbits >> 10;
	if (tx_tp_mbits >= pmlmeext->txss_tp_th) {
		tx_1ss = _FALSE;
	} else {
		if (pmlmeext->txss_tp_chk_cnt && --pmlmeext->txss_tp_chk_cnt)
			tx_1ss = _FALSE;
		else
			tx_1ss = _TRUE;
	}

	if (1) {
		RTW_INFO(FUNC_ADPT_FMT" tx_tp:%d [%d] tx_1ss(%d):%s\n",
			FUNC_ADPT_ARG(adapter),
			tx_tp_mbits, pmlmeext->txss_tp_th,
			pmlmeext->txss_tp_chk_cnt,
			(tx_1ss == _TRUE) ? "True" : "False");
	}

	if (pmlmeext->txss_1ss != tx_1ss) {
		if (from_timer)
			rtw_ctrl_txss_wk_cmd(adapter, psta, tx_1ss, 0);
		else
			rtw_ctrl_txss(adapter, psta, tx_1ss);
	}
}
#ifdef DBG_CTRL_TXSS
void dbg_ctrl_txss(_adapter *adapter, bool tx_1ss)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &(adapter->mlmeextpriv);
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct sta_info *psta;

	if (!MLME_IS_STA(adapter) ||
		!hal_is_mimo_support(adapter)
	)
		return;

	psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
	if (psta == NULL) {
		RTW_ERR(ADPT_FMT" sta == NULL\n", ADPT_ARG(adapter));
		rtw_warn_on(1);
		return;
	}

	rtw_ctrl_txss(adapter, psta, tx_1ss);
}
#endif
#endif /*CONFIG_CTRL_TXSS_BY_TP*/

#ifdef CONFIG_LPS
#ifdef CONFIG_LPS_CHK_BY_TP
#ifdef LPS_BCN_CNT_MONITOR
static u8 _bcn_cnt_expected(struct sta_info *psta)
{
	_adapter *adapter = psta->padapter;
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 dtim = rtw_get_bcn_dtim_period(adapter);
	u8 bcn_cnt = 0;

	if ((pmlmeinfo->bcn_interval !=0) && (dtim != 0))
		bcn_cnt = 2000 / pmlmeinfo->bcn_interval / dtim * 4 / 5; /*2s*/
	if (0)
		RTW_INFO("%s bcn_cnt:%d\n", __func__, bcn_cnt);

	if (bcn_cnt == 0) {
		RTW_ERR(FUNC_ADPT_FMT" bcn_cnt == 0\n", FUNC_ADPT_ARG(adapter));
		rtw_warn_on(1);
	}

	return bcn_cnt;
}
#endif
u8 _lps_chk_by_tp(_adapter *adapter, u8 from_timer)
{
	u8 enter_ps = _FALSE;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct sta_info *psta;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	u32 tx_tp_mbits, rx_tp_mbits, bi_tp_mbits;
	u8 rx_bcn_cnt;

	psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
	if (psta == NULL) {
		RTW_ERR(ADPT_FMT" sta == NULL\n", ADPT_ARG(adapter));
		rtw_warn_on(1);
		return enter_ps;
	}

	rx_bcn_cnt = rtw_get_bcn_cnt(psta->padapter);
	psta->sta_stats.acc_tx_bytes = psta->sta_stats.tx_bytes;
	psta->sta_stats.acc_rx_bytes = psta->sta_stats.rx_bytes;

#if 1
	tx_tp_mbits = psta->sta_stats.tx_tp_kbits >> 10;
	rx_tp_mbits = psta->sta_stats.rx_tp_kbits >> 10;
	bi_tp_mbits = tx_tp_mbits + rx_tp_mbits;
#else
	tx_tp_mbits = psta->sta_stats.smooth_tx_tp_kbits >> 10;
	rx_tp_mbits = psta->sta_stats.smooth_rx_tp_kbits >> 10;
	bi_tp_mbits = tx_tp_mbits + rx_tp_mbits;
#endif

	if ((bi_tp_mbits >= pwrpriv->lps_bi_tp_th) ||
		(tx_tp_mbits >= pwrpriv->lps_tx_tp_th) ||
		(rx_tp_mbits >= pwrpriv->lps_rx_tp_th)) {
		enter_ps = _FALSE;
		pwrpriv->lps_chk_cnt = pwrpriv->lps_chk_cnt_th;
	}
	else {
#ifdef LPS_BCN_CNT_MONITOR
		u8 bcn_cnt = _bcn_cnt_expected(psta);

		if (bcn_cnt && (rx_bcn_cnt < bcn_cnt)) {
			pwrpriv->lps_chk_cnt = 2;
			RTW_ERR(FUNC_ADPT_FMT" BCN_CNT:%d(%d) invalid\n",
				FUNC_ADPT_ARG(adapter), rx_bcn_cnt, bcn_cnt);
		}
#endif

		if (pwrpriv->lps_chk_cnt && --pwrpriv->lps_chk_cnt)
			enter_ps = _FALSE;
		else
			enter_ps = _TRUE;
	}

	if (1) {
		RTW_INFO(FUNC_ADPT_FMT" tx_tp:%d [%d], rx_tp:%d [%d], bi_tp:%d [%d], enter_ps(%d):%s\n",
			FUNC_ADPT_ARG(adapter),
			tx_tp_mbits, pwrpriv->lps_tx_tp_th,
			rx_tp_mbits, pwrpriv->lps_rx_tp_th,
			bi_tp_mbits, pwrpriv->lps_bi_tp_th,
			pwrpriv->lps_chk_cnt,
			(enter_ps == _TRUE) ? "True" : "False");
		RTW_INFO(FUNC_ADPT_FMT" tx_pkt_cnt :%d [%d], rx_pkt_cnt :%d [%d]\n",
			FUNC_ADPT_ARG(adapter),
			pmlmepriv->LinkDetectInfo.NumTxOkInPeriod,
			pwrpriv->lps_tx_pkts,
			pmlmepriv->LinkDetectInfo.NumRxUnicastOkInPeriod,
			pwrpriv->lps_rx_pkts);
		if (!adapter->bsta_tp_dump)
			RTW_INFO(FUNC_ADPT_FMT" bcn_cnt:%d (per-%d second)\n",
			FUNC_ADPT_ARG(adapter),
			rx_bcn_cnt,
			2);
	}

	if (enter_ps) {
		if (!from_timer)
			LPS_Enter(adapter, "TRAFFIC_IDLE");
	} else {
		if (!from_timer)
			LPS_Leave(adapter, "TRAFFIC_BUSY");
		else {
			#ifdef CONFIG_CONCURRENT_MODE
			#ifndef CONFIG_FW_MULTI_PORT_SUPPORT
			if (adapter->hw_port == HW_PORT0)
			#endif
			#endif
				rtw_lps_ctrl_wk_cmd(adapter, LPS_CTRL_TRAFFIC_BUSY, 0);
		}
	}

	return enter_ps;
}
#endif

static u8 _lps_chk_by_pkt_cnts(_adapter *padapter, u8 from_timer, u8 bBusyTraffic)
{		
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	u8	bEnterPS = _FALSE;

	/* check traffic for  powersaving. */
	if (((pmlmepriv->LinkDetectInfo.NumRxUnicastOkInPeriod + pmlmepriv->LinkDetectInfo.NumTxOkInPeriod) > 8) ||
		#ifdef CONFIG_LPS_SLOW_TRANSITION
		(pmlmepriv->LinkDetectInfo.NumRxUnicastOkInPeriod > 2)
		#else /* CONFIG_LPS_SLOW_TRANSITION */
		(pmlmepriv->LinkDetectInfo.NumRxUnicastOkInPeriod > 4)
		#endif /* CONFIG_LPS_SLOW_TRANSITION */
	) {
		#ifdef DBG_RX_COUNTER_DUMP
		if (padapter->dump_rx_cnt_mode & DUMP_DRV_TRX_COUNTER_DATA)
			RTW_INFO("(-)Tx = %d, Rx = %d\n", pmlmepriv->LinkDetectInfo.NumTxOkInPeriod, pmlmepriv->LinkDetectInfo.NumRxUnicastOkInPeriod);
		#endif

		bEnterPS = _FALSE;
		#ifdef CONFIG_LPS_SLOW_TRANSITION
		if (bBusyTraffic == _TRUE) {
			if (pmlmepriv->LinkDetectInfo.TrafficTransitionCount <= 4)
				pmlmepriv->LinkDetectInfo.TrafficTransitionCount = 4;

			pmlmepriv->LinkDetectInfo.TrafficTransitionCount++;

			/* RTW_INFO("Set TrafficTransitionCount to %d\n", pmlmepriv->LinkDetectInfo.TrafficTransitionCount); */

			if (pmlmepriv->LinkDetectInfo.TrafficTransitionCount > 30/*TrafficTransitionLevel*/)
				pmlmepriv->LinkDetectInfo.TrafficTransitionCount = 30;
		}
		#endif /* CONFIG_LPS_SLOW_TRANSITION */
	} else {
		#ifdef DBG_RX_COUNTER_DUMP
		if (padapter->dump_rx_cnt_mode & DUMP_DRV_TRX_COUNTER_DATA)
			RTW_INFO("(+)Tx = %d, Rx = %d\n", pmlmepriv->LinkDetectInfo.NumTxOkInPeriod, pmlmepriv->LinkDetectInfo.NumRxUnicastOkInPeriod);
		#endif

		#ifdef CONFIG_LPS_SLOW_TRANSITION
		if (pmlmepriv->LinkDetectInfo.TrafficTransitionCount >= 2)
			pmlmepriv->LinkDetectInfo.TrafficTransitionCount -= 2;
		else
			pmlmepriv->LinkDetectInfo.TrafficTransitionCount = 0;

		if (pmlmepriv->LinkDetectInfo.TrafficTransitionCount == 0)
			bEnterPS = _TRUE;
		#else /* CONFIG_LPS_SLOW_TRANSITION */
			bEnterPS = _TRUE;
		#endif /* CONFIG_LPS_SLOW_TRANSITION */
	}

	#ifdef CONFIG_DYNAMIC_DTIM
	if (pmlmepriv->LinkDetectInfo.LowPowerTransitionCount == 8)
		bEnterPS = _FALSE;

	RTW_INFO("LowPowerTransitionCount=%d\n", pmlmepriv->LinkDetectInfo.LowPowerTransitionCount);
	#endif /* CONFIG_DYNAMIC_DTIM */

	/* LeisurePS only work in infra mode. */
	if (bEnterPS) {
		if (!from_timer) {
			#ifdef CONFIG_DYNAMIC_DTIM
			if (pmlmepriv->LinkDetectInfo.LowPowerTransitionCount < 8)
				adapter_to_pwrctl(padapter)->dtim = 1;
			else
				adapter_to_pwrctl(padapter)->dtim = 3;
			#endif /* CONFIG_DYNAMIC_DTIM */
			LPS_Enter(padapter, "TRAFFIC_IDLE");
		} else {
			/* do this at caller */
			/* rtw_lps_ctrl_wk_cmd(adapter, LPS_CTRL_ENTER, 0); */
			/* rtw_hal_dm_watchdog_in_lps(padapter); */
		}

		#ifdef CONFIG_DYNAMIC_DTIM
		if (adapter_to_pwrctl(padapter)->bFwCurrentInPSMode == _TRUE)
			pmlmepriv->LinkDetectInfo.LowPowerTransitionCount++;
		#endif /* CONFIG_DYNAMIC_DTIM */
	} else {
		#ifdef CONFIG_DYNAMIC_DTIM
		if (pmlmepriv->LinkDetectInfo.LowPowerTransitionCount != 8)
			pmlmepriv->LinkDetectInfo.LowPowerTransitionCount = 0;
		else
			pmlmepriv->LinkDetectInfo.LowPowerTransitionCount++;
		#endif /* CONFIG_DYNAMIC_DTIM */

		if (!from_timer)
			LPS_Leave(padapter, "TRAFFIC_BUSY");
		else {
			#ifdef CONFIG_CONCURRENT_MODE
			#ifndef CONFIG_FW_MULTI_PORT_SUPPORT
			if (padapter->hw_port == HW_PORT0)
			#endif
			#endif
				rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_TRAFFIC_BUSY, 0);
		}
	}

	return bEnterPS;
}
#endif /* CONFIG_LPS */

/* from_timer == 1 means driver is in LPS */
u8 traffic_status_watchdog(_adapter *padapter, u8 from_timer)
{
	u8	bEnterPS = _FALSE;
	u16 BusyThresholdHigh;
	u16	BusyThresholdLow;
	u16	BusyThreshold;
	u8	bBusyTraffic = _FALSE, bTxBusyTraffic = _FALSE, bRxBusyTraffic = _FALSE;
	u8	bHigherBusyTraffic = _FALSE, bHigherBusyRxTraffic = _FALSE, bHigherBusyTxTraffic = _FALSE;

	struct mlme_priv		*pmlmepriv = &(padapter->mlmepriv);
#ifdef CONFIG_TDLS
	struct tdls_info *ptdlsinfo = &(padapter->tdlsinfo);
	struct tdls_txmgmt txmgmt;
	u8 baddr[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
#endif /* CONFIG_TDLS */
#ifdef CONFIG_TRAFFIC_PROTECT
	RT_LINK_DETECT_T *link_detect = &pmlmepriv->LinkDetectInfo;
#endif

#ifdef CONFIG_BT_COEXIST
	if (padapter->registrypriv.wifi_spec != 1) {
		BusyThresholdHigh = 25;
		BusyThresholdLow = 10;
	} else
#endif /* CONFIG_BT_COEXIST */
	{
		BusyThresholdHigh = 100;
		BusyThresholdLow = 75;
	}
	BusyThreshold = BusyThresholdHigh;


	/*  */
	/* Determine if our traffic is busy now */
	/*  */
	if ((check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
	    /*&& !MgntInitAdapterInProgress(pMgntInfo)*/) {
		/* if we raise bBusyTraffic in last watchdog, using lower threshold. */
		if (pmlmepriv->LinkDetectInfo.bBusyTraffic)
			BusyThreshold = BusyThresholdLow;

		if (pmlmepriv->LinkDetectInfo.NumRxOkInPeriod > BusyThreshold ||
		    pmlmepriv->LinkDetectInfo.NumTxOkInPeriod > BusyThreshold) {
			bBusyTraffic = _TRUE;

			if (pmlmepriv->LinkDetectInfo.NumRxOkInPeriod > pmlmepriv->LinkDetectInfo.NumTxOkInPeriod)
				bRxBusyTraffic = _TRUE;
			else
				bTxBusyTraffic = _TRUE;
		}

		/* Higher Tx/Rx data. */
		if (pmlmepriv->LinkDetectInfo.NumRxOkInPeriod > 4000 ||
		    pmlmepriv->LinkDetectInfo.NumTxOkInPeriod > 4000) {
			bHigherBusyTraffic = _TRUE;

			if (pmlmepriv->LinkDetectInfo.NumRxOkInPeriod > pmlmepriv->LinkDetectInfo.NumTxOkInPeriod)
				bHigherBusyRxTraffic = _TRUE;
			else
				bHigherBusyTxTraffic = _TRUE;
		}

#ifdef CONFIG_TRAFFIC_PROTECT
#define TX_ACTIVE_TH 10
#define RX_ACTIVE_TH 20
#define TRAFFIC_PROTECT_PERIOD_MS 4500

		if (link_detect->NumTxOkInPeriod > TX_ACTIVE_TH
		    || link_detect->NumRxUnicastOkInPeriod > RX_ACTIVE_TH) {

			RTW_INFO(FUNC_ADPT_FMT" acqiure wake_lock for %u ms(tx:%d,rx_unicast:%d)\n",
				 FUNC_ADPT_ARG(padapter),
				 TRAFFIC_PROTECT_PERIOD_MS,
				 link_detect->NumTxOkInPeriod,
				 link_detect->NumRxUnicastOkInPeriod);

			rtw_lock_traffic_suspend_timeout(TRAFFIC_PROTECT_PERIOD_MS);
		}
#endif

#ifdef CONFIG_TDLS
#ifdef CONFIG_TDLS_AUTOSETUP
		/* TDLS_WATCHDOG_PERIOD * 2sec, periodically send */
		if (hal_chk_wl_func(padapter, WL_FUNC_TDLS) == _TRUE) {
			if ((ptdlsinfo->watchdog_count % TDLS_WATCHDOG_PERIOD) == 0) {
				_rtw_memcpy(txmgmt.peer, baddr, ETH_ALEN);
				issue_tdls_dis_req(padapter, &txmgmt);
			}
			ptdlsinfo->watchdog_count++;
		}
#endif /* CONFIG_TDLS_AUTOSETUP */
#endif /* CONFIG_TDLS */

#ifdef CONFIG_SUPPORT_STATIC_SMPS
		_ssmps_chk_by_tp(padapter, from_timer);
#endif
#ifdef CONFIG_CTRL_TXSS_BY_TP
		rtw_ctrl_tx_ss_by_tp(padapter, from_timer);
#endif

#ifdef CONFIG_LPS
		if (adapter_to_pwrctl(padapter)->bLeisurePs && MLME_IS_STA(padapter)) {
			#ifdef CONFIG_LPS_CHK_BY_TP
			if (adapter_to_pwrctl(padapter)->lps_chk_by_tp)
				bEnterPS = _lps_chk_by_tp(padapter, from_timer);
			else
			#endif /*CONFIG_LPS_CHK_BY_TP*/
				bEnterPS = _lps_chk_by_pkt_cnts(padapter, from_timer, bBusyTraffic);
		}
#endif /* CONFIG_LPS */

	} else {
#ifdef CONFIG_LPS
		if (!from_timer && rtw_mi_get_assoc_if_num(padapter) == 0)
			LPS_Leave(padapter, "NON_LINKED");
#endif
	}

	session_tracker_chk_cmd(padapter, NULL);

#ifdef CONFIG_BEAMFORMING
#ifdef RTW_BEAMFORMING_VERSION_2
	rtw_bf_update_traffic(padapter);
#endif /* RTW_BEAMFORMING_VERSION_2 */
#endif /* CONFIG_BEAMFORMING */

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


/* for 11n Logo 4.2.31/4.2.32 */
static void dynamic_update_bcn_check(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	if (!padapter->registrypriv.wifi_spec)
		return;

	if (!padapter->registrypriv.ht_enable || !is_supported_ht(padapter->registrypriv.wireless_mode))
		return;

	if (!MLME_IS_AP(padapter))
		return;

	if (pmlmeext->bstart_bss) {
		/* In 10 * 2 = 20s, there are no legacy AP, update HT info  */
		static u8 count = 1;

		if (count % 10 == 0) {
			count = 1;
#ifdef CONFIG_80211N_HT
			if (_FALSE == ATOMIC_READ(&pmlmepriv->olbc)
				&& _FALSE == ATOMIC_READ(&pmlmepriv->olbc_ht)) {

				if (rtw_ht_operation_update(padapter) > 0) {
					update_beacon(padapter, _HT_CAPABILITY_IE_, NULL, _FALSE, 0);
					update_beacon(padapter, _HT_ADD_INFO_IE_, NULL, _TRUE, 0);
				}
			}
#endif /* CONFIG_80211N_HT */
		}

#ifdef CONFIG_80211N_HT
		/* In 2s, there are any legacy AP, update HT info, and then reset count  */

		if (_FALSE != ATOMIC_READ(&pmlmepriv->olbc)
			&& _FALSE != ATOMIC_READ(&pmlmepriv->olbc_ht)) {
					
			if (rtw_ht_operation_update(padapter) > 0) {
				update_beacon(padapter, _HT_CAPABILITY_IE_, NULL, _FALSE, 0);
				update_beacon(padapter, _HT_ADD_INFO_IE_, NULL, _TRUE, 0);

			}
			ATOMIC_SET(&pmlmepriv->olbc, _FALSE);
			ATOMIC_SET(&pmlmepriv->olbc_ht, _FALSE);
			count = 0;
		}
#endif /* CONFIG_80211N_HT */
		count ++;
	}
}
void rtw_iface_dynamic_chk_wk_hdl(_adapter *padapter)
{
	#ifdef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
	#ifdef CONFIG_AP_MODE
	if (MLME_IS_AP(padapter) || MLME_IS_MESH(padapter)) {
		expire_timeout_chk(padapter);
		#ifdef CONFIG_RTW_MESH
		if (MLME_IS_MESH(padapter) && MLME_IS_ASOC(padapter))
			rtw_mesh_peer_status_chk(padapter);
		#endif
	}
	#endif
	#endif /* CONFIG_ACTIVE_KEEP_ALIVE_CHECK */
	dynamic_update_bcn_check(padapter);

	linked_status_chk(padapter, 0);
	traffic_status_watchdog(padapter, 0);

	/* for debug purpose */
	_linked_info_dump(padapter);

#ifdef CONFIG_RTW_CFGVENDOR_RSSIMONITOR
        rtw_cfgvendor_rssi_monitor_evt(padapter);
#endif


}
void rtw_dynamic_chk_wk_hdl(_adapter *padapter)
{
	rtw_mi_dynamic_chk_wk_hdl(padapter);
#ifdef CONFIG_MP_INCLUDED
	if (rtw_mp_mode_check(padapter) == _FALSE)
#endif
	{
#ifdef DBG_CONFIG_ERROR_DETECT
		rtw_hal_sreset_xmit_status_check(padapter);
		rtw_hal_sreset_linked_status_check(padapter);
#endif
	}

	/* if(check_fwstate(pmlmepriv, WIFI_UNDER_LINKING|WIFI_UNDER_SURVEY)==_FALSE) */
	{
#ifdef DBG_RX_COUNTER_DUMP
		rtw_dump_rx_counters(padapter);
#endif
		dm_DynamicUsbTxAgg(padapter, 0);
	}
	rtw_hal_dm_watchdog(padapter);

	/* check_hw_pbc(padapter, pdrvextra_cmd->pbuf, pdrvextra_cmd->type); */

#ifdef CONFIG_BT_COEXIST
	/* BT-Coexist */
	rtw_btcoex_Handler(padapter);
#endif

#ifdef CONFIG_IPS_CHECK_IN_WD
	/* always call rtw_ps_processor() at last one. */
	rtw_ps_processor(padapter);
#endif

#ifdef CONFIG_MCC_MODE
	rtw_hal_mcc_sw_status_check(padapter);
#endif /* CONFIG_MCC_MODE */

	rtw_hal_periodic_tsf_update_chk(padapter);
}

#ifdef CONFIG_LPS
struct lps_ctrl_wk_parm {
	s8 lps_level;
	#ifdef CONFIG_LPS_1T1R
	s8 lps_1t1r;
	#endif
};

void lps_ctrl_wk_hdl(_adapter *padapter, u8 lps_ctrl_type, u8 *buf)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct lps_ctrl_wk_parm *parm = (struct lps_ctrl_wk_parm *)buf;
	u8	mstatus;

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE)
	    || (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE))
		return;

	switch (lps_ctrl_type) {
	case LPS_CTRL_SCAN:
		/* RTW_INFO("LPS_CTRL_SCAN\n"); */
#ifdef CONFIG_BT_COEXIST
		rtw_btcoex_ScanNotify(padapter, _TRUE);
#endif /* CONFIG_BT_COEXIST */
		if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE) {
			/* connect */
			LPS_Leave(padapter, "LPS_CTRL_SCAN");
		}
		break;
	case LPS_CTRL_JOINBSS:
		/* RTW_INFO("LPS_CTRL_JOINBSS\n"); */
		LPS_Leave(padapter, "LPS_CTRL_JOINBSS");
		break;
	case LPS_CTRL_CONNECT:
		/* RTW_INFO("LPS_CTRL_CONNECT\n"); */
		mstatus = 1;/* connect */
		/* Reset LPS Setting */
		pwrpriv->LpsIdleCount = 0;
		rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_JOINBSSRPT, (u8 *)(&mstatus));
#ifdef CONFIG_BT_COEXIST
		rtw_btcoex_MediaStatusNotify(padapter, mstatus);
#endif /* CONFIG_BT_COEXIST */
		break;
	case LPS_CTRL_DISCONNECT:
		/* RTW_INFO("LPS_CTRL_DISCONNECT\n"); */
		mstatus = 0;/* disconnect */
#ifdef CONFIG_BT_COEXIST
		rtw_btcoex_MediaStatusNotify(padapter, mstatus);
#endif /* CONFIG_BT_COEXIST */
		LPS_Leave(padapter, "LPS_CTRL_DISCONNECT");
		rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_JOINBSSRPT, (u8 *)(&mstatus));
		break;
	case LPS_CTRL_SPECIAL_PACKET:
		/* RTW_INFO("LPS_CTRL_SPECIAL_PACKET\n"); */
		rtw_set_lps_deny(padapter, LPS_DELAY_MS);
#ifdef CONFIG_BT_COEXIST
		rtw_btcoex_SpecialPacketNotify(padapter, PACKET_DHCP);
#endif /* CONFIG_BT_COEXIST */
		LPS_Leave(padapter, "LPS_CTRL_SPECIAL_PACKET");
		break;
	case LPS_CTRL_LEAVE:
		LPS_Leave(padapter, "LPS_CTRL_LEAVE");
		break;
	case LPS_CTRL_LEAVE_SET_OPTION:
		LPS_Leave(padapter, "LPS_CTRL_LEAVE_SET_OPTION");
		if (parm) {
			if (parm->lps_level >= 0)
				pwrpriv->lps_level = parm->lps_level;
			#ifdef CONFIG_LPS_1T1R
			if (parm->lps_1t1r >= 0)
				pwrpriv->lps_1t1r = parm->lps_1t1r;
			#endif
		}
		break;
	case LPS_CTRL_LEAVE_CFG80211_PWRMGMT:
		LPS_Leave(padapter, "CFG80211_PWRMGMT");
		break;
	case LPS_CTRL_TRAFFIC_BUSY:
		LPS_Leave(padapter, "LPS_CTRL_TRAFFIC_BUSY");
		break;
	case LPS_CTRL_TX_TRAFFIC_LEAVE:
		LPS_Leave(padapter, "LPS_CTRL_TX_TRAFFIC_LEAVE");
		break;
	case LPS_CTRL_RX_TRAFFIC_LEAVE:
		LPS_Leave(padapter, "LPS_CTRL_RX_TRAFFIC_LEAVE");
		break;
	case LPS_CTRL_ENTER:
		LPS_Enter(padapter, "TRAFFIC_IDLE_1");
		break;
	default:
		break;
	}

}

static u8 _rtw_lps_ctrl_wk_cmd(_adapter *adapter, u8 lps_ctrl_type, s8 lps_level, s8 lps_1t1r, u8 flags)
{
	struct cmd_obj *cmdobj;
	struct drvextra_cmd_parm *parm;
	struct lps_ctrl_wk_parm *wk_parm = NULL;
	struct cmd_priv	*pcmdpriv = &adapter->cmdpriv;
	struct submit_ctx sctx;
	u8	res = _SUCCESS;

	if (lps_ctrl_type == LPS_CTRL_LEAVE_SET_OPTION) {
		wk_parm = rtw_zmalloc(sizeof(*wk_parm));
		if (wk_parm == NULL) {
			res = _FAIL;
			goto exit;
		}
		wk_parm->lps_level = lps_level;
		#ifdef CONFIG_LPS_1T1R
		wk_parm->lps_1t1r = lps_1t1r;
		#endif
	}

	if (flags & RTW_CMDF_DIRECTLY) {
		/* no need to enqueue, do the cmd hdl directly */
		lps_ctrl_wk_hdl(adapter, lps_ctrl_type, (u8 *)wk_parm);
		if (wk_parm)
			rtw_mfree(wk_parm, sizeof(*wk_parm));
	} else {
		/* need enqueue, prepare cmd_obj and enqueue */
		parm = rtw_zmalloc(sizeof(*parm));
		if (parm == NULL) {
			if (wk_parm)
				rtw_mfree(wk_parm, sizeof(*wk_parm));
			res = _FAIL;
			goto exit;
		}

		parm->ec_id = LPS_CTRL_WK_CID;
		parm->type = lps_ctrl_type;
		parm->size = wk_parm ? sizeof(*wk_parm) : 0;
		parm->pbuf = (u8 *)wk_parm;

		cmdobj = rtw_zmalloc(sizeof(*cmdobj));
		if (cmdobj == NULL) {
			rtw_mfree(parm, sizeof(*parm));
			if (wk_parm)
				rtw_mfree(wk_parm, sizeof(*wk_parm));
			res = _FAIL;
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(cmdobj, parm, CMD_SET_DRV_EXTRA);

		if (flags & RTW_CMDF_WAIT_ACK) {
			cmdobj->sctx = &sctx;
			rtw_sctx_init(&sctx, 2000);
		}

		res = rtw_enqueue_cmd(pcmdpriv, cmdobj);

		if (res == _SUCCESS && (flags & RTW_CMDF_WAIT_ACK)) {
			rtw_sctx_wait(&sctx, __func__);
			_enter_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
			if (sctx.status == RTW_SCTX_SUBMITTED)
				cmdobj->sctx = NULL;
			_exit_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
			if (sctx.status != RTW_SCTX_DONE_SUCCESS)
				res = _FAIL;
		}
	}

exit:
	return res;
}

u8 rtw_lps_ctrl_wk_cmd(_adapter *adapter, u8 lps_ctrl_type, u8 flags)
{
	return _rtw_lps_ctrl_wk_cmd(adapter, lps_ctrl_type, -1, -1, flags);
}

u8 rtw_lps_ctrl_leave_set_level_cmd(_adapter *adapter, u8 lps_level, u8 flags)
{
	return _rtw_lps_ctrl_wk_cmd(adapter, LPS_CTRL_LEAVE_SET_OPTION, lps_level, -1, flags);
}

#ifdef CONFIG_LPS_1T1R
u8 rtw_lps_ctrl_leave_set_1t1r_cmd(_adapter *adapter, u8 lps_1t1r, u8 flags)
{
	return _rtw_lps_ctrl_wk_cmd(adapter, LPS_CTRL_LEAVE_SET_OPTION, -1, lps_1t1r, flags);
}
#endif

void rtw_dm_in_lps_hdl(_adapter *padapter)
{
	rtw_hal_set_hwreg(padapter, HW_VAR_DM_IN_LPS_LCLK, NULL);
}

u8 rtw_dm_in_lps_wk_cmd(_adapter *padapter)
{
	struct cmd_obj	*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;


	ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = DM_IN_LPS_WK_CID;
	pdrvextra_cmd_parm->type = 0;
	pdrvextra_cmd_parm->size = 0;
	pdrvextra_cmd_parm->pbuf = NULL;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;

}

void rtw_lps_change_dtim_hdl(_adapter *padapter, u8 dtim)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);

	if (dtim <= 0 || dtim > 16)
		return;

#ifdef CONFIG_BT_COEXIST
	if (rtw_btcoex_IsBtControlLps(padapter) == _TRUE)
		return;
#endif

#ifdef CONFIG_LPS_LCLK
	_enter_pwrlock(&pwrpriv->lock);
#endif

	if (pwrpriv->dtim != dtim) {
		RTW_INFO("change DTIM from %d to %d, bFwCurrentInPSMode=%d, ps_mode=%d\n", pwrpriv->dtim, dtim,
			 pwrpriv->bFwCurrentInPSMode, pwrpriv->pwr_mode);

		pwrpriv->dtim = dtim;
	}

	if ((pwrpriv->bFwCurrentInPSMode == _TRUE) && (pwrpriv->pwr_mode > PS_MODE_ACTIVE)) {
		u8 ps_mode = pwrpriv->pwr_mode;

		/* RTW_INFO("change DTIM from %d to %d, ps_mode=%d\n", pwrpriv->dtim, dtim, ps_mode); */

		rtw_exec_lps(padapter, ps_mode);
	}

#ifdef CONFIG_LPS_LCLK
	_exit_pwrlock(&pwrpriv->lock);
#endif

}

#endif

u8 rtw_lps_change_dtim_cmd(_adapter *padapter, u8 dtim)
{
	struct cmd_obj	*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;
	/*
	#ifdef CONFIG_CONCURRENT_MODE
		if (padapter->hw_port != HW_PORT0)
			return res;
	#endif
	*/
	{
		ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
		if (ph2c == NULL) {
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
		if (pdrvextra_cmd_parm == NULL) {
			rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm->ec_id = LPS_CHANGE_DTIM_CID;
		pdrvextra_cmd_parm->type = dtim;
		pdrvextra_cmd_parm->size = 0;
		pdrvextra_cmd_parm->pbuf = NULL;

		init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);

		res = rtw_enqueue_cmd(pcmdpriv, ph2c);
	}

exit:

	return res;

}

#if (RATE_ADAPTIVE_SUPPORT == 1)
void rpt_timer_setting_wk_hdl(_adapter *padapter, u16 minRptTime)
{
	rtw_hal_set_hwreg(padapter, HW_VAR_RPT_TIMER_SETTING, (u8 *)(&minRptTime));
}

u8 rtw_rpt_timer_cfg_cmd(_adapter *padapter, u16 minRptTime)
{
	struct cmd_obj		*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;

	u8	res = _SUCCESS;

	ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = RTP_TIMER_CFG_WK_CID;
	pdrvextra_cmd_parm->type = minRptTime;
	pdrvextra_cmd_parm->size = 0;
	pdrvextra_cmd_parm->pbuf = NULL;
	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);
	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
exit:


	return res;

}

#endif

#ifdef CONFIG_ANTENNA_DIVERSITY
void antenna_select_wk_hdl(_adapter *padapter, u8 antenna)
{
	rtw_hal_set_odm_var(padapter, HAL_ODM_ANTDIV_SELECT, &antenna, _TRUE);
}

u8 rtw_antenna_select_cmd(_adapter *padapter, u8 antenna, u8 enqueue)
{
	struct cmd_obj		*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	u8	bSupportAntDiv = _FALSE;
	u8	res = _SUCCESS;
	int	i;

	rtw_hal_get_def_var(padapter, HAL_DEF_IS_SUPPORT_ANT_DIV, &(bSupportAntDiv));
	if (_FALSE == bSupportAntDiv)
		return _FAIL;

	for (i = 0; i < dvobj->iface_nums; i++) {
		if (rtw_linked_check(dvobj->padapters[i]))
			return _FAIL;
	}

	if (_TRUE == enqueue) {
		ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
		if (ph2c == NULL) {
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
		if (pdrvextra_cmd_parm == NULL) {
			rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm->ec_id = ANT_SELECT_WK_CID;
		pdrvextra_cmd_parm->type = antenna;
		pdrvextra_cmd_parm->size = 0;
		pdrvextra_cmd_parm->pbuf = NULL;
		init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);

		res = rtw_enqueue_cmd(pcmdpriv, ph2c);
	} else
		antenna_select_wk_hdl(padapter, antenna);
exit:


	return res;

}
#endif

void rtw_dm_ra_mask_hdl(_adapter *padapter, struct sta_info *psta)
{
	if (psta)
		set_sta_rate(padapter, psta);
}

u8 rtw_dm_ra_mask_wk_cmd(_adapter *padapter, u8 *psta)
{
	struct cmd_obj	*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;


	ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = DM_RA_MSK_WK_CID;
	pdrvextra_cmd_parm->type = 0;
	pdrvextra_cmd_parm->size = 0;
	pdrvextra_cmd_parm->pbuf = psta;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;

}

void power_saving_wk_hdl(_adapter *padapter)
{
	rtw_ps_processor(padapter);
}

/* add for CONFIG_IEEE80211W, none 11w can use it */
void reset_securitypriv_hdl(_adapter *padapter)
{
	rtw_reset_securitypriv(padapter);
}

#ifdef CONFIG_P2P
u8 p2p_protocol_wk_cmd(_adapter *padapter, int intCmdType)
{
	struct cmd_obj	*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;


	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return res;

	ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = P2P_PROTO_WK_CID;
	pdrvextra_cmd_parm->type = intCmdType;	/*	As the command tppe. */
	pdrvextra_cmd_parm->size = 0;
	pdrvextra_cmd_parm->pbuf = NULL;		/*	Must be NULL here */

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:


	return res;

}

#ifdef CONFIG_IOCTL_CFG80211
static u8 _p2p_roch_cmd(_adapter *adapter
	, u64 cookie, struct wireless_dev *wdev
	, struct ieee80211_channel *ch, enum nl80211_channel_type ch_type
	, unsigned int duration
	, u8 flags
)
{
	struct cmd_obj *cmdobj;
	struct drvextra_cmd_parm *parm;
	struct p2p_roch_parm *roch_parm;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	struct submit_ctx sctx;
	u8 cancel = duration ? 0 : 1;
	u8	res = _SUCCESS;

	roch_parm = (struct p2p_roch_parm *)rtw_zmalloc(sizeof(struct p2p_roch_parm));
	if (roch_parm == NULL) {
		res = _FAIL;
		goto exit;
	}

	roch_parm->cookie = cookie;
	roch_parm->wdev = wdev;
	if (!cancel) {
		_rtw_memcpy(&roch_parm->ch, ch, sizeof(struct ieee80211_channel));
		roch_parm->ch_type = ch_type;
		roch_parm->duration = duration;
	}

	if (flags & RTW_CMDF_DIRECTLY) {
		/* no need to enqueue, do the cmd hdl directly and free cmd parameter */
		if (H2C_SUCCESS != p2p_protocol_wk_hdl(adapter, cancel ? P2P_CANCEL_RO_CH_WK : P2P_RO_CH_WK, (u8 *)roch_parm))
			res = _FAIL;
		rtw_mfree((u8 *)roch_parm, sizeof(*roch_parm));
	} else {
		/* need enqueue, prepare cmd_obj and enqueue */
		parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
		if (parm == NULL) {
			rtw_mfree((u8 *)roch_parm, sizeof(*roch_parm));
			res = _FAIL;
			goto exit;
		}

		parm->ec_id = P2P_PROTO_WK_CID;
		parm->type = cancel ? P2P_CANCEL_RO_CH_WK : P2P_RO_CH_WK;
		parm->size = sizeof(*roch_parm);
		parm->pbuf = (u8 *)roch_parm;

		cmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(*cmdobj));
		if (cmdobj == NULL) {
			res = _FAIL;
			rtw_mfree((u8 *)roch_parm, sizeof(*roch_parm));
			rtw_mfree((u8 *)parm, sizeof(*parm));
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(cmdobj, parm, CMD_SET_DRV_EXTRA);

		if (flags & RTW_CMDF_WAIT_ACK) {
			cmdobj->sctx = &sctx;
			rtw_sctx_init(&sctx, 10 * 1000);
		}

		res = rtw_enqueue_cmd(pcmdpriv, cmdobj);

		if (res == _SUCCESS && (flags & RTW_CMDF_WAIT_ACK)) {
			rtw_sctx_wait(&sctx, __func__);
			_enter_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
			if (sctx.status == RTW_SCTX_SUBMITTED)
				cmdobj->sctx = NULL;
			_exit_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
			if (sctx.status != RTW_SCTX_DONE_SUCCESS)
				res = _FAIL;
		}
	}

exit:
	return res;
}

inline u8 p2p_roch_cmd(_adapter *adapter
	, u64 cookie, struct wireless_dev *wdev
	, struct ieee80211_channel *ch, enum nl80211_channel_type ch_type
	, unsigned int duration
	, u8 flags
)
{
	return _p2p_roch_cmd(adapter, cookie, wdev, ch, ch_type, duration, flags);
}

inline u8 p2p_cancel_roch_cmd(_adapter *adapter, u64 cookie, struct wireless_dev *wdev, u8 flags)
{
	return _p2p_roch_cmd(adapter, cookie, wdev, NULL, 0, 0, flags);
}

#endif /* CONFIG_IOCTL_CFG80211 */
#endif /* CONFIG_P2P */

#ifdef CONFIG_IOCTL_CFG80211 
inline u8 rtw_mgnt_tx_cmd(_adapter *adapter, u8 tx_ch, u8 no_cck, const u8 *buf, size_t len, int wait_ack, u8 flags)
{
	struct cmd_obj *cmdobj;
	struct drvextra_cmd_parm *parm;
	struct mgnt_tx_parm *mgnt_parm;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	struct submit_ctx sctx;
	u8	res = _SUCCESS;

	mgnt_parm = (struct mgnt_tx_parm *)rtw_zmalloc(sizeof(struct mgnt_tx_parm));
	if (mgnt_parm == NULL) {
		res = _FAIL;
			goto exit;
	}

	mgnt_parm->tx_ch = tx_ch;
	mgnt_parm->no_cck = no_cck;
	mgnt_parm->buf = buf;
	mgnt_parm->len = len;
	mgnt_parm->wait_ack = wait_ack;

	if (flags & RTW_CMDF_DIRECTLY) {
		/* no need to enqueue, do the cmd hdl directly and free cmd parameter */
		if (H2C_SUCCESS != rtw_mgnt_tx_handler(adapter, (u8 *)mgnt_parm))
			res = _FAIL;
		rtw_mfree((u8 *)mgnt_parm, sizeof(*mgnt_parm));
	} else {
		/* need enqueue, prepare cmd_obj and enqueue */
		parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
		if (parm == NULL) {
			rtw_mfree((u8 *)mgnt_parm, sizeof(*mgnt_parm));
			res = _FAIL;
			goto exit;
		}

		parm->ec_id = MGNT_TX_WK_CID;
		parm->type = 0;
		parm->size = sizeof(*mgnt_parm);
		parm->pbuf = (u8 *)mgnt_parm;

		cmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(*cmdobj));
		if (cmdobj == NULL) {
			res = _FAIL;
			rtw_mfree((u8 *)mgnt_parm, sizeof(*mgnt_parm));
			rtw_mfree((u8 *)parm, sizeof(*parm));
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(cmdobj, parm, CMD_SET_DRV_EXTRA);

		if (flags & RTW_CMDF_WAIT_ACK) {
			cmdobj->sctx = &sctx;
			rtw_sctx_init(&sctx, 10 * 1000);
		}

		res = rtw_enqueue_cmd(pcmdpriv, cmdobj);

		if (res == _SUCCESS && (flags & RTW_CMDF_WAIT_ACK)) {
			rtw_sctx_wait(&sctx, __func__);
			_enter_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
			if (sctx.status == RTW_SCTX_SUBMITTED)
				cmdobj->sctx = NULL;
			_exit_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
			if (sctx.status != RTW_SCTX_DONE_SUCCESS)
				res = _FAIL;
		}
	}

exit:
	return res;
}
#endif

u8 rtw_ps_cmd(_adapter *padapter)
{
	struct cmd_obj		*ppscmd;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;

	u8	res = _SUCCESS;

#ifdef CONFIG_CONCURRENT_MODE
	if (!is_primary_adapter(padapter))
		goto exit;
#endif

	ppscmd = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (ppscmd == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		rtw_mfree((unsigned char *)ppscmd, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = POWER_SAVING_CTRL_WK_CID;
	pdrvextra_cmd_parm->type = 0;
	pdrvextra_cmd_parm->size = 0;
	pdrvextra_cmd_parm->pbuf = NULL;
	init_h2fwcmd_w_parm_no_rsp(ppscmd, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);

	res = rtw_enqueue_cmd(pcmdpriv, ppscmd);

exit:


	return res;

}

#if CONFIG_DFS
void rtw_dfs_ch_switch_hdl(struct dvobj_priv *dvobj)
{
	struct rf_ctl_t *rfctl = dvobj_to_rfctl(dvobj);
	_adapter *pri_adapter = dvobj_get_primary_adapter(dvobj);
	u8 ifbmp_m = rtw_mi_get_ap_mesh_ifbmp(pri_adapter);
	u8 ifbmp_s = rtw_mi_get_ld_sta_ifbmp(pri_adapter);
	s16 req_ch;

	rtw_hal_macid_sleep_all_used(pri_adapter);

	if (rtw_chset_search_ch(rfctl->channel_set, rfctl->csa_ch) >= 0
		&& !rtw_chset_is_ch_non_ocp(rfctl->channel_set, rfctl->csa_ch)
	) {
		/* CSA channel available and valid */
		req_ch = rfctl->csa_ch;
		RTW_INFO("%s valid CSA ch%u\n", __func__, rfctl->csa_ch);
	} else if (ifbmp_m) {
		/* no available or valid CSA channel, having AP/MESH ifaces */
		req_ch = REQ_CH_NONE;
		RTW_INFO("%s ch sel by AP/MESH ifaces\n", __func__);
	} else {
		/* no available or valid CSA channel and no AP/MESH ifaces */
		if (!IsSupported24G(dvobj_to_regsty(dvobj)->wireless_mode)
			#ifdef CONFIG_DFS_MASTER
			|| rfctl->radar_detected
			#endif
		)
			req_ch = 36;
		else
			req_ch = 1;
		RTW_INFO("%s switch to ch%d\n", __func__, req_ch);
	}

	/*  issue deauth for all asoc STA ifaces */
	if (ifbmp_s) {
		_adapter *iface;
		int i;

		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (!iface || !(ifbmp_s & BIT(iface->iface_id)))
				continue;
			set_fwstate(&iface->mlmepriv, WIFI_OP_CH_SWITCHING);

			/* TODO: true op ch switching */
			issue_deauth(iface, get_bssid(&iface->mlmepriv), WLAN_REASON_DEAUTH_LEAVING);
		}
	}

#ifdef CONFIG_AP_MODE
	if (ifbmp_m) {
		/* trigger channel selection without consideraton of asoc STA ifaces */
		rtw_change_bss_chbw_cmd(dvobj_get_primary_adapter(dvobj), RTW_CMDF_DIRECTLY
			, ifbmp_m, ifbmp_s, req_ch, REQ_BW_ORI, REQ_OFFSET_NONE);
	} else
#endif
	{
		/* no AP/MESH iface, switch DFS status and channel directly */
		rtw_warn_on(req_ch <= 0);
		#ifdef CONFIG_DFS_MASTER
		rtw_dfs_rd_en_decision(pri_adapter, MLME_OPCH_SWITCH, ifbmp_s);
		#endif
		set_channel_bwmode(pri_adapter, req_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
	}

	/* make asoc STA ifaces disconnect */
	/* TODO: true op ch switching */
	if (ifbmp_s) {
		_adapter *iface;
		int i;
	
		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (!iface || !(ifbmp_s & BIT(iface->iface_id)))
				continue;
			rtw_disassoc_cmd(iface, 0, RTW_CMDF_DIRECTLY);
			rtw_indicate_disconnect(iface, 0, _FALSE);
			rtw_free_assoc_resources(iface, _TRUE);
			rtw_free_network_queue(iface, _TRUE);
		}
	}

	rfctl->csa_ch = 0;

	rtw_hal_macid_wakeup_all_used(pri_adapter);
	rtw_mi_os_xmit_schedule(pri_adapter);
}
#endif /* CONFIG_DFS */

#ifdef CONFIG_AP_MODE

static void rtw_chk_hi_queue_hdl(_adapter *padapter)
{
	struct sta_info *psta_bmc;
	struct sta_priv *pstapriv = &padapter->stapriv;
	systime start = rtw_get_current_time();
	u8 empty = _FALSE;

	psta_bmc = rtw_get_bcmc_stainfo(padapter);
	if (!psta_bmc)
		return;

	rtw_hal_get_hwreg(padapter, HW_VAR_CHK_HI_QUEUE_EMPTY, &empty);

	while (_FALSE == empty && rtw_get_passing_time_ms(start) < rtw_get_wait_hiq_empty_ms()) {
		rtw_msleep_os(100);
		rtw_hal_get_hwreg(padapter, HW_VAR_CHK_HI_QUEUE_EMPTY, &empty);
	}

	if (psta_bmc->sleepq_len == 0) {
		if (empty == _SUCCESS) {
			bool update_tim = _FALSE;

			if (rtw_tim_map_is_set(padapter, pstapriv->tim_bitmap, 0))
				update_tim = _TRUE;

			rtw_tim_map_clear(padapter, pstapriv->tim_bitmap, 0);
			rtw_tim_map_clear(padapter, pstapriv->sta_dz_bitmap, 0);

			if (update_tim == _TRUE)
				_update_beacon(padapter, _TIM_IE_, NULL, _TRUE, 0,"bmc sleepq and HIQ empty");
		} else /* re check again */
			rtw_chk_hi_queue_cmd(padapter);

	}

}

u8 rtw_chk_hi_queue_cmd(_adapter *padapter)
{
	struct cmd_obj	*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;

	ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = CHECK_HIQ_WK_CID;
	pdrvextra_cmd_parm->type = 0;
	pdrvextra_cmd_parm->size = 0;
	pdrvextra_cmd_parm->pbuf = NULL;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;

}

#ifdef CONFIG_DFS_MASTER
u8 rtw_dfs_rd_hdl(_adapter *adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);

	if (!rfctl->radar_detect_enabled)
		goto exit;

	if (dvobj->oper_channel != rfctl->radar_detect_ch
		|| rtw_get_passing_time_ms(rtw_get_on_oper_ch_time(adapter)) < 300
	) {
		/* offchannel, bypass radar detect */
		goto cac_status_chk;
	}

	if (IS_CH_WAITING(rfctl) && !IS_UNDER_CAC(rfctl)) {
		/* non_ocp, bypass radar detect */
		goto cac_status_chk;
	}

	if (!rfctl->dbg_dfs_fake_radar_detect_cnt
		&& rtw_odm_radar_detect(adapter) != _TRUE)
		goto cac_status_chk;

	if (!rfctl->dbg_dfs_fake_radar_detect_cnt
		&& rfctl->dbg_dfs_radar_detect_trigger_non
	) {
		/* radar detect debug mode, trigger no mlme flow */
		RTW_INFO("%s radar detected on test mode, trigger no mlme flow\n", __func__);
		goto cac_status_chk;
	}

	if (rfctl->dbg_dfs_fake_radar_detect_cnt != 0) {
		RTW_INFO("%s fake radar detected, cnt:%d\n", __func__
			, rfctl->dbg_dfs_fake_radar_detect_cnt);
		rfctl->dbg_dfs_fake_radar_detect_cnt--;
	} else
		RTW_INFO("%s radar detected\n", __func__);

	rfctl->radar_detected = 1;

	rtw_chset_update_non_ocp(rfctl->channel_set
		, rfctl->radar_detect_ch, rfctl->radar_detect_bw, rfctl->radar_detect_offset);

	rtw_dfs_ch_switch_hdl(dvobj);

	if (rfctl->radar_detect_enabled)
		goto set_timer;
	goto exit;

cac_status_chk:

	if (!IS_CAC_STOPPED(rfctl)
		&& ((IS_UNDER_CAC(rfctl) && rfctl->cac_force_stop)
			|| !IS_CH_WAITING(rfctl)
			)
	) {
		u8 pause = 0x00;

		rtw_hal_set_hwreg(adapter, HW_VAR_TXPAUSE, &pause);
		rfctl->cac_start_time = rfctl->cac_end_time = RTW_CAC_STOPPED;

		if (rtw_mi_check_fwstate(adapter, WIFI_UNDER_LINKING|WIFI_UNDER_SURVEY) == _FALSE) {
			u8 doiqk = _TRUE;
			u8 u_ch, u_bw, u_offset;

			rtw_hal_set_hwreg(adapter , HW_VAR_DO_IQK , &doiqk);

			if (rtw_mi_get_ch_setting_union(adapter, &u_ch, &u_bw, &u_offset))
				set_channel_bwmode(adapter, u_ch, u_offset, u_bw);
			else
				rtw_warn_on(1);

			doiqk = _FALSE;
			rtw_hal_set_hwreg(adapter , HW_VAR_DO_IQK , &doiqk);

			ResumeTxBeacon(adapter);
			rtw_mi_tx_beacon_hdl(adapter);
		}
	}

set_timer:
	_set_timer(&rfctl->radar_detect_timer
		, rtw_odm_radar_detect_polling_int_ms(dvobj));

exit:
	return H2C_SUCCESS;
}

u8 rtw_dfs_rd_cmd(_adapter *adapter, bool enqueue)
{
	struct cmd_obj *cmdobj;
	struct drvextra_cmd_parm *parm;
	struct cmd_priv *cmdpriv = &adapter->cmdpriv;
	u8 res = _FAIL;

	if (enqueue) {
		cmdobj = rtw_zmalloc(sizeof(struct cmd_obj));
		if (cmdobj == NULL)
			goto exit;

		parm = rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
		if (parm == NULL) {
			rtw_mfree(cmdobj, sizeof(struct cmd_obj));
			goto exit;
		}

		parm->ec_id = DFS_RADAR_DETECT_WK_CID;
		parm->type = 0;
		parm->size = 0;
		parm->pbuf = NULL;

		init_h2fwcmd_w_parm_no_rsp(cmdobj, parm, CMD_SET_DRV_EXTRA);
		res = rtw_enqueue_cmd(cmdpriv, cmdobj);
	} else {
		rtw_dfs_rd_hdl(adapter);
		res = _SUCCESS;
	}

exit:
	return res;
}

void rtw_dfs_rd_timer_hdl(void *ctx)
{
	struct rf_ctl_t *rfctl = (struct rf_ctl_t *)ctx;
	struct dvobj_priv *dvobj = rfctl_to_dvobj(rfctl);

	rtw_dfs_rd_cmd(dvobj_get_primary_adapter(dvobj), _TRUE);
}

static void rtw_dfs_rd_enable(struct rf_ctl_t *rfctl, u8 ch, u8 bw, u8 offset, bool bypass_cac)
{
	struct dvobj_priv *dvobj = rfctl_to_dvobj(rfctl);
	_adapter *adapter = dvobj_get_primary_adapter(dvobj);

	RTW_INFO("%s on %u,%u,%u\n", __func__, ch, bw, offset);

	if (bypass_cac)
		rfctl->cac_start_time = rfctl->cac_end_time = RTW_CAC_STOPPED;
	else if (rtw_is_cac_reset_needed(rfctl, ch, bw, offset) == _TRUE)
		rtw_reset_cac(rfctl, ch, bw, offset);

	rfctl->radar_detect_by_others = _FALSE;
	rfctl->radar_detect_ch = ch;
	rfctl->radar_detect_bw = bw;
	rfctl->radar_detect_offset = offset;

	rfctl->radar_detected = 0;

	if (IS_CH_WAITING(rfctl))
		StopTxBeacon(adapter);

	if (!rfctl->radar_detect_enabled) {
		RTW_INFO("%s set radar_detect_enabled\n", __func__);
		rfctl->radar_detect_enabled = 1;
		#ifdef CONFIG_LPS
		LPS_Leave(adapter, "RADAR_DETECT_EN");
		#endif
		_set_timer(&rfctl->radar_detect_timer
			, rtw_odm_radar_detect_polling_int_ms(dvobj));

		if (rtw_rfctl_overlap_radar_detect_ch(rfctl)) {
			if (IS_CH_WAITING(rfctl)) {
				u8 pause = 0xFF;

				rtw_hal_set_hwreg(adapter, HW_VAR_TXPAUSE, &pause);
			}
			rtw_odm_radar_detect_enable(adapter);
		}
	}
}

static void rtw_dfs_rd_disable(struct rf_ctl_t *rfctl, u8 ch, u8 bw, u8 offset, bool by_others)
{
	_adapter *adapter = dvobj_get_primary_adapter(rfctl_to_dvobj(rfctl));

	rfctl->radar_detect_by_others = by_others;

	if (rfctl->radar_detect_enabled) {
		bool overlap_radar_detect_ch = rtw_rfctl_overlap_radar_detect_ch(rfctl);

		RTW_INFO("%s clear radar_detect_enabled\n", __func__);

		rfctl->radar_detect_enabled = 0;
		rfctl->radar_detected = 0;
		rfctl->radar_detect_ch = 0;
		rfctl->radar_detect_bw = 0;
		rfctl->radar_detect_offset = 0;
		rfctl->cac_start_time = rfctl->cac_end_time = RTW_CAC_STOPPED;
		_cancel_timer_ex(&rfctl->radar_detect_timer);

		if (rtw_mi_check_fwstate(adapter, WIFI_UNDER_LINKING|WIFI_UNDER_SURVEY) == _FALSE) {
			ResumeTxBeacon(adapter);
			rtw_mi_tx_beacon_hdl(adapter);
		}

		if (overlap_radar_detect_ch) {
			u8 pause = 0x00;

			rtw_hal_set_hwreg(adapter, HW_VAR_TXPAUSE, &pause);
			rtw_odm_radar_detect_disable(adapter);
		}
	}

	if (by_others) {
		rfctl->radar_detect_ch = ch;
		rfctl->radar_detect_bw = bw;
		rfctl->radar_detect_offset = offset;
	}
}

void rtw_dfs_rd_en_decision(_adapter *adapter, u8 mlme_act, u8 excl_ifbmp)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	struct mlme_ext_priv *mlmeext = &adapter->mlmeextpriv;
	struct mi_state mstate;
	u8 ifbmp;
	u8 u_ch, u_bw, u_offset;
	bool ld_sta_in_dfs = _FALSE;
	bool sync_ch = _FALSE; /* _FALSE: asign channel directly */
	bool needed = _FALSE;

	if (mlme_act == MLME_OPCH_SWITCH
		|| mlme_act == MLME_ACTION_NONE
	) {
		ifbmp = ~excl_ifbmp;
		rtw_mi_status_by_ifbmp(dvobj, ifbmp, &mstate);
		rtw_mi_get_ch_setting_union_by_ifbmp(dvobj, ifbmp, &u_ch, &u_bw, &u_offset);
	} else {
		ifbmp = ~excl_ifbmp & ~BIT(adapter->iface_id);
		rtw_mi_status_by_ifbmp(dvobj, ifbmp, &mstate);
		rtw_mi_get_ch_setting_union_by_ifbmp(dvobj, ifbmp, &u_ch, &u_bw, &u_offset);
		if (u_ch != 0)
			sync_ch = _TRUE;

		switch (mlme_act) {
		case MLME_STA_CONNECTING:
			MSTATE_STA_LG_NUM(&mstate)++;
			break;
		case MLME_STA_CONNECTED:
			MSTATE_STA_LD_NUM(&mstate)++;
			break;
		case MLME_STA_DISCONNECTED:
			break;
#ifdef CONFIG_AP_MODE
		case MLME_AP_STARTED:
			MSTATE_AP_NUM(&mstate)++;
			break;
		case MLME_AP_STOPPED:
			break;
#endif
#ifdef CONFIG_RTW_MESH
		case MLME_MESH_STARTED:
			MSTATE_MESH_NUM(&mstate)++;
			break;
		case MLME_MESH_STOPPED:
			break;
#endif
		default:
			rtw_warn_on(1);
			break;
		}

		if (sync_ch == _TRUE) {
			if (!MLME_IS_OPCH_SW(adapter)) {
				if (!rtw_is_chbw_grouped(mlmeext->cur_channel, mlmeext->cur_bwmode, mlmeext->cur_ch_offset, u_ch, u_bw, u_offset)) {
					RTW_INFO(FUNC_ADPT_FMT" can't sync %u,%u,%u with %u,%u,%u\n", FUNC_ADPT_ARG(adapter)
						, mlmeext->cur_channel, mlmeext->cur_bwmode, mlmeext->cur_ch_offset, u_ch, u_bw, u_offset);
					goto apply;
				}

				rtw_sync_chbw(&mlmeext->cur_channel, &mlmeext->cur_bwmode, &mlmeext->cur_ch_offset
					, &u_ch, &u_bw, &u_offset);
			}
		} else {
			u_ch = mlmeext->cur_channel;
			u_bw = mlmeext->cur_bwmode;
			u_offset = mlmeext->cur_ch_offset;
		}
	}

	if (MSTATE_STA_LG_NUM(&mstate) > 0) {
		/* STA mode is linking */
		goto apply;
	}

	if (MSTATE_STA_LD_NUM(&mstate) > 0) {
		if (rtw_chset_is_dfs_chbw(rfctl->channel_set, u_ch, u_bw, u_offset)) {
			/*
			* if operate as slave w/o radar detect,
			* rely on AP on which STA mode connects
			*/
			if (IS_DFS_SLAVE_WITH_RD(rfctl) && !rtw_rfctl_dfs_domain_unknown(rfctl))
				needed = _TRUE;
			ld_sta_in_dfs = _TRUE;
		}
		goto apply;
	}

	if (!MSTATE_AP_NUM(&mstate) && !MSTATE_MESH_NUM(&mstate)) {
		/* No working AP/Mesh mode */
		goto apply;
	}

	if (rtw_chset_is_dfs_chbw(rfctl->channel_set, u_ch, u_bw, u_offset))
		needed = _TRUE;

apply:

	RTW_INFO(FUNC_ADPT_FMT" needed:%d, mlme_act:%u, excl_ifbmp:0x%02x\n"
		, FUNC_ADPT_ARG(adapter), needed, mlme_act, excl_ifbmp);
	RTW_INFO(FUNC_ADPT_FMT" ld_sta_num:%u, lg_sta_num:%u, ap_num:%u, mesh_num:%u, %u,%u,%u\n"
		, FUNC_ADPT_ARG(adapter), MSTATE_STA_LD_NUM(&mstate), MSTATE_STA_LG_NUM(&mstate)
		, MSTATE_AP_NUM(&mstate), MSTATE_MESH_NUM(&mstate)
		, u_ch, u_bw, u_offset);

	if (needed == _TRUE)
		rtw_dfs_rd_enable(rfctl, u_ch, u_bw, u_offset, ld_sta_in_dfs);
	else
		rtw_dfs_rd_disable(rfctl, u_ch, u_bw, u_offset, ld_sta_in_dfs);
}

u8 rtw_dfs_rd_en_decision_cmd(_adapter *adapter)
{
	struct cmd_obj *cmdobj;
	struct drvextra_cmd_parm *parm;
	struct cmd_priv *cmdpriv = &adapter->cmdpriv;
	u8 res = _FAIL;

	cmdobj = rtw_zmalloc(sizeof(struct cmd_obj));
	if (cmdobj == NULL)
		goto exit;

	parm = rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (parm == NULL) {
		rtw_mfree(cmdobj, sizeof(struct cmd_obj));
		goto exit;
	}

	parm->ec_id = DFS_RADAR_DETECT_EN_DEC_WK_CID;
	parm->type = 0;
	parm->size = 0;
	parm->pbuf = NULL;

	init_h2fwcmd_w_parm_no_rsp(cmdobj, parm, CMD_SET_DRV_EXTRA);
	res = rtw_enqueue_cmd(cmdpriv, cmdobj);

exit:
	return res;
}
#endif /* CONFIG_DFS_MASTER */

#endif /* CONFIG_AP_MODE */

#ifdef CONFIG_BT_COEXIST
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

void btinfo_evt_dump(void *sel, void *buf)
{
	struct btinfo *info = (struct btinfo *)buf;

	RTW_PRINT_SEL(sel, "cid:0x%02x, len:%u\n", info->cid, info->len);

	if (info->len > 2)
		RTW_PRINT_SEL(sel, "byte2:%s%s%s%s%s%s%s%s\n"
			      , info->bConnection ? "bConnection " : ""
			      , info->bSCOeSCO ? "bSCOeSCO " : ""
			      , info->bInQPage ? "bInQPage " : ""
			      , info->bACLBusy ? "bACLBusy " : ""
			      , info->bSCOBusy ? "bSCOBusy " : ""
			      , info->bHID ? "bHID " : ""
			      , info->bA2DP ? "bA2DP " : ""
			      , info->bFTP ? "bFTP" : ""
			     );

	if (info->len > 3)
		RTW_PRINT_SEL(sel, "retry_cnt:%u\n", info->retry_cnt);

	if (info->len > 4)
		RTW_PRINT_SEL(sel, "rssi:%u\n", info->rssi);

	if (info->len > 5)
		RTW_PRINT_SEL(sel, "byte5:%s%s\n"
			      , info->eSCO_SCO ? "eSCO_SCO " : ""
			      , info->Master_Slave ? "Master_Slave " : ""
			     );
}

static void rtw_btinfo_hdl(_adapter *adapter, u8 *buf, u16 buf_len)
{
#define BTINFO_WIFI_FETCH 0x23
#define BTINFO_BT_AUTO_RPT 0x27
#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
	struct btinfo_8761ATV *info = (struct btinfo_8761ATV *)buf;
#else /* !CONFIG_BT_COEXIST_SOCKET_TRX */
	struct btinfo *info = (struct btinfo *)buf;
#endif /* CONFIG_BT_COEXIST_SOCKET_TRX */
	u8 cmd_idx;
	u8 len;

	cmd_idx = info->cid;

	if (info->len > buf_len - 2) {
		rtw_warn_on(1);
		len = buf_len - 2;
	} else
		len = info->len;

	/* #define DBG_PROC_SET_BTINFO_EVT */
#ifdef DBG_PROC_SET_BTINFO_EVT
#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
	RTW_INFO("%s: btinfo[0]=%x,btinfo[1]=%x,btinfo[2]=%x,btinfo[3]=%x btinfo[4]=%x,btinfo[5]=%x,btinfo[6]=%x,btinfo[7]=%x\n"
		, __func__, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
#else/* !CONFIG_BT_COEXIST_SOCKET_TRX */
	btinfo_evt_dump(RTW_DBGDUMP, info);
#endif /* CONFIG_BT_COEXIST_SOCKET_TRX */
#endif /* DBG_PROC_SET_BTINFO_EVT */

	/* transform BT-FW btinfo to WiFI-FW C2H format and notify */
	if (cmd_idx == BTINFO_WIFI_FETCH)
		buf[1] = 0;
	else if (cmd_idx == BTINFO_BT_AUTO_RPT)
		buf[1] = 2;
#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
	else if (0x01 == cmd_idx || 0x02 == cmd_idx)
		buf[1] = buf[0];
#endif /* CONFIG_BT_COEXIST_SOCKET_TRX */
	rtw_btcoex_BtInfoNotify(adapter , len + 1, &buf[1]);
}

u8 rtw_btinfo_cmd(_adapter *adapter, u8 *buf, u16 len)
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

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:
	return res;
}

static void rtw_btc_reduce_wl_txpwr_hdl(_adapter *adapter, u32 pwr_lvl)
{
	rtw_btcoex_set_reduced_wl_pwr_lvl(adapter, pwr_lvl);
	rtw_btcoex_do_reduce_wl_pwr_lvl(adapter);

	RTW_INFO(FUNC_ADPT_FMT ": BTC reduce WL TxPwr %d dB!\n",
		 FUNC_ADPT_ARG(adapter), pwr_lvl);
}

u8 rtw_btc_reduce_wl_txpwr_cmd(_adapter *adapter, u32 val)
{
	struct cmd_obj *pcmdobj;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	u8	res = _SUCCESS;

	pcmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (pcmdobj == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		rtw_mfree((u8 *)pcmdobj, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = BTC_REDUCE_WL_TXPWR_CID;
	pdrvextra_cmd_parm->type = val;
	pdrvextra_cmd_parm->size = 0;
	pdrvextra_cmd_parm->pbuf = NULL;

	init_h2fwcmd_w_parm_no_rsp(pcmdobj, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);

	res = rtw_enqueue_cmd(pcmdpriv, pcmdobj);

exit:
	return res;
}
#endif /* CONFIG_BT_COEXIST */

u8 rtw_test_h2c_cmd(_adapter *adapter, u8 *buf, u8 len)
{
	struct cmd_obj *pcmdobj;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	u8 *ph2c_content;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	u8	res = _SUCCESS;

	pcmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (pcmdobj == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		rtw_mfree((u8 *)pcmdobj, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	ph2c_content = rtw_zmalloc(len);
	if (ph2c_content == NULL) {
		rtw_mfree((u8 *)pcmdobj, sizeof(struct cmd_obj));
		rtw_mfree((u8 *)pdrvextra_cmd_parm, sizeof(struct drvextra_cmd_parm));
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = TEST_H2C_CID;
	pdrvextra_cmd_parm->type = 0;
	pdrvextra_cmd_parm->size = len;
	pdrvextra_cmd_parm->pbuf = ph2c_content;

	_rtw_memcpy(ph2c_content, buf, len);

	init_h2fwcmd_w_parm_no_rsp(pcmdobj, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);

	res = rtw_enqueue_cmd(pcmdpriv, pcmdobj);

exit:
	return res;
}

#ifdef CONFIG_MP_INCLUDED
static s32 rtw_mp_cmd_hdl(_adapter *padapter, u8 mp_cmd_id)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	int ret = H2C_SUCCESS;
	uint status = _SUCCESS;

	if (mp_cmd_id == MP_START) {
		if (padapter->registrypriv.mp_mode == 0) {
			rtw_intf_stop(padapter);
			rtw_hal_deinit(padapter);
			padapter->registrypriv.mp_mode = 1;
#ifdef CONFIG_BT_COEXIST
		padapter->mppriv.CureFuseBTCoex = pHalData->EEPROMBluetoothCoexist;
		pHalData->EEPROMBluetoothCoexist = _FALSE;
#endif
#ifdef CONFIG_RF_POWER_TRIM
			if (!IS_HARDWARE_TYPE_8814A(padapter) && !IS_HARDWARE_TYPE_8822B(padapter) && !IS_HARDWARE_TYPE_8822C(padapter)) {
				padapter->registrypriv.RegPwrTrimEnable = 1;
				rtw_hal_read_chip_info(padapter);
			}
#endif /*CONFIG_RF_POWER_TRIM*/
			rtw_reset_drv_sw(padapter);
#ifdef CONFIG_NEW_NETDEV_HDL
			if (!rtw_is_hw_init_completed(padapter)) {
				status = rtw_hal_init(padapter);
				if (status == _FAIL) {
					ret = H2C_REJECTED;
					goto exit;
				}
				rtw_hal_iface_init(padapter);
			}
#else
			status = rtw_hal_init(padapter);
			if (status == _FAIL) {
				ret = H2C_REJECTED;
				goto exit;
			}
#endif /*CONFIG_NEW_NETDEV_HDL*/
#ifndef RTW_HALMAC
			rtw_intf_start(padapter);
#endif /* !RTW_HALMAC */
#ifdef RTW_HALMAC /*for New IC*/
			MPT_InitializeAdapter(padapter, 1);
#endif /* CONFIG_MP_INCLUDED */
		}

		if (padapter->registrypriv.mp_mode == 0) {
			ret = H2C_REJECTED;
			goto exit;
		}

		if (padapter->mppriv.mode == MP_OFF) {
			if (mp_start_test(padapter) == _FAIL) {
				ret = H2C_REJECTED;
				goto exit;
			}
			padapter->mppriv.mode = MP_ON;
			MPT_PwrCtlDM(padapter, 0);
		}
		padapter->mppriv.bmac_filter = _FALSE;
#ifdef CONFIG_RTL8723B
#ifdef CONFIG_USB_HCI
		rtw_write32(padapter, 0x765, 0x0000);
		rtw_write32(padapter, 0x948, 0x0280);
#else
		rtw_write32(padapter, 0x765, 0x0000);
		rtw_write32(padapter, 0x948, 0x0000);
#endif
#ifdef CONFIG_FOR_RTL8723BS_VQ0
		rtw_write32(padapter, 0x765, 0x0000);
		rtw_write32(padapter, 0x948, 0x0280);
#endif
		rtw_write8(padapter, 0x66, 0x27); /*Open BT uart Log*/
		rtw_write8(padapter, 0xc50, 0x20); /*for RX init Gain*/
#endif
		odm_write_dig(&pHalData->odmpriv, 0x20);

	} else if (mp_cmd_id == MP_STOP) {
		if (padapter->registrypriv.mp_mode == 1) {
			MPT_DeInitAdapter(padapter);
			rtw_intf_stop(padapter);
			rtw_hal_deinit(padapter);
			padapter->registrypriv.mp_mode = 0;
#ifdef CONFIG_BT_COEXIST
			pHalData->EEPROMBluetoothCoexist = padapter->mppriv.CureFuseBTCoex;
#endif
			rtw_reset_drv_sw(padapter);
#ifdef CONFIG_NEW_NETDEV_HDL
			if (!rtw_is_hw_init_completed(padapter)) {
				status = rtw_hal_init(padapter);
				if (status == _FAIL) {
					ret = H2C_REJECTED;
					goto exit;
				}
				rtw_hal_iface_init(padapter);
			}
#else
			status = rtw_hal_init(padapter);
			if (status == _FAIL) {
				ret = H2C_REJECTED;
				goto exit;
			}
#endif /*CONFIG_NEW_NETDEV_HDL*/
#ifndef RTW_HALMAC
			rtw_intf_start(padapter);
#endif /* !RTW_HALMAC */
		}

		if (padapter->mppriv.mode != MP_OFF) {
			mp_stop_test(padapter);
			padapter->mppriv.mode = MP_OFF;
		}

	} else {
		RTW_INFO(FUNC_ADPT_FMT"invalid id:%d\n", FUNC_ADPT_ARG(padapter), mp_cmd_id);
		ret = H2C_PARAMETERS_ERROR;
		rtw_warn_on(1);
	}

exit:
	return ret;
}

u8 rtw_mp_cmd(_adapter *adapter, u8 mp_cmd_id, u8 flags)
{
	struct cmd_obj *cmdobj;
	struct drvextra_cmd_parm *parm;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	struct submit_ctx sctx;
	u8	res = _SUCCESS;

	parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (parm == NULL) {
		res = _FAIL;
		goto exit;
	}

	parm->ec_id = MP_CMD_WK_CID;
	parm->type = mp_cmd_id;
	parm->size = 0;
	parm->pbuf = NULL;

	if (flags & RTW_CMDF_DIRECTLY) {
		/* no need to enqueue, do the cmd hdl directly and free cmd parameter */
		if (H2C_SUCCESS != rtw_mp_cmd_hdl(adapter, mp_cmd_id))
			res = _FAIL;
		rtw_mfree((u8 *)parm, sizeof(*parm));
	} else {
		/* need enqueue, prepare cmd_obj and enqueue */
		cmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(*cmdobj));
		if (cmdobj == NULL) {
			res = _FAIL;
			rtw_mfree((u8 *)parm, sizeof(*parm));
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(cmdobj, parm, CMD_SET_DRV_EXTRA);

		if (flags & RTW_CMDF_WAIT_ACK) {
			cmdobj->sctx = &sctx;
			rtw_sctx_init(&sctx, 10 * 1000);
		}

		res = rtw_enqueue_cmd(pcmdpriv, cmdobj);

		if (res == _SUCCESS && (flags & RTW_CMDF_WAIT_ACK)) {
			rtw_sctx_wait(&sctx, __func__);
			_enter_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
			if (sctx.status == RTW_SCTX_SUBMITTED)
				cmdobj->sctx = NULL;
			_exit_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
			if (sctx.status != RTW_SCTX_DONE_SUCCESS)
				res = _FAIL;
		}
	}

exit:
	return res;
}
#endif	/*CONFIG_MP_INCLUDED*/

#ifdef CONFIG_RTW_CUSTOMER_STR
static s32 rtw_customer_str_cmd_hdl(_adapter *adapter, u8 write, const u8 *cstr)
{
	int ret = H2C_SUCCESS;

	if (write)
		ret = rtw_hal_h2c_customer_str_write(adapter, cstr);
	else
		ret = rtw_hal_h2c_customer_str_req(adapter);

	return ret == _SUCCESS ? H2C_SUCCESS : H2C_REJECTED;
}

static u8 rtw_customer_str_cmd(_adapter *adapter, u8 write, const u8 *cstr)
{
	struct cmd_obj *cmdobj;
	struct drvextra_cmd_parm *parm;
	u8 *str = NULL;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	struct submit_ctx sctx;
	u8 res = _SUCCESS;

	parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (parm == NULL) {
		res = _FAIL;
		goto exit;
	}

	if (write) {
		str = rtw_zmalloc(RTW_CUSTOMER_STR_LEN);
		if (str == NULL) {
			rtw_mfree((u8 *)parm, sizeof(struct drvextra_cmd_parm));
			res = _FAIL;
			goto exit;
		}
	}

	parm->ec_id = CUSTOMER_STR_WK_CID;
	parm->type = write;
	parm->size = write ? RTW_CUSTOMER_STR_LEN : 0;
	parm->pbuf = write ? str : NULL;

	if (write)
		_rtw_memcpy(str, cstr, RTW_CUSTOMER_STR_LEN);

	/* need enqueue, prepare cmd_obj and enqueue */
	cmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(*cmdobj));
	if (cmdobj == NULL) {
		res = _FAIL;
		rtw_mfree((u8 *)parm, sizeof(*parm));
		if (write)
			rtw_mfree(str, RTW_CUSTOMER_STR_LEN);
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(cmdobj, parm, CMD_SET_DRV_EXTRA);

	cmdobj->sctx = &sctx;
	rtw_sctx_init(&sctx, 2 * 1000);

	res = rtw_enqueue_cmd(pcmdpriv, cmdobj);

	if (res == _SUCCESS) {
		rtw_sctx_wait(&sctx, __func__);
		_enter_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
		if (sctx.status == RTW_SCTX_SUBMITTED)
			cmdobj->sctx = NULL;
		_exit_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
		if (sctx.status != RTW_SCTX_DONE_SUCCESS)
			res = _FAIL;
	}

exit:
	return res;
}

inline u8 rtw_customer_str_req_cmd(_adapter *adapter)
{
	return rtw_customer_str_cmd(adapter, 0, NULL);
}

inline u8 rtw_customer_str_write_cmd(_adapter *adapter, const u8 *cstr)
{
	return rtw_customer_str_cmd(adapter, 1, cstr);
}
#endif /* CONFIG_RTW_CUSTOMER_STR */

u8 rtw_c2h_wk_cmd(PADAPTER padapter, u8 *pbuf, u16 length, u8 type)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 *extra_cmd_buf;
	u8 res = _SUCCESS;

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

	extra_cmd_buf = rtw_zmalloc(length);
	if (extra_cmd_buf == NULL) {
		rtw_mfree((u8 *)ph2c, sizeof(struct cmd_obj));
		rtw_mfree((u8 *)pdrvextra_cmd_parm, sizeof(struct drvextra_cmd_parm));
		res = _FAIL;
		goto exit;
	}

	_rtw_memcpy(extra_cmd_buf, pbuf, length);
	pdrvextra_cmd_parm->ec_id = C2H_WK_CID;
	pdrvextra_cmd_parm->type = type;
	pdrvextra_cmd_parm->size = length;
	pdrvextra_cmd_parm->pbuf = extra_cmd_buf;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:
	return res;
}

#ifdef CONFIG_FW_C2H_REG
inline u8 rtw_c2h_reg_wk_cmd(_adapter *adapter, u8 *c2h_evt)
{
	return rtw_c2h_wk_cmd(adapter, c2h_evt, c2h_evt ? C2H_REG_LEN : 0, C2H_TYPE_REG);
}
#endif

#ifdef CONFIG_FW_C2H_PKT
inline u8 rtw_c2h_packet_wk_cmd(_adapter *adapter, u8 *c2h_evt, u16 length)
{
	return rtw_c2h_wk_cmd(adapter, c2h_evt, length, C2H_TYPE_PKT);
}
#endif

static u8 _rtw_run_in_thread_cmd(_adapter *adapter, void (*func)(void *), void *context, s32 timeout_ms)
{
	struct cmd_priv *cmdpriv = &adapter->cmdpriv;
	struct cmd_obj *cmdobj;
	struct RunInThread_param *parm;
	struct submit_ctx sctx;
	s32 res = _SUCCESS;

	cmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (NULL == cmdobj) {
		res = _FAIL;
		goto exit;
	}

	parm = (struct RunInThread_param *)rtw_zmalloc(sizeof(struct RunInThread_param));
	if (NULL == parm) {
		rtw_mfree((u8 *)cmdobj, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	parm->func = func;
	parm->context = context;
	init_h2fwcmd_w_parm_no_rsp(cmdobj, parm, CMD_RUN_INTHREAD);

	if (timeout_ms >= 0) {
		cmdobj->sctx = &sctx;
		rtw_sctx_init(&sctx, timeout_ms);
	}

	res = rtw_enqueue_cmd(cmdpriv, cmdobj);

	if (res == _SUCCESS && timeout_ms >= 0) {
		rtw_sctx_wait(&sctx, __func__);
		_enter_critical_mutex(&cmdpriv->sctx_mutex, NULL);
		if (sctx.status == RTW_SCTX_SUBMITTED)
			cmdobj->sctx = NULL;
		_exit_critical_mutex(&cmdpriv->sctx_mutex, NULL);
		if (sctx.status != RTW_SCTX_DONE_SUCCESS)
			res = _FAIL;
	}

exit:
	return res;
}

u8 rtw_run_in_thread_cmd(_adapter *adapter, void (*func)(void *), void *context)
{
	return _rtw_run_in_thread_cmd(adapter, func, context, -1);
}

u8 rtw_run_in_thread_cmd_wait(_adapter *adapter, void (*func)(void *), void *context, s32 timeout_ms)
{
	return _rtw_run_in_thread_cmd(adapter, func, context, timeout_ms);
}

#ifdef CONFIG_FW_C2H_REG
s32 c2h_evt_hdl(_adapter *adapter, u8 *c2h_evt, c2h_id_filter filter)
{
	s32 ret = _FAIL;
	u8 buf[C2H_REG_LEN] = {0};
	u8 id, seq, plen;
	u8 *payload;

	if (!c2h_evt) {
		/* No c2h event in cmd_obj, read c2h event before handling*/
		if (rtw_hal_c2h_evt_read(adapter, buf) != _SUCCESS)
			goto exit;
		c2h_evt = buf;
	}

	rtw_hal_c2h_reg_hdr_parse(adapter, c2h_evt, &id, &seq, &plen, &payload);

	if (filter && filter(adapter, id, seq, plen, payload) == _FALSE)
		goto exit;

	ret = rtw_hal_c2h_handler(adapter, id, seq, plen, payload);

exit:
	return ret;
}
#endif /* CONFIG_FW_C2H_REG */

u8 session_tracker_cmd(_adapter *adapter, u8 cmd, struct sta_info *sta, u8 *local_naddr, u8 *local_port, u8 *remote_naddr, u8 *remote_port)
{
	struct cmd_priv	*cmdpriv = &adapter->cmdpriv;
	struct cmd_obj *cmdobj;
	struct drvextra_cmd_parm *cmd_parm;
	struct st_cmd_parm *st_parm;
	u8	res = _SUCCESS;

	cmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (cmdobj == NULL) {
		res = _FAIL;
		goto exit;
	}

	cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (cmd_parm == NULL) {
		rtw_mfree((u8 *)cmdobj, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	st_parm = (struct st_cmd_parm *)rtw_zmalloc(sizeof(struct st_cmd_parm));
	if (st_parm == NULL) {
		rtw_mfree((u8 *)cmdobj, sizeof(struct cmd_obj));
		rtw_mfree((u8 *)cmd_parm, sizeof(struct drvextra_cmd_parm));
		res = _FAIL;
		goto exit;
	}

	st_parm->cmd = cmd;
	st_parm->sta = sta;
	if (cmd != ST_CMD_CHK) {
		_rtw_memcpy(&st_parm->local_naddr, local_naddr, 4);
		_rtw_memcpy(&st_parm->local_port, local_port, 2);
		_rtw_memcpy(&st_parm->remote_naddr, remote_naddr, 4);
		_rtw_memcpy(&st_parm->remote_port, remote_port, 2);
	}

	cmd_parm->ec_id = SESSION_TRACKER_WK_CID;
	cmd_parm->type = 0;
	cmd_parm->size = sizeof(struct st_cmd_parm);
	cmd_parm->pbuf = (u8 *)st_parm;
	init_h2fwcmd_w_parm_no_rsp(cmdobj, cmd_parm, CMD_SET_DRV_EXTRA);
	cmdobj->no_io = 1;

	res = rtw_enqueue_cmd(cmdpriv, cmdobj);

exit:
	return res;
}

inline u8 session_tracker_chk_cmd(_adapter *adapter, struct sta_info *sta)
{
	return session_tracker_cmd(adapter, ST_CMD_CHK, sta, NULL, NULL, NULL, NULL);
}

inline u8 session_tracker_add_cmd(_adapter *adapter, struct sta_info *sta, u8 *local_naddr, u8 *local_port, u8 *remote_naddr, u8 *remote_port)
{
	return session_tracker_cmd(adapter, ST_CMD_ADD, sta, local_naddr, local_port, remote_naddr, remote_port);
}

inline u8 session_tracker_del_cmd(_adapter *adapter, struct sta_info *sta, u8 *local_naddr, u8 *local_port, u8 *remote_naddr, u8 *remote_port)
{
	return session_tracker_cmd(adapter, ST_CMD_DEL, sta, local_naddr, local_port, remote_naddr, remote_port);
}

void session_tracker_chk_for_sta(_adapter *adapter, struct sta_info *sta)
{
	struct st_ctl_t *st_ctl = &sta->st_ctl;
	int i;
	_irqL irqL;
	_list *plist, *phead, *pnext;
	_list dlist;
	struct session_tracker *st = NULL;
	u8 op_wfd_mode = MIRACAST_DISABLED;

	if (DBG_SESSION_TRACKER)
		RTW_INFO(FUNC_ADPT_FMT" sta:%p\n", FUNC_ADPT_ARG(adapter), sta);

	if (!(sta->state & WIFI_ASOC_STATE))
		goto exit;

	for (i = 0; i < SESSION_TRACKER_REG_ID_NUM; i++) {
		if (st_ctl->reg[i].s_proto != 0)
			break;
	}
	if (i >= SESSION_TRACKER_REG_ID_NUM)
		goto chk_sta;

	_rtw_init_listhead(&dlist);

	_enter_critical_bh(&st_ctl->tracker_q.lock, &irqL);

	phead = &st_ctl->tracker_q.queue;
	plist = get_next(phead);
	pnext = get_next(plist);
	while (rtw_end_of_queue_search(phead, plist) == _FALSE) {
		st = LIST_CONTAINOR(plist, struct session_tracker, list);
		plist = pnext;
		pnext = get_next(pnext);

		if (st->status != ST_STATUS_ESTABLISH
			&& rtw_get_passing_time_ms(st->set_time) > ST_EXPIRE_MS
		) {
			rtw_list_delete(&st->list);
			rtw_list_insert_tail(&st->list, &dlist);
		}

		/* TODO: check OS for status update */
		if (st->status == ST_STATUS_CHECK)
			st->status = ST_STATUS_ESTABLISH;

		if (st->status != ST_STATUS_ESTABLISH)
			continue;

		#ifdef CONFIG_WFD
		if (0)
			RTW_INFO(FUNC_ADPT_FMT" local:%u, remote:%u, rtsp:%u, %u, %u\n", FUNC_ADPT_ARG(adapter)
				, ntohs(st->local_port), ntohs(st->remote_port), adapter->wfd_info.rtsp_ctrlport, adapter->wfd_info.tdls_rtsp_ctrlport
				, adapter->wfd_info.peer_rtsp_ctrlport);
		if (ntohs(st->local_port) == adapter->wfd_info.rtsp_ctrlport)
			op_wfd_mode |= MIRACAST_SINK;
		if (ntohs(st->local_port) == adapter->wfd_info.tdls_rtsp_ctrlport)
			op_wfd_mode |= MIRACAST_SINK;
		if (ntohs(st->remote_port) == adapter->wfd_info.peer_rtsp_ctrlport)
			op_wfd_mode |= MIRACAST_SOURCE;
		#endif
	}

	_exit_critical_bh(&st_ctl->tracker_q.lock, &irqL);

	plist = get_next(&dlist);
	while (rtw_end_of_queue_search(&dlist, plist) == _FALSE) {
		st = LIST_CONTAINOR(plist, struct session_tracker, list);
		plist = get_next(plist);
		rtw_mfree((u8 *)st, sizeof(struct session_tracker));
	}

chk_sta:
	if (STA_OP_WFD_MODE(sta) != op_wfd_mode) {
		STA_SET_OP_WFD_MODE(sta, op_wfd_mode);
		rtw_sta_media_status_rpt_cmd(adapter, sta, 1);
	}

exit:
	return;
}

void session_tracker_chk_for_adapter(_adapter *adapter)
{
	struct sta_priv *stapriv = &adapter->stapriv;
	struct sta_info *sta;
	int i;
	_irqL irqL;
	_list *plist, *phead;
	u8 op_wfd_mode = MIRACAST_DISABLED;

	_enter_critical_bh(&stapriv->sta_hash_lock, &irqL);

	for (i = 0; i < NUM_STA; i++) {
		phead = &(stapriv->sta_hash[i]);
		plist = get_next(phead);

		while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
			sta = LIST_CONTAINOR(plist, struct sta_info, hash_list);
			plist = get_next(plist);

			session_tracker_chk_for_sta(adapter, sta);

			op_wfd_mode |= STA_OP_WFD_MODE(sta);
		}
	}

	_exit_critical_bh(&stapriv->sta_hash_lock, &irqL);

#ifdef CONFIG_WFD
	adapter->wfd_info.op_wfd_mode = MIRACAST_MODE_REVERSE(op_wfd_mode);
#endif
}

void session_tracker_cmd_hdl(_adapter *adapter, struct st_cmd_parm *parm)
{
	u8 cmd = parm->cmd;
	struct sta_info *sta = parm->sta;

	if (cmd == ST_CMD_CHK) {
		if (sta)
			session_tracker_chk_for_sta(adapter, sta);
		else
			session_tracker_chk_for_adapter(adapter);

		goto exit;

	} else if (cmd == ST_CMD_ADD || cmd == ST_CMD_DEL) {
		struct st_ctl_t *st_ctl;
		u32 local_naddr = parm->local_naddr;
		u16 local_port = parm->local_port;
		u32 remote_naddr = parm->remote_naddr;
		u16 remote_port = parm->remote_port;
		struct session_tracker *st = NULL;
		_irqL irqL;
		_list *plist, *phead;
		u8 free_st = 0;
		u8 alloc_st = 0;

		if (DBG_SESSION_TRACKER)
			RTW_INFO(FUNC_ADPT_FMT" cmd:%u, sta:%p, local:"IP_FMT":"PORT_FMT", remote:"IP_FMT":"PORT_FMT"\n"
				, FUNC_ADPT_ARG(adapter), cmd, sta
				, IP_ARG(&local_naddr), PORT_ARG(&local_port)
				, IP_ARG(&remote_naddr), PORT_ARG(&remote_port)
			);

		if (!(sta->state & WIFI_ASOC_STATE))
			goto exit;

		st_ctl = &sta->st_ctl;

		_enter_critical_bh(&st_ctl->tracker_q.lock, &irqL);

		phead = &st_ctl->tracker_q.queue;
		plist = get_next(phead);
		while (rtw_end_of_queue_search(phead, plist) == _FALSE) {
			st = LIST_CONTAINOR(plist, struct session_tracker, list);

			if (st->local_naddr == local_naddr
				&& st->local_port == local_port
				&& st->remote_naddr == remote_naddr
				&& st->remote_port == remote_port)
				break;

			plist = get_next(plist);
		}

		if (rtw_end_of_queue_search(phead, plist) == _TRUE)
			st = NULL;

		switch (cmd) {
		case ST_CMD_DEL:
			if (st) {
				rtw_list_delete(plist);
				free_st = 1;
			}
			goto unlock;
		case ST_CMD_ADD:
			if (!st)
				alloc_st = 1;
		}

unlock:
		_exit_critical_bh(&st_ctl->tracker_q.lock, &irqL);

		if (free_st) {
			rtw_mfree((u8 *)st, sizeof(struct session_tracker));
			goto exit;
		}

		if (alloc_st) {
			st = (struct session_tracker *)rtw_zmalloc(sizeof(struct session_tracker));
			if (!st)
				goto exit;

			st->local_naddr = local_naddr;
			st->local_port = local_port;
			st->remote_naddr = remote_naddr;
			st->remote_port = remote_port;
			st->set_time = rtw_get_current_time();
			st->status = ST_STATUS_CHECK;

			_enter_critical_bh(&st_ctl->tracker_q.lock, &irqL);
			rtw_list_insert_tail(&st->list, phead);
			_exit_critical_bh(&st_ctl->tracker_q.lock, &irqL);
		}
	}

exit:
	return;
}

#if defined(CONFIG_RTW_MESH) && defined(RTW_PER_CMD_SUPPORT_FW)
static s32 rtw_req_per_cmd_hdl(_adapter *adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);
	struct macid_bmp req_macid_bmp, *macid_bmp;
	u8 i, ret = _FAIL;

	macid_bmp = &macid_ctl->if_g[adapter->iface_id];
	_rtw_memcpy(&req_macid_bmp, macid_bmp, sizeof(struct macid_bmp));

	/* Clear none mesh's macid */
	for (i = 0; i < macid_ctl->num; i++) {
		u8 role;
		role = GET_H2CCMD_MSRRPT_PARM_ROLE(&macid_ctl->h2c_msr[i]);
		if (role != H2C_MSR_ROLE_MESH)
			rtw_macid_map_clr(&req_macid_bmp, i);
	}

	/* group_macid: always be 0 in NIC, so only pass macid_bitmap.m0
	 * rpt_type: 0 includes all info in 1, use 0 for now 
	 * macid_bitmap: pass m0 only for NIC
	 */
	ret = rtw_hal_set_req_per_rpt_cmd(adapter, 0, 0, req_macid_bmp.m0);

	return ret;
}

u8 rtw_req_per_cmd(_adapter *adapter)
{
	struct cmd_obj *cmdobj;
	struct drvextra_cmd_parm *parm;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	struct submit_ctx sctx;
	u8 res = _SUCCESS;

	parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (parm == NULL) {
		res = _FAIL;
		goto exit;
	}

	parm->ec_id = REQ_PER_CMD_WK_CID;
	parm->type = 0;
	parm->size = 0;
	parm->pbuf = NULL;

	cmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(*cmdobj));
	if (cmdobj == NULL) {
		res = _FAIL;
		rtw_mfree((u8 *)parm, sizeof(*parm));
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(cmdobj, parm, CMD_SET_DRV_EXTRA);

	res = rtw_enqueue_cmd(pcmdpriv, cmdobj);

exit:
	return res;
}
#endif


void rtw_ac_parm_cmd_hdl(_adapter *padapter, u8 *_ac_parm_buf, int ac_type)
{

	u32 ac_parm_buf;

	_rtw_memcpy(&ac_parm_buf, _ac_parm_buf, sizeof(ac_parm_buf));
	switch (ac_type) {
	case XMIT_VO_QUEUE:
		RTW_INFO(FUNC_NDEV_FMT" AC_VO = 0x%08x\n", FUNC_ADPT_ARG(padapter), (unsigned int) ac_parm_buf);
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VO, (u8 *)(&ac_parm_buf));
		break;

	case XMIT_VI_QUEUE:
		RTW_INFO(FUNC_NDEV_FMT" AC_VI = 0x%08x\n", FUNC_ADPT_ARG(padapter), (unsigned int) ac_parm_buf);
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VI, (u8 *)(&ac_parm_buf));
		break;

	case XMIT_BE_QUEUE:
		RTW_INFO(FUNC_NDEV_FMT" AC_BE = 0x%08x\n", FUNC_ADPT_ARG(padapter), (unsigned int) ac_parm_buf);
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&ac_parm_buf));
		break;

	case XMIT_BK_QUEUE:
		RTW_INFO(FUNC_NDEV_FMT" AC_BK = 0x%08x\n", FUNC_ADPT_ARG(padapter), (unsigned int) ac_parm_buf);
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BK, (u8 *)(&ac_parm_buf));
		break;

	default:
		break;
	}

}


u8 rtw_drvextra_cmd_hdl(_adapter *padapter, unsigned char *pbuf)
{
	int ret = H2C_SUCCESS;
	struct drvextra_cmd_parm *pdrvextra_cmd;

	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	pdrvextra_cmd = (struct drvextra_cmd_parm *)pbuf;

	switch (pdrvextra_cmd->ec_id) {
	case STA_MSTATUS_RPT_WK_CID:
		rtw_sta_media_status_rpt_cmd_hdl(padapter, (struct sta_media_status_rpt_cmd_parm *)pdrvextra_cmd->pbuf);
		break;

	case DYNAMIC_CHK_WK_CID:/*only  primary padapter go to this cmd, but execute dynamic_chk_wk_hdl() for two interfaces */
		rtw_dynamic_chk_wk_hdl(padapter);
		break;
	case POWER_SAVING_CTRL_WK_CID:
		power_saving_wk_hdl(padapter);
		break;
#ifdef CONFIG_LPS
	case LPS_CTRL_WK_CID:
		lps_ctrl_wk_hdl(padapter, (u8)pdrvextra_cmd->type, pdrvextra_cmd->pbuf);
		break;
	case DM_IN_LPS_WK_CID:
		rtw_dm_in_lps_hdl(padapter);
		break;
	case LPS_CHANGE_DTIM_CID:
		rtw_lps_change_dtim_hdl(padapter, (u8)pdrvextra_cmd->type);
		break;
#endif
#if (RATE_ADAPTIVE_SUPPORT == 1)
	case RTP_TIMER_CFG_WK_CID:
		rpt_timer_setting_wk_hdl(padapter, pdrvextra_cmd->type);
		break;
#endif
#ifdef CONFIG_ANTENNA_DIVERSITY
	case ANT_SELECT_WK_CID:
		antenna_select_wk_hdl(padapter, pdrvextra_cmd->type);
		break;
#endif
#ifdef CONFIG_P2P_PS
	case P2P_PS_WK_CID:
		p2p_ps_wk_hdl(padapter, pdrvextra_cmd->type);
		break;
#endif
#ifdef CONFIG_P2P
	case P2P_PROTO_WK_CID:
		/*
		* Commented by Albert 2011/07/01
		* I used the type_size as the type command
		*/
		ret = p2p_protocol_wk_hdl(padapter, pdrvextra_cmd->type, pdrvextra_cmd->pbuf);
		break;
#endif
#ifdef CONFIG_AP_MODE
	case CHECK_HIQ_WK_CID:
		rtw_chk_hi_queue_hdl(padapter);
		break;
#endif
	/* add for CONFIG_IEEE80211W, none 11w can use it */
	case RESET_SECURITYPRIV:
		reset_securitypriv_hdl(padapter);
		break;
	case FREE_ASSOC_RESOURCES:
		free_assoc_resources_hdl(padapter, (u8)pdrvextra_cmd->type);
		break;
	case C2H_WK_CID:
		switch (pdrvextra_cmd->type) {
		#ifdef CONFIG_FW_C2H_REG
		case C2H_TYPE_REG:
			c2h_evt_hdl(padapter, pdrvextra_cmd->pbuf, NULL);
			break;
		#endif
		#ifdef CONFIG_FW_C2H_PKT
		case C2H_TYPE_PKT:
			rtw_hal_c2h_pkt_hdl(padapter, pdrvextra_cmd->pbuf, pdrvextra_cmd->size);
			break;
		#endif
		default:
			RTW_ERR("unknown C2H type:%d\n", pdrvextra_cmd->type);
			rtw_warn_on(1);
			break;
		}
		break;
#ifdef CONFIG_BEAMFORMING
	case BEAMFORMING_WK_CID:
		beamforming_wk_hdl(padapter, pdrvextra_cmd->type, pdrvextra_cmd->pbuf);
		break;
#endif
	case DM_RA_MSK_WK_CID:
		rtw_dm_ra_mask_hdl(padapter, (struct sta_info *)pdrvextra_cmd->pbuf);
		break;
#ifdef CONFIG_BT_COEXIST
	case BTINFO_WK_CID:
		rtw_btinfo_hdl(padapter, pdrvextra_cmd->pbuf, pdrvextra_cmd->size);
		break;
	case BTC_REDUCE_WL_TXPWR_CID:
		rtw_btc_reduce_wl_txpwr_hdl(padapter, pdrvextra_cmd->type);
		break;
#endif
#ifdef CONFIG_DFS_MASTER
	case DFS_RADAR_DETECT_WK_CID:
		rtw_dfs_rd_hdl(padapter);
		break;
	case DFS_RADAR_DETECT_EN_DEC_WK_CID:
		rtw_dfs_rd_en_decision(padapter, MLME_ACTION_NONE, 0);
		break;
#endif
	case SESSION_TRACKER_WK_CID:
		session_tracker_cmd_hdl(padapter, (struct st_cmd_parm *)pdrvextra_cmd->pbuf);
		break;
	case EN_HW_UPDATE_TSF_WK_CID:
		rtw_hal_set_hwreg(padapter, HW_VAR_EN_HW_UPDATE_TSF, NULL);
		break;
	case PERIOD_TSF_UPDATE_END_WK_CID:
		rtw_hal_periodic_tsf_update_chk(padapter);
		break;
	case TEST_H2C_CID:
		rtw_hal_fill_h2c_cmd(padapter, pdrvextra_cmd->pbuf[0], pdrvextra_cmd->size - 1, &pdrvextra_cmd->pbuf[1]);
		break;
	case MP_CMD_WK_CID:
#ifdef CONFIG_MP_INCLUDED
		ret = rtw_mp_cmd_hdl(padapter, pdrvextra_cmd->type);
#endif
		break;
#ifdef CONFIG_RTW_CUSTOMER_STR
	case CUSTOMER_STR_WK_CID:
		ret = rtw_customer_str_cmd_hdl(padapter, pdrvextra_cmd->type, pdrvextra_cmd->pbuf);
		break;
#endif

#ifdef CONFIG_RTW_REPEATER_SON
	case RSON_SCAN_WK_CID:
		rtw_rson_scan_cmd_hdl(padapter, pdrvextra_cmd->type);
		break;
#endif

#ifdef CONFIG_IOCTL_CFG80211
	case MGNT_TX_WK_CID:
		ret = rtw_mgnt_tx_handler(padapter, pdrvextra_cmd->pbuf);
		break;
#endif /* CONFIG_IOCTL_CFG80211 */
#ifdef CONFIG_MCC_MODE
	case MCC_CMD_WK_CID:
		ret = rtw_mcc_cmd_hdl(padapter, pdrvextra_cmd->type, pdrvextra_cmd->pbuf);
		break;
#endif /* CONFIG_MCC_MODE */
#if defined(CONFIG_RTW_MESH) && defined(RTW_PER_CMD_SUPPORT_FW)
	case REQ_PER_CMD_WK_CID:
		ret = rtw_req_per_cmd_hdl(padapter);
		break;
#endif
#ifdef CONFIG_SUPPORT_STATIC_SMPS
	case SSMPS_WK_CID :
		rtw_ssmps_wk_hdl(padapter, (struct ssmps_cmd_parm *)pdrvextra_cmd->pbuf);
		break;
#endif
#ifdef CONFIG_CTRL_TXSS_BY_TP
	case TXSS_WK_CID :
		rtw_ctrl_txss_wk_hdl(padapter, (struct txss_cmd_parm *)pdrvextra_cmd->pbuf);
		break;
#endif
	case AC_PARM_CMD_WK_CID:
		rtw_ac_parm_cmd_hdl(padapter, pdrvextra_cmd->pbuf, pdrvextra_cmd->type);
		break;
#ifdef CONFIG_AP_MODE
	case STOP_AP_WK_CID:
		stop_ap_hdl(padapter);
		break;
#endif
#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
	case TBTX_CONTROL_TX_WK_CID:
		tx_control_hdl(padapter);
		break;
#endif
	default:
		break;
	}

	if (pdrvextra_cmd->pbuf && pdrvextra_cmd->size > 0)
		rtw_mfree(pdrvextra_cmd->pbuf, pdrvextra_cmd->size);

	return ret;
}

void rtw_survey_cmd_callback(_adapter	*padapter ,  struct cmd_obj *pcmd)
{
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;


	if (pcmd->res == H2C_DROPPED) {
		/* TODO: cancel timer and do timeout handler directly... */
		/* need to make timeout handlerOS independent */
		mlme_set_scan_to_timer(pmlmepriv, 1);
	} else if (pcmd->res != H2C_SUCCESS) {
		mlme_set_scan_to_timer(pmlmepriv, 1);
	}

	/* free cmd */
	rtw_free_cmd_obj(pcmd);

}
void rtw_disassoc_cmd_callback(_adapter	*padapter,  struct cmd_obj *pcmd)
{
	_irqL	irqL;
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;


	if (pcmd->res != H2C_SUCCESS) {
		_enter_critical_bh(&pmlmepriv->lock, &irqL);
		set_fwstate(pmlmepriv, WIFI_ASOC_STATE);
		_exit_critical_bh(&pmlmepriv->lock, &irqL);
		goto exit;
	}
#ifdef CONFIG_BR_EXT
	else /* clear bridge database */
		nat25_db_cleanup(padapter);
#endif /* CONFIG_BR_EXT */

	/* free cmd */
	rtw_free_cmd_obj(pcmd);

exit:
	return;
}

void rtw_joinbss_cmd_callback(_adapter	*padapter,  struct cmd_obj *pcmd)
{
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;


	if (pcmd->res == H2C_DROPPED) {
		/* TODO: cancel timer and do timeout handler directly... */
		/* need to make timeout handlerOS independent */
		_set_timer(&pmlmepriv->assoc_timer, 1);
	} else if (pcmd->res != H2C_SUCCESS)
		_set_timer(&pmlmepriv->assoc_timer, 1);

	rtw_free_cmd_obj(pcmd);

}

void rtw_create_ibss_post_hdl(_adapter *padapter, int status)
{
	_irqL irqL;
	struct wlan_network *pwlan = NULL;
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;
	WLAN_BSSID_EX *pdev_network = &padapter->registrypriv.dev_network;
	struct wlan_network *mlme_cur_network = &(pmlmepriv->cur_network);

	if (status != H2C_SUCCESS)
		_set_timer(&pmlmepriv->assoc_timer, 1);

	_cancel_timer_ex(&pmlmepriv->assoc_timer);

	_enter_critical_bh(&pmlmepriv->lock, &irqL);

	{
		_irqL irqL;

		pwlan = _rtw_alloc_network(pmlmepriv);
		_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
		if (pwlan == NULL) {
			pwlan = rtw_get_oldest_wlan_network(&pmlmepriv->scanned_queue);
			if (pwlan == NULL) {
				_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
				goto createbss_cmd_fail;
			}
			pwlan->last_scanned = rtw_get_current_time();
		} else
			rtw_list_insert_tail(&(pwlan->list), &pmlmepriv->scanned_queue.queue);

		pdev_network->Length = get_WLAN_BSSID_EX_sz(pdev_network);
		_rtw_memcpy(&(pwlan->network), pdev_network, pdev_network->Length);
		/* pwlan->fixed = _TRUE; */

		/* copy pdev_network information to pmlmepriv->cur_network */
		_rtw_memcpy(&mlme_cur_network->network, pdev_network, (get_WLAN_BSSID_EX_sz(pdev_network)));

#if 0
		/* reset DSConfig */
		mlme_cur_network->network.Configuration.DSConfig = (u32)rtw_ch2freq(pdev_network->Configuration.DSConfig);
#endif

		_clr_fwstate_(pmlmepriv, WIFI_UNDER_LINKING);
		_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
		/* we will set WIFI_ASOC_STATE when there is one more sat to join us (rtw_stassoc_event_callback) */
	}

createbss_cmd_fail:
	_exit_critical_bh(&pmlmepriv->lock, &irqL);
	return;
}



void rtw_setstaKey_cmdrsp_callback(_adapter	*padapter ,  struct cmd_obj *pcmd)
{

	struct sta_priv *pstapriv = &padapter->stapriv;
	struct set_stakey_rsp *psetstakey_rsp = (struct set_stakey_rsp *)(pcmd->rsp);
	struct sta_info	*psta = rtw_get_stainfo(pstapriv, psetstakey_rsp->addr);


	if (psta == NULL) {
		goto exit;
	}

	/* psta->cmn.aid = psta->cmn.mac_id = psetstakey_rsp->keyid; */ /* CAM_ID(CAM_ENTRY) */

exit:

	rtw_free_cmd_obj(pcmd);


}

void rtw_getrttbl_cmd_cmdrsp_callback(_adapter	*padapter,  struct cmd_obj *pcmd)
{

	rtw_free_cmd_obj(pcmd);
#ifdef CONFIG_MP_INCLUDED
	if (padapter->registrypriv.mp_mode == 1)
		padapter->mppriv.workparam.bcompleted = _TRUE;
#endif


}

u8 set_txq_params_cmd(_adapter *adapter, u32 ac_parm, u8 ac_type)
{
	struct cmd_obj *cmdobj;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	u8 *ac_parm_buf = NULL;
	u8 sz;
	u8 res = _SUCCESS;


	cmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (cmdobj == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		rtw_mfree((u8 *)cmdobj, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	sz = sizeof(ac_parm);
	ac_parm_buf = rtw_zmalloc(sz);
	if (ac_parm_buf == NULL) {
		rtw_mfree((u8 *)cmdobj, sizeof(struct cmd_obj));
		rtw_mfree((u8 *)pdrvextra_cmd_parm, sizeof(struct drvextra_cmd_parm));
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = AC_PARM_CMD_WK_CID;
	pdrvextra_cmd_parm->type = ac_type;
	pdrvextra_cmd_parm->size = sz;
	pdrvextra_cmd_parm->pbuf = ac_parm_buf;

	_rtw_memcpy(ac_parm_buf, &ac_parm, sz);

	init_h2fwcmd_w_parm_no_rsp(cmdobj, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);
	res = rtw_enqueue_cmd(pcmdpriv, cmdobj);

exit:
	return res;
}
