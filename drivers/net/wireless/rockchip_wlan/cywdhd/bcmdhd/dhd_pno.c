/*
 * Broadcom Dongle Host Driver (DHD)
 * Prefered Network Offload and Wi-Fi Location Service(WLS) code.
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
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
 * $Id: dhd_pno.c 423669 2013-09-18 13:01:55Z yangj$
 */

#if defined(GSCAN_SUPPORT) && !defined(PNO_SUPPORT)
#error "GSCAN needs PNO to be enabled!"
#endif

#ifdef PNO_SUPPORT
#include <typedefs.h>
#include <osl.h>

#include <epivers.h>
#include <bcmutils.h>

#include <bcmendian.h>
#include <linuxver.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/sort.h>
#include <dngl_stats.h>
#include <wlioctl.h>

#include <proto/bcmevent.h>
#include <dhd.h>
#include <dhd_pno.h>
#include <dhd_dbg.h>
#ifdef GSCAN_SUPPORT
#include <linux/gcd.h>
#endif /* GSCAN_SUPPORT */

#ifdef __BIG_ENDIAN
#include <bcmendian.h>
#define htod32(i) (bcmswap32(i))
#define htod16(i) (bcmswap16(i))
#define dtoh32(i) (bcmswap32(i))
#define dtoh16(i) (bcmswap16(i))
#define htodchanspec(i) htod16(i)
#define dtohchanspec(i) dtoh16(i)
#else
#define htod32(i) (i)
#define htod16(i) (i)
#define dtoh32(i) (i)
#define dtoh16(i) (i)
#define htodchanspec(i) (i)
#define dtohchanspec(i) (i)
#endif /* IL_BIGENDINA */

#define NULL_CHECK(p, s, err)  \
			do { \
				if (!(p)) { \
					printf("NULL POINTER (%s) : %s\n", __FUNCTION__, (s)); \
					err = BCME_ERROR; \
					return err; \
				} \
			} while (0)
#define PNO_GET_PNOSTATE(dhd) ((dhd_pno_status_info_t *)dhd->pno_state)
#define PNO_BESTNET_LEN 2048
#define PNO_ON 1
#define PNO_OFF 0
#define CHANNEL_2G_MAX 14
#define CHANNEL_5G_MAX 165
#define MAX_NODE_CNT 5
#define WLS_SUPPORTED(pno_state) (pno_state->wls_supported == TRUE)
#define TIME_DIFF(timestamp1, timestamp2) (abs((uint32)(timestamp1/1000)  \
						- (uint32)(timestamp2/1000)))
#define TIME_DIFF_MS(timestamp1, timestamp2) (abs((uint32)(timestamp1)  \
						- (uint32)(timestamp2)))
#define TIMESPEC_TO_US(ts)  (((uint64)(ts).tv_sec * USEC_PER_SEC) + \
							(ts).tv_nsec / NSEC_PER_USEC)

#define ENTRY_OVERHEAD strlen("bssid=\nssid=\nfreq=\nlevel=\nage=\ndist=\ndistSd=\n====")
#define TIME_MIN_DIFF 5
#define EVENT_DATABUF_MAXLEN   (512 - sizeof(bcm_event_t))
#define EVENT_MAX_NETCNT \
   ((EVENT_DATABUF_MAXLEN - sizeof(wl_pfn_scanresults_t)) \
   / sizeof(wl_pfn_net_info_t) + 1)
#define WL_SCAN_IE_LEN_MAX      2048

static wlc_ssid_ext_t * dhd_pno_get_legacy_pno_ssid(dhd_pub_t *dhd,
        dhd_pno_status_info_t *pno_state);
#ifdef GSCAN_SUPPORT
static wl_pfn_gscan_ch_bucket_cfg_t *
dhd_pno_gscan_create_channel_list(dhd_pub_t *dhd, dhd_pno_status_info_t *pno_state,
      uint16 *chan_list, uint32 *num_buckets, uint32 *num_buckets_to_fw);
#endif /* GSCAN_SUPPORT */

static inline bool
is_dfs(uint16 channel)
{
	if (channel >= 52 && channel <= 64)			/* class 2 */
		return TRUE;
	else if (channel >= 100 && channel <= 140)	/* class 4 */
		return TRUE;
	else
		return FALSE;
}
int
dhd_pno_clean(dhd_pub_t *dhd)
{
	int pfn = 0;
	int err;
	dhd_pno_status_info_t *_pno_state;
	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	_pno_state = PNO_GET_PNOSTATE(dhd);
	DHD_PNO(("%s enter\n", __FUNCTION__));
	/* Disable PNO */
	err = dhd_iovar(dhd, 0, "pfn", (char *)&pfn, sizeof(pfn), NULL, 0, TRUE);
	if (err < 0) {
		DHD_ERROR(("%s : failed to execute pfn(error : %d)\n",
			__FUNCTION__, err));
		goto exit;
	}
	_pno_state->pno_status = DHD_PNO_DISABLED;
	err = dhd_iovar(dhd, 0, "pfnclear", NULL, 0, NULL, 0, TRUE);
	if (err < 0) {
		DHD_ERROR(("%s : failed to execute pfnclear(error : %d)\n",
			__FUNCTION__, err));
	}
exit:
	return err;
}

bool
dhd_is_pno_supported(dhd_pub_t *dhd)
{
	dhd_pno_status_info_t *_pno_state;

	if (!dhd || !dhd->pno_state) {
		DHD_ERROR(("NULL POINTER : %s\n",
			__FUNCTION__));
		return FALSE;
	}
	_pno_state = PNO_GET_PNOSTATE(dhd);
	return WLS_SUPPORTED(_pno_state);
}

bool
dhd_is_legacy_pno_enabled(dhd_pub_t *dhd)
{
	dhd_pno_status_info_t *_pno_state;

	if (!dhd || !dhd->pno_state) {
		DHD_ERROR(("NULL POINTER : %s\n",
			__FUNCTION__));
		return FALSE;
	}
	_pno_state = PNO_GET_PNOSTATE(dhd);
	return ((_pno_state->pno_mode & DHD_PNO_LEGACY_MODE) != 0);
}

#ifdef GSCAN_SUPPORT
static uint64
convert_fw_rel_time_to_systime(uint32 fw_ts_ms)
{
	struct timespec ts;

	get_monotonic_boottime(&ts);
	return ((uint64)(TIMESPEC_TO_US(ts)) - (uint64)(fw_ts_ms * 1000));
}

static void
dhd_pno_idx_to_ssid(struct dhd_pno_gscan_params *gscan_params,
            dhd_epno_results_t *res, uint32 idx)
{
	dhd_epno_params_t *iter, *next;

	if (gscan_params->num_epno_ssid > 0) {
		list_for_each_entry_safe(iter, next,
			&gscan_params->epno_ssid_list, list) {
			if (iter->index == idx) {
				memcpy(res->ssid, iter->ssid, iter->ssid_len);
				res->ssid_len = iter->ssid_len;
				return;
			}
		}
	}
	/* If we are here then there was no match */
	res->ssid[0] = '\0';
	res->ssid_len = 0;
	return;
}

/* Cleanup all results */
static void
dhd_gscan_clear_all_batch_results(dhd_pub_t *dhd)
{
	struct dhd_pno_gscan_params *gscan_params;
	dhd_pno_status_info_t *_pno_state;
	gscan_results_cache_t *iter;

	_pno_state = PNO_GET_PNOSTATE(dhd);
	gscan_params = &_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS].params_gscan;
	iter = gscan_params->gscan_batch_cache;
	/* Mark everything as consumed */
	while (iter) {
		iter->tot_consumed = iter->tot_count;
		iter = iter->next;
	}
	dhd_gscan_batch_cache_cleanup(dhd);
	return;
}

static int
_dhd_pno_gscan_cfg(dhd_pub_t *dhd, wl_pfn_gscan_cfg_t *pfncfg_gscan_param, int size)
{
	int err = BCME_OK;
	NULL_CHECK(dhd, "dhd is NULL", err);

	DHD_PNO(("%s enter\n", __FUNCTION__));

	err = dhd_iovar(dhd, 0, "pfn_gscan_cfg", (char *)pfncfg_gscan_param, size, NULL, 0, TRUE);
	if (err < 0) {
		DHD_ERROR(("%s : failed to execute pfncfg_gscan_param\n", __FUNCTION__));
		goto exit;
	}
exit:
	return err;
}

static bool
is_batch_retrieval_complete(struct dhd_pno_gscan_params *gscan_params)
{
	smp_rmb();
	return (gscan_params->get_batch_flag == GSCAN_BATCH_RETRIEVAL_COMPLETE);
}
#endif /* GSCAN_SUPPORT */

static int
_dhd_pno_suspend(dhd_pub_t *dhd)
{
	int err;
	int suspend = 1;
	dhd_pno_status_info_t *_pno_state;
	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);

	DHD_PNO(("%s enter\n", __FUNCTION__));
	_pno_state = PNO_GET_PNOSTATE(dhd);
	err = dhd_iovar(dhd, 0, "pfn_suspend", (char *)&suspend, sizeof(suspend), NULL, 0, TRUE);
	if (err < 0) {
		DHD_ERROR(("%s : failed to suspend pfn(error :%d)\n", __FUNCTION__, err));
		goto exit;

	}
	_pno_state->pno_status = DHD_PNO_SUSPEND;
exit:
	return err;
}
static int
_dhd_pno_enable(dhd_pub_t *dhd, int enable)
{
	int err = BCME_OK;
	dhd_pno_status_info_t *_pno_state;
	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	_pno_state = PNO_GET_PNOSTATE(dhd);
	DHD_PNO(("%s enter\n", __FUNCTION__));

	if (enable & 0xfffe) {
		DHD_ERROR(("%s invalid value\n", __FUNCTION__));
		err = BCME_BADARG;
		goto exit;
	}
	if (!dhd_support_sta_mode(dhd)) {
		DHD_ERROR(("PNO is not allowed for non-STA mode"));
		err = BCME_BADOPTION;
		goto exit;
	}
	if (enable) {
		if ((_pno_state->pno_mode & DHD_PNO_LEGACY_MODE) &&
			dhd_is_associated(dhd, NULL, NULL)) {
			DHD_ERROR(("%s Legacy PNO mode cannot be enabled "
				"in assoc mode , ignore it\n", __FUNCTION__));
			err = BCME_BADOPTION;
			goto exit;
		}
	}
	/* Enable/Disable PNO */
	err = dhd_iovar(dhd, 0, "pfn", (char *)&enable, sizeof(enable), NULL, 0, TRUE);
	if (err < 0) {
		DHD_ERROR(("%s : failed to execute pfn_set - %d\n", __FUNCTION__, err));
		goto exit;
	}
	_pno_state->pno_status = (enable)?
		DHD_PNO_ENABLED : DHD_PNO_DISABLED;
	if (!enable)
		_pno_state->pno_mode = DHD_PNO_NONE_MODE;

	DHD_PNO(("%s set pno as %s\n",
		__FUNCTION__, enable ? "Enable" : "Disable"));
exit:
	return err;
}

