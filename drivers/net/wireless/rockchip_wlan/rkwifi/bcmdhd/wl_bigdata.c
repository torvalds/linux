/*
 * Bigdata logging and report. None EWP and Hang event.
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
#include <dngl_stats.h>
#include <bcmutils.h>
#include <dhd.h>
#include <dhd_dbg.h>
#include <wl_cfg80211.h>
#include <wldev_common.h>
#include <bcmendian.h>
#include <wlioctl.h>
#include <dhd_linux_wq.h>
#include <wl_bigdata.h>

#define WL_AP_BIGDATA_LOG(args) WL_DBG(args)

#define WLC_E_IS_ASSOC(e, r) \
	(((e == WLC_E_ASSOC_IND) || (e == WLC_E_REASSOC_IND)) && r == DOT11_SC_SUCCESS)
#define WLC_E_IS_DEAUTH(e) \
	(e == WLC_E_DISASSOC_IND || e == WLC_E_DEAUTH_IND || e == WLC_E_DEAUTH)

static void dump_ap_stadata(wl_ap_sta_data_t *ap_sta_data);
static inline void copy_ap_stadata(wl_ap_sta_data_t *dest, wl_ap_sta_data_t *src);
static void wg_rate_dot11mode(uint32 *rate, uint8 *channel, uint32 *mode_80211);
static void wg_ht_mimo_ant(uint32 *nss, wl_rateset_args_t *rateset);
static void wg_vht_mimo_ant(uint32 *nss, wl_rateset_args_t *rateset);
#if defined(WL11AX)
static void wg_he_mimo_ant(uint32 *nss, uint16 *mcsset);
#endif /* WL11AX */
static int wg_parse_ap_stadata(struct net_device *dev, struct ether_addr *sta_mac,
		wl_ap_sta_data_t *ap_sta_data);

static void
dump_ap_stadata(wl_ap_sta_data_t *ap_sta_data)
{
	int i;

	if (!ap_sta_data) {
		WL_AP_BIGDATA_LOG(("ap_sta_data is NULL\n"));
		return;
	}

	for (i = 0; i < MAX_STA_INFO_AP_CNT; i++) {
		if (!ap_sta_data[i].is_empty) {
			WL_AP_BIGDATA_LOG(("idx %d "MACDBG" dis %d empty %d\n",
				i, MAC2STRDBG((char *)&ap_sta_data[i].mac),
				ap_sta_data[i].disconnected, ap_sta_data[i].is_empty));
			WL_AP_BIGDATA_LOG(("mode %d nss %d chanspec %d rssi %d "
						"rate %d reason_code %d\n\n",
						ap_sta_data[i].mode_80211,
						ap_sta_data[i].nss, ap_sta_data[i].chanspec,
						ap_sta_data[i].rssi, ap_sta_data[i].rate,
						ap_sta_data[i].reason_code));
		}
	}
}

static inline void
copy_ap_stadata(wl_ap_sta_data_t *dest, wl_ap_sta_data_t *src)
{
	memcpy(dest, src, sizeof(wl_ap_sta_data_t));
	dest->is_empty = FALSE;
	dest->disconnected = FALSE;
	dest->reason_code = 0;
}

