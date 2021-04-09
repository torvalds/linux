/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/netlink.h>
#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>

#include <bcmutils.h>
#include <bcmendian.h>
#include <ethernet.h>

#include <wl_android.h>
#include <linux/if_arp.h>
#include <linux/uaccess.h>
#include <linux/wireless.h>
#if defined(WL_WIRELESS_EXT)
#include <wl_iw.h>
#endif /* WL_WIRELESS_EXT */
#include <wldev_common.h>
#include <wlioctl.h>
#include <bcmutils.h>
#include <linux_osl.h>
#include <dhd_dbg.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_config.h>
#ifdef WL_CFG80211
#include <wl_cfg80211.h>
#endif /* WL_CFG80211 */
#ifdef WL_ESCAN
#include <wl_escan.h>
#endif /* WL_ESCAN */

#define AEXT_ERROR(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_ERROR_LEVEL) { \
			printk(KERN_ERR "[dhd-%s] AEXT-ERROR) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)
#define AEXT_TRACE(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_TRACE_LEVEL) { \
			printk(KERN_ERR "[dhd-%s] AEXT-TRACE) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)
#define AEXT_INFO(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_INFO_LEVEL) { \
			printk(KERN_ERR "[dhd-%s] AEXT-INFO) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)
#define AEXT_DBG(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_DBG_LEVEL) { \
			printk(KERN_ERR "[dhd-%s] AEXT-DBG) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)

#ifndef WL_CFG80211
#define htod32(i) i
#define htod16(i) i
#define dtoh32(i) i
#define dtoh16(i) i
#define htodchanspec(i) i
#define dtohchanspec(i) i
#define IEEE80211_BAND_2GHZ 0
#define IEEE80211_BAND_5GHZ 1
#define WL_SCAN_JOIN_PROBE_INTERVAL_MS 		20
#define WL_SCAN_JOIN_ACTIVE_DWELL_TIME_MS 	320
#define WL_SCAN_JOIN_PASSIVE_DWELL_TIME_MS 	400
#endif /* WL_CFG80211 */
#define strtoul(nptr, endptr, base) bcm_strtoul((nptr), (endptr), (base))

#ifndef IW_CUSTOM_MAX
#define IW_CUSTOM_MAX 256 /* size of extra buffer used for translation of events */
#endif /* IW_CUSTOM_MAX */

#define CMD_CHANNEL				"CHANNEL"
#define CMD_CHANNELS			"CHANNELS"
#define CMD_ROAM_TRIGGER		"ROAM_TRIGGER"
#define CMD_PM					"PM"
#define CMD_MONITOR				"MONITOR"
#define CMD_SET_SUSPEND_BCN_LI_DTIM		"SET_SUSPEND_BCN_LI_DTIM"

#ifdef WL_EXT_IAPSTA
#include <net/rtnetlink.h>
#define CMD_IAPSTA_INIT			"IAPSTA_INIT"
#define CMD_IAPSTA_CONFIG		"IAPSTA_CONFIG"
#define CMD_IAPSTA_ENABLE		"IAPSTA_ENABLE"
#define CMD_IAPSTA_DISABLE		"IAPSTA_DISABLE"
#define CMD_ISAM_INIT			"ISAM_INIT"
#define CMD_ISAM_CONFIG			"ISAM_CONFIG"
#define CMD_ISAM_ENABLE			"ISAM_ENABLE"
#define CMD_ISAM_DISABLE		"ISAM_DISABLE"
#define CMD_ISAM_STATUS			"ISAM_STATUS"
#define CMD_ISAM_PARAM			"ISAM_PARAM"
#ifdef PROP_TXSTATUS
#ifdef PROP_TXSTATUS_VSDB
#include <dhd_wlfc.h>
extern int disable_proptx;
#endif /* PROP_TXSTATUS_VSDB */
#endif /* PROP_TXSTATUS */
#endif /* WL_EXT_IAPSTA */
#define CMD_AUTOCHANNEL		"AUTOCHANNEL"
#define CMD_WL		"WL"

#ifdef WL_EXT_IAPSTA
typedef enum APSTAMODE {
	ISTAONLY_MODE = 1,
	IAPONLY_MODE = 2,
	ISTAAP_MODE = 3,
	ISTAGO_MODE = 4,
	ISTASTA_MODE = 5,
	IDUALAP_MODE = 6,
	ISTAAPAP_MODE = 7,
	IMESHONLY_MODE = 8,
	ISTAMESH_MODE = 9,
	IMESHAP_MODE = 10,
	ISTAAPMESH_MODE = 11,
	IMESHAPAP_MODE = 12
} apstamode_t;

typedef enum IFMODE {
	ISTA_MODE = 1,
	IAP_MODE,
	IMESH_MODE
} ifmode_t;

typedef enum BGNMODE {
	IEEE80211B = 1,
	IEEE80211G,
	IEEE80211BG,
	IEEE80211BGN,
	IEEE80211BGNAC
} bgnmode_t;

typedef enum AUTHMODE {
	AUTH_OPEN,
	AUTH_SHARED,
	AUTH_WPAPSK,
	AUTH_WPA2PSK,
	AUTH_WPAWPA2PSK,
	AUTH_SAE
} authmode_t;

typedef enum ENCMODE {
	ENC_NONE,
	ENC_WEP,
	ENC_TKIP,
	ENC_AES,
	ENC_TKIPAES
} encmode_t;

enum wl_if_list {
	IF_PIF,
	IF_VIF,
	IF_VIF2,
	MAX_IF_NUM
};

typedef enum WL_PRIO {
	PRIO_AP,
	PRIO_MESH,
	PRIO_STA
} wl_prio_t;

typedef struct wl_if_info {
	struct net_device *dev;
	ifmode_t ifmode;
	unsigned long status;
	char prefix;
	wl_prio_t prio;
	int ifidx;
	uint8 bssidx;
	char ifname[IFNAMSIZ+1];
	char ssid[DOT11_MAX_SSID_LEN];
	struct ether_addr bssid;
	bgnmode_t bgnmode;
	int hidden;
	int maxassoc;
	uint16 channel;
	authmode_t amode;
	encmode_t emode;
	char key[100];
#if defined(WLMESH) && defined(WL_ESCAN)
	struct wl_escan_info *escan;
	timer_list_compat_t delay_scan;
#endif /* WLMESH && WL_ESCAN */
} wl_if_info_t;

#define CSA_FW_BIT		(1<<0)
#define CSA_DRV_BIT		(1<<1)

typedef struct wl_apsta_params {
	struct wl_if_info if_info[MAX_IF_NUM];
	struct dhd_pub *dhd;
	int ioctl_ver;
	bool init;
	int rsdb;
	bool vsdb;
	uint csa;
	uint acs;
	bool radar;
	apstamode_t apstamode;
	wait_queue_head_t netif_change_event;
	struct mutex usr_sync;
#if defined(WLMESH) && defined(WL_ESCAN)
	int macs;
	struct wl_mesh_params mesh_info;
#endif /* WLMESH && WL_ESCAN */
} wl_apsta_params_t;

#define MAX_AP_LINK_WAIT_TIME   3000
#define MAX_STA_LINK_WAIT_TIME   15000
enum wifi_isam_status {
	ISAM_STATUS_IF_ADDING = 0,
	ISAM_STATUS_IF_READY,
	ISAM_STATUS_STA_CONNECTING,
	ISAM_STATUS_STA_CONNECTED,
	ISAM_STATUS_AP_CREATING,
	ISAM_STATUS_AP_CREATED
};

#define wl_get_isam_status(cur_if, stat) \
	(test_bit(ISAM_STATUS_ ## stat, &(cur_if)->status))
#define wl_set_isam_status(cur_if, stat) \
	(set_bit(ISAM_STATUS_ ## stat, &(cur_if)->status))
#define wl_clr_isam_status(cur_if, stat) \
	(clear_bit(ISAM_STATUS_ ## stat, &(cur_if)->status))
#define wl_chg_isam_status(cur_if, stat) \
	(change_bit(ISAM_STATUS_ ## stat, &(cur_if)->status))

static int wl_ext_enable_iface(struct net_device *dev, char *ifname, int wait_up);
static int wl_ext_disable_iface(struct net_device *dev, char *ifname);
#if defined(WLMESH) && defined(WL_ESCAN)
static int wl_mesh_escan_attach(dhd_pub_t *dhd, struct wl_if_info *cur_if);
#endif /* WLMESH && WL_ESCAN */
#endif /* WL_EXT_IAPSTA */

#ifdef IDHCP
typedef struct dhcpc_parameter {
	uint32 ip_addr;
	uint32 ip_serv;
	uint32 lease_time;
} dhcpc_para_t;
#endif /* IDHCP */

#ifdef WL_EXT_WOWL
#define WL_WOWL_TCPFIN	(1 << 26)
typedef struct wl_wowl_pattern2 {
	char cmd[4];
	wl_wowl_pattern_t wowl_pattern;
} wl_wowl_pattern2_t;
#endif /* WL_EXT_WOWL */

#ifdef WL_EXT_TCPKA
typedef struct tcpka_conn {
	uint32 sess_id;
	struct ether_addr dst_mac;	/* Destinition Mac */
	struct ipv4_addr  src_ip;	/* Sorce IP */
	struct ipv4_addr  dst_ip;	/* Destinition IP */
	uint16 ipid;	/* Ip Identification */
	uint16 srcport;	/* Source Port Address */
	uint16 dstport;	/* Destination Port Address */
	uint32 seq;		/* TCP Sequence Number */
	uint32 ack;		/* TCP Ack Number */
	uint16 tcpwin;	/* TCP window */
	uint32 tsval;	/* Timestamp Value */
	uint32 tsecr;	/* Timestamp Echo Reply */
	uint32 len;		/* last packet payload len */
	uint32 ka_payload_len;	/* keep alive payload length */
	uint8  ka_payload[1];	/* keep alive payload */
} tcpka_conn_t;

typedef struct tcpka_conn_sess {
	uint32 sess_id;	/* session id */
	uint32 flag;	/* enable/disable flag */
	wl_mtcpkeep_alive_timers_pkt_t  tcpka_timers;
} tcpka_conn_sess_t;

typedef struct tcpka_conn_info {
	uint32 ipid;
	uint32 seq;
	uint32 ack;
} tcpka_conn_sess_info_t;
#endif /* WL_EXT_TCPKA */

static int wl_ext_wl_iovar(struct net_device *dev, char *command, int total_len);

static int
wl_ext_ioctl(struct net_device *dev, u32 cmd, void *arg, u32 len, u32 set)
{
	int ret;

	ret = wldev_ioctl(dev, cmd, arg, len, set);
	if (ret)
		AEXT_ERROR(dev->name, "cmd=%d, ret=%d\n", cmd, ret);
	return ret;
}

static int
wl_ext_iovar_getint(struct net_device *dev, s8 *iovar, s32 *val)
{
	int ret;

	ret = wldev_iovar_getint(dev, iovar, val);
	if (ret)
		AEXT_ERROR(dev->name, "iovar=%s, ret=%d\n", iovar, ret);

	return ret;
}

static int
wl_ext_iovar_setint(struct net_device *dev, s8 *iovar, s32 val)
{
	int ret;

	ret = wldev_iovar_setint(dev, iovar, val);
	if (ret)
		AEXT_ERROR(dev->name, "iovar=%s, ret=%d\n", iovar, ret);

	return ret;
}

static int
wl_ext_iovar_getbuf(struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, struct mutex* buf_sync)
{
	int ret;

	ret = wldev_iovar_getbuf(dev, iovar_name, param, paramlen, buf, buflen, buf_sync);
	if (ret != 0)
		AEXT_ERROR(dev->name, "iovar=%s, ret=%d\n", iovar_name, ret);

	return ret;
}

static int
wl_ext_iovar_setbuf(struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, struct mutex* buf_sync)
{
	int ret;

	ret = wldev_iovar_setbuf(dev, iovar_name, param, paramlen, buf, buflen, buf_sync);
	if (ret != 0)
		AEXT_ERROR(dev->name, "iovar=%s, ret=%d\n", iovar_name, ret);

	return ret;
}

static int
wl_ext_iovar_setbuf_bsscfg(struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, s32 bsscfg_idx,
	struct mutex* buf_sync)
{
	int ret;

	ret = wldev_iovar_setbuf_bsscfg(dev, iovar_name, param, paramlen,
		buf, buflen, bsscfg_idx, buf_sync);
	if (ret < 0)
		AEXT_ERROR(dev->name, "iovar=%s, ret=%d\n", iovar_name, ret);

	return ret;
}

static chanspec_t
wl_ext_chspec_to_legacy(chanspec_t chspec)
{
	chanspec_t lchspec;

	if (wf_chspec_malformed(chspec)) {
		AEXT_ERROR("wlan", "input chanspec (0x%04X) malformed\n", chspec);
		return INVCHANSPEC;
	}

	/* get the channel number */
	lchspec = CHSPEC_CHANNEL(chspec);

	/* convert the band */
	if (CHSPEC_IS2G(chspec)) {
		lchspec |= WL_LCHANSPEC_BAND_2G;
	} else {
		lchspec |= WL_LCHANSPEC_BAND_5G;
	}

	/* convert the bw and sideband */
	if (CHSPEC_IS20(chspec)) {
		lchspec |= WL_LCHANSPEC_BW_20;
		lchspec |= WL_LCHANSPEC_CTL_SB_NONE;
	} else if (CHSPEC_IS40(chspec)) {
		lchspec |= WL_LCHANSPEC_BW_40;
		if (CHSPEC_CTL_SB(chspec) == WL_CHANSPEC_CTL_SB_L) {
			lchspec |= WL_LCHANSPEC_CTL_SB_LOWER;
		} else {
			lchspec |= WL_LCHANSPEC_CTL_SB_UPPER;
		}
	} else {
		/* cannot express the bandwidth */
		char chanbuf[CHANSPEC_STR_LEN];
		AEXT_ERROR("wlan", "unable to convert chanspec %s (0x%04X) "
			"to pre-11ac format\n",
			wf_chspec_ntoa(chspec, chanbuf), chspec);
		return INVCHANSPEC;
	}

	return lchspec;
}

static chanspec_t
wl_ext_chspec_host_to_driver(int ioctl_ver, chanspec_t chanspec)
{
	if (ioctl_ver == 1) {
		chanspec = wl_ext_chspec_to_legacy(chanspec);
		if (chanspec == INVCHANSPEC) {
			return chanspec;
		}
	}
	chanspec = htodchanspec(chanspec);

	return chanspec;
}

static void
wl_ext_ch_to_chanspec(int ioctl_ver, int ch,
	struct wl_join_params *join_params, size_t *join_params_size)
{
	chanspec_t chanspec = 0;

	if (ch != 0) {
		join_params->params.chanspec_num = 1;
		join_params->params.chanspec_list[0] = ch;

		if (join_params->params.chanspec_list[0] <= CH_MAX_2G_CHANNEL)
			chanspec |= WL_CHANSPEC_BAND_2G;
		else
			chanspec |= WL_CHANSPEC_BAND_5G;

		chanspec |= WL_CHANSPEC_BW_20;
		chanspec |= WL_CHANSPEC_CTL_SB_NONE;

		*join_params_size += WL_ASSOC_PARAMS_FIXED_SIZE +
			join_params->params.chanspec_num * sizeof(chanspec_t);

		join_params->params.chanspec_list[0]  &= WL_CHANSPEC_CHAN_MASK;
		join_params->params.chanspec_list[0] |= chanspec;
		join_params->params.chanspec_list[0] =
			wl_ext_chspec_host_to_driver(ioctl_ver,
				join_params->params.chanspec_list[0]);

		join_params->params.chanspec_num =
			htod32(join_params->params.chanspec_num);
	}
}

#if defined(WL_EXT_IAPSTA) || defined(WL_CFG80211) || defined(WL_ESCAN)
static chanspec_t
wl_ext_chspec_from_legacy(chanspec_t legacy_chspec)
{
	chanspec_t chspec;

	/* get the channel number */
	chspec = LCHSPEC_CHANNEL(legacy_chspec);

	/* convert the band */
	if (LCHSPEC_IS2G(legacy_chspec)) {
		chspec |= WL_CHANSPEC_BAND_2G;
	} else {
		chspec |= WL_CHANSPEC_BAND_5G;
	}

	/* convert the bw and sideband */
	if (LCHSPEC_IS20(legacy_chspec)) {
		chspec |= WL_CHANSPEC_BW_20;
	} else {
		chspec |= WL_CHANSPEC_BW_40;
		if (LCHSPEC_CTL_SB(legacy_chspec) == WL_LCHANSPEC_CTL_SB_LOWER) {
			chspec |= WL_CHANSPEC_CTL_SB_L;
		} else {
			chspec |= WL_CHANSPEC_CTL_SB_U;
		}
	}

	if (wf_chspec_malformed(chspec)) {
		AEXT_ERROR("wlan", "output chanspec (0x%04X) malformed\n", chspec);
		return INVCHANSPEC;
	}

	return chspec;
}

static chanspec_t
wl_ext_chspec_driver_to_host(int ioctl_ver, chanspec_t chanspec)
{
	chanspec = dtohchanspec(chanspec);
	if (ioctl_ver == 1) {
		chanspec = wl_ext_chspec_from_legacy(chanspec);
	}

	return chanspec;
}
#endif /* WL_EXT_IAPSTA || WL_CFG80211 || WL_ESCAN */

bool
wl_ext_check_scan(struct net_device *dev, dhd_pub_t *dhdp)
{
#ifdef WL_CFG80211
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
#endif /* WL_CFG80211 */
#ifdef WL_ESCAN
	struct wl_escan_info *escan = dhdp->escan;
#endif /* WL_ESCAN */

#ifdef WL_CFG80211
	if (wl_get_drv_status_all(cfg, SCANNING)) {
		AEXT_ERROR(dev->name, "cfg80211 scanning...\n");
		return TRUE;
	}
#endif /* WL_CFG80211 */

#ifdef WL_ESCAN
	if (escan->escan_state == ESCAN_STATE_SCANING) {
		AEXT_ERROR(dev->name, "escan scanning...\n");
		return TRUE;
	}
#endif /* WL_ESCAN */

	return FALSE;
}

#if defined(WL_CFG80211) || defined(WL_ESCAN)
void
wl_ext_user_sync(struct dhd_pub *dhd, int ifidx, bool lock)
{
	struct net_device *dev = dhd_idx2net(dhd, ifidx);
#ifdef WL_CFG80211
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
#endif /* WL_CFG80211 */
#ifdef WL_ESCAN
	struct wl_escan_info *escan = dhd->escan;
#endif /* WL_ESCAN */

	AEXT_INFO(dev->name, "lock=%d\n", lock);

	if (lock) {
#if defined(WL_CFG80211)
		mutex_lock(&cfg->usr_sync);
#endif
#if defined(WL_ESCAN)
		mutex_lock(&escan->usr_sync);
#endif
	} else {
#if defined(WL_CFG80211)
		mutex_unlock(&cfg->usr_sync);
#endif
#if defined(WL_ESCAN)
		mutex_unlock(&escan->usr_sync);
#endif
	}
}

bool
wl_ext_event_complete(struct dhd_pub *dhd, int ifidx)
{
	struct net_device *dev = dhd_idx2net(dhd, ifidx);
#ifdef WL_CFG80211
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
#endif /* WL_CFG80211 */
#ifdef WL_ESCAN
	struct wl_escan_info *escan = dhd->escan;
#endif /* WL_ESCAN */
	bool complete = TRUE;

#ifdef WL_CFG80211
	if (wl_get_drv_status_all(cfg, SCANNING)) {
		AEXT_INFO(dev->name, "SCANNING\n");
		complete = FALSE;
	}
	if (wl_get_drv_status_all(cfg, CONNECTING)) {
		AEXT_INFO(dev->name, "CONNECTING\n");
		complete = FALSE;
	}
	if (wl_get_drv_status_all(cfg, DISCONNECTING)) {
		AEXT_INFO(dev->name, "DISCONNECTING\n");
		complete = FALSE;
	}
#endif /* WL_CFG80211 */
#ifdef WL_ESCAN
	if (escan->escan_state == ESCAN_STATE_SCANING) {
		AEXT_INFO(dev->name, "ESCAN_STATE_SCANING\n");
		complete = FALSE;
	}
#endif /* WL_ESCAN */
	if (dhd->conf->eapol_status >= EAPOL_STATUS_4WAY_START &&
			dhd->conf->eapol_status < EAPOL_STATUS_4WAY_DONE) {
		AEXT_INFO(dev->name, "4-WAY handshaking\n");
		complete = FALSE;
	}

	return complete;
}
#endif /* WL_CFG80211 && WL_ESCAN */

static int
wl_ext_get_ioctl_ver(struct net_device *dev, int *ioctl_ver)
{
	int ret = 0;
	s32 val = 0;

	val = 1;
	ret = wl_ext_ioctl(dev, WLC_GET_VERSION, &val, sizeof(val), 0);
	if (ret) {
		return ret;
	}
	val = dtoh32(val);
	if (val != WLC_IOCTL_VERSION && val != 1) {
		AEXT_ERROR(dev->name, "Version mismatch, please upgrade. Got %d, expected %d or 1\n",
			val, WLC_IOCTL_VERSION);
		return BCME_VERSION;
	}
	*ioctl_ver = val;

	return ret;
}

static int
wl_ext_set_chanspec(struct net_device *dev, int ioctl_ver,
	uint16 channel, chanspec_t *ret_chspec)
{
	s32 _chan = channel;
	chanspec_t chspec = 0;
	chanspec_t fw_chspec = 0;
	u32 bw = WL_CHANSPEC_BW_20;
	s32 err = BCME_OK;
	s32 bw_cap = 0;
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	struct {
		u32 band;
		u32 bw_cap;
	} param = {0, 0};
	uint band;

	if (_chan <= CH_MAX_2G_CHANNEL)
		band = IEEE80211_BAND_2GHZ;
	else
		band = IEEE80211_BAND_5GHZ;

	if (band == IEEE80211_BAND_5GHZ) {
		param.band = WLC_BAND_5G;
		err = wl_ext_iovar_getbuf(dev, "bw_cap", &param, sizeof(param),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		if (err) {
			if (err != BCME_UNSUPPORTED) {
				AEXT_ERROR(dev->name, "bw_cap failed, %d\n", err);
				return err;
			} else {
				err = wl_ext_iovar_getint(dev, "mimo_bw_cap", &bw_cap);
				if (bw_cap != WLC_N_BW_20ALL)
					bw = WL_CHANSPEC_BW_40;
			}
		} else {
			if (WL_BW_CAP_80MHZ(iovar_buf[0]))
				bw = WL_CHANSPEC_BW_80;
			else if (WL_BW_CAP_40MHZ(iovar_buf[0]))
				bw = WL_CHANSPEC_BW_40;
			else
				bw = WL_CHANSPEC_BW_20;

		}
	}
	else if (band == IEEE80211_BAND_2GHZ)
		bw = WL_CHANSPEC_BW_20;

set_channel:
	chspec = wf_channel2chspec(_chan, bw);
	if (wf_chspec_valid(chspec)) {
		fw_chspec = wl_ext_chspec_host_to_driver(ioctl_ver, chspec);
		if (fw_chspec != INVCHANSPEC) {
			if ((err = wl_ext_iovar_setint(dev, "chanspec", fw_chspec)) == BCME_BADCHAN) {
				if (bw == WL_CHANSPEC_BW_80)
					goto change_bw;
				err = wl_ext_ioctl(dev, WLC_SET_CHANNEL, &_chan, sizeof(_chan), 1);
				WL_MSG(dev->name, "channel %d\n", _chan);
			} else if (err) {
				AEXT_ERROR(dev->name, "failed to set chanspec error %d\n", err);
			} else
				WL_MSG(dev->name, "channel %d, 0x%x\n", channel, chspec);
		} else {
			AEXT_ERROR(dev->name, "failed to convert host chanspec to fw chanspec\n");
			err = BCME_ERROR;
		}
	} else {
change_bw:
		if (bw == WL_CHANSPEC_BW_80)
			bw = WL_CHANSPEC_BW_40;
		else if (bw == WL_CHANSPEC_BW_40)
			bw = WL_CHANSPEC_BW_20;
		else
			bw = 0;
		if (bw)
			goto set_channel;
		AEXT_ERROR(dev->name, "Invalid chanspec 0x%x\n", chspec);
		err = BCME_ERROR;
	}
	*ret_chspec = fw_chspec;

	return err;
}

static int
wl_ext_channel(struct net_device *dev, char* command, int total_len)
{
	int ret;
	int channel=0;
	channel_info_t ci;
	int bytes_written = 0;
	chanspec_t fw_chspec;
	int ioctl_ver = 0;

	AEXT_TRACE(dev->name, "cmd %s", command);

	sscanf(command, "%*s %d", &channel);

	if (channel > 0) {
		wl_ext_get_ioctl_ver(dev, &ioctl_ver);
		ret = wl_ext_set_chanspec(dev, ioctl_ver, channel, &fw_chspec);
	} else {
		if (!(ret = wl_ext_ioctl(dev, WLC_GET_CHANNEL, &ci,
				sizeof(channel_info_t), FALSE))) {
			AEXT_TRACE(dev->name, "hw_channel %d\n", ci.hw_channel);
			AEXT_TRACE(dev->name, "target_channel %d\n", ci.target_channel);
			AEXT_TRACE(dev->name, "scan_channel %d\n", ci.scan_channel);
			bytes_written = snprintf(command, sizeof(channel_info_t)+2,
				"channel %d", ci.hw_channel);
			AEXT_TRACE(dev->name, "command result is %s\n", command);
			ret = bytes_written;
		}
	}

	return ret;
}

static int
wl_ext_channels(struct net_device *dev, char* command, int total_len)
{
	int ret, i;
	int bytes_written = -1;
	u8 valid_chan_list[sizeof(u32)*(WL_NUMCHANNELS + 1)];
	wl_uint32_list_t *list;

	AEXT_TRACE(dev->name, "cmd %s", command);

	memset(valid_chan_list, 0, sizeof(valid_chan_list));
	list = (wl_uint32_list_t *)(void *) valid_chan_list;
	list->count = htod32(WL_NUMCHANNELS);
	ret = wl_ext_ioctl(dev, WLC_GET_VALID_CHANNELS, valid_chan_list,
		sizeof(valid_chan_list), 0);
	if (ret<0) {
		AEXT_ERROR(dev->name, "get channels failed with %d\n", ret);
	} else {
		bytes_written = snprintf(command, total_len, "channels");
		for (i = 0; i < dtoh32(list->count); i++) {
			bytes_written += snprintf(command+bytes_written, total_len, " %d",
				dtoh32(list->element[i]));
		}
		AEXT_TRACE(dev->name, "command result is %s\n", command);
		ret = bytes_written;
	}

	return ret;
}

static int
wl_ext_roam_trigger(struct net_device *dev, char* command, int total_len)
{
	int ret = 0;
	int roam_trigger[2] = {0, 0};
	int trigger[2]= {0, 0};
	int bytes_written=-1;

	sscanf(command, "%*s %10d", &roam_trigger[0]);

	if (roam_trigger[0]) {
		roam_trigger[1] = WLC_BAND_ALL;
		ret = wl_ext_ioctl(dev, WLC_SET_ROAM_TRIGGER, roam_trigger,
			sizeof(roam_trigger), 1);
	} else {
		roam_trigger[1] = WLC_BAND_2G;
		ret = wl_ext_ioctl(dev, WLC_GET_ROAM_TRIGGER, roam_trigger,
			sizeof(roam_trigger), 0);
		if (!ret)
			trigger[0] = roam_trigger[0];

		roam_trigger[1] = WLC_BAND_5G;
		ret = wl_ext_ioctl(dev, WLC_GET_ROAM_TRIGGER, &roam_trigger,
			sizeof(roam_trigger), 0);
		if (!ret)
			trigger[1] = roam_trigger[0];

		AEXT_TRACE(dev->name, "roam_trigger %d %d\n", trigger[0], trigger[1]);
		bytes_written = snprintf(command, total_len, "%d %d", trigger[0], trigger[1]);
		ret = bytes_written;
	}

	return ret;
}

static int
wl_ext_pm(struct net_device *dev, char *command, int total_len)
{
	int pm=-1, ret = -1;
	char *pm_local;
	int bytes_written=-1;

	AEXT_TRACE(dev->name, "cmd %s", command);

	sscanf(command, "%*s %d", &pm);

	if (pm >= 0) {
		ret = wl_ext_ioctl(dev, WLC_SET_PM, &pm, sizeof(pm), 1);
	} else {
		ret = wl_ext_ioctl(dev, WLC_GET_PM, &pm, sizeof(pm), 0);
		if (!ret) {
			AEXT_TRACE(dev->name, "PM = %d", pm);
			if (pm == PM_OFF)
				pm_local = "PM_OFF";
			else if(pm == PM_MAX)
				pm_local = "PM_MAX";
			else if(pm == PM_FAST)
				pm_local = "PM_FAST";
			else {
				pm = 0;
				pm_local = "Invalid";
			}
			bytes_written = snprintf(command, total_len, "PM %s", pm_local);
			AEXT_TRACE(dev->name, "command result is %s\n", command);
			ret = bytes_written;
		}
	}

	return ret;
}

static int
wl_ext_monitor(struct net_device *dev, char *command, int total_len)
{
	int val = -1, ret = -1;
	int bytes_written=-1;

	sscanf(command, "%*s %d", &val);

	if (val >=0) {
		ret = wl_ext_ioctl(dev, WLC_SET_MONITOR, &val, sizeof(val), 1);
	} else {
		ret = wl_ext_ioctl(dev, WLC_GET_MONITOR, &val, sizeof(val), 0);
		if (!ret) {
			AEXT_TRACE(dev->name, "monitor = %d\n", val);
			bytes_written = snprintf(command, total_len, "monitor %d", val);
			AEXT_TRACE(dev->name, "command result is %s\n", command);
			ret = bytes_written;
		}
	}

	return ret;
}

s32
wl_ext_connect(struct net_device *dev, struct wl_conn_info *conn_info)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	wl_extjoin_params_t *ext_join_params = NULL;
	struct wl_join_params join_params;
	size_t join_params_size;
	s32 err = 0;
	u32 chan_cnt = 0;
	s8 *iovar_buf = NULL;
	int ioctl_ver = 0;
	char sec[32];

	wl_ext_get_ioctl_ver(dev, &ioctl_ver);

	if (dhd->conf->chip == BCM43362_CHIP_ID)
		goto set_ssid;

	if (conn_info->channel) {
		chan_cnt = 1;
	}

	iovar_buf = kzalloc(WLC_IOCTL_MAXLEN, GFP_KERNEL);
	if (iovar_buf == NULL) {
		err = -ENOMEM;
		goto exit;
	}

	/*
	 *	Join with specific BSSID and cached SSID
	 *	If SSID is zero join based on BSSID only
	 */
	join_params_size = WL_EXTJOIN_PARAMS_FIXED_SIZE +
		chan_cnt * sizeof(chanspec_t);
	ext_join_params =  (wl_extjoin_params_t*)kzalloc(join_params_size, GFP_KERNEL);
	if (ext_join_params == NULL) {
		err = -ENOMEM;
		goto exit;
	}
	ext_join_params->ssid.SSID_len = min((uint32)sizeof(ext_join_params->ssid.SSID),
		conn_info->ssid.SSID_len);
	memcpy(&ext_join_params->ssid.SSID, conn_info->ssid.SSID, ext_join_params->ssid.SSID_len);
	ext_join_params->ssid.SSID_len = htod32(ext_join_params->ssid.SSID_len);
	/* increate dwell time to receive probe response or detect Beacon
	* from target AP at a noisy air only during connect command
	*/
	ext_join_params->scan.active_time = chan_cnt ? WL_SCAN_JOIN_ACTIVE_DWELL_TIME_MS : -1;
	ext_join_params->scan.passive_time = chan_cnt ? WL_SCAN_JOIN_PASSIVE_DWELL_TIME_MS : -1;
	/* Set up join scan parameters */
	ext_join_params->scan.scan_type = -1;
	ext_join_params->scan.nprobes = chan_cnt ?
		(ext_join_params->scan.active_time/WL_SCAN_JOIN_PROBE_INTERVAL_MS) : -1;
	ext_join_params->scan.home_time = -1;

	if (memcmp(&ether_null, &conn_info->bssid, ETHER_ADDR_LEN))
		memcpy(&ext_join_params->assoc.bssid, &conn_info->bssid, ETH_ALEN);
	else
		memcpy(&ext_join_params->assoc.bssid, &ether_bcast, ETH_ALEN);
	ext_join_params->assoc.chanspec_num = chan_cnt;
	if (chan_cnt) {
		u16 band, bw, ctl_sb;
		chanspec_t chspec;
		band = (conn_info->channel <= CH_MAX_2G_CHANNEL) ? WL_CHANSPEC_BAND_2G
			: WL_CHANSPEC_BAND_5G;
		bw = WL_CHANSPEC_BW_20;
		ctl_sb = WL_CHANSPEC_CTL_SB_NONE;
		chspec = (conn_info->channel | band | bw | ctl_sb);
		ext_join_params->assoc.chanspec_list[0]  &= WL_CHANSPEC_CHAN_MASK;
		ext_join_params->assoc.chanspec_list[0] |= chspec;
		ext_join_params->assoc.chanspec_list[0] =
			wl_ext_chspec_host_to_driver(ioctl_ver,
				ext_join_params->assoc.chanspec_list[0]);
	}
	ext_join_params->assoc.chanspec_num = htod32(ext_join_params->assoc.chanspec_num);

	wl_ext_get_sec(dev, 0, sec, sizeof(sec));
	WL_MSG(dev->name,
		"Connecting with %pM channel (%d) ssid \"%s\", len (%d), sec=%s\n\n",
		&ext_join_params->assoc.bssid, conn_info->channel,
		ext_join_params->ssid.SSID, ext_join_params->ssid.SSID_len, sec);
	err = wl_ext_iovar_setbuf_bsscfg(dev, "join", ext_join_params,
		join_params_size, iovar_buf, WLC_IOCTL_MAXLEN, conn_info->bssidx, NULL);

	if (err) {
		if (err == BCME_UNSUPPORTED) {
			AEXT_TRACE(dev->name, "join iovar is not supported\n");
			goto set_ssid;
		} else {
			AEXT_ERROR(dev->name, "error (%d)\n", err);
			goto exit;
		}
	} else
		goto exit;

set_ssid:
	memset(&join_params, 0, sizeof(join_params));
	join_params_size = sizeof(join_params.ssid);

	join_params.ssid.SSID_len = min((uint32)sizeof(join_params.ssid.SSID),
		conn_info->ssid.SSID_len);
	memcpy(&join_params.ssid.SSID, conn_info->ssid.SSID, join_params.ssid.SSID_len);
	join_params.ssid.SSID_len = htod32(join_params.ssid.SSID_len);
	if (memcmp(&ether_null, &conn_info->bssid, ETHER_ADDR_LEN))
		memcpy(&join_params.params.bssid, &conn_info->bssid, ETH_ALEN);
	else
		memcpy(&join_params.params.bssid, &ether_bcast, ETH_ALEN);

	wl_ext_ch_to_chanspec(ioctl_ver, conn_info->channel, &join_params, &join_params_size);
	AEXT_TRACE(dev->name, "join_param_size %zu\n", join_params_size);

	if (join_params.ssid.SSID_len < IEEE80211_MAX_SSID_LEN) {
		AEXT_INFO(dev->name, "ssid \"%s\", len (%d)\n", join_params.ssid.SSID,
			join_params.ssid.SSID_len);
	}
	wl_ext_get_sec(dev, 0, sec, sizeof(sec));
	WL_MSG(dev->name,
		"Connecting with %pM channel (%d) ssid \"%s\", len (%d), sec=%s\n\n",
		&join_params.params.bssid, conn_info->channel,
		join_params.ssid.SSID, join_params.ssid.SSID_len, sec);
	err = wl_ext_ioctl(dev, WLC_SET_SSID, &join_params, join_params_size, 1);

exit:
	if (iovar_buf)
		kfree(iovar_buf);
	if (ext_join_params)
		kfree(ext_join_params);
	return err;

}

void
wl_ext_get_sec(struct net_device *dev, int ifmode, char *sec, int total_len)
{
	int auth=0, wpa_auth=0, wsec=0, mfp=0;
	int bytes_written=0;

	memset(sec, 0, total_len);
	wl_ext_iovar_getint(dev, "auth", &auth);
	wl_ext_iovar_getint(dev, "wpa_auth", &wpa_auth);
	wl_ext_iovar_getint(dev, "wsec", &wsec);
	wldev_iovar_getint(dev, "mfp", &mfp);

#ifdef WL_EXT_IAPSTA
	if (ifmode == IMESH_MODE) {
		if (auth == WL_AUTH_OPEN_SYSTEM && wpa_auth == WPA_AUTH_DISABLED) {
			bytes_written += snprintf(sec+bytes_written, total_len, "open");
		} else if (auth == WL_AUTH_OPEN_SYSTEM && wpa_auth == WPA2_AUTH_PSK) {
			bytes_written += snprintf(sec+bytes_written, total_len, "sae");
		} else {
			bytes_written += snprintf(sec+bytes_written, total_len, "%d/0x%x",
				auth, wpa_auth);
		}
	} else
#endif /* WL_EXT_IAPSTA */
	{
		if (auth == WL_AUTH_OPEN_SYSTEM && wpa_auth == WPA_AUTH_DISABLED) {
			bytes_written += snprintf(sec+bytes_written, total_len, "open");
		} else if (auth == WL_AUTH_SHARED_KEY && wpa_auth == WPA_AUTH_DISABLED) {
			bytes_written += snprintf(sec+bytes_written, total_len, "shared");
		} else if (auth == WL_AUTH_OPEN_SYSTEM && wpa_auth == WPA_AUTH_PSK) {
			bytes_written += snprintf(sec+bytes_written, total_len, "wpapsk");
		} else if (auth == WL_AUTH_OPEN_SYSTEM && wpa_auth == WPA2_AUTH_PSK) {
			bytes_written += snprintf(sec+bytes_written, total_len, "wpa2psk");
		} else if (auth == WL_AUTH_OPEN_SHARED && wpa_auth == WPA3_AUTH_SAE_PSK) {
			bytes_written += snprintf(sec+bytes_written, total_len, "wpa3");
		} else {
			bytes_written += snprintf(sec+bytes_written, total_len, "%d/0x%x",
				auth, wpa_auth);
		}
	}

	if (mfp == WL_MFP_NONE) {
		bytes_written += snprintf(sec+bytes_written, total_len, "/mfpn");
	} else if (mfp == WL_MFP_CAPABLE) {
		bytes_written += snprintf(sec+bytes_written, total_len, "/mfpc");
	} else if (mfp == WL_MFP_REQUIRED) {
		bytes_written += snprintf(sec+bytes_written, total_len, "/mfpr");
	} else {
		bytes_written += snprintf(sec+bytes_written, total_len, "/%d", mfp);
	}

#ifdef WL_EXT_IAPSTA
	if (ifmode == IMESH_MODE) {
		if (wsec == WSEC_NONE) {
			bytes_written += snprintf(sec+bytes_written, total_len, "/none");
		} else {
			bytes_written += snprintf(sec+bytes_written, total_len, "/aes");
		}
	} else
#endif /* WL_EXT_IAPSTA */
	{
		if (wsec == WSEC_NONE) {
			bytes_written += snprintf(sec+bytes_written, total_len, "/none");
		} else if (wsec == WEP_ENABLED) {
			bytes_written += snprintf(sec+bytes_written, total_len, "/wep");
		} else if (wsec == (TKIP_ENABLED|AES_ENABLED) ||
				wsec == (WSEC_SWFLAG|TKIP_ENABLED|AES_ENABLED)) {
			bytes_written += snprintf(sec+bytes_written, total_len, "/tkipaes");
		} else if (wsec == TKIP_ENABLED || wsec == (WSEC_SWFLAG|TKIP_ENABLED)) {
			bytes_written += snprintf(sec+bytes_written, total_len, "/tkip");
		} else if (wsec == AES_ENABLED || wsec == (WSEC_SWFLAG|AES_ENABLED)) {
			bytes_written += snprintf(sec+bytes_written, total_len, "/aes");
		} else {
			bytes_written += snprintf(sec+bytes_written, total_len, "/0x%x", wsec);
		}
	}

}

static bool
wl_ext_dfs_chan(uint16 chan)
{
	if (chan >= 52 && chan <= 144)
		return TRUE;
	return FALSE;
}

static uint16
wl_ext_get_default_chan(struct net_device *dev,
	uint16 *chan_2g, uint16 *chan_5g, bool nodfs)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	uint16 chan_tmp = 0, chan = 0;
	wl_uint32_list_t *list;
	u8 valid_chan_list[sizeof(u32)*(WL_NUMCHANNELS + 1)];
	s32 ret = BCME_OK;
	int i;

	*chan_2g = 0;
	*chan_5g = 0;
	memset(valid_chan_list, 0, sizeof(valid_chan_list));
	list = (wl_uint32_list_t *)(void *) valid_chan_list;
	list->count = htod32(WL_NUMCHANNELS);
	ret = wl_ext_ioctl(dev, WLC_GET_VALID_CHANNELS, valid_chan_list,
		sizeof(valid_chan_list), 0);
	if (ret == 0) {
		for (i=0; i<dtoh32(list->count); i++) {
			chan_tmp = dtoh32(list->element[i]);
			if (!dhd_conf_match_channel(dhd, chan_tmp))
				continue;
			if (chan_tmp <= 13) {
				*chan_2g = chan_tmp;
			} else {
				if (wl_ext_dfs_chan(chan_tmp) && nodfs)
					continue;
				else if (chan_tmp >= 36 && chan_tmp <= 161)
					*chan_5g = chan_tmp;
			}
		}
	}

	return chan;
}

#if defined(SENDPROB) || (defined(WLMESH) && defined(WL_ESCAN))
static int
wl_ext_add_del_ie(struct net_device *dev, uint pktflag, char *ie_data, const char* add_del_cmd)
{
	vndr_ie_setbuf_t *vndr_ie = NULL;
	char iovar_buf[WLC_IOCTL_SMLEN]="\0";
	int ie_data_len = 0, tot_len = 0, iecount;
	int err = -1;

	if (!strlen(ie_data)) {
		AEXT_ERROR(dev->name, "wrong ie %s\n", ie_data);
		goto exit;
	}

	tot_len = (int)(sizeof(vndr_ie_setbuf_t) + ((strlen(ie_data)-2)/2));
	vndr_ie = (vndr_ie_setbuf_t *) kzalloc(tot_len, GFP_KERNEL);
	if (!vndr_ie) {
		AEXT_ERROR(dev->name, "IE memory alloc failed\n");
		err = -ENOMEM;
		goto exit;
	}

	/* Copy the vndr_ie SET command ("add"/"del") to the buffer */
	strncpy(vndr_ie->cmd, add_del_cmd, VNDR_IE_CMD_LEN - 1);
	vndr_ie->cmd[VNDR_IE_CMD_LEN - 1] = '\0';

	/* Set the IE count - the buffer contains only 1 IE */
	iecount = htod32(1);
	memcpy((void *)&vndr_ie->vndr_ie_buffer.iecount, &iecount, sizeof(s32));

	/* Set packet flag to indicate that BEACON's will contain this IE */
	pktflag = htod32(pktflag);
	memcpy((void *)&vndr_ie->vndr_ie_buffer.vndr_ie_list[0].pktflag, &pktflag,
		sizeof(u32));

	/* Set the IE ID */
	vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.id = (uchar)DOT11_MNG_VS_ID;

	/* Set the IE LEN */
	vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.len = (strlen(ie_data)-2)/2;

	/* Set the IE OUI and DATA */
	ie_data_len = wl_pattern_atoh(ie_data,
		(char *)vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.oui);
	if (ie_data_len <= 0) {
		AEXT_ERROR(dev->name, "wrong ie_data_len %d\n", (int)strlen(ie_data)-2);
		goto exit;
	}

	err = wl_ext_iovar_setbuf(dev, "vndr_ie", vndr_ie, tot_len, iovar_buf,
		sizeof(iovar_buf), NULL);

exit:
	if (vndr_ie) {
		kfree(vndr_ie);
	}
	return err;
}
#endif /* SENDPROB || (WLMESH && WL_ESCAN) */

#ifdef WL_EXT_IAPSTA
static int
wl_ext_parse_wep(char *key, struct wl_wsec_key *wsec_key)
{
	char hex[] = "XX";
	unsigned char *data = wsec_key->data;
	char *keystr = key;

	switch (strlen(keystr)) {
	case 5:
	case 13:
	case 16:
		wsec_key->len = strlen(keystr);
		memcpy(data, keystr, wsec_key->len + 1);
		break;
	case 12:
	case 28:
	case 34:
	case 66:
		/* strip leading 0x */
		if (!strnicmp(keystr, "0x", 2))
			keystr += 2;
		else
			return -1;
		/* fall through */
	case 10:
	case 26:
	case 32:
	case 64:
		wsec_key->len = strlen(keystr) / 2;
		while (*keystr) {
			strncpy(hex, keystr, 2);
			*data++ = (char) strtoul(hex, NULL, 16);
			keystr += 2;
		}
		break;
	default:
		return -1;
	}

	switch (wsec_key->len) {
	case 5:
		wsec_key->algo = CRYPTO_ALGO_WEP1;
		break;
	case 13:
		wsec_key->algo = CRYPTO_ALGO_WEP128;
		break;
	case 16:
		/* default to AES-CCM */
		wsec_key->algo = CRYPTO_ALGO_AES_CCM;
		break;
	case 32:
		wsec_key->algo = CRYPTO_ALGO_TKIP;
		break;
	default:
		return -1;
	}

	/* Set as primary wsec_key by default */
	wsec_key->flags |= WL_PRIMARY_KEY;

	return 0;
}

static int
wl_ext_set_bgnmode(struct wl_if_info *cur_if)
{
	struct net_device *dev = cur_if->dev;
	bgnmode_t bgnmode = cur_if->bgnmode;
	int val;

	if (bgnmode == 0)
		return 0;

	wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
	if (bgnmode == IEEE80211B) {
		wl_ext_iovar_setint(dev, "nmode", 0);
		val = 0;
		wl_ext_ioctl(dev, WLC_SET_GMODE, &val, sizeof(val), 1);
		AEXT_TRACE(dev->name, "Network mode: B only\n");
	} else if (bgnmode == IEEE80211G) {
		wl_ext_iovar_setint(dev, "nmode", 0);
		val = 2;
		wl_ext_ioctl(dev, WLC_SET_GMODE, &val, sizeof(val), 1);
		AEXT_TRACE(dev->name, "Network mode: G only\n");
	} else if (bgnmode == IEEE80211BG) {
		wl_ext_iovar_setint(dev, "nmode", 0);
		val = 1;
		wl_ext_ioctl(dev, WLC_SET_GMODE, &val, sizeof(val), 1);
		AEXT_TRACE(dev->name, "Network mode: B/G mixed\n");
	} else if (bgnmode == IEEE80211BGN) {
		wl_ext_iovar_setint(dev, "nmode", 0);
		wl_ext_iovar_setint(dev, "nmode", 1);
		wl_ext_iovar_setint(dev, "vhtmode", 0);
		val = 1;
		wl_ext_ioctl(dev, WLC_SET_GMODE, &val, sizeof(val), 1);
		AEXT_TRACE(dev->name, "Network mode: B/G/N mixed\n");
	} else if (bgnmode == IEEE80211BGNAC) {
		wl_ext_iovar_setint(dev, "nmode", 0);
		wl_ext_iovar_setint(dev, "nmode", 1);
		wl_ext_iovar_setint(dev, "vhtmode", 1);
		val = 1;
		wl_ext_ioctl(dev, WLC_SET_GMODE, &val, sizeof(val), 1);
		AEXT_TRACE(dev->name, "Network mode: B/G/N/AC mixed\n");
	}
	wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);

	return 0;
}

static int
wl_ext_set_amode(struct wl_if_info *cur_if)
{
	struct net_device *dev = cur_if->dev;
	authmode_t amode = cur_if->amode;
	int auth=0, wpa_auth=0;

#ifdef WLMESH
	if (cur_if->ifmode == IMESH_MODE) {
		if (amode == AUTH_SAE) {
			auth = WL_AUTH_OPEN_SYSTEM;
			wpa_auth = WPA2_AUTH_PSK;
			AEXT_INFO(dev->name, "SAE\n");
		} else {
			auth = WL_AUTH_OPEN_SYSTEM;
			wpa_auth = WPA_AUTH_DISABLED;
			AEXT_INFO(dev->name, "Open System\n");
		}
	} else
#endif /* WLMESH */
	if (amode == AUTH_OPEN) {
		auth = WL_AUTH_OPEN_SYSTEM;
		wpa_auth = WPA_AUTH_DISABLED;
		AEXT_INFO(dev->name, "Open System\n");
	} else if (amode == AUTH_SHARED) {
		auth = WL_AUTH_SHARED_KEY;
		wpa_auth = WPA_AUTH_DISABLED;
		AEXT_INFO(dev->name, "Shared Key\n");
	} else if (amode == AUTH_WPAPSK) {
		auth = WL_AUTH_OPEN_SYSTEM;
		wpa_auth = WPA_AUTH_PSK;
		AEXT_INFO(dev->name, "WPA-PSK\n");
	} else if (amode == AUTH_WPA2PSK) {
		auth = WL_AUTH_OPEN_SYSTEM;
		wpa_auth = WPA2_AUTH_PSK;
		AEXT_INFO(dev->name, "WPA2-PSK\n");
	} else if (amode == AUTH_WPAWPA2PSK) {
		auth = WL_AUTH_OPEN_SYSTEM;
		wpa_auth = WPA2_AUTH_PSK | WPA_AUTH_PSK;
		AEXT_INFO(dev->name, "WPA/WPA2-PSK\n");
	}
#ifdef WLMESH
	if (cur_if->ifmode == IMESH_MODE) {
		s32 val = WL_BSSTYPE_MESH;
		wl_ext_ioctl(dev, WLC_SET_INFRA, &val, sizeof(val), 1);
	} else
#endif /* WLMESH */
	if (cur_if->ifmode == ISTA_MODE) {
		s32 val = WL_BSSTYPE_INFRA;
		wl_ext_ioctl(dev, WLC_SET_INFRA, &val, sizeof(val), 1);
	}
	wl_ext_iovar_setint(dev, "auth", auth);

	wl_ext_iovar_setint(dev, "wpa_auth", wpa_auth);

	return 0;
}

static int
wl_ext_set_emode(struct wl_apsta_params *apsta_params,
	struct wl_if_info *cur_if)
{
	struct net_device *dev = cur_if->dev;
	int wsec=0;
	struct wl_wsec_key wsec_key;
	wsec_pmk_t psk;
	authmode_t amode = cur_if->amode;
	encmode_t emode = cur_if->emode;
	char *key = cur_if->key;
	struct dhd_pub *dhd = apsta_params->dhd;

	memset(&wsec_key, 0, sizeof(wsec_key));
	memset(&psk, 0, sizeof(psk));

#ifdef WLMESH
	if (cur_if->ifmode == IMESH_MODE) {
		if (amode == AUTH_SAE) {
			wsec = AES_ENABLED;
		} else {
			wsec = WSEC_NONE;
		}
	} else
#endif /* WLMESH */
	if (emode == ENC_NONE) {
		wsec = WSEC_NONE;
		AEXT_INFO(dev->name, "No securiy\n");
	} else if (emode == ENC_WEP) {
		wsec = WEP_ENABLED;
		wl_ext_parse_wep(key, &wsec_key);
		AEXT_INFO(dev->name, "WEP key \"%s\"\n", wsec_key.data);
	} else if (emode == ENC_TKIP) {
		wsec = TKIP_ENABLED;
		psk.key_len = strlen(key);
		psk.flags = WSEC_PASSPHRASE;
		memcpy(psk.key, key, strlen(key));
		AEXT_INFO(dev->name, "TKIP key \"%s\"\n", psk.key);
	} else if (emode == ENC_AES || amode == AUTH_SAE) {
		wsec = AES_ENABLED;
		psk.key_len = strlen(key);
		psk.flags = WSEC_PASSPHRASE;
		memcpy(psk.key, key, strlen(key));
		AEXT_INFO(dev->name, "AES key \"%s\"\n", psk.key);
	} else if (emode == ENC_TKIPAES) {
		wsec = TKIP_ENABLED | AES_ENABLED;
		psk.key_len = strlen(key);
		psk.flags = WSEC_PASSPHRASE;
		memcpy(psk.key, key, strlen(key));
		AEXT_INFO(dev->name, "TKIP/AES key \"%s\"\n", psk.key);
	}
	if (dhd->conf->chip == BCM43430_CHIP_ID && cur_if->ifidx > 0 && wsec >= 2 &&
			apsta_params->apstamode == ISTAAP_MODE) {
		wsec |= WSEC_SWFLAG; // terence 20180628: fix me, this is a workaround
	}

	wl_ext_iovar_setint(dev, "wsec", wsec);

#ifdef WLMESH
	if (cur_if->ifmode == IMESH_MODE) {
		if (amode == AUTH_SAE) {
			s8 iovar_buf[WLC_IOCTL_SMLEN];
			AEXT_INFO(dev->name, "AES key \"%s\"\n", key);
			wl_ext_iovar_setint(dev, "mesh_auth_proto", 1);
			wl_ext_iovar_setint(dev, "mfp", WL_MFP_REQUIRED);
			wl_ext_iovar_setbuf(dev, "sae_password", key, strlen(key),
				iovar_buf, WLC_IOCTL_SMLEN, NULL);
		} else {
			AEXT_INFO(dev->name, "No securiy\n");
			wl_ext_iovar_setint(dev, "mesh_auth_proto", 0);
			wl_ext_iovar_setint(dev, "mfp", WL_MFP_NONE);
		}
	} else
#endif /* WLMESH */
	if (emode == ENC_WEP) {
		wl_ext_ioctl(dev, WLC_SET_KEY, &wsec_key, sizeof(wsec_key), 1);
	} else if (emode == ENC_TKIP || emode == ENC_AES || emode == ENC_TKIPAES) {
		if (cur_if->ifmode == ISTA_MODE)
			wl_ext_iovar_setint(dev, "sup_wpa", 1);
		wl_ext_ioctl(dev, WLC_SET_WSEC_PMK, &psk, sizeof(psk), 1);
	}

	return 0;
}

static u32
wl_ext_get_chanspec(struct wl_apsta_params *apsta_params,
	struct net_device *dev)
{
	int ret = 0;
	struct ether_addr bssid;
	u32 chanspec = 0;

	ret = wldev_ioctl(dev, WLC_GET_BSSID, &bssid, sizeof(bssid), 0);
	if (ret != BCME_NOTASSOCIATED && memcmp(&ether_null, &bssid, ETHER_ADDR_LEN)) {
		if (wl_ext_iovar_getint(dev, "chanspec", (s32 *)&chanspec) == BCME_OK) {
			chanspec = wl_ext_chspec_driver_to_host(apsta_params->ioctl_ver, chanspec);
			return chanspec;
		}
	}

	return 0;
}

static uint16
wl_ext_get_chan(struct wl_apsta_params *apsta_params, struct net_device *dev)
{
	int ret = 0;
	uint16 chan = 0, ctl_chan;
	struct ether_addr bssid;
	u32 chanspec = 0;

	ret = wldev_ioctl(dev, WLC_GET_BSSID, &bssid, sizeof(bssid), 0);
	if (ret != BCME_NOTASSOCIATED && memcmp(&ether_null, &bssid, ETHER_ADDR_LEN)) {
		if (wl_ext_iovar_getint(dev, "chanspec", (s32 *)&chanspec) == BCME_OK) {
			chanspec = wl_ext_chspec_driver_to_host(apsta_params->ioctl_ver, chanspec);
			ctl_chan = wf_chspec_ctlchan(chanspec);
			chan = (u16)(ctl_chan & 0x00FF);
			return chan;
		}
	}

	return 0;
}

static chanspec_t
wl_ext_chan_to_chanspec(struct wl_apsta_params *apsta_params,
	struct net_device *dev, uint16 channel)
{
	s32 _chan = channel;
	chanspec_t chspec = 0;
	chanspec_t fw_chspec = 0;
	u32 bw = WL_CHANSPEC_BW_20;
	s32 err = BCME_OK;
	s32 bw_cap = 0;
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	struct {
		u32 band;
		u32 bw_cap;
	} param = {0, 0};
	uint band;

	if (_chan <= CH_MAX_2G_CHANNEL)
		band = IEEE80211_BAND_2GHZ;
	else
		band = IEEE80211_BAND_5GHZ;

	if (band == IEEE80211_BAND_5GHZ) {
		param.band = WLC_BAND_5G;
		err = wl_ext_iovar_getbuf(dev, "bw_cap", &param, sizeof(param),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		if (err) {
			if (err != BCME_UNSUPPORTED) {
				AEXT_ERROR(dev->name, "bw_cap failed, %d\n", err);
				return err;
			} else {
				err = wl_ext_iovar_getint(dev, "mimo_bw_cap", &bw_cap);
				if (bw_cap != WLC_N_BW_20ALL)
					bw = WL_CHANSPEC_BW_40;
			}
		} else {
			if (WL_BW_CAP_80MHZ(iovar_buf[0]))
				bw = WL_CHANSPEC_BW_80;
			else if (WL_BW_CAP_40MHZ(iovar_buf[0]))
				bw = WL_CHANSPEC_BW_40;
			else
				bw = WL_CHANSPEC_BW_20;
		}
	}
	else if (band == IEEE80211_BAND_2GHZ)
		bw = WL_CHANSPEC_BW_20;

set_channel:
	chspec = wf_channel2chspec(_chan, bw);
	if (wf_chspec_valid(chspec)) {
		fw_chspec = wl_ext_chspec_host_to_driver(apsta_params->ioctl_ver, chspec);
		if (fw_chspec == INVCHANSPEC) {
			AEXT_ERROR(dev->name, "failed to convert host chanspec to fw chanspec\n");
			fw_chspec = 0;
		}
	} else {
		if (bw == WL_CHANSPEC_BW_80)
			bw = WL_CHANSPEC_BW_40;
		else if (bw == WL_CHANSPEC_BW_40)
			bw = WL_CHANSPEC_BW_20;
		else
			bw = 0;
		if (bw)
			goto set_channel;
		AEXT_ERROR(dev->name, "Invalid chanspec 0x%x\n", chspec);
		err = BCME_ERROR;
	}

	return fw_chspec;
}

static bool
wl_ext_radar_detect(struct net_device *dev)
{
	int ret = BCME_OK;
	bool radar = FALSE;
	s32 val = 0;

	if ((ret = wldev_ioctl(dev, WLC_GET_RADAR, &val, sizeof(int), false) == 0)) {
		radar = TRUE;
	}

	return radar;
}

#ifndef WL_STATIC_IF
static void
wl_ext_wait_netif_change(struct wl_apsta_params *apsta_params,
	struct wl_if_info *cur_if)
{
	rtnl_unlock();
	wait_event_interruptible_timeout(apsta_params->netif_change_event,
		wl_get_isam_status(cur_if, IF_READY),
		msecs_to_jiffies(MAX_AP_LINK_WAIT_TIME));
	rtnl_lock();
}

static void
wl_ext_interface_create(struct net_device *dev, struct wl_apsta_params *apsta_params,
	struct wl_if_info *cur_if, int iftype, u8 *addr)
{
	wl_interface_create_t iface;
	u8 iovar_buf[WLC_IOCTL_SMLEN];

	bzero(&iface, sizeof(iface));
	if (addr) {
		iftype |= WL_INTERFACE_MAC_USE;
	}
	iface.ver = WL_INTERFACE_CREATE_VER;
	iface.flags = iftype;
	if (addr) {
		memcpy(&iface.mac_addr.octet, addr, ETH_ALEN);
	}
	wl_set_isam_status(cur_if, IF_ADDING);
	wl_ext_iovar_getbuf(dev, "interface_create", &iface, sizeof(iface),
		iovar_buf, WLC_IOCTL_SMLEN, NULL);
	wl_ext_wait_netif_change(apsta_params, cur_if);
}

static void
wl_ext_iapsta_intf_add(struct net_device *dev, struct wl_apsta_params *apsta_params)
{
	struct dhd_pub *dhd;
	apstamode_t apstamode = apsta_params->apstamode;
	struct wl_if_info *cur_if;
	wlc_ssid_t ssid = { 0, {0} };
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	wl_p2p_if_t ifreq;
	struct ether_addr mac_addr;

	dhd = dhd_get_pub(dev);
	bzero(&mac_addr, sizeof(mac_addr));

	if (apstamode == ISTAAP_MODE) {
		cur_if = &apsta_params->if_info[IF_VIF];
		if (FW_SUPPORTED(dhd, rsdb)) {
			wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_CREATE_AP, NULL);
		} else {
			wl_set_isam_status(cur_if, IF_ADDING);
			wl_ext_iovar_setbuf_bsscfg(dev, "ssid", &ssid, sizeof(ssid),
				iovar_buf, WLC_IOCTL_SMLEN, 1, NULL);
			wl_ext_wait_netif_change(apsta_params, cur_if);
		}
	}
	else if (apstamode == ISTAGO_MODE) {
		bzero(&ifreq, sizeof(wl_p2p_if_t));
		ifreq.type = htod32(WL_P2P_IF_GO);
		cur_if = &apsta_params->if_info[IF_VIF];
		wl_set_isam_status(cur_if, IF_ADDING);
		wl_ext_iovar_setbuf(dev, "p2p_ifadd", &ifreq, sizeof(ifreq),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		wl_ext_wait_netif_change(apsta_params, cur_if);
	}
	else if (apstamode == ISTASTA_MODE) {
		cur_if = &apsta_params->if_info[IF_VIF];
		memcpy(&mac_addr, dev->dev_addr, ETHER_ADDR_LEN);
		mac_addr.octet[0] |= 0x02;
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_CREATE_STA,
			(u8*)&mac_addr);
	}
	else if (apstamode == IDUALAP_MODE) {
		cur_if = &apsta_params->if_info[IF_VIF];
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_CREATE_AP, NULL);
	}
	else if (apstamode == ISTAAPAP_MODE) {
		u8 rand_bytes[2] = {0, };
		get_random_bytes(&rand_bytes, sizeof(rand_bytes));
		cur_if = &apsta_params->if_info[IF_VIF];
		memcpy(&mac_addr, dev->dev_addr, ETHER_ADDR_LEN);
		mac_addr.octet[0] |= 0x02;
		mac_addr.octet[5] += 0x01;
		memcpy(&mac_addr.octet[3], rand_bytes, sizeof(rand_bytes));
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_CREATE_AP,
			(u8*)&mac_addr);
		cur_if = &apsta_params->if_info[IF_VIF2];
		memcpy(&mac_addr, dev->dev_addr, ETHER_ADDR_LEN);
		mac_addr.octet[0] |= 0x02;
		mac_addr.octet[5] += 0x02;
		memcpy(&mac_addr.octet[3], rand_bytes, sizeof(rand_bytes));
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_CREATE_AP,
			(u8*)&mac_addr);
	}
#ifdef WLMESH
	else if (apstamode == ISTAMESH_MODE) {
		cur_if = &apsta_params->if_info[IF_VIF];
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_CREATE_STA, NULL);
	}
	else if (apstamode == IMESHAP_MODE) {
		cur_if = &apsta_params->if_info[IF_VIF];
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_CREATE_AP, NULL);
	}
	else if (apstamode == ISTAAPMESH_MODE) {
		cur_if = &apsta_params->if_info[IF_VIF];
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_CREATE_AP, NULL);
		cur_if = &apsta_params->if_info[IF_VIF2];
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_CREATE_STA, NULL);
	}
	else if (apstamode == IMESHAPAP_MODE) {
		cur_if = &apsta_params->if_info[IF_VIF];
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_CREATE_AP, NULL);
		cur_if = &apsta_params->if_info[IF_VIF2];
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_CREATE_AP, NULL);
	}
#endif /* WLMESH */

}
#endif /* WL_STATIC_IF */

static void
wl_ext_iapsta_preinit(struct net_device *dev, struct wl_apsta_params *apsta_params)
{
	struct dhd_pub *dhd;
	apstamode_t apstamode = apsta_params->apstamode;
	struct wl_if_info *cur_if;
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	s32 val = 0;
	int i;

	dhd = dhd_get_pub(dev);

	for (i=0; i<MAX_IF_NUM; i++) {
		cur_if = &apsta_params->if_info[i];
		if (i >= 1 && !strlen(cur_if->ifname))
			snprintf(cur_if->ifname, IFNAMSIZ, "wlan%d", i);
		if (cur_if->ifmode == ISTA_MODE) {
			cur_if->channel = 0;
			cur_if->maxassoc = -1;
			cur_if->prio = PRIO_STA;
			cur_if->prefix = 'S';
			snprintf(cur_if->ssid, DOT11_MAX_SSID_LEN, "ttt_sta");
		} else if (cur_if->ifmode == IAP_MODE) {
			cur_if->channel = 1;
			cur_if->maxassoc = -1;
			cur_if->prio = PRIO_AP;
			cur_if->prefix = 'A';
			snprintf(cur_if->ssid, DOT11_MAX_SSID_LEN, "ttt_ap");
#ifdef WLMESH
		} else if (cur_if->ifmode == IMESH_MODE) {
			cur_if->channel = 1;
			cur_if->maxassoc = -1;
			cur_if->prio = PRIO_MESH;
			cur_if->prefix = 'M';
			snprintf(cur_if->ssid, DOT11_MAX_SSID_LEN, "ttt_mesh");
#ifdef WL_ESCAN
			if (i == 0 && apsta_params->macs)
				wl_mesh_escan_attach(dhd, cur_if);
#endif /* WL_ESCAN */
#endif /* WLMESH */
		}
	}

	if (FW_SUPPORTED(dhd, rsdb)) {
		if (apstamode == IDUALAP_MODE)
			apsta_params->rsdb = -1;
		else if (apstamode == ISTAAPAP_MODE)
			apsta_params->rsdb = 0;
		if (apstamode == ISTAAPAP_MODE || apstamode == IDUALAP_MODE ||
				apstamode == IMESHONLY_MODE || apstamode == ISTAMESH_MODE ||
				apstamode == IMESHAP_MODE || apstamode == ISTAAPMESH_MODE ||
				apstamode == IMESHAPAP_MODE) {
			wl_config_t rsdb_mode_cfg = {0, 0};
			rsdb_mode_cfg.config = apsta_params->rsdb;
			AEXT_INFO(dev->name, "set rsdb_mode %d\n", rsdb_mode_cfg.config);
			wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
			wl_ext_iovar_setbuf(dev, "rsdb_mode", &rsdb_mode_cfg,
				sizeof(rsdb_mode_cfg), iovar_buf, sizeof(iovar_buf), NULL);
			wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		}
	} else {
		apsta_params->rsdb = 0;
	}

	if (apstamode == ISTAONLY_MODE) {
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "apsta", 1); // keep 1 as we set in dhd_preinit_ioctls
		// don't set WLC_SET_AP to 0, some parameters will be reset, such as bcn_timeout and roam_off
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
	} else if (apstamode == IAPONLY_MODE) {
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
#ifdef ARP_OFFLOAD_SUPPORT
		/* IF SoftAP is enabled, disable arpoe */
		dhd_arp_offload_set(dhd, 0);
		dhd_arp_offload_enable(dhd, FALSE);
#endif /* ARP_OFFLOAD_SUPPORT */
		wl_ext_iovar_setint(dev, "mpc", 0);
		wl_ext_iovar_setint(dev, "apsta", 0);
		val = 1;
		wl_ext_ioctl(dev, WLC_SET_AP, &val, sizeof(val), 1);
#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO)
		if (!(FW_SUPPORTED(dhd, rsdb)) && !disable_proptx) {
			bool enabled;
			dhd_wlfc_get_enable(dhd, &enabled);
			if (!enabled) {
				dhd_wlfc_init(dhd);
				wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
			}
		}
#endif /* BCMSDIO */
#endif /* PROP_TXSTATUS_VSDB */
	}
	else if (apstamode == ISTAAP_MODE) {
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "mpc", 0);
		wl_ext_iovar_setint(dev, "apsta", 1);
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
	}
	else if (apstamode == ISTAGO_MODE) {
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "apsta", 1);
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
	}
	else if (apstamode == ISTASTA_MODE) {
	}
	else if (apstamode == IDUALAP_MODE) {
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		/* IF SoftAP is enabled, disable arpoe or wlan1 will ping fail */
#ifdef ARP_OFFLOAD_SUPPORT
		/* IF SoftAP is enabled, disable arpoe */
		dhd_arp_offload_set(dhd, 0);
		dhd_arp_offload_enable(dhd, FALSE);
#endif /* ARP_OFFLOAD_SUPPORT */
		wl_ext_iovar_setint(dev, "mpc", 0);
		wl_ext_iovar_setint(dev, "mbcn", 1);
		wl_ext_iovar_setint(dev, "apsta", 0);
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		val = 1;
		wl_ext_ioctl(dev, WLC_SET_AP, &val, sizeof(val), 1);
	}
	else if (apstamode == ISTAAPAP_MODE) {
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "mpc", 0);
		wl_ext_iovar_setint(dev, "mbss", 1);
		wl_ext_iovar_setint(dev, "apsta", 1); // keep 1 as we set in dhd_preinit_ioctls
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		// don't set WLC_SET_AP to 0, some parameters will be reset, such as bcn_timeout and roam_off
	}
