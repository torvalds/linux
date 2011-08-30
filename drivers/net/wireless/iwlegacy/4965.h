/******************************************************************************
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
 *****************************************************************************/

#ifndef __il_4965_h__
#define __il_4965_h__

#include "iwl-fh.h"
#include "iwl-debug.h"

struct il_rx_queue;
struct il_rx_buf;
struct il_rx_pkt;
struct il_tx_queue;
struct il_rxon_context;

/* configuration for the _4965 devices */
extern struct il_cfg il4965_cfg;

extern struct il_mod_params il4965_mod_params;

extern struct ieee80211_ops il4965_hw_ops;

/* tx queue */
void il4965_free_tfds_in_queue(struct il_priv *il,
			    int sta_id, int tid, int freed);

/* RXON */
void il4965_set_rxon_chain(struct il_priv *il,
				struct il_rxon_context *ctx);

/* uCode */
int il4965_verify_ucode(struct il_priv *il);

/* lib */
void il4965_check_abort_status(struct il_priv *il,
			    u8 frame_count, u32 status);

void il4965_rx_queue_reset(struct il_priv *il, struct il_rx_queue *rxq);
int il4965_rx_init(struct il_priv *il, struct il_rx_queue *rxq);
int il4965_hw_nic_init(struct il_priv *il);
int il4965_dump_fh(struct il_priv *il, char **buf, bool display);

/* rx */
void il4965_rx_queue_restock(struct il_priv *il);
void il4965_rx_replenish(struct il_priv *il);
void il4965_rx_replenish_now(struct il_priv *il);
void il4965_rx_queue_free(struct il_priv *il, struct il_rx_queue *rxq);
int il4965_rxq_stop(struct il_priv *il);
int il4965_hwrate_to_mac80211_idx(u32 rate_n_flags, enum ieee80211_band band);
void il4965_rx_reply_rx(struct il_priv *il,
		     struct il_rx_buf *rxb);
void il4965_rx_reply_rx_phy(struct il_priv *il,
			 struct il_rx_buf *rxb);
void il4965_rx_handle(struct il_priv *il);

/* tx */
void il4965_hw_txq_free_tfd(struct il_priv *il, struct il_tx_queue *txq);
int il4965_hw_txq_attach_buf_to_tfd(struct il_priv *il,
				 struct il_tx_queue *txq,
				 dma_addr_t addr, u16 len, u8 reset, u8 pad);
int il4965_hw_tx_queue_init(struct il_priv *il,
			 struct il_tx_queue *txq);
void il4965_hwrate_to_tx_control(struct il_priv *il, u32 rate_n_flags,
			      struct ieee80211_tx_info *info);
int il4965_tx_skb(struct il_priv *il, struct sk_buff *skb);
int il4965_tx_agg_start(struct il_priv *il, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta, u16 tid, u16 *ssn);
int il4965_tx_agg_stop(struct il_priv *il, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, u16 tid);
int il4965_txq_check_empty(struct il_priv *il,
			   int sta_id, u8 tid, int txq_id);
void il4965_rx_reply_compressed_ba(struct il_priv *il,
				struct il_rx_buf *rxb);
int il4965_tx_queue_reclaim(struct il_priv *il, int txq_id, int idx);
void il4965_hw_txq_ctx_free(struct il_priv *il);
int il4965_txq_ctx_alloc(struct il_priv *il);
void il4965_txq_ctx_reset(struct il_priv *il);
void il4965_txq_ctx_stop(struct il_priv *il);
void il4965_txq_set_sched(struct il_priv *il, u32 mask);

/*
 * Acquire il->lock before calling this function !
 */
void il4965_set_wr_ptrs(struct il_priv *il, int txq_id, u32 idx);
/**
 * il4965_tx_queue_set_status - (optionally) start Tx/Cmd queue
 * @tx_fifo_id: Tx DMA/FIFO channel (range 0-7) that the queue will feed
 * @scd_retry: (1) Indicates queue will be used in aggregation mode
 *
 * NOTE:  Acquire il->lock before calling this function !
 */
void il4965_tx_queue_set_status(struct il_priv *il,
					struct il_tx_queue *txq,
					int tx_fifo_id, int scd_retry);

u8 il4965_toggle_tx_ant(struct il_priv *il, u8 ant_idx, u8 valid);

/* rx */
void il4965_rx_missed_beacon_notif(struct il_priv *il,
				struct il_rx_buf *rxb);
bool il4965_good_plcp_health(struct il_priv *il,
			  struct il_rx_pkt *pkt);
void il4965_rx_stats(struct il_priv *il,
		       struct il_rx_buf *rxb);
void il4965_reply_stats(struct il_priv *il,
			  struct il_rx_buf *rxb);

/* scan */
int il4965_request_scan(struct il_priv *il, struct ieee80211_vif *vif);