static void
get_copy_ptr_stadata(struct ether_addr *sta_mac, wl_ap_sta_data_t *sta_data,
		uint32 *sta_list_cnt, void **data)
{
	int i;
	int discon_idx = -1;
	int empty_idx = -1;

	if  (!sta_mac || !sta_data || !sta_list_cnt ||!data) {
		WL_ERR(("sta_mac=%p sta_data=%p sta_lit_cnt=%p data=%p\n",
			sta_mac, sta_data, sta_list_cnt, data));
		return;
	}

	/* Find already existed sta */
	for (i = 0; i < MAX_STA_INFO_AP_CNT; i++) {
		if (!memcmp((char*)sta_mac, (char*)&sta_data[i].mac, ETHER_ADDR_LEN)) {
			WL_AP_BIGDATA_LOG(("found existed "
						"STA idx %d "MACDBG"\n",
						i, MAC2STRDBG((char *)sta_mac)));
			*data = (wl_ap_sta_data_t *)&sta_data[i];
			return;
		}

		if (sta_data[i].disconnected && (discon_idx == -1)) {
			discon_idx = i;
		}

		if (sta_data[i].is_empty && (empty_idx == -1)) {
			empty_idx = i;
		}
	}

	/* Buf is max */
	if (*sta_list_cnt >= MAX_STA_INFO_AP_CNT) {
		if (discon_idx != -1) {
			WL_AP_BIGDATA_LOG(("delete disconnected "
						"idx %d "MACDBG"\n",
						discon_idx, MAC2STRDBG((char *)sta_mac)));
			*data = (wl_ap_sta_data_t *)&sta_data[discon_idx];
			return;
		}
	}

	/* Buf is not max */
	if (empty_idx != -1) {
		(*sta_list_cnt)++;
		WL_AP_BIGDATA_LOG(("empty idx %d \n", empty_idx));
		*data = (wl_ap_sta_data_t *)&sta_data[empty_idx];
		return;
	}
}

static void
wg_rate_dot11mode(uint32 *rate, uint8 *channel, uint32 *mode_80211)
{
	if (*rate <= DOT11_11B_MAX_RATE) {
		/* 11b maximum rate is 11Mbps. 11b mode */
		*mode_80211 = BIGDATA_DOT11_11B_MODE;
	} else {
		/* It's not HT Capable case. */
		if (*channel > DOT11_2GHZ_MAX_CH_NUM) {
			*mode_80211 = BIGDATA_DOT11_11A_MODE; /* 11a mode */
		} else {
			*mode_80211 = BIGDATA_DOT11_11G_MODE; /* 11g mode */
		}
	}
}

static void
wg_ht_mimo_ant(uint32 *nss, wl_rateset_args_t *rateset)
{
	int i;

	*nss = 0;
	for (i = 0; i < MAX_STREAMS_SUPPORTED; i++) {
		int8 bitmap = DOT11_HT_MCS_RATE_MASK;
		if (i == MAX_STREAMS_SUPPORTED-1) {
			bitmap = DOT11_RATE_MASK;
		}
		if (rateset->mcs[i] & bitmap) {
			(*nss)++;
		}
	}
}

static void
wg_vht_mimo_ant(uint32 *nss, wl_rateset_args_t *rateset)
{
	int i;
	uint32 mcs_code;

	*nss = 0;

	for (i = 1; i <= VHT_CAP_MCS_MAP_NSS_MAX; i++) {
		mcs_code = VHT_MCS_MAP_TO_MCS_CODE(rateset->vht_mcs[i - 1]);
		if (mcs_code != VHT_CAP_MCS_MAP_NONE) {
			(*nss)++;
		}
	}
}

#if defined(WL11AX)
static void
wg_he_mimo_ant(uint32 *nss, uint16 *mcsset)
{
	int i;

	*nss = 0;

	for (i = 0; i <= HE_MCS_MAP_NSS_MAX; i++) {
		if (mcsset[i]) {
			(*nss)++;
		}
	}
}
#endif /* WL11AX */

