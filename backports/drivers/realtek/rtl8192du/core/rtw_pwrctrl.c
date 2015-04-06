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
#define _RTW_PWRCTRL_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <osdep_intf.h>

#ifdef CONFIG_IPS
void ips_enter(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	_enter_pwrlock(&pwrpriv->lock);

	pwrpriv->bips_processing = true;

	/*  syn ips_mode with request */
	pwrpriv->ips_mode = pwrpriv->ips_mode_req;

	pwrpriv->ips_enter_cnts++;
	DBG_8192D("==>ips_enter cnts:%d\n", pwrpriv->ips_enter_cnts);

	if (rf_off == pwrpriv->change_rfpwrstate) {
		if (pwrpriv->ips_mode == IPS_LEVEL_2)
			pwrpriv->bkeepfwalive = true;

		rtw_ips_pwr_down(padapter);
		pwrpriv->rf_pwrstate = rf_off;
	}
	pwrpriv->bips_processing = false;
	_exit_pwrlock(&pwrpriv->lock);
}

int ips_leave(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct security_priv *psecuritypriv = &(padapter->securitypriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	int result = _SUCCESS;
	int keyid;
	_enter_pwrlock(&pwrpriv->lock);
	if ((pwrpriv->rf_pwrstate == rf_off) && (!pwrpriv->bips_processing)) {
		pwrpriv->bips_processing = true;
		pwrpriv->change_rfpwrstate = rf_on;
		pwrpriv->ips_leave_cnts++;
		DBG_8192D("==>ips_leave cnts:%d\n", pwrpriv->ips_leave_cnts);

		result = rtw_ips_pwr_up(padapter);
		if (result == _SUCCESS)
			pwrpriv->rf_pwrstate = rf_on;

		if ((_WEP40_ == psecuritypriv->dot11PrivacyAlgrthm) ||
		    (_WEP104_ == psecuritypriv->dot11PrivacyAlgrthm)) {
			DBG_8192D("==>%s,channel(%d),processing(%x)\n",
				  __func__, padapter->mlmeextpriv.cur_channel,
				  pwrpriv->bips_processing);
			set_channel_bwmode(padapter,
					   padapter->mlmeextpriv.cur_channel,
					   HAL_PRIME_CHNL_OFFSET_DONT_CARE,
					   HT_CHANNEL_WIDTH_20);
			for (keyid = 0; keyid < 4; keyid++) {
				if (pmlmepriv->key_mask & BIT(keyid)) {
					if (keyid ==
					    psecuritypriv->dot11PrivacyKeyIndex)
						result =
						    rtw_set_key(padapter,
								psecuritypriv,
								keyid, 1);
					else
						result =
						    rtw_set_key(padapter,
								psecuritypriv,
								keyid, 0);
				}
			}
		}

		DBG_8192D("==> ips_leave.....LED(0x%08x)...\n",
			  rtw_read32(padapter, 0x4c));
		pwrpriv->bips_processing = false;

		pwrpriv->bkeepfwalive = false;
	}
	_exit_pwrlock(&pwrpriv->lock);
	return result;
}

#endif

#ifdef CONFIG_AUTOSUSPEND
extern void autosuspend_enter(struct rtw_adapter *padapter);
extern int autoresume_enter(struct rtw_adapter *padapter);
#endif

static bool rtw_pwr_unassociated_idle(struct rtw_adapter *adapter)
{
	struct rtw_adapter *buddy = adapter->pbuddy_adapter;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	bool ret = false;

	if (adapter->pwrctrlpriv.ips_deny_time >= rtw_get_current_time()) {
		/* DBG_8192D("%s ips_deny_time\n", __func__); */
		goto exit;
	}

	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE | WIFI_SITE_MONITOR) ||
	    check_fwstate(pmlmepriv, WIFI_UNDER_LINKING | WIFI_UNDER_WPS) ||
	    check_fwstate(pmlmepriv, WIFI_AP_STATE) ||
	    check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE | WIFI_ADHOC_STATE))
		goto exit;

	/* consider buddy, if exist */
	if (buddy) {
		struct mlme_priv *b_pmlmepriv = &(buddy->mlmepriv);

		if (check_fwstate(b_pmlmepriv, WIFI_ASOC_STATE | WIFI_SITE_MONITOR) ||
		    check_fwstate(b_pmlmepriv, WIFI_UNDER_LINKING | WIFI_UNDER_WPS) ||
		    check_fwstate(b_pmlmepriv, WIFI_AP_STATE) ||
		    check_fwstate(b_pmlmepriv, WIFI_ADHOC_MASTER_STATE | WIFI_ADHOC_STATE)) {
			goto exit;
		}
	}
	ret = true;

exit:
	return ret;
}