static int
_dhd_pno_set(dhd_pub_t *dhd, const dhd_pno_params_t *pno_params, dhd_pno_mode_t mode)
{
	int err = BCME_OK;
	wl_pfn_param_t pfn_param;
	dhd_pno_params_t *_params;
	dhd_pno_status_info_t *_pno_state;
	bool combined_scan = FALSE;
	DHD_PNO(("%s enter\n", __FUNCTION__));

	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	_pno_state = PNO_GET_PNOSTATE(dhd);

	memset(&pfn_param, 0, sizeof(pfn_param));

	/* set pfn parameters */
	pfn_param.version = htod32(PFN_VERSION);
	pfn_param.flags = ((PFN_LIST_ORDER << SORT_CRITERIA_BIT) |
		(ENABLE << IMMEDIATE_SCAN_BIT) | (ENABLE << REPORT_SEPERATELY_BIT));
	if (mode == DHD_PNO_LEGACY_MODE) {
		/* check and set extra pno params */
		if ((pno_params->params_legacy.pno_repeat != 0) ||
			(pno_params->params_legacy.pno_freq_expo_max != 0)) {
			pfn_param.flags |= htod16(ENABLE << ENABLE_ADAPTSCAN_BIT);
			pfn_param.repeat = (uchar) (pno_params->params_legacy.pno_repeat);
			pfn_param.exp = (uchar) (pno_params->params_legacy.pno_freq_expo_max);
		}
		/* set up pno scan fr */
		if (pno_params->params_legacy.scan_fr != 0)
			pfn_param.scan_freq = htod32(pno_params->params_legacy.scan_fr);
		if (_pno_state->pno_mode & DHD_PNO_BATCH_MODE) {
			DHD_PNO(("will enable combined scan with BATCHIG SCAN MODE\n"));
			mode |= DHD_PNO_BATCH_MODE;
			combined_scan = TRUE;
		} else if (_pno_state->pno_mode & DHD_PNO_HOTLIST_MODE) {
			DHD_PNO(("will enable combined scan with HOTLIST SCAN MODE\n"));
			mode |= DHD_PNO_HOTLIST_MODE;
			combined_scan = TRUE;
		}
#ifdef GSCAN_SUPPORT
		else if (_pno_state->pno_mode & DHD_PNO_GSCAN_MODE) {
			DHD_PNO(("will enable combined scan with GSCAN SCAN MODE\n"));
			mode |= DHD_PNO_GSCAN_MODE;
		}
#endif /* GSCAN_SUPPORT */
	}
	if (mode & (DHD_PNO_BATCH_MODE | DHD_PNO_HOTLIST_MODE)) {
		/* Scan frequency of 30 sec */
		pfn_param.scan_freq = htod32(30);
		/* slow adapt scan is off by default */
		pfn_param.slow_freq = htod32(0);
		/* RSSI margin of 30 dBm */
		pfn_param.rssi_margin = htod16(PNO_RSSI_MARGIN_DBM);
		/* Network timeout 60 sec */
		pfn_param.lost_network_timeout = htod32(60);
		/* best n = 2 by default */
		pfn_param.bestn = DEFAULT_BESTN;
		/* mscan m=0 by default, so not record best networks by default */
		pfn_param.mscan = DEFAULT_MSCAN;
		/*  default repeat = 10 */
		pfn_param.repeat = DEFAULT_REPEAT;
		/* by default, maximum scan interval = 2^2
		 * scan_freq when adaptive scan is turned on
		 */
		pfn_param.exp = DEFAULT_EXP;
		if (mode == DHD_PNO_BATCH_MODE) {
			/* In case of BATCH SCAN */
			if (pno_params->params_batch.bestn)
				pfn_param.bestn = pno_params->params_batch.bestn;
			if (pno_params->params_batch.scan_fr)
				pfn_param.scan_freq = htod32(pno_params->params_batch.scan_fr);
			if (pno_params->params_batch.mscan)
				pfn_param.mscan = pno_params->params_batch.mscan;
			/* enable broadcast scan */
			pfn_param.flags |= (ENABLE << ENABLE_BD_SCAN_BIT);
		} else if (mode == DHD_PNO_HOTLIST_MODE) {
			/* In case of HOTLIST SCAN */
			if (pno_params->params_hotlist.scan_fr)
				pfn_param.scan_freq = htod32(pno_params->params_hotlist.scan_fr);
			pfn_param.bestn = 0;
			pfn_param.repeat = 0;
			/* enable broadcast scan */
			pfn_param.flags |= (ENABLE << ENABLE_BD_SCAN_BIT);
		}
		if (combined_scan) {
			/* Disable Adaptive Scan */
			pfn_param.flags &= ~(htod16(ENABLE << ENABLE_ADAPTSCAN_BIT));
			pfn_param.flags |= (ENABLE << ENABLE_BD_SCAN_BIT);
			pfn_param.repeat = 0;
			pfn_param.exp = 0;
			if (_pno_state->pno_mode & DHD_PNO_BATCH_MODE) {
				/* In case of Legacy PNO + BATCH SCAN */
				_params = &(_pno_state->pno_params_arr[INDEX_OF_BATCH_PARAMS]);
				if (_params->params_batch.bestn)
					pfn_param.bestn = _params->params_batch.bestn;
				if (_params->params_batch.scan_fr)
					pfn_param.scan_freq = htod32(_params->params_batch.scan_fr);
				if (_params->params_batch.mscan)
					pfn_param.mscan = _params->params_batch.mscan;
			} else if (_pno_state->pno_mode & DHD_PNO_HOTLIST_MODE) {
				/* In case of Legacy PNO + HOTLIST SCAN */
				_params = &(_pno_state->pno_params_arr[INDEX_OF_HOTLIST_PARAMS]);
				if (_params->params_hotlist.scan_fr)
				pfn_param.scan_freq = htod32(_params->params_hotlist.scan_fr);
				pfn_param.bestn = 0;
				pfn_param.repeat = 0;
			}
		}
	}
#ifdef GSCAN_SUPPORT
	if (mode & DHD_PNO_GSCAN_MODE) {
		uint32 lost_network_timeout;

		pfn_param.scan_freq = htod32(pno_params->params_gscan.scan_fr);
		if (pno_params->params_gscan.mscan) {
			pfn_param.bestn = pno_params->params_gscan.bestn;
			pfn_param.mscan =  pno_params->params_gscan.mscan;
			pfn_param.flags |= (ENABLE << ENABLE_BD_SCAN_BIT);
		}
		/* RSSI margin of 30 dBm */
		pfn_param.rssi_margin = htod16(PNO_RSSI_MARGIN_DBM);
		pfn_param.repeat = 0;
		pfn_param.exp = 0;
		pfn_param.slow_freq = 0;
		pfn_param.flags |= htod16(ENABLE << ENABLE_ADAPTSCAN_BIT);

		if (_pno_state->pno_mode & DHD_PNO_LEGACY_MODE) {
			dhd_pno_status_info_t *_pno_state = PNO_GET_PNOSTATE(dhd);
			dhd_pno_params_t *_params;

			_params = &(_pno_state->pno_params_arr[INDEX_OF_LEGACY_PARAMS]);

			pfn_param.scan_freq = htod32(gcd(pno_params->params_gscan.scan_fr,
			                 _params->params_legacy.scan_fr));

			if ((_params->params_legacy.pno_repeat != 0) ||
				(_params->params_legacy.pno_freq_expo_max != 0)) {
				pfn_param.repeat = (uchar) (_params->params_legacy.pno_repeat);
				pfn_param.exp = (uchar) (_params->params_legacy.pno_freq_expo_max);
			}
		}

		lost_network_timeout = (pno_params->params_gscan.max_ch_bucket_freq *
		                        pfn_param.scan_freq *
		                        pno_params->params_gscan.lost_ap_window);
		if (lost_network_timeout) {
			pfn_param.lost_network_timeout = htod32(MIN(lost_network_timeout,
			                                 GSCAN_MIN_BSSID_TIMEOUT));
		} else {
			pfn_param.lost_network_timeout = htod32(GSCAN_MIN_BSSID_TIMEOUT);
		}
	} else
#endif /* GSCAN_SUPPORT */
	{
		if (pfn_param.scan_freq < htod32(PNO_SCAN_MIN_FW_SEC) ||
			pfn_param.scan_freq > htod32(PNO_SCAN_MAX_FW_SEC)) {
			DHD_ERROR(("%s pno freq(%d sec) is not valid \n",
				__FUNCTION__, PNO_SCAN_MIN_FW_SEC));
			err = BCME_BADARG;
			goto exit;
		}
	}

	err = dhd_set_rand_mac_oui(dhd);
	/* Ignore if chip doesnt support the feature */
	if (err < 0 && err != BCME_UNSUPPORTED) {
		DHD_ERROR(("%s : failed to set random mac for PNO scan, %d\n", __FUNCTION__, err));
		goto exit;
	}

#ifdef GSCAN_SUPPORT
			if (mode == DHD_PNO_BATCH_MODE ||
				((mode & DHD_PNO_GSCAN_MODE) && pno_params->params_gscan.mscan)) {
#else
			if (mode == DHD_PNO_BATCH_MODE) {
#endif /* GSCAN_SUPPORT */

		int _tmp = pfn_param.bestn;
		/* set bestn to calculate the max mscan which firmware supports */
		err = dhd_iovar(dhd, 0, "pfnmem", (char *)&_tmp, sizeof(_tmp), NULL, 0, TRUE);
		if (err < 0) {
			DHD_ERROR(("%s : failed to set pfnmem\n", __FUNCTION__));
			goto exit;
		}
		/* get max mscan which the firmware supports */
		err = dhd_iovar(dhd, 0, "pfnmem", NULL, 0, (char *)&_tmp, sizeof(_tmp), FALSE);
		if (err < 0) {
			DHD_ERROR(("%s : failed to get pfnmem\n", __FUNCTION__));
			goto exit;
		}
		pfn_param.mscan = MIN(pfn_param.mscan, _tmp);
		DHD_PNO((" returned mscan : %d, set bestn : %d mscan %d\n", _tmp, pfn_param.bestn,
		        pfn_param.mscan));
	}
	err = dhd_iovar(dhd, 0, "pfn_set", (char *)&pfn_param, sizeof(pfn_param), NULL, 0, TRUE);
	if (err < 0) {
		DHD_ERROR(("%s : failed to execute pfn_set %d\n", __FUNCTION__, err));
		goto exit;
	}
	/* need to return mscan if this is for batch scan instead of err */
	err = (mode == DHD_PNO_BATCH_MODE)? pfn_param.mscan : err;
exit:
	return err;
}
static int
_dhd_pno_add_ssid(dhd_pub_t *dhd, wlc_ssid_ext_t* ssids_list, int nssid)
{
	int err = BCME_OK;
	int i = 0;
	wl_pfn_t pfn_element;
	NULL_CHECK(dhd, "dhd is NULL", err);
	if (nssid) {
		NULL_CHECK(ssids_list, "ssid list is NULL", err);
	}
	memset(&pfn_element, 0, sizeof(pfn_element));
	{
		int j;
		for (j = 0; j < nssid; j++) {
			DHD_PNO(("%s size = %d hidden = %d flags = %x rssi_thresh %d\n",
				ssids_list[j].SSID, ssids_list[j].SSID_len, ssids_list[j].hidden,
				ssids_list[j].flags, ssids_list[i].rssi_thresh));
		}
	}
	/* Check for broadcast ssid */
	for (i = 0; i < nssid; i++) {
		if (!ssids_list[i].SSID_len) {
			DHD_ERROR(("%d: Broadcast SSID is ilegal for PNO setting\n", i));
			err = BCME_ERROR;
			goto exit;
		}
	}
	/* set all pfn ssid */
	for (i = 0; i < nssid; i++) {
		pfn_element.infra = htod32(DOT11_BSSTYPE_INFRASTRUCTURE);
		pfn_element.auth = (DOT11_OPEN_SYSTEM);
		pfn_element.wpa_auth = htod32(WPA_AUTH_PFN_ANY);
		pfn_element.wsec = htod32(0);
		pfn_element.infra = htod32(1);
		if (ssids_list[i].hidden) {
			pfn_element.flags = htod32(ENABLE << WL_PFN_HIDDEN_BIT);
		} else {
			pfn_element.flags = 0;
		}
		pfn_element.flags |= htod32(ssids_list[i].flags);
		/* If a single RSSI threshold is defined, use that */
#ifdef PNO_MIN_RSSI_TRIGGER
		pfn_element.flags |= ((PNO_MIN_RSSI_TRIGGER & 0xFF) << WL_PFN_RSSI_SHIFT);
#else
		pfn_element.flags |= ((ssids_list[i].rssi_thresh & 0xFF) << WL_PFN_RSSI_SHIFT);
#endif /* PNO_MIN_RSSI_TRIGGER */
		memcpy((char *)pfn_element.ssid.SSID, ssids_list[i].SSID,
			ssids_list[i].SSID_len);
		pfn_element.ssid.SSID_len = ssids_list[i].SSID_len;
		err = dhd_iovar(dhd, 0, "pfn_add", (char *)&pfn_element, sizeof(pfn_element),
				NULL, 0, TRUE);
		if (err < 0) {
			DHD_ERROR(("%s : failed to execute pfn_add\n", __FUNCTION__));
			goto exit;
		}
	}
exit:
	return err;
}
/* qsort compare function */
static int
_dhd_pno_cmpfunc(const void *a, const void *b)
{
	return (*(uint16*)a - *(uint16*)b);
}
static int
_dhd_pno_chan_merge(uint16 *d_chan_list, int *nchan,
	uint16 *chan_list1, int nchan1, uint16 *chan_list2, int nchan2)
{
	int err = BCME_OK;
	int i = 0, j = 0, k = 0;
	uint16 tmp;
	NULL_CHECK(d_chan_list, "d_chan_list is NULL", err);
	NULL_CHECK(nchan, "nchan is NULL", err);
	NULL_CHECK(chan_list1, "chan_list1 is NULL", err);
	NULL_CHECK(chan_list2, "chan_list2 is NULL", err);
	/* chan_list1 and chan_list2 should be sorted at first */
	while (i < nchan1 && j < nchan2) {
		tmp = chan_list1[i] < chan_list2[j]?
			chan_list1[i++] : chan_list2[j++];
		for (; i < nchan1 && chan_list1[i] == tmp; i++);
		for (; j < nchan2 && chan_list2[j] == tmp; j++);
		d_chan_list[k++] = tmp;
	}

	while (i < nchan1) {
		tmp = chan_list1[i++];
		for (; i < nchan1 && chan_list1[i] == tmp; i++);
		d_chan_list[k++] = tmp;
	}

	while (j < nchan2) {
		tmp = chan_list2[j++];
		for (; j < nchan2 && chan_list2[j] == tmp; j++);
		d_chan_list[k++] = tmp;

	}
	*nchan = k;
	return err;
}
static int
_dhd_pno_get_channels(dhd_pub_t *dhd, uint16 *d_chan_list,
	int *nchan, uint8 band, bool skip_dfs)
{
	int err = BCME_OK;
	int i, j;
	uint32 chan_buf[WL_NUMCHANNELS + 1];
	wl_uint32_list_t *list;
	NULL_CHECK(dhd, "dhd is NULL", err);
	if (*nchan) {
		NULL_CHECK(d_chan_list, "d_chan_list is NULL", err);
	}
	list = (wl_uint32_list_t *) (void *)chan_buf;
	list->count = htod32(WL_NUMCHANNELS);
	err = dhd_wl_ioctl_cmd(dhd, WLC_GET_VALID_CHANNELS, chan_buf, sizeof(chan_buf), FALSE, 0);
	if (err < 0) {
		DHD_ERROR(("failed to get channel list (err: %d)\n", err));
		goto exit;
	}
	for (i = 0, j = 0; i < dtoh32(list->count) && i < *nchan; i++) {
		if (band == WLC_BAND_2G) {
			if (dtoh32(list->element[i]) > CHANNEL_2G_MAX)
				continue;
		} else if (band == WLC_BAND_5G) {
			if (dtoh32(list->element[i]) <= CHANNEL_2G_MAX)
				continue;
			if (skip_dfs && is_dfs(dtoh32(list->element[i])))
				continue;

		} else if (band == WLC_BAND_AUTO) {
			if (skip_dfs || !is_dfs(dtoh32(list->element[i])))
				continue;

		} else { /* All channels */
			if (skip_dfs && is_dfs(dtoh32(list->element[i])))
				continue;
		}
		if (dtoh32(list->element[i]) <= CHANNEL_5G_MAX) {
			d_chan_list[j++] = (uint16) dtoh32(list->element[i]);
		} else {
			err = BCME_BADCHAN;
			goto exit;
		}
	}
	*nchan = j;
exit:
	return err;
}
static int
_dhd_pno_convert_format(dhd_pub_t *dhd, struct dhd_pno_batch_params *params_batch,
	char *buf, int nbufsize)
{
	int err = BCME_OK;
	int bytes_written = 0, nreadsize = 0;
	int t_delta = 0;
	int nleftsize = nbufsize;
	uint8 cnt = 0;
	char *bp = buf;
	char eabuf[ETHER_ADDR_STR_LEN];
#ifdef PNO_DEBUG
	char *_base_bp;
	char msg[150];
#endif
	dhd_pno_bestnet_entry_t *iter, *next;
	dhd_pno_scan_results_t *siter, *snext;
	dhd_pno_best_header_t *phead, *pprev;
	NULL_CHECK(params_batch, "params_batch is NULL", err);
	if (nbufsize > 0)
		NULL_CHECK(buf, "buf is NULL", err);
	/* initialize the buffer */
	memset(buf, 0, nbufsize);
	DHD_PNO(("%s enter \n", __FUNCTION__));
	/* # of scans */
	if (!params_batch->get_batch.batch_started) {
		bp += nreadsize = sprintf(bp, "scancount=%d\n",
			params_batch->get_batch.expired_tot_scan_cnt);
		nleftsize -= nreadsize;
		params_batch->get_batch.batch_started = TRUE;
	}
	DHD_PNO(("%s scancount %d\n", __FUNCTION__, params_batch->get_batch.expired_tot_scan_cnt));
	/* preestimate scan count until which scan result this report is going to end */
	list_for_each_entry_safe(siter, snext,
		&params_batch->get_batch.expired_scan_results_list, list) {
		phead = siter->bestnetheader;
		while (phead != NULL) {
			/* if left_size is less than bestheader total size , stop this */
			if (nleftsize <=
				(phead->tot_size + phead->tot_cnt * ENTRY_OVERHEAD))
				goto exit;
			/* increase scan count */
			cnt++;
			/* # best of each scan */
			DHD_PNO(("\n<loop : %d, apcount %d>\n", cnt - 1, phead->tot_cnt));
			/* attribute of the scan */
			if (phead->reason & PNO_STATUS_ABORT_MASK) {
				bp += nreadsize = sprintf(bp, "trunc\n");
				nleftsize -= nreadsize;
			}
			list_for_each_entry_safe(iter, next,
				&phead->entry_list, list) {
				t_delta = jiffies_to_msecs(jiffies - iter->recorded_time);
#ifdef PNO_DEBUG
				_base_bp = bp;
				memset(msg, 0, sizeof(msg));
#endif
				/* BSSID info */
				bp += nreadsize = sprintf(bp, "bssid=%s\n",
				bcm_ether_ntoa((const struct ether_addr *)&iter->BSSID, eabuf));
				nleftsize -= nreadsize;
				/* SSID */
				bp += nreadsize = sprintf(bp, "ssid=%s\n", iter->SSID);
				nleftsize -= nreadsize;
				/* channel */
				bp += nreadsize = sprintf(bp, "freq=%d\n",
				wf_channel2mhz(iter->channel,
				iter->channel <= CH_MAX_2G_CHANNEL?
				WF_CHAN_FACTOR_2_4_G : WF_CHAN_FACTOR_5_G));
				nleftsize -= nreadsize;
				/* RSSI */
				bp += nreadsize = sprintf(bp, "level=%d\n", iter->RSSI);
				nleftsize -= nreadsize;
				/* add the time consumed in Driver to the timestamp of firmware */
				iter->timestamp += t_delta;
				bp += nreadsize = sprintf(bp, "age=%d\n", iter->timestamp);
				nleftsize -= nreadsize;
				/* RTT0 */
				bp += nreadsize = sprintf(bp, "dist=%d\n",
				(iter->rtt0 == 0)? -1 : iter->rtt0);
				nleftsize -= nreadsize;
				/* RTT1 */
				bp += nreadsize = sprintf(bp, "distSd=%d\n",
				(iter->rtt0 == 0)? -1 : iter->rtt1);
				nleftsize -= nreadsize;
				bp += nreadsize = sprintf(bp, "%s", AP_END_MARKER);
				nleftsize -= nreadsize;
				list_del(&iter->list);
				MFREE(dhd->osh, iter, BESTNET_ENTRY_SIZE);
#ifdef PNO_DEBUG
				memcpy(msg, _base_bp, bp - _base_bp);
				DHD_PNO(("Entry : \n%s", msg));
#endif
			}
			bp += nreadsize = sprintf(bp, "%s", SCAN_END_MARKER);
			DHD_PNO(("%s", SCAN_END_MARKER));
			nleftsize -= nreadsize;
			pprev = phead;
			/* reset the header */
			siter->bestnetheader = phead = phead->next;
			MFREE(dhd->osh, pprev, BEST_HEADER_SIZE);

			siter->cnt_header--;
		}
		if (phead == NULL) {
			/* we store all entry in this scan , so it is ok to delete */
			list_del(&siter->list);
			MFREE(dhd->osh, siter, SCAN_RESULTS_SIZE);
		}
	}
exit:
	if (cnt < params_batch->get_batch.expired_tot_scan_cnt) {
		DHD_ERROR(("Buffer size is small to save all batch entry,"
			" cnt : %d (remained_scan_cnt): %d\n",
			cnt, params_batch->get_batch.expired_tot_scan_cnt - cnt));
	}
	params_batch->get_batch.expired_tot_scan_cnt -= cnt;
	/* set FALSE only if the link list  is empty after returning the data */
	if (list_empty(&params_batch->get_batch.expired_scan_results_list)) {
		params_batch->get_batch.batch_started = FALSE;
		bp += sprintf(bp, "%s", RESULTS_END_MARKER);
		DHD_PNO(("%s", RESULTS_END_MARKER));
		DHD_PNO(("%s : Getting the batching data is complete\n", __FUNCTION__));
	}
	/* return used memory in buffer */
	bytes_written = (int32)(bp - buf);
	return bytes_written;
}
static int
_dhd_pno_clear_all_batch_results(dhd_pub_t *dhd, struct list_head *head, bool only_last)
{
	int err = BCME_OK;
	int removed_scan_cnt = 0;
	dhd_pno_scan_results_t *siter, *snext;
	dhd_pno_best_header_t *phead, *pprev;
	dhd_pno_bestnet_entry_t *iter, *next;
	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(head, "head is NULL", err);
	NULL_CHECK(head->next, "head->next is NULL", err);
	DHD_PNO(("%s enter\n", __FUNCTION__));
	list_for_each_entry_safe(siter, snext,
		head, list) {
		if (only_last) {
			/* in case that we need to delete only last one */
			if (!list_is_last(&siter->list, head)) {
				/* skip if the one is not last */
				continue;
			}
		}
		/* delete all data belong if the one is last */
		phead = siter->bestnetheader;
		while (phead != NULL) {
			removed_scan_cnt++;
			list_for_each_entry_safe(iter, next,
			&phead->entry_list, list) {
				list_del(&iter->list);
				MFREE(dhd->osh, iter, BESTNET_ENTRY_SIZE);
			}
			pprev = phead;
			phead = phead->next;
			MFREE(dhd->osh, pprev, BEST_HEADER_SIZE);
		}
		if (phead == NULL) {
			/* it is ok to delete top node */
			list_del(&siter->list);
			MFREE(dhd->osh, siter, SCAN_RESULTS_SIZE);
		}
	}
	return removed_scan_cnt;
}

static int
_dhd_pno_cfg(dhd_pub_t *dhd, uint16 *channel_list, int nchan)
{
	int err = BCME_OK;
	int i = 0;
	wl_pfn_cfg_t pfncfg_param;
	NULL_CHECK(dhd, "dhd is NULL", err);
	if (nchan) {
		NULL_CHECK(channel_list, "nchan is NULL", err);
	}
	if (nchan > WL_NUMCHANNELS) {
		return BCME_RANGE;
	}
	DHD_PNO(("%s enter :  nchan : %d\n", __FUNCTION__, nchan));
	memset(&pfncfg_param, 0, sizeof(wl_pfn_cfg_t));
	/* Setup default values */
	pfncfg_param.reporttype = htod32(WL_PFN_REPORT_ALLNET);
	pfncfg_param.channel_num = htod32(0);

	for (i = 0; i < nchan; i++)
		pfncfg_param.channel_list[i] = channel_list[i];

	pfncfg_param.channel_num = htod32(nchan);
	err = dhd_iovar(dhd, 0, "pfn_cfg", (char *)&pfncfg_param, sizeof(pfncfg_param), NULL, 0,
			TRUE);
	if (err < 0) {
		DHD_ERROR(("%s : failed to execute pfn_cfg\n", __FUNCTION__));
		goto exit;
	}
exit:
	return err;
}
static int
_dhd_pno_reinitialize_prof(dhd_pub_t *dhd, dhd_pno_params_t *params, dhd_pno_mode_t mode)
{
	int err = BCME_OK;
	dhd_pno_status_info_t *_pno_state;
	NULL_CHECK(dhd, "dhd is NULL\n", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL\n", err);
	DHD_PNO(("%s enter\n", __FUNCTION__));
	_pno_state = PNO_GET_PNOSTATE(dhd);
	mutex_lock(&_pno_state->pno_mutex);
	switch (mode) {
	case DHD_PNO_LEGACY_MODE: {
		struct dhd_pno_ssid *iter, *next;
		if (params->params_legacy.nssid > 0) {
			list_for_each_entry_safe(iter, next,
				&params->params_legacy.ssid_list, list) {
				list_del(&iter->list);
				kfree(iter);
			}
		}
		params->params_legacy.nssid = 0;
		params->params_legacy.scan_fr = 0;
		params->params_legacy.pno_freq_expo_max = 0;
		params->params_legacy.pno_repeat = 0;
		params->params_legacy.nchan = 0;
		memset(params->params_legacy.chan_list, 0,
			sizeof(params->params_legacy.chan_list));
		break;
	}
	case DHD_PNO_BATCH_MODE: {
		params->params_batch.scan_fr = 0;
		params->params_batch.mscan = 0;
		params->params_batch.nchan = 0;
		params->params_batch.rtt = 0;
		params->params_batch.bestn = 0;
		params->params_batch.nchan = 0;
		params->params_batch.band = WLC_BAND_AUTO;
		memset(params->params_batch.chan_list, 0,
			sizeof(params->params_batch.chan_list));
		params->params_batch.get_batch.batch_started = FALSE;
		params->params_batch.get_batch.buf = NULL;
		params->params_batch.get_batch.bufsize = 0;
		params->params_batch.get_batch.reason = 0;
		_dhd_pno_clear_all_batch_results(dhd,
			&params->params_batch.get_batch.scan_results_list, FALSE);
		_dhd_pno_clear_all_batch_results(dhd,
			&params->params_batch.get_batch.expired_scan_results_list, FALSE);
		params->params_batch.get_batch.tot_scan_cnt = 0;
		params->params_batch.get_batch.expired_tot_scan_cnt = 0;
		params->params_batch.get_batch.top_node_cnt = 0;
		INIT_LIST_HEAD(&params->params_batch.get_batch.scan_results_list);
		INIT_LIST_HEAD(&params->params_batch.get_batch.expired_scan_results_list);
		break;
	}
	case DHD_PNO_HOTLIST_MODE: {
		struct dhd_pno_bssid *iter, *next;
		if (params->params_hotlist.nbssid > 0) {
			list_for_each_entry_safe(iter, next,
				&params->params_hotlist.bssid_list, list) {
				list_del(&iter->list);
				kfree(iter);
			}
		}
		params->params_hotlist.scan_fr = 0;
		params->params_hotlist.nbssid = 0;
		params->params_hotlist.nchan = 0;
		params->params_batch.band = WLC_BAND_AUTO;
		memset(params->params_hotlist.chan_list, 0,
			sizeof(params->params_hotlist.chan_list));
		break;
	}
	default:
		DHD_ERROR(("%s : unknown mode : %d\n", __FUNCTION__, mode));
		break;
	}
	mutex_unlock(&_pno_state->pno_mutex);
	return err;
}
static int
_dhd_pno_add_bssid(dhd_pub_t *dhd, wl_pfn_bssid_t *p_pfn_bssid, int nbssid)
{
	int err = BCME_OK;
	NULL_CHECK(dhd, "dhd is NULL", err);
	if (nbssid) {
		NULL_CHECK(p_pfn_bssid, "bssid list is NULL", err);
	}
	err = dhd_iovar(dhd, 0, "pfn_add_bssid", (char *)p_pfn_bssid,
			sizeof(wl_pfn_bssid_t) * nbssid, NULL, 0, TRUE);
	if (err < 0) {
		DHD_ERROR(("%s : failed to execute pfn_cfg\n", __FUNCTION__));
		goto exit;
	}
exit:
	return err;
}

#ifdef GSCAN_SUPPORT
static int
_dhd_pno_add_significant_bssid(dhd_pub_t *dhd,
   wl_pfn_significant_bssid_t *p_pfn_significant_bssid, int nbssid)
{
	int err = BCME_OK;
	NULL_CHECK(dhd, "dhd is NULL", err);

	if (!nbssid) {
		err = BCME_ERROR;
		goto exit;
	}

	NULL_CHECK(p_pfn_significant_bssid, "bssid list is NULL", err);

	err = dhd_iovar(dhd, 0, "pfn_add_swc_bssid", (char *)p_pfn_significant_bssid,
			sizeof(wl_pfn_significant_bssid_t) * nbssid, NULL, 0, TRUE);
	if (err < 0) {
		DHD_ERROR(("%s : failed to execute pfn_significant_bssid %d\n", __FUNCTION__, err));
		goto exit;
	}
exit:
	return err;
}
#endif /* GSCAN_SUPPORT */

int
dhd_pno_stop_for_ssid(dhd_pub_t *dhd)
{
	int err = BCME_OK;
	uint32 mode = 0;
	dhd_pno_status_info_t *_pno_state;
	dhd_pno_params_t *_params;
	wl_pfn_bssid_t *p_pfn_bssid = NULL;
	NULL_CHECK(dhd, "dev is NULL", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	_pno_state = PNO_GET_PNOSTATE(dhd);
	if (!(_pno_state->pno_mode & DHD_PNO_LEGACY_MODE)) {
		DHD_ERROR(("%s : LEGACY PNO MODE is not enabled\n", __FUNCTION__));
		goto exit;
	}
	DHD_PNO(("%s enter\n", __FUNCTION__));
	_pno_state->pno_mode &= ~DHD_PNO_LEGACY_MODE;
#ifdef GSCAN_SUPPORT
	if (_pno_state->pno_mode & DHD_PNO_GSCAN_MODE) {
		struct dhd_pno_gscan_params *gscan_params;

		_params = &_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS];
		gscan_params = &_params->params_gscan;
		if (gscan_params->mscan) {
			/* retrieve the batching data from firmware into host */
			err = dhd_wait_batch_results_complete(dhd);
			if (err != BCME_OK)
				goto exit;
		}
		/* save current pno_mode before calling dhd_pno_clean */
		mutex_lock(&_pno_state->pno_mutex);
		mode = _pno_state->pno_mode;
		err = dhd_pno_clean(dhd);
		if (err < 0) {
			DHD_ERROR(("%s : failed to call dhd_pno_clean (err: %d)\n",
				__FUNCTION__, err));
			mutex_unlock(&_pno_state->pno_mutex);
			goto exit;
		}
		/* restore previous pno_mode */
		_pno_state->pno_mode = mode;
		mutex_unlock(&_pno_state->pno_mutex);
		/* Restart gscan */
		err = dhd_pno_initiate_gscan_request(dhd, 1, 0);
		goto exit;
	}
#endif /* GSCAN_SUPPORT */
	/* restart Batch mode  if the batch mode is on */
	if (_pno_state->pno_mode & (DHD_PNO_BATCH_MODE | DHD_PNO_HOTLIST_MODE)) {
		/* retrieve the batching data from firmware into host */
		dhd_pno_get_for_batch(dhd, NULL, 0, PNO_STATUS_DISABLE);
		/* save current pno_mode before calling dhd_pno_clean */
		mode = _pno_state->pno_mode;
		dhd_pno_clean(dhd);
		/* restore previous pno_mode */
		_pno_state->pno_mode = mode;
		if (_pno_state->pno_mode & DHD_PNO_BATCH_MODE) {
			_params = &(_pno_state->pno_params_arr[INDEX_OF_BATCH_PARAMS]);
			/* restart BATCH SCAN */
			err = dhd_pno_set_for_batch(dhd, &_params->params_batch);
			if (err < 0) {
				_pno_state->pno_mode &= ~DHD_PNO_BATCH_MODE;
				DHD_ERROR(("%s : failed to restart batch scan(err: %d)\n",
					__FUNCTION__, err));
				goto exit;
			}
		} else if (_pno_state->pno_mode & DHD_PNO_HOTLIST_MODE) {
			/* restart HOTLIST SCAN */
			struct dhd_pno_bssid *iter, *next;
			_params = &(_pno_state->pno_params_arr[INDEX_OF_HOTLIST_PARAMS]);
			p_pfn_bssid = kzalloc(sizeof(wl_pfn_bssid_t) *
			_params->params_hotlist.nbssid, GFP_KERNEL);
			if (p_pfn_bssid == NULL) {
				DHD_ERROR(("%s : failed to allocate wl_pfn_bssid_t array"
				" (count: %d)",
					__FUNCTION__, _params->params_hotlist.nbssid));
				err = BCME_ERROR;
				_pno_state->pno_mode &= ~DHD_PNO_HOTLIST_MODE;
				goto exit;
			}
			/* convert dhd_pno_bssid to wl_pfn_bssid */
			list_for_each_entry_safe(iter, next,
			&_params->params_hotlist.bssid_list, list) {
				memcpy(&p_pfn_bssid->macaddr,
				&iter->macaddr, ETHER_ADDR_LEN);
				p_pfn_bssid->flags = iter->flags;
				p_pfn_bssid++;
			}
			err = dhd_pno_set_for_hotlist(dhd, p_pfn_bssid, &_params->params_hotlist);
			if (err < 0) {
				_pno_state->pno_mode &= ~DHD_PNO_HOTLIST_MODE;
				DHD_ERROR(("%s : failed to restart hotlist scan(err: %d)\n",
					__FUNCTION__, err));
				goto exit;
			}
		}
	} else {
		err = dhd_pno_clean(dhd);
		if (err < 0) {
			DHD_ERROR(("%s : failed to call dhd_pno_clean (err: %d)\n",
				__FUNCTION__, err));
			goto exit;
		}
	}
exit:
	kfree(p_pfn_bssid);
	return err;
}

int
dhd_pno_enable(dhd_pub_t *dhd, int enable)
{
	int err = BCME_OK;
	NULL_CHECK(dhd, "dhd is NULL", err);
	DHD_PNO(("%s enter\n", __FUNCTION__));
	return (_dhd_pno_enable(dhd, enable));
}

static wlc_ssid_ext_t *
dhd_pno_get_legacy_pno_ssid(dhd_pub_t *dhd, dhd_pno_status_info_t *pno_state)
{
	int err = BCME_OK;
	int i;
	struct dhd_pno_ssid *iter, *next;
	dhd_pno_params_t	*_params1 = &pno_state->pno_params_arr[INDEX_OF_LEGACY_PARAMS];
	wlc_ssid_ext_t *p_ssid_list;

	p_ssid_list = kzalloc(sizeof(wlc_ssid_ext_t) *
	                   _params1->params_legacy.nssid, GFP_KERNEL);
	if (p_ssid_list == NULL) {
		DHD_ERROR(("%s : failed to allocate wlc_ssid_ext_t array (count: %d)",
			__FUNCTION__, _params1->params_legacy.nssid));
		err = BCME_ERROR;
		pno_state->pno_mode &= ~DHD_PNO_LEGACY_MODE;
		goto exit;
	}
	i = 0;
	/* convert dhd_pno_ssid to wlc_ssid_ext_t */
	list_for_each_entry_safe(iter, next, &_params1->params_legacy.ssid_list, list) {
		p_ssid_list[i].SSID_len = iter->SSID_len;
		p_ssid_list[i].hidden = iter->hidden;
		p_ssid_list[i].rssi_thresh = iter->rssi_thresh;
		memcpy(p_ssid_list[i].SSID, iter->SSID, p_ssid_list[i].SSID_len);
		i++;
	}
exit:
	return p_ssid_list;
}

#ifdef GSCAN_SUPPORT
static int dhd_epno_set_ssid(dhd_pub_t *dhd,
            dhd_pno_status_info_t *pno_state)
{
	int err = BCME_OK;
	dhd_epno_params_t *iter, *next;
	dhd_pno_params_t *_params1 = &pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS];
	struct dhd_pno_gscan_params *gscan_params;
	wlc_ssid_ext_t ssid_elem;
	wl_pfn_ext_list_t *p_ssid_ext_elem = NULL;
	uint32 mem_needed = 0, i = 0;
	uint16 num_visible_epno_ssid;
	uint8 flags;

	gscan_params = &_params1->params_gscan;
	num_visible_epno_ssid = gscan_params->num_visible_epno_ssid;

	if (num_visible_epno_ssid) {
		mem_needed =  sizeof(wl_pfn_ext_list_t) + (sizeof(wl_pfn_ext_t) *
		                        (num_visible_epno_ssid - 1));
		p_ssid_ext_elem = kzalloc(mem_needed, GFP_KERNEL);
		if (p_ssid_ext_elem == NULL) {
			DHD_ERROR(("%s : failed to allocate memory %u\n",
				__FUNCTION__, mem_needed));
			err = BCME_NOMEM;
			goto exit;
		}
		p_ssid_ext_elem->version = PFN_SSID_EXT_VERSION;
		p_ssid_ext_elem->count = num_visible_epno_ssid;
	}

	DHD_ERROR(("Total ssids %d, visible SSIDs %d\n", gscan_params->num_epno_ssid,
	     num_visible_epno_ssid));

	/* convert dhd_pno_ssid to wlc_ssid_ext_t */
	list_for_each_entry_safe(iter, next, &gscan_params->epno_ssid_list, list) {
		if (iter->flags & DHD_PNO_USE_SSID) {
			memset(&ssid_elem, 0, sizeof(ssid_elem));
			ssid_elem.SSID_len = iter->ssid_len;
			ssid_elem.hidden = TRUE;
			flags = (iter->flags & DHD_EPNO_A_BAND_TRIG) ?
			       WL_PFN_SSID_A_BAND_TRIG: 0;
			flags |= (iter->flags & DHD_EPNO_BG_BAND_TRIG) ?
			       WL_PFN_SSID_BG_BAND_TRIG: 0;
			ssid_elem.flags = flags;
			ssid_elem.rssi_thresh = iter->rssi_thresh;
			memcpy(ssid_elem.SSID, iter->ssid, iter->ssid_len);
			if ((err = _dhd_pno_add_ssid(dhd, &ssid_elem, 1)) < 0) {
				DHD_ERROR(("failed to add ssid list (err %d) in firmware\n", err));
				goto exit;
			}
		} else if (i < num_visible_epno_ssid) {
			p_ssid_ext_elem->pfn_ext[i].rssi_thresh = iter->rssi_thresh;
			switch (iter->auth) {
				case DHD_PNO_AUTH_CODE_OPEN:
					p_ssid_ext_elem->pfn_ext[i].wpa_auth = WPA_AUTH_DISABLED;
					break;
				case DHD_PNO_AUTH_CODE_PSK:
					p_ssid_ext_elem->pfn_ext[i].wpa_auth =
					          (WPA2_AUTH_PSK | WPA_AUTH_PSK);
					break;
				case DHD_PNO_AUTH_CODE_EAPOL:
					p_ssid_ext_elem->pfn_ext[i].wpa_auth =
					           (uint16)WPA_AUTH_PFN_ANY;
					break;
				default:
					p_ssid_ext_elem->pfn_ext[i].wpa_auth =
					           (uint16)WPA_AUTH_PFN_ANY;
					break;
			}
			memcpy(p_ssid_ext_elem->pfn_ext[i].ssid, iter->ssid, iter->ssid_len);
			p_ssid_ext_elem->pfn_ext[i].ssid_len = iter->ssid_len;
			iter->index = gscan_params->ssid_ext_last_used_index++;
			flags = (iter->flags & DHD_EPNO_A_BAND_TRIG) ?
			       WL_PFN_SSID_A_BAND_TRIG: 0;
			flags |= (iter->flags & DHD_EPNO_BG_BAND_TRIG) ?
			       WL_PFN_SSID_BG_BAND_TRIG: 0;
			p_ssid_ext_elem->pfn_ext[i].flags = flags;
			DHD_ERROR(("SSID %s idx %d rssi thresh %d flags %x\n", iter->ssid,
			        iter->index, iter->rssi_thresh, flags));
			i++;
		}
	}
	if (num_visible_epno_ssid) {
		err = dhd_iovar(dhd, 0, "pfn_add_ssid_ext", (char *)p_ssid_ext_elem,
			mem_needed, NULL, 0, TRUE);

		if (err < 0) {
			DHD_ERROR(("%s : failed to execute pfn_add_pno_ext_ssid %d\n", __FUNCTION__,
			     err));
		}
	}
exit:
	kfree(p_ssid_ext_elem);
	return err;
}
#endif /* GSCAN_SUPPORT */

static int
dhd_pno_add_to_ssid_list(dhd_pno_params_t *params, wlc_ssid_ext_t *ssid_list,
    int nssid)
{
	int ret = 0;
	int i;
	struct dhd_pno_ssid *_pno_ssid;

	for (i = 0; i < nssid; i++) {
		if (ssid_list[i].SSID_len > DOT11_MAX_SSID_LEN) {
			DHD_ERROR(("%s : Invalid SSID length %d\n",
				__FUNCTION__, ssid_list[i].SSID_len));
			ret = BCME_ERROR;
			goto exit;
		}
		_pno_ssid = kzalloc(sizeof(struct dhd_pno_ssid), GFP_KERNEL);
		if (_pno_ssid == NULL) {
			DHD_ERROR(("%s : failed to allocate struct dhd_pno_ssid\n",
				__FUNCTION__));
			ret = BCME_ERROR;
			goto exit;
		}
		_pno_ssid->SSID_len = ssid_list[i].SSID_len;
		_pno_ssid->hidden = ssid_list[i].hidden;
		_pno_ssid->rssi_thresh = ssid_list[i].rssi_thresh;
		memcpy(_pno_ssid->SSID, ssid_list[i].SSID, _pno_ssid->SSID_len);
		list_add_tail(&_pno_ssid->list, &params->params_legacy.ssid_list);
		params->params_legacy.nssid++;
	}

exit:
	return ret;
}

int
dhd_pno_set_for_ssid(dhd_pub_t *dhd, wlc_ssid_ext_t* ssid_list, int nssid,
	uint16  scan_fr, int pno_repeat, int pno_freq_expo_max, uint16 *channel_list, int nchan)
{
	dhd_pno_params_t *_params;
	dhd_pno_params_t *_params2;
	dhd_pno_status_info_t *_pno_state;
	uint16 _chan_list[WL_NUMCHANNELS];
	int32 tot_nchan = 0;
	int err = BCME_OK;
	int i;
	int mode = 0;
	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	_pno_state = PNO_GET_PNOSTATE(dhd);

	if (!dhd_support_sta_mode(dhd)) {
		err = BCME_BADOPTION;
		goto exit_no_clear;
	}
	DHD_PNO(("%s enter : scan_fr :%d, pno_repeat :%d,"
			"pno_freq_expo_max: %d, nchan :%d\n", __FUNCTION__,
			scan_fr, pno_repeat, pno_freq_expo_max, nchan));

	_params = &(_pno_state->pno_params_arr[INDEX_OF_LEGACY_PARAMS]);
	/* If GSCAN is also ON will handle this down below */
#ifdef GSCAN_SUPPORT
	if (_pno_state->pno_mode & DHD_PNO_LEGACY_MODE &&
		!(_pno_state->pno_mode & DHD_PNO_GSCAN_MODE)) {
#else
		if (_pno_state->pno_mode & DHD_PNO_LEGACY_MODE) {
#endif /* GSCAN_SUPPORT */
		DHD_ERROR(("%s : Legacy PNO mode was already started, "
			"will disable previous one to start new one\n", __FUNCTION__));
		err = dhd_pno_stop_for_ssid(dhd);
		if (err < 0) {
			DHD_ERROR(("%s : failed to stop legacy PNO (err %d)\n",
				__FUNCTION__, err));
			goto exit_no_clear;
		}
	}
	_pno_state->pno_mode |= DHD_PNO_LEGACY_MODE;
	err = _dhd_pno_reinitialize_prof(dhd, _params, DHD_PNO_LEGACY_MODE);
	if (err < 0) {
		DHD_ERROR(("%s : failed to reinitialize profile (err %d)\n",
			__FUNCTION__, err));
		goto exit_no_clear;
	}
	memset(_chan_list, 0, sizeof(_chan_list));
	tot_nchan = MIN(nchan, WL_NUMCHANNELS);
	if (tot_nchan > 0 && channel_list) {
		for (i = 0; i < tot_nchan; i++)
		_params->params_legacy.chan_list[i] = _chan_list[i] = channel_list[i];
	}
#ifdef GSCAN_SUPPORT
	else {
		tot_nchan = WL_NUMCHANNELS;
		err = _dhd_pno_get_channels(dhd, _chan_list, &tot_nchan,
				(WLC_BAND_2G | WLC_BAND_5G), FALSE);
		if (err < 0) {
			tot_nchan = 0;
			DHD_PNO(("Could not get channel list for PNO SSID\n"));
		} else {
			for (i = 0; i < tot_nchan; i++)
			_params->params_legacy.chan_list[i] = _chan_list[i];
		}
	}
#endif /* GSCAN_SUPPORT */

	if (_pno_state->pno_mode & (DHD_PNO_BATCH_MODE | DHD_PNO_HOTLIST_MODE)) {
		DHD_PNO(("BATCH SCAN is on progress in firmware\n"));
		/* retrieve the batching data from firmware into host */
		dhd_pno_get_for_batch(dhd, NULL, 0, PNO_STATUS_DISABLE);
		/* store current pno_mode before disabling pno */
		mode = _pno_state->pno_mode;
		err = _dhd_pno_enable(dhd, PNO_OFF);
		if (err < 0) {
			DHD_ERROR(("%s : failed to disable PNO\n", __FUNCTION__));
			goto exit_no_clear;
		}
		/* restore the previous mode */
		_pno_state->pno_mode = mode;
		/* use superset of channel list between two mode */
		if (_pno_state->pno_mode & DHD_PNO_BATCH_MODE) {
			_params2 = &(_pno_state->pno_params_arr[INDEX_OF_BATCH_PARAMS]);
			if (_params2->params_batch.nchan > 0 && tot_nchan > 0) {
				err = _dhd_pno_chan_merge(_chan_list, &tot_nchan,
					&_params2->params_batch.chan_list[0],
					_params2->params_batch.nchan,
					&channel_list[0], tot_nchan);
				if (err < 0) {
					DHD_ERROR(("%s : failed to merge channel list"
					" between legacy and batch\n",
						__FUNCTION__));
					goto exit_no_clear;
				}
			}  else {
				DHD_PNO(("superset channel will use"
				" all channels in firmware\n"));
			}
		} else if (_pno_state->pno_mode & DHD_PNO_HOTLIST_MODE) {
			_params2 = &(_pno_state->pno_params_arr[INDEX_OF_HOTLIST_PARAMS]);
			if (_params2->params_hotlist.nchan > 0 && tot_nchan > 0) {
				err = _dhd_pno_chan_merge(_chan_list, &tot_nchan,
					&_params2->params_hotlist.chan_list[0],
					_params2->params_hotlist.nchan,
					&channel_list[0], tot_nchan);
				if (err < 0) {
					DHD_ERROR(("%s : failed to merge channel list"
					" between legacy and hotlist\n",
						__FUNCTION__));
					goto exit_no_clear;
				}
			}
		}
	}
	_params->params_legacy.scan_fr = scan_fr;
	_params->params_legacy.pno_repeat = pno_repeat;
	_params->params_legacy.pno_freq_expo_max = pno_freq_expo_max;
	_params->params_legacy.nchan = tot_nchan;
	_params->params_legacy.nssid = 0;
	INIT_LIST_HEAD(&_params->params_legacy.ssid_list);
#ifdef GSCAN_SUPPORT
	/* dhd_pno_initiate_gscan_request will handle simultaneous Legacy PNO and GSCAN */
	if (_pno_state->pno_mode & DHD_PNO_GSCAN_MODE) {
		if (dhd_pno_add_to_ssid_list(_params, ssid_list, nssid) < 0) {
			err = BCME_ERROR;
			goto exit;
		}
		DHD_PNO(("GSCAN mode is ON! Will restart GSCAN+Legacy PNO\n"));
		err = dhd_pno_initiate_gscan_request(dhd, 1, 0);
		goto exit;
	}
#endif /* GSCAN_SUPPORT */
	if ((err = _dhd_pno_set(dhd, _params, DHD_PNO_LEGACY_MODE)) < 0) {
		DHD_ERROR(("failed to set call pno_set (err %d) in firmware\n", err));
		goto exit;
	}
	if ((err = _dhd_pno_add_ssid(dhd, ssid_list, nssid)) < 0) {
		DHD_ERROR(("failed to add ssid list(err %d), %d in firmware\n", err, nssid));
		goto exit;
	}
	if (dhd_pno_add_to_ssid_list(_params, ssid_list, nssid) < 0) {
		err = BCME_ERROR;
		goto exit;
	}
	if (tot_nchan > 0) {
		if ((err = _dhd_pno_cfg(dhd, _chan_list, tot_nchan)) < 0) {
			DHD_ERROR(("%s : failed to set call pno_cfg (err %d) in firmware\n",
				__FUNCTION__, err));
			goto exit;
		}
	}
	if (_pno_state->pno_status == DHD_PNO_DISABLED) {
		if ((err = _dhd_pno_enable(dhd, PNO_ON)) < 0)
			DHD_ERROR(("%s : failed to enable PNO\n", __FUNCTION__));
	}
exit:
	if (err < 0) {
		_dhd_pno_reinitialize_prof(dhd, _params, DHD_PNO_LEGACY_MODE);
	}
exit_no_clear:
	/* clear mode in case of error */
	if (err < 0) {
		int ret = dhd_pno_clean(dhd);

		if (ret < 0) {
			DHD_ERROR(("%s : failed to call dhd_pno_clean (err: %d)\n",
				__FUNCTION__, ret));
		} else {
			_pno_state->pno_mode &= ~DHD_PNO_LEGACY_MODE;
		}
	}
	return err;
}
int
dhd_pno_set_for_batch(dhd_pub_t *dhd, struct dhd_pno_batch_params *batch_params)
{
	int err = BCME_OK;
	uint16 _chan_list[WL_NUMCHANNELS];
	int rem_nchan = 0, tot_nchan = 0;
	int mode = 0, mscan = 0;
	dhd_pno_params_t *_params;
	dhd_pno_params_t *_params2;
	dhd_pno_status_info_t *_pno_state;
	wlc_ssid_ext_t *p_ssid_list = NULL;
	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	NULL_CHECK(batch_params, "batch_params is NULL", err);
	_pno_state = PNO_GET_PNOSTATE(dhd);
	DHD_PNO(("%s enter\n", __FUNCTION__));
	if (!dhd_support_sta_mode(dhd)) {
		err = BCME_BADOPTION;
		goto exit;
	}
	if (!WLS_SUPPORTED(_pno_state)) {
		DHD_ERROR(("%s : wifi location service is not supported\n", __FUNCTION__));
		err = BCME_UNSUPPORTED;
		goto exit;
	}
	_params = &_pno_state->pno_params_arr[INDEX_OF_BATCH_PARAMS];
	if (!(_pno_state->pno_mode & DHD_PNO_BATCH_MODE)) {
		_pno_state->pno_mode |= DHD_PNO_BATCH_MODE;
		err = _dhd_pno_reinitialize_prof(dhd, _params, DHD_PNO_BATCH_MODE);
		if (err < 0) {
			DHD_ERROR(("%s : failed to call _dhd_pno_reinitialize_prof\n",
				__FUNCTION__));
			goto exit;
		}
	} else {
		/* batch mode is already started */
		return -EBUSY;
	}
	_params->params_batch.scan_fr = batch_params->scan_fr;
	_params->params_batch.bestn = batch_params->bestn;
	_params->params_batch.mscan = (batch_params->mscan)?
		batch_params->mscan : DEFAULT_BATCH_MSCAN;
	_params->params_batch.nchan = batch_params->nchan;
	memcpy(_params->params_batch.chan_list, batch_params->chan_list,
		sizeof(_params->params_batch.chan_list));

	memset(_chan_list, 0, sizeof(_chan_list));

	rem_nchan = ARRAYSIZE(batch_params->chan_list) - batch_params->nchan;
	if (batch_params->band == WLC_BAND_2G || batch_params->band == WLC_BAND_5G) {
		/* get a valid channel list based on band B or A */
		err = _dhd_pno_get_channels(dhd,
		&_params->params_batch.chan_list[batch_params->nchan],
		&rem_nchan, batch_params->band, FALSE);
		if (err < 0) {
			DHD_ERROR(("%s: failed to get valid channel list(band : %d)\n",
				__FUNCTION__, batch_params->band));
			goto exit;
		}
		/* now we need to update nchan because rem_chan has valid channel count */
		_params->params_batch.nchan += rem_nchan;
		/* need to sort channel list */
		sort(_params->params_batch.chan_list, _params->params_batch.nchan,
			sizeof(_params->params_batch.chan_list[0]), _dhd_pno_cmpfunc, NULL);
	}
#ifdef PNO_DEBUG
{
		DHD_PNO(("Channel list : "));
		for (i = 0; i < _params->params_batch.nchan; i++) {
			DHD_PNO(("%d ", _params->params_batch.chan_list[i]));
		}
		DHD_PNO(("\n"));
}
#endif
	if (_params->params_batch.nchan) {
		/* copy the channel list into local array */
		memcpy(_chan_list, _params->params_batch.chan_list, sizeof(_chan_list));
		tot_nchan = _params->params_batch.nchan;
	}
	if (_pno_state->pno_mode & DHD_PNO_LEGACY_MODE) {
		DHD_PNO(("PNO SSID is on progress in firmware\n"));
		/* store current pno_mode before disabling pno */
		mode = _pno_state->pno_mode;
		err = _dhd_pno_enable(dhd, PNO_OFF);
		if (err < 0) {
			DHD_ERROR(("%s : failed to disable PNO\n", __FUNCTION__));
			goto exit;
		}
		/* restore the previous mode */
		_pno_state->pno_mode = mode;
		/* Use the superset for channelist between two mode */
		_params2 = &(_pno_state->pno_params_arr[INDEX_OF_LEGACY_PARAMS]);
		if (_params2->params_legacy.nchan > 0 && _params->params_batch.nchan > 0) {
			err = _dhd_pno_chan_merge(_chan_list, &tot_nchan,
				&_params2->params_legacy.chan_list[0],
				_params2->params_legacy.nchan,
				&_params->params_batch.chan_list[0], _params->params_batch.nchan);
			if (err < 0) {
				DHD_ERROR(("%s : failed to merge channel list"
				" between legacy and batch\n",
					__FUNCTION__));
				goto exit;
			}
		} else {
			DHD_PNO(("superset channel will use all channels in firmware\n"));
		}
		p_ssid_list = dhd_pno_get_legacy_pno_ssid(dhd, _pno_state);
		if (!p_ssid_list) {
			err = BCME_NOMEM;
			DHD_ERROR(("failed to get Legacy PNO SSID list\n"));
			goto exit;
		}
		if ((err = _dhd_pno_add_ssid(dhd, p_ssid_list,
			_params2->params_legacy.nssid)) < 0) {
			DHD_ERROR(("failed to add ssid list (err %d) in firmware\n", err));
			goto exit;
		}
	}
	if ((err = _dhd_pno_set(dhd, _params, DHD_PNO_BATCH_MODE)) < 0) {
		DHD_ERROR(("%s : failed to set call pno_set (err %d) in firmware\n",
			__FUNCTION__, err));
		goto exit;
	} else {
		/* we need to return mscan */
		mscan = err;
	}
	if (tot_nchan > 0) {
		if ((err = _dhd_pno_cfg(dhd, _chan_list, tot_nchan)) < 0) {
			DHD_ERROR(("%s : failed to set call pno_cfg (err %d) in firmware\n",
				__FUNCTION__, err));
			goto exit;
		}
	}
	if (_pno_state->pno_status == DHD_PNO_DISABLED) {
		if ((err = _dhd_pno_enable(dhd, PNO_ON)) < 0)
			DHD_ERROR(("%s : failed to enable PNO\n", __FUNCTION__));
	}
exit:
	/* clear mode in case of error */
	if (err < 0)
		_pno_state->pno_mode &= ~DHD_PNO_BATCH_MODE;
	else {
		/* return #max scan firmware can do */
		err = mscan;
	}
	if (p_ssid_list)
		kfree(p_ssid_list);
	return err;
}


#ifdef GSCAN_SUPPORT
static void
dhd_pno_reset_cfg_gscan(dhd_pno_params_t *_params,
            dhd_pno_status_info_t *_pno_state, uint8 flags)
{
	DHD_ERROR(("%s enter\n", __FUNCTION__));

	if (flags & GSCAN_FLUSH_SCAN_CFG) {
		_params->params_gscan.bestn = 0;
		_params->params_gscan.mscan = 0;
		_params->params_gscan.buffer_threshold = GSCAN_BATCH_NO_THR_SET;
		_params->params_gscan.scan_fr = 0;
		_params->params_gscan.send_all_results_flag = 0;
		memset(_params->params_gscan.channel_bucket, 0,
		_params->params_gscan.nchannel_buckets *
		 sizeof(struct dhd_pno_gscan_channel_bucket));
		_params->params_gscan.nchannel_buckets = 0;
		DHD_PNO(("Flush Scan config\n"));
	}
	if (flags & GSCAN_FLUSH_HOTLIST_CFG) {
		struct dhd_pno_bssid *iter, *next;
		if (_params->params_gscan.nbssid_hotlist > 0) {
			list_for_each_entry_safe(iter, next,
				&_params->params_gscan.hotlist_bssid_list, list) {
				list_del(&iter->list);
				kfree(iter);
			}
		}
		_params->params_gscan.nbssid_hotlist = 0;
		DHD_PNO(("Flush Hotlist Config\n"));
	}
	if (flags & GSCAN_FLUSH_SIGNIFICANT_CFG) {
		dhd_pno_significant_bssid_t *iter, *next;

		if (_params->params_gscan.nbssid_significant_change > 0) {
			list_for_each_entry_safe(iter, next,
				&_params->params_gscan.significant_bssid_list, list) {
				list_del(&iter->list);
				kfree(iter);
			}
		}
		_params->params_gscan.nbssid_significant_change = 0;
		DHD_PNO(("Flush Significant Change Config\n"));
	}
	if (flags & GSCAN_FLUSH_EPNO_CFG) {
		dhd_epno_params_t *iter, *next;

		if (_params->params_gscan.num_epno_ssid > 0) {
			list_for_each_entry_safe(iter, next,
				&_params->params_gscan.epno_ssid_list, list) {
				list_del(&iter->list);
				kfree(iter);
			}
		}
		_params->params_gscan.num_epno_ssid = 0;
		_params->params_gscan.num_visible_epno_ssid = 0;
		_params->params_gscan.ssid_ext_last_used_index = 0;
		DHD_PNO(("Flushed ePNO Config\n"));
	}

	return;
}

int
dhd_pno_lock_batch_results(dhd_pub_t *dhd)
{
	dhd_pno_status_info_t *_pno_state;
	int err = BCME_OK;

	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	_pno_state = PNO_GET_PNOSTATE(dhd);
	mutex_lock(&_pno_state->pno_mutex);
	return err;
}

void
dhd_pno_unlock_batch_results(dhd_pub_t *dhd)
{
	dhd_pno_status_info_t *_pno_state;
	_pno_state = PNO_GET_PNOSTATE(dhd);
	mutex_unlock(&_pno_state->pno_mutex);
	return;
}

int
dhd_wait_batch_results_complete(dhd_pub_t *dhd)
{
	dhd_pno_status_info_t *_pno_state;
	dhd_pno_params_t *_params;
	int err = BCME_OK;

	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	_pno_state = PNO_GET_PNOSTATE(dhd);
	_params = &_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS];

	/* Has the workqueue finished its job already?? */
	if (_params->params_gscan.get_batch_flag == GSCAN_BATCH_RETRIEVAL_IN_PROGRESS) {
		DHD_PNO(("%s: Waiting to complete retrieval..\n", __FUNCTION__));
		wait_event_interruptible_timeout(_pno_state->batch_get_wait,
		     is_batch_retrieval_complete(&_params->params_gscan),
		     msecs_to_jiffies(GSCAN_BATCH_GET_MAX_WAIT));
	} else { /* GSCAN_BATCH_RETRIEVAL_COMPLETE */
		gscan_results_cache_t *iter;
		uint16 num_results = 0;

		mutex_lock(&_pno_state->pno_mutex);
		iter = _params->params_gscan.gscan_batch_cache;
		while (iter) {
			num_results += iter->tot_count - iter->tot_consumed;
			iter = iter->next;
		}
		mutex_unlock(&_pno_state->pno_mutex);

		/* All results consumed/No results cached??
		 * Get fresh results from FW
		 */
		if ((_pno_state->pno_mode & DHD_PNO_GSCAN_MODE) && !num_results) {
			DHD_PNO(("%s: No results cached, getting from FW..\n", __FUNCTION__));
			err = dhd_retreive_batch_scan_results(dhd);
			if (err == BCME_OK) {
				wait_event_interruptible_timeout(_pno_state->batch_get_wait,
				  is_batch_retrieval_complete(&_params->params_gscan),
				  msecs_to_jiffies(GSCAN_BATCH_GET_MAX_WAIT));
			}
		}
	}
	DHD_PNO(("%s: Wait complete\n", __FUNCTION__));
	return err;
}

static void *
dhd_get_gscan_batch_results(dhd_pub_t *dhd, uint32 *len)
{
	gscan_results_cache_t *iter, *results;
	dhd_pno_status_info_t *_pno_state;
	dhd_pno_params_t *_params;
	uint16 num_scan_ids = 0, num_results = 0;

	_pno_state = PNO_GET_PNOSTATE(dhd);
	_params = &_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS];

	iter = results = _params->params_gscan.gscan_batch_cache;
	while (iter) {
		num_results += iter->tot_count - iter->tot_consumed;
		num_scan_ids++;
		iter = iter->next;
	}

	*len = ((num_results << 16) | (num_scan_ids));
	return results;
}

