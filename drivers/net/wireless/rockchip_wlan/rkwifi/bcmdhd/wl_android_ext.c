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
#endif
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
#endif
#ifdef WL_ESCAN
#include <wl_escan.h>
#endif

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
#endif
#define strtoul(nptr, endptr, base) bcm_strtoul((nptr), (endptr), (base))

#ifndef IW_CUSTOM_MAX
#define IW_CUSTOM_MAX 256 /* size of extra buffer used for translation of events */
#endif /* IW_CUSTOM_MAX */

#define CMD_CHANNEL				"CHANNEL"
#define CMD_CHANNELS			"CHANNELS"
#define CMD_ROAM_TRIGGER		"ROAM_TRIGGER"
#define CMD_KEEP_ALIVE			"KEEP_ALIVE"
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
#ifdef PROP_TXSTATUS
#ifdef PROP_TXSTATUS_VSDB
#include <dhd_wlfc.h>
extern int disable_proptx;
#endif /* PROP_TXSTATUS_VSDB */
#endif
#endif
#ifdef IDHCP
#define CMD_DHCPC_ENABLE	"DHCPC_ENABLE"
#define CMD_DHCPC_DUMP		"DHCPC_DUMP"
#endif
#define CMD_AUTOCHANNEL		"AUTOCHANNEL"
#define CMD_WL		"WL"

int wl_ext_ioctl(struct net_device *dev, u32 cmd, void *arg, u32 len, u32 set)
{
	int ret;

	ret = wldev_ioctl(dev, cmd, arg, len, set);
	if (ret)
		ANDROID_ERROR(("%s: cmd=%d ret=%d\n", __FUNCTION__, cmd, ret));
	return ret;
}

int wl_ext_iovar_getint(struct net_device *dev, s8 *iovar, s32 *val)
{
	int ret;

	ret = wldev_iovar_getint(dev, iovar, val);
	if (ret)
		ANDROID_ERROR(("%s: iovar=%s, ret=%d\n", __FUNCTION__, iovar, ret));

	return ret;
}

int wl_ext_iovar_setint(struct net_device *dev, s8 *iovar, s32 val)
{
	int ret;

	ret = wldev_iovar_setint(dev, iovar, val);
	if (ret)
		ANDROID_ERROR(("%s: iovar=%s, ret=%d\n", __FUNCTION__, iovar, ret));

	return ret;
}

int wl_ext_iovar_getbuf(struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, struct mutex* buf_sync)
{
	int ret;

	ret = wldev_iovar_getbuf(dev, iovar_name, param, paramlen, buf, buflen, buf_sync);
	if (ret != 0)
		ANDROID_ERROR(("%s: iovar=%s, ret=%d\n", __FUNCTION__, iovar_name, ret));

	return ret;
}

int wl_ext_iovar_setbuf(struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, struct mutex* buf_sync)
{
	int ret;

	ret = wldev_iovar_setbuf(dev, iovar_name, param, paramlen, buf, buflen, buf_sync);
	if (ret != 0)
		ANDROID_ERROR(("%s: iovar=%s, ret=%d\n", __FUNCTION__, iovar_name, ret));

	return ret;
}

static int wl_ext_iovar_setbuf_bsscfg(struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, s32 bsscfg_idx,
	struct mutex* buf_sync)
{
	int ret;

	ret = wldev_iovar_setbuf_bsscfg(dev, iovar_name, param, paramlen,
		buf, buflen, bsscfg_idx, buf_sync);
	if (ret < 0)
		ANDROID_ERROR(("%s: iovar_name=%s ret=%d\n", __FUNCTION__, iovar_name, ret));

	return ret;
}

#ifdef WL_EXT_IAPSTA
typedef enum IF_STATE {
	IF_STATE_INIT = 1,
	IF_STATE_DISALBE,
	IF_STATE_ENABLE
} if_state_t;

typedef enum APSTAMODE {
	ISTAONLY_MODE = 1,
	IAPONLY_MODE,
	IAPSTA_MODE,
	IDUALAP_MODE,
	ISTAAPAP_MODE,
	IMESHONLY_MODE,
	IMESHSTA_MODE,
	IMESHAP_MODE,
	IMESHAPSTA_MODE,
	IMESHAPAP_MODE,
	IGOSTA_MODE
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
}wl_prio_t;

typedef struct wl_if_info {
	struct net_device *dev;
	if_state_t ifstate;
	ifmode_t ifmode;
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
} wl_if_info_t;

#define CSA_FW_BIT		(1<<0)
#define CSA_DRV_BIT		(1<<1)

typedef struct wl_apsta_params {
	struct wl_if_info if_info[MAX_IF_NUM];
	struct dhd_pub *dhd;
	int ioctl_ver;
	bool init;
	bool rsdb;
	bool vsdb;
	uint csa;
	apstamode_t apstamode;
	bool netif_change;
	wait_queue_head_t netif_change_event;
} wl_apsta_params_t;

static int wl_ext_enable_iface(struct net_device *dev, char *ifname);
#endif

/* Return a legacy chanspec given a new chanspec
 * Returns INVCHANSPEC on error
 */
static chanspec_t
wl_ext_chspec_to_legacy(chanspec_t chspec)
{
	chanspec_t lchspec;

	if (wf_chspec_malformed(chspec)) {
		ANDROID_ERROR(("wl_ext_chspec_to_legacy: input chanspec (0x%04X) malformed\n",
		        chspec));
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
		ANDROID_ERROR((
		        "wl_ext_chspec_to_legacy: unable to convert chanspec %s (0x%04X) "
		        "to pre-11ac format\n",
		        wf_chspec_ntoa(chspec, chanbuf), chspec));
		return INVCHANSPEC;
	}

	return lchspec;
}

/* given a chanspec value, do the endian and chanspec version conversion to
 * a chanspec_t value
 * Returns INVCHANSPEC on error
 */
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
		ANDROID_TRACE(("join_params->params.chanspec_list[0]= %X, %d channels\n",
			join_params->params.chanspec_list[0],
			join_params->params.chanspec_num));
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
		ANDROID_ERROR(("wl_ext_chspec_from_legacy: output chanspec (0x%04X) malformed\n",
		        chspec));
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
#endif

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
		ANDROID_ERROR(("Version mismatch, please upgrade. Got %d, expected %d or 1\n",
			val, WLC_IOCTL_VERSION));
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
				ANDROID_ERROR(("bw_cap failed, %d\n", err));
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
				wl_ext_ioctl(dev, WLC_SET_CHANNEL, &_chan, sizeof(_chan), 1);
				printf("%s: channel %d\n", __FUNCTION__, _chan);
			} else if (err) {
				ANDROID_ERROR(("%s: failed to set chanspec error %d\n",
					__FUNCTION__, err));
			} else
				printf("%s: %s channel %d, 0x%x\n", __FUNCTION__,
					dev->name, channel, chspec);
		} else {
			ANDROID_ERROR(("%s: failed to convert host chanspec to fw chanspec\n",
				__FUNCTION__));
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
		ANDROID_ERROR(("%s: Invalid chanspec 0x%x\n", __FUNCTION__, chspec));
		err = BCME_ERROR;
	}
	*ret_chspec = fw_chspec;

	return err;
}

int
wl_ext_channel(struct net_device *dev, char* command, int total_len)
{
	int ret;
	int channel=0;
	channel_info_t ci;
	int bytes_written = 0;
	chanspec_t fw_chspec;
	int ioctl_ver = 0;

	ANDROID_TRACE(("%s: cmd %s\n", __FUNCTION__, command));

	sscanf(command, "%*s %d", &channel);

	if (channel > 0) {
		wl_ext_get_ioctl_ver(dev, &ioctl_ver);
		ret = wl_ext_set_chanspec(dev, ioctl_ver, channel, &fw_chspec);
	} else {
		if (!(ret = wl_ext_ioctl(dev, WLC_GET_CHANNEL, &ci,
				sizeof(channel_info_t), FALSE))) {
			ANDROID_TRACE(("hw_channel %d\n", ci.hw_channel));
			ANDROID_TRACE(("target_channel %d\n", ci.target_channel));
			ANDROID_TRACE(("scan_channel %d\n", ci.scan_channel));
			bytes_written = snprintf(command, sizeof(channel_info_t)+2,
				"channel %d", ci.hw_channel);
			ANDROID_TRACE(("%s: command result is %s\n", __FUNCTION__, command));
			ret = bytes_written;
		}
	}

	return ret;
}

int
wl_ext_channels(struct net_device *dev, char* command, int total_len)
{
	int ret, i;
	int bytes_written = -1;
	u8 valid_chan_list[sizeof(u32)*(WL_NUMCHANNELS + 1)];
	wl_uint32_list_t *list;

	ANDROID_TRACE(("%s: cmd %s\n", __FUNCTION__, command));

	memset(valid_chan_list, 0, sizeof(valid_chan_list));
	list = (wl_uint32_list_t *)(void *) valid_chan_list;
	list->count = htod32(WL_NUMCHANNELS);
	ret = wl_ext_ioctl(dev, WLC_GET_VALID_CHANNELS, valid_chan_list,
		sizeof(valid_chan_list), 0);
	if (ret<0) {
		ANDROID_ERROR(("%s: get channels failed with %d\n", __FUNCTION__, ret));
	} else {
		bytes_written = snprintf(command, total_len, "channels");
		for (i = 0; i < dtoh32(list->count); i++) {
			bytes_written += snprintf(command+bytes_written, total_len, " %d",
				dtoh32(list->element[i]));
			printf("%d ", dtoh32(list->element[i]));
		}
		printf("\n");
		ret = bytes_written;
	}

	return ret;
}

int
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

		ANDROID_TRACE(("roam_trigger %d %d\n", trigger[0], trigger[1]));
		bytes_written = snprintf(command, total_len, "%d %d", trigger[0], trigger[1]);
		ret = bytes_written;
	}

	return ret;
}

static int
wl_ext_pattern_atoh(char *src, char *dst)
{
	int i;
	if (strncmp(src, "0x", 2) != 0 &&
	    strncmp(src, "0X", 2) != 0) {
		ANDROID_ERROR(("Mask invalid format. Needs to start with 0x\n"));
		return -1;
	}
	src = src + 2; /* Skip past 0x */
	if (strlen(src) % 2 != 0) {
		DHD_ERROR(("Mask invalid format. Needs to be of even length\n"));
		return -1;
	}
	for (i = 0; *src != '\0'; i++) {
		char num[3];
		bcm_strncpy_s(num, sizeof(num), src, 2);
		num[2] = '\0';
		dst[i] = (uint8)strtoul(num, NULL, 16);
		src += 2;
	}
	return i;
}

int
wl_ext_keep_alive(struct net_device *dev, char *command, int total_len)
{
	wl_mkeep_alive_pkt_t *mkeep_alive_pktp;
	int ret = -1, i;
	int	id, period=-1, len_bytes=0, buf_len=0;
	char data[200]="\0";
	char buf[WLC_IOCTL_SMLEN]="\0", iovar_buf[WLC_IOCTL_SMLEN]="\0";
	int bytes_written = -1;

	ANDROID_TRACE(("%s: command = %s\n", __FUNCTION__, command));
	sscanf(command, "%*s %d %d %s", &id, &period, data);
	ANDROID_TRACE(("%s: id=%d, period=%d, data=%s\n", __FUNCTION__, id, period, data));

	if (period >= 0) {
		mkeep_alive_pktp = (wl_mkeep_alive_pkt_t *)buf;
		mkeep_alive_pktp->version = htod16(WL_MKEEP_ALIVE_VERSION);
		mkeep_alive_pktp->length = htod16(WL_MKEEP_ALIVE_FIXED_LEN);
		mkeep_alive_pktp->keep_alive_id = id;
		buf_len += WL_MKEEP_ALIVE_FIXED_LEN;
		mkeep_alive_pktp->period_msec = period;
		if (strlen(data)) {
			len_bytes = wl_ext_pattern_atoh(data, (char *) mkeep_alive_pktp->data);
			buf_len += len_bytes;
		}
		mkeep_alive_pktp->len_bytes = htod16(len_bytes);

		ret = wl_ext_iovar_setbuf(dev, "mkeep_alive", buf, buf_len,
			iovar_buf, sizeof(iovar_buf), NULL);
	} else {
		if (id < 0)
			id = 0;
		ret = wl_ext_iovar_getbuf(dev, "mkeep_alive", &id, sizeof(id), buf,
			sizeof(buf), NULL);
		if (ret) {
			goto exit;
		} else {
			mkeep_alive_pktp = (wl_mkeep_alive_pkt_t *) buf;
			printf("Id            :%d\n"
					"Period (msec) :%d\n"
					"Length        :%d\n"
					"Packet        :0x",
					mkeep_alive_pktp->keep_alive_id,
					dtoh32(mkeep_alive_pktp->period_msec),
					dtoh16(mkeep_alive_pktp->len_bytes));
			for (i=0; i<mkeep_alive_pktp->len_bytes; i++) {
				printf("%02x", mkeep_alive_pktp->data[i]);
			}
			printf("\n");
		}
		bytes_written = snprintf(command, total_len, "mkeep_alive_period_msec %d ",
			dtoh32(mkeep_alive_pktp->period_msec));
		bytes_written += snprintf(command+bytes_written, total_len, "0x");
		for (i=0; i<mkeep_alive_pktp->len_bytes; i++) {
			bytes_written += snprintf(command+bytes_written, total_len, "%x",
				mkeep_alive_pktp->data[i]);
		}
		ANDROID_TRACE(("%s: command result is %s\n", __FUNCTION__, command));
		ret = bytes_written;
	}

exit:
	return ret;
}

