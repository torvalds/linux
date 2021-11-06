
#ifdef WL_EXT_IAPSTA
#include <net/rtnetlink.h>
#include <bcmendian.h>
#include <wl_android.h>
#include <dhd_config.h>
#ifdef WL_CFG80211
#include <wl_cfg80211.h>
#endif /* WL_CFG80211 */
#ifdef WL_ESCAN
#include <wl_escan.h>
#endif /* WL_ESCAN */

#define IAPSTA_ERROR(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_ERROR_LEVEL) { \
			printk(KERN_ERR DHD_LOG_PREFIX "[%s] IAPSTA-ERROR) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)
#define IAPSTA_TRACE(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_TRACE_LEVEL) { \
			printk(KERN_INFO DHD_LOG_PREFIX "[%s] IAPSTA-TRACE) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)
#define IAPSTA_INFO(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_INFO_LEVEL) { \
			printk(KERN_INFO DHD_LOG_PREFIX "[%s] IAPSTA-INFO) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)
#define IAPSTA_DBG(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_DBG_LEVEL) { \
			printk(KERN_INFO DHD_LOG_PREFIX "[%s] IAPSTA-DBG) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)

#ifdef PROP_TXSTATUS
#include <dhd_wlfc.h>
#ifdef PROP_TXSTATUS_VSDB
extern int disable_proptx;
#endif /* PROP_TXSTATUS_VSDB */
#endif /* PROP_TXSTATUS */

#define CSA_FW_BIT		(1<<0)
#define CSA_DRV_BIT		(1<<1)

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

enum wifi_isam_reason {
	ISAM_RC_MESH_ACS = 1,
	ISAM_RC_TPUT_MONITOR
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

static struct wl_if_info *
wl_get_cur_if(struct net_device *dev)
{
	dhd_pub_t *dhd = dhd_get_pub(dev);
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

	return cur_if;
}

#define WL_PM_ENABLE_TIMEOUT 10000
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
#define BCM_SET_CONTAINER_OF(entry, ptr, type, member) \
_Pragma("GCC diagnostic push") \
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"") \
entry = container_of((ptr), type, member); \
_Pragma("GCC diagnostic pop")
#else
#define BCM_SET_CONTAINER_OF(entry, ptr, type, member) \
entry = container_of((ptr), type, member);
#endif /* STRICT_GCC_WARNINGS */

static void
wl_ext_pm_work_handler(struct work_struct *work)
{
	struct wl_if_info *cur_if;
	s32 pm = PM_FAST;
	dhd_pub_t *dhd;

	BCM_SET_CONTAINER_OF(cur_if, work, struct wl_if_info, pm_enable_work.work);

	IAPSTA_TRACE("wlan", "%s: Enter\n", __FUNCTION__);

	if (cur_if->dev == NULL)
		return;

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif

	dhd = dhd_get_pub(cur_if->dev);

	if (!dhd || !dhd->up) {
		IAPSTA_TRACE(cur_if->ifname, "dhd is null or not up\n");
		return;
	}
	if (dhd_conf_get_pm(dhd) >= 0)
		pm = dhd_conf_get_pm(dhd);
	wl_ext_ioctl(cur_if->dev, WLC_SET_PM, &pm, sizeof(pm), 1);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
	DHD_PM_WAKE_UNLOCK(dhd);

}

void
wl_ext_add_remove_pm_enable_work(struct net_device *dev, bool add)
{
	dhd_pub_t *dhd = dhd_get_pub(dev);
	struct wl_if_info *cur_if = NULL;
	u16 wq_duration = 0;
	s32 pm = PM_OFF;

	cur_if = wl_get_cur_if(dev);
	if (!cur_if)
		return;

	mutex_lock(&cur_if->pm_sync);
	/*
	 * Make cancel and schedule work part mutually exclusive
	 * so that while cancelling, we are sure that there is no
	 * work getting scheduled.
	 */

	if (delayed_work_pending(&cur_if->pm_enable_work)) {
		cancel_delayed_work_sync(&cur_if->pm_enable_work);
		DHD_PM_WAKE_UNLOCK(dhd);
	}

	if (add) {
		wq_duration = (WL_PM_ENABLE_TIMEOUT);
	}

	/* It should schedule work item only if driver is up */
	if (dhd->up) {
		if (dhd_conf_get_pm(dhd) >= 0)
			pm = dhd_conf_get_pm(dhd);
		wl_ext_ioctl(cur_if->dev, WLC_SET_PM, &pm, sizeof(pm), 1);
		if (wq_duration) {
			if (schedule_delayed_work(&cur_if->pm_enable_work,
					msecs_to_jiffies((const unsigned int)wq_duration))) {
				DHD_PM_WAKE_LOCK_TIMEOUT(dhd, wq_duration);
			} else {
				IAPSTA_ERROR(cur_if->ifname, "Can't schedule pm work handler\n");
			}
		}
	}
	mutex_unlock(&cur_if->pm_sync);

}

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
		IAPSTA_TRACE(dev->name, "Network mode: B only\n");
	} else if (bgnmode == IEEE80211G) {
		wl_ext_iovar_setint(dev, "nmode", 0);
		val = 2;
		wl_ext_ioctl(dev, WLC_SET_GMODE, &val, sizeof(val), 1);
		IAPSTA_TRACE(dev->name, "Network mode: G only\n");
	} else if (bgnmode == IEEE80211BG) {
		wl_ext_iovar_setint(dev, "nmode", 0);
		val = 1;
		wl_ext_ioctl(dev, WLC_SET_GMODE, &val, sizeof(val), 1);
		IAPSTA_TRACE(dev->name, "Network mode: B/G mixed\n");
	} else if (bgnmode == IEEE80211BGN) {
		wl_ext_iovar_setint(dev, "nmode", 0);
		wl_ext_iovar_setint(dev, "nmode", 1);
		wl_ext_iovar_setint(dev, "vhtmode", 0);
		val = 1;
		wl_ext_ioctl(dev, WLC_SET_GMODE, &val, sizeof(val), 1);
		IAPSTA_TRACE(dev->name, "Network mode: B/G/N mixed\n");
	} else if (bgnmode == IEEE80211BGNAC) {
		wl_ext_iovar_setint(dev, "nmode", 0);
		wl_ext_iovar_setint(dev, "nmode", 1);
		wl_ext_iovar_setint(dev, "vhtmode", 1);
		val = 1;
		wl_ext_ioctl(dev, WLC_SET_GMODE, &val, sizeof(val), 1);
		IAPSTA_TRACE(dev->name, "Network mode: B/G/N/AC mixed\n");
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
			IAPSTA_INFO(dev->name, "SAE\n");
		} else {
			auth = WL_AUTH_OPEN_SYSTEM;
			wpa_auth = WPA_AUTH_DISABLED;
			IAPSTA_INFO(dev->name, "Open System\n");
		}
	} else
#endif /* WLMESH */
	if (amode == AUTH_OPEN) {
		auth = WL_AUTH_OPEN_SYSTEM;
		wpa_auth = WPA_AUTH_DISABLED;
		IAPSTA_INFO(dev->name, "Open System\n");
	} else if (amode == AUTH_SHARED) {
		auth = WL_AUTH_SHARED_KEY;
		wpa_auth = WPA_AUTH_DISABLED;
		IAPSTA_INFO(dev->name, "Shared Key\n");
	} else if (amode == AUTH_WPAPSK) {
		auth = WL_AUTH_OPEN_SYSTEM;
		wpa_auth = WPA_AUTH_PSK;
		IAPSTA_INFO(dev->name, "WPA-PSK\n");
	} else if (amode == AUTH_WPA2PSK) {
		auth = WL_AUTH_OPEN_SYSTEM;
		wpa_auth = WPA2_AUTH_PSK;
		IAPSTA_INFO(dev->name, "WPA2-PSK\n");
	} else if (amode == AUTH_WPAWPA2PSK) {
		auth = WL_AUTH_OPEN_SYSTEM;
		wpa_auth = WPA2_AUTH_PSK | WPA_AUTH_PSK;
		IAPSTA_INFO(dev->name, "WPA/WPA2-PSK\n");
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
		IAPSTA_INFO(dev->name, "No securiy\n");
	} else if (emode == ENC_WEP) {
		wsec = WEP_ENABLED;
		wl_ext_parse_wep(key, &wsec_key);
		IAPSTA_INFO(dev->name, "WEP key \"%s\"\n", wsec_key.data);
	} else if (emode == ENC_TKIP) {
		wsec = TKIP_ENABLED;
		psk.key_len = strlen(key);
		psk.flags = WSEC_PASSPHRASE;
		memcpy(psk.key, key, strlen(key));
		IAPSTA_INFO(dev->name, "TKIP key \"%s\"\n", psk.key);
	} else if (emode == ENC_AES || amode == AUTH_SAE) {
		wsec = AES_ENABLED;
		psk.key_len = strlen(key);
		psk.flags = WSEC_PASSPHRASE;
		memcpy(psk.key, key, strlen(key));
		IAPSTA_INFO(dev->name, "AES key \"%s\"\n", psk.key);
	} else if (emode == ENC_TKIPAES) {
		wsec = TKIP_ENABLED | AES_ENABLED;
		psk.key_len = strlen(key);
		psk.flags = WSEC_PASSPHRASE;
		memcpy(psk.key, key, strlen(key));
		IAPSTA_INFO(dev->name, "TKIP/AES key \"%s\"\n", psk.key);
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
			IAPSTA_INFO(dev->name, "AES key \"%s\"\n", key);
			wl_ext_iovar_setint(dev, "mesh_auth_proto", 1);
			wl_ext_iovar_setint(dev, "mfp", WL_MFP_REQUIRED);
			wl_ext_iovar_setbuf(dev, "sae_password", key, strlen(key),
				iovar_buf, WLC_IOCTL_SMLEN, NULL);
		} else {
			IAPSTA_INFO(dev->name, "No securiy\n");
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
				IAPSTA_ERROR(dev->name, "bw_cap failed, %d\n", err);
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
			IAPSTA_ERROR(dev->name, "failed to convert host chanspec to fw chanspec\n");
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
		IAPSTA_ERROR(dev->name, "Invalid chanspec 0x%x\n", chspec);
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

#if defined(WL_CFG80211) || (defined(WLMESH) && defined(WL_ESCAN))
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
#endif

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
	struct wl_if_info *cur_if;
	mesh_peer_info_dump_t *peer_results;
	mesh_peer_info_ext_t *mpi_ext;
	char *peer_buf = NULL;
	int peer_len = WLC_IOCTL_MAXLEN;
	int dump_written = 0, ret;

	if (!data) {
		peer_buf = kmalloc(peer_len, GFP_KERNEL);
		if (peer_buf == NULL) {
			IAPSTA_ERROR(dev->name, "Failed to allocate buffer of %d bytes\n",
				peer_len); 
			return -1;
		}
		cur_if = wl_get_cur_if(dev);
		if (cur_if && cur_if->ifmode == IMESH_MODE) {
			memset(peer_buf, 0, peer_len);
			ret = wl_mesh_get_peer_results(dev, peer_buf, peer_len);
			if (ret >= 0) {
				peer_results = (mesh_peer_info_dump_t *)peer_buf;
				mpi_ext = (mesh_peer_info_ext_t *)peer_results->mpi_ext;
				dump_written += wl_mesh_print_peer_info(mpi_ext,
					peer_results->count, command+dump_written,
					total_len-dump_written);
			}
		} else if (cur_if) {
			IAPSTA_ERROR(dev->name, "[%s][%c] is not mesh interface\n",
				cur_if->ifname, cur_if->prefix);
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
		IAPSTA_ERROR("wlan", "mesh_if is not ready\n");
		return;
	}

	if (!mesh_if->dev) {
		IAPSTA_ERROR("wlan", "ifidx %d is not ready\n", mesh_if->ifidx);
		return;
	}
	dhd = dhd_get_pub(mesh_if->dev);

	bzero(&msg, sizeof(wl_event_msg_t));
	IAPSTA_TRACE(mesh_if->dev->name, "timer expired\n");

	msg.ifidx = mesh_if->ifidx;
	msg.event_type = hton32(WLC_E_RESERVED);
	msg.reason = hton32(ISAM_RC_MESH_ACS);
	wl_ext_event_send(dhd->event_params, &msg, NULL);
}

static void
wl_mesh_set_timer(struct wl_if_info *mesh_if, uint timeout)
{
	IAPSTA_TRACE(mesh_if->dev->name, "timeout=%d\n", timeout);

	if (timer_pending(&mesh_if->delay_scan))
		del_timer_sync(&mesh_if->delay_scan);

	if (timeout) {
		if (timer_pending(&mesh_if->delay_scan))
			del_timer_sync(&mesh_if->delay_scan);
		mod_timer(&mesh_if->delay_scan, jiffies + msecs_to_jiffies(timeout));
	}
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
		IAPSTA_ERROR(dev->name, "IE memory alloc failed\n");
		err = -ENOMEM;
		goto exit;
	}

	iovar_buf = kzalloc(WLC_IOCTL_MEDLEN, GFP_KERNEL);
	if (!iovar_buf) {
		IAPSTA_ERROR(dev->name, "iovar_buf alloc failed\n");
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

	IAPSTA_TRACE(mesh_if->dev->name, "Enter\n");

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
	char *vndr_ie;
	uchar mesh_oui[]={0x00, 0x22, 0xf4};
	int bytes_written = 0;
	int ret = 0, i, vndr_ie_len;
	uint8 *peer_bssid;

	wl_mesh_clear_vndr_ie(mesh_if->dev, mesh_oui);

	vndr_ie_len = WLC_IOCTL_MEDLEN;
	vndr_ie = kmalloc(vndr_ie_len, GFP_KERNEL);
	if (vndr_ie == NULL) {
		IAPSTA_ERROR(mesh_if->dev->name, "Failed to allocate buffer of %d bytes\n",
			WLC_IOCTL_MEDLEN); 
		ret = -1;
		goto exit;
	}

	bytes_written += snprintf(vndr_ie+bytes_written, vndr_ie_len,
		"0x%02x%02x%02x", mesh_oui[0], mesh_oui[1], mesh_oui[2]);

	bytes_written += snprintf(vndr_ie+bytes_written, vndr_ie_len,
		"%02x%02x%02x%02x%02x%02x%02x%02x", MESH_INFO_MASTER_BSSID, ETHER_ADDR_LEN,
		((u8 *)(&mesh_info->master_bssid))[0], ((u8 *)(&mesh_info->master_bssid))[1],
		((u8 *)(&mesh_info->master_bssid))[2], ((u8 *)(&mesh_info->master_bssid))[3],
		((u8 *)(&mesh_info->master_bssid))[4], ((u8 *)(&mesh_info->master_bssid))[5]);

	bytes_written += snprintf(vndr_ie+bytes_written, vndr_ie_len,
		"%02x%02x%02x", MESH_INFO_MASTER_CHANNEL, 1, mesh_info->master_channel);

	bytes_written += snprintf(vndr_ie+bytes_written, vndr_ie_len,
		"%02x%02x%02x", MESH_INFO_HOP_CNT, 1, mesh_info->hop_cnt);

	bytes_written += snprintf(vndr_ie+bytes_written, vndr_ie_len,
		"%02x%02x", MESH_INFO_PEER_BSSID, mesh_info->hop_cnt*ETHER_ADDR_LEN);
	for (i=0; i<mesh_info->hop_cnt && i<MAX_HOP_LIST; i++) {
		peer_bssid = (uint8 *)&mesh_info->peer_bssid[i];
		bytes_written += snprintf(vndr_ie+bytes_written, vndr_ie_len,
			"%02x%02x%02x%02x%02x%02x",
 			peer_bssid[0], peer_bssid[1], peer_bssid[2],
			peer_bssid[3], peer_bssid[4], peer_bssid[5]);
	}

	ret = wl_ext_add_del_ie(mesh_if->dev, VNDR_IE_BEACON_FLAG|VNDR_IE_PRBRSP_FLAG,
		vndr_ie, "add");
	if (!ret) {
		IAPSTA_INFO(mesh_if->dev->name, "mbssid=%pM, mchannel=%d, hop=%d, pbssid=%pM\n",
			&mesh_info->master_bssid, mesh_info->master_channel, mesh_info->hop_cnt,
			mesh_info->peer_bssid); 
	}

exit:
	if (vndr_ie)
		kfree(vndr_ie);
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
		mesh_info->hop_cnt = 0;
		memset(mesh_info->peer_bssid, 0, MAX_HOP_LIST*ETHER_ADDR_LEN);
		if (!wl_mesh_update_vndr_ie(apsta_params, mesh_if))
			updated = TRUE;
	}

	return updated;
}

static bool
wl_mesh_update_mesh_info(struct wl_apsta_params *apsta_params,
	struct wl_if_info *mesh_if)
{
	struct wl_mesh_params *mesh_info = &apsta_params->mesh_info, peer_mesh_info;
	uint32 count = 0;
	char *dump_buf = NULL;
	mesh_peer_info_dump_t *peer_results;
	mesh_peer_info_ext_t *mpi_ext;
	struct ether_addr bssid;
	bool updated = FALSE, bss_found = FALSE;
	uint16 cur_chan;

	dump_buf = kmalloc(WLC_IOCTL_MAXLEN, GFP_KERNEL);
	if (dump_buf == NULL) {
		IAPSTA_ERROR(mesh_if->dev->name, "Failed to allocate buffer of %d bytes\n",
			WLC_IOCTL_MAXLEN); 
		return FALSE;
	}
	count = wl_mesh_get_peer_results(mesh_if->dev, dump_buf, WLC_IOCTL_MAXLEN);
	if (count > 0) {
		memset(&bssid, 0, ETHER_ADDR_LEN);
		wldev_ioctl(mesh_if->dev, WLC_GET_BSSID, &bssid, ETHER_ADDR_LEN, 0);
		peer_results = (mesh_peer_info_dump_t *)dump_buf;
		mpi_ext = (mesh_peer_info_ext_t *)peer_results->mpi_ext;
		for (count = 0; count < peer_results->count; count++) {
			if (mpi_ext->entry_state != MESH_SELF_PEER_ENTRY_STATE_TIMEDOUT &&
					mpi_ext->peer_info.state == MESH_PEERING_ESTAB) {
				memset(&peer_mesh_info, 0, sizeof(struct wl_mesh_params));
				bss_found = wl_escan_mesh_info(mesh_if->dev, mesh_if->escan,
					&mpi_ext->ea, &peer_mesh_info);
				if (bss_found && (mesh_info->master_channel == 0 ||
						peer_mesh_info.hop_cnt <= mesh_info->hop_cnt) &&
						memcmp(&peer_mesh_info.peer_bssid, &bssid, ETHER_ADDR_LEN)) {
					memcpy(&mesh_info->master_bssid, &peer_mesh_info.master_bssid,
						ETHER_ADDR_LEN);
					mesh_info->master_channel = peer_mesh_info.master_channel;
					mesh_info->hop_cnt = peer_mesh_info.hop_cnt+1;
					memset(mesh_info->peer_bssid, 0, MAX_HOP_LIST*ETHER_ADDR_LEN);
					memcpy(&mesh_info->peer_bssid, &mpi_ext->ea, ETHER_ADDR_LEN);
					memcpy(&mesh_info->peer_bssid[1], peer_mesh_info.peer_bssid,
						(MAX_HOP_LIST-1)*ETHER_ADDR_LEN);
					updated = TRUE;
				}
			}
			mpi_ext++;
		}
		if (updated) {
			if (wl_mesh_update_vndr_ie(apsta_params, mesh_if)) {
				IAPSTA_ERROR(mesh_if->dev->name, "update failed\n");
				mesh_info->master_channel = 0;
				updated = FALSE;
				goto exit;
			}
		}
	}

	if (!mesh_info->master_channel) {
		wlc_ssid_t cur_ssid;
		char sec[32];
		bool sae = FALSE;
		memset(&peer_mesh_info, 0, sizeof(struct wl_mesh_params));
		wl_ext_ioctl(mesh_if->dev, WLC_GET_SSID, &cur_ssid, sizeof(cur_ssid), 0);
		wl_ext_get_sec(mesh_if->dev, mesh_if->ifmode, sec, sizeof(sec), FALSE);
		if (strnicmp(sec, "sae/sae", strlen("sae/sae")) == 0)
			sae = TRUE;
		cur_chan = wl_ext_get_chan(apsta_params, mesh_if->dev);
		bss_found = wl_escan_mesh_peer(mesh_if->dev, mesh_if->escan, &cur_ssid, cur_chan,
			sae, &peer_mesh_info);

		if (bss_found && peer_mesh_info.master_channel&&
				(cur_chan != peer_mesh_info.master_channel)) {
			WL_MSG(mesh_if->ifname, "moving channel %d -> %d\n",
				cur_chan, peer_mesh_info.master_channel);
			wl_ext_disable_iface(mesh_if->dev, mesh_if->ifname);
			mesh_if->channel = peer_mesh_info.master_channel;
			wl_ext_enable_iface(mesh_if->dev, mesh_if->ifname, 500);
		}
	}

exit:
	if (dump_buf)
		kfree(dump_buf);
	return updated;
}

static void
wl_mesh_event_handler(struct wl_apsta_params *apsta_params,
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
			event_type == WLC_E_RESERVED && reason == ISAM_RC_MESH_ACS) {
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
	IAPSTA_TRACE(mesh_if->dev->name, "Enter\n");

	del_timer_sync(&mesh_if->delay_scan);

	if (mesh_if->escan) {
		mesh_if->escan = NULL;
	}
}

static int
wl_mesh_escan_attach(dhd_pub_t *dhd, struct wl_if_info *mesh_if)
{
	IAPSTA_TRACE(mesh_if->dev->name, "Enter\n");

	mesh_if->escan = dhd->escan;
	init_timer_compat(&mesh_if->delay_scan, wl_mesh_timer, mesh_if);

	return 0;
}

static uint
wl_mesh_update_peer_path(struct wl_if_info *mesh_if, char *command,
	int total_len)
{
	struct wl_mesh_params peer_mesh_info;
	uint32 count = 0;
	char *dump_buf = NULL;
	mesh_peer_info_dump_t *peer_results;
	mesh_peer_info_ext_t *mpi_ext;
	int bytes_written = 0, j, k;
	bool bss_found = FALSE;

	dump_buf = kmalloc(WLC_IOCTL_MAXLEN, GFP_KERNEL);
	if (dump_buf == NULL) {
		IAPSTA_ERROR(mesh_if->dev->name, "Failed to allocate buffer of %d bytes\n",
			WLC_IOCTL_MAXLEN); 
		return FALSE;
	}
	count = wl_mesh_get_peer_results(mesh_if->dev, dump_buf, WLC_IOCTL_MAXLEN);
	if (count > 0) {
		peer_results = (mesh_peer_info_dump_t *)dump_buf;
		mpi_ext = (mesh_peer_info_ext_t *)peer_results->mpi_ext;
		for (count = 0; count < peer_results->count; count++) {
			if (mpi_ext->entry_state != MESH_SELF_PEER_ENTRY_STATE_TIMEDOUT &&
					mpi_ext->peer_info.state == MESH_PEERING_ESTAB) {
				memset(&peer_mesh_info, 0, sizeof(struct wl_mesh_params));
				bss_found = wl_escan_mesh_info(mesh_if->dev, mesh_if->escan,
					&mpi_ext->ea, &peer_mesh_info);
				if (bss_found) {
					bytes_written += snprintf(command+bytes_written, total_len,
						"\npeer=%pM, hop=%d",
						&mpi_ext->ea, peer_mesh_info.hop_cnt);
					for (j=1; j<peer_mesh_info.hop_cnt; j++) {
						bytes_written += snprintf(command+bytes_written,
							total_len, "\n");
						for (k=0; k<j; k++) {
							bytes_written += snprintf(command+bytes_written,
								total_len, " ");
						}
						bytes_written += snprintf(command+bytes_written, total_len,
							"%pM", &peer_mesh_info.peer_bssid[j]);
					}
				}
			}
			mpi_ext++;
		}
	}

	if (dump_buf)
		kfree(dump_buf);
	return bytes_written;
}

static int
wl_ext_isam_peer_path(struct net_device *dev, char *command, int total_len)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_mesh_params *mesh_info = &apsta_params->mesh_info;
	struct wl_if_info *tmp_if;
	uint16 chan = 0;
	char *dump_buf = NULL;
	int dump_len = WLC_IOCTL_MEDLEN;
	int dump_written = 0;
	int i;

	if (command || android_msg_level & ANDROID_INFO_LEVEL) {
		if (command) {
			dump_buf = command;
			dump_len = total_len;
		} else {
			dump_buf = kmalloc(dump_len, GFP_KERNEL);
			if (dump_buf == NULL) {
				IAPSTA_ERROR(dev->name, "Failed to allocate buffer of %d bytes\n",
					dump_len); 
				return -1;
			}
		}
		for (i=0; i<MAX_IF_NUM; i++) {
			tmp_if = &apsta_params->if_info[i];
			if (tmp_if->dev && tmp_if->ifmode == IMESH_MODE && apsta_params->macs) {
				chan = wl_ext_get_chan(apsta_params, tmp_if->dev);
				if (chan) {
					dump_written += snprintf(dump_buf+dump_written, dump_len,
						DHD_LOG_PREFIX "[%s-%c] mbssid=%pM, mchan=%d, hop=%d, pbssid=%pM",
						tmp_if->ifname, tmp_if->prefix, &mesh_info->master_bssid,
						mesh_info->master_channel, mesh_info->hop_cnt,
						&mesh_info->peer_bssid);
					dump_written += wl_mesh_update_peer_path(tmp_if,
						dump_buf+dump_written, dump_len-dump_written);
				}
			}
		}
		IAPSTA_INFO(dev->name, "%s\n", dump_buf);
	}

	if (!command && dump_buf)
		kfree(dump_buf);
	return dump_written;
}
#endif /* WL_ESCAN */
#endif /* WLMESH */

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
wl_ext_if_up(struct wl_apsta_params *apsta_params, struct wl_if_info *cur_if,
	bool force_enable)
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
		IAPSTA_ERROR(cur_if->ifname, "Wrong ifmode\n");
		return 0;
	}

	if (wl_ext_dfs_chan(cur_if->channel) && !apsta_params->radar && !force_enable) {
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
		IAPSTA_INFO(cur_if->ifname, "cur_chan=%d, target_chan=%d\n",
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
				IAPSTA_ERROR(cur_if->ifname, "fail to get chanspec\n");
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
			other_chan = wl_ext_same_band(apsta_params, cur_if, FALSE);
			if (other_chan) {
				cur_if->channel = other_chan;
			} else {
				auto_band = WLC_BAND_5G;
				other_chan = wl_ext_autochannel(cur_if->dev, ACS_FW_BIT|ACS_DRV_BIT,
					auto_band);
				if (other_chan)
					cur_if->channel = other_chan;
			}
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
				wl_ext_if_up(apsta_params, target_if, FALSE);
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

#ifdef PROPTX_MAXCOUNT
int
wl_ext_get_wlfc_maxcount(struct dhd_pub *dhd, int ifidx)
{
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *tmp_if, *cur_if = NULL;
	int i, maxcount = WL_TXSTATUS_FREERUNCTR_MASK;

	if (!apsta_params->rsdb)
		return maxcount;

	for (i=0; i<MAX_IF_NUM; i++) {
		tmp_if = &apsta_params->if_info[i];
		if (tmp_if->dev && tmp_if->ifidx == ifidx) {
			cur_if = tmp_if;
			maxcount = cur_if->transit_maxcount;
		}
	}

	if (cur_if)
		IAPSTA_INFO(cur_if->ifname, "update maxcount %d\n", maxcount);
	else
		IAPSTA_INFO("wlan", "update maxcount %d for ifidx %d\n", maxcount, ifidx);
	return maxcount;
}

static void
wl_ext_update_wlfc_maxcount(struct dhd_pub *dhd)
{
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *tmp_if;
	bool band_5g = FALSE;
	uint16 chan = 0;
	int i, ret;

	if (!apsta_params->rsdb)
		return;

	for (i=0; i<MAX_IF_NUM; i++) {
		tmp_if = &apsta_params->if_info[i];
		if (tmp_if->dev) {
			chan = wl_ext_get_chan(apsta_params, tmp_if->dev);
			if (chan > CH_MAX_2G_CHANNEL) {
				tmp_if->transit_maxcount = dhd->conf->proptx_maxcnt_5g;
				ret = dhd_wlfc_update_maxcount(dhd, tmp_if->ifidx,
					tmp_if->transit_maxcount);
				if (ret == 0)
					IAPSTA_INFO(tmp_if->ifname, "updated maxcount %d\n",
						tmp_if->transit_maxcount);
				band_5g = TRUE;
			}
		}
	}

	for (i=0; i<MAX_IF_NUM; i++) {
		tmp_if = &apsta_params->if_info[i];
		if (tmp_if->dev) {
			chan = wl_ext_get_chan(apsta_params, tmp_if->dev);
			if ((chan == 0) || (chan <= CH_MAX_2G_CHANNEL && chan >= CH_MIN_2G_CHANNEL)) {
				if (chan == 0) {
					tmp_if->transit_maxcount = WL_TXSTATUS_FREERUNCTR_MASK;
				} else if (band_5g) {
					tmp_if->transit_maxcount = dhd->conf->proptx_maxcnt_2g;
				} else {
					tmp_if->transit_maxcount = dhd->conf->proptx_maxcnt_5g;
				}
				ret = dhd_wlfc_update_maxcount(dhd, tmp_if->ifidx,
					tmp_if->transit_maxcount);
				if (ret == 0)
					IAPSTA_INFO(tmp_if->ifname, "updated maxcount %d\n",
						tmp_if->transit_maxcount);
			}
		}
	}
}
#endif /* PROPTX_MAXCOUNT */

#ifdef WL_CFG80211
static struct wl_if_info *
wl_ext_get_dfs_master_if(struct wl_apsta_params *apsta_params)
{
	struct wl_if_info *cur_if = NULL;
	uint16 chan = 0;
	int i;

	for (i=0; i<MAX_IF_NUM; i++) {
		cur_if = &apsta_params->if_info[i];
		if (!cur_if->dev || !wl_ext_master_if(cur_if))
			continue;
		chan = wl_ext_get_chan(apsta_params, cur_if->dev);
		if (wl_ext_dfs_chan(chan)) {
			return cur_if;
		}
	}
	return NULL;
}

static void
wl_ext_save_master_channel(struct wl_apsta_params *apsta_params,
	uint16 post_channel)
{
	struct wl_if_info *cur_if = NULL;
	uint16 chan = 0;
	int i;

	if (apsta_params->vsdb)
		return;

	for (i=0; i<MAX_IF_NUM; i++) {
		cur_if = &apsta_params->if_info[i];
		if (!cur_if->dev || !wl_ext_master_if(cur_if))
			continue;
		chan = wl_ext_get_chan(apsta_params, cur_if->dev);
		if (chan) {
			cur_if->prev_channel = chan;
			cur_if->post_channel = post_channel;
		}
	}
}

u32
wl_ext_iapsta_update_channel(dhd_pub_t *dhd, struct net_device *dev,
	u32 channel)
{
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *cur_if = NULL;

	cur_if = wl_get_cur_if(dev);
	if (cur_if) {
		wl_ext_isam_status(cur_if->dev, NULL, 0);
		cur_if->channel = channel;
		if (wl_ext_master_if(cur_if) && apsta_params->acs) {
			uint auto_band = WL_GET_BAND(channel);
			cur_if->channel = wl_ext_autochannel(cur_if->dev, apsta_params->acs,
				auto_band);
		}
		channel = wl_ext_move_cur_channel(apsta_params, cur_if);
		if (channel) {
			if (cur_if->ifmode == ISTA_MODE && wl_ext_dfs_chan(channel))
				wl_ext_save_master_channel(apsta_params, channel);
			wl_ext_move_other_channel(apsta_params, cur_if);
		}
		if (cur_if->ifmode == ISTA_MODE)
			wl_set_isam_status(cur_if, STA_CONNECTING);
	}

	return channel;
}

static int
wl_ext_iftype_to_ifmode(struct net_device *net, int wl_iftype, ifmode_t *ifmode)
{	
	switch (wl_iftype) {
		case WL_IF_TYPE_STA:
			*ifmode = ISTA_MODE;
			break;
		case WL_IF_TYPE_AP:
			*ifmode = IAP_MODE;
			break;
		case WL_IF_TYPE_P2P_GO:
			*ifmode = IGO_MODE;
			break;
		case WL_IF_TYPE_P2P_GC:
			*ifmode = IGC_MODE;
			break;
		default:
			IAPSTA_ERROR(net->name, "Unknown interface wl_iftype:0x%x\n", wl_iftype);
			return BCME_ERROR;
	}
	return BCME_OK;
}

void
wl_ext_iapsta_update_iftype(struct net_device *net, int ifidx, int wl_iftype)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *cur_if = NULL;

	IAPSTA_TRACE(net->name, "ifidx=%d, wl_iftype=%d\n", ifidx, wl_iftype);

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
		} else if (wl_iftype == WL_IF_TYPE_P2P_GO) {
			cur_if->ifmode = IGO_MODE;
			cur_if->prio = PRIO_AP;
			cur_if->prefix = 'P';
			apsta_params->vsdb = TRUE;
		} else if (wl_iftype == WL_IF_TYPE_P2P_GC) {
			cur_if->ifmode = IGC_MODE;
			cur_if->prio = PRIO_STA;
			cur_if->prefix = 'P';
			apsta_params->vsdb = TRUE;
			wl_ext_iovar_setint(cur_if->dev, "assoc_retry_max", 3);
		}
	}
}

