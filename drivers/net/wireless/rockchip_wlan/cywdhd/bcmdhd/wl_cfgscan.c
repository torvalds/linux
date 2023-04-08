/*
 * Linux cfg80211 driver scan related code
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
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
 * $Id$
 */
/* */
#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>
#include <linux/kernel.h>

#include <bcmutils.h>
#include <bcmstdlib_s.h>
#include <bcmwifi_channels.h>
#include <bcmendian.h>
#include <ethernet.h>
#include <802.11.h>
#include <bcmiov.h>
#include <linux/if_arp.h>
#include <asm/uaccess.h>

#include <ethernet.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <linux/ieee80211.h>
#include <linux/wait.h>
#include <net/cfg80211.h>
#include <net/rtnetlink.h>

#include <wlioctl.h>
#include <bcmevent.h>
#include <wldev_common.h>
#include <wl_cfg80211.h>
#include <wl_cfgscan.h>
#include <wl_cfgp2p.h>
#include <bcmdevs.h>
#ifdef OEM_ANDROID
#include <wl_android.h>
#endif // endif
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_linux.h>
#include <dhd_debug.h>
#include <dhdioctl.h>
#include <wlioctl.h>
#include <dhd_cfg80211.h>
#include <dhd_bus.h>
#include <wl_cfgvendor.h>
#ifdef BCMPCIE
#include <dhd_flowring.h>
#endif // endif
#ifdef PNO_SUPPORT
#include <dhd_pno.h>
#endif /* PNO_SUPPORT */
#ifdef RTT_SUPPORT
#include "dhd_rtt.h"
#endif /* RTT_SUPPORT */

#define ACTIVE_SCAN 1
#define PASSIVE_SCAN 0

#define MIN_P2P_IE_LEN	8	/* p2p_ie->OUI(3) + p2p_ie->oui_type(1) +
				 * Attribute ID(1) + Length(2) + 1(Mininum length:1)
				 */
#define MAX_P2P_IE_LEN	251	/* Up To 251 */

#define WPS_ATTR_REQ_TYPE 0x103a
#define WPS_REQ_TYPE_ENROLLEE 0x01

#if defined(USE_INITIAL_2G_SCAN) || defined(USE_INITIAL_SHORT_DWELL_TIME)
#define FIRST_SCAN_ACTIVE_DWELL_TIME_MS 40
bool g_first_broadcast_scan = TRUE;
#endif /* USE_INITIAL_2G_SCAN || USE_INITIAL_SHORT_DWELL_TIME */
#ifdef CUSTOMER_HW4_DEBUG
bool wl_scan_timeout_dbg_enabled = 0;
#endif /* CUSTOMER_HW4_DEBUG */
#ifdef P2P_LISTEN_OFFLOADING
void wl_cfg80211_cancel_p2plo(struct bcm_cfg80211 *cfg);
#endif /* P2P_LISTEN_OFFLOADING */
static void _wl_notify_scan_done(struct bcm_cfg80211 *cfg, bool aborted);

void
wl_cfg80211_ch_switch_notify(struct net_device *dev, uint16 chanspec, struct wiphy *wiphy);

extern int passive_channel_skip;

#ifdef WL11U
bcm_tlv_t *
wl_cfg80211_find_interworking_ie(const u8 *parse, u32 len)
{
	bcm_tlv_t *ie;

/* unfortunately it's too much work to dispose the const cast - bcm_parse_tlvs
 * is used everywhere and changing its prototype to take const qualifier needs
 * a massive change to all its callers...
 */

	if ((ie = bcm_parse_tlvs(parse, len, DOT11_MNG_INTERWORKING_ID))) {
		return ie;
	}
	return NULL;
}

s32
wl_cfg80211_clear_iw_ie(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 bssidx)
{
	ie_setbuf_t ie_setbuf;

	WL_DBG(("clear interworking IE\n"));

	bzero(&ie_setbuf, sizeof(ie_setbuf_t));

	ie_setbuf.ie_buffer.iecount = htod32(1);
	ie_setbuf.ie_buffer.ie_list[0].ie_data.id = DOT11_MNG_INTERWORKING_ID;
	ie_setbuf.ie_buffer.ie_list[0].ie_data.len = 0;

	return wldev_iovar_setbuf_bsscfg(ndev, "ie", &ie_setbuf, sizeof(ie_setbuf),
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
}

s32
wl_cfg80211_add_iw_ie(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 bssidx, s32 pktflag,
                      uint8 ie_id, uint8 *data, uint8 data_len)
{
	s32 err = BCME_OK;
	s32 buf_len;
	ie_setbuf_t *ie_setbuf;
	ie_getbuf_t ie_getbufp;
	char getbuf[WLC_IOCTL_SMLEN];
	u32 iw_ie_len = 0;
	u8 iw_ie[IW_IES_MAX_BUF_LEN];

	if (ie_id != DOT11_MNG_INTERWORKING_ID) {
		WL_ERR(("unsupported (id=%d)\n", ie_id));
		return BCME_UNSUPPORTED;
	}

	/* access network options (1 octet)  is the mandatory field */
	if (!data || data_len == 0 || data_len > IW_IES_MAX_BUF_LEN) {
		WL_ERR(("wrong interworking IE (len=%d)\n", data_len));
		return BCME_BADARG;
	}

	/* Validate the pktflag parameter */
	if ((pktflag & ~(VNDR_IE_BEACON_FLAG | VNDR_IE_PRBRSP_FLAG |
			VNDR_IE_ASSOCRSP_FLAG | VNDR_IE_AUTHRSP_FLAG |
			VNDR_IE_PRBREQ_FLAG | VNDR_IE_ASSOCREQ_FLAG|
			VNDR_IE_CUSTOM_FLAG))) {
		WL_ERR(("invalid packet flag 0x%x\n", pktflag));
		return BCME_BADARG;
	}

	wl_get_iwdata_by_netdev(cfg, ndev, iw_ie, &iw_ie_len);

	if (iw_ie_len == data_len && !memcmp(iw_ie, data, data_len)) {
		WL_ERR(("Previous IW IE is equals to current IE\n"));
		return BCME_OK;
	}

	buf_len = sizeof(ie_setbuf_t) + data_len - 1;

	ie_getbufp.id = DOT11_MNG_INTERWORKING_ID;
	if (wldev_iovar_getbuf_bsscfg(ndev, "ie", (void *)&ie_getbufp,
			sizeof(ie_getbufp), getbuf, WLC_IOCTL_SMLEN, bssidx, &cfg->ioctl_buf_sync)
			== BCME_OK) {
		if (!memcmp(&getbuf[TLV_HDR_LEN], data, data_len)) {
			WL_DBG(("skip to set interworking IE\n"));
			return BCME_OK;
		}
	}

	/* if already set with previous values, delete it first */
	if (cfg->wl11u) {
		if ((err = wl_cfg80211_clear_iw_ie(cfg, ndev, bssidx)) != BCME_OK) {
			return err;
		}
	}

	ie_setbuf = (ie_setbuf_t *)MALLOCZ(cfg->osh, buf_len);
	if (!ie_setbuf) {
		WL_ERR(("Error allocating buffer for IE\n"));
		return -ENOMEM;
	}
	strlcpy(ie_setbuf->cmd, "add", sizeof(ie_setbuf->cmd));

	/* Buffer contains only 1 IE */
	ie_setbuf->ie_buffer.iecount = htod32(1);
	/* use VNDR_IE_CUSTOM_FLAG flags for none vendor IE . currently fixed value */
	ie_setbuf->ie_buffer.ie_list[0].pktflag = htod32(pktflag);

	/* Now, add the IE to the buffer */
	ie_setbuf->ie_buffer.ie_list[0].ie_data.id = DOT11_MNG_INTERWORKING_ID;
	ie_setbuf->ie_buffer.ie_list[0].ie_data.len = data_len;
	/* Returning void here as max data_len can be 8 */
	(void)memcpy_s((uchar *)&ie_setbuf->ie_buffer.ie_list[0].ie_data.data[0],
		 sizeof(uint8) * data_len, data, data_len);

	if ((err = wldev_iovar_setbuf_bsscfg(ndev, "ie", ie_setbuf, buf_len,
			cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync))
			== BCME_OK) {
		WL_DBG(("set interworking IE\n"));
		cfg->wl11u = TRUE;
		wl_set_iwdata_by_netdev(cfg, ndev, data, data_len);
		err = wldev_iovar_setint_bsscfg(ndev, "grat_arp", 1, bssidx);
	}

	MFREE(cfg->osh, ie_setbuf, buf_len);
	return err;
}
#endif /* WL11U */

#ifdef WL_BCNRECV
/* Beacon recv results handler sending to upper layer */
static s32
wl_bcnrecv_result_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
		wl_bss_info_v109_2_t *bi, uint32 scan_status)
{
	s32 err = BCME_OK;
	struct wiphy *wiphy = NULL;
	wl_bcnrecv_result_t *bcn_recv = NULL;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0))
	struct timespec64 ts;
#else
	struct timespec ts;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)) */

	if (!bi) {
		WL_ERR(("%s: bi is NULL\n", __func__));
		err = BCME_NORESOURCE;
		goto exit;
	}
	if ((bi->length - bi->ie_length) < sizeof(wl_bss_info_v109_2_t)) {
		WL_ERR(("bi info version doesn't support bcn_recv attributes\n"));
		goto exit;
	}

	if (scan_status == WLC_E_STATUS_RXBCN) {
		wiphy = cfg->wdev->wiphy;
		if (!wiphy) {
			 WL_ERR(("wiphy is NULL\n"));
			 err = BCME_NORESOURCE;
			 goto exit;
		}
		bcn_recv = (wl_bcnrecv_result_t *)MALLOCZ(cfg->osh, sizeof(*bcn_recv));
		if (unlikely(!bcn_recv)) {
			WL_ERR(("Failed to allocate memory\n"));
			return -ENOMEM;
		}
		/* Returning void here as copy size does not exceed dest size of SSID */
		(void)memcpy_s((char *)bcn_recv->SSID, DOT11_MAX_SSID_LEN,
			(char *)bi->SSID, DOT11_MAX_SSID_LEN);
		/* Returning void here as copy size does not exceed dest size of ETH_LEN */
		(void)memcpy_s(&bcn_recv->BSSID, ETHER_ADDR_LEN, &bi->BSSID, ETH_ALEN);
		bcn_recv->channel = wf_chspec_ctlchan(
			wl_chspec_driver_to_host(bi->chanspec));
		bcn_recv->beacon_interval = bi->beacon_period;

		/* kernal timestamp */
		get_monotonic_boottime(&ts);
		bcn_recv->system_time = ((u64)ts.tv_sec*1000000)
				+ ts.tv_nsec / 1000;
		bcn_recv->timestamp[0] = bi->timestamp[0];
		bcn_recv->timestamp[1] = bi->timestamp[1];
		if ((err = wl_android_bcnrecv_event(cfgdev_to_wlc_ndev(cfgdev, cfg),
				BCNRECV_ATTR_BCNINFO, 0, 0,
				(uint8 *)bcn_recv, sizeof(*bcn_recv)))
				!= BCME_OK) {
			WL_ERR(("failed to send bcnrecv event, error:%d\n", err));
		}
	} else {
		WL_DBG(("Ignoring Escan Event:%d \n", scan_status));
	}
exit:
	if (bcn_recv) {
		MFREE(cfg->osh, bcn_recv, sizeof(*bcn_recv));
	}
	return err;
}
#endif /* WL_BCNRECV */

#ifdef ESCAN_BUF_OVERFLOW_MGMT
#ifndef WL_DRV_AVOID_SCANCACHE
static void
wl_cfg80211_find_removal_candidate(wl_bss_info_t *bss, removal_element_t *candidate)
{
	int idx;
	for (idx = 0; idx < BUF_OVERFLOW_MGMT_COUNT; idx++) {
		int len = BUF_OVERFLOW_MGMT_COUNT - idx - 1;
		if (bss->RSSI < candidate[idx].RSSI) {
			if (len) {
				/* In the below memcpy operation the candidate array always has the
				* buffer space available to max 'len' calculated in the for loop.
				*/
				(void)memcpy_s(&candidate[idx + 1],
					(sizeof(removal_element_t) * len),
					&candidate[idx], sizeof(removal_element_t) * len);
			}
			candidate[idx].RSSI = bss->RSSI;
			candidate[idx].length = bss->length;
			(void)memcpy_s(&candidate[idx].BSSID, ETHER_ADDR_LEN,
				&bss->BSSID, ETHER_ADDR_LEN);
			return;
		}
	}
}

static void
wl_cfg80211_remove_lowRSSI_info(wl_scan_results_t *list, removal_element_t *candidate,
	wl_bss_info_t *bi)
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
				WL_DBG(("delete scan info of " MACDBG " to add new AP\n",
					MAC2STRDBG(bss->BSSID.octet)));
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
#endif /* WL_DRV_AVOID_SCANCACHE */
#endif /* ESCAN_BUF_OVERFLOW_MGMT */

s32
wl_escan_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	s32 err = BCME_OK;
	s32 status = ntoh32(e->status);
	wl_escan_result_t *escan_result;
	struct net_device *ndev = NULL;
#ifndef WL_DRV_AVOID_SCANCACHE
	wl_bss_info_t *bi;
	u32 bi_length;
	const wifi_p2p_ie_t * p2p_ie;
	const u8 *p2p_dev_addr = NULL;
	wl_scan_results_t *list;
	wl_bss_info_t *bss = NULL;
	u32 i;
#endif /* WL_DRV_AVOID_SCANCACHE */

	WL_DBG((" enter event type : %d, status : %d \n",
		ntoh32(e->event_type), ntoh32(e->status)));

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	mutex_lock(&cfg->scan_sync);
	/* P2P SCAN is coming from primary interface */
	if (wl_get_p2p_status(cfg, SCANNING)) {
		if (wl_get_drv_status_all(cfg, SENDING_ACT_FRM))
			ndev = cfg->afx_hdl->dev;
		else
			ndev = cfg->escan_info.ndev;
	}
	escan_result = (wl_escan_result_t *)data;
#ifdef WL_BCNRECV
	if (cfg->bcnrecv_info.bcnrecv_state == BEACON_RECV_STARTED &&
		status == WLC_E_STATUS_RXBCN) {
		/* handle beacon recv scan results */
		wl_bss_info_v109_2_t *bi_info;
		bi_info = (wl_bss_info_v109_2_t *)escan_result->bss_info;
		err = wl_bcnrecv_result_handler(cfg, cfgdev, bi_info, status);
		goto exit;
	}
#endif /* WL_BCNRECV */
	if (!ndev || (!wl_get_drv_status(cfg, SCANNING, ndev) && !cfg->sched_scan_running)) {
		WL_ERR_RLMT(("escan is not ready. drv_scan_status 0x%x"
			" e_type %d e_states %d\n",
			wl_get_drv_status(cfg, SCANNING, ndev),
			ntoh32(e->event_type), ntoh32(e->status)));
		goto exit;
	}

