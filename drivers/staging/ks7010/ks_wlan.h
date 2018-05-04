/* SPDX-License-Identifier: GPL-2.0 */
/*
 *   Driver for KeyStream IEEE802.11 b/g wireless LAN cards.
 *
 *   Copyright (C) 2006-2008 KeyStream Corp.
 *   Copyright (C) 2009 Renesas Technology Corp.
 */

#ifndef _KS_WLAN_H
#define _KS_WLAN_H

#include <linux/atomic.h>
#include <linux/circ_buf.h>
#include <linux/completion.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/wireless.h>

struct ks_wlan_parameter {
	u8 operation_mode;
	u8 channel;
	u8 tx_rate;
	struct {
		u8 size;
		u8 body[16];
	} rate_set;
	u8 bssid[ETH_ALEN];
	struct {
		u8 size;
		u8 body[32 + 1];
	} ssid;
	u8 preamble;
	u8 power_mgmt;
	u32 scan_type;
#define BEACON_LOST_COUNT_MIN 0
#define BEACON_LOST_COUNT_MAX 65535
	u32 beacon_lost_count;
	u32 rts;
	u32 fragment;
	u32 privacy_invoked;
	u32 wep_index;
	struct {
		u8 size;
		u8 val[13 * 2 + 1];
	} wep_key[4];
	u16 authenticate_type;
	u16 phy_type;
	u16 cts_mode;
	u16 phy_info_timer;
};

enum {
	DEVICE_STATE_OFF = 0,	/* this means hw_unavailable is != 0 */
	DEVICE_STATE_PREBOOT,	/* we are in a pre-boot state (empty RAM) */
	DEVICE_STATE_BOOT,	/* boot state (fw upload, run fw) */
	DEVICE_STATE_PREINIT,	/* pre-init state */
	DEVICE_STATE_INIT,	/* init state (restore MIB backup to device) */
	DEVICE_STATE_READY,	/* driver&device are in operational state */
	DEVICE_STATE_SLEEP	/* device in sleep mode */
};

/* SME flag */
#define SME_MODE_SET	    BIT(0)
#define SME_RTS             BIT(1)
#define SME_FRAG            BIT(2)
#define SME_WEP_FLAG        BIT(3)
#define SME_WEP_INDEX       BIT(4)
#define SME_WEP_VAL1        BIT(5)
#define SME_WEP_VAL2        BIT(6)
#define SME_WEP_VAL3        BIT(7)
#define SME_WEP_VAL4        BIT(8)
#define SME_WEP_VAL_MASK    GENMASK(8, 5)
#define SME_RSN             BIT(9)
#define SME_RSN_MULTICAST   BIT(10)
#define SME_RSN_UNICAST	    BIT(11)
#define SME_RSN_AUTH	    BIT(12)

#define SME_AP_SCAN         BIT(13)
#define SME_MULTICAST       BIT(14)

/* SME Event */
enum {
	SME_START,

	SME_MULTICAST_REQUEST,
	SME_MACADDRESS_SET_REQUEST,
	SME_BSS_SCAN_REQUEST,
	SME_SET_FLAG,
	SME_SET_TXKEY,
	SME_SET_KEY1,
	SME_SET_KEY2,
	SME_SET_KEY3,
	SME_SET_KEY4,
	SME_SET_PMK_TSC,
	SME_SET_GMK1_TSC,
	SME_SET_GMK2_TSC,
	SME_SET_GMK3_TSC,
	SME_SET_PMKSA,
	SME_POW_MNGMT_REQUEST,
	SME_PHY_INFO_REQUEST,
	SME_MIC_FAILURE_REQUEST,
	SME_GET_MAC_ADDRESS,
	SME_GET_PRODUCT_VERSION,
	SME_STOP_REQUEST,
	SME_RTS_THRESHOLD_REQUEST,
	SME_FRAGMENTATION_THRESHOLD_REQUEST,
	SME_WEP_INDEX_REQUEST,
	SME_WEP_KEY1_REQUEST,
	SME_WEP_KEY2_REQUEST,
	SME_WEP_KEY3_REQUEST,
	SME_WEP_KEY4_REQUEST,
	SME_WEP_FLAG_REQUEST,
	SME_RSN_UCAST_REQUEST,
	SME_RSN_MCAST_REQUEST,
	SME_RSN_AUTH_REQUEST,
	SME_RSN_ENABLED_REQUEST,
	SME_RSN_MODE_REQUEST,
	SME_WPS_ENABLE_REQUEST,
	SME_WPS_PROBE_REQUEST,
	SME_SET_GAIN,
	SME_GET_GAIN,
	SME_SLEEP_REQUEST,
	SME_SET_REGION,
	SME_MODE_SET_REQUEST,
	SME_START_REQUEST,
	SME_GET_EEPROM_CKSUM,