void rtw_ps_processor(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	pwrpriv->ps_processing = true;

	if (pwrpriv->ips_mode_req == IPS_NONE
#ifdef CONFIG_CONCURRENT_MODE
	    || padapter->pbuddy_adapter->pwrctrlpriv.ips_mode_req == IPS_NONE
#endif
	    )
		goto exit;

	if (rtw_pwr_unassociated_idle(padapter) == false)
		goto exit;

	if ((pwrpriv->rf_pwrstate == rf_on) &&
	    ((pwrpriv->pwr_state_check_cnts % 4) == 0)) {
		DBG_8192D("==>%s .fw_state(%x)\n", __func__,
			  get_fwstate(pmlmepriv));
		pwrpriv->change_rfpwrstate = rf_off;

#ifdef CONFIG_AUTOSUSPEND
		if (padapter->registrypriv.usbss_enable) {
			if (pwrpriv->bHWPwrPindetect)
				pwrpriv->bkeepfwalive = true;

			if (padapter->net_closed == true)
				pwrpriv->ps_flag = true;

			padapter->bCardDisableWOHSM = true;
			autosuspend_enter(padapter);
		} else
#endif /* CONFIG_AUTOSUSPEND */
		{
#ifdef CONFIG_IPS
			ips_enter(padapter);
#endif
		}
	}
exit:
	rtw_set_pwr_state_check_timer(&padapter->pwrctrlpriv);
	pwrpriv->ps_processing = false;
	return;
}

static void pwr_state_check_handler(void *FunctionContext)
{
	struct rtw_adapter *padapter = (struct rtw_adapter *)FunctionContext;

	rtw_ps_cmd(padapter);
}

#ifdef CONFIG_LPS
/*
 *
 * Parameters
 *	padapter
 *	pslv			power state level, only could be PS_STATE_S0 ~ PS_STATE_S4
 *
 */
void rtw_set_rpwm(struct rtw_adapter *padapter, u8 pslv)
{
	u8 rpwm;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;

	pslv = PS_STATE(pslv);

	if (pwrpriv->rpwm == pslv) {
		RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_err_,
			 ("%s: Already set rpwm[0x%02x]!\n", __func__, pslv));
		return;
	}

	if ((padapter->bDriverStopped == true) ||
	    (padapter->bSurpriseRemoved == true)) {
		RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_err_,
			 ("%s: bDriverStopped(%d) bSurpriseRemoved(%d)\n",
			  __func__, padapter->bDriverStopped,
			  padapter->bSurpriseRemoved));
		return;
	}

	rpwm = pslv | pwrpriv->tog;
#ifdef CONFIG_LPS_LCLK
	if ((pwrpriv->cpwm < PS_STATE_S2) && (pslv >= PS_STATE_S2))
		rpwm |= PS_ACK;
#endif
	RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_notice_,
		 ("rtw_set_rpwm: rpwm=0x%02x cpwm=0x%02x\n", rpwm,
		  pwrpriv->cpwm));

	pwrpriv->rpwm = pslv;

	rtw_hal_set_hwreg(padapter, HW_VAR_SET_RPWM, (u8 *)(&rpwm));

	pwrpriv->tog += 0x80;

	if (!(rpwm & PS_ACK))
		pwrpriv->cpwm = pslv;

}

static u8 ps_rdy_check(struct rtw_adapter *padapter)
{
	u32 curr_time, delta_time;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	curr_time = rtw_get_current_time();

	delta_time = curr_time - pwrpriv->DelayLPSLastTimeStamp;

	if (delta_time < LPS_DELAY_TIME)
		return false;

	if ((check_fwstate(pmlmepriv, _FW_LINKED) == false) ||
	    (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true))
		return false;

	if (true == pwrpriv->bInSuspend)
		return false;

	if ((padapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) &&
	    (padapter->securitypriv.binstallGrpkey == false)) {
		DBG_8192D("Group handshake still in progress !!!\n");
		return false;
	}
	if (!rtw_cfg80211_pwr_mgmt(padapter))
		return false;

	return true;
}

