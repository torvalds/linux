/******************************************************************************
 *
 * Copyright(c) 2003 - 2014 Intel Corporation. All rights reserved.
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
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/
/*
 * Please use this file (dev.h) for driver implementation definitions.
 * Please use commands.h for uCode API definitions.
 */

#ifndef __iwl_dev_h__
#define __iwl_dev_h__

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#include "iwl-fw.h"
#include "iwl-eeprom-parse.h"
#include "iwl-csr.h"
#include "iwl-debug.h"
#include "iwl-agn-hw.h"
#include "iwl-op-mode.h"
#include "iwl-notif-wait.h"
#include "iwl-trans.h"

#include "led.h"
#include "power.h"
#include "rs.h"
#include "tt.h"

/* CT-KILL constants */
#define CT_KILL_THRESHOLD_LEGACY   110 /* in Celsius */
#define CT_KILL_THRESHOLD	   114 /* in Celsius */
#define CT_KILL_EXIT_THRESHOLD     95  /* in Celsius */

/* Default noise level to report when noise measurement is not available.
 *   This may be because we're:
 *   1)  Not associated  no beacon statistics being sent to driver)
 *   2)  Scanning (noise measurement does not apply to associated channel)
 * Use default noise value of -127 ... this is below the range of measurable
 *   Rx dBm for all agn devices, so it can indicate "unmeasurable" to user.
 *   Also, -127 works better than 0 when averaging frames with/without
 *   noise info (e.g. averaging might be done in app); measured dBm values are
 *   always negative ... using a negative value as the default keeps all
 *   averages within an s8's (used in some apps) range of negative values. */
#define IWL_NOISE_MEAS_NOT_AVAILABLE (-127)

/*
 * RTS threshold here is total size [2347] minus 4 FCS bytes
 * Per spec:
 *   a value of 0 means RTS on all data/management packets
 *   a value > max MSDU size means no RTS
 * else RTS for data/management frames where MPDU is larger
 *   than RTS value.
 */
#define DEFAULT_RTS_THRESHOLD     2347U
#define MIN_RTS_THRESHOLD         0U
#define MAX_RTS_THRESHOLD         2347U
#define MAX_MSDU_SIZE		  2304U
#define MAX_MPDU_SIZE		  2346U
#define DEFAULT_BEACON_INTERVAL   200U
#define	DEFAULT_SHORT_RETRY_LIMIT 7U
#define	DEFAULT_LONG_RETRY_LIMIT  4U

#define IWL_NUM_SCAN_RATES         (2)


#define IEEE80211_DATA_LEN              2304
#define IEEE80211_4ADDR_LEN             30
#define IEEE80211_HLEN                  (IEEE80211_4ADDR_LEN)
#define IEEE80211_FRAME_LEN             (IEEE80211_DATA_LEN + IEEE80211_HLEN)

#define SUP_RATE_11A_MAX_NUM_CHANNELS  8
#define SUP_RATE_11B_MAX_NUM_CHANNELS  4
#define SUP_RATE_11G_MAX_NUM_CHANNELS  12

#define IWL_SUPPORTED_RATES_IE_LEN         8

#define IWL_INVALID_RATE     0xFF
#define IWL_INVALID_VALUE    -1

union iwl_ht_rate_supp {
	u16 rates;
	struct {
		u8 siso_rate;
		u8 mimo_rate;
	};
};

struct iwl_ht_config {
	bool single_chain_sufficient;
	enum ieee80211_smps_mode smps; /* current smps mode */
};

/* QoS structures */
struct iwl_qos_info {
	int qos_active;
	struct iwl_qosparam_cmd def_qos_parm;
};