void *
dhd_pno_get_gscan(dhd_pub_t *dhd, dhd_pno_gscan_cmd_cfg_t type,
         void *info, uint32 *len)
{
	void *ret = NULL;
	dhd_pno_gscan_capabilities_t *ptr;
	dhd_epno_params_t *epno_params;
	dhd_pno_params_t *_params;
	dhd_pno_status_info_t *_pno_state;

	if (!dhd || !dhd->pno_state) {
		DHD_ERROR(("NULL POINTER : %s\n", __FUNCTION__));
		return NULL;
	}
	_pno_state = PNO_GET_PNOSTATE(dhd);
	_params = &_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS];
	if (!len) {
		DHD_ERROR(("%s: len is NULL\n", __FUNCTION__));
		return NULL;
	}
	switch (type) {
		case DHD_PNO_GET_CAPABILITIES:
			ptr = (dhd_pno_gscan_capabilities_t *)
			kmalloc(sizeof(dhd_pno_gscan_capabilities_t), GFP_KERNEL);
			if (!ptr)
				break;
			/* Hardcoding these values for now, need to get
			 * these values from FW, will change in a later check-in
			 */
			ptr->max_scan_cache_size = GSCAN_MAX_AP_CACHE;
			ptr->max_scan_buckets = GSCAN_MAX_CH_BUCKETS;
			ptr->max_ap_cache_per_scan = GSCAN_MAX_AP_CACHE_PER_SCAN;
			ptr->max_rssi_sample_size = PFN_SWC_RSSI_WINDOW_MAX;
			ptr->max_scan_reporting_threshold = 100;
			ptr->max_hotlist_aps = PFN_HOTLIST_MAX_NUM_APS;
			ptr->max_significant_wifi_change_aps = PFN_SWC_MAX_NUM_APS;
			ptr->max_epno_ssid_crc32 = MAX_EPNO_SSID_NUM;
			ptr->max_epno_hidden_ssid = MAX_EPNO_HIDDEN_SSID;
			ptr->max_white_list_ssid = MAX_WHITELIST_SSID;
			ret = (void *)ptr;
			*len = sizeof(dhd_pno_gscan_capabilities_t);
			break;

		case DHD_PNO_GET_BATCH_RESULTS:
			ret = dhd_get_gscan_batch_results(dhd, len);
			break;
		case DHD_PNO_GET_CHANNEL_LIST:
			if (info) {
				uint16 ch_list[WL_NUMCHANNELS];
				uint32 *ptr, mem_needed, i;
				int32 err, nchan = WL_NUMCHANNELS;
				uint32 *gscan_band = (uint32 *) info;
				uint8 band = 0;

				/* No band specified?, nothing to do */
				if ((*gscan_band & GSCAN_BAND_MASK) == 0) {
					DHD_PNO(("No band specified\n"));
					*len = 0;
					break;
				}

				/* HAL and DHD use different bits for 2.4G and
				 * 5G in bitmap. Hence translating it here...
				 */
				if (*gscan_band & GSCAN_BG_BAND_MASK) {
					band |= WLC_BAND_2G;
				}
				if (*gscan_band & GSCAN_A_BAND_MASK) {
					band |= WLC_BAND_5G;
				}

				err = _dhd_pno_get_channels(dhd, ch_list, &nchan,
				                          (band & GSCAN_ABG_BAND_MASK),
				                          !(*gscan_band & GSCAN_DFS_MASK));

				if (err < 0) {
					DHD_ERROR(("%s: failed to get valid channel list\n",
						__FUNCTION__));
					*len = 0;
				} else {
					mem_needed = sizeof(uint32) * nchan;
					ptr = (uint32 *) kmalloc(mem_needed, GFP_KERNEL);
					if (!ptr) {
						DHD_ERROR(("%s: Unable to malloc %d bytes\n",
							__FUNCTION__, mem_needed));
						break;
					}
					for (i = 0; i < nchan; i++) {
						ptr[i] = wf_channel2mhz(ch_list[i],
							(ch_list[i] <= CH_MAX_2G_CHANNEL?
							WF_CHAN_FACTOR_2_4_G : WF_CHAN_FACTOR_5_G));
					}
					ret = ptr;
					*len = mem_needed;
				}
			} else {
				*len = 0;
				DHD_ERROR(("%s: info buffer is NULL\n", __FUNCTION__));
			}
			break;
		case DHD_PNO_GET_EPNO_SSID_ELEM:
			if (_params->params_gscan.num_epno_ssid >=
			   (MAX_EPNO_SSID_NUM + MAX_EPNO_HIDDEN_SSID)) {
				DHD_ERROR(("Excessive number of ePNO SSIDs programmed %d\n",
				     _params->params_gscan.num_epno_ssid));
				return NULL;
			}

			if (!_params->params_gscan.num_epno_ssid)
				INIT_LIST_HEAD(&_params->params_gscan.epno_ssid_list);

			epno_params = kzalloc(sizeof(dhd_epno_params_t), GFP_KERNEL);
			if (!epno_params) {
				DHD_ERROR(("EPNO ssid: cannot alloc %zd bytes",
				sizeof(dhd_epno_params_t)));
				return NULL;
			}
			_params->params_gscan.num_epno_ssid++;
			epno_params->index = DHD_EPNO_DEFAULT_INDEX;
			list_add_tail(&epno_params->list, &_params->params_gscan.epno_ssid_list);
			ret = epno_params;
			break;
		default:
			DHD_ERROR(("%s: Unrecognized cmd type - %d\n", __FUNCTION__, type));
			break;
	}

	return ret;

}