#ifndef WL_DRV_AVOID_SCANCACHE
	if (status == WLC_E_STATUS_PARTIAL) {
		WL_DBG(("WLC_E_STATUS_PARTIAL \n"));
		DBG_EVENT_LOG((dhd_pub_t *)cfg->pub, WIFI_EVENT_DRIVER_SCAN_RESULT_FOUND);
		if (!escan_result) {
			WL_ERR(("Invalid escan result (NULL pointer)\n"));
			goto exit;
		}
		if ((dtoh32(escan_result->buflen) > (int)ESCAN_BUF_SIZE) ||
		    (dtoh32(escan_result->buflen) < sizeof(wl_escan_result_t))) {
			WL_ERR(("Invalid escan buffer len:%d\n", dtoh32(escan_result->buflen)));
			goto exit;
		}
		if (dtoh16(escan_result->bss_count) != 1) {
			WL_ERR(("Invalid bss_count %d: ignoring\n", escan_result->bss_count));
			goto exit;
		}
		bi = escan_result->bss_info;
		if (!bi) {
			WL_ERR(("Invalid escan bss info (NULL pointer)\n"));
			goto exit;
		}
		bi_length = dtoh32(bi->length);
		if (bi_length != (dtoh32(escan_result->buflen) - WL_ESCAN_RESULTS_FIXED_SIZE)) {
			WL_ERR(("Invalid bss_info length %d: ignoring\n", bi_length));
			goto exit;
		}
		if (wl_escan_check_sync_id(status, escan_result->sync_id,
			cfg->escan_info.cur_sync_id) < 0)
			goto exit;

		if (!(bcmcfg_to_wiphy(cfg)->interface_modes & BIT(NL80211_IFTYPE_ADHOC))) {
			if (dtoh16(bi->capability) & DOT11_CAP_IBSS) {
				WL_DBG(("Ignoring IBSS result\n"));
				goto exit;
			}
		}

		if (wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL)) {
			p2p_dev_addr = wl_cfgp2p_retreive_p2p_dev_addr(bi, bi_length);
			if (p2p_dev_addr && !memcmp(p2p_dev_addr,
				cfg->afx_hdl->tx_dst_addr.octet, ETHER_ADDR_LEN)) {
				s32 channel = wf_chspec_ctlchan(
					wl_chspec_driver_to_host(bi->chanspec));

				if ((channel > MAXCHANNEL) || (channel <= 0))
					channel = WL_INVALID;
				else
					WL_ERR(("ACTION FRAME SCAN : Peer " MACDBG " found,"
						" channel : %d\n",
						MAC2STRDBG(cfg->afx_hdl->tx_dst_addr.octet),
						channel));

				wl_clr_p2p_status(cfg, SCANNING);
				cfg->afx_hdl->peer_chan = channel;
				complete(&cfg->act_frm_scan);
				goto exit;
			}

		} else {
			int cur_len = WL_SCAN_RESULTS_FIXED_SIZE;
#ifdef ESCAN_BUF_OVERFLOW_MGMT
			removal_element_t candidate[BUF_OVERFLOW_MGMT_COUNT];
			int remove_lower_rssi = FALSE;

			bzero(candidate, sizeof(removal_element_t)*BUF_OVERFLOW_MGMT_COUNT);
#endif /* ESCAN_BUF_OVERFLOW_MGMT */

			list = wl_escan_get_buf(cfg, FALSE);
			if (scan_req_match(cfg)) {
#ifdef WL_HOST_BAND_MGMT
				s32 channel_band = 0;
				chanspec_t chspec;
#endif /* WL_HOST_BAND_MGMT */
				/* p2p scan && allow only probe response */
				if ((cfg->p2p->search_state != WL_P2P_DISC_ST_SCAN) &&
					(bi->flags & WL_BSS_FLAGS_FROM_BEACON))
					goto exit;
				if ((p2p_ie = wl_cfgp2p_find_p2pie(((u8 *) bi) + bi->ie_offset,
					bi->ie_length)) == NULL) {
						WL_ERR(("Couldn't find P2PIE in probe"
							" response/beacon\n"));
						goto exit;
				}
#ifdef WL_HOST_BAND_MGMT
				chspec = wl_chspec_driver_to_host(bi->chanspec);
				channel_band = CHSPEC2WLC_BAND(chspec);

				if ((cfg->curr_band == WLC_BAND_5G) &&
					(channel_band == WLC_BAND_2G)) {
					/* Avoid sending the GO results in band conflict */
					if (wl_cfgp2p_retreive_p2pattrib(p2p_ie,
						P2P_SEID_GROUP_ID) != NULL)
						goto exit;
				}
#endif /* WL_HOST_BAND_MGMT */
			}
#ifdef ESCAN_BUF_OVERFLOW_MGMT
			if (bi_length > ESCAN_BUF_SIZE - list->buflen)
				remove_lower_rssi = TRUE;
#endif /* ESCAN_BUF_OVERFLOW_MGMT */

			for (i = 0; i < list->count; i++) {
				bss = bss ? (wl_bss_info_t *)((uintptr)bss + dtoh32(bss->length))
					: list->bss_info;
				if (!bss) {
					WL_ERR(("bss is NULL\n"));
					goto exit;
				}
#ifdef ESCAN_BUF_OVERFLOW_MGMT
				WL_TRACE(("%s("MACDBG"), i=%d bss: RSSI %d list->count %d\n",
					bss->SSID, MAC2STRDBG(bss->BSSID.octet),
					i, bss->RSSI, list->count));

				if (remove_lower_rssi)
					wl_cfg80211_find_removal_candidate(bss, candidate);
#endif /* ESCAN_BUF_OVERFLOW_MGMT */

				if (!bcmp(&bi->BSSID, &bss->BSSID, ETHER_ADDR_LEN) &&
					(CHSPEC_BAND(wl_chspec_driver_to_host(bi->chanspec))
					== CHSPEC_BAND(wl_chspec_driver_to_host(bss->chanspec))) &&
					bi->SSID_len == bss->SSID_len &&
					!bcmp(bi->SSID, bss->SSID, bi->SSID_len)) {

					/* do not allow beacon data to update
					*the data recd from a probe response
					*/
					if (!(bss->flags & WL_BSS_FLAGS_FROM_BEACON) &&
						(bi->flags & WL_BSS_FLAGS_FROM_BEACON))
						goto exit;

					WL_DBG(("%s("MACDBG"), i=%d prev: RSSI %d"
						" flags 0x%x, new: RSSI %d flags 0x%x\n",
						bss->SSID, MAC2STRDBG(bi->BSSID.octet), i,
						bss->RSSI, bss->flags, bi->RSSI, bi->flags));

					if ((bss->flags & WL_BSS_FLAGS_RSSI_ONCHANNEL) ==
						(bi->flags & WL_BSS_FLAGS_RSSI_ONCHANNEL)) {
						/* preserve max RSSI if the measurements are
						* both on-channel or both off-channel
						*/
						WL_SCAN(("%s("MACDBG"), same onchan"
						", RSSI: prev %d new %d\n",
						bss->SSID, MAC2STRDBG(bi->BSSID.octet),
						bss->RSSI, bi->RSSI));
						bi->RSSI = MAX(bss->RSSI, bi->RSSI);
					} else if ((bss->flags & WL_BSS_FLAGS_RSSI_ONCHANNEL) &&
						(bi->flags & WL_BSS_FLAGS_RSSI_ONCHANNEL) == 0) {
						/* preserve the on-channel rssi measurement
						* if the new measurement is off channel
						*/
						WL_SCAN(("%s("MACDBG"), prev onchan"
						", RSSI: prev %d new %d\n",
						bss->SSID, MAC2STRDBG(bi->BSSID.octet),
						bss->RSSI, bi->RSSI));
						bi->RSSI = bss->RSSI;
						bi->flags |= WL_BSS_FLAGS_RSSI_ONCHANNEL;
					}
					if (dtoh32(bss->length) != bi_length) {
						u32 prev_len = dtoh32(bss->length);

						WL_SCAN(("bss info replacement"
							" is occured(bcast:%d->probresp%d)\n",
							bss->ie_length, bi->ie_length));
						WL_DBG(("%s("MACDBG"), replacement!(%d -> %d)\n",
						bss->SSID, MAC2STRDBG(bi->BSSID.octet),
						prev_len, bi_length));

						if ((list->buflen - prev_len) + bi_length
							> ESCAN_BUF_SIZE) {
							WL_ERR(("Buffer is too small: keep the"
								" previous result of this AP\n"));
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
					/* In the above code under check
					*  '(dtoh32(bss->length) != bi_length)'
					* buffer overflow is avoided. bi_length
					* is already accounted in list->buflen
					*/
					if ((err = memcpy_s((u8 *)bss,
						(ESCAN_BUF_SIZE - (list->buflen - bi_length)),
						(u8 *)bi, bi_length)) != BCME_OK) {
						WL_ERR(("Failed to copy the recent bss_info."
							"err:%d recv_len:%d bi_len:%d\n", err,
							ESCAN_BUF_SIZE - (list->buflen - bi_length),
							bi_length));
						/* This scenario should never happen. If it happens,
						 * set list->count to zero for recovery
						 */
						list->count = 0;
						list->buflen = 0;
						ASSERT(0);
					}
					goto exit;
				}
				cur_len += dtoh32(bss->length);
			}
			if (bi_length > ESCAN_BUF_SIZE - list->buflen) {
#ifdef ESCAN_BUF_OVERFLOW_MGMT
				wl_cfg80211_remove_lowRSSI_info(list, candidate, bi);
				if (bi_length > ESCAN_BUF_SIZE - list->buflen) {
					WL_DBG(("RSSI(" MACDBG ") is too low(%d) to add Buffer\n",
						MAC2STRDBG(bi->BSSID.octet), bi->RSSI));
					goto exit;
				}
#else
				WL_ERR(("Buffer is too small: ignoring\n"));
				goto exit;
#endif /* ESCAN_BUF_OVERFLOW_MGMT */
			}
			/* In the previous step check is added to ensure the bi_legth does not
			* exceed the ESCAN_BUF_SIZE
			*/
			(void)memcpy_s(&(((char *)list)[list->buflen]),
				(ESCAN_BUF_SIZE - list->buflen), bi, bi_length);
			list->version = dtoh32(bi->version);
			list->buflen += bi_length;
			list->count++;

			/*
			 * !Broadcast && number of ssid = 1 && number of channels =1
			 * means specific scan to association
			 */
			if (wl_cfgp2p_is_p2p_specific_scan(cfg->scan_request)) {
				WL_ERR(("P2P assoc scan fast aborted.\n"));
				wl_notify_escan_complete(cfg, cfg->escan_info.ndev, false, true);
				goto exit;
			}
		}
	}
	else if (status == WLC_E_STATUS_SUCCESS) {
		cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
		wl_escan_print_sync_id(status, cfg->escan_info.cur_sync_id,
			escan_result->sync_id);

		if (wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL)) {
			WL_DBG(("ACTION FRAME SCAN DONE\n"));
			wl_clr_p2p_status(cfg, SCANNING);
			wl_clr_drv_status(cfg, SCANNING, cfg->afx_hdl->dev);
			if (cfg->afx_hdl->peer_chan == WL_INVALID)
				complete(&cfg->act_frm_scan);
		} else if ((likely(cfg->scan_request)) || (cfg->sched_scan_running)) {
			WL_INFORM_MEM(("ESCAN COMPLETED\n"));
			DBG_EVENT_LOG((dhd_pub_t *)cfg->pub, WIFI_EVENT_DRIVER_SCAN_COMPLETE);
			cfg->bss_list = wl_escan_get_buf(cfg, FALSE);
			if (!scan_req_match(cfg)) {
				WL_TRACE_HW4(("SCAN COMPLETED: scanned AP count=%d\n",
					cfg->bss_list->count));
			}
			wl_inform_bss(cfg);
			wl_notify_escan_complete(cfg, ndev, false, false);
		}
		wl_escan_increment_sync_id(cfg, SCAN_BUF_NEXT);
#ifdef CUSTOMER_HW4_DEBUG
		if (wl_scan_timeout_dbg_enabled)
			wl_scan_timeout_dbg_clear();
#endif /* CUSTOMER_HW4_DEBUG */
	} else if ((status == WLC_E_STATUS_ABORT) || (status == WLC_E_STATUS_NEWSCAN) ||
		(status == WLC_E_STATUS_11HQUIET) || (status == WLC_E_STATUS_CS_ABORT) ||
		(status == WLC_E_STATUS_NEWASSOC)) {
		/* Dump FW preserve buffer content */
		if (status == WLC_E_STATUS_ABORT) {
			wl_flush_fw_log_buffer(ndev, FW_LOGSET_MASK_ALL);
		}
		/* Handle all cases of scan abort */
		cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
		wl_escan_print_sync_id(status, escan_result->sync_id,
			cfg->escan_info.cur_sync_id);
		WL_DBG(("ESCAN ABORT reason: %d\n", status));
		if (wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL)) {
			WL_DBG(("ACTION FRAME SCAN DONE\n"));
			wl_clr_drv_status(cfg, SCANNING, cfg->afx_hdl->dev);
			wl_clr_p2p_status(cfg, SCANNING);
			if (cfg->afx_hdl->peer_chan == WL_INVALID)
				complete(&cfg->act_frm_scan);
		} else if ((likely(cfg->scan_request)) || (cfg->sched_scan_running)) {
			WL_INFORM_MEM(("ESCAN ABORTED\n"));

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
			if (p2p_scan(cfg) && cfg->scan_request &&
				(cfg->scan_request->flags & NL80211_SCAN_FLAG_FLUSH)) {
				WL_ERR(("scan list is changed"));
				cfg->bss_list = wl_escan_get_buf(cfg, FALSE);
			} else
#endif // endif
				cfg->bss_list = wl_escan_get_buf(cfg, TRUE);

			if (!scan_req_match(cfg)) {
				WL_TRACE_HW4(("SCAN ABORTED: scanned AP count=%d\n",
					cfg->bss_list->count));
			}
#ifdef DUAL_ESCAN_RESULT_BUFFER
			if (escan_result->sync_id != cfg->escan_info.cur_sync_id) {
				/* If sync_id is not matching, then the abort might have
				 * come for the old scan req or for the in-driver initiated
				 * scan. So do abort for scan_req for which sync_id is
				 * matching.
				 */
				WL_INFORM_MEM(("sync_id mismatch (%d != %d). "
					"Ignore the scan abort event.\n",
					escan_result->sync_id, cfg->escan_info.cur_sync_id));
				goto exit;
			} else {
				/* sync id is matching, abort the scan */
				WL_INFORM_MEM(("scan aborted for sync_id: %d \n",
					cfg->escan_info.cur_sync_id));
				wl_inform_bss(cfg);
				wl_notify_escan_complete(cfg, ndev, true, false);
			}
#else
			wl_inform_bss(cfg);
			wl_notify_escan_complete(cfg, ndev, true, false);
#endif /* DUAL_ESCAN_RESULT_BUFFER */
		} else {
			/* If there is no pending host initiated scan, do nothing */
			WL_DBG(("ESCAN ABORT: No pending scans. Ignoring event.\n"));
		}
		wl_escan_increment_sync_id(cfg, SCAN_BUF_CNT);
	} else if (status == WLC_E_STATUS_TIMEOUT) {
		WL_ERR(("WLC_E_STATUS_TIMEOUT : scan_request[%p]\n", cfg->scan_request));
		WL_ERR(("reason[0x%x]\n", e->reason));
		if (e->reason == 0xFFFFFFFF) {
			wl_notify_escan_complete(cfg, cfg->escan_info.ndev, true, true);
		}
	} else {
		WL_ERR(("unexpected Escan Event %d : abort\n", status));
		cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
		wl_escan_print_sync_id(status, escan_result->sync_id,
			cfg->escan_info.cur_sync_id);
		if (wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL)) {
			WL_DBG(("ACTION FRAME SCAN DONE\n"));
			wl_clr_p2p_status(cfg, SCANNING);
			wl_clr_drv_status(cfg, SCANNING, cfg->afx_hdl->dev);
			if (cfg->afx_hdl->peer_chan == WL_INVALID)
				complete(&cfg->act_frm_scan);
		} else if ((likely(cfg->scan_request)) || (cfg->sched_scan_running)) {
			cfg->bss_list = wl_escan_get_buf(cfg, TRUE);
			if (!scan_req_match(cfg)) {
				WL_TRACE_HW4(("SCAN ABORTED(UNEXPECTED): "
					"scanned AP count=%d\n",
					cfg->bss_list->count));
			}
			wl_inform_bss(cfg);
			wl_notify_escan_complete(cfg, ndev, true, false);
		}
		wl_escan_increment_sync_id(cfg, 2);
	}
#else /* WL_DRV_AVOID_SCANCACHE */
	err = wl_escan_without_scan_cache(cfg, escan_result, ndev, e, status);
#endif /* WL_DRV_AVOID_SCANCACHE */
exit:
	mutex_unlock(&cfg->scan_sync);
	return err;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)) && \
	defined(SUPPORT_RANDOM_MAC_SCAN)
static const u8 *
wl_retrieve_wps_attribute(const u8 *buf, u16 element_id)
{
	const wl_wps_ie_t *ie = NULL;
	u16 len = 0;
	const u8 *attrib;

	if (!buf) {
		WL_ERR(("WPS IE not present"));
		return 0;
	}

	ie = (const wl_wps_ie_t*) buf;
	len = ie->len;

	/* Point subel to the P2P IE's subelt field.
	 * Subtract the preceding fields (id, len, OUI, oui_type) from the length.
	 */
	attrib = ie->attrib;
	len -= 4;	/* exclude OUI + OUI_TYPE */

	/* Search for attrib */
	return wl_find_attribute(attrib, len, element_id);
}

static bool
wl_is_wps_enrollee_active(struct net_device *ndev, const u8 *ie_ptr, u16 len)
{
	const u8 *ie;
	const u8 *attrib;

	if ((ie = (const u8 *)wl_cfgp2p_find_wpsie(ie_ptr, len)) == NULL) {
		WL_DBG(("WPS IE not present. Do nothing.\n"));
		return false;
	}

	if ((attrib = wl_retrieve_wps_attribute(ie, WPS_ATTR_REQ_TYPE)) == NULL) {
		WL_DBG(("WPS_ATTR_REQ_TYPE not found!\n"));
		return false;
	}

	if (*attrib == WPS_REQ_TYPE_ENROLLEE) {
		WL_INFORM_MEM(("WPS Enrolle Active\n"));
		return true;
	} else {
		WL_DBG(("WPS_REQ_TYPE:%d\n", *attrib));
	}

	return false;
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0) && defined(SUPPORT_RANDOM_MAC_SCAN) */

/* Find listen channel */
static s32 wl_find_listen_channel(struct bcm_cfg80211 *cfg,
	const u8 *ie, u32 ie_len)
{
	const wifi_p2p_ie_t *p2p_ie;
	const u8 *end, *pos;
	s32 listen_channel;

	pos = (const u8 *)ie;

	p2p_ie = wl_cfgp2p_find_p2pie(pos, ie_len);

	if (p2p_ie == NULL) {
		return 0;
	}

	if (p2p_ie->len < MIN_P2P_IE_LEN || p2p_ie->len > MAX_P2P_IE_LEN) {
		CFGP2P_ERR(("p2p_ie->len out of range - %d\n", p2p_ie->len));
		return 0;
	}
	pos = p2p_ie->subelts;
	end = p2p_ie->subelts + (p2p_ie->len - 4);

	CFGP2P_DBG((" found p2p ie ! lenth %d \n",
		p2p_ie->len));

	while (pos < end) {
		uint16 attr_len;
		if (pos + 2 >= end) {
			CFGP2P_DBG((" -- Invalid P2P attribute"));
			return 0;
		}
		attr_len = ((uint16) (((pos + 1)[1] << 8) | (pos + 1)[0]));

		if (pos + 3 + attr_len > end) {
			CFGP2P_DBG(("P2P: Attribute underflow "
				   "(len=%u left=%d)",
				   attr_len, (int) (end - pos - 3)));
			return 0;
		}

		/* if Listen Channel att id is 6 and the vailue is valid,
		 * return the listen channel
		 */
		if (pos[0] == 6) {
			/* listen channel subel length format
			 * 1(id) + 2(len) + 3(country) + 1(op. class) + 1(chan num)
			 */
			listen_channel = pos[1 + 2 + 3 + 1];

			if (listen_channel == SOCIAL_CHAN_1 ||
				listen_channel == SOCIAL_CHAN_2 ||
				listen_channel == SOCIAL_CHAN_3) {
				CFGP2P_DBG((" Found my Listen Channel %d \n", listen_channel));
				return listen_channel;
			}
		}
		pos += 3 + attr_len;
	}
	return 0;
}

#ifdef WL_SCAN_TYPE
static u32
wl_cfgscan_map_nl80211_scan_type(struct bcm_cfg80211 *cfg, struct cfg80211_scan_request *request)
{
	u32 scan_flags = 0;

	if (!request) {
		return scan_flags;
	}

	if (request->flags & NL80211_SCAN_FLAG_LOW_SPAN) {
		scan_flags |= WL_SCANFLAGS_LOW_SPAN;
	}
	if (request->flags & NL80211_SCAN_FLAG_HIGH_ACCURACY) {
		scan_flags |= WL_SCANFLAGS_HIGH_ACCURACY;
	}
	if (request->flags & NL80211_SCAN_FLAG_LOW_POWER) {
		scan_flags |= WL_SCANFLAGS_LOW_POWER_SCAN;
	}
	if (request->flags & NL80211_SCAN_FLAG_LOW_PRIORITY) {
		scan_flags |= WL_SCANFLAGS_LOW_PRIO;
	}

	WL_INFORM(("scan flags. wl:%x cfg80211:%x\n", scan_flags, request->flags));
	return scan_flags;
}
#endif /* WL_SCAN_TYPE */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
#define IS_RADAR_CHAN(flags) (flags & (IEEE80211_CHAN_RADAR | IEEE80211_CHAN_PASSIVE_SCAN))
#else
#define IS_RADAR_CHAN(flags) (flags & (IEEE80211_CHAN_RADAR | IEEE80211_CHAN_NO_IR))
#endif // endif
static void
wl_cfgscan_populate_scan_channels(struct bcm_cfg80211 *cfg, u16 *channel_list,
	struct cfg80211_scan_request *request, u32 *num_channels)
{
	u32 i = 0, j = 0;
	u32 channel;
	u32 n_channels = 0;
	u32 chanspec = 0;

	if (!request || !request->n_channels) {
		/* Do full channel scan */
		return;
	}

	n_channels = request->n_channels;
	for (i = 0; i < n_channels; i++) {
			channel = ieee80211_frequency_to_channel(request->channels[i]->center_freq);
			/* SKIP DFS channels for Secondary interface */
			if ((cfg->escan_info.ndev != bcmcfg_to_prmry_ndev(cfg)) &&
				(IS_RADAR_CHAN(request->channels[i]->flags)))
				continue;

			chanspec = WL_CHANSPEC_BW_20;
			if (chanspec == INVCHANSPEC) {
				WL_ERR(("Invalid chanspec! Skipping channel\n"));
				continue;
			}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
			if (request->channels[i]->band == IEEE80211_BAND_60GHZ) {
				/* Not supported */
				continue;
			}
#endif /* LINUX_VER >= 3.6 */

#ifdef WL_6E
			if (request->channels[i]->band == IEEE80211_BAND_6GHZ) {
				chanspec |= WL_CHANSPEC_BAND_6G;
			} else
#endif /* WL_6E */
			if (request->channels[i]->band == IEEE80211_BAND_2GHZ) {
#ifdef WL_HOST_BAND_MGMT
				if (cfg->curr_band == WLC_BAND_5G) {
					WL_DBG(("In 5G only mode, omit 2G channel:%d\n", channel));
					continue;
				}
#endif /* WL_HOST_BAND_MGMT */
				chanspec |= WL_CHANSPEC_BAND_2G;
			} else {
#ifdef WL_HOST_BAND_MGMT
				if (cfg->curr_band == WLC_BAND_2G) {
					WL_DBG(("In 2G only mode, omit 5G channel:%d\n", channel));
					continue;
				}
#endif /* WL_HOST_BAND_MGMT */
				chanspec |= WL_CHANSPEC_BAND_5G;
			}
			channel_list[j] = channel;
			channel_list[j] &= WL_CHANSPEC_CHAN_MASK;
			channel_list[j] |= chanspec;
			WL_SCAN(("Chan : %d, Channel spec: %x \n",
				channel, channel_list[j]));
			channel_list[j] = wl_chspec_host_to_driver(channel_list[j]);
			j++;
	}
	*num_channels = j;

}

