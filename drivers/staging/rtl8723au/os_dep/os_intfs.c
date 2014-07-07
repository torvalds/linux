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
 ******************************************************************************/
#define _OS_INTFS_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <xmit_osdep.h>
#include <recv_osdep.h>
#include <hal_intf.h>
#include <rtw_version.h>

#include <rtl8723a_hal.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek Wireless Lan Driver");
MODULE_AUTHOR("Realtek Semiconductor Corp.");
MODULE_AUTHOR("Larry Finger <Larry.Finger@lwfinger.net>");
MODULE_AUTHOR("Jes Sorensen <Jes.Sorensen@redhat.com>");
MODULE_VERSION(DRIVERVERSION);
MODULE_FIRMWARE("rtlwifi/rtl8723aufw_A.bin");
MODULE_FIRMWARE("rtlwifi/rtl8723aufw_B.bin");
MODULE_FIRMWARE("rtlwifi/rtl8723aufw_B_NoBT.bin");

/* module param defaults */
static int rtw_chip_version = 0x00;
static int rtw_rfintfs = HWPI;
static int rtw_debug = 1;

static int rtw_channel = 1;/* ad-hoc support requirement */
static int rtw_wireless_mode = WIRELESS_11BG_24N;
static int rtw_vrtl_carrier_sense = AUTO_VCS;
static int rtw_vcs_type = RTS_CTS;/*  */
static int rtw_rts_thresh = 2347;/*  */
static int rtw_frag_thresh = 2346;/*  */
static int rtw_preamble = PREAMBLE_LONG;/* long, short, auto */
static int rtw_scan_mode = 1;/* active, passive */
static int rtw_adhoc_tx_pwr = 1;
static int rtw_soft_ap;
static int rtw_power_mgnt = 1;
static int rtw_ips_mode = IPS_NORMAL;

static int rtw_smart_ps = 2;

module_param(rtw_ips_mode, int, 0644);
MODULE_PARM_DESC(rtw_ips_mode, "The default IPS mode");

static int rtw_long_retry_lmt = 7;
static int rtw_short_retry_lmt = 7;
static int rtw_busy_thresh = 40;
static int rtw_ack_policy = NORMAL_ACK;

static int rtw_acm_method;/*  0:By SW 1:By HW. */

static int rtw_wmm_enable = 1;/*  default is set to enable the wmm. */
static int rtw_uapsd_enable;

static int rtw_ht_enable = 1;
/* 0 :diable, bit(0): enable 2.4g, bit(1): enable 5g */
static int rtw_cbw40_enable = 3;
static int rtw_ampdu_enable = 1;/* for enable tx_ampdu */
/*  0: disable, bit(0):enable 2.4g, bit(1):enable 5g, default is set to enable
 * 2.4GHZ for IOT issue with bufflao's AP at 5GHZ
 */
static int rtw_rx_stbc = 1;
static int rtw_ampdu_amsdu;/*  0: disabled, 1:enabled, 2:auto */

/* Use 2 path Tx to transmit MCS0~7 and legacy mode */
static int rtw_lowrate_two_xmit = 1;

/* int rf_config = RF_1T2R;  1T2R */
static int rtw_rf_config = RF_819X_MAX_TYPE;  /* auto */
static int rtw_low_power;
static int rtw_wifi_spec;
static int rtw_channel_plan = RT_CHANNEL_DOMAIN_MAX;

#ifdef CONFIG_8723AU_BT_COEXIST
static int rtw_btcoex_enable = 1;
static int rtw_bt_iso = 2;/*  0:Low, 1:High, 2:From Efuse */
/*  0:Idle, 1:None-SCO, 2:SCO, 3:From Counter, 4.Busy, 5.OtherBusy */
static int rtw_bt_sco = 3;
/*  0:Disable BT control A-MPDU, 1:Enable BT control A-MPDU. */
static int rtw_bt_ampdu = 1 ;
#endif

/*  0:Reject AP's Add BA req, 1:Accept AP's Add BA req. */
static int rtw_AcceptAddbaReq = true;

static int rtw_antdiv_cfg = 2; /*  0:OFF , 1:ON, 2:decide by Efuse config */
static int rtw_antdiv_type; /* 0:decide by efuse */

