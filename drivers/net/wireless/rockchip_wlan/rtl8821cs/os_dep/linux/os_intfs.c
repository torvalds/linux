/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2019 Realtek Corporation.
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
#define _OS_INTFS_C_

#include <drv_types.h>
#include <hal_data.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek Wireless Lan Driver");
MODULE_AUTHOR("Realtek Semiconductor Corp.");
MODULE_VERSION(DRIVERVERSION);

/* module param defaults */
int rtw_chip_version = 0x00;
int rtw_rfintfs = HWPI;
int rtw_lbkmode = 0;/* RTL8712_AIR_TRX; */
#ifdef DBG_LA_MODE
int rtw_la_mode_en=0;
module_param(rtw_la_mode_en, int, 0644);
#endif
int rtw_network_mode = Ndis802_11IBSS;/* Ndis802_11Infrastructure; */ /* infra, ad-hoc, auto */
/* NDIS_802_11_SSID	ssid; */
int rtw_channel = 1;/* ad-hoc support requirement */
int rtw_wireless_mode = WIRELESS_MODE_MAX;
module_param(rtw_wireless_mode, int, 0644);
int rtw_vrtl_carrier_sense = AUTO_VCS;
int rtw_vcs_type = RTS_CTS;
int rtw_rts_thresh = 2347;
int rtw_frag_thresh = 2346;
int rtw_preamble = PREAMBLE_LONG;/* long, short, auto */
int rtw_scan_mode = 1;/* active, passive */
/* int smart_ps = 1; */
#ifdef CONFIG_POWER_SAVING
	/* IPS configuration */
	int rtw_ips_mode = RTW_IPS_MODE;

	/* LPS configuration */
/* RTW_LPS_MODE=0:disable, 1:LPS , 2:LPS with clock gating, 3: power gating */
#if (RTW_LPS_MODE > 0)
	int rtw_power_mgnt = PS_MODE_MAX;

	#ifdef CONFIG_USB_HCI
		int rtw_lps_level = LPS_NORMAL; /*USB default LPS level*/
	#else /*SDIO,PCIE*/
		int rtw_lps_level = (RTW_LPS_MODE - 1);
	#endif/*CONFIG_USB_HCI*/
#else
	int rtw_power_mgnt = PS_MODE_ACTIVE;
	int rtw_lps_level = LPS_NORMAL;
#endif

	int rtw_lps_chk_by_tp = 1;

	/* WOW LPS configuration */
#ifdef CONFIG_WOWLAN
/* RTW_WOW_LPS_MODE=0:disable, 1:LPS , 2:LPS with clock gating, 3: power gating */
#if (RTW_WOW_LPS_MODE > 0)
	int rtw_wow_power_mgnt = PS_MODE_MAX;
	int rtw_wow_lps_level = (RTW_WOW_LPS_MODE - 1);
#else
	int rtw_wow_power_mgnt = PS_MODE_ACTIVE;
	int rtw_wow_lps_level = LPS_NORMAL;
#endif	
#endif /* CONFIG_WOWLAN */

#else /* !CONFIG_POWER_SAVING */
	int rtw_ips_mode = IPS_NONE;
	int rtw_power_mgnt = PS_MODE_ACTIVE;
	int rtw_lps_level = LPS_NORMAL;
	int rtw_lps_chk_by_tp = 0;
#ifdef CONFIG_WOWLAN
	int rtw_wow_power_mgnt = PS_MODE_ACTIVE;
	int rtw_wow_lps_level = LPS_NORMAL;
#endif /* CONFIG_WOWLAN */
#endif /* CONFIG_POWER_SAVING */

#ifdef CONFIG_NARROWBAND_SUPPORTING
int rtw_nb_config = CONFIG_NB_VALUE;
module_param(rtw_nb_config, int, 0644);
MODULE_PARM_DESC(rtw_nb_config, "5M/10M/Normal bandwidth configuration");
#endif

module_param(rtw_ips_mode, int, 0644);
MODULE_PARM_DESC(rtw_ips_mode, "The default IPS mode");

module_param(rtw_lps_level, int, 0644);
MODULE_PARM_DESC(rtw_lps_level, "The default LPS level");

#ifdef CONFIG_LPS_1T1R
int rtw_lps_1t1r = RTW_LPS_1T1R;
module_param(rtw_lps_1t1r, int, 0644);
MODULE_PARM_DESC(rtw_lps_1t1r, "The default LPS 1T1R setting");
#endif

module_param(rtw_lps_chk_by_tp, int, 0644);

#ifdef CONFIG_WOWLAN
module_param(rtw_wow_power_mgnt, int, 0644);
MODULE_PARM_DESC(rtw_wow_power_mgnt, "The default WOW LPS mode");
module_param(rtw_wow_lps_level, int, 0644);
MODULE_PARM_DESC(rtw_wow_lps_level, "The default WOW LPS level");
#ifdef CONFIG_LPS_1T1R
int rtw_wow_lps_1t1r = RTW_WOW_LPS_1T1R;
module_param(rtw_wow_lps_1t1r, int, 0644);
MODULE_PARM_DESC(rtw_wow_lps_1t1r, "The default WOW LPS 1T1R setting");
#endif
#endif /* CONFIG_WOWLAN */

/* LPS: 
 * rtw_smart_ps = 0 => TX: pwr bit = 1, RX: PS_Poll
 * rtw_smart_ps = 1 => TX: pwr bit = 0, RX: PS_Poll
 * rtw_smart_ps = 2 => TX: pwr bit = 0, RX: NullData with pwr bit = 0
*/
int rtw_smart_ps = 2;

int rtw_max_bss_cnt = 0;
module_param(rtw_max_bss_cnt, int, 0644);
#ifdef CONFIG_WMMPS_STA	
/* WMMPS: 
 * rtw_smart_ps = 0 => Only for fw test
 * rtw_smart_ps = 1 => Refer to Beacon's TIM Bitmap
 * rtw_smart_ps = 2 => Don't refer to Beacon's TIM Bitmap
*/
int rtw_wmm_smart_ps = 2;
#endif /* CONFIG_WMMPS_STA */

int rtw_check_fw_ps = 1;

#ifdef CONFIG_TX_EARLY_MODE
int rtw_early_mode = 1;
#endif

int rtw_usb_rxagg_mode = 2;/* RX_AGG_DMA=1, RX_AGG_USB=2 */
module_param(rtw_usb_rxagg_mode, int, 0644);

int rtw_dynamic_agg_enable = 1;
module_param(rtw_dynamic_agg_enable, int, 0644);

/* set log level when inserting driver module, default log level is _DRV_INFO_ = 4,
* please refer to "How_to_set_driver_debug_log_level.doc" to set the available level.
*/
#ifdef CONFIG_RTW_DEBUG
#ifdef RTW_LOG_LEVEL
	uint rtw_drv_log_level = (uint)RTW_LOG_LEVEL; /* from Makefile */
#else
	uint rtw_drv_log_level = _DRV_INFO_;
#endif
module_param(rtw_drv_log_level, uint, 0644);
MODULE_PARM_DESC(rtw_drv_log_level, "set log level when insert driver module, default log level is _DRV_INFO_ = 4");
#endif
int rtw_radio_enable = 1;
int rtw_long_retry_lmt = 7;
int rtw_short_retry_lmt = 7;
int rtw_busy_thresh = 40;
/* int qos_enable = 0; */ /* * */
int rtw_ack_policy = NORMAL_ACK;

int rtw_mp_mode = 0;

#if defined(CONFIG_MP_INCLUDED) && defined(CONFIG_RTW_CUSTOMER_STR)
uint rtw_mp_customer_str = 0;
module_param(rtw_mp_customer_str, uint, 0644);
MODULE_PARM_DESC(rtw_mp_customer_str, "Whether or not to enable customer str support on MP mode");
#endif

int rtw_software_encrypt = 0;
int rtw_software_decrypt = 0;

int rtw_acm_method = 0;/* 0:By SW 1:By HW. */

int rtw_wmm_enable = 1;/* default is set to enable the wmm. */

#ifdef CONFIG_WMMPS_STA
/* uapsd (unscheduled automatic power-save delivery) = a kind of wmmps */
/* 0: NO_LIMIT, 1: TWO_MSDU, 2: FOUR_MSDU, 3: SIX_MSDU */
int rtw_uapsd_max_sp = NO_LIMIT;
/* BIT0: AC_VO UAPSD, BIT1: AC_VI UAPSD, BIT2: AC_BK UAPSD, BIT3: AC_BE UAPSD */
int rtw_uapsd_ac_enable = 0x0;
#endif /* CONFIG_WMMPS_STA */

#if defined(CONFIG_RTL8814A)
	int rtw_pwrtrim_enable = 2; /* disable kfree , rename to power trim disable */
#elif defined(CONFIG_RTL8821C) || defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8822C)
	/*PHYDM API, must enable by default*/
	int rtw_pwrtrim_enable = 1;
#else
	int rtw_pwrtrim_enable = 0; /* Default Enalbe  power trim by efuse config */
#endif

#if CONFIG_TX_AC_LIFETIME
uint rtw_tx_aclt_flags = CONFIG_TX_ACLT_FLAGS;
module_param(rtw_tx_aclt_flags, uint, 0644);
MODULE_PARM_DESC(rtw_tx_aclt_flags, "device TX AC queue packet lifetime control flags");

static uint rtw_tx_aclt_conf_default[3] = CONFIG_TX_ACLT_CONF_DEFAULT;
static uint rtw_tx_aclt_conf_default_num = 0;
module_param_array(rtw_tx_aclt_conf_default, uint, &rtw_tx_aclt_conf_default_num, 0644);
MODULE_PARM_DESC(rtw_tx_aclt_conf_default, "device TX AC queue lifetime config for default status");

#ifdef CONFIG_TX_MCAST2UNI
static uint rtw_tx_aclt_conf_ap_m2u[3] = CONFIG_TX_ACLT_CONF_AP_M2U;
static uint rtw_tx_aclt_conf_ap_m2u_num = 0;
module_param_array(rtw_tx_aclt_conf_ap_m2u, uint, &rtw_tx_aclt_conf_ap_m2u_num, 0644);
MODULE_PARM_DESC(rtw_tx_aclt_conf_ap_m2u, "device TX AC queue lifetime config for AP mode M2U status");
#endif

#ifdef CONFIG_RTW_MESH
static uint rtw_tx_aclt_conf_mesh[3] = CONFIG_TX_ACLT_CONF_MESH;
static uint rtw_tx_aclt_conf_mesh_num = 0;
module_param_array(rtw_tx_aclt_conf_mesh, uint, &rtw_tx_aclt_conf_mesh_num, 0644);
MODULE_PARM_DESC(rtw_tx_aclt_conf_mesh, "device TX AC queue lifetime config for MESH status");
#endif
#endif /* CONFIG_TX_AC_LIFETIME */

uint rtw_tx_bw_mode = 0x21;
module_param(rtw_tx_bw_mode, uint, 0644);
MODULE_PARM_DESC(rtw_tx_bw_mode, "The max tx bw for 2.4G and 5G. format is the same as rtw_bw_mode");

#ifdef CONFIG_FW_HANDLE_TXBCN
uint rtw_tbtt_rpt = 0;	/*ROOT AP - BIT0, VAP1 - BIT1, VAP2 - BIT2, VAP3 - VAP3, FW report TBTT INT by C2H*/
module_param(rtw_tbtt_rpt, uint, 0644);
#endif

#ifdef CONFIG_80211N_HT
int rtw_ht_enable = 1;
/* 0: 20 MHz, 1: 40 MHz, 2: 80 MHz, 3: 160MHz, 4: 80+80MHz
* 2.4G use bit 0 ~ 3, 5G use bit 4 ~ 7
* 0x21 means enable 2.4G 40MHz & 5G 80MHz */
#ifdef CONFIG_RTW_CUSTOMIZE_BWMODE
int rtw_bw_mode = CONFIG_RTW_CUSTOMIZE_BWMODE;
#else
int rtw_bw_mode = 0x21;
#endif
int rtw_ampdu_enable = 1;/* for enable tx_ampdu , */ /* 0: disable, 0x1:enable */
int rtw_rx_stbc = 1;/* 0: disable, bit(0):enable 2.4g, bit(1):enable 5g, default is set to enable 2.4GHZ for IOT issue with bufflao's AP at 5GHZ */
#if (defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8814B) || defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8822C)) && defined(CONFIG_PCI_HCI)
int rtw_rx_ampdu_amsdu = 2;/* 0: disabled, 1:enabled, 2:auto . There is an IOT issu with DLINK DIR-629 when the flag turn on */
#elif ((defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8822C)) && defined(CONFIG_SDIO_HCI))
int rtw_rx_ampdu_amsdu = 1;
#else
int rtw_rx_ampdu_amsdu;/* 0: disabled, 1:enabled, 2:auto . There is an IOT issu with DLINK DIR-629 when the flag turn on */
#endif
/*
* 2: Follow the AMSDU filed in ADDBA Resp. (Deault)
* 0: Force the AMSDU filed in ADDBA Resp. to be disabled.
* 1: Force the AMSDU filed in ADDBA Resp. to be enabled.
*/
int rtw_tx_ampdu_amsdu = 2;

int rtw_quick_addba_req = 0;

static uint rtw_rx_ampdu_sz_limit_1ss[4] = CONFIG_RTW_RX_AMPDU_SZ_LIMIT_1SS;
static uint rtw_rx_ampdu_sz_limit_1ss_num = 0;
module_param_array(rtw_rx_ampdu_sz_limit_1ss, uint, &rtw_rx_ampdu_sz_limit_1ss_num, 0644);
MODULE_PARM_DESC(rtw_rx_ampdu_sz_limit_1ss, "RX AMPDU size limit for 1SS link of each BW, 0xFF: no limitation");

static uint rtw_rx_ampdu_sz_limit_2ss[4] = CONFIG_RTW_RX_AMPDU_SZ_LIMIT_2SS;
static uint rtw_rx_ampdu_sz_limit_2ss_num = 0;
module_param_array(rtw_rx_ampdu_sz_limit_2ss, uint, &rtw_rx_ampdu_sz_limit_2ss_num, 0644);
MODULE_PARM_DESC(rtw_rx_ampdu_sz_limit_2ss, "RX AMPDU size limit for 2SS link of each BW, 0xFF: no limitation");

static uint rtw_rx_ampdu_sz_limit_3ss[4] = CONFIG_RTW_RX_AMPDU_SZ_LIMIT_3SS;
static uint rtw_rx_ampdu_sz_limit_3ss_num = 0;
module_param_array(rtw_rx_ampdu_sz_limit_3ss, uint, &rtw_rx_ampdu_sz_limit_3ss_num, 0644);
MODULE_PARM_DESC(rtw_rx_ampdu_sz_limit_3ss, "RX AMPDU size limit for 3SS link of each BW, 0xFF: no limitation");

static uint rtw_rx_ampdu_sz_limit_4ss[4] = CONFIG_RTW_RX_AMPDU_SZ_LIMIT_4SS;
static uint rtw_rx_ampdu_sz_limit_4ss_num = 0;
module_param_array(rtw_rx_ampdu_sz_limit_4ss, uint, &rtw_rx_ampdu_sz_limit_4ss_num, 0644);
MODULE_PARM_DESC(rtw_rx_ampdu_sz_limit_4ss, "RX AMPDU size limit for 4SS link of each BW, 0xFF: no limitation");

/* Short GI support Bit Map
* BIT0 - 20MHz, 0: non-support, 1: support
* BIT1 - 40MHz, 0: non-support, 1: support
* BIT2 - 80MHz, 0: non-support, 1: support
* BIT3 - 160MHz, 0: non-support, 1: support */
int rtw_short_gi = 0xf;
/* BIT0: Enable VHT LDPC Rx, BIT1: Enable VHT LDPC Tx, BIT4: Enable HT LDPC Rx, BIT5: Enable HT LDPC Tx */
int rtw_ldpc_cap = 0x33;
/* BIT0: Enable VHT STBC Rx, BIT1: Enable VHT STBC Tx, BIT4: Enable HT STBC Rx, BIT5: Enable HT STBC Tx */
int rtw_stbc_cap = 0x13;

/*
* BIT0: Enable VHT SU Beamformer
* BIT1: Enable VHT SU Beamformee
* BIT2: Enable VHT MU Beamformer, depend on VHT SU Beamformer
* BIT3: Enable VHT MU Beamformee, depend on VHT SU Beamformee
* BIT4: Enable HT Beamformer
* BIT5: Enable HT Beamformee
*/
int rtw_beamform_cap = BIT(1) | BIT(3);
int rtw_bfer_rf_number = 0; /*BeamformerCapRfNum Rf path number, 0 for auto, others for manual*/
int rtw_bfee_rf_number = 0; /*BeamformeeCapRfNum  Rf path number, 0 for auto, others for manual*/

#endif /* CONFIG_80211N_HT */

#ifdef CONFIG_80211AC_VHT
int rtw_vht_enable = 1; /* 0:disable, 1:enable, 2:force auto enable */
module_param(rtw_vht_enable, int, 0644);

int rtw_ampdu_factor = 7;

uint rtw_vht_rx_mcs_map = 0xaaaa;
module_param(rtw_vht_rx_mcs_map, uint, 0644);
MODULE_PARM_DESC(rtw_vht_rx_mcs_map, "VHT RX MCS map");
#endif /* CONFIG_80211AC_VHT */

int rtw_lowrate_two_xmit = 1;/* Use 2 path Tx to transmit MCS0~7 and legacy mode */


/* 0: not check in watch dog, 1: check in watch dog  */
int rtw_check_hw_status = 0;

int rtw_low_power = 0;
int rtw_wifi_spec = 0;


int rtw_trx_path_bmp = 0x00;
module_param(rtw_trx_path_bmp, int, 0644); /* [7:4]TX path bmp, [0:3]RX path bmp, 0: not specified */

#ifdef CONFIG_SPECIAL_RF_PATH /* configure Nss/xTxR IC to 1ss/1T1R */
int rtw_tx_path_lmt = 1;
int rtw_rx_path_lmt = 1;
int rtw_tx_nss = 1;
int rtw_rx_nss = 1;
#elif defined(CONFIG_CUSTOMER01_SMART_ANTENNA)
int rtw_tx_path_lmt = 2;
int rtw_rx_path_lmt = 2;
int rtw_tx_nss = 1;
int rtw_rx_nss = 1;
#else
int rtw_tx_path_lmt = 0;
int rtw_rx_path_lmt = 0;
int rtw_tx_nss = 0;
int rtw_rx_nss = 0;
#endif
module_param(rtw_tx_path_lmt, int, 0644); /* limit of TX path number, 0: not specified */
module_param(rtw_rx_path_lmt, int, 0644); /* limit of RX path number, 0: not specified */
module_param(rtw_tx_nss, int, 0644);
module_param(rtw_rx_nss, int, 0644);

char rtw_country_unspecified[] = {0xFF, 0xFF, 0x00};
char *rtw_country_code = rtw_country_unspecified;
module_param(rtw_country_code, charp, 0644);
MODULE_PARM_DESC(rtw_country_code, "The default country code (in alpha2)");

int rtw_channel_plan = CONFIG_RTW_CHPLAN;
module_param(rtw_channel_plan, int, 0644);
MODULE_PARM_DESC(rtw_channel_plan, "The default chplan ID when rtw_alpha2 is not specified or valid");

static uint rtw_excl_chs[MAX_CHANNEL_NUM] = CONFIG_RTW_EXCL_CHS;
static int rtw_excl_chs_num = 0;
module_param_array(rtw_excl_chs, uint, &rtw_excl_chs_num, 0644);
MODULE_PARM_DESC(rtw_excl_chs, "exclusive channel array");

/*if concurrent softap + p2p(GO) is needed, this param lets p2p response full channel list.
But Softap must be SHUT DOWN once P2P decide to set up connection and become a GO.*/
#ifdef CONFIG_FULL_CH_IN_P2P_HANDSHAKE
	int rtw_full_ch_in_p2p_handshake = 1; /* reply full channel list*/
#else
	int rtw_full_ch_in_p2p_handshake = 0; /* reply only softap channel*/
#endif

#ifdef CONFIG_BT_COEXIST
int rtw_btcoex_enable = 2;
module_param(rtw_btcoex_enable, int, 0644);
MODULE_PARM_DESC(rtw_btcoex_enable, "BT co-existence on/off, 0:off, 1:on, 2:by efuse");

int rtw_ant_num = 0;
module_param(rtw_ant_num, int, 0644);
MODULE_PARM_DESC(rtw_ant_num, "Antenna number setting, 0:by efuse");

int rtw_bt_iso = 2;/* 0:Low, 1:High, 2:From Efuse */
int rtw_bt_sco = 3;/* 0:Idle, 1:None-SCO, 2:SCO, 3:From Counter, 4.Busy, 5.OtherBusy */
int rtw_bt_ampdu = 1 ; /* 0:Disable BT control A-MPDU, 1:Enable BT control A-MPDU. */
#endif /* CONFIG_BT_COEXIST */

int rtw_AcceptAddbaReq = _TRUE;/* 0:Reject AP's Add BA req, 1:Accept AP's Add BA req. */

int rtw_antdiv_cfg = 2; /* 0:OFF , 1:ON, 2:decide by Efuse config */
int rtw_antdiv_type = 0
	; /* 0:decide by efuse  1: for 88EE, 1Tx and 1RxCG are diversity.(2 Ant with SPDT), 2:  for 88EE, 1Tx and 2Rx are diversity.( 2 Ant, Tx and RxCG are both on aux port, RxCS is on main port ), 3: for 88EE, 1Tx and 1RxCG are fixed.(1Ant, Tx and RxCG are both on aux port) */

int rtw_drv_ant_band_switch = 1; /* 0:OFF , 1:ON, Driver control antenna band switch*/

int rtw_single_ant_path; /*0:main ant , 1:aux ant , Fixed single antenna path, default main ant*/

/* 0: doesn't switch, 1: switch from usb2.0 to usb 3.0 2: switch from usb3.0 to usb 2.0 */
int rtw_switch_usb_mode = 0;

#ifdef CONFIG_USB_AUTOSUSPEND
int rtw_enusbss = 1;/* 0:disable,1:enable */
#else
int rtw_enusbss = 0;/* 0:disable,1:enable */
#endif

int rtw_hwpdn_mode = 2; /* 0:disable,1:enable,2: by EFUSE config */

#ifdef CONFIG_HW_PWRP_DETECTION
int rtw_hwpwrp_detect = 1;
#else
int rtw_hwpwrp_detect = 0; /* HW power  ping detect 0:disable , 1:enable */
#endif

#ifdef CONFIG_USB_HCI
int rtw_hw_wps_pbc = 1;
#else
int rtw_hw_wps_pbc = 0;
#endif

#ifdef CONFIG_TX_MCAST2UNI
int rtw_mc2u_disable = 0;
#endif /* CONFIG_TX_MCAST2UNI */

#ifdef CONFIG_80211D
int rtw_80211d = 0;
#endif

#ifdef CONFIG_PCI_ASPM
/* CLK_REQ:BIT0 L0s:BIT1 ASPM_L1:BIT2 L1Off:BIT3*/
int	rtw_pci_aspm_enable = 0x5;
#else
int	rtw_pci_aspm_enable;
#endif

/*
 * BIT [15:12] mask of ps mode
 * BIT [11:8] val of ps mode
 * BIT [7:4] mask of perf mode
 * BIT [3:0] val of perf mode
 *
 * L0s:BIT[+0] L1:BIT[+1]
 *
 * 0x0030: change value only if perf mode
 * 0x3300: change value only if ps mode
 * 0x3330: change value in both perf and ps mode
 */
#ifdef CONFIG_PCI_DYNAMIC_ASPM
#ifdef CONFIG_PCI_ASPM
int rtw_pci_dynamic_aspm_linkctrl = 0x3330;
#else
int rtw_pci_dynamic_aspm_linkctrl = 0x0030;
#endif
#else
int rtw_pci_dynamic_aspm_linkctrl = 0x0000;
#endif
module_param(rtw_pci_dynamic_aspm_linkctrl, int, 0644);

#ifdef CONFIG_QOS_OPTIMIZATION
int rtw_qos_opt_enable = 1; /* 0: disable,1:enable */
#else
int rtw_qos_opt_enable = 0; /* 0: disable,1:enable */
#endif
module_param(rtw_qos_opt_enable, int, 0644);

#ifdef CONFIG_RTW_ACS
int rtw_acs_auto_scan = 0; /*0:disable, 1:enable*/
module_param(rtw_acs_auto_scan, int, 0644);

int rtw_acs = 1;
module_param(rtw_acs, int, 0644);
#endif

#ifdef CONFIG_BACKGROUND_NOISE_MONITOR
int rtw_nm = 1;/*noise monitor*/
module_param(rtw_nm, int, 0644);
#endif

char *ifname = "wlan%d";
module_param(ifname, charp, 0644);
MODULE_PARM_DESC(ifname, "The default name to allocate for first interface");

#ifdef CONFIG_PLATFORM_ANDROID
	char *if2name = "p2p%d";
#else /* CONFIG_PLATFORM_ANDROID */
	char *if2name = "wlan%d";
#endif /* CONFIG_PLATFORM_ANDROID */
module_param(if2name, charp, 0644);
MODULE_PARM_DESC(if2name, "The default name to allocate for second interface");

char *rtw_initmac = 0;  /* temp mac address if users want to use instead of the mac address in Efuse */

#ifdef CONFIG_CONCURRENT_MODE

	#if (CONFIG_IFACE_NUMBER > 2)
		int rtw_virtual_iface_num = CONFIG_IFACE_NUMBER - 1;
		module_param(rtw_virtual_iface_num, int, 0644);
	#else
		int rtw_virtual_iface_num = 1;
	#endif

#ifdef CONFIG_P2P
	int rtw_sel_p2p_iface = IFACE_ID1;

	module_param(rtw_sel_p2p_iface, int, 0644);

#endif

#endif
#ifdef CONFIG_AP_MODE
u8 rtw_bmc_tx_rate = MGN_UNKNOWN;
#endif
#ifdef RTW_WOW_STA_MIX
int rtw_wowlan_sta_mix_mode = 1;
#else
int rtw_wowlan_sta_mix_mode = 0;
#endif
module_param(rtw_wowlan_sta_mix_mode, int, 0644);
module_param(rtw_pwrtrim_enable, int, 0644);
module_param(rtw_initmac, charp, 0644);
module_param(rtw_chip_version, int, 0644);
module_param(rtw_rfintfs, int, 0644);
module_param(rtw_lbkmode, int, 0644);
module_param(rtw_network_mode, int, 0644);
module_param(rtw_channel, int, 0644);
module_param(rtw_mp_mode, int, 0644);
module_param(rtw_wmm_enable, int, 0644);
#ifdef CONFIG_WMMPS_STA
module_param(rtw_uapsd_max_sp, int, 0644);
module_param(rtw_uapsd_ac_enable, int, 0644);
module_param(rtw_wmm_smart_ps, int, 0644);
#endif /* CONFIG_WMMPS_STA */
module_param(rtw_vrtl_carrier_sense, int, 0644);
module_param(rtw_vcs_type, int, 0644);
module_param(rtw_busy_thresh, int, 0644);

