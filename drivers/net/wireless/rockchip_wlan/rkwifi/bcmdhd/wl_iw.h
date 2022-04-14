/*
 * Linux Wireless Extensions support
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
 * $Id: wl_iw.h 514727 2014-11-12 03:02:48Z $
 */

#ifndef _wl_iw_h_
#define _wl_iw_h_

#include <linux/wireless.h>

#include <typedefs.h>
#include <ethernet.h>
#include <wlioctl.h>
#include <dngl_stats.h>
#include <dhd.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
#define get_ds()    (KERNEL_DS)
#endif

#define WL_SCAN_PARAMS_SSID_MAX 	10
#define GET_SSID			"SSID="
#define GET_CHANNEL			"CH="
#define GET_NPROBE 			"NPROBE="
#define GET_ACTIVE_ASSOC_DWELL  	"ACTIVE="
#define GET_PASSIVE_ASSOC_DWELL  	"PASSIVE="
#define GET_HOME_DWELL  		"HOME="
#define GET_SCAN_TYPE			"TYPE="

#define BAND_GET_CMD				"GETBAND"
#define BAND_SET_CMD				"SETBAND"
#define DTIM_SKIP_GET_CMD			"DTIMSKIPGET"
#define DTIM_SKIP_SET_CMD			"DTIMSKIPSET"
#define SETSUSPEND_CMD				"SETSUSPENDOPT"
#define PNOSSIDCLR_SET_CMD			"PNOSSIDCLR"
/* Lin - Is the extra space needed? */
#define PNOSETUP_SET_CMD			"PNOSETUP " /* TLV command has extra end space */
#define PNOENABLE_SET_CMD			"PNOFORCE"
#define PNODEBUG_SET_CMD			"PNODEBUG"
#define TXPOWER_SET_CMD			"TXPOWER"

#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"

/* Structure to keep global parameters */
typedef struct wl_iw_extra_params {
	int 	target_channel; /* target channel */
} wl_iw_extra_params_t;

/* ============================================== */
/* Defines from wlc_pub.h */
#define	WL_IW_RSSI_MINVAL		-200	/* Low value, e.g. for forcing roam */
#define	WL_IW_RSSI_NO_SIGNAL	-91	/* NDIS RSSI link quality cutoffs */
#define	WL_IW_RSSI_VERY_LOW	-80	/* Very low quality cutoffs */
#define	WL_IW_RSSI_LOW		-70	/* Low quality cutoffs */
#define	WL_IW_RSSI_GOOD		-68	/* Good quality cutoffs */
#define	WL_IW_RSSI_VERY_GOOD	-58	/* Very good quality cutoffs */
#define	WL_IW_RSSI_EXCELLENT	-57	/* Excellent quality cutoffs */
#define	WL_IW_RSSI_INVALID	 0	/* invalid RSSI value */
#define MAX_WX_STRING 80
#define SSID_FMT_BUF_LEN	((4 * 32) + 1)
#define isprint(c) bcm_isprint(c)
#define WL_IW_SET_ACTIVE_SCAN	(SIOCIWFIRSTPRIV+1)
#define WL_IW_GET_RSSI			(SIOCIWFIRSTPRIV+3)
#define WL_IW_SET_PASSIVE_SCAN	(SIOCIWFIRSTPRIV+5)
#define WL_IW_GET_LINK_SPEED	(SIOCIWFIRSTPRIV+7)
#define WL_IW_GET_CURR_MACADDR	(SIOCIWFIRSTPRIV+9)
#define WL_IW_SET_STOP				(SIOCIWFIRSTPRIV+11)
#define WL_IW_SET_START			(SIOCIWFIRSTPRIV+13)

#define 		G_SCAN_RESULTS 8*1024
#define 		WE_ADD_EVENT_FIX	0x80
#define          G_WLAN_SET_ON	0
#define          G_WLAN_SET_OFF	1


typedef struct wl_iw {
	char nickname[IW_ESSID_MAX_SIZE];

	struct iw_statistics wstats;

	int spy_num;
	uint32 pwsec;			/* pairwise wsec setting */
	uint32 gwsec;			/* group wsec setting  */
	bool privacy_invoked; 		/* IW_AUTH_PRIVACY_INVOKED setting */
	struct ether_addr spy_addr[IW_MAX_SPY];
	struct iw_quality spy_qual[IW_MAX_SPY];
	void  *wlinfo;
} wl_iw_t;

struct wl_ctrl {
	timer_list_compat_t *timer;
	struct net_device *dev;
	long sysioc_pid;
	struct semaphore sysioc_sem;
	struct completion sysioc_exited;
};


#if WIRELESS_EXT > 12
#include <net/iw_handler.h>
extern const struct iw_handler_def wl_iw_handler_def;
#endif /* WIRELESS_EXT > 12 */

extern int wl_iw_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
extern int wl_iw_get_wireless_stats(struct net_device *dev, struct iw_statistics *wstats);
int wl_iw_send_priv_event(struct net_device *dev, char *flag);
#ifdef WL_ESCAN
int wl_iw_handle_scanresults_ies(char **event_p, char *end,
	struct iw_request_info *info, wl_bss_info_t *bi);
#endif
int wl_iw_attach(struct net_device *dev);
void wl_iw_detach(struct net_device *dev);
s32 wl_iw_autochannel(struct net_device *dev, char* command, int total_len);

/* message levels */
#define WL_ERROR_LEVEL	(1 << 0)
#define WL_TRACE_LEVEL	(1 << 1)
#define WL_INFO_LEVEL	(1 << 2)
#define WL_SCAN_LEVEL	(1 << 3)
#define WL_WSEC_LEVEL	(1 << 4)

#define CSCAN_COMMAND				"CSCAN "
#define CSCAN_TLV_PREFIX 			'S'
#define CSCAN_TLV_VERSION			1
#define CSCAN_TLV_SUBVERSION			0
#define CSCAN_TLV_TYPE_SSID_IE          'S'
#define CSCAN_TLV_TYPE_CHANNEL_IE   'C'
#define CSCAN_TLV_TYPE_NPROBE_IE     'N'
#define CSCAN_TLV_TYPE_ACTIVE_IE      'A'
#define CSCAN_TLV_TYPE_PASSIVE_IE    'P'
#define CSCAN_TLV_TYPE_HOME_IE         'H'
#define CSCAN_TLV_TYPE_STYPE_IE        'T'

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
#define IWE_STREAM_ADD_EVENT(info, stream, ends, iwe, extra) \
	iwe_stream_add_event(info, stream, ends, iwe, extra)
#define IWE_STREAM_ADD_VALUE(info, event, value, ends, iwe, event_len) \
	iwe_stream_add_value(info, event, value, ends, iwe, event_len)
#define IWE_STREAM_ADD_POINT(info, stream, ends, iwe, extra) \
	iwe_stream_add_point(info, stream, ends, iwe, extra)
#else
#define IWE_STREAM_ADD_EVENT(info, stream, ends, iwe, extra) \
	iwe_stream_add_event(stream, ends, iwe, extra)
#define IWE_STREAM_ADD_VALUE(info, event, value, ends, iwe, event_len) \
	iwe_stream_add_value(event, value, ends, iwe, event_len)
#define IWE_STREAM_ADD_POINT(info, stream, ends, iwe, extra) \
	iwe_stream_add_point(stream, ends, iwe, extra)
#endif

#endif /* _wl_iw_h_ */
