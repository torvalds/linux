/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2009 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#ifndef __iwl_core_h__
#define __iwl_core_h__

/************************
 * forward declarations *
 ************************/
struct iwl_host_cmd;
struct iwl_cmd;


#define IWLWIFI_VERSION "1.3.27k"
#define DRV_COPYRIGHT	"Copyright(c) 2003-2009 Intel Corporation"
#define DRV_AUTHOR     "<ilw@linux.intel.com>"

#define IWL_PCI_DEVICE(dev, subdev, cfg) \
	.vendor = PCI_VENDOR_ID_INTEL,  .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = (subdev), \
	.driver_data = (kernel_ulong_t)&(cfg)

#define IWL_SKU_G       0x1
#define IWL_SKU_A       0x2
#define IWL_SKU_N       0x8

#define IWL_CMD(x) case x: return #x

struct iwl_hcmd_ops {
	int (*rxon_assoc)(struct iwl_priv *priv);
	int (*commit_rxon)(struct iwl_priv *priv);
	void (*set_rxon_chain)(struct iwl_priv *priv);
	int (*set_tx_ant)(struct iwl_priv *priv, u8 valid_tx_ant);
};

struct iwl_hcmd_utils_ops {
	u16 (*get_hcmd_size)(u8 cmd_id, u16 len);
	u16 (*build_addsta_hcmd)(const struct iwl_addsta_cmd *cmd, u8 *data);
	void (*gain_computation)(struct iwl_priv *priv,
			u32 *average_noise,
			u16 min_average_noise_antennat_i,
			u32 min_average_noise,
			u8 default_chain);
	void (*chain_noise_reset)(struct iwl_priv *priv);
	void (*rts_tx_cmd_flag)(struct ieee80211_tx_info *info,
			__le32 *tx_flags);
	int  (*calc_rssi)(struct iwl_priv *priv,
			  struct iwl_rx_phy_res *rx_resp);
};

struct iwl_apm_ops {
	int (*init)(struct iwl_priv *priv);
	void (*stop)(struct iwl_priv *priv);
	void (*config)(struct iwl_priv *priv);
	int (*set_pwr_src)(struct iwl_priv *priv, enum iwl_pwr_src src);
};

struct iwl_temp_ops {
	void (*temperature)(struct iwl_priv *priv);
	void (*set_ct_kill)(struct iwl_priv *priv);
};

struct iwl_ucode_ops {
	u32 (*get_header_size)(u32);
	u32 (*get_build)(const struct iwl_ucode_header *, u32);
	u32 (*get_inst_size)(const struct iwl_ucode_header *, u32);
	u32 (*get_data_size)(const struct iwl_ucode_header *, u32);
	u32 (*get_init_size)(const struct iwl_ucode_header *, u32);
	u32 (*get_init_data_size)(const struct iwl_ucode_header *, u32);
	u32 (*get_boot_size)(const struct iwl_ucode_header *, u32);
	u8 * (*get_data)(const struct iwl_ucode_header *, u32);
};

struct iwl_lib_ops {
	/* set hw dependent parameters */
	int (*set_hw_params)(struct iwl_priv *priv);
	/* Handling TX */
	void (*txq_update_byte_cnt_tbl)(struct iwl_priv *priv,
					struct iwl_tx_queue *txq,
					u16 byte_cnt);
	void (*txq_inval_byte_cnt_tbl)(struct iwl_priv *priv,
				       struct iwl_tx_queue *txq);
	void (*txq_set_sched)(struct iwl_priv *priv, u32 mask);
	int (*txq_attach_buf_to_tfd)(struct iwl_priv *priv,
				     struct iwl_tx_queue *txq,
				     dma_addr_t addr,
				     u16 len, u8 reset, u8 pad);
	void (*txq_free_tfd)(struct iwl_priv *priv,
			     struct iwl_tx_queue *txq);
	int (*txq_init)(struct iwl_priv *priv,
			struct iwl_tx_queue *txq);
	/* aggregations */
	int (*txq_agg_enable)(struct iwl_priv *priv, int txq_id, int tx_fifo,
			      int sta_id, int tid, u16 ssn_idx);
	int (*txq_agg_disable)(struct iwl_priv *priv, u16 txq_id, u16 ssn_idx,
			       u8 tx_fifo);
	/* setup Rx handler */
	void (*rx_handler_setup)(struct iwl_priv *priv);
	/* setup deferred work */
	void (*setup_deferred_work)(struct iwl_priv *priv);
	/* cancel deferred work */
	void (*cancel_deferred_work)(struct iwl_priv *priv);
	/* alive notification after init uCode load */
	void (*init_alive_start)(struct iwl_priv *priv);
	/* alive notification */
	int (*alive_notify)(struct iwl_priv *priv);
	/* check validity of rtc data address */
	int (*is_valid_rtc_data_addr)(u32 addr);
	/* 1st ucode load */
	int (*load_ucode)(struct iwl_priv *priv);
	void (*dump_nic_event_log)(struct iwl_priv *priv);
	void (*dump_nic_error_log)(struct iwl_priv *priv);
	int (*set_channel_switch)(struct iwl_priv *priv, u16 channel);
	/* power management */
	struct iwl_apm_ops apm_ops;

