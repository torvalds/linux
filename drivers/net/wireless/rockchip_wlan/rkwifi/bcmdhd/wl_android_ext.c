/* SPDX-License-Identifier: GPL-2.0 */


#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/netlink.h>

#include <wl_android.h>
#include <wldev_common.h>
#include <wlioctl.h>
#include <bcmutils.h>
#include <linux_osl.h>
#include <dhd_dbg.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_config.h>

#define htod32(i) i
#define htod16(i) i
#define dtoh32(i) i
#define dtoh16(i) i
#define htodchanspec(i) i
#define dtohchanspec(i) i
#define strtoul(nptr, endptr, base) bcm_strtoul((nptr), (endptr), (base))

#define CMD_CHANNEL				"CHANNEL"
#define CMD_CHANNELS			"CHANNELS"
#define CMD_ROAM_TRIGGER		"ROAM_TRIGGER"
#define CMD_KEEP_ALIVE			"KEEP_ALIVE"
#define CMD_PM					"PM"
#define CMD_MONITOR				"MONITOR"
#define CMD_SET_SUSPEND_BCN_LI_DTIM		"SET_SUSPEND_BCN_LI_DTIM"

#ifdef WL_EXT_IAPSTA
#define CMD_IAPSTA_INIT			"IAPSTA_INIT"
#define CMD_IAPSTA_CONFIG		"IAPSTA_CONFIG"
#define CMD_IAPSTA_ENABLE		"IAPSTA_ENABLE"
#define CMD_IAPSTA_DISABLE		"IAPSTA_DISABLE"
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
#define CMD_WL		"WL"

#define IEEE80211_BAND_2GHZ 0
#define IEEE80211_BAND_5GHZ 1

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

#ifdef WL_EXT_IAPSTA
int wl_ext_iovar_setbuf_bsscfg(struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, s32 bsscfg_idx, struct mutex* buf_sync)
{
	int ret;
	
	ret = wldev_iovar_setbuf_bsscfg(dev, iovar_name, param, paramlen,
		buf, buflen, bsscfg_idx, buf_sync);
	if (ret < 0)
		ANDROID_ERROR(("%s: iovar_name=%s ret=%d\n", __FUNCTION__, iovar_name, ret));

	return ret;
}

int wl_ext_iovar_getbuf_bsscfg(struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, s32 bsscfg_idx, struct mutex* buf_sync)
{
	int ret;
	
	ret = wldev_iovar_getbuf_bsscfg(dev, iovar_name, param, paramlen,
		buf, buflen, bsscfg_idx, buf_sync);
	if (ret < 0)
		ANDROID_ERROR(("%s: iovar_name=%s ret=%d\n", __FUNCTION__, iovar_name, ret));

	return ret;
}
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