void rtw_set_ps_mode(struct rtw_adapter *padapter, u8 ps_mode, u8 smart_ps)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;

	RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_notice_,
		 ("%s: PowerMode=%d Smart_PS=%d\n",
		  __func__, ps_mode, smart_ps));

	if (ps_mode > PM_Card_Disable) {
		RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_err_,
			 ("ps_mode:%d error\n", ps_mode));
		return;
	}

	if ((pwrpriv->pwr_mode == ps_mode) && (pwrpriv->smart_ps == smart_ps)) {
		return;
	}

	/* if (pwrpriv->pwr_mode == PS_MODE_ACTIVE) */
	if (ps_mode == PS_MODE_ACTIVE) {
		{
#ifdef CONFIG_LPS_LCLK
			_enter_pwrlock(&pwrpriv->lock);
#endif
			DBG_8192D
			    ("rtw_set_ps_mode(): Busy Traffic , Leave 802.11 power save..\n");

			pwrpriv->smart_ps = smart_ps;
			pwrpriv->pwr_mode = ps_mode;

			rtw_set_rpwm(padapter, PS_STATE_S4);
#ifdef CONFIG_LPS_LCLK
			{
				u32 n = 0;
				while (pwrpriv->cpwm != PS_STATE_S4) {
					n++;
					if (n == 10000)
						break;
					if (padapter->bSurpriseRemoved == true)
						break;
					rtw_msleep_os(1);
				}
				if (n == 10000)
					printk(KERN_ERR
					       "%s: wait CPWM to S4 too long! cpwm=0x%02x\n",
					       __func__, pwrpriv->cpwm);
			}
#endif
			rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_PWRMODE,
					  (u8 *)(&ps_mode));
			pwrpriv->bFwCurrentInPSMode = false;
#ifdef CONFIG_LPS_LCLK
			_exit_pwrlock(&pwrpriv->lock);
#endif
		}
	} else {
		if (ps_rdy_check(padapter)) {
#ifdef CONFIG_LPS_LCLK
			_enter_pwrlock(&pwrpriv->lock);
#endif
			DBG_8192D
			    ("rtw_set_ps_mode(): Enter 802.11 power save mode...\n");

			pwrpriv->smart_ps = smart_ps;
			pwrpriv->pwr_mode = ps_mode;
			pwrpriv->bFwCurrentInPSMode = true;
			rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_PWRMODE, (u8 *)(&ps_mode));
#ifdef CONFIG_LPS_LCLK
			if (pwrpriv->alives == 0)
				rtw_set_rpwm(padapter, PS_STATE_S0);
#else
			rtw_set_rpwm(padapter, PS_STATE_S2);
#endif
#ifdef CONFIG_LPS_LCLK
			_exit_pwrlock(&pwrpriv->lock);
#endif
		}
		/* else */
		/*  */
		/*      pwrpriv->pwr_mode = PS_MODE_ACTIVE; */
		/*  */
	}

}

/*  */
/*	Description: */
/*		Enter the leisure power save mode. */
/*  */
void rtw_lps_enter(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct rtw_adapter *buddy = padapter->pbuddy_adapter;

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->iface_type != IFACE_PORT0)
		return;		/* Skip power saving for concurrent mode port 1 */

	/* consider buddy, if exist */
	if (buddy) {
		struct mlme_priv *b_pmlmepriv = &(buddy->mlmepriv);

		if (check_fwstate
		    (b_pmlmepriv, WIFI_ASOC_STATE | WIFI_SITE_MONITOR) ||
		     check_fwstate(b_pmlmepriv, WIFI_UNDER_LINKING | WIFI_UNDER_WPS) ||
		     check_fwstate(b_pmlmepriv, WIFI_AP_STATE) ||
		     check_fwstate(b_pmlmepriv, WIFI_ADHOC_MASTER_STATE | WIFI_ADHOC_STATE) ||
		    rtw_is_scan_deny(buddy))
			return;
	}
#endif

	if ((check_fwstate(pmlmepriv, _FW_LINKED) == false) ||
	    (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == true) ||
	    (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true))
		return;

	if (true == pwrpriv->bInSuspend)
		return;

	if (pwrpriv->bLeisurePs) {
		/*  Idle for a while if we connect to AP a while ago. */
		if (pwrpriv->LpsIdleCount >= 2) {	/*   4 Sec */
			if (pwrpriv->pwr_mode == PS_MODE_ACTIVE) {
				rtw_set_ps_mode(padapter, pwrpriv->power_mgnt,
						2);
			}
		} else {
			pwrpriv->LpsIdleCount++;
		}
	}

}

/*  */
/*	Description: */
/*		Leave the leisure power save mode. */
/*  */
void rtw_lps_leave(struct rtw_adapter *padapter)
{
#define LPS_LEAVE_TIMEOUT_MS 100

	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	u32 start_time;
	bool bAwake = false;

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->iface_type != IFACE_PORT0)
		return;		/* Skip power saving for concurrent mode port 1 */
#endif

	if (pwrpriv->bLeisurePs) {
		if (pwrpriv->pwr_mode != PS_MODE_ACTIVE) {
			rtw_set_ps_mode(padapter, PS_MODE_ACTIVE, 0);

			if (pwrpriv->pwr_mode == PS_MODE_ACTIVE) {
				start_time = rtw_get_current_time();
				while (1) {
					rtw_hal_get_hwreg(padapter,
							  HW_VAR_FWLPS_RF_ON,
							  (u8 *)(&bAwake));

					if (bAwake || padapter->bSurpriseRemoved)
						break;

					if (rtw_get_passing_time_ms(start_time) > LPS_LEAVE_TIMEOUT_MS) {
						DBG_8192D
						    ("Wait for FW LPS leave more than %u ms!!!\n",
						     LPS_LEAVE_TIMEOUT_MS);
						break;
					}
					rtw_usleep_os(100);
				}
			}
		}
	}

}

