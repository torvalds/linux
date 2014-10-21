/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
 ******************************************************************************/
#define _RTW_PWRCTRL_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <osdep_intf.h>
#include <rtl8723a_cmd.h>
#include <rtw_sreset.h>

#include <rtl8723a_bt_intf.h>
#include <usb_ops_linux.h>

void ips_enter23a(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;

	down(&pwrpriv->lock);

	pwrpriv->bips_processing = true;

	/*  syn ips_mode with request */
	pwrpriv->ips_mode = pwrpriv->ips_mode_req;

	pwrpriv->ips_enter23a_cnts++;
	DBG_8723A("==>ips_enter23a cnts:%d\n", pwrpriv->ips_enter23a_cnts);
	rtl8723a_BT_disable_coexist(padapter);

	if (pwrpriv->change_rfpwrstate == rf_off) {
		pwrpriv->bpower_saving = true;
		DBG_8723A_LEVEL(_drv_always_, "nolinked power save enter\n");

		if (pwrpriv->ips_mode == IPS_LEVEL_2)
			pwrpriv->bkeepfwalive = true;

		rtw_ips_pwr_down23a(padapter);
		pwrpriv->rf_pwrstate = rf_off;
	}
	pwrpriv->bips_processing = false;

	up(&pwrpriv->lock);
}

int ips_leave23a(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int result = _SUCCESS;
	int keyid;

	down(&pwrpriv->lock);

	if (pwrpriv->rf_pwrstate == rf_off && !pwrpriv->bips_processing) {
		pwrpriv->bips_processing = true;
		pwrpriv->change_rfpwrstate = rf_on;
		pwrpriv->ips_leave23a_cnts++;
		DBG_8723A("==>ips_leave23a cnts:%d\n",
			  pwrpriv->ips_leave23a_cnts);

		result = rtw_ips_pwr_up23a(padapter);
		if (result == _SUCCESS)
			pwrpriv->rf_pwrstate = rf_on;

		DBG_8723A_LEVEL(_drv_always_, "nolinked power save leave\n");

		if (psecuritypriv->dot11PrivacyAlgrthm ==
		    WLAN_CIPHER_SUITE_WEP40 ||
		    psecuritypriv->dot11PrivacyAlgrthm ==
		    WLAN_CIPHER_SUITE_WEP104) {
			DBG_8723A("==>%s, channel(%d), processing(%x)\n",
				  __func__, padapter->mlmeextpriv.cur_channel,
				  pwrpriv->bips_processing);
			set_channel_bwmode23a(padapter,
					      padapter->mlmeextpriv.cur_channel,
					      HAL_PRIME_CHNL_OFFSET_DONT_CARE,
					      HT_CHANNEL_WIDTH_20);
			for (keyid = 0; keyid < 4; keyid++) {
				if (pmlmepriv->key_mask & BIT(keyid)) {
					if (keyid ==
					    psecuritypriv->dot11PrivacyKeyIndex)
						result = rtw_set_key23a(padapter, psecuritypriv, keyid, 1);
					else
						result = rtw_set_key23a(padapter, psecuritypriv, keyid, 0);
				}
			}
		}

		DBG_8723A("==> ips_leave23a.....LED(0x%08x)...\n",
			  rtl8723au_read32(padapter, 0x4c));
		pwrpriv->bips_processing = false;

		pwrpriv->bkeepfwalive = false;
		pwrpriv->bpower_saving = false;
	}

	up(&pwrpriv->lock);

	return result;
}


