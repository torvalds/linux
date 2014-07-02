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
#define _RTW_CMD_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <mlme_osdep.h>
#include <rtl8723a_cmd.h>
#include <rtw_sreset.h>

static struct cmd_hdl wlancmds[] = {
	GEN_DRV_CMD_HANDLER(0, NULL) /*0*/
	GEN_DRV_CMD_HANDLER(0, NULL)
	GEN_DRV_CMD_HANDLER(0, NULL)
	GEN_DRV_CMD_HANDLER(0, NULL)
	GEN_DRV_CMD_HANDLER(0, NULL)
	GEN_DRV_CMD_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL) /*10*/
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(sizeof (struct wlan_bssid_ex), join_cmd_hdl23a) /*14*/
	GEN_MLME_EXT_HANDLER(sizeof (struct disconnect_parm), disconnect_hdl23a)
	GEN_MLME_EXT_HANDLER(sizeof (struct wlan_bssid_ex), createbss_hdl23a)
	GEN_MLME_EXT_HANDLER(sizeof (struct setopmode_parm), setopmode_hdl23a)
	GEN_MLME_EXT_HANDLER(sizeof (struct sitesurvey_parm), sitesurvey_cmd_hdl23a) /*18*/
	GEN_MLME_EXT_HANDLER(sizeof (struct setauth_parm), setauth_hdl23a)
	GEN_MLME_EXT_HANDLER(sizeof (struct setkey_parm), setkey_hdl23a) /*20*/
	GEN_MLME_EXT_HANDLER(sizeof (struct set_stakey_parm), set_stakey_hdl23a)
	GEN_MLME_EXT_HANDLER(sizeof (struct set_assocsta_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof (struct del_assocsta_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof (struct setstapwrstate_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof (struct setbasicrate_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof (struct getbasicrate_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof (struct setdatarate_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof (struct getdatarate_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof (struct setphyinfo_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof (struct getphyinfo_parm), NULL)  /*30*/
	GEN_MLME_EXT_HANDLER(sizeof (struct setphy_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof (struct getphy_parm), NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)	/*40*/
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct addBaReq_parm), add_ba_hdl23a)
	GEN_MLME_EXT_HANDLER(sizeof(struct set_ch_parm), set_ch_hdl23a) /* 46 */
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL) /*50*/
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct Tx_Beacon_param), tx_beacon_hdl23a) /*55*/

	GEN_MLME_EXT_HANDLER(0, mlme_evt_hdl23a) /*56*/
	GEN_MLME_EXT_HANDLER(0, rtw_drvextra_cmd_hdl23a) /*57*/

	GEN_MLME_EXT_HANDLER(0, h2c_msg_hdl23a) /*58*/
	GEN_MLME_EXT_HANDLER(sizeof(struct SetChannelPlan_param), set_chplan_hdl23a) /*59*/
	GEN_MLME_EXT_HANDLER(sizeof(struct LedBlink_param), led_blink_hdl23a) /*60*/

	GEN_MLME_EXT_HANDLER(sizeof(struct SetChannelSwitch_param), set_csa_hdl23a) /*61*/
	GEN_MLME_EXT_HANDLER(sizeof(struct TDLSoption_param), tdls_hdl23a) /*62*/
};

struct _cmd_callback	rtw_cmd_callback[] = {
	{GEN_CMD_CODE(_Read_MACREG), NULL}, /*0*/
	{GEN_CMD_CODE(_Write_MACREG), NULL},
	{GEN_CMD_CODE(_Read_BBREG), &rtw_getbbrfreg_cmdrsp_callback23a},
	{GEN_CMD_CODE(_Write_BBREG), NULL},
	{GEN_CMD_CODE(_Read_RFREG), &rtw_getbbrfreg_cmdrsp_callback23a},
	{GEN_CMD_CODE(_Write_RFREG), NULL}, /*5*/
	{GEN_CMD_CODE(_Read_EEPROM), NULL},
	{GEN_CMD_CODE(_Write_EEPROM), NULL},
	{GEN_CMD_CODE(_Read_EFUSE), NULL},
	{GEN_CMD_CODE(_Write_EFUSE), NULL},

	{GEN_CMD_CODE(_Read_CAM),	NULL},	/*10*/
	{GEN_CMD_CODE(_Write_CAM),	 NULL},
	{GEN_CMD_CODE(_setBCNITV), NULL},
	{GEN_CMD_CODE(_setMBIDCFG), NULL},
	{GEN_CMD_CODE(_JoinBss), &rtw_joinbss_cmd23a_callback},  /*14*/
	{GEN_CMD_CODE(_DisConnect), &rtw_disassoc_cmd23a_callback}, /*15*/
	{GEN_CMD_CODE(_CreateBss), &rtw_createbss_cmd23a_callback},
	{GEN_CMD_CODE(_SetOpMode), NULL},
	{GEN_CMD_CODE(_SiteSurvey), &rtw_survey_cmd_callback23a}, /*18*/
	{GEN_CMD_CODE(_SetAuth), NULL},

	{GEN_CMD_CODE(_SetKey), NULL},	/*20*/
	{GEN_CMD_CODE(_SetStaKey), &rtw_setstaKey_cmdrsp_callback23a},
	{GEN_CMD_CODE(_SetAssocSta), &rtw_setassocsta_cmdrsp_callback23a},
	{GEN_CMD_CODE(_DelAssocSta), NULL},
	{GEN_CMD_CODE(_SetStaPwrState), NULL},
	{GEN_CMD_CODE(_SetBasicRate), NULL}, /*25*/
	{GEN_CMD_CODE(_GetBasicRate), NULL},
	{GEN_CMD_CODE(_SetDataRate), NULL},
	{GEN_CMD_CODE(_GetDataRate), NULL},
	{GEN_CMD_CODE(_SetPhyInfo), NULL},

	{GEN_CMD_CODE(_GetPhyInfo), NULL}, /*30*/
	{GEN_CMD_CODE(_SetPhy), NULL},
	{GEN_CMD_CODE(_GetPhy), NULL},
	{GEN_CMD_CODE(_readRssi), NULL},
	{GEN_CMD_CODE(_readGain), NULL},
	{GEN_CMD_CODE(_SetAtim), NULL}, /*35*/
	{GEN_CMD_CODE(_SetPwrMode), NULL},
	{GEN_CMD_CODE(_JoinbssRpt), NULL},
	{GEN_CMD_CODE(_SetRaTable), NULL},
	{GEN_CMD_CODE(_GetRaTable), NULL},

	{GEN_CMD_CODE(_GetCCXReport), NULL}, /*40*/
	{GEN_CMD_CODE(_GetDTMReport),	NULL},
	{GEN_CMD_CODE(_GetTXRateStatistics), NULL},
	{GEN_CMD_CODE(_SetUsbSuspend), NULL},
	{GEN_CMD_CODE(_SetH2cLbk), NULL},
	{GEN_CMD_CODE(_AddBAReq), NULL}, /*45*/
	{GEN_CMD_CODE(_SetChannel), NULL},		/*46*/
	{GEN_CMD_CODE(_SetTxPower), NULL},
	{GEN_CMD_CODE(_SwitchAntenna), NULL},
	{GEN_CMD_CODE(_SetCrystalCap), NULL},
	{GEN_CMD_CODE(_SetSingleCarrierTx), NULL},	/*50*/

	{GEN_CMD_CODE(_SetSingleToneTx), NULL}, /*51*/
	{GEN_CMD_CODE(_SetCarrierSuppressionTx), NULL},
	{GEN_CMD_CODE(_SetContinuousTx), NULL},
	{GEN_CMD_CODE(_SwitchBandwidth), NULL},		/*54*/
	{GEN_CMD_CODE(_TX_Beacon), NULL},/*55*/

	{GEN_CMD_CODE(_Set_MLME_EVT), NULL},/*56*/
	{GEN_CMD_CODE(_Set_Drv_Extra), NULL},/*57*/
	{GEN_CMD_CODE(_Set_H2C_MSG), NULL},/*58*/
	{GEN_CMD_CODE(_SetChannelPlan), NULL},/*59*/
	{GEN_CMD_CODE(_LedBlink), NULL},/*60*/

	{GEN_CMD_CODE(_SetChannelSwitch), NULL},/*61*/
	{GEN_CMD_CODE(_TDLS), NULL},/*62*/
};

/*
Caller and the rtw_cmd_thread23a can protect cmd_q by spin_lock.
No irqsave is necessary.
*/

int rtw_init_cmd_priv23a(struct cmd_priv *pcmdpriv)
{
	int res = _SUCCESS;

	pcmdpriv->cmd_issued_cnt = 0;
	pcmdpriv->cmd_done_cnt = 0;
	pcmdpriv->rsp_cnt = 0;

	pcmdpriv->wq = alloc_workqueue("rtl8723au_cmd", 0, 1);
	if (!pcmdpriv->wq)
		res = _FAIL;

	return res;
}

/* forward definition */

static void rtw_irq_work(struct work_struct *work);

u32 rtw_init_evt_priv23a(struct evt_priv *pevtpriv)
{
	pevtpriv->wq = alloc_workqueue("rtl8723au_evt", 0, 1);

	INIT_WORK(&pevtpriv->irq_wk, rtw_irq_work);

	return _SUCCESS;
}

void rtw_free_evt_priv23a(struct evt_priv *pevtpriv)
{
	cancel_work_sync(&pevtpriv->irq_wk);
}

static int rtw_cmd_filter(struct cmd_priv *pcmdpriv, struct cmd_obj *cmd_obj)
{
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	/* set to true to allow enqueuing cmd when hw_init_completed is false */
	u8 bAllow = false;

	/* To decide allow or not */
	if (pcmdpriv->padapter->pwrctrlpriv.bHWPwrPindetect &&
	    !pcmdpriv->padapter->registrypriv.usbss_enable) {
		if (cmd_obj->cmdcode == GEN_CMD_CODE(_Set_Drv_Extra)) {
			pdrvextra_cmd_parm =
				(struct drvextra_cmd_parm *)cmd_obj->parmbuf;
			if (pdrvextra_cmd_parm->ec_id ==
			    POWER_SAVING_CTRL_WK_CID)
				bAllow = true;
		}
	}

	if (cmd_obj->cmdcode == GEN_CMD_CODE(_SetChannelPlan))
		bAllow = true;

	if (pcmdpriv->padapter->hw_init_completed == false && bAllow == false)
		return _FAIL;
	return _SUCCESS;
}

static void rtw_cmd_work(struct work_struct *work);

int rtw_enqueue_cmd23a(struct cmd_priv *pcmdpriv, struct cmd_obj *cmd_obj)
{
	int res = _FAIL;

	if (!cmd_obj)
		goto exit;

	cmd_obj->padapter = pcmdpriv->padapter;

	res = rtw_cmd_filter(pcmdpriv, cmd_obj);
	if (res == _FAIL) {
		rtw_free_cmd_obj23a(cmd_obj);
		goto exit;
	}

	INIT_WORK(&cmd_obj->work, rtw_cmd_work);

	res = queue_work(pcmdpriv->wq, &cmd_obj->work);

	if (!res) {
		printk(KERN_ERR "%s: Call to queue_work() failed\n", __func__);
		res = _FAIL;
	} else
		res = _SUCCESS;
exit:

	return res;
}

void rtw_cmd_clr_isr23a(struct	cmd_priv *pcmdpriv)
{
	pcmdpriv->cmd_done_cnt++;
}

void rtw_free_cmd_obj23a(struct cmd_obj *pcmd)
{

	if (pcmd->cmdcode != _JoinBss_CMD_ &&
	    pcmd->cmdcode != _CreateBss_CMD_) {
		/* free parmbuf in cmd_obj */
		kfree(pcmd->parmbuf);
	}

	if (pcmd->rsp) {
		if (pcmd->rspsz != 0) {
			/* free rsp in cmd_obj */
			kfree(pcmd->rsp);
		}
	}

	kfree(pcmd);
}

static void rtw_cmd_work(struct work_struct *work)
{
	int (*cmd_hdl)(struct rtw_adapter *padapter, const u8 *pbuf);
	void (*pcmd_callback)(struct rtw_adapter *dev, struct cmd_obj *pcmd);
	struct cmd_priv *pcmdpriv;
	struct cmd_obj *pcmd = container_of(work, struct cmd_obj, work);

	pcmdpriv = &pcmd->padapter->cmdpriv;

	if (rtw_cmd_filter(pcmdpriv, pcmd) == _FAIL) {
		pcmd->res = H2C_DROPPED;
		goto post_process;
	}

	pcmdpriv->cmd_issued_cnt++;

	pcmd->cmdsz = ALIGN(pcmd->cmdsz, 4);

	if (pcmd->cmdcode < (sizeof(wlancmds)/sizeof(struct cmd_hdl))) {
		cmd_hdl = wlancmds[pcmd->cmdcode].h2cfuns;

		if (cmd_hdl)
			pcmd->res = cmd_hdl(pcmd->padapter, pcmd->parmbuf);
		else
			pcmd->res = H2C_DROPPED;
	} else
		pcmd->res = H2C_PARAMETERS_ERROR;

post_process:
	/* call callback function for post-processed */
	if (pcmd->cmdcode < (sizeof(rtw_cmd_callback) /
			     sizeof(struct _cmd_callback))) {
		pcmd_callback =	rtw_cmd_callback[pcmd->cmdcode].callback;
		if (!pcmd_callback) {
			RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_,
				 ("mlme_cmd_hdl(): pcmd_callback = 0x%p, "
				  "cmdcode = 0x%x\n",
				  pcmd_callback, pcmd->cmdcode));
			rtw_free_cmd_obj23a(pcmd);
		} else {
			/* need conider that free cmd_obj in
			   rtw_cmd_callback */
			pcmd_callback(pcmd->padapter, pcmd);
		}
	} else {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_,
			 ("%s: cmdcode = 0x%x callback not defined!\n",
			  __func__, pcmd->cmdcode));
		rtw_free_cmd_obj23a(pcmd);
	}
}


int rtw_sitesurvey_cmd23a(struct rtw_adapter *padapter,
			  struct cfg80211_ssid *ssid, int ssid_num,
			  struct rtw_ieee80211_channel *ch, int ch_num)
{
	int res = _FAIL;
	struct cmd_obj *ph2c;
	struct sitesurvey_parm *psurveyPara;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (check_fwstate(pmlmepriv, _FW_LINKED))
		rtw_lps_ctrl_wk_cmd23a(padapter, LPS_CTRL_SCAN, 1);

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c)
		return _FAIL;

	psurveyPara = kzalloc(sizeof(struct sitesurvey_parm), GFP_ATOMIC);
	if (!psurveyPara) {
		kfree(ph2c);
		return _FAIL;
	}

	rtw_free_network_queue23a(padapter);

	RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_,
		 ("%s: flush network queue\n", __func__));

	init_h2fwcmd_w_parm_no_rsp(ph2c, psurveyPara,
				   GEN_CMD_CODE(_SiteSurvey));

	/* psurveyPara->bsslimit = 48; */
	psurveyPara->scan_mode = pmlmepriv->scan_mode;

	/* prepare ssid list */
	if (ssid) {
		int i;
		for (i = 0; i < ssid_num && i < RTW_SSID_SCAN_AMOUNT; i++) {
			if (ssid[i].ssid_len) {
				memcpy(&psurveyPara->ssid[i], &ssid[i],
				       sizeof(struct cfg80211_ssid));
				psurveyPara->ssid_num++;
			}
		}
	}

	/* prepare channel list */
	if (ch) {
		int i;
		for (i = 0; i < ch_num && i < RTW_CHANNEL_SCAN_AMOUNT; i++) {
			if (ch[i].hw_value &&
			    !(ch[i].flags & IEEE80211_CHAN_DISABLED)) {
				memcpy(&psurveyPara->ch[i], &ch[i],
				       sizeof(struct rtw_ieee80211_channel));
				psurveyPara->ch_num++;
			}
		}
	}

	set_fwstate(pmlmepriv, _FW_UNDER_SURVEY);

	res = rtw_enqueue_cmd23a(pcmdpriv, ph2c);

	if (res == _SUCCESS) {
		mod_timer(&pmlmepriv->scan_to_timer, jiffies +
			  msecs_to_jiffies(SCANNING_TIMEOUT));

		rtw_led_control(padapter, LED_CTL_SITE_SURVEY);

		pmlmepriv->scan_interval = SCAN_INTERVAL;/*  30*2 sec = 60sec */
	} else
		_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);

	return res;
}