/* station mgmt */
int il4965_manage_ibss_station(struct il_priv *il,
			       struct ieee80211_vif *vif, bool add);

/* hcmd */
int il4965_send_beacon_cmd(struct il_priv *il);

#ifdef CONFIG_IWLEGACY_DEBUG
const char *il4965_get_tx_fail_reason(u32 status);
#else
static inline const char *
il4965_get_tx_fail_reason(u32 status) { return ""; }
#endif

/* station management */
int il4965_alloc_bcast_station(struct il_priv *il,
			       struct il_rxon_context *ctx);
int il4965_add_bssid_station(struct il_priv *il,
				struct il_rxon_context *ctx,
			     const u8 *addr, u8 *sta_id_r);
int il4965_remove_default_wep_key(struct il_priv *il,
			       struct il_rxon_context *ctx,
			       struct ieee80211_key_conf *key);
int il4965_set_default_wep_key(struct il_priv *il,
			    struct il_rxon_context *ctx,
			    struct ieee80211_key_conf *key);
int il4965_restore_default_wep_keys(struct il_priv *il,
				 struct il_rxon_context *ctx);
int il4965_set_dynamic_key(struct il_priv *il,
			struct il_rxon_context *ctx,
			struct ieee80211_key_conf *key, u8 sta_id);
int il4965_remove_dynamic_key(struct il_priv *il,
			struct il_rxon_context *ctx,
			struct ieee80211_key_conf *key, u8 sta_id);
void il4965_update_tkip_key(struct il_priv *il,
			 struct il_rxon_context *ctx,
			 struct ieee80211_key_conf *keyconf,
			 struct ieee80211_sta *sta, u32 iv32, u16 *phase1key);
int il4965_sta_tx_modify_enable_tid(struct il_priv *il,
			int sta_id, int tid);
int il4965_sta_rx_agg_start(struct il_priv *il, struct ieee80211_sta *sta,
			 int tid, u16 ssn);
int il4965_sta_rx_agg_stop(struct il_priv *il, struct ieee80211_sta *sta,
			int tid);
void il4965_sta_modify_sleep_tx_count(struct il_priv *il,
			int sta_id, int cnt);
int il4965_update_bcast_stations(struct il_priv *il);

/* rate */
static inline u8 il4965_hw_get_rate(__le32 rate_n_flags)
{
	return le32_to_cpu(rate_n_flags) & 0xFF;
}

static inline __le32 il4965_hw_set_rate_n_flags(u8 rate, u32 flags)
{
	return cpu_to_le32(flags|(u32)rate);
}

/* eeprom */
void il4965_eeprom_get_mac(const struct il_priv *il, u8 *mac);
int il4965_eeprom_acquire_semaphore(struct il_priv *il);
void il4965_eeprom_release_semaphore(struct il_priv *il);
int  il4965_eeprom_check_version(struct il_priv *il);

/* mac80211 handlers (for 4965) */
void il4965_mac_tx(struct ieee80211_hw *hw, struct sk_buff *skb);
int il4965_mac_start(struct ieee80211_hw *hw);
void il4965_mac_stop(struct ieee80211_hw *hw);
void il4965_configure_filter(struct ieee80211_hw *hw,
			     unsigned int changed_flags,
			     unsigned int *total_flags,
			     u64 multicast);
int il4965_mac_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
		       struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		       struct ieee80211_key_conf *key);
void il4965_mac_update_tkip_key(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_key_conf *keyconf,
				struct ieee80211_sta *sta,
				u32 iv32, u16 *phase1key);
int il4965_mac_ampdu_action(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    enum ieee80211_ampdu_mlme_action action,
			    struct ieee80211_sta *sta, u16 tid, u16 *ssn,
			    u8 buf_size);
int il4965_mac_sta_add(struct ieee80211_hw *hw,
		       struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta);
void il4965_mac_channel_switch(struct ieee80211_hw *hw,
			       struct ieee80211_channel_switch *ch_switch);

void il4965_led_enable(struct il_priv *il);


/* EEPROM */
#define IL4965_EEPROM_IMG_SIZE			1024

/*
 * uCode queue management definitions ...
 * The first queue used for block-ack aggregation is #7 (4965 only).
 * All block-ack aggregation queues should map to Tx DMA/FIFO channel 7.
 */
#define IL49_FIRST_AMPDU_QUEUE	7

/* Sizes and addresses for instruction and data memory (SRAM) in
 * 4965's embedded processor.  Driver access is via HBUS_TARG_MEM_* regs. */
#define IL49_RTC_INST_LOWER_BOUND		(0x000000)
#define IL49_RTC_INST_UPPER_BOUND		(0x018000)

#define IL49_RTC_DATA_LOWER_BOUND		(0x800000)
#define IL49_RTC_DATA_UPPER_BOUND		(0x80A000)

