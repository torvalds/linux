/******************************************************************************
 *
 * Copyright(c) 2007 - 2020 Realtek Corporation.
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

#ifdef CONFIG_IOCTL_CFG80211
u8 rtw_roch_stay_in_cur_chan(_adapter *padapter)
{
	int i;
	_adapter *iface;
	struct mlme_priv *pmlmepriv;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	u8 rst = _FALSE;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (iface) {
			pmlmepriv = &iface->mlmepriv;

			if (check_fwstate(pmlmepriv, WIFI_UNDER_LINKING | WIFI_UNDER_WPS | WIFI_UNDER_KEY_HANDSHAKE) == _TRUE) {
				RTW_INFO(ADPT_FMT"- WIFI_UNDER_LINKING |WIFI_UNDER_WPS | WIFI_UNDER_KEY_HANDSHAKE (mlme state:0x%x)\n",
						ADPT_ARG(iface), get_fwstate(&iface->mlmepriv));
				rst = _TRUE;
				break;
			}
			#ifdef CONFIG_AP_MODE
			if (MLME_IS_AP(iface) || MLME_IS_MESH(iface)) {
				if (rtw_ap_sta_states_check(iface) == _TRUE) {
					rst = _TRUE;
					break;
				}
			}
			#endif
		}
	}

	return rst;
}

static int rtw_ro_ch_handler(_adapter *adapter, u8 *buf)
{
	int ret = H2C_SUCCESS;
	struct rtw_roch_parm *roch_parm = (struct rtw_roch_parm *)buf;
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(adapter);
	struct roch_info *prochinfo = &adapter->rochinfo;
#ifdef CONFIG_CONCURRENT_MODE
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
#endif
	u8 ready_on_channel = _FALSE;
	u8 remain_ch;
	unsigned int duration;

	_enter_critical_mutex(&pwdev_priv->roch_mutex, NULL);

	if (rtw_cfg80211_get_is_roch(adapter) != _TRUE)
		goto exit;

	remain_ch = (u8)ieee80211_frequency_to_channel(roch_parm->ch.center_freq);
	duration = roch_parm->duration;

	RTW_INFO(FUNC_ADPT_FMT" ch:%u duration:%d, cookie:0x%llx\n"
		, FUNC_ADPT_ARG(adapter), remain_ch, roch_parm->duration, roch_parm->cookie);

	if (roch_parm->wdev && roch_parm->cookie) {
		if (prochinfo->ro_ch_wdev != roch_parm->wdev) {
			RTW_WARN(FUNC_ADPT_FMT" ongoing wdev:%p, wdev:%p\n"
				, FUNC_ADPT_ARG(adapter), prochinfo->ro_ch_wdev, roch_parm->wdev);
			rtw_warn_on(1);
		}

		if (prochinfo->remain_on_ch_cookie != roch_parm->cookie) {
			RTW_WARN(FUNC_ADPT_FMT" ongoing cookie:0x%llx, cookie:0x%llx\n"
				, FUNC_ADPT_ARG(adapter), prochinfo->remain_on_ch_cookie, roch_parm->cookie);
			rtw_warn_on(1);
		}
	}

	if (rtw_roch_stay_in_cur_chan(adapter) == _TRUE) {
		remain_ch = rtw_mi_get_union_chan(adapter);
		RTW_INFO(FUNC_ADPT_FMT" stay in union ch:%d\n", FUNC_ADPT_ARG(adapter), remain_ch);
	}

	#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_check_status(adapter, MI_LINKED) && (0 != rtw_mi_get_union_chan(adapter))) {
		if ((remain_ch != rtw_mi_get_union_chan(adapter)) && !check_fwstate(&adapter->mlmepriv, WIFI_ASOC_STATE)) {
			if (remain_ch != pmlmeext->cur_channel
				#ifdef RTW_ROCH_BACK_OP
				|| ATOMIC_READ(&pwdev_priv->switch_ch_to) == 1
				#endif
			) {
				rtw_leave_opch(adapter);

				#ifdef RTW_ROCH_BACK_OP
				RTW_INFO("%s, set switch ch timer, duration=%d\n", __func__, prochinfo->max_away_dur);
				ATOMIC_SET(&pwdev_priv->switch_ch_to, 0);
				/* remain_ch is not same as union channel. duration is max_away_dur to
				 * back to AP's channel.
				 */
				_set_timer(&prochinfo->ap_roch_ch_switch_timer, prochinfo->max_away_dur);
				#endif
			}
		}
		ready_on_channel = _TRUE;
	} else
	#endif /* CONFIG_CONCURRENT_MODE */
	{
		if (remain_ch != rtw_get_oper_ch(adapter))
			ready_on_channel = _TRUE;
	}

	if (ready_on_channel == _TRUE) {
		#ifndef RTW_SINGLE_WIPHY
		if (!check_fwstate(&adapter->mlmepriv, WIFI_ASOC_STATE))
		#endif
		{
			#ifdef CONFIG_CONCURRENT_MODE
			if (rtw_get_oper_ch(adapter) != remain_ch)
			#endif
			{
				/* if (!padapter->mlmepriv.LinkDetectInfo.bBusyTraffic) */
				set_channel_bwmode(adapter, remain_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
			}
		}
	}

	#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_ScanNotify(adapter, _TRUE);
	#endif

	RTW_INFO("%s, set ro ch timer, duration=%d\n", __func__, duration);
	_set_timer(&prochinfo->remain_on_ch_timer, duration);

exit:
	_exit_critical_mutex(&pwdev_priv->roch_mutex, NULL);

	return ret;
}

