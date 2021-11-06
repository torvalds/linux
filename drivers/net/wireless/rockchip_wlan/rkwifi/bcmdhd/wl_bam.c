/*
 * Bad AP Manager for ADPS
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
#include <linuxver.h>
#include <bcmiov.h>
#include <linux/list_sort.h>
#include <wl_cfg80211.h>
#include <wlioctl.h>
#include <wldev_common.h>
#include <wl_bam.h>

static int
wl_bad_ap_mngr_add_entry(wl_bad_ap_mngr_t *bad_ap_mngr, wl_bad_ap_info_t *bad_ap_info)
{
	unsigned long flags;
	wl_bad_ap_info_entry_t *entry;

	entry = MALLOCZ(bad_ap_mngr->osh, sizeof(*entry));
	if (entry == NULL) {
		WL_ERR(("%s: allocation for list failed\n", __FUNCTION__));
		return BCME_NOMEM;
	}

	memcpy(&entry->bad_ap, bad_ap_info, sizeof(entry->bad_ap));
	INIT_LIST_HEAD(&entry->list);
	spin_lock_irqsave(&bad_ap_mngr->lock, flags);
	list_add_tail(&entry->list, &bad_ap_mngr->list);
	spin_unlock_irqrestore(&bad_ap_mngr->lock, flags);

	bad_ap_mngr->num++;

	return BCME_OK;
}

#if !defined(DHD_ADPS_BAM_EXPORT)
#define WL_BAD_AP_INFO_FILE_PATH	PLATFORM_PATH".bad_ap_list.info"
#define WL_BAD_AP_MAX_BUF_SIZE		1024u

/* Bad AP information data format
 *
 * Status and Reason: come from event
 * Connection count: Increase connecting Bad AP
 *
 * BSSID,year-month-day hour:min:sec,Status,Reason,Connection count
 * ex) XX:XX:XX:XX:XX:XX,1970-01-01 00:00:00,1,2,1
 *
 */
#define WL_BAD_AP_INFO_FMT \
	"%02x:%02x:%02x:%02x:%02x:%02x,%04ld-%02d-%02d %02d:%02d:%02d,%u,%u,%u\n"
#define WL_BAD_AP_INFO_FMT_ITEM_CNT	15u

