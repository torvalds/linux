/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2005-2014, 2021, 2024-2025 Intel Corporation
 */
#ifndef __iwl_agn_h__
#define __iwl_agn_h__

#include "iwl-config.h"

#include "dev.h"

/* The first 11 queues (0-10) are used otherwise */
#define IWLAGN_FIRST_AMPDU_QUEUE	11

/* AUX (TX during scan dwell) queue */
#define IWL_AUX_QUEUE		10

#define IWL_INVALID_STATION	255

/* device operations */
extern const struct iwl_dvm_cfg iwl_dvm_1000_cfg;
extern const struct iwl_dvm_cfg iwl_dvm_2000_cfg;
extern const struct iwl_dvm_cfg iwl_dvm_105_cfg;
extern const struct iwl_dvm_cfg iwl_dvm_2030_cfg;
extern const struct iwl_dvm_cfg iwl_dvm_5000_cfg;
extern const struct iwl_dvm_cfg iwl_dvm_5150_cfg;
extern const struct iwl_dvm_cfg iwl_dvm_6000_cfg;
extern const struct iwl_dvm_cfg iwl_dvm_6005_cfg;
extern const struct iwl_dvm_cfg iwl_dvm_6050_cfg;
extern const struct iwl_dvm_cfg iwl_dvm_6030_cfg;


#define TIME_UNIT		1024

/*****************************************************
* DRIVER STATUS FUNCTIONS
******************************************************/
#define STATUS_RF_KILL_HW	0
#define STATUS_CT_KILL		1
#define STATUS_ALIVE		2
#define STATUS_READY		3
#define STATUS_EXIT_PENDING	5
#define STATUS_STATISTICS	6
#define STATUS_SCANNING		7
#define STATUS_SCAN_ABORTING	8
#define STATUS_SCAN_HW		9
#define STATUS_FW_ERROR		10
#define STATUS_CHANNEL_SWITCH_PENDING 11
#define STATUS_SCAN_COMPLETE	12
#define STATUS_POWER_PMI	13

struct iwl_ucode_capabilities;

extern const struct ieee80211_ops iwlagn_hw_ops;

static inline void iwl_set_calib_hdr(struct iwl_calib_hdr *hdr, u8 cmd)
{
	hdr->op_code = cmd;
	hdr->first_group = 0;
	hdr->groups_num = 1;
	hdr->data_valid = 1;
}

void iwl_down(struct iwl_priv *priv);
void iwl_cancel_deferred_work(struct iwl_priv *priv);
void iwlagn_prepare_restart(struct iwl_priv *priv);
void iwl_rx_dispatch(struct iwl_op_mode *op_mode, struct napi_struct *napi,
		     struct iwl_rx_cmd_buffer *rxb);

bool iwl_check_for_ct_kill(struct iwl_priv *priv);

void iwlagn_lift_passive_no_rx(struct iwl_priv *priv);

/* MAC80211 */
struct ieee80211_hw *iwl_alloc_all(void);
int iwlagn_mac_setup_register(struct iwl_priv *priv,
			      const struct iwl_ucode_capabilities *capa);
void iwlagn_mac_unregister(struct iwl_priv *priv);

/* commands */
int iwl_dvm_send_cmd(struct iwl_priv *priv, struct iwl_host_cmd *cmd);
int iwl_dvm_send_cmd_pdu(struct iwl_priv *priv, u8 id,
			 u32 flags, u16 len, const void *data);

/* RXON */
void iwl_connection_init_rx_config(struct iwl_priv *priv,
				   struct iwl_rxon_context *ctx);
int iwlagn_set_pan_params(struct iwl_priv *priv);
int iwlagn_commit_rxon(struct iwl_priv *priv, struct iwl_rxon_context *ctx);
void iwlagn_set_rxon_chain(struct iwl_priv *priv, struct iwl_rxon_context *ctx);
int iwlagn_mac_config(struct ieee80211_hw *hw, u32 changed);
void iwlagn_bss_info_changed(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *bss_conf,
			     u64 changes);
