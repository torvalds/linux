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
#define _RTW_PWRCTRL_C_

#include <drv_types.h>
#include <hal_data.h>
#include <hal_com_h2c.h>

#ifdef DBG_CHECK_FW_PS_STATE
int rtw_fw_ps_state(PADAPTER padapter)
{
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	int ret = _FAIL, dont_care = 0;
	u16 fw_ps_state = 0;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct registry_priv  *registry_par = &padapter->registrypriv;

	if (registry_par->check_fw_ps != 1)
		return _SUCCESS;

	_enter_pwrlock(&pwrpriv->check_32k_lock);

	if (RTW_CANNOT_RUN(padapter)) {
		RTW_INFO("%s: bSurpriseRemoved=%s , hw_init_completed=%d, bDriverStopped=%s\n", __func__
			 , rtw_is_surprise_removed(padapter) ? "True" : "False"
			 , rtw_get_hw_init_completed(padapter)
			 , rtw_is_drv_stopped(padapter) ? "True" : "False");
		goto exit_fw_ps_state;
	}
	#if defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C) || defined(CONFIG_RTL8822C)
	rtw_hal_get_hwreg(padapter, HW_VAR_FW_PS_STATE, (u8 *)&fw_ps_state);
	if ((fw_ps_state & BIT_LPS_STATUS) == 0)
		ret = _SUCCESS;
	else {
		pdbgpriv->dbg_poll_fail_cnt++;
		RTW_INFO("%s: fw_ps_state=%04x\n", __FUNCTION__, fw_ps_state);
	}
	#else
	rtw_hal_set_hwreg(padapter, HW_VAR_SET_REQ_FW_PS, (u8 *)&dont_care);
	{
		/* 4. if 0x88[7]=1, driver set cmd to leave LPS/IPS. */
		/* Else, hw will keep in active mode. */
		/* debug info: */
		/* 0x88[7] = 32kpermission, */
		/* 0x88[6:0] = current_ps_state */
		/* 0x89[7:0] = last_rpwm */

		rtw_hal_get_hwreg(padapter, HW_VAR_FW_PS_STATE, (u8 *)&fw_ps_state);

		if ((fw_ps_state & 0x80) == 0)
			ret = _SUCCESS;
		else {
			pdbgpriv->dbg_poll_fail_cnt++;
			RTW_INFO("%s: fw_ps_state=%04x\n", __FUNCTION__, fw_ps_state);
		}
	}
	#endif

exit_fw_ps_state:
	_exit_pwrlock(&pwrpriv->check_32k_lock);
	return ret;
}
#endif /*DBG_CHECK_FW_PS_STATE*/
#ifdef CONFIG_IPS
void _ips_enter(_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);

	pwrpriv->bips_processing = _TRUE;

	/* syn ips_mode with request */
	pwrpriv->ips_mode = pwrpriv->ips_mode_req;

	pwrpriv->ips_enter_cnts++;
	RTW_INFO("==>ips_enter cnts:%d\n", pwrpriv->ips_enter_cnts);

	if (rf_off == pwrpriv->change_rfpwrstate) {
		pwrpriv->bpower_saving = _TRUE;
		RTW_PRINT("nolinked power save enter\n");

		if (pwrpriv->ips_mode == IPS_LEVEL_2)
			pwrpriv->bkeepfwalive = _TRUE;

#ifdef CONFIG_RTW_CFGVEDNOR_LLSTATS		
		pwrpriv->pwr_saving_start_time = rtw_get_current_time();
#endif /* CONFIG_RTW_CFGVEDNOR_LLSTATS */

		rtw_ips_pwr_down(padapter);
		pwrpriv->rf_pwrstate = rf_off;
	}
	pwrpriv->bips_processing = _FALSE;

}

void ips_enter(_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);


#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_IpsNotify(padapter, pwrpriv->ips_mode_req);
#endif /* CONFIG_BT_COEXIST */

	_enter_pwrlock(&pwrpriv->lock);
	_ips_enter(padapter);
	_exit_pwrlock(&pwrpriv->lock);
}

int _ips_leave(_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	int result = _SUCCESS;

	if ((pwrpriv->rf_pwrstate == rf_off) && (!pwrpriv->bips_processing)) {
		pwrpriv->bips_processing = _TRUE;
		pwrpriv->change_rfpwrstate = rf_on;
		pwrpriv->ips_leave_cnts++;
		RTW_INFO("==>ips_leave cnts:%d\n", pwrpriv->ips_leave_cnts);

		result = rtw_ips_pwr_up(padapter);
		if (result == _SUCCESS)
			pwrpriv->rf_pwrstate = rf_on;
		
#ifdef CONFIG_RTW_CFGVEDNOR_LLSTATS	
		pwrpriv->pwr_saving_time += rtw_get_passing_time_ms(pwrpriv->pwr_saving_start_time);
#endif /* CONFIG_RTW_CFGVEDNOR_LLSTATS */

		RTW_PRINT("nolinked power save leave\n");

		RTW_INFO("==> ips_leave.....LED(0x%08x)...\n", rtw_read32(padapter, 0x4c));
		pwrpriv->bips_processing = _FALSE;

		pwrpriv->bkeepfwalive = _FALSE;
		pwrpriv->bpower_saving = _FALSE;
	}

	return result;
}

int ips_leave(_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
#ifdef DBG_CHECK_FW_PS_STATE
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
#endif
	int ret;

	if (!is_primary_adapter(padapter))
		return _SUCCESS;

	_enter_pwrlock(&pwrpriv->lock);
	ret = _ips_leave(padapter);
#ifdef DBG_CHECK_FW_PS_STATE
	if (rtw_fw_ps_state(padapter) == _FAIL) {
		RTW_INFO("ips leave doesn't leave 32k\n");
		pdbgpriv->dbg_leave_ips_fail_cnt++;
	}
#endif /* DBG_CHECK_FW_PS_STATE */
	_exit_pwrlock(&pwrpriv->lock);

	if (_SUCCESS == ret)
		odm_dm_reset(&GET_HAL_DATA(padapter)->odmpriv);

#ifdef CONFIG_BT_COEXIST
	if (_SUCCESS == ret)
		rtw_btcoex_IpsNotify(padapter, IPS_NONE);
#endif /* CONFIG_BT_COEXIST */

	return ret;
}
#endif /* CONFIG_IPS */

#ifdef CONFIG_AUTOSUSPEND
	extern void autosuspend_enter(_adapter *padapter);
	extern int autoresume_enter(_adapter *padapter);
#endif

#ifdef SUPPORT_HW_RFOFF_DETECTED
	int rtw_hw_suspend(_adapter *padapter);
	int rtw_hw_resume(_adapter *padapter);
#endif

bool rtw_pwr_unassociated_idle(_adapter *adapter)
{
	u8 i;
	_adapter *iface;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct xmit_priv *pxmit_priv = &adapter->xmitpriv;
	struct mlme_priv *pmlmepriv;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo;
#endif

	bool ret = _FALSE;

	if (adapter_to_pwrctl(adapter)->bpower_saving == _TRUE) {
		/* RTW_INFO("%s: already in LPS or IPS mode\n", __func__); */
		goto exit;
	}

	if (rtw_time_after(adapter_to_pwrctl(adapter)->ips_deny_time, rtw_get_current_time())) {
		/* RTW_INFO("%s ips_deny_time\n", __func__); */
		goto exit;
	}

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if ((iface) && rtw_is_adapter_up(iface)) {
			pmlmepriv = &(iface->mlmepriv);
#ifdef CONFIG_P2P
			pwdinfo = &(iface->wdinfo);
#endif
			if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE | WIFI_SITE_MONITOR)
				|| check_fwstate(pmlmepriv, WIFI_UNDER_LINKING | WIFI_UNDER_WPS)
				|| MLME_IS_AP(iface)
				|| MLME_IS_MESH(iface)
				|| check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE | WIFI_ADHOC_STATE)
				#if defined(CONFIG_P2P) && defined(CONFIG_IOCTL_CFG80211)
				|| rtw_cfg80211_get_is_roch(iface) == _TRUE
				|| (rtw_cfg80211_is_ro_ch_once(adapter)
					&& rtw_cfg80211_get_last_ro_ch_passing_ms(adapter) < 3000)
				#elif defined(CONFIG_P2P)
				|| rtw_p2p_chk_state(pwdinfo, P2P_STATE_IDLE)
				|| rtw_p2p_chk_state(pwdinfo, P2P_STATE_LISTEN)
				#endif
			)
				goto exit;

		}
	}

#if (MP_DRIVER == 1)
	if (adapter->registrypriv.mp_mode == 1)
		goto exit;
#endif

	if (pxmit_priv->free_xmitbuf_cnt != NR_XMITBUFF ||
	    pxmit_priv->free_xmit_extbuf_cnt != NR_XMIT_EXTBUFF) {
		RTW_PRINT("There are some pkts to transmit\n");
		RTW_PRINT("free_xmitbuf_cnt: %d, free_xmit_extbuf_cnt: %d\n",
			pxmit_priv->free_xmitbuf_cnt, pxmit_priv->free_xmit_extbuf_cnt);
		goto exit;
	}

	ret = _TRUE;

exit:
	return ret;
}


/*
 * ATTENTION:
 *	rtw_ps_processor() doesn't handle LPS.
 */
void rtw_ps_processor(_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
#ifdef SUPPORT_HW_RFOFF_DETECTED
	rt_rf_power_state rfpwrstate;
#endif /* SUPPORT_HW_RFOFF_DETECTED */
	u32 ps_deny = 0;

	_enter_pwrlock(&adapter_to_pwrctl(padapter)->lock);
	ps_deny = rtw_ps_deny_get(padapter);
	_exit_pwrlock(&adapter_to_pwrctl(padapter)->lock);
	if (ps_deny != 0) {
		RTW_INFO(FUNC_ADPT_FMT ": ps_deny=0x%08X, skip power save!\n",
			 FUNC_ADPT_ARG(padapter), ps_deny);
		goto exit;
	}

	if (pwrpriv->bInSuspend == _TRUE) { /* system suspend or autosuspend */
		pdbgpriv->dbg_ps_insuspend_cnt++;
		RTW_INFO("%s, pwrpriv->bInSuspend == _TRUE ignore this process\n", __FUNCTION__);
		return;
	}

	pwrpriv->ps_processing = _TRUE;

#ifdef SUPPORT_HW_RFOFF_DETECTED
	if (pwrpriv->bips_processing == _TRUE)
		goto exit;

	/* RTW_INFO("==> fw report state(0x%x)\n",rtw_read8(padapter,0x1ca));	 */
	if (pwrpriv->bHWPwrPindetect) {
#ifdef CONFIG_AUTOSUSPEND
		if (padapter->registrypriv.usbss_enable) {
			if (pwrpriv->rf_pwrstate == rf_on) {
				if (padapter->net_closed == _TRUE)
					pwrpriv->ps_flag = _TRUE;

				rfpwrstate = RfOnOffDetect(padapter);
				RTW_INFO("@@@@- #1  %s==> rfstate:%s\n", __FUNCTION__, (rfpwrstate == rf_on) ? "rf_on" : "rf_off");
				if (rfpwrstate != pwrpriv->rf_pwrstate) {
					if (rfpwrstate == rf_off) {
						pwrpriv->change_rfpwrstate = rf_off;

						pwrpriv->bkeepfwalive = _TRUE;
						pwrpriv->brfoffbyhw = _TRUE;

						autosuspend_enter(padapter);
					}
				}
			}
		} else
#endif /* CONFIG_AUTOSUSPEND */
		{
			rfpwrstate = RfOnOffDetect(padapter);
			RTW_INFO("@@@@- #2  %s==> rfstate:%s\n", __FUNCTION__, (rfpwrstate == rf_on) ? "rf_on" : "rf_off");

			if (rfpwrstate != pwrpriv->rf_pwrstate) {
				if (rfpwrstate == rf_off) {
					pwrpriv->change_rfpwrstate = rf_off;
					pwrpriv->brfoffbyhw = _TRUE;
					rtw_hw_suspend(padapter);
				} else {
					pwrpriv->change_rfpwrstate = rf_on;
					rtw_hw_resume(padapter);
				}
				RTW_INFO("current rf_pwrstate(%s)\n", (pwrpriv->rf_pwrstate == rf_off) ? "rf_off" : "rf_on");
			}
		}
		pwrpriv->pwr_state_check_cnts++;
	}
#endif /* SUPPORT_HW_RFOFF_DETECTED */

	if (pwrpriv->ips_mode_req == IPS_NONE)
		goto exit;

	if (rtw_pwr_unassociated_idle(padapter) == _FALSE)
		goto exit;

	if ((pwrpriv->rf_pwrstate == rf_on) && ((pwrpriv->pwr_state_check_cnts % 4) == 0)) {
		RTW_INFO("==>%s .fw_state(%x)\n", __FUNCTION__, get_fwstate(pmlmepriv));
#if defined(CONFIG_BT_COEXIST) && defined (CONFIG_AUTOSUSPEND)
#else
		pwrpriv->change_rfpwrstate = rf_off;
#endif
#ifdef CONFIG_AUTOSUSPEND
		if (padapter->registrypriv.usbss_enable) {
			if (pwrpriv->bHWPwrPindetect)
				pwrpriv->bkeepfwalive = _TRUE;

			if (padapter->net_closed == _TRUE)
				pwrpriv->ps_flag = _TRUE;

#if defined(CONFIG_BT_COEXIST) && defined (CONFIG_AUTOSUSPEND)
			if (_TRUE == pwrpriv->bInternalAutoSuspend)
				RTW_INFO("<==%s .pwrpriv->bInternalAutoSuspend)(%x)\n", __FUNCTION__, pwrpriv->bInternalAutoSuspend);
			else {
				pwrpriv->change_rfpwrstate = rf_off;
				RTW_INFO("<==%s .pwrpriv->bInternalAutoSuspend)(%x) call autosuspend_enter\n", __FUNCTION__, pwrpriv->bInternalAutoSuspend);
				autosuspend_enter(padapter);
			}
#else
			autosuspend_enter(padapter);
#endif	/* if defined (CONFIG_BT_COEXIST)&& defined (CONFIG_AUTOSUSPEND) */
		} else if (pwrpriv->bHWPwrPindetect) {
		} else
#endif /* CONFIG_AUTOSUSPEND */
		{
#if defined(CONFIG_BT_COEXIST) && defined (CONFIG_AUTOSUSPEND)
			pwrpriv->change_rfpwrstate = rf_off;
#endif	/* defined (CONFIG_BT_COEXIST)&& defined (CONFIG_AUTOSUSPEND) */

#ifdef CONFIG_IPS
			ips_enter(padapter);
#endif
		}
	}
exit:
#ifndef CONFIG_IPS_CHECK_IN_WD
	rtw_set_pwr_state_check_timer(pwrpriv);
#endif
	pwrpriv->ps_processing = _FALSE;
	return;
}

