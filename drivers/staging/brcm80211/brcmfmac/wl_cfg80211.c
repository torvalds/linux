/*
 * Copyright (c) 2010 Broadcom Corporation
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

#include <linux/kernel.h>
#include <linux/if_arp.h>
#include <osl.h>

#include <bcmutils.h>

#include <asm/uaccess.h>

#include <dngl_stats.h>
#include <dhd.h>
#include <dhdioctl.h>
#include <wlioctl.h>

#include <linux/kthread.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>

#include <net/rtnetlink.h>
#include <linux/mmc/sdio_func.h>
#include <linux/firmware.h>
#include <wl_cfg80211.h>

static struct sdio_func *cfg80211_sdio_func;
static struct wl_dev *wl_cfg80211_dev;
static const u8 ether_bcast[ETH_ALEN] = {255, 255, 255, 255, 255, 255};

u32 wl_dbg_level = WL_DBG_ERR | WL_DBG_INFO;

#define WL_4329_FW_FILE "brcm/bcm4329-fullmac-4.bin"
#define WL_4329_NVRAM_FILE "brcm/bcm4329-fullmac-4.txt"

/*
** cfg80211_ops api/callback list
*/
static s32 wl_cfg80211_change_iface(struct wiphy *wiphy,
				      struct net_device *ndev,
				      enum nl80211_iftype type, u32 *flags,
				      struct vif_params *params);
static s32 __wl_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
				struct cfg80211_scan_request *request,
				struct cfg80211_ssid *this_ssid);
static s32 wl_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
			      struct cfg80211_scan_request *request);
static s32 wl_cfg80211_set_wiphy_params(struct wiphy *wiphy, u32 changed);
static s32 wl_cfg80211_join_ibss(struct wiphy *wiphy, struct net_device *dev,
				   struct cfg80211_ibss_params *params);
static s32 wl_cfg80211_leave_ibss(struct wiphy *wiphy,
				    struct net_device *dev);
static s32 wl_cfg80211_get_station(struct wiphy *wiphy,
				     struct net_device *dev, u8 *mac,
				     struct station_info *sinfo);
static s32 wl_cfg80211_set_power_mgmt(struct wiphy *wiphy,
					struct net_device *dev, bool enabled,
					s32 timeout);
static s32 wl_cfg80211_set_bitrate_mask(struct wiphy *wiphy,
					  struct net_device *dev,
					  const u8 *addr,
					  const struct cfg80211_bitrate_mask
					  *mask);
static int wl_cfg80211_connect(struct wiphy *wiphy, struct net_device *dev,
			       struct cfg80211_connect_params *sme);
static s32 wl_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *dev,
				    u16 reason_code);
static s32 wl_cfg80211_set_tx_power(struct wiphy *wiphy,
				      enum nl80211_tx_power_setting type,
				      s32 dbm);
static s32 wl_cfg80211_get_tx_power(struct wiphy *wiphy, s32 *dbm);
static s32 wl_cfg80211_config_default_key(struct wiphy *wiphy,
					  struct net_device *dev, u8 key_idx,
					  bool unicast, bool multicast);
static s32 wl_cfg80211_add_key(struct wiphy *wiphy, struct net_device *dev,
				 u8 key_idx, bool pairwise, const u8 *mac_addr,
				 struct key_params *params);
static s32 wl_cfg80211_del_key(struct wiphy *wiphy, struct net_device *dev,
				 u8 key_idx, bool pairwise, const u8 *mac_addr);
static s32 wl_cfg80211_get_key(struct wiphy *wiphy, struct net_device *dev,
				 u8 key_idx, bool pairwise, const u8 *mac_addr,
				 void *cookie, void (*callback) (void *cookie,
								 struct
								 key_params *
								 params));
static s32 wl_cfg80211_config_default_mgmt_key(struct wiphy *wiphy,
						 struct net_device *dev,
						 u8 key_idx);
static s32 wl_cfg80211_resume(struct wiphy *wiphy);
static s32 wl_cfg80211_suspend(struct wiphy *wiphy);
static s32 wl_cfg80211_set_pmksa(struct wiphy *wiphy, struct net_device *dev,
				   struct cfg80211_pmksa *pmksa);
static s32 wl_cfg80211_del_pmksa(struct wiphy *wiphy, struct net_device *dev,
				   struct cfg80211_pmksa *pmksa);
static s32 wl_cfg80211_flush_pmksa(struct wiphy *wiphy,
				     struct net_device *dev);
/*
** event & event Q handlers for cfg80211 interfaces
*/
static s32 wl_create_event_handler(struct wl_priv *wl);
static void wl_destroy_event_handler(struct wl_priv *wl);
static s32 wl_event_handler(void *data);
static void wl_init_eq(struct wl_priv *wl);
static void wl_flush_eq(struct wl_priv *wl);
static void wl_lock_eq(struct wl_priv *wl);
static void wl_unlock_eq(struct wl_priv *wl);
static void wl_init_eq_lock(struct wl_priv *wl);
static void wl_init_eloop_handler(struct wl_event_loop *el);
static struct wl_event_q *wl_deq_event(struct wl_priv *wl);
static s32 wl_enq_event(struct wl_priv *wl, u32 type,
			  const wl_event_msg_t *msg, void *data);
static void wl_put_event(struct wl_event_q *e);
static void wl_wakeup_event(struct wl_priv *wl);
static s32 wl_notify_connect_status(struct wl_priv *wl,
				      struct net_device *ndev,
				      const wl_event_msg_t *e, void *data);
static s32 wl_notify_roaming_status(struct wl_priv *wl,
				      struct net_device *ndev,
				      const wl_event_msg_t *e, void *data);
static s32 wl_notify_scan_status(struct wl_priv *wl, struct net_device *ndev,
				   const wl_event_msg_t *e, void *data);
static s32 wl_bss_connect_done(struct wl_priv *wl, struct net_device *ndev,
				 const wl_event_msg_t *e, void *data,
				bool completed);
static s32 wl_bss_roaming_done(struct wl_priv *wl, struct net_device *ndev,
				 const wl_event_msg_t *e, void *data);
static s32 wl_notify_mic_status(struct wl_priv *wl, struct net_device *ndev,
				  const wl_event_msg_t *e, void *data);

/*
** register/deregister sdio function
*/
struct sdio_func *wl_cfg80211_get_sdio_func(void);
static void wl_clear_sdio_func(void);

/*
** ioctl utilites
*/
static s32 wl_dev_bufvar_get(struct net_device *dev, s8 *name, s8 *buf,
			       s32 buf_len);
static __used s32 wl_dev_bufvar_set(struct net_device *dev, s8 *name,
				      s8 *buf, s32 len);
static s32 wl_dev_intvar_set(struct net_device *dev, s8 *name, s32 val);
static s32 wl_dev_intvar_get(struct net_device *dev, s8 *name,
			       s32 *retval);
static s32 wl_dev_ioctl(struct net_device *dev, u32 cmd, void *arg,
			  u32 len);

/*
** cfg80211 set_wiphy_params utilities
*/
static s32 wl_set_frag(struct net_device *dev, u32 frag_threshold);
static s32 wl_set_rts(struct net_device *dev, u32 frag_threshold);
static s32 wl_set_retry(struct net_device *dev, u32 retry, bool l);

/*
** wl profile utilities
*/
static s32 wl_update_prof(struct wl_priv *wl, const wl_event_msg_t *e,
			    void *data, s32 item);
static void *wl_read_prof(struct wl_priv *wl, s32 item);
static void wl_init_prof(struct wl_profile *prof);

/*
** cfg80211 connect utilites
*/
static s32 wl_set_wpa_version(struct net_device *dev,
				struct cfg80211_connect_params *sme);
static s32 wl_set_auth_type(struct net_device *dev,
			      struct cfg80211_connect_params *sme);
static s32 wl_set_set_cipher(struct net_device *dev,
			       struct cfg80211_connect_params *sme);
static s32 wl_set_key_mgmt(struct net_device *dev,
			     struct cfg80211_connect_params *sme);
static s32 wl_set_set_sharedkey(struct net_device *dev,
				  struct cfg80211_connect_params *sme);
static s32 wl_get_assoc_ies(struct wl_priv *wl);
static void wl_ch_to_chanspec(int ch,
	struct wl_join_params *join_params, size_t *join_params_size);

/*
** information element utilities
*/
static void wl_rst_ie(struct wl_priv *wl);
static __used s32 wl_add_ie(struct wl_priv *wl, u8 t, u8 l, u8 *v);
static s32 wl_mrg_ie(struct wl_priv *wl, u8 *ie_stream, u16 ie_size);
static s32 wl_cp_ie(struct wl_priv *wl, u8 *dst, u16 dst_size);
static u32 wl_get_ielen(struct wl_priv *wl);

static s32 wl_mode_to_nl80211_iftype(s32 mode);

static struct wireless_dev *wl_alloc_wdev(s32 sizeof_iface,
					  struct device *dev);
static void wl_free_wdev(struct wl_priv *wl);

static s32 wl_inform_bss(struct wl_priv *wl);
static s32 wl_inform_single_bss(struct wl_priv *wl, struct wl_bss_info *bi);
static s32 wl_update_bss_info(struct wl_priv *wl);

static s32 wl_add_keyext(struct wiphy *wiphy, struct net_device *dev,
			   u8 key_idx, const u8 *mac_addr,
			   struct key_params *params);

/*
** key indianess swap utilities
*/
static void swap_key_from_BE(struct wl_wsec_key *key);
static void swap_key_to_BE(struct wl_wsec_key *key);

/*
** wl_priv memory init/deinit utilities
*/
static s32 wl_init_priv_mem(struct wl_priv *wl);
static void wl_deinit_priv_mem(struct wl_priv *wl);

static void wl_delay(u32 ms);

/*
** store/restore cfg80211 instance data
*/
static void wl_set_drvdata(struct wl_dev *dev, void *data);
static void *wl_get_drvdata(struct wl_dev *dev);

/*
** ibss mode utilities
*/
static bool wl_is_ibssmode(struct wl_priv *wl);
static bool wl_is_ibssstarter(struct wl_priv *wl);

/*
** dongle up/down , default configuration utilities
*/
static bool wl_is_linkdown(struct wl_priv *wl, const wl_event_msg_t *e);
static bool wl_is_linkup(struct wl_priv *wl, const wl_event_msg_t *e);
static bool wl_is_nonetwork(struct wl_priv *wl, const wl_event_msg_t *e);
static void wl_link_up(struct wl_priv *wl);
static void wl_link_down(struct wl_priv *wl);
static s32 wl_dongle_mode(struct net_device *ndev, s32 iftype);
static s32 __wl_cfg80211_up(struct wl_priv *wl);
static s32 __wl_cfg80211_down(struct wl_priv *wl);
static s32 wl_dongle_probecap(struct wl_priv *wl);
static void wl_init_conf(struct wl_conf *conf);

/*
** dongle configuration utilities
*/
#ifndef EMBEDDED_PLATFORM
static s32 wl_dongle_mode(struct net_device *ndev, s32 iftype);
static s32 wl_dongle_country(struct net_device *ndev, u8 ccode);
static s32 wl_dongle_up(struct net_device *ndev, u32 up);
static s32 wl_dongle_power(struct net_device *ndev, u32 power_mode);
static s32 wl_dongle_glom(struct net_device *ndev, u32 glom,
			    u32 dongle_align);
static s32 wl_dongle_roam(struct net_device *ndev, u32 roamvar,
			    u32 bcn_timeout);
static s32 wl_dongle_eventmsg(struct net_device *ndev);
static s32 wl_dongle_scantime(struct net_device *ndev, s32 scan_assoc_time,
				s32 scan_unassoc_time);
static s32 wl_dongle_offload(struct net_device *ndev, s32 arpoe,
			       s32 arp_ol);
static s32 wl_pattern_atoh(s8 *src, s8 *dst);
static s32 wl_dongle_filter(struct net_device *ndev, u32 filter_mode);
static s32 wl_update_wiphybands(struct wl_priv *wl);
#endif				/* !EMBEDDED_PLATFORM */
static s32 wl_config_dongle(struct wl_priv *wl, bool need_lock);

/*
** iscan handler
*/
static void wl_iscan_timer(unsigned long data);
static void wl_term_iscan(struct wl_priv *wl);
static s32 wl_init_iscan(struct wl_priv *wl);
static s32 wl_iscan_thread(void *data);
static s32 wl_dev_iovar_setbuf(struct net_device *dev, s8 *iovar,
				 void *param, s32 paramlen, void *bufptr,
				 s32 buflen);
static s32 wl_dev_iovar_getbuf(struct net_device *dev, s8 *iovar,
				 void *param, s32 paramlen, void *bufptr,
				 s32 buflen);
static s32 wl_run_iscan(struct wl_iscan_ctrl *iscan, struct wlc_ssid *ssid,
			  u16 action);
static s32 wl_do_iscan(struct wl_priv *wl);
static s32 wl_wakeup_iscan(struct wl_iscan_ctrl *iscan);
static s32 wl_invoke_iscan(struct wl_priv *wl);
static s32 wl_get_iscan_results(struct wl_iscan_ctrl *iscan, u32 *status,
				  struct wl_scan_results **bss_list);
static void wl_notify_iscan_complete(struct wl_iscan_ctrl *iscan, bool aborted);
static void wl_init_iscan_eloop(struct wl_iscan_eloop *el);
static s32 wl_iscan_done(struct wl_priv *wl);
static s32 wl_iscan_pending(struct wl_priv *wl);
static s32 wl_iscan_inprogress(struct wl_priv *wl);
static s32 wl_iscan_aborted(struct wl_priv *wl);

/*
** fw/nvram downloading handler
*/
static void wl_init_fw(struct wl_fw_ctrl *fw);

/*
* find most significant bit set
*/
static __used u32 wl_find_msb(u16 bit16);

/*
* update pmklist to dongle
*/
static __used s32 wl_update_pmklist(struct net_device *dev,
				      struct wl_pmk_list *pmk_list, s32 err);

static void wl_set_mpc(struct net_device *ndev, int mpc);

/*
* debufs support
*/
static int wl_debugfs_add_netdev_params(struct wl_priv *wl);
static void wl_debugfs_remove_netdev(struct wl_priv *wl);

#define WL_PRIV_GET() 							\
	({								\
	struct wl_iface *ci;						\
	if (unlikely(!(wl_cfg80211_dev && 				\
		(ci = wl_get_drvdata(wl_cfg80211_dev))))) {		\
		WL_ERR("wl_cfg80211_dev is unavailable\n");		\
		BUG();							\
	} 								\
	ci_to_wl(ci);							\
})

#define CHECK_SYS_UP()							\
do {									\
	struct wl_priv *wl = wiphy_to_wl(wiphy);			\
	if (unlikely(!test_bit(WL_STATUS_READY, &wl->status))) {	\
		WL_INFO("device is not ready : status (%d)\n",		\
			(int)wl->status);				\
		return -EIO;						\
	}								\
} while (0)

extern int dhd_wait_pend8021x(struct net_device *dev);

#if (WL_DBG_LEVEL > 0)
#define WL_DBG_ESTR_MAX	32
static s8 wl_dbg_estr[][WL_DBG_ESTR_MAX] = {
	"SET_SSID", "JOIN", "START", "AUTH", "AUTH_IND",
	"DEAUTH", "DEAUTH_IND", "ASSOC", "ASSOC_IND", "REASSOC",
	"REASSOC_IND", "DISASSOC", "DISASSOC_IND", "QUIET_START", "QUIET_END",
	"BEACON_RX", "LINK", "MIC_ERROR", "NDIS_LINK", "ROAM",
	"TXFAIL", "PMKID_CACHE", "RETROGRADE_TSF", "PRUNE", "AUTOAUTH",
	"EAPOL_MSG", "SCAN_COMPLETE", "ADDTS_IND", "DELTS_IND", "BCNSENT_IND",
	"BCNRX_MSG", "BCNLOST_MSG", "ROAM_PREP", "PFN_NET_FOUND",
	"PFN_NET_LOST",
	"RESET_COMPLETE", "JOIN_START", "ROAM_START", "ASSOC_START",
	"IBSS_ASSOC",
	"RADIO", "PSM_WATCHDOG",
	"PROBREQ_MSG",
	"SCAN_CONFIRM_IND", "PSK_SUP", "COUNTRY_CODE_CHANGED",
	"EXCEEDED_MEDIUM_TIME", "ICV_ERROR",
	"UNICAST_DECODE_ERROR", "MULTICAST_DECODE_ERROR", "TRACE",
	"IF",
	"RSSI", "PFN_SCAN_COMPLETE", "ACTION_FRAME", "ACTION_FRAME_COMPLETE",
};
#endif				/* WL_DBG_LEVEL */

#define CHAN2G(_channel, _freq, _flags) {			\
	.band			= IEEE80211_BAND_2GHZ,		\
	.center_freq		= (_freq),			\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

#define CHAN5G(_channel, _flags) {				\
	.band			= IEEE80211_BAND_5GHZ,		\
	.center_freq		= 5000 + (5 * (_channel)),	\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

#define RATE_TO_BASE100KBPS(rate)   (((rate) * 10) / 2)
#define RATETAB_ENT(_rateid, _flags) \
	{                                                               \
		.bitrate        = RATE_TO_BASE100KBPS(_rateid),     \
		.hw_value       = (_rateid),                            \
		.flags          = (_flags),                             \
	}

