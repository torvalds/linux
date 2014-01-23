/*
 * This file is part of wlcore
 *
 * Copyright (C) 2011 Texas Instruments Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __WLCORE_H__
#define __WLCORE_H__

#include <linux/platform_device.h>

#include "wlcore_i.h"
#include "event.h"
#include "boot.h"

/* The maximum number of Tx descriptors in all chip families */
#define WLCORE_MAX_TX_DESCRIPTORS 32

/*
 * We always allocate this number of mac addresses. If we don't
 * have enough allocated addresses, the LAA bit is used
 */
#define WLCORE_NUM_MAC_ADDRESSES 3

/* wl12xx/wl18xx maximum transmission power (in dBm) */
#define WLCORE_MAX_TXPWR        25

/* forward declaration */
struct wl1271_tx_hw_descr;
enum wl_rx_buf_align;
struct wl1271_rx_descriptor;

struct wlcore_ops {
	int (*setup)(struct wl1271 *wl);
	int (*identify_chip)(struct wl1271 *wl);
	int (*identify_fw)(struct wl1271 *wl);
	int (*boot)(struct wl1271 *wl);
	int (*plt_init)(struct wl1271 *wl);
	int (*trigger_cmd)(struct wl1271 *wl, int cmd_box_addr,
			   void *buf, size_t len);
	int (*ack_event)(struct wl1271 *wl);
	int (*wait_for_event)(struct wl1271 *wl, enum wlcore_wait_event event,
			      bool *timeout);
	int (*process_mailbox_events)(struct wl1271 *wl);
	u32 (*calc_tx_blocks)(struct wl1271 *wl, u32 len, u32 spare_blks);
	void (*set_tx_desc_blocks)(struct wl1271 *wl,
				   struct wl1271_tx_hw_descr *desc,
				   u32 blks, u32 spare_blks);
	void (*set_tx_desc_data_len)(struct wl1271 *wl,
				     struct wl1271_tx_hw_descr *desc,
				     struct sk_buff *skb);
	enum wl_rx_buf_align (*get_rx_buf_align)(struct wl1271 *wl,
						 u32 rx_desc);
	int (*prepare_read)(struct wl1271 *wl, u32 rx_desc, u32 len);
	u32 (*get_rx_packet_len)(struct wl1271 *wl, void *rx_data,
				 u32 data_len);
	int (*tx_delayed_compl)(struct wl1271 *wl);
	void (*tx_immediate_compl)(struct wl1271 *wl);
	int (*hw_init)(struct wl1271 *wl);
	int (*init_vif)(struct wl1271 *wl, struct wl12xx_vif *wlvif);
	u32 (*sta_get_ap_rate_mask)(struct wl1271 *wl,
				    struct wl12xx_vif *wlvif);
	int (*get_pg_ver)(struct wl1271 *wl, s8 *ver);
	int (*get_mac)(struct wl1271 *wl);
	void (*set_tx_desc_csum)(struct wl1271 *wl,
				 struct wl1271_tx_hw_descr *desc,
				 struct sk_buff *skb);
	void (*set_rx_csum)(struct wl1271 *wl,
			    struct wl1271_rx_descriptor *desc,
			    struct sk_buff *skb);
	u32 (*ap_get_mimo_wide_rate_mask)(struct wl1271 *wl,
					  struct wl12xx_vif *wlvif);
	int (*debugfs_init)(struct wl1271 *wl, struct dentry *rootdir);
	int (*handle_static_data)(struct wl1271 *wl,
				  struct wl1271_static_data *static_data);
	int (*scan_start)(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			  struct cfg80211_scan_request *req);
	int (*scan_stop)(struct wl1271 *wl, struct wl12xx_vif *wlvif);
	int (*sched_scan_start)(struct wl1271 *wl, struct wl12xx_vif *wlvif,
				struct cfg80211_sched_scan_request *req,
				struct ieee80211_sched_scan_ies *ies);
	void (*sched_scan_stop)(struct wl1271 *wl, struct wl12xx_vif *wlvif);
	int (*get_spare_blocks)(struct wl1271 *wl, bool is_gem);
	int (*set_key)(struct wl1271 *wl, enum set_key_cmd cmd,
		       struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta,
		       struct ieee80211_key_conf *key_conf);
	int (*channel_switch)(struct wl1271 *wl,
			      struct wl12xx_vif *wlvif,
			      struct ieee80211_channel_switch *ch_switch);
	u32 (*pre_pkt_send)(struct wl1271 *wl, u32 buf_offset, u32 last_len);
	void (*sta_rc_update)(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			      struct ieee80211_sta *sta, u32 changed);
	int (*set_peer_cap)(struct wl1271 *wl,
			    struct ieee80211_sta_ht_cap *ht_cap,
			    bool allow_ht_operation,
			    u32 rate_set, u8 hlid);
	u32 (*convert_hwaddr)(struct wl1271 *wl, u32 hwaddr);
	bool (*lnk_high_prio)(struct wl1271 *wl, u8 hlid,
			      struct wl1271_link *lnk);
	bool (*lnk_low_prio)(struct wl1271 *wl, u8 hlid,
			     struct wl1271_link *lnk);
};

