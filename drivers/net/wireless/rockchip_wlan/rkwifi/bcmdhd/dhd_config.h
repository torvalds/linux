/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _dhd_config_
#define _dhd_config_

#include <bcmdevs.h>
#include <siutils.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <wlioctl.h>
#include <802.11.h>

#define FW_TYPE_STA     0
#define FW_TYPE_APSTA   1
#define FW_TYPE_P2P     2
#define FW_TYPE_MESH    3
#define FW_TYPE_EZMESH  4
#define FW_TYPE_ES      5
#define FW_TYPE_MFG     6
#define FW_TYPE_MINIME  7
#define FW_TYPE_G       0
#define FW_TYPE_AG      1

#define FW_PATH_AUTO_SELECT 1
#ifdef BCMDHD_MDRIVER
#define CONFIG_PATH_AUTO_SELECT
#else
//#define CONFIG_PATH_AUTO_SELECT
#endif
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

#ifdef SET_FWNV_BY_MAC
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
#endif

typedef struct wl_chip_nv_path {
	uint chip;
	uint chiprev;
	char name[MOD_PARAM_PATHLEN];
} wl_chip_nv_path_t;

typedef struct wl_chip_nv_path_list_ctrl {
	int count;
	struct wl_chip_nv_path *m_chip_nv_path_head;
} wl_chip_nv_path_list_ctrl_t;