void iwlagn_config_ht40(struct ieee80211_conf *conf,
			struct iwl_rxon_context *ctx);
void iwl_set_rxon_ht(struct iwl_priv *priv, struct iwl_ht_config *ht_conf);
void iwl_set_rxon_channel(struct iwl_priv *priv, struct ieee80211_channel *ch,
			 struct iwl_rxon_context *ctx);
void iwl_set_flags_for_band(struct iwl_priv *priv,
			    struct iwl_rxon_context *ctx,
			    enum nl80211_band band,
			    struct ieee80211_vif *vif);

/* uCode */
int iwl_send_bt_env(struct iwl_priv *priv, u8 action, u8 type);
void iwl_send_prio_tbl(struct iwl_priv *priv);
int iwl_init_alive_start(struct iwl_priv *priv);
int iwl_run_init_ucode(struct iwl_priv *priv);
int iwl_load_ucode_wait_alive(struct iwl_priv *priv,
			      enum iwl_ucode_type ucode_type);
int iwl_send_calib_results(struct iwl_priv *priv);
int iwl_calib_set(struct iwl_priv *priv,
		  const struct iwl_calib_cmd *cmd, size_t len);
void iwl_calib_free_results(struct iwl_priv *priv);
int iwl_dump_nic_event_log(struct iwl_priv *priv, bool full_log,
			    char **buf);
int iwlagn_hw_valid_rtc_data_addr(u32 addr);

/* lib */
int iwlagn_send_tx_power(struct iwl_priv *priv);
void iwlagn_temperature(struct iwl_priv *priv);
int iwlagn_txfifo_flush(struct iwl_priv *priv, u32 scd_q_msk);
void iwlagn_dev_txfifo_flush(struct iwl_priv *priv);
int iwlagn_send_beacon_cmd(struct iwl_priv *priv);
int iwl_send_statistics_request(struct iwl_priv *priv,
				u8 flags, bool clear);

static inline const struct ieee80211_supported_band *iwl_get_hw_mode(
			struct iwl_priv *priv, enum nl80211_band band)
{
	return priv->hw->wiphy->bands[band];
}

#ifdef CONFIG_PM_SLEEP
int iwlagn_send_patterns(struct iwl_priv *priv,
			 struct cfg80211_wowlan *wowlan);
int iwlagn_suspend(struct iwl_priv *priv, struct cfg80211_wowlan *wowlan);
#endif

/* rx */
int iwlagn_hwrate_to_mac80211_idx(u32 rate_n_flags, enum nl80211_band band);
void iwl_setup_rx_handlers(struct iwl_priv *priv);
void iwl_chswitch_done(struct iwl_priv *priv, bool is_success);


/* tx */
int iwlagn_tx_skb(struct iwl_priv *priv,
		  struct ieee80211_sta *sta,
		  struct sk_buff *skb);
int iwlagn_tx_agg_start(struct iwl_priv *priv, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta, u16 tid, u16 *ssn);
int iwlagn_tx_agg_oper(struct iwl_priv *priv, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta, u16 tid, u8 buf_size);
int iwlagn_tx_agg_stop(struct iwl_priv *priv, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, u16 tid);
int iwlagn_tx_agg_flush(struct iwl_priv *priv, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta, u16 tid);
void iwlagn_rx_reply_compressed_ba(struct iwl_priv *priv,
				   struct iwl_rx_cmd_buffer *rxb);
void iwlagn_rx_reply_tx(struct iwl_priv *priv, struct iwl_rx_cmd_buffer *rxb);

static inline u32 iwl_tx_status_to_mac80211(u32 status)
{
	status &= TX_STATUS_MSK;

	switch (status) {
	case TX_STATUS_SUCCESS:
	case TX_STATUS_DIRECT_DONE:
		return IEEE80211_TX_STAT_ACK;
	case TX_STATUS_FAIL_DEST_PS:
	case TX_STATUS_FAIL_PASSIVE_NO_RX:
		return IEEE80211_TX_STAT_TX_FILTERED;
	default:
		return 0;
	}
}

