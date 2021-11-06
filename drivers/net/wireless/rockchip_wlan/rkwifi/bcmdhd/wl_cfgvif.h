/*
 * Wifi Virtual Interface implementaion
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
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
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef _wl_cfgvif_h_
#define _wl_cfgvif_h_

#include <linux/wireless.h>
#include <typedefs.h>
#include <ethernet.h>
#include <wlioctl.h>
#include <linux/wireless.h>
#include <net/cfg80211.h>
#include <linux/rfkill.h>
#include <osl.h>
#if defined(BCMDONGLEHOST)
#include <dngl_stats.h>
#include <dhd.h>
#endif /* BCMDONGLEHOST */
#include <wl_cfgp2p.h>
#ifdef WL_NAN
#include <wl_cfgnan.h>
#endif /* WL_NAN */
#ifdef WL_BAM
#include <wl_bam.h>
#endif  /* WL_BAM */

#ifdef SUPPORT_AP_RADIO_PWRSAVE
#define RADIO_PWRSAVE_PPS               10
#define RADIO_PWRSAVE_QUIET_TIME        10
#define RADIO_PWRSAVE_LEVEL             3
#define RADIO_PWRSAVE_STAS_ASSOC_CHECK  0

#define RADIO_PWRSAVE_LEVEL_MIN         1
#define RADIO_PWRSAVE_LEVEL_MAX         9
#define RADIO_PWRSAVE_PPS_MIN           1
#define RADIO_PWRSAVE_QUIETTIME_MIN     1
#define RADIO_PWRSAVE_ASSOCCHECK_MIN    0
#define RADIO_PWRSAVE_ASSOCCHECK_MAX    1

#define RADIO_PWRSAVE_MAJOR_VER         1
#define RADIO_PWRSAVE_MINOR_VER         1
#define RADIO_PWRSAVE_MAJOR_VER_SHIFT   8
#define RADIO_PWRSAVE_VERSION \
	((RADIO_PWRSAVE_MAJOR_VER << RADIO_PWRSAVE_MAJOR_VER_SHIFT)| RADIO_PWRSAVE_MINOR_VER)
#endif /* SUPPORT_AP_RADIO_PWRSAVE */

#ifdef WLTDLS
#define TDLS_TUNNELED_PRB_REQ	"\x7f\x50\x6f\x9a\04"
#define TDLS_TUNNELED_PRB_RESP	"\x7f\x50\x6f\x9a\05"
#define TDLS_MAX_IFACE_FOR_ENABLE 1
#endif /* WLTDLS */

/* HE flag defines */
#define WL_HE_FEATURES_HE_AP		0x8
#define WL_HE_FEATURES_HE_P2P		0x20
#define WL_HE_FEATURES_6G		0x80u

extern bool wl_cfg80211_check_vif_in_use(struct net_device *ndev);

extern int wl_cfg80211_set_mgmt_vndr_ies(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, s32 bssidx, s32 pktflag,
	const u8 *vndr_ie, u32 vndr_ie_len);

#ifdef WL_SUPPORT_ACS
#define ACS_MSRMNT_DELAY 1000 /* dump_obss delay in ms */
#define IOCTL_RETRY_COUNT 5
#define CHAN_NOISE_DUMMY -80
#define OBSS_TOKEN_IDX 15
#define IBSS_TOKEN_IDX 15
#define TX_TOKEN_IDX 14
#define CTG_TOKEN_IDX 13
#define PKT_TOKEN_IDX 15
#define IDLE_TOKEN_IDX 12
#endif /* WL_SUPPORT_ACS */

extern s32 wl_cfg80211_dfs_ap_move(struct net_device *ndev, char *data,
		char *command, int total_len);
extern s32 wl_cfg80211_get_band_chanspecs(struct net_device *ndev,
		void *buf, s32 buflen, chanspec_band_t band, bool acs_req);

#ifdef WLTDLS
extern s32 wl_cfg80211_tdls_config(struct bcm_cfg80211 *cfg,
	enum wl_tdls_config state, bool tdls_mode);
