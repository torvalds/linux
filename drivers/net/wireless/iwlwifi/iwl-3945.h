/******************************************************************************
 *
 * Copyright(c) 2003 - 2008 Intel Corporation. All rights reserved.
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
/*
 * Please use this file (iwl-3945.h) for driver implementation definitions.
 * Please use iwl-3945-commands.h for uCode API definitions.
 * Please use iwl-3945-hw.h for hardware-related definitions.
 */

#ifndef __iwl_3945_h__
#define __iwl_3945_h__

#include <linux/pci.h> /* for struct pci_device_id */
#include <linux/kernel.h>
#include <net/ieee80211_radiotap.h>

/*used for rfkill*/
#include <linux/rfkill.h>
#include <linux/input.h>

/* Hardware specific file defines the PCI IDs table for that hardware module */
extern struct pci_device_id iwl3945_hw_card_ids[];

#define DRV_NAME	"iwl3945"
#include "iwl-csr.h"
#include "iwl-prph.h"
#include "iwl-3945-hw.h"
#include "iwl-3945-debug.h"
#include "iwl-3945-led.h"

/* Change firmware file name, using "-" and incrementing number,
 *   *only* when uCode interface or architecture changes so that it
 *   is not compatible with earlier drivers.
 * This number will also appear in << 8 position of 1st dword of uCode file */
#define IWL3945_UCODE_API "-1"

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

/* Module parameters accessible from iwl-*.c */
extern int iwl3945_param_hwcrypto;
extern int iwl3945_param_queues_num;

enum iwl3945_antenna {
	IWL_ANTENNA_DIVERSITY,
	IWL_ANTENNA_MAIN,
	IWL_ANTENNA_AUX
};

/*
 * RTS threshold here is total size [2347] minus 4 FCS bytes
 * Per spec:
 *   a value of 0 means RTS on all data/management packets
 *   a value > max MSDU size means no RTS
 * else RTS for data/management frames where MPDU is larger
 *   than RTS value.
 */
#define IWL_RX_BUF_SIZE           3000U
#define DEFAULT_RTS_THRESHOLD     2347U
#define MIN_RTS_THRESHOLD         0U
#define MAX_RTS_THRESHOLD         2347U
#define MAX_MSDU_SIZE		  2304U
#define MAX_MPDU_SIZE		  2346U
#define DEFAULT_BEACON_INTERVAL   100U
#define	DEFAULT_SHORT_RETRY_LIMIT 7U
#define	DEFAULT_LONG_RETRY_LIMIT  4U

struct iwl3945_rx_mem_buffer {
	dma_addr_t dma_addr;
	struct sk_buff *skb;
	struct list_head list;
};

/*
 * Generic queue structure
 *
 * Contains common data for Rx and Tx queues
 */
struct iwl3945_queue {
	int n_bd;              /* number of BDs in this queue */
	int write_ptr;       /* 1-st empty entry (index) host_w*/
	int read_ptr;         /* last used entry (index) host_r*/
	dma_addr_t dma_addr;   /* physical addr for BD's */
	int n_window;	       /* safe queue window */
	u32 id;
	int low_mark;	       /* low watermark, resume queue if free
				* space more than this */
	int high_mark;         /* high watermark, stop queue if free
				* space less than this */
} __attribute__ ((packed));

int iwl3945_queue_space(const struct iwl3945_queue *q);
int iwl3945_x2_queue_used(const struct iwl3945_queue *q, int i);

#define MAX_NUM_OF_TBS          (20)

/* One for each TFD */
struct iwl3945_tx_info {
	struct sk_buff *skb[MAX_NUM_OF_TBS];
};

/**
 * struct iwl3945_tx_queue - Tx Queue for DMA
 * @q: generic Rx/Tx queue descriptor
 * @bd: base of circular buffer of TFDs
 * @cmd: array of command/Tx buffers
 * @dma_addr_cmd: physical address of cmd/tx buffer array
 * @txb: array of per-TFD driver data
 * @need_update: indicates need to update read/write index
 *
 * A Tx queue consists of circular buffer of BDs (a.k.a. TFDs, transmit frame
 * descriptors) and required locking structures.
 */
