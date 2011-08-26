/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
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
 * Copyright(c) 2005 - 2011 Intel Corporation. All rights reserved.
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

#ifndef __il_core_h__
#define __il_core_h__

/************************
 * forward declarations *
 ************************/
struct il_host_cmd;
struct il_cmd;


#define IWLWIFI_VERSION "in-tree:"
#define DRV_COPYRIGHT	"Copyright(c) 2003-2011 Intel Corporation"
#define DRV_AUTHOR     "<ilw@linux.intel.com>"

#define IL_PCI_DEVICE(dev, subdev, cfg) \
	.vendor = PCI_VENDOR_ID_INTEL,  .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = (subdev), \
	.driver_data = (kernel_ulong_t)&(cfg)

#define TIME_UNIT		1024

#define IL_SKU_G       0x1
#define IL_SKU_A       0x2
#define IL_SKU_N       0x8

#define IL_CMD(x) case x: return #x

struct il_hcmd_ops {
	int (*rxon_assoc)(struct il_priv *il, struct il_rxon_context *ctx);
	int (*commit_rxon)(struct il_priv *il, struct il_rxon_context *ctx);
	void (*set_rxon_chain)(struct il_priv *il,
			       struct il_rxon_context *ctx);
};

struct il_hcmd_utils_ops {
	u16 (*get_hcmd_size)(u8 cmd_id, u16 len);
	u16 (*build_addsta_hcmd)(const struct il_addsta_cmd *cmd,
								u8 *data);
	int (*request_scan)(struct il_priv *il, struct ieee80211_vif *vif);
	void (*post_scan)(struct il_priv *il);
};

struct il_apm_ops {
	int (*init)(struct il_priv *il);
	void (*config)(struct il_priv *il);
};

struct il_debugfs_ops {
	ssize_t (*rx_stats_read)(struct file *file, char __user *user_buf,
				 size_t count, loff_t *ppos);
	ssize_t (*tx_stats_read)(struct file *file, char __user *user_buf,
				 size_t count, loff_t *ppos);
	ssize_t (*general_stats_read)(struct file *file, char __user *user_buf,
				      size_t count, loff_t *ppos);
};

struct il_temp_ops {
	void (*temperature)(struct il_priv *il);
};

struct il_lib_ops {
	/* set hw dependent parameters */
	int (*set_hw_params)(struct il_priv *il);
	/* Handling TX */
	void (*txq_update_byte_cnt_tbl)(struct il_priv *il,
					struct il_tx_queue *txq,
					u16 byte_cnt);
	int (*txq_attach_buf_to_tfd)(struct il_priv *il,
				     struct il_tx_queue *txq,
				     dma_addr_t addr,
				     u16 len, u8 reset, u8 pad);
	void (*txq_free_tfd)(struct il_priv *il,
			     struct il_tx_queue *txq);
	int (*txq_init)(struct il_priv *il,
			struct il_tx_queue *txq);
	/* setup Rx handler */
	void (*rx_handler_setup)(struct il_priv *il);
	/* alive notification after init uCode load */
	void (*init_alive_start)(struct il_priv *il);
	/* check validity of rtc data address */
	int (*is_valid_rtc_data_addr)(u32 addr);
	/* 1st ucode load */
	int (*load_ucode)(struct il_priv *il);

	void (*dump_nic_error_log)(struct il_priv *il);
	int (*dump_fh)(struct il_priv *il, char **buf, bool display);
	int (*set_channel_switch)(struct il_priv *il,
				  struct ieee80211_channel_switch *ch_switch);
	/* power management */
	struct il_apm_ops apm_ops;

	/* power */
	int (*send_tx_power) (struct il_priv *il);
	void (*update_chain_flags)(struct il_priv *il);

	/* eeprom operations (as defined in iwl-eeprom.h) */
	struct il_eeprom_ops eeprom_ops;

	/* temperature */
	struct il_temp_ops temp_ops;

	struct il_debugfs_ops debugfs_ops;

};

struct il_led_ops {
	int (*cmd)(struct il_priv *il, struct il_led_cmd *led_cmd);
};

struct il_legacy_ops {
	void (*post_associate)(struct il_priv *il);
	void (*config_ap)(struct il_priv *il);
	/* station management */
	int (*update_bcast_stations)(struct il_priv *il);
	int (*manage_ibss_station)(struct il_priv *il,
				   struct ieee80211_vif *vif, bool add);
};

