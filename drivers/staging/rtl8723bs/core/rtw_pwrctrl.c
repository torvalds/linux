// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#include <drv_types.h>
#include <hal_data.h>
#include <linux/jiffies.h>

void _ips_enter(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);

	pwrpriv->bips_processing = true;

	/*  syn ips_mode with request */
	pwrpriv->ips_mode = pwrpriv->ips_mode_req;

	pwrpriv->ips_enter_cnts++;

	if (rf_off == pwrpriv->change_rfpwrstate) {
		pwrpriv->bpower_saving = true;

		if (pwrpriv->ips_mode == IPS_LEVEL_2)
			pwrpriv->bkeepfwalive = true;

		rtw_ips_pwr_down(padapter);
		pwrpriv->rf_pwrstate = rf_off;
	}
	pwrpriv->bips_processing = false;

}

void ips_enter(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);


	hal_btcoex_IpsNotify(padapter, pwrpriv->ips_mode_req);

	mutex_lock(&pwrpriv->lock);
	_ips_enter(padapter);
	mutex_unlock(&pwrpriv->lock);
}

int _ips_leave(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	int result = _SUCCESS;

	if ((pwrpriv->rf_pwrstate == rf_off) && (!pwrpriv->bips_processing)) {
		pwrpriv->bips_processing = true;
		pwrpriv->change_rfpwrstate = rf_on;
		pwrpriv->ips_leave_cnts++;

		result = rtw_ips_pwr_up(padapter);
		if (result == _SUCCESS)
			pwrpriv->rf_pwrstate = rf_on;
		pwrpriv->bips_processing = false;

		pwrpriv->bkeepfwalive = false;
		pwrpriv->bpower_saving = false;
	}

	return result;
}

int ips_leave(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	int ret;

	mutex_lock(&pwrpriv->lock);
	ret = _ips_leave(padapter);
	mutex_unlock(&pwrpriv->lock);

	if (ret == _SUCCESS)
		hal_btcoex_IpsNotify(padapter, IPS_NONE);

	return ret;
}

static bool rtw_pwr_unassociated_idle(struct adapter *adapter)
{
	struct adapter *buddy = adapter->pbuddy_adapter;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct xmit_priv *pxmit_priv = &adapter->xmitpriv;

	bool ret = false;

	if (adapter_to_pwrctl(adapter)->bpower_saving)
		goto exit;

	if (time_before(jiffies, adapter_to_pwrctl(adapter)->ips_deny_time))
		goto exit;

	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE|WIFI_SITE_MONITOR)
		|| check_fwstate(pmlmepriv, WIFI_UNDER_LINKING|WIFI_UNDER_WPS)
		|| check_fwstate(pmlmepriv, WIFI_AP_STATE)
		|| check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE|WIFI_ADHOC_STATE)
	)
		goto exit;

	/* consider buddy, if exist */
	if (buddy) {
		struct mlme_priv *b_pmlmepriv = &(buddy->mlmepriv);

		if (check_fwstate(b_pmlmepriv, WIFI_ASOC_STATE|WIFI_SITE_MONITOR)
			|| check_fwstate(b_pmlmepriv, WIFI_UNDER_LINKING|WIFI_UNDER_WPS)
			|| check_fwstate(b_pmlmepriv, WIFI_AP_STATE)
			|| check_fwstate(b_pmlmepriv, WIFI_ADHOC_MASTER_STATE|WIFI_ADHOC_STATE)
		)
			goto exit;
	}

	if (pxmit_priv->free_xmitbuf_cnt != NR_XMITBUFF ||
		pxmit_priv->free_xmit_extbuf_cnt != NR_XMIT_EXTBUFF) {
		netdev_dbg(adapter->pnetdev,
			   "There are some pkts to transmit\n");
		netdev_dbg(adapter->pnetdev,
			   "free_xmitbuf_cnt: %d, free_xmit_extbuf_cnt: %d\n",
			   pxmit_priv->free_xmitbuf_cnt,
			   pxmit_priv->free_xmit_extbuf_cnt);
		goto exit;
	}

	ret = true;

exit:
	return ret;
}


/*
 * ATTENTION:
 *rtw_ps_processor() doesn't handle LPS.
 */