void pwr_state_check_handler(void *ctx)
{
	_adapter *padapter = (_adapter *)ctx;
	rtw_ps_cmd(padapter);
}

#ifdef CONFIG_LPS
#ifdef CONFIG_CHECK_LEAVE_LPS
#ifdef CONFIG_LPS_CHK_BY_TP
void traffic_check_for_leave_lps_by_tp(PADAPTER padapter, u8 tx, struct sta_info *sta)
{
	struct stainfo_stats *pstats = &sta->sta_stats;
	u64 cur_acc_tx_bytes = 0, cur_acc_rx_bytes = 0;
	u32 tx_tp_kbyte = 0, rx_tp_kbyte = 0;
	u32 tx_tp_th = 0, rx_tp_th = 0;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	u8	leave_lps = _FALSE;

	if (tx) { /* from tx */
		cur_acc_tx_bytes = pstats->tx_bytes - pstats->acc_tx_bytes;
		tx_tp_kbyte = cur_acc_tx_bytes >> 10;
		tx_tp_th = pwrpriv->lps_tx_tp_th * 1024 / 8 * 2; /*KBytes @2s*/

		if (tx_tp_kbyte >= tx_tp_th ||
			padapter->mlmepriv.LinkDetectInfo.NumTxOkInPeriod >= pwrpriv->lps_tx_pkts){
			if (pwrpriv->bLeisurePs
				&& (pwrpriv->pwr_mode != PS_MODE_ACTIVE)
				#ifdef CONFIG_BT_COEXIST
				&& (rtw_btcoex_IsBtControlLps(padapter) == _FALSE)
				#endif
			) {
				leave_lps = _TRUE;
			}
		}

	} else { /* from rx path */
		cur_acc_rx_bytes = pstats->rx_bytes - pstats->acc_rx_bytes;
		rx_tp_kbyte = cur_acc_rx_bytes >> 10;
		rx_tp_th = pwrpriv->lps_rx_tp_th * 1024 / 8 * 2;

		if (rx_tp_kbyte>= rx_tp_th ||
			padapter->mlmepriv.LinkDetectInfo.NumRxUnicastOkInPeriod >= pwrpriv->lps_rx_pkts) {
			if (pwrpriv->bLeisurePs
				&& (pwrpriv->pwr_mode != PS_MODE_ACTIVE)
				#ifdef CONFIG_BT_COEXIST
				&& (rtw_btcoex_IsBtControlLps(padapter) == _FALSE)
				#endif
			) {
				leave_lps = _TRUE;
			}
		}
	}

	if (leave_lps) {
		#ifdef DBG_LPS_CHK_BY_TP
		RTW_INFO("leave lps via %s, ", tx ? "Tx" : "Rx");
		if (tx)
			RTW_INFO("Tx = %d [%d] (KB)\n", tx_tp_kbyte, tx_tp_th);
		else
			RTW_INFO("Rx = %d [%d] (KB)\n", rx_tp_kbyte, rx_tp_th);
		#endif
		pwrpriv->lps_chk_cnt = pwrpriv->lps_chk_cnt_th;
		/* rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_LEAVE, 0); */
		rtw_lps_ctrl_wk_cmd(padapter, tx ? LPS_CTRL_TX_TRAFFIC_LEAVE : LPS_CTRL_RX_TRAFFIC_LEAVE, 0);
	}
}
#endif /*CONFIG_LPS_CHK_BY_TP*/

void	traffic_check_for_leave_lps(PADAPTER padapter, u8 tx, u32 tx_packets)
{
	static systime start_time = 0;
	static u32 xmit_cnt = 0;
	u8	bLeaveLPS = _FALSE;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;



	if (tx) { /* from tx */
		xmit_cnt += tx_packets;

		if (start_time == 0)
			start_time = rtw_get_current_time();

		if (rtw_get_passing_time_ms(start_time) > 2000) { /* 2 sec == watch dog timer */
			if (xmit_cnt > 8) {
				if ((adapter_to_pwrctl(padapter)->bLeisurePs)
				    && (adapter_to_pwrctl(padapter)->pwr_mode != PS_MODE_ACTIVE)
#ifdef CONFIG_BT_COEXIST
				    && (rtw_btcoex_IsBtControlLps(padapter) == _FALSE)
#endif
				   ) {
					/* RTW_INFO("leave lps via Tx = %d\n", xmit_cnt);			 */
					bLeaveLPS = _TRUE;
				}
			}

			start_time = rtw_get_current_time();
			xmit_cnt = 0;
		}

	} else { /* from rx path */
		if (pmlmepriv->LinkDetectInfo.NumRxUnicastOkInPeriod > 4/*2*/) {
			if ((adapter_to_pwrctl(padapter)->bLeisurePs)
			    && (adapter_to_pwrctl(padapter)->pwr_mode != PS_MODE_ACTIVE)
#ifdef CONFIG_BT_COEXIST
			    && (rtw_btcoex_IsBtControlLps(padapter) == _FALSE)
#endif
			   ) {
				/* RTW_INFO("leave lps via Rx = %d\n", pmlmepriv->LinkDetectInfo.NumRxUnicastOkInPeriod);	 */
				bLeaveLPS = _TRUE;
			}
		}
	}

	if (bLeaveLPS) {
		/* RTW_INFO("leave lps via %s, Tx = %d, Rx = %d\n", tx?"Tx":"Rx", pmlmepriv->LinkDetectInfo.NumTxOkInPeriod,pmlmepriv->LinkDetectInfo.NumRxUnicastOkInPeriod);	 */
		/* rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_LEAVE, 0); */
		rtw_lps_ctrl_wk_cmd(padapter, tx ? LPS_CTRL_TX_TRAFFIC_LEAVE : LPS_CTRL_RX_TRAFFIC_LEAVE, tx ? RTW_CMDF_DIRECTLY : 0);
	}
}
#endif /* CONFIG_CHECK_LEAVE_LPS */

#ifdef CONFIG_LPS_LCLK
#define LPS_CPWM_TIMEOUT_MS	10 /*ms*/
#define LPS_RPWM_RETRY_CNT		3

u8 rtw_cpwm_polling(_adapter *adapter, u8 rpwm, u8 cpwm_orig)
{
	u8 rst = _FAIL;
	u8 cpwm_now = 0;
	systime start_time;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	#ifdef DBG_CHECK_FW_PS_STATE
	struct debug_priv *pdbgpriv = &(adapter_to_dvobj(adapter)->drv_dbg);
	#endif

	pwrpriv->rpwm_retry = 0;

	do {
		start_time = rtw_get_current_time();
		do {
			rtw_msleep_os(1);
			rtw_hal_get_hwreg(adapter, HW_VAR_CPWM, &cpwm_now);

			if ((cpwm_orig ^ cpwm_now) & 0x80) {
				pwrpriv->cpwm = PS_STATE_S4;
				pwrpriv->cpwm_tog = cpwm_now & PS_TOGGLE;
				rst = _SUCCESS;
				break;
			}
		} while (rtw_get_passing_time_ms(start_time) < LPS_CPWM_TIMEOUT_MS && !RTW_CANNOT_RUN(adapter));

		if (rst == _SUCCESS)
			break;
		else {
			/* rpwm retry */
			cpwm_orig = cpwm_now;
			rpwm &= ~PS_TOGGLE;
			rpwm |= pwrpriv->tog;
			rtw_hal_set_hwreg(adapter, HW_VAR_SET_RPWM, (u8 *)(&rpwm));
			pwrpriv->tog += 0x80;
		}
	} while (pwrpriv->rpwm_retry++ < LPS_RPWM_RETRY_CNT && !RTW_CANNOT_RUN(adapter));

	if (rst == _SUCCESS) {
		#ifdef DBG_CHECK_FW_PS_STATE
		RTW_INFO("%s: polling cpwm OK! rpwm_retry=%d, cpwm_orig=%02x, cpwm_now=%02x , 0x100=0x%x\n"
			, __func__, pwrpriv->rpwm_retry, cpwm_orig, cpwm_now, rtw_read8(adapter, REG_CR));
		if (rtw_fw_ps_state(adapter) == _FAIL) {
			RTW_INFO("leave 32k but fw state in 32k\n");
			pdbgpriv->dbg_rpwm_toogle_cnt++;
		}
		#endif /* DBG_CHECK_FW_PS_STATE */
	} else {
		RTW_ERR("%s: polling cpwm timeout! rpwm_retry=%d, cpwm_orig=%02x, cpwm_now=%02x\n"
				, __func__, pwrpriv->rpwm_retry, cpwm_orig, cpwm_now);
		#ifdef DBG_CHECK_FW_PS_STATE
		if (rtw_fw_ps_state(adapter) == _FAIL) {
			RTW_INFO("rpwm timeout and fw ps state in 32k\n");
			pdbgpriv->dbg_rpwm_timeout_fail_cnt++;
		}
		#endif /* DBG_CHECK_FW_PS_STATE */

		#ifdef CONFIG_LPS_RPWM_TIMER
		_set_timer(&pwrpriv->pwr_rpwm_timer, 1);
		#endif /* CONFIG_LPS_RPWM_TIMER */
	}

	return rst;
}
#endif
/*
 * Description:
 *	This function MUST be called under power lock protect
 *
 * Parameters
 *	padapter
 *	pslv			power state level, only could be PS_STATE_S0 ~ PS_STATE_S4
 *
 */
u8 rtw_set_rpwm(PADAPTER padapter, u8 pslv)
{
	u8	rpwm = 0xFF;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
#ifdef CONFIG_LPS_LCLK
	u8 cpwm_orig;
#endif

	pslv = PS_STATE(pslv);

#ifdef CONFIG_LPS_RPWM_TIMER
	if (pwrpriv->brpwmtimeout == _TRUE)
		RTW_INFO("%s: RPWM timeout, force to set RPWM(0x%02X) again!\n", __FUNCTION__, pslv);
	else
#endif /* CONFIG_LPS_RPWM_TIMER */
	{
		if ((pwrpriv->rpwm == pslv)
#ifdef CONFIG_LPS_LCLK
		    || ((pwrpriv->rpwm >= PS_STATE_S2) && (pslv >= PS_STATE_S2))
#endif
			|| (pwrpriv->lps_level == LPS_NORMAL)
		   ) {
			return rpwm;
		}
	}

	if (rtw_is_surprise_removed(padapter) ||
	    (!rtw_is_hw_init_completed(padapter))) {

		pwrpriv->cpwm = PS_STATE_S4;

		return rpwm;
	}

	if (rtw_is_drv_stopped(padapter))
		if (pslv < PS_STATE_S2)
			return rpwm;

	rpwm = pslv | pwrpriv->tog;
#ifdef CONFIG_LPS_LCLK
	/* only when from PS_STATE S0/S1 to S2 and higher needs ACK */
	if ((pwrpriv->cpwm < PS_STATE_S2) && (pslv >= PS_STATE_S2))
		rpwm |= PS_ACK;
#endif

	pwrpriv->rpwm = pslv;

#ifdef CONFIG_LPS_LCLK
	cpwm_orig = 0;
	if (rpwm & PS_ACK)
		rtw_hal_get_hwreg(padapter, HW_VAR_CPWM, &cpwm_orig);
#endif

#if defined(CONFIG_LPS_RPWM_TIMER) && !defined(CONFIG_DETECT_CPWM_BY_POLLING)
	if (rpwm & PS_ACK) {
		#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN) || defined(CONFIG_P2P_WOWLAN)
		if (pwrpriv->wowlan_mode != _TRUE &&
			pwrpriv->wowlan_ap_mode != _TRUE &&
			pwrpriv->wowlan_p2p_mode != _TRUE)
		#endif
		_set_timer(&pwrpriv->pwr_rpwm_timer, LPS_CPWM_TIMEOUT_MS);
	}
#endif /* CONFIG_LPS_RPWM_TIMER & !CONFIG_DETECT_CPWM_BY_POLLING */

	rtw_hal_set_hwreg(padapter, HW_VAR_SET_RPWM, (u8 *)(&rpwm));

	pwrpriv->tog += 0x80;

#ifdef CONFIG_LPS_LCLK
	/* No LPS 32K, No Ack */
	if (rpwm & PS_ACK) {
		#ifdef CONFIG_DETECT_CPWM_BY_POLLING
		rtw_cpwm_polling(padapter, rpwm, cpwm_orig);
		#else
		#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN) || defined(CONFIG_P2P_WOWLAN)
		if (pwrpriv->wowlan_mode == _TRUE ||
			pwrpriv->wowlan_ap_mode == _TRUE ||
			pwrpriv->wowlan_p2p_mode == _TRUE)
				rtw_cpwm_polling(padapter, rpwm, cpwm_orig);
		#endif /*#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN) || defined(CONFIG_P2P_WOWLAN)*/
		#endif /*#ifdef CONFIG_DETECT_CPWM_BY_POLLING*/
	} else
#endif /* CONFIG_LPS_LCLK */
	{
		pwrpriv->cpwm = pslv;
	}

	return rpwm;
}

