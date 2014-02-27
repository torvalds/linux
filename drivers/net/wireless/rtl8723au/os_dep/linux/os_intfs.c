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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _OS_INTFS_C_

#include <drv_conf.h>

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)

#error "Shall be Linux or Windows, but not both!\n"

#endif

#include <osdep_service.h>
#include <drv_types.h>
#include <xmit_osdep.h>
#include <recv_osdep.h>
#include <hal_intf.h>
#include <rtw_ioctl.h>
#include <rtw_version.h>

#ifdef CONFIG_USB_HCI
#include <usb_osintf.h>
#endif

#ifdef CONFIG_PCI_HCI
#include <pci_osintf.h>
#endif

#ifdef CONFIG_BR_EXT
#include <rtw_br_ext.h>
#endif //CONFIG_BR_EXT

#ifdef CONFIG_RF_GAIN_OFFSET
#ifdef CONFIG_RTL8723A
#define	RF_GAIN_OFFSET_ON			BIT0
#define		REG_RF_BB_GAIN_OFFSET	0x7f
#define		RF_GAIN_OFFSET_MASK		0xfffff
#else
#define	RF_GAIN_OFFSET_ON			BIT4
#define		REG_RF_BB_GAIN_OFFSET	0x55
#define		RF_GAIN_OFFSET_MASK		0xfffff
#endif  //CONFIG_RTL8723A
#endif //CONFIG_RF_GAIN_OFFSET

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek Wireless Lan Driver");
MODULE_AUTHOR("Realtek Semiconductor Corp.");
MODULE_VERSION(DRIVERVERSION);

/* module param defaults */
int rtw_chip_version = 0x00;
int rtw_rfintfs = HWPI;
int rtw_lbkmode = 0;//RTL8712_AIR_TRX;


int rtw_network_mode = Ndis802_11IBSS;//Ndis802_11Infrastructure;//infra, ad-hoc, auto
//NDIS_802_11_SSID	ssid;
int rtw_channel = 1;//ad-hoc support requirement 
int rtw_wireless_mode = WIRELESS_11BG_24N;
int rtw_vrtl_carrier_sense = AUTO_VCS;
int rtw_vcs_type = RTS_CTS;//*
int rtw_rts_thresh = 2347;//*
int rtw_frag_thresh = 2346;//*
int rtw_preamble = PREAMBLE_LONG;//long, short, auto
int rtw_scan_mode = 1;//active, passive
int rtw_adhoc_tx_pwr = 1;
int rtw_soft_ap = 0;
//int smart_ps = 1;  
#ifdef CONFIG_POWER_SAVING
int rtw_power_mgnt = 1;
#ifdef CONFIG_IPS_LEVEL_2
int rtw_ips_mode = IPS_LEVEL_2;
#else
int rtw_ips_mode = IPS_NORMAL;
#endif
#else
int rtw_power_mgnt = PS_MODE_ACTIVE;
int rtw_ips_mode = IPS_NONE;
#endif

int rtw_smart_ps = 2;

#ifdef CONFIG_TX_EARLY_MODE
int rtw_early_mode=1;
#endif
module_param(rtw_ips_mode, int, 0644);
MODULE_PARM_DESC(rtw_ips_mode,"The default IPS mode");

int rtw_radio_enable = 1;
int rtw_long_retry_lmt = 7;
int rtw_short_retry_lmt = 7;
int rtw_busy_thresh = 40;
//int qos_enable = 0; //*
int rtw_ack_policy = NORMAL_ACK;

int rtw_mp_mode = 0;

int rtw_software_encrypt = 0;
int rtw_software_decrypt = 0;

int rtw_acm_method = 0;// 0:By SW 1:By HW.
 
int rtw_wmm_enable = 1;// default is set to enable the wmm.
int rtw_uapsd_enable = 0;
int rtw_uapsd_max_sp = NO_LIMIT;
int rtw_uapsd_acbk_en = 0;
int rtw_uapsd_acbe_en = 0;
int rtw_uapsd_acvi_en = 0;
int rtw_uapsd_acvo_en = 0;
	
#ifdef CONFIG_80211N_HT
int rtw_ht_enable = 1;
int rtw_cbw40_enable = 3; // 0 :diable, bit(0): enable 2.4g, bit(1): enable 5g
int rtw_ampdu_enable = 1;//for enable tx_ampdu
int rtw_rx_stbc = 1;// 0: disable, bit(0):enable 2.4g, bit(1):enable 5g, default is set to enable 2.4GHZ for IOT issue with bufflao's AP at 5GHZ
int rtw_ampdu_amsdu = 0;// 0: disabled, 1:enabled, 2:auto
#endif

int rtw_lowrate_two_xmit = 1;//Use 2 path Tx to transmit MCS0~7 and legacy mode

//int rf_config = RF_1T2R;  // 1T2R	
int rtw_rf_config = RF_819X_MAX_TYPE;  //auto
int rtw_low_power = 0;
#ifdef CONFIG_WIFI_TEST
int rtw_wifi_spec = 1;//for wifi test
#else
int rtw_wifi_spec = 0;
#endif
int rtw_channel_plan = RT_CHANNEL_DOMAIN_MAX;

#ifdef CONFIG_BT_COEXIST
int rtw_btcoex_enable = 1;
int rtw_bt_iso = 2;// 0:Low, 1:High, 2:From Efuse
int rtw_bt_sco = 3;// 0:Idle, 1:None-SCO, 2:SCO, 3:From Counter, 4.Busy, 5.OtherBusy
int rtw_bt_ampdu =1 ;// 0:Disable BT control A-MPDU, 1:Enable BT control A-MPDU.
#endif

int rtw_AcceptAddbaReq = _TRUE;// 0:Reject AP's Add BA req, 1:Accept AP's Add BA req.

int rtw_antdiv_cfg = 2; // 0:OFF , 1:ON, 2:decide by Efuse config
int rtw_antdiv_type = 0 ; //0:decide by efuse  1: for 88EE, 1Tx and 1RxCG are diversity.(2 Ant with SPDT), 2:  for 88EE, 1Tx and 2Rx are diversity.( 2 Ant, Tx and RxCG are both on aux port, RxCS is on main port ), 3: for 88EE, 1Tx and 1RxCG are fixed.(1Ant, Tx and RxCG are both on aux port)


#ifdef CONFIG_USB_AUTOSUSPEND
int rtw_enusbss = 1;//0:disable,1:enable
#else
int rtw_enusbss = 0;//0:disable,1:enable
#endif

int rtw_hwpdn_mode=2;//0:disable,1:enable,2: by EFUSE config

#ifdef CONFIG_HW_PWRP_DETECTION
int rtw_hwpwrp_detect = 1;
#else
int rtw_hwpwrp_detect = 0; //HW power  ping detect 0:disable , 1:enable
#endif

#ifdef CONFIG_USB_HCI
int rtw_hw_wps_pbc = 1;
#else
int rtw_hw_wps_pbc = 0;
#endif

#ifdef CONFIG_TX_MCAST2UNI
int rtw_mc2u_disable = 0;
#endif	// CONFIG_TX_MCAST2UNI

#ifdef CONFIG_DUALMAC_CONCURRENT
int rtw_dmsp = 0;
#endif	// CONFIG_DUALMAC_CONCURRENT

#ifdef CONFIG_80211D
int rtw_80211d = 0;
#endif

#ifdef CONFIG_REGULATORY_CTRL
int rtw_regulatory_id =2;
#else
int rtw_regulatory_id = 0xff;// Regulatory tab id, 0xff = follow efuse's setting
#endif
module_param(rtw_regulatory_id, int, 0644);


#ifdef CONFIG_SPECIAL_SETTING_FOR_FUNAI_TV
int rtw_force_ant = 2;//0 :normal, 1:Main ant, 2:Aux ant
int rtw_force_igi =0;//0 :normal
module_param(rtw_force_ant, int, 0644);
module_param(rtw_force_igi, int, 0644);
#endif

#ifdef CONFIG_QOS_OPTIMIZATION
int rtw_qos_opt_enable=1;//0: disable,1:enable
#else
int rtw_qos_opt_enable=0;//0: disable,1:enable
#endif
module_param(rtw_qos_opt_enable,int,0644);

char* ifname = "wlan%d";
module_param(ifname, charp, 0644);
MODULE_PARM_DESC(ifname, "The default name to allocate for first interface");

char* if2name = "p2p0";
module_param(if2name, charp, 0644);
MODULE_PARM_DESC(if2name, "The default name to allocate for second interface");

char* rtw_initmac = 0;  // temp mac address if users want to use instead of the mac address in Efuse

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
#ifdef CONFIG_80211N_HT
module_param(rtw_ht_enable, int, 0644);
module_param(rtw_cbw40_enable, int, 0644);
module_param(rtw_ampdu_enable, int, 0644);
module_param(rtw_rx_stbc, int, 0644);
module_param(rtw_ampdu_amsdu, int, 0644);
#endif

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

#ifdef CONFIG_TX_EARLY_MODE
module_param(rtw_early_mode, int, 0644);
#endif
#ifdef CONFIG_ADAPTOR_INFO_CACHING_FILE
char *rtw_adaptor_info_caching_file_path= "/data/misc/wifi/rtw_cache";
module_param(rtw_adaptor_info_caching_file_path, charp, 0644);
MODULE_PARM_DESC(rtw_adaptor_info_caching_file_path, "The path of adapter info cache file");
#endif //CONFIG_ADAPTOR_INFO_CACHING_FILE

#ifdef CONFIG_LAYER2_ROAMING
uint rtw_max_roaming_times=2;
module_param(rtw_max_roaming_times, uint, 0644);
MODULE_PARM_DESC(rtw_max_roaming_times,"The max roaming times to try");
#endif //CONFIG_LAYER2_ROAMING

#ifdef CONFIG_IOL
int rtw_fw_iol=1;// 0:Disable, 1:enable, 2:by usb speed
module_param(rtw_fw_iol, int, 0644);
MODULE_PARM_DESC(rtw_fw_iol,"FW IOL");
#endif //CONFIG_IOL

#ifdef CONFIG_FILE_FWIMG
char *rtw_fw_file_path= "";
module_param(rtw_fw_file_path, charp, 0644);
MODULE_PARM_DESC(rtw_fw_file_path, "The path of fw image");
#endif //CONFIG_FILE_FWIMG

#ifdef CONFIG_TX_MCAST2UNI
module_param(rtw_mc2u_disable, int, 0644);
#endif	// CONFIG_TX_MCAST2UNI

#ifdef CONFIG_DUALMAC_CONCURRENT
module_param(rtw_dmsp, int, 0644);
#endif	// CONFIG_DUALMAC_CONCURRENT

#ifdef CONFIG_80211D
module_param(rtw_80211d, int, 0644);
MODULE_PARM_DESC(rtw_80211d, "Enable 802.11d mechanism");
#endif

#ifdef CONFIG_BT_COEXIST
module_param(rtw_btcoex_enable, int, 0644);
MODULE_PARM_DESC(rtw_btcoex_enable, "Enable BT co-existence mechanism");
#endif

uint rtw_notch_filter = RTW_NOTCH_FILTER;
module_param(rtw_notch_filter, uint, 0644);
MODULE_PARM_DESC(rtw_notch_filter, "0:Disable, 1:Enable, 2:Enable only for P2P");

static uint loadparam(PADAPTER padapter, _nic_hdl pnetdev);
int _netdev_open(struct net_device *pnetdev);
int netdev_open (struct net_device *pnetdev);
static int netdev_close (struct net_device *pnetdev);

//#ifdef RTK_DMP_PLATFORM
#ifdef CONFIG_PROC_DEBUG
#define RTL8192C_PROC_NAME "rtl819xC"
#define RTL8192D_PROC_NAME "rtl819xD"
static char rtw_proc_name[IFNAMSIZ];
static struct proc_dir_entry *rtw_proc = NULL;
static int	rtw_proc_cnt = 0;

#define RTW_PROC_NAME DRV_NAME