#define IL49_RTC_INST_SIZE  (IL49_RTC_INST_UPPER_BOUND - \
				IL49_RTC_INST_LOWER_BOUND)
#define IL49_RTC_DATA_SIZE  (IL49_RTC_DATA_UPPER_BOUND - \
				IL49_RTC_DATA_LOWER_BOUND)

#define IL49_MAX_INST_SIZE IL49_RTC_INST_SIZE
#define IL49_MAX_DATA_SIZE IL49_RTC_DATA_SIZE

/* Size of uCode instruction memory in bootstrap state machine */
#define IL49_MAX_BSM_SIZE BSM_SRAM_SIZE

static inline int il4965_hw_valid_rtc_data_addr(u32 addr)
{
	return (addr >= IL49_RTC_DATA_LOWER_BOUND &&
		addr < IL49_RTC_DATA_UPPER_BOUND);
}

/********************* START TEMPERATURE *************************************/

/**
 * 4965 temperature calculation.
 *
 * The driver must calculate the device temperature before calculating
 * a txpower setting (amplifier gain is temperature dependent).  The
 * calculation uses 4 measurements, 3 of which (R1, R2, R3) are calibration
 * values used for the life of the driver, and one of which (R4) is the
 * real-time temperature indicator.
 *
 * uCode provides all 4 values to the driver via the "initialize alive"
 * notification (see struct il4965_init_alive_resp).  After the runtime uCode
 * image loads, uCode updates the R4 value via stats notifications
 * (see STATISTICS_NOTIFICATION), which occur after each received beacon
 * when associated, or can be requested via REPLY_STATISTICS_CMD.
 *
 * NOTE:  uCode provides the R4 value as a 23-bit signed value.  Driver
 *        must sign-extend to 32 bits before applying formula below.
 *
 * Formula:
 *
 * degrees Kelvin = ((97 * 259 * (R4 - R2) / (R3 - R1)) / 100) + 8
 *
 * NOTE:  The basic formula is 259 * (R4-R2) / (R3-R1).  The 97/100 is
 * an additional correction, which should be centered around 0 degrees
 * Celsius (273 degrees Kelvin).  The 8 (3 percent of 273) compensates for
 * centering the 97/100 correction around 0 degrees K.
 *
 * Add 273 to Kelvin value to find degrees Celsius, for comparing current
 * temperature with factory-measured temperatures when calculating txpower
 * settings.
 */
#define TEMPERATURE_CALIB_KELVIN_OFFSET 8
#define TEMPERATURE_CALIB_A_VAL 259

/* Limit range of calculated temperature to be between these Kelvin values */
#define IL_TX_POWER_TEMPERATURE_MIN  (263)
#define IL_TX_POWER_TEMPERATURE_MAX  (410)

#define IL_TX_POWER_TEMPERATURE_OUT_OF_RANGE(t) \
	((t) < IL_TX_POWER_TEMPERATURE_MIN || \
	 (t) > IL_TX_POWER_TEMPERATURE_MAX)

/********************* END TEMPERATURE ***************************************/

/********************* START TXPOWER *****************************************/

/**
 * 4965 txpower calculations rely on information from three sources:
 *
 *     1) EEPROM
 *     2) "initialize" alive notification
 *     3) stats notifications
 *
 * EEPROM data consists of:
 *
 * 1)  Regulatory information (max txpower and channel usage flags) is provided
 *     separately for each channel that can possibly supported by 4965.
 *     40 MHz wide (.11n HT40) channels are listed separately from 20 MHz
 *     (legacy) channels.
 *
 *     See struct il4965_eeprom_channel for format, and struct il4965_eeprom
 *     for locations in EEPROM.
 *
 * 2)  Factory txpower calibration information is provided separately for
 *     sub-bands of contiguous channels.  2.4GHz has just one sub-band,
 *     but 5 GHz has several sub-bands.
 *
 *     In addition, per-band (2.4 and 5 Ghz) saturation txpowers are provided.
 *
 *     See struct il4965_eeprom_calib_info (and the tree of structures
 *     contained within it) for format, and struct il4965_eeprom for
 *     locations in EEPROM.
 *
 * "Initialization alive" notification (see struct il4965_init_alive_resp)
 * consists of:
 *
 * 1)  Temperature calculation parameters.
 *
 * 2)  Power supply voltage measurement.
 *
 * 3)  Tx gain compensation to balance 2 transmitters for MIMO use.
 *
 * Statistics notifications deliver:
 *
 * 1)  Current values for temperature param R4.
 */