static inline bool iwl_is_tx_success(u32 status)
{
	status &= TX_STATUS_MSK;
	return (status == TX_STATUS_SUCCESS) ||
	       (status == TX_STATUS_DIRECT_DONE);
}

u8 iwl_toggle_tx_ant(struct iwl_priv *priv, u8 ant_idx, u8 valid);

/* scan */
void iwlagn_post_scan(struct iwl_priv *priv);
int iwl_force_rf_reset(struct iwl_priv *priv, bool external);
void iwl_init_scan_params(struct iwl_priv *priv);
int iwl_scan_cancel(struct iwl_priv *priv);
void iwl_scan_cancel_timeout(struct iwl_priv *priv, unsigned long ms);
void iwl_force_scan_end(struct iwl_priv *priv);
void iwl_internal_short_hw_scan(struct iwl_priv *priv);
void iwl_setup_rx_scan_handlers(struct iwl_priv *priv);
void iwl_setup_scan_deferred_work(struct iwl_priv *priv);
void iwl_cancel_scan_deferred_work(struct iwl_priv *priv);
int __must_check iwl_scan_initiate(struct iwl_priv *priv,
				   struct ieee80211_vif *vif,
				   enum iwl_scan_type scan_type,
				   enum nl80211_band band);

/* For faster active scanning, scan will move to the next channel if fewer than
 * PLCP_QUIET_THRESH packets are heard on this channel within
 * ACTIVE_QUIET_TIME after sending probe request.  This shortens the dwell
 * time if it's a quiet channel (nothing responded to our probe, and there's
 * no other traffic).
 * Disable "quiet" feature by setting PLCP_QUIET_THRESH to 0. */
#define IWL_ACTIVE_QUIET_TIME       cpu_to_le16(10)  /* msec */
#define IWL_PLCP_QUIET_THRESH       cpu_to_le16(1)  /* packets */

#define IWL_SCAN_CHECK_WATCHDOG		(HZ * 15)


/* bt coex */
void iwlagn_send_advance_bt_config(struct iwl_priv *priv);
void iwlagn_bt_rx_handler_setup(struct iwl_priv *priv);
void iwlagn_bt_setup_deferred_work(struct iwl_priv *priv);
void iwlagn_bt_cancel_deferred_work(struct iwl_priv *priv);
void iwlagn_bt_coex_rssi_monitor(struct iwl_priv *priv);
void iwlagn_bt_adjust_rssi_monitor(struct iwl_priv *priv, bool rssi_ena);

static inline bool iwl_advanced_bt_coexist(struct iwl_priv *priv)
{
	return priv->lib->bt_params &&
	       priv->lib->bt_params->advanced_bt_coexist;
}

#ifdef CONFIG_IWLWIFI_DEBUG
const char *iwl_get_tx_fail_reason(u32 status);
const char *iwl_get_agg_tx_fail_reason(u16 status);
#else
static inline const char *iwl_get_tx_fail_reason(u32 status) { return ""; }
static inline const char *iwl_get_agg_tx_fail_reason(u16 status) { return ""; }
#endif


/* station management */
int iwlagn_manage_ibss_station(struct iwl_priv *priv,
			       struct ieee80211_vif *vif, bool add);
#define IWL_STA_DRIVER_ACTIVE BIT(0) /* driver entry is active */
#define IWL_STA_UCODE_ACTIVE  BIT(1) /* ucode entry is active */
#define IWL_STA_UCODE_INPROGRESS  BIT(2) /* ucode entry is in process of
					    being activated */
#define IWL_STA_LOCAL BIT(3) /* station state not directed by mac80211;
				(this is for the IBSS BSSID stations) */
#define IWL_STA_BCAST BIT(4) /* this station is the special bcast station */


void iwl_restore_stations(struct iwl_priv *priv, struct iwl_rxon_context *ctx);
void iwl_clear_ucode_stations(struct iwl_priv *priv,
			      struct iwl_rxon_context *ctx);
