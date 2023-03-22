/**
 ****************************************************************************************
 *
 * @file rwnx_msg_tx.h
 *
 * @brief TX function declarations
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ****************************************************************************************
 */

#ifndef _RWNX_MSG_TX_H_
#define _RWNX_MSG_TX_H_

#include "rwnx_defs.h"

int rwnx_send_reset(struct rwnx_hw *rwnx_hw);
int rwnx_send_start(struct rwnx_hw *rwnx_hw);
int rwnx_send_version_req(struct rwnx_hw *rwnx_hw, struct mm_version_cfm *cfm);
int rwnx_send_add_if(struct rwnx_hw *rwnx_hw, const unsigned char *mac,
                     enum nl80211_iftype iftype, bool p2p, struct mm_add_if_cfm *cfm);
int rwnx_send_remove_if(struct rwnx_hw *rwnx_hw, u8 vif_index, bool defer);
int rwnx_send_set_channel(struct rwnx_hw *rwnx_hw, int phy_idx,
                          struct mm_set_channel_cfm *cfm);
int rwnx_send_key_add(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 sta_idx, bool pairwise,
                      u8 *key, u8 key_len, u8 key_idx, u8 cipher_suite,
                      struct mm_key_add_cfm *cfm);
int rwnx_send_key_del(struct rwnx_hw *rwnx_hw, uint8_t hw_key_idx);
int rwnx_send_bcn(struct rwnx_hw *rwnx_hw,u8 *buf, u8 vif_idx, u16 bcn_len);

int rwnx_send_bcn_change(struct rwnx_hw *rwnx_hw, u8 vif_idx, u32 bcn_addr,
                         u16 bcn_len, u16 tim_oft, u16 tim_len, u16 *csa_oft);
int rwnx_send_tim_update(struct rwnx_hw *rwnx_hw, u8 vif_idx, u16 aid,
                         u8 tx_status);
int rwnx_send_roc(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
              struct ieee80211_channel *chan, unsigned int duration, struct mm_remain_on_channel_cfm *roc_cfm);
int rwnx_send_cancel_roc(struct rwnx_hw *rwnx_hw);
int rwnx_send_set_power(struct rwnx_hw *rwnx_hw,  u8 vif_idx, s8 pwr,
                        struct mm_set_power_cfm *cfm);
int rwnx_send_set_edca(struct rwnx_hw *rwnx_hw, u8 hw_queue, u32 param,
                       bool uapsd, u8 inst_nbr);
int rwnx_send_tdls_chan_switch_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
                                   struct rwnx_sta *rwnx_sta, bool sta_initiator,
                                   u8 oper_class, struct cfg80211_chan_def *chandef,
                                   struct tdls_chan_switch_cfm *cfm);
int rwnx_send_tdls_cancel_chan_switch_req(struct rwnx_hw *rwnx_hw,
                                          struct rwnx_vif *rwnx_vif,
                                          struct rwnx_sta *rwnx_sta,
                                          struct tdls_cancel_chan_switch_cfm *cfm);

#ifdef CONFIG_RWNX_P2P_DEBUGFS
int rwnx_send_p2p_oppps_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
                            u8 ctw, struct mm_set_p2p_oppps_cfm *cfm);
int rwnx_send_p2p_noa_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
                          int count, int interval, int duration,
                          bool dyn_noa, struct mm_set_p2p_noa_cfm *cfm);
#endif /* CONFIG_RWNX_P2P_DEBUGFS */

#ifdef AICWF_ARP_OFFLOAD
int rwnx_send_arpoffload_en_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
                          u32_l ipaddr,  u8_l enable);
#endif
int rwnx_send_rf_config_req(struct rwnx_hw *rwnx_hw, u8_l ofst, u8_l sel, u8_l *tbl, u16_l len);
int rwnx_send_rf_calib_req(struct rwnx_hw *rwnx_hw, struct mm_set_rf_calib_cfm *cfm);
int rwnx_send_get_macaddr_req(struct rwnx_hw *rwnx_hw, struct mm_get_mac_addr_cfm *cfm);