enum wlcore_partitions {
	PART_DOWN,
	PART_WORK,
	PART_BOOT,
	PART_DRPW,
	PART_TOP_PRCM_ELP_SOC,
	PART_PHY_INIT,

	PART_TABLE_LEN,
};

struct wlcore_partition {
	u32 size;
	u32 start;
};

struct wlcore_partition_set {
	struct wlcore_partition mem;
	struct wlcore_partition reg;
	struct wlcore_partition mem2;
	struct wlcore_partition mem3;
};

enum wlcore_registers {
	/* register addresses, used with partition translation */
	REG_ECPU_CONTROL,
	REG_INTERRUPT_NO_CLEAR,
	REG_INTERRUPT_ACK,
	REG_COMMAND_MAILBOX_PTR,
	REG_EVENT_MAILBOX_PTR,
	REG_INTERRUPT_TRIG,
	REG_INTERRUPT_MASK,
	REG_PC_ON_RECOVERY,
	REG_CHIP_ID_B,
	REG_CMD_MBOX_ADDRESS,

	/* data access memory addresses, used with partition translation */
	REG_SLV_MEM_DATA,
	REG_SLV_REG_DATA,

	/* raw data access memory addresses */
	REG_RAW_FW_STATUS_ADDR,

	REG_TABLE_LEN,
};

struct wl1271_stats {
	void *fw_stats;
	unsigned long fw_stats_update;
	size_t fw_stats_len;

	unsigned int retry_count;
	unsigned int excessive_retries;
};

struct wl1271 {
	bool initialized;
	struct ieee80211_hw *hw;
	bool mac80211_registered;

	struct device *dev;
	struct platform_device *pdev;

	void *if_priv;

	struct wl1271_if_operations *if_ops;

	int irq;

	spinlock_t wl_lock;

	enum wlcore_state state;
	enum wl12xx_fw_type fw_type;
	bool plt;
	enum plt_mode plt_mode;
	u8 fem_manuf;
	u8 last_vif_count;
	struct mutex mutex;

	unsigned long flags;

	struct wlcore_partition_set curr_part;

	struct wl1271_chip chip;

	int cmd_box_addr;

	u8 *fw;
	size_t fw_len;
	void *nvs;
	size_t nvs_len;

	s8 hw_pg_ver;

	/* address read from the fuse ROM */
	u32 fuse_oui_addr;
	u32 fuse_nic_addr;

	/* we have up to 2 MAC addresses */
	struct mac_address addresses[WLCORE_NUM_MAC_ADDRESSES];
	int channel;
	u8 system_hlid;

	unsigned long links_map[BITS_TO_LONGS(WL12XX_MAX_LINKS)];
	unsigned long roles_map[BITS_TO_LONGS(WL12XX_MAX_ROLES)];
	unsigned long roc_map[BITS_TO_LONGS(WL12XX_MAX_ROLES)];
	unsigned long rate_policies_map[
			BITS_TO_LONGS(WL12XX_MAX_RATE_POLICIES)];
	unsigned long klv_templates_map[
			BITS_TO_LONGS(WLCORE_MAX_KLV_TEMPLATES)];

	u8 session_ids[WL12XX_MAX_LINKS];

	struct list_head wlvif_list;

	u8 sta_count;
	u8 ap_count;

	struct wl1271_acx_mem_map *target_mem_map;

	/* Accounting for allocated / available TX blocks on HW */
	u32 tx_blocks_freed;
	u32 tx_blocks_available;
	u32 tx_allocated_blocks;
	u32 tx_results_count;