static int
wg_parse_ap_stadata(struct net_device *dev, struct ether_addr *sta_mac,
		wl_ap_sta_data_t *ap_sta_data)
{
	sta_info_v4_t *sta_v4 = NULL;
	sta_info_v5_t *sta_v5 = NULL;
	wl_rateset_args_t *rateset_adv;
	int ret = BCME_OK;
	char* ioctl_buf = NULL;
#if defined(WL11AX)
	struct wl_rateset_args_v2 *rateset_adv_v2;
#endif

	ioctl_buf = (char*)kmalloc(WLC_IOCTL_MEDLEN, GFP_KERNEL);
	if (ioctl_buf == NULL) {
		WL_ERR(("failed to allocated ioctl_buf \n"));
		return BCME_ERROR;
	}

	ret = wldev_iovar_getbuf(dev, "sta_info", (struct ether_addr *)sta_mac,
			ETHER_ADDR_LEN, ioctl_buf, WLC_IOCTL_MEDLEN, NULL);

	if (ret < 0) {
		WL_ERR(("sta_info err value :%d\n", ret));
		ret = BCME_ERROR;
		goto done;
	}

	sta_v4 = (sta_info_v4_t *)ioctl_buf;
	ap_sta_data->mac = *sta_mac;
	ap_sta_data->rssi = 0;
	ap_sta_data->mimo = 0;

	rateset_adv = &sta_v4->rateset_adv;
	ap_sta_data->chanspec = sta_v4->chanspec;

	WL_AP_BIGDATA_LOG(("sta_info ver %d\n", sta_v4->ver));

	if (sta_v4->ver == WL_STA_VER_5) {
		sta_v5 = (sta_info_v5_t *)ioctl_buf;
		ap_sta_data->chanspec = sta_v5->chanspec;
		rateset_adv = &sta_v5->rateset_adv;
	}

	ap_sta_data->channel = wf_chspec_ctlchan(ap_sta_data->chanspec);
	ap_sta_data->rate =
		(sta_v4->rateset.rates[sta_v4->rateset.count - 1] & DOT11_RATE_MASK) / 2;

	if (sta_v4->vht_flags) {
		ap_sta_data->mode_80211 = BIGDATA_DOT11_11AC_MODE;
		wg_vht_mimo_ant(&ap_sta_data->nss, rateset_adv);
	} else if (sta_v4->ht_capabilities) {
		ap_sta_data->mode_80211 = BIGDATA_DOT11_11N_MODE;
		wg_ht_mimo_ant(&ap_sta_data->nss, rateset_adv);
	} else {
		wg_rate_dot11mode(&ap_sta_data->rate, &ap_sta_data->channel,
				&ap_sta_data->mode_80211);
	}

#if defined(WL11AX)
	ret = wldev_iovar_getbuf(dev, "rateset", NULL, 0, ioctl_buf,
			sizeof(wl_rateset_args_v2_t), NULL);
	if (ret < 0) {
		WL_ERR(("get rateset failed = %d\n", ret));
	} else {
		rateset_adv_v2 = (wl_rateset_args_v2_t *)ioctl_buf;
		WL_AP_BIGDATA_LOG(("rateset ver %d\n", rateset_adv_v2->version));

		if (rateset_adv_v2->version == RATESET_ARGS_V2) {
			rateset_adv_v2 = (wl_rateset_args_v2_t *)&sta_v4->rateset_adv;
			if (sta_v4->ver == WL_STA_VER_5) {
				rateset_adv_v2 = (wl_rateset_args_v2_t *)&sta_v5->rateset_adv;
			}

			if (rateset_adv_v2->he_mcs[0]) {
				WL_AP_BIGDATA_LOG(("there is he mcs rate\n"));
				ap_sta_data->mode_80211 = BIGDATA_DOT11_11AX_MODE;
				wg_he_mimo_ant(&ap_sta_data->nss, &rateset_adv_v2->he_mcs[0]);
			}
		}
	}
#endif /* WL11AX */

	if (ap_sta_data->nss) {
		ap_sta_data->nss = ap_sta_data->nss - 1;
	}

done:
	if (ioctl_buf) {
		kfree(ioctl_buf);
	}

	return ret;
}

