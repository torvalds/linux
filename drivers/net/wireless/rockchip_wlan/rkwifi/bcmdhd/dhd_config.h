/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _dhd_config_
#define _dhd_config_

#include <bcmdevs.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <wlioctl.h>
#include <802.11.h>

#define FW_TYPE_STA     0
#define FW_TYPE_APSTA   1
#define FW_TYPE_P2P     2
#define FW_TYPE_MESH    3
#define FW_TYPE_ES      4
#define FW_TYPE_MFG     5
#define FW_TYPE_G       0
#define FW_TYPE_AG      1

#define FW_PATH_AUTO_SELECT 1
//#define CONFIG_PATH_AUTO_SELECT
extern char firmware_path[MOD_PARAM_PATHLEN];
#if defined(BCMSDIO) || defined(BCMPCIE)
extern uint dhd_rxbound;
extern uint dhd_txbound;
#endif
#ifdef BCMSDIO
#define TXGLOM_RECV_OFFSET 8
extern uint dhd_doflow;
extern uint dhd_slpauto;
#endif

typedef struct wl_mac_range {
	uint32 oui;
	uint32 nic_start;
	uint32 nic_end;
} wl_mac_range_t;

typedef struct wl_mac_list {
	int count;
	wl_mac_range_t *mac;
	char name[MOD_PARAM_PATHLEN];
} wl_mac_list_t;

typedef struct wl_mac_list_ctrl {
	int count;
	struct wl_mac_list *m_mac_list_head;
} wl_mac_list_ctrl_t;

typedef struct wl_chip_nv_path {
	uint chip;
	uint chiprev;
	char name[MOD_PARAM_PATHLEN];
} wl_chip_nv_path_t;

typedef struct wl_chip_nv_path_list_ctrl {
	int count;
	struct wl_chip_nv_path *m_chip_nv_path_head;
} wl_chip_nv_path_list_ctrl_t;

typedef struct wl_channel_list {
	uint32 count;
	uint32 channel[WL_NUMCHANNELS];
} wl_channel_list_t;

typedef struct wmes_param {
	int aifsn[AC_COUNT];
	int ecwmin[AC_COUNT];
	int ecwmax[AC_COUNT];
	int txop[AC_COUNT];
} wme_param_t;

#ifdef PKT_FILTER_SUPPORT
#define DHD_CONF_FILTER_MAX	8
#define PKT_FILTER_LEN 300
#define MAGIC_PKT_FILTER_LEN 450
typedef struct conf_pkt_filter_add {
	uint32 count;
	char filter[DHD_CONF_FILTER_MAX][PKT_FILTER_LEN];
} conf_pkt_filter_add_t;

typedef struct conf_pkt_filter_del {
	uint32 count;
	uint32 id[DHD_CONF_FILTER_MAX];
} conf_pkt_filter_del_t;
#endif

#define CONFIG_COUNTRY_LIST_SIZE 100
typedef struct conf_country_list {
	uint32 count;
	wl_country_t *cspec[CONFIG_COUNTRY_LIST_SIZE];
} conf_country_list_t;

/* mchan_params */
#define MCHAN_MAX_NUM 4
#define MIRACAST_SOURCE	1
#define MIRACAST_SINK	2
typedef struct mchan_params {
	int bw;
	int p2p_mode;
	int miracast_mode;
} mchan_params_t;

enum in4way_flags {
	NO_SCAN_IN4WAY	= (1 << (0)),
	NO_BTC_IN4WAY	= (1 << (1)),
	DONT_DELETE_GC_AFTER_WPS	= (1 << (2)),
	WAIT_DISCONNECTED	= (1 << (3)),
};