int
wl_ext_pm(struct net_device *dev, char *command, int total_len)
{
	int pm=-1, ret = -1;
	char *pm_local;
	int bytes_written=-1;

	ANDROID_TRACE(("%s: cmd %s\n", __FUNCTION__, command));

	sscanf(command, "%*s %d", &pm);

	if (pm >= 0) {
		ret = wl_ext_ioctl(dev, WLC_SET_PM, &pm, sizeof(pm), 1);
	} else {
		ret = wl_ext_ioctl(dev, WLC_GET_PM, &pm, sizeof(pm), 0);
		if (!ret) {
			ANDROID_TRACE(("%s: PM = %d\n", __func__, pm));
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
			ANDROID_TRACE(("%s: command result is %s\n", __FUNCTION__, command));
			ret = bytes_written;
		}
	}

	return ret;
}

static int
wl_ext_monitor(struct net_device *dev, char *command, int total_len)
{
	int val, ret = -1;
	int bytes_written=-1;

	sscanf(command, "%*s %d", &val);

	if (val >=0) {
		ret = wl_ext_ioctl(dev, WLC_SET_MONITOR, &val, sizeof(val), 1);
	} else {
		ret = wl_ext_ioctl(dev, WLC_GET_MONITOR, &val, sizeof(val), 0);
		if (!ret) {
			ANDROID_TRACE(("%s: monitor = %d\n", __FUNCTION__, val));
			bytes_written = snprintf(command, total_len, "monitor %d", val);
			ANDROID_TRACE(("%s: command result is %s\n", __FUNCTION__, command));
			ret = bytes_written;
		}
	}

	return ret;
}

s32
wl_ext_connect(struct net_device *dev, struct wl_conn_info *conn_info)
{
	wl_extjoin_params_t *ext_join_params;
	struct wl_join_params join_params;
	size_t join_params_size;
	s32 err = 0;
	u32 chan_cnt = 0;
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	int ioctl_ver = 0;

	if (conn_info->channel) {
		chan_cnt = 1;
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
	wl_ext_get_ioctl_ver(dev, &ioctl_ver);
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

	err = wl_ext_iovar_setbuf_bsscfg(dev, "join", ext_join_params,
		join_params_size, iovar_buf, WLC_IOCTL_SMLEN, conn_info->bssidx, NULL);

	printf("Connecting with " MACDBG " channel (%d) ssid \"%s\", len (%d)\n\n",
		MAC2STRDBG((u8*)(&ext_join_params->assoc.bssid)), conn_info->channel,
		ext_join_params->ssid.SSID, ext_join_params->ssid.SSID_len);

	kfree(ext_join_params);
	if (err) {
		if (err == BCME_UNSUPPORTED) {
			ANDROID_TRACE(("join iovar is not supported\n"));
			goto set_ssid;
		} else {
			ANDROID_ERROR(("error (%d)\n", err));
			goto exit;
		}
	} else
		goto exit;

set_ssid:
	memset(&join_params, 0, sizeof(join_params));
	join_params_size = sizeof(join_params.ssid);

	join_params.ssid.SSID_len = min((uint32)sizeof(join_params.ssid.SSID), conn_info->ssid.SSID_len);
	memcpy(&join_params.ssid.SSID, conn_info->ssid.SSID, join_params.ssid.SSID_len);
	join_params.ssid.SSID_len = htod32(join_params.ssid.SSID_len);
	if (memcmp(&ether_null, &conn_info->bssid, ETHER_ADDR_LEN))
		memcpy(&join_params.params.bssid, &conn_info->bssid, ETH_ALEN);
	else
		memcpy(&join_params.params.bssid, &ether_bcast, ETH_ALEN);

	wl_ext_ch_to_chanspec(ioctl_ver, conn_info->channel, &join_params, &join_params_size);
	ANDROID_TRACE(("join_param_size %zu\n", join_params_size));

	if (join_params.ssid.SSID_len < IEEE80211_MAX_SSID_LEN) {
		ANDROID_INFO(("ssid \"%s\", len (%d)\n", join_params.ssid.SSID,
			join_params.ssid.SSID_len));
	}
	err = wl_ext_ioctl(dev, WLC_SET_SSID, &join_params,join_params_size, 1);

exit:
	return err;

}

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
		ANDROID_TRACE(("%s: Network mode: B only\n", __FUNCTION__));
	} else if (bgnmode == IEEE80211G) {
		wl_ext_iovar_setint(dev, "nmode", 0);
		val = 2;
		wl_ext_ioctl(dev, WLC_SET_GMODE, &val, sizeof(val), 1);
		ANDROID_TRACE(("%s: Network mode: G only\n", __FUNCTION__));
	} else if (bgnmode == IEEE80211BG) {
		wl_ext_iovar_setint(dev, "nmode", 0);
		val = 1;
		wl_ext_ioctl(dev, WLC_SET_GMODE, &val, sizeof(val), 1);
		ANDROID_TRACE(("%s: Network mode: B/G mixed\n", __FUNCTION__));
	} else if (bgnmode == IEEE80211BGN) {
		wl_ext_iovar_setint(dev, "nmode", 0);
		wl_ext_iovar_setint(dev, "nmode", 1);
		wl_ext_iovar_setint(dev, "vhtmode", 0);
		val = 1;
		wl_ext_ioctl(dev, WLC_SET_GMODE, &val, sizeof(val), 1);
		ANDROID_TRACE(("%s: Network mode: B/G/N mixed\n", __FUNCTION__));
	} else if (bgnmode == IEEE80211BGNAC) {
		wl_ext_iovar_setint(dev, "nmode", 0);
		wl_ext_iovar_setint(dev, "nmode", 1);
		wl_ext_iovar_setint(dev, "vhtmode", 1);
		val = 1;
		wl_ext_ioctl(dev, WLC_SET_GMODE, &val, sizeof(val), 1);
		ANDROID_TRACE(("%s: Network mode: B/G/N/AC mixed\n", __FUNCTION__));
	}
	wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);

	return 0;
}

static void
wl_ext_get_amode(struct wl_if_info *cur_if, char *amode)
{
	struct net_device *dev = cur_if->dev;
	int auth=-1, wpa_auth=-1;

	wl_ext_iovar_getint(dev, "auth", &auth);
	wl_ext_iovar_getint(dev, "wpa_auth", &wpa_auth);

#ifdef WLMESH
	if (cur_if->ifmode == IMESH_MODE) {
		if (auth == 0 && wpa_auth == 0) {
			strcpy(amode, "open");
		} else if (auth == 0 && wpa_auth == 128) {
			strcpy(amode, "sae");
		}
	} else
#endif
	if (auth == 0 && wpa_auth == 0) {
		strcpy(amode, "open");
	} else if (auth == 1 && wpa_auth == 0) {
		strcpy(amode, "shared");
	} else if (auth == 0 && wpa_auth == 4) {
		strcpy(amode, "wpapsk");
	} else if (auth == 0 && wpa_auth == 128) {
		strcpy(amode, "wpa2psk");
	} else if (auth == 0 && wpa_auth == 132) {
		strcpy(amode, "wpawpa2psk");
	}
}

static void
wl_ext_get_emode(struct wl_if_info *cur_if, char *emode)
{
	struct net_device *dev = cur_if->dev;
	int wsec=0;

	wl_ext_iovar_getint(dev, "wsec", &wsec);

#ifdef WLMESH
	if (cur_if->ifmode == IMESH_MODE) {
		if (wsec == 0) {
			strcpy(emode, "none");
		} else {
			strcpy(emode, "sae");
		}
	} else
#endif
	if (wsec == 0) {
		strcpy(emode, "none");
	} else if (wsec == 1) {
		strcpy(emode, "wep");
	} else if (wsec == 2 || wsec == 10) {
		strcpy(emode, "tkip");
	} else if (wsec == 4 || wsec == 12) {
		strcpy(emode, "aes");
	} else if (wsec == 6 || wsec == 14) {
		strcpy(emode, "tkipaes");
	}
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
			auth = 0;
			wpa_auth = 128;
			ANDROID_INFO(("%s: Authentication: SAE\n", __FUNCTION__));
		} else {
			auth = 0;
			wpa_auth = 0;
			ANDROID_INFO(("%s: Authentication: Open System\n", __FUNCTION__));
		}
	} else
#endif
	if (amode == AUTH_OPEN) {
		auth = 0;
		wpa_auth = 0;
		ANDROID_INFO(("%s: Authentication: Open System\n", __FUNCTION__));
	} else if (amode == AUTH_SHARED) {
		auth = 1;
		wpa_auth = 0;
		ANDROID_INFO(("%s: Authentication: Shared Key\n", __FUNCTION__));
	} else if (amode == AUTH_WPAPSK) {
		auth = 0;
		wpa_auth = 4;
		ANDROID_INFO(("%s: Authentication: WPA-PSK\n", __FUNCTION__));
	} else if (amode == AUTH_WPA2PSK) {
		auth = 0;
		wpa_auth = 128;
		ANDROID_INFO(("%s: Authentication: WPA2-PSK\n", __FUNCTION__));
	} else if (amode == AUTH_WPAWPA2PSK) {
		auth = 0;
		wpa_auth = 132;
		ANDROID_INFO(("%s: Authentication: WPA/WPA2-PSK\n", __FUNCTION__));
	}
#ifdef WLMESH
	if (cur_if->ifmode == IMESH_MODE) {
		s32 val = WL_BSSTYPE_MESH;
		wl_ext_ioctl(dev, WLC_SET_INFRA, &val, sizeof(val), 1);
	} else
#endif
	if (cur_if->ifmode == ISTA_MODE) {
		s32 val = WL_BSSTYPE_INFRA;
		wl_ext_ioctl(dev, WLC_SET_INFRA, &val, sizeof(val), 1);
	}
	wl_ext_iovar_setint(dev, "auth", auth);

	wl_ext_iovar_setint(dev, "wpa_auth", wpa_auth);

	return 0;
}

