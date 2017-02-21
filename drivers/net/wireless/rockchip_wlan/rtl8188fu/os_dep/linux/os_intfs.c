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

#include <drv_types.h>
#include <hal_data.h>

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)

#error "Shall be Linux or Windows, but not both!\n"

#endif


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
int rtw_wireless_mode = WIRELESS_MODE_MAX;
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
int rtw_power_mgnt = PS_MODE_MAX;
#ifdef CONFIG_IPS_LEVEL_2
int rtw_ips_mode = IPS_LEVEL_2;
#else
int rtw_ips_mode = IPS_NORMAL;
#endif
#else
int rtw_power_mgnt = PS_MODE_ACTIVE;
int rtw_ips_mode = IPS_NONE;
#endif
module_param(rtw_ips_mode, int, 0644);
MODULE_PARM_DESC(rtw_ips_mode,"The default IPS mode");

int rtw_smart_ps = 2;

int rtw_check_fw_ps = 1;

#ifdef CONFIG_TX_EARLY_MODE
int rtw_early_mode=1;
#endif

int rtw_usb_rxagg_mode = 2;//USB_RX_AGG_DMA =1,USB_RX_AGG_USB=2
module_param(rtw_usb_rxagg_mode, int, 0644);

int rtw_radio_enable = 1;
int rtw_long_retry_lmt = 7;
int rtw_short_retry_lmt = 7;
int rtw_busy_thresh = 40;
//int qos_enable = 0; //*
int rtw_ack_policy = NORMAL_ACK;

int rtw_mp_mode = 0;

#if defined(CONFIG_MP_INCLUDED) && defined(CONFIG_RTW_CUSTOMER_STR)
uint rtw_mp_customer_str = 0;
module_param(rtw_mp_customer_str, uint, 0644);
MODULE_PARM_DESC(rtw_mp_customer_str, "Whether or not to enable customer str support on MP mode");
#endif

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
#ifdef CONFIG_RTL8814A
int rtw_pwrtrim_enable = 2; /* disable kfree , rename to power trim disable */
#else
int rtw_pwrtrim_enable = 0; /* Default Enalbe  power trim by efuse config */
#endif
#ifdef CONFIG_80211N_HT
int rtw_ht_enable = 1;
// 0: 20 MHz, 1: 40 MHz, 2: 80 MHz, 3: 160MHz, 4: 80+80MHz
// 2.4G use bit 0 ~ 3, 5G use bit 4 ~ 7
// 0x21 means enable 2.4G 40MHz & 5G 80MHz
int rtw_bw_mode = 0x21;
int rtw_ampdu_enable = 1;//for enable tx_ampdu ,// 0: disable, 0x1:enable
int rtw_rx_stbc = 1;// 0: disable, bit(0):enable 2.4g, bit(1):enable 5g, default is set to enable 2.4GHZ for IOT issue with bufflao's AP at 5GHZ
int rtw_ampdu_amsdu = 0;// 0: disabled, 1:enabled, 2:auto . There is an IOT issu with DLINK DIR-629 when the flag turn on
// Short GI support Bit Map
// BIT0 - 20MHz, 0: non-support, 1: support
// BIT1 - 40MHz, 0: non-support, 1: support
// BIT2 - 80MHz, 0: non-support, 1: support
// BIT3 - 160MHz, 0: non-support, 1: support
int rtw_short_gi = 0xf;
// BIT0: Enable VHT LDPC Rx, BIT1: Enable VHT LDPC Tx, BIT4: Enable HT LDPC Rx, BIT5: Enable HT LDPC Tx
int rtw_ldpc_cap = 0x00;
// BIT0: Enable VHT STBC Rx, BIT1: Enable VHT STBC Tx, BIT4: Enable HT STBC Rx, BIT5: Enable HT STBC Tx
int rtw_stbc_cap = 0x13;
// BIT0: Enable VHT Beamformer, BIT1: Enable VHT Beamformee, BIT4: Enable HT Beamformer, BIT5: Enable HT Beamformee
int rtw_beamform_cap = 0x2;
int rtw_bfer_rf_number = 0; /*BeamformerCapRfNum Rf path number, 0 for auto, others for manual*/
int rtw_bfee_rf_number = 0; /*BeamformeeCapRfNum  Rf path number, 0 for auto, others for manual*/

#endif //CONFIG_80211N_HT

#ifdef CONFIG_80211AC_VHT
int rtw_vht_enable = 1; //0:disable, 1:enable, 2:force auto enable
int rtw_ampdu_factor = 7;
int rtw_vht_rate_sel = 0;
#endif //CONFIG_80211AC_VHT

int rtw_lowrate_two_xmit = 1;//Use 2 path Tx to transmit MCS0~7 and legacy mode

//int rf_config = RF_1T2R;  // 1T2R
int rtw_rf_config = RF_MAX_TYPE;  //auto

int rtw_low_power = 0;
#ifdef CONFIG_WIFI_TEST
int rtw_wifi_spec = 1;//for wifi test
#else
int rtw_wifi_spec = 0;
#endif

int rtw_special_rf_path = 0; //0: 2T2R ,1: only turn on path A 1T1R

char rtw_country_unspecified[] = {0xFF, 0xFF, 0x00};
char *rtw_country_code = rtw_country_unspecified;
module_param(rtw_country_code, charp, 0644);
MODULE_PARM_DESC(rtw_country_code, "The default country code (in alpha2)");

int rtw_channel_plan = RTW_CHPLAN_MAX;
module_param(rtw_channel_plan, int, 0644);
MODULE_PARM_DESC(rtw_channel_plan, "The default chplan ID when rtw_alpha2 is not specified or valid");

/*if concurrent softap + p2p(GO) is needed, this param lets p2p response full channel list.
But Softap must be SHUT DOWN once P2P decide to set up connection and become a GO.*/
#ifdef CONFIG_FULL_CH_IN_P2P_HANDSHAKE
int rtw_full_ch_in_p2p_handshake = 1; /* reply full channel list*/
#else
int rtw_full_ch_in_p2p_handshake = 0; /* reply only softap channel*/
#endif

#ifdef CONFIG_BT_COEXIST
int rtw_btcoex_enable = 1;
module_param(rtw_btcoex_enable, int, 0644);
MODULE_PARM_DESC(rtw_btcoex_enable, "Enable BT co-existence mechanism");
int rtw_bt_iso = 2;// 0:Low, 1:High, 2:From Efuse
int rtw_bt_sco = 3;// 0:Idle, 1:None-SCO, 2:SCO, 3:From Counter, 4.Busy, 5.OtherBusy
int rtw_bt_ampdu =1 ;// 0:Disable BT control A-MPDU, 1:Enable BT control A-MPDU.
int rtw_ant_num = -1; // <0: undefined, >0: Antenna number
module_param(rtw_ant_num, int, 0644);
MODULE_PARM_DESC(rtw_ant_num, "Antenna number setting");
#endif

int rtw_AcceptAddbaReq = _TRUE;// 0:Reject AP's Add BA req, 1:Accept AP's Add BA req.

int rtw_antdiv_cfg = 2; // 0:OFF , 1:ON, 2:decide by Efuse config
int rtw_antdiv_type = 0 ; //0:decide by efuse  1: for 88EE, 1Tx and 1RxCG are diversity.(2 Ant with SPDT), 2:  for 88EE, 1Tx and 2Rx are diversity.( 2 Ant, Tx and RxCG are both on aux port, RxCS is on main port ), 3: for 88EE, 1Tx and 1RxCG are fixed.(1Ant, Tx and RxCG are both on aux port)

int rtw_switch_usb3 = _FALSE; /* _FALSE: doesn't switch, _TRUE: switch from usb2.0 to usb 3.0 */

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

#ifdef CONFIG_80211D
int rtw_80211d = 0;
#endif

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

#ifdef CONFIG_AUTO_CHNL_SEL_NHM
int rtw_acs_mode = 1; /*0:disable, 1:enable*/
module_param(rtw_acs_mode, int, 0644);

int rtw_acs_auto_scan = 0; /*0:disable, 1:enable*/
module_param(rtw_acs_auto_scan, int, 0644);

#endif

char* ifname = "wlan%d";
module_param(ifname, charp, 0644);
MODULE_PARM_DESC(ifname, "The default name to allocate for first interface");

#ifdef CONFIG_PLATFORM_ANDROID
char* if2name = "p2p%d";
#else //CONFIG_PLATFORM_ANDROID
char* if2name = "wlan%d";
#endif //CONFIG_PLATFORM_ANDROID
module_param(if2name, charp, 0644);
MODULE_PARM_DESC(if2name, "The default name to allocate for second interface");

char* rtw_initmac = 0;  // temp mac address if users want to use instead of the mac address in Efuse

#ifdef CONFIG_MULTI_VIR_IFACES
int rtw_ext_iface_num  = 1;//primary/secondary iface is excluded
module_param(rtw_ext_iface_num, int, 0644);
#endif //CONFIG_MULTI_VIR_IFACES

module_param(rtw_pwrtrim_enable, int, 0644);
module_param(rtw_initmac, charp, 0644);
module_param(rtw_special_rf_path, int, 0644);
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
module_param(rtw_bw_mode, int, 0644);
module_param(rtw_ampdu_enable, int, 0644);
module_param(rtw_rx_stbc, int, 0644);
module_param(rtw_ampdu_amsdu, int, 0644);
#endif //CONFIG_80211N_HT
#ifdef CONFIG_80211AC_VHT
module_param(rtw_vht_enable, int, 0644);
#endif //CONFIG_80211AC_VHT
#ifdef CONFIG_BEAMFORMING
module_param(rtw_beamform_cap, int, 0644);
#endif
module_param(rtw_lowrate_two_xmit, int, 0644);

module_param(rtw_rf_config, int, 0644);
module_param(rtw_power_mgnt, int, 0644);
module_param(rtw_smart_ps, int, 0644);
module_param(rtw_low_power, int, 0644);
module_param(rtw_wifi_spec, int, 0644);

module_param(rtw_full_ch_in_p2p_handshake, int, 0644);
module_param(rtw_antdiv_cfg, int, 0644);
module_param(rtw_antdiv_type, int, 0644);

module_param(rtw_switch_usb3, int, 0644);

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
int rtw_fw_iol=1;
module_param(rtw_fw_iol, int, 0644);
MODULE_PARM_DESC(rtw_fw_iol, "FW IOL. 0:Disable, 1:enable, 2:by usb speed");
#endif //CONFIG_IOL

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
#endif // CONFIG_MP_INCLUDED
#endif // CONFIG_FILE_FWIMG

#ifdef CONFIG_TX_MCAST2UNI
module_param(rtw_mc2u_disable, int, 0644);
#endif	// CONFIG_TX_MCAST2UNI

#ifdef CONFIG_80211D
module_param(rtw_80211d, int, 0644);
MODULE_PARM_DESC(rtw_80211d, "Enable 802.11d mechanism");
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

uint rtw_adaptivity_dml = CONFIG_RTW_ADAPTIVITY_DML;
module_param(rtw_adaptivity_dml, uint, 0644);
MODULE_PARM_DESC(rtw_adaptivity_dml, "0:disable, 1:enable");

uint rtw_adaptivity_dc_backoff = CONFIG_RTW_ADAPTIVITY_DC_BACKOFF;
module_param(rtw_adaptivity_dc_backoff, uint, 0644);
MODULE_PARM_DESC(rtw_adaptivity_dc_backoff, "DC backoff for Adaptivity");

int rtw_adaptivity_th_l2h_ini = CONFIG_RTW_ADAPTIVITY_TH_L2H_INI;
module_param(rtw_adaptivity_th_l2h_ini, int, 0644);
MODULE_PARM_DESC(rtw_adaptivity_th_l2h_ini, "TH_L2H_ini for Adaptivity");

int rtw_adaptivity_th_edcca_hl_diff = CONFIG_RTW_ADAPTIVITY_TH_EDCCA_HL_DIFF;
module_param(rtw_adaptivity_th_edcca_hl_diff, int, 0644);
MODULE_PARM_DESC(rtw_adaptivity_th_edcca_hl_diff, "TH_EDCCA_HL_diff for Adaptivity");

uint rtw_amplifier_type_2g = CONFIG_RTW_AMPLIFIER_TYPE_2G;
module_param(rtw_amplifier_type_2g, uint, 0644);
MODULE_PARM_DESC(rtw_amplifier_type_2g, "BIT3:2G ext-PA, BIT4:2G ext-LNA");

uint rtw_amplifier_type_5g = CONFIG_RTW_AMPLIFIER_TYPE_5G;
module_param(rtw_amplifier_type_5g, uint, 0644);
MODULE_PARM_DESC(rtw_amplifier_type_5g, "BIT6:5G ext-PA, BIT7:5G ext-LNA");

uint rtw_RFE_type = CONFIG_RTW_RFE_TYPE;
module_param(rtw_RFE_type, uint, 0644);
MODULE_PARM_DESC(rtw_RFE_type, "default init value:64");

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

uint rtw_pll_ref_clk_sel = CONFIG_RTW_PLL_REF_CLK_SEL;
module_param(rtw_pll_ref_clk_sel, uint, 0644);
MODULE_PARM_DESC(rtw_pll_ref_clk_sel, "force pll_ref_clk_sel, 0xF:use autoload value");

#if defined(CONFIG_CALIBRATE_TX_POWER_BY_REGULATORY) //eFuse: Regulatory selection=1
int rtw_tx_pwr_lmt_enable = 1;
int rtw_tx_pwr_by_rate = 1;
#elif defined(CONFIG_CALIBRATE_TX_POWER_TO_MAX)//eFuse: Regulatory selection=0
int rtw_tx_pwr_lmt_enable = 0;
int rtw_tx_pwr_by_rate = 1;
#else //eFuse: Regulatory selection=2
#ifdef CONFIG_PCI_HCI
int rtw_tx_pwr_lmt_enable = 2; // 2- Depend on efuse
int rtw_tx_pwr_by_rate = 2;// 2- Depend on efuse
#else // USB & SDIO
int rtw_tx_pwr_lmt_enable = 0;
int rtw_tx_pwr_by_rate = 0;
#endif 
#endif

module_param(rtw_tx_pwr_lmt_enable, int, 0644);
MODULE_PARM_DESC(rtw_tx_pwr_lmt_enable,"0:Disable, 1:Enable, 2: Depend on efuse");

module_param(rtw_tx_pwr_by_rate, int, 0644);
MODULE_PARM_DESC(rtw_tx_pwr_by_rate,"0:Disable, 1:Enable, 2: Depend on efuse");

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

