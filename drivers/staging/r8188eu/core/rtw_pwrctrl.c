// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2012 Realtek Corporation. */

#define _RTW_PWRCTRL_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/osdep_intf.h"
#include "../include/linux/usb.h"

void ips_enter(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct xmit_priv *pxmit_priv = &padapter->xmitpriv;

	if (pxmit_priv->free_xmitbuf_cnt != NR_XMITBUFF ||
	    pxmit_priv->free_xmit_extbuf_cnt != NR_XMIT_EXTBUFF)
		return;

	mutex_lock(&pwrpriv->lock);

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

	mutex_unlock(&pwrpriv->lock);
}

int ips_leave(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int result = _SUCCESS;
	int keyid;

	mutex_lock(&pwrpriv->lock);

	if ((pwrpriv->rf_pwrstate == rf_off) && (!pwrpriv->bips_processing)) {
		pwrpriv->bips_processing = true;
		pwrpriv->change_rfpwrstate = rf_on;
		pwrpriv->ips_leave_cnts++;

		result = rtw_ips_pwr_up(padapter);
		if (result == _SUCCESS) {
			pwrpriv->rf_pwrstate = rf_on;
		}

		if ((_WEP40_ == psecuritypriv->dot11PrivacyAlgrthm) || (_WEP104_ == psecuritypriv->dot11PrivacyAlgrthm)) {
			set_channel_bwmode(padapter, padapter->mlmeextpriv.cur_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);
			for (keyid = 0; keyid < 4; keyid++) {
				if (pmlmepriv->key_mask & BIT(keyid)) {
					if (keyid == psecuritypriv->dot11PrivacyKeyIndex)
						result = rtw_set_key(padapter, psecuritypriv, keyid, 1);
					else
						result = rtw_set_key(padapter, psecuritypriv, keyid, 0);
				}
			}
		}

		pwrpriv->bips_processing = false;

		pwrpriv->bkeepfwalive = false;
		pwrpriv->bpower_saving = false;
	}

	mutex_unlock(&pwrpriv->lock);

	return result;
}

static bool rtw_pwr_unassociated_idle(struct adapter *adapter)
{
	struct adapter *buddy = adapter->pbuddy_adapter;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct wifidirect_info	*pwdinfo = &adapter->wdinfo;
	bool ret = false;

	if (adapter->pwrctrlpriv.ips_deny_time >= jiffies)
		goto exit;

	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE | WIFI_SITE_MONITOR) ||
	    check_fwstate(pmlmepriv, WIFI_UNDER_LINKING | WIFI_UNDER_WPS) ||
	    check_fwstate(pmlmepriv, WIFI_UNDER_WPS) ||
	    check_fwstate(pmlmepriv, WIFI_AP_STATE) ||
	    check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE | WIFI_ADHOC_STATE) ||
	    !rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		goto exit;

	/* consider buddy, if exist */
	if (buddy) {
		struct mlme_priv *b_pmlmepriv = &buddy->mlmepriv;
		struct wifidirect_info *b_pwdinfo = &buddy->wdinfo;

		if (check_fwstate(b_pmlmepriv, WIFI_ASOC_STATE | WIFI_SITE_MONITOR) ||
		    check_fwstate(b_pmlmepriv, WIFI_UNDER_LINKING | WIFI_UNDER_WPS) ||
		    check_fwstate(b_pmlmepriv, WIFI_AP_STATE) ||
		    check_fwstate(b_pmlmepriv, WIFI_ADHOC_MASTER_STATE | WIFI_ADHOC_STATE) ||
		    !rtw_p2p_chk_state(b_pwdinfo, P2P_STATE_NONE))
			goto exit;
	}
	ret = true;

exit:
	return ret;
}

void rtw_ps_processor(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;

	pwrpriv->ps_processing = true;

	if (pwrpriv->bips_processing)
		goto exit;

	if (pwrpriv->ips_mode_req == IPS_NONE)
		goto exit;

	if (!rtw_pwr_unassociated_idle(padapter))
		goto exit;

	if ((pwrpriv->rf_pwrstate == rf_on) && ((pwrpriv->pwr_state_check_cnts % 4) == 0)) {
		pwrpriv->change_rfpwrstate = rf_off;

		ips_enter(padapter);
	}
exit:
	rtw_set_pwr_state_check_timer(&padapter->pwrctrlpriv);
	pwrpriv->ps_processing = false;
}

static void pwr_state_check_handler(struct timer_list *t)
{
	struct adapter *padapter =
		from_timer(padapter, t,
			   pwrctrlpriv.pwr_state_check_timer);
	rtw_ps_cmd(padapter);
}