/**
 * enum iwl_agg_state
 *
 * The state machine of the BA agreement establishment / tear down.
 * These states relate to a specific RA / TID.
 *
 * @IWL_AGG_OFF: aggregation is not used
 * @IWL_AGG_STARTING: aggregation are starting (between start and oper)
 * @IWL_AGG_ON: aggregation session is up
 * @IWL_EMPTYING_HW_QUEUE_ADDBA: establishing a BA session - waiting for the
 *	HW queue to be empty from packets for this RA /TID.
 * @IWL_EMPTYING_HW_QUEUE_DELBA: tearing down a BA session - waiting for the
 *	HW queue to be empty from packets for this RA /TID.
 */
enum iwl_agg_state {
	IWL_AGG_OFF = 0,
	IWL_AGG_STARTING,
	IWL_AGG_ON,
	IWL_EMPTYING_HW_QUEUE_ADDBA,
	IWL_EMPTYING_HW_QUEUE_DELBA,
};

/**
 * struct iwl_ht_agg - aggregation state machine

 * This structs holds the states for the BA agreement establishment and tear
 * down. It also holds the state during the BA session itself. This struct is
 * duplicated for each RA / TID.

 * @rate_n_flags: Rate at which Tx was attempted. Holds the data between the
 *	Tx response (REPLY_TX), and the block ack notification
 *	(REPLY_COMPRESSED_BA).
 * @state: state of the BA agreement establishment / tear down.
 * @txq_id: Tx queue used by the BA session
 * @ssn: the first packet to be sent in AGG HW queue in Tx AGG start flow, or
 *	the first packet to be sent in legacy HW queue in Tx AGG stop flow.
 *	Basically when next_reclaimed reaches ssn, we can tell mac80211 that
 *	we are ready to finish the Tx AGG stop / start flow.
 * @wait_for_ba: Expect block-ack before next Tx reply
 */
struct iwl_ht_agg {
	u32 rate_n_flags;
	enum iwl_agg_state state;
	u16 txq_id;
	u16 ssn;
	bool wait_for_ba;
};

/**
 * struct iwl_tid_data - one for each RA / TID

 * This structs holds the states for each RA / TID.

 * @seq_number: the next WiFi sequence number to use
 * @next_reclaimed: the WiFi sequence number of the next packet to be acked.
 *	This is basically (last acked packet++).
 * @agg: aggregation state machine
 */
struct iwl_tid_data {
	u16 seq_number;
	u16 next_reclaimed;
	struct iwl_ht_agg agg;
};

/*
 * Structure should be accessed with sta_lock held. When station addition
 * is in progress (IWL_STA_UCODE_INPROGRESS) it is possible to access only
 * the commands (iwl_addsta_cmd and iwl_link_quality_cmd) without sta_lock
 * held.
 */
struct iwl_station_entry {
	struct iwl_addsta_cmd sta;
	u8 used, ctxid;
	struct iwl_link_quality_cmd *lq;
};

/*
 * iwl_station_priv: Driver's private station information
 *
 * When mac80211 creates a station it reserves some space (hw->sta_data_size)
 * in the structure for use by driver. This structure is places in that
 * space.
 */
struct iwl_station_priv {
	struct iwl_rxon_context *ctx;
	struct iwl_lq_sta lq_sta;
	atomic_t pending_frames;
	bool client;
	bool asleep;
	u8 max_agg_bufsize;
	u8 sta_id;
};

/**
 * struct iwl_vif_priv - driver's private per-interface information
 *
 * When mac80211 allocates a virtual interface, it can allocate
 * space for us to put data into.
 */
struct iwl_vif_priv {
	struct iwl_rxon_context *ctx;
	u8 ibss_bssid_sta_id;
};

struct iwl_sensitivity_ranges {
	u16 min_nrg_cck;

	u16 nrg_th_cck;
	u16 nrg_th_ofdm;

	u16 auto_corr_min_ofdm;
	u16 auto_corr_min_ofdm_mrc;
	u16 auto_corr_min_ofdm_x1;
	u16 auto_corr_min_ofdm_mrc_x1;

	u16 auto_corr_max_ofdm;
	u16 auto_corr_max_ofdm_mrc;
	u16 auto_corr_max_ofdm_x1;
	u16 auto_corr_max_ofdm_mrc_x1;