struct iwl3945_tx_queue {
	struct iwl3945_queue q;
	struct iwl3945_tfd_frame *bd;
	struct iwl3945_cmd *cmd;
	dma_addr_t dma_addr_cmd;
	struct iwl3945_tx_info *txb;
	int need_update;
	int active;
};

#define IWL_NUM_SCAN_RATES         (2)

struct iwl3945_channel_tgd_info {
	u8 type;
	s8 max_power;
};

struct iwl3945_channel_tgh_info {
	s64 last_radar_time;
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
#define IWL4965_MAX_RATE (33)

struct iwl3945_channel_info {
	struct iwl3945_channel_tgd_info tgd;
	struct iwl3945_channel_tgh_info tgh;
	struct iwl3945_eeprom_channel eeprom;	/* EEPROM regulatory limit */
	struct iwl3945_eeprom_channel fat_eeprom;	/* EEPROM regulatory limit for
						 * FAT channel */

	u8 channel;	  /* channel number */
	u8 flags;	  /* flags copied from EEPROM */
	s8 max_power_avg; /* (dBm) regul. eeprom, normal Tx, any rate */
	s8 curr_txpow;	  /* (dBm) regulatory/spectrum/user (not h/w) */
	s8 min_power;	  /* always 0 */
	s8 scan_power;	  /* (dBm) regul. eeprom, direct scans, any rate */

	u8 group_index;	  /* 0-4, maps channel to group1/2/3/4/5 */
	u8 band_index;	  /* 0-4, maps channel to band1/2/3/4/5 */
	enum ieee80211_band band;

	/* Radio/DSP gain settings for each "normal" data Tx rate.
	 * These include, in addition to RF and DSP gain, a few fields for
	 *   remembering/modifying gain settings (indexes). */
	struct iwl3945_channel_power_info power_info[IWL4965_MAX_RATE];

	/* Radio/DSP gain settings for each scan rate, for directed scans. */
	struct iwl3945_scan_power_info scan_pwr_info[IWL_NUM_SCAN_RATES];
};

struct iwl3945_clip_group {
	/* maximum power level to prevent clipping for each rate, derived by
	 *   us from this band's saturation power in EEPROM */
	const s8 clip_powers[IWL_MAX_RATES];
};

#include "iwl-3945-rs.h"

#define IWL_TX_FIFO_AC0	0
#define IWL_TX_FIFO_AC1	1
#define IWL_TX_FIFO_AC2	2
#define IWL_TX_FIFO_AC3	3
#define IWL_TX_FIFO_HCCA_1	5
#define IWL_TX_FIFO_HCCA_2	6
#define IWL_TX_FIFO_NONE	7

/* Minimum number of queues. MAX_NUM is defined in hw specific files */
#define IWL_MIN_NUM_QUEUES	4

/* Power management (not Tx power) structures */

struct iwl3945_power_vec_entry {
	struct iwl3945_powertable_cmd cmd;
	u8 no_dtim;
};
#define IWL_POWER_RANGE_0  (0)
#define IWL_POWER_RANGE_1  (1)

#define IWL_POWER_MODE_CAM	0x00	/* Continuously Aware Mode, always on */
#define IWL_POWER_INDEX_3	0x03
#define IWL_POWER_INDEX_5	0x05
#define IWL_POWER_AC		0x06
#define IWL_POWER_BATTERY	0x07
#define IWL_POWER_LIMIT		0x07
#define IWL_POWER_MASK		0x0F
#define IWL_POWER_ENABLED	0x10
#define IWL_POWER_LEVEL(x)	((x) & IWL_POWER_MASK)

struct iwl3945_power_mgr {
	spinlock_t lock;
	struct iwl3945_power_vec_entry pwr_range_0[IWL_POWER_AC];
	struct iwl3945_power_vec_entry pwr_range_1[IWL_POWER_AC];
	u8 active_index;
	u32 dtim_val;
};

#define IEEE80211_DATA_LEN              2304
#define IEEE80211_4ADDR_LEN             30
#define IEEE80211_HLEN                  (IEEE80211_4ADDR_LEN)
#define IEEE80211_FRAME_LEN             (IEEE80211_DATA_LEN + IEEE80211_HLEN)

struct iwl3945_frame {
	union {
		struct ieee80211_hdr frame;
		struct iwl3945_tx_beacon_cmd beacon;
		u8 raw[IEEE80211_FRAME_LEN];
		u8 cmd[360];
	} u;
	struct list_head list;
};

#define SEQ_TO_QUEUE(x)  ((x >> 8) & 0xbf)
#define QUEUE_TO_SEQ(x)  ((x & 0xbf) << 8)
#define SEQ_TO_INDEX(x) ((u8)(x & 0xff))
#define INDEX_TO_SEQ(x) ((u8)(x & 0xff))
#define SEQ_HUGE_FRAME  (0x4000)
#define SEQ_RX_FRAME    __constant_cpu_to_le16(0x8000)
#define SEQ_TO_SN(seq) (((seq) & IEEE80211_SCTL_SEQ) >> 4)
#define SN_TO_SEQ(ssn) (((ssn) << 4) & IEEE80211_SCTL_SEQ)
#define MAX_SN ((IEEE80211_SCTL_SEQ) >> 4)

enum {
	/* CMD_SIZE_NORMAL = 0, */
	CMD_SIZE_HUGE = (1 << 0),
	/* CMD_SYNC = 0, */
	CMD_ASYNC = (1 << 1),
	/* CMD_NO_SKB = 0, */
	CMD_WANT_SKB = (1 << 2),
};

struct iwl3945_cmd;
struct iwl3945_priv;

struct iwl3945_cmd_meta {
	struct iwl3945_cmd_meta *source;
	union {
		struct sk_buff *skb;
		int (*callback)(struct iwl3945_priv *priv,
				struct iwl3945_cmd *cmd, struct sk_buff *skb);
	} __attribute__ ((packed)) u;