static bool PS_RDY_CHECK(struct adapter *padapter)
{
	u32 curr_time, delta_time;
	struct pwrctrl_priv	*pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;

	curr_time = jiffies;
	delta_time = curr_time - pwrpriv->DelayLPSLastTimeStamp;

	if (delta_time < LPS_DELAY_TIME)
		return false;

	if (!check_fwstate(pmlmepriv, _FW_LINKED) ||
	    check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) ||
	    check_fwstate(pmlmepriv, WIFI_AP_STATE) ||
	    check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) ||
	    check_fwstate(pmlmepriv, WIFI_ADHOC_STATE))
		return false;
	if (pwrpriv->bInSuspend)
		return false;
	if (padapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X &&
	    !padapter->securitypriv.binstallGrpkey)
		return false;
	return true;
}

void rtw_set_ps_mode(struct adapter *padapter, u8 ps_mode, u8 smart_ps, u8 bcn_ant_mode)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct wifidirect_info	*pwdinfo = &padapter->wdinfo;

	if (ps_mode > PM_Card_Disable)
		return;

	if (pwrpriv->pwr_mode == ps_mode) {
		if (PS_MODE_ACTIVE == ps_mode)
			return;

		if ((pwrpriv->smart_ps == smart_ps) &&
		    (pwrpriv->bcn_ant_mode == bcn_ant_mode))
			return;
	}

	/* if (pwrpriv->pwr_mode == PS_MODE_ACTIVE) */
	if (ps_mode == PS_MODE_ACTIVE) {
		if (pwdinfo->opp_ps == 0) {
			pwrpriv->pwr_mode = ps_mode;
			SetHwReg8188EU(padapter, HW_VAR_H2C_FW_PWRMODE, (u8 *)(&ps_mode));
			pwrpriv->bFwCurrentInPSMode = false;
		}
	} else {
		if (PS_RDY_CHECK(padapter)) {
			pwrpriv->bFwCurrentInPSMode = true;
			pwrpriv->pwr_mode = ps_mode;
			pwrpriv->smart_ps = smart_ps;
			pwrpriv->bcn_ant_mode = bcn_ant_mode;
			SetHwReg8188EU(padapter, HW_VAR_H2C_FW_PWRMODE, (u8 *)(&ps_mode));

			/*  Set CTWindow after LPS */
			if (pwdinfo->opp_ps == 1)
				p2p_ps_wk_cmd(padapter, P2P_PS_ENABLE, 0);
		}
	}

}

static bool lps_rf_on(struct adapter *adapter)
{
	/* When we halt NIC, we should check if FW LPS is leave. */
	if (adapter->pwrctrlpriv.rf_pwrstate == rf_off) {
		/*  If it is in HW/SW Radio OFF or IPS state, we do not check Fw LPS Leave, */
		/*  because Fw is unload. */
		return true;
	}

	if (rtw_read32(adapter, REG_RCR) & 0x00070000)
		return false;

	return true;
}

/*
 * Return:
 *	0:	Leave OK
 *	-1:	Timeout
 *	-2:	Other error
 */
s32 LPS_RF_ON_check(struct adapter *padapter, u32 delay_ms)
{
	u32 start_time;
	s32 err = 0;

	start_time = jiffies;
	while (1) {
		if (lps_rf_on(padapter))
			break;

		if (padapter->bSurpriseRemoved) {
			err = -2;
			break;
		}

		if (rtw_get_passing_time_ms(start_time) > delay_ms) {
			err = -1;
			break;
		}
		rtw_usleep_os(100);
	}

	return err;
}

/*  */
/*	Description: */
/*		Enter the leisure power save mode. */
/*  */
void LPS_Enter(struct adapter *padapter)
{
	struct pwrctrl_priv	*pwrpriv = &padapter->pwrctrlpriv;

	if (!PS_RDY_CHECK(padapter))
		return;

	if (pwrpriv->bLeisurePs) {
		/*  Idle for a while if we connect to AP a while ago. */
		if (pwrpriv->LpsIdleCount >= 2) { /*   4 Sec */
			if (pwrpriv->pwr_mode == PS_MODE_ACTIVE) {
				pwrpriv->bpower_saving = true;
				/* For Tenda W311R IOT issue */
				rtw_set_ps_mode(padapter, pwrpriv->power_mgnt,
						pwrpriv->smart_ps, 0x40);
			}
		} else {
			pwrpriv->LpsIdleCount++;
		}
	}

}

#define LPS_LEAVE_TIMEOUT_MS 100

/*	Description: */
/*		Leave the leisure power save mode. */
void LPS_Leave(struct adapter *padapter)
{
	struct pwrctrl_priv	*pwrpriv = &padapter->pwrctrlpriv;

	if (pwrpriv->bLeisurePs) {
		if (pwrpriv->pwr_mode != PS_MODE_ACTIVE) {
			rtw_set_ps_mode(padapter, PS_MODE_ACTIVE, 0, 0x40);

			if (pwrpriv->pwr_mode == PS_MODE_ACTIVE)
				LPS_RF_ON_check(padapter, LPS_LEAVE_TIMEOUT_MS);
		}
	}

	pwrpriv->bpower_saving = false;

}

