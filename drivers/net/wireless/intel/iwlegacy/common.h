/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************
 *
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/
#ifndef __il_core_h__
#define __il_core_h__

#include <linux/interrupt.h>
#include <linux/pci.h>		/* for struct pci_device_id */
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/wait.h>
#include <linux/io.h>
#include <net/mac80211.h>
#include <net/ieee80211_radiotap.h>

#include "commands.h"
#include "csr.h"
#include "prph.h"

struct il_host_cmd;
struct il_cmd;
struct il_tx_queue;

#define IL_ERR(f, a...) dev_err(&il->pci_dev->dev, f, ## a)
#define IL_WARN(f, a...) dev_warn(&il->pci_dev->dev, f, ## a)
#define IL_WARN_ONCE(f, a...) dev_warn_once(&il->pci_dev->dev, f, ## a)
#define IL_INFO(f, a...) dev_info(&il->pci_dev->dev, f, ## a)

#define RX_QUEUE_SIZE                         256
#define RX_QUEUE_MASK                         255
#define RX_QUEUE_SIZE_LOG                     8

/*
 * RX related structures and functions
 */
#define RX_FREE_BUFFERS 64
#define RX_LOW_WATERMARK 8

#define U32_PAD(n)		((4-(n))&0x3)

/* CT-KILL constants */
#define CT_KILL_THRESHOLD_LEGACY   110	/* in Celsius */

/* Default noise level to report when noise measurement is not available.
 *   This may be because we're:
 *   1)  Not associated (4965, no beacon stats being sent to driver)
 *   2)  Scanning (noise measurement does not apply to associated channel)
 *   3)  Receiving CCK (3945 delivers noise info only for OFDM frames)
 * Use default noise value of -127 ... this is below the range of measurable
 *   Rx dBm for either 3945 or 4965, so it can indicate "unmeasurable" to user.
 *   Also, -127 works better than 0 when averaging frames with/without
 *   noise info (e.g. averaging might be done in app); measured dBm values are
 *   always negative ... using a negative value as the default keeps all
 *   averages within an s8's (used in some apps) range of negative values. */
#define IL_NOISE_MEAS_NOT_AVAILABLE (-127)

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
#define DEFAULT_BEACON_INTERVAL   100U
#define	DEFAULT_SHORT_RETRY_LIMIT 7U
#define	DEFAULT_LONG_RETRY_LIMIT  4U

struct il_rx_buf {
	dma_addr_t page_dma;
	struct page *page;
	struct list_head list;
};

#define rxb_addr(r) page_address(r->page)

/* defined below */
struct il_device_cmd;

struct il_cmd_meta {
	/* only for SYNC commands, iff the reply skb is wanted */
	struct il_host_cmd *source;
	/*
	 * only for ASYNC commands
	 * (which is somewhat stupid -- look at common.c for instance
	 * which duplicates a bunch of code because the callback isn't
	 * invoked for SYNC commands, if it were and its result passed
	 * through it would be simpler...)
	 */
	void (*callback) (struct il_priv *il, struct il_device_cmd *cmd,
			  struct il_rx_pkt *pkt);

	/* The CMD_SIZE_HUGE flag bit indicates that the command
	 * structure is stored at the end of the shared queue memory. */
	u32 flags;

	 DEFINE_DMA_UNMAP_ADDR(mapping);
	 DEFINE_DMA_UNMAP_LEN(len);
};

/*
 * Generic queue structure
 *
 * Contains common data for Rx and Tx queues
 */
struct il_queue {
	int n_bd;		/* number of BDs in this queue */
	int write_ptr;		/* 1-st empty entry (idx) host_w */
	int read_ptr;		/* last used entry (idx) host_r */
	/* use for monitoring and recovering the stuck queue */
	dma_addr_t dma_addr;	/* physical addr for BD's */
	int n_win;		/* safe queue win */
	u32 id;
	int low_mark;		/* low watermark, resume queue if free
				 * space more than this */
	int high_mark;		/* high watermark, stop queue if free
				 * space less than this */
};

/**
 * struct il_tx_queue - Tx Queue for DMA
 * @q: generic Rx/Tx queue descriptor
 * @bd: base of circular buffer of TFDs
 * @cmd: array of command/TX buffer pointers
 * @meta: array of meta data for each command/tx buffer
 * @dma_addr_cmd: physical address of cmd/tx buffer array
 * @skbs: array of per-TFD socket buffer pointers
 * @time_stamp: time (in jiffies) of last read_ptr change
 * @need_update: indicates need to update read/write idx
 * @sched_retry: indicates queue is high-throughput aggregation (HT AGG) enabled
 *
 * A Tx queue consists of circular buffer of BDs (a.k.a. TFDs, transmit frame
 * descriptors) and required locking structures.
 */
#define TFD_TX_CMD_SLOTS 256
#define TFD_CMD_SLOTS 32

struct il_tx_queue {
	struct il_queue q;
	void *tfds;
	struct il_device_cmd **cmd;
	struct il_cmd_meta *meta;
	struct sk_buff **skbs;
	unsigned long time_stamp;
	u8 need_update;
	u8 sched_retry;
	u8 active;
	u8 swq_id;
};

/*
 * EEPROM access time values:
 *
 * Driver initiates EEPROM read by writing byte address << 1 to CSR_EEPROM_REG.
 * Driver then polls CSR_EEPROM_REG for CSR_EEPROM_REG_READ_VALID_MSK (0x1).
 * When polling, wait 10 uSec between polling loops, up to a maximum 5000 uSec.
 * Driver reads 16-bit value from bits 31-16 of CSR_EEPROM_REG.
 */
#define IL_EEPROM_ACCESS_TIMEOUT	5000	/* uSec */

#define IL_EEPROM_SEM_TIMEOUT		10	/* microseconds */
#define IL_EEPROM_SEM_RETRY_LIMIT	1000	/* number of attempts (not time) */

/*
 * Regulatory channel usage flags in EEPROM struct il4965_eeprom_channel.flags.
 *
 * IBSS and/or AP operation is allowed *only* on those channels with
 * (VALID && IBSS && ACTIVE && !RADAR).  This restriction is in place because
 * RADAR detection is not supported by the 4965 driver, but is a
 * requirement for establishing a new network for legal operation on channels
 * requiring RADAR detection or restricting ACTIVE scanning.
 *
 * NOTE:  "WIDE" flag does not indicate anything about "HT40" 40 MHz channels.
 *        It only indicates that 20 MHz channel use is supported; HT40 channel
 *        usage is indicated by a separate set of regulatory flags for each
 *        HT40 channel pair.
 *
 * NOTE:  Using a channel inappropriately will result in a uCode error!
 */
#define IL_NUM_TX_CALIB_GROUPS 5
enum {
	EEPROM_CHANNEL_VALID = (1 << 0),	/* usable for this SKU/geo */
	EEPROM_CHANNEL_IBSS = (1 << 1),	/* usable as an IBSS channel */
	/* Bit 2 Reserved */
	EEPROM_CHANNEL_ACTIVE = (1 << 3),	/* active scanning allowed */
	EEPROM_CHANNEL_RADAR = (1 << 4),	/* radar detection required */
	EEPROM_CHANNEL_WIDE = (1 << 5),	/* 20 MHz channel okay */
	/* Bit 6 Reserved (was Narrow Channel) */
	EEPROM_CHANNEL_DFS = (1 << 7),	/* dynamic freq selection candidate */
};

/* SKU Capabilities */
/* 3945 only */
#define EEPROM_SKU_CAP_SW_RF_KILL_ENABLE                (1 << 0)
#define EEPROM_SKU_CAP_HW_RF_KILL_ENABLE                (1 << 1)

/* *regulatory* channel data format in eeprom, one for each channel.
 * There are separate entries for HT40 (40 MHz) vs. normal (20 MHz) channels. */
struct il_eeprom_channel {
	u8 flags;		/* EEPROM_CHANNEL_* flags copied from EEPROM */
	s8 max_power_avg;	/* max power (dBm) on this chnl, limit 31 */
} __packed;

/* 3945 Specific */
#define EEPROM_3945_EEPROM_VERSION	(0x2f)

/* 4965 has two radio transmitters (and 3 radio receivers) */
#define EEPROM_TX_POWER_TX_CHAINS      (2)

/* 4965 has room for up to 8 sets of txpower calibration data */
#define EEPROM_TX_POWER_BANDS          (8)

/* 4965 factory calibration measures txpower gain settings for
 * each of 3 target output levels */
#define EEPROM_TX_POWER_MEASUREMENTS   (3)

/* 4965 Specific */
/* 4965 driver does not work with txpower calibration version < 5 */
#define EEPROM_4965_TX_POWER_VERSION    (5)
#define EEPROM_4965_EEPROM_VERSION	(0x2f)
#define EEPROM_4965_CALIB_VERSION_OFFSET       (2*0xB6)	/* 2 bytes */
#define EEPROM_4965_CALIB_TXPOWER_OFFSET       (2*0xE8)	/* 48  bytes */
#define EEPROM_4965_BOARD_REVISION             (2*0x4F)	/* 2 bytes */
#define EEPROM_4965_BOARD_PBA                  (2*0x56+1)	/* 9 bytes */

/* 2.4 GHz */
extern const u8 il_eeprom_band_1[14];

/*
 * factory calibration data for one txpower level, on one channel,
 * measured on one of the 2 tx chains (radio transmitter and associated
 * antenna).  EEPROM contains:
 *
 * 1)  Temperature (degrees Celsius) of device when measurement was made.
 *
 * 2)  Gain table idx used to achieve the target measurement power.
 *     This refers to the "well-known" gain tables (see 4965.h).
 *
 * 3)  Actual measured output power, in half-dBm ("34" = 17 dBm).
 *
 * 4)  RF power amplifier detector level measurement (not used).
 */
struct il_eeprom_calib_measure {
	u8 temperature;		/* Device temperature (Celsius) */
	u8 gain_idx;		/* Index into gain table */
	u8 actual_pow;		/* Measured RF output power, half-dBm */
	s8 pa_det;		/* Power amp detector level (not used) */
} __packed;

/*
 * measurement set for one channel.  EEPROM contains:
 *
 * 1)  Channel number measured
 *
 * 2)  Measurements for each of 3 power levels for each of 2 radio transmitters
 *     (a.k.a. "tx chains") (6 measurements altogether)
 */
struct il_eeprom_calib_ch_info {
	u8 ch_num;
	struct il_eeprom_calib_measure
	    measurements[EEPROM_TX_POWER_TX_CHAINS]
	    [EEPROM_TX_POWER_MEASUREMENTS];
} __packed;

/*
 * txpower subband info.
 *
 * For each frequency subband, EEPROM contains the following:
 *
 * 1)  First and last channels within range of the subband.  "0" values
 *     indicate that this sample set is not being used.
 *
 * 2)  Sample measurement sets for 2 channels close to the range endpoints.
 */
struct il_eeprom_calib_subband_info {
	u8 ch_from;		/* channel number of lowest channel in subband */
	u8 ch_to;		/* channel number of highest channel in subband */
	struct il_eeprom_calib_ch_info ch1;
	struct il_eeprom_calib_ch_info ch2;
} __packed;

/*
 * txpower calibration info.  EEPROM contains:
 *
 * 1)  Factory-measured saturation power levels (maximum levels at which
 *     tx power amplifier can output a signal without too much distortion).
 *     There is one level for 2.4 GHz band and one for 5 GHz band.  These
 *     values apply to all channels within each of the bands.
 *
 * 2)  Factory-measured power supply voltage level.  This is assumed to be
 *     constant (i.e. same value applies to all channels/bands) while the
 *     factory measurements are being made.
 *
 * 3)  Up to 8 sets of factory-measured txpower calibration values.
 *     These are for different frequency ranges, since txpower gain
 *     characteristics of the analog radio circuitry vary with frequency.
 *
 *     Not all sets need to be filled with data;
 *     struct il_eeprom_calib_subband_info contains range of channels
 *     (0 if unused) for each set of data.
 */
struct il_eeprom_calib_info {
	u8 saturation_power24;	/* half-dBm (e.g. "34" = 17 dBm) */
	u8 saturation_power52;	/* half-dBm */
	__le16 voltage;		/* signed */
	struct il_eeprom_calib_subband_info band_info[EEPROM_TX_POWER_BANDS];
} __packed;

/* General */
#define EEPROM_DEVICE_ID                    (2*0x08)	/* 2 bytes */
#define EEPROM_MAC_ADDRESS                  (2*0x15)	/* 6  bytes */
#define EEPROM_BOARD_REVISION               (2*0x35)	/* 2  bytes */
#define EEPROM_BOARD_PBA_NUMBER             (2*0x3B+1)	/* 9  bytes */
#define EEPROM_VERSION                      (2*0x44)	/* 2  bytes */
#define EEPROM_SKU_CAP                      (2*0x45)	/* 2  bytes */
#define EEPROM_OEM_MODE                     (2*0x46)	/* 2  bytes */
#define EEPROM_WOWLAN_MODE                  (2*0x47)	/* 2  bytes */
#define EEPROM_RADIO_CONFIG                 (2*0x48)	/* 2  bytes */
#define EEPROM_NUM_MAC_ADDRESS              (2*0x4C)	/* 2  bytes */

