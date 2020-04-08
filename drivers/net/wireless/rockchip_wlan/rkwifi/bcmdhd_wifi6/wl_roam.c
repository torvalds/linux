/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linux roam cache
 *
 * Copyright (C) 1999-2019, Broadcom.
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
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: wl_roam.c 798173 2019-01-07 09:23:21Z $
 */

#include <typedefs.h>
#include <osl.h>
#include <bcmwifi_channels.h>
#include <wlioctl.h>
#include <bcmutils.h>
#ifdef WL_CFG80211
#include <wl_cfg80211.h>
#endif // endif
#include <wldev_common.h>
#include <bcmstdlib_s.h>

#ifdef ESCAN_CHANNEL_CACHE
#define MAX_ROAM_CACHE		200
#define MAX_SSID_BUFSIZE	36

#define ROAMSCAN_MODE_NORMAL	0
#define ROAMSCAN_MODE_WES		1

typedef struct {
	chanspec_t chanspec;
	int ssid_len;
	char ssid[MAX_SSID_BUFSIZE];
} roam_channel_cache;

static int n_roam_cache = 0;
static int roam_band = WLC_BAND_AUTO;
static roam_channel_cache roam_cache[MAX_ROAM_CACHE];
static uint band2G, band5G, band_bw;

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
	if (ioctl_ver == 1) {
		/* legacy chanspec */
		band2G = WL_LCHANSPEC_BAND_2G;
		band5G = WL_LCHANSPEC_BAND_5G;
		band_bw = WL_LCHANSPEC_BW_20 | WL_LCHANSPEC_CTL_SB_NONE;
	} else {
		band2G = WL_CHANSPEC_BAND_2G;
		band5G = WL_CHANSPEC_BAND_5G;
		band_bw = WL_CHANSPEC_BW_20;
	}
#else
	band2G = WL_CHANSPEC_BAND_2G;
	band5G = WL_CHANSPEC_BAND_5G;
	band_bw = WL_CHANSPEC_BW_20 | WL_CHANSPEC_CTL_SB_NONE;
#endif /* D11AC_IOTYPES */

	n_roam_cache = 0;
	roam_band = WLC_BAND_AUTO;

	return 0;
}
#endif /* ROAM_CHANNEL_CACHE */

#ifdef ESCAN_CHANNEL_CACHE
void set_roam_band(int band)
{
	roam_band = band;
}

void reset_roam_cache(struct bcm_cfg80211 *cfg)
{
	if (!cfg->rcc_enabled) {
		return;
	}

	n_roam_cache = 0;
}