void
wl_ext_iapsta_ifadding(struct net_device *net, int ifidx)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *cur_if = NULL;

	IAPSTA_TRACE(net->name, "ifidx=%d\n", ifidx);
	if (ifidx < MAX_IF_NUM) {
		cur_if = &apsta_params->if_info[ifidx];
		wl_set_isam_status(cur_if, IF_ADDING);
	}
}

bool
wl_ext_iapsta_iftype_enabled(struct net_device *net, int wl_iftype)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *cur_if = NULL;
	ifmode_t ifmode = 0;

	wl_ext_iftype_to_ifmode(net, wl_iftype, &ifmode);
	cur_if = wl_ext_if_enabled(apsta_params, ifmode);
	if (cur_if)
		return TRUE;

	return FALSE;
}

bool
wl_ext_iapsta_other_if_enabled(struct net_device *net)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *tmp_if;
	bool enabled = FALSE;
	int i;

	for (i=0; i<MAX_IF_NUM; i++) {
		tmp_if = &apsta_params->if_info[i];
		if (tmp_if && wl_get_isam_status(tmp_if, IF_READY)) {
			if (wl_ext_get_chan(apsta_params, tmp_if->dev)) {
				enabled = TRUE;
				break;
			}
		}
	}

	return enabled;
}

void
wl_ext_iapsta_enable_master_if(struct net_device *dev, bool post)
{
	dhd_pub_t *dhd = dhd_get_pub(dev);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *cur_if = NULL;
	int i;

	for (i=0; i<MAX_IF_NUM; i++) {
		cur_if = &apsta_params->if_info[i];
		if (cur_if && cur_if->post_channel) {
			if (post)
				cur_if->channel = cur_if->post_channel;
			else
				cur_if->channel = cur_if->prev_channel;
			wl_ext_if_up(apsta_params, cur_if, TRUE);
			cur_if->prev_channel = 0;
			cur_if->post_channel = 0;
		}
	}
}

