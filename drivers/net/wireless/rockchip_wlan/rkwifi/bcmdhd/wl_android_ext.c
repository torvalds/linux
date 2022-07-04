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
#include <asm/uaccess.h>
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
			printf("[%s] AEXT-ERROR) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)
#define AEXT_TRACE(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_TRACE_LEVEL) { \
			printf("[%s] AEXT-TRACE) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)
#define AEXT_INFO(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_INFO_LEVEL) { \
			printf("[%s] AEXT-INFO) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)
#define AEXT_DBG(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_DBG_LEVEL) { \
			printf("[%s] AEXT-DBG) %s : " arg1, name, __func__, ## args); \
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

#ifndef IW_CUSTOM_MAX
#define IW_CUSTOM_MAX 256 /* size of extra buffer used for translation of events */
#endif /* IW_CUSTOM_MAX */

#define CMD_CHANNEL				"CHANNEL"
#define CMD_CHANNELS			"CHANNELS"
#define CMD_ROAM_TRIGGER		"ROAM_TRIGGER"
#define CMD_PM					"PM"
#define CMD_MONITOR				"MONITOR"
#define CMD_SET_SUSPEND_BCN_LI_DTIM		"SET_SUSPEND_BCN_LI_DTIM"
#define CMD_WLMSGLEVEL			"WLMSGLEVEL"
#ifdef WL_EXT_IAPSTA
#define CMD_IAPSTA_INIT			"IAPSTA_INIT"
#define CMD_IAPSTA_CONFIG		"IAPSTA_CONFIG"
#define CMD_IAPSTA_ENABLE		"IAPSTA_ENABLE"
#define CMD_IAPSTA_DISABLE		"IAPSTA_DISABLE"
#define CMD_ISAM_INIT			"ISAM_INIT"
#define CMD_ISAM_CONFIG			"ISAM_CONFIG"
#define CMD_ISAM_ENABLE			"ISAM_ENABLE"
#define CMD_ISAM_DISABLE		"ISAM_DISABLE"
#define CMD_ISAM_STATUS			"ISAM_STATUS"
#define CMD_ISAM_PEER_PATH		"ISAM_PEER_PATH"
#define CMD_ISAM_PARAM			"ISAM_PARAM"
#endif /* WL_EXT_IAPSTA */
#define CMD_AUTOCHANNEL		"AUTOCHANNEL"
#define CMD_WL		"WL"
#define CMD_CONF	"CONF"

#if defined(PKT_STATICS) && defined(BCMSDIO)
#define CMD_DUMP_PKT_STATICS			"DUMP_PKT_STATICS"
#define CMD_CLEAR_PKT_STATICS			"CLEAR_PKT_STATICS"
extern void dhd_bus_dump_txpktstatics(dhd_pub_t *dhdp);
extern void dhd_bus_clear_txpktstatics(dhd_pub_t *dhdp);
#endif /* PKT_STATICS && BCMSDIO */

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

typedef struct auth_name_map_t {
	uint auth;
	uint wpa_auth;
	char *auth_name;
} auth_name_map_t;

const auth_name_map_t auth_name_map[] = {
	{WL_AUTH_OPEN_SYSTEM,	WPA_AUTH_DISABLED,	"open"},
	{WL_AUTH_SHARED_KEY,	WPA_AUTH_DISABLED,	"shared"},
	{WL_AUTH_OPEN_SYSTEM,	WPA_AUTH_PSK,		"wpa/psk"},
	{WL_AUTH_OPEN_SYSTEM,	WPA2_AUTH_PSK,		"wpa2/psk"},
	{WL_AUTH_OPEN_SYSTEM,	WPA2_AUTH_PSK_SHA256|WPA2_AUTH_PSK,	"wpa2/psk/sha256"},
	{WL_AUTH_OPEN_SYSTEM,	WPA2_AUTH_FT|WPA2_AUTH_PSK,			"wpa2/psk/ft"},
	{WL_AUTH_OPEN_SYSTEM,	WPA2_AUTH_UNSPECIFIED,				"wpa2/eap"},
	{WL_AUTH_OPEN_SYSTEM,	WPA2_AUTH_FT|WPA2_AUTH_UNSPECIFIED,	"wpa2/eap/ft"},
	{WL_AUTH_OPEN_SYSTEM,	WPA3_AUTH_SAE_PSK,	"wpa3/psk"},
	{WL_AUTH_SAE_KEY,		WPA3_AUTH_SAE_PSK,	"wpa3sae/psk"},
	{WL_AUTH_OPEN_SYSTEM,	WPA3_AUTH_SAE_PSK|WPA2_AUTH_PSK,	"wpa3/psk"},
	{WL_AUTH_SAE_KEY,		WPA3_AUTH_SAE_PSK|WPA2_AUTH_PSK,	"wpa3sae/psk"},
	{WL_AUTH_OPEN_SYSTEM,	0x20,	"wpa3/psk"},
	{WL_AUTH_SAE_KEY,		0x20,	"wpa3sae/psk"},
	{WL_AUTH_OPEN_SYSTEM,	WPA3_AUTH_SAE_PSK|WPA2_AUTH_PSK_SHA256|WPA2_AUTH_PSK,	"wpa3/psk/sha256"},
	{WL_AUTH_SAE_KEY,		WPA3_AUTH_SAE_PSK|WPA2_AUTH_PSK_SHA256|WPA2_AUTH_PSK,	"wpa3sae/psk/sha256"},
	{WL_AUTH_OPEN_SYSTEM,	0x20|WPA2_AUTH_PSK_SHA256|WPA2_AUTH_PSK,	"wpa3/psk/sha256"},
	{WL_AUTH_SAE_KEY,		0x20|WPA2_AUTH_PSK_SHA256|WPA2_AUTH_PSK,	"wpa3sae/psk/sha256"},
	{WL_AUTH_OPEN_SYSTEM,	WPA3_AUTH_OWE,	"owe"},
};

typedef struct wsec_name_map_t {
	uint wsec;
	char *wsec_name;
} wsec_name_map_t;

const wsec_name_map_t wsec_name_map[] = {
	{WSEC_NONE,		"none"},
	{WEP_ENABLED,	"wep"},
	{TKIP_ENABLED,	"tkip"},
	{AES_ENABLED,	"aes"},
	{TKIP_ENABLED|AES_ENABLED,	"tkip/aes"},
};

static int wl_ext_wl_iovar(struct net_device *dev, char *command, int total_len);

int
wl_ext_ioctl(struct net_device *dev, u32 cmd, void *arg, u32 len, u32 set)
{
	int ret;

	ret = wldev_ioctl(dev, cmd, arg, len, set);
	if (ret)
		AEXT_ERROR(dev->name, "cmd=%d, ret=%d\n", cmd, ret);
	return ret;
}

int
wl_ext_iovar_getint(struct net_device *dev, s8 *iovar, s32 *val)
{
	int ret;

	ret = wldev_iovar_getint(dev, iovar, val);
	if (ret)
		AEXT_ERROR(dev->name, "iovar=%s, ret=%d\n", iovar, ret);

	return ret;
}

int
wl_ext_iovar_setint(struct net_device *dev, s8 *iovar, s32 val)
{
	int ret;

	ret = wldev_iovar_setint(dev, iovar, val);
	if (ret)
		AEXT_ERROR(dev->name, "iovar=%s, ret=%d\n", iovar, ret);

	return ret;
}

int
wl_ext_iovar_getbuf(struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, struct mutex* buf_sync)
{
	int ret;

	ret = wldev_iovar_getbuf(dev, iovar_name, param, paramlen, buf, buflen, buf_sync);
	if (ret != 0)
		AEXT_ERROR(dev->name, "iovar=%s, ret=%d\n", iovar_name, ret);

	return ret;
}

int
wl_ext_iovar_setbuf(struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, struct mutex* buf_sync)
{
	int ret;

	ret = wldev_iovar_setbuf(dev, iovar_name, param, paramlen, buf, buflen, buf_sync);
	if (ret != 0)
		AEXT_ERROR(dev->name, "iovar=%s, ret=%d\n", iovar_name, ret);

	return ret;
}

int
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

chanspec_t
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

chanspec_t
wl_ext_chspec_driver_to_host(int ioctl_ver, chanspec_t chanspec)
{
	chanspec = dtohchanspec(chanspec);
	if (ioctl_ver == 1) {
		chanspec = wl_ext_chspec_from_legacy(chanspec);
	}

	return chanspec;
}
#endif /* WL_EXT_IAPSTA || WL_CFG80211 || WL_ESCAN */

chanspec_band_t
wl_ext_wlcband_to_chanspec_band(int band)
{
	chanspec_band_t chanspec_band = WLC_BAND_INVALID;

	switch (band) {
#ifdef WL_6G_BAND
		case WLC_BAND_6G:
			chanspec_band = WL_CHANSPEC_BAND_6G;
			break;
#endif /* WL_6G_BAND */
		case WLC_BAND_5G:
			chanspec_band = WL_CHANSPEC_BAND_5G;
			break;
		case WLC_BAND_2G:
			chanspec_band = WL_CHANSPEC_BAND_2G;
			break;
		default:
			break;
	}
	return chanspec_band;
}

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
#endif /* WL_CFG80211 && WL_ESCAN */

static bool
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
		AEXT_INFO(dev->name, "CFG80211 CONNECTING\n");
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
#ifdef WL_EXT_IAPSTA
	if (wl_ext_sta_connecting(dev)) {
		AEXT_INFO(dev->name, "CONNECTING\n");
		complete = FALSE;
	}
#endif /* WL_EXT_IAPSTA */

	return complete;
}

void
wl_ext_wait_event_complete(struct dhd_pub *dhd, int ifidx)
{
	struct net_device *net;
	s32 timeout = -1;

	timeout = wait_event_interruptible_timeout(dhd->conf->event_complete,
		wl_ext_event_complete(dhd, ifidx), msecs_to_jiffies(10000));
	if (timeout <= 0 || !wl_ext_event_complete(dhd, ifidx)) {
		wl_ext_event_complete(dhd, ifidx);
		net = dhd_idx2net(dhd, ifidx);
		AEXT_ERROR(net->name, "timeout\n");
	}
}

int
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

void
wl_ext_bss_iovar_war(struct net_device *ndev, s32 *val)
{
	dhd_pub_t *dhd = dhd_get_pub(ndev);
	uint chip;
	bool need_war = false;

	chip = dhd_conf_get_chip(dhd);

	if (chip == BCM43362_CHIP_ID || chip == BCM4330_CHIP_ID ||
		chip == BCM4354_CHIP_ID || chip == BCM4356_CHIP_ID ||
		chip == BCM4371_CHIP_ID ||
		chip == BCM43430_CHIP_ID ||
		chip == BCM4345_CHIP_ID || chip == BCM43454_CHIP_ID ||
		chip == BCM4359_CHIP_ID ||
		chip == BCM43143_CHIP_ID || chip == BCM43242_CHIP_ID ||
		chip == BCM43569_CHIP_ID) {
		need_war = true;
	}

	if (need_war) {
		/* Few firmware branches have issues in bss iovar handling and
		 * that can't be changed since they are in production.
		 */
		if (*val == WLC_AP_IOV_OP_MANUAL_AP_BSSCFG_CREATE) {
			*val = WLC_AP_IOV_OP_MANUAL_STA_BSSCFG_CREATE;
		} else if (*val == WLC_AP_IOV_OP_MANUAL_STA_BSSCFG_CREATE) {
			*val = WLC_AP_IOV_OP_MANUAL_AP_BSSCFG_CREATE;
		} else {
			/* Ignore for other bss enums */
			return;
		}
		AEXT_TRACE(ndev->name, "wl bss %d\n", *val);
	}
}

int
wl_ext_set_chanspec(struct net_device *dev, struct wl_chan_info *chan_info,
	chanspec_t *ret_chspec)
{
	s32 _chan = chan_info->chan;
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
	chanspec_band_t chanspec_band = 0;
	int ioctl_ver;

	if ((chan_info->band != WLC_BAND_2G) && (chan_info->band != WLC_BAND_5G) &&
			(chan_info->band != WLC_BAND_6G)) {
		AEXT_ERROR(dev->name, "bad band %d\n", chan_info->band);
		return BCME_BADBAND;
	}

	param.band = chan_info->band;
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

set_channel:
	chanspec_band = wl_ext_wlcband_to_chanspec_band(chan_info->band);
	chspec = wf_create_chspec_from_primary(chan_info->chan, bw, chanspec_band);
	if (wf_chspec_valid(chspec)) {
		wl_ext_get_ioctl_ver(dev, &ioctl_ver);
		fw_chspec = wl_ext_chspec_host_to_driver(ioctl_ver, chspec);
		if (fw_chspec != INVCHANSPEC) {
			if ((err = wl_ext_iovar_setint(dev, "chanspec", fw_chspec)) == BCME_BADCHAN) {
				if (bw == WL_CHANSPEC_BW_80)
					goto change_bw;
				err = wl_ext_ioctl(dev, WLC_SET_CHANNEL, &_chan, sizeof(_chan), 1);
				WL_MSG(dev->name, "channel %s-%d\n", CHSPEC2BANDSTR(chspec), _chan);
			} else if (err) {
				AEXT_ERROR(dev->name, "failed to set chanspec error %d\n", err);
			} else
				WL_MSG(dev->name, "channel %s-%d(0x%x)\n",
					CHSPEC2BANDSTR(chspec), chan_info->chan, chspec);
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
	struct wl_chan_info chan_info;
	int ret;
	char band[16]="";
	int channel = 0;
	channel_info_t ci;
	int bytes_written = 0;
	chanspec_t fw_chspec;

	AEXT_TRACE(dev->name, "cmd %s", command);

	sscanf(command, "%*s %d %s", &channel, band);
	if (strnicmp(band, "band=auto", strlen("band=auto")) == 0) {
		chan_info.band = WLC_BAND_AUTO;
	}
#ifdef WL_6G_BAND
	else if (strnicmp(band, "band=6g", strlen("band=6g")) == 0) {
		chan_info.band = WLC_BAND_6G;
	}
#endif /* WL_6G_BAND */
	else if (strnicmp(band, "band=5g", strlen("band=5g")) == 0) {
		chan_info.band = WLC_BAND_5G;
	}
	else if (strnicmp(band, "band=2g", strlen("band=2g")) == 0) {
		chan_info.band = WLC_BAND_2G;
	}

	if (channel > 0) {
		chan_info.chan = channel;
		ret = wl_ext_set_chanspec(dev, &chan_info, &fw_chspec);
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
	char sec[64];

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

	wl_ext_get_sec(dev, 0, sec, sizeof(sec), TRUE);
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
	wl_ext_get_sec(dev, 0, sec, sizeof(sec), TRUE);
	WL_MSG(dev->name,
		"Connecting with %pM channel (%d) ssid \"%s\", len (%d), sec=%s\n\n",
		&join_params.params.bssid, conn_info->channel,
		join_params.ssid.SSID, join_params.ssid.SSID_len, sec);
	err = wl_ext_ioctl(dev, WLC_SET_SSID, &join_params, join_params_size, 1);

exit:
#ifdef WL_EXT_IAPSTA
	if (!err)
		wl_ext_add_remove_pm_enable_work(dev, TRUE);
#endif /* WL_EXT_IAPSTA */
	if (iovar_buf)
		kfree(iovar_buf);
	if (ext_join_params)
		kfree(ext_join_params);
	return err;

}

void
wl_ext_get_sec(struct net_device *dev, int ifmode, char *sec, int total_len, bool dump)
{
	int auth=0, wpa_auth=0, wsec=0, mfp=0, i;
	int bytes_written=0;
	bool match = FALSE;

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
		match = FALSE;
		for (i=0; i<sizeof(auth_name_map)/sizeof(auth_name_map[0]); i++) {
			const auth_name_map_t* row = &auth_name_map[i];
			if (row->auth == auth && row->wpa_auth == wpa_auth) {
				bytes_written += snprintf(sec+bytes_written, total_len, "%s",
					row->auth_name);
				match = TRUE;
				break;
			}
		}
		if (!match) {
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
		match = FALSE;
		for (i=0; i<sizeof(wsec_name_map)/sizeof(wsec_name_map[0]); i++) {
			const wsec_name_map_t* row = &wsec_name_map[i];
			if (row->wsec == (wsec&0x7)) {
				bytes_written += snprintf(sec+bytes_written, total_len, "/%s",
					row->wsec_name);
				match = TRUE;
				break;
			}
		}
		if (!match) {
			bytes_written += snprintf(sec+bytes_written, total_len, "/0x%x", wsec);
		}
	}
	if (dump) {
		AEXT_INFO(dev->name, "auth/wpa_auth/mfp/wsec = %d/0x%x/%d/0x%x\n",
			auth, wpa_auth, mfp, wsec);
	}
}

bool
wl_ext_dfs_chan(struct wl_chan_info *chan_info)
{
	if (chan_info->band == WLC_BAND_5G && chan_info->chan >= 52 && chan_info->chan <= 144)
		return TRUE;
	return FALSE;
}

uint16
wl_ext_get_default_chan(struct net_device *dev,
	uint16 *chan_2g, uint16 *chan_5g, bool nodfs)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	struct wl_chan_info chan_info;
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
			if (chan_tmp <= 13 && !*chan_2g) {
				*chan_2g = chan_tmp;
			} else if (chan_tmp >= 36 && chan_tmp <= 161 && !*chan_5g) {
				chan_info.band = WLC_BAND_5G;
				chan_info.chan = chan_tmp;
				if (wl_ext_dfs_chan(&chan_info) && nodfs)
					continue;
				else
					*chan_5g = chan_tmp;
			}
		}
	}

	return chan;
}

int
wl_ext_set_scan_time(struct net_device *dev, int scan_time,
	uint32 scan_get, uint32 scan_set)
{
	int ret, cur_scan_time;

	ret = wl_ext_ioctl(dev, scan_get, &cur_scan_time, sizeof(cur_scan_time), 0);
	if (ret)
		return 0;

	if (scan_time != cur_scan_time)
		wl_ext_ioctl(dev, scan_set, &scan_time, sizeof(scan_time), 1);

	return cur_scan_time;
}

static int
wl_ext_wlmsglevel(struct net_device *dev, char *command, int total_len)
{
	int val = -1, ret = 0;
	int bytes_written = 0;

	sscanf(command, "%*s %x", &val);

	if (val >=0) {
		if (val & DHD_ANDROID_VAL) {
			android_msg_level = (uint)(val & 0xFFFF);
			WL_MSG(dev->name, "android_msg_level=0x%x\n", android_msg_level);
		}
#if defined(WL_WIRELESS_EXT)
		else if (val & DHD_IW_VAL) {
			iw_msg_level = (uint)(val & 0xFFFF);
			WL_MSG(dev->name, "iw_msg_level=0x%x\n", iw_msg_level);
		}
#endif
#ifdef WL_CFG80211
		else if (val & DHD_CFG_VAL) {
			wl_cfg80211_enable_trace((u32)(val & 0xFFFF));
		}
#endif
		else if (val & DHD_CONFIG_VAL) {
			config_msg_level = (uint)(val & 0xFFFF);
			WL_MSG(dev->name, "config_msg_level=0x%x\n", config_msg_level);
		}
		else if (val & DHD_DUMP_VAL) {
			dump_msg_level = (uint)(val & 0xFFFF);
			WL_MSG(dev->name, "dump_msg_level=0x%x\n", dump_msg_level);
		}
	}
	else {
		bytes_written += snprintf(command+bytes_written, total_len,
			"android_msg_level=0x%x", android_msg_level);
#if defined(WL_WIRELESS_EXT)
		bytes_written += snprintf(command+bytes_written, total_len,
			"\niw_msg_level=0x%x", iw_msg_level);
#endif
#ifdef WL_CFG80211
		bytes_written += snprintf(command+bytes_written, total_len,
			"\nwl_dbg_level=0x%x", wl_dbg_level);
#endif
		bytes_written += snprintf(command+bytes_written, total_len,
			"\nconfig_msg_level=0x%x", config_msg_level);
		bytes_written += snprintf(command+bytes_written, total_len,
			"\ndump_msg_level=0x%x", dump_msg_level);
		AEXT_INFO(dev->name, "%s\n", command);
		ret = bytes_written;
	}

	return ret;
}

#ifdef WL_CFG80211
bool
wl_legacy_chip_check(struct net_device *net)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	uint chip;

	chip = dhd_conf_get_chip(dhd);

	if (chip == BCM43362_CHIP_ID || chip == BCM4330_CHIP_ID ||
		chip == BCM4334_CHIP_ID || chip == BCM43340_CHIP_ID ||
		chip == BCM43341_CHIP_ID || chip == BCM4324_CHIP_ID ||
		chip == BCM4335_CHIP_ID || chip == BCM4339_CHIP_ID ||
		chip == BCM4354_CHIP_ID || chip == BCM4356_CHIP_ID ||
		chip == BCM4371_CHIP_ID ||
		chip == BCM43430_CHIP_ID ||
		chip == BCM4345_CHIP_ID || chip == BCM43454_CHIP_ID ||
		chip == BCM4359_CHIP_ID ||
		chip == BCM43143_CHIP_ID || chip == BCM43242_CHIP_ID ||
		chip == BCM43569_CHIP_ID) {
		return true;
	}

	return false;
}

bool
wl_new_chip_check(struct net_device *net)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	uint chip;

	chip = dhd_conf_get_chip(dhd);

	if (chip == BCM4359_CHIP_ID || chip == BCM43012_CHIP_ID ||
			chip == BCM43751_CHIP_ID || chip == BCM43752_CHIP_ID) {
		return true;
	}

	return false;
}

bool
wl_extsae_chip(struct dhd_pub *dhd)
{
	uint chip;

	chip = dhd_conf_get_chip(dhd);

	if (chip == BCM43362_CHIP_ID || chip == BCM4330_CHIP_ID ||
		chip == BCM4334_CHIP_ID || chip == BCM43340_CHIP_ID ||
		chip == BCM43341_CHIP_ID || chip == BCM4324_CHIP_ID ||
		chip == BCM4335_CHIP_ID || chip == BCM4339_CHIP_ID ||
		chip == BCM4354_CHIP_ID || chip == BCM4356_CHIP_ID ||
		chip == BCM43143_CHIP_ID || chip == BCM43242_CHIP_ID ||
		chip == BCM43569_CHIP_ID) {
		return false;
	}

	return true;
}
#endif

#ifdef WLEASYMESH
#define CMD_EASYMESH "EASYMESH"
//Set map 4 and dwds 1 on wlan0 interface
#define EASYMESH_SLAVE		"slave"
#define EASYMESH_MASTER		"master"

static int
wl_ext_easymesh(struct net_device *dev, char* command, int total_len)
{
	int ret = 0, wlc_down = 1, wlc_up = 1, map = 4, dwds = 1;

	AEXT_TRACE(dev->name, "command=%s, len=%d\n", command, total_len);
	if (strncmp(command, EASYMESH_SLAVE, strlen(EASYMESH_SLAVE)) == 0) {
		WL_MSG(dev->name, "try to set map %d, dwds %d\n", map, dwds);
		ret = wl_ext_ioctl(dev, WLC_DOWN, &wlc_down, sizeof(wlc_down), 1);
		if (ret)
			goto exit;
		wl_ext_iovar_setint(dev, "map", map);
		wl_ext_iovar_setint(dev, "dwds", dwds);
		ret = wl_ext_ioctl(dev, WLC_UP, &wlc_up, sizeof(wlc_up), 1);
		if (ret)
			goto exit;
	}
	else if (strncmp(command, EASYMESH_MASTER, strlen(EASYMESH_MASTER)) == 0) {
		map = dwds = 0;
		WL_MSG(dev->name, "try to set map %d, dwds %d\n", map, dwds);
		ret = wl_ext_ioctl(dev, WLC_DOWN, &wlc_down, sizeof(wlc_down), 1);
		if (ret) {
			goto exit;
		}
		wl_ext_iovar_setint(dev, "map", map);
		wl_ext_iovar_setint(dev, "dwds", dwds);
		ret = wl_ext_ioctl(dev, WLC_UP, &wlc_up, sizeof(wlc_up), 1);
		if (ret) {
			goto exit;
		}
	}

exit:
	return ret;
}
#endif /* WLEASYMESH */

int
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
		sscanf(data, "%d %s %s %s %d %d %d %u %u %d %u %u %u %32s",
			&sess_id, dst_mac, src_ip, dst_ip, &ipid, &srcport, &dstport, &seq,
			&ack, &tcpwin, &tsval, &tsecr, &len, ka_payload);

		ka_payload_len = strlen(ka_payload) / 2;
		tcpka = kmalloc(sizeof(struct tcpka_conn) + ka_payload_len, GFP_KERNEL);
		if (tcpka == NULL) {
			AEXT_ERROR(dev->name, "Failed to allocate buffer of %d bytes\n",
				sizeof(struct tcpka_conn) + ka_payload_len);
			ret = -1;
			goto exit;
		}
		memset(tcpka, 0, sizeof(struct tcpka_conn) + ka_payload_len);

		tcpka->sess_id = sess_id;
		if (!(ret = bcm_ether_atoe(dst_mac, &tcpka->dst_mac))) {
			AEXT_ERROR(dev->name, "mac parsing err addr=%s\n", dst_mac);
			ret = -1;
			goto exit;
		}
		if (!bcm_atoipv4(src_ip, &tcpka->src_ip)) {
			AEXT_ERROR(dev->name, "src_ip parsing err ip=%s\n", src_ip);
			ret = -1;
			goto exit;
		}
		if (!bcm_atoipv4(dst_ip, &tcpka->dst_ip)) {
			AEXT_ERROR(dev->name, "dst_ip parsing err ip=%s\n", dst_ip);
			ret = -1;
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
		ka_payload_len = wl_pattern_atoh(ka_payload, (char *)tcpka->ka_payload);
		if (ka_payload_len == -1) {
			AEXT_ERROR(dev->name,"rejecting ka_payload=%s\n", ka_payload);
			ret = -1;
			goto exit;
		}
		tcpka->ka_payload_len = ka_payload_len;

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
	int ret = 0;
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
	int ret = 0, bytes_written = 0;

	if (data) {
		sscanf(data, "%d", &sess_id);
		AEXT_INFO(dev->name, "tcpka_conn_sess_info %d\n", sess_id);
		ret = wl_ext_iovar_getbuf(dev, "tcpka_conn_sess_info", (char *)&sess_id,
			sizeof(uint32), iovar_buf, sizeof(iovar_buf), NULL);
		if (!ret) {
			info = (tcpka_conn_sess_info_t *) iovar_buf;
			bytes_written += snprintf(command+bytes_written, total_len,
				"id   :%d\n"
				"ipid :%d\n"
				"seq  :%u\n"
				"ack  :%u",
				sess_id, info->ipid, info->seq, info->ack);
			AEXT_INFO(dev->name, "%s\n", command);
			ret = bytes_written;
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
			if (dhd->conf->pm) {
				strcpy(cmd, "wl 86 2");
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
	char cmd[64];

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

#if defined(USE_IW)
static int
wl_ext_gtk_key_info(struct net_device *dev, char *data, char *command, int total_len)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	int err = 0;
	char iovar_buf[WLC_IOCTL_SMLEN]="\0";
	gtk_keyinfo_t keyinfo;
	bcol_gtk_para_t bcol_keyinfo;

	/* wl gtk_key_info [kck kek replay_ctr] */
	/* wl gtk_key_info 001122..FF001122..FF00000000000001 */
	if (data) {
		if (!dhd->conf->rekey_offload) {
			AEXT_INFO(dev->name, "rekey_offload disabled\n");
			return BCME_UNSUPPORTED;
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

		memset(&keyinfo, 0, sizeof(keyinfo));
		memcpy(&keyinfo, data, RSN_KCK_LENGTH+RSN_KEK_LENGTH+RSN_REPLAY_LEN);
		err = wl_ext_iovar_setbuf(dev, "gtk_key_info", &keyinfo, sizeof(keyinfo),
			iovar_buf, sizeof(iovar_buf), NULL);
		if (err) {
			AEXT_ERROR(dev->name, "failed to set gtk_key_info\n");
			return err;
		}
	}

exit:
	if (android_msg_level & ANDROID_INFO_LEVEL) {
		prhex("kck", (uchar *)keyinfo.KCK, RSN_KCK_LENGTH);
		prhex("kek", (uchar *)keyinfo.KEK, RSN_KEK_LENGTH);
		prhex("replay_ctr", (uchar *)keyinfo.ReplayCounter, RSN_REPLAY_LEN);
	}
    return err;
}
#endif /* USE_IW */

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

#ifdef CSI_SUPPORT
typedef struct csi_config {
	/* Peer device mac address. */
	struct ether_addr addr;
	/* BW to be used in the measurements. This needs to be supported both by the */
	/* device itself and the peer. */
	uint32 bw;
	/* Time interval between measurements (units: 1 ms). */
	uint32 period;
	/* CSI method */
	uint32 method;
} csi_config_t;

typedef struct csi_list {
	uint32 cnt;
	csi_config_t configs[1];
} csi_list_t;

static int
wl_ether_atoe(const char *a, struct ether_addr *n)
{
	char *c = NULL;
	int i = 0;

	memset(n, 0, ETHER_ADDR_LEN);
	for (;;) {
		n->octet[i++] = (uint8)strtoul(a, &c, 16);
		if (!*c++ || i == ETHER_ADDR_LEN)
			break;
		a = c;
	}
	return (i == ETHER_ADDR_LEN);
}

static int
wl_ext_csi(struct net_device *dev, char *data, char *command, int total_len)
{
	csi_config_t csi, *csip;
	csi_list_t *csi_list;
	int ret = -1, period=-1, i;
	char mac[32], *buf = NULL;
	struct ether_addr ea;
	int bytes_written = 0;

	buf = kmalloc(WLC_IOCTL_SMLEN, GFP_KERNEL);
	if (buf == NULL) {
		AEXT_ERROR(dev->name, "Failed to allocate buffer of %d bytes\n", WLC_IOCTL_SMLEN);
		goto exit;
	}
	memset(buf, 0, WLC_IOCTL_SMLEN);

	if (data) {
		sscanf(data, "%s %d", mac, &period);
		ret = wl_ether_atoe(mac, &ea);
		if (!ret) {
			AEXT_ERROR(dev->name, "rejecting mac=%s, ret=%d\n", mac, ret);
			goto exit;
		}
		AEXT_TRACE(dev->name, "mac=%pM, period=%d", &ea, period);
		if (period > 0) {
			memset(&csi, 0, sizeof(csi_config_t));
			bcopy(&ea, &csi.addr, ETHER_ADDR_LEN);
			csi.period = period;
			ret = wl_ext_iovar_setbuf(dev, "csi", (char *)&csi, sizeof(csi),
				buf, WLC_IOCTL_SMLEN, NULL);
		} else if (period == 0) {
			memset(&csi, 0, sizeof(csi_config_t));
			bcopy(&ea, &csi.addr, ETHER_ADDR_LEN);
			ret = wl_ext_iovar_setbuf(dev, "csi_del", (char *)&csi, sizeof(csi),
				buf, WLC_IOCTL_SMLEN, NULL);
		} else {
			ret = wl_ext_iovar_getbuf(dev, "csi", &ea, ETHER_ADDR_LEN, buf,
				WLC_IOCTL_SMLEN, NULL);
			if (!ret) {
				csip = (csi_config_t *) buf;
					/* Dump all lists */
				bytes_written += snprintf(command+bytes_written, total_len,
					"Mac    :%pM\n"
					"Period :%d\n"
					"BW     :%d\n"
					"Method :%d\n",
					&csip->addr, csip->period, csip->bw, csip->method);
				AEXT_TRACE(dev->name, "command result is %s\n", command);
				ret = bytes_written;
			}
		}
	}
	else {
		ret = wl_ext_iovar_getbuf(dev, "csi_list", NULL, 0, buf, WLC_IOCTL_SMLEN, NULL);
		if (!ret) {
			csi_list = (csi_list_t *)buf;
			bytes_written += snprintf(command+bytes_written, total_len,
				"Total number :%d\n", csi_list->cnt);
			for (i=0; i<csi_list->cnt; i++) {
				csip = &csi_list->configs[i];
				bytes_written += snprintf(command+bytes_written, total_len,
					"Idx    :%d\n"
					"Mac    :%pM\n"
					"Period :%d\n"
					"BW     :%d\n"
					"Method :%d\n\n",
					i+1, &csip->addr, csip->period, csip->bw, csip->method);
			}
			AEXT_TRACE(dev->name, "command result is %s\n", command);
			ret = bytes_written;
		}
	}

exit:
	if (buf)
		kfree(buf);
	return ret;
}
#endif /* CSI_SUPPORT */

static int
wl_ext_get_country(struct net_device *dev, char *data, char *command,
	int total_len)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	wl_country_t cspec = {{0}, 0, {0}};
	int bytes_written = 0, ret = 0;

	if (data) {
		char *country_code = data;
		char *rev_info_delim = country_code + 2; /* 2 bytes of country code */
		int revinfo = 0;
		if ((rev_info_delim) &&
			(strnicmp(rev_info_delim, "/", strlen("/")) == 0) && (rev_info_delim + 1)) {
			revinfo  = bcm_atoi(rev_info_delim + 1);
		}
#ifdef WL_CFG80211
		bytes_written = wl_cfg80211_set_country_code(dev, country_code,
			true, true, revinfo);
#else
		bytes_written = wldev_set_country(dev, country_code, true, true, revinfo);
#endif /* WL_CFG80211 */
	} else {
		ret = dhd_conf_get_country(dhd, &cspec);
		if (!ret) {
			bytes_written += snprintf(command+bytes_written, total_len,
				"%s/%d", cspec.ccode, cspec.rev);
		}
		if (!bytes_written)
			bytes_written = -1;
		AEXT_TRACE(dev->name, "command result is %s\n", command);
	}

	return bytes_written;
}

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
#if defined(USE_IW)
	{WLC_GET_VAR,	WLC_SET_VAR,	"gtk_key_info",	wl_ext_gtk_key_info},
#endif /* USE_IW */
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
#ifdef CSI_SUPPORT
	{WLC_GET_VAR,	WLC_SET_VAR,	"csi",				wl_ext_csi},
#endif /* CSI_SUPPORT */
	{WLC_GET_VAR,	WLC_SET_VAR,	"country",			wl_ext_get_country},
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
	char *pEnd;

	AEXT_TRACE(dev->name, "cmd %s\n", command);
	pick_tmp = command;

	pch = bcmstrtok(&pick_tmp, " ", 0); // pick wl
	if (!pch || strncmp(pch, "wl", 2))
		goto exit;

	pch = bcmstrtok(&pick_tmp, " ", 0); // pick cmd
	if (!pch)
		goto exit;

	memset(name, 0 , sizeof (name));
	cmd = bcm_strtoul(pch, &pEnd, 0);
	if (cmd == 0 || strlen(pEnd)) {
		strcpy(name, pch);
	}
	data = bcmstrtok(&pick_tmp, "", 0); // pick data
	if (data && (cmd == 0|| strlen(pEnd))) {
		cmd = WLC_SET_VAR;
	} else if (cmd == 0|| strlen(pEnd)) {
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
wl_ext_conf_iovar(struct net_device *dev, char *command, int total_len)
{
	int ret = 0;
	char name[32], *pch, *pick_tmp, *data;
	int bytes_written=-1;
	struct dhd_pub *dhd = dhd_get_pub(dev);

	AEXT_TRACE(dev->name, "cmd %s\n", command);
	pick_tmp = command;

	pch = bcmstrtok(&pick_tmp, " ", 0); // pick conf
	if (!pch || strncmp(pch, "conf", 4))
		goto exit;

	pch = bcmstrtok(&pick_tmp, " ", 0); // pick cmd
	if (!pch)
		goto exit;

	strncpy(name, pch, sizeof(name));
	
	data = bcmstrtok(&pick_tmp, "", 0); // pick data

	if (!strcmp(name, "pm")) {
		if (data) {
			dhd->conf->pm = simple_strtol(data, NULL, 0);
			ret = 0;
		} else {
			bytes_written = snprintf(command, total_len, "%d", dhd->conf->pm);
			ret = bytes_written;
		}
	} else {
		AEXT_ERROR(dev->name, "no config parameter found\n");
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
#if defined(WLMESH) && defined(WL_ESCAN)
	else if (strnicmp(command, CMD_ISAM_PEER_PATH, strlen(CMD_ISAM_PEER_PATH)) == 0) {
		*bytes_written = wl_ext_isam_peer_path(net, command, total_len);
	}
#endif /* WLMESH && WL_ESCAN */
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
	else if (strnicmp(command, CMD_WLMSGLEVEL, strlen(CMD_WLMSGLEVEL)) == 0) {
		*bytes_written = wl_ext_wlmsglevel(net, command, total_len);
	}
#ifdef WLEASYMESH
    else if (strnicmp(command, CMD_EASYMESH, strlen(CMD_EASYMESH)) == 0) {
		int skip = strlen(CMD_EASYMESH) + 1;
		*bytes_written = wl_ext_easymesh(net, command+skip, total_len);
    }
#endif /* WLEASYMESH */
#if defined(PKT_STATICS) && defined(BCMSDIO)
	else if (strnicmp(command, CMD_DUMP_PKT_STATICS, strlen(CMD_DUMP_PKT_STATICS)) == 0) {
		struct dhd_pub *dhd = dhd_get_pub(net);
		dhd_bus_dump_txpktstatics(dhd);
	}
	else if (strnicmp(command, CMD_CLEAR_PKT_STATICS, strlen(CMD_CLEAR_PKT_STATICS)) == 0) {
		struct dhd_pub *dhd = dhd_get_pub(net);
		dhd_bus_clear_txpktstatics(dhd);
	}
#endif /* PKT_STATICS && BCMSDIO */
	else if (strnicmp(command, CMD_WL, strlen(CMD_WL)) == 0) {
		*bytes_written = wl_ext_wl_iovar(net, command, total_len);
	}
	else if (strnicmp(command, CMD_CONF, strlen(CMD_CONF)) == 0) {
		*bytes_written = wl_ext_conf_iovar(net, command, total_len);
	}
	else
		ret = -1;

	return ret;
}

#define CH_MIN_5G_CHANNEL 34
int
wl_construct_ctl_chanspec_list(struct net_device *dev, wl_uint32_list_t *chan_list)
{
	void *list;
	u32 i, channel;
	chanspec_t chspec = 0;
	s32 err = BCME_OK;
	bool legacy_chan_info = FALSE;
	u16 list_count;

#define LOCAL_BUF_LEN 4096
	list = kmalloc(LOCAL_BUF_LEN, GFP_KERNEL);
	if (list == NULL) {
		WL_ERR(("failed to allocate local buf\n"));
		return -ENOMEM;
	}

	err = wl_ext_iovar_getbuf(dev, "chan_info_list", NULL,
		0, list, LOCAL_BUF_LEN, NULL);
	if (err == BCME_UNSUPPORTED) {
		err = wl_ext_iovar_getbuf(dev, "chanspecs", NULL,
			0, list, LOCAL_BUF_LEN, NULL);
		if (err != BCME_OK) {
			WL_ERR(("get chanspecs err(%d)\n", err));
			kfree(list);
			return err;
		}
		/* Update indicating legacy chan info usage */
		legacy_chan_info = TRUE;
	} else if (err != BCME_OK) {
		WL_ERR(("get chan_info_list err(%d)\n", err));
		kfree(list);
		return err;
	}

	list_count = legacy_chan_info ? ((wl_uint32_list_t *)list)->count :
		((wl_chanspec_list_v1_t *)list)->count;
	for (i = 0; i < dtoh32(list_count); i++) {
		if (legacy_chan_info) {
			chspec = (chanspec_t)dtoh32(((wl_uint32_list_t *)list)->element[i]);
		} else {
			chspec = (chanspec_t)dtoh32
			(((wl_chanspec_list_v1_t *)list)->chspecs[i].chanspec);
		}
		chspec = wl_chspec_driver_to_host(chspec);
		channel = wf_chspec_ctlchan(chspec);

		if (!CHSPEC_IS20(chspec)) {
			continue;
		}
		if (CHSPEC_IS2G(chspec) && (channel >= CH_MIN_2G_CHANNEL) &&
				(channel <= CH_MAX_2G_CHANNEL)) {
			chan_list->element[chan_list->count] = chspec;
			chan_list->count++;
		}
#ifdef WL_6G_BAND
		else if (CHSPEC_IS6G(chspec) && (channel >= CH_MIN_6G_CHANNEL) &&
				(channel <= CH_MAX_6G_CHANNEL)) {
			if (channel == 2)
				continue;
			chan_list->element[chan_list->count] = chspec;
			chan_list->count++;
		}
#endif /* WL_6G_BAND */
		else if (CHSPEC_IS5G(chspec) && (channel >= CH_MIN_5G_CHANNEL) &&
				(channel <= 165)) {
			chan_list->element[chan_list->count] = chspec;
			chan_list->count++;
		} else {
			continue;
		}
	}

	kfree(list);
#undef LOCAL_BUF_LEN
	return err;
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
	wl_scan_results_t *bss_list,
#endif /* BSSCACHE */
	int ioctl_ver, int *best_2g_ch, int *best_5g_ch, int *best_6g_ch)
{
	struct wl_bss_info *bi = NULL;	/* must be initialized */
	s32 i, j;
#if defined(BSSCACHE)
	wl_bss_cache_t *node;
#endif /* BSSCACHE */
	int b_band[CH_MAX_2G_CHANNEL]={0}, a_band1[4]={0}, a_band4[5]={0};
#ifdef WL_6G_BAND
	int six_g_band5[24]={0}, six_g_band6[5]={0}, six_g_band7[18]={0}, six_g_band8[13]={0};
	s32 distance_6g;
#endif /* WL_6G_BAND */
	s32 cen_ch, distance, distance_2g, distance_5g, chanspec, min_ap=999;
	u8 valid_chan_list[sizeof(u32)*(MAX_CTRL_CHANSPECS + 1)];
	wl_uint32_list_t *list;
	int ret;
	chanspec_t chspec;
	u32 channel;

	memset(b_band, -1, sizeof(b_band));
	memset(a_band1, -1, sizeof(a_band1));
	memset(a_band4, -1, sizeof(a_band4));
#ifdef WL_6G_BAND
	memset(six_g_band5, -1, sizeof(six_g_band5));
	memset(six_g_band6, -1, sizeof(six_g_band6));
	memset(six_g_band7, -1, sizeof(six_g_band7));
	memset(six_g_band8, -1, sizeof(six_g_band8));
#endif /* WL_6G_BAND */

	memset(valid_chan_list, 0, sizeof(valid_chan_list));
	list = (wl_uint32_list_t *)(void *) valid_chan_list;

	ret = wl_construct_ctl_chanspec_list(net, list);
	if (ret < 0) {
		AEXT_ERROR(net->name, "get channels failed with %d\n", ret);
		return 0;
	} else {
		for (i = 0; i < list->count; i++) {
			chspec = list->element[i];
			channel = wf_chspec_ctlchan(chspec);
			if (CHSPEC_IS2G(chspec) && (channel >= CH_MIN_2G_CHANNEL) &&
				(channel <= CH_MAX_2G_CHANNEL)) {
				b_band[channel-1] = 0;
			}
#ifdef WL_6G_BAND
			else if (CHSPEC_IS6G(chspec) && (channel >= CH_MIN_6G_CHANNEL) &&
				(channel <= CH_MAX_6G_CHANNEL)) {
				if (channel <= 93)
					six_g_band5[(channel-1)/4] = 0;
				else if (channel >= 97 && channel <= 109)
					six_g_band6[(channel-97)/4] = 0;
				else if (channel >= 117 && channel <= 181)
					six_g_band7[(channel-117)/4] = 0;
				else if (channel >= 189 && channel <= 221)
					six_g_band8[(channel-189)/4] = 0;
			}
#endif /* WL_6G_BAND */
			else if (CHSPEC_IS5G(chspec) && channel >= CH_MIN_5G_CHANNEL) {
				if (channel <= 48)
					a_band1[(channel-36)/4] = 0;
				else if (channel >= 149 && channel <= 161)
					a_band4[(channel-149)/4] = 0;
			}
		}
	}

	distance_2g = wl_ext_get_distance(net, WLC_BAND_2G);
	distance_5g = wl_ext_get_distance(net, WLC_BAND_5G);
#ifdef WL_6G_BAND
	distance_6g = wl_ext_get_distance(net, WLC_BAND_6G);
#endif /* WL_6G_BAND */

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
		}
#ifdef WL_6G_BAND
		else if (CHSPEC_IS6G(chanspec)) {
			distance += distance_6g;
			if (cen_ch <= 93) {
				for (j=0; j<ARRAYSIZE(a_band1); j++) {
					if (six_g_band5[j] >= 0 && abs(cen_ch-(93+j*4)) <= distance)
						six_g_band5[j] += 1;
				}
			}
			else if (channel >= 97 && channel <= 109) {
				for (j=0; j<ARRAYSIZE(a_band4); j++) {
					if (six_g_band6[j] >= 0 && abs(cen_ch-(97+j*4)) <= distance)
						six_g_band6[j] += 1;
				}
			}
			else if (channel >= 117 && channel <= 181) {
				for (j=0; j<ARRAYSIZE(a_band4); j++) {
					if (six_g_band7[j] >= 0 && abs(cen_ch-(117+j*4)) <= distance)
						six_g_band7[j] += 1;
				}
			}
			else if (channel >= 189 && channel <= 221) {
				for (j=0; j<ARRAYSIZE(a_band4); j++) {
					if (six_g_band8[j] >= 0 && abs(cen_ch-(189+j*4)) <= distance)
						six_g_band8[j] += 1;
				}
			}
		}
#endif /* WL_6G_BAND */
		else {
			distance += distance_5g;
			if (cen_ch <= 48) {
				for (j=0; j<ARRAYSIZE(a_band1); j++) {
					if (a_band1[j] >= 0 && abs(cen_ch-(36+j*4)) <= distance)
						a_band1[j] += 1;
				}
			}
			else if (cen_ch >= 149) {
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
#ifdef WL_6G_BAND
	*best_6g_ch = 0;
	min_ap = 999;
	for (i=0; i<ARRAYSIZE(six_g_band5); i++) {
		if(six_g_band5[i] < min_ap && six_g_band5[i] >= 0) {
			min_ap = six_g_band5[i];
			*best_6g_ch = i*4 + 1;
		}
	}
	for (i=0; i<ARRAYSIZE(six_g_band6); i++) {
		if(six_g_band6[i] < min_ap && six_g_band6[i] >= 0) {
			min_ap = six_g_band6[i];
			*best_6g_ch = i*4 + 97;
		}
	}
	for (i=0; i<ARRAYSIZE(six_g_band7); i++) {
		if(six_g_band7[i] < min_ap && six_g_band7[i] >= 0) {
			min_ap = six_g_band7[i];
			*best_6g_ch = i*4 + 117;
		}
	}
	for (i=0; i<ARRAYSIZE(six_g_band8); i++) {
		if(six_g_band8[i] < min_ap && six_g_band8[i] >= 0) {
			min_ap = six_g_band8[i];
			*best_6g_ch = i*4 + 189;
		}
	}
#endif /* WL_6G_BAND */

	if (android_msg_level & ANDROID_INFO_LEVEL) {
		struct bcmstrbuf strbuf;
		char *tmp_buf = NULL;
		tmp_buf = kmalloc(WLC_IOCTL_MEDLEN, GFP_KERNEL);
		if (tmp_buf == NULL) {
			AEXT_ERROR(net->name, "Failed to allocate buffer of %d bytes\n", WLC_IOCTL_SMLEN);
			goto exit;
		}
		bcm_binit(&strbuf, tmp_buf, WLC_IOCTL_MEDLEN);
		bcm_bprintf(&strbuf, "2g: ");
		for (j=0; j<ARRAYSIZE(b_band); j++)
			bcm_bprintf(&strbuf, "%d/%d, ", b_band[j], 1+j);
		bcm_bprintf(&strbuf, "\n");
		bcm_bprintf(&strbuf, "5g band 1: ");
		for (j=0; j<ARRAYSIZE(a_band1); j++)
			bcm_bprintf(&strbuf, "%d/%d, ", a_band1[j], 36+j*4);
		bcm_bprintf(&strbuf, "\n");
		bcm_bprintf(&strbuf, "5g band 4: ");
		for (j=0; j<ARRAYSIZE(a_band4); j++)
			bcm_bprintf(&strbuf, "%d/%d, ", a_band4[j], 149+j*4);
		bcm_bprintf(&strbuf, "\n");
#ifdef WL_6G_BAND
		bcm_bprintf(&strbuf, "6g band 5: ");
		for (j=0; j<ARRAYSIZE(six_g_band5); j++)
			bcm_bprintf(&strbuf, "%d/%d, ", six_g_band5[j], 1+j*4);
		bcm_bprintf(&strbuf, "\n");
		bcm_bprintf(&strbuf, "6g band 6: ");
		for (j=0; j<ARRAYSIZE(six_g_band6); j++)
			bcm_bprintf(&strbuf, "%d/%d, ", six_g_band6[j], 97+j*4);
		bcm_bprintf(&strbuf, "\n");
		bcm_bprintf(&strbuf, "6g band 7: ");
		for (j=0; j<ARRAYSIZE(six_g_band7); j++)
			bcm_bprintf(&strbuf, "%d/%d, ", six_g_band7[j], 117+j*4);
		bcm_bprintf(&strbuf, "\n");
		bcm_bprintf(&strbuf, "6g band 8: ");
		for (j=0; j<ARRAYSIZE(six_g_band8); j++)
			bcm_bprintf(&strbuf, "%d/%d, ", six_g_band8[j], 189+j*4);
		bcm_bprintf(&strbuf, "\n");
#endif /* WL_6G_BAND */
		bcm_bprintf(&strbuf, "best_2g_ch=%d, best_5g_ch=%d",
			*best_2g_ch, *best_5g_ch);
#ifdef WL_6G_BAND
		bcm_bprintf(&strbuf, ", best_6g_ch=%d", *best_6g_ch);
#endif /* WL_6G_BAND */
		bcm_bprintf(&strbuf, "\n");
		AEXT_INFO(net->name, "\n%s", strbuf.origbuf);
		if (tmp_buf) {
			kfree(tmp_buf);
		}
	}

exit:
	return 0;
}
#endif /* WL_CFG80211 || WL_ESCAN */

#ifdef WL_CFG80211
#define APCS_MAX_RETRY		10
static int
wl_ext_fw_apcs(struct net_device *dev, uint32 band)
{
	int channel = 0, chosen = 0, retry = 0, ret = 0, spect = 0;
	u8 *reqbuf = NULL;
	uint32 buf_size;
	chanspec_band_t acs_band = WLC_BAND_INVALID;

	ret = wldev_ioctl_get(dev, WLC_GET_SPECT_MANAGMENT, &spect, sizeof(spect));
	if (ret) {
		AEXT_ERROR(dev->name, "ACS: error getting the spect, ret=%d\n", ret);
		goto done;
	}

	if (spect > 0) {
		ret = wl_android_set_spect(dev, 0);
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

	acs_band = wl_ext_wlcband_to_chanspec_band(band);
	if ((uint32)acs_band == WLC_BAND_INVALID) {
		acs_band = WL_CHANSPEC_BAND_2G;
	}

	if ((ret = wl_android_get_band_chanspecs(dev, reqbuf, CHANSPEC_BUF_SIZE,
			acs_band, true)) < 0) {
		WL_ERR(("ACS chanspec retrieval failed!\n"));
		goto done;
	}

	AEXT_INFO(dev->name, "ACS chanspec band 0x%x\n", acs_band);

	buf_size = CHANSPEC_BUF_SIZE;
	ret = wldev_ioctl_set(dev, WLC_START_CHANNEL_SEL, (void *)reqbuf,
		buf_size);
	if (ret < 0) {
		AEXT_ERROR(dev->name, "can't start auto channel scan, err = %d\n", ret);
		channel = 0;
		goto done;
	}

	/* Wait for auto channel selection, max 3000 ms */
	if ((band == WLC_BAND_2G) || (band == WLC_BAND_5G) || (band == WLC_BAND_6G)) {
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

		if (wf_chspec_valid((chanspec_t)chosen)) {
			channel = wf_chspec_ctlchan((chanspec_t)chosen);
			acs_band = CHSPEC_BAND((chanspec_t)chosen);
			WL_MSG(dev->name, "selected channel = %d(band %d)\n",
				channel, CHSPEC2WLC_BAND((chanspec_t)chosen));
			break;
		}
		AEXT_INFO(dev->name, "%d tried, ret = %d, chosen = 0x%x\n",
			(APCS_MAX_RETRY - retry), ret, chosen);
		OSL_SLEEP(250);
	}

done:
	if (spect > 0) {
		if ((ret = wl_android_set_spect(dev, spect) < 0)) {
			AEXT_ERROR(dev->name, "ACS: error while setting spect\n");
		}
	}

	if (reqbuf) {
		kfree(reqbuf);
	}

	return channel;
}
#endif /* WL_CFG80211 */

#ifdef WL_ESCAN
int
wl_ext_drv_scan(struct net_device *dev, uint32 band, bool fast_scan)
{
	int ret = -1, i, cnt = 0;
	int retry = 0, retry_max, retry_interval = 250, up = 1;
	wl_scan_info_t scan_info;

	retry_max = WL_ESCAN_TIMER_INTERVAL_MS/retry_interval;
	ret = wldev_ioctl_get(dev, WLC_GET_UP, &up, sizeof(s32));
	if (ret < 0 || up == 0) {
		ret = wldev_ioctl_set(dev, WLC_UP, &up, sizeof(s32));
	}
	memset(&scan_info, 0, sizeof(wl_scan_info_t));
	if (band == WLC_BAND_2G || band == WLC_BAND_AUTO) {
		for (i=0; i<13; i++) {
			scan_info.channels.channel[i+cnt] = wf_create_chspec_from_primary(i+1,
				WL_CHANSPEC_BW_20, WL_CHANSPEC_BAND_2G);
		}
		cnt += 13;
	}
	if (band == WLC_BAND_5G || band == WLC_BAND_AUTO) {
		for (i=0; i<4; i++) {
			scan_info.channels.channel[i+cnt] = wf_create_chspec_from_primary(36+i*4,
				WL_CHANSPEC_BW_20, WL_CHANSPEC_BAND_5G);
		}
		cnt += 4;
		for (i=0; i<4; i++) {
			scan_info.channels.channel[i+cnt] = wf_create_chspec_from_primary(149+i*4,
				WL_CHANSPEC_BW_20, WL_CHANSPEC_BAND_5G);
		}
		cnt += 4;
	}
#ifdef WL_6G_BAND
	if (band == WLC_BAND_6G || band == WLC_BAND_AUTO) {
		for (i=0; i<59; i++) {
			scan_info.channels.channel[i+cnt] = wf_create_chspec_from_primary(1+i*4,
				WL_CHANSPEC_BW_20, WL_CHANSPEC_BAND_6G);
		}
		cnt += 59;
	}
#endif /* WL_6G_BAND */
	if (band == WLC_BAND_2G)
		fast_scan = FALSE;
	scan_info.channels.count = cnt;
	if (fast_scan)
		scan_info.scan_time = 40;
	scan_info.bcast_ssid = TRUE;
	retry = retry_max;
	while (retry--) {
		ret = wl_escan_set_scan(dev, &scan_info);
		if (!ret)
			break;
		OSL_SLEEP(retry_interval);
	}
	if (retry == 0) {
		AEXT_ERROR(dev->name, "scan retry failed %d\n", retry_max);
		ret = -1;
	}

	return ret;
}

int
wl_ext_drv_apcs(struct net_device *dev, uint32 band)
{
	int ret = 0, channel = 0;
	struct dhd_pub *dhd = dhd_get_pub(dev);
	struct wl_escan_info *escan = NULL;
	int retry = 0, retry_max, retry_interval = 250;

	escan = dhd->escan;
	WL_MSG(dev->name, "ACS_SCAN\n");
	escan->autochannel = 1;
	ret = wl_ext_drv_scan(dev, band, TRUE);
	if (ret < 0)
		goto done;
	retry_max = WL_ESCAN_TIMER_INTERVAL_MS/retry_interval;
	retry = retry_max;
	while (retry--) {
		if (escan->escan_state == ESCAN_STATE_IDLE) {
			if (band == WLC_BAND_5G)
				channel = escan->best_5g_ch;
#ifdef WL_6G_BAND
			else if (band == WLC_BAND_6G)
				channel = escan->best_6g_ch;
#endif /* WL_6G_BAND */
			else
				channel = escan->best_2g_ch;
			WL_MSG(dev->name, "selected channel = %d\n", channel);
			goto done;
		}
		AEXT_INFO(dev->name, "escan_state=%d, %d tried, ret = %d\n",
			escan->escan_state, (retry_max - retry), ret);
		OSL_SLEEP(retry_interval);
	}

done:
	escan->autochannel = 0;

	return channel;
}
#endif /* WL_ESCAN */

int
wl_ext_autochannel(struct net_device *dev, uint acs, uint32 band)
{
	int channel = 0;
	uint16 chan_2g, chan_5g;

	AEXT_INFO(dev->name, "acs=0x%x, band=%d \n", acs, band);

#ifdef WL_CFG80211
	if (acs & ACS_FW_BIT) {
		int ret = 0;
		ret = wldev_ioctl_get(dev, WLC_GET_CHANNEL_SEL, &channel, sizeof(channel));
		channel = 0;
		if (ret != BCME_UNSUPPORTED)
			channel = wl_ext_fw_apcs(dev, band);
		if (channel)
			return channel;
	}
#endif

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
	rssi = dtoh32(scbval.val);

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
		if (now.tv_sec > node->tv.tv_sec || node->dirty > BSSCACHE_DIRTY) {
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

int
wl_bss_cache_size(wl_bss_cache_ctrl_t *bss_cache_ctrl)
{
	wl_bss_cache_t *node, **bss_head;
	int bss_num = 0;

	bss_head = &bss_cache_ctrl->m_cache_head;

	node = *bss_head;
	for (;node;) {
		if (node->dirty > 1) {
			bss_num++;
		}
		node = node->next;
	}
	return bss_num;
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

static void
wl_bss_cache_dump(
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
		k++;
		AEXT_TRACE("wlan", "dump %d with cached BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
			k, &node->results.bss_info->BSSID, rssi, node->results.bss_info->SSID);
		node = node->next;
	}
}

#if defined(SORT_BSS_CHANNEL)
static wl_bss_cache_t *
wl_bss_cache_sort_channel(wl_bss_cache_t **bss_head, wl_bss_cache_t *leaf)
{
	wl_bss_cache_t *node, *prev;
	uint16 channel, channel_node;

	node = *bss_head;
	channel = wf_chspec_ctlchan(leaf->results.bss_info->chanspec);
	for (;node;) {
		channel_node = wf_chspec_ctlchan(node->results.bss_info->chanspec);
		if (channel_node > channel) {
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

	return *bss_head;
}
#endif /* SORT_BSS_CHANNEL */

#if defined(SORT_BSS_RSSI)
static wl_bss_cache_t *
wl_bss_cache_sort_rssi(wl_bss_cache_t **bss_head, wl_bss_cache_t *leaf
#if defined(RSSIAVG)
, wl_rssi_cache_ctrl_t *rssi_cache_ctrl
#endif /* RSSIAVG */
)
{
	wl_bss_cache_t *node, *prev;
	int16 rssi, rssi_node;

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

	return *bss_head;
}
#endif /* SORT_BSS_BY_RSSI */

void
wl_update_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl,
#if defined(RSSIAVG)
	wl_rssi_cache_ctrl_t *rssi_cache_ctrl,
#endif /* RSSIAVG */
	wl_scan_results_t *ss_list)
{
	wl_bss_cache_t *node, *node_target = NULL, *prev, *leaf, **bss_head;
	wl_bss_cache_t *node_rssi_prev = NULL, *node_rssi = NULL;
	wl_bss_info_t *bi = NULL;
	int i, k=0, bss_num = 0;
	struct osl_timespec now, timeout;
	int16 rssi_min;
	bool rssi_replace = FALSE;

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

	// get the num of bss cache
	node = *bss_head;
	for (;node;) {
		node = node->next;
		bss_num++;
	}

	for (i=0; i < ss_list->count; i++) {
		node = *bss_head;
		prev = NULL;
		node_target = NULL;
		node_rssi_prev = NULL;
		bi = bi ? (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length)) : ss_list->bss_info;

		// find the bss with same BSSID
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
		if (node)
			node_target = node;

		// find the bss with lowest RSSI
		if (!node_target && bss_num >= BSSCACHE_MAXCNT) {
			node = *bss_head;
			prev = NULL;
			rssi_min = dtoh16(bi->RSSI);
			for (;node;) {
				if (dtoh16(node->results.bss_info->RSSI) < rssi_min) {
					node_rssi = node;
					node_rssi_prev = prev;
					rssi_min = dtoh16(node->results.bss_info->RSSI);
				}
				prev = node;
				node = node->next;
			}
			if (dtoh16(bi->RSSI) > rssi_min) {
				rssi_replace = TRUE;
				node_target = node_rssi;
				if (node_rssi == *bss_head)
					*bss_head = node_rssi->next;
				else if (node_rssi) {
					node_rssi_prev->next = node_rssi->next;
				}
			}
		}

		k++;
		if (bss_num < BSSCACHE_MAXCNT) {
			bss_num++;
			AEXT_TRACE("wlan",
				"Add %d with cached BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
				k, &bi->BSSID, dtoh16(bi->RSSI), bi->SSID);
		} else if (node_target) {
			if (rssi_replace) {
				AEXT_TRACE("wlan",
					"Replace %d with cached BSSID %pM(%3d) => %pM(%3d), "\
					"SSID \"%s\" => \"%s\"\n",
					k, &node_target->results.bss_info->BSSID,
					dtoh16(node_target->results.bss_info->RSSI),
					&bi->BSSID, dtoh16(bi->RSSI),
					node_target->results.bss_info->SSID, bi->SSID);
			} else {
				AEXT_TRACE("wlan",
					"Update %d with cached BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
					k, &bi->BSSID, dtoh16(bi->RSSI), bi->SSID);
			}
			kfree(node_target);
			node_target = NULL;
		} else {
			AEXT_TRACE("wlan", "Skip %d BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
				k, &bi->BSSID, dtoh16(bi->RSSI), bi->SSID);
			continue;
		}

		leaf = kmalloc(dtoh32(bi->length) + sizeof(wl_bss_cache_t), GFP_KERNEL);
		if (!leaf) {
			AEXT_ERROR("wlan", "Memory alloc failure %d\n",
				dtoh32(bi->length) + (int)sizeof(wl_bss_cache_t));
			return;
		}

		memcpy(leaf->results.bss_info, bi, dtoh32(bi->length));
		leaf->next = NULL;
		leaf->dirty = 0;
		leaf->tv = timeout;
		leaf->results.count = 1;
		leaf->results.version = ss_list->version;

		if (*bss_head == NULL)
			*bss_head = leaf;
		else {
#if defined(SORT_BSS_CHANNEL)
			*bss_head = wl_bss_cache_sort_channel(bss_head, leaf);
#elif defined(SORT_BSS_RSSI)
			*bss_head = wl_bss_cache_sort_rssi(bss_head, leaf
#if defined(RSSIAVG)
				, rssi_cache_ctrl
#endif /* RSSIAVG */
				);
#else
			leaf->next = *bss_head;
			*bss_head = leaf;
#endif /* SORT_BSS_BY_RSSI */
		}
	}
	wl_bss_cache_dump(
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