static int
wl_ext_set_emode(struct wl_apsta_params *apsta_params, struct wl_if_info *cur_if)
{
	struct net_device *dev = cur_if->dev;
	int wsec=0;
	struct wl_wsec_key wsec_key;
	wsec_pmk_t psk;
	authmode_t amode = cur_if->amode;
	encmode_t emode = cur_if->emode;
	char *key = cur_if->key;
	struct dhd_pub *dhd = dhd_get_pub(dev);

	memset(&wsec_key, 0, sizeof(wsec_key));
	memset(&psk, 0, sizeof(psk));

#ifdef WLMESH
	if (cur_if->ifmode == IMESH_MODE) {
		if (amode == AUTH_SAE) {
			wsec = 4;
			ANDROID_INFO(("%s: Encryption: AES\n", __FUNCTION__));
		} else {
			wsec = 0;
			ANDROID_INFO(("%s: Encryption: No securiy\n", __FUNCTION__));
		}
	} else
#endif
	if (emode == ENC_NONE) {
		wsec = 0;
		ANDROID_INFO(("%s: Encryption: No securiy\n", __FUNCTION__));
	} else if (emode == ENC_WEP) {
		wsec = 1;
		wl_ext_parse_wep(key, &wsec_key);
		ANDROID_INFO(("%s: Encryption: WEP\n", __FUNCTION__));
		ANDROID_INFO(("%s: Key: \"%s\"\n", __FUNCTION__, wsec_key.data));
	} else if (emode == ENC_TKIP) {
		wsec = 2;
		psk.key_len = strlen(key);
		psk.flags = WSEC_PASSPHRASE;
		memcpy(psk.key, key, strlen(key));
		ANDROID_INFO(("%s: Encryption: TKIP\n", __FUNCTION__));
		ANDROID_INFO(("%s: Key: \"%s\"\n", __FUNCTION__, psk.key));
	} else if (emode == ENC_AES || amode == AUTH_SAE) {
		wsec = 4;
		psk.key_len = strlen(key);
		psk.flags = WSEC_PASSPHRASE;
		memcpy(psk.key, key, strlen(key));
		ANDROID_INFO(("%s: Encryption: AES\n", __FUNCTION__));
		ANDROID_INFO(("%s: Key: \"%s\"\n", __FUNCTION__, psk.key));
	} else if (emode == ENC_TKIPAES) {
		wsec = 6;
		psk.key_len = strlen(key);
		psk.flags = WSEC_PASSPHRASE;
		memcpy(psk.key, key, strlen(key));
		ANDROID_INFO(("%s: Encryption: TKIP/AES\n", __FUNCTION__));
		ANDROID_INFO(("%s: Key: \"%s\"\n", __FUNCTION__, psk.key));
	}
	if (dhd->conf->chip == BCM43430_CHIP_ID && cur_if->ifidx > 0 && wsec >= 2 &&
			apsta_params->apstamode == IAPSTA_MODE) {
		wsec |= 0x8; // terence 20180628: fix me, this is a workaround
	}

	wl_ext_iovar_setint(dev, "wsec", wsec);

#ifdef WLMESH
	if (cur_if->ifmode == IMESH_MODE) {
		if (amode == AUTH_SAE) {
			s8 iovar_buf[WLC_IOCTL_SMLEN];
			ANDROID_INFO(("%s: Key: \"%s\"\n", __FUNCTION__, key));
			wl_ext_iovar_setint(dev, "mesh_auth_proto", 1);
			wl_ext_iovar_setint(dev, "mfp", WL_MFP_REQUIRED);
			wl_ext_iovar_setbuf(dev, "sae_password", key, strlen(key),
				iovar_buf, WLC_IOCTL_SMLEN, NULL);
		} else {
			wl_ext_iovar_setint(dev, "mesh_auth_proto", 0);
			wl_ext_iovar_setint(dev, "mfp", WL_MFP_NONE);
		}
	} else
#endif
	if (emode == ENC_WEP) {
		wl_ext_ioctl(dev, WLC_SET_KEY, &wsec_key, sizeof(wsec_key), 1);
	} else if (emode == ENC_TKIP || emode == ENC_AES || emode == ENC_TKIPAES) {
		if (dev) {
			if (cur_if->ifmode == ISTA_MODE)
				wl_ext_iovar_setint(dev, "sup_wpa", 1);
			wl_ext_ioctl(dev, WLC_SET_WSEC_PMK, &psk, sizeof(psk), 1);
		} else {
			ANDROID_ERROR(("%s: apdev is null\n", __FUNCTION__));
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
	
	ret = wl_ext_ioctl(dev, WLC_GET_BSSID, &bssid, sizeof(bssid), 0);
	if (ret != BCME_NOTASSOCIATED && memcmp(&ether_null, &bssid, ETHER_ADDR_LEN)) {
		if (wl_ext_iovar_getint(dev, "chanspec", (s32 *)&chanspec) == BCME_OK) {
			chanspec = wl_ext_chspec_driver_to_host(apsta_params->ioctl_ver, chanspec);
			ctl_chan = wf_chspec_ctlchan(chanspec);
			chan = (u16)(ctl_chan & 0x00FF);
			ANDROID_INFO(("%s: cur_chan=%d(0x%x)\n", __FUNCTION__,
				chan, chanspec));
			return chan;
		}
	}

	return 0;
}

static chanspec_t
wl_ext_get_chanspec(struct wl_apsta_params *apsta_params,
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
				ANDROID_ERROR(("bw_cap failed, %d\n", err));
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
			ANDROID_ERROR(("%s: failed to convert host chanspec to fw chanspec\n",
				__FUNCTION__));
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
		ANDROID_ERROR(("%s: Invalid chanspec 0x%x\n", __FUNCTION__, chspec));
		err = BCME_ERROR;
	}

	return fw_chspec;
}

static void
wl_ext_wait_netif_change(struct wl_apsta_params *apsta_params,
	bool need_rtnl_unlock)
{
	if (need_rtnl_unlock)
		rtnl_unlock();
	wait_event_interruptible_timeout(apsta_params->netif_change_event,
		apsta_params->netif_change, msecs_to_jiffies(1500));
	if (need_rtnl_unlock)
		rtnl_lock();
}

static void
wl_ext_iapsta_preinit(struct net_device *dev, struct wl_apsta_params *apsta_params)
{
	struct dhd_pub *dhd;
	apstamode_t apstamode = apsta_params->apstamode;
	wl_interface_create_t iface;
	struct wl_if_info *cur_if;
	wlc_ssid_t ssid = { 0, {0} };
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	wl_country_t cspec = {{0}, 0, {0}};
	wl_p2p_if_t ifreq;
	s32 val = 0;
	int i, dfs = 1;

	dhd = dhd_get_pub(dev);

	for (i=0; i<MAX_IF_NUM; i++) {
		cur_if = &apsta_params->if_info[i];
		if (i == 1 && !strlen(cur_if->ifname))
			strcpy(cur_if->ifname, "wlan1");
		if (i == 2 && !strlen(cur_if->ifname))
			strcpy(cur_if->ifname, "wlan2");
		if (cur_if->ifmode == ISTA_MODE) {
			cur_if->channel = 0;
			cur_if->maxassoc = -1;
			cur_if->ifstate = IF_STATE_INIT;
			cur_if->prio = PRIO_STA;
			cur_if->prefix = 'S';
			snprintf(cur_if->ssid, DOT11_MAX_SSID_LEN, "ttt_sta");
		} else if (cur_if->ifmode == IAP_MODE) {
			cur_if->channel = 1;
			cur_if->maxassoc = -1;
			cur_if->ifstate = IF_STATE_INIT;
			cur_if->prio = PRIO_AP;
			cur_if->prefix = 'A';
			snprintf(cur_if->ssid, DOT11_MAX_SSID_LEN, "ttt_ap");
			dfs = 0;
#ifdef WLMESH
		} else if (cur_if->ifmode == IMESH_MODE) {
			cur_if->channel = 1;
			cur_if->maxassoc = -1;
			cur_if->ifstate = IF_STATE_INIT;
			cur_if->prio = PRIO_MESH;
			cur_if->prefix = 'M';
			snprintf(cur_if->ssid, DOT11_MAX_SSID_LEN, "ttt_mesh");
			dfs = 0;
#endif
		}
	}

	if (!dfs && !apsta_params->vsdb) {
		dhd_conf_get_country(dhd, &cspec);
		if (!dhd_conf_map_country_list(dhd, &cspec)) {
			dhd_conf_set_country(dhd, &cspec);
			dhd_bus_country_set(dev, &cspec, TRUE);
		}
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "dfs_chan_disable", 1);
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
	}

	if (FW_SUPPORTED(dhd, rsdb)) {
		if (apstamode == IDUALAP_MODE)
			apsta_params->rsdb = TRUE;
		else if (apstamode == ISTAAPAP_MODE)
			apsta_params->rsdb = FALSE;
		if (apstamode == IDUALAP_MODE || apstamode == ISTAAPAP_MODE ||
			apstamode == IMESHONLY_MODE || apstamode == IMESHSTA_MODE ||
			apstamode == IMESHAP_MODE || apstamode == IMESHAPSTA_MODE ||
			apstamode == IMESHAPAP_MODE) {
			wl_config_t rsdb_mode_cfg = {0, 0};
			if (apsta_params->rsdb)
				rsdb_mode_cfg.config = 1;
			printf("%s: set rsdb_mode %d\n", __FUNCTION__, rsdb_mode_cfg.config);
			wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
			wl_ext_iovar_setbuf(dev, "rsdb_mode", &rsdb_mode_cfg,
				sizeof(rsdb_mode_cfg), iovar_buf, sizeof(iovar_buf), NULL);
			wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		}
	} else {
		apsta_params->rsdb = FALSE;
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
		if (!FW_SUPPORTED(dhd, rsdb) && !disable_proptx) {
			bool enabled;
			dhd_wlfc_get_enable(dhd, &enabled);
			if (!enabled) {
				dhd_wlfc_init(dhd);
				wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
			}
		}
#endif
#endif /* PROP_TXSTATUS_VSDB */
	}
	else if (apstamode == IAPSTA_MODE) {
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "mpc", 0);
		wl_ext_iovar_setint(dev, "apsta", 1);
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		apsta_params->netif_change = FALSE;
		if (FW_SUPPORTED(dhd, rsdb)) {
			bzero(&iface, sizeof(wl_interface_create_t));
			iface.ver = WL_INTERFACE_CREATE_VER;
			iface.flags = WL_INTERFACE_CREATE_AP;
			wl_ext_iovar_getbuf(dev, "interface_create", &iface,
				sizeof(iface), iovar_buf, WLC_IOCTL_SMLEN, NULL);
		} else {
			wl_ext_iovar_setbuf_bsscfg(dev, "ssid", &ssid, sizeof(ssid),
				iovar_buf, WLC_IOCTL_SMLEN, 1, NULL);
		}
		wl_ext_wait_netif_change(apsta_params, TRUE);
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
		bzero(&iface, sizeof(wl_interface_create_t));
		iface.ver = WL_INTERFACE_CREATE_VER;
		iface.flags = WL_INTERFACE_CREATE_AP;
		apsta_params->netif_change = FALSE;
		wl_ext_iovar_getbuf(dev, "interface_create", &iface, sizeof(iface),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		wl_ext_wait_netif_change(apsta_params, TRUE);
	}
	else if (apstamode == ISTAAPAP_MODE) {
		u8 rand_bytes[2] = {0, };
		get_random_bytes(&rand_bytes, sizeof(rand_bytes));
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "mpc", 0);
		wl_ext_iovar_setint(dev, "mbss", 1);
		wl_ext_iovar_setint(dev, "apsta", 1); // keep 1 as we set in dhd_preinit_ioctls
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		// don't set WLC_SET_AP to 0, some parameters will be reset, such as bcn_timeout and roam_off
		bzero(&iface, sizeof(wl_interface_create_t));
		iface.ver = WL_INTERFACE_CREATE_VER;
		iface.flags = WL_INTERFACE_CREATE_AP | WL_INTERFACE_MAC_USE;
		memcpy(&iface.mac_addr, dev->dev_addr, ETHER_ADDR_LEN);
		iface.mac_addr.octet[0] |= 0x02;
		iface.mac_addr.octet[5] += 0x01;
		memcpy(&iface.mac_addr.octet[3], rand_bytes, sizeof(rand_bytes));
		apsta_params->netif_change = FALSE;
		wl_ext_iovar_getbuf(dev, "interface_create", &iface, sizeof(iface),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		wl_ext_wait_netif_change(apsta_params, TRUE);
		bzero(&iface, sizeof(wl_interface_create_t));
		iface.ver = WL_INTERFACE_CREATE_VER;
		iface.flags = WL_INTERFACE_CREATE_AP | WL_INTERFACE_MAC_USE;
		memcpy(&iface.mac_addr, dev->dev_addr, ETHER_ADDR_LEN);
		iface.mac_addr.octet[0] |= 0x02;
		iface.mac_addr.octet[5] += 0x02;
		memcpy(&iface.mac_addr.octet[3], rand_bytes, sizeof(rand_bytes));
		apsta_params->netif_change = FALSE;
		wl_ext_iovar_getbuf(dev, "interface_create", &iface, sizeof(iface),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		wl_ext_wait_netif_change(apsta_params, TRUE);
	}
#ifdef WLMESH
	else if (apstamode == IMESHONLY_MODE) {
		int pm = 0;
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "mpc", 0);
		wl_ext_iovar_setint(dev, "apsta", 1); // keep 1 as we set in dhd_preinit_ioctls
		wl_ext_ioctl(dev, WLC_SET_PM, &pm, sizeof(pm), 1);
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		// don't set WLC_SET_AP to 0, some parameters will be reset, such as bcn_timeout and roam_off
	}
	else if (apstamode == IMESHSTA_MODE) {
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "mpc", 0);
		wl_ext_iovar_setint(dev, "mbcn", 1);
		wl_ext_iovar_setint(dev, "apsta", 1);
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		bzero(&iface, sizeof(wl_interface_create_t));
		iface.ver = WL_INTERFACE_CREATE_VER;
		iface.flags = WL_INTERFACE_CREATE_STA;
		apsta_params->netif_change = FALSE;
		wl_ext_iovar_getbuf(dev, "interface_create", &iface, sizeof(iface),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		wl_ext_wait_netif_change(apsta_params, TRUE);
	}
	else if (apstamode == IMESHAP_MODE) {
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "mpc", 0);
		wl_ext_iovar_setint(dev, "mbcn", 1);
		wl_ext_iovar_setint(dev, "apsta", 1); // keep 1 as we set in dhd_preinit_ioctls
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		// don't set WLC_SET_AP to 0, some parameters will be reset, such as bcn_timeout and roam_off
		bzero(&iface, sizeof(wl_interface_create_t));
		iface.ver = WL_INTERFACE_CREATE_VER;
		iface.flags = WL_INTERFACE_CREATE_AP;
		apsta_params->netif_change = FALSE;
		wl_ext_iovar_getbuf(dev, "interface_create", &iface, sizeof(iface),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		wl_ext_wait_netif_change(apsta_params, TRUE);
	}
	else if (apstamode == IMESHAPSTA_MODE) {
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "mpc", 0);
		wl_ext_iovar_setint(dev, "mbcn", 1);
		wl_ext_iovar_setint(dev, "apsta", 1); // keep 1 as we set in dhd_preinit_ioctls
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		// don't set WLC_SET_AP to 0, some parameters will be reset, such as bcn_timeout and roam_off
		bzero(&iface, sizeof(wl_interface_create_t));
		iface.ver = WL_INTERFACE_CREATE_VER;
		iface.flags = WL_INTERFACE_CREATE_AP;
		apsta_params->netif_change = FALSE;
		wl_ext_iovar_getbuf(dev, "interface_create", &iface, sizeof(iface),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		wl_ext_wait_netif_change(apsta_params, TRUE);
		bzero(&iface, sizeof(wl_interface_create_t));
		iface.ver = WL_INTERFACE_CREATE_VER;
		iface.flags = WL_INTERFACE_CREATE_STA;
		apsta_params->netif_change = FALSE;
		wl_ext_iovar_getbuf(dev, "interface_create", &iface, sizeof(iface),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		wl_ext_wait_netif_change(apsta_params, TRUE);
	}
	else if (apstamode == IMESHAPAP_MODE) {
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "mpc", 0);
		wl_ext_iovar_setint(dev, "mbcn", 1);
		wl_ext_iovar_setint(dev, "apsta", 1); // keep 1 as we set in dhd_preinit_ioctls
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		// don't set WLC_SET_AP to 0, some parameters will be reset, such as bcn_timeout and roam_off
		bzero(&iface, sizeof(wl_interface_create_t));
		iface.ver = WL_INTERFACE_CREATE_VER;
		iface.flags = WL_INTERFACE_CREATE_AP;
		apsta_params->netif_change = FALSE;
		wl_ext_iovar_getbuf(dev, "interface_create", &iface, sizeof(iface),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		wl_ext_wait_netif_change(apsta_params, TRUE);
		bzero(&iface, sizeof(wl_interface_create_t));
		iface.ver = WL_INTERFACE_CREATE_VER;
		iface.flags = WL_INTERFACE_CREATE_AP;
		apsta_params->netif_change = FALSE;
		wl_ext_iovar_getbuf(dev, "interface_create", &iface, sizeof(iface),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		wl_ext_wait_netif_change(apsta_params, TRUE);
	}
#endif
	else if (apstamode == IGOSTA_MODE) {
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "apsta", 1);
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		bzero(&ifreq, sizeof(wl_p2p_if_t));
		ifreq.type = htod32(WL_P2P_IF_GO);
		apsta_params->netif_change = FALSE;
		wl_ext_iovar_setbuf(dev, "p2p_ifadd", &ifreq, sizeof(ifreq),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		wl_ext_wait_netif_change(apsta_params, TRUE);
	}

	wl_ext_get_ioctl_ver(dev, &apsta_params->ioctl_ver);
	apsta_params->init = TRUE;

	printf("%s: apstamode=%d\n", __FUNCTION__, apstamode);
}

static int
wl_ext_isam_init(struct net_device *dev, char *command, int total_len)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	char *pch, *pick_tmp, *pick_tmp2, *param;
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	int i;

	if (apsta_params->init) {
		ANDROID_ERROR(("%s: don't init twice\n", __FUNCTION__));
		return -1;
	}

	ANDROID_TRACE(("%s: command=%s, len=%d\n", __FUNCTION__, command, total_len));

	pick_tmp = command;
	param = bcmstrtok(&pick_tmp, " ", 0); // skip iapsta_init
	param = bcmstrtok(&pick_tmp, " ", 0);
	while (param != NULL) {
		if (!strcmp(param, "mode")) {
			pch = NULL;
			pick_tmp2 = bcmstrtok(&pick_tmp, " ", 0);
			if (pick_tmp2) {
				if (!strcmp(pick_tmp2, "sta")) {
					apsta_params->apstamode = ISTAONLY_MODE;
				} else if (!strcmp(pick_tmp2, "ap")) {
					apsta_params->apstamode = IAPONLY_MODE;
				} else if (!strcmp(pick_tmp2, "sta-ap")) {
					apsta_params->apstamode = IAPSTA_MODE;
				} else if (!strcmp(pick_tmp2, "ap-ap")) {
					apsta_params->apstamode = IDUALAP_MODE;
				} else if (!strcmp(pick_tmp2, "sta-ap-ap")) {
					apsta_params->apstamode = ISTAAPAP_MODE;
#ifdef WLMESH
				} else if (!strcmp(pick_tmp2, "mesh")) {
					apsta_params->apstamode = IMESHONLY_MODE;
				} else if (!strcmp(pick_tmp2, "mesh-sta") ||
						!strcmp(pick_tmp2, "sta-mesh")) {
					apsta_params->apstamode = IMESHSTA_MODE;
				} else if (!strcmp(pick_tmp2, "mesh-ap") ||
						!strcmp(pick_tmp2, "ap-mesh")) {
					apsta_params->apstamode = IMESHAP_MODE;
				} else if (!strcmp(pick_tmp2, "mesh-ap-sta") ||
						!strcmp(pick_tmp2, "sta-ap-mesh") ||
						!strcmp(pick_tmp2, "sta-mesh-ap")) {
					apsta_params->apstamode = IMESHAPSTA_MODE;
				} else if (!strcmp(pick_tmp2, "mesh-ap-ap") ||
						!strcmp(pick_tmp2, "ap-ap-mesh")) {
					apsta_params->apstamode = IMESHAPAP_MODE;
#endif
				} else if (!strcmp(pick_tmp2, "apsta")) {
					apsta_params->apstamode = IAPSTA_MODE;
					apsta_params->if_info[IF_PIF].ifmode = ISTA_MODE;
					apsta_params->if_info[IF_VIF].ifmode = IAP_MODE;
				} else if (!strcmp(pick_tmp2, "dualap")) {
					apsta_params->apstamode = IDUALAP_MODE;
					apsta_params->if_info[IF_PIF].ifmode = IAP_MODE;
					apsta_params->if_info[IF_VIF].ifmode = IAP_MODE;
				} else if (!strcmp(pick_tmp2, "gosta")) {
					if (!FW_SUPPORTED(dhd, p2p)) {
						return -1;
					}
					apsta_params->apstamode = IGOSTA_MODE;
					apsta_params->if_info[IF_PIF].ifmode = ISTA_MODE;
					apsta_params->if_info[IF_VIF].ifmode = IAP_MODE;
				} else {
					ANDROID_ERROR(("%s: mode [sta|ap|sta-ap|ap-ap]\n", __FUNCTION__));
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
							ANDROID_ERROR(("%s: wrong fw type\n", __FUNCTION__));
							return -1;
						}
						apsta_params->if_info[i].ifmode = IMESH_MODE;
					}
#endif
					pch = bcmstrtok(&pick_tmp2, " -", 0);
				}
			}
		}
		else if (!strcmp(param, "rsdb")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch) {
				if (!strcmp(pch, "y")) {
					apsta_params->rsdb = TRUE;
				} else if (!strcmp(pch, "n")) {
					apsta_params->rsdb = FALSE;
				} else {
					ANDROID_ERROR(("%s: rsdb [y|n]\n", __FUNCTION__));
					return -1;
				}
			}
		} else if (!strcmp(param, "vsdb")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch) {
				if (!strcmp(pch, "y")) {
					apsta_params->vsdb = TRUE;
				} else if (!strcmp(pch, "n")) {
					apsta_params->vsdb = FALSE;
				} else {
					ANDROID_ERROR(("%s: vsdb [y|n]\n", __FUNCTION__));
					return -1;
				}
			}
		} else if (!strcmp(param, "csa")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch) {
				apsta_params->csa = (int)simple_strtol(pch, NULL, 0);
			}
		} else if (!strcmp(param, "ifname")) {
			pch = NULL;
			pick_tmp2 = bcmstrtok(&pick_tmp, " ", 0);
			if (pick_tmp2)
				pch = bcmstrtok(&pick_tmp2, " -", 0);
			for (i=0; i<MAX_IF_NUM && pch; i++) {
				strcpy(apsta_params->if_info[i].ifname, pch);
				pch = bcmstrtok(&pick_tmp2, " -", 0);
			}
		} else if (!strcmp(param, "vifname")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch)
				strcpy(apsta_params->if_info[IF_VIF].ifname, pch);
			else {
				ANDROID_ERROR(("%s: vifname [wlan1]\n", __FUNCTION__));
				return -1;
			}
		}
		param = bcmstrtok(&pick_tmp, " ", 0);
	}

	if (apsta_params->apstamode == 0) {
		ANDROID_ERROR(("%s: mode [sta|ap|sta-ap|ap-ap]\n", __FUNCTION__));
		return -1;
	}

	wl_ext_iapsta_preinit(dev, apsta_params);

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
					ANDROID_ERROR(("%s: bgnmode [b|g|bg|bgn|bgnac]\n", __FUNCTION__));
					return -1;
				}
			} else if (!strcmp(row->name, " hidden ")) {
				if (!strcmp(pick_tmp, "n"))
					cur_if->hidden = 0;
				else if (!strcmp(pick_tmp, "y"))
					cur_if->hidden = 1;
				else {
					ANDROID_ERROR(("%s: hidden [y|n]\n", __FUNCTION__));
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
					ANDROID_ERROR(("%s: amode [open|shared|wpapsk|wpa2psk|wpawpa2psk]\n",
						__FUNCTION__));
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
					ANDROID_ERROR(("%s: emode [none|wep|tkip|aes|tkipaes]\n",
						__FUNCTION__));
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
	struct wl_if_info *cur_if = NULL;

	if (!apsta_params->init) {
		ANDROID_ERROR(("%s: please init first\n", __FUNCTION__));
		return -1;
	}

	ANDROID_TRACE(("%s: command=%s, len=%d\n", __FUNCTION__, command, total_len));

	pick_tmp = command;
	param = bcmstrtok(&pick_tmp, " ", 0); // skip iapsta_config

	while (pick_tmp != NULL) {
		memset(ifname, 0, IFNAMSIZ+1);
		if (!strncmp(pick_tmp, "ifname ", strlen("ifname "))) {
			pch = pick_tmp + strlen("ifname ");
			pch2 = strchr(pch, ' ');
			if (pch && pch2) {
				strncpy(ifname, pch, pch2-pch);
			} else {
				ANDROID_ERROR(("%s: ifname [wlanX]\n", __FUNCTION__));
				return -1;
			}
			for (i=0; i<MAX_IF_NUM; i++) {
				if (apsta_params->if_info[i].dev &&
						!strcmp(apsta_params->if_info[i].dev->name, ifname)) {
					cur_if = &apsta_params->if_info[i];
					break;
				}
			}
			if (!cur_if) {
				ANDROID_ERROR(("%s: wrong ifname=%s in apstamode=%d\n",
					__FUNCTION__, ifname, apsta_params->apstamode));
				return -1;
			}
			ret = wl_ext_parse_config(cur_if, pick_tmp, &pick_next);
			if (ret)
				return -1;
			pick_tmp = pick_next;
		} else {
			ANDROID_ERROR(("%s: first arg must be ifname\n", __FUNCTION__));
			return -1;
		}

	}

	return 0;
}

static int
wl_ext_isam_status(struct net_device *dev, char *command, int total_len)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	int i;
	bool now_if;
	struct wl_if_info *tmp_if;
	uint16 chan = 0;
	wlc_ssid_t ssid = { 0, {0} };
	char amode[16], emode[16];
	int bytes_written=0;

	if (apsta_params->init == FALSE) {
		return 0;
	}

	ANDROID_INFO(("****************************\n"));
	ANDROID_INFO(("%s: apstamode=%d\n", __FUNCTION__, apsta_params->apstamode));
	for (i=0; i<MAX_IF_NUM; i++) {
		now_if = FALSE;
		memset(&ssid, 0, sizeof(ssid));
		memset(amode, 0, sizeof(amode));
		memset(emode, 0, sizeof(emode));
		tmp_if = &apsta_params->if_info[i];
		if (dev == tmp_if->dev)
			now_if = TRUE;
		if (tmp_if->dev) {
			chan = wl_ext_get_chan(apsta_params, tmp_if->dev);
			if (chan) {
				wl_ext_ioctl(tmp_if->dev, WLC_GET_SSID, &ssid, sizeof(ssid), 0);
				wl_ext_get_amode(tmp_if, amode);
				wl_ext_get_emode(tmp_if, emode);
				ANDROID_INFO(("%s[%c-%c%s]: chan %3d, amode %s, emode %s, SSID \"%s\"\n",
					tmp_if->ifname, tmp_if->prefix, chan?'E':'D',
					now_if?"*":" ", chan, amode, emode, ssid.SSID));
				if (command)
					bytes_written += snprintf(command+bytes_written, total_len,
						"%s[%c-%c%s]: chan %3d, amode %s, emode %s, SSID \"%s\"\n",
						tmp_if->ifname, tmp_if->prefix, chan?'E':'D',
						now_if?"*":" ", chan, amode, emode, ssid.SSID);
			} else {
				ANDROID_INFO(("%s[%c-%c%s]:\n",
					tmp_if->ifname, tmp_if->prefix, chan?'E':'D', now_if?"*":" "));
				if (command)
					bytes_written += snprintf(command+bytes_written, total_len, "%s[%c-%c%s]:\n",
						tmp_if->ifname, tmp_if->prefix, chan?'E':'D', now_if?"*":" ");
			}
		}
	}
	ANDROID_INFO(("****************************\n"));

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
	for (count = 0; count < peer_results_count; count++) {
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
				"\n%d: %pM : %6s : %5s : %6s :"
				" %5s : %4s : %4s : %11s : %4s",
				count, &mpi_ext->ea, "  NA  ", "  NA ", "  NA  ",
				"  NA ", " NA ", " NA ", "  TIMEDOUT ", " NA ");
		}
		mpi_ext++;
	}
	ANDROID_INFO(("\n%s\n", command));

	return bytes_written;
}

