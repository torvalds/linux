// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _OS_INTFS_C_

#include <drv_types.h>
#include <rtw_debug.h>
#include <hal_data.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek Wireless Lan Driver");
MODULE_AUTHOR("Realtek Semiconductor Corp.");
MODULE_VERSION(DRIVERVERSION);

/* module param defaults */
static int rtw_chip_version;
static int rtw_rfintfs = HWPI;
static int rtw_lbkmode;/* RTL8712_AIR_TRX; */


static int rtw_network_mode = Ndis802_11IBSS;/* Ndis802_11Infrastructure;infra, ad-hoc, auto */
/* struct ndis_802_11_ssid	ssid; */
static int rtw_channel = 1;/* ad-hoc support requirement */
static int rtw_wireless_mode = WIRELESS_MODE_MAX;
static int rtw_vrtl_carrier_sense = AUTO_VCS;
static int rtw_vcs_type = RTS_CTS;/*  */
static int rtw_rts_thresh = 2347;/*  */
static int rtw_frag_thresh = 2346;/*  */
static int rtw_preamble = PREAMBLE_LONG;/* long, short, auto */
static int rtw_scan_mode = 1;/* active, passive */
static int rtw_adhoc_tx_pwr = 1;
static int rtw_soft_ap;
/* int smart_ps = 1; */
static int rtw_power_mgnt = 1;
static int rtw_ips_mode = IPS_NORMAL;
module_param(rtw_ips_mode, int, 0644);
MODULE_PARM_DESC(rtw_ips_mode, "The default IPS mode");

static int rtw_smart_ps = 2;

static int rtw_check_fw_ps = 1;

static int rtw_usb_rxagg_mode = 2;/* USB_RX_AGG_DMA = 1, USB_RX_AGG_USB =2 */
module_param(rtw_usb_rxagg_mode, int, 0644);

static int rtw_radio_enable = 1;
static int rtw_long_retry_lmt = 7;
static int rtw_short_retry_lmt = 7;
static int rtw_busy_thresh = 40;
/* int qos_enable = 0; */
static int rtw_ack_policy = NORMAL_ACK;

static int rtw_software_encrypt;
static int rtw_software_decrypt;

static int rtw_acm_method;/*  0:By SW 1:By HW. */

static int rtw_wmm_enable = 1;/*  default is set to enable the wmm. */
static int rtw_uapsd_enable;
static int rtw_uapsd_max_sp = NO_LIMIT;
static int rtw_uapsd_acbk_en;
static int rtw_uapsd_acbe_en;
static int rtw_uapsd_acvi_en;
static int rtw_uapsd_acvo_en;

int rtw_ht_enable = 1;
/*  0: 20 MHz, 1: 40 MHz, 2: 80 MHz, 3: 160MHz, 4: 80+80MHz */
/*  2.4G use bit 0 ~ 3, 5G use bit 4 ~ 7 */
/*  0x21 means enable 2.4G 40MHz & 5G 80MHz */
static int rtw_bw_mode = 0x21;
static int rtw_ampdu_enable = 1;/* for enable tx_ampdu ,0: disable, 0x1:enable (but wifi_spec should be 0), 0x2: force enable (don't care wifi_spec) */
static int rtw_rx_stbc = 1;/*  0: disable, 1:enable 2.4g */
static int rtw_ampdu_amsdu;/*  0: disabled, 1:enabled, 2:auto . There is an IOT issu with DLINK DIR-629 when the flag turn on */
/*  Short GI support Bit Map */
/*  BIT0 - 20MHz, 0: non-support, 1: support */
/*  BIT1 - 40MHz, 0: non-support, 1: support */
/*  BIT2 - 80MHz, 0: non-support, 1: support */
/*  BIT3 - 160MHz, 0: non-support, 1: support */
static int rtw_short_gi = 0xf;
/*  BIT0: Enable VHT LDPC Rx, BIT1: Enable VHT LDPC Tx, BIT4: Enable HT LDPC Rx, BIT5: Enable HT LDPC Tx */
static int rtw_ldpc_cap = 0x33;
/*  BIT0: Enable VHT STBC Rx, BIT1: Enable VHT STBC Tx, BIT4: Enable HT STBC Rx, BIT5: Enable HT STBC Tx */
static int rtw_stbc_cap = 0x13;
/*  BIT0: Enable VHT Beamformer, BIT1: Enable VHT Beamformee, BIT4: Enable HT Beamformer, BIT5: Enable HT Beamformee */
static int rtw_beamform_cap = 0x2;

static int rtw_lowrate_two_xmit = 1;/* Use 2 path Tx to transmit MCS0~7 and legacy mode */

/* int rf_config = RF_1T2R;  1T2R */
static int rtw_rf_config = RF_MAX_TYPE;  /* auto */
static int rtw_low_power;
static int rtw_wifi_spec;
static int rtw_channel_plan = RT_CHANNEL_DOMAIN_MAX;

static int rtw_btcoex_enable = 1;
module_param(rtw_btcoex_enable, int, 0644);
MODULE_PARM_DESC(rtw_btcoex_enable, "Enable BT co-existence mechanism");
static int rtw_bt_iso = 2;/*  0:Low, 1:High, 2:From Efuse */
static int rtw_bt_sco = 3;/*  0:Idle, 1:None-SCO, 2:SCO, 3:From Counter, 4.Busy, 5.OtherBusy */
static int rtw_bt_ampdu = 1 ;/*  0:Disable BT control A-MPDU, 1:Enable BT control A-MPDU. */
static int rtw_ant_num = -1; /*  <0: undefined, >0: Antenna number */
module_param(rtw_ant_num, int, 0644);
MODULE_PARM_DESC(rtw_ant_num, "Antenna number setting");

static int rtw_antdiv_cfg = 1; /*  0:OFF , 1:ON, 2:decide by Efuse config */
static int rtw_antdiv_type; /* 0:decide by efuse  1: for 88EE, 1Tx and 1RxCG are diversity.(2 Ant with SPDT), 2:  for 88EE, 1Tx and 2Rx are diversity.(2 Ant, Tx and RxCG are both on aux port, RxCS is on main port), 3: for 88EE, 1Tx and 1RxCG are fixed.(1Ant, Tx and RxCG are both on aux port) */


static int rtw_enusbss;/* 0:disable, 1:enable */

static int rtw_hwpdn_mode = 2;/* 0:disable, 1:enable, 2: by EFUSE config */

#ifdef CONFIG_HW_PWRP_DETECTION
static int rtw_hwpwrp_detect = 1;
#else
static int rtw_hwpwrp_detect; /* HW power  ping detect 0:disable , 1:enable */
#endif

static int rtw_hw_wps_pbc;

int rtw_mc2u_disable = 0;

static int rtw_80211d;

#ifdef CONFIG_QOS_OPTIMIZATION
static int rtw_qos_opt_enable = 1;/* 0: disable, 1:enable */
#else
static int rtw_qos_opt_enable;/* 0: disable, 1:enable */
#endif
module_param(rtw_qos_opt_enable, int, 0644);

static char *ifname = "wlan%d";
module_param(ifname, charp, 0644);
MODULE_PARM_DESC(ifname, "The default name to allocate for first interface");

char *rtw_initmac = NULL;  /*  temp mac address if users want to use instead of the mac address in Efuse */

module_param(rtw_initmac, charp, 0644);
module_param(rtw_channel_plan, int, 0644);
module_param(rtw_chip_version, int, 0644);
module_param(rtw_rfintfs, int, 0644);
module_param(rtw_lbkmode, int, 0644);
module_param(rtw_network_mode, int, 0644);
module_param(rtw_channel, int, 0644);
module_param(rtw_wmm_enable, int, 0644);
module_param(rtw_vrtl_carrier_sense, int, 0644);
module_param(rtw_vcs_type, int, 0644);
module_param(rtw_busy_thresh, int, 0644);

module_param(rtw_ht_enable, int, 0644);
module_param(rtw_bw_mode, int, 0644);
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
module_param(rtw_antdiv_type, int, 0644);

module_param(rtw_enusbss, int, 0644);
module_param(rtw_hwpdn_mode, int, 0644);
module_param(rtw_hwpwrp_detect, int, 0644);