static struct ieee80211_rate __wl_rates[] = {
	RATETAB_ENT(WLC_RATE_1M, 0),
	RATETAB_ENT(WLC_RATE_2M, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(WLC_RATE_5M5, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(WLC_RATE_11M, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(WLC_RATE_6M, 0),
	RATETAB_ENT(WLC_RATE_9M, 0),
	RATETAB_ENT(WLC_RATE_12M, 0),
	RATETAB_ENT(WLC_RATE_18M, 0),
	RATETAB_ENT(WLC_RATE_24M, 0),
	RATETAB_ENT(WLC_RATE_36M, 0),
	RATETAB_ENT(WLC_RATE_48M, 0),
	RATETAB_ENT(WLC_RATE_54M, 0),
};

#define wl_a_rates		(__wl_rates + 4)
#define wl_a_rates_size	8
#define wl_g_rates		(__wl_rates + 0)
#define wl_g_rates_size	12

static struct ieee80211_channel __wl_2ghz_channels[] = {
	CHAN2G(1, 2412, 0),
	CHAN2G(2, 2417, 0),
	CHAN2G(3, 2422, 0),
	CHAN2G(4, 2427, 0),
	CHAN2G(5, 2432, 0),
	CHAN2G(6, 2437, 0),
	CHAN2G(7, 2442, 0),
	CHAN2G(8, 2447, 0),
	CHAN2G(9, 2452, 0),
	CHAN2G(10, 2457, 0),
	CHAN2G(11, 2462, 0),
	CHAN2G(12, 2467, 0),
	CHAN2G(13, 2472, 0),
	CHAN2G(14, 2484, 0),
};

static struct ieee80211_channel __wl_5ghz_a_channels[] = {
	CHAN5G(34, 0), CHAN5G(36, 0),
	CHAN5G(38, 0), CHAN5G(40, 0),
	CHAN5G(42, 0), CHAN5G(44, 0),
	CHAN5G(46, 0), CHAN5G(48, 0),
	CHAN5G(52, 0), CHAN5G(56, 0),
	CHAN5G(60, 0), CHAN5G(64, 0),
	CHAN5G(100, 0), CHAN5G(104, 0),
	CHAN5G(108, 0), CHAN5G(112, 0),
	CHAN5G(116, 0), CHAN5G(120, 0),
	CHAN5G(124, 0), CHAN5G(128, 0),
	CHAN5G(132, 0), CHAN5G(136, 0),
	CHAN5G(140, 0), CHAN5G(149, 0),
	CHAN5G(153, 0), CHAN5G(157, 0),
	CHAN5G(161, 0), CHAN5G(165, 0),
	CHAN5G(184, 0), CHAN5G(188, 0),
	CHAN5G(192, 0), CHAN5G(196, 0),
	CHAN5G(200, 0), CHAN5G(204, 0),
	CHAN5G(208, 0), CHAN5G(212, 0),
	CHAN5G(216, 0),
};

static struct ieee80211_channel __wl_5ghz_n_channels[] = {
	CHAN5G(32, 0), CHAN5G(34, 0),
	CHAN5G(36, 0), CHAN5G(38, 0),
	CHAN5G(40, 0), CHAN5G(42, 0),
	CHAN5G(44, 0), CHAN5G(46, 0),
	CHAN5G(48, 0), CHAN5G(50, 0),
	CHAN5G(52, 0), CHAN5G(54, 0),
	CHAN5G(56, 0), CHAN5G(58, 0),
	CHAN5G(60, 0), CHAN5G(62, 0),
	CHAN5G(64, 0), CHAN5G(66, 0),
	CHAN5G(68, 0), CHAN5G(70, 0),
	CHAN5G(72, 0), CHAN5G(74, 0),
	CHAN5G(76, 0), CHAN5G(78, 0),
	CHAN5G(80, 0), CHAN5G(82, 0),
	CHAN5G(84, 0), CHAN5G(86, 0),
	CHAN5G(88, 0), CHAN5G(90, 0),
	CHAN5G(92, 0), CHAN5G(94, 0),
	CHAN5G(96, 0), CHAN5G(98, 0),
	CHAN5G(100, 0), CHAN5G(102, 0),
	CHAN5G(104, 0), CHAN5G(106, 0),
	CHAN5G(108, 0), CHAN5G(110, 0),
	CHAN5G(112, 0), CHAN5G(114, 0),
	CHAN5G(116, 0), CHAN5G(118, 0),
	CHAN5G(120, 0), CHAN5G(122, 0),
	CHAN5G(124, 0), CHAN5G(126, 0),
	CHAN5G(128, 0), CHAN5G(130, 0),
	CHAN5G(132, 0), CHAN5G(134, 0),
	CHAN5G(136, 0), CHAN5G(138, 0),
	CHAN5G(140, 0), CHAN5G(142, 0),
	CHAN5G(144, 0), CHAN5G(145, 0),
	CHAN5G(146, 0), CHAN5G(147, 0),
	CHAN5G(148, 0), CHAN5G(149, 0),
	CHAN5G(150, 0), CHAN5G(151, 0),
	CHAN5G(152, 0), CHAN5G(153, 0),
	CHAN5G(154, 0), CHAN5G(155, 0),
	CHAN5G(156, 0), CHAN5G(157, 0),
	CHAN5G(158, 0), CHAN5G(159, 0),
	CHAN5G(160, 0), CHAN5G(161, 0),
	CHAN5G(162, 0), CHAN5G(163, 0),
	CHAN5G(164, 0), CHAN5G(165, 0),
	CHAN5G(166, 0), CHAN5G(168, 0),
	CHAN5G(170, 0), CHAN5G(172, 0),
	CHAN5G(174, 0), CHAN5G(176, 0),
	CHAN5G(178, 0), CHAN5G(180, 0),
	CHAN5G(182, 0), CHAN5G(184, 0),
	CHAN5G(186, 0), CHAN5G(188, 0),
	CHAN5G(190, 0), CHAN5G(192, 0),
	CHAN5G(194, 0), CHAN5G(196, 0),
	CHAN5G(198, 0), CHAN5G(200, 0),
	CHAN5G(202, 0), CHAN5G(204, 0),
	CHAN5G(206, 0), CHAN5G(208, 0),
	CHAN5G(210, 0), CHAN5G(212, 0),
	CHAN5G(214, 0), CHAN5G(216, 0),
	CHAN5G(218, 0), CHAN5G(220, 0),
	CHAN5G(222, 0), CHAN5G(224, 0),
	CHAN5G(226, 0), CHAN5G(228, 0),
};

static struct ieee80211_supported_band __wl_band_2ghz = {
	.band = IEEE80211_BAND_2GHZ,
	.channels = __wl_2ghz_channels,
	.n_channels = ARRAY_SIZE(__wl_2ghz_channels),
	.bitrates = wl_g_rates,
	.n_bitrates = wl_g_rates_size,
};

static struct ieee80211_supported_band __wl_band_5ghz_a = {
	.band = IEEE80211_BAND_5GHZ,
	.channels = __wl_5ghz_a_channels,
	.n_channels = ARRAY_SIZE(__wl_5ghz_a_channels),
	.bitrates = wl_a_rates,
	.n_bitrates = wl_a_rates_size,
};

static struct ieee80211_supported_band __wl_band_5ghz_n = {
	.band = IEEE80211_BAND_5GHZ,
	.channels = __wl_5ghz_n_channels,
	.n_channels = ARRAY_SIZE(__wl_5ghz_n_channels),
	.bitrates = wl_a_rates,
	.n_bitrates = wl_a_rates_size,
};

static const u32 __wl_cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
	WLAN_CIPHER_SUITE_AES_CMAC,
};

static void swap_key_from_BE(struct wl_wsec_key *key)
{
	key->index = cpu_to_le32(key->index);
	key->len = cpu_to_le32(key->len);
	key->algo = cpu_to_le32(key->algo);
	key->flags = cpu_to_le32(key->flags);
	key->rxiv.hi = cpu_to_le32(key->rxiv.hi);
	key->rxiv.lo = cpu_to_le16(key->rxiv.lo);
	key->iv_initialized = cpu_to_le32(key->iv_initialized);
}

static void swap_key_to_BE(struct wl_wsec_key *key)
{
	key->index = le32_to_cpu(key->index);
	key->len = le32_to_cpu(key->len);
	key->algo = le32_to_cpu(key->algo);
	key->flags = le32_to_cpu(key->flags);
	key->rxiv.hi = le32_to_cpu(key->rxiv.hi);
	key->rxiv.lo = le16_to_cpu(key->rxiv.lo);
	key->iv_initialized = le32_to_cpu(key->iv_initialized);
}

static s32
wl_dev_ioctl(struct net_device *dev, u32 cmd, void *arg, u32 len)
{
	struct ifreq ifr;
	struct wl_ioctl ioc;
	mm_segment_t fs;
	s32 err = 0;

	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = cmd;
	ioc.buf = arg;
	ioc.len = len;
	strcpy(ifr.ifr_name, dev->name);
	ifr.ifr_data = (caddr_t)&ioc;

	fs = get_fs();
	set_fs(get_ds());
	err = dev->netdev_ops->ndo_do_ioctl(dev, &ifr, SIOCDEVPRIVATE);
	set_fs(fs);

	return err;
}

static s32
wl_cfg80211_change_iface(struct wiphy *wiphy, struct net_device *ndev,
			 enum nl80211_iftype type, u32 *flags,
			 struct vif_params *params)
{
	struct wl_priv *wl = wiphy_to_wl(wiphy);
	struct wireless_dev *wdev;
	s32 infra = 0;
	s32 ap = 0;
	s32 err = 0;

	CHECK_SYS_UP();
	switch (type) {
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_WDS:
		WL_ERR("type (%d) : currently we do not support this type\n",
		       type);
		return -EOPNOTSUPP;
	case NL80211_IFTYPE_ADHOC:
		wl->conf->mode = WL_MODE_IBSS;
		break;
	case NL80211_IFTYPE_STATION:
		wl->conf->mode = WL_MODE_BSS;
		infra = 1;
		break;
	default:
		return -EINVAL;
	}
	infra = cpu_to_le32(infra);
	ap = cpu_to_le32(ap);
	wdev = ndev->ieee80211_ptr;
	wdev->iftype = type;
	WL_DBG("%s : ap (%d), infra (%d)\n", ndev->name, ap, infra);
	err = wl_dev_ioctl(ndev, WLC_SET_INFRA, &infra, sizeof(infra));
	if (unlikely(err)) {
		WL_ERR("WLC_SET_INFRA error (%d)\n", err);
		return err;
	}
	err = wl_dev_ioctl(ndev, WLC_SET_AP, &ap, sizeof(ap));
	if (unlikely(err)) {
		WL_ERR("WLC_SET_AP error (%d)\n", err);
		return err;
	}

	/* -EINPROGRESS: Call commit handler */
	return -EINPROGRESS;
}

static void wl_iscan_prep(struct wl_scan_params *params, struct wlc_ssid *ssid)
{
	memcpy(params->bssid, ether_bcast, ETH_ALEN);
	params->bss_type = DOT11_BSSTYPE_ANY;
	params->scan_type = 0;
	params->nprobes = -1;
	params->active_time = -1;
	params->passive_time = -1;
	params->home_time = -1;
	params->channel_num = 0;

	params->nprobes = cpu_to_le32(params->nprobes);
	params->active_time = cpu_to_le32(params->active_time);
	params->passive_time = cpu_to_le32(params->passive_time);
	params->home_time = cpu_to_le32(params->home_time);
	if (ssid && ssid->SSID_len)
		memcpy(&params->ssid, ssid, sizeof(wlc_ssid_t));

}

static s32
wl_dev_iovar_setbuf(struct net_device *dev, s8 * iovar, void *param,
		    s32 paramlen, void *bufptr, s32 buflen)
{
	s32 iolen;

	iolen = bcm_mkiovar(iovar, param, paramlen, bufptr, buflen);
	BUG_ON(!iolen);

	return wl_dev_ioctl(dev, WLC_SET_VAR, bufptr, iolen);
}

static s32
wl_dev_iovar_getbuf(struct net_device *dev, s8 * iovar, void *param,
		    s32 paramlen, void *bufptr, s32 buflen)
{
	s32 iolen;

	iolen = bcm_mkiovar(iovar, param, paramlen, bufptr, buflen);
	BUG_ON(!iolen);

	return wl_dev_ioctl(dev, WLC_GET_VAR, bufptr, buflen);
}

static s32
wl_run_iscan(struct wl_iscan_ctrl *iscan, struct wlc_ssid *ssid, u16 action)
{
	s32 params_size =
	    (WL_SCAN_PARAMS_FIXED_SIZE + offsetof(wl_iscan_params_t, params));
	struct wl_iscan_params *params;
	s32 err = 0;

	if (ssid && ssid->SSID_len)
		params_size += sizeof(struct wlc_ssid);
	params = kzalloc(params_size, GFP_KERNEL);
	if (unlikely(!params))
		return -ENOMEM;
	BUG_ON(params_size >= WLC_IOCTL_SMLEN);

	wl_iscan_prep(&params->params, ssid);

	params->version = cpu_to_le32(ISCAN_REQ_VERSION);
	params->action = cpu_to_le16(action);
	params->scan_duration = cpu_to_le16(0);

	/* params_size += offsetof(wl_iscan_params_t, params); */
	err = wl_dev_iovar_setbuf(iscan->dev, "iscan", params, params_size,
				iscan->ioctl_buf, WLC_IOCTL_SMLEN);
	if (unlikely(err)) {
		if (err == -EBUSY) {
			WL_INFO("system busy : iscan canceled\n");
		} else {
			WL_ERR("error (%d)\n", err);
		}
	}
	kfree(params);
	return err;
}

static s32 wl_do_iscan(struct wl_priv *wl)
{
	struct wl_iscan_ctrl *iscan = wl_to_iscan(wl);
	struct net_device *ndev = wl_to_ndev(wl);
	struct wlc_ssid ssid;
	s32 passive_scan;
	s32 err = 0;

	/* Broadcast scan by default */
	memset(&ssid, 0, sizeof(ssid));

	iscan->state = WL_ISCAN_STATE_SCANING;

	passive_scan = wl->active_scan ? 0 : 1;
	err = wl_dev_ioctl(wl_to_ndev(wl), WLC_SET_PASSIVE_SCAN,
			&passive_scan, sizeof(passive_scan));
	if (unlikely(err)) {
		WL_DBG("error (%d)\n", err);
		return err;
	}
	wl_set_mpc(ndev, 0);
	wl->iscan_kickstart = true;
	wl_run_iscan(iscan, &ssid, WL_SCAN_ACTION_START);
	mod_timer(&iscan->timer, jiffies + iscan->timer_ms * HZ / 1000);
	iscan->timer_on = 1;

	return err;
}

static s32
__wl_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
		   struct cfg80211_scan_request *request,
		   struct cfg80211_ssid *this_ssid)
{
	struct wl_priv *wl = ndev_to_wl(ndev);
	struct cfg80211_ssid *ssids;
	struct wl_scan_req *sr = wl_to_sr(wl);
	s32 passive_scan;
	bool iscan_req;
	bool spec_scan;
	s32 err = 0;

	if (unlikely(test_bit(WL_STATUS_SCANNING, &wl->status))) {
		WL_ERR("Scanning already : status (%d)\n", (int)wl->status);
		return -EAGAIN;
	}
	if (unlikely(test_bit(WL_STATUS_SCAN_ABORTING, &wl->status))) {
		WL_ERR("Scanning being aborted : status (%d)\n",
		       (int)wl->status);
		return -EAGAIN;
	}

	iscan_req = false;
	spec_scan = false;
	if (request) {		/* scan bss */
		ssids = request->ssids;
		if (wl->iscan_on && (!ssids || !ssids->ssid_len)) {	/* for
							 * specific scan,
							 * ssids->ssid_len has
							 * non-zero(ssid string)
							 * length.
							 * Otherwise this is 0.
							 * we do not iscan for
							 * specific scan request
							 */
			iscan_req = true;
		}
	} else {		/* scan in ibss */
		/* we don't do iscan in ibss */
		ssids = this_ssid;
	}
	wl->scan_request = request;
	set_bit(WL_STATUS_SCANNING, &wl->status);
	if (iscan_req) {
		err = wl_do_iscan(wl);
		if (likely(!err))
			return err;
		else
			goto scan_out;
	} else {
		WL_DBG("ssid \"%s\", ssid_len (%d)\n",
		       ssids->ssid, ssids->ssid_len);
		memset(&sr->ssid, 0, sizeof(sr->ssid));
		sr->ssid.SSID_len =
			    min_t(u8, sizeof(sr->ssid.SSID), ssids->ssid_len);
		if (sr->ssid.SSID_len) {
			memcpy(sr->ssid.SSID, ssids->ssid, sr->ssid.SSID_len);
			sr->ssid.SSID_len = cpu_to_le32(sr->ssid.SSID_len);
			WL_DBG("Specific scan ssid=\"%s\" len=%d\n",
			       sr->ssid.SSID, sr->ssid.SSID_len);
			spec_scan = true;
		} else {
			WL_DBG("Broadcast scan\n");
		}
		WL_DBG("sr->ssid.SSID_len (%d)\n", sr->ssid.SSID_len);
		passive_scan = wl->active_scan ? 0 : 1;
		err = wl_dev_ioctl(ndev, WLC_SET_PASSIVE_SCAN,
				&passive_scan, sizeof(passive_scan));
		if (unlikely(err)) {
			WL_ERR("WLC_SET_PASSIVE_SCAN error (%d)\n", err);
			goto scan_out;
		}
		wl_set_mpc(ndev, 0);
		err = wl_dev_ioctl(ndev, WLC_SCAN, &sr->ssid,
				sizeof(sr->ssid));
		if (err) {
			if (err == -EBUSY) {
				WL_INFO("system busy : scan for \"%s\" canceled\n",
					sr->ssid.SSID);
			} else {
				WL_ERR("WLC_SCAN error (%d)\n", err);
			}
			wl_set_mpc(ndev, 1);
			goto scan_out;
		}
	}

	return 0;

scan_out:
	clear_bit(WL_STATUS_SCANNING, &wl->status);
	wl->scan_request = NULL;
	return err;
}

static s32
wl_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
		 struct cfg80211_scan_request *request)
{
	s32 err = 0;

	CHECK_SYS_UP();
	err = __wl_cfg80211_scan(wiphy, ndev, request, NULL);
	if (unlikely(err)) {
		WL_DBG("scan error (%d)\n", err);
		return err;
	}

	return err;
}

static s32 wl_dev_intvar_set(struct net_device *dev, s8 *name, s32 val)
{
	s8 buf[WLC_IOCTL_SMLEN];
	u32 len;
	s32 err = 0;

	val = cpu_to_le32(val);
	len = bcm_mkiovar(name, (char *)(&val), sizeof(val), buf, sizeof(buf));
	BUG_ON(!len);

	err = wl_dev_ioctl(dev, WLC_SET_VAR, buf, len);
	if (unlikely(err)) {
		WL_ERR("error (%d)\n", err);
	}

	return err;
}

static s32
wl_dev_intvar_get(struct net_device *dev, s8 *name, s32 *retval)
{
	union {
		s8 buf[WLC_IOCTL_SMLEN];
		s32 val;
	} var;
	u32 len;
	u32 data_null;
	s32 err = 0;

	len =
	    bcm_mkiovar(name, (char *)(&data_null), 0, (char *)(&var),
			sizeof(var.buf));
	BUG_ON(!len);
	err = wl_dev_ioctl(dev, WLC_GET_VAR, &var, len);
	if (unlikely(err)) {
		WL_ERR("error (%d)\n", err);
	}
	*retval = le32_to_cpu(var.val);

	return err;
}

static s32 wl_set_rts(struct net_device *dev, u32 rts_threshold)
{
	s32 err = 0;

	err = wl_dev_intvar_set(dev, "rtsthresh", rts_threshold);
	if (unlikely(err)) {
		WL_ERR("Error (%d)\n", err);
		return err;
	}
	return err;
}