static void
wl_cfgscan_populate_scan_ssids(struct bcm_cfg80211 *cfg, u8 *buf_ptr, u32 buf_len,
	struct cfg80211_scan_request *request, u32 *ssid_num)
{
	u32 n_ssids;
	wlc_ssid_t ssid;
	int i, j = 0;

	if (!request || !buf_ptr) {
		/* Do full channel scan */
		return;
	}

	n_ssids = request->n_ssids;
	if (n_ssids > 0) {

		if (buf_len < (n_ssids * sizeof(wlc_ssid_t))) {
			WL_ERR(("buf len not sufficient for scan ssids\n"));
			return;
		}

		for (i = 0; i < n_ssids; i++) {
			bzero(&ssid, sizeof(wlc_ssid_t));
			ssid.SSID_len = MIN(request->ssids[i].ssid_len, DOT11_MAX_SSID_LEN);
			/* Returning void here, as per previous line copy length does not exceed
			* DOT11_MAX_SSID_LEN
			*/
			(void)memcpy_s(ssid.SSID, DOT11_MAX_SSID_LEN, request->ssids[i].ssid,
				ssid.SSID_len);
			if (!ssid.SSID_len) {
				WL_SCAN(("%d: Broadcast scan\n", i));
			} else {
				WL_SCAN(("%d: scan  for  %s size =%d\n", i,
				ssid.SSID, ssid.SSID_len));
			}
			/* For multiple ssid case copy the each SSID info the ptr below corresponds
			* to that so dest is of type wlc_ssid_t
			*/
			(void)memcpy_s(buf_ptr, sizeof(wlc_ssid_t), &ssid, sizeof(wlc_ssid_t));
			buf_ptr += sizeof(wlc_ssid_t);
			j++;
		}
	} else {
		WL_SCAN(("Broadcast scan\n"));
	}
	*ssid_num = j;
}

static s32
wl_scan_prep(struct bcm_cfg80211 *cfg, void *scan_params, u32 len,
	struct cfg80211_scan_request *request)
{
	wl_scan_params_t *params = NULL;
	wl_scan_params_v2_t *params_v2 = NULL;
	u32 scan_type = htod32(scan_type);
	u32 scan_param_size = 0;
	u32 n_channels = 0;
	u32 n_ssids = 0;
	uint16 *chan_list = NULL;
	u32 channel_offset = 0;
	u32 cur_offset;

	if (!scan_params) {
		return BCME_ERROR;
	}

	if (cfg->active_scan == PASSIVE_SCAN) {
		WL_INFORM_MEM(("Enforcing passive scan\n"));
		scan_type = WL_SCANFLAGS_PASSIVE;
	}

	WL_DBG(("Preparing Scan request\n"));
	if (cfg->scan_params_v2) {
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
#if defined(WL_SCAN_TYPE)
		scan_type  += wl_cfgscan_map_nl80211_scan_type(cfg, request);
#endif /* WL_SCAN_TYPE */

		(void)memcpy_s(&params_v2->bssid, ETHER_ADDR_LEN, &ether_bcast, ETHER_ADDR_LEN);
		params_v2->version = htod16(WL_SCAN_PARAMS_VERSION_V2);
		params_v2->length = htod16(sizeof(wl_scan_params_v2_t));
		params_v2->bss_type = DOT11_BSSTYPE_ANY;
		params_v2->scan_type = htod32(scan_type);
		params_v2->nprobes = htod32(-1);
		params_v2->active_time = htod32(-1);
		params_v2->passive_time = htod32(-1);
		params_v2->home_time = htod32(-1);
		params_v2->channel_num = 0;
		bzero(&params_v2->ssid, sizeof(wlc_ssid_t));
		chan_list = params_v2->channel_list;
	} else {
		/* scan params ver 1 */
		if (!params) {
			ASSERT(0);
			return BCME_ERROR;
		}
		(void)memcpy_s(&params->bssid, ETHER_ADDR_LEN, &ether_bcast, ETHER_ADDR_LEN);
		params->bss_type = DOT11_BSSTYPE_ANY;
		params->scan_type = 0;
		params->nprobes = htod32(-1);
		params->active_time = htod32(-1);
		params->passive_time = htod32(-1);
		params->home_time = htod32(-1);
		params->channel_num = 0;
		bzero(&params->ssid, sizeof(wlc_ssid_t));
		chan_list = params->channel_list;
	}

	if (!request) {
		/* scan_request null, do scan based on base config */
		WL_DBG(("scan_request is null\n"));
		return BCME_OK;
	}

	WL_INFORM(("n_channels:%d n_ssids:%d\n", request->n_channels, request->n_ssids));

	cur_offset = channel_offset;
	/* Copy channel array if applicable */
	if ((request->n_channels > 0) && chan_list) {
		if (len >= (scan_param_size + (request->n_channels * sizeof(u16)))) {
			wl_cfgscan_populate_scan_channels(cfg,
					chan_list, request, &n_channels);
			cur_offset += (n_channels * (sizeof(u16)));
		}
	}

	/* Copy ssid array if applicable */
	if (request->n_ssids > 0) {
		cur_offset = roundup(cur_offset, sizeof(u32));
		if (len > (cur_offset + (request->n_ssids * sizeof(wlc_ssid_t)))) {
			u32 rem_len = len - cur_offset;
			wl_cfgscan_populate_scan_ssids(cfg,
				((u8 *)scan_params + cur_offset), rem_len, request, &n_ssids);
		}
	}

	if (n_ssids || n_channels) {
		u32 channel_num =
				htod32((n_ssids << WL_SCAN_PARAMS_NSSID_SHIFT) |
				(n_channels & WL_SCAN_PARAMS_COUNT_MASK));
		if (params_v2) {
			params_v2->channel_num = channel_num;
			if (n_channels == 1) {
				params_v2->active_time = htod32(WL_SCAN_CONNECT_DWELL_TIME_MS);
				params_v2->nprobes = htod32(
					params_v2->active_time / WL_SCAN_JOIN_PROBE_INTERVAL_MS);
			}
		} else {
			params->channel_num = channel_num;
			if (n_channels == 1) {
				params->active_time = htod32(WL_SCAN_CONNECT_DWELL_TIME_MS);
				params->nprobes = htod32(
					params->active_time / WL_SCAN_JOIN_PROBE_INTERVAL_MS);
			}
		}
	}

	WL_INFORM(("scan_prep done. n_channels:%d n_ssids:%d\n", n_channels, n_ssids));
	return BCME_OK;
}

static s32
wl_get_valid_channels(struct net_device *ndev, u8 *valid_chan_list, s32 size)
{
	wl_uint32_list_t *list;
	s32 err = BCME_OK;
	if (valid_chan_list == NULL || size <= 0)
		return -ENOMEM;

	bzero(valid_chan_list, size);
	list = (wl_uint32_list_t *)(void *) valid_chan_list;
	list->count = htod32(WL_NUMCHANNELS);
	err = wldev_ioctl_get(ndev, WLC_GET_VALID_CHANNELS, valid_chan_list, size);
	if (err != 0) {
		WL_ERR(("get channels failed with %d\n", err));
	}

	return err;
}

#if defined(SAVE_CONNECTION_WHEN_CC_UPDATE)
bool
wl_update_ap_chandef(struct net_device *ndev)
{
	uint8 idx;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	dhd_pub_t *dhd = (dhd_pub_t *) cfg->pub;
	struct net_device *net = NULL;
	struct wireless_dev *wdev = NULL;
	u32 chanspec = 0;
	s32 err = BCME_OK;

	for (idx = 0; idx < DHD_MAX_IFS; idx++) {
		net = dhd_idx2net(dhd, idx);
		if (!net)
			continue;
		if (net->ieee80211_ptr) {
			wdev = net->ieee80211_ptr;
			if ((wdev->iftype == NL80211_IFTYPE_P2P_GO) ||
				(wdev->iftype == NL80211_IFTYPE_AP)) {
				chanspec = 0;
				err = wldev_iovar_getint(net, "chanspec", (u32 *)&chanspec);
				if (unlikely(err)) {
					WL_ERR(("chanspec failed %d\n", err));
					return err;
				}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
				wl_cfg80211_ch_switch_notify(net, chanspec, bcmcfg_to_wiphy(cfg));
				if (!cfg80211_chandef_valid(&wdev->chandef)) {
					wl_cfg80211_ch_switch_notify(net,
						0x1001, bcmcfg_to_wiphy(cfg));
					WL_ERR(("current channel %d is not allowed\n"
						"it MUST be stopped or"
						" moved to a valid channel immediately\n",
						CHSPEC_CHANNEL(chanspec)));
				}
#endif /* LINUX_VERSION_CODE >= (3, 5, 0) */
			}
		}
	}

	return err;
}

bool
wl_check_valid_channel_in_country(struct net_device *ndev, char *ccode, int forced)
{
	bool found = FALSE;
	s32 err = BCME_OK;
	chanspec_t chanspec = 0;
	int i, j, cnt;
	char smbuf[WLC_IOCTL_SMLEN] = {0};
	wl_channels_in_country_t *cic;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	uint32 channels[WL_NUMCHANNELS] = {0x00, };
	uint32 channel = 0;

	if (!(wl_get_drv_status(cfg, CONNECTING, ndev) ||
				wl_get_drv_status(cfg, CONNECTED, ndev))) {
		WL_DBG(("skip, not connected\n"));
		return TRUE;
	}

	cic = (wl_channels_in_country_t *)smbuf;
	cic->country_abbrev[0] = ccode[0];
	cic->country_abbrev[1] = ccode[1];
	cic->country_abbrev[2] = '\0';
	cnt = 0;
	for (i = WLC_BAND_2G; i >= WLC_BAND_5G; --i) {
		cic->band = i;
		cic->count = 0;
		cic->buflen = WLC_IOCTL_SMLEN - sizeof(cic);

		err = wldev_ioctl_get(ndev, WLC_GET_CHANNELS_IN_COUNTRY, cic, sizeof(smbuf));
		if (!err) {
			for (j = 0; j < cic->count; ++j) {
				channels[cnt++] = dtoh32(cic->channel[j]);
			}
		}
	}

	if (cnt) {
		err = wldev_iovar_getint(ndev, "chanspec", (s32 *)&chanspec);
		if (!err) {
			channel = wf_chspec_ctlchan(wl_chspec_driver_to_host(chanspec));
			for (i = 0; i < cnt; ++i) {
				if (channel == channels[i]) {
					found = TRUE;
					break;
				}
			}
		}
	}

	if (forced && !found) {
		scb_val_t scbval;

		wl_set_drv_status(cfg, DISCONNECTING, ndev);
		bzero(&scbval, sizeof(scb_val_t));
		WL_DBG(("Channel %d is not allowed on %s\n", channel, ccode));
		err = wldev_ioctl_set(ndev, WLC_DISASSOC, &scbval,
			sizeof(scb_val_t));
		if (unlikely(err)) {
			wl_clr_drv_status(cfg, DISCONNECTING, ndev);
			WL_ERR(("error(%d)\n", err));
			return err;
		}
		else {
			int wait_cnt = 50;
			while (!wl_get_drv_status(cfg, DISCONNECTING, ndev) && wait_cnt) {
				WL_DBG(("Waiting for disconnection terminated, wait_cnt: %d\n",
					wait_cnt));
				wait_cnt--;
				OSL_SLEEP(10);
			}
		}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
		/* cfg80211 expects disconnect event from DHD to release wdev->current_bss */
		CFG80211_DISCONNECTED(ndev, DOT11_RC_DEAUTH_LEAVING, NULL, 0, true, GFP_KERNEL);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)) */
	}
	return found;
}
#endif /* SAVE_CONNECTION_WHEN_CC_UPDATE */

static s32
wl_run_escan(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	struct cfg80211_scan_request *request, uint16 action)
{
	s32 err = BCME_OK;
	u32 n_channels;
	u32 n_ssids;
	s32 params_size;
	wl_escan_params_t *eparams = NULL;
	wl_escan_params_v2_t *eparams_v2 = NULL;
	u8 *scan_params = NULL;
	u8 *params = NULL;
	u8 chan_buf[sizeof(u32)*(WL_NUMCHANNELS + 1)];
	u32 num_chans = 0;
	s32 channel;
	u32 n_valid_chan;
	s32 search_state = WL_P2P_DISC_ST_SCAN;
	u32 i, j, n_nodfs = 0;
	u16 *default_chan_list = NULL;
	wl_uint32_list_t *list;
	s32 bssidx = -1;
	struct net_device *dev = NULL;
#if defined(USE_INITIAL_2G_SCAN) || defined(USE_INITIAL_SHORT_DWELL_TIME)
	bool is_first_init_2g_scan = false;
#endif /* USE_INITIAL_2G_SCAN || USE_INITIAL_SHORT_DWELL_TIME */
	p2p_scan_purpose_t	p2p_scan_purpose = P2P_SCAN_PURPOSE_MIN;
	u32 chan_mem = 0;
	u32 sync_id = 0;

	WL_DBG(("Enter \n"));

	/* scan request can come with empty request : perform all default scan */
	if (!cfg) {
		err = -EINVAL;
		goto exit;
	}

	if (cfg->scan_params_v2) {
		params_size = (WL_SCAN_PARAMS_V2_FIXED_SIZE +
				OFFSETOF(wl_escan_params_v2_t, params));
	} else {
		params_size = (WL_SCAN_PARAMS_FIXED_SIZE + OFFSETOF(wl_escan_params_t, params));
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)) && \
	defined(SUPPORT_RANDOM_MAC_SCAN)
	if ((request != NULL) && !ETHER_ISNULLADDR(request->mac_addr) &&
		!ETHER_ISNULLADDR(request->mac_addr_mask) &&
		!wl_is_wps_enrollee_active(ndev, request->ie, request->ie_len)) {
		/* Call scanmac only for valid configuration */
		err = wl_cfg80211_scan_mac_enable(ndev, request->mac_addr,
			request->mac_addr_mask);
		if (err < 0) {
			if (err == BCME_UNSUPPORTED) {
				/* Ignore if chip doesnt support the feature */
				err = BCME_OK;
			} else {
				/* For errors other than unsupported fail the scan */
				WL_ERR(("%s : failed to set random mac for host scan, %d\n",
					__FUNCTION__, err));
				err = -EAGAIN;
				goto exit;
			}
		}
	}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0) && defined(SUPPORT_RANDOM_MAC_SCAN) */

	if (!cfg->p2p_supported || !p2p_scan(cfg)) {
		/* LEGACY SCAN TRIGGER */
		WL_SCAN((" LEGACY E-SCAN START\n"));

#if defined(USE_INITIAL_2G_SCAN) || defined(USE_INITIAL_SHORT_DWELL_TIME)
		if (!request) {
			err = -EINVAL;
			goto exit;
		}
		if (ndev == bcmcfg_to_prmry_ndev(cfg) && g_first_broadcast_scan == true) {
#ifdef USE_INITIAL_2G_SCAN
			struct ieee80211_channel tmp_channel_list[CH_MAX_2G_CHANNEL];
			/* allow one 5G channel to add previous connected channel in 5G */
			bool allow_one_5g_channel = TRUE;
			j = 0;
			for (i = 0; i < request->n_channels; i++) {
				int tmp_chan = ieee80211_frequency_to_channel
					(request->channels[i]->center_freq);
				if (tmp_chan > CH_MAX_2G_CHANNEL) {
					if (allow_one_5g_channel)
						allow_one_5g_channel = FALSE;
					else
						continue;
				}
				if (j > CH_MAX_2G_CHANNEL) {
					WL_ERR(("Index %d exceeds max 2.4GHz channels %d"
						" and previous 5G connected channel\n",
						j, CH_MAX_2G_CHANNEL));
					break;
				}
				bcopy(request->channels[i], &tmp_channel_list[j],
					sizeof(struct ieee80211_channel));
				WL_SCAN(("channel of request->channels[%d]=%d\n", i, tmp_chan));
				j++;
			}
			if ((j > 0) && (j <= CH_MAX_2G_CHANNEL)) {
				for (i = 0; i < j; i++)
					bcopy(&tmp_channel_list[i], request->channels[i],
						sizeof(struct ieee80211_channel));

				request->n_channels = j;
				is_first_init_2g_scan = true;
			}
			else
				WL_ERR(("Invalid number of 2.4GHz channels %d\n", j));

			WL_SCAN(("request->n_channels=%d\n", request->n_channels));
#else /* USE_INITIAL_SHORT_DWELL_TIME */
			is_first_init_2g_scan = true;
#endif /* USE_INITIAL_2G_SCAN */
			g_first_broadcast_scan = false;
		}
#endif /* USE_INITIAL_2G_SCAN || USE_INITIAL_SHORT_DWELL_TIME */

		/* if scan request is not empty parse scan request paramters */
		if (request != NULL) {
			n_channels = request->n_channels;
			n_ssids = request->n_ssids;
			if (n_channels % 2)
				/* If n_channels is odd, add a padd of u16 */
				params_size += sizeof(u16) * (n_channels + 1);
			else
				params_size += sizeof(u16) * n_channels;

			/* Allocate space for populating ssids in wl_escan_params_t struct */
			params_size += sizeof(struct wlc_ssid) * n_ssids;
		}
		params = MALLOCZ(cfg->osh, params_size);
		if (params == NULL) {
			err = -ENOMEM;
			goto exit;
		}

		wl_escan_set_sync_id(sync_id, cfg);
		if (cfg->scan_params_v2) {
			eparams_v2 = (wl_escan_params_v2_t *)params;
			scan_params = (u8 *)&eparams_v2->params;
			eparams_v2->version = htod32(ESCAN_REQ_VERSION_V2);
			eparams_v2->action =  htod16(action);
			eparams_v2->sync_id = sync_id;
		} else {
			eparams = (wl_escan_params_t *)params;
			scan_params = (u8 *)&eparams->params;
			eparams->version = htod32(ESCAN_REQ_VERSION);
			eparams->action =  htod16(action);
			eparams->sync_id = sync_id;
		}

		if (wl_scan_prep(cfg, scan_params, params_size, request) < 0) {
			WL_ERR(("scan_prep failed\n"));
			err = -EINVAL;
			goto exit;
		}

#if defined(USE_INITIAL_2G_SCAN) || defined(USE_INITIAL_SHORT_DWELL_TIME)
		/* Override active_time to reduce scan time if it's first bradcast scan. */
		if (is_first_init_2g_scan) {
			if (eparams_v2) {
				eparams_v2->params.active_time = FIRST_SCAN_ACTIVE_DWELL_TIME_MS;
			} else {
				eparams->params.active_time = FIRST_SCAN_ACTIVE_DWELL_TIME_MS;
			}
		}
#endif /* USE_INITIAL_2G_SCAN || USE_INITIAL_SHORT_DWELL_TIME */

		wl_escan_set_type(cfg, WL_SCANTYPE_LEGACY);
		if (params_size + sizeof("escan") >= WLC_IOCTL_MEDLEN) {
			WL_ERR(("ioctl buffer length not sufficient\n"));
			MFREE(cfg->osh, params, params_size);
			err = -ENOMEM;
			goto exit;
		}

		bssidx = wl_get_bssidx_by_wdev(cfg, ndev->ieee80211_ptr);
		err = wldev_iovar_setbuf(ndev, "escan", params, params_size,
			cfg->escan_ioctl_buf, WLC_IOCTL_MEDLEN, NULL);
		WL_INFORM_MEM(("LEGACY_SCAN sync ID: %d, bssidx: %d\n", sync_id, bssidx));
		if (unlikely(err)) {
			if (err == BCME_EPERM)
				/* Scan Not permitted at this point of time */
				WL_DBG((" Escan not permitted at this time (%d)\n", err));
			else
				WL_ERR((" Escan set error (%d)\n", err));
		} else {
			DBG_EVENT_LOG((dhd_pub_t *)cfg->pub, WIFI_EVENT_DRIVER_SCAN_REQUESTED);
		}
		MFREE(cfg->osh, params, params_size);
	}
	else if (p2p_is_on(cfg) && p2p_scan(cfg)) {
		/* P2P SCAN TRIGGER */
		s32 _freq = 0;
		n_nodfs = 0;

		if (request && request->n_channels) {
			num_chans = request->n_channels;
			WL_SCAN((" chann number : %d\n", num_chans));
			chan_mem = (u32)(num_chans * sizeof(*default_chan_list));
			default_chan_list = MALLOCZ(cfg->osh, chan_mem);
			if (default_chan_list == NULL) {
				WL_ERR(("channel list allocation failed \n"));
				err = -ENOMEM;
				goto exit;
			}
			if (!wl_get_valid_channels(ndev, chan_buf, sizeof(chan_buf))) {
#ifdef P2P_SKIP_DFS
				int is_printed = false;
#endif /* P2P_SKIP_DFS */
				list = (wl_uint32_list_t *) chan_buf;
				n_valid_chan = dtoh32(list->count);
				if (n_valid_chan > WL_NUMCHANNELS) {
					WL_ERR(("wrong n_valid_chan:%d\n", n_valid_chan));
					MFREE(cfg->osh, default_chan_list, chan_mem);
					err = -EINVAL;
					goto exit;
				}

				for (i = 0; i < num_chans; i++)
				{
#ifdef WL_HOST_BAND_MGMT
					int channel_band = 0;
#endif /* WL_HOST_BAND_MGMT */
					_freq = request->channels[i]->center_freq;
					channel = ieee80211_frequency_to_channel(_freq);
#ifdef WL_HOST_BAND_MGMT
					channel_band = (channel > CH_MAX_2G_CHANNEL) ?
						WLC_BAND_5G : WLC_BAND_2G;
					if ((cfg->curr_band != WLC_BAND_AUTO) &&
						(cfg->curr_band != channel_band) &&
						!IS_P2P_SOCIAL_CHANNEL(channel))
							continue;
#endif /* WL_HOST_BAND_MGMT */

					/* ignore DFS channels */
					if (request->channels[i]->flags &
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
						(IEEE80211_CHAN_NO_IR
						| IEEE80211_CHAN_RADAR))
#else
						(IEEE80211_CHAN_RADAR
						| IEEE80211_CHAN_PASSIVE_SCAN))
#endif // endif
						continue;
#ifdef P2P_SKIP_DFS
					if (channel >= 52 && channel <= 144) {
						if (is_printed == false) {
							WL_ERR(("SKIP DFS CHANs(52~144)\n"));
							is_printed = true;
						}
						continue;
					}
#endif /* P2P_SKIP_DFS */

					for (j = 0; j < n_valid_chan; j++) {
						/* allows only supported channel on
						*  current reguatory
						*/
						if (n_nodfs >= num_chans) {
							break;
						}
						if (channel == (dtoh32(list->element[j]))) {
							default_chan_list[n_nodfs++] =
								channel;
						}
					}

				}
			}
			if (num_chans == SOCIAL_CHAN_CNT && (
						(default_chan_list[0] == SOCIAL_CHAN_1) &&
						(default_chan_list[1] == SOCIAL_CHAN_2) &&
						(default_chan_list[2] == SOCIAL_CHAN_3))) {
				/* SOCIAL CHANNELS 1, 6, 11 */
				search_state = WL_P2P_DISC_ST_SEARCH;
				p2p_scan_purpose = P2P_SCAN_SOCIAL_CHANNEL;
				WL_DBG(("P2P SEARCH PHASE START \n"));
			} else if (((dev = wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_CONNECTION1)) &&
				(wl_get_mode_by_netdev(cfg, dev) == WL_MODE_AP)) ||
				((dev = wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_CONNECTION2)) &&
				(wl_get_mode_by_netdev(cfg, dev) == WL_MODE_AP))) {
				/* If you are already a GO, then do SEARCH only */
				WL_DBG(("Already a GO. Do SEARCH Only"));
				search_state = WL_P2P_DISC_ST_SEARCH;
				num_chans = n_nodfs;
				p2p_scan_purpose = P2P_SCAN_NORMAL;

			} else if (num_chans == 1) {
				p2p_scan_purpose = P2P_SCAN_CONNECT_TRY;
				WL_INFORM_MEM(("Trigger p2p join scan\n"));
			} else if (num_chans == SOCIAL_CHAN_CNT + 1) {
			/* SOCIAL_CHAN_CNT + 1 takes care of the Progressive scan supported by
			 * the supplicant
			 */
				p2p_scan_purpose = P2P_SCAN_SOCIAL_CHANNEL;
			} else {
				WL_DBG(("P2P SCAN STATE START \n"));
				num_chans = n_nodfs;
				p2p_scan_purpose = P2P_SCAN_NORMAL;
			}
		} else {
			err = -EINVAL;
			goto exit;
		}
		err = wl_cfgp2p_escan(cfg, ndev, ACTIVE_SCAN, num_chans, default_chan_list,
			search_state, action,
			wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE), NULL,
			p2p_scan_purpose);

		if (!err)
			cfg->p2p->search_state = search_state;

		MFREE(cfg->osh, default_chan_list, chan_mem);
	}