/**
 * To calculate a txpower setting for a given desired target txpower, channel,
 * modulation bit rate, and transmitter chain (4965 has 2 transmitters to
 * support MIMO and transmit diversity), driver must do the following:
 *
 * 1)  Compare desired txpower vs. (EEPROM) regulatory limit for this channel.
 *     Do not exceed regulatory limit; reduce target txpower if necessary.
 *
 *     If setting up txpowers for MIMO rates (rate idxes 8-15, 24-31),
 *     2 transmitters will be used simultaneously; driver must reduce the
 *     regulatory limit by 3 dB (half-power) for each transmitter, so the
 *     combined total output of the 2 transmitters is within regulatory limits.
 *
 *
 * 2)  Compare target txpower vs. (EEPROM) saturation txpower *reduced by
 *     backoff for this bit rate*.  Do not exceed (saturation - backoff[rate]);
 *     reduce target txpower if necessary.
 *
 *     Backoff values below are in 1/2 dB units (equivalent to steps in
 *     txpower gain tables):
 *
 *     OFDM 6 - 36 MBit:  10 steps (5 dB)
 *     OFDM 48 MBit:      15 steps (7.5 dB)
 *     OFDM 54 MBit:      17 steps (8.5 dB)
 *     OFDM 60 MBit:      20 steps (10 dB)
 *     CCK all rates:     10 steps (5 dB)
 *
 *     Backoff values apply to saturation txpower on a per-transmitter basis;
 *     when using MIMO (2 transmitters), each transmitter uses the same
 *     saturation level provided in EEPROM, and the same backoff values;
 *     no reduction (such as with regulatory txpower limits) is required.
 *
 *     Saturation and Backoff values apply equally to 20 Mhz (legacy) channel
 *     widths and 40 Mhz (.11n HT40) channel widths; there is no separate
 *     factory measurement for ht40 channels.
 *
 *     The result of this step is the final target txpower.  The rest of
 *     the steps figure out the proper settings for the device to achieve
 *     that target txpower.
 *
 *
 * 3)  Determine (EEPROM) calibration sub band for the target channel, by
 *     comparing against first and last channels in each sub band
 *     (see struct il4965_eeprom_calib_subband_info).
 *
 *
 * 4)  Linearly interpolate (EEPROM) factory calibration measurement sets,
 *     referencing the 2 factory-measured (sample) channels within the sub band.
 *
 *     Interpolation is based on difference between target channel's frequency
 *     and the sample channels' frequencies.  Since channel numbers are based
 *     on frequency (5 MHz between each channel number), this is equivalent
 *     to interpolating based on channel number differences.
 *
 *     Note that the sample channels may or may not be the channels at the
 *     edges of the sub band.  The target channel may be "outside" of the
 *     span of the sampled channels.
 *
 *     Driver may choose the pair (for 2 Tx chains) of measurements (see
 *     struct il4965_eeprom_calib_ch_info) for which the actual measured
 *     txpower comes closest to the desired txpower.  Usually, though,
 *     the middle set of measurements is closest to the regulatory limits,
 *     and is therefore a good choice for all txpower calculations (this
 *     assumes that high accuracy is needed for maximizing legal txpower,
 *     while lower txpower configurations do not need as much accuracy).
 *
 *     Driver should interpolate both members of the chosen measurement pair,
 *     i.e. for both Tx chains (radio transmitters), unless the driver knows
 *     that only one of the chains will be used (e.g. only one tx antenna
 *     connected, but this should be unusual).  The rate scaling algorithm
 *     switches antennas to find best performance, so both Tx chains will
 *     be used (although only one at a time) even for non-MIMO transmissions.
 *
 *     Driver should interpolate factory values for temperature, gain table
 *     idx, and actual power.  The power amplifier detector values are
 *     not used by the driver.
 *
 *     Sanity check:  If the target channel happens to be one of the sample
 *     channels, the results should agree with the sample channel's
 *     measurements!
 *
 *
 * 5)  Find difference between desired txpower and (interpolated)
 *     factory-measured txpower.  Using (interpolated) factory gain table idx
 *     (shown elsewhere) as a starting point, adjust this idx lower to
 *     increase txpower, or higher to decrease txpower, until the target
 *     txpower is reached.  Each step in the gain table is 1/2 dB.
 *
 *     For example, if factory measured txpower is 16 dBm, and target txpower
 *     is 13 dBm, add 6 steps to the factory gain idx to reduce txpower
 *     by 3 dB.
 *
 *
 * 6)  Find difference between current device temperature and (interpolated)
 *     factory-measured temperature for sub-band.  Factory values are in
 *     degrees Celsius.  To calculate current temperature, see comments for
 *     "4965 temperature calculation".
 *
 *     If current temperature is higher than factory temperature, driver must
 *     increase gain (lower gain table idx), and vice verse.
 *
 *     Temperature affects gain differently for different channels:
 *
 *     2.4 GHz all channels:  3.5 degrees per half-dB step
 *     5 GHz channels 34-43:  4.5 degrees per half-dB step
 *     5 GHz channels >= 44:  4.0 degrees per half-dB step
 *
 *     NOTE:  Temperature can increase rapidly when transmitting, especially
 *            with heavy traffic at high txpowers.  Driver should update
 *            temperature calculations often under these conditions to
 *            maintain strong txpower in the face of rising temperature.
 *
 *
 * 7)  Find difference between current power supply voltage indicator
 *     (from "initialize alive") and factory-measured power supply voltage
 *     indicator (EEPROM).
 *
 *     If the current voltage is higher (indicator is lower) than factory
 *     voltage, gain should be reduced (gain table idx increased) by:
 *
 *     (eeprom - current) / 7
 *
 *     If the current voltage is lower (indicator is higher) than factory
 *     voltage, gain should be increased (gain table idx decreased) by:
 *
 *     2 * (current - eeprom) / 7
 *
 *     If number of idx steps in either direction turns out to be > 2,
 *     something is wrong ... just use 0.
 *
 *     NOTE:  Voltage compensation is independent of band/channel.
 *
 *     NOTE:  "Initialize" uCode measures current voltage, which is assumed
 *            to be constant after this initial measurement.  Voltage
 *            compensation for txpower (number of steps in gain table)
 *            may be calculated once and used until the next uCode bootload.
 *
 *
 * 8)  If setting up txpowers for MIMO rates (rate idxes 8-15, 24-31),
 *     adjust txpower for each transmitter chain, so txpower is balanced
 *     between the two chains.  There are 5 pairs of tx_atten[group][chain]
 *     values in "initialize alive", one pair for each of 5 channel ranges:
 *
 *     Group 0:  5 GHz channel 34-43
 *     Group 1:  5 GHz channel 44-70
 *     Group 2:  5 GHz channel 71-124
 *     Group 3:  5 GHz channel 125-200
 *     Group 4:  2.4 GHz all channels
 *
 *     Add the tx_atten[group][chain] value to the idx for the target chain.
 *     The values are signed, but are in pairs of 0 and a non-negative number,
 *     so as to reduce gain (if necessary) of the "hotter" channel.  This
 *     avoids any need to double-check for regulatory compliance after
 *     this step.
 *
 *
 * 9)  If setting up for a CCK rate, lower the gain by adding a CCK compensation
 *     value to the idx:
 *
 *     Hardware rev B:  9 steps (4.5 dB)
 *     Hardware rev C:  5 steps (2.5 dB)
 *
 *     Hardware rev for 4965 can be determined by reading CSR_HW_REV_WA_REG,
 *     bits [3:2], 1 = B, 2 = C.
 *
 *     NOTE:  This compensation is in addition to any saturation backoff that
 *            might have been applied in an earlier step.
 *
 *
 * 10) Select the gain table, based on band (2.4 vs 5 GHz).
 *
 *     Limit the adjusted idx to stay within the table!
 *
 *
 * 11) Read gain table entries for DSP and radio gain, place into appropriate
 *     location(s) in command (struct il4965_txpowertable_cmd).
 */