void rtw_proc_init_one(struct net_device *dev)
{
	struct proc_dir_entry *dir_dev = NULL;
	struct proc_dir_entry *entry=NULL;
	_adapter	*padapter = rtw_netdev_priv(dev);
	u8 rf_type;

	if(rtw_proc == NULL)
	{
		if(padapter->chip_type == RTL8188C_8192C)
		{
			_rtw_memcpy(rtw_proc_name, RTL8192C_PROC_NAME, sizeof(RTL8192C_PROC_NAME));
		}
		else if(padapter->chip_type == RTL8192D)
		{
			_rtw_memcpy(rtw_proc_name, RTL8192D_PROC_NAME, sizeof(RTL8192D_PROC_NAME));
		}
		else if(padapter->chip_type == RTL8723A)
		{
			_rtw_memcpy(rtw_proc_name, RTW_PROC_NAME, sizeof(RTW_PROC_NAME));
		}
		else if(padapter->chip_type == RTL8188E)
		{
			_rtw_memcpy(rtw_proc_name, RTW_PROC_NAME, sizeof(RTW_PROC_NAME));
		}
		else
		{
			_rtw_memcpy(rtw_proc_name, RTW_PROC_NAME, sizeof(RTW_PROC_NAME));
		}

#if(LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
		rtw_proc=create_proc_entry(rtw_proc_name, S_IFDIR, proc_net);
#else
		rtw_proc=create_proc_entry(rtw_proc_name, S_IFDIR, init_net.proc_net);
#endif
		if (rtw_proc == NULL) {
			DBG_871X(KERN_ERR "Unable to create rtw_proc directory\n");
			return;
		}

		entry = create_proc_read_entry("ver_info", S_IFREG | S_IRUGO, rtw_proc, proc_get_drv_version, dev);
		if (!entry) {
			DBG_871X("Unable to create_proc_read_entry!\n");
			return;
		}
		
#ifdef DBG_MEM_ALLOC
		entry = create_proc_read_entry("mstat", S_IFREG | S_IRUGO,
				   rtw_proc, proc_get_mstat, dev);
		if (!entry) {
			DBG_871X("Unable to create_proc_read_entry!\n");
			return;
		}
#endif /* DBG_MEM_ALLOC */
	}

	

	if(padapter->dir_dev == NULL)
	{
		padapter->dir_dev = create_proc_entry(dev->name,
					  S_IFDIR | S_IRUGO | S_IXUGO,
					  rtw_proc);

		dir_dev = padapter->dir_dev;

		if(dir_dev==NULL)
		{
			if(rtw_proc_cnt == 0)
			{
				if(rtw_proc){
#if(LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
					remove_proc_entry(rtw_proc_name, proc_net);
#else
					remove_proc_entry(rtw_proc_name, init_net.proc_net);
#endif		
					rtw_proc = NULL;
				}
			}

			DBG_871X("Unable to create dir_dev directory\n");
			return;
		}
	}
	else
	{
		return;
	}

	rtw_proc_cnt++;

	entry = create_proc_read_entry("write_reg", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_write_reg, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}
	entry->write_proc = proc_set_write_reg;

	entry = create_proc_read_entry("read_reg", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_read_reg, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}
	entry->write_proc = proc_set_read_reg;

	
	entry = create_proc_read_entry("fwstate", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_fwstate, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}


	entry = create_proc_read_entry("sec_info", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_sec_info, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}


	entry = create_proc_read_entry("mlmext_state", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_mlmext_state, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}


	entry = create_proc_read_entry("qos_option", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_qos_option, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("ht_option", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_ht_option, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("rf_info", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_rf_info, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}
	
	entry = create_proc_read_entry("ap_info", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_ap_info, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("adapter_state", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_adapter_state, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("trx_info", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_trx_info, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("mac_reg_dump1", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_mac_reg_dump1, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("mac_reg_dump2", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_mac_reg_dump2, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("mac_reg_dump3", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_mac_reg_dump3, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}
	
	entry = create_proc_read_entry("bb_reg_dump1", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_bb_reg_dump1, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("bb_reg_dump2", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_bb_reg_dump2, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("bb_reg_dump3", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_bb_reg_dump3, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("rf_reg_dump1", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_rf_reg_dump1, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}

	entry = create_proc_read_entry("rf_reg_dump2", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_rf_reg_dump2, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}
	
	rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
	if((RF_1T2R == rf_type) ||(RF_1T1R ==rf_type ))	{
		entry = create_proc_read_entry("rf_reg_dump3", S_IFREG | S_IRUGO,
					   dir_dev, proc_get_rf_reg_dump3, dev);
		if (!entry) {
			DBG_871X("Unable to create_proc_read_entry!\n");
			return;
		}

		entry = create_proc_read_entry("rf_reg_dump4", S_IFREG | S_IRUGO,
					   dir_dev, proc_get_rf_reg_dump4, dev);
		if (!entry) {
			DBG_871X("Unable to create_proc_read_entry!\n");
			return;
		}
	}
	
#ifdef CONFIG_AP_MODE

	entry = create_proc_read_entry("all_sta_info", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_all_sta_info, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}
#endif

#ifdef DBG_MEMORY_LEAK
	entry = create_proc_read_entry("_malloc_cnt", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_malloc_cnt, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}
#endif

#ifdef CONFIG_FIND_BEST_CHANNEL
	entry = create_proc_read_entry("best_channel", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_best_channel, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}
	entry->write_proc = proc_set_best_channel;
#endif

	entry = create_proc_read_entry("rx_signal", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_rx_signal, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}
	entry->write_proc = proc_set_rx_signal;
#ifdef CONFIG_80211N_HT
	entry = create_proc_read_entry("ht_enable", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_ht_enable, dev);				   
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n"); 
		return;
	}
	entry->write_proc = proc_set_ht_enable;
	
	entry = create_proc_read_entry("cbw40_enable", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_cbw40_enable, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}
	entry->write_proc = proc_set_cbw40_enable;
	
	entry = create_proc_read_entry("ampdu_enable", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_ampdu_enable, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}
	entry->write_proc = proc_set_ampdu_enable;
	
	entry = create_proc_read_entry("rx_stbc", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_rx_stbc, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}
	entry->write_proc = proc_set_rx_stbc;
#endif //CONFIG_80211N_HT
	
	entry = create_proc_read_entry("path_rssi", S_IFREG | S_IRUGO,
					dir_dev, proc_get_two_path_rssi, dev);
	

	entry = create_proc_read_entry("rssi_disp", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_rssi_disp, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}
	entry->write_proc = proc_set_rssi_disp;
#ifdef CONFIG_BT_COEXIST
	entry = create_proc_read_entry("btcoex_dbg", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_btcoex_dbg, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}
	entry->write_proc = proc_set_btcoex_dbg;
#endif /*CONFIG_BT_COEXIST*/

#if defined(DBG_CONFIG_ERROR_DETECT)
	entry = create_proc_read_entry("sreset", S_IFREG | S_IRUGO,
				   dir_dev, proc_get_sreset, dev);
	if (!entry) {
		DBG_871X("Unable to create_proc_read_entry!\n");
		return;
	}
	entry->write_proc = proc_set_sreset;
#endif /* DBG_CONFIG_ERROR_DETECT */

	/* for odm */
	{
		struct proc_dir_entry *dir_odm = NULL;

		if (padapter->dir_odm == NULL) {
			padapter->dir_odm = create_proc_entry(
				"odm", S_IFDIR | S_IRUGO | S_IXUGO, dir_dev);

			dir_odm = padapter->dir_odm;

			if(dir_odm==NULL) {
				DBG_871X("Unable to create dir_odm directory\n");
				return;
			}
		}
		else
		{
			return;
		}

		entry = create_proc_read_entry("dbg_comp", S_IFREG | S_IRUGO,
					   dir_odm, proc_get_odm_dbg_comp, dev);
		if (!entry) {
			rtw_warn_on(1);
			return;
		}
		entry->write_proc = proc_set_odm_dbg_comp;

		entry = create_proc_read_entry("dbg_level", S_IFREG | S_IRUGO,
					   dir_odm, proc_get_odm_dbg_level, dev);
		if (!entry) {
			rtw_warn_on(1);
			return;
		}
		entry->write_proc = proc_set_odm_dbg_level;

		entry = create_proc_read_entry("adaptivity", S_IFREG | S_IRUGO,
					   dir_odm, proc_get_odm_adaptivity, dev);
		if (!entry) {
			rtw_warn_on(1);
			return;
		}
		entry->write_proc = proc_set_odm_adaptivity;
	}
}

void rtw_proc_remove_one(struct net_device *dev)
{
	struct proc_dir_entry *dir_dev = NULL;
	_adapter	*padapter = rtw_netdev_priv(dev);
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
		if((RF_1T2R == rf_type) ||(RF_1T1R ==rf_type ))	{
			remove_proc_entry("rf_reg_dump3", dir_dev);
			remove_proc_entry("rf_reg_dump4", dir_dev);
		}
#ifdef CONFIG_AP_MODE	
		remove_proc_entry("all_sta_info", dir_dev);
#endif		

#ifdef DBG_MEMORY_LEAK
		remove_proc_entry("_malloc_cnt", dir_dev);
#endif 

#ifdef CONFIG_FIND_BEST_CHANNEL
		remove_proc_entry("best_channel", dir_dev);
#endif 
		remove_proc_entry("rx_signal", dir_dev);
#ifdef CONFIG_80211N_HT
		remove_proc_entry("cbw40_enable", dir_dev);

		remove_proc_entry("ht_enable", dir_dev);

		remove_proc_entry("ampdu_enable", dir_dev);

		remove_proc_entry("rx_stbc", dir_dev);
#endif //CONFIG_80211N_HT
		remove_proc_entry("path_rssi", dir_dev);

		remove_proc_entry("rssi_disp", dir_dev);

#ifdef CONFIG_BT_COEXIST
		remove_proc_entry("btcoex_dbg", dir_dev);
#endif //CONFIG_BT_COEXIST

#if defined(DBG_CONFIG_ERROR_DETECT)
	remove_proc_entry("sreset", dir_dev);
#endif /* DBG_CONFIG_ERROR_DETECT */

		/* for odm */
		{
			struct proc_dir_entry *dir_odm = NULL;
			dir_odm = padapter->dir_odm;
			padapter->dir_odm = NULL;

			if (dir_odm) {
				remove_proc_entry("dbg_comp", dir_odm);
				remove_proc_entry("dbg_level", dir_odm);
				remove_proc_entry("adaptivity", dir_odm);

				remove_proc_entry("odm", dir_dev);
			}
		}

		remove_proc_entry(dev->name, rtw_proc);
		dir_dev = NULL;
		
	}
	else
	{
		return;
	}

	rtw_proc_cnt--;

	if(rtw_proc_cnt == 0)
	{
		if(rtw_proc){
			remove_proc_entry("ver_info", rtw_proc);
			#ifdef DBG_MEM_ALLOC
			remove_proc_entry("mstat", rtw_proc);
			#endif /* DBG_MEM_ALLOC */
#if(LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
			remove_proc_entry(rtw_proc_name, proc_net);
#else
			remove_proc_entry(rtw_proc_name, init_net.proc_net);
#endif		
			rtw_proc = NULL;
		}
	}
}
#endif

uint loadparam( _adapter *padapter,  _nic_hdl	pnetdev);
uint loadparam( _adapter *padapter,  _nic_hdl	pnetdev)
{
       
	uint status = _SUCCESS;
	struct registry_priv  *registry_par = &padapter->registrypriv;

_func_enter_;

	registry_par->chip_version = (u8)rtw_chip_version;
	registry_par->rfintfs = (u8)rtw_rfintfs;
	registry_par->lbkmode = (u8)rtw_lbkmode;	
	//registry_par->hci = (u8)hci;
	registry_par->network_mode  = (u8)rtw_network_mode;	

	_rtw_memcpy(registry_par->ssid.Ssid, "ANY", 3);
	registry_par->ssid.SsidLength = 3;
	
	registry_par->channel = (u8)rtw_channel;
	registry_par->wireless_mode = (u8)rtw_wireless_mode;
	registry_par->vrtl_carrier_sense = (u8)rtw_vrtl_carrier_sense ;
	registry_par->vcs_type = (u8)rtw_vcs_type;
	registry_par->rts_thresh=(u16)rtw_rts_thresh;
	registry_par->frag_thresh=(u16)rtw_frag_thresh;
	registry_par->preamble = (u8)rtw_preamble;
	registry_par->scan_mode = (u8)rtw_scan_mode;
	registry_par->adhoc_tx_pwr = (u8)rtw_adhoc_tx_pwr;
	registry_par->soft_ap=  (u8)rtw_soft_ap;
	registry_par->smart_ps =  (u8)rtw_smart_ps;  
	registry_par->power_mgnt = (u8)rtw_power_mgnt;
	registry_par->ips_mode = (u8)rtw_ips_mode;
	registry_par->radio_enable = (u8)rtw_radio_enable;
	registry_par->long_retry_lmt = (u8)rtw_long_retry_lmt;
	registry_par->short_retry_lmt = (u8)rtw_short_retry_lmt;
  	registry_par->busy_thresh = (u16)rtw_busy_thresh;
  	//registry_par->qos_enable = (u8)rtw_qos_enable;
	registry_par->ack_policy = (u8)rtw_ack_policy;
	registry_par->mp_mode = (u8)rtw_mp_mode;	
	registry_par->software_encrypt = (u8)rtw_software_encrypt;
	registry_par->software_decrypt = (u8)rtw_software_decrypt;	  

	registry_par->acm_method = (u8)rtw_acm_method;

	 //UAPSD
	registry_par->wmm_enable = (u8)rtw_wmm_enable;
	registry_par->uapsd_enable = (u8)rtw_uapsd_enable;	  
	registry_par->uapsd_max_sp = (u8)rtw_uapsd_max_sp;
	registry_par->uapsd_acbk_en = (u8)rtw_uapsd_acbk_en;
	registry_par->uapsd_acbe_en = (u8)rtw_uapsd_acbe_en;
	registry_par->uapsd_acvi_en = (u8)rtw_uapsd_acvi_en;
	registry_par->uapsd_acvo_en = (u8)rtw_uapsd_acvo_en;

#ifdef CONFIG_80211N_HT
	registry_par->ht_enable = (u8)rtw_ht_enable;
	registry_par->cbw40_enable = (u8)rtw_cbw40_enable;
	registry_par->ampdu_enable = (u8)rtw_ampdu_enable;
	registry_par->rx_stbc = (u8)rtw_rx_stbc;	
	registry_par->ampdu_amsdu = (u8)rtw_ampdu_amsdu;
#endif
#ifdef CONFIG_TX_EARLY_MODE
	registry_par->early_mode = (u8)rtw_early_mode;
#endif
	registry_par->lowrate_two_xmit = (u8)rtw_lowrate_two_xmit;
	registry_par->rf_config = (u8)rtw_rf_config;
	registry_par->low_power = (u8)rtw_low_power;

	
	registry_par->wifi_spec = (u8)rtw_wifi_spec;

	registry_par->channel_plan = (u8)rtw_channel_plan;

#ifdef CONFIG_BT_COEXIST
	registry_par->btcoex = (u8)rtw_btcoex_enable;
	registry_par->bt_iso = (u8)rtw_bt_iso;
	registry_par->bt_sco = (u8)rtw_bt_sco;
	registry_par->bt_ampdu = (u8)rtw_bt_ampdu;
#endif

	registry_par->bAcceptAddbaReq = (u8)rtw_AcceptAddbaReq;

	registry_par->antdiv_cfg = (u8)rtw_antdiv_cfg;
	registry_par->antdiv_type = (u8)rtw_antdiv_type;

#ifdef CONFIG_AUTOSUSPEND
	registry_par->usbss_enable = (u8)rtw_enusbss;//0:disable,1:enable
#endif
#ifdef SUPPORT_HW_RFOFF_DETECTED
	registry_par->hwpdn_mode = (u8)rtw_hwpdn_mode;//0:disable,1:enable,2:by EFUSE config
	registry_par->hwpwrp_detect = (u8)rtw_hwpwrp_detect;//0:disable,1:enable
#endif

	registry_par->qos_opt_enable = (u8)rtw_qos_opt_enable;
	registry_par->hw_wps_pbc = (u8)rtw_hw_wps_pbc;

#ifdef CONFIG_ADAPTOR_INFO_CACHING_FILE
	snprintf(registry_par->adaptor_info_caching_file_path, PATH_LENGTH_MAX, "%s", rtw_adaptor_info_caching_file_path);
	registry_par->adaptor_info_caching_file_path[PATH_LENGTH_MAX-1]=0;
#endif

#ifdef CONFIG_LAYER2_ROAMING
	registry_par->max_roaming_times = (u8)rtw_max_roaming_times;
#ifdef CONFIG_INTEL_WIDI
	registry_par->max_roaming_times = (u8)rtw_max_roaming_times + 2;
#endif // CONFIG_INTEL_WIDI
#endif

#ifdef CONFIG_IOL
	registry_par->fw_iol = rtw_fw_iol;
#endif

#ifdef CONFIG_DUALMAC_CONCURRENT
	registry_par->dmsp= (u8)rtw_dmsp;
#endif

#ifdef CONFIG_80211D
	registry_par->enable80211d = (u8)rtw_80211d;
#endif

	snprintf(registry_par->ifname, 16, "%s", ifname);
	snprintf(registry_par->if2name, 16, "%s", if2name);

	registry_par->notch_filter = (u8)rtw_notch_filter;

#ifdef CONFIG_SPECIAL_SETTING_FOR_FUNAI_TV
	registry_par->force_ant = (u8)rtw_force_ant;
	registry_par->force_igi = (u8)rtw_force_igi;
#endif
	registry_par->regulatory_tid = (u8)rtw_regulatory_id;
	

_func_exit_;

	return status;
}

static int rtw_net_set_mac_address(struct net_device *pnetdev, void *p)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	struct sockaddr *addr = p;
	
	if(padapter->bup == _FALSE)
	{
		//DBG_871X("r8711_net_set_mac_address(), MAC=%x:%x:%x:%x:%x:%x\n", addr->sa_data[0], addr->sa_data[1], addr->sa_data[2], addr->sa_data[3],
		//addr->sa_data[4], addr->sa_data[5]);
		_rtw_memcpy(padapter->eeprompriv.mac_addr, addr->sa_data, ETH_ALEN);
		//_rtw_memcpy(pnetdev->dev_addr, addr->sa_data, ETH_ALEN);
		//padapter->bset_hwaddr = _TRUE;
	}

	return 0;
}

static struct net_device_stats *rtw_net_get_stats(struct net_device *pnetdev)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);
	struct recv_priv *precvpriv = &(padapter->recvpriv);

	padapter->stats.tx_packets = pxmitpriv->tx_pkts;//pxmitpriv->tx_pkts++;
	padapter->stats.rx_packets = precvpriv->rx_pkts;//precvpriv->rx_pkts++; 		
	padapter->stats.tx_dropped = pxmitpriv->tx_drop;
	padapter->stats.rx_dropped = precvpriv->rx_drop;
	padapter->stats.tx_bytes = pxmitpriv->tx_bytes;
	padapter->stats.rx_bytes = precvpriv->rx_bytes;
	
	return &padapter->stats;
}

#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
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
unsigned int rtw_classify8021d(struct sk_buff *skb)
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

static u16 rtw_select_queue(struct net_device *dev, struct sk_buff *skb)
{
	_adapter	*padapter = rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	skb->priority = rtw_classify8021d(skb);

	if(pmlmepriv->acm_mask != 0)
	{
		skb->priority = qos_acm(pmlmepriv->acm_mask, skb->priority);
	}

	return rtw_1d_to_queue[skb->priority];
}

u16 rtw_recv_select_queue(struct sk_buff *skb)
{
	struct iphdr *piphdr;
	unsigned int dscp;
	u16	eth_type;
	u32 priority;
	u8 *pdata = skb->data;

	_rtw_memcpy(&eth_type, pdata+(ETH_ALEN<<1), 2);

	switch (eth_type) {
		case htons(ETH_P_IP):

			piphdr = (struct iphdr *)(pdata+ETH_HLEN);

			dscp = piphdr->tos & 0xfc;
			
			priority = dscp >> 5;
			
			break;
		default:
			priority = 0;
	}

	return rtw_1d_to_queue[priority];
	
}

#endif

#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,29))
static const struct net_device_ops rtw_netdev_ops = {
	.ndo_open = netdev_open,
	.ndo_stop = netdev_close,
	.ndo_start_xmit = rtw_xmit_entry,
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
	.ndo_select_queue	= rtw_select_queue,
#endif
	.ndo_set_mac_address = rtw_net_set_mac_address,
	.ndo_get_stats = rtw_net_get_stats,
	.ndo_do_ioctl = rtw_ioctl,
};
#endif

int rtw_init_netdev_name(struct net_device *pnetdev, const char *ifname)
{
	_adapter *padapter = rtw_netdev_priv(pnetdev);

#ifdef CONFIG_EASY_REPLACEMENT
	struct net_device	*TargetNetdev = NULL;
	_adapter			*TargetAdapter = NULL;
	struct net 		*devnet = NULL;

	if(padapter->bDongle == 1)
	{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
		TargetNetdev = dev_get_by_name("wlan0");
#else
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26))
		devnet = pnetdev->nd_net;
	#else
		devnet = dev_net(pnetdev);
	#endif
		TargetNetdev = dev_get_by_name(devnet, "wlan0");
#endif
		if(TargetNetdev) {
			DBG_871X("Force onboard module driver disappear !!!\n");
			TargetAdapter = rtw_netdev_priv(TargetNetdev);
			TargetAdapter->DriverState = DRIVER_DISAPPEAR;

			padapter->pid[0] = TargetAdapter->pid[0];
			padapter->pid[1] = TargetAdapter->pid[1];
			padapter->pid[2] = TargetAdapter->pid[2];
			
			dev_put(TargetNetdev);
			unregister_netdev(TargetNetdev);

			if(TargetAdapter->chip_type == padapter->chip_type)
				rtw_proc_remove_one(TargetNetdev);

			padapter->DriverState = DRIVER_REPLACE_DONGLE;
		}
	}
#endif

	if(dev_alloc_name(pnetdev, ifname) < 0)
	{
		RT_TRACE(_module_os_intfs_c_,_drv_err_,("dev_alloc_name, fail! \n"));
	}

	netif_carrier_off(pnetdev);
	//rtw_netif_stop_queue(pnetdev);

	return 0;
}

struct net_device *rtw_init_netdev(_adapter *old_padapter)	
{
	_adapter *padapter;
	struct net_device *pnetdev;

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("+init_net_dev\n"));

	if(old_padapter != NULL) 
		pnetdev = rtw_alloc_etherdev_with_old_priv(sizeof(_adapter), (void *)old_padapter);
	else 
		pnetdev = rtw_alloc_etherdev(sizeof(_adapter));
	
	if (!pnetdev)
		return NULL;

	padapter = rtw_netdev_priv(pnetdev);
	padapter->pnetdev = pnetdev;	

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	SET_MODULE_OWNER(pnetdev);
#endif
	
	//pnetdev->init = NULL;
	
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,29))
	DBG_871X("register rtw_netdev_ops to netdev_ops\n");
	pnetdev->netdev_ops = &rtw_netdev_ops;
#else
	pnetdev->open = netdev_open;
	pnetdev->stop = netdev_close;	
	pnetdev->hard_start_xmit = rtw_xmit_entry;
	pnetdev->set_mac_address = rtw_net_set_mac_address;
	pnetdev->get_stats = rtw_net_get_stats;
	pnetdev->do_ioctl = rtw_ioctl;
#endif


#ifdef CONFIG_TCP_CSUM_OFFLOAD_TX
	pnetdev->features |= NETIF_F_IP_CSUM;
#endif	
	//pnetdev->tx_timeout = NULL;
	pnetdev->watchdog_timeo = HZ*3; /* 3 second timeout */
#ifdef CONFIG_WIRELESS_EXT
	pnetdev->wireless_handlers = (struct iw_handler_def *)&rtw_handlers_def;
#endif

#ifdef WIRELESS_SPY
	//priv->wireless_data.spy_data = &priv->spy_data;
	//pnetdev->wireless_data = &priv->wireless_data;
#endif

	//step 2.
   	loadparam(padapter, pnetdev);
	
	return pnetdev;

}