	/* The CMD_SIZE_HUGE flag bit indicates that the command
	 * structure is stored at the end of the shared queue memory. */
	u32 flags;

} __attribute__ ((packed));

/**
 * struct iwl3945_cmd
 *
 * For allocation of the command and tx queues, this establishes the overall
 * size of the largest command we send to uCode, except for a scan command
 * (which is relatively huge; space is allocated separately).
 */
struct iwl3945_cmd {
	struct iwl3945_cmd_meta meta;
	struct iwl3945_cmd_header hdr;
	union {
		struct iwl3945_addsta_cmd addsta;
		struct iwl3945_led_cmd led;
		u32 flags;
		u8 val8;
		u16 val16;
		u32 val32;
		struct iwl3945_bt_cmd bt;
		struct iwl3945_rxon_time_cmd rxon_time;
		struct iwl3945_powertable_cmd powertable;
		struct iwl3945_qosparam_cmd qosparam;
		struct iwl3945_tx_cmd tx;
		struct iwl3945_tx_beacon_cmd tx_beacon;
		struct iwl3945_rxon_assoc_cmd rxon_assoc;
		u8 *indirect;
		u8 payload[360];
	} __attribute__ ((packed)) cmd;
} __attribute__ ((packed));

struct iwl3945_host_cmd {
	u8 id;
	u16 len;
	struct iwl3945_cmd_meta meta;
	const void *data;
};

#define TFD_MAX_PAYLOAD_SIZE (sizeof(struct iwl3945_cmd) - \
			      sizeof(struct iwl3945_cmd_meta))

/*
 * RX related structures and functions
 */
#define RX_FREE_BUFFERS 64
#define RX_LOW_WATERMARK 8

#define SUP_RATE_11A_MAX_NUM_CHANNELS  8
#define SUP_RATE_11B_MAX_NUM_CHANNELS  4
#define SUP_RATE_11G_MAX_NUM_CHANNELS  12

/**
 * struct iwl3945_rx_queue - Rx queue
 * @processed: Internal index to last handled Rx packet
 * @read: Shared index to newest available Rx buffer
 * @write: Shared index to oldest written Rx packet
 * @free_count: Number of pre-allocated buffers in rx_free
 * @rx_free: list of free SKBs for use
 * @rx_used: List of Rx buffers with no SKB
 * @need_update: flag to indicate we need to update read/write index
 *
 * NOTE:  rx_free and rx_used are used as a FIFO for iwl3945_rx_mem_buffers
 */