static s32 wl_set_frag(struct net_device *dev, u32 frag_threshold)
{
	s32 err = 0;

	err = wl_dev_intvar_set(dev, "fragthresh", frag_threshold);
	if (unlikely(err)) {
		WL_ERR("Error (%d)\n", err);
		return err;
	}
	return err;
}

static s32 wl_set_retry(struct net_device *dev, u32 retry, bool l)
{
	s32 err = 0;
	u32 cmd = (l ? WLC_SET_LRL : WLC_SET_SRL);

	retry = cpu_to_le32(retry);
	err = wl_dev_ioctl(dev, cmd, &retry, sizeof(retry));
	if (unlikely(err)) {
		WL_ERR("cmd (%d) , error (%d)\n", cmd, err);
		return err;
	}
	return err;
}

static s32 wl_cfg80211_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	struct wl_priv *wl = wiphy_to_wl(wiphy);
	struct net_device *ndev = wl_to_ndev(wl);
	s32 err = 0;

	CHECK_SYS_UP();
	if (changed & WIPHY_PARAM_RTS_THRESHOLD &&
	    (wl->conf->rts_threshold != wiphy->rts_threshold)) {
		wl->conf->rts_threshold = wiphy->rts_threshold;
		err = wl_set_rts(ndev, wl->conf->rts_threshold);
		if (!err)
			return err;
	}
	if (changed & WIPHY_PARAM_FRAG_THRESHOLD &&
	    (wl->conf->frag_threshold != wiphy->frag_threshold)) {
		wl->conf->frag_threshold = wiphy->frag_threshold;
		err = wl_set_frag(ndev, wl->conf->frag_threshold);
		if (!err)
			return err;
	}
	if (changed & WIPHY_PARAM_RETRY_LONG
	    && (wl->conf->retry_long != wiphy->retry_long)) {
		wl->conf->retry_long = wiphy->retry_long;
		err = wl_set_retry(ndev, wl->conf->retry_long, true);
		if (!err)
			return err;
	}
	if (changed & WIPHY_PARAM_RETRY_SHORT
	    && (wl->conf->retry_short != wiphy->retry_short)) {
		wl->conf->retry_short = wiphy->retry_short;
		err = wl_set_retry(ndev, wl->conf->retry_short, false);
		if (!err) {
			return err;
		}
	}

	return err;
}

static s32
wl_cfg80211_join_ibss(struct wiphy *wiphy, struct net_device *dev,
		      struct cfg80211_ibss_params *params)
{
	struct wl_priv *wl = wiphy_to_wl(wiphy);
	struct cfg80211_bss *bss;
	struct ieee80211_channel *chan;
	struct wl_join_params join_params;
	struct cfg80211_ssid ssid;
	s32 scan_retry = 0;
	s32 err = 0;

	CHECK_SYS_UP();
	if (params->bssid) {
		WL_ERR("Invalid bssid\n");
		return -EOPNOTSUPP;
	}
	bss = cfg80211_get_ibss(wiphy, NULL, params->ssid, params->ssid_len);
	if (!bss) {
		memcpy(ssid.ssid, params->ssid, params->ssid_len);
		ssid.ssid_len = params->ssid_len;
		do {
			if (unlikely
			    (__wl_cfg80211_scan(wiphy, dev, NULL, &ssid) ==
			     -EBUSY)) {
				wl_delay(150);
			} else {
				break;
			}
		} while (++scan_retry < WL_SCAN_RETRY_MAX);
		rtnl_unlock();	/* to allow scan_inform to paropagate
					 to cfg80211 plane */
		schedule_timeout_interruptible(4 * HZ);	/* wait 4 secons
						 till scan done.... */
		rtnl_lock();
		bss = cfg80211_get_ibss(wiphy, NULL,
					params->ssid, params->ssid_len);
	}
	if (bss) {
		wl->ibss_starter = false;
		WL_DBG("Found IBSS\n");
	} else {
		wl->ibss_starter = true;
	}
	chan = params->channel;
	if (chan)
		wl->channel = ieee80211_frequency_to_channel(chan->center_freq);
	/*
	 ** Join with specific BSSID and cached SSID
	 ** If SSID is zero join based on BSSID only
	 */
	memset(&join_params, 0, sizeof(join_params));
	memcpy((void *)join_params.ssid.SSID, (void *)params->ssid,
	       params->ssid_len);
	join_params.ssid.SSID_len = cpu_to_le32(params->ssid_len);
	if (params->bssid)
		memcpy(&join_params.params.bssid, params->bssid,
		       ETH_ALEN);
	else
		memset(&join_params.params.bssid, 0, ETH_ALEN);

	err = wl_dev_ioctl(dev, WLC_SET_SSID, &join_params,
			sizeof(join_params));
	if (unlikely(err)) {
		WL_ERR("Error (%d)\n", err);
		return err;
	}
	return err;
}

static s32 wl_cfg80211_leave_ibss(struct wiphy *wiphy, struct net_device *dev)
{
	struct wl_priv *wl = wiphy_to_wl(wiphy);
	s32 err = 0;

	CHECK_SYS_UP();
	wl_link_down(wl);

	return err;
}

static s32
wl_set_wpa_version(struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct wl_priv *wl = ndev_to_wl(dev);
	struct wl_security *sec;
	s32 val = 0;
	s32 err = 0;

	if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_1)
		val = WPA_AUTH_PSK | WPA_AUTH_UNSPECIFIED;
	else if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_2)
		val = WPA2_AUTH_PSK | WPA2_AUTH_UNSPECIFIED;
	else
		val = WPA_AUTH_DISABLED;
	WL_DBG("setting wpa_auth to 0x%0x\n", val);
	err = wl_dev_intvar_set(dev, "wpa_auth", val);
	if (unlikely(err)) {
		WL_ERR("set wpa_auth failed (%d)\n", err);
		return err;
	}
	sec = wl_read_prof(wl, WL_PROF_SEC);
	sec->wpa_versions = sme->crypto.wpa_versions;
	return err;
}

static s32
wl_set_auth_type(struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct wl_priv *wl = ndev_to_wl(dev);
	struct wl_security *sec;
	s32 val = 0;
	s32 err = 0;

	switch (sme->auth_type) {
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
		val = 0;
		WL_DBG("open system\n");
		break;
	case NL80211_AUTHTYPE_SHARED_KEY:
		val = 1;
		WL_DBG("shared key\n");
		break;
	case NL80211_AUTHTYPE_AUTOMATIC:
		val = 2;
		WL_DBG("automatic\n");
		break;
	case NL80211_AUTHTYPE_NETWORK_EAP:
		WL_DBG("network eap\n");
	default:
		val = 2;
		WL_ERR("invalid auth type (%d)\n", sme->auth_type);
		break;
	}

	err = wl_dev_intvar_set(dev, "auth", val);
	if (unlikely(err)) {
		WL_ERR("set auth failed (%d)\n", err);
		return err;
	}
	sec = wl_read_prof(wl, WL_PROF_SEC);
	sec->auth_type = sme->auth_type;
	return err;
}

static s32
wl_set_set_cipher(struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct wl_priv *wl = ndev_to_wl(dev);
	struct wl_security *sec;
	s32 pval = 0;
	s32 gval = 0;
	s32 err = 0;

	if (sme->crypto.n_ciphers_pairwise) {
		switch (sme->crypto.ciphers_pairwise[0]) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			pval = WEP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			pval = TKIP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			pval = AES_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:
			pval = AES_ENABLED;
			break;
		default:
			WL_ERR("invalid cipher pairwise (%d)\n",
			       sme->crypto.ciphers_pairwise[0]);
			return -EINVAL;
		}
	}
	if (sme->crypto.cipher_group) {
		switch (sme->crypto.cipher_group) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			gval = WEP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			gval = TKIP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			gval = AES_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:
			gval = AES_ENABLED;
			break;
		default:
			WL_ERR("invalid cipher group (%d)\n",
			       sme->crypto.cipher_group);
			return -EINVAL;
		}
	}

	WL_DBG("pval (%d) gval (%d)\n", pval, gval);
	err = wl_dev_intvar_set(dev, "wsec", pval | gval);
	if (unlikely(err)) {
		WL_ERR("error (%d)\n", err);
		return err;
	}

	sec = wl_read_prof(wl, WL_PROF_SEC);
	sec->cipher_pairwise = sme->crypto.ciphers_pairwise[0];
	sec->cipher_group = sme->crypto.cipher_group;

	return err;
}

static s32
wl_set_key_mgmt(struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct wl_priv *wl = ndev_to_wl(dev);
	struct wl_security *sec;
	s32 val = 0;
	s32 err = 0;

	if (sme->crypto.n_akm_suites) {
		err = wl_dev_intvar_get(dev, "wpa_auth", &val);
		if (unlikely(err)) {
			WL_ERR("could not get wpa_auth (%d)\n", err);
			return err;
		}
		if (val & (WPA_AUTH_PSK | WPA_AUTH_UNSPECIFIED)) {
			switch (sme->crypto.akm_suites[0]) {
			case WLAN_AKM_SUITE_8021X:
				val = WPA_AUTH_UNSPECIFIED;
				break;
			case WLAN_AKM_SUITE_PSK:
				val = WPA_AUTH_PSK;
				break;
			default:
				WL_ERR("invalid cipher group (%d)\n",
				       sme->crypto.cipher_group);
				return -EINVAL;
			}
		} else if (val & (WPA2_AUTH_PSK | WPA2_AUTH_UNSPECIFIED)) {
			switch (sme->crypto.akm_suites[0]) {
			case WLAN_AKM_SUITE_8021X:
				val = WPA2_AUTH_UNSPECIFIED;
				break;
			case WLAN_AKM_SUITE_PSK:
				val = WPA2_AUTH_PSK;
				break;
			default:
				WL_ERR("invalid cipher group (%d)\n",
				       sme->crypto.cipher_group);
				return -EINVAL;
			}
		}

		WL_DBG("setting wpa_auth to %d\n", val);
		err = wl_dev_intvar_set(dev, "wpa_auth", val);
		if (unlikely(err)) {
			WL_ERR("could not set wpa_auth (%d)\n", err);
			return err;
		}
	}
	sec = wl_read_prof(wl, WL_PROF_SEC);
	sec->wpa_auth = sme->crypto.akm_suites[0];

	return err;
}

static s32
wl_set_set_sharedkey(struct net_device *dev,
		     struct cfg80211_connect_params *sme)
{
	struct wl_priv *wl = ndev_to_wl(dev);
	struct wl_security *sec;
	struct wl_wsec_key key;
	s32 val;
	s32 err = 0;

	WL_DBG("key len (%d)\n", sme->key_len);
	if (sme->key_len) {
		sec = wl_read_prof(wl, WL_PROF_SEC);
		WL_DBG("wpa_versions 0x%x cipher_pairwise 0x%x\n",
		       sec->wpa_versions, sec->cipher_pairwise);
		if (!
		    (sec->wpa_versions & (NL80211_WPA_VERSION_1 |
					  NL80211_WPA_VERSION_2))
&& (sec->cipher_pairwise & (WLAN_CIPHER_SUITE_WEP40 |
			    WLAN_CIPHER_SUITE_WEP104))) {
			memset(&key, 0, sizeof(key));
			key.len = (u32) sme->key_len;
			key.index = (u32) sme->key_idx;
			if (unlikely(key.len > sizeof(key.data))) {
				WL_ERR("Too long key length (%u)\n", key.len);
				return -EINVAL;
			}
			memcpy(key.data, sme->key, key.len);
			key.flags = WL_PRIMARY_KEY;
			switch (sec->cipher_pairwise) {
			case WLAN_CIPHER_SUITE_WEP40:
				key.algo = CRYPTO_ALGO_WEP1;
				break;
			case WLAN_CIPHER_SUITE_WEP104:
				key.algo = CRYPTO_ALGO_WEP128;
				break;
			default:
				WL_ERR("Invalid algorithm (%d)\n",
				       sme->crypto.ciphers_pairwise[0]);
				return -EINVAL;
			}
			/* Set the new key/index */
			WL_DBG("key length (%d) key index (%d) algo (%d)\n",
			       key.len, key.index, key.algo);
			WL_DBG("key \"%s\"\n", key.data);
			swap_key_from_BE(&key);
			err = wl_dev_ioctl(dev, WLC_SET_KEY, &key,
					sizeof(key));
			if (unlikely(err)) {
				WL_ERR("WLC_SET_KEY error (%d)\n", err);
				return err;
			}
			if (sec->auth_type == NL80211_AUTHTYPE_OPEN_SYSTEM) {
				WL_DBG("set auth_type to shared key\n");
				val = 1;	/* shared key */
				err = wl_dev_intvar_set(dev, "auth", val);
				if (unlikely(err)) {
					WL_ERR("set auth failed (%d)\n", err);
					return err;
				}
			}
		}
	}
	return err;
}

static s32
wl_cfg80211_connect(struct wiphy *wiphy, struct net_device *dev,
		    struct cfg80211_connect_params *sme)
{
	struct wl_priv *wl = wiphy_to_wl(wiphy);
	struct ieee80211_channel *chan = sme->channel;
	struct wl_join_params join_params;
	size_t join_params_size;

	s32 err = 0;

	CHECK_SYS_UP();
	if (unlikely(!sme->ssid)) {
		WL_ERR("Invalid ssid\n");
		return -EOPNOTSUPP;
	}
	if (chan) {
		wl->channel = ieee80211_frequency_to_channel(chan->center_freq);
		WL_DBG("channel (%d), center_req (%d)\n",
		       wl->channel, chan->center_freq);
	}
	WL_DBG("ie (%p), ie_len (%zd)\n", sme->ie, sme->ie_len);
	err = wl_set_wpa_version(dev, sme);
	if (unlikely(err))
		return err;

	err = wl_set_auth_type(dev, sme);
	if (unlikely(err))
		return err;

	err = wl_set_set_cipher(dev, sme);
	if (unlikely(err))
		return err;

	err = wl_set_key_mgmt(dev, sme);
	if (unlikely(err))
		return err;

	err = wl_set_set_sharedkey(dev, sme);
	if (unlikely(err))
		return err;

	wl_update_prof(wl, NULL, sme->bssid, WL_PROF_BSSID);
	/*
	 **  Join with specific BSSID and cached SSID
	 **  If SSID is zero join based on BSSID only
	 */
	memset(&join_params, 0, sizeof(join_params));
	join_params_size = sizeof(join_params.ssid);

	join_params.ssid.SSID_len = min(sizeof(join_params.ssid.SSID), sme->ssid_len);
	memcpy(&join_params.ssid.SSID, sme->ssid, join_params.ssid.SSID_len);
	join_params.ssid.SSID_len = cpu_to_le32(join_params.ssid.SSID_len);
	wl_update_prof(wl, NULL, &join_params.ssid, WL_PROF_SSID);
	memcpy(join_params.params.bssid, ether_bcast, ETH_ALEN);

	wl_ch_to_chanspec(wl->channel, &join_params, &join_params_size);
	WL_DBG("join_param_size %zu\n", join_params_size);

	if (join_params.ssid.SSID_len < IEEE80211_MAX_SSID_LEN) {
		WL_DBG("ssid \"%s\", len (%d)\n",
		       join_params.ssid.SSID, join_params.ssid.SSID_len);
	}
	err = wl_dev_ioctl(dev, WLC_SET_SSID, &join_params, join_params_size);
	if (unlikely(err)) {
		WL_ERR("error (%d)\n", err);
		return err;
	}
	set_bit(WL_STATUS_CONNECTING, &wl->status);

	return err;
}

static s32
wl_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *dev,
		       u16 reason_code)
{
	struct wl_priv *wl = wiphy_to_wl(wiphy);
	scb_val_t scbval;
	bool act = false;
	s32 err = 0;

	WL_DBG("Reason %d\n", reason_code);
	CHECK_SYS_UP();
	act = *(bool *) wl_read_prof(wl, WL_PROF_ACT);
	if (likely(act)) {
		scbval.val = reason_code;
		memcpy(&scbval.ea, &wl->bssid, ETH_ALEN);
		scbval.val = cpu_to_le32(scbval.val);
		err = wl_dev_ioctl(dev, WLC_DISASSOC, &scbval,
				sizeof(scb_val_t));
		if (unlikely(err)) {
			WL_ERR("error (%d)\n", err);
			return err;
		}
	}

	return err;
}

static s32
wl_cfg80211_set_tx_power(struct wiphy *wiphy,
			 enum nl80211_tx_power_setting type, s32 dbm)
{

	struct wl_priv *wl = wiphy_to_wl(wiphy);
	struct net_device *ndev = wl_to_ndev(wl);
	u16 txpwrmw;
	s32 err = 0;
	s32 disable = 0;

	CHECK_SYS_UP();
	switch (type) {
	case NL80211_TX_POWER_AUTOMATIC:
		break;
	case NL80211_TX_POWER_LIMITED:
		if (dbm < 0) {
			WL_ERR("TX_POWER_LIMITED - dbm is negative\n");
			return -EINVAL;
		}
		break;
	case NL80211_TX_POWER_FIXED:
		if (dbm < 0) {
			WL_ERR("TX_POWER_FIXED - dbm is negative\n");
			return -EINVAL;
		}
		break;
	}
	/* Make sure radio is off or on as far as software is concerned */
	disable = WL_RADIO_SW_DISABLE << 16;
	disable = cpu_to_le32(disable);
	err = wl_dev_ioctl(ndev, WLC_SET_RADIO, &disable, sizeof(disable));
	if (unlikely(err)) {
		WL_ERR("WLC_SET_RADIO error (%d)\n", err);
		return err;
	}

	if (dbm > 0xffff)
		txpwrmw = 0xffff;
	else
		txpwrmw = (u16) dbm;
	err = wl_dev_intvar_set(ndev, "qtxpower",
			(s32) (bcm_mw_to_qdbm(txpwrmw)));
	if (unlikely(err)) {
		WL_ERR("qtxpower error (%d)\n", err);
		return err;
	}
	wl->conf->tx_power = dbm;

	return err;
}

static s32 wl_cfg80211_get_tx_power(struct wiphy *wiphy, s32 *dbm)
{
	struct wl_priv *wl = wiphy_to_wl(wiphy);
	struct net_device *ndev = wl_to_ndev(wl);
	s32 txpwrdbm;
	u8 result;
	s32 err = 0;

	CHECK_SYS_UP();
	err = wl_dev_intvar_get(ndev, "qtxpower", &txpwrdbm);
	if (unlikely(err)) {
		WL_ERR("error (%d)\n", err);
		return err;
	}
	result = (u8) (txpwrdbm & ~WL_TXPWR_OVERRIDE);
	*dbm = (s32) bcm_qdbm_to_mw(result);

	return err;
}