u32 rtw_start_drv_threads(_adapter *padapter)
{
	u32 _status = _SUCCESS;

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("+rtw_start_drv_threads\n"));
#ifdef CONFIG_XMIT_THREAD_MODE
#if defined(CONFIG_SDIO_HCI) && defined(CONFIG_CONCURRENT_MODE)
	if(padapter->adapter_type == PRIMARY_ADAPTER){
#endif
	padapter->xmitThread = kthread_run(rtw_xmit_thread, padapter, "RTW_XMIT_THREAD");
	if(IS_ERR(padapter->xmitThread))
		_status = _FAIL;
#if defined(CONFIG_SDIO_HCI) && defined(CONFIG_CONCURRENT_MODE)
	}
#endif		// CONFIG_SDIO_HCI+CONFIG_CONCURRENT_MODE
#endif

#ifdef CONFIG_RECV_THREAD_MODE
	padapter->recvThread = kthread_run(rtw_recv_thread, padapter, "RTW_RECV_THREAD");
	if(IS_ERR(padapter->recvThread))
		_status = _FAIL;	
#endif


#ifdef CONFIG_CONCURRENT_MODE
	if(padapter->isprimary == _TRUE)
#endif //CONFIG_CONCURRENT_MODE
	{
		padapter->cmdThread = kthread_run(rtw_cmd_thread, padapter, "RTW_CMD_THREAD");
	        if(IS_ERR(padapter->cmdThread))
			_status = _FAIL;
		else
			_rtw_down_sema(&padapter->cmdpriv.terminate_cmdthread_sema); //wait for cmd_thread to run
	}	
		