void add_roam_cache(struct bcm_cfg80211 *cfg, wl_bss_info_t *bi)
{
	int i;
	uint8 channel;
	char chanbuf[CHANSPEC_STR_LEN];

	if (!cfg->rcc_enabled) {
		return;
	}

	if (n_roam_cache >= MAX_ROAM_CACHE)
		return;

	for (i = 0; i < n_roam_cache; i++) {
		if ((roam_cache[i].ssid_len == bi->SSID_len) &&
			(roam_cache[i].chanspec == bi->chanspec) &&
			(memcmp(roam_cache[i].ssid, bi->SSID, bi->SSID_len) == 0)) {
			/* identical one found, just return */
			return;
		}
	}

	roam_cache[n_roam_cache].ssid_len = bi->SSID_len;
	channel = wf_chspec_ctlchan(bi->chanspec);
	WL_DBG(("CHSPEC  = %s, CTL %d\n", wf_chspec_ntoa_ex(bi->chanspec, chanbuf), channel));
	roam_cache[n_roam_cache].chanspec =
		(channel <= CH_MAX_2G_CHANNEL ? band2G : band5G) | band_bw | channel;
	(void)memcpy_s(roam_cache[n_roam_cache].ssid, bi->SSID_len, bi->SSID, bi->SSID_len);

	n_roam_cache++;
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

int get_roam_channel_list(int target_chan,
	chanspec_t *channels, int n_channels, const wlc_ssid_t *ssid, int ioctl_ver)
{
	int i, n = 1;
	char chanbuf[CHANSPEC_STR_LEN];

	/* first index is filled with the given target channel */
	if (target_chan) {
		channels[0] = (target_chan & WL_CHANSPEC_CHAN_MASK) |
			(target_chan <= CH_MAX_2G_CHANNEL ? band2G : band5G) | band_bw;
	} else {
		/* If target channel is not provided, set the index to 0 */
		n = 0;
	}

	WL_DBG((" %s: %03d 0x%04X\n", __FUNCTION__, target_chan, channels[0]));

	for (i = 0; i < n_roam_cache; i++) {
		chanspec_t ch = roam_cache[i].chanspec;
		bool is_2G = ioctl_ver == 1 ? LCHSPEC_IS2G(ch) : CHSPEC_IS2G(ch);
		bool is_5G = ioctl_ver == 1 ? LCHSPEC_IS5G(ch) : CHSPEC_IS5G(ch);
		bool band_match = ((roam_band == WLC_BAND_AUTO) ||
			((roam_band == WLC_BAND_2G) && is_2G) ||
			((roam_band == WLC_BAND_5G) && is_5G));

		ch = CHSPEC_CHANNEL(ch) | (is_2G ? band2G : band5G) | band_bw;
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
#endif /* ESCAN_CHANNEL_CACHE */

#ifdef ROAM_CHANNEL_CACHE
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
		bool is_2G = ioctl_ver == 1 ? LCHSPEC_IS2G(ch) : CHSPEC_IS2G(ch);
		bool is_5G = ioctl_ver == 1 ? LCHSPEC_IS5G(ch) : CHSPEC_IS5G(ch);
		bool band_match = ((roam_band == WLC_BAND_AUTO) ||
			((roam_band == WLC_BAND_2G) && is_2G) ||
			((roam_band == WLC_BAND_5G) && is_5G));

		if ((roam_cache[i].ssid_len == ssid.SSID_len) &&
			band_match && (memcmp(roam_cache[i].ssid, ssid.SSID, ssid.SSID_len) == 0)) {
			/* match found, add it */
			ch = CHSPEC_CHANNEL(ch) | (is_2G ? band2G : band5G) | band_bw;
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

void wl_update_roamscan_cache_by_band(struct net_device *dev, int band)
{
	int i, error, ioctl_ver, wes_mode;
	wl_roam_channel_list_t chanlist_before, chanlist_after;
	char iobuf[WLC_IOCTL_SMLEN];

	roam_band = band;

	error = wldev_iovar_getint(dev, "roamscan_mode", &wes_mode);
	if (error) {
		WL_ERR(("Failed to get roamscan mode, error = %d\n", error));
		return;
	}

	ioctl_ver = wl_cfg80211_get_ioctl_version();
	/* in case of WES mode, update channel list by band based on the cache in DHD */
	if (wes_mode) {
		int n = 0;
		chanlist_before.n = n_roam_cache;

		for (n = 0; n < n_roam_cache; n++) {
			chanspec_t ch = roam_cache[n].chanspec;
			bool is_2G = ioctl_ver == 1 ? LCHSPEC_IS2G(ch) : CHSPEC_IS2G(ch);
			chanlist_before.channels[n] = CHSPEC_CHANNEL(ch) |
				(is_2G ? band2G : band5G) | band_bw;
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
		bool is_2G = ioctl_ver == 1 ? LCHSPEC_IS2G(chspec) : CHSPEC_IS2G(chspec);
		bool is_5G = ioctl_ver == 1 ? LCHSPEC_IS5G(chspec) : CHSPEC_IS5G(chspec);
		bool band_match = ((band == WLC_BAND_AUTO) ||
				((band == WLC_BAND_2G) && is_2G) ||
				((band == WLC_BAND_5G) && is_5G));
		if (band_match) {
			chanlist_after.channels[chanlist_after.n++] = chspec;
		}
	}

	if (wes_mode) {
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