static int
wl_ext_get_ioctl_ver(struct net_device *dev, int *ioctl_ver)
{
	int ret = 0;
	s32 val = 0;

	val = 1;
	ret = wl_ext_ioctl(dev, WLC_GET_VERSION, &val, sizeof(val), 0);
	if (ret) {
		ANDROID_ERROR(("WLC_GET_VERSION failed, err=%d\n", ret));
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
wl_ext_set_chanspec(struct net_device *dev, uint16 channel)
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
	int ioctl_ver = 0;

	if (_chan <= CH_MAX_2G_CHANNEL)
		band = IEEE80211_BAND_2GHZ;
	else
		band = IEEE80211_BAND_5GHZ;
	wl_ext_get_ioctl_ver(dev, &ioctl_ver);

	if (band == IEEE80211_BAND_5GHZ) {
		param.band = WLC_BAND_5G;
		err = wldev_iovar_getbuf(dev, "bw_cap", &param, sizeof(param),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		if (err) {
			if (err != BCME_UNSUPPORTED) {
				ANDROID_ERROR(("bw_cap failed, %d\n", err));
				return err;
			} else {
				err = wldev_iovar_getint(dev, "mimo_bw_cap", &bw_cap);
				if (err) {
					ANDROID_ERROR(("error get mimo_bw_cap (%d)\n", err));
				}
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
			if ((err = wldev_iovar_setint(dev, "chanspec", fw_chspec)) == BCME_BADCHAN) {
				if (bw == WL_CHANSPEC_BW_80)
					goto change_bw;
				wl_ext_ioctl(dev, WLC_SET_CHANNEL, &_chan, sizeof(_chan), 1);
				printf("%s: channel %d\n", __FUNCTION__, _chan);
			} else if (err) {
				ANDROID_ERROR(("%s: failed to set chanspec error %d\n", __FUNCTION__, err));
			} else
				printf("%s: channel %d, 0x%x\n", __FUNCTION__, channel, chspec);
		} else {
			ANDROID_ERROR(("%s: failed to convert host chanspec to fw chanspec\n", __FUNCTION__));
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

	return err;
}

int
wl_ext_channel(struct net_device *dev, char* command, int total_len)
{
	int ret;
	int channel=0;
	channel_info_t ci;
	int bytes_written = 0;

	ANDROID_TRACE(("%s: cmd %s\n", __FUNCTION__, command));

	sscanf(command, "%*s %d", &channel);

	if (channel > 0) {
		ret = wl_ext_set_chanspec(dev, channel);
	} else {
		if (!(ret = wldev_ioctl(dev, WLC_GET_CHANNEL, &ci, sizeof(channel_info_t), FALSE))) {
			ANDROID_TRACE(("hw_channel %d\n", ci.hw_channel));
			ANDROID_TRACE(("target_channel %d\n", ci.target_channel));
			ANDROID_TRACE(("scan_channel %d\n", ci.scan_channel));
			bytes_written = snprintf(command, sizeof(channel_info_t)+2, "channel %d", ci.hw_channel);
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
	ret = wldev_ioctl(dev, WLC_GET_VALID_CHANNELS, valid_chan_list, sizeof(valid_chan_list), 0);
	if (ret<0) {
		ANDROID_ERROR(("%s: get channels failed with %d\n", __FUNCTION__, ret));
	} else {
		bytes_written = snprintf(command, total_len, "channels");
		for (i = 0; i < dtoh32(list->count); i++) {
			bytes_written += snprintf(command+bytes_written, total_len, " %d", dtoh32(list->element[i]));
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
		ret = wldev_ioctl(dev, WLC_SET_ROAM_TRIGGER, roam_trigger, sizeof(roam_trigger), 1);
		if (ret)
			ANDROID_ERROR(("WLC_SET_ROAM_TRIGGER ERROR %d ret=%d\n", roam_trigger[0], ret));
	} else {
		roam_trigger[1] = WLC_BAND_2G;
		ret = wldev_ioctl(dev, WLC_GET_ROAM_TRIGGER, roam_trigger, sizeof(roam_trigger), 0);
		if (!ret)
			trigger[0] = roam_trigger[0];
		else
			ANDROID_ERROR(("2G WLC_GET_ROAM_TRIGGER ERROR %d ret=%d\n", roam_trigger[0], ret));

		roam_trigger[1] = WLC_BAND_5G;
		ret = wldev_ioctl(dev, WLC_GET_ROAM_TRIGGER, roam_trigger, sizeof(roam_trigger), 0);
		if (!ret)
			trigger[1] = roam_trigger[0];
		else
			ANDROID_ERROR(("5G WLC_GET_ROAM_TRIGGER ERROR %d ret=%d\n", roam_trigger[0], ret));

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
		ret = wl_ext_iovar_getbuf(dev, "mkeep_alive", &id, sizeof(id), buf, sizeof(buf), NULL);
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
		bytes_written = snprintf(command, total_len, "mkeep_alive_period_msec %d ", dtoh32(mkeep_alive_pktp->period_msec));
		bytes_written += snprintf(command+bytes_written, total_len, "0x");
		for (i=0; i<mkeep_alive_pktp->len_bytes; i++) {
			bytes_written += snprintf(command+bytes_written, total_len, "%x", mkeep_alive_pktp->data[i]);
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
		ret = wldev_ioctl(dev, WLC_SET_PM, &pm, sizeof(pm), FALSE);
		if (ret)
			ANDROID_ERROR(("WLC_SET_PM ERROR %d ret=%d\n", pm, ret));
	} else {
		ret = wldev_ioctl(dev, WLC_GET_PM, &pm, sizeof(pm), FALSE);
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
		ret = wldev_ioctl(dev, WLC_SET_MONITOR, &val, sizeof(int), 1);
		if (ret)
			ANDROID_ERROR(("WLC_SET_MONITOR ERROR %d ret=%d\n", val, ret));
	} else {
		ret = wldev_ioctl(dev, WLC_GET_MONITOR, &val, sizeof(val), FALSE);
		if (!ret) {
			ANDROID_TRACE(("%s: monitor = %d\n", __FUNCTION__, val));
			bytes_written = snprintf(command, total_len, "monitor %d", val);
			ANDROID_TRACE(("%s: command result is %s\n", __FUNCTION__, command));
			ret = bytes_written;
		}
	}

	return ret;
}

#ifdef WL_EXT_IAPSTA
struct wl_apsta_params g_apsta_params;
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
		ANDROID_TRACE(("%s: Network mode: : B/G mixed\n", __FUNCTION__));
	} else if (bgnmode == IEEE80211BGN) {
		wl_ext_iovar_setint(dev, "nmode", 0);
		wl_ext_iovar_setint(dev, "nmode", 1);
		wl_ext_iovar_setint(dev, "vhtmode", 0);
		val = 1;
		wl_ext_ioctl(dev, WLC_SET_GMODE, &val, sizeof(val), 1);
		ANDROID_TRACE(("%s: Network mode: : B/G/N mixed\n", __FUNCTION__));
	} else if (bgnmode == IEEE80211BGNAC) {
		wl_ext_iovar_setint(dev, "nmode", 0);
		wl_ext_iovar_setint(dev, "nmode", 1);
		wl_ext_iovar_setint(dev, "vhtmode", 1);
		val = 1;
		wl_ext_ioctl(dev, WLC_SET_GMODE, &val, sizeof(val), 1);
		ANDROID_TRACE(("%s: Network mode: : B/G/N/AC mixed\n", __FUNCTION__));
	}
	wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);

	return 0;
}

static int
wl_ext_set_amode(struct wl_if_info *cur_if, struct wl_apsta_params *apsta_params)
{
	struct net_device *dev = cur_if->dev;
	authmode_t amode = cur_if->amode;
	int auth=0, wpa_auth=0;

	if (amode == AUTH_OPEN) {
		auth = 0;
		wpa_auth = 0;
		ANDROID_TRACE(("%s: Authentication: Open System\n", __FUNCTION__));
	} else if (amode == AUTH_SHARED) {
		auth = 1;
		wpa_auth = 0;
		ANDROID_TRACE(("%s: Authentication: Shared Key\n", __FUNCTION__));
	} else if (amode == AUTH_WPAPSK) {
		auth = 0;
		wpa_auth = 4;
		ANDROID_TRACE(("%s: Authentication: WPA-PSK\n", __FUNCTION__));
	} else if (amode == AUTH_WPA2PSK) {
		auth = 0;
		wpa_auth = 128;
		ANDROID_TRACE(("%s: Authentication: WPA2-PSK\n", __FUNCTION__));
	} else if (amode == AUTH_WPAWPA2PSK) {
		auth = 0;
		wpa_auth = 132;
		ANDROID_TRACE(("%s: Authentication: WPA/WPA2-PSK\n", __FUNCTION__));
	}
	wl_ext_iovar_setint(dev, "auth", auth);

	wl_ext_iovar_setint(dev, "wpa_auth", wpa_auth);

	return 0;
}

static int
wl_ext_set_emode(struct wl_if_info *cur_if, struct wl_apsta_params *apsta_params)
{
	struct net_device *dev = cur_if->dev;
	int wsec=0;
	struct wl_wsec_key wsec_key;
	wsec_pmk_t psk;
	encmode_t emode = cur_if->emode;
	char *key = cur_if->key;

	memset(&wsec_key, 0, sizeof(wsec_key));
	memset(&psk, 0, sizeof(psk));
	if (emode == ENC_NONE) {
		wsec = 0;
		ANDROID_TRACE(("%s: Encryption: No securiy\n", __FUNCTION__));
	} else if (emode == ENC_WEP) {
		wsec = 1;
		wl_ext_parse_wep(key, &wsec_key);
		ANDROID_TRACE(("%s: Encryption: WEP\n", __FUNCTION__));
		ANDROID_TRACE(("%s: Key: %s\n", __FUNCTION__, wsec_key.data));
	} else if (emode == ENC_TKIP) {
		wsec = 2;
		psk.key_len = strlen(key);
		psk.flags = WSEC_PASSPHRASE;
		memcpy(psk.key, key, strlen(key));
		ANDROID_TRACE(("%s: Encryption: TKIP\n", __FUNCTION__));
		ANDROID_TRACE(("%s: Key: %s\n", __FUNCTION__, psk.key));
	} else if (emode == ENC_AES) {
		wsec = 4;
		psk.key_len = strlen(key);
		psk.flags = WSEC_PASSPHRASE;
		memcpy(psk.key, key, strlen(key));
		ANDROID_TRACE(("%s: Encryption: AES\n", __FUNCTION__));
		ANDROID_TRACE(("%s: Key: %s\n", __FUNCTION__, psk.key));
	} else if (emode == ENC_TKIPAES) {
		wsec = 6;
		psk.key_len = strlen(key);
		psk.flags = WSEC_PASSPHRASE;
		memcpy(psk.key, key, strlen(key));
		ANDROID_TRACE(("%s: Encryption: TKIP/AES\n", __FUNCTION__));
		ANDROID_TRACE(("%s: Key: %s\n", __FUNCTION__, psk.key));
	}

	wl_ext_iovar_setint(dev, "wsec", wsec);

	if (wsec == 1) {
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

static int
wl_ext_iapsta_init(struct net_device *dev, char *command, int total_len)
{
	s32 val = 0;
	char *pch, *pick_tmp, *param;
	wlc_ssid_t ssid = { 0, {0} };
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	struct wl_apsta_params *apsta_params = &g_apsta_params;
	wl_interface_create_t iface;
	struct dhd_pub *dhd;
	wl_p2p_if_t ifreq;

	if (apsta_params->init) {
		ANDROID_ERROR(("%s: don't init twice\n", __FUNCTION__));
		return -1;
	}

	dhd = dhd_get_pub(dev);
	memset(apsta_params, 0, sizeof(struct wl_apsta_params));

	ANDROID_TRACE(("%s: command=%s, len=%d\n", __FUNCTION__, command, total_len));

	pick_tmp = command;
	param = bcmstrtok(&pick_tmp, " ", 0); // skip iapsta_init
	param = bcmstrtok(&pick_tmp, " ", 0);
	while (param != NULL) {
		if (!strcmp(param, "mode")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch) {
				if (!strcmp(pch, "sta")) {
					apsta_params->apstamode = ISTAONLY_MODE;
				} else if (!strcmp(pch, "ap")) {
					apsta_params->apstamode = IAPONLY_MODE;
				} else if (!strcmp(pch, "apsta")) {
					apsta_params->apstamode = IAPSTA_MODE;
				} else if (!strcmp(pch, "dualap")) {
					apsta_params->apstamode = IDUALAP_MODE;
				} else if (!strcmp(pch, "gosta")) {
					if (!FW_SUPPORTED(dhd, p2p)) {
						return -1;
					}
					apsta_params->apstamode = IGOSTA_MODE;
				} else if (!strcmp(pch, "gcsta")) {
					if (!FW_SUPPORTED(dhd, p2p)) {
						return -1;
					}
					apsta_params->apstamode = IGCSTA_MODE;
				} else {
					ANDROID_ERROR(("%s: mode [sta|ap|apsta|dualap]\n", __FUNCTION__));
					return -1;
				}
			}
		} else if (!strcmp(param, "vifname")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch)
				strcpy(apsta_params->vif.ifname, pch);
			else {
				ANDROID_ERROR(("%s: vifname [wlan1]\n", __FUNCTION__));
				return -1;
			}
		}
		param = bcmstrtok(&pick_tmp, " ", 0);
	}

	if (apsta_params->apstamode == 0) {
		ANDROID_ERROR(("%s: mode [sta|ap|apsta|dualap]\n", __FUNCTION__));
		return -1;
	}

	apsta_params->pif.dev = dev;
	apsta_params->pif.bssidx = 0;
	strcpy(apsta_params->pif.ifname, dev->name);
	strcpy(apsta_params->pif.ssid, "tttp");
	apsta_params->pif.maxassoc = -1;
	apsta_params->pif.channel = 1;

	if (!strlen(apsta_params->vif.ifname))
		strcpy(apsta_params->vif.ifname, "wlan1");
	strcpy(apsta_params->vif.ssid, "tttv");
	apsta_params->vif.maxassoc = -1;
	apsta_params->vif.channel = 1;

	if (apsta_params->apstamode == ISTAONLY_MODE) {
		apsta_params->pif.ifmode = ISTA_MODE;
		apsta_params->pif.ifstate = IF_STATE_INIT;
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "apsta", 1); // keep 1 as we set in dhd_preinit_ioctls
		// don't set WLC_SET_AP to 0, some parameters will be reset, such as bcn_timeout and roam_off
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
	} else if (apsta_params->apstamode == IAPONLY_MODE) {
		apsta_params->pif.ifmode = IAP_MODE;
		apsta_params->pif.ifstate = IF_STATE_INIT;
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
	} else if (apsta_params->apstamode == IAPSTA_MODE) {
		apsta_params->pif.ifmode = ISTA_MODE;
		apsta_params->pif.ifstate = IF_STATE_INIT;
		apsta_params->vif.ifmode = IAP_MODE;
		apsta_params->vif.ifstate = IF_STATE_INIT;
		wl_ext_iovar_setint(dev, "mpc", 0);
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "apsta", 1);
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		if (FW_SUPPORTED(dhd, rsdb)) {
			bzero(&iface, sizeof(wl_interface_create_t));
			iface.ver = WL_INTERFACE_CREATE_VER;
			iface.flags = WL_INTERFACE_CREATE_AP;
			wl_ext_iovar_getbuf_bsscfg(dev, "interface_create", &iface, sizeof(iface), iovar_buf,
				WLC_IOCTL_SMLEN, 1, NULL);
		} else {
			wl_ext_iovar_setbuf_bsscfg(dev, "ssid", &ssid, sizeof(ssid), iovar_buf,
				WLC_IOCTL_SMLEN, 1, NULL);
		}
	}
	else if (apsta_params->apstamode == IDUALAP_MODE) {
		apsta_params->pif.ifmode = IAP_MODE;
		apsta_params->pif.ifstate = IF_STATE_INIT;
		apsta_params->vif.ifmode = IAP_MODE;
		apsta_params->vif.ifstate = IF_STATE_INIT;
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "apsta", 0);
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		val = 1;
		wl_ext_ioctl(dev, WLC_SET_AP, &val, sizeof(val), 1);
		/* IF SoftAP is enabled, disable arpoe or wlan1 will ping fail */
#ifdef ARP_OFFLOAD_SUPPORT
		/* IF SoftAP is enabled, disable arpoe */
		dhd_arp_offload_set(dhd, 0);
		dhd_arp_offload_enable(dhd, FALSE);
#endif /* ARP_OFFLOAD_SUPPORT */
		bzero(&iface, sizeof(wl_interface_create_t));
		iface.ver = WL_INTERFACE_CREATE_VER;
		iface.flags = WL_INTERFACE_CREATE_AP;
		wl_ext_iovar_getbuf_bsscfg(dev, "interface_create", &iface, sizeof(iface), iovar_buf,
			WLC_IOCTL_SMLEN, 1, NULL);
	}
	else if (apsta_params->apstamode == IGOSTA_MODE) {
		apsta_params->pif.ifmode = ISTA_MODE;
		apsta_params->pif.ifstate = IF_STATE_INIT;
		apsta_params->vif.ifmode = IAP_MODE;
		apsta_params->vif.ifstate = IF_STATE_INIT;
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "apsta", 1);
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		bzero(&ifreq, sizeof(wl_p2p_if_t));
		ifreq.type = htod32(WL_P2P_IF_GO);
		wl_ext_iovar_setbuf(dev, "p2p_ifadd", &ifreq, sizeof(ifreq),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
	}
	else if (apsta_params->apstamode == IGCSTA_MODE) {
		apsta_params->pif.ifmode = ISTA_MODE;
		apsta_params->pif.ifstate = IF_STATE_INIT;
		apsta_params->vif.ifmode = ISTA_MODE;
		apsta_params->vif.ifstate = IF_STATE_INIT;
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "apsta", 1);
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		bzero(&ifreq, sizeof(wl_p2p_if_t));
		ifreq.type = htod32(WL_P2P_IF_CLIENT);
		wl_ext_iovar_setbuf(dev, "p2p_ifadd", &ifreq, sizeof(ifreq),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
	}

	wl_ext_get_ioctl_ver(dev, &apsta_params->ioctl_ver);
	printf("%s: apstamode=%d\n", __FUNCTION__, apsta_params->apstamode);

	apsta_params->init = TRUE;

	return 0;
}

static int
wl_ext_iapsta_config(struct net_device *dev, char *command, int total_len)
{
	int i;
	char *pch, *pick_tmp, *param;
	struct wl_apsta_params *apsta_params = &g_apsta_params;
	char ifname[IFNAMSIZ+1];
	struct wl_if_info *cur_if = &apsta_params->pif;

	if (!apsta_params->init) {
		ANDROID_ERROR(("%s: please init first\n", __FUNCTION__));
		return -1;
	}

	ANDROID_TRACE(("%s: command=%s, len=%d\n", __FUNCTION__, command, total_len));

	pick_tmp = command;
	param = bcmstrtok(&pick_tmp, " ", 0); // skip iapsta_config
	param = bcmstrtok(&pick_tmp, " ", 0);

	if (param != NULL) {
		if (strcmp(param, "ifname")) {
			ANDROID_ERROR(("%s: first arg must be ifname\n", __FUNCTION__));
			return -1;
		}
	}

	while (param != NULL) {
		if (!strcmp(param, "ifname")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch)
				strcpy(ifname, pch);
			else {
				ANDROID_ERROR(("%s: ifname [wlanX]\n", __FUNCTION__));
				return -1;
			}
			if (!strcmp(apsta_params->pif.dev->name, ifname)) {
				cur_if = &apsta_params->pif;
			} else if (!strcmp(apsta_params->vif.ifname, ifname)) {
				cur_if = &apsta_params->vif;
			} else {
				ANDROID_ERROR(("%s: wrong ifname=%s in apstamode=%d\n", __FUNCTION__,
					ifname, apsta_params->apstamode));
				return -1;
			}
		} else if (!strcmp(param, "ssid")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch)
				strcpy(cur_if->ssid, pch);
		} else if (!strcmp(param, "bssid")) {
			pch = bcmstrtok(&pick_tmp, ": ", 0);
			for (i=0; i<6 && pch; i++) {
				((u8 *)&cur_if->bssid)[i] = (int)simple_strtol(pch, NULL, 16);
				pch = bcmstrtok(&pick_tmp, ": ", 0);
			}
		} else if (!strcmp(param, "bgnmode")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch) {
				if (!strcmp(pch, "b"))
					cur_if->bgnmode = IEEE80211B;
				else if (!strcmp(pch, "g"))
					cur_if->bgnmode = IEEE80211G;
				else if (!strcmp(pch, "bg"))
					cur_if->bgnmode = IEEE80211BG;
				else if (!strcmp(pch, "bgn"))
					cur_if->bgnmode = IEEE80211BGN;
				else if (!strcmp(pch, "bgnac"))
					cur_if->bgnmode = IEEE80211BGNAC;
				else {
					ANDROID_ERROR(("%s: bgnmode [b|g|bg|bgn|bgnac]\n", __FUNCTION__));
					return -1;
				}
			}
		} else if (!strcmp(param, "hidden")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch) {
				if (!strcmp(pch, "n"))
					cur_if->hidden = 0;
				else if (!strcmp(pch, "y"))
					cur_if->hidden = 1;
				else {
					ANDROID_ERROR(("%s: hidden [y|n]\n", __FUNCTION__));
					return -1;
				}
			}
		} else if (!strcmp(param, "maxassoc")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch)
				cur_if->maxassoc = (int)simple_strtol(pch, NULL, 10);
		} else if (!strcmp(param, "chan")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch)
				cur_if->channel = (int)simple_strtol(pch, NULL, 10);
		} else if (!strcmp(param, "amode")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch) {
				if (!strcmp(pch, "open"))
					cur_if->amode = AUTH_OPEN;
				else if (!strcmp(pch, "shared"))
					cur_if->amode = AUTH_SHARED;
				else if (!strcmp(pch, "wpapsk"))
					cur_if->amode = AUTH_WPAPSK;
				else if (!strcmp(pch, "wpa2psk"))
					cur_if->amode = AUTH_WPA2PSK;
				else if (!strcmp(pch, "wpawpa2psk")) 
					cur_if->amode = AUTH_WPAWPA2PSK;
				else {
					ANDROID_ERROR(("%s: amode [open|shared|wpapsk|wpa2psk|wpawpa2psk]\n",
						__FUNCTION__));
					return -1;
				}
			}
		} else if (!strcmp(param, "emode")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch) {
				if (!strcmp(pch, "none"))
					cur_if->emode = ENC_NONE;
				else if (!strcmp(pch, "wep"))
					cur_if->emode = ENC_WEP;
				else if (!strcmp(pch, "tkip"))
					cur_if->emode = ENC_TKIP;
				else if (!strcmp(pch, "aes"))
					cur_if->emode = ENC_AES;
				else if (!strcmp(pch, "tkipaes")) 
					cur_if->emode = ENC_TKIPAES;
				else {
					ANDROID_ERROR(("%s: emode [none|wep|tkip|aes|tkipaes]\n",
						__FUNCTION__));
					return -1;
				}
			}
		} else if (!strcmp(param, "key")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch) {
				strcpy(cur_if->key, pch);
			}
		}
		param = bcmstrtok(&pick_tmp, " ", 0);
	}

	return 0;
}