void
wl_ext_iapsta_restart_master(struct net_device *dev)
{
	dhd_pub_t *dhd = dhd_get_pub(dev);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *ap_if = NULL;

	if (apsta_params->radar)
		return;

	ap_if = wl_ext_get_dfs_master_if(apsta_params);
	if (ap_if) {
		uint16 chan_2g, chan_5g;
		wl_ext_if_down(apsta_params, ap_if);
		wl_ext_iapsta_restart_master(dev);
		wl_ext_get_default_chan(ap_if->dev, &chan_2g, &chan_5g, TRUE);
		if (chan_5g)
			ap_if->channel = chan_5g;
		else if (chan_2g)
			ap_if->channel = chan_2g;
		else
			ap_if->channel = 0;
		if (ap_if->channel) {
			wl_ext_move_cur_channel(apsta_params, ap_if);
			wl_ext_if_up(apsta_params, ap_if, FALSE);
		}
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

#ifndef WL_STATIC_IF
s32
wl_ext_add_del_bss(struct net_device *ndev, s32 bsscfg_idx,
	int iftype, s32 del, u8 *addr)
{
	s32 ret = BCME_OK;
	s32 val = 0;
	u8 ioctl_buf[WLC_IOCTL_SMLEN];
	struct {
		s32 cfg;
		s32 val;
		struct ether_addr ea;
	} bss_setbuf;

	IAPSTA_TRACE(ndev->name, "wl_iftype:%d del:%d \n", iftype, del);

	bzero(&bss_setbuf, sizeof(bss_setbuf));

	/* AP=2, STA=3, up=1, down=0, val=-1 */
	if (del) {
		val = WLC_AP_IOV_OP_DELETE;
	} else if (iftype == WL_INTERFACE_TYPE_AP) {
		/* Add/role change to AP Interface */
		IAPSTA_TRACE(ndev->name, "Adding AP Interface\n");
		val = WLC_AP_IOV_OP_MANUAL_AP_BSSCFG_CREATE;
	} else if (iftype == WL_INTERFACE_TYPE_STA) {
		/* Add/role change to STA Interface */
		IAPSTA_TRACE(ndev->name, "Adding STA Interface\n");
		val = WLC_AP_IOV_OP_MANUAL_STA_BSSCFG_CREATE;
	} else {
		IAPSTA_ERROR(ndev->name, "add_del_bss NOT supported for IFACE type:0x%x", iftype);
		return -EINVAL;
	}

	if (!del) {
		wl_ext_bss_iovar_war(ndev, &val);
	}

	bss_setbuf.cfg = htod32(bsscfg_idx);
	bss_setbuf.val = htod32(val);

	if (addr) {
		memcpy(&bss_setbuf.ea.octet, addr, ETH_ALEN);
	}

	IAPSTA_INFO(ndev->name, "wl bss %d bssidx:%d\n", val, bsscfg_idx);
	ret = wl_ext_iovar_setbuf(ndev, "bss", &bss_setbuf, sizeof(bss_setbuf),
		ioctl_buf, WLC_IOCTL_SMLEN, NULL);
	if (ret != 0)
		IAPSTA_ERROR(ndev->name, "'bss %d' failed with %d\n", val, ret);

	return ret;
}

static int
wl_ext_interface_ops(struct net_device *dev,
	struct wl_apsta_params *apsta_params, int iftype, u8 *addr)
{
	s32 ret;
	struct wl_interface_create_v2 iface;
	wl_interface_create_v3_t iface_v3;
	struct wl_interface_info_v1 *info;
	wl_interface_info_v2_t *info_v2;
	uint32 ifflags = 0;
	bool use_iface_info_v2 = false;
	u8 ioctl_buf[WLC_IOCTL_SMLEN];
	wl_wlc_version_t wlc_ver;

	/* Interface create */
	bzero(&iface, sizeof(iface));

	if (addr) {
		ifflags |= WL_INTERFACE_MAC_USE;
	}

	ret = wldev_iovar_getbuf(dev, "wlc_ver", NULL, 0,
		&wlc_ver, sizeof(wl_wlc_version_t), NULL);
	if ((ret == BCME_OK) && (wlc_ver.wlc_ver_major >= 5)) {
		ret = wldev_iovar_getbuf(dev, "interface_create",
			&iface, sizeof(struct wl_interface_create_v2),
			ioctl_buf, sizeof(ioctl_buf), NULL);
		if ((ret == BCME_OK) && (*((uint32 *)ioctl_buf) == WL_INTERFACE_CREATE_VER_3)) {
			use_iface_info_v2 = true;
			bzero(&iface_v3, sizeof(wl_interface_create_v3_t));
			iface_v3.ver = WL_INTERFACE_CREATE_VER_3;
			iface_v3.iftype = iftype;
			iface_v3.flags = ifflags;
			if (addr) {
				memcpy(&iface_v3.mac_addr.octet, addr, ETH_ALEN);
			}
			ret = wl_ext_iovar_getbuf(dev, "interface_create",
				&iface_v3, sizeof(wl_interface_create_v3_t),
				ioctl_buf, sizeof(ioctl_buf), NULL);
			if (unlikely(ret)) {
				IAPSTA_ERROR(dev->name, "Interface v3 create failed!! ret %d\n", ret);
				return ret;
			}
		}
	}

	/* success case */
	if (use_iface_info_v2 == true) {
		info_v2 = (wl_interface_info_v2_t *)ioctl_buf;
		ret = info_v2->bsscfgidx;
	} else {
		/* Use v1 struct */
		iface.ver = WL_INTERFACE_CREATE_VER_2;
		iface.iftype = iftype;
		iface.flags = iftype | ifflags;
		if (addr) {
			memcpy(&iface.mac_addr.octet, addr, ETH_ALEN);
		}
		ret = wldev_iovar_getbuf(dev, "interface_create",
			&iface, sizeof(struct wl_interface_create_v2),
			ioctl_buf, sizeof(ioctl_buf), NULL);
		if (ret == BCME_OK) {
			info = (struct wl_interface_info_v1 *)ioctl_buf;
			ret = info->bsscfgidx;
		}
	}

	IAPSTA_INFO(dev->name, "wl interface create success!! bssidx:%d \n", ret);
	return ret;
}

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
	s32 ret;

	wl_set_isam_status(cur_if, IF_ADDING);
	ret = wl_ext_interface_ops(dev, apsta_params, iftype, addr);
	if (ret == BCME_UNSUPPORTED) {
		wl_ext_add_del_bss(dev, 1, iftype, 0, addr);
	}
	wl_ext_wait_netif_change(apsta_params, cur_if);
}

static void
wl_ext_iapsta_intf_add(struct net_device *dev, struct wl_apsta_params *apsta_params)
{
	struct dhd_pub *dhd;
	apstamode_t apstamode = apsta_params->apstamode;
	struct wl_if_info *cur_if;
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	wl_p2p_if_t ifreq;
	struct ether_addr mac_addr;

	dhd = dhd_get_pub(dev);
	bzero(&mac_addr, sizeof(mac_addr));

	if (apstamode == ISTAAP_MODE) {
		cur_if = &apsta_params->if_info[IF_VIF];
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_TYPE_AP, NULL);
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
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_TYPE_STA,
			(u8*)&mac_addr);
	}
	else if (apstamode == IDUALAP_MODE) {
		cur_if = &apsta_params->if_info[IF_VIF];
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_TYPE_AP, NULL);
	}
	else if (apstamode == ISTAAPAP_MODE) {
		u8 rand_bytes[2] = {0, };
		get_random_bytes(&rand_bytes, sizeof(rand_bytes));
		cur_if = &apsta_params->if_info[IF_VIF];
		memcpy(&mac_addr, dev->dev_addr, ETHER_ADDR_LEN);
		mac_addr.octet[0] |= 0x02;
		mac_addr.octet[5] += 0x01;
		memcpy(&mac_addr.octet[3], rand_bytes, sizeof(rand_bytes));
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_TYPE_AP,
			(u8*)&mac_addr);
		cur_if = &apsta_params->if_info[IF_VIF2];
		memcpy(&mac_addr, dev->dev_addr, ETHER_ADDR_LEN);
		mac_addr.octet[0] |= 0x02;
		mac_addr.octet[5] += 0x02;
		memcpy(&mac_addr.octet[3], rand_bytes, sizeof(rand_bytes));
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_TYPE_AP,
			(u8*)&mac_addr);
	}