extern s32 wl_tdls_event_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
#endif /* WLTDLS */

#ifdef SUPPORT_AP_HIGHER_BEACONRATE
int wl_set_ap_beacon_rate(struct net_device *dev, int val, char *ifname);
int wl_get_ap_basic_rate(struct net_device *dev, char* command, char *ifname, int total_len);
#endif /* SUPPORT_AP_HIGHER_BEACONRATE */
#ifdef SUPPORT_AP_RADIO_PWRSAVE
int wl_get_ap_rps(struct net_device *dev, char* command, char *ifname, int total_len);
int wl_set_ap_rps(struct net_device *dev, bool enable, char *ifname);
int wl_update_ap_rps_params(struct net_device *dev, ap_rps_info_t* rps, char *ifname);
void wl_cfg80211_init_ap_rps(struct bcm_cfg80211 *cfg);
#endif /* SUPPORT_AP_RADIO_PWRSAVE */
int wl_cfg80211_iface_count(struct net_device *dev);
struct net_device* wl_get_ap_netdev(struct bcm_cfg80211 *cfg, char *ifname);
void wl_cfg80211_cleanup_virtual_ifaces(struct bcm_cfg80211 *cfg, bool rtnl_lock_reqd);
#ifdef WL_IFACE_MGMT
extern int wl_cfg80211_set_iface_policy(struct net_device *ndev, char *arg, int len);
extern uint8 wl_cfg80211_get_iface_policy(struct net_device *ndev);
extern s32 wl_cfg80211_handle_if_role_conflict(struct bcm_cfg80211 *cfg, wl_iftype_t new_wl_iftype);
extern wl_iftype_t wl_cfg80211_get_sec_iface(struct bcm_cfg80211 *cfg);
#endif /* WL_IFACE_MGMT */

extern s32 wl_get_vif_macaddr(struct bcm_cfg80211 *cfg, u16 wl_iftype, u8 *mac_addr);
extern s32 wl_release_vif_macaddr(struct bcm_cfg80211 *cfg, u8 *mac_addr, u16 wl_iftype);

int wl_cfg80211_set_he_mode(struct net_device *dev, struct bcm_cfg80211 *cfg,
		s32 bssidx, u32 interface_type, bool set);
#ifdef SUPPORT_AP_SUSPEND
extern int wl_set_ap_suspend(struct net_device *dev, bool enable, char *ifname);
#endif /* SUPPORT_AP_SUSPEND */
#ifdef SUPPORT_SOFTAP_ELNA_BYPASS
int wl_set_softap_elna_bypass(struct net_device *dev, char *ifname, int enable);
int wl_get_softap_elna_bypass(struct net_device *dev, char *ifname, void *param);
#endif /* SUPPORT_SOFTAP_ELNA_BYPASS */
#ifdef SUPPORT_AP_BWCTRL
extern int wl_set_ap_bw(struct net_device *dev, u32 bw, char *ifname);
extern int wl_get_ap_bw(struct net_device *dev, char* command, char *ifname, int total_len);
#endif /* SUPPORT_AP_BWCTRL */
extern s32 wl_get_nl80211_band(u32 wl_band);
extern int wl_get_bandwidth_cap(struct net_device *ndev, uint32 band, uint32 *bandwidth);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 2, 0)) || \
	defined(WL_COMPAT_WIRELESS)
#if (defined(CONFIG_ARCH_MSM) && defined(TDLS_MGMT_VERSION2)) || \
	((LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0) && \
	LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)))
extern s32 wl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
	u8 *peer, u8 action_code, u8 dialog_token, u16 status_code,
	u32 peer_capability, const u8 *buf, size_t len);
#elif ((LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)) && \
		(LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0)))
extern s32 wl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
	const u8 *peer, u8 action_code, u8 dialog_token, u16 status_code,
	u32 peer_capability, const u8 *buf, size_t len);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