#ifdef CONFIG_IEEE80211_BAND_5GHZ
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

#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
char *rtw_phy_file_path = REALTEK_CONFIG_PATH;
module_param(rtw_phy_file_path, charp, 0644);
MODULE_PARM_DESC(rtw_phy_file_path, "The path of phy parameter");
// PHY FILE Bit Map
// BIT0 - MAC,				0: non-support, 1: support
// BIT1 - BB,					0: non-support, 1: support
// BIT2 - BB_PG,				0: non-support, 1: support
// BIT3 - BB_MP,				0: non-support, 1: support
// BIT4 - RF,					0: non-support, 1: support
// BIT5 - RF_TXPWR_TRACK,	0: non-support, 1: support
// BIT6 - RF_TXPWR_LMT,		0: non-support, 1: support
int rtw_load_phy_file = (BIT2|BIT6);
module_param(rtw_load_phy_file, int, 0644);
MODULE_PARM_DESC(rtw_load_phy_file,"PHY File Bit Map");
int rtw_decrypt_phy_file = 0;
module_param(rtw_decrypt_phy_file, int, 0644);
MODULE_PARM_DESC(rtw_decrypt_phy_file,"Enable Decrypt PHY File");
#endif

int _netdev_open(struct net_device *pnetdev);
int netdev_open (struct net_device *pnetdev);
static int netdev_close (struct net_device *pnetdev);
#ifdef CONFIG_PLATFORM_INTEL_BYT
extern int rtw_sdio_set_power(int on);
#endif //CONFIG_PLATFORM_INTEL_BYT

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

#ifdef CONFIG_IEEE80211_BAND_5GHZ
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

uint loadparam(_adapter *padapter)
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

	if (IsSupported24G(registry_par->wireless_mode) && (!IsSupported5G(registry_par->wireless_mode))
		&& (registry_par->channel > 14)) {
		registry_par->channel = 1;
	}
	else if (IsSupported5G(registry_par->wireless_mode) && (!IsSupported24G(registry_par->wireless_mode))
		&& (registry_par->channel <= 14)) {
		registry_par->channel = 36;
	}
	
	registry_par->vrtl_carrier_sense = (u8)rtw_vrtl_carrier_sense ;
	registry_par->vcs_type = (u8)rtw_vcs_type;
	registry_par->rts_thresh=(u16)rtw_rts_thresh;
	registry_par->frag_thresh=(u16)rtw_frag_thresh;
	registry_par->preamble = (u8)rtw_preamble;
	registry_par->scan_mode = (u8)rtw_scan_mode;
	registry_par->adhoc_tx_pwr = (u8)rtw_adhoc_tx_pwr;
	registry_par->soft_ap=  (u8)rtw_soft_ap;
	registry_par->smart_ps =  (u8)rtw_smart_ps;
	registry_par->check_fw_ps = (u8)rtw_check_fw_ps;
	registry_par->power_mgnt = (u8)rtw_power_mgnt;
	registry_par->ips_mode = (u8)rtw_ips_mode;
	registry_par->radio_enable = (u8)rtw_radio_enable;
	registry_par->long_retry_lmt = (u8)rtw_long_retry_lmt;
	registry_par->short_retry_lmt = (u8)rtw_short_retry_lmt;
  	registry_par->busy_thresh = (u16)rtw_busy_thresh;
  	//registry_par->qos_enable = (u8)rtw_qos_enable;
	registry_par->ack_policy = (u8)rtw_ack_policy;
	registry_par->mp_mode = (u8)rtw_mp_mode;
#if defined(CONFIG_MP_INCLUDED) && defined(CONFIG_RTW_CUSTOMER_STR)
	registry_par->mp_customer_str = (u8)rtw_mp_customer_str;
#endif
	registry_par->software_encrypt = (u8)rtw_software_encrypt;
	registry_par->software_decrypt = (u8)rtw_software_decrypt;

	registry_par->acm_method = (u8)rtw_acm_method;
	registry_par->usb_rxagg_mode = (u8)rtw_usb_rxagg_mode;

	 //UAPSD
	registry_par->wmm_enable = (u8)rtw_wmm_enable;
	registry_par->uapsd_enable = (u8)rtw_uapsd_enable;
	registry_par->uapsd_max_sp = (u8)rtw_uapsd_max_sp;
	registry_par->uapsd_acbk_en = (u8)rtw_uapsd_acbk_en;
	registry_par->uapsd_acbe_en = (u8)rtw_uapsd_acbe_en;
	registry_par->uapsd_acvi_en = (u8)rtw_uapsd_acvi_en;
	registry_par->uapsd_acvo_en = (u8)rtw_uapsd_acvo_en;

	registry_par->RegPwrTrimEnable = (u8)rtw_pwrtrim_enable;
	
#ifdef CONFIG_80211N_HT
	registry_par->ht_enable = (u8)rtw_ht_enable;
	registry_par->bw_mode = (u8)rtw_bw_mode;
	registry_par->ampdu_enable = (u8)rtw_ampdu_enable;
	registry_par->rx_stbc = (u8)rtw_rx_stbc;
	registry_par->ampdu_amsdu = (u8)rtw_ampdu_amsdu;
	registry_par->short_gi = (u8)rtw_short_gi;
	registry_par->ldpc_cap = (u8)rtw_ldpc_cap;
	registry_par->stbc_cap = (u8)rtw_stbc_cap;
	registry_par->beamform_cap = (u8)rtw_beamform_cap;
	registry_par->beamformer_rf_num = (u8)rtw_bfer_rf_number;
	registry_par->beamformee_rf_num = (u8)rtw_bfee_rf_number;
#endif

#ifdef CONFIG_80211AC_VHT
	registry_par->vht_enable = (u8)rtw_vht_enable;
	registry_par->ampdu_factor = (u8)rtw_ampdu_factor;
	registry_par->vht_rate_sel = (u8)rtw_vht_rate_sel;
#endif

#ifdef CONFIG_TX_EARLY_MODE
	registry_par->early_mode = (u8)rtw_early_mode;
#endif
	registry_par->lowrate_two_xmit = (u8)rtw_lowrate_two_xmit;
	registry_par->rf_config = (u8)rtw_rf_config;
	registry_par->low_power = (u8)rtw_low_power;


	registry_par->wifi_spec = (u8)rtw_wifi_spec;

	if (strlen(rtw_country_code) != 2
		|| is_alpha(rtw_country_code[0]) == _FALSE
		|| is_alpha(rtw_country_code[1]) == _FALSE
	) {
		if (rtw_country_code != rtw_country_unspecified)
			DBG_871X_LEVEL(_drv_err_, "%s discard rtw_country_code not in alpha2\n", __func__);
		_rtw_memset(registry_par->alpha2, 0xFF, 2);
	} else
		_rtw_memcpy(registry_par->alpha2, rtw_country_code, 2);

	registry_par->channel_plan = (u8)rtw_channel_plan;
	registry_par->special_rf_path = (u8)rtw_special_rf_path;

	registry_par->full_ch_in_p2p_handshake = (u8)rtw_full_ch_in_p2p_handshake;
#ifdef CONFIG_BT_COEXIST
	registry_par->btcoex = (u8)rtw_btcoex_enable;
	registry_par->bt_iso = (u8)rtw_bt_iso;
	registry_par->bt_sco = (u8)rtw_bt_sco;
	registry_par->bt_ampdu = (u8)rtw_bt_ampdu;
	registry_par->ant_num = (s8)rtw_ant_num;
#endif

	registry_par->bAcceptAddbaReq = (u8)rtw_AcceptAddbaReq;

	registry_par->antdiv_cfg = (u8)rtw_antdiv_cfg;
	registry_par->antdiv_type = (u8)rtw_antdiv_type;
	
	registry_par->switch_usb3 = (u8)rtw_switch_usb3;

#ifdef CONFIG_AUTOSUSPEND
	registry_par->usbss_enable = (u8)rtw_enusbss;//0:disable,1:enable
#endif
#ifdef SUPPORT_HW_RFOFF_DETECTED
	registry_par->hwpdn_mode = (u8)rtw_hwpdn_mode;//0:disable,1:enable,2:by EFUSE config
	registry_par->hwpwrp_detect = (u8)rtw_hwpwrp_detect;//0:disable,1:enable
#endif

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

#ifdef CONFIG_MULTI_VIR_IFACES
	registry_par->ext_iface_num = (u8)rtw_ext_iface_num;
#endif //CONFIG_MULTI_VIR_IFACES

	registry_par->pll_ref_clk_sel = (u8)rtw_pll_ref_clk_sel;

	registry_par->RegEnableTxPowerLimit = (u8)rtw_tx_pwr_lmt_enable;
	registry_par->RegEnableTxPowerByRate = (u8)rtw_tx_pwr_by_rate;

	rtw_regsty_load_target_tx_power(registry_par);

	registry_par->RegPowerBase = 14;
	registry_par->TxBBSwing_2G = (s8)rtw_TxBBSwing_2G;
	registry_par->TxBBSwing_5G = (s8)rtw_TxBBSwing_5G;
	registry_par->bEn_RFE = 1;
	registry_par->RFE_Type = (u8)rtw_RFE_type;
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
	registry_par->adaptivity_dml = (u8)rtw_adaptivity_dml;
	registry_par->adaptivity_dc_backoff = (u8)rtw_adaptivity_dc_backoff;
	registry_par->adaptivity_th_l2h_ini = (s8)rtw_adaptivity_th_l2h_ini;
	registry_par->adaptivity_th_edcca_hl_diff = (s8)rtw_adaptivity_th_edcca_hl_diff;

	registry_par->boffefusemask = (u8)rtw_OffEfuseMask;
	registry_par->bFileMaskEfuse = (u8)rtw_FileMaskEfuse;
#ifdef CONFIG_AUTO_CHNL_SEL_NHM
	registry_par->acs_mode = (u8)rtw_acs_mode;
	registry_par->acs_auto_scan = (u8)rtw_acs_auto_scan;
#endif
_func_exit_;

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
		DBG_871X(FUNC_ADPT_FMT": The net_device's is not in down state\n"
			, FUNC_ADPT_ARG(padapter));

		return ret;
	}

	/* if the net_device is linked, it's not permit to modify mac addr */
	if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) ||
		check_fwstate(pmlmepriv, _FW_LINKED) ||
		check_fwstate(pmlmepriv, _FW_UNDER_SURVEY)) {
		DBG_871X(FUNC_ADPT_FMT": The net_device's is not idle currently\n"
			, FUNC_ADPT_ARG(padapter));

		return ret;
	}

	/* check whether the input mac address is valid to permit modifying mac addr */
	if (rtw_check_invalid_mac_address(sa->sa_data, _FALSE) == _TRUE) {
		DBG_871X(FUNC_ADPT_FMT": Invalid Mac Addr for "MAC_FMT"\n"
			, FUNC_ADPT_ARG(padapter), MAC_ARG(sa->sa_data));

		return ret;
	}

	_rtw_memcpy(adapter_mac_addr(padapter), sa->sa_data, ETH_ALEN); /* set mac addr to adapter */
	_rtw_memcpy(pnetdev->dev_addr, sa->sa_data, ETH_ALEN); /* set mac addr to net_device */

	rtw_ps_deny(padapter, PS_DENY_IOCTL);
	LeaveAllPowerSaveModeDirect(padapter); /* leave PS mode for guaranteeing to access hw register successfully */
	rtw_hal_set_hwreg(padapter, HW_VAR_MAC_ADDR, sa->sa_data); /* set mac addr to mac register */
	rtw_ps_deny_cancel(padapter, PS_DENY_IOCTL);

	DBG_871X(FUNC_ADPT_FMT": Set Mac Addr to "MAC_FMT" Successfully\n"
		, FUNC_ADPT_ARG(padapter), MAC_ARG(sa->sa_data));

	ret = 0;

	return ret;
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

 
static u16 rtw_select_queue(struct net_device *dev, struct sk_buff *skb
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0) 	
				, void *accel_priv
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0) 
				, select_queue_fallback_t fallback
#endif

#endif
)
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
static int rtw_ndev_notifier_call(struct notifier_block * nb, unsigned long state, void *ptr)
{	
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(3,11,0))
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
#else
	struct net_device *dev = ptr;
#endif

	if (dev == NULL)
		return NOTIFY_DONE;

#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,29))
	if (dev->netdev_ops == NULL)
		return NOTIFY_DONE;

	if (dev->netdev_ops->ndo_do_ioctl == NULL)
		return NOTIFY_DONE;

	if (dev->netdev_ops->ndo_do_ioctl != rtw_ioctl)
#else
	if (dev->do_ioctl == NULL)
		return NOTIFY_DONE;

	if (dev->do_ioctl != rtw_ioctl)
#endif
		return NOTIFY_DONE;

	DBG_871X_LEVEL(_drv_info_, FUNC_NDEV_FMT" state:%lu\n", FUNC_NDEV_ARG(dev), state);

	switch (state) {
	case NETDEV_CHANGENAME:
		rtw_adapter_proc_replace(dev);
		break;
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

	DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" if%d mac_addr="MAC_FMT"\n"
		, FUNC_ADPT_ARG(adapter), (adapter->iface_id+1), MAC_ARG(dev->dev_addr));
	strncpy(adapter->old_ifname, dev->name, IFNAMSIZ);
	adapter->old_ifname[IFNAMSIZ-1] = '\0';
	rtw_adapter_proc_init(dev);

	return 0;
}

void rtw_ndev_uninit(struct net_device *dev)
{
	_adapter *adapter = rtw_netdev_priv(dev);

	DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" if%d\n"
		, FUNC_ADPT_ARG(adapter), (adapter->iface_id+1));
	rtw_adapter_proc_deinit(dev);
}

#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,29))
static const struct net_device_ops rtw_netdev_ops = {
	.ndo_init = rtw_ndev_init,
	.ndo_uninit = rtw_ndev_uninit,
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

			padapter->DriverState = DRIVER_REPLACE_DONGLE;
		}
	}
#endif //CONFIG_EASY_REPLACEMENT

	if(dev_alloc_name(pnetdev, ifname) < 0)
	{
		RT_TRACE(_module_os_intfs_c_,_drv_err_,("dev_alloc_name, fail! \n"));
	}

	netif_carrier_off(pnetdev);
	//rtw_netif_stop_queue(pnetdev);

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

	rtw_hook_if_ops(pnetdev);

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
		goto free_ndev;
	}
#endif

	ret = _SUCCESS;