void rtw_ps_processor(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	u32 ps_deny = 0;

	mutex_lock(&adapter_to_pwrctl(padapter)->lock);
	ps_deny = rtw_ps_deny_get(padapter);
	mutex_unlock(&adapter_to_pwrctl(padapter)->lock);
	if (ps_deny != 0)
		goto exit;

	if (pwrpriv->bInSuspend) {/* system suspend or autosuspend */
		pdbgpriv->dbg_ps_insuspend_cnt++;
		return;
	}

	pwrpriv->ps_processing = true;

	if (pwrpriv->ips_mode_req == IPS_NONE)
		goto exit;

	if (!rtw_pwr_unassociated_idle(padapter))
		goto exit;

	if ((pwrpriv->rf_pwrstate == rf_on) && ((pwrpriv->pwr_state_check_cnts%4) == 0)) {
		pwrpriv->change_rfpwrstate = rf_off;
		{
			ips_enter(padapter);
		}
	}
exit:
	pwrpriv->ps_processing = false;
}

static void pwr_state_check_handler(struct timer_list *t)
{
	struct pwrctrl_priv *pwrctrlpriv =
		timer_container_of(pwrctrlpriv, t, pwr_state_check_timer);
	struct adapter *padapter = pwrctrlpriv->adapter;

	rtw_ps_cmd(padapter);
}

void traffic_check_for_leave_lps(struct adapter *padapter, u8 tx, u32 tx_packets)
{
	static unsigned long start_time;
	static u32 xmit_cnt;
	u8 bLeaveLPS = false;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;



	if (tx) { /* from tx */
		xmit_cnt += tx_packets;

		if (start_time == 0)
			start_time = jiffies;

		if (jiffies_to_msecs(jiffies - start_time) > 2000) { /*  2 sec == watch dog timer */
			if (xmit_cnt > 8) {
				if (adapter_to_pwrctl(padapter)->bLeisurePs
				    && (adapter_to_pwrctl(padapter)->pwr_mode != PS_MODE_ACTIVE)
				    && !(hal_btcoex_IsBtControlLps(padapter))) {
					bLeaveLPS = true;
				}
			}

			start_time = jiffies;
			xmit_cnt = 0;
		}

	} else { /*  from rx path */
		if (pmlmepriv->LinkDetectInfo.NumRxUnicastOkInPeriod > 4/*2*/) {
			if (adapter_to_pwrctl(padapter)->bLeisurePs
			    && (adapter_to_pwrctl(padapter)->pwr_mode != PS_MODE_ACTIVE)
			    && !(hal_btcoex_IsBtControlLps(padapter)))
				bLeaveLPS = true;
		}
	}

	if (bLeaveLPS)
		/* rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_LEAVE, 1); */
		rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_LEAVE, tx?0:1);
}

/*
 * Description:
 *This function MUST be called under power lock protect
 *
 * Parameters
 *padapter
 *pslv			power state level, only could be PS_STATE_S0 ~ PS_STATE_S4
 *
 */
void rtw_set_rpwm(struct adapter *padapter, u8 pslv)
{
	u8 rpwm;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	u8 cpwm_orig;

	pslv = PS_STATE(pslv);

	if (!pwrpriv->brpwmtimeout) {
		if (pwrpriv->rpwm == pslv ||
		    (pwrpriv->rpwm >= PS_STATE_S2 && pslv >= PS_STATE_S2))
			return;

	}

	if ((padapter->bSurpriseRemoved) || !(padapter->hw_init_completed)) {
		pwrpriv->cpwm = PS_STATE_S4;

		return;
	}

	if (padapter->bDriverStopped) {
		if (pslv < PS_STATE_S2)
			return;
	}

	rpwm = pslv | pwrpriv->tog;
	/*  only when from PS_STATE S0/S1 to S2 and higher needs ACK */
	if ((pwrpriv->cpwm < PS_STATE_S2) && (pslv >= PS_STATE_S2))
		rpwm |= PS_ACK;

	pwrpriv->rpwm = pslv;

	cpwm_orig = 0;
	if (rpwm & PS_ACK)
		rtw_hal_get_hwreg(padapter, HW_VAR_CPWM, &cpwm_orig);

	if (rpwm & PS_ACK)
		_set_timer(&pwrpriv->pwr_rpwm_timer, LPS_RPWM_WAIT_MS);
	rtw_hal_set_hwreg(padapter, HW_VAR_SET_RPWM, (u8 *)(&rpwm));

	pwrpriv->tog += 0x80;

	/*  No LPS 32K, No Ack */
	if (rpwm & PS_ACK) {
		unsigned long start_time;
		u8 cpwm_now;

		start_time = jiffies;

		/*  polling cpwm */
		do {
			mdelay(1);
			rtw_hal_get_hwreg(padapter, HW_VAR_CPWM, &cpwm_now);
			if ((cpwm_orig ^ cpwm_now) & 0x80) {
				pwrpriv->cpwm = PS_STATE_S4;
				pwrpriv->cpwm_tog = cpwm_now & PS_TOGGLE;
				break;
			}

			if (jiffies_to_msecs(jiffies - start_time) > LPS_RPWM_WAIT_MS) {
				_set_timer(&pwrpriv->pwr_rpwm_timer, 1);
				break;
			}
		} while (1);
	} else
		pwrpriv->cpwm = pslv;
}

