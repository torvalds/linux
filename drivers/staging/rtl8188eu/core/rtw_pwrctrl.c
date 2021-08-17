// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _RTW_PWRCTRL_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <osdep_intf.h>
#include <usb_ops_linux.h>
#include <linux/usb.h>

static int rtw_hw_suspend(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct net_device *pnetdev = padapter->pnetdev;

	if ((!padapter->bup) || (padapter->bDriverStopped) ||
	    (padapter->bSurpriseRemoved))
		goto error_exit;

	/* system suspend */
	LeaveAllPowerSaveMode(padapter);

	mutex_lock(&pwrpriv->mutex_lock);
	pwrpriv->bips_processing = true;
	/* s1. */
	if (pnetdev) {
		netif_carrier_off(pnetdev);
		netif_tx_stop_all_queues(pnetdev);
	}

	/* s2. */
	rtw_disassoc_cmd(padapter, 500, false);

	/* s2-2.  indicate disconnect to os */
	{
		struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;

		if (check_fwstate(pmlmepriv, _FW_LINKED)) {
			_clr_fwstate_(pmlmepriv, _FW_LINKED);

			led_control_8188eu(padapter, LED_CTL_NO_LINK);

			rtw_os_indicate_disconnect(padapter);

			/* donnot enqueue cmd */
			rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_DISCONNECT, 0);
		}
	}
	/* s2-3. */
	rtw_free_assoc_resources(padapter);

	/* s2-4. */
	rtw_free_network_queue(padapter, true);
	rtw_ips_dev_unload(padapter);
	pwrpriv->rf_pwrstate = rf_off;
	pwrpriv->bips_processing = false;

	mutex_unlock(&pwrpriv->mutex_lock);

	return 0;

error_exit:
	return -1;
}

static int rtw_hw_resume(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct net_device *pnetdev = padapter->pnetdev;

	/* system resume */
	mutex_lock(&pwrpriv->mutex_lock);
	pwrpriv->bips_processing = true;
	rtw_reset_drv_sw(padapter);

	if (ips_netdrv_open(netdev_priv(pnetdev)) != _SUCCESS) {
		mutex_unlock(&pwrpriv->mutex_lock);
		goto error_exit;
	}

	netif_device_attach(pnetdev);
	netif_carrier_on(pnetdev);

	if (!netif_queue_stopped(pnetdev))
		netif_start_queue(pnetdev);
	else
		netif_wake_queue(pnetdev);

	pwrpriv->bkeepfwalive = false;
	pwrpriv->brfoffbyhw = false;

	pwrpriv->rf_pwrstate = rf_on;
	pwrpriv->bips_processing = false;

	mutex_unlock(&pwrpriv->mutex_lock);

	return 0;
error_exit:
	return -1;
}

void ips_enter(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct xmit_priv *pxmit_priv = &padapter->xmitpriv;

	if (padapter->registrypriv.mp_mode == 1)
		return;

	if (pxmit_priv->free_xmitbuf_cnt != NR_XMITBUFF ||
	    pxmit_priv->free_xmit_extbuf_cnt != NR_XMIT_EXTBUFF)
		return;

	mutex_lock(&pwrpriv->mutex_lock);

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

	mutex_unlock(&pwrpriv->mutex_lock);
}

int ips_leave(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int result = _SUCCESS;
	int keyid;

	mutex_lock(&pwrpriv->mutex_lock);

	if ((pwrpriv->rf_pwrstate == rf_off) && (!pwrpriv->bips_processing)) {
		pwrpriv->bips_processing = true;
		pwrpriv->change_rfpwrstate = rf_on;
		pwrpriv->ips_leave_cnts++;

		result = rtw_ips_pwr_up(padapter);
		if (result == _SUCCESS)
			pwrpriv->rf_pwrstate = rf_on;

		if ((psecuritypriv->dot11PrivacyAlgrthm == _WEP40_) || (psecuritypriv->dot11PrivacyAlgrthm == _WEP104_)) {
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

	mutex_unlock(&pwrpriv->mutex_lock);

	return result;
}

static bool rtw_pwr_unassociated_idle(struct adapter *adapter)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

	if (time_after_eq(adapter->pwrctrlpriv.ips_deny_time, jiffies))
		return false;

	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE | WIFI_SITE_MONITOR) ||
	    check_fwstate(pmlmepriv, WIFI_UNDER_LINKING | WIFI_UNDER_WPS) ||
	    check_fwstate(pmlmepriv, WIFI_AP_STATE) ||
	    check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE | WIFI_ADHOC_STATE))
		return false;

	return true;
}