static bool rtw_pwr_unassociated_idle(struct rtw_adapter *adapter)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct xmit_priv *pxmit_priv = &adapter->xmitpriv;

	bool ret = false;

	if (time_after_eq(adapter->pwrctrlpriv.ips_deny_time, jiffies))
		goto exit;

	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE|WIFI_SITE_MONITOR) ||
	    check_fwstate(pmlmepriv, WIFI_UNDER_LINKING|WIFI_UNDER_WPS) ||
	    check_fwstate(pmlmepriv, WIFI_AP_STATE) ||
	    check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE|WIFI_ADHOC_STATE)){
		goto exit;
	}

	if (pxmit_priv->free_xmitbuf_cnt != NR_XMITBUFF ||
		pxmit_priv->free_xmit_extbuf_cnt != NR_XMIT_EXTBUFF) {
		DBG_8723A_LEVEL(_drv_always_,
				"There are some pkts to transmit\n");
		DBG_8723A_LEVEL(_drv_info_, "free_xmitbuf_cnt: %d, "
				"free_xmit_extbuf_cnt: %d\n",
				pxmit_priv->free_xmitbuf_cnt,
				pxmit_priv->free_xmit_extbuf_cnt);
		goto exit;
	}

	ret = true;

exit:
	return ret;
}

void rtw_ps_processor23a(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	pwrpriv->ps_processing = true;

	if (pwrpriv->bips_processing == true)
		goto exit;

	if (pwrpriv->ips_mode_req == IPS_NONE)
		goto exit;

	if (rtw_pwr_unassociated_idle(padapter) == false)
		goto exit;

	if (pwrpriv->rf_pwrstate == rf_on &&
	    (pwrpriv->pwr_state_check_cnts % 4) == 0) {
		DBG_8723A("==>%s .fw_state(%x)\n", __func__,
			  get_fwstate(pmlmepriv));
		pwrpriv->change_rfpwrstate = rf_off;
		ips_enter23a(padapter);
	}
exit:
	rtw_set_pwr_state_check_timer(&padapter->pwrctrlpriv);
	pwrpriv->ps_processing = false;
	return;
}

static void pwr_state_check_handler(unsigned long data)
{
	struct rtw_adapter *padapter = (struct rtw_adapter *)data;
	rtw_ps_cmd23a(padapter);
}

/*
 *
 * Parameters
 *   padapter
 *   pslv	power state level, only could be PS_STATE_S0 ~ PS_STATE_S4
 *
 */
void rtw_set_rpwm23a(struct rtw_adapter *padapter, u8 pslv)
{
	u8 rpwm;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;

	pslv = PS_STATE(pslv);

	if (pwrpriv->btcoex_rfon) {
		if (pslv < PS_STATE_S4)
			pslv = PS_STATE_S3;
	}

	if (pwrpriv->rpwm == pslv) {
		RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_err_,
			("%s: Already set rpwm[0x%02X], new = 0x%02X!\n",
			 __func__, pwrpriv->rpwm, pslv));
		return;
	}

	if (padapter->bSurpriseRemoved == true ||
	    padapter->hw_init_completed == false) {
		RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_err_,
			 ("%s: SurpriseRemoved(%d) hw_init_completed(%d)\n",
			  __func__, padapter->bSurpriseRemoved,
			  padapter->hw_init_completed));

		pwrpriv->cpwm = PS_STATE_S4;

		return;
	}

	if (padapter->bDriverStopped == true) {
		RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_err_,
			 ("%s: change power state(0x%02X) when DriverStopped\n",
			  __func__, pslv));

		if (pslv < PS_STATE_S2) {
			RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_err_,
				 ("%s: Reject to enter PS_STATE(0x%02X) lower "
				  "than S2 when DriverStopped!!\n",
				  __func__, pslv));
			return;
		}
	}

	rpwm = pslv | pwrpriv->tog;
	RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_notice_,
		 ("rtw_set_rpwm23a: rpwm = 0x%02x cpwm = 0x%02x\n",
		  rpwm, pwrpriv->cpwm));

	pwrpriv->rpwm = pslv;

	rtl8723a_set_rpwm(padapter, rpwm);

	pwrpriv->tog += 0x80;
	pwrpriv->cpwm = pslv;
}

static bool PS_RDY_CHECK(struct rtw_adapter *padapter)
{
	unsigned long delta_time;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	delta_time = jiffies - pwrpriv->DelayLPSLastTimeStamp;

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
	    !padapter->securitypriv.binstallGrpkey) {
		DBG_8723A("Group handshake still in progress !!!\n");
		return false;
	}
	if (!rtw_cfg80211_pwr_mgmt(padapter))
		return false;

	return true;
}