	u16 auto_corr_max_cck;
	u16 auto_corr_max_cck_mrc;
	u16 auto_corr_min_cck;
	u16 auto_corr_min_cck_mrc;

	u16 barker_corr_th_min;
	u16 barker_corr_th_min_mrc;
	u16 nrg_th_cca;
};


#define KELVIN_TO_CELSIUS(x) ((x)-273)
#define CELSIUS_TO_KELVIN(x) ((x)+273)


/******************************************************************************
 *
 * Functions implemented in core module which are forward declared here
 * for use by iwl-[4-5].c
 *
 * NOTE:  The implementation of these functions are not hardware specific
 * which is why they are in the core module files.
 *
 * Naming convention --
 * iwl_         <-- Is part of iwlwifi
 * iwlXXXX_     <-- Hardware specific (implemented in iwl-XXXX.c for XXXX)
 *
 ****************************************************************************/
void iwl_update_chain_flags(struct iwl_priv *priv);
extern const u8 iwl_bcast_addr[ETH_ALEN];

#define IWL_OPERATION_MODE_AUTO     0
#define IWL_OPERATION_MODE_HT_ONLY  1
#define IWL_OPERATION_MODE_MIXED    2
#define IWL_OPERATION_MODE_20MHZ    3

#define TX_POWER_IWL_ILLEGAL_VOLTAGE -10000

/* Sensitivity and chain noise calibration */
#define INITIALIZATION_VALUE		0xFFFF
#define IWL_CAL_NUM_BEACONS		16
#define MAXIMUM_ALLOWED_PATHLOSS	15

#define CHAIN_NOISE_MAX_DELTA_GAIN_CODE 3

#define MAX_FA_OFDM  50
#define MIN_FA_OFDM  5
#define MAX_FA_CCK   50
#define MIN_FA_CCK   5

#define AUTO_CORR_STEP_OFDM       1

#define AUTO_CORR_STEP_CCK     3
#define AUTO_CORR_MAX_TH_CCK   160

#define NRG_DIFF               2
#define NRG_STEP_CCK           2
#define NRG_MARGIN             8
#define MAX_NUMBER_CCK_NO_FA 100

#define AUTO_CORR_CCK_MIN_VAL_DEF    (125)

#define CHAIN_A             0
#define CHAIN_B             1
#define CHAIN_C             2
#define CHAIN_NOISE_DELTA_GAIN_INIT_VAL 4
#define ALL_BAND_FILTER			0xFF00
#define IN_BAND_FILTER			0xFF
#define MIN_AVERAGE_NOISE_MAX_VALUE	0xFFFFFFFF

#define NRG_NUM_PREV_STAT_L     20
#define NUM_RX_CHAINS           3

enum iwlagn_false_alarm_state {
	IWL_FA_TOO_MANY = 0,
	IWL_FA_TOO_FEW = 1,
	IWL_FA_GOOD_RANGE = 2,
};

enum iwlagn_chain_noise_state {
	IWL_CHAIN_NOISE_ALIVE = 0,  /* must be 0 */
	IWL_CHAIN_NOISE_ACCUMULATE,
	IWL_CHAIN_NOISE_CALIBRATED,
	IWL_CHAIN_NOISE_DONE,
};

/* Sensitivity calib data */
struct iwl_sensitivity_data {
	u32 auto_corr_ofdm;
	u32 auto_corr_ofdm_mrc;
	u32 auto_corr_ofdm_x1;
	u32 auto_corr_ofdm_mrc_x1;
	u32 auto_corr_cck;
	u32 auto_corr_cck_mrc;

	u32 last_bad_plcp_cnt_ofdm;
	u32 last_fa_cnt_ofdm;
	u32 last_bad_plcp_cnt_cck;
	u32 last_fa_cnt_cck;