	/* Accounting for allocated / available Tx packets in HW */
	u32 tx_pkts_freed[NUM_TX_QUEUES];
	u32 tx_allocated_pkts[NUM_TX_QUEUES];

	/* Transmitted TX packets counter for chipset interface */
	u32 tx_packets_count;

	/* Time-offset between host and chipset clocks */
	s64 time_offset;

	/* Frames scheduled for transmission, not handled yet */
	int tx_queue_count[NUM_TX_QUEUES];
	unsigned long queue_stop_reasons[
				NUM_TX_QUEUES * WLCORE_NUM_MAC_ADDRESSES];

	/* Frames received, not handled yet by mac80211 */
	struct sk_buff_head deferred_rx_queue;

	/* Frames sent, not returned yet to mac80211 */
	struct sk_buff_head deferred_tx_queue;

	struct work_struct tx_work;
	struct workqueue_struct *freezable_wq;

	/* Pending TX frames */
	unsigned long tx_frames_map[BITS_TO_LONGS(WLCORE_MAX_TX_DESCRIPTORS)];
	struct sk_buff *tx_frames[WLCORE_MAX_TX_DESCRIPTORS];
	int tx_frames_cnt;

	/* FW Rx counter */
	u32 rx_counter;

	/* Intermediate buffer, used for packet aggregation */
	u8 *aggr_buf;
	u32 aggr_buf_size;

	/* Reusable dummy packet template */
	struct sk_buff *dummy_packet;

	/* Network stack work  */
	struct work_struct netstack_work;

	/* FW log buffer */
	u8 *fwlog;

	/* Number of valid bytes in the FW log buffer */
	ssize_t fwlog_size;

	/* FW log end marker */
	u32 fwlog_end;

	/* FW memory block size */
	u32 fw_mem_block_size;

	/* Sysfs FW log entry readers wait queue */
	wait_queue_head_t fwlog_waitq;

	/* Hardware recovery work */
	struct work_struct recovery_work;
	bool watchdog_recovery;

	/* Reg domain last configuration */
	u32 reg_ch_conf_last[2];
	/* Reg domain pending configuration */
	u32 reg_ch_conf_pending[2];

	/* Pointer that holds DMA-friendly block for the mailbox */
	void *mbox;

	/* The mbox event mask */
	u32 event_mask;
	/* events to unmask only when ap interface is up */
	u32 ap_event_mask;

	/* Mailbox pointers */
	u32 mbox_size;
	u32 mbox_ptr[2];

	/* Are we currently scanning */
	struct wl12xx_vif *scan_wlvif;
	struct wl1271_scan scan;
	struct delayed_work scan_complete_work;

	struct ieee80211_vif *roc_vif;
	struct delayed_work roc_complete_work;

	struct wl12xx_vif *sched_vif;

	/* The current band */
	enum ieee80211_band band;

	struct completion *elp_compl;
	struct delayed_work elp_work;

	/* in dBm */
	int power_level;

	struct wl1271_stats stats;

	__le32 *buffer_32;
	u32 buffer_cmd;
	u32 buffer_busyword[WL1271_BUSY_WORD_CNT];

	struct wl_fw_status_1 *fw_status_1;
	struct wl_fw_status_2 *fw_status_2;
	struct wl1271_tx_hw_res_if *tx_res_if;

	/* Current chipset configuration */
	struct wlcore_conf conf;

	bool sg_enabled;

	bool enable_11a;

	int recovery_count;

	/* Most recently reported noise in dBm */
	s8 noise;

	/* bands supported by this instance of wl12xx */
	struct ieee80211_supported_band bands[WLCORE_NUM_BANDS];

	/*
	 * wowlan trigger was configured during suspend.
	 * (currently, only "ANY" trigger is supported)
	 */
	bool wow_enabled;
	bool irq_wake_enabled;

	/*
	 * AP-mode - links indexed by HLID. The global and broadcast links
	 * are always active.
	 */
	struct wl1271_link links[WL12XX_MAX_LINKS];

	/* number of currently active links */
	int active_link_count;

	/* Fast/slow links bitmap according to FW */
	u32 fw_fast_lnk_map;

	/* AP-mode - a bitmap of links currently in PS mode according to FW */
	u32 ap_fw_ps_map;

	/* AP-mode - a bitmap of links currently in PS mode in mac80211 */
	unsigned long ap_ps_map;

	/* Quirks of specific hardware revisions */
	unsigned int quirks;