	/* power */
	int (*send_tx_power) (struct iwl_priv *priv);
	void (*update_chain_flags)(struct iwl_priv *priv);
	void (*post_associate) (struct iwl_priv *priv);
	void (*config_ap) (struct iwl_priv *priv);
	irqreturn_t (*isr) (int irq, void *data);

	/* eeprom operations (as defined in iwl-eeprom.h) */
	struct iwl_eeprom_ops eeprom_ops;

	/* temperature */
	struct iwl_temp_ops temp_ops;
};

struct iwl_led_ops {
	int (*cmd)(struct iwl_priv *priv, struct iwl_led_cmd *led_cmd);
	int (*on)(struct iwl_priv *priv);
	int (*off)(struct iwl_priv *priv);
};

struct iwl_ops {
	const struct iwl_ucode_ops *ucode;
	const struct iwl_lib_ops *lib;
	const struct iwl_hcmd_ops *hcmd;
	const struct iwl_hcmd_utils_ops *utils;
	const struct iwl_led_ops *led;
};

struct iwl_mod_params {
	int sw_crypto;		/* def: 0 = using hardware encryption */
	int disable_hw_scan;	/* def: 0 = use h/w scan */
	int num_of_queues;	/* def: HW dependent */
	int disable_11n;	/* def: 0 = 11n capabilities enabled */
	int amsdu_size_8K;	/* def: 1 = enable 8K amsdu size */
	int antenna;  		/* def: 0 = both antennas (use diversity) */
	int restart_fw;		/* def: 1 = restart firmware */
};

/**
 * struct iwl_cfg
 * @fw_name_pre: Firmware filename prefix. The api version and extension
 * 	(.ucode) will be added to filename before loading from disk. The
 * 	filename is constructed as fw_name_pre<api>.ucode.
 * @ucode_api_max: Highest version of uCode API supported by driver.
 * @ucode_api_min: Lowest version of uCode API supported by driver.
 * @pa_type: used by 6000 series only to identify the type of Power Amplifier
 * @max_ll_items: max number of OTP blocks
 * @shadow_ram_support: shadow support for OTP memory
 * @led_compensation: compensate on the led on/off time per HW according
 *	to the deviation to achieve the desired led frequency.
 *	The detail algorithm is described in iwl-led.c
 * @use_rts_for_ht: use rts/cts protection for HT traffic
 * @chain_noise_num_beacons: number of beacons used to compute chain noise
 * @adv_thermal_throttle: support advance thermal throttle
 * @support_ct_kill_exit: support ct kill exit condition
 * @support_sm_ps: support spatial multiplexing power save
 * @support_wimax_coexist: support wimax/wifi co-exist
 *
 * We enable the driver to be backward compatible wrt API version. The
 * driver specifies which APIs it supports (with @ucode_api_max being the
 * highest and @ucode_api_min the lowest). Firmware will only be loaded if
 * it has a supported API version. The firmware's API version will be
 * stored in @iwl_priv, enabling the driver to make runtime changes based
 * on firmware version used.
 *
 * For example,
 * if (IWL_UCODE_API(priv->ucode_ver) >= 2) {
 * 	Driver interacts with Firmware API version >= 2.
 * } else {
 * 	Driver interacts with Firmware API version 1.
 * }
 *
 * The ideal usage of this infrastructure is to treat a new ucode API
 * release as a new hardware revision. That is, through utilizing the
 * iwl_hcmd_utils_ops etc. we accommodate different command structures
 * and flows between hardware versions (4965/5000) as well as their API
 * versions.
 *
 */