struct il_ops {
	const struct il_lib_ops *lib;
	const struct il_hcmd_ops *hcmd;
	const struct il_hcmd_utils_ops *utils;
	const struct il_led_ops *led;
	const struct il_nic_ops *nic;
	const struct il_legacy_ops *legacy;
	const struct ieee80211_ops *ieee80211_ops;
};

struct il_mod_params {
	int sw_crypto;		/* def: 0 = using hardware encryption */
	int disable_hw_scan;	/* def: 0 = use h/w scan */
	int num_of_queues;	/* def: HW dependent */
	int disable_11n;	/* def: 0 = 11n capabilities enabled */
	int amsdu_size_8K;	/* def: 1 = enable 8K amsdu size */
	int antenna;  		/* def: 0 = both antennas (use diversity) */
	int restart_fw;		/* def: 1 = restart firmware */
};

/*
 * @led_compensation: compensate on the led on/off time per HW according
 *	to the deviation to achieve the desired led frequency.
 *	The detail algorithm is described in iwl-led.c
 * @chain_noise_num_beacons: number of beacons used to compute chain noise
 * @wd_timeout: TX queues watchdog timeout
 * @temperature_kelvin: temperature report by uCode in kelvin
 * @ucode_tracing: support ucode continuous tracing
 * @sensitivity_calib_by_driver: driver has the capability to perform
 *	sensitivity calibration operation
 * @chain_noise_calib_by_driver: driver has the capability to perform
 *	chain noise calibration operation
 */
struct il_base_params {
	int eeprom_size;
	int num_of_queues;	/* def: HW dependent */
	int num_of_ampdu_queues;/* def: HW dependent */
	/* for il_apm_init() */
	u32 pll_cfg_val;
	bool set_l0s;
	bool use_bsm;

	u16 led_compensation;
	int chain_noise_num_beacons;
	unsigned int wd_timeout;
	bool temperature_kelvin;
	const bool ucode_tracing;
	const bool sensitivity_calib_by_driver;
	const bool chain_noise_calib_by_driver;
};

/**
 * struct il_cfg
 * @fw_name_pre: Firmware filename prefix. The api version and extension
 *	(.ucode) will be added to filename before loading from disk. The
 *	filename is constructed as fw_name_pre<api>.ucode.
 * @ucode_api_max: Highest version of uCode API supported by driver.
 * @ucode_api_min: Lowest version of uCode API supported by driver.
 * @scan_antennas: available antenna for scan operation
 * @led_mode: 0=blinking, 1=On(RF On)/Off(RF Off)
 *
 * We enable the driver to be backward compatible wrt API version. The
 * driver specifies which APIs it supports (with @ucode_api_max being the
 * highest and @ucode_api_min the lowest). Firmware will only be loaded if
 * it has a supported API version. The firmware's API version will be
 * stored in @il_priv, enabling the driver to make runtime changes based
 * on firmware version used.
 *
 * For example,
 * if (IL_UCODE_API(il->ucode_ver) >= 2) {
 *	Driver interacts with Firmware API version >= 2.
 * } else {
 *	Driver interacts with Firmware API version 1.
 * }
 *
 * The ideal usage of this infrastructure is to treat a new ucode API
 * release as a new hardware revision. That is, through utilizing the
 * il_hcmd_utils_ops etc. we accommodate different command structures
 * and flows between hardware versions as well as their API
 * versions.
 *
 */
struct il_cfg {
	/* params specific to an individual device within a device family */
	const char *name;
	const char *fw_name_pre;
	const unsigned int ucode_api_max;
	const unsigned int ucode_api_min;
	u8   valid_tx_ant;
	u8   valid_rx_ant;
	unsigned int sku;
	u16  eeprom_ver;
	u16  eeprom_calib_ver;
	const struct il_ops *ops;
	/* module based parameters which can be set from modprobe cmd */
	const struct il_mod_params *mod_params;
	/* params not likely to change within a device family */
	struct il_base_params *base_params;
	/* params likely to change within a device family */
	u8 scan_rx_antennas[IEEE80211_NUM_BANDS];
	enum il_led_mode led_mode;
};