u8 PS_RDY_CHECK(_adapter *padapter)
{
	u32 delta_ms;
	struct pwrctrl_priv	*pwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	if (_TRUE == pwrpriv->bInSuspend && pwrpriv->wowlan_mode)
		return _TRUE;
	else if (_TRUE == pwrpriv->bInSuspend && pwrpriv->wowlan_ap_mode)
		return _TRUE;
	else if (_TRUE == pwrpriv->bInSuspend)
		return _FALSE;
#else
	if (_TRUE == pwrpriv->bInSuspend)
		return _FALSE;
#endif

	delta_ms = rtw_get_passing_time_ms(pwrpriv->DelayLPSLastTimeStamp);
	if (delta_ms < LPS_DELAY_MS)
		return _FALSE;

	if (check_fwstate(pmlmepriv, WIFI_SITE_MONITOR)
		|| check_fwstate(pmlmepriv, WIFI_UNDER_LINKING | WIFI_UNDER_WPS)
		|| MLME_IS_AP(padapter)
		|| MLME_IS_MESH(padapter)
		|| check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE | WIFI_ADHOC_STATE)
		#if defined(CONFIG_P2P) && defined(CONFIG_IOCTL_CFG80211)
		|| rtw_cfg80211_get_is_roch(padapter) == _TRUE
		#endif
		|| rtw_is_scan_deny(padapter)
		#ifdef CONFIG_TDLS
		/* TDLS link is established. */
		|| (padapter->tdlsinfo.link_established == _TRUE)
		#endif /* CONFIG_TDLS		 */
		#ifdef CONFIG_DFS_MASTER
		|| adapter_to_rfctl(padapter)->radar_detect_enabled
		#endif
	)
		return _FALSE;

	if ((padapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) && (padapter->securitypriv.binstallGrpkey == _FALSE)) {
		RTW_INFO("Group handshake still in progress !!!\n");
		return _FALSE;
	}

#ifdef CONFIG_IOCTL_CFG80211
	if (!rtw_cfg80211_pwr_mgmt(padapter))
		return _FALSE;
#endif

	return _TRUE;
}

#if defined(CONFIG_FWLPS_IN_IPS)
void rtw_set_fw_in_ips_mode(PADAPTER padapter, u8 enable)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	int cnt = 0;
	systime start_time;
	u8 val8 = 0;
	u8 cpwm_orig = 0, cpwm_now = 0;
	u8 parm[H2C_INACTIVE_PS_LEN] = {0};

	if (padapter->netif_up == _FALSE) {
		RTW_INFO("%s: ERROR, netif is down\n", __func__);
		return;
	}

	/* u8 cmd_param; */ /* BIT0:enable, BIT1:NoConnect32k */
	if (enable) {
#ifdef CONFIG_BT_COEXIST
		rtw_btcoex_IpsNotify(padapter, pwrpriv->ips_mode_req);
#endif
		/* Enter IPS */
		RTW_INFO("%s: issue H2C to FW when entering IPS\n", __func__);

		parm[0] = 0x1;/* suggest by Isaac.Hsu*/
#ifdef CONFIG_PNO_SUPPORT
		if (pwrpriv->pno_inited) {
			parm[1] = pwrpriv->pnlo_info->fast_scan_iterations;
			parm[2] = pwrpriv->pnlo_info->slow_scan_period;
		}
#endif

		rtw_hal_fill_h2c_cmd(padapter, /* H2C_FWLPS_IN_IPS_, */
				     H2C_INACTIVE_PS_,
				     H2C_INACTIVE_PS_LEN, parm);
		/* poll 0x1cc to make sure H2C command already finished by FW; MAC_0x1cc=0 means H2C done by FW. */
		do {
			val8 = rtw_read8(padapter, REG_HMETFR);
			cnt++;
			RTW_INFO("%s  polling REG_HMETFR=0x%x, cnt=%d\n",
				 __func__, val8, cnt);
			rtw_mdelay_os(10);
		} while (cnt < 100 && (val8 != 0));

#ifdef CONFIG_LPS_LCLK
		/* H2C done, enter 32k */
		if (val8 == 0) {
			/* ser rpwm to enter 32k */
			rtw_hal_get_hwreg(padapter, HW_VAR_RPWM_TOG, &val8);
			RTW_INFO("%s: read rpwm=%02x\n", __FUNCTION__, val8);
			val8 += 0x80;
			val8 |= BIT(0);
			rtw_hal_set_hwreg(padapter, HW_VAR_SET_RPWM, (u8 *)(&val8));
			RTW_INFO("%s: write rpwm=%02x\n", __FUNCTION__, val8);
			adapter_to_pwrctl(padapter)->tog = (val8 + 0x80) & 0x80;
			cnt = val8 = 0;
			if (parm[1] == 0 || parm[2] == 0) {
				do {
					val8 = rtw_read8(padapter, REG_CR);
					cnt++;
					RTW_INFO("%s  polling 0x100=0x%x, cnt=%d\n",
						 __func__, val8, cnt);
					RTW_INFO("%s 0x08:%02x, 0x03:%02x\n",
						 __func__,
						 rtw_read8(padapter, 0x08),
						 rtw_read8(padapter, 0x03));
					rtw_mdelay_os(10);
				} while (cnt < 20 && (val8 != 0xEA));
			}
		}
#endif
	} else {
		/* Leave IPS */
		RTW_INFO("%s: Leaving IPS in FWLPS state\n", __func__);

#ifdef CONFIG_LPS_LCLK
		/* for polling cpwm */
		cpwm_orig = 0;
		rtw_hal_get_hwreg(padapter, HW_VAR_CPWM, &cpwm_orig);

		/* ser rpwm */
		rtw_hal_get_hwreg(padapter, HW_VAR_RPWM_TOG, &val8);
		val8 += 0x80;
		val8 |= BIT(6);
		rtw_hal_set_hwreg(padapter, HW_VAR_SET_RPWM, (u8 *)(&val8));
		RTW_INFO("%s: write rpwm=%02x\n", __FUNCTION__, val8);
		adapter_to_pwrctl(padapter)->tog = (val8 + 0x80) & 0x80;

		/* do polling cpwm */
		start_time = rtw_get_current_time();
		do {

			rtw_mdelay_os(1);

			rtw_hal_get_hwreg(padapter, HW_VAR_CPWM, &cpwm_now);
			if ((cpwm_orig ^ cpwm_now) & 0x80)
				break;

			if (rtw_get_passing_time_ms(start_time) > 100) {
				RTW_INFO("%s: polling cpwm timeout when leaving IPS in FWLPS state\n", __FUNCTION__);
				break;
			}
		} while (1);

#endif
		parm[0] = 0x0;
		parm[1] = 0x0;
		parm[2] = 0x0;
		rtw_hal_fill_h2c_cmd(padapter, H2C_INACTIVE_PS_,
				     H2C_INACTIVE_PS_LEN, parm);
#ifdef CONFIG_BT_COEXIST
		rtw_btcoex_IpsNotify(padapter, IPS_NONE);
#endif
	}
}
#endif /* CONFIG_PNO_SUPPORT */

void rtw_set_ps_mode(PADAPTER padapter, u8 ps_mode, u8 smart_ps, u8 bcn_ant_mode, const char *msg)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN) || defined(CONFIG_P2P_WOWLAN)
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
#endif
#ifdef CONFIG_WMMPS_STA	
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
#endif
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
#endif /* CONFIG_P2P */
#ifdef CONFIG_TDLS
	struct sta_priv *pstapriv = &padapter->stapriv;
	_irqL irqL;
	int i, j;
	_list	*plist, *phead;
	struct sta_info *ptdls_sta;
#endif /* CONFIG_TDLS */
#ifdef CONFIG_LPS_PG
	u8 lps_pg_hdl_id = 0;
#endif



	if (ps_mode > PM_Card_Disable) {
		return;
	}

	if (pwrpriv->pwr_mode == ps_mode) {
		if (PS_MODE_ACTIVE == ps_mode)
			return;

#ifndef CONFIG_BT_COEXIST
#ifdef CONFIG_WMMPS_STA	
		if (!rtw_is_wmmps_mode(padapter))
#endif /* CONFIG_WMMPS_STA */
			if ((pwrpriv->smart_ps == smart_ps) &&
			    (pwrpriv->bcn_ant_mode == bcn_ant_mode))
				return;
#endif /* !CONFIG_BT_COEXIST */
	}

#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
	if (PS_MODE_ACTIVE != ps_mode) {
		rtw_set_ps_rsvd_page(padapter);
		rtw_set_default_port_id(padapter);
	}
#endif

#ifdef CONFIG_LPS_PG
	if ((PS_MODE_ACTIVE != ps_mode) && (pwrpriv->blpspg_info_up)) {
		if (pwrpriv->wowlan_mode != _TRUE) {
				/*rtw_hal_set_lps_pg_info(padapter);*/
				lps_pg_hdl_id = LPS_PG_INFO_CFG;
				rtw_hal_set_hwreg(padapter, HW_VAR_LPS_PG_HANDLE, (u8 *)(&lps_pg_hdl_id));
		}
	}
#endif

#ifdef CONFIG_LPS_LCLK
	_enter_pwrlock(&pwrpriv->lock);
#endif

	/* if(pwrpriv->pwr_mode == PS_MODE_ACTIVE) */
	if (ps_mode == PS_MODE_ACTIVE) {
		if (1
#ifdef CONFIG_BT_COEXIST
		    && (((rtw_btcoex_IsBtControlLps(padapter) == _FALSE)
#ifdef CONFIG_P2P_PS
			 && (pwdinfo->opp_ps == 0)
#endif /* CONFIG_P2P_PS */
			)
			|| ((rtw_btcoex_IsBtControlLps(padapter) == _TRUE)
			    && (rtw_btcoex_IsLpsOn(padapter) == _FALSE))
		       )
#else /* !CONFIG_BT_COEXIST */
#ifdef CONFIG_P2P_PS
		    && (pwdinfo->opp_ps == 0)
#endif /* CONFIG_P2P_PS */
#endif /* !CONFIG_BT_COEXIST */
		   ) {
			RTW_INFO(FUNC_ADPT_FMT" Leave 802.11 power save - %s\n",
				 FUNC_ADPT_ARG(padapter), msg);

			if (pwrpriv->lps_leave_cnts < UINT_MAX)
				pwrpriv->lps_leave_cnts++;
			else
				pwrpriv->lps_leave_cnts = 0;
#ifdef CONFIG_TDLS
			for (i = 0; i < NUM_STA; i++) {
				phead = &(pstapriv->sta_hash[i]);
				plist = get_next(phead);

				while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
					ptdls_sta = LIST_CONTAINOR(plist, struct sta_info, hash_list);

					if (ptdls_sta->tdls_sta_state & TDLS_LINKED_STATE)
						issue_nulldata_to_TDLS_peer_STA(padapter, ptdls_sta->cmn.mac_addr, 0, 0, 0);
					plist = get_next(plist);
				}
			}
#endif /* CONFIG_TDLS */

			pwrpriv->pwr_mode = ps_mode;
			rtw_set_rpwm(padapter, PS_STATE_S4);

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN) || defined(CONFIG_P2P_WOWLAN)
			if (pwrpriv->wowlan_mode == _TRUE ||
			    pwrpriv->wowlan_ap_mode == _TRUE ||
			    pwrpriv->wowlan_p2p_mode == _TRUE) {
				systime start_time;
				u32 delay_ms;
				u8 val8;
				delay_ms = 20;
				start_time = rtw_get_current_time();
				do {
					rtw_hal_get_hwreg(padapter, HW_VAR_SYS_CLKR, &val8);
					if (!(val8 & BIT(4))) { /* 0x08 bit4 =1 --> in 32k, bit4 = 0 --> leave 32k */
						pwrpriv->cpwm = PS_STATE_S4;
						break;
					}
					if (rtw_get_passing_time_ms(start_time) > delay_ms) {
						RTW_INFO("%s: Wait for FW 32K leave more than %u ms!!!\n",
							__FUNCTION__, delay_ms);
						pdbgpriv->dbg_wow_leave_ps_fail_cnt++;
						break;
					}
					rtw_usleep_os(100);
				} while (1);
			}
#endif
#ifdef CONFIG_LPS_PG
			if (pwrpriv->lps_level == LPS_PG) {
				lps_pg_hdl_id = LPS_PG_REDLEMEM;
				rtw_hal_set_hwreg(padapter, HW_VAR_LPS_PG_HANDLE, (u8 *)(&lps_pg_hdl_id));
			}
#endif
#ifdef CONFIG_WOWLAN
			if (pwrpriv->wowlan_mode == _TRUE)
				rtw_hal_set_hwreg(padapter, HW_VAR_H2C_INACTIVE_IPS, (u8 *)(&ps_mode));
#endif /* CONFIG_WOWLAN */

			rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_PWRMODE, (u8 *)(&ps_mode));
			rtw_hal_set_hwreg(padapter, HW_VAR_LPS_STATE_CHK, (u8 *)(&ps_mode));


