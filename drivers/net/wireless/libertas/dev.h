/**
  * This file contains definitions and data structures specific
  * to Marvell 802.11 NIC. It contains the Device Information
  * structure struct lbs_private..
  */
#ifndef _LBS_DEV_H_
#define _LBS_DEV_H_

#include "scan.h"
#include "assoc.h"



/** sleep_params */
struct sleep_params {
	uint16_t sp_error;
	uint16_t sp_offset;
	uint16_t sp_stabletime;
	uint8_t  sp_calcontrol;
	uint8_t  sp_extsleepclk;
	uint16_t sp_reserved;
};

/* Mesh statistics */
struct lbs_mesh_stats {
	u32	fwd_bcast_cnt;		/* Fwd: Broadcast counter */
	u32	fwd_unicast_cnt;	/* Fwd: Unicast counter */
	u32	fwd_drop_ttl;		/* Fwd: TTL zero */
	u32	fwd_drop_rbt;		/* Fwd: Recently Broadcasted */
	u32	fwd_drop_noroute; 	/* Fwd: No route to Destination */
	u32	fwd_drop_nobuf;		/* Fwd: Run out of internal buffers */
	u32	drop_blind;		/* Rx:  Dropped by blinding table */
	u32	tx_failed_cnt;		/* Tx:  Failed transmissions */
};

/** Private structure for the MV device */
struct lbs_private {

	/* Basic networking */
	struct net_device *dev;
	u32 connect_status;
	int infra_open;
	struct work_struct mcast_work;
	u32 nr_of_multicastmacaddr;
	u8 multicastlist[MRVDRV_MAX_MULTICAST_LIST_SIZE][ETH_ALEN];

	/* CFG80211 */
	struct wireless_dev *wdev;

	/* Mesh */
	struct net_device *mesh_dev; /* Virtual device */
	u32 mesh_connect_status;
	struct lbs_mesh_stats mstats;
	int mesh_open;
	int mesh_fw_ver;
	int mesh_autostart_enabled;
	uint16_t mesh_tlv;
	u8 mesh_ssid[IEEE80211_MAX_SSID_LEN + 1];
	u8 mesh_ssid_len;
	struct work_struct sync_channel;

	/* Monitor mode */
	struct net_device *rtap_net_dev;
	u32 monitormode;

	/* Debugfs */
	struct dentry *debugfs_dir;
	struct dentry *debugfs_debug;
	struct dentry *debugfs_files[6];
	struct dentry *events_dir;
	struct dentry *debugfs_events_files[6];
	struct dentry *regs_dir;
	struct dentry *debugfs_regs_files[6];

	/* Hardware debugging */
	u32 mac_offset;
	u32 bbp_offset;
	u32 rf_offset;
	struct lbs_offset_value offsetvalue;

	/* Power management */
	u16 psmode;
	u32 psstate;
	u8 needtowakeup;

	/* Deep sleep */
	int is_deep_sleep;
	int is_auto_deep_sleep_enabled;
	int wakeup_dev_required;
	int is_activity_detected;
	int auto_deep_sleep_timeout; /* in ms */
	wait_queue_head_t ds_awake_q;
	struct timer_list auto_deepsleep_timer;

	/* Hardware access */
	void *card;
	u8 fw_ready;
	u8 surpriseremoved;
	int (*hw_host_to_card) (struct lbs_private *priv, u8 type, u8 *payload, u16 nb);
	void (*reset_card) (struct lbs_private *priv);
	int (*enter_deep_sleep) (struct lbs_private *priv);
	int (*exit_deep_sleep) (struct lbs_private *priv);
	int (*reset_deep_sleep_wakeup) (struct lbs_private *priv);

	/* Adapter info (from EEPROM) */
	u32 fwrelease;
	u32 fwcapinfo;
	u16 regioncode;
	u8 current_addr[ETH_ALEN];

	/* Command download */
	u8 dnld_sent;
	/* bit0 1/0=data_sent/data_tx_done,
	   bit1 1/0=cmd_sent/cmd_tx_done,
	   all other bits reserved 0 */
	u16 seqnum;
	struct cmd_ctrl_node *cmd_array;
	struct cmd_ctrl_node *cur_cmd;
	struct list_head cmdfreeq;    /* free command buffers */
	struct list_head cmdpendingq; /* pending command buffers */
	wait_queue_head_t cmd_pending;
	struct timer_list command_timer;
	int nr_retries;
	int cmd_timed_out;

	/* Command responses sent from the hardware to the driver */
	int cur_cmd_retcode;
	u8 resp_idx;
	u8 resp_buf[2][LBS_UPLD_SIZE];
	u32 resp_len[2];

	/* Events sent from hardware to driver */
	struct kfifo *event_fifo;

	/** thread to service interrupts */
	struct task_struct *main_thread;
	wait_queue_head_t waitq;
	struct workqueue_struct *work_thread;

	/** Encryption stuff */
	struct lbs_802_11_security secinfo;
	struct enc_key wpa_mcast_key;
	struct enc_key wpa_unicast_key;
	u8 wpa_ie[MAX_WPA_IE_LEN];
	u8 wpa_ie_len;
	u16 wep_tx_keyidx;
	struct enc_key wep_keys[4];

	/* Wake On LAN */
	uint32_t wol_criteria;
	uint8_t wol_gpio;
	uint8_t wol_gap;

	/* Transmitting */
	int tx_pending_len;		/* -1 while building packet */
	u8 tx_pending_buf[LBS_UPLD_SIZE];
	/* protected by hard_start_xmit serialization */
	u8 txretrycount;
	struct sk_buff *currenttxskb;

	/* Locks */
	struct mutex lock;
	spinlock_t driver_lock;

	/* NIC/link operation characteristics */
	u16 mac_control;
	u8 radio_on;
	u8 channel;
	s16 txpower_cur;
	s16 txpower_min;
	s16 txpower_max;

	/** Scanning */
	struct delayed_work scan_work;
	int scan_channel;
	/* remember which channel was scanned last, != 0 if currently scanning */
	u8 scan_ssid[IEEE80211_MAX_SSID_LEN + 1];
	u8 scan_ssid_len;

	/* Associating */
	struct delayed_work assoc_work;
	struct current_bss_params curbssparams;
	u8 mode;
	struct list_head network_list;
	struct list_head network_free_list;
	struct bss_descriptor *networks;
	struct assoc_request * pending_assoc_req;
	struct assoc_request * in_progress_assoc_req;
	u16 capability;
	uint16_t enablehwauto;
	uint16_t ratebitmap;

	/* ADHOC */
	u16 beacon_period;
	u8 beacon_enable;
	u8 adhoccreate;

	/* WEXT */
	char name[DEV_NAME_LEN];
	u8 nodename[16];
	struct iw_statistics wstats;
	u8 cur_rate;
#define	MAX_REGION_CHANNEL_NUM	2
	struct region_channel region_channel[MAX_REGION_CHANNEL_NUM];

	/** Requested Signal Strength*/
	u16 SNR[MAX_TYPE_B][MAX_TYPE_AVG];
	u16 NF[MAX_TYPE_B][MAX_TYPE_AVG];
	u8 RSSI[MAX_TYPE_B][MAX_TYPE_AVG];
	u8 rawSNR[DEFAULT_DATA_AVG_FACTOR];
	u8 rawNF[DEFAULT_DATA_AVG_FACTOR];
	u16 nextSNRNF;
	u16 numSNRNF;
};

extern struct cmd_confirm_sleep confirm_sleep;

#endif