exit:
	if (unlikely(err)) {
		/* Don't print Error incase of Scan suppress */
		if ((err == BCME_EPERM) && cfg->scan_suppressed)
			WL_DBG(("Escan failed: Scan Suppressed \n"));
		else
			WL_ERR(("scan error (%d)\n", err));
	}
	return err;
}

s32
wl_do_escan(struct bcm_cfg80211 *cfg, struct wiphy *wiphy, struct net_device *ndev,
	struct cfg80211_scan_request *request)
{
	s32 err = BCME_OK;
	s32 passive_scan;
	s32 passive_scan_time;
	s32 passive_scan_time_org;
	wl_scan_results_t *results;
	WL_SCAN(("Enter \n"));

	results = wl_escan_get_buf(cfg, FALSE);
	results->version = 0;
	results->count = 0;
	results->buflen = WL_SCAN_RESULTS_FIXED_SIZE;

	cfg->escan_info.ndev = ndev;
	cfg->escan_info.wiphy = wiphy;
	cfg->escan_info.escan_state = WL_ESCAN_STATE_SCANING;
	passive_scan = cfg->active_scan ? 0 : 1;
	err = wldev_ioctl_set(ndev, WLC_SET_PASSIVE_SCAN,
	                      &passive_scan, sizeof(passive_scan));
	if (unlikely(err)) {
		WL_ERR(("error (%d)\n", err));
		goto exit;
	}

	if (passive_channel_skip) {

		err = wldev_ioctl_get(ndev, WLC_GET_SCAN_PASSIVE_TIME,
			&passive_scan_time_org, sizeof(passive_scan_time_org));
		if (unlikely(err)) {
			WL_ERR(("== error (%d)\n", err));
			goto exit;
		}

		WL_SCAN(("PASSIVE SCAN time : %d \n", passive_scan_time_org));

		passive_scan_time = 0;
		err = wldev_ioctl_set(ndev, WLC_SET_SCAN_PASSIVE_TIME,
			&passive_scan_time, sizeof(passive_scan_time));
		if (unlikely(err)) {
			WL_ERR(("== error (%d)\n", err));
			goto exit;
		}

		WL_SCAN(("PASSIVE SCAN SKIPED!! (passive_channel_skip:%d) \n",
			passive_channel_skip));
	}

	err = wl_run_escan(cfg, ndev, request, WL_SCAN_ACTION_START);

	if (passive_channel_skip) {
		err = wldev_ioctl_set(ndev, WLC_SET_SCAN_PASSIVE_TIME,
			&passive_scan_time_org, sizeof(passive_scan_time_org));
		if (unlikely(err)) {
			WL_ERR(("== error (%d)\n", err));
			goto exit;
		}

		WL_SCAN(("PASSIVE SCAN RECOVERED!! (passive_scan_time_org:%d) \n",
			passive_scan_time_org));
	}

exit:
	return err;
}

static s32
wl_get_scan_timeout_val(struct bcm_cfg80211 *cfg)
{
	u32 scan_timer_interval_ms = WL_SCAN_TIMER_INTERVAL_MS;

#ifdef WES_SUPPORT
#ifdef CUSTOMER_SCAN_TIMEOUT_SETTING
	if ((cfg->custom_scan_channel_time > DHD_SCAN_ASSOC_ACTIVE_TIME) |
		(cfg->custom_scan_unassoc_time > DHD_SCAN_UNASSOC_ACTIVE_TIME) |
		(cfg->custom_scan_passive_time > DHD_SCAN_PASSIVE_TIME) |
		(cfg->custom_scan_home_time > DHD_SCAN_HOME_TIME) |
		(cfg->custom_scan_home_away_time > DHD_SCAN_HOME_AWAY_TIME)) {
		scan_timer_interval_ms = CUSTOMER_WL_SCAN_TIMER_INTERVAL_MS;
	}
#endif /* CUSTOMER_SCAN_TIMEOUT_SETTING */
#endif /* WES_SUPPORT */

	/* If NAN is enabled adding +10 sec to the existing timeout value */
#ifdef WL_NAN
	if (cfg->nan_enable) {
		scan_timer_interval_ms += WL_SCAN_TIMER_INTERVAL_MS_NAN;
	}
#endif /* WL_NAN */
	WL_MEM(("scan_timer_interval_ms %d\n", scan_timer_interval_ms));
	return scan_timer_interval_ms;
}

#define SCAN_EBUSY_RETRY_LIMIT 20
static s32
wl_cfgscan_handle_scanbusy(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 err)
{
	s32	scanbusy_err = 0;
	static u32 busy_count = 0;

	if (!err) {
		busy_count = 0;
		return scanbusy_err;
	}
	if (err == BCME_BUSY || err == BCME_NOTREADY) {
		WL_ERR(("Scan err = (%d), busy?%d", err, -EBUSY));
		scanbusy_err = -EBUSY;
	} else if ((err == BCME_EPERM) && cfg->scan_suppressed) {
		WL_ERR(("Scan not permitted due to scan suppress\n"));
		scanbusy_err = -EPERM;
	} else {
		/* For all other fw errors, use a generic error code as return
		 * value to cfg80211 stack
		 */
		scanbusy_err = -EAGAIN;
	}

	if (scanbusy_err == -EBUSY) {
		/* Flush FW preserve buffer logs for checking failure */
		if (busy_count++ > (SCAN_EBUSY_RETRY_LIMIT/5)) {
			wl_flush_fw_log_buffer(ndev, FW_LOGSET_MASK_ALL);
		}
		if (busy_count > SCAN_EBUSY_RETRY_LIMIT) {
			struct ether_addr bssid;
			s32 ret = 0;
			dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
			if (dhd_query_bus_erros(dhdp)) {
				return BCME_NOTREADY;
			}
			dhdp->scan_busy_occurred = TRUE;
			busy_count = 0;
			WL_ERR(("Unusual continuous EBUSY error, %d %d %d %d %d %d %d %d %d\n",
				wl_get_drv_status(cfg, SCANNING, ndev),
				wl_get_drv_status(cfg, SCAN_ABORTING, ndev),
				wl_get_drv_status(cfg, CONNECTING, ndev),
				wl_get_drv_status(cfg, CONNECTED, ndev),
				wl_get_drv_status(cfg, DISCONNECTING, ndev),
				wl_get_drv_status(cfg, AP_CREATING, ndev),
				wl_get_drv_status(cfg, AP_CREATED, ndev),
				wl_get_drv_status(cfg, SENDING_ACT_FRM, ndev),
				wl_get_drv_status(cfg, SENDING_ACT_FRM, ndev)));

#if defined(DHD_DEBUG) && defined(DHD_FW_COREDUMP)
			if (dhdp->memdump_enabled) {
				dhdp->memdump_type = DUMP_TYPE_SCAN_BUSY;
				dhd_bus_mem_dump(dhdp);
			}
#endif /* DHD_DEBUG && DHD_FW_COREDUMP */

			bzero(&bssid, sizeof(bssid));
			if ((ret = wldev_ioctl_get(ndev, WLC_GET_BSSID,
				&bssid, ETHER_ADDR_LEN)) == 0) {
				WL_ERR(("FW is connected with " MACDBG "/n",
					MAC2STRDBG(bssid.octet)));
			} else {
				WL_ERR(("GET BSSID failed with %d\n", ret));
			}

			wl_cfg80211_scan_abort(cfg);

		} else {
			/* Hold the context for 400msec, so that 10 subsequent scans
			* can give a buffer of 4sec which is enough to
			* cover any on-going scan in the firmware
			*/
			WL_DBG(("Enforcing delay for EBUSY case \n"));
			msleep(400);
		}
	} else {
		busy_count = 0;
	}

	return scanbusy_err;
}

s32
__wl_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
	struct cfg80211_scan_request *request,
	struct cfg80211_ssid *this_ssid)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct cfg80211_ssid *ssids;
	struct ether_addr primary_mac;
	bool p2p_ssid;
#ifdef WL11U
	bcm_tlv_t *interworking_ie;
	u8 iw_ie[IW_IES_MAX_BUF_LEN];
	u32 iw_ie_len = 0;
#endif // endif
	s32 err = 0;
	s32 bssidx = -1;
	s32 i;
	bool escan_req_failed = false;
	s32 scanbusy_err = 0;

	unsigned long flags;
#ifdef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
	struct net_device *remain_on_channel_ndev = NULL;
#endif // endif
	/*
	 * Hostapd triggers scan before starting automatic channel selection
	 * to collect channel characteristics. However firmware scan engine
	 * doesn't support any channel characteristics collection along with
	 * scan. Hence return scan success.
	 */
#ifndef IGUANA_LEGACY_CHIPS
	if (request && (scan_req_iftype(request) == NL80211_IFTYPE_AP)) {
		WL_DBG(("Scan Command on SoftAP Interface. Ignoring...\n"));
		return 0;
	}
#endif // endif

	ndev = ndev_to_wlc_ndev(ndev, cfg);

	if (WL_DRV_STATUS_SENDING_AF_FRM_EXT(cfg)) {
		WL_ERR(("Sending Action Frames. Try it again.\n"));
		return -EAGAIN;
	}

	WL_DBG(("Enter wiphy (%p)\n", wiphy));
	if (wl_get_drv_status_all(cfg, SCANNING)) {
		if (cfg->scan_request == NULL) {
			wl_clr_drv_status_all(cfg, SCANNING);
			WL_DBG(("<<<<<<<<<<<Force Clear Scanning Status>>>>>>>>>>>\n"));
		} else {
			WL_ERR(("Scanning already\n"));
			return -EAGAIN;
		}
	}
	if (wl_get_drv_status(cfg, SCAN_ABORTING, ndev)) {
		WL_ERR(("Scanning being aborted\n"));
		return -EAGAIN;
	}
	if (request && request->n_ssids > WL_SCAN_PARAMS_SSID_MAX) {
		WL_ERR(("request null or n_ssids > WL_SCAN_PARAMS_SSID_MAX\n"));
		return -EOPNOTSUPP;
	}
#if !defined(DISALBE_11H) && defined(DHD_NOSCAN_DURING_CSA)
	if (request && cfg->in_csa) {
		WL_ERR(("Scanning is not allowed during CSA\n"));
		return -EAGAIN;
	}
#endif /* DISABLE_11H */
#ifdef WL_BCNRECV
	/* check fakeapscan in progress then abort */
	wl_android_bcnrecv_stop(ndev, WL_BCNRECV_SCANBUSY);
#endif /* WL_BCNRECV */

#ifdef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
	mutex_lock(&cfg->scan_sync);
	remain_on_channel_ndev = wl_cfg80211_get_remain_on_channel_ndev(cfg);
	if (remain_on_channel_ndev) {
		WL_DBG(("Remain_on_channel bit is set, somehow it didn't get cleared\n"));
		wl_notify_escan_complete(cfg, remain_on_channel_ndev, true, true);
	}
	mutex_unlock(&cfg->scan_sync);
#endif /* WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST */

#ifdef P2P_LISTEN_OFFLOADING
	wl_cfg80211_cancel_p2plo(cfg);
#endif /* P2P_LISTEN_OFFLOADING */

	if (request) {		/* scan bss */
		ssids = request->ssids;
		p2p_ssid = false;
		for (i = 0; i < request->n_ssids; i++) {
			if (ssids[i].ssid_len &&
				IS_P2P_SSID(ssids[i].ssid, ssids[i].ssid_len)) {
				/* P2P Scan */
#ifdef WL_BLOCK_P2P_SCAN_ON_STA
				if (!(IS_P2P_IFACE(request->wdev))) {
					/* P2P scan on non-p2p iface. Fail scan */
					WL_ERR(("p2p_search on non p2p iface\n"));
					goto scan_out;
				}
#endif /* WL_BLOCK_P2P_SCAN_ON_STA */
				p2p_ssid = true;
				break;
			}
		}
		if (p2p_ssid) {
			if (cfg->p2p_supported) {
				/* p2p scan trigger */
				if (p2p_on(cfg) == false) {
					/* p2p on at the first time */
					p2p_on(cfg) = true;
					wl_cfgp2p_set_firm_p2p(cfg);
					get_primary_mac(cfg, &primary_mac);
					wl_cfgp2p_generate_bss_mac(cfg, &primary_mac);
#if defined(P2P_IE_MISSING_FIX)
					cfg->p2p_prb_noti = false;
#endif // endif
				}
				wl_clr_p2p_status(cfg, GO_NEG_PHASE);
				WL_DBG(("P2P: GO_NEG_PHASE status cleared \n"));
				p2p_scan(cfg) = true;
			}
		} else {
			/* legacy scan trigger
			 * So, we have to disable p2p discovery if p2p discovery is on
			 */
			if (cfg->p2p_supported) {
				p2p_scan(cfg) = false;
				/* If Netdevice is not equals to primary and p2p is on
				*  , we will do p2p scan using P2PAPI_BSSCFG_DEVICE.
				*/

				if (p2p_scan(cfg) == false) {
					if (wl_get_p2p_status(cfg, DISCOVERY_ON)) {
						err = wl_cfgp2p_discover_enable_search(cfg,
						false);
						if (unlikely(err)) {
							goto scan_out;
						}

					}
				}
			}
			if (!cfg->p2p_supported || !p2p_scan(cfg)) {
				if ((bssidx = wl_get_bssidx_by_wdev(cfg,
					ndev->ieee80211_ptr)) < 0) {
					WL_ERR(("Find p2p index from ndev(%p) failed\n",
						ndev));
					err = BCME_ERROR;
					goto scan_out;
				}
#ifdef WL11U
				wl_get_iwdata_by_netdev(cfg, ndev, iw_ie, &iw_ie_len);
				if (request && (interworking_ie = wl_cfg80211_find_interworking_ie(
						request->ie, request->ie_len)) != NULL) {
					if ((err = wl_cfg80211_add_iw_ie(cfg, ndev, bssidx,
							VNDR_IE_CUSTOM_FLAG, interworking_ie->id,
							interworking_ie->data,
							interworking_ie->len)) != BCME_OK) {
						WL_ERR(("Failed to add interworking IE"));
					}
				} else if (iw_ie_len != 0) {
					/* we have to clear IW IE and disable gratuitous APR */
					wl_cfg80211_clear_iw_ie(cfg, ndev, bssidx);
					err = wldev_iovar_setint_bsscfg(ndev, "grat_arp",
					                                0, bssidx);
					/* we don't care about error here
					 * because the only failure case is unsupported,
					 * which is fine
					 */
					if (unlikely(err)) {
						WL_ERR(("Set grat_arp failed:(%d) Ignore!\n", err));
					}
					wl_clear_iwdata_by_netdev(cfg, ndev);
					cfg->wl11u = FALSE;
				}
#endif /* WL11U */
				if (request) {
					err = wl_cfg80211_set_mgmt_vndr_ies(cfg,
						ndev_to_cfgdev(ndev), bssidx, VNDR_IE_PRBREQ_FLAG,
						request->ie, request->ie_len);
				}

				if (unlikely(err)) {
					goto scan_out;
				}

			}
		}
	} else {		/* scan in ibss */
		ssids = this_ssid;
	}

	if (request && cfg->p2p_supported) {
		WL_TRACE_HW4(("START SCAN\n"));
#if defined(OEM_ANDROID)
		DHD_OS_SCAN_WAKE_LOCK_TIMEOUT((dhd_pub_t *)(cfg->pub),
			SCAN_WAKE_LOCK_TIMEOUT);
		DHD_DISABLE_RUNTIME_PM((dhd_pub_t *)(cfg->pub));
#endif // endif
	}

	if (cfg->p2p_supported) {
		if (request && p2p_on(cfg) && p2p_scan(cfg)) {

			/* find my listen channel */
			cfg->afx_hdl->my_listen_chan =
				wl_find_listen_channel(cfg, request->ie,
				request->ie_len);
			err = wl_cfgp2p_enable_discovery(cfg, ndev,
			request->ie, request->ie_len);

			if (unlikely(err)) {
				goto scan_out;
			}
		}
	}

	mutex_lock(&cfg->scan_sync);
	err = wl_do_escan(cfg, wiphy, ndev, request);
	if (likely(!err)) {
		goto scan_success;
	} else {
		escan_req_failed = true;
		goto scan_out;
	}