#endif

/*  */
/*  Description: Leave all power save mode: LPS, FwLPS, IPS if needed. */
/*  Move code to function by tynli. 2010.03.26. */
/*  */
void LeaveAllPowerSaveMode(struct rtw_adapter *adapter)
{
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);

	/* DBG_8192D("%s.....\n",__func__); */
	if (check_fwstate(pmlmepriv, _FW_LINKED) == true) {	/* connect */
#ifdef CONFIG_LPS
		/* DBG_8192D("==> leave LPS.......\n"); */
		rtw_lps_leave(adapter);
#endif
	} else {
		if (adapter->pwrctrlpriv.rf_pwrstate == rf_off) {
#ifdef CONFIG_AUTOSUSPEND
			if (adapter->registrypriv.usbss_enable) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
				usb_disable_autosuspend(adapter_to_dvobj
							(adapter)->pusbdev);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22) && LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 34))
				adapter_to_dvobj(adapter)->pusbdev->autosuspend_disabled = adapter->bDisableAutosuspend;	/* autosuspend disabled by the user */
#endif
			} else
#endif
			{
			}
		}
	}

}

#ifdef CONFIG_LPS_LCLK
/*
 * Caller:ISR handler...
 *
 * This will be called when CPWM interrupt is up.
 *
 * using to update cpwn of drv; and drv willl make a decision to up or down pwr level
 */
void cpwm_int_hdl(struct rtw_adapter *padapter,
		  struct reportpwrstate_parm *preportpwrstate)
{
	struct pwrctrl_priv *pwrpriv;

	pwrpriv = &padapter->pwrctrlpriv;
	pwrpriv->cpwm = PS_STATE(preportpwrstate->state);
	pwrpriv->cpwm_tog = preportpwrstate->state & PS_TOGGLE;

	if (pwrpriv->cpwm >= PS_STATE_S2) {
		if (pwrpriv->alives & CMD_ALIVE)
			_rtw_up_sema(&padapter->cmdpriv.cmd_queue_sema);

		if (pwrpriv->alives & XMIT_ALIVE)
			_rtw_up_sema(&padapter->xmitpriv.xmit_sema);
	}

exit:
	RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_notice_,
		 ("cpwm_int_hdl: cpwm=0x%02x\n", pwrpriv->cpwm));

}

static inline void register_task_alive(struct pwrctrl_priv *pwrctrl, u32 tag)
{
	pwrctrl->alives |= tag;
}

static inline void unregister_task_alive(struct pwrctrl_priv *pwrctrl,
					   u32 tag)
{
	pwrctrl->alives &= ~tag;
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
 *	 _SUCCESS	rtw_xmit_thread can write fifo/txcmd afterwards.
 *	 _FAIL		rtw_xmit_thread can not do anything.
 */
s32 rtw_register_tx_alive(struct rtw_adapter *padapter)
{
	s32 res;
	struct pwrctrl_priv *pwrctrl;

	res = _SUCCESS;
	pwrctrl = &padapter->pwrctrlpriv;

	_enter_pwrlock(&pwrctrl->lock);

	register_task_alive(pwrctrl, XMIT_ALIVE);

	if (pwrctrl->bFwCurrentInPSMode == true) {
		RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_err_,
			 ("rtw_register_tx_alive: cpwm=0x%02x alives=0x%08x\n",
			  pwrctrl->cpwm, pwrctrl->alives));

		if (pwrctrl->cpwm < PS_STATE_S2) {
			if (pwrctrl->rpwm < PS_STATE_S2)
				rtw_set_rpwm(padapter, PS_STATE_S2);
			res = _FAIL;
		}
	}

	_exit_pwrlock(&pwrctrl->lock);

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
 *	_SUCCESS	rtw_cmd_thread can issue cmds to firmware afterwards.
 *	_FAIL		rtw_cmd_thread can not do anything.
 */
s32 rtw_register_cmd_alive(struct rtw_adapter *padapter)
{
	s32 res;
	struct pwrctrl_priv *pwrctrl;

	res = _SUCCESS;
	pwrctrl = &padapter->pwrctrlpriv;

	_enter_pwrlock(&pwrctrl->lock);

	register_task_alive(pwrctrl, CMD_ALIVE);

	if (pwrctrl->bFwCurrentInPSMode == true) {
		RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_notice_,
			 ("rtw_register_cmd_alive: cpwm=0x%02x alives=0x%08x\n",
			  pwrctrl->cpwm, pwrctrl->alives));

		if (pwrctrl->cpwm < PS_STATE_S2) {
			if (pwrctrl->rpwm < PS_STATE_S2)
				rtw_set_rpwm(padapter, PS_STATE_S2);
			res = _FAIL;
		}
	}

	_exit_pwrlock(&pwrctrl->lock);

	return res;
}