#ifdef WLMESH
	else if (apstamode == IMESHONLY_MODE || apstamode == ISTAMESH_MODE ||
			apstamode == IMESHAP_MODE || apstamode == ISTAAPMESH_MODE ||
			apstamode == IMESHAPAP_MODE) {
		int pm = 0;
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "mpc", 0);
		if (apstamode == IMESHONLY_MODE)
			wl_ext_ioctl(dev, WLC_SET_PM, &pm, sizeof(pm), 1);
		else
			wl_ext_iovar_setint(dev, "mbcn", 1);
		wl_ext_iovar_setint(dev, "apsta", 1); // keep 1 as we set in dhd_preinit_ioctls
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		// don't set WLC_SET_AP to 0, some parameters will be reset, such as bcn_timeout and roam_off
	}
#endif /* WLMESH */

	wl_ext_get_ioctl_ver(dev, &apsta_params->ioctl_ver);
	apsta_params->init = TRUE;

	WL_MSG(dev->name, "apstamode=%d\n", apstamode);
}

static int
wl_ext_isam_param(struct net_device *dev, char *command, int total_len)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	int ret = -1;
	char *pick_tmp, *data, *param;
	int bytes_written=-1;

	AEXT_TRACE(dev->name, "command=%s, len=%d\n", command, total_len);

	pick_tmp = command;
	param = bcmstrtok(&pick_tmp, " ", 0); // pick isam_param
	param = bcmstrtok(&pick_tmp, " ", 0); // pick cmd
	while (param != NULL) {
		data = bcmstrtok(&pick_tmp, " ", 0); // pick data
		if (!strcmp(param, "acs")) {
			if (data) {
				apsta_params->acs = simple_strtol(data, NULL, 0);
				ret = 0;
			} else {
				bytes_written = snprintf(command, total_len, "%d", apsta_params->acs);
				ret = bytes_written;
				goto exit;
			}
		}
		param = bcmstrtok(&pick_tmp, " ", 0); // pick cmd
	}

