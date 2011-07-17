/*
 * Copyright (c) 2010-2011 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef COMMON_H
#define COMMON_H

#include <linux/netdevice.h>

#define ATH6KL_MAX_IE			256

extern int ath6kl_printk(const char *level, const char *fmt, ...);

#define A_CACHE_LINE_PAD            128

/*
 * Reflects the version of binary interface exposed by ATH6KL target
 * firmware. Needs to be incremented by 1 for any change in the firmware
 * that requires upgrade of the driver on the host side for the change to
 * work correctly
 */
#define ATH6KL_ABI_VERSION        1

#define SIGNAL_QUALITY_METRICS_NUM_MAX    2

enum {
	SIGNAL_QUALITY_METRICS_SNR = 0,
	SIGNAL_QUALITY_METRICS_RSSI,
	SIGNAL_QUALITY_METRICS_ALL,
};

/*
 * Data Path
 */

#define WMI_MAX_TX_DATA_FRAME_LENGTH	      \
	(1500 + sizeof(struct wmi_data_hdr) + \
	 sizeof(struct ethhdr) +      \
	 sizeof(struct ath6kl_llc_snap_hdr))

/* An AMSDU frame */ /* The MAX AMSDU length of AR6003 is 3839 */
#define WMI_MAX_AMSDU_RX_DATA_FRAME_LENGTH    \
	(3840 + sizeof(struct wmi_data_hdr) + \
	 sizeof(struct ethhdr) +      \
	 sizeof(struct ath6kl_llc_snap_hdr))

#define EPPING_ALIGNMENT_PAD			       \
	(((sizeof(struct htc_frame_hdr) + 3) & (~0x3)) \
	 - sizeof(struct htc_frame_hdr))

struct ath6kl_llc_snap_hdr {
	u8 dsap;
	u8 ssap;
	u8 cntl;
	u8 org_code[3];
	__be16 eth_type;
} __packed;

enum crypto_type {
	NONE_CRYPT          = 0x01,
	WEP_CRYPT           = 0x02,
	TKIP_CRYPT          = 0x04,
	AES_CRYPT           = 0x08,
};

#define ATH6KL_NODE_HASHSIZE 32
/* simple hash is enough for variation of macaddr */
#define ATH6KL_NODE_HASH(addr)   \
	(((const u8 *)(addr))[ETH_ALEN - 1] % \
	 ATH6KL_NODE_HASHSIZE)

/*
 * Table of ath6kl_node instances.  Each ieee80211com
 * has at least one for holding the scan candidates.
 * When operating as an access point or in ibss mode there
 * is a second table for associated stations or neighbors.
 */
struct ath6kl_node_table {
	void *nt_wmi;		/* back reference */
	spinlock_t nt_nodelock;	/* on node table */
	struct bss *nt_node_first;	/* information of all nodes */
	struct bss *nt_node_last;	/* information of all nodes */
	struct bss *nt_hash[ATH6KL_NODE_HASHSIZE];
	const char *nt_name;	/* for debugging */
	u32 nt_node_age;		/* node aging time */
};

#define WLAN_NODE_INACT_TIMEOUT_MSEC    120000
#define WLAN_NODE_INACT_CNT		4

struct ath6kl_common_ie {
	u16 ie_chan;
	u8 *ie_tstamp;
	u8 *ie_ssid;
	u8 *ie_rates;
	u8 *ie_xrates;
	u8 *ie_country;
	u8 *ie_wpa;
	u8 *ie_rsn;
	u8 *ie_wmm;
	u8 *ie_ath;
	u16 ie_capInfo;
	u16 ie_beaconInt;
	u8 *ie_tim;
	u8 *ie_chswitch;
	u8 ie_erp;
	u8 *ie_wsc;
	u8 *ie_htcap;
	u8 *ie_htop;
};

struct bss {
	u8 ni_macaddr[ETH_ALEN];
	u8 ni_snr;
	s16 ni_rssi;
	struct bss *ni_list_next;
	struct bss *ni_list_prev;
	struct bss *ni_hash_next;
	struct bss *ni_hash_prev;
	struct ath6kl_common_ie ni_cie;
	u8 *ni_buf;
	u16 ni_framelen;
	struct ath6kl_node_table *ni_table;
	u32 ni_refcnt;

	u32 ni_tstamp;
	u32 ni_actcnt;
};

struct htc_endpoint_credit_dist;
struct ath6kl;
enum htc_credit_dist_reason;
struct htc_credit_state_info;

struct bss *wlan_node_alloc(int wh_size);
void wlan_node_free(struct bss *ni);
void wlan_setup_node(struct ath6kl_node_table *nt, struct bss *ni,
		     const u8 *mac_addr);
struct bss *wlan_find_node(struct ath6kl_node_table *nt,
			   const u8 *mac_addr);
void wlan_node_reclaim(struct ath6kl_node_table *nt, struct bss *ni);
void wlan_free_allnodes(struct ath6kl_node_table *nt);
void wlan_iterate_nodes(struct ath6kl_node_table *nt,
			void (*f) (void *arg, struct bss *),
			void *arg);

void wlan_node_table_init(void *wmip, struct ath6kl_node_table *nt);
void wlan_node_table_cleanup(struct ath6kl_node_table *nt);

void wlan_refresh_inactive_nodes(struct ath6kl_node_table *nt);

struct bss *wlan_find_ssid_node(struct ath6kl_node_table *nt, u8 *ssid,
				  u32 ssid_len, bool is_wpa2, bool match_ssid);

void wlan_node_return(struct ath6kl_node_table *nt, struct bss *ni);

int ath6k_setup_credit_dist(void *htc_handle,
			    struct htc_credit_state_info *cred_info);
void ath6k_credit_distribute(struct htc_credit_state_info *cred_inf,
			     struct list_head *epdist_list,
			     enum htc_credit_dist_reason reason);
void ath6k_credit_init(struct htc_credit_state_info *cred_inf,
		       struct list_head *ep_list,
		       int tot_credits);
void ath6k_seek_credits(struct htc_credit_state_info *cred_inf,
			struct htc_endpoint_credit_dist *ep_dist);
struct ath6kl *ath6kl_core_alloc(struct device *sdev);
int ath6kl_core_init(struct ath6kl *ar);
int ath6kl_unavail_ev(struct ath6kl *ar);
struct sk_buff *ath6kl_buf_alloc(int size);
#endif /* COMMON_H */
