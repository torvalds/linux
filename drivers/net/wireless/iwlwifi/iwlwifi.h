/******************************************************************************
 *
 * Copyright(c) 2003 - 2007 Intel Corporation. All rights reserved.
 *
 * Portions of this file are derived from the ipw3945 project, as well
 * as portions of the ieee80211 subsystem header files.
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

#ifndef __iwlwifi_h__
#define __iwlwifi_h__

#include <linux/pci.h> /* for struct pci_device_id */
#include <linux/kernel.h>
#include <net/ieee80211_radiotap.h>

struct iwl_priv;

/* Hardware specific file defines the PCI IDs table for that hardware module */
extern struct pci_device_id iwl_hw_card_ids[];

#if IWL == 3945

#define DRV_NAME	"iwl3945"
#include "iwl-hw.h"
#include "iwl-3945-hw.h"

#elif IWL == 4965

#define DRV_NAME        "iwl4965"
#include "iwl-hw.h"
#include "iwl-4965-hw.h"

#endif

#include "iwl-prph.h"

/*
 * Driver implementation data structures, constants, inline
 * functions
 *
 * NOTE:  DO NOT PUT HARDWARE/UCODE SPECIFIC DECLRATIONS HERE
 *
 * Hardware specific declrations go into iwl-*hw.h
 *
 */

#include "iwl-debug.h"

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
extern int iwl_param_disable_hw_scan;
extern int iwl_param_debug;
extern int iwl_param_mode;
extern int iwl_param_disable;
extern int iwl_param_antenna;
extern int iwl_param_hwcrypto;
extern int iwl_param_qos_enable;
extern int iwl_param_queues_num;

enum iwl_antenna {
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
#define DEFAULT_RTS_THRESHOLD     2347U
#define MIN_RTS_THRESHOLD         0U
#define MAX_RTS_THRESHOLD         2347U
#define MAX_MSDU_SIZE		  2304U
#define MAX_MPDU_SIZE		  2346U
#define DEFAULT_BEACON_INTERVAL   100U
#define	DEFAULT_SHORT_RETRY_LIMIT 7U
#define	DEFAULT_LONG_RETRY_LIMIT  4U

struct iwl_rx_mem_buffer {
	dma_addr_t dma_addr;
	struct sk_buff *skb;
	struct list_head list;
};

struct iwl_rt_rx_hdr {
	struct ieee80211_radiotap_header rt_hdr;
	__le64 rt_tsf;		/* TSF */
	u8 rt_flags;		/* radiotap packet flags */
	u8 rt_rate;		/* rate in 500kb/s */
	__le16 rt_channelMHz;	/* channel in MHz */
	__le16 rt_chbitmask;	/* channel bitfield */
	s8 rt_dbmsignal;	/* signal in dBm, kluged to signed */
	s8 rt_dbmnoise;
	u8 rt_antenna;		/* antenna number */
	u8 payload[0];		/* payload... */
} __attribute__ ((packed));

struct iwl_rt_tx_hdr {
	struct ieee80211_radiotap_header rt_hdr;
	u8 rt_rate;		/* rate in 500kb/s */
	__le16 rt_channel;	/* channel in mHz */
	__le16 rt_chbitmask;	/* channel bitfield */
	s8 rt_dbmsignal;	/* signal in dBm, kluged to signed */
	u8 rt_antenna;		/* antenna number */
	u8 payload[0];		/* payload... */
} __attribute__ ((packed));

/*
 * Generic queue structure
 *
 * Contains common data for Rx and Tx queues
 */
struct iwl_queue {
	int n_bd;              /* number of BDs in this queue */
	int first_empty;       /* 1-st empty entry (index) host_w*/
	int last_used;         /* last used entry (index) host_r*/
	dma_addr_t dma_addr;   /* physical addr for BD's */
	int n_window;	       /* safe queue window */
	u32 id;
	int low_mark;	       /* low watermark, resume queue if free
				* space more than this */
	int high_mark;         /* high watermark, stop queue if free
				* space less than this */
} __attribute__ ((packed));

#define MAX_NUM_OF_TBS          (20)

struct iwl_tx_info {
	struct ieee80211_tx_status status;
	struct sk_buff *skb[MAX_NUM_OF_TBS];
};

/**
 * struct iwl_tx_queue - Tx Queue for DMA
 * @need_update: need to update read/write index
 * @shed_retry: queue is HT AGG enabled
 *
 * Queue consists of circular buffer of BD's and required locking structures.
 */
struct iwl_tx_queue {
	struct iwl_queue q;
	struct iwl_tfd_frame *bd;
	struct iwl_cmd *cmd;
	dma_addr_t dma_addr_cmd;
	struct iwl_tx_info *txb;
	int need_update;
	int sched_retry;
	int active;
};

#include "iwl-channel.h"

#if IWL == 3945
#include "iwl-3945-rs.h"
#else
#include "iwl-4965-rs.h"
#endif

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

struct iwl_power_vec_entry {
	struct iwl_powertable_cmd cmd;
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

struct iwl_power_mgr {
	spinlock_t lock;
	struct iwl_power_vec_entry pwr_range_0[IWL_POWER_AC];
	struct iwl_power_vec_entry pwr_range_1[IWL_POWER_AC];
	u8 active_index;
	u32 dtim_val;
};

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

#define SEQ_TO_QUEUE(x)  ((x >> 8) & 0xbf)
#define QUEUE_TO_SEQ(x)  ((x & 0xbf) << 8)
#define SEQ_TO_INDEX(x) (x & 0xff)
#define INDEX_TO_SEQ(x) (x & 0xff)
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

struct iwl_cmd;
struct iwl_priv;

struct iwl_cmd_meta {
	struct iwl_cmd_meta *source;
	union {
		struct sk_buff *skb;
		int (*callback)(struct iwl_priv *priv,
				struct iwl_cmd *cmd, struct sk_buff *skb);
	} __attribute__ ((packed)) u;