	u32 nrg_curr_state;
	u32 nrg_prev_state;
	u32 nrg_value[10];
	u8  nrg_silence_rssi[NRG_NUM_PREV_STAT_L];
	u32 nrg_silence_ref;
	u32 nrg_energy_idx;
	u32 nrg_silence_idx;
	u32 nrg_th_cck;
	s32 nrg_auto_corr_silence_diff;
	u32 num_in_cck_no_fa;
	u32 nrg_th_ofdm;

	u16 barker_corr_th_min;
	u16 barker_corr_th_min_mrc;
	u16 nrg_th_cca;
};

/* Chain noise (differential Rx gain) calib data */
struct iwl_chain_noise_data {
	u32 active_chains;
	u32 chain_noise_a;
	u32 chain_noise_b;
	u32 chain_noise_c;
	u32 chain_signal_a;
	u32 chain_signal_b;
	u32 chain_signal_c;
	u16 beacon_count;
	u8 disconn_array[NUM_RX_CHAINS];
	u8 delta_gain_code[NUM_RX_CHAINS];
	u8 radio_write;
	u8 state;
};

enum {
	MEASUREMENT_READY = (1 << 0),
	MEASUREMENT_ACTIVE = (1 << 1),
};

/* reply_tx_statistics (for _agn devices) */
struct reply_tx_error_statistics {
	u32 pp_delay;
	u32 pp_few_bytes;
	u32 pp_bt_prio;
	u32 pp_quiet_period;
	u32 pp_calc_ttak;
	u32 int_crossed_retry;
	u32 short_limit;
	u32 long_limit;
	u32 fifo_underrun;
	u32 drain_flow;
	u32 rfkill_flush;
	u32 life_expire;
	u32 dest_ps;
	u32 host_abort;
	u32 bt_retry;
	u32 sta_invalid;
	u32 frag_drop;
	u32 tid_disable;
	u32 fifo_flush;
	u32 insuff_cf_poll;
	u32 fail_hw_drop;
	u32 sta_color_mismatch;
	u32 unknown;
};

/* reply_agg_tx_statistics (for _agn devices) */
struct reply_agg_tx_error_statistics {
	u32 underrun;
	u32 bt_prio;
	u32 few_bytes;
	u32 abort;
	u32 last_sent_ttl;
	u32 last_sent_try;
	u32 last_sent_bt_kill;
	u32 scd_query;
	u32 bad_crc32;
	u32 response;
	u32 dump_tx;
	u32 delay_tx;
	u32 unknown;
};

/*
 * schedule the timer to wake up every UCODE_TRACE_PERIOD milliseconds
 * to perform continuous uCode event logging operation if enabled
 */
#define UCODE_TRACE_PERIOD (10)

/*
 * iwl_event_log: current uCode event log position
 *
 * @ucode_trace: enable/disable ucode continuous trace timer
 * @num_wraps: how many times the event buffer wraps
 * @next_entry:  the entry just before the next one that uCode would fill
 * @non_wraps_count: counter for no wrap detected when dump ucode events
 * @wraps_once_count: counter for wrap once detected when dump ucode events
 * @wraps_more_count: counter for wrap more than once detected
 *		      when dump ucode events
 */
struct iwl_event_log {
	bool ucode_trace;
	u32 num_wraps;
	u32 next_entry;
	int non_wraps_count;
	int wraps_once_count;
	int wraps_more_count;
};

#define IWL_DELAY_NEXT_FORCE_RF_RESET  (HZ*3)

/* BT Antenna Coupling Threshold (dB) */
#define IWL_BT_ANTENNA_COUPLING_THRESHOLD	(35)

/* Firmware reload counter and Timestamp */
#define IWL_MIN_RELOAD_DURATION		1000 /* 1000 ms */
#define IWL_MAX_CONTINUE_RELOAD_CNT	4


struct iwl_rf_reset {
	int reset_request_count;
	int reset_success_count;
	int reset_reject_count;
	unsigned long last_reset_jiffies;
};

enum iwl_rxon_context_id {
	IWL_RXON_CTX_BSS,
	IWL_RXON_CTX_PAN,

	NUM_IWL_RXON_CTX
};