#define MAX_CTRL_CHANSPECS 256
typedef struct wl_channel_list {
	uint32 count;
	uint32 channel[MAX_CTRL_CHANSPECS];
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

#ifdef SCAN_SUPPRESS
enum scan_intput_flags {
	NO_SCAN_INTPUT	= (1 << (0)),
	SCAN_CURCHAN_INTPUT	= (1 << (1)),
	SCAN_LIGHT_INTPUT	= (1 << (2)),
};
#endif

enum war_flags {
	SET_CHAN_INCONN	= (1 << (0)),
	FW_REINIT_INCSA	= (1 << (1)),
	FW_REINIT_EMPTY_SCAN	= (1 << (2)),
	P2P_AP_MAC_CONFLICT	= (1 << (3)),
	RESEND_EAPOL_PKT	= (1 << (4))
};

enum in4way_flags {
	STA_NO_SCAN_IN4WAY	= (1 << (0)),
	STA_NO_BTC_IN4WAY	= (1 << (1)),
	STA_WAIT_DISCONNECTED	= (1 << (2)),
	AP_WAIT_STA_RECONNECT	= (1 << (3)),
	STA_FAKE_SCAN_IN_CONNECT	= (1 << (4)),
	STA_REASSOC_RETRY	= (1 << (5)),
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
	EARLY_SUSPEND = 0,
	PM_NOTIFIER = 1,
	SUSPEND_MODE_2 = 2
};

#ifdef TPUT_MONITOR
enum data_drop_mode {
	NO_DATA_DROP = -1,
	FW_DROP = 0,
	TXPKT_DROP = 1,
	XMIT_DROP = 2
};
#endif

enum conn_state {
	CONN_STATE_IDLE = 0,
	CONN_STATE_CONNECTING = 1,
	CONN_STATE_AUTH_SAE_M1 = 2,
	CONN_STATE_AUTH_SAE_M2 = 3,
	CONN_STATE_AUTH_SAE_M3 = 4,
	CONN_STATE_AUTH_SAE_M4 = 5,
	CONN_STATE_REQID = 6,
	CONN_STATE_RSPID = 7,
	CONN_STATE_WSC_START = 8,
	CONN_STATE_WPS_M1 = 9,
	CONN_STATE_WPS_M2 = 10,
	CONN_STATE_WPS_M3 = 11,
	CONN_STATE_WPS_M4 = 12,
	CONN_STATE_WPS_M5 = 13,
	CONN_STATE_WPS_M6 = 14,
	CONN_STATE_WPS_M7 = 15,
	CONN_STATE_WPS_M8 = 16,
	CONN_STATE_WSC_DONE = 17,
	CONN_STATE_4WAY_M1 = 18,
	CONN_STATE_4WAY_M2 = 19,
	CONN_STATE_4WAY_M3 = 20,
	CONN_STATE_4WAY_M4 = 21,
	CONN_STATE_CONNECTED = 22,
	CONN_STATE_GROUPKEY_M1 = 23,
	CONN_STATE_GROUPKEY_M2 = 24,
};

enum enq_pkt_type {
	ENQ_PKT_TYPE_EAPOL	= (1 << (0)),
	ENQ_PKT_TYPE_ARP	= (1 << (1)),
	ENQ_PKT_TYPE_DHCP	= (1 << (2)),
	ENQ_PKT_TYPE_ICMP	= (1 << (3)),
};

typedef struct dhd_conf {
	uint devid;
	uint chip;
	uint chiprev;
#if defined(BCMPCIE)
	uint svid;
	uint ssid;
#endif
#ifdef GET_OTP_MODULE_NAME
	char module_name[16];
#endif
	struct ether_addr otp_mac;
	int fw_type;
#ifdef SET_FWNV_BY_MAC
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
	bool rekey_offload;
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
#ifdef DYNAMIC_MAX_HDR_READ
	int max_hdr_read;
#endif
	bool oob_enabled_later;
#ifdef MINIME
	uint32 ramsize;
#endif
#if defined(SDIO_ISR_THREAD)
	bool intr_extn;
#endif
#ifdef BCMSDIO_RXLIM_POST
	bool rxlim_en;
#endif
#ifdef BCMSDIO_TXSEQ_SYNC
	bool txseq_sync;
#endif
#ifdef BCMSDIO_INTSTATUS_WAR
	uint read_intr_mode;
#endif
	int kso_try_max;
#ifdef KSO_DEBUG
	uint kso_try_array[10];
#endif
#endif
#ifdef BCMPCIE
	int bus_deepsleep_disable;
	int flow_ring_queue_threshold;
	int d2h_intr_method;
	int d2h_intr_control;
	int enq_hdr_pkt;
#endif
	int dpc_cpucore;
	int rxf_cpucore;
	int dhd_dpc_prio;
	int frameburst;
	bool deepsleep;
	int pm;
	int pm_in_suspend;
	int suspend_mode;
	int suspend_bcn_li_dtim;
#ifdef DHDTCPACK_SUPPRESS
	uint8 tcpack_sup_mode;
	uint32 tcpack_sup_ratio;
	uint32 tcpack_sup_delay;
#endif
	int pktprio8021x;
	uint insuspend;
	bool suspended;
	struct ether_addr bssid_insuspend;
#ifdef SUSPEND_EVENT
	char resume_eventmask[WL_EVENTING_MASK_LEN];
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
	uint rxcnt_timeout;
	mchan_params_t *mchan;
	char *wl_preinit;
	char *wl_suspend;
	char *wl_resume;
	int tsq;
	int orphan_move;
	uint in4way;
	uint war;
#ifdef WL_EXT_WOWL
	uint wowl;
#endif
#ifdef GET_CUSTOM_MAC_FROM_CONFIG
	char hw_ether[62];
#endif
	wait_queue_head_t event_complete;
#ifdef PROPTX_MAXCOUNT
	int proptx_maxcnt_2g;
	int proptx_maxcnt_5g;
#endif /* DYNAMIC_PROPTX_MAXCOUNT */
#ifdef TPUT_MONITOR
	int data_drop_mode;
	unsigned long net_len;
	uint tput_monitor_ms;
	struct osl_timespec tput_ts;
	unsigned long last_tx;
	unsigned long last_rx;
	unsigned long last_net_tx;
#ifdef BCMSDIO
	int32 doflow_tput_thresh;
#endif
#endif
#ifdef SCAN_SUPPRESS
	uint scan_intput;
	int scan_busy_thresh;
	int scan_busy_tmo;
	int32 scan_tput_thresh;
#endif
#ifdef DHD_TPUT_PATCH
	bool tput_patch;
	int mtu;
	bool pktsetsum;
#endif
#ifdef SET_XPS_CPUS
	bool xps_cpus;
#endif
#ifdef SET_RPS_CPUS
	bool rps_cpus;
#endif
#ifdef CHECK_DOWNLOAD_FW
	bool fwchk;
#endif
	char *vndr_ie_assocreq;
} dhd_conf_t;

#ifdef BCMSDIO
void dhd_conf_get_otp(dhd_pub_t *dhd, bcmsdh_info_t *sdh, si_t *sih);
#if defined(HW_OOB) || defined(FORCE_WOWLAN)
void dhd_conf_set_hw_oob_intr(bcmsdh_info_t *sdh, struct si_pub *sih);
#endif
void dhd_conf_set_txglom_params(dhd_pub_t *dhd, bool enable);
#endif
#ifdef BCMPCIE
int dhd_conf_get_otp(dhd_pub_t *dhd, si_t *sih);
bool dhd_conf_legacy_msi_chip(dhd_pub_t *dhd);
#endif
void dhd_conf_set_path_params(dhd_pub_t *dhd, char *fw_path, char *nv_path);
int dhd_conf_set_intiovar(dhd_pub_t *dhd, int ifidx, uint cmd, char *name,
	int val, int def, bool down);
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
#ifdef TPUT_MONITOR
void dhd_conf_tput_monitor(dhd_pub_t *dhd);
#endif
uint dhd_conf_get_insuspend(dhd_pub_t *dhd, uint mask);
int dhd_conf_set_suspend_resume(dhd_pub_t *dhd, int suspend);
void dhd_conf_postinit_ioctls(dhd_pub_t *dhd);
int dhd_conf_preinit(dhd_pub_t *dhd);
int dhd_conf_reset(dhd_pub_t *dhd);
int dhd_conf_attach(dhd_pub_t *dhd);
void dhd_conf_detach(dhd_pub_t *dhd);
void *dhd_get_pub(struct net_device *dev);
int wl_pattern_atoh(char *src, char *dst);
int dhd_conf_suspend_resume_sta(dhd_pub_t *dhd, int ifidx, int suspend);
/* Add to adjust 802.1x priority */
extern void pktset8021xprio(void *pkt, int prio);
#ifdef BCMSDIO
extern int dhd_bus_sleep(dhd_pub_t *dhdp, bool sleep, uint32 *intstatus);
#endif
#endif /* _dhd_config_ */