exit:
	return ret;
}

static int
wl_ext_isam_init(struct net_device *dev, char *command, int total_len)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	char *pch, *pick_tmp, *pick_tmp2, *param;
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	int i;

	if (apsta_params->init) {
		AEXT_ERROR(dev->name, "don't init twice\n");
		return -1;
	}
	AEXT_TRACE(dev->name, "command=%s, len=%d\n", command, total_len);

	pick_tmp = command;
	param = bcmstrtok(&pick_tmp, " ", 0); // skip iapsta_init
	param = bcmstrtok(&pick_tmp, " ", 0);
	while (param != NULL) {
		pick_tmp2 = bcmstrtok(&pick_tmp, " ", 0);
		if (!pick_tmp2) {
			AEXT_ERROR(dev->name, "wrong param %s\n", param);
			return -1;
		}
		if (!strcmp(param, "mode")) {
			pch = NULL;
			if (!strcmp(pick_tmp2, "sta")) {
				apsta_params->apstamode = ISTAONLY_MODE;
			} else if (!strcmp(pick_tmp2, "ap")) {
				apsta_params->apstamode = IAPONLY_MODE;
			} else if (!strcmp(pick_tmp2, "sta-ap")) {
				apsta_params->apstamode = ISTAAP_MODE;
			} else if (!strcmp(pick_tmp2, "sta-sta")) {
				apsta_params->apstamode = ISTASTA_MODE;
				apsta_params->vsdb = TRUE;
			} else if (!strcmp(pick_tmp2, "ap-ap")) {
				apsta_params->apstamode = IDUALAP_MODE;
			} else if (!strcmp(pick_tmp2, "sta-ap-ap")) {
				apsta_params->apstamode = ISTAAPAP_MODE;
			} else if (!strcmp(pick_tmp2, "apsta")) {
				apsta_params->apstamode = ISTAAP_MODE;
				apsta_params->if_info[IF_PIF].ifmode = ISTA_MODE;
				apsta_params->if_info[IF_VIF].ifmode = IAP_MODE;
			} else if (!strcmp(pick_tmp2, "dualap")) {
				apsta_params->apstamode = IDUALAP_MODE;
				apsta_params->if_info[IF_PIF].ifmode = IAP_MODE;
				apsta_params->if_info[IF_VIF].ifmode = IAP_MODE;
			} else if (!strcmp(pick_tmp2, "sta-go") ||
					!strcmp(pick_tmp2, "gosta")) {
				if (!FW_SUPPORTED(dhd, p2p)) {
					return -1;
				}
				apsta_params->apstamode = ISTAGO_MODE;
				apsta_params->if_info[IF_PIF].ifmode = ISTA_MODE;
				apsta_params->if_info[IF_VIF].ifmode = IAP_MODE;
#ifdef WLMESH
			} else if (!strcmp(pick_tmp2, "mesh")) {
				apsta_params->apstamode = IMESHONLY_MODE;
			} else if (!strcmp(pick_tmp2, "sta-mesh")) {
				apsta_params->apstamode = ISTAMESH_MODE;
			} else if (!strcmp(pick_tmp2, "sta-ap-mesh")) {
				apsta_params->apstamode = ISTAAPMESH_MODE;
			} else if (!strcmp(pick_tmp2, "mesh-ap")) {
				apsta_params->apstamode = IMESHAP_MODE;
			} else if (!strcmp(pick_tmp2, "mesh-ap-ap")) {
				apsta_params->apstamode = IMESHAPAP_MODE;
#endif /* WLMESH */
			} else {
				AEXT_ERROR(dev->name, "mode [sta|ap|sta-ap|ap-ap]\n");
				return -1;
			}
			pch = bcmstrtok(&pick_tmp2, " -", 0);
			for (i=0; i<MAX_IF_NUM && pch; i++) {
				if (!strcmp(pch, "sta"))
					apsta_params->if_info[i].ifmode = ISTA_MODE;
				else if (!strcmp(pch, "ap"))
					apsta_params->if_info[i].ifmode = IAP_MODE;
#ifdef WLMESH
				else if (!strcmp(pch, "mesh")) {
					if (dhd->conf->fw_type != FW_TYPE_MESH) {
						AEXT_ERROR(dev->name, "wrong fw type\n");
						return -1;
					}
					apsta_params->if_info[i].ifmode = IMESH_MODE;
				}
#endif /* WLMESH */
				pch = bcmstrtok(&pick_tmp2, " -", 0);
			}
		}
		else if (!strcmp(param, "rsdb")) {
			apsta_params->rsdb = (int)simple_strtol(pick_tmp2, NULL, 0);
		} else if (!strcmp(param, "vsdb")) {
			if (!strcmp(pick_tmp2, "y")) {
				apsta_params->vsdb = TRUE;
			} else if (!strcmp(pick_tmp2, "n")) {
				apsta_params->vsdb = FALSE;
			} else {
				AEXT_ERROR(dev->name, "vsdb [y|n]\n");
				return -1;
			}
		} else if (!strcmp(param, "csa")) {
			apsta_params->csa = (int)simple_strtol(pick_tmp2, NULL, 0);
		} else if (!strcmp(param, "acs")) {
			apsta_params->acs = (int)simple_strtol(pick_tmp2, NULL, 0);
#if defined(WLMESH) && defined(WL_ESCAN)
		} else if (!strcmp(param, "macs")) {
			apsta_params->macs = (int)simple_strtol(pick_tmp2, NULL, 0);
#endif /* WLMESH && WL_ESCAN */
		} else if (!strcmp(param, "ifname")) {
			pch = NULL;
			pch = bcmstrtok(&pick_tmp2, " -", 0);
			for (i=0; i<MAX_IF_NUM && pch; i++) {
				strcpy(apsta_params->if_info[i].ifname, pch);
				pch = bcmstrtok(&pick_tmp2, " -", 0);
			}
		} else if (!strcmp(param, "vifname")) {
			strcpy(apsta_params->if_info[IF_VIF].ifname, pick_tmp2);
		}
		param = bcmstrtok(&pick_tmp, " ", 0);
	}

	if (apsta_params->apstamode == 0) {
		AEXT_ERROR(dev->name, "mode [sta|ap|sta-ap|ap-ap]\n");
		return -1;
	}

	wl_ext_iapsta_preinit(dev, apsta_params);
#ifndef WL_STATIC_IF
	wl_ext_iapsta_intf_add(dev, apsta_params);
#endif /* WL_STATIC_IF */

	return 0;
}

static int
wl_ext_parse_config(struct wl_if_info *cur_if, char *command, char **pick_next)
{
	char *pch, *pick_tmp;
	char name[20], data[100];
	int i, j, len;
	char *ifname_head = NULL;

	typedef struct config_map_t {
		char name[20];
		char *head;
		char *tail;
	} config_map_t;

	config_map_t config_map [] = {
		{" ifname ",	NULL, NULL},
		{" ssid ",		NULL, NULL},
		{" bssid ", 	NULL, NULL},
		{" bgnmode ",	NULL, NULL},
		{" hidden ",	NULL, NULL},
		{" maxassoc ",	NULL, NULL},
		{" chan ",		NULL, NULL},
		{" amode ", 	NULL, NULL},
		{" emode ", 	NULL, NULL},
		{" key ",		NULL, NULL},
	};
	config_map_t *row, *row_prev;

	pick_tmp = command;

	// reset head and tail
	for (i = 0; i < sizeof(config_map)/sizeof(config_map[0]); i++) {
		row = &config_map[i];
		row->head = NULL;
		row->tail = pick_tmp + strlen(pick_tmp);
	}

	// pick head
	for (i = 0; i < sizeof(config_map)/sizeof(config_map[0]); i++) {
		row = &config_map[i];
		pch = strstr(pick_tmp, row->name);
		if (pch) {
			row->head = pch;
		}
	}

	// sort by head
	for (i = 0; i < sizeof(config_map)/sizeof(config_map[0]) - 1; i++) {
		row_prev = &config_map[i];
		for (j = i+1; j < sizeof(config_map)/sizeof(config_map[0]); j++) {
			row = &config_map[j];
			if (row->head < row_prev->head) {
				strcpy(name, row_prev->name);
				strcpy(row_prev->name, row->name);
				strcpy(row->name, name);
				pch = row_prev->head;
				row_prev->head = row->head;
				row->head = pch;
			}
		}
	}

	// pick tail
	for (i = 0; i < sizeof(config_map)/sizeof(config_map[0]) - 1; i++) {
		row_prev = &config_map[i];
		row = &config_map[i+1];
		if (row_prev->head) {
			row_prev->tail = row->head;
		}
	}

	// remove name from head
	for (i = 0; i < sizeof(config_map)/sizeof(config_map[0]); i++) {
		row = &config_map[i];
		if (row->head) {
			if (!strcmp(row->name, " ifname ")) {
				ifname_head = row->head + 1;
				break;
			}
			row->head += strlen(row->name);
		}
	}

	for (i = 0; i < sizeof(config_map)/sizeof(config_map[0]); i++) {
		row = &config_map[i];
		if (row->head) {
			memset(data, 0, sizeof(data));
			if (row->tail && row->tail > row->head) {
				strncpy(data, row->head, row->tail-row->head);
			} else {
				strcpy(data, row->head);
			}
			pick_tmp = data;

			if (!strcmp(row->name, " ifname ")) {
				break;
			} else if (!strcmp(row->name, " ssid ")) {
				len = strlen(pick_tmp);
				memset(cur_if->ssid, 0, sizeof(cur_if->ssid));
				if (pick_tmp[0] == '"' && pick_tmp[len-1] == '"')
					strncpy(cur_if->ssid, &pick_tmp[1], len-2);
				else
					strcpy(cur_if->ssid, pick_tmp);
			} else if (!strcmp(row->name, " bssid ")) {
				pch = bcmstrtok(&pick_tmp, ": ", 0);
				for (j=0; j<6 && pch; j++) {
					((u8 *)&cur_if->bssid)[j] = (int)simple_strtol(pch, NULL, 16);
					pch = bcmstrtok(&pick_tmp, ": ", 0);
				}
			} else if (!strcmp(row->name, " bgnmode ")) {
				if (!strcmp(pick_tmp, "b"))
					cur_if->bgnmode = IEEE80211B;
				else if (!strcmp(pick_tmp, "g"))
					cur_if->bgnmode = IEEE80211G;
				else if (!strcmp(pick_tmp, "bg"))
					cur_if->bgnmode = IEEE80211BG;
				else if (!strcmp(pick_tmp, "bgn"))
					cur_if->bgnmode = IEEE80211BGN;
				else if (!strcmp(pick_tmp, "bgnac"))
					cur_if->bgnmode = IEEE80211BGNAC;
				else {
					AEXT_ERROR(cur_if->dev->name, "bgnmode [b|g|bg|bgn|bgnac]\n");
					return -1;
				}
			} else if (!strcmp(row->name, " hidden ")) {
				if (!strcmp(pick_tmp, "n"))
					cur_if->hidden = 0;
				else if (!strcmp(pick_tmp, "y"))
					cur_if->hidden = 1;
				else {
					AEXT_ERROR(cur_if->dev->name, "hidden [y|n]\n");
					return -1;
				}
			} else if (!strcmp(row->name, " maxassoc ")) {
				cur_if->maxassoc = (int)simple_strtol(pick_tmp, NULL, 10);
			} else if (!strcmp(row->name, " chan ")) {
				cur_if->channel = (int)simple_strtol(pick_tmp, NULL, 10);
			} else if (!strcmp(row->name, " amode ")) {
				if (!strcmp(pick_tmp, "open"))
					cur_if->amode = AUTH_OPEN;
				else if (!strcmp(pick_tmp, "shared"))
					cur_if->amode = AUTH_SHARED;
				else if (!strcmp(pick_tmp, "wpapsk"))
					cur_if->amode = AUTH_WPAPSK;
				else if (!strcmp(pick_tmp, "wpa2psk"))
					cur_if->amode = AUTH_WPA2PSK;
				else if (!strcmp(pick_tmp, "wpawpa2psk"))
					cur_if->amode = AUTH_WPAWPA2PSK;
				else if (!strcmp(pick_tmp, "sae"))
					cur_if->amode = AUTH_SAE;
				else {
					AEXT_ERROR(cur_if->dev->name, "amode [open|shared|wpapsk|wpa2psk|wpawpa2psk]\n");
					return -1;
				}
			} else if (!strcmp(row->name, " emode ")) {
				if (!strcmp(pick_tmp, "none"))
					cur_if->emode = ENC_NONE;
				else if (!strcmp(pick_tmp, "wep"))
					cur_if->emode = ENC_WEP;
				else if (!strcmp(pick_tmp, "tkip"))
					cur_if->emode = ENC_TKIP;
				else if (!strcmp(pick_tmp, "aes"))
					cur_if->emode = ENC_AES;
				else if (!strcmp(pick_tmp, "tkipaes"))
					cur_if->emode = ENC_TKIPAES;
				else {
					AEXT_ERROR(cur_if->dev->name, "emode [none|wep|tkip|aes|tkipaes]\n");
					return -1;
				}
			} else if (!strcmp(row->name, " key ")) {
				len = strlen(pick_tmp);
				memset(cur_if->key, 0, sizeof(cur_if->key));
				if (pick_tmp[0] == '"' && pick_tmp[len-1] == '"')
					strncpy(cur_if->key, &pick_tmp[1], len-2);
				else
					strcpy(cur_if->key, pick_tmp);
			}
		}
	}

	*pick_next = ifname_head;
	return 0;
}

static int
wl_ext_iapsta_config(struct net_device *dev, char *command, int total_len)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	int ret=0, i;
	char *pch, *pch2, *pick_tmp, *pick_next=NULL, *param;
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	char ifname[IFNAMSIZ+1];
	struct wl_if_info *cur_if = NULL, *tmp_if = NULL;

	if (!apsta_params->init) {
		AEXT_ERROR(dev->name, "please init first\n");
		return -1;
	}

	AEXT_TRACE(dev->name, "command=%s, len=%d\n", command, total_len);

	pick_tmp = command;
	param = bcmstrtok(&pick_tmp, " ", 0); // skip iapsta_config

	mutex_lock(&apsta_params->usr_sync);

	while (pick_tmp != NULL) {
		memset(ifname, 0, IFNAMSIZ+1);
		if (!strncmp(pick_tmp, "ifname ", strlen("ifname "))) {
			pch = pick_tmp + strlen("ifname ");
			pch2 = strchr(pch, ' ');
			if (pch && pch2) {
				strncpy(ifname, pch, pch2-pch);
			} else {
				AEXT_ERROR(dev->name, "ifname [wlanX]\n");
				ret = -1;
				break;
			}
			for (i=0; i<MAX_IF_NUM; i++) {
				tmp_if = &apsta_params->if_info[i];
				if (tmp_if->dev && !strcmp(tmp_if->dev->name, ifname)) {
					cur_if = tmp_if;
					break;
				}
			}
			if (!cur_if) {
				AEXT_ERROR(dev->name, "wrong ifname=%s in apstamode=%d\n",
					ifname, apsta_params->apstamode);
				ret = -1;
				break;
			}
			ret = wl_ext_parse_config(cur_if, pick_tmp, &pick_next);
			if (ret)
				break;
			pick_tmp = pick_next;
		} else {
			AEXT_ERROR(dev->name, "first arg must be ifname\n");
			ret = -1;
			break;
		}

	}

	mutex_unlock(&apsta_params->usr_sync);

	return ret;
}

static int
wl_ext_assoclist(struct net_device *dev, char *data, char *command,
	int total_len)
{
	int ret = 0, i, maxassoc = 0, bytes_written = 0;
	char mac_buf[MAX_NUM_OF_ASSOCLIST *
		sizeof(struct ether_addr) + sizeof(uint)] = {0};
	struct maclist *assoc_maclist = (struct maclist *)mac_buf;

	assoc_maclist->count = htod32(MAX_NUM_OF_ASSOCLIST);
	ret = wl_ext_ioctl(dev, WLC_GET_ASSOCLIST, assoc_maclist, sizeof(mac_buf), 0);
	if (ret)
		return -1;
	maxassoc = dtoh32(assoc_maclist->count);
	bytes_written += snprintf(command+bytes_written, total_len,
		"%2s: %12s",
		"no", "------addr------");
	for (i=0; i<maxassoc; i++) {
		bytes_written += snprintf(command+bytes_written, total_len,
			"\n%2d: %pM", i, &assoc_maclist->ea[i]);
	}

	return bytes_written;
}

#ifdef WLMESH
static int
wl_mesh_print_peer_info(mesh_peer_info_ext_t *mpi_ext,
	uint32 peer_results_count, char *command, int total_len)
{
	char *peering_map[] = MESH_PEERING_STATE_STRINGS;
	uint32 count = 0;
	int bytes_written = 0;

	bytes_written += snprintf(command+bytes_written, total_len,
		"%2s: %12s : %6s : %-6s : %6s :"
		" %5s : %4s : %4s : %11s : %4s",
		"no", "------addr------ ", "l.aid", "state", "p.aid",
		"mppid", "llid", "plid", "entry_state", "rssi");
	for (count=0; count < peer_results_count; count++) {
		if (mpi_ext->entry_state != MESH_SELF_PEER_ENTRY_STATE_TIMEDOUT) {
			bytes_written += snprintf(command+bytes_written, total_len,
				"\n%2d: %pM : 0x%4x : %6s : 0x%4x :"
				" %5d : %4d : %4d : %11s : %4d",
				count, &mpi_ext->ea, mpi_ext->local_aid,
				peering_map[mpi_ext->peer_info.state],
				mpi_ext->peer_info.peer_aid,
				mpi_ext->peer_info.mesh_peer_prot_id,
				mpi_ext->peer_info.local_link_id,
				mpi_ext->peer_info.peer_link_id,
				(mpi_ext->entry_state == MESH_SELF_PEER_ENTRY_STATE_ACTIVE) ?
				"ACTIVE" :
				"EXTERNAL",
				mpi_ext->rssi);
		} else {
			bytes_written += snprintf(command+bytes_written, total_len,
				"\n%2d: %pM : %6s : %5s : %6s :"
				" %5s : %4s : %4s : %11s : %4s",
				count, &mpi_ext->ea, "  NA  ", "  NA  ", "  NA  ",
				"  NA ", " NA ", " NA ", "  TIMEDOUT ", " NA ");
		}
		mpi_ext++;
	}

	return bytes_written;
}

static int
wl_mesh_get_peer_results(struct net_device *dev, char *buf, int len)
{
	int indata, inlen;
	mesh_peer_info_dump_t *peer_results;
	int ret;

	memset(buf, 0, len);
	peer_results = (mesh_peer_info_dump_t *)buf;
	indata = htod32(len);
	inlen = 4;
	ret = wl_ext_iovar_getbuf(dev, "mesh_peer_status", &indata, inlen, buf, len, NULL);
	if (!ret) {
		peer_results = (mesh_peer_info_dump_t *)buf;
		ret = peer_results->count;
	}

	return ret;
}

static int
wl_ext_mesh_peer_status(struct net_device *dev, char *data, char *command,
	int total_len)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	int i;
	struct wl_if_info *cur_if;
	mesh_peer_info_dump_t *peer_results;
	mesh_peer_info_ext_t *mpi_ext;
	char *peer_buf = NULL;
	int peer_len = WLC_IOCTL_MAXLEN;
	int dump_written = 0, ret;

	if (!data) {
		peer_buf = kmalloc(peer_len, GFP_KERNEL);
		if (peer_buf == NULL) {
			AEXT_ERROR(dev->name, "Failed to allocate buffer of %d bytes\n",
				peer_len); 
			return -1;
		}
		for (i=0; i<MAX_IF_NUM; i++) {
			cur_if = &apsta_params->if_info[i];
			if (cur_if && dev == cur_if->dev && cur_if->ifmode == IMESH_MODE) {
				memset(peer_buf, 0, peer_len);
				ret = wl_mesh_get_peer_results(dev, peer_buf, peer_len);
				if (ret >= 0) {
					peer_results = (mesh_peer_info_dump_t *)peer_buf;
					mpi_ext = (mesh_peer_info_ext_t *)peer_results->mpi_ext;
					dump_written += wl_mesh_print_peer_info(mpi_ext,
						peer_results->count, command+dump_written,
						total_len-dump_written);
				}
			} else if (cur_if && dev == cur_if->dev) {
				AEXT_ERROR(dev->name, "[%s][%c] is not mesh interface\n",
					cur_if->ifname, cur_if->prefix);
			}
		}
	}

	if (peer_buf)
		kfree(peer_buf);
	return dump_written;
}

#ifdef WL_ESCAN
#define WL_MESH_DELAY_SCAN_MS	3000
static void
wl_mesh_timer(unsigned long data)
{
	wl_event_msg_t msg;
	struct wl_if_info *mesh_if = (struct wl_if_info *)data;
	struct dhd_pub *dhd;

	if (!mesh_if) {
		AEXT_ERROR("wlan", "mesh_if is not ready\n");
		return;
	}

	if (!mesh_if->dev) {
		AEXT_ERROR("wlan", "ifidx %d is not ready\n", mesh_if->ifidx);
		return;
	}
	dhd = dhd_get_pub(mesh_if->dev);

	bzero(&msg, sizeof(wl_event_msg_t));
	AEXT_TRACE(mesh_if->dev->name, "timer expired\n");

	msg.ifidx = mesh_if->ifidx;
	msg.event_type = hton32(WLC_E_RESERVED);
	msg.reason = 0xFFFFFFFF;
	wl_ext_event_send(dhd->event_params, &msg, NULL);
}

static void
wl_mesh_set_timer(struct wl_if_info *mesh_if, uint timeout)
{
	AEXT_TRACE(mesh_if->dev->name, "timeout=%d\n", timeout);

	if (timer_pending(&mesh_if->delay_scan))
		del_timer_sync(&mesh_if->delay_scan);

	if (timeout) {
		if (timer_pending(&mesh_if->delay_scan))
			del_timer_sync(&mesh_if->delay_scan);
		mod_timer(&mesh_if->delay_scan, jiffies + msecs_to_jiffies(timeout));
	}
}

static struct wl_if_info *
wl_ext_if_enabled(struct wl_apsta_params *apsta_params, ifmode_t ifmode)
{
	struct wl_if_info *tmp_if, *target_if = NULL;
	int i;

	for (i=0; i<MAX_IF_NUM; i++) {
		tmp_if = &apsta_params->if_info[i];
		if (tmp_if && tmp_if->ifmode == ifmode &&
				wl_get_isam_status(tmp_if, IF_READY)) {
			if (wl_ext_get_chan(apsta_params, tmp_if->dev)) {
				target_if = tmp_if;
				break;
			}
		}
	}

	return target_if;
}

static int
wl_mesh_clear_vndr_ie(struct net_device *dev, uchar *oui)
{
	char *vndr_ie_buf = NULL;
	vndr_ie_setbuf_t *vndr_ie = NULL;
	ie_getbuf_t vndr_ie_tmp;
	char *iovar_buf = NULL;
	int err = -1, i;
	vndr_ie_buf_t *vndr_ie_dump = NULL;
	uchar *iebuf;
	vndr_ie_info_t *ie_info;
	vndr_ie_t *ie;

	vndr_ie_buf = kzalloc(WLC_IOCTL_SMLEN, GFP_KERNEL);
	if (!vndr_ie_buf) {
		AEXT_ERROR(dev->name, "IE memory alloc failed\n");
		err = -ENOMEM;
		goto exit;
	}

	iovar_buf = kzalloc(WLC_IOCTL_MEDLEN, GFP_KERNEL);
	if (!iovar_buf) {
		AEXT_ERROR(dev->name, "iovar_buf alloc failed\n");
		err = -ENOMEM;
		goto exit;
	}

	memset(iovar_buf, 0, WLC_IOCTL_MEDLEN);
	vndr_ie_tmp.pktflag = (uint32) -1;
	vndr_ie_tmp.id = (uint8) DOT11_MNG_PROPR_ID;
	err = wl_ext_iovar_getbuf(dev, "vndr_ie", &vndr_ie_tmp, sizeof(vndr_ie_tmp),
		iovar_buf, WLC_IOCTL_MEDLEN, NULL);
	if (err)
		goto exit;

	vndr_ie_dump = (vndr_ie_buf_t *)iovar_buf;
	if (!vndr_ie_dump->iecount)
		goto exit;

	iebuf = (uchar *)&vndr_ie_dump->vndr_ie_list[0];
	for (i=0; i<vndr_ie_dump->iecount; i++) {
		ie_info = (vndr_ie_info_t *) iebuf;
		ie = &ie_info->vndr_ie_data;
		if (memcmp(ie->oui, oui, 3))
			memset(ie->oui, 0, 3);
		iebuf += sizeof(uint32) + ie->len + VNDR_IE_HDR_LEN;
	}

	vndr_ie = (vndr_ie_setbuf_t *) vndr_ie_buf;
	strncpy(vndr_ie->cmd, "del", VNDR_IE_CMD_LEN - 1);
	vndr_ie->cmd[VNDR_IE_CMD_LEN - 1] = '\0';
	memcpy(&vndr_ie->vndr_ie_buffer, vndr_ie_dump, WLC_IOCTL_SMLEN-VNDR_IE_CMD_LEN-1);

	memset(iovar_buf, 0, WLC_IOCTL_MEDLEN);
	err = wl_ext_iovar_setbuf(dev, "vndr_ie", vndr_ie, WLC_IOCTL_SMLEN, iovar_buf,
		WLC_IOCTL_MEDLEN, NULL);

exit:
	if (vndr_ie) {
		kfree(vndr_ie);
	}
	if (iovar_buf) {
		kfree(iovar_buf);
	}
	return err;
}

static int
wl_mesh_clear_mesh_info(struct wl_apsta_params *apsta_params,
	struct wl_if_info *mesh_if, bool scan)
{
	struct wl_mesh_params *mesh_info = &apsta_params->mesh_info;
	uchar mesh_oui[]={0x00, 0x22, 0xf4};
	int ret;

	AEXT_TRACE(mesh_if->dev->name, "Enter\n");

	ret = wl_mesh_clear_vndr_ie(mesh_if->dev, mesh_oui);
	memset(mesh_info, 0, sizeof(struct wl_mesh_params));
	if (scan) {
		mesh_info->scan_channel = wl_ext_get_chan(apsta_params, mesh_if->dev);
		wl_mesh_set_timer(mesh_if, 100);
	}

