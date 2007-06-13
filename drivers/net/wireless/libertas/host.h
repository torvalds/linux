/**
  * This file contains definitions of WLAN commands.
  */

#ifndef _HOST_H_
#define _HOST_H_

/** PUBLIC DEFINITIONS */
#define DEFAULT_AD_HOC_CHANNEL       6
#define DEFAULT_AD_HOC_CHANNEL_A    36

/** IEEE 802.11 oids */
#define OID_802_11_SSID                       0x00008002
#define OID_802_11_INFRASTRUCTURE_MODE        0x00008008
#define OID_802_11_FRAGMENTATION_THRESHOLD    0x00008009
#define OID_802_11_RTS_THRESHOLD              0x0000800A
#define OID_802_11_TX_ANTENNA_SELECTED        0x0000800D
#define OID_802_11_SUPPORTED_RATES            0x0000800E
#define OID_802_11_STATISTICS                 0x00008012
#define OID_802_11_TX_RETRYCOUNT              0x0000801D
#define OID_802_11D_ENABLE                    0x00008020

#define cmd_option_waitforrsp             0x0002

/** Host command ID */
#define cmd_code_dnld                 0x0002
#define cmd_get_hw_spec               0x0003
#define cmd_eeprom_update             0x0004
#define cmd_802_11_reset              0x0005
#define cmd_802_11_scan               0x0006
#define cmd_802_11_get_log            0x000b
#define cmd_mac_multicast_adr         0x0010
#define cmd_802_11_authenticate       0x0011
#define cmd_802_11_eeprom_access      0x0059
#define cmd_802_11_associate          0x0050
#define cmd_802_11_set_wep            0x0013
#define cmd_802_11_get_stat           0x0014
#define cmd_802_3_get_stat            0x0015
#define cmd_802_11_snmp_mib           0x0016
#define cmd_mac_reg_map               0x0017
#define cmd_bbp_reg_map               0x0018
#define cmd_mac_reg_access            0x0019
#define cmd_bbp_reg_access            0x001a
#define cmd_rf_reg_access             0x001b
#define cmd_802_11_radio_control      0x001c
#define cmd_802_11_rf_channel         0x001d
#define cmd_802_11_rf_tx_power        0x001e
#define cmd_802_11_rssi               0x001f
#define cmd_802_11_rf_antenna         0x0020

#define cmd_802_11_ps_mode	      0x0021

#define cmd_802_11_data_rate          0x0022
#define cmd_rf_reg_map                0x0023
#define cmd_802_11_deauthenticate     0x0024
#define cmd_802_11_reassociate        0x0025
#define cmd_802_11_disassociate       0x0026
#define cmd_mac_control               0x0028
#define cmd_802_11_ad_hoc_start       0x002b
#define cmd_802_11_ad_hoc_join        0x002c

#define cmd_802_11_query_tkip_reply_cntrs  0x002e
#define cmd_802_11_enable_rsn              0x002f
#define cmd_802_11_pairwise_tsc       0x0036
#define cmd_802_11_group_tsc          0x0037
#define cmd_802_11_key_material       0x005e

#define cmd_802_11_set_afc            0x003c
#define cmd_802_11_get_afc            0x003d

#define cmd_802_11_ad_hoc_stop        0x0040

#define cmd_802_11_beacon_stop        0x0049

#define cmd_802_11_mac_address        0x004D
#define cmd_802_11_eeprom_access      0x0059

#define cmd_802_11_band_config        0x0058

#define cmd_802_11d_domain_info       0x005b

#define cmd_802_11_sleep_params          0x0066

#define cmd_802_11_inactivity_timeout    0x0067

#define cmd_802_11_tpc_cfg               0x0072
#define cmd_802_11_pwr_cfg               0x0073

#define cmd_802_11_led_gpio_ctrl         0x004e

#define cmd_802_11_subscribe_event       0x0075

#define cmd_802_11_rate_adapt_rateset    0x0076

#define cmd_802_11_tx_rate_query	0x007f

#define cmd_get_tsf                      0x0080

#define cmd_bt_access                 0x0087
#define cmd_ret_bt_access                 0x8087

#define cmd_fwt_access                0x0095
#define cmd_ret_fwt_access                0x8095

#define cmd_mesh_access               0x009b
#define cmd_ret_mesh_access               0x809b

/* For the IEEE Power Save */
#define cmd_subcmd_enter_ps               0x0030
#define cmd_subcmd_exit_ps                0x0031
#define cmd_subcmd_sleep_confirmed        0x0034
#define cmd_subcmd_full_powerdown         0x0035
#define cmd_subcmd_full_powerup           0x0036