static u8 PS_RDY_CHECK(struct adapter *padapter)
{
	unsigned long curr_time, delta_time;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	if (pwrpriv->bInSuspend)
		return false;

	curr_time = jiffies;

	delta_time = curr_time - pwrpriv->DelayLPSLastTimeStamp;

	if (delta_time < LPS_DELAY_TIME)
		return false;

	if (check_fwstate(pmlmepriv, WIFI_SITE_MONITOR)
		|| check_fwstate(pmlmepriv, WIFI_UNDER_LINKING|WIFI_UNDER_WPS)
		|| check_fwstate(pmlmepriv, WIFI_AP_STATE)
		|| check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE|WIFI_ADHOC_STATE)
		|| rtw_is_scan_deny(padapter)
	)
		return false;

	if (padapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X &&
	    !padapter->securitypriv.binstallGrpkey)
		return false;

	if (!rtw_cfg80211_pwr_mgmt(padapter))
		return false;

	return true;
}

void rtw_set_ps_mode(struct adapter *padapter, u8 ps_mode, u8 smart_ps, u8 bcn_ant_mode, const char *msg)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);

	if (ps_mode > PM_Card_Disable)
		return;

	if (pwrpriv->pwr_mode == ps_mode)
		if (ps_mode == PS_MODE_ACTIVE)
			return;


	mutex_lock(&pwrpriv->lock);

	/* if (pwrpriv->pwr_mode == PS_MODE_ACTIVE) */
	if (ps_mode == PS_MODE_ACTIVE) {
		if (!(hal_btcoex_IsBtControlLps(padapter))
				|| (hal_btcoex_IsBtControlLps(padapter)
					&& !(hal_btcoex_IsLpsOn(padapter)))) {
			pwrpriv->pwr_mode = ps_mode;
			rtw_set_rpwm(padapter, PS_STATE_S4);

			rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_PWRMODE, (u8 *)(&ps_mode));
			pwrpriv->fw_current_in_ps_mode = false;

			hal_btcoex_LpsNotify(padapter, ps_mode);
		}
	} else {
		if ((PS_RDY_CHECK(padapter) && check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE)) ||
		    ((hal_btcoex_IsBtControlLps(padapter)) && (hal_btcoex_IsLpsOn(padapter)))
			) {
			u8 pslv;

			hal_btcoex_LpsNotify(padapter, ps_mode);

			pwrpriv->fw_current_in_ps_mode = true;
			pwrpriv->pwr_mode = ps_mode;
			pwrpriv->smart_ps = smart_ps;
			pwrpriv->bcn_ant_mode = bcn_ant_mode;
			rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_PWRMODE, (u8 *)(&ps_mode));

			pslv = PS_STATE_S2;
			if (pwrpriv->alives == 0)
				pslv = PS_STATE_S0;

			if (!(hal_btcoex_IsBtDisabled(padapter)) &&
			    (hal_btcoex_IsBtControlLps(padapter))) {
				u8 val8;

				val8 = hal_btcoex_LpsVal(padapter);
				if (val8 & BIT(4))
					pslv = PS_STATE_S2;
			}

			rtw_set_rpwm(padapter, pslv);
		}
	}

	mutex_unlock(&pwrpriv->lock);
}

/*
 * Return:
 *0:	Leave OK
 *-1:	Timeout
 *-2:	Other error
 */
s32 LPS_RF_ON_check(struct adapter *padapter, u32 delay_ms)
{
	unsigned long start_time;
	u8 bAwake = false;
	s32 err = 0;


	start_time = jiffies;
	while (1) {
		rtw_hal_get_hwreg(padapter, HW_VAR_FWLPS_RF_ON, &bAwake);
		if (bAwake)
			break;

		if (padapter->bSurpriseRemoved) {
			err = -2;
			break;
		}

		if (jiffies_to_msecs(jiffies - start_time) > delay_ms) {
			err = -1;
			break;
		}
		msleep(1);
	}

	return err;
}

