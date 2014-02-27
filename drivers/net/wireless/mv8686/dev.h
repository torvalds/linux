/**
  * This file contains definitions and data structures specific
  * to Marvell 802.11 NIC. It contains the Device Information
  * structure struct lbs_private..
  */
#ifndef _LBS_DEV_H_
#define _LBS_DEV_H_

#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <linux/ethtool.h>
#include <linux/debugfs.h>
#include <linux/spinlock.h>
#include <linux/version.h>

#include "defs.h"
#include "hostcmd.h"

extern struct ethtool_ops lbs_ethtool_ops;

#define	MAX_BSSID_PER_CHANNEL		16

#define NR_TX_QUEUE			3

/* For the extended Scan */
#define MAX_EXTENDED_SCAN_BSSID_LIST    MAX_BSSID_PER_CHANNEL * \
						MRVDRV_MAX_CHANNEL_SIZE + 1

#define	MAX_REGION_CHANNEL_NUM	2

/** Chan-freq-TxPower mapping table*/
struct chan_freq_power {
	/** channel Number		*/
	u16 channel;
	/** frequency of this channel	*/
	u32 freq;
	/** Max allowed Tx power level	*/
	u16 maxtxpower;
	/** TRUE:channel unsupported;  FLASE:supported*/
	u8 unsupported;
};

/** region-band mapping table*/
struct region_channel {
	/** TRUE if this entry is valid		     */
	u8 valid;
	/** region code for US, Japan ...	     */
	u8 region;
	/** band B/G/A, used for BAND_CONFIG cmd	     */
	u8 band;
	/** Actual No. of elements in the array below */
	u8 nrcfp;
	/** chan-freq-txpower mapping table*/
	struct chan_freq_power *CFP;
};

struct lbs_802_11_security {
	u8 WPAenabled;
	u8 WPA2enabled;
	u8 wep_enabled;
	u8 auth_mode;
	u32 key_mgmt;
};

/** Current Basic Service Set State Structure */
struct current_bss_params {
	/** bssid */
	u8 bssid[ETH_ALEN];
	/** ssid */
	u8 ssid[IW_ESSID_MAX_SIZE + 1];
	u8 ssid_len;

	/** band */
	u8 band;
	/** channel */
	u8 channel;
	/** zero-terminated array of supported data rates */
	u8 rates[MAX_RATES + 1];
};

/** sleep_params */
struct sleep_params {
	uint16_t sp_error;
	uint16_t sp_offset;
	uint16_t sp_stabletime;
	uint8_t  sp_calcontrol;
	uint8_t  sp_extsleepclk;
	uint16_t sp_reserved;
};

#define MAX_SCAN_REQ        16

struct lbs_scan_req
{
    struct list_head list;
    int ssid_len;   /* ssid_len == 0 means broadcast scanning */
    char ssid[IW_ESSID_MAX_SIZE+1]; 
};

/** Private structure for the MV device */
struct lbs_private 
{
	int infra_open;

	char name[DEV_NAME_LEN];

	void *card;
	struct net_device *dev;

	struct iw_statistics wstats;
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

	/* Download sent:
	   bit0 1/0=data_sent/data_tx_done,
	   bit1 1/0=cmd_sent/cmd_tx_done,
	   all other bits reserved 0 */
	u8 dnld_sent;

	/** thread to service interrupts */
	struct task_struct *main_thread;
	wait_queue_head_t waitq;
	struct workqueue_struct *work_thread;

#if (MV8686_SUPPORT_MCAST == 1)
	struct work_struct mcast_work;
#endif

	/** Scanning */
	struct delayed_work    scan_work;
	spinlock_t             scan_work_lock;
	struct delayed_work    assoc_work;
	struct work_struct     sync_channel;
	
	/* remember which channel was scanned last, != 0 if currently scanning */
	int scan_channel;
	u8 scan_ssid[IW_ESSID_MAX_SIZE + 1];
	u8 scan_ssid_len;