static int
wl_ext_iapsta_disable(struct net_device *dev, char *command, int total_len)
{
	char *pch, *pick_tmp, *param;
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	wlc_ssid_t ssid = { 0, {0} };
	scb_val_t scbval;
	struct {
		s32 tmp;
		s32 cfg;
		s32 val;
	} bss_setbuf;
	struct wl_apsta_params *apsta_params = &g_apsta_params;
	apstamode_t apstamode = apsta_params->apstamode;
	char ifname[IFNAMSIZ+1];
	struct wl_if_info *cur_if;
	struct dhd_pub *dhd;

	if (!apsta_params->init) {
		ANDROID_ERROR(("%s: please init first\n", __FUNCTION__));
		return -1;
	}

	ANDROID_TRACE(("%s: command=%s, len=%d\n", __FUNCTION__, command, total_len));
	dhd = dhd_get_pub(dev);

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
	if (!strcmp(apsta_params->pif.dev->name, ifname)) {
		cur_if = &apsta_params->pif;
	} else if (!strcmp(apsta_params->vif.ifname, ifname)) {
		cur_if = &apsta_params->vif;
	} else {
		ANDROID_ERROR(("%s: wrong ifname=%s\n", __FUNCTION__, ifname));
		return -1;
	}
	if (!cur_if->dev) {
		ANDROID_ERROR(("%s: %s is not ready\n", __FUNCTION__, ifname));
		return -1;
	}

	if (cur_if->ifmode == ISTA_MODE) {
		wl_ext_ioctl(cur_if->dev, WLC_DISASSOC, NULL, 0, 1);
	} else if (cur_if->ifmode == IAP_MODE) {
		// deauthenticate all STA first
		memcpy(scbval.ea.octet, &ether_bcast, ETHER_ADDR_LEN);
		wl_ext_ioctl(cur_if->dev, WLC_SCB_DEAUTHENTICATE, &scbval.ea, ETHER_ADDR_LEN, 1);
	}

	if (apstamode == IAPONLY_MODE) {
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_ioctl(dev, WLC_SET_SSID, &ssid, sizeof(ssid), 1); // reset ssid
		wl_ext_iovar_setint(dev, "mpc", 1);
	} else if ((apstamode==IAPSTA_MODE || apstamode==IGOSTA_MODE) &&
			cur_if->ifmode == IAP_MODE) {
		// vif is AP mode
		bss_setbuf.tmp = 0xffffffff;
		bss_setbuf.cfg = 0; // must be 0, or wlan1 can not be down
		bss_setbuf.val = htod32(0);
		wl_ext_iovar_setbuf(cur_if->dev, "bss", &bss_setbuf, sizeof(bss_setbuf),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		wl_ext_iovar_setint(dev, "mpc", 1);
#ifdef ARP_OFFLOAD_SUPPORT
		/* IF SoftAP is disabled, enable arpoe back for STA mode. */
		dhd_arp_offload_set(dhd, dhd_arp_mode);
		dhd_arp_offload_enable(dhd, TRUE);
#endif /* ARP_OFFLOAD_SUPPORT */
	} else if (apstamode == IDUALAP_MODE) {
		bss_setbuf.tmp = 0xffffffff;
		bss_setbuf.cfg = 0; // must be 0, or wlan1 can not be down
		bss_setbuf.val = htod32(0);
		wl_ext_iovar_setbuf(cur_if->dev, "bss", &bss_setbuf, sizeof(bss_setbuf),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
	}

#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO)
	if (cur_if==&apsta_params->vif && dhd->conf->disable_proptx!=0) {
		bool enabled;
		dhd_wlfc_get_enable(dhd, &enabled);
		if (enabled) {
			dhd_wlfc_deinit(dhd);
		}
	}
#endif 
#endif /* PROP_TXSTATUS_VSDB */

	cur_if->ifstate = IF_STATE_DISALBE;
	printf("%s: apstamode=%d, ifname=%s\n", __FUNCTION__, apstamode, ifname);

	return 0;
}

static int
wl_ext_iapsta_enable(struct net_device *dev, char *command, int total_len)
{
	int ret = 0;
	s32 val = 0;
	char *pch, *pick_tmp, *param;
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	wlc_ssid_t ssid = { 0, {0} };
	struct {
		s32 cfg;
		s32 val;
	} bss_setbuf;
	struct wl_apsta_params *apsta_params = &g_apsta_params;
	apstamode_t apstamode = apsta_params->apstamode;
	char ifname[IFNAMSIZ+1];
	struct wl_if_info *cur_if;
	char cmd[128] = "iapsta_stop ifname ";
	struct dhd_pub *dhd;

	if (!apsta_params->init) {
		ANDROID_ERROR(("%s: please init first\n", __FUNCTION__));
		return -1;
	}

	ANDROID_TRACE(("%s: command=%s, len=%d\n", __FUNCTION__, command, total_len));
	dhd = dhd_get_pub(dev);

	pick_tmp = command;
	param = bcmstrtok(&pick_tmp, " ", 0); // skip iapsta_enable
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
	if (!strcmp(apsta_params->pif.dev->name, ifname)) {
		cur_if = &apsta_params->pif;
	} else if (!strcmp(apsta_params->vif.ifname, ifname)) {
		cur_if = &apsta_params->vif;
	} else {
		ANDROID_ERROR(("%s: wrong ifname=%s\n", __FUNCTION__, ifname));
		return -1;
	}
	if (!cur_if->dev) {
		ANDROID_ERROR(("%s: %s is not ready\n", __FUNCTION__, ifname));
		return -1;
	}
	ssid.SSID_len = strlen(cur_if->ssid);
	memcpy(ssid.SSID, cur_if->ssid, ssid.SSID_len);
	ANDROID_TRACE(("%s: apstamode=%d, bssidx=%d\n", __FUNCTION__, apstamode, cur_if->bssidx));

	snprintf(cmd, 128, "iapsta_stop ifname %s", cur_if->ifname);
	ret = wl_ext_iapsta_disable(dev, cmd, strlen(cmd));
	if (ret)
		goto exit;

	if (cur_if == &apsta_params->vif) {
		wl_ext_iovar_setbuf(cur_if->dev, "cur_etheraddr", (u8 *)cur_if->dev->dev_addr,
			ETHER_ADDR_LEN, iovar_buf, WLC_IOCTL_SMLEN, NULL);
	}

	// set ssid for AP
	if (cur_if->ifmode == IAP_MODE) {
		wl_ext_iovar_setint(dev, "mpc", 0);
		if (apstamode == IAPONLY_MODE) {
			wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		} else if (apstamode==IAPSTA_MODE || apstamode==IGOSTA_MODE) {
			wl_ext_iovar_setbuf_bsscfg(cur_if->dev, "ssid", &ssid, sizeof(ssid),
				iovar_buf, WLC_IOCTL_SMLEN, cur_if->bssidx, NULL);
		}
	}

	if (cur_if->ifmode == IAP_MODE) {
		wl_ext_set_bgnmode(cur_if);
		wl_ext_set_chanspec(cur_if->dev, cur_if->channel);
	}
	wl_ext_set_amode(cur_if, apsta_params);
	wl_ext_set_emode(cur_if, apsta_params);

	if (apstamode == ISTAONLY_MODE || apstamode == IGCSTA_MODE) {
		if (!ETHER_ISBCAST(&cur_if->bssid) && !ETHER_ISNULLADDR(&cur_if->bssid)) {
			printf("%s: BSSID: %pM\n", __FUNCTION__, &cur_if->bssid);
			wl_ext_ioctl(cur_if->dev, WLC_SET_BSSID, &cur_if->bssid, ETHER_ADDR_LEN, 1);
		}
		val = 1;
		wl_ext_ioctl(dev, WLC_SET_INFRA, &val, sizeof(val), 1);
	}
	if (cur_if->ifmode == IAP_MODE) {
		if (cur_if->maxassoc >= 0)
			wl_ext_iovar_setint(dev, "maxassoc", cur_if->maxassoc);
		printf("%s: Broadcast SSID: %s\n", __FUNCTION__, cur_if->hidden ? "OFF":"ON");
		// terence: fix me, hidden does not work in dualAP mode
		wl_ext_ioctl(cur_if->dev, WLC_SET_CLOSED, &cur_if->hidden, sizeof(cur_if->hidden), 1);
	}

	if (apstamode == ISTAONLY_MODE || apstamode == IGCSTA_MODE) {
		wl_ext_ioctl(cur_if->dev, WLC_SET_SSID, &ssid, sizeof(ssid), 1);
	} else if (apstamode == IAPONLY_MODE) {
		wl_ext_ioctl(cur_if->dev, WLC_SET_SSID, &ssid, sizeof(ssid), 1);
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
	} else if (apstamode == IAPSTA_MODE || apstamode == IGOSTA_MODE) {
		if (cur_if->ifmode == ISTA_MODE) {
			wl_ext_ioctl(cur_if->dev, WLC_SET_SSID, &ssid, sizeof(ssid), 1);
		} else {
			if (FW_SUPPORTED(dhd, rsdb)) {
				wl_ext_ioctl(cur_if->dev, WLC_SET_SSID, &ssid, sizeof(ssid), 1);
			} else {
				bss_setbuf.cfg = htod32(cur_if->bssidx);
				bss_setbuf.val = htod32(1);
				wl_ext_iovar_setbuf(cur_if->dev, "bss", &bss_setbuf, sizeof(bss_setbuf),
					iovar_buf, WLC_IOCTL_SMLEN, NULL);
			}
#ifdef ARP_OFFLOAD_SUPPORT
			/* IF SoftAP is enabled, disable arpoe */
			dhd_arp_offload_set(dhd, 0);
			dhd_arp_offload_enable(dhd, FALSE);
#endif /* ARP_OFFLOAD_SUPPORT */
		}
	}
	else if (apstamode == IDUALAP_MODE) {
		wl_ext_ioctl(cur_if->dev, WLC_SET_SSID, &ssid, sizeof(ssid), 1);
	}

#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO)
	if (cur_if==&apsta_params->vif && !disable_proptx) {
		bool enabled;
		dhd_wlfc_get_enable(dhd, &enabled);
		if (!enabled) {
			dhd_wlfc_init(dhd);
			wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
		}
	}
#endif 
#endif /* PROP_TXSTATUS_VSDB */

	printf("%s: ifname=%s, SSID: %s\n", __FUNCTION__, ifname, cur_if->ssid);

	cur_if->ifstate = IF_STATE_ENABLE;

exit:
	return ret;
}

void
wl_android_ext_iapsta_disconnect_sta(struct net_device *dev, u32 channel)
{
	struct wl_apsta_params *apsta_params = &g_apsta_params;
	struct wl_if_info *cur_if = &apsta_params->vif;
	scb_val_t scbval;
	int ret;
	channel_info_t ci;
	struct dhd_pub *dhd;

	if (apsta_params->apstamode==IAPSTA_MODE && cur_if->ifstate==IF_STATE_ENABLE) {
		dhd = dhd_get_pub(dev);
		if (!FW_SUPPORTED(dhd, vsdb)) {
			if (!(ret = wldev_ioctl(cur_if->dev, WLC_GET_CHANNEL, &ci, sizeof(channel_info_t), FALSE))) {
				if (channel != ci.target_channel) {
					printf("%s: deauthenticate all STA on vif\n", __FUNCTION__);
					memcpy(scbval.ea.octet, &ether_bcast, ETHER_ADDR_LEN);
					wl_ext_ioctl(cur_if->dev, WLC_SCB_DEAUTHENTICATE, &scbval.ea, ETHER_ADDR_LEN, 1);
				}
			}
		}
	}
}

int wl_android_ext_attach_netdev(struct net_device *net, uint8 bssidx)
{
	g_apsta_params.vif.dev = net;
	g_apsta_params.vif.bssidx = bssidx;
	if (strlen(g_apsta_params.vif.ifname)) {
		memset(net->name, 0, sizeof(IFNAMSIZ));
		strcpy(net->name, g_apsta_params.vif.ifname);
		net->name[IFNAMSIZ - 1] = '\0';
	}
	if (g_apsta_params.pif.dev) {
		memcpy(net->dev_addr, g_apsta_params.pif.dev->dev_addr, ETHER_ADDR_LEN);
		net->dev_addr[0] |= 0x02;
	}

	return 0;
}

int wl_android_ext_dettach_netdev(void)
{
	struct wl_apsta_params *apsta_params = &g_apsta_params;

	ANDROID_TRACE(("%s: Enter\n", __FUNCTION__));
	memset(apsta_params, 0, sizeof(struct wl_apsta_params));

	return 0;
}
#endif

#ifdef IDHCP
int wl_ext_ip_dump(int ip, char *buf)
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

/*
dhd_priv dhd [string] ==> Not ready
1. Get dhd val:
  Ex: dhd_priv dhd bussleep
2. Set dhd val:
  Ex: dhd_priv dhd bussleep 1

dhd_priv wl [WLC_GET_PM]  ==> Ready to get int val
dhd_priv wl [WLC_SET_PM] [int]  ==> Ready to set int val
dhd_priv wl [string]  ==> Ready to get int val
dhd_priv wl [string] [int]  ==> Ready to set int val
Ex: get/set WLC_PM
  dhd_priv wl 85
  dhd_priv wl 86 1
Ex: get/set mpc
  dhd_priv wl mpc
  dhd_priv wl mpc 1
*/
int
wl_ext_iovar(struct net_device *dev, char *command, int total_len)
{
	int ret = 0;
	char wl[3]="\0", arg[20]="\0", cmd_str[20]="\0", val_str[20]="\0";
	int cmd=-1, val=0;
	int bytes_written=-1;

	ANDROID_TRACE(("%s: cmd %s\n", __FUNCTION__, command));

	sscanf(command, "%s %d %s", wl, &cmd, arg);
	if (cmd < 0)
		sscanf(command, "%s %s %s", wl, cmd_str, val_str);

	if (!strcmp(wl, "wl")) {
		if (cmd>=0 && cmd!=WLC_GET_VAR && cmd!=WLC_SET_VAR) {
			ret = sscanf(arg, "%d", &val);
			if (ret > 0) { // set
				ret = wl_ext_ioctl(dev, cmd, &val, sizeof(val), TRUE);
			} else { // get
				ret = wl_ext_ioctl(dev, cmd, &val, sizeof(val), FALSE);
				if (!ret) {
					bytes_written = snprintf(command, total_len, "%d", val);
					ANDROID_TRACE(("%s: command result is %s\n", __FUNCTION__, command));
					ret = bytes_written;
				}
			}
		} else if (strlen(cmd_str)) {
			ret = sscanf(val_str, "%d", &val);
			if (ret > 0) { // set
				ret = wl_ext_iovar_setint(dev, cmd_str, val);
			} else { // get
				ret = wl_ext_iovar_getint(dev, cmd_str, &val);
				if (!ret) {
					bytes_written = snprintf(command, total_len, "%d", val);
					ANDROID_TRACE(("%s: command result is %s\n", __FUNCTION__, command));
					ret = bytes_written;
				}
			}
		}
	}

	return ret;
}

int wl_android_ext_priv_cmd(struct net_device *net, char *command, int total_len,
		int *bytes_written)
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
		*bytes_written = wl_ext_iapsta_init(net, command, total_len);
	}
	else if (strnicmp(command, CMD_IAPSTA_CONFIG, strlen(CMD_IAPSTA_CONFIG)) == 0) {
		*bytes_written = wl_ext_iapsta_config(net, command, total_len);
	}
	else if (strnicmp(command, CMD_IAPSTA_ENABLE, strlen(CMD_IAPSTA_ENABLE)) == 0) {
		*bytes_written = wl_ext_iapsta_enable(net, command, total_len);
	}
	else if (strnicmp(command, CMD_IAPSTA_DISABLE, strlen(CMD_IAPSTA_DISABLE)) == 0) {
		*bytes_written = wl_ext_iapsta_disable(net, command, total_len);
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
	else if (strnicmp(command, CMD_WL, strlen(CMD_WL)) == 0) {
		*bytes_written = wl_ext_iovar(net, command, total_len);
	}
	else
		ret = -1;

	return ret;
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
wl_delete_disconnected_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl, u8 *bssid)
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
wl_update_connected_rssi_cache(struct net_device *net, wl_rssi_cache_ctrl_t *rssi_cache_ctrl, int *rssi_avg)
{
	wl_rssi_cache_t *node, *prev, *leaf, **rssi_head;
	int j, k=0;
	int rssi, error=0;
	struct ether_addr bssid;
	struct timeval now, timeout;

	if (!g_wifi_on)
		return 0;

	error = wldev_ioctl(net, WLC_GET_BSSID, &bssid, sizeof(bssid), false);
	if (error == BCME_NOTASSOCIATED) {
		ANDROID_INFO(("%s: Not Associated! res:%d\n", __FUNCTION__, error));
		return 0;
	}
	if (error) {
		ANDROID_ERROR(("Could not get bssid (%d)\n", error));
	}
	error = wldev_get_rssi(net, &rssi);
	if (error) {
		ANDROID_ERROR(("Could not get rssi (%d)\n", error));
		return error;
	}

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
wl_update_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl, wl_scan_results_t *ss_list)
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
wl_delete_disconnected_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl, u8 *bssid)
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
			__FUNCTION__, k, &node->results.bss_info->BSSID, rssi, node->results.bss_info->SSID));
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
				rssi_node = wl_get_avg_rssi(rssi_cache_ctrl, &node->results.bss_info->BSSID);
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