/* Description: Enter the leisure power save mode. */
void LPS_Enter(struct adapter *padapter, const char *msg)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(dvobj);
	int n_assoc_iface = 0;
	char buf[32] = {0};

	if (hal_btcoex_IsBtControlLps(padapter))
		return;

	/* Skip lps enter request if number of associated adapters is not 1 */
	if (check_fwstate(&(dvobj->padapters->mlmepriv), WIFI_ASOC_STATE))
		n_assoc_iface++;
	if (n_assoc_iface != 1)
		return;

	if (!PS_RDY_CHECK(dvobj->padapters))
		return;

	if (pwrpriv->bLeisurePs) {
		/*  Idle for a while if we connect to AP a while ago. */
		if (pwrpriv->LpsIdleCount >= 2) { /*   4 Sec */
			if (pwrpriv->pwr_mode == PS_MODE_ACTIVE) {
				scnprintf(buf, sizeof(buf), "WIFI-%s", msg);
				pwrpriv->bpower_saving = true;
				rtw_set_ps_mode(padapter, pwrpriv->power_mgnt, padapter->registrypriv.smart_ps, 0, buf);
			}
		} else
			pwrpriv->LpsIdleCount++;
	}
}

/* Description: Leave the leisure power save mode. */
void LPS_Leave(struct adapter *padapter, const char *msg)
{
#define LPS_LEAVE_TIMEOUT_MS 100

	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(dvobj);
	char buf[32] = {0};

	if (hal_btcoex_IsBtControlLps(padapter))
		return;

	if (pwrpriv->bLeisurePs) {
		if (pwrpriv->pwr_mode != PS_MODE_ACTIVE) {
			scnprintf(buf, sizeof(buf), "WIFI-%s", msg);
			rtw_set_ps_mode(padapter, PS_MODE_ACTIVE, 0, 0, buf);

			if (pwrpriv->pwr_mode == PS_MODE_ACTIVE)
				LPS_RF_ON_check(padapter, LPS_LEAVE_TIMEOUT_MS);
		}
	}

	pwrpriv->bpower_saving = false;
}

void LeaveAllPowerSaveModeDirect(struct adapter *Adapter)
{
	struct adapter *pri_padapter = GET_PRIMARY_ADAPTER(Adapter);
	struct mlme_priv *pmlmepriv = &(Adapter->mlmepriv);
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(Adapter);

	if (Adapter->bSurpriseRemoved)
		return;

	if (check_fwstate(pmlmepriv, _FW_LINKED)) { /* connect */

		if (pwrpriv->pwr_mode == PS_MODE_ACTIVE)
			return;

		mutex_lock(&pwrpriv->lock);

		rtw_set_rpwm(Adapter, PS_STATE_S4);

		mutex_unlock(&pwrpriv->lock);

		rtw_lps_ctrl_wk_cmd(pri_padapter, LPS_CTRL_LEAVE, 0);
	} else {
		if (pwrpriv->rf_pwrstate == rf_off)
			ips_leave(pri_padapter);
	}
}

/*  */
/*  Description: Leave all power save mode: LPS, FwLPS, IPS if needed. */
/*  Move code to function by tynli. 2010.03.26. */
/*  */
void LeaveAllPowerSaveMode(struct adapter *Adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(Adapter);
	u8 enqueue = 0;
	int n_assoc_iface = 0;

	if (!Adapter->bup)
		return;

	if (Adapter->bSurpriseRemoved)
		return;

	if (check_fwstate(&(dvobj->padapters->mlmepriv), WIFI_ASOC_STATE))
		n_assoc_iface++;

	if (n_assoc_iface) { /* connect */
		enqueue = 1;

		rtw_lps_ctrl_wk_cmd(Adapter, LPS_CTRL_LEAVE, enqueue);

		LPS_Leave_check(Adapter);
	} else {
		if (adapter_to_pwrctl(Adapter)->rf_pwrstate == rf_off)
			ips_leave(Adapter);
	}
}

void LPS_Leave_check(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv;
	unsigned long	start_time;
	u8 bReady;

	pwrpriv = adapter_to_pwrctl(padapter);

	bReady = false;
	start_time = jiffies;

	cond_resched();

	while (1) {
		mutex_lock(&pwrpriv->lock);

		if (padapter->bSurpriseRemoved ||
		    !(padapter->hw_init_completed) ||
		    (pwrpriv->pwr_mode == PS_MODE_ACTIVE))
			bReady = true;

		mutex_unlock(&pwrpriv->lock);

		if (bReady)
			break;

		if (jiffies_to_msecs(jiffies - start_time) > 100)
			break;

		msleep(1);
	}
}

/*
 * Caller:ISR handler...
 *
 * This will be called when CPWM interrupt is up.
 *
 * using to update cpwn of drv; and drv willl make a decision to up or down pwr level
 */