struct iwl_cfg {
	const char *name;
	const char *fw_name_pre;
	const unsigned int ucode_api_max;
	const unsigned int ucode_api_min;
	unsigned int sku;
	int eeprom_size;
	u16  eeprom_ver;
	u16  eeprom_calib_ver;
	int num_of_queues;	/* def: HW dependent */
	int num_of_ampdu_queues;/* def: HW dependent */
	const struct iwl_ops *ops;
	const struct iwl_mod_params *mod_params;
	u8   valid_tx_ant;
	u8   valid_rx_ant;

	/* for iwl_apm_init() */
	u32 pll_cfg_val;
	bool set_l0s;
	bool use_bsm;

	bool use_isr_legacy;
	enum iwl_pa_type pa_type;
	const u16 max_ll_items;
	const bool shadow_ram_support;
	const bool ht_greenfield_support;
	u16 led_compensation;
	const bool broken_powersave;
	bool use_rts_for_ht;
	int chain_noise_num_beacons;
	const bool supports_idle;
	bool adv_thermal_throttle;
	bool support_ct_kill_exit;
	bool support_sm_ps;
	const bool support_wimax_coexist;
};

/***************************
 *   L i b                 *
 ***************************/

struct ieee80211_hw *iwl_alloc_all(struct iwl_cfg *cfg,
		struct ieee80211_ops *hw_ops);
void iwl_hw_detect(struct iwl_priv *priv);
void iwl_reset_qos(struct iwl_priv *priv);
void iwl_activate_qos(struct iwl_priv *priv, u8 force);
int iwl_mac_conf_tx(struct ieee80211_hw *hw, u16 queue,
		    const struct ieee80211_tx_queue_params *params);
void iwl_set_rxon_hwcrypto(struct iwl_priv *priv, int hw_decrypt);
int iwl_check_rxon_cmd(struct iwl_priv *priv);
int iwl_full_rxon_required(struct iwl_priv *priv);
void iwl_set_rxon_chain(struct iwl_priv *priv);
int iwl_set_rxon_channel(struct iwl_priv *priv, struct ieee80211_channel *ch);
void iwl_set_rxon_ht(struct iwl_priv *priv, struct iwl_ht_config *ht_conf);
u8 iwl_is_ht40_tx_allowed(struct iwl_priv *priv,
			 struct ieee80211_sta_ht_cap *sta_ht_inf);
void iwl_set_flags_for_band(struct iwl_priv *priv, enum ieee80211_band band);
void iwl_connection_init_rx_config(struct iwl_priv *priv, int mode);
int iwl_set_decrypted_flag(struct iwl_priv *priv,
			   struct ieee80211_hdr *hdr,
			   u32 decrypt_res,
			   struct ieee80211_rx_status *stats);
void iwl_irq_handle_error(struct iwl_priv *priv);
void iwl_configure_filter(struct ieee80211_hw *hw,
			  unsigned int changed_flags,
			  unsigned int *total_flags, u64 multicast);
int iwl_hw_nic_init(struct iwl_priv *priv);
int iwl_set_hw_params(struct iwl_priv *priv);
bool iwl_is_monitor_mode(struct iwl_priv *priv);
void iwl_post_associate(struct iwl_priv *priv);
void iwl_bss_info_changed(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *bss_conf,
				     u32 changes);
int iwl_mac_beacon_update(struct ieee80211_hw *hw, struct sk_buff *skb);
int iwl_commit_rxon(struct iwl_priv *priv);
int iwl_set_mode(struct iwl_priv *priv, int mode);
int iwl_mac_add_interface(struct ieee80211_hw *hw,
				 struct ieee80211_if_init_conf *conf);