	return ret;
}

static int
wl_mesh_update_vndr_ie(struct wl_apsta_params *apsta_params,
	struct wl_if_info *mesh_if)
{
	struct wl_mesh_params *mesh_info = &apsta_params->mesh_info;
	char vndr_ie[64];
	uchar mesh_oui[]={0x00, 0x22, 0xf4};
	int bytes_written = 0;
	int ret;

	wl_mesh_clear_vndr_ie(mesh_if->dev, mesh_oui);

	bytes_written += snprintf(vndr_ie+bytes_written, sizeof(vndr_ie),
		"0x%02x%02x%02x", mesh_oui[0], mesh_oui[1], mesh_oui[2]);

	bytes_written += snprintf(vndr_ie+bytes_written, sizeof(vndr_ie),
		"%02d%02d%02x%02x%02x%02x%02x%02x", MESH_INFO_MASTER_BSSID, ETHER_ADDR_LEN,
		((u8 *)(&mesh_info->master_bssid))[0], ((u8 *)(&mesh_info->master_bssid))[1],
		((u8 *)(&mesh_info->master_bssid))[2], ((u8 *)(&mesh_info->master_bssid))[3],
		((u8 *)(&mesh_info->master_bssid))[4], ((u8 *)(&mesh_info->master_bssid))[5]);

	bytes_written += snprintf(vndr_ie+bytes_written, sizeof(vndr_ie),
		"%02x%02x%02x", MESH_INFO_MASTER_CHANNEL, 1, mesh_info->master_channel);

	bytes_written += snprintf(vndr_ie+bytes_written, sizeof(vndr_ie),
		"%02x%02x%02x", MESH_INFO_HOP_CNT, 1, mesh_info->hop_cnt);

	bytes_written += snprintf(vndr_ie+bytes_written, sizeof(vndr_ie),
		"%02d%02d%02x%02x%02x%02x%02x%02x", MESH_INFO_PEER_BSSID, ETHER_ADDR_LEN,
		((u8 *)(&mesh_info->peer_bssid))[0], ((u8 *)(&mesh_info->peer_bssid))[1],
		((u8 *)(&mesh_info->peer_bssid))[2], ((u8 *)(&mesh_info->peer_bssid))[3],
		((u8 *)(&mesh_info->peer_bssid))[4], ((u8 *)(&mesh_info->peer_bssid))[5]);

	ret = wl_ext_add_del_ie(mesh_if->dev, VNDR_IE_BEACON_FLAG|VNDR_IE_PRBRSP_FLAG,
		vndr_ie, "add");
	if (!ret) {
		AEXT_INFO(mesh_if->dev->name, "mbssid=%pM, mchannel=%d, hop=%d, pbssid=%pM\n",
			&mesh_info->master_bssid, mesh_info->master_channel, mesh_info->hop_cnt,
			&mesh_info->peer_bssid); 
	}

	return ret;
}

static bool
wl_mesh_update_master_info(struct wl_apsta_params *apsta_params,
	struct wl_if_info *mesh_if)
{
	struct wl_mesh_params *mesh_info = &apsta_params->mesh_info;
	struct wl_if_info *sta_if = NULL;
	bool updated = FALSE;

	sta_if = wl_ext_if_enabled(apsta_params, ISTA_MODE);
	if (sta_if) {
		wldev_ioctl(mesh_if->dev, WLC_GET_BSSID, &mesh_info->master_bssid,
			ETHER_ADDR_LEN, 0);
		mesh_info->master_channel = wl_ext_get_chan(apsta_params, mesh_if->dev);
		mesh_info->hop_cnt = 1;
		memcpy(&mesh_info->peer_bssid, &mesh_info->master_bssid, ETHER_ADDR_LEN);
		wl_mesh_update_vndr_ie(apsta_params, mesh_if);
		updated = TRUE;
	}

	return updated;
}

static uint
wl_mesh_update_mesh_info(struct wl_apsta_params *apsta_params,
	struct wl_if_info *mesh_if)
{
	struct wl_mesh_params *mesh_info = &apsta_params->mesh_info, peer_mesh_info;
	uint32 count = 0;
	char *dump_buf = NULL;
	mesh_peer_info_dump_t *peer_results;
	mesh_peer_info_ext_t *mpi_ext;
	struct ether_addr bssid;
	bool updated = FALSE;
	uint16 cur_chan;

	dump_buf = kmalloc(WLC_IOCTL_MAXLEN, GFP_KERNEL);
	if (dump_buf == NULL) {
		AEXT_ERROR(mesh_if->dev->name, "Failed to allocate buffer of %d bytes\n",
			WLC_IOCTL_MAXLEN); 
		return FALSE;
	}
	count = wl_mesh_get_peer_results(mesh_if->dev, dump_buf, WLC_IOCTL_MAXLEN);
	if (count > 0) {
		memset(&bssid, 0 , ETHER_ADDR_LEN);
		wldev_ioctl(mesh_if->dev, WLC_GET_BSSID, &bssid, ETHER_ADDR_LEN, 0);
		peer_results = (mesh_peer_info_dump_t *)dump_buf;
		mpi_ext = (mesh_peer_info_ext_t *)peer_results->mpi_ext;
		for (count = 0; count < peer_results->count; count++) {
			if (mpi_ext->entry_state != MESH_SELF_PEER_ENTRY_STATE_TIMEDOUT &&
					mpi_ext->peer_info.state == MESH_PEERING_ESTAB) {
				memset(&peer_mesh_info, 0 , sizeof(struct wl_mesh_params));
				wl_escan_mesh_info(mesh_if->dev, mesh_if->escan,
					&mpi_ext->ea, &peer_mesh_info);
				if ((memcmp(&peer_mesh_info.peer_bssid, &bssid, ETHER_ADDR_LEN)) &&
						peer_mesh_info.hop_cnt && (mesh_info->hop_cnt == 0 ||
						peer_mesh_info.hop_cnt <= mesh_info->hop_cnt)) {
					memcpy(&mesh_info->master_bssid, &peer_mesh_info.master_bssid,
						ETHER_ADDR_LEN);
					mesh_info->master_channel = peer_mesh_info.master_channel;
					mesh_info->hop_cnt = peer_mesh_info.hop_cnt+1;
					memcpy(&mesh_info->peer_bssid, &mpi_ext->ea, ETHER_ADDR_LEN);
					mesh_info->channel = peer_mesh_info.channel;
					updated = TRUE;
				}
			}
			mpi_ext++;
		}
		if (updated)
			wl_mesh_update_vndr_ie(apsta_params, mesh_if);
	}

	if (!mesh_info->hop_cnt) {
		wlc_ssid_t cur_ssid;
		char sec[32];
		bool sae = FALSE;
		memset(&peer_mesh_info, 0, sizeof(struct wl_mesh_params));
		wl_ext_ioctl(mesh_if->dev, WLC_GET_SSID, &cur_ssid, sizeof(cur_ssid), 0);
		wl_ext_get_sec(mesh_if->dev, mesh_if->ifmode, sec, sizeof(sec));
		if (strnicmp(sec, "sae/sae", strlen("sae/sae")) == 0)
			sae = TRUE;
		cur_chan = wl_ext_get_chan(apsta_params, mesh_if->dev);
		wl_escan_mesh_peer(mesh_if->dev, mesh_if->escan, &cur_ssid, cur_chan,
			sae, &peer_mesh_info);

		if (peer_mesh_info.hop_cnt && peer_mesh_info.channel &&
				(cur_chan != peer_mesh_info.channel)) {
			WL_MSG(mesh_if->ifname, "moving channel %d -> %d\n",
				cur_chan, peer_mesh_info.channel);
			wl_ext_disable_iface(mesh_if->dev, mesh_if->ifname);
			mesh_if->channel = peer_mesh_info.channel;
			wl_ext_enable_iface(mesh_if->dev, mesh_if->ifname, 500);
		}
	}

	if (dump_buf)
		kfree(dump_buf);
	return mesh_info->hop_cnt;
}

static void
wl_mesh_event_handler(	struct wl_apsta_params *apsta_params,
	struct wl_if_info *mesh_if, const wl_event_msg_t *e, void *data)
{
	struct wl_mesh_params *mesh_info = &apsta_params->mesh_info;
	uint32 event_type = ntoh32(e->event_type);
	uint32 status = ntoh32(e->status);
	uint32 reason = ntoh32(e->reason);
	wlc_ssid_t ssid;
	int ret;

	if (wl_get_isam_status(mesh_if, AP_CREATED) &&
			((event_type == WLC_E_SET_SSID && status == WLC_E_STATUS_SUCCESS) ||
			(event_type == WLC_E_LINK && status == WLC_E_STATUS_SUCCESS &&
			reason == WLC_E_REASON_INITIAL_ASSOC))) {
		if (!wl_mesh_update_master_info(apsta_params, mesh_if)) {
			mesh_info->scan_channel = wl_ext_get_chan(apsta_params, mesh_if->dev);
			wl_mesh_set_timer(mesh_if, WL_MESH_DELAY_SCAN_MS);
		}
	}
	else if ((event_type == WLC_E_LINK && reason == WLC_E_LINK_BSSCFG_DIS) ||
			(event_type == WLC_E_LINK && status == WLC_E_STATUS_SUCCESS &&
			reason == WLC_E_REASON_DEAUTH)) {
		wl_mesh_clear_mesh_info(apsta_params, mesh_if, FALSE);
	}
	else if (wl_get_isam_status(mesh_if, AP_CREATED) &&
			(event_type == WLC_E_ASSOC_IND || event_type == WLC_E_REASSOC_IND) &&
			reason == DOT11_SC_SUCCESS) {
		mesh_info->scan_channel = wl_ext_get_chan(apsta_params, mesh_if->dev);
		wl_mesh_set_timer(mesh_if, 100);
	}
	else if (event_type == WLC_E_DISASSOC_IND || event_type == WLC_E_DEAUTH_IND ||
			(event_type == WLC_E_DEAUTH && reason != DOT11_RC_RESERVED)) {
		if (!memcmp(&mesh_info->peer_bssid, &e->addr, ETHER_ADDR_LEN))
			wl_mesh_clear_mesh_info(apsta_params, mesh_if, TRUE);
	}
	else if (wl_get_isam_status(mesh_if, AP_CREATED) &&
			event_type == WLC_E_RESERVED && reason == 0xFFFFFFFF) {
		if (!wl_mesh_update_master_info(apsta_params, mesh_if)) {
			wl_ext_ioctl(mesh_if->dev, WLC_GET_SSID, &ssid, sizeof(ssid), 0);
			ret = wl_escan_set_scan(mesh_if->dev, apsta_params->dhd, &ssid,
				mesh_info->scan_channel, FALSE);
			if (ret)
				wl_mesh_set_timer(mesh_if, WL_MESH_DELAY_SCAN_MS);
		}
	}
	else if (wl_get_isam_status(mesh_if, AP_CREATED) &&
			((event_type == WLC_E_ESCAN_RESULT && status == WLC_E_STATUS_SUCCESS) ||
			(event_type == WLC_E_ESCAN_RESULT &&
			(status == WLC_E_STATUS_ABORT || status == WLC_E_STATUS_NEWSCAN ||
			status == WLC_E_STATUS_11HQUIET || status == WLC_E_STATUS_CS_ABORT ||
			status == WLC_E_STATUS_NEWASSOC || status == WLC_E_STATUS_TIMEOUT)))) {
		if (!wl_mesh_update_master_info(apsta_params, mesh_if)) {
			if (!wl_mesh_update_mesh_info(apsta_params, mesh_if)) {
				mesh_info->scan_channel = 0;
				wl_mesh_set_timer(mesh_if, WL_MESH_DELAY_SCAN_MS);
			}
		}
	}
}

static void
wl_mesh_escan_detach(dhd_pub_t *dhd, struct wl_if_info *mesh_if)
{
	AEXT_TRACE(mesh_if->dev->name, "Enter\n");

	del_timer_sync(&mesh_if->delay_scan);

	if (mesh_if->escan) {
		mesh_if->escan = NULL;
	}
}

static int
wl_mesh_escan_attach(dhd_pub_t *dhd, struct wl_if_info *mesh_if)
{
	AEXT_TRACE(mesh_if->dev->name, "Enter\n");

	mesh_if->escan = dhd->escan;
	init_timer_compat(&mesh_if->delay_scan, wl_mesh_timer, mesh_if);

	return 0;
}
#endif /* WL_ESCAN */
#endif /* WLMESH */

static int
wl_ext_isam_status(struct net_device *dev, char *command, int total_len)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	int i;
	struct wl_if_info *tmp_if;
	uint16 chan = 0;
	wlc_ssid_t ssid = { 0, {0} };
	struct ether_addr bssid;
	scb_val_t scb_val;
	char sec[32];
	u32 chanspec = 0;
	char *dump_buf = NULL;
	int dump_len = WLC_IOCTL_MEDLEN;
	int dump_written = 0;

	if (command || android_msg_level & ANDROID_INFO_LEVEL) {
		if (command) {
			dump_buf = command;
			dump_len = total_len;
		} else {
			dump_buf = kmalloc(dump_len, GFP_KERNEL);
			if (dump_buf == NULL) {
				AEXT_ERROR(dev->name, "Failed to allocate buffer of %d bytes\n",
					dump_len); 
				return -1;
			}
		}
		dump_written += snprintf(dump_buf+dump_written, dump_len,
			"apstamode=%d", apsta_params->apstamode);
		for (i=0; i<MAX_IF_NUM; i++) {
			memset(&ssid, 0, sizeof(ssid));
			memset(&bssid, 0, sizeof(bssid));
			memset(&scb_val, 0, sizeof(scb_val));
			tmp_if = &apsta_params->if_info[i];
			if (tmp_if->dev) {
				chan = wl_ext_get_chan(apsta_params, tmp_if->dev);
				if (chan) {
					wl_ext_ioctl(tmp_if->dev, WLC_GET_SSID, &ssid, sizeof(ssid), 0);
					wldev_ioctl(tmp_if->dev, WLC_GET_BSSID, &bssid, sizeof(bssid), 0);
					wldev_ioctl(tmp_if->dev, WLC_GET_RSSI, &scb_val,
						sizeof(scb_val_t), 0);
					chanspec = wl_ext_get_chanspec(apsta_params, tmp_if->dev);
					wl_ext_get_sec(tmp_if->dev, tmp_if->ifmode, sec, sizeof(sec));
					dump_written += snprintf(dump_buf+dump_written, dump_len,
						"\n[dhd-%s-%c]: bssid=%pM, chan=%3d(0x%x %sMHz), "
						"rssi=%3d, sec=%-15s, SSID=\"%s\"",
						tmp_if->ifname, tmp_if->prefix, &bssid, chan, chanspec,
						CHSPEC_IS20(chanspec)?"20":
						CHSPEC_IS40(chanspec)?"40":
						CHSPEC_IS80(chanspec)?"80":"160",
						dtoh32(scb_val.val), sec, ssid.SSID);
					if (tmp_if->ifmode == IAP_MODE) {
						dump_written += snprintf(dump_buf+dump_written, dump_len, "\n");
						dump_written += wl_ext_assoclist(tmp_if->dev, NULL,
							dump_buf+dump_written, dump_len-dump_written);
					}
#ifdef WLMESH
					else if (tmp_if->ifmode == IMESH_MODE) {
						dump_written += snprintf(dump_buf+dump_written, dump_len, "\n");
						dump_written += wl_ext_mesh_peer_status(tmp_if->dev, NULL,
							dump_buf+dump_written, dump_len-dump_written);
					}
#endif /* WLMESH */
				} else {
					dump_written += snprintf(dump_buf+dump_written, dump_len,
						"\n[dhd-%s-%c]:", tmp_if->ifname, tmp_if->prefix);
				}
			}
		}
		AEXT_INFO(dev->name, "%s\n", dump_buf);
	}

	if (!command && dump_buf)
		kfree(dump_buf);
	return dump_written;
}

static bool
wl_ext_master_if(struct wl_if_info *cur_if)
{
	if (cur_if->ifmode == IAP_MODE || cur_if->ifmode == IMESH_MODE)
		return TRUE;
	else
		return FALSE;
}