#ifdef WLMESH
	else if (apstamode == ISTAMESH_MODE) {
		cur_if = &apsta_params->if_info[IF_VIF];
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_TYPE_STA, NULL);
	}
	else if (apstamode == IMESHAP_MODE) {
		cur_if = &apsta_params->if_info[IF_VIF];
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_TYPE_AP, NULL);
	}
	else if (apstamode == ISTAAPMESH_MODE) {
		cur_if = &apsta_params->if_info[IF_VIF];
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_TYPE_AP, NULL);
		cur_if = &apsta_params->if_info[IF_VIF2];
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_TYPE_STA, NULL);
	}
	else if (apstamode == IMESHAPAP_MODE) {
		cur_if = &apsta_params->if_info[IF_VIF];
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_TYPE_AP, NULL);
		cur_if = &apsta_params->if_info[IF_VIF2];
		wl_ext_interface_create(dev, apsta_params, cur_if, WL_INTERFACE_TYPE_AP, NULL);
	}
#endif /* WLMESH */

}
#endif /* WL_STATIC_IF */

void
wl_ext_update_eapol_status(dhd_pub_t *dhd, int ifidx, uint eapol_status)
{
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *cur_if = NULL;

	if (ifidx < MAX_IF_NUM) {
		cur_if = &apsta_params->if_info[ifidx];
		cur_if->eapol_status = eapol_status;
	}
}