/**
 * When MIMO is used (2 transmitters operating simultaneously), driver should
 * limit each transmitter to deliver a max of 3 dB below the regulatory limit
 * for the device.  That is, use half power for each transmitter, so total
 * txpower is within regulatory limits.
 *
 * The value "6" represents number of steps in gain table to reduce power 3 dB.
 * Each step is 1/2 dB.
 */
#define IL_TX_POWER_MIMO_REGULATORY_COMPENSATION (6)

/**
 * CCK gain compensation.
 *
 * When calculating txpowers for CCK, after making sure that the target power
 * is within regulatory and saturation limits, driver must additionally
 * back off gain by adding these values to the gain table idx.
 *
 * Hardware rev for 4965 can be determined by reading CSR_HW_REV_WA_REG,
 * bits [3:2], 1 = B, 2 = C.
 */
#define IL_TX_POWER_CCK_COMPENSATION_B_STEP (9)
#define IL_TX_POWER_CCK_COMPENSATION_C_STEP (5)

/*
 * 4965 power supply voltage compensation for txpower
 */
#define TX_POWER_IL_VOLTAGE_CODES_PER_03V   (7)

/**
 * Gain tables.
 *
 * The following tables contain pair of values for setting txpower, i.e.
 * gain settings for the output of the device's digital signal processor (DSP),
 * and for the analog gain structure of the transmitter.
 *
 * Each entry in the gain tables represents a step of 1/2 dB.  Note that these
 * are *relative* steps, not indications of absolute output power.  Output
 * power varies with temperature, voltage, and channel frequency, and also
 * requires consideration of average power (to satisfy regulatory constraints),
 * and peak power (to avoid distortion of the output signal).
 *
 * Each entry contains two values:
 * 1)  DSP gain (or sometimes called DSP attenuation).  This is a fine-grained
 *     linear value that multiplies the output of the digital signal processor,
 *     before being sent to the analog radio.
 * 2)  Radio gain.  This sets the analog gain of the radio Tx path.
 *     It is a coarser setting, and behaves in a logarithmic (dB) fashion.
 *
 * EEPROM contains factory calibration data for txpower.  This maps actual
 * measured txpower levels to gain settings in the "well known" tables
 * below ("well-known" means here that both factory calibration *and* the
 * driver work with the same table).
 *
 * There are separate tables for 2.4 GHz and 5 GHz bands.  The 5 GHz table
 * has an extension (into negative idxes), in case the driver needs to
 * boost power setting for high device temperatures (higher than would be
 * present during factory calibration).  A 5 Ghz EEPROM idx of "40"
 * corresponds to the 49th entry in the table used by the driver.
 */