module_param(rtw_hw_wps_pbc, int, 0644);

static uint rtw_max_roaming_times = 2;
module_param(rtw_max_roaming_times, uint, 0644);
MODULE_PARM_DESC(rtw_max_roaming_times, "The max roaming times to try");

module_param(rtw_mc2u_disable, int, 0644);

module_param(rtw_80211d, int, 0644);
MODULE_PARM_DESC(rtw_80211d, "Enable 802.11d mechanism");

static uint rtw_notch_filter;
module_param(rtw_notch_filter, uint, 0644);
MODULE_PARM_DESC(rtw_notch_filter, "0:Disable, 1:Enable, 2:Enable only for P2P");

#define CONFIG_RTW_HIQ_FILTER 1

static uint rtw_hiq_filter = CONFIG_RTW_HIQ_FILTER;
module_param(rtw_hiq_filter, uint, 0644);
MODULE_PARM_DESC(rtw_hiq_filter, "0:allow all, 1:allow special, 2:deny all");

static int rtw_tx_pwr_lmt_enable;
static int rtw_tx_pwr_by_rate;

module_param(rtw_tx_pwr_lmt_enable, int, 0644);
MODULE_PARM_DESC(rtw_tx_pwr_lmt_enable, "0:Disable, 1:Enable, 2: Depend on efuse");

module_param(rtw_tx_pwr_by_rate, int, 0644);
MODULE_PARM_DESC(rtw_tx_pwr_by_rate, "0:Disable, 1:Enable, 2: Depend on efuse");

int _netdev_open(struct net_device *pnetdev);
int netdev_open(struct net_device *pnetdev);
static int netdev_close(struct net_device *pnetdev);

static void loadparam(struct adapter *padapter, _nic_hdl pnetdev)
{
	struct registry_priv  *registry_par = &padapter->registrypriv;

	registry_par->chip_version = (u8)rtw_chip_version;
	registry_par->rfintfs = (u8)rtw_rfintfs;
	registry_par->lbkmode = (u8)rtw_lbkmode;
	/* registry_par->hci = (u8)hci; */
	registry_par->network_mode  = (u8)rtw_network_mode;

	memcpy(registry_par->ssid.Ssid, "ANY", 3);
	registry_par->ssid.SsidLength = 3;

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
	registry_par->check_fw_ps = (u8)rtw_check_fw_ps;
	registry_par->power_mgnt = (u8)rtw_power_mgnt;
	registry_par->ips_mode = (u8)rtw_ips_mode;
	registry_par->radio_enable = (u8)rtw_radio_enable;
	registry_par->long_retry_lmt = (u8)rtw_long_retry_lmt;
	registry_par->short_retry_lmt = (u8)rtw_short_retry_lmt;
	registry_par->busy_thresh = (u16)rtw_busy_thresh;
	/* registry_par->qos_enable = (u8)rtw_qos_enable; */
	registry_par->ack_policy = (u8)rtw_ack_policy;
	registry_par->software_encrypt = (u8)rtw_software_encrypt;
	registry_par->software_decrypt = (u8)rtw_software_decrypt;

	registry_par->acm_method = (u8)rtw_acm_method;
	registry_par->usb_rxagg_mode = (u8)rtw_usb_rxagg_mode;

	 /* UAPSD */
	registry_par->wmm_enable = (u8)rtw_wmm_enable;
	registry_par->uapsd_enable = (u8)rtw_uapsd_enable;
	registry_par->uapsd_max_sp = (u8)rtw_uapsd_max_sp;
	registry_par->uapsd_acbk_en = (u8)rtw_uapsd_acbk_en;
	registry_par->uapsd_acbe_en = (u8)rtw_uapsd_acbe_en;
	registry_par->uapsd_acvi_en = (u8)rtw_uapsd_acvi_en;
	registry_par->uapsd_acvo_en = (u8)rtw_uapsd_acvo_en;

	registry_par->ht_enable = (u8)rtw_ht_enable;
	registry_par->bw_mode = (u8)rtw_bw_mode;
	registry_par->ampdu_enable = (u8)rtw_ampdu_enable;
	registry_par->rx_stbc = (u8)rtw_rx_stbc;
	registry_par->ampdu_amsdu = (u8)rtw_ampdu_amsdu;
	registry_par->short_gi = (u8)rtw_short_gi;
	registry_par->ldpc_cap = (u8)rtw_ldpc_cap;
	registry_par->stbc_cap = (u8)rtw_stbc_cap;
	registry_par->beamform_cap = (u8)rtw_beamform_cap;

	registry_par->lowrate_two_xmit = (u8)rtw_lowrate_two_xmit;
	registry_par->rf_config = (u8)rtw_rf_config;
	registry_par->low_power = (u8)rtw_low_power;


	registry_par->wifi_spec = (u8)rtw_wifi_spec;

	registry_par->channel_plan = (u8)rtw_channel_plan;

	registry_par->btcoex = (u8)rtw_btcoex_enable;
	registry_par->bt_iso = (u8)rtw_bt_iso;
	registry_par->bt_sco = (u8)rtw_bt_sco;
	registry_par->bt_ampdu = (u8)rtw_bt_ampdu;
	registry_par->ant_num = (s8)rtw_ant_num;

	registry_par->accept_addba_req = true;

	registry_par->antdiv_cfg = (u8)rtw_antdiv_cfg;
	registry_par->antdiv_type = (u8)rtw_antdiv_type;

	registry_par->hw_wps_pbc = (u8)rtw_hw_wps_pbc;

	registry_par->max_roaming_times = (u8)rtw_max_roaming_times;

	registry_par->enable80211d = (u8)rtw_80211d;

	snprintf(registry_par->ifname, 16, "%s", ifname);

	registry_par->notch_filter = (u8)rtw_notch_filter;

	registry_par->RegEnableTxPowerLimit = (u8)rtw_tx_pwr_lmt_enable;
	registry_par->RegEnableTxPowerByRate = (u8)rtw_tx_pwr_by_rate;

	registry_par->RegPowerBase = 14;
	registry_par->TxBBSwing_2G = 0xFF;
	registry_par->TxBBSwing_5G = 0xFF;
	registry_par->bEn_RFE = 1;
	registry_par->RFE_Type = 64;

	registry_par->qos_opt_enable = (u8)rtw_qos_opt_enable;

	registry_par->hiq_filter = (u8)rtw_hiq_filter;
}

static int rtw_net_set_mac_address(struct net_device *pnetdev, void *p)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(pnetdev);
	struct sockaddr *addr = p;

	if (!padapter->bup) {
		/* DBG_871X("r8711_net_set_mac_address(), MAC =%x:%x:%x:%x:%x:%x\n", addr->sa_data[0], addr->sa_data[1], addr->sa_data[2], addr->sa_data[3], */
		/* addr->sa_data[4], addr->sa_data[5]); */
		memcpy(padapter->eeprompriv.mac_addr, addr->sa_data, ETH_ALEN);
		/* memcpy(pnetdev->dev_addr, addr->sa_data, ETH_ALEN); */
		/* padapter->bset_hwaddr = true; */
	}

	return 0;
}

static struct net_device_stats *rtw_net_get_stats(struct net_device *pnetdev)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(pnetdev);
	struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);
	struct recv_priv *precvpriv = &(padapter->recvpriv);

	padapter->stats.tx_packets = pxmitpriv->tx_pkts;/* pxmitpriv->tx_pkts++; */
	padapter->stats.rx_packets = precvpriv->rx_pkts;/* precvpriv->rx_pkts++; */
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
static unsigned int rtw_classify8021d(struct sk_buff *skb)
{
	unsigned int dscp;

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
			    struct net_device *sb_dev)
{
	struct adapter	*padapter = rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	skb->priority = rtw_classify8021d(skb);

	if (pmlmepriv->acm_mask != 0)
		skb->priority = qos_acm(pmlmepriv->acm_mask, skb->priority);

	return rtw_1d_to_queue[skb->priority];
}