	SME_MIC_FAILURE_CONFIRM,
	SME_START_CONFIRM,

	SME_MULTICAST_CONFIRM,
	SME_BSS_SCAN_CONFIRM,
	SME_GET_CURRENT_AP,
	SME_POW_MNGMT_CONFIRM,
	SME_PHY_INFO_CONFIRM,
	SME_STOP_CONFIRM,
	SME_RTS_THRESHOLD_CONFIRM,
	SME_FRAGMENTATION_THRESHOLD_CONFIRM,
	SME_WEP_INDEX_CONFIRM,
	SME_WEP_KEY1_CONFIRM,
	SME_WEP_KEY2_CONFIRM,
	SME_WEP_KEY3_CONFIRM,
	SME_WEP_KEY4_CONFIRM,
	SME_WEP_FLAG_CONFIRM,
	SME_RSN_UCAST_CONFIRM,
	SME_RSN_MCAST_CONFIRM,
	SME_RSN_AUTH_CONFIRM,
	SME_RSN_ENABLED_CONFIRM,
	SME_RSN_MODE_CONFIRM,
	SME_MODE_SET_CONFIRM,
	SME_SLEEP_CONFIRM,

	SME_RSN_SET_CONFIRM,
	SME_WEP_SET_CONFIRM,
	SME_TERMINATE,

	SME_EVENT_SIZE
};

/* SME Status */
enum {
	SME_IDLE,
	SME_SETUP,
	SME_DISCONNECT,
	SME_CONNECT
};

#define	SME_EVENT_BUFF_SIZE	128

struct sme_info {
	int sme_status;
	int event_buff[SME_EVENT_BUFF_SIZE];
	unsigned int qhead;
	unsigned int qtail;
	spinlock_t sme_spin;
	unsigned long sme_flag;
};

struct hostt {
	int buff[SME_EVENT_BUFF_SIZE];
	unsigned int qhead;
	unsigned int qtail;
};

#define RSN_IE_BODY_MAX 64
struct rsn_ie {
	u8 id;	/* 0xdd = WPA or 0x30 = RSN */
	u8 size;	/* max ? 255 ? */
	u8 body[RSN_IE_BODY_MAX];
} __packed;

#define WPS_IE_BODY_MAX 255
struct wps_ie {
	u8 id;	/* 221 'dd <len> 00 50 F2 04' */
	u8 size;	/* max ? 255 ? */
	u8 body[WPS_IE_BODY_MAX];
} __packed;

struct local_ap {
	u8 bssid[6];
	u8 rssi;
	u8 sq;
	struct {
		u8 size;
		u8 body[32];
		u8 ssid_pad;
	} ssid;
	struct {
		u8 size;
		u8 body[16];
		u8 rate_pad;
	} rate_set;
	u16 capability;
	u8 channel;
	u8 noise;
	struct rsn_ie wpa_ie;
	struct rsn_ie rsn_ie;
	struct wps_ie wps_ie;
};

