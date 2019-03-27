/*
 * BSS table
 * Copyright (c) 2009-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef BSS_H
#define BSS_H

struct wpa_scan_res;

#define WPA_BSS_QUAL_INVALID		BIT(0)
#define WPA_BSS_NOISE_INVALID		BIT(1)
#define WPA_BSS_LEVEL_INVALID		BIT(2)
#define WPA_BSS_LEVEL_DBM		BIT(3)
#define WPA_BSS_AUTHENTICATED		BIT(4)
#define WPA_BSS_ASSOCIATED		BIT(5)
#define WPA_BSS_ANQP_FETCH_TRIED	BIT(6)

struct wpa_bss_anqp_elem {
	struct dl_list list;
	u16 infoid;
	struct wpabuf *payload;
};

/**
 * struct wpa_bss_anqp - ANQP data for a BSS entry (struct wpa_bss)
 */
struct wpa_bss_anqp {
	/** Number of BSS entries referring to this ANQP data instance */
	unsigned int users;
#ifdef CONFIG_INTERWORKING
	struct wpabuf *capability_list;
	struct wpabuf *venue_name;
	struct wpabuf *network_auth_type;
	struct wpabuf *roaming_consortium;
	struct wpabuf *ip_addr_type_availability;
	struct wpabuf *nai_realm;
	struct wpabuf *anqp_3gpp;
	struct wpabuf *domain_name;
	struct wpabuf *fils_realm_info;
	struct dl_list anqp_elems; /* list of struct wpa_bss_anqp_elem */
#endif /* CONFIG_INTERWORKING */
#ifdef CONFIG_HS20
	struct wpabuf *hs20_capability_list;
	struct wpabuf *hs20_operator_friendly_name;
	struct wpabuf *hs20_wan_metrics;
	struct wpabuf *hs20_connection_capability;
	struct wpabuf *hs20_operating_class;
	struct wpabuf *hs20_osu_providers_list;
	struct wpabuf *hs20_operator_icon_metadata;
	struct wpabuf *hs20_osu_providers_nai_list;
#endif /* CONFIG_HS20 */
};

/**
 * struct wpa_bss - BSS table
 *
 * This structure is used to store information about neighboring BSSes in
 * generic format. It is mainly updated based on scan results from the driver.
 */
struct wpa_bss {
	/** List entry for struct wpa_supplicant::bss */
	struct dl_list list;
	/** List entry for struct wpa_supplicant::bss_id */
	struct dl_list list_id;
	/** Unique identifier for this BSS entry */
	unsigned int id;
	/** Number of counts without seeing this BSS */
	unsigned int scan_miss_count;
	/** Index of the last scan update */
	unsigned int last_update_idx;
	/** Information flags about the BSS/IBSS (WPA_BSS_*) */
	unsigned int flags;
	/** BSSID */
	u8 bssid[ETH_ALEN];
	/** HESSID */
	u8 hessid[ETH_ALEN];
	/** SSID */
	u8 ssid[SSID_MAX_LEN];
	/** Length of SSID */
	size_t ssid_len;
	/** Frequency of the channel in MHz (e.g., 2412 = channel 1) */
	int freq;
	/** Beacon interval in TUs (host byte order) */
	u16 beacon_int;
	/** Capability information field in host byte order */
	u16 caps;
	/** Signal quality */
	int qual;
	/** Noise level */
	int noise;
	/** Signal level */
	int level;
	/** Timestamp of last Beacon/Probe Response frame */
	u64 tsf;
	/** Time of the last update (i.e., Beacon or Probe Response RX) */
	struct os_reltime last_update;
	/** Estimated throughput in kbps */
	unsigned int est_throughput;
	/** Signal-to-noise ratio in dB */
	int snr;
	/** ANQP data */
	struct wpa_bss_anqp *anqp;
	/** Length of the following IE field in octets (from Probe Response) */
	size_t ie_len;
	/** Length of the following Beacon IE field in octets */
	size_t beacon_ie_len;
	/* followed by ie_len octets of IEs */
	/* followed by beacon_ie_len octets of IEs */
};

void wpa_bss_update_start(struct wpa_supplicant *wpa_s);
void wpa_bss_update_scan_res(struct wpa_supplicant *wpa_s,
			     struct wpa_scan_res *res,
			     struct os_reltime *fetch_time);
void wpa_bss_remove(struct wpa_supplicant *wpa_s, struct wpa_bss *bss,
		    const char *reason);
void wpa_bss_update_end(struct wpa_supplicant *wpa_s, struct scan_info *info,
			int new_scan);
int wpa_bss_init(struct wpa_supplicant *wpa_s);
void wpa_bss_deinit(struct wpa_supplicant *wpa_s);
void wpa_bss_flush(struct wpa_supplicant *wpa_s);
void wpa_bss_flush_by_age(struct wpa_supplicant *wpa_s, int age);
struct wpa_bss * wpa_bss_get(struct wpa_supplicant *wpa_s, const u8 *bssid,
			     const u8 *ssid, size_t ssid_len);
struct wpa_bss * wpa_bss_get_bssid(struct wpa_supplicant *wpa_s,
				   const u8 *bssid);
struct wpa_bss * wpa_bss_get_bssid_latest(struct wpa_supplicant *wpa_s,
					  const u8 *bssid);
struct wpa_bss * wpa_bss_get_p2p_dev_addr(struct wpa_supplicant *wpa_s,
					  const u8 *dev_addr);
struct wpa_bss * wpa_bss_get_id(struct wpa_supplicant *wpa_s, unsigned int id);
struct wpa_bss * wpa_bss_get_id_range(struct wpa_supplicant *wpa_s,
				      unsigned int idf, unsigned int idl);
const u8 * wpa_bss_get_ie(const struct wpa_bss *bss, u8 ie);
const u8 * wpa_bss_get_vendor_ie(const struct wpa_bss *bss, u32 vendor_type);
const u8 * wpa_bss_get_vendor_ie_beacon(const struct wpa_bss *bss,
					u32 vendor_type);
struct wpabuf * wpa_bss_get_vendor_ie_multi(const struct wpa_bss *bss,
					    u32 vendor_type);
struct wpabuf * wpa_bss_get_vendor_ie_multi_beacon(const struct wpa_bss *bss,
						   u32 vendor_type);
int wpa_bss_get_max_rate(const struct wpa_bss *bss);
int wpa_bss_get_bit_rates(const struct wpa_bss *bss, u8 **rates);
struct wpa_bss_anqp * wpa_bss_anqp_alloc(void);
int wpa_bss_anqp_unshare_alloc(struct wpa_bss *bss);
const u8 * wpa_bss_get_fils_cache_id(struct wpa_bss *bss);

static inline int bss_is_dmg(const struct wpa_bss *bss)
{
	return bss->freq > 45000;
}

/**
 * Test whether a BSS is a PBSS.
 * This checks whether a BSS is a DMG-band PBSS. PBSS is used for P2P DMG
 * network.
 */
static inline int bss_is_pbss(struct wpa_bss *bss)
{
	return bss_is_dmg(bss) &&
		(bss->caps & IEEE80211_CAP_DMG_MASK) == IEEE80211_CAP_DMG_PBSS;
}

static inline void wpa_bss_update_level(struct wpa_bss *bss, int new_level)
{
	if (bss != NULL && new_level < 0)
		bss->level = new_level;
}

void calculate_update_time(const struct os_reltime *fetch_time,
			   unsigned int age_ms,
			   struct os_reltime *update_time);

#endif /* BSS_H */