#ifdef CONFIG_EVENT_THREAD_MODE
	padapter->evtThread = kthread_run(event_thread, padapter, "RTW_EVENT_THREAD");
	if(IS_ERR(padapter->evtThread))
		_status = _FAIL;		
#endif

	rtw_hal_start_thread(padapter);
	return _status;

}

void rtw_unregister_netdevs(struct dvobj_priv *dvobj)
{
	int i;
	_adapter *padapter = NULL;

	for(i=0;i<dvobj->iface_nums;i++)
	{
		struct net_device *pnetdev = NULL;
		
		padapter = dvobj->padapters[i];

		if (padapter == NULL)
			continue;

		pnetdev = padapter->pnetdev;

		if((padapter->DriverState != DRIVER_DISAPPEAR) && pnetdev) {
		
			unregister_netdev(pnetdev); //will call netdev_close()
			rtw_proc_remove_one(pnetdev);
		}

#ifdef CONFIG_IOCTL_CFG80211	
		rtw_wdev_unregister(padapter->rtw_wdev);
#endif

	}
	
}	


void rtw_stop_drv_threads (_adapter *padapter)
{
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("+rtw_stop_drv_threads\n"));	

#ifdef CONFIG_CONCURRENT_MODE
	if(padapter->isprimary == _TRUE)
#endif //CONFIG_CONCURRENT_MODE
	{
		rtw_stop_cmd_thread(padapter);
	}	

#ifdef CONFIG_EVENT_THREAD_MODE
        _rtw_up_sema(&padapter->evtpriv.evt_notify);
	if(padapter->evtThread){
		_rtw_down_sema(&padapter->evtpriv.terminate_evtthread_sema);
	}
#endif

#ifdef CONFIG_XMIT_THREAD_MODE
	// Below is to termindate tx_thread...
#if defined(CONFIG_SDIO_HCI) && defined(CONFIG_CONCURRENT_MODE)	
	// Only wake-up primary adapter
	if(padapter->adapter_type == PRIMARY_ADAPTER) 
#endif  //SDIO_HCI + CONCURRENT
	{
	_rtw_up_sema(&padapter->xmitpriv.xmit_sema);
	_rtw_down_sema(&padapter->xmitpriv.terminate_xmitthread_sema);
	}
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("\n drv_halt: rtw_xmit_thread can be terminated ! \n"));
#endif
	 
#ifdef CONFIG_RECV_THREAD_MODE	
	// Below is to termindate rx_thread...
	_rtw_up_sema(&padapter->recvpriv.recv_sema);
	_rtw_down_sema(&padapter->recvpriv.terminate_recvthread_sema);
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("\n drv_halt:recv_thread can be terminated! \n"));
#endif

	rtw_hal_stop_thread(padapter);
}

u8 rtw_init_default_value(_adapter *padapter);
u8 rtw_init_default_value(_adapter *padapter)
{
	u8 ret  = _SUCCESS;
	struct registry_priv* pregistrypriv = &padapter->registrypriv;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct mlme_priv *pmlmepriv= &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	//xmit_priv
	pxmitpriv->vcs_setting = pregistrypriv->vrtl_carrier_sense;
	pxmitpriv->vcs = pregistrypriv->vcs_type;
	pxmitpriv->vcs_type = pregistrypriv->vcs_type;
	//pxmitpriv->rts_thresh = pregistrypriv->rts_thresh;
	pxmitpriv->frag_len = pregistrypriv->frag_thresh;
	
		

	//recv_priv
	

	//mlme_priv
	pmlmepriv->scan_interval = SCAN_INTERVAL;// 30*2 sec = 60sec
	pmlmepriv->scan_mode = SCAN_ACTIVE;

	//qos_priv
	//pmlmepriv->qospriv.qos_option = pregistrypriv->wmm_enable;
	
	//ht_priv
#ifdef CONFIG_80211N_HT		
	pmlmepriv->htpriv.ampdu_enable = _FALSE;//set to disabled
#endif	

	//security_priv
	//rtw_get_encrypt_decrypt_from_registrypriv(padapter);
	psecuritypriv->binstallGrpkey = _FAIL;
	psecuritypriv->sw_encrypt=pregistrypriv->software_encrypt;
	psecuritypriv->sw_decrypt=pregistrypriv->software_decrypt;
	
	psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open; //open system
	psecuritypriv->dot11PrivacyAlgrthm = _NO_PRIVACY_;

	psecuritypriv->dot11PrivacyKeyIndex = 0;

	psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
	psecuritypriv->dot118021XGrpKeyid = 1;

	psecuritypriv->ndisauthtype = Ndis802_11AuthModeOpen;
	psecuritypriv->ndisencryptstatus = Ndis802_11WEPDisabled;
	

	//pwrctrl_priv


	//registry_priv
	rtw_init_registrypriv_dev_network(padapter);		
	rtw_update_registrypriv_dev_network(padapter);


	//hal_priv
	rtw_hal_def_value_init(padapter);

	//misc.
	padapter->bReadPortCancel = _FALSE;
	padapter->bWritePortCancel = _FALSE;
	padapter->bRxRSSIDisplay = 0;
	padapter->bNotifyChannelChange = 0;
#ifdef CONFIG_P2P
	padapter->bShowGetP2PState = 1;
#endif

	return ret;
}

u8 rtw_reset_drv_sw(_adapter *padapter)
{
	u8	ret8=_SUCCESS;	
	struct mlme_priv *pmlmepriv= &padapter->mlmepriv;
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	//hal_priv
	rtw_hal_def_value_init(padapter);
	padapter->bReadPortCancel = _FALSE;
	padapter->bWritePortCancel = _FALSE;
	padapter->bRxRSSIDisplay = 0;
	pmlmepriv->scan_interval = SCAN_INTERVAL;// 30*2 sec = 60sec

	padapter->xmitpriv.tx_pkts = 0;
	padapter->recvpriv.rx_pkts = 0;

	pmlmepriv->LinkDetectInfo.bBusyTraffic = _FALSE;

	_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY |_FW_UNDER_LINKING);

#ifdef CONFIG_AUTOSUSPEND	
	#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,22) && LINUX_VERSION_CODE<=KERNEL_VERSION(2,6,34))
		adapter_to_dvobj(padapter)->pusbdev->autosuspend_disabled = 1;//autosuspend disabled by the user
	#endif
#endif

#ifdef DBG_CONFIG_ERROR_DETECT
	rtw_hal_sreset_reset_value(padapter);
#endif
	pwrctrlpriv->pwr_state_check_cnts = 0;

	//mlmeextpriv
	padapter->mlmeextpriv.sitesurvey_res.state= SCAN_DISABLE;

#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	rtw_set_signal_stat_timer(&padapter->recvpriv);
#endif

	return ret8;
}


u8 rtw_init_drv_sw(_adapter *padapter)
{

	u8	ret8=_SUCCESS;

_func_enter_;

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("+rtw_init_drv_sw\n"));

	if ((rtw_init_cmd_priv(&padapter->cmdpriv)) == _FAIL)
	{
		RT_TRACE(_module_os_intfs_c_,_drv_err_,("\n Can't init cmd_priv\n"));
		ret8=_FAIL;
		goto exit;
	}
	
	padapter->cmdpriv.padapter=padapter;
	
	if ((rtw_init_evt_priv(&padapter->evtpriv)) == _FAIL)
	{
		RT_TRACE(_module_os_intfs_c_,_drv_err_,("\n Can't init evt_priv\n"));
		ret8=_FAIL;
		goto exit;
	}
	
	
	if (rtw_init_mlme_priv(padapter) == _FAIL)
	{
		RT_TRACE(_module_os_intfs_c_,_drv_err_,("\n Can't init mlme_priv\n"));
		ret8=_FAIL;
		goto exit;
	}

#ifdef CONFIG_P2P
	rtw_init_wifidirect_timers(padapter);
	init_wifidirect_info(padapter, P2P_ROLE_DISABLE);
	reset_global_wifidirect_info(padapter);
	#ifdef CONFIG_IOCTL_CFG80211
	rtw_init_cfg80211_wifidirect_info(padapter);
	#endif
#ifdef CONFIG_WFD
	if(rtw_init_wifi_display_info(padapter) == _FAIL)
		RT_TRACE(_module_os_intfs_c_,_drv_err_,("\n Can't init init_wifi_display_info\n"));
#endif
#endif /* CONFIG_P2P */

	if(init_mlme_ext_priv(padapter) == _FAIL)
	{
		RT_TRACE(_module_os_intfs_c_,_drv_err_,("\n Can't init mlme_ext_priv\n"));
		ret8=_FAIL;
		goto exit;
	}

#ifdef CONFIG_TDLS
	if(rtw_init_tdls_info(padapter) == _FAIL)
	{
		DBG_871X("Can't rtw_init_tdls_info\n");
		ret8=_FAIL;
		goto exit;
	}
#endif //CONFIG_TDLS

	if(_rtw_init_xmit_priv(&padapter->xmitpriv, padapter) == _FAIL)
	{
		DBG_871X("Can't _rtw_init_xmit_priv\n");
		ret8=_FAIL;
		goto exit;
	}
		
	if(_rtw_init_recv_priv(&padapter->recvpriv, padapter) == _FAIL)
	{
		DBG_871X("Can't _rtw_init_recv_priv\n");
		ret8=_FAIL;
		goto exit;
	}
	// add for CONFIG_IEEE80211W, none 11w also can use
	_rtw_spinlock_init(&padapter->security_key_mutex);
	
	// We don't need to memset padapter->XXX to zero, because adapter is allocated by rtw_zvmalloc().
	//_rtw_memset((unsigned char *)&padapter->securitypriv, 0, sizeof (struct security_priv));	
	
	//_init_timer(&(padapter->securitypriv.tkip_timer), padapter->pifp, rtw_use_tkipkey_handler, padapter);

	if(_rtw_init_sta_priv(&padapter->stapriv) == _FAIL)
	{
		DBG_871X("Can't _rtw_init_sta_priv\n");
		ret8=_FAIL;
		goto exit;
	}
	
	padapter->stapriv.padapter = padapter;		
	padapter->setband = GHZ24_50;	
	padapter->fix_rate = 0xFF;
	rtw_init_bcmc_stainfo(padapter);

	rtw_init_pwrctrl_priv(padapter);	

	//_rtw_memset((u8 *)&padapter->qospriv, 0, sizeof (struct qos_priv));//move to mlme_priv
	
#ifdef CONFIG_MP_INCLUDED
	if (init_mp_priv(padapter) == _FAIL) {
		DBG_871X("%s: initialize MP private data Fail!\n", __func__);
	}
#endif

	ret8 = rtw_init_default_value(padapter);

	rtw_hal_dm_init(padapter);
	rtw_hal_sw_led_init(padapter);

