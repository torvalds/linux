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
#ifndef __iwl_4965_h__
#define __iwl_4965_h__

struct iwl_priv;
struct sta_ht_info;

/*
 * Forward declare iwl-4965.c functions for iwl-base.c
 */
extern int iwl_eeprom_aqcuire_semaphore(struct iwl_priv *priv);
extern void iwl_eeprom_release_semaphore(struct iwl_priv *priv);

extern int iwl4965_tx_queue_update_wr_ptr(struct iwl_priv *priv,
					  struct iwl_tx_queue *txq,
					  u16 byte_cnt);
extern void iwl4965_add_station(struct iwl_priv *priv, const u8 *addr,
				int is_ap);
extern void iwl4965_set_rxon_ht(struct iwl_priv *priv,
				struct sta_ht_info *ht_info);

extern void iwl4965_set_rxon_chain(struct iwl_priv *priv);
extern int iwl4965_tx_cmd(struct iwl_priv *priv, struct iwl_cmd *out_cmd,
			  u8 sta_id, dma_addr_t txcmd_phys,
			  struct ieee80211_hdr *hdr, u8 hdr_len,
			  struct ieee80211_tx_control *ctrl, void *sta_in);
extern int iwl4965_init_hw_rates(struct iwl_priv *priv,
				 struct ieee80211_rate *rates);
extern int iwl4965_alive_notify(struct iwl_priv *priv);
extern void iwl4965_update_rate_scaling(struct iwl_priv *priv, u8 mode);
extern void iwl4965_set_ht_add_station(struct iwl_priv *priv, u8 index);

extern void iwl4965_chain_noise_reset(struct iwl_priv *priv);
extern void iwl4965_init_sensitivity(struct iwl_priv *priv, u8 flags,
				     u8 force);
extern int iwl4965_set_fat_chan_info(struct iwl_priv *priv, int phymode,
				u16 channel,
				const struct iwl_eeprom_channel *eeprom_ch,
				u8 fat_extension_channel);
extern void iwl4965_rf_kill_ct_config(struct iwl_priv *priv);

#ifdef CONFIG_IWLWIFI_HT
#ifdef CONFIG_IWLWIFI_HT_AGG
extern int iwl_mac_ht_tx_agg_start(struct ieee80211_hw *hw, u8 *da,
				   u16 tid, u16 *start_seq_num);
extern int iwl_mac_ht_rx_agg_start(struct ieee80211_hw *hw, u8 *da,
				   u16 tid, u16 start_seq_num);
extern int iwl_mac_ht_rx_agg_stop(struct ieee80211_hw *hw, u8 *da,
				  u16 tid, int generator);
extern int iwl_mac_ht_tx_agg_stop(struct ieee80211_hw *hw, u8 *da,
				  u16 tid, int generator);
extern void iwl4965_turn_off_agg(struct iwl_priv *priv, u8 tid);
#endif /* CONFIG_IWLWIFI_HT_AGG */
#endif /*CONFIG_IWLWIFI_HT */
/* Structures, enum, and defines specific to the 4965 */

#define IWL4965_KW_SIZE 0x1000	/*4k */

struct iwl_kw {
	dma_addr_t dma_addr;
	void *v_addr;
	size_t size;
};

#define TID_QUEUE_CELL_SPACING 50	/*mS */
#define TID_QUEUE_MAX_SIZE     20
#define TID_ROUND_VALUE        5	/* mS */
#define TID_MAX_LOAD_COUNT     8

#define TID_MAX_TIME_DIFF ((TID_QUEUE_MAX_SIZE - 1) * TID_QUEUE_CELL_SPACING)
#define TIME_WRAP_AROUND(x, y) (((y) > (x)) ? (y) - (x) : (0-(x)) + (y))

#define TID_ALL_ENABLED		0x7f
#define TID_ALL_SPECIFIED       0xff
#define TID_AGG_TPT_THREHOLD    0x0

#define IWL_CHANNEL_WIDTH_20MHZ   0
#define IWL_CHANNEL_WIDTH_40MHZ   1

#define IWL_MIMO_PS_STATIC        0
#define IWL_MIMO_PS_NONE          3
#define IWL_MIMO_PS_DYNAMIC       1
#define IWL_MIMO_PS_INVALID       2

