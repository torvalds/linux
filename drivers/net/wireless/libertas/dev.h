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
	struct wireless_dev *wdev;
	int mesh_open;
	int mesh_fw_ver;
	int infra_open;
	int mesh_autostart_enabled;

	char name[DEV_NAME_LEN];

	void *card;
	struct net_device *dev;

	struct net_device *mesh_dev; /* Virtual device */
	struct net_device *rtap_net_dev;

	struct iw_statistics wstats;
	struct lbs_mesh_stats mstats;
	struct dentry *debugfs_dir;
	struct dentry *debugfs_debug;
	struct dentry *debugfs_files[6];

	struct dentry *events_dir;
	struct dentry *debugfs_events_files[6];

	struct dentry *regs_dir;
	struct dentry *debugfs_regs_files[6];

	u32 mac_offset;
	u32 bbp_offset;
	u32 rf_offset;

	/** Deep sleep flag */
	int is_deep_sleep;
	/** Auto deep sleep enabled flag */
	int is_auto_deep_sleep_enabled;
	/** Device wakeup required flag */
	int wakeup_dev_required;
	/** Auto deep sleep flag*/
	int is_activity_detected;
	/** Auto deep sleep timeout (in miliseconds) */
	int auto_deep_sleep_timeout;

	/** Deep sleep wait queue */
	wait_queue_head_t       ds_awake_q;

	/* Download sent:
	   bit0 1/0=data_sent/data_tx_done,
	   bit1 1/0=cmd_sent/cmd_tx_done,
	   all other bits reserved 0 */
	u8 dnld_sent;

	/** thread to service interrupts */
	struct task_struct *main_thread;
	wait_queue_head_t waitq;
	struct workqueue_struct *work_thread;

	struct work_struct mcast_work;

	/** Scanning */
	struct delayed_work scan_work;
	struct delayed_work assoc_work;
	struct work_struct sync_channel;
	/* remember which channel was scanned last, != 0 if currently scanning */
	int scan_channel;
	u8 scan_ssid[IEEE80211_MAX_SSID_LEN + 1];
	u8 scan_ssid_len;

	/** Hardware access */
	int (*hw_host_to_card) (struct lbs_private *priv, u8 type, u8 *payload, u16 nb);
	void (*reset_card) (struct lbs_private *priv);
	int (*enter_deep_sleep) (struct lbs_private *priv);
	int (*exit_deep_sleep) (struct lbs_private *priv);
	int (*reset_deep_sleep_wakeup) (struct lbs_private *priv);

	/* Wake On LAN */
	uint32_t wol_criteria;
	uint8_t wol_gpio;
	uint8_t wol_gap;

	/** Wlan adapter data structure*/
	/** STATUS variables */
	u32 fwrelease;
	u32 fwcapinfo;

	struct mutex lock;

	/* TX packet ready to be sent... */
	int tx_pending_len;		/* -1 while building packet */

	u8 tx_pending_buf[LBS_UPLD_SIZE];
	/* protected by hard_start_xmit serialization */

	/** command-related variables */
	u16 seqnum;

	struct cmd_ctrl_node *cmd_array;
	/** Current command */
	struct cmd_ctrl_node *cur_cmd;
	int cur_cmd_retcode;
	/** command Queues */
	/** Free command buffers */
	struct list_head cmdfreeq;
	/** Pending command buffers */
	struct list_head cmdpendingq;

	wait_queue_head_t cmd_pending;

	/* Command responses sent from the hardware to the driver */
	u8 resp_idx;
	u8 resp_buf[2][LBS_UPLD_SIZE];
	u32 resp_len[2];

	/* Events sent from hardware to driver */
	struct kfifo *event_fifo;

	/* nickname */
	u8 nodename[16];

	/** spin locks */
	spinlock_t driver_lock;

	/** Timers */
	struct timer_list command_timer;
	struct timer_list auto_deepsleep_timer;
	int nr_retries;
	int cmd_timed_out;

	/** current ssid/bssid related parameters*/
	struct current_bss_params curbssparams;

	uint16_t mesh_tlv;
	u8 mesh_ssid[IEEE80211_MAX_SSID_LEN + 1];
	u8 mesh_ssid_len;

	/* IW_MODE_* */
	u8 mode;

	/* Scan results list */
	struct list_head network_list;
	struct list_head network_free_list;
	struct bss_descriptor *networks;

	u16 beacon_period;
	u8 beacon_enable;
	u8 adhoccreate;

	/** capability Info used in Association, start, join */
	u16 capability;

	/** MAC address information */
	u8 current_addr[ETH_ALEN];
	u8 multicastlist[MRVDRV_MAX_MULTICAST_LIST_SIZE][ETH_ALEN];
	u32 nr_of_multicastmacaddr;

	/** 802.11 statistics */
//	struct cmd_DS_802_11_GET_STAT wlan802_11Stat;

	uint16_t enablehwauto;
	uint16_t ratebitmap;

	u8 txretrycount;

	/** Tx-related variables (for single packet tx) */
	struct sk_buff *currenttxskb;

	/** NIC Operation characteristics */
	u16 mac_control;
	u32 connect_status;
	u32 mesh_connect_status;
	u16 regioncode;
	s16 txpower_cur;
	s16 txpower_min;
	s16 txpower_max;

	/** POWER MANAGEMENT AND PnP SUPPORT */
	u8 surpriseremoved;

	u16 psmode;		/* Wlan802_11PowermodeCAM=disable
				   Wlan802_11PowermodeMAX_PSP=enable */
	u32 psstate;
	u8 needtowakeup;

	struct assoc_request * pending_assoc_req;
	struct assoc_request * in_progress_assoc_req;

	/** Encryption parameter */
	struct lbs_802_11_security secinfo;

	/** WEP keys */
	struct enc_key wep_keys[4];
	u16 wep_tx_keyidx;

	/** WPA keys */
	struct enc_key wpa_mcast_key;
	struct enc_key wpa_unicast_key;

	/** WPA Information Elements*/
	u8 wpa_ie[MAX_WPA_IE_LEN];
	u8 wpa_ie_len;

	/** Requested Signal Strength*/
	u16 SNR[MAX_TYPE_B][MAX_TYPE_AVG];
	u16 NF[MAX_TYPE_B][MAX_TYPE_AVG];
	u8 RSSI[MAX_TYPE_B][MAX_TYPE_AVG];
	u8 rawSNR[DEFAULT_DATA_AVG_FACTOR];
	u8 rawNF[DEFAULT_DATA_AVG_FACTOR];
	u16 nextSNRNF;
	u16 numSNRNF;

	u8 radio_on;

	/** data rate stuff */
	u8 cur_rate;

	/** RF calibration data */

#define	MAX_REGION_CHANNEL_NUM	2
	/** region channel data */
	struct region_channel region_channel[MAX_REGION_CHANNEL_NUM];

	/**	MISCELLANEOUS */
	struct lbs_offset_value offsetvalue;

	u32 monitormode;
	u8 fw_ready;
};

extern struct cmd_confirm_sleep confirm_sleep;

#endif
