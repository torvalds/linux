/*
 * Linux cfgp2p driver
 *
 * Copyright (C) 1999-2011, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: wl_cfg80211.h,v 1.1.4.1.2.8 2011/02/09 01:37:52 Exp $
 */
#ifndef _wl_cfgp2p_h_
#define _wl_cfgp2p_h_
#include <proto/802.11.h>
#include <proto/p2p.h>

extern void
wl_cfgp2p_init_priv(struct wl_priv *wl);
extern s32
wl_cfgp2p_set_firm_p2p(struct wl_priv *wl);
extern s32
wl_cfgp2p_set_p2p_mode(struct wl_priv *wl, u8 mode,
            u32 channel, u16 listen_ms, int bssidx);
extern s32
wl_cfgp2p_ifadd(struct wl_priv *wl, struct ether_addr *mac, u8 if_type,
            chanspec_t chspec);
extern s32
wl_cfgp2p_ifdel(struct wl_priv *wl, struct ether_addr *mac);
extern s32
wl_cfgp2p_ifchange(struct wl_priv *wl, struct ether_addr *mac, u8 if_type, chanspec_t chspec);

extern s32
wl_cfgp2p_ifidx(struct wl_priv *wl, struct ether_addr *mac, s32 *index);

extern s32
wl_cfgp2p_init_discovery(struct wl_priv *wl);
extern s32
wl_cfgp2p_enable_discovery(struct wl_priv *wl, struct net_device *dev, const u8 *ie, u32 ie_len);
extern s32
wl_cfgp2p_disable_discovery(struct wl_priv *wl);
extern s32
wl_cfgp2p_escan(struct wl_priv *wl, struct net_device *dev, u16 active, u32 num_chans,
	u16 *channels,
	s32 search_state, u16 action, u32 bssidx);

extern wpa_ie_fixed_t *
wl_cfgp2p_find_wpaie(u8 *parse, u32 len);

extern wpa_ie_fixed_t *
wl_cfgp2p_find_wpsie(u8 *parse, u32 len);

extern wifi_p2p_ie_t *
wl_cfgp2p_find_p2pie(u8 *parse, u32 len);

extern s32
wl_cfgp2p_set_managment_ie(struct wl_priv *wl, struct net_device *ndev, s32 bssidx,
            s32 pktflag, const u8 *p2p_ie, u32 p2p_ie_len);
extern s32
wl_cfgp2p_clear_management_ie(struct wl_priv *wl, s32 bssidx);

extern s32
wl_cfgp2p_find_idx(struct wl_priv *wl, struct net_device *ndev);


extern s32
wl_cfgp2p_listen_complete(struct wl_priv *wl, struct net_device *ndev,
            const wl_event_msg_t *e, void *data);
extern s32
wl_cfgp2p_discover_listen(struct wl_priv *wl, s32 channel, u32 duration_ms);

extern s32
wl_cfgp2p_discover_enable_search(struct wl_priv *wl, u8 search_enable);

extern s32
wl_cfgp2p_action_tx_complete(struct wl_priv *wl, struct net_device *ndev,
            const wl_event_msg_t *e, void *data);
extern s32
wl_cfgp2p_tx_action_frame(struct wl_priv *wl, struct net_device *dev,
	wl_af_params_t *af_params, s32 bssidx);

extern void
wl_cfgp2p_generate_bss_mac(struct ether_addr *primary_addr, struct ether_addr *out_dev_addr,
            struct ether_addr *out_int_addr);

extern void
wl_cfg80211_change_ifaddr(u8* buf, struct ether_addr *p2p_int_addr, u8 element_id);
extern bool
wl_cfgp2p_bss_isup(struct net_device *ndev, int bsscfg_idx);

extern s32
wl_cfgp2p_bss(struct net_device *ndev, s32 bsscfg_idx, s32 up);


extern s32
wl_cfgp2p_is_p2p_supported(struct wl_priv *wl, struct net_device *ndev);

extern s32
wl_cfgp2p_down(struct wl_priv *wl);

/* WiFi Direct */
#define SOCIAL_CHAN_1 1
#define SOCIAL_CHAN_2 6
#define SOCIAL_CHAN_3 11
#define WL_P2P_WILDCARD_SSID "DIRECT-"
#define WL_P2P_WILDCARD_SSID_LEN 7
#define IS_P2P_SSID(ssid) (memcmp(ssid, WL_P2P_WILDCARD_SSID, WL_P2P_WILDCARD_SSID_LEN) == 0)
#endif				/* _wl_cfgp2p_h_ */