static int rtw_cancel_ro_ch_handler(_adapter *padapter, u8 *buf)
{
	int ret = H2C_SUCCESS;
	struct rtw_roch_parm *roch_parm = (struct rtw_roch_parm *)buf;
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);
	struct roch_info *prochinfo = &padapter->rochinfo;
	struct wireless_dev *wdev;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
#endif
	u8 ch, bw, offset;

	_enter_critical_mutex(&pwdev_priv->roch_mutex, NULL);

	if (rtw_cfg80211_get_is_roch(padapter) != _TRUE)
		goto exit;

	if (roch_parm->wdev && roch_parm->cookie) {
		if (prochinfo->ro_ch_wdev != roch_parm->wdev) {
			RTW_WARN(FUNC_ADPT_FMT" ongoing wdev:%p, wdev:%p\n"
				, FUNC_ADPT_ARG(padapter), prochinfo->ro_ch_wdev, roch_parm->wdev);
			rtw_warn_on(1);
		}

		if (prochinfo->remain_on_ch_cookie != roch_parm->cookie) {
			RTW_WARN(FUNC_ADPT_FMT" ongoing cookie:0x%llx, cookie:0x%llx\n"
				, FUNC_ADPT_ARG(padapter), prochinfo->remain_on_ch_cookie, roch_parm->cookie);
			rtw_warn_on(1);
		}
	}

#if defined(RTW_ROCH_BACK_OP) && defined(CONFIG_CONCURRENT_MODE)
	_cancel_timer_ex(&prochinfo->ap_roch_ch_switch_timer);
	ATOMIC_SET(&pwdev_priv->switch_ch_to, 1);