void cpwm_int_hdl(struct adapter *padapter, struct reportpwrstate_parm *preportpwrstate)
{
	struct pwrctrl_priv *pwrpriv;

	pwrpriv = adapter_to_pwrctl(padapter);

	mutex_lock(&pwrpriv->lock);

	if (pwrpriv->rpwm < PS_STATE_S2)
		goto exit;

	pwrpriv->cpwm = PS_STATE(preportpwrstate->state);
	pwrpriv->cpwm_tog = preportpwrstate->state & PS_TOGGLE;

	if (pwrpriv->cpwm >= PS_STATE_S2) {
		if (pwrpriv->alives & CMD_ALIVE)
			complete(&padapter->cmdpriv.cmd_queue_comp);

		if (pwrpriv->alives & XMIT_ALIVE)
			complete(&padapter->xmitpriv.xmit_comp);
	}

exit:
	mutex_unlock(&pwrpriv->lock);

}

static void cpwm_event_callback(struct work_struct *work)
{
	struct pwrctrl_priv *pwrpriv = container_of(work, struct pwrctrl_priv, cpwm_event);
	struct dvobj_priv *dvobj = pwrctl_to_dvobj(pwrpriv);
	struct adapter *adapter = dvobj->if1;
	struct reportpwrstate_parm report;

	report.state = PS_STATE_S2;
	cpwm_int_hdl(adapter, &report);
}

static void rpwmtimeout_workitem_callback(struct work_struct *work)
{
	struct adapter *padapter;
	struct dvobj_priv *dvobj;
	struct pwrctrl_priv *pwrpriv;


	pwrpriv = container_of(work, struct pwrctrl_priv, rpwmtimeoutwi);
	dvobj = pwrctl_to_dvobj(pwrpriv);
	padapter = dvobj->if1;

	mutex_lock(&pwrpriv->lock);
	if ((pwrpriv->rpwm == pwrpriv->cpwm) || (pwrpriv->cpwm >= PS_STATE_S2))
		goto exit;

	mutex_unlock(&pwrpriv->lock);

	if (rtw_read8(padapter, 0x100) != 0xEA) {
		struct reportpwrstate_parm report;

		report.state = PS_STATE_S2;
		cpwm_int_hdl(padapter, &report);

		return;
	}

	mutex_lock(&pwrpriv->lock);

	if ((pwrpriv->rpwm == pwrpriv->cpwm) || (pwrpriv->cpwm >= PS_STATE_S2))
		goto exit;

	pwrpriv->brpwmtimeout = true;
	rtw_set_rpwm(padapter, pwrpriv->rpwm);
	pwrpriv->brpwmtimeout = false;

exit:
	mutex_unlock(&pwrpriv->lock);
}

/*
 * This function is a timer handler, can't do any IO in it.
 */
static void pwr_rpwm_timeout_handler(struct timer_list *t)
{
	struct pwrctrl_priv *pwrpriv = timer_container_of(pwrpriv, t,
							  pwr_rpwm_timer);

	if ((pwrpriv->rpwm == pwrpriv->cpwm) || (pwrpriv->cpwm >= PS_STATE_S2))
		return;

	_set_workitem(&pwrpriv->rpwmtimeoutwi);
}

static inline void register_task_alive(struct pwrctrl_priv *pwrctrl, u32 tag)
{
	pwrctrl->alives |= tag;
}

static inline void unregister_task_alive(struct pwrctrl_priv *pwrctrl, u32 tag)
{
	pwrctrl->alives &= ~tag;
}


/*
 * Description:
 *Check if the fw_pwrstate is okay for I/O.
 *If not (cpwm is less than S2), then the sub-routine
 *will raise the cpwm to be greater than or equal to S2.
 *
 *Calling Context: Passive
 *
 *Constraint:
 *	1. this function will request pwrctrl->lock
 *
 * Return Value:
 *_SUCCESS	hardware is ready for I/O
 *_FAIL		can't I/O right now
 */
s32 rtw_register_task_alive(struct adapter *padapter, u32 task)
{
	s32 res;
	struct pwrctrl_priv *pwrctrl;
	u8 pslv;

	res = _SUCCESS;
	pwrctrl = adapter_to_pwrctl(padapter);
	pslv = PS_STATE_S2;

	mutex_lock(&pwrctrl->lock);

	register_task_alive(pwrctrl, task);

	if (pwrctrl->fw_current_in_ps_mode) {
		if (pwrctrl->cpwm < pslv) {
			if (pwrctrl->cpwm < PS_STATE_S2)
				res = _FAIL;
			if (pwrctrl->rpwm < pslv)
				rtw_set_rpwm(padapter, pslv);
		}
	}

	mutex_unlock(&pwrctrl->lock);

	if (res == _FAIL)
		if (pwrctrl->cpwm >= PS_STATE_S2)
			res = _SUCCESS;

	return res;
}

/*
 * Description:
 *If task is done, call this func. to power down firmware again.
 *
 *Constraint:
 *	1. this function will request pwrctrl->lock
 *
 * Return Value:
 *none
 */