u16 rtw_recv_select_queue(struct sk_buff *skb)
{
	struct iphdr *piphdr;
	unsigned int dscp;
	__be16	eth_type;
	u32 priority;
	u8 *pdata = skb->data;

	memcpy(&eth_type, pdata + (ETH_ALEN << 1), 2);

	switch (be16_to_cpu(eth_type)) {
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

static int rtw_ndev_notifier_call(struct notifier_block *nb, unsigned long state, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	if (dev->netdev_ops->ndo_do_ioctl != rtw_ioctl)
		return NOTIFY_DONE;

	DBG_871X_LEVEL(_drv_info_, FUNC_NDEV_FMT " state:%lu\n", FUNC_NDEV_ARG(dev), state);

	return NOTIFY_DONE;
}

static struct notifier_block rtw_ndev_notifier = {
	.notifier_call = rtw_ndev_notifier_call,
};

int rtw_ndev_notifier_register(void)
{
	return register_netdevice_notifier(&rtw_ndev_notifier);
}

void rtw_ndev_notifier_unregister(void)
{
	unregister_netdevice_notifier(&rtw_ndev_notifier);
}


static int rtw_ndev_init(struct net_device *dev)
{
	struct adapter *adapter = rtw_netdev_priv(dev);

	DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT "\n", FUNC_ADPT_ARG(adapter));
	strncpy(adapter->old_ifname, dev->name, IFNAMSIZ);

	return 0;
}

static void rtw_ndev_uninit(struct net_device *dev)
{
	struct adapter *adapter = rtw_netdev_priv(dev);

	DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT "\n", FUNC_ADPT_ARG(adapter));
}

static const struct net_device_ops rtw_netdev_ops = {
	.ndo_init = rtw_ndev_init,
	.ndo_uninit = rtw_ndev_uninit,
	.ndo_open = netdev_open,
	.ndo_stop = netdev_close,
	.ndo_start_xmit = rtw_xmit_entry,
	.ndo_select_queue	= rtw_select_queue,
	.ndo_set_mac_address = rtw_net_set_mac_address,
	.ndo_get_stats = rtw_net_get_stats,
	.ndo_do_ioctl = rtw_ioctl,
};

int rtw_init_netdev_name(struct net_device *pnetdev, const char *ifname)
{
	if (dev_alloc_name(pnetdev, ifname) < 0) {
		pr_err("dev_alloc_name, fail for %s\n", ifname);
		return 1;
	}
	netif_carrier_off(pnetdev);
	/* rtw_netif_stop_queue(pnetdev); */

	return 0;
}

struct net_device *rtw_init_netdev(struct adapter *old_padapter)
{
	struct adapter *padapter;
	struct net_device *pnetdev;

	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("+init_net_dev\n"));

	if (old_padapter != NULL)
		pnetdev = rtw_alloc_etherdev_with_old_priv(sizeof(struct adapter), (void *)old_padapter);
	else
		pnetdev = rtw_alloc_etherdev(sizeof(struct adapter));

	pr_info("pnetdev = %p\n", pnetdev);
	if (!pnetdev)
		return NULL;

	padapter = rtw_netdev_priv(pnetdev);
	padapter->pnetdev = pnetdev;

	/* pnetdev->init = NULL; */

	DBG_871X("register rtw_netdev_ops to netdev_ops\n");
	pnetdev->netdev_ops = &rtw_netdev_ops;

	/* pnetdev->tx_timeout = NULL; */
	pnetdev->watchdog_timeo = HZ * 3; /* 3 second timeout */
	pnetdev->wireless_handlers = (struct iw_handler_def *)&rtw_handlers_def;

	/* step 2. */
	loadparam(padapter, pnetdev);

	return pnetdev;
}

void rtw_unregister_netdevs(struct dvobj_priv *dvobj)
{
	struct adapter *padapter = NULL;
	struct net_device *pnetdev = NULL;

	padapter = dvobj->padapters;

	if (padapter == NULL)
		return;

	pnetdev = padapter->pnetdev;

	if ((padapter->DriverState != DRIVER_DISAPPEAR) && pnetdev)
		unregister_netdev(pnetdev); /* will call netdev_close() */
	rtw_wdev_unregister(padapter->rtw_wdev);
}

u32 rtw_start_drv_threads(struct adapter *padapter)
{
	u32 _status = _SUCCESS;

	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("+rtw_start_drv_threads\n"));
	padapter->xmitThread = kthread_run(rtw_xmit_thread, padapter, "RTW_XMIT_THREAD");
	if (IS_ERR(padapter->xmitThread))
		_status = _FAIL;

	padapter->cmdThread = kthread_run(rtw_cmd_thread, padapter, "RTW_CMD_THREAD");
	if (IS_ERR(padapter->cmdThread))
		_status = _FAIL;
	else
		wait_for_completion(&padapter->cmdpriv.terminate_cmdthread_comp); /* wait for cmd_thread to run */

	rtw_hal_start_thread(padapter);
	return _status;
}

void rtw_stop_drv_threads(struct adapter *padapter)
{
	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("+rtw_stop_drv_threads\n"));

	rtw_stop_cmd_thread(padapter);

	/*  Below is to termindate tx_thread... */
	complete(&padapter->xmitpriv.xmit_comp);
	wait_for_completion(&padapter->xmitpriv.terminate_xmitthread_comp);
	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("\n drv_halt: rtw_xmit_thread can be terminated !\n"));

	rtw_hal_stop_thread(padapter);
}

static void rtw_init_default_value(struct adapter *padapter)
{
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	/* xmit_priv */
	pxmitpriv->vcs_setting = pregistrypriv->vrtl_carrier_sense;
	pxmitpriv->vcs = pregistrypriv->vcs_type;
	pxmitpriv->vcs_type = pregistrypriv->vcs_type;
	/* pxmitpriv->rts_thresh = pregistrypriv->rts_thresh; */
	pxmitpriv->frag_len = pregistrypriv->frag_thresh;

	/* recv_priv */

	/* mlme_priv */
	pmlmepriv->scan_mode = SCAN_ACTIVE;

	/* qos_priv */
	/* pmlmepriv->qospriv.qos_option = pregistrypriv->wmm_enable; */

	/* ht_priv */
	pmlmepriv->htpriv.ampdu_enable = false;/* set to disabled */

	/* security_priv */
	/* rtw_get_encrypt_decrypt_from_registrypriv(padapter); */
	psecuritypriv->binstallGrpkey = _FAIL;
#ifdef CONFIG_GTK_OL
	psecuritypriv->binstallKCK_KEK = _FAIL;
#endif /* CONFIG_GTK_OL */
	psecuritypriv->sw_encrypt = pregistrypriv->software_encrypt;
	psecuritypriv->sw_decrypt = pregistrypriv->software_decrypt;

	psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open; /* open system */
	psecuritypriv->dot11PrivacyAlgrthm = _NO_PRIVACY_;

	psecuritypriv->dot11PrivacyKeyIndex = 0;

	psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
	psecuritypriv->dot118021XGrpKeyid = 1;

	psecuritypriv->ndisauthtype = Ndis802_11AuthModeOpen;
	psecuritypriv->ndisencryptstatus = Ndis802_11WEPDisabled;

	/* registry_priv */
	rtw_init_registrypriv_dev_network(padapter);
	rtw_update_registrypriv_dev_network(padapter);

	/* hal_priv */
	rtw_hal_def_value_init(padapter);

	/* misc. */
	RTW_ENABLE_FUNC(padapter, DF_RX_BIT);
	RTW_ENABLE_FUNC(padapter, DF_TX_BIT);
	padapter->bLinkInfoDump = 0;
	padapter->bNotifyChannelChange = 0;

	/* for debug purpose */
	padapter->fix_rate = 0xFF;
	padapter->driver_ampdu_spacing = 0xFF;
	padapter->driver_rx_ampdu_factor =  0xFF;

}

struct dvobj_priv *devobj_init(void)
{
	struct dvobj_priv *pdvobj = NULL;

	pdvobj = rtw_zmalloc(sizeof(*pdvobj));
	if (pdvobj == NULL)
		return NULL;

	mutex_init(&pdvobj->hw_init_mutex);
	mutex_init(&pdvobj->h2c_fwcmd_mutex);
	mutex_init(&pdvobj->setch_mutex);
	mutex_init(&pdvobj->setbw_mutex);

	spin_lock_init(&pdvobj->lock);

	pdvobj->macid[1] = true; /* macid = 1 for bc/mc stainfo */