/*
 * Caller: rx_isr
 *
 * Calling Context: Dispatch/ISR
 *
 * Return Value:
 *	_SUCCESS
 *	_FAIL
 */
s32 rtw_register_rx_alive(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrctrl;

	pwrctrl = &padapter->pwrctrlpriv;

	_enter_pwrlock(&pwrctrl->lock);

	register_task_alive(pwrctrl, RECV_ALIVE);
	RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_notice_,
		 ("rtw_register_rx_alive: cpwm=0x%02x alives=0x%08x\n",
		  pwrctrl->cpwm, pwrctrl->alives));

	_exit_pwrlock(&pwrctrl->lock);

	return _SUCCESS;
}

/*
 * Caller: evt_isr or evt_thread
 *
 * Calling Context: Dispatch/ISR or Passive
 *
 * Return Value:
 *	_SUCCESS
 *	_FAIL
 */
s32 rtw_register_evt_alive(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrctrl;

	pwrctrl = &padapter->pwrctrlpriv;

	_enter_pwrlock(&pwrctrl->lock);

	register_task_alive(pwrctrl, EVT_ALIVE);
	RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_notice_,
		 ("rtw_register_evt_alive: cpwm=0x%02x alives=0x%08x\n",
		  pwrctrl->cpwm, pwrctrl->alives));

	_exit_pwrlock(&pwrctrl->lock);

	return _SUCCESS;
}

/*
 * Caller: ISR
 *
 * If ISR's txdone,
 * No more pkts for TX,
 * Then driver shall call this fun. to power down firmware again.
 */
void rtw_unregister_tx_alive(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrctrl;

	pwrctrl = &padapter->pwrctrlpriv;

	_enter_pwrlock(&pwrctrl->lock);

	unregister_task_alive(pwrctrl, XMIT_ALIVE);

	if ((pwrctrl->pwr_mode != PS_MODE_ACTIVE) &&
	    (pwrctrl->bFwCurrentInPSMode == true)) {
		if ((pwrctrl->alives == 0) && (pwrctrl->cpwm > PS_STATE_S0))
			rtw_set_rpwm(padapter, PS_STATE_S0);

		RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_notice_,
			 ("rtw_unregister_tx_alive: cpwm=0x%02x alives=0x%08x\n",
			  pwrctrl->cpwm, pwrctrl->alives));
	}

	_exit_pwrlock(&pwrctrl->lock);

}

/*
 * Caller: ISR
 *
 * If all commands have been done,
 * and no more command to do,
 * then driver shall call this fun. to power down firmware again.
 */
void rtw_unregister_cmd_alive(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrctrl;

	pwrctrl = &padapter->pwrctrlpriv;

	_enter_pwrlock(&pwrctrl->lock);

	unregister_task_alive(pwrctrl, CMD_ALIVE);

	if ((pwrctrl->pwr_mode != PS_MODE_ACTIVE) &&
	    (pwrctrl->bFwCurrentInPSMode == true)) {
		if ((pwrctrl->alives == 0) && (pwrctrl->cpwm > PS_STATE_S0))
			rtw_set_rpwm(padapter, PS_STATE_S0);

		RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_notice_,
			 ("rtw_unregister_cmd_alive: cpwm=0x%02x alives=0x%08x\n",
			  pwrctrl->cpwm, pwrctrl->alives));
	}

	_exit_pwrlock(&pwrctrl->lock);

}

/*
 * Caller: ISR
 */
void rtw_unregister_rx_alive(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrctrl;

	pwrctrl = &padapter->pwrctrlpriv;

	_enter_pwrlock(&pwrctrl->lock);

	unregister_task_alive(pwrctrl, RECV_ALIVE);

	RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_notice_,
		 ("rtw_unregister_rx_alive: cpwm=0x%02x alives=0x%08x\n",
		  pwrctrl->cpwm, pwrctrl->alives));

	_exit_pwrlock(&pwrctrl->lock);

}

void rtw_unregister_evt_alive(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrctrl;

	pwrctrl = &padapter->pwrctrlpriv;

	unregister_task_alive(pwrctrl, EVT_ALIVE);

	RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_notice_,
		 ("rtw_unregister_evt_alive: cpwm=0x%02x alives=0x%08x\n",
		  pwrctrl->cpwm, pwrctrl->alives));

	_exit_pwrlock(&pwrctrl->lock);

}
#endif /* CONFIG_LPS_LCLK */

#ifdef CONFIG_RESUME_IN_WORKQUEUE
static void resume_workitem_callback(struct work_struct *work);
#endif /* CONFIG_RESUME_IN_WORKQUEUE */