void iwl_mac_remove_interface(struct ieee80211_hw *hw,
				 struct ieee80211_if_init_conf *conf);
int iwl_mac_config(struct ieee80211_hw *hw, u32 changed);
void iwl_config_ap(struct iwl_priv *priv);
int iwl_mac_get_tx_stats(struct ieee80211_hw *hw,
			 struct ieee80211_tx_queue_stats *stats);
void iwl_mac_reset_tsf(struct ieee80211_hw *hw);
int iwl_alloc_txq_mem(struct iwl_priv *priv);
void iwl_free_txq_mem(struct iwl_priv *priv);
void iwlcore_rts_tx_cmd_flag(struct ieee80211_tx_info *info,
				__le32 *tx_flags);
int iwl_send_wimax_coex(struct iwl_priv *priv);
#ifdef CONFIG_IWLWIFI_DEBUGFS
int iwl_alloc_traffic_mem(struct iwl_priv *priv);
void iwl_free_traffic_mem(struct iwl_priv *priv);
void iwl_reset_traffic_log(struct iwl_priv *priv);
void iwl_dbg_log_tx_data_frame(struct iwl_priv *priv,
				u16 length, struct ieee80211_hdr *header);
void iwl_dbg_log_rx_data_frame(struct iwl_priv *priv,
				u16 length, struct ieee80211_hdr *header);
const char *get_mgmt_string(int cmd);
const char *get_ctrl_string(int cmd);
void iwl_clear_tx_stats(struct iwl_priv *priv);
void iwl_clear_rx_stats(struct iwl_priv *priv);
void iwl_update_stats(struct iwl_priv *priv, bool is_tx, __le16 fc,
		      u16 len);
#else
static inline int iwl_alloc_traffic_mem(struct iwl_priv *priv)
{
	return 0;
}
static inline void iwl_free_traffic_mem(struct iwl_priv *priv)
{
}
static inline void iwl_reset_traffic_log(struct iwl_priv *priv)
{
}
static inline void iwl_dbg_log_tx_data_frame(struct iwl_priv *priv,
		      u16 length, struct ieee80211_hdr *header)
{
}
static inline void iwl_dbg_log_rx_data_frame(struct iwl_priv *priv,
		      u16 length, struct ieee80211_hdr *header)
{
}
static inline void iwl_update_stats(struct iwl_priv *priv, bool is_tx,
				    __le16 fc, u16 len)
{
	struct traffic_stats	*stats;

	if (is_tx)
		stats = &priv->tx_stats;
	else
		stats = &priv->rx_stats;

	if (ieee80211_is_data(fc)) {
		/* data */
		stats->data_bytes += len;
	}
}
#endif
/*****************************************************
 * RX handlers.
 * **************************************************/
void iwl_rx_pm_sleep_notif(struct iwl_priv *priv,
			   struct iwl_rx_mem_buffer *rxb);
void iwl_rx_pm_debug_statistics_notif(struct iwl_priv *priv,
				      struct iwl_rx_mem_buffer *rxb);
void iwl_rx_reply_error(struct iwl_priv *priv,
			struct iwl_rx_mem_buffer *rxb);

/*****************************************************
* RX
******************************************************/
void iwl_rx_queue_free(struct iwl_priv *priv, struct iwl_rx_queue *rxq);
void iwl_cmd_queue_free(struct iwl_priv *priv);
int iwl_rx_queue_alloc(struct iwl_priv *priv);
void iwl_rx_handle(struct iwl_priv *priv);
int iwl_rx_queue_update_write_ptr(struct iwl_priv *priv,
				  struct iwl_rx_queue *q);
void iwl_rx_queue_reset(struct iwl_priv *priv, struct iwl_rx_queue *rxq);
void iwl_rx_replenish(struct iwl_priv *priv);
void iwl_rx_replenish_now(struct iwl_priv *priv);
int iwl_rx_init(struct iwl_priv *priv, struct iwl_rx_queue *rxq);
int iwl_rx_queue_restock(struct iwl_priv *priv);
int iwl_rx_queue_space(const struct iwl_rx_queue *q);
void iwl_rx_allocate(struct iwl_priv *priv, gfp_t priority);
void iwl_tx_cmd_complete(struct iwl_priv *priv, struct iwl_rx_mem_buffer *rxb);
int iwl_tx_queue_reclaim(struct iwl_priv *priv, int txq_id, int index);
/* Handlers */
void iwl_rx_missed_beacon_notif(struct iwl_priv *priv,
			       struct iwl_rx_mem_buffer *rxb);