free_ndev:
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

	if (adapter->pnetdev) {
		rtw_free_netdev(adapter->pnetdev);
		adapter->pnetdev = NULL;
	}
}

int rtw_os_ndev_register(_adapter *adapter, char *name)
{
	int ret = _SUCCESS;
	struct net_device *ndev = adapter->pnetdev;

#if defined(CONFIG_IOCTL_CFG80211)
	if (rtw_cfg80211_ndev_res_register(adapter) != _SUCCESS) {
		rtw_warn_on(1);
		ret = _FAIL;
		goto exit;
	}
#endif

	/* alloc netdev name */
	rtw_init_netdev_name(ndev, name);

	_rtw_memcpy(ndev->dev_addr, adapter_mac_addr(adapter), ETH_ALEN);

	/* Tell the network stack we exist */
	if (register_netdev(ndev) != 0) {
		DBG_871X(FUNC_NDEV_FMT" if%d Failed!\n", FUNC_NDEV_ARG(ndev), (adapter->iface_id+1));
		ret = _FAIL;
	}

#if defined(CONFIG_IOCTL_CFG80211)
	if (ret != _SUCCESS) {
		rtw_cfg80211_ndev_res_unregister(adapter);
		#if !defined(RTW_SINGLE_WIPHY)
		rtw_wiphy_unregister(adapter_to_wiphy(adapter));
		#endif
	}
#endif

exit:
	return ret;
}

void rtw_os_ndev_unregister(_adapter *adapter)
{
	struct net_device *netdev = NULL;

	if (adapter == NULL)
		return;

	adapter->ndev_unregistering = 1;

	netdev = adapter->pnetdev;

#if defined(CONFIG_IOCTL_CFG80211)
	rtw_cfg80211_ndev_res_unregister(adapter);
#endif

	if ((adapter->DriverState != DRIVER_DISAPPEAR) && netdev)
		unregister_netdev(netdev); /* will call netdev_close() */

#if defined(CONFIG_IOCTL_CFG80211) && !defined(RTW_SINGLE_WIPHY)
	rtw_wiphy_unregister(adapter_to_wiphy(adapter));
#endif

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
int rtw_os_ndev_init(_adapter *adapter, char *name)
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
		status = _FAIL;
		goto exit;
	}
#endif

	for (i = 0; i < dvobj->iface_nums; i++) {

		if (i >= IFACE_ID_MAX) {
			DBG_871X_LEVEL(_drv_err_, "%s %d >= IFACE_ID_MAX\n", __func__, i);
			rtw_warn_on(1);
			continue;
		}

		adapter = dvobj->padapters[i];
		if (adapter && !adapter->pnetdev) {
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
exit:
	return status;
}

void rtw_os_ndevs_free(struct dvobj_priv *dvobj)
{
	int i;
	_adapter *adapter = NULL;

	for (i = 0; i < dvobj->iface_nums; i++) {

		if (i >= IFACE_ID_MAX) {
			DBG_871X_LEVEL(_drv_err_, "%s %d >= IFACE_ID_MAX\n", __func__, i);
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

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("+rtw_start_drv_threads\n"));

#ifdef CONFIG_XMIT_THREAD_MODE
#if defined(CONFIG_SDIO_HCI)
	if (is_primary_adapter(padapter))
#endif		
	{
		padapter->xmitThread = kthread_run(rtw_xmit_thread, padapter, "RTW_XMIT_THREAD");
		if(IS_ERR(padapter->xmitThread))
			_status = _FAIL;
	}
#endif //#ifdef CONFIG_XMIT_THREAD_MODE

#ifdef CONFIG_RECV_THREAD_MODE
	padapter->recvThread = kthread_run(rtw_recv_thread, padapter, "RTW_RECV_THREAD");
	if(IS_ERR(padapter->recvThread))
		_status = _FAIL;
#endif

	if (is_primary_adapter(padapter)) {
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

void rtw_stop_drv_threads (_adapter *padapter)
{
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("+rtw_stop_drv_threads\n"));

	if (is_primary_adapter(padapter))
		rtw_stop_cmd_thread(padapter);

#ifdef CONFIG_EVENT_THREAD_MODE
        _rtw_up_sema(&padapter->evtpriv.evt_notify);
	if(padapter->evtThread){
		_rtw_down_sema(&padapter->evtpriv.terminate_evtthread_sema);
	}
#endif

#ifdef CONFIG_XMIT_THREAD_MODE
	// Below is to termindate tx_thread...
#if defined(CONFIG_SDIO_HCI) 
	// Only wake-up primary adapter
	if (is_primary_adapter(padapter))
#endif  /*SDIO_HCI */
	{
		_rtw_up_sema(&padapter->xmitpriv.xmit_sema);
		_rtw_down_sema(&padapter->xmitpriv.terminate_xmitthread_sema);
	}
	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("\n drv_halt: rtw_xmit_thread can be terminated !\n"));
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
#ifdef CONFIG_GTK_OL
	psecuritypriv->binstallKCK_KEK = _FAIL;
#endif //CONFIG_GTK_OL
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
	RTW_ENABLE_FUNC(padapter, DF_RX_BIT);
	RTW_ENABLE_FUNC(padapter, DF_TX_BIT);
	padapter->bLinkInfoDump = 0;
	padapter->bNotifyChannelChange = _FALSE;
#ifdef CONFIG_P2P
	padapter->bShowGetP2PState = 1;
#endif

	//for debug purpose
	padapter->fix_rate = 0xFF;
	padapter->data_fb = 0;
	padapter->driver_ampdu_spacing = 0xFF;
	padapter->driver_rx_ampdu_factor =  0xFF;
	padapter->driver_rx_ampdu_spacing = 0xFF;
	padapter->fix_rx_ampdu_accept = RX_AMPDU_ACCEPT_INVALID;
	padapter->fix_rx_ampdu_size = RX_AMPDU_SIZE_INVALID;
#ifdef DBG_RX_COUNTER_DUMP
	padapter->dump_rx_cnt_mode = 0;
	padapter->drv_rx_cnt_ok = 0;
	padapter->drv_rx_cnt_crcerror = 0;
	padapter->drv_rx_cnt_drop = 0;
#endif	
	return ret;
}

struct dvobj_priv *devobj_init(void)
{
	struct dvobj_priv *pdvobj = NULL;

	if ((pdvobj = (struct dvobj_priv*)rtw_zmalloc(sizeof(*pdvobj))) == NULL) 
	{
		return NULL;
	}

	_rtw_mutex_init(&pdvobj->hw_init_mutex);
	_rtw_mutex_init(&pdvobj->h2c_fwcmd_mutex);
	_rtw_mutex_init(&pdvobj->setch_mutex);
	_rtw_mutex_init(&pdvobj->setbw_mutex);
	_rtw_mutex_init(&pdvobj->rf_read_reg_mutex);
#ifdef CONFIG_SDIO_INDIRECT_ACCESS
	_rtw_mutex_init(&pdvobj->sd_indirect_access_mutex);
#endif

#ifdef CONFIG_RTW_CUSTOMER_STR
	_rtw_mutex_init(&pdvobj->customer_str_mutex);
	_rtw_memset(pdvobj->customer_str, 0xFF, RTW_CUSTOMER_STR_LEN);
#endif

	pdvobj->processing_dev_remove = _FALSE;

	ATOMIC_SET(&pdvobj->disable_func, 0);

	rtw_macid_ctl_init(&pdvobj->macid_ctl);
	_rtw_spinlock_init(&pdvobj->cam_ctl.lock);
	_rtw_mutex_init(&pdvobj->cam_ctl.sec_cam_access_mutex);

	return pdvobj;

}

void devobj_deinit(struct dvobj_priv *pdvobj)
{
	if(!pdvobj)
		return;

	/* TODO: use rtw_os_ndevs_deinit instead at the first stage of driver's dev deinit function */
#if defined(CONFIG_IOCTL_CFG80211)
	rtw_cfg80211_dev_res_free(pdvobj);
#endif

	_rtw_mutex_free(&pdvobj->hw_init_mutex);
	_rtw_mutex_free(&pdvobj->h2c_fwcmd_mutex);

#ifdef CONFIG_RTW_CUSTOMER_STR
	_rtw_mutex_free(&pdvobj->customer_str_mutex);
#endif

	_rtw_mutex_free(&pdvobj->setch_mutex);
	_rtw_mutex_free(&pdvobj->setbw_mutex);
	_rtw_mutex_free(&pdvobj->rf_read_reg_mutex);
#ifdef CONFIG_SDIO_INDIRECT_ACCESS
	_rtw_mutex_free(&pdvobj->sd_indirect_access_mutex);
#endif

	rtw_macid_ctl_deinit(&pdvobj->macid_ctl);
	_rtw_spinlock_free(&pdvobj->cam_ctl.lock);
	_rtw_mutex_free(&pdvobj->cam_ctl.sec_cam_access_mutex);

	rtw_mfree((u8*)pdvobj, sizeof(*pdvobj));
}	

u8 rtw_reset_drv_sw(_adapter *padapter)
{
	u8	ret8=_SUCCESS;
	struct mlme_priv *pmlmepriv= &padapter->mlmepriv;
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	//hal_priv
	if( is_primary_adapter(padapter))
		rtw_hal_def_value_init(padapter);

	RTW_ENABLE_FUNC(padapter, DF_RX_BIT);
	RTW_ENABLE_FUNC(padapter, DF_TX_BIT);
	padapter->bLinkInfoDump = 0;

	padapter->xmitpriv.tx_pkts = 0;
	padapter->recvpriv.rx_pkts = 0;

	pmlmepriv->LinkDetectInfo.bBusyTraffic = _FALSE;

	//pmlmepriv->LinkDetectInfo.TrafficBusyState = _FALSE;
	pmlmepriv->LinkDetectInfo.TrafficTransitionCount = 0;
	pmlmepriv->LinkDetectInfo.LowPowerTransitionCount = 0;

	_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY |_FW_UNDER_LINKING);

#ifdef CONFIG_AUTOSUSPEND
	#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,22) && LINUX_VERSION_CODE<=KERNEL_VERSION(2,6,34))
		adapter_to_dvobj(padapter)->pusbdev->autosuspend_disabled = 1;//autosuspend disabled by the user
	#endif
#endif

#ifdef DBG_CONFIG_ERROR_DETECT
	if (is_primary_adapter(padapter))
		rtw_hal_sreset_reset_value(padapter);
#endif
	pwrctrlpriv->pwr_state_check_cnts = 0;

	//mlmeextpriv
	mlmeext_set_scan_state(&padapter->mlmeextpriv, SCAN_DISABLE);

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

	ret8 = rtw_init_default_value(padapter);

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
	padapter->setband = WIFI_FREQUENCY_BAND_AUTO;
	padapter->fix_rate = 0xFF;
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

	//_rtw_memset((u8 *)&padapter->qospriv, 0, sizeof (struct qos_priv));//move to mlme_priv

#ifdef CONFIG_MP_INCLUDED
	if (init_mp_priv(padapter) == _FAIL) {
		DBG_871X("%s: initialize MP private data Fail!\n", __func__);
	}
#endif

	rtw_hal_dm_init(padapter);
#ifdef CONFIG_SW_LED
	rtw_hal_sw_led_init(padapter);
#endif
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
	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("rtw_cancel_all_timer:cancel association timer complete!\n"));

	#if 0
	_cancel_timer_ex(&padapter->securitypriv.tkip_timer);
	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("rtw_cancel_all_timer:cancel tkip_timer!\n"));
	#endif

	_cancel_timer_ex(&padapter->mlmepriv.scan_to_timer);
	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("rtw_cancel_all_timer:cancel scan_to_timer!\n"));

	#ifdef CONFIG_DFS_MASTER
	_cancel_timer_ex(&padapter->mlmepriv.dfs_master_timer);
	#endif

	_cancel_timer_ex(&padapter->mlmepriv.dynamic_chk_timer);
	RT_TRACE(_module_os_intfs_c_, _drv_info_, ("rtw_cancel_all_timer:cancel dynamic_chk_timer!\n"));

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
	//cancel dm timer
	rtw_hal_dm_deinit(padapter);

#ifdef CONFIG_PLATFORM_FS_MX61
	msleep(50);
#endif
}

u8 rtw_free_drv_sw(_adapter *padapter)
{
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
#ifdef CONFIG_MULTI_VIR_IFACES
int _netdev_vir_if_open(struct net_device *pnetdev)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	_adapter *primary_padapter = GET_PRIMARY_ADAPTER(padapter);

	DBG_871X(FUNC_NDEV_FMT" enter\n", FUNC_NDEV_ARG(pnetdev));

	if(!primary_padapter)
		goto _netdev_virtual_iface_open_error;

	if (primary_padapter->bup == _FALSE || !rtw_is_hw_init_completed(primary_padapter))
		_netdev_open(primary_padapter->pnetdev);

	if(padapter->bup == _FALSE && primary_padapter->bup == _TRUE &&
		rtw_is_hw_init_completed(primary_padapter))
	{
		padapter->bFWReady = primary_padapter->bFWReady;

		if(rtw_start_drv_threads(padapter) == _FAIL)
		{
			goto _netdev_virtual_iface_open_error;
		}

#ifdef CONFIG_IOCTL_CFG80211
		rtw_cfg80211_init_wiphy(padapter);
#endif

		padapter->bup = _TRUE;

	}

	padapter->net_closed = _FALSE;

	_set_timer(&padapter->mlmepriv.dynamic_chk_timer, 2000);

	rtw_netif_wake_queue(pnetdev);

	DBG_871X(FUNC_NDEV_FMT" exit\n", FUNC_NDEV_ARG(pnetdev));
	return 0;

_netdev_virtual_iface_open_error:

	padapter->bup = _FALSE;

	netif_carrier_off(pnetdev);
	rtw_netif_stop_queue(pnetdev);

	return (-1);

}

int netdev_vir_if_open(struct net_device *pnetdev)
{
	int ret;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);

	_enter_critical_mutex(&(adapter_to_dvobj(padapter)->hw_init_mutex), NULL);
	ret = _netdev_vir_if_open(pnetdev);
	_exit_critical_mutex(&(adapter_to_dvobj(padapter)->hw_init_mutex), NULL);

#ifdef CONFIG_AUTO_AP_MODE
	//if(padapter->iface_id == 2)
	//	rtw_start_auto_ap(padapter);
#endif

	return ret;
}

static int netdev_vir_if_close(struct net_device *pnetdev)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);

	padapter->net_closed = _TRUE;

	if(pnetdev)
	{
		rtw_netif_stop_queue(pnetdev);
	}