#endif

	if (rtw_mi_get_ch_setting_union(padapter, &ch, &bw, &offset) != 0) {
		if (0)
			RTW_INFO(FUNC_ADPT_FMT" back to linked/linking union - ch:%u, bw:%u, offset:%u\n",
				 FUNC_ADPT_ARG(padapter), ch, bw, offset);
#ifdef CONFIG_P2P
	} else if (adapter_wdev_data(padapter)->p2p_enabled && pwdinfo->listen_channel) {
		ch = pwdinfo->listen_channel;
		bw = CHANNEL_WIDTH_20;
		offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		if (0)
			RTW_INFO(FUNC_ADPT_FMT" back to listen ch - ch:%u, bw:%u, offset:%u\n",
				 FUNC_ADPT_ARG(padapter), ch, bw, offset);
#endif
	} else {
		ch = prochinfo->restore_channel;
		bw = CHANNEL_WIDTH_20;
		offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		if (0)
			RTW_INFO(FUNC_ADPT_FMT" back to restore ch - ch:%u, bw:%u, offset:%u\n",
				 FUNC_ADPT_ARG(padapter), ch, bw, offset);
	}

	set_channel_bwmode(padapter, ch, offset, bw);
	rtw_back_opch(padapter);
#ifdef CONFIG_P2P
	rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));
#ifdef CONFIG_DEBUG_CFG80211
	RTW_INFO("%s, role=%d, p2p_state=%d\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo));
#endif
#endif

	wdev = prochinfo->ro_ch_wdev;

	rtw_cfg80211_set_is_roch(padapter, _FALSE);
	prochinfo->ro_ch_wdev = NULL;
	rtw_cfg80211_set_last_ro_ch_time(padapter);

	rtw_cfg80211_remain_on_channel_expired(wdev
		, prochinfo->remain_on_ch_cookie
		, &prochinfo->remain_on_ch_channel
		, prochinfo->remain_on_ch_type, GFP_KERNEL);

	RTW_INFO("cfg80211_remain_on_channel_expired cookie:0x%llx\n"
		, prochinfo->remain_on_ch_cookie);

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_ScanNotify(padapter, _FALSE);
#endif

exit:
	_exit_critical_mutex(&pwdev_priv->roch_mutex, NULL);

	return ret;
}

static void rtw_ro_ch_timer_process(void *FunctionContext)
{
	_adapter *adapter = (_adapter *)FunctionContext;

	rtw_cancel_roch_cmd(adapter, 0, NULL, 0);
}
#endif /* CONFIG_IOCTL_CFG80211 */

#if (defined(CONFIG_P2P) && defined(CONFIG_CONCURRENT_MODE)) || defined(CONFIG_IOCTL_CFG80211)
s32 rtw_roch_wk_hdl(_adapter *padapter, int intCmdType, u8 *buf)
{
	int ret = H2C_SUCCESS;

	switch (intCmdType) {

#ifdef CONFIG_IOCTL_CFG80211
	case ROCH_RO_CH_WK:
		ret = rtw_ro_ch_handler(padapter, buf);
		break;
	case ROCH_CANCEL_RO_CH_WK:
		ret = rtw_cancel_ro_ch_handler(padapter, buf);
		break;
#endif

#ifdef CONFIG_CONCURRENT_MODE
	case ROCH_AP_ROCH_CH_SWITCH_PROCESS_WK:
		rtw_concurrent_handler(padapter);
		break;
#endif

	default:
		rtw_warn_on(1);
		break;
	}

	return ret;
}

static int get_roch_parm_size(struct rtw_roch_parm *roch_parm)
{
#ifdef CONFIG_IOCTL_CFG80211
	return (roch_parm ? sizeof(*roch_parm) : 0);
#else
	rtw_warn_on(roch_parm);
	return 0;
#endif
}

