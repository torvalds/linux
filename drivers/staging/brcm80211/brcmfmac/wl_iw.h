/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _wl_iw_h_
#define _wl_iw_h_

#include <linux/wireless.h>

#include <typedefs.h>
#include <proto/ethernet.h>
#include <wlioctl.h>

#define WL_SCAN_PARAMS_SSID_MAX	10
#define GET_SSID	"SSID="
#define GET_CHANNEL	"CH="
#define GET_NPROBE 			"NPROBE="
#define GET_ACTIVE_ASSOC_DWELL  	"ACTIVE="
#define GET_PASSIVE_ASSOC_DWELL	"PASSIVE="
#define GET_HOME_DWELL		"HOME="
#define GET_SCAN_TYPE			"TYPE="

#define BAND_GET_CMD				"BANDGET"
#define BAND_SET_CMD				"BANDSET"
#define DTIM_SKIP_GET_CMD			"DTIMSKIPGET"
#define DTIM_SKIP_SET_CMD			"DTIMSKIPSET"
#define SETSUSPEND_CMD				"SETSUSPENDOPT"
#define PNOSSIDCLR_SET_CMD			"PNOSSIDCLR"
#define PNOSETUP_SET_CMD			"PNOSETUP"
#define PNOENABLE_SET_CMD			"PNOFORCE"
#define PNODEBUG_SET_CMD			"PNODEBUG"

#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"

typedef struct wl_iw_extra_params {
	int target_channel;
} wl_iw_extra_params_t;

#define	WL_IW_RSSI_MINVAL		-200
#define	WL_IW_RSSI_NO_SIGNAL	-91
#define	WL_IW_RSSI_VERY_LOW	-80
#define	WL_IW_RSSI_LOW		-70
#define	WL_IW_RSSI_GOOD		-68
#define	WL_IW_RSSI_VERY_GOOD	-58
#define	WL_IW_RSSI_EXCELLENT	-57
#define	WL_IW_RSSI_INVALID	 0
#define MAX_WX_STRING 80
#define isprint(c) bcm_isprint(c)
#define WL_IW_SET_ACTIVE_SCAN	(SIOCIWFIRSTPRIV+1)
#define WL_IW_GET_RSSI			(SIOCIWFIRSTPRIV+3)
#define WL_IW_SET_PASSIVE_SCAN	(SIOCIWFIRSTPRIV+5)
#define WL_IW_GET_LINK_SPEED	(SIOCIWFIRSTPRIV+7)
#define WL_IW_GET_CURR_MACADDR	(SIOCIWFIRSTPRIV+9)
#define WL_IW_SET_STOP				(SIOCIWFIRSTPRIV+11)
#define WL_IW_SET_START			(SIOCIWFIRSTPRIV+13)

#define WL_SET_AP_CFG           (SIOCIWFIRSTPRIV+15)
#define WL_AP_STA_LIST          (SIOCIWFIRSTPRIV+17)
#define WL_AP_MAC_FLTR	        (SIOCIWFIRSTPRIV+19)
#define WL_AP_BSS_START         (SIOCIWFIRSTPRIV+21)
#define AP_LPB_CMD              (SIOCIWFIRSTPRIV+23)
#define WL_AP_STOP              (SIOCIWFIRSTPRIV+25)
#define WL_FW_RELOAD            (SIOCIWFIRSTPRIV+27)
#define WL_COMBO_SCAN            (SIOCIWFIRSTPRIV+29)
#define WL_AP_SPARE3            (SIOCIWFIRSTPRIV+31)
#define 		G_SCAN_RESULTS 8*1024
#define	WE_ADD_EVENT_FIX	0x80
#define          G_WLAN_SET_ON	0
#define          G_WLAN_SET_OFF	1

#define CHECK_EXTRA_FOR_NULL(extra) \
if (!extra) { \
	WL_ERROR(("%s: error : extra is null pointer\n", __func__)); \
	return -EINVAL; \
}

typedef struct wl_iw {
	char nickname[IW_ESSID_MAX_SIZE];

	struct iw_statistics wstats;

	int spy_num;
	uint32 pwsec;
	uint32 gwsec;
	bool privacy_invoked;

	struct ether_addr spy_addr[IW_MAX_SPY];
	struct iw_quality spy_qual[IW_MAX_SPY];
	void *wlinfo;
	dhd_pub_t *pub;
} wl_iw_t;

#if WIRELESS_EXT > 12
#include <net/iw_handler.h>
extern const struct iw_handler_def wl_iw_handler_def;
#endif

extern int wl_iw_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
extern void wl_iw_event(struct net_device *dev, wl_event_msg_t *e, void *data);
extern int wl_iw_get_wireless_stats(struct net_device *dev,
				    struct iw_statistics *wstats);
int wl_iw_attach(struct net_device *dev, void *dhdp);
void wl_iw_detach(void);
extern int net_os_set_suspend_disable(struct net_device *dev, int val);
extern int net_os_set_suspend(struct net_device *dev, int val);
extern int net_os_set_dtim_skip(struct net_device *dev, int val);
extern int net_os_set_packet_filter(struct net_device *dev, int val);

#define IWE_STREAM_ADD_EVENT(info, stream, ends, iwe, extra) \
	iwe_stream_add_event(info, stream, ends, iwe, extra)
#define IWE_STREAM_ADD_VALUE(info, event, value, ends, iwe, event_len) \
	iwe_stream_add_value(info, event, value, ends, iwe, event_len)
#define IWE_STREAM_ADD_POINT(info, stream, ends, iwe, extra) \
	iwe_stream_add_point(info, stream, ends, iwe, extra)

extern int dhd_pno_enable(dhd_pub_t *dhd, int pfn_enabled);
extern int dhd_pno_clean(dhd_pub_t *dhd);
extern int dhd_pno_set(dhd_pub_t *dhd, wlc_ssid_t *ssids_local, int nssid,
		       unsigned char scan_fr);
extern int dhd_pno_get_status(dhd_pub_t *dhd);
extern int dhd_dev_pno_reset(struct net_device *dev);
extern int dhd_dev_pno_set(struct net_device *dev, wlc_ssid_t *ssids_local,
			   int nssid, unsigned char scan_fr);
extern int dhd_dev_pno_enable(struct net_device *dev, int pfn_enabled);
extern int dhd_dev_get_pno_status(struct net_device *dev);

#define PNO_TLV_PREFIX			'S'
#define PNO_TLV_VERSION			1
#define PNO_TLV_SUBVERSION		0
#define PNO_TLV_RESERVED		0
#define PNO_TLV_TYPE_SSID_IE		'S'
#define PNO_TLV_TYPE_TIME		'T'
#define  PNO_EVENT_UP			"PNO_EVENT"

typedef struct cmd_tlv {
	char prefix;
	char version;
	char subver;
	char reserved;
} cmd_tlv_t;
#endif				/* _wl_iw_h_ */