/* extend beacon time format bit shifting  */
/*
 * for _agn devices
 * bits 31:22 - extended
 * bits 21:0  - interval
 */
#define IWLAGN_EXT_BEACON_TIME_POS	22

struct iwl_rxon_context {
	struct ieee80211_vif *vif;

	u8 mcast_queue;
	u8 ac_to_queue[IEEE80211_NUM_ACS];
	u8 ac_to_fifo[IEEE80211_NUM_ACS];

	/*
	 * We could use the vif to indicate active, but we
	 * also need it to be active during disabling when
	 * we already removed the vif for type setting.
	 */
	bool always_active, is_active;

	bool ht_need_multiple_chains;

	enum iwl_rxon_context_id ctxid;

	u32 interface_modes, exclusive_interface_modes;
	u8 unused_devtype, ap_devtype, ibss_devtype, station_devtype;

	/*
	 * We declare this const so it can only be
	 * changed via explicit cast within the
	 * routines that actually update the physical
	 * hardware.
	 */
	const struct iwl_rxon_cmd active;
	struct iwl_rxon_cmd staging;

	struct iwl_rxon_time_cmd timing;

	struct iwl_qos_info qos_data;

	u8 bcast_sta_id, ap_sta_id;

	u8 rxon_cmd, rxon_assoc_cmd, rxon_timing_cmd;
	u8 qos_cmd;
	u8 wep_key_cmd;

	struct iwl_wep_key wep_keys[WEP_KEYS_MAX];
	u8 key_mapping_keys;

	__le32 station_flags;

	int beacon_int;

	struct {
		bool non_gf_sta_present;
		u8 protection;
		bool enabled, is_40mhz;
		u8 extension_chan_offset;
	} ht;
};

enum iwl_scan_type {
	IWL_SCAN_NORMAL,
	IWL_SCAN_RADIO_RESET,
};

/**
 * struct iwl_hw_params
 *
 * Holds the module parameters
 *
 * @tx_chains_num: Number of TX chains
 * @rx_chains_num: Number of RX chains
 * @ct_kill_threshold: temperature threshold - in hw dependent unit
 * @ct_kill_exit_threshold: when to reeable the device - in hw dependent unit
 *	relevant for 1000, 6000 and up
 * @struct iwl_sensitivity_ranges: range of sensitivity values
 * @use_rts_for_aggregation: use rts/cts protection for HT traffic
 */
struct iwl_hw_params {
	u8  tx_chains_num;
	u8  rx_chains_num;
	bool use_rts_for_aggregation;
	u32 ct_kill_threshold;
	u32 ct_kill_exit_threshold;

	const struct iwl_sensitivity_ranges *sens;
};

/**
 * struct iwl_dvm_bt_params - DVM specific BT (coex) parameters
 * @advanced_bt_coexist: support advanced bt coexist
 * @bt_init_traffic_load: specify initial bt traffic load
 * @bt_prio_boost: default bt priority boost value
 * @agg_time_limit: maximum number of uSec in aggregation
 * @bt_sco_disable: uCode should not response to BT in SCO/ESCO mode
 */
struct iwl_dvm_bt_params {
	bool advanced_bt_coexist;
	u8 bt_init_traffic_load;
	u32 bt_prio_boost;
	u16 agg_time_limit;
	bool bt_sco_disable;
	bool bt_session_2;
};

/**
 * struct iwl_dvm_cfg - DVM firmware specific device configuration
 * @set_hw_params: set hardware parameters
 * @set_channel_switch: send channel switch command
 * @nic_config: apply device specific configuration
 * @temperature: read temperature
 * @adv_thermal_throttle: support advance thermal throttle
 * @support_ct_kill_exit: support ct kill exit condition
 * @plcp_delta_threshold: plcp error rate threshold used to trigger
 *	radio tuning when there is a high receiving plcp error rate
 * @chain_noise_scale: default chain noise scale used for gain computation
 * @hd_v2: v2 of enhanced sensitivity value, used for 2000 series and up
 * @no_idle_support: do not support idle mode
 * @bt_params: pointer to BT parameters
 * @need_temp_offset_calib: need to perform temperature offset calibration
 * @no_xtal_calib: some devices do not need crystal calibration data,
 *	don't send it to those
 * @temp_offset_v2: support v2 of temperature offset calibration
 * @adv_pm: advanced power management
 */