#ifdef CONFIG_IOCTL_CFG80211
	rtw_scan_abort(padapter);
	rtw_cfg80211_wait_scan_req_empty(padapter, 200);
	adapter_wdev_data(padapter)->bandroid_scan = _FALSE;
#endif

	return 0;
}

#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,29))
static const struct net_device_ops rtw_netdev_vir_if_ops = {
	 .ndo_open = netdev_vir_if_open,
        .ndo_stop = netdev_vir_if_close,
        .ndo_start_xmit = rtw_xmit_entry,
        .ndo_set_mac_address = rtw_net_set_mac_address,
        .ndo_get_stats = rtw_net_get_stats,
        .ndo_do_ioctl = rtw_ioctl,
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
	.ndo_select_queue	= rtw_select_queue,
#endif
};
#endif

void rtw_hook_vir_if_ops(struct net_device *ndev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
	ndev->netdev_ops = &rtw_netdev_vir_if_ops;
#else
	ndev->open = netdev_vir_if_open;
	ndev->stop = netdev_vir_if_close;
	ndev->set_mac_address = rtw_net_set_mac_address;
#endif
}

_adapter *rtw_drv_add_vir_if(_adapter *primary_padapter,
	void (*set_intf_ops)(_adapter *primary_padapter,struct _io_ops *pops))
{
	int res = _FAIL;
	_adapter *padapter = NULL;
	struct dvobj_priv *pdvobjpriv;
	u8 mac[ETH_ALEN];

/*
	if((primary_padapter->bup == _FALSE) ||
		(rtw_buddy_adapter_up(primary_padapter) == _FALSE))
		goto exit;
*/

	/****** init adapter ******/
	padapter = (_adapter *)rtw_zvmalloc(sizeof(*padapter));
	if (padapter == NULL)
		goto exit;

	if (loadparam(padapter) != _SUCCESS)
		goto free_adapter;

	_rtw_memcpy(padapter, primary_padapter, sizeof(_adapter));

	//
	padapter->bup = _FALSE;
	padapter->net_closed = _TRUE;
	padapter->dir_dev = NULL;
	padapter->dir_odm = NULL;


	//set adapter_type/iface type
	padapter->isprimary = _FALSE;
	padapter->adapter_type = MAX_ADAPTER;
	padapter->pbuddy_adapter = primary_padapter;
#if 0
#ifndef CONFIG_HWPORT_SWAP	//Port0 -> Pri , Port1 -> Sec
	padapter->iface_type = IFACE_PORT1;
#else
	padapter->iface_type = IFACE_PORT0;
#endif  //CONFIG_HWPORT_SWAP
#else
	//extended virtual interfaces always are set to port0
	padapter->iface_type = IFACE_PORT0;
#endif

	/****** hook vir if into dvobj ******/
	pdvobjpriv = adapter_to_dvobj(padapter);
	padapter->iface_id = pdvobjpriv->iface_nums;
	pdvobjpriv->padapters[pdvobjpriv->iface_nums++] = padapter;

	padapter->intf_start = NULL;
	padapter->intf_stop = NULL;

	//step init_io_priv
	if ((rtw_init_io_priv(padapter, set_intf_ops)) == _FAIL) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("\n Can't init io_reqs\n"));
		goto free_adapter;
	}

	//init drv data
	if(rtw_init_drv_sw(padapter)!= _SUCCESS)
		goto free_drv_sw;


	//get mac address from primary_padapter
	_rtw_memcpy(mac, adapter_mac_addr(primary_padapter), ETH_ALEN);

	/*
	* If the BIT1 is 0, the address is universally administered.
	* If it is 1, the address is locally administered
	*/
#if 1 /* needs enable MBSSID CAM */
	mac[0] |= BIT(1);
	mac[0] |= (padapter->iface_id-1)<<4;
#endif

	_rtw_memcpy(adapter_mac_addr(padapter), mac, ETH_ALEN);

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
	struct net_device *pnetdev=NULL;

	if (padapter == NULL)
		return;

	pnetdev = padapter->pnetdev;


	if (padapter->bup == _TRUE)
	{
		#ifdef CONFIG_XMIT_ACK
		if (padapter->xmitpriv.ack_tx)
			rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_DRV_STOP);
		#endif

		if (padapter->intf_stop)
		{
			padapter->intf_stop(padapter);
		}

		rtw_stop_drv_threads(padapter);

		padapter->bup = _FALSE;
	}

	/* cancel timer after thread stop */
	rtw_cancel_all_timer(padapter);
}

void rtw_drv_free_vir_if(_adapter *padapter)
{
	if (padapter == NULL)
		return;

	padapter->pbuddy_adapter = NULL;

	rtw_free_drv_sw(padapter);

	/* TODO: use rtw_os_ndevs_deinit instead at the first stage of driver's dev deinit function */
	rtw_os_ndev_free(padapter);

	rtw_vmfree((u8 *)padapter, sizeof(_adapter));
}

void rtw_drv_stop_vir_ifaces(struct dvobj_priv *dvobj)
{
	int i;
	//struct dvobj_priv *dvobj = primary_padapter->dvobj;

	for(i=2;i<dvobj->iface_nums;i++)
	{
		rtw_drv_stop_vir_if(dvobj->padapters[i]);
	}
}

void rtw_drv_free_vir_ifaces(struct dvobj_priv *dvobj)
{
	int i;
	//struct dvobj_priv *dvobj = primary_padapter->dvobj;

	for(i=2;i<dvobj->iface_nums;i++)
	{
		rtw_drv_free_vir_if(dvobj->padapters[i]);
	}
}

void rtw_drv_del_vir_if(_adapter *padapter)
{
	rtw_drv_stop_vir_if(padapter);
	rtw_drv_free_vir_if(padapter);
}

void rtw_drv_del_vir_ifaces(_adapter *primary_padapter)
{
	int i;
	struct dvobj_priv *dvobj = primary_padapter->dvobj;

	for(i=2;i<dvobj->iface_nums;i++)
	{
		rtw_drv_del_vir_if(dvobj->padapters[i]);
	}
}
#endif //CONFIG_MULTI_VIR_IFACES

int _netdev_if2_open(struct net_device *pnetdev)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	_adapter *primary_padapter = padapter->pbuddy_adapter;

	DBG_871X("+871x_drv - if2_open, bup=%d\n", padapter->bup);

#ifdef CONFIG_PLATFORM_INTEL_BYT
	if (padapter->bup == _FALSE)
	{
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
		rtw_init_wifidirect_addrs(padapter, adapter_mac_addr(padapter), adapter_mac_addr(padapter));
		_rtw_memcpy(pnetdev->dev_addr, adapter_mac_addr(padapter), ETH_ALEN);
	}
#endif //CONFIG_PLATFORM_INTEL_BYT

	if (primary_padapter->bup == _FALSE || !rtw_is_hw_init_completed(primary_padapter))
		_netdev_open(primary_padapter->pnetdev);

	if(padapter->bup == _FALSE && primary_padapter->bup == _TRUE &&
		rtw_is_hw_init_completed(primary_padapter))
	{
		padapter->bFWReady = primary_padapter->bFWReady;

		//if (init_mlme_ext_priv(padapter) == _FAIL)
		//	goto netdev_if2_open_error;


		if (rtw_start_drv_threads(padapter) == _FAIL)
		{
			goto netdev_if2_open_error;
		}


		if (padapter->intf_start)
		{
			padapter->intf_start(padapter);
		}

#ifdef CONFIG_IOCTL_CFG80211
		rtw_cfg80211_init_wiphy(padapter);
#endif

		padapter->bup = _TRUE;

	}

	padapter->net_closed = _FALSE;

	//execute dynamic_chk_timer only on primary interface
	// secondary interface shares the timer with primary interface.
	//_set_timer(&padapter->mlmepriv.dynamic_chk_timer, 2000);

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
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	if (pwrctrlpriv->bInSuspend == _TRUE)
	{
		DBG_871X("+871x_drv - netdev_if2_open, bInSuspend=%d\n", pwrctrlpriv->bInSuspend);
		return 0;
	}

	_enter_critical_mutex(&(adapter_to_dvobj(padapter)->hw_init_mutex), NULL);
	ret = _netdev_if2_open(pnetdev);
	_exit_critical_mutex(&(adapter_to_dvobj(padapter)->hw_init_mutex), NULL);

#ifdef CONFIG_AUTO_AP_MODE
	//if(padapter->iface_id == 2)
		rtw_start_auto_ap(padapter);
#endif

	return ret;
}

static int netdev_if2_close(struct net_device *pnetdev)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;

	padapter->net_closed = _TRUE;
	pmlmepriv->LinkDetectInfo.bBusyTraffic = _FALSE;

	if(pnetdev)
	{
		rtw_netif_stop_queue(pnetdev);
	}

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

#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,29))
static const struct net_device_ops rtw_netdev_if2_ops = {
	.ndo_init = rtw_ndev_init,
	.ndo_uninit = rtw_ndev_uninit,
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

void rtw_hook_if2_ops(struct net_device *ndev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
	ndev->netdev_ops = &rtw_netdev_if2_ops;
#else
	ndev->init = rtw_ndev_init;
	ndev->uninit = rtw_ndev_uninit;
	ndev->open = netdev_if2_open;
	ndev->stop = netdev_if2_close;
	ndev->set_mac_address = rtw_net_set_mac_address;
#endif
}

_adapter *rtw_drv_if2_init(_adapter *primary_padapter, 
	void (*set_intf_ops)(_adapter *primary_padapter,struct _io_ops *pops))
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

	_rtw_memcpy(padapter, primary_padapter, sizeof(*padapter));

	//
	padapter->bup = _FALSE;
	padapter->net_closed = _TRUE;
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

	/****** hook if2 into dvobj ******/
	pdvobjpriv = adapter_to_dvobj(padapter);
	pdvobjpriv->padapters[pdvobjpriv->iface_nums++] = padapter;

	//
	padapter->intf_start = primary_padapter->intf_start;
	padapter->intf_stop = primary_padapter->intf_stop;

	//step init_io_priv
	if ((rtw_init_io_priv(padapter, set_intf_ops)) == _FAIL) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("\n Can't init io_reqs\n"));
		goto free_adapter;
	}

	//init drv data
	if(rtw_init_drv_sw(padapter)!= _SUCCESS)
		goto free_drv_sw;


	/* get mac address from primary_padapter */
	_rtw_memcpy(mac, adapter_mac_addr(primary_padapter), ETH_ALEN);

	/*
	* If the BIT1 is 0, the address is universally administered.
	* If it is 1, the address is locally administered
	*/
	mac[0] |= BIT(1);

	_rtw_memcpy(adapter_mac_addr(padapter), mac, ETH_ALEN);
	rtw_init_wifidirect_addrs(padapter, adapter_mac_addr(padapter), adapter_mac_addr(padapter));

	primary_padapter->pbuddy_adapter = padapter;

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

void rtw_drv_if2_free(_adapter *if2)
{
	_adapter *padapter = if2;

	if (padapter == NULL)
		return;

	rtw_free_drv_sw(padapter);

	/* TODO: use rtw_os_ndevs_deinit instead at the first stage of driver's dev deinit function */
	rtw_os_ndev_free(padapter);

	rtw_vmfree((u8 *)padapter, sizeof(_adapter));
}

void rtw_drv_if2_stop(_adapter *if2)
{
	_adapter *padapter = if2;
	struct net_device *pnetdev = NULL;

	if (padapter == NULL)
		return;


	if (padapter->bup == _TRUE) {
		#ifdef CONFIG_XMIT_ACK
		if (padapter->xmitpriv.ack_tx)
			rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_DRV_STOP);
		#endif

		if (padapter->intf_stop)
		{
			padapter->intf_stop(padapter);
		}

		rtw_stop_drv_threads(padapter);

		padapter->bup = _FALSE;
	}

	/* cancel timer after thread stop */
	rtw_cancel_all_timer(padapter);
}
#endif //end of CONFIG_CONCURRENT_MODE

int rtw_os_ndevs_register(struct dvobj_priv *dvobj)
{
	int i, status = _SUCCESS;
	struct registry_priv *regsty = dvobj_to_regsty(dvobj);
	_adapter *adapter;

#if defined(CONFIG_IOCTL_CFG80211)
	if (rtw_cfg80211_dev_res_register(dvobj) != _SUCCESS) {
		rtw_warn_on(1);
		status = _FAIL;
		goto exit;
	}
#endif

	for (i = 0; i < dvobj->iface_nums; i++) {

		if (i >= IFACE_ID_MAX) {
			DBG_871X_LEVEL(_drv_err_, "%s %d >= IFACE_ID_MAX\n", __func__, i);
			rtw_warn_on(1);
			continue;
		}

		adapter = dvobj->padapters[i];
		if (adapter) {
			char *name;

			if (adapter->iface_id == IFACE_ID0)
				name = regsty->ifname;
			else if (adapter->iface_id == IFACE_ID1)
				name = regsty->if2name;
			else
				name = "wlan%d";

			#ifdef CONFIG_CONCURRENT_MODE
			switch (adapter->adapter_type) {
			case SECONDARY_ADAPTER:
				rtw_hook_if2_ops(adapter->pnetdev);
				break;
			#ifdef CONFIG_MULTI_VIR_IFACES
			case MAX_ADAPTER:
				rtw_hook_vir_if_ops(adapter->pnetdev);
				break;
			#endif
			}
			#endif /* CONFIG_CONCURRENT_MODE */

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
exit:
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
				printk("%s()-%d: dev_get_by_name(%s) failed!", __FUNCTION__, __LINE__, CONFIG_BR_EXT_BRNAME);
		}

		adapter->ethBrExtInfo.addPPPoETag = 1;
	}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35))
	rcu_read_unlock();
#endif	// (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35))
}
#endif //CONFIG_BR_EXT

int _netdev_open(struct net_device *pnetdev)
{
	uint status;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);
#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
#endif //CONFIG_BT_COEXIST_SOCKET_TRX

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("+871x_drv - dev_open\n"));
	DBG_871X("+871x_drv - drv_open, bup=%d\n", padapter->bup);

	padapter->netif_up = _TRUE;

#ifdef CONFIG_PLATFORM_INTEL_BYT
	rtw_sdio_set_power(1);