void
wl_gather_ap_stadata(void *handle, void *event_info, u8 event)
{
	u32 event_type = 0;
	u32 reason = 0;
	u32 status = 0;
	struct ether_addr sta_mac;
	dhd_pub_t *dhdp;

	struct net_device *dev = NULL;
	struct bcm_cfg80211 *cfg = NULL;
	wl_event_msg_t *e;

	wl_ap_sta_data_t *sta_data;
	wl_ap_sta_data_t temp_sta_data;
	void *data = NULL;
	int i;
	int ret;

	ap_sta_wq_data_t *wq_event_data = event_info;
	if (!wq_event_data) {
		WL_ERR(("wq_event_data is NULL\n"));
		return;
	}

	cfg = (struct bcm_cfg80211 *)wq_event_data->bcm_cfg;
	if (!cfg || !cfg->ap_sta_info) {
		WL_ERR(("cfg=%p ap_sta_info=%p\n", cfg, (cfg ? cfg->ap_sta_info : NULL)));
		if (wq_event_data) {
			kfree(wq_event_data);
		}
		return;
	}

	mutex_lock(&cfg->ap_sta_info->wq_data_sync);

	dhdp = (dhd_pub_t *)cfg->pub;
	e = &wq_event_data->e;
	dev = (struct net_device *)wq_event_data->ndev;

	if (!e || !dev) {
		WL_ERR(("e=%p dev=%p\n", e, dev));
		goto done;
	}

	if (!wl_get_drv_status(cfg, AP_CREATED, dev)) {
		WL_ERR(("skip to gather data becasue interface is not available\n"));
		goto done;
	}

	sta_data = cfg->ap_sta_info->ap_sta_data;

	event_type = ntoh32(e->event_type);
	reason = ntoh32(e->reason);
	status = ntoh32(e->status);
	sta_mac = e->addr;

	if (!sta_data) {
		WL_ERR(("sta_data is NULL\n"));
		goto done;
	}

	WL_AP_BIGDATA_LOG((""MACDBG" event %d status %d reason %d\n",
		MAC2STRDBG((char*)&sta_mac), event_type, status, reason));

	if (WLC_E_IS_ASSOC(event_type, reason)) {
		ret = wg_parse_ap_stadata(dev, &sta_mac, &temp_sta_data);
		if (ret < 0) {
			WL_AP_BIGDATA_LOG(("sta_info err value :%d\n", ret));
			goto done;
		}

		if (cfg->ap_sta_info->sta_list_cnt == 0) {
			copy_ap_stadata(&sta_data[0], &temp_sta_data);
			cfg->ap_sta_info->sta_list_cnt++;
			dump_ap_stadata(sta_data);
		} else {
			get_copy_ptr_stadata(&sta_mac, sta_data,
					&cfg->ap_sta_info->sta_list_cnt, &data);
			if (data != NULL) {
				copy_ap_stadata((wl_ap_sta_data_t *)data, &temp_sta_data);
				dump_ap_stadata(sta_data);
			}
		}
	}

	if (WLC_E_IS_DEAUTH(event_type)) {
		/* Find already existed sta */
		for (i = 0; i < MAX_STA_INFO_AP_CNT; i++) {
			if (!sta_data[i].is_empty &&
				!memcmp((char*)&sta_mac, (char*)&sta_data[i].mac, ETHER_ADDR_LEN)) {
				WL_AP_BIGDATA_LOG(("found disconnected "
							"STA idx %d "MACDBG"\n",
							i, MAC2STRDBG((char *)&sta_mac)));
				sta_data[i].is_empty = FALSE;
				sta_data[i].disconnected = TRUE;
				sta_data[i].reason_code = reason;
				dump_ap_stadata(sta_data);
				goto done;
			}
		}
	}

done:
	if (wq_event_data) {
		ASSERT(dhdp->osh);
		MFREE(dhdp->osh, wq_event_data, sizeof(ap_sta_wq_data_t));
	}
	mutex_unlock(&cfg->ap_sta_info->wq_data_sync);
}

int
wl_attach_ap_stainfo(void *bcm_cfg)
{
	gfp_t kflags;
	uint32 alloc_len;
	wl_ap_sta_info_t *sta_info;
	wl_ap_sta_data_t *sta_data = NULL;
	int i;
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)bcm_cfg;

	if (!cfg) {
		WL_ERR(("cfg is NULL\n"));
		return -EINVAL;
	}

	kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;

	alloc_len = sizeof(wl_ap_sta_info_t);
	sta_info = (wl_ap_sta_info_t *)kzalloc(alloc_len, kflags);

	if (unlikely(!sta_info)) {
		WL_ERR(("could not allocate memory for - "
					"wl_ap_sta_info_t\n"));
		goto fail;
	}
	cfg->ap_sta_info = sta_info;

	alloc_len = sizeof(wl_ap_sta_data_t) * MAX_STA_INFO_AP_CNT;
	sta_data = (wl_ap_sta_data_t *)kzalloc(alloc_len, kflags);

	if (unlikely(!sta_data)) {
		WL_ERR(("could not allocate memory for - "
					"wl_ap_sta_data_t\n"));
		goto fail;
	}

	cfg->ap_sta_info->sta_list_cnt = 0;

	for (i = 0; i < MAX_STA_INFO_AP_CNT; i++) {
		sta_data[i].is_empty = TRUE;
		memset(&sta_data[i].mac, 0, ETHER_ADDR_LEN);
	}

	cfg->ap_sta_info->ap_sta_data = sta_data;

	mutex_init(&cfg->ap_sta_info->wq_data_sync);

	WL_ERR(("attach success\n"));

	return BCME_OK;