static int
wl_ext_if_down(struct wl_apsta_params *apsta_params, struct wl_if_info *cur_if)
{
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	scb_val_t scbval;
	struct {
		s32 cfg;
		s32 val;
	} bss_setbuf;
	apstamode_t apstamode = apsta_params->apstamode;

	WL_MSG(cur_if->ifname, "[%c] Turning off...\n", cur_if->prefix);

	if (cur_if->ifmode == ISTA_MODE) {
		wl_ext_ioctl(cur_if->dev, WLC_DISASSOC, NULL, 0, 1);
	} else if (cur_if->ifmode == IAP_MODE || cur_if->ifmode == IMESH_MODE) {
		// deauthenticate all STA first
		memcpy(scbval.ea.octet, &ether_bcast, ETHER_ADDR_LEN);
		wl_ext_ioctl(cur_if->dev, WLC_SCB_DEAUTHENTICATE, &scbval.ea, ETHER_ADDR_LEN, 1);
	}

	if (apstamode == IAPONLY_MODE || apstamode == IMESHONLY_MODE) {
		wl_ext_ioctl(cur_if->dev, WLC_DOWN, NULL, 0, 1);
	} else {
		bss_setbuf.cfg = 0xffffffff;
		bss_setbuf.val = htod32(0);
		wl_ext_iovar_setbuf(cur_if->dev, "bss", &bss_setbuf, sizeof(bss_setbuf),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
	}
	wl_clr_isam_status(cur_if, AP_CREATED);

	return 0;
}

static int
wl_ext_if_up(struct wl_apsta_params *apsta_params, struct wl_if_info *cur_if)
{
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	struct {
		s32 cfg;
		s32 val;
	} bss_setbuf;
	apstamode_t apstamode = apsta_params->apstamode;
	chanspec_t fw_chspec;
	u32 timeout;
	wlc_ssid_t ssid = { 0, {0} };
	uint16 chan = 0;

	if (cur_if->ifmode != IAP_MODE) {
		AEXT_ERROR(cur_if->ifname, "Wrong ifmode\n");
		return 0;
	}

	if (wl_ext_dfs_chan(cur_if->channel) && !apsta_params->radar) {
		WL_MSG(cur_if->ifname, "[%c] skip DFS channel %d\n",
			cur_if->prefix, cur_if->channel);
		return 0;
	} else if (!cur_if->channel) {
		WL_MSG(cur_if->ifname, "[%c] no valid channel\n", cur_if->prefix);
		return 0;
	}

	WL_MSG(cur_if->ifname, "[%c] Turning on...\n", cur_if->prefix);

	wl_ext_set_chanspec(cur_if->dev, apsta_params->ioctl_ver, cur_if->channel,
		&fw_chspec);

	wl_clr_isam_status(cur_if, AP_CREATED);
	wl_set_isam_status(cur_if, AP_CREATING);
	if (apstamode == IAPONLY_MODE) {
		wl_ext_ioctl(cur_if->dev, WLC_UP, NULL, 0, 1);
	} else {
		bss_setbuf.cfg = 0xffffffff;	
		bss_setbuf.val = htod32(1);
		wl_ext_iovar_setbuf(cur_if->dev, "bss", &bss_setbuf,
			sizeof(bss_setbuf), iovar_buf, WLC_IOCTL_SMLEN, NULL);
	}

	timeout = wait_event_interruptible_timeout(apsta_params->netif_change_event,
		wl_get_isam_status(cur_if, AP_CREATED),
		msecs_to_jiffies(MAX_AP_LINK_WAIT_TIME));
	if (timeout <= 0 || !wl_get_isam_status(cur_if, AP_CREATED)) {
		wl_ext_if_down(apsta_params, cur_if);
		WL_MSG(cur_if->ifname, "[%c] failed to up with SSID: \"%s\"\n",
			cur_if->prefix, cur_if->ssid);
	} else {
		wl_ext_ioctl(cur_if->dev, WLC_GET_SSID, &ssid, sizeof(ssid), 0);
		chan = wl_ext_get_chan(apsta_params, cur_if->dev);
		WL_MSG(cur_if->ifname, "[%c] enabled with SSID: \"%s\" on channel %d\n",
			cur_if->prefix, ssid.SSID, chan);
	}
	wl_clr_isam_status(cur_if, AP_CREATING);

	wl_ext_isam_status(cur_if->dev, NULL, 0);

	return 0;
}

static int
wl_ext_disable_iface(struct net_device *dev, char *ifname)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	int i;
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	wlc_ssid_t ssid = { 0, {0} };
	scb_val_t scbval;
	struct {
		s32 cfg;
		s32 val;
	} bss_setbuf;
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	apstamode_t apstamode = apsta_params->apstamode;
	struct wl_if_info *cur_if = NULL, *tmp_if = NULL;

	for (i=0; i<MAX_IF_NUM; i++) {
		tmp_if = &apsta_params->if_info[i];
		if (tmp_if->dev && !strcmp(tmp_if->dev->name, ifname)) {
			cur_if = tmp_if;
			break;
		}
	}
	if (!cur_if) {
		AEXT_ERROR(dev->name, "wrong ifname=%s or dev not ready\n", ifname);
		return -1;
	}

	mutex_lock(&apsta_params->usr_sync);
	WL_MSG(ifname, "[%c] Disabling...\n", cur_if->prefix);

	if (cur_if->ifmode == ISTA_MODE) {
		wl_ext_ioctl(cur_if->dev, WLC_DISASSOC, NULL, 0, 1);
	} else if (cur_if->ifmode == IAP_MODE || cur_if->ifmode == IMESH_MODE) {
		// deauthenticate all STA first
		memcpy(scbval.ea.octet, &ether_bcast, ETHER_ADDR_LEN);
		wl_ext_ioctl(cur_if->dev, WLC_SCB_DEAUTHENTICATE, &scbval.ea, ETHER_ADDR_LEN, 1);
	}

	if (apstamode == IAPONLY_MODE || apstamode == IMESHONLY_MODE) {
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_ioctl(dev, WLC_SET_SSID, &ssid, sizeof(ssid), 1); // reset ssid
		wl_ext_iovar_setint(dev, "mpc", 1);
	} else if ((apstamode==ISTAAP_MODE || apstamode==ISTAGO_MODE) &&
			cur_if->ifmode == IAP_MODE) {
		bss_setbuf.cfg = 0xffffffff;
		bss_setbuf.val = htod32(0);
		wl_ext_iovar_setbuf(cur_if->dev, "bss", &bss_setbuf, sizeof(bss_setbuf),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		wl_ext_iovar_setint(dev, "mpc", 1);
#ifdef ARP_OFFLOAD_SUPPORT
		/* IF SoftAP is disabled, enable arpoe back for STA mode. */
		dhd_arp_offload_set(dhd, dhd_arp_mode);
		dhd_arp_offload_enable(dhd, TRUE);
#endif /* ARP_OFFLOAD_SUPPORT */
#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO)
		if (dhd->conf->disable_proptx!=0) {
			bool enabled;
			dhd_wlfc_get_enable(dhd, &enabled);
			if (enabled) {
				dhd_wlfc_deinit(dhd);
			}
		}
#endif /* BCMSDIO */
#endif /* PROP_TXSTATUS_VSDB */
	}
	else if (apstamode == IDUALAP_MODE) {
		bss_setbuf.cfg = 0xffffffff;
		bss_setbuf.val = htod32(0);
		wl_ext_iovar_setbuf(cur_if->dev, "bss", &bss_setbuf, sizeof(bss_setbuf),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
#ifdef WLMESH
	} else if (apstamode == ISTAMESH_MODE || apstamode == IMESHAP_MODE ||
			apstamode == ISTAAPMESH_MODE || apstamode == IMESHAPAP_MODE ||
			apstamode == ISTAAPAP_MODE) {
		bss_setbuf.cfg = 0xffffffff;
		bss_setbuf.val = htod32(0);
		wl_ext_iovar_setbuf(cur_if->dev, "bss", &bss_setbuf, sizeof(bss_setbuf),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
#endif /* WLMESH */
	}
#ifdef WLMESH
	if ((cur_if->ifmode == IMESH_MODE) &&
			(apstamode == ISTAMESH_MODE || apstamode == IMESHAP_MODE ||
			apstamode == ISTAAPMESH_MODE || apstamode == IMESHAPAP_MODE)) {
		int scan_assoc_time = DHD_SCAN_ASSOC_ACTIVE_TIME;
		for (i=0; i<MAX_IF_NUM; i++) {
			tmp_if = &apsta_params->if_info[i];
			if (tmp_if->dev && tmp_if->ifmode == ISTA_MODE) {
				wl_ext_ioctl(tmp_if->dev, WLC_SET_SCAN_CHANNEL_TIME,
					&scan_assoc_time, sizeof(scan_assoc_time), 1);
			}
		}
	}
#endif /* WLMESH */

	wl_clr_isam_status(cur_if, AP_CREATED);

	WL_MSG(ifname, "[%c] Exit\n", cur_if->prefix);
	mutex_unlock(&apsta_params->usr_sync);
	return 0;
}

static int
wl_ext_iapsta_disable(struct net_device *dev, char *command, int total_len)
{
	int ret = 0;
	char *pch, *pick_tmp, *param;
	char ifname[IFNAMSIZ+1];

	AEXT_TRACE(dev->name, "command=%s, len=%d\n", command, total_len);

	pick_tmp = command;
	param = bcmstrtok(&pick_tmp, " ", 0); // skip iapsta_disable
	param = bcmstrtok(&pick_tmp, " ", 0);
	while (param != NULL) {
		if (!strcmp(param, "ifname")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch) {
				strcpy(ifname, pch);
				ret = wl_ext_disable_iface(dev, ifname);
				if (ret)
					return ret;
			}
			else {
				AEXT_ERROR(dev->name, "ifname [wlanX]\n");
				return -1;
			}
		}
		param = bcmstrtok(&pick_tmp, " ", 0);
	}

	return ret;
}

static bool
wl_ext_diff_band(uint16 chan1, uint16 chan2)
{
	if ((chan1 <= CH_MAX_2G_CHANNEL && chan2 > CH_MAX_2G_CHANNEL) ||
		(chan1 > CH_MAX_2G_CHANNEL && chan2 <= CH_MAX_2G_CHANNEL)) {
		return TRUE;
	}
	return FALSE;
}

static uint16
wl_ext_same_band(struct wl_apsta_params *apsta_params,
	struct wl_if_info *cur_if, bool nodfs)
{
	struct wl_if_info *tmp_if;
	uint16 tmp_chan, target_chan = 0;
	wl_prio_t max_prio;
	int i;

	// find the max prio
	max_prio = cur_if->prio;
	for (i=0; i<MAX_IF_NUM; i++) {
		tmp_if = &apsta_params->if_info[i];
		if (cur_if != tmp_if && wl_get_isam_status(tmp_if, IF_READY) &&
				tmp_if->prio > max_prio) {
			tmp_chan = wl_ext_get_chan(apsta_params, tmp_if->dev);
			if (wl_ext_dfs_chan(tmp_chan) && nodfs)
				continue;
			if (tmp_chan && !wl_ext_diff_band(cur_if->channel, tmp_chan)) {
				target_chan = tmp_chan;
				max_prio = tmp_if->prio;
			}
		}
	}

	return target_chan;
}

static uint16
wl_ext_get_vsdb_chan(struct wl_apsta_params *apsta_params,
	struct wl_if_info *cur_if, struct wl_if_info *target_if)
{
	uint16 target_chan = 0, cur_chan = cur_if->channel;

	target_chan = wl_ext_get_chan(apsta_params, target_if->dev);
	if (target_chan) {
		AEXT_INFO(cur_if->ifname, "cur_chan=%d, target_chan=%d\n",
			cur_chan, target_chan);
		if (wl_ext_diff_band(cur_chan, target_chan)) {
			if (!apsta_params->rsdb)
				return target_chan;
		} else {
			if (cur_chan != target_chan)
				return target_chan;
		}
	}

	return 0;
}

static int
wl_ext_rsdb_core_conflict(struct wl_apsta_params *apsta_params,
	struct wl_if_info *cur_if)
{
	struct wl_if_info *tmp_if;
	uint16 cur_chan, tmp_chan;
	int i;

	if (apsta_params->rsdb) {
		cur_chan = wl_ext_get_chan(apsta_params, cur_if->dev);
		for (i=0; i<MAX_IF_NUM; i++) {
			tmp_if = &apsta_params->if_info[i];
			if (tmp_if != cur_if && wl_get_isam_status(tmp_if, IF_READY) &&
					tmp_if->prio > cur_if->prio) {
				tmp_chan = wl_ext_get_chan(apsta_params, tmp_if->dev);
				if (!tmp_chan)
					continue;
				if (wl_ext_diff_band(cur_chan, tmp_chan) &&
						wl_ext_diff_band(cur_chan, cur_if->channel))
					return TRUE;
				else if (!wl_ext_diff_band(cur_chan, tmp_chan) &&
						wl_ext_diff_band(cur_chan, cur_if->channel))
					return TRUE;
			}
		}
	}
	return FALSE;
}

static int
wl_ext_trigger_csa(struct wl_apsta_params *apsta_params, struct wl_if_info *cur_if)
{
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	bool core_conflict = FALSE;

	if (wl_ext_master_if(cur_if) && (apsta_params->csa & CSA_DRV_BIT)) {
		if (!cur_if->channel) {
			WL_MSG(cur_if->ifname, "[%c] no valid channel\n", cur_if->prefix);
		} else if (wl_ext_dfs_chan(cur_if->channel) && !apsta_params->radar) {
			WL_MSG(cur_if->ifname, "[%c] skip DFS channel %d\n",
				cur_if->prefix, cur_if->channel);
			wl_ext_if_down(apsta_params, cur_if);
		} else {
			wl_chan_switch_t csa_arg;
			memset(&csa_arg, 0, sizeof(csa_arg));
			csa_arg.mode = 1;
			csa_arg.count = 3;
			csa_arg.chspec = wl_ext_chan_to_chanspec(apsta_params, cur_if->dev,
				cur_if->channel);
			core_conflict = wl_ext_rsdb_core_conflict(apsta_params, cur_if);
			if (core_conflict) {
				WL_MSG(cur_if->ifname, "[%c] Skip CSA due to rsdb core conflict\n",
					cur_if->prefix);
			} else if (csa_arg.chspec) {
				WL_MSG(cur_if->ifname, "[%c] Trigger CSA to channel %d(0x%x)\n",
					cur_if->prefix, cur_if->channel, csa_arg.chspec);
				wl_set_isam_status(cur_if, AP_CREATING);
				wl_ext_iovar_setbuf(cur_if->dev, "csa", &csa_arg, sizeof(csa_arg),
					iovar_buf, sizeof(iovar_buf), NULL);
				OSL_SLEEP(500);
				wl_clr_isam_status(cur_if, AP_CREATING);
				wl_ext_isam_status(cur_if->dev, NULL, 0);
			} else {
				AEXT_ERROR(cur_if->ifname, "fail to get chanspec\n");
			}
		}
	}

	return 0;
}

static void
wl_ext_move_cur_dfs_channel(struct wl_apsta_params *apsta_params,
	struct wl_if_info *cur_if)
{
	uint16 other_chan = 0, cur_chan = cur_if->channel;
	uint16 chan_2g = 0, chan_5g = 0;
	uint32 auto_band = WLC_BAND_2G;

	if (wl_ext_master_if(cur_if) && wl_ext_dfs_chan(cur_if->channel) &&
			!apsta_params->radar) {

		wl_ext_get_default_chan(cur_if->dev, &chan_2g, &chan_5g, TRUE);
		if (!chan_2g && !chan_5g) {
			cur_if->channel = 0;
			WL_MSG(cur_if->ifname, "[%c] no valid channel\n", cur_if->prefix);
			return;
		}

		if (apsta_params->vsdb) {
			if (chan_5g) {
				cur_if->channel = chan_5g;
				auto_band = WLC_BAND_5G;
				other_chan = wl_ext_same_band(apsta_params, cur_if, TRUE);
			} else {
				cur_if->channel = chan_2g;
				auto_band = WLC_BAND_2G;
				other_chan = wl_ext_same_band(apsta_params, cur_if, TRUE);
			}
			if (!other_chan) {
				other_chan = wl_ext_autochannel(cur_if->dev, ACS_FW_BIT|ACS_DRV_BIT,
					auto_band);
			}
			if (other_chan)
				cur_if->channel = other_chan;
		} else if (apsta_params->rsdb) {
			if (chan_5g) {
				cur_if->channel = chan_5g;
				auto_band = WLC_BAND_5G;
				other_chan = wl_ext_same_band(apsta_params, cur_if, FALSE);
				if (wl_ext_dfs_chan(other_chan) && chan_2g) {
					cur_if->channel = chan_2g;
					auto_band = WLC_BAND_2G;
					other_chan = wl_ext_same_band(apsta_params, cur_if, TRUE);
				}
			} else {
				cur_if->channel = chan_2g;
				auto_band = WLC_BAND_2G;
				other_chan = wl_ext_same_band(apsta_params, cur_if, TRUE);
			}
			if (!other_chan) {
				other_chan = wl_ext_autochannel(cur_if->dev, ACS_FW_BIT|ACS_DRV_BIT,
					auto_band);
			}
			if (other_chan)
				cur_if->channel = other_chan;
		} else {
			cur_if->channel = chan_5g;
			auto_band = WLC_BAND_5G;
			other_chan = wl_ext_same_band(apsta_params, cur_if, FALSE);
			if (wl_ext_dfs_chan(other_chan)) {
				cur_if->channel = 0;
			}
			else if (!other_chan) {
				other_chan = wl_ext_autochannel(cur_if->dev, ACS_FW_BIT|ACS_DRV_BIT,
					auto_band);
			}
			if (other_chan)
				cur_if->channel = other_chan;
		}
		WL_MSG(cur_if->ifname, "[%c] move channel %d => %d\n",
			cur_if->prefix, cur_chan, cur_if->channel);
	}
}

static void
wl_ext_move_other_dfs_channel(struct wl_apsta_params *apsta_params,
	struct wl_if_info *cur_if)
{
	uint16 other_chan = 0, cur_chan = cur_if->channel;
	uint16 chan_2g = 0, chan_5g = 0;
	uint32 auto_band = WLC_BAND_2G;

	if (wl_ext_master_if(cur_if) && wl_ext_dfs_chan(cur_if->channel) &&
			!apsta_params->radar) {

		wl_ext_get_default_chan(cur_if->dev, &chan_2g, &chan_5g, TRUE);
		if (!chan_2g && !chan_5g) {
			cur_if->channel = 0;
			WL_MSG(cur_if->ifname, "[%c] no valid channel\n", cur_if->prefix);
			return;
		}

		if (apsta_params->vsdb) {
			if (chan_5g) {
				cur_if->channel = chan_5g;
				auto_band = WLC_BAND_5G;
				other_chan = wl_ext_same_band(apsta_params, cur_if, TRUE);
			} else {
				cur_if->channel = chan_2g;
				auto_band = WLC_BAND_2G;
				other_chan = wl_ext_same_band(apsta_params, cur_if, TRUE);
			}
			if (!other_chan) {
				other_chan = wl_ext_autochannel(cur_if->dev, ACS_FW_BIT|ACS_DRV_BIT,
					auto_band);
			}
			if (other_chan)
				cur_if->channel = other_chan;
		} else if (apsta_params->rsdb) {
			if (chan_2g) {
				cur_if->channel = chan_2g;
				auto_band = WLC_BAND_2G;
				other_chan = wl_ext_same_band(apsta_params, cur_if, TRUE);
				if (!other_chan) {
					other_chan = wl_ext_autochannel(cur_if->dev, ACS_FW_BIT|ACS_DRV_BIT,
						auto_band);
				}
			} else {
				cur_if->channel = 0;
			}
			if (other_chan)
				cur_if->channel = other_chan;
		} else {
			cur_if->channel = 0;
		}
		WL_MSG(cur_if->ifname, "[%c] move channel %d => %d\n",
			cur_if->prefix, cur_chan, cur_if->channel);
	}
}

static uint16
wl_ext_move_cur_channel(struct wl_apsta_params *apsta_params,
	struct wl_if_info *cur_if)
{
	struct wl_if_info *tmp_if, *target_if = NULL;
	uint16 tmp_chan, target_chan = 0;
	wl_prio_t max_prio;
	int i;

	if (apsta_params->vsdb) {
		target_chan = cur_if->channel;
		goto exit;
	}

	// find the max prio
	max_prio = cur_if->prio;
	for (i=0; i<MAX_IF_NUM; i++) {
		tmp_if = &apsta_params->if_info[i];
		if (cur_if != tmp_if && wl_get_isam_status(tmp_if, IF_READY) &&
				tmp_if->prio > max_prio) {
			tmp_chan = wl_ext_get_vsdb_chan(apsta_params, cur_if, tmp_if);
			if (tmp_chan) {
				target_if = tmp_if;
				target_chan = tmp_chan;
				max_prio = tmp_if->prio;
			}
		}
	}

	if (target_chan) {
		tmp_chan = wl_ext_get_chan(apsta_params, cur_if->dev);
		if (apsta_params->rsdb && tmp_chan &&
				wl_ext_diff_band(tmp_chan, target_chan)) {
			WL_MSG(cur_if->ifname, "[%c] keep on current channel %d\n",
				cur_if->prefix, tmp_chan);
			cur_if->channel = 0;
		} else {
			WL_MSG(cur_if->ifname, "[%c] channel=%d => %s[%c] channel=%d\n",
				cur_if->prefix, cur_if->channel,
				target_if->ifname, target_if->prefix, target_chan);
			cur_if->channel = target_chan;
		}
	}

exit:
	wl_ext_move_cur_dfs_channel(apsta_params, cur_if);

	return cur_if->channel;
}

static void
wl_ext_move_other_channel(struct wl_apsta_params *apsta_params,
	struct wl_if_info *cur_if)
{
	struct wl_if_info *tmp_if, *target_if=NULL;
	uint16 tmp_chan, target_chan = 0;
	wl_prio_t max_prio = 0, cur_prio;
	int i;

	if (apsta_params->vsdb || !cur_if->channel) {
		return;
	}

	// find the max prio, but lower than cur_if
	cur_prio = cur_if->prio;
	for (i=0; i<MAX_IF_NUM; i++) {
		tmp_if = &apsta_params->if_info[i];
		if (cur_if != tmp_if && wl_get_isam_status(tmp_if, IF_READY) &&
				tmp_if->prio >= max_prio && tmp_if->prio <= cur_prio) {
			tmp_chan = wl_ext_get_vsdb_chan(apsta_params, cur_if, tmp_if);
			if (tmp_chan) {
				target_if = tmp_if;
				target_chan = tmp_chan;
				max_prio = tmp_if->prio;
			}
		}
	}

	if (target_if) {
		WL_MSG(target_if->ifname, "channel=%d => %s channel=%d\n",
			target_chan, cur_if->ifname, cur_if->channel);
		target_if->channel = cur_if->channel;
		wl_ext_move_other_dfs_channel(apsta_params, target_if);
		if (apsta_params->csa == 0) {
			wl_ext_if_down(apsta_params, target_if);
			wl_ext_move_other_channel(apsta_params, cur_if);
			if (target_if->ifmode == ISTA_MODE || target_if->ifmode == IMESH_MODE) {
				wl_ext_enable_iface(target_if->dev, target_if->ifname, 0);
			} else if (target_if->ifmode == IAP_MODE) {
				wl_ext_if_up(apsta_params, target_if);
			}
		} else {
			wl_ext_trigger_csa(apsta_params, target_if);
		}
	}

}

static bool
wl_ext_wait_other_enabling(struct wl_apsta_params *apsta_params,
	struct wl_if_info *cur_if)
{
	struct wl_if_info *tmp_if;
	bool enabling = FALSE;
	u32 timeout = 1;
	int i;

	for (i=0; i<MAX_IF_NUM; i++) {
		tmp_if = &apsta_params->if_info[i];
		if (tmp_if->dev && tmp_if->dev != cur_if->dev) {
			if (tmp_if->ifmode == ISTA_MODE)
				enabling = wl_get_isam_status(tmp_if, STA_CONNECTING);
			else if (tmp_if->ifmode == IAP_MODE || tmp_if->ifmode == IMESH_MODE)
				enabling = wl_get_isam_status(tmp_if, AP_CREATING);
			if (enabling)
				WL_MSG(cur_if->ifname, "waiting for %s[%c] enabling...\n",
					tmp_if->ifname, tmp_if->prefix);
			if (enabling && tmp_if->ifmode == ISTA_MODE) {
				timeout = wait_event_interruptible_timeout(
					apsta_params->netif_change_event,
					!wl_get_isam_status(tmp_if, STA_CONNECTING),
					msecs_to_jiffies(MAX_STA_LINK_WAIT_TIME));
			} else if (enabling &&
					(tmp_if->ifmode == IAP_MODE || tmp_if->ifmode == IMESH_MODE)) {
				timeout = wait_event_interruptible_timeout(
					apsta_params->netif_change_event,
					!wl_get_isam_status(tmp_if, AP_CREATING),
					msecs_to_jiffies(MAX_STA_LINK_WAIT_TIME));
			}
			if (tmp_if->ifmode == ISTA_MODE)
				enabling = wl_get_isam_status(tmp_if, STA_CONNECTING);
			else if (tmp_if->ifmode == IAP_MODE || tmp_if->ifmode == IMESH_MODE)
				enabling = wl_get_isam_status(tmp_if, AP_CREATING);
			if (timeout <= 0 || enabling) {
				WL_MSG(cur_if->ifname, "%s[%c] is still enabling...\n",
					tmp_if->ifname, tmp_if->prefix);
			}
		}
	}

	return enabling;
}

static int
wl_ext_enable_iface(struct net_device *dev, char *ifname, int wait_up)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	int i, ret = 0;
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	wlc_ssid_t ssid = { 0, {0} };
	chanspec_t fw_chspec;
	struct {
		s32 cfg;
		s32 val;
	} bss_setbuf;
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	apstamode_t apstamode = apsta_params->apstamode;
	struct wl_if_info *cur_if = NULL, *tmp_if = NULL;
	uint16 cur_chan;
	struct wl_conn_info conn_info;
	u32 timeout;

	for (i=0; i<MAX_IF_NUM; i++) {
		tmp_if = &apsta_params->if_info[i];
		if (tmp_if->dev && !strcmp(tmp_if->dev->name, ifname)) {
			cur_if = tmp_if;
			break;
		}
	}
	if (!cur_if) {
		AEXT_ERROR(dev->name, "wrong ifname=%s or dev not ready\n", ifname);
		return -1;
	}

	mutex_lock(&apsta_params->usr_sync);

	if (cur_if->ifmode == ISTA_MODE) {
		wl_set_isam_status(cur_if, STA_CONNECTING);
	} else if (cur_if->ifmode == IAP_MODE || cur_if->ifmode == IMESH_MODE) {
		wl_set_isam_status(cur_if, AP_CREATING);
	}

	wl_ext_isam_status(cur_if->dev, NULL, 0);
	WL_MSG(ifname, "[%c] Enabling...\n", cur_if->prefix);

	wl_ext_wait_other_enabling(apsta_params, cur_if);

	if (wl_ext_master_if(cur_if) && apsta_params->acs) {
		uint16 chan_2g, chan_5g;
		uint auto_band;
		auto_band = WL_GET_BAND(cur_if->channel);
		wl_ext_get_default_chan(cur_if->dev, &chan_2g, &chan_5g, TRUE);
		if ((chan_2g && auto_band == WLC_BAND_2G) ||
				(chan_5g && auto_band == WLC_BAND_5G)) {
			cur_if->channel = wl_ext_autochannel(cur_if->dev, apsta_params->acs,
				auto_band);
		} else {
			AEXT_ERROR(ifname, "invalid channel\n");
			ret = -1;
			goto exit;
		}
	}

	wl_ext_move_cur_channel(apsta_params, cur_if);

	if (wl_ext_master_if(cur_if) && !cur_if->channel) {
		AEXT_ERROR(ifname, "skip channel 0\n");
		ret = -1;
		goto exit;
	}

	cur_chan = wl_ext_get_chan(apsta_params, cur_if->dev);
	if (cur_chan) {
		AEXT_INFO(cur_if->ifname, "Associated\n");
		if (cur_chan != cur_if->channel) {
			wl_ext_trigger_csa(apsta_params, cur_if);
		}
		goto exit;
	}
	if (cur_if->ifmode == ISTA_MODE) {
		wl_clr_isam_status(cur_if, STA_CONNECTED);
	} else if (cur_if->ifmode == IAP_MODE || cur_if->ifmode == IMESH_MODE) {
		wl_clr_isam_status(cur_if, AP_CREATED);
	}

	wl_ext_move_other_channel(apsta_params, cur_if);

	if (cur_if->ifidx > 0) {
		wl_ext_iovar_setbuf(cur_if->dev, "cur_etheraddr", (u8 *)cur_if->dev->dev_addr,
			ETHER_ADDR_LEN, iovar_buf, WLC_IOCTL_SMLEN, NULL);
	}

	// set ssid for AP
	ssid.SSID_len = strlen(cur_if->ssid);
	memcpy(ssid.SSID, cur_if->ssid, ssid.SSID_len);
	if (cur_if->ifmode == IAP_MODE || cur_if->ifmode == IMESH_MODE) {
		wl_ext_iovar_setint(dev, "mpc", 0);
		if (apstamode == IAPONLY_MODE || apstamode == IMESHONLY_MODE) {
			wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		} else if (apstamode==ISTAAP_MODE || apstamode==ISTAGO_MODE) {
			wl_ext_iovar_setbuf_bsscfg(cur_if->dev, "ssid", &ssid, sizeof(ssid),
				iovar_buf, WLC_IOCTL_SMLEN, cur_if->bssidx, NULL);
		}
	}

	if (wl_ext_master_if(cur_if)) {
		wl_ext_set_bgnmode(cur_if);
		if (!cur_if->channel) {
			cur_if->channel = 1;
		}
		ret = wl_ext_set_chanspec(cur_if->dev, apsta_params->ioctl_ver,
			cur_if->channel, &fw_chspec);
		if (ret)
			goto exit;
	}

	wl_ext_set_amode(cur_if);
	wl_ext_set_emode(apsta_params, cur_if);

	if (cur_if->ifmode == ISTA_MODE) {
		conn_info.bssidx = cur_if->bssidx;
		conn_info.channel = cur_if->channel;
		memcpy(conn_info.ssid.SSID, cur_if->ssid, strlen(cur_if->ssid));
		conn_info.ssid.SSID_len = strlen(cur_if->ssid);
		memcpy(&conn_info.bssid, &cur_if->bssid, ETHER_ADDR_LEN);
	}
	if (cur_if->ifmode == IAP_MODE) {
		if (cur_if->maxassoc >= 0)
			wl_ext_iovar_setint(dev, "maxassoc", cur_if->maxassoc);
		// terence: fix me, hidden does not work in dualAP mode
		if (cur_if->hidden > 0) {
			wl_ext_ioctl(cur_if->dev, WLC_SET_CLOSED, &cur_if->hidden,
				sizeof(cur_if->hidden), 1);
			WL_MSG(ifname, "[%c] Broadcast SSID: %s\n",
				cur_if->prefix, cur_if->hidden ? "OFF":"ON");
		}
	}

	if (apstamode == ISTAONLY_MODE) {
		wl_ext_connect(cur_if->dev, &conn_info);
	} else if (apstamode == IAPONLY_MODE) {
		wl_ext_ioctl(cur_if->dev, WLC_SET_SSID, &ssid, sizeof(ssid), 1);
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
	} else if (apstamode == ISTAAP_MODE || apstamode == ISTAGO_MODE) {
		if (cur_if->ifmode == ISTA_MODE) {
			wl_ext_connect(cur_if->dev, &conn_info);
		} else {
			if (FW_SUPPORTED(dhd, rsdb)) {
				wl_ext_ioctl(cur_if->dev, WLC_SET_SSID, &ssid, sizeof(ssid), 1);
			} else {
				bss_setbuf.cfg = htod32(cur_if->bssidx);
				bss_setbuf.val = htod32(1);
				wl_ext_iovar_setbuf(cur_if->dev, "bss", &bss_setbuf,
					sizeof(bss_setbuf), iovar_buf, WLC_IOCTL_SMLEN, NULL);
			}
#ifdef ARP_OFFLOAD_SUPPORT
			/* IF SoftAP is enabled, disable arpoe */
			dhd_arp_offload_set(dhd, 0);
			dhd_arp_offload_enable(dhd, FALSE);
#endif /* ARP_OFFLOAD_SUPPORT */
#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO)
			if (!(FW_SUPPORTED(dhd, rsdb)) && !disable_proptx) {
				bool enabled;
				dhd_wlfc_get_enable(dhd, &enabled);
				if (!enabled) {
					dhd_wlfc_init(dhd);
					wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
				}
			}
#endif /* BCMSDIO */
#endif /* PROP_TXSTATUS_VSDB */
		}
	}
	else if (apstamode == IDUALAP_MODE) {
		wl_ext_ioctl(cur_if->dev, WLC_SET_SSID, &ssid, sizeof(ssid), 1);
	} else if (apstamode == ISTAAPAP_MODE) {
		if (cur_if->ifmode == ISTA_MODE) {
			wl_ext_connect(cur_if->dev, &conn_info);
		} else if (cur_if->ifmode == IAP_MODE) {
			wl_ext_ioctl(cur_if->dev, WLC_SET_SSID, &ssid, sizeof(ssid), 1);
		} else {
			AEXT_ERROR(cur_if->ifname, "wrong ifmode %d\n", cur_if->ifmode);
		}
#ifdef WLMESH
	} else if (apstamode == IMESHONLY_MODE ||
			apstamode == ISTAMESH_MODE || apstamode == IMESHAP_MODE ||
			apstamode == ISTAAPMESH_MODE || apstamode == IMESHAPAP_MODE) {
		if (cur_if->ifmode == ISTA_MODE) {
			wl_ext_connect(cur_if->dev, &conn_info);
		} else if (cur_if->ifmode == IAP_MODE) {
			wl_ext_ioctl(cur_if->dev, WLC_SET_SSID, &ssid, sizeof(ssid), 1);
		} else if (cur_if->ifmode == IMESH_MODE) {
			struct wl_join_params join_params;
			// need to up before setting ssid
			memset(&join_params, 0, sizeof(join_params));
			join_params.ssid.SSID_len = strlen(cur_if->ssid);
			memcpy((void *)join_params.ssid.SSID, cur_if->ssid, strlen(cur_if->ssid));
			join_params.params.chanspec_list[0] = fw_chspec;
			join_params.params.chanspec_num = 1;
			wl_ext_ioctl(cur_if->dev, WLC_SET_SSID, &join_params, sizeof(join_params), 1);
		} else {
			AEXT_ERROR(cur_if->ifname, "wrong ifmode %d\n", cur_if->ifmode);
		}
#endif /* WLMESH */
	}

	if (wait_up) {
		OSL_SLEEP(wait_up);
	} else if (cur_if->ifmode == IAP_MODE || cur_if->ifmode == IMESH_MODE) {
		timeout = wait_event_interruptible_timeout(apsta_params->netif_change_event,
			wl_get_isam_status(cur_if, AP_CREATED),
			msecs_to_jiffies(MAX_AP_LINK_WAIT_TIME));
		if (timeout <= 0 || !wl_get_isam_status(cur_if, AP_CREATED)) {
			mutex_unlock(&apsta_params->usr_sync);
			wl_ext_disable_iface(dev, cur_if->ifname);
			WL_MSG(ifname, "[%c] failed to enable with SSID: \"%s\"\n",
				cur_if->prefix, cur_if->ssid);
			ret = -1;
		}
	}

	if (wl_get_isam_status(cur_if, AP_CREATED) &&
			(cur_if->ifmode == IMESH_MODE || cur_if->ifmode == IAP_MODE) &&
			(apstamode == ISTAAP_MODE || apstamode == ISTAAPAP_MODE ||
			apstamode == ISTAMESH_MODE || apstamode == IMESHAP_MODE ||
			apstamode == ISTAAPMESH_MODE || apstamode == IMESHAPAP_MODE)) {
		int scan_assoc_time = 80;
		for (i=0; i<MAX_IF_NUM; i++) {
			tmp_if = &apsta_params->if_info[i];
			if (tmp_if->dev && tmp_if->ifmode == ISTA_MODE) {
				wl_ext_ioctl(tmp_if->dev, WLC_SET_SCAN_CHANNEL_TIME,
					&scan_assoc_time, sizeof(scan_assoc_time), 1);
			}
		}
	}

	wl_ext_isam_status(cur_if->dev, NULL, 0);

exit:
	if (cur_if->ifmode == IAP_MODE || cur_if->ifmode == IMESH_MODE) {
		wl_clr_isam_status(cur_if, AP_CREATING);
	}
	WL_MSG(ifname, "[%c] Exit ret=%d\n", cur_if->prefix, ret);
	mutex_unlock(&apsta_params->usr_sync);
	return ret;
}

static int
wl_ext_iapsta_enable(struct net_device *dev, char *command, int total_len)
{
	int ret = 0;
	char *pch, *pick_tmp, *param;
	char ifname[IFNAMSIZ+1];

	AEXT_TRACE(dev->name, "command=%s, len=%d\n", command, total_len);

	pick_tmp = command;
	param = bcmstrtok(&pick_tmp, " ", 0); // skip iapsta_enable
	param = bcmstrtok(&pick_tmp, " ", 0);
	while (param != NULL) {
		if (!strcmp(param, "ifname")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch) {
				strcpy(ifname, pch);
				ret = wl_ext_enable_iface(dev, ifname, 0);
				if (ret)
					return ret;
			} else {
				AEXT_ERROR(dev->name, "ifname [wlanX]\n");
				return -1;
			}
		}
		param = bcmstrtok(&pick_tmp, " ", 0);
	}

	return ret;
}

static int
wl_ext_iapsta_event(struct net_device *dev,
	struct wl_apsta_params *apsta_params, wl_event_msg_t *e, void* data)
{
	struct wl_if_info *cur_if = NULL, *tmp_if = NULL;
#if defined(WLMESH) && defined(WL_ESCAN)
	struct wl_if_info *mesh_if = NULL;
#endif /* WLMESH && WL_ESCAN */
	int i;
	uint32 event_type = ntoh32(e->event_type);
	uint32 status =  ntoh32(e->status);
	uint32 reason =  ntoh32(e->reason);
	uint16 flags =  ntoh16(e->flags);

	for (i=0; i<MAX_IF_NUM; i++) {
		tmp_if = &apsta_params->if_info[i];
		if (tmp_if->dev == dev) {
			cur_if = tmp_if;
			break;
		}
	}
#if defined(WLMESH) && defined(WL_ESCAN)
	for (i=0; i<MAX_IF_NUM; i++) {
		tmp_if = &apsta_params->if_info[i];
		if (tmp_if->dev && tmp_if->ifmode == IMESH_MODE) {
			mesh_if = tmp_if;
			break;
		}
	}
#endif /* WLMESH && WL_ESCAN */
	if (!cur_if || !cur_if->dev) {
		AEXT_DBG(dev->name, "ifidx %d is not ready\n", e->ifidx);
		return -1;
	}

	if (cur_if->ifmode == ISTA_MODE) {
		if (event_type == WLC_E_LINK) {
			if (!(flags & WLC_EVENT_MSG_LINK)) {
				WL_MSG(cur_if->ifname,
					"[%c] Link down with %pM, %s(%d), reason %d\n",
					cur_if->prefix, &e->addr, bcmevent_get_name(event_type),
					event_type, reason);
				wl_clr_isam_status(cur_if, STA_CONNECTED);
#if defined(WLMESH) && defined(WL_ESCAN)
				if (mesh_if && apsta_params->macs)
					wl_mesh_clear_mesh_info(apsta_params, mesh_if, TRUE);
#endif /* WLMESH && WL_ESCAN */
			} else {
				WL_MSG(cur_if->ifname, "[%c] Link UP with %pM\n",
					cur_if->prefix, &e->addr);
				wl_set_isam_status(cur_if, STA_CONNECTED);
#if defined(WLMESH) && defined(WL_ESCAN)
				if (mesh_if && apsta_params->macs)
					wl_mesh_update_master_info(apsta_params, mesh_if);
#endif /* WLMESH && WL_ESCAN */
			}
			wl_clr_isam_status(cur_if, STA_CONNECTING);
			wake_up_interruptible(&apsta_params->netif_change_event);
		} else if (event_type == WLC_E_SET_SSID && status != WLC_E_STATUS_SUCCESS) {
			WL_MSG(cur_if->ifname,
				"connect failed event=%d, reason=%d, status=%d\n",
				event_type, reason, status);
			wl_clr_isam_status(cur_if, STA_CONNECTING);
			wake_up_interruptible(&apsta_params->netif_change_event);
#if defined(WLMESH) && defined(WL_ESCAN)
			if (mesh_if && apsta_params->macs)
				wl_mesh_clear_mesh_info(apsta_params, mesh_if, TRUE);
#endif /* WLMESH && WL_ESCAN */
		} else if (event_type == WLC_E_DEAUTH || event_type == WLC_E_DEAUTH_IND ||
				event_type == WLC_E_DISASSOC || event_type == WLC_E_DISASSOC_IND) {
			WL_MSG(cur_if->ifname, "[%c] Link down with %pM, %s(%d), reason %d\n",
				cur_if->prefix, &e->addr, bcmevent_get_name(event_type),
				event_type, reason);
#if defined(WLMESH) && defined(WL_ESCAN)
			if (mesh_if && apsta_params->macs)
				wl_mesh_clear_mesh_info(apsta_params, mesh_if, TRUE);
#endif /* WLMESH && WL_ESCAN */
		}
	}
	else if (cur_if->ifmode == IAP_MODE || cur_if->ifmode == IMESH_MODE) {
		if ((event_type == WLC_E_SET_SSID && status == WLC_E_STATUS_SUCCESS) ||
				(event_type == WLC_E_LINK && status == WLC_E_STATUS_SUCCESS &&
				reason == WLC_E_REASON_INITIAL_ASSOC)) {
			if (wl_get_isam_status(cur_if, AP_CREATING)) {
				WL_MSG(cur_if->ifname, "[%c] Link up (etype=%d)\n",
					cur_if->prefix, event_type);
				wl_set_isam_status(cur_if, AP_CREATED);
				wake_up_interruptible(&apsta_params->netif_change_event);
			} else {
				wl_set_isam_status(cur_if, AP_CREATED);
				WL_MSG(cur_if->ifname, "[%c] Link up w/o creating? (etype=%d)\n",
					cur_if->prefix, event_type);
			}
		}
		else if ((event_type == WLC_E_LINK && reason == WLC_E_LINK_BSSCFG_DIS) ||
				(event_type == WLC_E_LINK && status == WLC_E_STATUS_SUCCESS &&
				reason == WLC_E_REASON_DEAUTH)) {
			wl_clr_isam_status(cur_if, AP_CREATED);
			WL_MSG(cur_if->ifname, "[%c] Link down, reason=%d\n",
				cur_if->prefix, reason);
		}
		else if ((event_type == WLC_E_ASSOC_IND || event_type == WLC_E_REASSOC_IND) &&
				reason == DOT11_SC_SUCCESS) {
			WL_MSG(cur_if->ifname, "[%c] connected device %pM\n",
				cur_if->prefix, &e->addr);
			wl_ext_isam_status(cur_if->dev, NULL, 0);
		}
		else if (event_type == WLC_E_DISASSOC_IND ||
				event_type == WLC_E_DEAUTH_IND ||
				(event_type == WLC_E_DEAUTH && reason != DOT11_RC_RESERVED)) {
			WL_MSG(cur_if->ifname,
				"[%c] disconnected device %pM, %s(%d), reason=%d\n",
				cur_if->prefix, &e->addr, bcmevent_get_name(event_type),
				event_type, reason);
			wl_ext_isam_status(cur_if->dev, NULL, 0);
		}
#if defined(WLMESH) && defined(WL_ESCAN)
		if (cur_if->ifmode == IMESH_MODE && apsta_params->macs)
			wl_mesh_event_handler(apsta_params, cur_if, e, data);
#endif /* WLMESH && WL_ESCAN */
	}

	return 0;
}

#ifdef WL_CFG80211
u32
wl_ext_iapsta_update_channel(dhd_pub_t *dhd, struct net_device *dev,
	u32 channel)
{
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *cur_if = NULL, *tmp_if = NULL;
	int i;

	for (i=0; i<MAX_IF_NUM; i++) {
		tmp_if = &apsta_params->if_info[i];
		if (tmp_if->dev && tmp_if->dev == dev) {
			cur_if = tmp_if;
			break;
		}
	}

	if (cur_if) {
		wl_ext_isam_status(cur_if->dev, NULL, 0);
		cur_if->channel = channel;
		if (wl_ext_master_if(cur_if) && apsta_params->acs) {
			uint auto_band = WL_GET_BAND(channel);
			cur_if->channel = wl_ext_autochannel(cur_if->dev, apsta_params->acs,
				auto_band);
		}
		channel = wl_ext_move_cur_channel(apsta_params, cur_if);
		if (channel)
			wl_ext_move_other_channel(apsta_params, cur_if);
		if (cur_if->ifmode == ISTA_MODE)
			wl_set_isam_status(cur_if, STA_CONNECTING);
	}

	return channel;
}

void
wl_ext_iapsta_update_iftype(struct net_device *net, int ifidx, int wl_iftype)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *cur_if = NULL;

	AEXT_TRACE(net->name, "ifidx=%d, wl_iftype=%d\n", ifidx, wl_iftype);

	if (ifidx < MAX_IF_NUM) {
		cur_if = &apsta_params->if_info[ifidx];
	}

	if (cur_if) {
		if (wl_iftype == WL_IF_TYPE_STA) {
			cur_if->ifmode = ISTA_MODE;
			cur_if->prio = PRIO_STA;
			cur_if->prefix = 'S';
		} else if (wl_iftype == WL_IF_TYPE_AP && cur_if->ifmode != IMESH_MODE) {
			cur_if->ifmode = IAP_MODE;
			cur_if->prio = PRIO_AP;
			cur_if->prefix = 'A';
		}
	}
}

void
wl_ext_iapsta_ifadding(struct net_device *net, int ifidx)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *cur_if = NULL;

	AEXT_TRACE(net->name, "ifidx=%d\n", ifidx);
	if (ifidx < MAX_IF_NUM) {
		cur_if = &apsta_params->if_info[ifidx];
		wl_set_isam_status(cur_if, IF_ADDING);
	}
}

bool
wl_ext_iapsta_mesh_creating(struct net_device *net)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *cur_if;
	int i;

	if (apsta_params) {
		for (i=0; i<MAX_IF_NUM; i++) {
			cur_if = &apsta_params->if_info[i];
			if (cur_if->ifmode==IMESH_MODE && wl_get_isam_status(cur_if, IF_ADDING))
				return TRUE;
		}
	}
	return FALSE;
}
#endif /* WL_CFG80211 */

int
wl_ext_iapsta_alive_preinit(struct net_device *dev)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;

	if (apsta_params->init == TRUE) {
		AEXT_ERROR(dev->name, "don't init twice\n");
		return -1;
	}

	AEXT_TRACE(dev->name, "Enter\n");

	apsta_params->init = TRUE;

	return 0;
}

int
wl_ext_iapsta_alive_postinit(struct net_device *dev)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	s32 apsta = 0, ap = 0;
	struct wl_if_info *cur_if;
	int i;

	wl_ext_iovar_getint(dev, "apsta", &apsta);
	wl_ext_ioctl(dev, WLC_GET_AP, &ap, sizeof(ap), 0);
	if (apsta == 1 || ap == 0) {
		apsta_params->apstamode = ISTAONLY_MODE;
		apsta_params->if_info[IF_PIF].ifmode = ISTA_MODE;
		op_mode = DHD_FLAG_STA_MODE;
	} else {
		apsta_params->apstamode = IAPONLY_MODE;
		apsta_params->if_info[IF_PIF].ifmode = IAP_MODE;
		op_mode = DHD_FLAG_HOSTAP_MODE;
	}
	// fix me: how to check it's ISTAAP_MODE or IDUALAP_MODE?

	wl_ext_get_ioctl_ver(dev, &apsta_params->ioctl_ver);
	WL_MSG(dev->name, "apstamode=%d\n", apsta_params->apstamode);

	for (i=0; i<MAX_IF_NUM; i++) {
		cur_if = &apsta_params->if_info[i];
		if (i == 1 && !strlen(cur_if->ifname))
			strcpy(cur_if->ifname, "wlan1");
		if (i == 2 && !strlen(cur_if->ifname))
			strcpy(cur_if->ifname, "wlan2");
		if (cur_if->ifmode == ISTA_MODE) {
			cur_if->channel = 0;
			cur_if->maxassoc = -1;
			wl_set_isam_status(cur_if, IF_READY);
			cur_if->prio = PRIO_STA;
			cur_if->prefix = 'S';
			snprintf(cur_if->ssid, DOT11_MAX_SSID_LEN, "ttt_sta");
		} else if (cur_if->ifmode == IAP_MODE) {
			cur_if->channel = 1;
			cur_if->maxassoc = -1;
			wl_set_isam_status(cur_if, IF_READY);
			cur_if->prio = PRIO_AP;
			cur_if->prefix = 'A';
			snprintf(cur_if->ssid, DOT11_MAX_SSID_LEN, "ttt_ap");
#ifdef WLMESH
		} else if (cur_if->ifmode == IMESH_MODE) {
			cur_if->channel = 1;
			cur_if->maxassoc = -1;
			wl_set_isam_status(cur_if, IF_READY);
			cur_if->prio = PRIO_MESH;
			cur_if->prefix = 'M';
			snprintf(cur_if->ssid, DOT11_MAX_SSID_LEN, "ttt_mesh");
#endif /* WLMESH */
		}
	}

	return op_mode;
}