struct iwl3945_rx_queue {
	__le32 *bd;
	dma_addr_t dma_addr;
	struct iwl3945_rx_mem_buffer pool[RX_QUEUE_SIZE + RX_FREE_BUFFERS];
	struct iwl3945_rx_mem_buffer *queue[RX_QUEUE_SIZE];
	u32 processed;
	u32 read;
	u32 write;
	u32 free_count;
	struct list_head rx_free;
	struct list_head rx_used;
	int need_update;
	spinlock_t lock;
};

#define IWL_SUPPORTED_RATES_IE_LEN         8

#define SCAN_INTERVAL 100

#define MAX_A_CHANNELS  252
#define MIN_A_CHANNELS  7

#define MAX_B_CHANNELS  14
#define MIN_B_CHANNELS  1

#define STATUS_HCMD_ACTIVE	0	/* host command in progress */
#define STATUS_HCMD_SYNC_ACTIVE	1	/* sync host command in progress */
#define STATUS_INT_ENABLED	2
#define STATUS_RF_KILL_HW	3
#define STATUS_RF_KILL_SW	4
#define STATUS_INIT		5
#define STATUS_ALIVE		6
#define STATUS_READY		7
#define STATUS_TEMPERATURE	8
#define STATUS_GEO_CONFIGURED	9
#define STATUS_EXIT_PENDING	10
#define STATUS_IN_SUSPEND	11
#define STATUS_STATISTICS	12
#define STATUS_SCANNING		13
#define STATUS_SCAN_ABORTING	14
#define STATUS_SCAN_HW		15
#define STATUS_POWER_PMI	16
#define STATUS_FW_ERROR		17
#define STATUS_CONF_PENDING	18

#define MAX_TID_COUNT        9

#define IWL_INVALID_RATE     0xFF
#define IWL_INVALID_VALUE    -1

struct iwl3945_tid_data {
	u16 seq_number;
};

struct iwl3945_hw_key {
	enum ieee80211_key_alg alg;
	int keylen;
	u8 key[32];
};

union iwl3945_ht_rate_supp {
	u16 rates;
	struct {
		u8 siso_rate;
		u8 mimo_rate;
	};
};

union iwl3945_qos_capabity {
	struct {
		u8 edca_count:4;	/* bit 0-3 */
		u8 q_ack:1;		/* bit 4 */
		u8 queue_request:1;	/* bit 5 */
		u8 txop_request:1;	/* bit 6 */
		u8 reserved:1;		/* bit 7 */
	} q_AP;
	struct {
		u8 acvo_APSD:1;		/* bit 0 */
		u8 acvi_APSD:1;		/* bit 1 */
		u8 ac_bk_APSD:1;	/* bit 2 */
		u8 ac_be_APSD:1;	/* bit 3 */
		u8 q_ack:1;		/* bit 4 */
		u8 max_len:2;		/* bit 5-6 */
		u8 more_data_ack:1;	/* bit 7 */
	} q_STA;
	u8 val;
};

/* QoS structures */
struct iwl3945_qos_info {
	int qos_enable;
	int qos_active;
	union iwl3945_qos_capabity qos_cap;
	struct iwl3945_qosparam_cmd def_qos_parm;
};

#define STA_PS_STATUS_WAKE             0
#define STA_PS_STATUS_SLEEP            1

struct iwl3945_station_entry {
	struct iwl3945_addsta_cmd sta;
	struct iwl3945_tid_data tid[MAX_TID_COUNT];
	union {
		struct {
			u8 rate;
			u8 flags;
		} s;
		u16 rate_n_flags;
	} current_rate;
	u8 used;
	u8 ps_status;
	struct iwl3945_hw_key keyinfo;
};

/* one for each uCode image (inst/data, boot/init/runtime) */
struct fw_desc {
	void *v_addr;		/* access by driver */
	dma_addr_t p_addr;	/* access by card's busmaster DMA */
	u32 len;		/* bytes */
};

/* uCode file layout */
struct iwl3945_ucode {
	__le32 ver;		/* major/minor/subminor */
	__le32 inst_size;	/* bytes of runtime instructions */
	__le32 data_size;	/* bytes of runtime data */
	__le32 init_size;	/* bytes of initialization instructions */
	__le32 init_data_size;	/* bytes of initialization data */
	__le32 boot_size;	/* bytes of bootstrap instructions */
	u8 data[0];		/* data in same order as "size" elements */
};