void rtw_unregister_task_alive(struct adapter *padapter, u32 task)
{
	struct pwrctrl_priv *pwrctrl;
	u8 pslv;

	pwrctrl = adapter_to_pwrctl(padapter);
	pslv = PS_STATE_S0;

	if (!(hal_btcoex_IsBtDisabled(padapter)) && hal_btcoex_IsBtControlLps(padapter)) {
		u8 val8;

		val8 = hal_btcoex_LpsVal(padapter);
		if (val8 & BIT(4))
			pslv = PS_STATE_S2;
	}

	mutex_lock(&pwrctrl->lock);

	unregister_task_alive(pwrctrl, task);

	if ((pwrctrl->pwr_mode != PS_MODE_ACTIVE) && pwrctrl->fw_current_in_ps_mode) {
		if (pwrctrl->cpwm > pslv)
			if ((pslv >= PS_STATE_S2) || (pwrctrl->alives == 0))
				rtw_set_rpwm(padapter, pslv);

	}

	mutex_unlock(&pwrctrl->lock);
}

/*
 * Caller: rtw_xmit_thread
 *
 * Check if the fw_pwrstate is okay for xmit.
 * If not (cpwm is less than S3), then the sub-routine
 * will raise the cpwm to be greater than or equal to S3.
 *
 * Calling Context: Passive
 *
 * Return Value:
 * _SUCCESS	rtw_xmit_thread can write fifo/txcmd afterwards.
 * _FAIL		rtw_xmit_thread can not do anything.
 */
s32 rtw_register_tx_alive(struct adapter *padapter)
{
	s32 res;
	struct pwrctrl_priv *pwrctrl;
	u8 pslv;

	res = _SUCCESS;
	pwrctrl = adapter_to_pwrctl(padapter);
	pslv = PS_STATE_S2;

	mutex_lock(&pwrctrl->lock);

	register_task_alive(pwrctrl, XMIT_ALIVE);

	if (pwrctrl->fw_current_in_ps_mode) {
		if (pwrctrl->cpwm < pslv) {
			if (pwrctrl->cpwm < PS_STATE_S2)
				res = _FAIL;
			if (pwrctrl->rpwm < pslv)
				rtw_set_rpwm(padapter, pslv);
		}
	}

	mutex_unlock(&pwrctrl->lock);

	if (res == _FAIL)
		if (pwrctrl->cpwm >= PS_STATE_S2)
			res = _SUCCESS;

	return res;
}

/*
 * Caller: rtw_cmd_thread
 *
 * Check if the fw_pwrstate is okay for issuing cmd.
 * If not (cpwm should be is less than S2), then the sub-routine
 * will raise the cpwm to be greater than or equal to S2.
 *
 * Calling Context: Passive
 *
 * Return Value:
 *_SUCCESS	rtw_cmd_thread can issue cmds to firmware afterwards.
 *_FAIL		rtw_cmd_thread can not do anything.
 */
s32 rtw_register_cmd_alive(struct adapter *padapter)
{
	s32 res;
	struct pwrctrl_priv *pwrctrl;
	u8 pslv;

	res = _SUCCESS;
	pwrctrl = adapter_to_pwrctl(padapter);
	pslv = PS_STATE_S2;

	mutex_lock(&pwrctrl->lock);

	register_task_alive(pwrctrl, CMD_ALIVE);

	if (pwrctrl->fw_current_in_ps_mode) {
		if (pwrctrl->cpwm < pslv) {
			if (pwrctrl->cpwm < PS_STATE_S2)
				res = _FAIL;
			if (pwrctrl->rpwm < pslv)
				rtw_set_rpwm(padapter, pslv);
		}
	}

	mutex_unlock(&pwrctrl->lock);

	if (res == _FAIL)
		if (pwrctrl->cpwm >= PS_STATE_S2)
			res = _SUCCESS;

	return res;
}

/*
 * Caller: ISR
 *
 * If ISR's txdone,
 * No more pkts for TX,
 * Then driver shall call this fun. to power down firmware again.
 */
void rtw_unregister_tx_alive(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrctrl;
	u8 pslv;

	pwrctrl = adapter_to_pwrctl(padapter);
	pslv = PS_STATE_S0;

	if (!(hal_btcoex_IsBtDisabled(padapter)) && hal_btcoex_IsBtControlLps(padapter)) {
		u8 val8;

		val8 = hal_btcoex_LpsVal(padapter);
		if (val8 & BIT(4))
			pslv = PS_STATE_S2;
	}

	mutex_lock(&pwrctrl->lock);

	unregister_task_alive(pwrctrl, XMIT_ALIVE);

	if ((pwrctrl->pwr_mode != PS_MODE_ACTIVE) && pwrctrl->fw_current_in_ps_mode) {
		if (pwrctrl->cpwm > pslv)
			if ((pslv >= PS_STATE_S2) || (pwrctrl->alives == 0))
				rtw_set_rpwm(padapter, pslv);
	}

	mutex_unlock(&pwrctrl->lock);
}