static int
wl_ext_iapsta_get_rsdb(struct net_device *net, struct dhd_pub *dhd)
{
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	wl_config_t *rsdb_p;
	int ret = 0, rsdb = 0;

	if (dhd->conf->chip == BCM4359_CHIP_ID) {
		ret = wldev_iovar_getbuf(net, "rsdb_mode", NULL, 0,
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		if (!ret) {
			if (dhd->conf->fw_type == FW_TYPE_MESH) {
				rsdb = 1;
			} else {
				rsdb_p = (wl_config_t *) iovar_buf;
				rsdb = rsdb_p->config;
			}
		}
	}

	AEXT_INFO(net->name, "rsdb_mode=%d\n", rsdb);

	return rsdb;
}

static void
wl_ext_iapsta_postinit(struct net_device *net, struct wl_if_info *cur_if)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	int pm;

	AEXT_TRACE(cur_if->ifname, "ifidx=%d\n", cur_if->ifidx);
	if (cur_if->ifidx == 0) {
		apsta_params->rsdb = wl_ext_iapsta_get_rsdb(net, dhd);
		apsta_params->vsdb = FALSE;
		apsta_params->csa = 0;
		apsta_params->acs = 0;
		apsta_params->radar = wl_ext_radar_detect(net);
		if (dhd->conf->fw_type == FW_TYPE_MESH) {
			apsta_params->csa |= (CSA_FW_BIT | CSA_DRV_BIT);
		}
	} else {
		if (cur_if->ifmode == ISTA_MODE) {
			wl_ext_iovar_setint(cur_if->dev, "roam_off", dhd->conf->roam_off);
			wl_ext_iovar_setint(cur_if->dev, "bcn_timeout", dhd->conf->bcn_timeout);
			if (dhd->conf->pm >= 0)
				pm = dhd->conf->pm;
			else
				pm = PM_FAST;
			wl_ext_ioctl(cur_if->dev, WLC_SET_PM, &pm, sizeof(pm), 1);
			wl_ext_iovar_setint(cur_if->dev, "assoc_retry_max", 20);
		}
#ifdef WLMESH
		else if (cur_if->ifmode == IMESH_MODE) {
			pm = 0;
			wl_ext_ioctl(cur_if->dev, WLC_SET_PM, &pm, sizeof(pm), 1);
		}
#endif /* WLMESH */
	}

}

int
wl_ext_iapsta_attach_name(struct net_device *net, int ifidx)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *cur_if = NULL;

	AEXT_TRACE(net->name, "ifidx=%d\n", ifidx);
	if (ifidx < MAX_IF_NUM) {
		cur_if = &apsta_params->if_info[ifidx];
	}
	if (ifidx == 0) {
		strcpy(cur_if->ifname, net->name);
		wl_ext_iapsta_postinit(net, cur_if);
		wl_set_isam_status(cur_if, IF_READY);
	} else if (cur_if && wl_get_isam_status(cur_if, IF_ADDING)) {
		strcpy(cur_if->ifname, net->name);
		wl_ext_iapsta_postinit(net, cur_if);
		wl_clr_isam_status(cur_if, IF_ADDING);
		wl_set_isam_status(cur_if, IF_READY);
#ifndef WL_STATIC_IF
		wake_up_interruptible(&apsta_params->netif_change_event);
#endif /* WL_STATIC_IF */
	}

	return 0;
}

int
wl_ext_iapsta_update_net_device(struct net_device *net, int ifidx)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *cur_if = NULL, *primary_if;

	AEXT_TRACE(net->name, "ifidx=%d\n", ifidx);
	if (ifidx < MAX_IF_NUM) {
		cur_if = &apsta_params->if_info[ifidx];
	}
	if (cur_if && wl_get_isam_status(cur_if, IF_ADDING)) {
		primary_if = &apsta_params->if_info[IF_PIF];
		if (strlen(cur_if->ifname)) {
			memset(net->name, 0, sizeof(IFNAMSIZ));
			strcpy(net->name, cur_if->ifname);
			net->name[IFNAMSIZ-1] = '\0';
		}
#ifndef WL_STATIC_IF
		if (apsta_params->apstamode != ISTAAPAP_MODE &&
				apsta_params->apstamode != ISTASTA_MODE) {
			memcpy(net->dev_addr, primary_if->dev->dev_addr, ETHER_ADDR_LEN);
			net->dev_addr[0] |= 0x02;
			if (ifidx >= 2) {
				net->dev_addr[4] ^= 0x80;
				net->dev_addr[4] += ifidx;
				net->dev_addr[5] += (ifidx-1);
			}
		}
#endif /* WL_STATIC_IF */
	}

	return 0;
}

int
wl_ext_iapsta_attach_netdev(struct net_device *net, int ifidx, uint8 bssidx)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *cur_if = NULL, *primary_if;

	AEXT_TRACE(net->name, "ifidx=%d, bssidx=%d\n", ifidx, bssidx);
	if (ifidx < MAX_IF_NUM) {
		cur_if = &apsta_params->if_info[ifidx];
	}
	if (ifidx == 0) {
		memset(apsta_params, 0, sizeof(struct wl_apsta_params));
		apsta_params->dhd = dhd;
		cur_if->dev = net;
		cur_if->ifidx = ifidx;
		cur_if->bssidx = bssidx;
		cur_if->ifmode = ISTA_MODE;
		cur_if->prio = PRIO_STA;
		cur_if->prefix = 'S';
		wl_ext_event_register(net, dhd, WLC_E_LAST, wl_ext_iapsta_event,
			apsta_params, PRIO_EVENT_IAPSTA);
		strcpy(cur_if->ifname, net->name);
		init_waitqueue_head(&apsta_params->netif_change_event);
		mutex_init(&apsta_params->usr_sync);
	} else if (cur_if && wl_get_isam_status(cur_if, IF_ADDING)) {
		primary_if = &apsta_params->if_info[IF_PIF];
		cur_if->dev = net;
		cur_if->ifidx = ifidx;
		cur_if->bssidx = bssidx;
		wl_ext_event_register(net, dhd, WLC_E_LAST, wl_ext_iapsta_event,
			apsta_params, PRIO_EVENT_IAPSTA);
#if defined(WLMESH) && defined(WL_ESCAN)
		if (cur_if->ifmode == IMESH_MODE && apsta_params->macs) {
			wl_mesh_escan_attach(dhd, cur_if);
		}
#endif /* WLMESH && WL_ESCAN */
	}

	return 0;
}

int
wl_ext_iapsta_dettach_netdev(struct net_device *net, int ifidx)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *cur_if = NULL;

	if (!apsta_params)
		return 0;

	AEXT_TRACE(net->name, "ifidx=%d\n", ifidx);
	if (ifidx < MAX_IF_NUM) {
		cur_if = &apsta_params->if_info[ifidx];
	}

	if (ifidx == 0) {
		wl_ext_event_deregister(net, dhd, WLC_E_LAST, wl_ext_iapsta_event);
#if defined(WLMESH) && defined(WL_ESCAN)
		if (cur_if->ifmode == IMESH_MODE && apsta_params->macs) {
			wl_mesh_escan_detach(dhd, cur_if);
		}
#endif /* WLMESH && WL_ESCAN */
		memset(apsta_params, 0, sizeof(struct wl_apsta_params));
	} else if (cur_if && (wl_get_isam_status(cur_if, IF_READY) ||
			wl_get_isam_status(cur_if, IF_ADDING))) {
		wl_ext_event_deregister(net, dhd, WLC_E_LAST, wl_ext_iapsta_event);
#if defined(WLMESH) && defined(WL_ESCAN)
		if (cur_if->ifmode == IMESH_MODE && apsta_params->macs) {
			wl_mesh_escan_detach(dhd, cur_if);
		}
#endif /* WLMESH && WL_ESCAN */
		memset(cur_if, 0, sizeof(struct wl_if_info));
	}

	return 0;
}

int
wl_ext_iapsta_attach(dhd_pub_t *pub)
{
	struct wl_apsta_params *iapsta_params;

	iapsta_params = kzalloc(sizeof(struct wl_apsta_params), GFP_KERNEL);
	if (unlikely(!iapsta_params)) {
		AEXT_ERROR("wlan", "Could not allocate apsta_params\n");
		return -ENOMEM;
	}
	pub->iapsta_params = (void *)iapsta_params;

	return 0;
}

void
wl_ext_iapsta_dettach(dhd_pub_t *pub)
{
	if (pub->iapsta_params) {
		kfree(pub->iapsta_params);
		pub->iapsta_params = NULL;
	}
}
#endif /* WL_EXT_IAPSTA */

#ifdef IDHCP
/*
terence 20190409:
dhd_priv wl dhcpc_dump
dhd_priv wl dhcpc_param <client ip> <server ip> <lease time>
*/
static int
wl_ext_dhcpc_dump(struct net_device *dev, char *data, char *command,
	int total_len)
{
	int ret = 0;
	int bytes_written = 0;
	uint32 ip_addr;
	char buf[20]="";

	if (!data) {
		ret = wl_ext_iovar_getint(dev, "dhcpc_ip_addr", &ip_addr);
		if (!ret) {
			bcm_ip_ntoa((struct ipv4_addr *)&ip_addr, buf);
			bytes_written += snprintf(command+bytes_written, total_len,
				"ipaddr %s ", buf);
		}

		ret = wl_ext_iovar_getint(dev, "dhcpc_ip_mask", &ip_addr);
		if (!ret) {
			bcm_ip_ntoa((struct ipv4_addr *)&ip_addr, buf);
			bytes_written += snprintf(command+bytes_written, total_len,
				"mask %s ", buf);
		}

		ret = wl_ext_iovar_getint(dev, "dhcpc_ip_gateway", &ip_addr);
		if (!ret) {
			bcm_ip_ntoa((struct ipv4_addr *)&ip_addr, buf);
			bytes_written += snprintf(command+bytes_written, total_len,
				"gw %s ", buf);
		}

		ret = wl_ext_iovar_getint(dev, "dhcpc_ip_dnsserv", &ip_addr);
		if (!ret) {
			bcm_ip_ntoa((struct ipv4_addr *)&ip_addr, buf);
			bytes_written += snprintf(command+bytes_written, total_len,
				"dnsserv %s ", buf);
		}

		if (!bytes_written)
			bytes_written = -1;

		AEXT_TRACE(dev->name, "command result is %s\n", command);
	}

	return bytes_written;
}

int
wl_ext_dhcpc_param(struct net_device *dev, char *data, char *command,
	int total_len)
{
	int ret = -1, bytes_written = 0;
	char ip_addr_str[20]="", ip_serv_str[20]="";
	struct dhcpc_parameter dhcpc_param;
	uint32 ip_addr, ip_serv, lease_time;
	char iovar_buf[WLC_IOCTL_SMLEN]="\0";

	if (data) {
		AEXT_TRACE(dev->name, "cmd %s", command);
		sscanf(data, "%s %s %d", ip_addr_str, ip_serv_str, &lease_time);
		AEXT_TRACE(dev->name, "ip_addr = %s, ip_serv = %s, lease_time = %d",
			ip_addr_str, ip_serv_str, lease_time);

		memset(&dhcpc_param, 0, sizeof(struct dhcpc_parameter));
		if (!bcm_atoipv4(ip_addr_str, (struct ipv4_addr *)&ip_addr)) {
			AEXT_ERROR(dev->name, "wrong ip_addr_str %s\n", ip_addr_str);
			ret = -1;
			goto exit;
		}
		dhcpc_param.ip_addr = ip_addr;

		if (!bcm_atoipv4(ip_addr_str, (struct ipv4_addr *)&ip_serv)) {
			AEXT_ERROR(dev->name, "wrong ip_addr_str %s\n", ip_addr_str);
			ret = -1;
			goto exit;
		}
		dhcpc_param.ip_serv = ip_serv;
		dhcpc_param.lease_time = lease_time;
		ret = wl_ext_iovar_setbuf(dev, "dhcpc_param", &dhcpc_param,
			sizeof(struct dhcpc_parameter), iovar_buf, sizeof(iovar_buf), NULL);
	} else {
		ret = wl_ext_iovar_getbuf(dev, "dhcpc_param", &dhcpc_param,
			sizeof(struct dhcpc_parameter), iovar_buf, WLC_IOCTL_SMLEN, NULL);
		if (!ret) {
			bcm_ip_ntoa((struct ipv4_addr *)&dhcpc_param.ip_addr, ip_addr_str);
			bytes_written += snprintf(command + bytes_written, total_len,
				"ip_addr %s\n", ip_addr_str);
			bcm_ip_ntoa((struct ipv4_addr *)&dhcpc_param.ip_serv, ip_serv_str);
			bytes_written += snprintf(command + bytes_written, total_len,
				"ip_serv %s\n", ip_serv_str);
			bytes_written += snprintf(command + bytes_written, total_len,
				"lease_time %d\n", dhcpc_param.lease_time);
			AEXT_TRACE(dev->name, "command result is %s\n", command);
			ret = bytes_written;
		}
	}

	exit:
		return ret;
}
#endif /* IDHCP */

int
wl_ext_mkeep_alive(struct net_device *dev, char *data, char *command,
	int total_len)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	wl_mkeep_alive_pkt_t *mkeep_alive_pktp;
	int ret = -1, i, ifidx, id, period=-1;
	char *packet = NULL, *buf = NULL;
	int bytes_written = 0;

	if (data) {
		buf = kmalloc(total_len, GFP_KERNEL);
		if (buf == NULL) {
			AEXT_ERROR(dev->name, "Failed to allocate buffer of %d bytes\n", WLC_IOCTL_SMLEN);
			goto exit;
		}
		packet = kmalloc(WLC_IOCTL_SMLEN, GFP_KERNEL);
		if (packet == NULL) {
			AEXT_ERROR(dev->name, "Failed to allocate buffer of %d bytes\n", WLC_IOCTL_SMLEN);
			goto exit;
		}
		AEXT_TRACE(dev->name, "cmd %s", command);
		sscanf(data, "%d %d %s", &id, &period, packet);
		AEXT_TRACE(dev->name, "id=%d, period=%d, packet=%s", id, period, packet);
		if (period >= 0) {
			ifidx = dhd_net2idx(dhd->info, dev);
			ret = dhd_conf_mkeep_alive(dhd, ifidx, id, period, packet, FALSE);
		} else {
			if (id < 0)
				id = 0;
			ret = wl_ext_iovar_getbuf(dev, "mkeep_alive", &id, sizeof(id), buf,
				total_len, NULL);
			if (!ret) {
				mkeep_alive_pktp = (wl_mkeep_alive_pkt_t *) buf;
				bytes_written += snprintf(command+bytes_written, total_len,
					"Id            :%d\n"
					"Period (msec) :%d\n"
					"Length        :%d\n"
					"Packet        :0x",
					mkeep_alive_pktp->keep_alive_id,
					dtoh32(mkeep_alive_pktp->period_msec),
					dtoh16(mkeep_alive_pktp->len_bytes));
				for (i=0; i<mkeep_alive_pktp->len_bytes; i++) {
					bytes_written += snprintf(command+bytes_written, total_len,
						"%02x", mkeep_alive_pktp->data[i]);
				}
				AEXT_TRACE(dev->name, "command result is %s\n", command);
				ret = bytes_written;
			}
		}
	}

exit:
	if (buf)
		kfree(buf);
	if (packet)
		kfree(packet);
	return ret;
}

#ifdef WL_EXT_TCPKA
static int
wl_ext_tcpka_conn_add(struct net_device *dev, char *data, char *command,
	int total_len)
{
	int ret = 0;
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	tcpka_conn_t *tcpka = NULL;
	uint32 sess_id = 0, ipid = 0, srcport = 0, dstport = 0, seq = 0, ack = 0,
		tcpwin = 0, tsval = 0, tsecr = 0, len = 0, ka_payload_len = 0;
	char dst_mac[ETHER_ADDR_STR_LEN], src_ip[IPV4_ADDR_STR_LEN],
		dst_ip[IPV4_ADDR_STR_LEN], ka_payload[32];

	if (data) {
		memset(dst_mac, 0, sizeof(dst_mac));
		memset(src_ip, 0, sizeof(src_ip));
		memset(dst_ip, 0, sizeof(dst_ip));
		memset(ka_payload, 0, sizeof(ka_payload));
		sscanf(data, "%d %s %s %s %d %d %d %u %u %d %u %u %u %u %32s",
			&sess_id, dst_mac, src_ip, dst_ip, &ipid, &srcport, &dstport, &seq,
			&ack, &tcpwin, &tsval, &tsecr, &len, &ka_payload_len, ka_payload);

		tcpka = kmalloc(sizeof(struct tcpka_conn) + ka_payload_len, GFP_KERNEL);
		if (tcpka == NULL) {
			AEXT_ERROR(dev->name, "Failed to allocate buffer of %d bytes\n",
				sizeof(struct tcpka_conn) + ka_payload_len);
			goto exit;
		}
		memset(tcpka, 0, sizeof(struct tcpka_conn) + ka_payload_len);

		tcpka->sess_id = sess_id;
		if (!(ret = bcm_ether_atoe(dst_mac, &tcpka->dst_mac))) {
			AEXT_ERROR(dev->name, "mac parsing err addr=%s\n", dst_mac);
			goto exit;
		}
		if (!bcm_atoipv4(src_ip, &tcpka->src_ip)) {
			AEXT_ERROR(dev->name, "src_ip parsing err ip=%s\n", src_ip);
			goto exit;
		}
		if (!bcm_atoipv4(dst_ip, &tcpka->dst_ip)) {
			AEXT_ERROR(dev->name, "dst_ip parsing err ip=%s\n", dst_ip);
			goto exit;
		}
		tcpka->ipid = ipid;
		tcpka->srcport = srcport;
		tcpka->dstport = dstport;
		tcpka->seq = seq;
		tcpka->ack = ack;
		tcpka->tcpwin = tcpwin;
		tcpka->tsval = tsval;
		tcpka->tsecr = tsecr;
		tcpka->len = len;
		tcpka->ka_payload_len = ka_payload_len;
		strncpy(tcpka->ka_payload, ka_payload, ka_payload_len);

		AEXT_INFO(dev->name,
			"tcpka_conn_add %d %pM %pM %pM %d %d %d %u %u %d %u %u %u %u \"%s\"\n",
			tcpka->sess_id, &tcpka->dst_mac, &tcpka->src_ip, &tcpka->dst_ip,
			tcpka->ipid, tcpka->srcport, tcpka->dstport, tcpka->seq,
			tcpka->ack, tcpka->tcpwin, tcpka->tsval, tcpka->tsecr,
			tcpka->len, tcpka->ka_payload_len, tcpka->ka_payload);

		ret = wl_ext_iovar_setbuf(dev, "tcpka_conn_add", (char *)tcpka,
			(sizeof(tcpka_conn_t) + tcpka->ka_payload_len - 1),
			iovar_buf, sizeof(iovar_buf), NULL);
	}

exit:
	if (tcpka)
		kfree(tcpka);
	return ret;
}

static int
wl_ext_tcpka_conn_enable(struct net_device *dev, char *data, char *command,
	int total_len)
{
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	tcpka_conn_sess_t tcpka_conn;
	int ret;
	uint32 sess_id = 0, flag, interval = 0, retry_interval = 0, retry_count = 0;

	if (data) {
		sscanf(data, "%d %d %d %d %d",
			&sess_id, &flag, &interval, &retry_interval, &retry_count);
		tcpka_conn.sess_id = sess_id;
		tcpka_conn.flag = flag;
		if (tcpka_conn.flag) {
			tcpka_conn.tcpka_timers.interval = interval;
			tcpka_conn.tcpka_timers.retry_interval = retry_interval;
			tcpka_conn.tcpka_timers.retry_count = retry_count;
		} else {
			tcpka_conn.tcpka_timers.interval = 0;
			tcpka_conn.tcpka_timers.retry_interval = 0;
			tcpka_conn.tcpka_timers.retry_count = 0;
		}

		AEXT_INFO(dev->name, "tcpka_conn_enable %d %d %d %d %d\n",
			tcpka_conn.sess_id, tcpka_conn.flag,
			tcpka_conn.tcpka_timers.interval,
			tcpka_conn.tcpka_timers.retry_interval,
			tcpka_conn.tcpka_timers.retry_count);

		ret = wl_ext_iovar_setbuf(dev, "tcpka_conn_enable", (char *)&tcpka_conn,
			sizeof(tcpka_conn_sess_t), iovar_buf, sizeof(iovar_buf), NULL);
	}

	return ret;
}

static int
wl_ext_tcpka_conn_info(struct net_device *dev, char *data, char *command,
	int total_len)
{
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	tcpka_conn_sess_info_t *info = NULL;
	uint32 sess_id = 0;
	int ret = 0;

	if (data) {
		sscanf(data, "%d", &sess_id);
		AEXT_INFO(dev->name, "tcpka_conn_sess_info %d\n", sess_id);
		ret = wl_ext_iovar_getbuf(dev, "tcpka_conn_sess_info", (char *)&sess_id,
			sizeof(uint32), iovar_buf, sizeof(iovar_buf), NULL);
		if (!ret) {
			info = (tcpka_conn_sess_info_t *) iovar_buf;
			ret = snprintf(command, total_len, "id=%d, ipid=%d, seq=%u, ack=%u",
				sess_id, info->ipid, info->seq, info->ack);
			AEXT_INFO(dev->name, "%s\n", command);
		}
	}

	return ret;
}
#endif /* WL_EXT_TCPKA */

static int
wl_ext_rsdb_mode(struct net_device *dev, char *data, char *command,
	int total_len)
{
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	wl_config_t rsdb_mode_cfg = {1, 0}, *rsdb_p;
	int ret = 0;

	if (data) {
		rsdb_mode_cfg.config = (int)simple_strtol(data, NULL, 0);
		ret = wl_ext_iovar_setbuf(dev, "rsdb_mode", (char *)&rsdb_mode_cfg,
			sizeof(rsdb_mode_cfg), iovar_buf, WLC_IOCTL_SMLEN, NULL);
		AEXT_INFO(dev->name, "rsdb_mode %d\n", rsdb_mode_cfg.config);
	} else {
		ret = wl_ext_iovar_getbuf(dev, "rsdb_mode", NULL, 0,
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		if (!ret) {
			rsdb_p = (wl_config_t *) iovar_buf;
			ret = snprintf(command, total_len, "%d", rsdb_p->config);
			AEXT_TRACE(dev->name, "command result is %s\n", command);
		}
	}

	return ret;
}

static int
wl_ext_recal(struct net_device *dev, char *data, char *command,
	int total_len)
{
	int ret = 0, i, nchan, nssid = 0;
	int params_size = WL_SCAN_PARAMS_FIXED_SIZE + WL_NUMCHANNELS * sizeof(uint16);
	wl_scan_params_t *params = NULL;
	int ioctl_ver;
	char *p;

	AEXT_TRACE(dev->name, "Enter\n");

	if (data) {
		params_size += WL_SCAN_PARAMS_SSID_MAX * sizeof(wlc_ssid_t);
		params = (wl_scan_params_t *) kzalloc(params_size, GFP_KERNEL);
		if (params == NULL) {
			ret = -ENOMEM;
			goto exit;
		}
		memset(params, 0, params_size);

		wl_ext_get_ioctl_ver(dev, &ioctl_ver);

		memcpy(&params->bssid, &ether_bcast, ETHER_ADDR_LEN);
		params->bss_type = DOT11_BSSTYPE_ANY;
		params->scan_type = 0;
		params->nprobes = -1;
		params->active_time = -1;
		params->passive_time = -1;
		params->home_time = -1;
		params->channel_num = 0;

		params->scan_type |= WL_SCANFLAGS_PASSIVE;
		nchan = 2;
		params->channel_list[0] = wf_channel2chspec(1, WL_CHANSPEC_BW_20);
		params->channel_list[1] = wf_channel2chspec(2, WL_CHANSPEC_BW_20);

		params->nprobes = htod32(params->nprobes);
		params->active_time = htod32(params->active_time);
		params->passive_time = htod32(params->passive_time);
		params->home_time = htod32(params->home_time);

		for (i = 0; i < nchan; i++) {
			wl_ext_chspec_host_to_driver(ioctl_ver, params->channel_list[i]);
		}

		p = (char*)params->channel_list + nchan * sizeof(uint16);

		params->channel_num = htod32((nssid << WL_SCAN_PARAMS_NSSID_SHIFT) |
		                             (nchan & WL_SCAN_PARAMS_COUNT_MASK));
		params_size = p - (char*)params + nssid * sizeof(wlc_ssid_t);

		AEXT_INFO(dev->name, "recal\n");
		ret = wl_ext_ioctl(dev, WLC_SCAN, params, params_size, 1);
	}

exit:
	if (params)
		kfree(params);
	return ret;
}

static s32
wl_ext_add_remove_eventmsg(struct net_device *ndev, u16 event, bool add)
{
	s8 iovbuf[WL_EVENTING_MASK_LEN + 12];
	s8 eventmask[WL_EVENTING_MASK_LEN];
	s32 err = 0;

	if (!ndev)
		return -ENODEV;

	/* Setup event_msgs */
	err = wldev_iovar_getbuf(ndev, "event_msgs", NULL, 0, iovbuf, sizeof(iovbuf), NULL);
	if (unlikely(err)) {
		AEXT_ERROR(ndev->name, "Get event_msgs error (%d)\n", err);
		goto eventmsg_out;
	}
	memcpy(eventmask, iovbuf, WL_EVENTING_MASK_LEN);
	if (add) {
		setbit(eventmask, event);
	} else {
		clrbit(eventmask, event);
	}
	err = wldev_iovar_setbuf(ndev, "event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf,
			sizeof(iovbuf), NULL);
	if (unlikely(err)) {
		AEXT_ERROR(ndev->name, "Set event_msgs error (%d)\n", err);
		goto eventmsg_out;
	}

eventmsg_out:
	return err;
}

static int
wl_ext_event_msg(struct net_device *dev, char *data,
	char *command, int total_len)
{
	s8 iovbuf[WL_EVENTING_MASK_LEN + 12];
	s8 eventmask[WL_EVENTING_MASK_LEN];
	int i, bytes_written = 0, add = -1;
	uint event;
	char *vbuf;
	bool skipzeros;

	/* dhd_priv wl event_msg [offset] [1/0, 1 for add, 0 for remove] */
	/* dhd_priv wl event_msg 40 1 */
	if (data) {
		AEXT_TRACE(dev->name, "data = %s\n", data);
		sscanf(data, "%d %d", &event, &add);
		/* Setup event_msgs */
		bytes_written = wldev_iovar_getbuf(dev, "event_msgs", NULL, 0, iovbuf,
			sizeof(iovbuf), NULL);
		if (unlikely(bytes_written)) {
			AEXT_ERROR(dev->name, "Get event_msgs error (%d)\n", bytes_written);
			goto eventmsg_out;
		}
		memcpy(eventmask, iovbuf, WL_EVENTING_MASK_LEN);
		if (add == -1) {
			if (isset(eventmask, event))
				bytes_written += snprintf(command+bytes_written, total_len, "1");
			else
				bytes_written += snprintf(command+bytes_written, total_len, "0");
			AEXT_INFO(dev->name, "%s\n", command);
			goto eventmsg_out;
		}
		bytes_written = wl_ext_add_remove_eventmsg(dev, event, add);
	}
	else {
		/* Setup event_msgs */
		bytes_written = wldev_iovar_getbuf(dev, "event_msgs", NULL, 0, iovbuf,
			sizeof(iovbuf), NULL);
		if (bytes_written) {
			AEXT_ERROR(dev->name, "Get event_msgs error (%d)\n", bytes_written);
			goto eventmsg_out;
		}
		vbuf = (char *)iovbuf;
		bytes_written += snprintf(command+bytes_written, total_len, "0x");
		for (i = (sizeof(eventmask) - 1); i >= 0; i--) {
			if (vbuf[i] || (i == 0))
				skipzeros = FALSE;
			if (skipzeros)
				continue;
			bytes_written += snprintf(command+bytes_written, total_len,
				"%02x", vbuf[i] & 0xff);
		}
		AEXT_INFO(dev->name, "%s\n", command);
	}

eventmsg_out:
	return bytes_written;
}

#ifdef PKT_FILTER_SUPPORT
extern void dhd_pktfilter_offload_set(dhd_pub_t * dhd, char *arg);
extern void dhd_pktfilter_offload_delete(dhd_pub_t *dhd, int id);
extern void dhd_pktfilter_offload_enable(dhd_pub_t * dhd, char *arg, int enable, int master_mode);
static int
wl_ext_pkt_filter_add(struct net_device *dev, char *data, char *command,
	int total_len)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	int i, filter_id, new_id = 0, cnt;
	conf_pkt_filter_add_t *filter_add = &dhd->conf->pkt_filter_add;
	char **pktfilter = dhd->pktfilter;
	int err = 0;

	if (data) {
		AEXT_TRACE(dev->name, "data = %s\n", data);

		new_id = simple_strtol(data, NULL, 10);
		if (new_id <= 0) {
			AEXT_ERROR(dev->name, "wrong id %d\n", new_id);
			return -1;
		}

		cnt = dhd->pktfilter_count;
		for (i=0; i<cnt; i++) {
			if (!pktfilter[i])
				continue;
			filter_id = simple_strtol(pktfilter[i], NULL, 10);
			if (new_id == filter_id) {
				AEXT_ERROR(dev->name, "filter id %d already in list\n", filter_id);
				return -1;
			}
		}

		cnt = filter_add->count;
		if (cnt >= DHD_CONF_FILTER_MAX) {
			AEXT_ERROR(dev->name, "not enough filter\n");
			return -1;
		}
		for (i=0; i<cnt; i++) {
			filter_id = simple_strtol(filter_add->filter[i], NULL, 10);
			if (new_id == filter_id) {
				AEXT_ERROR(dev->name, "filter id %d already in list\n", filter_id);
				return -1;
			}
		}

		strcpy(&filter_add->filter[cnt][0], data);
		dhd->pktfilter[dhd->pktfilter_count] = filter_add->filter[cnt];
		filter_add->count++;
		dhd->pktfilter_count++;

		dhd_pktfilter_offload_set(dhd, data);
		AEXT_INFO(dev->name, "filter id %d added\n", new_id);
	}

	return err;
}

static int
wl_ext_pkt_filter_delete(struct net_device *dev, char *data, char *command,
	int total_len)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	int i, j, filter_id, cnt;
	char **pktfilter = dhd->pktfilter;
	conf_pkt_filter_add_t *filter_add = &dhd->conf->pkt_filter_add;
	bool in_filter = FALSE;
	int id, err = 0;

	if (data) {
		AEXT_TRACE(dev->name, "data = %s\n", data);
		id = (int)simple_strtol(data, NULL, 0);

		cnt = filter_add->count;
		for (i=0; i<cnt; i++) {
			filter_id = simple_strtol(filter_add->filter[i], NULL, 10);
			if (id == filter_id) {
				in_filter = TRUE;
				memset(filter_add->filter[i], 0, PKT_FILTER_LEN);
				for (j=i; j<(cnt-1); j++) {
					strcpy(filter_add->filter[j], filter_add->filter[j+1]);
					memset(filter_add->filter[j+1], 0, PKT_FILTER_LEN);
				}
				cnt--;
				filter_add->count--;
				dhd->pktfilter_count--;
			}
		}

		cnt = dhd->pktfilter_count;
		for (i=0; i<cnt; i++) {
			if (!pktfilter[i])
				continue;
			filter_id = simple_strtol(pktfilter[i], NULL, 10);
			if (id == filter_id) {
				in_filter = TRUE;
				memset(pktfilter[i], 0, strlen(pktfilter[i]));
			}
		}

		if (in_filter) {
			dhd_pktfilter_offload_delete(dhd, id);
			AEXT_INFO(dev->name, "filter id %d deleted\n", id);
		} else {
			AEXT_ERROR(dev->name, "filter id %d not in list\n", id);
			err = -1;
		}
	}

	return err;
}

