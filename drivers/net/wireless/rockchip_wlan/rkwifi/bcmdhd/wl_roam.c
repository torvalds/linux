/*
 * Linux roam cache
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

#include <typedefs.h>
#include <osl.h>
#include <bcmwifi_channels.h>
#include <wlioctl.h>
#include <bcmutils.h>
#ifdef WL_CFG80211
#include <wl_cfg80211.h>
#endif
#include <wldev_common.h>
#if defined(__linux__)
#include <bcmstdlib_s.h>
#endif /* defined(__linux__) */

#ifdef ESCAN_CHANNEL_CACHE
#define MAX_ROAM_CACHE		200
#define MAX_SSID_BUFSIZE	36

typedef struct {
	chanspec_t chanspec;
	int ssid_len;
	char ssid[MAX_SSID_BUFSIZE];
} roam_channel_cache;

static int n_roam_cache = 0;
static int roam_band = WLC_BAND_AUTO;
static roam_channel_cache roam_cache[MAX_ROAM_CACHE];
static uint band_bw;

static void add_roamcache_channel(wl_roam_channel_list_t *channels, chanspec_t ch)
{
	int i;

	if (channels->n >= MAX_ROAM_CHANNEL) /* buffer full */
		return;

	for (i = 0; i < channels->n; i++) {
		if (channels->channels[i] == ch) /* already in the list */
			return;
	}

	channels->channels[i] = ch;
	channels->n++;

	WL_DBG((" RCC: %02d 0x%04X\n",
		ch & WL_CHANSPEC_CHAN_MASK, ch));
}

void update_roam_cache(struct bcm_cfg80211 *cfg, int ioctl_ver)
{
	int error, i, prev_channels;
	wl_roam_channel_list_t channel_list;
	char iobuf[WLC_IOCTL_SMLEN];
	struct net_device *dev = bcmcfg_to_prmry_ndev(cfg);
	wlc_ssid_t ssid;

	if (!cfg->rcc_enabled) {
		return;
	}

#ifdef WES_SUPPORT
	if (cfg->roamscan_mode == ROAMSCAN_MODE_WES) {
		/* no update when ROAMSCAN_MODE_WES */
		return;
	}
#endif /* WES_SUPPORT */

	if (!wl_get_drv_status(cfg, CONNECTED, dev)) {
		WL_DBG(("Not associated\n"));
		return;
	}

	/* need to read out the current cache list
	   as the firmware may change dynamically
	*/
	error = wldev_iovar_getbuf(dev, "roamscan_channels", 0, 0,
		(void *)&channel_list, sizeof(channel_list), NULL);
	if (error) {
		WL_ERR(("Failed to get roamscan channels, error = %d\n", error));
		return;
	}

	error = wldev_get_ssid(dev, &ssid);
	if (error) {
		WL_ERR(("Failed to get SSID, err=%d\n", error));
		return;
	}

	prev_channels = channel_list.n;
	for (i = 0; i < n_roam_cache; i++) {
		chanspec_t ch = roam_cache[i].chanspec;
		bool band_match = ((roam_band == WLC_BAND_AUTO) ||
#ifdef WL_6G_BAND
			((roam_band == WLC_BAND_6G) && (CHSPEC_IS6G(ch))) ||
#endif /* WL_6G_BAND */
			((roam_band == WLC_BAND_2G) && (CHSPEC_IS2G(ch))) ||
			((roam_band == WLC_BAND_5G) && (CHSPEC_IS5G(ch))));

		if ((roam_cache[i].ssid_len == ssid.SSID_len) &&
			band_match && (memcmp(roam_cache[i].ssid, ssid.SSID, ssid.SSID_len) == 0)) {
			/* match found, add it */
			ch = wf_chspec_ctlchan(ch) | CHSPEC_BAND(ch) | band_bw;
			add_roamcache_channel(&channel_list, ch);
		}
	}
	if (prev_channels != channel_list.n) {
		/* channel list updated */
		error = wldev_iovar_setbuf(dev, "roamscan_channels", &channel_list,
			sizeof(channel_list), iobuf, sizeof(iobuf), NULL);
		if (error) {
			WL_ERR(("Failed to update roamscan channels, error = %d\n", error));
		}
	}

	WL_DBG(("%d AP, %d cache item(s), err=%d\n", n_roam_cache, channel_list.n, error));
}

void set_roam_band(int band)
{
	roam_band = band;
}

void reset_roam_cache(struct bcm_cfg80211 *cfg)
{
	if (!cfg->rcc_enabled) {
		return;
	}

#ifdef WES_SUPPORT
	if (cfg->roamscan_mode == ROAMSCAN_MODE_WES)
		return;
#endif /* WES_SUPPORT */

	n_roam_cache = 0;
}