static int rtw_enusbss;/* 0:disable, 1:enable */

static int rtw_hwpdn_mode = 2;/* 0:disable, 1:enable, 2: by EFUSE config */

static int rtw_hwpwrp_detect; /* HW power  ping detect 0:disable , 1:enable */

static int rtw_hw_wps_pbc = 1;

static int rtw_80211d;

static int rtw_regulatory_id = 0xff;/*  Regulatory tab id, 0xff = follow efuse's setting */

module_param(rtw_regulatory_id, int, 0644);

static char *ifname = "wlan%d";
module_param(ifname, charp, 0644);
MODULE_PARM_DESC(ifname, "The default name to allocate for first interface");

static char *if2name = "wlan%d";
module_param(if2name, charp, 0644);
MODULE_PARM_DESC(if2name, "The default name to allocate for second interface");

module_param(rtw_channel_plan, int, 0644);
module_param(rtw_chip_version, int, 0644);
module_param(rtw_rfintfs, int, 0644);
module_param(rtw_channel, int, 0644);
module_param(rtw_wmm_enable, int, 0644);
module_param(rtw_vrtl_carrier_sense, int, 0644);
module_param(rtw_vcs_type, int, 0644);
module_param(rtw_busy_thresh, int, 0644);
module_param(rtw_ht_enable, int, 0644);
module_param(rtw_cbw40_enable, int, 0644);
module_param(rtw_ampdu_enable, int, 0644);
module_param(rtw_rx_stbc, int, 0644);
module_param(rtw_ampdu_amsdu, int, 0644);

module_param(rtw_lowrate_two_xmit, int, 0644);

module_param(rtw_rf_config, int, 0644);
module_param(rtw_power_mgnt, int, 0644);
module_param(rtw_smart_ps, int, 0644);
module_param(rtw_low_power, int, 0644);
module_param(rtw_wifi_spec, int, 0644);

module_param(rtw_antdiv_cfg, int, 0644);

module_param(rtw_enusbss, int, 0644);
module_param(rtw_hwpdn_mode, int, 0644);
module_param(rtw_hwpwrp_detect, int, 0644);

module_param(rtw_hw_wps_pbc, int, 0644);

static uint rtw_max_roaming_times = 2;
module_param(rtw_max_roaming_times, uint, 0644);
MODULE_PARM_DESC(rtw_max_roaming_times, "The max roaming times to try");

module_param(rtw_80211d, int, 0644);
MODULE_PARM_DESC(rtw_80211d, "Enable 802.11d mechanism");

#ifdef CONFIG_8723AU_BT_COEXIST
module_param(rtw_btcoex_enable, int, 0644);
MODULE_PARM_DESC(rtw_btcoex_enable, "Enable BT co-existence mechanism");
#endif

static uint rtw_notch_filter;
module_param(rtw_notch_filter, uint, 0644);
MODULE_PARM_DESC(rtw_notch_filter, "0:Disable, 1:Enable, 2:Enable only for P2P");
module_param_named(debug, rtw_debug, int, 0444);
MODULE_PARM_DESC(debug, "Set debug level (1-9) (default 1)");

static int netdev_close(struct net_device *pnetdev);