static int
wl_ext_mesh_peer_status(struct net_device *dev, char *data, char *command,
	int total_len)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	int i;
	struct wl_if_info *cur_if;
	int bytes_written = -1;
	int indata, inlen;
	char *dump_buf;
	mesh_peer_info_dump_t *peer_results;
	mesh_peer_info_ext_t *mpi_ext;

	if (apsta_params->init == FALSE) {
		ANDROID_ERROR(("%s: please init first\n", __FUNCTION__));
		return -1;
	}

	if (!data) {
		dump_buf = kmalloc(WLC_IOCTL_MAXLEN, GFP_KERNEL);
		if (dump_buf == NULL) {
			ANDROID_ERROR(("Failed to allocate buffer of %d bytes\n", WLC_IOCTL_MAXLEN));
			return -1;
		}
		memset(dump_buf, 0, WLC_IOCTL_MAXLEN);

		for (i=0; i<MAX_IF_NUM; i++) {
			cur_if = &apsta_params->if_info[i];
			if (dev == cur_if->dev && cur_if->ifmode == IMESH_MODE) {
				peer_results = (mesh_peer_info_dump_t *)dump_buf;
				indata = htod32(WLC_IOCTL_MAXLEN);
				inlen = 4;
				bytes_written = wl_ext_iovar_getbuf(dev, "mesh_peer_status",
					&indata, inlen, dump_buf, WLC_IOCTL_MAXLEN, NULL);
				if (!bytes_written) {
					peer_results = (mesh_peer_info_dump_t *)dump_buf;
					mpi_ext = (mesh_peer_info_ext_t *)peer_results->mpi_ext;
					bytes_written = wl_mesh_print_peer_info(mpi_ext, peer_results->count,
						command, total_len);
				}
			} else if (dev == cur_if->dev) {
				ANDROID_ERROR(("%s: %s[%c] is not mesh interface\n",
					__FUNCTION__, cur_if->ifname, cur_if->prefix));
			}
		}
		kfree(dump_buf);
	}
	
	return bytes_written;
}
#endif

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

	printf("%s: %s[%c] Turning off\n", __FUNCTION__, cur_if->ifname, cur_if->prefix);

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

	if (cur_if->ifmode != IAP_MODE) {
		ANDROID_ERROR(("%s: Wrong ifmode on %s[%c]\n", __FUNCTION__,
			cur_if->ifname, cur_if->prefix));
		return 0;
	}

	if (cur_if->channel >= 52 && cur_if->channel <= 148) {
		printf("%s: %s[%c] skip DFS channel %d\n", __FUNCTION__,
			cur_if->ifname, cur_if->prefix, cur_if->channel);
		return 0;
	}

	printf("%s: %s[%c] Turning on\n", __FUNCTION__, cur_if->ifname, cur_if->prefix);

	wl_ext_set_chanspec(cur_if->dev, apsta_params->ioctl_ver, cur_if->channel,
		&fw_chspec);

	if (apstamode == IAPONLY_MODE) {
		wl_ext_ioctl(cur_if->dev, WLC_UP, NULL, 0, 1);
	} else {
		bss_setbuf.cfg = 0xffffffff;	
		bss_setbuf.val = htod32(1);
		wl_ext_iovar_setbuf(cur_if->dev, "bss", &bss_setbuf,
			sizeof(bss_setbuf), iovar_buf, WLC_IOCTL_SMLEN, NULL);
	}

	OSL_SLEEP(500);
	wl_ext_isam_status(cur_if->dev, NULL, 0);

	return 0;
}

