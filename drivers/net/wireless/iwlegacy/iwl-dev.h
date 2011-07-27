/******************************************************************************
 *
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
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
 * Please use this file (iwl-dev.h) for driver implementation definitions.
 * Please use iwl-commands.h for uCode API definitions.
 * Please use iwl-4965-hw.h for hardware-related definitions.
 */

#ifndef __iwl_legacy_dev_h__
#define __iwl_legacy_dev_h__

#include <linux/pci.h> /* for struct pci_device_id */
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/wait.h>
#include <net/ieee80211_radiotap.h>

#include "iwl-eeprom.h"
#include "iwl-csr.h"
#include "iwl-prph.h"
#include "iwl-fh.h"
#include "iwl-debug.h"
#include "iwl-4965-hw.h"
#include "iwl-3945-hw.h"
#include "iwl-led.h"
#include "iwl-power.h"
#include "iwl-legacy-rs.h"

struct iwl_tx_queue;

/* CT-KILL constants */
#define CT_KILL_THRESHOLD_LEGACY   110 /* in Celsius */

/* Default noise level to report when noise measurement is not available.
 *   This may be because we're:
 *   1)  Not associated (4965, no beacon statistics being sent to driver)
 *   2)  Scanning (noise measurement does not apply to associated channel)
 *   3)  Receiving CCK (3945 delivers noise info only for OFDM frames)
 * Use default noise value of -127 ... this is below the range of measurable
 *   Rx dBm for either 3945 or 4965, so it can indicate "unmeasurable" to user.
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
#define DEFAULT_BEACON_INTERVAL   100U
#define	DEFAULT_SHORT_RETRY_LIMIT 7U
#define	DEFAULT_LONG_RETRY_LIMIT  4U

struct iwl_rx_mem_buffer {
	dma_addr_t page_dma;
	struct page *page;
	struct list_head list;
};

#define rxb_addr(r) page_address(r->page)

/* defined below */
struct iwl_device_cmd;

struct iwl_cmd_meta {
	/* only for SYNC commands, iff the reply skb is wanted */
	struct iwl_host_cmd *source;
	/*
	 * only for ASYNC commands
	 * (which is somewhat stupid -- look at iwl-sta.c for instance
	 * which duplicates a bunch of code because the callback isn't
	 * invoked for SYNC commands, if it were and its result passed
	 * through it would be simpler...)
	 */
	void (*callback)(struct iwl_priv *priv,
			 struct iwl_device_cmd *cmd,
			 struct iwl_rx_packet *pkt);

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
struct iwl_queue {
	int n_bd;              /* number of BDs in this queue */
	int write_ptr;       /* 1-st empty entry (index) host_w*/
	int read_ptr;         /* last used entry (index) host_r*/
	/* use for monitoring and recovering the stuck queue */
	dma_addr_t dma_addr;   /* physical addr for BD's */
	int n_window;	       /* safe queue window */
	u32 id;
	int low_mark;	       /* low watermark, resume queue if free
				* space more than this */
	int high_mark;         /* high watermark, stop queue if free
				* space less than this */
};

/* One for each TFD */
struct iwl_tx_info {
	struct sk_buff *skb;
	struct iwl_rxon_context *ctx;
};

/**
 * struct iwl_tx_queue - Tx Queue for DMA
 * @q: generic Rx/Tx queue descriptor
 * @bd: base of circular buffer of TFDs
 * @cmd: array of command/TX buffer pointers
 * @meta: array of meta data for each command/tx buffer
 * @dma_addr_cmd: physical address of cmd/tx buffer array
 * @txb: array of per-TFD driver data
 * @time_stamp: time (in jiffies) of last read_ptr change
 * @need_update: indicates need to update read/write index
 * @sched_retry: indicates queue is high-throughput aggregation (HT AGG) enabled
 *
 * A Tx queue consists of circular buffer of BDs (a.k.a. TFDs, transmit frame
 * descriptors) and required locking structures.
 */
#define TFD_TX_CMD_SLOTS 256
#define TFD_CMD_SLOTS 32

struct iwl_tx_queue {
	struct iwl_queue q;
	void *tfds;
	struct iwl_device_cmd **cmd;
	struct iwl_cmd_meta *meta;
	struct iwl_tx_info *txb;
	unsigned long time_stamp;
	u8 need_update;
	u8 sched_retry;
	u8 active;
	u8 swq_id;
};

#define IWL_NUM_SCAN_RATES         (2)

struct iwl4965_channel_tgd_info {
	u8 type;
	s8 max_power;
};

struct iwl4965_channel_tgh_info {
	s64 last_radar_time;
};

#define IWL4965_MAX_RATE (33)

struct iwl3945_clip_group {
	/* maximum power level to prevent clipping for each rate, derived by
	 *   us from this band's saturation power in EEPROM */
	const s8 clip_powers[IWL_MAX_RATES];
};

/* current Tx power values to use, one for each rate for each channel.
 * requested power is limited by:
 * -- regulatory EEPROM limits for this channel
 * -- hardware capabilities (clip-powers)
 * -- spectrum management
 * -- user preference (e.g. iwconfig)
 * when requested power is set, base power index must also be set. */
struct iwl3945_channel_power_info {
	struct iwl3945_tx_power tpc;	/* actual radio and DSP gain settings */
	s8 power_table_index;	/* actual (compenst'd) index into gain table */
	s8 base_power_index;	/* gain index for power at factory temp. */
	s8 requested_power;	/* power (dBm) requested for this chnl/rate */
};

/* current scan Tx power values to use, one for each scan rate for each
 * channel. */
struct iwl3945_scan_power_info {
	struct iwl3945_tx_power tpc;	/* actual radio and DSP gain settings */
	s8 power_table_index;	/* actual (compenst'd) index into gain table */
	s8 requested_power;	/* scan pwr (dBm) requested for chnl/rate */
};

/*
 * One for each channel, holds all channel setup data
 * Some of the fields (e.g. eeprom and flags/max_power_avg) are redundant
 *     with one another!
 */
struct iwl_channel_info {
	struct iwl4965_channel_tgd_info tgd;
	struct iwl4965_channel_tgh_info tgh;
	struct iwl_eeprom_channel eeprom;	/* EEPROM regulatory limit */
	struct iwl_eeprom_channel ht40_eeprom;	/* EEPROM regulatory limit for
						 * HT40 channel */

