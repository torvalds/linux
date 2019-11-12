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

#include <drv_types.h>
#include <hal_data.h>
#ifdef CONFIG_RTW_80211K
#include "rtw_rm_fsm.h"
#include "rtw_rm_util.h"

struct fsm_state {
	u8 *name;
	int(*fsm_func)(struct rm_obj *prm, enum RM_EV_ID evid);
};

static void rm_state_initial(struct rm_obj *prm);
static void rm_state_goto(struct rm_obj *prm, enum RM_STATE rm_state);
static void rm_state_run(struct rm_obj *prm, enum RM_EV_ID evid);
static struct rm_event *rm_dequeue_ev(_queue *queue);
static struct rm_obj *rm_dequeue_rm(_queue *queue);

void rm_timer_callback(void *data)
{
	int i;
	_adapter *padapter = (_adapter *)data;
	struct rm_priv *prmpriv = &padapter->rmpriv;
	struct rm_clock *pclock;


	/* deal with clock */
	for (i=0;i<RM_TIMER_NUM;i++) {
		pclock = &prmpriv->clock[i];
		if (pclock->prm == NULL
			||(ATOMIC_READ(&(pclock->counter)) == 0))
			continue;

		ATOMIC_DEC(&(pclock->counter));

		if (ATOMIC_READ(&(pclock->counter)) == 0)
			rm_post_event(pclock->prm->psta->padapter,
				pclock->prm->rmid, prmpriv->clock[i].evid);
	}
	_set_timer(&prmpriv->rm_timer, CLOCK_UNIT);
}

int rtw_init_rm(_adapter *padapter)
{
	struct rm_priv *prmpriv = &padapter->rmpriv;


	RTW_INFO("RM: %s\n",__func__);
	_rtw_init_queue(&(prmpriv->rm_queue));
	_rtw_init_queue(&(prmpriv->ev_queue));

	/* bit 0-7 */
	prmpriv->rm_en_cap_def[0] = 0
		| BIT(RM_LINK_MEAS_CAP_EN)
		| BIT(RM_NB_REP_CAP_EN)
		/*| BIT(RM_PARAL_MEAS_CAP_EN)*/
		| BIT(RM_REPEAT_MEAS_CAP_EN)
		| BIT(RM_BCN_PASSIVE_MEAS_CAP_EN)
		| BIT(RM_BCN_ACTIVE_MEAS_CAP_EN)
		| BIT(RM_BCN_TABLE_MEAS_CAP_EN)
		/*| BIT(RM_BCN_MEAS_REP_COND_CAP_EN)*/;

	/* bit  8-15 */
	prmpriv->rm_en_cap_def[1] = 0
		/*| BIT(RM_FRAME_MEAS_CAP_EN - 8)*/
#ifdef CONFIG_RTW_ACS
		| BIT(RM_CH_LOAD_CAP_EN - 8)
		| BIT(RM_NOISE_HISTO_CAP_EN - 8)
#endif
		/*| BIT(RM_STATIS_MEAS_CAP_EN - 8)*/
		/*| BIT(RM_LCI_MEAS_CAP_EN - 8)*/
		/*| BIT(RM_LCI_AMIMUTH_CAP_EN - 8)*/
		/*| BIT(RM_TRANS_STREAM_CAT_MEAS_CAP_EN - 8)*/
		/*| BIT(RM_TRIG_TRANS_STREAM_CAT_MEAS_CAP_EN - 8)*/;

	/* bit 16-23 */
	prmpriv->rm_en_cap_def[2] = 0
		/*| BIT(RM_AP_CH_REP_CAP_EN - 16)*/
		/*| BIT(RM_RM_MIB_CAP_EN - 16)*/
		/*| BIT(RM_OP_CH_MAX_MEAS_DUR0 - 16)*/
		/*| BIT(RM_OP_CH_MAX_MEAS_DUR1 - 16)*/
		/*| BIT(RM_OP_CH_MAX_MEAS_DUR2 - 16)*/
		/*| BIT(RM_NONOP_CH_MAX_MEAS_DUR0 - 16)*/
		/*| BIT(RM_NONOP_CH_MAX_MEAS_DUR1 - 16)*/
		/*| BIT(RM_NONOP_CH_MAX_MEAS_DUR2 - 16)*/;

	/* bit 24-31 */
	prmpriv->rm_en_cap_def[3] = 0
		/*| BIT(RM_MEAS_PILOT_CAP0 - 24)*/
		/*| BIT(RM_MEAS_PILOT_CAP1 - 24)*/
		/*| BIT(RM_MEAS_PILOT_CAP2 - 24)*/
		/*| BIT(RM_MEAS_PILOT_TRANS_INFO_CAP_EN - 24)*/
		/*| BIT(RM_NB_REP_TSF_OFFSET_CAP_EN - 24)*/
		| BIT(RM_RCPI_MEAS_CAP_EN - 24)
		| BIT(RM_RSNI_MEAS_CAP_EN - 24)
		/*| BIT(RM_BSS_AVG_ACCESS_DELAY_CAP_EN - 24)*/;

	/* bit 32-39 */
	prmpriv->rm_en_cap_def[4] = 0
		/*| BIT(RM_BSS_AVG_ACCESS_DELAY_CAP_EN - 32)*/
		/*| BIT(RM_AVALB_ADMIS_CAPACITY_CAP_EN - 32)*/
		/*| BIT(RM_ANT_CAP_EN - 32)*/;

	prmpriv->enable = _TRUE;

	/* clock timer */
	rtw_init_timer(&prmpriv->rm_timer,
		padapter, rm_timer_callback, padapter);
	_set_timer(&prmpriv->rm_timer, CLOCK_UNIT);

	return _SUCCESS;
}