void rtw_getbbrfreg_cmdrsp_callback23a(struct rtw_adapter *padapter,
				       struct cmd_obj *pcmd)
{
	kfree(pcmd->parmbuf);
	kfree(pcmd);
}

int rtw_createbss_cmd23a(struct rtw_adapter  *padapter)
{
	struct cmd_obj *pcmd;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_bssid_ex *pdev_network;
	u8 res = _SUCCESS;

	pdev_network = &padapter->registrypriv.dev_network;

	rtw_led_control(padapter, LED_CTL_START_TO_LINK);

	if (pmlmepriv->assoc_ssid.ssid_len == 0) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_,
			 (" createbss for Any SSid:%s\n",
			  pmlmepriv->assoc_ssid.ssid));
	} else {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_,
			 (" createbss for SSid:%s\n",
			  pmlmepriv->assoc_ssid.ssid));
	}

	pcmd = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!pcmd) {
		res = _FAIL;
		goto exit;
	}

	pcmd->cmdcode = _CreateBss_CMD_;
	pcmd->parmbuf = (unsigned char *)pdev_network;
	pcmd->cmdsz = get_wlan_bssid_ex_sz(pdev_network);
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	pdev_network->Length = pcmd->cmdsz;

	res = rtw_enqueue_cmd23a(pcmdpriv, pcmd);