void iwl_dealloc_bcast_stations(struct iwl_priv *priv);
int iwl_get_free_ucode_key_offset(struct iwl_priv *priv);
int iwl_send_add_sta(struct iwl_priv *priv,
		     struct iwl_addsta_cmd *sta, u8 flags);
int iwl_add_station_common(struct iwl_priv *priv, struct iwl_rxon_context *ctx,
			   const u8 *addr, bool is_ap,
			   struct ieee80211_sta *sta, u8 *sta_id_r);
int iwl_remove_station(struct iwl_priv *priv, const u8 sta_id,
		       const u8 *addr);
void iwl_deactivate_station(struct iwl_priv *priv, const u8 sta_id,
			    const u8 *addr);
u8 iwl_prep_station(struct iwl_priv *priv, struct iwl_rxon_context *ctx,
		    const u8 *addr, bool is_ap, struct ieee80211_sta *sta);

int iwl_send_lq_cmd(struct iwl_priv *priv, struct iwl_rxon_context *ctx,
		    struct iwl_link_quality_cmd *lq, u8 flags, bool init);
void iwl_add_sta_callback(struct iwl_priv *priv, struct iwl_rx_cmd_buffer *rxb);
int iwl_sta_update_ht(struct iwl_priv *priv, struct iwl_rxon_context *ctx,
		      struct ieee80211_sta *sta);

bool iwl_is_ht40_tx_allowed(struct iwl_priv *priv,
			    struct iwl_rxon_context *ctx,
			    struct ieee80211_sta *sta);

static inline int iwl_sta_id(struct ieee80211_sta *sta)
{
	if (WARN_ON(!sta))
		return IWL_INVALID_STATION;

	return ((struct iwl_station_priv *)sta->drv_priv)->sta_id;
}

int iwlagn_alloc_bcast_station(struct iwl_priv *priv,
			       struct iwl_rxon_context *ctx);
int iwlagn_add_bssid_station(struct iwl_priv *priv, struct iwl_rxon_context *ctx,
			     const u8 *addr, u8 *sta_id_r);
int iwl_remove_default_wep_key(struct iwl_priv *priv,
			       struct iwl_rxon_context *ctx,
			       struct ieee80211_key_conf *key);
int iwl_set_default_wep_key(struct iwl_priv *priv,
			    struct iwl_rxon_context *ctx,
			    struct ieee80211_key_conf *key);
int iwl_restore_default_wep_keys(struct iwl_priv *priv,
				 struct iwl_rxon_context *ctx);
int iwl_set_dynamic_key(struct iwl_priv *priv, struct iwl_rxon_context *ctx,
			struct ieee80211_key_conf *key,
			struct ieee80211_sta *sta);
int iwl_remove_dynamic_key(struct iwl_priv *priv, struct iwl_rxon_context *ctx,
			   struct ieee80211_key_conf *key,
			   struct ieee80211_sta *sta);
void iwl_update_tkip_key(struct iwl_priv *priv,
			 struct ieee80211_vif *vif,
			 struct ieee80211_key_conf *keyconf,
			 struct ieee80211_sta *sta, u32 iv32, u16 *phase1key);
int iwl_sta_tx_modify_enable_tid(struct iwl_priv *priv, int sta_id, int tid);
int iwl_sta_rx_agg_start(struct iwl_priv *priv, struct ieee80211_sta *sta,
			 int tid, u16 ssn);
int iwl_sta_rx_agg_stop(struct iwl_priv *priv, struct ieee80211_sta *sta,
			int tid);
void iwl_sta_modify_sleep_tx_count(struct iwl_priv *priv, int sta_id, int cnt);
int iwl_update_bcast_station(struct iwl_priv *priv,
			     struct iwl_rxon_context *ctx);
int iwl_update_bcast_stations(struct iwl_priv *priv);

/* rate */
static inline u32 iwl_ant_idx_to_flags(u8 ant_idx)
{
	return BIT(ant_idx) << RATE_MCS_ANT_POS;
}

static inline u8 iwl_hw_get_rate(__le32 rate_n_flags)
{
	return le32_to_cpu(rate_n_flags) & RATE_MCS_RATE_MSK;
}