#ifdef CONFIG_RWNX_FULLMAC
int rwnx_send_me_config_req(struct rwnx_hw *rwnx_hw);
int rwnx_send_me_chan_config_req(struct rwnx_hw *rwnx_hw);
int rwnx_send_me_set_control_port_req(struct rwnx_hw *rwnx_hw, bool opened,
                                      u8 sta_idx);
int rwnx_send_me_sta_add(struct rwnx_hw *rwnx_hw, struct station_parameters *params,
                         const u8 *mac, u8 inst_nbr, struct me_sta_add_cfm *cfm);
int rwnx_send_me_sta_del(struct rwnx_hw *rwnx_hw, u8 sta_idx, bool tdls_sta);
int rwnx_send_me_traffic_ind(struct rwnx_hw *rwnx_hw, u8 sta_idx, bool uapsd, u8 tx_status);
int rwnx_send_me_rc_stats(struct rwnx_hw *rwnx_hw, u8 sta_idx,
                          struct me_rc_stats_cfm *cfm);
int rwnx_send_me_rc_set_rate(struct rwnx_hw *rwnx_hw,
                             u8 sta_idx,
                             u16 rate_idx);
int rwnx_send_me_set_ps_mode(struct rwnx_hw *rwnx_hw, u8 ps_mode);
int rwnx_send_me_set_lp_level(struct rwnx_hw *rwnx_hw, u8 lp_level);
int rwnx_send_sm_connect_req(struct rwnx_hw *rwnx_hw,
                             struct rwnx_vif *rwnx_vif,
                             struct cfg80211_connect_params *sme,
                             struct sm_connect_cfm *cfm);
int rwnx_send_sm_disconnect_req(struct rwnx_hw *rwnx_hw,
                                struct rwnx_vif *rwnx_vif,
                                u16 reason);
int rwnx_send_sm_external_auth_required_rsp(struct rwnx_hw *rwnx_hw,
                                            struct rwnx_vif *rwnx_vif,
                                            u16 status);
int rwnx_send_apm_start_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
                            struct cfg80211_ap_settings *settings,
                            struct apm_start_cfm *cfm,
                            struct rwnx_ipc_elem_var *elem);
int rwnx_send_apm_stop_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif);
int rwnx_send_scanu_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
                        struct cfg80211_scan_request *param);
int rwnx_send_scanu_cancel_req(struct rwnx_hw *rwnx_hw,
                              struct scan_cancel_cfm *cfm);

int rwnx_send_apm_start_cac_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
                                struct cfg80211_chan_def *chandef,
                                struct apm_start_cac_cfm *cfm);
int rwnx_send_apm_stop_cac_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif);
int rwnx_send_tdls_peer_traffic_ind_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif);
int rwnx_send_config_monitor_req(struct rwnx_hw *rwnx_hw,
                                 struct cfg80211_chan_def *chandef,
                                 struct me_config_monitor_cfm *cfm);
int rwnx_send_mesh_start_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
                             const struct mesh_config *conf, const struct mesh_setup *setup,
                             struct mesh_start_cfm *cfm);
int rwnx_send_mesh_stop_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
                            struct mesh_stop_cfm *cfm);
int rwnx_send_mesh_update_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
                              u32 mask, const struct mesh_config *p_mconf, struct mesh_update_cfm *cfm);
int rwnx_send_mesh_peer_info_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
                                 u8 sta_idx, struct mesh_peer_info_cfm *cfm);
void rwnx_send_mesh_peer_update_ntf(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
                                    u8 sta_idx, u8 mlink_state);
void rwnx_send_mesh_path_create_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, u8 *tgt_addr);
int rwnx_send_mesh_path_update_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, const u8 *tgt_addr,
                                   const u8 *p_nhop_addr, struct mesh_path_update_cfm *cfm);
void rwnx_send_mesh_proxy_add_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, u8 *ext_addr);
#endif /* CONFIG_RWNX_FULLMAC */

#ifdef CONFIG_RWNX_BFMER
#ifdef CONFIG_RWNX_FULLMAC
void rwnx_send_bfmer_enable(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta,
                            const struct ieee80211_vht_cap *vht_cap);