exit:

	return res;
}

int rtw_joinbss_cmd23a(struct rtw_adapter *padapter,
		       struct wlan_network *pnetwork)
{
	u8 *auth;
	int res = _SUCCESS;
	struct wlan_bssid_ex *psecnetwork;
	struct cmd_obj *pcmd;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct ht_priv *phtpriv = &pmlmepriv->htpriv;
	enum nl80211_iftype ifmode;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	ifmode = pnetwork->network.ifmode;

	rtw_led_control(padapter, LED_CTL_START_TO_LINK);

	if (pmlmepriv->assoc_ssid.ssid_len == 0) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_info_,
			 ("+Join cmd: Any SSid\n"));
	} else {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_notice_,
			 ("+Join cmd: SSid =[%s]\n",
			  pmlmepriv->assoc_ssid.ssid));
	}

	pcmd = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!pcmd) {
		res = _FAIL;
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_,
			 ("rtw_joinbss_cmd23a: memory allocate for cmd_obj "
			  "fail!!!\n"));
		goto exit;
	}

	/* for hidden ap to set fw_state here */
	if (!check_fwstate(pmlmepriv, WIFI_STATION_STATE|WIFI_ADHOC_STATE)) {
		switch (ifmode) {
		case NL80211_IFTYPE_ADHOC:
			set_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
			break;
		case NL80211_IFTYPE_P2P_CLIENT:
		case NL80211_IFTYPE_STATION:
			set_fwstate(pmlmepriv, WIFI_STATION_STATE);
			break;
		default:
			break;
		}
	}

	psecnetwork = &psecuritypriv->sec_bss;
	if (!psecnetwork) {
		if (pcmd)
			kfree(pcmd);

		res = _FAIL;

		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_,
			 ("rtw_joinbss_cmd23a :psecnetwork == NULL!!!\n"));

		goto exit;
	}

	memset(psecnetwork, 0, sizeof(struct wlan_bssid_ex));

	memcpy(psecnetwork, &pnetwork->network,
	       get_wlan_bssid_ex_sz(&pnetwork->network));

	auth = &psecuritypriv->authenticator_ie[0];
	psecuritypriv->authenticator_ie[0] =
		(unsigned char)psecnetwork->IELength;

	if ((psecnetwork->IELength-12) < (256-1)) {
		memcpy(&psecuritypriv->authenticator_ie[1],
		       &psecnetwork->IEs[12], psecnetwork->IELength - 12);
	} else {
		memcpy(&psecuritypriv->authenticator_ie[1],
		       &psecnetwork->IEs[12], 256 - 1);
	}

	psecnetwork->IELength = 0;
	/*  Added by Albert 2009/02/18 */
	/*  If the the driver wants to use the bssid to create the
	 *  connection. If not,  we have to copy the connecting AP's
	 *  MAC address to it so that the driver just has the bssid
	 *  information for PMKIDList searching. */

	if (pmlmepriv->assoc_by_bssid == false)
		ether_addr_copy(&pmlmepriv->assoc_bssid[0],
				&pnetwork->network.MacAddress[0]);

	psecnetwork->IELength =
		rtw_restruct_sec_ie23a(padapter, &pnetwork->network.IEs[0],
				       &psecnetwork->IEs[0],
				       pnetwork->network.IELength);

	pmlmepriv->qos_option = 0;

	if (pregistrypriv->wmm_enable) {
		u32 tmp_len;

		tmp_len = rtw_restruct_wmm_ie23a(padapter,
						 &pnetwork->network.IEs[0],
						 &psecnetwork->IEs[0],
						 pnetwork->network.IELength,
						 psecnetwork->IELength);

		if (psecnetwork->IELength != tmp_len) {
			psecnetwork->IELength = tmp_len;
			/* There is WMM IE in this corresp. beacon */
			pmlmepriv->qos_option = 1;
		} else {
			/* There is no WMM IE in this corresp. beacon */
			pmlmepriv->qos_option = 0;
		}
	}

	phtpriv->ht_option = false;
	if (pregistrypriv->ht_enable) {
		u32 algo = padapter->securitypriv.dot11PrivacyAlgrthm;
		/*	Added by Albert 2010/06/23 */
		/*	For the WEP mode, we will use the bg mode to do
			the connection to avoid some IOT issue. */
		/*	Especially for Realtek 8192u SoftAP. */
		if (algo != WLAN_CIPHER_SUITE_WEP40 &&
		    algo != WLAN_CIPHER_SUITE_WEP104 &&
		    algo != WLAN_CIPHER_SUITE_TKIP) {
			/* rtw_restructure_ht_ie23a */
			rtw_restructure_ht_ie23a(padapter,
						 &pnetwork->network.IEs[0],
						 &psecnetwork->IEs[0],
						 pnetwork->network.IELength,
						 &psecnetwork->IELength);
		}
	}

	pmlmeinfo->assoc_AP_vendor =
		check_assoc_AP23a(pnetwork->network.IEs,
				  pnetwork->network.IELength);

	if (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_TENDA)
		padapter->pwrctrlpriv.smart_ps = 0;
	else
		padapter->pwrctrlpriv.smart_ps =
			padapter->registrypriv.smart_ps;

	DBG_8723A("%s: smart_ps =%d\n", __func__,
		  padapter->pwrctrlpriv.smart_ps);

	/* get cmdsz before endian conversion */
	pcmd->cmdsz = get_wlan_bssid_ex_sz(psecnetwork);

	pcmd->cmdcode = _JoinBss_CMD_;/* GEN_CMD_CODE(_JoinBss) */
	pcmd->parmbuf = (unsigned char *)psecnetwork;
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	res = rtw_enqueue_cmd23a(pcmdpriv, pcmd);
exit:

	return res;
}