static inline __le32 iwl_hw_set_rate_n_flags(u8 rate, u32 flags)
{
	return cpu_to_le32(flags|(u32)rate);
}

int iwl_alive_start(struct iwl_priv *priv);

#ifdef CONFIG_IWLWIFI_DEBUG
void iwl_print_rx_config_cmd(struct iwl_priv *priv,
			     enum iwl_rxon_context_id ctxid);
#else
static inline void iwl_print_rx_config_cmd(struct iwl_priv *priv,
					   enum iwl_rxon_context_id ctxid)
{
}
#endif

/* status checks */

static inline int iwl_is_ready(struct iwl_priv *priv)
{
	/* The adapter is 'ready' if READY EXIT_PENDING is not set */
	return test_bit(STATUS_READY, &priv->status) &&
	       !test_bit(STATUS_EXIT_PENDING, &priv->status);
}

static inline int iwl_is_alive(struct iwl_priv *priv)
{
	return test_bit(STATUS_ALIVE, &priv->status);
}

static inline int iwl_is_rfkill(struct iwl_priv *priv)
{
	return test_bit(STATUS_RF_KILL_HW, &priv->status);
}

static inline int iwl_is_ctkill(struct iwl_priv *priv)
{
	return test_bit(STATUS_CT_KILL, &priv->status);
}

static inline int iwl_is_ready_rf(struct iwl_priv *priv)
{
	if (iwl_is_rfkill(priv))
		return 0;

	return iwl_is_ready(priv);
}

static inline void iwl_dvm_set_pmi(struct iwl_priv *priv, bool state)
{
	if (state)
		set_bit(STATUS_POWER_PMI, &priv->status);
	else
		clear_bit(STATUS_POWER_PMI, &priv->status);
	iwl_trans_set_pmi(priv->trans, state);
}

/**
 * iwl_parse_eeprom_data - parse EEPROM data and return values
 *
 * @trans: ransport we're parsing for, for debug only
 * @cfg: device configuration for parsing and overrides
 * @eeprom: the EEPROM data
 * @eeprom_size: length of the EEPROM data
 *
 * This function parses all EEPROM values we need and then
 * returns a (newly allocated) struct containing all the
 * relevant values for driver use. The struct must be freed
 * later with iwl_free_nvm_data().
 */
struct iwl_nvm_data *
iwl_parse_eeprom_data(struct iwl_trans *trans, const struct iwl_rf_cfg *cfg,
		      const u8 *eeprom, size_t eeprom_size);

int iwl_read_eeprom(struct iwl_trans *trans, u8 **eeprom, size_t *eeprom_size);

#ifdef CONFIG_IWLWIFI_DEBUGFS
void iwl_dbgfs_register(struct iwl_priv *priv, struct dentry *dbgfs_dir);
#else
static inline void iwl_dbgfs_register(struct iwl_priv *priv,
				      struct dentry *dbgfs_dir) { }
#endif /* CONFIG_IWLWIFI_DEBUGFS */

#ifdef CONFIG_IWLWIFI_DEBUG
#define IWL_DEBUG_QUIET_RFKILL(m, fmt, args...)	\
do {									\
	if (!iwl_is_rfkill((m)))					\
		IWL_ERR(m, fmt, ##args);				\
	else								\
		__iwl_err((m)->dev,					\
			  iwl_have_debug_level(IWL_DL_RADIO) ?		\
				IWL_ERR_MODE_RFKILL :			\
				IWL_ERR_MODE_TRACE_ONLY,		\
			  fmt, ##args);					\
} while (0)
#else
#define IWL_DEBUG_QUIET_RFKILL(m, fmt, args...)	\
do {									\
	if (!iwl_is_rfkill((m)))					\
		IWL_ERR(m, fmt, ##args);				\
	else								\
		__iwl_err((m)->dev, IWL_ERR_MODE_TRACE_ONLY,		\
			  fmt, ##args);					\
} while (0)
#endif				/* CONFIG_IWLWIFI_DEBUG */

#endif /* __iwl_agn_h__ */