#endif //CONFIG_PLATFORM_INTEL_BYT

	if(pwrctrlpriv->ps_flag == _TRUE){
		padapter->net_closed = _FALSE;
		goto netdev_open_normal_process;
	}

	if(padapter->bup == _FALSE)
	{
#ifdef CONFIG_PLATFORM_INTEL_BYT
		rtw_macaddr_cfg(adapter_mac_addr(padapter),  get_hal_mac_addr(padapter));
		rtw_init_wifidirect_addrs(padapter, adapter_mac_addr(padapter), adapter_mac_addr(padapter));
		_rtw_memcpy(pnetdev->dev_addr, adapter_mac_addr(padapter), ETH_ALEN);
#endif //CONFIG_PLATFORM_INTEL_BYT

		rtw_clr_surprise_removed(padapter);
		rtw_clr_drv_stopped(padapter);

		status = rtw_hal_init(padapter);
		if (status ==_FAIL)
		{
			RT_TRACE(_module_os_intfs_c_,_drv_err_,("rtl871x_hal_init(): Can't init h/w!\n"));
			goto netdev_open_error;
		}

		DBG_871X("MAC Address = "MAC_FMT"\n", MAC_ARG(pnetdev->dev_addr));

		status=rtw_start_drv_threads(padapter);
		if(status ==_FAIL)
		{
			DBG_871X("Initialize driver software resource Failed!\n");
			goto netdev_open_error;
		}

#ifdef CONFIG_DRVEXT_MODULE
		init_drvext(padapter);
#endif

		if (padapter->intf_start)
		{
			padapter->intf_start(padapter);
		}

#ifdef CONFIG_IOCTL_CFG80211
		rtw_cfg80211_init_wiphy(padapter);
#endif

		rtw_led_control(padapter, LED_CTL_NO_LINK);

		padapter->bup = _TRUE;
		pwrctrlpriv->bips_processing = _FALSE;

#ifdef CONFIG_PLATFORM_INTEL_BYT
#ifdef CONFIG_BT_COEXIST	
		rtw_btcoex_IpsNotify(padapter, IPS_NONE);
#endif // CONFIG_BT_COEXIST
#endif //CONFIG_PLATFORM_INTEL_BYT		
	}
	padapter->net_closed = _FALSE;

	_set_timer(&padapter->mlmepriv.dynamic_chk_timer, 2000);

#ifndef CONFIG_IPS_CHECK_IN_WD
	rtw_set_pwr_state_check_timer(pwrctrlpriv);
#endif 

	//netif_carrier_on(pnetdev);//call this func when rtw_joinbss_event_callback return success
	rtw_netif_wake_queue(pnetdev);

#ifdef CONFIG_BR_EXT
	netdev_br_init(pnetdev);
#endif	// CONFIG_BR_EXT

#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
	if(is_primary_adapter(padapter) &&  _TRUE == pHalData->EEPROMBluetoothCoexist)
	{
		rtw_btcoex_init_socket(padapter);
		padapter->coex_info.BtMgnt.ExtConfig.HCIExtensionVer = 0x04;
		rtw_btcoex_SetHciVersion(padapter,0x04);
	}
	else
		DBG_871X("CONFIG_BT_COEXIST: SECONDARY_ADAPTER\n");
#endif //CONFIG_BT_COEXIST_SOCKET_TRX


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
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	if (pwrctrlpriv->bInSuspend == _TRUE)
	{
		DBG_871X("+871x_drv - drv_open, bInSuspend=%d\n", pwrctrlpriv->bInSuspend);
		return 0;
	}

	_enter_critical_mutex(&(adapter_to_dvobj(padapter)->hw_init_mutex), NULL);
	ret = _netdev_open(pnetdev);
	_exit_critical_mutex(&(adapter_to_dvobj(padapter)->hw_init_mutex), NULL);

	return ret;
}

#ifdef CONFIG_IPS
int  ips_netdrv_open(_adapter *padapter)
{
	int status = _SUCCESS;
	//struct pwrctrl_priv	*pwrpriv = adapter_to_pwrctl(padapter);
	
	padapter->net_closed = _FALSE;

	DBG_871X("===> %s.........\n",__FUNCTION__);


	rtw_clr_drv_stopped(padapter);
	//padapter->bup = _TRUE;

	status = rtw_hal_init(padapter);
	if (status ==_FAIL)
	{
		RT_TRACE(_module_os_intfs_c_,_drv_err_,("ips_netdrv_open(): Can't init h/w!\n"));
		goto netdev_open_error;
	}

	if (padapter->intf_start)
	{
		padapter->intf_start(padapter);
	}

#ifndef CONFIG_IPS_CHECK_IN_WD
	rtw_set_pwr_state_check_timer(adapter_to_pwrctl(padapter));
#endif		
  	_set_timer(&padapter->mlmepriv.dynamic_chk_timer,2000);

	 return _SUCCESS;

netdev_open_error:
	//padapter->bup = _FALSE;
	DBG_871X("-ips_netdrv_open - drv_open failure, bup=%d\n", padapter->bup);

	return _FAIL;
}


int rtw_ips_pwr_up(_adapter *padapter)
{
	int result;
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
#ifdef DBG_CONFIG_ERROR_DETECT
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;
#endif//#ifdef DBG_CONFIG_ERROR_DETECT
	u32 start_time = rtw_get_current_time();
	DBG_871X("===>  rtw_ips_pwr_up..............\n");

#if defined(CONFIG_SWLPS_IN_IPS) || defined(CONFIG_FWLPS_IN_IPS)
#ifdef DBG_CONFIG_ERROR_DETECT
	if (psrtpriv->silent_reset_inprogress == _TRUE)
#endif//#ifdef DBG_CONFIG_ERROR_DETECT		
#endif //defined(CONFIG_SWLPS_IN_IPS) || defined(CONFIG_FWLPS_IN_IPS)
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

	padapter->net_closed = _TRUE;

	rtw_ips_dev_unload(padapter);
	DBG_871X("<=== rtw_ips_pwr_down..................... in %dms\n", rtw_get_passing_time_ms(start_time));
}
#endif
void rtw_ips_dev_unload(_adapter *padapter)
{
	struct net_device *pnetdev= (struct net_device*)padapter->pnetdev;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
#ifdef DBG_CONFIG_ERROR_DETECT	
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;
#endif//#ifdef DBG_CONFIG_ERROR_DETECT
	DBG_871X("====> %s...\n",__FUNCTION__);


#if defined(CONFIG_SWLPS_IN_IPS) || defined(CONFIG_FWLPS_IN_IPS)
#ifdef DBG_CONFIG_ERROR_DETECT
	if (psrtpriv->silent_reset_inprogress == _TRUE)
#endif //#ifdef DBG_CONFIG_ERROR_DETECT		
#endif //defined(CONFIG_SWLPS_IN_IPS) || defined(CONFIG_FWLPS_IN_IPS)
	{
		rtw_hal_set_hwreg(padapter, HW_VAR_FIFO_CLEARN_UP, 0);

		if (padapter->intf_stop)
		{
			padapter->intf_stop(padapter);
		}
	}

	if (!rtw_is_surprise_removed(padapter))
		rtw_hal_deinit(padapter);

}


int pm_netdev_open(struct net_device *pnetdev,u8 bnormal)
{
	int status = 0;

	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);

	if (_TRUE == bnormal)
	{
		_enter_critical_mutex(&(adapter_to_dvobj(padapter)->hw_init_mutex), NULL);
		status = _netdev_open(pnetdev);
		_exit_critical_mutex(&(adapter_to_dvobj(padapter)->hw_init_mutex), NULL);
	}	
#ifdef CONFIG_IPS
	else
		status =  (_SUCCESS == ips_netdrv_open(padapter))?(0):(-1);
#endif

	return status;
}

static int netdev_close(struct net_device *pnetdev)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
#endif //CONFIG_BT_COEXIST_SOCKET_TRX

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("+871x_drv - drv_close\n"));

#ifndef CONFIG_PLATFORM_INTEL_BYT
	if(pwrctl->bInternalAutoSuspend == _TRUE)
	{
		//rtw_pwr_wakeup(padapter);
		if(pwrctl->rf_pwrstate == rf_off)
			pwrctl->ps_flag = _TRUE;
	}
	padapter->net_closed = _TRUE;
	padapter->netif_up = _FALSE;
	pmlmepriv->LinkDetectInfo.bBusyTraffic = _FALSE;

/*	if (!rtw_is_hw_init_completed(padapter)) {
		DBG_871X("(1)871x_drv - drv_close, bup=%d, hw_init_completed=%s\n", padapter->bup, rtw_is_hw_init_completed(padapter)?"_TRUE":"_FALSE");

		rtw_set_drv_stopped(padapter);

		rtw_dev_unload(padapter);
	}
	else*/
	if(pwrctl->rf_pwrstate == rf_on){
		DBG_871X("(2)871x_drv - drv_close, bup=%d, hw_init_completed=%s\n", padapter->bup, rtw_is_hw_init_completed(padapter)?"_TRUE":"_FALSE");

		//s1.
		if(pnetdev)
		{
			rtw_netif_stop_queue(pnetdev);
		}

#ifndef CONFIG_ANDROID
		//s2.
		LeaveAllPowerSaveMode(padapter);
		rtw_disassoc_cmd(padapter, 500, _FALSE);
		//s2-2.  indicate disconnect to os
		rtw_indicate_disconnect(padapter, 0, _FALSE);
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
	if (!rtw_p2p_chk_role(&padapter->wdinfo, P2P_ROLE_DISABLE))
		rtw_p2p_enable(padapter, P2P_ROLE_DISABLE);
#endif //CONFIG_P2P

#ifdef CONFIG_IOCTL_CFG80211
	rtw_scan_abort(padapter);
	rtw_cfg80211_wait_scan_req_empty(padapter, 200);
	adapter_wdev_data(padapter)->bandroid_scan = _FALSE;
	//padapter->rtw_wdev->iftype = NL80211_IFTYPE_MONITOR; //set this at the end
#endif //CONFIG_IOCTL_CFG80211

#ifdef CONFIG_WAPI_SUPPORT
	rtw_wapi_disable_tx(padapter);
#endif
#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
	if(is_primary_adapter(padapter) &&  _TRUE == pHalData->EEPROMBluetoothCoexist)
		rtw_btcoex_close_socket(padapter);
	else
		DBG_871X("CONFIG_BT_COEXIST: SECONDARY_ADAPTER\n");
#endif //CONFIG_BT_COEXIST_SOCKET_TRX
#else //!CONFIG_PLATFORM_INTEL_BYT

	if (pwrctl->bInSuspend == _TRUE)
	{
		DBG_871X("+871x_drv - drv_close, bInSuspend=%d\n", pwrctl->bInSuspend);
		return 0;
	}

	rtw_scan_abort(padapter); // stop scanning process before wifi is going to down
	#ifdef CONFIG_IOCTL_CFG80211
	rtw_cfg80211_wait_scan_req_empty(padapter, 200);
	#endif

	DBG_871X("netdev_close, bips_processing=%d\n", pwrctl->bips_processing);
	while (pwrctl->bips_processing == _TRUE) // waiting for ips_processing done before call rtw_dev_unload()
		rtw_msleep_os(1);	

	rtw_dev_unload(padapter);
	rtw_sdio_set_power(0);

#endif //!CONFIG_PLATFORM_INTEL_BYT

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("-871x_drv - drv_close\n"));
	DBG_871X("-871x_drv - drv_close, bup=%d\n", padapter->bup);

	return 0;

}

int pm_netdev_close(struct net_device *pnetdev,u8 bnormal)
{
	int status = 0;

	status = netdev_close(pnetdev);

	return status;
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

	oldfs = get_fs(); set_fs(KERNEL_DS);
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

#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
restart:
#endif

	for (;;) 
	{
		struct nlmsghdr *h;

		iov.iov_base = pg;
		iov.iov_len = PAGE_SIZE;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
		iov_iter_init(&msg.msg_iter, READ, &iov, 1, PAGE_SIZE);
#endif

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
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
		iov_iter_init(&msg.msg_iter, WRITE, &iov, 1, sizeof(req));
#else
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
#endif
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_flags=MSG_DONTWAIT;

		oldfs = get_fs(); set_fs(KERNEL_DS);
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
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
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
		DBG_871X("Get Gateway IP/MAC fail!\n");
	}

	return res;
}
#endif

void rtw_dev_unload(PADAPTER padapter)
{
	struct net_device *pnetdev = (struct net_device*)padapter->pnetdev;	
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	struct dvobj_priv *pobjpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &pobjpriv->drv_dbg;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	u8 cnt = 0;

	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("+%s\n",__FUNCTION__));

	if (padapter->bup == _TRUE)
	{
		DBG_871X("===> %s\n",__FUNCTION__);

		rtw_set_drv_stopped(padapter);
		#ifdef CONFIG_XMIT_ACK
		if (padapter->xmitpriv.ack_tx)
			rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_DRV_STOP);
		#endif

		if (padapter->intf_stop)
			padapter->intf_stop(padapter);
		
		RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("@ rtw_dev_unload: stop intf complete!\n"));

		if (!pwrctl->bInternalAutoSuspend)
			rtw_stop_drv_threads(padapter);

		while(ATOMIC_READ(&(pcmdpriv->cmdthd_running)) == _TRUE){
			if (cnt > 5) {
				DBG_871X("stop cmdthd timeout\n");
				break;
			} else {
				cnt ++;
				DBG_871X("cmdthd is running(%d)\n", cnt);
				rtw_msleep_os(10);
			}
		}

		RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("@ %s: stop thread complete!\n",__FUNCTION__));

		//check the status of IPS
		if(rtw_hal_check_ips_status(padapter) == _TRUE || pwrctl->rf_pwrstate == rf_off) { //check HW status and SW state
			DBG_871X_LEVEL(_drv_always_, "%s: driver in IPS-FWLPS\n", __func__);
			pdbgpriv->dbg_dev_unload_inIPS_cnt++;
		} else {
			DBG_871X_LEVEL(_drv_always_, "%s: driver not in IPS\n", __func__);
		}

		if (!rtw_is_surprise_removed(padapter)) {
#ifdef CONFIG_BT_COEXIST
			rtw_btcoex_IpsNotify(padapter, pwrctl->ips_mode_req);
#endif
#ifdef CONFIG_WOWLAN
			if (pwrctl->bSupportRemoteWakeup == _TRUE && 
				pwrctl->wowlan_mode ==_TRUE) {
				DBG_871X_LEVEL(_drv_always_, "%s bSupportRemoteWakeup==_TRUE  do not run rtw_hal_deinit()\n",__FUNCTION__);
			}
			else
#endif
			{
				//amy modify 20120221 for power seq is different between driver open and ips
				rtw_hal_deinit(padapter);
			}
			rtw_set_surprise_removed(padapter);
		}
		RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("@ %s: deinit hal complelt!\n",__FUNCTION__));

		padapter->bup = _FALSE;

		DBG_871X("<=== %s\n",__FUNCTION__);
	}
	else {
		RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("%s: bup==_FALSE\n",__FUNCTION__));
		DBG_871X("%s: bup==_FALSE\n",__FUNCTION__);
	}

	/* cancel timer after thread stop */
	rtw_cancel_all_timer(padapter);
	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("-%s\n",__FUNCTION__));
}

