/**
 ****************************************************************************************
 *
 * @file ecrnx_msg_tx.h
 *
 * @brief TX function declarations
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ****************************************************************************************
 */

#ifndef _ECRNX_MSG_TX_H_
#define _ECRNX_MSG_TX_H_

#include "ecrnx_defs.h"

int ecrnx_send_reset(struct ecrnx_hw *ecrnx_hw);
int ecrnx_send_start(struct ecrnx_hw *ecrnx_hw);
int ecrnx_send_version_req(struct ecrnx_hw *ecrnx_hw, struct mm_version_cfm *cfm);
int ecrnx_send_add_if(struct ecrnx_hw *ecrnx_hw, const unsigned char *mac,
                     enum nl80211_iftype iftype, bool p2p, struct mm_add_if_cfm *cfm);
int ecrnx_send_remove_if(struct ecrnx_hw *ecrnx_hw, u8 vif_index);
int ecrnx_send_set_channel(struct ecrnx_hw *ecrnx_hw, int phy_idx,
                          struct mm_set_channel_cfm *cfm);
int ecrnx_send_key_add(struct ecrnx_hw *ecrnx_hw, u8 vif_idx, u8 sta_idx, bool pairwise,
                      u8 *key, u8 key_len, u8 key_idx, u8 cipher_suite,
                      struct mm_key_add_cfm *cfm);
int ecrnx_send_key_del(struct ecrnx_hw *ecrnx_hw, uint8_t hw_key_idx);
int ecrnx_send_bcn_change(struct ecrnx_hw *ecrnx_hw, u8 vif_idx, dma_addr_t bcn_addr,
                         u16 bcn_len, u16 tim_oft, u16 tim_len, u16 *csa_oft);
int ecrnx_send_tim_update(struct ecrnx_hw *ecrnx_hw, u8 vif_idx, u16 aid,
                         u8 tx_status);
int ecrnx_send_roc(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *vif,
                  struct ieee80211_channel *chan, unsigned int duration);
int ecrnx_send_cancel_roc(struct ecrnx_hw *ecrnx_hw);
int ecrnx_send_set_power(struct ecrnx_hw *ecrnx_hw,  u8 vif_idx, s8 pwr,
                        struct mm_set_power_cfm *cfm);
int ecrnx_send_set_edca(struct ecrnx_hw *ecrnx_hw, u8 hw_queue, u32 param,
                       bool uapsd, u8 inst_nbr);
int ecrnx_send_tdls_chan_switch_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *ecrnx_vif,
                                   struct ecrnx_sta *ecrnx_sta, bool sta_initiator,
                                   u8 oper_class, struct cfg80211_chan_def *chandef,
                                   struct tdls_chan_switch_cfm *cfm);
int ecrnx_send_tdls_cancel_chan_switch_req(struct ecrnx_hw *ecrnx_hw,
                                          struct ecrnx_vif *ecrnx_vif,
                                          struct ecrnx_sta *ecrnx_sta,
                                          struct tdls_cancel_chan_switch_cfm *cfm);

#ifdef CONFIG_ECRNX_P2P_DEBUGFS
int ecrnx_send_p2p_oppps_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *ecrnx_vif,
                            u8 ctw, struct mm_set_p2p_oppps_cfm *cfm);
int ecrnx_send_p2p_noa_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *ecrnx_vif,
                          int count, int interval, int duration,
                          bool dyn_noa, struct mm_set_p2p_noa_cfm *cfm);
#endif /* CONFIG_ECRNX_P2P_DEBUGFS */

#ifdef CONFIG_ECRNX_SOFTMAC
int ecrnx_send_sta_add(struct ecrnx_hw *ecrnx_hw, struct ieee80211_sta *sta,
                      u8 inst_nbr, struct mm_sta_add_cfm *cfm);
int ecrnx_send_sta_del(struct ecrnx_hw *ecrnx_hw, u8 sta_idx);
int ecrnx_send_set_filter(struct ecrnx_hw *ecrnx_hw, uint32_t filter);
int ecrnx_send_add_chanctx(struct ecrnx_hw *ecrnx_hw,
                          struct ieee80211_chanctx_conf *ctx,
                          struct mm_chan_ctxt_add_cfm *cfm);
int ecrnx_send_del_chanctx(struct ecrnx_hw *ecrnx_hw, u8 index);
int ecrnx_send_link_chanctx(struct ecrnx_hw *ecrnx_hw, u8 vif_idx, u8 chan_idx,
                           u8 chan_switch);
int ecrnx_send_unlink_chanctx(struct ecrnx_hw *ecrnx_hw, u8 vif_idx);
int ecrnx_send_update_chanctx(struct ecrnx_hw *ecrnx_hw,
                             struct ieee80211_chanctx_conf *ctx);
