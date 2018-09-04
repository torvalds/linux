/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _wl_escan_
#define _wl_escan_

#include <linux/wireless.h>
#include <wl_iw.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <linux/time.h>


#ifdef DHD_MAX_IFS
#define WL_MAX_IFS DHD_MAX_IFS
#else
#define WL_MAX_IFS 16
#endif

#define ESCAN_BUF_SIZE (64 * 1024)

#define WL_ESCAN_TIMER_INTERVAL_MS	10000 /* Scan timeout */

/* event queue for cfg80211 main event */
struct escan_event_q {
	struct list_head eq_list;
	u32 etype;
	wl_event_msg_t emsg;
	s8 edata[1];
};

/* donlge escan state */
enum escan_state {
	ESCAN_STATE_IDLE,
	ESCAN_STATE_SCANING
};

struct wl_escan_info;

typedef s32(*ESCAN_EVENT_HANDLER) (struct wl_escan_info *escan,
                            const wl_event_msg_t *e, void *data);

typedef struct wl_escan_info {
	struct net_device *dev;
	dhd_pub_t *pub;
	struct timer_list scan_timeout;   /* Timer for catch scan event timeout */
	int    escan_state;
	int ioctl_ver;

	char ioctlbuf[WLC_IOCTL_SMLEN];
	u8 escan_buf[ESCAN_BUF_SIZE];
	struct wl_scan_results *bss_list;
	struct wl_scan_results *scan_results;
	struct ether_addr disconnected_bssid;
	u8 *escan_ioctl_buf;
	spinlock_t eq_lock;	/* for event queue synchronization */
	struct list_head eq_list;	/* used for event queue */
	tsk_ctl_t event_tsk;  		/* task of main event handler thread */
	ESCAN_EVENT_HANDLER evt_handler[WLC_E_LAST];
	struct mutex usr_sync;	/* maily for up/down synchronization */
	int autochannel;
	int best_2g_ch;
	int best_5g_ch;
#if defined(RSSIAVG)
	wl_rssi_cache_ctrl_t g_rssi_cache_ctrl;
	wl_rssi_cache_ctrl_t g_connected_rssi_cache_ctrl;
#endif
#if defined(BSSCACHE)
	wl_bss_cache_ctrl_t g_bss_cache_ctrl;
#endif
} wl_escan_info_t;

void wl_escan_event(struct net_device *dev, const wl_event_msg_t * e, void *data);

int wl_escan_set_scan(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
);
int wl_escan_get_scan(struct net_device *dev,	struct iw_request_info *info,
	struct iw_point *dwrq, char *extra);
s32 wl_escan_autochannel(struct net_device *dev, char* command, int total_len);
int wl_escan_attach(struct net_device *dev, dhd_pub_t *dhdp);
void wl_escan_detach(dhd_pub_t *dhdp);

#endif /* _wl_escan_ */