	u8 channel;	  /* channel number */
	u8 flags;	  /* flags copied from EEPROM */
	s8 max_power_avg; /* (dBm) regul. eeprom, normal Tx, any rate */
	s8 curr_txpow;	  /* (dBm) regulatory/spectrum/user (not h/w) limit */
	s8 min_power;	  /* always 0 */
	s8 scan_power;	  /* (dBm) regul. eeprom, direct scans, any rate */

	u8 group_index;	  /* 0-4, maps channel to group1/2/3/4/5 */
	u8 band_index;	  /* 0-4, maps channel to band1/2/3/4/5 */
	enum ieee80211_band band;

	/* HT40 channel info */
	s8 ht40_max_power_avg;	/* (dBm) regul. eeprom, normal Tx, any rate */
	u8 ht40_flags;		/* flags copied from EEPROM */
	u8 ht40_extension_channel; /* HT_IE_EXT_CHANNEL_* */

	/* Radio/DSP gain settings for each "normal" data Tx rate.
	 * These include, in addition to RF and DSP gain, a few fields for
	 *   remembering/modifying gain settings (indexes). */
	struct iwl3945_channel_power_info power_info[IWL4965_MAX_RATE];

	/* Radio/DSP gain settings for each scan rate, for directed scans. */
	struct iwl3945_scan_power_info scan_pwr_info[IWL_NUM_SCAN_RATES];
};

#define IWL_TX_FIFO_BK		0	/* shared */
#define IWL_TX_FIFO_BE		1
#define IWL_TX_FIFO_VI		2	/* shared */
#define IWL_TX_FIFO_VO		3
#define IWL_TX_FIFO_UNUSED	-1

/* Minimum number of queues. MAX_NUM is defined in hw specific files.
 * Set the minimum to accommodate the 4 standard TX queues, 1 command
 * queue, 2 (unused) HCCA queues, and 4 HT queues (one for each AC) */
#define IWL_MIN_NUM_QUEUES	10

#define IWL_DEFAULT_CMD_QUEUE_NUM	4

#define IEEE80211_DATA_LEN              2304
#define IEEE80211_4ADDR_LEN             30
#define IEEE80211_HLEN                  (IEEE80211_4ADDR_LEN)
#define IEEE80211_FRAME_LEN             (IEEE80211_DATA_LEN + IEEE80211_HLEN)

struct iwl_frame {
	union {
		struct ieee80211_hdr frame;
		struct iwl_tx_beacon_cmd beacon;
		u8 raw[IEEE80211_FRAME_LEN];
		u8 cmd[360];
	} u;
	struct list_head list;
};

#define SEQ_TO_SN(seq) (((seq) & IEEE80211_SCTL_SEQ) >> 4)
#define SN_TO_SEQ(ssn) (((ssn) << 4) & IEEE80211_SCTL_SEQ)
#define MAX_SN ((IEEE80211_SCTL_SEQ) >> 4)

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
 * struct iwl_device_cmd
 *
 * For allocation of the command and tx queues, this establishes the overall
 * size of the largest command we send to uCode, except for a scan command
 * (which is relatively huge; space is allocated separately).
 */
struct iwl_device_cmd {
	struct iwl_cmd_header hdr;	/* uCode API */
	union {
		u32 flags;
		u8 val8;
		u16 val16;
		u32 val32;
		struct iwl_tx_cmd tx;
		u8 payload[DEF_CMD_PAYLOAD_SIZE];
	} __packed cmd;
} __packed;

#define TFD_MAX_PAYLOAD_SIZE (sizeof(struct iwl_device_cmd))


struct iwl_host_cmd {
	const void *data;
	unsigned long reply_page;
	void (*callback)(struct iwl_priv *priv,
			 struct iwl_device_cmd *cmd,
			 struct iwl_rx_packet *pkt);
	u32 flags;
	u16 len;
	u8 id;
};

#define SUP_RATE_11A_MAX_NUM_CHANNELS  8
#define SUP_RATE_11B_MAX_NUM_CHANNELS  4
#define SUP_RATE_11G_MAX_NUM_CHANNELS  12

/**
 * struct iwl_rx_queue - Rx queue
 * @bd: driver's pointer to buffer of receive buffer descriptors (rbd)
 * @bd_dma: bus address of buffer of receive buffer descriptors (rbd)
 * @read: Shared index to newest available Rx buffer
 * @write: Shared index to oldest written Rx packet
 * @free_count: Number of pre-allocated buffers in rx_free
 * @rx_free: list of free SKBs for use
 * @rx_used: List of Rx buffers with no SKB
 * @need_update: flag to indicate we need to update read/write index
 * @rb_stts: driver's pointer to receive buffer status
 * @rb_stts_dma: bus address of receive buffer status
 *
 * NOTE:  rx_free and rx_used are used as a FIFO for iwl_rx_mem_buffers
 */
struct iwl_rx_queue {
	__le32 *bd;
	dma_addr_t bd_dma;
	struct iwl_rx_mem_buffer pool[RX_QUEUE_SIZE + RX_FREE_BUFFERS];
	struct iwl_rx_mem_buffer *queue[RX_QUEUE_SIZE];
	u32 read;
	u32 write;
	u32 free_count;
	u32 write_actual;
	struct list_head rx_free;
	struct list_head rx_used;
	int need_update;
	struct iwl_rb_status *rb_stts;
	dma_addr_t rb_stts_dma;
	spinlock_t lock;
};

#define IWL_SUPPORTED_RATES_IE_LEN         8

#define MAX_TID_COUNT        9

#define IWL_INVALID_RATE     0xFF
#define IWL_INVALID_VALUE    -1

/**
 * struct iwl_ht_agg -- aggregation status while waiting for block-ack
 * @txq_id: Tx queue used for Tx attempt
 * @frame_count: # frames attempted by Tx command
 * @wait_for_ba: Expect block-ack before next Tx reply
 * @start_idx: Index of 1st Transmit Frame Descriptor (TFD) in Tx window
 * @bitmap0: Low order bitmap, one bit for each frame pending ACK in Tx window
 * @bitmap1: High order, one bit for each frame pending ACK in Tx window
 * @rate_n_flags: Rate at which Tx was attempted
 *
 * If REPLY_TX indicates that aggregation was attempted, driver must wait
 * for block ack (REPLY_COMPRESSED_BA).  This struct stores tx reply info
 * until block ack arrives.
 */
struct iwl_ht_agg {
	u16 txq_id;
	u16 frame_count;
	u16 wait_for_ba;
	u16 start_idx;
	u64 bitmap;
	u32 rate_n_flags;
#define IWL_AGG_OFF 0
#define IWL_AGG_ON 1
#define IWL_EMPTYING_HW_QUEUE_ADDBA 2
#define IWL_EMPTYING_HW_QUEUE_DELBA 3
	u8 state;
};


struct iwl_tid_data {
	u16 seq_number; /* 4965 only */
	u16 tfds_in_queue;
	struct iwl_ht_agg agg;
};

struct iwl_hw_key {
	u32 cipher;
	int keylen;
	u8 keyidx;
	u8 key[32];
};

union iwl_ht_rate_supp {
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

struct iwl_ht_config {
	bool single_chain_sufficient;
	enum ieee80211_smps_mode smps; /* current smps mode */
};

/* QoS structures */
struct iwl_qos_info {
	int qos_active;
	struct iwl_qosparam_cmd def_qos_parm;
};

/*
 * Structure should be accessed with sta_lock held. When station addition
 * is in progress (IWL_STA_UCODE_INPROGRESS) it is possible to access only
 * the commands (iwl_legacy_addsta_cmd and iwl_link_quality_cmd) without
 * sta_lock held.
 */
struct iwl_station_entry {
	struct iwl_legacy_addsta_cmd sta;
	struct iwl_tid_data tid[MAX_TID_COUNT];
	u8 used, ctxid;
	struct iwl_hw_key keyinfo;
	struct iwl_link_quality_cmd *lq;
};

struct iwl_station_priv_common {
	struct iwl_rxon_context *ctx;
	u8 sta_id;
};

/*
 * iwl_station_priv: Driver's private station information
 *
 * When mac80211 creates a station it reserves some space (hw->sta_data_size)
 * in the structure for use by driver. This structure is places in that
 * space.
 *
 * The common struct MUST be first because it is shared between
 * 3945 and 4965!
 */
struct iwl_station_priv {
	struct iwl_station_priv_common common;
	struct iwl_lq_sta lq_sta;
	atomic_t pending_frames;
	bool client;
	bool asleep;
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

/* one for each uCode image (inst/data, boot/init/runtime) */
struct fw_desc {
	void *v_addr;		/* access by driver */
	dma_addr_t p_addr;	/* access by card's busmaster DMA */
	u32 len;		/* bytes */
};

/* uCode file layout */
struct iwl_ucode_header {
	__le32 ver;	/* major/minor/API/serial */
	struct {
		__le32 inst_size;	/* bytes of runtime code */
		__le32 data_size;	/* bytes of runtime data */
		__le32 init_size;	/* bytes of init code */
		__le32 init_data_size;	/* bytes of init data */
		__le32 boot_size;	/* bytes of bootstrap code */
		u8 data[0];		/* in same order as sizes */
	} v1;
};

struct iwl4965_ibss_seq {
	u8 mac[ETH_ALEN];
	u16 seq_num;
	u16 frag_num;
	unsigned long packet_time;
	struct list_head list;
};

struct iwl_sensitivity_ranges {
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


#define KELVIN_TO_CELSIUS(x) ((x)-273)
#define CELSIUS_TO_KELVIN(x) ((x)+273)


/**
 * struct iwl_hw_params
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
 * BIT(IEEE80211_BAND_5GHZ) BIT(IEEE80211_BAND_5GHZ)
 * @sw_crypto: 0 for hw, 1 for sw
 * @max_xxx_size: for ucode uses
 * @ct_kill_threshold: temperature threshold
 * @beacon_time_tsf_bits: number of valid tsf bits for beacon time
 * @struct iwl_sensitivity_ranges: range of sensitivity values
 */
struct iwl_hw_params {
	u8 max_txq_num;
	u8 dma_chnl_num;
	u16 scd_bc_tbls_size;
	u32 tfd_size;
	u8  tx_chains_num;
	u8  rx_chains_num;
	u8  valid_tx_ant;
	u8  valid_rx_ant;
	u16 max_rxq_size;
	u16 max_rxq_log;
	u32 rx_page_order;
	u32 rx_wrt_ptr_reg;
	u8  max_stations;
	u8  ht40_channel;
	u8  max_beacon_itrvl;	/* in 1024 ms */
	u32 max_inst_size;
	u32 max_data_size;
	u32 max_bsm_size;
	u32 ct_kill_threshold; /* value in hw-dependent units */
	u16 beacon_time_tsf_bits;
	const struct iwl_sensitivity_ranges *sens;
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
 * iwl_         <-- Is part of iwlwifi
 * iwlXXXX_     <-- Hardware specific (implemented in iwl-XXXX.c for XXXX)
 * iwl4965_bg_      <-- Called from work queue context
 * iwl4965_mac_     <-- mac80211 callback
 *
 ****************************************************************************/
extern void iwl4965_update_chain_flags(struct iwl_priv *priv);
extern const u8 iwlegacy_bcast_addr[ETH_ALEN];
extern int iwl_legacy_queue_space(const struct iwl_queue *q);
static inline int iwl_legacy_queue_used(const struct iwl_queue *q, int i)
{
	return q->write_ptr >= q->read_ptr ?
		(i >= q->read_ptr && i < q->write_ptr) :
		!(i < q->read_ptr && i >= q->write_ptr);
}


static inline u8 iwl_legacy_get_cmd_index(struct iwl_queue *q, u32 index,
								int is_huge)
{
	/*
	 * This is for init calibration result and scan command which
	 * required buffer > TFD_MAX_PAYLOAD_SIZE,
	 * the big buffer at end of command array
	 */
	if (is_huge)
		return q->n_window;	/* must be power of 2 */

	/* Otherwise, use normal size buffers */
	return index & (q->n_window - 1);
}


struct iwl_dma_ptr {
	dma_addr_t dma;
	void *addr;
	size_t size;
};

#define IWL_OPERATION_MODE_AUTO     0
#define IWL_OPERATION_MODE_HT_ONLY  1
#define IWL_OPERATION_MODE_MIXED    2
#define IWL_OPERATION_MODE_20MHZ    3

#define IWL_TX_CRC_SIZE 4
#define IWL_TX_DELIMITER_SIZE 4

#define TX_POWER_IWL_ILLEGAL_VOLTAGE -10000

/* Sensitivity and chain noise calibration */
#define INITIALIZATION_VALUE		0xFFFF
#define IWL4965_CAL_NUM_BEACONS		20
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

enum iwl4965_false_alarm_state {
	IWL_FA_TOO_MANY = 0,
	IWL_FA_TOO_FEW = 1,
	IWL_FA_GOOD_RANGE = 2,
};

enum iwl4965_chain_noise_state {
	IWL_CHAIN_NOISE_ALIVE = 0,  /* must be 0 */
	IWL_CHAIN_NOISE_ACCUMULATE,
	IWL_CHAIN_NOISE_CALIBRATED,
	IWL_CHAIN_NOISE_DONE,
};

enum iwl4965_calib_enabled_state {
	IWL_CALIB_DISABLED = 0,  /* must be 0 */
	IWL_CALIB_ENABLED = 1,
};

/*
 * enum iwl_calib
 * defines the order in which results of initial calibrations
 * should be sent to the runtime uCode
 */
enum iwl_calib {
	IWL_CALIB_MAX,
};

/* Opaque calibration results */
struct iwl_calib_result {
	void *buf;
	size_t buf_len;
};

enum ucode_type {
	UCODE_NONE = 0,
	UCODE_INIT,
	UCODE_RT
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

#define	EEPROM_SEM_TIMEOUT 10		/* milliseconds */
#define EEPROM_SEM_RETRY_LIMIT 1000	/* number of attempts (not time) */

#define IWL_TRAFFIC_ENTRIES	(256)
#define IWL_TRAFFIC_ENTRY_SIZE  (64)

enum {
	MEASUREMENT_READY = (1 << 0),
	MEASUREMENT_ACTIVE = (1 << 1),
};

/* interrupt statistics */
struct isr_statistics {
	u32 hw;
	u32 sw;
	u32 err_code;
	u32 sch;
	u32 alive;
	u32 rfkill;
	u32 ctkill;
	u32 wakeup;
	u32 rx;
	u32 rx_handlers[REPLY_MAX];
	u32 tx;
	u32 unhandled;
};

/* management statistics */
enum iwl_mgmt_stats {
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
/* control statistics */
enum iwl_ctrl_stats {
	CONTROL_BACK_REQ =  0,
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
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUGFS
	u32 mgmt[MANAGEMENT_MAX];
	u32 ctrl[CONTROL_MAX];
	u32 data_cnt;
	u64 data_bytes;
#endif
};

/*
 * schedule the timer to wake up every UCODE_TRACE_PERIOD milliseconds
 * to perform continuous uCode event logging operation if enabled
 */
#define UCODE_TRACE_PERIOD (100)

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

/*
 * host interrupt timeout value
 * used with setting interrupt coalescing timer
 * the CSR_INT_COALESCING is an 8 bit register in 32-usec unit
 *
 * default interrupt coalescing timer is 64 x 32 = 2048 usecs
 * default interrupt coalescing calibration timer is 16 x 32 = 512 usecs
 */
#define IWL_HOST_INT_TIMEOUT_MAX	(0xFF)
#define IWL_HOST_INT_TIMEOUT_DEF	(0x40)
#define IWL_HOST_INT_TIMEOUT_MIN	(0x0)
#define IWL_HOST_INT_CALIB_TIMEOUT_MAX	(0xFF)
#define IWL_HOST_INT_CALIB_TIMEOUT_DEF	(0x10)
#define IWL_HOST_INT_CALIB_TIMEOUT_MIN	(0x0)

/*
 * This is the threshold value of plcp error rate per 100mSecs.  It is
 * used to set and check for the validity of plcp_delta.
 */
#define IWL_MAX_PLCP_ERR_THRESHOLD_MIN	(1)
#define IWL_MAX_PLCP_ERR_THRESHOLD_DEF	(50)
#define IWL_MAX_PLCP_ERR_LONG_THRESHOLD_DEF	(100)
#define IWL_MAX_PLCP_ERR_EXT_LONG_THRESHOLD_DEF	(200)
#define IWL_MAX_PLCP_ERR_THRESHOLD_MAX	(255)
#define IWL_MAX_PLCP_ERR_THRESHOLD_DISABLE	(0)

#define IWL_DELAY_NEXT_FORCE_RF_RESET  (HZ*3)
#define IWL_DELAY_NEXT_FORCE_FW_RELOAD (HZ*5)

/* TX queue watchdog timeouts in mSecs */
#define IWL_DEF_WD_TIMEOUT	(2000)
#define IWL_LONG_WD_TIMEOUT	(10000)
#define IWL_MAX_WD_TIMEOUT	(120000)

enum iwl_reset {
	IWL_RF_RESET = 0,
	IWL_FW_RESET,
	IWL_MAX_FORCE_RESET,
};

struct iwl_force_reset {
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
#define IWL3945_EXT_BEACON_TIME_POS	24
/*
 * for _4965 devices
 * bits 31:22 - extended
 * bits 21:0  - interval
 */
#define IWL4965_EXT_BEACON_TIME_POS	22

enum iwl_rxon_context_id {
	IWL_RXON_CTX_BSS,