struct iwl_dvm_cfg {
	void (*set_hw_params)(struct iwl_priv *priv);
	int (*set_channel_switch)(struct iwl_priv *priv,
				  struct ieee80211_channel_switch *ch_switch);
	void (*nic_config)(struct iwl_priv *priv);
	void (*temperature)(struct iwl_priv *priv);

	const struct iwl_dvm_bt_params *bt_params;
	s32 chain_noise_scale;
	u8 plcp_delta_threshold;
	bool adv_thermal_throttle;
	bool support_ct_kill_exit;
	bool hd_v2;
	bool no_idle_support;
	bool need_temp_offset_calib;
	bool no_xtal_calib;
	bool temp_offset_v2;
	bool adv_pm;
};

struct iwl_wipan_noa_data {
	struct rcu_head rcu_head;
	u32 length;
	u8 data[];
};

/* Calibration disabling bit mask */
enum {
	IWL_CALIB_ENABLE_ALL			= 0,

	IWL_SENSITIVITY_CALIB_DISABLED		= BIT(0),
	IWL_CHAIN_NOISE_CALIB_DISABLED		= BIT(1),
	IWL_TX_POWER_CALIB_DISABLED		= BIT(2),

	IWL_CALIB_DISABLE_ALL			= 0xFFFFFFFF,
};

#define IWL_OP_MODE_GET_DVM(_iwl_op_mode) \
	((struct iwl_priv *) ((_iwl_op_mode)->op_mode_specific))

#define IWL_MAC80211_GET_DVM(_hw) \
	((struct iwl_priv *) ((struct iwl_op_mode *) \
	(_hw)->priv)->op_mode_specific)

struct iwl_priv {

	struct iwl_trans *trans;
	struct device *dev;		/* for debug prints only */
	const struct iwl_cfg *cfg;
	const struct iwl_fw *fw;
	const struct iwl_dvm_cfg *lib;
	unsigned long status;

	spinlock_t sta_lock;
	struct mutex mutex;

	unsigned long transport_queue_stop;
	bool passive_no_rx;
#define IWL_INVALID_MAC80211_QUEUE	0xff
	u8 queue_to_mac80211[IWL_MAX_HW_QUEUES];
	atomic_t queue_stop_count[IWL_MAX_HW_QUEUES];

	unsigned long agg_q_alloc[BITS_TO_LONGS(IWL_MAX_HW_QUEUES)];

	/* ieee device used by generic ieee processing code */
	struct ieee80211_hw *hw;

	struct napi_struct *napi;

	struct list_head calib_results;

	struct workqueue_struct *workqueue;

	struct iwl_hw_params hw_params;

	enum ieee80211_band band;
	u8 valid_contexts;

	void (*rx_handlers[REPLY_MAX])(struct iwl_priv *priv,
				       struct iwl_rx_cmd_buffer *rxb);

	struct iwl_notif_wait_data notif_wait;

	/* spectrum measurement report caching */
	struct iwl_spectrum_notification measure_report;
	u8 measurement_status;

	/* ucode beacon time */
	u32 ucode_beacon_time;
	int missed_beacon_threshold;

	/* track IBSS manager (last beacon) status */
	u32 ibss_manager;

	/* jiffies when last recovery from statistics was performed */
	unsigned long rx_statistics_jiffies;

	/*counters */
	u32 rx_handlers_stats[REPLY_MAX];

	/* rf reset */
	struct iwl_rf_reset rf_reset;

	/* firmware reload counter and timestamp */
	unsigned long reload_jiffies;
	int reload_count;
	bool ucode_loaded;