#ifdef CONFIG_80211N_HT
module_param(rtw_ht_enable, int, 0644);
module_param(rtw_bw_mode, int, 0644);
module_param(rtw_ampdu_enable, int, 0644);
module_param(rtw_rx_stbc, int, 0644);
module_param(rtw_rx_ampdu_amsdu, int, 0644);
module_param(rtw_tx_ampdu_amsdu, int, 0644);
module_param(rtw_quick_addba_req, int, 0644);
#endif /* CONFIG_80211N_HT */

#ifdef CONFIG_BEAMFORMING
module_param(rtw_beamform_cap, int, 0644);
#endif
module_param(rtw_lowrate_two_xmit, int, 0644);

module_param(rtw_power_mgnt, int, 0644);
module_param(rtw_smart_ps, int, 0644);
module_param(rtw_low_power, int, 0644);
module_param(rtw_wifi_spec, int, 0644);

module_param(rtw_full_ch_in_p2p_handshake, int, 0644);
module_param(rtw_antdiv_cfg, int, 0644);
module_param(rtw_antdiv_type, int, 0644);

module_param(rtw_drv_ant_band_switch, int, 0644);
module_param(rtw_single_ant_path, int, 0644);

module_param(rtw_switch_usb_mode, int, 0644);

module_param(rtw_enusbss, int, 0644);
module_param(rtw_hwpdn_mode, int, 0644);
module_param(rtw_hwpwrp_detect, int, 0644);

module_param(rtw_hw_wps_pbc, int, 0644);
module_param(rtw_check_hw_status, int, 0644);

#ifdef CONFIG_PCI_HCI
module_param(rtw_pci_aspm_enable, int, 0644);
#endif

#ifdef CONFIG_TX_EARLY_MODE
module_param(rtw_early_mode, int, 0644);
#endif
#ifdef CONFIG_ADAPTOR_INFO_CACHING_FILE
char *rtw_adaptor_info_caching_file_path = "/data/misc/wifi/rtw_cache";
module_param(rtw_adaptor_info_caching_file_path, charp, 0644);
MODULE_PARM_DESC(rtw_adaptor_info_caching_file_path, "The path of adapter info cache file");
#endif /* CONFIG_ADAPTOR_INFO_CACHING_FILE */

#ifdef CONFIG_LAYER2_ROAMING
uint rtw_max_roaming_times = 2;
module_param(rtw_max_roaming_times, uint, 0644);
MODULE_PARM_DESC(rtw_max_roaming_times, "The max roaming times to try");
#endif /* CONFIG_LAYER2_ROAMING */

#ifdef CONFIG_IOL
int rtw_fw_iol = 1;
module_param(rtw_fw_iol, int, 0644);
MODULE_PARM_DESC(rtw_fw_iol, "FW IOL. 0:Disable, 1:enable, 2:by usb speed");
#endif /* CONFIG_IOL */

#ifdef CONFIG_FILE_FWIMG
char *rtw_fw_file_path = "/system/etc/firmware/rtlwifi/FW_NIC.BIN";
module_param(rtw_fw_file_path, charp, 0644);
MODULE_PARM_DESC(rtw_fw_file_path, "The path of fw image");

char *rtw_fw_wow_file_path = "/system/etc/firmware/rtlwifi/FW_WoWLAN.BIN";
module_param(rtw_fw_wow_file_path, charp, 0644);
MODULE_PARM_DESC(rtw_fw_wow_file_path, "The path of fw for Wake on Wireless image");

#ifdef CONFIG_MP_INCLUDED
char *rtw_fw_mp_bt_file_path = "";
module_param(rtw_fw_mp_bt_file_path, charp, 0644);
MODULE_PARM_DESC(rtw_fw_mp_bt_file_path, "The path of fw for MP-BT image");
#endif /* CONFIG_MP_INCLUDED */
#endif /* CONFIG_FILE_FWIMG */

#ifdef CONFIG_TX_MCAST2UNI
module_param(rtw_mc2u_disable, int, 0644);
#endif /* CONFIG_TX_MCAST2UNI */

#ifdef CONFIG_80211D
module_param(rtw_80211d, int, 0644);
MODULE_PARM_DESC(rtw_80211d, "Enable 802.11d mechanism");
#endif

#ifdef CONFIG_ADVANCE_OTA
/*	BIT(0): OTA continuous rotated test within low RSSI,1R CCA in path B
	BIT(1) & BIT(2): OTA continuous rotated test with low high RSSI */
/* Experimental environment: shielding room with half of absorber and 2~3 rotation per minute */
int rtw_advnace_ota;
module_param(rtw_advnace_ota, int, 0644);
#endif

uint rtw_notch_filter = RTW_NOTCH_FILTER;
module_param(rtw_notch_filter, uint, 0644);
MODULE_PARM_DESC(rtw_notch_filter, "0:Disable, 1:Enable, 2:Enable only for P2P");

uint rtw_hiq_filter = CONFIG_RTW_HIQ_FILTER;
module_param(rtw_hiq_filter, uint, 0644);
MODULE_PARM_DESC(rtw_hiq_filter, "0:allow all, 1:allow special, 2:deny all");

uint rtw_adaptivity_en = CONFIG_RTW_ADAPTIVITY_EN;
module_param(rtw_adaptivity_en, uint, 0644);
MODULE_PARM_DESC(rtw_adaptivity_en, "0:disable, 1:enable");

uint rtw_adaptivity_mode = CONFIG_RTW_ADAPTIVITY_MODE;
module_param(rtw_adaptivity_mode, uint, 0644);
MODULE_PARM_DESC(rtw_adaptivity_mode, "0:normal, 1:carrier sense");

int rtw_adaptivity_th_l2h_ini = CONFIG_RTW_ADAPTIVITY_TH_L2H_INI;
module_param(rtw_adaptivity_th_l2h_ini, int, 0644);
MODULE_PARM_DESC(rtw_adaptivity_th_l2h_ini, "th_l2h_ini for Adaptivity");

int rtw_adaptivity_th_edcca_hl_diff = CONFIG_RTW_ADAPTIVITY_TH_EDCCA_HL_DIFF;
module_param(rtw_adaptivity_th_edcca_hl_diff, int, 0644);
MODULE_PARM_DESC(rtw_adaptivity_th_edcca_hl_diff, "th_edcca_hl_diff for Adaptivity");

#ifdef CONFIG_DFS_MASTER
uint rtw_dfs_region_domain = CONFIG_RTW_DFS_REGION_DOMAIN;
module_param(rtw_dfs_region_domain, uint, 0644);
MODULE_PARM_DESC(rtw_dfs_region_domain, "0:UNKNOWN, 1:FCC, 2:MKK, 3:ETSI");
#endif

uint rtw_amplifier_type_2g = CONFIG_RTW_AMPLIFIER_TYPE_2G;
module_param(rtw_amplifier_type_2g, uint, 0644);
MODULE_PARM_DESC(rtw_amplifier_type_2g, "BIT3:2G ext-PA, BIT4:2G ext-LNA");

uint rtw_amplifier_type_5g = CONFIG_RTW_AMPLIFIER_TYPE_5G;
module_param(rtw_amplifier_type_5g, uint, 0644);
MODULE_PARM_DESC(rtw_amplifier_type_5g, "BIT6:5G ext-PA, BIT7:5G ext-LNA");

uint rtw_RFE_type = CONFIG_RTW_RFE_TYPE;
module_param(rtw_RFE_type, uint, 0644);
MODULE_PARM_DESC(rtw_RFE_type, "default init value:64");

uint rtw_powertracking_type = 64;
module_param(rtw_powertracking_type, uint, 0644);
MODULE_PARM_DESC(rtw_powertracking_type, "default init value:64");

uint rtw_GLNA_type = CONFIG_RTW_GLNA_TYPE;
module_param(rtw_GLNA_type, uint, 0644);
MODULE_PARM_DESC(rtw_GLNA_type, "default init value:0");

uint rtw_TxBBSwing_2G = 0xFF;
module_param(rtw_TxBBSwing_2G, uint, 0644);
MODULE_PARM_DESC(rtw_TxBBSwing_2G, "default init value:0xFF");

uint rtw_TxBBSwing_5G = 0xFF;
module_param(rtw_TxBBSwing_5G, uint, 0644);
MODULE_PARM_DESC(rtw_TxBBSwing_5G, "default init value:0xFF");

uint rtw_OffEfuseMask = 0;
module_param(rtw_OffEfuseMask, uint, 0644);
MODULE_PARM_DESC(rtw_OffEfuseMask, "default open Efuse Mask value:0");

uint rtw_FileMaskEfuse = 0;
module_param(rtw_FileMaskEfuse, uint, 0644);
MODULE_PARM_DESC(rtw_FileMaskEfuse, "default drv Mask Efuse value:0");

uint rtw_rxgain_offset_2g = 0;
module_param(rtw_rxgain_offset_2g, uint, 0644);
MODULE_PARM_DESC(rtw_rxgain_offset_2g, "default RF Gain 2G Offset value:0");

uint rtw_rxgain_offset_5gl = 0;
module_param(rtw_rxgain_offset_5gl, uint, 0644);
MODULE_PARM_DESC(rtw_rxgain_offset_5gl, "default RF Gain 5GL Offset value:0");

uint rtw_rxgain_offset_5gm = 0;
module_param(rtw_rxgain_offset_5gm, uint, 0644);
MODULE_PARM_DESC(rtw_rxgain_offset_5gm, "default RF Gain 5GM Offset value:0");

uint rtw_rxgain_offset_5gh = 0;
module_param(rtw_rxgain_offset_5gh, uint, 0644);
MODULE_PARM_DESC(rtw_rxgain_offset_5gm, "default RF Gain 5GL Offset value:0");

uint rtw_pll_ref_clk_sel = CONFIG_RTW_PLL_REF_CLK_SEL;
module_param(rtw_pll_ref_clk_sel, uint, 0644);
MODULE_PARM_DESC(rtw_pll_ref_clk_sel, "force pll_ref_clk_sel, 0xF:use autoload value");

int rtw_tx_pwr_by_rate = CONFIG_TXPWR_BY_RATE_EN;
module_param(rtw_tx_pwr_by_rate, int, 0644);
MODULE_PARM_DESC(rtw_tx_pwr_by_rate, "0:Disable, 1:Enable, 2: Depend on efuse");

#if CONFIG_TXPWR_LIMIT
int rtw_tx_pwr_lmt_enable = CONFIG_TXPWR_LIMIT_EN;
module_param(rtw_tx_pwr_lmt_enable, int, 0644);
MODULE_PARM_DESC(rtw_tx_pwr_lmt_enable, "0:Disable, 1:Enable, 2: Depend on efuse");
#endif

static int rtw_target_tx_pwr_2g_a[RATE_SECTION_NUM] = CONFIG_RTW_TARGET_TX_PWR_2G_A;
static int rtw_target_tx_pwr_2g_a_num = 0;
module_param_array(rtw_target_tx_pwr_2g_a, int, &rtw_target_tx_pwr_2g_a_num, 0644);
MODULE_PARM_DESC(rtw_target_tx_pwr_2g_a, "2.4G target tx power (unit:dBm) of RF path A for each rate section, should match the real calibrate power, -1: undefined");

static int rtw_target_tx_pwr_2g_b[RATE_SECTION_NUM] = CONFIG_RTW_TARGET_TX_PWR_2G_B;
static int rtw_target_tx_pwr_2g_b_num = 0;
module_param_array(rtw_target_tx_pwr_2g_b, int, &rtw_target_tx_pwr_2g_b_num, 0644);
MODULE_PARM_DESC(rtw_target_tx_pwr_2g_b, "2.4G target tx power (unit:dBm) of RF path B for each rate section, should match the real calibrate power, -1: undefined");

static int rtw_target_tx_pwr_2g_c[RATE_SECTION_NUM] = CONFIG_RTW_TARGET_TX_PWR_2G_C;
static int rtw_target_tx_pwr_2g_c_num = 0;
module_param_array(rtw_target_tx_pwr_2g_c, int, &rtw_target_tx_pwr_2g_c_num, 0644);
MODULE_PARM_DESC(rtw_target_tx_pwr_2g_c, "2.4G target tx power (unit:dBm) of RF path C for each rate section, should match the real calibrate power, -1: undefined");

static int rtw_target_tx_pwr_2g_d[RATE_SECTION_NUM] = CONFIG_RTW_TARGET_TX_PWR_2G_D;
static int rtw_target_tx_pwr_2g_d_num = 0;
module_param_array(rtw_target_tx_pwr_2g_d, int, &rtw_target_tx_pwr_2g_d_num, 0644);
MODULE_PARM_DESC(rtw_target_tx_pwr_2g_d, "2.4G target tx power (unit:dBm) of RF path D for each rate section, should match the real calibrate power, -1: undefined");

#if CONFIG_IEEE80211_BAND_5GHZ
static int rtw_target_tx_pwr_5g_a[RATE_SECTION_NUM - 1] = CONFIG_RTW_TARGET_TX_PWR_5G_A;
static int rtw_target_tx_pwr_5g_a_num = 0;
module_param_array(rtw_target_tx_pwr_5g_a, int, &rtw_target_tx_pwr_5g_a_num, 0644);
MODULE_PARM_DESC(rtw_target_tx_pwr_5g_a, "5G target tx power (unit:dBm) of RF path A for each rate section, should match the real calibrate power, -1: undefined");

static int rtw_target_tx_pwr_5g_b[RATE_SECTION_NUM - 1] = CONFIG_RTW_TARGET_TX_PWR_5G_B;
static int rtw_target_tx_pwr_5g_b_num = 0;
module_param_array(rtw_target_tx_pwr_5g_b, int, &rtw_target_tx_pwr_5g_b_num, 0644);
MODULE_PARM_DESC(rtw_target_tx_pwr_5g_b, "5G target tx power (unit:dBm) of RF path B for each rate section, should match the real calibrate power, -1: undefined");

static int rtw_target_tx_pwr_5g_c[RATE_SECTION_NUM - 1] = CONFIG_RTW_TARGET_TX_PWR_5G_C;
static int rtw_target_tx_pwr_5g_c_num = 0;
module_param_array(rtw_target_tx_pwr_5g_c, int, &rtw_target_tx_pwr_5g_c_num, 0644);
MODULE_PARM_DESC(rtw_target_tx_pwr_5g_c, "5G target tx power (unit:dBm) of RF path C for each rate section, should match the real calibrate power, -1: undefined");

static int rtw_target_tx_pwr_5g_d[RATE_SECTION_NUM - 1] = CONFIG_RTW_TARGET_TX_PWR_5G_D;
static int rtw_target_tx_pwr_5g_d_num = 0;
module_param_array(rtw_target_tx_pwr_5g_d, int, &rtw_target_tx_pwr_5g_d_num, 0644);
MODULE_PARM_DESC(rtw_target_tx_pwr_5g_d, "5G target tx power (unit:dBm) of RF path D for each rate section, should match the real calibrate power, -1: undefined");
#endif /* CONFIG_IEEE80211_BAND_5GHZ */

#ifdef CONFIG_RTW_TX_NPATH_EN
/*0:disable ,1: 2path*/
int rtw_tx_npath_enable = 1;
module_param(rtw_tx_npath_enable, int, 0644);
MODULE_PARM_DESC(rtw_tx_npath_enable, "0:Disable, 1:TX-2PATH");
#endif

#ifdef CONFIG_RTW_PATH_DIV
/*0:disable ,1: path diversity*/
int rtw_path_div_enable = 1;
module_param(rtw_path_div_enable, int, 0644);
MODULE_PARM_DESC(rtw_path_div_enable, "0:Disable, 1:Enable path diversity");
#endif


int rtw_tsf_update_pause_factor = CONFIG_TSF_UPDATE_PAUSE_FACTOR;
module_param(rtw_tsf_update_pause_factor, int, 0644);
MODULE_PARM_DESC(rtw_tsf_update_pause_factor, "num of bcn intervals to stay TSF update pause status");

int rtw_tsf_update_restore_factor = CONFIG_TSF_UPDATE_RESTORE_FACTOR;
module_param(rtw_tsf_update_restore_factor, int, 0644);
MODULE_PARM_DESC(rtw_tsf_update_restore_factor, "num of bcn intervals to stay TSF update restore status");

#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
char *rtw_phy_file_path = REALTEK_CONFIG_PATH;
module_param(rtw_phy_file_path, charp, 0644);
MODULE_PARM_DESC(rtw_phy_file_path, "The path of phy parameter");
/* PHY FILE Bit Map
* BIT0 - MAC,				0: non-support, 1: support
* BIT1 - BB,					0: non-support, 1: support
* BIT2 - BB_PG,				0: non-support, 1: support
* BIT3 - BB_MP,				0: non-support, 1: support
* BIT4 - RF,					0: non-support, 1: support
* BIT5 - RF_TXPWR_TRACK,	0: non-support, 1: support
* BIT6 - RF_TXPWR_LMT,		0: non-support, 1: support */
int rtw_load_phy_file = (BIT2 | BIT6);
module_param(rtw_load_phy_file, int, 0644);
MODULE_PARM_DESC(rtw_load_phy_file, "PHY File Bit Map");
int rtw_decrypt_phy_file = 0;
module_param(rtw_decrypt_phy_file, int, 0644);
MODULE_PARM_DESC(rtw_decrypt_phy_file, "Enable Decrypt PHY File");
#endif

#ifdef CONFIG_SUPPORT_TRX_SHARED
#ifdef DFT_TRX_SHARE_MODE
int rtw_trx_share_mode = DFT_TRX_SHARE_MODE;
#else
int rtw_trx_share_mode = 0;
#endif
module_param(rtw_trx_share_mode, int, 0644);
MODULE_PARM_DESC(rtw_trx_share_mode, "TRx FIFO Shared");
#endif

#ifdef CONFIG_DYNAMIC_SOML
uint rtw_dynamic_soml_en = 1;
module_param(rtw_dynamic_soml_en, int, 0644);
MODULE_PARM_DESC(rtw_dynamic_soml_en, "0: disable, 1: enable with default param, 2: enable with specified param.");

uint rtw_dynamic_soml_train_num = 0;
module_param(rtw_dynamic_soml_train_num, int, 0644);
MODULE_PARM_DESC(rtw_dynamic_soml_train_num, "SOML training number");

uint rtw_dynamic_soml_interval = 0;
module_param(rtw_dynamic_soml_interval, int, 0644);
MODULE_PARM_DESC(rtw_dynamic_soml_interval, "SOML training interval");

uint rtw_dynamic_soml_period = 0;
module_param(rtw_dynamic_soml_period, int, 0644);
MODULE_PARM_DESC(rtw_dynamic_soml_period, "SOML training period");

uint rtw_dynamic_soml_delay = 0;
module_param(rtw_dynamic_soml_delay, int, 0644);
MODULE_PARM_DESC(rtw_dynamic_soml_delay, "SOML training delay");
#endif

uint rtw_phydm_ability = 0xffffffff;
module_param(rtw_phydm_ability, uint, 0644);

uint rtw_halrf_ability = 0xffffffff;
module_param(rtw_halrf_ability, uint, 0644);

#ifdef CONFIG_RTW_MESH
uint rtw_peer_alive_based_preq = 1;
module_param(rtw_peer_alive_based_preq, uint, 0644);
MODULE_PARM_DESC(rtw_peer_alive_based_preq,
	"On demand PREQ will reference peer alive status. 0: Off, 1: On");
#endif

int _netdev_open(struct net_device *pnetdev);
int netdev_open(struct net_device *pnetdev);
static int netdev_close(struct net_device *pnetdev);
#ifdef CONFIG_PLATFORM_INTEL_BYT
extern int rtw_sdio_set_power(int on);
#endif /* CONFIG_PLATFORM_INTEL_BYT */

#ifdef CONFIG_MCC_MODE
/* enable MCC mode or not */
int rtw_en_mcc = 1;
/* can referece following value before insmod driver */
int rtw_mcc_ap_bw20_target_tx_tp = MCC_AP_BW20_TARGET_TX_TP;
int rtw_mcc_ap_bw40_target_tx_tp = MCC_AP_BW40_TARGET_TX_TP;
int rtw_mcc_ap_bw80_target_tx_tp = MCC_AP_BW80_TARGET_TX_TP;
int rtw_mcc_sta_bw20_target_tx_tp = MCC_STA_BW20_TARGET_TX_TP;
int rtw_mcc_sta_bw40_target_tx_tp = MCC_STA_BW40_TARGET_TX_TP;
int rtw_mcc_sta_bw80_target_tx_tp = MCC_STA_BW80_TARGET_TX_TP;
int rtw_mcc_single_tx_cri = MCC_SINGLE_TX_CRITERIA;
int rtw_mcc_policy_table_idx = 0;
int rtw_mcc_duration = 0;
int rtw_mcc_enable_runtime_duration = 1;
#ifdef CONFIG_MCC_PHYDM_OFFLOAD
int rtw_mcc_phydm_offload = 1;
#else
int rtw_mcc_phydm_offload = 0;
#endif
module_param(rtw_en_mcc, int, 0644);
module_param(rtw_mcc_single_tx_cri, int, 0644);
module_param(rtw_mcc_ap_bw20_target_tx_tp, int, 0644);
module_param(rtw_mcc_ap_bw40_target_tx_tp, int, 0644);
module_param(rtw_mcc_ap_bw80_target_tx_tp, int, 0644);
module_param(rtw_mcc_sta_bw20_target_tx_tp, int, 0644);
module_param(rtw_mcc_sta_bw40_target_tx_tp, int, 0644);
module_param(rtw_mcc_sta_bw80_target_tx_tp, int, 0644);
module_param(rtw_mcc_policy_table_idx, int, 0644);
module_param(rtw_mcc_duration, int, 0644);
module_param(rtw_mcc_phydm_offload, int, 0644);
#endif /*CONFIG_MCC_MODE */

#ifdef CONFIG_RTW_NAPI
/*following setting should define NAPI in Makefile
enable napi only = 1, disable napi = 0*/
int rtw_en_napi = 1;
module_param(rtw_en_napi, int, 0644);
#ifdef CONFIG_RTW_NAPI_DYNAMIC
int rtw_napi_threshold = 100; /* unit: Mbps */
module_param(rtw_napi_threshold, int, 0644);
#endif /* CONFIG_RTW_NAPI_DYNAMIC */
#ifdef CONFIG_RTW_GRO
/*following setting should define GRO in Makefile
enable gro = 1, disable gro = 0*/
int rtw_en_gro = 1;
module_param(rtw_en_gro, int, 0644);
#endif /* CONFIG_RTW_GRO */
#endif /* CONFIG_RTW_NAPI */

#ifdef RTW_IQK_FW_OFFLOAD
int rtw_iqk_fw_offload = 1;
#else
int rtw_iqk_fw_offload;
#endif /* RTW_IQK_FW_OFFLOAD */
module_param(rtw_iqk_fw_offload, int, 0644);

#ifdef RTW_CHANNEL_SWITCH_OFFLOAD
int rtw_ch_switch_offload = 0;
#else
int rtw_ch_switch_offload;
#endif /* RTW_CHANNEL_SWITCH_OFFLOAD */
module_param(rtw_ch_switch_offload, int, 0644);

#ifdef CONFIG_TDLS
int rtw_en_tdls = 1;
module_param(rtw_en_tdls, int, 0644);
#endif

#ifdef CONFIG_FW_OFFLOAD_PARAM_INIT
int rtw_fw_param_init = 1;
module_param(rtw_fw_param_init, int, 0644);
#endif

#ifdef CONFIG_TDMADIG
int rtw_tdmadig_en = 1;
/*
1:MODE_PERFORMANCE
2:MODE_COVERAGE
*/
int rtw_tdmadig_mode = 1;
int rtw_dynamic_tdmadig = 0;
module_param(rtw_tdmadig_en, int, 0644);
module_param(rtw_tdmadig_mode, int, 0644);
module_param(rtw_dynamic_tdmadig, int, 0644);
#endif/*CONFIG_TDMADIG*/

/*dynamic RRSR default enable*/
int rtw_en_dyn_rrsr = 1;
int rtw_rrsr_value = 0xFFFFFFFF;
module_param(rtw_en_dyn_rrsr, int, 0644);
module_param(rtw_rrsr_value, int, 0644);

#ifdef CONFIG_WOWLAN
/*
 * 0: disable, 1: enable
 */
uint rtw_wow_enable = 1;
module_param(rtw_wow_enable, uint, 0644);
/*
 * bit[0]: magic packet wake up
 * bit[1]: unucast packet(HW/FW unuicast)
 * bit[2]: deauth wake up
 */
uint rtw_wakeup_event = RTW_WAKEUP_EVENT;
module_param(rtw_wakeup_event, uint, 0644);
/*
 * 0: common WOWLAN
 * bit[0]: disable BB RF
 * bit[1]: For wireless remote controller with or without connection
 */
uint rtw_suspend_type = RTW_SUSPEND_TYPE;
module_param(rtw_suspend_type, uint, 0644);
#endif

#ifdef RTW_BUSY_DENY_SCAN
uint rtw_scan_interval_thr = BUSY_TRAFFIC_SCAN_DENY_PERIOD;
module_param(rtw_scan_interval_thr, uint, 0644);
MODULE_PARM_DESC(rtw_scan_interval_thr, "Threshold used to judge if scan " \
		 "request comes from scan UI, unit is ms.");
#endif /* RTW_BUSY_DENY_SCAN */

#ifdef CONFIG_RTL8822C_XCAP_NEW_POLICY
uint rtw_8822c_xcap_overwrite = 1;
module_param(rtw_8822c_xcap_overwrite, uint, 0644);
#endif

#if CONFIG_TX_AC_LIFETIME
static void rtw_regsty_load_tx_ac_lifetime(struct registry_priv *regsty)
{
	int i, j;
	struct tx_aclt_conf_t *conf;
	uint *parm;

	regsty->tx_aclt_flags = (u8)rtw_tx_aclt_flags;

	for (i = 0; i < TX_ACLT_CONF_NUM; i++) {
		conf = &regsty->tx_aclt_confs[i];
		if (i == TX_ACLT_CONF_DEFAULT)
			parm = rtw_tx_aclt_conf_default;
		#ifdef CONFIG_TX_MCAST2UNI
		else if (i == TX_ACLT_CONF_AP_M2U)
			parm = rtw_tx_aclt_conf_ap_m2u;
		#endif
		#ifdef CONFIG_RTW_MESH
		else if (i == TX_ACLT_CONF_MESH)
			parm = rtw_tx_aclt_conf_mesh;
		#endif
		else
			parm = NULL;

		if (parm) {
			conf->en = parm[0] & 0xF;
			conf->vo_vi = parm[1];
			conf->be_bk = parm[2];
		}	
	}
}
#endif