/*
 * Caller: ISR
 *
 * If all commands have been done,
 * and no more command to do,
 * then driver shall call this fun. to power down firmware again.
 */
void rtw_unregister_cmd_alive(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrctrl;
	u8 pslv;

	pwrctrl = adapter_to_pwrctl(padapter);
	pslv = PS_STATE_S0;

	if (!(hal_btcoex_IsBtDisabled(padapter)) && hal_btcoex_IsBtControlLps(padapter)) {
		u8 val8;

		val8 = hal_btcoex_LpsVal(padapter);
		if (val8 & BIT(4))
			pslv = PS_STATE_S2;
	}

	mutex_lock(&pwrctrl->lock);

	unregister_task_alive(pwrctrl, CMD_ALIVE);

	if ((pwrctrl->pwr_mode != PS_MODE_ACTIVE) && pwrctrl->fw_current_in_ps_mode) {
		if (pwrctrl->cpwm > pslv) {
			if ((pslv >= PS_STATE_S2) || (pwrctrl->alives == 0))
				rtw_set_rpwm(padapter, pslv);
		}
	}

	mutex_unlock(&pwrctrl->lock);
}

void rtw_init_pwrctrl_priv(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	mutex_init(&pwrctrlpriv->lock);
	pwrctrlpriv->rf_pwrstate = rf_on;
	pwrctrlpriv->ips_enter_cnts = 0;
	pwrctrlpriv->ips_leave_cnts = 0;
	pwrctrlpriv->bips_processing = false;

	pwrctrlpriv->ips_mode = padapter->registrypriv.ips_mode;
	pwrctrlpriv->ips_mode_req = padapter->registrypriv.ips_mode;

	pwrctrlpriv->pwr_state_check_interval = RTW_PWR_STATE_CHK_INTERVAL;
	pwrctrlpriv->pwr_state_check_cnts = 0;
	pwrctrlpriv->bInternalAutoSuspend = false;
	pwrctrlpriv->bInSuspend = false;
	pwrctrlpriv->bkeepfwalive = false;

	pwrctrlpriv->LpsIdleCount = 0;
	pwrctrlpriv->power_mgnt = padapter->registrypriv.power_mgnt;/*  PS_MODE_MIN; */
	pwrctrlpriv->bLeisurePs = pwrctrlpriv->power_mgnt != PS_MODE_ACTIVE;

	pwrctrlpriv->fw_current_in_ps_mode = false;

	pwrctrlpriv->rpwm = 0;
	pwrctrlpriv->cpwm = PS_STATE_S4;

	pwrctrlpriv->pwr_mode = PS_MODE_ACTIVE;
	pwrctrlpriv->smart_ps = padapter->registrypriv.smart_ps;
	pwrctrlpriv->bcn_ant_mode = 0;
	pwrctrlpriv->dtim = 0;

	pwrctrlpriv->tog = 0x80;

	rtw_hal_set_hwreg(padapter, HW_VAR_SET_RPWM, (u8 *)(&pwrctrlpriv->rpwm));

	_init_workitem(&pwrctrlpriv->cpwm_event, cpwm_event_callback, NULL);

	pwrctrlpriv->brpwmtimeout = false;
	pwrctrlpriv->adapter = padapter;
	_init_workitem(&pwrctrlpriv->rpwmtimeoutwi, rpwmtimeout_workitem_callback, NULL);
	timer_setup(&pwrctrlpriv->pwr_rpwm_timer, pwr_rpwm_timeout_handler, 0);
	timer_setup(&pwrctrlpriv->pwr_state_check_timer,
		    pwr_state_check_handler, 0);

	pwrctrlpriv->wowlan_mode = false;
	pwrctrlpriv->wowlan_ap_mode = false;
}

void rtw_free_pwrctrl_priv(struct adapter *adapter)
{
}

inline void rtw_set_ips_deny(struct adapter *padapter, u32 ms)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	pwrpriv->ips_deny_time = jiffies + msecs_to_jiffies(ms);
}

/*
* rtw_pwr_wakeup - Wake the NIC up from: 1)IPS. 2)USB autosuspend
* @adapter: pointer to struct adapter structure
* @ips_deffer_ms: the ms will prevent from falling into IPS after wakeup
* Return _SUCCESS or _FAIL
*/

