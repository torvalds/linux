
#ifndef _wl_android_ext_
#define _wl_android_ext_
typedef struct bcol_gtk_para {
	int enable;
	int ptk_len;
	char ptk[64];
	char replay[8];
} bcol_gtk_para_t;
#define ACS_FW_BIT		(1<<0)
#define ACS_DRV_BIT		(1<<1)
int wl_ext_autochannel(struct net_device *dev, uint acs, uint32 band);
int wl_android_ext_priv_cmd(struct net_device *net, char *command, int total_len,
	int *bytes_written);
void wl_ext_get_sec(struct net_device *dev, int ifmode, char *sec, int total_len, bool dump);
bool wl_ext_check_scan(struct net_device *dev, dhd_pub_t *dhdp);
int wl_ext_set_scan_time(struct net_device *dev, int scan_time,
	uint32 scan_get, uint32 scan_set);
void wl_ext_wait_event_complete(struct dhd_pub *dhd, int ifidx);
int wl_ext_add_del_ie(struct net_device *dev, uint pktflag, char *ie_data, const char* add_del_cmd);
#ifdef WL_ESCAN
int wl_ext_drv_scan(struct net_device *dev, uint band, bool fast_scan);
#endif
#ifdef WL_EXT_GENL
int wl_ext_genl_init(struct net_device *net);
void wl_ext_genl_deinit(struct net_device *net);
#endif
#ifdef WL_EXT_IAPSTA
#ifndef strtoul
#define strtoul(nptr, endptr, base) bcm_strtoul((nptr), (endptr), (base))
#endif
int wl_ext_ioctl(struct net_device *dev, u32 cmd, void *arg, u32 len, u32 set);
int wl_ext_iovar_getint(struct net_device *dev, s8 *iovar, s32 *val);
int wl_ext_iovar_setint(struct net_device *dev, s8 *iovar, s32 val);
int wl_ext_iovar_getbuf(struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, struct mutex* buf_sync);
int wl_ext_iovar_setbuf(struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, struct mutex* buf_sync);
int wl_ext_iovar_setbuf_bsscfg(struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, s32 bsscfg_idx,
	struct mutex* buf_sync);
chanspec_t wl_ext_chspec_driver_to_host(int ioctl_ver, chanspec_t chanspec);
chanspec_t wl_ext_chspec_host_to_driver(int ioctl_ver, chanspec_t chanspec);
bool wl_ext_dfs_chan(uint16 chan);
uint16 wl_ext_get_default_chan(struct net_device *dev,
	uint16 *chan_2g, uint16 *chan_5g, bool nodfs);
int wl_ext_set_chanspec(struct net_device *dev, int ioctl_ver,
	uint16 channel, chanspec_t *ret_chspec);
int wl_ext_get_ioctl_ver(struct net_device *dev, int *ioctl_ver);
#endif
#if defined(WL_CFG80211) || defined(WL_ESCAN)
void wl_ext_user_sync(struct dhd_pub *dhd, int ifidx, bool lock);
#endif
#if defined(WL_CFG80211)
bool wl_legacy_chip_check(struct net_device *net);
bool wl_new_chip_check(struct net_device *net);
bool wl_extsae_chip(struct dhd_pub *dhd);
#endif
#if defined(WL_EXT_IAPSTA) || defined(WL_CFG80211)
void wl_ext_bss_iovar_war(struct net_device *dev, s32 *val);
#endif /* WL_EXT_IAPSTA ||WL_CFG80211 */

typedef struct wl_conn_info {
	uint8 bssidx;
	wlc_ssid_t ssid;
	struct ether_addr bssid;
	uint16 channel;
} wl_conn_info_t;
#if defined(WL_EXT_IAPSTA) || defined(USE_IW)
s32 wl_ext_connect(struct net_device *dev, wl_conn_info_t *conn_info);
#endif /* WL_EXT_IAPSTA || USE_IW */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0))
#define strnicmp(str1, str2, len) strncasecmp((str1), (str2), (len))
#endif