	pdvobj->processing_dev_remove = false;

	atomic_set(&pdvobj->disable_func, 0);

	spin_lock_init(&pdvobj->cam_ctl.lock);

	return pdvobj;
}

void devobj_deinit(struct dvobj_priv *pdvobj)
{
	if (!pdvobj)
		return;

	mutex_destroy(&pdvobj->hw_init_mutex);
	mutex_destroy(&pdvobj->h2c_fwcmd_mutex);
	mutex_destroy(&pdvobj->setch_mutex);
	mutex_destroy(&pdvobj->setbw_mutex);

	kfree(pdvobj);
}

void rtw_reset_drv_sw(struct adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	/* hal_priv */
	if (is_primary_adapter(padapter))
		rtw_hal_def_value_init(padapter);

	RTW_ENABLE_FUNC(padapter, DF_RX_BIT);
	RTW_ENABLE_FUNC(padapter, DF_TX_BIT);
	padapter->bLinkInfoDump = 0;

	padapter->xmitpriv.tx_pkts = 0;
	padapter->recvpriv.rx_pkts = 0;

	pmlmepriv->LinkDetectInfo.bBusyTraffic = false;

	/* pmlmepriv->LinkDetectInfo.TrafficBusyState = false; */
	pmlmepriv->LinkDetectInfo.TrafficTransitionCount = 0;
	pmlmepriv->LinkDetectInfo.LowPowerTransitionCount = 0;

	_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY | _FW_UNDER_LINKING);

	pwrctrlpriv->pwr_state_check_cnts = 0;

	/* mlmeextpriv */
	padapter->mlmeextpriv.sitesurvey_res.state = SCAN_DISABLE;

	rtw_set_signal_stat_timer(&padapter->recvpriv);

}


u8 rtw_init_drv_sw(struct adapter *padapter)
{
	u8 ret8 = _SUCCESS;

	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("+rtw_init_drv_sw\n"));

	rtw_init_default_value(padapter);

	rtw_init_hal_com_default_value(padapter);

	if (rtw_init_cmd_priv(&padapter->cmdpriv)) {
		RT_TRACE(_module_os_intfs_c_, _drv_err_, ("\n Can't init cmd_priv\n"));
		ret8 = _FAIL;
		goto exit;
	}

	padapter->cmdpriv.padapter = padapter;

	if (rtw_init_evt_priv(&padapter->evtpriv)) {
		RT_TRACE(_module_os_intfs_c_, _drv_err_, ("\n Can't init evt_priv\n"));
		ret8 = _FAIL;
		goto exit;
	}


	if (rtw_init_mlme_priv(padapter) == _FAIL) {
		RT_TRACE(_module_os_intfs_c_, _drv_err_, ("\n Can't init mlme_priv\n"));
		ret8 = _FAIL;
		goto exit;
	}

	init_mlme_ext_priv(padapter);

	if (_rtw_init_xmit_priv(&padapter->xmitpriv, padapter) == _FAIL) {
		DBG_871X("Can't _rtw_init_xmit_priv\n");
		ret8 = _FAIL;
		goto exit;
	}

	if (_rtw_init_recv_priv(&padapter->recvpriv, padapter) == _FAIL) {
		DBG_871X("Can't _rtw_init_recv_priv\n");
		ret8 = _FAIL;
		goto exit;
	}
	/*  add for CONFIG_IEEE80211W, none 11w also can use */
	spin_lock_init(&padapter->security_key_mutex);

	/*  We don't need to memset padapter->XXX to zero, because adapter is allocated by vzalloc(). */
	/* memset((unsigned char *)&padapter->securitypriv, 0, sizeof (struct security_priv)); */

	if (_rtw_init_sta_priv(&padapter->stapriv) == _FAIL) {
		DBG_871X("Can't _rtw_init_sta_priv\n");
		ret8 = _FAIL;
		goto exit;
	}

	padapter->stapriv.padapter = padapter;
	padapter->setband = GHZ24_50;
	padapter->fix_rate = 0xFF;
	rtw_init_bcmc_stainfo(padapter);

	rtw_init_pwrctrl_priv(padapter);

	rtw_hal_dm_init(padapter);

exit:

	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("-rtw_init_drv_sw\n"));

	return ret8;
}

void rtw_cancel_all_timer(struct adapter *padapter)
{
	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("+rtw_cancel_all_timer\n"));

	del_timer_sync(&padapter->mlmepriv.assoc_timer);
	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("rtw_cancel_all_timer:cancel association timer complete!\n"));

	del_timer_sync(&padapter->mlmepriv.scan_to_timer);
	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("rtw_cancel_all_timer:cancel scan_to_timer!\n"));

	del_timer_sync(&padapter->mlmepriv.dynamic_chk_timer);
	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("rtw_cancel_all_timer:cancel dynamic_chk_timer!\n"));

	del_timer_sync(&(adapter_to_pwrctl(padapter)->pwr_state_check_timer));

	del_timer_sync(&padapter->mlmepriv.set_scan_deny_timer);
	rtw_clear_scan_deny(padapter);
	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("rtw_cancel_all_timer:cancel set_scan_deny_timer!\n"));

	del_timer_sync(&padapter->recvpriv.signal_stat_timer);

	/* cancel dm timer */
	rtw_hal_dm_deinit(padapter);
}

u8 rtw_free_drv_sw(struct adapter *padapter)
{
	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("==>rtw_free_drv_sw"));

	free_mlme_ext_priv(&padapter->mlmeextpriv);

	rtw_free_cmd_priv(&padapter->cmdpriv);

	rtw_free_evt_priv(&padapter->evtpriv);

	rtw_free_mlme_priv(&padapter->mlmepriv);

	/* free_io_queue(padapter); */

	_rtw_free_xmit_priv(&padapter->xmitpriv);

	_rtw_free_sta_priv(&padapter->stapriv); /* will free bcmc_stainfo here */

	_rtw_free_recv_priv(&padapter->recvpriv);

	rtw_free_pwrctrl_priv(padapter);

	/* kfree((void *)padapter); */

	rtw_hal_free_data(padapter);

	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("<==rtw_free_drv_sw\n"));

	/* free the old_pnetdev */
	if (padapter->rereg_nd_name_priv.old_pnetdev) {
		free_netdev(padapter->rereg_nd_name_priv.old_pnetdev);
		padapter->rereg_nd_name_priv.old_pnetdev = NULL;
	}

	/*  clear pbuddystruct adapter to avoid access wrong pointer. */
	if (padapter->pbuddy_adapter != NULL)
		padapter->pbuddy_adapter->pbuddy_adapter = NULL;

	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("-rtw_free_drv_sw\n"));

	return _SUCCESS;
}

static int _rtw_drv_register_netdev(struct adapter *padapter, char *name)
{
	int ret = _SUCCESS;
	struct net_device *pnetdev = padapter->pnetdev;

	/* alloc netdev name */
	if (rtw_init_netdev_name(pnetdev, name))
		return _FAIL;

	memcpy(pnetdev->dev_addr, padapter->eeprompriv.mac_addr, ETH_ALEN);

	/* Tell the network stack we exist */
	if (register_netdev(pnetdev) != 0) {
		DBG_871X(FUNC_NDEV_FMT "Failed!\n", FUNC_NDEV_ARG(pnetdev));
		ret = _FAIL;
		goto error_register_netdev;
	}

	DBG_871X("%s, MAC Address (if%d) = " MAC_FMT "\n", __func__, (padapter->iface_id + 1), MAC_ARG(pnetdev->dev_addr));

	return ret;

error_register_netdev:

	rtw_free_drv_sw(padapter);

	rtw_free_netdev(pnetdev);

	return ret;
}

int rtw_drv_register_netdev(struct adapter *if1)
{
	struct dvobj_priv *dvobj = if1->dvobj;
	struct adapter *padapter = dvobj->padapters;
	char *name = if1->registrypriv.ifname;

	return _rtw_drv_register_netdev(padapter, name);
}