static int
wl_ext_in4way_sync_sta(dhd_pub_t *dhd, struct wl_if_info *cur_if,
	uint action, enum wl_ext_status status, void *context)
{
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct net_device *dev = cur_if->dev;
	struct osl_timespec cur_ts, *sta_disc_ts = &apsta_params->sta_disc_ts;
	int ret = 0, err, cur_eapol_status;
	int max_wait_time, max_wait_cnt;
	int suppressed = 0, wpa_auth = 0;

	action = action & dhd->conf->in4way;
	cur_eapol_status = cur_if->eapol_status;
	IAPSTA_TRACE(dev->name, "status=%d, action=0x%x, in4way=0x%x\n",
		status, action, dhd->conf->in4way);

	switch (status) {
		case WL_EXT_STATUS_SCAN:
			wldev_ioctl(dev, WLC_GET_SCANSUPPRESS, &suppressed, sizeof(int), false);
			if (suppressed) {
				IAPSTA_ERROR(dev->name, "scan suppressed\n");
				ret = -EBUSY;
				break;
			}
			if (action & STA_NO_SCAN_IN4WAY) {
				if (apsta_params->sta_handshaking > 0 && apsta_params->sta_handshaking <= 3) {
					IAPSTA_ERROR(dev->name, "return -EBUSY cnt %d\n",
						apsta_params->sta_handshaking);
					apsta_params->sta_handshaking++;
					ret = -EBUSY;
					break;
				}
			}
			break;
 		case WL_EXT_STATUS_DISCONNECTING:
			if (cur_eapol_status >= EAPOL_STATUS_4WAY_START &&
					cur_eapol_status < EAPOL_STATUS_4WAY_DONE) {
				IAPSTA_ERROR(dev->name, "WPA failed at %d\n", cur_eapol_status);
				cur_if->eapol_status = EAPOL_STATUS_NONE;
			} else if (cur_eapol_status >= EAPOL_STATUS_WSC_START &&
					cur_eapol_status < EAPOL_STATUS_WSC_DONE) {
				IAPSTA_ERROR(dev->name, "WPS failed at %d\n", cur_eapol_status);
				cur_if->eapol_status = EAPOL_STATUS_NONE;
			}
			if (action & (STA_NO_SCAN_IN4WAY|STA_NO_BTC_IN4WAY)) {
				if (apsta_params->sta_handshaking) {
					if ((action & STA_NO_BTC_IN4WAY) && apsta_params->sta_btc_mode) {
						IAPSTA_INFO(dev->name, "status=%d, restore btc_mode %d\n",
							status, apsta_params->sta_btc_mode);
						wldev_iovar_setint(dev, "btc_mode", apsta_params->sta_btc_mode);
					}
					apsta_params->sta_handshaking = 0;
				}
			}
			if (action & STA_WAIT_DISCONNECTED) {
				uint32 diff_ms;
				max_wait_time = 200;
				max_wait_cnt = 20;
				osl_do_gettimeofday(sta_disc_ts);
				osl_do_gettimeofday(&cur_ts);
				diff_ms = osl_do_gettimediff(&cur_ts, sta_disc_ts)/1000;
				while (diff_ms < max_wait_time && max_wait_cnt) {
					IAPSTA_INFO(dev->name, "status=%d, max_wait_cnt=%d waiting...\n",
						status, max_wait_cnt);
					mutex_unlock(&apsta_params->in4way_sync);
					OSL_SLEEP(50);
					mutex_lock(&apsta_params->in4way_sync);
					max_wait_cnt--;
					osl_do_gettimeofday(&cur_ts);
					diff_ms = osl_do_gettimediff(&cur_ts, sta_disc_ts)/1000;
				}
				wake_up_interruptible(&dhd->conf->event_complete);
			}
			break;
		case WL_EXT_STATUS_CONNECTING:
			wl_ext_iovar_getint(dev, "wpa_auth", &wpa_auth);
			if ((wpa_auth >= 2) && !(wpa_auth & WPA2_AUTH_FT))
				cur_if->eapol_status = EAPOL_STATUS_4WAY_START;
			else
				cur_if->eapol_status = EAPOL_STATUS_NONE;
			if (action & (STA_NO_SCAN_IN4WAY|STA_NO_BTC_IN4WAY)) {
				if ((wpa_auth >= 2) && !(wpa_auth & WPA2_AUTH_FT) &&
						(cur_if->bssidx == 0)) {
					apsta_params->sta_handshaking = 1;
					if (action & STA_NO_BTC_IN4WAY) {
						err = wldev_iovar_getint(dev, "btc_mode", &apsta_params->sta_btc_mode);
						if (!err && apsta_params->sta_btc_mode) {
							IAPSTA_INFO(dev->name, "status=%d, disable current btc_mode %d\n",
								status, apsta_params->sta_btc_mode);
							wldev_iovar_setint(dev, "btc_mode", 0);
						}
					}
				}
			}
			if (action & STA_WAIT_DISCONNECTED) {
				uint32 diff_ms;
				max_wait_time = 200;
				max_wait_cnt = 10;
				osl_do_gettimeofday(&cur_ts);
				diff_ms = osl_do_gettimediff(&cur_ts, sta_disc_ts)/1000;
				while (diff_ms < max_wait_time && max_wait_cnt) {
					IAPSTA_INFO(dev->name, "status=%d, max_wait_cnt=%d waiting...\n",
						status, max_wait_cnt);
					mutex_unlock(&apsta_params->in4way_sync);
					OSL_SLEEP(50);
					mutex_lock(&apsta_params->in4way_sync);
					max_wait_cnt--;
					osl_do_gettimeofday(&cur_ts);
					diff_ms = osl_do_gettimediff(&cur_ts, sta_disc_ts)/1000;
				}
				wake_up_interruptible(&dhd->conf->event_complete);
			}
#ifdef WL_CLIENT_SAE
			if (action & STA_START_AUTH_DELAY) {
				struct wireless_dev *wdev = dev->ieee80211_ptr;
				max_wait_cnt = 5;
				while (max_wait_cnt) {
					if (wdev->conn_owner_nlportid)
						break;
					IAPSTA_INFO(dev->name, "status=%d, max_wait_cnt=%d, waiting...\n",
						status, max_wait_cnt);
					mutex_unlock(&apsta_params->in4way_sync);
					OSL_SLEEP(10);
					mutex_lock(&apsta_params->in4way_sync);
					max_wait_cnt--;
				}
				if (max_wait_cnt == 0) {
					wl_ext_ioctl(cur_if->dev, WLC_DISASSOC, NULL, 0, 1);
					ret = -1;
					break;
				}
			}
#endif
			break;
		case WL_EXT_STATUS_CONNECTED:
			if (cur_if->ifmode == ISTA_MODE) {
				dhd_conf_set_wme(dhd, cur_if->ifidx, 0);
				wake_up_interruptible(&dhd->conf->event_complete);
			}
			else if (cur_if->ifmode == IGC_MODE) {
				dhd_conf_set_mchan_bw(dhd, WL_P2P_IF_CLIENT, -1);
			}
			break;
		case WL_EXT_STATUS_DISCONNECTED:
			if (cur_eapol_status >= EAPOL_STATUS_4WAY_START &&
					cur_eapol_status < EAPOL_STATUS_4WAY_DONE) {
				IAPSTA_ERROR(dev->name, "WPA failed at %d\n", cur_eapol_status);
				cur_if->eapol_status = EAPOL_STATUS_NONE;
			} else if (cur_eapol_status >= EAPOL_STATUS_WSC_START &&
					cur_eapol_status < EAPOL_STATUS_WSC_DONE) {
				IAPSTA_ERROR(dev->name, "WPS failed at %d\n", cur_eapol_status);
				cur_if->eapol_status = EAPOL_STATUS_NONE;
			}
			if (action & (STA_NO_SCAN_IN4WAY|STA_NO_BTC_IN4WAY)) {
				if (apsta_params->sta_handshaking) {
					if ((action & STA_NO_BTC_IN4WAY) && apsta_params->sta_btc_mode) {
						IAPSTA_INFO(dev->name, "status=%d, restore btc_mode %d\n",
							status, apsta_params->sta_btc_mode);
						wldev_iovar_setint(dev, "btc_mode", apsta_params->sta_btc_mode);
					}
					apsta_params->sta_handshaking = 0;
				}
			}
			if (action & STA_WAIT_DISCONNECTED) {
				osl_do_gettimeofday(sta_disc_ts);
			}
			wake_up_interruptible(&dhd->conf->event_complete);
			break;
		case WL_EXT_STATUS_ADD_KEY:
			cur_if->eapol_status = EAPOL_STATUS_4WAY_DONE;
			if (action & (STA_NO_SCAN_IN4WAY|STA_NO_BTC_IN4WAY)) {
				if (apsta_params->sta_handshaking) {
					if ((action & STA_NO_BTC_IN4WAY) && apsta_params->sta_btc_mode) {
						IAPSTA_INFO(dev->name, "status=%d, restore btc_mode %d\n",
							status, apsta_params->sta_btc_mode);
						wldev_iovar_setint(dev, "btc_mode", apsta_params->sta_btc_mode);
					}
					apsta_params->sta_handshaking = 0;
				}
			}
			wake_up_interruptible(&dhd->conf->event_complete);
			break;
		default:
			IAPSTA_INFO(dev->name, "Unknown action=0x%x, status=%d\n", action, status);
	}

	return ret;
}