static s32
wl_cfg80211_config_default_key(struct wiphy *wiphy, struct net_device *dev,
			       u8 key_idx, bool unicast, bool multicast)
{
	u32 index;
	s32 wsec;
	s32 err = 0;

	WL_DBG("key index (%d)\n", key_idx);
	CHECK_SYS_UP();

	err = wl_dev_ioctl(dev, WLC_GET_WSEC, &wsec, sizeof(wsec));
	if (unlikely(err)) {
		WL_ERR("WLC_GET_WSEC error (%d)\n", err);
		return err;
	}
	wsec = le32_to_cpu(wsec);
	if (wsec & WEP_ENABLED) {
		/* Just select a new current key */
		index = (u32) key_idx;
		index = cpu_to_le32(index);
		err = wl_dev_ioctl(dev, WLC_SET_KEY_PRIMARY, &index,
				sizeof(index));
		if (unlikely(err)) {
			WL_ERR("error (%d)\n", err);
		}
	}
	return err;
}

static s32
wl_add_keyext(struct wiphy *wiphy, struct net_device *dev,
	      u8 key_idx, const u8 *mac_addr, struct key_params *params)
{
	struct wl_wsec_key key;
	s32 err = 0;

	memset(&key, 0, sizeof(key));
	key.index = (u32) key_idx;
	/* Instead of bcast for ea address for default wep keys,
		 driver needs it to be Null */
	if (!is_multicast_ether_addr(mac_addr))
		memcpy((char *)&key.ea, (void *)mac_addr, ETH_ALEN);
	key.len = (u32) params->key_len;
	/* check for key index change */
	if (key.len == 0) {
		/* key delete */
		swap_key_from_BE(&key);
		err = wl_dev_ioctl(dev, WLC_SET_KEY, &key, sizeof(key));
		if (unlikely(err)) {
			WL_ERR("key delete error (%d)\n", err);
			return err;
		}
	} else {
		if (key.len > sizeof(key.data)) {
			WL_ERR("Invalid key length (%d)\n", key.len);
			return -EINVAL;
		}

		WL_DBG("Setting the key index %d\n", key.index);
		memcpy(key.data, params->key, key.len);

		if (params->cipher == WLAN_CIPHER_SUITE_TKIP) {
			u8 keybuf[8];
			memcpy(keybuf, &key.data[24], sizeof(keybuf));
			memcpy(&key.data[24], &key.data[16], sizeof(keybuf));
			memcpy(&key.data[16], keybuf, sizeof(keybuf));
		}

		/* if IW_ENCODE_EXT_RX_SEQ_VALID set */
		if (params->seq && params->seq_len == 6) {
			/* rx iv */
			u8 *ivptr;
			ivptr = (u8 *) params->seq;
			key.rxiv.hi = (ivptr[5] << 24) | (ivptr[4] << 16) |
			    (ivptr[3] << 8) | ivptr[2];
			key.rxiv.lo = (ivptr[1] << 8) | ivptr[0];
			key.iv_initialized = true;
		}

		switch (params->cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
			key.algo = CRYPTO_ALGO_WEP1;
			WL_DBG("WLAN_CIPHER_SUITE_WEP40\n");
			break;
		case WLAN_CIPHER_SUITE_WEP104:
			key.algo = CRYPTO_ALGO_WEP128;
			WL_DBG("WLAN_CIPHER_SUITE_WEP104\n");
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			key.algo = CRYPTO_ALGO_TKIP;
			WL_DBG("WLAN_CIPHER_SUITE_TKIP\n");
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:
			key.algo = CRYPTO_ALGO_AES_CCM;
			WL_DBG("WLAN_CIPHER_SUITE_AES_CMAC\n");
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			key.algo = CRYPTO_ALGO_AES_CCM;
			WL_DBG("WLAN_CIPHER_SUITE_CCMP\n");
			break;
		default:
			WL_ERR("Invalid cipher (0x%x)\n", params->cipher);
			return -EINVAL;
		}
		swap_key_from_BE(&key);

		dhd_wait_pend8021x(dev);
		err = wl_dev_ioctl(dev, WLC_SET_KEY, &key, sizeof(key));
		if (unlikely(err)) {
			WL_ERR("WLC_SET_KEY error (%d)\n", err);
			return err;
		}
	}
	return err;
}

static s32
wl_cfg80211_add_key(struct wiphy *wiphy, struct net_device *dev,
		    u8 key_idx, bool pairwise, const u8 *mac_addr,
		    struct key_params *params)
{
	struct wl_wsec_key key;
	s32 val;
	s32 wsec;
	s32 err = 0;

	WL_DBG("key index (%d)\n", key_idx);
	CHECK_SYS_UP();

	if (mac_addr)
		return wl_add_keyext(wiphy, dev, key_idx, mac_addr, params);
	memset(&key, 0, sizeof(key));

	key.len = (u32) params->key_len;
	key.index = (u32) key_idx;

	if (unlikely(key.len > sizeof(key.data))) {
		WL_ERR("Too long key length (%u)\n", key.len);
		return -EINVAL;
	}
	memcpy(key.data, params->key, key.len);

	key.flags = WL_PRIMARY_KEY;
	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		key.algo = CRYPTO_ALGO_WEP1;
		WL_DBG("WLAN_CIPHER_SUITE_WEP40\n");
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		key.algo = CRYPTO_ALGO_WEP128;
		WL_DBG("WLAN_CIPHER_SUITE_WEP104\n");
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		key.algo = CRYPTO_ALGO_TKIP;
		WL_DBG("WLAN_CIPHER_SUITE_TKIP\n");
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		key.algo = CRYPTO_ALGO_AES_CCM;
		WL_DBG("WLAN_CIPHER_SUITE_AES_CMAC\n");
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		key.algo = CRYPTO_ALGO_AES_CCM;
		WL_DBG("WLAN_CIPHER_SUITE_CCMP\n");
		break;
	default:
		WL_ERR("Invalid cipher (0x%x)\n", params->cipher);
		return -EINVAL;
	}

	/* Set the new key/index */
	swap_key_from_BE(&key);
	err = wl_dev_ioctl(dev, WLC_SET_KEY, &key, sizeof(key));
	if (unlikely(err)) {
		WL_ERR("WLC_SET_KEY error (%d)\n", err);
		return err;
	}

	val = WEP_ENABLED;
	err = wl_dev_intvar_get(dev, "wsec", &wsec);
	if (unlikely(err)) {
		WL_ERR("get wsec error (%d)\n", err);
		return err;
	}
	wsec &= ~(WEP_ENABLED);
	wsec |= val;
	err = wl_dev_intvar_set(dev, "wsec", wsec);
	if (unlikely(err)) {
		WL_ERR("set wsec error (%d)\n", err);
		return err;
	}

	val = 1;		/* assume shared key. otherwise 0 */
	val = cpu_to_le32(val);
	err = wl_dev_ioctl(dev, WLC_SET_AUTH, &val, sizeof(val));
	if (unlikely(err)) {
		WL_ERR("WLC_SET_AUTH error (%d)\n", err);
		return err;
	}
	return err;
}

static s32
wl_cfg80211_del_key(struct wiphy *wiphy, struct net_device *dev,
		    u8 key_idx, bool pairwise, const u8 *mac_addr)
{
	struct wl_wsec_key key;
	s32 err = 0;
	s32 val;
	s32 wsec;

	CHECK_SYS_UP();
	memset(&key, 0, sizeof(key));

	key.index = (u32) key_idx;
	key.flags = WL_PRIMARY_KEY;
	key.algo = CRYPTO_ALGO_OFF;

	WL_DBG("key index (%d)\n", key_idx);
	/* Set the new key/index */
	swap_key_from_BE(&key);
	err = wl_dev_ioctl(dev, WLC_SET_KEY, &key, sizeof(key));
	if (unlikely(err)) {
		if (err == -EINVAL) {
			if (key.index >= DOT11_MAX_DEFAULT_KEYS) {
				/* we ignore this key index in this case */
				WL_DBG("invalid key index (%d)\n", key_idx);
			}
		} else {
			WL_ERR("WLC_SET_KEY error (%d)\n", err);
		}
		return err;
	}

	val = 0;
	err = wl_dev_intvar_get(dev, "wsec", &wsec);
	if (unlikely(err)) {
		WL_ERR("get wsec error (%d)\n", err);
		return err;
	}
	wsec &= ~(WEP_ENABLED);
	wsec |= val;
	err = wl_dev_intvar_set(dev, "wsec", wsec);
	if (unlikely(err)) {
		WL_ERR("set wsec error (%d)\n", err);
		return err;
	}

	val = 0;		/* assume open key. otherwise 1 */
	val = cpu_to_le32(val);
	err = wl_dev_ioctl(dev, WLC_SET_AUTH, &val, sizeof(val));
	if (unlikely(err)) {
		WL_ERR("WLC_SET_AUTH error (%d)\n", err);
		return err;
	}
	return err;
}

static s32
wl_cfg80211_get_key(struct wiphy *wiphy, struct net_device *dev,
		    u8 key_idx, bool pairwise, const u8 *mac_addr, void *cookie,
		    void (*callback) (void *cookie, struct key_params * params))
{
	struct key_params params;
	struct wl_wsec_key key;
	struct wl_priv *wl = wiphy_to_wl(wiphy);
	struct wl_security *sec;
	s32 wsec;
	s32 err = 0;

	WL_DBG("key index (%d)\n", key_idx);
	CHECK_SYS_UP();

	memset(&key, 0, sizeof(key));
	key.index = key_idx;
	swap_key_to_BE(&key);
	memset(&params, 0, sizeof(params));
	params.key_len = (u8) min_t(u8, DOT11_MAX_KEY_SIZE, key.len);
	memcpy(params.key, key.data, params.key_len);

	err = wl_dev_ioctl(dev, WLC_GET_WSEC, &wsec, sizeof(wsec));
	if (unlikely(err)) {
		WL_ERR("WLC_GET_WSEC error (%d)\n", err);
		return err;
	}
	wsec = le32_to_cpu(wsec);
	switch (wsec) {
	case WEP_ENABLED:
		sec = wl_read_prof(wl, WL_PROF_SEC);
		if (sec->cipher_pairwise & WLAN_CIPHER_SUITE_WEP40) {
			params.cipher = WLAN_CIPHER_SUITE_WEP40;
			WL_DBG("WLAN_CIPHER_SUITE_WEP40\n");
		} else if (sec->cipher_pairwise & WLAN_CIPHER_SUITE_WEP104) {
			params.cipher = WLAN_CIPHER_SUITE_WEP104;
			WL_DBG("WLAN_CIPHER_SUITE_WEP104\n");
		}
		break;
	case TKIP_ENABLED:
		params.cipher = WLAN_CIPHER_SUITE_TKIP;
		WL_DBG("WLAN_CIPHER_SUITE_TKIP\n");
		break;
	case AES_ENABLED:
		params.cipher = WLAN_CIPHER_SUITE_AES_CMAC;
		WL_DBG("WLAN_CIPHER_SUITE_AES_CMAC\n");
		break;
	default:
		WL_ERR("Invalid algo (0x%x)\n", wsec);
		return -EINVAL;
	}

	callback(cookie, &params);
	return err;
}

static s32
wl_cfg80211_config_default_mgmt_key(struct wiphy *wiphy,
				    struct net_device *dev, u8 key_idx)
{
	WL_INFO("Not supported\n");
	CHECK_SYS_UP();
	return -EOPNOTSUPP;
}

static s32
wl_cfg80211_get_station(struct wiphy *wiphy, struct net_device *dev,
			u8 *mac, struct station_info *sinfo)
{
	struct wl_priv *wl = wiphy_to_wl(wiphy);
	scb_val_t scb_val;
	int rssi;
	s32 rate;
	s32 err = 0;

	CHECK_SYS_UP();
	if (unlikely
	    (memcmp(mac, wl_read_prof(wl, WL_PROF_BSSID), ETH_ALEN))) {
		WL_ERR("Wrong Mac address\n");
		return -ENOENT;
	}

	/* Report the current tx rate */
	err = wl_dev_ioctl(dev, WLC_GET_RATE, &rate, sizeof(rate));
	if (err) {
		WL_ERR("Could not get rate (%d)\n", err);
	} else {
		rate = le32_to_cpu(rate);
		sinfo->filled |= STATION_INFO_TX_BITRATE;
		sinfo->txrate.legacy = rate * 5;
		WL_DBG("Rate %d Mbps\n", rate / 2);
	}

	if (test_bit(WL_STATUS_CONNECTED, &wl->status)) {
		scb_val.val = 0;
		err = wl_dev_ioctl(dev, WLC_GET_RSSI, &scb_val,
				sizeof(scb_val_t));
		if (unlikely(err)) {
			WL_ERR("Could not get rssi (%d)\n", err);
			return err;
		}
		rssi = le32_to_cpu(scb_val.val);
		sinfo->filled |= STATION_INFO_SIGNAL;
		sinfo->signal = rssi;
		WL_DBG("RSSI %d dBm\n", rssi);
	}

	return err;
}

static s32
wl_cfg80211_set_power_mgmt(struct wiphy *wiphy, struct net_device *dev,
			   bool enabled, s32 timeout)
{
	s32 pm;
	s32 err = 0;

	CHECK_SYS_UP();
	pm = enabled ? PM_FAST : PM_OFF;
	pm = cpu_to_le32(pm);
	WL_DBG("power save %s\n", (pm ? "enabled" : "disabled"));
	err = wl_dev_ioctl(dev, WLC_SET_PM, &pm, sizeof(pm));
	if (unlikely(err)) {
		if (err == -ENODEV)
			WL_DBG("net_device is not ready yet\n");
		else
			WL_ERR("error (%d)\n", err);
		return err;
	}
	return err;
}

static __used u32 wl_find_msb(u16 bit16)
{
	u32 ret = 0;

	if (bit16 & 0xff00) {
		ret += 8;
		bit16 >>= 8;
	}

	if (bit16 & 0xf0) {
		ret += 4;
		bit16 >>= 4;
	}

	if (bit16 & 0xc) {
		ret += 2;
		bit16 >>= 2;
	}

	if (bit16 & 2)
		ret += bit16 & 2;
	else if (bit16)
		ret += bit16;

	return ret;
}

static s32
wl_cfg80211_set_bitrate_mask(struct wiphy *wiphy, struct net_device *dev,
			     const u8 *addr,
			     const struct cfg80211_bitrate_mask *mask)
{
	struct wl_rateset rateset;
	s32 rate;
	s32 val;
	s32 err_bg;
	s32 err_a;
	u32 legacy;
	s32 err = 0;

	CHECK_SYS_UP();
	/* addr param is always NULL. ignore it */
	/* Get current rateset */
	err = wl_dev_ioctl(dev, WLC_GET_CURR_RATESET, &rateset,
			sizeof(rateset));
	if (unlikely(err)) {
		WL_ERR("could not get current rateset (%d)\n", err);
		return err;
	}

	rateset.count = le32_to_cpu(rateset.count);

	legacy = wl_find_msb(mask->control[IEEE80211_BAND_2GHZ].legacy);
	if (!legacy)
		legacy = wl_find_msb(mask->control[IEEE80211_BAND_5GHZ].legacy);

	val = wl_g_rates[legacy - 1].bitrate * 100000;

	if (val < rateset.count) {
		/* Select rate by rateset index */
		rate = rateset.rates[val] & 0x7f;
	} else {
		/* Specified rate in bps */
		rate = val / 500000;
	}

	WL_DBG("rate %d mbps\n", rate / 2);

	/*
	 *
	 *      Set rate override,
	 *      Since the is a/b/g-blind, both a/bg_rate are enforced.
	 */
	err_bg = wl_dev_intvar_set(dev, "bg_rate", rate);
	err_a = wl_dev_intvar_set(dev, "a_rate", rate);
	if (unlikely(err_bg && err_a)) {
		WL_ERR("could not set fixed rate (%d) (%d)\n", err_bg, err_a);
		return err_bg | err_a;
	}

	return err;
}

static s32 wl_cfg80211_resume(struct wiphy *wiphy)
{
	s32 err = 0;

	CHECK_SYS_UP();
	wl_invoke_iscan(wiphy_to_wl(wiphy));

	return err;
}

static s32 wl_cfg80211_suspend(struct wiphy *wiphy)
{
	struct wl_priv *wl = wiphy_to_wl(wiphy);
	struct net_device *ndev = wl_to_ndev(wl);
	s32 err = 0;

	CHECK_SYS_UP();

	set_bit(WL_STATUS_SCAN_ABORTING, &wl->status);
	wl_term_iscan(wl);
	if (wl->scan_request) {
		cfg80211_scan_done(wl->scan_request, true);	/* true means
								 abort */
		wl_set_mpc(ndev, 1);
		wl->scan_request = NULL;
	}
	clear_bit(WL_STATUS_SCANNING, &wl->status);
	clear_bit(WL_STATUS_SCAN_ABORTING, &wl->status);

	return err;
}

static __used s32
wl_update_pmklist(struct net_device *dev, struct wl_pmk_list *pmk_list,
		  s32 err)
{
	int i, j;

	WL_DBG("No of elements %d\n", pmk_list->pmkids.npmkid);
	for (i = 0; i < pmk_list->pmkids.npmkid; i++) {
		WL_DBG("PMKID[%d]: %pM =\n", i,
			&pmk_list->pmkids.pmkid[i].BSSID);
		for (j = 0; j < WLAN_PMKID_LEN; j++) {
			WL_DBG("%02x\n", pmk_list->pmkids.pmkid[i].PMKID[j]);
		}
	}
	if (likely(!err)) {
		err = wl_dev_bufvar_set(dev, "pmkid_info", (char *)pmk_list,
					sizeof(*pmk_list));
	}

	return err;
}

static s32
wl_cfg80211_set_pmksa(struct wiphy *wiphy, struct net_device *dev,
		      struct cfg80211_pmksa *pmksa)
{
	struct wl_priv *wl = wiphy_to_wl(wiphy);
	s32 err = 0;
	int i;

	CHECK_SYS_UP();
	for (i = 0; i < wl->pmk_list->pmkids.npmkid; i++)
		if (!memcmp(pmksa->bssid, &wl->pmk_list->pmkids.pmkid[i].BSSID,
			    ETH_ALEN))
			break;
	if (i < WL_NUM_PMKIDS_MAX) {
		memcpy(&wl->pmk_list->pmkids.pmkid[i].BSSID, pmksa->bssid,
		       ETH_ALEN);
		memcpy(&wl->pmk_list->pmkids.pmkid[i].PMKID, pmksa->pmkid,
		       WLAN_PMKID_LEN);
		if (i == wl->pmk_list->pmkids.npmkid)
			wl->pmk_list->pmkids.npmkid++;
	} else {
		err = -EINVAL;
	}
	WL_DBG("set_pmksa,IW_PMKSA_ADD - PMKID: %pM =\n",
	       &wl->pmk_list->pmkids.pmkid[wl->pmk_list->pmkids.npmkid].BSSID);
	for (i = 0; i < WLAN_PMKID_LEN; i++) {
		WL_DBG("%02x\n",
		       wl->pmk_list->pmkids.pmkid[wl->pmk_list->pmkids.npmkid].
		       PMKID[i]);
	}