void rtw_regsty_load_target_tx_power(struct registry_priv *regsty)
{
	int path, rs;
	int *target_tx_pwr;

	for (path = RF_PATH_A; path < RF_PATH_MAX; path++) {
		if (path == RF_PATH_A)
			target_tx_pwr = rtw_target_tx_pwr_2g_a;
		else if (path == RF_PATH_B)
			target_tx_pwr = rtw_target_tx_pwr_2g_b;
		else if (path == RF_PATH_C)
			target_tx_pwr = rtw_target_tx_pwr_2g_c;
		else if (path == RF_PATH_D)
			target_tx_pwr = rtw_target_tx_pwr_2g_d;

		for (rs = CCK; rs < RATE_SECTION_NUM; rs++)
			regsty->target_tx_pwr_2g[path][rs] = target_tx_pwr[rs];
	}

#if CONFIG_IEEE80211_BAND_5GHZ
	for (path = RF_PATH_A; path < RF_PATH_MAX; path++) {
		if (path == RF_PATH_A)
			target_tx_pwr = rtw_target_tx_pwr_5g_a;
		else if (path == RF_PATH_B)
			target_tx_pwr = rtw_target_tx_pwr_5g_b;
		else if (path == RF_PATH_C)
			target_tx_pwr = rtw_target_tx_pwr_5g_c;
		else if (path == RF_PATH_D)
			target_tx_pwr = rtw_target_tx_pwr_5g_d;

		for (rs = OFDM; rs < RATE_SECTION_NUM; rs++)
			regsty->target_tx_pwr_5g[path][rs - 1] = target_tx_pwr[rs - 1];
	}
#endif /* CONFIG_IEEE80211_BAND_5GHZ */
}

inline void rtw_regsty_load_excl_chs(struct registry_priv *regsty)
{
	int i;
	int ch_num = 0;

	for (i = 0; i < MAX_CHANNEL_NUM; i++)
		if (((u8)rtw_excl_chs[i]) != 0)
			regsty->excl_chs[ch_num++] = (u8)rtw_excl_chs[i];

	if (ch_num < MAX_CHANNEL_NUM)
		regsty->excl_chs[ch_num] = 0;
}

#ifdef CONFIG_80211N_HT
inline void rtw_regsty_init_rx_ampdu_sz_limit(struct registry_priv *regsty)
{
	int i, j;
	uint *sz_limit;

	for (i = 0; i < 4; i++) {
		if (i == 0)
			sz_limit = rtw_rx_ampdu_sz_limit_1ss;
		else if (i == 1)
			sz_limit = rtw_rx_ampdu_sz_limit_2ss;
		else if (i == 2)
			sz_limit = rtw_rx_ampdu_sz_limit_3ss;
		else if (i == 3)
			sz_limit = rtw_rx_ampdu_sz_limit_4ss;

		for (j = 0; j < 4; j++)
			regsty->rx_ampdu_sz_limit_by_nss_bw[i][j] = sz_limit[j];
	}
}
#endif /* CONFIG_80211N_HT */

uint loadparam(_adapter *padapter)
{
	uint status = _SUCCESS;
	struct registry_priv  *registry_par = &padapter->registrypriv;


#ifdef CONFIG_RTW_DEBUG
	if (rtw_drv_log_level >= _DRV_MAX_)
		rtw_drv_log_level = _DRV_DEBUG_;
#endif

	registry_par->chip_version = (u8)rtw_chip_version;
	registry_par->rfintfs = (u8)rtw_rfintfs;
	registry_par->lbkmode = (u8)rtw_lbkmode;
	/* registry_par->hci = (u8)hci; */
	registry_par->network_mode  = (u8)rtw_network_mode;

	_rtw_memcpy(registry_par->ssid.Ssid, "ANY", 3);
	registry_par->ssid.SsidLength = 3;

	registry_par->channel = (u8)rtw_channel;
#ifdef CONFIG_NARROWBAND_SUPPORTING
	if (rtw_nb_config != RTW_NB_CONFIG_NONE)
		rtw_wireless_mode &= ~WIRELESS_11B;
#endif
	registry_par->wireless_mode = (u8)rtw_wireless_mode;

	if (IsSupported24G(registry_par->wireless_mode) && (!is_supported_5g(registry_par->wireless_mode))
	    && (registry_par->channel > 14))
		registry_par->channel = 1;
	else if (is_supported_5g(registry_par->wireless_mode) && (!IsSupported24G(registry_par->wireless_mode))
		 && (registry_par->channel <= 14))
		registry_par->channel = 36;

	registry_par->vrtl_carrier_sense = (u8)rtw_vrtl_carrier_sense ;
	registry_par->vcs_type = (u8)rtw_vcs_type;
	registry_par->rts_thresh = (u16)rtw_rts_thresh;
	registry_par->frag_thresh = (u16)rtw_frag_thresh;
	registry_par->preamble = (u8)rtw_preamble;
	registry_par->scan_mode = (u8)rtw_scan_mode;
	registry_par->smart_ps = (u8)rtw_smart_ps;
	registry_par->check_fw_ps = (u8)rtw_check_fw_ps;
	#ifdef CONFIG_TDMADIG
		registry_par->tdmadig_en = (u8)rtw_tdmadig_en;
		registry_par->tdmadig_mode = (u8)rtw_tdmadig_mode;
		registry_par->tdmadig_dynamic = (u8) rtw_dynamic_tdmadig;
		registry_par->power_mgnt = PS_MODE_ACTIVE;
		registry_par->ips_mode = IPS_NONE;
	#else
		registry_par->power_mgnt = (u8)rtw_power_mgnt;
		registry_par->ips_mode = (u8)rtw_ips_mode;
	#endif/*CONFIG_TDMADIG*/
	registry_par->lps_level = (u8)rtw_lps_level;
	registry_par->en_dyn_rrsr = (u8)rtw_en_dyn_rrsr;
	registry_par->set_rrsr_value = (u32)rtw_rrsr_value;
#ifdef CONFIG_LPS_1T1R
	registry_par->lps_1t1r = (u8)(rtw_lps_1t1r ? 1 : 0);
#endif
	registry_par->lps_chk_by_tp = (u8)rtw_lps_chk_by_tp;
#ifdef CONFIG_WOWLAN
	registry_par->wow_power_mgnt = (u8)rtw_wow_power_mgnt;
	registry_par->wow_lps_level = (u8)rtw_wow_lps_level;
	#ifdef CONFIG_LPS_1T1R
	registry_par->wow_lps_1t1r = (u8)(rtw_wow_lps_1t1r ? 1 : 0);
	#endif
#endif /* CONFIG_WOWLAN */
	registry_par->radio_enable = (u8)rtw_radio_enable;
	registry_par->long_retry_lmt = (u8)rtw_long_retry_lmt;
	registry_par->short_retry_lmt = (u8)rtw_short_retry_lmt;
	registry_par->busy_thresh = (u16)rtw_busy_thresh;
	registry_par->max_bss_cnt = (u16)rtw_max_bss_cnt;
	/* registry_par->qos_enable = (u8)rtw_qos_enable; */
	registry_par->ack_policy = (u8)rtw_ack_policy;
	registry_par->mp_mode = (u8)rtw_mp_mode;
#if defined(CONFIG_MP_INCLUDED) && defined(CONFIG_RTW_CUSTOMER_STR)
	registry_par->mp_customer_str = (u8)rtw_mp_customer_str;
#endif
	registry_par->software_encrypt = (u8)rtw_software_encrypt;
	registry_par->software_decrypt = (u8)rtw_software_decrypt;

	registry_par->acm_method = (u8)rtw_acm_method;
	registry_par->usb_rxagg_mode = (u8)rtw_usb_rxagg_mode;
	registry_par->dynamic_agg_enable = (u8)rtw_dynamic_agg_enable;

	/* WMM */
	registry_par->wmm_enable = (u8)rtw_wmm_enable;

#ifdef CONFIG_WMMPS_STA
	/* UAPSD */
	registry_par->uapsd_max_sp_len= (u8)rtw_uapsd_max_sp;
	registry_par->uapsd_ac_enable = (u8)rtw_uapsd_ac_enable;
	registry_par->wmm_smart_ps = (u8)rtw_wmm_smart_ps;
#endif /* CONFIG_WMMPS_STA */

	registry_par->RegPwrTrimEnable = (u8)rtw_pwrtrim_enable;

#if CONFIG_TX_AC_LIFETIME
	rtw_regsty_load_tx_ac_lifetime(registry_par);
#endif

	registry_par->tx_bw_mode = (u8)rtw_tx_bw_mode;

#ifdef CONFIG_80211N_HT
	registry_par->ht_enable = (u8)rtw_ht_enable;
	if (registry_par->ht_enable && is_supported_ht(registry_par->wireless_mode)) {
#ifdef CONFIG_NARROWBAND_SUPPORTING
	if (rtw_nb_config != RTW_NB_CONFIG_NONE)
		rtw_bw_mode = 0;
#endif
		registry_par->bw_mode = (u8)rtw_bw_mode;
		registry_par->ampdu_enable = (u8)rtw_ampdu_enable;
		registry_par->rx_stbc = (u8)rtw_rx_stbc;
		registry_par->rx_ampdu_amsdu = (u8)rtw_rx_ampdu_amsdu;
		registry_par->tx_ampdu_amsdu = (u8)rtw_tx_ampdu_amsdu;
		registry_par->tx_quick_addba_req = (u8)rtw_quick_addba_req;
		registry_par->short_gi = (u8)rtw_short_gi;
		registry_par->ldpc_cap = (u8)rtw_ldpc_cap;
#if defined(CONFIG_CUSTOMER01_SMART_ANTENNA)
		rtw_stbc_cap = 0x0;
#endif
#ifdef CONFIG_RTW_TX_NPATH_EN
		registry_par->tx_npath = (u8)rtw_tx_npath_enable;
#endif
#ifdef CONFIG_RTW_PATH_DIV
		registry_par->path_div = (u8)rtw_path_div_enable;
#endif
		registry_par->stbc_cap = (u8)rtw_stbc_cap;
		registry_par->beamform_cap = (u8)rtw_beamform_cap;
		registry_par->beamformer_rf_num = (u8)rtw_bfer_rf_number;
		registry_par->beamformee_rf_num = (u8)rtw_bfee_rf_number;
		rtw_regsty_init_rx_ampdu_sz_limit(registry_par);
	}
#endif
#ifdef DBG_LA_MODE
	registry_par->la_mode_en = (u8)rtw_la_mode_en;
#endif
#ifdef CONFIG_NARROWBAND_SUPPORTING
	registry_par->rtw_nb_config = (u8)rtw_nb_config;
#endif

#ifdef CONFIG_80211AC_VHT
	registry_par->vht_enable = (u8)rtw_vht_enable;
	registry_par->ampdu_factor = (u8)rtw_ampdu_factor;
	registry_par->vht_rx_mcs_map[0] = (u8)(rtw_vht_rx_mcs_map & 0xFF);
	registry_par->vht_rx_mcs_map[1] = (u8)((rtw_vht_rx_mcs_map & 0xFF00) >> 8);
#endif

#ifdef CONFIG_TX_EARLY_MODE
	registry_par->early_mode = (u8)rtw_early_mode;
#endif
	registry_par->lowrate_two_xmit = (u8)rtw_lowrate_two_xmit;
	registry_par->trx_path_bmp = (u8)rtw_trx_path_bmp;
	registry_par->tx_path_lmt = (u8)rtw_tx_path_lmt;
	registry_par->rx_path_lmt = (u8)rtw_rx_path_lmt;
	registry_par->tx_nss = (u8)rtw_tx_nss;
	registry_par->rx_nss = (u8)rtw_rx_nss;
	registry_par->low_power = (u8)rtw_low_power;

	registry_par->check_hw_status = (u8)rtw_check_hw_status;

	registry_par->wifi_spec = (u8)rtw_wifi_spec;

	if (strlen(rtw_country_code) != 2
		|| is_alpha(rtw_country_code[0]) == _FALSE
		|| is_alpha(rtw_country_code[1]) == _FALSE
	) {
		if (rtw_country_code != rtw_country_unspecified)
			RTW_ERR("%s discard rtw_country_code not in alpha2\n", __func__);
		_rtw_memset(registry_par->alpha2, 0xFF, 2);
	} else
		_rtw_memcpy(registry_par->alpha2, rtw_country_code, 2);

	registry_par->channel_plan = (u8)rtw_channel_plan;
	rtw_regsty_load_excl_chs(registry_par);

	registry_par->full_ch_in_p2p_handshake = (u8)rtw_full_ch_in_p2p_handshake;
#ifdef CONFIG_BT_COEXIST
	registry_par->btcoex = (u8)rtw_btcoex_enable;
	registry_par->bt_iso = (u8)rtw_bt_iso;
	registry_par->bt_sco = (u8)rtw_bt_sco;
	registry_par->bt_ampdu = (u8)rtw_bt_ampdu;
	registry_par->ant_num = (u8)rtw_ant_num;
	registry_par->single_ant_path = (u8) rtw_single_ant_path;
#endif

	registry_par->bAcceptAddbaReq = (u8)rtw_AcceptAddbaReq;

	registry_par->antdiv_cfg = (u8)rtw_antdiv_cfg;
	registry_par->antdiv_type = (u8)rtw_antdiv_type;

	registry_par->drv_ant_band_switch = (u8) rtw_drv_ant_band_switch;

	registry_par->switch_usb_mode = (u8)rtw_switch_usb_mode;
#ifdef SUPPORT_HW_RFOFF_DETECTED
	registry_par->hwpdn_mode = (u8)rtw_hwpdn_mode;/* 0:disable,1:enable,2:by EFUSE config */
	registry_par->hwpwrp_detect = (u8)rtw_hwpwrp_detect;/* 0:disable,1:enable */
#endif

	registry_par->hw_wps_pbc = (u8)rtw_hw_wps_pbc;

#ifdef CONFIG_ADAPTOR_INFO_CACHING_FILE
	snprintf(registry_par->adaptor_info_caching_file_path, PATH_LENGTH_MAX, "%s", rtw_adaptor_info_caching_file_path);
	registry_par->adaptor_info_caching_file_path[PATH_LENGTH_MAX - 1] = 0;
#endif

#ifdef CONFIG_LAYER2_ROAMING
	registry_par->max_roaming_times = (u8)rtw_max_roaming_times;
#endif

#ifdef CONFIG_IOL
	registry_par->fw_iol = rtw_fw_iol;
#endif

#ifdef CONFIG_80211D
	registry_par->enable80211d = (u8)rtw_80211d;
#endif

	snprintf(registry_par->ifname, 16, "%s", ifname);
	snprintf(registry_par->if2name, 16, "%s", if2name);

	registry_par->notch_filter = (u8)rtw_notch_filter;

#ifdef CONFIG_CONCURRENT_MODE
	registry_par->virtual_iface_num = (u8)rtw_virtual_iface_num;
#ifdef CONFIG_P2P
	registry_par->sel_p2p_iface = (u8)rtw_sel_p2p_iface;
	RTW_INFO("%s, Select P2P interface: iface_id:%d\n", __func__, registry_par->sel_p2p_iface);
#endif
#endif
	registry_par->pll_ref_clk_sel = (u8)rtw_pll_ref_clk_sel;

#if CONFIG_TXPWR_LIMIT
	registry_par->RegEnableTxPowerLimit = (u8)rtw_tx_pwr_lmt_enable;
#endif
	registry_par->RegEnableTxPowerByRate = (u8)rtw_tx_pwr_by_rate;

	rtw_regsty_load_target_tx_power(registry_par);

	registry_par->tsf_update_pause_factor = (u8)rtw_tsf_update_pause_factor;
	registry_par->tsf_update_restore_factor = (u8)rtw_tsf_update_restore_factor;

	registry_par->TxBBSwing_2G = (s8)rtw_TxBBSwing_2G;
	registry_par->TxBBSwing_5G = (s8)rtw_TxBBSwing_5G;
	registry_par->bEn_RFE = 1;
	registry_par->RFE_Type = (u8)rtw_RFE_type;
	registry_par->PowerTracking_Type = (u8)rtw_powertracking_type;
	registry_par->AmplifierType_2G = (u8)rtw_amplifier_type_2g;
	registry_par->AmplifierType_5G = (u8)rtw_amplifier_type_5g;
	registry_par->GLNA_Type = (u8)rtw_GLNA_type;
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	registry_par->load_phy_file = (u8)rtw_load_phy_file;
	registry_par->RegDecryptCustomFile = (u8)rtw_decrypt_phy_file;
#endif
	registry_par->qos_opt_enable = (u8)rtw_qos_opt_enable;

	registry_par->hiq_filter = (u8)rtw_hiq_filter;

	registry_par->adaptivity_en = (u8)rtw_adaptivity_en;
	registry_par->adaptivity_mode = (u8)rtw_adaptivity_mode;
	registry_par->adaptivity_th_l2h_ini = (s8)rtw_adaptivity_th_l2h_ini;
	registry_par->adaptivity_th_edcca_hl_diff = (s8)rtw_adaptivity_th_edcca_hl_diff;

#ifdef CONFIG_DYNAMIC_SOML
	registry_par->dyn_soml_en = (u8)rtw_dynamic_soml_en;
	registry_par->dyn_soml_train_num = (u8)rtw_dynamic_soml_train_num;
	registry_par->dyn_soml_interval = (u8)rtw_dynamic_soml_interval;
	registry_par->dyn_soml_period = (u8)rtw_dynamic_soml_period;
	registry_par->dyn_soml_delay = (u8)rtw_dynamic_soml_delay;
#endif

	registry_par->boffefusemask = (u8)rtw_OffEfuseMask;
	registry_par->bFileMaskEfuse = (u8)rtw_FileMaskEfuse;
	registry_par->bBTFileMaskEfuse = (u8)rtw_FileMaskEfuse;

#ifdef CONFIG_RTW_ACS
	registry_par->acs_mode = (u8)rtw_acs;
	registry_par->acs_auto_scan = (u8)rtw_acs_auto_scan;
#endif
#ifdef CONFIG_BACKGROUND_NOISE_MONITOR
	registry_par->nm_mode = (u8)rtw_nm;
#endif
	registry_par->reg_rxgain_offset_2g = (u32) rtw_rxgain_offset_2g;
	registry_par->reg_rxgain_offset_5gl = (u32) rtw_rxgain_offset_5gl;
	registry_par->reg_rxgain_offset_5gm = (u32) rtw_rxgain_offset_5gm;
	registry_par->reg_rxgain_offset_5gh = (u32) rtw_rxgain_offset_5gh;

#ifdef CONFIG_DFS_MASTER
	registry_par->dfs_region_domain = (u8)rtw_dfs_region_domain;
#endif

#ifdef CONFIG_MCC_MODE
	registry_par->en_mcc = (u8)rtw_en_mcc;
	registry_par->rtw_mcc_ap_bw20_target_tx_tp = (u32)rtw_mcc_ap_bw20_target_tx_tp;
	registry_par->rtw_mcc_ap_bw40_target_tx_tp = (u32)rtw_mcc_ap_bw40_target_tx_tp;
	registry_par->rtw_mcc_ap_bw80_target_tx_tp = (u32)rtw_mcc_ap_bw80_target_tx_tp;
	registry_par->rtw_mcc_sta_bw20_target_tx_tp = (u32)rtw_mcc_sta_bw20_target_tx_tp;
	registry_par->rtw_mcc_sta_bw40_target_tx_tp = (u32)rtw_mcc_sta_bw40_target_tx_tp;
	registry_par->rtw_mcc_sta_bw80_target_tx_tp = (u32)rtw_mcc_sta_bw80_target_tx_tp;
	registry_par->rtw_mcc_single_tx_cri = (u32)rtw_mcc_single_tx_cri;
	registry_par->rtw_mcc_policy_table_idx = rtw_mcc_policy_table_idx;
	registry_par->rtw_mcc_duration = (u8)rtw_mcc_duration;
	registry_par->rtw_mcc_enable_runtime_duration = rtw_mcc_enable_runtime_duration;
	registry_par->rtw_mcc_phydm_offload = rtw_mcc_phydm_offload;
#endif /*CONFIG_MCC_MODE */

#ifdef CONFIG_WOWLAN
	registry_par->wowlan_enable = rtw_wow_enable;
	registry_par->wakeup_event = rtw_wakeup_event;
	registry_par->suspend_type = rtw_suspend_type;
#endif

#ifdef CONFIG_SUPPORT_TRX_SHARED
	registry_par->trx_share_mode = rtw_trx_share_mode;
#endif
	registry_par->wowlan_sta_mix_mode = rtw_wowlan_sta_mix_mode;

#ifdef CONFIG_PCI_HCI
	registry_par->pci_aspm_config = rtw_pci_aspm_enable;
	registry_par->pci_dynamic_aspm_linkctrl = rtw_pci_dynamic_aspm_linkctrl;
#endif

#ifdef CONFIG_RTW_NAPI
	registry_par->en_napi = (u8)rtw_en_napi;
#ifdef CONFIG_RTW_NAPI_DYNAMIC
	registry_par->napi_threshold = (u32)rtw_napi_threshold;
#endif /* CONFIG_RTW_NAPI_DYNAMIC */
#ifdef CONFIG_RTW_GRO
	registry_par->en_gro = (u8)rtw_en_gro;
	if (!registry_par->en_napi && registry_par->en_gro) {
		registry_par->en_gro = 0;
		RTW_WARN("Disable GRO because NAPI is not enabled\n");
	}
#endif /* CONFIG_RTW_GRO */
#endif /* CONFIG_RTW_NAPI */

	registry_par->iqk_fw_offload = (u8)rtw_iqk_fw_offload;
	registry_par->ch_switch_offload = (u8)rtw_ch_switch_offload;

#ifdef CONFIG_TDLS
	registry_par->en_tdls = rtw_en_tdls;
#endif

#ifdef CONFIG_ADVANCE_OTA
	registry_par->adv_ota = rtw_advnace_ota;
#endif
#ifdef CONFIG_FW_OFFLOAD_PARAM_INIT
	registry_par->fw_param_init = rtw_fw_param_init;
#endif
#ifdef CONFIG_AP_MODE
	registry_par->bmc_tx_rate = rtw_bmc_tx_rate;
#endif
#ifdef CONFIG_FW_HANDLE_TXBCN
	registry_par->fw_tbtt_rpt = rtw_tbtt_rpt;
#endif
	registry_par->phydm_ability = rtw_phydm_ability;
	registry_par->halrf_ability = rtw_halrf_ability;
#ifdef CONFIG_RTW_MESH
	registry_par->peer_alive_based_preq = rtw_peer_alive_based_preq;
#endif

#ifdef RTW_BUSY_DENY_SCAN
	registry_par->scan_interval_thr = rtw_scan_interval_thr;
#endif

#ifdef CONFIG_RTL8822C_XCAP_NEW_POLICY
	registry_par->rtw_8822c_xcap_overwrite = (u8)rtw_8822c_xcap_overwrite;
#endif

	return status;
}

/**
 * rtw_net_set_mac_address
 * This callback function is used for the Media Access Control address
 * of each net_device needs to be changed.
 *
 * Arguments:
 * @pnetdev: net_device pointer.
 * @addr: new MAC address.
 *
 * Return:
 * ret = 0: Permit to change net_device's MAC address.
 * ret = -1 (Default): Operation not permitted.
 *
 * Auther: Arvin Liu
 * Date: 2015/05/29
 */
static int rtw_net_set_mac_address(struct net_device *pnetdev, void *addr)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct sockaddr *sa = (struct sockaddr *)addr;
	int ret = -1;

	/* only the net_device is in down state to permit modifying mac addr */
	if ((pnetdev->flags & IFF_UP) == _TRUE) {
		RTW_INFO(FUNC_ADPT_FMT": The net_device's is not in down state\n"
			 , FUNC_ADPT_ARG(padapter));

		return ret;
	}

	/* if the net_device is linked, it's not permit to modify mac addr */
	if (check_fwstate(pmlmepriv, WIFI_UNDER_LINKING) ||
	    check_fwstate(pmlmepriv, WIFI_ASOC_STATE) ||
	    check_fwstate(pmlmepriv, WIFI_UNDER_SURVEY)) {
		RTW_INFO(FUNC_ADPT_FMT": The net_device's is not idle currently\n"
			 , FUNC_ADPT_ARG(padapter));

		return ret;
	}

	/* check whether the input mac address is valid to permit modifying mac addr */
	if (rtw_check_invalid_mac_address(sa->sa_data, _FALSE) == _TRUE) {
		RTW_INFO(FUNC_ADPT_FMT": Invalid Mac Addr for "MAC_FMT"\n"
			 , FUNC_ADPT_ARG(padapter), MAC_ARG(sa->sa_data));

		return ret;
	}

	_rtw_memcpy(adapter_mac_addr(padapter), sa->sa_data, ETH_ALEN); /* set mac addr to adapter */
	_rtw_memcpy(pnetdev->dev_addr, sa->sa_data, ETH_ALEN); /* set mac addr to net_device */

#if 0
	if (rtw_is_hw_init_completed(padapter)) {
		rtw_ps_deny(padapter, PS_DENY_IOCTL);
		LeaveAllPowerSaveModeDirect(padapter); /* leave PS mode for guaranteeing to access hw register successfully */

#ifdef CONFIG_MI_WITH_MBSSID_CAM
		rtw_hal_change_macaddr_mbid(padapter, sa->sa_data);
#else
		rtw_hal_set_hwreg(padapter, HW_VAR_MAC_ADDR, sa->sa_data); /* set mac addr to mac register */
#endif

		rtw_ps_deny_cancel(padapter, PS_DENY_IOCTL);
	}
#else
	rtw_ps_deny(padapter, PS_DENY_IOCTL);
	LeaveAllPowerSaveModeDirect(padapter); /* leave PS mode for guaranteeing to access hw register successfully */
#ifdef CONFIG_MI_WITH_MBSSID_CAM
	rtw_hal_change_macaddr_mbid(padapter, sa->sa_data);
#else
	rtw_hal_set_hwreg(padapter, HW_VAR_MAC_ADDR, sa->sa_data); /* set mac addr to mac register */
#endif
	rtw_ps_deny_cancel(padapter, PS_DENY_IOCTL);
#endif

	RTW_INFO(FUNC_ADPT_FMT": Set Mac Addr to "MAC_FMT" Successfully\n"
		 , FUNC_ADPT_ARG(padapter), MAC_ARG(sa->sa_data));

	ret = 0;

	return ret;
}

static struct net_device_stats *rtw_net_get_stats(struct net_device *pnetdev)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
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


