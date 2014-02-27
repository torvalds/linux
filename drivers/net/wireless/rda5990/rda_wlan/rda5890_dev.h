/**
  * This file contains definitions and data structures specific
  * to Marvell 802.11 NIC. It contains the Device Information
  * structure struct lbs_private..
  */
#ifndef _RDA5890_DEV_H_
#define _RDA5890_DEV_H_
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <linux/ethtool.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/wakelock.h>
#include "rda5890_defs.h"
//#include "hif_sdio.h"


#ifndef MAX_WPA_IE_LEN
#define MAX_WPA_IE_LEN 100
#endif

#ifndef MAX_RATES
#define MAX_RATES			14
#endif

#define DEV_NAME_LEN			32

#define RDA5890_MAX_NETWORK_NUM    32

/** RSSI related MACRO DEFINITIONS */
#define RDA5890_NF_DEFAULT_SCAN_VALUE		(-96)
#define PERFECT_RSSI ((u8)50)
#define WORST_RSSI   ((u8)0)
#define RSSI_DIFF    ((u8)(PERFECT_RSSI - WORST_RSSI))

/** RTS/FRAG related defines */
#define RDA5890_RTS_MIN_VALUE		0
#define RDA5890_RTS_MAX_VALUE		2347
#define RDA5890_FRAG_MIN_VALUE		256
#define RDA5890_FRAG_MAX_VALUE		2346

#define KEY_LEN_WPA_AES			16
#define KEY_LEN_WPA_TKIP		32
#define KEY_LEN_WEP_104			13
#define KEY_LEN_WEP_40			5

#define BIT7                    (1 << 7)
#define BIT6                    (1 << 6)
#define BIT5                    (1 << 5)
#define BIT4                    (1 << 4)
#define BIT3                    (1 << 3)
#define BIT2                    (1 << 2)
#define BIT1                    (1 << 1)
#define BIT0                    (1 << 0)

#define RDA_SLEEP_ENABLE        BIT0
#define RDA_SLEEP_PREASSO       BIT1

//#define WIFI_UNIT_TEST

/** KEY_TYPE_ID */
enum KEY_TYPE_ID {
	KEY_TYPE_ID_WEP = 0,
	KEY_TYPE_ID_TKIP,
	KEY_TYPE_ID_AES
};

enum PACKET_TYPE{
    WID_REQUEST_PACKET,
    WID_REQUEST_POLLING_PACKET,
    DATA_REQUEST_PACKET
};

/** KEY_INFO_WPA (applies to both TKIP and AES/CCMP) */
enum KEY_INFO_WPA {
	KEY_INFO_WPA_MCAST = 0x01,
	KEY_INFO_WPA_UNICAST = 0x02,
	KEY_INFO_WPA_ENABLED = 0x04
};

/* RDA5890 defined bss descriptor, 44 bytes for each bss */
struct rda5890_bss_descriptor {
	u8 ssid[IW_ESSID_MAX_SIZE + 1];
	u8 bss_type;
	u8 channel;
	u8 dot11i_info;
	u8 bssid[ETH_ALEN];
	u8 rssi;
	u8 auth_info;
	u8 rsn_cap[2];
} __attribute__ ((packed));

/**
 *  @brief Structure used to store information for each beacon/probe response
 */
struct bss_descriptor {
	struct rda5890_bss_descriptor data;

    	u8 bssid[ETH_ALEN];

	u8 ssid[IW_ESSID_MAX_SIZE + 1];
	u8 ssid_len;

	u16 capability;
	u32 rssi;
	u32 channel;
	u16 beaconperiod;

	/* IW_MODE_AUTO, IW_MODE_ADHOC, IW_MODE_INFRA */
	u8 mode;

	/* zero-terminated array of supported data rates */
	u8 rates[MAX_RATES + 1];    
	u8 wpa_ie[MAX_WPA_IE_LEN];
	size_t wpa_ie_len;
	u8 rsn_ie[MAX_WPA_IE_LEN];
	size_t rsn_ie_len;

      u8 wapi_ie[100];
      size_t wapi_ie_len; //wapi valid payload length

	unsigned long last_scanned;


	struct list_head list;
};

/* Generic structure to hold all key types. */
struct enc_key {
	u16 len;
	u16 flags;  /* KEY_INFO_* from defs.h */
	u16 type; /* KEY_TYPE_* from defs.h */
	u8 key[32];
};