u8 rtw_roch_wk_cmd(_adapter *padapter, int intCmdType, struct rtw_roch_parm *roch_parm, u8 flags)
{
	struct cmd_obj	*ph2c = NULL;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm = NULL;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	struct submit_ctx sctx;
	u8	res = _SUCCESS;

	if (flags & RTW_CMDF_DIRECTLY) {
		/* no need to enqueue, do the cmd hdl directly and free cmd parameter */
		if (H2C_SUCCESS != rtw_roch_wk_hdl(padapter, intCmdType, (u8 *)roch_parm))
			res = _FAIL;
		goto free_parm;
	} else {
		ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
		if (!ph2c) {
			res = _FAIL;
			goto free_parm;
		}

		pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
		if (!pdrvextra_cmd_parm) {
			res = _FAIL;
			goto free_parm;
		}

		pdrvextra_cmd_parm->ec_id = ROCH_WK_CID;
		pdrvextra_cmd_parm->type = intCmdType;
		pdrvextra_cmd_parm->size = get_roch_parm_size(roch_parm);
		pdrvextra_cmd_parm->pbuf = (u8 *)roch_parm;

		init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, CMD_SET_DRV_EXTRA);

		if (flags & RTW_CMDF_WAIT_ACK) {
			ph2c->sctx = &sctx;
			rtw_sctx_init(&sctx, 10 * 1000);
		}

		res = rtw_enqueue_cmd(pcmdpriv, ph2c);

		if (res == _SUCCESS && (flags & RTW_CMDF_WAIT_ACK)) {
			rtw_sctx_wait(&sctx, __func__);
			_enter_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
			if (sctx.status == RTW_SCTX_SUBMITTED)
				ph2c->sctx = NULL;
			_exit_critical_mutex(&pcmdpriv->sctx_mutex, NULL);
			if (sctx.status != RTW_SCTX_DONE_SUCCESS)
				res = _FAIL;
		}
	}

	return res;

free_parm:
	if (roch_parm)
		rtw_mfree((u8 *)roch_parm, get_roch_parm_size(roch_parm));
	if (ph2c)
		rtw_mfree((u8 *)ph2c, sizeof(*ph2c));

	return res;
}

#ifdef CONFIG_CONCURRENT_MODE
void rtw_ap_roch_ch_switch_timer_process(void *ctx)
{
	_adapter *adapter = (_adapter *)ctx;
#ifdef CONFIG_IOCTL_CFG80211
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(adapter);
#endif

#ifdef CONFIG_IOCTL_CFG80211
	ATOMIC_SET(&pwdev_priv->switch_ch_to, 1);
#endif

	rtw_roch_wk_cmd(adapter, ROCH_AP_ROCH_CH_SWITCH_PROCESS_WK, NULL, 0);
}

static bool chk_need_stay_in_cur_chan(_adapter *padapter)
{
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	/* When CONFIG_FULL_CH_IN_P2P_HANDSHAKE is defined and the
	 * interface is in the P2P_STATE_GONEGO_OK state, do not let the
	 * interface switch to the listen channel, because the interface will
	 * switch to the OP channel after the GO negotiation is successful.
	 */
	if (padapter->registrypriv.full_ch_in_p2p_handshake == 1 && rtw_p2p_chk_state(pwdinfo , P2P_STATE_GONEGO_OK)) {
		RTW_INFO("%s, No linked interface now, but go nego ok, do not back to listen channel\n", __func__);
		return _TRUE;
	}
#endif

	return _FALSE;
}

static bool chk_driver_interface(_adapter *padapter, u8 driver_interface)
{
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	if (pwdinfo->driver_interface == driver_interface)
		return _TRUE;
#elif defined(CONFIG_IOCTL_CFG80211)
	if (driver_interface == DRIVER_CFG80211)
		return _TRUE;
#endif

	return _FALSE;
}

static u8 get_remain_ch(_adapter *padapter)
{
	struct roch_info *prochinfo = &padapter->rochinfo;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
#endif
	u8 remain_ch;

#ifdef CONFIG_P2P
	remain_ch = pwdinfo->listen_channel;
#elif defined(CONFIG_IOCTL_CFG80211)
	if (chk_driver_interface(padapter, DRIVER_CFG80211))
		remain_ch = ieee80211_frequency_to_channel(prochinfo->remain_on_ch_channel.center_freq);
	else
		rtw_warn_on(1);
#endif

	return remain_ch;
}