extern s32 wl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
       const u8 *peer, u8 action_code, u8 dialog_token, u16 status_code,
       u32 peer_capability, bool initiator, const u8 *buf, size_t len);
#else /* CONFIG_ARCH_MSM && TDLS_MGMT_VERSION2 */
extern s32 wl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
	u8 *peer, u8 action_code, u8 dialog_token, u16 status_code,
	const u8 *buf, size_t len);
#endif /* CONFIG_ARCH_MSM && TDLS_MGMT_VERSION2 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
extern s32 wl_cfg80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
	const u8 *peer, enum nl80211_tdls_operation oper);
#else
extern s32 wl_cfg80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
	u8 *peer, enum nl80211_tdls_operation oper);
#endif
#endif /* LINUX_VERSION > KERNEL_VERSION(3,2,0) || WL_COMPAT_WIRELESS */

extern s32 wl_notify_connect_status_ap(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data);
extern s32 wl_cfg80211_change_virtual_iface(struct wiphy *wiphy, struct net_device *ndev,
	enum nl80211_iftype type,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))
	u32 *flags,
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0) */
	struct vif_params *params);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)) || \
	defined(WL_COMPAT_WIRELESS)
s32
wl_cfg80211_set_channel(struct wiphy *wiphy, struct net_device *dev,
	struct ieee80211_channel *chan,
	enum nl80211_channel_type channel_type);
#endif /* ((LINUX_VERSION < VERSION(3, 6, 0)) || WL_COMPAT_WIRELESS */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)) || \
	defined(WL_COMPAT_WIRELESS)
extern s32 wl_cfg80211_start_ap(struct wiphy *wiphy, struct net_device *dev,
		struct cfg80211_ap_settings *info);
extern s32 wl_cfg80211_stop_ap(struct wiphy *wiphy, struct net_device *dev);
extern s32 wl_cfg80211_change_beacon(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_beacon_data *info);
#else
extern s32 wl_cfg80211_add_set_beacon(struct wiphy *wiphy, struct net_device *dev,
	struct beacon_parameters *info);
extern s32 wl_cfg80211_del_beacon(struct wiphy *wiphy, struct net_device *dev);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)  || WL_COMPAT_WIRELESS */

extern s32 wl_ap_start_ind(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
extern s32 wl_csa_complete_ind(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
extern s32 wl_cfg80211_set_ap_role(struct bcm_cfg80211 *cfg, struct net_device *dev);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0))
extern int wl_cfg80211_channel_switch(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_csa_settings *params);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0) */

extern bcm_struct_cfgdev *
wl_cfg80211_add_virtual_iface(struct wiphy *wiphy,
#if defined(WL_CFG80211_P2P_DEV_IF)
	const char *name,
#else
	char *name,
#endif /* WL_CFG80211_P2P_DEV_IF */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	unsigned char name_assign_type,
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)) */
	enum nl80211_iftype type,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))
	u32 *flags,
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0) */
	struct vif_params *params);
extern s32 wl_cfg80211_del_virtual_iface(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev);
extern s32 wl_cfg80211_change_beacon(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_beacon_data *info);

extern s32 wl_get_auth_assoc_status(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data);
extern s32 wl_frame_get_mgmt(struct bcm_cfg80211 *cfg, u16 fc,
	const struct ether_addr *da, const struct ether_addr *sa,
	const struct ether_addr *bssid, u8 **pheader, u32 *body_len, u8 *pbody);
extern s32 wl_cfg80211_parse_ies(const u8 *ptr, u32 len, struct parsed_ies *ies);
extern void wl_cfg80211_ap_timeout_work(struct work_struct *work);

#if defined(WLTDLS)
extern bool wl_cfg80211_is_tdls_tunneled_frame(void *frame, u32 frame_len);
#endif /* WLTDLS */

#ifdef SUPPORT_AP_BWCTRL
extern void wl_restore_ap_bw(struct bcm_cfg80211 *cfg);
#endif /* SUPPORT_AP_BWCTRL */
#endif /* _wl_cfgvif_h_ */