int
dhd_pno_set_cfg_gscan(dhd_pub_t *dhd, dhd_pno_gscan_cmd_cfg_t type,
    void *buf, uint8 flush)
{
	int err = BCME_OK;
	dhd_pno_params_t *_params;
	int i;
	dhd_pno_status_info_t *_pno_state;

	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);

	DHD_PNO(("%s enter\n", __FUNCTION__));

	_pno_state = PNO_GET_PNOSTATE(dhd);
	_params = &_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS];
	mutex_lock(&_pno_state->pno_mutex);

	switch (type) {
		case DHD_PNO_BATCH_SCAN_CFG_ID:
		{
			gscan_batch_params_t *ptr = (gscan_batch_params_t *)buf;
			_params->params_gscan.bestn = ptr->bestn;
			_params->params_gscan.mscan = ptr->mscan;
			_params->params_gscan.buffer_threshold = ptr->buffer_threshold;
			break;
		}
		case DHD_PNO_GEOFENCE_SCAN_CFG_ID:
		{
			gscan_hotlist_scan_params_t *ptr = (gscan_hotlist_scan_params_t *)buf;
			struct dhd_pno_bssid *_pno_bssid;
			struct bssid_t *bssid_ptr;
			int8 flags;
			if (flush) {
				dhd_pno_reset_cfg_gscan(_params, _pno_state,
				    GSCAN_FLUSH_HOTLIST_CFG);
			}

			if (!ptr->nbssid) {
				break;
			}
			if (!_params->params_gscan.nbssid_hotlist) {
				INIT_LIST_HEAD(&_params->params_gscan.hotlist_bssid_list);
			}
			if ((_params->params_gscan.nbssid_hotlist +
			          ptr->nbssid) > PFN_SWC_MAX_NUM_APS) {
				DHD_ERROR(("Excessive number of hotlist APs programmed %d\n",
				     (_params->params_gscan.nbssid_hotlist +
				      ptr->nbssid)));
				err = BCME_RANGE;
				goto exit;
			}

			for (i = 0, bssid_ptr = ptr->bssid; i < ptr->nbssid; i++, bssid_ptr++) {
				_pno_bssid = kzalloc(sizeof(struct dhd_pno_bssid), GFP_KERNEL);

				if (!_pno_bssid) {
					DHD_ERROR(("_pno_bssid is NULL, cannot kalloc %zd bytes",
					       sizeof(struct dhd_pno_bssid)));
					err = BCME_NOMEM;
					goto exit;
				}
				memcpy(&_pno_bssid->macaddr, &bssid_ptr->macaddr, ETHER_ADDR_LEN);

				flags = (int8) bssid_ptr->rssi_reporting_threshold;
				_pno_bssid->flags = flags  << WL_PFN_RSSI_SHIFT;
				list_add_tail(&_pno_bssid->list,
				   &_params->params_gscan.hotlist_bssid_list);
			}

			_params->params_gscan.nbssid_hotlist += ptr->nbssid;
			_params->params_gscan.lost_ap_window = ptr->lost_ap_window;
			break;
		}
		case DHD_PNO_SIGNIFICANT_SCAN_CFG_ID:
		{
			gscan_swc_params_t *ptr = (gscan_swc_params_t *)buf;
			dhd_pno_significant_bssid_t *_pno_significant_change_bssid;
			wl_pfn_significant_bssid_t *significant_bssid_ptr;

			if (flush) {
				dhd_pno_reset_cfg_gscan(_params, _pno_state,
				   GSCAN_FLUSH_SIGNIFICANT_CFG);
			}

			if (!ptr->nbssid) {
				break;
			}
			if (!_params->params_gscan.nbssid_significant_change) {
				INIT_LIST_HEAD(&_params->params_gscan.significant_bssid_list);
			}
			if ((_params->params_gscan.nbssid_significant_change +
			          ptr->nbssid) > PFN_SWC_MAX_NUM_APS) {
				DHD_ERROR(("Excessive number of SWC APs programmed %d\n",
				     (_params->params_gscan.nbssid_significant_change +
				      ptr->nbssid)));
				err = BCME_RANGE;
				goto exit;
			}

			for (i = 0, significant_bssid_ptr = ptr->bssid_elem_list;
			     i < ptr->nbssid; i++, significant_bssid_ptr++) {
				_pno_significant_change_bssid =
				      kzalloc(sizeof(dhd_pno_significant_bssid_t),
				      GFP_KERNEL);

				if (!_pno_significant_change_bssid) {
					DHD_ERROR(("SWC bssidptr is NULL, cannot kalloc %zd bytes",
					sizeof(dhd_pno_significant_bssid_t)));
					err = BCME_NOMEM;
					goto exit;
				}
				memcpy(&_pno_significant_change_bssid->BSSID,
				    &significant_bssid_ptr->macaddr, ETHER_ADDR_LEN);
				_pno_significant_change_bssid->rssi_low_threshold =
				    significant_bssid_ptr->rssi_low_threshold;
				_pno_significant_change_bssid->rssi_high_threshold =
				    significant_bssid_ptr->rssi_high_threshold;
				list_add_tail(&_pno_significant_change_bssid->list,
				    &_params->params_gscan.significant_bssid_list);
			}

			_params->params_gscan.swc_nbssid_threshold = ptr->swc_threshold;
			_params->params_gscan.swc_rssi_window_size = ptr->rssi_window;
			_params->params_gscan.lost_ap_window = ptr->lost_ap_window;
			_params->params_gscan.nbssid_significant_change += ptr->nbssid;
			break;
		}
		case DHD_PNO_SCAN_CFG_ID:
		{
			int i, k;
			uint16 band;
			gscan_scan_params_t *ptr = (gscan_scan_params_t *)buf;
			struct dhd_pno_gscan_channel_bucket *ch_bucket;

			if (ptr->nchannel_buckets <= GSCAN_MAX_CH_BUCKETS) {
				_params->params_gscan.nchannel_buckets = ptr->nchannel_buckets;

				memcpy(_params->params_gscan.channel_bucket, ptr->channel_bucket,
				    _params->params_gscan.nchannel_buckets *
				    sizeof(struct dhd_pno_gscan_channel_bucket));
				ch_bucket = _params->params_gscan.channel_bucket;

				for (i = 0; i < ptr->nchannel_buckets; i++) {
					band = ch_bucket[i].band;
					for (k = 0; k < ptr->channel_bucket[i].num_channels; k++)  {
						ch_bucket[i].chan_list[k] =
						wf_mhz2channel(ptr->channel_bucket[i].chan_list[k],
							0);
					}
					ch_bucket[i].band = 0;
					/* HAL and DHD use different bits for 2.4G and
					 * 5G in bitmap. Hence translating it here...
					 */
					if (band & GSCAN_BG_BAND_MASK)
						ch_bucket[i].band |= WLC_BAND_2G;
					if (band & GSCAN_A_BAND_MASK)
						ch_bucket[i].band |= WLC_BAND_5G;
					if (band & GSCAN_DFS_MASK)
						ch_bucket[i].band |= GSCAN_DFS_MASK;

					DHD_PNO(("band %d report_flag %d\n", ch_bucket[i].band,
					          ch_bucket[i].report_flag));
				}

				for (i = 0; i < ptr->nchannel_buckets; i++) {
					ch_bucket[i].bucket_freq_multiple =
					ch_bucket[i].bucket_freq_multiple/ptr->scan_fr;
					ch_bucket[i].bucket_max_multiple =
					ch_bucket[i].bucket_max_multiple/ptr->scan_fr;
					DHD_PNO(("mult %d max_mult %d\n",
						ch_bucket[i].bucket_freq_multiple,
						ch_bucket[i].bucket_max_multiple));
				}
				_params->params_gscan.scan_fr = ptr->scan_fr;

				DHD_PNO(("num_buckets %d scan_fr %d\n", ptr->nchannel_buckets,
				        _params->params_gscan.scan_fr));
			} else {
				err = BCME_BADARG;
			}
			break;
		}
	case DHD_PNO_EPNO_CFG_ID:
		if (flush) {
			dhd_pno_reset_cfg_gscan(_params, _pno_state,
			   GSCAN_FLUSH_EPNO_CFG);
		} else {
			_params->params_gscan.num_visible_epno_ssid += *((uint16 *)buf);
		}
		break;
		default:
			err = BCME_BADARG;
			DHD_ERROR(("%s: Unrecognized cmd type - %d\n", __FUNCTION__, type));
			break;
	}
exit:
	mutex_unlock(&_pno_state->pno_mutex);
	return err;

}


static bool
validate_gscan_params(struct dhd_pno_gscan_params *gscan_params)
{
	unsigned int i, k;

	if (!gscan_params->scan_fr || !gscan_params->nchannel_buckets) {
		DHD_ERROR(("%s : Scan freq - %d or number of channel buckets - %d is empty\n",
		 __FUNCTION__, gscan_params->scan_fr, gscan_params->nchannel_buckets));
		return false;
	}

	for (i = 0; i < gscan_params->nchannel_buckets; i++) {
		if (!gscan_params->channel_bucket[i].band) {
			for (k = 0; k < gscan_params->channel_bucket[i].num_channels; k++) {
				if (gscan_params->channel_bucket[i].chan_list[k] > CHANNEL_5G_MAX) {
					DHD_ERROR(("%s : Unknown channel %d\n", __FUNCTION__,
					 gscan_params->channel_bucket[i].chan_list[k]));
					return false;
				}
			}
		}
	}

	return true;
}