static void
add_roam_cache_list(uint8 *SSID, uint32 SSID_len, chanspec_t chanspec)
{
	int i;
	uint8 channel;
	char chanbuf[CHANSPEC_STR_LEN];

	if (n_roam_cache >= MAX_ROAM_CACHE) {
		return;
	}

	for (i = 0; i < n_roam_cache; i++) {
		if ((roam_cache[i].ssid_len == SSID_len) &&
			(roam_cache[i].chanspec == chanspec) &&
			(memcmp(roam_cache[i].ssid, SSID, SSID_len) == 0)) {
			/* identical one found, just return */
			return;
		}
	}

	roam_cache[n_roam_cache].ssid_len = SSID_len;
	channel = wf_chspec_ctlchan(chanspec);
	WL_DBG(("CHSPEC  = %s, CTL %d SSID %s\n",
		wf_chspec_ntoa_ex(chanspec, chanbuf), channel, SSID));
	roam_cache[n_roam_cache].chanspec = CHSPEC_BAND(chanspec) | band_bw | channel;
	(void)memcpy_s(roam_cache[n_roam_cache].ssid, SSID_len, SSID, SSID_len);

	n_roam_cache++;
}

void
add_roam_cache(struct bcm_cfg80211 *cfg, wl_bss_info_t *bi)
{
	if (!cfg->rcc_enabled) {
		return;
	}

#ifdef WES_SUPPORT
	if (cfg->roamscan_mode == ROAMSCAN_MODE_WES) {
		return;
	}
#endif /* WES_SUPPORT */

	add_roam_cache_list(bi->SSID, bi->SSID_len, bi->chanspec);
}

static bool is_duplicated_channel(const chanspec_t *channels, int n_channels, chanspec_t new)
{
	int i;

	for (i = 0; i < n_channels; i++) {
		if (channels[i] == new)
			return TRUE;
	}

	return FALSE;
}

int get_roam_channel_list(struct bcm_cfg80211 *cfg, chanspec_t target_chan,
	chanspec_t *channels, int n_channels, const wlc_ssid_t *ssid, int ioctl_ver)
{
	int i, n = 0;
	char chanbuf[CHANSPEC_STR_LEN];

	/* first index is filled with the given target channel */
	if ((target_chan != INVCHANSPEC) && (target_chan != 0)) {
		channels[0] = target_chan;
		n++;
	}

	WL_DBG((" %s: 0x%04X\n", __FUNCTION__, channels[0]));

#ifdef WES_SUPPORT
	if (cfg->roamscan_mode == ROAMSCAN_MODE_WES) {
		for (i = 0; i < n_roam_cache; i++) {
			chanspec_t ch = roam_cache[i].chanspec;
			bool band_match = ((roam_band == WLC_BAND_AUTO) ||
#ifdef WL_6G_BAND
				((roam_band == WLC_BAND_6G) && (CHSPEC_IS6G(ch))) ||
#endif /* WL_6G_BAND */
				((roam_band == WLC_BAND_2G) && (CHSPEC_IS2G(ch))) ||
				((roam_band == WLC_BAND_5G) && (CHSPEC_IS5G(ch))));

			ch = wf_chspec_ctlchan(ch) | CHSPEC_BAND(ch) | band_bw;

			if (band_match && !is_duplicated_channel(channels, n, ch)) {
				WL_DBG(("%s: Chanspec = %s\n", __FUNCTION__,
					wf_chspec_ntoa_ex(ch, chanbuf)));
				channels[n++] = ch;
				if (n >= n_channels) {
					WL_ERR(("Too many roam scan channels\n"));
					return n;
				}
			}
		}

		return n;
	}
#endif /* WES_SUPPORT */

	for (i = 0; i < n_roam_cache; i++) {
		chanspec_t ch = roam_cache[i].chanspec;
		bool band_match = ((roam_band == WLC_BAND_AUTO) ||
#ifdef WL_6G_BAND
			((roam_band == WLC_BAND_6G) && (CHSPEC_IS6G(ch))) ||
#endif /* WL_6G_BAND */
			((roam_band == WLC_BAND_2G) && (CHSPEC_IS2G(ch))) ||
			((roam_band == WLC_BAND_5G) && (CHSPEC_IS5G(ch))));

		ch = wf_chspec_ctlchan(ch) | CHSPEC_BAND(ch) | band_bw;
		if ((roam_cache[i].ssid_len == ssid->SSID_len) &&
			band_match && !is_duplicated_channel(channels, n, ch) &&
			(memcmp(roam_cache[i].ssid, ssid->SSID, ssid->SSID_len) == 0)) {
			/* match found, add it */
			WL_DBG(("%s: Chanspec = %s\n", __FUNCTION__,
				wf_chspec_ntoa_ex(ch, chanbuf)));
			channels[n++] = ch;
			if (n >= n_channels) {
				WL_ERR(("Too many roam scan channels\n"));
				return n;
			}
		}
	}

	return n;
}