void iwl_rx_statistics(struct iwl_priv *priv,
			      struct iwl_rx_mem_buffer *rxb);
void iwl_rx_csa(struct iwl_priv *priv, struct iwl_rx_mem_buffer *rxb);

/* TX helpers */

/*****************************************************
* TX
******************************************************/
int iwl_txq_ctx_reset(struct iwl_priv *priv);
void iwl_hw_txq_free_tfd(struct iwl_priv *priv, struct iwl_tx_queue *txq);
int iwl_hw_txq_attach_buf_to_tfd(struct iwl_priv *priv,
				 struct iwl_tx_queue *txq,
				 dma_addr_t addr, u16 len, u8 reset, u8 pad);
int iwl_tx_skb(struct iwl_priv *priv, struct sk_buff *skb);
void iwl_hw_txq_ctx_free(struct iwl_priv *priv);
int iwl_hw_tx_queue_init(struct iwl_priv *priv,
			 struct iwl_tx_queue *txq);
int iwl_txq_update_write_ptr(struct iwl_priv *priv, struct iwl_tx_queue *txq);
int iwl_tx_queue_init(struct iwl_priv *priv, struct iwl_tx_queue *txq,
		      int slots_num, u32 txq_id);
void iwl_tx_queue_free(struct iwl_priv *priv, int txq_id);
int iwl_tx_agg_start(struct iwl_priv *priv, const u8 *ra, u16 tid, u16 *ssn);
int iwl_tx_agg_stop(struct iwl_priv *priv , const u8 *ra, u16 tid);
int iwl_txq_check_empty(struct iwl_priv *priv, int sta_id, u8 tid, int txq_id);
/*****************************************************
 * TX power
 ****************************************************/
int iwl_set_tx_power(struct iwl_priv *priv, s8 tx_power, bool force);

/*******************************************************************************
 * Rate
 ******************************************************************************/

void iwl_hwrate_to_tx_control(struct iwl_priv *priv, u32 rate_n_flags,
			      struct ieee80211_tx_info *info);
int iwl_hwrate_to_plcp_idx(u32 rate_n_flags);
int iwl_hwrate_to_mac80211_idx(u32 rate_n_flags, enum ieee80211_band band);

u8 iwl_rate_get_lowest_plcp(struct iwl_priv *priv);

u8 iwl_toggle_tx_ant(struct iwl_priv *priv, u8 ant_idx);

static inline u32 iwl_ant_idx_to_flags(u8 ant_idx)
{
	return BIT(ant_idx) << RATE_MCS_ANT_POS;
}

static inline u8 iwl_hw_get_rate(__le32 rate_n_flags)
{
	return le32_to_cpu(rate_n_flags) & 0xFF;
}
static inline u32 iwl_hw_get_rate_n_flags(__le32 rate_n_flags)
{
	return le32_to_cpu(rate_n_flags) & 0x1FFFF;
}
static inline __le32 iwl_hw_set_rate_n_flags(u8 rate, u32 flags)
{
	return cpu_to_le32(flags|(u32)rate);
}

/*******************************************************************************
 * Scanning
 ******************************************************************************/
void iwl_init_scan_params(struct iwl_priv *priv);
int iwl_scan_cancel(struct iwl_priv *priv);
int iwl_scan_cancel_timeout(struct iwl_priv *priv, unsigned long ms);
int iwl_mac_hw_scan(struct ieee80211_hw *hw, struct cfg80211_scan_request *req);
u16 iwl_fill_probe_req(struct iwl_priv *priv, struct ieee80211_mgmt *frame,
		       const u8 *ie, int ie_len, int left);
void iwl_setup_rx_scan_handlers(struct iwl_priv *priv);
u16 iwl_get_active_dwell_time(struct iwl_priv *priv,
			      enum ieee80211_band band,
			      u8 n_probes);