void rtw_init_pwrctrl_priv(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;

	_init_pwrlock(&pwrctrlpriv->lock);
	pwrctrlpriv->rf_pwrstate = rf_on;
	pwrctrlpriv->ips_enter_cnts = 0;
	pwrctrlpriv->ips_leave_cnts = 0;

	pwrctrlpriv->ips_mode = padapter->registrypriv.ips_mode;
	pwrctrlpriv->ips_mode_req = padapter->registrypriv.ips_mode;

	pwrctrlpriv->pwr_state_check_interval = RTW_PWR_STATE_CHK_INTERVAL;
	pwrctrlpriv->pwr_state_check_cnts = 0;
	pwrctrlpriv->bInternalAutoSuspend = false;
	pwrctrlpriv->bInSuspend = false;
	pwrctrlpriv->bkeepfwalive = false;

	pwrctrlpriv->LpsIdleCount = 0;
	pwrctrlpriv->power_mgnt = padapter->registrypriv.power_mgnt;	/*  PS_MODE_MIN; */
	pwrctrlpriv->bLeisurePs =
	    (PS_MODE_ACTIVE != pwrctrlpriv->power_mgnt) ? true : false;

	pwrctrlpriv->bFwCurrentInPSMode = false;

	pwrctrlpriv->cpwm = PS_STATE_S4;

	pwrctrlpriv->pwr_mode = PS_MODE_ACTIVE;

	pwrctrlpriv->smart_ps = 0;

	pwrctrlpriv->tog = 0x80;

	_init_timer(&(pwrctrlpriv->pwr_state_check_timer), padapter->pnetdev,
		    pwr_state_check_handler, (u8 *)padapter);

#ifdef CONFIG_RESUME_IN_WORKQUEUE
	_init_workitem(&pwrctrlpriv->resume_work, resume_workitem_callback,
		       NULL);
	pwrctrlpriv->rtw_workqueue =
	    create_singlethread_workqueue("rtw_workqueue");
#endif /* CONFIG_RESUME_IN_WORKQUEUE */

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
	pwrctrlpriv->early_suspend.suspend = NULL;
	rtw_register_early_suspend(pwrctrlpriv);
#endif /* CONFIG_HAS_EARLYSUSPEND || CONFIG_ANDROID_POWER */

}

void rtw_free_pwrctrl_priv(struct rtw_adapter *adapter)
{
	struct pwrctrl_priv *pwrctrlpriv = &adapter->pwrctrlpriv;

#ifdef CONFIG_RESUME_IN_WORKQUEUE
	if (pwrctrlpriv->rtw_workqueue) {
		flush_workqueue(pwrctrlpriv->rtw_workqueue);
		destroy_workqueue(pwrctrlpriv->rtw_workqueue);
	}
#endif

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
	rtw_unregister_early_suspend(pwrctrlpriv);
#endif /* CONFIG_HAS_EARLYSUSPEND || CONFIG_ANDROID_POWER */

	_free_pwrlock(&pwrctrlpriv->lock);

}

#ifdef CONFIG_RESUME_IN_WORKQUEUE
extern int rtw_resume_process(struct rtw_adapter *padapter);
static void resume_workitem_callback(struct work_struct *work)
{
	struct pwrctrl_priv *pwrpriv =
	    container_of(work, struct pwrctrl_priv, resume_work);
	struct rtw_adapter *adapter =
	    container_of(pwrpriv, _adapter, pwrctrlpriv);

	DBG_8192D("%s\n", __func__);
	rtw_resume_process(adapter);
}

void rtw_resume_in_workqueue(struct pwrctrl_priv *pwrpriv)
{
	/*  accquire system's suspend lock preventing from falliing
	 * asleep while resume in workqueue */
	rtw_lock_suspend();
	queue_work(pwrpriv->rtw_workqueue, &pwrpriv->resume_work);
}
#endif /* CONFIG_RESUME_IN_WORKQUEUE */

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
inline bool rtw_is_earlysuspend_registered(struct pwrctrl_priv *pwrpriv)
{
	return (pwrpriv->early_suspend.suspend) ? true : false;
}

inline bool rtw_is_do_late_resume(struct pwrctrl_priv *pwrpriv)
{
	return (pwrpriv->do_late_resume) ? true : false;
}

inline void rtw_set_do_late_resume(struct pwrctrl_priv *pwrpriv, bool enable)
{
	pwrpriv->do_late_resume = enable;
}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
extern int rtw_resume_process(struct rtw_adapter *padapter);
static void rtw_early_suspend(struct early_suspend *h)
{
	struct pwrctrl_priv *pwrpriv =
	    container_of(h, struct pwrctrl_priv, early_suspend);
	DBG_8192D("%s\n", __func__);

	rtw_set_do_late_resume(pwrpriv, false);
}

static void rtw_late_resume(struct early_suspend *h)
{
	struct pwrctrl_priv *pwrpriv =
	    container_of(h, struct pwrctrl_priv, early_suspend);
	struct rtw_adapter *adapter =
	    container_of(pwrpriv, struct rtw_adapter, pwrctrlpriv);

	DBG_8192D("%s\n", __func__);
	if (pwrpriv->do_late_resume) {
		rtw_set_do_late_resume(pwrpriv, false);
		rtw_resume_process(adapter);
	}
}