int ecrnx_send_sched_chanctx(struct ecrnx_hw *ecrnx_hw, u8 vif_idx, u8 chan_idx,
                            u8 type);

int ecrnx_send_dtim_req(struct ecrnx_hw *ecrnx_hw, u8 dtim_period);
int ecrnx_send_set_br(struct ecrnx_hw *ecrnx_hw, u32 basic_rates, u8 vif_idx, u8 band);
int ecrnx_send_set_beacon_int(struct ecrnx_hw *ecrnx_hw, u16 beacon_int, u8 vif_idx);
int ecrnx_send_set_bssid(struct ecrnx_hw *ecrnx_hw, const u8 *bssid, u8 vif_idx);
int ecrnx_send_set_vif_state(struct ecrnx_hw *ecrnx_hw, bool active,
                            u16 aid, u8 vif_idx);
int ecrnx_send_set_mode(struct ecrnx_hw *ecrnx_hw, u8 abgmode);
int ecrnx_send_set_idle(struct ecrnx_hw *ecrnx_hw, int idle);
int ecrnx_send_set_ps_mode(struct ecrnx_hw *ecrnx_hw, u8 ps_mode);
int ecrnx_send_set_ps_options(struct ecrnx_hw *ecrnx_hw, bool listen_bcmc,
                             u16 listen_interval, u8 vif_idx);
int ecrnx_send_set_slottime(struct ecrnx_hw *ecrnx_hw, int use_short_slot);
int ecrnx_send_ba_add(struct ecrnx_hw *ecrnx_hw, uint8_t type, uint8_t sta_idx,
                     u16 tid, uint8_t bufsz, uint16_t ssn,
                     struct mm_ba_add_cfm *cfm);
int ecrnx_send_ba_del(struct ecrnx_hw *ecrnx_hw, uint8_t sta_idx, u16 tid,
                     struct mm_ba_del_cfm *cfm);
int ecrnx_send_scan_req(struct ecrnx_hw *ecrnx_hw, struct ieee80211_vif *vif,
                       struct cfg80211_scan_request *param,
                       struct scan_start_cfm *cfm);
int ecrnx_send_scan_cancel_req(struct ecrnx_hw *ecrnx_hw,
                              struct scan_cancel_cfm *cfm);
void ecrnx_send_tdls_ps(struct ecrnx_hw *ecrnx_hw, bool ps_mode);
#endif /* CONFIG_ECRNX_SOFTMAC */

#ifdef CONFIG_ECRNX_FULLMAC
int ecrnx_send_me_config_req(struct ecrnx_hw *ecrnx_hw);
int ecrnx_send_me_chan_config_req(struct ecrnx_hw *ecrnx_hw);
int ecrnx_send_me_set_control_port_req(struct ecrnx_hw *ecrnx_hw, bool opened,
                                      u8 sta_idx);
int ecrnx_send_me_sta_add(struct ecrnx_hw *ecrnx_hw, struct station_parameters *params,
                         const u8 *mac, u8 inst_nbr, struct me_sta_add_cfm *cfm);
int ecrnx_send_me_sta_del(struct ecrnx_hw *ecrnx_hw, u8 sta_idx, bool tdls_sta);
int ecrnx_send_me_traffic_ind(struct ecrnx_hw *ecrnx_hw, u8 sta_idx, bool uapsd, u8 tx_status);
int ecrnx_send_twt_request(struct ecrnx_hw *ecrnx_hw,
                          u8 setup_type, u8 vif_idx,
                          struct twt_conf_tag *conf,
                          struct twt_setup_cfm *cfm);
int ecrnx_send_twt_teardown(struct ecrnx_hw *ecrnx_hw,
                           struct twt_teardown_req *twt_teardown,
                           struct twt_teardown_cfm *cfm);
int ecrnx_send_me_rc_stats(struct ecrnx_hw *ecrnx_hw, u8 sta_idx,
                          struct me_rc_stats_cfm *cfm);
int ecrnx_send_me_rc_set_rate(struct ecrnx_hw *ecrnx_hw,
                             u8 sta_idx,
                             u16 rate_idx);
int ecrnx_send_me_set_ps_mode(struct ecrnx_hw *ecrnx_hw, u8 ps_mode);
int ecrnx_send_sm_connect_req(struct ecrnx_hw *ecrnx_hw,
                             struct ecrnx_vif *ecrnx_vif,
                             struct cfg80211_connect_params *sme,
                             struct sm_connect_cfm *cfm);
int ecrnx_send_sm_disconnect_req(struct ecrnx_hw *ecrnx_hw,
                                struct ecrnx_vif *ecrnx_vif,
                                u16 reason);
