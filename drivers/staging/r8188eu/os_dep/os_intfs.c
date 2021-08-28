// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _OS_INTFS_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/xmit_osdep.h"
#include "../include/recv_osdep.h"
#include "../include/hal_intf.h"
#include "../include/rtw_ioctl.h"

#include "../include/usb_osintf.h"
#include "../include/rtw_br_ext.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek Wireless Lan Driver");
MODULE_AUTHOR("Realtek Semiconductor Corp.");
MODULE_VERSION(DRIVERVERSION);

#define CONFIG_BR_EXT_BRNAME "br0"
#define RTW_NOTCH_FILTER 0 /* 0:Disable, 1:Enable, */

/* module param defaults */
static int rtw_chip_version = 0x00;
static int rtw_rfintfs = HWPI;
static int rtw_lbkmode;/* RTL8712_AIR_TRX; */
static int rtw_network_mode = Ndis802_11IBSS;/* Ndis802_11Infrastructure; infra, ad-hoc, auto */
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

static int rtw_debug = 1;
static int rtw_radio_enable = 1;
static int rtw_long_retry_lmt = 7;
static int rtw_short_retry_lmt = 7;
static int rtw_busy_thresh = 40;
static int rtw_ack_policy = NORMAL_ACK;

static int rtw_mp_mode;

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

static int rtw_led_enable = 1;

int rtw_ht_enable = 1;
int rtw_cbw40_enable = 3; /*  0 :disable, bit(0): enable 2.4g, bit(1): enable 5g */
int rtw_ampdu_enable = 1;/* for enable tx_ampdu */
static int rtw_rx_stbc = 1;/*  0: disable, bit(0):enable 2.4g, bit(1):enable 5g, default is set to enable 2.4GHZ for IOT issue with bufflao's AP at 5GHZ */
static int rtw_ampdu_amsdu;/*  0: disabled, 1:enabled, 2:auto */

static int rtw_lowrate_two_xmit = 1;/* Use 2 path Tx to transmit MCS0~7 and legacy mode */

static int rtw_rf_config = RF_819X_MAX_TYPE;  /* auto */
static int rtw_low_power;
static int rtw_wifi_spec;
static int rtw_channel_plan = RT_CHANNEL_DOMAIN_MAX;
static int rtw_AcceptAddbaReq = true;/*  0:Reject AP's Add BA req, 1:Accept AP's Add BA req. */

static int rtw_antdiv_cfg = 2; /*  0:OFF , 1:ON, 2:decide by Efuse config */
static int rtw_antdiv_type; /* 0:decide by efuse  1: for 88EE, 1Tx and 1RxCG are diversity.(2 Ant with SPDT), 2:  for 88EE, 1Tx and 2Rx are diversity.(2 Ant, Tx and RxCG are both on aux port, RxCS is on main port), 3: for 88EE, 1Tx and 1RxCG are fixed.(1Ant, Tx and RxCG are both on aux port) */


static int rtw_hwpdn_mode = 2;/* 0:disable, 1:enable, 2: by EFUSE config */

static int rtw_hwpwrp_detect; /* HW power  ping detect 0:disable , 1:enable */

static int rtw_hw_wps_pbc = 1;

int rtw_mc2u_disable;

static int rtw_80211d;

static char *ifname = "wlan%d";
module_param(ifname, charp, 0644);
MODULE_PARM_DESC(ifname, "The default name to allocate for first interface");

static char *if2name = "wlan%d";
module_param(if2name, charp, 0644);
MODULE_PARM_DESC(if2name, "The default name to allocate for second interface");

char *rtw_initmac;  /*  temp mac address if users want to use instead of the mac address in Efuse */

module_param(rtw_initmac, charp, 0644);
module_param(rtw_channel_plan, int, 0644);
module_param(rtw_chip_version, int, 0644);
module_param(rtw_rfintfs, int, 0644);
module_param(rtw_lbkmode, int, 0644);
module_param(rtw_network_mode, int, 0644);
module_param(rtw_channel, int, 0644);
module_param(rtw_mp_mode, int, 0644);
module_param(rtw_wmm_enable, int, 0644);
module_param(rtw_vrtl_carrier_sense, int, 0644);
module_param(rtw_vcs_type, int, 0644);
module_param(rtw_busy_thresh, int, 0644);
module_param(rtw_led_enable, int, 0644);
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
module_param(rtw_antdiv_type, int, 0644);
module_param(rtw_hwpdn_mode, int, 0644);
module_param(rtw_hwpwrp_detect, int, 0644);
module_param(rtw_hw_wps_pbc, int, 0644);