#define LOCAL_APLIST_MAX 31
#define LOCAL_CURRENT_AP LOCAL_APLIST_MAX
struct local_aplist {
	int size;
	struct local_ap ap[LOCAL_APLIST_MAX + 1];
};

struct local_gain {
	u8 tx_mode;
	u8 rx_mode;
	u8 tx_gain;
	u8 rx_gain;
};

struct local_eeprom_sum {
	u8 type;
	u8 result;
};

enum {
	EEPROM_OK,
	EEPROM_CHECKSUM_NONE,
	EEPROM_FW_NOT_SUPPORT,
	EEPROM_NG,
};

/* Power Save Status */
enum {
	PS_NONE,
	PS_ACTIVE_SET,
	PS_SAVE_SET,
	PS_CONF_WAIT,
	PS_SNOOZE,
	PS_WAKEUP
};

struct power_save_status {
	atomic_t status;	/* initialvalue 0 */
	struct completion wakeup_wait;
	atomic_t confirm_wait;
	atomic_t snooze_guard;
};

struct sleep_status {
	atomic_t status;	/* initialvalue 0 */
	atomic_t doze_request;
	atomic_t wakeup_request;
};

/* WPA */
struct scan_ext {
	unsigned int flag;
	char ssid[IW_ESSID_MAX_SIZE + 1];
};

#define CIPHER_ID_WPA_NONE    "\x00\x50\xf2\x00"
#define CIPHER_ID_WPA_WEP40   "\x00\x50\xf2\x01"
#define CIPHER_ID_WPA_TKIP    "\x00\x50\xf2\x02"
#define CIPHER_ID_WPA_CCMP    "\x00\x50\xf2\x04"
#define CIPHER_ID_WPA_WEP104  "\x00\x50\xf2\x05"

#define CIPHER_ID_WPA2_NONE   "\x00\x0f\xac\x00"
#define CIPHER_ID_WPA2_WEP40  "\x00\x0f\xac\x01"
#define CIPHER_ID_WPA2_TKIP   "\x00\x0f\xac\x02"
#define CIPHER_ID_WPA2_CCMP   "\x00\x0f\xac\x04"
#define CIPHER_ID_WPA2_WEP104 "\x00\x0f\xac\x05"

#define CIPHER_ID_LEN    4

enum {
	KEY_MGMT_802_1X,
	KEY_MGMT_PSK,
	KEY_MGMT_WPANONE,
};

#define KEY_MGMT_ID_WPA_NONE     "\x00\x50\xf2\x00"
#define KEY_MGMT_ID_WPA_1X       "\x00\x50\xf2\x01"
#define KEY_MGMT_ID_WPA_PSK      "\x00\x50\xf2\x02"
#define KEY_MGMT_ID_WPA_WPANONE  "\x00\x50\xf2\xff"

#define KEY_MGMT_ID_WPA2_NONE    "\x00\x0f\xac\x00"
#define KEY_MGMT_ID_WPA2_1X      "\x00\x0f\xac\x01"
#define KEY_MGMT_ID_WPA2_PSK     "\x00\x0f\xac\x02"
#define KEY_MGMT_ID_WPA2_WPANONE "\x00\x0f\xac\xff"

#define KEY_MGMT_ID_LEN  4

#define MIC_KEY_SIZE 8

struct wpa_key {
	u32 ext_flags;	/* IW_ENCODE_EXT_xxx */
	u8 tx_seq[IW_ENCODE_SEQ_MAX_SIZE];	/* LSB first */
	u8 rx_seq[IW_ENCODE_SEQ_MAX_SIZE];	/* LSB first */
	struct sockaddr addr;	/* ff:ff:ff:ff:ff:ff for broadcast/multicast
				 * (group) keys or unicast address for
				 * individual keys
				 */
	u16 alg;
	u16 key_len;	/* WEP: 5 or 13, TKIP: 32, CCMP: 16 */
	u8 key_val[IW_ENCODING_TOKEN_MAX];
	u8 tx_mic_key[MIC_KEY_SIZE];
	u8 rx_mic_key[MIC_KEY_SIZE];
};