static int
dhd_pno_set_for_gscan(dhd_pub_t *dhd, struct dhd_pno_gscan_params *gscan_params)
{
	int err = BCME_OK;
	int mode, i = 0, k;
	uint16 _chan_list[WL_NUMCHANNELS];
	int tot_nchan = 0;
	int num_buckets_to_fw, tot_num_buckets, gscan_param_size = 0;
	dhd_pno_status_info_t *_pno_state = PNO_GET_PNOSTATE(dhd);
	wl_pfn_gscan_ch_bucket_cfg_t *ch_bucket = NULL;
	wl_pfn_gscan_cfg_t *pfn_gscan_cfg_t = NULL;
	wl_pfn_significant_bssid_t *p_pfn_significant_bssid = NULL;
	wl_pfn_bssid_t *p_pfn_bssid = NULL;
	wlc_ssid_ext_t *pssid_list = NULL;
	dhd_pno_params_t	*params_legacy;
	dhd_pno_params_t	*_params;

	params_legacy = &_pno_state->pno_params_arr[INDEX_OF_LEGACY_PARAMS];
	_params = &_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS];

	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	NULL_CHECK(gscan_params, "gscan_params is NULL", err);

	DHD_PNO(("%s enter\n", __FUNCTION__));

	if (!dhd_support_sta_mode(dhd)) {
		err = BCME_BADOPTION;
		goto exit;
	}
	if (!WLS_SUPPORTED(_pno_state)) {
		DHD_ERROR(("%s : wifi location service is not supported\n", __FUNCTION__));
		err = BCME_UNSUPPORTED;
		goto exit;
	}

	if (!validate_gscan_params(gscan_params)) {
		DHD_ERROR(("%s : Cannot start gscan - bad params\n", __FUNCTION__));
		err = BCME_BADARG;
		goto exit;
	}
	/* Create channel list based on channel buckets */
	if (!(ch_bucket = dhd_pno_gscan_create_channel_list(dhd, _pno_state,
	    _chan_list, &tot_num_buckets, &num_buckets_to_fw))) {
		goto exit;
	}

	mutex_lock(&_pno_state->pno_mutex);
	/* Clear any pre-existing results in our cache
	 * not consumed by framework
	 */
	dhd_gscan_clear_all_batch_results(dhd);
	if (_pno_state->pno_mode & (DHD_PNO_GSCAN_MODE | DHD_PNO_LEGACY_MODE)) {
		/* store current pno_mode before disabling pno */
		mode = _pno_state->pno_mode;
		err = dhd_pno_clean(dhd);
		if (err < 0) {
			DHD_ERROR(("%s : failed to disable PNO\n", __FUNCTION__));
			mutex_unlock(&_pno_state->pno_mutex);
			goto exit;
		}
		/* restore the previous mode */
		_pno_state->pno_mode = mode;
	}
	_pno_state->pno_mode |= DHD_PNO_GSCAN_MODE;
	mutex_unlock(&_pno_state->pno_mutex);

	if (_pno_state->pno_mode & DHD_PNO_LEGACY_MODE) {
		pssid_list = dhd_pno_get_legacy_pno_ssid(dhd, _pno_state);

		if (!pssid_list) {
			err = BCME_NOMEM;
			DHD_ERROR(("failed to get Legacy PNO SSID list\n"));
			goto exit;
		}

		if ((err = _dhd_pno_add_ssid(dhd, pssid_list,
			params_legacy->params_legacy.nssid)) < 0) {
			DHD_ERROR(("failed to add ssid list (err %d) in firmware\n", err));
			goto exit;
		}
	}

	if ((err = _dhd_pno_set(dhd, _params, DHD_PNO_GSCAN_MODE)) < 0) {
		DHD_ERROR(("failed to set call pno_set (err %d) in firmware\n", err));
		goto exit;
	}

	gscan_param_size = sizeof(wl_pfn_gscan_cfg_t) +
	          (num_buckets_to_fw - 1) * sizeof(wl_pfn_gscan_ch_bucket_cfg_t);
	pfn_gscan_cfg_t = (wl_pfn_gscan_cfg_t *) MALLOC(dhd->osh, gscan_param_size);

	if (!pfn_gscan_cfg_t) {
		DHD_ERROR(("%s: failed to malloc memory of size %d\n",
		   __FUNCTION__, gscan_param_size));
		err = BCME_NOMEM;
		goto exit;
	}

	pfn_gscan_cfg_t->version = WL_GSCAN_CFG_VERSION;
	if (gscan_params->mscan) {
		pfn_gscan_cfg_t->buffer_threshold = gscan_params->buffer_threshold;
	} else {
		pfn_gscan_cfg_t->buffer_threshold = GSCAN_BATCH_NO_THR_SET;
	}
	if (gscan_params->nbssid_significant_change) {
		pfn_gscan_cfg_t->swc_nbssid_threshold = gscan_params->swc_nbssid_threshold;
		pfn_gscan_cfg_t->swc_rssi_window_size = gscan_params->swc_rssi_window_size;
		pfn_gscan_cfg_t->lost_ap_window	= gscan_params->lost_ap_window;
	} else {
		pfn_gscan_cfg_t->swc_nbssid_threshold = 0;
		pfn_gscan_cfg_t->swc_rssi_window_size = 0;
		pfn_gscan_cfg_t->lost_ap_window	= 0;
	}

	pfn_gscan_cfg_t->flags =
	         (gscan_params->send_all_results_flag & GSCAN_SEND_ALL_RESULTS_MASK);
	pfn_gscan_cfg_t->count_of_channel_buckets = num_buckets_to_fw;
	pfn_gscan_cfg_t->retry_threshold = GSCAN_RETRY_THRESHOLD;

	for (i = 0, k = 0; i < tot_num_buckets; i++) {
		if (ch_bucket[i].bucket_end_index  != CHANNEL_BUCKET_EMPTY_INDEX) {
			pfn_gscan_cfg_t->channel_bucket[k].bucket_end_index =
			           ch_bucket[i].bucket_end_index;
			pfn_gscan_cfg_t->channel_bucket[k].bucket_freq_multiple =
			           ch_bucket[i].bucket_freq_multiple;
			pfn_gscan_cfg_t->channel_bucket[k].max_freq_multiple =
			           ch_bucket[i].max_freq_multiple;
			pfn_gscan_cfg_t->channel_bucket[k].repeat =
			           ch_bucket[i].repeat;
			pfn_gscan_cfg_t->channel_bucket[k].flag =
			           ch_bucket[i].flag;
			k++;
		}
	}

	tot_nchan = pfn_gscan_cfg_t->channel_bucket[num_buckets_to_fw - 1].bucket_end_index + 1;
	DHD_PNO(("Total channel num %d total ch_buckets  %d ch_buckets_to_fw %d \n", tot_nchan,
	      tot_num_buckets, num_buckets_to_fw));

	if ((err = _dhd_pno_cfg(dhd, _chan_list, tot_nchan)) < 0) {
		DHD_ERROR(("%s : failed to set call pno_cfg (err %d) in firmware\n",
			__FUNCTION__, err));
		goto exit;
	}

	if ((err = _dhd_pno_gscan_cfg(dhd, pfn_gscan_cfg_t, gscan_param_size)) < 0) {
		DHD_ERROR(("%s : failed to set call pno_gscan_cfg (err %d) in firmware\n",
			__FUNCTION__, err));
		goto exit;
	}
	if (gscan_params->nbssid_significant_change) {
		dhd_pno_significant_bssid_t *iter, *next;

		p_pfn_significant_bssid = kzalloc(sizeof(wl_pfn_significant_bssid_t) *
		                   gscan_params->nbssid_significant_change, GFP_KERNEL);
		if (p_pfn_significant_bssid == NULL) {
			DHD_ERROR(("%s : failed to allocate memory %zd\n",
				__FUNCTION__,
				sizeof(wl_pfn_significant_bssid_t) *
				gscan_params->nbssid_significant_change));
			err = BCME_NOMEM;
			goto exit;
		}
		i = 0;
		/* convert dhd_pno_significant_bssid_t to wl_pfn_significant_bssid_t */
		list_for_each_entry_safe(iter, next, &gscan_params->significant_bssid_list, list) {
			p_pfn_significant_bssid[i].rssi_low_threshold = iter->rssi_low_threshold;
			p_pfn_significant_bssid[i].rssi_high_threshold = iter->rssi_high_threshold;
			memcpy(&p_pfn_significant_bssid[i].macaddr, &iter->BSSID, ETHER_ADDR_LEN);
			i++;
		}
		DHD_PNO(("nbssid_significant_change %d \n",
				gscan_params->nbssid_significant_change));
		err = _dhd_pno_add_significant_bssid(dhd, p_pfn_significant_bssid,
		                     gscan_params->nbssid_significant_change);
		if (err < 0) {
			DHD_ERROR(("%s : failed to call _dhd_pno_add_significant_bssid(err :%d)\n",
				__FUNCTION__, err));
			goto exit;
		}
	}

	if (gscan_params->nbssid_hotlist) {
		struct dhd_pno_bssid *iter, *next;
		wl_pfn_bssid_t *ptr;
		p_pfn_bssid = (wl_pfn_bssid_t *)kzalloc(sizeof(wl_pfn_bssid_t) *
		       gscan_params->nbssid_hotlist, GFP_KERNEL);
		if (p_pfn_bssid == NULL) {
			DHD_ERROR(("%s : failed to allocate wl_pfn_bssid_t array"
			" (count: %d)",
				__FUNCTION__, _params->params_hotlist.nbssid));
			err = BCME_NOMEM;
			_pno_state->pno_mode &= ~DHD_PNO_HOTLIST_MODE;
			goto exit;
		}
		ptr = p_pfn_bssid;
		/* convert dhd_pno_bssid to wl_pfn_bssid */
		DHD_PNO(("nhotlist %d\n", gscan_params->nbssid_hotlist));
		list_for_each_entry_safe(iter, next,
		          &gscan_params->hotlist_bssid_list, list) {
			memcpy(&ptr->macaddr,
			&iter->macaddr, ETHER_ADDR_LEN);
			ptr->flags = iter->flags;
			ptr++;
		}

		err = _dhd_pno_add_bssid(dhd, p_pfn_bssid, gscan_params->nbssid_hotlist);
		if (err < 0) {
			DHD_ERROR(("%s : failed to call _dhd_pno_add_bssid(err :%d)\n",
				__FUNCTION__, err));
			goto exit;
		}
	}

	if (gscan_params->num_epno_ssid > 0) {
		DHD_ERROR(("num_epno_ssid %d\n", gscan_params->num_epno_ssid));
		err = dhd_epno_set_ssid(dhd, _pno_state);
		if (err  < 0) {
			DHD_ERROR(("failed to add ssid list (err %d) in firmware\n", err));
			goto exit;
		}
	}

	if ((err = _dhd_pno_enable(dhd, PNO_ON)) < 0) {
		DHD_ERROR(("%s : failed to enable PNO err %d\n", __FUNCTION__, err));
	}

exit:
	/* clear mode in case of error */
	if (err < 0) {
		int ret = dhd_pno_clean(dhd);

		if (ret < 0) {
			DHD_ERROR(("%s : failed to call dhd_pno_clean (err: %d)\n",
				__FUNCTION__, ret));
		} else {
			_pno_state->pno_mode &= ~DHD_PNO_GSCAN_MODE;
		}
	}
	kfree(pssid_list);
	kfree(p_pfn_significant_bssid);
	kfree(p_pfn_bssid);
	if (pfn_gscan_cfg_t) {
		MFREE(dhd->osh, pfn_gscan_cfg_t, gscan_param_size);
	}
	if (ch_bucket) {
		MFREE(dhd->osh, ch_bucket,
		(tot_num_buckets * sizeof(wl_pfn_gscan_ch_bucket_cfg_t)));
	}
	return err;

}

static wl_pfn_gscan_ch_bucket_cfg_t *
dhd_pno_gscan_create_channel_list(dhd_pub_t *dhd,
                                  dhd_pno_status_info_t *_pno_state,
                                  uint16 *chan_list,
                                  uint32 *num_buckets,
                                  uint32 *num_buckets_to_fw)
{
	int i, num_channels, err, nchan = WL_NUMCHANNELS, ch_cnt;
	uint16 *ptr = chan_list, max;
	wl_pfn_gscan_ch_bucket_cfg_t *ch_bucket;
	dhd_pno_params_t *_params = &_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS];
	bool is_pno_legacy_running = _pno_state->pno_mode & DHD_PNO_LEGACY_MODE;
	dhd_pno_gscan_channel_bucket_t *gscan_buckets = _params->params_gscan.channel_bucket;

	if (is_pno_legacy_running)
		*num_buckets = _params->params_gscan.nchannel_buckets + 1;
	 else
		*num_buckets = _params->params_gscan.nchannel_buckets;

	*num_buckets_to_fw = 0;

	ch_bucket = (wl_pfn_gscan_ch_bucket_cfg_t *) MALLOC(dhd->osh,
	   ((*num_buckets) * sizeof(wl_pfn_gscan_ch_bucket_cfg_t)));

	if (!ch_bucket) {
		DHD_ERROR(("%s: failed to malloc memory of size %zd\n",
			__FUNCTION__, (*num_buckets) * sizeof(wl_pfn_gscan_ch_bucket_cfg_t)));
		*num_buckets_to_fw = *num_buckets = 0;
		return NULL;
	}

	max = gscan_buckets[0].bucket_freq_multiple;
	num_channels = 0;
	/* nchan is the remaining space left in chan_list buffer
	 * So any overflow list of channels is ignored
	 */
	for (i = 0; i < _params->params_gscan.nchannel_buckets && nchan; i++) {
		if (!gscan_buckets[i].band) {
			ch_cnt = MIN(gscan_buckets[i].num_channels, (uint8)nchan);
			num_channels += ch_cnt;
			memcpy(ptr, gscan_buckets[i].chan_list,
			    ch_cnt * sizeof(uint16));
			ptr = ptr + ch_cnt;
		} else {
			/* get a valid channel list based on band B or A */
			err = _dhd_pno_get_channels(dhd, ptr,
			        &nchan, (gscan_buckets[i].band & GSCAN_ABG_BAND_MASK),
			        !(gscan_buckets[i].band & GSCAN_DFS_MASK));

			if (err < 0) {
				DHD_ERROR(("%s: failed to get valid channel list(band : %d)\n",
					__FUNCTION__, gscan_buckets[i].band));
				MFREE(dhd->osh, ch_bucket,
				      ((*num_buckets) * sizeof(wl_pfn_gscan_ch_bucket_cfg_t)));
				*num_buckets_to_fw = *num_buckets = 0;
				return NULL;
			}

			num_channels += nchan;
			ptr = ptr + nchan;
		}

		ch_bucket[i].bucket_end_index = num_channels - 1;
		ch_bucket[i].bucket_freq_multiple = gscan_buckets[i].bucket_freq_multiple;
		ch_bucket[i].repeat = gscan_buckets[i].repeat;
		ch_bucket[i].max_freq_multiple = gscan_buckets[i].bucket_max_multiple;
		ch_bucket[i].flag = gscan_buckets[i].report_flag;
		/* HAL and FW interpretations are opposite for this bit */
		ch_bucket[i].flag ^= DHD_PNO_REPORT_NO_BATCH;
		if (max < gscan_buckets[i].bucket_freq_multiple)
			max = gscan_buckets[i].bucket_freq_multiple;
		nchan = WL_NUMCHANNELS - num_channels;
		*num_buckets_to_fw = *num_buckets_to_fw + 1;
		DHD_PNO(("end_idx  %d freq_mult - %d\n",
		ch_bucket[i].bucket_end_index, ch_bucket[i].bucket_freq_multiple));
	}

	_params->params_gscan.max_ch_bucket_freq = max;
	/* Legacy PNO maybe running, which means we need to create a legacy PNO bucket
	 * Get GCF of Legacy PNO and Gscan scanfreq
	 */
	if (is_pno_legacy_running) {
		dhd_pno_params_t *_params1 = &_pno_state->pno_params_arr[INDEX_OF_LEGACY_PARAMS];
		uint16 *legacy_chan_list = _params1->params_legacy.chan_list;
		uint16 common_freq;
		uint32 legacy_bucket_idx = _params->params_gscan.nchannel_buckets;
		/* If no space is left then only gscan buckets will be sent to FW */
		if (nchan) {
			common_freq = gcd(_params->params_gscan.scan_fr,
					_params1->params_legacy.scan_fr);
			max = gscan_buckets[0].bucket_freq_multiple;
			/* GSCAN buckets */
			for (i = 0; i < _params->params_gscan.nchannel_buckets; i++) {
				ch_bucket[i].bucket_freq_multiple *= _params->params_gscan.scan_fr;
				ch_bucket[i].bucket_freq_multiple /= common_freq;
				if (max < gscan_buckets[i].bucket_freq_multiple)
					max = gscan_buckets[i].bucket_freq_multiple;
			}
			/* Legacy PNO bucket */
			ch_bucket[legacy_bucket_idx].bucket_freq_multiple =
					_params1->params_legacy.scan_fr;
			ch_bucket[legacy_bucket_idx].bucket_freq_multiple /=
					common_freq;
			_params->params_gscan.max_ch_bucket_freq = MAX(max,
				ch_bucket[legacy_bucket_idx].bucket_freq_multiple);
			ch_bucket[legacy_bucket_idx].flag = CH_BUCKET_REPORT_REGULAR;
			/* Now add channels to the legacy scan bucket */
			for (i = 0; i < _params1->params_legacy.nchan && nchan; i++, nchan--) {
				ptr[i] = legacy_chan_list[i];
				num_channels++;
			}
			ch_bucket[legacy_bucket_idx].bucket_end_index = num_channels - 1;
			*num_buckets_to_fw = *num_buckets_to_fw + 1;
			DHD_PNO(("end_idx  %d freq_mult - %d\n",
					ch_bucket[legacy_bucket_idx].bucket_end_index,
					ch_bucket[legacy_bucket_idx].bucket_freq_multiple));
		}
	}
	return ch_bucket;
}

static int
dhd_pno_stop_for_gscan(dhd_pub_t *dhd)
{
	int err = BCME_OK;
	int mode;
	dhd_pno_status_info_t *_pno_state;
	wlc_ssid_ext_t *pssid_list = NULL;

	_pno_state = PNO_GET_PNOSTATE(dhd);
	DHD_PNO(("%s enter\n", __FUNCTION__));

	if (!dhd_support_sta_mode(dhd)) {
		err = BCME_BADOPTION;
		goto exit;
	}
	if (!WLS_SUPPORTED(_pno_state)) {
		DHD_ERROR(("%s : wifi location service is not supported\n",
			__FUNCTION__));
		err = BCME_UNSUPPORTED;
		goto exit;
	}

	if (!(_pno_state->pno_mode & DHD_PNO_GSCAN_MODE)) {
		DHD_ERROR(("%s : GSCAN is not enabled\n", __FUNCTION__));
		goto exit;
	}
	if (_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS].params_gscan.mscan) {
		/* retrieve the batching data from firmware into host */
		err = dhd_wait_batch_results_complete(dhd);
		if (err != BCME_OK)
			goto exit;
	}
	mutex_lock(&_pno_state->pno_mutex);
	mode = _pno_state->pno_mode & ~DHD_PNO_GSCAN_MODE;
	err = dhd_pno_clean(dhd);
	if (err < 0) {
		DHD_ERROR(("%s : failed to call dhd_pno_clean (err: %d)\n",
			__FUNCTION__, err));
		mutex_unlock(&_pno_state->pno_mutex);
		return err;
	}
	_pno_state->pno_mode = mode;
	mutex_unlock(&_pno_state->pno_mutex);
	_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS].params_gscan.ssid_ext_last_used_index = 0;

	/* Reprogram Legacy PNO if it was running */
	if (_pno_state->pno_mode & DHD_PNO_LEGACY_MODE) {
		struct dhd_pno_legacy_params *params_legacy;
		uint16 chan_list[WL_NUMCHANNELS];

		params_legacy = &(_pno_state->pno_params_arr[INDEX_OF_LEGACY_PARAMS].params_legacy);
		_pno_state->pno_mode &= ~DHD_PNO_LEGACY_MODE;
		pssid_list = dhd_pno_get_legacy_pno_ssid(dhd, _pno_state);
		if (!pssid_list) {
			err = BCME_NOMEM;
			DHD_ERROR(("failed to get Legacy PNO SSID list\n"));
			goto exit;
		}

		DHD_PNO(("Restarting Legacy PNO SSID scan...\n"));
		memcpy(chan_list, params_legacy->chan_list,
		    (params_legacy->nchan * sizeof(uint16)));
		err = dhd_pno_set_for_ssid(dhd, pssid_list, params_legacy->nssid,
			params_legacy->scan_fr, params_legacy->pno_repeat,
			params_legacy->pno_freq_expo_max, chan_list,
			params_legacy->nchan);
		if (err < 0) {
			DHD_ERROR(("%s : failed to restart legacy PNO scan(err: %d)\n",
				__FUNCTION__, err));
			goto exit;
		}

	}

exit:
	kfree(pssid_list);
	return err;
}

int
dhd_pno_initiate_gscan_request(dhd_pub_t *dhd, bool run, bool flush)
{
	int err = BCME_OK;
	dhd_pno_params_t *params;
	dhd_pno_status_info_t *_pno_state;
	struct dhd_pno_gscan_params *gscan_params;

	NULL_CHECK(dhd, "dhd is NULL\n", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	_pno_state = PNO_GET_PNOSTATE(dhd);

	DHD_PNO(("%s enter - run %d flush %d\n", __FUNCTION__, run, flush));

	params = &_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS];
	gscan_params = &params->params_gscan;

	if (run) {
		err = dhd_pno_set_for_gscan(dhd, gscan_params);
	} else {
		if (flush) {
			mutex_lock(&_pno_state->pno_mutex);
			dhd_pno_reset_cfg_gscan(params, _pno_state, GSCAN_FLUSH_ALL_CFG);
			mutex_unlock(&_pno_state->pno_mutex);
		}
		/* Need to stop all gscan */
		err = dhd_pno_stop_for_gscan(dhd);
	}

	return err;
}