int _netdev_open(struct net_device *pnetdev)
{
	uint status;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(pnetdev);
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("+871x_drv - dev_open\n"));
	DBG_871X("+871x_drv - drv_open, bup =%d\n", padapter->bup);

	padapter->netif_up = true;

	if (pwrctrlpriv->ps_flag) {
		padapter->net_closed = false;
		goto netdev_open_normal_process;
	}

	if (!padapter->bup) {
		padapter->bDriverStopped = false;
		padapter->bSurpriseRemoved = false;
		padapter->bCardDisableWOHSM = false;

		status = rtw_hal_init(padapter);
		if (status == _FAIL) {
			RT_TRACE(_module_os_intfs_c_, _drv_err_, ("rtl871x_hal_init(): Can't init h/w!\n"));
			goto netdev_open_error;
		}

		DBG_871X("MAC Address = " MAC_FMT "\n", MAC_ARG(pnetdev->dev_addr));

		status = rtw_start_drv_threads(padapter);
		if (status == _FAIL) {
			DBG_871X("Initialize driver software resource Failed!\n");
			goto netdev_open_error;
		}

		if (padapter->intf_start)
			padapter->intf_start(padapter);

		rtw_cfg80211_init_wiphy(padapter);

		padapter->bup = true;
		pwrctrlpriv->bips_processing = false;
	}
	padapter->net_closed = false;

	_set_timer(&padapter->mlmepriv.dynamic_chk_timer, 2000);

	if (!rtw_netif_queue_stopped(pnetdev))
		rtw_netif_start_queue(pnetdev);
	else
		rtw_netif_wake_queue(pnetdev);

netdev_open_normal_process:

	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("-871x_drv - dev_open\n"));
	DBG_871X("-871x_drv - drv_open, bup =%d\n", padapter->bup);

	return 0;

netdev_open_error:

	padapter->bup = false;

	netif_carrier_off(pnetdev);
	rtw_netif_stop_queue(pnetdev);

	RT_TRACE(_module_os_intfs_c_, _drv_err_, ("-871x_drv - dev_open, fail!\n"));
	DBG_871X("-871x_drv - drv_open fail, bup =%d\n", padapter->bup);

	return (-1);
}

int netdev_open(struct net_device *pnetdev)
{
	int ret;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(pnetdev);
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	if (pwrctrlpriv->bInSuspend) {
		DBG_871X("+871x_drv - drv_open, bInSuspend =%d\n", pwrctrlpriv->bInSuspend);
		return 0;
	}

	if (mutex_lock_interruptible(&(adapter_to_dvobj(padapter)->hw_init_mutex)))
		return -1;

	ret = _netdev_open(pnetdev);
	mutex_unlock(&(adapter_to_dvobj(padapter)->hw_init_mutex));

	return ret;
}

static int  ips_netdrv_open(struct adapter *padapter)
{
	int status = _SUCCESS;
	/* struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter); */

	padapter->net_closed = false;

	DBG_871X("===> %s.........\n", __func__);


	padapter->bDriverStopped = false;
	padapter->bCardDisableWOHSM = false;
	/* padapter->bup = true; */

	status = rtw_hal_init(padapter);
	if (status == _FAIL) {
		RT_TRACE(_module_os_intfs_c_, _drv_err_, ("ips_netdrv_open(): Can't init h/w!\n"));
		goto netdev_open_error;
	}

	if (padapter->intf_start)
		padapter->intf_start(padapter);

	_set_timer(&padapter->mlmepriv.dynamic_chk_timer, 2000);

	return _SUCCESS;

netdev_open_error:
	/* padapter->bup = false; */
	DBG_871X("-ips_netdrv_open - drv_open failure, bup =%d\n", padapter->bup);

	return _FAIL;
}


int rtw_ips_pwr_up(struct adapter *padapter)
{
	int result;
	DBG_871X("===>  rtw_ips_pwr_up..............\n");

	result = ips_netdrv_open(padapter);

	DBG_871X("<===  rtw_ips_pwr_up..............\n");
	return result;
}

void rtw_ips_pwr_down(struct adapter *padapter)
{
	DBG_871X("===> rtw_ips_pwr_down...................\n");

	padapter->bCardDisableWOHSM = true;
	padapter->net_closed = true;

	rtw_ips_dev_unload(padapter);
	padapter->bCardDisableWOHSM = false;
	DBG_871X("<=== rtw_ips_pwr_down.....................\n");
}

void rtw_ips_dev_unload(struct adapter *padapter)
{
	DBG_871X("====> %s...\n", __func__);


	if (!padapter->bSurpriseRemoved)
		rtw_hal_deinit(padapter);
}


static int pm_netdev_open(struct net_device *pnetdev, u8 bnormal)
{
	int status = -1;

	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(pnetdev);

	if (bnormal) {
		if (mutex_lock_interruptible(&(adapter_to_dvobj(padapter)->hw_init_mutex)) == 0) {
			status = _netdev_open(pnetdev);
			mutex_unlock(&(adapter_to_dvobj(padapter)->hw_init_mutex));
		}
	} else {
		status =  (_SUCCESS == ips_netdrv_open(padapter)) ? (0) : (-1);
	}

	return status;
}

static int netdev_close(struct net_device *pnetdev)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(pnetdev);
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);

	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("+871x_drv - drv_close\n"));

	if (pwrctl->bInternalAutoSuspend) {
		/* rtw_pwr_wakeup(padapter); */
		if (pwrctl->rf_pwrstate == rf_off)
			pwrctl->ps_flag = true;
	}
	padapter->net_closed = true;
	padapter->netif_up = false;

/*if (!padapter->hw_init_completed)
	{
		DBG_871X("(1)871x_drv - drv_close, bup =%d, hw_init_completed =%d\n", padapter->bup, padapter->hw_init_completed);

		padapter->bDriverStopped = true;

		rtw_dev_unload(padapter);
	}
	else*/
	if (pwrctl->rf_pwrstate == rf_on) {
		DBG_871X("(2)871x_drv - drv_close, bup =%d, hw_init_completed =%d\n", padapter->bup, padapter->hw_init_completed);

		/* s1. */
		if (pnetdev) {
			if (!rtw_netif_queue_stopped(pnetdev))
				rtw_netif_stop_queue(pnetdev);
		}

		/* s2. */
		LeaveAllPowerSaveMode(padapter);
		rtw_disassoc_cmd(padapter, 500, false);
		/* s2-2.  indicate disconnect to os */
		rtw_indicate_disconnect(padapter);
		/* s2-3. */
		rtw_free_assoc_resources(padapter, 1);
		/* s2-4. */
		rtw_free_network_queue(padapter, true);
	}

	rtw_scan_abort(padapter);
	adapter_wdev_data(padapter)->bandroid_scan = false;

	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("-871x_drv - drv_close\n"));
	DBG_871X("-871x_drv - drv_close, bup =%d\n", padapter->bup);

	return 0;
}

void rtw_ndev_destructor(struct net_device *ndev)
{
	DBG_871X(FUNC_NDEV_FMT "\n", FUNC_NDEV_ARG(ndev));

	kfree(ndev->ieee80211_ptr);
}

void rtw_dev_unload(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	struct dvobj_priv *pobjpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &pobjpriv->drv_dbg;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 cnt = 0;

	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("+%s\n", __func__));

	if (padapter->bup) {
		DBG_871X("===> %s\n", __func__);

		padapter->bDriverStopped = true;
		if (padapter->xmitpriv.ack_tx)
			rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_DRV_STOP);

		if (padapter->intf_stop)
			padapter->intf_stop(padapter);

		RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("@ rtw_dev_unload: stop intf complete!\n"));

		if (!pwrctl->bInternalAutoSuspend)
			rtw_stop_drv_threads(padapter);

		while (atomic_read(&pcmdpriv->cmdthd_running)) {
			if (cnt > 5) {
				DBG_871X("stop cmdthd timeout\n");
				break;
			} else {
				cnt++;
				DBG_871X("cmdthd is running(%d)\n", cnt);
				msleep(10);
			}
		}

		RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("@ %s: stop thread complete!\n", __func__));

		/* check the status of IPS */
		if (rtw_hal_check_ips_status(padapter) || pwrctl->rf_pwrstate == rf_off) {
			/* check HW status and SW state */
			DBG_871X_LEVEL(_drv_always_, "%s: driver in IPS-FWLPS\n", __func__);
			pdbgpriv->dbg_dev_unload_inIPS_cnt++;
			LeaveAllPowerSaveMode(padapter);
		} else {
			DBG_871X_LEVEL(_drv_always_, "%s: driver not in IPS\n", __func__);
		}

		if (!padapter->bSurpriseRemoved) {
			hal_btcoex_IpsNotify(padapter, pwrctl->ips_mode_req);
#ifdef CONFIG_WOWLAN
			if (pwrctl->bSupportRemoteWakeup && pwrctl->wowlan_mode) {
				DBG_871X_LEVEL(_drv_always_, "%s bSupportRemoteWakeup ==true  do not run rtw_hal_deinit()\n", __func__);
			}
			else
#endif
			{
				/* amy modify 20120221 for power seq is different between driver open and ips */
				rtw_hal_deinit(padapter);
			}
			padapter->bSurpriseRemoved = true;
		}
		RT_TRACE(_module_hci_intfs_c_, _drv_notice_,
			 ("@ %s: deinit hal complete!\n", __func__));

		padapter->bup = false;

		DBG_871X("<=== %s\n", __func__);
	} else {
		RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("%s: bup ==false\n", __func__));
		DBG_871X("%s: bup ==false\n", __func__);
	}

	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("-%s\n", __func__));
}

