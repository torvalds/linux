/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _wl_escan_
#define _wl_escan_
#include <linuxver.h>
#include <wl_iw.h>

#define ESCAN_BUF_SIZE (64 * 1024)

#define WL_ESCAN_TIMER_INTERVAL_MS	10000 /* Scan timeout */

/* donlge escan state */
enum escan_state {
	ESCAN_STATE_DOWN,
	ESCAN_STATE_IDLE,
	ESCAN_STATE_SCANING
};

typedef struct wl_escan_info {
	struct net_device *dev;
	dhd_pub_t *pub;
	timer_list_compat_t scan_timeout;   /* Timer for catch scan event timeout */
	int escan_state;
	int ioctl_ver;
	u8 escan_buf[ESCAN_BUF_SIZE];
	struct wl_scan_results *bss_list;
	struct ether_addr disconnected_bssid;
	u8 *escan_ioctl_buf;
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

#if defined(WLMESH)
/* donlge escan state */
enum mesh_info_id {
	MESH_INFO_MASTER_BSSID = 1,
	MESH_INFO_MASTER_CHANNEL,
	MESH_INFO_HOP_CNT,
	MESH_INFO_PEER_BSSID
};

typedef struct wl_mesh_params {
	struct ether_addr master_bssid;
	uint16 master_channel;
	uint hop_cnt;
	struct ether_addr peer_bssid;
	uint16 channel;
	uint16 scan_channel;
} wl_mesh_params_t;
int wl_escan_mesh_info(struct net_device *dev,
	struct wl_escan_info *escan, struct ether_addr *peer_bssid,
	struct wl_mesh_params *mesh_info);
int wl_escan_mesh_peer(struct net_device *dev,
	struct wl_escan_info *escan, wlc_ssid_t *cur_ssid, uint16 cur_chan, bool sae,
	struct wl_mesh_params *mesh_info);
#endif /* WLMESH */

int wl_escan_set_scan(struct net_device *dev, dhd_pub_t *dhdp,
	wlc_ssid_t *ssid, uint16 channel, bool bcast);
int wl_escan_get_scan(struct net_device *dev, dhd_pub_t *dhdp,
	struct iw_request_info *info, struct iw_point *dwrq, char *extra);
int wl_escan_attach(struct net_device *dev, dhd_pub_t *dhdp);
void wl_escan_detach(struct net_device *dev, dhd_pub_t *dhdp);
int wl_escan_event_attach(struct net_device *dev, dhd_pub_t *dhdp);
int wl_escan_event_dettach(struct net_device *dev, dhd_pub_t *dhdp);
int wl_escan_up(struct net_device *dev, dhd_pub_t *dhdp);
void wl_escan_down(struct net_device *dev, dhd_pub_t *dhdp);

#endif /* _wl_escan_ */

