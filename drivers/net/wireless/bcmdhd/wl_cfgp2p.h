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
 * $Id: wl_cfgp2p.h,v 1.1.4.1.2.8 2011/02/09 01:37:52 Exp $
 */
#ifndef _wl_cfgp2p_h_
#define _wl_cfgp2p_h_
#include <proto/802.11.h>
#include <proto/p2p.h>

struct wl_priv;
extern u32 wl_dbg_level;

/* Enumeration of the usages of the BSSCFGs used by the P2P Library.  Do not
 * confuse this with a bsscfg index.  This value is an index into the
 * saved_ie[] array of structures which in turn contains a bsscfg index field.
 */
typedef enum {
	P2PAPI_BSSCFG_PRIMARY, /* maps to driver's primary bsscfg */
	P2PAPI_BSSCFG_DEVICE, /* maps to driver's P2P device discovery bsscfg */
	P2PAPI_BSSCFG_CONNECTION, /* maps to driver's P2P connection bsscfg */
	P2PAPI_BSSCFG_MAX
} p2p_bsscfg_type_t;

#define IE_MAX_LEN 512
/* Structure to hold all saved P2P and WPS IEs for a BSSCFG */
struct p2p_saved_ie {
	u8  p2p_probe_req_ie[IE_MAX_LEN];
	u8  p2p_probe_res_ie[IE_MAX_LEN];
	u8  p2p_assoc_req_ie[IE_MAX_LEN];
	u8  p2p_assoc_res_ie[IE_MAX_LEN];
	u8  p2p_beacon_ie[IE_MAX_LEN];
	u32 p2p_probe_req_ie_len;
	u32 p2p_probe_res_ie_len;
	u32 p2p_assoc_req_ie_len;
	u32 p2p_assoc_res_ie_len;
	u32 p2p_beacon_ie_len;
};

struct p2p_bss {
	u32 bssidx;
	struct net_device *dev;
	struct p2p_saved_ie saved_ie;
	void *private_data;
};

struct p2p_info {
	bool on;    /* p2p on/off switch */
	bool scan;
	bool vif_created;
	s8 vir_ifname[IFNAMSIZ];
	unsigned long status;
	struct ether_addr dev_addr;
	struct ether_addr int_addr;
	struct p2p_bss bss_idx[P2PAPI_BSSCFG_MAX];
	struct timer_list listen_timer;
	wl_p2p_sched_t noa;
	wl_p2p_ops_t ops;
	wlc_ssid_t ssid;
};

/* dongle status */
enum wl_cfgp2p_status {
	WLP2P_STATUS_DISCOVERY_ON = 0,
	WLP2P_STATUS_SEARCH_ENABLED,
	WLP2P_STATUS_IF_ADD,
	WLP2P_STATUS_IF_DEL,
	WLP2P_STATUS_IF_DELETING,
	WLP2P_STATUS_IF_CHANGING,
	WLP2P_STATUS_IF_CHANGED,
	WLP2P_STATUS_LISTEN_EXPIRED,
	WLP2P_STATUS_ACTION_TX_COMPLETED,
	WLP2P_STATUS_ACTION_TX_NOACK,
	WLP2P_STATUS_SCANNING,
	WLP2P_STATUS_GO_NEG_PHASE
};


#define wl_to_p2p_bss_ndev(w, type) 	((wl)->p2p->bss_idx[type].dev)
#define wl_to_p2p_bss_bssidx(w, type) 	((wl)->p2p->bss_idx[type].bssidx)
#define wl_to_p2p_bss_saved_ie(w, type) 	((wl)->p2p->bss_idx[type].saved_ie)
#define wl_to_p2p_bss_private(w, type) 	((wl)->p2p->bss_idx[type].private_data)
#define wl_to_p2p_bss(wl, type) ((wl)->p2p->bss_idx[type])
#define wl_get_p2p_status(wl, stat) ((!(wl)->p2p_supported) ? 0:test_bit(WLP2P_STATUS_ ## stat, \
									&(wl)->p2p->status))
#define wl_set_p2p_status(wl, stat) ((!(wl)->p2p_supported) ? 0:set_bit(WLP2P_STATUS_ ## stat, \
									&(wl)->p2p->status))
#define wl_clr_p2p_status(wl, stat) ((!(wl)->p2p_supported) ? 0:clear_bit(WLP2P_STATUS_ ## stat, \
									&(wl)->p2p->status))
#define wl_chg_p2p_status(wl, stat) ((!(wl)->p2p_supported) ? 0:change_bit(WLP2P_STATUS_ ## stat, \
									&(wl)->p2p->status))
#define p2p_on(wl) ((wl)->p2p->on)
#define p2p_scan(wl) ((wl)->p2p->scan)
#define p2p_is_on(wl) ((wl)->p2p && (wl)->p2p->on)

/* dword align allocation */
#define WLC_IOCTL_MAXLEN 8192
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"

#define CFGP2P_ERR(args)									\
	do {										\
		if (wl_dbg_level & WL_DBG_ERR) {				\
			printk(KERN_ERR "CFGP2P-ERROR) %s : ", __func__);	\
			printk args;						\
		}									\
	} while (0)