scan_success:
	wl_cfgscan_handle_scanbusy(cfg, ndev, BCME_OK);
	cfg->scan_request = request;
	wl_set_drv_status(cfg, SCANNING, ndev);
	/* Arm the timer */
	mod_timer(&cfg->scan_timeout,
		jiffies + msecs_to_jiffies(wl_get_scan_timeout_val(cfg)));
	mutex_unlock(&cfg->scan_sync);
	return 0;

scan_out:
	if (escan_req_failed) {
		WL_CFG_DRV_LOCK(&cfg->cfgdrv_lock, flags);
		cfg->scan_request = NULL;
		WL_CFG_DRV_UNLOCK(&cfg->cfgdrv_lock, flags);
		mutex_unlock(&cfg->scan_sync);
		/* Handling for scan busy errors */
		scanbusy_err = wl_cfgscan_handle_scanbusy(cfg, ndev, err);
		if (scanbusy_err == BCME_NOTREADY) {
			/* In case of bus failures avoid ioctl calls */
#if defined(OEM_ANDROID)
			DHD_OS_SCAN_WAKE_UNLOCK((dhd_pub_t *)(cfg->pub));
#endif // endif
			return -ENODEV;
		}
		err = scanbusy_err;
	}

#if defined(OEM_ANDROID)
	DHD_OS_SCAN_WAKE_UNLOCK((dhd_pub_t *)(cfg->pub));
#endif // endif
	return err;
}

#if defined(WL_CFG80211_P2P_DEV_IF)
s32
wl_cfg80211_scan(struct wiphy *wiphy, struct cfg80211_scan_request *request)
#else
s32
wl_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
	struct cfg80211_scan_request *request)
#endif /* WL_CFG80211_P2P_DEV_IF */
{
	s32 err = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
#if defined(WL_CFG80211_P2P_DEV_IF)
	struct net_device *ndev = wdev_to_wlc_ndev(request->wdev, cfg);
#endif /* WL_CFG80211_P2P_DEV_IF */

	WL_DBG(("Enter\n"));
	RETURN_EIO_IF_NOT_UP(cfg);

#ifdef DHD_IFDEBUG
#ifdef WL_CFG80211_P2P_DEV_IF
	PRINT_WDEV_INFO(request->wdev);
#else
	PRINT_WDEV_INFO(ndev);
#endif /* WL_CFG80211_P2P_DEV_IF */
#endif /* DHD_IFDEBUG */

#ifndef IGUANA_LEGACY_CHIPS
	if (ndev == bcmcfg_to_prmry_ndev(cfg)) {
		if (wl_cfg_multip2p_operational(cfg)) {
			WL_ERR(("wlan0 scan failed, p2p devices are operational"));
			 return -ENODEV;
		}
	}
#endif // endif
	err = __wl_cfg80211_scan(wiphy, ndev, request, NULL);
	if (unlikely(err)) {
		WL_ERR(("scan error (%d)\n", err));
	}
#ifdef WL_DRV_AVOID_SCANCACHE
	/* Reset roam cache after successful scan request */
#ifdef ROAM_CHANNEL_CACHE
	if (!err) {
		reset_roam_cache(cfg);
	}
#endif /* ROAM_CHANNEL_CACHE */
#endif /* WL_DRV_AVOID_SCANCACHE */
	return err;
}

/* Note: This API should be invoked with scan_sync mutex
 * held so that scan_request data structures doesn't
 * get modified in between.
 */
struct wireless_dev *
wl_get_scan_wdev(struct bcm_cfg80211 *cfg)
{
	struct wireless_dev *wdev = NULL;

	if (!cfg) {
		WL_ERR(("cfg ptr null\n"));
		return NULL;
	}

	if (!cfg->scan_request && !cfg->sched_scan_req) {
		/* No scans in progress */
		WL_MEM(("no scan in progress \n"));
		return NULL;
	}

	if (cfg->scan_request) {
		wdev = GET_SCAN_WDEV(cfg->scan_request);
#ifdef WL_SCHED_SCAN
	} else if (cfg->sched_scan_req) {
		wdev = GET_SCHED_SCAN_WDEV(cfg->sched_scan_req);
#endif /* WL_SCHED_SCAN */
	} else {
		WL_MEM(("no scan in progress \n"));
	}

	return wdev;
}

void wl_cfg80211_cancel_scan(struct bcm_cfg80211 *cfg)
{
	struct wireless_dev *wdev = NULL;
	struct net_device *ndev = NULL;

	mutex_lock(&cfg->scan_sync);
	if (!cfg->scan_request && !cfg->sched_scan_req) {
		/* No scans in progress */
		WL_INFORM_MEM(("No scan in progress\n"));
		goto exit;
	}

	wdev = wl_get_scan_wdev(cfg);
	if (!wdev) {
		WL_ERR(("No wdev present\n"));
		goto exit;
	}

	ndev = wdev_to_wlc_ndev(wdev, cfg);
	wl_notify_escan_complete(cfg, ndev, true, true);
	WL_INFORM_MEM(("Scan aborted! \n"));
exit:
	mutex_unlock(&cfg->scan_sync);
}

void wl_cfg80211_scan_abort(struct bcm_cfg80211 *cfg)
{
	void *params = NULL;
	s32 params_size = 0;
	s32 err = BCME_OK;
	struct net_device *dev = bcmcfg_to_prmry_ndev(cfg);
	u32 channel, channel_num;

	if (!in_atomic()) {
		/* Abort scan params only need space for 1 channel and 0 ssids */
		if (cfg->scan_params_v2) {
			params_size = WL_SCAN_PARAMS_V2_FIXED_SIZE + 1 * sizeof(uint16);
		} else {
			params_size = WL_SCAN_PARAMS_FIXED_SIZE + 1 * sizeof(uint16);
		}
		params = MALLOCZ(cfg->osh, params_size);
		if (params == NULL) {
			WL_ERR(("mem alloc failed (%d bytes)\n", params_size));
			return;
		}

		/* Use magic value of channel=-1 to abort scan */
		channel = htodchanspec(-1);
		channel_num = htod32((0 << WL_SCAN_PARAMS_NSSID_SHIFT) |
				(1 & WL_SCAN_PARAMS_COUNT_MASK));
		if (cfg->scan_params_v2) {
			wl_scan_params_v2_t *params_v2 = (wl_scan_params_v2_t *)params;
			params_v2->channel_list[0] = channel;
			params_v2->channel_num = channel_num;
		} else {
			wl_scan_params_t *params_v1 = (wl_scan_params_t *)params;
			params_v1->channel_list[0] = channel;
			params_v1->channel_num = channel_num;
		}
		/* Do a scan abort to stop the driver's scan engine */
		err = wldev_ioctl_set(dev, WLC_SCAN, params, params_size);
		if (err < 0) {
			/* scan abort can fail if there is no outstanding scan */
			WL_DBG(("scan abort  failed. ret:%d\n", err));
		}
		MFREE(cfg->osh, params, params_size);
	}
#ifdef WLTDLS
	if (cfg->tdls_mgmt_frame) {
		MFREE(cfg->osh, cfg->tdls_mgmt_frame, cfg->tdls_mgmt_frame_len);
		cfg->tdls_mgmt_frame = NULL;
		cfg->tdls_mgmt_frame_len = 0;
	}
#endif /* WLTDLS */
}

s32 wl_notify_escan_complete(struct bcm_cfg80211 *cfg,
	struct net_device *ndev,
	bool aborted, bool fw_abort)
{
	s32 err = BCME_OK;
	unsigned long flags;
	struct net_device *dev;
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);

	WL_DBG(("Enter \n"));
	BCM_REFERENCE(dhdp);

	if (!ndev) {
		WL_ERR(("ndev is null\n"));
		err = BCME_ERROR;
		goto out;
	}

	if (cfg->escan_info.ndev != ndev) {
		WL_ERR(("Outstanding scan req ndev not matching (%p:%p)\n",
			cfg->escan_info.ndev, ndev));
		err = BCME_ERROR;
		goto out;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)) && \
	defined(SUPPORT_RANDOM_MAC_SCAN)
		/* Disable scanmac if enabled */
		if (cfg->scanmac_enabled) {
			wl_cfg80211_scan_mac_disable(ndev);
		}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0) && defined(SUPPORT_RANDOM_MAC_SCAN) */

	if (cfg->scan_request) {
		dev = bcmcfg_to_prmry_ndev(cfg);
#if defined(WL_ENABLE_P2P_IF)
		if (cfg->scan_request->dev != cfg->p2p_net)
			dev = cfg->scan_request->dev;
#elif defined(WL_CFG80211_P2P_DEV_IF)
		if (cfg->scan_request->wdev->iftype != NL80211_IFTYPE_P2P_DEVICE)
			dev = cfg->scan_request->wdev->netdev;
#endif /* WL_ENABLE_P2P_IF */
	}
	else {
		WL_DBG(("cfg->scan_request is NULL. Internal scan scenario."
			"doing scan_abort for ndev %p primary %p",
			ndev, bcmcfg_to_prmry_ndev(cfg)));
		dev = ndev;
	}
	if (fw_abort && !in_atomic())
		wl_cfg80211_scan_abort(cfg);
	if (timer_pending(&cfg->scan_timeout))
		del_timer_sync(&cfg->scan_timeout);
	cfg->scan_enq_time = 0;
#if defined(ESCAN_RESULT_PATCH)
	if (likely(cfg->scan_request)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
		if (aborted && cfg->p2p && p2p_scan(cfg) &&
			(cfg->scan_request->flags & NL80211_SCAN_FLAG_FLUSH)) {
			WL_ERR(("scan list is changed"));
			cfg->bss_list = wl_escan_get_buf(cfg, !aborted);
		} else
#endif // endif
			cfg->bss_list = wl_escan_get_buf(cfg, aborted);

		wl_inform_bss(cfg);
	}
#endif /* ESCAN_RESULT_PATCH */
	WL_CFG_DRV_LOCK(&cfg->cfgdrv_lock, flags);
#ifdef WL_SCHED_SCAN
	if (cfg->sched_scan_req && !cfg->scan_request) {
		if (!aborted) {
			WL_INFORM_MEM(("[%s] Report sched scan done.\n", dev->name));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0))
			cfg80211_sched_scan_results(cfg->sched_scan_req->wiphy,
				cfg->sched_scan_req->reqid);
#else
			cfg80211_sched_scan_results(cfg->sched_scan_req->wiphy);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)) */
		}

		DBG_EVENT_LOG(dhdp, WIFI_EVENT_DRIVER_PNO_SCAN_COMPLETE);
		cfg->sched_scan_running = FALSE;
		cfg->sched_scan_req = NULL;
	}
#endif /* WL_SCHED_SCAN */
	if (likely(cfg->scan_request)) {
		WL_INFORM_MEM(("[%s] Report scan done.\n", dev->name));
		/* scan_sync mutex is already held */
		_wl_notify_scan_done(cfg, aborted);
		cfg->scan_request = NULL;
	}
	if (cfg->p2p && p2p_is_on(cfg))
		wl_clr_p2p_status(cfg, SCANNING);
	wl_clr_drv_status(cfg, SCANNING, dev);

#if defined(OEM_ANDROID)
	DHD_OS_SCAN_WAKE_UNLOCK((dhd_pub_t *)(cfg->pub));
	DHD_ENABLE_RUNTIME_PM((dhd_pub_t *)(cfg->pub));
#endif // endif
	WL_CFG_DRV_UNLOCK(&cfg->cfgdrv_lock, flags);

out:
	return err;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0))
void
wl_cfg80211_abort_scan(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct bcm_cfg80211 *cfg;

	WL_DBG(("Enter wl_cfg80211_abort_scan\n"));
	cfg = wiphy_priv(wdev->wiphy);

	/* Check if any scan in progress only then abort */
	if (wl_get_drv_status_all(cfg, SCANNING)) {
		wl_cfg80211_scan_abort(cfg);
		/* Only scan abort is issued here. As per the expectation of abort_scan
		* the status of abort is needed to be communicated using cfg80211_scan_done call.
		* Here we just issue abort request and let the scan complete path to indicate
		* abort to cfg80211 layer.
		*/
		WL_DBG(("wl_cfg80211_abort_scan: Scan abort issued to FW\n"));
	}
}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)) */

int wl_cfg80211_scan_stop(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev)
{
	int ret = 0;

	WL_TRACE(("Enter\n"));

	if (!cfg || !cfgdev) {
		return -EINVAL;
	}

	/* cancel scan and notify scan status */
	wl_cfg80211_cancel_scan(cfg);

	return ret;
}

/* This API is just meant as a wrapper for cfg80211_scan_done
 * API. This doesn't do state mgmt. For cancelling scan,
 * please use wl_cfg80211_cancel_scan API.
 */
static void
_wl_notify_scan_done(struct bcm_cfg80211 *cfg, bool aborted)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
	struct cfg80211_scan_info info;
#endif // endif

	if (!cfg->scan_request) {
		return;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
	memset_s(&info, sizeof(struct cfg80211_scan_info), 0, sizeof(struct cfg80211_scan_info));
	info.aborted = aborted;
	cfg80211_scan_done(cfg->scan_request, &info);
#else
	cfg80211_scan_done(cfg->scan_request, aborted);
#endif // endif
	cfg->scan_request = NULL;
}

#ifdef WL_DRV_AVOID_SCANCACHE
static u32 wl_p2p_find_peer_channel(struct bcm_cfg80211 *cfg, s32 status, wl_bss_info_t *bi,
		u32 bi_length)
{
	u32 ret;
	u8 *p2p_dev_addr = NULL;

	ret = wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL);
	if (!ret) {
		return ret;
	}
	if (status == WLC_E_STATUS_PARTIAL) {
		p2p_dev_addr = wl_cfgp2p_retreive_p2p_dev_addr(bi, bi_length);
		if (p2p_dev_addr && !memcmp(p2p_dev_addr,
			cfg->afx_hdl->tx_dst_addr.octet, ETHER_ADDR_LEN)) {
			s32 channel = wf_chspec_ctlchan(
				wl_chspec_driver_to_host(bi->chanspec));

			if ((channel > MAXCHANNEL) || (channel <= 0)) {
				channel = WL_INVALID;
			} else {
				WL_ERR(("ACTION FRAME SCAN : Peer " MACDBG " found,"
					" channel : %d\n",
					MAC2STRDBG(cfg->afx_hdl->tx_dst_addr.octet),
					channel));
			}
			wl_clr_p2p_status(cfg, SCANNING);
			cfg->afx_hdl->peer_chan = channel;
			complete(&cfg->act_frm_scan);
		}
	} else {
		WL_INFORM_MEM(("ACTION FRAME SCAN DONE\n"));
		wl_clr_p2p_status(cfg, SCANNING);
		wl_clr_drv_status(cfg, SCANNING, cfg->afx_hdl->dev);
		if (cfg->afx_hdl->peer_chan == WL_INVALID)
			complete(&cfg->act_frm_scan);
	}

	return ret;
}

static s32 wl_escan_without_scan_cache(struct bcm_cfg80211 *cfg, wl_escan_result_t *escan_result,
	struct net_device *ndev, const wl_event_msg_t *e, s32 status)
{
	s32 err = BCME_OK;
	wl_bss_info_t *bi;
	u32 bi_length;
	bool aborted = false;
	bool fw_abort = false;
	bool notify_escan_complete = false;

	if (wl_escan_check_sync_id(status, escan_result->sync_id,
		cfg->escan_info.cur_sync_id) < 0) {
		goto exit;
	}

	wl_escan_print_sync_id(status, escan_result->sync_id,
			cfg->escan_info.cur_sync_id);

	if (!(status == WLC_E_STATUS_TIMEOUT) || !(status == WLC_E_STATUS_PARTIAL)) {
		cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
	}

	if ((likely(cfg->scan_request)) || (cfg->sched_scan_running)) {
		notify_escan_complete = true;
	}

	if (status == WLC_E_STATUS_PARTIAL) {
		WL_DBG(("WLC_E_STATUS_PARTIAL \n"));
		DBG_EVENT_LOG((dhd_pub_t *)cfg->pub, WIFI_EVENT_DRIVER_SCAN_RESULT_FOUND);
		if ((!escan_result) || (dtoh16(escan_result->bss_count) != 1)) {
			WL_ERR(("Invalid escan result (NULL pointer) or invalid bss_count\n"));
			goto exit;
		}

		bi = escan_result->bss_info;
		bi_length = dtoh32(bi->length);
		if ((!bi) ||
		(bi_length != (dtoh32(escan_result->buflen) - WL_ESCAN_RESULTS_FIXED_SIZE))) {
			WL_ERR(("Invalid escan bss info (NULL pointer)"
				"or invalid bss_info length\n"));
			goto exit;
		}

		if (!(bcmcfg_to_wiphy(cfg)->interface_modes & BIT(NL80211_IFTYPE_ADHOC))) {
			if (dtoh16(bi->capability) & DOT11_CAP_IBSS) {
				WL_DBG(("Ignoring IBSS result\n"));
				goto exit;
			}
		}

		if (wl_p2p_find_peer_channel(cfg, status, bi, bi_length)) {
			goto exit;
		} else {
			if (scan_req_match(cfg)) {
				/* p2p scan && allow only probe response */
				if ((cfg->p2p->search_state != WL_P2P_DISC_ST_SCAN) &&
					(bi->flags & WL_BSS_FLAGS_FROM_BEACON))
					goto exit;
			}
#ifdef ROAM_CHANNEL_CACHE
			add_roam_cache(cfg, bi);
#endif /* ROAM_CHANNEL_CACHE */
			err = wl_inform_single_bss(cfg, bi, false);
#ifdef ROAM_CHANNEL_CACHE
			/* print_roam_cache(); */
			update_roam_cache(cfg, ioctl_version);
#endif /* ROAM_CHANNEL_CACHE */

			/*
			 * !Broadcast && number of ssid = 1 && number of channels =1
			 * means specific scan to association
			 */
			if (wl_cfgp2p_is_p2p_specific_scan(cfg->scan_request)) {
				WL_ERR(("P2P assoc scan fast aborted.\n"));
				aborted = false;
				fw_abort = true;
			}
			/* Directly exit from function here and
			* avoid sending notify completion to cfg80211
			*/
			goto exit;
		}
	} else if (status == WLC_E_STATUS_SUCCESS) {
		if (wl_p2p_find_peer_channel(cfg, status, NULL, 0)) {
			goto exit;
		}
		WL_INFORM_MEM(("ESCAN COMPLETED\n"));
		DBG_EVENT_LOG((dhd_pub_t *)cfg->pub, WIFI_EVENT_DRIVER_SCAN_COMPLETE);

		/* Update escan complete status */
		aborted = false;
		fw_abort = false;

#ifdef CUSTOMER_HW4_DEBUG
		if (wl_scan_timeout_dbg_enabled)
			wl_scan_timeout_dbg_clear();
#endif /* CUSTOMER_HW4_DEBUG */
	} else if ((status == WLC_E_STATUS_ABORT) || (status == WLC_E_STATUS_NEWSCAN) ||
		(status == WLC_E_STATUS_11HQUIET) || (status == WLC_E_STATUS_CS_ABORT) ||
		(status == WLC_E_STATUS_NEWASSOC)) {
		/* Handle all cases of scan abort */

		WL_DBG(("ESCAN ABORT reason: %d\n", status));
		if (wl_p2p_find_peer_channel(cfg, status, NULL, 0)) {
			goto exit;
		}
		WL_INFORM_MEM(("ESCAN ABORTED\n"));

		/* Update escan complete status */
		aborted = true;
		fw_abort = false;

	} else if (status == WLC_E_STATUS_TIMEOUT) {
		WL_ERR(("WLC_E_STATUS_TIMEOUT : scan_request[%p]\n", cfg->scan_request));
		WL_ERR(("reason[0x%x]\n", e->reason));
		if (e->reason == 0xFFFFFFFF) {
			/* Update escan complete status */
			aborted = true;
			fw_abort = true;
		}
	} else {
		WL_ERR(("unexpected Escan Event %d : abort\n", status));

		if (wl_p2p_find_peer_channel(cfg, status, NULL, 0)) {
			goto exit;
		}
		/* Update escan complete status */
		aborted = true;
		fw_abort = false;
	}