/***************************
 *   L i b                 *
 ***************************/

struct ieee80211_hw *il_alloc_all(struct il_cfg *cfg);
int il_mac_conf_tx(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif, u16 queue,
		    const struct ieee80211_tx_queue_params *params);
int il_mac_tx_last_beacon(struct ieee80211_hw *hw);
void il_set_rxon_hwcrypto(struct il_priv *il,
			struct il_rxon_context *ctx,
			int hw_decrypt);
int il_check_rxon_cmd(struct il_priv *il,
			struct il_rxon_context *ctx);
int il_full_rxon_required(struct il_priv *il,
			struct il_rxon_context *ctx);
int il_set_rxon_channel(struct il_priv *il,
			struct ieee80211_channel *ch,
			struct il_rxon_context *ctx);
void il_set_flags_for_band(struct il_priv *il,
			    struct il_rxon_context *ctx,
			    enum ieee80211_band band,
			    struct ieee80211_vif *vif);
u8 il_get_single_channel_number(struct il_priv *il,
				  enum ieee80211_band band);
void il_set_rxon_ht(struct il_priv *il,
			struct il_ht_config *ht_conf);
bool il_is_ht40_tx_allowed(struct il_priv *il,
			    struct il_rxon_context *ctx,
			    struct ieee80211_sta_ht_cap *ht_cap);
void il_connection_init_rx_config(struct il_priv *il,
				   struct il_rxon_context *ctx);
void il_set_rate(struct il_priv *il);
int il_set_decrypted_flag(struct il_priv *il,
			   struct ieee80211_hdr *hdr,
			   u32 decrypt_res,
			   struct ieee80211_rx_status *stats);
void il_irq_handle_error(struct il_priv *il);
int il_mac_add_interface(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif);
void il_mac_remove_interface(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif);
int il_mac_change_interface(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     enum nl80211_iftype newtype, bool newp2p);
int il_alloc_txq_mem(struct il_priv *il);
void il_txq_mem(struct il_priv *il);

#ifdef CONFIG_IWLEGACY_DEBUGFS
int il_alloc_traffic_mem(struct il_priv *il);
void il_free_traffic_mem(struct il_priv *il);
void il_reset_traffic_log(struct il_priv *il);
void il_dbg_log_tx_data_frame(struct il_priv *il,
				u16 length, struct ieee80211_hdr *header);
void il_dbg_log_rx_data_frame(struct il_priv *il,
				u16 length, struct ieee80211_hdr *header);
const char *il_get_mgmt_string(int cmd);
const char *il_get_ctrl_string(int cmd);
void il_clear_traffic_stats(struct il_priv *il);
void il_update_stats(struct il_priv *il, bool is_tx, __le16 fc,
		      u16 len);
#else
static inline int il_alloc_traffic_mem(struct il_priv *il)
{
	return 0;
}
static inline void il_free_traffic_mem(struct il_priv *il)
{
}
static inline void il_reset_traffic_log(struct il_priv *il)
{
}
static inline void il_dbg_log_tx_data_frame(struct il_priv *il,
		      u16 length, struct ieee80211_hdr *header)
{
}
static inline void il_dbg_log_rx_data_frame(struct il_priv *il,
		      u16 length, struct ieee80211_hdr *header)
{
}
static inline void il_update_stats(struct il_priv *il, bool is_tx,
				    __le16 fc, u16 len)
{
}
#endif
/*****************************************************
 * RX handlers.
 * **************************************************/
void il_rx_pm_sleep_notif(struct il_priv *il,
			   struct il_rx_buf *rxb);
void il_rx_pm_debug_stats_notif(struct il_priv *il,
				      struct il_rx_buf *rxb);
void il_rx_reply_error(struct il_priv *il,
			struct il_rx_buf *rxb);

/*****************************************************
* RX
******************************************************/
void il_cmd_queue_unmap(struct il_priv *il);
void il_cmd_queue_free(struct il_priv *il);
int il_rx_queue_alloc(struct il_priv *il);
void il_rx_queue_update_write_ptr(struct il_priv *il,
				  struct il_rx_queue *q);
int il_rx_queue_space(const struct il_rx_queue *q);
void il_tx_cmd_complete(struct il_priv *il,
				struct il_rx_buf *rxb);