int rtw_disassoc_cmd23a(struct rtw_adapter*padapter, u32 deauth_timeout_ms,
			bool enqueue)
{
	struct cmd_obj *cmdobj = NULL;
	struct disconnect_parm *param = NULL;
	struct cmd_priv *cmdpriv = &padapter->cmdpriv;
	int res = _SUCCESS;

	RT_TRACE(_module_rtl871x_cmd_c_, _drv_notice_,
		 ("+rtw_disassoc_cmd23a\n"));

	/* prepare cmd parameter */
	param = kzalloc(sizeof(*param), GFP_ATOMIC);
	if (param == NULL) {
		res = _FAIL;
		goto exit;
	}
	param->deauth_timeout_ms = deauth_timeout_ms;

	if (enqueue) {
		/* need enqueue, prepare cmd_obj and enqueue */
		cmdobj = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
		if (!cmdobj) {
			res = _FAIL;
			kfree(param);
			goto exit;
		}
		init_h2fwcmd_w_parm_no_rsp(cmdobj, param, _DisConnect_CMD_);
		res = rtw_enqueue_cmd23a(cmdpriv, cmdobj);
	} else {
		/* no need to enqueue, do the cmd hdl directly and
		   free cmd parameter */
		if (H2C_SUCCESS != disconnect_hdl23a(padapter, (u8 *)param))
			res = _FAIL;
		kfree(param);
	}

exit:
	return res;
}

int rtw_setopmode_cmd23a(struct rtw_adapter *padapter,
			 enum nl80211_iftype ifmode)
{
	struct	cmd_obj *ph2c;
	struct	setopmode_parm *psetop;
	struct	cmd_priv   *pcmdpriv = &padapter->cmdpriv;
	int res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
	if (!ph2c) {
		res = false;
		goto exit;
	}
	psetop = kzalloc(sizeof(struct setopmode_parm), GFP_KERNEL);

	if (!psetop) {
		kfree(ph2c);
		res = false;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetop, _SetOpMode_CMD_);
	psetop->mode = ifmode;

	res = rtw_enqueue_cmd23a(pcmdpriv, ph2c);
exit:
	return res;
}