struct rda5890_802_11_security {
	u8 WPAenabled;
	u8 WPA2enabled;
	u8 wep_enabled;
	u8 auth_mode;
	u32 key_mgmt;
    u32 cipther_type;
};

/** Private structure for the rda5890 device */
struct rda5890_private {
	char name[DEV_NAME_LEN];

	void *card;
	struct net_device *dev;

	struct net_device_stats stats;

	/** current ssid/bssid related parameters and status */
	int connect_status;
	struct rda5890_bss_descriptor curbssparams;
	struct iw_statistics wstats; 

	/** association flags */
	unsigned char assoc_ssid[IW_ESSID_MAX_SIZE + 1];
    unsigned char assoc_bssid[6];
	unsigned char assoc_ssid_len; 
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
#define ASSOC_FLAG_ASSOC_RETRY		12
#define ASSOC_FLAG_ASSOC_START  13
#define ASSOC_FLAG_WLAN_CONNECTING  14

	unsigned long assoc_flags;
	unsigned char imode;
	unsigned char authtype;

	/** debugfs */
	struct dentry *debugfs_dir;
	struct dentry *debugfs_files[6];

	/** for wid request and response */
	struct mutex wid_lock;
	struct completion wid_done;
	int wid_pending;
	char *wid_rsp;
	unsigned short wid_rsp_len;
	char wid_msg_id;

	/** delayed worker */
	struct workqueue_struct *work_thread;
	struct delayed_work scan_work;
	struct delayed_work assoc_work;
	struct delayed_work assoc_done_work;
    struct delayed_work   wlan_connect_work;

	/** Hardware access */
	int (*hw_host_to_card) (struct rda5890_private *priv, u8 *payload, u16 nb, 
	                            unsigned char packet_type);

	/** Scan results list */
	int scan_running;
	struct list_head network_list;
	struct list_head network_free_list;
	struct bss_descriptor *networks;

	/** Encryption parameter */
	struct rda5890_802_11_security secinfo;

	/** WEP keys */
	struct enc_key wep_keys[4];
	u16 wep_tx_keyidx;

	/** WPA keys */
	struct enc_key wpa_mcast_key;
	struct enc_key wpa_unicast_key;

	/** WPA Information Elements*/
	u8 wpa_ie[MAX_WPA_IE_LEN];
	u8 wpa_ie_len;

	u8 is_wapi;
    	/** sleep/awake flag */
#ifdef WIFI_POWER_MANAGER        
	atomic_t sleep_flag;
#endif
    u8  first_init;
    u16 version;
};

extern int rda5890_sleep_flags;

#ifdef WIFI_TEST_MODE
extern unsigned char rda_5990_wifi_in_test_mode(void);
#endif

#ifdef WIFI_UNLOCK_SYSTEM
extern atomic_t     wake_lock_counter;
extern struct wake_lock sleep_worker_wake_lock;
extern void rda5990_wakeLock(void);
extern void rda5990_wakeUnlock(void);
extern void rda5990_wakeLock_destroy(void);
#endif

struct rda5890_private *rda5890_add_card(void *card);
void rda5890_remove_card(struct rda5890_private *priv);
int  rda5890_start_card(struct rda5890_private *priv);
void rda5890_stop_card(struct rda5890_private *priv);
int rda5890_sdio_core_wake_mode(struct rda5890_private *priv);
int rda5890_sdio_core_sleep_mode(struct rda5890_private *priv);
int rda5890_sdio_core_wake_mode_polling(struct rda5890_private *priv);

void rda5890_sdio_set_notch_by_channel(struct rda5890_private *priv, unsigned int channel);
void rda5890_rssi_up_to_200(struct rda5890_private *priv);
void rda5990_assoc_power_save(struct rda5890_private *priv);
unsigned char is_sdio_init_complete(void);
unsigned char is_sdio_patch_complete(void);
int rda5890_sdio_init(struct rda5890_private *priv);
int rda5890_init_pm(struct rda5890_private *priv);
void rda5890_shedule_timeout(int msecs);
int rda5890_read_mac(char* buf);
int rda5890_write_mac(char * buf);
int rda5890_disable_block_bt(struct rda5890_private *priv);
int rda5890_disable_self_cts(struct rda5890_private *priv);
int rda5890_set_active_scan_time(struct rda5890_private *priv);
int rda5890_set_test_mode(struct rda5890_private *priv);
int rda5890_get_fw_version_polling(struct rda5890_private *priv, unsigned int* version);
#endif
