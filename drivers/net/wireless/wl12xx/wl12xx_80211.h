#ifndef __WL12XX_80211_H__
#define __WL12XX_80211_H__

#include <linux/if_ether.h>	/* ETH_ALEN */
#include <linux/if_arp.h>

/* RATES */
#define IEEE80211_CCK_RATE_1MB		        0x02
#define IEEE80211_CCK_RATE_2MB		        0x04
#define IEEE80211_CCK_RATE_5MB		        0x0B
#define IEEE80211_CCK_RATE_11MB		        0x16
#define IEEE80211_OFDM_RATE_6MB		        0x0C
#define IEEE80211_OFDM_RATE_9MB		        0x12
#define IEEE80211_OFDM_RATE_12MB		0x18
#define IEEE80211_OFDM_RATE_18MB		0x24
#define IEEE80211_OFDM_RATE_24MB		0x30
#define IEEE80211_OFDM_RATE_36MB		0x48
#define IEEE80211_OFDM_RATE_48MB		0x60
#define IEEE80211_OFDM_RATE_54MB		0x6C
#define IEEE80211_BASIC_RATE_MASK		0x80

#define IEEE80211_CCK_RATE_1MB_MASK		(1<<0)
#define IEEE80211_CCK_RATE_2MB_MASK		(1<<1)
#define IEEE80211_CCK_RATE_5MB_MASK		(1<<2)
#define IEEE80211_CCK_RATE_11MB_MASK		(1<<3)
#define IEEE80211_OFDM_RATE_6MB_MASK		(1<<4)
#define IEEE80211_OFDM_RATE_9MB_MASK		(1<<5)
#define IEEE80211_OFDM_RATE_12MB_MASK		(1<<6)
#define IEEE80211_OFDM_RATE_18MB_MASK		(1<<7)
#define IEEE80211_OFDM_RATE_24MB_MASK		(1<<8)
#define IEEE80211_OFDM_RATE_36MB_MASK		(1<<9)
#define IEEE80211_OFDM_RATE_48MB_MASK		(1<<10)
#define IEEE80211_OFDM_RATE_54MB_MASK		(1<<11)

#define IEEE80211_CCK_RATES_MASK	  0x0000000F
#define IEEE80211_CCK_BASIC_RATES_MASK	 (IEEE80211_CCK_RATE_1MB_MASK | \
	IEEE80211_CCK_RATE_2MB_MASK)
#define IEEE80211_CCK_DEFAULT_RATES_MASK (IEEE80211_CCK_BASIC_RATES_MASK | \
	IEEE80211_CCK_RATE_5MB_MASK | \
	IEEE80211_CCK_RATE_11MB_MASK)

#define IEEE80211_OFDM_RATES_MASK	  0x00000FF0
#define IEEE80211_OFDM_BASIC_RATES_MASK	  (IEEE80211_OFDM_RATE_6MB_MASK | \
	IEEE80211_OFDM_RATE_12MB_MASK | \
	IEEE80211_OFDM_RATE_24MB_MASK)
#define IEEE80211_OFDM_DEFAULT_RATES_MASK (IEEE80211_OFDM_BASIC_RATES_MASK | \
	IEEE80211_OFDM_RATE_9MB_MASK  | \
	IEEE80211_OFDM_RATE_18MB_MASK | \
	IEEE80211_OFDM_RATE_36MB_MASK | \
	IEEE80211_OFDM_RATE_48MB_MASK | \
	IEEE80211_OFDM_RATE_54MB_MASK)
#define IEEE80211_DEFAULT_RATES_MASK (IEEE80211_OFDM_DEFAULT_RATES_MASK | \
				      IEEE80211_CCK_DEFAULT_RATES_MASK)


/* This really should be 8, but not for our firmware */
#define MAX_SUPPORTED_RATES 32
#define MAX_COUNTRY_TRIPLETS 32

/* Headers */
struct ieee80211_header {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 da[ETH_ALEN];
	u8 sa[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	__le16 seq_ctl;
	u8 payload[0];
} __packed;

struct wl12xx_ie_header {
	u8 id;
	u8 len;
} __packed;

/* IEs */

struct wl12xx_ie_ssid {
	struct wl12xx_ie_header header;
	char ssid[IEEE80211_MAX_SSID_LEN];
} __packed;

struct wl12xx_ie_rates {
	struct wl12xx_ie_header header;
	u8 rates[MAX_SUPPORTED_RATES];
} __packed;

struct wl12xx_ie_ds_params {
	struct wl12xx_ie_header header;
	u8 channel;
} __packed;

struct country_triplet {
	u8 channel;
	u8 num_channels;
	u8 max_tx_power;
} __packed;

struct wl12xx_ie_country {
	struct wl12xx_ie_header header;
	u8 country_string[IEEE80211_COUNTRY_STRING_LEN];
	struct country_triplet triplets[MAX_COUNTRY_TRIPLETS];
} __packed;


/* Templates */

struct wl12xx_null_data_template {
	struct ieee80211_header header;
} __packed;

struct wl12xx_ps_poll_template {
	__le16 fc;
	__le16 aid;
	u8 bssid[ETH_ALEN];
	u8 ta[ETH_ALEN];
} __packed;

struct wl12xx_arp_rsp_template {
	struct ieee80211_hdr_3addr hdr;

	u8 llc_hdr[sizeof(rfc1042_header)];
	__be16 llc_type;

	struct arphdr arp_hdr;
	u8 sender_hw[ETH_ALEN];
	__be32 sender_ip;
	u8 target_hw[ETH_ALEN];
	__be32 target_ip;
} __packed;

struct wl12xx_disconn_template {
	struct ieee80211_header header;
	__le16 disconn_reason;
} __packed;

#endif