int _rtw_pwr_wakeup(struct adapter *padapter, u32 ips_deffer_ms, const char *caller)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(dvobj);
	struct mlme_priv *pmlmepriv;
	int ret = _SUCCESS;
	unsigned long start = jiffies;
	unsigned long deny_time = jiffies + msecs_to_jiffies(ips_deffer_ms);

	/* for LPS */
	LeaveAllPowerSaveMode(padapter);

	/* IPS still bound with primary adapter */
	padapter = GET_PRIMARY_ADAPTER(padapter);
	pmlmepriv = &padapter->mlmepriv;

	if (time_before(pwrpriv->ips_deny_time, deny_time))
		pwrpriv->ips_deny_time = deny_time;


	if (pwrpriv->ps_processing)
		while (pwrpriv->ps_processing && jiffies_to_msecs(jiffies - start) <= 3000)
			mdelay(10);

	if (!(pwrpriv->bInternalAutoSuspend) && pwrpriv->bInSuspend)
		while (pwrpriv->bInSuspend && jiffies_to_msecs(jiffies - start) <= 3000
		)
			mdelay(10);

	/* System suspend is not allowed to wakeup */
	if (!(pwrpriv->bInternalAutoSuspend) && pwrpriv->bInSuspend) {
		ret = _FAIL;
		goto exit;
	}

	/* block??? */
	if (pwrpriv->bInternalAutoSuspend  && padapter->net_closed) {
		ret = _FAIL;
		goto exit;
	}

	/* I think this should be check in IPS, LPS, autosuspend functions... */
	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		ret = _SUCCESS;
		goto exit;
	}

	if (rf_off == pwrpriv->rf_pwrstate) {
		{
			if (ips_leave(padapter) == _FAIL) {
				ret = _FAIL;
				goto exit;
			}
		}
	}

	/* TODO: the following checking need to be merged... */
	if (padapter->bDriverStopped || !padapter->bup || !padapter->hw_init_completed) {
		ret = false;
		goto exit;
	}

exit:
	deny_time = jiffies + msecs_to_jiffies(ips_deffer_ms);
	if (time_before(pwrpriv->ips_deny_time, deny_time))
		pwrpriv->ips_deny_time = deny_time;
	return ret;

}

int rtw_pm_set_lps(struct adapter *padapter, u8 mode)
{
	int	ret = 0;
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	if (mode < PS_MODE_NUM) {
		if (pwrctrlpriv->power_mgnt != mode) {
			if (mode == PS_MODE_ACTIVE)
				LeaveAllPowerSaveMode(padapter);
			else
				pwrctrlpriv->LpsIdleCount = 2;

			pwrctrlpriv->power_mgnt = mode;
			pwrctrlpriv->bLeisurePs =
				pwrctrlpriv->power_mgnt != PS_MODE_ACTIVE;
		}
	} else
		ret = -EINVAL;

	return ret;
}

int rtw_pm_set_ips(struct adapter *padapter, u8 mode)
{
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	if (mode == IPS_NORMAL || mode == IPS_LEVEL_2) {
		rtw_ips_mode_req(pwrctrlpriv, mode);
		return 0;
	} else if (mode == IPS_NONE) {
		rtw_ips_mode_req(pwrctrlpriv, mode);
		if ((padapter->bSurpriseRemoved == 0) && (rtw_pwr_wakeup(padapter) == _FAIL))
			return -EFAULT;
	} else
		return -EINVAL;

	return 0;
}

/*
 * ATTENTION:
 *This function will request pwrctrl LOCK!
 */
void rtw_ps_deny(struct adapter *padapter, enum ps_deny_reason reason)
{
	struct pwrctrl_priv *pwrpriv;

	pwrpriv = adapter_to_pwrctl(padapter);

	mutex_lock(&pwrpriv->lock);
	pwrpriv->ps_deny |= BIT(reason);
	mutex_unlock(&pwrpriv->lock);
}

/*
 * ATTENTION:
 *This function will request pwrctrl LOCK!
 */
void rtw_ps_deny_cancel(struct adapter *padapter, enum ps_deny_reason reason)
{
	struct pwrctrl_priv *pwrpriv;

	pwrpriv = adapter_to_pwrctl(padapter);

	mutex_lock(&pwrpriv->lock);
	pwrpriv->ps_deny &= ~BIT(reason);
	mutex_unlock(&pwrpriv->lock);
}

/*
 * ATTENTION:
 *Before calling this function pwrctrl lock should be occupied already,
 *otherwise it may return incorrect value.
 */
u32 rtw_ps_deny_get(struct adapter *padapter)
{
	return adapter_to_pwrctl(padapter)->ps_deny;
}