#define MIN_TX_GAIN_IDX		(0)  /* highest gain, lowest idx, 2.4 */
#define MIN_TX_GAIN_IDX_52GHZ_EXT	(-9) /* highest gain, lowest idx, 5 */

/**
 * 2.4 GHz gain table
 *
 * Index    Dsp gain   Radio gain
 *   0        110         0x3f      (highest gain)
 *   1        104         0x3f
 *   2         98         0x3f
 *   3        110         0x3e
 *   4        104         0x3e
 *   5         98         0x3e
 *   6        110         0x3d
 *   7        104         0x3d
 *   8         98         0x3d
 *   9        110         0x3c
 *  10        104         0x3c
 *  11         98         0x3c
 *  12        110         0x3b
 *  13        104         0x3b
 *  14         98         0x3b
 *  15        110         0x3a
 *  16        104         0x3a
 *  17         98         0x3a
 *  18        110         0x39
 *  19        104         0x39
 *  20         98         0x39
 *  21        110         0x38
 *  22        104         0x38
 *  23         98         0x38
 *  24        110         0x37
 *  25        104         0x37
 *  26         98         0x37
 *  27        110         0x36
 *  28        104         0x36
 *  29         98         0x36
 *  30        110         0x35
 *  31        104         0x35
 *  32         98         0x35
 *  33        110         0x34
 *  34        104         0x34
 *  35         98         0x34
 *  36        110         0x33
 *  37        104         0x33
 *  38         98         0x33
 *  39        110         0x32
 *  40        104         0x32
 *  41         98         0x32
 *  42        110         0x31
 *  43        104         0x31
 *  44         98         0x31
 *  45        110         0x30
 *  46        104         0x30
 *  47         98         0x30
 *  48        110          0x6
 *  49        104          0x6
 *  50         98          0x6
 *  51        110          0x5
 *  52        104          0x5
 *  53         98          0x5
 *  54        110          0x4
 *  55        104          0x4
 *  56         98          0x4
 *  57        110          0x3
 *  58        104          0x3
 *  59         98          0x3
 *  60        110          0x2
 *  61        104          0x2
 *  62         98          0x2
 *  63        110          0x1
 *  64        104          0x1
 *  65         98          0x1
 *  66        110          0x0
 *  67        104          0x0
 *  68         98          0x0
 *  69         97            0
 *  70         96            0
 *  71         95            0
 *  72         94            0
 *  73         93            0
 *  74         92            0
 *  75         91            0
 *  76         90            0
 *  77         89            0
 *  78         88            0
 *  79         87            0
 *  80         86            0
 *  81         85            0
 *  82         84            0
 *  83         83            0
 *  84         82            0
 *  85         81            0
 *  86         80            0
 *  87         79            0
 *  88         78            0
 *  89         77            0
 *  90         76            0
 *  91         75            0
 *  92         74            0
 *  93         73            0
 *  94         72            0
 *  95         71            0
 *  96         70            0
 *  97         69            0
 *  98         68            0
 */