#ifdef WES_SUPPORT
int get_roamscan_mode(struct net_device *dev, int *mode)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	*mode = cfg->roamscan_mode;

	return 0;
}

int set_roamscan_mode(struct net_device *dev, int mode)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	int error = 0;
	cfg->roamscan_mode = mode;
	n_roam_cache = 0;

	error = wldev_iovar_setint(dev, "roamscan_mode", mode);
	if (error) {
		WL_ERR(("Failed to set roamscan mode to %d, error = %d\n", mode, error));
	}

	return error;
}

int
get_roamscan_chanspec_list(struct net_device *dev, chanspec_t *chanspecs)
{
	int i = 0;
	int error = BCME_OK;
	wl_roam_channel_list_t channel_list;

	/* Get Current RCC List */
	error = wldev_iovar_getbuf(dev, "roamscan_channels", 0, 0,
		(void *)&channel_list, sizeof(channel_list), NULL);
	if (error) {
		WL_ERR(("Failed to get roamscan channels, err = %d\n", error));
		return error;
	}
	if (channel_list.n > MAX_ROAM_CHANNEL) {
		WL_ERR(("Invalid roamscan channels count(%d)\n", channel_list.n));
		return BCME_ERROR;
	}

	for (i = 0; i < channel_list.n; i++) {
		chanspecs[i] = channel_list.channels[i];
		WL_DBG(("%02d: chanspec %04x\n", i, chanspecs[i]));
	}

	return i;
}

int
set_roamscan_chanspec_list(struct net_device *dev, uint nchan, chanspec_t *chanspecs)
{
	int i;
	int error;
	wl_roam_channel_list_t channel_list;
	char iobuf[WLC_IOCTL_SMLEN];
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	cfg->roamscan_mode = ROAMSCAN_MODE_WES;

	if (nchan > MAX_ROAM_CHANNEL) {
		nchan = MAX_ROAM_CHANNEL;
	}

	for (i = 0; i < nchan; i++) {
		roam_cache[i].chanspec = chanspecs[i];
		channel_list.channels[i] = chanspecs[i];

		WL_DBG(("%02d/%d: chan: 0x%04x\n", i, nchan, chanspecs[i]));
	}

	n_roam_cache = nchan;
	channel_list.n = nchan;

	/* need to set ROAMSCAN_MODE_NORMAL to update roamscan_channels,
	 * otherwise, it won't be updated
	 */
	error = wldev_iovar_setint(dev, "roamscan_mode", ROAMSCAN_MODE_NORMAL);
	if (error) {
		WL_ERR(("Failed to set roamscan mode to %d, error = %d\n",
			ROAMSCAN_MODE_NORMAL, error));
		return error;
	}
	error = wldev_iovar_setbuf(dev, "roamscan_channels", &channel_list,
		sizeof(channel_list), iobuf, sizeof(iobuf), NULL);
	if (error) {
		WL_ERR(("Failed to set roamscan channels, error = %d\n", error));
		return error;
	}
	error = wldev_iovar_setint(dev, "roamscan_mode", ROAMSCAN_MODE_WES);
	if (error) {
		WL_ERR(("Failed to set roamscan mode to %d, error = %d\n",
			ROAMSCAN_MODE_WES, error));
	}

	return error;
}

int
add_roamscan_chanspec_list(struct net_device *dev, uint nchan, chanspec_t *chanspecs)
{
	int i, error = BCME_OK;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	wlc_ssid_t ssid;

	if (!cfg->rcc_enabled) {
		return BCME_ERROR;
	}

	if (cfg->roamscan_mode == ROAMSCAN_MODE_WES) {
		WL_ERR(("Failed to add roamscan channels, WES mode %d\n",
			cfg->roamscan_mode));
		return BCME_ERROR;
	}

	if (nchan > MAX_ROAM_CHANNEL) {
		WL_ERR(("Failed Over MAX channel list(%d)\n", nchan));
		return BCME_BADARG;
	}

	error = wldev_get_ssid(dev, &ssid);
	if (error) {
		WL_ERR(("Failed to get SSID, err=%d\n", error));
		return error;
	}

	WL_DBG(("Add Roam scan channel count %d\n", nchan));

	for (i = 0; i < nchan; i++) {
		if (chanspecs[i] == 0) {
			continue;
		}
		add_roam_cache_list(ssid.SSID, ssid.SSID_len, chanspecs[i]);
		WL_DBG(("channel[%d] - 0x%04x SSID %s\n", i, chanspecs[i], ssid.SSID));
	}

	update_roam_cache(cfg, ioctl_version);

	return error;
}
#endif /* WES_SUPPORT */