	/* Notify escan complete status */
	if (notify_escan_complete) {
		wl_notify_escan_complete(cfg, ndev, aborted, fw_abort);
	}

exit:
	return err;

}
#endif /* WL_DRV_AVOID_SCANCACHE */

s32
wl_notify_scan_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	struct channel_info channel_inform;
	struct wl_scan_results *bss_list;
	struct net_device *ndev = NULL;
	u32 len = WL_SCAN_BUF_MAX;
	s32 err = 0;
	unsigned long flags;

	WL_DBG(("Enter \n"));

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	if (!ndev || !wl_get_drv_status(cfg, SCANNING, ndev)) {
		WL_DBG(("scan is not ready \n"));
		return err;
	}

	mutex_lock(&cfg->scan_sync);
	wl_clr_drv_status(cfg, SCANNING, ndev);
	bzero(&channel_inform, sizeof(channel_inform));
	err = wldev_ioctl_get(ndev, WLC_GET_CHANNEL, &channel_inform,
		sizeof(channel_inform));
	if (unlikely(err)) {
		WL_ERR(("scan busy (%d)\n", err));
		goto scan_done_out;
	}
	channel_inform.scan_channel = dtoh32(channel_inform.scan_channel);
	if (unlikely(channel_inform.scan_channel)) {

		WL_DBG(("channel_inform.scan_channel (%d)\n",
			channel_inform.scan_channel));
	}
	cfg->bss_list = cfg->scan_results;
	bss_list = cfg->bss_list;
	bzero(bss_list, len);
	bss_list->buflen = htod32(len);
	err = wldev_ioctl_get(ndev, WLC_SCAN_RESULTS, bss_list, len);
	if (unlikely(err) && unlikely(!cfg->scan_suppressed)) {
		WL_ERR(("%s Scan_results error (%d)\n", ndev->name, err));
		err = -EINVAL;
		goto scan_done_out;
	}
	bss_list->buflen = dtoh32(bss_list->buflen);
	bss_list->version = dtoh32(bss_list->version);
	bss_list->count = dtoh32(bss_list->count);

	err = wl_inform_bss(cfg);

scan_done_out:
	del_timer_sync(&cfg->scan_timeout);
	WL_CFG_DRV_LOCK(&cfg->cfgdrv_lock, flags);
	if (cfg->scan_request) {
		_wl_notify_scan_done(cfg, false);
		cfg->scan_request = NULL;
	}
	WL_CFG_DRV_UNLOCK(&cfg->cfgdrv_lock, flags);
	WL_DBG(("cfg80211_scan_done\n"));
	mutex_unlock(&cfg->scan_sync);
	return err;
}

void wl_notify_scan_done(struct bcm_cfg80211 *cfg, bool aborted)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
	struct cfg80211_scan_info info;

	bzero(&info, sizeof(struct cfg80211_scan_info));
	info.aborted = aborted;
	cfg80211_scan_done(cfg->scan_request, &info);
#else
	cfg80211_scan_done(cfg->scan_request, aborted);
#endif // endif
}

#if defined(SUPPORT_RANDOM_MAC_SCAN)
int
wl_cfg80211_set_random_mac(struct net_device *dev, bool enable)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	int ret;

	if (cfg->random_mac_enabled == enable) {
		WL_ERR(("Random MAC already %s\n", enable ? "Enabled" : "Disabled"));
		return BCME_OK;
	}

	if (enable) {
		ret = wl_cfg80211_random_mac_enable(dev);
	} else {
		ret = wl_cfg80211_random_mac_disable(dev);
	}

	if (!ret) {
		cfg->random_mac_enabled = enable;
	}

	return ret;
}

int
wl_cfg80211_random_mac_enable(struct net_device *dev)
{
	u8 random_mac[ETH_ALEN] = {0, };
	u8 rand_bytes[3] = {0, };
	s32 err = BCME_ERROR;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
#if !defined(LEGACY_RANDOM_MAC)
	uint8 buffer[WLC_IOCTL_SMLEN] = {0, };
	wl_scanmac_t *sm = NULL;
	int len = 0;
	wl_scanmac_enable_t *sm_enable = NULL;
	wl_scanmac_config_t *sm_config = NULL;
#endif /* !LEGACY_RANDOM_MAC */

	if (wl_get_drv_status_all(cfg, CONNECTED) || wl_get_drv_status_all(cfg, CONNECTING) ||
	    wl_get_drv_status_all(cfg, AP_CREATED) || wl_get_drv_status_all(cfg, AP_CREATING)) {
		WL_ERR(("fail to Set random mac, current state is wrong\n"));
		return err;
	}

	(void)memcpy_s(random_mac, ETH_ALEN, bcmcfg_to_prmry_ndev(cfg)->dev_addr, ETH_ALEN);
	get_random_bytes(&rand_bytes, sizeof(rand_bytes));

	if (rand_bytes[2] == 0x0 || rand_bytes[2] == 0xff) {
		rand_bytes[2] = 0xf0;
	}

#if defined(LEGACY_RANDOM_MAC)
	/* of the six bytes of random_mac the bytes 3, 4, 5 are copied with contents of rand_bytes
	* So while copying 3 bytes of content no overflow would be seen. Hence returning void.
	*/
	(void)memcpy_s(&random_mac[3], (sizeof(u8) * 3), rand_bytes, sizeof(rand_bytes));

	err = wldev_iovar_setbuf_bsscfg(bcmcfg_to_prmry_ndev(cfg), "cur_etheraddr",
		random_mac, ETH_ALEN, cfg->ioctl_buf, WLC_IOCTL_SMLEN, 0, &cfg->ioctl_buf_sync);

	if (err != BCME_OK) {
		WL_ERR(("failed to set random generate MAC address\n"));
	} else {
		WL_ERR(("set mac " MACDBG " to " MACDBG "\n",
			MAC2STRDBG((const u8 *)bcmcfg_to_prmry_ndev(cfg)->dev_addr),
			MAC2STRDBG((const u8 *)&random_mac)));
		WL_ERR(("random MAC enable done"));
	}
#else
	/* Enable scan mac */
	sm = (wl_scanmac_t *)buffer;
	sm_enable = (wl_scanmac_enable_t *)sm->data;
	sm->len = sizeof(*sm_enable);
	sm_enable->enable = 1;
	len = OFFSETOF(wl_scanmac_t, data) + sm->len;
	sm->subcmd_id = WL_SCANMAC_SUBCMD_ENABLE;

	err = wldev_iovar_setbuf_bsscfg(dev, "scanmac",
		sm, len, cfg->ioctl_buf, WLC_IOCTL_SMLEN, 0, &cfg->ioctl_buf_sync);

	/* For older chip which which does not have scanmac support can still use
	 * cur_etheraddr to set the randmac. rand_mask and rand_mac comes from upper
	 * cfg80211 layer. If rand_mask and rand_mac is not passed then fallback
	 * to default cur_etheraddr and default mask.
	 */
	if (err == BCME_UNSUPPORTED) {
		/* In case of host based legacy randomization, random address is
		 * generated by mixing 3 bytes of cur_etheraddr and 3 bytes of
		 * random bytes generated.In that case rand_mask is nothing but
		 * random bytes.
		 */
		(void)memcpy_s(&random_mac[3], (sizeof(u8) * 3), rand_bytes, sizeof(rand_bytes));
		err = wldev_iovar_setbuf_bsscfg(bcmcfg_to_prmry_ndev(cfg), "cur_etheraddr",
				random_mac, ETH_ALEN, cfg->ioctl_buf,
				WLC_IOCTL_SMLEN, 0, &cfg->ioctl_buf_sync);
		if (err != BCME_OK) {
			WL_ERR(("failed to set random generate MAC address\n"));
		} else {
			WL_ERR(("set mac " MACDBG " to " MACDBG "\n",
				MAC2STRDBG((const u8 *)bcmcfg_to_prmry_ndev(cfg)->dev_addr),
				MAC2STRDBG((const u8 *)&random_mac)));
			WL_ERR(("random MAC enable done using legacy randmac"));
		}
	} else if (err == BCME_OK) {
		/* Configure scanmac */
		(void)memset_s(buffer, sizeof(buffer), 0x0, sizeof(buffer));
		sm_config = (wl_scanmac_config_t *)sm->data;
		sm->len = sizeof(*sm_config);
		sm->subcmd_id = WL_SCANMAC_SUBCMD_CONFIG;
		sm_config->scan_bitmap = WL_SCANMAC_SCAN_UNASSOC;

		/* Set randomize mac address recv from upper layer */
		(void)memcpy_s(&sm_config->mac.octet, ETH_ALEN, random_mac, ETH_ALEN);

		/* Set randomize mask recv from upper layer */

		/* Currently in samsung case, upper layer does not provide
		 * variable randmask and its using fixed 3 byte randomization
		 */
		(void)memset_s(&sm_config->random_mask.octet, ETH_ALEN, 0x0, ETH_ALEN);
		/* Memsetting the remaining octets 3, 4, 5. So remaining dest length is 3 */
		(void)memset_s(&sm_config->random_mask.octet[3], 3, 0xFF, 3);

		WL_DBG(("recv random mac addr " MACDBG  " recv rand mask" MACDBG "\n",
			MAC2STRDBG((const u8 *)&sm_config->mac.octet),
			MAC2STRDBG((const u8 *)&sm_config->random_mask)));

		len = OFFSETOF(wl_scanmac_t, data) + sm->len;
		err = wldev_iovar_setbuf_bsscfg(dev, "scanmac",
			sm, len, cfg->ioctl_buf, WLC_IOCTL_SMLEN, 0, &cfg->ioctl_buf_sync);

		if (err != BCME_OK) {
			WL_ERR(("failed scanmac configuration\n"));

			/* Disable scan mac for clean-up */
			wl_cfg80211_random_mac_disable(dev);
			return err;
		}
		WL_DBG(("random MAC enable done using scanmac"));
	} else  {
		WL_ERR(("failed to enable scanmac, err=%d\n", err));
	}
#endif /* LEGACY_RANDOM_MAC */

	return err;
}

int
wl_cfg80211_random_mac_disable(struct net_device *dev)
{
	s32 err = BCME_ERROR;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
#if !defined(LEGACY_RANDOM_MAC)
	uint8 buffer[WLC_IOCTL_SMLEN] = {0, };
	wl_scanmac_t *sm = NULL;
	int len = 0;
	wl_scanmac_enable_t *sm_enable = NULL;
#endif /* !LEGACY_RANDOM_MAC */

#if defined(LEGACY_RANDOM_MAC)
	WL_ERR(("set original mac " MACDBG "\n",
		MAC2STRDBG((const u8 *)bcmcfg_to_prmry_ndev(cfg)->dev_addr)));

	err = wldev_iovar_setbuf_bsscfg(bcmcfg_to_prmry_ndev(cfg), "cur_etheraddr",
		bcmcfg_to_prmry_ndev(cfg)->dev_addr, ETH_ALEN,
		cfg->ioctl_buf, WLC_IOCTL_SMLEN, 0, &cfg->ioctl_buf_sync);

	if (err != BCME_OK) {
		WL_ERR(("failed to set original MAC address\n"));
	} else {
		WL_ERR(("legacy random MAC disable done \n"));
	}
#else
	sm = (wl_scanmac_t *)buffer;
	sm_enable = (wl_scanmac_enable_t *)sm->data;
	sm->len = sizeof(*sm_enable);
	/* Disable scanmac */
	sm_enable->enable = 0;
	len = OFFSETOF(wl_scanmac_t, data) + sm->len;

	sm->subcmd_id = WL_SCANMAC_SUBCMD_ENABLE;

	err = wldev_iovar_setbuf_bsscfg(dev, "scanmac",
		sm, len, cfg->ioctl_buf, WLC_IOCTL_SMLEN, 0, &cfg->ioctl_buf_sync);

	if (err != BCME_OK) {
		WL_ERR(("failed to disable scanmac, err=%d\n", err));
		return err;
	}
	/* Clear scanmac enabled status */
	cfg->scanmac_enabled = 0;
	WL_DBG(("random MAC disable done\n"));
#endif /* LEGACY_RANDOM_MAC */

	return err;
}

/*
 * This is new interface for mac randomization. It takes randmac and randmask
 * as arg and it uses scanmac iovar to offload the mac randomization to firmware.
 */
int wl_cfg80211_scan_mac_enable(struct net_device *dev, uint8 *rand_mac, uint8 *rand_mask)
{
	int byte_index = 0;
	s32 err = BCME_ERROR;
	uint8 buffer[WLC_IOCTL_SMLEN] = {0, };
	wl_scanmac_t *sm = NULL;
	int len = 0;
	wl_scanmac_enable_t *sm_enable = NULL;
	wl_scanmac_config_t *sm_config = NULL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	if ((rand_mac == NULL) || (rand_mask == NULL)) {
		err = BCME_BADARG;
		WL_ERR(("fail to Set random mac, bad argument\n"));
		/* Disable the current scanmac config */
		wl_cfg80211_scan_mac_disable(dev);
		return err;
	}

	if (ETHER_ISNULLADDR(rand_mac)) {
		WL_DBG(("fail to Set random mac, Invalid rand mac\n"));
		/* Disable the current scanmac config */
		wl_cfg80211_scan_mac_disable(dev);
		return err;
	}

	if (wl_get_drv_status_all(cfg, CONNECTED) || wl_get_drv_status_all(cfg, CONNECTING) ||
	    wl_get_drv_status_all(cfg, AP_CREATED) || wl_get_drv_status_all(cfg, AP_CREATING)) {
		WL_ERR(("fail to Set random mac, current state is wrong\n"));
		return BCME_UNSUPPORTED;
	}

	/* Enable scan mac */
	sm = (wl_scanmac_t *)buffer;
	sm_enable = (wl_scanmac_enable_t *)sm->data;
	sm->len = sizeof(*sm_enable);
	sm_enable->enable = 1;
	len = OFFSETOF(wl_scanmac_t, data) + sm->len;
	sm->subcmd_id = WL_SCANMAC_SUBCMD_ENABLE;

	err = wldev_iovar_setbuf_bsscfg(dev, "scanmac",
		sm, len, cfg->ioctl_buf, WLC_IOCTL_SMLEN, 0, &cfg->ioctl_buf_sync);

	if (err == BCME_OK) {
			/* Configure scanmac */
		(void)memset_s(buffer, sizeof(buffer), 0x0, sizeof(buffer));
		sm_config = (wl_scanmac_config_t *)sm->data;
		sm->len = sizeof(*sm_config);
		sm->subcmd_id = WL_SCANMAC_SUBCMD_CONFIG;
		sm_config->scan_bitmap = WL_SCANMAC_SCAN_UNASSOC;

		/* Set randomize mac address recv from upper layer */
		(void)memcpy_s(&sm_config->mac.octet, ETH_ALEN, rand_mac, ETH_ALEN);

		/* Set randomize mask recv from upper layer */

		/* There is a difference in how to interpret rand_mask between
		 * upperlayer and firmware. If the byte is set as FF then for
		 * upper layer it  means keep that byte and do not randomize whereas
		 * for firmware it means randomize those bytes and vice versa. Hence
		 * conversion is needed before setting the iovar
		 */
		(void)memset_s(&sm_config->random_mask.octet, ETH_ALEN, 0x0, ETH_ALEN);
		/* Only byte randomization is supported currently. If mask recv is 0x0F
		 * for a particular byte then it will be treated as no randomization
		 * for that byte.
		 */
		while (byte_index < ETH_ALEN) {
			if (rand_mask[byte_index] == 0xFF) {
				sm_config->random_mask.octet[byte_index] = 0x00;
			} else if (rand_mask[byte_index] == 0x00) {
				sm_config->random_mask.octet[byte_index] = 0xFF;
			}
			byte_index++;
		}

		WL_DBG(("recv random mac addr " MACDBG  "recv rand mask" MACDBG "\n",
			MAC2STRDBG((const u8 *)&sm_config->mac.octet),
			MAC2STRDBG((const u8 *)&sm_config->random_mask)));

		len = OFFSETOF(wl_scanmac_t, data) + sm->len;
		err = wldev_iovar_setbuf_bsscfg(dev, "scanmac",
			sm, len, cfg->ioctl_buf, WLC_IOCTL_SMLEN, 0, &cfg->ioctl_buf_sync);

		if (err != BCME_OK) {
			WL_ERR(("failed scanmac configuration\n"));

			/* Disable scan mac for clean-up */
			wl_cfg80211_random_mac_disable(dev);
			return err;
		}
		/* Mark scanmac enabled */
		cfg->scanmac_enabled = 1;
		WL_DBG(("scanmac enable done"));
	} else  {
		WL_ERR(("failed to enable scanmac, err=%d\n", err));
	}

	return err;
}

int
wl_cfg80211_scan_mac_disable(struct net_device *dev)
{
	s32 err = BCME_ERROR;

	err = wl_cfg80211_random_mac_disable(dev);

	return err;
}
#endif /* SUPPORT_RANDOM_MAC_SCAN */

#ifdef WL_SCHED_SCAN
#define PNO_TIME		30
#define PNO_REPEAT		4
#define PNO_FREQ_EXPO_MAX	2
static bool
is_ssid_in_list(struct cfg80211_ssid *ssid, struct cfg80211_ssid *ssid_list, int count)
{
	int i;

	if (!ssid || !ssid_list)
		return FALSE;

	for (i = 0; i < count; i++) {
		if (ssid->ssid_len == ssid_list[i].ssid_len) {
			if (strncmp(ssid->ssid, ssid_list[i].ssid, ssid->ssid_len) == 0)
				return TRUE;
		}
	}
	return FALSE;
}

int
wl_cfg80211_sched_scan_start(struct wiphy *wiphy,
                             struct net_device *dev,
                             struct cfg80211_sched_scan_request *request)
{
	ushort pno_time = PNO_TIME;
	int pno_repeat = PNO_REPEAT;
	int pno_freq_expo_max = PNO_FREQ_EXPO_MAX;
	wlc_ssid_ext_t ssids_local[MAX_PFN_LIST_COUNT];
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
	struct cfg80211_ssid *ssid = NULL;
	struct cfg80211_ssid *hidden_ssid_list = NULL;
	log_conn_event_t *event_data = NULL;
	tlv_log *tlv_data = NULL;
	u32 alloc_len = 0, tlv_len = 0;
	u32 payload_len;
	int ssid_cnt = 0;
	int i;
	int ret = 0;
	unsigned long flags;