	u8 plcp_delta_threshold;

	/* thermal calibration */
	s32 temperature;	/* Celsius */
	s32 last_temperature;

	struct iwl_wipan_noa_data __rcu *noa_data;

	/* Scan related variables */
	unsigned long scan_start;
	unsigned long scan_start_tsf;
	void *scan_cmd;
	enum ieee80211_band scan_band;
	struct cfg80211_scan_request *scan_request;
	struct ieee80211_vif *scan_vif;
	enum iwl_scan_type scan_type;
	u8 scan_tx_ant[IEEE80211_NUM_BANDS];
	u8 mgmt_tx_ant;

	/* max number of station keys */
	u8 sta_key_max_num;

	bool new_scan_threshold_behaviour;

	bool wowlan;

	/* EEPROM MAC addresses */
	struct mac_address addresses[2];

	struct iwl_rxon_context contexts[NUM_IWL_RXON_CTX];

	__le16 switch_channel;

	u8 start_calib;
	struct iwl_sensitivity_data sensitivity_data;
	struct iwl_chain_noise_data chain_noise_data;
	__le16 sensitivity_tbl[HD_TABLE_SIZE];
	__le16 enhance_sensitivity_tbl[ENHANCE_HD_TABLE_ENTRIES];

	struct iwl_ht_config current_ht_config;

	/* Rate scaling data */
	u8 retry_rate;

	int activity_timer_active;

	struct iwl_power_mgr power_data;
	struct iwl_tt_mgmt thermal_throttle;

	/* station table variables */
	int num_stations;
	struct iwl_station_entry stations[IWLAGN_STATION_COUNT];
	unsigned long ucode_key_table;
	struct iwl_tid_data tid_data[IWLAGN_STATION_COUNT][IWL_MAX_TID_COUNT];
	atomic_t num_aux_in_flight;

	u8 mac80211_registered;

	/* Indication if ieee80211_ops->open has been called */
	u8 is_open;

	enum nl80211_iftype iw_mode;

	/* Last Rx'd beacon timestamp */
	u64 timestamp;

	struct {
		__le32 flag;
		struct statistics_general_common common;
		struct statistics_rx_non_phy rx_non_phy;
		struct statistics_rx_phy rx_ofdm;
		struct statistics_rx_ht_phy rx_ofdm_ht;
		struct statistics_rx_phy rx_cck;
		struct statistics_tx tx;
#ifdef CONFIG_IWLWIFI_DEBUGFS
		struct statistics_bt_activity bt_activity;
		__le32 num_bt_kills, accum_num_bt_kills;
#endif
		spinlock_t lock;
	} statistics;
#ifdef CONFIG_IWLWIFI_DEBUGFS
	struct {
		struct statistics_general_common common;
		struct statistics_rx_non_phy rx_non_phy;
		struct statistics_rx_phy rx_ofdm;
		struct statistics_rx_ht_phy rx_ofdm_ht;
		struct statistics_rx_phy rx_cck;
		struct statistics_tx tx;
		struct statistics_bt_activity bt_activity;
	} accum_stats, delta_stats, max_delta_stats;
#endif

	/*
	 * reporting the number of tids has AGG on. 0 means
	 * no AGGREGATION
	 */
	u8 agg_tids_count;

	struct iwl_rx_phy_res last_phy_res;
	u32 ampdu_ref;
	bool last_phy_res_valid;

	/*
	 * chain noise reset and gain commands are the
	 * two extra calibration commands follows the standard
	 * phy calibration commands
	 */
	u8 phy_calib_chain_noise_reset_cmd;
	u8 phy_calib_chain_noise_gain_cmd;

	/* counts reply_tx error */
	struct reply_tx_error_statistics reply_tx_stats;
	struct reply_agg_tx_error_statistics reply_agg_tx_stats;