static int
wl_ext_pkt_filter_enable(struct net_device *dev, char *data, char *command,
	int total_len)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	int err = 0, id, enable;
	int i, filter_id, cnt;
	char **pktfilter = dhd->pktfilter;
	bool in_filter = FALSE;

	/* dhd_priv wl pkt_filter_enable [id] [1/0] */
	/* dhd_priv wl pkt_filter_enable 141 1 */
	if (data) {
		sscanf(data, "%d %d", &id, &enable);

		cnt = dhd->pktfilter_count;
		for (i=0; i<cnt; i++) {
			if (!pktfilter[i])
				continue;
			filter_id = simple_strtol(pktfilter[i], NULL, 10);
			if (id == filter_id) {
				in_filter = TRUE;
				break;
			}
		}

		if (in_filter) {
			dhd_pktfilter_offload_enable(dhd, dhd->pktfilter[i],
				enable, dhd_master_mode);
			AEXT_INFO(dev->name, "filter id %d %s\n", id, enable?"enabled":"disabled");
		} else {
			AEXT_ERROR(dev->name, "filter id %d not in list\n", id);
			err = -1;
		}
	}

	return err;
}
#endif /* PKT_FILTER_SUPPORT */

#ifdef SENDPROB
static int
wl_ext_send_probreq(struct net_device *dev, char *data, char *command,
	int total_len)
{
	int err = 0;
	char addr_str[16], addr[6];
	char iovar_buf[WLC_IOCTL_SMLEN]="\0";
	char ie_data[WLC_IOCTL_SMLEN] = "\0";
	wl_probe_params_t params;

	/* dhd_priv wl send_probreq [dest. addr] [OUI+VAL] */
	/* dhd_priv wl send_probreq 0x00904c010203 0x00904c01020304050607 */
	if (data) {
		AEXT_TRACE(dev->name, "data = %s\n", data);
		sscanf(data, "%s %s", addr_str, ie_data);
		AEXT_TRACE(dev->name, "addr=%s, ie=%s\n", addr_str, ie_data);

		if (strlen(addr_str) != 14) {
			AEXT_ERROR(dev->name, "wrong addr %s\n", addr_str);
			goto exit;
		}
		wl_pattern_atoh(addr_str, (char *) addr);
		memset(&params, 0, sizeof(params));
		memcpy(&params.bssid, addr, ETHER_ADDR_LEN);
		memcpy(&params.mac, addr, ETHER_ADDR_LEN);

		err = wl_ext_add_del_ie(dev, VNDR_IE_PRBREQ_FLAG, ie_data, "add");
		if (err)
			goto exit;
		err = wl_ext_iovar_setbuf(dev, "sendprb", (char *)&params, sizeof(params),
			iovar_buf, sizeof(iovar_buf), NULL);
		OSL_SLEEP(100);
		wl_ext_add_del_ie(dev, VNDR_IE_PRBREQ_FLAG, ie_data, "del");
	}

exit:
    return err;
}

static int
wl_ext_send_probresp(struct net_device *dev, char *data, char *command,
	int total_len)
{
	int err = 0;
	char addr_str[16], addr[6];
	char iovar_buf[WLC_IOCTL_SMLEN]="\0";
	char ie_data[WLC_IOCTL_SMLEN] = "\0";

	/* dhd_priv wl send_probresp [dest. addr] [OUI+VAL] */
	/* dhd_priv wl send_probresp 0x00904c010203 0x00904c01020304050607 */
	if (data) {
		AEXT_TRACE(dev->name, "data = %s\n", data);
		sscanf(data, "%s %s", addr_str, ie_data);
		AEXT_TRACE(dev->name, "addr=%s, ie=%s\n", addr_str, ie_data);

		if (strlen(addr_str) != 14) {
			AEXT_ERROR(dev->name, "wrong addr %s\n", addr_str);
			goto exit;
		}
		wl_pattern_atoh(addr_str, (char *) addr);

		err = wl_ext_add_del_ie(dev, VNDR_IE_PRBRSP_FLAG, ie_data, "add");
		if (err)
			goto exit;
		err = wl_ext_iovar_setbuf(dev, "send_probresp", addr, sizeof(addr),
			iovar_buf, sizeof(iovar_buf), NULL);
		OSL_SLEEP(100);
		wl_ext_add_del_ie(dev, VNDR_IE_PRBRSP_FLAG, ie_data, "del");
	}

exit:
    return err;
}

static int
wl_ext_recv_probreq(struct net_device *dev, char *data, char *command,
	int total_len)
{
	int err = 0, enable = 0;
	char cmd[32];
	struct dhd_pub *dhd = dhd_get_pub(dev);

	/* enable:
	    1. dhd_priv wl 86 0
	    2. dhd_priv wl event_msg 44 1
	    disable:
	    1. dhd_priv wl 86 2;
	    2. dhd_priv wl event_msg 44 0
	*/
	if (data) {
		AEXT_TRACE(dev->name, "data = %s\n", data);
		sscanf(data, "%d", &enable);
		if (enable) {
			strcpy(cmd, "wl 86 0");
			err = wl_ext_wl_iovar(dev, cmd, total_len);
			if (err)
				goto exit;
			strcpy(cmd, "wl event_msg 44 1");
			err = wl_ext_wl_iovar(dev, cmd, total_len);
			if (err)
				goto exit;
			dhd->recv_probereq = TRUE;
		} else {
			if (dhd->conf->pm)
				strcpy(cmd, "wl 86 2"); {
				wl_ext_wl_iovar(dev, cmd, total_len);
			}
			strcpy(cmd, "wl event_msg 44 0");
			wl_ext_wl_iovar(dev, cmd, total_len);
			dhd->recv_probereq = FALSE;
		}
	}

exit:
    return err;
}

static int
wl_ext_recv_probresp(struct net_device *dev, char *data, char *command,
	int total_len)
{
	int err = 0, enable = 0;
	char cmd[32];

	/* enable:
	    1. dhd_priv wl pkt_filter_add 150 0 0 0 0xFF 0x50
	    2. dhd_priv wl pkt_filter_enable 150 1 
	    3. dhd_priv wl mpc 0
	    4. dhd_priv wl 108 1
	    disable:
	    1. dhd_priv wl 108 0
	    2. dhd_priv wl mpc 1
	    3. dhd_priv wl pkt_filter_disable 150 0
	    4. dhd_priv pkt_filter_delete 150
	*/
	if (data) {
		AEXT_TRACE(dev->name, "data = %s\n", data);
		sscanf(data, "%d", &enable);
		if (enable) {
			strcpy(cmd, "wl pkt_filter_add 150 0 0 0 0xFF 0x50");
			err = wl_ext_wl_iovar(dev, cmd, total_len);
			if (err)
				goto exit;
			strcpy(cmd, "wl pkt_filter_enable 150 1");
			err = wl_ext_wl_iovar(dev, cmd, total_len);
			if (err)
				goto exit;
			strcpy(cmd, "wl mpc 0");
			err = wl_ext_wl_iovar(dev, cmd, total_len);
			if (err)
				goto exit;
			strcpy(cmd, "wl 108 1");
			err= wl_ext_wl_iovar(dev, cmd, total_len);
		} else {
			strcpy(cmd, "wl 108 0");
			wl_ext_wl_iovar(dev, cmd, total_len);
			strcpy(cmd, "wl mpc 1");
			wl_ext_wl_iovar(dev, cmd, total_len);
			strcpy(cmd, "wl pkt_filter_enable 150 0");
			wl_ext_wl_iovar(dev, cmd, total_len);
			strcpy(cmd, "wl pkt_filter_delete 150");
			wl_ext_wl_iovar(dev, cmd, total_len);
		}
	}

exit:
    return err;
}
#endif /* SENDPROB */

static int
wl_ext_gtk_key_info(struct net_device *dev, char *data, char *command, int total_len)
{
	int err = 0;
	char iovar_buf[WLC_IOCTL_SMLEN]="\0";
	gtk_keyinfo_t keyinfo;
	bcol_gtk_para_t bcol_keyinfo;

	/* wl gtk_key_info [kck kek replay_ctr] */
	/* wl gtk_key_info 001122..FF001122..FF00000000000001 */
	if (data) {
		memset(&keyinfo, 0, sizeof(keyinfo));
		memcpy(&keyinfo, data, RSN_KCK_LENGTH+RSN_KEK_LENGTH+RSN_REPLAY_LEN);
		if (android_msg_level & ANDROID_INFO_LEVEL) {
			prhex("kck", (uchar *)keyinfo.KCK, RSN_KCK_LENGTH);
			prhex("kek", (uchar *)keyinfo.KEK, RSN_KEK_LENGTH);
			prhex("replay_ctr", (uchar *)keyinfo.ReplayCounter, RSN_REPLAY_LEN);
		}

		memset(&bcol_keyinfo, 0, sizeof(bcol_keyinfo));
		bcol_keyinfo.enable = 1;
		bcol_keyinfo.ptk_len = 64;
		memcpy(&bcol_keyinfo.ptk, data, RSN_KCK_LENGTH+RSN_KEK_LENGTH);
		err = wl_ext_iovar_setbuf(dev, "bcol_gtk_rekey_ptk", &bcol_keyinfo,
			sizeof(bcol_keyinfo), iovar_buf, sizeof(iovar_buf), NULL);
		if (!err) {
			goto exit;
		}

		err = wl_ext_iovar_setbuf(dev, "gtk_key_info", &keyinfo, sizeof(keyinfo),
			iovar_buf, sizeof(iovar_buf), NULL);
		if (err) {
			AEXT_ERROR(dev->name, "failed to set gtk_key_info\n");
			goto exit;
		}
	}

exit:
    return err;
}

#ifdef WL_EXT_WOWL
static int
wl_ext_wowl_pattern(struct net_device *dev, char *data, char *command,
	int total_len)
{
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	uint buf_len = 0;
	int	offset;
	char mask[128]="\0", pattern[128]="\0", add[4]="\0",
		mask_tmp[128], *pmask_tmp;
	uint32 masksize, patternsize, pad_len = 0;
	wl_wowl_pattern2_t *wowl_pattern2 = NULL;
	wl_wowl_pattern_t *wowl_pattern = NULL;
	char *mask_and_pattern;
	wl_wowl_pattern_list_t *list;
	uint8 *ptr;
	int ret = 0, i, j, v;

	if (data) {
		sscanf(data, "%s %d %s %s", add, &offset, mask_tmp, pattern);
		if (strcmp(add, "add") != 0 && strcmp(add, "clr") != 0) {
			AEXT_ERROR(dev->name, "first arg should be add or clr\n");
			goto exit;
		}
		if (!strcmp(add, "clr")) {
			AEXT_INFO(dev->name, "wowl_pattern clr\n");
			ret = wl_ext_iovar_setbuf(dev, "wowl_pattern", add,
				sizeof(add), iovar_buf, sizeof(iovar_buf), NULL);
			goto exit;
		}
		masksize = strlen(mask_tmp) -2;
		AEXT_TRACE(dev->name, "0 mask_tmp=%s, masksize=%d\n", mask_tmp, masksize);

		// add pading
		if (masksize % 16)
			pad_len = (16 - masksize % 16);
		for (i=0; i<pad_len; i++)
			strcat(mask_tmp, "0");
		masksize += pad_len;
		AEXT_TRACE(dev->name, "1 mask_tmp=%s, masksize=%d\n", mask_tmp, masksize);

		// translate 0x00 to 0, others to 1
		j = 0;
		pmask_tmp = &mask_tmp[2];
		for (i=0; i<masksize/2; i++) {
			if(strncmp(&pmask_tmp[i*2], "00", 2))
				pmask_tmp[j] = '1';
			else
				pmask_tmp[j] = '0';
			j++;
		}
		pmask_tmp[j] = '\0';
		masksize = masksize / 2;
		AEXT_TRACE(dev->name, "2 mask_tmp=%s, masksize=%d\n", mask_tmp, masksize);

		// reorder per 8bits
		pmask_tmp = &mask_tmp[2];
		for (i=0; i<masksize/8; i++) {
			char c;
			for (j=0; j<4; j++) {
				c = pmask_tmp[i*8+j];
				pmask_tmp[i*8+j] = pmask_tmp[(i+1)*8-j-1];
				pmask_tmp[(i+1)*8-j-1] = c;
			}
		}
		AEXT_TRACE(dev->name, "3 mask_tmp=%s, masksize=%d\n", mask_tmp, masksize);

		// translate 8bits to 1byte
		j = 0; v = 0;
		pmask_tmp = &mask_tmp[2];
		strcpy(mask, "0x");
		for (i=0; i<masksize; i++) {
			v = (v<<1) | (pmask_tmp[i]=='1');
			if (((i+1)%4) == 0) {
				if (v < 10)
					mask[j+2] = v + '0';
				else
					mask[j+2] = (v-10) + 'a';
				j++;
				v = 0;
			}
		}
		mask[j+2] = '\0';
		masksize = j/2;
		AEXT_TRACE(dev->name, "4 mask=%s, masksize=%d\n", mask, masksize);

		patternsize = (strlen(pattern)-2)/2;
		buf_len = sizeof(wl_wowl_pattern2_t) + patternsize + masksize;
		wowl_pattern2 = kmalloc(buf_len, GFP_KERNEL);
		if (wowl_pattern2 == NULL) {
			AEXT_ERROR(dev->name, "Failed to allocate buffer of %d bytes\n", buf_len);
			goto exit;
		}
		memset(wowl_pattern2, 0, sizeof(wl_wowl_pattern2_t));

		strncpy(wowl_pattern2->cmd, add, sizeof(add));
		wowl_pattern2->wowl_pattern.type = 0;
		wowl_pattern2->wowl_pattern.offset = offset;
		mask_and_pattern = (char*)wowl_pattern2 + sizeof(wl_wowl_pattern2_t);

		wowl_pattern2->wowl_pattern.masksize = masksize;
		ret = wl_pattern_atoh(mask, mask_and_pattern);
		if (ret == -1) {
			AEXT_ERROR(dev->name, "rejecting mask=%s\n", mask);
			goto exit;
		}

		mask_and_pattern += wowl_pattern2->wowl_pattern.masksize;
		wowl_pattern2->wowl_pattern.patternoffset = sizeof(wl_wowl_pattern_t) +
			wowl_pattern2->wowl_pattern.masksize;

		wowl_pattern2->wowl_pattern.patternsize = patternsize;
		ret = wl_pattern_atoh(pattern, mask_and_pattern);
		if (ret == -1) {
			AEXT_ERROR(dev->name, "rejecting pattern=%s\n", pattern);
			goto exit;
		}

		AEXT_INFO(dev->name, "%s %d %s %s\n", add, offset, mask, pattern);

		ret = wl_ext_iovar_setbuf(dev, "wowl_pattern", (char *)wowl_pattern2,
			buf_len, iovar_buf, sizeof(iovar_buf), NULL);
	}
	else {
		ret = wl_ext_iovar_getbuf(dev, "wowl_pattern", NULL, 0,
			iovar_buf, sizeof(iovar_buf), NULL);
		if (!ret) {
			list = (wl_wowl_pattern_list_t *)iovar_buf;
			ret = snprintf(command, total_len, "#of patterns :%d\n", list->count);
			ptr = (uint8 *)list->pattern;
			for (i=0; i<list->count; i++) {
				uint8 *pattern;
				wowl_pattern = (wl_wowl_pattern_t *)ptr;
				ret += snprintf(command+ret, total_len,
					"Pattern %d:\n"
					"ID         :0x%x\n"
					"Offset     :%d\n"
					"Masksize   :%d\n"
					"Mask       :0x",
					i+1, (uint32)wowl_pattern->id, wowl_pattern->offset,
					wowl_pattern->masksize);
				pattern = ((uint8 *)wowl_pattern + sizeof(wl_wowl_pattern_t));
				for (j = 0; j < wowl_pattern->masksize; j++) {
					ret += snprintf(command+ret, total_len, "%02x", pattern[j]);
				}
				ret += snprintf(command+ret, total_len, "\n");
				ret += snprintf(command+ret, total_len,
					"PatternSize:%d\n"
					"Pattern    :0x",
					wowl_pattern->patternsize);

				pattern = ((uint8*)wowl_pattern + wowl_pattern->patternoffset);
				for (j=0; j<wowl_pattern->patternsize; j++)
					ret += snprintf(command+ret, total_len, "%02x", pattern[j]);
				ret += snprintf(command+ret, total_len, "\n");
				ptr += (wowl_pattern->masksize + wowl_pattern->patternsize +
				        sizeof(wl_wowl_pattern_t));
			}

			AEXT_INFO(dev->name, "%s\n", command);
		}
	}

exit:
	if (wowl_pattern2)
		kfree(wowl_pattern2);
	return ret;
}

static int
wl_ext_wowl_wakeind(struct net_device *dev, char *data, char *command,
	int total_len)
{
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	wl_wowl_wakeind_t *wake = NULL;
	int ret = -1;
	char clr[6]="\0";

	if (data) {
		sscanf(data, "%s", clr);
		if (!strcmp(clr, "clear")) {
			AEXT_INFO(dev->name, "wowl_wakeind clear\n");
			ret = wl_ext_iovar_setbuf(dev, "wowl_wakeind", clr, sizeof(clr),
				iovar_buf, sizeof(iovar_buf), NULL);
		} else {
			AEXT_ERROR(dev->name, "first arg should be clear\n");
		}
	} else {
		ret = wl_ext_iovar_getbuf(dev, "wowl_wakeind", NULL, 0,
			iovar_buf, sizeof(iovar_buf), NULL);
		if (!ret) {
			wake = (wl_wowl_wakeind_t *) iovar_buf;
			ret = snprintf(command, total_len, "wakeind=0x%x", wake->ucode_wakeind);
			if (wake->ucode_wakeind & WL_WOWL_MAGIC)
				ret += snprintf(command+ret, total_len, " (MAGIC packet)");
			if (wake->ucode_wakeind & WL_WOWL_NET)
				ret += snprintf(command+ret, total_len, " (Netpattern)");
			if (wake->ucode_wakeind & WL_WOWL_DIS)
				ret += snprintf(command+ret, total_len, " (Disassoc/Deauth)");
			if (wake->ucode_wakeind & WL_WOWL_BCN)
				ret += snprintf(command+ret, total_len, " (Loss of beacon)");
			if (wake->ucode_wakeind & WL_WOWL_TCPKEEP_TIME)
				ret += snprintf(command+ret, total_len, " (TCPKA timeout)");
			if (wake->ucode_wakeind & WL_WOWL_TCPKEEP_DATA)
				ret += snprintf(command+ret, total_len, " (TCPKA data)");
			if (wake->ucode_wakeind & WL_WOWL_TCPFIN)
				ret += snprintf(command+ret, total_len, " (TCP FIN)");
			AEXT_INFO(dev->name, "%s\n", command);
		}
	}

	return ret;
}
#endif /* WL_EXT_WOWL */

#ifdef WL_GPIO_NOTIFY
typedef struct notify_payload {
	int index;
	int len;
	char payload[128];
} notify_payload_t;

static int
wl_ext_gpio_notify(struct net_device *dev, char *data, char *command,
	int total_len)
{
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	notify_payload_t notify, *pnotify = NULL;
	int i, ret = 0, bytes_written = 0;
	char frame_str[WLC_IOCTL_SMLEN+3];

	if (data) {
		memset(&notify, 0, sizeof(notify));
		memset(frame_str, 0, sizeof(frame_str));
		sscanf(data, "%d %s", &notify.index, frame_str);

		if (notify.index < 0)
			notify.index = 0;

		if (strlen(frame_str)) {
			notify.len = wl_pattern_atoh(frame_str, notify.payload);
			if (notify.len == -1) {
				AEXT_ERROR(dev->name, "rejecting pattern=%s\n", frame_str);
				goto exit;
			}
			AEXT_INFO(dev->name, "index=%d, len=%d\n", notify.index, notify.len);
			if (android_msg_level & ANDROID_INFO_LEVEL)
				prhex("payload", (uchar *)notify.payload, notify.len);
			ret = wl_ext_iovar_setbuf(dev, "bcol_gpio_noti", (char *)&notify,
				sizeof(notify), iovar_buf, WLC_IOCTL_SMLEN, NULL);
		} else {
			AEXT_INFO(dev->name, "index=%d\n", notify.index);
			ret = wl_ext_iovar_getbuf(dev, "bcol_gpio_noti", &notify.index,
				sizeof(notify.index), iovar_buf, sizeof(iovar_buf), NULL);
			if (!ret) {
				pnotify = (notify_payload_t *)iovar_buf;
				bytes_written += snprintf(command+bytes_written, total_len,
					"Id            :%d\n"
					"Packet        :0x",
					pnotify->index);
				for (i=0; i<pnotify->len; i++) {
					bytes_written += snprintf(command+bytes_written, total_len,
						"%02x", pnotify->payload[i]);
				}
				AEXT_TRACE(dev->name, "command result is\n%s\n", command);
				ret = bytes_written;
			}
		}
	}

exit:
	return ret;
}
#endif /* WL_GPIO_NOTIFY */

typedef int (wl_ext_tpl_parse_t)(struct net_device *dev, char *data, char *command,
	int total_len);

typedef struct wl_ext_iovar_tpl_t {
	int get;
	int set;
	char *name;
	wl_ext_tpl_parse_t *parse;
} wl_ext_iovar_tpl_t;

const wl_ext_iovar_tpl_t wl_ext_iovar_tpl_list[] = {
	{WLC_GET_VAR,	WLC_SET_VAR,	"event_msg",	wl_ext_event_msg},
	{WLC_GET_VAR,	WLC_SET_VAR,	"gtk_key_info",	wl_ext_gtk_key_info},
	{WLC_GET_VAR,	WLC_SET_VAR,	"recal",		wl_ext_recal},
	{WLC_GET_VAR,	WLC_SET_VAR,	"rsdb_mode",	wl_ext_rsdb_mode},
	{WLC_GET_VAR,	WLC_SET_VAR,	"mkeep_alive",	wl_ext_mkeep_alive},
#ifdef PKT_FILTER_SUPPORT
	{WLC_GET_VAR,	WLC_SET_VAR,	"pkt_filter_add",		wl_ext_pkt_filter_add},
	{WLC_GET_VAR,	WLC_SET_VAR,	"pkt_filter_delete",	wl_ext_pkt_filter_delete},
	{WLC_GET_VAR,	WLC_SET_VAR,	"pkt_filter_enable",	wl_ext_pkt_filter_enable},
#endif /* PKT_FILTER_SUPPORT */
#if defined(WL_EXT_IAPSTA) && defined(WLMESH)
	{WLC_GET_VAR,	WLC_SET_VAR,	"mesh_peer_status",	wl_ext_mesh_peer_status},
#endif /* WL_EXT_IAPSTA && WLMESH */
#ifdef SENDPROB
	{WLC_GET_VAR,	WLC_SET_VAR,	"send_probreq",		wl_ext_send_probreq},
	{WLC_GET_VAR,	WLC_SET_VAR,	"send_probresp",	wl_ext_send_probresp},
	{WLC_GET_VAR,	WLC_SET_VAR,	"recv_probreq",		wl_ext_recv_probreq},
	{WLC_GET_VAR,	WLC_SET_VAR,	"recv_probresp",	wl_ext_recv_probresp},
#endif /* SENDPROB */
#ifdef WL_EXT_TCPKA
	{WLC_GET_VAR,	WLC_SET_VAR,	"tcpka_conn_add",		wl_ext_tcpka_conn_add},
	{WLC_GET_VAR,	WLC_SET_VAR,	"tcpka_conn_enable",	wl_ext_tcpka_conn_enable},
	{WLC_GET_VAR,	WLC_SET_VAR,	"tcpka_conn_sess_info",	wl_ext_tcpka_conn_info},
#endif /* WL_EXT_TCPKA */
#ifdef WL_EXT_WOWL
	{WLC_GET_VAR,	WLC_SET_VAR,	"wowl_pattern",		wl_ext_wowl_pattern},
	{WLC_GET_VAR,	WLC_SET_VAR,	"wowl_wakeind",		wl_ext_wowl_wakeind},
#endif /* WL_EXT_WOWL */
#ifdef IDHCP
	{WLC_GET_VAR,	WLC_SET_VAR,	"dhcpc_dump",		wl_ext_dhcpc_dump},
	{WLC_GET_VAR,	WLC_SET_VAR,	"dhcpc_param",		wl_ext_dhcpc_param},
#endif /* IDHCP */
#ifdef WL_GPIO_NOTIFY
	{WLC_GET_VAR,	WLC_SET_VAR,	"bcol_gpio_noti",	wl_ext_gpio_notify},
#endif /* WL_GPIO_NOTIFY */
};

/*
Ex: dhd_priv wl [cmd] [val]
  dhd_priv wl 85
  dhd_priv wl 86 1
  dhd_priv wl mpc
  dhd_priv wl mpc 1
*/
static int
wl_ext_wl_iovar(struct net_device *dev, char *command, int total_len)
{
	int cmd, val, ret = -1, i;
	char name[32], *pch, *pick_tmp, *data;
	int bytes_written=-1;
	const wl_ext_iovar_tpl_t *tpl = wl_ext_iovar_tpl_list;
	int tpl_count = ARRAY_SIZE(wl_ext_iovar_tpl_list);

	AEXT_TRACE(dev->name, "cmd %s\n", command);
	pick_tmp = command;

	pch = bcmstrtok(&pick_tmp, " ", 0); // pick wl
	if (!pch || strncmp(pch, "wl", 2))
		goto exit;

	pch = bcmstrtok(&pick_tmp, " ", 0); // pick cmd
	if (!pch)
		goto exit;

	memset(name, 0 , sizeof (name));
	cmd = (int)simple_strtol(pch, NULL, 0);
	if (cmd == 0) {
		strcpy(name, pch);
	}
	data = bcmstrtok(&pick_tmp, "", 0); // pick data
	if (data && cmd == 0) {
		cmd = WLC_SET_VAR;
	} else if (cmd == 0) {
		cmd = WLC_GET_VAR;
	}

	/* look for a matching code in the table */
	for (i = 0; i < tpl_count; i++, tpl++) {
		if ((tpl->get == cmd || tpl->set == cmd) && !strcmp(tpl->name, name))
			break;
	}
	if (i < tpl_count && tpl->parse) {
		ret = tpl->parse(dev, data, command, total_len);
	} else {
		if (cmd == WLC_SET_VAR) {
			val = (int)simple_strtol(data, NULL, 0);
			AEXT_INFO(dev->name, "set %s %d\n", name, val);
			ret = wl_ext_iovar_setint(dev, name, val);
		} else if (cmd == WLC_GET_VAR) {
			AEXT_INFO(dev->name, "get %s\n", name);
			ret = wl_ext_iovar_getint(dev, name, &val);
			if (!ret) {
				bytes_written = snprintf(command, total_len, "%d", val);
				AEXT_INFO(dev->name, "command result is %s\n", command);
				ret = bytes_written;
			}
		} else if (data) {
			val = (int)simple_strtol(data, NULL, 0);
			AEXT_INFO(dev->name, "set %d %d\n", cmd, val);
			ret = wl_ext_ioctl(dev, cmd, &val, sizeof(val), TRUE);
		} else {
			AEXT_INFO(dev->name, "get %d\n", cmd);
			ret = wl_ext_ioctl(dev, cmd, &val, sizeof(val), FALSE);
			if (!ret) {
				bytes_written = snprintf(command, total_len, "%d", val);
				AEXT_INFO(dev->name, "command result is %s\n", command);
				ret = bytes_written;
			}
		}
	}

exit:
	return ret;
}

int
wl_android_ext_priv_cmd(struct net_device *net, char *command,
	int total_len, int *bytes_written)
{
	int ret = 0;

	if (strnicmp(command, CMD_CHANNELS, strlen(CMD_CHANNELS)) == 0) {
		*bytes_written = wl_ext_channels(net, command, total_len);
	}
	else if (strnicmp(command, CMD_CHANNEL, strlen(CMD_CHANNEL)) == 0) {
		*bytes_written = wl_ext_channel(net, command, total_len);
	}
	else if (strnicmp(command, CMD_ROAM_TRIGGER, strlen(CMD_ROAM_TRIGGER)) == 0) {
		*bytes_written = wl_ext_roam_trigger(net, command, total_len);
	}
	else if (strnicmp(command, CMD_PM, strlen(CMD_PM)) == 0) {
		*bytes_written = wl_ext_pm(net, command, total_len);
	}
	else if (strnicmp(command, CMD_MONITOR, strlen(CMD_MONITOR)) == 0) {
		*bytes_written = wl_ext_monitor(net, command, total_len);
	}
	else if (strnicmp(command, CMD_SET_SUSPEND_BCN_LI_DTIM, strlen(CMD_SET_SUSPEND_BCN_LI_DTIM)) == 0) {
		int bcn_li_dtim;
		bcn_li_dtim = (int)simple_strtol((command + strlen(CMD_SET_SUSPEND_BCN_LI_DTIM) + 1), NULL, 10);
		*bytes_written = net_os_set_suspend_bcn_li_dtim(net, bcn_li_dtim);
	}
#ifdef WL_EXT_IAPSTA
	else if (strnicmp(command, CMD_IAPSTA_INIT, strlen(CMD_IAPSTA_INIT)) == 0 ||
			strnicmp(command, CMD_ISAM_INIT, strlen(CMD_ISAM_INIT)) == 0) {
		*bytes_written = wl_ext_isam_init(net, command, total_len);
	}
	else if (strnicmp(command, CMD_IAPSTA_CONFIG, strlen(CMD_IAPSTA_CONFIG)) == 0 ||
			strnicmp(command, CMD_ISAM_CONFIG, strlen(CMD_ISAM_CONFIG)) == 0) {
		*bytes_written = wl_ext_iapsta_config(net, command, total_len);
	}
	else if (strnicmp(command, CMD_IAPSTA_ENABLE, strlen(CMD_IAPSTA_ENABLE)) == 0 ||
			strnicmp(command, CMD_ISAM_ENABLE, strlen(CMD_ISAM_ENABLE)) == 0) {
		*bytes_written = wl_ext_iapsta_enable(net, command, total_len);
	}
	else if (strnicmp(command, CMD_IAPSTA_DISABLE, strlen(CMD_IAPSTA_DISABLE)) == 0 ||
			strnicmp(command, CMD_ISAM_DISABLE, strlen(CMD_ISAM_DISABLE)) == 0) {
		*bytes_written = wl_ext_iapsta_disable(net, command, total_len);
	}
	else if (strnicmp(command, CMD_ISAM_STATUS, strlen(CMD_ISAM_STATUS)) == 0) {
		*bytes_written = wl_ext_isam_status(net, command, total_len);
	}
	else if (strnicmp(command, CMD_ISAM_PARAM, strlen(CMD_ISAM_PARAM)) == 0) {
		*bytes_written = wl_ext_isam_param(net, command, total_len);
	}
#endif /* WL_EXT_IAPSTA */
#ifdef WL_CFG80211
	else if (strnicmp(command, CMD_AUTOCHANNEL, strlen(CMD_AUTOCHANNEL)) == 0) {
		*bytes_written = wl_cfg80211_autochannel(net, command, total_len);
	}
#endif /* WL_CFG80211 */
#if defined(WL_WIRELESS_EXT) && defined(WL_ESCAN)
	else if (strnicmp(command, CMD_AUTOCHANNEL, strlen(CMD_AUTOCHANNEL)) == 0) {
		*bytes_written = wl_iw_autochannel(net, command, total_len);
	}
#endif /* WL_WIRELESS_EXT && WL_ESCAN */
	else if (strnicmp(command, CMD_WL, strlen(CMD_WL)) == 0) {
		*bytes_written = wl_ext_wl_iovar(net, command, total_len);
	}
	else
		ret = -1;

	return ret;
}