#ifdef WL_CFG80211
static int
wl_ext_in4way_sync_ap(dhd_pub_t *dhd, struct wl_if_info *cur_if,
	uint action, enum wl_ext_status status, void *context)
{
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct net_device *dev = cur_if->dev;
	struct osl_timespec cur_ts, *ap_disc_sta_ts = &apsta_params->ap_disc_sta_ts;
	u8 *ap_disc_sta_bssid = (u8*)&apsta_params->ap_disc_sta_bssid;
	uint32 diff_ms, timeout, max_wait_time = 300;
	int ret = 0, suppressed = 0;
	u8* mac_addr = context;
	bool wait = FALSE;

	action = action & dhd->conf->in4way;
	IAPSTA_TRACE(dev->name, "status=%d, action=0x%x, in4way=0x%x\n",
		status, action, dhd->conf->in4way);

	switch (status) {
		case WL_EXT_STATUS_SCAN:
			wldev_ioctl(dev, WLC_GET_SCANSUPPRESS, &suppressed, sizeof(int), false);
			if (suppressed) {
				IAPSTA_ERROR(dev->name, "scan suppressed\n");
				ret = -EBUSY;
				break;
			}
			break;
		case WL_EXT_STATUS_AP_ENABLED:
			if (cur_if->ifmode == IAP_MODE)
				dhd_conf_set_wme(dhd, cur_if->ifidx, 1);
			else if (cur_if->ifmode == IGO_MODE)
				dhd_conf_set_mchan_bw(dhd, WL_P2P_IF_GO, -1);
			break;
		case WL_EXT_STATUS_DELETE_STA:
			if (action & AP_WAIT_STA_RECONNECT) {
				osl_do_gettimeofday(&cur_ts);
				diff_ms = osl_do_gettimediff(&cur_ts, ap_disc_sta_ts)/1000;
				if (cur_if->ifmode == IAP_MODE &&
						mac_addr && diff_ms < max_wait_time &&
						!memcmp(ap_disc_sta_bssid, mac_addr, ETHER_ADDR_LEN)) {
					wait = TRUE;
				} else if (cur_if->ifmode == IGO_MODE &&
						cur_if->eapol_status == EAPOL_STATUS_WSC_DONE &&
						memcmp(&ether_bcast, mac_addr, ETHER_ADDR_LEN)) {
					wait = TRUE;
				}
				if (wait) {
					IAPSTA_INFO(dev->name, "status=%d, ap_recon_sta=%d, waiting %dms ...\n",
						status, apsta_params->ap_recon_sta, max_wait_time);
					mutex_unlock(&apsta_params->in4way_sync);
					timeout = wait_event_interruptible_timeout(apsta_params->ap_recon_sta_event,
						apsta_params->ap_recon_sta, msecs_to_jiffies(max_wait_time));
					mutex_lock(&apsta_params->in4way_sync);
					IAPSTA_INFO(dev->name, "status=%d, ap_recon_sta=%d, timeout=%d\n",
						status, apsta_params->ap_recon_sta, timeout);
					if (timeout > 0) {
						IAPSTA_INFO(dev->name, "skip delete STA %pM\n", mac_addr);
						ret = -1;
						break;
					}
				} else {
					IAPSTA_INFO(dev->name, "status=%d, ap_recon_sta=%d => 0\n",
						status, apsta_params->ap_recon_sta);
					apsta_params->ap_recon_sta = FALSE;
					if (cur_if->ifmode == IGO_MODE)
						cur_if->eapol_status = EAPOL_STATUS_NONE;
				}
			}
			break;
		case WL_EXT_STATUS_STA_DISCONNECTED:
			if (action & AP_WAIT_STA_RECONNECT) {
				IAPSTA_INFO(dev->name, "latest disc STA %pM ap_recon_sta=%d\n",
					ap_disc_sta_bssid, apsta_params->ap_recon_sta);
				osl_do_gettimeofday(ap_disc_sta_ts);
				memcpy(ap_disc_sta_bssid, mac_addr, ETHER_ADDR_LEN);
				apsta_params->ap_recon_sta = FALSE;
			}
			break;
		case WL_EXT_STATUS_STA_CONNECTED:
			if (action & AP_WAIT_STA_RECONNECT) {
				osl_do_gettimeofday(&cur_ts);
				diff_ms = osl_do_gettimediff(&cur_ts, ap_disc_sta_ts)/1000;
				if (diff_ms < max_wait_time &&
						!memcmp(ap_disc_sta_bssid, mac_addr, ETHER_ADDR_LEN)) {
					IAPSTA_INFO(dev->name, "status=%d, ap_recon_sta=%d => 1\n",
						status, apsta_params->ap_recon_sta);
					apsta_params->ap_recon_sta = TRUE;
					wake_up_interruptible(&apsta_params->ap_recon_sta_event);
				} else {
					apsta_params->ap_recon_sta = FALSE;
				}
			}
			break;
		default:
			IAPSTA_INFO(dev->name, "Unknown action=0x%x, status=%d\n", action, status);
	}

	return ret;
}

int
wl_ext_in4way_sync(struct net_device *dev, uint action,
	enum wl_ext_status status, void *context)
{
	dhd_pub_t *dhd = dhd_get_pub(dev);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *cur_if = NULL;
	int ret = 0;

	mutex_lock(&apsta_params->in4way_sync);
	cur_if = wl_get_cur_if(dev);
	if (cur_if) {
		if (cur_if->ifmode == ISTA_MODE || cur_if->ifmode == IGC_MODE)
			ret = wl_ext_in4way_sync_sta(dhd, cur_if, action, status, context);
		else if (cur_if->ifmode == IAP_MODE || cur_if->ifmode == IGO_MODE)
			ret = wl_ext_in4way_sync_ap(dhd, cur_if, action, status, context);
		else
			IAPSTA_INFO(dev->name, "Unknown mode %d\n", cur_if->ifmode);
	}
	mutex_unlock(&apsta_params->in4way_sync);

	return ret;
}

void
wl_ext_update_extsae_4way(struct net_device *dev,
	const struct ieee80211_mgmt *mgmt, bool tx)
{
	dhd_pub_t *dhd = dhd_get_pub(dev);
	struct wl_if_info *cur_if = NULL;
	uint32 auth_alg, auth_seq;
	uint eapol_status = 0;

	cur_if = wl_get_cur_if(dev);
	if (!cur_if)
		return;

	auth_alg = mgmt->u.auth.auth_alg;
	auth_seq = mgmt->u.auth.auth_transaction;
	if (auth_alg == WLAN_AUTH_SAE) {
		if (cur_if->ifmode == ISTA_MODE || cur_if->ifmode == IGC_MODE) {
			if (auth_seq == 1) {
				if (tx)
					eapol_status = AUTH_SAE_COMMIT_M1;
				else
					eapol_status = AUTH_SAE_COMMIT_M2;
			} else if (auth_seq == 2) {
				if (tx)
					eapol_status = AUTH_SAE_CONFIRM_M3;
				else
					eapol_status = AUTH_SAE_CONFIRM_M4;
			}
		} else if (cur_if->ifmode == IAP_MODE || cur_if->ifmode == IGO_MODE) {
			if (auth_seq == 1) {
				if (tx)
					eapol_status = AUTH_SAE_COMMIT_M2;
				else
					eapol_status = AUTH_SAE_COMMIT_M1;
			} else if (auth_seq == 2) {
				if (tx)	
					eapol_status = AUTH_SAE_CONFIRM_M4;
				else
					eapol_status = AUTH_SAE_CONFIRM_M3;
			}
		}
	}
	if (eapol_status) {
		wl_ext_update_eapol_status(dhd, cur_if->ifidx, eapol_status);
		if (dump_msg_level & DUMP_EAPOL_VAL) {
			if (tx) {
				WL_MSG(dev->name, "WPA3 SAE M%d [TX] : (%pM) -> (%pM)\n",
					eapol_status-AUTH_SAE_COMMIT_M1+1, mgmt->sa, mgmt->da);
			} else {
				WL_MSG(dev->name, "WPA3 SAE M%d [RX] : (%pM) <- (%pM)\n",
					eapol_status-AUTH_SAE_COMMIT_M1+1, mgmt->da, mgmt->sa);
			}
		}
	} else {
		WL_ERR(("Unknown auth_alg=%d or auth_seq=%d\n", auth_alg, auth_seq));
	}

	return;
}
#endif /* WL_CFG80211 */

#ifdef WL_WIRELESS_EXT
int
wl_ext_in4way_sync_wext(struct net_device *dev, uint action,
	enum wl_ext_status status, void *context)
{
	int ret = 0;
#ifndef WL_CFG80211
	dhd_pub_t *dhd = dhd_get_pub(dev);
	struct wl_apsta_params *apsta_params;
	struct wl_if_info *cur_if = NULL;
	int i;

	if (!dhd)
		return 0;

	apsta_params = dhd->iapsta_params;

	mutex_lock(&apsta_params->in4way_sync);
	cur_if = wl_get_cur_if(dev);
	if (cur_if && cur_if->ifmode == ISTA_MODE) {
		if (status == WL_EXT_STATUS_DISCONNECTING) {
			wl_ext_add_remove_pm_enable_work(dev, FALSE);
		} else if (status == WL_EXT_STATUS_CONNECTING) {
			wl_ext_add_remove_pm_enable_work(dev, TRUE);
		}
		ret = wl_ext_in4way_sync_sta(dhd, cur_if, 0, status, NULL);
	}
	mutex_unlock(&apsta_params->in4way_sync);
#endif
	return ret;
}
#endif /* WL_WIRELESS_EXT */

#ifdef TPUT_MONITOR
static void
wl_tput_monitor_timer(unsigned long data)
{
	struct wl_apsta_params *apsta_params = (struct wl_apsta_params *)data;
	wl_event_msg_t msg;

	if (!apsta_params) {
		IAPSTA_ERROR("wlan", "apsta_params is not ready\n");
		return;
	}

	bzero(&msg, sizeof(wl_event_msg_t));
	IAPSTA_TRACE("wlan", "timer expired\n");

	msg.ifidx = 0;
	msg.event_type = hton32(WLC_E_RESERVED);
	msg.reason = hton32(ISAM_RC_TPUT_MONITOR);
	wl_ext_event_send(apsta_params->dhd->event_params, &msg, NULL);
}

static void
wl_tput_monitor_set_timer(struct wl_apsta_params *apsta_params, uint timeout)
{
	IAPSTA_TRACE("wlan", "timeout=%d\n", timeout);

	if (timer_pending(&apsta_params->monitor_timer))
		del_timer_sync(&apsta_params->monitor_timer);

	if (timeout) {
		if (timer_pending(&apsta_params->monitor_timer))
			del_timer_sync(&apsta_params->monitor_timer);
		mod_timer(&apsta_params->monitor_timer, jiffies + msecs_to_jiffies(timeout));
	}
}