static int
wl_ext_iapsta_disable(struct net_device *dev, char *command, int total_len)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	char *pch, *pick_tmp, *param;
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	wlc_ssid_t ssid = { 0, {0} };
	scb_val_t scbval;
	struct {
		s32 cfg;
		s32 val;
	} bss_setbuf;
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	apstamode_t apstamode = apsta_params->apstamode;
	char ifname[IFNAMSIZ+1];
	struct wl_if_info *cur_if = NULL;
	int i;

	if (!apsta_params->init) {
		ANDROID_ERROR(("%s: please init first\n", __FUNCTION__));
		return -1;
	}

	ANDROID_TRACE(("%s: command=%s, len=%d\n", __FUNCTION__, command, total_len));

	pick_tmp = command;
	param = bcmstrtok(&pick_tmp, " ", 0); // skip iapsta_disable
	param = bcmstrtok(&pick_tmp, " ", 0);
	while (param != NULL) {
		if (!strcmp(param, "ifname")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch)
				strcpy(ifname, pch);
			else {
				ANDROID_ERROR(("%s: ifname [wlanX]\n", __FUNCTION__));
				return -1;
			}
		}
		param = bcmstrtok(&pick_tmp, " ", 0);
	}

	for (i=0; i<MAX_IF_NUM; i++) {
		if (apsta_params->if_info[i].dev &&
				!strcmp(apsta_params->if_info[i].dev->name, ifname)) {
			cur_if = &apsta_params->if_info[i];
			break;
		}
	}
	if (!cur_if) {
		ANDROID_ERROR(("%s: wrong ifname=%s or dev not ready\n", __FUNCTION__, ifname));
		return -1;
	}

	printf("%s: %s[%c] Disabling\n", __FUNCTION__, ifname, cur_if->prefix);

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
	} else if ((apstamode==IAPSTA_MODE || apstamode==IGOSTA_MODE) &&
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
#endif 
#endif /* PROP_TXSTATUS_VSDB */
	}
	else if (apstamode == IDUALAP_MODE) {
		bss_setbuf.cfg = 0xffffffff;
		bss_setbuf.val = htod32(0);
		wl_ext_iovar_setbuf(cur_if->dev, "bss", &bss_setbuf, sizeof(bss_setbuf),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
#ifdef WLMESH
	} else if (apstamode == IMESHSTA_MODE || apstamode == IMESHAP_MODE ||
			apstamode == IMESHAPSTA_MODE || apstamode == IMESHAPAP_MODE ||
			apstamode == ISTAAPAP_MODE) {
		bss_setbuf.cfg = 0xffffffff;
		bss_setbuf.val = htod32(0);
		wl_ext_iovar_setbuf(cur_if->dev, "bss", &bss_setbuf, sizeof(bss_setbuf),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
#endif
	}

	cur_if->ifstate = IF_STATE_DISALBE;

	printf("%s: %s[%c] disabled\n", __FUNCTION__, ifname, cur_if->prefix);

	return 0;
}

static uint16
wl_ext_get_vsdb_chan(struct wl_apsta_params *apsta_params,
	struct wl_if_info *cur_if, struct wl_if_info *target_if)
{
	uint16 target_chan = 0, cur_chan = cur_if->channel, cur_working_chan = 0;

	target_chan = wl_ext_get_chan(apsta_params, target_if->dev);
	if (target_chan) {
		ANDROID_INFO(("%s: cur_chan=%d, target_chan=%d\n", __FUNCTION__,
			cur_chan, target_chan));
		if ((cur_chan <= CH_MAX_2G_CHANNEL && target_chan > CH_MAX_2G_CHANNEL) ||
				(cur_chan > CH_MAX_2G_CHANNEL && target_chan <= CH_MAX_2G_CHANNEL)) {
			if (!FW_SUPPORTED(apsta_params->dhd, rsdb) || !apsta_params->rsdb)
				return target_chan;
			cur_working_chan = wl_ext_get_chan(apsta_params, cur_if->dev);
			if (cur_working_chan)
				return cur_working_chan;
		} else {
			if (target_chan != cur_chan)
				return target_chan;
		}
	}

	return 0;
}

static int
wl_ext_triger_csa(struct wl_apsta_params *apsta_params, struct wl_if_info *cur_if)
{
	s8 iovar_buf[WLC_IOCTL_SMLEN];

	if (apsta_params->csa & CSA_DRV_BIT &&
			(cur_if->ifmode == IAP_MODE || cur_if->ifmode == IMESH_MODE)) {
		if (!cur_if->channel) {
			printf("%s: %s[%c] skip channel %d\n", __FUNCTION__,
				cur_if->ifname, cur_if->prefix, cur_if->channel);
		} else if (cur_if->channel >= 52 && cur_if->channel <= 148) {
			printf("%s: %s[%c] skip DFS channel %d\n", __FUNCTION__,
				cur_if->ifname, cur_if->prefix, cur_if->channel);
			wl_ext_if_down(apsta_params, cur_if);
		} else {
			wl_chan_switch_t csa_arg;
			memset(&csa_arg, 0, sizeof(csa_arg));
			csa_arg.mode = 1;
			csa_arg.count = 3;
			csa_arg.chspec = wl_ext_get_chanspec(apsta_params, cur_if->dev,
				cur_if->channel);
			if (csa_arg.chspec) {
				printf("%s: Trigger CSA to channel %d(0x%x)\n", __FUNCTION__,
					cur_if->channel, csa_arg.chspec);
				wl_ext_iovar_setbuf(cur_if->dev, "csa", &csa_arg, sizeof(csa_arg),
					iovar_buf, sizeof(iovar_buf), NULL);
				OSL_SLEEP(500);
				wl_ext_isam_status(cur_if->dev, NULL, 0);
			} else {
				printf("%s: fail to get chanspec\n", __FUNCTION__);
			}
		}
	}

	return 0;
}

static uint16
wl_ext_move_cur_channel(struct wl_apsta_params *apsta_params,
	struct net_device *dev, struct wl_if_info *cur_if)
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
		if (tmp_if->ifstate >= IF_STATE_INIT && cur_if != tmp_if &&
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
		printf("%s: %s[%c] channel=%d => %s[%c] channel=%d\n", __FUNCTION__,
			cur_if->ifname, cur_if->prefix, cur_if->channel,
			target_if->ifname, target_if->prefix, target_chan);
		cur_if->channel = target_chan;
	}
exit:
	if ((cur_if->ifmode == IAP_MODE || cur_if->ifmode == IMESH_MODE) &&
			(cur_if->channel >= 52 && cur_if->channel <= 148)) {
		printf("%s: %s[%c] skip DFS channel %d\n", __FUNCTION__,
			cur_if->ifname, cur_if->prefix, cur_if->channel);
		cur_if->channel = 0;
	}

	return cur_if->channel;
}

static void
wl_ext_move_other_channel(struct wl_apsta_params *apsta_params,
	struct net_device *dev, struct wl_if_info *cur_if)
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
		if (tmp_if->ifstate >= IF_STATE_INIT && cur_if != tmp_if &&
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
		printf("%s: %s channel=%d => %s channel=%d\n", __FUNCTION__,
			target_if->ifname, target_chan, cur_if->ifname, cur_if->channel);
		target_if->channel = cur_if->channel;
		if (apsta_params->csa == 0) {
			wl_ext_if_down(apsta_params, target_if);
			wl_ext_move_other_channel(apsta_params, dev, target_if);
			if (target_if->ifmode == ISTA_MODE || target_if->ifmode == IMESH_MODE) {
				wl_ext_enable_iface(target_if->dev, target_if->ifname);
			} else if (target_if->ifmode == IAP_MODE) {
				wl_ext_if_up(apsta_params, target_if);
			}
		} else {
			wl_ext_triger_csa(apsta_params, target_if);
		}
	}

}