enum eapol_status {
	EAPOL_STATUS_NONE = 0,
	EAPOL_STATUS_WPS_REQID,
	EAPOL_STATUS_WPS_RSPID,
	EAPOL_STATUS_WPS_WSC_START,
	EAPOL_STATUS_WPS_M1,
	EAPOL_STATUS_WPS_M2,
	EAPOL_STATUS_WPS_M3,
	EAPOL_STATUS_WPS_M4,
	EAPOL_STATUS_WPS_M5,
	EAPOL_STATUS_WPS_M6,
	EAPOL_STATUS_WPS_M7,
	EAPOL_STATUS_WPS_M8,
	EAPOL_STATUS_WPS_DONE,
	EAPOL_STATUS_WPA_START,
	EAPOL_STATUS_WPA_M1,
	EAPOL_STATUS_WPA_M2,
	EAPOL_STATUS_WPA_M3,
	EAPOL_STATUS_WPA_M4,
	EAPOL_STATUS_WPA_END
};

typedef struct dhd_conf {
	uint chip;
	uint chiprev;
	int fw_type;
	wl_mac_list_ctrl_t fw_by_mac;
	wl_mac_list_ctrl_t nv_by_mac;
	wl_chip_nv_path_list_ctrl_t nv_by_chip;
	conf_country_list_t country_list;
	int band;
	int bw_cap[2];
	wl_country_t cspec;
	wl_channel_list_t channels;
	uint roam_off;
	uint roam_off_suspend;
	int roam_trigger[2];
	int roam_scan_period[2];
	int roam_delta[2];
	int fullroamperiod;
	uint keep_alive_period;
	int force_wme_ac;
	wme_param_t wme_sta;
	wme_param_t wme_ap;
	int phy_oclscdenable;
#ifdef PKT_FILTER_SUPPORT
	conf_pkt_filter_add_t pkt_filter_add;
	conf_pkt_filter_del_t pkt_filter_del;
	char *magic_pkt_filter_add;
#endif
	int srl;
	int lrl;
	uint bcn_timeout;
	int txbf;
	int disable_proptx;
	int dhd_poll;
#ifdef BCMSDIO
	int use_rxchain;
	bool bus_rxglom;
	bool txglom_ext; /* Only for 43362/4330/43340/43341/43241 */
	/* terence 20161011:
	    1) conf->tx_max_offset = 1 to fix credict issue in adaptivity testing
	    2) conf->tx_max_offset = 1 will cause to UDP Tx not work in rxglom supported,
	        but not happened in sw txglom
	*/
	int tx_max_offset;
	uint txglomsize;
	int txctl_tmo_fix;
	bool txglom_mode;
	uint deferred_tx_len;
	/*txglom_bucket_size:
	 * 43362/4330: 1680
	 * 43340/43341/43241: 1684
	 */
	int txglom_bucket_size;
	int txinrx_thres;
	int dhd_txminmax; // -1=DATABUFCNT(bus)
	uint sd_f2_blocksize;
	bool oob_enabled_later;
	int orphan_move;
#endif
#ifdef BCMPCIE
	int bus_deepsleep_disable;
#endif
	int dpc_cpucore;
	int rxf_cpucore;
	int frameburst;
	bool deepsleep;
	int pm;
	int pm_in_suspend;
	int suspend_bcn_li_dtim;
#ifdef DHDTCPACK_SUPPRESS
	uint8 tcpack_sup_mode;
#endif
	int pktprio8021x;
	int xmit_in_suspend;
	int ap_in_suspend;
#ifdef SUSPEND_EVENT
	bool suspend_eventmask_enable;
	char suspend_eventmask[WL_EVENTING_MASK_LEN];
	char resume_eventmask[WL_EVENTING_MASK_LEN];
#endif
#ifdef IDHCP
	int dhcpc_enable;
	int dhcpd_enable;
	struct ipv4_addr dhcpd_ip_addr;
	struct ipv4_addr dhcpd_ip_mask;
	struct ipv4_addr dhcpd_ip_start;
	struct ipv4_addr dhcpd_ip_end;
#endif
#ifdef ISAM_PREINIT
	char isam_init[50];
	char isam_config[300];
	char isam_enable[50];
#endif
	int ctrl_resched;
	int dhd_ioctl_timeout_msec;
	struct mchan_params mchan[MCHAN_MAX_NUM];
	char *wl_preinit;
	int tsq;
	uint eapol_status;
	uint in4way;
	uint max_wait_gc_time;
} dhd_conf_t;