int rtw_suspend_free_assoc_resource(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct net_device *pnetdev = padapter->pnetdev;
#ifdef CONFIG_P2P
	struct wifidirect_info*	pwdinfo = &padapter->wdinfo;
#endif // CONFIG_P2P

	DBG_871X("==> "FUNC_ADPT_FMT" entry....\n", FUNC_ADPT_ARG(padapter));

	if (rtw_chk_roam_flags(padapter, RTW_ROAM_ON_RESUME)) {
		if(check_fwstate(pmlmepriv, WIFI_STATION_STATE)
			&& check_fwstate(pmlmepriv, _FW_LINKED)
#ifdef CONFIG_P2P
			&& rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)
#endif // CONFIG_P2P
			)
		{
			DBG_871X("%s %s(" MAC_FMT "), length:%d assoc_ssid.length:%d\n",__FUNCTION__,
					pmlmepriv->cur_network.network.Ssid.Ssid,
					MAC_ARG(pmlmepriv->cur_network.network.MacAddress),
					pmlmepriv->cur_network.network.Ssid.SsidLength,
					pmlmepriv->assoc_ssid.SsidLength);
			rtw_set_to_roam(padapter, 1);
		}
	}

	if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) && check_fwstate(pmlmepriv, _FW_LINKED))
	{	
		rtw_disassoc_cmd(padapter, 0, _FALSE);	
		//s2-2.  indicate disconnect to os
		rtw_indicate_disconnect(padapter, 0, _FALSE);
	}
	#ifdef CONFIG_AP_MODE
	else if(check_fwstate(pmlmepriv, WIFI_AP_STATE))	
	{
		rtw_sta_flush(padapter, _TRUE);
	}
	#endif
		
	//s2-3.
	rtw_free_assoc_resources(padapter, 1);

	//s2-4.
#ifdef CONFIG_AUTOSUSPEND
	if(is_primary_adapter(padapter) && (!adapter_to_pwrctl(padapter)->bInternalAutoSuspend ))
#endif
		rtw_free_network_queue(padapter, _TRUE);

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY)) {
		DBG_871X_LEVEL(_drv_always_, "%s: fw_under_survey\n", __func__);
		rtw_indicate_scan_done(padapter, 1);
		clr_fwstate(pmlmepriv, _FW_UNDER_SURVEY);
	}

	if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == _TRUE)
	{
		DBG_871X_LEVEL(_drv_always_, "%s: fw_under_linking\n", __FUNCTION__);
		rtw_indicate_disconnect(padapter, 0, _FALSE);
	}
	
	DBG_871X("<== "FUNC_ADPT_FMT" exit....\n", FUNC_ADPT_ARG(padapter));
	return _SUCCESS;
}

#ifdef CONFIG_WOWLAN
int rtw_suspend_wow(_adapter *padapter)
{
	u8 ch, bw, offset;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct net_device *pnetdev = padapter->pnetdev;
	#ifdef CONFIG_CONCURRENT_MODE
	struct net_device *pbuddy_netdev = padapter->pbuddy_adapter->pnetdev;	
	#endif	
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;	
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct wowlan_ioctl_param poidparam;
	u8 ps_mode;
	int ret = _SUCCESS;

	DBG_871X("==> "FUNC_ADPT_FMT" entry....\n", FUNC_ADPT_ARG(padapter));


	DBG_871X("wowlan_mode: %d\n", pwrpriv->wowlan_mode);
	DBG_871X("wowlan_pno_enable: %d\n", pwrpriv->wowlan_pno_enable);
#ifdef CONFIG_P2P_WOWLAN
	DBG_871X("wowlan_p2p_enable: %d\n", pwrpriv->wowlan_p2p_enable);
#endif
	
	if (pwrpriv->wowlan_mode == _TRUE) {
		
		if(pnetdev)
			rtw_netif_stop_queue(pnetdev);	
		#ifdef CONFIG_CONCURRENT_MODE
		if(pbuddy_netdev){
			netif_carrier_off(pbuddy_netdev);
			rtw_netif_stop_queue(pbuddy_netdev);
		}
		#endif//CONFIG_CONCURRENT_MODE
		// 0. Power off LED
		rtw_led_control(padapter, LED_CTL_POWER_OFF);
		// 1. stop thread
		rtw_set_drv_stopped(padapter);	/*for stop thread*/
		rtw_stop_drv_threads(padapter);
		#ifdef CONFIG_CONCURRENT_MODE	
		if (rtw_buddy_adapter_up(padapter))
			rtw_stop_drv_threads(padapter->pbuddy_adapter);
		#endif /*CONFIG_CONCURRENT_MODE*/
		rtw_clr_drv_stopped(padapter);	/*for 32k command*/

		//#ifdef CONFIG_LPS
		//rtw_set_ps_mode(padapter, PS_MODE_ACTIVE, 0, 0, "WOWLAN");
		//#endif

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		// 2. disable interrupt
		if (padapter->intf_stop) {
			padapter->intf_stop(padapter);
		}


		#ifdef CONFIG_CONCURRENT_MODE
		if (rtw_buddy_adapter_up(padapter)) { //free buddy adapter's resource
			padapter->pbuddy_adapter->intf_stop(padapter->pbuddy_adapter);
		}
		#endif

		// 2.1 clean interupt
		rtw_hal_clear_interrupt(padapter);
#endif //CONFIG_SDIO_HCI

		// 2.2 free irq
		//sdio_free_irq(adapter_to_dvobj(padapter));
		if(padapter->intf_free_irq)
			padapter->intf_free_irq(adapter_to_dvobj(padapter));

		#ifdef CONFIG_RUNTIME_PORT_SWITCH
		if (rtw_port_switch_chk(padapter)) {
			DBG_871X(" ### PORT SWITCH ### \n");
			rtw_hal_set_hwreg(padapter, HW_VAR_PORT_SWITCH, NULL);
		}
		#endif

		poidparam.subcode = WOWLAN_ENABLE;
		rtw_hal_set_hwreg(padapter,HW_VAR_WOWLAN,(u8 *)&poidparam);
		if (rtw_chk_roam_flags(padapter, RTW_ROAM_ON_RESUME)) {
			if(check_fwstate(pmlmepriv, WIFI_STATION_STATE)
				&& check_fwstate(pmlmepriv, _FW_LINKED))
			{
				DBG_871X("%s %s(" MAC_FMT "), length:%d assoc_ssid.length:%d\n",__FUNCTION__,
						pmlmepriv->cur_network.network.Ssid.Ssid,
						MAC_ARG(pmlmepriv->cur_network.network.MacAddress),
						pmlmepriv->cur_network.network.Ssid.SsidLength,
						pmlmepriv->assoc_ssid.SsidLength);

				rtw_set_to_roam(padapter, 0);
			}
		}

		DBG_871X_LEVEL(_drv_always_, "%s: wowmode suspending\n", __func__);

		if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == _TRUE)
		{
			DBG_871X_LEVEL(_drv_always_, "%s: fw_under_survey\n", __func__);
			rtw_indicate_scan_done(padapter, 1);
			clr_fwstate(pmlmepriv, _FW_UNDER_SURVEY);
		}
		
		if (rtw_get_ch_setting_union(padapter, &ch, &bw, &offset) != 0) {
			DBG_871X(FUNC_ADPT_FMT" back to linked/linking union - ch:%u, bw:%u, offset:%u\n",
				FUNC_ADPT_ARG(padapter), ch, bw, offset);
			set_channel_bwmode(padapter, ch, offset, bw);
		}
		#ifdef CONFIG_CONCURRENT_MODE
		if(rtw_buddy_adapter_up(padapter)){ //free buddy adapter's resource
			rtw_suspend_free_assoc_resource(padapter->pbuddy_adapter);
		}
		#endif	

		if(pwrpriv->wowlan_pno_enable) {
			DBG_871X_LEVEL(_drv_always_, "%s: pno: %d\n", __func__,
					pwrpriv->wowlan_pno_enable);
#ifdef CONFIG_FWLPS_IN_IPS
			rtw_set_fw_in_ips_mode(padapter, _TRUE);
#endif
		}
		#ifdef CONFIG_LPS
		else
			rtw_set_ps_mode(padapter, PS_MODE_MAX, 0, 0, "WOWLAN");
		#endif //#ifdef CONFIG_LPS

	}
	else
	{
		DBG_871X_LEVEL(_drv_always_, "%s: ### ERROR ### wowlan_mode=%d\n", __FUNCTION__, pwrpriv->wowlan_mode);	
	}
	DBG_871X("<== "FUNC_ADPT_FMT" exit....\n", FUNC_ADPT_ARG(padapter));
	return ret;
}
#endif //#ifdef CONFIG_WOWLAN

#ifdef CONFIG_AP_WOWLAN
int rtw_suspend_ap_wow(_adapter *padapter)
{
	u8 ch, bw, offset;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct net_device *pnetdev = padapter->pnetdev;
	#ifdef CONFIG_CONCURRENT_MODE
	struct net_device *pbuddy_netdev;
	#endif
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct wowlan_ioctl_param poidparam;
	u8 ps_mode;
	int ret = _SUCCESS;

	DBG_871X("==> "FUNC_ADPT_FMT" entry....\n", FUNC_ADPT_ARG(padapter));
	
	pwrpriv->wowlan_ap_mode = _TRUE;
	
	DBG_871X("wowlan_ap_mode: %d\n", pwrpriv->wowlan_ap_mode);
	
	if(pnetdev)
		rtw_netif_stop_queue(pnetdev);
	#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_buddy_adapter_up(padapter)) {
		pbuddy_netdev = padapter->pbuddy_adapter->pnetdev;
		if (pbuddy_netdev)
		rtw_netif_stop_queue(pbuddy_netdev);
	}
	#endif//CONFIG_CONCURRENT_MODE
	// 0. Power off LED
	rtw_led_control(padapter, LED_CTL_POWER_OFF);
	// 1. stop thread
	rtw_set_drv_stopped(padapter);	/*for stop thread*/
	rtw_stop_drv_threads(padapter);
	#ifdef CONFIG_CONCURRENT_MODE	
	if (rtw_buddy_adapter_up(padapter))
		rtw_stop_drv_threads(padapter->pbuddy_adapter);
	#endif /* CONFIG_CONCURRENT_MODE */
	rtw_clr_drv_stopped(padapter);	/*for 32k command*/

#ifdef CONFIG_SDIO_HCI
	// 2. disable interrupt
	rtw_hal_disable_interrupt(padapter); // It need wait for leaving 32K.

	#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_buddy_adapter_up(padapter)) { //free buddy adapter's resource
		padapter->pbuddy_adapter->intf_stop(padapter->pbuddy_adapter);
	}
	#endif

	// 2.1 clean interupt
	rtw_hal_clear_interrupt(padapter);
#endif //CONFIG_SDIO_HCI

	// 2.2 free irq
	if(padapter->intf_free_irq)
		padapter->intf_free_irq(adapter_to_dvobj(padapter));

	#ifdef CONFIG_RUNTIME_PORT_SWITCH
	if (rtw_port_switch_chk(padapter)) {
		DBG_871X(" ### PORT SWITCH ### \n");
		rtw_hal_set_hwreg(padapter, HW_VAR_PORT_SWITCH, NULL);
	}
	#endif

	poidparam.subcode = WOWLAN_AP_ENABLE;
	rtw_hal_set_hwreg(padapter, HW_VAR_WOWLAN, (u8 *)&poidparam);

	DBG_871X_LEVEL(_drv_always_, "%s: wowmode suspending\n", __func__);

#ifdef CONFIG_CONCURRENT_MODE
	if (check_buddy_fwstate(padapter, WIFI_AP_STATE) == _TRUE) {
		if (rtw_get_ch_setting_union(padapter->pbuddy_adapter, &ch, &bw, &offset) != 0) {
			DBG_871X(FUNC_ADPT_FMT" back to linked/linking union - ch:%u, bw:%u, offset:%u\n",
				FUNC_ADPT_ARG(padapter->pbuddy_adapter), ch, bw, offset);
			set_channel_bwmode(padapter->pbuddy_adapter, ch, offset, bw);
		}
		rtw_suspend_free_assoc_resource(padapter);
	} else {
		if (rtw_get_ch_setting_union(padapter, &ch, &bw, &offset) != 0) {
			DBG_871X(FUNC_ADPT_FMT" back to linked/linking union - ch:%u, bw:%u, offset:%u\n",
				FUNC_ADPT_ARG(padapter), ch, bw, offset);
			set_channel_bwmode(padapter, ch, offset, bw);
		}
		rtw_suspend_free_assoc_resource(padapter->pbuddy_adapter);
	}
#else
	if (rtw_get_ch_setting_union(padapter, &ch, &bw, &offset) != 0) {
		DBG_871X(FUNC_ADPT_FMT" back to linked/linking union - ch:%u, bw:%u, offset:%u\n",
			FUNC_ADPT_ARG(padapter), ch, bw, offset);
			set_channel_bwmode(padapter, ch, offset, bw);
	}
#endif

#ifdef CONFIG_LPS
	rtw_set_ps_mode(padapter, PS_MODE_MIN, 0, 0, "AP-WOWLAN");
#endif

	DBG_871X("<== "FUNC_ADPT_FMT" exit....\n", FUNC_ADPT_ARG(padapter));
	return ret;
}
#endif //#ifdef CONFIG_AP_WOWLAN