/* Handlers */
void il_rx_spectrum_measure_notif(struct il_priv *il,
					  struct il_rx_buf *rxb);
void il_recover_from_stats(struct il_priv *il,
				struct il_rx_pkt *pkt);
void il_chswitch_done(struct il_priv *il, bool is_success);
void il_rx_csa(struct il_priv *il, struct il_rx_buf *rxb);

/* TX helpers */

/*****************************************************
* TX
******************************************************/
void il_txq_update_write_ptr(struct il_priv *il,
					struct il_tx_queue *txq);
int il_tx_queue_init(struct il_priv *il, struct il_tx_queue *txq,
		      int slots_num, u32 txq_id);
void il_tx_queue_reset(struct il_priv *il,
			struct il_tx_queue *txq,
			int slots_num, u32 txq_id);
void il_tx_queue_unmap(struct il_priv *il, int txq_id);
void il_tx_queue_free(struct il_priv *il, int txq_id);
void il_setup_watchdog(struct il_priv *il);
/*****************************************************
 * TX power
 ****************************************************/
int il_set_tx_power(struct il_priv *il, s8 tx_power, bool force);

/*******************************************************************************
 * Rate
 ******************************************************************************/

u8 il_get_lowest_plcp(struct il_priv *il,
			    struct il_rxon_context *ctx);

/*******************************************************************************
 * Scanning
 ******************************************************************************/
void il_init_scan_params(struct il_priv *il);
int il_scan_cancel(struct il_priv *il);
int il_scan_cancel_timeout(struct il_priv *il, unsigned long ms);
void il_force_scan_end(struct il_priv *il);
int il_mac_hw_scan(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif,
		    struct cfg80211_scan_request *req);
void il_internal_short_hw_scan(struct il_priv *il);
int il_force_reset(struct il_priv *il, bool external);
u16 il_fill_probe_req(struct il_priv *il,
			struct ieee80211_mgmt *frame,
		       const u8 *ta, const u8 *ie, int ie_len, int left);
void il_setup_rx_scan_handlers(struct il_priv *il);
u16 il_get_active_dwell_time(struct il_priv *il,
			      enum ieee80211_band band,
			      u8 n_probes);
u16 il_get_passive_dwell_time(struct il_priv *il,
			       enum ieee80211_band band,
			       struct ieee80211_vif *vif);
void il_setup_scan_deferred_work(struct il_priv *il);
void il_cancel_scan_deferred_work(struct il_priv *il);

/* For faster active scanning, scan will move to the next channel if fewer than
 * PLCP_QUIET_THRESH packets are heard on this channel within
 * ACTIVE_QUIET_TIME after sending probe request.  This shortens the dwell
 * time if it's a quiet channel (nothing responded to our probe, and there's
 * no other traffic).
 * Disable "quiet" feature by setting PLCP_QUIET_THRESH to 0. */
#define IL_ACTIVE_QUIET_TIME       cpu_to_le16(10)  /* msec */
#define IL_PLCP_QUIET_THRESH       cpu_to_le16(1)  /* packets */

#define IL_SCAN_CHECK_WATCHDOG		(HZ * 7)

/*****************************************************
 *   S e n d i n g     H o s t     C o m m a n d s   *
 *****************************************************/

const char *il_get_cmd_string(u8 cmd);
int __must_check il_send_cmd_sync(struct il_priv *il,
				   struct il_host_cmd *cmd);
int il_send_cmd(struct il_priv *il, struct il_host_cmd *cmd);
int __must_check il_send_cmd_pdu(struct il_priv *il, u8 id,
				  u16 len, const void *data);
int il_send_cmd_pdu_async(struct il_priv *il, u8 id, u16 len,
			   const void *data,
			   void (*callback)(struct il_priv *il,
					    struct il_device_cmd *cmd,
					    struct il_rx_pkt *pkt));

int il_enqueue_hcmd(struct il_priv *il, struct il_host_cmd *cmd);


/*****************************************************
 * PCI						     *
 *****************************************************/

static inline u16 il_pcie_link_ctl(struct il_priv *il)
{
	int pos;
	u16 pci_lnk_ctl;
	pos = pci_pcie_cap(il->pci_dev);
	pci_read_config_word(il->pci_dev, pos + PCI_EXP_LNKCTL, &pci_lnk_ctl);
	return pci_lnk_ctl;
}