static int
wl_ext_enable_iface(struct net_device *dev, char *ifname)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	int i;
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	wlc_ssid_t ssid = { 0, {0} };
	chanspec_t fw_chspec;
	struct {
		s32 cfg;
		s32 val;
	} bss_setbuf;
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	apstamode_t apstamode = apsta_params->apstamode;
	struct wl_if_info *cur_if = NULL;
	uint16 cur_chan;
	struct wl_conn_info conn_info;

	for (i=0; i<MAX_IF_NUM; i++) {
		if (apsta_params->if_info[i].dev &&
				!strcmp(apsta_params->if_info[i].dev->name, ifname)) {
			cur_if = &apsta_params->if_info[i];
			break;
		}
	}
	if (!cur_if) {
		ANDROID_ERROR(("%s: wrong ifname=%s or dev not ready\n", __FUNCTION__, ifname));
		return -1;
	}

	printf("%s: %s[%c] Enabling\n", __FUNCTION__, ifname, cur_if->prefix);

	wl_ext_isam_status(cur_if->dev, NULL, 0);

	wl_ext_move_cur_channel(apsta_params, dev, cur_if);
	if (!cur_if->channel && cur_if->ifmode != ISTA_MODE) {
		return 0;
	}

	cur_chan = wl_ext_get_chan(apsta_params, cur_if->dev);
	if (cur_chan) {
		ANDROID_INFO(("%s: Associated!\n", __FUNCTION__));
		if (cur_chan != cur_if->channel) {
			wl_ext_triger_csa(apsta_params, cur_if);
		}
		return 0;
	}

	wl_ext_move_other_channel(apsta_params, dev, cur_if);

	if (cur_if->ifidx > 0) {
		wl_ext_iovar_setbuf(cur_if->dev, "cur_etheraddr", (u8 *)cur_if->dev->dev_addr,
			ETHER_ADDR_LEN, iovar_buf, WLC_IOCTL_SMLEN, NULL);
	}

	// set ssid for AP
	ssid.SSID_len = strlen(cur_if->ssid);
	memcpy(ssid.SSID, cur_if->ssid, ssid.SSID_len);
	if (cur_if->ifmode == IAP_MODE || cur_if->ifmode == IMESH_MODE) {
		wl_ext_iovar_setint(dev, "mpc", 0);
		if (apstamode == IAPONLY_MODE) {
			wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		} else if (apstamode==IAPSTA_MODE || apstamode==IGOSTA_MODE) {
			wl_ext_iovar_setbuf_bsscfg(cur_if->dev, "ssid", &ssid, sizeof(ssid),
				iovar_buf, WLC_IOCTL_SMLEN, cur_if->bssidx, NULL);
		}
	}

	if (cur_if->ifmode == IAP_MODE || cur_if->ifmode == IMESH_MODE) {
		wl_ext_set_bgnmode(cur_if);
		if (!cur_if->channel) {
#ifdef WL_CFG80211
			char *pick_tmp, *param;
			char cmd[128];
			uint16 cur_chan;
			cur_chan = 1;
			snprintf(cmd, 128, "get_best_channels");
			wl_cfg80211_get_best_channels(dev, cmd, strlen(cmd));
			pick_tmp = cmd;
			param = bcmstrtok(&pick_tmp, " ", 0);
			while (param != NULL) {
				if (!strnicmp(param, "2g=", strlen("2g="))) {
					cur_chan = (int)simple_strtol(param+strlen("2g="), NULL, 10);
				} else if (!strnicmp(param, "5g=", strlen("5g="))) {
					cur_chan = (int)simple_strtol(param+strlen("5g="), NULL, 10);
				}
				param = bcmstrtok(&pick_tmp, " ", 0);
			}
			cur_if->channel = cur_chan;
#else
			cur_if->channel = 1;
#endif
		}
		wl_ext_set_chanspec(cur_if->dev, apsta_params->ioctl_ver, cur_if->channel,
			&fw_chspec);
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
			printf("%s: Broadcast SSID: %s\n", __FUNCTION__,
				cur_if->hidden ? "OFF":"ON");
		}
	}

	if (apstamode == ISTAONLY_MODE) {
		wl_ext_connect(cur_if->dev, &conn_info);
	} else if (apstamode == IAPONLY_MODE) {
		wl_ext_ioctl(cur_if->dev, WLC_SET_SSID, &ssid, sizeof(ssid), 1);
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
	} else if (apstamode == IAPSTA_MODE || apstamode == IGOSTA_MODE) {
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
			if (!FW_SUPPORTED(dhd, rsdb) && !disable_proptx) {
				bool enabled;
				dhd_wlfc_get_enable(dhd, &enabled);
				if (!enabled) {
					dhd_wlfc_init(dhd);
					wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
				}
			}
#endif
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
			printf("%s: wrong ifmode %d\n", __FUNCTION__, cur_if->ifmode);
		}
#ifdef WLMESH
	} else if (apstamode == IMESHONLY_MODE ||
			apstamode == IMESHSTA_MODE || apstamode == IMESHAP_MODE ||
			apstamode == IMESHAPSTA_MODE || apstamode == IMESHAPAP_MODE) {
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
			printf("%s: wrong ifmode %d\n", __FUNCTION__, cur_if->ifmode);
		}
#endif
	}

	OSL_SLEEP(500);
	printf("%s: %s[%c] enabled with SSID: \"%s\"\n", __FUNCTION__,
		ifname, cur_if->prefix, cur_if->ssid);
	wl_ext_isam_status(cur_if->dev, NULL, 0);

	cur_if->ifstate = IF_STATE_ENABLE;

	return 0;
}

static int
wl_ext_iapsta_enable(struct net_device *dev, char *command, int total_len)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	int ret = 0;
	char *pch, *pick_tmp, *param;
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	char ifname[IFNAMSIZ+1];

	if (!apsta_params->init) {
		ANDROID_ERROR(("%s: please init first\n", __FUNCTION__));
		return -1;
	}

	ANDROID_TRACE(("%s: command=%s, len=%d\n", __FUNCTION__, command, total_len));

	pick_tmp = command;
	param = bcmstrtok(&pick_tmp, " ", 0); // skip iapsta_enable
	param = bcmstrtok(&pick_tmp, " ", 0);
	while (param != NULL) {
		if (!strcmp(param, "ifname")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch) {
				strcpy(ifname, pch);
				ret = wl_ext_enable_iface(dev, ifname);
				if (ret)
					return ret;
			} else {
				ANDROID_ERROR(("%s: ifname [wlanX]\n", __FUNCTION__));
				return -1;
			}
		}
		param = bcmstrtok(&pick_tmp, " ", 0);
	}

	return ret;
}

int
wl_ext_iapsta_alive_preinit(struct net_device *dev)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *cur_if;
	int i;

	if (apsta_params->init == TRUE) {
		ANDROID_ERROR(("%s: don't init twice\n", __FUNCTION__));
		return -1;
	}

	ANDROID_TRACE(("%s: Enter\n", __FUNCTION__));

	for (i=0; i<MAX_IF_NUM; i++) {
		cur_if = &apsta_params->if_info[i];
		if (i == 1 && !strlen(cur_if->ifname))
			strcpy(cur_if->ifname, "wlan1");
		if (i == 2 && !strlen(cur_if->ifname))
			strcpy(cur_if->ifname, "wlan2");
		if (cur_if->ifmode == ISTA_MODE) {
			cur_if->channel = 0;
			cur_if->maxassoc = -1;
			cur_if->ifstate = IF_STATE_INIT;
			cur_if->prio = PRIO_STA;
			cur_if->prefix = 'S';
			snprintf(cur_if->ssid, DOT11_MAX_SSID_LEN, "ttt_sta");
		} else if (cur_if->ifmode == IAP_MODE) {
			cur_if->channel = 1;
			cur_if->maxassoc = -1;
			cur_if->ifstate = IF_STATE_INIT;
			cur_if->prio = PRIO_AP;
			cur_if->prefix = 'A';
			snprintf(cur_if->ssid, DOT11_MAX_SSID_LEN, "ttt_ap");
#ifdef WLMESH
		} else if (cur_if->ifmode == IMESH_MODE) {
			cur_if->channel = 1;
			cur_if->maxassoc = -1;
			cur_if->ifstate = IF_STATE_INIT;
			cur_if->prio = PRIO_MESH;
			cur_if->prefix = 'M';
			snprintf(cur_if->ssid, DOT11_MAX_SSID_LEN, "ttt_mesh");
#endif
		}
	}

	apsta_params->init = TRUE;

	return 0;
}

int
wl_ext_iapsta_alive_postinit(struct net_device *dev)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	s32 apsta = 0;
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;

	wl_ext_iovar_getint(dev, "apsta", &apsta);
	if (apsta == 1) {
		apsta_params->apstamode = ISTAONLY_MODE;
		apsta_params->if_info[IF_PIF].ifmode = ISTA_MODE;
		op_mode = DHD_FLAG_STA_MODE;
	} else {
		apsta_params->apstamode = IAPONLY_MODE;
		apsta_params->if_info[IF_PIF].ifmode = IAP_MODE;
		op_mode = DHD_FLAG_HOSTAP_MODE;
	}
	// fix me: how to check it's IAPSTA_MODE or IDUALAP_MODE?

	wl_ext_get_ioctl_ver(dev, &apsta_params->ioctl_ver);
	printf("%s: apstamode=%d\n", __FUNCTION__, apsta_params->apstamode);

	return op_mode;
}

#if defined(WL_WIRELESS_EXT)
static bool
wl_ext_conn_status_str(uint32 event_type,
	uint32 status, uint32 reason, char* stringBuf, uint buflen)
{
	int i;

	typedef struct conn_fail_event_map_t {
		uint32 inEvent;			/* input: event type to match */
		uint32 inStatus;		/* input: event status code to match */
		uint32 inReason;		/* input: event reason code to match */
	} conn_fail_event_map_t;

	/* Map of WLC_E events to connection failure strings */
#	define WL_IW_DONT_CARE	9999
	const conn_fail_event_map_t event_map [] = {
		/* inEvent				inStatus				inReason         */
		{WLC_E_LINK,			WL_IW_DONT_CARE,	WL_IW_DONT_CARE},
		{WLC_E_DEAUTH,			WL_IW_DONT_CARE,	WL_IW_DONT_CARE},
		{WLC_E_DEAUTH_IND,		WL_IW_DONT_CARE,	WL_IW_DONT_CARE},
		{WLC_E_DISASSOC,		WL_IW_DONT_CARE,	WL_IW_DONT_CARE},
		{WLC_E_DISASSOC_IND,	WL_IW_DONT_CARE,	WL_IW_DONT_CARE},
		{WLC_E_OVERLAY_REQ,		WL_IW_DONT_CARE,	WL_IW_DONT_CARE},
		{WLC_E_ASSOC_IND,		WL_IW_DONT_CARE,	DOT11_SC_SUCCESS},
		{WLC_E_REASSOC_IND,		WL_IW_DONT_CARE,	DOT11_SC_SUCCESS},
	};

	/* Search the event map table for a matching event */
	for (i = 0;  i < sizeof(event_map)/sizeof(event_map[0]);  i++) {
		const conn_fail_event_map_t* row = &event_map[i];
		if (row->inEvent == event_type &&
		    (row->inStatus == status || row->inStatus == WL_IW_DONT_CARE) &&
		    (row->inReason == reason || row->inReason == WL_IW_DONT_CARE)) {
			memset(stringBuf, 0, buflen);
			snprintf(stringBuf, buflen, "isam_event event=%d reason=%d",
				event_type, reason);
			return TRUE;
		}
	}

	return FALSE;
}
#endif /* WL_WIRELESS_EXT */

int
wl_ext_iapsta_event(struct net_device *dev, wl_event_msg_t *e, void* data)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *cur_if = NULL;
	int i;
#if defined(WL_WIRELESS_EXT)
	char extra[IW_CUSTOM_MAX + 1];
	union iwreq_data wrqu;
#endif
	uint32 event_type = ntoh32(e->event_type);
	uint32 status =  ntoh32(e->status);
	uint32 reason =  ntoh32(e->reason);
	uint16 flags =  ntoh16(e->flags);

	if (!apsta_params->init) {
		ANDROID_TRACE(("%s: please init first\n", __FUNCTION__));
		return -1;
	}

	for (i=0; i<MAX_IF_NUM; i++) {
		if (apsta_params->if_info[i].ifidx == e->ifidx) {
			cur_if = &apsta_params->if_info[i];
			break;
		}
	}
	if (!cur_if || !cur_if->dev) {
		ANDROID_ERROR(("%s: %s ifidx %d is not ready\n", __FUNCTION__,
			dev->name, e->ifidx));
		return -1;
	}

	if (cur_if->ifmode == ISTA_MODE) {
		if (event_type == WLC_E_LINK) {
			if (!(flags & WLC_EVENT_MSG_LINK)) {
				printf("%s: %s[%c] Link Down with %pM\n", __FUNCTION__,
					cur_if->ifname, cur_if->prefix, &e->addr);
			} else {
				printf("%s: %s[%c] Link UP with %pM\n", __FUNCTION__,
					cur_if->ifname, cur_if->prefix, &e->addr);
			}
		}
	}
	else if (cur_if->ifmode == IAP_MODE || cur_if->ifmode == IMESH_MODE) {
		if ((event_type == WLC_E_SET_SSID && status == WLC_E_STATUS_SUCCESS) ||
				(event_type == WLC_E_LINK && status == WLC_E_STATUS_SUCCESS &&
				reason == WLC_E_REASON_INITIAL_ASSOC)) {
			printf("%s: %s[%c] Link up\n", __FUNCTION__,
				cur_if->ifname, cur_if->prefix);
		} else if ((event_type == WLC_E_LINK && reason == WLC_E_LINK_BSSCFG_DIS) ||
				(event_type == WLC_E_LINK && status == WLC_E_STATUS_SUCCESS &&
				reason == WLC_E_REASON_DEAUTH)) {
			printf("%s: %s[%c] Link down\n", __FUNCTION__,
				cur_if->ifname, cur_if->prefix);
		}
		else if ((event_type == WLC_E_ASSOC_IND || event_type == WLC_E_REASSOC_IND) &&
				reason == DOT11_SC_SUCCESS) {
			printf("%s: %s[%c] connected device %pM\n", __FUNCTION__,
				cur_if->ifname, cur_if->prefix, &e->addr);
		} else if (event_type == WLC_E_DISASSOC_IND) {
			printf("%s: %s[%c] disassociated device %pM\n", __FUNCTION__,
				cur_if->ifname, cur_if->prefix, &e->addr);
		} else if (event_type == WLC_E_DEAUTH_IND ||
				(event_type == WLC_E_DEAUTH && reason != DOT11_RC_RESERVED)) {
			printf("%s: %s[%c] deauthenticated device %pM\n", __FUNCTION__,
				cur_if->ifname, cur_if->prefix, &e->addr);
		}
	}