int
dhd_pno_enable_full_scan_result(dhd_pub_t *dhd, bool real_time_flag)
{
	int err = BCME_OK;
	dhd_pno_params_t *params;
	dhd_pno_status_info_t *_pno_state;
	struct dhd_pno_gscan_params *gscan_params;
	uint8 old_flag;

	NULL_CHECK(dhd, "dhd is NULL\n", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	_pno_state = PNO_GET_PNOSTATE(dhd);

	DHD_PNO(("%s enter\n", __FUNCTION__));

	if (!WLS_SUPPORTED(_pno_state)) {
		DHD_ERROR(("%s : wifi location service is not supported\n", __FUNCTION__));
		err = BCME_UNSUPPORTED;
		goto exit;
	}

	params = &_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS];
	gscan_params = &params->params_gscan;

	mutex_lock(&_pno_state->pno_mutex);

	old_flag = gscan_params->send_all_results_flag;
	gscan_params->send_all_results_flag = (uint8) real_time_flag;
	if (_pno_state->pno_mode & DHD_PNO_GSCAN_MODE) {
	    if (old_flag != gscan_params->send_all_results_flag) {
			wl_pfn_gscan_cfg_t gscan_cfg;
			gscan_cfg.version = WL_GSCAN_CFG_VERSION;
			gscan_cfg.flags = (gscan_params->send_all_results_flag &
				GSCAN_SEND_ALL_RESULTS_MASK);
			gscan_cfg.flags |= GSCAN_CFG_FLAGS_ONLY_MASK;

			if ((err = _dhd_pno_gscan_cfg(dhd, &gscan_cfg,
				sizeof(wl_pfn_gscan_cfg_t))) < 0) {
				DHD_ERROR(("%s : pno_gscan_cfg failed (err %d) in firmware\n",
					__FUNCTION__, err));
				goto exit_mutex_unlock;
			}
		} else {
			DHD_PNO(("No change in flag - %d\n", old_flag));
		}
	} else {
		DHD_PNO(("Gscan not started\n"));
	}
exit_mutex_unlock:
	mutex_unlock(&_pno_state->pno_mutex);
exit:
	return err;
}

/* Cleanup any consumed results
 * Return TRUE if all results consumed, else FALSE
 */
int dhd_gscan_batch_cache_cleanup(dhd_pub_t *dhd)
{
	int ret = 0;
	dhd_pno_params_t *params;
	struct dhd_pno_gscan_params *gscan_params;
	dhd_pno_status_info_t *_pno_state;
	gscan_results_cache_t *iter, *tmp;

	_pno_state = PNO_GET_PNOSTATE(dhd);
	params = &_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS];
	gscan_params = &params->params_gscan;
	iter = gscan_params->gscan_batch_cache;

	while (iter) {
		if (iter->tot_consumed == iter->tot_count) {
			tmp = iter->next;
			kfree(iter);
			iter = tmp;
		} else
			break;
	}
	gscan_params->gscan_batch_cache = iter;
	ret = (iter == NULL);
	return ret;
}

static int
_dhd_pno_get_gscan_batch_from_fw(dhd_pub_t *dhd)
{
	int err = BCME_OK;
	uint32 timestamp = 0, ts = 0, i, j, timediff;
	dhd_pno_params_t *params;
	dhd_pno_status_info_t *_pno_state;
	wl_pfn_lnet_info_t *plnetinfo;
	struct dhd_pno_gscan_params *gscan_params;
	wl_pfn_lscanresults_t *plbestnet = NULL;
	gscan_results_cache_t *iter, *tail;
	wifi_gscan_result_t *result;
	uint8 *nAPs_per_scan = NULL;
	uint8 num_scans_in_cur_iter;
	uint16 count;

	NULL_CHECK(dhd, "dhd is NULL\n", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);

	_pno_state = PNO_GET_PNOSTATE(dhd);
	params = &_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS];
	DHD_PNO(("%s enter\n", __FUNCTION__));

	if (!WLS_SUPPORTED(_pno_state)) {
		DHD_ERROR(("%s : wifi location service is not supported\n", __FUNCTION__));
		err = BCME_UNSUPPORTED;
		goto exit;
	}
	if (!(_pno_state->pno_mode & DHD_PNO_GSCAN_MODE)) {
		DHD_ERROR(("%s: GSCAN is not enabled\n", __FUNCTION__));
		goto exit;
	}
	gscan_params = &params->params_gscan;
	nAPs_per_scan = (uint8 *) MALLOC(dhd->osh, gscan_params->mscan);

	if (!nAPs_per_scan) {
		DHD_ERROR(("%s :Out of memory!! Cant malloc %d bytes\n", __FUNCTION__,
		gscan_params->mscan));
		err = BCME_NOMEM;
		goto exit;
	}

	plbestnet = (wl_pfn_lscanresults_t *)MALLOC(dhd->osh, PNO_BESTNET_LEN);

	mutex_lock(&_pno_state->pno_mutex);

	dhd_gscan_clear_all_batch_results(dhd);

	if (!(_pno_state->pno_mode & DHD_PNO_GSCAN_MODE)) {
		DHD_ERROR(("%s : GSCAN is not enabled\n", __FUNCTION__));
		goto exit_mutex_unlock;
	}

	timediff = gscan_params->scan_fr * 1000;
	timediff = timediff >> 1;

	/* Ok, now lets start getting results from the FW */
	plbestnet->status = PFN_INCOMPLETE;
	tail = gscan_params->gscan_batch_cache;
	while (plbestnet->status != PFN_COMPLETE) {
		memset(plbestnet, 0, PNO_BESTNET_LEN);
		err = dhd_iovar(dhd, 0, "pfnlbest", NULL, 0, (char *)plbestnet, PNO_BESTNET_LEN,
			FALSE);
		if (err < 0) {
			DHD_ERROR(("%s : Cannot get all the batch results, err :%d\n",
				__FUNCTION__, err));
			goto exit_mutex_unlock;
		}
		DHD_PNO(("ver %d, status : %d, count %d\n", plbestnet->version,
			plbestnet->status, plbestnet->count));
		if (plbestnet->version != PFN_SCANRESULT_VERSION) {
			err = BCME_VERSION;
			DHD_ERROR(("bestnet version(%d) is mismatch with Driver version(%d)\n",
				plbestnet->version, PFN_SCANRESULT_VERSION));
			goto exit_mutex_unlock;
		}
		if (plbestnet->count == 0) {
			DHD_PNO(("No more batch results\n"));
			goto exit_mutex_unlock;
		}
		num_scans_in_cur_iter = 0;
		timestamp = plbestnet->netinfo[0].timestamp;
		/* find out how many scans' results did we get in this batch of FW results */
		for (i = 0, count = 0; i < plbestnet->count; i++, count++) {
			plnetinfo = &plbestnet->netinfo[i];
			/* Unlikely to happen, but just in case the results from
			 * FW doesnt make sense..... Assume its part of one single scan
			 */
			if (num_scans_in_cur_iter > gscan_params->mscan) {
				num_scans_in_cur_iter = 0;
				count = plbestnet->count;
				break;
			}
			if (TIME_DIFF_MS(timestamp, plnetinfo->timestamp) > timediff) {
				nAPs_per_scan[num_scans_in_cur_iter] = count;
				count = 0;
				num_scans_in_cur_iter++;
			}
			timestamp = plnetinfo->timestamp;
		}
		nAPs_per_scan[num_scans_in_cur_iter] = count;
		num_scans_in_cur_iter++;

		DHD_PNO(("num_scans_in_cur_iter %d\n", num_scans_in_cur_iter));
		plnetinfo = &plbestnet->netinfo[0];

		for (i = 0; i < num_scans_in_cur_iter; i++) {
			iter = (gscan_results_cache_t *)
			kmalloc(((nAPs_per_scan[i] - 1) * sizeof(wifi_gscan_result_t)) +
			              sizeof(gscan_results_cache_t), GFP_KERNEL);
			if (!iter) {
				DHD_ERROR(("%s :Out of memory!! Cant malloc %d bytes\n",
				 __FUNCTION__, gscan_params->mscan));
				err = BCME_NOMEM;
				goto exit_mutex_unlock;
			}
			/* Need this check because the new set of results from FW
			 * maybe a continuation of previous sets' scan results
			 */
			if (TIME_DIFF_MS(ts, plnetinfo->timestamp) > timediff) {
				iter->scan_id = ++gscan_params->scan_id;
			} else {
				iter->scan_id = gscan_params->scan_id;
			}
			DHD_PNO(("scan_id %d tot_count %d\n", gscan_params->scan_id,
			      nAPs_per_scan[i]));
			iter->tot_count = nAPs_per_scan[i];
			iter->tot_consumed = 0;
			iter->flag = 0;
			if (plnetinfo->flags & PFN_PARTIAL_SCAN_MASK) {
				DHD_PNO(("This scan is aborted\n"));
				iter->flag = (ENABLE << PNO_STATUS_ABORT);
			} else if (gscan_params->reason) {
				iter->flag = (ENABLE << gscan_params->reason);
			}

			if (!tail) {
				gscan_params->gscan_batch_cache = iter;
			} else {
				tail->next = iter;
			}
			tail = iter;
			iter->next = NULL;
			for (j = 0; j < nAPs_per_scan[i]; j++, plnetinfo++) {
				result = &iter->results[j];

				result->channel = wf_channel2mhz(plnetinfo->pfnsubnet.channel,
					(plnetinfo->pfnsubnet.channel <= CH_MAX_2G_CHANNEL?
					WF_CHAN_FACTOR_2_4_G : WF_CHAN_FACTOR_5_G));
				result->rssi = (int32) plnetinfo->RSSI;
				/* Info not available & not expected */
				result->beacon_period = 0;
				result->capability = 0;
				result->ie_length = 0;
				result->rtt = (uint64) plnetinfo->rtt0;
				result->rtt_sd = (uint64) plnetinfo->rtt1;
				result->ts = convert_fw_rel_time_to_systime(plnetinfo->timestamp);
				ts = plnetinfo->timestamp;
				if (plnetinfo->pfnsubnet.SSID_len > DOT11_MAX_SSID_LEN) {
					DHD_ERROR(("%s: Invalid SSID length %d\n",
					      __FUNCTION__, plnetinfo->pfnsubnet.SSID_len));
					plnetinfo->pfnsubnet.SSID_len = DOT11_MAX_SSID_LEN;
				}
				memcpy(result->ssid, plnetinfo->pfnsubnet.SSID,
					plnetinfo->pfnsubnet.SSID_len);
				result->ssid[plnetinfo->pfnsubnet.SSID_len] = '\0';
				memcpy(&result->macaddr, &plnetinfo->pfnsubnet.BSSID,
				    ETHER_ADDR_LEN);

				DHD_PNO(("\tSSID : "));
				DHD_PNO(("\n"));
					DHD_PNO(("\tBSSID: %02x:%02x:%02x:%02x:%02x:%02x\n",
					result->macaddr.octet[0],
					result->macaddr.octet[1],
					result->macaddr.octet[2],
					result->macaddr.octet[3],
					result->macaddr.octet[4],
					result->macaddr.octet[5]));
				DHD_PNO(("\tchannel: %d, RSSI: %d, timestamp: %d ms\n",
					plnetinfo->pfnsubnet.channel,
					plnetinfo->RSSI, plnetinfo->timestamp));
				DHD_PNO(("\tRTT0 : %d, RTT1: %d\n",
				    plnetinfo->rtt0, plnetinfo->rtt1));

			}
		}
	}
exit_mutex_unlock:
	mutex_unlock(&_pno_state->pno_mutex);
exit:
	params->params_gscan.get_batch_flag = GSCAN_BATCH_RETRIEVAL_COMPLETE;
	smp_wmb();
	wake_up_interruptible(&_pno_state->batch_get_wait);
	if (nAPs_per_scan) {
		MFREE(dhd->osh, nAPs_per_scan, gscan_params->mscan);
	}
	if (plbestnet) {
		MFREE(dhd->osh, plbestnet, PNO_BESTNET_LEN);
	}
	DHD_PNO(("Batch retrieval done!\n"));
	return err;
}
#endif /* GSCAN_SUPPORT */

static int
_dhd_pno_get_for_batch(dhd_pub_t *dhd, char *buf, int bufsize, int reason)
{
	int err = BCME_OK;
	int i, j;
	uint32 timestamp = 0;
	dhd_pno_params_t *_params = NULL;
	dhd_pno_status_info_t *_pno_state = NULL;
	wl_pfn_lscanresults_t *plbestnet = NULL;
	wl_pfn_lnet_info_t *plnetinfo;
	dhd_pno_bestnet_entry_t *pbestnet_entry;
	dhd_pno_best_header_t *pbestnetheader = NULL;
	dhd_pno_scan_results_t *pscan_results = NULL, *siter, *snext;
	bool allocate_header = FALSE;
	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	if (!dhd_support_sta_mode(dhd)) {
		err = BCME_BADOPTION;
		goto exit_no_unlock;
	}
	DHD_PNO(("%s enter\n", __FUNCTION__));
	_pno_state = PNO_GET_PNOSTATE(dhd);

	if (!WLS_SUPPORTED(_pno_state)) {
		DHD_ERROR(("%s : wifi location service is not supported\n", __FUNCTION__));
		err = BCME_UNSUPPORTED;
		goto exit_no_unlock;
	}

	if (!(_pno_state->pno_mode & DHD_PNO_BATCH_MODE)) {
		DHD_ERROR(("%s: Batching SCAN mode is not enabled\n", __FUNCTION__));
		goto exit_no_unlock;
	}
	mutex_lock(&_pno_state->pno_mutex);
	_params = &_pno_state->pno_params_arr[INDEX_OF_BATCH_PARAMS];
	if (buf && bufsize) {
		if (!list_empty(&_params->params_batch.get_batch.expired_scan_results_list)) {
			/* need to check whether we have cashed data or not */
			DHD_PNO(("%s: have cashed batching data in Driver\n",
				__FUNCTION__));
			/* convert to results format */
			goto convert_format;
		} else {
			/* this is a first try to get batching results */
			if (!list_empty(&_params->params_batch.get_batch.scan_results_list)) {
				/* move the scan_results_list to expired_scan_results_lists */
				list_for_each_entry_safe(siter, snext,
					&_params->params_batch.get_batch.scan_results_list, list) {
					list_move_tail(&siter->list,
					&_params->params_batch.get_batch.expired_scan_results_list);
				}
				_params->params_batch.get_batch.top_node_cnt = 0;
				_params->params_batch.get_batch.expired_tot_scan_cnt =
					_params->params_batch.get_batch.tot_scan_cnt;
				_params->params_batch.get_batch.tot_scan_cnt = 0;
				goto convert_format;
			}
		}
	}
	/* create dhd_pno_scan_results_t whenever we got event WLC_E_PFN_BEST_BATCHING */
	pscan_results = (dhd_pno_scan_results_t *)MALLOC(dhd->osh, SCAN_RESULTS_SIZE);
	if (pscan_results == NULL) {
		err = BCME_NOMEM;
		DHD_ERROR(("failed to allocate dhd_pno_scan_results_t\n"));
		goto exit;
	}
	pscan_results->bestnetheader = NULL;
	pscan_results->cnt_header = 0;
	/* add the element into list unless total node cnt is less than MAX_NODE_ CNT */
	if (_params->params_batch.get_batch.top_node_cnt < MAX_NODE_CNT) {
		list_add(&pscan_results->list, &_params->params_batch.get_batch.scan_results_list);
		_params->params_batch.get_batch.top_node_cnt++;
	} else {
		int _removed_scan_cnt;
		/* remove oldest one and add new one */
		DHD_PNO(("%s : Remove oldest node and add new one\n", __FUNCTION__));
		_removed_scan_cnt = _dhd_pno_clear_all_batch_results(dhd,
			&_params->params_batch.get_batch.scan_results_list, TRUE);
		_params->params_batch.get_batch.tot_scan_cnt -= _removed_scan_cnt;
		list_add(&pscan_results->list, &_params->params_batch.get_batch.scan_results_list);

	}
	plbestnet = (wl_pfn_lscanresults_t *)MALLOC(dhd->osh, PNO_BESTNET_LEN);
	NULL_CHECK(plbestnet, "failed to allocate buffer for bestnet", err);
	DHD_PNO(("%s enter\n", __FUNCTION__));
	memset(plbestnet, 0, PNO_BESTNET_LEN);
	while (plbestnet->status != PFN_COMPLETE) {
		memset(plbestnet, 0, PNO_BESTNET_LEN);
		err = dhd_iovar(dhd, 0, "pfnlbest", NULL, 0, (char *)plbestnet, PNO_BESTNET_LEN,
			FALSE);
		if (err < 0) {
			if (err == BCME_EPERM) {
				DHD_ERROR(("we cannot get the batching data "
					"during scanning in firmware, try again\n,"));
				msleep(500);
				continue;
			} else {
				DHD_ERROR(("%s : failed to execute pfnlbest (err :%d)\n",
					__FUNCTION__, err));
				goto exit;
			}
		}
		DHD_PNO(("ver %d, status : %d, count %d\n", plbestnet->version,
			plbestnet->status, plbestnet->count));
		if (plbestnet->version != PFN_SCANRESULT_VERSION) {
			err = BCME_VERSION;
			DHD_ERROR(("bestnet version(%d) is mismatch with Driver version(%d)\n",
				plbestnet->version, PFN_SCANRESULT_VERSION));
			goto exit;
		}
		plnetinfo = plbestnet->netinfo;
		for (i = 0; i < plbestnet->count; i++) {
			pbestnet_entry = (dhd_pno_bestnet_entry_t *)
			MALLOC(dhd->osh, BESTNET_ENTRY_SIZE);
			if (pbestnet_entry == NULL) {
				err = BCME_NOMEM;
				DHD_ERROR(("failed to allocate dhd_pno_bestnet_entry\n"));
				goto exit;
			}
			memset(pbestnet_entry, 0, BESTNET_ENTRY_SIZE);
			pbestnet_entry->recorded_time = jiffies; /* record the current time */
			/* create header for the first entry */
			allocate_header = (i == 0)? TRUE : FALSE;
			/* check whether the new generation is started or not */
			if (timestamp && (TIME_DIFF(timestamp, plnetinfo->timestamp)
				> TIME_MIN_DIFF))
				allocate_header = TRUE;
			timestamp = plnetinfo->timestamp;
			if (allocate_header) {
				pbestnetheader = (dhd_pno_best_header_t *)
				MALLOC(dhd->osh, BEST_HEADER_SIZE);
				if (pbestnetheader == NULL) {
					err = BCME_NOMEM;
					if (pbestnet_entry)
						MFREE(dhd->osh, pbestnet_entry,
						BESTNET_ENTRY_SIZE);
					DHD_ERROR(("failed to allocate dhd_pno_bestnet_entry\n"));
					goto exit;
				}
				/* increase total cnt of bestnet header */
				pscan_results->cnt_header++;
				/* need to record the reason to call dhd_pno_get_for_bach */
				if (reason)
					pbestnetheader->reason = (ENABLE << reason);
				memset(pbestnetheader, 0, BEST_HEADER_SIZE);
				/* initialize the head of linked list */
				INIT_LIST_HEAD(&(pbestnetheader->entry_list));
				/* link the pbestnet heaer into existed list */
				if (pscan_results->bestnetheader == NULL)
					/* In case of header */
					pscan_results->bestnetheader = pbestnetheader;
				else {
					dhd_pno_best_header_t *head = pscan_results->bestnetheader;
					pscan_results->bestnetheader = pbestnetheader;
					pbestnetheader->next = head;
				}
			}
			/* fills the best network info */
			pbestnet_entry->channel = plnetinfo->pfnsubnet.channel;
			pbestnet_entry->RSSI = plnetinfo->RSSI;
			if (plnetinfo->flags & PFN_PARTIAL_SCAN_MASK) {
				/* if RSSI is positive value, we assume that
				 * this scan is aborted by other scan
				 */
				DHD_PNO(("This scan is aborted\n"));
				pbestnetheader->reason = (ENABLE << PNO_STATUS_ABORT);
			}
			pbestnet_entry->rtt0 = plnetinfo->rtt0;
			pbestnet_entry->rtt1 = plnetinfo->rtt1;
			pbestnet_entry->timestamp = plnetinfo->timestamp;
			if (plnetinfo->pfnsubnet.SSID_len > DOT11_MAX_SSID_LEN) {
				DHD_ERROR(("%s: Invalid SSID length %d: trimming it to max\n",
				      __FUNCTION__, plnetinfo->pfnsubnet.SSID_len));
				plnetinfo->pfnsubnet.SSID_len = DOT11_MAX_SSID_LEN;
			}
			pbestnet_entry->SSID_len = plnetinfo->pfnsubnet.SSID_len;
			memcpy(pbestnet_entry->SSID, plnetinfo->pfnsubnet.SSID,
				pbestnet_entry->SSID_len);
			memcpy(&pbestnet_entry->BSSID, &plnetinfo->pfnsubnet.BSSID, ETHER_ADDR_LEN);
			/* add the element into list */
			list_add_tail(&pbestnet_entry->list, &pbestnetheader->entry_list);
			/* increase best entry count */
			pbestnetheader->tot_cnt++;
			pbestnetheader->tot_size += BESTNET_ENTRY_SIZE;
			DHD_PNO(("Header %d\n", pscan_results->cnt_header - 1));
			DHD_PNO(("\tSSID : "));
			for (j = 0; j < plnetinfo->pfnsubnet.SSID_len; j++)
				DHD_PNO(("%c", plnetinfo->pfnsubnet.SSID[j]));
			DHD_PNO(("\n"));
			DHD_PNO(("\tBSSID: %02x:%02x:%02x:%02x:%02x:%02x\n",
				plnetinfo->pfnsubnet.BSSID.octet[0],
				plnetinfo->pfnsubnet.BSSID.octet[1],
				plnetinfo->pfnsubnet.BSSID.octet[2],
				plnetinfo->pfnsubnet.BSSID.octet[3],
				plnetinfo->pfnsubnet.BSSID.octet[4],
				plnetinfo->pfnsubnet.BSSID.octet[5]));
			DHD_PNO(("\tchannel: %d, RSSI: %d, timestamp: %d ms\n",
				plnetinfo->pfnsubnet.channel,
				plnetinfo->RSSI, plnetinfo->timestamp));
			DHD_PNO(("\tRTT0 : %d, RTT1: %d\n", plnetinfo->rtt0, plnetinfo->rtt1));
			plnetinfo++;
		}
	}
	if (pscan_results->cnt_header == 0) {
		/* In case that we didn't get any data from the firmware
		 * Remove the current scan_result list from get_bach.scan_results_list.
		 */
		DHD_PNO(("NO BATCH DATA from Firmware, Delete current SCAN RESULT LIST\n"));
		list_del(&pscan_results->list);
		MFREE(dhd->osh, pscan_results, SCAN_RESULTS_SIZE);
		_params->params_batch.get_batch.top_node_cnt--;
	}
	/* increase total scan count using current scan count */
	_params->params_batch.get_batch.tot_scan_cnt += pscan_results->cnt_header;

	if (buf && bufsize) {
		/* This is a first try to get batching results */
		if (!list_empty(&_params->params_batch.get_batch.scan_results_list)) {
			/* move the scan_results_list to expired_scan_results_lists */
			list_for_each_entry_safe(siter, snext,
				&_params->params_batch.get_batch.scan_results_list, list) {
				list_move_tail(&siter->list,
					&_params->params_batch.get_batch.expired_scan_results_list);
			}
			/* reset gloval values after  moving to expired list */
			_params->params_batch.get_batch.top_node_cnt = 0;
			_params->params_batch.get_batch.expired_tot_scan_cnt =
				_params->params_batch.get_batch.tot_scan_cnt;
			_params->params_batch.get_batch.tot_scan_cnt = 0;
		}
convert_format:
		err = _dhd_pno_convert_format(dhd, &_params->params_batch, buf, bufsize);
		if (err < 0) {
			DHD_ERROR(("failed to convert the data into upper layer format\n"));
			goto exit;
		}
	}
exit:
	if (plbestnet)
		MFREE(dhd->osh, plbestnet, PNO_BESTNET_LEN);
	if (_params) {
		_params->params_batch.get_batch.buf = NULL;
		_params->params_batch.get_batch.bufsize = 0;
		_params->params_batch.get_batch.bytes_written = err;
	}
	mutex_unlock(&_pno_state->pno_mutex);
exit_no_unlock:
	if (waitqueue_active(&_pno_state->get_batch_done.wait))
		complete(&_pno_state->get_batch_done);
	return err;
}
static void
_dhd_pno_get_batch_handler(struct work_struct *work)
{
	dhd_pno_status_info_t *_pno_state;
	dhd_pub_t *dhd;
	struct dhd_pno_batch_params *params_batch;
	DHD_PNO(("%s enter\n", __FUNCTION__));
	_pno_state = container_of(work, struct dhd_pno_status_info, work);
	dhd = _pno_state->dhd;
	if (dhd == NULL) {
		DHD_ERROR(("%s : dhd is NULL\n", __FUNCTION__));
		return;
	}
#ifdef GSCAN_SUPPORT
	_dhd_pno_get_gscan_batch_from_fw(dhd);
#endif /* GSCAN_SUPPORT */
	if (_pno_state->pno_mode & DHD_PNO_BATCH_MODE) {
		params_batch = &_pno_state->pno_params_arr[INDEX_OF_BATCH_PARAMS].params_batch;

		_dhd_pno_get_for_batch(dhd, params_batch->get_batch.buf,
			params_batch->get_batch.bufsize, params_batch->get_batch.reason);
	}
}