	/** Hardware access */
	int (*hw_host_to_card) (struct lbs_private *priv, u8 type, u8 *payload, u16 nb);
	void (*reset_card) (struct lbs_private *priv);

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

	/* command-related variables */
	u16 seqnum;

    /*
     * Command materials
     */
	struct cmd_ctrl_node *cmd_array;      /* Allocated buffer for cmds */
	struct cmd_ctrl_node *cur_cmd;        /* Current command */
	int                  cur_cmd_retcode; /* Return code for current cmd */
	struct list_head     cmdfreeq;        /* Free command buffers */
	struct list_head     cmdpendingq;     /* Pending command buffers */
	wait_queue_head_t    cmd_pending;
	u8                   resp_idx;        /* Cmd responses sent from the -- */
	u8                   resp_buf[2][LBS_UPLD_SIZE]; /* hardware to the driver */
	u32                  resp_len[2];

	struct timer_list    command_timer;   /* Command timeout timers */
	int                  nr_retries;
	int                  cmd_timed_out;
	
	/* Events sent from hardware to driver */
	struct kfifo *event_fifo;

	/* nickname */
	u8 nodename[16];

	/** spin locks */
	spinlock_t driver_lock;

	/** current ssid/bssid related parameters*/
	struct current_bss_params curbssparams;

	/* IW_MODE_* */
	u8 mode;

	/* Scan results list */
	struct list_head network_list;
	struct list_head network_free_list;
	struct bss_descriptor *networks;

    struct list_head scan_req_list;
	struct list_head scan_req_free_list;
	struct lbs_scan_req *scan_req;
	
	u16 beacon_period;
	u8 beacon_enable;
	u8 adhoccreate;

	/** capability Info used in Association, start, join */
	u16 capability;

	/** MAC address information */
	u8 current_addr[ETH_ALEN];
	u8 multicastlist[MRVDRV_MAX_MULTICAST_LIST_SIZE][ETH_ALEN];
	u32 nr_of_multicastmacaddr;

	uint16_t enablehwauto;
	uint16_t ratebitmap;

	u8 txretrycount;

	/** Tx-related variables (for single packet tx) */
	struct sk_buff *currenttxskb;

	/** NIC Operation characteristics */
	u16 mac_control;
	u32 connect_status;
	u16 regioncode;
	s16 txpower_cur;
	s16 txpower_min;
	s16 txpower_max;

	/** POWER MANAGEMENT AND PnP SUPPORT */
	u8 	surpriseremoved;

	u16 psmode;		/* Wlan802_11PowermodeCAM=disable
				   				 Wlan802_11PowermodeMAX_PSP=enable */
	u32 psstate;
	
	u8 	deepsleep;				/* Deep sleep status : true (on) : false (off) */
	u8	auto_deepsleep;		/* */
	wait_queue_head_t ds_waitq; /* Deep sleep wakeup queue */
	
	char ps_supported;
	u8 needtowakeup;

#if (NEW_MV8686_PS == 1)
	struct timer_list   turnoff_timer;
	u8					turnoff_idle_count;
	u8					wifi_state; //0-busy 1-idle
	struct delayed_work ps_work;
	unsigned long		scan_result_timeout;
	u8					need_reassoc; //0-no 1-yes
	struct timer_list   disconnect_timer; //check association whether is successful.
	u8					associate_retry;
	u8					needto_notify_upper;
#endif

#if (ANDROID_POWER_SAVE == 1)
	struct timer_list   ps_timer;          /* Power save timer. */
	struct delayed_work ps_work;           /* Worker to do auto power save. */
	int                 wifi_ps_work_req;  /* Which request to the ps_work. */
	spinlock_t          ps_lock;           /* Lock for power save. */
#endif

	struct assoc_request * pending_assoc_req;
	struct assoc_request * in_progress_assoc_req;
#if (AUTO_REASSOC == 1)
	struct assoc_request * prev_assoc_req;
	int 			     reassoc_count;
#endif