static int rtw_suspend_free_assoc_resource(struct adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	DBG_871X("==> " FUNC_ADPT_FMT " entry....\n", FUNC_ADPT_ARG(padapter));

	if (rtw_chk_roam_flags(padapter, RTW_ROAM_ON_RESUME)) {
		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)
			&& check_fwstate(pmlmepriv, _FW_LINKED)) {
			DBG_871X("%s %s(" MAC_FMT "), length:%d assoc_ssid.length:%d\n", __func__,
					pmlmepriv->cur_network.network.Ssid.Ssid,
					MAC_ARG(pmlmepriv->cur_network.network.MacAddress),
					pmlmepriv->cur_network.network.Ssid.SsidLength,
					pmlmepriv->assoc_ssid.SsidLength);
			rtw_set_to_roam(padapter, 1);
		}
	}

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) && check_fwstate(pmlmepriv, _FW_LINKED)) {
		rtw_disassoc_cmd(padapter, 0, false);
		/* s2-2.  indicate disconnect to os */
		rtw_indicate_disconnect(padapter);
	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		rtw_sta_flush(padapter);
	}

	/* s2-3. */
	rtw_free_assoc_resources(padapter, 1);

	/* s2-4. */
	rtw_free_network_queue(padapter, true);

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY))
		rtw_indicate_scan_done(padapter, 1);

	if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) {
		DBG_871X_LEVEL(_drv_always_, "%s: fw_under_linking\n", __func__);
		rtw_indicate_disconnect(padapter);
	}

	DBG_871X("<== " FUNC_ADPT_FMT " exit....\n", FUNC_ADPT_ARG(padapter));
	return _SUCCESS;
}

#ifdef CONFIG_WOWLAN
void rtw_suspend_wow(struct adapter *padapter)
{
	u8 ch, bw, offset;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct net_device *pnetdev = padapter->pnetdev;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct wowlan_ioctl_param poidparam;

	DBG_871X("==> " FUNC_ADPT_FMT " entry....\n", FUNC_ADPT_ARG(padapter));


	DBG_871X("wowlan_mode: %d\n", pwrpriv->wowlan_mode);
	DBG_871X("wowlan_pno_enable: %d\n", pwrpriv->wowlan_pno_enable);

	if (pwrpriv->wowlan_mode) {
		if (pnetdev)
			rtw_netif_stop_queue(pnetdev);
		/*  1. stop thread */
		padapter->bDriverStopped = true;	/* for stop thread */
		rtw_stop_drv_threads(padapter);
		padapter->bDriverStopped = false;	/* for 32k command */

		/*  2. disable interrupt */
		if (padapter->intf_stop)
			padapter->intf_stop(padapter);

		/*  2.1 clean interrupt */
		if (padapter->HalFunc.clear_interrupt)
			padapter->HalFunc.clear_interrupt(padapter);

		/*  2.2 free irq */
		/* sdio_free_irq(adapter_to_dvobj(padapter)); */
		if (padapter->intf_free_irq)
			padapter->intf_free_irq(adapter_to_dvobj(padapter));

		poidparam.subcode = WOWLAN_ENABLE;
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_WOWLAN, (u8 *)&poidparam);
		if (rtw_chk_roam_flags(padapter, RTW_ROAM_ON_RESUME)) {
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) && check_fwstate(pmlmepriv, _FW_LINKED)) {
				DBG_871X("%s %s(" MAC_FMT "), length:%d assoc_ssid.length:%d\n", __func__,
						pmlmepriv->cur_network.network.Ssid.Ssid,
						MAC_ARG(pmlmepriv->cur_network.network.MacAddress),
						pmlmepriv->cur_network.network.Ssid.SsidLength,
						pmlmepriv->assoc_ssid.SsidLength);

				rtw_set_to_roam(padapter, 0);
			}
		}

		DBG_871X_LEVEL(_drv_always_, "%s: wowmode suspending\n", __func__);

		if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY)) {
			DBG_871X_LEVEL(_drv_always_, "%s: fw_under_survey\n", __func__);
			rtw_indicate_scan_done(padapter, 1);
			clr_fwstate(pmlmepriv, _FW_UNDER_SURVEY);
		}

		if (rtw_get_ch_setting_union(padapter, &ch, &bw, &offset) != 0) {
			DBG_871X(FUNC_ADPT_FMT " back to linked/linking union - ch:%u, bw:%u, offset:%u\n",
				FUNC_ADPT_ARG(padapter), ch, bw, offset);
			set_channel_bwmode(padapter, ch, offset, bw);
		}

		if (pwrpriv->wowlan_pno_enable)
			DBG_871X_LEVEL(_drv_always_, "%s: pno: %d\n", __func__, pwrpriv->wowlan_pno_enable);
		else
			rtw_set_ps_mode(padapter, PS_MODE_DTIM, 0, 0, "WOWLAN");
	} else {
		DBG_871X_LEVEL(_drv_always_, "%s: ### ERROR ### wowlan_mode =%d\n", __func__, pwrpriv->wowlan_mode);
	}
	DBG_871X("<== " FUNC_ADPT_FMT " exit....\n", FUNC_ADPT_ARG(padapter));
}
#endif /* ifdef CONFIG_WOWLAN */

#ifdef CONFIG_AP_WOWLAN
void rtw_suspend_ap_wow(struct adapter *padapter)
{
	u8 ch, bw, offset;
	struct net_device *pnetdev = padapter->pnetdev;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct wowlan_ioctl_param poidparam;

	DBG_871X("==> " FUNC_ADPT_FMT " entry....\n", FUNC_ADPT_ARG(padapter));

	pwrpriv->wowlan_ap_mode = true;

	DBG_871X("wowlan_ap_mode: %d\n", pwrpriv->wowlan_ap_mode);

	if (pnetdev)
		rtw_netif_stop_queue(pnetdev);
	/*  1. stop thread */
	padapter->bDriverStopped = true;	/* for stop thread */
	rtw_stop_drv_threads(padapter);
	padapter->bDriverStopped = false;	/* for 32k command */

	/*  2. disable interrupt */
	rtw_hal_disable_interrupt(padapter); /*  It need wait for leaving 32K. */

	/*  2.1 clean interrupt */
	if (padapter->HalFunc.clear_interrupt)
		padapter->HalFunc.clear_interrupt(padapter);

	/*  2.2 free irq */
	/* sdio_free_irq(adapter_to_dvobj(padapter)); */
	if (padapter->intf_free_irq)
		padapter->intf_free_irq(adapter_to_dvobj(padapter));

	poidparam.subcode = WOWLAN_AP_ENABLE;
	padapter->HalFunc.SetHwRegHandler(padapter,
					HW_VAR_AP_WOWLAN, (u8 *)&poidparam);

	DBG_871X_LEVEL(_drv_always_, "%s: wowmode suspending\n", __func__);

	if (rtw_get_ch_setting_union(padapter, &ch, &bw, &offset) != 0) {
		DBG_871X(FUNC_ADPT_FMT " back to linked/linking union - ch:%u, bw:%u, offset:%u\n",
			 FUNC_ADPT_ARG(padapter), ch, bw, offset);
		set_channel_bwmode(padapter, ch, offset, bw);
	}

	rtw_set_ps_mode(padapter, PS_MODE_MIN, 0, 0, "AP-WOWLAN");

	DBG_871X("<== " FUNC_ADPT_FMT " exit....\n", FUNC_ADPT_ARG(padapter));
}
#endif /* ifdef CONFIG_AP_WOWLAN */