static u16 rtw_select_queue(struct net_device *dev, struct sk_buff *skb
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
	, struct net_device *sb_dev
	#else
	, void *accel_priv
	#endif
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	, select_queue_fallback_t fallback
	#endif
#endif
)
{
	_adapter	*padapter = rtw_netdev_priv(dev);
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
	u16	eth_type;
	u32 priority;
	u8 *pdata = skb->data;

	_rtw_memcpy(&eth_type, pdata + (ETH_ALEN << 1), 2);

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

#endif

static u8 is_rtw_ndev(struct net_device *ndev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
	return ndev->netdev_ops
		&& ndev->netdev_ops->ndo_do_ioctl
		&& ndev->netdev_ops->ndo_do_ioctl == rtw_ioctl;
#else
	return ndev->do_ioctl
		&& ndev->do_ioctl == rtw_ioctl;
#endif
}

static int rtw_ndev_notifier_call(struct notifier_block *nb, unsigned long state, void *ptr)
{
	struct net_device *ndev;

	if (ptr == NULL)
		return NOTIFY_DONE;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
	ndev = netdev_notifier_info_to_dev(ptr);
#else
	ndev = ptr;
#endif

	if (ndev == NULL)
		return NOTIFY_DONE;

	if (!is_rtw_ndev(ndev))
		return NOTIFY_DONE;

	RTW_INFO(FUNC_NDEV_FMT" state:%lu\n", FUNC_NDEV_ARG(ndev), state);

	switch (state) {
	case NETDEV_CHANGENAME:
		rtw_adapter_proc_replace(ndev);
		break;
	#ifdef CONFIG_NEW_NETDEV_HDL
	case NETDEV_PRE_UP :
		{
			_adapter *adapter = rtw_netdev_priv(ndev);

			rtw_pwr_wakeup(adapter);
		}
		break;
	#endif
	}

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

int rtw_ndev_init(struct net_device *dev)
{
	_adapter *adapter = rtw_netdev_priv(dev);

	RTW_PRINT(FUNC_ADPT_FMT" if%d mac_addr="MAC_FMT"\n"
		, FUNC_ADPT_ARG(adapter), (adapter->iface_id + 1), MAC_ARG(dev->dev_addr));
	strncpy(adapter->old_ifname, dev->name, IFNAMSIZ);
	adapter->old_ifname[IFNAMSIZ - 1] = '\0';
	rtw_adapter_proc_init(dev);

	return 0;
}

void rtw_ndev_uninit(struct net_device *dev)
{
	_adapter *adapter = rtw_netdev_priv(dev);

	RTW_PRINT(FUNC_ADPT_FMT" if%d\n"
		  , FUNC_ADPT_ARG(adapter), (adapter->iface_id + 1));
	rtw_adapter_proc_deinit(dev);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
static const struct net_device_ops rtw_netdev_ops = {
	.ndo_init = rtw_ndev_init,
	.ndo_uninit = rtw_ndev_uninit,
	.ndo_open = netdev_open,
	.ndo_stop = netdev_close,
	.ndo_start_xmit = rtw_xmit_entry,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	.ndo_select_queue	= rtw_select_queue,
#endif
	.ndo_set_mac_address = rtw_net_set_mac_address,
	.ndo_get_stats = rtw_net_get_stats,
	.ndo_do_ioctl = rtw_ioctl,
};
#endif

int rtw_init_netdev_name(struct net_device *pnetdev, const char *ifname)
{
#ifdef CONFIG_EASY_REPLACEMENT
	_adapter *padapter = rtw_netdev_priv(pnetdev);
	struct net_device	*TargetNetdev = NULL;
	_adapter			*TargetAdapter = NULL;

	if (padapter->bDongle == 1) {
		TargetNetdev = rtw_get_same_net_ndev_by_name(pnetdev, "wlan0");
		if (TargetNetdev) {
			RTW_INFO("Force onboard module driver disappear !!!\n");
			TargetAdapter = rtw_netdev_priv(TargetNetdev);
			TargetAdapter->DriverState = DRIVER_DISAPPEAR;

			padapter->pid[0] = TargetAdapter->pid[0];
			padapter->pid[1] = TargetAdapter->pid[1];
			padapter->pid[2] = TargetAdapter->pid[2];

			dev_put(TargetNetdev);
			unregister_netdev(TargetNetdev);

			padapter->DriverState = DRIVER_REPLACE_DONGLE;
		}
	}
#endif /* CONFIG_EASY_REPLACEMENT */

	if (dev_alloc_name(pnetdev, ifname) < 0)
		RTW_ERR("dev_alloc_name, fail!\n");

	rtw_netif_carrier_off(pnetdev);
	/* rtw_netif_stop_queue(pnetdev); */

	return 0;
}

void rtw_hook_if_ops(struct net_device *ndev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
	ndev->netdev_ops = &rtw_netdev_ops;
#else
	ndev->init = rtw_ndev_init;
	ndev->uninit = rtw_ndev_uninit;
	ndev->open = netdev_open;
	ndev->stop = netdev_close;
	ndev->hard_start_xmit = rtw_xmit_entry;
	ndev->set_mac_address = rtw_net_set_mac_address;
	ndev->get_stats = rtw_net_get_stats;
	ndev->do_ioctl = rtw_ioctl;
#endif
}

#ifdef CONFIG_CONCURRENT_MODE
static void rtw_hook_vir_if_ops(struct net_device *ndev);
#endif
struct net_device *rtw_init_netdev(_adapter *old_padapter)
{
	_adapter *padapter;
	struct net_device *pnetdev;

	if (old_padapter != NULL) {
		rtw_os_ndev_free(old_padapter);
		pnetdev = rtw_alloc_etherdev_with_old_priv(sizeof(_adapter), (void *)old_padapter);
	} else
		pnetdev = rtw_alloc_etherdev(sizeof(_adapter));

	if (!pnetdev)
		return NULL;

	padapter = rtw_netdev_priv(pnetdev);
	padapter->pnetdev = pnetdev;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24)
	SET_MODULE_OWNER(pnetdev);
#endif

	rtw_hook_if_ops(pnetdev);
#ifdef CONFIG_CONCURRENT_MODE
	if (!is_primary_adapter(padapter))
		rtw_hook_vir_if_ops(pnetdev);
#endif /* CONFIG_CONCURRENT_MODE */


#ifdef CONFIG_TCP_CSUM_OFFLOAD_TX
        pnetdev->features |= (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
        pnetdev->hw_features |= (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM);
#endif
#endif

#ifdef CONFIG_RTW_NETIF_SG
        pnetdev->features |= NETIF_F_SG;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
        pnetdev->hw_features |= NETIF_F_SG;
#endif
#endif

	if ((pnetdev->features & NETIF_F_SG) && (pnetdev->features & NETIF_F_IP_CSUM)) {
		pnetdev->features |= (NETIF_F_TSO | NETIF_F_GSO);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
		pnetdev->hw_features |= (NETIF_F_TSO | NETIF_F_GSO);
#endif
	}
	/* pnetdev->tx_timeout = NULL; */
	pnetdev->watchdog_timeo = HZ * 3; /* 3 second timeout */

#ifdef CONFIG_WIRELESS_EXT
	pnetdev->wireless_handlers = (struct iw_handler_def *)&rtw_handlers_def;
#endif

#ifdef WIRELESS_SPY
	/* priv->wireless_data.spy_data = &priv->spy_data; */
	/* pnetdev->wireless_data = &priv->wireless_data; */
#endif

	return pnetdev;
}

int rtw_os_ndev_alloc(_adapter *adapter)
{
	int ret = _FAIL;
	struct net_device *ndev = NULL;

	ndev = rtw_init_netdev(adapter);
	if (ndev == NULL) {
		rtw_warn_on(1);
		goto exit;
	}
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0)
	SET_NETDEV_DEV(ndev, dvobj_to_dev(adapter_to_dvobj(adapter)));
#endif

#ifdef CONFIG_PCI_HCI
	if (adapter_to_dvobj(adapter)->bdma64)
		ndev->features |= NETIF_F_HIGHDMA;
	ndev->irq = adapter_to_dvobj(adapter)->irq;
#endif

#if defined(CONFIG_IOCTL_CFG80211)
	if (rtw_cfg80211_ndev_res_alloc(adapter) != _SUCCESS) {
		rtw_warn_on(1);
	} else
#endif
	ret = _SUCCESS;

	if (ret != _SUCCESS && ndev)
		rtw_free_netdev(ndev);
exit:
	return ret;
}

void rtw_os_ndev_free(_adapter *adapter)
{
#if defined(CONFIG_IOCTL_CFG80211)
	rtw_cfg80211_ndev_res_free(adapter);
#endif

	/* free the old_pnetdev */
	if (adapter->rereg_nd_name_priv.old_pnetdev) {
		rtw_free_netdev(adapter->rereg_nd_name_priv.old_pnetdev);
		adapter->rereg_nd_name_priv.old_pnetdev = NULL;
	}

	if (adapter->pnetdev) {
		rtw_free_netdev(adapter->pnetdev);
		adapter->pnetdev = NULL;
	}
}

int rtw_os_ndev_register(_adapter *adapter, const char *name)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	int ret = _SUCCESS;
	struct net_device *ndev = adapter->pnetdev;
	u8 rtnl_lock_needed = rtw_rtnl_lock_needed(dvobj);

#ifdef CONFIG_RTW_NAPI
	netif_napi_add(ndev, &adapter->napi, rtw_recv_napi_poll, RTL_NAPI_WEIGHT);
#endif /* CONFIG_RTW_NAPI */

#if defined(CONFIG_IOCTL_CFG80211)
	if (rtw_cfg80211_ndev_res_register(adapter) != _SUCCESS) {
		rtw_warn_on(1);
		ret = _FAIL;
		goto exit;
	}
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)) && defined(CONFIG_PCI_HCI)
	ndev->gro_flush_timeout = 100000;
#endif
	/* alloc netdev name */
	rtw_init_netdev_name(ndev, name);

	_rtw_memcpy(ndev->dev_addr, adapter_mac_addr(adapter), ETH_ALEN);

	/* Tell the network stack we exist */

	if (rtnl_lock_needed)
		ret = (register_netdev(ndev) == 0) ? _SUCCESS : _FAIL;
	else
		ret = (register_netdevice(ndev) == 0) ? _SUCCESS : _FAIL;

	if (ret == _SUCCESS)
		adapter->registered = 1;
	else
		RTW_INFO(FUNC_NDEV_FMT" if%d Failed!\n", FUNC_NDEV_ARG(ndev), (adapter->iface_id + 1));

#if defined(CONFIG_IOCTL_CFG80211)
	if (ret != _SUCCESS) {
		rtw_cfg80211_ndev_res_unregister(adapter);
		#if !defined(RTW_SINGLE_WIPHY)
		rtw_wiphy_unregister(adapter_to_wiphy(adapter));
		#endif
	}
#endif

#if defined(CONFIG_IOCTL_CFG80211)
exit:
#endif
#ifdef CONFIG_RTW_NAPI
	if (ret != _SUCCESS)
		netif_napi_del(&adapter->napi);
#endif /* CONFIG_RTW_NAPI */

	return ret;
}

void rtw_os_ndev_unregister(_adapter *adapter)
{
	struct net_device *netdev = NULL;

	if (adapter == NULL || adapter->registered == 0)
		return;

	adapter->ndev_unregistering = 1;

	netdev = adapter->pnetdev;

#if defined(CONFIG_IOCTL_CFG80211)
	rtw_cfg80211_ndev_res_unregister(adapter);
#endif

	if ((adapter->DriverState != DRIVER_DISAPPEAR) && netdev) {
		struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
		u8 rtnl_lock_needed = rtw_rtnl_lock_needed(dvobj);

		if (rtnl_lock_needed)
			unregister_netdev(netdev);
		else
			unregister_netdevice(netdev);
	}

#if defined(CONFIG_IOCTL_CFG80211) && !defined(RTW_SINGLE_WIPHY)
#ifdef CONFIG_RFKILL_POLL
	rtw_cfg80211_deinit_rfkill(adapter_to_wiphy(adapter));
#endif
	rtw_wiphy_unregister(adapter_to_wiphy(adapter));
#endif

#ifdef CONFIG_RTW_NAPI
	if (adapter->napi_state == NAPI_ENABLE) {
		napi_disable(&adapter->napi);
		adapter->napi_state = NAPI_DISABLE;
	}
	netif_napi_del(&adapter->napi);
#endif /* CONFIG_RTW_NAPI */

	adapter->registered = 0;
	adapter->ndev_unregistering = 0;
}

/**
 * rtw_os_ndev_init - Allocate and register OS layer net device and relating structures for @adapter
 * @adapter: the adapter on which this function applies
 * @name: the requesting net device name
 *
 * Returns:
 * _SUCCESS or _FAIL
 */
int rtw_os_ndev_init(_adapter *adapter, const char *name)
{
	int ret = _FAIL;

	if (rtw_os_ndev_alloc(adapter) != _SUCCESS)
		goto exit;

	if (rtw_os_ndev_register(adapter, name) != _SUCCESS)
		goto os_ndev_free;

	ret = _SUCCESS;

os_ndev_free:
	if (ret != _SUCCESS)
		rtw_os_ndev_free(adapter);
exit:
	return ret;
}

/**
 * rtw_os_ndev_deinit - Unregister and free OS layer net device and relating structures for @adapter
 * @adapter: the adapter on which this function applies
 */
void rtw_os_ndev_deinit(_adapter *adapter)
{
	rtw_os_ndev_unregister(adapter);
	rtw_os_ndev_free(adapter);
}

int rtw_os_ndevs_alloc(struct dvobj_priv *dvobj)
{
	int i, status = _SUCCESS;
	_adapter *adapter;

#if defined(CONFIG_IOCTL_CFG80211)
	if (rtw_cfg80211_dev_res_alloc(dvobj) != _SUCCESS) {
		rtw_warn_on(1);
		return _FAIL;
	}
#endif

	for (i = 0; i < dvobj->iface_nums; i++) {

		if (i >= CONFIG_IFACE_NUMBER) {
			RTW_ERR("%s %d >= CONFIG_IFACE_NUMBER(%d)\n", __func__, i, CONFIG_IFACE_NUMBER);
			rtw_warn_on(1);
			continue;
		}

		adapter = dvobj->padapters[i];
		if (adapter && !adapter->pnetdev) {

			#ifdef CONFIG_RTW_DYNAMIC_NDEV
			if (!is_primary_adapter(adapter))
				continue;
			#endif

			status = rtw_os_ndev_alloc(adapter);
			if (status != _SUCCESS) {
				rtw_warn_on(1);
				break;
			}
		}
	}

	if (status != _SUCCESS) {
		for (; i >= 0; i--) {
			adapter = dvobj->padapters[i];
			if (adapter && adapter->pnetdev)
				rtw_os_ndev_free(adapter);
		}
	}

#if defined(CONFIG_IOCTL_CFG80211)
	if (status != _SUCCESS)
		rtw_cfg80211_dev_res_free(dvobj);
#endif

	return status;
}

void rtw_os_ndevs_free(struct dvobj_priv *dvobj)
{
	int i;
	_adapter *adapter = NULL;

	for (i = 0; i < dvobj->iface_nums; i++) {

		if (i >= CONFIG_IFACE_NUMBER) {
			RTW_ERR("%s %d >= CONFIG_IFACE_NUMBER(%d)\n", __func__, i, CONFIG_IFACE_NUMBER);
			rtw_warn_on(1);
			continue;
		}

		adapter = dvobj->padapters[i];

		if (adapter == NULL)
			continue;

		rtw_os_ndev_free(adapter);
	}

#if defined(CONFIG_IOCTL_CFG80211)
	rtw_cfg80211_dev_res_free(dvobj);
#endif
}

u32 rtw_start_drv_threads(_adapter *padapter)
{
	u32 _status = _SUCCESS;

	RTW_INFO(FUNC_ADPT_FMT" enter\n", FUNC_ADPT_ARG(padapter));

#ifdef CONFIG_XMIT_THREAD_MODE
#if defined(CONFIG_SDIO_HCI)
	if (is_primary_adapter(padapter))
#endif
	{
		if (padapter->xmitThread == NULL) {
			RTW_INFO(FUNC_ADPT_FMT " start RTW_XMIT_THREAD\n", FUNC_ADPT_ARG(padapter));
			padapter->xmitThread = kthread_run(rtw_xmit_thread, padapter, "RTW_XMIT_THREAD");
			if (IS_ERR(padapter->xmitThread)) {
				padapter->xmitThread = NULL;
				_status = _FAIL;
			}
		}
	}
#endif /* #ifdef CONFIG_XMIT_THREAD_MODE */

#ifdef CONFIG_RECV_THREAD_MODE
	if (is_primary_adapter(padapter)) {
		if (padapter->recvThread == NULL) {
			RTW_INFO(FUNC_ADPT_FMT " start RTW_RECV_THREAD\n", FUNC_ADPT_ARG(padapter));
			padapter->recvThread = kthread_run(rtw_recv_thread, padapter, "RTW_RECV_THREAD");
			if (IS_ERR(padapter->recvThread)) {
				padapter->recvThread = NULL;
				_status = _FAIL;
			}
		}
	}
#endif

	if (is_primary_adapter(padapter)) {
		if (padapter->cmdThread == NULL) {
			RTW_INFO(FUNC_ADPT_FMT " start RTW_CMD_THREAD\n", FUNC_ADPT_ARG(padapter));
			padapter->cmdThread = kthread_run(rtw_cmd_thread, padapter, "RTW_CMD_THREAD");
			if (IS_ERR(padapter->cmdThread)) {
				padapter->cmdThread = NULL;
				_status = _FAIL;
			}
			else
				_rtw_down_sema(&padapter->cmdpriv.start_cmdthread_sema); /* wait for cmd_thread to run */
		}
	}


#ifdef CONFIG_EVENT_THREAD_MODE
	if (padapter->evtThread == NULL) {
		RTW_INFO(FUNC_ADPT_FMT " start RTW_EVENT_THREAD\n", FUNC_ADPT_ARG(padapter));
		padapter->evtThread = kthread_run(event_thread, padapter, "RTW_EVENT_THREAD");
		if (IS_ERR(padapter->evtThread)) {
			padapter->evtThread = NULL;
			_status = _FAIL;
		}
	}
#endif

	rtw_hal_start_thread(padapter);
	return _status;

}

void rtw_stop_drv_threads(_adapter *padapter)
{
	RTW_INFO(FUNC_ADPT_FMT" enter\n", FUNC_ADPT_ARG(padapter));
	if (is_primary_adapter(padapter))
		rtw_stop_cmd_thread(padapter);

#ifdef CONFIG_EVENT_THREAD_MODE
	if (padapter->evtThread) {
		_rtw_up_sema(&padapter->evtpriv.evt_notify);
		rtw_thread_stop(padapter->evtThread);
		padapter->evtThread = NULL;
	}
#endif

#ifdef CONFIG_XMIT_THREAD_MODE
	/* Below is to termindate tx_thread... */
#if defined(CONFIG_SDIO_HCI)
	/* Only wake-up primary adapter */
	if (is_primary_adapter(padapter))
#endif  /*SDIO_HCI */
	{
		if (padapter->xmitThread) {
			_rtw_up_sema(&padapter->xmitpriv.xmit_sema);
			rtw_thread_stop(padapter->xmitThread);
			padapter->xmitThread = NULL;
		}
	}
#endif

#ifdef CONFIG_RECV_THREAD_MODE
	if (is_primary_adapter(padapter) && padapter->recvThread) {
		/* Below is to termindate rx_thread... */
		_rtw_up_sema(&padapter->recvpriv.recv_sema);
		rtw_thread_stop(padapter->recvThread);
		padapter->recvThread = NULL;
	}
#endif

	rtw_hal_stop_thread(padapter);
}

u8 rtw_init_default_value(_adapter *padapter)
{
	u8 ret  = _SUCCESS;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	/* xmit_priv */
	pxmitpriv->vcs_setting = pregistrypriv->vrtl_carrier_sense;
	pxmitpriv->vcs = pregistrypriv->vcs_type;
	pxmitpriv->vcs_type = pregistrypriv->vcs_type;
	/* pxmitpriv->rts_thresh = pregistrypriv->rts_thresh; */
	pxmitpriv->frag_len = pregistrypriv->frag_thresh;

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
	psecuritypriv->dot118021x_bmc_cam_id = INVALID_SEC_MAC_CAM_ID;


	/* pwrctrl_priv */


	/* registry_priv */
	rtw_init_registrypriv_dev_network(padapter);
	rtw_update_registrypriv_dev_network(padapter);


	/* hal_priv */
	rtw_hal_def_value_init(padapter);

#ifdef CONFIG_MCC_MODE
	/* MCC parameter */
	rtw_hal_mcc_parameter_init(padapter);
#endif /* CONFIG_MCC_MODE */

	/* misc. */
	RTW_ENABLE_FUNC(padapter, DF_RX_BIT);
	RTW_ENABLE_FUNC(padapter, DF_TX_BIT);
	padapter->bLinkInfoDump = 0;
	padapter->bNotifyChannelChange = _FALSE;
#ifdef CONFIG_P2P
	padapter->bShowGetP2PState = 1;
#endif

	/* for debug purpose */
	padapter->fix_rate = 0xFF;
	padapter->data_fb = 0;
	padapter->fix_bw = 0xFF;
	padapter->power_offset = 0;
	padapter->rsvd_page_offset = 0;
	padapter->rsvd_page_num = 0;
#ifdef CONFIG_AP_MODE
	padapter->bmc_tx_rate = pregistrypriv->bmc_tx_rate;
#endif
	padapter->driver_tx_bw_mode = pregistrypriv->tx_bw_mode;

	padapter->driver_ampdu_spacing = 0xFF;
	padapter->driver_rx_ampdu_factor =  0xFF;
	padapter->driver_rx_ampdu_spacing = 0xFF;
	padapter->fix_rx_ampdu_accept = RX_AMPDU_ACCEPT_INVALID;
	padapter->fix_rx_ampdu_size = RX_AMPDU_SIZE_INVALID;
#ifdef CONFIG_TX_AMSDU
	padapter->tx_amsdu = 2;
	padapter->tx_amsdu_rate = 400;
#endif
	padapter->driver_tx_max_agg_num = 0xFF;
#ifdef DBG_RX_COUNTER_DUMP
	padapter->dump_rx_cnt_mode = 0;
	padapter->drv_rx_cnt_ok = 0;
	padapter->drv_rx_cnt_crcerror = 0;
	padapter->drv_rx_cnt_drop = 0;
#endif
#ifdef CONFIG_RTW_NAPI
	padapter->napi_state = NAPI_DISABLE;
#endif

#ifdef CONFIG_RTW_ACS
	if (pregistrypriv->acs_mode)
		rtw_acs_start(padapter);
	else
		rtw_acs_stop(padapter);
#endif
#ifdef CONFIG_BACKGROUND_NOISE_MONITOR
	if (pregistrypriv->nm_mode)
		rtw_nm_enable(padapter);
	else
		rtw_nm_disable(padapter);
#endif

#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
	ATOMIC_SET(&padapter->tbtx_tx_pause, _FALSE);
	ATOMIC_SET(&padapter->tbtx_remove_tx_pause, _FALSE);
	padapter->tbtx_capability = _TRUE;
#endif

	return ret;
}
#ifdef CONFIG_CLIENT_PORT_CFG
extern void rtw_clt_port_init(struct clt_port_t  *cltp);
extern void rtw_clt_port_deinit(struct clt_port_t  *cltp);
#endif

struct dvobj_priv *devobj_init(void)
{
	struct dvobj_priv *pdvobj = NULL;

	pdvobj = (struct dvobj_priv *)rtw_zmalloc(sizeof(*pdvobj));
	if (pdvobj == NULL)
		return NULL;

	_rtw_mutex_init(&pdvobj->hw_init_mutex);
	_rtw_mutex_init(&pdvobj->h2c_fwcmd_mutex);
	_rtw_mutex_init(&pdvobj->setch_mutex);
	_rtw_mutex_init(&pdvobj->setbw_mutex);
	_rtw_mutex_init(&pdvobj->rf_read_reg_mutex);
	_rtw_mutex_init(&pdvobj->ioctrl_mutex);
#ifdef CONFIG_SDIO_INDIRECT_ACCESS
	_rtw_mutex_init(&pdvobj->sd_indirect_access_mutex);
#endif
#ifdef CONFIG_SYSON_INDIRECT_ACCESS
	_rtw_mutex_init(&pdvobj->syson_indirect_access_mutex);
#endif
#ifdef CONFIG_RTW_CUSTOMER_STR
	_rtw_mutex_init(&pdvobj->customer_str_mutex);
	_rtw_memset(pdvobj->customer_str, 0xFF, RTW_CUSTOMER_STR_LEN);
#endif
#ifdef CONFIG_PROTSEL_PORT
	_rtw_mutex_init(&pdvobj->protsel_port.mutex);
#endif
#ifdef CONFIG_PROTSEL_ATIMDTIM
	_rtw_mutex_init(&pdvobj->protsel_atimdtim.mutex);
#endif
#ifdef CONFIG_PROTSEL_MACSLEEP
	_rtw_mutex_init(&pdvobj->protsel_macsleep.mutex);
#endif

	pdvobj->processing_dev_remove = _FALSE;

	ATOMIC_SET(&pdvobj->disable_func, 0);

	rtw_macid_ctl_init(&pdvobj->macid_ctl);
#ifdef CONFIG_CLIENT_PORT_CFG
	rtw_clt_port_init(&pdvobj->clt_port);
#endif
	_rtw_spinlock_init(&pdvobj->cam_ctl.lock);
	_rtw_mutex_init(&pdvobj->cam_ctl.sec_cam_access_mutex);
#if defined(RTK_129X_PLATFORM) && defined(CONFIG_PCI_HCI)
	_rtw_spinlock_init(&pdvobj->io_reg_lock);
#endif
#ifdef CONFIG_MBSSID_CAM
	rtw_mbid_cam_init(pdvobj);
#endif

#ifdef CONFIG_AP_MODE
	#ifdef CONFIG_SUPPORT_MULTI_BCN
	pdvobj->nr_ap_if = 0;
	pdvobj->inter_bcn_space = DEFAULT_BCN_INTERVAL; /* default value is equal to the default beacon_interval (100ms) */
	_rtw_init_queue(&pdvobj->ap_if_q);
	pdvobj->vap_map = 0;
	#endif /*CONFIG_SUPPORT_MULTI_BCN*/
	#ifdef CONFIG_SWTIMER_BASED_TXBCN
	rtw_init_timer(&(pdvobj->txbcn_timer), NULL, tx_beacon_timer_handlder, pdvobj);
	#endif
#endif

	rtw_init_timer(&(pdvobj->dynamic_chk_timer), NULL, rtw_dynamic_check_timer_handlder, pdvobj);
	rtw_init_timer(&(pdvobj->periodic_tsf_update_end_timer), NULL, rtw_hal_periodic_tsf_update_end_timer_hdl, pdvobj);

#ifdef CONFIG_MCC_MODE
	_rtw_mutex_init(&(pdvobj->mcc_objpriv.mcc_mutex));
	_rtw_mutex_init(&(pdvobj->mcc_objpriv.mcc_tsf_req_mutex));
	_rtw_mutex_init(&(pdvobj->mcc_objpriv.mcc_dbg_reg_mutex));
	_rtw_spinlock_init(&pdvobj->mcc_objpriv.mcc_lock);
#endif /* CONFIG_MCC_MODE */

#ifdef CONFIG_RTW_NAPI_DYNAMIC
	pdvobj->en_napi_dynamic = 0;
#endif /* CONFIG_RTW_NAPI_DYNAMIC */


#ifdef CONFIG_RTW_TPT_MODE
	pdvobj->tpt_mode = 0;
	pdvobj->edca_be_ul = 0x5ea42b;
	pdvobj->edca_be_dl = 0x00a42b;
#endif 
	pdvobj->scan_deny = _FALSE;