	/** Encryption parameter */
	struct lbs_802_11_security secinfo;

	/** WEP keys */
	struct enc_key wep_keys[4];
	u16 wep_tx_keyidx;

	/** WPA keys */
	struct enc_key wpa_mcast_key;
	struct enc_key wpa_unicast_key;

/*
 * In theory, the IE is limited to the IE length, 255,
 * but in practice 64 bytes are enough.
 */
#define MAX_WPA_IE_LEN 64

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

	struct region_channel universal_channel[MAX_REGION_CHANNEL_NUM];

	/** 11D and Domain Regulatory Data */
	struct lbs_802_11d_domain_reg domainreg;
	struct parsed_region_chan_11d parsed_region_chan;

	/** FSM variable for 11d support */
	u32 enable11d;

	/**	MISCELLANEOUS */
	struct lbs_offset_value offsetvalue;

	u32 monitormode;
	u8 fw_ready;
};

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
#define GET_PRIV_FROM_NETDEV(dev) (struct lbs_private *)(dev)->priv
#else
#define GET_PRIV_FROM_NETDEV(dev) (struct lbs_private *)(dev)->ml_priv
#endif

extern struct cmd_confirm_sleep confirm_sleep;

/**
 *  @brief Structure used to store information for each beacon/probe response
 */
struct bss_descriptor {
	u8 bssid[ETH_ALEN];

	u8 ssid[IW_ESSID_MAX_SIZE + 1];
	u8 ssid_len;

	u16 capability;
	u32 rssi;
	u32 channel;
	u16 beaconperiod;
	u32 atimwindow;

	/* IW_MODE_AUTO, IW_MODE_ADHOC, IW_MODE_INFRA */
	u8 mode;

	/* zero-terminated array of supported data rates */
	u8 rates[MAX_RATES + 1];

	unsigned long last_scanned;

	union ieeetypes_phyparamset phyparamset;
	union IEEEtypes_ssparamset ssparamset;

	struct ieeetypes_countryinfofullset countryinfo;

	u8 wpa_ie[MAX_WPA_IE_LEN];
	size_t wpa_ie_len;
	u8 rsn_ie[MAX_WPA_IE_LEN];
	size_t rsn_ie_len;

	struct list_head list;
};

/** Association request
 *
 * Encapsulates all the options that describe a specific assocation request
 * or configuration of the wireless card's radio, mode, and security settings.
 */
#define ASSOC_FLAG_SSID			1
#define ASSOC_FLAG_CHANNEL		2
#define ASSOC_FLAG_BAND			3
#define ASSOC_FLAG_MODE			4
#define ASSOC_FLAG_BSSID		5
#define ASSOC_FLAG_WEP_KEYS		6
#define ASSOC_FLAG_WEP_TX_KEYIDX	7
#define ASSOC_FLAG_WPA_MCAST_KEY	8
#define ASSOC_FLAG_WPA_UCAST_KEY	9
#define ASSOC_FLAG_SECINFO		10
#define ASSOC_FLAG_WPA_IE		11

struct assoc_request {
	unsigned long flags;

	u8 ssid[IW_ESSID_MAX_SIZE + 1];
	u8 ssid_len;
	u8 channel;
	u8 band;
	u8 mode;
	u8 bssid[ETH_ALEN] __attribute__ ((aligned (2)));

	/** WEP keys */
	struct enc_key wep_keys[4];
	u16 wep_tx_keyidx;

	/** WPA keys */
	struct enc_key wpa_mcast_key;
	struct enc_key wpa_unicast_key;

	struct lbs_802_11_security secinfo;

	/** WPA Information Elements*/
	u8 wpa_ie[MAX_WPA_IE_LEN];
	u8 wpa_ie_len;

	/* BSS to associate with for infrastructure of Ad-Hoc join */
	struct bss_descriptor bss;
};

#endif