static void rtw_suspend_normal(struct adapter *padapter)
{
	struct net_device *pnetdev = padapter->pnetdev;

	DBG_871X("==> " FUNC_ADPT_FMT " entry....\n", FUNC_ADPT_ARG(padapter));
	if (pnetdev) {
		netif_carrier_off(pnetdev);
		rtw_netif_stop_queue(pnetdev);
	}

	rtw_suspend_free_assoc_resource(padapter);

	if ((rtw_hal_check_ips_status(padapter)) || (adapter_to_pwrctl(padapter)->rf_pwrstate == rf_off))
		DBG_871X_LEVEL(_drv_always_, "%s: ### ERROR #### driver in IPS ####ERROR###!!!\n", __func__);

	rtw_dev_unload(padapter);

	/* sdio_deinit(adapter_to_dvobj(padapter)); */
	if (padapter->intf_deinit)
		padapter->intf_deinit(adapter_to_dvobj(padapter));

	DBG_871X("<== " FUNC_ADPT_FMT " exit....\n", FUNC_ADPT_ARG(padapter));
}

int rtw_suspend_common(struct adapter *padapter)
{
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(psdpriv);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	int ret = 0;
	unsigned long start_time = jiffies;

	DBG_871X_LEVEL(_drv_always_, " suspend start\n");
	DBG_871X("==> %s (%s:%d)\n", __func__, current->comm, current->pid);
	pdbgpriv->dbg_suspend_cnt++;

	pwrpriv->bInSuspend = true;

	while (pwrpriv->bips_processing)
		msleep(1);

	if ((!padapter->bup) || (padapter->bDriverStopped) || (padapter->bSurpriseRemoved)) {
		DBG_871X("%s bup =%d bDriverStopped =%d bSurpriseRemoved = %d\n", __func__
			, padapter->bup, padapter->bDriverStopped, padapter->bSurpriseRemoved);
		pdbgpriv->dbg_suspend_error_cnt++;
		goto exit;
	}
	rtw_ps_deny(padapter, PS_DENY_SUSPEND);

	rtw_cancel_all_timer(padapter);

	LeaveAllPowerSaveModeDirect(padapter);

	rtw_stop_cmd_thread(padapter);

	/*  wait for the latest FW to remove this condition. */
	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		hal_btcoex_SuspendNotify(padapter, 0);
		DBG_871X("WIFI_AP_STATE\n");
	} else if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		hal_btcoex_SuspendNotify(padapter, 1);
		DBG_871X("STATION\n");
	}

	rtw_ps_deny_cancel(padapter, PS_DENY_SUSPEND);

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
	#ifdef CONFIG_WOWLAN
		if (check_fwstate(pmlmepriv, _FW_LINKED))
			pwrpriv->wowlan_mode = true;
		else if (pwrpriv->wowlan_pno_enable)
			pwrpriv->wowlan_mode |= pwrpriv->wowlan_pno_enable;

		if (pwrpriv->wowlan_mode)
			rtw_suspend_wow(padapter);
		else
			rtw_suspend_normal(padapter);

	#else /* CONFIG_WOWLAN */
		rtw_suspend_normal(padapter);
	#endif /* CONFIG_WOWLAN */
	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
	#ifdef CONFIG_AP_WOWLAN
		rtw_suspend_ap_wow(padapter);
	#else
		rtw_suspend_normal(padapter);
	#endif /* CONFIG_AP_WOWLAN */
	} else {
		rtw_suspend_normal(padapter);
	}

	DBG_871X_LEVEL(_drv_always_, "rtw suspend success in %d ms\n",
		jiffies_to_msecs(jiffies - start_time));

exit:
	DBG_871X("<===  %s return %d.............. in %dms\n", __func__
		, ret, jiffies_to_msecs(jiffies - start_time));

	return ret;
}

#ifdef CONFIG_WOWLAN
int rtw_resume_process_wow(struct adapter *padapter)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct net_device *pnetdev = padapter->pnetdev;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	struct wowlan_ioctl_param poidparam;
	struct sta_info *psta = NULL;
	int ret = _SUCCESS;

	DBG_871X("==> " FUNC_ADPT_FMT " entry....\n", FUNC_ADPT_ARG(padapter));

	if (padapter) {
		pnetdev = padapter->pnetdev;
		pwrpriv = adapter_to_pwrctl(padapter);
	} else {
		pdbgpriv->dbg_resume_error_cnt++;
		ret = -1;
		goto exit;
	}

	if (padapter->bDriverStopped || padapter->bSurpriseRemoved) {
		DBG_871X("%s pdapter %p bDriverStopped %d bSurpriseRemoved %d\n",
				__func__, padapter, padapter->bDriverStopped,
				padapter->bSurpriseRemoved);
		goto exit;
	}

#ifdef CONFIG_PNO_SUPPORT
	pwrpriv->pno_in_resume = true;
#endif

	if (pwrpriv->wowlan_mode) {
		rtw_set_ps_mode(padapter, PS_MODE_ACTIVE, 0, 0, "WOWLAN");

		pwrpriv->bFwCurrentInPSMode = false;

		if (padapter->intf_stop)
			padapter->intf_stop(padapter);

		if (padapter->HalFunc.clear_interrupt)
			padapter->HalFunc.clear_interrupt(padapter);

		/* if (sdio_alloc_irq(adapter_to_dvobj(padapter)) != _SUCCESS) { */
		if ((padapter->intf_alloc_irq) && (padapter->intf_alloc_irq(adapter_to_dvobj(padapter)) != _SUCCESS)) {
			ret = -1;
			RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("%s: sdio_alloc_irq Failed!!\n", __func__));
			goto exit;
		}

		/* Disable WOW, set H2C command */
		poidparam.subcode = WOWLAN_DISABLE;
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_WOWLAN, (u8 *)&poidparam);

		psta = rtw_get_stainfo(&padapter->stapriv, get_bssid(&padapter->mlmepriv));
		if (psta)
			set_sta_rate(padapter, psta);

		padapter->bDriverStopped = false;
		DBG_871X("%s: wowmode resuming, DriverStopped:%d\n", __func__, padapter->bDriverStopped);
		rtw_start_drv_threads(padapter);

		if (padapter->intf_start)
			padapter->intf_start(padapter);

		/*  start netif queue */
		if (pnetdev) {
			if (!rtw_netif_queue_stopped(pnetdev))
				rtw_netif_start_queue(pnetdev);
			else
				rtw_netif_wake_queue(pnetdev);
		}
	} else {
		DBG_871X_LEVEL(_drv_always_, "%s: ### ERROR ### wowlan_mode =%d\n", __func__, pwrpriv->wowlan_mode);
	}

	if (padapter->pid[1] != 0) {
		DBG_871X("pid[1]:%d\n", padapter->pid[1]);
		rtw_signal_process(padapter->pid[1], SIGUSR2);
	}

	if (rtw_chk_roam_flags(padapter, RTW_ROAM_ON_RESUME)) {
		if (pwrpriv->wowlan_wake_reason == FWDecisionDisconnect ||
			pwrpriv->wowlan_wake_reason == Rx_DisAssoc ||
			pwrpriv->wowlan_wake_reason == Rx_DeAuth) {
			DBG_871X("%s: disconnect reason: %02x\n", __func__,
						pwrpriv->wowlan_wake_reason);
			rtw_indicate_disconnect(padapter);

			rtw_sta_media_status_rpt(padapter,
				rtw_get_stainfo(&padapter->stapriv,
					get_bssid(&padapter->mlmepriv)), 0);

			rtw_free_assoc_resources(padapter, 1);
			pmlmeinfo->state = WIFI_FW_NULL_STATE;

		} else {
			DBG_871X("%s: do roaming\n", __func__);
			rtw_roaming(padapter, NULL);
		}
	}

	if (pwrpriv->wowlan_mode) {
		pwrpriv->bips_processing = false;
		_set_timer(&padapter->mlmepriv.dynamic_chk_timer, 2000);
	} else {
		DBG_871X_LEVEL(_drv_always_, "do not reset timer\n");
	}

	pwrpriv->wowlan_mode = false;

	/* clean driver side wake up reason. */
	pwrpriv->wowlan_wake_reason = 0;
