/*
 * Broadcom Dongle Host Driver (DHD)
 * Prefered Network Offload and Wi-Fi Location Service(WLS) code.
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: dhd_pno.c 420056 2013-08-24 00:53:12Z $
 */
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

#ifdef __BIG_ENDIAN
#include <bcmendian.h>
#define htod32(i) (bcmswap32(i))
#define htod16(i) (bcmswap16(i))
#define dtoh32(i) (bcmswap32(i))
#define dtoh16(i) (bcmswap16(i))
#define htodchanspec(i) htod16(i)
#define dtohchanspec(i) dtoh16(i)
#else
#define htod32(i) i
#define htod16(i) i
#define dtoh32(i) i
#define dtoh16(i) i
#define htodchanspec(i) i
#define dtohchanspec(i) i
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
#define PNO_BESTNET_LEN 1024
#define PNO_ON 1
#define PNO_OFF 0
#define CHANNEL_2G_MAX 14
#define MAX_NODE_CNT 5
#define WLS_SUPPORTED(pno_state) (pno_state->wls_supported == TRUE)
#define TIME_DIFF(timestamp1, timestamp2) (abs((uint32)(timestamp1/1000)  \
						- (uint32)(timestamp2/1000)))

#define ENTRY_OVERHEAD strlen("bssid=\nssid=\nfreq=\nlevel=\nage=\ndist=\ndistSd=\n====")
#define TIME_MIN_DIFF 5
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
static int
_dhd_pno_clean(dhd_pub_t *dhd)
{
	int pfn = 0;
	int err;
	dhd_pno_status_info_t *_pno_state;
	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	_pno_state = PNO_GET_PNOSTATE(dhd);
	DHD_PNO(("%s enter\n", __FUNCTION__));
	/* Disable PNO */
	err = dhd_iovar(dhd, 0, "pfn", (char *)&pfn, sizeof(pfn), 1);
	if (err < 0) {
		DHD_ERROR(("%s : failed to execute pfn(error : %d)\n",
			__FUNCTION__, err));
		goto exit;
	}
	_pno_state->pno_status = DHD_PNO_DISABLED;
	err = dhd_iovar(dhd, 0, "pfnclear", NULL, 0, 1);
	if (err < 0) {
		DHD_ERROR(("%s : failed to execute pfnclear(error : %d)\n",
			__FUNCTION__, err));
	}
exit:
	return err;
}