#ifdef CONFIG_LPS_PG
			if (pwrpriv->lps_level == LPS_PG) {
				lps_pg_hdl_id = LPS_PG_PHYDM_EN;
				rtw_hal_set_hwreg(padapter, HW_VAR_LPS_PG_HANDLE, (u8 *)(&lps_pg_hdl_id));
			}
#endif

#ifdef CONFIG_LPS_POFF
			rtw_hal_set_hwreg(padapter, HW_VAR_LPS_POFF_SET_MODE,
					  (u8 *)(&ps_mode));
#endif /*CONFIG_LPS_POFF*/

			pwrpriv->bFwCurrentInPSMode = _FALSE;

#ifdef CONFIG_BT_COEXIST
			rtw_btcoex_LpsNotify(padapter, ps_mode);
#endif /* CONFIG_BT_COEXIST */
		}
	} else {
		if ((PS_RDY_CHECK(padapter) && check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE))
#ifdef CONFIG_BT_COEXIST
		    || ((rtw_btcoex_IsBtControlLps(padapter) == _TRUE)
			&& (rtw_btcoex_IsLpsOn(padapter) == _TRUE))
#endif
#ifdef CONFIG_P2P_WOWLAN
		    || (_TRUE == pwrpriv->wowlan_p2p_mode)
#endif /* CONFIG_P2P_WOWLAN */
#ifdef CONFIG_WOWLAN
			|| WOWLAN_IS_STA_MIX_MODE(padapter)
#endif /* CONFIG_WOWLAN */
		   ) {
			u8 pslv;

			RTW_INFO(FUNC_ADPT_FMT" Enter 802.11 power save - %s\n",
				 FUNC_ADPT_ARG(padapter), msg);

			if (pwrpriv->lps_enter_cnts < UINT_MAX)
				pwrpriv->lps_enter_cnts++;
			else
				pwrpriv->lps_enter_cnts = 0;
#ifdef CONFIG_TDLS
			for (i = 0; i < NUM_STA; i++) {
				phead = &(pstapriv->sta_hash[i]);
				plist = get_next(phead);

				while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
					ptdls_sta = LIST_CONTAINOR(plist, struct sta_info, hash_list);

					if (ptdls_sta->tdls_sta_state & TDLS_LINKED_STATE)
						issue_nulldata_to_TDLS_peer_STA(padapter, ptdls_sta->cmn.mac_addr, 1, 0, 0);
					plist = get_next(plist);
				}
			}
#endif /* CONFIG_TDLS */

#ifdef CONFIG_BT_COEXIST
			rtw_btcoex_LpsNotify(padapter, ps_mode);
#endif /* CONFIG_BT_COEXIST */

#ifdef CONFIG_LPS_POFF
			rtw_hal_set_hwreg(padapter, HW_VAR_LPS_POFF_SET_MODE,
					  (u8 *)(&ps_mode));
#endif /*CONFIG_LPS_POFF*/

			pwrpriv->bFwCurrentInPSMode = _TRUE;
			pwrpriv->pwr_mode = ps_mode;
			pwrpriv->smart_ps = smart_ps;
			pwrpriv->bcn_ant_mode = bcn_ant_mode;
#ifdef CONFIG_LPS_PG
			if (pwrpriv->lps_level == LPS_PG) {
				lps_pg_hdl_id = LPS_PG_PHYDM_DIS;
				rtw_hal_set_hwreg(padapter, HW_VAR_LPS_PG_HANDLE, (u8 *)(&lps_pg_hdl_id));
			}
#endif

#ifdef CONFIG_WMMPS_STA	
			pwrpriv->wmm_smart_ps = pregistrypriv->wmm_smart_ps;
#endif /* CONFIG_WMMPS_STA */
			
			
			if (check_fwstate(pmlmepriv, _FW_LINKED))
				rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_PWRMODE, (u8 *)(&ps_mode));
#ifdef CONFIG_WOWLAN
			if (pwrpriv->wowlan_mode == _TRUE)
				rtw_hal_set_hwreg(padapter, HW_VAR_H2C_INACTIVE_IPS, (u8 *)(&ps_mode));
#endif /* CONFIG_WOWLAN */

#ifdef CONFIG_P2P_PS
			/* Set CTWindow after LPS */
			if (pwdinfo->opp_ps == 1)
				p2p_ps_wk_cmd(padapter, P2P_PS_ENABLE, 0);
#endif /* CONFIG_P2P_PS */

			pslv = PS_STATE_S2;
#ifdef CONFIG_LPS_LCLK
			if (pwrpriv->alives == 0)
				pslv = PS_STATE_S0;
#endif /* CONFIG_LPS_LCLK */

#ifdef CONFIG_BT_COEXIST
			if ((rtw_btcoex_IsBtDisabled(padapter) == _FALSE)
			    && (rtw_btcoex_IsBtControlLps(padapter) == _TRUE)) {
				u8 val8;

				val8 = rtw_btcoex_LpsVal(padapter);
				if (val8 & BIT(4))
					pslv = PS_STATE_S2;

			}
#endif /* CONFIG_BT_COEXIST */

			rtw_set_rpwm(padapter, pslv);
		}
	}

#ifdef CONFIG_LPS_LCLK
	_exit_pwrlock(&pwrpriv->lock);
#endif

}

/*
 *	Description:
 *		Enter the leisure power save mode.
 *   */
void LPS_Enter(PADAPTER padapter, const char *msg)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct pwrctrl_priv	*pwrpriv = dvobj_to_pwrctl(dvobj);
	int i;
	char buf[32] = {0};
#ifdef DBG_LA_MODE
	struct registry_priv *registry_par = &(padapter->registrypriv);
#endif

	/*	RTW_INFO("+LeisurePSEnter\n"); */
	if (GET_HAL_DATA(padapter)->bFWReady == _FALSE)
		return;

#ifdef CONFIG_BT_COEXIST
	if (rtw_btcoex_IsBtControlLps(padapter) == _TRUE)
		return;
#endif

#ifdef DBG_LA_MODE
	if(registry_par->la_mode_en == 1) {
		RTW_INFO("%s LA debug mode lps_leave \n", __func__);
		return;
	}
#endif
	/* Skip lps enter request if number of assocated adapters is not 1 */
	if (rtw_mi_get_assoc_if_num(padapter) != 1)
		return;

#ifndef CONFIG_FW_MULTI_PORT_SUPPORT
	/* Skip lps enter request for adapter not port0 */
	if (get_hw_port(padapter) != HW_PORT0)
		return;
#endif

	for (i = 0; i < dvobj->iface_nums; i++) {
		if (PS_RDY_CHECK(dvobj->padapters[i]) == _FALSE)
			return;
	}

#ifdef CONFIG_CLIENT_PORT_CFG
	if ((rtw_hal_get_port(padapter) == CLT_PORT_INVALID) ||
		get_clt_num(padapter) > MAX_CLIENT_PORT_NUM){
		RTW_ERR(ADPT_FMT" cannot get client port or clt num(%d) over than 4\n", ADPT_ARG(padapter), get_clt_num(padapter));
		return;
	}
#endif

#ifdef CONFIG_P2P_PS
	if (padapter->wdinfo.p2p_ps_mode == P2P_PS_NOA) {
		return;/* supporting p2p client ps NOA via H2C_8723B_P2P_PS_OFFLOAD */
	}
#endif /* CONFIG_P2P_PS */

	if (pwrpriv->bLeisurePs) {
		/* Idle for a while if we connect to AP a while ago. */
		if (pwrpriv->LpsIdleCount >= 2) { /* 4 Sec */
			if (pwrpriv->pwr_mode == PS_MODE_ACTIVE) {

#ifdef CONFIG_WMMPS_STA
				if (rtw_is_wmmps_mode(padapter))
					msg = "WMMPS_IDLE";
#endif /* CONFIG_WMMPS_STA */
				
				sprintf(buf, "WIFI-%s", msg);
				pwrpriv->bpower_saving = _TRUE;
				
#ifdef CONFIG_RTW_CFGVEDNOR_LLSTATS
				pwrpriv->pwr_saving_start_time = rtw_get_current_time();
#endif /* CONFIG_RTW_CFGVEDNOR_LLSTATS */

				rtw_set_ps_mode(padapter, pwrpriv->power_mgnt, padapter->registrypriv.smart_ps, 0, buf);
			}
		} else
			pwrpriv->LpsIdleCount++;
	}

	/*	RTW_INFO("-LeisurePSEnter\n"); */

}

/*
 *	Description:
 *		Leave the leisure power save mode.
 *   */
void LPS_Leave(PADAPTER padapter, const char *msg)
{
#define LPS_LEAVE_TIMEOUT_MS 100

	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct pwrctrl_priv	*pwrpriv = dvobj_to_pwrctl(dvobj);
	char buf[32] = {0};
#ifdef DBG_CHECK_FW_PS_STATE
	struct debug_priv *pdbgpriv = &dvobj->drv_dbg;
#endif


	/*	RTW_INFO("+LeisurePSLeave\n"); */

#ifdef CONFIG_BT_COEXIST
	if (rtw_btcoex_IsBtControlLps(padapter) == _TRUE)
		return;
#endif

	if (pwrpriv->bLeisurePs) {
		if (pwrpriv->pwr_mode != PS_MODE_ACTIVE) {

#ifdef CONFIG_WMMPS_STA
			if (rtw_is_wmmps_mode(padapter))
				msg = "WMMPS_BUSY";
#endif /* CONFIG_WMMPS_STA */
			
			sprintf(buf, "WIFI-%s", msg);
			rtw_set_ps_mode(padapter, PS_MODE_ACTIVE, 0, 0, buf);

#ifdef CONFIG_RTW_CFGVEDNOR_LLSTATS	
			pwrpriv->pwr_saving_time += rtw_get_passing_time_ms(pwrpriv->pwr_saving_start_time);
#endif /* CONFIG_RTW_CFGVEDNOR_LLSTATS */
		}
	}

	pwrpriv->bpower_saving = _FALSE;
#ifdef DBG_CHECK_FW_PS_STATE
	if (rtw_fw_ps_state(padapter) == _FAIL) {
		RTW_INFO("leave lps, fw in 32k\n");
		pdbgpriv->dbg_leave_lps_fail_cnt++;
	}
#endif /* DBG_CHECK_FW_PS_STATE
 * 	RTW_INFO("-LeisurePSLeave\n"); */

}

void rtw_wow_lps_level_decide(_adapter *adapter, u8 wow_en)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(dvobj);

	if (wow_en) {
		pwrpriv->lps_level_bk = pwrpriv->lps_level;
#ifdef CONFIG_WOWLAN
		pwrpriv->lps_level = pwrpriv->wowlan_lps_level;
#endif /* CONFIG_WOWLAN */
	} else
		pwrpriv->lps_level = pwrpriv->lps_level_bk;
}
#endif

void LeaveAllPowerSaveModeDirect(PADAPTER Adapter)
{
	PADAPTER pri_padapter = GET_PRIMARY_ADAPTER(Adapter);
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(Adapter);
#ifdef CONFIG_LPS_LCLK
#ifndef CONFIG_DETECT_CPWM_BY_POLLING
	u8 cpwm_orig;
#endif /* CONFIG_DETECT_CPWM_BY_POLLING */
	u8 rpwm;
#endif

	RTW_INFO("%s.....\n", __FUNCTION__);

	if (rtw_is_surprise_removed(Adapter)) {
		RTW_INFO(FUNC_ADPT_FMT ": bSurpriseRemoved=_TRUE Skip!\n", FUNC_ADPT_ARG(Adapter));
		return;
	}

	if (rtw_mi_check_status(Adapter, MI_LINKED)) { /*connect*/

		if (pwrpriv->pwr_mode == PS_MODE_ACTIVE) {
			RTW_INFO("%s: Driver Already Leave LPS\n", __FUNCTION__);
			return;
		}

#ifdef CONFIG_LPS_LCLK
		_enter_pwrlock(&pwrpriv->lock);

#ifndef CONFIG_DETECT_CPWM_BY_POLLING
		cpwm_orig = 0;
		rtw_hal_get_hwreg(Adapter, HW_VAR_CPWM, &cpwm_orig);
#endif /* CONFIG_DETECT_CPWM_BY_POLLING */
		rpwm = rtw_set_rpwm(Adapter, PS_STATE_S4);

#ifndef CONFIG_DETECT_CPWM_BY_POLLING
		if (rpwm != 0xFF && rpwm & PS_ACK)
			rtw_cpwm_polling(Adapter, rpwm, cpwm_orig);
#endif /* CONFIG_DETECT_CPWM_BY_POLLING */

		_exit_pwrlock(&pwrpriv->lock);
#endif/*CONFIG_LPS_LCLK*/

#ifdef CONFIG_P2P_PS
		p2p_ps_wk_cmd(pri_padapter, P2P_PS_DISABLE, 0);
#endif /* CONFIG_P2P_PS */

#ifdef CONFIG_LPS
		rtw_lps_ctrl_wk_cmd(pri_padapter, LPS_CTRL_LEAVE, RTW_CMDF_DIRECTLY);
#endif
	} else {
		if (pwrpriv->rf_pwrstate == rf_off) {
#ifdef CONFIG_AUTOSUSPEND
			if (Adapter->registrypriv.usbss_enable) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
				usb_disable_autosuspend(adapter_to_dvobj(Adapter)->pusbdev);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22) && LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 34))
				adapter_to_dvobj(Adapter)->pusbdev->autosuspend_disabled = Adapter->bDisableAutosuspend;/* autosuspend disabled by the user */
#endif
			} else