void rtw_concurrent_handler(_adapter	*padapter)
{
#ifdef CONFIG_IOCTL_CFG80211
	struct rtw_wdev_priv	*pwdev_priv = adapter_wdev_data(padapter);
#endif
	struct dvobj_priv	*pdvobj = adapter_to_dvobj(padapter);
	struct roch_info	*prochinfo = &padapter->rochinfo;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &padapter->wdinfo;
	u8			val8;
#endif
	u8			remain_ch = get_remain_ch(padapter);

#ifdef CONFIG_IOCTL_CFG80211
	if (chk_driver_interface(padapter, DRIVER_CFG80211)
		&& !rtw_cfg80211_get_is_roch(padapter))
		return;
#endif

	if (rtw_mi_check_status(padapter, MI_LINKED)) {
		u8 union_ch = rtw_mi_get_union_chan(padapter);
		u8 union_bw = rtw_mi_get_union_bw(padapter);
		u8 union_offset = rtw_mi_get_union_offset(padapter);
		unsigned int duration;

	#ifdef CONFIG_P2P
		pwdinfo->operating_channel = union_ch;
	#endif

		if (chk_driver_interface(padapter, DRIVER_CFG80211)) {
	#ifdef CONFIG_IOCTL_CFG80211
			_enter_critical_mutex(&pwdev_priv->roch_mutex, NULL);

			if (rtw_get_oper_ch(padapter) != union_ch) {
				/* Current channel is not AP's channel - switching to AP's channel */
				RTW_INFO("%s, switch ch back to union=%u,%u, %u\n"
					, __func__, union_ch, union_bw, union_offset);
				set_channel_bwmode(padapter, union_ch, union_offset, union_bw);
				rtw_back_opch(padapter);

				/* Now, the driver stays on AP's channel. We should stay on AP's
				 * channel for min_home_dur (duration) and next switch channel is
				 * listen channel.
				 */
				duration = prochinfo->min_home_dur;
			} else {
				/* Current channel is AP's channel - switching to listen channel */
				RTW_INFO("%s, switch ch to roch=%u\n"
					, __func__, remain_ch);
				rtw_leave_opch(padapter);
				set_channel_bwmode(padapter,
						remain_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);

				/* Now, the driver stays on listen channel. We should stay on listen
				 * channel for max_away_dur (duration) and next switch channel is AP's
				 * channel.
				 */
				duration = prochinfo->max_away_dur;
			}

			/* set channel switch timer */
			ATOMIC_SET(&pwdev_priv->switch_ch_to, 0);
			_set_timer(&prochinfo->ap_roch_ch_switch_timer, duration);
			RTW_INFO("%s, set switch ch timer, duration=%d\n", __func__, duration);

			_exit_critical_mutex(&pwdev_priv->roch_mutex, NULL);
	#endif
		}
	#ifdef CONFIG_P2P
		else if (chk_driver_interface(padapter, DRIVER_WEXT)) {
			if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_IDLE)) {
				/*	Now, the driver stays on the AP's channel. */
				/*	If the pwdinfo->ext_listen_period = 0, that means the P2P listen state is not available on listen channel. */
				if (pwdinfo->ext_listen_period > 0) {
					RTW_INFO("[%s] P2P_STATE_IDLE, ext_listen_period = %d\n", __FUNCTION__, pwdinfo->ext_listen_period);

					if (union_ch != pwdinfo->listen_channel) {
						rtw_leave_opch(padapter);
						set_channel_bwmode(padapter, pwdinfo->listen_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
					}

					rtw_p2p_set_state(pwdinfo, P2P_STATE_LISTEN);

					if (!rtw_mi_check_mlmeinfo_state(padapter, WIFI_FW_AP_STATE)) {
						val8 = 1;
						rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
					}
					/*	Todo: To check the value of pwdinfo->ext_listen_period is equal to 0 or not. */
					_set_timer(&prochinfo->ap_roch_ch_switch_timer, pwdinfo->ext_listen_period);
				}

			} else if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_LISTEN) ||
				rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_FAIL) ||
				(rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_ING) && pwdinfo->nego_req_info.benable == _FALSE) ||
				rtw_p2p_chk_state(pwdinfo, P2P_STATE_RX_PROVISION_DIS_REQ)) {
				/*	Now, the driver is in the listen state of P2P mode. */
				RTW_INFO("[%s] P2P_STATE_IDLE, ext_listen_interval = %d\n", __FUNCTION__, pwdinfo->ext_listen_interval);

				/*	Commented by Albert 2012/11/01 */
				/*	If the AP's channel is the same as the listen channel, we should still be in the listen state */
				/*	Other P2P device is still able to find this device out even this device is in the AP's channel. */
				/*	So, configure this device to be able to receive the probe request frame and set it to listen state. */
				if (union_ch != pwdinfo->listen_channel) {

					set_channel_bwmode(padapter, union_ch, union_offset, union_bw);
					if (!rtw_mi_check_status(padapter, MI_AP_MODE)) {
						val8 = 0;
						rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
					}
					rtw_p2p_set_state(pwdinfo, P2P_STATE_IDLE);
					rtw_back_opch(padapter);
				}

				/*	Todo: To check the value of pwdinfo->ext_listen_interval is equal to 0 or not. */
				_set_timer(&prochinfo->ap_roch_ch_switch_timer, pwdinfo->ext_listen_interval);

			} else if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_OK)) {
				/*	The driver had finished the P2P handshake successfully. */
				val8 = 0;
				rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
				set_channel_bwmode(padapter, union_ch, union_offset, union_bw);
				rtw_back_opch(padapter);

			} else if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_TX_PROVISION_DIS_REQ)) {
				val8 = 1;
				set_channel_bwmode(padapter, pwdinfo->tx_prov_disc_info.peer_channel_num[0], HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
				rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
				issue_probereq_p2p(padapter, NULL);
				_set_timer(&pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT);
			} else if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_ING) && pwdinfo->nego_req_info.benable == _TRUE) {
				val8 = 1;
				set_channel_bwmode(padapter, pwdinfo->nego_req_info.peer_channel_num[0], HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
				rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
				issue_probereq_p2p(padapter, NULL);
				_set_timer(&pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT);
			} else if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_TX_INVITE_REQ) && pwdinfo->invitereq_info.benable == _TRUE) {
				/*
				val8 = 1;
				set_channel_bwmode(padapter, , HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
				rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
				issue_probereq_p2p(padapter, NULL);
				_set_timer( &pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT );
				*/
			}
		}
	#endif /* CONFIG_P2P */
	} else if (!chk_need_stay_in_cur_chan(padapter)) {
		set_channel_bwmode(padapter, remain_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
	}
}
#endif  /* CONFIG_CONCURRENT_MODE */

void rtw_init_roch_info(_adapter *padapter)
{
	struct roch_info *prochinfo = &padapter->rochinfo;

	_rtw_memset(prochinfo, 0x00, sizeof(struct roch_info));

#ifdef CONFIG_CONCURRENT_MODE
	rtw_init_timer(&prochinfo->ap_roch_ch_switch_timer, padapter, rtw_ap_roch_ch_switch_timer_process, padapter);
#ifdef CONFIG_IOCTL_CFG80211
	prochinfo->min_home_dur = 1500; 		/* min duration for traffic, home_time */
	prochinfo->max_away_dur = 250;		/* max acceptable away duration, home_away_time */
#endif
#endif

#ifdef CONFIG_IOCTL_CFG80211
	rtw_init_timer(&prochinfo->remain_on_ch_timer, padapter, rtw_ro_ch_timer_process, padapter);
#endif
}
#endif /* (defined(CONFIG_P2P) && defined(CONFIG_CONCURRENT_MODE)) || defined(CONFIG_IOCTL_CFG80211) */