#define IWL_OPERATION_MODE_AUTO     0
#define IWL_OPERATION_MODE_HT_ONLY  1
#define IWL_OPERATION_MODE_MIXED    2
#define IWL_OPERATION_MODE_20MHZ    3

#define IWL_EXT_CHANNEL_OFFSET_AUTO   0
#define IWL_EXT_CHANNEL_OFFSET_ABOVE  1
#define IWL_EXT_CHANNEL_OFFSET_       2
#define IWL_EXT_CHANNEL_OFFSET_BELOW  3
#define IWL_EXT_CHANNEL_OFFSET_MAX    4

#define NRG_NUM_PREV_STAT_L     20
#define NUM_RX_CHAINS           (3)

#define TX_POWER_IWL_ILLEGAL_VDET    -100000
#define TX_POWER_IWL_ILLEGAL_VOLTAGE -10000
#define TX_POWER_IWL_CLOSED_LOOP_MIN_POWER 18
#define TX_POWER_IWL_CLOSED_LOOP_MAX_POWER 34
#define TX_POWER_IWL_VDET_SLOPE_BELOW_NOMINAL 17
#define TX_POWER_IWL_VDET_SLOPE_ABOVE_NOMINAL 20
#define TX_POWER_IWL_NOMINAL_POWER            26
#define TX_POWER_IWL_CLOSED_LOOP_ITERATION_LIMIT 1
#define TX_POWER_IWL_VOLTAGE_CODES_PER_03V       7
#define TX_POWER_IWL_DEGREES_PER_VDET_CODE       11
#define IWL_TX_POWER_MAX_NUM_PA_MEASUREMENTS 1
#define IWL_TX_POWER_CCK_COMPENSATION_B_STEP (9)
#define IWL_TX_POWER_CCK_COMPENSATION_C_STEP (5)

struct iwl_traffic_load {
	unsigned long time_stamp;
	u32 packet_count[TID_QUEUE_MAX_SIZE];
	u8 queue_count;
	u8 head;
	u32 total;
};

#ifdef CONFIG_IWLWIFI_HT_AGG
struct iwl_agg_control {
	unsigned long next_retry;
	u32 wait_for_agg_status;
	u32 tid_retry;
	u32 requested_ba;
	u32 granted_ba;
	u8 auto_agg;
	u32 tid_traffic_load_threshold;
	u32 ba_timeout;
	struct iwl_traffic_load traffic_load[TID_MAX_LOAD_COUNT];
};
#endif				/*CONFIG_IWLWIFI_HT_AGG */

struct iwl_lq_mngr {
#ifdef CONFIG_IWLWIFI_HT_AGG
	struct iwl_agg_control agg_ctrl;
#endif
	spinlock_t lock;
	s32 max_window_size;
	s32 *expected_tpt;
	u8 *next_higher_rate;
	u8 *next_lower_rate;
	unsigned long stamp;
	unsigned long stamp_last;
	u32 flush_time;
	u32 tx_packets;
	u8 lq_ready;
};


/* Sensitivity and chain noise calibration */
#define INTERFERENCE_DATA_AVAILABLE	__constant_cpu_to_le32(1)
#define INITIALIZATION_VALUE		0xFFFF
#define CAL_NUM_OF_BEACONS		20
#define MAXIMUM_ALLOWED_PATHLOSS	15

/* Param table within SENSITIVITY_CMD */
#define HD_MIN_ENERGY_CCK_DET_INDEX                 (0)
#define HD_MIN_ENERGY_OFDM_DET_INDEX                (1)
#define HD_AUTO_CORR32_X1_TH_ADD_MIN_INDEX          (2)
#define HD_AUTO_CORR32_X1_TH_ADD_MIN_MRC_INDEX      (3)
#define HD_AUTO_CORR40_X4_TH_ADD_MIN_MRC_INDEX      (4)
#define HD_AUTO_CORR32_X4_TH_ADD_MIN_INDEX          (5)
#define HD_AUTO_CORR32_X4_TH_ADD_MIN_MRC_INDEX      (6)
#define HD_BARKER_CORR_TH_ADD_MIN_INDEX             (7)
#define HD_BARKER_CORR_TH_ADD_MIN_MRC_INDEX         (8)
#define HD_AUTO_CORR40_X4_TH_ADD_MIN_INDEX          (9)
#define HD_OFDM_ENERGY_TH_IN_INDEX                  (10)