#endif
			{
#if defined(CONFIG_FWLPS_IN_IPS) || defined(CONFIG_SWLPS_IN_IPS) || defined(CONFIG_RTL8188E)
#ifdef CONFIG_IPS
				if (_FALSE == ips_leave(pri_padapter))
					RTW_INFO("======> ips_leave fail.............\n");
#endif
#endif /* CONFIG_SWLPS_IN_IPS || (CONFIG_PLATFORM_SPRD && CONFIG_RTL8188E) */
			}
		}
	}

}

/*
 * Description: Leave all power save mode: LPS, FwLPS, IPS if needed.
 * Move code to function by tynli. 2010.03.26.
 *   */
void LeaveAllPowerSaveMode(PADAPTER Adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(Adapter);
	u8	enqueue = 0;
	int i;

	#ifndef CONFIG_NEW_NETDEV_HDL
	if (_FALSE == Adapter->bup) {
		RTW_INFO(FUNC_ADPT_FMT ": bup=%d Skip!\n",
			 FUNC_ADPT_ARG(Adapter), Adapter->bup);
		return;
	}
	#endif

/*	RTW_INFO(FUNC_ADPT_FMT "\n", FUNC_ADPT_ARG(Adapter));*/

	if (rtw_is_surprise_removed(Adapter)) {
		RTW_INFO(FUNC_ADPT_FMT ": bSurpriseRemoved=_TRUE Skip!\n", FUNC_ADPT_ARG(Adapter));
		return;
	}

	if (rtw_mi_get_assoc_if_num(Adapter)) {
		/* connect */
#ifdef CONFIG_LPS_LCLK
		enqueue = 1;
#endif

#ifdef CONFIG_P2P_PS
		for (i = 0; i < dvobj->iface_nums; i++) {
			_adapter *iface = dvobj->padapters[i];
			struct wifidirect_info *pwdinfo = &(iface->wdinfo);

			if (pwdinfo->p2p_ps_mode > P2P_PS_NONE)
				p2p_ps_wk_cmd(iface, P2P_PS_DISABLE, enqueue);
		}
#endif /* CONFIG_P2P_PS */

#ifdef CONFIG_LPS
		rtw_lps_ctrl_wk_cmd(Adapter, LPS_CTRL_LEAVE, enqueue ? 0 : RTW_CMDF_DIRECTLY);
#endif

#ifdef CONFIG_LPS_LCLK
		LPS_Leave_check(Adapter);
#endif
	} else {
		if (adapter_to_pwrctl(Adapter)->rf_pwrstate == rf_off) {
#ifdef CONFIG_AUTOSUSPEND
			if (Adapter->registrypriv.usbss_enable) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
				usb_disable_autosuspend(adapter_to_dvobj(Adapter)->pusbdev);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22) && LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 34))
				adapter_to_dvobj(Adapter)->pusbdev->autosuspend_disabled = Adapter->bDisableAutosuspend;/* autosuspend disabled by the user */
#endif
			} else
#endif
			{
#if defined(CONFIG_FWLPS_IN_IPS) || defined(CONFIG_SWLPS_IN_IPS) || (defined(CONFIG_PLATFORM_SPRD) && defined(CONFIG_RTL8188E))
#ifdef CONFIG_IPS
				if (_FALSE == ips_leave(Adapter))
					RTW_INFO("======> ips_leave fail.............\n");
#endif
#endif /* CONFIG_SWLPS_IN_IPS || (CONFIG_PLATFORM_SPRD && CONFIG_RTL8188E) */
			}
		}
	}

}

#ifdef CONFIG_LPS_LCLK
void LPS_Leave_check(
	PADAPTER padapter)
{
	struct pwrctrl_priv *pwrpriv;
	systime	start_time;
	u8	bReady;


	pwrpriv = adapter_to_pwrctl(padapter);

	bReady = _FALSE;
	start_time = rtw_get_current_time();

	rtw_yield_os();

	while (1) {
		_enter_pwrlock(&pwrpriv->lock);

		if (rtw_is_surprise_removed(padapter)
		    || (!rtw_is_hw_init_completed(padapter))
#ifdef CONFIG_USB_HCI
		    || rtw_is_drv_stopped(padapter)
#endif
		    || (pwrpriv->pwr_mode == PS_MODE_ACTIVE)
		   )
			bReady = _TRUE;

		_exit_pwrlock(&pwrpriv->lock);

		if (_TRUE == bReady)
			break;

		if (rtw_get_passing_time_ms(start_time) > 100) {
			RTW_ERR("Wait for cpwm event  than 100 ms!!!\n");
			break;
		}
		rtw_msleep_os(1);
	}

}

/*
 * Caller:ISR handler...
 *
 * This will be called when CPWM interrupt is up.
 *
 * using to update cpwn of drv; and drv willl make a decision to up or down pwr level
 */
void cpwm_int_hdl(
	PADAPTER padapter,
	struct reportpwrstate_parm *preportpwrstate)
{
	struct pwrctrl_priv *pwrpriv;

	if (!padapter)
		goto exit;

	if (RTW_CANNOT_RUN(padapter))
		goto exit;

	pwrpriv = adapter_to_pwrctl(padapter);
#if 0
	if (pwrpriv->cpwm_tog == (preportpwrstate->state & PS_TOGGLE)) {
		goto exit;
	}
#endif

	_enter_pwrlock(&pwrpriv->lock);

#ifdef CONFIG_LPS_RPWM_TIMER
	if (pwrpriv->rpwm < PS_STATE_S2) {
		RTW_INFO("%s: Redundant CPWM Int. RPWM=0x%02X CPWM=0x%02x\n", __func__, pwrpriv->rpwm, pwrpriv->cpwm);
		_exit_pwrlock(&pwrpriv->lock);
		goto exit;
	}
#endif /* CONFIG_LPS_RPWM_TIMER */

	pwrpriv->cpwm = PS_STATE(preportpwrstate->state);
	pwrpriv->cpwm_tog = preportpwrstate->state & PS_TOGGLE;

	if (pwrpriv->cpwm >= PS_STATE_S2) {
		if (pwrpriv->alives & CMD_ALIVE)
			_rtw_up_sema(&padapter->cmdpriv.cmd_queue_sema);

		if (pwrpriv->alives & XMIT_ALIVE)
			_rtw_up_sema(&padapter->xmitpriv.xmit_sema);
	}

	_exit_pwrlock(&pwrpriv->lock);

exit:
	return;
}

static void cpwm_event_callback(struct work_struct *work)
{
	struct pwrctrl_priv *pwrpriv = container_of(work, struct pwrctrl_priv, cpwm_event);
	struct dvobj_priv *dvobj = pwrctl_to_dvobj(pwrpriv);
	_adapter *adapter = dvobj_get_primary_adapter(dvobj);
	struct reportpwrstate_parm report;

	/* RTW_INFO("%s\n",__FUNCTION__); */

	report.state = PS_STATE_S2;
	cpwm_int_hdl(adapter, &report);
}

static void dma_event_callback(struct work_struct *work)
{
	struct pwrctrl_priv *pwrpriv = container_of(work, struct pwrctrl_priv, dma_event);
	struct dvobj_priv *dvobj = pwrctl_to_dvobj(pwrpriv);
	_adapter *adapter = dvobj_get_primary_adapter(dvobj);

	rtw_unregister_tx_alive(adapter);
}

#ifdef CONFIG_LPS_RPWM_TIMER

#define DBG_CPWM_CHK_FAIL
#if defined(DBG_CPWM_CHK_FAIL) && (defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C) || defined(CONFIG_RTL8822C)) 
#define CPU_EXCEPTION_CODE 0xFAFAFAFA
static void rtw_cpwm_chk_fail_debug(_adapter *padapter)
{
	u32 cpu_state;

	cpu_state = rtw_read32(padapter, 0x10FC);

	RTW_INFO("[PS-DBG] Reg_10FC =0x%08x\n", cpu_state);
	RTW_INFO("[PS-DBG] Reg_10F8 =0x%08x\n", rtw_read32(padapter, 0x10F8));
	RTW_INFO("[PS-DBG] Reg_11F8 =0x%08x\n", rtw_read32(padapter, 0x11F8));
	RTW_INFO("[PS-DBG] Reg_4A4 =0x%08x\n", rtw_read32(padapter, 0x4A4));
	RTW_INFO("[PS-DBG] Reg_4A8 =0x%08x\n", rtw_read32(padapter, 0x4A8));

	if (cpu_state == CPU_EXCEPTION_CODE) {
		RTW_INFO("[PS-DBG] Reg_48C =0x%08x\n", rtw_read32(padapter, 0x48C));
		RTW_INFO("[PS-DBG] Reg_490 =0x%08x\n", rtw_read32(padapter, 0x490));
		RTW_INFO("[PS-DBG] Reg_494 =0x%08x\n", rtw_read32(padapter, 0x494));
		RTW_INFO("[PS-DBG] Reg_498 =0x%08x\n", rtw_read32(padapter, 0x498));
		RTW_INFO("[PS-DBG] Reg_49C =0x%08x\n", rtw_read32(padapter, 0x49C));
		RTW_INFO("[PS-DBG] Reg_4A0 =0x%08x\n", rtw_read32(padapter, 0x4A0));
		RTW_INFO("[PS-DBG] Reg_1BC =0x%08x\n", rtw_read32(padapter, 0x1BC));

		RTW_INFO("[PS-DBG] Reg_008 =0x%08x\n", rtw_read32(padapter, 0x08));
		RTW_INFO("[PS-DBG] Reg_2F0 =0x%08x\n", rtw_read32(padapter, 0x2F0));
		RTW_INFO("[PS-DBG] Reg_2F4 =0x%08x\n", rtw_read32(padapter, 0x2F4));
		RTW_INFO("[PS-DBG] Reg_2F8 =0x%08x\n", rtw_read32(padapter, 0x2F8));
		RTW_INFO("[PS-DBG] Reg_2FC =0x%08x\n", rtw_read32(padapter, 0x2FC));

		rtw_dump_fifo(RTW_DBGDUMP, padapter, 5, 0, 3072);
	}
}
#endif
static void rpwmtimeout_workitem_callback(struct work_struct *work)
{
	PADAPTER padapter;
	struct dvobj_priv *dvobj;
	struct pwrctrl_priv *pwrpriv;


	pwrpriv = container_of(work, struct pwrctrl_priv, rpwmtimeoutwi);
	dvobj = pwrctl_to_dvobj(pwrpriv);
	padapter = dvobj_get_primary_adapter(dvobj);

	if (!padapter)
		return;

	if (RTW_CANNOT_RUN(padapter))
		return;

	_enter_pwrlock(&pwrpriv->lock);
	if ((pwrpriv->rpwm == pwrpriv->cpwm) || (pwrpriv->cpwm >= PS_STATE_S2)) {
		RTW_INFO("%s: rpwm=0x%02X cpwm=0x%02X CPWM done!\n", __func__, pwrpriv->rpwm, pwrpriv->cpwm);
		goto exit;
	}

	if (pwrpriv->rpwm_retry++ < LPS_RPWM_RETRY_CNT) {
		u8 rpwm = (pwrpriv->rpwm | pwrpriv->tog | PS_ACK);

		rtw_hal_set_hwreg(padapter, HW_VAR_SET_RPWM, (u8 *)(&rpwm));

		pwrpriv->tog += 0x80;
		_set_timer(&pwrpriv->pwr_rpwm_timer, LPS_CPWM_TIMEOUT_MS);
		goto exit;
	}

	pwrpriv->rpwm_retry = 0;
	_exit_pwrlock(&pwrpriv->lock);

#if defined(DBG_CPWM_CHK_FAIL) && (defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C) || defined(CONFIG_RTL8822C))
	RTW_INFO("+%s: rpwm=0x%02X cpwm=0x%02X\n", __func__, pwrpriv->rpwm, pwrpriv->cpwm);
	rtw_cpwm_chk_fail_debug(padapter);
#endif

	if (rtw_read8(padapter, 0x100) != 0xEA) {
#if 1
		struct reportpwrstate_parm report;

		report.state = PS_STATE_S2;
		RTW_INFO("\n%s: FW already leave 32K!\n\n", __func__);
		cpwm_int_hdl(padapter, &report);
#else
		RTW_INFO("\n%s: FW already leave 32K!\n\n", __func__);
		cpwm_event_callback(&pwrpriv->cpwm_event);
#endif
		return;
	}

	_enter_pwrlock(&pwrpriv->lock);

	if ((pwrpriv->rpwm == pwrpriv->cpwm) || (pwrpriv->cpwm >= PS_STATE_S2)) {
		RTW_INFO("%s: cpwm=%d, nothing to do!\n", __func__, pwrpriv->cpwm);
		goto exit;
	}
	pwrpriv->brpwmtimeout = _TRUE;
	rtw_set_rpwm(padapter, pwrpriv->rpwm);
	pwrpriv->brpwmtimeout = _FALSE;

exit:
	_exit_pwrlock(&pwrpriv->lock);
}

/*
 * This function is a timer handler, can't do any IO in it.
 */
static void pwr_rpwm_timeout_handler(void *FunctionContext)
{
	PADAPTER padapter;
	struct pwrctrl_priv *pwrpriv;


	padapter = (PADAPTER)FunctionContext;
	pwrpriv = adapter_to_pwrctl(padapter);
	if (!padapter)
		return;

	if (RTW_CANNOT_RUN(padapter))
		return;

	RTW_INFO("+%s: rpwm=0x%02X cpwm=0x%02X\n", __func__, pwrpriv->rpwm, pwrpriv->cpwm);

	if ((pwrpriv->rpwm == pwrpriv->cpwm) || (pwrpriv->cpwm >= PS_STATE_S2)) {
		RTW_INFO("+%s: cpwm=%d, nothing to do!\n", __func__, pwrpriv->cpwm);
		return;
	}

	_set_workitem(&pwrpriv->rpwmtimeoutwi);
}
#endif /* CONFIG_LPS_RPWM_TIMER */