static int loadparam(struct rtw_adapter *padapter,  struct net_device *pnetdev)
{
	struct registry_priv  *registry_par = &padapter->registrypriv;
	int status = _SUCCESS;

	GlobalDebugLevel23A = rtw_debug;
	registry_par->chip_version = (u8)rtw_chip_version;
	registry_par->rfintfs = (u8)rtw_rfintfs;
	memcpy(registry_par->ssid.ssid, "ANY", 3);
	registry_par->ssid.ssid_len = 3;
	registry_par->channel = (u8)rtw_channel;
	registry_par->wireless_mode = (u8)rtw_wireless_mode;
	registry_par->vrtl_carrier_sense = (u8)rtw_vrtl_carrier_sense;
	registry_par->vcs_type = (u8)rtw_vcs_type;
	registry_par->rts_thresh = (u16)rtw_rts_thresh;
	registry_par->frag_thresh = (u16)rtw_frag_thresh;
	registry_par->preamble = (u8)rtw_preamble;
	registry_par->scan_mode = (u8)rtw_scan_mode;
	registry_par->adhoc_tx_pwr = (u8)rtw_adhoc_tx_pwr;
	registry_par->soft_ap =  (u8)rtw_soft_ap;
	registry_par->smart_ps =  (u8)rtw_smart_ps;
	registry_par->power_mgnt = (u8)rtw_power_mgnt;
	registry_par->ips_mode = (u8)rtw_ips_mode;
	registry_par->long_retry_lmt = (u8)rtw_long_retry_lmt;
	registry_par->short_retry_lmt = (u8)rtw_short_retry_lmt;
	registry_par->busy_thresh = (u16)rtw_busy_thresh;
	registry_par->ack_policy = (u8)rtw_ack_policy;
	registry_par->acm_method = (u8)rtw_acm_method;
	 /* UAPSD */
	registry_par->wmm_enable = (u8)rtw_wmm_enable;
	registry_par->uapsd_enable = (u8)rtw_uapsd_enable;
	registry_par->ht_enable = (u8)rtw_ht_enable;
	registry_par->cbw40_enable = (u8)rtw_cbw40_enable;
	registry_par->ampdu_enable = (u8)rtw_ampdu_enable;
	registry_par->rx_stbc = (u8)rtw_rx_stbc;
	registry_par->ampdu_amsdu = (u8)rtw_ampdu_amsdu;
	registry_par->lowrate_two_xmit = (u8)rtw_lowrate_two_xmit;
	registry_par->rf_config = (u8)rtw_rf_config;
	registry_par->low_power = (u8)rtw_low_power;
	registry_par->wifi_spec = (u8)rtw_wifi_spec;
	registry_par->channel_plan = (u8)rtw_channel_plan;
#ifdef CONFIG_8723AU_BT_COEXIST
	registry_par->btcoex = (u8)rtw_btcoex_enable;
	registry_par->bt_iso = (u8)rtw_bt_iso;
	registry_par->bt_sco = (u8)rtw_bt_sco;
	registry_par->bt_ampdu = (u8)rtw_bt_ampdu;
#endif
	registry_par->bAcceptAddbaReq = (u8)rtw_AcceptAddbaReq;
	registry_par->antdiv_cfg = (u8)rtw_antdiv_cfg;
	registry_par->antdiv_type = (u8)rtw_antdiv_type;

	/* 0:disable, 1:enable, 2:by EFUSE config */
	registry_par->hwpdn_mode = (u8)rtw_hwpdn_mode;
	/* 0:disable, 1:enable */
	registry_par->hwpwrp_detect = (u8)rtw_hwpwrp_detect;
	registry_par->hw_wps_pbc = (u8)rtw_hw_wps_pbc;
	registry_par->max_roaming_times = (u8)rtw_max_roaming_times;
	registry_par->enable80211d = (u8)rtw_80211d;
	snprintf(registry_par->ifname, 16, "%s", ifname);
	snprintf(registry_par->if2name, 16, "%s", if2name);
	registry_par->notch_filter = (u8)rtw_notch_filter;
	registry_par->regulatory_tid = (u8)rtw_regulatory_id;
	return status;
}

static int rtw_net_set_mac_address(struct net_device *pnetdev, void *p)
{
	struct rtw_adapter *padapter = netdev_priv(pnetdev);
	struct sockaddr *addr = p;

	if (!padapter->bup)
		ether_addr_copy(padapter->eeprompriv.mac_addr, addr->sa_data);
	return 0;
}

static struct net_device_stats *rtw_net_get_stats(struct net_device *pnetdev)
{
	struct rtw_adapter *padapter = netdev_priv(pnetdev);
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct recv_priv *precvpriv = &padapter->recvpriv;

	padapter->stats.tx_packets = pxmitpriv->tx_pkts;
	padapter->stats.rx_packets = precvpriv->rx_pkts;
	padapter->stats.tx_dropped = pxmitpriv->tx_drop;
	padapter->stats.rx_dropped = precvpriv->rx_drop;
	padapter->stats.tx_bytes = pxmitpriv->tx_bytes;
	padapter->stats.rx_bytes = precvpriv->rx_bytes;

	return &padapter->stats;
}