int rtw_setstakey_cmd23a(struct rtw_adapter *padapter, u8 *psta, u8 unicast_key)
{
	struct cmd_obj *ph2c;
	struct set_stakey_parm *psetstakey_para;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	struct set_stakey_rsp *psetstakey_rsp = NULL;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct sta_info *sta = (struct sta_info*)psta;
	int res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}

	psetstakey_para = kzalloc(sizeof(struct set_stakey_parm), GFP_KERNEL);
	if (!psetstakey_para) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	psetstakey_rsp = kzalloc(sizeof(struct set_stakey_rsp), GFP_KERNEL);
	if (!psetstakey_rsp) {
		kfree(ph2c);
		kfree(psetstakey_para);
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetstakey_para, _SetStaKey_CMD_);
	ph2c->rsp = (u8 *) psetstakey_rsp;
	ph2c->rspsz = sizeof(struct set_stakey_rsp);

	ether_addr_copy(psetstakey_para->addr, sta->hwaddr);

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		psetstakey_para->algorithm =
			(unsigned char)psecuritypriv->dot11PrivacyAlgrthm;
	} else {
		GET_ENCRY_ALGO(psecuritypriv, sta, psetstakey_para->algorithm,
			       false);
	}

	if (unicast_key == true) {
		memcpy(&psetstakey_para->key, &sta->dot118021x_UncstKey, 16);
        } else {
		int idx = psecuritypriv->dot118021XGrpKeyid;
		memcpy(&psetstakey_para->key,
		       &psecuritypriv->dot118021XGrpKey[idx].skey, 16);
        }

	/* jeff: set this becasue at least sw key is ready */
	padapter->securitypriv.busetkipkey = 1;

	res = rtw_enqueue_cmd23a(pcmdpriv, ph2c);

exit:

	return res;
}

int rtw_clearstakey_cmd23a(struct rtw_adapter *padapter, u8 *psta, u8 entry,
			   u8 enqueue)
{
	struct cmd_obj *ph2c;
	struct set_stakey_parm *psetstakey_para;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	struct set_stakey_rsp *psetstakey_rsp = NULL;
	struct sta_info *sta = (struct sta_info *)psta;
	int res = _SUCCESS;

	if (!enqueue) {
		clear_cam_entry23a(padapter, entry);
	} else {
		ph2c = kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
		if (!ph2c) {
			res = _FAIL;
			goto exit;
		}

		psetstakey_para = kzalloc(sizeof(struct set_stakey_parm),
					  GFP_KERNEL);
		if (!psetstakey_para) {
			kfree(ph2c);
			res = _FAIL;
			goto exit;
		}

		psetstakey_rsp = kzalloc(sizeof(struct set_stakey_rsp),
					 GFP_KERNEL);
		if (!psetstakey_rsp) {
			kfree(ph2c);
			kfree(psetstakey_para);
			res = _FAIL;
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(ph2c, psetstakey_para,
					   _SetStaKey_CMD_);
		ph2c->rsp = (u8 *) psetstakey_rsp;
		ph2c->rspsz = sizeof(struct set_stakey_rsp);

		ether_addr_copy(psetstakey_para->addr, sta->hwaddr);

		psetstakey_para->algorithm = 0;

		psetstakey_para->id = entry;

		res = rtw_enqueue_cmd23a(pcmdpriv, ph2c);
	}
exit:
	return res;
}

int rtw_addbareq_cmd23a(struct rtw_adapter*padapter, u8 tid, u8 *addr)
{
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	struct cmd_obj *ph2c;
	struct addBaReq_parm *paddbareq_parm;
	int res = _SUCCESS;

	if (tid >= MAXTID) {
		res = _FAIL;
		goto exit;
	}

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}

	paddbareq_parm = kzalloc(sizeof(struct addBaReq_parm), GFP_ATOMIC);
	if (!paddbareq_parm) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	paddbareq_parm->tid = tid;
	ether_addr_copy(paddbareq_parm->addr, addr);

	init_h2fwcmd_w_parm_no_rsp(ph2c, paddbareq_parm,
				   GEN_CMD_CODE(_AddBAReq));

	res = rtw_enqueue_cmd23a(pcmdpriv, ph2c);
exit:
	return res;
}

int rtw_dynamic_chk_wk_cmd23a(struct rtw_adapter*padapter)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	int res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = kzalloc(sizeof(*pdrvextra_cmd_parm), GFP_ATOMIC);
	if (!pdrvextra_cmd_parm) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = DYNAMIC_CHK_WK_CID;
	pdrvextra_cmd_parm->type_size = 0;
	pdrvextra_cmd_parm->pbuf = (u8 *)padapter;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm,
				   GEN_CMD_CODE(_Set_Drv_Extra));

	res = rtw_enqueue_cmd23a(pcmdpriv, ph2c);
exit:

	return res;
}

/*
 * This is only ever called from on_action_spct23a_ch_switch () which isn't
 * called from anywhere itself
 */
int rtw_set_ch_cmd23a(struct rtw_adapter*padapter, u8 ch, u8 bw, u8 ch_offset,
		      u8 enqueue)
{
	struct cmd_obj *pcmdobj;
	struct set_ch_parm *set_ch_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	int res = _SUCCESS;

	DBG_8723A("%s(%s): ch:%u, bw:%u, ch_offset:%u\n", __func__,
		  padapter->pnetdev->name, ch, bw, ch_offset);

	/* check input parameter */

	/* prepare cmd parameter */
	set_ch_parm = kzalloc(sizeof(*set_ch_parm), GFP_KERNEL);
	if (!set_ch_parm) {
		res = _FAIL;
		goto exit;
	}
	set_ch_parm->ch = ch;
	set_ch_parm->bw = bw;
	set_ch_parm->ch_offset = ch_offset;

	if (enqueue) {
		/* need enqueue, prepare cmd_obj and enqueue */
		pcmdobj = kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
		if (!pcmdobj) {
			kfree(set_ch_parm);
			res = _FAIL;
			goto exit;
		}

		init_h2fwcmd_w_parm_no_rsp(pcmdobj, set_ch_parm,
					   GEN_CMD_CODE(_SetChannel));
		res = rtw_enqueue_cmd23a(pcmdpriv, pcmdobj);
	} else {
		/* no need to enqueue, do the cmd hdl directly and
		   free cmd parameter */
		if (H2C_SUCCESS != set_ch_hdl23a(padapter, (u8 *)set_ch_parm))
			res = _FAIL;

		kfree(set_ch_parm);
	}

	/* do something based on res... */
exit:

	DBG_8723A("%s(%s): res:%u\n", __func__, padapter->pnetdev->name, res);

	return res;
}