	return pdvobj;

}

void devobj_deinit(struct dvobj_priv *pdvobj)
{
	if (!pdvobj)
		return;

	/* TODO: use rtw_os_ndevs_deinit instead at the first stage of driver's dev deinit function */
#if defined(CONFIG_IOCTL_CFG80211)
	rtw_cfg80211_dev_res_free(pdvobj);
#endif

#ifdef CONFIG_MCC_MODE
	_rtw_mutex_free(&(pdvobj->mcc_objpriv.mcc_mutex));
	_rtw_mutex_free(&(pdvobj->mcc_objpriv.mcc_tsf_req_mutex));
	_rtw_mutex_free(&(pdvobj->mcc_objpriv.mcc_dbg_reg_mutex));
	_rtw_spinlock_free(&pdvobj->mcc_objpriv.mcc_lock);
#endif /* CONFIG_MCC_MODE */

	_rtw_mutex_free(&pdvobj->hw_init_mutex);
	_rtw_mutex_free(&pdvobj->h2c_fwcmd_mutex);

#ifdef CONFIG_RTW_CUSTOMER_STR
	_rtw_mutex_free(&pdvobj->customer_str_mutex);
#endif
#ifdef CONFIG_PROTSEL_PORT
	_rtw_mutex_free(&pdvobj->protsel_port.mutex);
#endif
#ifdef CONFIG_PROTSEL_ATIMDTIM
	_rtw_mutex_free(&pdvobj->protsel_atimdtim.mutex);
#endif
#ifdef CONFIG_PROTSEL_MACSLEEP
	_rtw_mutex_free(&pdvobj->protsel_macsleep.mutex);
#endif

	_rtw_mutex_free(&pdvobj->setch_mutex);
	_rtw_mutex_free(&pdvobj->setbw_mutex);
	_rtw_mutex_free(&pdvobj->rf_read_reg_mutex);
	_rtw_mutex_free(&pdvobj->ioctrl_mutex);
#ifdef CONFIG_SDIO_INDIRECT_ACCESS
	_rtw_mutex_free(&pdvobj->sd_indirect_access_mutex);
#endif
#ifdef CONFIG_SYSON_INDIRECT_ACCESS
	_rtw_mutex_free(&pdvobj->syson_indirect_access_mutex);
#endif

	rtw_macid_ctl_deinit(&pdvobj->macid_ctl);
#ifdef CONFIG_CLIENT_PORT_CFG
	rtw_clt_port_deinit(&pdvobj->clt_port);
#endif

	_rtw_spinlock_free(&pdvobj->cam_ctl.lock);
	_rtw_mutex_free(&pdvobj->cam_ctl.sec_cam_access_mutex);

#if defined(RTK_129X_PLATFORM) && defined(CONFIG_PCI_HCI)
	_rtw_spinlock_free(&pdvobj->io_reg_lock);
#endif
#ifdef CONFIG_MBSSID_CAM
	rtw_mbid_cam_deinit(pdvobj);
#endif
#ifdef CONFIG_SUPPORT_MULTI_BCN
	_rtw_spinlock_free(&(pdvobj->ap_if_q.lock));
#endif
	rtw_mfree((u8 *)pdvobj, sizeof(*pdvobj));
}

inline u8 rtw_rtnl_lock_needed(struct dvobj_priv *dvobj)
{
	if (dvobj->rtnl_lock_holder && dvobj->rtnl_lock_holder == current)
		return 0;
	return 1;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 26))
static inline int rtnl_is_locked(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 17))
	if (unlikely(rtnl_trylock())) {
		rtnl_unlock();
#else
	if (unlikely(down_trylock(&rtnl_sem) == 0)) {
		up(&rtnl_sem);
#endif
		return 0;
	}
	return 1;
}
#endif

inline void rtw_set_rtnl_lock_holder(struct dvobj_priv *dvobj, _thread_hdl_ thd_hdl)
{
	rtw_warn_on(!rtnl_is_locked());

	if (!thd_hdl || rtnl_is_locked())
		dvobj->rtnl_lock_holder = thd_hdl;

	if (dvobj->rtnl_lock_holder && 0)
		RTW_INFO("rtnl_lock_holder: %s:%d\n", current->comm, current->pid);
}

u8 rtw_reset_drv_sw(_adapter *padapter)
{
	u8	ret8 = _SUCCESS;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	/* hal_priv */
	rtw_hal_def_value_init(padapter);

	RTW_ENABLE_FUNC(padapter, DF_RX_BIT);
	RTW_ENABLE_FUNC(padapter, DF_TX_BIT);

	padapter->bLinkInfoDump = 0;

	padapter->xmitpriv.tx_pkts = 0;
	padapter->recvpriv.rx_pkts = 0;

	pmlmepriv->LinkDetectInfo.bBusyTraffic = _FALSE;

	/* pmlmepriv->LinkDetectInfo.TrafficBusyState = _FALSE; */
	pmlmepriv->LinkDetectInfo.TrafficTransitionCount = 0;
	pmlmepriv->LinkDetectInfo.LowPowerTransitionCount = 0;

	_clr_fwstate_(pmlmepriv, WIFI_UNDER_SURVEY | WIFI_UNDER_LINKING);

#ifdef DBG_CONFIG_ERROR_DETECT
	if (is_primary_adapter(padapter))
		rtw_hal_sreset_reset_value(padapter);
#endif
	pwrctrlpriv->pwr_state_check_cnts = 0;

	/* mlmeextpriv */
	mlmeext_set_scan_state(&padapter->mlmeextpriv, SCAN_DISABLE);

#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	rtw_set_signal_stat_timer(&padapter->recvpriv);
#endif

	return ret8;
}


u8 rtw_init_drv_sw(_adapter *padapter)
{
	u8	ret8 = _SUCCESS;

#ifdef CONFIG_RTW_CFGVENDOR_RANDOM_MAC_OUI
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);
#endif

	#if defined(CONFIG_AP_MODE) && defined(CONFIG_SUPPORT_MULTI_BCN)
	_rtw_init_listhead(&padapter->list);
	#ifdef CONFIG_FW_HANDLE_TXBCN
	padapter->vap_id = CONFIG_LIMITED_AP_NUM;
	if (is_primary_adapter(padapter))
		adapter_to_dvobj(padapter)->vap_tbtt_rpt_map = adapter_to_regsty(padapter)->fw_tbtt_rpt;
	#endif
	#endif

	#ifdef CONFIG_CLIENT_PORT_CFG
	padapter->client_id = MAX_CLIENT_PORT_NUM;
	padapter->client_port = CLT_PORT_INVALID;
	#endif

	if (is_primary_adapter(padapter)) {
		struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
		struct hal_spec_t *hal_spec = GET_HAL_SPEC(padapter);

		dvobj->macid_ctl.num = rtw_min(hal_spec->macid_num, MACID_NUM_SW_LIMIT);
		dvobj->macid_ctl.macid_cap = hal_spec->macid_cap;
		dvobj->macid_ctl.macid_txrpt = hal_spec->macid_txrpt;
		dvobj->macid_ctl.macid_txrpt_pgsz = hal_spec->macid_txrpt_pgsz;
		dvobj->cam_ctl.sec_cap = hal_spec->sec_cap;
		dvobj->cam_ctl.num = rtw_min(hal_spec->sec_cam_ent_num, SEC_CAM_ENT_NUM_SW_LIMIT);
		
		dvobj->wow_ctl.wow_cap = hal_spec->wow_cap;
		
		#if CONFIG_TX_AC_LIFETIME
		{
			struct registry_priv *regsty = adapter_to_regsty(padapter);
			int i;

			dvobj->tx_aclt_flags = regsty->tx_aclt_flags;
			for (i = 0; i < TX_ACLT_CONF_NUM; i++) {
				dvobj->tx_aclt_confs[i].en = regsty->tx_aclt_confs[i].en;
				dvobj->tx_aclt_confs[i].vo_vi
					= regsty->tx_aclt_confs[i].vo_vi / (hal_spec->tx_aclt_unit_factor * 32);
				if (dvobj->tx_aclt_confs[i].vo_vi > 0xFFFF)
					dvobj->tx_aclt_confs[i].vo_vi = 0xFFFF;
				dvobj->tx_aclt_confs[i].be_bk
					= regsty->tx_aclt_confs[i].be_bk / (hal_spec->tx_aclt_unit_factor * 32);
				if (dvobj->tx_aclt_confs[i].be_bk > 0xFFFF)
					dvobj->tx_aclt_confs[i].be_bk = 0xFFFF;
			}

			dvobj->tx_aclt_force_val.en = 0xFF;
		}
		#endif
	}

	ret8 = rtw_init_default_value(padapter);

	if ((rtw_init_cmd_priv(&padapter->cmdpriv)) == _FAIL) {
		ret8 = _FAIL;
		goto exit;
	}

	padapter->cmdpriv.padapter = padapter;

	if ((rtw_init_evt_priv(&padapter->evtpriv)) == _FAIL) {
		ret8 = _FAIL;
		goto exit;
	}

	if (is_primary_adapter(padapter))
		rtw_rfctl_init(padapter);

	if (is_primary_adapter(padapter)) {
		if (rtw_hal_rfpath_init(padapter) == _FAIL) {
			ret8 = _FAIL;
			goto exit;
		}
		if (rtw_hal_trxnss_init(padapter) == _FAIL) {
			ret8 = _FAIL;
			goto exit;
		}
	}

	if (rtw_init_mlme_priv(padapter) == _FAIL) {
		ret8 = _FAIL;
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
	if (rtw_init_wifi_display_info(padapter) == _FAIL)
		RTW_ERR("Can't init init_wifi_display_info\n");
#endif
#endif /* CONFIG_P2P */

	if (init_mlme_ext_priv(padapter) == _FAIL) {
		ret8 = _FAIL;
		goto exit;
	}

#ifdef CONFIG_TDLS
	if (rtw_init_tdls_info(padapter) == _FAIL) {
		RTW_INFO("Can't rtw_init_tdls_info\n");
		ret8 = _FAIL;
		goto exit;
	}
#endif /* CONFIG_TDLS */

#ifdef CONFIG_RTW_MESH
	rtw_mesh_cfg_init(padapter);
#endif

	if (_rtw_init_xmit_priv(&padapter->xmitpriv, padapter) == _FAIL) {
		RTW_INFO("Can't _rtw_init_xmit_priv\n");
		ret8 = _FAIL;
		goto exit;
	}

	if (_rtw_init_recv_priv(&padapter->recvpriv, padapter) == _FAIL) {
		RTW_INFO("Can't _rtw_init_recv_priv\n");
		ret8 = _FAIL;
		goto exit;
	}
	/* add for CONFIG_IEEE80211W, none 11w also can use */
	_rtw_spinlock_init(&padapter->security_key_mutex);

	/* We don't need to memset padapter->XXX to zero, because adapter is allocated by rtw_zvmalloc(). */
	/* _rtw_memset((unsigned char *)&padapter->securitypriv, 0, sizeof (struct security_priv)); */

	if (_rtw_init_sta_priv(&padapter->stapriv) == _FAIL) {
		RTW_INFO("Can't _rtw_init_sta_priv\n");
		ret8 = _FAIL;
		goto exit;
	}

	padapter->setband = WIFI_FREQUENCY_BAND_AUTO;
	padapter->fix_rate = 0xFF;
	padapter->power_offset = 0;
	padapter->rsvd_page_offset = 0;
	padapter->rsvd_page_num = 0;

	padapter->data_fb = 0;
	padapter->fix_rx_ampdu_accept = RX_AMPDU_ACCEPT_INVALID;
	padapter->fix_rx_ampdu_size = RX_AMPDU_SIZE_INVALID;
#ifdef DBG_RX_COUNTER_DUMP
	padapter->dump_rx_cnt_mode = 0;
	padapter->drv_rx_cnt_ok = 0;
	padapter->drv_rx_cnt_crcerror = 0;
	padapter->drv_rx_cnt_drop = 0;
#endif
	rtw_init_bcmc_stainfo(padapter);

	rtw_init_pwrctrl_priv(padapter);

	/* _rtw_memset((u8 *)&padapter->qospriv, 0, sizeof (struct qos_priv)); */ /* move to mlme_priv */

#ifdef CONFIG_MP_INCLUDED
	if (init_mp_priv(padapter) == _FAIL)
		RTW_INFO("%s: initialize MP private data Fail!\n", __func__);
#endif

	rtw_hal_dm_init(padapter);
#ifdef CONFIG_RTW_SW_LED
	rtw_hal_sw_led_init(padapter);
#endif
#ifdef DBG_CONFIG_ERROR_DETECT
	rtw_hal_sreset_init(padapter);
#endif

#ifdef CONFIG_WAPI_SUPPORT
	padapter->WapiSupport = true; /* set true temp, will revise according to Efuse or Registry value later. */
	rtw_wapi_init(padapter);
#endif

#ifdef CONFIG_BR_EXT
	_rtw_spinlock_init(&padapter->br_ext_lock);
#endif /* CONFIG_BR_EXT */

#ifdef CONFIG_BEAMFORMING
#ifdef RTW_BEAMFORMING_VERSION_2
	rtw_bf_init(padapter);
#endif /* RTW_BEAMFORMING_VERSION_2 */
#endif /* CONFIG_BEAMFORMING */

#ifdef CONFIG_RTW_REPEATER_SON
	init_rtw_rson_data(adapter_to_dvobj(padapter));
#endif

#ifdef CONFIG_RTW_80211K
	rtw_init_rm(padapter);
#endif

#ifdef CONFIG_RTW_CFGVENDOR_RANDOM_MAC_OUI
	memset(pwdev_priv->pno_mac_addr, 0xFF, ETH_ALEN);
#endif

exit:



	return ret8;

}

#ifdef CONFIG_WOWLAN
void rtw_cancel_dynamic_chk_timer(_adapter *padapter)
{
	_cancel_timer_ex(&adapter_to_dvobj(padapter)->dynamic_chk_timer);
}
#endif

void rtw_cancel_all_timer(_adapter *padapter)
{

	_cancel_timer_ex(&padapter->mlmepriv.assoc_timer);

	_cancel_timer_ex(&padapter->mlmepriv.scan_to_timer);

#ifdef CONFIG_DFS_MASTER
	_cancel_timer_ex(&adapter_to_rfctl(padapter)->radar_detect_timer);
#endif

	_cancel_timer_ex(&adapter_to_dvobj(padapter)->dynamic_chk_timer);
	_cancel_timer_ex(&adapter_to_dvobj(padapter)->periodic_tsf_update_end_timer);
#ifdef CONFIG_RTW_SW_LED
	/* cancel sw led timer */
	rtw_hal_sw_led_deinit(padapter);
#endif
	_cancel_timer_ex(&(adapter_to_pwrctl(padapter)->pwr_state_check_timer));

#ifdef CONFIG_TX_AMSDU
	_cancel_timer_ex(&padapter->xmitpriv.amsdu_bk_timer);
	_cancel_timer_ex(&padapter->xmitpriv.amsdu_be_timer);
	_cancel_timer_ex(&padapter->xmitpriv.amsdu_vo_timer);
	_cancel_timer_ex(&padapter->xmitpriv.amsdu_vi_timer);
#endif

#ifdef CONFIG_IOCTL_CFG80211
#ifdef CONFIG_P2P
	_cancel_timer_ex(&padapter->cfg80211_wdinfo.remain_on_ch_timer);
#endif /* CONFIG_P2P */
#endif /* CONFIG_IOCTL_CFG80211 */

#ifdef CONFIG_SET_SCAN_DENY_TIMER
	_cancel_timer_ex(&padapter->mlmepriv.set_scan_deny_timer);
	rtw_clear_scan_deny(padapter);
#endif

#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	_cancel_timer_ex(&padapter->recvpriv.signal_stat_timer);
#endif

#ifdef CONFIG_LPS_RPWM_TIMER
	_cancel_timer_ex(&(adapter_to_pwrctl(padapter)->pwr_rpwm_timer));
#endif /* CONFIG_LPS_RPWM_TIMER */

#ifdef CONFIG_RTW_TOKEN_BASED_XMIT	
	_cancel_timer_ex(&padapter->mlmeextpriv.tbtx_xmit_timer);
	_cancel_timer_ex(&padapter->mlmeextpriv.tbtx_token_dispatch_timer);
#endif

	/* cancel dm timer */
	rtw_hal_dm_deinit(padapter);

#ifdef CONFIG_PLATFORM_FS_MX61
	msleep(50);
#endif
}

u8 rtw_free_drv_sw(_adapter *padapter)
{

#ifdef CONFIG_WAPI_SUPPORT
	rtw_wapi_free(padapter);
#endif

	/* we can call rtw_p2p_enable here, but: */
	/* 1. rtw_p2p_enable may have IO operation */
	/* 2. rtw_p2p_enable is bundled with wext interface */
	#ifdef CONFIG_P2P
	{
		struct wifidirect_info *pwdinfo = &padapter->wdinfo;
		if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)) {
			_cancel_timer_ex(&pwdinfo->find_phase_timer);
			_cancel_timer_ex(&pwdinfo->restore_p2p_state_timer);
			_cancel_timer_ex(&pwdinfo->pre_tx_scan_timer);
			#ifdef CONFIG_CONCURRENT_MODE
			_cancel_timer_ex(&pwdinfo->ap_p2p_switch_timer);
			#endif /* CONFIG_CONCURRENT_MODE */
			rtw_p2p_set_state(pwdinfo, P2P_STATE_NONE);
		}
	}
	#endif
	/* add for CONFIG_IEEE80211W, none 11w also can use */
	_rtw_spinlock_free(&padapter->security_key_mutex);

#ifdef CONFIG_BR_EXT
	_rtw_spinlock_free(&padapter->br_ext_lock);
#endif /* CONFIG_BR_EXT */

	free_mlme_ext_priv(&padapter->mlmeextpriv);

#ifdef CONFIG_TDLS
	/* rtw_free_tdls_info(&padapter->tdlsinfo); */
#endif /* CONFIG_TDLS */

#ifdef CONFIG_RTW_80211K
	rtw_free_rm_priv(padapter);
#endif

	rtw_free_cmd_priv(&padapter->cmdpriv);

	rtw_free_evt_priv(&padapter->evtpriv);

	rtw_free_mlme_priv(&padapter->mlmepriv);

	if (is_primary_adapter(padapter))
		rtw_rfctl_deinit(padapter);

	/* free_io_queue(padapter); */

	_rtw_free_xmit_priv(&padapter->xmitpriv);

	_rtw_free_sta_priv(&padapter->stapriv); /* will free bcmc_stainfo here */

	_rtw_free_recv_priv(&padapter->recvpriv);

	rtw_free_pwrctrl_priv(padapter);

	/* rtw_mfree((void *)padapter, sizeof (padapter)); */

	rtw_hal_free_data(padapter);

	return _SUCCESS;

}
void rtw_intf_start(_adapter *adapter)
{
	if (adapter->intf_start)
		adapter->intf_start(adapter);
	GET_HAL_DATA(adapter)->intf_start = 1;
}
void rtw_intf_stop(_adapter *adapter)
{
	if (adapter->intf_stop)
		adapter->intf_stop(adapter);
	GET_HAL_DATA(adapter)->intf_start = 0;
}

#ifdef CONFIG_CONCURRENT_MODE
#ifndef CONFIG_NEW_NETDEV_HDL
int _netdev_vir_if_open(struct net_device *pnetdev)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	_adapter *primary_padapter = GET_PRIMARY_ADAPTER(padapter);

	RTW_INFO(FUNC_NDEV_FMT" , bup=%d\n", FUNC_NDEV_ARG(pnetdev), padapter->bup);

	if (!primary_padapter)
		goto _netdev_virtual_iface_open_error;

#ifdef CONFIG_PLATFORM_INTEL_BYT
	if (padapter->bup == _FALSE) {
		u8 mac[ETH_ALEN];

		/* get mac address from primary_padapter */
		if (primary_padapter->bup == _FALSE)
			rtw_macaddr_cfg(adapter_mac_addr(primary_padapter), get_hal_mac_addr(primary_padapter));

		_rtw_memcpy(mac, adapter_mac_addr(primary_padapter), ETH_ALEN);

		/*
		* If the BIT1 is 0, the address is universally administered.
		* If it is 1, the address is locally administered
		*/
		mac[0] |= BIT(1);

		_rtw_memcpy(adapter_mac_addr(padapter), mac, ETH_ALEN);

#ifdef CONFIG_MI_WITH_MBSSID_CAM
		rtw_mbid_camid_alloc(padapter, adapter_mac_addr(padapter));
#endif
		rtw_init_wifidirect_addrs(padapter, adapter_mac_addr(padapter), adapter_mac_addr(padapter));
		_rtw_memcpy(pnetdev->dev_addr, adapter_mac_addr(padapter), ETH_ALEN);
	}
#endif /*CONFIG_PLATFORM_INTEL_BYT*/

	if (primary_padapter->bup == _FALSE || !rtw_is_hw_init_completed(primary_padapter))
		_netdev_open(primary_padapter->pnetdev);

	if (padapter->bup == _FALSE && primary_padapter->bup == _TRUE &&
	    rtw_is_hw_init_completed(primary_padapter)) {
#if 0 /*#ifdef CONFIG_MI_WITH_MBSSID_CAM*/
		rtw_hal_set_hwreg(padapter, HW_VAR_MAC_ADDR, adapter_mac_addr(padapter)); /* set mac addr to mac register */
#endif

	}

	if (padapter->bup == _FALSE) {
		if (rtw_start_drv_threads(padapter) == _FAIL)
			goto _netdev_virtual_iface_open_error;
	}

#ifdef CONFIG_RTW_NAPI
	if (padapter->napi_state == NAPI_DISABLE) {
		napi_enable(&padapter->napi);
		padapter->napi_state = NAPI_ENABLE;
	}
#endif

#ifdef CONFIG_IOCTL_CFG80211
	rtw_cfg80211_init_wiphy(padapter);
	rtw_cfg80211_init_wdev_data(padapter);
#endif

	padapter->bup = _TRUE;

	padapter->net_closed = _FALSE;

	rtw_netif_wake_queue(pnetdev);

	RTW_INFO(FUNC_NDEV_FMT" (bup=%d) exit\n", FUNC_NDEV_ARG(pnetdev), padapter->bup);

	return 0;

_netdev_virtual_iface_open_error:

	padapter->bup = _FALSE;

#ifdef CONFIG_RTW_NAPI
	if(padapter->napi_state == NAPI_ENABLE) {
		napi_disable(&padapter->napi);
		padapter->napi_state = NAPI_DISABLE;
	}
#endif

	rtw_netif_carrier_off(pnetdev);
	rtw_netif_stop_queue(pnetdev);

	return -1;

}

int netdev_vir_if_open(struct net_device *pnetdev)
{
	int ret;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);

	_enter_critical_mutex(&(adapter_to_dvobj(padapter)->hw_init_mutex), NULL);
	ret = _netdev_vir_if_open(pnetdev);
	_exit_critical_mutex(&(adapter_to_dvobj(padapter)->hw_init_mutex), NULL);

#ifdef CONFIG_AUTO_AP_MODE
	/* if(padapter->iface_id == 2) */
	/*	rtw_start_auto_ap(padapter); */
#endif

	return ret;
}

static int netdev_vir_if_close(struct net_device *pnetdev)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;

	RTW_INFO(FUNC_NDEV_FMT" , bup=%d\n", FUNC_NDEV_ARG(pnetdev), padapter->bup);
	padapter->net_closed = _TRUE;
	pmlmepriv->LinkDetectInfo.bBusyTraffic = _FALSE;

	if (pnetdev)
		rtw_netif_stop_queue(pnetdev);

#ifdef CONFIG_P2P
	if (!rtw_p2p_chk_role(&padapter->wdinfo, P2P_ROLE_DISABLE))
		rtw_p2p_enable(padapter, P2P_ROLE_DISABLE);
#endif

#ifdef CONFIG_IOCTL_CFG80211
	rtw_scan_abort(padapter);
	rtw_cfg80211_wait_scan_req_empty(padapter, 200);
	adapter_wdev_data(padapter)->bandroid_scan = _FALSE;
#endif

	return 0;
}
#endif /*#ifndef CONFIG_NEW_NETDEV_HDL*/

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
static const struct net_device_ops rtw_netdev_vir_if_ops = {
	.ndo_init = rtw_ndev_init,
	.ndo_uninit = rtw_ndev_uninit,
	#ifdef CONFIG_NEW_NETDEV_HDL
	.ndo_open = netdev_open,
	.ndo_stop = netdev_close,
	#else
	.ndo_open = netdev_vir_if_open,
	.ndo_stop = netdev_vir_if_close,
	#endif
	.ndo_start_xmit = rtw_xmit_entry,
	.ndo_set_mac_address = rtw_net_set_mac_address,
	.ndo_get_stats = rtw_net_get_stats,
	.ndo_do_ioctl = rtw_ioctl,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	.ndo_select_queue	= rtw_select_queue,
#endif
};
#endif

static void rtw_hook_vir_if_ops(struct net_device *ndev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
	ndev->netdev_ops = &rtw_netdev_vir_if_ops;
#else
	ndev->init = rtw_ndev_init;
	ndev->uninit = rtw_ndev_uninit;
	#ifdef CONFIG_NEW_NETDEV_HDL
	ndev->open = netdev_open;
	ndev->stop = netdev_close;
	#else
	ndev->open = netdev_vir_if_open;
	ndev->stop = netdev_vir_if_close;
	#endif

	ndev->set_mac_address = rtw_net_set_mac_address;
#endif
}
_adapter *rtw_drv_add_vir_if(_adapter *primary_padapter,
	void (*set_intf_ops)(_adapter *primary_padapter, struct _io_ops *pops))
{
	int res = _FAIL;
	_adapter *padapter = NULL;
	struct dvobj_priv *pdvobjpriv;
	u8 mac[ETH_ALEN];

	/****** init adapter ******/
	padapter = (_adapter *)rtw_zvmalloc(sizeof(*padapter));
	if (padapter == NULL)
		goto exit;

	if (loadparam(padapter) != _SUCCESS)
		goto free_adapter;

	_rtw_memcpy(padapter, primary_padapter, sizeof(_adapter));

	/*  */
	padapter->bup = _FALSE;
	padapter->net_closed = _TRUE;
	padapter->dir_dev = NULL;
	padapter->dir_odm = NULL;

	/*set adapter_type/iface type*/
	padapter->isprimary = _FALSE;
	padapter->adapter_type = VIRTUAL_ADAPTER;

#ifdef CONFIG_MI_WITH_MBSSID_CAM
	padapter->hw_port = HW_PORT0;
#else
	padapter->hw_port = HW_PORT1;
#endif


	/****** hook vir if into dvobj ******/
	pdvobjpriv = adapter_to_dvobj(padapter);
	padapter->iface_id = pdvobjpriv->iface_nums;
	pdvobjpriv->padapters[pdvobjpriv->iface_nums++] = padapter;

	padapter->intf_start = primary_padapter->intf_start;
	padapter->intf_stop = primary_padapter->intf_stop;

	/* step init_io_priv */
	if ((rtw_init_io_priv(padapter, set_intf_ops)) == _FAIL) {
		goto free_adapter;
	}

	/*init drv data*/
	if (rtw_init_drv_sw(padapter) != _SUCCESS)
		goto free_drv_sw;


	/*get mac address from primary_padapter*/
	_rtw_memcpy(mac, adapter_mac_addr(primary_padapter), ETH_ALEN);

	/*
	* If the BIT1 is 0, the address is universally administered.
	* If it is 1, the address is locally administered
	*/
	mac[0] |= BIT(1);
	if (padapter->iface_id > IFACE_ID1)
		mac[0] ^= ((padapter->iface_id)<<2);

	_rtw_memcpy(adapter_mac_addr(padapter), mac, ETH_ALEN);
	/* update mac-address to mbsid-cam cache*/
#ifdef CONFIG_MI_WITH_MBSSID_CAM
	rtw_mbid_camid_alloc(padapter, adapter_mac_addr(padapter));
#endif
	RTW_INFO("%s if%d mac_addr : "MAC_FMT"\n", __func__, padapter->iface_id + 1, MAC_ARG(adapter_mac_addr(padapter)));
#ifdef CONFIG_P2P
	rtw_init_wifidirect_addrs(padapter, adapter_mac_addr(padapter), adapter_mac_addr(padapter));
#endif

	rtw_led_set_ctl_en_mask_virtual(padapter);
	rtw_led_set_iface_en(padapter, 1);

	res = _SUCCESS;

free_drv_sw:
	if (res != _SUCCESS && padapter)
		rtw_free_drv_sw(padapter);
free_adapter:
	if (res != _SUCCESS && padapter) {
		rtw_vmfree((u8 *)padapter, sizeof(*padapter));
		padapter = NULL;
	}
exit:
	return padapter;
}