	/* Platform limitations */
	unsigned int platform_quirks;

	/* number of currently active RX BA sessions */
	int ba_rx_session_count;

	/* Maximum number of supported RX BA sessions */
	int ba_rx_session_count_max;

	/* AP-mode - number of currently connected stations */
	int active_sta_count;

	/* last wlvif we transmitted from */
	struct wl12xx_vif *last_wlvif;

	/* work to fire when Tx is stuck */
	struct delayed_work tx_watchdog_work;

	struct wlcore_ops *ops;
	/* pointer to the lower driver partition table */
	const struct wlcore_partition_set *ptable;
	/* pointer to the lower driver register table */
	const int *rtable;
	/* name of the firmwares to load - for PLT, single role, multi-role */
	const char *plt_fw_name;
	const char *sr_fw_name;
	const char *mr_fw_name;

	u8 scan_templ_id_2_4;
	u8 scan_templ_id_5;
	u8 sched_scan_templ_id_2_4;
	u8 sched_scan_templ_id_5;
	u8 max_channels_5;

	/* per-chip-family private structure */
	void *priv;

	/* number of TX descriptors the HW supports. */
	u32 num_tx_desc;
	/* number of RX descriptors the HW supports. */
	u32 num_rx_desc;

	/* translate HW Tx rates to standard rate-indices */
	const u8 **band_rate_to_idx;

	/* size of table for HW rates that can be received from chip */
	u8 hw_tx_rate_tbl_size;

	/* this HW rate and below are considered HT rates for this chip */
	u8 hw_min_ht_rate;

	/* HW HT (11n) capabilities */
	struct ieee80211_sta_ht_cap ht_cap[WLCORE_NUM_BANDS];

	/* size of the private FW status data */
	size_t fw_status_priv_len;

	/* RX Data filter rule state - enabled/disabled */
	bool rx_filter_enabled[WL1271_MAX_RX_FILTERS];

	/* size of the private static data */
	size_t static_data_priv_len;

	/* the current channel type */
	enum nl80211_channel_type channel_type;

	/* mutex for protecting the tx_flush function */
	struct mutex flush_mutex;

	/* sleep auth value currently configured to FW */
	int sleep_auth;

	/* the number of allocated MAC addresses in this chip */
	int num_mac_addr;

	/* minimum FW version required for the driver to work in single-role */
	unsigned int min_sr_fw_ver[NUM_FW_VER];

	/* minimum FW version required for the driver to work in multi-role */
	unsigned int min_mr_fw_ver[NUM_FW_VER];

	struct completion nvs_loading_complete;

	/* number of concurrent channels the HW supports */
	u32 num_channels;
};

int wlcore_probe(struct wl1271 *wl, struct platform_device *pdev);
int wlcore_remove(struct platform_device *pdev);
struct ieee80211_hw *wlcore_alloc_hw(size_t priv_size, u32 aggr_buf_size,
				     u32 mbox_size);
int wlcore_free_hw(struct wl1271 *wl);
int wlcore_set_key(struct wl1271 *wl, enum set_key_cmd cmd,
		   struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta,
		   struct ieee80211_key_conf *key_conf);
void wlcore_regdomain_config(struct wl1271 *wl);
void wlcore_update_inconn_sta(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			      struct wl1271_station *wl_sta, bool in_conn);

static inline void
wlcore_set_ht_cap(struct wl1271 *wl, enum ieee80211_band band,
		  struct ieee80211_sta_ht_cap *ht_cap)
{
	memcpy(&wl->ht_cap[band], ht_cap, sizeof(*ht_cap));
}

/* Tell wlcore not to care about this element when checking the version */
#define WLCORE_FW_VER_IGNORE	-1

static inline void
wlcore_set_min_fw_ver(struct wl1271 *wl, unsigned int chip,
		      unsigned int iftype_sr, unsigned int major_sr,
		      unsigned int subtype_sr, unsigned int minor_sr,
		      unsigned int iftype_mr, unsigned int major_mr,
		      unsigned int subtype_mr, unsigned int minor_mr)
{
	wl->min_sr_fw_ver[FW_VER_CHIP] = chip;
	wl->min_sr_fw_ver[FW_VER_IF_TYPE] = iftype_sr;
	wl->min_sr_fw_ver[FW_VER_MAJOR] = major_sr;
	wl->min_sr_fw_ver[FW_VER_SUBTYPE] = subtype_sr;
	wl->min_sr_fw_ver[FW_VER_MINOR] = minor_sr;

	wl->min_mr_fw_ver[FW_VER_CHIP] = chip;
	wl->min_mr_fw_ver[FW_VER_IF_TYPE] = iftype_mr;
	wl->min_mr_fw_ver[FW_VER_MAJOR] = major_mr;
	wl->min_mr_fw_ver[FW_VER_SUBTYPE] = subtype_mr;
	wl->min_mr_fw_ver[FW_VER_MINOR] = minor_mr;
}