/* terence:
 * BSSCACHE: Cache bss list
 * RSSAVG: Average RSSI of BSS list
 * RSSIOFFSET: RSSI offset
 * SORT_BSS_BY_RSSI: Sort BSS by RSSI
 */
//#define BSSCACHE
//#define RSSIAVG
//#define RSSIOFFSET
//#define RSSIOFFSET_NEW

#define RSSI_MAXVAL -2
#define RSSI_MINVAL -200

#if defined(ESCAN_RESULT_PATCH)
#define REPEATED_SCAN_RESULT_CNT	2
#else
#define REPEATED_SCAN_RESULT_CNT	1
#endif

#if defined(RSSIAVG) || defined(RSSIOFFSET)
extern int g_wifi_on;
#endif

#if defined(RSSIAVG)
#define RSSIAVG_LEN (4*REPEATED_SCAN_RESULT_CNT)
#define RSSICACHE_TIMEOUT 15

typedef struct wl_rssi_cache {
	struct wl_rssi_cache *next;
	int dirty;
	struct osl_timespec tv;
	struct ether_addr BSSID;
	int16 RSSI[RSSIAVG_LEN];
} wl_rssi_cache_t;

typedef struct wl_rssi_cache_ctrl {
	wl_rssi_cache_t *m_cache_head;
} wl_rssi_cache_ctrl_t;

void wl_free_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl);
void wl_delete_dirty_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl);
void wl_delete_disconnected_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl, u8 *bssid);
void wl_reset_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl);
void wl_update_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl, wl_scan_results_t *ss_list);
int wl_update_connected_rssi_cache(struct net_device *net, wl_rssi_cache_ctrl_t *rssi_cache_ctrl, int *rssi_avg);
int16 wl_get_avg_rssi(wl_rssi_cache_ctrl_t *rssi_cache_ctrl, void *addr);
#endif

#if defined(RSSIOFFSET)
#define RSSI_OFFSET	5
#if defined(RSSIOFFSET_NEW)
#define RSSI_OFFSET_MAXVAL -80
#define RSSI_OFFSET_MINVAL -94
#define RSSI_OFFSET_INTVAL ((RSSI_OFFSET_MAXVAL-RSSI_OFFSET_MINVAL)/RSSI_OFFSET)
#endif
#define BCM4330_CHIP_ID		0x4330
#define BCM4330B2_CHIP_REV      4
int wl_update_rssi_offset(struct net_device *net, int rssi);
#endif

#if defined(BSSCACHE)
#define BSSCACHE_TIMEOUT	30
#define BSSCACHE_MAXCNT		20
#define BSSCACHE_DIRTY		4
#define SORT_BSS_CHANNEL
//#define SORT_BSS_RSSI

typedef struct wl_bss_cache {
	struct wl_bss_cache *next;
	int dirty;
	struct osl_timespec tv;
	wl_scan_results_t results;
} wl_bss_cache_t;

typedef struct wl_bss_cache_ctrl {
	wl_bss_cache_t *m_cache_head;
} wl_bss_cache_ctrl_t;

void wl_free_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl);
void wl_delete_dirty_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl);
void wl_delete_disconnected_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl, u8 *bssid);
int wl_bss_cache_size(wl_bss_cache_ctrl_t *bss_cache_ctrl);
void wl_reset_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl);
void wl_update_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl,
#if defined(RSSIAVG)
	wl_rssi_cache_ctrl_t *rssi_cache_ctrl,
#endif
	wl_scan_results_t *ss_list);
void wl_release_bss_cache_ctrl(wl_bss_cache_ctrl_t *bss_cache_ctrl);
#endif
int wl_ext_get_best_channel(struct net_device *net,
#if defined(BSSCACHE)
	wl_bss_cache_ctrl_t *bss_cache_ctrl,
#else
	wl_scan_results_t *bss_list,
#endif
	int ioctl_ver, int *best_2g_ch, int *best_5g_ch
);
#endif