#define	CFGP2P_INFO(args)									\
	do {										\
		if (wl_dbg_level & WL_DBG_INFO) {				\
			printk(KERN_ERR "CFGP2P-INFO) %s : ", __func__);	\
			printk args;						\
		}									\
	} while (0)
#define	CFGP2P_DBG(args)								\
	do {									\
		if (wl_dbg_level & WL_DBG_DBG) {			\
			printk(KERN_ERR "CFGP2P-DEBUG) %s :", __func__);	\
			printk args;							\
		}									\
	} while (0)

extern bool
wl_cfgp2p_is_pub_action(void *frame, u32 frame_len);
extern bool
wl_cfgp2p_is_p2p_action(void *frame, u32 frame_len);
extern bool
wl_cfgp2p_is_gas_action(void *frame, u32 frame_len);
extern void
wl_cfgp2p_print_actframe(bool tx, void *frame, u32 frame_len);
extern s32
wl_cfgp2p_init_priv(struct wl_priv *wl);
extern void
wl_cfgp2p_deinit_priv(struct wl_priv *wl);
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

extern s32
wl_cfgp2p_act_frm_search(struct wl_priv *wl, struct net_device *ndev,
	s32 bssidx, s32 channel);

extern wpa_ie_fixed_t *
wl_cfgp2p_find_wpaie(u8 *parse, u32 len);

extern wpa_ie_fixed_t *
wl_cfgp2p_find_wpsie(u8 *parse, u32 len);

extern wifi_p2p_ie_t *
wl_cfgp2p_find_p2pie(u8 *parse, u32 len);

extern s32
wl_cfgp2p_set_management_ie(struct wl_priv *wl, struct net_device *ndev, s32 bssidx,
            s32 pktflag, const u8 *vndr_ie, u32 vndr_ie_len);
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
wl_cfgp2p_discover_enable_search(struct wl_priv *wl, u8 enable);

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
wl_cfgp2p_bss(struct wl_priv *wl, struct net_device *ndev, s32 bsscfg_idx, s32 up);


extern s32
wl_cfgp2p_supported(struct wl_priv *wl, struct net_device *ndev);

extern s32
wl_cfgp2p_down(struct wl_priv *wl);

extern s32
wl_cfgp2p_set_p2p_noa(struct wl_priv *wl, struct net_device *ndev, char* buf, int len);

extern s32
wl_cfgp2p_get_p2p_noa(struct wl_priv *wl, struct net_device *ndev, char* buf, int len);

extern s32
wl_cfgp2p_set_p2p_ps(struct wl_priv *wl, struct net_device *ndev, char* buf, int len);

extern u8 *
wl_cfgp2p_retreive_p2pattrib(void *buf, u8 element_id);

extern u8 *
wl_cfgp2p_retreive_p2p_dev_addr(wl_bss_info_t *bi, u32 bi_length);

extern s32
wl_cfgp2p_register_ndev(struct wl_priv *wl);

extern s32
wl_cfgp2p_unregister_ndev(struct wl_priv *wl);

/* WiFi Direct */
#define SOCIAL_CHAN_1 1
#define SOCIAL_CHAN_2 6
#define SOCIAL_CHAN_3 11
#define SOCIAL_CHAN_CNT 3
#define WL_P2P_WILDCARD_SSID "DIRECT-"
#define WL_P2P_WILDCARD_SSID_LEN 7
#define WL_P2P_INTERFACE_PREFIX "p2p"
#define WL_P2P_TEMP_CHAN "11"

/* If the provision discovery is for JOIN operations, then we need not do an internal scan to find GO */
#define IS_PROV_DISC_WITHOUT_GROUP_ID(p2p_ie, len) (wl_cfgp2p_retreive_p2pattrib(p2p_ie, P2P_SEID_GROUP_ID) == NULL )

#define IS_GAS_REQ(frame, len) (wl_cfgp2p_is_gas_action(frame, len) && \
					((frame->action == P2PSD_ACTION_ID_GAS_IREQ) || \
					(frame->action == P2PSD_ACTION_ID_GAS_CREQ)))
#define IS_P2P_PUB_ACT_REQ(frame, p2p_ie, len) (wl_cfgp2p_is_pub_action(frame, len) && \
						((frame->subtype == P2P_PAF_GON_REQ) || \
						(frame->subtype == P2P_PAF_INVITE_REQ) || \
						((frame->subtype == P2P_PAF_PROVDIS_REQ) && IS_PROV_DISC_WITHOUT_GROUP_ID(p2p_ie, len))))
#define IS_P2P_SOCIAL(ch) ((ch == SOCIAL_CHAN_1) || (ch == SOCIAL_CHAN_2) || (ch == SOCIAL_CHAN_3))
#define IS_P2P_SSID(ssid) (memcmp(ssid, WL_P2P_WILDCARD_SSID, WL_P2P_WILDCARD_SSID_LEN) == 0)
#endif				/* _wl_cfgp2p_h_ */