/* command RET code, MSB is set to 1 */
#define cmd_ret_hw_spec_info              0x8003
#define cmd_ret_eeprom_update             0x8004
#define cmd_ret_802_11_reset              0x8005
#define cmd_ret_802_11_scan               0x8006
#define cmd_ret_802_11_get_log            0x800b
#define cmd_ret_mac_control               0x8028
#define cmd_ret_mac_multicast_adr         0x8010
#define cmd_ret_802_11_authenticate       0x8011
#define cmd_ret_802_11_deauthenticate     0x8024
#define cmd_ret_802_11_associate          0x8012
#define cmd_ret_802_11_reassociate        0x8025
#define cmd_ret_802_11_disassociate       0x8026
#define cmd_ret_802_11_set_wep            0x8013
#define cmd_ret_802_11_stat               0x8014
#define cmd_ret_802_3_stat                0x8015
#define cmd_ret_802_11_snmp_mib           0x8016
#define cmd_ret_mac_reg_map               0x8017
#define cmd_ret_bbp_reg_map               0x8018
#define cmd_ret_rf_reg_map                0x8023
#define cmd_ret_mac_reg_access            0x8019
#define cmd_ret_bbp_reg_access            0x801a
#define cmd_ret_rf_reg_access             0x801b
#define cmd_ret_802_11_radio_control      0x801c
#define cmd_ret_802_11_rf_channel         0x801d
#define cmd_ret_802_11_rssi               0x801f
#define cmd_ret_802_11_rf_tx_power        0x801e
#define cmd_ret_802_11_rf_antenna         0x8020
#define cmd_ret_802_11_ps_mode            0x8021
#define cmd_ret_802_11_data_rate          0x8022

#define cmd_ret_802_11_ad_hoc_start       0x802B
#define cmd_ret_802_11_ad_hoc_join        0x802C

#define cmd_ret_802_11_query_tkip_reply_cntrs  0x802e
#define cmd_ret_802_11_enable_rsn              0x802f
#define cmd_ret_802_11_pairwise_tsc       0x8036
#define cmd_ret_802_11_group_tsc          0x8037
#define cmd_ret_802_11_key_material       0x805e

#define cmd_enable_rsn                    0x0001
#define cmd_disable_rsn                   0x0000

#define cmd_act_set                       0x0001
#define cmd_act_get                       0x0000

#define cmd_act_get_AES                   (cmd_act_get + 2)
#define cmd_act_set_AES                   (cmd_act_set + 2)
#define cmd_act_remove_aes                (cmd_act_set + 3)

#define cmd_ret_802_11_set_afc            0x803c
#define cmd_ret_802_11_get_afc            0x803d

#define cmd_ret_802_11_ad_hoc_stop        0x8040

#define cmd_ret_802_11_beacon_stop        0x8049

#define cmd_ret_802_11_mac_address        0x804D
#define cmd_ret_802_11_eeprom_access      0x8059

#define cmd_ret_802_11_band_config        0x8058

#define cmd_ret_802_11_sleep_params          0x8066

#define cmd_ret_802_11_inactivity_timeout    0x8067

#define cmd_ret_802_11d_domain_info      (0x8000 |                  \
                                              cmd_802_11d_domain_info)

#define cmd_ret_802_11_tpc_cfg        (cmd_802_11_tpc_cfg | 0x8000)
#define cmd_ret_802_11_pwr_cfg        (cmd_802_11_pwr_cfg | 0x8000)

#define cmd_ret_802_11_led_gpio_ctrl     0x804e

#define cmd_ret_802_11_subscribe_event	(cmd_802_11_subscribe_event | 0x8000)

#define cmd_ret_802_11_rate_adapt_rateset	(cmd_802_11_rate_adapt_rateset | 0x8000)

#define cmd_rte_802_11_tx_rate_query 	(cmd_802_11_tx_rate_query | 0x8000)

#define cmd_ret_get_tsf             0x8080

/* Define action or option for cmd_802_11_set_wep */
#define cmd_act_add                         0x0002
#define cmd_act_remove                      0x0004
#define cmd_act_use_default                 0x0008

#define cmd_type_wep_40_bit                 0x0001
#define cmd_type_wep_104_bit                0x0002

#define cmd_NUM_OF_WEP_KEYS                 4

#define cmd_WEP_KEY_INDEX_MASK              0x3fff

/* Define action or option for cmd_802_11_reset */
#define cmd_act_halt                        0x0003

/* Define action or option for cmd_802_11_scan */
#define cmd_bss_type_bss                    0x0001
#define cmd_bss_type_ibss                   0x0002
#define cmd_bss_type_any                    0x0003

/* Define action or option for cmd_802_11_scan */
#define cmd_scan_type_active                0x0000
#define cmd_scan_type_passive               0x0001

#define cmd_scan_radio_type_bg		0

#define cmd_scan_probe_delay_time           0

/* Define action or option for cmd_mac_control */
#define cmd_act_mac_rx_on                   0x0001
#define cmd_act_mac_tx_on                   0x0002
#define cmd_act_mac_loopback_on             0x0004
#define cmd_act_mac_wep_enable              0x0008
#define cmd_act_mac_int_enable              0x0010
#define cmd_act_mac_multicast_enable        0x0020
#define cmd_act_mac_broadcast_enable        0x0040
#define cmd_act_mac_promiscuous_enable      0x0080
#define cmd_act_mac_all_multicast_enable    0x0100
#define cmd_act_mac_strict_protection_enable  0x0400