int
dhd_pno_get_for_batch(dhd_pub_t *dhd, char *buf, int bufsize, int reason)
{
	int err = BCME_OK;
	char *pbuf = buf;
	dhd_pno_status_info_t *_pno_state;
	struct dhd_pno_batch_params *params_batch;
	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	if (!dhd_support_sta_mode(dhd)) {
		err = BCME_BADOPTION;
		goto exit;
	}
	DHD_PNO(("%s enter\n", __FUNCTION__));
	_pno_state = PNO_GET_PNOSTATE(dhd);

	if (!WLS_SUPPORTED(_pno_state)) {
		DHD_ERROR(("%s : wifi location service is not supported\n", __FUNCTION__));
		err = BCME_UNSUPPORTED;
		goto exit;
	}
	params_batch = &_pno_state->pno_params_arr[INDEX_OF_BATCH_PARAMS].params_batch;
#ifdef GSCAN_SUPPORT
	if (_pno_state->pno_mode & DHD_PNO_GSCAN_MODE) {
		struct dhd_pno_gscan_params *gscan_params;
		gscan_params = &_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS].params_gscan;
		gscan_params->reason = reason;
		err = dhd_retreive_batch_scan_results(dhd);
		if (err == BCME_OK) {
			wait_event_interruptible_timeout(_pno_state->batch_get_wait,
			     is_batch_retrieval_complete(gscan_params),
			     msecs_to_jiffies(GSCAN_BATCH_GET_MAX_WAIT));
		}
	} else
#endif
	{
		if (!(_pno_state->pno_mode & DHD_PNO_BATCH_MODE)) {
			DHD_ERROR(("%s: Batching SCAN mode is not enabled\n", __FUNCTION__));
			memset(pbuf, 0, bufsize);
			pbuf += sprintf(pbuf, "scancount=%d\n", 0);
			sprintf(pbuf, "%s", RESULTS_END_MARKER);
			err = strlen(buf);
			goto exit;
		}
		params_batch->get_batch.buf = buf;
		params_batch->get_batch.bufsize = bufsize;
		params_batch->get_batch.reason = reason;
		params_batch->get_batch.bytes_written = 0;
		schedule_work(&_pno_state->work);
		wait_for_completion(&_pno_state->get_batch_done);
	}

#ifdef GSCAN_SUPPORT
	if (!(_pno_state->pno_mode & DHD_PNO_GSCAN_MODE))
#endif
	err = params_batch->get_batch.bytes_written;
exit:
	return err;
}

int
dhd_pno_stop_for_batch(dhd_pub_t *dhd)
{
	int err = BCME_OK;
	int mode = 0;
	int i = 0;
	dhd_pno_status_info_t *_pno_state;
	dhd_pno_params_t *_params;
	wl_pfn_bssid_t *p_pfn_bssid = NULL;
	wlc_ssid_ext_t *p_ssid_list = NULL;
	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	_pno_state = PNO_GET_PNOSTATE(dhd);
	DHD_PNO(("%s enter\n", __FUNCTION__));
	if (!dhd_support_sta_mode(dhd)) {
		err = BCME_BADOPTION;
		goto exit;
	}
	if (!WLS_SUPPORTED(_pno_state)) {
		DHD_ERROR(("%s : wifi location service is not supported\n",
			__FUNCTION__));
		err = BCME_UNSUPPORTED;
		goto exit;
	}

#ifdef GSCAN_SUPPORT
	if (_pno_state->pno_mode & DHD_PNO_GSCAN_MODE) {
		DHD_PNO(("Gscan is ongoing, nothing to stop here\n"));
		return err;
	}
#endif

	if (!(_pno_state->pno_mode & DHD_PNO_BATCH_MODE)) {
		DHD_ERROR(("%s : PNO BATCH MODE is not enabled\n", __FUNCTION__));
		goto exit;
	}
	_pno_state->pno_mode &= ~DHD_PNO_BATCH_MODE;
	if (_pno_state->pno_mode & (DHD_PNO_LEGACY_MODE | DHD_PNO_HOTLIST_MODE)) {
		mode = _pno_state->pno_mode;
		dhd_pno_clean(dhd);
		_pno_state->pno_mode = mode;
		/* restart Legacy PNO if the Legacy PNO is on */
		if (_pno_state->pno_mode & DHD_PNO_LEGACY_MODE) {
			struct dhd_pno_legacy_params *_params_legacy;
			_params_legacy =
				&(_pno_state->pno_params_arr[INDEX_OF_LEGACY_PARAMS].params_legacy);
			p_ssid_list = dhd_pno_get_legacy_pno_ssid(dhd, _pno_state);
			if (!p_ssid_list) {
				err = BCME_NOMEM;
				DHD_ERROR(("failed to get Legacy PNO SSID list\n"));
				goto exit;
			}
			err = dhd_pno_set_for_ssid(dhd, p_ssid_list, _params_legacy->nssid,
				_params_legacy->scan_fr, _params_legacy->pno_repeat,
				_params_legacy->pno_freq_expo_max, _params_legacy->chan_list,
				_params_legacy->nchan);
			if (err < 0) {
				DHD_ERROR(("%s : failed to restart legacy PNO scan(err: %d)\n",
					__FUNCTION__, err));
				goto exit;
			}
		} else if (_pno_state->pno_mode & DHD_PNO_HOTLIST_MODE) {
			struct dhd_pno_bssid *iter, *next;
			_params = &(_pno_state->pno_params_arr[INDEX_OF_HOTLIST_PARAMS]);
			p_pfn_bssid = kzalloc(sizeof(wl_pfn_bssid_t) *
				_params->params_hotlist.nbssid, GFP_KERNEL);
			if (p_pfn_bssid == NULL) {
				DHD_ERROR(("%s : failed to allocate wl_pfn_bssid_t array"
					" (count: %d)",
					__FUNCTION__, _params->params_hotlist.nbssid));
				err = BCME_ERROR;
				_pno_state->pno_mode &= ~DHD_PNO_HOTLIST_MODE;
				goto exit;
			}
			i = 0;
			/* convert dhd_pno_bssid to wl_pfn_bssid */
			list_for_each_entry_safe(iter, next,
				&_params->params_hotlist.bssid_list, list) {
				memcpy(&p_pfn_bssid[i].macaddr, &iter->macaddr, ETHER_ADDR_LEN);
				p_pfn_bssid[i].flags = iter->flags;
				i++;
			}
			err = dhd_pno_set_for_hotlist(dhd, p_pfn_bssid, &_params->params_hotlist);
			if (err < 0) {
				_pno_state->pno_mode &= ~DHD_PNO_HOTLIST_MODE;
				DHD_ERROR(("%s : failed to restart hotlist scan(err: %d)\n",
					__FUNCTION__, err));
				goto exit;
			}
		}
	} else {
		err = dhd_pno_clean(dhd);
		if (err < 0) {
			DHD_ERROR(("%s : failed to call dhd_pno_clean (err: %d)\n",
				__FUNCTION__, err));
			goto exit;
		}
	}
exit:
	_params = &_pno_state->pno_params_arr[INDEX_OF_BATCH_PARAMS];
	_dhd_pno_reinitialize_prof(dhd, _params, DHD_PNO_BATCH_MODE);
	kfree(p_ssid_list);
	kfree(p_pfn_bssid);
	return err;
}

int
dhd_pno_set_for_hotlist(dhd_pub_t *dhd, wl_pfn_bssid_t *p_pfn_bssid,
	struct dhd_pno_hotlist_params *hotlist_params)
{
	int err = BCME_OK;
	int i;
	uint16 _chan_list[WL_NUMCHANNELS];
	int rem_nchan = 0;
	int tot_nchan = 0;
	int mode = 0;
	dhd_pno_params_t *_params;
	dhd_pno_params_t *_params2;
	struct dhd_pno_bssid *_pno_bssid;
	dhd_pno_status_info_t *_pno_state;
	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	NULL_CHECK(hotlist_params, "hotlist_params is NULL", err);
	NULL_CHECK(p_pfn_bssid, "p_pfn_bssid is NULL", err);
	_pno_state = PNO_GET_PNOSTATE(dhd);
	DHD_PNO(("%s enter\n", __FUNCTION__));

	if (!dhd_support_sta_mode(dhd)) {
		err = BCME_BADOPTION;
		goto exit;
	}
	if (!WLS_SUPPORTED(_pno_state)) {
		DHD_ERROR(("%s : wifi location service is not supported\n", __FUNCTION__));
		err = BCME_UNSUPPORTED;
		goto exit;
	}
	_params = &_pno_state->pno_params_arr[INDEX_OF_HOTLIST_PARAMS];
	if (!(_pno_state->pno_mode & DHD_PNO_HOTLIST_MODE)) {
		_pno_state->pno_mode |= DHD_PNO_HOTLIST_MODE;
		err = _dhd_pno_reinitialize_prof(dhd, _params, DHD_PNO_HOTLIST_MODE);
		if (err < 0) {
			DHD_ERROR(("%s : failed to call _dhd_pno_reinitialize_prof\n",
				__FUNCTION__));
			goto exit;
		}
	}
	_params->params_batch.nchan = hotlist_params->nchan;
	_params->params_batch.scan_fr = hotlist_params->scan_fr;
	if (hotlist_params->nchan)
		memcpy(_params->params_hotlist.chan_list, hotlist_params->chan_list,
			sizeof(_params->params_hotlist.chan_list));
	memset(_chan_list, 0, sizeof(_chan_list));

	rem_nchan = ARRAYSIZE(hotlist_params->chan_list) - hotlist_params->nchan;
	if (hotlist_params->band == WLC_BAND_2G || hotlist_params->band == WLC_BAND_5G) {
		/* get a valid channel list based on band B or A */
		err = _dhd_pno_get_channels(dhd,
		&_params->params_hotlist.chan_list[hotlist_params->nchan],
		&rem_nchan, hotlist_params->band, FALSE);
		if (err < 0) {
			DHD_ERROR(("%s: failed to get valid channel list(band : %d)\n",
				__FUNCTION__, hotlist_params->band));
			goto exit;
		}
		/* now we need to update nchan because rem_chan has valid channel count */
		_params->params_hotlist.nchan += rem_nchan;
		/* need to sort channel list */
		sort(_params->params_hotlist.chan_list, _params->params_hotlist.nchan,
			sizeof(_params->params_hotlist.chan_list[0]), _dhd_pno_cmpfunc, NULL);
	}
#ifdef PNO_DEBUG
{
		int i;
		DHD_PNO(("Channel list : "));
		for (i = 0; i < _params->params_batch.nchan; i++) {
			DHD_PNO(("%d ", _params->params_batch.chan_list[i]));
		}
		DHD_PNO(("\n"));
}
#endif
	if (_params->params_hotlist.nchan) {
		/* copy the channel list into local array */
		memcpy(_chan_list, _params->params_hotlist.chan_list,
			sizeof(_chan_list));
		tot_nchan = _params->params_hotlist.nchan;
	}
	if (_pno_state->pno_mode & DHD_PNO_LEGACY_MODE) {
			DHD_PNO(("PNO SSID is on progress in firmware\n"));
			/* store current pno_mode before disabling pno */
			mode = _pno_state->pno_mode;
			err = _dhd_pno_enable(dhd, PNO_OFF);
			if (err < 0) {
				DHD_ERROR(("%s : failed to disable PNO\n", __FUNCTION__));
				goto exit;
			}
			/* restore the previous mode */
			_pno_state->pno_mode = mode;
			/* Use the superset for channelist between two mode */
			_params2 = &(_pno_state->pno_params_arr[INDEX_OF_LEGACY_PARAMS]);
			if (_params2->params_legacy.nchan > 0 &&
				_params->params_hotlist.nchan > 0) {
				err = _dhd_pno_chan_merge(_chan_list, &tot_nchan,
					&_params2->params_legacy.chan_list[0],
					_params2->params_legacy.nchan,
					&_params->params_hotlist.chan_list[0],
					_params->params_hotlist.nchan);
				if (err < 0) {
					DHD_ERROR(("%s : failed to merge channel list"
						"between legacy and hotlist\n",
						__FUNCTION__));
					goto exit;
				}
			}

	}

	INIT_LIST_HEAD(&(_params->params_hotlist.bssid_list));

	err = _dhd_pno_add_bssid(dhd, p_pfn_bssid, hotlist_params->nbssid);
	if (err < 0) {
		DHD_ERROR(("%s : failed to call _dhd_pno_add_bssid(err :%d)\n",
			__FUNCTION__, err));
		goto exit;
	}
	if ((err = _dhd_pno_set(dhd, _params, DHD_PNO_HOTLIST_MODE)) < 0) {
		DHD_ERROR(("%s : failed to set call pno_set (err %d) in firmware\n",
			__FUNCTION__, err));
		goto exit;
	}
	if (tot_nchan > 0) {
		if ((err = _dhd_pno_cfg(dhd, _chan_list, tot_nchan)) < 0) {
			DHD_ERROR(("%s : failed to set call pno_cfg (err %d) in firmware\n",
				__FUNCTION__, err));
			goto exit;
		}
	}
	for (i = 0; i < hotlist_params->nbssid; i++) {
		_pno_bssid = kzalloc(sizeof(struct dhd_pno_bssid), GFP_KERNEL);
		NULL_CHECK(_pno_bssid, "_pfn_bssid is NULL", err);
		memcpy(&_pno_bssid->macaddr, &p_pfn_bssid[i].macaddr, ETHER_ADDR_LEN);
		_pno_bssid->flags = p_pfn_bssid[i].flags;
		list_add_tail(&_pno_bssid->list, &_params->params_hotlist.bssid_list);
	}
	_params->params_hotlist.nbssid = hotlist_params->nbssid;
	if (_pno_state->pno_status == DHD_PNO_DISABLED) {
		if ((err = _dhd_pno_enable(dhd, PNO_ON)) < 0)
			DHD_ERROR(("%s : failed to enable PNO\n", __FUNCTION__));
	}
exit:
	/* clear mode in case of error */
	if (err < 0)
		_pno_state->pno_mode &= ~DHD_PNO_HOTLIST_MODE;
	return err;
}

int
dhd_pno_stop_for_hotlist(dhd_pub_t *dhd)
{
	int err = BCME_OK;
	uint32 mode = 0;
	dhd_pno_status_info_t *_pno_state;
	dhd_pno_params_t *_params;
	wlc_ssid_ext_t *p_ssid_list = NULL;
	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	_pno_state = PNO_GET_PNOSTATE(dhd);

	if (!WLS_SUPPORTED(_pno_state)) {
		DHD_ERROR(("%s : wifi location service is not supported\n",
			__FUNCTION__));
		err = BCME_UNSUPPORTED;
		goto exit;
	}

	if (!(_pno_state->pno_mode & DHD_PNO_HOTLIST_MODE)) {
		DHD_ERROR(("%s : Hotlist MODE is not enabled\n",
			__FUNCTION__));
		goto exit;
	}
	_pno_state->pno_mode &= ~DHD_PNO_BATCH_MODE;

	if (_pno_state->pno_mode & (DHD_PNO_LEGACY_MODE | DHD_PNO_BATCH_MODE)) {
		/* retrieve the batching data from firmware into host */
		dhd_pno_get_for_batch(dhd, NULL, 0, PNO_STATUS_DISABLE);
		/* save current pno_mode before calling dhd_pno_clean */
		mode = _pno_state->pno_mode;
		err = dhd_pno_clean(dhd);
		if (err < 0) {
			DHD_ERROR(("%s : failed to call dhd_pno_clean (err: %d)\n",
				__FUNCTION__, err));
			goto exit;
		}
		/* restore previos pno mode */
		_pno_state->pno_mode = mode;
		if (_pno_state->pno_mode & DHD_PNO_LEGACY_MODE) {
			/* restart Legacy PNO Scan */
			struct dhd_pno_legacy_params *_params_legacy;
			_params_legacy =
			&(_pno_state->pno_params_arr[INDEX_OF_LEGACY_PARAMS].params_legacy);
			p_ssid_list = dhd_pno_get_legacy_pno_ssid(dhd, _pno_state);
			if (!p_ssid_list) {
				err = BCME_NOMEM;
				DHD_ERROR(("failed to get Legacy PNO SSID list\n"));
				goto exit;
			}
			err = dhd_pno_set_for_ssid(dhd, p_ssid_list, _params_legacy->nssid,
				_params_legacy->scan_fr, _params_legacy->pno_repeat,
				_params_legacy->pno_freq_expo_max, _params_legacy->chan_list,
				_params_legacy->nchan);
			if (err < 0) {
				DHD_ERROR(("%s : failed to restart legacy PNO scan(err: %d)\n",
					__FUNCTION__, err));
				goto exit;
			}
		} else if (_pno_state->pno_mode & DHD_PNO_BATCH_MODE) {
			/* restart Batching Scan */
			_params = &(_pno_state->pno_params_arr[INDEX_OF_BATCH_PARAMS]);
			/* restart BATCH SCAN */
			err = dhd_pno_set_for_batch(dhd, &_params->params_batch);
			if (err < 0) {
				_pno_state->pno_mode &= ~DHD_PNO_BATCH_MODE;
				DHD_ERROR(("%s : failed to restart batch scan(err: %d)\n",
					__FUNCTION__,  err));
				goto exit;
			}
		}
	} else {
		err = dhd_pno_clean(dhd);
		if (err < 0) {
			DHD_ERROR(("%s : failed to call dhd_pno_clean (err: %d)\n",
				__FUNCTION__, err));
			goto exit;
		}
	}
exit:
	kfree(p_ssid_list);
	return err;
}

#ifdef GSCAN_SUPPORT
int
dhd_retreive_batch_scan_results(dhd_pub_t *dhd)
{
	int err = BCME_OK;
	dhd_pno_status_info_t *_pno_state;
	dhd_pno_params_t *_params;
	struct dhd_pno_batch_params *params_batch;

	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	_pno_state = PNO_GET_PNOSTATE(dhd);
	_params = &_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS];

	params_batch = &_pno_state->pno_params_arr[INDEX_OF_BATCH_PARAMS].params_batch;
	if (_params->params_gscan.get_batch_flag == GSCAN_BATCH_RETRIEVAL_COMPLETE) {
		DHD_PNO(("Retreive batch results\n"));
		params_batch->get_batch.buf = NULL;
		params_batch->get_batch.bufsize = 0;
		params_batch->get_batch.reason = PNO_STATUS_EVENT;
		_params->params_gscan.get_batch_flag = GSCAN_BATCH_RETRIEVAL_IN_PROGRESS;
		smp_wmb();
		schedule_work(&_pno_state->work);
	} else {
		DHD_PNO(("%s : WLC_E_PFN_BEST_BATCHING retrieval"
			"already in progress, will skip\n", __FUNCTION__));
		err = BCME_ERROR;
	}

	return err;
}

/* Handle Significant WiFi Change (SWC) event from FW
 * Send event to HAL when all results arrive from FW
 */