#ifdef DBG_CONFIG_ERROR_DETECT
	rtw_hal_sreset_init(padapter);
#endif

#ifdef CONFIG_INTEL_WIDI
	if(rtw_init_intel_widi(padapter) == _FAIL)
	{
		DBG_871X("Can't rtw_init_intel_widi\n");
		ret8=_FAIL;
		goto exit;
	}
#endif //CONFIG_INTEL_WIDI

#ifdef CONFIG_WAPI_SUPPORT
	padapter->WapiSupport = true; //set true temp, will revise according to Efuse or Registry value later.
	rtw_wapi_init(padapter);
#endif

#ifdef CONFIG_BR_EXT
	_rtw_spinlock_init(&padapter->br_ext_lock);
#endif	// CONFIG_BR_EXT

exit:
	
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("-rtw_init_drv_sw\n"));

	_func_exit_;	
	
	return ret8;
	
}

#ifdef CONFIG_WOWLAN
void rtw_cancel_dynamic_chk_timer(_adapter *padapter)
{
	_cancel_timer_ex(&padapter->mlmepriv.dynamic_chk_timer);
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("rtw_cancel_all_timer:cancel dynamic_chk_timer! \n"));
}
#endif

void rtw_cancel_all_timer(_adapter *padapter)
{
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("+rtw_cancel_all_timer\n"));

	_cancel_timer_ex(&padapter->mlmepriv.assoc_timer);
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("rtw_cancel_all_timer:cancel association timer complete! \n"));

	//_cancel_timer_ex(&padapter->securitypriv.tkip_timer);
	//RT_TRACE(_module_os_intfs_c_,_drv_info_,("rtw_cancel_all_timer:cancel tkip_timer! \n"));

	_cancel_timer_ex(&padapter->mlmepriv.scan_to_timer);
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("rtw_cancel_all_timer:cancel scan_to_timer! \n"));	
	
	_cancel_timer_ex(&padapter->mlmepriv.dynamic_chk_timer);
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("rtw_cancel_all_timer:cancel dynamic_chk_timer! \n"));

	// cancel sw led timer
	rtw_hal_sw_led_deinit(padapter);
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("rtw_cancel_all_timer:cancel DeInitSwLeds! \n"));

	_cancel_timer_ex(&(adapter_to_pwrctl(padapter)->pwr_state_check_timer));

#ifdef CONFIG_IOCTL_CFG80211
#ifdef CONFIG_P2P
	_cancel_timer_ex(&padapter->cfg80211_wdinfo.remain_on_ch_timer);
#endif //CONFIG_P2P
#endif //CONFIG_IOCTL_CFG80211

#ifdef CONFIG_SET_SCAN_DENY_TIMER
	_cancel_timer_ex(&padapter->mlmepriv.set_scan_deny_timer);
	rtw_clear_scan_deny(padapter);
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("rtw_cancel_all_timer:cancel set_scan_deny_timer! \n"));
#endif

#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	_cancel_timer_ex(&padapter->recvpriv.signal_stat_timer);
#endif

#ifdef CONFIG_DETECT_C2H_BY_POLLING
	_cancel_timer_ex(&padapter->mlmepriv.event_polling_timer);
#endif

#if defined(CONFIG_CHECK_BT_HANG) && defined(CONFIG_BT_COEXIST)
	if (padapter->HalFunc.hal_cancel_checkbthang_workqueue)
		padapter->HalFunc.hal_cancel_checkbthang_workqueue(padapter);
#endif	
	//cancel dm timer
	rtw_hal_dm_deinit(padapter);	

}

u8 rtw_free_drv_sw(_adapter *padapter)
{
	struct net_device *pnetdev = (struct net_device*)padapter->pnetdev;

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("==>rtw_free_drv_sw"));

#ifdef CONFIG_WAPI_SUPPORT
	rtw_wapi_free(padapter);
#endif

	//we can call rtw_p2p_enable here, but:
	// 1. rtw_p2p_enable may have IO operation
	// 2. rtw_p2p_enable is bundled with wext interface
	#ifdef CONFIG_P2P
	{
		struct wifidirect_info *pwdinfo = &padapter->wdinfo;
		if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		{
			_cancel_timer_ex( &pwdinfo->find_phase_timer );
			_cancel_timer_ex( &pwdinfo->restore_p2p_state_timer );
			_cancel_timer_ex( &pwdinfo->pre_tx_scan_timer);
#ifdef CONFIG_CONCURRENT_MODE
			_cancel_timer_ex( &pwdinfo->ap_p2p_switch_timer );
#endif // CONFIG_CONCURRENT_MODE
			rtw_p2p_set_state(pwdinfo, P2P_STATE_NONE);
		}
	}
	#endif
	// add for CONFIG_IEEE80211W, none 11w also can use
	_rtw_spinlock_free(&padapter->security_key_mutex);
	
#ifdef CONFIG_BR_EXT
	_rtw_spinlock_free(&padapter->br_ext_lock);
#endif	// CONFIG_BR_EXT

#ifdef CONFIG_INTEL_WIDI
	rtw_free_intel_widi(padapter);
#endif //CONFIG_INTEL_WIDI

	free_mlme_ext_priv(&padapter->mlmeextpriv);
	
#ifdef CONFIG_TDLS
	//rtw_free_tdls_info(&padapter->tdlsinfo);
#endif //CONFIG_TDLS
	
	rtw_free_cmd_priv(&padapter->cmdpriv);
	
	rtw_free_evt_priv(&padapter->evtpriv);

	rtw_free_mlme_priv(&padapter->mlmepriv);
#if defined(CONFIG_CHECK_BT_HANG) && defined(CONFIG_BT_COEXIST)	
	if (padapter->HalFunc.hal_free_checkbthang_workqueue)
		padapter->HalFunc.hal_free_checkbthang_workqueue(padapter);
#endif	
	//free_io_queue(padapter);
	
	_rtw_free_xmit_priv(&padapter->xmitpriv);
	
	_rtw_free_sta_priv(&padapter->stapriv); //will free bcmc_stainfo here
	
	_rtw_free_recv_priv(&padapter->recvpriv);	

	rtw_free_pwrctrl_priv(padapter);

	//rtw_mfree((void *)padapter, sizeof (padapter));

#ifdef CONFIG_DRVEXT_MODULE
	free_drvext(&padapter->drvextpriv);
#endif

	rtw_hal_free_data(padapter);

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("<==rtw_free_drv_sw\n"));

	//free the old_pnetdev
	if(padapter->rereg_nd_name_priv.old_pnetdev) {
		free_netdev(padapter->rereg_nd_name_priv.old_pnetdev);
		padapter->rereg_nd_name_priv.old_pnetdev = NULL;
	}	

	// clear pbuddy_adapter to avoid access wrong pointer.
	if(padapter->pbuddy_adapter != NULL) {
		padapter->pbuddy_adapter->pbuddy_adapter = NULL;
	}

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("-rtw_free_drv_sw\n"));

	return _SUCCESS;
	
}

#ifdef CONFIG_CONCURRENT_MODE
int _netdev_if2_open(struct net_device *pnetdev)
{	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	_adapter *primary_padapter = padapter->pbuddy_adapter;

	DBG_871X("+871x_drv - if2_open, bup=%d\n", padapter->bup);

	if(primary_padapter->bup == _FALSE || primary_padapter->hw_init_completed == _FALSE)
	{
		_netdev_open(primary_padapter->pnetdev);
	}

	if(padapter->bup == _FALSE && primary_padapter->bup == _TRUE && 
		primary_padapter->hw_init_completed == _TRUE)
	{
		int i;
	
		padapter->bDriverStopped = _FALSE;
	 	padapter->bSurpriseRemoved = _FALSE;	 
		padapter->bCardDisableWOHSM = _FALSE;
		
		padapter->bFWReady = primary_padapter->bFWReady;
		
		//if (init_mlme_ext_priv(padapter) == _FAIL)
		//	goto netdev_if2_open_error;	


		if(rtw_start_drv_threads(padapter) == _FAIL)
		{
			goto netdev_if2_open_error;		
		}


		if(padapter->intf_start)
		{
			padapter->intf_start(padapter);
		}
	
		rtw_proc_init_one(pnetdev);


#ifdef CONFIG_IOCTL_CFG80211
		rtw_cfg80211_init_wiphy(padapter);
#endif

		padapter->bup = _TRUE;
		
	}

	padapter->net_closed = _FALSE;

	_set_timer(&padapter->mlmepriv.dynamic_chk_timer, 2000);

	if(!rtw_netif_queue_stopped(pnetdev))
		rtw_netif_start_queue(pnetdev);
	else
		rtw_netif_wake_queue(pnetdev);

	DBG_871X("-871x_drv - if2_open, bup=%d\n", padapter->bup);
	return 0;

netdev_if2_open_error:

	padapter->bup = _FALSE;
	
	netif_carrier_off(pnetdev);	
	rtw_netif_stop_queue(pnetdev);
	
	return (-1);	

}

int netdev_if2_open(struct net_device *pnetdev)
{
	int ret;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	
	_enter_critical_mutex(&(adapter_to_dvobj(padapter)->hw_init_mutex), NULL);
	ret = _netdev_if2_open(pnetdev);
	_exit_critical_mutex(&(adapter_to_dvobj(padapter)->hw_init_mutex), NULL);
	return ret;
}

static int netdev_if2_close(struct net_device *pnetdev)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);

	padapter->net_closed = _TRUE;

	if(pnetdev)   
	{
		if (!rtw_netif_queue_stopped(pnetdev))
			rtw_netif_stop_queue(pnetdev);
	}

#ifdef CONFIG_IOCTL_CFG80211
	rtw_scan_abort(padapter);
	wdev_to_priv(padapter->rtw_wdev)->bandroid_scan = _FALSE;
#endif

	return 0;
}

#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,29))
static const struct net_device_ops rtw_netdev_if2_ops = {
	.ndo_open = netdev_if2_open,
        .ndo_stop = netdev_if2_close,
        .ndo_start_xmit = rtw_xmit_entry,
        .ndo_set_mac_address = rtw_net_set_mac_address,
        .ndo_get_stats = rtw_net_get_stats,
        .ndo_do_ioctl = rtw_ioctl,
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
	.ndo_select_queue	= rtw_select_queue,
#endif	
};
#endif


#ifdef CONFIG_USB_HCI
	#include <usb_hal.h>
#endif
#ifdef CONFIG_SDIO_HCI
	#include <sdio_hal.h>