void rtw_ps_processor(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	enum rt_rf_power_state rfpwrstate;

	pwrpriv->ps_processing = true;

	if (pwrpriv->bips_processing)
		goto exit;

	if (padapter->pwrctrlpriv.bHWPwrPindetect) {
		rfpwrstate = RfOnOffDetect(padapter);

		if (rfpwrstate != pwrpriv->rf_pwrstate) {
			if (rfpwrstate == rf_off) {
				pwrpriv->change_rfpwrstate = rf_off;
				pwrpriv->brfoffbyhw = true;
				rtw_hw_suspend(padapter);
			} else {
				pwrpriv->change_rfpwrstate = rf_on;
				rtw_hw_resume(padapter);
			}
		}
		pwrpriv->pwr_state_check_cnts++;
	}

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

/*
 *
 * Parameters
 *	padapter
 *	pslv			power state level, only could be PS_STATE_S0 ~ PS_STATE_S4
 *
 */
void rtw_set_rpwm(struct adapter *padapter, u8 pslv)
{
	u8 rpwm;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;

	pslv = PS_STATE(pslv);

	if (pwrpriv->btcoex_rfon) {
		if (pslv < PS_STATE_S4)
			pslv = PS_STATE_S3;
	}

	if (pwrpriv->rpwm == pslv)
		return;

	if ((padapter->bSurpriseRemoved) ||
	    (!padapter->hw_init_completed)) {
		pwrpriv->cpwm = PS_STATE_S4;

		return;
	}

	if (padapter->bDriverStopped) {
		if (pslv < PS_STATE_S2)
			return;
	}

	rpwm = pslv | pwrpriv->tog;

	pwrpriv->rpwm = pslv;

	rtw_hal_set_hwreg(padapter, HW_VAR_SET_RPWM, (u8 *)(&rpwm));

	pwrpriv->tog += 0x80;
	pwrpriv->cpwm = pslv;
}

static u8 PS_RDY_CHECK(struct adapter *padapter)
{
	unsigned long curr_time, delta_time;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	curr_time = jiffies;
	delta_time = curr_time - pwrpriv->DelayLPSLastTimeStamp;

	if (delta_time < LPS_DELAY_TIME)
		return false;

	if ((!check_fwstate(pmlmepriv, _FW_LINKED)) ||
	    (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY)) ||
	    (check_fwstate(pmlmepriv, WIFI_AP_STATE)) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)))
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

	if (ps_mode > PM_Card_Disable)
		return;

	if (pwrpriv->pwr_mode == ps_mode) {
		if (ps_mode == PS_MODE_ACTIVE)
			return;

		if ((pwrpriv->smart_ps == smart_ps) &&
		    (pwrpriv->bcn_ant_mode == bcn_ant_mode))
			return;
	}

	/* if (pwrpriv->pwr_mode == PS_MODE_ACTIVE) */
	if (ps_mode == PS_MODE_ACTIVE) {
		if (PS_RDY_CHECK(padapter)) {
			pwrpriv->bFwCurrentInPSMode = true;
			pwrpriv->pwr_mode = ps_mode;
			pwrpriv->smart_ps = smart_ps;
			pwrpriv->bcn_ant_mode = bcn_ant_mode;
			rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_PWRMODE, (u8 *)(&ps_mode));
			rtw_set_rpwm(padapter, PS_STATE_S2);
		}
	}
}