/*  */
/*  Description: Leave all power save mode: LPS, FwLPS, IPS if needed. */
/*  Move code to function by tynli. 2010.03.26. */
/*  */
void LeaveAllPowerSaveMode(struct adapter *Adapter)
{
	struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
	u8	enqueue = 0;

	if (check_fwstate(pmlmepriv, _FW_LINKED)) { /* connect */
		p2p_ps_wk_cmd(Adapter, P2P_PS_DISABLE, enqueue);

		rtw_lps_ctrl_wk_cmd(Adapter, LPS_CTRL_LEAVE, enqueue);
	}

}

void rtw_init_pwrctrl_priv(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;

	mutex_init(&pwrctrlpriv->lock);
	pwrctrlpriv->rf_pwrstate = rf_on;
	pwrctrlpriv->ips_enter_cnts = 0;
	pwrctrlpriv->ips_leave_cnts = 0;
	pwrctrlpriv->bips_processing = false;

	pwrctrlpriv->ips_mode = padapter->registrypriv.ips_mode;
	pwrctrlpriv->ips_mode_req = padapter->registrypriv.ips_mode;

	pwrctrlpriv->pwr_state_check_interval = RTW_PWR_STATE_CHK_INTERVAL;
	pwrctrlpriv->pwr_state_check_cnts = 0;
	pwrctrlpriv->bInSuspend = false;
	pwrctrlpriv->bkeepfwalive = false;

	pwrctrlpriv->LpsIdleCount = 0;
	pwrctrlpriv->power_mgnt = padapter->registrypriv.power_mgnt;/*  PS_MODE_MIN; */
	pwrctrlpriv->bLeisurePs = (PS_MODE_ACTIVE != pwrctrlpriv->power_mgnt) ? true : false;

	pwrctrlpriv->bFwCurrentInPSMode = false;

	pwrctrlpriv->pwr_mode = PS_MODE_ACTIVE;
	pwrctrlpriv->smart_ps = padapter->registrypriv.smart_ps;
	pwrctrlpriv->bcn_ant_mode = 0;

	timer_setup(&pwrctrlpriv->pwr_state_check_timer, pwr_state_check_handler, 0);
}

/* Wake the NIC up from: 1)IPS 2)USB autosuspend */
int rtw_pwr_wakeup(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int ret = _SUCCESS;
	u32 start = jiffies;
	u32 ips_deffer_ms;

	/* the ms will prevent from falling into IPS after wakeup */
	ips_deffer_ms = RTW_PWR_STATE_CHK_INTERVAL;

	if (pwrpriv->ips_deny_time < jiffies + rtw_ms_to_systime(ips_deffer_ms))
		pwrpriv->ips_deny_time = jiffies + rtw_ms_to_systime(ips_deffer_ms);

	if (pwrpriv->ps_processing) {
		while (pwrpriv->ps_processing && rtw_get_passing_time_ms(start) <= 3000)
			msleep(10);
	}

	/* System suspend is not allowed to wakeup */
	if (pwrpriv->bInSuspend) {
		while (pwrpriv->bInSuspend &&
		       (rtw_get_passing_time_ms(start) <= 3000 ||
		       (rtw_get_passing_time_ms(start) <= 500)))
				msleep(10);
	}

	/* I think this should be check in IPS, LPS, autosuspend functions... */
	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		ret = _SUCCESS;
		goto exit;
	}
	if (rf_off == pwrpriv->rf_pwrstate) {
		if (_FAIL ==  ips_leave(padapter)) {
			ret = _FAIL;
			goto exit;
		}
	}

	/* TODO: the following checking need to be merged... */
	if (padapter->bDriverStopped || !padapter->bup ||
	    !padapter->hw_init_completed) {
		ret = false;
		goto exit;
	}

exit:
	if (pwrpriv->ips_deny_time < jiffies + rtw_ms_to_systime(ips_deffer_ms))
		pwrpriv->ips_deny_time = jiffies + rtw_ms_to_systime(ips_deffer_ms);
	return ret;
}

int rtw_pm_set_lps(struct adapter *padapter, u8 mode)
{
	int	ret = 0;
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;

	if (mode < PS_MODE_NUM) {
		if (pwrctrlpriv->power_mgnt != mode) {
			if (PS_MODE_ACTIVE == mode)
				LeaveAllPowerSaveMode(padapter);
			else
				pwrctrlpriv->LpsIdleCount = 2;
			pwrctrlpriv->power_mgnt = mode;
			pwrctrlpriv->bLeisurePs = (PS_MODE_ACTIVE != pwrctrlpriv->power_mgnt) ? true : false;
		}
	} else {
		ret = -EINVAL;
	}

	return ret;
}

int rtw_pm_set_ips(struct adapter *padapter, u8 mode)
{
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;

	if (mode == IPS_NORMAL || mode == IPS_LEVEL_2) {
		rtw_ips_mode_req(pwrctrlpriv, mode);
		return 0;
	} else if (mode == IPS_NONE) {
		rtw_ips_mode_req(pwrctrlpriv, mode);
		if ((padapter->bSurpriseRemoved == 0) && (_FAIL == rtw_pwr_wakeup(padapter)))
			return -EFAULT;
	} else {
		return -EINVAL;
	}
	return 0;
}