static void traffic_status_watchdog(struct rtw_adapter *padapter)
{
	u8 bEnterPS;
	u8 bBusyTraffic = false, bTxBusyTraffic = false, bRxBusyTraffic = false;
	u8 bHigherBusyTraffic = false, bHigherBusyRxTraffic = false;
	u8 bHigherBusyTxTraffic = false;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int BusyThreshold = 100;
	/*  */
	/*  Determine if our traffic is busy now */
	/*  */
	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		if (rtl8723a_BT_coexist(padapter))
			BusyThreshold = 50;
		else if (pmlmepriv->LinkDetectInfo.bBusyTraffic)
			BusyThreshold = 75;
		/*  if we raise bBusyTraffic in last watchdog, using
		    lower threshold. */
		if (pmlmepriv->LinkDetectInfo.NumRxOkInPeriod > BusyThreshold ||
		    pmlmepriv->LinkDetectInfo.NumTxOkInPeriod > BusyThreshold) {
			bBusyTraffic = true;

			if (pmlmepriv->LinkDetectInfo.NumRxOkInPeriod >
			    pmlmepriv->LinkDetectInfo.NumTxOkInPeriod)
				bRxBusyTraffic = true;
			else
				bTxBusyTraffic = true;
		}

		/*  Higher Tx/Rx data. */
		if (pmlmepriv->LinkDetectInfo.NumRxOkInPeriod > 4000 ||
		    pmlmepriv->LinkDetectInfo.NumTxOkInPeriod > 4000) {
			bHigherBusyTraffic = true;

			if (pmlmepriv->LinkDetectInfo.NumRxOkInPeriod >
			    pmlmepriv->LinkDetectInfo.NumTxOkInPeriod)
				bHigherBusyRxTraffic = true;
			else
				bHigherBusyTxTraffic = true;
		}

		if (!rtl8723a_BT_coexist(padapter) ||
		    !rtl8723a_BT_using_antenna_1(padapter)) {
		/*  check traffic for  powersaving. */
			if (((pmlmepriv->LinkDetectInfo.NumRxUnicastOkInPeriod +
			      pmlmepriv->LinkDetectInfo.NumTxOkInPeriod) > 8) ||
			    pmlmepriv->LinkDetectInfo.NumRxUnicastOkInPeriod >2)
				bEnterPS = false;
			else
				bEnterPS = true;

			/*  LeisurePS only work in infra mode. */
			if (bEnterPS)
				LPS_Enter23a(padapter);
			else
				LPS_Leave23a(padapter);
		}
	} else
		LPS_Leave23a(padapter);

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

	padapter = (struct rtw_adapter *)pbuf;
	pmlmepriv = &padapter->mlmepriv;

#ifdef CONFIG_8723AU_AP_MODE
	if (check_fwstate(pmlmepriv, WIFI_AP_STATE))
		expire_timeout_chk23a(padapter);
#endif

	rtl8723a_sreset_xmit_status_check(padapter);

	linked_status_chk23a(padapter);
	traffic_status_watchdog(padapter);

	rtl8723a_HalDmWatchDog(padapter);

	/*  */
	/*  BT-Coexist */
	/*  */
	rtl8723a_BT_do_coexist(padapter);
}

static void lps_ctrl_wk_hdl(struct rtw_adapter *padapter, u8 lps_ctrl_type)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 mstatus;

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) ||
	    check_fwstate(pmlmepriv, WIFI_ADHOC_STATE))
		return;

	switch (lps_ctrl_type)
	{
		case LPS_CTRL_SCAN:
			rtl8723a_BT_wifiscan_notify(padapter, true);
			if (!rtl8723a_BT_using_antenna_1(padapter)) {
				if (check_fwstate(pmlmepriv, _FW_LINKED))
					LPS_Leave23a(padapter);
			}
			break;
		case LPS_CTRL_JOINBSS:
			LPS_Leave23a(padapter);
			break;
		case LPS_CTRL_CONNECT:
			mstatus = 1;/* connect */
			/*  Reset LPS Setting */
			padapter->pwrctrlpriv.LpsIdleCount = 0;
			rtl8723a_set_FwJoinBssReport_cmd(padapter, 1);
			rtl8723a_BT_mediastatus_notify(padapter, mstatus);
			break;
		case LPS_CTRL_DISCONNECT:
			mstatus = 0;/* disconnect */
			rtl8723a_BT_mediastatus_notify(padapter, mstatus);
			if (!rtl8723a_BT_using_antenna_1(padapter))
				LPS_Leave23a(padapter);
			rtl8723a_set_FwJoinBssReport_cmd(padapter, 0);
			break;
		case LPS_CTRL_SPECIAL_PACKET:
			pwrpriv->DelayLPSLastTimeStamp = jiffies;
			rtl8723a_BT_specialpacket_notify(padapter);
			if (!rtl8723a_BT_using_antenna_1(padapter))
				LPS_Leave23a(padapter);
			break;
		case LPS_CTRL_LEAVE:
			rtl8723a_BT_lps_leave(padapter);
			if (!rtl8723a_BT_using_antenna_1(padapter))
				LPS_Leave23a(padapter);
			break;

		default:
			break;
	}
}

int rtw_lps_ctrl_wk_cmd23a(struct rtw_adapter *padapter,
			   u8 lps_ctrl_type, u8 enqueue)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	int res = _SUCCESS;

	if (enqueue) {
		ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
		if (!ph2c) {
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm = kzalloc(sizeof(struct drvextra_cmd_parm),
					     GFP_ATOMIC);
		if (!pdrvextra_cmd_parm) {
			kfree(ph2c);
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm->ec_id = LPS_CTRL_WK_CID;
		pdrvextra_cmd_parm->type_size = lps_ctrl_type;
		pdrvextra_cmd_parm->pbuf = NULL;

		init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm,
					   GEN_CMD_CODE(_Set_Drv_Extra));

		res = rtw_enqueue_cmd23a(pcmdpriv, ph2c);
	} else
		lps_ctrl_wk_hdl(padapter, lps_ctrl_type);
exit:

	return res;
}

static void power_saving_wk_hdl(struct rtw_adapter *padapter, u8 *pbuf, int sz)
{
	 rtw_ps_processor23a(padapter);
}

int rtw_ps_cmd23a(struct rtw_adapter*padapter)
{
	struct cmd_obj *ppscmd;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	int res = _SUCCESS;

	ppscmd = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ppscmd) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = kzalloc(sizeof(struct drvextra_cmd_parm),
				     GFP_ATOMIC);
	if (!pdrvextra_cmd_parm) {
		kfree(ppscmd);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = POWER_SAVING_CTRL_WK_CID;
	pdrvextra_cmd_parm->pbuf = NULL;
	init_h2fwcmd_w_parm_no_rsp(ppscmd, pdrvextra_cmd_parm,
				   GEN_CMD_CODE(_Set_Drv_Extra));

	res = rtw_enqueue_cmd23a(pcmdpriv, ppscmd);
exit:

	return res;
}