#endif
_adapter *rtw_drv_if2_init(_adapter *primary_padapter, void (*set_intf_ops)(struct _io_ops *pops))
{
	int res = _FAIL;
	struct net_device *pnetdev = NULL;
	_adapter *padapter = NULL;
	struct dvobj_priv *pdvobjpriv;
	u8 mac[ETH_ALEN];

	/****** init netdev ******/
	pnetdev = rtw_init_netdev(NULL);
	if (!pnetdev) 
		goto error_rtw_drv_if2_init;

#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,29))
	DBG_871X("register rtw_netdev_if2_ops to netdev_ops\n");
	pnetdev->netdev_ops = &rtw_netdev_if2_ops;
#else
	pnetdev->open = netdev_if2_open;
	pnetdev->stop = netdev_if2_close;
#endif
	
#ifdef CONFIG_NO_WIRELESS_HANDLERS
	pnetdev->wireless_handlers = NULL;
#endif

	/****** init adapter ******/
	padapter = rtw_netdev_priv(pnetdev);
	_rtw_memcpy(padapter, primary_padapter, sizeof(_adapter));

	//
	padapter->bup = _FALSE;
	padapter->net_closed = _TRUE;
	padapter->hw_init_completed = _FALSE;
	padapter->dir_dev = NULL;
	padapter->dir_odm = NULL;

	//set adapter_type/iface type
	padapter->isprimary = _FALSE;
	padapter->adapter_type = SECONDARY_ADAPTER;
	padapter->pbuddy_adapter = primary_padapter;
	padapter->iface_id = IFACE_ID1;
#ifndef CONFIG_HWPORT_SWAP			//Port0 -> Pri , Port1 -> Sec
	padapter->iface_type = IFACE_PORT1;
#else	
	padapter->iface_type = IFACE_PORT0;
#endif  //CONFIG_HWPORT_SWAP
	//
	padapter->pnetdev = pnetdev;

	/****** setup dvobj ******/
	pdvobjpriv = adapter_to_dvobj(padapter);
	pdvobjpriv->if2 = padapter;
	pdvobjpriv->padapters[pdvobjpriv->iface_nums++] = padapter;

	SET_NETDEV_DEV(pnetdev, dvobj_to_dev(pdvobjpriv));
	#ifdef CONFIG_IOCTL_CFG80211
	rtw_wdev_alloc(padapter, dvobj_to_dev(pdvobjpriv));
	#endif //CONFIG_IOCTL_CFG80211

	//set interface_type/chip_type/HardwareType
	padapter->interface_type = primary_padapter->interface_type;		
	padapter->chip_type = primary_padapter->chip_type;
	padapter->HardwareType = primary_padapter->HardwareType;
	
	//step 2. hook HalFunc, allocate HalData
	hal_set_hal_ops(padapter);
	
	padapter->HalFunc.inirp_init = NULL;
	padapter->HalFunc.inirp_deinit = NULL;

	//	
	padapter->intf_start = primary_padapter->intf_start;
	padapter->intf_stop = primary_padapter->intf_stop;

	//step init_io_priv
	if ((rtw_init_io_priv(padapter, set_intf_ops)) == _FAIL) {
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,(" \n Can't init io_reqs\n"));
	}

	//step read_chip_version
	rtw_hal_read_chip_version(padapter);

	//step usb endpoint mapping
	rtw_hal_chip_configure(padapter);


	//init drv data
	if(rtw_init_drv_sw(padapter)!= _SUCCESS)
		goto error_rtw_drv_if2_init;

	//get mac address from primary_padapter
	_rtw_memcpy(mac, primary_padapter->eeprompriv.mac_addr, ETH_ALEN);
	
	if (((mac[0]==0xff) &&(mac[1]==0xff) && (mac[2]==0xff) &&
	     (mac[3]==0xff) && (mac[4]==0xff) &&(mac[5]==0xff)) ||
	    ((mac[0]==0x0) && (mac[1]==0x0) && (mac[2]==0x0) &&
	     (mac[3]==0x0) && (mac[4]==0x0) &&(mac[5]==0x0)))
	{
		mac[0] = 0x00;
		mac[1] = 0xe0;
		mac[2] = 0x4c;
		mac[3] = 0x87;
		mac[4] = 0x11;
		mac[5] = 0x22;
	}
	else
	{
		//If the BIT1 is 0, the address is universally administered. 
		//If it is 1, the address is locally administered
		mac[0] |= BIT(1); // locally administered
		
	}

	_rtw_memcpy(padapter->eeprompriv.mac_addr, mac, ETH_ALEN);
	rtw_init_wifidirect_addrs(padapter, padapter->eeprompriv.mac_addr, padapter->eeprompriv.mac_addr);

	primary_padapter->pbuddy_adapter = padapter;

	res = _SUCCESS;

	return padapter;

		
error_rtw_drv_if2_init:

	if(padapter)
		rtw_free_drv_sw(padapter);	

	if (pnetdev)
		rtw_free_netdev(pnetdev);

	return NULL;
	
}

void rtw_drv_if2_free(_adapter *if2)
{
	_adapter *padapter = if2;
	struct net_device *pnetdev = NULL;

	if (padapter == NULL)
		return;

	pnetdev = padapter->pnetdev;

#ifdef CONFIG_IOCTL_CFG80211
	rtw_wdev_free(padapter->rtw_wdev);
#endif /* CONFIG_IOCTL_CFG80211 */

	
	rtw_free_drv_sw(padapter);

	rtw_free_netdev(pnetdev);

}

void rtw_drv_if2_stop(_adapter *if2)
{
	_adapter *padapter = if2;
	//struct net_device *pnetdev = NULL;

	if (padapter == NULL)
		return;
/*
	pnetdev = padapter->pnetdev;

	if (pnetdev) {
		unregister_netdev(pnetdev); //will call netdev_close()
		rtw_proc_remove_one(pnetdev);
	}
*/
	rtw_cancel_all_timer(padapter);

	if (padapter->bup == _TRUE) {
		padapter->bDriverStopped = _TRUE;
		#ifdef CONFIG_XMIT_ACK
		if (padapter->xmitpriv.ack_tx)
			rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_DRV_STOP);
		#endif
		
		if(padapter->intf_stop)
		{
			padapter->intf_stop(padapter);
		}

		rtw_stop_drv_threads(padapter);
		
		padapter->bup = _FALSE;
	}
/*
	#ifdef CONFIG_IOCTL_CFG80211
	rtw_wdev_unregister(padapter->rtw_wdev);
	#endif
*/
}
#endif //end of CONFIG_CONCURRENT_MODE

#ifdef CONFIG_BR_EXT
void netdev_br_init(struct net_device *netdev)
{
	_adapter *adapter = (_adapter *)rtw_netdev_priv(netdev);
	
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35))
	rcu_read_lock();
#endif	// (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35))

	//if(check_fwstate(pmlmepriv, WIFI_STATION_STATE|WIFI_ADHOC_STATE) == _TRUE)
	{
		//struct net_bridge	*br = netdev->br_port->br;//->dev->dev_addr;
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
		if (netdev->br_port) 
#else   // (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
		if (rcu_dereference(adapter->pnetdev->rx_handler_data))
#endif  // (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
		{
			struct net_device *br_netdev;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
			br_netdev = dev_get_by_name(CONFIG_BR_EXT_BRNAME);
#else	// (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
			struct net *devnet = NULL;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26))
			devnet = netdev->nd_net;
#else	// (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26))
			devnet = dev_net(netdev);
#endif	// (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26))

			br_netdev = dev_get_by_name(devnet, CONFIG_BR_EXT_BRNAME);
#endif	// (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))

			if (br_netdev) {
				memcpy(adapter->br_mac, br_netdev->dev_addr, ETH_ALEN);
				dev_put(br_netdev);
			} else
				DBG_871X("%s()-%d: dev_get_by_name(%s) failed!", __FUNCTION__, __LINE__, CONFIG_BR_EXT_BRNAME);
		}
		
		adapter->ethBrExtInfo.addPPPoETag = 1;
	}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35))
	rcu_read_unlock();
#endif	// (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35))
}
#endif //CONFIG_BR_EXT

static int _rtw_drv_register_netdev(_adapter *padapter, char *name)
{
	int ret = _SUCCESS;
	struct net_device *pnetdev = padapter->pnetdev;

	/* alloc netdev name */
	rtw_init_netdev_name(pnetdev, name);

	_rtw_memcpy(pnetdev->dev_addr, padapter->eeprompriv.mac_addr, ETH_ALEN);

	/* Tell the network stack we exist */
	if (register_netdev(pnetdev) != 0) {
		DBG_871X(FUNC_NDEV_FMT "Failed!\n", FUNC_NDEV_ARG(pnetdev));
		ret = _FAIL;
		goto error_register_netdev;
	}

	DBG_871X("%s, MAC Address (if%d) = " MAC_FMT "\n", __FUNCTION__, (padapter->iface_id+1), MAC_ARG(pnetdev->dev_addr));

	return ret;

error_register_netdev:

	if(padapter->iface_id > IFACE_ID0)
	{
		rtw_free_drv_sw(padapter);

		rtw_free_netdev(pnetdev);
	}

	return ret;
}

int rtw_drv_register_netdev(_adapter *if1)
{
	int i, status = _SUCCESS;
	struct dvobj_priv *dvobj = if1->dvobj;

	if(dvobj->iface_nums < IFACE_ID_MAX)
	{
		for(i=0; i<dvobj->iface_nums; i++)
		{
			_adapter *padapter = dvobj->padapters[i];

			if(padapter)
			{
				char *name;

				if(padapter->iface_id == IFACE_ID0)
					name = if1->registrypriv.ifname;
				else if(padapter->iface_id == IFACE_ID1)
					name = if1->registrypriv.if2name;
				else
					name = "wlan%d";

				if((status = _rtw_drv_register_netdev(padapter, name)) != _SUCCESS) {
					break;
				}
			}
		}
	}

	return status;
}

int _netdev_open(struct net_device *pnetdev)
{
	uint status;	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("+871x_drv - dev_open\n"));
	DBG_871X("+871x_drv - drv_open, bup=%d\n", padapter->bup);

	if(pwrctrlpriv->ps_flag == _TRUE){
		padapter->net_closed = _FALSE;
		goto netdev_open_normal_process;
	}

	if(padapter->bup == _FALSE)
	{
		padapter->bDriverStopped = _FALSE;
	 	padapter->bSurpriseRemoved = _FALSE;
		padapter->bCardDisableWOHSM = _FALSE;

		status = rtw_hal_init(padapter);
		if (status ==_FAIL)
		{			
			RT_TRACE(_module_os_intfs_c_,_drv_err_,("rtl871x_hal_init(): Can't init h/w!\n"));
			goto netdev_open_error;
		}
		
		DBG_871X("MAC Address = "MAC_FMT"\n", MAC_ARG(pnetdev->dev_addr));

#ifdef CONFIG_RF_GAIN_OFFSET
		rtw_bb_rf_gain_offset(padapter);	
#endif //CONFIG_RF_GAIN_OFFSET

		status=rtw_start_drv_threads(padapter);
		if(status ==_FAIL)
		{			
			DBG_871X("Initialize driver software resource Failed!\n");
			goto netdev_open_error;			
		}

#ifdef CONFIG_DRVEXT_MODULE
		init_drvext(padapter);
#endif

		if(padapter->intf_start)
		{
			padapter->intf_start(padapter);
		}

#ifndef RTK_DMP_PLATFORM
		rtw_proc_init_one(pnetdev);
#endif

#ifdef CONFIG_IOCTL_CFG80211
		rtw_cfg80211_init_wiphy(padapter);
#endif

		rtw_led_control(padapter, LED_CTL_NO_LINK);

		padapter->bup = _TRUE;
		
		pwrctrlpriv->bips_processing = _FALSE;	
	}
	padapter->net_closed = _FALSE;

	_set_timer(&padapter->mlmepriv.dynamic_chk_timer, 2000);

#ifdef CONFIG_DETECT_C2H_BY_POLLING
	_set_timer(&padapter->mlmepriv.event_polling_timer, 200);
#endif

	rtw_set_pwr_state_check_timer(pwrctrlpriv);

	//netif_carrier_on(pnetdev);//call this func when rtw_joinbss_event_callback return success
	if(!rtw_netif_queue_stopped(pnetdev))
		rtw_netif_start_queue(pnetdev);
	else
		rtw_netif_wake_queue(pnetdev);

#ifdef CONFIG_BR_EXT
	netdev_br_init(pnetdev);
#endif	// CONFIG_BR_EXT

netdev_open_normal_process:

	#ifdef CONFIG_CONCURRENT_MODE
	{
		_adapter *sec_adapter = padapter->pbuddy_adapter;
		if(sec_adapter && (sec_adapter->bup == _FALSE))
			_netdev_if2_open(sec_adapter->pnetdev);
	}
	#endif

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("-871x_drv - dev_open\n"));
	DBG_871X("-871x_drv - drv_open, bup=%d\n", padapter->bup);
		
	return 0;
	
netdev_open_error:

	padapter->bup = _FALSE;
	
	netif_carrier_off(pnetdev);	
	rtw_netif_stop_queue(pnetdev);
	
	RT_TRACE(_module_os_intfs_c_,_drv_err_,("-871x_drv - dev_open, fail!\n"));
	DBG_871X("-871x_drv - drv_open fail, bup=%d\n", padapter->bup);
	
	return (-1);
	
}

int netdev_open(struct net_device *pnetdev)
{
	int ret;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	
	_enter_critical_mutex(&(adapter_to_dvobj(padapter)->hw_init_mutex), NULL);
	ret = _netdev_open(pnetdev);
	_exit_critical_mutex(&(adapter_to_dvobj(padapter)->hw_init_mutex), NULL);

	return ret;
}