/* The following masks are to be applied on EEPROM_RADIO_CONFIG */
#define EEPROM_RF_CFG_TYPE_MSK(x)   (x & 0x3)	/* bits 0-1   */
#define EEPROM_RF_CFG_STEP_MSK(x)   ((x >> 2)  & 0x3)	/* bits 2-3   */
#define EEPROM_RF_CFG_DASH_MSK(x)   ((x >> 4)  & 0x3)	/* bits 4-5   */
#define EEPROM_RF_CFG_PNUM_MSK(x)   ((x >> 6)  & 0x3)	/* bits 6-7   */
#define EEPROM_RF_CFG_TX_ANT_MSK(x) ((x >> 8)  & 0xF)	/* bits 8-11  */
#define EEPROM_RF_CFG_RX_ANT_MSK(x) ((x >> 12) & 0xF)	/* bits 12-15 */

#define EEPROM_3945_RF_CFG_TYPE_MAX  0x0
#define EEPROM_4965_RF_CFG_TYPE_MAX  0x1

/*
 * Per-channel regulatory data.
 *
 * Each channel that *might* be supported by iwl has a fixed location
 * in EEPROM containing EEPROM_CHANNEL_* usage flags (LSB) and max regulatory
 * txpower (MSB).
 *
 * Entries immediately below are for 20 MHz channel width.  HT40 (40 MHz)
 * channels (only for 4965, not supported by 3945) appear later in the EEPROM.
 *
 * 2.4 GHz channels 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
 */
#define EEPROM_REGULATORY_SKU_ID            (2*0x60)	/* 4  bytes */
#define EEPROM_REGULATORY_BAND_1            (2*0x62)	/* 2  bytes */
#define EEPROM_REGULATORY_BAND_1_CHANNELS   (2*0x63)	/* 28 bytes */

/*
 * 4.9 GHz channels 183, 184, 185, 187, 188, 189, 192, 196,
 * 5.0 GHz channels 7, 8, 11, 12, 16
 * (4915-5080MHz) (none of these is ever supported)
 */
#define EEPROM_REGULATORY_BAND_2            (2*0x71)	/* 2  bytes */
#define EEPROM_REGULATORY_BAND_2_CHANNELS   (2*0x72)	/* 26 bytes */

/*
 * 5.2 GHz channels 34, 36, 38, 40, 42, 44, 46, 48, 52, 56, 60, 64
 * (5170-5320MHz)
 */
#define EEPROM_REGULATORY_BAND_3            (2*0x7F)	/* 2  bytes */
#define EEPROM_REGULATORY_BAND_3_CHANNELS   (2*0x80)	/* 24 bytes */

/*
 * 5.5 GHz channels 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140
 * (5500-5700MHz)
 */
#define EEPROM_REGULATORY_BAND_4            (2*0x8C)	/* 2  bytes */
#define EEPROM_REGULATORY_BAND_4_CHANNELS   (2*0x8D)	/* 22 bytes */

/*
 * 5.7 GHz channels 145, 149, 153, 157, 161, 165
 * (5725-5825MHz)
 */
#define EEPROM_REGULATORY_BAND_5            (2*0x98)	/* 2  bytes */
#define EEPROM_REGULATORY_BAND_5_CHANNELS   (2*0x99)	/* 12 bytes */

/*
 * 2.4 GHz HT40 channels 1 (5), 2 (6), 3 (7), 4 (8), 5 (9), 6 (10), 7 (11)
 *
 * The channel listed is the center of the lower 20 MHz half of the channel.
 * The overall center frequency is actually 2 channels (10 MHz) above that,
 * and the upper half of each HT40 channel is centered 4 channels (20 MHz) away
 * from the lower half; e.g. the upper half of HT40 channel 1 is channel 5,
 * and the overall HT40 channel width centers on channel 3.
 *
 * NOTE:  The RXON command uses 20 MHz channel numbers to specify the
 *        control channel to which to tune.  RXON also specifies whether the
 *        control channel is the upper or lower half of a HT40 channel.
 *
 * NOTE:  4965 does not support HT40 channels on 2.4 GHz.
 */
#define EEPROM_4965_REGULATORY_BAND_24_HT40_CHANNELS (2*0xA0)	/* 14 bytes */

/*
 * 5.2 GHz HT40 channels 36 (40), 44 (48), 52 (56), 60 (64),
 * 100 (104), 108 (112), 116 (120), 124 (128), 132 (136), 149 (153), 157 (161)
 */
#define EEPROM_4965_REGULATORY_BAND_52_HT40_CHANNELS (2*0xA8)	/* 22 bytes */

#define EEPROM_REGULATORY_BAND_NO_HT40			(0)

int il_eeprom_init(struct il_priv *il);
void il_eeprom_free(struct il_priv *il);
const u8 *il_eeprom_query_addr(const struct il_priv *il, size_t offset);
u16 il_eeprom_query16(const struct il_priv *il, size_t offset);
int il_init_channel_map(struct il_priv *il);
void il_free_channel_map(struct il_priv *il);
const struct il_channel_info *il_get_channel_info(const struct il_priv *il,
						  enum nl80211_band band,
						  u16 channel);

#define IL_NUM_SCAN_RATES         (2)

struct il4965_channel_tgd_info {
	u8 type;
	s8 max_power;
};

struct il4965_channel_tgh_info {
	s64 last_radar_time;
};

#define IL4965_MAX_RATE (33)

struct il3945_clip_group {
	/* maximum power level to prevent clipping for each rate, derived by
	 *   us from this band's saturation power in EEPROM */
	const s8 clip_powers[IL_MAX_RATES];
};

/* current Tx power values to use, one for each rate for each channel.
 * requested power is limited by:
 * -- regulatory EEPROM limits for this channel
 * -- hardware capabilities (clip-powers)
 * -- spectrum management
 * -- user preference (e.g. iwconfig)
 * when requested power is set, base power idx must also be set. */
struct il3945_channel_power_info {
	struct il3945_tx_power tpc;	/* actual radio and DSP gain settings */
	s8 power_table_idx;	/* actual (compenst'd) idx into gain table */
	s8 base_power_idx;	/* gain idx for power at factory temp. */
	s8 requested_power;	/* power (dBm) requested for this chnl/rate */
};

/* current scan Tx power values to use, one for each scan rate for each
 * channel. */
struct il3945_scan_power_info {
	struct il3945_tx_power tpc;	/* actual radio and DSP gain settings */
	s8 power_table_idx;	/* actual (compenst'd) idx into gain table */
	s8 requested_power;	/* scan pwr (dBm) requested for chnl/rate */
};

/*
 * One for each channel, holds all channel setup data
 * Some of the fields (e.g. eeprom and flags/max_power_avg) are redundant
 *     with one another!
 */
struct il_channel_info {
	struct il4965_channel_tgd_info tgd;
	struct il4965_channel_tgh_info tgh;
	struct il_eeprom_channel eeprom;	/* EEPROM regulatory limit */
	struct il_eeprom_channel ht40_eeprom;	/* EEPROM regulatory limit for
						 * HT40 channel */

	u8 channel;		/* channel number */
	u8 flags;		/* flags copied from EEPROM */
	s8 max_power_avg;	/* (dBm) regul. eeprom, normal Tx, any rate */
	s8 curr_txpow;		/* (dBm) regulatory/spectrum/user (not h/w) limit */
	s8 min_power;		/* always 0 */
	s8 scan_power;		/* (dBm) regul. eeprom, direct scans, any rate */

	u8 group_idx;		/* 0-4, maps channel to group1/2/3/4/5 */
	u8 band_idx;		/* 0-4, maps channel to band1/2/3/4/5 */
	enum nl80211_band band;

	/* HT40 channel info */
	s8 ht40_max_power_avg;	/* (dBm) regul. eeprom, normal Tx, any rate */
	u8 ht40_flags;		/* flags copied from EEPROM */
	u8 ht40_extension_channel;	/* HT_IE_EXT_CHANNEL_* */

	/* Radio/DSP gain settings for each "normal" data Tx rate.
	 * These include, in addition to RF and DSP gain, a few fields for
	 *   remembering/modifying gain settings (idxes). */
	struct il3945_channel_power_info power_info[IL4965_MAX_RATE];

	/* Radio/DSP gain settings for each scan rate, for directed scans. */
	struct il3945_scan_power_info scan_pwr_info[IL_NUM_SCAN_RATES];
};

#define IL_TX_FIFO_BK		0	/* shared */
#define IL_TX_FIFO_BE		1
#define IL_TX_FIFO_VI		2	/* shared */
#define IL_TX_FIFO_VO		3
#define IL_TX_FIFO_UNUSED	-1

/* Minimum number of queues. MAX_NUM is defined in hw specific files.
 * Set the minimum to accommodate the 4 standard TX queues, 1 command
 * queue, 2 (unused) HCCA queues, and 4 HT queues (one for each AC) */
#define IL_MIN_NUM_QUEUES	10

#define IL_DEFAULT_CMD_QUEUE_NUM	4

#define IEEE80211_DATA_LEN              2304
#define IEEE80211_4ADDR_LEN             30
#define IEEE80211_HLEN                  (IEEE80211_4ADDR_LEN)
#define IEEE80211_FRAME_LEN             (IEEE80211_DATA_LEN + IEEE80211_HLEN)

struct il_frame {
	union {
		struct ieee80211_hdr frame;
		struct il_tx_beacon_cmd beacon;
		u8 raw[IEEE80211_FRAME_LEN];
		u8 cmd[360];
	} u;
	struct list_head list;
};

enum {
	CMD_SYNC = 0,
	CMD_SIZE_NORMAL = 0,
	CMD_NO_SKB = 0,
	CMD_SIZE_HUGE = (1 << 0),
	CMD_ASYNC = (1 << 1),
	CMD_WANT_SKB = (1 << 2),
	CMD_MAPPED = (1 << 3),
};

#define DEF_CMD_PAYLOAD_SIZE 320

/**
 * struct il_device_cmd
 *
 * For allocation of the command and tx queues, this establishes the overall
 * size of the largest command we send to uCode, except for a scan command
 * (which is relatively huge; space is allocated separately).
 */
struct il_device_cmd {
	struct il_cmd_header hdr;	/* uCode API */
	union {
		u32 flags;
		u8 val8;
		u16 val16;
		u32 val32;
		struct il_tx_cmd tx;
		u8 payload[DEF_CMD_PAYLOAD_SIZE];
	} __packed cmd;
} __packed;

#define TFD_MAX_PAYLOAD_SIZE (sizeof(struct il_device_cmd))

struct il_host_cmd {
	const void *data;
	unsigned long reply_page;
	void (*callback) (struct il_priv *il, struct il_device_cmd *cmd,
			  struct il_rx_pkt *pkt);
	u32 flags;
	u16 len;
	u8 id;
};

#define SUP_RATE_11A_MAX_NUM_CHANNELS  8
#define SUP_RATE_11B_MAX_NUM_CHANNELS  4
#define SUP_RATE_11G_MAX_NUM_CHANNELS  12

/**
 * struct il_rx_queue - Rx queue
 * @bd: driver's pointer to buffer of receive buffer descriptors (rbd)
 * @bd_dma: bus address of buffer of receive buffer descriptors (rbd)
 * @read: Shared idx to newest available Rx buffer
 * @write: Shared idx to oldest written Rx packet
 * @free_count: Number of pre-allocated buffers in rx_free
 * @rx_free: list of free SKBs for use
 * @rx_used: List of Rx buffers with no SKB
 * @need_update: flag to indicate we need to update read/write idx
 * @rb_stts: driver's pointer to receive buffer status
 * @rb_stts_dma: bus address of receive buffer status
 *
 * NOTE:  rx_free and rx_used are used as a FIFO for il_rx_bufs
 */
struct il_rx_queue {
	__le32 *bd;
	dma_addr_t bd_dma;
	struct il_rx_buf pool[RX_QUEUE_SIZE + RX_FREE_BUFFERS];
	struct il_rx_buf *queue[RX_QUEUE_SIZE];
	u32 read;
	u32 write;
	u32 free_count;
	u32 write_actual;
	struct list_head rx_free;
	struct list_head rx_used;
	int need_update;
	struct il_rb_status *rb_stts;
	dma_addr_t rb_stts_dma;
	spinlock_t lock;
};

#define IL_SUPPORTED_RATES_IE_LEN         8

#define MAX_TID_COUNT        9

#define IL_INVALID_RATE     0xFF
#define IL_INVALID_VALUE    -1

/**
 * struct il_ht_agg -- aggregation status while waiting for block-ack
 * @txq_id: Tx queue used for Tx attempt
 * @frame_count: # frames attempted by Tx command
 * @wait_for_ba: Expect block-ack before next Tx reply
 * @start_idx: Index of 1st Transmit Frame Descriptor (TFD) in Tx win
 * @bitmap0: Low order bitmap, one bit for each frame pending ACK in Tx win
 * @bitmap1: High order, one bit for each frame pending ACK in Tx win
 * @rate_n_flags: Rate at which Tx was attempted
 *
 * If C_TX indicates that aggregation was attempted, driver must wait
 * for block ack (N_COMPRESSED_BA).  This struct stores tx reply info
 * until block ack arrives.
 */
struct il_ht_agg {
	u16 txq_id;
	u16 frame_count;
	u16 wait_for_ba;
	u16 start_idx;
	u64 bitmap;
	u32 rate_n_flags;
#define IL_AGG_OFF 0
#define IL_AGG_ON 1
#define IL_EMPTYING_HW_QUEUE_ADDBA 2
#define IL_EMPTYING_HW_QUEUE_DELBA 3
	u8 state;
};

struct il_tid_data {
	u16 seq_number;		/* 4965 only */
	u16 tfds_in_queue;
	struct il_ht_agg agg;
};

struct il_hw_key {
	u32 cipher;
	int keylen;
	u8 keyidx;
	u8 key[32];
};