void *
dhd_handle_swc_evt(dhd_pub_t *dhd, const void *event_data, int *send_evt_bytes)
{
	void *ptr = NULL;
	dhd_pno_status_info_t *_pno_state = PNO_GET_PNOSTATE(dhd);
	struct dhd_pno_gscan_params *gscan_params;
	struct dhd_pno_swc_evt_param *params;
	wl_pfn_swc_results_t *results = (wl_pfn_swc_results_t *)event_data;
	wl_pfn_significant_net_t *change_array;
	int i;

	gscan_params = &(_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS].params_gscan);
	params = &(gscan_params->param_significant);

	if (!results->total_count) {
		*send_evt_bytes = 0;
		return ptr;
	}

	if (!params->results_rxed_so_far) {
		if (!params->change_array) {
			params->change_array = (wl_pfn_significant_net_t *)
			kmalloc(sizeof(wl_pfn_significant_net_t) * results->total_count,
			GFP_KERNEL);

			if (!params->change_array) {
				DHD_ERROR(("%s Cannot Malloc %zd bytes!!\n", __FUNCTION__,
				sizeof(wl_pfn_significant_net_t) * results->total_count));
				*send_evt_bytes = 0;
				return ptr;
			}
		} else {
			DHD_ERROR(("RX'ed WLC_E_PFN_SWC evt from FW, previous evt not complete!!"));
			*send_evt_bytes = 0;
			return ptr;
		}

	}

	DHD_PNO(("%s: pkt_count %d total_count %d\n", __FUNCTION__,
	results->pkt_count, results->total_count));

	for (i = 0; i < results->pkt_count; i++) {
		DHD_PNO(("\t %02x:%02x:%02x:%02x:%02x:%02x\n",
		results->list[i].BSSID.octet[0],
		results->list[i].BSSID.octet[1],
		results->list[i].BSSID.octet[2],
		results->list[i].BSSID.octet[3],
		results->list[i].BSSID.octet[4],
		results->list[i].BSSID.octet[5]));
	}

	change_array = &params->change_array[params->results_rxed_so_far];
	memcpy(change_array, results->list, sizeof(wl_pfn_significant_net_t) * results->pkt_count);
	params->results_rxed_so_far += results->pkt_count;

	if (params->results_rxed_so_far == results->total_count) {
		params->results_rxed_so_far = 0;
		*send_evt_bytes = sizeof(wl_pfn_significant_net_t) * results->total_count;
		/* Pack up change buffer to send up and reset
		 * results_rxed_so_far, after its done.
		 */
		ptr = (void *) params->change_array;
		/* expecting the callee to free this mem chunk */
		params->change_array = NULL;
	}
	 else {
		*send_evt_bytes = 0;
	}

	return ptr;
}

void
dhd_gscan_hotlist_cache_cleanup(dhd_pub_t *dhd, hotlist_type_t type)
{
	dhd_pno_status_info_t *_pno_state = PNO_GET_PNOSTATE(dhd);
	struct dhd_pno_gscan_params *gscan_params;
	gscan_results_cache_t *iter, *tmp;

	if (!_pno_state) {
		return;
	}
	gscan_params = &(_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS].params_gscan);

	if (type == HOTLIST_FOUND) {
		iter = gscan_params->gscan_hotlist_found;
		gscan_params->gscan_hotlist_found = NULL;
	} else {
		iter = gscan_params->gscan_hotlist_lost;
		gscan_params->gscan_hotlist_lost = NULL;
	}

	while (iter) {
		tmp = iter->next;
		kfree(iter);
		iter = tmp;
	}

	return;
}

void *
dhd_process_full_gscan_result(dhd_pub_t *dhd, const void *data, uint32 len, int *size)
{
	wl_bss_info_t *bi = NULL;
	wl_gscan_result_t *gscan_result;
	wifi_gscan_result_t *result = NULL;
	u32 bi_length = 0;
	uint8 channel;
	uint32 mem_needed;
	struct timespec ts;
	u32 bi_ie_length = 0;
	u32 bi_ie_offset = 0;

	*size = 0;

	gscan_result = (wl_gscan_result_t *)data;

	if (!gscan_result) {
		DHD_ERROR(("Invalid gscan result (NULL pointer)\n"));
		goto exit;
	}
	if (!gscan_result->bss_info) {
		DHD_ERROR(("Invalid gscan bss info (NULL pointer)\n"));
		goto exit;
	}

	if ((len < sizeof(*gscan_result)) ||
		(len < dtoh32(gscan_result->buflen)) ||
		(dtoh32(gscan_result->buflen) >
		 (sizeof(*gscan_result) + WL_SCAN_IE_LEN_MAX))) {
		DHD_ERROR(("%s: invalid gscan buflen:%u\n", __FUNCTION__,
					dtoh32(gscan_result->buflen)));
		goto exit;
	}

	bi = &gscan_result->bss_info[0].info;
	bi_length = dtoh32(bi->length);
	if (bi_length != (dtoh32(gscan_result->buflen) -
		WL_GSCAN_RESULTS_FIXED_SIZE - WL_GSCAN_INFO_FIXED_FIELD_SIZE)) {
		DHD_ERROR(("Invalid bss_info length %d: ignoring\n", bi_length));
		goto exit;
	}
	bi_ie_offset = dtoh32(bi->ie_offset);
	bi_ie_length = dtoh32(bi->ie_length);
	if ((bi_ie_offset + bi_ie_length) > bi_length) {
		DHD_ERROR(("%s: Invalid ie_length:%u or ie_offset:%u\n",
			__FUNCTION__, bi_ie_length, bi_ie_offset));
		goto exit;
	}
	if (bi->SSID_len > DOT11_MAX_SSID_LEN) {
		DHD_ERROR(("%s: Invalid SSID length:%u\n", __FUNCTION__, bi->SSID_len));
		goto exit;
	}

	mem_needed = OFFSETOF(wifi_gscan_result_t, ie_data) + bi_ie_length;
	result = kmalloc(mem_needed, GFP_KERNEL);

	if (!result) {
		DHD_ERROR(("%s Cannot malloc scan result buffer %d bytes\n",
		 __FUNCTION__, mem_needed));
		goto exit;
	}

	memcpy(result->ssid, bi->SSID, bi->SSID_len);
	result->ssid[bi->SSID_len] = '\0';
	channel = wf_chspec_ctlchan(bi->chanspec);
	result->channel = wf_channel2mhz(channel,
		(channel <= CH_MAX_2G_CHANNEL?
		WF_CHAN_FACTOR_2_4_G : WF_CHAN_FACTOR_5_G));
	result->rssi = (int32) bi->RSSI;
	result->rtt = 0;
	result->rtt_sd = 0;
	get_monotonic_boottime(&ts);
	result->ts = (uint64) TIMESPEC_TO_US(ts);
	result->beacon_period = dtoh16(bi->beacon_period);
	result->capability = dtoh16(bi->capability);
	result->ie_length = bi_ie_length;
	memcpy(&result->macaddr, &bi->BSSID, ETHER_ADDR_LEN);
	memcpy(result->ie_data, ((uint8 *)bi + bi_ie_offset), bi->ie_length);
	*size = mem_needed;
exit:
	return result;
}

void *
dhd_pno_process_epno_result(dhd_pub_t *dhd, const void *data, uint32 event, int *size)
{
	dhd_epno_results_t *results = NULL;
	dhd_pno_status_info_t *_pno_state = PNO_GET_PNOSTATE(dhd);
	struct dhd_pno_gscan_params *gscan_params;
	uint32 count, mem_needed = 0, i;
	uint8 ssid[DOT11_MAX_SSID_LEN + 1];
	struct ether_addr *bssid;

	*size = 0;
	if (!_pno_state)
		return NULL;
	gscan_params = &(_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS].params_gscan);

	if (event == WLC_E_PFN_SSID_EXT) {
		wl_pfn_ssid_ext_result_t *evt_data;
		evt_data = (wl_pfn_ssid_ext_result_t *) data;

		if (evt_data->version != PFN_SSID_EXT_VERSION) {
			DHD_PNO(("ePNO event: Incorrect version %d %d\n", evt_data->version,
			     PFN_SSID_EXT_VERSION));
			return NULL;
		}
		count = evt_data->count;
		mem_needed = sizeof(dhd_epno_results_t) * count;
		results = (dhd_epno_results_t *) kmalloc(mem_needed, GFP_KERNEL);
		if (!results) {
			DHD_ERROR(("%s: Can't malloc %d bytes for results\n", __FUNCTION__,
			        mem_needed));
			return NULL;
		}
		DHD_ERROR(("Rx'ed WLC_E_PFN_SSID_EXT event: %d results\n", count));
		for (i = 0; i < count; i++) {
			results[i].rssi = evt_data->net[i].rssi;
			results[i].channel = wf_channel2mhz(evt_data->net[i].channel,
			                (evt_data->net[i].channel <= CH_MAX_2G_CHANNEL ?
			                WF_CHAN_FACTOR_2_4_G : WF_CHAN_FACTOR_5_G));
			results[i].flags = evt_data->net[i].flags;
			dhd_pno_idx_to_ssid(gscan_params, &results[i],
			           evt_data->net[i].index);
			memcpy(ssid, results[i].ssid, results[i].ssid_len);
			bssid = &results[i].bssid;
			memcpy(bssid, &evt_data->net[i].bssid, ETHER_ADDR_LEN);
			ssid[results[i].ssid_len] = '\0';
			DHD_PNO(("ssid - %s bssid %02x:%02x:%02x:%02x:%02x:%02x "
			        "idx %d ch %d rssi %d flags %d\n", ssid,
			        bssid->octet[0], bssid->octet[1],
			        bssid->octet[2], bssid->octet[3],
			        bssid->octet[4], bssid->octet[5],
			        evt_data->net[i].index, results[i].channel,
			        results[i].rssi, results[i].flags));
		}
	} else if (event == WLC_E_PFN_NET_FOUND || event == WLC_E_PFN_NET_LOST) {
		wl_pfn_scanresults_t *pfn_result = (wl_pfn_scanresults_t *)data;
		wl_pfn_net_info_t *net;

		if (pfn_result->version != PFN_SCANRESULT_VERSION) {
			DHD_ERROR(("%s event %d: Incorrect version %d %d\n", __FUNCTION__, event,
			          pfn_result->version, PFN_SCANRESULT_VERSION));
			return NULL;
		}
		if ((pfn_result->count == 0) || (pfn_result->count > EVENT_MAX_NETCNT)) {
			DHD_ERROR(("%s event %d: wrong pfn results count %d\n",
				__FUNCTION__, event, pfn_result->count));
			return NULL;
		}
		count = pfn_result->count;
		mem_needed = sizeof(dhd_epno_results_t) * count;
		results = (dhd_epno_results_t *) kmalloc(mem_needed, GFP_KERNEL);
		if (!results) {
			DHD_ERROR(("%s: Can't malloc %d bytes for results\n", __FUNCTION__,
			        mem_needed));
			return NULL;
		}
		for (i = 0; i < count; i++) {
			net = &pfn_result->netinfo[i];
			results[i].rssi = net->RSSI;
			results[i].channel =  wf_channel2mhz(net->pfnsubnet.channel,
			                  (net->pfnsubnet.channel <= CH_MAX_2G_CHANNEL ?
			                  WF_CHAN_FACTOR_2_4_G : WF_CHAN_FACTOR_5_G));
			results[i].flags = (event == WLC_E_PFN_NET_FOUND) ?
			               WL_PFN_SSID_EXT_FOUND: WL_PFN_SSID_EXT_LOST;
			results[i].ssid_len = min(net->pfnsubnet.SSID_len,
			               (uint8)DOT11_MAX_SSID_LEN);
			bssid = &results[i].bssid;
			memcpy(bssid, &net->pfnsubnet.BSSID, ETHER_ADDR_LEN);
			memcpy(results[i].ssid, net->pfnsubnet.SSID, results[i].ssid_len);
			memcpy(ssid, results[i].ssid, results[i].ssid_len);
			ssid[results[i].ssid_len] = '\0';
			DHD_PNO(("ssid - %s bssid %02x:%02x:%02x:%02x:%02x:%02x "
			        "ch %d rssi %d flags %d\n", ssid,
			        bssid->octet[0], bssid->octet[1],
			        bssid->octet[2], bssid->octet[3],
			        bssid->octet[4], bssid->octet[5],
			        results[i].channel, results[i].rssi, results[i].flags));
		}
	}
	*size = mem_needed;
	return results;
}

void *
dhd_handle_hotlist_scan_evt(dhd_pub_t *dhd, const void *event_data,
        int *send_evt_bytes, hotlist_type_t type)
{
	void *ptr = NULL;
	dhd_pno_status_info_t *_pno_state = PNO_GET_PNOSTATE(dhd);
	struct dhd_pno_gscan_params *gscan_params;
	wl_pfn_scanresults_t *results = (wl_pfn_scanresults_t *)event_data;
	wifi_gscan_result_t *hotlist_found_array;
	wl_pfn_net_info_t *plnetinfo;
	gscan_results_cache_t *gscan_hotlist_cache;
	int malloc_size = 0, i, total = 0;

	gscan_params = &(_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS].params_gscan);

	if ((results->count == 0) || (results->count > EVENT_MAX_NETCNT)) {
		DHD_ERROR(("%s: wrong result count:%d\n", __FUNCTION__, results->count));
		*send_evt_bytes = 0;
		return ptr;
	}

	malloc_size = sizeof(gscan_results_cache_t) +
	((results->count - 1) * sizeof(wifi_gscan_result_t));
	gscan_hotlist_cache = (gscan_results_cache_t *) kmalloc(malloc_size, GFP_KERNEL);

	if (!gscan_hotlist_cache) {
		DHD_ERROR(("%s Cannot Malloc %d bytes!!\n", __FUNCTION__, malloc_size));
		*send_evt_bytes = 0;
		return ptr;
	}

	if (type == HOTLIST_FOUND) {
		gscan_hotlist_cache->next = gscan_params->gscan_hotlist_found;
		gscan_params->gscan_hotlist_found = gscan_hotlist_cache;
		DHD_PNO(("%s enter, FOUND results count %d\n", __FUNCTION__, results->count));
	} else {
		gscan_hotlist_cache->next = gscan_params->gscan_hotlist_lost;
		gscan_params->gscan_hotlist_lost = gscan_hotlist_cache;
		DHD_PNO(("%s enter, LOST results count %d\n", __FUNCTION__, results->count));
	}

	gscan_hotlist_cache->tot_count = results->count;
	gscan_hotlist_cache->tot_consumed = 0;
	plnetinfo = results->netinfo;

	for (i = 0; i < results->count; i++, plnetinfo++) {
		hotlist_found_array = &gscan_hotlist_cache->results[i];
		hotlist_found_array->channel = wf_channel2mhz(plnetinfo->pfnsubnet.channel,
			(plnetinfo->pfnsubnet.channel <= CH_MAX_2G_CHANNEL?
			WF_CHAN_FACTOR_2_4_G : WF_CHAN_FACTOR_5_G));
		hotlist_found_array->rssi = (int32) plnetinfo->RSSI;
		/* Info not available & not expected */
		hotlist_found_array->beacon_period = 0;
		hotlist_found_array->capability = 0;
		hotlist_found_array->ie_length = 0;

		hotlist_found_array->ts = convert_fw_rel_time_to_systime(plnetinfo->timestamp);
		if (plnetinfo->pfnsubnet.SSID_len > DOT11_MAX_SSID_LEN) {
			DHD_ERROR(("Invalid SSID length %d: trimming it to max\n",
			          plnetinfo->pfnsubnet.SSID_len));
			plnetinfo->pfnsubnet.SSID_len = DOT11_MAX_SSID_LEN;
		}
		memcpy(hotlist_found_array->ssid, plnetinfo->pfnsubnet.SSID,
			plnetinfo->pfnsubnet.SSID_len);
		hotlist_found_array->ssid[plnetinfo->pfnsubnet.SSID_len] = '\0';

		memcpy(&hotlist_found_array->macaddr, &plnetinfo->pfnsubnet.BSSID, ETHER_ADDR_LEN);
	DHD_PNO(("\t%s %02x:%02x:%02x:%02x:%02x:%02x rssi %d\n", hotlist_found_array->ssid,
		hotlist_found_array->macaddr.octet[0],
		hotlist_found_array->macaddr.octet[1],
		hotlist_found_array->macaddr.octet[2],
		hotlist_found_array->macaddr.octet[3],
		hotlist_found_array->macaddr.octet[4],
		hotlist_found_array->macaddr.octet[5],
		hotlist_found_array->rssi));
	}


	if (results->status == PFN_COMPLETE) {
		ptr = (void *) gscan_hotlist_cache;
		while (gscan_hotlist_cache) {
			total += gscan_hotlist_cache->tot_count;
			gscan_hotlist_cache = gscan_hotlist_cache->next;
		}
		*send_evt_bytes =  total * sizeof(wifi_gscan_result_t);
	}

	return ptr;
}
#endif /* GSCAN_SUPPORT */
int
dhd_pno_event_handler(dhd_pub_t *dhd, wl_event_msg_t *event, void *event_data)
{
	int err = BCME_OK;
	uint status, event_type, flags, datalen;
	dhd_pno_status_info_t *_pno_state;
	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	_pno_state = PNO_GET_PNOSTATE(dhd);
	if (!WLS_SUPPORTED(_pno_state)) {
		DHD_ERROR(("%s : wifi location service is not supported\n", __FUNCTION__));
		err = BCME_UNSUPPORTED;
		goto exit;
	}
	event_type = ntoh32(event->event_type);
	flags = ntoh16(event->flags);
	status = ntoh32(event->status);
	datalen = ntoh32(event->datalen);
	DHD_PNO(("%s enter : event_type :%d\n", __FUNCTION__, event_type));
	switch (event_type) {
	case WLC_E_PFN_BSSID_NET_FOUND:
	case WLC_E_PFN_BSSID_NET_LOST:
		/* TODO : need to implement event logic using generic netlink */
		break;
	case WLC_E_PFN_BEST_BATCHING:
#ifndef GSCAN_SUPPORT
	{
		struct dhd_pno_batch_params *params_batch;
		params_batch = &_pno_state->pno_params_arr[INDEX_OF_BATCH_PARAMS].params_batch;
		if (!waitqueue_active(&_pno_state->get_batch_done.wait)) {
			DHD_PNO(("%s : WLC_E_PFN_BEST_BATCHING\n", __FUNCTION__));
			params_batch->get_batch.buf = NULL;
			params_batch->get_batch.bufsize = 0;
			params_batch->get_batch.reason = PNO_STATUS_EVENT;
			schedule_work(&_pno_state->work);
		} else
			DHD_PNO(("%s : WLC_E_PFN_BEST_BATCHING"
				"will skip this event\n", __FUNCTION__));
		break;
	}
#else
		break;
#endif /* !GSCAN_SUPPORT */
	default:
		DHD_ERROR(("unknown event : %d\n", event_type));
	}
exit:
	return err;
}

int dhd_pno_init(dhd_pub_t *dhd)
{
	int err = BCME_OK;
	dhd_pno_status_info_t *_pno_state;
	char *buf = NULL;
	NULL_CHECK(dhd, "dhd is NULL", err);
	DHD_PNO(("%s enter\n", __FUNCTION__));
	UNUSED_PARAMETER(_dhd_pno_suspend);
	if (dhd->pno_state)
		goto exit;
	dhd->pno_state = MALLOC(dhd->osh, sizeof(dhd_pno_status_info_t));
	NULL_CHECK(dhd->pno_state, "failed to create dhd_pno_state", err);
	memset(dhd->pno_state, 0, sizeof(dhd_pno_status_info_t));
	/* need to check whether current firmware support batching and hotlist scan */
	_pno_state = PNO_GET_PNOSTATE(dhd);
	_pno_state->wls_supported = TRUE;
	_pno_state->dhd = dhd;
	mutex_init(&_pno_state->pno_mutex);
	INIT_WORK(&_pno_state->work, _dhd_pno_get_batch_handler);
	init_completion(&_pno_state->get_batch_done);
#ifdef GSCAN_SUPPORT
	init_waitqueue_head(&_pno_state->batch_get_wait);
#endif /* GSCAN_SUPPORT */
	buf = kmalloc(WLC_IOCTL_SMLEN, GFP_KERNEL);
	if (!buf) {
		DHD_ERROR((":%s buf alloc err.\n", __FUNCTION__));
		return BCME_NOMEM;
	}
	err = dhd_iovar(dhd, 0, "pfnlbest", NULL, 0, buf, WLC_IOCTL_SMLEN,
		FALSE);
	if (err == BCME_UNSUPPORTED) {
		_pno_state->wls_supported = FALSE;
		DHD_INFO(("Current firmware doesn't support"
			" Android Location Service\n"));
	} else {
		DHD_ERROR(("%s: Support Android Location Service\n",
			__FUNCTION__));
	}
exit:
	kfree(buf);
	return err;
}

int dhd_pno_deinit(dhd_pub_t *dhd)
{
	int err = BCME_OK;
	dhd_pno_status_info_t *_pno_state;
	dhd_pno_params_t *_params;
	NULL_CHECK(dhd, "dhd is NULL", err);

	DHD_PNO(("%s enter\n", __FUNCTION__));
	_pno_state = PNO_GET_PNOSTATE(dhd);
	NULL_CHECK(_pno_state, "pno_state is NULL", err);
	/* may need to free legacy ssid_list */
	if (_pno_state->pno_mode & DHD_PNO_LEGACY_MODE) {
		_params = &_pno_state->pno_params_arr[INDEX_OF_LEGACY_PARAMS];
		_dhd_pno_reinitialize_prof(dhd, _params, DHD_PNO_LEGACY_MODE);
	}

#ifdef GSCAN_SUPPORT
	if (_pno_state->pno_mode & DHD_PNO_GSCAN_MODE) {
		_params = &_pno_state->pno_params_arr[INDEX_OF_GSCAN_PARAMS];
		mutex_lock(&_pno_state->pno_mutex);
		dhd_pno_reset_cfg_gscan(_params, _pno_state, GSCAN_FLUSH_ALL_CFG);
		mutex_unlock(&_pno_state->pno_mutex);
	}
#endif /* GSCAN_SUPPORT */

	if (_pno_state->pno_mode & DHD_PNO_BATCH_MODE) {
		_params = &_pno_state->pno_params_arr[INDEX_OF_BATCH_PARAMS];
		/* clear resource if the BATCH MODE is on */
		_dhd_pno_reinitialize_prof(dhd, _params, DHD_PNO_BATCH_MODE);
	}
	cancel_work_sync(&_pno_state->work);
	MFREE(dhd->osh, _pno_state, sizeof(dhd_pno_status_info_t));
	dhd->pno_state = NULL;
	return err;
}
#endif /* PNO_SUPPORT */
