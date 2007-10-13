/******************************************************************************
 *
 * Copyright(c) 2003 - 2007 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#ifndef __iwl_priv_h__
#define __iwl_priv_h__

#include <linux/workqueue.h>

#ifdef CONFIG_IWLWIFI_SPECTRUM_MEASUREMENT

enum {
	MEASUREMENT_READY = (1 << 0),
	MEASUREMENT_ACTIVE = (1 << 1),
};

#endif

struct iwl_priv {

	/* ieee device used by generic ieee processing code */
	struct ieee80211_hw *hw;
	struct ieee80211_channel *ieee_channels;
	struct ieee80211_rate *ieee_rates;

	/* temporary frame storage list */
	struct list_head free_frames;
	int frames_count;

	u8 phymode;
	int alloc_rxb_skb;

	void (*rx_handlers[REPLY_MAX])(struct iwl_priv *priv,
				       struct iwl_rx_mem_buffer *rxb);

	const struct ieee80211_hw_mode *modes;

#ifdef CONFIG_IWLWIFI_SPECTRUM_MEASUREMENT
	/* spectrum measurement report caching */
	struct iwl_spectrum_notification measure_report;
	u8 measurement_status;
#endif
	/* ucode beacon time */
	u32 ucode_beacon_time;

	/* we allocate array of iwl_channel_info for NIC's valid channels.
	 *    Access via channel # using indirect index array */
	struct iwl_channel_info *channel_info;	/* channel info array */
	u8 channel_count;	/* # of channels */

	/* each calibration channel group in the EEPROM has a derived
	 * clip setting for each rate. */
	const struct iwl_clip_group clip_groups[5];

	/* thermal calibration */
	s32 temperature;	/* degrees Kelvin */
	s32 last_temperature;

	/* Scan related variables */
	unsigned long last_scan_jiffies;
	unsigned long scan_start;
	unsigned long scan_pass_start;
	unsigned long scan_start_tsf;
	int scan_bands;
	int one_direct_scan;
	u8 direct_ssid_len;
	u8 direct_ssid[IW_ESSID_MAX_SIZE];
	struct iwl_scan_cmd *scan;
	u8 only_active_channel;

	/* spinlock */
	spinlock_t lock;	/* protect general shared data */
	spinlock_t hcmd_lock;	/* protect hcmd */
	struct mutex mutex;

	/* basic pci-network driver stuff */
	struct pci_dev *pci_dev;

	/* pci hardware address support */
	void __iomem *hw_base;

	/* uCode images, save to reload in case of failure */
	struct fw_image_desc ucode_code;	/* runtime inst */
	struct fw_image_desc ucode_data;	/* runtime data original */
	struct fw_image_desc ucode_data_backup;	/* runtime data save/restore */
	struct fw_image_desc ucode_init;	/* initialization inst */
	struct fw_image_desc ucode_init_data;	/* initialization data */
	struct fw_image_desc ucode_boot;	/* bootstrap inst */


	struct iwl_rxon_time_cmd rxon_timing;

	/* We declare this const so it can only be
	 * changed via explicit cast within the
	 * routines that actually update the physical
	 * hardware */
	const struct iwl_rxon_cmd active_rxon;
	struct iwl_rxon_cmd staging_rxon;

	int error_recovering;
	struct iwl_rxon_cmd recovery_rxon;

	/* 1st responses from initialize and runtime uCode images.
	 * 4965's initialize alive response contains some calibration data. */
	struct iwl_init_alive_resp card_alive_init;
	struct iwl_alive_resp card_alive;

#ifdef LED
	/* LED related variables */
	struct iwl_activity_blink activity;
	unsigned long led_packets;
	int led_state;
#endif

	u16 active_rate;
	u16 active_rate_basic;

	u8 call_post_assoc_from_beacon;
	u8 assoc_station_added;
#if IWL == 4965
	u8 use_ant_b_for_management_frame;	/* Tx antenna selection */
	/* HT variables */
	u8 is_dup;
	u8 is_ht_enabled;
	u8 channel_width;	/* 0=20MHZ, 1=40MHZ */
	u8 current_channel_width;
	u8 valid_antenna;	/* Bit mask of antennas actually connected */
#ifdef CONFIG_IWLWIFI_SENSITIVITY
	struct iwl_sensitivity_data sensitivity_data;
	struct iwl_chain_noise_data chain_noise_data;
	u8 start_calib;
	__le16 sensitivity_tbl[HD_TABLE_SIZE];