/*
 * AC to queue mapping
 *
 * AC_VO -> queue 0
 * AC_VI -> queue 1
 * AC_BE -> queue 2
 * AC_BK -> queue 3
 */
static const u16 rtw_1d_to_queue[8] = { 2, 3, 3, 2, 1, 1, 0, 0 };

/* Given a data frame determine the 802.1p/1d tag to use. */
static u32 rtw_classify8021d(struct sk_buff *skb)
{
	u32 dscp;

	/* skb->priority values from 256->263 are magic values to
	 * directly indicate a specific 802.1d priority.  This is used
	 * to allow 802.1d priority to be passed directly in from VLAN
	 * tags, etc.
	 */
	if (skb->priority >= 256 && skb->priority <= 263)
		return skb->priority - 256;
	switch (skb->protocol) {
	case htons(ETH_P_IP):
		dscp = ip_hdr(skb)->tos & 0xfc;
		break;
	default:
		return 0;
	}
	return dscp >> 5;
}

static u16 rtw_select_queue(struct net_device *dev, struct sk_buff *skb,
			    void *accel_priv,
			    select_queue_fallback_t fallback)
{
	struct rtw_adapter *padapter = netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	skb->priority = rtw_classify8021d(skb);

	if (pmlmepriv->acm_mask != 0)
		skb->priority = qos_acm23a(pmlmepriv->acm_mask, skb->priority);
	return rtw_1d_to_queue[skb->priority];
}

u16 rtw_recv_select_queue23a(struct sk_buff *skb)
{
	struct iphdr *piphdr;
	struct ethhdr *eth = (struct ethhdr *)skb->data;
	unsigned int dscp;
	u16 eth_type = get_unaligned_be16(&eth->h_proto);
	u32 priority;
	u8 *pdata = skb->data;

	switch (eth_type) {
	case ETH_P_IP:
		piphdr = (struct iphdr *)(pdata + ETH_HLEN);
		dscp = piphdr->tos & 0xfc;
		priority = dscp >> 5;
		break;
	default:
		priority = 0;
	}
	return rtw_1d_to_queue[priority];
}

static const struct net_device_ops rtw_netdev_ops = {
	.ndo_open = netdev_open23a,
	.ndo_stop = netdev_close,
	.ndo_start_xmit = rtw_xmit23a_entry23a,
	.ndo_select_queue = rtw_select_queue,
	.ndo_set_mac_address = rtw_net_set_mac_address,
	.ndo_get_stats = rtw_net_get_stats,
};

int rtw_init_netdev23a_name23a(struct net_device *pnetdev, const char *ifname)
{
	if (dev_alloc_name(pnetdev, ifname) < 0) {
		RT_TRACE(_module_os_intfs_c_, _drv_err_,
			 ("dev_alloc_name, fail!\n"));
	}
	netif_carrier_off(pnetdev);
	return 0;
}

static const struct device_type wlan_type = {
	.name = "wlan",
};

struct net_device *rtw_init_netdev23a(struct rtw_adapter *old_padapter)
{
	struct rtw_adapter *padapter;
	struct net_device *pnetdev;

	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("+init_net_dev\n"));

	pnetdev = alloc_etherdev_mq(sizeof(struct rtw_adapter), 4);
	if (!pnetdev)
		return NULL;

	pnetdev->dev.type = &wlan_type;
	padapter = netdev_priv(pnetdev);
	padapter->pnetdev = pnetdev;

	DBG_8723A("register rtw_netdev_ops to netdev_ops\n");
	pnetdev->netdev_ops = &rtw_netdev_ops;

	pnetdev->watchdog_timeo = HZ*3; /* 3 second timeout */

	/* step 2. */
	loadparam(padapter, pnetdev);
	return pnetdev;
}