	err = wl_update_pmklist(dev, wl->pmk_list, err);

	return err;
}

static s32
wl_cfg80211_del_pmksa(struct wiphy *wiphy, struct net_device *dev,
		      struct cfg80211_pmksa *pmksa)
{
	struct wl_priv *wl = wiphy_to_wl(wiphy);
	struct _pmkid_list pmkid;
	s32 err = 0;
	int i;

	CHECK_SYS_UP();
	memcpy(&pmkid.pmkid[0].BSSID, pmksa->bssid, ETH_ALEN);
	memcpy(&pmkid.pmkid[0].PMKID, pmksa->pmkid, WLAN_PMKID_LEN);

	WL_DBG("del_pmksa,IW_PMKSA_REMOVE - PMKID: %pM =\n",
	       &pmkid.pmkid[0].BSSID);
	for (i = 0; i < WLAN_PMKID_LEN; i++) {
		WL_DBG("%02x\n", pmkid.pmkid[0].PMKID[i]);
	}

	for (i = 0; i < wl->pmk_list->pmkids.npmkid; i++)
		if (!memcmp
		    (pmksa->bssid, &wl->pmk_list->pmkids.pmkid[i].BSSID,
		     ETH_ALEN))
			break;

	if ((wl->pmk_list->pmkids.npmkid > 0)
	    && (i < wl->pmk_list->pmkids.npmkid)) {
		memset(&wl->pmk_list->pmkids.pmkid[i], 0, sizeof(pmkid_t));
		for (; i < (wl->pmk_list->pmkids.npmkid - 1); i++) {
			memcpy(&wl->pmk_list->pmkids.pmkid[i].BSSID,
			       &wl->pmk_list->pmkids.pmkid[i + 1].BSSID,
			       ETH_ALEN);
			memcpy(&wl->pmk_list->pmkids.pmkid[i].PMKID,
			       &wl->pmk_list->pmkids.pmkid[i + 1].PMKID,
			       WLAN_PMKID_LEN);
		}
		wl->pmk_list->pmkids.npmkid--;
	} else {
		err = -EINVAL;
	}

	err = wl_update_pmklist(dev, wl->pmk_list, err);

	return err;

}

static s32
wl_cfg80211_flush_pmksa(struct wiphy *wiphy, struct net_device *dev)
{
	struct wl_priv *wl = wiphy_to_wl(wiphy);
	s32 err = 0;

	CHECK_SYS_UP();
	memset(wl->pmk_list, 0, sizeof(*wl->pmk_list));
	err = wl_update_pmklist(dev, wl->pmk_list, err);
	return err;

}

static struct cfg80211_ops wl_cfg80211_ops = {
	.change_virtual_intf = wl_cfg80211_change_iface,
	.scan = wl_cfg80211_scan,
	.set_wiphy_params = wl_cfg80211_set_wiphy_params,
	.join_ibss = wl_cfg80211_join_ibss,
	.leave_ibss = wl_cfg80211_leave_ibss,
	.get_station = wl_cfg80211_get_station,
	.set_tx_power = wl_cfg80211_set_tx_power,
	.get_tx_power = wl_cfg80211_get_tx_power,
	.add_key = wl_cfg80211_add_key,
	.del_key = wl_cfg80211_del_key,
	.get_key = wl_cfg80211_get_key,
	.set_default_key = wl_cfg80211_config_default_key,
	.set_default_mgmt_key = wl_cfg80211_config_default_mgmt_key,
	.set_power_mgmt = wl_cfg80211_set_power_mgmt,
	.set_bitrate_mask = wl_cfg80211_set_bitrate_mask,
	.connect = wl_cfg80211_connect,
	.disconnect = wl_cfg80211_disconnect,
	.suspend = wl_cfg80211_suspend,
	.resume = wl_cfg80211_resume,
	.set_pmksa = wl_cfg80211_set_pmksa,
	.del_pmksa = wl_cfg80211_del_pmksa,
	.flush_pmksa = wl_cfg80211_flush_pmksa
};

static s32 wl_mode_to_nl80211_iftype(s32 mode)
{
	s32 err = 0;

	switch (mode) {
	case WL_MODE_BSS:
		return NL80211_IFTYPE_STATION;
	case WL_MODE_IBSS:
		return NL80211_IFTYPE_ADHOC;
	default:
		return NL80211_IFTYPE_UNSPECIFIED;
	}

	return err;
}

static struct wireless_dev *wl_alloc_wdev(s32 sizeof_iface,
					  struct device *dev)
{
	struct wireless_dev *wdev;
	s32 err = 0;

	wdev = kzalloc(sizeof(*wdev), GFP_KERNEL);
	if (unlikely(!wdev)) {
		WL_ERR("Could not allocate wireless device\n");
		return ERR_PTR(-ENOMEM);
	}
	wdev->wiphy =
	    wiphy_new(&wl_cfg80211_ops, sizeof(struct wl_priv) + sizeof_iface);
	if (unlikely(!wdev->wiphy)) {
		WL_ERR("Couldn not allocate wiphy device\n");
		err = -ENOMEM;
		goto wiphy_new_out;
	}
	set_wiphy_dev(wdev->wiphy, dev);
	wdev->wiphy->max_scan_ssids = WL_NUM_SCAN_MAX;
	wdev->wiphy->max_num_pmkids = WL_NUM_PMKIDS_MAX;
	wdev->wiphy->interface_modes =
	    BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_ADHOC);
	wdev->wiphy->bands[IEEE80211_BAND_2GHZ] = &__wl_band_2ghz;
	wdev->wiphy->bands[IEEE80211_BAND_5GHZ] = &__wl_band_5ghz_a;	/* Set
						* it as 11a by default.
						* This will be updated with
						* 11n phy tables in
						* "ifconfig up"
						* if phy has 11n capability
						*/
	wdev->wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	wdev->wiphy->cipher_suites = __wl_cipher_suites;
	wdev->wiphy->n_cipher_suites = ARRAY_SIZE(__wl_cipher_suites);
#ifndef WL_POWERSAVE_DISABLED
	wdev->wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;	/* enable power
								 * save mode
								 * by default
								 */
#else
	wdev->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;
#endif				/* !WL_POWERSAVE_DISABLED */
	err = wiphy_register(wdev->wiphy);
	if (unlikely(err < 0)) {
		WL_ERR("Couldn not register wiphy device (%d)\n", err);
		goto wiphy_register_out;
	}
	return wdev;

wiphy_register_out:
	wiphy_free(wdev->wiphy);

wiphy_new_out:
	kfree(wdev);

	return ERR_PTR(err);
}

static void wl_free_wdev(struct wl_priv *wl)
{
	struct wireless_dev *wdev = wl_to_wdev(wl);

	if (unlikely(!wdev)) {
		WL_ERR("wdev is invalid\n");
		return;
	}
	wiphy_unregister(wdev->wiphy);
	wiphy_free(wdev->wiphy);
	kfree(wdev);
	wl_to_wdev(wl) = NULL;
}

static s32 wl_inform_bss(struct wl_priv *wl)
{
	struct wl_scan_results *bss_list;
	struct wl_bss_info *bi = NULL;	/* must be initialized */
	s32 err = 0;
	int i;

	bss_list = wl->bss_list;
	if (unlikely(bss_list->version != WL_BSS_INFO_VERSION)) {
		WL_ERR("Version %d != WL_BSS_INFO_VERSION\n",
		       bss_list->version);
		return -EOPNOTSUPP;
	}
	WL_DBG("scanned AP count (%d)\n", bss_list->count);
	bi = next_bss(bss_list, bi);
	for_each_bss(bss_list, bi, i) {
		err = wl_inform_single_bss(wl, bi);
		if (unlikely(err))
			break;
	}
	return err;
}

static s32 wl_inform_single_bss(struct wl_priv *wl, struct wl_bss_info *bi)
{
	struct wiphy *wiphy = wl_to_wiphy(wl);
	struct ieee80211_mgmt *mgmt;
	struct ieee80211_channel *channel;
	struct ieee80211_supported_band *band;
	struct wl_cfg80211_bss_info *notif_bss_info;
	struct wl_scan_req *sr = wl_to_sr(wl);
	struct beacon_proberesp *beacon_proberesp;
	s32 mgmt_type;
	u32 signal;
	u32 freq;
	s32 err = 0;

	if (unlikely(le32_to_cpu(bi->length) > WL_BSS_INFO_MAX)) {
		WL_DBG("Beacon is larger than buffer. Discarding\n");
		return err;
	}
	notif_bss_info =
	    kzalloc(sizeof(*notif_bss_info) + sizeof(*mgmt) - sizeof(u8) +
		    WL_BSS_INFO_MAX, GFP_KERNEL);
	if (unlikely(!notif_bss_info)) {
		WL_ERR("notif_bss_info alloc failed\n");
		return -ENOMEM;
	}
	mgmt = (struct ieee80211_mgmt *)notif_bss_info->frame_buf;
	notif_bss_info->channel =
		bi->ctl_ch ? bi->ctl_ch : CHSPEC_CHANNEL(bi->chanspec);

	if (notif_bss_info->channel <= CH_MAX_2G_CHANNEL)
		band = wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		band = wiphy->bands[IEEE80211_BAND_5GHZ];
	notif_bss_info->rssi = bi->RSSI;
	memcpy(mgmt->bssid, &bi->BSSID, ETH_ALEN);
	mgmt_type = wl->active_scan ?
		IEEE80211_STYPE_PROBE_RESP : IEEE80211_STYPE_BEACON;
	if (!memcmp(bi->SSID, sr->ssid.SSID, bi->SSID_len)) {
		mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
							mgmt_type);
	}
	beacon_proberesp = wl->active_scan ?
		(struct beacon_proberesp *)&mgmt->u.probe_resp :
		(struct beacon_proberesp *)&mgmt->u.beacon;
	beacon_proberesp->timestamp = 0;
	beacon_proberesp->beacon_int = cpu_to_le16(bi->beacon_period);
	beacon_proberesp->capab_info = cpu_to_le16(bi->capability);
	wl_rst_ie(wl);
	/*
	* wl_add_ie is not necessary because it can only add duplicated
	* SSID, rate information to frame_buf
	*/
	/*
	* wl_add_ie(wl, WLAN_EID_SSID, bi->SSID_len, bi->SSID);
	* wl_add_ie(wl, WLAN_EID_SUPP_RATES, bi->rateset.count,
	* bi->rateset.rates);
	*/
	wl_mrg_ie(wl, ((u8 *) bi) + bi->ie_offset, bi->ie_length);
	wl_cp_ie(wl, beacon_proberesp->variable, WL_BSS_INFO_MAX -
		 offsetof(struct wl_cfg80211_bss_info, frame_buf));
	notif_bss_info->frame_len =
	    offsetof(struct ieee80211_mgmt,
		     u.beacon.variable) + wl_get_ielen(wl);
	freq = ieee80211_channel_to_frequency(notif_bss_info->channel);
	channel = ieee80211_get_channel(wiphy, freq);

	WL_DBG("SSID : \"%s\", rssi %d, channel %d, capability : 0x04%x, bssid %pM\n",
	       bi->SSID,
	       notif_bss_info->rssi, notif_bss_info->channel,
	       mgmt->u.beacon.capab_info, &bi->BSSID);

	signal = notif_bss_info->rssi * 100;
	if (unlikely(!cfg80211_inform_bss_frame(wiphy, channel, mgmt,
						le16_to_cpu
						(notif_bss_info->frame_len),
						signal, GFP_KERNEL))) {
		WL_ERR("cfg80211_inform_bss_frame error\n");
		kfree(notif_bss_info);
		return -EINVAL;
	}
	kfree(notif_bss_info);

	return err;
}

static bool wl_is_linkup(struct wl_priv *wl, const wl_event_msg_t *e)
{
	u32 event = be32_to_cpu(e->event_type);
	u16 flags = be16_to_cpu(e->flags);

	if (event == WLC_E_LINK) {
		if (flags & WLC_EVENT_MSG_LINK) {
			if (wl_is_ibssmode(wl)) {
				if (wl_is_ibssstarter(wl)) {
				}
			} else {
				return true;
			}
		}
	}

	return false;
}

static bool wl_is_linkdown(struct wl_priv *wl, const wl_event_msg_t *e)
{
	u32 event = be32_to_cpu(e->event_type);
	u16 flags = be16_to_cpu(e->flags);

	if (event == WLC_E_DEAUTH_IND || event == WLC_E_DISASSOC_IND) {
		return true;
	} else if (event == WLC_E_LINK) {
		if (!(flags & WLC_EVENT_MSG_LINK))
			return true;
	}

	return false;
}

static bool wl_is_nonetwork(struct wl_priv *wl, const wl_event_msg_t *e)
{
	u32 event = be32_to_cpu(e->event_type);
	u32 status = be32_to_cpu(e->status);

	if (event == WLC_E_SET_SSID || event == WLC_E_LINK) {
		if (status == WLC_E_STATUS_NO_NETWORKS)
			return true;
	}

	return false;
}

static s32
wl_notify_connect_status(struct wl_priv *wl, struct net_device *ndev,
			 const wl_event_msg_t *e, void *data)
{
	bool act;
	s32 err = 0;

	if (wl_is_linkup(wl, e)) {
		wl_link_up(wl);
		if (wl_is_ibssmode(wl)) {
			cfg80211_ibss_joined(ndev, (s8 *)&e->addr,
					     GFP_KERNEL);
			WL_DBG("joined in IBSS network\n");
		} else {
			wl_bss_connect_done(wl, ndev, e, data, true);
			WL_DBG("joined in BSS network \"%s\"\n",
			       ((struct wlc_ssid *)
				wl_read_prof(wl, WL_PROF_SSID))->SSID);
		}
		act = true;
		wl_update_prof(wl, e, &act, WL_PROF_ACT);
	} else if (wl_is_linkdown(wl, e)) {
		cfg80211_disconnected(ndev, 0, NULL, 0, GFP_KERNEL);
		clear_bit(WL_STATUS_CONNECTED, &wl->status);
		wl_link_down(wl);
		wl_init_prof(wl->profile);
	} else if (wl_is_nonetwork(wl, e)) {
		wl_bss_connect_done(wl, ndev, e, data, false);
	}

	return err;
}

static s32
wl_notify_roaming_status(struct wl_priv *wl, struct net_device *ndev,
			 const wl_event_msg_t *e, void *data)
{
	bool act;
	s32 err = 0;

	wl_bss_roaming_done(wl, ndev, e, data);
	act = true;
	wl_update_prof(wl, e, &act, WL_PROF_ACT);

	return err;
}

static __used s32
wl_dev_bufvar_set(struct net_device *dev, s8 *name, s8 *buf, s32 len)
{
	struct wl_priv *wl = ndev_to_wl(dev);
	u32 buflen;

	buflen = bcm_mkiovar(name, buf, len, wl->ioctl_buf, WL_IOCTL_LEN_MAX);
	BUG_ON(!buflen);

	return wl_dev_ioctl(dev, WLC_SET_VAR, wl->ioctl_buf, buflen);
}

static s32
wl_dev_bufvar_get(struct net_device *dev, s8 *name, s8 *buf,
		  s32 buf_len)
{
	struct wl_priv *wl = ndev_to_wl(dev);
	u32 len;
	s32 err = 0;

	len = bcm_mkiovar(name, NULL, 0, wl->ioctl_buf, WL_IOCTL_LEN_MAX);
	BUG_ON(!len);
	err = wl_dev_ioctl(dev, WLC_GET_VAR, (void *)wl->ioctl_buf,
			WL_IOCTL_LEN_MAX);
	if (unlikely(err)) {
		WL_ERR("error (%d)\n", err);
		return err;
	}
	memcpy(buf, wl->ioctl_buf, buf_len);

	return err;
}

static s32 wl_get_assoc_ies(struct wl_priv *wl)
{
	struct net_device *ndev = wl_to_ndev(wl);
	struct wl_assoc_ielen *assoc_info;
	struct wl_connect_info *conn_info = wl_to_conn(wl);
	u32 req_len;
	u32 resp_len;
	s32 err = 0;

	err = wl_dev_bufvar_get(ndev, "assoc_info", wl->extra_buf,
				WL_ASSOC_INFO_MAX);
	if (unlikely(err)) {
		WL_ERR("could not get assoc info (%d)\n", err);
		return err;
	}
	assoc_info = (struct wl_assoc_ielen *)wl->extra_buf;
	req_len = assoc_info->req_len;
	resp_len = assoc_info->resp_len;
	if (req_len) {
		err = wl_dev_bufvar_get(ndev, "assoc_req_ies", wl->extra_buf,
					WL_ASSOC_INFO_MAX);
		if (unlikely(err)) {
			WL_ERR("could not get assoc req (%d)\n", err);
			return err;
		}
		conn_info->req_ie_len = req_len;
		conn_info->req_ie =
		    kmemdup(wl->extra_buf, conn_info->req_ie_len, GFP_KERNEL);
	} else {
		conn_info->req_ie_len = 0;
		conn_info->req_ie = NULL;
	}
	if (resp_len) {
		err = wl_dev_bufvar_get(ndev, "assoc_resp_ies", wl->extra_buf,
					WL_ASSOC_INFO_MAX);
		if (unlikely(err)) {
			WL_ERR("could not get assoc resp (%d)\n", err);
			return err;
		}
		conn_info->resp_ie_len = resp_len;
		conn_info->resp_ie =
		    kmemdup(wl->extra_buf, conn_info->resp_ie_len, GFP_KERNEL);
	} else {
		conn_info->resp_ie_len = 0;
		conn_info->resp_ie = NULL;
	}
	WL_DBG("req len (%d) resp len (%d)\n",
	       conn_info->req_ie_len, conn_info->resp_ie_len);

	return err;
}

static void wl_ch_to_chanspec(int ch, struct wl_join_params *join_params,
	size_t *join_params_size)
{
	chanspec_t chanspec = 0;

	if (ch != 0) {
		join_params->params.chanspec_num = 1;
		join_params->params.chanspec_list[0] = ch;

		if (join_params->params.chanspec_list[0])
			chanspec |= WL_CHANSPEC_BAND_2G;
		else
			chanspec |= WL_CHANSPEC_BAND_5G;

		chanspec |= WL_CHANSPEC_BW_20;
		chanspec |= WL_CHANSPEC_CTL_SB_NONE;

		*join_params_size += WL_ASSOC_PARAMS_FIXED_SIZE +
			join_params->params.chanspec_num * sizeof(chanspec_t);

		join_params->params.chanspec_list[0] &= WL_CHANSPEC_CHAN_MASK;
		join_params->params.chanspec_list[0] |= chanspec;
		join_params->params.chanspec_list[0] =
		cpu_to_le16(join_params->params.chanspec_list[0]);

		join_params->params.chanspec_num =
			cpu_to_le32(join_params->params.chanspec_num);

		WL_DBG("join_params->params.chanspec_list[0]= %#X, channel %d, chanspec %#X\n",
		       join_params->params.chanspec_list[0], ch, chanspec);
	}
}