#endif /*CONFIG_IWLWIFI_SENSITIVITY*/

#ifdef CONFIG_IWLWIFI_HT
	struct sta_ht_info current_assoc_ht;
#endif
	u8 active_rate_ht[2];
	u8 last_phy_res[100];

	/* Rate scaling data */
	struct iwl_lq_mngr lq_mngr;
#endif

	/* Rate scaling data */
	s8 data_retry_limit;
	u8 retry_rate;

	wait_queue_head_t wait_command_queue;

	int activity_timer_active;

	/* Rx and Tx DMA processing queues */
	struct iwl_rx_queue rxq;
	struct iwl_tx_queue txq[IWL_MAX_NUM_QUEUES];
#if IWL == 4965
	unsigned long txq_ctx_active_msk;
	struct iwl_kw kw;	/* keep warm address */
	u32 scd_base_addr;	/* scheduler sram base address */
#endif

	unsigned long status;
	u32 config;

	int last_rx_rssi;	/* From Rx packet statisitics */
	int last_rx_noise;	/* From beacon statistics */

	struct iwl_power_mgr power_data;

	struct iwl_notif_statistics statistics;
	unsigned long last_statistics_time;

	/* context information */
	u8 essid[IW_ESSID_MAX_SIZE];
	u8 essid_len;
	u16 rates_mask;

	u32 power_mode;
	u32 antenna;
	u8 bssid[ETH_ALEN];
	u16 rts_threshold;
	u8 mac_addr[ETH_ALEN];

	/*station table variables */
	spinlock_t sta_lock;
	int num_stations;
	struct iwl_station_entry stations[IWL_STATION_COUNT];

	/* Indication if ieee80211_ops->open has been called */
	int is_open;

	u8 mac80211_registered;
	int is_abg;

	u32 notif_missed_beacons;

	/* Rx'd packet timing information */
	u32 last_beacon_time;
	u64 last_tsf;

	/* Duplicate packet detection */
	u16 last_seq_num;
	u16 last_frag_num;
	unsigned long last_packet_time;
	struct list_head ibss_mac_hash[IWL_IBSS_MAC_HASH_SIZE];

	/* eeprom */
	struct iwl_eeprom eeprom;

	int iw_mode;

	struct sk_buff *ibss_beacon;

	/* Last Rx'd beacon timestamp */
	u32 timestamp0;
	u32 timestamp1;
	u16 beacon_int;
	struct iwl_driver_hw_info hw_setting;
	int interface_id;

	/* Current association information needed to configure the
	 * hardware */
	u16 assoc_id;
	u16 assoc_capability;
	u8 ps_mode;

#ifdef CONFIG_IWLWIFI_QOS
	struct iwl_qos_info qos_data;
#endif /*CONFIG_IWLWIFI_QOS */

	struct workqueue_struct *workqueue;

	struct work_struct up;
	struct work_struct restart;
	struct work_struct calibrated_work;
	struct work_struct scan_completed;
	struct work_struct rx_replenish;
	struct work_struct rf_kill;
	struct work_struct abort_scan;
	struct work_struct update_link_led;
	struct work_struct auth_work;
	struct work_struct report_work;
	struct work_struct request_scan;
	struct work_struct beacon_update;

	struct tasklet_struct irq_tasklet;

	struct delayed_work init_alive_start;
	struct delayed_work alive_start;
	struct delayed_work activity_timer;
	struct delayed_work thermal_periodic;
	struct delayed_work gather_stats;
	struct delayed_work scan_check;
	struct delayed_work post_associate;

#define IWL_DEFAULT_TX_POWER 0x0F
	s8 user_txpower_limit;
	s8 max_channel_txpower_limit;
	u32 cck_power_index_compensation;

#ifdef CONFIG_PM
	u32 pm_state[16];
#endif

#ifdef CONFIG_IWLWIFI_DEBUG
	/* debugging info */
	u32 framecnt_to_us;
	atomic_t restrict_refcnt;
#endif

#if IWL == 4965
	struct work_struct txpower_work;
#ifdef CONFIG_IWLWIFI_SENSITIVITY
	struct work_struct sensitivity_work;
#endif
	struct work_struct statistics_work;
	struct timer_list statistics_periodic;

#ifdef CONFIG_IWLWIFI_HT_AGG
	struct work_struct agg_work;
#endif

#endif /* 4965 */
};				/*iwl_priv */

#endif /* __iwl_priv_h__ */