void rtw_set_ps_mode23a(struct rtw_adapter *padapter, u8 ps_mode,
			u8 smart_ps, u8 bcn_ant_mode)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;

	RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_notice_,
			 ("%s: PowerMode =%d Smart_PS =%d\n",
			  __func__, ps_mode, smart_ps));

	if (ps_mode > PM_Card_Disable) {
		RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_err_,
			 ("ps_mode:%d error\n", ps_mode));
		return;
	}

	if (pwrpriv->pwr_mode == ps_mode) {
		if (PS_MODE_ACTIVE == ps_mode)
			return;

		if (pwrpriv->smart_ps == smart_ps &&
		    pwrpriv->bcn_ant_mode == bcn_ant_mode)
			return;
	}

	if (ps_mode == PS_MODE_ACTIVE) {
		DBG_8723A("rtw_set_ps_mode23a: Leave 802.11 power save\n");

		pwrpriv->pwr_mode = ps_mode;
		rtw_set_rpwm23a(padapter, PS_STATE_S4);
		rtl8723a_set_FwPwrMode_cmd(padapter, ps_mode);
		pwrpriv->bFwCurrentInPSMode = false;
	} else {
		if (PS_RDY_CHECK(padapter) ||
		    rtl8723a_BT_using_antenna_1(padapter)) {
			DBG_8723A("%s: Enter 802.11 power save\n", __func__);

			pwrpriv->bFwCurrentInPSMode = true;
			pwrpriv->pwr_mode = ps_mode;
			pwrpriv->smart_ps = smart_ps;
			pwrpriv->bcn_ant_mode = bcn_ant_mode;
			rtl8723a_set_FwPwrMode_cmd(padapter, ps_mode);

			rtw_set_rpwm23a(padapter, PS_STATE_S2);
		}
	}
}

/*
 * Return:
 *	0:	Leave OK
 *	-1:	Timeout
 *	-2:	Other error
 */
s32 LPS_RF_ON_check23a(struct rtw_adapter *padapter, u32 delay_ms)
{
	unsigned long start_time, end_time;
	u8 bAwake = false;
	s32 err = 0;

	start_time = jiffies;
	end_time = start_time + msecs_to_jiffies(delay_ms);

	while (1)
	{
		bAwake = rtl8723a_get_fwlps_rf_on(padapter);
		if (bAwake == true)
			break;

		if (padapter->bSurpriseRemoved == true) {
			err = -2;
			DBG_8723A("%s: device surprise removed!!\n", __func__);
			break;
		}

		if (time_after(jiffies, end_time)) {
			err = -1;
			DBG_8723A("%s: Wait for FW LPS leave more than %u "
				  "ms!\n", __func__, delay_ms);
			break;
		}
		udelay(100);
	}

	return err;
}

/*	Description: */
/*		Enter the leisure power save mode. */
void LPS_Enter23a(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;

	if (!PS_RDY_CHECK(padapter))
		return;

	if (pwrpriv->bLeisurePs) {
		/*  Idle for a while if we connect to AP a while ago. */
		if (pwrpriv->LpsIdleCount >= 2) { /*   4 Sec */
			if (pwrpriv->pwr_mode == PS_MODE_ACTIVE) {
				pwrpriv->bpower_saving = true;
				DBG_8723A("%s smart_ps:%d\n", __func__,
					  pwrpriv->smart_ps);
				/* For Tenda W311R IOT issue */
				rtw_set_ps_mode23a(padapter,
						   pwrpriv->power_mgnt,
						   pwrpriv->smart_ps, 0);
			}
		} else
			pwrpriv->LpsIdleCount++;
	}
}