__inline static void register_task_alive(struct pwrctrl_priv *pwrctrl, u32 tag)
{
	pwrctrl->alives |= tag;
}

__inline static void unregister_task_alive(struct pwrctrl_priv *pwrctrl, u32 tag)
{
	pwrctrl->alives &= ~tag;
}


/*
 * Description:
 *	Check if the fw_pwrstate is okay for I/O.
 *	If not (cpwm is less than S2), then the sub-routine
 *	will raise the cpwm to be greater than or equal to S2.
 *
 *	Calling Context: Passive
 *
 *	Constraint:
 *		1. this function will request pwrctrl->lock
 *
 * Return Value:
 *	_SUCCESS	hardware is ready for I/O
 *	_FAIL		can't I/O right now
 */
s32 rtw_register_task_alive(PADAPTER padapter, u32 task)
{
	s32 res;
	struct pwrctrl_priv *pwrctrl;
	u8 pslv;


	res = _SUCCESS;
	pwrctrl = adapter_to_pwrctl(padapter);
	pslv = PS_STATE_S2;

	_enter_pwrlock(&pwrctrl->lock);

	register_task_alive(pwrctrl, task);

	if (pwrctrl->bFwCurrentInPSMode == _TRUE) {

		if (pwrctrl->cpwm < pslv) {
			if (pwrctrl->cpwm < PS_STATE_S2)
				res = _FAIL;
			if (pwrctrl->rpwm < pslv)
				rtw_set_rpwm(padapter, pslv);
		}
	}

	_exit_pwrlock(&pwrctrl->lock);

#ifdef CONFIG_DETECT_CPWM_BY_POLLING
	if (_FAIL == res) {
		if (pwrctrl->cpwm >= PS_STATE_S2)
			res = _SUCCESS;
	}
#endif /* CONFIG_DETECT_CPWM_BY_POLLING */


	return res;
}

/*
 * Description:
 *	If task is done, call this func. to power down firmware again.
 *
 *	Constraint:
 *		1. this function will request pwrctrl->lock
 *
 * Return Value:
 *	none
 */