	NUM_IWL_RXON_CTX
};

struct iwl_rxon_context {
	struct ieee80211_vif *vif;

	const u8 *ac_to_fifo;
	const u8 *ac_to_queue;
	u8 mcast_queue;

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
	const struct iwl_legacy_rxon_cmd active;
	struct iwl_legacy_rxon_cmd staging;

	struct iwl_rxon_time_cmd timing;

	struct iwl_qos_info qos_data;

	u8 bcast_sta_id, ap_sta_id;

	u8 rxon_cmd, rxon_assoc_cmd, rxon_timing_cmd;
	u8 qos_cmd;
	u8 wep_key_cmd;

	struct iwl_wep_key wep_keys[WEP_KEYS_MAX];
	u8 key_mapping_keys;

	__le32 station_flags;

	struct {
		bool non_gf_sta_present;
		u8 protection;
		bool enabled, is_40mhz;
		u8 extension_chan_offset;
	} ht;
};

struct iwl_priv {

	/* ieee device used by generic ieee processing code */
	struct ieee80211_hw *hw;
	struct ieee80211_channel *ieee_channels;
	struct ieee80211_rate *ieee_rates;
	struct iwl_cfg *cfg;

	/* temporary frame storage list */
	struct list_head free_frames;
	int frames_count;