#define WPA_KEY_INDEX_MAX 4
#define WPA_RX_SEQ_LEN 6

struct mic_failure {
	u16 failure;	/* MIC Failure counter 0 or 1 or 2 */
	u16 counter;	/* 1sec counter 0-60 */
	u32 last_failure_time;
	int stop;
};

struct wpa_status {
	int wpa_enabled;
	unsigned int rsn_enabled;
	int version;
	int pairwise_suite;	/* unicast cipher */
	int group_suite;	/* multicast cipher */
	int key_mgmt_suite;
	int auth_alg;
	int txkey;
	struct wpa_key key[WPA_KEY_INDEX_MAX];
	struct scan_ext scan_ext;
	struct mic_failure mic_failure;
};

#include <linux/list.h>
#define PMK_LIST_MAX 8
struct pmk_list {
	u16 size;
	struct list_head head;
	struct pmk {
		struct list_head list;
		u8 bssid[ETH_ALEN];
		u8 pmkid[IW_PMKID_LEN];
	} pmk[PMK_LIST_MAX];
};

struct wps_status {
	int wps_enabled;
	int ielen;
	u8 ie[255];
};

/* Tx Device struct */
#define	TX_DEVICE_BUFF_SIZE	1024

struct ks_wlan_private;

/**
 * struct tx_device_buffer - Queue item for the tx queue.
 * @sendp: Pointer to the send request data.
 * @size: Size of @sendp data.
 * @complete_handler: Function called once data write to device is complete.
 * @arg1: First argument to @complete_handler.
 * @arg2: Second argument to @complete_handler.
 */
struct tx_device_buffer {
	unsigned char *sendp;
	unsigned int size;
	void (*complete_handler)(struct ks_wlan_private *priv,
				 struct sk_buff *skb);
	struct sk_buff *skb;
};

/**
 * struct tx_device - Tx buffer queue.
 * @tx_device_buffer: Queue buffer.
 * @qhead: Head of tx queue.
 * @qtail: Tail of tx queue.
 * @tx_dev_lock: Queue lock.
 */
struct tx_device {
	struct tx_device_buffer tx_dev_buff[TX_DEVICE_BUFF_SIZE];
	unsigned int qhead;
	unsigned int qtail;
	spinlock_t tx_dev_lock;	/* protect access to the queue */
};

/* Rx Device struct */
#define	RX_DATA_SIZE	(2 + 2 + 2347 + 1)
#define	RX_DEVICE_BUFF_SIZE	32

/**
 * struct rx_device_buffer - Queue item for the rx queue.
 * @data: rx data.
 * @size: Size of @data.
 */
struct rx_device_buffer {
	unsigned char data[RX_DATA_SIZE];
	unsigned int size;
};

/**
 * struct rx_device - Rx buffer queue.
 * @rx_device_buffer: Queue buffer.
 * @qhead: Head of rx queue.
 * @qtail: Tail of rx queue.
 * @rx_dev_lock: Queue lock.
 */
struct rx_device {
	struct rx_device_buffer rx_dev_buff[RX_DEVICE_BUFF_SIZE];
	unsigned int qhead;
	unsigned int qtail;
	spinlock_t rx_dev_lock;	/* protect access to the queue */
};

struct ks_wlan_private {
	/* hardware information */
	void *if_hw;
	struct workqueue_struct *wq;
	struct delayed_work rw_dwork;
	struct tasklet_struct rx_bh_task;

	struct net_device *net_dev;
	struct net_device_stats nstats;
	struct iw_statistics wstats;

	struct completion confirm_wait;

	/* trx device & sme */
	struct tx_device tx_dev;
	struct rx_device rx_dev;
	struct sme_info sme_i;
	u8 *rxp;
	unsigned int rx_size;
	struct tasklet_struct sme_task;
	struct work_struct wakeup_work;
	int scan_ind_count;

