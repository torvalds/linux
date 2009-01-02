#ifndef HOSTAP_AP_H
#define HOSTAP_AP_H

#include "hostap_80211.h"

/* AP data structures for STAs */

/* maximum number of frames to buffer per STA */
#define STA_MAX_TX_BUFFER 32

/* STA flags */
#define WLAN_STA_AUTH BIT(0)
#define WLAN_STA_ASSOC BIT(1)
#define WLAN_STA_PS BIT(2)
#define WLAN_STA_TIM BIT(3) /* TIM bit is on for PS stations */
#define WLAN_STA_PERM BIT(4) /* permanent; do not remove entry on expiration */
#define WLAN_STA_AUTHORIZED BIT(5) /* If 802.1X is used, this flag is
				    * controlling whether STA is authorized to
				    * send and receive non-IEEE 802.1X frames
				    */
#define WLAN_STA_PENDING_POLL BIT(6) /* pending activity poll not ACKed */

#define WLAN_RATE_1M BIT(0)
#define WLAN_RATE_2M BIT(1)
#define WLAN_RATE_5M5 BIT(2)
#define WLAN_RATE_11M BIT(3)
#define WLAN_RATE_COUNT 4

/* Maximum size of Supported Rates info element. IEEE 802.11 has a limit of 8,
 * but some pre-standard IEEE 802.11g products use longer elements. */
#define WLAN_SUPP_RATES_MAX 32

/* Try to increase TX rate after # successfully sent consecutive packets */
#define WLAN_RATE_UPDATE_COUNT 50

/* Decrease TX rate after # consecutive dropped packets */
#define WLAN_RATE_DECREASE_THRESHOLD 2

struct sta_info {
	struct list_head list;
	struct sta_info *hnext; /* next entry in hash table list */
	atomic_t users; /* number of users (do not remove if > 0) */
	struct proc_dir_entry *proc;

	u8 addr[6];
	u16 aid; /* STA's unique AID (1 .. 2007) or 0 if not yet assigned */
	u32 flags;
	u16 capability;
	u16 listen_interval; /* or beacon_int for APs */
	u8 supported_rates[WLAN_SUPP_RATES_MAX];

	unsigned long last_auth;
	unsigned long last_assoc;
	unsigned long last_rx;
	unsigned long last_tx;
	unsigned long rx_packets, tx_packets;
	unsigned long rx_bytes, tx_bytes;
	struct sk_buff_head tx_buf;
	/* FIX: timeout buffers with an expiry time somehow derived from
	 * listen_interval */

	s8 last_rx_silence; /* Noise in dBm */
	s8 last_rx_signal; /* Signal strength in dBm */
	u8 last_rx_rate; /* TX rate in 0.1 Mbps */
	u8 last_rx_updated; /* IWSPY's struct iw_quality::updated */

	u8 tx_supp_rates; /* bit field of supported TX rates */
	u8 tx_rate; /* current TX rate (in 0.1 Mbps) */
	u8 tx_rate_idx; /* current TX rate (WLAN_RATE_*) */
	u8 tx_max_rate; /* max TX rate (WLAN_RATE_*) */
	u32 tx_count[WLAN_RATE_COUNT]; /* number of frames sent (per rate) */
	u32 rx_count[WLAN_RATE_COUNT]; /* number of frames received (per rate)
					*/
	u32 tx_since_last_failure;
	u32 tx_consecutive_exc;

	struct lib80211_crypt_data *crypt;

	int ap; /* whether this station is an AP */

	local_info_t *local;

#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
	union {
		struct {
			char *challenge; /* shared key authentication
					  * challenge */
		} sta;
		struct {
			int ssid_len;
			unsigned char ssid[MAX_SSID_LEN + 1]; /* AP's ssid */
			int channel;
			unsigned long last_beacon; /* last RX beacon time */
		} ap;
	} u;

	struct timer_list timer;
	enum { STA_NULLFUNC = 0, STA_DISASSOC, STA_DEAUTH } timeout_next;
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */
};


#define MAX_STA_COUNT 1024

/* Maximum number of AIDs to use for STAs; must be 2007 or lower
 * (8802.11 limitation) */
#define MAX_AID_TABLE_SIZE 128

#define STA_HASH_SIZE 256
#define STA_HASH(sta) (sta[5])


/* Default value for maximum station inactivity. After AP_MAX_INACTIVITY_SEC
 * has passed since last received frame from the station, a nullfunc data
 * frame is sent to the station. If this frame is not acknowledged and no other
 * frames have been received, the station will be disassociated after
 * AP_DISASSOC_DELAY. Similarily, a the station will be deauthenticated after
 * AP_DEAUTH_DELAY. AP_TIMEOUT_RESOLUTION is the resolution that is used with
 * max inactivity timer. */
#define AP_MAX_INACTIVITY_SEC (5 * 60)
#define AP_DISASSOC_DELAY (HZ)
#define AP_DEAUTH_DELAY (HZ)

/* ap_policy: whether to accept frames to/from other APs/IBSS */
typedef enum {
	AP_OTHER_AP_SKIP_ALL = 0,
	AP_OTHER_AP_SAME_SSID = 1,
	AP_OTHER_AP_ALL = 2,
	AP_OTHER_AP_EVEN_IBSS = 3
} ap_policy_enum;