int rtw_suspend_normal(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct net_device *pnetdev = padapter->pnetdev;
	#ifdef CONFIG_CONCURRENT_MODE
	struct net_device *pbuddy_netdev = padapter->pbuddy_adapter->pnetdev;	
	#endif	
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	int ret = _SUCCESS;	

	DBG_871X("==> "FUNC_ADPT_FMT" entry....\n", FUNC_ADPT_ARG(padapter));			
	if(pnetdev){
		netif_carrier_off(pnetdev);
		rtw_netif_stop_queue(pnetdev);
	}		
#ifdef CONFIG_CONCURRENT_MODE
	if(rtw_buddy_adapter_up(padapter)){
		pbuddy_netdev = padapter->pbuddy_adapter->pnetdev;
		netif_carrier_off(pbuddy_netdev);
		rtw_netif_stop_queue(pbuddy_netdev);
	}
#endif	

	rtw_suspend_free_assoc_resource(padapter);

#ifdef CONFIG_CONCURRENT_MODE
	if(rtw_buddy_adapter_up(padapter)){
		rtw_suspend_free_assoc_resource(padapter->pbuddy_adapter);
	}
#endif
	rtw_led_control(padapter, LED_CTL_POWER_OFF);

	if ((rtw_hal_check_ips_status(padapter) == _TRUE)
		|| (adapter_to_pwrctl(padapter)->rf_pwrstate == rf_off))
	{
		DBG_871X_LEVEL(_drv_always_, "%s: ### ERROR #### driver in IPS ####ERROR###!!!\n", __FUNCTION__);	
		
	}
	
#ifdef CONFIG_CONCURRENT_MODE
	if(rtw_buddy_adapter_up(padapter)){
		rtw_dev_unload(padapter->pbuddy_adapter);
	}
#endif
	rtw_dev_unload(padapter);

	//sdio_deinit(adapter_to_dvobj(padapter));
	if(padapter->intf_deinit)
		padapter->intf_deinit(adapter_to_dvobj(padapter));

	DBG_871X("<== "FUNC_ADPT_FMT" exit....\n", FUNC_ADPT_ARG(padapter));
	return ret;
}

int rtw_suspend_common(_adapter *padapter)
{
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(psdpriv);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	
	int ret = 0;
	u32 start_time = rtw_get_current_time();

	DBG_871X_LEVEL(_drv_always_, " suspend start\n");
	DBG_871X("==> %s (%s:%d)\n",__FUNCTION__, current->comm, current->pid);
	
	pdbgpriv->dbg_suspend_cnt++;
	
	pwrpriv->bInSuspend = _TRUE;
	
	while (pwrpriv->bips_processing == _TRUE)
		rtw_msleep_os(1);		

#ifdef CONFIG_IOL_READ_EFUSE_MAP
	if(!padapter->bup){
		u8 bMacPwrCtrlOn = _FALSE;
		rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
		if(bMacPwrCtrlOn)
			rtw_hal_power_off(padapter);
	}
#endif

	if ((!padapter->bup) || RTW_CANNOT_RUN(padapter)) {
		DBG_871X("%s bup=%d bDriverStopped=%s bSurpriseRemoved = %s\n", __func__
			, padapter->bup
			, rtw_is_drv_stopped(padapter)?"True":"False"
			, rtw_is_surprise_removed(padapter)?"True":"False");
		pdbgpriv->dbg_suspend_error_cnt++;
		goto exit;
	}
	rtw_ps_deny(padapter, PS_DENY_SUSPEND);

	rtw_cancel_all_timer(padapter);
#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->pbuddy_adapter){
		rtw_cancel_all_timer(padapter->pbuddy_adapter);
	}
#endif // CONFIG_CONCURRENT_MODE

	LeaveAllPowerSaveModeDirect(padapter);

	rtw_stop_cmd_thread(padapter);
	
#ifdef CONFIG_BT_COEXIST
	// wait for the latest FW to remove this condition.
	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE) {
		rtw_btcoex_SuspendNotify(padapter, 0);
		DBG_871X("WIFI_AP_STATE\n");
#ifdef CONFIG_CONCURRENT_MODE
	} else if (check_buddy_fwstate(padapter, WIFI_AP_STATE)) {
		rtw_btcoex_SuspendNotify(padapter, 0);
		DBG_871X("P2P_ROLE_GO\n");
#endif //CONFIG_CONCURRENT_MODE
	} else if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE) {
		rtw_btcoex_SuspendNotify(padapter, 1);
		DBG_871X("STATION\n");
	}
#endif // CONFIG_BT_COEXIST

	rtw_ps_deny_cancel(padapter, PS_DENY_SUSPEND);

	if (check_fwstate(pmlmepriv,WIFI_STATION_STATE) == _TRUE
#ifdef CONFIG_CONCURRENT_MODE
		&& check_buddy_fwstate(padapter, WIFI_AP_STATE) == _FALSE
#endif
	) {
	#ifdef CONFIG_WOWLAN
		if (check_fwstate(pmlmepriv, _FW_LINKED)) {
			pwrpriv->wowlan_mode = _TRUE;
		} else if (pwrpriv->wowlan_pno_enable == _TRUE) {
			pwrpriv->wowlan_mode |= pwrpriv->wowlan_pno_enable;
		}

	#ifdef CONFIG_P2P_WOWLAN
		if(!rtw_p2p_chk_state(&padapter->wdinfo, P2P_STATE_NONE) || P2P_ROLE_DISABLE != padapter->wdinfo.role)
		{
			pwrpriv->wowlan_p2p_mode = _TRUE;
		}
		if(_TRUE == pwrpriv->wowlan_p2p_mode)
			pwrpriv->wowlan_mode |= pwrpriv->wowlan_p2p_mode;
	#endif //CONFIG_P2P_WOWLAN

		if (pwrpriv->wowlan_mode == _TRUE)
			rtw_suspend_wow(padapter);
		else
			rtw_suspend_normal(padapter);
		
	#else //CONFIG_WOWLAN
		rtw_suspend_normal(padapter);
	#endif //CONFIG_WOWLAN
	} else if (check_fwstate(pmlmepriv,WIFI_AP_STATE) == _TRUE
#ifdef CONFIG_CONCURRENT_MODE
		&& check_buddy_fwstate(padapter, WIFI_AP_STATE) == _FALSE
#endif
	) {
	#ifdef CONFIG_AP_WOWLAN
		rtw_suspend_ap_wow(padapter);
	#else
		rtw_suspend_normal(padapter);
	#endif //CONFIG_AP_WOWLAN
#ifdef CONFIG_CONCURRENT_MODE
	} else if (check_fwstate(pmlmepriv,WIFI_STATION_STATE) == _TRUE
		&& check_buddy_fwstate(padapter, WIFI_AP_STATE) == _TRUE) {
	#ifdef CONFIG_AP_WOWLAN
		rtw_suspend_ap_wow(padapter);
	#else
		rtw_suspend_normal(padapter);
	#endif //CONFIG_AP_WOWLAN
#endif
	} else {
		rtw_suspend_normal(padapter);
	}


	DBG_871X_LEVEL(_drv_always_, "rtw suspend success in %d ms\n",
		rtw_get_passing_time_ms(start_time));

exit:
	DBG_871X("<===  %s return %d.............. in %dms\n", __FUNCTION__
		, ret, rtw_get_passing_time_ms(start_time));

	return ret;	
}

#ifdef CONFIG_WOWLAN
int rtw_resume_process_wow(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct net_device *pnetdev = padapter->pnetdev;
	#ifdef CONFIG_CONCURRENT_MODE
	struct net_device *pbuddy_netdev;	
	#endif	
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	struct wowlan_ioctl_param poidparam;
	struct sta_info	*psta = NULL;
	int ret = _SUCCESS;
_func_enter_;

	DBG_871X("==> "FUNC_ADPT_FMT" entry....\n", FUNC_ADPT_ARG(padapter));
	
	if (padapter) {
		pnetdev = padapter->pnetdev;
		pwrpriv = adapter_to_pwrctl(padapter);
	} else {
		pdbgpriv->dbg_resume_error_cnt++;
		ret = -1;
		goto exit;
	}

	if (RTW_CANNOT_RUN(padapter)) {
		DBG_871X("%s pdapter %p bDriverStopped %s bSurpriseRemoved %s\n"
				, __func__, padapter
				, rtw_is_drv_stopped(padapter)?"True":"False"
				, rtw_is_surprise_removed(padapter)?"True":"False");
		goto exit;
	}

#ifdef CONFIG_PNO_SUPPORT
	pwrpriv->pno_in_resume = _TRUE;
#ifdef CONFIG_FWLPS_IN_IPS
	if(pwrpriv->wowlan_pno_enable)
		rtw_set_fw_in_ips_mode(padapter, _FALSE);
#endif //CONFIG_FWLPS_IN_IPS
#endif//CONFIG_PNO_SUPPORT

	if (pwrpriv->wowlan_mode == _TRUE){
#ifdef CONFIG_LPS
		rtw_set_ps_mode(padapter, PS_MODE_ACTIVE, 0, 0, "WOWLAN");
#endif //CONFIG_LPS

		pwrpriv->bFwCurrentInPSMode = _FALSE;

#ifdef CONFIG_SDIO_HCI
		if (padapter->intf_stop) {
			padapter->intf_stop(padapter);
		}

		#ifdef CONFIG_CONCURRENT_MODE
		if (rtw_buddy_adapter_up(padapter)) { //free buddy adapter's resource
			padapter->pbuddy_adapter->intf_stop(padapter->pbuddy_adapter);
		}
		#endif

		rtw_hal_clear_interrupt(padapter);
#endif //CONFIG_SDIO_HCI

		//if (sdio_alloc_irq(adapter_to_dvobj(padapter)) != _SUCCESS) {		
		if((padapter->intf_alloc_irq) && (padapter->intf_alloc_irq(adapter_to_dvobj(padapter)) != _SUCCESS)){
			ret = -1;
			RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("%s: sdio_alloc_irq Failed!!\n", __FUNCTION__));
			goto exit;
		}

		//Disable WOW, set H2C command
		poidparam.subcode=WOWLAN_DISABLE;
		rtw_hal_set_hwreg(padapter,HW_VAR_WOWLAN,(u8 *)&poidparam);

		#ifdef CONFIG_CONCURRENT_MODE
		rtw_reset_drv_sw(padapter->pbuddy_adapter);
		#endif		

		psta = rtw_get_stainfo(&padapter->stapriv, get_bssid(&padapter->mlmepriv));
		if (psta) {
			set_sta_rate(padapter, psta);
		}

	
		rtw_clr_drv_stopped(padapter);
		DBG_871X("%s: wowmode resuming, DriverStopped:%s\n", __func__, rtw_is_drv_stopped(padapter)?"True":"False");
		rtw_start_drv_threads(padapter);

#ifdef CONFIG_CONCURRENT_MODE
		if (padapter->pbuddy_adapter)
			rtw_start_drv_threads(padapter->pbuddy_adapter);
#endif /* CONFIG_CONCURRENT_MODE*/

		if (padapter->intf_start) {
			padapter->intf_start(padapter);
		}
		#ifdef CONFIG_CONCURRENT_MODE
		if (rtw_buddy_adapter_up(padapter)) { //free buddy adapter's resource
			padapter->pbuddy_adapter->intf_start(padapter->pbuddy_adapter);
		}

		if (rtw_buddy_adapter_up(padapter)) {
			pbuddy_netdev = padapter->pbuddy_adapter->pnetdev;

			if(pbuddy_netdev){
				netif_device_attach(pbuddy_netdev);
				netif_carrier_on(pbuddy_netdev);
			}
		}
		#endif

		// start netif queue
		if (pnetdev) {
			rtw_netif_wake_queue(pnetdev);
		}
	}
	else{

		DBG_871X_LEVEL(_drv_always_, "%s: ### ERROR ### wowlan_mode=%d\n", __FUNCTION__, pwrpriv->wowlan_mode);		
	} 

	if( padapter->pid[1]!=0) {
		DBG_871X("pid[1]:%d\n",padapter->pid[1]);
		rtw_signal_process(padapter->pid[1], SIGUSR2);
	}	

	if (rtw_chk_roam_flags(padapter, RTW_ROAM_ON_RESUME)) {
		if (pwrpriv->wowlan_wake_reason == FWDecisionDisconnect ||
			pwrpriv->wowlan_wake_reason == Rx_DisAssoc ||
			pwrpriv->wowlan_wake_reason == Rx_DeAuth) {

			DBG_871X("%s: disconnect reason: %02x\n", __func__,
						pwrpriv->wowlan_wake_reason);
			rtw_indicate_disconnect(padapter, 0, _FALSE);

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

	if (pwrpriv->wowlan_wake_reason == FWDecisionDisconnect) {
		rtw_lock_ext_suspend_timeout(2000);
	}

	if (pwrpriv->wowlan_wake_reason == Rx_GTK ||
		pwrpriv->wowlan_wake_reason == Rx_DisAssoc ||
		pwrpriv->wowlan_wake_reason == Rx_DeAuth) {
		rtw_lock_ext_suspend_timeout(8000);
	}

	if (pwrpriv->wowlan_wake_reason == RX_PNOWakeUp) {
#ifdef CONFIG_IOCTL_CFG80211
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0))
			u8 locally_generated = 1;

			cfg80211_disconnected(padapter->pnetdev, 0, NULL, 0, locally_generated, GFP_ATOMIC);
#else
			cfg80211_disconnected(padapter->pnetdev, 0, NULL, 0, GFP_ATOMIC);
#endif
#endif /* CONFIG_IOCTL_CFG80211 */
		rtw_lock_ext_suspend_timeout(10000);
	}

	if (pwrpriv->wowlan_mode == _TRUE) {
		pwrpriv->bips_processing = _FALSE;
		_set_timer(&padapter->mlmepriv.dynamic_chk_timer, 2000);
#ifndef CONFIG_IPS_CHECK_IN_WD
		rtw_set_pwr_state_check_timer(pwrpriv);
#endif
	} else {
		DBG_871X_LEVEL(_drv_always_, "do not reset timer\n");
	}

	pwrpriv->wowlan_mode =_FALSE;

	// Power On LED
#ifdef CONFIG_SW_LED
	if(pwrpriv->wowlan_wake_reason == Rx_DisAssoc ||
		pwrpriv->wowlan_wake_reason == Rx_DeAuth ||
		pwrpriv->wowlan_wake_reason == FWDecisionDisconnect)
		rtw_led_control(padapter, LED_CTL_NO_LINK);
	else
		rtw_led_control(padapter, LED_CTL_LINK);
#endif
	//clean driver side wake up reason.
	pwrpriv->wowlan_wake_reason = 0;

exit:
	DBG_871X("<== "FUNC_ADPT_FMT" exit....\n", FUNC_ADPT_ARG(padapter));
_func_exit_;
	return ret;
}
#endif //#ifdef CONFIG_WOWLAN