#ifdef ROAM_CHANNEL_CACHE
int init_roam_cache(struct bcm_cfg80211 *cfg, int ioctl_ver)
{
	int err;
	struct net_device *dev = bcmcfg_to_prmry_ndev(cfg);
	s32 mode;

	/* Check support in firmware */
	err = wldev_iovar_getint(dev, "roamscan_mode", &mode);
	if (err && (err == BCME_UNSUPPORTED)) {
		/* If firmware doesn't support, return error. Else proceed */
		WL_ERR(("roamscan_mode iovar failed. %d\n", err));
		return err;
	}

#ifdef D11AC_IOTYPES
	band_bw = WL_CHANSPEC_BW_20;
#else
	band_bw = WL_CHANSPEC_BW_20 | WL_CHANSPEC_CTL_SB_NONE;
#endif /* D11AC_IOTYPES */

	n_roam_cache = 0;
	roam_band = WLC_BAND_AUTO;
	cfg->roamscan_mode = ROAMSCAN_MODE_NORMAL;

	return 0;
}

void print_roam_cache(struct bcm_cfg80211 *cfg)
{
	int i;

	if (!cfg->rcc_enabled) {
		return;
	}

	WL_DBG((" %d cache\n", n_roam_cache));

	for (i = 0; i < n_roam_cache; i++) {
		roam_cache[i].ssid[roam_cache[i].ssid_len] = 0;
		WL_DBG(("0x%02X %02d %s\n", roam_cache[i].chanspec,
			roam_cache[i].ssid_len, roam_cache[i].ssid));
	}
}

void wl_update_roamscan_cache_by_band(struct net_device *dev, int band)
{
	int i, error, roamscan_mode;
	wl_roam_channel_list_t chanlist_before, chanlist_after;
	char iobuf[WLC_IOCTL_SMLEN];

	roam_band = band;

	error = wldev_iovar_getint(dev, "roamscan_mode", &roamscan_mode);
	if (error) {
		WL_ERR(("Failed to get roamscan mode, error = %d\n", error));
		return;
	}

	/* in case of WES mode, update channel list by band based on the cache in DHD */
	if (roamscan_mode) {
		int n = 0;
		chanlist_before.n = n_roam_cache;

		for (n = 0; n < n_roam_cache; n++) {
			chanspec_t ch = roam_cache[n].chanspec;
			chanlist_before.channels[n] = wf_chspec_ctlchan(ch) |
				CHSPEC_BAND(ch) | band_bw;
		}
	} else {
		if (band == WLC_BAND_AUTO) {
			return;
		}
		error = wldev_iovar_getbuf(dev, "roamscan_channels", 0, 0,
				(void *)&chanlist_before, sizeof(wl_roam_channel_list_t), NULL);
		if (error) {
			WL_ERR(("Failed to get roamscan channels, error = %d\n", error));
			return;
		}
	}
	chanlist_after.n = 0;
	/* filtering by the given band */
	for (i = 0; i < chanlist_before.n; i++) {
		chanspec_t chspec = chanlist_before.channels[i];
		bool band_match = ((band == WLC_BAND_AUTO) ||
#ifdef WL_6G_BAND
				((band == WLC_BAND_6G) && (CHSPEC_IS6G(chspec))) ||
#endif /* WL_6G_BAND */
				((band == WLC_BAND_2G) && (CHSPEC_IS2G(chspec))) ||
				((band == WLC_BAND_5G) && (CHSPEC_IS5G(chspec))));
		if (band_match) {
			chanlist_after.channels[chanlist_after.n++] = chspec;
		}
	}

	if (roamscan_mode) {
		/* need to set ROAMSCAN_MODE_NORMAL to update roamscan_channels,
		 * otherwise, it won't be updated
		 */
		wldev_iovar_setint(dev, "roamscan_mode", ROAMSCAN_MODE_NORMAL);

		error = wldev_iovar_setbuf(dev, "roamscan_channels", &chanlist_after,
				sizeof(wl_roam_channel_list_t), iobuf, sizeof(iobuf), NULL);
		if (error) {
			WL_ERR(("Failed to update roamscan channels, error = %d\n", error));
		}
		wldev_iovar_setint(dev, "roamscan_mode", ROAMSCAN_MODE_WES);
	} else {
		if (chanlist_before.n == chanlist_after.n) {
			return;
		}
		error = wldev_iovar_setbuf(dev, "roamscan_channels", &chanlist_after,
				sizeof(wl_roam_channel_list_t), iobuf, sizeof(iobuf), NULL);
		if (error) {
			WL_ERR(("Failed to update roamscan channels, error = %d\n", error));
		}
	}
}
#endif /* ROAM_CHANNEL_CACHE */
#endif /* ESCAN_CHANNEL_CACHE */