exit:
	DBG_871X("<== " FUNC_ADPT_FMT " exit....\n", FUNC_ADPT_ARG(padapter));
	return ret;
}
#endif /* ifdef CONFIG_WOWLAN */

#ifdef CONFIG_AP_WOWLAN
int rtw_resume_process_ap_wow(struct adapter *padapter)
{
	struct net_device *pnetdev = padapter->pnetdev;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	struct wowlan_ioctl_param poidparam;
	int ret = _SUCCESS;

	DBG_871X("==> " FUNC_ADPT_FMT " entry....\n", FUNC_ADPT_ARG(padapter));

	if (padapter) {
		pnetdev = padapter->pnetdev;
		pwrpriv = adapter_to_pwrctl(padapter);
	} else {
		pdbgpriv->dbg_resume_error_cnt++;
		ret = -1;
		goto exit;
	}

	rtw_set_ps_mode(padapter, PS_MODE_ACTIVE, 0, 0, "AP-WOWLAN");

	pwrpriv->bFwCurrentInPSMode = false;

	rtw_hal_disable_interrupt(padapter);

	if (padapter->HalFunc.clear_interrupt)
		padapter->HalFunc.clear_interrupt(padapter);

	/* if (sdio_alloc_irq(adapter_to_dvobj(padapter)) != _SUCCESS) { */
	if ((padapter->intf_alloc_irq) && (padapter->intf_alloc_irq(adapter_to_dvobj(padapter)) != _SUCCESS)) {
		ret = -1;
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("%s: sdio_alloc_irq Failed!!\n", __func__));
		goto exit;
	}

	/* Disable WOW, set H2C command */
	poidparam.subcode = WOWLAN_AP_DISABLE;
	padapter->HalFunc.SetHwRegHandler(padapter,
		HW_VAR_AP_WOWLAN, (u8 *)&poidparam);
	pwrpriv->wowlan_ap_mode = false;

	padapter->bDriverStopped = false;
	DBG_871X("%s: wowmode resuming, DriverStopped:%d\n", __func__, padapter->bDriverStopped);
	rtw_start_drv_threads(padapter);

	if (padapter->intf_start)
		padapter->intf_start(padapter);

	/*  start netif queue */
	if (pnetdev) {
		if (!rtw_netif_queue_stopped(pnetdev))
			rtw_netif_start_queue(pnetdev);
		else
			rtw_netif_wake_queue(pnetdev);
	}

	if (padapter->pid[1] != 0) {
		DBG_871X("pid[1]:%d\n", padapter->pid[1]);
		rtw_signal_process(padapter->pid[1], SIGUSR2);
	}

	pwrpriv->bips_processing = false;
	_set_timer(&padapter->mlmepriv.dynamic_chk_timer, 2000);

	/* clean driver side wake up reason. */
	pwrpriv->wowlan_wake_reason = 0;
exit:
	DBG_871X("<== " FUNC_ADPT_FMT " exit....\n", FUNC_ADPT_ARG(padapter));
	return ret;
}
#endif /* ifdef CONFIG_APWOWLAN */

static int rtw_resume_process_normal(struct adapter *padapter)
{
	struct net_device *pnetdev;
	struct pwrctrl_priv *pwrpriv;
	struct mlme_priv *pmlmepriv;
	struct dvobj_priv *psdpriv;
	struct debug_priv *pdbgpriv;

	int ret = _SUCCESS;

	if (!padapter) {
		ret = -1;
		goto exit;
	}

	pnetdev = padapter->pnetdev;
	pwrpriv = adapter_to_pwrctl(padapter);
	pmlmepriv = &padapter->mlmepriv;
	psdpriv = padapter->dvobj;
	pdbgpriv = &psdpriv->drv_dbg;

	DBG_871X("==> " FUNC_ADPT_FMT " entry....\n", FUNC_ADPT_ARG(padapter));
	/*  interface init */
	/* if (sdio_init(adapter_to_dvobj(padapter)) != _SUCCESS) */
	if ((padapter->intf_init) && (padapter->intf_init(adapter_to_dvobj(padapter)) != _SUCCESS)) {
		ret = -1;
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("%s: initialize SDIO Failed!!\n", __func__));
		goto exit;
	}
	rtw_hal_disable_interrupt(padapter);
	/* if (sdio_alloc_irq(adapter_to_dvobj(padapter)) != _SUCCESS) */
	if ((padapter->intf_alloc_irq) && (padapter->intf_alloc_irq(adapter_to_dvobj(padapter)) != _SUCCESS)) {
		ret = -1;
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("%s: sdio_alloc_irq Failed!!\n", __func__));
		goto exit;
	}

	rtw_reset_drv_sw(padapter);
	pwrpriv->bkeepfwalive = false;

	DBG_871X("bkeepfwalive(%x)\n", pwrpriv->bkeepfwalive);
	if (pm_netdev_open(pnetdev, true) != 0) {
		ret = -1;
		pdbgpriv->dbg_resume_error_cnt++;
		goto exit;
	}

	netif_device_attach(pnetdev);
	netif_carrier_on(pnetdev);

	if (padapter->pid[1] != 0) {
		DBG_871X("pid[1]:%d\n", padapter->pid[1]);
		rtw_signal_process(padapter->pid[1], SIGUSR2);
	}

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		DBG_871X(FUNC_ADPT_FMT " fwstate:0x%08x - WIFI_STATION_STATE\n", FUNC_ADPT_ARG(padapter), get_fwstate(pmlmepriv));

		if (rtw_chk_roam_flags(padapter, RTW_ROAM_ON_RESUME))
			rtw_roaming(padapter, NULL);

	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		DBG_871X(FUNC_ADPT_FMT " fwstate:0x%08x - WIFI_AP_STATE\n", FUNC_ADPT_ARG(padapter), get_fwstate(pmlmepriv));
		rtw_ap_restore_network(padapter);
	} else if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {
		DBG_871X(FUNC_ADPT_FMT " fwstate:0x%08x - WIFI_ADHOC_STATE\n", FUNC_ADPT_ARG(padapter), get_fwstate(pmlmepriv));
	} else {
		DBG_871X(FUNC_ADPT_FMT " fwstate:0x%08x - ???\n", FUNC_ADPT_ARG(padapter), get_fwstate(pmlmepriv));
	}

	DBG_871X("<== " FUNC_ADPT_FMT " exit....\n", FUNC_ADPT_ARG(padapter));

exit:
	return ret;
}

int rtw_resume_common(struct adapter *padapter)
{
	int ret = 0;
	unsigned long start_time = jiffies;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	DBG_871X_LEVEL(_drv_always_, "resume start\n");
	DBG_871X("==> %s (%s:%d)\n", __func__, current->comm, current->pid);

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
	#ifdef CONFIG_WOWLAN
		if (pwrpriv->wowlan_mode)
			rtw_resume_process_wow(padapter);
		else
			rtw_resume_process_normal(padapter);
	#else
		rtw_resume_process_normal(padapter);
	#endif

	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
	#ifdef CONFIG_AP_WOWLAN
		rtw_resume_process_ap_wow(padapter);
	#else
		rtw_resume_process_normal(padapter);
	#endif /* CONFIG_AP_WOWLAN */
	} else {
		rtw_resume_process_normal(padapter);
	}

	hal_btcoex_SuspendNotify(padapter, 0);

	if (pwrpriv) {
		pwrpriv->bInSuspend = false;
	#ifdef CONFIG_PNO_SUPPORT
		pwrpriv->pno_in_resume = false;
	#endif
	}
	DBG_871X_LEVEL(_drv_always_, "%s:%d in %d ms\n", __func__, ret,
		jiffies_to_msecs(jiffies - start_time));

	return ret;
}