static int
_dhd_pno_suspend(dhd_pub_t *dhd)
{
	int err;
	int suspend = 1;
	dhd_pno_status_info_t *_pno_state;
	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	DHD_PNO(("%s enter\n", __FUNCTION__));
	err = dhd_iovar(dhd, 0, "pfn_suspend", (char *)&suspend, sizeof(suspend), 1);
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
	err = dhd_iovar(dhd, 0, "pfn", (char *)&enable, sizeof(enable), 1);
	if (err < 0) {
		DHD_ERROR(("%s : failed to execute pfn_set\n", __FUNCTION__));
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
	}
	if (mode & (DHD_PNO_BATCH_MODE | DHD_PNO_HOTLIST_MODE)) {
		/* Scan frequency of 30 sec */
		pfn_param.scan_freq = htod32(30);
		/* slow adapt scan is off by default */
		pfn_param.slow_freq = htod32(0);
		/* RSSI margin of 30 dBm */
		pfn_param.rssi_margin = htod16(30);
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
	if (pfn_param.scan_freq < htod32(PNO_SCAN_MIN_FW_SEC) ||
		pfn_param.scan_freq > htod32(PNO_SCAN_MAX_FW_SEC)) {
		DHD_ERROR(("%s pno freq(%d sec) is not valid \n",
			__FUNCTION__, PNO_SCAN_MIN_FW_SEC));
		err = BCME_BADARG;
		goto exit;
	}
	if (mode == DHD_PNO_BATCH_MODE) {
		int _tmp = pfn_param.bestn;
		/* set bestn to calculate the max mscan which firmware supports */
		err = dhd_iovar(dhd, 0, "pfnmscan", (char *)&_tmp, sizeof(_tmp), 1);
		if (err < 0) {
			DHD_ERROR(("%s : failed to set pfnmscan\n", __FUNCTION__));
			goto exit;
		}
		/* get max mscan which the firmware supports */
		err = dhd_iovar(dhd, 0, "pfnmscan", (char *)&_tmp, sizeof(_tmp), 0);
		if (err < 0) {
			DHD_ERROR(("%s : failed to get pfnmscan\n", __FUNCTION__));
			goto exit;
		}
		DHD_PNO((" returned mscan : %d, set bestn : %d\n", _tmp, pfn_param.bestn));
		pfn_param.mscan = MIN(pfn_param.mscan, _tmp);
	}
	err = dhd_iovar(dhd, 0, "pfn_set", (char *)&pfn_param, sizeof(pfn_param), 1);
	if (err < 0) {
		DHD_ERROR(("%s : failed to execute pfn_set\n", __FUNCTION__));
		goto exit;
	}
	/* need to return mscan if this is for batch scan instead of err */
	err = (mode == DHD_PNO_BATCH_MODE)? pfn_param.mscan : err;
exit:
	return err;
}
static int
_dhd_pno_add_ssid(dhd_pub_t *dhd, wlc_ssid_t* ssids_list, int nssid)
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
			DHD_PNO(("%d: scan  for  %s size = %d\n", j,
				ssids_list[j].SSID, ssids_list[j].SSID_len));
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
		pfn_element.flags = htod32(ENABLE << WL_PFN_HIDDEN_BIT);
		memcpy((char *)pfn_element.ssid.SSID, ssids_list[i].SSID,
			ssids_list[i].SSID_len);
		pfn_element.ssid.SSID_len = ssids_list[i].SSID_len;
		err = dhd_iovar(dhd, 0, "pfn_add", (char *)&pfn_element,
			sizeof(pfn_element), 1);
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

		} else { /* All channels */
			if (skip_dfs && is_dfs(dtoh32(list->element[i])))
				continue;
		}
		d_chan_list[j++] = dtoh32(list->element[i]);
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
	DHD_PNO(("%s enter :  nchan : %d\n", __FUNCTION__, nchan));
	memset(&pfncfg_param, 0, sizeof(wl_pfn_cfg_t));
	/* Setup default values */
	pfncfg_param.reporttype = htod32(WL_PFN_REPORT_ALLNET);
	pfncfg_param.channel_num = htod32(0);

	for (i = 0; i < nchan && nchan < WL_NUMCHANNELS; i++)
		pfncfg_param.channel_list[i] = channel_list[i];

	pfncfg_param.channel_num = htod32(nchan);
	err = dhd_iovar(dhd, 0, "pfn_cfg", (char *)&pfncfg_param, sizeof(pfncfg_param), 1);
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
	err = dhd_iovar(dhd, 0, "pfn_add_bssid", (char *)&p_pfn_bssid,
		sizeof(wl_pfn_bssid_t) * nbssid, 1);
	if (err < 0) {
		DHD_ERROR(("%s : failed to execute pfn_cfg\n", __FUNCTION__));
		goto exit;
	}