	if (!request) {
		WL_ERR(("Sched scan request was NULL\n"));
		return -EINVAL;
	}

	WL_DBG(("Enter \n"));
	WL_PNO((">>> SCHED SCAN START\n"));
	WL_PNO(("Enter n_match_sets:%d   n_ssids:%d \n",
		request->n_match_sets, request->n_ssids));
	WL_PNO(("ssids:%d pno_time:%d pno_repeat:%d pno_freq:%d \n",
		request->n_ssids, pno_time, pno_repeat, pno_freq_expo_max));

	if (!request->n_ssids || !request->n_match_sets) {
		WL_ERR(("Invalid sched scan req!! n_ssids:%d \n", request->n_ssids));
		return -EINVAL;
	}

	bzero(&ssids_local, sizeof(ssids_local));

	if (request->n_ssids > 0) {
		hidden_ssid_list = request->ssids;
	}

	if (DBG_RING_ACTIVE(dhdp, DHD_EVENT_RING_ID)) {
		alloc_len = sizeof(log_conn_event_t) + DOT11_MAX_SSID_LEN;
		event_data = (log_conn_event_t *)MALLOC(cfg->osh, alloc_len);
		if (!event_data) {
			WL_ERR(("%s: failed to allocate log_conn_event_t with "
						"length(%d)\n", __func__, alloc_len));
			return -ENOMEM;
		}
		bzero(event_data, alloc_len);
		event_data->tlvs = NULL;
		tlv_len = sizeof(tlv_log);
		event_data->tlvs = (tlv_log *)MALLOC(cfg->osh, tlv_len);
		if (!event_data->tlvs) {
			WL_ERR(("%s: failed to allocate log_tlv with "
					"length(%d)\n", __func__, tlv_len));
			MFREE(cfg->osh, event_data, alloc_len);
			return -ENOMEM;
		}
	}
	for (i = 0; i < request->n_match_sets && ssid_cnt < MAX_PFN_LIST_COUNT; i++) {
		ssid = &request->match_sets[i].ssid;
		/* No need to include null ssid */
		if (ssid->ssid_len) {
			ssids_local[ssid_cnt].SSID_len = MIN(ssid->ssid_len,
				(uint32)DOT11_MAX_SSID_LEN);
			/* In previous step max SSID_len is limited to DOT11_MAX_SSID_LEN,
			* returning void
			*/
			(void)memcpy_s(ssids_local[ssid_cnt].SSID, DOT11_MAX_SSID_LEN, ssid->ssid,
				ssids_local[ssid_cnt].SSID_len);
			if (is_ssid_in_list(ssid, hidden_ssid_list, request->n_ssids)) {
				ssids_local[ssid_cnt].hidden = TRUE;
				WL_PNO((">>> PNO hidden SSID (%s) \n", ssid->ssid));
			} else {
				ssids_local[ssid_cnt].hidden = FALSE;
				WL_PNO((">>> PNO non-hidden SSID (%s) \n", ssid->ssid));
			}
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 15, 0))
			if (request->match_sets[i].rssi_thold != NL80211_SCAN_RSSI_THOLD_OFF) {
				ssids_local[ssid_cnt].rssi_thresh =
				      (int8)request->match_sets[i].rssi_thold;
			}
#endif /* (LINUX_VERSION_CODE > KERNEL_VERSION(3, 15, 0)) */
			ssid_cnt++;
		}
	}

	if (ssid_cnt) {
		if ((ret = dhd_dev_pno_set_for_ssid(dev, ssids_local, ssid_cnt,
			pno_time, pno_repeat, pno_freq_expo_max, NULL, 0)) < 0) {
			WL_ERR(("PNO setup failed!! ret=%d \n", ret));
			ret = -EINVAL;
			goto exit;
		}

		if (DBG_RING_ACTIVE(dhdp, DHD_EVENT_RING_ID)) {
			for (i = 0; i < ssid_cnt; i++) {
				payload_len = sizeof(log_conn_event_t);
				event_data->event = WIFI_EVENT_DRIVER_PNO_ADD;
				tlv_data = event_data->tlvs;
				/* ssid */
				tlv_data->tag = WIFI_TAG_SSID;
				tlv_data->len = ssids_local[i].SSID_len;
				(void)memcpy_s(tlv_data->value, DOT11_MAX_SSID_LEN,
					ssids_local[i].SSID, ssids_local[i].SSID_len);
				payload_len += TLV_LOG_SIZE(tlv_data);

				dhd_os_push_push_ring_data(dhdp, DHD_EVENT_RING_ID,
					event_data, payload_len);
			}
		}

		WL_CFG_DRV_LOCK(&cfg->cfgdrv_lock, flags);
		cfg->sched_scan_req = request;
		WL_CFG_DRV_UNLOCK(&cfg->cfgdrv_lock, flags);
	} else {
		ret = -EINVAL;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)) && \
	defined(SUPPORT_RANDOM_MAC_SCAN)
	if (!ETHER_ISNULLADDR(request->mac_addr) && !ETHER_ISNULLADDR(request->mac_addr_mask)) {
		ret = wl_cfg80211_scan_mac_enable(dev, request->mac_addr, request->mac_addr_mask);
		/* Ignore if chip doesnt support the feature */
		if (ret < 0) {
			if (ret == BCME_UNSUPPORTED) {
				/* If feature is not supported, ignore the error (legacy chips) */
				ret = BCME_OK;
			} else {
				WL_ERR(("set random mac failed (%d). Ignore.\n", ret));
				/* Cleanup the states and stop the pno */
				if (dhd_dev_pno_stop_for_ssid(dev) < 0) {
					WL_ERR(("PNO Stop for SSID failed"));
				}
				WL_CFG_DRV_LOCK(&cfg->cfgdrv_lock, flags);
				cfg->sched_scan_req = NULL;
				cfg->sched_scan_running = FALSE;
				WL_CFG_DRV_UNLOCK(&cfg->cfgdrv_lock, flags);
			}
		}
	}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)) && (defined(SUPPORT_RANDOM_MAC_SCAN)) */
exit:
	if (event_data) {
		MFREE(cfg->osh, event_data->tlvs, tlv_len);
		MFREE(cfg->osh, event_data, alloc_len);
	}
	return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0))
int
wl_cfg80211_sched_scan_stop(struct wiphy *wiphy, struct net_device *dev, u64 reqid)
#else
int
wl_cfg80211_sched_scan_stop(struct wiphy *wiphy, struct net_device *dev)
#endif /* LINUX_VERSION_CODE >=  KERNEL_VERSION(4, 11, 0) */
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
	unsigned long flags;

	WL_DBG(("Enter \n"));
	WL_PNO((">>> SCHED SCAN STOP\n"));

	if (dhd_dev_pno_stop_for_ssid(dev) < 0) {
		WL_ERR(("PNO Stop for SSID failed"));
	} else {
		DBG_EVENT_LOG(dhdp, WIFI_EVENT_DRIVER_PNO_REMOVE);
	}

	if (cfg->sched_scan_req || cfg->sched_scan_running) {
		WL_PNO((">>> Sched scan running. Aborting it..\n"));
		wl_cfg80211_cancel_scan(cfg);
	}
	WL_CFG_DRV_LOCK(&cfg->cfgdrv_lock, flags);
	cfg->sched_scan_req = NULL;
	cfg->sched_scan_running = FALSE;
	WL_CFG_DRV_UNLOCK(&cfg->cfgdrv_lock, flags);
	return 0;
}
#endif /* WL_SCHED_SCAN */

#ifdef WES_SUPPORT
#ifdef CUSTOMER_SCAN_TIMEOUT_SETTING
s32 wl_cfg80211_custom_scan_time(struct net_device *dev,
		enum wl_custom_scan_time_type type, int time)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	if (cfg == NULL) {
		return FALSE;
	}

	switch (type) {
		case WL_CUSTOM_SCAN_CHANNEL_TIME :
			WL_ERR(("Scan Channel Time %d\n", time));
			cfg->custom_scan_channel_time = time;
			break;
		case WL_CUSTOM_SCAN_UNASSOC_TIME :
			WL_ERR(("Scan Unassoc Time %d\n", time));
			cfg->custom_scan_unassoc_time = time;
			break;
		case WL_CUSTOM_SCAN_PASSIVE_TIME :
			WL_ERR(("Scan Passive Time %d\n", time));
			cfg->custom_scan_passive_time = time;
			break;
		case WL_CUSTOM_SCAN_HOME_TIME :
			WL_ERR(("Scan Home Time %d\n", time));
			cfg->custom_scan_home_time = time;
			break;
		case WL_CUSTOM_SCAN_HOME_AWAY_TIME :
			WL_ERR(("Scan Home Away Time %d\n", time));
			cfg->custom_scan_home_away_time = time;
			break;
		default:
			return FALSE;
	}
	return TRUE;
}
#endif /* CUSTOMER_SCAN_TIMEOUT_SETTING */
#endif /* WES_SUPPORT */

#ifdef CUSTOMER_HW4_DEBUG
uint prev_dhd_console_ms = 0;
u32 prev_wl_dbg_level = 0;
static void wl_scan_timeout_dbg_set(void);

static void wl_scan_timeout_dbg_set(void)
{
	WL_ERR(("Enter \n"));
	prev_dhd_console_ms = dhd_console_ms;
	prev_wl_dbg_level = wl_dbg_level;

	dhd_console_ms = 1;
	wl_dbg_level |= (WL_DBG_ERR | WL_DBG_P2P_ACTION | WL_DBG_SCAN);

	wl_scan_timeout_dbg_enabled = 1;
}
void wl_scan_timeout_dbg_clear(void)
{
	WL_ERR(("Enter \n"));
	dhd_console_ms = prev_dhd_console_ms;
	wl_dbg_level = prev_wl_dbg_level;

	wl_scan_timeout_dbg_enabled = 0;
}
#endif /* CUSTOMER_HW4_DEBUG */

static void wl_scan_timeout(unsigned long data)
{
	wl_event_msg_t msg;
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)data;
	struct wireless_dev *wdev = NULL;
	struct net_device *ndev = NULL;
	struct wl_scan_results *bss_list;
	wl_bss_info_t *bi = NULL;
	s32 i;
	u32 channel;
	u64 cur_time = OSL_LOCALTIME_NS();
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
	unsigned long flags;
#ifdef RTT_SUPPORT
	rtt_status_info_t *rtt_status = NULL;
	UNUSED_PARAMETER(rtt_status);
#endif /* RTT_SUPPORT */

	UNUSED_PARAMETER(cur_time);
	WL_CFG_DRV_LOCK(&cfg->cfgdrv_lock, flags);
	if (!(cfg->scan_request)) {
		WL_ERR(("timer expired but no scan request\n"));
		WL_CFG_DRV_UNLOCK(&cfg->cfgdrv_lock, flags);
		return;
	}

	wdev = GET_SCAN_WDEV(cfg->scan_request);
	WL_CFG_DRV_UNLOCK(&cfg->cfgdrv_lock, flags);

	if (!wdev) {
		WL_ERR(("No wireless_dev present\n"));
		return;
	}

	if (dhd_query_bus_erros(dhdp)) {
		return;
	}
#if defined(DHD_KERNEL_SCHED_DEBUG) && defined(DHD_FW_COREDUMP)
	if (dhdp->memdump_enabled == DUMP_MEMFILE_BUGON &&
		((cfg->scan_deq_time < cfg->scan_enq_time) ||
		dhd_bus_query_dpc_sched_errors(dhdp))) {
		WL_ERR(("****SCAN event timeout due to scheduling problem\n"));
		/* change g_assert_type to trigger Kernel panic */
		g_assert_type = 2;
#ifdef RTT_SUPPORT
		rtt_status = GET_RTTSTATE(dhdp);
#endif /* RTT_SUPPORT */
		WL_ERR(("***SCAN event timeout. WQ state:0x%x scan_enq_time:"SEC_USEC_FMT
			" evt_hdlr_entry_time:"SEC_USEC_FMT" evt_deq_time:"SEC_USEC_FMT
			"\nscan_deq_time:"SEC_USEC_FMT" scan_hdlr_cmplt_time:"SEC_USEC_FMT
			" scan_cmplt_time:"SEC_USEC_FMT" evt_hdlr_exit_time:"SEC_USEC_FMT
			"\ncurrent_time:"SEC_USEC_FMT"\n", work_busy(&cfg->event_work),
			GET_SEC_USEC(cfg->scan_enq_time), GET_SEC_USEC(cfg->wl_evt_hdlr_entry_time),
			GET_SEC_USEC(cfg->wl_evt_deq_time), GET_SEC_USEC(cfg->scan_deq_time),
			GET_SEC_USEC(cfg->scan_hdlr_cmplt_time), GET_SEC_USEC(cfg->scan_cmplt_time),
			GET_SEC_USEC(cfg->wl_evt_hdlr_exit_time), GET_SEC_USEC(cur_time)));
		if (cfg->scan_enq_time) {
			WL_ERR(("Elapsed time(ns): %llu\n", (cur_time - cfg->scan_enq_time)));
		}
		WL_ERR(("lock_states:[%d:%d:%d:%d:%d:%d]\n",
			mutex_is_locked(&cfg->if_sync),
			mutex_is_locked(&cfg->usr_sync),
			mutex_is_locked(&cfg->pm_sync),
			mutex_is_locked(&cfg->scan_sync),
			spin_is_locked(&cfg->cfgdrv_lock),
			spin_is_locked(&cfg->eq_lock)));
#ifdef RTT_SUPPORT
		WL_ERR(("RTT lock_state:[%d]\n",
			mutex_is_locked(&rtt_status->rtt_mutex)));
#ifdef WL_NAN
		WL_ERR(("RTT and Geofence lock_states:[%d:%d]\n",
			mutex_is_locked(&cfg->nancfg.nan_sync),
			mutex_is_locked(&(rtt_status)->geofence_mutex)));
#endif /* WL_NAN */
#endif /* RTT_SUPPORT */

		/* use ASSERT() to trigger panic */
		ASSERT(0);
	}
#endif /* DHD_KERNEL_SCHED_DEBUG && DHD_FW_COREDUMP */
	dhd_bus_intr_count_dump(dhdp);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)) && !defined(CONFIG_MODULES)
	/* Print WQ states. Enable only for in-built drivers as the symbol is not exported  */
	show_workqueue_state();
#endif /* LINUX_VER >= 4.1 && !CONFIG_MODULES */

	bss_list = wl_escan_get_buf(cfg, FALSE);
	if (!bss_list) {
		WL_ERR(("bss_list is null. Didn't receive any partial scan results\n"));
	} else {
		WL_ERR(("Dump scan buffer:\n"
			"scanned AP count (%d)\n", bss_list->count));

		bi = next_bss(bss_list, bi);
		for_each_bss(bss_list, bi, i) {
			channel = wf_chspec_ctlchan(wl_chspec_driver_to_host(bi->chanspec));
			WL_ERR(("SSID :%s  Channel :%d\n", bi->SSID, channel));
		}
	}

	ndev = wdev_to_wlc_ndev(wdev, cfg);
	bzero(&msg, sizeof(wl_event_msg_t));
	WL_ERR(("timer expired\n"));
	dhdp->scan_timeout_occurred = TRUE;
#ifdef BCMPCIE
	(void)dhd_pcie_dump_int_regs(dhdp);
	dhd_pcie_dump_rc_conf_space_cap(dhdp);
#endif /* BCMPCIE */
#ifdef DHD_FW_COREDUMP
	if (dhdp->memdump_enabled) {
		dhdp->memdump_type = DUMP_TYPE_SCAN_TIMEOUT;
		dhd_bus_mem_dump(dhdp);
	}
#endif /* DHD_FW_COREDUMP */
	msg.event_type = hton32(WLC_E_ESCAN_RESULT);
	msg.status = hton32(WLC_E_STATUS_TIMEOUT);
	msg.reason = 0xFFFFFFFF;
	wl_cfg80211_event(ndev, &msg, NULL);
#ifdef CUSTOMER_HW4_DEBUG
	if (!wl_scan_timeout_dbg_enabled)
		wl_scan_timeout_dbg_set();
#endif /* CUSTOMER_HW4_DEBUG */
}

s32 wl_init_scan(struct bcm_cfg80211 *cfg)
{
	int err = 0;

	cfg->evt_handler[WLC_E_ESCAN_RESULT] = wl_escan_handler;
	cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
	wl_escan_init_sync_id(cfg);

	/* Init scan_timeout timer */
	init_timer_compat(&cfg->scan_timeout, wl_scan_timeout, cfg);

	wl_cfg80211_set_bcmcfg(cfg);

	return err;
}

#ifdef WL_SCHED_SCAN
/* If target scan is not reliable, set the below define to "1" to do a
 * full escan
 */