	enum ieee80211_band band;
	int alloc_rxb_page;

	void (*rx_handlers[REPLY_MAX])(struct iwl_priv *priv,
				       struct iwl_rx_mem_buffer *rxb);

	struct ieee80211_supported_band bands[IEEE80211_NUM_BANDS];

	/* spectrum measurement report caching */
	struct iwl_spectrum_notification measure_report;
	u8 measurement_status;

	/* ucode beacon time */
	u32 ucode_beacon_time;
	int missed_beacon_threshold;

	/* track IBSS manager (last beacon) status */
	u32 ibss_manager;

	/* storing the jiffies when the plcp error rate is received */
	unsigned long plcp_jiffies;

	/* force reset */
	struct iwl_force_reset force_reset[IWL_MAX_FORCE_RESET];

	/* we allocate array of iwl_channel_info for NIC's valid channels.
	 *    Access via channel # using indirect index array */
	struct iwl_channel_info *channel_info;	/* channel info array */
	u8 channel_count;	/* # of channels */

	/* thermal calibration */
	s32 temperature;	/* degrees Kelvin */
	s32 last_temperature;

	/* init calibration results */
	struct iwl_calib_result calib_results[IWL_CALIB_MAX];

	/* Scan related variables */
	unsigned long scan_start;
	unsigned long scan_start_tsf;
	void *scan_cmd;
	enum ieee80211_band scan_band;
	struct cfg80211_scan_request *scan_request;
	struct ieee80211_vif *scan_vif;
	bool is_internal_short_scan;
	u8 scan_tx_ant[IEEE80211_NUM_BANDS];
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
	u32  hw_rev;
	u32  hw_wa_rev;
	u8   rev_id;