union il_ht_rate_supp {
	u16 rates;
	struct {
		u8 siso_rate;
		u8 mimo_rate;
	};
};

#define CFG_HT_RX_AMPDU_FACTOR_8K   (0x0)
#define CFG_HT_RX_AMPDU_FACTOR_16K  (0x1)
#define CFG_HT_RX_AMPDU_FACTOR_32K  (0x2)
#define CFG_HT_RX_AMPDU_FACTOR_64K  (0x3)
#define CFG_HT_RX_AMPDU_FACTOR_DEF  CFG_HT_RX_AMPDU_FACTOR_64K
#define CFG_HT_RX_AMPDU_FACTOR_MAX  CFG_HT_RX_AMPDU_FACTOR_64K
#define CFG_HT_RX_AMPDU_FACTOR_MIN  CFG_HT_RX_AMPDU_FACTOR_8K

/*
 * Maximal MPDU density for TX aggregation
 * 4 - 2us density
 * 5 - 4us density
 * 6 - 8us density
 * 7 - 16us density
 */
#define CFG_HT_MPDU_DENSITY_2USEC   (0x4)
#define CFG_HT_MPDU_DENSITY_4USEC   (0x5)
#define CFG_HT_MPDU_DENSITY_8USEC   (0x6)
#define CFG_HT_MPDU_DENSITY_16USEC  (0x7)
#define CFG_HT_MPDU_DENSITY_DEF CFG_HT_MPDU_DENSITY_4USEC
#define CFG_HT_MPDU_DENSITY_MAX CFG_HT_MPDU_DENSITY_16USEC
#define CFG_HT_MPDU_DENSITY_MIN     (0x1)

struct il_ht_config {
	bool single_chain_sufficient;
	enum ieee80211_smps_mode smps;	/* current smps mode */
};

/* QoS structures */
struct il_qos_info {
	int qos_active;
	struct il_qosparam_cmd def_qos_parm;
};

/*
 * Structure should be accessed with sta_lock held. When station addition
 * is in progress (IL_STA_UCODE_INPROGRESS) it is possible to access only
 * the commands (il_addsta_cmd and il_link_quality_cmd) without
 * sta_lock held.
 */
struct il_station_entry {
	struct il_addsta_cmd sta;
	struct il_tid_data tid[MAX_TID_COUNT];
	u8 used;
	struct il_hw_key keyinfo;
	struct il_link_quality_cmd *lq;
};

struct il_station_priv_common {
	u8 sta_id;
};

/**
 * struct il_vif_priv - driver's ilate per-interface information
 *
 * When mac80211 allocates a virtual interface, it can allocate
 * space for us to put data into.
 */
struct il_vif_priv {
	u8 ibss_bssid_sta_id;
};

/* one for each uCode image (inst/data, boot/init/runtime) */
struct fw_desc {
	void *v_addr;		/* access by driver */
	dma_addr_t p_addr;	/* access by card's busmaster DMA */
	u32 len;		/* bytes */
};

/* uCode file layout */
struct il_ucode_header {
	__le32 ver;		/* major/minor/API/serial */
	struct {
		__le32 inst_size;	/* bytes of runtime code */
		__le32 data_size;	/* bytes of runtime data */
		__le32 init_size;	/* bytes of init code */
		__le32 init_data_size;	/* bytes of init data */
		__le32 boot_size;	/* bytes of bootstrap code */
		u8 data[0];	/* in same order as sizes */
	} v1;
};

struct il4965_ibss_seq {
	u8 mac[ETH_ALEN];
	u16 seq_num;
	u16 frag_num;
	unsigned long packet_time;
	struct list_head list;
};

struct il_sensitivity_ranges {
	u16 min_nrg_cck;
	u16 max_nrg_cck;

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

/**
 * struct il_hw_params
 * @bcast_id: f/w broadcast station ID
 * @max_txq_num: Max # Tx queues supported
 * @dma_chnl_num: Number of Tx DMA/FIFO channels
 * @scd_bc_tbls_size: size of scheduler byte count tables
 * @tfd_size: TFD size
 * @tx/rx_chains_num: Number of TX/RX chains
 * @valid_tx/rx_ant: usable antennas
 * @max_rxq_size: Max # Rx frames in Rx queue (must be power-of-2)
 * @max_rxq_log: Log-base-2 of max_rxq_size
 * @rx_page_order: Rx buffer page order
 * @rx_wrt_ptr_reg: FH{39}_RSCSR_CHNL0_WPTR
 * @max_stations:
 * @ht40_channel: is 40MHz width possible in band 2.4
 * BIT(NL80211_BAND_5GHZ) BIT(NL80211_BAND_5GHZ)
 * @sw_crypto: 0 for hw, 1 for sw
 * @max_xxx_size: for ucode uses
 * @ct_kill_threshold: temperature threshold
 * @beacon_time_tsf_bits: number of valid tsf bits for beacon time
 * @struct il_sensitivity_ranges: range of sensitivity values
 */
struct il_hw_params {
	u8 bcast_id;
	u8 max_txq_num;
	u8 dma_chnl_num;
	u16 scd_bc_tbls_size;
	u32 tfd_size;
	u8 tx_chains_num;
	u8 rx_chains_num;
	u8 valid_tx_ant;
	u8 valid_rx_ant;
	u16 max_rxq_size;
	u16 max_rxq_log;
	u32 rx_page_order;
	u32 rx_wrt_ptr_reg;
	u8 max_stations;
	u8 ht40_channel;
	u8 max_beacon_itrvl;	/* in 1024 ms */
	u32 max_inst_size;
	u32 max_data_size;
	u32 max_bsm_size;
	u32 ct_kill_threshold;	/* value in hw-dependent units */
	u16 beacon_time_tsf_bits;
	const struct il_sensitivity_ranges *sens;
};

/******************************************************************************
 *
 * Functions implemented in core module which are forward declared here
 * for use by iwl-[4-5].c
 *
 * NOTE:  The implementation of these functions are not hardware specific
 * which is why they are in the core module files.
 *
 * Naming convention --
 * il_         <-- Is part of iwlwifi
 * iwlXXXX_     <-- Hardware specific (implemented in iwl-XXXX.c for XXXX)
 * il4965_bg_      <-- Called from work queue context
 * il4965_mac_     <-- mac80211 callback
 *
 ****************************************************************************/
void il4965_update_chain_flags(struct il_priv *il);
extern const u8 il_bcast_addr[ETH_ALEN];
int il_queue_space(const struct il_queue *q);
static inline int
il_queue_used(const struct il_queue *q, int i)
{
	return q->write_ptr >= q->read_ptr ? (i >= q->read_ptr &&
					      i < q->write_ptr) : !(i <
								    q->read_ptr
								    && i >=
								    q->
								    write_ptr);
}

static inline u8
il_get_cmd_idx(struct il_queue *q, u32 idx, int is_huge)
{
	/*
	 * This is for init calibration result and scan command which
	 * required buffer > TFD_MAX_PAYLOAD_SIZE,
	 * the big buffer at end of command array
	 */
	if (is_huge)
		return q->n_win;	/* must be power of 2 */

	/* Otherwise, use normal size buffers */
	return idx & (q->n_win - 1);
}

struct il_dma_ptr {
	dma_addr_t dma;
	void *addr;
	size_t size;
};

#define IL_OPERATION_MODE_AUTO     0
#define IL_OPERATION_MODE_HT_ONLY  1
#define IL_OPERATION_MODE_MIXED    2
#define IL_OPERATION_MODE_20MHZ    3

#define IL_TX_CRC_SIZE 4
#define IL_TX_DELIMITER_SIZE 4

#define TX_POWER_IL_ILLEGAL_VOLTAGE -10000

/* Sensitivity and chain noise calibration */
#define INITIALIZATION_VALUE		0xFFFF
#define IL4965_CAL_NUM_BEACONS		20
#define IL_CAL_NUM_BEACONS		16
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

enum il4965_false_alarm_state {
	IL_FA_TOO_MANY = 0,
	IL_FA_TOO_FEW = 1,
	IL_FA_GOOD_RANGE = 2,
};

enum il4965_chain_noise_state {
	IL_CHAIN_NOISE_ALIVE = 0,	/* must be 0 */
	IL_CHAIN_NOISE_ACCUMULATE,
	IL_CHAIN_NOISE_CALIBRATED,
	IL_CHAIN_NOISE_DONE,
};

enum ucode_type {
	UCODE_NONE = 0,
	UCODE_INIT,
	UCODE_RT
};

/* Sensitivity calib data */
struct il_sensitivity_data {
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
	u8 nrg_silence_rssi[NRG_NUM_PREV_STAT_L];
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
struct il_chain_noise_data {
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

#define	EEPROM_SEM_TIMEOUT 10	/* milliseconds */
#define EEPROM_SEM_RETRY_LIMIT 1000	/* number of attempts (not time) */

#define IL_TRAFFIC_ENTRIES	(256)
#define IL_TRAFFIC_ENTRY_SIZE  (64)

enum {
	MEASUREMENT_READY = (1 << 0),
	MEASUREMENT_ACTIVE = (1 << 1),
};

/* interrupt stats */
struct isr_stats {
	u32 hw;
	u32 sw;
	u32 err_code;
	u32 sch;
	u32 alive;
	u32 rfkill;
	u32 ctkill;
	u32 wakeup;
	u32 rx;
	u32 handlers[IL_CN_MAX];
	u32 tx;
	u32 unhandled;
};

/* management stats */
enum il_mgmt_stats {
	MANAGEMENT_ASSOC_REQ = 0,
	MANAGEMENT_ASSOC_RESP,
	MANAGEMENT_REASSOC_REQ,
	MANAGEMENT_REASSOC_RESP,
	MANAGEMENT_PROBE_REQ,
	MANAGEMENT_PROBE_RESP,
	MANAGEMENT_BEACON,
	MANAGEMENT_ATIM,
	MANAGEMENT_DISASSOC,
	MANAGEMENT_AUTH,
	MANAGEMENT_DEAUTH,
	MANAGEMENT_ACTION,
	MANAGEMENT_MAX,
};
/* control stats */
enum il_ctrl_stats {
	CONTROL_BACK_REQ = 0,
	CONTROL_BACK,
	CONTROL_PSPOLL,
	CONTROL_RTS,
	CONTROL_CTS,
	CONTROL_ACK,
	CONTROL_CFEND,
	CONTROL_CFENDACK,
	CONTROL_MAX,
};

struct traffic_stats {
#ifdef CONFIG_IWLEGACY_DEBUGFS
	u32 mgmt[MANAGEMENT_MAX];
	u32 ctrl[CONTROL_MAX];
	u32 data_cnt;
	u64 data_bytes;
#endif
};

/*
 * host interrupt timeout value
 * used with setting interrupt coalescing timer
 * the CSR_INT_COALESCING is an 8 bit register in 32-usec unit
 *
 * default interrupt coalescing timer is 64 x 32 = 2048 usecs
 * default interrupt coalescing calibration timer is 16 x 32 = 512 usecs
 */
#define IL_HOST_INT_TIMEOUT_MAX	(0xFF)
#define IL_HOST_INT_TIMEOUT_DEF	(0x40)
#define IL_HOST_INT_TIMEOUT_MIN	(0x0)
#define IL_HOST_INT_CALIB_TIMEOUT_MAX	(0xFF)
#define IL_HOST_INT_CALIB_TIMEOUT_DEF	(0x10)
#define IL_HOST_INT_CALIB_TIMEOUT_MIN	(0x0)

#define IL_DELAY_NEXT_FORCE_FW_RELOAD (HZ*5)

/* TX queue watchdog timeouts in mSecs */
#define IL_DEF_WD_TIMEOUT	(2000)
#define IL_LONG_WD_TIMEOUT	(10000)
#define IL_MAX_WD_TIMEOUT	(120000)

struct il_force_reset {
	int reset_request_count;
	int reset_success_count;
	int reset_reject_count;
	unsigned long reset_duration;
	unsigned long last_force_reset_jiffies;
};

/* extend beacon time format bit shifting  */
/*
 * for _3945 devices
 * bits 31:24 - extended
 * bits 23:0  - interval
 */
#define IL3945_EXT_BEACON_TIME_POS	24
/*
 * for _4965 devices
 * bits 31:22 - extended
 * bits 21:0  - interval
 */
#define IL4965_EXT_BEACON_TIME_POS	22

struct il_rxon_context {
	struct ieee80211_vif *vif;
};

struct il_power_mgr {
	struct il_powertable_cmd sleep_cmd;
	struct il_powertable_cmd sleep_cmd_next;
	int debug_sleep_level_override;
	bool pci_pm;
	bool ps_disabled;
};

struct il_priv {
	struct ieee80211_hw *hw;
	struct ieee80211_channel *ieee_channels;
	struct ieee80211_rate *ieee_rates;

	struct il_cfg *cfg;
	const struct il_ops *ops;
#ifdef CONFIG_IWLEGACY_DEBUGFS
	const struct il_debugfs_ops *debugfs_ops;
#endif

	/* temporary frame storage list */
	struct list_head free_frames;
	int frames_count;

	enum nl80211_band band;
	int alloc_rxb_page;

	void (*handlers[IL_CN_MAX]) (struct il_priv *il,
				     struct il_rx_buf *rxb);

	struct ieee80211_supported_band bands[NUM_NL80211_BANDS];

	/* spectrum measurement report caching */
	struct il_spectrum_notification measure_report;
	u8 measurement_status;

	/* ucode beacon time */
	u32 ucode_beacon_time;
	int missed_beacon_threshold;