int ecrnx_send_sm_external_auth_required_rsp(struct ecrnx_hw *ecrnx_hw,
                                            struct ecrnx_vif *ecrnx_vif,
                                            u16 status);
int ecrnx_send_sm_ft_auth_rsp(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *ecrnx_vif,
                             uint8_t *ie, int ie_len);
int ecrnx_send_apm_start_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *vif,
                            struct cfg80211_ap_settings *settings,
                            struct apm_start_cfm *cfm,
                            struct ecrnx_ipc_elem_var *elem);
int ecrnx_send_apm_stop_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *vif);
int ecrnx_send_apm_probe_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *vif,
                            struct ecrnx_sta *sta, struct apm_probe_client_cfm *cfm);
int ecrnx_send_scanu_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *ecrnx_vif,
                        struct cfg80211_scan_request *param);
int ecrnx_send_scanu_cancel_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *ecrnx_vif);
int ecrnx_send_apm_start_cac_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *vif,
                                struct cfg80211_chan_def *chandef,
                                struct apm_start_cac_cfm *cfm);
int ecrnx_send_apm_stop_cac_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *vif);
int ecrnx_send_tdls_peer_traffic_ind_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *ecrnx_vif);
int ecrnx_send_config_monitor_req(struct ecrnx_hw *ecrnx_hw,
                                 struct cfg80211_chan_def *chandef,
                                 struct me_config_monitor_cfm *cfm);
int ecrnx_send_mesh_start_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *vif,
                             const struct mesh_config *conf, const struct mesh_setup *setup,
                             struct mesh_start_cfm *cfm);
int ecrnx_send_mesh_stop_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *vif,
                            struct mesh_stop_cfm *cfm);
int ecrnx_send_mesh_update_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *vif,
                              u32 mask, const struct mesh_config *p_mconf, struct mesh_update_cfm *cfm);
int ecrnx_send_mesh_peer_info_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *vif,
                                 u8 sta_idx, struct mesh_peer_info_cfm *cfm);
void ecrnx_send_mesh_peer_update_ntf(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *vif,
                                    u8 sta_idx, u8 mlink_state);
void ecrnx_send_mesh_path_create_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *vif, u8 *tgt_addr);
int ecrnx_send_mesh_path_update_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *vif, const u8 *tgt_addr,
                                   const u8 *p_nhop_addr, struct mesh_path_update_cfm *cfm);
void ecrnx_send_mesh_proxy_add_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *vif, u8 *ext_addr);
#if defined(CONFIG_ECRNX_P2P)
int ecrnx_send_p2p_start_listen_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *ecrnx_vif, unsigned int duration);
int ecrnx_send_p2p_cancel_listen_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *ecrnx_vif);
#endif
#endif /* CONFIG_ECRNX_FULLMAC */

#ifdef CONFIG_ECRNX_BFMER
#ifdef CONFIG_ECRNX_SOFTMAC
void ecrnx_send_bfmer_enable(struct ecrnx_hw *ecrnx_hw, struct ieee80211_sta *sta);
#else
void ecrnx_send_bfmer_enable(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sta *ecrnx_sta,
                            const struct ieee80211_vht_cap *vht_cap);
#endif /* CONFIG_ECRNX_SOFTMAC*/
#ifdef CONFIG_ECRNX_MUMIMO_TX
int ecrnx_send_mu_group_update_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sta *ecrnx_sta);
#endif /* CONFIG_ECRNX_MUMIMO_TX */
#endif /* CONFIG_ECRNX_BFMER */

/* Debug messages */
int ecrnx_send_dbg_trigger_req(struct ecrnx_hw *ecrnx_hw, char *msg);
int ecrnx_send_dbg_mem_read_req(struct ecrnx_hw *ecrnx_hw, u32 mem_addr,
                               struct dbg_mem_read_cfm *cfm);
int ecrnx_send_dbg_mem_write_req(struct ecrnx_hw *ecrnx_hw, u32 mem_addr,
                                u32 mem_data);
int ecrnx_send_dbg_set_mod_filter_req(struct ecrnx_hw *ecrnx_hw, u32 filter);
int ecrnx_send_dbg_set_sev_filter_req(struct ecrnx_hw *ecrnx_hw, u32 filter);
int ecrnx_send_dbg_get_sys_stat_req(struct ecrnx_hw *ecrnx_hw,
                                   struct dbg_get_sys_stat_cfm *cfm);
int ecrnx_send_cfg_rssi_req(struct ecrnx_hw *ecrnx_hw, u8 vif_index, int rssi_thold, u32 rssi_hyst);
int ecrnx_send_set_gain_delta_req(struct ecrnx_hw *ecrnx_hw);
int ecrnx_send_cal_result_get_req(struct ecrnx_hw *ecrnx_hw, void *cfm);
#endif /* _ECRNX_MSG_TX_H_ */