#if defined(WL_CFG80211) || defined(WL_ESCAN)
int
wl_ext_get_distance(struct net_device *net, u32 band)
{
	u32 bw = WL_CHANSPEC_BW_20;
	s32 bw_cap = 0, distance = 0;
	struct {
		u32 band;
		u32 bw_cap;
	} param = {0, 0};
	char buf[WLC_IOCTL_SMLEN]="\0";
	s32 err = BCME_OK;

	param.band = band;
	err = wl_ext_iovar_getbuf(net, "bw_cap", &param, sizeof(param), buf,
		sizeof(buf), NULL);
	if (err) {
		if (err != BCME_UNSUPPORTED) {
			AEXT_ERROR(net->name, "bw_cap failed, %d\n", err);
			return err;
		} else {
			err = wl_ext_iovar_getint(net, "mimo_bw_cap", &bw_cap);
			if (bw_cap != WLC_N_BW_20ALL)
				bw = WL_CHANSPEC_BW_40;
		}
	} else {
		if (WL_BW_CAP_80MHZ(buf[0]))
			bw = WL_CHANSPEC_BW_80;
		else if (WL_BW_CAP_40MHZ(buf[0]))
			bw = WL_CHANSPEC_BW_40;
		else
			bw = WL_CHANSPEC_BW_20;
	}

	if (bw == WL_CHANSPEC_BW_20)
		distance = 2;
	else if (bw == WL_CHANSPEC_BW_40)
		distance = 4;
	else if (bw == WL_CHANSPEC_BW_80)
		distance = 8;
	else
		distance = 16;
	AEXT_INFO(net->name, "bw=0x%x, distance=%d\n", bw, distance);

	return distance;
}

int
wl_ext_get_best_channel(struct net_device *net,
#if defined(BSSCACHE)
	wl_bss_cache_ctrl_t *bss_cache_ctrl,
#else
	struct wl_scan_results *bss_list,
#endif /* BSSCACHE */
	int ioctl_ver, int *best_2g_ch, int *best_5g_ch
)
{
	struct wl_bss_info *bi = NULL;	/* must be initialized */
	s32 i, j;
#if defined(BSSCACHE)
	wl_bss_cache_t *node;
#endif /* BSSCACHE */
	int b_band[CH_MAX_2G_CHANNEL]={0}, a_band1[4]={0}, a_band4[5]={0};
	s32 cen_ch, distance, distance_2g, distance_5g, ch, min_ap=999;
	u8 valid_chan_list[sizeof(u32)*(WL_NUMCHANNELS + 1)];
	wl_uint32_list_t *list;
	int ret;
	chanspec_t chanspec;
	struct dhd_pub *dhd = dhd_get_pub(net);

	memset(b_band, -1, sizeof(b_band));
	memset(a_band1, -1, sizeof(a_band1));
	memset(a_band4, -1, sizeof(a_band4));

	memset(valid_chan_list, 0, sizeof(valid_chan_list));
	list = (wl_uint32_list_t *)(void *) valid_chan_list;
	list->count = htod32(WL_NUMCHANNELS);
	ret = wl_ext_ioctl(net, WLC_GET_VALID_CHANNELS, &valid_chan_list,
		sizeof(valid_chan_list), 0);
	if (ret<0) {
		AEXT_ERROR(net->name, "get channels failed with %d\n", ret);
		return 0;
	} else {
		for (i = 0; i < dtoh32(list->count); i++) {
			ch = dtoh32(list->element[i]);
			if (!dhd_conf_match_channel(dhd, ch))
				continue;
			if (ch < CH_MAX_2G_CHANNEL)
				b_band[ch-1] = 0;
			else if (ch <= 48)
				a_band1[(ch-36)/4] = 0;
			else if (ch >= 149 && ch <= 161)
				a_band4[(ch-149)/4] = 0;
		}
	}

	distance_2g = wl_ext_get_distance(net, WLC_BAND_2G);
	distance_5g = wl_ext_get_distance(net, WLC_BAND_5G);

#if defined(BSSCACHE)
	node = bss_cache_ctrl->m_cache_head;
	for (i=0; node && i<256; i++)
#else
	for (i=0; i < bss_list->count; i++)
#endif /* BSSCACHE */
	{
#if defined(BSSCACHE)
		bi = node->results.bss_info;
#else
		bi = bi ? (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length)) : bss_list->bss_info;
#endif /* BSSCACHE */
		chanspec = wl_ext_chspec_driver_to_host(ioctl_ver, bi->chanspec);
		cen_ch = CHSPEC_CHANNEL(bi->chanspec);
		distance = 0;
		if (CHSPEC_IS20(chanspec))
			distance += 2;
		else if (CHSPEC_IS40(chanspec))
			distance += 4;
		else if (CHSPEC_IS80(chanspec))
			distance += 8;
		else
			distance += 16;

		if (CHSPEC_IS2G(chanspec)) {
			distance += distance_2g;
			for (j=0; j<ARRAYSIZE(b_band); j++) {
				if (b_band[j] >= 0 && abs(cen_ch-(1+j)) <= distance)
					b_band[j] += 1;
			}
		} else {
			distance += distance_5g;
			if (cen_ch <= 48) {
				for (j=0; j<ARRAYSIZE(a_band1); j++) {
					if (a_band1[j] >= 0 && abs(cen_ch-(36+j*4)) <= distance)
						a_band1[j] += 1;
				}
			} else if (cen_ch >= 149) {
				for (j=0; j<ARRAYSIZE(a_band4); j++) {
					if (a_band4[j] >= 0 && abs(cen_ch-(149+j*4)) <= distance)
						a_band4[j] += 1;
				}
			}
		}
#if defined(BSSCACHE)
		node = node->next;
#endif /* BSSCACHE */
	}

	*best_2g_ch = 0;
	min_ap = 999;
	for (i=0; i<CH_MAX_2G_CHANNEL; i++) {
		if(b_band[i] < min_ap && b_band[i] >= 0) {
			min_ap = b_band[i];
			*best_2g_ch = i+1;
		}
	}
	*best_5g_ch = 0;
	min_ap = 999;
	for (i=0; i<ARRAYSIZE(a_band1); i++) {
		if(a_band1[i] < min_ap && a_band1[i] >= 0) {
			min_ap = a_band1[i];
			*best_5g_ch = i*4 + 36;
		}
	}
	for (i=0; i<ARRAYSIZE(a_band4); i++) {
		if(a_band4[i] < min_ap && a_band4[i] >= 0) {
			min_ap = a_band4[i];
			*best_5g_ch = i*4 + 149;
		}
	}

	if (android_msg_level & ANDROID_INFO_LEVEL) {
		struct bcmstrbuf strbuf;
		char *tmp_buf = NULL;
		tmp_buf = kmalloc(WLC_IOCTL_SMLEN, GFP_KERNEL);
		if (tmp_buf == NULL) {
			AEXT_ERROR(net->name, "Failed to allocate buffer of %d bytes\n", WLC_IOCTL_SMLEN);
			goto exit;
		}
		bcm_binit(&strbuf, tmp_buf, WLC_IOCTL_SMLEN);
		for (j=0; j<ARRAYSIZE(b_band); j++)
			bcm_bprintf(&strbuf, "%d/%d, ", b_band[j], 1+j);
		bcm_bprintf(&strbuf, "\n");
		for (j=0; j<ARRAYSIZE(a_band1); j++)
			bcm_bprintf(&strbuf, "%d/%d, ", a_band1[j], 36+j*4);
		bcm_bprintf(&strbuf, "\n");
		for (j=0; j<ARRAYSIZE(a_band4); j++)
			bcm_bprintf(&strbuf, "%d/%d, ", a_band4[j], 149+j*4);
		bcm_bprintf(&strbuf, "\n");
		bcm_bprintf(&strbuf, "best_2g_ch=%d, best_5g_ch=%d\n",
			*best_2g_ch, *best_5g_ch);
		AEXT_INFO(net->name, "\n%s", strbuf.origbuf);
		if (tmp_buf) {
			kfree(tmp_buf);
		}
	}

exit:
	return 0;
}
#endif /* WL_CFG80211 || WL_ESCAN */

#define APCS_MAX_RETRY		10
static int
wl_ext_fw_apcs(struct net_device *dev, uint32 band)
{
	int channel = 0, chosen = 0, retry = 0, ret = 0, spect = 0;
	u8 *reqbuf = NULL;
	uint32 buf_size;

	ret = wldev_ioctl_get(dev, WLC_GET_SPECT_MANAGMENT, &spect, sizeof(spect));
	if (ret) {
		AEXT_ERROR(dev->name, "ACS: error getting the spect, ret=%d\n", ret);
		goto done;
	}

	if (spect > 0) {
		ret = wl_cfg80211_set_spect(dev, 0);
		if (ret < 0) {
			AEXT_ERROR(dev->name, "ACS: error while setting spect, ret=%d\n", ret);
			goto done;
		}
	}

	reqbuf = kmalloc(CHANSPEC_BUF_SIZE, GFP_KERNEL);
	if (reqbuf == NULL) {
		AEXT_ERROR(dev->name, "failed to allocate chanspec buffer\n");
		goto done;
	}
	memset(reqbuf, 0, CHANSPEC_BUF_SIZE);

	if (band == WLC_BAND_AUTO) {
		AEXT_INFO(dev->name, "ACS full channel scan \n");
		reqbuf[0] = htod32(0);
	} else if (band == WLC_BAND_5G) {
		AEXT_INFO(dev->name, "ACS 5G band scan \n");
		if ((ret = wl_cfg80211_get_chanspecs_5g(dev, reqbuf, CHANSPEC_BUF_SIZE)) < 0) {
			AEXT_ERROR(dev->name, "ACS 5g chanspec retreival failed! \n");
			goto done;
		}
	} else if (band == WLC_BAND_2G) {
		/*
		 * If channel argument is not provided/ argument 20 is provided,
		 * Restrict channel to 2GHz, 20MHz BW, No SB
		 */
		AEXT_INFO(dev->name, "ACS 2G band scan \n");
		if ((ret = wl_cfg80211_get_chanspecs_2g(dev, reqbuf, CHANSPEC_BUF_SIZE)) < 0) {
			AEXT_ERROR(dev->name, "ACS 2g chanspec retreival failed! \n");
			goto done;
		}
	} else {
		AEXT_ERROR(dev->name, "ACS: No band chosen\n");
		goto done;
	}

	buf_size = (band == WLC_BAND_AUTO) ? sizeof(int) : CHANSPEC_BUF_SIZE;
	ret = wldev_ioctl_set(dev, WLC_START_CHANNEL_SEL, (void *)reqbuf,
		buf_size);
	if (ret < 0) {
		AEXT_ERROR(dev->name, "can't start auto channel scan, err = %d\n", ret);
		channel = 0;
		goto done;
	}

	/* Wait for auto channel selection, max 3000 ms */
	if ((band == WLC_BAND_2G) || (band == WLC_BAND_5G)) {
		OSL_SLEEP(500);
	} else {
		/*
		 * Full channel scan at the minimum takes 1.2secs
		 * even with parallel scan. max wait time: 3500ms
		 */
		OSL_SLEEP(1000);
	}

	retry = APCS_MAX_RETRY;
	while (retry--) {
		ret = wldev_ioctl_get(dev, WLC_GET_CHANNEL_SEL, &chosen,
			sizeof(chosen));
		if (ret < 0) {
			chosen = 0;
		} else {
			chosen = dtoh32(chosen);
		}

		if (chosen) {
			int chosen_band;
			int apcs_band;
#ifdef D11AC_IOTYPES
			if (wl_cfg80211_get_ioctl_version() == 1) {
				channel = LCHSPEC_CHANNEL((chanspec_t)chosen);
			} else {
				channel = CHSPEC_CHANNEL((chanspec_t)chosen);
			}
#else
			channel = CHSPEC_CHANNEL((chanspec_t)chosen);
#endif /* D11AC_IOTYPES */
			apcs_band = (band == WLC_BAND_AUTO) ? WLC_BAND_2G : band;
			chosen_band = (channel <= CH_MAX_2G_CHANNEL) ? WLC_BAND_2G : WLC_BAND_5G;
			if (apcs_band == chosen_band) {
				WL_MSG(dev->name, "selected channel = %d\n", channel);
				break;
			}
		}
		AEXT_INFO(dev->name, "%d tried, ret = %d, chosen = 0x%x\n",
			(APCS_MAX_RETRY - retry), ret, chosen);
		OSL_SLEEP(250);
	}

done:
	if (spect > 0) {
		if ((ret = wl_cfg80211_set_spect(dev, spect) < 0)) {
			AEXT_ERROR(dev->name, "ACS: error while setting spect\n");
		}
	}

	if (reqbuf) {
		kfree(reqbuf);
	}

	return channel;
}

#ifdef WL_ESCAN
int
wl_ext_drv_apcs(struct net_device *dev, uint32 band)
{
	int ret = 0, channel = 0;
	struct dhd_pub *dhd = dhd_get_pub(dev);
	struct wl_escan_info *escan = NULL;
	int retry = 0, retry_max, retry_interval = 250, up = 1;
#ifdef WL_CFG80211
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
#endif /* WL_CFG80211 */

	escan = dhd->escan;
	if (dhd) {
		retry_max = WL_ESCAN_TIMER_INTERVAL_MS/retry_interval;
		ret = wldev_ioctl_get(dev, WLC_GET_UP, &up, sizeof(s32));
		if (ret < 0 || up == 0) {
			ret = wldev_ioctl_set(dev, WLC_UP, &up, sizeof(s32));
		}
		retry = retry_max;
		while (retry--) {
			if (escan->escan_state == ESCAN_STATE_SCANING
#ifdef WL_CFG80211
				|| wl_get_drv_status_all(cfg, SCANNING)
#endif
			)
			{
				AEXT_INFO(dev->name, "Scanning %d tried, ret = %d\n",
					(retry_max - retry), ret);
			} else {
				escan->autochannel = 1;
				ret = wl_escan_set_scan(dev, dhd, NULL, 0, TRUE);
				if (!ret)
					break;
			}
			OSL_SLEEP(retry_interval);
		}
		if ((retry == 0) || (ret < 0))
			goto done;
		retry = retry_max;
		while (retry--) {
			if (escan->escan_state == ESCAN_STATE_IDLE) {
				if (band == WLC_BAND_5G)
					channel = escan->best_5g_ch;
				else
					channel = escan->best_2g_ch;
				WL_MSG(dev->name, "selected channel = %d\n", channel);
				goto done;
			}
			AEXT_INFO(dev->name, "escan_state=%d, %d tried, ret = %d\n",
				escan->escan_state, (retry_max - retry), ret);
			OSL_SLEEP(retry_interval);
		}
		if ((retry == 0) || (ret < 0))
			goto done;
	}

done:
	if (escan)
		escan->autochannel = 0;

	return channel;
}
#endif /* WL_ESCAN */

int
wl_ext_autochannel(struct net_device *dev, uint acs, uint32 band)
{
	int ret = 0, channel = 0;
	uint16 chan_2g, chan_5g;

	AEXT_INFO(dev->name, "acs=0x%x, band=%d \n", acs, band);

	if (acs & ACS_FW_BIT) {
		ret = wldev_ioctl_get(dev, WLC_GET_CHANNEL_SEL, &channel, sizeof(channel));
		channel = 0;
		if (ret != BCME_UNSUPPORTED)
			channel = wl_ext_fw_apcs(dev, band);
		if (channel)
			return channel;
	}

#ifdef WL_ESCAN
	if (acs & ACS_DRV_BIT)
		channel = wl_ext_drv_apcs(dev, band);
#endif /* WL_ESCAN */

	if (channel == 0) {
		wl_ext_get_default_chan(dev, &chan_2g, &chan_5g, TRUE);
		if (band == WLC_BAND_5G) {
			channel = chan_5g;
		} else {
			channel = chan_2g;
		}
		AEXT_ERROR(dev->name, "ACS failed. Fall back to default channel (%d) \n", channel);
	}

	return channel;
}

#if defined(RSSIAVG)
void
wl_free_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl)
{
	wl_rssi_cache_t *node, *cur, **rssi_head;
	int i=0;

	rssi_head = &rssi_cache_ctrl->m_cache_head;
	node = *rssi_head;

	for (;node;) {
		AEXT_INFO("wlan", "Free %d with BSSID %pM\n", i, &node->BSSID);
		cur = node;
		node = cur->next;
		kfree(cur);
		i++;
	}
	*rssi_head = NULL;
}

void
wl_delete_dirty_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl)
{
	wl_rssi_cache_t *node, *prev, **rssi_head;
	int i = -1, tmp = 0;
	struct osl_timespec now;

	osl_do_gettimeofday(&now);

	rssi_head = &rssi_cache_ctrl->m_cache_head;
	node = *rssi_head;
	prev = node;
	for (;node;) {
		i++;
		if (now.tv_sec > node->tv.tv_sec) {
			if (node == *rssi_head) {
				tmp = 1;
				*rssi_head = node->next;
			} else {
				tmp = 0;
				prev->next = node->next;
			}
			AEXT_INFO("wlan", "Del %d with BSSID %pM\n", i, &node->BSSID);
			kfree(node);
			if (tmp == 1) {
				node = *rssi_head;
				prev = node;
			} else {
				node = prev->next;
			}
			continue;
		}
		prev = node;
		node = node->next;
	}
}

void
wl_delete_disconnected_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl,
	u8 *bssid)
{
	wl_rssi_cache_t *node, *prev, **rssi_head;
	int i = -1, tmp = 0;

	rssi_head = &rssi_cache_ctrl->m_cache_head;
	node = *rssi_head;
	prev = node;
	for (;node;) {
		i++;
		if (!memcmp(&node->BSSID, bssid, ETHER_ADDR_LEN)) {
			if (node == *rssi_head) {
				tmp = 1;
				*rssi_head = node->next;
			} else {
				tmp = 0;
				prev->next = node->next;
			}
			AEXT_INFO("wlan", "Del %d with BSSID %pM\n", i, &node->BSSID);
			kfree(node);
			if (tmp == 1) {
				node = *rssi_head;
				prev = node;
			} else {
				node = prev->next;
			}
			continue;
		}
		prev = node;
		node = node->next;
	}
}

void
wl_reset_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl)
{
	wl_rssi_cache_t *node, **rssi_head;

	rssi_head = &rssi_cache_ctrl->m_cache_head;

	/* reset dirty */
	node = *rssi_head;
	for (;node;) {
		node->dirty += 1;
		node = node->next;
	}
}

int
wl_update_connected_rssi_cache(struct net_device *net,
	wl_rssi_cache_ctrl_t *rssi_cache_ctrl, int *rssi_avg)
{
	wl_rssi_cache_t *node, *prev, *leaf, **rssi_head;
	int j, k=0;
	int rssi, error=0;
	struct ether_addr bssid;
	struct osl_timespec now, timeout;
	scb_val_t scbval;

	if (!g_wifi_on)
		return 0;

	error = wldev_ioctl(net, WLC_GET_BSSID, &bssid, sizeof(bssid), 0);
	if (error == BCME_NOTASSOCIATED) {
		AEXT_INFO("wlan", "Not Associated! res:%d\n", error);
		return 0;
	}
	if (error) {
		AEXT_ERROR(net->name, "Could not get bssid (%d)\n", error);
	}
	error = wldev_get_rssi(net, &scbval);
	if (error) {
		AEXT_ERROR(net->name, "Could not get rssi (%d)\n", error);
		return error;
	}
	rssi = scbval.val;

	osl_do_gettimeofday(&now);
	timeout.tv_sec = now.tv_sec + RSSICACHE_TIMEOUT;
	if (timeout.tv_sec < now.tv_sec) {
		/*
		 * Integer overflow - assume long enough timeout to be assumed
		 * to be infinite, i.e., the timeout would never happen.
		 */
		AEXT_TRACE(net->name,
			"Too long timeout (secs=%d) to ever happen - now=%lu, timeout=%lu\n",
			RSSICACHE_TIMEOUT, now.tv_sec, timeout.tv_sec);
	}

	/* update RSSI */
	rssi_head = &rssi_cache_ctrl->m_cache_head;
	node = *rssi_head;
	prev = NULL;
	for (;node;) {
		if (!memcmp(&node->BSSID, &bssid, ETHER_ADDR_LEN)) {
			AEXT_INFO("wlan", "Update %d with BSSID %pM, RSSI=%d\n", k, &bssid, rssi);
			for (j=0; j<RSSIAVG_LEN-1; j++)
				node->RSSI[j] = node->RSSI[j+1];
			node->RSSI[j] = rssi;
			node->dirty = 0;
			node->tv = timeout;
			goto exit;
		}
		prev = node;
		node = node->next;
		k++;
	}

	leaf = kmalloc(sizeof(wl_rssi_cache_t), GFP_KERNEL);
	if (!leaf) {
		AEXT_ERROR(net->name, "Memory alloc failure %d\n", (int)sizeof(wl_rssi_cache_t));
		return 0;
	}
	AEXT_INFO(net->name, "Add %d with cached BSSID %pM, RSSI=%3d in the leaf\n",
		k, &bssid, rssi);

	leaf->next = NULL;
	leaf->dirty = 0;
	leaf->tv = timeout;
	memcpy(&leaf->BSSID, &bssid, ETHER_ADDR_LEN);
	for (j=0; j<RSSIAVG_LEN; j++)
		leaf->RSSI[j] = rssi;

	if (!prev)
		*rssi_head = leaf;
	else
		prev->next = leaf;

exit:
	*rssi_avg = (int)wl_get_avg_rssi(rssi_cache_ctrl, &bssid);

	return error;
}

void
wl_update_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl,
	wl_scan_results_t *ss_list)
{
	wl_rssi_cache_t *node, *prev, *leaf, **rssi_head;
	wl_bss_info_t *bi = NULL;
	int i, j, k;
	struct osl_timespec now, timeout;

	if (!ss_list->count)
		return;

	osl_do_gettimeofday(&now);
	timeout.tv_sec = now.tv_sec + RSSICACHE_TIMEOUT;
	if (timeout.tv_sec < now.tv_sec) {
		/*
		 * Integer overflow - assume long enough timeout to be assumed
		 * to be infinite, i.e., the timeout would never happen.
		 */
		AEXT_TRACE("wlan",
			"Too long timeout (secs=%d) to ever happen - now=%lu, timeout=%lu\n",
			RSSICACHE_TIMEOUT, now.tv_sec, timeout.tv_sec);
	}

	rssi_head = &rssi_cache_ctrl->m_cache_head;

	/* update RSSI */
	for (i = 0; i < ss_list->count; i++) {
		node = *rssi_head;
		prev = NULL;
		k = 0;
		bi = bi ? (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length)) : ss_list->bss_info;
		for (;node;) {
			if (!memcmp(&node->BSSID, &bi->BSSID, ETHER_ADDR_LEN)) {
				AEXT_INFO("wlan", "Update %d with BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
					k, &bi->BSSID, dtoh16(bi->RSSI), bi->SSID);
				for (j=0; j<RSSIAVG_LEN-1; j++)
					node->RSSI[j] = node->RSSI[j+1];
				node->RSSI[j] = dtoh16(bi->RSSI);
				node->dirty = 0;
				node->tv = timeout;
				break;
			}
			prev = node;
			node = node->next;
			k++;
		}

		if (node)
			continue;

		leaf = kmalloc(sizeof(wl_rssi_cache_t), GFP_KERNEL);
		if (!leaf) {
			AEXT_ERROR("wlan", "Memory alloc failure %d\n",
				(int)sizeof(wl_rssi_cache_t));
			return;
		}
		AEXT_INFO("wlan", "Add %d with cached BSSID %pM, RSSI=%3d, SSID \"%s\" in the leaf\n",
			k, &bi->BSSID, dtoh16(bi->RSSI), bi->SSID);

		leaf->next = NULL;
		leaf->dirty = 0;
		leaf->tv = timeout;
		memcpy(&leaf->BSSID, &bi->BSSID, ETHER_ADDR_LEN);
		for (j=0; j<RSSIAVG_LEN; j++)
			leaf->RSSI[j] = dtoh16(bi->RSSI);

		if (!prev)
			*rssi_head = leaf;
		else
			prev->next = leaf;
	}
}

int16
wl_get_avg_rssi(wl_rssi_cache_ctrl_t *rssi_cache_ctrl, void *addr)
{
	wl_rssi_cache_t *node, **rssi_head;
	int j, rssi_sum, rssi=RSSI_MINVAL;

	rssi_head = &rssi_cache_ctrl->m_cache_head;

	node = *rssi_head;
	for (;node;) {
		if (!memcmp(&node->BSSID, addr, ETHER_ADDR_LEN)) {
			rssi_sum = 0;
			rssi = 0;
			for (j=0; j<RSSIAVG_LEN; j++)
				rssi_sum += node->RSSI[RSSIAVG_LEN-j-1];
			rssi = rssi_sum / j;
			break;
		}
		node = node->next;
	}
	rssi = MIN(rssi, RSSI_MAXVAL);
	if (rssi == RSSI_MINVAL) {
		AEXT_ERROR("wlan", "BSSID %pM does not in RSSI cache\n", addr);
	}
	return (int16)rssi;
}
#endif /* RSSIAVG */

#if defined(RSSIOFFSET)
int
wl_update_rssi_offset(struct net_device *net, int rssi)
{
#if defined(RSSIOFFSET_NEW)
	int j;
#endif /* RSSIOFFSET_NEW */

	if (!g_wifi_on)
		return rssi;

#if defined(RSSIOFFSET_NEW)
	for (j=0; j<RSSI_OFFSET; j++) {
		if (rssi - (RSSI_OFFSET_MINVAL+RSSI_OFFSET_INTVAL*(j+1)) < 0)
			break;
	}
	rssi += j;
#else
	rssi += RSSI_OFFSET;
#endif /* RSSIOFFSET_NEW */
	return MIN(rssi, RSSI_MAXVAL);
}
#endif /* RSSIOFFSET */

#if defined(BSSCACHE)
void
wl_free_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl)
{
	wl_bss_cache_t *node, *cur, **bss_head;
	int i=0;

	AEXT_TRACE("wlan", "called\n");

	bss_head = &bss_cache_ctrl->m_cache_head;
	node = *bss_head;

	for (;node;) {
		AEXT_TRACE("wlan", "Free %d with BSSID %pM\n",
			i, &node->results.bss_info->BSSID);
		cur = node;
		node = cur->next;
		kfree(cur);
		i++;
	}
	*bss_head = NULL;
}

void
wl_delete_dirty_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl)
{
	wl_bss_cache_t *node, *prev, **bss_head;
	int i = -1, tmp = 0;
	struct osl_timespec now;

	osl_do_gettimeofday(&now);

	bss_head = &bss_cache_ctrl->m_cache_head;
	node = *bss_head;
	prev = node;
	for (;node;) {
		i++;
		if (now.tv_sec > node->tv.tv_sec) {
			if (node == *bss_head) {
				tmp = 1;
				*bss_head = node->next;
			} else {
				tmp = 0;
				prev->next = node->next;
			}
			AEXT_TRACE("wlan", "Del %d with BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
				i, &node->results.bss_info->BSSID,
				dtoh16(node->results.bss_info->RSSI), node->results.bss_info->SSID);
			kfree(node);
			if (tmp == 1) {
				node = *bss_head;
				prev = node;
			} else {
				node = prev->next;
			}
			continue;
		}
		prev = node;
		node = node->next;
	}
}

void
wl_delete_disconnected_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl,
	u8 *bssid)
{
	wl_bss_cache_t *node, *prev, **bss_head;
	int i = -1, tmp = 0;

	bss_head = &bss_cache_ctrl->m_cache_head;
	node = *bss_head;
	prev = node;
	for (;node;) {
		i++;
		if (!memcmp(&node->results.bss_info->BSSID, bssid, ETHER_ADDR_LEN)) {
			if (node == *bss_head) {
				tmp = 1;
				*bss_head = node->next;
			} else {
				tmp = 0;
				prev->next = node->next;
			}
			AEXT_TRACE("wlan", "Del %d with BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
				i, &node->results.bss_info->BSSID,
				dtoh16(node->results.bss_info->RSSI), node->results.bss_info->SSID);
			kfree(node);
			if (tmp == 1) {
				node = *bss_head;
				prev = node;
			} else {
				node = prev->next;
			}
			continue;
		}
		prev = node;
		node = node->next;
	}
}

void
wl_reset_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl)
{
	wl_bss_cache_t *node, **bss_head;

	bss_head = &bss_cache_ctrl->m_cache_head;

	/* reset dirty */
	node = *bss_head;
	for (;node;) {
		node->dirty += 1;
		node = node->next;
	}
}

void dump_bss_cache(
#if defined(RSSIAVG)
	wl_rssi_cache_ctrl_t *rssi_cache_ctrl,
#endif /* RSSIAVG */
	wl_bss_cache_t *node)
{
	int k = 0;
	int16 rssi;

	for (;node;) {
#if defined(RSSIAVG)
		rssi = wl_get_avg_rssi(rssi_cache_ctrl, &node->results.bss_info->BSSID);
#else
		rssi = dtoh16(node->results.bss_info->RSSI);
#endif /* RSSIAVG */
		AEXT_TRACE("wlan", "dump %d with cached BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
			k, &node->results.bss_info->BSSID, rssi, node->results.bss_info->SSID);
		k++;
		node = node->next;
	}
}

void
wl_update_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl,
#if defined(RSSIAVG)
	wl_rssi_cache_ctrl_t *rssi_cache_ctrl,
#endif /* RSSIAVG */
	wl_scan_results_t *ss_list)
{
	wl_bss_cache_t *node, *prev, *leaf, **bss_head;
	wl_bss_info_t *bi = NULL;
	int i, k=0;
#if defined(SORT_BSS_BY_RSSI)
	int16 rssi, rssi_node;
#endif /* SORT_BSS_BY_RSSI */
	struct osl_timespec now, timeout;

	if (!ss_list->count)
		return;

	osl_do_gettimeofday(&now);
	timeout.tv_sec = now.tv_sec + BSSCACHE_TIMEOUT;
	if (timeout.tv_sec < now.tv_sec) {
		/*
		 * Integer overflow - assume long enough timeout to be assumed
		 * to be infinite, i.e., the timeout would never happen.
		 */
		AEXT_TRACE("wlan",
			"Too long timeout (secs=%d) to ever happen - now=%lu, timeout=%lu\n",
			BSSCACHE_TIMEOUT, now.tv_sec, timeout.tv_sec);
	}

	bss_head = &bss_cache_ctrl->m_cache_head;

	for (i=0; i < ss_list->count; i++) {
		node = *bss_head;
		prev = NULL;
		bi = bi ? (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length)) : ss_list->bss_info;

		for (;node;) {
			if (!memcmp(&node->results.bss_info->BSSID, &bi->BSSID, ETHER_ADDR_LEN)) {
				if (node == *bss_head)
					*bss_head = node->next;
				else {
					prev->next = node->next;
				}
				break;
			}
			prev = node;
			node = node->next;
		}

		leaf = kmalloc(dtoh32(bi->length) + sizeof(wl_bss_cache_t), GFP_KERNEL);
		if (!leaf) {
			AEXT_ERROR("wlan", "Memory alloc failure %d\n",
				dtoh32(bi->length) + (int)sizeof(wl_bss_cache_t));
			return;
		}
		if (node) {
			kfree(node);
			node = NULL;
			AEXT_TRACE("wlan",
				"Update %d with cached BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
				k, &bi->BSSID, dtoh16(bi->RSSI), bi->SSID);
		} else
			AEXT_TRACE("wlan",
				"Add %d with cached BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
				k, &bi->BSSID, dtoh16(bi->RSSI), bi->SSID);

		memcpy(leaf->results.bss_info, bi, dtoh32(bi->length));
		leaf->next = NULL;
		leaf->dirty = 0;
		leaf->tv = timeout;
		leaf->results.count = 1;
		leaf->results.version = ss_list->version;
		k++;

		if (*bss_head == NULL)
			*bss_head = leaf;
		else {
#if defined(SORT_BSS_BY_RSSI)
			node = *bss_head;
#if defined(RSSIAVG)
			rssi = wl_get_avg_rssi(rssi_cache_ctrl, &leaf->results.bss_info->BSSID);
#else
			rssi = dtoh16(leaf->results.bss_info->RSSI);
#endif /* RSSIAVG */
			for (;node;) {
#if defined(RSSIAVG)
				rssi_node = wl_get_avg_rssi(rssi_cache_ctrl,
					&node->results.bss_info->BSSID);
#else
				rssi_node = dtoh16(node->results.bss_info->RSSI);
#endif /* RSSIAVG */
				if (rssi > rssi_node) {
					leaf->next = node;
					if (node == *bss_head)
						*bss_head = leaf;
					else
						prev->next = leaf;
					break;
				}
				prev = node;
				node = node->next;
			}
			if (node == NULL)
				prev->next = leaf;
#else
			leaf->next = *bss_head;
			*bss_head = leaf;
#endif /* SORT_BSS_BY_RSSI */
		}
	}
	dump_bss_cache(
#if defined(RSSIAVG)
		rssi_cache_ctrl,
#endif /* RSSIAVG */
		*bss_head);
}

void
wl_release_bss_cache_ctrl(wl_bss_cache_ctrl_t *bss_cache_ctrl)
{
	AEXT_TRACE("wlan", "Enter\n");
	wl_free_bss_cache(bss_cache_ctrl);
}
#endif /* BSSCACHE */