/*	Description: */
/*		Leave the leisure power save mode. */
void LPS_Leave23a(struct rtw_adapter *padapter)
{
#define LPS_LEAVE_TIMEOUT_MS 100

	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;

	if (pwrpriv->bLeisurePs) {
		if (pwrpriv->pwr_mode != PS_MODE_ACTIVE) {
			rtw_set_ps_mode23a(padapter, PS_MODE_ACTIVE, 0, 0);

			if (pwrpriv->pwr_mode == PS_MODE_ACTIVE)
				LPS_RF_ON_check23a(padapter,
						   LPS_LEAVE_TIMEOUT_MS);
		}
	}

	pwrpriv->bpower_saving = false;
}

/*  Description: Leave all power save mode: LPS, FwLPS, IPS if needed. */
/*  Move code to function by tynli. 2010.03.26. */
void LeaveAllPowerSaveMode23a(struct rtw_adapter *Adapter)
{
	struct mlme_priv *pmlmepriv = &Adapter->mlmepriv;
	u8 enqueue = 0;

	/* DBG_8723A("%s.....\n", __func__); */
	if (check_fwstate(pmlmepriv, _FW_LINKED))
		rtw_lps_ctrl_wk_cmd23a(Adapter, LPS_CTRL_LEAVE, enqueue);
}

void rtw_init_pwrctrl_priv23a(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;

	sema_init(&pwrctrlpriv->lock, 1);
	pwrctrlpriv->rf_pwrstate = rf_on;
	pwrctrlpriv->ips_enter23a_cnts = 0;
	pwrctrlpriv->ips_leave23a_cnts = 0;
	pwrctrlpriv->bips_processing = false;

	pwrctrlpriv->ips_mode = padapter->registrypriv.ips_mode;
	pwrctrlpriv->ips_mode_req = padapter->registrypriv.ips_mode;

	pwrctrlpriv->pwr_state_check_interval = RTW_PWR_STATE_CHK_INTERVAL;
	pwrctrlpriv->pwr_state_check_cnts = 0;
	pwrctrlpriv->bInSuspend = false;
	pwrctrlpriv->bkeepfwalive = false;

	pwrctrlpriv->LpsIdleCount = 0;

	/*  PS_MODE_MIN; */
	pwrctrlpriv->power_mgnt = padapter->registrypriv.power_mgnt;
	pwrctrlpriv->bLeisurePs =
		(PS_MODE_ACTIVE != pwrctrlpriv->power_mgnt)?true:false;

	pwrctrlpriv->bFwCurrentInPSMode = false;

	pwrctrlpriv->rpwm = 0;
	pwrctrlpriv->cpwm = PS_STATE_S4;

	pwrctrlpriv->pwr_mode = PS_MODE_ACTIVE;
	pwrctrlpriv->smart_ps = padapter->registrypriv.smart_ps;
	pwrctrlpriv->bcn_ant_mode = 0;

	pwrctrlpriv->tog = 0x80;

	pwrctrlpriv->btcoex_rfon = false;

	setup_timer(&pwrctrlpriv->pwr_state_check_timer,
		    pwr_state_check_handler, (unsigned long)padapter);
}

void rtw_free_pwrctrl_priv(struct rtw_adapter *adapter)
{
}

inline void rtw_set_ips_deny23a(struct rtw_adapter *padapter, u32 ms)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	pwrpriv->ips_deny_time = jiffies + msecs_to_jiffies(ms);
}

/*
* rtw_pwr_wakeup - Wake the NIC up from: 1)IPS. 2)USB autosuspend
* @adapter: pointer to _adapter structure
* @ips_deffer_ms: the ms will prevent from falling into IPS after wakeup
* Return _SUCCESS or _FAIL
*/