#define FULL_ESCAN_ON_PFN_NET_FOUND		0
static s32
wl_notify_sched_scan_results(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data)
{
	wl_pfn_net_info_v1_t *netinfo, *pnetinfo;
	wl_pfn_net_info_v2_t *netinfo_v2, *pnetinfo_v2;
	struct wiphy *wiphy	= bcmcfg_to_wiphy(cfg);
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
	int err = 0;
	struct cfg80211_scan_request *request = NULL;
	struct cfg80211_ssid ssid[MAX_PFN_LIST_COUNT];
	struct ieee80211_channel *channel = NULL;
	int channel_req = 0;
	int band = 0;
	wl_pfn_scanresults_v1_t *pfn_result_v1 = (wl_pfn_scanresults_v1_t *)data;
	wl_pfn_scanresults_v2_t *pfn_result_v2 = (wl_pfn_scanresults_v2_t *)data;
	int n_pfn_results = 0;
	log_conn_event_t *event_data = NULL;
	tlv_log *tlv_data = NULL;
	u32 alloc_len = 0, tlv_len = 0;
	u32 payload_len;
	u8 tmp_buf[DOT11_MAX_SSID_LEN + 1];

	WL_DBG(("Enter\n"));

	/* These static asserts guarantee v1/v2 net_info and subnet_info are compatible
	 * in size and SSID offset, allowing v1 to be used below except for the results
	 * fields themselves (status, count, offset to netinfo).
	 */
	STATIC_ASSERT(sizeof(wl_pfn_net_info_v1_t) == sizeof(wl_pfn_net_info_v2_t));
	STATIC_ASSERT(sizeof(wl_pfn_lnet_info_v1_t) == sizeof(wl_pfn_lnet_info_v2_t));
	STATIC_ASSERT(sizeof(wl_pfn_subnet_info_v1_t) == sizeof(wl_pfn_subnet_info_v2_t));
	STATIC_ASSERT(OFFSETOF(wl_pfn_subnet_info_v1_t, SSID) ==
	              OFFSETOF(wl_pfn_subnet_info_v2_t, u.SSID));

	/* Extract the version-specific items */
	if (pfn_result_v1->version == PFN_SCANRESULT_VERSION_V1) {
		n_pfn_results = pfn_result_v1->count;
		pnetinfo = pfn_result_v1->netinfo;
		WL_INFORM_MEM(("PFN NET FOUND event. count:%d \n", n_pfn_results));

		if (n_pfn_results > 0) {
			int i;

			if (n_pfn_results > MAX_PFN_LIST_COUNT)
				n_pfn_results = MAX_PFN_LIST_COUNT;

			bzero(&ssid, sizeof(ssid));

			request = (struct cfg80211_scan_request *)MALLOCZ(cfg->osh,
				sizeof(*request) + sizeof(*request->channels) * n_pfn_results);
			channel = (struct ieee80211_channel *)MALLOCZ(cfg->osh,
				(sizeof(struct ieee80211_channel) * n_pfn_results));
			if (!request || !channel) {
				WL_ERR(("No memory"));
				err = -ENOMEM;
				goto out_err;
			}

			request->wiphy = wiphy;

			if (DBG_RING_ACTIVE(dhdp, DHD_EVENT_RING_ID)) {
				alloc_len = sizeof(log_conn_event_t) + DOT11_MAX_SSID_LEN +
					sizeof(uint16) + sizeof(int16);
				event_data = (log_conn_event_t *)MALLOC(cfg->osh, alloc_len);
				if (!event_data) {
					WL_ERR(("%s: failed to allocate the log_conn_event_t with "
						"length(%d)\n", __func__, alloc_len));
					goto out_err;
				}
				tlv_len = 3 * sizeof(tlv_log);
				event_data->tlvs = (tlv_log *)MALLOC(cfg->osh, tlv_len);
				if (!event_data->tlvs) {
					WL_ERR(("%s: failed to allocate the tlv_log with "
						"length(%d)\n", __func__, tlv_len));
					goto out_err;
				}
			}

			for (i = 0; i < n_pfn_results; i++) {
				netinfo = &pnetinfo[i];
				if (!netinfo) {
					WL_ERR(("Invalid netinfo ptr. index:%d", i));
					err = -EINVAL;
					goto out_err;
				}
				if (netinfo->pfnsubnet.SSID_len > DOT11_MAX_SSID_LEN) {
					WL_ERR(("Wrong SSID length:%d\n",
						netinfo->pfnsubnet.SSID_len));
					err = -EINVAL;
					goto out_err;
				}
				/* In previous step max SSID_len limited to DOT11_MAX_SSID_LEN
				* and tmp_buf size is DOT11_MAX_SSID_LEN+1
				*/
				(void)memcpy_s(tmp_buf, DOT11_MAX_SSID_LEN,
					netinfo->pfnsubnet.SSID, netinfo->pfnsubnet.SSID_len);
				tmp_buf[netinfo->pfnsubnet.SSID_len] = '\0';
				WL_PNO((">>> SSID:%s Channel:%d \n",
					tmp_buf, netinfo->pfnsubnet.channel));
				/* PFN result doesn't have all the info which are required by
				 * the supplicant. (For e.g IEs) Do a target Escan so that
				 * sched scan results are reported via wl_inform_single_bss in
				 * the required format. Escan does require the scan request in
				 * the form of cfg80211_scan_request. For timebeing, create
				 * cfg80211_scan_request one out of the received PNO event.
				 */

				ssid[i].ssid_len = netinfo->pfnsubnet.SSID_len;
				/* Returning void as ssid[i].ssid_len is limited to max of
				* DOT11_MAX_SSID_LEN
				*/
				(void)memcpy_s(ssid[i].ssid, IEEE80211_MAX_SSID_LEN,
					netinfo->pfnsubnet.SSID, ssid[i].ssid_len);
				request->n_ssids++;

				channel_req = netinfo->pfnsubnet.channel;
				band = (channel_req <= CH_MAX_2G_CHANNEL) ? NL80211_BAND_2GHZ
					: NL80211_BAND_5GHZ;
				channel[i].center_freq =
					ieee80211_channel_to_frequency(channel_req, band);
				channel[i].band = band;
				channel[i].flags |= IEEE80211_CHAN_NO_HT40;
				request->channels[i] = &channel[i];
				request->n_channels++;

				if (DBG_RING_ACTIVE(dhdp, DHD_EVENT_RING_ID)) {
					payload_len = sizeof(log_conn_event_t);
					event_data->event = WIFI_EVENT_DRIVER_PNO_NETWORK_FOUND;
					tlv_data = event_data->tlvs;

					/* ssid */
					tlv_data->tag = WIFI_TAG_SSID;
					tlv_data->len = ssid[i].ssid_len;
					(void)memcpy_s(tlv_data->value, DOT11_MAX_SSID_LEN,
						ssid[i].ssid, ssid[i].ssid_len);
					payload_len += TLV_LOG_SIZE(tlv_data);
					tlv_data = TLV_LOG_NEXT(tlv_data);

					/* channel */
					tlv_data->tag = WIFI_TAG_CHANNEL;
					tlv_data->len = sizeof(uint16);
					(void)memcpy_s(tlv_data->value, sizeof(uint16),
						&channel_req, sizeof(uint16));
					payload_len += TLV_LOG_SIZE(tlv_data);
					tlv_data = TLV_LOG_NEXT(tlv_data);

					/* rssi */
					tlv_data->tag = WIFI_TAG_RSSI;
					tlv_data->len = sizeof(int16);
					(void)memcpy_s(tlv_data->value, sizeof(int16),
						&netinfo->RSSI, sizeof(int16));
					payload_len += TLV_LOG_SIZE(tlv_data);
					tlv_data = TLV_LOG_NEXT(tlv_data);

					dhd_os_push_push_ring_data(dhdp, DHD_EVENT_RING_ID,
						&event_data->event, payload_len);
				}
			}

			/* assign parsed ssid array */
			if (request->n_ssids)
				request->ssids = &ssid[0];

			if (wl_get_drv_status_all(cfg, SCANNING)) {
				/* Abort any on-going scan */
				wl_cfg80211_cancel_scan(cfg);
			}

			if (wl_get_p2p_status(cfg, DISCOVERY_ON)) {
				WL_PNO((">>> P2P discovery was ON. Disabling it\n"));
				err = wl_cfgp2p_discover_enable_search(cfg, false);
				if (unlikely(err)) {
					wl_clr_drv_status(cfg, SCANNING, ndev);
					goto out_err;
				}
				p2p_scan(cfg) = false;
			}
			wl_set_drv_status(cfg, SCANNING, ndev);
#if FULL_ESCAN_ON_PFN_NET_FOUND
			WL_PNO((">>> Doing Full ESCAN on PNO event\n"));
			err = wl_do_escan(cfg, wiphy, ndev, NULL);
#else
			WL_PNO((">>> Doing targeted ESCAN on PNO event\n"));
			err = wl_do_escan(cfg, wiphy, ndev, request);
#endif // endif
			if (err) {
				wl_clr_drv_status(cfg, SCANNING, ndev);
				goto out_err;
			}
			DBG_EVENT_LOG(dhdp, WIFI_EVENT_DRIVER_PNO_SCAN_REQUESTED);
			cfg->sched_scan_running = TRUE;
		}
		else {
			WL_ERR(("FALSE PNO Event. (pfn_count == 0) \n"));
		}

	} else if (pfn_result_v2->version == PFN_SCANRESULT_VERSION_V2) {
		n_pfn_results = pfn_result_v2->count;
		pnetinfo_v2 = (wl_pfn_net_info_v2_t *)pfn_result_v2->netinfo;

		if (e->event_type == WLC_E_PFN_NET_LOST) {
			WL_PNO(("Do Nothing %d\n", e->event_type));
			return 0;
		}

		WL_INFORM_MEM(("PFN NET FOUND event. count:%d \n", n_pfn_results));

		if (n_pfn_results > 0) {
			int i;

			if (n_pfn_results > MAX_PFN_LIST_COUNT)
				n_pfn_results = MAX_PFN_LIST_COUNT;

			bzero(&ssid, sizeof(ssid));

			request = (struct cfg80211_scan_request *)MALLOCZ(cfg->osh,
				sizeof(*request) + sizeof(*request->channels) * n_pfn_results);
			channel = (struct ieee80211_channel *)MALLOCZ(cfg->osh,
				(sizeof(struct ieee80211_channel) * n_pfn_results));
			if (!request || !channel) {
				WL_ERR(("No memory"));
				err = -ENOMEM;
				goto out_err;
			}

			request->wiphy = wiphy;

			if (DBG_RING_ACTIVE(dhdp, DHD_EVENT_RING_ID)) {
				alloc_len = sizeof(log_conn_event_t) + DOT11_MAX_SSID_LEN +
					sizeof(uint16) + sizeof(int16);
				event_data = (log_conn_event_t *)MALLOC(cfg->osh, alloc_len);
				if (!event_data) {
					WL_ERR(("%s: failed to allocate the log_conn_event_t with "
						"length(%d)\n", __func__, alloc_len));
					goto out_err;
				}
				tlv_len = 3 * sizeof(tlv_log);
				event_data->tlvs = (tlv_log *)MALLOC(cfg->osh, tlv_len);
				if (!event_data->tlvs) {
					WL_ERR(("%s: failed to allocate the tlv_log with "
						"length(%d)\n", __func__, tlv_len));
					goto out_err;
				}
			}

			for (i = 0; i < n_pfn_results; i++) {
				netinfo_v2 = &pnetinfo_v2[i];
				if (!netinfo_v2) {
					WL_ERR(("Invalid netinfo ptr. index:%d", i));
					err = -EINVAL;
					goto out_err;
				}
				WL_PNO((">>> SSID:%s Channel:%d \n",
					netinfo_v2->pfnsubnet.u.SSID,
					netinfo_v2->pfnsubnet.channel));
				/* PFN result doesn't have all the info which are required by the
				 * supplicant. (For e.g IEs) Do a target Escan so that sched scan
				 * results are reported via wl_inform_single_bss in the required
				 * format. Escan does require the scan request in the form of
				 * cfg80211_scan_request. For timebeing, create
				 * cfg80211_scan_request one out of the received PNO event.
				 */
				ssid[i].ssid_len = MIN(DOT11_MAX_SSID_LEN,
					netinfo_v2->pfnsubnet.SSID_len);
				/* max ssid_len as in previous step DOT11_MAX_SSID_LEN is same
				* as DOT11_MAX_SSID_LEN = 32
				*/
				(void)memcpy_s(ssid[i].ssid, IEEE80211_MAX_SSID_LEN,
					netinfo_v2->pfnsubnet.u.SSID, ssid[i].ssid_len);
				request->n_ssids++;

				channel_req = netinfo_v2->pfnsubnet.channel;
				band = (channel_req <= CH_MAX_2G_CHANNEL) ? NL80211_BAND_2GHZ
					: NL80211_BAND_5GHZ;
				channel[i].center_freq =
					ieee80211_channel_to_frequency(channel_req, band);
				channel[i].band = band;
				channel[i].flags |= IEEE80211_CHAN_NO_HT40;
				request->channels[i] = &channel[i];
				request->n_channels++;

				if (DBG_RING_ACTIVE(dhdp, DHD_EVENT_RING_ID)) {
					payload_len = sizeof(log_conn_event_t);
					event_data->event = WIFI_EVENT_DRIVER_PNO_NETWORK_FOUND;
					tlv_data = event_data->tlvs;

					/* ssid */
					tlv_data->tag = WIFI_TAG_SSID;
					tlv_data->len = netinfo_v2->pfnsubnet.SSID_len;
					(void)memcpy_s(tlv_data->value, DOT11_MAX_SSID_LEN,
						ssid[i].ssid, ssid[i].ssid_len);
					payload_len += TLV_LOG_SIZE(tlv_data);
					tlv_data = TLV_LOG_NEXT(tlv_data);

					/* channel */
					tlv_data->tag = WIFI_TAG_CHANNEL;
					tlv_data->len = sizeof(uint16);
					(void)memcpy_s(tlv_data->value, sizeof(uint16),
						&channel_req, sizeof(uint16));
					payload_len += TLV_LOG_SIZE(tlv_data);
					tlv_data = TLV_LOG_NEXT(tlv_data);

					/* rssi */
					tlv_data->tag = WIFI_TAG_RSSI;
					tlv_data->len = sizeof(int16);
					(void)memcpy_s(tlv_data->value, sizeof(uint16),
						&netinfo_v2->RSSI, sizeof(int16));
					payload_len += TLV_LOG_SIZE(tlv_data);
					tlv_data = TLV_LOG_NEXT(tlv_data);

					dhd_os_push_push_ring_data(dhdp, DHD_EVENT_RING_ID,
						&event_data->event, payload_len);
				}
			}

			/* assign parsed ssid array */
			if (request->n_ssids)
				request->ssids = &ssid[0];

			if (wl_get_drv_status_all(cfg, SCANNING)) {
				/* Abort any on-going scan */
				wl_cfg80211_cancel_scan(cfg);
			}

			if (wl_get_p2p_status(cfg, DISCOVERY_ON)) {
				WL_PNO((">>> P2P discovery was ON. Disabling it\n"));
				err = wl_cfgp2p_discover_enable_search(cfg, false);
				if (unlikely(err)) {
					wl_clr_drv_status(cfg, SCANNING, ndev);
					goto out_err;
				}
				p2p_scan(cfg) = false;
			}

			wl_set_drv_status(cfg, SCANNING, ndev);
#if FULL_ESCAN_ON_PFN_NET_FOUND
			WL_PNO((">>> Doing Full ESCAN on PNO event\n"));
			err = wl_do_escan(cfg, wiphy, ndev, NULL);
#else
			WL_PNO((">>> Doing targeted ESCAN on PNO event\n"));
			err = wl_do_escan(cfg, wiphy, ndev, request);
#endif // endif
			if (err) {
				wl_clr_drv_status(cfg, SCANNING, ndev);
				goto out_err;
			}
			DBG_EVENT_LOG(dhdp, WIFI_EVENT_DRIVER_PNO_SCAN_REQUESTED);
			cfg->sched_scan_running = TRUE;
		}
		else {
			WL_ERR(("FALSE PNO Event. (pfn_count == 0) \n"));
		}
	} else {
		WL_ERR(("Unsupported version %d, expected %d or %d\n", pfn_result_v1->version,
			PFN_SCANRESULT_VERSION_V1, PFN_SCANRESULT_VERSION_V2));
		return 0;
	}
out_err:
	if (request) {
		MFREE(cfg->osh, request,
			sizeof(*request) + sizeof(*request->channels) * n_pfn_results);
	}
	if (channel) {
		MFREE(cfg->osh, channel,
			(sizeof(struct ieee80211_channel) * n_pfn_results));
	}

	if (event_data) {
		if (event_data->tlvs) {
			MFREE(cfg->osh, event_data->tlvs, tlv_len);
		}
		MFREE(cfg->osh, event_data, alloc_len);
	}
	return err;
}
#endif /* WL_SCHED_SCAN */

#ifdef PNO_SUPPORT
s32
wl_notify_pfn_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	struct net_device *ndev = NULL;
#ifdef GSCAN_SUPPORT
	void *ptr;
	int send_evt_bytes = 0;
	u32 event = be32_to_cpu(e->event_type);
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
#endif /* GSCAN_SUPPORT */

	WL_INFORM_MEM((">>> PNO Event\n"));

	if (!data) {
		WL_ERR(("Data received is NULL!\n"));
		return 0;
	}

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
#ifdef GSCAN_SUPPORT
	ptr = dhd_dev_process_epno_result(ndev, data, event, &send_evt_bytes);
	if (ptr) {
		wl_cfgvendor_send_async_event(wiphy, ndev,
			GOOGLE_SCAN_EPNO_EVENT, ptr, send_evt_bytes);
		MFREE(cfg->osh, ptr, send_evt_bytes);
	}
	if (!dhd_dev_is_legacy_pno_enabled(ndev))
		return 0;
#endif /* GSCAN_SUPPORT */

#ifndef WL_SCHED_SCAN
	mutex_lock(&cfg->usr_sync);
	/* TODO: Use cfg80211_sched_scan_results(wiphy); */
	CFG80211_DISCONNECTED(ndev, 0, NULL, 0, false, GFP_KERNEL);
	mutex_unlock(&cfg->usr_sync);
#else
	/* If cfg80211 scheduled scan is supported, report the pno results via sched
	 * scan results
	 */
	wl_notify_sched_scan_results(cfg, ndev, e, data);
#endif /* WL_SCHED_SCAN */
	return 0;
}
#endif /* PNO_SUPPORT */

#ifdef GSCAN_SUPPORT
s32
wl_notify_gscan_event(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	s32 err = 0;
	u32 event = be32_to_cpu(e->event_type);
	void *ptr = NULL;
	int send_evt_bytes = 0;
	int event_type;
	struct net_device *ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	u32 len = ntoh32(e->datalen);
	u32 buf_len = 0;

	switch (event) {
		case WLC_E_PFN_BEST_BATCHING:
			err = dhd_dev_retrieve_batch_scan(ndev);
			if (err < 0) {
				WL_ERR(("Batch retrieval already in progress %d\n", err));
			} else {
				event_type = WIFI_SCAN_THRESHOLD_NUM_SCANS;
				if (data && len) {
					event_type = *((int *)data);
				}
				wl_cfgvendor_send_async_event(wiphy, ndev,
				    GOOGLE_GSCAN_BATCH_SCAN_EVENT,
				     &event_type, sizeof(int));
			}
			break;
		case WLC_E_PFN_SCAN_COMPLETE:
			event_type = WIFI_SCAN_COMPLETE;
			wl_cfgvendor_send_async_event(wiphy, ndev,
				GOOGLE_SCAN_COMPLETE_EVENT,
				&event_type, sizeof(int));
			break;
		case WLC_E_PFN_BSSID_NET_FOUND:
			ptr = dhd_dev_hotlist_scan_event(ndev, data, &send_evt_bytes,
			      HOTLIST_FOUND, &buf_len);
			if (ptr) {
				wl_cfgvendor_send_hotlist_event(wiphy, ndev,
				 ptr, send_evt_bytes, GOOGLE_GSCAN_GEOFENCE_FOUND_EVENT);
				dhd_dev_gscan_hotlist_cache_cleanup(ndev, HOTLIST_FOUND);
			} else {
				err = -ENOMEM;
			}
			break;
		case WLC_E_PFN_BSSID_NET_LOST:
			/* WLC_E_PFN_BSSID_NET_LOST is conflict shared with WLC_E_PFN_SCAN_ALLGONE
			 * We currently do not use WLC_E_PFN_SCAN_ALLGONE, so if we get it, ignore
			 */
			if (len) {
				ptr = dhd_dev_hotlist_scan_event(ndev, data, &send_evt_bytes,
				                                 HOTLIST_LOST, &buf_len);
				if (ptr) {
					wl_cfgvendor_send_hotlist_event(wiphy, ndev,
					 ptr, send_evt_bytes, GOOGLE_GSCAN_GEOFENCE_LOST_EVENT);
					dhd_dev_gscan_hotlist_cache_cleanup(ndev, HOTLIST_LOST);
					MFREE(cfg->osh, ptr, buf_len);
				} else {
					err = -ENOMEM;
				}
			} else {
				err = -EINVAL;
			}
			break;
		case WLC_E_PFN_GSCAN_FULL_RESULT:
			ptr = dhd_dev_process_full_gscan_result(ndev, data, len, &send_evt_bytes);
			if (ptr) {
				wl_cfgvendor_send_async_event(wiphy, ndev,
				    GOOGLE_SCAN_FULL_RESULTS_EVENT, ptr, send_evt_bytes);
				MFREE(cfg->osh, ptr, send_evt_bytes);
			} else {
				err = -ENOMEM;
			}
			break;
		case WLC_E_PFN_SSID_EXT:
			ptr = dhd_dev_process_epno_result(ndev, data, event, &send_evt_bytes);
			if (ptr) {
				wl_cfgvendor_send_async_event(wiphy, ndev,
				    GOOGLE_SCAN_EPNO_EVENT, ptr, send_evt_bytes);
				MFREE(cfg->osh, ptr, send_evt_bytes);
			} else {
				err = -ENOMEM;
			}
			break;
		default:
			WL_ERR(("Unknown event %d\n", event));
			break;
	}
	return err;
}
#endif /* GSCAN_SUPPORT */

void wl_cfg80211_set_passive_scan(struct net_device *dev, char *command)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	if (strcmp(command, "SCAN-ACTIVE") == 0) {
		cfg->active_scan = 1;
	} else if (strcmp(command, "SCAN-PASSIVE") == 0) {
		cfg->active_scan = 0;
	} else
		WL_ERR(("Unknown command \n"));
	return;
}