	/* microcode/device supports multiple contexts */
	u8 valid_contexts;

	/* command queue number */
	u8 cmd_queue;

	/* max number of station keys */
	u8 sta_key_max_num;

	/* EEPROM MAC addresses */
	struct mac_address addresses[1];

	/* uCode images, save to reload in case of failure */
	int fw_index;			/* firmware we're trying to load */
	u32 ucode_ver;			/* version of ucode, copy of
					   iwl_ucode.ver */
	struct fw_desc ucode_code;	/* runtime inst */
	struct fw_desc ucode_data;	/* runtime data original */
	struct fw_desc ucode_data_backup;	/* runtime data save/restore */
	struct fw_desc ucode_init;	/* initialization inst */
	struct fw_desc ucode_init_data;	/* initialization data */
	struct fw_desc ucode_boot;	/* bootstrap inst */
	enum ucode_type ucode_type;
	u8 ucode_write_complete;	/* the image write is complete */
	char firmware_name[25];

	struct iwl_rxon_context contexts[NUM_IWL_RXON_CTX];

	__le16 switch_channel;

	/* 1st responses from initialize and runtime uCode images.
	 * _4965's initialize alive response contains some calibration data. */
	struct iwl_init_alive_resp card_alive_init;
	struct iwl_alive_resp card_alive;