static s32 wl_update_bss_info(struct wl_priv *wl)
{
	struct cfg80211_bss *bss;
	struct wl_bss_info *bi;
	struct wlc_ssid *ssid;
	struct bcm_tlv *tim;
	u16 beacon_interval;
	u8 dtim_period;
	size_t ie_len;
	u8 *ie;
	s32 err = 0;

	if (wl_is_ibssmode(wl))
		return err;

	ssid = (struct wlc_ssid *)wl_read_prof(wl, WL_PROF_SSID);
	bss =
	    cfg80211_get_bss(wl_to_wiphy(wl), NULL, (s8 *)&wl->bssid,
			     ssid->SSID, ssid->SSID_len, WLAN_CAPABILITY_ESS,
			     WLAN_CAPABILITY_ESS);

	rtnl_lock();
	if (unlikely(!bss)) {
		WL_DBG("Could not find the AP\n");
		*(u32 *) wl->extra_buf = cpu_to_le32(WL_EXTRA_BUF_MAX);
		err = wl_dev_ioctl(wl_to_ndev(wl), WLC_GET_BSS_INFO,
				wl->extra_buf, WL_EXTRA_BUF_MAX);
		if (unlikely(err)) {
			WL_ERR("Could not get bss info %d\n", err);
			goto update_bss_info_out;
		}
		bi = (struct wl_bss_info *)(wl->extra_buf + 4);
		if (unlikely(memcmp(&bi->BSSID, &wl->bssid, ETH_ALEN))) {
			err = -EIO;
			goto update_bss_info_out;
		}
		err = wl_inform_single_bss(wl, bi);
		if (unlikely(err))
			goto update_bss_info_out;

		ie = ((u8 *)bi) + bi->ie_offset;
		ie_len = bi->ie_length;
		beacon_interval = cpu_to_le16(bi->beacon_period);
	} else {
		WL_DBG("Found the AP in the list - BSSID %pM\n", bss->bssid);
		ie = bss->information_elements;
		ie_len = bss->len_information_elements;
		beacon_interval = bss->beacon_interval;
		cfg80211_put_bss(bss);
	}

	tim = bcm_parse_tlvs(ie, ie_len, WLAN_EID_TIM);
	if (tim) {
		dtim_period = tim->data[1];
	} else {
		/*
		* active scan was done so we could not get dtim
		* information out of probe response.
		* so we speficially query dtim information to dongle.
		*/
		err = wl_dev_ioctl(wl_to_ndev(wl), WLC_GET_DTIMPRD,
			&dtim_period, sizeof(dtim_period));
		if (unlikely(err)) {
			WL_ERR("WLC_GET_DTIMPRD error (%d)\n", err);
			goto update_bss_info_out;
		}
	}

	wl_update_prof(wl, NULL, &beacon_interval, WL_PROF_BEACONINT);
	wl_update_prof(wl, NULL, &dtim_period, WL_PROF_DTIMPERIOD);

update_bss_info_out:
	rtnl_unlock();
	return err;
}

static s32
wl_bss_roaming_done(struct wl_priv *wl, struct net_device *ndev,
		    const wl_event_msg_t *e, void *data)
{
	struct wl_connect_info *conn_info = wl_to_conn(wl);
	s32 err = 0;

	wl_get_assoc_ies(wl);
	memcpy(&wl->bssid, &e->addr, ETH_ALEN);
	wl_update_bss_info(wl);
	cfg80211_roamed(ndev,
			(u8 *)&wl->bssid,
			conn_info->req_ie, conn_info->req_ie_len,
			conn_info->resp_ie, conn_info->resp_ie_len, GFP_KERNEL);
	WL_DBG("Report roaming result\n");

	set_bit(WL_STATUS_CONNECTED, &wl->status);

	return err;
}

static s32
wl_bss_connect_done(struct wl_priv *wl, struct net_device *ndev,
		    const wl_event_msg_t *e, void *data, bool completed)
{
	struct wl_connect_info *conn_info = wl_to_conn(wl);
	s32 err = 0;

	wl_get_assoc_ies(wl);
	memcpy(&wl->bssid, &e->addr, ETH_ALEN);
	wl_update_bss_info(wl);
	if (test_and_clear_bit(WL_STATUS_CONNECTING, &wl->status)) {
		cfg80211_connect_result(ndev,
					(u8 *)&wl->bssid,
					conn_info->req_ie,
					conn_info->req_ie_len,
					conn_info->resp_ie,
					conn_info->resp_ie_len,
					completed ? WLAN_STATUS_SUCCESS : WLAN_STATUS_AUTH_TIMEOUT,
					GFP_KERNEL);
		WL_DBG("Report connect result - connection %s\n",
		       completed ? "succeeded" : "failed");
	} else {
		cfg80211_roamed(ndev,
				(u8 *)&wl->bssid,
				conn_info->req_ie, conn_info->req_ie_len,
				conn_info->resp_ie, conn_info->resp_ie_len,
				GFP_KERNEL);
		WL_DBG("Report roaming result\n");
	}
	set_bit(WL_STATUS_CONNECTED, &wl->status);

	return err;
}

static s32
wl_notify_mic_status(struct wl_priv *wl, struct net_device *ndev,
		     const wl_event_msg_t *e, void *data)
{
	u16 flags = be16_to_cpu(e->flags);
	enum nl80211_key_type key_type;

	rtnl_lock();
	if (flags & WLC_EVENT_MSG_GROUP)
		key_type = NL80211_KEYTYPE_GROUP;
	else
		key_type = NL80211_KEYTYPE_PAIRWISE;

	cfg80211_michael_mic_failure(ndev, (u8 *)&e->addr, key_type, -1,
				     NULL, GFP_KERNEL);
	rtnl_unlock();

	return 0;
}

static s32
wl_notify_scan_status(struct wl_priv *wl, struct net_device *ndev,
		      const wl_event_msg_t *e, void *data)
{
	struct channel_info channel_inform;
	struct wl_scan_results *bss_list;
	u32 len = WL_SCAN_BUF_MAX;
	s32 err = 0;

	if (wl->iscan_on && wl->iscan_kickstart)
		return wl_wakeup_iscan(wl_to_iscan(wl));

	if (unlikely(!test_and_clear_bit(WL_STATUS_SCANNING, &wl->status))) {
		WL_ERR("Scan complete while device not scanning\n");
		return -EINVAL;
	}
	if (unlikely(!wl->scan_request)) {
	}
	rtnl_lock();
	err = wl_dev_ioctl(ndev, WLC_GET_CHANNEL, &channel_inform,
			sizeof(channel_inform));
	if (unlikely(err)) {
		WL_ERR("scan busy (%d)\n", err);
		goto scan_done_out;
	}
	channel_inform.scan_channel = le32_to_cpu(channel_inform.scan_channel);
	if (unlikely(channel_inform.scan_channel)) {

		WL_DBG("channel_inform.scan_channel (%d)\n",
		       channel_inform.scan_channel);
	}
	wl->bss_list = wl->scan_results;
	bss_list = wl->bss_list;
	memset(bss_list, 0, len);
	bss_list->buflen = cpu_to_le32(len);
	err = wl_dev_ioctl(ndev, WLC_SCAN_RESULTS, bss_list, len);
	if (unlikely(err)) {
		WL_ERR("%s Scan_results error (%d)\n", ndev->name, err);
		err = -EINVAL;
		goto scan_done_out;
	}
	bss_list->buflen = le32_to_cpu(bss_list->buflen);
	bss_list->version = le32_to_cpu(bss_list->version);
	bss_list->count = le32_to_cpu(bss_list->count);

	err = wl_inform_bss(wl);
	if (err)
		goto scan_done_out;

scan_done_out:
	if (wl->scan_request) {
		cfg80211_scan_done(wl->scan_request, false);
		wl_set_mpc(ndev, 1);
		wl->scan_request = NULL;
	}
	rtnl_unlock();
	return err;
}

static void wl_init_conf(struct wl_conf *conf)
{
	conf->mode = (u32)-1;
	conf->frag_threshold = (u32)-1;
	conf->rts_threshold = (u32)-1;
	conf->retry_short = (u32)-1;
	conf->retry_long = (u32)-1;
	conf->tx_power = -1;
}

static void wl_init_prof(struct wl_profile *prof)
{
	memset(prof, 0, sizeof(*prof));
}

static void wl_init_eloop_handler(struct wl_event_loop *el)
{
	memset(el, 0, sizeof(*el));
	el->handler[WLC_E_SCAN_COMPLETE] = wl_notify_scan_status;
	el->handler[WLC_E_JOIN] = wl_notify_connect_status;
	el->handler[WLC_E_LINK] = wl_notify_connect_status;
	el->handler[WLC_E_DEAUTH_IND] = wl_notify_connect_status;
	el->handler[WLC_E_DISASSOC_IND] = wl_notify_connect_status;
	el->handler[WLC_E_ASSOC_IND] = wl_notify_connect_status;
	el->handler[WLC_E_REASSOC_IND] = wl_notify_connect_status;
	el->handler[WLC_E_ROAM] = wl_notify_roaming_status;
	el->handler[WLC_E_MIC_ERROR] = wl_notify_mic_status;
	el->handler[WLC_E_SET_SSID] = wl_notify_connect_status;
}

static s32 wl_init_priv_mem(struct wl_priv *wl)
{
	wl->scan_results = kzalloc(WL_SCAN_BUF_MAX, GFP_KERNEL);
	if (unlikely(!wl->scan_results)) {
		WL_ERR("Scan results alloc failed\n");
		goto init_priv_mem_out;
	}
	wl->conf = kzalloc(sizeof(*wl->conf), GFP_KERNEL);
	if (unlikely(!wl->conf)) {
		WL_ERR("wl_conf alloc failed\n");
		goto init_priv_mem_out;
	}
	wl->profile = kzalloc(sizeof(*wl->profile), GFP_KERNEL);
	if (unlikely(!wl->profile)) {
		WL_ERR("wl_profile alloc failed\n");
		goto init_priv_mem_out;
	}
	wl->bss_info = kzalloc(WL_BSS_INFO_MAX, GFP_KERNEL);
	if (unlikely(!wl->bss_info)) {
		WL_ERR("Bss information alloc failed\n");
		goto init_priv_mem_out;
	}
	wl->scan_req_int = kzalloc(sizeof(*wl->scan_req_int), GFP_KERNEL);
	if (unlikely(!wl->scan_req_int)) {
		WL_ERR("Scan req alloc failed\n");
		goto init_priv_mem_out;
	}
	wl->ioctl_buf = kzalloc(WL_IOCTL_LEN_MAX, GFP_KERNEL);
	if (unlikely(!wl->ioctl_buf)) {
		WL_ERR("Ioctl buf alloc failed\n");
		goto init_priv_mem_out;
	}
	wl->extra_buf = kzalloc(WL_EXTRA_BUF_MAX, GFP_KERNEL);
	if (unlikely(!wl->extra_buf)) {
		WL_ERR("Extra buf alloc failed\n");
		goto init_priv_mem_out;
	}
	wl->iscan = kzalloc(sizeof(*wl->iscan), GFP_KERNEL);
	if (unlikely(!wl->iscan)) {
		WL_ERR("Iscan buf alloc failed\n");
		goto init_priv_mem_out;
	}
	wl->fw = kzalloc(sizeof(*wl->fw), GFP_KERNEL);
	if (unlikely(!wl->fw)) {
		WL_ERR("fw object alloc failed\n");
		goto init_priv_mem_out;
	}
	wl->pmk_list = kzalloc(sizeof(*wl->pmk_list), GFP_KERNEL);
	if (unlikely(!wl->pmk_list)) {
		WL_ERR("pmk list alloc failed\n");
		goto init_priv_mem_out;
	}

	return 0;

init_priv_mem_out:
	wl_deinit_priv_mem(wl);

	return -ENOMEM;
}

static void wl_deinit_priv_mem(struct wl_priv *wl)
{
	kfree(wl->scan_results);
	wl->scan_results = NULL;
	kfree(wl->bss_info);
	wl->bss_info = NULL;
	kfree(wl->conf);
	wl->conf = NULL;
	kfree(wl->profile);
	wl->profile = NULL;
	kfree(wl->scan_req_int);
	wl->scan_req_int = NULL;
	kfree(wl->ioctl_buf);
	wl->ioctl_buf = NULL;
	kfree(wl->extra_buf);
	wl->extra_buf = NULL;
	kfree(wl->iscan);
	wl->iscan = NULL;
	kfree(wl->fw);
	wl->fw = NULL;
	kfree(wl->pmk_list);
	wl->pmk_list = NULL;
}

static s32 wl_create_event_handler(struct wl_priv *wl)
{
	sema_init(&wl->event_sync, 0);
	wl->event_tsk = kthread_run(wl_event_handler, wl, "wl_event_handler");
	if (IS_ERR(wl->event_tsk)) {
		wl->event_tsk = NULL;
		WL_ERR("failed to create event thread\n");
		return -ENOMEM;
	}
	return 0;
}

static void wl_destroy_event_handler(struct wl_priv *wl)
{
	if (wl->event_tsk) {
		send_sig(SIGTERM, wl->event_tsk, 1);
		kthread_stop(wl->event_tsk);
		wl->event_tsk = NULL;
	}
}

static void wl_term_iscan(struct wl_priv *wl)
{
	struct wl_iscan_ctrl *iscan = wl_to_iscan(wl);

	if (wl->iscan_on && iscan->tsk) {
		iscan->state = WL_ISCAN_STATE_IDLE;
		send_sig(SIGTERM, iscan->tsk, 1);
		kthread_stop(iscan->tsk);
		iscan->tsk = NULL;
	}
}

static void wl_notify_iscan_complete(struct wl_iscan_ctrl *iscan, bool aborted)
{
	struct wl_priv *wl = iscan_to_wl(iscan);
	struct net_device *ndev = wl_to_ndev(wl);

	if (unlikely(!test_and_clear_bit(WL_STATUS_SCANNING, &wl->status))) {
		WL_ERR("Scan complete while device not scanning\n");
		return;
	}
	if (likely(wl->scan_request)) {
		cfg80211_scan_done(wl->scan_request, aborted);
		wl_set_mpc(ndev, 1);
		wl->scan_request = NULL;
	}
	wl->iscan_kickstart = false;
}

static s32 wl_wakeup_iscan(struct wl_iscan_ctrl *iscan)
{
	if (likely(iscan->state != WL_ISCAN_STATE_IDLE)) {
		WL_DBG("wake up iscan\n");
		up(&iscan->sync);
		return 0;
	}

	return -EIO;
}

static s32
wl_get_iscan_results(struct wl_iscan_ctrl *iscan, u32 *status,
		     struct wl_scan_results **bss_list)
{
	struct wl_iscan_results list;
	struct wl_scan_results *results;
	struct wl_iscan_results *list_buf;
	s32 err = 0;

	memset(iscan->scan_buf, 0, WL_ISCAN_BUF_MAX);
	list_buf = (struct wl_iscan_results *)iscan->scan_buf;
	results = &list_buf->results;
	results->buflen = WL_ISCAN_RESULTS_FIXED_SIZE;
	results->version = 0;
	results->count = 0;

	memset(&list, 0, sizeof(list));
	list.results.buflen = cpu_to_le32(WL_ISCAN_BUF_MAX);
	err = wl_dev_iovar_getbuf(iscan->dev, "iscanresults", &list,
				WL_ISCAN_RESULTS_FIXED_SIZE, iscan->scan_buf,
				WL_ISCAN_BUF_MAX);
	if (unlikely(err)) {
		WL_ERR("error (%d)\n", err);
		return err;
	}
	results->buflen = le32_to_cpu(results->buflen);
	results->version = le32_to_cpu(results->version);
	results->count = le32_to_cpu(results->count);
	WL_DBG("results->count = %d\n", results->count);
	WL_DBG("results->buflen = %d\n", results->buflen);
	*status = le32_to_cpu(list_buf->status);
	*bss_list = results;

	return err;
}

static s32 wl_iscan_done(struct wl_priv *wl)
{
	struct wl_iscan_ctrl *iscan = wl->iscan;
	s32 err = 0;

	iscan->state = WL_ISCAN_STATE_IDLE;
	rtnl_lock();
	wl_inform_bss(wl);
	wl_notify_iscan_complete(iscan, false);
	rtnl_unlock();

	return err;
}

static s32 wl_iscan_pending(struct wl_priv *wl)
{
	struct wl_iscan_ctrl *iscan = wl->iscan;
	s32 err = 0;

	/* Reschedule the timer */
	mod_timer(&iscan->timer, jiffies + iscan->timer_ms * HZ / 1000);
	iscan->timer_on = 1;

	return err;
}

static s32 wl_iscan_inprogress(struct wl_priv *wl)
{
	struct wl_iscan_ctrl *iscan = wl->iscan;
	s32 err = 0;

	rtnl_lock();
	wl_inform_bss(wl);
	wl_run_iscan(iscan, NULL, WL_SCAN_ACTION_CONTINUE);
	rtnl_unlock();
	/* Reschedule the timer */
	mod_timer(&iscan->timer, jiffies + iscan->timer_ms * HZ / 1000);
	iscan->timer_on = 1;

	return err;
}

static s32 wl_iscan_aborted(struct wl_priv *wl)
{
	struct wl_iscan_ctrl *iscan = wl->iscan;
	s32 err = 0;

	iscan->state = WL_ISCAN_STATE_IDLE;
	rtnl_lock();
	wl_notify_iscan_complete(iscan, true);
	rtnl_unlock();

	return err;
}

static s32 wl_iscan_thread(void *data)
{
	struct sched_param param = {.sched_priority = MAX_RT_PRIO - 1 };
	struct wl_iscan_ctrl *iscan = (struct wl_iscan_ctrl *)data;
	struct wl_priv *wl = iscan_to_wl(iscan);
	struct wl_iscan_eloop *el = &iscan->el;
	u32 status;
	int err = 0;

	sched_setscheduler(current, SCHED_FIFO, &param);
	allow_signal(SIGTERM);
	status = WL_SCAN_RESULTS_PARTIAL;
	while (likely(!down_interruptible(&iscan->sync))) {
		if (kthread_should_stop())
			break;
		if (iscan->timer_on) {
			del_timer_sync(&iscan->timer);
			iscan->timer_on = 0;
		}
		rtnl_lock();
		err = wl_get_iscan_results(iscan, &status, &wl->bss_list);
		if (unlikely(err)) {
			status = WL_SCAN_RESULTS_ABORTED;
			WL_ERR("Abort iscan\n");
		}
		rtnl_unlock();
		el->handler[status] (wl);
	}
	if (iscan->timer_on) {
		del_timer_sync(&iscan->timer);
		iscan->timer_on = 0;
	}
	WL_DBG("%s was terminated\n", __func__);

	return 0;
}