void rtw_register_early_suspend(struct pwrctrl_priv *pwrpriv)
{
	DBG_8192D("%s\n", __func__);

	/* jeff: set the early suspend level before blank screen,
	 * so we wll do late resume after scree is lit */
	pwrpriv->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 20;
	pwrpriv->early_suspend.suspend = rtw_early_suspend;
	pwrpriv->early_suspend.resume = rtw_late_resume;
	register_early_suspend(&pwrpriv->early_suspend);
}

void rtw_unregister_early_suspend(struct pwrctrl_priv *pwrpriv)
{
	DBG_8192D("%s\n", __func__);

	rtw_set_do_late_resume(pwrpriv, false);

	if (pwrpriv->early_suspend.suspend)
		unregister_early_suspend(&pwrpriv->early_suspend);

	pwrpriv->early_suspend.suspend = NULL;
	pwrpriv->early_suspend.resume = NULL;
}
#endif /* CONFIG_HAS_EARLYSUSPEND */

#ifdef CONFIG_ANDROID_POWER
extern int rtw_resume_process(struct rtw_adapter *padapter);
static void rtw_early_suspend(android_early_suspend_t *h)
{
	struct pwrctrl_priv *pwrpriv =
	    container_of(h, struct pwrctrl_priv, early_suspend);
	DBG_8192D("%s\n", __func__);

	rtw_set_do_late_resume(pwrpriv, false);
}

static void rtw_late_resume(android_early_suspend_t *h)
{
	struct pwrctrl_priv *pwrpriv =
	    container_of(h, struct pwrctrl_priv, early_suspend);
	struct rtw_adapter *adapter =
	    container_of(pwrpriv, _adapter, pwrctrlpriv);

	DBG_8192D("%s\n", __func__);
	if (pwrpriv->do_late_resume) {
		rtw_set_do_late_resume(pwrpriv, false);
		rtw_resume_process(adapter);
	}
}

void rtw_register_early_suspend(struct pwrctrl_priv *pwrpriv)
{
	DBG_8192D("%s\n", __func__);

	/* jeff: set the early suspend level before blank screen,
	 * so we wll do late resume after screen is lit */
	pwrpriv->early_suspend.level =
	    ANDROID_EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 20;
	pwrpriv->early_suspend.suspend = rtw_early_suspend;
	pwrpriv->early_suspend.resume = rtw_late_resume;
	android_register_early_suspend(&pwrpriv->early_suspend);
}

void rtw_unregister_early_suspend(struct pwrctrl_priv *pwrpriv)
{
	DBG_8192D("%s\n", __func__);

	rtw_set_do_late_resume(pwrpriv, false);

	if (pwrpriv->early_suspend.suspend)
		android_unregister_early_suspend(&pwrpriv->early_suspend);

	pwrpriv->early_suspend.suspend = NULL;
	pwrpriv->early_suspend.resume = NULL;
}
#endif /* CONFIG_ANDROID_POWER */

u8 rtw_interface_ps_func(struct rtw_adapter *padapter,
			 enum HAL_INTF_PS_FUNC efunc_id, u8 *val)
{
	rtw_hal_intf_ps_func(padapter, efunc_id, val);
	return true;
}

inline void rtw_set_ips_deny(struct rtw_adapter *padapter, u32 ms)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	pwrpriv->ips_deny_time = rtw_get_current_time() + rtw_ms_to_systime(ms);
}

/*
* rtw_pwr_wakeup - Wake the NIC up from: 1)IPS. 2)USB autosuspend
* @adapter: pointer to _adapter structure
* @ips_deffer_ms: the ms wiil prevent from falling into IPS after wakeup
* Return _SUCCESS or _FAIL
*/
int _rtw_pwr_wakeup(struct rtw_adapter *padapter, u32 ips_deffer_ms,
		    const char *caller)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int ret = _SUCCESS;
	u32 start = rtw_get_current_time();

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->pbuddy_adapter)
		LeaveAllPowerSaveMode(padapter->pbuddy_adapter);

	if ((padapter->isprimary == false) && padapter->pbuddy_adapter) {
		padapter = padapter->pbuddy_adapter;
		pwrpriv = &padapter->pwrctrlpriv;
		pmlmepriv = &padapter->mlmepriv;
	}