#if defined(WL_WIRELESS_EXT)
	memset(extra, 0, sizeof(extra));
	memset(&wrqu, 0, sizeof(wrqu));
	memcpy(wrqu.addr.sa_data, &e->addr, ETHER_ADDR_LEN);
	wrqu.addr.sa_family = ARPHRD_ETHER;
	if (wl_ext_conn_status_str(event_type, status, reason, extra, sizeof(extra))) {
		wrqu.data.length = strlen(extra);
		wireless_send_event(cur_if->dev, IWEVCUSTOM, &wrqu, extra);
		ANDROID_EVENT(("%s: %s[%c] event=%d, status=%d, reason=%d, flags=%d sent up\n",
			__FUNCTION__, cur_if->ifname, cur_if->prefix, event_type, status,
			reason, flags));
	} else
#endif /* WL_WIRELESS_EXT */
	{
		ANDROID_EVENT(("%s: %s[%c] event=%d, status=%d, reason=%d, flags=%d\n",
			__FUNCTION__, cur_if->ifname, cur_if->prefix, event_type, status,
			reason, flags));
	}

	return 0;
}

u32
wl_ext_iapsta_update_channel(struct net_device *dev, u32 channel)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
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
		channel = wl_ext_move_cur_channel(apsta_params,
			apsta_params->if_info[IF_PIF].dev, cur_if);
		if (channel)
			wl_ext_move_other_channel(apsta_params,
				apsta_params->if_info[IF_PIF].dev, cur_if);
	}

	return channel;
}

int
wl_ext_iapsta_attach_name(struct net_device *net, int ifidx)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *cur_if = NULL;

	ANDROID_TRACE(("%s: ifidx=%d, %s\n", __FUNCTION__, ifidx, net->name));
	if (ifidx < MAX_IF_NUM) {
		cur_if = &apsta_params->if_info[ifidx];
	}
	if (ifidx == 0) {
		if (dhd->conf->fw_type == FW_TYPE_MESH) {
			apsta_params->rsdb = TRUE;
			apsta_params->csa = CSA_FW_BIT | CSA_DRV_BIT;
		}
		strcpy(cur_if->ifname, net->name);
	} else if (cur_if && cur_if->ifstate == IF_STATE_INIT) {
		strcpy(cur_if->ifname, net->name);
		apsta_params->netif_change = TRUE;
		wake_up_interruptible(&apsta_params->netif_change_event);
	}

	return 0;
}

int
wl_ext_iapsta_attach_netdev(struct net_device *net, int ifidx, uint8 bssidx)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *cur_if = NULL, *primary_if;

	printf("%s: ifidx=%d, bssidx=%d\n", __FUNCTION__, ifidx, bssidx);
	if (ifidx < MAX_IF_NUM) {
		cur_if = &apsta_params->if_info[ifidx];
	}
	if (ifidx == 0) {
		memset(apsta_params, 0, sizeof(struct wl_apsta_params));
		apsta_params->dhd = dhd;
		apsta_params->vsdb = FALSE;
		cur_if->dev = net;
		cur_if->ifidx = ifidx;
		cur_if->bssidx = bssidx;
		strcpy(cur_if->ifname, net->name);
		init_waitqueue_head(&apsta_params->netif_change_event);
	} else if (cur_if && cur_if->ifstate == IF_STATE_INIT) {
		primary_if = &apsta_params->if_info[IF_PIF];
		cur_if->dev = net;
		cur_if->ifidx = ifidx;
		cur_if->bssidx = bssidx;
		if (strlen(cur_if->ifname)) {
			memset(net->name, 0, sizeof(IFNAMSIZ));
			strcpy(net->name, cur_if->ifname);
			net->name[IFNAMSIZ-1] = '\0';
		}
		if (apsta_params->apstamode != ISTAAPAP_MODE) {
			memcpy(net->dev_addr, primary_if->dev->dev_addr, ETHER_ADDR_LEN);
			net->dev_addr[0] |= 0x02;
			if (ifidx >= 2) {
				net->dev_addr[4] ^= 0x80;
				net->dev_addr[4] += ifidx;
				net->dev_addr[5] += (ifidx-1);
			}
		}
		if (cur_if->ifmode == ISTA_MODE) {
			wl_ext_iovar_setint(net, "roam_off", dhd->conf->roam_off);
			wl_ext_iovar_setint(net, "bcn_timeout", dhd->conf->bcn_timeout);
#ifdef WLMESH
		} else if (cur_if->ifmode == IMESH_MODE) {
			int pm = 0;
			wl_ext_ioctl(net, WLC_SET_PM, &pm, sizeof(pm), 1);
#endif
		}
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

	printf("%s: ifidx=%d\n", __FUNCTION__, ifidx);
	if (ifidx < MAX_IF_NUM) {
		cur_if = &apsta_params->if_info[ifidx];
	}

	if (ifidx == 0) {
		memset(apsta_params, 0, sizeof(struct wl_apsta_params));
	} else if (cur_if && cur_if->ifstate >= IF_STATE_INIT) {
		memset(cur_if, 0, sizeof(struct wl_if_info));
	}

	return 0;
}

int wl_ext_iapsta_attach(dhd_pub_t *pub)
{
	struct wl_apsta_params *iapsta_params;

	iapsta_params = kzalloc(sizeof(struct wl_apsta_params), GFP_KERNEL);
	if (unlikely(!iapsta_params)) {
		printf("%s: Could not allocate apsta_params\n", __FUNCTION__);
		return -ENOMEM;
	}
	pub->iapsta_params = (void *)iapsta_params;

	return 0;
}

void wl_ext_iapsta_dettach(dhd_pub_t *pub)
{
	if (pub->iapsta_params) {
		kfree(pub->iapsta_params);
		pub->iapsta_params = NULL;
	}
}
#endif

#ifdef IDHCP
int
wl_ext_ip_dump(int ip, char *buf)
{
	unsigned char bytes[4];
	int bytes_written=-1;

	bytes[0] = ip & 0xFF;
	bytes[1] = (ip >> 8) & 0xFF;
	bytes[2] = (ip >> 16) & 0xFF;
	bytes[3] = (ip >> 24) & 0xFF;
	bytes_written = sprintf(buf, "%d.%d.%d.%d", bytes[0], bytes[1], bytes[2], bytes[3]);

	return bytes_written;
}

/*
terence 20170215:
dhd_priv dhcpc_dump ifname [wlan0|wlan1]
dhd_priv dhcpc_enable [0|1]
*/
int
wl_ext_dhcpc_enable(struct net_device *dev, char *command, int total_len)
{
	int enable = -1, ret = -1;
	int bytes_written = -1;

	ANDROID_TRACE(("%s: cmd %s\n", __FUNCTION__, command));

	sscanf(command, "%*s %d", &enable);

	if (enable >= 0)
		ret = wl_ext_iovar_setint(dev, "dhcpc_enable", enable);
	else {
		ret = wl_ext_iovar_getint(dev, "dhcpc_enable", &enable);
		if (!ret) {
			bytes_written = snprintf(command, total_len, "%d", enable);
			ANDROID_TRACE(("%s: command result is %s\n", __FUNCTION__, command));
			ret = bytes_written;
		}
	}

	return ret;
}

int
wl_ext_dhcpc_dump(struct net_device *dev, char *command, int total_len)
{
	int ret = 0;
	int bytes_written = 0;
	uint32 ip_addr;
	char buf[20]="";

	ret = wl_ext_iovar_getint(dev, "dhcpc_ip_addr", &ip_addr);
	if (!ret) {
		wl_ext_ip_dump(ip_addr, buf);
		bytes_written += snprintf(command+bytes_written, total_len, "ipaddr %s ", buf);
	}

	ret = wl_ext_iovar_getint(dev, "dhcpc_ip_mask", &ip_addr);
	if (!ret) {
		wl_ext_ip_dump(ip_addr, buf);
		bytes_written += snprintf(command+bytes_written, total_len, "mask %s ", buf);
	}

	ret = wl_ext_iovar_getint(dev, "dhcpc_ip_gateway", &ip_addr);
	if (!ret) {
		wl_ext_ip_dump(ip_addr, buf);
		bytes_written += snprintf(command+bytes_written, total_len, "gw %s ", buf);
	}

	ret = wl_ext_iovar_getint(dev, "dhcpc_ip_dnsserv", &ip_addr);
	if (!ret) {
		wl_ext_ip_dump(ip_addr, buf);
		bytes_written += snprintf(command+bytes_written, total_len, "dnsserv %s ", buf);
	}

	if (!bytes_written)
		bytes_written = -1;

	ANDROID_TRACE(("%s: command result is %s\n", __FUNCTION__, command));

	return bytes_written;
}
#endif

static int
wl_ext_rsdb_mode(struct net_device *dev, char *data, char *command,
	int total_len)
{
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	wl_config_t rsdb_mode_cfg = {1, 0}, *rsdb_p;
	int ret = 0;

	ANDROID_TRACE(("%s: Enter\n", __FUNCTION__));

	if (data) {
		rsdb_mode_cfg.config = (int)simple_strtol(data, NULL, 0);
		ret = wl_ext_iovar_setbuf(dev, "rsdb_mode", (char *)&rsdb_mode_cfg,
			sizeof(rsdb_mode_cfg), iovar_buf, WLC_IOCTL_SMLEN, NULL);
		printf("%s: rsdb_mode %d\n", __FUNCTION__, rsdb_mode_cfg.config);
	} else {
		ret = wl_ext_iovar_getbuf(dev, "rsdb_mode", NULL, 0,
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		if (!ret) {
			rsdb_p = (wl_config_t *) iovar_buf;
			ret = snprintf(command, total_len, "%d", rsdb_p->config);
			ANDROID_TRACE(("%s: command result is %s\n", __FUNCTION__,
				command));
		}
	}

	return ret;
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
	{WLC_GET_VAR,	WLC_SET_VAR,	"rsdb_mode",	wl_ext_rsdb_mode},
#ifdef WLMESH
	{WLC_GET_VAR,	WLC_SET_VAR,	"mesh_peer_status",	wl_ext_mesh_peer_status},
#endif
};

/*
Ex: dhd_priv wl [cmd] [val]
  dhd_priv wl 85
  dhd_priv wl 86 1
  dhd_priv wl mpc
  dhd_priv wl mpc 1
*/
int
wl_ext_wl_iovar(struct net_device *dev, char *command, int total_len)
{
	int cmd, val, ret = -1, i;
	char name[32], *pch, *pick_tmp, *data;
	int bytes_written=-1;
	const wl_ext_iovar_tpl_t *tpl = wl_ext_iovar_tpl_list;
	int tpl_count = ARRAY_SIZE(wl_ext_iovar_tpl_list);

	ANDROID_TRACE(("%s: cmd %s\n", __FUNCTION__, command));
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
	data = bcmstrtok(&pick_tmp, " ", 0); // pick data
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
			ANDROID_TRACE(("%s: set %s %d\n", __FUNCTION__, name, val));
			ret = wl_ext_iovar_setint(dev, name, val);
		} else if (cmd == WLC_GET_VAR) {
			ANDROID_TRACE(("%s: get %s\n", __FUNCTION__, name));
			ret = wl_ext_iovar_getint(dev, name, &val);
			if (!ret) {
				bytes_written = snprintf(command, total_len, "%d", val);
				ANDROID_TRACE(("%s: command result is %s\n", __FUNCTION__,
					command));
				ret = bytes_written;
			}
		} else if (data) {
			val = (int)simple_strtol(data, NULL, 0);
			ANDROID_TRACE(("%s: set %d %d\n", __FUNCTION__, cmd, val));
			ret = wl_ext_ioctl(dev, cmd, &val, sizeof(val), TRUE);
		} else {
			ANDROID_TRACE(("%s: get %d\n", __FUNCTION__, cmd));
			ret = wl_ext_ioctl(dev, cmd, &val, sizeof(val), FALSE);
			if (!ret) {
				bytes_written = snprintf(command, total_len, "%d", val);
				ANDROID_TRACE(("%s: command result is %s\n", __FUNCTION__,
					command));
				ret = bytes_written;
			}
		}
	}

exit:
	return ret;
}