static int rtw_init_default_value(struct rtw_adapter *padapter)
{
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	int ret = _SUCCESS;

	/* xmit_priv */
	pxmitpriv->vcs_setting = pregistrypriv->vrtl_carrier_sense;
	pxmitpriv->vcs = pregistrypriv->vcs_type;
	pxmitpriv->vcs_type = pregistrypriv->vcs_type;
	/* pxmitpriv->rts_thresh = pregistrypriv->rts_thresh; */
	pxmitpriv->frag_len = pregistrypriv->frag_thresh;

	/* mlme_priv */
	pmlmepriv->scan_interval = SCAN_INTERVAL;/*  30*2 sec = 60sec */
	pmlmepriv->scan_mode = SCAN_ACTIVE;

	/* ht_priv */
	pmlmepriv->htpriv.ampdu_enable = false;/* set to disabled */

	/* security_priv */
	psecuritypriv->binstallGrpkey = 0;

	 /* open system */
	psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
	psecuritypriv->dot11PrivacyAlgrthm = 0;

	psecuritypriv->dot11PrivacyKeyIndex = 0;

	psecuritypriv->dot118021XGrpPrivacy = 0;
	psecuritypriv->dot118021XGrpKeyid = 1;

	psecuritypriv->ndisauthtype = Ndis802_11AuthModeOpen;
	psecuritypriv->ndisencryptstatus = Ndis802_11WEPDisabled;

	/* registry_priv */
	rtw_init_registrypriv_dev_network23a(padapter);
	rtw_update_registrypriv_dev_network23a(padapter);

	/* hal_priv */
	rtl8723a_init_default_value(padapter);

	/* misc. */
	padapter->bReadPortCancel = false;
	padapter->bWritePortCancel = false;
	padapter->bNotifyChannelChange = 0;
	return ret;
}

int rtw_reset_drv_sw23a(struct rtw_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;

	/* hal_priv */
	rtl8723a_init_default_value(padapter);
	padapter->bReadPortCancel = false;
	padapter->bWritePortCancel = false;
	pmlmepriv->scan_interval = SCAN_INTERVAL;/*  30*2 sec = 60sec */

	padapter->xmitpriv.tx_pkts = 0;
	padapter->recvpriv.rx_pkts = 0;

	pmlmepriv->LinkDetectInfo.bBusyTraffic = false;

	_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY | _FW_UNDER_LINKING);

	rtw_sreset_reset_value(padapter);
	pwrctrlpriv->pwr_state_check_cnts = 0;

	/* mlmeextpriv */
	padapter->mlmeextpriv.sitesurvey_res.state = SCAN_DISABLE;

	rtw_set_signal_stat_timer(&padapter->recvpriv);
	return _SUCCESS;
}

int rtw_init_drv_sw23a(struct rtw_adapter *padapter)
{
	int ret8 = _SUCCESS;

	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("+rtw_init_drv_sw23a\n"));

	if (rtw_init_cmd_priv23a(&padapter->cmdpriv) == _FAIL) {
		RT_TRACE(_module_os_intfs_c_, _drv_err_,
			 ("\n Can't init cmd_priv\n"));
		ret8 = _FAIL;
		goto exit;
	}

	padapter->cmdpriv.padapter = padapter;

	if (rtw_init_evt_priv23a(&padapter->evtpriv) == _FAIL) {
		RT_TRACE(_module_os_intfs_c_, _drv_err_,
			 ("\n Can't init evt_priv\n"));
		ret8 = _FAIL;
		goto exit;
	}

	if (rtw_init_mlme_priv23a(padapter) == _FAIL) {
		RT_TRACE(_module_os_intfs_c_, _drv_err_,
			 ("\n Can't init mlme_priv\n"));
		ret8 = _FAIL;
		goto exit;
	}


	if (init_mlme_ext_priv23a(padapter) == _FAIL) {
		RT_TRACE(_module_os_intfs_c_, _drv_err_,
			 ("\n Can't init mlme_ext_priv\n"));
		ret8 = _FAIL;
		goto exit;
	}

	if (_rtw_init_xmit_priv23a(&padapter->xmitpriv, padapter) == _FAIL) {
		DBG_8723A("Can't _rtw_init_xmit_priv23a\n");
		ret8 = _FAIL;
		goto exit;
	}

	if (_rtw_init_recv_priv23a(&padapter->recvpriv, padapter) == _FAIL) {
		DBG_8723A("Can't _rtw_init_recv_priv23a\n");
		ret8 = _FAIL;
		goto exit;
	}

	if (_rtw_init_sta_priv23a(&padapter->stapriv) == _FAIL) {
		DBG_8723A("Can't _rtw_init_sta_priv23a\n");
		ret8 = _FAIL;
		goto exit;
	}

	padapter->stapriv.padapter = padapter;
	padapter->setband = GHZ24_50;
	rtw_init_bcmc_stainfo23a(padapter);

	rtw_init_pwrctrl_priv23a(padapter);

	ret8 = rtw_init_default_value(padapter);

	rtl8723a_init_dm_priv(padapter);

	rtw_sreset_init(padapter);