	/* track IBSS manager (last beacon) status */
	u32 ibss_manager;

	/* force reset */
	struct il_force_reset force_reset;

	/* we allocate array of il_channel_info for NIC's valid channels.
	 *    Access via channel # using indirect idx array */
	struct il_channel_info *channel_info;	/* channel info array */
	u8 channel_count;	/* # of channels */

	/* thermal calibration */
	s32 temperature;	/* degrees Kelvin */
	s32 last_temperature;

	/* Scan related variables */
	unsigned long scan_start;
	unsigned long scan_start_tsf;
	void *scan_cmd;
	enum nl80211_band scan_band;
	struct cfg80211_scan_request *scan_request;
	struct ieee80211_vif *scan_vif;
	u8 scan_tx_ant[NUM_NL80211_BANDS];
	u8 mgmt_tx_ant;

	/* spinlock */
	spinlock_t lock;	/* protect general shared data */
	spinlock_t hcmd_lock;	/* protect hcmd */
	spinlock_t reg_lock;	/* protect hw register access */
	struct mutex mutex;

	/* basic pci-network driver stuff */
	struct pci_dev *pci_dev;

	/* pci hardware address support */
	void __iomem *hw_base;
	u32 hw_rev;
	u32 hw_wa_rev;
	u8 rev_id;

	/* command queue number */
	u8 cmd_queue;

	/* max number of station keys */
	u8 sta_key_max_num;

	/* EEPROM MAC addresses */
	struct mac_address addresses[1];

	/* uCode images, save to reload in case of failure */
	int fw_idx;		/* firmware we're trying to load */
	u32 ucode_ver;		/* version of ucode, copy of
				   il_ucode.ver */
	struct fw_desc ucode_code;	/* runtime inst */
	struct fw_desc ucode_data;	/* runtime data original */
	struct fw_desc ucode_data_backup;	/* runtime data save/restore */
	struct fw_desc ucode_init;	/* initialization inst */
	struct fw_desc ucode_init_data;	/* initialization data */
	struct fw_desc ucode_boot;	/* bootstrap inst */
	enum ucode_type ucode_type;
	u8 ucode_write_complete;	/* the image write is complete */
	char firmware_name[25];

	struct ieee80211_vif *vif;

	struct il_qos_info qos_data;

	struct {
		bool enabled;
		bool is_40mhz;
		bool non_gf_sta_present;
		u8 protection;
		u8 extension_chan_offset;
	} ht;

	/*
	 * We declare this const so it can only be
	 * changed via explicit cast within the
	 * routines that actually update the physical
	 * hardware.
	 */
	const struct il_rxon_cmd active;
	struct il_rxon_cmd staging;

	struct il_rxon_time_cmd timing;

	__le16 switch_channel;

	/* 1st responses from initialize and runtime uCode images.
	 * _4965's initialize alive response contains some calibration data. */
	struct il_init_alive_resp card_alive_init;
	struct il_alive_resp card_alive;

	u16 active_rate;

	u8 start_calib;
	struct il_sensitivity_data sensitivity_data;
	struct il_chain_noise_data chain_noise_data;
	__le16 sensitivity_tbl[HD_TBL_SIZE];

	struct il_ht_config current_ht_config;

	/* Rate scaling data */
	u8 retry_rate;

	wait_queue_head_t wait_command_queue;

	int activity_timer_active;

	/* Rx and Tx DMA processing queues */
	struct il_rx_queue rxq;
	struct il_tx_queue *txq;
	unsigned long txq_ctx_active_msk;
	struct il_dma_ptr kw;	/* keep warm address */
	struct il_dma_ptr scd_bc_tbls;

	u32 scd_base_addr;	/* scheduler sram base address */

	unsigned long status;

	/* counts mgmt, ctl, and data packets */
	struct traffic_stats tx_stats;
	struct traffic_stats rx_stats;

	/* counts interrupts */
	struct isr_stats isr_stats;

	struct il_power_mgr power_data;

	/* context information */
	u8 bssid[ETH_ALEN];	/* used only on 3945 but filled by core */

	/* station table variables */

	/* Note: if lock and sta_lock are needed, lock must be acquired first */
	spinlock_t sta_lock;
	int num_stations;
	struct il_station_entry stations[IL_STATION_COUNT];
	unsigned long ucode_key_table;

	/* queue refcounts */
#define IL_MAX_HW_QUEUES	32
	unsigned long queue_stopped[BITS_TO_LONGS(IL_MAX_HW_QUEUES)];
#define IL_STOP_REASON_PASSIVE	0
	unsigned long stop_reason;
	/* for each AC */
	atomic_t queue_stop_count[4];

	/* Indication if ieee80211_ops->open has been called */
	u8 is_open;

	u8 mac80211_registered;

	/* eeprom -- this is in the card's little endian byte order */
	u8 *eeprom;
	struct il_eeprom_calib_info *calib_info;

	enum nl80211_iftype iw_mode;

	/* Last Rx'd beacon timestamp */
	u64 timestamp;

	union {
#if IS_ENABLED(CONFIG_IWL3945)
		struct {
			void *shared_virt;
			dma_addr_t shared_phys;

			struct delayed_work thermal_periodic;
			struct delayed_work rfkill_poll;

			struct il3945_notif_stats stats;
#ifdef CONFIG_IWLEGACY_DEBUGFS
			struct il3945_notif_stats accum_stats;
			struct il3945_notif_stats delta_stats;
			struct il3945_notif_stats max_delta;
#endif

			u32 sta_supp_rates;
			int last_rx_rssi;	/* From Rx packet stats */

			/* Rx'd packet timing information */
			u32 last_beacon_time;
			u64 last_tsf;

			/*
			 * each calibration channel group in the
			 * EEPROM has a derived clip setting for
			 * each rate.
			 */
			const struct il3945_clip_group clip_groups[5];

		} _3945;
#endif
#if IS_ENABLED(CONFIG_IWL4965)
		struct {
			struct il_rx_phy_res last_phy_res;
			bool last_phy_res_valid;
			u32 ampdu_ref;

			struct completion firmware_loading_complete;

			/*
			 * chain noise reset and gain commands are the
			 * two extra calibration commands follows the standard
			 * phy calibration commands
			 */
			u8 phy_calib_chain_noise_reset_cmd;
			u8 phy_calib_chain_noise_gain_cmd;

			u8 key_mapping_keys;
			struct il_wep_key wep_keys[WEP_KEYS_MAX];

			struct il_notif_stats stats;
#ifdef CONFIG_IWLEGACY_DEBUGFS
			struct il_notif_stats accum_stats;
			struct il_notif_stats delta_stats;
			struct il_notif_stats max_delta;
#endif

		} _4965;
#endif
	};

	struct il_hw_params hw_params;

	u32 inta_mask;

	struct workqueue_struct *workqueue;

	struct work_struct restart;
	struct work_struct scan_completed;
	struct work_struct rx_replenish;
	struct work_struct abort_scan;

	bool beacon_enabled;
	struct sk_buff *beacon_skb;

	struct work_struct tx_flush;

	struct tasklet_struct irq_tasklet;

	struct delayed_work init_alive_start;
	struct delayed_work alive_start;
	struct delayed_work scan_check;

	/* TX Power */
	s8 tx_power_user_lmt;
	s8 tx_power_device_lmt;
	s8 tx_power_next;

#ifdef CONFIG_IWLEGACY_DEBUG
	/* debugging info */
	u32 debug_level;	/* per device debugging will override global
				   il_debug_level if set */
#endif				/* CONFIG_IWLEGACY_DEBUG */
#ifdef CONFIG_IWLEGACY_DEBUGFS
	/* debugfs */
	u16 tx_traffic_idx;
	u16 rx_traffic_idx;
	u8 *tx_traffic;
	u8 *rx_traffic;
	struct dentry *debugfs_dir;
	u32 dbgfs_sram_offset, dbgfs_sram_len;
	bool disable_ht40;
#endif				/* CONFIG_IWLEGACY_DEBUGFS */

	struct work_struct txpower_work;
	bool disable_sens_cal;
	bool disable_chain_noise_cal;
	bool disable_tx_power_cal;
	struct work_struct run_time_calib_work;
	struct timer_list stats_periodic;
	struct timer_list watchdog;
	bool hw_ready;

	struct led_classdev led;
	unsigned long blink_on, blink_off;
	bool led_registered;
};				/*il_priv */

static inline void
il_txq_ctx_activate(struct il_priv *il, int txq_id)
{
	set_bit(txq_id, &il->txq_ctx_active_msk);
}

static inline void
il_txq_ctx_deactivate(struct il_priv *il, int txq_id)
{
	clear_bit(txq_id, &il->txq_ctx_active_msk);
}

static inline int
il_is_associated(struct il_priv *il)
{
	return (il->active.filter_flags & RXON_FILTER_ASSOC_MSK) ? 1 : 0;
}

static inline int
il_is_any_associated(struct il_priv *il)
{
	return il_is_associated(il);
}

static inline int
il_is_channel_valid(const struct il_channel_info *ch_info)
{
	if (ch_info == NULL)
		return 0;
	return (ch_info->flags & EEPROM_CHANNEL_VALID) ? 1 : 0;
}

static inline int
il_is_channel_radar(const struct il_channel_info *ch_info)
{
	return (ch_info->flags & EEPROM_CHANNEL_RADAR) ? 1 : 0;
}

static inline u8
il_is_channel_a_band(const struct il_channel_info *ch_info)
{
	return ch_info->band == NL80211_BAND_5GHZ;
}

static inline int
il_is_channel_passive(const struct il_channel_info *ch)
{
	return (!(ch->flags & EEPROM_CHANNEL_ACTIVE)) ? 1 : 0;
}

static inline int
il_is_channel_ibss(const struct il_channel_info *ch)
{
	return (ch->flags & EEPROM_CHANNEL_IBSS) ? 1 : 0;
}

static inline void
__il_free_pages(struct il_priv *il, struct page *page)
{
	__free_pages(page, il->hw_params.rx_page_order);
	il->alloc_rxb_page--;
}

static inline void
il_free_pages(struct il_priv *il, unsigned long page)
{
	free_pages(page, il->hw_params.rx_page_order);
	il->alloc_rxb_page--;
}

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

/* Size of one Rx buffer in host DRAM */
#define IL_RX_BUF_SIZE_3K (3 * 1000)	/* 3945 only */
#define IL_RX_BUF_SIZE_4K (4 * 1024)
#define IL_RX_BUF_SIZE_8K (8 * 1024)

#ifdef CONFIG_IWLEGACY_DEBUGFS
struct il_debugfs_ops {
	ssize_t(*rx_stats_read) (struct file *file, char __user *user_buf,
				 size_t count, loff_t *ppos);
	ssize_t(*tx_stats_read) (struct file *file, char __user *user_buf,
				 size_t count, loff_t *ppos);
	ssize_t(*general_stats_read) (struct file *file,
				      char __user *user_buf, size_t count,
				      loff_t *ppos);
};
#endif

struct il_ops {
	/* Handling TX */
	void (*txq_update_byte_cnt_tbl) (struct il_priv *il,
					 struct il_tx_queue *txq,
					 u16 byte_cnt);
	int (*txq_attach_buf_to_tfd) (struct il_priv *il,
				      struct il_tx_queue *txq, dma_addr_t addr,
				      u16 len, u8 reset, u8 pad);
	void (*txq_free_tfd) (struct il_priv *il, struct il_tx_queue *txq);
	int (*txq_init) (struct il_priv *il, struct il_tx_queue *txq);
	/* alive notification after init uCode load */
	void (*init_alive_start) (struct il_priv *il);
	/* check validity of rtc data address */
	int (*is_valid_rtc_data_addr) (u32 addr);
	/* 1st ucode load */
	int (*load_ucode) (struct il_priv *il);

	void (*dump_nic_error_log) (struct il_priv *il);
	int (*dump_fh) (struct il_priv *il, char **buf, bool display);
	int (*set_channel_switch) (struct il_priv *il,
				   struct ieee80211_channel_switch *ch_switch);
	/* power management */
	int (*apm_init) (struct il_priv *il);

	/* tx power */
	int (*send_tx_power) (struct il_priv *il);
	void (*update_chain_flags) (struct il_priv *il);

	/* eeprom operations */
	int (*eeprom_acquire_semaphore) (struct il_priv *il);
	void (*eeprom_release_semaphore) (struct il_priv *il);

	int (*rxon_assoc) (struct il_priv *il);
	int (*commit_rxon) (struct il_priv *il);
	void (*set_rxon_chain) (struct il_priv *il);

	u16(*get_hcmd_size) (u8 cmd_id, u16 len);
	u16(*build_addsta_hcmd) (const struct il_addsta_cmd *cmd, u8 *data);

	int (*request_scan) (struct il_priv *il, struct ieee80211_vif *vif);
	void (*post_scan) (struct il_priv *il);
	void (*post_associate) (struct il_priv *il);
	void (*config_ap) (struct il_priv *il);
	/* station management */
	int (*update_bcast_stations) (struct il_priv *il);
	int (*manage_ibss_station) (struct il_priv *il,
				    struct ieee80211_vif *vif, bool add);