int wl_android_ext_priv_cmd(struct net_device *net, char *command,
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
	else if (strnicmp(command, CMD_KEEP_ALIVE, strlen(CMD_KEEP_ALIVE)) == 0) {
		*bytes_written = wl_ext_keep_alive(net, command, total_len);
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
	else if (strnicmp(command, CMD_IAPSTA_INIT, strlen(CMD_IAPSTA_INIT)) == 0) {
		*bytes_written = wl_ext_isam_init(net, command, total_len);
	}
	else if (strnicmp(command, CMD_ISAM_INIT, strlen(CMD_ISAM_INIT)) == 0) {
		*bytes_written = wl_ext_isam_init(net, command, total_len);
	}
	else if (strnicmp(command, CMD_IAPSTA_CONFIG, strlen(CMD_IAPSTA_CONFIG)) == 0) {
		*bytes_written = wl_ext_iapsta_config(net, command, total_len);
	}
	else if (strnicmp(command, CMD_ISAM_CONFIG, strlen(CMD_ISAM_CONFIG)) == 0) {
		*bytes_written = wl_ext_iapsta_config(net, command, total_len);
	}
	else if (strnicmp(command, CMD_IAPSTA_ENABLE, strlen(CMD_IAPSTA_ENABLE)) == 0) {
		*bytes_written = wl_ext_iapsta_enable(net, command, total_len);
	}
	else if (strnicmp(command, CMD_ISAM_ENABLE, strlen(CMD_ISAM_ENABLE)) == 0) {
		*bytes_written = wl_ext_iapsta_enable(net, command, total_len);
	}
	else if (strnicmp(command, CMD_IAPSTA_DISABLE, strlen(CMD_IAPSTA_DISABLE)) == 0) {
		*bytes_written = wl_ext_iapsta_disable(net, command, total_len);
	}
	else if (strnicmp(command, CMD_ISAM_DISABLE, strlen(CMD_ISAM_DISABLE)) == 0) {
		*bytes_written = wl_ext_iapsta_disable(net, command, total_len);
	}
	else if (strnicmp(command, CMD_ISAM_STATUS, strlen(CMD_ISAM_STATUS)) == 0) {
		*bytes_written = wl_ext_isam_status(net, command, total_len);
	}
#endif
#ifdef IDHCP
	else if (strnicmp(command, CMD_DHCPC_ENABLE, strlen(CMD_DHCPC_ENABLE)) == 0) {
		*bytes_written = wl_ext_dhcpc_enable(net, command, total_len);
	}
	else if (strnicmp(command, CMD_DHCPC_DUMP, strlen(CMD_DHCPC_DUMP)) == 0) {
		*bytes_written = wl_ext_dhcpc_dump(net, command, total_len);
	}
#endif
#ifdef WL_CFG80211
	else if (strnicmp(command, CMD_AUTOCHANNEL, strlen(CMD_AUTOCHANNEL)) == 0) {
		*bytes_written = wl_cfg80211_autochannel(net, command, total_len);
	}
#endif
#ifdef WL_ESCAN
	else if (strnicmp(command, CMD_AUTOCHANNEL, strlen(CMD_AUTOCHANNEL)) == 0) {
		*bytes_written = wl_escan_autochannel(net, command, total_len);
	}
#endif
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
			ANDROID_ERROR(("bw_cap failed, %d\n", err));
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
	ANDROID_INFO(("%s: bw=0x%x, distance=%d\n", __FUNCTION__, bw, distance));

	return distance;
}

int
wl_ext_get_best_channel(struct net_device *net,
#if defined(BSSCACHE)
	wl_bss_cache_ctrl_t *bss_cache_ctrl,
#else
	struct wl_scan_results *bss_list,
#endif
	int ioctl_ver, int *best_2g_ch, int *best_5g_ch
)
{
	struct wl_bss_info *bi = NULL;	/* must be initialized */
	s32 i, j;
#if defined(BSSCACHE)
	wl_bss_cache_t *node;
#endif
	int b_band[CH_MAX_2G_CHANNEL]={0}, a_band1[4]={0}, a_band4[5]={0};
	s32 cen_ch, distance, distance_2g, distance_5g, ch, min_ap=999;
	u8 valid_chan_list[sizeof(u32)*(WL_NUMCHANNELS + 1)];
	wl_uint32_list_t *list;
	int ret;
	chanspec_t chanspec;

	memset(b_band, -1, sizeof(b_band));
	memset(a_band1, -1, sizeof(a_band1));
	memset(a_band4, -1, sizeof(a_band4));

	memset(valid_chan_list, 0, sizeof(valid_chan_list));
	list = (wl_uint32_list_t *)(void *) valid_chan_list;
	list->count = htod32(WL_NUMCHANNELS);
	ret = wl_ext_ioctl(net, WLC_GET_VALID_CHANNELS, &valid_chan_list,
		sizeof(valid_chan_list), 0);
	if (ret<0) {
		ANDROID_ERROR(("%s: get channels failed with %d\n", __FUNCTION__, ret));
		return 0;
	} else {
		for (i = 0; i < dtoh32(list->count); i++) {
			ch = dtoh32(list->element[i]);
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
#endif
	{
#if defined(BSSCACHE)
		bi = node->results.bss_info;
#else
		bi = bi ? (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length)) : bss_list->bss_info;
#endif
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
#endif
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

	if (android_msg_level&ANDROID_INFO_LEVEL) {
		printf("%s: b_band: ", __FUNCTION__);
		for (j=0; j<ARRAYSIZE(b_band); j++)
			printf("%d, ", b_band[j]);
		printf("\n");
		printf("%s: a_band1: ", __FUNCTION__);
		for (j=0; j<ARRAYSIZE(a_band1); j++)
			printf("%d, ", a_band1[j]);
		printf("\n");
		printf("%s: a_band4: ", __FUNCTION__);
		for (j=0; j<ARRAYSIZE(a_band4); j++)
			printf("%d, ", a_band4[j]);
		printf("\n");
		printf("%s: best_2g_ch=%d, best_5g_ch=%d\n", __FUNCTION__,
			*best_2g_ch, *best_5g_ch);
	}

	return 0;
}
#endif

#if defined(RSSIAVG)
void
wl_free_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl)
{
	wl_rssi_cache_t *node, *cur, **rssi_head;
	int i=0;

	rssi_head = &rssi_cache_ctrl->m_cache_head;
	node = *rssi_head;

	for (;node;) {
		ANDROID_INFO(("%s: Free %d with BSSID %pM\n",
			__FUNCTION__, i, &node->BSSID));
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
	struct timeval now;

	do_gettimeofday(&now);

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
			ANDROID_INFO(("%s: Del %d with BSSID %pM\n",
				__FUNCTION__, i, &node->BSSID));
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
			ANDROID_INFO(("%s: Del %d with BSSID %pM\n",
				__FUNCTION__, i, &node->BSSID));
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
	struct timeval now, timeout;
	scb_val_t scbval;

	if (!g_wifi_on)
		return 0;

	error = wl_ext_ioctl(net, WLC_GET_BSSID, &bssid, sizeof(bssid), 0);
	if (error == BCME_NOTASSOCIATED) {
		ANDROID_INFO(("%s: Not Associated! res:%d\n", __FUNCTION__, error));
		return 0;
	}
	if (error) {
		ANDROID_ERROR(("Could not get bssid (%d)\n", error));
	}
	error = wldev_get_rssi(net, &scbval);
	if (error) {
		ANDROID_ERROR(("Could not get rssi (%d)\n", error));
		return error;
	}
	rssi = scbval.val;

	do_gettimeofday(&now);
	timeout.tv_sec = now.tv_sec + RSSICACHE_TIMEOUT;
	if (timeout.tv_sec < now.tv_sec) {
		/*
		 * Integer overflow - assume long enough timeout to be assumed
		 * to be infinite, i.e., the timeout would never happen.
		 */
		ANDROID_TRACE(("%s: Too long timeout (secs=%d) to ever happen - now=%lu, timeout=%lu",
			__FUNCTION__, RSSICACHE_TIMEOUT, now.tv_sec, timeout.tv_sec));
	}

	/* update RSSI */
	rssi_head = &rssi_cache_ctrl->m_cache_head;
	node = *rssi_head;
	prev = NULL;
	for (;node;) {
		if (!memcmp(&node->BSSID, &bssid, ETHER_ADDR_LEN)) {
			ANDROID_INFO(("%s: Update %d with BSSID %pM, RSSI=%d\n",
				__FUNCTION__, k, &bssid, rssi));
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
		ANDROID_ERROR(("%s: Memory alloc failure %d\n",
			__FUNCTION__, (int)sizeof(wl_rssi_cache_t)));
		return 0;
	}
	ANDROID_INFO(("%s: Add %d with cached BSSID %pM, RSSI=%3d in the leaf\n",
			__FUNCTION__, k, &bssid, rssi));

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
	struct timeval now, timeout;

	if (!ss_list->count)
		return;

	do_gettimeofday(&now);
	timeout.tv_sec = now.tv_sec + RSSICACHE_TIMEOUT;
	if (timeout.tv_sec < now.tv_sec) {
		/*
		 * Integer overflow - assume long enough timeout to be assumed
		 * to be infinite, i.e., the timeout would never happen.
		 */
		ANDROID_TRACE(("%s: Too long timeout (secs=%d) to ever happen - now=%lu, timeout=%lu",
			__FUNCTION__, RSSICACHE_TIMEOUT, now.tv_sec, timeout.tv_sec));
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
				ANDROID_INFO(("%s: Update %d with BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
					__FUNCTION__, k, &bi->BSSID, dtoh16(bi->RSSI), bi->SSID));
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
			ANDROID_ERROR(("%s: Memory alloc failure %d\n",
				__FUNCTION__, (int)sizeof(wl_rssi_cache_t)));
			return;
		}
		ANDROID_INFO(("%s: Add %d with cached BSSID %pM, RSSI=%3d, SSID \"%s\" in the leaf\n",
				__FUNCTION__, k, &bi->BSSID, dtoh16(bi->RSSI), bi->SSID));

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
		ANDROID_ERROR(("%s: BSSID %pM does not in RSSI cache\n",
		__FUNCTION__, addr));
	}
	return (int16)rssi;
}
#endif

#if defined(RSSIOFFSET)
int
wl_update_rssi_offset(struct net_device *net, int rssi)
{
#if defined(RSSIOFFSET_NEW)
	int j;
#endif

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
#endif
	return MIN(rssi, RSSI_MAXVAL);
}
#endif

#if defined(BSSCACHE)
void
wl_free_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl)
{
	wl_bss_cache_t *node, *cur, **bss_head;
	int i=0;

	ANDROID_TRACE(("%s called\n", __FUNCTION__));

	bss_head = &bss_cache_ctrl->m_cache_head;
	node = *bss_head;

	for (;node;) {
		ANDROID_TRACE(("%s: Free %d with BSSID %pM\n",
			__FUNCTION__, i, &node->results.bss_info->BSSID));
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
	struct timeval now;

	do_gettimeofday(&now);

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
			ANDROID_TRACE(("%s: Del %d with BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
				__FUNCTION__, i, &node->results.bss_info->BSSID,
				dtoh16(node->results.bss_info->RSSI), node->results.bss_info->SSID));
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
			ANDROID_TRACE(("%s: Del %d with BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
				__FUNCTION__, i, &node->results.bss_info->BSSID,
				dtoh16(node->results.bss_info->RSSI), node->results.bss_info->SSID));
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
#endif
	wl_bss_cache_t *node)
{
	int k = 0;
	int16 rssi;

	for (;node;) {
#if defined(RSSIAVG)
		rssi = wl_get_avg_rssi(rssi_cache_ctrl, &node->results.bss_info->BSSID);
#else
		rssi = dtoh16(node->results.bss_info->RSSI);
#endif
		ANDROID_TRACE(("%s: dump %d with cached BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
			__FUNCTION__, k, &node->results.bss_info->BSSID, rssi,
			node->results.bss_info->SSID));
		k++;
		node = node->next;
	}
}

void
wl_update_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl,
#if defined(RSSIAVG)
	wl_rssi_cache_ctrl_t *rssi_cache_ctrl,
#endif
	wl_scan_results_t *ss_list)
{
	wl_bss_cache_t *node, *prev, *leaf, **bss_head;
	wl_bss_info_t *bi = NULL;
	int i, k=0;
#if defined(SORT_BSS_BY_RSSI)
	int16 rssi, rssi_node;
#endif
	struct timeval now, timeout;

	if (!ss_list->count)
		return;

	do_gettimeofday(&now);
	timeout.tv_sec = now.tv_sec + BSSCACHE_TIMEOUT;
	if (timeout.tv_sec < now.tv_sec) {
		/*
		 * Integer overflow - assume long enough timeout to be assumed
		 * to be infinite, i.e., the timeout would never happen.
		 */
		ANDROID_TRACE(("%s: Too long timeout (secs=%d) to ever happen - now=%lu, timeout=%lu",
			__FUNCTION__, BSSCACHE_TIMEOUT, now.tv_sec, timeout.tv_sec));
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
			ANDROID_ERROR(("%s: Memory alloc failure %d\n", __FUNCTION__,
				dtoh32(bi->length) + (int)sizeof(wl_bss_cache_t)));
			return;
		}
		if (node) {
			kfree(node);
			node = NULL;
			ANDROID_TRACE(("%s: Update %d with cached BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
				__FUNCTION__, k, &bi->BSSID, dtoh16(bi->RSSI), bi->SSID));
		} else
			ANDROID_TRACE(("%s: Add %d with cached BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
				__FUNCTION__, k, &bi->BSSID, dtoh16(bi->RSSI), bi->SSID));

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
#endif
			for (;node;) {
#if defined(RSSIAVG)
				rssi_node = wl_get_avg_rssi(rssi_cache_ctrl,
					&node->results.bss_info->BSSID);
#else
				rssi_node = dtoh16(node->results.bss_info->RSSI);
#endif
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
#endif
		}
	}
	dump_bss_cache(
#if defined(RSSIAVG)
		rssi_cache_ctrl,
#endif
		*bss_head);
}

void
wl_release_bss_cache_ctrl(wl_bss_cache_ctrl_t *bss_cache_ctrl)
{
	ANDROID_TRACE(("%s:\n", __FUNCTION__));
	wl_free_bss_cache(bss_cache_ctrl);
}
#endif


