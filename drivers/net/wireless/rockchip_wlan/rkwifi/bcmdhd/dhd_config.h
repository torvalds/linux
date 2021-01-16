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

#define CONFIG_COUNTRY_LIST_SIZE 500
typedef struct country_list {
	struct country_list *next;
	wl_country_t cspec;
} country_list_t;

/* mchan_params */
#define MCHAN_MAX_NUM 4
#define MIRACAST_SOURCE	1
#define MIRACAST_SINK	2
typedef struct mchan_params {
	struct mchan_params *next;
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

enum in_suspend_flags {
	NO_EVENT_IN_SUSPEND		= (1 << (0)),
	NO_TXDATA_IN_SUSPEND	= (1 << (1)),
	NO_TXCTL_IN_SUSPEND		= (1 << (2)),
	AP_DOWN_IN_SUSPEND		= (1 << (3)),
	ROAM_OFFLOAD_IN_SUSPEND	= (1 << (4)),
	AP_FILTER_IN_SUSPEND	= (1 << (5)),
	WOWL_IN_SUSPEND			= (1 << (6)),
	ALL_IN_SUSPEND 			= 0xFFFFFFFF,
};

enum in_suspend_mode {
	AUTO_SUSPEND = -1,
	EARLY_SUSPEND = 0,
	PM_NOTIFIER = 1
};

enum eapol_status {
	EAPOL_STATUS_NONE = 0,
	EAPOL_STATUS_REQID = 1,
	EAPOL_STATUS_RSPID = 2,
	EAPOL_STATUS_WSC_START = 3,
	EAPOL_STATUS_WPS_M1 = 4,
	EAPOL_STATUS_WPS_M2 = 5,
	EAPOL_STATUS_WPS_M3 = 6,
	EAPOL_STATUS_WPS_M4 = 7,
	EAPOL_STATUS_WPS_M5 = 8,
	EAPOL_STATUS_WPS_M6 = 9,
	EAPOL_STATUS_WPS_M7 = 10,
	EAPOL_STATUS_WPS_M8 = 11,
	EAPOL_STATUS_WSC_DONE = 12,
	EAPOL_STATUS_4WAY_START = 13,
	EAPOL_STATUS_4WAY_M1 = 14,
	EAPOL_STATUS_4WAY_M2 = 15,
	EAPOL_STATUS_4WAY_M3 = 16,
	EAPOL_STATUS_4WAY_M4 = 17,
	EAPOL_STATUS_GROUPKEY_M1 = 18,
	EAPOL_STATUS_GROUPKEY_M2 = 19,
	EAPOL_STATUS_4WAY_DONE = 20
};

typedef struct dhd_conf {
	uint chip;
	uint chiprev;
	int fw_type;
#ifdef BCMSDIO
	wl_mac_list_ctrl_t fw_by_mac;
	wl_mac_list_ctrl_t nv_by_mac;
#endif
	wl_chip_nv_path_list_ctrl_t nv_by_chip;
	country_list_t *country_head;
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
#ifdef ARP_OFFLOAD_SUPPORT
	bool garp;
#endif
	int force_wme_ac;
	wme_param_t wme_sta;
	wme_param_t wme_ap;
#ifdef PKT_FILTER_SUPPORT
	conf_pkt_filter_add_t pkt_filter_add;
	conf_pkt_filter_del_t pkt_filter_del;
	char *magic_pkt_filter_add;
#endif
	int srl;
	int lrl;
	uint bcn_timeout;
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
	bool oob_enabled_later;
#if defined(SDIO_ISR_THREAD)
	bool intr_extn;
#endif
#ifdef BCMSDIO_RXLIM_POST
	bool rxlim_en;
#endif
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
	int suspend_mode;
	int suspend_bcn_li_dtim;
#ifdef DHDTCPACK_SUPPRESS
	uint8 tcpack_sup_mode;
#endif
	int pktprio8021x;
	uint insuspend;
	bool suspended;
#ifdef SUSPEND_EVENT
	char resume_eventmask[WL_EVENTING_MASK_LEN];
	struct ether_addr bssid_insuspend;
	bool wlfc;
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
	mchan_params_t *mchan;
	char *wl_preinit;
	int tsq;
	int orphan_move;
	uint eapol_status;
	uint in4way;
#ifdef WL_EXT_WOWL
	uint wowl;
#endif
#ifdef GET_CUSTOM_MAC_FROM_CONFIG
	char hw_ether[62];
#endif
	wait_queue_head_t event_complete;
} dhd_conf_t;

#ifdef BCMSDIO
int dhd_conf_get_mac(dhd_pub_t *dhd, bcmsdh_info_t *sdh, uint8 *mac);
#if defined(HW_OOB) || defined(FORCE_WOWLAN)
void dhd_conf_set_hw_oob_intr(bcmsdh_info_t *sdh, uint chip);
#endif
void dhd_conf_set_txglom_params(dhd_pub_t *dhd, bool enable);
int dhd_conf_set_blksize(bcmsdh_info_t *sdh);
#endif
void dhd_conf_set_path_params(dhd_pub_t *dhd, void *sdh,
	char *fw_path, char *nv_path);
int dhd_conf_set_intiovar(dhd_pub_t *dhd, uint cmd, char *name, int val,
	int def, bool down);
int dhd_conf_get_band(dhd_pub_t *dhd);
int dhd_conf_set_country(dhd_pub_t *dhd, wl_country_t *cspec);
int dhd_conf_get_country(dhd_pub_t *dhd, wl_country_t *cspec);
int dhd_conf_map_country_list(dhd_pub_t *dhd, wl_country_t *cspec);
#ifdef CCODE_LIST
int dhd_ccode_map_country_list(dhd_pub_t *dhd, wl_country_t *cspec);
#endif
int dhd_conf_fix_country(dhd_pub_t *dhd);
bool dhd_conf_match_channel(dhd_pub_t *dhd, uint32 channel);
void dhd_conf_set_wme(dhd_pub_t *dhd, int ifidx, int mode);
void dhd_conf_set_mchan_bw(dhd_pub_t *dhd, int go, int source);
void dhd_conf_add_pkt_filter(dhd_pub_t *dhd);
bool dhd_conf_del_pkt_filter(dhd_pub_t *dhd, uint32 id);
void dhd_conf_discard_pkt_filter(dhd_pub_t *dhd);
int dhd_conf_read_config(dhd_pub_t *dhd, char *conf_path);
int dhd_conf_set_chiprev(dhd_pub_t *dhd, uint chip, uint chiprev);
uint dhd_conf_get_chip(void *context);
uint dhd_conf_get_chiprev(void *context);
int dhd_conf_get_pm(dhd_pub_t *dhd);
int dhd_conf_check_hostsleep(dhd_pub_t *dhd, int cmd, void *buf, int len,
	int *hostsleep_set, int *hostsleep_val, int *ret);
void dhd_conf_get_hostsleep(dhd_pub_t *dhd,
	int hostsleep_set, int hostsleep_val, int ret);
int dhd_conf_mkeep_alive(dhd_pub_t *dhd, int ifidx, int id, int period,
	char *packet, bool bcast);
#ifdef ARP_OFFLOAD_SUPPORT
void dhd_conf_set_garp(dhd_pub_t *dhd, int ifidx, uint32 ipa, bool enable);
#endif
#ifdef PROP_TXSTATUS
int dhd_conf_get_disable_proptx(dhd_pub_t *dhd);
#endif
uint dhd_conf_get_insuspend(dhd_pub_t *dhd, uint mask);
int dhd_conf_set_suspend_resume(dhd_pub_t *dhd, int suspend, int suspend_mode);
void dhd_conf_postinit_ioctls(dhd_pub_t *dhd);
int dhd_conf_preinit(dhd_pub_t *dhd);
int dhd_conf_reset(dhd_pub_t *dhd);
int dhd_conf_attach(dhd_pub_t *dhd);
void dhd_conf_detach(dhd_pub_t *dhd);
void *dhd_get_pub(struct net_device *dev);
int wl_pattern_atoh(char *src, char *dst);
#ifdef BCMSDIO
extern int dhd_bus_sleep(dhd_pub_t *dhdp, bool sleep, uint32 *intstatus);
#endif
#endif /* _dhd_config_ */