void rtw_drv_stop_vir_if(_adapter *padapter)
{
	struct net_device *pnetdev = NULL;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (padapter == NULL)
		return;
	RTW_INFO(FUNC_ADPT_FMT" enter\n", FUNC_ADPT_ARG(padapter));

	pnetdev = padapter->pnetdev;

	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE))
		rtw_disassoc_cmd(padapter, 0, RTW_CMDF_DIRECTLY);

#ifdef CONFIG_AP_MODE
	if (MLME_IS_AP(padapter) || MLME_IS_MESH(padapter)) {
		free_mlme_ap_info(padapter);
		#ifdef CONFIG_HOSTAPD_MLME
		hostapd_mode_unload(padapter);
		#endif
	}
#endif

	if (padapter->bup == _TRUE) {
		#ifdef CONFIG_XMIT_ACK
		if (padapter->xmitpriv.ack_tx)
			rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_DRV_STOP);
		#endif

		rtw_intf_stop(padapter);
	#ifndef CONFIG_NEW_NETDEV_HDL
		rtw_stop_drv_threads(padapter);
	#endif
		padapter->bup = _FALSE;
	}
	#ifdef CONFIG_NEW_NETDEV_HDL
	rtw_stop_drv_threads(padapter);
	#endif
	/* cancel timer after thread stop */
	rtw_cancel_all_timer(padapter);
}

void rtw_drv_free_vir_if(_adapter *padapter)
{
	if (padapter == NULL)
		return;

	RTW_INFO(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));
	rtw_free_drv_sw(padapter);

	/* TODO: use rtw_os_ndevs_deinit instead at the first stage of driver's dev deinit function */
	rtw_os_ndev_free(padapter);

	rtw_vmfree((u8 *)padapter, sizeof(_adapter));
}


void rtw_drv_stop_vir_ifaces(struct dvobj_priv *dvobj)
{
	int i;

	for (i = VIF_START_ID; i < dvobj->iface_nums; i++)
		rtw_drv_stop_vir_if(dvobj->padapters[i]);
}

void rtw_drv_free_vir_ifaces(struct dvobj_priv *dvobj)
{
	int i;

	for (i = VIF_START_ID; i < dvobj->iface_nums; i++)
		rtw_drv_free_vir_if(dvobj->padapters[i]);
}


#endif /*end of CONFIG_CONCURRENT_MODE*/

/* IPv4, IPv6 IP addr notifier */
static int rtw_inetaddr_notifier_call(struct notifier_block *nb,
				      unsigned long action, void *data)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *)data;
	struct net_device *ndev;
	struct mlme_ext_priv *pmlmeext = NULL;
	struct mlme_ext_info *pmlmeinfo = NULL;
	_adapter *adapter = NULL;

	if (!ifa || !ifa->ifa_dev || !ifa->ifa_dev->dev)
		return NOTIFY_DONE;

	ndev = ifa->ifa_dev->dev;

	if (!is_rtw_ndev(ndev))
		return NOTIFY_DONE;

	adapter = (_adapter *)rtw_netdev_priv(ifa->ifa_dev->dev);

	if (adapter == NULL)
		return NOTIFY_DONE;

	pmlmeext = &adapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;

	switch (action) {
	case NETDEV_UP:
		_rtw_memcpy(pmlmeinfo->ip_addr, &ifa->ifa_address,
					RTW_IP_ADDR_LEN);
		RTW_DBG("%s[%s]: up IP: %pI4\n", __func__,
					ifa->ifa_label, pmlmeinfo->ip_addr);
	break;
	case NETDEV_DOWN:
		_rtw_memset(pmlmeinfo->ip_addr, 0, RTW_IP_ADDR_LEN);
		RTW_DBG("%s[%s]: down IP: %pI4\n", __func__,
					ifa->ifa_label, pmlmeinfo->ip_addr);
	break;
	default:
		RTW_DBG("%s: default action\n", __func__);
	break;
	}
	return NOTIFY_DONE;
}

#ifdef CONFIG_IPV6
static int rtw_inet6addr_notifier_call(struct notifier_block *nb,
				       unsigned long action, void *data)
{
	struct inet6_ifaddr *inet6_ifa = data;
	struct net_device *ndev;
	struct pwrctrl_priv *pwrctl = NULL;
	struct mlme_ext_priv *pmlmeext = NULL;
	struct mlme_ext_info *pmlmeinfo = NULL;
	_adapter *adapter = NULL;

	if (!inet6_ifa || !inet6_ifa->idev || !inet6_ifa->idev->dev)
		return NOTIFY_DONE;

	ndev = inet6_ifa->idev->dev;

	if (!is_rtw_ndev(ndev))
		return NOTIFY_DONE;

	adapter = (_adapter *)rtw_netdev_priv(inet6_ifa->idev->dev);

	if (adapter == NULL)
		return NOTIFY_DONE;

	pmlmeext =  &adapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;
	pwrctl = adapter_to_pwrctl(adapter);

	pmlmeext = &adapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;

	switch (action) {
	case NETDEV_UP:
#ifdef CONFIG_WOWLAN
		pwrctl->wowlan_ns_offload_en = _TRUE;
#endif
		_rtw_memcpy(pmlmeinfo->ip6_addr, &inet6_ifa->addr,
					RTW_IPv6_ADDR_LEN);
		RTW_DBG("%s: up IPv6 addrs: %pI6\n", __func__,
					pmlmeinfo->ip6_addr);
			break;
	case NETDEV_DOWN:
#ifdef CONFIG_WOWLAN
		pwrctl->wowlan_ns_offload_en = _FALSE;
#endif
		_rtw_memset(pmlmeinfo->ip6_addr, 0, RTW_IPv6_ADDR_LEN);
		RTW_DBG("%s: down IPv6 addrs: %pI6\n", __func__,
					pmlmeinfo->ip6_addr);
		break;
	default:
		RTW_DBG("%s: default action\n", __func__);
		break;
	}
	return NOTIFY_DONE;
}
#endif

static struct notifier_block rtw_inetaddr_notifier = {
	.notifier_call = rtw_inetaddr_notifier_call
};

#ifdef CONFIG_IPV6
static struct notifier_block rtw_inet6addr_notifier = {
	.notifier_call = rtw_inet6addr_notifier_call
};
#endif

void rtw_inetaddr_notifier_register(void)
{
	RTW_INFO("%s\n", __func__);
	register_inetaddr_notifier(&rtw_inetaddr_notifier);
#ifdef CONFIG_IPV6
	register_inet6addr_notifier(&rtw_inet6addr_notifier);
#endif
}

void rtw_inetaddr_notifier_unregister(void)
{
	RTW_INFO("%s\n", __func__);
	unregister_inetaddr_notifier(&rtw_inetaddr_notifier);
#ifdef CONFIG_IPV6
	unregister_inet6addr_notifier(&rtw_inet6addr_notifier);
#endif
}

int rtw_os_ndevs_register(struct dvobj_priv *dvobj)
{
	int i, status = _SUCCESS;
	struct registry_priv *regsty = dvobj_to_regsty(dvobj);
	_adapter *adapter;

#if defined(CONFIG_IOCTL_CFG80211)
	if (rtw_cfg80211_dev_res_register(dvobj) != _SUCCESS) {
		rtw_warn_on(1);
		return _FAIL;
	}
#endif

	for (i = 0; i < dvobj->iface_nums; i++) {

		if (i >= CONFIG_IFACE_NUMBER) {
			RTW_ERR("%s %d >= CONFIG_IFACE_NUMBER(%d)\n", __func__, i, CONFIG_IFACE_NUMBER);
			rtw_warn_on(1);
			continue;
		}

		adapter = dvobj->padapters[i];
		if (adapter) {
			char *name;

			#ifdef CONFIG_RTW_DYNAMIC_NDEV
			if (!is_primary_adapter(adapter))
				continue;
			#endif

			if (adapter->iface_id == IFACE_ID0)
				name = regsty->ifname;
			else if (adapter->iface_id == IFACE_ID1)
				name = regsty->if2name;
			else
				name = "wlan%d";

			status = rtw_os_ndev_register(adapter, name);

			if (status != _SUCCESS) {
				rtw_warn_on(1);
				break;
			}
		}
	}

	if (status != _SUCCESS) {
		for (; i >= 0; i--) {
			adapter = dvobj->padapters[i];
			if (adapter)
				rtw_os_ndev_unregister(adapter);
		}
	}

#if defined(CONFIG_IOCTL_CFG80211)
	if (status != _SUCCESS)
		rtw_cfg80211_dev_res_unregister(dvobj);
#endif
	return status;
}

void rtw_os_ndevs_unregister(struct dvobj_priv *dvobj)
{
	int i;
	_adapter *adapter = NULL;

	for (i = 0; i < dvobj->iface_nums; i++) {
		adapter = dvobj->padapters[i];

		if (adapter == NULL)
			continue;

		rtw_os_ndev_unregister(adapter);
	}

#if defined(CONFIG_IOCTL_CFG80211)
	rtw_cfg80211_dev_res_unregister(dvobj);
#endif
}

/**
 * rtw_os_ndevs_init - Allocate and register OS layer net devices and relating structures for @dvobj
 * @dvobj: the dvobj on which this function applies
 *
 * Returns:
 * _SUCCESS or _FAIL
 */
int rtw_os_ndevs_init(struct dvobj_priv *dvobj)
{
	int ret = _FAIL;

	if (rtw_os_ndevs_alloc(dvobj) != _SUCCESS)
		goto exit;

	if (rtw_os_ndevs_register(dvobj) != _SUCCESS)
		goto os_ndevs_free;

	ret = _SUCCESS;

os_ndevs_free:
	if (ret != _SUCCESS)
		rtw_os_ndevs_free(dvobj);
exit:
	return ret;
}

/**
 * rtw_os_ndevs_deinit - Unregister and free OS layer net devices and relating structures for @dvobj
 * @dvobj: the dvobj on which this function applies
 */
void rtw_os_ndevs_deinit(struct dvobj_priv *dvobj)
{
	rtw_os_ndevs_unregister(dvobj);
	rtw_os_ndevs_free(dvobj);
}

#ifdef CONFIG_BR_EXT
void netdev_br_init(struct net_device *netdev)
{
	_adapter *adapter = (_adapter *)rtw_netdev_priv(netdev);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35))
	rcu_read_lock();
#endif

	/* if(check_fwstate(pmlmepriv, WIFI_STATION_STATE|WIFI_ADHOC_STATE) == _TRUE) */
	{
		/* struct net_bridge	*br = netdev->br_port->br; */ /* ->dev->dev_addr; */
		#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
		if (netdev->br_port)
		#else
		if (rcu_dereference(adapter->pnetdev->rx_handler_data))
		#endif
		{
			struct net_device *br_netdev;

			br_netdev = rtw_get_bridge_ndev_by_name(CONFIG_BR_EXT_BRNAME);
			if (br_netdev) {
				memcpy(adapter->br_mac, br_netdev->dev_addr, ETH_ALEN);
				dev_put(br_netdev);
				RTW_INFO(FUNC_NDEV_FMT" bind bridge dev "NDEV_FMT"("MAC_FMT")\n"
					, FUNC_NDEV_ARG(netdev), NDEV_ARG(br_netdev), MAC_ARG(br_netdev->dev_addr));
			} else {
				RTW_INFO(FUNC_NDEV_FMT" can't get bridge dev by name \"%s\"\n"
					, FUNC_NDEV_ARG(netdev), CONFIG_BR_EXT_BRNAME);
			}
		}

		adapter->ethBrExtInfo.addPPPoETag = 1;
	}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35))
	rcu_read_unlock();
#endif
}
#endif /* CONFIG_BR_EXT */

#ifdef CONFIG_NEW_NETDEV_HDL
int _netdev_open(struct net_device *pnetdev)
{
	uint status;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	RTW_INFO(FUNC_NDEV_FMT" start\n", FUNC_NDEV_ARG(pnetdev));

	if (!rtw_is_hw_init_completed(padapter)) { // ips 
		rtw_clr_surprise_removed(padapter);
		rtw_clr_drv_stopped(padapter);
		RTW_ENABLE_FUNC(padapter, DF_RX_BIT);
		RTW_ENABLE_FUNC(padapter, DF_TX_BIT);
		status = rtw_hal_init(padapter);
		if (status == _FAIL)
			goto netdev_open_error;
		rtw_led_control(padapter, LED_CTL_NO_LINK);
		#ifndef RTW_HALMAC
		status = rtw_mi_start_drv_threads(padapter);
		if (status == _FAIL) {
			RTW_ERR(FUNC_NDEV_FMT "Initialize driver thread failed!\n", FUNC_NDEV_ARG(pnetdev));
			goto netdev_open_error;
		}

		rtw_intf_start(GET_PRIMARY_ADAPTER(padapter));
		#endif /* !RTW_HALMAC */

		{
	#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
			_adapter *prim_adpt = GET_PRIMARY_ADAPTER(padapter);
		
			if (prim_adpt && (_TRUE == prim_adpt->EEPROMBluetoothCoexist)) {
				rtw_btcoex_init_socket(prim_adpt);
				prim_adpt->coex_info.BtMgnt.ExtConfig.HCIExtensionVer = 0x04;
				rtw_btcoex_SetHciVersion(prim_adpt, 0x04);
			}
	#endif /* CONFIG_BT_COEXIST_SOCKET_TRX */

			_set_timer(&adapter_to_dvobj(padapter)->dynamic_chk_timer, 2000);

	#ifndef CONFIG_IPS_CHECK_IN_WD
			rtw_set_pwr_state_check_timer(pwrctrlpriv);
	#endif /*CONFIG_IPS_CHECK_IN_WD*/
		}

	}

	/*if (padapter->bup == _FALSE) */
	{
		rtw_hal_iface_init(padapter);

		#ifdef CONFIG_RTW_NAPI
		if(padapter->napi_state == NAPI_DISABLE) {
			napi_enable(&padapter->napi);
			padapter->napi_state = NAPI_ENABLE;
		}
		#endif

		#ifdef CONFIG_IOCTL_CFG80211
		rtw_cfg80211_init_wiphy(padapter);
		rtw_cfg80211_init_wdev_data(padapter);
		#endif
		/* rtw_netif_carrier_on(pnetdev); */ /* call this func when rtw_joinbss_event_callback return success */
		rtw_netif_wake_queue(pnetdev);

		#ifdef CONFIG_BR_EXT
		if (is_primary_adapter(padapter))
			netdev_br_init(pnetdev);
		#endif /* CONFIG_BR_EXT */


		padapter->bup = _TRUE;
		padapter->net_closed = _FALSE;
		padapter->netif_up = _TRUE;
		pwrctrlpriv->bips_processing = _FALSE;
	}

	RTW_INFO(FUNC_NDEV_FMT" Success (bup=%d)\n", FUNC_NDEV_ARG(pnetdev), padapter->bup);
	return 0;

netdev_open_error:
	padapter->bup = _FALSE;

	#ifdef CONFIG_RTW_NAPI
	if(padapter->napi_state == NAPI_ENABLE) {
		napi_disable(&padapter->napi);
		padapter->napi_state = NAPI_DISABLE;
	}
	#endif

	rtw_netif_carrier_off(pnetdev);
	rtw_netif_stop_queue(pnetdev);

	RTW_ERR(FUNC_NDEV_FMT" Failed!! (bup=%d)\n", FUNC_NDEV_ARG(pnetdev), padapter->bup);

	return -1;

}

#else
int _netdev_open(struct net_device *pnetdev)
{
	uint status;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);
#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
#endif /* CONFIG_BT_COEXIST_SOCKET_TRX */


	RTW_INFO(FUNC_NDEV_FMT" , bup=%d\n", FUNC_NDEV_ARG(pnetdev), padapter->bup);

	padapter->netif_up = _TRUE;

#ifdef CONFIG_PLATFORM_INTEL_BYT
	rtw_sdio_set_power(1);
#endif /* CONFIG_PLATFORM_INTEL_BYT */

	if (padapter->bup == _FALSE) {
#ifdef CONFIG_PLATFORM_INTEL_BYT
		rtw_macaddr_cfg(adapter_mac_addr(padapter),  get_hal_mac_addr(padapter));
#ifdef CONFIG_MI_WITH_MBSSID_CAM
		rtw_mbid_camid_alloc(padapter, adapter_mac_addr(padapter));
#endif
		rtw_init_wifidirect_addrs(padapter, adapter_mac_addr(padapter), adapter_mac_addr(padapter));
		_rtw_memcpy(pnetdev->dev_addr, adapter_mac_addr(padapter), ETH_ALEN);
#endif /* CONFIG_PLATFORM_INTEL_BYT */

		rtw_clr_surprise_removed(padapter);
		rtw_clr_drv_stopped(padapter);

		status = rtw_hal_init(padapter);
		if (status == _FAIL) {
			goto netdev_open_error;
		}
#if 0/*#ifdef CONFIG_MI_WITH_MBSSID_CAM*/
		rtw_hal_set_hwreg(padapter, HW_VAR_MAC_ADDR, adapter_mac_addr(padapter)); /* set mac addr to mac register */
#endif

		RTW_INFO("MAC Address = "MAC_FMT"\n", MAC_ARG(pnetdev->dev_addr));

#ifndef RTW_HALMAC
		status = rtw_start_drv_threads(padapter);
		if (status == _FAIL) {
			RTW_INFO("Initialize driver software resource Failed!\n");
			goto netdev_open_error;
		}
#endif /* !RTW_HALMAC */

#ifdef CONFIG_RTW_NAPI
		if(padapter->napi_state == NAPI_DISABLE) {
			napi_enable(&padapter->napi);
			padapter->napi_state = NAPI_ENABLE;
		}
#endif

#ifndef RTW_HALMAC
		rtw_intf_start(padapter);
#endif /* !RTW_HALMAC */

#ifdef CONFIG_IOCTL_CFG80211
		rtw_cfg80211_init_wiphy(padapter);
		rtw_cfg80211_init_wdev_data(padapter);
#endif

		rtw_led_control(padapter, LED_CTL_NO_LINK);

		padapter->bup = _TRUE;
		pwrctrlpriv->bips_processing = _FALSE;

#ifdef CONFIG_PLATFORM_INTEL_BYT
#ifdef CONFIG_BT_COEXIST
		rtw_btcoex_IpsNotify(padapter, IPS_NONE);
#endif /* CONFIG_BT_COEXIST */
#endif /* CONFIG_PLATFORM_INTEL_BYT		 */
	}
	padapter->net_closed = _FALSE;

	_set_timer(&adapter_to_dvobj(padapter)->dynamic_chk_timer, 2000);

#ifndef CONFIG_IPS_CHECK_IN_WD
	rtw_set_pwr_state_check_timer(pwrctrlpriv);
#endif

	/* rtw_netif_carrier_on(pnetdev); */ /* call this func when rtw_joinbss_event_callback return success */
	rtw_netif_wake_queue(pnetdev);

#ifdef CONFIG_BR_EXT
	netdev_br_init(pnetdev);
#endif /* CONFIG_BR_EXT */

#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
	if (is_primary_adapter(padapter) && (_TRUE == pHalData->EEPROMBluetoothCoexist)) {
		rtw_btcoex_init_socket(padapter);
		padapter->coex_info.BtMgnt.ExtConfig.HCIExtensionVer = 0x04;
		rtw_btcoex_SetHciVersion(padapter, 0x04);
	} else
		RTW_INFO("CONFIG_BT_COEXIST: VIRTUAL_ADAPTER\n");
#endif /* CONFIG_BT_COEXIST_SOCKET_TRX */

#ifdef CONFIG_CONCURRENT_MODE
	{
		_adapter *sec_adapter = adapter_to_dvobj(padapter)->padapters[IFACE_ID1];

		#ifndef CONFIG_RTW_DYNAMIC_NDEV
		if (sec_adapter && (sec_adapter->bup == _FALSE))
			_netdev_vir_if_open(sec_adapter->pnetdev);
		#endif
	}
#endif

#ifdef CONFIG_RTW_CFGVENDOR_LLSTATS
	pwrctrlpriv->radio_on_start_time = rtw_get_current_time();
	pwrctrlpriv->pwr_saving_start_time = rtw_get_current_time();
	pwrctrlpriv->pwr_saving_time = 0;
	pwrctrlpriv->on_time = 0;
	pwrctrlpriv->tx_time = 0;
	pwrctrlpriv->rx_time = 0;
#endif /* CONFIG_RTW_CFGVEDNOR_LLSTATS */

	RTW_INFO("-871x_drv - drv_open, bup=%d\n", padapter->bup);

	return 0;

netdev_open_error:

	padapter->bup = _FALSE;

#ifdef CONFIG_RTW_NAPI
	if(padapter->napi_state == NAPI_ENABLE) {
		napi_disable(&padapter->napi);
		padapter->napi_state = NAPI_DISABLE;
	}
#endif

	rtw_netif_carrier_off(pnetdev);
	rtw_netif_stop_queue(pnetdev);

	RTW_INFO("-871x_drv - drv_open fail, bup=%d\n", padapter->bup);

	return -1;

}
#endif
int netdev_open(struct net_device *pnetdev)
{
	int ret = _FALSE;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	if (pwrctrlpriv->bInSuspend == _TRUE) {
		RTW_INFO(" [WARN] "ADPT_FMT" %s  failed, bInSuspend=%d\n", ADPT_ARG(padapter), __func__, pwrctrlpriv->bInSuspend);
		return 0;
	}

	_enter_critical_mutex(&(adapter_to_dvobj(padapter)->hw_init_mutex), NULL);
#ifdef CONFIG_NEW_NETDEV_HDL
	ret = _netdev_open(pnetdev);
#else
	if (is_primary_adapter(padapter))
		ret = _netdev_open(pnetdev);
#ifdef CONFIG_CONCURRENT_MODE
	else
		ret = _netdev_vir_if_open(pnetdev);
#endif
#endif
	_exit_critical_mutex(&(adapter_to_dvobj(padapter)->hw_init_mutex), NULL);


#ifdef CONFIG_AUTO_AP_MODE
	if (padapter->iface_id == IFACE_ID2)
		rtw_start_auto_ap(padapter);
#endif

	return ret;
}

#ifdef CONFIG_IPS
int  ips_netdrv_open(_adapter *padapter)
{
	int status = _SUCCESS;
	/* struct pwrctrl_priv	*pwrpriv = adapter_to_pwrctl(padapter); */

	padapter->net_closed = _FALSE;

	RTW_INFO("===> %s.........\n", __FUNCTION__);


	rtw_clr_drv_stopped(padapter);
	/* padapter->bup = _TRUE; */
#ifdef CONFIG_NEW_NETDEV_HDL
	if (!rtw_is_hw_init_completed(padapter)) {
		status = rtw_hal_init(padapter);
		if (status == _FAIL) {
			goto netdev_open_error;
		}
		rtw_mi_hal_iface_init(padapter);
	}
#else
	status = rtw_hal_init(padapter);
	if (status == _FAIL) {
		goto netdev_open_error;
	}
#endif
#if 0
	rtw_mi_set_mac_addr(padapter);
#endif
#ifndef RTW_HALMAC
	rtw_intf_start(padapter);
#endif /* !RTW_HALMAC */

#ifndef CONFIG_IPS_CHECK_IN_WD
	rtw_set_pwr_state_check_timer(adapter_to_pwrctl(padapter));
#endif
	_set_timer(&adapter_to_dvobj(padapter)->dynamic_chk_timer, 2000);

	return _SUCCESS;

netdev_open_error:
	/* padapter->bup = _FALSE; */
	RTW_INFO("-ips_netdrv_open - drv_open failure, bup=%d\n", padapter->bup);

	return _FAIL;
}

int rtw_ips_pwr_up(_adapter *padapter)
{
	int result;
#if defined(CONFIG_SWLPS_IN_IPS) || defined(CONFIG_FWLPS_IN_IPS)
#ifdef DBG_CONFIG_ERROR_DETECT
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;
#endif/* #ifdef DBG_CONFIG_ERROR_DETECT */
#endif /* defined(CONFIG_SWLPS_IN_IPS) || defined(CONFIG_FWLPS_IN_IPS) */
	systime start_time = rtw_get_current_time();
	RTW_INFO("===>  rtw_ips_pwr_up..............\n");

#if defined(CONFIG_SWLPS_IN_IPS) || defined(CONFIG_FWLPS_IN_IPS)
#ifdef DBG_CONFIG_ERROR_DETECT
	if (psrtpriv->silent_reset_inprogress == _TRUE)
#endif/* #ifdef DBG_CONFIG_ERROR_DETECT */
#endif /* defined(CONFIG_SWLPS_IN_IPS) || defined(CONFIG_FWLPS_IN_IPS) */
		rtw_reset_drv_sw(padapter);

	result = ips_netdrv_open(padapter);

	rtw_led_control(padapter, LED_CTL_NO_LINK);

	RTW_INFO("<===  rtw_ips_pwr_up.............. in %dms\n", rtw_get_passing_time_ms(start_time));
	return result;

}