/* Define action or option for cmd_802_11_radio_control */
#define cmd_type_auto_preamble              0x0001
#define cmd_type_short_preamble             0x0002
#define cmd_type_long_preamble              0x0003

#define TURN_ON_RF                              0x01
#define RADIO_ON                                0x01
#define RADIO_OFF                               0x00

#define SET_AUTO_PREAMBLE                       0x05
#define SET_SHORT_PREAMBLE                      0x03
#define SET_LONG_PREAMBLE                       0x01

/* Define action or option for CMD_802_11_RF_CHANNEL */
#define cmd_opt_802_11_rf_channel_get       0x00
#define cmd_opt_802_11_rf_channel_set       0x01

/* Define action or option for cmd_802_11_rf_tx_power */
#define cmd_act_tx_power_opt_get            0x0000
#define cmd_act_tx_power_opt_set_high       0x8007
#define cmd_act_tx_power_opt_set_mid        0x8004
#define cmd_act_tx_power_opt_set_low        0x8000

#define cmd_act_tx_power_index_high         0x0007
#define cmd_act_tx_power_index_mid          0x0004
#define cmd_act_tx_power_index_low          0x0000

/* Define action or option for cmd_802_11_data_rate */
#define cmd_act_set_tx_auto                 0x0000
#define cmd_act_set_tx_fix_rate             0x0001
#define cmd_act_get_tx_rate                 0x0002

#define cmd_act_set_rx                      0x0001
#define cmd_act_set_tx                      0x0002
#define cmd_act_set_both                    0x0003
#define cmd_act_get_rx                      0x0004
#define cmd_act_get_tx                      0x0008
#define cmd_act_get_both                    0x000c

/* Define action or option for cmd_802_11_ps_mode */
#define cmd_type_cam                        0x0000
#define cmd_type_max_psp                    0x0001
#define cmd_type_fast_psp                   0x0002

/* Define action or option for cmd_bt_access */
enum cmd_bt_access_opts {
	/* The bt commands start at 5 instead of 1 because the old dft commands
	 * are mapped to 1-4.  These old commands are no longer maintained and
	 * should not be called.
	 */
	cmd_act_bt_access_add = 5,
	cmd_act_bt_access_del,
	cmd_act_bt_access_list,
	cmd_act_bt_access_reset,
	cmd_act_bt_access_set_invert,
	cmd_act_bt_access_get_invert
};

/* Define action or option for cmd_fwt_access */
enum cmd_fwt_access_opts {
	cmd_act_fwt_access_add = 1,
	cmd_act_fwt_access_del,
	cmd_act_fwt_access_lookup,
	cmd_act_fwt_access_list,
	cmd_act_fwt_access_list_route,
	cmd_act_fwt_access_list_neighbor,
	cmd_act_fwt_access_reset,
	cmd_act_fwt_access_cleanup,
	cmd_act_fwt_access_time,
};

/* Define action or option for cmd_mesh_access */
enum cmd_mesh_access_opts {
	cmd_act_mesh_get_ttl = 1,
	cmd_act_mesh_set_ttl,
	cmd_act_mesh_get_stats,
	cmd_act_mesh_get_anycast,
	cmd_act_mesh_set_anycast,
};

/** Card Event definition */
#define MACREG_INT_CODE_TX_PPA_FREE             0x00000000
#define MACREG_INT_CODE_TX_DMA_DONE             0x00000001
#define MACREG_INT_CODE_LINK_LOSE_W_SCAN        0x00000002
#define MACREG_INT_CODE_LINK_LOSE_NO_SCAN       0x00000003
#define MACREG_INT_CODE_LINK_SENSED             0x00000004
#define MACREG_INT_CODE_CMD_FINISHED            0x00000005
#define MACREG_INT_CODE_MIB_CHANGED             0x00000006
#define MACREG_INT_CODE_INIT_DONE               0x00000007
#define MACREG_INT_CODE_DEAUTHENTICATED         0x00000008
#define MACREG_INT_CODE_DISASSOCIATED           0x00000009
#define MACREG_INT_CODE_PS_AWAKE                0x0000000a
#define MACREG_INT_CODE_PS_SLEEP                0x0000000b
#define MACREG_INT_CODE_MIC_ERR_MULTICAST       0x0000000d
#define MACREG_INT_CODE_MIC_ERR_UNICAST         0x0000000e
#define MACREG_INT_CODE_WM_AWAKE                0x0000000f
#define MACREG_INT_CODE_ADHOC_BCN_LOST          0x00000011
#define MACREG_INT_CODE_RSSI_LOW		0x00000019
#define MACREG_INT_CODE_SNR_LOW			0x0000001a
#define MACREG_INT_CODE_MAX_FAIL		0x0000001b
#define MACREG_INT_CODE_RSSI_HIGH		0x0000001c
#define MACREG_INT_CODE_SNR_HIGH		0x0000001d
#define MACREG_INT_CODE_MESH_AUTO_STARTED	0x00000023

#endif				/* _HOST_H_ */