u16 iwl_get_passive_dwell_time(struct iwl_priv *priv,
			       enum ieee80211_band band);
void iwl_bg_scan_check(struct work_struct *data);
void iwl_bg_abort_scan(struct work_struct *work);
void iwl_bg_scan_completed(struct work_struct *work);
void iwl_setup_scan_deferred_work(struct iwl_priv *priv);

/* For faster active scanning, scan will move to the next channel if fewer than
 * PLCP_QUIET_THRESH packets are heard on this channel within
 * ACTIVE_QUIET_TIME after sending probe request.  This shortens the dwell
 * time if it's a quiet channel (nothing responded to our probe, and there's
 * no other traffic).
 * Disable "quiet" feature by setting PLCP_QUIET_THRESH to 0. */
#define IWL_ACTIVE_QUIET_TIME       cpu_to_le16(10)  /* msec */
#define IWL_PLCP_QUIET_THRESH       cpu_to_le16(1)  /* packets */


/*******************************************************************************
 * Calibrations - implemented in iwl-calib.c
 ******************************************************************************/
int iwl_send_calib_results(struct iwl_priv *priv);
int iwl_calib_set(struct iwl_calib_result *res, const u8 *buf, int len);
void iwl_calib_free_results(struct iwl_priv *priv);

/*******************************************************************************
 * Spectrum Measureemtns in  iwl-spectrum.c
 ******************************************************************************/
#ifdef CONFIG_IWLWIFI_SPECTRUM_MEASUREMENT
void iwl_setup_spectrum_handlers(struct iwl_priv *priv);
#else
static inline void iwl_setup_spectrum_handlers(struct iwl_priv *priv) {}
#endif
/*****************************************************
 *   S e n d i n g     H o s t     C o m m a n d s   *
 *****************************************************/

const char *get_cmd_string(u8 cmd);
int __must_check iwl_send_cmd_sync(struct iwl_priv *priv,
				   struct iwl_host_cmd *cmd);
int iwl_send_cmd(struct iwl_priv *priv, struct iwl_host_cmd *cmd);
int __must_check iwl_send_cmd_pdu(struct iwl_priv *priv, u8 id,
				  u16 len, const void *data);
int iwl_send_cmd_pdu_async(struct iwl_priv *priv, u8 id, u16 len,
			   const void *data,
			   void (*callback)(struct iwl_priv *priv,
					    struct iwl_device_cmd *cmd,
					    struct iwl_rx_packet *pkt));

int iwl_enqueue_hcmd(struct iwl_priv *priv, struct iwl_host_cmd *cmd);

int iwl_send_card_state(struct iwl_priv *priv, u32 flags,
			u8 meta_flag);

/*****************************************************
 * PCI						     *
 *****************************************************/
irqreturn_t iwl_isr_legacy(int irq, void *data);
int iwl_reset_ict(struct iwl_priv *priv);
void iwl_disable_ict(struct iwl_priv *priv);
int iwl_alloc_isr_ict(struct iwl_priv *priv);
void iwl_free_isr_ict(struct iwl_priv *priv);
irqreturn_t iwl_isr_ict(int irq, void *data);

static inline u16 iwl_pcie_link_ctl(struct iwl_priv *priv)
{
	int pos;
	u16 pci_lnk_ctl;
	pos = pci_find_capability(priv->pci_dev, PCI_CAP_ID_EXP);
	pci_read_config_word(priv->pci_dev, pos + PCI_EXP_LNKCTL, &pci_lnk_ctl);
	return pci_lnk_ctl;
}
#ifdef CONFIG_PM
int iwl_pci_suspend(struct pci_dev *pdev, pm_message_t state);
int iwl_pci_resume(struct pci_dev *pdev);
#endif /* CONFIG_PM */

/*****************************************************
*  Error Handling Debugging
******************************************************/
#ifdef CONFIG_IWLWIFI_DEBUG
void iwl_dump_nic_event_log(struct iwl_priv *priv);
void iwl_dump_nic_error_log(struct iwl_priv *priv);
void iwl_print_rx_config_cmd(struct iwl_priv *priv);
#else
static inline void iwl_dump_nic_event_log(struct iwl_priv *priv)
{
}