struct iwl3945_ibss_seq {
	u8 mac[ETH_ALEN];
	u16 seq_num;
	u16 frag_num;
	unsigned long packet_time;
	struct list_head list;
};

/**
 * struct iwl3945_driver_hw_info
 * @max_txq_num: Max # Tx queues supported
 * @tx_cmd_len: Size of Tx command (but not including frame itself)
 * @tx_ant_num: Number of TX antennas
 * @max_rxq_size: Max # Rx frames in Rx queue (must be power-of-2)
 * @rx_buf_size:
 * @max_pkt_size:
 * @max_rxq_log: Log-base-2 of max_rxq_size
 * @max_stations:
 * @bcast_sta_id:
 * @shared_virt: Pointer to driver/uCode shared Tx Byte Counts and Rx status
 * @shared_phys: Physical Pointer to Tx Byte Counts and Rx status
 */
struct iwl3945_driver_hw_info {
	u16 max_txq_num;
	u16 tx_cmd_len;
	u16 tx_ant_num;
	u16 max_rxq_size;
	u32 rx_buf_size;
	u32 max_pkt_size;
	u16 max_rxq_log;
	u8  max_stations;
	u8  bcast_sta_id;
	void *shared_virt;
	dma_addr_t shared_phys;
};

#define IWL_RX_HDR(x) ((struct iwl3945_rx_frame_hdr *)(\
		       x->u.rx_frame.stats.payload + \
		       x->u.rx_frame.stats.phy_count))
#define IWL_RX_END(x) ((struct iwl3945_rx_frame_end *)(\
		       IWL_RX_HDR(x)->payload + \
		       le16_to_cpu(IWL_RX_HDR(x)->len)))
#define IWL_RX_STATS(x) (&x->u.rx_frame.stats)
#define IWL_RX_DATA(x) (IWL_RX_HDR(x)->payload)


/******************************************************************************
 *
 * Functions implemented in iwl-base.c which are forward declared here
 * for use by iwl-*.c
 *
 *****************************************************************************/
struct iwl3945_addsta_cmd;
extern int iwl3945_send_add_station(struct iwl3945_priv *priv,
				struct iwl3945_addsta_cmd *sta, u8 flags);
extern u8 iwl3945_add_station(struct iwl3945_priv *priv, const u8 *bssid,
			  int is_ap, u8 flags);
extern int iwl3945_power_init_handle(struct iwl3945_priv *priv);
extern int iwl3945_eeprom_init(struct iwl3945_priv *priv);
extern int iwl3945_rx_queue_alloc(struct iwl3945_priv *priv);
extern void iwl3945_rx_queue_reset(struct iwl3945_priv *priv,
			       struct iwl3945_rx_queue *rxq);
extern int iwl3945_calc_db_from_ratio(int sig_ratio);
extern int iwl3945_calc_sig_qual(int rssi_dbm, int noise_dbm);
extern int iwl3945_tx_queue_init(struct iwl3945_priv *priv,
			     struct iwl3945_tx_queue *txq, int count, u32 id);
extern void iwl3945_rx_replenish(void *data);
extern void iwl3945_tx_queue_free(struct iwl3945_priv *priv, struct iwl3945_tx_queue *txq);
extern int iwl3945_send_cmd_pdu(struct iwl3945_priv *priv, u8 id, u16 len,
			    const void *data);
extern int __must_check iwl3945_send_cmd(struct iwl3945_priv *priv,
		struct iwl3945_host_cmd *cmd);
extern unsigned int iwl3945_fill_beacon_frame(struct iwl3945_priv *priv,
					struct ieee80211_hdr *hdr,
					const u8 *dest, int left);
extern int iwl3945_rx_queue_update_write_ptr(struct iwl3945_priv *priv,
					 struct iwl3945_rx_queue *q);
extern int iwl3945_send_statistics_request(struct iwl3945_priv *priv);
extern void iwl3945_set_decrypted_flag(struct iwl3945_priv *priv, struct sk_buff *skb,
				   u32 decrypt_res,
				   struct ieee80211_rx_status *stats);
extern const u8 iwl3945_broadcast_addr[ETH_ALEN];

/*
 * Currently used by iwl-3945-rs... look at restructuring so that it doesn't
 * call this... todo... fix that.
*/
extern u8 iwl3945_sync_station(struct iwl3945_priv *priv, int sta_id,
			   u16 tx_rate, u8 flags);