int _rtw_pwr_wakeup23a(struct rtw_adapter *padapter, u32 ips_deffer_ms, const char *caller)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int ret = _SUCCESS;
	unsigned long start = jiffies;
	unsigned long new_deny_time;

	new_deny_time = jiffies + msecs_to_jiffies(ips_deffer_ms);

	if (time_before(pwrpriv->ips_deny_time, new_deny_time))
		pwrpriv->ips_deny_time = new_deny_time;

	if (pwrpriv->ps_processing) {
		DBG_8723A("%s wait ps_processing...\n", __func__);
		while (pwrpriv->ps_processing &&
		       jiffies_to_msecs(jiffies - start) <= 3000)
			msleep(10);
		if (pwrpriv->ps_processing)
			DBG_8723A("%s wait ps_processing timeout\n", __func__);
		else
			DBG_8723A("%s wait ps_processing done\n", __func__);
	}

	if (rtw_sreset_inprogress(padapter)) {
		DBG_8723A("%s wait sreset_inprogress...\n", __func__);
		while (rtw_sreset_inprogress(padapter) &&
		       jiffies_to_msecs(jiffies - start) <= 4000)
			msleep(10);
		if (rtw_sreset_inprogress(padapter))
			DBG_8723A("%s wait sreset_inprogress timeout\n",
				  __func__);
		else
			DBG_8723A("%s wait sreset_inprogress done\n", __func__);
	}

	if (pwrpriv->bInSuspend) {
		DBG_8723A("%s wait bInSuspend...\n", __func__);
		while (pwrpriv->bInSuspend &&
		       (jiffies_to_msecs(jiffies - start) <= 3000)) {
			msleep(10);
		}
		if (pwrpriv->bInSuspend)
			DBG_8723A("%s wait bInSuspend timeout\n", __func__);
		else
			DBG_8723A("%s wait bInSuspend done\n", __func__);
	}

	/* System suspend is not allowed to wakeup */
	if (pwrpriv->bInSuspend) {
		ret = _FAIL;
		goto exit;
	}

	/* I think this should be check in IPS, LPS, autosuspend functions... */
	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		ret = _SUCCESS;
		goto exit;
	}

	if (rf_off == pwrpriv->rf_pwrstate) {
		DBG_8723A("%s call ips_leave23a....\n", __func__);
		if (ips_leave23a(padapter)== _FAIL) {
			DBG_8723A("======> ips_leave23a fail.............\n");
			ret = _FAIL;
			goto exit;
		}
	}

	/* TODO: the following checking need to be merged... */
	if (padapter->bDriverStopped || !padapter->bup ||
	    !padapter->hw_init_completed) {
		DBG_8723A("%s: bDriverStopped =%d, bup =%d, hw_init_completed "
			  "=%u\n", caller, padapter->bDriverStopped,
			  padapter->bup, padapter->hw_init_completed);
		ret = _FAIL;
		goto exit;
	}

exit:
	new_deny_time = jiffies + msecs_to_jiffies(ips_deffer_ms);
	if (time_before(pwrpriv->ips_deny_time, new_deny_time))
		pwrpriv->ips_deny_time = new_deny_time;
	return ret;
}

int rtw_pm_set_lps23a(struct rtw_adapter *padapter, u8 mode)
{
	int ret = 0;
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;

	if (mode < PS_MODE_NUM) {
		if (pwrctrlpriv->power_mgnt != mode) {
			if (PS_MODE_ACTIVE == mode)
				LeaveAllPowerSaveMode23a(padapter);
			else
				pwrctrlpriv->LpsIdleCount = 2;
			pwrctrlpriv->power_mgnt = mode;
			pwrctrlpriv->bLeisurePs =
				(PS_MODE_ACTIVE != pwrctrlpriv->power_mgnt) ?
				true:false;
		}
	} else
		ret = -EINVAL;

	return ret;
}

int rtw_pm_set_ips23a(struct rtw_adapter *padapter, u8 mode)
{
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;

	if (mode != IPS_NORMAL && mode != IPS_LEVEL_2 && mode != IPS_NONE)
		return -EINVAL;

	pwrctrlpriv->ips_mode_req = mode;
	if (mode == IPS_NONE) {
		DBG_8723A("%s %s\n", __func__, "IPS_NONE");
		if (padapter->bSurpriseRemoved == 0 &&
		    rtw_pwr_wakeup(padapter) == _FAIL)
			return -EFAULT;
	}

	return 0;
}
