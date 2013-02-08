/*
 * Copyright (c) 2012 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef WL_CFGP2P_H_
#define WL_CFGP2P_H_

#include <net/cfg80211.h>

struct brcmf_cfg80211_info;

/* vendor ies max buffer length for probe response or beacon */
#define VNDR_IES_MAX_BUF_LEN 1400
/* normal vendor ies buffer length */
#define VNDR_IES_BUF_LEN 512

/* Structure to hold all saved P2P and WPS IEs for a BSSCFG */
/**
 * enum p2p_bss_type - different type of BSS configurations.
 *
 * @P2PAPI_BSSCFG_PRIMARY: maps to driver's primary bsscfg.
 * @P2PAPI_BSSCFG_DEVICE: maps to driver's P2P device discovery bsscfg.
 * @P2PAPI_BSSCFG_CONNECTION: maps to driver's P2P connection bsscfg.
 * @P2PAPI_BSSCFG_MAX: used for range checking.
 */
enum p2p_bss_type {
	P2PAPI_BSSCFG_PRIMARY, /* maps to driver's primary bsscfg */
	P2PAPI_BSSCFG_DEVICE, /* maps to driver's P2P device discovery bsscfg */
	P2PAPI_BSSCFG_CONNECTION, /* maps to driver's P2P connection bsscfg */
	P2PAPI_BSSCFG_MAX
};

/**
 * struct p2p_bss - peer-to-peer bss related information.
 *
 * @vif: virtual interface of this P2P bss.
 * @private_data: TBD
 */
struct p2p_bss {
	struct brcmf_cfg80211_vif *vif;
	void *private_data;
};

/**
 * enum brcmf_p2p_status - P2P specific dongle status.
 *
 * @BRCMF_P2P_STATUS_IF_ADD: peer-to-peer vif add sent to dongle.
 * @BRCMF_P2P_STATUS_IF_DEL: NOT-USED?
 * @BRCMF_P2P_STATUS_IF_DELETING: peer-to-peer vif delete sent to dongle.
 * @BRCMF_P2P_STATUS_IF_CHANGING: peer-to-peer vif change sent to dongle.
 * @BRCMF_P2P_STATUS_IF_CHANGED: peer-to-peer vif change completed on dongle.
 * @BRCMF_P2P_STATUS_ACTION_TX_COMPLETED: action frame tx completed.
 * @BRCMF_P2P_STATUS_ACTION_TX_NOACK: action frame tx not acked.
 * @BRCMF_P2P_STATUS_GO_NEG_PHASE: P2P GO negotiation ongoing.
 * @BRCMF_P2P_STATUS_REMAIN_ON_CHANNEL: P2P listen, remaining on channel.
 */
enum brcmf_p2p_status {
	BRCMF_P2P_STATUS_IF_ADD = 0,
	BRCMF_P2P_STATUS_IF_DEL,
	BRCMF_P2P_STATUS_IF_DELETING,
	BRCMF_P2P_STATUS_IF_CHANGING,
	BRCMF_P2P_STATUS_IF_CHANGED,
	BRCMF_P2P_STATUS_ACTION_TX_COMPLETED,
	BRCMF_P2P_STATUS_ACTION_TX_NOACK,
	BRCMF_P2P_STATUS_GO_NEG_PHASE,
	BRCMF_P2P_STATUS_REMAIN_ON_CHANNEL
};

/**
 * struct brcmf_p2p_info - p2p specific driver information.
 *
 * @cfg: driver private data for cfg80211 interface.
 * @status: status of P2P (see enum brcmf_p2p_status).
 * @dev_addr: P2P device address.
 * @int_addr: P2P interface address.
 * @bss_idx: informate for P2P bss types.
 * @listen_timer: timer for @WL_P2P_DISC_ST_LISTEN discover state.
 * @ssid: ssid for P2P GO.
 * @listen_channel: channel for @WL_P2P_DISC_ST_LISTEN discover state.
 * @remain_on_channel: contains copy of struct used by cfg80211.
 */
struct brcmf_p2p_info {
	struct brcmf_cfg80211_info *cfg;
	unsigned long status;
	u8 dev_addr[ETH_ALEN];
	u8 int_addr[ETH_ALEN];
	struct p2p_bss bss_idx[P2PAPI_BSSCFG_MAX];
	struct timer_list listen_timer;
	struct brcmf_ssid ssid;
	u8 listen_channel;
	struct ieee80211_channel remain_on_channel;
};

void brcmf_p2p_attach(struct brcmf_cfg80211_info *cfg);
void brcmf_p2p_detach(struct brcmf_p2p_info *p2p);
struct wireless_dev *brcmf_p2p_add_vif(struct wiphy *wiphy, const char *name,
				       enum nl80211_iftype type, u32 *flags,
				       struct vif_params *params);
int brcmf_p2p_del_vif(struct wiphy *wiphy, struct wireless_dev *wdev);
int brcmf_p2p_ifchange(struct brcmf_cfg80211_info *cfg,
		       enum brcmf_fil_p2p_if_types if_type);
int brcmf_p2p_start_device(struct wiphy *wiphy, struct wireless_dev *wdev);
void brcmf_p2p_stop_device(struct wiphy *wiphy, struct wireless_dev *wdev);
int brcmf_p2p_scan_prep(struct wiphy *wiphy,
			struct cfg80211_scan_request *request,
			struct brcmf_cfg80211_vif *vif);
int brcmf_p2p_remain_on_channel(struct wiphy *wiphy, struct wireless_dev *wdev,
				struct ieee80211_channel *channel,
				unsigned int duration, u64 *cookie);
int brcmf_p2p_notify_listen_complete(struct brcmf_if *ifp,
				     const struct brcmf_event_msg *e,
				     void *data);
void brcmf_p2p_cancel_remain_on_channel(struct brcmf_if *ifp);

#endif /* WL_CFGP2P_H_ */