/******************************************************************************
 *
 * Functions implemented in iwl-[34]*.c which are forward declared here
 * for use by iwl-base.c
 *
 * NOTE:  The implementation of these functions are hardware specific
 * which is why they are in the hardware specific files (vs. iwl-base.c)
 *
 * Naming convention --
 * iwl3945_         <-- Its part of iwlwifi (should be changed to iwl3945_)
 * iwl3945_hw_      <-- Hardware specific (implemented in iwl-XXXX.c by all HW)
 * iwlXXXX_     <-- Hardware specific (implemented in iwl-XXXX.c for XXXX)
 * iwl3945_bg_      <-- Called from work queue context
 * iwl3945_mac_     <-- mac80211 callback
 *
 ****************************************************************************/
extern void iwl3945_hw_rx_handler_setup(struct iwl3945_priv *priv);
extern void iwl3945_hw_setup_deferred_work(struct iwl3945_priv *priv);
extern void iwl3945_hw_cancel_deferred_work(struct iwl3945_priv *priv);
extern int iwl3945_hw_rxq_stop(struct iwl3945_priv *priv);
extern int iwl3945_hw_set_hw_setting(struct iwl3945_priv *priv);
extern int iwl3945_hw_nic_init(struct iwl3945_priv *priv);
extern int iwl3945_hw_nic_stop_master(struct iwl3945_priv *priv);
extern void iwl3945_hw_txq_ctx_free(struct iwl3945_priv *priv);
extern void iwl3945_hw_txq_ctx_stop(struct iwl3945_priv *priv);
extern int iwl3945_hw_nic_reset(struct iwl3945_priv *priv);
extern int iwl3945_hw_txq_attach_buf_to_tfd(struct iwl3945_priv *priv, void *tfd,
					dma_addr_t addr, u16 len);
extern int iwl3945_hw_txq_free_tfd(struct iwl3945_priv *priv, struct iwl3945_tx_queue *txq);
extern int iwl3945_hw_get_temperature(struct iwl3945_priv *priv);
extern int iwl3945_hw_tx_queue_init(struct iwl3945_priv *priv,
				struct iwl3945_tx_queue *txq);
extern unsigned int iwl3945_hw_get_beacon_cmd(struct iwl3945_priv *priv,
				 struct iwl3945_frame *frame, u8 rate);
extern int iwl3945_hw_get_rx_read(struct iwl3945_priv *priv);
extern void iwl3945_hw_build_tx_cmd_rate(struct iwl3945_priv *priv,
				     struct iwl3945_cmd *cmd,
				     struct ieee80211_tx_info *info,
				     struct ieee80211_hdr *hdr,
				     int sta_id, int tx_id);
extern int iwl3945_hw_reg_send_txpower(struct iwl3945_priv *priv);
extern int iwl3945_hw_reg_set_txpower(struct iwl3945_priv *priv, s8 power);
extern void iwl3945_hw_rx_statistics(struct iwl3945_priv *priv,
				 struct iwl3945_rx_mem_buffer *rxb);
extern void iwl3945_disable_events(struct iwl3945_priv *priv);
extern int iwl4965_get_temperature(const struct iwl3945_priv *priv);

/**
 * iwl3945_hw_find_station - Find station id for a given BSSID
 * @bssid: MAC address of station ID to find
 *
 * NOTE:  This should not be hardware specific but the code has
 * not yet been merged into a single common layer for managing the
 * station tables.
 */
extern u8 iwl3945_hw_find_station(struct iwl3945_priv *priv, const u8 *bssid);

extern int iwl3945_hw_channel_switch(struct iwl3945_priv *priv, u16 channel);

/*
 * Forward declare iwl-3945.c functions for iwl-base.c
 */
extern __le32 iwl3945_get_antenna_flags(const struct iwl3945_priv *priv);
extern int iwl3945_init_hw_rate_table(struct iwl3945_priv *priv);
extern void iwl3945_reg_txpower_periodic(struct iwl3945_priv *priv);
extern int iwl3945_txpower_set_from_eeprom(struct iwl3945_priv *priv);
extern u8 iwl3945_sync_sta(struct iwl3945_priv *priv, int sta_id,
		 u16 tx_rate, u8 flags);


