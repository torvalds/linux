/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2020 Felix Fietkau <nbd@nbd.name>
 */
#ifndef __MT76_TESTMODE_H
#define __MT76_TESTMODE_H

/**
 * enum mt76_testmode_attr - testmode attributes inside NL80211_ATTR_TESTDATA
 *
 * @MT76_TM_ATTR_UNSPEC: (invalid attribute)
 *
 * @MT76_TM_ATTR_RESET: reset parameters to default (flag)
 * @MT76_TM_ATTR_STATE: test state (u32), see &enum mt76_testmode_state
 *
 * @MT76_TM_ATTR_MTD_PART: mtd partition used for eeprom data (string)
 * @MT76_TM_ATTR_MTD_OFFSET: offset of eeprom data within the partition (u32)
 *
 * @MT76_TM_ATTR_TX_COUNT: configured number of frames to send when setting
 *	state to MT76_TM_STATE_TX_FRAMES (u32)
 * @MT76_TM_ATTR_TX_PENDING: pending frames during MT76_TM_STATE_TX_FRAMES (u32)
 * @MT76_TM_ATTR_TX_LENGTH: packet tx msdu length (u32)
 * @MT76_TM_ATTR_TX_RATE_MODE: packet tx mode (u8, see &enum mt76_testmode_tx_mode)
 * @MT76_TM_ATTR_TX_RATE_NSS: packet tx number of spatial streams (u8)
 * @MT76_TM_ATTR_TX_RATE_IDX: packet tx rate/MCS index (u8)
 * @MT76_TM_ATTR_TX_RATE_SGI: packet tx use short guard interval (u8)
 * @MT76_TM_ATTR_TX_RATE_LDPC: packet tx enable LDPC (u8)
 *
 * @MT76_TM_ATTR_TX_ANTENNA: tx antenna mask (u8)
 * @MT76_TM_ATTR_TX_POWER_CONTROL: enable tx power control (u8)
 * @MT76_TM_ATTR_TX_POWER: per-antenna tx power array (nested, u8 attrs)
 *
 * @MT76_TM_ATTR_FREQ_OFFSET: RF frequency offset (u32)
 *
 * @MT76_TM_ATTR_STATS: statistics (nested, see &enum mt76_testmode_stats_attr)
 */
enum mt76_testmode_attr {
	MT76_TM_ATTR_UNSPEC,

	MT76_TM_ATTR_RESET,
	MT76_TM_ATTR_STATE,

	MT76_TM_ATTR_MTD_PART,
	MT76_TM_ATTR_MTD_OFFSET,

	MT76_TM_ATTR_TX_COUNT,
	MT76_TM_ATTR_TX_LENGTH,
	MT76_TM_ATTR_TX_RATE_MODE,
	MT76_TM_ATTR_TX_RATE_NSS,
	MT76_TM_ATTR_TX_RATE_IDX,
	MT76_TM_ATTR_TX_RATE_SGI,
	MT76_TM_ATTR_TX_RATE_LDPC,

	MT76_TM_ATTR_TX_ANTENNA,
	MT76_TM_ATTR_TX_POWER_CONTROL,
	MT76_TM_ATTR_TX_POWER,

	MT76_TM_ATTR_FREQ_OFFSET,

	MT76_TM_ATTR_STATS,

	/* keep last */
	NUM_MT76_TM_ATTRS,
	MT76_TM_ATTR_MAX = NUM_MT76_TM_ATTRS - 1,
};

/**
 * enum mt76_testmode_state - statistics attributes
 *
 * @MT76_TM_STATS_ATTR_TX_PENDING: pending tx frames (u32)
 * @MT76_TM_STATS_ATTR_TX_QUEUED: queued tx frames (u32)
 * @MT76_TM_STATS_ATTR_TX_QUEUED: completed tx frames (u32)
 *
 * @MT76_TM_STATS_ATTR_RX_PACKETS: number of rx packets (u64)
 * @MT76_TM_STATS_ATTR_RX_FCS_ERROR: number of rx packets with FCS error (u64)
 * @MT76_TM_STATS_ATTR_LAST_RX: information about the last received packet
 *	see &enum mt76_testmode_rx_attr
 */
enum mt76_testmode_stats_attr {
	MT76_TM_STATS_ATTR_UNSPEC,
	MT76_TM_STATS_ATTR_PAD,

	MT76_TM_STATS_ATTR_TX_PENDING,
	MT76_TM_STATS_ATTR_TX_QUEUED,
	MT76_TM_STATS_ATTR_TX_DONE,

	MT76_TM_STATS_ATTR_RX_PACKETS,
	MT76_TM_STATS_ATTR_RX_FCS_ERROR,
	MT76_TM_STATS_ATTR_LAST_RX,

	/* keep last */
	NUM_MT76_TM_STATS_ATTRS,
	MT76_TM_STATS_ATTR_MAX = NUM_MT76_TM_STATS_ATTRS - 1,
};


/**
 * enum mt76_testmode_rx_attr - packet rx information
 *
 * @MT76_TM_RX_ATTR_FREQ_OFFSET: frequency offset (s32)
 * @MT76_TM_RX_ATTR_RCPI: received channel power indicator (array, u8)
 * @MT76_TM_RX_ATTR_IB_RSSI: internal inband RSSI (s8)
 * @MT76_TM_RX_ATTR_WB_RSSI: internal wideband RSSI (s8)
 */
enum mt76_testmode_rx_attr {
	MT76_TM_RX_ATTR_UNSPEC,

	MT76_TM_RX_ATTR_FREQ_OFFSET,
	MT76_TM_RX_ATTR_RCPI,
	MT76_TM_RX_ATTR_IB_RSSI,
	MT76_TM_RX_ATTR_WB_RSSI,

	/* keep last */
	NUM_MT76_TM_RX_ATTRS,
	MT76_TM_RX_ATTR_MAX = NUM_MT76_TM_RX_ATTRS - 1,
};

/**
 * enum mt76_testmode_state - phy test state
 *
 * @MT76_TM_STATE_OFF: test mode disabled (normal operation)
 * @MT76_TM_STATE_IDLE: test mode enabled, but idle
 * @MT76_TM_STATE_TX_FRAMES: send a fixed number of test frames
 * @MT76_TM_STATE_RX_FRAMES: receive packets and keep statistics
 */
enum mt76_testmode_state {
	MT76_TM_STATE_OFF,
	MT76_TM_STATE_IDLE,
	MT76_TM_STATE_TX_FRAMES,
	MT76_TM_STATE_RX_FRAMES,

	/* keep last */
	NUM_MT76_TM_STATES,
	MT76_TM_STATE_MAX = NUM_MT76_TM_STATES - 1,
};

/**
 * enum mt76_testmode_tx_mode - packet tx phy mode
 *
 * @MT76_TM_TX_MODE_CCK: legacy CCK mode
 * @MT76_TM_TX_MODE_OFDM: legacy OFDM mode
 * @MT76_TM_TX_MODE_HT: 802.11n MCS
 * @MT76_TM_TX_MODE_VHT: 802.11ac MCS
 */
enum mt76_testmode_tx_mode {
	MT76_TM_TX_MODE_CCK,
	MT76_TM_TX_MODE_OFDM,
	MT76_TM_TX_MODE_HT,
	MT76_TM_TX_MODE_VHT,

	/* keep last */
	NUM_MT76_TM_TX_MODES,
	MT76_TM_TX_MODE_MAX = NUM_MT76_TM_TX_MODES - 1,
};

#endif
