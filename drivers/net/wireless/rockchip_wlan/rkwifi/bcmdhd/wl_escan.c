/* SPDX-License-Identifier: GPL-2.0 */
#if defined(WL_ESCAN)
#include <bcmendian.h>
#include <linux/if_arp.h>
#include <asm/uaccess.h>
#include <wl_android.h>
#include <wl_escan.h>
#include <dhd_config.h>

#define ESCAN_ERROR(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_ERROR_LEVEL) { \
			printf("[%s] ESCAN-ERROR) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)
#define ESCAN_TRACE(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_TRACE_LEVEL) { \
			printf("[%s] ESCAN-TRACE) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)
#define ESCAN_SCAN(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_SCAN_LEVEL) { \
			printf("[%s] ESCAN-SCAN) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)
#define ESCAN_DBG(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_DBG_LEVEL) { \
			printf("[%s] ESCAN-DBG) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)

/* IOCTL swapping mode for Big Endian host with Little Endian dongle.  Default to off */
#define htod32(i) (i)
#define htod16(i) (i)
#define dtoh32(i) (i)
#define dtoh16(i) (i)
#define htodchanspec(i) (i)
#define dtohchanspec(i) (i)
#define WL_EXTRA_BUF_MAX 2048

#define wl_escan_get_buf(a) ((wl_scan_results_t *) (a)->escan_buf)

#if defined(WL_WIRELESS_EXT)
extern int wl_iw_handle_scanresults_ies(char **event_p, char *end,
	struct iw_request_info *info, wl_bss_info_t *bi);
#define for_each_bss_wext(list, bss, __i)	\
	for (__i = 0; __i < list->count && __i < IW_MAX_AP; __i++, bss = next_bss(list, bss))
#endif
#define for_each_bss(list, bss, __i)	\
	for (__i = 0; __i < list->count; __i++, bss = next_bss(list, bss))

#define wl_escan_set_sync_id(a) ((a) = htod16(0x1234))

#ifdef ESCAN_BUF_OVERFLOW_MGMT
#define BUF_OVERFLOW_MGMT_COUNT 3
typedef struct {
	int RSSI;
	int length;
	struct ether_addr BSSID;
} removal_element_t;
#endif /* ESCAN_BUF_OVERFLOW_MGMT */

/* Return a new chanspec given a legacy chanspec
 * Returns INVCHANSPEC on error
 */
static chanspec_t
wl_chspec_from_legacy(chanspec_t legacy_chspec)
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
		ESCAN_ERROR("wlan", "wl_chspec_from_legacy: output chanspec (0x%04X) malformed\n",
			chspec);
		return INVCHANSPEC;
	}

	return chspec;
}

/* Return a legacy chanspec given a new chanspec
 * Returns INVCHANSPEC on error
 */
static chanspec_t
wl_chspec_to_legacy(chanspec_t chspec)
{
	chanspec_t lchspec;

	if (wf_chspec_malformed(chspec)) {
		ESCAN_ERROR("wlan", "wl_chspec_to_legacy: input chanspec (0x%04X) malformed\n",
			chspec);
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
		ESCAN_ERROR("wlan", "wl_chspec_to_legacy: unable to convert chanspec %s "
			"(0x%04X) to pre-11ac format\n",
			wf_chspec_ntoa(chspec, chanbuf), chspec);
		return INVCHANSPEC;
	}

	return lchspec;
}

/* given a chanspec value from the driver, do the endian and chanspec version conversion to
 * a chanspec_t value
 * Returns INVCHANSPEC on error
 */
static chanspec_t
wl_chspec_driver_to_host(int ioctl_ver, chanspec_t chanspec)
{
	chanspec = dtohchanspec(chanspec);
	if (ioctl_ver == 1) {
		chanspec = wl_chspec_from_legacy(chanspec);
	}

	return chanspec;
}

/* given a chanspec value, do the endian and chanspec version conversion to
 * a chanspec_t value
 * Returns INVCHANSPEC on error
 */
static chanspec_t
wl_chspec_host_to_driver(int ioctl_ver, chanspec_t chanspec)
{
	if (ioctl_ver == 1) {
		chanspec = wl_chspec_to_legacy(chanspec);
		if (chanspec == INVCHANSPEC) {
			return chanspec;
		}
	}
	chanspec = htodchanspec(chanspec);

	return chanspec;
}

/* given a channel value, do the endian and chanspec version conversion to
 * a chanspec_t value
 * Returns INVCHANSPEC on error
 */
static chanspec_t
wl_ch_host_to_driver(int ioctl_ver, u16 channel)
{
	chanspec_t chanspec;

	chanspec = channel & WL_CHANSPEC_CHAN_MASK;

	if (channel <= CH_MAX_2G_CHANNEL)
		chanspec |= WL_CHANSPEC_BAND_2G;
	else
		chanspec |= WL_CHANSPEC_BAND_5G;

	chanspec |= WL_CHANSPEC_BW_20;

	chanspec |= WL_CHANSPEC_CTL_SB_NONE;

	return wl_chspec_host_to_driver(ioctl_ver, chanspec);
}

static inline struct wl_bss_info *next_bss(wl_scan_results_t *list,
	struct wl_bss_info *bss)
{
	return bss = bss ?
		(struct wl_bss_info *)((uintptr) bss + dtoh32(bss->length)) : list->bss_info;
}

#if defined(ESCAN_RESULT_PATCH)
#ifndef BSSCACHE
static void
wl_escan_dump_bss(struct net_device *dev, struct wl_escan_info *escan,
	wl_bss_info_t *bi)
{
	int16 rssi;
	int channel;
	chanspec_t chanspec;

#if defined(RSSIAVG)
	rssi = wl_get_avg_rssi(&escan->g_rssi_cache_ctrl, &bi->BSSID);
	if (rssi == RSSI_MINVAL)
		rssi = MIN(dtoh16(bi->RSSI), RSSI_MAXVAL);
#else
	// terence 20150419: limit the max. rssi to -2 or the bss will be filtered out in android OS
	rssi = MIN(dtoh16(bi->RSSI), RSSI_MAXVAL);
#endif
	chanspec = wl_chspec_driver_to_host(escan->ioctl_ver, bi->chanspec);
	channel = wf_chspec_ctlchan(chanspec);
	ESCAN_SCAN(dev->name, "BSSID %pM, channel %3d(%3d %sMHz), rssi %3d, SSID \"%s\"\n",
		&bi->BSSID, channel, CHSPEC_CHANNEL(chanspec),
		CHSPEC_IS20(chanspec)?"20":
		CHSPEC_IS40(chanspec)?"40":
		CHSPEC_IS80(chanspec)?"80":"160",
		rssi, bi->SSID);
}
#endif /* BSSCACHE */

static s32
wl_escan_inform_bss(struct net_device *dev, struct wl_escan_info *escan)
{
	wl_scan_results_t *bss_list;
#ifndef BSSCACHE
	wl_bss_info_t *bi = NULL;	/* must be initialized */
	s32 i;
#endif
	s32 err = 0;
#if defined(RSSIAVG)
	int rssi;
#endif

	bss_list = escan->bss_list;

	ESCAN_SCAN(dev->name, "scanned AP count (%d)\n", bss_list->count);

	/* Update cache */
#if defined(RSSIAVG)
	wl_update_rssi_cache(&escan->g_rssi_cache_ctrl, bss_list);
	if (!in_atomic())
		wl_update_connected_rssi_cache(dev, &escan->g_rssi_cache_ctrl, &rssi);
#endif
#if defined(BSSCACHE)
	wl_update_bss_cache(&escan->g_bss_cache_ctrl,
#if defined(RSSIAVG)
		&escan->g_rssi_cache_ctrl,
#endif
		bss_list);
#endif

	/* delete dirty cache */
#if defined(RSSIAVG)
	wl_delete_dirty_rssi_cache(&escan->g_rssi_cache_ctrl);
	wl_reset_rssi_cache(&escan->g_rssi_cache_ctrl);
#endif

#if defined(BSSCACHE)
	wl_delete_dirty_bss_cache(&escan->g_bss_cache_ctrl);
	wl_reset_bss_cache(&escan->g_bss_cache_ctrl);
	if (escan->autochannel)
		wl_ext_get_best_channel(dev, &escan->g_bss_cache_ctrl,
			escan->ioctl_ver, &escan->best_2g_ch, &escan->best_5g_ch, &escan->best_6g_ch);
#else
	bi = next_bss(bss_list, bi);
	for_each_bss(bss_list, bi, i) {
		wl_escan_dump_bss(dev, escan, bi);
	}
	if (escan->autochannel)
		wl_ext_get_best_channel(dev, bss_list, escan->ioctl_ver,
			&escan->best_2g_ch, &escan->best_5g_ch, &escan->best_6g_ch);
#endif

	return err;
}
#endif /* ESCAN_RESULT_PATCH */

static wl_scan_params_t *
wl_escan_alloc_params(struct net_device *dev, struct wl_escan_info *escan,
	int channel, int nprobes, int *out_params_size)
{
	wl_scan_params_t *params;
	int params_size;
	int num_chans;

	*out_params_size = 0;

	/* Our scan params only need space for 1 channel and 0 ssids */
	params_size = WL_SCAN_PARAMS_FIXED_SIZE + 1 * sizeof(uint16);
	params = (wl_scan_params_t*) kzalloc(params_size, GFP_KERNEL);
	if (params == NULL) {
		ESCAN_ERROR(dev->name, "mem alloc failed (%d bytes)\n", params_size);
		return params;
	}
	memset(params, 0, params_size);
	params->nprobes = nprobes;

	num_chans = (channel == 0) ? 0 : 1;

	memcpy(&params->bssid, &ether_bcast, ETHER_ADDR_LEN);
	params->bss_type = DOT11_BSSTYPE_ANY;
	params->scan_type = DOT11_SCANTYPE_ACTIVE;
	params->nprobes = htod32(1);
	params->active_time = htod32(-1);
	params->passive_time = htod32(-1);
	params->home_time = htod32(10);
	if (channel == -1)
		params->channel_list[0] = htodchanspec(channel);
	else
		params->channel_list[0] = wl_ch_host_to_driver(escan->ioctl_ver, channel);

	/* Our scan params have 1 channel and 0 ssids */
	params->channel_num = htod32((0 << WL_SCAN_PARAMS_NSSID_SHIFT) |
		(num_chans & WL_SCAN_PARAMS_COUNT_MASK));

	*out_params_size = params_size;	/* rtn size to the caller */
	return params;
}

static void
wl_escan_abort(struct net_device *dev, struct wl_escan_info *escan)
{
	wl_scan_params_t *params = NULL;
	s32 params_size = 0;
	s32 err = BCME_OK;
	if (!in_atomic()) {
		/* Our scan params only need space for 1 channel and 0 ssids */
		params = wl_escan_alloc_params(dev, escan, -1, 0, &params_size);
		if (params == NULL) {
			ESCAN_ERROR(dev->name, "scan params allocation failed \n");
			err = -ENOMEM;
		} else {
			/* Do a scan abort to stop the driver's scan engine */
			err = wldev_ioctl(dev, WLC_SCAN, params, params_size, true);
			if (err < 0) {
				ESCAN_ERROR(dev->name, "scan abort  failed \n");
			}
			kfree(params);
		}
	}
}

static s32
wl_escan_notify_complete(struct net_device *dev,
	struct wl_escan_info *escan, bool fw_abort)
{
	s32 err = BCME_OK;
#if defined(WL_WIRELESS_EXT)
	int cmd = 0;
#if WIRELESS_EXT > 13
	union iwreq_data wrqu;
	char extra[IW_CUSTOM_MAX + 1];
#endif
#endif
	struct dhd_pub *dhd = dhd_get_pub(dev);

	ESCAN_TRACE(dev->name, "Enter\n");

	if (fw_abort && !in_atomic())
		wl_escan_abort(dev, escan);

	if (timer_pending(&escan->scan_timeout))
		del_timer_sync(&escan->scan_timeout);

#if defined(ESCAN_RESULT_PATCH)
	escan->bss_list = wl_escan_get_buf(escan);
	wl_escan_inform_bss(dev, escan);
#endif /* ESCAN_RESULT_PATCH */

	escan->escan_state = ESCAN_STATE_IDLE;
	wake_up_interruptible(&dhd->conf->event_complete);

#if defined(WL_WIRELESS_EXT)
#if WIRELESS_EXT > 13
#if WIRELESS_EXT > 14
	cmd = SIOCGIWSCAN;
#endif
	// terence 20150224: fix "wlan0: (WE) : Wireless Event too big (65306)"
	memset(&wrqu, 0, sizeof(wrqu));
	memset(extra, 0, sizeof(extra));
	if (cmd) {
		if (cmd == SIOCGIWSCAN) {
			wireless_send_event(dev, cmd, &wrqu, NULL);
		} else
			wireless_send_event(dev, cmd, &wrqu, extra);
	}
#endif
#endif

	return err;
}

#ifdef ESCAN_BUF_OVERFLOW_MGMT
static void
wl_escan_find_removal_candidate(struct wl_escan_info *escan,
	wl_bss_info_t *bss, removal_element_t *candidate)
{
	int idx;
	for (idx = 0; idx < BUF_OVERFLOW_MGMT_COUNT; idx++) {
		int len = BUF_OVERFLOW_MGMT_COUNT - idx - 1;
		if (bss->RSSI < candidate[idx].RSSI) {
			if (len)
				memcpy(&candidate[idx + 1], &candidate[idx],
					sizeof(removal_element_t) * len);
			candidate[idx].RSSI = bss->RSSI;
			candidate[idx].length = bss->length;
			memcpy(&candidate[idx].BSSID, &bss->BSSID, ETHER_ADDR_LEN);
			return;
		}
	}
}

static void
wl_escan_remove_lowRSSI_info(struct net_device *dev, struct wl_escan_info *escan,
	wl_scan_results_t *list, removal_element_t *candidate, wl_bss_info_t *bi)
{
	int idx1, idx2;
	int total_delete_len = 0;
	for (idx1 = 0; idx1 < BUF_OVERFLOW_MGMT_COUNT; idx1++) {
		int cur_len = WL_SCAN_RESULTS_FIXED_SIZE;
		wl_bss_info_t *bss = NULL;
		if (candidate[idx1].RSSI >= bi->RSSI)
			continue;
		for (idx2 = 0; idx2 < list->count; idx2++) {
			bss = bss ? (wl_bss_info_t *)((uintptr)bss + dtoh32(bss->length)) :
				list->bss_info;
			if (!bcmp(&candidate[idx1].BSSID, &bss->BSSID, ETHER_ADDR_LEN) &&
				candidate[idx1].RSSI == bss->RSSI &&
				candidate[idx1].length == dtoh32(bss->length)) {
				u32 delete_len = dtoh32(bss->length);
				ESCAN_DBG(dev->name,
					"delete scan info of %pM to add new AP\n", &bss->BSSID);
				if (idx2 < list->count -1) {
					memmove((u8 *)bss, (u8 *)bss + delete_len,
						list->buflen - cur_len - delete_len);
				}
				list->buflen -= delete_len;
				list->count--;
				total_delete_len += delete_len;
				/* if delete_len is greater than or equal to result length */
				if (total_delete_len >= bi->length) {
					return;
				}
				break;
			}
			cur_len += dtoh32(bss->length);
		}
	}
}
#endif /* ESCAN_BUF_OVERFLOW_MGMT */

void
wl_escan_ext_handler(struct net_device *dev, void *argu,
	const wl_event_msg_t *e, void *data)
{
	struct wl_escan_info *escan = (struct wl_escan_info *)argu;
	s32 status = ntoh32(e->status);
	wl_bss_info_t *bi;
	wl_escan_result_t *escan_result;
	wl_bss_info_t *bss = NULL;
	wl_scan_results_t *list;
	u32 bi_length;
	u32 i;
	u16 channel;

	mutex_lock(&escan->usr_sync);
	escan_result = (wl_escan_result_t *)data;

	if (escan->escan_state != ESCAN_STATE_SCANING) {
		ESCAN_DBG(dev->name, "Not my scan\n");
		goto exit;
	}

	ESCAN_DBG(dev->name, "enter event type : %d, status : %d \n",
		ntoh32(e->event_type), ntoh32(e->status));

	if (status == WLC_E_STATUS_PARTIAL) {
		ESCAN_DBG(dev->name, "WLC_E_STATUS_PARTIAL \n");
		if (!escan_result) {
			ESCAN_ERROR(dev->name, "Invalid escan result (NULL pointer)\n");
			goto exit;
		}
		if (dtoh16(escan_result->bss_count) != 1) {
			ESCAN_ERROR(dev->name, "Invalid bss_count %d: ignoring\n",
				escan_result->bss_count);
			goto exit;
		}
		bi = escan_result->bss_info;
		if (!bi) {
			ESCAN_ERROR(dev->name, "Invalid escan bss info (NULL pointer)\n");
			goto exit;
		}
		bi_length = dtoh32(bi->length);
		if (bi_length != (dtoh32(escan_result->buflen) - WL_ESCAN_RESULTS_FIXED_SIZE)) {
			ESCAN_ERROR(dev->name, "Invalid bss_info length %d: ignoring\n",
				bi_length);
			goto exit;
		}

		/* +++++ terence 20130524: skip invalid bss */
		channel =
			bi->ctl_ch ? bi->ctl_ch :
			CHSPEC_CHANNEL(wl_chspec_driver_to_host(escan->ioctl_ver, bi->chanspec));
		if (!dhd_conf_match_channel(escan->pub, channel))
			goto exit;
		/* ----- terence 20130524: skip invalid bss */

		{
			int cur_len = WL_SCAN_RESULTS_FIXED_SIZE;
#ifdef ESCAN_BUF_OVERFLOW_MGMT
			removal_element_t candidate[BUF_OVERFLOW_MGMT_COUNT];
			int remove_lower_rssi = FALSE;

			bzero(candidate, sizeof(removal_element_t)*BUF_OVERFLOW_MGMT_COUNT);
#endif /* ESCAN_BUF_OVERFLOW_MGMT */

			list = wl_escan_get_buf(escan);
#ifdef ESCAN_BUF_OVERFLOW_MGMT
			if (bi_length > ESCAN_BUF_SIZE - list->buflen)
				remove_lower_rssi = TRUE;
#endif /* ESCAN_BUF_OVERFLOW_MGMT */

			ESCAN_DBG(dev->name, "%s(%pM) RSSI %d flags 0x%x length %d\n",
				bi->SSID, &bi->BSSID, bi->RSSI, bi->flags, bi->length);
			for (i = 0; i < list->count; i++) {
				bss = bss ? (wl_bss_info_t *)((uintptr)bss + dtoh32(bss->length))
					: list->bss_info;
#ifdef ESCAN_BUF_OVERFLOW_MGMT
				ESCAN_DBG(dev->name,
					"%s(%pM), i=%d bss: RSSI %d list->count %d\n",
					bss->SSID, &bss->BSSID, i, bss->RSSI, list->count);

				if (remove_lower_rssi)
					wl_escan_find_removal_candidate(escan, bss, candidate);
#endif /* ESCAN_BUF_OVERFLOW_MGMT */
				if (!bcmp(&bi->BSSID, &bss->BSSID, ETHER_ADDR_LEN) &&
						(CHSPEC_BAND(wl_chspec_driver_to_host(escan->ioctl_ver, bi->chanspec))
						== CHSPEC_BAND(wl_chspec_driver_to_host(escan->ioctl_ver, bss->chanspec))) &&
						bi->SSID_len == bss->SSID_len &&
						!bcmp(bi->SSID, bss->SSID, bi->SSID_len)) {

					/* do not allow beacon data to update
					*the data recd from a probe response
					*/
					if (!(bss->flags & WL_BSS_FLAGS_FROM_BEACON) &&
						(bi->flags & WL_BSS_FLAGS_FROM_BEACON))
						goto exit;

					ESCAN_DBG(dev->name,
						"%s(%pM), i=%d prev: RSSI %d flags 0x%x, "
						"new: RSSI %d flags 0x%x\n",
						bss->SSID, &bi->BSSID, i, bss->RSSI, bss->flags,
						bi->RSSI, bi->flags);

					if ((bss->flags & WL_BSS_FLAGS_RSSI_ONCHANNEL) ==
						(bi->flags & WL_BSS_FLAGS_RSSI_ONCHANNEL)) {
						/* preserve max RSSI if the measurements are
						* both on-channel or both off-channel
						*/
						ESCAN_DBG(dev->name,
							"%s(%pM), same onchan, RSSI: prev %d new %d\n",
							bss->SSID, &bi->BSSID, bss->RSSI, bi->RSSI);
						bi->RSSI = MAX(bss->RSSI, bi->RSSI);
					} else if ((bss->flags & WL_BSS_FLAGS_RSSI_ONCHANNEL) &&
						(bi->flags & WL_BSS_FLAGS_RSSI_ONCHANNEL) == 0) {
						/* preserve the on-channel rssi measurement
						* if the new measurement is off channel
						*/
						ESCAN_DBG(dev->name,
							"%s(%pM), prev onchan, RSSI: prev %d new %d\n",
							bss->SSID, &bi->BSSID, bss->RSSI, bi->RSSI);
						bi->RSSI = bss->RSSI;
						bi->flags |= WL_BSS_FLAGS_RSSI_ONCHANNEL;
					}
					if (dtoh32(bss->length) != bi_length) {
						u32 prev_len = dtoh32(bss->length);

						ESCAN_DBG(dev->name,
							"bss info replacement occured(bcast:%d->probresp%d)\n",
							bss->ie_length, bi->ie_length);
						ESCAN_DBG(dev->name,
							"%s(%pM), replacement!(%d -> %d)\n",
							bss->SSID, &bi->BSSID, prev_len, bi_length);

						if (list->buflen - prev_len + bi_length	> ESCAN_BUF_SIZE) {
							ESCAN_ERROR(dev->name,
								"Buffer is too small: keep the previous result "
								"of this AP\n");
							/* Only update RSSI */
							bss->RSSI = bi->RSSI;
							bss->flags |= (bi->flags
								& WL_BSS_FLAGS_RSSI_ONCHANNEL);
							goto exit;
						}

						if (i < list->count - 1) {
							/* memory copy required by this case only */
							memmove((u8 *)bss + bi_length,
								(u8 *)bss + prev_len,
								list->buflen - cur_len - prev_len);
						}
						list->buflen -= prev_len;
						list->buflen += bi_length;
					}
					list->version = dtoh32(bi->version);
					memcpy((u8 *)bss, (u8 *)bi, bi_length);
					goto exit;
				}
				cur_len += dtoh32(bss->length);
			}
			if (bi_length > ESCAN_BUF_SIZE - list->buflen) {
#ifdef ESCAN_BUF_OVERFLOW_MGMT
				wl_escan_remove_lowRSSI_info(dev, escan, list, candidate, bi);
				if (bi_length > ESCAN_BUF_SIZE - list->buflen) {
					ESCAN_DBG(dev->name,
						"RSSI(%pM) is too low(%d) to add Buffer\n",
						&bi->BSSID, bi->RSSI);
					goto exit;
				}
#else
				ESCAN_ERROR(dev->name, "Buffer is too small: ignoring\n");
				goto exit;
#endif /* ESCAN_BUF_OVERFLOW_MGMT */
			}

			memcpy(&(((char *)list)[list->buflen]), bi, bi_length);
			list->version = dtoh32(bi->version);
			list->buflen += bi_length;
			list->count++;
		}
	}
	else if (status == WLC_E_STATUS_SUCCESS) {
		ESCAN_DBG(dev->name, "ESCAN COMPLETED\n");
		escan->bss_list = wl_escan_get_buf(escan);
		ESCAN_DBG(dev->name, "SCAN COMPLETED: scanned AP count=%d\n",
			escan->bss_list->count);
		wl_escan_notify_complete(dev, escan, false);
	} else if ((status == WLC_E_STATUS_ABORT) || (status == WLC_E_STATUS_NEWSCAN) ||
		(status == WLC_E_STATUS_11HQUIET) || (status == WLC_E_STATUS_CS_ABORT) ||
		(status == WLC_E_STATUS_NEWASSOC)) {
		/* Handle all cases of scan abort */
		ESCAN_DBG(dev->name, "ESCAN ABORT reason: %d\n", status);
		escan->bss_list = wl_escan_get_buf(escan);
		ESCAN_DBG(dev->name, "SCAN ABORT: scanned AP count=%d\n",
			escan->bss_list->count);
		wl_escan_notify_complete(dev, escan, false);
	} else if (status == WLC_E_STATUS_TIMEOUT) {
		ESCAN_ERROR(dev->name, "WLC_E_STATUS_TIMEOUT\n");
		ESCAN_ERROR(dev->name, "reason[0x%x]\n", e->reason);
		if (e->reason == 0xFFFFFFFF) {
			wl_escan_notify_complete(dev, escan, true);
		}
	} else {
		ESCAN_ERROR(dev->name, "unexpected Escan Event %d : abort\n", status);
		escan->bss_list = wl_escan_get_buf(escan);
		ESCAN_DBG(dev->name, "SCAN ABORTED(UNEXPECTED): scanned AP count=%d\n",
			escan->bss_list->count);
		wl_escan_notify_complete(dev, escan, false);
	}
exit:
	mutex_unlock(&escan->usr_sync);
	return;
}

static int
wl_escan_prep(struct net_device *dev, struct wl_escan_info *escan,
	wl_uint32_list_t *list, void *scan_params, wl_scan_info_t *scan_info)
{
	int err = 0;
	wl_scan_results_t *results;
	char *ptr;
	int i = 0, j = 0;
	wlc_ssid_t ssid_tmp;
	u32 n_channels = 0;
	chanspec_t chanspec;
	u32 n_ssids = 0;
	wl_scan_params_t *params = NULL;
	wl_scan_params_v2_t *params_v2 = NULL;
	u32 scan_param_size = 0;
	u32 channel_offset = 0;
	u32 cur_offset;
	uint16 *chan_list = NULL;

	results = wl_escan_get_buf(escan);
	results->version = 0;
	results->count = 0;
	results->buflen = WL_SCAN_RESULTS_FIXED_SIZE;
	escan->escan_state = ESCAN_STATE_SCANING;

	/* Arm scan timeout timer */
	mod_timer(&escan->scan_timeout, jiffies + msecs_to_jiffies(WL_ESCAN_TIMER_INTERVAL_MS));

	if (escan->scan_params_v2) {
		params_v2 = (wl_scan_params_v2_t *)scan_params;
		scan_param_size = sizeof(wl_scan_params_v2_t);
		channel_offset = offsetof(wl_scan_params_v2_t, channel_list);
	} else {
		params = (wl_scan_params_t *)scan_params;
		scan_param_size = sizeof(wl_scan_params_t);
		channel_offset = offsetof(wl_scan_params_t, channel_list);
	}

	if (params_v2) {
		/* scan params ver2 */
		memcpy(&params_v2->bssid, &ether_bcast, ETHER_ADDR_LEN);
		params_v2->version = htod16(WL_SCAN_PARAMS_VERSION_V2);
		params_v2->length = htod16(sizeof(wl_scan_params_v2_t));
		params_v2->bss_type = DOT11_BSSTYPE_ANY;
		params_v2->scan_type = DOT11_SCANTYPE_ACTIVE;
		params_v2->nprobes = htod32(-1);
		if (scan_info->scan_time)
			params_v2->active_time = htod32(scan_info->scan_time);
		else
			params_v2->active_time = htod32(-1);
		params_v2->passive_time = htod32(-1);
		params_v2->home_time = htod32(-1);
		params_v2->channel_num = 0;
		bzero(&params_v2->ssid, sizeof(wlc_ssid_t));
		chan_list = params_v2->channel_list;
	} else {
		/* scan params ver 1 */
		memcpy(&params->bssid, &ether_bcast, ETHER_ADDR_LEN);
		params->bss_type = DOT11_BSSTYPE_ANY;
		params->scan_type = DOT11_SCANTYPE_ACTIVE;
		params->nprobes = htod32(-1);
		if (scan_info->scan_time)
			params->active_time = htod32(scan_info->scan_time);
		else
			params->active_time = htod32(-1);
		params->passive_time = htod32(-1);
		params->home_time = htod32(-1);
		params->channel_num = 0;
		bzero(&params->ssid, sizeof(wlc_ssid_t));
		chan_list = params->channel_list;
	}

	cur_offset = channel_offset;

	n_channels = list->count;
	/* Copy channel array if applicable */
	ESCAN_SCAN(dev->name, "### List of channelspecs to scan ###\n");
	if (n_channels > 0) {
		for (i = 0; i < n_channels; i++) {
			chanspec = list->element[i];
			if (chanspec == INVCHANSPEC) {
				ESCAN_ERROR(dev->name, "Invalid chanspec! Skipping channel\n");
				continue;
			}
			chan_list[j] = chanspec;
			ESCAN_SCAN(dev->name, "Chan : %d, Channel spec: %x\n",
				CHSPEC_CHANNEL(chanspec), chanspec);
			chan_list[j] = wl_chspec_host_to_driver(escan->ioctl_ver,
				chan_list[j]);
			j++;
		}
		cur_offset += (j * (sizeof(u16)));
		n_channels = j;
	} else {
		ESCAN_SCAN(dev->name, "Scanning all channels\n");
	}

	if (scan_info->ssid.SSID_len) {
		/* Copy ssid array if applicable */
		ESCAN_SCAN(dev->name, "### List of SSIDs to scan ###\n");
		cur_offset = (u32) roundup(cur_offset, sizeof(u32));
		if (params_v2)
			ptr = (char*)params_v2 + cur_offset;
		else
			ptr = (char*)params + cur_offset;

		if (scan_info->bcast_ssid) {
			n_ssids = 2;
			ESCAN_SCAN(dev->name, "0: Broadcast scan\n");
			memset(&ssid_tmp, 0, sizeof(wlc_ssid_t));
			ssid_tmp.SSID_len = 0;
			memcpy(ptr, &ssid_tmp, sizeof(wlc_ssid_t));
			ptr += sizeof(wlc_ssid_t);
		} else {
			n_ssids = 1;
		}

		memset(&ssid_tmp, 0, sizeof(wlc_ssid_t));
		ssid_tmp.SSID_len = scan_info->ssid.SSID_len;
		memcpy(ssid_tmp.SSID, scan_info->ssid.SSID, scan_info->ssid.SSID_len);
		memcpy(ptr, &ssid_tmp, sizeof(wlc_ssid_t));
		ptr += sizeof(wlc_ssid_t);
		ESCAN_SCAN(dev->name, "1: scan for %s size=%d\n",
			ssid_tmp.SSID, ssid_tmp.SSID_len);
	}
	else {
		ESCAN_SCAN(dev->name, "Broadcast scan\n");
	}

	if (n_ssids || n_channels) {
		u32 channel_num =
				htod32((n_ssids << WL_SCAN_PARAMS_NSSID_SHIFT) |
				(n_channels & WL_SCAN_PARAMS_COUNT_MASK));
		if (params_v2) {
			params_v2->channel_num = channel_num;
		} else {
			params->channel_num = channel_num;
		}
	}

	return err;
}

static int
wl_escan_reset(struct wl_escan_info *escan)
{
	if (timer_pending(&escan->scan_timeout))
		del_timer_sync(&escan->scan_timeout);
	escan->escan_state = ESCAN_STATE_IDLE;

	return 0;
}

static void
wl_escan_timeout(unsigned long data)
{
	wl_event_msg_t msg;
	struct wl_escan_info *escan = (struct wl_escan_info *)data;
	wl_scan_results_t *bss_list;
	struct wl_bss_info *bi = NULL;
	s32 i;
	u32 channel;

	if (!escan->dev) {
		ESCAN_ERROR("wlan", "No dev present\n");
		return;
	}

	bss_list = wl_escan_get_buf(escan);
	if (!bss_list) {
		ESCAN_ERROR(escan->dev->name,
			"bss_list is null. Didn't receive any partial scan results\n");
	} else {
		ESCAN_ERROR(escan->dev->name, "scanned AP count (%d)\n", bss_list->count);
		bi = next_bss(bss_list, bi);
		for_each_bss(bss_list, bi, i) {
			channel = wf_chspec_ctlchan(wl_chspec_driver_to_host(escan->ioctl_ver,
				bi->chanspec));
			ESCAN_ERROR(escan->dev->name, "SSID :%s  Channel :%d\n", bi->SSID, channel);
		}
	}

	bzero(&msg, sizeof(wl_event_msg_t));
	ESCAN_ERROR(escan->dev->name, "timer expired\n");

	msg.ifidx = dhd_net2idx(escan->pub->info, escan->dev);
	msg.event_type = hton32(WLC_E_ESCAN_RESULT);
	msg.status = hton32(WLC_E_STATUS_TIMEOUT);
	msg.reason = 0xFFFFFFFF;
	wl_ext_event_send(escan->pub->event_params, &msg, NULL);
}

int
wl_escan_set_scan(struct net_device *dev, wl_scan_info_t *scan_info)
{
	struct dhd_pub *dhdp = dhd_get_pub(dev);
	struct wl_escan_info *escan = dhdp->escan;
	s32 err = BCME_OK;
	wl_escan_params_t *eparams = NULL;
	wl_escan_params_v2_t *eparams_v2 = NULL;
	u8 *scan_params = NULL;
	s32 params_size;
	wl_escan_params_t *params = NULL;
	u32 n_channels = 0;
	wl_uint32_list_t *list;
	u8 valid_chan_list[sizeof(u32)*(MAX_CTRL_CHANSPECS + 1)];

	mutex_lock(&escan->usr_sync);
	if (escan->escan_state == ESCAN_STATE_DOWN) {
		ESCAN_ERROR(dev->name, "STATE is down\n");
		err = -EINVAL;
		goto exit2;
	}

#if defined(WL_EXT_IAPSTA) && defined(WL_CFG80211)
	err = wl_ext_in4way_sync(dev, STA_NO_SCAN_IN4WAY, WL_EXT_STATUS_SCAN, NULL);
	if (err) {
		ESCAN_SCAN(dev->name, "scan busy %d\n", err);
		goto exit2;
	}
#endif

	if (wl_ext_check_scan(dev, dhdp)) {
		err = -EBUSY;
		goto exit2;
	}

	ESCAN_TRACE(dev->name, "Enter\n");

	if (escan->scan_params_v2) {
		params_size = (WL_SCAN_PARAMS_V2_FIXED_SIZE +
			OFFSETOF(wl_escan_params_v2_t, params));
	} else {
		params_size = (WL_SCAN_PARAMS_FIXED_SIZE +
			OFFSETOF(wl_escan_params_t, params));
	}

	/* if scan request is not empty parse scan request paramters */
	memset(valid_chan_list, 0, sizeof(valid_chan_list));
	list = (wl_uint32_list_t *)(void *) valid_chan_list;

	if (scan_info->channels.count) {
		memcpy(list, &scan_info->channels, sizeof(wl_channel_list_t));
	} else {
		err = wl_construct_ctl_chanspec_list(dev, list);
		if (err != 0) {
			ESCAN_ERROR(dev->name, "get channels failed with %d\n", err);
			goto exit;
		}
	}

	n_channels = list->count;
	/* Allocate space for populating ssids in wl_escan_params_t struct */
	if (list->count % 2)
		/* If n_channels is odd, add a padd of u16 */
		params_size += sizeof(u16) * (n_channels + 1);
	else
		params_size += sizeof(u16) * n_channels;
	if (scan_info->ssid.SSID_len) {
		params_size += sizeof(struct wlc_ssid) * 2;
	}

	params = (wl_escan_params_t *) kzalloc(params_size, GFP_KERNEL);
	if (params == NULL) {
		ESCAN_ERROR(dev->name, "kzalloc failed\n");
		err = -ENOMEM;
		goto exit;
	}

	if (escan->scan_params_v2) {
		eparams_v2 = (wl_escan_params_v2_t *)params;
		scan_params = (u8 *)&eparams_v2->params;
		eparams_v2->version = htod32(ESCAN_REQ_VERSION_V2);
		eparams_v2->action =  htod16(WL_SCAN_ACTION_START);
	} else {
		eparams = (wl_escan_params_t *)params;
		scan_params = (u8 *)&eparams->params;
		eparams->version = htod32(ESCAN_REQ_VERSION);
		eparams->action =  htod16(WL_SCAN_ACTION_START);
	}
	wl_escan_set_sync_id(params->sync_id);

	wl_escan_prep(dev, escan, list, scan_params, scan_info);

	if (params_size + sizeof("escan") >= WLC_IOCTL_MEDLEN) {
		ESCAN_ERROR(dev->name, "ioctl buffer length not sufficient\n");
		kfree(params);
		err = -ENOMEM;
		goto exit;
	}

	WL_MSG(dev->name, "LEGACY_SCAN\n");
	err = wldev_iovar_setbuf(dev, "escan", params, params_size,
		escan->escan_ioctl_buf, WLC_IOCTL_MEDLEN, NULL);
	if (unlikely(err)) {
		ESCAN_ERROR(dev->name, "escan error (%d)\n", err);
	} else {
		escan->dev = dev;
	}
	kfree(params);
exit:
	if (unlikely(err)) {
		wl_escan_reset(escan);
	}
exit2:
	mutex_unlock(&escan->usr_sync);
	return err;
}

#if defined(WL_WIRELESS_EXT)
static int
rssi_to_qual(int rssi)
{
	if (rssi <= WL_IW_RSSI_NO_SIGNAL)
		return 0;
	else if (rssi <= WL_IW_RSSI_VERY_LOW)
		return 1;
	else if (rssi <= WL_IW_RSSI_LOW)
		return 2;
	else if (rssi <= WL_IW_RSSI_GOOD)
		return 3;
	else if (rssi <= WL_IW_RSSI_VERY_GOOD)
		return 4;
	else
		return 5;
}

static int
wl_escan_merge_scan_results(struct net_device *dev, struct wl_escan_info *escan,
	struct iw_request_info *info, char *extra, wl_bss_info_t *bi, int *len, int max_size)
{
	s32 err = BCME_OK;
	struct iw_event	iwe;
	int j;
	char *event = extra, *end = extra + max_size - WE_ADD_EVENT_FIX, *value;
	int16 rssi;
	int channel;
	chanspec_t chanspec;

	/* overflow check cover fields before wpa IEs */
	if (event + ETHER_ADDR_LEN + bi->SSID_len + IW_EV_UINT_LEN + IW_EV_FREQ_LEN +
		IW_EV_QUAL_LEN >= end) {
		err = -E2BIG;
		goto exit;
	}

#if defined(RSSIAVG)
	rssi = wl_get_avg_rssi(&escan->g_rssi_cache_ctrl, &bi->BSSID);
	if (rssi == RSSI_MINVAL)
		rssi = MIN(dtoh16(bi->RSSI), RSSI_MAXVAL);
#else
	// terence 20150419: limit the max. rssi to -2 or the bss will be filtered out in android OS
	rssi = MIN(dtoh16(bi->RSSI), RSSI_MAXVAL);
#endif
	chanspec = wl_chspec_driver_to_host(escan->ioctl_ver, bi->chanspec);
	channel = wf_chspec_ctlchan(chanspec);
	ESCAN_SCAN(dev->name, "BSSID %pM, channel %3d(%3d %sMHz), rssi %3d, SSID \"%s\"\n",
		&bi->BSSID, channel, CHSPEC_CHANNEL(chanspec),
		CHSPEC_IS20(chanspec)?"20":
		CHSPEC_IS40(chanspec)?"40":
		CHSPEC_IS80(chanspec)?"80":"160",
		rssi, bi->SSID);

	/* First entry must be the BSSID */
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(iwe.u.ap_addr.sa_data, &bi->BSSID, ETHER_ADDR_LEN);
	event = IWE_STREAM_ADD_EVENT(info, event, end, &iwe, IW_EV_ADDR_LEN);

	/* SSID */
	iwe.u.data.length = dtoh32(bi->SSID_len);
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.flags = 1;
	event = IWE_STREAM_ADD_POINT(info, event, end, &iwe, bi->SSID);

	/* Mode */
	if (dtoh16(bi->capability) & (DOT11_CAP_ESS | DOT11_CAP_IBSS)) {
		iwe.cmd = SIOCGIWMODE;
		if (dtoh16(bi->capability) & DOT11_CAP_ESS)
			iwe.u.mode = IW_MODE_INFRA;
		else
			iwe.u.mode = IW_MODE_ADHOC;
		event = IWE_STREAM_ADD_EVENT(info, event, end, &iwe, IW_EV_UINT_LEN);
	}

	/* Channel */
	iwe.cmd = SIOCGIWFREQ;
#if 1
	iwe.u.freq.m = wf_channel2mhz(channel, channel <= CH_MAX_2G_CHANNEL ?
			WF_CHAN_FACTOR_2_4_G : WF_CHAN_FACTOR_5_G);
#else
	iwe.u.freq.m = wf_channel2mhz(bi->n_cap ?
			bi->ctl_ch : CHSPEC_CHANNEL(bi->chanspec),
			CHSPEC_CHANNEL(bi->chanspec) <= CH_MAX_2G_CHANNEL ?
			WF_CHAN_FACTOR_2_4_G : WF_CHAN_FACTOR_5_G);
#endif
	iwe.u.freq.e = 6;
	event = IWE_STREAM_ADD_EVENT(info, event, end, &iwe, IW_EV_FREQ_LEN);

	/* Channel quality */
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.qual = rssi_to_qual(rssi);
	iwe.u.qual.level = 0x100 + rssi;
	iwe.u.qual.noise = 0x100 + bi->phy_noise;
	event = IWE_STREAM_ADD_EVENT(info, event, end, &iwe, IW_EV_QUAL_LEN);

	wl_iw_handle_scanresults_ies(&event, end, info, bi);

	/* Encryption */
	iwe.cmd = SIOCGIWENCODE;
	if (dtoh16(bi->capability) & DOT11_CAP_PRIVACY)
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	else
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	iwe.u.data.length = 0;
	event = IWE_STREAM_ADD_POINT(info, event, end, &iwe, (char *)event);

	/* Rates */
	if (bi->rateset.count <= sizeof(bi->rateset.rates)) {
		if (event + IW_MAX_BITRATES*IW_EV_PARAM_LEN >= end) {
			err = -E2BIG;
			goto exit;
		}
		value = event + IW_EV_LCP_LEN;
		iwe.cmd = SIOCGIWRATE;
		/* Those two flags are ignored... */
		iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;
		for (j = 0; j < bi->rateset.count && j < IW_MAX_BITRATES; j++) {
			iwe.u.bitrate.value = (bi->rateset.rates[j] & 0x7f) * 500000;
			value = IWE_STREAM_ADD_VALUE(info, event, value, end, &iwe,
				IW_EV_PARAM_LEN);
		}
		event = value;
	}
	*len = event - extra;
	if (*len < 0)
		ESCAN_ERROR(dev->name, "==> Wrong size\n");

exit:
	return err;
}

int
wl_escan_merge_scan_list(struct net_device *dev, u8 *cur_bssid,
	struct iw_request_info *info, struct iw_point *dwrq, char *extra,
	int *len_ret, int *bss_cnt)
{
	struct dhd_pub *dhdp = dhd_get_pub(dev);
	struct wl_escan_info *escan = dhdp->escan;
	s32 err = BCME_OK;
	int i = 0, cnt = 0;
	int len_prep = 0;
	wl_bss_info_t *bi = NULL;
	wl_scan_results_t *bss_list;
	__u16 buflen_from_user = dwrq->length;

	bss_list = escan->bss_list;
	bi = next_bss(bss_list, bi);
	for_each_bss_wext(bss_list, bi, i)
	{
		if (!memcmp(&bi->BSSID, cur_bssid, ETHER_ADDR_LEN)) {
			ESCAN_SCAN(dev->name, "skip connected AP %pM\n", cur_bssid);
			continue;
		}
		len_prep = 0;
		err = wl_escan_merge_scan_results(dev, escan, info, extra+*len_ret, bi,
			&len_prep, buflen_from_user-*len_ret);
		*len_ret += len_prep;
		if (err)
			goto exit;
		cnt++;
	}
	*bss_cnt = cnt;

exit:
	return err;
}

#if defined(BSSCACHE)
int
wl_escan_merge_cache_list(struct net_device *dev, u8 *cur_bssid,
	struct iw_request_info *info, struct iw_point *dwrq, char *extra,
	int *len_ret, int *bss_cnt)
{
	struct dhd_pub *dhdp = dhd_get_pub(dev);
	struct wl_escan_info *escan = dhdp->escan;
	s32 err = BCME_OK;
	int i = 0, cnt = 0;
	int len_prep = 0;
	wl_bss_info_t *bi = NULL;
	wl_scan_results_t *bss_list;
	__u16 buflen_from_user = dwrq->length;
	wl_bss_cache_t *node;

	bss_list = &escan->g_bss_cache_ctrl.m_cache_head->results;
	node = escan->g_bss_cache_ctrl.m_cache_head;
	for (i=0; node && i<IW_MAX_AP; i++)
	{
		bi = node->results.bss_info;
		if (node->dirty > 1) {
			if (!memcmp(&bi->BSSID, cur_bssid, ETHER_ADDR_LEN)) {
				ESCAN_SCAN(dev->name, "skip connected AP %pM\n", cur_bssid);
				node = node->next;
				continue;
			}
			len_prep = 0;
			err = wl_escan_merge_scan_results(dev, escan, info, extra+*len_ret, bi,
				&len_prep, buflen_from_user-*len_ret);
			*len_ret += len_prep;
			if (err)
				goto exit;
			cnt++;
		}
		node = node->next;
	}
	*bss_cnt = cnt;

exit:
	return err;
}
#endif

int
wl_escan_get_scan(struct net_device *dev,
	struct iw_request_info *info, struct iw_point *dwrq, char *extra)
{
	struct dhd_pub *dhdp = dhd_get_pub(dev);
	struct wl_escan_info *escan = dhdp->escan;
	s32 err = BCME_OK;
	int scan_cnt = 0;
#if defined(BSSCACHE)
	int cache_cnt = 0;
#endif
	int len_prep = 0, len_ret = 0;
	wl_bss_info_t *bi = NULL;
	__u16 buflen_from_user = dwrq->length;
	char *buf = NULL;
	struct ether_addr cur_bssid;
	u8 ioctl_buf[WLC_IOCTL_SMLEN];

	if (!extra) {
		ESCAN_TRACE(dev->name, "extra is null\n");
		return -EINVAL;
	}

	mutex_lock(&escan->usr_sync);

	/* Check for scan in progress */
	if (escan->escan_state == ESCAN_STATE_SCANING) {
		ESCAN_DBG(dev->name, "SIOCGIWSCAN GET still scanning\n");
		err = -EAGAIN;
		goto exit;
	}
	if (!escan->bss_list) {
		ESCAN_ERROR(dev->name, "scan not ready\n");
		err = -EAGAIN;
		goto exit;
	}
	if (dev != escan->dev) {
		ESCAN_ERROR(dev->name, "not my scan from %s\n", escan->dev->name);
		err = -EINVAL;
		goto exit;
	}

	ESCAN_SCAN(dev->name, "SIOCGIWSCAN, len=%d\n", dwrq->length);

	wldev_iovar_getbuf(dev, "cur_etheraddr", NULL, 0, ioctl_buf, WLC_IOCTL_SMLEN, NULL);
	err = wldev_ioctl(dev, WLC_GET_BSSID, &cur_bssid, sizeof(cur_bssid), false);
	if (err != BCME_NOTASSOCIATED &&
			memcmp(&ether_null, &cur_bssid, ETHER_ADDR_LEN) &&
			memcmp(ioctl_buf, &cur_bssid, ETHER_ADDR_LEN)) {
		// merge current connected bss
		buf = kzalloc(WL_EXTRA_BUF_MAX, GFP_ATOMIC);
		if (!buf) {
			ESCAN_ERROR(dev->name, "buffer alloc failed.\n");
			err = BCME_NOMEM;
			goto exit;
		}
		*(u32 *)buf = htod32(WL_EXTRA_BUF_MAX);
		err = wldev_ioctl(dev, WLC_GET_BSS_INFO, buf, WL_EXTRA_BUF_MAX, false);
		if (unlikely(err)) {
			ESCAN_ERROR(dev->name, "Could not get bss info %d\n", err);
			goto exit;
		}
		bi = (struct wl_bss_info *)(buf + 4);
		len_prep = 0;
		err = wl_escan_merge_scan_results(dev, escan, info, extra+len_ret, bi,
			&len_prep, buflen_from_user-len_ret);
		len_ret += len_prep;
		if (err)
			goto exit;
		bi = NULL;
	}

	err = wl_escan_merge_scan_list(dev, (u8 *)&cur_bssid, info, dwrq, extra,
		&len_ret, &scan_cnt);
	if (err)
		goto exit;
#if defined(BSSCACHE)
	err = wl_escan_merge_cache_list(dev, (u8 *)&cur_bssid, info, dwrq, extra,
		&len_ret, &cache_cnt);
	if (err)
		goto exit;
#endif

	if ((len_ret + WE_ADD_EVENT_FIX) < dwrq->length)
		dwrq->length = len_ret;

	dwrq->flags = 0;	/* todo */
	ESCAN_SCAN(dev->name, "scanned AP count (%d)\n", scan_cnt);
#if defined(BSSCACHE)
	ESCAN_SCAN(dev->name, "cached AP count (%d)\n", cache_cnt);
#endif
exit:
	kfree(buf);
	dwrq->length = len_ret;
	mutex_unlock(&escan->usr_sync);
	return err;
}
#endif /* WL_WIRELESS_EXT */

#ifdef WLMESH
bool
wl_escan_meshid_ie(u8 *parse, u32 len, wlc_ssid_t *mesh_id)
{
	bcm_tlv_t *ie;

	if((ie = bcm_parse_tlvs(parse, (int)len, DOT11_MNG_MESH_ID)) != NULL) {
		mesh_id->SSID_len = ie->len;
		if (ie->len) {
			strncpy(mesh_id->SSID, ie->data, ie->len);
		}
		return TRUE;
	}
	return FALSE;
}

bool
wl_escan_rsn_ie(u8 *parse, u32 len)
{
	if (bcm_parse_tlvs(parse, (u32)len, DOT11_MNG_RSN_ID)) {
		return TRUE;
	}
	return FALSE;
}

bool
wl_escan_mesh_info_ie(struct net_device *dev, u8 *parse, u32 len,
	struct wl_mesh_params *mesh_info)
{
	bcm_tlv_t *ie;
	uchar mesh_oui[]={0x00, 0x22, 0xf4};
	int totl_len;
	uint8 *pie;
	uint max_len;
	bool found = FALSE;

	memset(mesh_info, 0, sizeof(struct wl_mesh_params));
	if((ie = bcm_parse_tlvs(parse, (int)len, DOT11_MNG_VS_ID)) != NULL) {
		totl_len = ie->len;
		if (!memcmp(ie->data, &mesh_oui, sizeof(mesh_oui))) {
			pie = ie->data + sizeof(mesh_oui);
			ie = (bcm_tlv_t *)pie;
			totl_len -= sizeof(mesh_oui);
			while (totl_len > 2 && ie->len) {
				if (ie->id == MESH_INFO_MASTER_BSSID && ie->len == ETHER_ADDR_LEN) {
					memcpy(&mesh_info->master_bssid, ie->data, ETHER_ADDR_LEN);
				} else if (ie->id == MESH_INFO_MASTER_CHANNEL) {
					mesh_info->master_channel = ie->data[0];
					found = TRUE;
				} else if (ie->id == MESH_INFO_HOP_CNT) {
					mesh_info->hop_cnt = ie->data[0];
				} else if (ie->id == MESH_INFO_PEER_BSSID) {
					max_len = min(MAX_HOP_LIST*ETHER_ADDR_LEN, (int)ie->len);
					memcpy(mesh_info->peer_bssid, ie->data, max_len);
				}
				totl_len -= (ie->len + 2);
				pie = ie->data + ie->len;
				ie = (bcm_tlv_t *)pie;
			}
		}
	}

	return found;
}

bool
wl_escan_mesh_info(struct net_device *dev, struct wl_escan_info *escan,
	struct ether_addr *peer_bssid, struct wl_mesh_params *mesh_info)
{
	int i = 0;
	wl_bss_info_t *bi = NULL;
	wl_scan_results_t *bss_list;
	int16 bi_rssi, bi_chan;
	wlc_ssid_t bi_meshid;
	bool is_mesh_peer = FALSE, found = FALSE;
	struct wl_mesh_params peer_mesh_info;

	mutex_lock(&escan->usr_sync);

	/* Check for scan in progress */
	if (escan->escan_state == ESCAN_STATE_SCANING) {
		ESCAN_ERROR(dev->name, "SIOCGIWSCAN GET still scanning\n");
		goto exit;
	}
	if (!escan->bss_list) {
		ESCAN_ERROR(dev->name, "scan not ready\n");
		goto exit;
	}
	if (dev != escan->dev) {
		ESCAN_ERROR(dev->name, "not my scan from %s\n", escan->dev->name);
		goto exit;
	}

	bss_list = escan->bss_list;
	bi = next_bss(bss_list, bi);
	ESCAN_SCAN(dev->name, "scanned AP/Mesh count (%d)\n", bss_list->count);
	for_each_bss(bss_list, bi, i)
	{
		memset(&bi_meshid, 0, sizeof(bi_meshid));
		is_mesh_peer = FALSE;
		bi_chan = wf_chspec_ctlchan(
			wl_chspec_driver_to_host(escan->ioctl_ver, bi->chanspec));
		bi_rssi = MIN(dtoh16(bi->RSSI), RSSI_MAXVAL);
		is_mesh_peer = wl_escan_meshid_ie(((u8*)bi)+bi->ie_offset,
			bi->ie_length, &bi_meshid);
		if (!(bi->capability & (DOT11_CAP_ESS|DOT11_CAP_IBSS)) && is_mesh_peer) {
			bool bi_sae = FALSE, bss_found = FALSE, prefer = FALSE;
			if (!memcmp(peer_bssid, &bi->BSSID, ETHER_ADDR_LEN)) {
				bi_sae = wl_escan_rsn_ie(((u8*)bi)+bi->ie_offset, bi->ie_length);
				bss_found = wl_escan_mesh_info_ie(dev, ((u8*)bi)+bi->ie_offset,
					bi->ie_length, &peer_mesh_info);
				if (bss_found) {
					memcpy(&mesh_info->master_bssid, &peer_mesh_info.master_bssid,
						ETHER_ADDR_LEN);
					mesh_info->master_channel = peer_mesh_info.master_channel;
					mesh_info->hop_cnt = peer_mesh_info.hop_cnt;
					memcpy(mesh_info->peer_bssid, peer_mesh_info.peer_bssid,
						sizeof(peer_mesh_info.peer_bssid));
					prefer = TRUE;
					found = TRUE;
				}
			}
			ESCAN_SCAN(dev->name,
				"%s[Mesh] BSSID=%pM, channel=%d, RSSI=%d, sec=%s, "
				"mbssid=%pM, mchannel=%d, hop=%d, pbssid=%pM, MeshID=\"%s\"\n",
				prefer?"*":" ", &bi->BSSID, bi_chan, bi_rssi, bi_sae?"SAE":"OPEN",
				&peer_mesh_info.master_bssid, peer_mesh_info.master_channel,
				peer_mesh_info.hop_cnt, &peer_mesh_info.peer_bssid, bi_meshid.SSID);
		}
	}

exit:
	mutex_unlock(&escan->usr_sync);
	return found;
}

bool
wl_escan_mesh_peer(struct net_device *dev, struct wl_escan_info *escan,
	wlc_ssid_t *cur_ssid, uint16 cur_chan, bool sae,
	struct wl_mesh_params *mesh_info)
{
	int i = 0;
	wl_bss_info_t *bi = NULL;
	wl_scan_results_t *bss_list;
	int16 bi_rssi, bi_chan, max_rssi = -100;
	uint min_hop_cnt = 255;
	wlc_ssid_t bi_meshid;
	bool is_mesh_peer = FALSE, chan_matched = FALSE, found = FALSE;
	struct wl_mesh_params peer_mesh_info;

	mutex_lock(&escan->usr_sync);

	/* Check for scan in progress */
	if (escan->escan_state == ESCAN_STATE_SCANING) {
		ESCAN_ERROR(dev->name, "SIOCGIWSCAN GET still scanning\n");
		goto exit;
	}
	if (!escan->bss_list) {
		ESCAN_ERROR(dev->name, "scan not ready\n");
		goto exit;
	}
	if (dev != escan->dev) {
		ESCAN_ERROR(dev->name, "not my scan from %s\n", escan->dev->name);
		goto exit;
	}

	bss_list = escan->bss_list;
	bi = next_bss(bss_list, bi);
	ESCAN_SCAN(dev->name, "scanned AP/Mesh count (%d)\n", bss_list->count);
	for_each_bss(bss_list, bi, i)
	{
		memset(&bi_meshid, 0, sizeof(bi_meshid));
		is_mesh_peer = FALSE;
		bi_chan = wf_chspec_ctlchan(
			wl_chspec_driver_to_host(escan->ioctl_ver, bi->chanspec));
		bi_rssi = MIN(dtoh16(bi->RSSI), RSSI_MAXVAL);
		is_mesh_peer = wl_escan_meshid_ie(((u8*)bi)+bi->ie_offset,
			bi->ie_length, &bi_meshid);
		if (!(bi->capability & (DOT11_CAP_ESS|DOT11_CAP_IBSS)) && is_mesh_peer) {
			bool meshid_matched = FALSE, sec_matched = FALSE, bi_sae = FALSE,
				bss_found = FALSE, prefer = FALSE;

			if (cur_ssid->SSID_len && cur_ssid->SSID_len == bi_meshid.SSID_len &&
					!memcmp(cur_ssid->SSID, bi_meshid.SSID, bi_meshid.SSID_len))
				meshid_matched = TRUE;

			bi_sae = wl_escan_rsn_ie(((u8*)bi)+bi->ie_offset, bi->ie_length);
			if (bi_sae == sae)
				sec_matched = TRUE;

			bss_found = wl_escan_mesh_info_ie(dev, ((u8*)bi)+bi->ie_offset, bi->ie_length,
				&peer_mesh_info);
			if (meshid_matched && sec_matched && bss_found &&
					(cur_chan == bi_chan)) {
				if (peer_mesh_info.hop_cnt < min_hop_cnt) {
					memcpy(&mesh_info->master_bssid, &peer_mesh_info.master_bssid,
						ETHER_ADDR_LEN);
					mesh_info->master_channel = peer_mesh_info.master_channel;
					mesh_info->hop_cnt = peer_mesh_info.hop_cnt;
					memcpy(mesh_info->peer_bssid, peer_mesh_info.peer_bssid,
						sizeof(peer_mesh_info.peer_bssid));
					min_hop_cnt = peer_mesh_info.hop_cnt;
					prefer = TRUE;
					chan_matched = TRUE;
					found = TRUE;
				}
			}
			else if (meshid_matched && sec_matched && bss_found &&
					(cur_chan != bi_chan) && !chan_matched) {
				if (bi_rssi > max_rssi) {
					memcpy(&mesh_info->master_bssid, &peer_mesh_info.master_bssid,
						ETHER_ADDR_LEN);
					mesh_info->master_channel = peer_mesh_info.master_channel;
					mesh_info->hop_cnt = peer_mesh_info.hop_cnt;
					memcpy(mesh_info->peer_bssid, peer_mesh_info.peer_bssid,
						sizeof(peer_mesh_info.peer_bssid));
					max_rssi = bi_rssi;
					prefer = TRUE;
					found = TRUE;
				}
			}

			ESCAN_SCAN(dev->name,
				"%s[Mesh] BSSID=%pM, channel=%d, RSSI=%d, sec=%s, "
				"mbssid=%pM, mchannel=%d, hop=%d, pbssid=%pM, MeshID=\"%s\"\n",
				prefer?"*":" ", &bi->BSSID, bi_chan, bi_rssi, bi_sae?"SAE":"OPEN",
				&peer_mesh_info.master_bssid, peer_mesh_info.master_channel,
				peer_mesh_info.hop_cnt, &peer_mesh_info.peer_bssid, bi_meshid.SSID);
		} else {
			ESCAN_SCAN(dev->name,
				"[AP] BSSID=%pM, channel=%d, RSSI=%d, SSID=\"%s\"\n",
				&bi->BSSID, bi_chan, bi_rssi, bi->SSID);
		}
	}

exit:
	mutex_unlock(&escan->usr_sync);
	return found;
}
#endif /* WLMESH */

static void
wl_escan_deinit(struct net_device *dev, struct wl_escan_info *escan)
{
	ESCAN_TRACE(dev->name, "Enter\n");

	del_timer_sync(&escan->scan_timeout);
	escan->escan_state = ESCAN_STATE_DOWN;

#if defined(RSSIAVG)
	wl_free_rssi_cache(&escan->g_rssi_cache_ctrl);
#endif
#if defined(BSSCACHE)
	wl_free_bss_cache(&escan->g_bss_cache_ctrl);
#endif
}

static s32
wl_escan_init(struct net_device *dev, struct wl_escan_info *escan)
{
	ESCAN_TRACE(dev->name, "Enter\n");

	/* Init scan_timeout timer */
	init_timer_compat(&escan->scan_timeout, wl_escan_timeout, escan);
	escan->escan_state = ESCAN_STATE_IDLE;

	return 0;
}

void
wl_escan_down(struct net_device *dev)
{
	struct dhd_pub *dhdp = dhd_get_pub(dev);
	struct wl_escan_info *escan = dhdp->escan;

	ESCAN_TRACE(dev->name, "Enter\n");
	if (!escan) {
		ESCAN_ERROR(dev->name, "escan is NULL\n");
		return;
	}

	escan->scan_params_v2 = false;

	wl_escan_deinit(dev, escan);
}

int
wl_escan_up(struct net_device *dev)
{
	struct dhd_pub *dhdp = dhd_get_pub(dev);
	struct wl_escan_info *escan = dhdp->escan;
	u8 ioctl_buf[WLC_IOCTL_SMLEN];
	s32 val = 0;
	int ret = -1;

	ESCAN_TRACE(dev->name, "Enter\n");
	if (!escan) {
		ESCAN_ERROR(dev->name, "escan is NULL\n");
		return ret;
	}

	ret = wl_escan_init(dev, escan);
	if (ret) {
		ESCAN_ERROR(dev->name, "wl_escan_init ret %d\n", ret);
		return ret;
	}

	if (!escan->ioctl_ver) {
		val = 1;
		if ((ret = wldev_ioctl(dev, WLC_GET_VERSION, &val, sizeof(int), false) < 0)) {
			ESCAN_ERROR(dev->name, "WLC_GET_VERSION failed, ret=%d\n", ret);
			return ret;
		}
		val = dtoh32(val);
		if (val != WLC_IOCTL_VERSION && val != 1) {
			ESCAN_ERROR(dev->name,
				"Version mismatch, please upgrade. Got %d, expected %d or 1\n",
				val, WLC_IOCTL_VERSION);
			return ret;
		}
		escan->ioctl_ver = val;
	}

	if ((ret = wldev_iovar_getbuf(dev, "scan_ver", NULL, 0,
		ioctl_buf, sizeof(ioctl_buf), NULL)) == BCME_OK) {
		ESCAN_TRACE(dev->name, "scan_params v2\n");
		/* use scan_params ver2 */
		escan->scan_params_v2 = true;
	} else {
		if (ret == BCME_UNSUPPORTED) {
			ESCAN_TRACE(dev->name, "scan_ver, UNSUPPORTED\n");
			ret = BCME_OK;
		} else {
			ESCAN_ERROR(dev->name, "get scan_ver err(%d)\n", ret);
		}
	}

	return 0;
}

int
wl_escan_event_dettach(struct net_device *dev, int ifidx)
{
	struct dhd_pub *dhdp = dhd_get_pub(dev);
	struct wl_escan_info *escan = dhdp->escan;
	int ret = -1;

	if (!escan) {
		ESCAN_ERROR(dev->name, "escan is NULL\n");
		return ret;
	}

	if (ifidx < DHD_MAX_IFS) {
		wl_ext_event_deregister(dev, dhdp, WLC_E_ESCAN_RESULT, wl_escan_ext_handler);
	}

	return 0;
}

int
wl_escan_event_attach(struct net_device *dev, int ifidx)
{
	struct dhd_pub *dhdp = dhd_get_pub(dev);
	struct wl_escan_info *escan = dhdp->escan;
	int ret = -1;

	if (!escan) {
		ESCAN_ERROR(dev->name, "escan is NULL\n");
		return ret;
	}

	if (ifidx < DHD_MAX_IFS) {
		ret = wl_ext_event_register(dev, dhdp, WLC_E_ESCAN_RESULT, wl_escan_ext_handler,
			escan, PRIO_EVENT_ESCAN);
		if (ret) {
			ESCAN_ERROR(dev->name, "wl_ext_event_register err %d\n", ret);
		}
	}

	return ret;
}

void
wl_escan_detach(struct net_device *dev)
{
	struct dhd_pub *dhdp = dhd_get_pub(dev);
	struct wl_escan_info *escan = dhdp->escan;

	ESCAN_TRACE(dev->name, "Enter\n");

	if (!escan)
		return;

	wl_escan_deinit(dev, escan);
	if (escan->escan_ioctl_buf) {
		kfree(escan->escan_ioctl_buf);
		escan->escan_ioctl_buf = NULL;
	}
	wl_ext_event_deregister(dev, dhdp, WLC_E_ESCAN_RESULT, wl_escan_ext_handler);

	DHD_OS_PREFREE(dhdp, escan, sizeof(struct wl_escan_info));
	dhdp->escan = NULL;
}

int
wl_escan_attach(struct net_device *dev)
{
	struct dhd_pub *dhdp = dhd_get_pub(dev);
	struct wl_escan_info *escan = NULL;
	int ret = 0;

	ESCAN_TRACE(dev->name, "Enter\n");

	escan = (struct wl_escan_info *)DHD_OS_PREALLOC(dhdp,
		DHD_PREALLOC_WL_ESCAN, sizeof(struct wl_escan_info));
	if (!escan)
		return -ENOMEM;
	memset(escan, 0, sizeof(struct wl_escan_info));

	dhdp->escan = escan;

	/* we only care about main interface so save a global here */
	escan->pub = dhdp;
	escan->escan_state = ESCAN_STATE_DOWN;

	escan->escan_ioctl_buf = (void *)kzalloc(WLC_IOCTL_MAXLEN, GFP_KERNEL);
	if (unlikely(!escan->escan_ioctl_buf)) {
		ESCAN_ERROR(dev->name, "Ioctl buf alloc failed\n");
		ret = -ENOMEM;
		goto exit;
	}
	ret = wl_escan_init(dev, escan);
	if (ret) {
		ESCAN_ERROR(dev->name, "wl_escan_init err %d\n", ret);
		goto exit;
	}
	mutex_init(&escan->usr_sync);

	return 0;

exit:
	wl_escan_detach(dev);
	return ret;
}

#endif /* WL_ESCAN */