	/* bt coex */
	u8 bt_enable_flag;
	u8 bt_status;
	u8 bt_traffic_load, last_bt_traffic_load;
	bool bt_ch_announce;
	bool bt_full_concurrent;
	bool bt_ant_couple_ok;
	__le32 kill_ack_mask;
	__le32 kill_cts_mask;
	__le16 bt_valid;
	bool reduced_txpower;
	u16 bt_on_thresh;
	u16 bt_duration;
	u16 dynamic_frag_thresh;
	u8 bt_ci_compliance;
	struct work_struct bt_traffic_change_work;
	bool bt_enable_pspoll;
	struct iwl_rxon_context *cur_rssi_ctx;
	bool bt_is_sco;

	struct work_struct restart;
	struct work_struct scan_completed;
	struct work_struct abort_scan;

	struct work_struct beacon_update;
	struct iwl_rxon_context *beacon_ctx;
	struct sk_buff *beacon_skb;
	void *beacon_cmd;

	struct work_struct tt_work;
	struct work_struct ct_enter;
	struct work_struct ct_exit;
	struct work_struct start_internal_scan;
	struct work_struct tx_flush;
	struct work_struct bt_full_concurrency;
	struct work_struct bt_runtime_config;

	struct delayed_work scan_check;

	/* TX Power settings */
	s8 tx_power_user_lmt;
	s8 tx_power_next;

#ifdef CONFIG_IWLWIFI_DEBUGFS
	/* debugfs */
	struct dentry *debugfs_dir;
	u32 dbgfs_sram_offset, dbgfs_sram_len;
	bool disable_ht40;
	void *wowlan_sram;
#endif /* CONFIG_IWLWIFI_DEBUGFS */

	struct iwl_nvm_data *nvm_data;
	/* eeprom blob for debugfs */
	u8 *eeprom_blob;
	size_t eeprom_blob_size;

	struct work_struct txpower_work;
	u32 calib_disabled;
	struct work_struct run_time_calib_work;
	struct timer_list statistics_periodic;
	struct timer_list ucode_trace;

	struct iwl_event_log event_log;

#ifdef CONFIG_IWLWIFI_LEDS
	struct led_classdev led;
	unsigned long blink_on, blink_off;
	bool led_registered;
#endif

	/* WoWLAN GTK rekey data */
	u8 kck[NL80211_KCK_LEN], kek[NL80211_KEK_LEN];
	__le64 replay_ctr;
	__le16 last_seq_ctl;
	bool have_rekey_data;
#ifdef CONFIG_PM_SLEEP
	struct wiphy_wowlan_support wowlan_support;
#endif

	/* device_pointers: pointers to ucode event tables */
	struct {
		u32 error_event_table;
		u32 log_event_table;
	} device_pointers;

	/* indicator of loaded ucode image */
	enum iwl_ucode_type cur_ucode;
}; /*iwl_priv */

static inline struct iwl_rxon_context *
iwl_rxon_ctx_from_vif(struct ieee80211_vif *vif)
{
	struct iwl_vif_priv *vif_priv = (void *)vif->drv_priv;

	return vif_priv->ctx;
}

#define for_each_context(priv, ctx)				\
	for (ctx = &priv->contexts[IWL_RXON_CTX_BSS];		\
	     ctx < &priv->contexts[NUM_IWL_RXON_CTX]; ctx++)	\
		if (priv->valid_contexts & BIT(ctx->ctxid))

static inline int iwl_is_associated_ctx(struct iwl_rxon_context *ctx)
{
	return (ctx->active.filter_flags & RXON_FILTER_ASSOC_MSK) ? 1 : 0;
}

static inline int iwl_is_associated(struct iwl_priv *priv,
				    enum iwl_rxon_context_id ctxid)
{
	return iwl_is_associated_ctx(&priv->contexts[ctxid]);
}

static inline int iwl_is_any_associated(struct iwl_priv *priv)
{
	struct iwl_rxon_context *ctx;
	for_each_context(priv, ctx)
		if (iwl_is_associated_ctx(ctx))
			return true;
	return false;
}

#endif				/* __iwl_dev_h__ */