void rtw_unregister_task_alive(PADAPTER padapter, u32 task)
{
	struct pwrctrl_priv *pwrctrl;
	u8 pslv;


	pwrctrl = adapter_to_pwrctl(padapter);
	pslv = PS_STATE_S0;

#ifdef CONFIG_BT_COEXIST
	if ((rtw_btcoex_IsBtDisabled(padapter) == _FALSE)
	    && (rtw_btcoex_IsBtControlLps(padapter) == _TRUE)) {
		u8 val8;

		val8 = rtw_btcoex_LpsVal(padapter);
		if (val8 & BIT(4))
			pslv = PS_STATE_S2;

	}
#endif /* CONFIG_BT_COEXIST */

	_enter_pwrlock(&pwrctrl->lock);

	unregister_task_alive(pwrctrl, task);

	if ((pwrctrl->pwr_mode != PS_MODE_ACTIVE)
	    && (pwrctrl->bFwCurrentInPSMode == _TRUE)) {

		if (pwrctrl->cpwm > pslv) {
			if ((pslv >= PS_STATE_S2) || (pwrctrl->alives == 0))
				rtw_set_rpwm(padapter, pslv);
		}
	}

	_exit_pwrlock(&pwrctrl->lock);

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
s32 rtw_register_tx_alive(PADAPTER padapter)
{
	s32 res;
	struct pwrctrl_priv *pwrctrl;
	u8 pslv;


	res = _SUCCESS;
	pwrctrl = adapter_to_pwrctl(padapter);
	pslv = PS_STATE_S2;

	_enter_pwrlock(&pwrctrl->lock);

	register_task_alive(pwrctrl, XMIT_ALIVE);

	if (pwrctrl->bFwCurrentInPSMode == _TRUE) {

		if (pwrctrl->cpwm < pslv) {
			if (pwrctrl->cpwm < PS_STATE_S2)
				res = _FAIL;
			if (pwrctrl->rpwm < pslv)
				rtw_set_rpwm(padapter, pslv);
		}
	}

	_exit_pwrlock(&pwrctrl->lock);

#ifdef CONFIG_DETECT_CPWM_BY_POLLING
	if (_FAIL == res) {
		if (pwrctrl->cpwm >= PS_STATE_S2)
			res = _SUCCESS;
	}
#endif /* CONFIG_DETECT_CPWM_BY_POLLING */


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
s32 rtw_register_cmd_alive(PADAPTER padapter)
{
	s32 res;
	struct pwrctrl_priv *pwrctrl;
	u8 pslv;


	res = _SUCCESS;
	pwrctrl = adapter_to_pwrctl(padapter);
	pslv = PS_STATE_S2;

	_enter_pwrlock(&pwrctrl->lock);

	register_task_alive(pwrctrl, CMD_ALIVE);

	if (pwrctrl->bFwCurrentInPSMode == _TRUE) {

		if (pwrctrl->cpwm < pslv) {
			if (pwrctrl->cpwm < PS_STATE_S2)
				res = _FAIL;
			if (pwrctrl->rpwm < pslv)
				rtw_set_rpwm(padapter, pslv);
		}
	}

	_exit_pwrlock(&pwrctrl->lock);

#ifdef CONFIG_DETECT_CPWM_BY_POLLING
	if (_FAIL == res) {
		if (pwrctrl->cpwm >= PS_STATE_S2)
			res = _SUCCESS;
	}
#endif /* CONFIG_DETECT_CPWM_BY_POLLING */


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
s32 rtw_register_rx_alive(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrctrl;


	pwrctrl = adapter_to_pwrctl(padapter);

	_enter_pwrlock(&pwrctrl->lock);

	register_task_alive(pwrctrl, RECV_ALIVE);

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
s32 rtw_register_evt_alive(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrctrl;


	pwrctrl = adapter_to_pwrctl(padapter);

	_enter_pwrlock(&pwrctrl->lock);

	register_task_alive(pwrctrl, EVT_ALIVE);

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
void rtw_unregister_tx_alive(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrctrl;
	_adapter *iface;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	u8 pslv, i;


	pwrctrl = adapter_to_pwrctl(padapter);
	pslv = PS_STATE_S0;

#ifdef CONFIG_BT_COEXIST
	if ((rtw_btcoex_IsBtDisabled(padapter) == _FALSE)
	    && (rtw_btcoex_IsBtControlLps(padapter) == _TRUE)) {
		u8 val8;

		val8 = rtw_btcoex_LpsVal(padapter);
		if (val8 & BIT(4))
			pslv = PS_STATE_S2;

	}
#endif /* CONFIG_BT_COEXIST */

#ifdef CONFIG_P2P_PS
	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if ((iface) && rtw_is_adapter_up(iface)) {
			if (iface->wdinfo.p2p_ps_mode > P2P_PS_NONE) {
				pslv = PS_STATE_S2;
				break;
			}
		}
	}
#endif
	_enter_pwrlock(&pwrctrl->lock);

	unregister_task_alive(pwrctrl, XMIT_ALIVE);

	if ((pwrctrl->pwr_mode != PS_MODE_ACTIVE)
	    && (pwrctrl->bFwCurrentInPSMode == _TRUE)) {

		if (pwrctrl->cpwm > pslv) {
			if ((pslv >= PS_STATE_S2) || (pwrctrl->alives == 0))
				rtw_set_rpwm(padapter, pslv);
		}
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
void rtw_unregister_cmd_alive(PADAPTER padapter)
{
	_adapter *iface;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct pwrctrl_priv *pwrctrl;
	u8 pslv, i;


	pwrctrl = adapter_to_pwrctl(padapter);
	pslv = PS_STATE_S0;

#ifdef CONFIG_BT_COEXIST
	if ((rtw_btcoex_IsBtDisabled(padapter) == _FALSE)
	    && (rtw_btcoex_IsBtControlLps(padapter) == _TRUE)) {
		u8 val8;

		val8 = rtw_btcoex_LpsVal(padapter);
		if (val8 & BIT(4))
			pslv = PS_STATE_S2;

	}
#endif /* CONFIG_BT_COEXIST */

#ifdef CONFIG_P2P_PS
	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if ((iface) && rtw_is_adapter_up(iface)) {
			if (iface->wdinfo.p2p_ps_mode > P2P_PS_NONE) {
				pslv = PS_STATE_S2;
				break;
			}
		}
	}
#endif

	_enter_pwrlock(&pwrctrl->lock);

	unregister_task_alive(pwrctrl, CMD_ALIVE);

	if ((pwrctrl->pwr_mode != PS_MODE_ACTIVE)
	    && (pwrctrl->bFwCurrentInPSMode == _TRUE)) {

		if (pwrctrl->cpwm > pslv) {
			if ((pslv >= PS_STATE_S2) || (pwrctrl->alives == 0))
				rtw_set_rpwm(padapter, pslv);
		}
	}

	_exit_pwrlock(&pwrctrl->lock);

}

/*
 * Caller: ISR
 */
void rtw_unregister_rx_alive(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrctrl;


	pwrctrl = adapter_to_pwrctl(padapter);

	_enter_pwrlock(&pwrctrl->lock);

	unregister_task_alive(pwrctrl, RECV_ALIVE);


	_exit_pwrlock(&pwrctrl->lock);

}

void rtw_unregister_evt_alive(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrctrl;


	pwrctrl = adapter_to_pwrctl(padapter);

	unregister_task_alive(pwrctrl, EVT_ALIVE);


	_exit_pwrlock(&pwrctrl->lock);

}
#endif	/* CONFIG_LPS_LCLK */

#ifdef CONFIG_RESUME_IN_WORKQUEUE
	static void resume_workitem_callback(struct work_struct *work);
#endif /* CONFIG_RESUME_IN_WORKQUEUE */

void rtw_init_pwrctrl_priv(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);
#ifdef CONFIG_WOWLAN
	struct registry_priv  *registry_par = &padapter->registrypriv;
#endif
#ifdef CONFIG_GPIO_WAKEUP
	u8 val8 = 0;
#endif

#if defined(CONFIG_CONCURRENT_MODE)
	if (!is_primary_adapter(padapter))
		return;
#endif

	_init_pwrlock(&pwrctrlpriv->lock);
	_init_pwrlock(&pwrctrlpriv->check_32k_lock);
	pwrctrlpriv->rf_pwrstate = rf_on;
	pwrctrlpriv->ips_enter_cnts = 0;
	pwrctrlpriv->ips_leave_cnts = 0;
	pwrctrlpriv->lps_enter_cnts = 0;
	pwrctrlpriv->lps_leave_cnts = 0;
	pwrctrlpriv->bips_processing = _FALSE;
#ifdef CONFIG_LPS_CHK_BY_TP
	pwrctrlpriv->lps_chk_by_tp = padapter->registrypriv.lps_chk_by_tp;
	pwrctrlpriv->lps_tx_tp_th = LPS_TX_TP_TH;
	pwrctrlpriv->lps_rx_tp_th = LPS_RX_TP_TH;
	pwrctrlpriv->lps_bi_tp_th = LPS_BI_TP_TH;
	pwrctrlpriv->lps_chk_cnt = pwrctrlpriv->lps_chk_cnt_th = LPS_TP_CHK_CNT;
	pwrctrlpriv->lps_tx_pkts = LPS_CHK_PKTS_TX;
	pwrctrlpriv->lps_rx_pkts = LPS_CHK_PKTS_RX;
#endif

	pwrctrlpriv->ips_mode = padapter->registrypriv.ips_mode;
	pwrctrlpriv->ips_mode_req = padapter->registrypriv.ips_mode;
	pwrctrlpriv->ips_deny_time = rtw_get_current_time();
	pwrctrlpriv->lps_level = padapter->registrypriv.lps_level;

	pwrctrlpriv->pwr_state_check_interval = RTW_PWR_STATE_CHK_INTERVAL;
	pwrctrlpriv->pwr_state_check_cnts = 0;
	#ifdef CONFIG_AUTOSUSPEND
	pwrctrlpriv->bInternalAutoSuspend = _FALSE;
	#endif
	pwrctrlpriv->bInSuspend = _FALSE;
	pwrctrlpriv->bkeepfwalive = _FALSE;

#ifdef CONFIG_AUTOSUSPEND
#ifdef SUPPORT_HW_RFOFF_DETECTED
	pwrctrlpriv->pwr_state_check_interval = (pwrctrlpriv->bHWPwrPindetect) ? 1000 : 2000;
#endif
#endif

	pwrctrlpriv->LpsIdleCount = 0;

#ifdef CONFIG_LPS_PG
	pwrctrlpriv->lpspg_rsvd_page_locate = 0;
#endif

	/* pwrctrlpriv->FWCtrlPSMode =padapter->registrypriv.power_mgnt; */ /* PS_MODE_MIN; */
	if (padapter->registrypriv.mp_mode == 1)
		pwrctrlpriv->power_mgnt = PS_MODE_ACTIVE ;
	else
		pwrctrlpriv->power_mgnt = padapter->registrypriv.power_mgnt; /* PS_MODE_MIN; */
	pwrctrlpriv->bLeisurePs = (PS_MODE_ACTIVE != pwrctrlpriv->power_mgnt) ? _TRUE : _FALSE;

	pwrctrlpriv->bFwCurrentInPSMode = _FALSE;

	pwrctrlpriv->rpwm = 0;
	pwrctrlpriv->cpwm = PS_STATE_S4;

	pwrctrlpriv->pwr_mode = PS_MODE_ACTIVE;
	pwrctrlpriv->smart_ps = padapter->registrypriv.smart_ps;
	pwrctrlpriv->bcn_ant_mode = 0;
	pwrctrlpriv->dtim = 0;

	pwrctrlpriv->tog = 0x80;
	pwrctrlpriv->rpwm_retry = 0;

	RTW_INFO("%s: IPS_mode=%d, LPS_mode=%d, LPS_level=%d\n", 
		__func__, pwrctrlpriv->ips_mode, pwrctrlpriv->power_mgnt, pwrctrlpriv->lps_level);

#ifdef CONFIG_LPS_LCLK
	rtw_hal_set_hwreg(padapter, HW_VAR_SET_RPWM, (u8 *)(&pwrctrlpriv->rpwm));

	_init_workitem(&pwrctrlpriv->cpwm_event, cpwm_event_callback, NULL);

	_init_workitem(&pwrctrlpriv->dma_event, dma_event_callback, NULL);

#ifdef CONFIG_LPS_RPWM_TIMER
	pwrctrlpriv->brpwmtimeout = _FALSE;
	_init_workitem(&pwrctrlpriv->rpwmtimeoutwi, rpwmtimeout_workitem_callback, NULL);
	rtw_init_timer(&pwrctrlpriv->pwr_rpwm_timer, padapter, pwr_rpwm_timeout_handler, padapter);
#endif /* CONFIG_LPS_RPWM_TIMER */
#endif /* CONFIG_LPS_LCLK */

	rtw_init_timer(&pwrctrlpriv->pwr_state_check_timer, padapter, pwr_state_check_handler, padapter);

	pwrctrlpriv->wowlan_mode = _FALSE;
	pwrctrlpriv->wowlan_ap_mode = _FALSE;
	pwrctrlpriv->wowlan_p2p_mode = _FALSE;
	pwrctrlpriv->wowlan_in_resume = _FALSE;
	pwrctrlpriv->wowlan_last_wake_reason = 0;

#ifdef CONFIG_RESUME_IN_WORKQUEUE
	_init_workitem(&pwrctrlpriv->resume_work, resume_workitem_callback, NULL);
	pwrctrlpriv->rtw_workqueue = create_singlethread_workqueue("rtw_workqueue");
#endif /* CONFIG_RESUME_IN_WORKQUEUE */

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
	pwrctrlpriv->early_suspend.suspend = NULL;
	rtw_register_early_suspend(pwrctrlpriv);
#endif /* CONFIG_HAS_EARLYSUSPEND || CONFIG_ANDROID_POWER */

#ifdef CONFIG_GPIO_WAKEUP
	/*default low active*/
	pwrctrlpriv->is_high_active = HIGH_ACTIVE_DEV2HST;
	pwrctrlpriv->hst2dev_high_active = HIGH_ACTIVE_HST2DEV;
#ifdef CONFIG_RTW_ONE_PIN_GPIO
	rtw_hal_switch_gpio_wl_ctrl(padapter, WAKEUP_GPIO_IDX, _TRUE);
	rtw_hal_set_input_gpio(padapter, WAKEUP_GPIO_IDX);
#else
	#ifdef CONFIG_WAKEUP_GPIO_INPUT_MODE
	if (pwrctrlpriv->is_high_active == 0)
		rtw_hal_set_input_gpio(padapter, WAKEUP_GPIO_IDX);
	else
		rtw_hal_set_output_gpio(padapter, WAKEUP_GPIO_IDX, 0);
	#else
	val8 = (pwrctrlpriv->is_high_active == 0) ? 1 : 0;
	rtw_hal_set_output_gpio(padapter, WAKEUP_GPIO_IDX, val8);
	RTW_INFO("%s: set GPIO_%d %d as default.\n",
		 __func__, WAKEUP_GPIO_IDX, val8);
	#endif /*CONFIG_WAKEUP_GPIO_INPUT_MODE*/
#endif /* CONFIG_RTW_ONE_PIN_GPIO */
#endif /* CONFIG_GPIO_WAKEUP */

#ifdef CONFIG_WOWLAN
	pwrctrlpriv->wowlan_power_mgmt = padapter->registrypriv.wow_power_mgnt;
	pwrctrlpriv->wowlan_lps_level = padapter->registrypriv.wow_lps_level;

	RTW_INFO("%s: WOW_LPS_mode=%d, WOW_LPS_level=%d\n",
		__func__, pwrctrlpriv->wowlan_power_mgmt, pwrctrlpriv->wowlan_lps_level);

	if (registry_par->wakeup_event & BIT(1))
		pwrctrlpriv->default_patterns_en = _TRUE;
	else
		pwrctrlpriv->default_patterns_en = _FALSE;

	rtw_wow_pattern_sw_reset(padapter);
#ifdef CONFIG_PNO_SUPPORT
	pwrctrlpriv->pno_inited = _FALSE;
	pwrctrlpriv->pnlo_info = NULL;
	pwrctrlpriv->pscan_info = NULL;
	pwrctrlpriv->pno_ssid_list = NULL;
#endif /* CONFIG_PNO_SUPPORT */
#ifdef CONFIG_WOW_PATTERN_HW_CAM
	_rtw_mutex_init(&pwrctrlpriv->wowlan_pattern_cam_mutex);
#endif
	pwrctrlpriv->wowlan_aoac_rpt_loc = 0;
#endif /* CONFIG_WOWLAN */

#ifdef CONFIG_LPS_POFF
	rtw_hal_set_hwreg(padapter, HW_VAR_LPS_POFF_INIT, 0);
#endif


}


void rtw_free_pwrctrl_priv(PADAPTER adapter)
{
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(adapter);

#if defined(CONFIG_CONCURRENT_MODE)
	if (!is_primary_adapter(adapter))
		return;
#endif


	/* _rtw_memset((unsigned char *)pwrctrlpriv, 0, sizeof(struct pwrctrl_priv)); */


#ifdef CONFIG_RESUME_IN_WORKQUEUE
	if (pwrctrlpriv->rtw_workqueue) {
		flush_workqueue(pwrctrlpriv->rtw_workqueue);
		destroy_workqueue(pwrctrlpriv->rtw_workqueue);
	}
#endif

#ifdef CONFIG_LPS_POFF
	rtw_hal_set_hwreg(adapter, HW_VAR_LPS_POFF_DEINIT, 0);
#endif

#ifdef CONFIG_LPS_LCLK
	_cancel_workitem_sync(&pwrctrlpriv->cpwm_event);
	_cancel_workitem_sync(&pwrctrlpriv->dma_event);
	#ifdef CONFIG_LPS_RPWM_TIMER
	_cancel_workitem_sync(&pwrctrlpriv->rpwmtimeoutwi);
	#endif
#endif /* CONFIG_LPS_LCLK */

#ifdef CONFIG_WOWLAN
#ifdef CONFIG_PNO_SUPPORT
	if (pwrctrlpriv->pnlo_info != NULL)
		printk("****** pnlo_info memory leak********\n");

	if (pwrctrlpriv->pscan_info != NULL)
		printk("****** pscan_info memory leak********\n");

	if (pwrctrlpriv->pno_ssid_list != NULL)
		printk("****** pno_ssid_list memory leak********\n");
#endif
#ifdef CONFIG_WOW_PATTERN_HW_CAM
	_rtw_mutex_free(&pwrctrlpriv->wowlan_pattern_cam_mutex);
#endif

#endif /* CONFIG_WOWLAN */

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
	rtw_unregister_early_suspend(pwrctrlpriv);
#endif /* CONFIG_HAS_EARLYSUSPEND || CONFIG_ANDROID_POWER */

	_free_pwrlock(&pwrctrlpriv->lock);
	_free_pwrlock(&pwrctrlpriv->check_32k_lock);

}

#ifdef CONFIG_RESUME_IN_WORKQUEUE
extern int rtw_resume_process(_adapter *padapter);

static void resume_workitem_callback(struct work_struct *work)
{
	struct pwrctrl_priv *pwrpriv = container_of(work, struct pwrctrl_priv, resume_work);
	struct dvobj_priv *dvobj = pwrctl_to_dvobj(pwrpriv);
	_adapter *adapter = dvobj_get_primary_adapter(dvobj);

	RTW_INFO("%s\n", __FUNCTION__);

	rtw_resume_process(adapter);

	rtw_resume_unlock_suspend();
}

void rtw_resume_in_workqueue(struct pwrctrl_priv *pwrpriv)
{
	/* accquire system's suspend lock preventing from falliing asleep while resume in workqueue */
	/* rtw_lock_suspend(); */

	rtw_resume_lock_suspend();

#if 1
	queue_work(pwrpriv->rtw_workqueue, &pwrpriv->resume_work);
#else
	_set_workitem(&pwrpriv->resume_work);
#endif
}
#endif /* CONFIG_RESUME_IN_WORKQUEUE */

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
inline bool rtw_is_earlysuspend_registered(struct pwrctrl_priv *pwrpriv)
{
	return (pwrpriv->early_suspend.suspend) ? _TRUE : _FALSE;
}

inline bool rtw_is_do_late_resume(struct pwrctrl_priv *pwrpriv)
{
	return (pwrpriv->do_late_resume) ? _TRUE : _FALSE;
}

inline void rtw_set_do_late_resume(struct pwrctrl_priv *pwrpriv, bool enable)
{
	pwrpriv->do_late_resume = enable;
}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
extern int rtw_resume_process(_adapter *padapter);
static void rtw_early_suspend(struct early_suspend *h)
{
	struct pwrctrl_priv *pwrpriv = container_of(h, struct pwrctrl_priv, early_suspend);
	RTW_INFO("%s\n", __FUNCTION__);

	rtw_set_do_late_resume(pwrpriv, _FALSE);
}

static void rtw_late_resume(struct early_suspend *h)
{
	struct pwrctrl_priv *pwrpriv = container_of(h, struct pwrctrl_priv, early_suspend);
	struct dvobj_priv *dvobj = pwrctl_to_dvobj(pwrpriv);
	_adapter *adapter = dvobj_get_primary_adapter(dvobj);

	RTW_INFO("%s\n", __FUNCTION__);

	if (pwrpriv->do_late_resume) {
		rtw_set_do_late_resume(pwrpriv, _FALSE);
		rtw_resume_process(adapter);
	}
}

void rtw_register_early_suspend(struct pwrctrl_priv *pwrpriv)
{
	RTW_INFO("%s\n", __FUNCTION__);

	/* jeff: set the early suspend level before blank screen, so we wll do late resume after scree is lit */
	pwrpriv->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 20;
	pwrpriv->early_suspend.suspend = rtw_early_suspend;
	pwrpriv->early_suspend.resume = rtw_late_resume;
	register_early_suspend(&pwrpriv->early_suspend);


}

void rtw_unregister_early_suspend(struct pwrctrl_priv *pwrpriv)
{
	RTW_INFO("%s\n", __FUNCTION__);

	rtw_set_do_late_resume(pwrpriv, _FALSE);

	if (pwrpriv->early_suspend.suspend)
		unregister_early_suspend(&pwrpriv->early_suspend);

	pwrpriv->early_suspend.suspend = NULL;
	pwrpriv->early_suspend.resume = NULL;
}
#endif /* CONFIG_HAS_EARLYSUSPEND */

#ifdef CONFIG_ANDROID_POWER
#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	extern int rtw_resume_process(PADAPTER padapter);
#endif
static void rtw_early_suspend(android_early_suspend_t *h)
{
	struct pwrctrl_priv *pwrpriv = container_of(h, struct pwrctrl_priv, early_suspend);
	RTW_INFO("%s\n", __FUNCTION__);

	rtw_set_do_late_resume(pwrpriv, _FALSE);
}

static void rtw_late_resume(android_early_suspend_t *h)
{
	struct pwrctrl_priv *pwrpriv = container_of(h, struct pwrctrl_priv, early_suspend);
	struct dvobj_priv *dvobj = pwrctl_to_dvobj(pwrpriv);
	_adapter *adapter = dvobj_get_primary_adapter(dvobj);

	RTW_INFO("%s\n", __FUNCTION__);
	if (pwrpriv->do_late_resume) {
#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		rtw_set_do_late_resume(pwrpriv, _FALSE);
		rtw_resume_process(adapter);
#endif
	}
}

void rtw_register_early_suspend(struct pwrctrl_priv *pwrpriv)
{
	RTW_INFO("%s\n", __FUNCTION__);

	/* jeff: set the early suspend level before blank screen, so we wll do late resume after scree is lit */
	pwrpriv->early_suspend.level = ANDROID_EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 20;
	pwrpriv->early_suspend.suspend = rtw_early_suspend;
	pwrpriv->early_suspend.resume = rtw_late_resume;
	android_register_early_suspend(&pwrpriv->early_suspend);
}

void rtw_unregister_early_suspend(struct pwrctrl_priv *pwrpriv)
{
	RTW_INFO("%s\n", __FUNCTION__);

	rtw_set_do_late_resume(pwrpriv, _FALSE);

	if (pwrpriv->early_suspend.suspend)
		android_unregister_early_suspend(&pwrpriv->early_suspend);

	pwrpriv->early_suspend.suspend = NULL;
	pwrpriv->early_suspend.resume = NULL;
}
#endif /* CONFIG_ANDROID_POWER */

u8 rtw_interface_ps_func(_adapter *padapter, HAL_INTF_PS_FUNC efunc_id, u8 *val)
{
	u8 bResult = _TRUE;
	rtw_hal_intf_ps_func(padapter, efunc_id, val);

	return bResult;
}


inline void rtw_set_ips_deny(_adapter *padapter, u32 ms)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	pwrpriv->ips_deny_time = rtw_get_current_time() + rtw_ms_to_systime(ms);
}

/*
* rtw_pwr_wakeup - Wake the NIC up from: 1)IPS. 2)USB autosuspend
* @adapter: pointer to _adapter structure
* @ips_deffer_ms: the ms wiil prevent from falling into IPS after wakeup
* Return _SUCCESS or _FAIL
*/

int _rtw_pwr_wakeup(_adapter *padapter, u32 ips_deffer_ms, const char *caller)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(dvobj);
	struct mlme_priv *pmlmepriv;
	int ret = _SUCCESS;
	systime start = rtw_get_current_time();

	/*RTW_INFO(FUNC_ADPT_FMT "===>\n", FUNC_ADPT_ARG(padapter));*/
	/* for LPS */
	LeaveAllPowerSaveMode(padapter);

	/* IPS still bound with primary adapter */
	padapter = GET_PRIMARY_ADAPTER(padapter);
	pmlmepriv = &padapter->mlmepriv;

	if (rtw_time_after(rtw_get_current_time() + rtw_ms_to_systime(ips_deffer_ms), pwrpriv->ips_deny_time))
		pwrpriv->ips_deny_time = rtw_get_current_time() + rtw_ms_to_systime(ips_deffer_ms);


	if (pwrpriv->ps_processing) {
		RTW_INFO("%s wait ps_processing...\n", __func__);
		while (pwrpriv->ps_processing && rtw_get_passing_time_ms(start) <= 3000)
			rtw_msleep_os(10);
		if (pwrpriv->ps_processing)
			RTW_INFO("%s wait ps_processing timeout\n", __func__);
		else
			RTW_INFO("%s wait ps_processing done\n", __func__);
	}

#ifdef DBG_CONFIG_ERROR_DETECT
	if (rtw_hal_sreset_inprogress(padapter)) {
		RTW_INFO("%s wait sreset_inprogress...\n", __func__);
		while (rtw_hal_sreset_inprogress(padapter) && rtw_get_passing_time_ms(start) <= 4000)
			rtw_msleep_os(10);
		if (rtw_hal_sreset_inprogress(padapter))
			RTW_INFO("%s wait sreset_inprogress timeout\n", __func__);
		else
			RTW_INFO("%s wait sreset_inprogress done\n", __func__);
	}
#endif

	if (pwrpriv->bInSuspend
		#ifdef CONFIG_AUTOSUSPEND
		&& pwrpriv->bInternalAutoSuspend == _FALSE
		#endif
		) {
		RTW_INFO("%s wait bInSuspend...\n", __func__);
		while (pwrpriv->bInSuspend
		       && ((rtw_get_passing_time_ms(start) <= 3000 && !rtw_is_do_late_resume(pwrpriv))
			|| (rtw_get_passing_time_ms(start) <= 500 && rtw_is_do_late_resume(pwrpriv)))
		      )
			rtw_msleep_os(10);
		if (pwrpriv->bInSuspend)
			RTW_INFO("%s wait bInSuspend timeout\n", __func__);
		else
			RTW_INFO("%s wait bInSuspend done\n", __func__);
	}

	/* System suspend is not allowed to wakeup */
	if ((_TRUE == pwrpriv->bInSuspend)
		#ifdef CONFIG_AUTOSUSPEND
		&& (pwrpriv->bInternalAutoSuspend == _FALSE)
		#endif
	) {
		ret = _FAIL;
		goto exit;
	}
#ifdef CONFIG_AUTOSUSPEND
	/* usb autosuspend block??? */
	if ((pwrpriv->bInternalAutoSuspend == _TRUE)  && (padapter->net_closed == _TRUE)) {
		ret = _FAIL;
		goto exit;
	}
#endif
	/* I think this should be check in IPS, LPS, autosuspend functions... */
	if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) {
#if defined(CONFIG_BT_COEXIST) && defined (CONFIG_AUTOSUSPEND)
		if (_TRUE == pwrpriv->bInternalAutoSuspend) {
			if (0 == pwrpriv->autopm_cnt) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33))
				if (usb_autopm_get_interface(adapter_to_dvobj(padapter)->pusbintf) < 0)
					RTW_INFO("can't get autopm:\n");
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 20))
				usb_autopm_disable(adapter_to_dvobj(padapter)->pusbintf);