	u16 active_rate;

	u8 start_calib;
	struct iwl_sensitivity_data sensitivity_data;
	struct iwl_chain_noise_data chain_noise_data;
	__le16 sensitivity_tbl[HD_TABLE_SIZE];

	struct iwl_ht_config current_ht_config;

	/* Rate scaling data */
	u8 retry_rate;

	wait_queue_head_t wait_command_queue;

	int activity_timer_active;

	/* Rx and Tx DMA processing queues */
	struct iwl_rx_queue rxq;
	struct iwl_tx_queue *txq;
	unsigned long txq_ctx_active_msk;
	struct iwl_dma_ptr  kw;	/* keep warm address */
	struct iwl_dma_ptr  scd_bc_tbls;

	u32 scd_base_addr;	/* scheduler sram base address */

	unsigned long status;

	/* counts mgmt, ctl, and data packets */
	struct traffic_stats tx_stats;
	struct traffic_stats rx_stats;

	/* counts interrupts */
	struct isr_statistics isr_stats;

	struct iwl_power_mgr power_data;

	/* context information */
	u8 bssid[ETH_ALEN]; /* used only on 3945 but filled by core */

	/* station table variables */

	/* Note: if lock and sta_lock are needed, lock must be acquired first */
	spinlock_t sta_lock;
	int num_stations;
	struct iwl_station_entry stations[IWL_STATION_COUNT];
	unsigned long ucode_key_table;