/**
 * 5 GHz gain table
 *
 * Index    Dsp gain   Radio gain
 *  -9 	      123         0x3F      (highest gain)
 *  -8 	      117         0x3F
 *  -7        110         0x3F
 *  -6        104         0x3F
 *  -5         98         0x3F
 *  -4        110         0x3E
 *  -3        104         0x3E
 *  -2         98         0x3E
 *  -1        110         0x3D
 *   0        104         0x3D
 *   1         98         0x3D
 *   2        110         0x3C
 *   3        104         0x3C
 *   4         98         0x3C
 *   5        110         0x3B
 *   6        104         0x3B
 *   7         98         0x3B
 *   8        110         0x3A
 *   9        104         0x3A
 *  10         98         0x3A
 *  11        110         0x39
 *  12        104         0x39
 *  13         98         0x39
 *  14        110         0x38
 *  15        104         0x38
 *  16         98         0x38
 *  17        110         0x37
 *  18        104         0x37
 *  19         98         0x37
 *  20        110         0x36
 *  21        104         0x36
 *  22         98         0x36
 *  23        110         0x35
 *  24        104         0x35
 *  25         98         0x35
 *  26        110         0x34
 *  27        104         0x34
 *  28         98         0x34
 *  29        110         0x33
 *  30        104         0x33
 *  31         98         0x33
 *  32        110         0x32
 *  33        104         0x32
 *  34         98         0x32
 *  35        110         0x31
 *  36        104         0x31
 *  37         98         0x31
 *  38        110         0x30
 *  39        104         0x30
 *  40         98         0x30
 *  41        110         0x25
 *  42        104         0x25
 *  43         98         0x25
 *  44        110         0x24
 *  45        104         0x24
 *  46         98         0x24
 *  47        110         0x23
 *  48        104         0x23
 *  49         98         0x23
 *  50        110         0x22
 *  51        104         0x18
 *  52         98         0x18
 *  53        110         0x17
 *  54        104         0x17
 *  55         98         0x17
 *  56        110         0x16
 *  57        104         0x16
 *  58         98         0x16
 *  59        110         0x15
 *  60        104         0x15
 *  61         98         0x15
 *  62        110         0x14
 *  63        104         0x14
 *  64         98         0x14
 *  65        110         0x13
 *  66        104         0x13
 *  67         98         0x13
 *  68        110         0x12
 *  69        104         0x08
 *  70         98         0x08
 *  71        110         0x07
 *  72        104         0x07
 *  73         98         0x07
 *  74        110         0x06
 *  75        104         0x06
 *  76         98         0x06
 *  77        110         0x05
 *  78        104         0x05
 *  79         98         0x05
 *  80        110         0x04
 *  81        104         0x04
 *  82         98         0x04
 *  83        110         0x03
 *  84        104         0x03
 *  85         98         0x03
 *  86        110         0x02
 *  87        104         0x02
 *  88         98         0x02
 *  89        110         0x01
 *  90        104         0x01
 *  91         98         0x01
 *  92        110         0x00
 *  93        104         0x00
 *  94         98         0x00
 *  95         93         0x00
 *  96         88         0x00
 *  97         83         0x00
 *  98         78         0x00
 */


/**
 * Sanity checks and default values for EEPROM regulatory levels.
 * If EEPROM values fall outside MIN/MAX range, use default values.
 *
 * Regulatory limits refer to the maximum average txpower allowed by
 * regulatory agencies in the geographies in which the device is meant
 * to be operated.  These limits are SKU-specific (i.e. geography-specific),
 * and channel-specific; each channel has an individual regulatory limit
 * listed in the EEPROM.
 *
 * Units are in half-dBm (i.e. "34" means 17 dBm).
 */
#define IL_TX_POWER_DEFAULT_REGULATORY_24   (34)
#define IL_TX_POWER_DEFAULT_REGULATORY_52   (34)
#define IL_TX_POWER_REGULATORY_MIN          (0)
#define IL_TX_POWER_REGULATORY_MAX          (34)

/**
 * Sanity checks and default values for EEPROM saturation levels.
 * If EEPROM values fall outside MIN/MAX range, use default values.
 *
 * Saturation is the highest level that the output power amplifier can produce
 * without significant clipping distortion.  This is a "peak" power level.
 * Different types of modulation (i.e. various "rates", and OFDM vs. CCK)
 * require differing amounts of backoff, relative to their average power output,
 * in order to avoid clipping distortion.
 *
 * Driver must make sure that it is violating neither the saturation limit,
 * nor the regulatory limit, when calculating Tx power settings for various
 * rates.
 *
 * Units are in half-dBm (i.e. "38" means 19 dBm).
 */
#define IL_TX_POWER_DEFAULT_SATURATION_24   (38)
#define IL_TX_POWER_DEFAULT_SATURATION_52   (38)
#define IL_TX_POWER_SATURATION_MIN          (20)
#define IL_TX_POWER_SATURATION_MAX          (50)

/**
 * Channel groups used for Tx Attenuation calibration (MIMO tx channel balance)
 * and thermal Txpower calibration.
 *
 * When calculating txpower, driver must compensate for current device
 * temperature; higher temperature requires higher gain.  Driver must calculate
 * current temperature (see "4965 temperature calculation"), then compare vs.
 * factory calibration temperature in EEPROM; if current temperature is higher
 * than factory temperature, driver must *increase* gain by proportions shown
 * in table below.  If current temperature is lower than factory, driver must
 * *decrease* gain.
 *
 * Different frequency ranges require different compensation, as shown below.
 */
/* Group 0, 5.2 GHz ch 34-43:  4.5 degrees per 1/2 dB. */
#define CALIB_IL_TX_ATTEN_GR1_FCH 34
#define CALIB_IL_TX_ATTEN_GR1_LCH 43

/* Group 1, 5.3 GHz ch 44-70:  4.0 degrees per 1/2 dB. */
#define CALIB_IL_TX_ATTEN_GR2_FCH 44
#define CALIB_IL_TX_ATTEN_GR2_LCH 70