	int (*send_led_cmd) (struct il_priv *il, struct il_led_cmd *led_cmd);
};

struct il_mod_params {
	int sw_crypto;		/* def: 0 = using hardware encryption */
	int disable_hw_scan;	/* def: 0 = use h/w scan */
	int num_of_queues;	/* def: HW dependent */
	int disable_11n;	/* def: 0 = 11n capabilities enabled */
	int amsdu_size_8K;	/* def: 0 = disable 8K amsdu size */
	int antenna;		/* def: 0 = both antennas (use diversity) */
	int restart_fw;		/* def: 1 = restart firmware */
};

#define IL_LED_SOLID 11
#define IL_DEF_LED_INTRVL cpu_to_le32(1000)

#define IL_LED_ACTIVITY       (0<<1)
#define IL_LED_LINK           (1<<1)

/*
 * LED mode
 *    IL_LED_DEFAULT:  use device default
 *    IL_LED_RF_STATE: turn LED on/off based on RF state
 *			LED ON  = RF ON
 *			LED OFF = RF OFF
 *    IL_LED_BLINK:    adjust led blink rate based on blink table
 */
enum il_led_mode {
	IL_LED_DEFAULT,
	IL_LED_RF_STATE,
	IL_LED_BLINK,
};

void il_leds_init(struct il_priv *il);
void il_leds_exit(struct il_priv *il);

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
	u8 valid_tx_ant;
	u8 valid_rx_ant;
	unsigned int sku;
	u16 eeprom_ver;
	u16 eeprom_calib_ver;
	/* module based parameters which can be set from modprobe cmd */
	const struct il_mod_params *mod_params;
	/* params not likely to change within a device family */
	struct il_base_params *base_params;
	/* params likely to change within a device family */
	u8 scan_rx_antennas[NUM_NL80211_BANDS];
	enum il_led_mode led_mode;

	int eeprom_size;
	int num_of_queues;		/* def: HW dependent */
	int num_of_ampdu_queues;	/* def: HW dependent */
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

	const u32 regulatory_bands[7];
};

/***************************
 *   L i b                 *
 ***************************/

int il_mac_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   unsigned int link_id, u16 queue,
		   const struct ieee80211_tx_queue_params *params);
int il_mac_tx_last_beacon(struct ieee80211_hw *hw);

void il_set_rxon_hwcrypto(struct il_priv *il, int hw_decrypt);
int il_check_rxon_cmd(struct il_priv *il);
int il_full_rxon_required(struct il_priv *il);
int il_set_rxon_channel(struct il_priv *il, struct ieee80211_channel *ch);
void il_set_flags_for_band(struct il_priv *il, enum nl80211_band band,
			   struct ieee80211_vif *vif);
u8 il_get_single_channel_number(struct il_priv *il, enum nl80211_band band);
void il_set_rxon_ht(struct il_priv *il, struct il_ht_config *ht_conf);
bool il_is_ht40_tx_allowed(struct il_priv *il,
			   struct ieee80211_sta_ht_cap *ht_cap);
void il_connection_init_rx_config(struct il_priv *il);
void il_set_rate(struct il_priv *il);
int il_set_decrypted_flag(struct il_priv *il, struct ieee80211_hdr *hdr,
			  u32 decrypt_res, struct ieee80211_rx_status *stats);
void il_irq_handle_error(struct il_priv *il);
int il_mac_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
void il_mac_remove_interface(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif);
int il_mac_change_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    enum nl80211_iftype newtype, bool newp2p);
void il_mac_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  u32 queues, bool drop);
int il_alloc_txq_mem(struct il_priv *il);
void il_free_txq_mem(struct il_priv *il);

#ifdef CONFIG_IWLEGACY_DEBUGFS
void il_update_stats(struct il_priv *il, bool is_tx, __le16 fc, u16 len);
#else
static inline void
il_update_stats(struct il_priv *il, bool is_tx, __le16 fc, u16 len)
{
}
#endif

/*****************************************************
 * Handlers
 ***************************************************/
void il_hdl_pm_sleep(struct il_priv *il, struct il_rx_buf *rxb);
void il_hdl_pm_debug_stats(struct il_priv *il, struct il_rx_buf *rxb);
void il_hdl_error(struct il_priv *il, struct il_rx_buf *rxb);
void il_hdl_csa(struct il_priv *il, struct il_rx_buf *rxb);

/*****************************************************
* RX
******************************************************/
void il_cmd_queue_unmap(struct il_priv *il);
void il_cmd_queue_free(struct il_priv *il);
int il_rx_queue_alloc(struct il_priv *il);
void il_rx_queue_update_write_ptr(struct il_priv *il, struct il_rx_queue *q);
int il_rx_queue_space(const struct il_rx_queue *q);
void il_tx_cmd_complete(struct il_priv *il, struct il_rx_buf *rxb);

void il_hdl_spectrum_measurement(struct il_priv *il, struct il_rx_buf *rxb);
void il_recover_from_stats(struct il_priv *il, struct il_rx_pkt *pkt);
void il_chswitch_done(struct il_priv *il, bool is_success);

/*****************************************************
* TX
******************************************************/
void il_txq_update_write_ptr(struct il_priv *il, struct il_tx_queue *txq);
int il_tx_queue_init(struct il_priv *il, u32 txq_id);
void il_tx_queue_reset(struct il_priv *il, u32 txq_id);
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

u8 il_get_lowest_plcp(struct il_priv *il);

/*******************************************************************************
 * Scanning
 ******************************************************************************/
void il_init_scan_params(struct il_priv *il);
int il_scan_cancel(struct il_priv *il);
int il_scan_cancel_timeout(struct il_priv *il, unsigned long ms);
void il_force_scan_end(struct il_priv *il);
int il_mac_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   struct ieee80211_scan_request *hw_req);
void il_internal_short_hw_scan(struct il_priv *il);
int il_force_reset(struct il_priv *il, bool external);
u16 il_fill_probe_req(struct il_priv *il, struct ieee80211_mgmt *frame,
		      const u8 *ta, const u8 *ie, int ie_len, int left);
void il_setup_rx_scan_handlers(struct il_priv *il);
u16 il_get_active_dwell_time(struct il_priv *il, enum nl80211_band band,
			     u8 n_probes);
u16 il_get_passive_dwell_time(struct il_priv *il, enum nl80211_band band,
			      struct ieee80211_vif *vif);
void il_setup_scan_deferred_work(struct il_priv *il);
void il_cancel_scan_deferred_work(struct il_priv *il);

/* For faster active scanning, scan will move to the next channel if fewer than
 * PLCP_QUIET_THRESH packets are heard on this channel within
 * ACTIVE_QUIET_TIME after sending probe request.  This shortens the dwell
 * time if it's a quiet channel (nothing responded to our probe, and there's
 * no other traffic).
 * Disable "quiet" feature by setting PLCP_QUIET_THRESH to 0. */
#define IL_ACTIVE_QUIET_TIME       cpu_to_le16(10)	/* msec */
#define IL_PLCP_QUIET_THRESH       cpu_to_le16(1)	/* packets */

#define IL_SCAN_CHECK_WATCHDOG		(HZ * 7)

/*****************************************************
 *   S e n d i n g     H o s t     C o m m a n d s   *
 *****************************************************/

const char *il_get_cmd_string(u8 cmd);
int __must_check il_send_cmd_sync(struct il_priv *il, struct il_host_cmd *cmd);
int il_send_cmd(struct il_priv *il, struct il_host_cmd *cmd);
int __must_check il_send_cmd_pdu(struct il_priv *il, u8 id, u16 len,
				 const void *data);
int il_send_cmd_pdu_async(struct il_priv *il, u8 id, u16 len, const void *data,
			  void (*callback) (struct il_priv *il,
					    struct il_device_cmd *cmd,
					    struct il_rx_pkt *pkt));

int il_enqueue_hcmd(struct il_priv *il, struct il_host_cmd *cmd);

/*****************************************************
 * PCI						     *
 *****************************************************/

void il_bg_watchdog(struct timer_list *t);
u32 il_usecs_to_beacons(struct il_priv *il, u32 usec, u32 beacon_interval);
__le32 il_add_beacon_time(struct il_priv *il, u32 base, u32 addon,
			  u32 beacon_interval);

#ifdef CONFIG_PM_SLEEP
extern const struct dev_pm_ops il_pm_ops;

#define IL_LEGACY_PM_OPS	(&il_pm_ops)

#else /* !CONFIG_PM_SLEEP */

#define IL_LEGACY_PM_OPS	NULL

#endif /* !CONFIG_PM_SLEEP */

/*****************************************************
*  Error Handling Debugging
******************************************************/
void il4965_dump_nic_error_log(struct il_priv *il);
#ifdef CONFIG_IWLEGACY_DEBUG
void il_print_rx_config_cmd(struct il_priv *il);
#else
static inline void
il_print_rx_config_cmd(struct il_priv *il)
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

#define S_HCMD_ACTIVE	0	/* host command in progress */
/* 1 is unused (used to be S_HCMD_SYNC_ACTIVE) */
#define S_INT_ENABLED	2
#define S_RFKILL	3
#define S_CT_KILL		4
#define S_INIT		5
#define S_ALIVE		6
#define S_READY		7
#define S_TEMPERATURE	8
#define S_GEO_CONFIGURED	9
#define S_EXIT_PENDING	10
#define S_STATS		12
#define S_SCANNING		13
#define S_SCAN_ABORTING	14
#define S_SCAN_HW		15
#define S_POWER_PMI	16
#define S_FW_ERROR		17
#define S_CHANNEL_SWITCH_PENDING 18

static inline int
il_is_ready(struct il_priv *il)
{
	/* The adapter is 'ready' if READY and GEO_CONFIGURED bits are
	 * set but EXIT_PENDING is not */
	return test_bit(S_READY, &il->status) &&
	    test_bit(S_GEO_CONFIGURED, &il->status) &&
	    !test_bit(S_EXIT_PENDING, &il->status);
}

static inline int
il_is_alive(struct il_priv *il)
{
	return test_bit(S_ALIVE, &il->status);
}

static inline int
il_is_init(struct il_priv *il)
{
	return test_bit(S_INIT, &il->status);
}

static inline int
il_is_rfkill(struct il_priv *il)
{
	return test_bit(S_RFKILL, &il->status);
}

static inline int
il_is_ctkill(struct il_priv *il)
{
	return test_bit(S_CT_KILL, &il->status);
}

static inline int
il_is_ready_rf(struct il_priv *il)
{

	if (il_is_rfkill(il))
		return 0;

	return il_is_ready(il);
}

void il_send_bt_config(struct il_priv *il);
int il_send_stats_request(struct il_priv *il, u8 flags, bool clear);
void il_apm_stop(struct il_priv *il);
void _il_apm_stop(struct il_priv *il);

int il_apm_init(struct il_priv *il);

int il_send_rxon_timing(struct il_priv *il);

static inline int
il_send_rxon_assoc(struct il_priv *il)
{
	return il->ops->rxon_assoc(il);
}

static inline int
il_commit_rxon(struct il_priv *il)
{
	return il->ops->commit_rxon(il);
}

static inline const struct ieee80211_supported_band *
il_get_hw_mode(struct il_priv *il, enum nl80211_band band)
{
	return il->hw->wiphy->bands[band];
}

/* mac80211 handlers */
int il_mac_config(struct ieee80211_hw *hw, u32 changed);
void il_mac_reset_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
void il_mac_bss_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *bss_conf, u64 changes);
void il_tx_cmd_protection(struct il_priv *il, struct ieee80211_tx_info *info,
			  __le16 fc, __le32 *tx_flags);

irqreturn_t il_isr(int irq, void *data);

void il_set_bit(struct il_priv *p, u32 r, u32 m);
void il_clear_bit(struct il_priv *p, u32 r, u32 m);
bool _il_grab_nic_access(struct il_priv *il);
int _il_poll_bit(struct il_priv *il, u32 addr, u32 bits, u32 mask, int timeout);
int il_poll_bit(struct il_priv *il, u32 addr, u32 mask, int timeout);
u32 il_rd_prph(struct il_priv *il, u32 reg);
void il_wr_prph(struct il_priv *il, u32 addr, u32 val);
u32 il_read_targ_mem(struct il_priv *il, u32 addr);
void il_write_targ_mem(struct il_priv *il, u32 addr, u32 val);

static inline bool il_need_reclaim(struct il_priv *il, struct il_rx_pkt *pkt)
{
	/* Reclaim a command buffer only if this packet is a response
	 * to a (driver-originated) command. If the packet (e.g. Rx frame)
	 * originated from uCode, there is no command buffer to reclaim.
	 * Ucode should set SEQ_RX_FRAME bit if ucode-originated, but
	 * apparently a few don't get set; catch them here.
	 */
	return !(pkt->hdr.sequence & SEQ_RX_FRAME) &&
	       pkt->hdr.cmd != N_STATS && pkt->hdr.cmd != C_TX &&
	       pkt->hdr.cmd != N_RX_PHY && pkt->hdr.cmd != N_RX &&
	       pkt->hdr.cmd != N_RX_MPDU && pkt->hdr.cmd != N_COMPRESSED_BA;
}

static inline void
_il_write8(struct il_priv *il, u32 ofs, u8 val)
{
	writeb(val, il->hw_base + ofs);
}
#define il_write8(il, ofs, val) _il_write8(il, ofs, val)

static inline void
_il_wr(struct il_priv *il, u32 ofs, u32 val)
{
	writel(val, il->hw_base + ofs);
}

static inline u32
_il_rd(struct il_priv *il, u32 ofs)
{
	return readl(il->hw_base + ofs);
}

static inline void
_il_clear_bit(struct il_priv *il, u32 reg, u32 mask)
{
	_il_wr(il, reg, _il_rd(il, reg) & ~mask);
}

static inline void
_il_set_bit(struct il_priv *il, u32 reg, u32 mask)
{
	_il_wr(il, reg, _il_rd(il, reg) | mask);
}