#ifdef BCMSDIO
int dhd_conf_get_mac(dhd_pub_t *dhd, bcmsdh_info_t *sdh, uint8 *mac);
void dhd_conf_set_fw_name_by_mac(dhd_pub_t *dhd, bcmsdh_info_t *sdh, char *fw_path);
void dhd_conf_set_nv_name_by_mac(dhd_pub_t *dhd, bcmsdh_info_t *sdh, char *nv_path);
#if defined(HW_OOB) || defined(FORCE_WOWLAN)
void dhd_conf_set_hw_oob_intr(bcmsdh_info_t *sdh, uint chip);
#endif
void dhd_conf_set_txglom_params(dhd_pub_t *dhd, bool enable);
int dhd_conf_set_blksize(bcmsdh_info_t *sdh);
#endif
void dhd_conf_set_fw_name_by_chip(dhd_pub_t *dhd, char *fw_path);
void dhd_conf_set_clm_name_by_chip(dhd_pub_t *dhd, char *clm_path);
void dhd_conf_set_nv_name_by_chip(dhd_pub_t *dhd, char *nv_path);
void dhd_conf_set_path(dhd_pub_t *dhd, char *dst_name, char *dst_path, char *src_path);
#ifdef CONFIG_PATH_AUTO_SELECT
void dhd_conf_set_conf_name_by_chip(dhd_pub_t *dhd, char *conf_path);
#endif
int dhd_conf_set_intiovar(dhd_pub_t *dhd, uint cmd, char *name, int val, int def, bool down);
int dhd_conf_get_iovar(dhd_pub_t *dhd, int cmd, char *name, char *buf, int len, int ifidx);
int dhd_conf_set_bufiovar(dhd_pub_t *dhd, uint cmd, char *name, char *buf, int len, bool down);
uint dhd_conf_get_band(dhd_pub_t *dhd);
int dhd_conf_set_country(dhd_pub_t *dhd, wl_country_t *cspec);
int dhd_conf_get_country(dhd_pub_t *dhd, wl_country_t *cspec);
int dhd_conf_map_country_list(dhd_pub_t *dhd, wl_country_t *cspec);
int dhd_conf_fix_country(dhd_pub_t *dhd);
bool dhd_conf_match_channel(dhd_pub_t *dhd, uint32 channel);
void dhd_conf_set_wme(dhd_pub_t *dhd, int mode);
void dhd_conf_set_mchan_bw(dhd_pub_t *dhd, int go, int source);
void dhd_conf_add_pkt_filter(dhd_pub_t *dhd);
bool dhd_conf_del_pkt_filter(dhd_pub_t *dhd, uint32 id);
void dhd_conf_discard_pkt_filter(dhd_pub_t *dhd);
int dhd_conf_read_config(dhd_pub_t *dhd, char *conf_path);
int dhd_conf_set_chiprev(dhd_pub_t *dhd, uint chip, uint chiprev);
uint dhd_conf_get_chip(void *context);
uint dhd_conf_get_chiprev(void *context);
int dhd_conf_get_pm(dhd_pub_t *dhd);

#ifdef PROP_TXSTATUS
int dhd_conf_get_disable_proptx(dhd_pub_t *dhd);
#endif
int dhd_conf_get_ap_mode_in_suspend(dhd_pub_t *dhd);
int dhd_conf_set_ap_in_suspend(dhd_pub_t *dhd, int suspend);
void dhd_conf_postinit_ioctls(dhd_pub_t *dhd);
int dhd_conf_preinit(dhd_pub_t *dhd);
int dhd_conf_reset(dhd_pub_t *dhd);
int dhd_conf_attach(dhd_pub_t *dhd);
void dhd_conf_detach(dhd_pub_t *dhd);
void *dhd_get_pub(struct net_device *dev);
void *dhd_get_conf(struct net_device *dev);
#endif /* _dhd_config_ */