#ifdef CONFIG_IWL3945_SPECTRUM_MEASUREMENT

enum {
	MEASUREMENT_READY = (1 << 0),
	MEASUREMENT_ACTIVE = (1 << 1),
};

#endif

#ifdef CONFIG_IWL3945_RFKILL
struct iwl3945_priv;

void iwl3945_rfkill_set_hw_state(struct iwl3945_priv *priv);
void iwl3945_rfkill_unregister(struct iwl3945_priv *priv);
int iwl3945_rfkill_init(struct iwl3945_priv *priv);
#else
static inline void iwl3945_rfkill_set_hw_state(struct iwl3945_priv *priv) {}
static inline void iwl3945_rfkill_unregister(struct iwl3945_priv *priv) {}
static inline int iwl3945_rfkill_init(struct iwl3945_priv *priv) { return 0; }
#endif

#define IWL_MAX_NUM_QUEUES IWL39_MAX_NUM_QUEUES

struct iwl3945_priv {

	/* ieee device used by generic ieee processing code */
	struct ieee80211_hw *hw;
	struct ieee80211_channel *ieee_channels;
	struct ieee80211_rate *ieee_rates;
	struct iwl_3945_cfg *cfg; /* device configuration */

	/* temporary frame storage list */
	struct list_head free_frames;
	int frames_count;

	enum ieee80211_band band;
	int alloc_rxb_skb;

	void (*rx_handlers[REPLY_MAX])(struct iwl3945_priv *priv,
				       struct iwl3945_rx_mem_buffer *rxb);

	struct ieee80211_supported_band bands[IEEE80211_NUM_BANDS];

#ifdef CONFIG_IWL3945_SPECTRUM_MEASUREMENT
	/* spectrum measurement report caching */
	struct iwl3945_spectrum_notification measure_report;
	u8 measurement_status;
#endif
	/* ucode beacon time */
	u32 ucode_beacon_time;

	/* we allocate array of iwl3945_channel_info for NIC's valid channels.
	 *    Access via channel # using indirect index array */
	struct iwl3945_channel_info *channel_info;	/* channel info array */
	u8 channel_count;	/* # of channels */

	/* each calibration channel group in the EEPROM has a derived
	 * clip setting for each rate. */
	const struct iwl3945_clip_group clip_groups[5];

	/* thermal calibration */
	s32 temperature;	/* degrees Kelvin */
	s32 last_temperature;

	/* Scan related variables */
	unsigned long last_scan_jiffies;
	unsigned long next_scan_jiffies;
	unsigned long scan_start;
	unsigned long scan_pass_start;
	unsigned long scan_start_tsf;
	int scan_bands;
	int one_direct_scan;
	u8 direct_ssid_len;
	u8 direct_ssid[IW_ESSID_MAX_SIZE];
	struct iwl3945_scan_cmd *scan;

	/* spinlock */
	spinlock_t lock;	/* protect general shared data */
	spinlock_t hcmd_lock;	/* protect hcmd */
	struct mutex mutex;

	/* basic pci-network driver stuff */
	struct pci_dev *pci_dev;

	/* pci hardware address support */
	void __iomem *hw_base;

	/* uCode images, save to reload in case of failure */
	struct fw_desc ucode_code;	/* runtime inst */
	struct fw_desc ucode_data;	/* runtime data original */
	struct fw_desc ucode_data_backup;	/* runtime data save/restore */
	struct fw_desc ucode_init;	/* initialization inst */
	struct fw_desc ucode_init_data;	/* initialization data */
	struct fw_desc ucode_boot;	/* bootstrap inst */


	struct iwl3945_rxon_time_cmd rxon_timing;

	/* We declare this const so it can only be
	 * changed via explicit cast within the
	 * routines that actually update the physical
	 * hardware */
	const struct iwl3945_rxon_cmd active_rxon;
	struct iwl3945_rxon_cmd staging_rxon;

	int error_recovering;
	struct iwl3945_rxon_cmd recovery_rxon;