static inline void
_il_release_nic_access(struct il_priv *il)
{
	_il_clear_bit(il, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
}

static inline u32
il_rd(struct il_priv *il, u32 reg)
{
	u32 value;
	unsigned long reg_flags;

	spin_lock_irqsave(&il->reg_lock, reg_flags);
	_il_grab_nic_access(il);
	value = _il_rd(il, reg);
	_il_release_nic_access(il);
	spin_unlock_irqrestore(&il->reg_lock, reg_flags);
	return value;
}

static inline void
il_wr(struct il_priv *il, u32 reg, u32 value)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&il->reg_lock, reg_flags);
	if (likely(_il_grab_nic_access(il))) {
		_il_wr(il, reg, value);
		_il_release_nic_access(il);
	}
	spin_unlock_irqrestore(&il->reg_lock, reg_flags);
}

static inline u32
_il_rd_prph(struct il_priv *il, u32 reg)
{
	_il_wr(il, HBUS_TARG_PRPH_RADDR, reg | (3 << 24));
	return _il_rd(il, HBUS_TARG_PRPH_RDAT);
}

static inline void
_il_wr_prph(struct il_priv *il, u32 addr, u32 val)
{
	_il_wr(il, HBUS_TARG_PRPH_WADDR, ((addr & 0x0000FFFF) | (3 << 24)));
	_il_wr(il, HBUS_TARG_PRPH_WDAT, val);
}

static inline void
il_set_bits_prph(struct il_priv *il, u32 reg, u32 mask)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&il->reg_lock, reg_flags);
	if (likely(_il_grab_nic_access(il))) {
		_il_wr_prph(il, reg, (_il_rd_prph(il, reg) | mask));
		_il_release_nic_access(il);
	}
	spin_unlock_irqrestore(&il->reg_lock, reg_flags);
}

static inline void
il_set_bits_mask_prph(struct il_priv *il, u32 reg, u32 bits, u32 mask)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&il->reg_lock, reg_flags);
	if (likely(_il_grab_nic_access(il))) {
		_il_wr_prph(il, reg, ((_il_rd_prph(il, reg) & mask) | bits));
		_il_release_nic_access(il);
	}
	spin_unlock_irqrestore(&il->reg_lock, reg_flags);
}

static inline void
il_clear_bits_prph(struct il_priv *il, u32 reg, u32 mask)
{
	unsigned long reg_flags;
	u32 val;

	spin_lock_irqsave(&il->reg_lock, reg_flags);
	if (likely(_il_grab_nic_access(il))) {
		val = _il_rd_prph(il, reg);
		_il_wr_prph(il, reg, (val & ~mask));
		_il_release_nic_access(il);
	}
	spin_unlock_irqrestore(&il->reg_lock, reg_flags);
}

#define HW_KEY_DYNAMIC 0
#define HW_KEY_DEFAULT 1

#define IL_STA_DRIVER_ACTIVE BIT(0)	/* driver entry is active */
#define IL_STA_UCODE_ACTIVE  BIT(1)	/* ucode entry is active */
#define IL_STA_UCODE_INPROGRESS  BIT(2)	/* ucode entry is in process of
					   being activated */
#define IL_STA_LOCAL BIT(3)	/* station state not directed by mac80211;
				   (this is for the IBSS BSSID stations) */
#define IL_STA_BCAST BIT(4)	/* this station is the special bcast station */

void il_restore_stations(struct il_priv *il);
void il_clear_ucode_stations(struct il_priv *il);
void il_dealloc_bcast_stations(struct il_priv *il);
int il_get_free_ucode_key_idx(struct il_priv *il);
int il_send_add_sta(struct il_priv *il, struct il_addsta_cmd *sta, u8 flags);
int il_add_station_common(struct il_priv *il, const u8 *addr, bool is_ap,
			  struct ieee80211_sta *sta, u8 *sta_id_r);
int il_remove_station(struct il_priv *il, const u8 sta_id, const u8 * addr);
int il_mac_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      struct ieee80211_sta *sta);

u8 il_prep_station(struct il_priv *il, const u8 *addr, bool is_ap,
		   struct ieee80211_sta *sta);

int il_send_lq_cmd(struct il_priv *il, struct il_link_quality_cmd *lq,
		   u8 flags, bool init);

/**
 * il_clear_driver_stations - clear knowledge of all stations from driver
 * @il: iwl il struct
 *
 * This is called during il_down() to make sure that in the case
 * we're coming there from a hardware restart mac80211 will be
 * able to reconfigure stations -- if we're getting there in the
 * normal down flow then the stations will already be cleared.
 */
static inline void
il_clear_driver_stations(struct il_priv *il)
{
	unsigned long flags;

	spin_lock_irqsave(&il->sta_lock, flags);
	memset(il->stations, 0, sizeof(il->stations));
	il->num_stations = 0;
	il->ucode_key_table = 0;
	spin_unlock_irqrestore(&il->sta_lock, flags);
}

static inline int
il_sta_id(struct ieee80211_sta *sta)
{
	if (WARN_ON(!sta))
		return IL_INVALID_STATION;

	return ((struct il_station_priv_common *)sta->drv_priv)->sta_id;
}

/**
 * il_sta_id_or_broadcast - return sta_id or broadcast sta
 * @il: iwl il
 * @context: the current context
 * @sta: mac80211 station
 *
 * In certain circumstances mac80211 passes a station pointer
 * that may be %NULL, for example during TX or key setup. In
 * that case, we need to use the broadcast station, so this
 * inline wraps that pattern.
 */
static inline int
il_sta_id_or_broadcast(struct il_priv *il, struct ieee80211_sta *sta)
{
	int sta_id;

	if (!sta)
		return il->hw_params.bcast_id;

	sta_id = il_sta_id(sta);

	/*
	 * mac80211 should not be passing a partially
	 * initialised station!
	 */
	WARN_ON(sta_id == IL_INVALID_STATION);

	return sta_id;
}

/**
 * il_queue_inc_wrap - increment queue idx, wrap back to beginning
 * @idx -- current idx
 * @n_bd -- total number of entries in queue (must be power of 2)
 */
static inline int
il_queue_inc_wrap(int idx, int n_bd)
{
	return ++idx & (n_bd - 1);
}

/**
 * il_queue_dec_wrap - decrement queue idx, wrap back to end
 * @idx -- current idx
 * @n_bd -- total number of entries in queue (must be power of 2)
 */
static inline int
il_queue_dec_wrap(int idx, int n_bd)
{
	return --idx & (n_bd - 1);
}

/* TODO: Move fw_desc functions to iwl-pci.ko */
static inline void
il_free_fw_desc(struct pci_dev *pci_dev, struct fw_desc *desc)
{
	if (desc->v_addr)
		dma_free_coherent(&pci_dev->dev, desc->len, desc->v_addr,
				  desc->p_addr);
	desc->v_addr = NULL;
	desc->len = 0;
}

static inline int
il_alloc_fw_desc(struct pci_dev *pci_dev, struct fw_desc *desc)
{
	if (!desc->len) {
		desc->v_addr = NULL;
		return -EINVAL;
	}

	desc->v_addr = dma_alloc_coherent(&pci_dev->dev, desc->len,
					  &desc->p_addr, GFP_KERNEL);
	return (desc->v_addr != NULL) ? 0 : -ENOMEM;
}

/*
 * we have 8 bits used like this:
 *
 * 7 6 5 4 3 2 1 0
 * | | | | | | | |
 * | | | | | | +-+-------- AC queue (0-3)
 * | | | | | |
 * | +-+-+-+-+------------ HW queue ID
 * |
 * +---------------------- unused
 */
static inline void
il_set_swq_id(struct il_tx_queue *txq, u8 ac, u8 hwq)
{
	BUG_ON(ac > 3);		/* only have 2 bits */
	BUG_ON(hwq > 31);	/* only use 5 bits */

	txq->swq_id = (hwq << 2) | ac;
}

static inline void
_il_wake_queue(struct il_priv *il, u8 ac)
{
	if (atomic_dec_return(&il->queue_stop_count[ac]) <= 0)
		ieee80211_wake_queue(il->hw, ac);
}

static inline void
_il_stop_queue(struct il_priv *il, u8 ac)
{
	if (atomic_inc_return(&il->queue_stop_count[ac]) > 0)
		ieee80211_stop_queue(il->hw, ac);
}
static inline void
il_wake_queue(struct il_priv *il, struct il_tx_queue *txq)
{
	u8 queue = txq->swq_id;
	u8 ac = queue & 3;
	u8 hwq = (queue >> 2) & 0x1f;

	if (test_and_clear_bit(hwq, il->queue_stopped))
		_il_wake_queue(il, ac);
}

static inline void
il_stop_queue(struct il_priv *il, struct il_tx_queue *txq)
{
	u8 queue = txq->swq_id;
	u8 ac = queue & 3;
	u8 hwq = (queue >> 2) & 0x1f;

	if (!test_and_set_bit(hwq, il->queue_stopped))
		_il_stop_queue(il, ac);
}

static inline void
il_wake_queues_by_reason(struct il_priv *il, int reason)
{
	u8 ac;

	if (test_and_clear_bit(reason, &il->stop_reason))
		for (ac = 0; ac < 4; ac++)
			_il_wake_queue(il, ac);
}

static inline void
il_stop_queues_by_reason(struct il_priv *il, int reason)
{
	u8 ac;

	if (!test_and_set_bit(reason, &il->stop_reason))
		for (ac = 0; ac < 4; ac++)
			_il_stop_queue(il, ac);
}

#ifdef ieee80211_stop_queue
#undef ieee80211_stop_queue
#endif

#define ieee80211_stop_queue DO_NOT_USE_ieee80211_stop_queue

#ifdef ieee80211_wake_queue
#undef ieee80211_wake_queue
#endif

#define ieee80211_wake_queue DO_NOT_USE_ieee80211_wake_queue

static inline void
il_disable_interrupts(struct il_priv *il)
{
	clear_bit(S_INT_ENABLED, &il->status);

	/* disable interrupts from uCode/NIC to host */
	_il_wr(il, CSR_INT_MASK, 0x00000000);

	/* acknowledge/clear/reset any interrupts still pending
	 * from uCode or flow handler (Rx/Tx DMA) */
	_il_wr(il, CSR_INT, 0xffffffff);
	_il_wr(il, CSR_FH_INT_STATUS, 0xffffffff);
}

static inline void
il_enable_rfkill_int(struct il_priv *il)
{
	_il_wr(il, CSR_INT_MASK, CSR_INT_BIT_RF_KILL);
}

static inline void
il_enable_interrupts(struct il_priv *il)
{
	set_bit(S_INT_ENABLED, &il->status);
	_il_wr(il, CSR_INT_MASK, il->inta_mask);
}

/**
 * il_beacon_time_mask_low - mask of lower 32 bit of beacon time
 * @il -- pointer to il_priv data structure
 * @tsf_bits -- number of bits need to shift for masking)
 */
static inline u32
il_beacon_time_mask_low(struct il_priv *il, u16 tsf_bits)
{
	return (1 << tsf_bits) - 1;
}

/**
 * il_beacon_time_mask_high - mask of higher 32 bit of beacon time
 * @il -- pointer to il_priv data structure
 * @tsf_bits -- number of bits need to shift for masking)
 */
static inline u32
il_beacon_time_mask_high(struct il_priv *il, u16 tsf_bits)
{
	return ((1 << (32 - tsf_bits)) - 1) << tsf_bits;
}

/**
 * struct il_rb_status - reseve buffer status host memory mapped FH registers
 *
 * @closed_rb_num [0:11] - Indicates the idx of the RB which was closed
 * @closed_fr_num [0:11] - Indicates the idx of the RX Frame which was closed
 * @finished_rb_num [0:11] - Indicates the idx of the current RB
 *			     in which the last frame was written to
 * @finished_fr_num [0:11] - Indicates the idx of the RX Frame
 *			     which was transferred
 */
struct il_rb_status {
	__le16 closed_rb_num;
	__le16 closed_fr_num;
	__le16 finished_rb_num;
	__le16 finished_fr_nam;
	__le32 __unused;	/* 3945 only */
} __packed;

#define TFD_QUEUE_SIZE_MAX      256
#define TFD_QUEUE_SIZE_BC_DUP	64
#define TFD_QUEUE_BC_SIZE	(TFD_QUEUE_SIZE_MAX + TFD_QUEUE_SIZE_BC_DUP)
#define IL_TX_DMA_MASK		DMA_BIT_MASK(36)
#define IL_NUM_OF_TBS		20

static inline u8
il_get_dma_hi_addr(dma_addr_t addr)
{
	return (sizeof(addr) > sizeof(u32) ? (addr >> 16) >> 16 : 0) & 0xF;
}

/**
 * struct il_tfd_tb transmit buffer descriptor within transmit frame descriptor
 *
 * This structure contains dma address and length of transmission address
 *
 * @lo: low [31:0] portion of the dma address of TX buffer every even is
 *	unaligned on 16 bit boundary
 * @hi_n_len: 0-3 [35:32] portion of dma
 *	      4-15 length of the tx buffer
 */
struct il_tfd_tb {
	__le32 lo;
	__le16 hi_n_len;
} __packed;