#ifdef CONFIG_IPS
int  ips_netdrv_open(_adapter *padapter)
{
	int status = _SUCCESS;
	padapter->net_closed = _FALSE;
	DBG_871X("===> %s.........\n",__FUNCTION__);


	padapter->bDriverStopped = _FALSE;
	padapter->bCardDisableWOHSM = _FALSE;
	//padapter->bup = _TRUE;

	status = rtw_hal_init(padapter);
	if (status ==_FAIL)
	{
		RT_TRACE(_module_os_intfs_c_,_drv_err_,("ips_netdrv_open(): Can't init h/w!\n"));
		goto netdev_open_error;
	}
	
#ifdef CONFIG_RF_GAIN_OFFSET
	rtw_bb_rf_gain_offset(padapter);	
#endif //CONFIG_RF_GAIN_OFFSET

	if(padapter->intf_start)
	{
		padapter->intf_start(padapter);
	}

	rtw_set_pwr_state_check_timer(adapter_to_pwrctl(padapter));
  	_set_timer(&padapter->mlmepriv.dynamic_chk_timer,5000);

	 return _SUCCESS;

netdev_open_error:
	//padapter->bup = _FALSE;
	DBG_871X("-ips_netdrv_open - drv_open failure, bup=%d\n", padapter->bup);

	return _FAIL;
}


int rtw_ips_pwr_up(_adapter *padapter)
{	
	int result;
	u32 start_time = rtw_get_current_time();
	DBG_871X("===>  rtw_ips_pwr_up..............\n");
	rtw_reset_drv_sw(padapter);

	result = ips_netdrv_open(padapter);

	rtw_led_control(padapter, LED_CTL_NO_LINK);
	
 	DBG_871X("<===  rtw_ips_pwr_up.............. in %dms\n", rtw_get_passing_time_ms(start_time));
	return result;

}

void rtw_ips_pwr_down(_adapter *padapter)
{
	u32 start_time = rtw_get_current_time();
	DBG_871X("===> rtw_ips_pwr_down...................\n");

	padapter->bCardDisableWOHSM = _TRUE;
	padapter->net_closed = _TRUE;

	rtw_led_control(padapter, LED_CTL_POWER_OFF);
	
	rtw_ips_dev_unload(padapter);
	padapter->bCardDisableWOHSM = _FALSE;
	DBG_871X("<=== rtw_ips_pwr_down..................... in %dms\n", rtw_get_passing_time_ms(start_time));
}
#endif
void rtw_ips_dev_unload(_adapter *padapter)
{
	struct net_device *pnetdev= (struct net_device*)padapter->pnetdev;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	DBG_871X("====> %s...\n",__FUNCTION__);

	rtw_hal_set_hwreg(padapter, HW_VAR_FIFO_CLEARN_UP, 0);

	if(padapter->intf_stop)
	{
		padapter->intf_stop(padapter);
	}

	//s5.
	if(padapter->bSurpriseRemoved == _FALSE)
	{
		rtw_hal_deinit(padapter);
	}

}

#ifdef CONFIG_RF_GAIN_OFFSET
void rtw_bb_rf_gain_offset(_adapter *padapter)
{
	u8      value = padapter->eeprompriv.EEPROMRFGainOffset;
	u8      tmp = 0x3e;
	u32     res;

	DBG_871X("+%s value: 0x%02x+\n", __func__, value);

	if (value & RF_GAIN_OFFSET_ON) {
		//DBG_871X("Offset RF Gain.\n");
		//DBG_871X("Offset RF Gain.  padapter->eeprompriv.EEPROMRFGainVal=0x%x\n",padapter->eeprompriv.EEPROMRFGainVal);
		if(padapter->eeprompriv.EEPROMRFGainVal != 0xff){
#ifdef CONFIG_RTL8723A
			res = rtw_hal_read_rfreg(padapter, RF_PATH_A, 0xd, 0xffffffff);
			//DBG_871X("Offset RF Gain. reg 0xd=0x%x\n",res);
			res &= 0xfff87fff;

			res |= (padapter->eeprompriv.EEPROMRFGainVal & 0x0f)<< 15;
			//DBG_871X("Offset RF Gain.    reg 0xd=0x%x\n",res);

			rtw_hal_write_rfreg(padapter, RF_PATH_A, REG_RF_BB_GAIN_OFFSET, RF_GAIN_OFFSET_MASK, res);

			res = rtw_hal_read_rfreg(padapter, RF_PATH_A, 0xe, 0xffffffff);
			DBG_871X("Offset RF Gain. reg 0xe=0x%x\n",res);
			res &= 0xfffffff0;

			res |= (padapter->eeprompriv.EEPROMRFGainVal & 0x0f);
			//DBG_871X("Offset RF Gain.    reg 0xe=0x%x\n",res);

			rtw_hal_write_rfreg(padapter, RF_PATH_A, REG_RF_BB_GAIN_OFFSET, RF_GAIN_OFFSET_MASK, res);
#else
			res = rtw_hal_read_rfreg(padapter, RF_PATH_A, REG_RF_BB_GAIN_OFFSET, 0xffffffff);
			DBG_871X("REG_RF_BB_GAIN_OFFSET=%x \n",res);
			res &= 0xfff87fff;
			res |= (padapter->eeprompriv.EEPROMRFGainVal & 0x0f)<< 15;
			DBG_871X("write REG_RF_BB_GAIN_OFFSET=%x \n",res);
			rtw_hal_write_rfreg(padapter, RF_PATH_A, REG_RF_BB_GAIN_OFFSET, RF_GAIN_OFFSET_MASK, res);
#endif
		}
		else
		{
			//DBG_871X("Offset RF Gain.  padapter->eeprompriv.EEPROMRFGainVal=0x%x  != 0xff, didn't run Kfree\n",padapter->eeprompriv.EEPROMRFGainVal);
		}
	} else {
		//DBG_871X("Using the default RF gain.\n");
	}

}
#endif //CONFIG_RF_GAIN_OFFSET

int pm_netdev_open(struct net_device *pnetdev,u8 bnormal)
{
	int status;


	if (_TRUE == bnormal)
		status = netdev_open(pnetdev);
#ifdef CONFIG_IPS
	else
		status =  (_SUCCESS == ips_netdrv_open((_adapter *)rtw_netdev_priv(pnetdev)))?(0):(-1);
#endif

	return status;
}

static int netdev_close(struct net_device *pnetdev)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("+871x_drv - drv_close\n"));	

	if(adapter_to_pwrctl(padapter)->bInternalAutoSuspend == _TRUE)
	{
		//rtw_pwr_wakeup(padapter);
		if(adapter_to_pwrctl(padapter)->rf_pwrstate == rf_off)
			adapter_to_pwrctl(padapter)->ps_flag = _TRUE;
	}
	padapter->net_closed = _TRUE;

/*	if(!padapter->hw_init_completed)
	{
		DBG_871X("(1)871x_drv - drv_close, bup=%d, hw_init_completed=%d\n", padapter->bup, padapter->hw_init_completed);

		padapter->bDriverStopped = _TRUE;

		rtw_dev_unload(padapter);
	}
	else*/
	if(adapter_to_pwrctl(padapter)->rf_pwrstate == rf_on){
		DBG_871X("(2)871x_drv - drv_close, bup=%d, hw_init_completed=%d\n", padapter->bup, padapter->hw_init_completed);

		//s1.
		if(pnetdev)   
		{
			if (!rtw_netif_queue_stopped(pnetdev))
				rtw_netif_stop_queue(pnetdev);
		}

#ifndef CONFIG_ANDROID
		//s2.
		LeaveAllPowerSaveMode(padapter);
		rtw_disassoc_cmd(padapter, 500, _FALSE);
		//s2-2.  indicate disconnect to os
		rtw_indicate_disconnect(padapter);
		//s2-3. 
		rtw_free_assoc_resources(padapter, 1);
		//s2-4.
		rtw_free_network_queue(padapter,_TRUE);
#endif
		// Close LED
		rtw_led_control(padapter, LED_CTL_POWER_OFF);
	}

#ifdef CONFIG_BR_EXT
	//if (OPMODE & (WIFI_STATION_STATE | WIFI_ADHOC_STATE)) 
	{
		//void nat25_db_cleanup(_adapter *priv);
		nat25_db_cleanup(padapter);
	}
#endif	// CONFIG_BR_EXT

#ifdef CONFIG_P2P
	rtw_p2p_enable(padapter, P2P_ROLE_DISABLE);
#endif //CONFIG_P2P

#ifdef CONFIG_IOCTL_CFG80211
	rtw_scan_abort(padapter);
	wdev_to_priv(padapter->rtw_wdev)->bandroid_scan = _FALSE;
	padapter->rtw_wdev->iftype = NL80211_IFTYPE_MONITOR; //set this at the end
#endif //CONFIG_IOCTL_CFG80211

#ifdef CONFIG_WAPI_SUPPORT
	rtw_wapi_disable_tx(padapter);
#endif

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("-871x_drv - drv_close\n"));
	DBG_871X("-871x_drv - drv_close, bup=%d\n", padapter->bup);

	return 0;
	
}

void rtw_ndev_destructor(struct net_device *ndev)
{
	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

#ifdef CONFIG_IOCTL_CFG80211
	if (ndev->ieee80211_ptr)
		rtw_mfree((u8 *)ndev->ieee80211_ptr, sizeof(struct wireless_dev));
#endif
	free_netdev(ndev);
}

#ifdef CONFIG_ARP_KEEP_ALIVE
struct route_info {
    struct in_addr dst_addr;
    struct in_addr src_addr;
    struct in_addr gateway;
    unsigned int dev_index;
};

static void parse_routes(struct nlmsghdr *nl_hdr, struct route_info *rt_info)
{
    struct rtmsg *rt_msg;
    struct rtattr *rt_attr;
    int rt_len;

    rt_msg = (struct rtmsg *) NLMSG_DATA(nl_hdr);
    if ((rt_msg->rtm_family != AF_INET) || (rt_msg->rtm_table != RT_TABLE_MAIN))
        return;

    rt_attr = (struct rtattr *) RTM_RTA(rt_msg);
    rt_len = RTM_PAYLOAD(nl_hdr);

    for (; RTA_OK(rt_attr, rt_len); rt_attr = RTA_NEXT(rt_attr, rt_len)) 
	{
        switch (rt_attr->rta_type) {
        case RTA_OIF:
		rt_info->dev_index = *(int *) RTA_DATA(rt_attr);
            break;
        case RTA_GATEWAY:
            rt_info->gateway.s_addr = *(u_int *) RTA_DATA(rt_attr);
            break;
        case RTA_PREFSRC:
            rt_info->src_addr.s_addr = *(u_int *) RTA_DATA(rt_attr);
            break;
        case RTA_DST:
            rt_info->dst_addr.s_addr = *(u_int *) RTA_DATA(rt_attr);
            break;
        }
    }
}