int rtw_deinit_rm(_adapter *padapter)
{
	struct rm_priv *prmpriv = &padapter->rmpriv;
	struct rm_obj *prm;
	struct rm_event *pev;


	RTW_INFO("RM: %s\n",__func__);
	prmpriv->enable = _FALSE;
	_cancel_timer_ex(&prmpriv->rm_timer);

	/* free all events and measurements */
	while((pev = rm_dequeue_ev(&prmpriv->ev_queue)) != NULL)
		rtw_mfree((void *)pev, sizeof(struct rm_event));

	while((prm = rm_dequeue_rm(&prmpriv->rm_queue)) != NULL)
		rm_state_run(prm, RM_EV_cancel);

	_rtw_deinit_queue(&(prmpriv->rm_queue));
	_rtw_deinit_queue(&(prmpriv->ev_queue));

	return _SUCCESS;
}

int rtw_free_rm_priv(_adapter *padapter)
{
	return rtw_deinit_rm(padapter);
}

static int rm_enqueue_ev(_queue *queue, struct rm_event *obj, bool to_head)
{
	_irqL irqL;


	if (obj == NULL)
		return _FAIL;

	_enter_critical(&queue->lock, &irqL);

	if (to_head)
		rtw_list_insert_head(&obj->list, &queue->queue);
	else
		rtw_list_insert_tail(&obj->list, &queue->queue);

	_exit_critical(&queue->lock, &irqL);

	return _SUCCESS;
}

static void rm_set_clock(struct rm_obj *prm, u32 ms, enum RM_EV_ID evid)
{
	ATOMIC_SET(&(prm->pclock->counter), (ms/CLOCK_UNIT));
	prm->pclock->evid = evid;
}

static struct rm_clock *rm_alloc_clock(_adapter *padapter, struct rm_obj *prm)
{
	int i;
	struct rm_priv *prmpriv = &padapter->rmpriv;
	struct rm_clock *pclock = NULL;


	for (i=0;i<RM_TIMER_NUM;i++) {
		pclock = &prmpriv->clock[i];

		if (pclock->prm == NULL) {
			pclock->prm = prm;
			ATOMIC_SET(&(pclock->counter), 0);
			pclock->evid = RM_EV_max;
			break;
		}
	}
	return pclock;
}

static void rm_cancel_clock(struct rm_obj *prm)
{
	ATOMIC_SET(&(prm->pclock->counter), 0);
	prm->pclock->evid = RM_EV_max;
}

static void rm_free_clock(struct rm_clock *pclock)
{
	pclock->prm = NULL;
	ATOMIC_SET(&(pclock->counter), 0);
	pclock->evid = RM_EV_max;
}

static int is_list_linked(const struct list_head *head)
{
	return head->prev != NULL;
}