/**
 * struct il_tfd
 *
 * Transmit Frame Descriptor (TFD)
 *
 * @ __reserved1[3] reserved
 * @ num_tbs 0-4 number of active tbs
 *	     5   reserved
 * 	     6-7 padding (not used)
 * @ tbs[20]	transmit frame buffer descriptors
 * @ __pad	padding
 *
 * Each Tx queue uses a circular buffer of 256 TFDs stored in host DRAM.
 * Both driver and device share these circular buffers, each of which must be
 * contiguous 256 TFDs x 128 bytes-per-TFD = 32 KBytes
 *
 * Driver must indicate the physical address of the base of each
 * circular buffer via the FH49_MEM_CBBC_QUEUE registers.
 *
 * Each TFD contains pointer/size information for up to 20 data buffers
 * in host DRAM.  These buffers collectively contain the (one) frame described
 * by the TFD.  Each buffer must be a single contiguous block of memory within
 * itself, but buffers may be scattered in host DRAM.  Each buffer has max size
 * of (4K - 4).  The concatenates all of a TFD's buffers into a single
 * Tx frame, up to 8 KBytes in size.
 *
 * A maximum of 255 (not 256!) TFDs may be on a queue waiting for Tx.
 */
struct il_tfd {
	u8 __reserved1[3];
	u8 num_tbs;
	struct il_tfd_tb tbs[IL_NUM_OF_TBS];
	__le32 __pad;
} __packed;
/* PCI registers */
#define PCI_CFG_RETRY_TIMEOUT	0x041

struct il_rate_info {
	u8 plcp;		/* uCode API:  RATE_6M_PLCP, etc. */
	u8 plcp_siso;		/* uCode API:  RATE_SISO_6M_PLCP, etc. */
	u8 plcp_mimo2;		/* uCode API:  RATE_MIMO2_6M_PLCP, etc. */
	u8 ieee;		/* MAC header:  RATE_6M_IEEE, etc. */
	u8 prev_ieee;		/* previous rate in IEEE speeds */
	u8 next_ieee;		/* next rate in IEEE speeds */
	u8 prev_rs;		/* previous rate used in rs algo */
	u8 next_rs;		/* next rate used in rs algo */
	u8 prev_rs_tgg;		/* previous rate used in TGG rs algo */
	u8 next_rs_tgg;		/* next rate used in TGG rs algo */
};

struct il3945_rate_info {
	u8 plcp;		/* uCode API:  RATE_6M_PLCP, etc. */
	u8 ieee;		/* MAC header:  RATE_6M_IEEE, etc. */
	u8 prev_ieee;		/* previous rate in IEEE speeds */
	u8 next_ieee;		/* next rate in IEEE speeds */
	u8 prev_rs;		/* previous rate used in rs algo */
	u8 next_rs;		/* next rate used in rs algo */
	u8 prev_rs_tgg;		/* previous rate used in TGG rs algo */
	u8 next_rs_tgg;		/* next rate used in TGG rs algo */
	u8 table_rs_idx;	/* idx in rate scale table cmd */
	u8 prev_table_rs;	/* prev in rate table cmd */
};

/*
 * These serve as idxes into
 * struct il_rate_info il_rates[RATE_COUNT];
 */
enum {
	RATE_1M_IDX = 0,
	RATE_2M_IDX,
	RATE_5M_IDX,
	RATE_11M_IDX,
	RATE_6M_IDX,
	RATE_9M_IDX,
	RATE_12M_IDX,
	RATE_18M_IDX,
	RATE_24M_IDX,
	RATE_36M_IDX,
	RATE_48M_IDX,
	RATE_54M_IDX,
	RATE_60M_IDX,
	RATE_COUNT,
	RATE_COUNT_LEGACY = RATE_COUNT - 1,	/* Excluding 60M */
	RATE_COUNT_3945 = RATE_COUNT - 1,
	RATE_INVM_IDX = RATE_COUNT,
	RATE_INVALID = RATE_COUNT,
};

enum {
	RATE_6M_IDX_TBL = 0,
	RATE_9M_IDX_TBL,
	RATE_12M_IDX_TBL,
	RATE_18M_IDX_TBL,
	RATE_24M_IDX_TBL,
	RATE_36M_IDX_TBL,
	RATE_48M_IDX_TBL,
	RATE_54M_IDX_TBL,
	RATE_1M_IDX_TBL,
	RATE_2M_IDX_TBL,
	RATE_5M_IDX_TBL,
	RATE_11M_IDX_TBL,
	RATE_INVM_IDX_TBL = RATE_INVM_IDX - 1,
};

enum {
	IL_FIRST_OFDM_RATE = RATE_6M_IDX,
	IL39_LAST_OFDM_RATE = RATE_54M_IDX,
	IL_LAST_OFDM_RATE = RATE_60M_IDX,
	IL_FIRST_CCK_RATE = RATE_1M_IDX,
	IL_LAST_CCK_RATE = RATE_11M_IDX,
};

/* #define vs. enum to keep from defaulting to 'large integer' */
#define	RATE_6M_MASK   (1 << RATE_6M_IDX)
#define	RATE_9M_MASK   (1 << RATE_9M_IDX)
#define	RATE_12M_MASK  (1 << RATE_12M_IDX)
#define	RATE_18M_MASK  (1 << RATE_18M_IDX)
#define	RATE_24M_MASK  (1 << RATE_24M_IDX)
#define	RATE_36M_MASK  (1 << RATE_36M_IDX)
#define	RATE_48M_MASK  (1 << RATE_48M_IDX)
#define	RATE_54M_MASK  (1 << RATE_54M_IDX)
#define RATE_60M_MASK  (1 << RATE_60M_IDX)
#define	RATE_1M_MASK   (1 << RATE_1M_IDX)
#define	RATE_2M_MASK   (1 << RATE_2M_IDX)
#define	RATE_5M_MASK   (1 << RATE_5M_IDX)
#define	RATE_11M_MASK  (1 << RATE_11M_IDX)

/* uCode API values for legacy bit rates, both OFDM and CCK */
enum {
	RATE_6M_PLCP = 13,
	RATE_9M_PLCP = 15,
	RATE_12M_PLCP = 5,
	RATE_18M_PLCP = 7,
	RATE_24M_PLCP = 9,
	RATE_36M_PLCP = 11,
	RATE_48M_PLCP = 1,
	RATE_54M_PLCP = 3,
	RATE_60M_PLCP = 3,	/*FIXME:RS:should be removed */
	RATE_1M_PLCP = 10,
	RATE_2M_PLCP = 20,
	RATE_5M_PLCP = 55,
	RATE_11M_PLCP = 110,
	/*FIXME:RS:add RATE_LEGACY_INVM_PLCP = 0, */
};

/* uCode API values for OFDM high-throughput (HT) bit rates */
enum {
	RATE_SISO_6M_PLCP = 0,
	RATE_SISO_12M_PLCP = 1,
	RATE_SISO_18M_PLCP = 2,
	RATE_SISO_24M_PLCP = 3,
	RATE_SISO_36M_PLCP = 4,
	RATE_SISO_48M_PLCP = 5,
	RATE_SISO_54M_PLCP = 6,
	RATE_SISO_60M_PLCP = 7,
	RATE_MIMO2_6M_PLCP = 0x8,
	RATE_MIMO2_12M_PLCP = 0x9,
	RATE_MIMO2_18M_PLCP = 0xa,
	RATE_MIMO2_24M_PLCP = 0xb,
	RATE_MIMO2_36M_PLCP = 0xc,
	RATE_MIMO2_48M_PLCP = 0xd,
	RATE_MIMO2_54M_PLCP = 0xe,
	RATE_MIMO2_60M_PLCP = 0xf,
	RATE_SISO_INVM_PLCP,
	RATE_MIMO2_INVM_PLCP = RATE_SISO_INVM_PLCP,
};

/* MAC header values for bit rates */
enum {
	RATE_6M_IEEE = 12,
	RATE_9M_IEEE = 18,
	RATE_12M_IEEE = 24,
	RATE_18M_IEEE = 36,
	RATE_24M_IEEE = 48,
	RATE_36M_IEEE = 72,
	RATE_48M_IEEE = 96,
	RATE_54M_IEEE = 108,
	RATE_60M_IEEE = 120,
	RATE_1M_IEEE = 2,
	RATE_2M_IEEE = 4,
	RATE_5M_IEEE = 11,
	RATE_11M_IEEE = 22,
};

#define IL_CCK_BASIC_RATES_MASK    \
	(RATE_1M_MASK          | \
	RATE_2M_MASK)

#define IL_CCK_RATES_MASK          \
	(IL_CCK_BASIC_RATES_MASK  | \
	RATE_5M_MASK          | \
	RATE_11M_MASK)

#define IL_OFDM_BASIC_RATES_MASK   \
	(RATE_6M_MASK         | \
	RATE_12M_MASK         | \
	RATE_24M_MASK)

#define IL_OFDM_RATES_MASK         \
	(IL_OFDM_BASIC_RATES_MASK | \
	RATE_9M_MASK          | \
	RATE_18M_MASK         | \
	RATE_36M_MASK         | \
	RATE_48M_MASK         | \
	RATE_54M_MASK)

#define IL_BASIC_RATES_MASK         \
	(IL_OFDM_BASIC_RATES_MASK | \
	 IL_CCK_BASIC_RATES_MASK)

#define RATES_MASK ((1 << RATE_COUNT) - 1)
#define RATES_MASK_3945 ((1 << RATE_COUNT_3945) - 1)

#define IL_INVALID_VALUE    -1

#define IL_MIN_RSSI_VAL                 -100
#define IL_MAX_RSSI_VAL                    0

/* These values specify how many Tx frame attempts before
 * searching for a new modulation mode */
#define IL_LEGACY_FAILURE_LIMIT	160
#define IL_LEGACY_SUCCESS_LIMIT	480
#define IL_LEGACY_TBL_COUNT		160

#define IL_NONE_LEGACY_FAILURE_LIMIT	400
#define IL_NONE_LEGACY_SUCCESS_LIMIT	4500
#define IL_NONE_LEGACY_TBL_COUNT	1500

/* Success ratio (ACKed / attempted tx frames) values (perfect is 128 * 100) */
#define IL_RS_GOOD_RATIO		12800	/* 100% */
#define RATE_SCALE_SWITCH		10880	/*  85% */
#define RATE_HIGH_TH		10880	/*  85% */
#define RATE_INCREASE_TH		6400	/*  50% */
#define RATE_DECREASE_TH		1920	/*  15% */

/* possible actions when in legacy mode */
#define IL_LEGACY_SWITCH_ANTENNA1      0
#define IL_LEGACY_SWITCH_ANTENNA2      1
#define IL_LEGACY_SWITCH_SISO          2
#define IL_LEGACY_SWITCH_MIMO2_AB      3
#define IL_LEGACY_SWITCH_MIMO2_AC      4
#define IL_LEGACY_SWITCH_MIMO2_BC      5

/* possible actions when in siso mode */
#define IL_SISO_SWITCH_ANTENNA1        0
#define IL_SISO_SWITCH_ANTENNA2        1
#define IL_SISO_SWITCH_MIMO2_AB        2
#define IL_SISO_SWITCH_MIMO2_AC        3
#define IL_SISO_SWITCH_MIMO2_BC        4
#define IL_SISO_SWITCH_GI              5

/* possible actions when in mimo mode */
#define IL_MIMO2_SWITCH_ANTENNA1       0
#define IL_MIMO2_SWITCH_ANTENNA2       1
#define IL_MIMO2_SWITCH_SISO_A         2
#define IL_MIMO2_SWITCH_SISO_B         3
#define IL_MIMO2_SWITCH_SISO_C         4
#define IL_MIMO2_SWITCH_GI             5

#define IL_MAX_SEARCH IL_MIMO2_SWITCH_GI

#define IL_ACTION_LIMIT		3	/* # possible actions */

#define LQ_SIZE		2	/* 2 mode tables:  "Active" and "Search" */

/* load per tid defines for A-MPDU activation */
#define IL_AGG_TPT_THREHOLD	0
#define IL_AGG_LOAD_THRESHOLD	10
#define IL_AGG_ALL_TID		0xff
#define TID_QUEUE_CELL_SPACING	50	/*mS */
#define TID_QUEUE_MAX_SIZE	20
#define TID_ROUND_VALUE		5	/* mS */
#define TID_MAX_LOAD_COUNT	8

#define TID_MAX_TIME_DIFF ((TID_QUEUE_MAX_SIZE - 1) * TID_QUEUE_CELL_SPACING)
#define TIME_WRAP_AROUND(x, y) (((y) > (x)) ? (y) - (x) : (0-(x)) + (y))

extern const struct il_rate_info il_rates[RATE_COUNT];

enum il_table_type {
	LQ_NONE,
	LQ_G,			/* legacy types */
	LQ_A,
	LQ_SISO,		/* high-throughput types */
	LQ_MIMO2,
	LQ_MAX,
};

#define is_legacy(tbl) ((tbl) == LQ_G || (tbl) == LQ_A)
#define is_siso(tbl) ((tbl) == LQ_SISO)
#define is_mimo2(tbl) ((tbl) == LQ_MIMO2)
#define is_mimo(tbl) (is_mimo2(tbl))
#define is_Ht(tbl) (is_siso(tbl) || is_mimo(tbl))
#define is_a_band(tbl) ((tbl) == LQ_A)
#define is_g_and(tbl) ((tbl) == LQ_G)

#define	ANT_NONE	0x0
#define	ANT_A		BIT(0)
#define	ANT_B		BIT(1)
#define	ANT_AB		(ANT_A | ANT_B)
#define ANT_C		BIT(2)
#define	ANT_AC		(ANT_A | ANT_C)
#define ANT_BC		(ANT_B | ANT_C)
#define ANT_ABC		(ANT_AB | ANT_C)

#define IL_MAX_MCS_DISPLAY_SIZE	12