#else
				usb_autoresume_device(adapter_to_dvobj(padapter)->pusbdev, 1);
#endif
				pwrpriv->autopm_cnt++;
			}
#endif	/* #if defined (CONFIG_BT_COEXIST) && defined (CONFIG_AUTOSUSPEND) */
			ret = _SUCCESS;
			goto exit;
#if defined(CONFIG_BT_COEXIST) && defined (CONFIG_AUTOSUSPEND)
		}
#endif	/* #if defined (CONFIG_BT_COEXIST) && defined (CONFIG_AUTOSUSPEND) */
	}

	if (rf_off == pwrpriv->rf_pwrstate) {
#ifdef CONFIG_USB_HCI
#ifdef CONFIG_AUTOSUSPEND
		if (pwrpriv->brfoffbyhw == _TRUE) {
			RTW_INFO("hw still in rf_off state ...........\n");
			ret = _FAIL;
			goto exit;
		} else if (padapter->registrypriv.usbss_enable) {
			RTW_INFO("%s call autoresume_enter....\n", __FUNCTION__);
			if (_FAIL ==  autoresume_enter(padapter)) {
				RTW_INFO("======> autoresume fail.............\n");
				ret = _FAIL;
				goto exit;
			}
		} else
#endif
#endif
		{
#ifdef CONFIG_IPS
			RTW_INFO("%s call ips_leave....\n", __FUNCTION__);
			if (_FAIL ==  ips_leave(padapter)) {
				RTW_INFO("======> ips_leave fail.............\n");
				ret = _FAIL;
				goto exit;
			}
#endif
		}
	}

	/* TODO: the following checking need to be merged... */
	if (rtw_is_drv_stopped(padapter)
	    || !padapter->bup
	    || !rtw_is_hw_init_completed(padapter)
	   ) {
		RTW_INFO("%s: bDriverStopped=%s, bup=%d, hw_init_completed=%u\n"
			 , caller
			 , rtw_is_drv_stopped(padapter) ? "True" : "False"
			 , padapter->bup
			 , rtw_get_hw_init_completed(padapter));
		ret = _FALSE;
		goto exit;
	}

exit:
	if (rtw_time_after(rtw_get_current_time() + rtw_ms_to_systime(ips_deffer_ms), pwrpriv->ips_deny_time))
		pwrpriv->ips_deny_time = rtw_get_current_time() + rtw_ms_to_systime(ips_deffer_ms);
	/*RTW_INFO(FUNC_ADPT_FMT "<===\n", FUNC_ADPT_ARG(padapter));*/
	return ret;

}

int rtw_pm_set_lps(_adapter *padapter, u8 mode)
{
	int	ret = 0;
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	if (mode < PS_MODE_NUM) {
		if (pwrctrlpriv->power_mgnt != mode) {
			if (PS_MODE_ACTIVE == mode)
				LeaveAllPowerSaveMode(padapter);
			else
				pwrctrlpriv->LpsIdleCount = 2;
			pwrctrlpriv->power_mgnt = mode;
			pwrctrlpriv->bLeisurePs = (PS_MODE_ACTIVE != pwrctrlpriv->power_mgnt) ? _TRUE : _FALSE;
		}
	} else
		ret = -EINVAL;

	return ret;
}

int rtw_pm_set_lps_level(_adapter *padapter, u8 level)
{
	int	ret = 0;
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	if (level < LPS_LEVEL_MAX) {
		if (pwrctrlpriv->lps_level != level) {
			rtw_ps_deny(padapter, PS_DENY_UPDATE_LPS_CONF);
			#ifdef CONFIG_LPS
			rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_LEAVE, RTW_CMDF_WAIT_ACK);
			#endif
			pwrctrlpriv->lps_level = level;
			rtw_ps_deny_cancel(padapter, PS_DENY_UPDATE_LPS_CONF);
		}
	} else
		ret = -EINVAL;

	return ret;
}

#ifdef CONFIG_WOWLAN
int rtw_pm_set_wow_lps(_adapter *padapter, u8 mode)
{
	int	ret = 0;
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	if (mode < PS_MODE_NUM) {
		if (pwrctrlpriv->wowlan_power_mgmt != mode) 
			pwrctrlpriv->wowlan_power_mgmt = mode;
	} else
		ret = -EINVAL;

	return ret;
}
int rtw_pm_set_wow_lps_level(_adapter *padapter, u8 level)
{
	int	ret = 0;
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	if (level < LPS_LEVEL_MAX)
		pwrctrlpriv->wowlan_lps_level = level;
	else
		ret = -EINVAL;

	return ret;
}
#endif /* CONFIG_WOWLAN */

int rtw_pm_set_ips(_adapter *padapter, u8 mode)
{
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	if (mode == IPS_NORMAL || mode == IPS_LEVEL_2) {
		rtw_ips_mode_req(pwrctrlpriv, mode);
		RTW_INFO("%s %s\n", __FUNCTION__, mode == IPS_NORMAL ? "IPS_NORMAL" : "IPS_LEVEL_2");
		return 0;
	} else if (mode == IPS_NONE) {
		rtw_ips_mode_req(pwrctrlpriv, mode);
		RTW_INFO("%s %s\n", __FUNCTION__, "IPS_NONE");
		if (!rtw_is_surprise_removed(padapter) && (_FAIL == rtw_pwr_wakeup(padapter)))
			return -EFAULT;
	} else
		return -EINVAL;
	return 0;
}

/*
 * ATTENTION:
 *	This function will request pwrctrl LOCK!
 */
void rtw_ps_deny(PADAPTER padapter, PS_DENY_REASON reason)
{
	struct pwrctrl_priv *pwrpriv;

	/* 	RTW_INFO("+" FUNC_ADPT_FMT ": Request PS deny for %d (0x%08X)\n",
	 *		FUNC_ADPT_ARG(padapter), reason, BIT(reason)); */

	pwrpriv = adapter_to_pwrctl(padapter);

	_enter_pwrlock(&pwrpriv->lock);
	if (pwrpriv->ps_deny & BIT(reason)) {
		RTW_INFO(FUNC_ADPT_FMT ": [WARNING] Reason %d had been set before!!\n",
			 FUNC_ADPT_ARG(padapter), reason);
	}
	pwrpriv->ps_deny |= BIT(reason);
	_exit_pwrlock(&pwrpriv->lock);

	/* 	RTW_INFO("-" FUNC_ADPT_FMT ": Now PS deny for 0x%08X\n",
	 *		FUNC_ADPT_ARG(padapter), pwrpriv->ps_deny); */
}

/*
 * ATTENTION:
 *	This function will request pwrctrl LOCK!
 */
void rtw_ps_deny_cancel(PADAPTER padapter, PS_DENY_REASON reason)
{
	struct pwrctrl_priv *pwrpriv;


	/* 	RTW_INFO("+" FUNC_ADPT_FMT ": Cancel PS deny for %d(0x%08X)\n",
	 *		FUNC_ADPT_ARG(padapter), reason, BIT(reason)); */

	pwrpriv = adapter_to_pwrctl(padapter);

	_enter_pwrlock(&pwrpriv->lock);
	if ((pwrpriv->ps_deny & BIT(reason)) == 0) {
		RTW_INFO(FUNC_ADPT_FMT ": [ERROR] Reason %d had been canceled before!!\n",
			 FUNC_ADPT_ARG(padapter), reason);
	}
	pwrpriv->ps_deny &= ~BIT(reason);
	_exit_pwrlock(&pwrpriv->lock);

	/* 	RTW_INFO("-" FUNC_ADPT_FMT ": Now PS deny for 0x%08X\n",
	 *		FUNC_ADPT_ARG(padapter), pwrpriv->ps_deny); */
}

/*
 * ATTENTION:
 *	Before calling this function pwrctrl lock should be occupied already,
 *	otherwise it may return incorrect value.
 */
u32 rtw_ps_deny_get(PADAPTER padapter)
{
	u32 deny;


	deny = adapter_to_pwrctl(padapter)->ps_deny;

	return deny;
}

static void _rtw_ssmps(_adapter *adapter, struct sta_info *sta)
{
	struct mlme_ext_priv *pmlmeext = &(adapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	if (MLME_IS_STA(adapter)) {
		issue_action_SM_PS_wait_ack(adapter , get_my_bssid(&(pmlmeinfo->network)),
			sta->cmn.sm_ps, 3 , 1);
	}
	else if (MLME_IS_AP(adapter)) {

	}
	rtw_phydm_ra_registed(adapter, sta);
}
void rtw_ssmps_enter(_adapter *adapter, struct sta_info *sta)
{
	if (sta->cmn.sm_ps == SM_PS_STATIC)
		return;

	RTW_INFO(ADPT_FMT" STA [" MAC_FMT "]\n", ADPT_ARG(adapter), MAC_ARG(sta->cmn.mac_addr));

	sta->cmn.sm_ps = SM_PS_STATIC;
	_rtw_ssmps(adapter, sta);
}
void rtw_ssmps_leave(_adapter *adapter, struct sta_info *sta)
{
	if (sta->cmn.sm_ps == SM_PS_DISABLE)
		return;

	RTW_INFO(ADPT_FMT" STA [" MAC_FMT "] \n", ADPT_ARG(adapter), MAC_ARG(sta->cmn.mac_addr));
	sta->cmn.sm_ps = SM_PS_DISABLE;
	_rtw_ssmps(adapter, sta);
}