/* Group 2, 5.5 GHz ch 71-124:  4.0 degrees per 1/2 dB. */
#define CALIB_IL_TX_ATTEN_GR3_FCH 71
#define CALIB_IL_TX_ATTEN_GR3_LCH 124

/* Group 3, 5.7 GHz ch 125-200:  4.0 degrees per 1/2 dB. */
#define CALIB_IL_TX_ATTEN_GR4_FCH 125
#define CALIB_IL_TX_ATTEN_GR4_LCH 200

/* Group 4, 2.4 GHz all channels:  3.5 degrees per 1/2 dB. */
#define CALIB_IL_TX_ATTEN_GR5_FCH 1
#define CALIB_IL_TX_ATTEN_GR5_LCH 20

enum {
	CALIB_CH_GROUP_1 = 0,
	CALIB_CH_GROUP_2 = 1,
	CALIB_CH_GROUP_3 = 2,
	CALIB_CH_GROUP_4 = 3,
	CALIB_CH_GROUP_5 = 4,
	CALIB_CH_GROUP_MAX
};

/********************* END TXPOWER *****************************************/


/**
 * Tx/Rx Queues
 *
 * Most communication between driver and 4965 is via queues of data buffers.
 * For example, all commands that the driver issues to device's embedded
 * controller (uCode) are via the command queue (one of the Tx queues).  All
 * uCode command responses/replies/notifications, including Rx frames, are
 * conveyed from uCode to driver via the Rx queue.
 *
 * Most support for these queues, including handshake support, resides in
 * structures in host DRAM, shared between the driver and the device.  When
 * allocating this memory, the driver must make sure that data written by
 * the host CPU updates DRAM immediately (and does not get "stuck" in CPU's
 * cache memory), so DRAM and cache are consistent, and the device can
 * immediately see changes made by the driver.
 *
 * 4965 supports up to 16 DRAM-based Tx queues, and services these queues via
 * up to 7 DMA channels (FIFOs).  Each Tx queue is supported by a circular array
 * in DRAM containing 256 Transmit Frame Descriptors (TFDs).
 */
#define IL49_NUM_FIFOS	7
#define IL49_CMD_FIFO_NUM	4
#define IL49_NUM_QUEUES	16
#define IL49_NUM_AMPDU_QUEUES	8


/**
 * struct il4965_schedq_bc_tbl
 *
 * Byte Count table
 *
 * Each Tx queue uses a byte-count table containing 320 entries:
 * one 16-bit entry for each of 256 TFDs, plus an additional 64 entries that
 * duplicate the first 64 entries (to avoid wrap-around within a Tx win;
 * max Tx win is 64 TFDs).
 *
 * When driver sets up a new TFD, it must also enter the total byte count
 * of the frame to be transmitted into the corresponding entry in the byte
 * count table for the chosen Tx queue.  If the TFD idx is 0-63, the driver
 * must duplicate the byte count entry in corresponding idx 256-319.
 *
 * padding puts each byte count table on a 1024-byte boundary;
 * 4965 assumes tables are separated by 1024 bytes.
 */
struct il4965_scd_bc_tbl {
	__le16 tfd_offset[TFD_QUEUE_BC_SIZE];
	u8 pad[1024 - (TFD_QUEUE_BC_SIZE) * sizeof(__le16)];
} __packed;


#define IL4965_RTC_INST_LOWER_BOUND		(0x000000)

/* RSSI to dBm */
#define IL4965_RSSI_OFFSET	44

/* PCI registers */
#define PCI_CFG_RETRY_TIMEOUT	0x041

/* PCI register values */
#define PCI_CFG_LINK_CTRL_VAL_L0S_EN	0x01
#define PCI_CFG_LINK_CTRL_VAL_L1_EN	0x02

#define IL4965_DEFAULT_TX_RETRY  15

/* EEPROM */
#define IL4965_FIRST_AMPDU_QUEUE	10

/* Calibration */
void il4965_chain_noise_calibration(struct il_priv *il, void *stat_resp);
void il4965_sensitivity_calibration(struct il_priv *il, void *resp);
void il4965_init_sensitivity(struct il_priv *il);
void il4965_reset_run_time_calib(struct il_priv *il);
void il4965_calib_free_results(struct il_priv *il);

/* Debug */
#ifdef CONFIG_IWLEGACY_DEBUGFS
ssize_t il4965_ucode_rx_stats_read(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos);
ssize_t il4965_ucode_tx_stats_read(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos);
ssize_t il4965_ucode_general_stats_read(struct file *file,
			char __user *user_buf, size_t count, loff_t *ppos);
#else
static ssize_t
il4965_ucode_rx_stats_read(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	return 0;
}
static ssize_t
il4965_ucode_tx_stats_read(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	return 0;
}
static ssize_t
il4965_ucode_general_stats_read(struct file *file, char __user *user_buf,
					    size_t count, loff_t *ppos)
{
	return 0;
}
#endif

#endif /* __il_4965_h__ */