#ifdef CONFIG_AP_WOWLAN
int rtw_resume_process_ap_wow(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct net_device *pnetdev = padapter->pnetdev;
	#ifdef CONFIG_CONCURRENT_MODE
	struct net_device *pbuddy_netdev;	
	#endif	
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	struct wowlan_ioctl_param poidparam;
	struct sta_info	*psta = NULL;
	int ret = _SUCCESS;
	u8 ch, bw, offset;
_func_enter_;

	DBG_871X("==> "FUNC_ADPT_FMT" entry....\n", FUNC_ADPT_ARG(padapter));
	
	if (padapter) {
		pnetdev = padapter->pnetdev;
		pwrpriv = adapter_to_pwrctl(padapter);
	} else {
		pdbgpriv->dbg_resume_error_cnt++;
		ret = -1;
		goto exit;
	}


#ifdef CONFIG_LPS
	rtw_set_ps_mode(padapter, PS_MODE_ACTIVE, 0, 0, "AP-WOWLAN");
#endif //CONFIG_LPS

	pwrpriv->bFwCurrentInPSMode = _FALSE;

	rtw_hal_disable_interrupt(padapter);

	rtw_hal_clear_interrupt(padapter);
		
	//if (sdio_alloc_irq(adapter_to_dvobj(padapter)) != _SUCCESS) {		
	if((padapter->intf_alloc_irq) && (padapter->intf_alloc_irq(adapter_to_dvobj(padapter)) != _SUCCESS)){
		ret = -1;
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("%s: sdio_alloc_irq Failed!!\n", __FUNCTION__));
		goto exit;
	}

	//Disable WOW, set H2C command
	poidparam.subcode = WOWLAN_AP_DISABLE;
	rtw_hal_set_hwreg(padapter, HW_VAR_WOWLAN, (u8 *)&poidparam);
	pwrpriv->wowlan_ap_mode = _FALSE;

	rtw_clr_drv_stopped(padapter);
	DBG_871X("%s: wowmode resuming, DriverStopped:%s\n", __func__, rtw_is_drv_stopped(padapter)?"True":"False");
	rtw_start_drv_threads(padapter);

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_buddy_adapter_up(padapter))
		rtw_start_drv_threads(padapter->pbuddy_adapter);
#endif /* CONFIG_CONCURRENT_MODE */

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_buddy_adapter_up(padapter)) {
		if (rtw_get_ch_setting_union(padapter->pbuddy_adapter, &ch, &bw, &offset) != 0) {
			DBG_871X(FUNC_ADPT_FMT" back to linked/linking union - ch:%u, bw:%u, offset:%u\n",
			FUNC_ADPT_ARG(padapter->pbuddy_adapter), ch, bw, offset);
			set_channel_bwmode(padapter->pbuddy_adapter, ch, offset, bw);
		}
	} else {
		DBG_871X(FUNC_ADPT_FMT" back to linked/linking union - ch:%u, bw:%u, offset:%u\n",
			FUNC_ADPT_ARG(padapter), ch, bw, offset);
		set_channel_bwmode(padapter, ch, offset, bw);
		rtw_reset_drv_sw(padapter->pbuddy_adapter);
	}
#else
	if (rtw_get_ch_setting_union(padapter, &ch, &bw, &offset) != 0) {
		DBG_871X(FUNC_ADPT_FMT" back to linked/linking union - ch:%u, bw:%u, offset:%u\n",
			FUNC_ADPT_ARG(padapter), ch, bw, offset);
		set_channel_bwmode(padapter, ch, offset, bw);
	}
#endif

	if (padapter->intf_start) {
		padapter->intf_start(padapter);
	}

	#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_buddy_adapter_up(padapter)) { //free buddy adapter's resource
		padapter->pbuddy_adapter->intf_start(padapter->pbuddy_adapter);
	}
	#endif

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_buddy_adapter_up(padapter)) {			
		pbuddy_netdev = padapter->pbuddy_adapter->pnetdev;			
		if(pbuddy_netdev){
			rtw_netif_wake_queue(pbuddy_netdev);
		}
	}
#endif
	
	// start netif queue
	if (pnetdev) {
		rtw_netif_wake_queue(pnetdev);
	}

	if( padapter->pid[1]!=0) {
		DBG_871X("pid[1]:%d\n",padapter->pid[1]);
		rtw_signal_process(padapter->pid[1], SIGUSR2);
	}	

	#ifdef CONFIG_RESUME_IN_WORKQUEUE
	//rtw_unlock_suspend();
	#endif //CONFIG_RESUME_IN_WORKQUEUE

	if (pwrpriv->wowlan_wake_reason == AP_WakeUp)
		rtw_lock_ext_suspend_timeout(8000);

	pwrpriv->bips_processing = _FALSE;
	_set_timer(&padapter->mlmepriv.dynamic_chk_timer, 2000);
#ifndef CONFIG_IPS_CHECK_IN_WD
	rtw_set_pwr_state_check_timer(pwrpriv);
#endif
	//clean driver side wake up reason.
	pwrpriv->wowlan_wake_reason = 0;

	// Power On LED
#ifdef CONFIG_SW_LED
	rtw_led_control(padapter, LED_CTL_LINK);
#endif
exit:
	DBG_871X("<== "FUNC_ADPT_FMT" exit....\n", FUNC_ADPT_ARG(padapter));
_func_exit_;
	return ret;
}
#endif //#ifdef CONFIG_APWOWLAN

int rtw_resume_process_normal(_adapter *padapter)
{
	struct net_device *pnetdev;
	#ifdef CONFIG_CONCURRENT_MODE
	struct net_device *pbuddy_netdev;	
	#endif	
	struct pwrctrl_priv *pwrpriv;
	struct mlme_priv *pmlmepriv;
	struct dvobj_priv *psdpriv;
	struct debug_priv *pdbgpriv;	
	
	int ret = _SUCCESS;
_func_enter_;
	
	if (!padapter) {
		ret = -1;
		goto exit;
	}
	
	pnetdev = padapter->pnetdev;
	pwrpriv = adapter_to_pwrctl(padapter);
	pmlmepriv = &padapter->mlmepriv;	
	psdpriv = padapter->dvobj;
	pdbgpriv = &psdpriv->drv_dbg;
	
	DBG_871X("==> "FUNC_ADPT_FMT" entry....\n", FUNC_ADPT_ARG(padapter));
	// interface init
	//if (sdio_init(adapter_to_dvobj(padapter)) != _SUCCESS)
	if((padapter->intf_init)&& (padapter->intf_init(adapter_to_dvobj(padapter)) != _SUCCESS))
	{
		ret = -1;
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("%s: initialize SDIO Failed!!\n", __FUNCTION__));
		goto exit;
	}
	rtw_hal_disable_interrupt(padapter);
	//if (sdio_alloc_irq(adapter_to_dvobj(padapter)) != _SUCCESS)
	if ((padapter->intf_alloc_irq)&&(padapter->intf_alloc_irq(adapter_to_dvobj(padapter)) != _SUCCESS))
	{
		ret = -1;
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("%s: sdio_alloc_irq Failed!!\n", __FUNCTION__));
		goto exit;
	}

	rtw_reset_drv_sw(padapter);
	#ifdef CONFIG_CONCURRENT_MODE
	rtw_reset_drv_sw(padapter->pbuddy_adapter);
	#endif
	
	pwrpriv->bkeepfwalive = _FALSE;

	DBG_871X("bkeepfwalive(%x)\n",pwrpriv->bkeepfwalive);
	if(pm_netdev_open(pnetdev,_TRUE) != 0) {
		ret = -1;
		pdbgpriv->dbg_resume_error_cnt++;
		goto exit;
	}

	netif_device_attach(pnetdev);
	netif_carrier_on(pnetdev);

	#ifdef CONFIG_CONCURRENT_MODE
	if(rtw_buddy_adapter_up(padapter)){			
		pbuddy_netdev = padapter->pbuddy_adapter->pnetdev;				
		
		netif_device_attach(pbuddy_netdev);
		netif_carrier_on(pbuddy_netdev);	
	}
	#endif
	

	if( padapter->pid[1]!=0) {
		DBG_871X("pid[1]:%d\n",padapter->pid[1]);
		rtw_signal_process(padapter->pid[1], SIGUSR2);
	}	


	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		DBG_871X(FUNC_ADPT_FMT" fwstate:0x%08x - WIFI_STATION_STATE\n", FUNC_ADPT_ARG(padapter), get_fwstate(pmlmepriv));

		if (rtw_chk_roam_flags(padapter, RTW_ROAM_ON_RESUME))
			rtw_roaming(padapter, NULL);
		
	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		DBG_871X(FUNC_ADPT_FMT" fwstate:0x%08x - WIFI_AP_STATE\n", FUNC_ADPT_ARG(padapter), get_fwstate(pmlmepriv));
		rtw_ap_restore_network(padapter);
	} else if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {
		DBG_871X(FUNC_ADPT_FMT" fwstate:0x%08x - WIFI_ADHOC_STATE\n", FUNC_ADPT_ARG(padapter), get_fwstate(pmlmepriv));
	} else {
		DBG_871X(FUNC_ADPT_FMT" fwstate:0x%08x - ???\n", FUNC_ADPT_ARG(padapter), get_fwstate(pmlmepriv));
	}

	#ifdef CONFIG_CONCURRENT_MODE
	if(rtw_buddy_adapter_up(padapter))
	{	
		_adapter *buddy = padapter->pbuddy_adapter;
		struct mlme_priv *buddy_mlme = &padapter->pbuddy_adapter->mlmepriv;
		if (check_fwstate(buddy_mlme, WIFI_STATION_STATE)) {
			DBG_871X(FUNC_ADPT_FMT" fwstate:0x%08x - WIFI_STATION_STATE\n", FUNC_ADPT_ARG(buddy), get_fwstate(buddy_mlme));

			if (rtw_chk_roam_flags(buddy, RTW_ROAM_ON_RESUME))
				rtw_roaming(buddy, NULL);
		
		} else if (check_fwstate(buddy_mlme, WIFI_AP_STATE)) {
			DBG_871X(FUNC_ADPT_FMT" fwstate:0x%08x - WIFI_AP_STATE\n", FUNC_ADPT_ARG(buddy), get_fwstate(buddy_mlme));
			rtw_ap_restore_network(buddy);
		} else if (check_fwstate(buddy_mlme, WIFI_ADHOC_STATE)) {
			DBG_871X(FUNC_ADPT_FMT" fwstate:0x%08x - WIFI_ADHOC_STATE\n", FUNC_ADPT_ARG(buddy), get_fwstate(buddy_mlme));
		} else {
			DBG_871X(FUNC_ADPT_FMT" fwstate:0x%08x - ???\n", FUNC_ADPT_ARG(buddy), get_fwstate(buddy_mlme));
		}
	}
	#endif

#ifdef CONFIG_RESUME_IN_WORKQUEUE
	//rtw_unlock_suspend();
#endif //CONFIG_RESUME_IN_WORKQUEUE
	DBG_871X("<== "FUNC_ADPT_FMT" exit....\n", FUNC_ADPT_ARG(padapter));

exit:	
_func_exit_;
	return ret;	
}

int rtw_resume_common(_adapter *padapter)
{
	int ret = 0;
	u32 start_time = rtw_get_current_time();
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	
	_func_enter_;

	if (pwrpriv->bInSuspend == _FALSE)
		return 0;

	DBG_871X_LEVEL(_drv_always_, "resume start\n");
	DBG_871X("==> %s (%s:%d)\n",__FUNCTION__, current->comm, current->pid);	

	if (check_fwstate(pmlmepriv,WIFI_STATION_STATE) == _TRUE
#ifdef CONFIG_CONCURRENT_MODE
		&& check_buddy_fwstate(padapter, WIFI_AP_STATE) == _FALSE
#endif
	) {
	#ifdef CONFIG_WOWLAN
		if (pwrpriv->wowlan_mode == _TRUE)
			rtw_resume_process_wow(padapter);
		else
			rtw_resume_process_normal(padapter);
	#else
		rtw_resume_process_normal(padapter);
	#endif

	} else if (check_fwstate(pmlmepriv,WIFI_AP_STATE) == _TRUE
#ifdef CONFIG_CONCURRENT_MODE
		&& check_buddy_fwstate(padapter, WIFI_AP_STATE) == _FALSE
#endif
	) {
	#ifdef CONFIG_AP_WOWLAN
		rtw_resume_process_ap_wow(padapter);
	#else
		rtw_resume_process_normal(padapter);
	#endif //CONFIG_AP_WOWLAN
#ifdef CONFIG_CONCURRENT_MODE
	} else if (check_fwstate(pmlmepriv,WIFI_STATION_STATE) == _TRUE
		&& check_buddy_fwstate(padapter, WIFI_AP_STATE) == _TRUE) {
	#ifdef CONFIG_AP_WOWLAN
		rtw_resume_process_ap_wow(padapter);
	#else
		rtw_resume_process_normal(padapter);
	#endif //CONFIG_AP_WOWLAN
#endif
	} else {
		rtw_resume_process_normal(padapter);
	}

	#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_SuspendNotify(padapter, 0);
	#endif // CONFIG_BT_COEXIST

	if (pwrpriv) {
		pwrpriv->bInSuspend = _FALSE;
	#ifdef CONFIG_PNO_SUPPORT
		pwrpriv->pno_in_resume = _FALSE;
	#endif
	}
	DBG_871X_LEVEL(_drv_always_, "%s:%d in %d ms\n", __FUNCTION__ ,ret,
		rtw_get_passing_time_ms(start_time));

	_func_exit_;
	
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
	return rtw_hal_set_gpio_output_value(adapter, gpio_num,isHigh);
}
EXPORT_SYMBOL(rtw_set_gpio_output_value);

int rtw_config_gpio(struct net_device *netdev, u8 gpio_num, bool isOutput)
{
	_adapter *adapter = (_adapter *)rtw_netdev_priv(netdev);	
	return rtw_hal_config_gpio(adapter,gpio_num,isOutput);	
}
EXPORT_SYMBOL(rtw_config_gpio);
int rtw_register_gpio_interrupt(struct net_device *netdev, int gpio_num, void(*callback)(u8 level))
{
	_adapter *adapter = (_adapter *)rtw_netdev_priv(netdev);
	return rtw_hal_register_gpio_interrupt(adapter,gpio_num,callback);
}
EXPORT_SYMBOL(rtw_register_gpio_interrupt);

int rtw_disable_gpio_interrupt(struct net_device *netdev, int gpio_num)
{
	_adapter *adapter = (_adapter *)rtw_netdev_priv(netdev);
	return rtw_hal_disable_gpio_interrupt(adapter,gpio_num);
}
EXPORT_SYMBOL(rtw_disable_gpio_interrupt);

#endif //#ifdef CONFIG_GPIO_API 