static uint rtw_max_roaming_times = 2;
module_param(rtw_max_roaming_times, uint, 0644);
MODULE_PARM_DESC(rtw_max_roaming_times, "The max roaming times to try");

static int rtw_fw_iol = 1;/*  0:Disable, 1:enable, 2:by usb speed */
module_param(rtw_fw_iol, int, 0644);
MODULE_PARM_DESC(rtw_fw_iol, "FW IOL");

module_param(rtw_mc2u_disable, int, 0644);

module_param(rtw_80211d, int, 0644);
MODULE_PARM_DESC(rtw_80211d, "Enable 802.11d mechanism");

static uint rtw_notch_filter = RTW_NOTCH_FILTER;
module_param(rtw_notch_filter, uint, 0644);
MODULE_PARM_DESC(rtw_notch_filter, "0:Disable, 1:Enable, 2:Enable only for P2P");
module_param_named(debug, rtw_debug, int, 0444);
MODULE_PARM_DESC(debug, "Set debug level (1-9) (default 1)");

/* dummy routines */
void rtw_proc_remove_one(struct net_device *dev)
{
}

void rtw_proc_init_one(struct net_device *dev)
{
}

#if 0	/* TODO: Convert these to /sys */
void rtw_proc_init_one(struct net_device *dev)
{
	struct proc_dir_entry *dir_dev = NULL;
	struct proc_dir_entry *entry = NULL;
	struct adapter	*padapter = rtw_netdev_priv(dev);
	u8 rf_type;

	if (!rtw_proc) {
		memcpy(rtw_proc_name, DRV_NAME, sizeof(DRV_NAME));

		rtw_proc = create_proc_entry(rtw_proc_name, S_IFDIR, init_net.proc_net);
		if (!rtw_proc) {
			DBG_88E(KERN_ERR "Unable to create rtw_proc directory\n");
			return;
		}

		entry = create_proc_read_entry("ver_info", S_IFREG | S_IRUGO, rtw_proc, proc_get_drv_version, dev);
		if (!entry) {
			pr_info("Unable to create_proc_read_entry!\n");
			return;
		}
	}

	if (!padapter->dir_dev) {
		padapter->dir_dev = create_proc_entry(dev->name,
					  S_IFDIR | S_IRUGO | S_IXUGO,
					  rtw_proc);
		dir_dev = padapter->dir_dev;
		if (!dir_dev) {
			if (rtw_proc_cnt == 0) {
				if (rtw_proc) {
					remove_proc_entry(rtw_proc_name, init_net.proc_net);
					rtw_proc = NULL;
				}
			}

			pr_info("Unable to create dir_dev directory\n");
			return;
		}
	} else {
		return;
	}

	rtw_proc_cnt++;

	entry = create_proc_read_entry("write_reg", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_write_reg, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}
	entry->write_proc = proc_set_write_reg;

	entry = create_proc_read_entry("read_reg", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_read_reg, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}
	entry->write_proc = proc_set_read_reg;

	entry = create_proc_read_entry("fwstate", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_fwstate, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("sec_info", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_sec_info, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("mlmext_state", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_mlmext_state, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("qos_option", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_qos_option, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("ht_option", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_ht_option, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("rf_info", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_rf_info, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("ap_info", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_ap_info, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("adapter_state", S_IFREG | S_IRUGO,
				   dir_dev, proc_getstruct adapter_state, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("trx_info", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_trx_info, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("mac_reg_dump1", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_mac_reg_dump1, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("mac_reg_dump2", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_mac_reg_dump2, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("mac_reg_dump3", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_mac_reg_dump3, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("bb_reg_dump1", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_bb_reg_dump1, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("bb_reg_dump2", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_bb_reg_dump2, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("bb_reg_dump3", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_bb_reg_dump3, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("rf_reg_dump1", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_rf_reg_dump1, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("rf_reg_dump2", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_rf_reg_dump2, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}

	rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
	if ((RF_1T2R == rf_type) || (RF_1T1R == rf_type)) {
		entry = create_proc_read_entry("rf_reg_dump3", S_IFREG | S_IRUGO,
					   dir_dev, proc_get_rf_reg_dump3, dev);
		if (!entry) {
			pr_info("Unable to create_proc_read_entry!\n");
			return;
		}

		entry = create_proc_read_entry("rf_reg_dump4", S_IFREG | S_IRUGO,
					   dir_dev, proc_get_rf_reg_dump4, dev);
		if (!entry) {
			pr_info("Unable to create_proc_read_entry!\n");
			return;
		}
	}

#ifdef CONFIG_88EU_AP_MODE

	entry = create_proc_read_entry("all_sta_info", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_all_sta_info, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}
#endif

	entry = create_proc_read_entry("best_channel", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_best_channel, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("rx_signal", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_rx_signal, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}
	entry->write_proc = proc_set_rx_signal;
	entry = create_proc_read_entry("ht_enable", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_ht_enable, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}
	entry->write_proc = proc_set_ht_enable;

	entry = create_proc_read_entry("cbw40_enable", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_cbw40_enable, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}
	entry->write_proc = proc_set_cbw40_enable;

	entry = create_proc_read_entry("ampdu_enable", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_ampdu_enable, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}
	entry->write_proc = proc_set_ampdu_enable;

	entry = create_proc_read_entry("rx_stbc", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_rx_stbc, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}
	entry->write_proc = proc_set_rx_stbc;

	entry = create_proc_read_entry("path_rssi", S_IFREG | S_IRUGO,
					dir_dev, proc_get_two_path_rssi, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}
	entry = create_proc_read_entry("rssi_disp", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_rssi_disp, dev);
	if (!entry) {
		pr_info("Unable to create_proc_read_entry!\n");
		return;
	}
	entry->write_proc = proc_set_rssi_disp;
}

void rtw_proc_remove_one(struct net_device *dev)
{
	struct proc_dir_entry *dir_dev = NULL;
	struct adapter	*padapter = rtw_netdev_priv(dev);
	u8 rf_type;

	dir_dev = padapter->dir_dev;
	padapter->dir_dev = NULL;

	if (dir_dev) {
		remove_proc_entry("write_reg", dir_dev);
		remove_proc_entry("read_reg", dir_dev);
		remove_proc_entry("fwstate", dir_dev);
		remove_proc_entry("sec_info", dir_dev);
		remove_proc_entry("mlmext_state", dir_dev);
		remove_proc_entry("qos_option", dir_dev);
		remove_proc_entry("ht_option", dir_dev);
		remove_proc_entry("rf_info", dir_dev);
		remove_proc_entry("ap_info", dir_dev);
		remove_proc_entry("adapter_state", dir_dev);
		remove_proc_entry("trx_info", dir_dev);
		remove_proc_entry("mac_reg_dump1", dir_dev);
		remove_proc_entry("mac_reg_dump2", dir_dev);
		remove_proc_entry("mac_reg_dump3", dir_dev);
		remove_proc_entry("bb_reg_dump1", dir_dev);
		remove_proc_entry("bb_reg_dump2", dir_dev);
		remove_proc_entry("bb_reg_dump3", dir_dev);
		remove_proc_entry("rf_reg_dump1", dir_dev);
		remove_proc_entry("rf_reg_dump2", dir_dev);
		rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
		if ((RF_1T2R == rf_type) || (RF_1T1R == rf_type)) {
			remove_proc_entry("rf_reg_dump3", dir_dev);
			remove_proc_entry("rf_reg_dump4", dir_dev);
		}
#ifdef CONFIG_88EU_AP_MODE
		remove_proc_entry("all_sta_info", dir_dev);
#endif

		remove_proc_entry("best_channel", dir_dev);
		remove_proc_entry("rx_signal", dir_dev);
		remove_proc_entry("cbw40_enable", dir_dev);
		remove_proc_entry("ht_enable", dir_dev);
		remove_proc_entry("ampdu_enable", dir_dev);
		remove_proc_entry("rx_stbc", dir_dev);
		remove_proc_entry("path_rssi", dir_dev);
		remove_proc_entry("rssi_disp", dir_dev);
		remove_proc_entry(dev->name, rtw_proc);
		dir_dev = NULL;
	} else {
		return;
	}
	rtw_proc_cnt--;

	if (rtw_proc_cnt == 0) {
		if (rtw_proc) {
			remove_proc_entry("ver_info", rtw_proc);

			remove_proc_entry(rtw_proc_name, init_net.proc_net);
			rtw_proc = NULL;
		}
	}
}
#endif

static uint loadparam(struct adapter *padapter,  struct  net_device *pnetdev)
{
	struct registry_priv  *registry_par = &padapter->registrypriv;

	GlobalDebugLevel = rtw_debug;
	registry_par->chip_version = (u8)rtw_chip_version;
	registry_par->rfintfs = (u8)rtw_rfintfs;
	registry_par->lbkmode = (u8)rtw_lbkmode;
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
	registry_par->power_mgnt = (u8)rtw_power_mgnt;
	registry_par->ips_mode = (u8)rtw_ips_mode;
	registry_par->radio_enable = (u8)rtw_radio_enable;
	registry_par->long_retry_lmt = (u8)rtw_long_retry_lmt;
	registry_par->short_retry_lmt = (u8)rtw_short_retry_lmt;
	registry_par->busy_thresh = (u16)rtw_busy_thresh;
	registry_par->ack_policy = (u8)rtw_ack_policy;
	registry_par->mp_mode = (u8)rtw_mp_mode;
	registry_par->software_encrypt = (u8)rtw_software_encrypt;
	registry_par->software_decrypt = (u8)rtw_software_decrypt;
	registry_par->acm_method = (u8)rtw_acm_method;

	 /* UAPSD */
	registry_par->wmm_enable = (u8)rtw_wmm_enable;
	registry_par->uapsd_enable = (u8)rtw_uapsd_enable;
	registry_par->uapsd_max_sp = (u8)rtw_uapsd_max_sp;
	registry_par->uapsd_acbk_en = (u8)rtw_uapsd_acbk_en;
	registry_par->uapsd_acbe_en = (u8)rtw_uapsd_acbe_en;
	registry_par->uapsd_acvi_en = (u8)rtw_uapsd_acvi_en;
	registry_par->uapsd_acvo_en = (u8)rtw_uapsd_acvo_en;

	registry_par->led_enable = (u8)rtw_led_enable;

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
	registry_par->bAcceptAddbaReq = (u8)rtw_AcceptAddbaReq;
	registry_par->antdiv_cfg = (u8)rtw_antdiv_cfg;
	registry_par->antdiv_type = (u8)rtw_antdiv_type;
	registry_par->hwpdn_mode = (u8)rtw_hwpdn_mode;/* 0:disable, 1:enable, 2:by EFUSE config */
	registry_par->hwpwrp_detect = (u8)rtw_hwpwrp_detect;/* 0:disable, 1:enable */
	registry_par->hw_wps_pbc = (u8)rtw_hw_wps_pbc;

	registry_par->max_roaming_times = (u8)rtw_max_roaming_times;

	registry_par->fw_iol = rtw_fw_iol;

	registry_par->enable80211d = (u8)rtw_80211d;
	snprintf(registry_par->ifname, 16, "%s", ifname);
	snprintf(registry_par->if2name, 16, "%s", if2name);
	registry_par->notch_filter = (u8)rtw_notch_filter;

	return _SUCCESS;
}

static int rtw_net_set_mac_address(struct net_device *pnetdev, void *p)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(pnetdev);
	struct sockaddr *addr = p;

	if (!padapter->bup)
		memcpy(padapter->eeprompriv.mac_addr, addr->sa_data, ETH_ALEN);

	return 0;
}

static struct net_device_stats *rtw_net_get_stats(struct net_device *pnetdev)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(pnetdev);
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct recv_priv *precvpriv = &padapter->recvpriv;

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

static u16 rtw_select_queue(struct net_device *dev, struct sk_buff *skb, struct net_device *sb_dev)
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

	switch (eth_type) {
	case htons(ETH_P_IP):
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
	.ndo_open = netdev_open,
	.ndo_stop = netdev_close,
	.ndo_start_xmit = rtw_xmit_entry,
	.ndo_select_queue	= rtw_select_queue,
	.ndo_set_mac_address = rtw_net_set_mac_address,
	.ndo_get_stats = rtw_net_get_stats,
};

int rtw_init_netdev_name(struct net_device *pnetdev, const char *ifname)
{
	int err;

	err = dev_alloc_name(pnetdev, ifname);
	if (err < 0)
		return err;

	netif_carrier_off(pnetdev);
	return 0;
}

static const struct device_type wlan_type = {
	.name = "wlan",
};

struct net_device *rtw_init_netdev(struct adapter *old_padapter)
{
	struct adapter *padapter;
	struct net_device *pnetdev;

	if (old_padapter)
		pnetdev = rtw_alloc_etherdev_with_old_priv(sizeof(struct adapter), (void *)old_padapter);
	else
		pnetdev = rtw_alloc_etherdev(sizeof(struct adapter));

	if (!pnetdev)
		return NULL;

	pnetdev->dev.type = &wlan_type;
	padapter = rtw_netdev_priv(pnetdev);
	padapter->pnetdev = pnetdev;
	DBG_88E("register rtw_netdev_ops to netdev_ops\n");
	pnetdev->netdev_ops = &rtw_netdev_ops;
	pnetdev->watchdog_timeo = HZ * 3; /* 3 second timeout */
	pnetdev->wireless_handlers = (struct iw_handler_def *)&rtw_handlers_def;

	/* step 2. */
	loadparam(padapter, pnetdev);

	return pnetdev;
}

u32 rtw_start_drv_threads(struct adapter *padapter)
{
	u32 _status = _SUCCESS;

	padapter->cmdThread = kthread_run(rtw_cmd_thread, padapter, "RTW_CMD_THREAD");
	if (IS_ERR(padapter->cmdThread))
		_status = _FAIL;
	else
		_rtw_down_sema(&padapter->cmdpriv.terminate_cmdthread_sema); /* wait for cmd_thread to run */

	rtw_hal_start_thread(padapter);
	return _status;
}

void rtw_stop_drv_threads(struct adapter *padapter)
{
	/* Below is to termindate rtw_cmd_thread & event_thread... */
	up(&padapter->cmdpriv.cmd_queue_sema);
	if (padapter->cmdThread)
		_rtw_down_sema(&padapter->cmdpriv.terminate_cmdthread_sema);

	rtw_hal_stop_thread(padapter);
}

static u8 rtw_init_default_value(struct adapter *padapter)
{
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	/* xmit_priv */
	pxmitpriv->vcs_setting = pregistrypriv->vrtl_carrier_sense;
	pxmitpriv->vcs = pregistrypriv->vcs_type;
	pxmitpriv->vcs_type = pregistrypriv->vcs_type;
	pxmitpriv->frag_len = pregistrypriv->frag_thresh;

	/* mlme_priv */
	pmlmepriv->scan_interval = SCAN_INTERVAL;/*  30*2 sec = 60sec */
	pmlmepriv->scan_mode = SCAN_ACTIVE;

	/* ht_priv */
	pmlmepriv->htpriv.ampdu_enable = false;/* set to disabled */

	/* security_priv */
	psecuritypriv->binstallGrpkey = _FAIL;
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
	rtl8188eu_init_default_value(padapter);

	/* misc. */
	padapter->bReadPortCancel = false;
	padapter->bWritePortCancel = false;
	padapter->bRxRSSIDisplay = 0;
	padapter->bNotifyChannelChange = 0;
#ifdef CONFIG_88EU_P2P
	padapter->bShowGetP2PState = 1;
#endif
	return _SUCCESS;
}

u8 rtw_reset_drv_sw(struct adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;

	/* hal_priv */
	rtl8188eu_init_default_value(padapter);
	padapter->bReadPortCancel = false;
	padapter->bWritePortCancel = false;
	padapter->bRxRSSIDisplay = 0;
	pmlmepriv->scan_interval = SCAN_INTERVAL;/*  30*2 sec = 60sec */

	padapter->xmitpriv.tx_pkts = 0;
	padapter->recvpriv.rx_pkts = 0;

	pmlmepriv->LinkDetectInfo.bBusyTraffic = false;

	_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY | _FW_UNDER_LINKING);

	rtw_hal_sreset_reset_value(padapter);
	pwrctrlpriv->pwr_state_check_cnts = 0;

	/* mlmeextpriv */
	padapter->mlmeextpriv.sitesurvey_res.state = SCAN_DISABLE;

	rtw_set_signal_stat_timer(&padapter->recvpriv);

	return _SUCCESS;
}

u8 rtw_init_drv_sw(struct adapter *padapter)
{
	u8	ret8 = _SUCCESS;

	if ((rtw_init_cmd_priv(&padapter->cmdpriv)) == _FAIL) {
		ret8 = _FAIL;
		goto exit;
	}

	padapter->cmdpriv.padapter = padapter;

	if ((rtw_init_evt_priv(&padapter->evtpriv)) == _FAIL) {
		ret8 = _FAIL;
		goto exit;
	}

	if (rtw_init_mlme_priv(padapter) == _FAIL) {
		ret8 = _FAIL;
		goto exit;
	}

#ifdef CONFIG_88EU_P2P
	rtw_init_wifidirect_timers(padapter);
	init_wifidirect_info(padapter, P2P_ROLE_DISABLE);
	reset_global_wifidirect_info(padapter);
#endif /* CONFIG_88EU_P2P */

	if (init_mlme_ext_priv(padapter) == _FAIL) {
		ret8 = _FAIL;
		goto exit;
	}

	if (_rtw_init_xmit_priv(&padapter->xmitpriv, padapter) == _FAIL) {
		DBG_88E("Can't _rtw_init_xmit_priv\n");
		ret8 = _FAIL;
		goto exit;
	}

	if (_rtw_init_recv_priv(&padapter->recvpriv, padapter) == _FAIL) {
		DBG_88E("Can't _rtw_init_recv_priv\n");
		ret8 = _FAIL;
		goto exit;
	}

	if (_rtw_init_sta_priv(&padapter->stapriv) == _FAIL) {
		DBG_88E("Can't _rtw_init_sta_priv\n");
		ret8 = _FAIL;
		goto exit;
	}

	padapter->stapriv.padapter = padapter;

	rtw_init_bcmc_stainfo(padapter);

	rtw_init_pwrctrl_priv(padapter);

	if (init_mp_priv(padapter) == _FAIL)
		DBG_88E("%s: initialize MP private data Fail!\n", __func__);

	ret8 = rtw_init_default_value(padapter);

	rtw_hal_dm_init(padapter);
	rtw_hal_sw_led_init(padapter);

	rtw_hal_sreset_init(padapter);

	spin_lock_init(&padapter->br_ext_lock);

exit:
	return ret8;
}

void rtw_cancel_all_timer(struct adapter *padapter)
{
	_cancel_timer_ex(&padapter->mlmepriv.assoc_timer);

	_cancel_timer_ex(&padapter->mlmepriv.scan_to_timer);

	_cancel_timer_ex(&padapter->mlmepriv.dynamic_chk_timer);

	/*  cancel sw led timer */
	rtw_hal_sw_led_deinit(padapter);

	_cancel_timer_ex(&padapter->pwrctrlpriv.pwr_state_check_timer);

	_cancel_timer_ex(&padapter->recvpriv.signal_stat_timer);
	/* cancel dm timer */
	rtw_hal_dm_deinit(padapter);
}

u8 rtw_free_drv_sw(struct adapter *padapter)
{
	/* we can call rtw_p2p_enable here, but: */
	/*  1. rtw_p2p_enable may have IO operation */
	/*  2. rtw_p2p_enable is bundled with wext interface */
	#ifdef CONFIG_88EU_P2P
	{
		struct wifidirect_info *pwdinfo = &padapter->wdinfo;
		if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)) {
			_cancel_timer_ex(&pwdinfo->find_phase_timer);
			_cancel_timer_ex(&pwdinfo->restore_p2p_state_timer);
			_cancel_timer_ex(&pwdinfo->pre_tx_scan_timer);
			rtw_p2p_set_state(pwdinfo, P2P_STATE_NONE);
		}
	}
	#endif

	free_mlme_ext_priv(&padapter->mlmeextpriv);

	rtw_free_cmd_priv(&padapter->cmdpriv);

	rtw_free_evt_priv(&padapter->evtpriv);

	rtw_free_mlme_priv(&padapter->mlmepriv);
	_rtw_free_xmit_priv(&padapter->xmitpriv);

	_rtw_free_sta_priv(&padapter->stapriv); /* will free bcmc_stainfo here */

	_rtw_free_recv_priv(&padapter->recvpriv);

	rtw_free_pwrctrl_priv(padapter);

	rtw_hal_free_data(padapter);

	/* free the old_pnetdev */
	if (padapter->rereg_nd_name_priv.old_pnetdev) {
		free_netdev(padapter->rereg_nd_name_priv.old_pnetdev);
		padapter->rereg_nd_name_priv.old_pnetdev = NULL;
	}

	/*  clear pbuddystruct adapter to avoid access wrong pointer. */
	if (padapter->pbuddy_adapter)
		padapter->pbuddy_adapter->pbuddy_adapter = NULL;

	return _SUCCESS;
}

void netdev_br_init(struct net_device *netdev)
{
	struct adapter *adapter = (struct adapter *)rtw_netdev_priv(netdev);

	rcu_read_lock();

	if (rcu_dereference(adapter->pnetdev->rx_handler_data)) {
		struct net_device *br_netdev;
		struct net *devnet = NULL;

		devnet = dev_net(netdev);
		br_netdev = dev_get_by_name(devnet, CONFIG_BR_EXT_BRNAME);
		if (br_netdev) {
			memcpy(adapter->br_mac, br_netdev->dev_addr, ETH_ALEN);
			dev_put(br_netdev);
		} else {
			pr_info("%s()-%d: dev_get_by_name(%s) failed!",
				__func__, __LINE__, CONFIG_BR_EXT_BRNAME);
		}
	}
	adapter->ethBrExtInfo.addPPPoETag = 1;

	rcu_read_unlock();
}

int _netdev_open(struct net_device *pnetdev)
{
	uint status;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(pnetdev);
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;

	DBG_88E("+88eu_drv - drv_open, bup =%d\n", padapter->bup);

	if (pwrctrlpriv->ps_flag) {
		padapter->net_closed = false;
		goto netdev_open_normal_process;
	}

	if (!padapter->bup) {
		padapter->bDriverStopped = false;
		padapter->bSurpriseRemoved = false;
		padapter->bCardDisableWOHSM = false;

		status = rtw_hal_init(padapter);
		if (status == _FAIL)
			goto netdev_open_error;

		pr_info("MAC Address = %pM\n", pnetdev->dev_addr);

		status = rtw_start_drv_threads(padapter);
		if (status == _FAIL) {
			pr_info("Initialize driver software resource Failed!\n");
			goto netdev_open_error;
		}

		if (init_hw_mlme_ext(padapter) == _FAIL) {
			pr_info("can't init mlme_ext_priv\n");
			goto netdev_open_error;
		}
		if (padapter->intf_start)
			padapter->intf_start(padapter);
		rtw_proc_init_one(pnetdev);

		rtw_led_control(padapter, LED_CTL_NO_LINK);

		padapter->bup = true;
	}
	padapter->net_closed = false;

	_set_timer(&padapter->mlmepriv.dynamic_chk_timer, 2000);

	padapter->pwrctrlpriv.bips_processing = false;
	rtw_set_pwr_state_check_timer(&padapter->pwrctrlpriv);

	if (!rtw_netif_queue_stopped(pnetdev))
		rtw_netif_start_queue(pnetdev);
	else
		rtw_netif_wake_queue(pnetdev);

	netdev_br_init(pnetdev);

netdev_open_normal_process:
	DBG_88E("-88eu_drv - drv_open, bup =%d\n", padapter->bup);
	return 0;

netdev_open_error:
	padapter->bup = false;
	netif_carrier_off(pnetdev);
	rtw_netif_stop_queue(pnetdev);
	DBG_88E("-88eu_drv - drv_open fail, bup =%d\n", padapter->bup);
	return -1;
}

int netdev_open(struct net_device *pnetdev)
{
	int ret;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(pnetdev);

	mutex_lock(padapter->hw_init_mutex);
	ret = _netdev_open(pnetdev);
	mutex_unlock(padapter->hw_init_mutex);
	return ret;
}

static int  ips_netdrv_open(struct adapter *padapter)
{
	int status = _SUCCESS;
	padapter->net_closed = false;
	DBG_88E("===> %s.........\n", __func__);

	padapter->bDriverStopped = false;
	padapter->bSurpriseRemoved = false;
	padapter->bCardDisableWOHSM = false;

	status = rtw_hal_init(padapter);
	if (status == _FAIL)
		goto netdev_open_error;

	if (padapter->intf_start)
		padapter->intf_start(padapter);

	rtw_set_pwr_state_check_timer(&padapter->pwrctrlpriv);
	_set_timer(&padapter->mlmepriv.dynamic_chk_timer, 5000);

	return _SUCCESS;

netdev_open_error:
	DBG_88E("-ips_netdrv_open - drv_open failure, bup =%d\n", padapter->bup);

	return _FAIL;
}

int rtw_ips_pwr_up(struct adapter *padapter)
{
	int result;
	u32 start_time = jiffies;
	DBG_88E("===>  rtw_ips_pwr_up..............\n");
	rtw_reset_drv_sw(padapter);

	result = ips_netdrv_open(padapter);

	rtw_led_control(padapter, LED_CTL_NO_LINK);

	DBG_88E("<===  rtw_ips_pwr_up.............. in %dms\n", rtw_get_passing_time_ms(start_time));
	return result;
}

void rtw_ips_pwr_down(struct adapter *padapter)
{
	u32 start_time = jiffies;
	DBG_88E("===> rtw_ips_pwr_down...................\n");

	padapter->bCardDisableWOHSM = true;
	padapter->net_closed = true;

	rtw_led_control(padapter, LED_CTL_POWER_OFF);

	rtw_ips_dev_unload(padapter);
	padapter->bCardDisableWOHSM = false;
	DBG_88E("<=== rtw_ips_pwr_down..................... in %dms\n", rtw_get_passing_time_ms(start_time));
}

void rtw_ips_dev_unload(struct adapter *padapter)
{
	DBG_88E("====> %s...\n", __func__);

	rtw_hal_set_hwreg(padapter, HW_VAR_FIFO_CLEARN_UP, NULL);

	if (padapter->intf_stop)
		padapter->intf_stop(padapter);

	/* s5. */
	if (!padapter->bSurpriseRemoved)
		rtw_hal_deinit(padapter);
}

int pm_netdev_open(struct net_device *pnetdev, u8 bnormal)
{
	int status;

	if (bnormal)
		status = netdev_open(pnetdev);
	else
		status =  (_SUCCESS == ips_netdrv_open((struct adapter *)rtw_netdev_priv(pnetdev))) ? (0) : (-1);
	return status;
}

int netdev_close(struct net_device *pnetdev)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(pnetdev);
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

	if (padapter->pwrctrlpriv.bInternalAutoSuspend) {
		if (padapter->pwrctrlpriv.rf_pwrstate == rf_off)
			padapter->pwrctrlpriv.ps_flag = true;
	}
	padapter->net_closed = true;

	if (padapter->pwrctrlpriv.rf_pwrstate == rf_on) {
		DBG_88E("(2)88eu_drv - drv_close, bup =%d, hw_init_completed =%d\n",
			padapter->bup, padapter->hw_init_completed);

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
		/*  Close LED */
		rtw_led_control(padapter, LED_CTL_POWER_OFF);
	}

	nat25_db_cleanup(padapter);

#ifdef CONFIG_88EU_P2P
	rtw_p2p_enable(padapter, P2P_ROLE_DISABLE);
#endif /* CONFIG_88EU_P2P */

	kfree(dvobj->firmware.szFwBuffer);
	dvobj->firmware.szFwBuffer = NULL;

	DBG_88E("-88eu_drv - drv_close, bup =%d\n", padapter->bup);
	return 0;
}