struct il_rate_mcs_info {
	char mbps[IL_MAX_MCS_DISPLAY_SIZE];
	char mcs[IL_MAX_MCS_DISPLAY_SIZE];
};

/**
 * struct il_rate_scale_data -- tx success history for one rate
 */
struct il_rate_scale_data {
	u64 data;		/* bitmap of successful frames */
	s32 success_counter;	/* number of frames successful */
	s32 success_ratio;	/* per-cent * 128  */
	s32 counter;		/* number of frames attempted */
	s32 average_tpt;	/* success ratio * expected throughput */
	unsigned long stamp;
};

/**
 * struct il_scale_tbl_info -- tx params and success history for all rates
 *
 * There are two of these in struct il_lq_sta,
 * one for "active", and one for "search".
 */
struct il_scale_tbl_info {
	enum il_table_type lq_type;
	u8 ant_type;
	u8 is_SGI;		/* 1 = short guard interval */
	u8 is_ht40;		/* 1 = 40 MHz channel width */
	u8 is_dup;		/* 1 = duplicated data streams */
	u8 action;		/* change modulation; IL_[LEGACY/SISO/MIMO]_SWITCH_* */
	u8 max_search;		/* maximun number of tables we can search */
	s32 *expected_tpt;	/* throughput metrics; expected_tpt_G, etc. */
	u32 current_rate;	/* rate_n_flags, uCode API format */
	struct il_rate_scale_data win[RATE_COUNT];	/* rate histories */
};

struct il_traffic_load {
	unsigned long time_stamp;	/* age of the oldest stats */
	u32 packet_count[TID_QUEUE_MAX_SIZE];	/* packet count in this time
						 * slice */
	u32 total;		/* total num of packets during the
				 * last TID_MAX_TIME_DIFF */
	u8 queue_count;		/* number of queues that has
				 * been used since the last cleanup */
	u8 head;		/* start of the circular buffer */
};

/**
 * struct il_lq_sta -- driver's rate scaling ilate structure
 *
 * Pointer to this gets passed back and forth between driver and mac80211.
 */
struct il_lq_sta {
	u8 active_tbl;		/* idx of active table, range 0-1 */
	u8 enable_counter;	/* indicates HT mode */
	u8 stay_in_tbl;		/* 1: disallow, 0: allow search for new mode */
	u8 search_better_tbl;	/* 1: currently trying alternate mode */
	s32 last_tpt;

	/* The following determine when to search for a new mode */
	u32 table_count_limit;
	u32 max_failure_limit;	/* # failed frames before new search */
	u32 max_success_limit;	/* # successful frames before new search */
	u32 table_count;
	u32 total_failed;	/* total failed frames, any/all rates */
	u32 total_success;	/* total successful frames, any/all rates */
	u64 flush_timer;	/* time staying in mode before new search */

	u8 action_counter;	/* # mode-switch actions tried */
	u8 is_green;
	u8 is_dup;
	enum nl80211_band band;

	/* The following are bitmaps of rates; RATE_6M_MASK, etc. */
	u32 supp_rates;
	u16 active_legacy_rate;
	u16 active_siso_rate;
	u16 active_mimo2_rate;
	s8 max_rate_idx;	/* Max rate set by user */
	u8 missed_rate_counter;

	struct il_link_quality_cmd lq;
	struct il_scale_tbl_info lq_info[LQ_SIZE];	/* "active", "search" */
	struct il_traffic_load load[TID_MAX_LOAD_COUNT];
	u8 tx_agg_tid_en;
#ifdef CONFIG_MAC80211_DEBUGFS
	u32 dbg_fixed_rate;
#endif
	struct il_priv *drv;

	/* used to be in sta_info */
	int last_txrate_idx;
	/* last tx rate_n_flags */
	u32 last_rate_n_flags;
	/* packets destined for this STA are aggregated */
	u8 is_agg;
};

/*
 * il_station_priv: Driver's ilate station information
 *
 * When mac80211 creates a station it reserves some space (hw->sta_data_size)
 * in the structure for use by driver. This structure is places in that
 * space.
 *
 * The common struct MUST be first because it is shared between
 * 3945 and 4965!
 */
struct il_station_priv {
	struct il_station_priv_common common;
	struct il_lq_sta lq_sta;
	atomic_t pending_frames;
	bool client;
	bool asleep;
};

static inline u8
il4965_num_of_ant(u8 m)
{
	return !!(m & ANT_A) + !!(m & ANT_B) + !!(m & ANT_C);
}

static inline u8
il4965_first_antenna(u8 mask)
{
	if (mask & ANT_A)
		return ANT_A;
	if (mask & ANT_B)
		return ANT_B;
	return ANT_C;
}

/**
 * il3945_rate_scale_init - Initialize the rate scale table based on assoc info
 *
 * The specific throughput table used is based on the type of network
 * the associated with, including A, B, G, and G w/ TGG protection
 */
void il3945_rate_scale_init(struct ieee80211_hw *hw, s32 sta_id);

/* Initialize station's rate scaling information after adding station */
void il4965_rs_rate_init(struct il_priv *il, struct ieee80211_sta *sta,
			 u8 sta_id);
void il3945_rs_rate_init(struct il_priv *il, struct ieee80211_sta *sta,
			 u8 sta_id);

/**
 * il_rate_control_register - Register the rate control algorithm callbacks
 *
 * Since the rate control algorithm is hardware specific, there is no need
 * or reason to place it as a stand alone module.  The driver can call
 * il_rate_control_register in order to register the rate control callbacks
 * with the mac80211 subsystem.  This should be performed prior to calling
 * ieee80211_register_hw
 *
 */
int il4965_rate_control_register(void);
int il3945_rate_control_register(void);

/**
 * il_rate_control_unregister - Unregister the rate control callbacks
 *
 * This should be called after calling ieee80211_unregister_hw, but before
 * the driver is unloaded.
 */
void il4965_rate_control_unregister(void);
void il3945_rate_control_unregister(void);

int il_power_update_mode(struct il_priv *il, bool force);
void il_power_initialize(struct il_priv *il);

extern u32 il_debug_level;

#ifdef CONFIG_IWLEGACY_DEBUG
/*
 * il_get_debug_level: Return active debug level for device
 *
 * Using sysfs it is possible to set per device debug level. This debug
 * level will be used if set, otherwise the global debug level which can be
 * set via module parameter is used.
 */
static inline u32
il_get_debug_level(struct il_priv *il)
{
	if (il->debug_level)
		return il->debug_level;
	else
		return il_debug_level;
}
#else
static inline u32
il_get_debug_level(struct il_priv *il)
{
	return il_debug_level;
}
#endif

#define il_print_hex_error(il, p, len)					\
do {									\
	print_hex_dump(KERN_ERR, "iwl data: ",				\
		       DUMP_PREFIX_OFFSET, 16, 1, p, len, 1);		\
} while (0)

#ifdef CONFIG_IWLEGACY_DEBUG
#define IL_DBG(level, fmt, args...)					\
do {									\
	if (il_get_debug_level(il) & level)				\
		dev_err(&il->hw->wiphy->dev, "%s " fmt, __func__,	\
			 ##args);					\
} while (0)

#define il_print_hex_dump(il, level, p, len)				\
do {									\
	if (il_get_debug_level(il) & level)				\
		print_hex_dump(KERN_DEBUG, "iwl data: ",		\
			       DUMP_PREFIX_OFFSET, 16, 1, p, len, 1);	\
} while (0)

#else
#define IL_DBG(level, fmt, args...) no_printk(fmt, ##args)
static inline void
il_print_hex_dump(struct il_priv *il, int level, const void *p, u32 len)
{
}
#endif /* CONFIG_IWLEGACY_DEBUG */

#ifdef CONFIG_IWLEGACY_DEBUGFS
void il_dbgfs_register(struct il_priv *il, const char *name);
void il_dbgfs_unregister(struct il_priv *il);
#else
static inline void il_dbgfs_register(struct il_priv *il, const char *name)
{
}

static inline void
il_dbgfs_unregister(struct il_priv *il)
{
}
#endif /* CONFIG_IWLEGACY_DEBUGFS */

/*
 * To use the debug system:
 *
 * If you are defining a new debug classification, simply add it to the #define
 * list here in the form of
 *
 * #define IL_DL_xxxx VALUE
 *
 * where xxxx should be the name of the classification (for example, WEP).
 *
 * You then need to either add a IL_xxxx_DEBUG() macro definition for your
 * classification, or use IL_DBG(IL_DL_xxxx, ...) whenever you want
 * to send output to that classification.
 *
 * The active debug levels can be accessed via files
 *
 *	/sys/module/iwl4965/parameters/debug
 *	/sys/module/iwl3945/parameters/debug
 *	/sys/class/net/wlan0/device/debug_level
 *
 * when CONFIG_IWLEGACY_DEBUG=y.
 */

/* 0x0000000F - 0x00000001 */
#define IL_DL_INFO		(1 << 0)
#define IL_DL_MAC80211		(1 << 1)
#define IL_DL_HCMD		(1 << 2)
#define IL_DL_STATE		(1 << 3)
/* 0x000000F0 - 0x00000010 */
#define IL_DL_MACDUMP		(1 << 4)
#define IL_DL_HCMD_DUMP		(1 << 5)
#define IL_DL_EEPROM		(1 << 6)
#define IL_DL_RADIO		(1 << 7)
/* 0x00000F00 - 0x00000100 */
#define IL_DL_POWER		(1 << 8)
#define IL_DL_TEMP		(1 << 9)
#define IL_DL_NOTIF		(1 << 10)
#define IL_DL_SCAN		(1 << 11)
/* 0x0000F000 - 0x00001000 */
#define IL_DL_ASSOC		(1 << 12)
#define IL_DL_DROP		(1 << 13)
#define IL_DL_TXPOWER		(1 << 14)
#define IL_DL_AP		(1 << 15)
/* 0x000F0000 - 0x00010000 */
#define IL_DL_FW		(1 << 16)
#define IL_DL_RF_KILL		(1 << 17)
#define IL_DL_FW_ERRORS		(1 << 18)
#define IL_DL_LED		(1 << 19)
/* 0x00F00000 - 0x00100000 */
#define IL_DL_RATE		(1 << 20)
#define IL_DL_CALIB		(1 << 21)
#define IL_DL_WEP		(1 << 22)
#define IL_DL_TX		(1 << 23)
/* 0x0F000000 - 0x01000000 */
#define IL_DL_RX		(1 << 24)
#define IL_DL_ISR		(1 << 25)
#define IL_DL_HT		(1 << 26)
/* 0xF0000000 - 0x10000000 */
#define IL_DL_11H		(1 << 28)
#define IL_DL_STATS		(1 << 29)
#define IL_DL_TX_REPLY		(1 << 30)
#define IL_DL_QOS		(1 << 31)

#define D_INFO(f, a...)		IL_DBG(IL_DL_INFO, f, ## a)
#define D_MAC80211(f, a...)	IL_DBG(IL_DL_MAC80211, f, ## a)
#define D_MACDUMP(f, a...)	IL_DBG(IL_DL_MACDUMP, f, ## a)
#define D_TEMP(f, a...)		IL_DBG(IL_DL_TEMP, f, ## a)
#define D_SCAN(f, a...)		IL_DBG(IL_DL_SCAN, f, ## a)
#define D_RX(f, a...)		IL_DBG(IL_DL_RX, f, ## a)
#define D_TX(f, a...)		IL_DBG(IL_DL_TX, f, ## a)
#define D_ISR(f, a...)		IL_DBG(IL_DL_ISR, f, ## a)
#define D_LED(f, a...)		IL_DBG(IL_DL_LED, f, ## a)
#define D_WEP(f, a...)		IL_DBG(IL_DL_WEP, f, ## a)
#define D_HC(f, a...)		IL_DBG(IL_DL_HCMD, f, ## a)
#define D_HC_DUMP(f, a...)	IL_DBG(IL_DL_HCMD_DUMP, f, ## a)
#define D_EEPROM(f, a...)	IL_DBG(IL_DL_EEPROM, f, ## a)
#define D_CALIB(f, a...)	IL_DBG(IL_DL_CALIB, f, ## a)
#define D_FW(f, a...)		IL_DBG(IL_DL_FW, f, ## a)
#define D_RF_KILL(f, a...)	IL_DBG(IL_DL_RF_KILL, f, ## a)
#define D_DROP(f, a...)		IL_DBG(IL_DL_DROP, f, ## a)
#define D_AP(f, a...)		IL_DBG(IL_DL_AP, f, ## a)
#define D_TXPOWER(f, a...)	IL_DBG(IL_DL_TXPOWER, f, ## a)
#define D_RATE(f, a...)		IL_DBG(IL_DL_RATE, f, ## a)
#define D_NOTIF(f, a...)	IL_DBG(IL_DL_NOTIF, f, ## a)
#define D_ASSOC(f, a...)	IL_DBG(IL_DL_ASSOC, f, ## a)
#define D_HT(f, a...)		IL_DBG(IL_DL_HT, f, ## a)
#define D_STATS(f, a...)	IL_DBG(IL_DL_STATS, f, ## a)
#define D_TX_REPLY(f, a...)	IL_DBG(IL_DL_TX_REPLY, f, ## a)
#define D_QOS(f, a...)		IL_DBG(IL_DL_QOS, f, ## a)
#define D_RADIO(f, a...)	IL_DBG(IL_DL_RADIO, f, ## a)
#define D_POWER(f, a...)	IL_DBG(IL_DL_POWER, f, ## a)
#define D_11H(f, a...)		IL_DBG(IL_DL_11H, f, ## a)

#endif /* __il_core_h__ */