	unsigned char eth_addr[ETH_ALEN];

	struct local_aplist aplist;
	struct local_ap current_ap;
	struct power_save_status psstatus;
	struct sleep_status sleepstatus;
	struct wpa_status wpa;
	struct pmk_list pmklist;
	/* wireless parameter */
	struct ks_wlan_parameter reg;
	u8 current_rate;

	char nick[IW_ESSID_MAX_SIZE + 1];

	spinlock_t multicast_spin;

	spinlock_t dev_read_lock;
	wait_queue_head_t devread_wait;

	unsigned int need_commit;	/* for ioctl */

	/* DeviceIoControl */
	bool is_device_open;
	atomic_t event_count;
	atomic_t rec_count;
	int dev_count;
#define DEVICE_STOCK_COUNT 20
	unsigned char *dev_data[DEVICE_STOCK_COUNT];
	int dev_size[DEVICE_STOCK_COUNT];

	/* ioctl : IOCTL_FIRMWARE_VERSION */
	unsigned char firmware_version[128 + 1];
	int version_size;

	bool mac_address_valid;

	int dev_state;

	struct sk_buff *skb;
	unsigned int cur_rx;	/* Index into the Rx buffer of next Rx pkt. */
#define FORCE_DISCONNECT    0x80000000
#define CONNECT_STATUS_MASK 0x7FFFFFFF
	u32 connect_status;
	int infra_status;
	u8 scan_ssid_len;
	u8 scan_ssid[IW_ESSID_MAX_SIZE + 1];
	struct local_gain gain;
	struct wps_status wps;
	u8 sleep_mode;

	u8 region;
	struct local_eeprom_sum eeprom_sum;
	u8 eeprom_checksum;

	struct hostt hostt;

	unsigned long last_doze;
	unsigned long last_wakeup;

	uint wakeup_count;	/* for detect wakeup loop */
};

static inline void inc_txqhead(struct ks_wlan_private *priv)
{
	priv->tx_dev.qhead = (priv->tx_dev.qhead + 1) % TX_DEVICE_BUFF_SIZE;
}

static inline void inc_txqtail(struct ks_wlan_private *priv)
{
	priv->tx_dev.qtail = (priv->tx_dev.qtail + 1) % TX_DEVICE_BUFF_SIZE;
}

static inline bool txq_has_space(struct ks_wlan_private *priv)
{
	return (CIRC_SPACE(priv->tx_dev.qhead, priv->tx_dev.qtail,
			   TX_DEVICE_BUFF_SIZE) > 0);
}

static inline void inc_rxqhead(struct ks_wlan_private *priv)
{
	priv->rx_dev.qhead = (priv->rx_dev.qhead + 1) % RX_DEVICE_BUFF_SIZE;
}

static inline void inc_rxqtail(struct ks_wlan_private *priv)
{
	priv->rx_dev.qtail = (priv->rx_dev.qtail + 1) % RX_DEVICE_BUFF_SIZE;
}

static inline bool rxq_has_space(struct ks_wlan_private *priv)
{
	return (CIRC_SPACE(priv->rx_dev.qhead, priv->rx_dev.qtail,
			   RX_DEVICE_BUFF_SIZE) > 0);
}

static inline unsigned int txq_count(struct ks_wlan_private *priv)
{
	return CIRC_CNT_TO_END(priv->tx_dev.qhead, priv->tx_dev.qtail,
			       TX_DEVICE_BUFF_SIZE);
}

static inline unsigned int rxq_count(struct ks_wlan_private *priv)
{
	return CIRC_CNT_TO_END(priv->rx_dev.qhead, priv->rx_dev.qtail,
			       RX_DEVICE_BUFF_SIZE);
}

int ks_wlan_net_start(struct net_device *dev);
int ks_wlan_net_stop(struct net_device *dev);
bool is_connect_status(u32 status);
bool is_disconnect_status(u32 status);

#endif /* _KS_WLAN_H */