#define PRISM2_AUTH_OPEN BIT(0)
#define PRISM2_AUTH_SHARED_KEY BIT(1)


/* MAC address-based restrictions */
struct mac_entry {
	struct list_head list;
	u8 addr[6];
};

struct mac_restrictions {
	enum { MAC_POLICY_OPEN = 0, MAC_POLICY_ALLOW, MAC_POLICY_DENY } policy;
	unsigned int entries;
	struct list_head mac_list;
	spinlock_t lock;
};


struct add_sta_proc_data {
	u8 addr[ETH_ALEN];
	struct add_sta_proc_data *next;
};


typedef enum { WDS_ADD, WDS_DEL } wds_oper_type;
struct wds_oper_data {
	wds_oper_type type;
	u8 addr[ETH_ALEN];
	struct wds_oper_data *next;
};


struct ap_data {
	int initialized; /* whether ap_data has been initialized */
	local_info_t *local;
	int bridge_packets; /* send packet to associated STAs directly to the
			     * wireless media instead of higher layers in the
			     * kernel */
	unsigned int bridged_unicast; /* number of unicast frames bridged on
				       * wireless media */
	unsigned int bridged_multicast; /* number of non-unicast frames
					 * bridged on wireless media */
	unsigned int tx_drop_nonassoc; /* number of unicast TX packets dropped
					* because they were to an address that
					* was not associated */
	int nullfunc_ack; /* use workaround for nullfunc frame ACKs */

	spinlock_t sta_table_lock;
	int num_sta; /* number of entries in sta_list */
	struct list_head sta_list; /* STA info list head */
	struct sta_info *sta_hash[STA_HASH_SIZE];

	struct proc_dir_entry *proc;

	ap_policy_enum ap_policy;
	unsigned int max_inactivity;
	int autom_ap_wds;

	struct mac_restrictions mac_restrictions; /* MAC-based auth */
	int last_tx_rate;

	struct work_struct add_sta_proc_queue;
	struct add_sta_proc_data *add_sta_proc_entries;

	struct work_struct wds_oper_queue;
	struct wds_oper_data *wds_oper_entries;

	u16 tx_callback_idx;

#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
	/* pointers to STA info; based on allocated AID or NULL if AID free
	 * AID is in the range 1-2007, so sta_aid[0] corresponders to AID 1
	 * and so on
	 */
	struct sta_info *sta_aid[MAX_AID_TABLE_SIZE];

	u16 tx_callback_auth, tx_callback_assoc, tx_callback_poll;

	/* WEP operations for generating challenges to be used with shared key
	 * authentication */
	struct lib80211_crypto_ops *crypt;
	void *crypt_priv;
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */
};


void hostap_rx(struct net_device *dev, struct sk_buff *skb,
	       struct hostap_80211_rx_status *rx_stats);
void hostap_init_data(local_info_t *local);
void hostap_init_ap_proc(local_info_t *local);
void hostap_free_data(struct ap_data *ap);
void hostap_check_sta_fw_version(struct ap_data *ap, int sta_fw_ver);

typedef enum {
	AP_TX_CONTINUE, AP_TX_DROP, AP_TX_RETRY, AP_TX_BUFFERED,
	AP_TX_CONTINUE_NOT_AUTHORIZED
} ap_tx_ret;
struct hostap_tx_data {
	struct sk_buff *skb;
	int host_encrypt;
	struct lib80211_crypt_data *crypt;
	void *sta_ptr;
};
ap_tx_ret hostap_handle_sta_tx(local_info_t *local, struct hostap_tx_data *tx);
void hostap_handle_sta_release(void *ptr);
void hostap_handle_sta_tx_exc(local_info_t *local, struct sk_buff *skb);
int hostap_update_sta_ps(local_info_t *local, struct ieee80211_hdr_4addr *hdr);
typedef enum {
	AP_RX_CONTINUE, AP_RX_DROP, AP_RX_EXIT, AP_RX_CONTINUE_NOT_AUTHORIZED
} ap_rx_ret;
ap_rx_ret hostap_handle_sta_rx(local_info_t *local, struct net_device *dev,
			       struct sk_buff *skb,
			       struct hostap_80211_rx_status *rx_stats,
			       int wds);
int hostap_handle_sta_crypto(local_info_t *local, struct ieee80211_hdr_4addr *hdr,
			     struct lib80211_crypt_data **crypt,
			     void **sta_ptr);
int hostap_is_sta_assoc(struct ap_data *ap, u8 *sta_addr);
int hostap_is_sta_authorized(struct ap_data *ap, u8 *sta_addr);
int hostap_add_sta(struct ap_data *ap, u8 *sta_addr);
int hostap_update_rx_stats(struct ap_data *ap, struct ieee80211_hdr_4addr *hdr,
			   struct hostap_80211_rx_status *rx_stats);
void hostap_update_rates(local_info_t *local);
void hostap_add_wds_links(local_info_t *local);
void hostap_wds_link_oper(local_info_t *local, u8 *addr, wds_oper_type type);

#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
void hostap_deauth_all_stas(struct net_device *dev, struct ap_data *ap,
			    int resend);
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */

#endif /* HOSTAP_AP_H */