void il_bg_watchdog(unsigned long data);
u32 il_usecs_to_beacons(struct il_priv *il,
					u32 usec, u32 beacon_interval);
__le32 il_add_beacon_time(struct il_priv *il, u32 base,
			   u32 addon, u32 beacon_interval);

#ifdef CONFIG_PM
int il_pci_suspend(struct device *device);
int il_pci_resume(struct device *device);
extern const struct dev_pm_ops il_pm_ops;

#define IL_LEGACY_PM_OPS	(&il_pm_ops)

#else /* !CONFIG_PM */

#define IL_LEGACY_PM_OPS	NULL

#endif /* !CONFIG_PM */

/*****************************************************
*  Error Handling Debugging
******************************************************/
void il4965_dump_nic_error_log(struct il_priv *il);
#ifdef CONFIG_IWLEGACY_DEBUG
void il_print_rx_config_cmd(struct il_priv *il,
			     struct il_rxon_context *ctx);
#else
static inline void il_print_rx_config_cmd(struct il_priv *il,
					   struct il_rxon_context *ctx)
{
}
#endif

void il_clear_isr_stats(struct il_priv *il);

/*****************************************************
*  GEOS
******************************************************/
int il_init_geos(struct il_priv *il);
void il_free_geos(struct il_priv *il);

/*************** DRIVER STATUS FUNCTIONS   *****/

#define STATUS_HCMD_ACTIVE	0	/* host command in progress */
/* 1 is unused (used to be STATUS_HCMD_SYNC_ACTIVE) */
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
#define STATUS_CHANNEL_SWITCH_PENDING 18

static inline int il_is_ready(struct il_priv *il)
{
	/* The adapter is 'ready' if READY and GEO_CONFIGURED bits are
	 * set but EXIT_PENDING is not */
	return test_bit(STATUS_READY, &il->status) &&
	       test_bit(STATUS_GEO_CONFIGURED, &il->status) &&
	       !test_bit(STATUS_EXIT_PENDING, &il->status);
}

static inline int il_is_alive(struct il_priv *il)
{
	return test_bit(STATUS_ALIVE, &il->status);
}

static inline int il_is_init(struct il_priv *il)
{
	return test_bit(STATUS_INIT, &il->status);
}

static inline int il_is_rfkill_hw(struct il_priv *il)
{
	return test_bit(STATUS_RF_KILL_HW, &il->status);
}

static inline int il_is_rfkill(struct il_priv *il)
{
	return il_is_rfkill_hw(il);
}

static inline int il_is_ctkill(struct il_priv *il)
{
	return test_bit(STATUS_CT_KILL, &il->status);
}

static inline int il_is_ready_rf(struct il_priv *il)
{

	if (il_is_rfkill(il))
		return 0;

	return il_is_ready(il);
}

extern void il_send_bt_config(struct il_priv *il);
extern int il_send_stats_request(struct il_priv *il,
				       u8 flags, bool clear);
void il_apm_stop(struct il_priv *il);
int il_apm_init(struct il_priv *il);

int il_send_rxon_timing(struct il_priv *il,
				struct il_rxon_context *ctx);
static inline int il_send_rxon_assoc(struct il_priv *il,
				      struct il_rxon_context *ctx)
{
	return il->cfg->ops->hcmd->rxon_assoc(il, ctx);
}
static inline int il_commit_rxon(struct il_priv *il,
				      struct il_rxon_context *ctx)
{
	return il->cfg->ops->hcmd->commit_rxon(il, ctx);
}
static inline const struct ieee80211_supported_band *il_get_hw_mode(
			struct il_priv *il, enum ieee80211_band band)
{
	return il->hw->wiphy->bands[band];
}

/* mac80211 handlers */
int il_mac_config(struct ieee80211_hw *hw, u32 changed);
void il_mac_reset_tsf(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif);
void il_mac_bss_info_changed(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *bss_conf,
				     u32 changes);
void il_tx_cmd_protection(struct il_priv *il,
				struct ieee80211_tx_info *info,
				__le16 fc, __le32 *tx_flags);

irqreturn_t il_isr(int irq, void *data);

#endif /* __il_core_h__ */