#endif /* CONFIG_RWNX_FULLMAC */
#ifdef CONFIG_RWNX_MUMIMO_TX
int rwnx_send_mu_group_update_req(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta);
#endif /* CONFIG_RWNX_MUMIMO_TX */
#endif /* CONFIG_RWNX_BFMER */

/* Debug messages */
int rwnx_send_dbg_trigger_req(struct rwnx_hw *rwnx_hw, char *msg);
int rwnx_send_dbg_mem_read_req(struct rwnx_hw *rwnx_hw, u32 mem_addr,
                               struct dbg_mem_read_cfm *cfm);
int rwnx_send_dbg_mem_write_req(struct rwnx_hw *rwnx_hw, u32 mem_addr,
                                u32 mem_data);
int rwnx_send_dbg_mem_mask_write_req(struct rwnx_hw *rwnx_hw, u32 mem_addr,
                                     u32 mem_mask, u32 mem_data);
int rwnx_send_dbg_set_mod_filter_req(struct rwnx_hw *rwnx_hw, u32 filter);
#ifdef CONFIG_RFTEST
int rwnx_send_rftest_req(struct rwnx_hw *rwnx_hw, u32_l cmd, u32_l argc, u8_l *argv, struct dbg_rftest_cmd_cfm *cfm);
#endif
int rwnx_send_dbg_set_sev_filter_req(struct rwnx_hw *rwnx_hw, u32 filter);
int rwnx_send_dbg_get_sys_stat_req(struct rwnx_hw *rwnx_hw,
                                   struct dbg_get_sys_stat_cfm *cfm);
int rwnx_send_dbg_mem_block_write_req(struct rwnx_hw *rwnx_hw, u32 mem_addr,
                                      u32 mem_size, u32 *mem_data);
int rwnx_send_dbg_start_app_req(struct rwnx_hw *rwnx_hw, u32 boot_addr,
                                u32 boot_type);
int rwnx_send_dbg_gpio_write_req(struct rwnx_hw *rwnx_hw, u8_l gpio_idx, u8_l gpio_val);
int rwnx_send_dbg_gpio_read_req(struct rwnx_hw *rwnx_hw, u8_l gpio_idx, struct dbg_gpio_read_cfm *cfm);
int rwnx_send_dbg_gpio_init_req(struct rwnx_hw *rwnx_hw, u8_l gpio_idx, u8_l gpio_dir, u8_l gpio_val);
int rwnx_send_cfg_rssi_req(struct rwnx_hw *rwnx_hw, u8 vif_index, int rssi_thold, u32 rssi_hyst);
int rwnx_send_coex_req(struct rwnx_hw *rwnx_hw, u8_l disable_coexnull, u8_l enable_nullcts);
int rwnx_send_get_sta_info_req(struct rwnx_hw *rwnx_hw, u8_l sta_idx, struct mm_get_sta_info_cfm *cfm);
int rwnx_send_set_stack_start_req(struct rwnx_hw *rwnx_hw, u8_l on, u8_l efuse_valid, u8_l set_vendor_info,
					u8_l fwtrace_redir_en, struct mm_set_stack_start_cfm *cfm);
int rwnx_send_txop_req(struct rwnx_hw *rwnx_hw, uint16_t *txop, u8_l long_nav_en, u8_l cfe_en);
int rwnx_send_vendor_hwconfig_req(struct rwnx_hw *rwnx_hw, uint32_t hwconfig_id, int32_t *param);

int rwnx_send_get_fw_version_req(struct rwnx_hw *rwnx_hw, struct mm_get_fw_version_cfm *cfm);
int rwnx_send_txpwr_idx_req(struct rwnx_hw *rwnx_hw);
int rwnx_send_txpwr_ofst_req(struct rwnx_hw *rwnx_hw);
int rwnx_send_set_filter(struct rwnx_hw *rwnx_hw, uint32_t filter);
int rwnx_send_txpwr_lvl_req(struct rwnx_hw *rwnx_hw);
#ifdef CONFIG_USB_BT
int rwnx_send_reboot(struct rwnx_hw *rwnx_hw);
#endif // CONFIG_USB_BT


#endif /* _RWNX_MSG_TX_H_ */