static inline void
wl_bad_ap_mngr_tm2ts(struct timespec *ts, const struct tm tm)
{
	ts->tv_sec = mktime(tm.tm_year, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	ts->tv_nsec = 0;
}

/* Ignore compiler warnings due to -Werror=cast-qual */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
static int
wl_bad_ap_mngr_timecmp(void *priv, struct list_head *a, struct list_head *b)
{
	int ret;

	struct timespec ts1;
	struct timespec ts2;

	wl_bad_ap_info_entry_t *e1 = CONTAINEROF(a, wl_bad_ap_info_entry_t, list);
	wl_bad_ap_info_entry_t *e2 = CONTAINEROF(b, wl_bad_ap_info_entry_t, list);

	wl_bad_ap_mngr_tm2ts(&ts1, e1->bad_ap.tm);
	wl_bad_ap_mngr_tm2ts(&ts2, e2->bad_ap.tm);

	ret = timespec_compare((const struct timespec *)&ts1, (const struct timespec *)&ts2);

	return ret;
}

static void
wl_bad_ap_mngr_update(struct bcm_cfg80211 *cfg, wl_bad_ap_info_t *bad_ap_info)
{
	wl_bad_ap_info_entry_t *entry;
	unsigned long flags;

	if (list_empty(&cfg->bad_ap_mngr.list)) {
		return;
	}

	WL_CFG_BAM_LOCK(&cfg->bad_ap_mngr.lock, flags);
	/* sort by timestamp */
	list_sort(NULL, &cfg->bad_ap_mngr.list, wl_bad_ap_mngr_timecmp);

	/* update entry with the latest bad ap information */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	entry = list_first_entry(&cfg->bad_ap_mngr.list, wl_bad_ap_info_entry_t, list);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	if (entry != NULL) {
		memcpy(&entry->bad_ap, bad_ap_info, sizeof(entry->bad_ap));
	}
	WL_CFG_BAM_UNLOCK(&cfg->bad_ap_mngr.lock, flags);
}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static inline int
wl_bad_ap_mngr_fread_bad_ap_info(char *buf, int buf_len, wl_bad_ap_info_t *bad_ap)
{
	return snprintf(buf, buf_len, WL_BAD_AP_INFO_FMT,
			bad_ap->bssid.octet[0], bad_ap->bssid.octet[1],
			bad_ap->bssid.octet[2], bad_ap->bssid.octet[3],
			bad_ap->bssid.octet[4], bad_ap->bssid.octet[5],
			bad_ap->tm.tm_year + 1900, bad_ap->tm.tm_mon + 1, bad_ap->tm.tm_mday,
			bad_ap->tm.tm_hour, bad_ap->tm.tm_min, bad_ap->tm.tm_sec,
			bad_ap->status, bad_ap->reason, bad_ap->connect_count);
}

static int
wl_bad_ap_mngr_fparse(struct bcm_cfg80211 *cfg, struct file *fp)
{
	int len;
	int pos = 0;
	char tmp[128];
	int ret = BCME_ERROR;

	wl_bad_ap_info_t bad_ap;
	char *buf = NULL;

	buf = MALLOCZ(cfg->osh, WL_BAD_AP_MAX_BUF_SIZE);
	if (buf == NULL) {
		WL_ERR(("%s: allocation for buf failed\n", __FUNCTION__));
		return BCME_NOMEM;
	}

	ret = vfs_read(fp, buf, WL_BAD_AP_MAX_BUF_SIZE, &fp->f_pos);
	if (ret  < 0) {
		WL_ERR(("%s: file read failed (%d)\n", __FUNCTION__, ret));
		goto fail;
	}

	len = ret;
	do {
		ret = sscanf(&buf[pos], WL_BAD_AP_INFO_FMT,
				(uint32 *)&bad_ap.bssid.octet[0], (uint32 *)&bad_ap.bssid.octet[1],
				(uint32 *)&bad_ap.bssid.octet[2], (uint32 *)&bad_ap.bssid.octet[3],
				(uint32 *)&bad_ap.bssid.octet[4], (uint32 *)&bad_ap.bssid.octet[5],
				(long int *)&bad_ap.tm.tm_year, (uint32 *)&bad_ap.tm.tm_mon,
				(uint32 *)&bad_ap.tm.tm_mday, (uint32 *)&bad_ap.tm.tm_hour,
				(uint32 *)&bad_ap.tm.tm_min, (uint32 *)&bad_ap.tm.tm_sec,
				(uint32 *)&bad_ap.status, (uint32 *)&bad_ap.reason,
				(uint32 *)&bad_ap.connect_count);
		if (ret != WL_BAD_AP_INFO_FMT_ITEM_CNT) {
			WL_ERR(("%s: file parse failed(expected: %d actual: %d)\n",
				__FUNCTION__, WL_BAD_AP_INFO_FMT_ITEM_CNT, ret));
			ret = BCME_ERROR;
			goto fail;
		}

		/* convert struct tm format */
		bad_ap.tm.tm_year -= 1900;
		bad_ap.tm.tm_mon -= 1;

		ret = wl_bad_ap_mngr_add(&cfg->bad_ap_mngr, &bad_ap);
		if (ret < 0) {
			WL_ERR(("%s: bad ap add failed\n", __FUNCTION__));
			goto fail;
		}

		ret = wl_bad_ap_mngr_fread_bad_ap_info(tmp, sizeof(tmp), &bad_ap);
		if (ret < 0) {
			WL_ERR(("%s: wl_bad_ap_mngr_fread_bad_ap_info failed (%d)\n",
				__FUNCTION__, ret));
			goto fail;
		}

		if (cfg->bad_ap_mngr.num >= WL_BAD_AP_MAX_ENTRY_NUM) {
			break;
		}

		len -= ret;
		pos += ret;
	} while (len > 0);

	ret = BCME_OK;

fail:
	if (buf) {
		MFREE(cfg->osh, buf, WL_BAD_AP_MAX_BUF_SIZE);
	}

	return ret;
}

static int
wl_bad_ap_mngr_fread(struct bcm_cfg80211 *cfg, const char *fname)
{
	int ret = BCME_ERROR;

	mm_segment_t fs;
	struct file *fp = NULL;

	if (fname == NULL) {
		WL_ERR(("%s: fname is NULL\n", __FUNCTION__));
		return ret;
	}
	mutex_lock(&cfg->bad_ap_mngr.fs_lock);

	fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(fname, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		fp = NULL;
		WL_ERR(("%s: file open failed(%d)\n", __FUNCTION__, ret));
		goto fail;
	}

	if ((ret = wl_bad_ap_mngr_fparse(cfg, fp)) < 0) {
		goto fail;
	}
fail:
	if (fp) {
		filp_close(fp, NULL);
	}
	set_fs(fs);

	mutex_unlock(&cfg->bad_ap_mngr.fs_lock);

	return ret;
}

static int
wl_bad_ap_mngr_fwrite(struct bcm_cfg80211 *cfg, const char *fname)
{
	int ret = BCME_ERROR;

	mm_segment_t fs;
	struct file *fp = NULL;

	int len = 0;
	char tmp[WL_BAD_AP_MAX_BUF_SIZE];
	wl_bad_ap_info_t *bad_ap;
	wl_bad_ap_info_entry_t *entry;

	if (list_empty(&cfg->bad_ap_mngr.list)) {
		return BCME_ERROR;
	}

	if (fname == NULL) {
		return BCME_NOTFOUND;
	}

	mutex_lock(&cfg->bad_ap_mngr.fs_lock);

	fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(fname, O_CREAT | O_RDWR | O_TRUNC,  0666);
	if (IS_ERR(fp)) {
		ret = PTR_ERR(fp);
		WL_ERR(("%s: file open failed(%d)\n", __FUNCTION__, ret));
		fp = NULL;
		goto fail;
	}

	memset(tmp, 0, sizeof(tmp));
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	list_for_each_entry(entry, &cfg->bad_ap_mngr.list, list) {
		bad_ap = &entry->bad_ap;
		ret = wl_bad_ap_mngr_fread_bad_ap_info(&tmp[len], sizeof(tmp) - len, bad_ap);
		if (ret < 0) {
			WL_ERR(("%s: snprintf failed(%d)\n", __FUNCTION__, ret));
			goto fail;
		}

		len += ret;
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

	ret = vfs_write(fp, tmp, len, &fp->f_pos);
	if (ret < 0) {
		WL_ERR(("%s: file write failed(%d)\n", __FUNCTION__, ret));
		goto fail;
	}
	/* Sync file from filesystem to physical media */
	ret = vfs_fsync(fp, 0);
	if (ret < 0) {
		WL_ERR(("%s: sync file failed(%d)\n", __FUNCTION__, ret));
		goto fail;
	}
	ret = BCME_OK;
fail:
	if (fp) {
		filp_close(fp, NULL);
	}
	set_fs(fs);
	mutex_unlock(&cfg->bad_ap_mngr.fs_lock);

	return ret;
}
#else
extern wl_bad_ap_mngr_t *g_bad_ap_mngr;
#endif	/* DHD_ADPS_BAM_EXPORT */

wl_bad_ap_info_entry_t*
wl_bad_ap_mngr_find(wl_bad_ap_mngr_t *bad_ap_mngr, const struct ether_addr *bssid)
{
	wl_bad_ap_info_entry_t *entry;
	unsigned long flags;

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	spin_lock_irqsave(&bad_ap_mngr->lock, flags);
	list_for_each_entry(entry, &bad_ap_mngr->list, list) {
		if (!memcmp(&entry->bad_ap.bssid.octet, bssid->octet, ETHER_ADDR_LEN)) {
			spin_unlock_irqrestore(&bad_ap_mngr->lock, flags);
			return entry;
		}
	}
	spin_unlock_irqrestore(&bad_ap_mngr->lock, flags);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	return NULL;
}

int
wl_bad_ap_mngr_add(wl_bad_ap_mngr_t *bad_ap_mngr, wl_bad_ap_info_t *bad_ap_info)
{
	int ret;
	wl_bad_ap_info_entry_t *entry;
	unsigned long flags;

	BCM_REFERENCE(entry);
	BCM_REFERENCE(flags);

#if !defined(DHD_ADPS_BAM_EXPORT)
	ret = wl_bad_ap_mngr_add_entry(bad_ap_mngr, bad_ap_info);
#else
	if (bad_ap_mngr->num == WL_BAD_AP_MAX_ENTRY_NUM) {
		/* Remove the oldest entry if entry list is full */
		spin_lock_irqsave(&bad_ap_mngr->lock, flags);
		list_del(bad_ap_mngr->list.next);
		bad_ap_mngr->num--;
		spin_unlock_irqrestore(&bad_ap_mngr->lock, flags);
	}

	/* delete duplicated entry to update it at tail to keep the odrer */
	entry = wl_bad_ap_mngr_find(bad_ap_mngr, &bad_ap_info->bssid);
	if (entry != NULL) {
		spin_lock_irqsave(&bad_ap_mngr->lock, flags);
		list_del(&entry->list);
		bad_ap_mngr->num--;
		spin_unlock_irqrestore(&bad_ap_mngr->lock, flags);
	}

	ret = wl_bad_ap_mngr_add_entry(bad_ap_mngr, bad_ap_info);
	if (ret < 0) {
		WL_ERR(("%s - fail to add bad ap data(%d)\n", __FUNCTION__, ret));
		return ret;
	}
#endif	/* DHD_ADPS_BAM_EXPORT */
	return ret;
}

void
wl_bad_ap_mngr_deinit(struct bcm_cfg80211 *cfg)
{
	wl_bad_ap_info_entry_t *entry;
	unsigned long flags;

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	WL_CFG_BAM_LOCK(&cfg->bad_ap_mngr.lock, flags);
	while (!list_empty(&cfg->bad_ap_mngr.list)) {
		entry = list_entry(cfg->bad_ap_mngr.list.next, wl_bad_ap_info_entry_t, list);
		if (entry) {
			list_del(&cfg->bad_ap_mngr.list);
			MFREE(cfg->osh, entry, sizeof(*entry));
		}
	}
	WL_CFG_BAM_UNLOCK(&cfg->bad_ap_mngr.lock, flags);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#if !defined(DHD_ADPS_BAM_EXPORT)
	mutex_destroy(&cfg->bad_ap_mngr.fs_lock);
#endif	/* !DHD_ADPS_BAM_EXPORT */
}

void
wl_bad_ap_mngr_init(struct bcm_cfg80211 *cfg)
{
	cfg->bad_ap_mngr.osh = cfg->osh;
	cfg->bad_ap_mngr.num = 0;

	spin_lock_init(&cfg->bad_ap_mngr.lock);
	INIT_LIST_HEAD(&cfg->bad_ap_mngr.list);

#if !defined(DHD_ADPS_BAM_EXPORT)
	mutex_init(&cfg->bad_ap_mngr.fs_lock);
#else
	g_bad_ap_mngr = &cfg->bad_ap_mngr;
#endif	/* !DHD_ADPS_BAM_EXPORT */
}

static int
wl_event_adps_bad_ap_mngr(struct bcm_cfg80211 *cfg, void *data)
{
	int ret = BCME_OK;

	wl_event_adps_t *event_data = (wl_event_adps_t *)data;
	wl_event_adps_bad_ap_t *bad_ap_data;

	wl_bad_ap_info_entry_t *entry;
	wl_bad_ap_info_t temp;
#if !defined(DHD_ADPS_BAM_EXPORT)
	struct timespec ts;
#endif	/* !DHD_ADPS_BAM_EXPORT */

	if (event_data->version != WL_EVENT_ADPS_VER_1) {
		return BCME_VERSION;
	}

	if (event_data->length != (OFFSETOF(wl_event_adps_t, data) + sizeof(*bad_ap_data))) {
		return BCME_ERROR;
	}

	BCM_REFERENCE(ret);
	BCM_REFERENCE(entry);
	bad_ap_data = (wl_event_adps_bad_ap_t *)event_data->data;

#if !defined(DHD_ADPS_BAM_EXPORT)
	/* Update Bad AP list */
	if (list_empty(&cfg->bad_ap_mngr.list)) {
		wl_bad_ap_mngr_fread(cfg, WL_BAD_AP_INFO_FILE_PATH);
	}

	getnstimeofday(&ts);
	entry = wl_bad_ap_mngr_find(&cfg->bad_ap_mngr, &bad_ap_data->ea);
	if (entry != NULL) {
		time_to_tm((ts.tv_sec - (sys_tz.tz_minuteswest * 60)), 0, &entry->bad_ap.tm);
		entry->bad_ap.status = bad_ap_data->status;
		entry->bad_ap.reason = bad_ap_data->reason;
		entry->bad_ap.connect_count++;
	}
	else {
		time_to_tm((ts.tv_sec - (sys_tz.tz_minuteswest * 60)), 0, &temp.tm);
		temp.status = bad_ap_data->status;
		temp.reason = bad_ap_data->reason;
		temp.connect_count = 1;
		memcpy(temp.bssid.octet, &bad_ap_data->ea.octet, ETHER_ADDR_LEN);

		if (cfg->bad_ap_mngr.num < WL_BAD_AP_MAX_ENTRY_NUM) {
			wl_bad_ap_mngr_add(&cfg->bad_ap_mngr, &temp);
		}
		else {
			wl_bad_ap_mngr_update(cfg, &temp);
		}
	}

	wl_bad_ap_mngr_fwrite(cfg, WL_BAD_AP_INFO_FILE_PATH);
#else
	memcpy(temp.bssid.octet, &bad_ap_data->ea.octet, ETHER_ADDR_LEN);
	ret = wl_bad_ap_mngr_add(&cfg->bad_ap_mngr, &temp);
#endif	/* !DHD_ADPS_BAM_EXPORT */

	return ret;
}

static int
wl_adps_get_mode(struct net_device *ndev, uint8 band)
{
	int len;
	int ret;

	uint8 *pdata;
	char buf[WLC_IOCTL_SMLEN];

	bcm_iov_buf_t iov_buf;
	bcm_iov_buf_t *resp;
	wl_adps_params_v1_t *data = NULL;

	memset(&iov_buf, 0, sizeof(iov_buf));
	len = OFFSETOF(bcm_iov_buf_t, data) + sizeof(band);

	iov_buf.version = WL_ADPS_IOV_VER;
	iov_buf.len = sizeof(band);
	iov_buf.id = WL_ADPS_IOV_MODE;
	pdata = (uint8 *)iov_buf.data;
	*pdata = band;

	ret = wldev_iovar_getbuf(ndev, "adps", &iov_buf, len, buf, WLC_IOCTL_SMLEN, NULL);
	if (ret < 0) {
		return ret;
	}
	resp = (bcm_iov_buf_t *)buf;
	data = (wl_adps_params_v1_t *)resp->data;

	return data->mode;
}

/*
 * Return value:
 *  Disabled: 0
 *  Enabled: bitmap of WLC_BAND_2G or WLC_BAND_5G when ADPS is enabled at each BAND
 *
 */
int
wl_adps_enabled(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	uint8 i;
	int mode;
	int ret = 0;

	for (i = 1; i <= MAX_BANDS; i++) {
		mode = wl_adps_get_mode(ndev, i);
		if (mode > 0) {
			ret |= (1 << i);
		}
	}

	return ret;
}

int
wl_adps_set_suspend(struct bcm_cfg80211 *cfg, struct net_device *ndev, uint8 suspend)
{
	int ret = BCME_OK;

	int buf_len;
	bcm_iov_buf_t *iov_buf = NULL;
	wl_adps_suspend_v1_t *data = NULL;

	buf_len = OFFSETOF(bcm_iov_buf_t, data) + sizeof(*data);
	iov_buf = MALLOCZ(cfg->osh, buf_len);
	if (iov_buf == NULL) {
		WL_ERR(("%s - failed to alloc %d bytes for iov_buf\n",
			__FUNCTION__, buf_len));
		ret = BCME_NOMEM;
		goto exit;
	}

	iov_buf->version = WL_ADPS_IOV_VER;
	iov_buf->len = sizeof(*data);
	iov_buf->id = WL_ADPS_IOV_SUSPEND;

	data = (wl_adps_suspend_v1_t *)iov_buf->data;
	data->version = ADPS_SUB_IOV_VERSION_1;
	data->length = sizeof(*data);
	data->suspend = suspend;

	ret = wldev_iovar_setbuf(ndev, "adps", (char *)iov_buf, buf_len,
		cfg->ioctl_buf, WLC_IOCTL_SMLEN, NULL);
	if (ret < 0) {
		if (ret == BCME_UNSUPPORTED) {
			WL_ERR(("%s - adps suspend is not supported\n", __FUNCTION__));
			ret = BCME_OK;
		}
		else {
			WL_ERR(("%s - fail to set adps suspend %d (%d)\n",
				__FUNCTION__, suspend, ret));
		}
		goto exit;
	}
	WL_INFORM_MEM(("[%s] Detect BAD AP and Suspend ADPS\n",	ndev->name));
exit:
	if (iov_buf) {
		MFREE(cfg->osh, iov_buf, buf_len);
	}
	return ret;
}

bool
wl_adps_bad_ap_check(struct bcm_cfg80211 *cfg, const struct ether_addr *bssid)
{
#if !defined(DHD_ADPS_BAM_EXPORT)
	/* Update Bad AP list */
	if (list_empty(&cfg->bad_ap_mngr.list)) {
		wl_bad_ap_mngr_fread(cfg, WL_BAD_AP_INFO_FILE_PATH);
	}
#endif	/* DHD_ADPS_BAM_EXPORT */

	if (wl_bad_ap_mngr_find(&cfg->bad_ap_mngr, bssid) != NULL)
		return TRUE;

	return FALSE;
}

s32
wl_adps_event_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	int ret = BCME_OK;
	wl_event_adps_t *event_data = (wl_event_adps_t *)data;

	switch (event_data->type) {
	case WL_E_TYPE_ADPS_BAD_AP:
		ret = wl_event_adps_bad_ap_mngr(cfg, data);
		break;
	default:
		break;
	}

	return ret;
}