	/* queue refcounts */
#define IWL_MAX_HW_QUEUES	32
	unsigned long queue_stopped[BITS_TO_LONGS(IWL_MAX_HW_QUEUES)];
	/* for each AC */
	atomic_t queue_stop_count[4];

	/* Indication if ieee80211_ops->open has been called */
	u8 is_open;

	u8 mac80211_registered;

	/* eeprom -- this is in the card's little endian byte order */
	u8 *eeprom;
	struct iwl_eeprom_calib_info *calib_info;

	enum nl80211_iftype iw_mode;

	/* Last Rx'd beacon timestamp */
	u64 timestamp;

	union {
#if defined(CONFIG_IWL3945) || defined(CONFIG_IWL3945_MODULE)
		struct {
			void *shared_virt;
			dma_addr_t shared_phys;

			struct delayed_work thermal_periodic;
			struct delayed_work rfkill_poll;

			struct iwl3945_notif_statistics statistics;
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUGFS
			struct iwl3945_notif_statistics accum_statistics;
			struct iwl3945_notif_statistics delta_statistics;
			struct iwl3945_notif_statistics max_delta;
#endif

			u32 sta_supp_rates;
			int last_rx_rssi;	/* From Rx packet statistics */

			/* Rx'd packet timing information */
			u32 last_beacon_time;
			u64 last_tsf;

			/*
			 * each calibration channel group in the
			 * EEPROM has a derived clip setting for
			 * each rate.
			 */
			const struct iwl3945_clip_group clip_groups[5];

		} _3945;
#endif
#if defined(CONFIG_IWL4965) || defined(CONFIG_IWL4965_MODULE)
		struct {
			/*
			 * reporting the number of tids has AGG on. 0 means
			 * no AGGREGATION
			 */
			u8 agg_tids_count;

			struct iwl_rx_phy_res last_phy_res;
			bool last_phy_res_valid;

			struct completion firmware_loading_complete;

			/*
			 * chain noise reset and gain commands are the
			 * two extra calibration commands follows the standard
			 * phy calibration commands
			 */
			u8 phy_calib_chain_noise_reset_cmd;
			u8 phy_calib_chain_noise_gain_cmd;

			struct iwl_notif_statistics statistics;
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUGFS
			struct iwl_notif_statistics accum_statistics;
			struct iwl_notif_statistics delta_statistics;
			struct iwl_notif_statistics max_delta;
#endif

		} _4965;
#endif
	};

	struct iwl_hw_params hw_params;

	u32 inta_mask;

	struct workqueue_struct *workqueue;

	struct work_struct restart;
	struct work_struct scan_completed;
	struct work_struct rx_replenish;
	struct work_struct abort_scan;

	struct iwl_rxon_context *beacon_ctx;
	struct sk_buff *beacon_skb;

	struct work_struct start_internal_scan;
	struct work_struct tx_flush;

	struct tasklet_struct irq_tasklet;

	struct delayed_work init_alive_start;
	struct delayed_work alive_start;
	struct delayed_work scan_check;

	/* TX Power */
	s8 tx_power_user_lmt;
	s8 tx_power_device_lmt;
	s8 tx_power_next;


#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
	/* debugging info */
	u32 debug_level; /* per device debugging will override global
			    iwlegacy_debug_level if set */
#endif /* CONFIG_IWLWIFI_LEGACY_DEBUG */
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUGFS
	/* debugfs */
	u16 tx_traffic_idx;
	u16 rx_traffic_idx;
	u8 *tx_traffic;
	u8 *rx_traffic;
	struct dentry *debugfs_dir;
	u32 dbgfs_sram_offset, dbgfs_sram_len;
	bool disable_ht40;
#endif /* CONFIG_IWLWIFI_LEGACY_DEBUGFS */