	/* The CMD_SIZE_HUGE flag bit indicates that the command
	 * structure is stored at the end of the shared queue memory. */
	u32 flags;

} __attribute__ ((packed));

struct iwl_cmd {
	struct iwl_cmd_meta meta;
	struct iwl_cmd_header hdr;
	union {
		struct iwl_addsta_cmd addsta;
		struct iwl_led_cmd led;
		u32 flags;
		u8 val8;
		u16 val16;
		u32 val32;
		struct iwl_bt_cmd bt;
		struct iwl_rxon_time_cmd rxon_time;
		struct iwl_powertable_cmd powertable;
		struct iwl_qosparam_cmd qosparam;
		struct iwl_tx_cmd tx;
		struct iwl_tx_beacon_cmd tx_beacon;
		struct iwl_rxon_assoc_cmd rxon_assoc;
		u8 *indirect;
		u8 payload[360];
	} __attribute__ ((packed)) cmd;
} __attribute__ ((packed));

struct iwl_host_cmd {
	u8 id;
	u16 len;
	struct iwl_cmd_meta meta;
	const void *data;
};

#define TFD_MAX_PAYLOAD_SIZE (sizeof(struct iwl_cmd) - \
			      sizeof(struct iwl_cmd_meta))

/*
 * RX related structures and functions
 */
#define RX_FREE_BUFFERS 64
#define RX_LOW_WATERMARK 8

#define SUP_RATE_11A_MAX_NUM_CHANNELS  8
#define SUP_RATE_11B_MAX_NUM_CHANNELS  4
#define SUP_RATE_11G_MAX_NUM_CHANNELS  12

/**
 * struct iwl_rx_queue - Rx queue
 * @processed: Internal index to last handled Rx packet
 * @read: Shared index to newest available Rx buffer
 * @write: Shared index to oldest written Rx packet
 * @free_count: Number of pre-allocated buffers in rx_free
 * @rx_free: list of free SKBs for use
 * @rx_used: List of Rx buffers with no SKB
 * @need_update: flag to indicate we need to update read/write index
 *
 * NOTE:  rx_free and rx_used are used as a FIFO for iwl_rx_mem_buffers
 */
struct iwl_rx_queue {
	__le32 *bd;
	dma_addr_t dma_addr;
	struct iwl_rx_mem_buffer pool[RX_QUEUE_SIZE + RX_FREE_BUFFERS];
	struct iwl_rx_mem_buffer *queue[RX_QUEUE_SIZE];
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
#define STATUS_INT_ENABLED	1
#define STATUS_RF_KILL_HW	2
#define STATUS_RF_KILL_SW	3
#define STATUS_INIT		4
#define STATUS_ALIVE		5
#define STATUS_READY		6
#define STATUS_TEMPERATURE	7
#define STATUS_GEO_CONFIGURED	8
#define STATUS_EXIT_PENDING	9
#define STATUS_IN_SUSPEND	10
#define STATUS_STATISTICS	11
#define STATUS_SCANNING		12
#define STATUS_SCAN_ABORTING	13
#define STATUS_SCAN_HW		14
#define STATUS_POWER_PMI	15
#define STATUS_FW_ERROR		16

#define MAX_TID_COUNT        9

#define IWL_INVALID_RATE     0xFF
#define IWL_INVALID_VALUE    -1

#if IWL == 4965
#ifdef CONFIG_IWLWIFI_HT
#ifdef CONFIG_IWLWIFI_HT_AGG
struct iwl_ht_agg {
	u16 txq_id;
	u16 frame_count;
	u16 wait_for_ba;
	u16 start_idx;
	u32 bitmap0;
	u32 bitmap1;
	u32 rate_n_flags;
};
#endif /* CONFIG_IWLWIFI_HT_AGG */
#endif /* CONFIG_IWLWIFI_HT */
#endif

struct iwl_tid_data {
	u16 seq_number;
#if IWL == 4965
#ifdef CONFIG_IWLWIFI_HT
#ifdef CONFIG_IWLWIFI_HT_AGG
	struct iwl_ht_agg agg;
#endif	/* CONFIG_IWLWIFI_HT_AGG */
#endif /* CONFIG_IWLWIFI_HT */
#endif
};

struct iwl_hw_key {
	enum ieee80211_key_alg alg;
	int keylen;
	u8 key[32];
};

union iwl_ht_rate_supp {
	u16 rates;
	struct {
		u8 siso_rate;
		u8 mimo_rate;
	};
};

#ifdef CONFIG_IWLWIFI_HT
#define CFG_HT_RX_AMPDU_FACTOR_DEF  (0x3)
#define HT_IE_MAX_AMSDU_SIZE_4K     (0)
#define CFG_HT_MPDU_DENSITY_2USEC   (0x5)
#define CFG_HT_MPDU_DENSITY_DEF CFG_HT_MPDU_DENSITY_2USEC

struct sta_ht_info {
	u8 is_ht;
	u16 rx_mimo_ps_mode;
	u16 tx_mimo_ps_mode;
	u16 control_channel;
	u8 max_amsdu_size;
	u8 ampdu_factor;
	u8 mpdu_density;
	u8 operating_mode;
	u8 supported_chan_width;
	u8 extension_chan_offset;
	u8 is_green_field;
	u8 sgf;
	u8 supp_rates[16];
	u8 tx_chan_width;
	u8 chan_width_cap;
};
#endif				/*CONFIG_IWLWIFI_HT */

#ifdef CONFIG_IWLWIFI_QOS

union iwl_qos_capabity {
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

/* QoS sturctures */
struct iwl_qos_info {
	int qos_enable;
	int qos_active;
	union iwl_qos_capabity qos_cap;
	struct iwl_qosparam_cmd def_qos_parm;
};
#endif /*CONFIG_IWLWIFI_QOS */

#define STA_PS_STATUS_WAKE             0
#define STA_PS_STATUS_SLEEP            1

struct iwl_station_entry {
	struct iwl_addsta_cmd sta;
	struct iwl_tid_data tid[MAX_TID_COUNT];
#if IWL == 3945
	union {
		struct {
			u8 rate;
			u8 flags;
		} s;
		u16 rate_n_flags;
	} current_rate;
#endif
	u8 used;
	u8 ps_status;
	struct iwl_hw_key keyinfo;
};

/* one for each uCode image (inst/data, boot/init/runtime) */
struct fw_image_desc {
	void *v_addr;		/* access by driver */
	dma_addr_t p_addr;	/* access by card's busmaster DMA */
	u32 len;		/* bytes */
};

/* uCode file layout */
struct iwl_ucode {
	__le32 ver;		/* major/minor/subminor */
	__le32 inst_size;	/* bytes of runtime instructions */
	__le32 data_size;	/* bytes of runtime data */
	__le32 init_size;	/* bytes of initialization instructions */
	__le32 init_data_size;	/* bytes of initialization data */
	__le32 boot_size;	/* bytes of bootstrap instructions */
	u8 data[0];		/* data in same order as "size" elements */
};

#define IWL_IBSS_MAC_HASH_SIZE 32

struct iwl_ibss_seq {
	u8 mac[ETH_ALEN];
	u16 seq_num;
	u16 frag_num;
	unsigned long packet_time;
	struct list_head list;
};

struct iwl_driver_hw_info {
	u16 max_txq_num;
	u16 ac_queue_count;
	u32 rx_buffer_size;
	u16 tx_cmd_len;
	u16 max_rxq_size;
	u16 max_rxq_log;
	u32 cck_flag;
	u8  max_stations;
	u8  bcast_sta_id;
	void *shared_virt;
	dma_addr_t shared_phys;
};


#define STA_FLG_RTS_MIMO_PROT_MSK	__constant_cpu_to_le32(1 << 17)
#define STA_FLG_AGG_MPDU_8US_MSK	__constant_cpu_to_le32(1 << 18)
#define STA_FLG_MAX_AGG_SIZE_POS	(19)
#define STA_FLG_MAX_AGG_SIZE_MSK	__constant_cpu_to_le32(3 << 19)
#define STA_FLG_FAT_EN_MSK		__constant_cpu_to_le32(1 << 21)
#define STA_FLG_MIMO_DIS_MSK		__constant_cpu_to_le32(1 << 22)
#define STA_FLG_AGG_MPDU_DENSITY_POS	(23)
#define STA_FLG_AGG_MPDU_DENSITY_MSK	__constant_cpu_to_le32(7 << 23)
#define HT_SHORT_GI_20MHZ_ONLY          (1 << 0)
#define HT_SHORT_GI_40MHZ_ONLY          (1 << 1)


#include "iwl-priv.h"

/* Requires full declaration of iwl_priv before including */
#include "iwl-io.h"

#define IWL_RX_HDR(x) ((struct iwl_rx_frame_hdr *)(\
		       x->u.rx_frame.stats.payload + \
		       x->u.rx_frame.stats.phy_count))
#define IWL_RX_END(x) ((struct iwl_rx_frame_end *)(\
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
struct iwl_addsta_cmd;
extern int iwl_send_add_station(struct iwl_priv *priv,
				struct iwl_addsta_cmd *sta, u8 flags);
extern const char *iwl_get_tx_fail_reason(u32 status);
extern u8 iwl_add_station(struct iwl_priv *priv, const u8 *bssid,
			  int is_ap, u8 flags);
extern int iwl_is_network_packet(struct iwl_priv *priv,
				 struct ieee80211_hdr *header);
extern int iwl_power_init_handle(struct iwl_priv *priv);
extern int iwl_eeprom_init(struct iwl_priv *priv);
#ifdef CONFIG_IWLWIFI_DEBUG
extern void iwl_report_frame(struct iwl_priv *priv,
			     struct iwl_rx_packet *pkt,
			     struct ieee80211_hdr *header, int group100);
#else
static inline void iwl_report_frame(struct iwl_priv *priv,
				    struct iwl_rx_packet *pkt,
				    struct ieee80211_hdr *header,
				    int group100) {}
#endif
extern int iwl_tx_queue_update_write_ptr(struct iwl_priv *priv,
					 struct iwl_tx_queue *txq);
extern void iwl_handle_data_packet_monitor(struct iwl_priv *priv,
					   struct iwl_rx_mem_buffer *rxb,
					   void *data, short len,
					   struct ieee80211_rx_status *stats,
					   u16 phy_flags);
extern int is_duplicate_packet(struct iwl_priv *priv, struct ieee80211_hdr
			       *header);
extern void iwl_rx_queue_free(struct iwl_priv *priv, struct iwl_rx_queue *rxq);
extern int iwl_rx_queue_alloc(struct iwl_priv *priv);
extern void iwl_rx_queue_reset(struct iwl_priv *priv,
			       struct iwl_rx_queue *rxq);
extern int iwl_calc_db_from_ratio(int sig_ratio);
extern int iwl_calc_sig_qual(int rssi_dbm, int noise_dbm);
extern int iwl_tx_queue_init(struct iwl_priv *priv,
			     struct iwl_tx_queue *txq, int count, u32 id);
extern int iwl_rx_queue_restock(struct iwl_priv *priv);
extern void iwl_rx_replenish(void *data);
extern void iwl_tx_queue_free(struct iwl_priv *priv, struct iwl_tx_queue *txq);
extern int iwl_send_cmd_pdu(struct iwl_priv *priv, u8 id, u16 len,
			    const void *data);
extern int __must_check iwl_send_cmd_async(struct iwl_priv *priv,
		struct iwl_host_cmd *cmd);
extern int __must_check iwl_send_cmd_sync(struct iwl_priv *priv,
		struct iwl_host_cmd *cmd);
extern int __must_check iwl_send_cmd(struct iwl_priv *priv,
		struct iwl_host_cmd *cmd);
extern unsigned int iwl_fill_beacon_frame(struct iwl_priv *priv,
					struct ieee80211_hdr *hdr,
					const u8 *dest, int left);
extern int iwl_rx_queue_update_write_ptr(struct iwl_priv *priv,
					 struct iwl_rx_queue *q);
extern int iwl_send_statistics_request(struct iwl_priv *priv);
extern void iwl_set_decrypted_flag(struct iwl_priv *priv, struct sk_buff *skb,
				   u32 decrypt_res,
				   struct ieee80211_rx_status *stats);
extern __le16 *ieee80211_get_qos_ctrl(struct ieee80211_hdr *hdr);

extern const u8 BROADCAST_ADDR[ETH_ALEN];

/*
 * Currently used by iwl-3945-rs... look at restructuring so that it doesn't
 * call this... todo... fix that.
*/
extern u8 iwl_sync_station(struct iwl_priv *priv, int sta_id,
			   u16 tx_rate, u8 flags);

static inline int iwl_is_associated(struct iwl_priv *priv)
{
	return (priv->active_rxon.filter_flags & RXON_FILTER_ASSOC_MSK) ? 1 : 0;
}

/******************************************************************************
 *
 * Functions implemented in iwl-[34]*.c which are forward declared here
 * for use by iwl-base.c
 *
 * NOTE:  The implementation of these functions are hardware specific
 * which is why they are in the hardware specific files (vs. iwl-base.c)
 *
 * Naming convention --
 * iwl_         <-- Its part of iwlwifi (should be changed to iwl_)
 * iwl_hw_      <-- Hardware specific (implemented in iwl-XXXX.c by all HW)
 * iwlXXXX_     <-- Hardware specific (implemented in iwl-XXXX.c for XXXX)
 * iwl_bg_      <-- Called from work queue context
 * iwl_mac_     <-- mac80211 callback
 *
 ****************************************************************************/
extern void iwl_hw_rx_handler_setup(struct iwl_priv *priv);
extern void iwl_hw_setup_deferred_work(struct iwl_priv *priv);
extern void iwl_hw_cancel_deferred_work(struct iwl_priv *priv);
extern int iwl_hw_rxq_stop(struct iwl_priv *priv);
extern int iwl_hw_set_hw_setting(struct iwl_priv *priv);
extern int iwl_hw_nic_init(struct iwl_priv *priv);
extern void iwl_hw_card_show_info(struct iwl_priv *priv);
extern int iwl_hw_nic_stop_master(struct iwl_priv *priv);
extern void iwl_hw_txq_ctx_free(struct iwl_priv *priv);
extern void iwl_hw_txq_ctx_stop(struct iwl_priv *priv);
extern int iwl_hw_nic_reset(struct iwl_priv *priv);
extern int iwl_hw_txq_attach_buf_to_tfd(struct iwl_priv *priv, void *tfd,
					dma_addr_t addr, u16 len);
extern int iwl_hw_txq_free_tfd(struct iwl_priv *priv, struct iwl_tx_queue *txq);
extern int iwl_hw_get_temperature(struct iwl_priv *priv);
extern int iwl_hw_tx_queue_init(struct iwl_priv *priv,
				struct iwl_tx_queue *txq);
extern unsigned int iwl_hw_get_beacon_cmd(struct iwl_priv *priv,
				 struct iwl_frame *frame, u8 rate);
extern int iwl_hw_get_rx_read(struct iwl_priv *priv);
extern void iwl_hw_build_tx_cmd_rate(struct iwl_priv *priv,
				     struct iwl_cmd *cmd,
				     struct ieee80211_tx_control *ctrl,
				     struct ieee80211_hdr *hdr,
				     int sta_id, int tx_id);
extern int iwl_hw_reg_send_txpower(struct iwl_priv *priv);
extern int iwl_hw_reg_set_txpower(struct iwl_priv *priv, s8 power);
extern void iwl_hw_rx_statistics(struct iwl_priv *priv,
				 struct iwl_rx_mem_buffer *rxb);
extern void iwl_disable_events(struct iwl_priv *priv);
extern int iwl4965_get_temperature(const struct iwl_priv *priv);

/**
 * iwl_hw_find_station - Find station id for a given BSSID
 * @bssid: MAC address of station ID to find
 *
 * NOTE:  This should not be hardware specific but the code has
 * not yet been merged into a single common layer for managing the
 * station tables.
 */
extern u8 iwl_hw_find_station(struct iwl_priv *priv, const u8 *bssid);

extern int iwl_hw_channel_switch(struct iwl_priv *priv, u16 channel);
extern int iwl_tx_queue_reclaim(struct iwl_priv *priv, int txq_id, int index);
#endif
