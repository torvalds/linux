#ifndef HOSTAP_80211_H
#define HOSTAP_80211_H

#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>

struct hostap_ieee80211_mgmt {
	__le16 frame_control;
	__le16 duration;
	u8 da[6];
	u8 sa[6];
	u8 bssid[6];
	__le16 seq_ctrl;
	union {
		struct {
			__le16 auth_alg;
			__le16 auth_transaction;
			__le16 status_code;
			/* possibly followed by Challenge text */
			u8 variable[0];
		} __attribute__ ((packed)) auth;
		struct {
			__le16 reason_code;
		} __attribute__ ((packed)) deauth;
		struct {
			__le16 capab_info;
			__le16 listen_interval;
			/* followed by SSID and Supported rates */
			u8 variable[0];
		} __attribute__ ((packed)) assoc_req;
		struct {
			__le16 capab_info;
			__le16 status_code;
			__le16 aid;
			/* followed by Supported rates */
			u8 variable[0];
		} __attribute__ ((packed)) assoc_resp, reassoc_resp;
		struct {
			__le16 capab_info;
			__le16 listen_interval;
			u8 current_ap[6];
			/* followed by SSID and Supported rates */
			u8 variable[0];
		} __attribute__ ((packed)) reassoc_req;
		struct {
			__le16 reason_code;
		} __attribute__ ((packed)) disassoc;
		struct {
		} __attribute__ ((packed)) probe_req;
		struct {
			u8 timestamp[8];
			__le16 beacon_int;
			__le16 capab_info;
			/* followed by some of SSID, Supported rates,
			 * FH Params, DS Params, CF Params, IBSS Params, TIM */
			u8 variable[0];
		} __attribute__ ((packed)) beacon, probe_resp;
	} u;
} __attribute__ ((packed));


#define IEEE80211_MGMT_HDR_LEN 24
#define IEEE80211_DATA_HDR3_LEN 24
#define IEEE80211_DATA_HDR4_LEN 30


struct hostap_80211_rx_status {
	u32 mac_time;
	u8 signal;
	u8 noise;
	u16 rate; /* in 100 kbps */
};

/* prism2_rx_80211 'type' argument */
enum {
	PRISM2_RX_MONITOR, PRISM2_RX_MGMT, PRISM2_RX_NON_ASSOC,
	PRISM2_RX_NULLFUNC_ACK
};

int prism2_rx_80211(struct net_device *dev, struct sk_buff *skb,
		    struct hostap_80211_rx_status *rx_stats, int type);
void hostap_80211_rx(struct net_device *dev, struct sk_buff *skb,
		     struct hostap_80211_rx_status *rx_stats);
void hostap_dump_rx_80211(const char *name, struct sk_buff *skb,
			  struct hostap_80211_rx_status *rx_stats);

void hostap_dump_tx_80211(const char *name, struct sk_buff *skb);
netdev_tx_t hostap_data_start_xmit(struct sk_buff *skb,
				   struct net_device *dev);
netdev_tx_t hostap_mgmt_start_xmit(struct sk_buff *skb,
				   struct net_device *dev);
netdev_tx_t hostap_master_start_xmit(struct sk_buff *skb,
				     struct net_device *dev);

#endif /* HOSTAP_80211_H */