void rtw_ips_pwr_down(_adapter *padapter)
{
	systime start_time = rtw_get_current_time();
	RTW_INFO("===> rtw_ips_pwr_down...................\n");

	padapter->net_closed = _TRUE;

	rtw_ips_dev_unload(padapter);
	RTW_INFO("<=== rtw_ips_pwr_down..................... in %dms\n", rtw_get_passing_time_ms(start_time));
}
#endif
void rtw_ips_dev_unload(_adapter *padapter)
{
#if defined(CONFIG_SWLPS_IN_IPS) || defined(CONFIG_FWLPS_IN_IPS)
#ifdef DBG_CONFIG_ERROR_DETECT
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;
#endif/* #ifdef DBG_CONFIG_ERROR_DETECT */
#endif /* defined(CONFIG_SWLPS_IN_IPS) || defined(CONFIG_FWLPS_IN_IPS) */
	RTW_INFO("====> %s...\n", __FUNCTION__);


#if defined(CONFIG_SWLPS_IN_IPS) || defined(CONFIG_FWLPS_IN_IPS)
#ifdef DBG_CONFIG_ERROR_DETECT
	if (psrtpriv->silent_reset_inprogress == _TRUE)
#endif /* #ifdef DBG_CONFIG_ERROR_DETECT */
#endif /* defined(CONFIG_SWLPS_IN_IPS) || defined(CONFIG_FWLPS_IN_IPS) */
	{
		rtw_hal_set_hwreg(padapter, HW_VAR_FIFO_CLEARN_UP, 0);
		rtw_intf_stop(padapter);
	}

	if (!rtw_is_surprise_removed(padapter))
		rtw_hal_deinit(padapter);

}
#ifdef CONFIG_NEW_NETDEV_HDL
int _pm_netdev_open(_adapter *padapter)
{
	uint status;
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);
	struct net_device *pnetdev = padapter->pnetdev;

	RTW_INFO(FUNC_NDEV_FMT" start\n", FUNC_NDEV_ARG(pnetdev));

	if (!rtw_is_hw_init_completed(padapter)) { // ips 
		rtw_clr_surprise_removed(padapter);
		rtw_clr_drv_stopped(padapter);
		status = rtw_hal_init(padapter);
		if (status == _FAIL)
			goto netdev_open_error;
		rtw_led_control(padapter, LED_CTL_NO_LINK);
		#ifndef RTW_HALMAC
		status = rtw_mi_start_drv_threads(padapter);
		if (status == _FAIL) {
			RTW_ERR(FUNC_NDEV_FMT "Initialize driver thread failed!\n", FUNC_NDEV_ARG(pnetdev));
			goto netdev_open_error;
		}

		rtw_intf_start(GET_PRIMARY_ADAPTER(padapter));
		#endif /* !RTW_HALMAC */

		{
			_set_timer(&adapter_to_dvobj(padapter)->dynamic_chk_timer, 2000);

	#ifndef CONFIG_IPS_CHECK_IN_WD
			rtw_set_pwr_state_check_timer(pwrctrlpriv);
	#endif /*CONFIG_IPS_CHECK_IN_WD*/
		}

	}

	/*if (padapter->bup == _FALSE) */
	{
		rtw_hal_iface_init(padapter);

		padapter->bup = _TRUE;
		padapter->net_closed = _FALSE;
		padapter->netif_up = _TRUE;
		pwrctrlpriv->bips_processing = _FALSE;
	}

	RTW_INFO(FUNC_NDEV_FMT" Success (bup=%d)\n", FUNC_NDEV_ARG(pnetdev), padapter->bup);
	return 0;

netdev_open_error:
	padapter->bup = _FALSE;

	rtw_netif_carrier_off(pnetdev);
	rtw_netif_stop_queue(pnetdev);

	RTW_ERR(FUNC_NDEV_FMT" Failed!! (bup=%d)\n", FUNC_NDEV_ARG(pnetdev), padapter->bup);

	return -1;

}
int _mi_pm_netdev_open(struct net_device *pnetdev)
{
	int i;
	int status = 0;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	_adapter *iface;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (iface->netif_up) {
			status = _pm_netdev_open(iface);
			if (status == -1) {
				RTW_ERR("%s failled\n", __func__);
				break;
			}
		}
	}

	return status;
}
#endif /*CONFIG_NEW_NETDEV_HDL*/
int pm_netdev_open(struct net_device *pnetdev, u8 bnormal)
{
	int status = 0;

	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);

	if (_TRUE == bnormal) {
		_enter_critical_mutex(&(adapter_to_dvobj(padapter)->hw_init_mutex), NULL);
		#ifdef CONFIG_NEW_NETDEV_HDL
		status = _mi_pm_netdev_open(pnetdev);
		#else
		status = _netdev_open(pnetdev);
		#endif
		_exit_critical_mutex(&(adapter_to_dvobj(padapter)->hw_init_mutex), NULL);
	}
#ifdef CONFIG_IPS
	else
		status = (_SUCCESS == ips_netdrv_open(padapter)) ? (0) : (-1);
#endif

	return status;
}
#ifdef CONFIG_CLIENT_PORT_CFG
extern void rtw_hw_client_port_release(_adapter *adapter);
#endif
static int netdev_close(struct net_device *pnetdev)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
#endif /* CONFIG_BT_COEXIST_SOCKET_TRX */

	RTW_INFO(FUNC_NDEV_FMT" , bup=%d\n", FUNC_NDEV_ARG(pnetdev), padapter->bup);
#ifndef CONFIG_PLATFORM_INTEL_BYT
	padapter->net_closed = _TRUE;
	padapter->netif_up = _FALSE;
	pmlmepriv->LinkDetectInfo.bBusyTraffic = _FALSE;

#ifdef CONFIG_CLIENT_PORT_CFG
	if (MLME_IS_STA(padapter))
		rtw_hw_client_port_release(padapter);
#endif
	/*	if (!rtw_is_hw_init_completed(padapter)) {
			RTW_INFO("(1)871x_drv - drv_close, bup=%d, hw_init_completed=%s\n", padapter->bup, rtw_is_hw_init_completed(padapter)?"_TRUE":"_FALSE");

			rtw_set_drv_stopped(padapter);

			rtw_dev_unload(padapter);
		}
		else*/
	if (pwrctl->rf_pwrstate == rf_on) {
		RTW_INFO("(2)871x_drv - drv_close, bup=%d, hw_init_completed=%s\n", padapter->bup, rtw_is_hw_init_completed(padapter) ? "_TRUE" : "_FALSE");

		/* s1. */
		if (pnetdev)
			rtw_netif_stop_queue(pnetdev);

#ifndef CONFIG_RTW_ANDROID
		/* s2. */
		LeaveAllPowerSaveMode(padapter);
		rtw_disassoc_cmd(padapter, 500, RTW_CMDF_WAIT_ACK);
		/* s2-2.  indicate disconnect to os */
		rtw_indicate_disconnect(padapter, 0, _FALSE);
		/* s2-3. */
		rtw_free_assoc_resources_cmd(padapter, _TRUE, RTW_CMDF_WAIT_ACK);
		/* s2-4. */
		rtw_free_network_queue(padapter, _TRUE);
#endif
	}

#ifdef CONFIG_BR_EXT
	/* if (OPMODE & (WIFI_STATION_STATE | WIFI_ADHOC_STATE)) */
	{
		/* void nat25_db_cleanup(_adapter *priv); */
		nat25_db_cleanup(padapter);
	}
#endif /* CONFIG_BR_EXT */

#ifdef CONFIG_P2P
	if (!rtw_p2p_chk_role(&padapter->wdinfo, P2P_ROLE_DISABLE))
		rtw_p2p_enable(padapter, P2P_ROLE_DISABLE);
#endif /* CONFIG_P2P */

	rtw_scan_abort(padapter); /* stop scanning process before wifi is going to down */
#ifdef CONFIG_IOCTL_CFG80211
	rtw_cfg80211_wait_scan_req_empty(padapter, 200);
	adapter_wdev_data(padapter)->bandroid_scan = _FALSE;
	/* padapter->rtw_wdev->iftype = NL80211_IFTYPE_MONITOR; */ /* set this at the end */
#endif /* CONFIG_IOCTL_CFG80211 */

#ifdef CONFIG_WAPI_SUPPORT
	rtw_wapi_disable_tx(padapter);
#endif
#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
	if (is_primary_adapter(padapter) && (_TRUE == pHalData->EEPROMBluetoothCoexist))
		rtw_btcoex_close_socket(padapter);
	else
		RTW_INFO("CONFIG_BT_COEXIST: VIRTUAL_ADAPTER\n");
#endif /* CONFIG_BT_COEXIST_SOCKET_TRX */
#else /* !CONFIG_PLATFORM_INTEL_BYT */

	if (pwrctl->bInSuspend == _TRUE) {
		RTW_INFO("+871x_drv - drv_close, bInSuspend=%d\n", pwrctl->bInSuspend);
		return 0;
	}

	rtw_scan_abort(padapter); /* stop scanning process before wifi is going to down */
#ifdef CONFIG_IOCTL_CFG80211
	rtw_cfg80211_wait_scan_req_empty(padapter, 200);
#endif

	RTW_INFO("netdev_close, bips_processing=%d\n", pwrctl->bips_processing);
	while (pwrctl->bips_processing == _TRUE) /* waiting for ips_processing done before call rtw_dev_unload() */
		rtw_msleep_os(1);

	rtw_dev_unload(padapter);
	rtw_sdio_set_power(0);

#endif /* !CONFIG_PLATFORM_INTEL_BYT */

	RTW_INFO("-871x_drv - drv_close, bup=%d\n", padapter->bup);

	return 0;

}

int pm_netdev_close(struct net_device *pnetdev, u8 bnormal)
{
	int status = 0;

	status = netdev_close(pnetdev);

	return status;
}

void rtw_ndev_destructor(struct net_device *ndev)
{
	RTW_INFO(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

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

	for (; RTA_OK(rt_attr, rt_len); rt_attr = RTA_NEXT(rt_attr, rt_len)) {
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

static int route_dump(u32 *gw_addr , int *gw_index)
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
	if (err) {
		printk(": Could not create a datagram socket, error = %d\n", -ENXIO);
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
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
	/* referece:sock_xmit in kernel code
	 * WRITE for sock_sendmsg, READ for sock_recvmsg
	 * third parameter for msg_iovlen
	 * last parameter for iov_len
	 */
	iov_iter_init(&msg.msg_iter, WRITE, &iov, 1, sizeof(req));
#else
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
#endif
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = MSG_DONTWAIT;

	oldfs = get_fs();
	set_fs(KERNEL_DS);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	err = sock_sendmsg(sock, &msg);
#else
	err = sock_sendmsg(sock, &msg, sizeof(req));
#endif
	set_fs(oldfs);

	if (err < 0)
		goto out_sock;

	pg = (char *) __get_free_page(GFP_KERNEL);
	if (pg == NULL) {
		err = -ENOMEM;
		goto out_sock;
	}

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
restart:
#endif

	for (;;) {
		struct nlmsghdr *h;

		iov.iov_base = pg;
		iov.iov_len = PAGE_SIZE;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
		iov_iter_init(&msg.msg_iter, READ, &iov, 1, PAGE_SIZE);
#endif

		oldfs = get_fs();
		set_fs(KERNEL_DS);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0))
		err = sock_recvmsg(sock, &msg, MSG_DONTWAIT);
#else
		err = sock_recvmsg(sock, &msg, PAGE_SIZE, MSG_DONTWAIT);
#endif
		set_fs(oldfs);

		if (err < 0)
			goto out_sock_pg;

		if (msg.msg_flags & MSG_TRUNC) {
			err = -ENOBUFS;
			goto out_sock_pg;
		}

		h = (struct nlmsghdr *) pg;

		while (NLMSG_OK(h, err)) {
			struct route_info rt_info;
			if (h->nlmsg_type == NLMSG_DONE) {
				err = 0;
				goto done;
			}

			if (h->nlmsg_type == NLMSG_ERROR) {
				struct nlmsgerr *errm = (struct nlmsgerr *) NLMSG_DATA(h);
				err = errm->error;
				printk("NLMSG error: %d\n", errm->error);
				goto done;
			}

			if (h->nlmsg_type == RTM_GETROUTE)
				printk("RTM_GETROUTE: NLMSG: %d\n", h->nlmsg_type);
			if (h->nlmsg_type != RTM_NEWROUTE) {
				printk("NLMSG: %d\n", h->nlmsg_type);
				err = -EINVAL;
				goto done;
			}

			memset(&rt_info, 0, sizeof(struct route_info));
			parse_routes(h, &rt_info);
			if (!rt_info.dst_addr.s_addr && rt_info.gateway.s_addr && rt_info.dev_index) {
				*gw_addr = rt_info.gateway.s_addr;
				*gw_index = rt_info.dev_index;

			}
			h = NLMSG_NEXT(h, err);
		}

		if (err) {
			printk("!!!Remnant of size %d %d %d\n", err, h->nlmsg_len, h->nlmsg_type);
			err = -EINVAL;
			break;
		}
	}

done:
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	if (!err && req.g.rtgen_family == AF_INET) {
		req.g.rtgen_family = AF_INET6;

		iov.iov_base = &req;
		iov.iov_len = sizeof(req);

		msg.msg_name = &nladdr;
		msg.msg_namelen = sizeof(nladdr);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
		iov_iter_init(&msg.msg_iter, WRITE, &iov, 1, sizeof(req));
#else
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
#endif
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_flags = MSG_DONTWAIT;

		oldfs = get_fs();
		set_fs(KERNEL_DS);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
		err = sock_sendmsg(sock, &msg);
#else
		err = sock_sendmsg(sock, &msg, sizeof(req));
#endif
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

static int get_defaultgw(u32 *ip_addr , char mac[])
{
	int gw_index = 0; /* oif device index */
	struct net_device *gw_dev = NULL; /* oif device */

	route_dump(ip_addr, &gw_index);

	if (!(*ip_addr) || !gw_index) {
		/* RTW_INFO("No default GW\n"); */
		return -1;
	}

	gw_dev = dev_get_by_index(&init_net, gw_index);

	if (gw_dev == NULL) {
		/* RTW_INFO("get Oif Device Fail\n"); */
		return -1;
	}

	if (!arp_query(mac, *ip_addr, gw_dev)) {
		/* RTW_INFO( "arp query failed\n"); */
		dev_put(gw_dev);
		return -1;

	}
	dev_put(gw_dev);

	return 0;
}

int	rtw_gw_addr_query(_adapter *padapter)
{
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	u32 gw_addr = 0; /* default gw address */
	unsigned char gw_mac[32] = {0}; /* default gw mac */
	int i;
	int res;

	res = get_defaultgw(&gw_addr, gw_mac);
	if (!res) {
		pmlmepriv->gw_ip[0] = gw_addr & 0xff;
		pmlmepriv->gw_ip[1] = (gw_addr & 0xff00) >> 8;
		pmlmepriv->gw_ip[2] = (gw_addr & 0xff0000) >> 16;
		pmlmepriv->gw_ip[3] = (gw_addr & 0xff000000) >> 24;
		_rtw_memcpy(pmlmepriv->gw_mac_addr, gw_mac, ETH_ALEN);
		RTW_INFO("%s Gateway Mac:\t" MAC_FMT "\n", __FUNCTION__, MAC_ARG(pmlmepriv->gw_mac_addr));
		RTW_INFO("%s Gateway IP:\t" IP_FMT "\n", __FUNCTION__, IP_ARG(pmlmepriv->gw_ip));
	} else
		RTW_INFO("Get Gateway IP/MAC fail!\n");

	return res;
}
#endif

void rtw_dev_unload(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	struct dvobj_priv *pobjpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &pobjpriv->drv_dbg;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	if (padapter->bup == _TRUE) {
		RTW_INFO("==> "FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));

#ifdef CONFIG_WOWLAN
#ifdef CONFIG_GPIO_WAKEUP
		/*default wake up pin change to BT*/
		RTW_INFO("%s:default wake up pin change to BT\n", __FUNCTION__);
		rtw_hal_switch_gpio_wl_ctrl(padapter, WAKEUP_GPIO_IDX, _FALSE);
#endif /* CONFIG_GPIO_WAKEUP */
#endif /* CONFIG_WOWLAN */

		rtw_set_drv_stopped(padapter);
#ifdef CONFIG_XMIT_ACK
		if (padapter->xmitpriv.ack_tx)
			rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_DRV_STOP);
#endif

		rtw_intf_stop(padapter);
		
		rtw_stop_drv_threads(padapter);

		if (ATOMIC_READ(&(pcmdpriv->cmdthd_running)) == _TRUE) {
			RTW_ERR("cmd_thread not stop !!\n");
			rtw_warn_on(1);
		}
		
		/* check the status of IPS */
		if (rtw_hal_check_ips_status(padapter) == _TRUE || pwrctl->rf_pwrstate == rf_off) { /* check HW status and SW state */
			RTW_PRINT("%s: driver in IPS-FWLPS\n", __func__);
			pdbgpriv->dbg_dev_unload_inIPS_cnt++;
		} else
			RTW_PRINT("%s: driver not in IPS\n", __func__);

		if (!rtw_is_surprise_removed(padapter)) {
#ifdef CONFIG_BT_COEXIST
			rtw_btcoex_IpsNotify(padapter, pwrctl->ips_mode_req);
#endif
#ifdef CONFIG_WOWLAN
			if (pwrctl->bSupportRemoteWakeup == _TRUE &&
			    pwrctl->wowlan_mode == _TRUE)
				RTW_PRINT("%s bSupportRemoteWakeup==_TRUE  do not run rtw_hal_deinit()\n", __FUNCTION__);
			else
#endif
			{
				/* amy modify 20120221 for power seq is different between driver open and ips */
				rtw_hal_deinit(padapter);
			}
			rtw_set_surprise_removed(padapter);
		}

		padapter->bup = _FALSE;

		RTW_INFO("<== "FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));
	} else {
		RTW_INFO("%s: bup==_FALSE\n", __FUNCTION__);
	}
	rtw_cancel_all_timer(padapter);
}

int rtw_suspend_free_assoc_resource(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &padapter->wdinfo;
#endif /* CONFIG_P2P */

	RTW_INFO("==> "FUNC_ADPT_FMT" entry....\n", FUNC_ADPT_ARG(padapter));

	if (rtw_chk_roam_flags(padapter, RTW_ROAM_ON_RESUME)) {
		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)
			&& check_fwstate(pmlmepriv, WIFI_ASOC_STATE)
			#ifdef CONFIG_P2P
			&& (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)
				#if defined(CONFIG_IOCTL_CFG80211) && RTW_P2P_GROUP_INTERFACE
				|| rtw_p2p_chk_role(pwdinfo, P2P_ROLE_DEVICE)
				#endif
				)
			#endif /* CONFIG_P2P */
		) {
			RTW_INFO("%s %s(" MAC_FMT "), length:%d assoc_ssid.length:%d\n", __FUNCTION__,
				pmlmepriv->cur_network.network.Ssid.Ssid,
				MAC_ARG(pmlmepriv->cur_network.network.MacAddress),
				pmlmepriv->cur_network.network.Ssid.SsidLength,
				pmlmepriv->assoc_ssid.SsidLength);
			rtw_set_to_roam(padapter, 1);
		}
	}

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) && check_fwstate(pmlmepriv, WIFI_ASOC_STATE)) {
		rtw_disassoc_cmd(padapter, 0, RTW_CMDF_DIRECTLY);
		/* s2-2.  indicate disconnect to os */
		rtw_indicate_disconnect(padapter, 0, _FALSE);
	}
#ifdef CONFIG_AP_MODE
	else if (MLME_IS_AP(padapter) || MLME_IS_MESH(padapter))
		rtw_sta_flush(padapter, _TRUE);
#endif

	/* s2-3. */
	rtw_free_assoc_resources(padapter, _TRUE);

	/* s2-4. */
	rtw_free_network_queue(padapter, _TRUE);

	if (check_fwstate(pmlmepriv, WIFI_UNDER_SURVEY)) {
		RTW_PRINT("%s: fw_under_survey\n", __func__);
		rtw_indicate_scan_done(padapter, 1);
		clr_fwstate(pmlmepriv, WIFI_UNDER_SURVEY);
	}

	if (check_fwstate(pmlmepriv, WIFI_UNDER_LINKING) == _TRUE) {
		RTW_PRINT("%s: fw_under_linking\n", __FUNCTION__);
		rtw_indicate_disconnect(padapter, 0, _FALSE);
	}

	RTW_INFO("<== "FUNC_ADPT_FMT" exit....\n", FUNC_ADPT_ARG(padapter));
	return _SUCCESS;
}

#ifdef CONFIG_WOWLAN
int rtw_suspend_wow(_adapter *padapter)
{
	u8 ch, bw, offset;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct wowlan_ioctl_param poidparam;
	int ret = _SUCCESS;

	RTW_INFO("==> "FUNC_ADPT_FMT" entry....\n", FUNC_ADPT_ARG(padapter));


	RTW_INFO("wowlan_mode: %d\n", pwrpriv->wowlan_mode);
	RTW_INFO("wowlan_pno_enable: %d\n", pwrpriv->wowlan_pno_enable);
#ifdef CONFIG_P2P_WOWLAN
	RTW_INFO("wowlan_p2p_enable: %d\n", pwrpriv->wowlan_p2p_enable);
#endif

	if (pwrpriv->wowlan_mode == _TRUE) {
		rtw_mi_netif_stop_queue(padapter);
		#ifdef CONFIG_CONCURRENT_MODE
		rtw_mi_buddy_netif_carrier_off(padapter);
		#endif

		/* 0. Power off LED */
		rtw_led_control(padapter, LED_CTL_POWER_OFF);

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		/* 2.only for SDIO disable interrupt */
		rtw_intf_stop(padapter);

		/* 2.1 clean interrupt */
		rtw_hal_clear_interrupt(padapter);
#endif /* CONFIG_SDIO_HCI */

		/* 1. stop thread */
		rtw_set_drv_stopped(padapter);	/*for stop thread*/
		rtw_mi_stop_drv_threads(padapter);

		rtw_clr_drv_stopped(padapter);	/*for 32k command*/

		/* #ifdef CONFIG_LPS */
		/* rtw_set_ps_mode(padapter, PS_MODE_ACTIVE, 0, 0, "WOWLAN"); */
		/* #endif */

		#ifdef CONFIG_SDIO_HCI
		/* 2.2 free irq */
		#if !(CONFIG_RTW_SDIO_KEEP_IRQ)
		sdio_free_irq(adapter_to_dvobj(padapter));
		#endif
		#endif/*CONFIG_SDIO_HCI*/

#ifdef CONFIG_RUNTIME_PORT_SWITCH
		if (rtw_port_switch_chk(padapter)) {
			RTW_INFO(" ### PORT SWITCH ###\n");
			rtw_hal_set_hwreg(padapter, HW_VAR_PORT_SWITCH, NULL);
		}
#endif

		rtw_wow_lps_level_decide(padapter, _TRUE);
		poidparam.subcode = WOWLAN_ENABLE;
		rtw_hal_set_hwreg(padapter, HW_VAR_WOWLAN, (u8 *)&poidparam);
		if (rtw_chk_roam_flags(padapter, RTW_ROAM_ON_RESUME)) {
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)
			    && check_fwstate(pmlmepriv, WIFI_ASOC_STATE)) {
				RTW_INFO("%s %s(" MAC_FMT "), length:%d assoc_ssid.length:%d\n", __FUNCTION__,
					pmlmepriv->cur_network.network.Ssid.Ssid,
					MAC_ARG(pmlmepriv->cur_network.network.MacAddress),
					pmlmepriv->cur_network.network.Ssid.SsidLength,
					 pmlmepriv->assoc_ssid.SsidLength);

				rtw_set_to_roam(padapter, 0);
			}
		}

		RTW_PRINT("%s: wowmode suspending\n", __func__);

		if (check_fwstate(pmlmepriv, WIFI_UNDER_SURVEY) == _TRUE) {
			RTW_PRINT("%s: fw_under_survey\n", __func__);
			rtw_indicate_scan_done(padapter, 1);
			clr_fwstate(pmlmepriv, WIFI_UNDER_SURVEY);
		}

#if 1
		if (rtw_mi_check_status(padapter, MI_LINKED)) {
			ch =  rtw_mi_get_union_chan(padapter);
			bw = rtw_mi_get_union_bw(padapter);
			offset = rtw_mi_get_union_offset(padapter);
			RTW_INFO(FUNC_ADPT_FMT" back to linked/linking union - ch:%u, bw:%u, offset:%u\n",
				 FUNC_ADPT_ARG(padapter), ch, bw, offset);
			set_channel_bwmode(padapter, ch, offset, bw);
		}
#else
		if (rtw_mi_get_ch_setting_union(padapter, &ch, &bw, &offset) != 0) {
			RTW_INFO(FUNC_ADPT_FMT" back to linked/linking union - ch:%u, bw:%u, offset:%u\n",
				 FUNC_ADPT_ARG(padapter), ch, bw, offset);
			set_channel_bwmode(padapter, ch, offset, bw);
			rtw_mi_update_union_chan_inf(padapter, ch, offset, bw);
		}
#endif
#ifdef CONFIG_CONCURRENT_MODE
		rtw_mi_buddy_suspend_free_assoc_resource(padapter);
#endif

#ifdef CONFIG_BT_COEXIST
		rtw_btcoex_SuspendNotify(padapter, BTCOEX_SUSPEND_STATE_SUSPEND_KEEP_ANT);
#endif

		if (pwrpriv->wowlan_pno_enable) {
			RTW_PRINT("%s: pno: %d\n", __func__,
				  pwrpriv->wowlan_pno_enable);
#ifdef CONFIG_FWLPS_IN_IPS
			rtw_set_fw_in_ips_mode(padapter, _TRUE);
#endif
		}
#ifdef CONFIG_LPS
		else {
			if(pwrpriv->wowlan_power_mgmt != PS_MODE_ACTIVE) {
				rtw_set_ps_mode(padapter, pwrpriv->wowlan_power_mgmt, 0, 0, "WOWLAN");
			}
		}
#endif /* #ifdef CONFIG_LPS */

	} else
		RTW_PRINT("%s: ### ERROR ### wowlan_mode=%d\n", __FUNCTION__, pwrpriv->wowlan_mode);
	RTW_INFO("<== "FUNC_ADPT_FMT" exit....\n", FUNC_ADPT_ARG(padapter));
	return ret;
}
#endif /* #ifdef CONFIG_WOWLAN */