/*
 * Return:
 *	0:	Leave OK
 *	-1:	Timeout
 *	-2:	Other error
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

/*  */
/*	Description: */
/*		Enter the leisure power save mode. */
/*  */
void LPS_Enter(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;

	if (!PS_RDY_CHECK(padapter))
		return;

	if (pwrpriv->bLeisurePs) {
		/*  Idle for a while if we connect to AP a while ago. */
		if (pwrpriv->LpsIdleCount >= 2) { /*   4 Sec */
			if (pwrpriv->pwr_mode == PS_MODE_ACTIVE) {
				pwrpriv->bpower_saving = true;
				/* For Tenda W311R IOT issue */
				rtw_set_ps_mode(padapter, pwrpriv->power_mgnt, pwrpriv->smart_ps, 0);
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
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;

	if (pwrpriv->bLeisurePs) {
		if (pwrpriv->pwr_mode != PS_MODE_ACTIVE) {
			rtw_set_ps_mode(padapter, PS_MODE_ACTIVE, 0, 0);

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
	struct mlme_priv *pmlmepriv = &Adapter->mlmepriv;

	if (check_fwstate(pmlmepriv, _FW_LINKED))
		rtw_lps_ctrl_wk_cmd(Adapter, LPS_CTRL_LEAVE, 0);
}

void rtw_init_pwrctrl_priv(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;

	mutex_init(&pwrctrlpriv->mutex_lock);
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
	if (padapter->registrypriv.mp_mode == 1)
		pwrctrlpriv->power_mgnt = PS_MODE_ACTIVE;
	else
		pwrctrlpriv->power_mgnt = padapter->registrypriv.power_mgnt;/*  PS_MODE_MIN; */
	pwrctrlpriv->bLeisurePs = (pwrctrlpriv->power_mgnt != PS_MODE_ACTIVE);

	pwrctrlpriv->bFwCurrentInPSMode = false;

	pwrctrlpriv->rpwm = 0;
	pwrctrlpriv->cpwm = PS_STATE_S4;

	pwrctrlpriv->pwr_mode = PS_MODE_ACTIVE;
	pwrctrlpriv->smart_ps = padapter->registrypriv.smart_ps;
	pwrctrlpriv->bcn_ant_mode = 0;

	pwrctrlpriv->tog = 0x80;

	pwrctrlpriv->btcoex_rfon = false;

	timer_setup(&pwrctrlpriv->pwr_state_check_timer,
		    pwr_state_check_handler, 0);
}

/*
 * rtw_pwr_wakeup - Wake the NIC up from: 1)IPS. 2)USB autosuspend
 * @adapter: pointer to struct adapter structure
 * @ips_deffer_ms: the ms will prevent from falling into IPS after wakeup
 * Return _SUCCESS or _FAIL
 */

int _rtw_pwr_wakeup(struct adapter *padapter, u32 ips_deffer_ms, const char *caller)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	unsigned long expires;
	unsigned long start;
	int ret = _SUCCESS;

	expires = jiffies + msecs_to_jiffies(ips_deffer_ms);
	if (time_before(pwrpriv->ips_deny_time, expires))
		pwrpriv->ips_deny_time = jiffies + msecs_to_jiffies(ips_deffer_ms);

	start = jiffies;
	if (pwrpriv->ps_processing) {
		while (pwrpriv->ps_processing &&
		       jiffies_to_msecs(jiffies - start) <= 3000)
			udelay(1500);
	}

	/* System suspend is not allowed to wakeup */
	if ((!pwrpriv->bInternalAutoSuspend) && (pwrpriv->bInSuspend)) {
		ret = _FAIL;
		goto exit;
	}

	/* block??? */
	if ((pwrpriv->bInternalAutoSuspend)  && (padapter->net_closed)) {
		ret = _FAIL;
		goto exit;
	}

	/* I think this should be check in IPS, LPS, autosuspend functions... */
	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		ret = _SUCCESS;
		goto exit;
	}
	if (rf_off == pwrpriv->rf_pwrstate) {
		if (ips_leave(padapter) ==  _FAIL) {
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
	expires = jiffies + msecs_to_jiffies(ips_deffer_ms);
	if (time_before(pwrpriv->ips_deny_time, expires))
		pwrpriv->ips_deny_time = jiffies + msecs_to_jiffies(ips_deffer_ms);
	return ret;
}

int rtw_pm_set_lps(struct adapter *padapter, u8 mode)
{
	int ret = 0;
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;

	if (mode < PS_MODE_NUM) {
		if (pwrctrlpriv->power_mgnt != mode) {
			if (mode == PS_MODE_ACTIVE)
				LeaveAllPowerSaveMode(padapter);
			else
				pwrctrlpriv->LpsIdleCount = 2;
			pwrctrlpriv->power_mgnt = mode;
			pwrctrlpriv->bLeisurePs = (pwrctrlpriv->power_mgnt != PS_MODE_ACTIVE);
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
		if ((padapter->bSurpriseRemoved == 0) && (rtw_pwr_wakeup(padapter) == _FAIL))
			return -EFAULT;
	} else {
		return -EINVAL;
	}
	return 0;
}