exit:

	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("-rtw_init_drv_sw23a\n"));
	return ret8;
}

void rtw_cancel_all_timer23a(struct rtw_adapter *padapter)
{
	RT_TRACE(_module_os_intfs_c_, _drv_info_,
		 ("+rtw_cancel_all_timer23a\n"));

	del_timer_sync(&padapter->mlmepriv.assoc_timer);
	RT_TRACE(_module_os_intfs_c_, _drv_info_,
		 ("%s:cancel association timer complete!\n", __func__));

	del_timer_sync(&padapter->mlmepriv.scan_to_timer);
	RT_TRACE(_module_os_intfs_c_, _drv_info_,
		 ("%s:cancel scan_to_timer!\n", __func__));

	del_timer_sync(&padapter->mlmepriv.dynamic_chk_timer);
	RT_TRACE(_module_os_intfs_c_, _drv_info_,
		 ("%s:cancel dynamic_chk_timer!\n", __func__));

	RT_TRACE(_module_os_intfs_c_, _drv_info_,
		 ("%s:cancel DeInitSwLeds!\n", __func__));

	del_timer_sync(&padapter->pwrctrlpriv.pwr_state_check_timer);

	del_timer_sync(&padapter->mlmepriv.set_scan_deny_timer);
	rtw_clear_scan_deny(padapter);
	RT_TRACE(_module_os_intfs_c_, _drv_info_,
		 ("%s:cancel set_scan_deny_timer!\n", __func__));

	del_timer_sync(&padapter->recvpriv.signal_stat_timer);
	/* cancel dm timer */
	rtl8723a_deinit_dm_priv(padapter);
}

int rtw_free_drv_sw23a(struct rtw_adapter *padapter)
{
	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("==>rtw_free_drv_sw23a"));

	free_mlme_ext_priv23a(&padapter->mlmeextpriv);

	rtw_free_evt_priv23a(&padapter->evtpriv);

	rtw_free_mlme_priv23a(&padapter->mlmepriv);

	_rtw_free_xmit_priv23a(&padapter->xmitpriv);

	/* will free bcmc_stainfo here */
	_rtw_free_sta_priv23a(&padapter->stapriv);

	_rtw_free_recv_priv23a(&padapter->recvpriv);

	rtw_free_pwrctrl_priv(padapter);

	kfree(padapter->HalData);
	padapter->HalData = NULL;

	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("<== rtw_free_drv_sw23a\n"));

	/*  clear pbuddy_adapter to avoid access wrong pointer. */
	if (padapter->pbuddy_adapter != NULL)
		padapter->pbuddy_adapter->pbuddy_adapter = NULL;
	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("-rtw_free_drv_sw23a\n"));
	return _SUCCESS;
}

static int _rtw_drv_register_netdev(struct rtw_adapter *padapter, char *name)
{
	struct net_device *pnetdev = padapter->pnetdev;
	int ret = _SUCCESS;

	/* alloc netdev name */
	rtw_init_netdev23a_name23a(pnetdev, name);

	ether_addr_copy(pnetdev->dev_addr, padapter->eeprompriv.mac_addr);

	/* Tell the network stack we exist */
	if (register_netdev(pnetdev)) {
		DBG_8723A("%s(%s): Failed!\n", __func__, pnetdev->name);
		ret = _FAIL;
		goto error_register_netdev;
	}
	DBG_8723A("%s, MAC Address (if%d) = " MAC_FMT "\n", __func__,
		  (padapter->iface_id + 1), MAC_ARG(pnetdev->dev_addr));
	return ret;

error_register_netdev:

	if (padapter->iface_id > IFACE_ID0) {
		rtw_free_drv_sw23a(padapter);

		free_netdev(pnetdev);
	}
	return ret;
}