exit:
	return err;
}
int
dhd_pno_stop_for_ssid(dhd_pub_t *dhd)
{
	int err = BCME_OK;
	uint32 mode = 0;
	dhd_pno_status_info_t *_pno_state;
	dhd_pno_params_t *_params;
	wl_pfn_bssid_t *p_pfn_bssid;
	NULL_CHECK(dhd, "dev is NULL", err);
	NULL_CHECK(dhd->pno_state, "pno_state is NULL", err);
	_pno_state = PNO_GET_PNOSTATE(dhd);
	if (!(_pno_state->pno_mode & DHD_PNO_LEGACY_MODE)) {
		DHD_ERROR(("%s : LEGACY PNO MODE is not enabled\n", __FUNCTION__));
		goto exit;
	}
	DHD_PNO(("%s enter\n", __FUNCTION__));
	_pno_state->pno_mode &= ~DHD_PNO_LEGACY_MODE;
	/* restart Batch mode  if the batch mode is on */
	if (_pno_state->pno_mode & (DHD_PNO_BATCH_MODE | DHD_PNO_HOTLIST_MODE)) {
		/* retrieve the batching data from firmware into host */
		dhd_pno_get_for_batch(dhd, NULL, 0, PNO_STATUS_DISABLE);
		/* save current pno_mode before calling dhd_pno_clean */
		mode = _pno_state->pno_mode;
		_dhd_pno_clean(dhd);
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
		err = _dhd_pno_clean(dhd);
		if (err < 0) {
			DHD_ERROR(("%s : failed to call _dhd_pno_clean (err: %d)\n",
				__FUNCTION__, err));
			goto exit;
		}
	}
exit:
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

int
dhd_pno_set_for_ssid(dhd_pub_t *dhd, wlc_ssid_t* ssid_list, int nssid,
	uint16  scan_fr, int pno_repeat, int pno_freq_expo_max, uint16 *channel_list, int nchan)
{
	struct dhd_pno_ssid *_pno_ssid;
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
		goto exit;
	}
	DHD_PNO(("%s enter : scan_fr :%d, pno_repeat :%d,"
			"pno_freq_expo_max: %d, nchan :%d\n", __FUNCTION__,
			scan_fr, pno_repeat, pno_freq_expo_max, nchan));

	_params = &(_pno_state->pno_params_arr[INDEX_OF_LEGACY_PARAMS]);
	if (!(_pno_state->pno_mode & DHD_PNO_LEGACY_MODE)) {
		_pno_state->pno_mode |= DHD_PNO_LEGACY_MODE;
		err = _dhd_pno_reinitialize_prof(dhd, _params, DHD_PNO_LEGACY_MODE);
		if (err < 0) {
			DHD_ERROR(("%s : failed to reinitialize profile (err %d)\n",
				__FUNCTION__, err));
			goto exit;
		}
	}
	memset(_chan_list, 0, sizeof(_chan_list));
	tot_nchan = nchan;
	if (tot_nchan > 0 && channel_list) {
		for (i = 0; i < nchan; i++)
		_params->params_legacy.chan_list[i] = _chan_list[i] = channel_list[i];
	}
	if (_pno_state->pno_mode & (DHD_PNO_BATCH_MODE | DHD_PNO_HOTLIST_MODE)) {
		DHD_PNO(("BATCH SCAN is on progress in firmware\n"));
		/* retrieve the batching data from firmware into host */
		dhd_pno_get_for_batch(dhd, NULL, 0, PNO_STATUS_DISABLE);
		/* store current pno_mode before disabling pno */
		mode = _pno_state->pno_mode;
		err = _dhd_pno_enable(dhd, PNO_OFF);
		if (err < 0) {
			DHD_ERROR(("%s : failed to disable PNO\n", __FUNCTION__));
			goto exit;
		}
		/* restore the previous mode */
		_pno_state->pno_mode = mode;
		/* use superset of channel list between two mode */
		if (_pno_state->pno_mode & DHD_PNO_BATCH_MODE) {
			_params2 = &(_pno_state->pno_params_arr[INDEX_OF_BATCH_PARAMS]);
			if (_params2->params_batch.nchan > 0 && nchan > 0) {
				err = _dhd_pno_chan_merge(_chan_list, &tot_nchan,
					&_params2->params_batch.chan_list[0],
					_params2->params_batch.nchan,
					&channel_list[0], nchan);
				if (err < 0) {
					DHD_ERROR(("%s : failed to merge channel list"
					" between legacy and batch\n",
						__FUNCTION__));
					goto exit;
				}
			}  else {
				DHD_PNO(("superset channel will use"
				" all channels in firmware\n"));
			}
		} else if (_pno_state->pno_mode & DHD_PNO_HOTLIST_MODE) {
			_params2 = &(_pno_state->pno_params_arr[INDEX_OF_HOTLIST_PARAMS]);
			if (_params2->params_hotlist.nchan > 0 && nchan > 0) {
				err = _dhd_pno_chan_merge(_chan_list, &tot_nchan,
					&_params2->params_hotlist.chan_list[0],
					_params2->params_hotlist.nchan,
					&channel_list[0], nchan);
				if (err < 0) {
					DHD_ERROR(("%s : failed to merge channel list"
					" between legacy and hotlist\n",
						__FUNCTION__));
					goto exit;
				}
			}
		}
	}
	_params->params_legacy.scan_fr = scan_fr;
	_params->params_legacy.pno_repeat = pno_repeat;
	_params->params_legacy.pno_freq_expo_max = pno_freq_expo_max;
	_params->params_legacy.nchan = nchan;
	_params->params_legacy.nssid = nssid;
	INIT_LIST_HEAD(&_params->params_legacy.ssid_list);
	if ((err = _dhd_pno_set(dhd, _params, DHD_PNO_LEGACY_MODE)) < 0) {
		DHD_ERROR(("failed to set call pno_set (err %d) in firmware\n", err));
		goto exit;
	}
	if ((err = _dhd_pno_add_ssid(dhd, ssid_list, nssid)) < 0) {
		DHD_ERROR(("failed to add ssid list (err %d) in firmware\n", err));
		goto exit;
	}
	for (i = 0; i < nssid; i++) {
		_pno_ssid = kzalloc(sizeof(struct dhd_pno_ssid), GFP_KERNEL);
		if (_pno_ssid == NULL) {
			DHD_ERROR(("%s : failed to allocate struct dhd_pno_ssid\n",
				__FUNCTION__));
			goto exit;
		}
		_pno_ssid->SSID_len = ssid_list[i].SSID_len;
		memcpy(_pno_ssid->SSID, ssid_list[i].SSID, _pno_ssid->SSID_len);
		list_add_tail(&_pno_ssid->list, &_params->params_legacy.ssid_list);

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
		_pno_state->pno_mode &= ~DHD_PNO_LEGACY_MODE;
	return err;
}
int
dhd_pno_set_for_batch(dhd_pub_t *dhd, struct dhd_pno_batch_params *batch_params)
{
	int err = BCME_OK;
	uint16 _chan_list[WL_NUMCHANNELS];
	int rem_nchan = 0, tot_nchan = 0;
	int mode = 0, mscan = 0;
	int i = 0;
	dhd_pno_params_t *_params;
	dhd_pno_params_t *_params2;
	dhd_pno_status_info_t *_pno_state;
	wlc_ssid_t *p_ssid_list = NULL;
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
		struct dhd_pno_ssid *iter, *next;
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
		p_ssid_list = kzalloc(sizeof(wlc_ssid_t) *
							_params2->params_legacy.nssid, GFP_KERNEL);
		if (p_ssid_list == NULL) {
			DHD_ERROR(("%s : failed to allocate wlc_ssid_t array (count: %d)",
				__FUNCTION__, _params2->params_legacy.nssid));
			err = BCME_ERROR;
			_pno_state->pno_mode &= ~DHD_PNO_LEGACY_MODE;
			goto exit;
		}
		i = 0;
		/* convert dhd_pno_ssid to dhd_pno_ssid */
		list_for_each_entry_safe(iter, next, &_params2->params_legacy.ssid_list, list) {
			p_ssid_list[i].SSID_len = iter->SSID_len;
			memcpy(p_ssid_list->SSID, iter->SSID, p_ssid_list[i].SSID_len);
			i++;
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
		goto exit;
	}
	DHD_PNO(("%s enter\n", __FUNCTION__));
	_pno_state = PNO_GET_PNOSTATE(dhd);

	if (!WLS_SUPPORTED(_pno_state)) {
		DHD_ERROR(("%s : wifi location service is not supported\n", __FUNCTION__));
		err = BCME_UNSUPPORTED;
		goto exit;
	}
	if (!(_pno_state->pno_mode & DHD_PNO_BATCH_MODE)) {
		DHD_ERROR(("%s: Batching SCAN mode is not enabled\n", __FUNCTION__));
		goto exit;
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
		err = dhd_iovar(dhd, 0, "pfnlbest", (char *)plbestnet, PNO_BESTNET_LEN, 0);
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
		if (plbestnet->version != PFN_LSCANRESULT_VERSION) {
			err = BCME_VERSION;
			DHD_ERROR(("bestnet version(%d) is mismatch with Driver version(%d)\n",
				plbestnet->version, PFN_LSCANRESULT_VERSION));
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
			if (pbestnet_entry->RSSI > 0) {
				/* if RSSI is positive value, we assume that
				 * this scan is aborted by other scan
				 */
				pbestnet_entry->RSSI *= -1;
				pbestnetheader->reason = (ENABLE << PNO_STATUS_ABORT);
			}
			pbestnet_entry->rtt0 = plnetinfo->rtt0;
			pbestnet_entry->rtt1 = plnetinfo->rtt1;
			pbestnet_entry->timestamp = plnetinfo->timestamp;
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
		if (!list_empty(&_params->params_batch.get_batch.expired_scan_results_list)) {
			err = _dhd_pno_convert_format(dhd, &_params->params_batch, buf, bufsize);
			if (err < 0) {
				DHD_ERROR(("failed to convert the data into upper layer format\n"));
				goto exit;
			}
		}
	}
exit:
	if (plbestnet)
		MFREE(dhd->osh, plbestnet, PNO_BESTNET_LEN);
	_params->params_batch.get_batch.buf = NULL;
	_params->params_batch.get_batch.bufsize = 0;
	mutex_unlock(&_pno_state->pno_mutex);
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
	params_batch = &_pno_state->pno_params_arr[INDEX_OF_BATCH_PARAMS].params_batch;
	_dhd_pno_get_for_batch(dhd, params_batch->get_batch.buf,
		params_batch->get_batch.bufsize, params_batch->get_batch.reason);

}

int
dhd_pno_get_for_batch(dhd_pub_t *dhd, char *buf, int bufsize, int reason)
{
	int err = BCME_OK;
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
	if (!(_pno_state->pno_mode & DHD_PNO_BATCH_MODE)) {
		DHD_ERROR(("%s: Batching SCAN mode is not enabled\n", __FUNCTION__));
		goto exit;
	}
	params_batch->get_batch.buf = buf;
	params_batch->get_batch.bufsize = bufsize;
	params_batch->get_batch.reason = reason;
	schedule_work(&_pno_state->work);
	wait_for_completion(&_pno_state->get_batch_done);
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
	wl_pfn_bssid_t *p_pfn_bssid;
	wlc_ssid_t *p_ssid_list = NULL;
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
	if (!(_pno_state->pno_mode & DHD_PNO_BATCH_MODE)) {
		DHD_ERROR(("%s : PNO BATCH MODE is not enabled\n", __FUNCTION__));
		goto exit;
	}
	_pno_state->pno_mode &= ~DHD_PNO_BATCH_MODE;
	if (_pno_state->pno_mode & (DHD_PNO_LEGACY_MODE | DHD_PNO_HOTLIST_MODE)) {
		mode = _pno_state->pno_mode;
		_dhd_pno_clean(dhd);
		_pno_state->pno_mode = mode;
		/* restart Legacy PNO if the Legacy PNO is on */
		if (_pno_state->pno_mode & DHD_PNO_LEGACY_MODE) {
			struct dhd_pno_legacy_params *_params_legacy;
			struct dhd_pno_ssid *iter, *next;
			_params_legacy =
				&(_pno_state->pno_params_arr[INDEX_OF_LEGACY_PARAMS].params_legacy);
			p_ssid_list = kzalloc(sizeof(wlc_ssid_t) *
				_params_legacy->nssid, GFP_KERNEL);
			if (p_ssid_list == NULL) {
				DHD_ERROR(("%s : failed to allocate wlc_ssid_t array (count: %d)",
					__FUNCTION__, _params_legacy->nssid));
				err = BCME_ERROR;
				_pno_state->pno_mode &= ~DHD_PNO_LEGACY_MODE;
				goto exit;
			}
			i = 0;
			/* convert dhd_pno_ssid to dhd_pno_ssid */
			list_for_each_entry_safe(iter, next, &_params_legacy->ssid_list, list) {
				p_ssid_list[i].SSID_len = iter->SSID_len;
				memcpy(p_ssid_list[i].SSID, iter->SSID, p_ssid_list[i].SSID_len);
				i++;
			}
			err = dhd_pno_set_for_ssid(dhd, p_ssid_list, _params_legacy->nssid,
				_params_legacy->scan_fr, _params_legacy->pno_repeat,
				_params_legacy->pno_freq_expo_max, _params_legacy->chan_list,
				_params_legacy->nchan);
			if (err < 0) {
				_pno_state->pno_mode &= ~DHD_PNO_LEGACY_MODE;
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
		err = _dhd_pno_clean(dhd);
		if (err < 0) {
			DHD_ERROR(("%s : failed to call _dhd_pno_clean (err: %d)\n",
				__FUNCTION__, err));
			goto exit;
		}
	}
exit:
	_params = &_pno_state->pno_params_arr[INDEX_OF_BATCH_PARAMS];
	_dhd_pno_reinitialize_prof(dhd, _params, DHD_PNO_BATCH_MODE);
	if (p_ssid_list)
		kfree(p_ssid_list);
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
	wlc_ssid_t *p_ssid_list;
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
		err = _dhd_pno_clean(dhd);
		if (err < 0) {
			DHD_ERROR(("%s : failed to call _dhd_pno_clean (err: %d)\n",
				__FUNCTION__, err));
			goto exit;
		}
		/* restore previos pno mode */
		_pno_state->pno_mode = mode;
		if (_pno_state->pno_mode & DHD_PNO_LEGACY_MODE) {
			/* restart Legacy PNO Scan */
			struct dhd_pno_legacy_params *_params_legacy;
			struct dhd_pno_ssid *iter, *next;
			_params_legacy =
			&(_pno_state->pno_params_arr[INDEX_OF_LEGACY_PARAMS].params_legacy);
			p_ssid_list =
			kzalloc(sizeof(wlc_ssid_t) * _params_legacy->nssid, GFP_KERNEL);
			if (p_ssid_list == NULL) {
				DHD_ERROR(("%s : failed to allocate wlc_ssid_t array (count: %d)",
					__FUNCTION__, _params_legacy->nssid));
				err = BCME_ERROR;
				_pno_state->pno_mode &= ~DHD_PNO_LEGACY_MODE;
				goto exit;
			}
			/* convert dhd_pno_ssid to dhd_pno_ssid */
			list_for_each_entry_safe(iter, next, &_params_legacy->ssid_list, list) {
				p_ssid_list->SSID_len = iter->SSID_len;
				memcpy(p_ssid_list->SSID, iter->SSID, p_ssid_list->SSID_len);
				p_ssid_list++;
			}
			err = dhd_pno_set_for_ssid(dhd, p_ssid_list, _params_legacy->nssid,
				_params_legacy->scan_fr, _params_legacy->pno_repeat,
				_params_legacy->pno_freq_expo_max, _params_legacy->chan_list,
				_params_legacy->nchan);
			if (err < 0) {
				_pno_state->pno_mode &= ~DHD_PNO_LEGACY_MODE;
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
		err = _dhd_pno_clean(dhd);
		if (err < 0) {
			DHD_ERROR(("%s : failed to call _dhd_pno_clean (err: %d)\n",
				__FUNCTION__, err));
			goto exit;
		}
	}
exit:
	return err;
}

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
	{
		struct dhd_pno_batch_params *params_batch;
		params_batch = &_pno_state->pno_params_arr[INDEX_OF_BATCH_PARAMS].params_batch;
		DHD_PNO(("%s : WLC_E_PFN_BEST_BATCHING\n", __FUNCTION__));
		params_batch->get_batch.buf = NULL;
		params_batch->get_batch.bufsize = 0;
		params_batch->get_batch.reason = PNO_STATUS_EVENT;
		schedule_work(&_pno_state->work);
		break;
	}
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
	NULL_CHECK(dhd, "dhd is NULL", err);
	DHD_PNO(("%s enter\n", __FUNCTION__));
	UNUSED_PARAMETER(_dhd_pno_suspend);
	if (dhd->pno_state)
		goto exit;
	dhd->pno_state = MALLOC(dhd->osh, sizeof(dhd_pno_status_info_t));
	memset(dhd->pno_state, 0, sizeof(dhd_pno_status_info_t));
	NULL_CHECK(dhd, "failed to create dhd_pno_state", err);
	/* need to check whether current firmware support batching and hotlist scan */
	_pno_state = PNO_GET_PNOSTATE(dhd);
	_pno_state->wls_supported = TRUE;
	_pno_state->dhd = dhd;
	mutex_init(&_pno_state->pno_mutex);
	INIT_WORK(&_pno_state->work, _dhd_pno_get_batch_handler);
	init_completion(&_pno_state->get_batch_done);
	err = dhd_iovar(dhd, 0, "pfnlbest", NULL, 0, 0);
	if (err == BCME_UNSUPPORTED) {
		_pno_state->wls_supported = FALSE;
		DHD_INFO(("Current firmware doesn't support"
			" Android Location Service\n"));
	}
exit:
	return err;
}
int dhd_pno_deinit(dhd_pub_t *dhd)
{
	int err = BCME_OK;
	dhd_pno_status_info_t *_pno_state = PNO_GET_PNOSTATE(dhd);
	NULL_CHECK(dhd, "dhd is NULL", err);
	DHD_PNO(("%s enter\n", __FUNCTION__));
	cancel_work_sync(&_pno_state->work);
	if (dhd->pno_state)
		MFREE(dhd->osh, dhd->pno_state, sizeof(dhd_pno_status_info_t));
	dhd->pno_state = NULL;
	return err;
}