#define SENSITIVITY_CMD_CONTROL_DEFAULT_TABLE	__constant_cpu_to_le16(0)
#define SENSITIVITY_CMD_CONTROL_WORK_TABLE	__constant_cpu_to_le16(1)

#define CHAIN_NOISE_MAX_DELTA_GAIN_CODE 3

#define MAX_FA_OFDM  50
#define MIN_FA_OFDM  5
#define MAX_FA_CCK   50
#define MIN_FA_CCK   5

#define NRG_MIN_CCK  97
#define NRG_MAX_CCK  0

#define AUTO_CORR_MIN_OFDM        85
#define AUTO_CORR_MIN_OFDM_MRC    170
#define AUTO_CORR_MIN_OFDM_X1     105
#define AUTO_CORR_MIN_OFDM_MRC_X1 220
#define AUTO_CORR_MAX_OFDM        120
#define AUTO_CORR_MAX_OFDM_MRC    210
#define AUTO_CORR_MAX_OFDM_X1     140
#define AUTO_CORR_MAX_OFDM_MRC_X1 270
#define AUTO_CORR_STEP_OFDM       1

#define AUTO_CORR_MIN_CCK      (125)
#define AUTO_CORR_MAX_CCK      (200)
#define AUTO_CORR_MIN_CCK_MRC  200
#define AUTO_CORR_MAX_CCK_MRC  400
#define AUTO_CORR_STEP_CCK     3
#define AUTO_CORR_MAX_TH_CCK   160

#define NRG_ALG                0
#define AUTO_CORR_ALG          1
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

enum iwl_false_alarm_state {
	IWL_FA_TOO_MANY = 0,
	IWL_FA_TOO_FEW = 1,
	IWL_FA_GOOD_RANGE = 2,
};

enum iwl_chain_noise_state {
	IWL_CHAIN_NOISE_ALIVE = 0,  /* must be 0 */
	IWL_CHAIN_NOISE_ACCUMULATE = 1,
	IWL_CHAIN_NOISE_CALIBRATED = 2,
};

enum iwl_sensitivity_state {
	IWL_SENS_CALIB_ALLOWED = 0,
	IWL_SENS_CALIB_NEED_REINIT = 1,
};

enum iwl_calib_enabled_state {
	IWL_CALIB_DISABLED = 0,  /* must be 0 */
	IWL_CALIB_ENABLED = 1,
};

struct statistics_general_data {
	u32 beacon_silence_rssi_a;
	u32 beacon_silence_rssi_b;
	u32 beacon_silence_rssi_c;
	u32 beacon_energy_a;
	u32 beacon_energy_b;
	u32 beacon_energy_c;
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

	u8 state;
};

/* Chain noise (differential Rx gain) calib data */
struct iwl_chain_noise_data {
	u8 state;
	u16 beacon_count;
	u32 chain_noise_a;
	u32 chain_noise_b;
	u32 chain_noise_c;
	u32 chain_signal_a;
	u32 chain_signal_b;
	u32 chain_signal_c;
	u8 disconn_array[NUM_RX_CHAINS];
	u8 delta_gain_code[NUM_RX_CHAINS];
	u8 radio_write;
};

/* IWL4965 */
#define RATE_MCS_CODE_MSK 0x7
#define RATE_MCS_MIMO_POS 3
#define RATE_MCS_MIMO_MSK 0x8
#define RATE_MCS_HT_DUP_POS 5
#define RATE_MCS_HT_DUP_MSK 0x20
#define RATE_MCS_FLAGS_POS 8
#define RATE_MCS_HT_POS 8
#define RATE_MCS_HT_MSK 0x100
#define RATE_MCS_CCK_POS 9
#define RATE_MCS_CCK_MSK 0x200
#define RATE_MCS_GF_POS 10
#define RATE_MCS_GF_MSK 0x400

#define RATE_MCS_FAT_POS 11
#define RATE_MCS_FAT_MSK 0x800
#define RATE_MCS_DUP_POS 12
#define RATE_MCS_DUP_MSK 0x1000
#define RATE_MCS_SGI_POS 13
#define RATE_MCS_SGI_MSK 0x2000

#define	EEPROM_SEM_TIMEOUT 10
#define EEPROM_SEM_RETRY_LIMIT 1000

#endif				/* __iwl_4965_h__ */