#ifdef CONFIG_AP_WOWLAN
int rtw_suspend_ap_wow(_adapter *padapter)
{
	u8 ch, bw, offset;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct wowlan_ioctl_param poidparam;
	int ret = _SUCCESS;

	RTW_INFO("==> "FUNC_ADPT_FMT" entry....\n", FUNC_ADPT_ARG(padapter));

	pwrpriv->wowlan_ap_mode = _TRUE;

	RTW_INFO("wowlan_ap_mode: %d\n", pwrpriv->wowlan_ap_mode);

	rtw_mi_netif_stop_queue(padapter);

	/* 0. Power off LED */
	rtw_led_control(padapter, LED_CTL_POWER_OFF);
#ifdef CONFIG_SDIO_HCI
	/* 2.only for SDIO disable interrupt*/
	rtw_intf_stop(padapter);

	/* 2.1 clean interrupt */
	rtw_hal_clear_interrupt(padapter);
#endif /* CONFIG_SDIO_HCI */

	/* 1. stop thread */
	rtw_set_drv_stopped(padapter);	/*for stop thread*/
	rtw_mi_stop_drv_threads(padapter);
	rtw_clr_drv_stopped(padapter);	/*for 32k command*/

	#ifdef CONFIG_SDIO_HCI
	/* 2.2 free irq */
	#if !(CONFIG_RTW_SDIO_KEEP_IRQ)
	sdio_free_irq(adapter_to_dvobj(padapter));
	#endif
	#endif/*CONFIG_SDIO_HCI*/

#ifdef CONFIG_RUNTIME_PORT_SWITCH
	if (rtw_port_switch_chk(padapter)) {
		RTW_INFO(" ### PORT SWITCH ###\n");
		rtw_hal_set_hwreg(padapter, HW_VAR_PORT_SWITCH, NULL);
	}
#endif

	rtw_wow_lps_level_decide(padapter, _TRUE);
	poidparam.subcode = WOWLAN_AP_ENABLE;
	rtw_hal_set_hwreg(padapter, HW_VAR_WOWLAN, (u8 *)&poidparam);

	RTW_PRINT("%s: wowmode suspending\n", __func__);
#if 1
	if (rtw_mi_check_status(padapter, MI_LINKED)) {
		ch =  rtw_mi_get_union_chan(padapter);
		bw = rtw_mi_get_union_bw(padapter);
		offset = rtw_mi_get_union_offset(padapter);
		RTW_INFO("back to linked/linking union - ch:%u, bw:%u, offset:%u\n", ch, bw, offset);
		set_channel_bwmode(padapter, ch, offset, bw);
	}
#else
	if (rtw_mi_get_ch_setting_union(padapter, &ch, &bw, &offset) != 0) {
		RTW_INFO("back to linked/linking union - ch:%u, bw:%u, offset:%u\n", ch, bw, offset);
		set_channel_bwmode(padapter, ch, offset, bw);
		rtw_mi_update_union_chan_inf(padapter, ch, offset, bw);
	}
#endif

	/*FOR ONE AP - TODO :Multi-AP*/
	{
		int i;
		_adapter *iface;
		struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if ((iface) && rtw_is_adapter_up(iface)) {
				if (check_fwstate(&iface->mlmepriv, WIFI_AP_STATE | WIFI_MESH_STATE) == _FALSE)
					rtw_suspend_free_assoc_resource(iface);
			}
		}

	}

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_SuspendNotify(padapter, BTCOEX_SUSPEND_STATE_SUSPEND_KEEP_ANT);
#endif

#ifdef CONFIG_LPS
	if(pwrpriv->wowlan_power_mgmt != PS_MODE_ACTIVE) {
		rtw_set_ps_mode(padapter, pwrpriv->wowlan_power_mgmt, 0, 0, "AP-WOWLAN");
	}
#endif

	RTW_INFO("<== "FUNC_ADPT_FMT" exit....\n", FUNC_ADPT_ARG(padapter));
	return ret;
}
#endif /* #ifdef CONFIG_AP_WOWLAN */


int rtw_suspend_normal(_adapter *padapter)
{
	int ret = _SUCCESS;

	RTW_INFO("==> "FUNC_ADPT_FMT" entry....\n", FUNC_ADPT_ARG(padapter));

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_SuspendNotify(padapter, BTCOEX_SUSPEND_STATE_SUSPEND);
#endif
	rtw_mi_netif_caroff_qstop(padapter);

	rtw_mi_suspend_free_assoc_resource(padapter);

	rtw_led_control(padapter, LED_CTL_POWER_OFF);

	if ((rtw_hal_check_ips_status(padapter) == _TRUE)
	    || (adapter_to_pwrctl(padapter)->rf_pwrstate == rf_off))
		RTW_PRINT("%s: ### ERROR #### driver in IPS ####ERROR###!!!\n", __FUNCTION__);


#ifdef CONFIG_CONCURRENT_MODE
	rtw_set_drv_stopped(padapter);	/*for stop thread*/
	rtw_stop_cmd_thread(padapter);
	rtw_drv_stop_vir_ifaces(adapter_to_dvobj(padapter));
#endif
	rtw_dev_unload(padapter);

	#ifdef CONFIG_SDIO_HCI
	sdio_deinit(adapter_to_dvobj(padapter));

	#if !(CONFIG_RTW_SDIO_KEEP_IRQ)
	sdio_free_irq(adapter_to_dvobj(padapter));
	#endif
	#endif /*CONFIG_SDIO_HCI*/

	RTW_INFO("<== "FUNC_ADPT_FMT" exit....\n", FUNC_ADPT_ARG(padapter));
	return ret;
}

int rtw_suspend_common(_adapter *padapter)
{
	struct dvobj_priv *dvobj = padapter->dvobj;
	struct debug_priv *pdbgpriv = &dvobj->drv_dbg;
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(dvobj);
#ifdef CONFIG_WOWLAN
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct registry_priv *registry_par = &padapter->registrypriv;
#endif

	int ret = 0;
	systime start_time = rtw_get_current_time();

	RTW_PRINT(" suspend start\n");
	RTW_INFO("==> %s (%s:%d)\n", __FUNCTION__, current->comm, current->pid);

	pdbgpriv->dbg_suspend_cnt++;

	pwrpriv->bInSuspend = _TRUE;

	while (pwrpriv->bips_processing == _TRUE)
		rtw_msleep_os(1);

#ifdef CONFIG_IOL_READ_EFUSE_MAP
	if (!padapter->bup) {
		u8 bMacPwrCtrlOn = _FALSE;
		rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
		if (bMacPwrCtrlOn)
			rtw_hal_power_off(padapter);
	}
#endif

	if ((!padapter->bup) || RTW_CANNOT_RUN(padapter)) {
		RTW_INFO("%s bup=%d bDriverStopped=%s bSurpriseRemoved = %s\n", __func__
			 , padapter->bup
			 , rtw_is_drv_stopped(padapter) ? "True" : "False"
			, rtw_is_surprise_removed(padapter) ? "True" : "False");
		pdbgpriv->dbg_suspend_error_cnt++;
		goto exit;
	}
	rtw_mi_scan_abort(padapter, _TRUE);
	rtw_ps_deny(padapter, PS_DENY_SUSPEND);

	rtw_mi_cancel_all_timer(padapter);
	LeaveAllPowerSaveModeDirect(padapter);

	rtw_ps_deny_cancel(padapter, PS_DENY_SUSPEND);

	if (rtw_mi_check_status(padapter, MI_AP_MODE) == _FALSE) {
#ifdef CONFIG_WOWLAN
		if (WOWLAN_IS_STA_MIX_MODE(padapter))
			pwrpriv->wowlan_mode = _TRUE;
		else if ( registry_par->wowlan_enable && check_fwstate(pmlmepriv, WIFI_ASOC_STATE))
			pwrpriv->wowlan_mode = _TRUE;
		else if (pwrpriv->wowlan_pno_enable == _TRUE)
			pwrpriv->wowlan_mode |= pwrpriv->wowlan_pno_enable;

#ifdef CONFIG_P2P_WOWLAN
		if (!rtw_p2p_chk_state(&padapter->wdinfo, P2P_STATE_NONE) || P2P_ROLE_DISABLE != padapter->wdinfo.role)
			pwrpriv->wowlan_p2p_mode = _TRUE;
		if (_TRUE == pwrpriv->wowlan_p2p_mode)
			pwrpriv->wowlan_mode |= pwrpriv->wowlan_p2p_mode;
#endif /* CONFIG_P2P_WOWLAN */

		if (pwrpriv->wowlan_mode == _TRUE)
			rtw_suspend_wow(padapter);
		else
#endif /* CONFIG_WOWLAN */
			rtw_suspend_normal(padapter);
	} else if (rtw_mi_check_status(padapter, MI_AP_MODE)) {
#ifdef CONFIG_AP_WOWLAN
		rtw_suspend_ap_wow(padapter);
#else
		rtw_suspend_normal(padapter);
#endif /*CONFIG_AP_WOWLAN*/
	}


	RTW_PRINT("rtw suspend success in %d ms\n",
		  rtw_get_passing_time_ms(start_time));

exit:
	RTW_INFO("<===  %s return %d.............. in %dms\n", __FUNCTION__
		 , ret, rtw_get_passing_time_ms(start_time));

	return ret;
}

#ifdef CONFIG_WOWLAN
int rtw_resume_process_wow(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	struct wowlan_ioctl_param poidparam;
	struct sta_info	*psta = NULL;
	struct registry_priv  *registry_par = &padapter->registrypriv;
	int ret = _SUCCESS;

	RTW_INFO("==> "FUNC_ADPT_FMT" entry....\n", FUNC_ADPT_ARG(padapter));

	if (padapter) {
		pwrpriv = adapter_to_pwrctl(padapter);
	} else {
		pdbgpriv->dbg_resume_error_cnt++;
		ret = -1;
		goto exit;
	}

	if (RTW_CANNOT_RUN(padapter)) {
		RTW_INFO("%s pdapter %p bDriverStopped %s bSurpriseRemoved %s\n"
			 , __func__, padapter
			 , rtw_is_drv_stopped(padapter) ? "True" : "False"
			, rtw_is_surprise_removed(padapter) ? "True" : "False");
		goto exit;
	}

	pwrpriv->wowlan_in_resume = _TRUE;
#ifdef CONFIG_PNO_SUPPORT
#ifdef CONFIG_FWLPS_IN_IPS
	if (pwrpriv->wowlan_pno_enable)
		rtw_set_fw_in_ips_mode(padapter, _FALSE);
#endif /* CONFIG_FWLPS_IN_IPS */
#endif/* CONFIG_PNO_SUPPORT */

	if (pwrpriv->wowlan_mode == _TRUE) {
#ifdef CONFIG_LPS
		if(pwrpriv->wowlan_power_mgmt != PS_MODE_ACTIVE) {
			rtw_set_ps_mode(padapter, PS_MODE_ACTIVE, 0, 0, "WOWLAN");
			rtw_wow_lps_level_decide(padapter, _FALSE);
		}
#endif /* CONFIG_LPS */

		pwrpriv->bFwCurrentInPSMode = _FALSE;

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_PCI_HCI)
		rtw_mi_intf_stop(padapter);
		rtw_hal_clear_interrupt(padapter);
#endif

		#ifdef CONFIG_SDIO_HCI
		#if !(CONFIG_RTW_SDIO_KEEP_IRQ)
		if (sdio_alloc_irq(adapter_to_dvobj(padapter)) != _SUCCESS) {
			ret = -1;
			goto exit;
		}
		#endif
		#endif/*CONFIG_SDIO_HCI*/

		/* Disable WOW, set H2C command */
		poidparam.subcode = WOWLAN_DISABLE;
		rtw_hal_set_hwreg(padapter, HW_VAR_WOWLAN, (u8 *)&poidparam);

#ifdef CONFIG_CONCURRENT_MODE
		rtw_mi_buddy_reset_drv_sw(padapter);
#endif

		psta = rtw_get_stainfo(&padapter->stapriv, get_bssid(&padapter->mlmepriv));
		if (psta)
			set_sta_rate(padapter, psta);


		rtw_clr_drv_stopped(padapter);
		RTW_INFO("%s: wowmode resuming, DriverStopped:%s\n", __func__, rtw_is_drv_stopped(padapter) ? "True" : "False");

		rtw_mi_start_drv_threads(padapter);

		rtw_mi_intf_start(padapter);
		
		if(registry_par->suspend_type == FW_IPS_DISABLE_BBRF && !check_fwstate(pmlmepriv, WIFI_ASOC_STATE)) {
			if (!rtw_is_surprise_removed(padapter)) {
				rtw_hal_deinit(padapter);
				rtw_hal_init(padapter);
			}
			RTW_INFO("FW_IPS_DISABLE_BBRF hal deinit, hal init \n");
		}

#ifdef CONFIG_CONCURRENT_MODE
		rtw_mi_buddy_netif_carrier_on(padapter);
#endif

		/* start netif queue */
		rtw_mi_netif_wake_queue(padapter);

	} else

		RTW_PRINT("%s: ### ERROR ### wowlan_mode=%d\n", __FUNCTION__, pwrpriv->wowlan_mode);

	if (padapter->pid[1] != 0) {
		RTW_INFO("pid[1]:%d\n", padapter->pid[1]);
		rtw_signal_process(padapter->pid[1], SIGUSR2);
	}

	if (rtw_chk_roam_flags(padapter, RTW_ROAM_ON_RESUME)) {
		if (pwrpriv->wowlan_wake_reason == FW_DECISION_DISCONNECT ||
		    pwrpriv->wowlan_wake_reason == RX_DISASSOC||
		    pwrpriv->wowlan_wake_reason == RX_DEAUTH) {

			RTW_INFO("%s: disconnect reason: %02x\n", __func__,
				 pwrpriv->wowlan_wake_reason);
			rtw_indicate_disconnect(padapter, 0, _FALSE);

			rtw_sta_media_status_rpt(padapter,
					 rtw_get_stainfo(&padapter->stapriv,
					 get_bssid(&padapter->mlmepriv)), 0);

			rtw_free_assoc_resources(padapter, _TRUE);
			pmlmeinfo->state = WIFI_FW_NULL_STATE;

		} else {
			RTW_INFO("%s: do roaming\n", __func__);
			rtw_roaming(padapter, NULL);
		}
	}

	if (pwrpriv->wowlan_mode == _TRUE) {
		pwrpriv->bips_processing = _FALSE;
		_set_timer(&adapter_to_dvobj(padapter)->dynamic_chk_timer, 2000);
#ifndef CONFIG_IPS_CHECK_IN_WD
		rtw_set_pwr_state_check_timer(pwrpriv);
#endif
	} else
		RTW_PRINT("do not reset timer\n");

	pwrpriv->wowlan_mode = _FALSE;

	/* Power On LED */
#ifdef CONFIG_RTW_SW_LED

	if (pwrpriv->wowlan_wake_reason == RX_DISASSOC||
	    pwrpriv->wowlan_wake_reason == RX_DEAUTH||
	    pwrpriv->wowlan_wake_reason == FW_DECISION_DISCONNECT)
		rtw_led_control(padapter, LED_CTL_NO_LINK);
	else
		rtw_led_control(padapter, LED_CTL_LINK);
#endif
	/* clean driver side wake up reason. */
	pwrpriv->wowlan_last_wake_reason = pwrpriv->wowlan_wake_reason;
	pwrpriv->wowlan_wake_reason = 0;

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_SuspendNotify(padapter, BTCOEX_SUSPEND_STATE_RESUME);
#endif /* CONFIG_BT_COEXIST */

exit:
	RTW_INFO("<== "FUNC_ADPT_FMT" exit....\n", FUNC_ADPT_ARG(padapter));
	return ret;
}
#endif /* #ifdef CONFIG_WOWLAN */

#ifdef CONFIG_AP_WOWLAN
int rtw_resume_process_ap_wow(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	struct wowlan_ioctl_param poidparam;
	struct sta_info	*psta = NULL;
	int ret = _SUCCESS;
	u8 ch, bw, offset;

	RTW_INFO("==> "FUNC_ADPT_FMT" entry....\n", FUNC_ADPT_ARG(padapter));

	if (padapter) {
		pwrpriv = adapter_to_pwrctl(padapter);
	} else {
		pdbgpriv->dbg_resume_error_cnt++;
		ret = -1;
		goto exit;
	}


#ifdef CONFIG_LPS
	if(pwrpriv->wowlan_power_mgmt != PS_MODE_ACTIVE) {
		rtw_set_ps_mode(padapter, PS_MODE_ACTIVE, 0, 0, "AP-WOWLAN");
		rtw_wow_lps_level_decide(padapter, _FALSE);
	}
#endif /* CONFIG_LPS */

	pwrpriv->bFwCurrentInPSMode = _FALSE;

	rtw_hal_disable_interrupt(padapter);

	rtw_hal_clear_interrupt(padapter);

	#ifdef CONFIG_SDIO_HCI
	#if !(CONFIG_RTW_SDIO_KEEP_IRQ)
	if (sdio_alloc_irq(adapter_to_dvobj(padapter)) != _SUCCESS) {
		ret = -1;
		goto exit;
	}
	#endif
	#endif/*CONFIG_SDIO_HCI*/
	/* Disable WOW, set H2C command */
	poidparam.subcode = WOWLAN_AP_DISABLE;
	rtw_hal_set_hwreg(padapter, HW_VAR_WOWLAN, (u8 *)&poidparam);
	pwrpriv->wowlan_ap_mode = _FALSE;

	rtw_clr_drv_stopped(padapter);
	RTW_INFO("%s: wowmode resuming, DriverStopped:%s\n", __func__, rtw_is_drv_stopped(padapter) ? "True" : "False");

	rtw_mi_start_drv_threads(padapter);

#if 1
	if (rtw_mi_check_status(padapter, MI_LINKED)) {
		ch =  rtw_mi_get_union_chan(padapter);
		bw = rtw_mi_get_union_bw(padapter);
		offset = rtw_mi_get_union_offset(padapter);
		RTW_INFO(FUNC_ADPT_FMT" back to linked/linking union - ch:%u, bw:%u, offset:%u\n", FUNC_ADPT_ARG(padapter), ch, bw, offset);
		set_channel_bwmode(padapter, ch, offset, bw);
	}
#else
	if (rtw_mi_get_ch_setting_union(padapter, &ch, &bw, &offset) != 0) {
		RTW_INFO(FUNC_ADPT_FMT" back to linked/linking union - ch:%u, bw:%u, offset:%u\n", FUNC_ADPT_ARG(padapter), ch, bw, offset);
		set_channel_bwmode(padapter, ch, offset, bw);
		rtw_mi_update_union_chan_inf(padapter, ch, offset, bw);
	}
#endif

	/*FOR ONE AP - TODO :Multi-AP*/
	{
		int i;
		_adapter *iface;
		struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if ((iface) && rtw_is_adapter_up(iface)) {
				if (check_fwstate(&iface->mlmepriv, WIFI_AP_STATE | WIFI_MESH_STATE | WIFI_ASOC_STATE))
					rtw_reset_drv_sw(iface);
			}
		}

	}
	rtw_mi_intf_start(padapter);

	/* start netif queue */
	rtw_mi_netif_wake_queue(padapter);

	if (padapter->pid[1] != 0) {
		RTW_INFO("pid[1]:%d\n", padapter->pid[1]);
		rtw_signal_process(padapter->pid[1], SIGUSR2);
	}

#ifdef CONFIG_RESUME_IN_WORKQUEUE
	/* rtw_unlock_suspend(); */
#endif /* CONFIG_RESUME_IN_WORKQUEUE */

	pwrpriv->bips_processing = _FALSE;
	_set_timer(&adapter_to_dvobj(padapter)->dynamic_chk_timer, 2000);
#ifndef CONFIG_IPS_CHECK_IN_WD
	rtw_set_pwr_state_check_timer(pwrpriv);
#endif
	/* clean driver side wake up reason. */
	pwrpriv->wowlan_wake_reason = 0;

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_SuspendNotify(padapter, BTCOEX_SUSPEND_STATE_RESUME);
#endif /* CONFIG_BT_COEXIST */

	/* Power On LED */
#ifdef CONFIG_RTW_SW_LED

	rtw_led_control(padapter, LED_CTL_LINK);
#endif
exit:
	RTW_INFO("<== "FUNC_ADPT_FMT" exit....\n", FUNC_ADPT_ARG(padapter));
	return ret;
}
#endif /* #ifdef CONFIG_APWOWLAN */

void rtw_mi_resume_process_normal(_adapter *padapter)
{
	int i;
	_adapter *iface;
	struct mlme_priv *pmlmepriv;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if ((iface) && rtw_is_adapter_up(iface)) {
			pmlmepriv = &iface->mlmepriv;

			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
				RTW_INFO(FUNC_ADPT_FMT" fwstate:0x%08x - WIFI_STATION_STATE\n", FUNC_ADPT_ARG(iface), get_fwstate(pmlmepriv));

				if (rtw_chk_roam_flags(iface, RTW_ROAM_ON_RESUME))
					rtw_roaming(iface, NULL);

			} else if (MLME_IS_AP(iface) || MLME_IS_MESH(iface)) {
				RTW_INFO(FUNC_ADPT_FMT" %s\n", FUNC_ADPT_ARG(iface), MLME_IS_AP(iface) ? "AP" : "MESH");
				rtw_ap_restore_network(iface);
			} else if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE))
				RTW_INFO(FUNC_ADPT_FMT" fwstate:0x%08x - WIFI_ADHOC_STATE\n", FUNC_ADPT_ARG(iface), get_fwstate(pmlmepriv));
			else
				RTW_INFO(FUNC_ADPT_FMT" fwstate:0x%08x - ???\n", FUNC_ADPT_ARG(iface), get_fwstate(pmlmepriv));
		}
	}
}

int rtw_resume_process_normal(_adapter *padapter)
{
	struct net_device *pnetdev;
	struct pwrctrl_priv *pwrpriv;
	struct dvobj_priv *psdpriv;
	struct debug_priv *pdbgpriv;

	int ret = _SUCCESS;

	if (!padapter) {
		ret = -1;
		goto exit;
	}

	pnetdev = padapter->pnetdev;
	pwrpriv = adapter_to_pwrctl(padapter);
	psdpriv = padapter->dvobj;
	pdbgpriv = &psdpriv->drv_dbg;

	RTW_INFO("==> "FUNC_ADPT_FMT" entry....\n", FUNC_ADPT_ARG(padapter));

	#ifdef CONFIG_SDIO_HCI
	/* interface init */
	if (sdio_init(adapter_to_dvobj(padapter)) != _SUCCESS) {
		ret = -1;
		goto exit;
	}
	#endif/*CONFIG_SDIO_HCI*/

	rtw_clr_surprise_removed(padapter);
	rtw_hal_disable_interrupt(padapter);

	#ifdef CONFIG_SDIO_HCI
	#if !(CONFIG_RTW_SDIO_KEEP_IRQ)
	if (sdio_alloc_irq(adapter_to_dvobj(padapter)) != _SUCCESS) {
		ret = -1;
		goto exit;
	}
	#endif
	#endif/*CONFIG_SDIO_HCI*/

	rtw_mi_reset_drv_sw(padapter);

	pwrpriv->bkeepfwalive = _FALSE;

	RTW_INFO("bkeepfwalive(%x)\n", pwrpriv->bkeepfwalive);
	if (pm_netdev_open(pnetdev, _TRUE) != 0) {
		ret = -1;
		pdbgpriv->dbg_resume_error_cnt++;
		goto exit;
	}

	rtw_mi_netif_caron_qstart(padapter);

	if (padapter->pid[1] != 0) {
		RTW_INFO("pid[1]:%d\n", padapter->pid[1]);
		rtw_signal_process(padapter->pid[1], SIGUSR2);
	}

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_SuspendNotify(padapter, BTCOEX_SUSPEND_STATE_RESUME);
#endif /* CONFIG_BT_COEXIST */

	rtw_mi_resume_process_normal(padapter);

#ifdef CONFIG_RESUME_IN_WORKQUEUE
	/* rtw_unlock_suspend(); */
#endif /* CONFIG_RESUME_IN_WORKQUEUE */
	RTW_INFO("<== "FUNC_ADPT_FMT" exit....\n", FUNC_ADPT_ARG(padapter));

exit:
	return ret;
}

int rtw_resume_common(_adapter *padapter)
{
	int ret = 0;
	systime start_time = rtw_get_current_time();
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);

	if (pwrpriv == NULL)
		return 0;

	if (pwrpriv->bInSuspend == _FALSE)
		return 0;

	RTW_PRINT("resume start\n");
	RTW_INFO("==> %s (%s:%d)\n", __FUNCTION__, current->comm, current->pid);

	if (rtw_mi_check_status(padapter, MI_AP_MODE) == _FALSE) {
#ifdef CONFIG_WOWLAN
		if (pwrpriv->wowlan_mode == _TRUE)
			rtw_resume_process_wow(padapter);
		else
#endif
			rtw_resume_process_normal(padapter);

	} else if (rtw_mi_check_status(padapter, MI_AP_MODE)) {
#ifdef CONFIG_AP_WOWLAN
		rtw_resume_process_ap_wow(padapter);
#else
		rtw_resume_process_normal(padapter);
#endif /* CONFIG_AP_WOWLAN */
	}

	pwrpriv->bInSuspend = _FALSE;
	pwrpriv->wowlan_in_resume = _FALSE;

	RTW_PRINT("%s:%d in %d ms\n", __FUNCTION__ , ret,
		  rtw_get_passing_time_ms(start_time));


	return ret;
}

#ifdef CONFIG_GPIO_API
u8 rtw_get_gpio(struct net_device *netdev, u8 gpio_num)
{
	_adapter *adapter = (_adapter *)rtw_netdev_priv(netdev);
	return rtw_hal_get_gpio(adapter, gpio_num);
}
EXPORT_SYMBOL(rtw_get_gpio);

int  rtw_set_gpio_output_value(struct net_device *netdev, u8 gpio_num, bool isHigh)
{
	u8 direction = 0;
	u8 res = -1;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(netdev);
	return rtw_hal_set_gpio_output_value(adapter, gpio_num, isHigh);
}
EXPORT_SYMBOL(rtw_set_gpio_output_value);

int rtw_config_gpio(struct net_device *netdev, u8 gpio_num, bool isOutput)
{
	_adapter *adapter = (_adapter *)rtw_netdev_priv(netdev);
	return rtw_hal_config_gpio(adapter, gpio_num, isOutput);
}
EXPORT_SYMBOL(rtw_config_gpio);
int rtw_register_gpio_interrupt(struct net_device *netdev, int gpio_num, void(*callback)(u8 level))
{
	_adapter *adapter = (_adapter *)rtw_netdev_priv(netdev);
	return rtw_hal_register_gpio_interrupt(adapter, gpio_num, callback);
}
EXPORT_SYMBOL(rtw_register_gpio_interrupt);

int rtw_disable_gpio_interrupt(struct net_device *netdev, int gpio_num)
{
	_adapter *adapter = (_adapter *)rtw_netdev_priv(netdev);
	return rtw_hal_disable_gpio_interrupt(adapter, gpio_num);
}
EXPORT_SYMBOL(rtw_disable_gpio_interrupt);

#endif /* #ifdef CONFIG_GPIO_API */

#ifdef CONFIG_APPEND_VENDOR_IE_ENABLE

int rtw_vendor_ie_get_api(struct net_device *dev, int ie_num, char *extra,
		u16 extra_len)
{
	int ret = 0;

	ret = rtw_vendor_ie_get_raw_data(dev, ie_num, extra, extra_len);
	return ret;
}
EXPORT_SYMBOL(rtw_vendor_ie_get_api);

int rtw_vendor_ie_set_api(struct net_device *dev, char *extra)
{
	return rtw_vendor_ie_set(dev, NULL, NULL, extra);
}
EXPORT_SYMBOL(rtw_vendor_ie_set_api);

#endif