#ifdef CONFIG_8723AU_AP_MODE

static void rtw_chk_hi_queue_hdl(struct rtw_adapter *padapter)
{
	int cnt = 0;
	struct sta_info *psta_bmc;
	struct sta_priv *pstapriv = &padapter->stapriv;

	psta_bmc = rtw_get_bcmc_stainfo23a(padapter);
	if (!psta_bmc)
		return;

	if (psta_bmc->sleepq_len == 0) {
		bool val;

		val = rtl8723a_chk_hi_queue_empty(padapter);

		while (val == false) {
			msleep(100);

			cnt++;

			if (cnt>10)
				break;

			val = rtl8723a_chk_hi_queue_empty(padapter);
		}

		if (cnt <= 10) {
			pstapriv->tim_bitmap &= ~BIT(0);
			pstapriv->sta_dz_bitmap &= ~BIT(0);

			update_beacon23a(padapter, WLAN_EID_TIM, NULL, false);
		} else /* re check again */
			rtw_chk_hi_queue_cmd23a(padapter);
	}
}

int rtw_chk_hi_queue_cmd23a(struct rtw_adapter*padapter)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	int res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = kzalloc(sizeof(struct drvextra_cmd_parm),
				     GFP_ATOMIC);
	if (!pdrvextra_cmd_parm) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = CHECK_HIQ_WK_CID;
	pdrvextra_cmd_parm->type_size = 0;
	pdrvextra_cmd_parm->pbuf = NULL;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm,
				   GEN_CMD_CODE(_Set_Drv_Extra));

	res = rtw_enqueue_cmd23a(pcmdpriv, ph2c);
exit:

	return res;
}
#endif

int rtw_c2h_wk_cmd23a(struct rtw_adapter *padapter, u8 *c2h_evt)
{
	struct cmd_obj *ph2c;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	int res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_ATOMIC);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = kzalloc(sizeof(struct drvextra_cmd_parm),
				     GFP_ATOMIC);
	if (!pdrvextra_cmd_parm) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = C2H_WK_CID;
	pdrvextra_cmd_parm->type_size = c2h_evt?16:0;
	pdrvextra_cmd_parm->pbuf = c2h_evt;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm,
				   GEN_CMD_CODE(_Set_Drv_Extra));

	res = rtw_enqueue_cmd23a(pcmdpriv, ph2c);

exit:

	return res;
}

static int c2h_evt_hdl(struct rtw_adapter *adapter, struct c2h_evt_hdr *c2h_evt)
{
	int ret = _FAIL;
	u8 buf[16];

	if (!c2h_evt) {
		/* No c2h event in cmd_obj, read c2h event before handling*/
		if (c2h_evt_read23a(adapter, buf) == _SUCCESS) {
			c2h_evt = (struct c2h_evt_hdr *)buf;

			ret = c2h_handler_8723a(adapter, c2h_evt);
		}
	} else
		ret = c2h_handler_8723a(adapter, c2h_evt);

	return ret;
}

static void rtw_irq_work(struct work_struct *work)
{
	struct evt_priv *evtpriv;
	struct rtw_adapter *adapter;

	evtpriv = container_of(work, struct evt_priv, irq_wk);
	adapter = container_of(evtpriv, struct rtw_adapter, evtpriv);

	c2h_evt_clear23a(adapter);
}

void rtw_evt_work(struct work_struct *work)
{
	struct evt_work *ework;
	struct rtw_adapter *adapter;

	ework = container_of(work, struct evt_work, work);
	adapter = ework->adapter;

	c2h_evt_clear23a(adapter);

	if (!c2h_evt_exist(&ework->u.c2h_evt)) {
		kfree(ework);
		return;
	}

	if (c2h_id_filter_ccx_8723a(ework->u.c2h_evt.id) == true) {
		/* Handle CCX report here */
		c2h_handler_8723a(adapter, &ework->u.c2h_evt);
		kfree(ework);
	} else {
		/*
		 * Enqueue into cmd_thread for others.
		 * ework will be turned into a c2h_evt and freed once it
		 * has been consumed.
		 */
		rtw_c2h_wk_cmd23a(adapter, (u8 *)&ework->u.c2h_evt);
	}
}

int rtw_drvextra_cmd_hdl23a(struct rtw_adapter *padapter, const u8 *pbuf)
{
	const struct drvextra_cmd_parm *pdrvextra_cmd;

	if (!pbuf)
		return H2C_PARAMETERS_ERROR;

	pdrvextra_cmd = (struct drvextra_cmd_parm *)pbuf;

	switch (pdrvextra_cmd->ec_id)
	{
	case DYNAMIC_CHK_WK_CID:
		dynamic_chk_wk_hdl(padapter, pdrvextra_cmd->pbuf,
				   pdrvextra_cmd->type_size);
		break;
	case POWER_SAVING_CTRL_WK_CID:
		power_saving_wk_hdl(padapter, pdrvextra_cmd->pbuf,
				    pdrvextra_cmd->type_size);
		break;
	case LPS_CTRL_WK_CID:
		lps_ctrl_wk_hdl(padapter, (u8)pdrvextra_cmd->type_size);
		break;
#ifdef CONFIG_8723AU_AP_MODE
	case CHECK_HIQ_WK_CID:
		rtw_chk_hi_queue_hdl(padapter);
		break;
#endif /* CONFIG_8723AU_AP_MODE */
	case C2H_WK_CID:
		c2h_evt_hdl(padapter,
			    (struct c2h_evt_hdr *)pdrvextra_cmd->pbuf);
		break;

	default:
		break;
	}

	if (pdrvextra_cmd->pbuf && (pdrvextra_cmd->type_size > 0)) {
		kfree(pdrvextra_cmd->pbuf);
		/*
		 * No need to set pdrvextra_cmd->pbuf = NULL as we were
		 * operating on a copy of the original pcmd->parmbuf
		 * created in rtw_cmd_work().
		 */
	}

	return H2C_SUCCESS;
}

void rtw_survey_cmd_callback23a(struct rtw_adapter *padapter,
				struct cmd_obj *pcmd)
{
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (pcmd->res == H2C_DROPPED) {
		/* TODO: cancel timer and do timeout handler directly... */
		/* need to make timeout handlerOS independent */
		mod_timer(&pmlmepriv->scan_to_timer,
			  jiffies + msecs_to_jiffies(1));
	} else if (pcmd->res != H2C_SUCCESS) {
		mod_timer(&pmlmepriv->scan_to_timer,
			  jiffies + msecs_to_jiffies(1));
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_,
			 ("\n ********Error: MgntActrtw_set_802_11_bssid23a_"
			  "LIST_SCAN Fail ************\n\n."));
	}

	/*  free cmd */
	rtw_free_cmd_obj23a(pcmd);
}