	/* 1st responses from initialize and runtime uCode images.
	 * 4965's initialize alive response contains some calibration data. */
	struct iwl3945_init_alive_resp card_alive_init;
	struct iwl3945_alive_resp card_alive;

#ifdef CONFIG_IWL3945_RFKILL
	struct rfkill *rfkill;
#endif

#ifdef CONFIG_IWL3945_LEDS
	struct iwl3945_led led[IWL_LED_TRG_MAX];
	unsigned long last_blink_time;
	u8 last_blink_rate;
	u8 allow_blinking;
	unsigned int rxtxpackets;
	u64 led_tpt;
#endif


	u16 active_rate;
	u16 active_rate_basic;

	u8 call_post_assoc_from_beacon;
	/* Rate scaling data */
	s8 data_retry_limit;
	u8 retry_rate;

	wait_queue_head_t wait_command_queue;

	int activity_timer_active;

	/* Rx and Tx DMA processing queues */
	struct iwl3945_rx_queue rxq;
	struct iwl3945_tx_queue txq[IWL_MAX_NUM_QUEUES];

	unsigned long status;

	int last_rx_rssi;	/* From Rx packet statisitics */
	int last_rx_noise;	/* From beacon statistics */

	struct iwl3945_power_mgr power_data;

	struct iwl3945_notif_statistics statistics;
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
	struct iwl3945_station_entry stations[IWL_STATION_COUNT];

	/* Indication if ieee80211_ops->open has been called */
	u8 is_open;

	u8 mac80211_registered;

	/* Rx'd packet timing information */
	u32 last_beacon_time;
	u64 last_tsf;

	/* eeprom */
	struct iwl3945_eeprom eeprom;

	enum nl80211_iftype iw_mode;

	struct sk_buff *ibss_beacon;

	/* Last Rx'd beacon timestamp */
	u32 timestamp0;
	u32 timestamp1;
	u16 beacon_int;
	struct iwl3945_driver_hw_info hw_setting;
	struct ieee80211_vif *vif;

	/* Current association information needed to configure the
	 * hardware */
	u16 assoc_id;
	u16 assoc_capability;
	u8 ps_mode;

	struct iwl3945_qos_info qos_data;

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
	struct work_struct set_monitor;

	struct tasklet_struct irq_tasklet;

	struct delayed_work init_alive_start;
	struct delayed_work alive_start;
	struct delayed_work activity_timer;
	struct delayed_work thermal_periodic;
	struct delayed_work gather_stats;
	struct delayed_work scan_check;

#define IWL_DEFAULT_TX_POWER 0x0F
	s8 user_txpower_limit;
	s8 max_channel_txpower_limit;

#ifdef CONFIG_PM
	u32 pm_state[16];
#endif

#ifdef CONFIG_IWL3945_DEBUG
	/* debugging info */
	u32 framecnt_to_us;
	atomic_t restrict_refcnt;
#endif
};				/*iwl3945_priv */

static inline int iwl3945_is_associated(struct iwl3945_priv *priv)
{
	return (priv->active_rxon.filter_flags & RXON_FILTER_ASSOC_MSK) ? 1 : 0;
}

static inline int is_channel_valid(const struct iwl3945_channel_info *ch_info)
{
	if (ch_info == NULL)
		return 0;
	return (ch_info->flags & EEPROM_CHANNEL_VALID) ? 1 : 0;
}

static inline int is_channel_radar(const struct iwl3945_channel_info *ch_info)
{
	return (ch_info->flags & EEPROM_CHANNEL_RADAR) ? 1 : 0;
}

static inline u8 is_channel_a_band(const struct iwl3945_channel_info *ch_info)
{
	return ch_info->band == IEEE80211_BAND_5GHZ;
}

static inline u8 is_channel_bg_band(const struct iwl3945_channel_info *ch_info)
{
	return ch_info->band == IEEE80211_BAND_2GHZ;
}

static inline int is_channel_passive(const struct iwl3945_channel_info *ch)
{
	return (!(ch->flags & EEPROM_CHANNEL_ACTIVE)) ? 1 : 0;
}

static inline int is_channel_ibss(const struct iwl3945_channel_info *ch)
{
	return ((ch->flags & EEPROM_CHANNEL_IBSS)) ? 1 : 0;
}

extern const struct iwl3945_channel_info *iwl3945_get_channel_info(
	const struct iwl3945_priv *priv, enum ieee80211_band band, u16 channel);

/* Requires full declaration of iwl3945_priv before including */
#include "iwl-3945-io.h"

#endif