fail:
	if (sta_data) {
		kfree(sta_data);
		cfg->ap_sta_info->ap_sta_data = NULL;
	}

	if (sta_info) {
		kfree(sta_info);
		cfg->ap_sta_info = NULL;
	}

	return BCME_ERROR;
}

int
wl_detach_ap_stainfo(void *bcm_cfg)
{
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)bcm_cfg;

	if (!cfg || !cfg->ap_sta_info) {
		WL_ERR(("cfg=%p ap_sta_info=%p\n",
			cfg, (cfg ? cfg->ap_sta_info : NULL)));
		return -EINVAL;
	}

	if (cfg->ap_sta_info->ap_sta_data) {
		kfree(cfg->ap_sta_info->ap_sta_data);
		cfg->ap_sta_info->ap_sta_data = NULL;
	}

	mutex_destroy(&cfg->ap_sta_info->wq_data_sync);

	kfree(cfg->ap_sta_info);
	cfg->ap_sta_info = NULL;

	WL_ERR(("detach success\n"));

	return BCME_OK;
}

int
wl_ap_stainfo_init(void *bcm_cfg)
{
	int i;
	wl_ap_sta_data_t *sta_data;
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)bcm_cfg;

	if (!cfg || !cfg->ap_sta_info) {
		WL_ERR(("cfg=%p ap_sta_info=%p\n",
			cfg, (cfg ? cfg->ap_sta_info : NULL)));
		return -EINVAL;
	}

	sta_data = cfg->ap_sta_info->ap_sta_data;
	cfg->ap_sta_info->sta_list_cnt = 0;

	if (!sta_data) {
		WL_ERR(("ap_sta_data is NULL\n"));
		return -EINVAL;
	}

	for (i = 0; i < MAX_STA_INFO_AP_CNT; i++) {
		sta_data[i].is_empty = TRUE;
		memset(&sta_data[i].mac, 0, ETHER_ADDR_LEN);
	}

	return BCME_OK;
}

int
wl_get_ap_stadata(void *bcm_cfg, struct ether_addr *sta_mac, void **data)
{
	int i;
	wl_ap_sta_data_t *sta_data;
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)bcm_cfg;

	if (!cfg || !cfg->ap_sta_info) {
		WL_ERR(("cfg=%p ap_sta_info=%p\n",
			cfg, (cfg ? cfg->ap_sta_info : NULL)));
		return -EINVAL;
	}

	if  (!sta_mac || !data) {
		WL_ERR(("sta_mac=%p data=%p\n", sta_mac, data));
		return -EINVAL;
	}

	sta_data = cfg->ap_sta_info->ap_sta_data;

	if (!sta_data) {
		WL_ERR(("ap_sta_data is NULL\n"));
		return -EINVAL;
	}

	/* Find already existed sta */
	for (i = 0; i < MAX_STA_INFO_AP_CNT; i++) {
		if (!sta_data[i].is_empty) {
			WL_AP_BIGDATA_LOG(("%d " MACDBG " " MACDBG "\n", i,
				MAC2STRDBG((char *)sta_mac),
				MAC2STRDBG((char*)&sta_data[i].mac)));

			if (!memcmp(sta_mac, (char*)&sta_data[i].mac, ETHER_ADDR_LEN)) {
				WL_AP_BIGDATA_LOG(("Found STA idx %d " MACDBG "\n",
					i, MAC2STRDBG((char *)&sta_mac)));

				*data = (wl_ap_sta_data_t*)&sta_data[i];
				return BCME_OK;
			}
		}
	}

	return BCME_ERROR;
}