void rm_free_rmobj(struct rm_obj *prm)
{
	if (is_list_linked(&prm->list))
		rtw_list_delete(&prm->list);

	if (prm->q.pssid)
		rtw_mfree(prm->q.pssid, strlen(prm->q.pssid)+1);

	if (prm->q.opt.bcn.req_start)
		rtw_mfree(prm->q.opt.bcn.req_start,
			prm->q.opt.bcn.req_len);

	if (prm->pclock)
		rm_free_clock(prm->pclock);

	rtw_mfree((void *)prm, sizeof(struct rm_obj));
}

struct rm_obj *rm_alloc_rmobj(_adapter *padapter)
{
	struct rm_obj *prm;


	prm = (struct rm_obj *)rtw_malloc(sizeof(struct rm_obj));
	if (prm == NULL)
		return NULL;

	_rtw_memset(prm, 0, sizeof(struct rm_obj));

	/* alloc timer */
	if ((prm->pclock = rm_alloc_clock(padapter, prm)) == NULL) {
		rm_free_rmobj(prm);
		return NULL;
	}
	return prm;
}

int rm_enqueue_rmobj(_adapter *padapter, struct rm_obj *prm, bool to_head)
{
	_irqL irqL;
	struct rm_priv *prmpriv = &padapter->rmpriv;
	_queue *queue = &prmpriv->rm_queue;


	if (prm == NULL)
		return _FAIL;

	_enter_critical(&queue->lock, &irqL);
	if (to_head)
		rtw_list_insert_head(&prm->list, &queue->queue);
	else
		rtw_list_insert_tail(&prm->list, &queue->queue);
	_exit_critical(&queue->lock, &irqL);

	rm_state_initial(prm);

	return _SUCCESS;
}

static struct rm_obj *rm_dequeue_rm(_queue *queue)
{
	_irqL irqL;
	struct rm_obj *prm;


	_enter_critical(&queue->lock, &irqL);
	if (rtw_is_list_empty(&(queue->queue)))
		prm = NULL;
	else {
		prm = LIST_CONTAINOR(get_next(&(queue->queue)),
			struct rm_obj, list);
		/* rtw_list_delete(&prm->list); */
	}
	_exit_critical(&queue->lock, &irqL);

	return prm;
}

static struct rm_event *rm_dequeue_ev(_queue *queue)
{
	_irqL irqL;
	struct rm_event *ev;


	_enter_critical(&queue->lock, &irqL);
	if (rtw_is_list_empty(&(queue->queue)))
		ev = NULL;
	else {
		ev = LIST_CONTAINOR(get_next(&(queue->queue)),
			struct rm_event, list);
		rtw_list_delete(&ev->list);
	}
	_exit_critical(&queue->lock, &irqL);

	return ev;
}

static struct rm_obj *_rm_get_rmobj(_queue *queue, u32 rmid)
{
	_irqL irqL;
	_list *phead, *plist;
	struct rm_obj *prm = NULL;


	if (rmid == 0)
		return NULL;

	_enter_critical(&queue->lock, &irqL);

	phead = get_list_head(queue);
	plist = get_next(phead);
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {

		prm = LIST_CONTAINOR(plist, struct rm_obj, list);
		if (rmid == (prm->rmid)) {
			_exit_critical(&queue->lock, &irqL);
			return prm;
		}
		plist = get_next(plist);
	}
	_exit_critical(&queue->lock, &irqL);

	return NULL;
}

struct sta_info *rm_get_psta(_adapter *padapter, u32 rmid)
{
	struct rm_priv *prmpriv = &padapter->rmpriv;
	struct rm_obj *prm;


	prm = _rm_get_rmobj(&prmpriv->rm_queue, rmid);

	if (prm)
		return prm->psta;

	return NULL;
}

struct rm_obj *rm_get_rmobj(_adapter *padapter, u32 rmid)
{
	struct rm_priv *prmpriv = &padapter->rmpriv;

	return _rm_get_rmobj(&prmpriv->rm_queue, rmid);
}