	struct work_struct txpower_work;
	u32 disable_sens_cal;
	u32 disable_chain_noise_cal;
	u32 disable_tx_power_cal;
	struct work_struct run_time_calib_work;
	struct timer_list statistics_periodic;
	struct timer_list ucode_trace;
	struct timer_list watchdog;
	bool hw_ready;

	struct iwl_event_log event_log;

	struct led_classdev led;
	unsigned long blink_on, blink_off;
	bool led_registered;
}; /*iwl_priv */

static inline void iwl_txq_ctx_activate(struct iwl_priv *priv, int txq_id)
{
	set_bit(txq_id, &priv->txq_ctx_active_msk);
}

static inline void iwl_txq_ctx_deactivate(struct iwl_priv *priv, int txq_id)
{
	clear_bit(txq_id, &priv->txq_ctx_active_msk);
}

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
/*
 * iwl_legacy_get_debug_level: Return active debug level for device
 *
 * Using sysfs it is possible to set per device debug level. This debug
 * level will be used if set, otherwise the global debug level which can be
 * set via module parameter is used.
 */
static inline u32 iwl_legacy_get_debug_level(struct iwl_priv *priv)
{
	if (priv->debug_level)
		return priv->debug_level;
	else
		return iwlegacy_debug_level;
}
#else
static inline u32 iwl_legacy_get_debug_level(struct iwl_priv *priv)
{
	return iwlegacy_debug_level;
}
#endif


static inline struct ieee80211_hdr *
iwl_legacy_tx_queue_get_hdr(struct iwl_priv *priv,
						 int txq_id, int idx)
{
	if (priv->txq[txq_id].txb[idx].skb)
		return (struct ieee80211_hdr *)priv->txq[txq_id].
				txb[idx].skb->data;
	return NULL;
}

static inline struct iwl_rxon_context *
iwl_legacy_rxon_ctx_from_vif(struct ieee80211_vif *vif)
{
	struct iwl_vif_priv *vif_priv = (void *)vif->drv_priv;

	return vif_priv->ctx;
}

#define for_each_context(priv, ctx)				\
	for (ctx = &priv->contexts[IWL_RXON_CTX_BSS];		\
	     ctx < &priv->contexts[NUM_IWL_RXON_CTX]; ctx++)	\
		if (priv->valid_contexts & BIT(ctx->ctxid))

static inline int iwl_legacy_is_associated(struct iwl_priv *priv,
				    enum iwl_rxon_context_id ctxid)
{
	return (priv->contexts[ctxid].active.filter_flags &
			RXON_FILTER_ASSOC_MSK) ? 1 : 0;
}

static inline int iwl_legacy_is_any_associated(struct iwl_priv *priv)
{
	return iwl_legacy_is_associated(priv, IWL_RXON_CTX_BSS);
}

static inline int iwl_legacy_is_associated_ctx(struct iwl_rxon_context *ctx)
{
	return (ctx->active.filter_flags & RXON_FILTER_ASSOC_MSK) ? 1 : 0;
}

static inline int iwl_legacy_is_channel_valid(const struct iwl_channel_info *ch_info)
{
	if (ch_info == NULL)
		return 0;
	return (ch_info->flags & EEPROM_CHANNEL_VALID) ? 1 : 0;
}

static inline int iwl_legacy_is_channel_radar(const struct iwl_channel_info *ch_info)
{
	return (ch_info->flags & EEPROM_CHANNEL_RADAR) ? 1 : 0;
}

static inline u8 iwl_legacy_is_channel_a_band(const struct iwl_channel_info *ch_info)
{
	return ch_info->band == IEEE80211_BAND_5GHZ;
}

static inline int
iwl_legacy_is_channel_passive(const struct iwl_channel_info *ch)
{
	return (!(ch->flags & EEPROM_CHANNEL_ACTIVE)) ? 1 : 0;
}

static inline int
iwl_legacy_is_channel_ibss(const struct iwl_channel_info *ch)
{
	return (ch->flags & EEPROM_CHANNEL_IBSS) ? 1 : 0;
}

static inline void
__iwl_legacy_free_pages(struct iwl_priv *priv, struct page *page)
{
	__free_pages(page, priv->hw_params.rx_page_order);
	priv->alloc_rxb_page--;
}

static inline void iwl_legacy_free_pages(struct iwl_priv *priv, unsigned long page)
{
	free_pages(page, priv->hw_params.rx_page_order);
	priv->alloc_rxb_page--;
}
#endif				/* __iwl_legacy_dev_h__ */
