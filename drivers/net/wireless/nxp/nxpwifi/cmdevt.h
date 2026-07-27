/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * nxpwifi: commands and events
 *
 * Copyright 2011-2024 NXP
 */

#ifndef _NXPWIFI_CMD_EVT_H_
#define _NXPWIFI_CMD_EVT_H_

struct nxpwifi_cmd_entry {
	u16 cmd_no;
	int (*prepare_cmd)(struct nxpwifi_private *priv,
			   struct host_cmd_ds_command *cmd,
			   u16 cmd_no, void *data_buf,
			   u16 cmd_action, u32 cmd_type);
	int (*cmd_resp)(struct nxpwifi_private *priv,
			struct host_cmd_ds_command *resp,
			u16 cmdresp_no,
			void *data_buf);
};

struct nxpwifi_evt_entry {
	u32 event_cause;
	int (*event_handler)(struct nxpwifi_private *priv);
};

static inline int
nxpwifi_cmd_fill_head_only(struct nxpwifi_private *priv,
			   struct host_cmd_ds_command *cmd,
			   u16 cmd_no, void *data_buf,
			   u16 cmd_action, u32 cmd_type)
{
	cmd->command = cpu_to_le16(cmd_no);
	cmd->size = cpu_to_le16(S_DS_GEN);

	return 0;
}

int nxpwifi_send_cmd(struct nxpwifi_private *priv, u16 cmd_no,
		     u16 cmd_action, u32 cmd_oid, void *data_buf, bool sync);
int nxpwifi_sta_prepare_cmd(struct nxpwifi_private *priv,
			    struct cmd_ctrl_node *cmd_node,
			    u16 cmd_action, u32 cmd_oid);
int nxpwifi_sta_init_cmd(struct nxpwifi_private *priv, u8 first_sta, bool init);
int nxpwifi_uap_prepare_cmd(struct nxpwifi_private *priv,
			    struct cmd_ctrl_node *cmd_node,
			    u16 cmd_action, u32 type);
int nxpwifi_set_secure_params(struct nxpwifi_private *priv,
			      struct nxpwifi_uap_bss_param *bss_config,
			      struct cfg80211_ap_settings *params);
void nxpwifi_set_ht_params(struct nxpwifi_private *priv,
			   struct nxpwifi_uap_bss_param *bss_cfg,
			   struct cfg80211_ap_settings *params);
void nxpwifi_set_vht_params(struct nxpwifi_private *priv,
			    struct nxpwifi_uap_bss_param *bss_cfg,
			    struct cfg80211_ap_settings *params);
void nxpwifi_set_tpc_params(struct nxpwifi_private *priv,
			    struct nxpwifi_uap_bss_param *bss_cfg,
			    struct cfg80211_ap_settings *params);
void nxpwifi_set_uap_rates(struct nxpwifi_uap_bss_param *bss_cfg,
			   struct cfg80211_ap_settings *params);
void nxpwifi_set_vht_width(struct nxpwifi_private *priv,
			   enum nl80211_chan_width width,
			   bool ap_11ac_disable);
bool nxpwifi_check_11ax_capability(struct nxpwifi_private *priv,
				   struct nxpwifi_uap_bss_param *bss_cfg,
				   struct cfg80211_ap_settings *params);
int nxpwifi_set_11ax_status(struct nxpwifi_private *priv,
			    struct nxpwifi_uap_bss_param *bss_cfg,
			    struct cfg80211_ap_settings *params);
void nxpwifi_set_sys_config_invalid_data(struct nxpwifi_uap_bss_param *config);
void nxpwifi_set_wmm_params(struct nxpwifi_private *priv,
			    struct nxpwifi_uap_bss_param *bss_cfg,
			    struct cfg80211_ap_settings *params);
void nxpwifi_config_uap_11d(struct nxpwifi_private *priv,
			    struct cfg80211_beacon_data *beacon_data);
void nxpwifi_uap_set_channel(struct nxpwifi_private *priv,
			     struct nxpwifi_uap_bss_param *bss_cfg,
			     struct cfg80211_chan_def chandef);
int nxpwifi_config_start_uap(struct nxpwifi_private *priv,
			     struct nxpwifi_uap_bss_param *bss_cfg);
int nxpwifi_process_event(struct nxpwifi_adapter *adapter);
int nxpwifi_process_sta_event(struct nxpwifi_private *priv);
int nxpwifi_process_uap_event(struct nxpwifi_private *priv);
void nxpwifi_reset_connect_state(struct nxpwifi_private *priv, u16 reason,
				 bool from_ap);
void nxpwifi_process_multi_chan_event(struct nxpwifi_private *priv,
				      struct sk_buff *event_skb);
void nxpwifi_process_tx_pause_event(struct nxpwifi_private *priv,
				    struct sk_buff *event);
void nxpwifi_bt_coex_wlan_param_update_event(struct nxpwifi_private *priv,
					     struct sk_buff *event_skb);
int nxpwifi_mgmt_frame_reg(struct nxpwifi_private *priv, u32 mask);
int nxpwifi_set_uap_sys_cfg(struct nxpwifi_private *priv,
			    struct nxpwifi_uap_bss_param *cfg);
int nxpwifi_set_rts(struct nxpwifi_private *priv, u32 rts_thr);
int nxpwifi_set_frag(struct nxpwifi_private *priv, u32 frag_thr);
int nxpwifi_set_bss_mode(struct nxpwifi_private *priv);
int nxpwifi_config_monitor_mode(struct nxpwifi_private *priv,
				struct nxpwifi_802_11_net_monitor *cfg);
int nxpwifi_apply_regdomain(struct nxpwifi_private *priv);
int nxpwifi_get_tx_pwr(struct nxpwifi_private *priv);
int nxpwifi_get_rssi_info(struct nxpwifi_private *priv);
int nxpwifi_get_802_11_snmp_mib(struct nxpwifi_private *priv, u16 oid, void *value);
int nxpwifi_set_rf_antenna(struct nxpwifi_private *priv, void *antcfg);
int nxpwifi_get_rf_antenna(struct nxpwifi_private *priv, u32 *tx_ant, u32 *rx_ant);
int nxpwifi_ap_stop_bss(struct nxpwifi_private *priv);
int nxpwifi_ap_sys_reset(struct nxpwifi_private *priv);
int nxpwifi_cfg80211_deinit_p2p(struct nxpwifi_private *priv);
int nxpwifi_ap_get_sta_list(struct nxpwifi_private *priv);
int nxpwifi_set_tx_rate(struct nxpwifi_private *priv, void *bitmap_rates);
int nxpwifi_802_11_subscribe_event(struct nxpwifi_private *priv,
				   struct nxpwifi_ds_misc_subsc_evt *subsc_evt);
int nxpwifi_uap_sta_deauth(struct nxpwifi_private *priv, u8 *mac);
int nxpwifi_bg_scan_config(struct nxpwifi_private *priv, void *bg_scan_cfg);
int nxpwifi_mef_cfg(struct nxpwifi_private *priv, void *mef_cfg);
int nxpwifi_coalesce_cfg(struct nxpwifi_private *priv, void *coalesce_cfg);
int nxpwifi_add_new_station(struct nxpwifi_private *priv, void *add_sta);
int nxpwifi_hostcmd(struct nxpwifi_private *priv, struct nxpwifi_ds_misc_cmd *hostcmd);
int nxpwifi_chan_report_request(struct nxpwifi_private *priv, void *radar_params);
#endif /* !_NXPWIFI_CMD_EVT_H_ */