u8 rtw_rm_post_envent_cmd(_adapter *padapter, u32 rmid, u8 evid)
{
	struct cmd_obj *pcmd;
	struct rm_event *pev;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;


	pcmd = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (pcmd == NULL) {
		res = _FAIL;
		goto exit;
	}
	pev = (struct rm_event*)rtw_zmalloc(sizeof(struct rm_event));

	if (pev == NULL) {
		rtw_mfree((u8 *) pcmd, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}
	pev->rmid = rmid;
	pev->evid = evid;

	init_h2fwcmd_w_parm_no_rsp(pcmd, pev, GEN_CMD_CODE(_RM_POST_EVENT));
	res = rtw_enqueue_cmd(pcmdpriv, pcmd);
exit:
	return res;
}

int rm_post_event(_adapter *padapter, u32 rmid, enum RM_EV_ID evid)
{
	if (padapter->rmpriv.enable == _FALSE)
		return _FALSE;

	RTW_INFO("RM: post asyn %s to rmid=%x\n", rm_event_name(evid), rmid);
	rtw_rm_post_envent_cmd(padapter, rmid, evid);
	return _SUCCESS;
}

int _rm_post_event(_adapter *padapter, u32 rmid, enum RM_EV_ID evid)
{
	struct rm_priv *prmpriv = &padapter->rmpriv;
	struct rm_event *pev;

	if (evid >= RM_EV_max || rmid == 0)
		return _FALSE;

	pev = (struct rm_event *)rtw_malloc(sizeof(struct rm_event));
	if (pev == NULL)
		return _FALSE;

	pev->rmid = rmid;
	pev->evid = evid;

	RTW_INFO("RM: post sync %s to rmid=%x\n", rm_event_name(evid), rmid);
	rm_enqueue_ev(&prmpriv->ev_queue, pev, FALSE);

	return _SUCCESS;
}

static void rm_bcast_aid_handler(_adapter *padapter, struct rm_event *pev)
{
	_irqL irqL;
	_list *phead, *plist;
	_queue *queue = &padapter->rmpriv.rm_queue;
	struct rm_obj *prm;


	_enter_critical(&queue->lock, &irqL);
	phead = get_list_head(queue);
	plist = get_next(phead);
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {

		prm = LIST_CONTAINOR(plist, struct rm_obj, list);
		plist = get_next(plist);
		if (RM_GET_AID(pev->rmid) == RM_GET_AID(prm->rmid)) {
			_exit_critical(&queue->lock, &irqL);
			rm_state_run(prm, pev->evid);
			_enter_critical(&queue->lock, &irqL);
		}
	}
	_exit_critical(&queue->lock, &irqL);
	return;
}

/* main handler of RM (Resource Management) */
void rm_handler(_adapter *padapter, struct rm_event *pe)
{
	int i;
	struct rm_priv *prmpriv = &padapter->rmpriv;
	struct rm_obj *prm;
	struct rm_event *pev;


	/* dequeue event */
	while((pev = rm_dequeue_ev(&prmpriv->ev_queue)) != NULL)
	{
		if (RM_IS_ID_FOR_ALL(pev->rmid)) {
			/* apply to all aid mateched measurement */
			rm_bcast_aid_handler(padapter, pev);
			rtw_mfree((void *)pev, sizeof(struct rm_event));
			continue;
		}

		/* retrieve rmobj */
		prm = _rm_get_rmobj(&prmpriv->rm_queue, pev->rmid);
		if (prm == NULL) {
			RTW_ERR("RM: rmid=%x event=%s doesn't find rm obj\n",
				pev->rmid, rm_event_name(pev->evid));
			rtw_mfree((void *)pev, sizeof(struct rm_event));
			return;
		}
		/* run state machine */
		rm_state_run(prm, pev->evid);
		rtw_mfree((void *)pev, sizeof(struct rm_event));
	}
}

static int rm_issue_meas_req(struct rm_obj *prm)
{
	switch (prm->q.action_code) {
	case RM_ACT_RADIO_MEAS_REQ:
		switch (prm->q.m_type) {
		case bcn_req:
		case ch_load_req:
		case noise_histo_req:
			issue_radio_meas_req(prm);
			break;
		default:
			break;
		} /* meas_type */
		break;
	case RM_ACT_NB_REP_REQ:
		/* issue neighbor request */
		issue_nb_req(prm);
		break;
	case RM_ACT_LINK_MEAS_REQ:
		issue_link_meas_req(prm);
		break;
	default:
		return _FALSE;
	} /* action_code */

	return _SUCCESS;
}

/*
* RM state machine
*/

static int rm_state_idle(struct rm_obj *prm, enum RM_EV_ID evid)
{
	_adapter *padapter = prm->psta->padapter;
	u8 val8;
	u32 val32;


	prm->p.category = RTW_WLAN_CATEGORY_RADIO_MEAS;

	switch (evid) {
	case RM_EV_state_in:
		switch (prm->q.action_code) {
		case RM_ACT_RADIO_MEAS_REQ:
			/* copy attrib from meas_req to meas_rep */
			prm->p.action_code = RM_ACT_RADIO_MEAS_REP;
			prm->p.diag_token = prm->q.diag_token;
			prm->p.e_id = _MEAS_RSP_IE_;
			prm->p.m_token = prm->q.m_token;
			prm->p.m_type = prm->q.m_type;
			prm->p.rpt = prm->q.rpt;
			prm->p.ch_num = prm->q.ch_num;
			prm->p.op_class = prm->q.op_class;

			if (prm->q.m_type == ch_load_req
				|| prm->q.m_type == noise_histo_req) {
				/*
				 * phydm measure current ch periodically
				 * scan current ch is not necessary
				 */
				val8 = padapter->mlmeextpriv.cur_channel;
				if (prm->q.ch_num == val8)
					prm->poll_mode = 1;
			}
			RTW_INFO("RM: rmid=%x %s switch in repeat=%u\n",
				prm->rmid, rm_type_req_name(prm->q.m_type),
				prm->q.rpt);
			break;
		case RM_ACT_NB_REP_REQ:
			prm->p.action_code = RM_ACT_NB_REP_RESP;
			RTW_INFO("RM: rmid=%x Neighbor request switch in\n",
				prm->rmid);
			break;
		case RM_ACT_LINK_MEAS_REQ:
			prm->p.diag_token = prm->q.diag_token;
			prm->p.action_code = RM_ACT_LINK_MEAS_REP;
			RTW_INFO("RM: rmid=%x Link meas switch in\n",
				prm->rmid);
			break;
		default:
			prm->p.action_code = prm->q.action_code;
			rm_set_rep_mode(prm, MEAS_REP_MOD_INCAP);
			RTW_INFO("RM: rmid=%x recv unknown action %d\n",
				prm->rmid,prm->p.action_code);
			break;
		} /* switch() */

		if (prm->rmid & RM_MASTER) {
			if (rm_issue_meas_req(prm) == _SUCCESS)
				rm_state_goto(prm, RM_ST_WAIT_MEAS);
			else
				rm_state_goto(prm, RM_ST_END);
			return _SUCCESS;
		} else {
			rm_state_goto(prm, RM_ST_DO_MEAS);
			return _SUCCESS;
		}

		if (prm->p.m_mode) {
			issue_null_reply(prm);
			rm_state_goto(prm, RM_ST_END);
			return _SUCCESS;
		}
		if (prm->q.rand_intvl) {
			/* get low tsf to generate random interval */
			val32 = rtw_read32(padapter, REG_TSFTR);
			val32 = val32 % prm->q.rand_intvl;
			RTW_INFO("RM: rmid=%x rand_intval=%d, rand=%d\n",
				prm->rmid, (int)prm->q.rand_intvl,val32);
			rm_set_clock(prm, prm->q.rand_intvl,
				RM_EV_delay_timer_expire);
			return _SUCCESS;
		}
		break;
	case RM_EV_delay_timer_expire:
		rm_state_goto(prm, RM_ST_DO_MEAS);
		break;
	case RM_EV_cancel:
		rm_state_goto(prm, RM_ST_END);
		break;
	case RM_EV_state_out:
		rm_cancel_clock(prm);
		break;
	default:
		break;
	}
	return _SUCCESS;
}

/* we do the measuring */
static int rm_state_do_meas(struct rm_obj *prm, enum RM_EV_ID evid)
{
	_adapter *padapter = prm->psta->padapter;
	u8 val8;
	u64 val64;


	switch (evid) {
	case RM_EV_state_in:
		if (prm->q.action_code == RM_ACT_RADIO_MEAS_REQ) {
			switch (prm->q.m_type) {
			case bcn_req:
				if (prm->q.m_mode == bcn_req_bcn_table) {
					RTW_INFO("RM: rmid=%x Beacon table\n",
						prm->rmid);
					_rm_post_event(padapter, prm->rmid,
						RM_EV_survey_done);
					return _SUCCESS;
				}
				break;
			case ch_load_req:
			case noise_histo_req:
				if (prm->poll_mode)
					_rm_post_event(padapter, prm->rmid,
						RM_EV_survey_done);
				return _SUCCESS;
			default:
				rm_state_goto(prm, RM_ST_END);
				return _SUCCESS;
			}

			if (!ready_for_scan(prm)) {
				prm->wait_busy = RM_BUSY_TRAFFIC_TIMES;
				RTW_INFO("RM: wait busy traffic - %d\n",
					prm->wait_busy);
				rm_set_clock(prm, RM_WAIT_BUSY_TIMEOUT,
					RM_EV_busy_timer_expire);
				return _SUCCESS;
			}
		} else if (prm->q.action_code == RM_ACT_LINK_MEAS_REQ) {
			; /* do nothing */
			rm_state_goto(prm, RM_ST_SEND_REPORT);
			return _SUCCESS;
		}
		_rm_post_event(padapter, prm->rmid, RM_EV_start_meas);
		break;
	case RM_EV_start_meas:
		if (prm->q.action_code == RM_ACT_RADIO_MEAS_REQ) {
			/* resotre measurement start time */
			prm->meas_start_time = rtw_hal_get_tsftr_by_port(padapter
									, rtw_hal_get_port(padapter));

			switch (prm->q.m_type) {
			case bcn_req:
				val8 = 1; /* Enable free run counter */
				rtw_hal_set_hwreg(padapter,
					HW_VAR_FREECNT, &val8);
				rm_sitesurvey(prm);
				break;
			case ch_load_req:
			case noise_histo_req:
				rm_sitesurvey(prm);
				break;
			default:
				rm_state_goto(prm, RM_ST_END);
				return _SUCCESS;
				break;
			}
		}
		/* handle measurement timeout */
		rm_set_clock(prm, RM_MEAS_TIMEOUT, RM_EV_meas_timer_expire);
		break;
	case RM_EV_survey_done:
		if (prm->q.action_code == RM_ACT_RADIO_MEAS_REQ) {
			switch (prm->q.m_type) {
			case bcn_req:
				rm_cancel_clock(prm);
				rm_state_goto(prm, RM_ST_SEND_REPORT);
				return _SUCCESS;
			case ch_load_req:
			case noise_histo_req:
				retrieve_radio_meas_result(prm);

				if (rm_radio_meas_report_cond(prm) == _SUCCESS)
					rm_state_goto(prm, RM_ST_SEND_REPORT);
				else
					rm_set_clock(prm, RM_COND_INTVL,
						RM_EV_retry_timer_expire);
				break;
			default:
				rm_state_goto(prm, RM_ST_END);
				return _SUCCESS;
			}
		}
		break;
	case RM_EV_meas_timer_expire:
		RTW_INFO("RM: rmid=%x measurement timeount\n",prm->rmid);
		rm_set_rep_mode(prm, MEAS_REP_MOD_REFUSE);
		issue_null_reply(prm);
		rm_state_goto(prm, RM_ST_END);
		break;
	case RM_EV_busy_timer_expire:
		if (!ready_for_scan(prm) && prm->wait_busy--) {
			RTW_INFO("RM: wait busy - %d\n",prm->wait_busy);
			rm_set_clock(prm, RM_WAIT_BUSY_TIMEOUT,
				RM_EV_busy_timer_expire);
			break;
		}
		else if (prm->wait_busy <= 0) {
			RTW_INFO("RM: wait busy timeout\n");
			rm_set_rep_mode(prm, MEAS_REP_MOD_REFUSE);
			issue_null_reply(prm);
			rm_state_goto(prm, RM_ST_END);
			return _SUCCESS;
		}
		_rm_post_event(padapter, prm->rmid, RM_EV_start_meas);
		break;
	case RM_EV_request_timer_expire:
		rm_set_rep_mode(prm, MEAS_REP_MOD_REFUSE);
		issue_null_reply(prm);
		rm_state_goto(prm, RM_ST_END);
		break;
	case RM_EV_retry_timer_expire:
		/* expired due to meas condition mismatch, meas again */
		_rm_post_event(padapter, prm->rmid, RM_EV_start_meas);
		break;
	case RM_EV_cancel:
		rm_set_rep_mode(prm, MEAS_REP_MOD_REFUSE);
		issue_null_reply(prm);
		rm_state_goto(prm, RM_ST_END);
		break;
	case RM_EV_state_out:
		rm_cancel_clock(prm);
		/* resotre measurement end time */
		prm->meas_end_time = rtw_hal_get_tsftr_by_port(padapter
								, rtw_hal_get_port(padapter));

		val8 = 0; /* Disable free run counter */
		rtw_hal_set_hwreg(padapter, HW_VAR_FREECNT, &val8);
		break;
	default:
		break;
	}

	return _SUCCESS;
}

static int rm_state_wait_meas(struct rm_obj *prm, enum RM_EV_ID evid)
{
	u8 val8;
	u64 val64;


	switch (evid) {
	case RM_EV_state_in:
		/* we create meas_req, waiting for peer report */
		rm_set_clock(prm, RM_REQ_TIMEOUT,
			RM_EV_request_timer_expire);
		break;
	case RM_EV_recv_rep:
		rm_state_goto(prm, RM_ST_RECV_REPORT);
		break;
	case RM_EV_request_timer_expire:
	case RM_EV_cancel:
		rm_state_goto(prm, RM_ST_END);
		break;
	case RM_EV_state_out:
		rm_cancel_clock(prm);
		break;
	default:
		break;
	}
	return _SUCCESS;
}

static int rm_state_send_report(struct rm_obj *prm, enum RM_EV_ID evid)
{
	u8 val8;


	switch (evid) {
	case RM_EV_state_in:
		/* we have to issue report */
		if (prm->q.action_code == RM_ACT_RADIO_MEAS_REQ) {
			switch (prm->q.m_type) {
			case bcn_req:
				issue_beacon_rep(prm);
				break;
			case ch_load_req:
			case noise_histo_req:
				issue_radio_meas_rep(prm);
				break;
			default:
				rm_state_goto(prm, RM_ST_END);
				return _SUCCESS;
			}

		} else if (prm->q.action_code == RM_ACT_LINK_MEAS_REQ) {
			issue_link_meas_rep(prm);
			rm_state_goto(prm, RM_ST_END);
			return _SUCCESS;

		} else {
			rm_state_goto(prm, RM_ST_END);
			return _SUCCESS;
		}

		/* check repeat */
		if (prm->p.rpt) {
			RTW_INFO("RM: rmid=%x repeat=%u/%u\n",
				prm->rmid, prm->p.rpt,
				prm->q.rpt);
			prm->p.rpt--;
			/*
			* we recv meas_req,
			* delay for a wihile and than meas again
			*/
			if (prm->poll_mode)
				rm_set_clock(prm, RM_REPT_POLL_INTVL,
					RM_EV_repeat_delay_expire);
			else
				rm_set_clock(prm, RM_REPT_SCAN_INTVL,
					RM_EV_repeat_delay_expire);
			return _SUCCESS;
		}
		/* we are done */
		rm_state_goto(prm, RM_ST_END);
		break;
	case RM_EV_repeat_delay_expire:
		rm_state_goto(prm, RM_ST_DO_MEAS);
		break;
	case RM_EV_cancel:
		rm_state_goto(prm, RM_ST_END);
		break;
	case RM_EV_state_out:
		rm_cancel_clock(prm);
		break;
	default:
		break;
	}
	return _SUCCESS;
}

static int rm_state_recv_report(struct rm_obj *prm, enum RM_EV_ID evid)
{
	u8 val8;


	switch (evid) {
	case RM_EV_state_in:
		/* we issue meas_req, got peer's meas report */
		switch (prm->p.action_code) {
		case RM_ACT_RADIO_MEAS_REP:
			/* check refuse, incapable and repeat */
			val8 = prm->p.m_mode;
			if (val8) {
				RTW_INFO("RM: rmid=%x peer reject (%s repeat=%d)\n",
					prm->rmid,
					val8|MEAS_REP_MOD_INCAP?"INCAP":
					val8|MEAS_REP_MOD_REFUSE?"REFUSE":
					val8|MEAS_REP_MOD_LATE?"LATE":"",
					prm->p.rpt);
				rm_state_goto(prm, RM_ST_END);
				return _SUCCESS;
			}
			break;
		case RM_ACT_NB_REP_RESP:
			/* report to upper layer if needing */
			rm_state_goto(prm, RM_ST_END);
			return _SUCCESS;
		default:
			rm_state_goto(prm, RM_ST_END);
			return _SUCCESS;
		}
		/* check repeat */
		if (prm->p.rpt) {
			RTW_INFO("RM: rmid=%x repeat=%u/%u\n",
				prm->rmid, prm->p.rpt,
				prm->q.rpt);
			prm->p.rpt--;
			/* waitting more report */
			rm_state_goto(prm, RM_ST_WAIT_MEAS);
			break;
		}
		/* we are done */
		rm_state_goto(prm, RM_ST_END);
		break;
	case RM_EV_cancel:
		rm_state_goto(prm, RM_ST_END);
		break;
	case RM_EV_state_out:
		rm_cancel_clock(prm);
		break;
	default:
		break;
	}
	return _SUCCESS;
}

static int rm_state_end(struct rm_obj *prm, enum RM_EV_ID evid)
{
	switch (evid) {
	case RM_EV_state_in:
		_rm_post_event(prm->psta->padapter, prm->rmid, RM_EV_state_out);
		break;

	case RM_EV_cancel:
	case RM_EV_state_out:
	default:
		rm_free_rmobj(prm);
		break;
	}
	return _SUCCESS;
}

struct fsm_state rm_fsm[] = {
	{"RM_ST_IDLE",		rm_state_idle},
	{"RM_ST_DO_MEAS",	rm_state_do_meas},
	{"RM_ST_WAIT_MEAS", 	rm_state_wait_meas},
	{"RM_ST_SEND_REPORT", 	rm_state_send_report},
	{"RM_ST_RECV_REPORT", 	rm_state_recv_report},
	{"RM_ST_END", 		rm_state_end}
};

char *rm_state_name(enum RM_STATE state)
{
	return rm_fsm[state].name;
}

char *rm_event_name(enum RM_EV_ID evid)
{
	switch(evid) {
	case RM_EV_state_in:
		return "RM_EV_state_in";
	case RM_EV_busy_timer_expire:
		return "RM_EV_busy_timer_expire";
	case RM_EV_delay_timer_expire:
		return "RM_EV_delay_timer_expire";
	case RM_EV_meas_timer_expire:
		return "RM_EV_meas_timer_expire";
	case RM_EV_repeat_delay_expire:
		return "RM_EV_repeat_delay_expire";
	case RM_EV_retry_timer_expire:
		return "RM_EV_retry_timer_expire";
	case RM_EV_request_timer_expire:
		return "RM_EV_request_timer_expire";
	case RM_EV_wait_report:
		return "RM_EV_wait_report";
	case RM_EV_start_meas:
		return "RM_EV_start_meas";
	case RM_EV_survey_done:
		return "RM_EV_survey_done";
	case RM_EV_recv_rep:
		return "RM_EV_recv_report";
	case RM_EV_cancel:
		return "RM_EV_cancel";
	case RM_EV_state_out:
		return "RM_EV_state_out";
	case RM_EV_max:
		return "RM_EV_max";
	default:
		return "RM_EV_unknown";
	}
	return "UNKNOWN";
}

static void rm_state_initial(struct rm_obj *prm)
{
	prm->state = RM_ST_IDLE;

	RTW_INFO("\n");
	RTW_INFO("RM: rmid=%x %-18s -> %s\n",prm->rmid,
		"new measurement", rm_fsm[prm->state].name);

	rm_post_event(prm->psta->padapter, prm->rmid, RM_EV_state_in);
}

static void rm_state_run(struct rm_obj *prm, enum RM_EV_ID evid)
{
	RTW_INFO("RM: rmid=%x %-18s    %s\n",prm->rmid,
		rm_fsm[prm->state].name,rm_event_name(evid));

	rm_fsm[prm->state].fsm_func(prm, evid);
}

static void rm_state_goto(struct rm_obj *prm, enum RM_STATE rm_state)
{
	if (prm->state == rm_state)
		return;

	rm_state_run(prm, RM_EV_state_out);

	RTW_INFO("\n");
	RTW_INFO("RM: rmid=%x %-18s -> %s\n",prm->rmid,
		rm_fsm[prm->state].name, rm_fsm[rm_state].name);

	prm->state = rm_state;
	rm_state_run(prm, RM_EV_state_in);
}
#endif /* CONFIG_RTW_80211K */