int rtw_drv_register_netdev(struct rtw_adapter *if1)
{
	struct dvobj_priv *dvobj = if1->dvobj;
	int i, status = _SUCCESS;

	if (dvobj->iface_nums >= IFACE_ID_MAX) {
		status = _FAIL; /* -EINVAL */
		goto exit;
	}

	for (i = 0; i < dvobj->iface_nums; i++) {
		struct rtw_adapter *padapter = dvobj->padapters[i];

		if (padapter) {
			char *name;

			if (padapter->iface_id == IFACE_ID0)
				name = if1->registrypriv.ifname;
			else if (padapter->iface_id == IFACE_ID1)
				name = if1->registrypriv.if2name;
			else
				name = "wlan%d";
			status = _rtw_drv_register_netdev(padapter, name);
			if (status != _SUCCESS)
				break;
		}
	}

exit:
	return status;
}

int netdev_open23a(struct net_device *pnetdev)
{
	struct rtw_adapter *padapter = netdev_priv(pnetdev);
	struct pwrctrl_priv *pwrctrlpriv;
	int ret = 0;
	int status;

	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("+871x_drv - dev_open\n"));
	DBG_8723A("+871x_drv - drv_open, bup =%d\n", padapter->bup);

	mutex_lock(&adapter_to_dvobj(padapter)->hw_init_mutex);

	pwrctrlpriv = &padapter->pwrctrlpriv;
	if (pwrctrlpriv->ps_flag) {
		padapter->net_closed = false;
		goto netdev_open23a_normal_process;
	}

	if (!padapter->bup) {
		padapter->bDriverStopped = false;
		padapter->bSurpriseRemoved = false;
		padapter->bCardDisableWOHSM = false;

		status = rtw_hal_init23a(padapter);
		if (status == _FAIL) {
			RT_TRACE(_module_os_intfs_c_, _drv_err_,
				 ("rtl871x_hal_init(): Can't init h/w!\n"));
			goto netdev_open23a_error;
		}

		DBG_8723A("MAC Address = "MAC_FMT"\n",
			  MAC_ARG(pnetdev->dev_addr));

		if (init_hw_mlme_ext23a(padapter) == _FAIL) {
			DBG_8723A("can't init mlme_ext_priv\n");
			goto netdev_open23a_error;
		}

		rtl8723au_inirp_init(padapter);

		rtw_cfg80211_init_wiphy(padapter);

		rtw_led_control(padapter, LED_CTL_NO_LINK);

		padapter->bup = true;
	}
	padapter->net_closed = false;

	mod_timer(&padapter->mlmepriv.dynamic_chk_timer,
		  jiffies + msecs_to_jiffies(2000));

	padapter->pwrctrlpriv.bips_processing = false;
	rtw_set_pwr_state_check_timer(&padapter->pwrctrlpriv);

	/* netif_carrier_on(pnetdev);call this func when
	   rtw23a_joinbss_event_cb return success */
	if (!rtw_netif_queue_stopped(pnetdev))
		netif_tx_start_all_queues(pnetdev);
	else
		netif_tx_wake_all_queues(pnetdev);

netdev_open23a_normal_process:
	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("-871x_drv - dev_open\n"));
	DBG_8723A("-871x_drv - drv_open, bup =%d\n", padapter->bup);
exit:
	mutex_unlock(&adapter_to_dvobj(padapter)->hw_init_mutex);
	return ret;

netdev_open23a_error:
	padapter->bup = false;

	netif_carrier_off(pnetdev);
	netif_tx_stop_all_queues(pnetdev);

	RT_TRACE(_module_os_intfs_c_, _drv_err_,
		 ("-871x_drv - dev_open, fail!\n"));
	DBG_8723A("-871x_drv - drv_open fail, bup =%d\n", padapter->bup);

	ret = -1;
	goto exit;
}