void rtw_disassoc_cmd23a_callback(struct rtw_adapter *padapter,
				  struct cmd_obj *pcmd)
{
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (pcmd->res != H2C_SUCCESS) {
		spin_lock_bh(&pmlmepriv->lock);
		set_fwstate(pmlmepriv, _FW_LINKED);
		spin_unlock_bh(&pmlmepriv->lock);
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_,
			 ("\n ***Error: disconnect_cmd_callback Fail ***\n."));
		return;
	}

	/*  free cmd */
	rtw_free_cmd_obj23a(pcmd);
}

void rtw_joinbss_cmd23a_callback(struct rtw_adapter *padapter,
				 struct cmd_obj *pcmd)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (pcmd->res == H2C_DROPPED) {
		/* TODO: cancel timer and do timeout handler directly... */
		/* need to make timeout handlerOS independent */
		mod_timer(&pmlmepriv->assoc_timer,
			  jiffies + msecs_to_jiffies(1));
	} else if (pcmd->res != H2C_SUCCESS) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_,
			 ("********Error:rtw_select_and_join_from_scanned_"
			  "queue Wait Sema  Fail ************\n"));
		mod_timer(&pmlmepriv->assoc_timer,
			  jiffies + msecs_to_jiffies(1));
	}

	rtw_free_cmd_obj23a(pcmd);
}

void rtw_createbss_cmd23a_callback(struct rtw_adapter *padapter,
				   struct cmd_obj *pcmd)
{
	struct sta_info *psta;
	struct wlan_network *pwlan;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_bssid_ex *pnetwork = (struct wlan_bssid_ex *)pcmd->parmbuf;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;

	if (pcmd->res != H2C_SUCCESS) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_,
			 ("\n ********Error: rtw_createbss_cmd23a_callback  "
			  "Fail ************\n\n."));
		mod_timer(&pmlmepriv->assoc_timer,
			  jiffies + msecs_to_jiffies(1));
	}

	del_timer_sync(&pmlmepriv->assoc_timer);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		psta = rtw_get_stainfo23a(&padapter->stapriv,
					  pnetwork->MacAddress);
		if (!psta) {
			psta = rtw_alloc_stainfo23a(&padapter->stapriv,
						    pnetwork->MacAddress,
						    GFP_KERNEL);
			if (!psta) {
				RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_,
					 ("\nCan't alloc sta_info when "
					  "createbss_cmd_callback\n"));
				goto createbss_cmd_fail ;
			}
		}

		spin_lock_bh(&pmlmepriv->lock);
		rtw_indicate_connect23a(padapter);
		spin_unlock_bh(&pmlmepriv->lock);
	} else {
		pwlan = rtw_alloc_network(pmlmepriv, GFP_KERNEL);
		spin_lock_bh(&pmlmepriv->scanned_queue.lock);
		if (!pwlan) {
			pwlan = rtw_get_oldest_wlan_network23a(&pmlmepriv->scanned_queue);
			if (!pwlan) {
				RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_,
					 ("\n Error:  can't get pwlan in "
					  "rtw23a_joinbss_event_cb\n"));
				spin_unlock_bh(&pmlmepriv->scanned_queue.lock);
				goto createbss_cmd_fail;
			}
			pwlan->last_scanned = jiffies;
		} else {
			list_add_tail(&pwlan->list,
				      &pmlmepriv->scanned_queue.queue);
		}

		pnetwork->Length = get_wlan_bssid_ex_sz(pnetwork);
		memcpy(&pwlan->network, pnetwork, pnetwork->Length);
		/* pwlan->fixed = true; */

		/* list_add_tail(&pwlan->list,
		   &pmlmepriv->scanned_queue.queue); */

		/*  copy pdev_network information to
		    pmlmepriv->cur_network */
		memcpy(&tgt_network->network, pnetwork,
		       get_wlan_bssid_ex_sz(pnetwork));

		/*  reset DSConfig */

		clr_fwstate(pmlmepriv, _FW_UNDER_LINKING);

		spin_unlock_bh(&pmlmepriv->scanned_queue.lock);
		/*  we will set _FW_LINKED when there is one more sat to
		    join us (rtw_stassoc_event_callback23a) */
	}

createbss_cmd_fail:

	rtw_free_cmd_obj23a(pcmd);
}

void rtw_setstaKey_cmdrsp_callback23a(struct rtw_adapter *padapter,
				      struct cmd_obj *pcmd)
{
	struct sta_priv *pstapriv;
	struct set_stakey_rsp *psetstakey_rsp;
	struct sta_info *psta;

	pstapriv = &padapter->stapriv;
	psetstakey_rsp = (struct set_stakey_rsp*) (pcmd->rsp);
	psta = rtw_get_stainfo23a(pstapriv, psetstakey_rsp->addr);

	if (!psta) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_,
			 ("\nERROR: rtw_setstaKey_cmdrsp_callback23a => "
			  "can't get sta_info\n\n"));
		goto exit;
	}

exit:

	rtw_free_cmd_obj23a(pcmd);
}

void rtw_setassocsta_cmdrsp_callback23a(struct rtw_adapter *padapter,
					struct cmd_obj *pcmd)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct set_assocsta_parm* passocsta_parm;
	struct set_assocsta_rsp* passocsta_rsp;
	struct sta_info *psta;

	passocsta_parm = (struct set_assocsta_parm *)(pcmd->parmbuf);
	passocsta_rsp = (struct set_assocsta_rsp*) (pcmd->rsp);
	psta = rtw_get_stainfo23a(pstapriv, passocsta_parm->addr);

	if (psta == NULL) {
		RT_TRACE(_module_rtl871x_cmd_c_, _drv_err_,
			 ("\nERROR: setassocsta_cmdrsp_callbac => can't "
			  "get sta_info\n\n"));
		goto exit;
	}

	psta->aid = psta->mac_id = passocsta_rsp->cam_id;

	spin_lock_bh(&pmlmepriv->lock);

	if (check_fwstate(pmlmepriv, WIFI_MP_STATE) &&
	    check_fwstate(pmlmepriv, _FW_UNDER_LINKING))
		_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);

	set_fwstate(pmlmepriv, _FW_LINKED);
	spin_unlock_bh(&pmlmepriv->lock);

exit:
	rtw_free_cmd_obj23a(pcmd);
}