static void
wl_tput_dump(struct wl_apsta_params *apsta_params, struct wl_if_info *cur_if)
{
	struct dhd_pub *dhd = apsta_params->dhd;
	void *buf = NULL;
	sta_info_v4_t *sta = NULL;
	struct ether_addr bssid;
	wl_rssi_ant_t *rssi_ant_p;
	char rssi_buf[16];
	scb_val_t scb_val;
	int ret, bytes_written = 0, i;
	s32 rate = 0;

	if (apsta_params->tput_ts.tv_sec == 0 && apsta_params->tput_ts.tv_nsec == 0) {
		osl_do_gettimeofday(&apsta_params->tput_ts);
		cur_if->last_tx = dhd->dstats.tx_bytes;
		cur_if->last_rx = dhd->dstats.rx_bytes;
	} else {
		struct osl_timespec cur_ts;
		uint32 diff_ms;
		int32 tx_tput = 0, rx_tput = 0;

		buf = kmalloc(WLC_IOCTL_MEDLEN, GFP_KERNEL);
		if (buf == NULL) {
			IAPSTA_ERROR(cur_if->dev->name, "MALLOC failed\n");
			goto error;
		}

		wldev_ioctl(cur_if->dev, WLC_GET_BSSID, &bssid, sizeof(bssid), 0);
		ret = wldev_iovar_getbuf(cur_if->dev, "phy_rssi_ant", NULL, 0,
			buf, WLC_IOCTL_MEDLEN, NULL);
		rssi_ant_p = (wl_rssi_ant_t *)buf;
		rssi_ant_p->version = dtoh32(rssi_ant_p->version);
		rssi_ant_p->count = dtoh32(rssi_ant_p->count);
		if (ret < 0 || rssi_ant_p->count == 0) {
			wldev_ioctl(cur_if->dev, WLC_GET_RSSI, &scb_val, sizeof(scb_val_t), 0);
			rssi_ant_p->count = 1;
			rssi_ant_p->rssi_ant[0] = dtoh32(scb_val.val);
		}
		for (i=0; i<rssi_ant_p->count && rssi_ant_p->rssi_ant[i]; i++) {
			bytes_written += snprintf(rssi_buf+bytes_written, sizeof(rssi_buf),
				"[%d]", rssi_ant_p->rssi_ant[i]);
		}
		wldev_ioctl_get(cur_if->dev, WLC_GET_RATE, &rate, sizeof(rate));
		rate = dtoh32(rate);
		ret = wldev_iovar_getbuf(cur_if->dev, "sta_info", (const void*)&bssid,
				ETHER_ADDR_LEN, buf, WLC_IOCTL_MEDLEN, NULL);
		if (ret == 0) {
			sta = (sta_info_v4_t *)buf;
		}

		osl_do_gettimeofday(&cur_ts);
		diff_ms = osl_do_gettimediff(&cur_ts, &apsta_params->tput_ts)/1000;
		tx_tput = (int32)(((dhd->dstats.tx_bytes-cur_if->last_tx)/1024/1024)*8)*1000/diff_ms;
		rx_tput = (int32)(((dhd->dstats.rx_bytes-cur_if->last_rx)/1024/1024)*8)*1000/diff_ms;
		cur_if->last_tx = dhd->dstats.tx_bytes;
		cur_if->last_rx = dhd->dstats.rx_bytes;
		memcpy(&apsta_params->tput_ts, &cur_ts, sizeof(struct osl_timespec));

		if (sta == NULL || (sta->ver != WL_STA_VER_4 && sta->ver != WL_STA_VER_5)) {
			WL_MSG(cur_if->dev->name, "rssi:%s, tx=%3d Mbps(rate:%d%s), rx=%3d Mbps\n",
				rssi_buf, tx_tput, rate/2, (rate & 1) ? ".5" : "", rx_tput);
		} else {
			WL_MSG(cur_if->dev->name, "rssi:%s, tx=%3d Mbps(rate:%4d%s), rx=%3d Mbps(rate:%4d.%d)\n",
				rssi_buf, tx_tput,
				rate/2, (rate & 1) ? ".5" : "",
				rx_tput, dtoh32(sta->rx_rate)/1000, ((dtoh32(sta->rx_rate)/100)%10));
		}
	}

error:
	if (buf) {
		kfree(buf);
	}
}

static void
wl_tput_monitor_handler(struct wl_apsta_params *apsta_params,
	struct wl_if_info *cur_if, const wl_event_msg_t *e, void *data)
{
	struct dhd_pub *dhd = apsta_params->dhd;
	struct wl_if_info *tmp_if;
	uint32 etype = ntoh32(e->event_type);
	uint32 reason = ntoh32(e->reason);
	uint16 flags =  ntoh16(e->flags);
	uint timeout = dhd->conf->tput_monitor_ms;
	int i;

	if (etype == WLC_E_RESERVED && reason == ISAM_RC_TPUT_MONITOR) {
		for (i=0; i<MAX_IF_NUM; i++) {
			tmp_if = &apsta_params->if_info[i];
			if (tmp_if->dev && tmp_if->ifmode == ISTA_MODE) {
				if (wl_ext_get_chan(apsta_params, tmp_if->dev)) {
					wl_tput_dump(apsta_params, tmp_if);
					wl_tput_monitor_set_timer(apsta_params, timeout);
				}
			}
		}
	}
	else if (etype == WLC_E_LINK) {
		if (cur_if->ifmode == ISTA_MODE) {
			if (flags & WLC_EVENT_MSG_LINK) {
				wl_tput_monitor_set_timer(apsta_params, timeout);
			} else if (!wl_ext_if_enabled(apsta_params, ISTA_MODE)) {
				wl_tput_monitor_set_timer(apsta_params, 0);
			}
		}
	}
}

static void
wl_tput_monitor_detach(dhd_pub_t *dhd, struct wl_apsta_params *apsta_params)
{
	del_timer_sync(&apsta_params->monitor_timer);
}

static void
wl_tput_monitor_attach(dhd_pub_t *dhd, struct wl_apsta_params *apsta_params)
{
	init_timer_compat(&apsta_params->monitor_timer, wl_tput_monitor_timer, apsta_params);
}
#endif /* TPUT_MONITOR */

void
wl_ext_iapsta_event(struct net_device *dev, void *argu,
	const wl_event_msg_t *e, void *data)
{
	struct wl_apsta_params *apsta_params = (struct wl_apsta_params *)argu;
	struct wl_if_info *cur_if = NULL;
#if defined(WLMESH) && defined(WL_ESCAN)
	struct wl_if_info *tmp_if = NULL;
	struct wl_if_info *mesh_if = NULL;
	int i;
#endif /* WLMESH && WL_ESCAN */
	uint32 event_type = ntoh32(e->event_type);
	uint32 status =  ntoh32(e->status);
	uint32 reason =  ntoh32(e->reason);
	uint16 flags =  ntoh16(e->flags);

	cur_if = wl_get_cur_if(dev);

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
		IAPSTA_DBG(dev->name, "ifidx %d is not ready\n", e->ifidx);
		return;
	}

	if (cur_if->ifmode == ISTA_MODE || cur_if->ifmode == IGC_MODE) {
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
#ifdef PROPTX_MAXCOUNT
			wl_ext_update_wlfc_maxcount(apsta_params->dhd);
#endif /* PROPTX_MAXCOUNT */
		}
		else if (event_type == WLC_E_SET_SSID && status != WLC_E_STATUS_SUCCESS) {
			WL_MSG(cur_if->ifname,
				"connect failed event=%d, reason=%d, status=%d\n",
				event_type, reason, status);
			wl_clr_isam_status(cur_if, STA_CONNECTING);
			wake_up_interruptible(&apsta_params->netif_change_event);
#if defined(WLMESH) && defined(WL_ESCAN)
			if (mesh_if && apsta_params->macs)
				wl_mesh_clear_mesh_info(apsta_params, mesh_if, TRUE);
#endif /* WLMESH && WL_ESCAN */
#ifdef PROPTX_MAXCOUNT
			wl_ext_update_wlfc_maxcount(apsta_params->dhd);
#endif /* PROPTX_MAXCOUNT */
		}
		else if (event_type == WLC_E_DEAUTH || event_type == WLC_E_DEAUTH_IND ||
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
#ifdef PROPTX_MAXCOUNT
			wl_ext_update_wlfc_maxcount(apsta_params->dhd);
#endif /* PROPTX_MAXCOUNT */
		}
		else if ((event_type == WLC_E_LINK && reason == WLC_E_LINK_BSSCFG_DIS) ||
				(event_type == WLC_E_LINK && status == WLC_E_STATUS_SUCCESS &&
				reason == WLC_E_REASON_DEAUTH)) {
			wl_clr_isam_status(cur_if, AP_CREATED);
			WL_MSG(cur_if->ifname, "[%c] Link down, reason=%d\n",
				cur_if->prefix, reason);
#ifdef PROPTX_MAXCOUNT
			wl_ext_update_wlfc_maxcount(apsta_params->dhd);
#endif /* PROPTX_MAXCOUNT */
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
			WL_MSG_RLMT(cur_if->ifname, &e->addr, ETHER_ADDR_LEN,
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

#ifdef TPUT_MONITOR
	if (apsta_params->dhd->conf->tput_monitor_ms)
		wl_tput_monitor_handler(apsta_params, cur_if, e, data);
#endif /* TPUT_MONITOR */

	return;
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
					IAPSTA_ERROR(cur_if->dev->name, "bgnmode [b|g|bg|bgn|bgnac]\n");
					return -1;
				}
			} else if (!strcmp(row->name, " hidden ")) {
				if (!strcmp(pick_tmp, "n"))
					cur_if->hidden = 0;
				else if (!strcmp(pick_tmp, "y"))
					cur_if->hidden = 1;
				else {
					IAPSTA_ERROR(cur_if->dev->name, "hidden [y|n]\n");
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
					IAPSTA_ERROR(cur_if->dev->name, "amode [open|shared|wpapsk|wpa2psk|wpawpa2psk]\n");
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
					IAPSTA_ERROR(cur_if->dev->name, "emode [none|wep|tkip|aes|tkipaes]\n");
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
			IAPSTA_INFO(dev->name, "set rsdb_mode %d\n", rsdb_mode_cfg.config);
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
		IAPSTA_ERROR(dev->name, "wrong ifname=%s or dev not ready\n", ifname);
		return -1;
	}

	mutex_lock(&apsta_params->usr_sync);
	WL_MSG(ifname, "[%c] Disabling...\n", cur_if->prefix);

	if (cur_if->ifmode == ISTA_MODE) {
		wl_ext_ioctl(cur_if->dev, WLC_DISASSOC, NULL, 0, 1);
		wl_ext_add_remove_pm_enable_work(dev, FALSE);
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
	else if (apstamode == IDUALAP_MODE || apstamode == ISTAAPAP_MODE) {
		bss_setbuf.cfg = 0xffffffff;
		bss_setbuf.val = htod32(0);
		wl_ext_iovar_setbuf(cur_if->dev, "bss", &bss_setbuf, sizeof(bss_setbuf),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
#ifdef WLMESH
	} else if (apstamode == ISTAMESH_MODE || apstamode == IMESHAP_MODE ||
			apstamode == ISTAAPMESH_MODE || apstamode == IMESHAPAP_MODE) {
		bss_setbuf.cfg = 0xffffffff;
		bss_setbuf.val = htod32(0);
		wl_ext_iovar_setbuf(cur_if->dev, "bss", &bss_setbuf, sizeof(bss_setbuf),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		if (cur_if->ifmode == IMESH_MODE) {
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
	}

	wl_clr_isam_status(cur_if, AP_CREATED);

	WL_MSG(ifname, "[%c] Exit\n", cur_if->prefix);
	mutex_unlock(&apsta_params->usr_sync);
	return 0;
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
		IAPSTA_ERROR(dev->name, "wrong ifname=%s or dev not ready\n", ifname);
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
			IAPSTA_ERROR(ifname, "invalid channel\n");
			ret = -1;
			goto exit;
		}
	}

	wl_ext_move_cur_channel(apsta_params, cur_if);

	if (wl_ext_master_if(cur_if) && !cur_if->channel) {
		IAPSTA_ERROR(ifname, "skip channel 0\n");
		ret = -1;
		goto exit;
	}

	cur_chan = wl_ext_get_chan(apsta_params, cur_if->dev);
	if (cur_chan) {
		IAPSTA_INFO(cur_if->ifname, "Associated\n");
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
			IAPSTA_ERROR(cur_if->ifname, "wrong ifmode %d\n", cur_if->ifmode);
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
			IAPSTA_ERROR(cur_if->ifname, "wrong ifmode %d\n", cur_if->ifmode);
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

int
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
				IAPSTA_ERROR(dev->name, "Failed to allocate buffer of %d bytes\n",
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
					wl_ext_get_sec(tmp_if->dev, tmp_if->ifmode, sec, sizeof(sec), FALSE);
					dump_written += snprintf(dump_buf+dump_written, dump_len,
						"\n" DHD_LOG_PREFIX "[%s-%c]: bssid=%pM, chan=%3d(0x%x %sMHz), "
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
						"\n" DHD_LOG_PREFIX "[%s-%c]:", tmp_if->ifname, tmp_if->prefix);
				}
			}
		}
		IAPSTA_INFO(dev->name, "%s\n", dump_buf);
	}

	if (!command && dump_buf)
		kfree(dump_buf);
	return dump_written;
}