static int route_dump(u32 *gw_addr ,int* gw_index)
{
	int err = 0;
	struct socket *sock;
	struct {
		struct nlmsghdr nlh;
		struct rtgenmsg g;
	} req;
	struct msghdr msg;
	struct iovec iov;
	struct sockaddr_nl nladdr;
	mm_segment_t oldfs;
	char *pg;
	int size = 0;

	err = sock_create(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE, &sock);
	if (err)
	{
		printk( ": Could not create a datagram socket, error = %d\n", -ENXIO);
		return err;
	}
	
	memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;

	req.nlh.nlmsg_len = sizeof(req);
	req.nlh.nlmsg_type = RTM_GETROUTE;
	req.nlh.nlmsg_flags = NLM_F_ROOT | NLM_F_MATCH | NLM_F_REQUEST;
	req.nlh.nlmsg_pid = 0;
	req.g.rtgen_family = AF_INET;

	iov.iov_base = &req;
	iov.iov_len = sizeof(req);

	msg.msg_name = &nladdr;
	msg.msg_namelen = sizeof(nladdr);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = MSG_DONTWAIT;

	oldfs = get_fs(); set_fs(KERNEL_DS);
	err = sock_sendmsg(sock, &msg, sizeof(req));
	set_fs(oldfs);

	if (size < 0)
		goto out_sock;

	pg = (char *) __get_free_page(GFP_KERNEL);
	if (pg == NULL) {
		err = -ENOMEM;
		goto out_sock;
	}

#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
restart:
#endif

	for (;;) 
	{
		struct nlmsghdr *h;

		iov.iov_base = pg;
		iov.iov_len = PAGE_SIZE;

		oldfs = get_fs(); set_fs(KERNEL_DS);
		err = sock_recvmsg(sock, &msg, PAGE_SIZE, MSG_DONTWAIT);
		set_fs(oldfs);

		if (err < 0)
			goto out_sock_pg;

		if (msg.msg_flags & MSG_TRUNC) {
			err = -ENOBUFS;
			goto out_sock_pg;
		}

		h = (struct nlmsghdr*) pg;
		
		while (NLMSG_OK(h, err)) 
		{
			struct route_info rt_info;
			if (h->nlmsg_type == NLMSG_DONE) {
				err = 0;
				goto done;
			}

			if (h->nlmsg_type == NLMSG_ERROR) {
				struct nlmsgerr *errm = (struct nlmsgerr*) NLMSG_DATA(h);
				err = errm->error;
				printk( "NLMSG error: %d\n", errm->error);
				goto done;
			}

			if (h->nlmsg_type == RTM_GETROUTE)
			{
				printk( "RTM_GETROUTE: NLMSG: %d\n", h->nlmsg_type);
			}
			if (h->nlmsg_type != RTM_NEWROUTE) {
				printk( "NLMSG: %d\n", h->nlmsg_type);
				err = -EINVAL;
				goto done;
			}

			memset(&rt_info, 0, sizeof(struct route_info));
			parse_routes(h, &rt_info);
			if(!rt_info.dst_addr.s_addr && rt_info.gateway.s_addr && rt_info.dev_index)
			{
				*gw_addr = rt_info.gateway.s_addr;
				*gw_index = rt_info.dev_index;
				 	
			}
			h = NLMSG_NEXT(h, err);
		}

		if (err) 
		{
			printk( "!!!Remnant of size %d %d %d\n", err, h->nlmsg_len, h->nlmsg_type);
			err = -EINVAL;
			break;
		}
	}

done:
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
	if (!err && req.g.rtgen_family == AF_INET) {
		req.g.rtgen_family = AF_INET6;

		iov.iov_base = &req;
		iov.iov_len = sizeof(req);

		msg.msg_name = &nladdr;
		msg.msg_namelen = sizeof(nladdr);
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_flags=MSG_DONTWAIT;

		oldfs = get_fs(); set_fs(KERNEL_DS);
		err = sock_sendmsg(sock, &msg, sizeof(req));
		set_fs(oldfs);

		if (err > 0)
			goto restart;
	}
#endif

out_sock_pg:
	free_page((unsigned long) pg);

out_sock:
	sock_release(sock);
	return err;
}

static int arp_query(unsigned char *haddr, u32 paddr,
             struct net_device *dev)
{
	struct neighbour *neighbor_entry;
	int	ret = 0;

	neighbor_entry = neigh_lookup(&arp_tbl, &paddr, dev);

	if (neighbor_entry != NULL) {
		neighbor_entry->used = jiffies;
		if (neighbor_entry->nud_state & NUD_VALID) {
			_rtw_memcpy(haddr, neighbor_entry->ha, dev->addr_len);
			ret = 1;
		}
		neigh_release(neighbor_entry);
	}
	return ret;
}

static int get_defaultgw(u32 *ip_addr ,char mac[])
{
	int gw_index = 0; // oif device index
	struct net_device *gw_dev = NULL; //oif device
	
	route_dump(ip_addr, &gw_index);

	if( !(*ip_addr) || !gw_index )
	{
		//DBG_871X("No default GW \n");
		return -1;
	}

	gw_dev = dev_get_by_index(&init_net, gw_index);

	if(gw_dev == NULL)
	{
		//DBG_871X("get Oif Device Fail \n");
		return -1;
	}
	
	if(!arp_query(mac, *ip_addr, gw_dev))
	{
		//DBG_871X( "arp query failed\n");
		dev_put(gw_dev);
		return -1;
		
	}
	dev_put(gw_dev);
	
	return 0;
}

int	rtw_gw_addr_query(_adapter *padapter)
{
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	u32 gw_addr = 0; // default gw address
	unsigned char gw_mac[32] = {0}; // default gw mac
	int i;
	int res;

	res = get_defaultgw(&gw_addr, gw_mac);
	if(!res)
	{
		pmlmepriv->gw_ip[0] = gw_addr&0xff;
		pmlmepriv->gw_ip[1] = (gw_addr&0xff00)>>8;
		pmlmepriv->gw_ip[2] = (gw_addr&0xff0000)>>16;
		pmlmepriv->gw_ip[3] = (gw_addr&0xff000000)>>24;
		_rtw_memcpy(pmlmepriv->gw_mac_addr, gw_mac, 6);
		DBG_871X("%s Gateway Mac:\t" MAC_FMT "\n", __FUNCTION__, MAC_ARG(pmlmepriv->gw_mac_addr));
		DBG_871X("%s Gateway IP:\t" IP_FMT "\n", __FUNCTION__, IP_ARG(pmlmepriv->gw_ip));
	}
	else
	{
		//DBG_871X("Get Gateway IP/MAC fail!\n");
	}

	return res;
} 
#endif

int rtw_suspend_free_assoc_resource(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct net_device *pnetdev = padapter->pnetdev;
	struct wifidirect_info*	pwdinfo = &padapter->wdinfo;

	DBG_871X("==> "FUNC_ADPT_FMT" entry....\n", FUNC_ADPT_ARG(padapter));
	
	rtw_cancel_all_timer(padapter);
	if(pnetdev){
		netif_carrier_off(pnetdev);
		rtw_netif_stop_queue(pnetdev);
	}		

	#ifdef CONFIG_LAYER2_ROAMING_RESUME
	if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) && check_fwstate(pmlmepriv, _FW_LINKED) && rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
	{
		DBG_871X("%s %s(" MAC_FMT "), length:%d assoc_ssid.length:%d\n",__FUNCTION__,
				pmlmepriv->cur_network.network.Ssid.Ssid,
				MAC_ARG(pmlmepriv->cur_network.network.MacAddress),
				pmlmepriv->cur_network.network.Ssid.SsidLength,
				pmlmepriv->assoc_ssid.SsidLength);
		rtw_set_roaming(padapter, 1);
	}
	#endif //CONFIG_LAYER2_ROAMING_RESUME

	if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) && check_fwstate(pmlmepriv, _FW_LINKED))
	{	
		rtw_disassoc_cmd(padapter, 0, _FALSE);		
	}
	#ifdef CONFIG_AP_MODE
	else if(check_fwstate(pmlmepriv, WIFI_AP_STATE))	
	{
		rtw_sta_flush(padapter);
	}
	#endif
	if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) ){
		//s2-2.  indicate disconnect to os
		rtw_indicate_disconnect(padapter);
	}
		
	//s2-3.
	rtw_free_assoc_resources(padapter, 1);

	//s2-4.
#ifdef CONFIG_AUTOSUSPEND
	if(is_primary_adapter(padapter) && (!adapter_to_pwrctl(padapter)->bInternalAutoSuspend ))
#endif
	rtw_free_network_queue(padapter, _TRUE);

	if(check_fwstate(pmlmepriv, _FW_UNDER_SURVEY))
		rtw_indicate_scan_done(padapter, 1);
	
	DBG_871X("==> "FUNC_ADPT_FMT" exit....\n", FUNC_ADPT_ARG(padapter));
	return 0;
}
extern void rtw_dev_unload(_adapter *padapter);
int rtw_suspend_common(_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	
	int ret = 0;
	_func_enter_;
	
	rtw_suspend_free_assoc_resource(padapter);

#ifdef CONFIG_CONCURRENT_MODE
	if(rtw_buddy_adapter_up(padapter)){
		rtw_suspend_free_assoc_resource(padapter->pbuddy_adapter);
	}
#endif
	rtw_led_control(padapter, LED_CTL_POWER_OFF);
	
#ifdef CONFIG_CONCURRENT_MODE
	if(rtw_buddy_adapter_up(padapter)){
		rtw_dev_unload(padapter->pbuddy_adapter);
	}
#endif
	rtw_dev_unload(padapter);

exit:

	_func_exit_;
	return ret;
}

int rtw_resume_common(_adapter *padapter)
{
	int ret = 0;
	struct net_device *pnetdev= padapter->pnetdev;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv *mlmepriv = &padapter->mlmepriv;

	_func_enter_;

	#ifdef CONFIG_CONCURRENT_MODE
	rtw_reset_drv_sw(padapter->pbuddy_adapter);
	#endif

	rtw_reset_drv_sw(padapter);
	pwrpriv->bkeepfwalive = _FALSE;

	DBG_871X("bkeepfwalive(%x)\n",pwrpriv->bkeepfwalive);
	if(pm_netdev_open(pnetdev,_TRUE) != 0) {
		DBG_871X("%s ==> pm_netdev_open failed \n",__FUNCTION__);
		ret = -1;
		return ret;
	}

	netif_device_attach(pnetdev);
	netif_carrier_on(pnetdev);

	
	#ifdef CONFIG_CONCURRENT_MODE
	if(rtw_buddy_adapter_up(padapter)){			
		pnetdev = padapter->pbuddy_adapter->pnetdev;				
		
		netif_device_attach(pnetdev);
		netif_carrier_on(pnetdev);	
	}
	#endif

	if (check_fwstate(mlmepriv, WIFI_STATION_STATE)) {
		DBG_871X(FUNC_ADPT_FMT" fwstate:0x%08x - WIFI_STATION_STATE\n", FUNC_ADPT_ARG(padapter), get_fwstate(mlmepriv));

		#ifdef CONFIG_LAYER2_ROAMING_RESUME
		rtw_roaming(padapter, NULL);
		#endif //CONFIG_LAYER2_ROAMING_RESUME
		
	} else if (check_fwstate(mlmepriv, WIFI_AP_STATE)) {
		DBG_871X(FUNC_ADPT_FMT" fwstate:0x%08x - WIFI_AP_STATE\n", FUNC_ADPT_ARG(padapter), get_fwstate(mlmepriv));
		rtw_ap_restore_network(padapter);
	} else if (check_fwstate(mlmepriv, WIFI_ADHOC_STATE)) {
		DBG_871X(FUNC_ADPT_FMT" fwstate:0x%08x - WIFI_ADHOC_STATE\n", FUNC_ADPT_ARG(padapter), get_fwstate(mlmepriv));
	} else {
		DBG_871X(FUNC_ADPT_FMT" fwstate:0x%08x - ???\n", FUNC_ADPT_ARG(padapter), get_fwstate(mlmepriv));
	}

	#ifdef CONFIG_CONCURRENT_MODE
	if(rtw_buddy_adapter_up(padapter))
	{
		padapter = padapter->pbuddy_adapter;
		mlmepriv = &padapter->mlmepriv;	
		if (check_fwstate(mlmepriv, WIFI_STATION_STATE)) {
			DBG_871X(FUNC_ADPT_FMT" fwstate:0x%08x - WIFI_STATION_STATE\n", FUNC_ADPT_ARG(padapter), get_fwstate(mlmepriv));

			#ifdef CONFIG_LAYER2_ROAMING_RESUME
			rtw_roaming(padapter, NULL);
			#endif //CONFIG_LAYER2_ROAMING_RESUME
		
		} else if (check_fwstate(mlmepriv, WIFI_AP_STATE)) {
			DBG_871X(FUNC_ADPT_FMT" fwstate:0x%08x - WIFI_AP_STATE\n", FUNC_ADPT_ARG(padapter), get_fwstate(mlmepriv));
			rtw_ap_restore_network(padapter);
		} else if (check_fwstate(mlmepriv, WIFI_ADHOC_STATE)) {
			DBG_871X(FUNC_ADPT_FMT" fwstate:0x%08x - WIFI_ADHOC_STATE\n", FUNC_ADPT_ARG(padapter), get_fwstate(mlmepriv));
		} else {
			DBG_871X(FUNC_ADPT_FMT" fwstate:0x%08x - ???\n", FUNC_ADPT_ARG(padapter), get_fwstate(mlmepriv));
		}
	}
	#endif
	
	_func_exit_;
	return ret;
}