static int ips_netdrv_open(struct rtw_adapter *padapter)
{
	int status = _SUCCESS;

	padapter->net_closed = false;
	DBG_8723A("===> %s.........\n", __func__);

	padapter->bDriverStopped = false;
	padapter->bSurpriseRemoved = false;
	padapter->bCardDisableWOHSM = false;

	status = rtw_hal_init23a(padapter);
	if (status == _FAIL) {
		RT_TRACE(_module_os_intfs_c_, _drv_err_,
			 ("ips_netdrv_open(): Can't init h/w!\n"));
		goto netdev_open23a_error;
	}

	rtl8723au_inirp_init(padapter);

	rtw_set_pwr_state_check_timer(&padapter->pwrctrlpriv);
	mod_timer(&padapter->mlmepriv.dynamic_chk_timer,
		  jiffies + msecs_to_jiffies(5000));

	return _SUCCESS;

netdev_open23a_error:
	/* padapter->bup = false; */
	DBG_8723A("-ips_netdrv_open - drv_open failure, bup =%d\n",
		  padapter->bup);

	return _FAIL;
}

int rtw_ips_pwr_up23a(struct rtw_adapter *padapter)
{
	int result;
	unsigned long start_time = jiffies;

	DBG_8723A("===>  rtw_ips_pwr_up23a..............\n");
	rtw_reset_drv_sw23a(padapter);

	result = ips_netdrv_open(padapter);

	rtw_led_control(padapter, LED_CTL_NO_LINK);

	DBG_8723A("<===  rtw_ips_pwr_up23a.............. in %dms\n",
		  jiffies_to_msecs(jiffies - start_time));
	return result;
}

void rtw_ips_pwr_down23a(struct rtw_adapter *padapter)
{
	unsigned long start_time = jiffies;

	DBG_8723A("===> rtw_ips_pwr_down23a...................\n");

	padapter->bCardDisableWOHSM = true;
	padapter->net_closed = true;

	rtw_led_control(padapter, LED_CTL_POWER_OFF);

	rtw_ips_dev_unload23a(padapter);
	padapter->bCardDisableWOHSM = false;
	DBG_8723A("<=== rtw_ips_pwr_down23a..................... in %dms\n",
		  jiffies_to_msecs(jiffies - start_time));
}

void rtw_ips_dev_unload23a(struct rtw_adapter *padapter)
{
	rtl8723a_fifo_cleanup(padapter);

	rtl8723a_usb_intf_stop(padapter);

	/* s5. */
	if (!padapter->bSurpriseRemoved)
		rtw_hal_deinit23a(padapter);
}

int pm_netdev_open23a(struct net_device *pnetdev, u8 bnormal)
{
	int status;

	if (bnormal)
		status = netdev_open23a(pnetdev);
	else
		status = (_SUCCESS == ips_netdrv_open(netdev_priv(pnetdev))) ?
			 (0) : (-1);

	return status;
}

static int netdev_close(struct net_device *pnetdev)
{
	struct rtw_adapter *padapter = netdev_priv(pnetdev);

	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("+871x_drv - drv_close\n"));

	padapter->net_closed = true;

	if (padapter->pwrctrlpriv.rf_pwrstate == rf_on) {
		DBG_8723A("(2)871x_drv - drv_close, bup =%d, "
			  "hw_init_completed =%d\n", padapter->bup,
			  padapter->hw_init_completed);

		/* s1. */
		if (pnetdev) {
			if (!rtw_netif_queue_stopped(pnetdev))
				netif_tx_stop_all_queues(pnetdev);
		}

		/* s2. */
		LeaveAllPowerSaveMode23a(padapter);
		rtw_disassoc_cmd23a(padapter, 500, false);
		/* s2-2.  indicate disconnect to os */
		rtw_indicate_disconnect23a(padapter);
		/* s2-3. */
		rtw_free_assoc_resources23a(padapter, 1);
		/* s2-4. */
		rtw_free_network_queue23a(padapter);
		/*  Close LED */
		rtw_led_control(padapter, LED_CTL_POWER_OFF);
	}

	rtw_scan_abort23a(padapter);

	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("-871x_drv - drv_close\n"));
	DBG_8723A("-871x_drv - drv_close, bup =%d\n", padapter->bup);

	return 0;
}

void rtw_ndev_destructor(struct net_device *ndev)
{
	DBG_8723A("%s(%s)\n", __func__, ndev->name);
	kfree(ndev->ieee80211_ptr);
	free_netdev(ndev);
}

void _rtw_init_queue23a(struct rtw_queue *pqueue)
{
	INIT_LIST_HEAD(&pqueue->queue);
	spin_lock_init(&pqueue->lock);
}