#endif

	if (pwrpriv->ips_deny_time <
	    rtw_get_current_time() + rtw_ms_to_systime(ips_deffer_ms))
		pwrpriv->ips_deny_time =
		    rtw_get_current_time() + rtw_ms_to_systime(ips_deffer_ms);

	if (pwrpriv->ps_processing) {
		DBG_8192D("%s wait ps_processing...\n", __func__);
		while (pwrpriv->ps_processing &&
		       rtw_get_passing_time_ms(start) <= 3000)
			rtw_msleep_os(10);
		if (pwrpriv->ps_processing)
			DBG_8192D("%s wait ps_processing timeout\n", __func__);
		else
			DBG_8192D("%s wait ps_processing done\n", __func__);
	}

	if (pwrpriv->bInternalAutoSuspend == false && pwrpriv->bInSuspend) {
		DBG_8192D("%s wait bInSuspend...\n", __func__);
		while (pwrpriv->bInSuspend &&
		       ((rtw_get_passing_time_ms(start) <= 3000 &&
		       !rtw_is_do_late_resume(pwrpriv)) ||
		       (rtw_get_passing_time_ms(start) <= 500 &&
		       rtw_is_do_late_resume(pwrpriv)))) {
			rtw_msleep_os(10);
		}
		if (pwrpriv->bInSuspend)
			DBG_8192D("%s wait bInSuspend timeout\n", __func__);
		else
			DBG_8192D("%s wait bInSuspend done\n", __func__);
	}

	/* System suspend is not allowed to wakeup */
	if ((pwrpriv->bInternalAutoSuspend == false) &&
	    (true == pwrpriv->bInSuspend)) {
		ret = _FAIL;
		goto exit;
	}

	/* block??? */
	if ((pwrpriv->bInternalAutoSuspend == true) &&
	    (padapter->net_closed == true)) {
		ret = _FAIL;
		goto exit;
	}

	/* I think this should be check in IPS, LPS, autosuspend functions... */
	if (check_fwstate(pmlmepriv, _FW_LINKED) == true) {
		ret = _SUCCESS;
		goto exit;
	}

	if (rf_off == pwrpriv->rf_pwrstate) {
#ifdef CONFIG_AUTOSUSPEND
		if (pwrpriv->brfoffbyhw == true) {
			DBG_8192D("hw still in rf_off state ...........\n");
			ret = _FAIL;
			goto exit;
		} else if (padapter->registrypriv.usbss_enable) {
			DBG_8192D("%s call autoresume_enter....\n", __func__);
			if (_FAIL == autoresume_enter(padapter)) {
				DBG_8192D
				    ("======> autoresume fail.............\n");
				ret = _FAIL;
				goto exit;
			}
		} else
#endif
		{
#ifdef CONFIG_IPS
			DBG_8192D("%s call ips_leave....\n", __func__);
			if (_FAIL == ips_leave(padapter)) {
				DBG_8192D
				    ("======> ips_leave fail.............\n");
				ret = _FAIL;
				goto exit;
			}
#endif
		}
	}

	/* TODO: the following checking need to be merged... */
	if (padapter->bDriverStopped || !padapter->bup ||
	    !padapter->hw_init_completed) {
		DBG_8192D
		    ("%s: bDriverStopped=%d, bup=%d, hw_init_completed=%u\n",
		     caller, padapter->bDriverStopped, padapter->bup,
		     padapter->hw_init_completed);
		ret = false;
		goto exit;
	}

exit:
	if (pwrpriv->ips_deny_time <
	    rtw_get_current_time() + rtw_ms_to_systime(ips_deffer_ms))
		pwrpriv->ips_deny_time =
		    rtw_get_current_time() + rtw_ms_to_systime(ips_deffer_ms);
	return ret;
}

int rtw_pm_set_lps(struct rtw_adapter *padapter, u8 mode)
{
	int ret = 0;
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;

	if (mode < PS_MODE_NUM) {
		if (pwrctrlpriv->power_mgnt != mode) {
			if (PS_MODE_ACTIVE == mode)
				LeaveAllPowerSaveMode(padapter);
			else
				pwrctrlpriv->LpsIdleCount = 2;
			pwrctrlpriv->power_mgnt = mode;
			pwrctrlpriv->bLeisurePs =
			    (PS_MODE_ACTIVE !=
			     pwrctrlpriv->power_mgnt) ? true : false;
		}
	} else {
		ret = -EINVAL;
	}

	return ret;
}

int rtw_pm_set_ips(struct rtw_adapter *padapter, u8 mode)
{
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;

	if (mode == IPS_NORMAL || mode == IPS_LEVEL_2) {
		rtw_ips_mode_req(pwrctrlpriv, mode);
		DBG_8192D("%s %s\n", __func__,
			  mode == IPS_NORMAL ? "IPS_NORMAL" : "IPS_LEVEL_2");
		return 0;
	} else if (mode == IPS_NONE) {
		rtw_ips_mode_req(pwrctrlpriv, mode);
		DBG_8192D("%s %s\n", __func__, "IPS_NONE");
		if ((padapter->bSurpriseRemoved == 0) &&
		    (_FAIL == rtw_pwr_wakeup(padapter)))
			return -EFAULT;
	} else {
		return -EINVAL;
	}
	return 0;
}