static void wl_iscan_timer(unsigned long data)
{
	struct wl_iscan_ctrl *iscan = (struct wl_iscan_ctrl *)data;

	if (iscan) {
		iscan->timer_on = 0;
		WL_DBG("timer expired\n");
		wl_wakeup_iscan(iscan);
	}
}

static s32 wl_invoke_iscan(struct wl_priv *wl)
{
	struct wl_iscan_ctrl *iscan = wl_to_iscan(wl);
	int err = 0;

	if (wl->iscan_on && !iscan->tsk) {
		iscan->state = WL_ISCAN_STATE_IDLE;
		sema_init(&iscan->sync, 0);
		iscan->tsk = kthread_run(wl_iscan_thread, iscan, "wl_iscan");
		if (IS_ERR(iscan->tsk)) {
			WL_ERR("Could not create iscan thread\n");
			iscan->tsk = NULL;
			return -ENOMEM;
		}
	}

	return err;
}

static void wl_init_iscan_eloop(struct wl_iscan_eloop *el)
{
	memset(el, 0, sizeof(*el));
	el->handler[WL_SCAN_RESULTS_SUCCESS] = wl_iscan_done;
	el->handler[WL_SCAN_RESULTS_PARTIAL] = wl_iscan_inprogress;
	el->handler[WL_SCAN_RESULTS_PENDING] = wl_iscan_pending;
	el->handler[WL_SCAN_RESULTS_ABORTED] = wl_iscan_aborted;
	el->handler[WL_SCAN_RESULTS_NO_MEM] = wl_iscan_aborted;
}

static s32 wl_init_iscan(struct wl_priv *wl)
{
	struct wl_iscan_ctrl *iscan = wl_to_iscan(wl);
	int err = 0;

	if (wl->iscan_on) {
		iscan->dev = wl_to_ndev(wl);
		iscan->state = WL_ISCAN_STATE_IDLE;
		wl_init_iscan_eloop(&iscan->el);
		iscan->timer_ms = WL_ISCAN_TIMER_INTERVAL_MS;
		init_timer(&iscan->timer);
		iscan->timer.data = (unsigned long) iscan;
		iscan->timer.function = wl_iscan_timer;
		sema_init(&iscan->sync, 0);
		iscan->tsk = kthread_run(wl_iscan_thread, iscan, "wl_iscan");
		if (IS_ERR(iscan->tsk)) {
			WL_ERR("Could not create iscan thread\n");
			iscan->tsk = NULL;
			return -ENOMEM;
		}
		iscan->data = wl;
	}

	return err;
}

static void wl_init_fw(struct wl_fw_ctrl *fw)
{
	fw->status = 0;		/* init fw loading status.
				 0 means nothing was loaded yet */
}

static s32 wl_init_priv(struct wl_priv *wl)
{
	struct wiphy *wiphy = wl_to_wiphy(wl);
	s32 err = 0;

	wl->scan_request = NULL;
	wl->pwr_save = !!(wiphy->flags & WIPHY_FLAG_PS_ON_BY_DEFAULT);
	wl->iscan_on = true;	/* iscan on & off switch.
				 we enable iscan per default */
	wl->roam_on = false;	/* roam on & off switch.
				 we enable roam per default */

	wl->iscan_kickstart = false;
	wl->active_scan = true;	/* we do active scan for
				 specific scan per default */
	wl->dongle_up = false;	/* dongle is not up yet */
	wl_init_eq(wl);
	err = wl_init_priv_mem(wl);
	if (unlikely(err))
		return err;
	if (unlikely(wl_create_event_handler(wl)))
		return -ENOMEM;
	wl_init_eloop_handler(&wl->el);
	mutex_init(&wl->usr_sync);
	err = wl_init_iscan(wl);
	if (unlikely(err))
		return err;
	wl_init_fw(wl->fw);
	wl_init_conf(wl->conf);
	wl_init_prof(wl->profile);
	wl_link_down(wl);

	return err;
}

static void wl_deinit_priv(struct wl_priv *wl)
{
	wl_destroy_event_handler(wl);
	wl->dongle_up = false;	/* dongle down */
	wl_flush_eq(wl);
	wl_link_down(wl);
	wl_term_iscan(wl);
	wl_deinit_priv_mem(wl);
}

s32 wl_cfg80211_attach(struct net_device *ndev, void *data)
{
	struct wireless_dev *wdev;
	struct wl_priv *wl;
	struct wl_iface *ci;
	s32 err = 0;

	if (unlikely(!ndev)) {
		WL_ERR("ndev is invalid\n");
		return -ENODEV;
	}
	wl_cfg80211_dev = kzalloc(sizeof(struct wl_dev), GFP_KERNEL);
	if (unlikely(!wl_cfg80211_dev)) {
		WL_ERR("wl_cfg80211_dev is invalid\n");
		return -ENOMEM;
	}
	WL_DBG("func %p\n", wl_cfg80211_get_sdio_func());
	wdev = wl_alloc_wdev(sizeof(struct wl_iface), &wl_cfg80211_get_sdio_func()->dev);
	if (IS_ERR(wdev))
		return -ENOMEM;

	wdev->iftype = wl_mode_to_nl80211_iftype(WL_MODE_BSS);
	wl = wdev_to_wl(wdev);
	wl->wdev = wdev;
	wl->pub = data;
	ci = (struct wl_iface *)wl_to_ci(wl);
	ci->wl = wl;
	ndev->ieee80211_ptr = wdev;
	SET_NETDEV_DEV(ndev, wiphy_dev(wdev->wiphy));
	wdev->netdev = ndev;
	err = wl_init_priv(wl);
	if (unlikely(err)) {
		WL_ERR("Failed to init iwm_priv (%d)\n", err);
		goto cfg80211_attach_out;
	}
	wl_set_drvdata(wl_cfg80211_dev, ci);
	set_bit(WL_STATUS_READY, &wl->status);

	return err;

cfg80211_attach_out:
	wl_free_wdev(wl);
	return err;
}

void wl_cfg80211_detach(void)
{
	struct wl_priv *wl;

	wl = WL_PRIV_GET();

	wl_deinit_priv(wl);
	wl_free_wdev(wl);
	wl_set_drvdata(wl_cfg80211_dev, NULL);
	kfree(wl_cfg80211_dev);
	wl_cfg80211_dev = NULL;
	wl_clear_sdio_func();
}

static void wl_wakeup_event(struct wl_priv *wl)
{
	up(&wl->event_sync);
}

static s32 wl_event_handler(void *data)
{
	struct wl_priv *wl = (struct wl_priv *)data;
	struct sched_param param = {.sched_priority = MAX_RT_PRIO - 1 };
	struct wl_event_q *e;

	sched_setscheduler(current, SCHED_FIFO, &param);
	allow_signal(SIGTERM);
	while (likely(!down_interruptible(&wl->event_sync))) {
		if (kthread_should_stop())
			break;
		e = wl_deq_event(wl);
		if (unlikely(!e)) {
			WL_ERR("event queue empty...\n");
			BUG();
		}
		WL_DBG("event type (%d)\n", e->etype);
		if (wl->el.handler[e->etype]) {
			wl->el.handler[e->etype] (wl, wl_to_ndev(wl), &e->emsg,
						  e->edata);
		} else {
			WL_DBG("Unknown Event (%d): ignoring\n", e->etype);
		}
		wl_put_event(e);
	}
	WL_DBG("%s was terminated\n", __func__);
	return 0;
}

void
wl_cfg80211_event(struct net_device *ndev, const wl_event_msg_t * e, void *data)
{
	u32 event_type = be32_to_cpu(e->event_type);
	struct wl_priv *wl = ndev_to_wl(ndev);
#if (WL_DBG_LEVEL > 0)
	s8 *estr = (event_type <= sizeof(wl_dbg_estr) / WL_DBG_ESTR_MAX - 1) ?
	    wl_dbg_estr[event_type] : (s8 *) "Unknown";
#endif				/* (WL_DBG_LEVEL > 0) */
	WL_DBG("event_type (%d):" "WLC_E_" "%s\n", event_type, estr);
	if (likely(!wl_enq_event(wl, event_type, e, data)))
		wl_wakeup_event(wl);
}

static void wl_init_eq(struct wl_priv *wl)
{
	wl_init_eq_lock(wl);
	INIT_LIST_HEAD(&wl->eq_list);
}

static void wl_flush_eq(struct wl_priv *wl)
{
	struct wl_event_q *e;

	wl_lock_eq(wl);
	while (!list_empty(&wl->eq_list)) {
		e = list_first_entry(&wl->eq_list, struct wl_event_q, eq_list);
		list_del(&e->eq_list);
		kfree(e);
	}
	wl_unlock_eq(wl);
}

/*
* retrieve first queued event from head
*/

static struct wl_event_q *wl_deq_event(struct wl_priv *wl)
{
	struct wl_event_q *e = NULL;

	wl_lock_eq(wl);
	if (likely(!list_empty(&wl->eq_list))) {
		e = list_first_entry(&wl->eq_list, struct wl_event_q, eq_list);
		list_del(&e->eq_list);
	}
	wl_unlock_eq(wl);

	return e;
}

/*
** push event to tail of the queue
*/

static s32
wl_enq_event(struct wl_priv *wl, u32 event, const wl_event_msg_t *msg,
	     void *data)
{
	struct wl_event_q *e;
	s32 err = 0;

	e = kzalloc(sizeof(struct wl_event_q), GFP_KERNEL);
	if (unlikely(!e)) {
		WL_ERR("event alloc failed\n");
		return -ENOMEM;
	}

	e->etype = event;
	memcpy(&e->emsg, msg, sizeof(wl_event_msg_t));
	if (data) {
	}
	wl_lock_eq(wl);
	list_add_tail(&e->eq_list, &wl->eq_list);
	wl_unlock_eq(wl);

	return err;
}

static void wl_put_event(struct wl_event_q *e)
{
	kfree(e);
}

void wl_cfg80211_sdio_func(void *func)
{
	cfg80211_sdio_func = (struct sdio_func *)func;
}

static void wl_clear_sdio_func(void)
{
	cfg80211_sdio_func = NULL;
}

struct sdio_func *wl_cfg80211_get_sdio_func(void)
{
	return cfg80211_sdio_func;
}

static s32 wl_dongle_mode(struct net_device *ndev, s32 iftype)
{
	s32 infra = 0;
	s32 ap = 0;
	s32 err = 0;

	switch (iftype) {
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_WDS:
		WL_ERR("type (%d) : currently we do not support this mode\n",
		       iftype);
		err = -EINVAL;
		return err;
	case NL80211_IFTYPE_ADHOC:
		break;
	case NL80211_IFTYPE_STATION:
		infra = 1;
		break;
	default:
		err = -EINVAL;
		WL_ERR("invalid type (%d)\n", iftype);
		return err;
	}
	infra = cpu_to_le32(infra);
	ap = cpu_to_le32(ap);
	WL_DBG("%s ap (%d), infra (%d)\n", ndev->name, ap, infra);
	err = wl_dev_ioctl(ndev, WLC_SET_INFRA, &infra, sizeof(infra));
	if (unlikely(err)) {
		WL_ERR("WLC_SET_INFRA error (%d)\n", err);
		return err;
	}
	err = wl_dev_ioctl(ndev, WLC_SET_AP, &ap, sizeof(ap));
	if (unlikely(err)) {
		WL_ERR("WLC_SET_AP error (%d)\n", err);
		return err;
	}

	return -EINPROGRESS;
}

#ifndef EMBEDDED_PLATFORM
static s32 wl_dongle_country(struct net_device *ndev, u8 ccode)
{

	s32 err = 0;

	return err;
}

static s32 wl_dongle_up(struct net_device *ndev, u32 up)
{
	s32 err = 0;

	err = wl_dev_ioctl(ndev, WLC_UP, &up, sizeof(up));
	if (unlikely(err)) {
		WL_ERR("WLC_UP error (%d)\n", err);
	}
	return err;
}

static s32 wl_dongle_power(struct net_device *ndev, u32 power_mode)
{
	s32 err = 0;

	err = wl_dev_ioctl(ndev, WLC_SET_PM, &power_mode, sizeof(power_mode));
	if (unlikely(err)) {
		WL_ERR("WLC_SET_PM error (%d)\n", err);
	}
	return err;
}