static inline void iwl_dump_nic_error_log(struct iwl_priv *priv)
{
}

static inline void iwl_print_rx_config_cmd(struct iwl_priv *priv)
{
}
#endif

void iwl_clear_isr_stats(struct iwl_priv *priv);

/*****************************************************
*  GEOS
******************************************************/
int iwlcore_init_geos(struct iwl_priv *priv);
void iwlcore_free_geos(struct iwl_priv *priv);

/*************** DRIVER STATUS FUNCTIONS   *****/

#define STATUS_HCMD_ACTIVE	0	/* host command in progress */
#define STATUS_HCMD_SYNC_ACTIVE	1	/* sync host command in progress */
#define STATUS_INT_ENABLED	2
#define STATUS_RF_KILL_HW	3
#define STATUS_CT_KILL		4
#define STATUS_INIT		5
#define STATUS_ALIVE		6
#define STATUS_READY		7
#define STATUS_TEMPERATURE	8
#define STATUS_GEO_CONFIGURED	9
#define STATUS_EXIT_PENDING	10
#define STATUS_STATISTICS	12
#define STATUS_SCANNING		13
#define STATUS_SCAN_ABORTING	14
#define STATUS_SCAN_HW		15
#define STATUS_POWER_PMI	16
#define STATUS_FW_ERROR		17
#define STATUS_MODE_PENDING	18


static inline int iwl_is_ready(struct iwl_priv *priv)
{
	/* The adapter is 'ready' if READY and GEO_CONFIGURED bits are
	 * set but EXIT_PENDING is not */
	return test_bit(STATUS_READY, &priv->status) &&
	       test_bit(STATUS_GEO_CONFIGURED, &priv->status) &&
	       !test_bit(STATUS_EXIT_PENDING, &priv->status);
}

static inline int iwl_is_alive(struct iwl_priv *priv)
{
	return test_bit(STATUS_ALIVE, &priv->status);
}

static inline int iwl_is_init(struct iwl_priv *priv)
{
	return test_bit(STATUS_INIT, &priv->status);
}

static inline int iwl_is_rfkill_hw(struct iwl_priv *priv)
{
	return test_bit(STATUS_RF_KILL_HW, &priv->status);
}

static inline int iwl_is_rfkill(struct iwl_priv *priv)
{
	return iwl_is_rfkill_hw(priv);
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

extern void iwl_rf_kill_ct_config(struct iwl_priv *priv);
extern int iwl_send_bt_config(struct iwl_priv *priv);
extern int iwl_send_statistics_request(struct iwl_priv *priv, u8 flags);
extern int iwl_verify_ucode(struct iwl_priv *priv);
extern int iwl_send_lq_cmd(struct iwl_priv *priv,
		struct iwl_link_quality_cmd *lq, u8 flags);
extern void iwl_rx_reply_rx(struct iwl_priv *priv,
		struct iwl_rx_mem_buffer *rxb);
extern void iwl_rx_reply_rx_phy(struct iwl_priv *priv,
				    struct iwl_rx_mem_buffer *rxb);
void iwl_rx_reply_compressed_ba(struct iwl_priv *priv,
					   struct iwl_rx_mem_buffer *rxb);
void iwl_apm_stop(struct iwl_priv *priv);
int iwl_apm_stop_master(struct iwl_priv *priv);
int iwl_apm_init(struct iwl_priv *priv);

void iwl_setup_rxon_timing(struct iwl_priv *priv);
static inline int iwl_send_rxon_assoc(struct iwl_priv *priv)
{
	return priv->cfg->ops->hcmd->rxon_assoc(priv);
}
static inline int iwlcore_commit_rxon(struct iwl_priv *priv)
{
	return priv->cfg->ops->hcmd->commit_rxon(priv);
}
static inline void iwlcore_config_ap(struct iwl_priv *priv)
{
	priv->cfg->ops->lib->config_ap(priv);
}
static inline const struct ieee80211_supported_band *iwl_get_hw_mode(
			struct iwl_priv *priv, enum ieee80211_band band)
{
	return priv->hw->wiphy->bands[band];
}
#endif /* __iwl_core_h__ */