int
wl_ext_isam_param(struct net_device *dev, char *command, int total_len)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	int ret = -1;
	char *pick_tmp, *data, *param;
	int bytes_written=-1;

	IAPSTA_TRACE(dev->name, "command=%s, len=%d\n", command, total_len);

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

int
wl_ext_iapsta_disable(struct net_device *dev, char *command, int total_len)
{
	int ret = 0;
	char *pch, *pick_tmp, *param;
	char ifname[IFNAMSIZ+1];

	IAPSTA_TRACE(dev->name, "command=%s, len=%d\n", command, total_len);

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
				IAPSTA_ERROR(dev->name, "ifname [wlanX]\n");
				return -1;
			}
		}
		param = bcmstrtok(&pick_tmp, " ", 0);
	}

	return ret;
}

int
wl_ext_iapsta_enable(struct net_device *dev, char *command, int total_len)
{
	int ret = 0;
	char *pch, *pick_tmp, *param;
	char ifname[IFNAMSIZ+1];

	IAPSTA_TRACE(dev->name, "command=%s, len=%d\n", command, total_len);

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
				IAPSTA_ERROR(dev->name, "ifname [wlanX]\n");
				return -1;
			}
		}
		param = bcmstrtok(&pick_tmp, " ", 0);
	}

	return ret;
}

int
wl_ext_iapsta_config(struct net_device *dev, char *command, int total_len)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	int ret=0, i;
	char *pch, *pch2, *pick_tmp, *pick_next=NULL, *param;
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	char ifname[IFNAMSIZ+1];
	struct wl_if_info *cur_if = NULL, *tmp_if = NULL;

	if (!apsta_params->init) {
		IAPSTA_ERROR(dev->name, "please init first\n");
		return -1;
	}

	IAPSTA_TRACE(dev->name, "command=%s, len=%d\n", command, total_len);

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
				IAPSTA_ERROR(dev->name, "ifname [wlanX]\n");
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
				IAPSTA_ERROR(dev->name, "wrong ifname=%s in apstamode=%d\n",
					ifname, apsta_params->apstamode);
				ret = -1;
				break;
			}
			ret = wl_ext_parse_config(cur_if, pick_tmp, &pick_next);
			if (ret)
				break;
			pick_tmp = pick_next;
		} else {
			IAPSTA_ERROR(dev->name, "first arg must be ifname\n");
			ret = -1;
			break;
		}

	}

	mutex_unlock(&apsta_params->usr_sync);

	return ret;
}

int
wl_ext_isam_init(struct net_device *dev, char *command, int total_len)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	char *pch, *pick_tmp, *pick_tmp2, *param;
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	int i;

	if (apsta_params->init) {
		IAPSTA_ERROR(dev->name, "don't init twice\n");
		return -1;
	}
	IAPSTA_TRACE(dev->name, "command=%s, len=%d\n", command, total_len);

	pick_tmp = command;
	param = bcmstrtok(&pick_tmp, " ", 0); // skip iapsta_init
	param = bcmstrtok(&pick_tmp, " ", 0);
	while (param != NULL) {
		pick_tmp2 = bcmstrtok(&pick_tmp, " ", 0);
		if (!pick_tmp2) {
			IAPSTA_ERROR(dev->name, "wrong param %s\n", param);
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
				IAPSTA_ERROR(dev->name, "mode [sta|ap|sta-ap|ap-ap]\n");
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
						IAPSTA_ERROR(dev->name, "wrong fw type\n");
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
				IAPSTA_ERROR(dev->name, "vsdb [y|n]\n");
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
		IAPSTA_ERROR(dev->name, "mode [sta|ap|sta-ap|ap-ap]\n");
		return -1;
	}

	wl_ext_iapsta_preinit(dev, apsta_params);
#ifndef WL_STATIC_IF
	wl_ext_iapsta_intf_add(dev, apsta_params);
#endif /* WL_STATIC_IF */

	return 0;
}

int
wl_ext_iapsta_alive_preinit(struct net_device *dev)
{
	struct dhd_pub *dhd = dhd_get_pub(dev);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;

	if (apsta_params->init == TRUE) {
		IAPSTA_ERROR(dev->name, "don't init twice\n");
		return -1;
	}

	IAPSTA_TRACE(dev->name, "Enter\n");

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
		}
		else if (cur_if->ifmode == IAP_MODE) {
			cur_if->channel = 1;
			cur_if->maxassoc = -1;
			wl_set_isam_status(cur_if, IF_READY);
			cur_if->prio = PRIO_AP;
			cur_if->prefix = 'A';
			snprintf(cur_if->ssid, DOT11_MAX_SSID_LEN, "ttt_ap");
		}
#ifdef WLMESH
		else if (cur_if->ifmode == IMESH_MODE) {
			cur_if->channel = 1;
			cur_if->maxassoc = -1;
			wl_set_isam_status(cur_if, IF_READY);
			cur_if->prio = PRIO_MESH;
			cur_if->prefix = 'M';
			snprintf(cur_if->ssid, DOT11_MAX_SSID_LEN, "ttt_mesh");
		}
#endif /* WLMESH */
	}

	return op_mode;
}

static int
wl_ext_iapsta_get_rsdb(struct net_device *net, struct dhd_pub *dhd)
{
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	wl_config_t *rsdb_p;
	int ret = 0, rsdb = 0;

	if (dhd->conf->chip == BCM4359_CHIP_ID || dhd->conf->chip == BCM4375_CHIP_ID) {
		ret = wldev_iovar_getbuf(net, "rsdb_mode", NULL, 0,
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		if (!ret) {
			if (dhd->conf->fw_type == FW_TYPE_MESH) {
				rsdb = 1;
			} else {
				rsdb_p = (wl_config_t *) iovar_buf;
				if (dhd->conf->chip == BCM4375_CHIP_ID)
					rsdb = rsdb_p->status;
				else
					rsdb = rsdb_p->config;
				IAPSTA_INFO(net->name, "config=%d, status=%d\n",
					rsdb_p->config, rsdb_p->status);
			}
		}
	}

	IAPSTA_INFO(net->name, "rsdb_mode=%d\n", rsdb);

	return rsdb;
}

static void
wl_ext_iapsta_postinit(struct net_device *net, struct wl_if_info *cur_if)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	int pm;

	IAPSTA_TRACE(cur_if->ifname, "ifidx=%d\n", cur_if->ifidx);
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
#ifdef PROPTX_MAXCOUNT
	wl_ext_update_wlfc_maxcount(dhd);
#endif /* PROPTX_MAXCOUNT */

}

int
wl_ext_iapsta_attach_name(struct net_device *net, int ifidx)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	struct wl_apsta_params *apsta_params = dhd->iapsta_params;
	struct wl_if_info *cur_if = NULL;

	IAPSTA_TRACE(net->name, "ifidx=%d\n", ifidx);
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

	IAPSTA_TRACE(net->name, "ifidx=%d\n", ifidx);
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
		if (apsta_params->apstamode != IUNKNOWN_MODE &&
				apsta_params->apstamode != ISTAAPAP_MODE &&
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

	IAPSTA_TRACE(net->name, "ifidx=%d, bssidx=%d\n", ifidx, bssidx);
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
		init_waitqueue_head(&apsta_params->ap_recon_sta_event);
		mutex_init(&apsta_params->usr_sync);
		mutex_init(&apsta_params->in4way_sync);
		mutex_init(&cur_if->pm_sync);
#ifdef TPUT_MONITOR
		wl_tput_monitor_attach(dhd, apsta_params);
#endif /* TPUT_MONITOR */
		INIT_DELAYED_WORK(&cur_if->pm_enable_work, wl_ext_pm_work_handler);
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
		mutex_init(&cur_if->pm_sync);
		INIT_DELAYED_WORK(&cur_if->pm_enable_work, wl_ext_pm_work_handler);
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

	IAPSTA_TRACE(net->name, "ifidx=%d\n", ifidx);
	if (ifidx < MAX_IF_NUM) {
		cur_if = &apsta_params->if_info[ifidx];
	}

	if (ifidx == 0) {
		wl_ext_add_remove_pm_enable_work(net, FALSE);
		wl_ext_event_deregister(net, dhd, WLC_E_LAST, wl_ext_iapsta_event);
#if defined(WLMESH) && defined(WL_ESCAN)
		if (cur_if->ifmode == IMESH_MODE && apsta_params->macs) {
			wl_mesh_escan_detach(dhd, cur_if);
		}
#endif /* WLMESH && WL_ESCAN */
#ifdef TPUT_MONITOR
		wl_tput_monitor_detach(dhd, apsta_params);
#endif /* TPUT_MONITOR */
		memset(apsta_params, 0, sizeof(struct wl_apsta_params));
	} else if (cur_if && (wl_get_isam_status(cur_if, IF_READY) ||
			wl_get_isam_status(cur_if, IF_ADDING))) {
		wl_ext_add_remove_pm_enable_work(net, FALSE);
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
		IAPSTA_ERROR("wlan", "Could not allocate apsta_params\n");
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