static s32
wl_dongle_glom(struct net_device *ndev, u32 glom, u32 dongle_align)
{
	s8 iovbuf[WL_EVENTING_MASK_LEN + 12];	/*  Room for "event_msgs" +
						 '\0' + bitvec  */
	s32 err = 0;

	/* Match Host and Dongle rx alignment */
	bcm_mkiovar("bus:txglomalign", (char *)&dongle_align, 4, iovbuf,
		    sizeof(iovbuf));
	err = wl_dev_ioctl(ndev, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
	if (unlikely(err)) {
		WL_ERR("txglomalign error (%d)\n", err);
		goto dongle_glom_out;
	}
	/* disable glom option per default */
	bcm_mkiovar("bus:txglom", (char *)&glom, 4, iovbuf, sizeof(iovbuf));
	err = wl_dev_ioctl(ndev, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
	if (unlikely(err)) {
		WL_ERR("txglom error (%d)\n", err);
		goto dongle_glom_out;
	}
dongle_glom_out:
	return err;
}

static s32
wl_dongle_roam(struct net_device *ndev, u32 roamvar, u32 bcn_timeout)
{
	s8 iovbuf[WL_EVENTING_MASK_LEN + 12];	/*  Room for "event_msgs" +
						 '\0' + bitvec  */
	s32 err = 0;

	/* Setup timeout if Beacons are lost and roam is
		 off to report link down */
	if (roamvar) {
		bcm_mkiovar("bcn_timeout", (char *)&bcn_timeout, 4, iovbuf,
			    sizeof(iovbuf));
		err = wl_dev_ioctl(ndev, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
		if (unlikely(err)) {
			WL_ERR("bcn_timeout error (%d)\n", err);
			goto dongle_rom_out;
		}
	}
	/* Enable/Disable built-in roaming to allow supplicant
		 to take care of roaming */
	bcm_mkiovar("roam_off", (char *)&roamvar, 4, iovbuf, sizeof(iovbuf));
	err = wl_dev_ioctl(ndev, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
	if (unlikely(err)) {
		WL_ERR("roam_off error (%d)\n", err);
		goto dongle_rom_out;
	}
dongle_rom_out:
	return err;
}

static s32 wl_dongle_eventmsg(struct net_device *ndev)
{

	s8 iovbuf[WL_EVENTING_MASK_LEN + 12];	/*  Room for "event_msgs" +
						 '\0' + bitvec  */
	s8 eventmask[WL_EVENTING_MASK_LEN];
	s32 err = 0;

	/* Setup event_msgs */
	bcm_mkiovar("event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf,
		    sizeof(iovbuf));
	err = wl_dev_ioctl(ndev, WLC_GET_VAR, iovbuf, sizeof(iovbuf));
	if (unlikely(err)) {
		WL_ERR("Get event_msgs error (%d)\n", err);
		goto dongle_eventmsg_out;
	}
	memcpy(eventmask, iovbuf, WL_EVENTING_MASK_LEN);

	setbit(eventmask, WLC_E_SET_SSID);
	setbit(eventmask, WLC_E_PRUNE);
	setbit(eventmask, WLC_E_AUTH);
	setbit(eventmask, WLC_E_REASSOC);
	setbit(eventmask, WLC_E_REASSOC_IND);
	setbit(eventmask, WLC_E_DEAUTH_IND);
	setbit(eventmask, WLC_E_DISASSOC_IND);
	setbit(eventmask, WLC_E_DISASSOC);
	setbit(eventmask, WLC_E_JOIN);
	setbit(eventmask, WLC_E_ASSOC_IND);
	setbit(eventmask, WLC_E_PSK_SUP);
	setbit(eventmask, WLC_E_LINK);
	setbit(eventmask, WLC_E_NDIS_LINK);
	setbit(eventmask, WLC_E_MIC_ERROR);
	setbit(eventmask, WLC_E_PMKID_CACHE);
	setbit(eventmask, WLC_E_TXFAIL);
	setbit(eventmask, WLC_E_JOIN_START);
	setbit(eventmask, WLC_E_SCAN_COMPLETE);

	bcm_mkiovar("event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf,
		    sizeof(iovbuf));
	err = wl_dev_ioctl(ndev, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
	if (unlikely(err)) {
		WL_ERR("Set event_msgs error (%d)\n", err);
		goto dongle_eventmsg_out;
	}

dongle_eventmsg_out:
	return err;
}

static s32
wl_dongle_scantime(struct net_device *ndev, s32 scan_assoc_time,
		   s32 scan_unassoc_time)
{
	s32 err = 0;

	err = wl_dev_ioctl(ndev, WLC_SET_SCAN_CHANNEL_TIME, &scan_assoc_time,
			sizeof(scan_assoc_time));
	if (err) {
		if (err == -EOPNOTSUPP) {
			WL_INFO("Scan assoc time is not supported\n");
		} else {
			WL_ERR("Scan assoc time error (%d)\n", err);
		}
		goto dongle_scantime_out;
	}
	err = wl_dev_ioctl(ndev, WLC_SET_SCAN_UNASSOC_TIME, &scan_unassoc_time,
			sizeof(scan_unassoc_time));
	if (err) {
		if (err == -EOPNOTSUPP) {
			WL_INFO("Scan unassoc time is not supported\n");
		} else {
			WL_ERR("Scan unassoc time error (%d)\n", err);
		}
		goto dongle_scantime_out;
	}

dongle_scantime_out:
	return err;
}

static s32
wl_dongle_offload(struct net_device *ndev, s32 arpoe, s32 arp_ol)
{
	s8 iovbuf[WL_EVENTING_MASK_LEN + 12];	/*  Room for "event_msgs" +
							 '\0' + bitvec  */
	s32 err = 0;

	/* Set ARP offload */
	bcm_mkiovar("arpoe", (char *)&arpoe, 4, iovbuf, sizeof(iovbuf));
	err = wl_dev_ioctl(ndev, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
	if (err) {
		if (err == -EOPNOTSUPP)
			WL_INFO("arpoe is not supported\n");
		else
			WL_ERR("arpoe error (%d)\n", err);

		goto dongle_offload_out;
	}
	bcm_mkiovar("arp_ol", (char *)&arp_ol, 4, iovbuf, sizeof(iovbuf));
	err = wl_dev_ioctl(ndev, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
	if (err) {
		if (err == -EOPNOTSUPP)
			WL_INFO("arp_ol is not supported\n");
		else
			WL_ERR("arp_ol error (%d)\n", err);

		goto dongle_offload_out;
	}

dongle_offload_out:
	return err;
}

static s32 wl_pattern_atoh(s8 *src, s8 *dst)
{
	int i;
	if (strncmp(src, "0x", 2) != 0 && strncmp(src, "0X", 2) != 0) {
		WL_ERR("Mask invalid format. Needs to start with 0x\n");
		return -1;
	}
	src = src + 2;		/* Skip past 0x */
	if (strlen(src) % 2 != 0) {
		WL_ERR("Mask invalid format. Needs to be of even length\n");
		return -1;
	}
	for (i = 0; *src != '\0'; i++) {
		char num[3];
		strncpy(num, src, 2);
		num[2] = '\0';
		dst[i] = (u8) simple_strtoul(num, NULL, 16);
		src += 2;
	}
	return i;
}

static s32 wl_dongle_filter(struct net_device *ndev, u32 filter_mode)
{
	s8 iovbuf[WL_EVENTING_MASK_LEN + 12];	/*  Room for "event_msgs" +
							 '\0' + bitvec  */
	const s8 *str;
	struct wl_pkt_filter pkt_filter;
	struct wl_pkt_filter *pkt_filterp;
	s32 buf_len;
	s32 str_len;
	u32 mask_size;
	u32 pattern_size;
	s8 buf[256];
	s32 err = 0;

/* add a default packet filter pattern */
	str = "pkt_filter_add";
	str_len = strlen(str);
	strncpy(buf, str, str_len);
	buf[str_len] = '\0';
	buf_len = str_len + 1;

	pkt_filterp = (struct wl_pkt_filter *)(buf + str_len + 1);

	/* Parse packet filter id. */
	pkt_filter.id = cpu_to_le32(100);

	/* Parse filter polarity. */
	pkt_filter.negate_match = cpu_to_le32(0);

	/* Parse filter type. */
	pkt_filter.type = cpu_to_le32(0);

	/* Parse pattern filter offset. */
	pkt_filter.u.pattern.offset = cpu_to_le32(0);

	/* Parse pattern filter mask. */
	mask_size = cpu_to_le32(wl_pattern_atoh("0xff",
						(char *)pkt_filterp->u.pattern.
						mask_and_pattern));

	/* Parse pattern filter pattern. */
	pattern_size = cpu_to_le32(wl_pattern_atoh("0x00",
						   (char *)&pkt_filterp->u.
						   pattern.
						   mask_and_pattern
						   [mask_size]));

	if (mask_size != pattern_size) {
		WL_ERR("Mask and pattern not the same size\n");
		err = -EINVAL;
		goto dongle_filter_out;
	}

	pkt_filter.u.pattern.size_bytes = mask_size;
	buf_len += WL_PKT_FILTER_FIXED_LEN;
	buf_len += (WL_PKT_FILTER_PATTERN_FIXED_LEN + 2 * mask_size);

	/* Keep-alive attributes are set in local
	 * variable (keep_alive_pkt), and
	 * then memcpy'ed into buffer (keep_alive_pktp) since there is no
	 * guarantee that the buffer is properly aligned.
	 */
	memcpy((char *)pkt_filterp, &pkt_filter,
	       WL_PKT_FILTER_FIXED_LEN + WL_PKT_FILTER_PATTERN_FIXED_LEN);

	err = wl_dev_ioctl(ndev, WLC_SET_VAR, buf, buf_len);
	if (err) {
		if (err == -EOPNOTSUPP) {
			WL_INFO("filter not supported\n");
		} else {
			WL_ERR("filter (%d)\n", err);
		}
		goto dongle_filter_out;
	}

	/* set mode to allow pattern */
	bcm_mkiovar("pkt_filter_mode", (char *)&filter_mode, 4, iovbuf,
		    sizeof(iovbuf));
	err = wl_dev_ioctl(ndev, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
	if (err) {
		if (err == -EOPNOTSUPP) {
			WL_INFO("filter_mode not supported\n");
		} else {
			WL_ERR("filter_mode (%d)\n", err);
		}
		goto dongle_filter_out;
	}

dongle_filter_out:
	return err;
}
#endif				/* !EMBEDDED_PLATFORM */

s32 wl_config_dongle(struct wl_priv *wl, bool need_lock)
{
#ifndef DHD_SDALIGN
#define DHD_SDALIGN	32
#endif
	struct net_device *ndev;
	struct wireless_dev *wdev;
	s32 err = 0;

	if (wl->dongle_up)
		return err;

	ndev = wl_to_ndev(wl);
	wdev = ndev->ieee80211_ptr;
	if (need_lock)
		rtnl_lock();

#ifndef EMBEDDED_PLATFORM
	err = wl_dongle_up(ndev, 0);
	if (unlikely(err))
		goto default_conf_out;
	err = wl_dongle_country(ndev, 0);
	if (unlikely(err))
		goto default_conf_out;
	err = wl_dongle_power(ndev, PM_FAST);
	if (unlikely(err))
		goto default_conf_out;
	err = wl_dongle_glom(ndev, 0, DHD_SDALIGN);
	if (unlikely(err))
		goto default_conf_out;
	err = wl_dongle_roam(ndev, (wl->roam_on ? 0 : 1), 3);
	if (unlikely(err))
		goto default_conf_out;
	err = wl_dongle_eventmsg(ndev);
	if (unlikely(err))
		goto default_conf_out;

	wl_dongle_scantime(ndev, 40, 80);
	wl_dongle_offload(ndev, 1, 0xf);
	wl_dongle_filter(ndev, 1);
#endif				/* !EMBEDDED_PLATFORM */

	err = wl_dongle_mode(ndev, wdev->iftype);
	if (unlikely(err && err != -EINPROGRESS))
		goto default_conf_out;
	err = wl_dongle_probecap(wl);
	if (unlikely(err))
		goto default_conf_out;

	/* -EINPROGRESS: Call commit handler */

default_conf_out:
	if (need_lock)
		rtnl_unlock();

	wl->dongle_up = true;

	return err;

}

static s32 wl_update_wiphybands(struct wl_priv *wl)
{
	struct wiphy *wiphy;
	s32 phy_list;
	s8 phy;
	s32 err = 0;

	err = wl_dev_ioctl(wl_to_ndev(wl), WLC_GET_PHYLIST, &phy_list,
			sizeof(phy_list));
	if (unlikely(err)) {
		WL_ERR("error (%d)\n", err);
		return err;
	}

	phy = ((char *)&phy_list)[1];
	WL_DBG("%c phy\n", phy);
	if (phy == 'n' || phy == 'a') {
		wiphy = wl_to_wiphy(wl);
		wiphy->bands[IEEE80211_BAND_5GHZ] = &__wl_band_5ghz_n;
	}

	return err;
}

static s32 __wl_cfg80211_up(struct wl_priv *wl)
{
	s32 err = 0;

	wl_debugfs_add_netdev_params(wl);

	err = wl_config_dongle(wl, false);
	if (unlikely(err))
		return err;

	wl_invoke_iscan(wl);
	set_bit(WL_STATUS_READY, &wl->status);
	return err;
}

static s32 __wl_cfg80211_down(struct wl_priv *wl)
{
	s32 err = 0;

	/* Check if cfg80211 interface is already down */
	if (!test_bit(WL_STATUS_READY, &wl->status))
		return err;	/* it is even not ready */

	set_bit(WL_STATUS_SCAN_ABORTING, &wl->status);
	wl_term_iscan(wl);
	if (wl->scan_request) {
		cfg80211_scan_done(wl->scan_request, true);	/* true
								 means abort */
		/* wl_set_mpc(wl_to_ndev(wl), 1); */	/* BUG
						* this operation cannot help
						* but here because sdio
						* is already down through
						* rmmod process.
						* Need to figure out how to
						* address this issue
						*/
		wl->scan_request = NULL;
	}
	clear_bit(WL_STATUS_READY, &wl->status);
	clear_bit(WL_STATUS_SCANNING, &wl->status);
	clear_bit(WL_STATUS_SCAN_ABORTING, &wl->status);
	clear_bit(WL_STATUS_CONNECTED, &wl->status);

	wl_debugfs_remove_netdev(wl);

	return err;
}

s32 wl_cfg80211_up(void)
{
	struct wl_priv *wl;
	s32 err = 0;

	wl = WL_PRIV_GET();
	mutex_lock(&wl->usr_sync);
	err = __wl_cfg80211_up(wl);
	mutex_unlock(&wl->usr_sync);

	return err;
}

s32 wl_cfg80211_down(void)
{
	struct wl_priv *wl;
	s32 err = 0;

	wl = WL_PRIV_GET();
	mutex_lock(&wl->usr_sync);
	err = __wl_cfg80211_down(wl);
	mutex_unlock(&wl->usr_sync);

	return err;
}

static s32 wl_dongle_probecap(struct wl_priv *wl)
{
	s32 err = 0;

	err = wl_update_wiphybands(wl);
	if (unlikely(err))
		return err;

	return err;
}

static void *wl_read_prof(struct wl_priv *wl, s32 item)
{
	switch (item) {
	case WL_PROF_SEC:
		return &wl->profile->sec;
	case WL_PROF_ACT:
		return &wl->profile->active;
	case WL_PROF_BSSID:
		return &wl->profile->bssid;
	case WL_PROF_SSID:
		return &wl->profile->ssid;
	}
	WL_ERR("invalid item (%d)\n", item);
	return NULL;
}

static s32
wl_update_prof(struct wl_priv *wl, const wl_event_msg_t *e, void *data,
	       s32 item)
{
	s32 err = 0;
	struct wlc_ssid *ssid;

	switch (item) {
	case WL_PROF_SSID:
		ssid = (wlc_ssid_t *) data;
		memset(wl->profile->ssid.SSID, 0,
		       sizeof(wl->profile->ssid.SSID));
		memcpy(wl->profile->ssid.SSID, ssid->SSID, ssid->SSID_len);
		wl->profile->ssid.SSID_len = ssid->SSID_len;
		break;
	case WL_PROF_BSSID:
		if (data)
			memcpy(wl->profile->bssid, data, ETH_ALEN);
		else
			memset(wl->profile->bssid, 0, ETH_ALEN);
		break;
	case WL_PROF_SEC:
		memcpy(&wl->profile->sec, data, sizeof(wl->profile->sec));
		break;
	case WL_PROF_ACT:
		wl->profile->active = *(bool *)data;
		break;
	case WL_PROF_BEACONINT:
		wl->profile->beacon_interval = *(u16 *)data;
		break;
	case WL_PROF_DTIMPERIOD:
		wl->profile->dtim_period = *(u8 *)data;
		break;
	default:
		WL_ERR("unsupported item (%d)\n", item);
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

void wl_cfg80211_dbg_level(u32 level)
{
	/*
	* prohibit to change debug level
	* by insmod parameter.
	* eventually debug level will be configured
	* in compile time by using CONFIG_XXX
	*/
	/* wl_dbg_level = level; */
}

static bool wl_is_ibssmode(struct wl_priv *wl)
{
	return wl->conf->mode == WL_MODE_IBSS;
}

static bool wl_is_ibssstarter(struct wl_priv *wl)
{
	return wl->ibss_starter;
}

static void wl_rst_ie(struct wl_priv *wl)
{
	struct wl_ie *ie = wl_to_ie(wl);

	ie->offset = 0;
}

static __used s32 wl_add_ie(struct wl_priv *wl, u8 t, u8 l, u8 *v)
{
	struct wl_ie *ie = wl_to_ie(wl);
	s32 err = 0;

	if (unlikely(ie->offset + l + 2 > WL_TLV_INFO_MAX)) {
		WL_ERR("ei crosses buffer boundary\n");
		return -ENOSPC;
	}
	ie->buf[ie->offset] = t;
	ie->buf[ie->offset + 1] = l;
	memcpy(&ie->buf[ie->offset + 2], v, l);
	ie->offset += l + 2;

	return err;
}

static s32 wl_mrg_ie(struct wl_priv *wl, u8 *ie_stream, u16 ie_size)
{
	struct wl_ie *ie = wl_to_ie(wl);
	s32 err = 0;

	if (unlikely(ie->offset + ie_size > WL_TLV_INFO_MAX)) {
		WL_ERR("ei_stream crosses buffer boundary\n");
		return -ENOSPC;
	}
	memcpy(&ie->buf[ie->offset], ie_stream, ie_size);
	ie->offset += ie_size;

	return err;
}

static s32 wl_cp_ie(struct wl_priv *wl, u8 *dst, u16 dst_size)
{
	struct wl_ie *ie = wl_to_ie(wl);
	s32 err = 0;

	if (unlikely(ie->offset > dst_size)) {
		WL_ERR("dst_size is not enough\n");
		return -ENOSPC;
	}
	memcpy(dst, &ie->buf[0], ie->offset);

	return err;
}

static u32 wl_get_ielen(struct wl_priv *wl)
{
	struct wl_ie *ie = wl_to_ie(wl);

	return ie->offset;
}

static void wl_link_up(struct wl_priv *wl)
{
	wl->link_up = true;
}

static void wl_link_down(struct wl_priv *wl)
{
	struct wl_connect_info *conn_info = wl_to_conn(wl);

	wl->link_up = false;
	kfree(conn_info->req_ie);
	conn_info->req_ie = NULL;
	conn_info->req_ie_len = 0;
	kfree(conn_info->resp_ie);
	conn_info->resp_ie = NULL;
	conn_info->resp_ie_len = 0;
}

static void wl_lock_eq(struct wl_priv *wl)
{
	spin_lock_irq(&wl->eq_lock);
}

static void wl_unlock_eq(struct wl_priv *wl)
{
	spin_unlock_irq(&wl->eq_lock);
}

static void wl_init_eq_lock(struct wl_priv *wl)
{
	spin_lock_init(&wl->eq_lock);
}

static void wl_delay(u32 ms)
{
	if (ms < 1000 / HZ) {
		cond_resched();
		mdelay(ms);
	} else {
		msleep(ms);
	}
}

static void wl_set_drvdata(struct wl_dev *dev, void *data)
{
	dev->driver_data = data;
}

static void *wl_get_drvdata(struct wl_dev *dev)
{
	return dev->driver_data;
}

s32 wl_cfg80211_read_fw(s8 *buf, u32 size)
{
	const struct firmware *fw_entry;
	struct wl_priv *wl;

	wl = WL_PRIV_GET();

	fw_entry = wl->fw->fw_entry;

	if (fw_entry->size < wl->fw->ptr + size)
		size = fw_entry->size - wl->fw->ptr;

	memcpy(buf, &fw_entry->data[wl->fw->ptr], size);
	wl->fw->ptr += size;
	return size;
}

void wl_cfg80211_release_fw(void)
{
	struct wl_priv *wl;

	wl = WL_PRIV_GET();
	release_firmware(wl->fw->fw_entry);
	wl->fw->ptr = 0;
}

void *wl_cfg80211_request_fw(s8 *file_name)
{
	struct wl_priv *wl;
	const struct firmware *fw_entry = NULL;
	s32 err = 0;

	WL_DBG("file name : \"%s\"\n", file_name);
	wl = WL_PRIV_GET();

	if (!test_bit(WL_FW_LOADING_DONE, &wl->fw->status)) {
		err = request_firmware(&wl->fw->fw_entry, file_name,
				&wl_cfg80211_get_sdio_func()->dev);
		if (unlikely(err)) {
			WL_ERR("Could not download fw (%d)\n", err);
			goto req_fw_out;
		}
		set_bit(WL_FW_LOADING_DONE, &wl->fw->status);
		fw_entry = wl->fw->fw_entry;
		if (fw_entry) {
			WL_DBG("fw size (%zd), data (%p)\n",
			       fw_entry->size, fw_entry->data);
		}
	} else if (!test_bit(WL_NVRAM_LOADING_DONE, &wl->fw->status)) {
		err = request_firmware(&wl->fw->fw_entry, file_name,
				&wl_cfg80211_get_sdio_func()->dev);
		if (unlikely(err)) {
			WL_ERR("Could not download nvram (%d)\n", err);
			goto req_fw_out;
		}
		set_bit(WL_NVRAM_LOADING_DONE, &wl->fw->status);
		fw_entry = wl->fw->fw_entry;
		if (fw_entry) {
			WL_DBG("nvram size (%zd), data (%p)\n",
			       fw_entry->size, fw_entry->data);
		}
	} else {
		WL_DBG("Downloading already done. Nothing to do more\n");
		err = -EPERM;
	}

req_fw_out:
	if (unlikely(err)) {
		return NULL;
	}
	wl->fw->ptr = 0;
	return (void *)fw_entry->data;
}

s8 *wl_cfg80211_get_fwname(void)
{
	struct wl_priv *wl;

	wl = WL_PRIV_GET();
	strcpy(wl->fw->fw_name, WL_4329_FW_FILE);
	return wl->fw->fw_name;
}

s8 *wl_cfg80211_get_nvramname(void)
{
	struct wl_priv *wl;

	wl = WL_PRIV_GET();
	strcpy(wl->fw->nvram_name, WL_4329_NVRAM_FILE);
	return wl->fw->nvram_name;
}

static void wl_set_mpc(struct net_device *ndev, int mpc)
{
	s32 err = 0;

	err = wl_dev_intvar_set(ndev, "mpc", mpc);
	if (unlikely(err)) {
		WL_ERR("fail to set mpc\n");
		return;
	}
	WL_DBG("MPC : %d\n", mpc);
}

static int wl_debugfs_add_netdev_params(struct wl_priv *wl)
{
	char buf[10+IFNAMSIZ];
	struct dentry *fd;
	s32 err = 0;

	sprintf(buf, "netdev:%s", wl_to_ndev(wl)->name);
	wl->debugfsdir = debugfs_create_dir(buf, wl_to_wiphy(wl)->debugfsdir);

	fd = debugfs_create_u16("beacon_int", S_IRUGO, wl->debugfsdir,
		(u16 *)&wl->profile->beacon_interval);
	if (!fd) {
		err = -ENOMEM;
		goto err_out;
	}

	fd = debugfs_create_u8("dtim_period", S_IRUGO, wl->debugfsdir,
		(u8 *)&wl->profile->dtim_period);
	if (!fd) {
		err = -ENOMEM;
		goto err_out;
	}

err_out:
	return err;
}

static void wl_debugfs_remove_netdev(struct wl_priv *wl)
{
	debugfs_remove_recursive(wl->debugfsdir);
	wl->debugfsdir = NULL;
}