/* Firmware image load chunk size */
#define CHUNK_SIZE	16384

/* Quirks */

/* Each RX/TX transaction requires an end-of-transaction transfer */
#define WLCORE_QUIRK_END_OF_TRANSACTION		BIT(0)

/* the first start_role(sta) sometimes doesn't work on wl12xx */
#define WLCORE_QUIRK_START_STA_FAILS		BIT(1)

/* wl127x and SPI don't support SDIO block size alignment */
#define WLCORE_QUIRK_TX_BLOCKSIZE_ALIGN		BIT(2)

/* means aggregated Rx packets are aligned to a SDIO block */
#define WLCORE_QUIRK_RX_BLOCKSIZE_ALIGN		BIT(3)

/* Older firmwares did not implement the FW logger over bus feature */
#define WLCORE_QUIRK_FWLOG_NOT_IMPLEMENTED	BIT(4)

/* Older firmwares use an old NVS format */
#define WLCORE_QUIRK_LEGACY_NVS			BIT(5)

/* pad only the last frame in the aggregate buffer */
#define WLCORE_QUIRK_TX_PAD_LAST_FRAME		BIT(7)

/* extra header space is required for TKIP */
#define WLCORE_QUIRK_TKIP_HEADER_SPACE		BIT(8)

/* Some firmwares not support sched scans while connected */
#define WLCORE_QUIRK_NO_SCHED_SCAN_WHILE_CONN	BIT(9)

/* separate probe response templates for one-shot and sched scans */
#define WLCORE_QUIRK_DUAL_PROBE_TMPL		BIT(10)

/* Firmware requires reg domain configuration for active calibration */
#define WLCORE_QUIRK_REGDOMAIN_CONF		BIT(11)

/* The FW only support a zero session id for AP */
#define WLCORE_QUIRK_AP_ZERO_SESSION_ID		BIT(12)

/* TODO: move all these common registers and values elsewhere */
#define HW_ACCESS_ELP_CTRL_REG		0x1FFFC

/* ELP register commands */
#define ELPCTRL_WAKE_UP             0x1
#define ELPCTRL_WAKE_UP_WLAN_READY  0x5
#define ELPCTRL_SLEEP               0x0
/* ELP WLAN_READY bit */
#define ELPCTRL_WLAN_READY          0x2

/*************************************************************************

    Interrupt Trigger Register (Host -> WiLink)

**************************************************************************/

/* Hardware to Embedded CPU Interrupts - first 32-bit register set */

/*
 * The host sets this bit to inform the Wlan
 * FW that a TX packet is in the XFER
 * Buffer #0.
 */
#define INTR_TRIG_TX_PROC0 BIT(2)

/*
 * The host sets this bit to inform the FW
 * that it read a packet from RX XFER
 * Buffer #0.
 */
#define INTR_TRIG_RX_PROC0 BIT(3)

#define INTR_TRIG_DEBUG_ACK BIT(4)

#define INTR_TRIG_STATE_CHANGED BIT(5)

/* Hardware to Embedded CPU Interrupts - second 32-bit register set */

/*
 * The host sets this bit to inform the FW
 * that it read a packet from RX XFER
 * Buffer #1.
 */
#define INTR_TRIG_RX_PROC1 BIT(17)

/*
 * The host sets this bit to inform the Wlan
 * hardware that a TX packet is in the XFER
 * Buffer #1.
 */
#define INTR_TRIG_TX_PROC1 BIT(18)

#define ACX_SLV_SOFT_RESET_BIT	BIT(1)
#define SOFT_RESET_MAX_TIME	1000000
#define SOFT_RESET_STALL_TIME	1000

#define ECPU_CONTROL_HALT	0x00000101

#define WELP_ARM_COMMAND_VAL	0x4

#endif /* __WLCORE_H__ */
