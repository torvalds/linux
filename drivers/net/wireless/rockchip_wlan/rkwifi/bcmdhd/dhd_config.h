
#ifndef _dhd_config_
#define _dhd_config_

#include <bcmdevs.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <wlioctl.h>
#include <proto/802.11.h>

#define FW_PATH_AUTO_SELECT 1
//#define CONFIG_PATH_AUTO_SELECT
extern char firmware_path[MOD_PARAM_PATHLEN];
extern uint dhd_rxbound;
extern uint dhd_txbound;
#ifdef BCMSDIO
#define TXGLOM_RECV_OFFSET 8
extern uint dhd_doflow;
extern uint dhd_slpauto;

#define BCM43362A0_CHIP_REV     0
#define BCM43362A2_CHIP_REV     1
#define BCM43430A0_CHIP_REV     0
#define BCM43430A1_CHIP_REV     1
#define BCM43430A2_CHIP_REV     2
#define BCM4330B2_CHIP_REV      4
#define BCM4334B1_CHIP_REV      3
#define BCM43341B0_CHIP_REV     2
#define BCM43241B4_CHIP_REV     5
#define BCM4335A0_CHIP_REV      2
#define BCM4339A0_CHIP_REV      1
#define BCM43455C0_CHIP_REV     6
#define BCM4354A1_CHIP_REV      1
#define BCM4359B1_CHIP_REV      5
#define BCM4359C0_CHIP_REV      9
#endif
#define BCM4356A2_CHIP_REV      2

/* mac range */
typedef struct wl_mac_range {
	uint32 oui;
	uint32 nic_start;
	uint32 nic_end;
} wl_mac_range_t;

/* mac list */
typedef struct wl_mac_list {
	int count;
	wl_mac_range_t *mac;
	char name[MOD_PARAM_PATHLEN];		/* path */
} wl_mac_list_t;

/* mac list head */
typedef struct wl_mac_list_ctrl {
	int count;
	struct wl_mac_list *m_mac_list_head;
} wl_mac_list_ctrl_t;

/* chip_nv_path */
typedef struct wl_chip_nv_path {
	uint chip;
	uint chiprev;
	char name[MOD_PARAM_PATHLEN];		/* path */
} wl_chip_nv_path_t;

/* chip_nv_path list head */
typedef struct wl_chip_nv_path_list_ctrl {
	int count;
	struct wl_chip_nv_path *m_chip_nv_path_head;
} wl_chip_nv_path_list_ctrl_t;

/* channel list */
typedef struct wl_channel_list {
	/* in - # of channels, out - # of entries */
	uint32 count;
	/* variable length channel list */
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
/* filter list */
#define PKT_FILTER_LEN 300
typedef struct conf_pkt_filter_add {
	/* in - # of channels, out - # of entries */
	uint32 count;
	/* variable length filter list */
	char filter[DHD_CONF_FILTER_MAX][PKT_FILTER_LEN];
} conf_pkt_filter_add_t;

/* pkt_filter_del list */
typedef struct conf_pkt_filter_del {
	/* in - # of channels, out - # of entries */
	uint32 count;
	/* variable length filter list */
	uint32 id[DHD_CONF_FILTER_MAX];
} conf_pkt_filter_del_t;
#endif

#define CONFIG_COUNTRY_LIST_SIZE 100
/* country list */
typedef struct conf_country_list {
	uint32 count;
	wl_country_t cspec[CONFIG_COUNTRY_LIST_SIZE];
} conf_country_list_t;

typedef struct dhd_conf {
	uint	chip;			/* chip number */
	uint	chiprev;		/* chip revision */
	wl_mac_list_ctrl_t fw_by_mac;	/* Firmware auto selection by MAC */
	wl_mac_list_ctrl_t nv_by_mac;	/* NVRAM auto selection by MAC */
	wl_chip_nv_path_list_ctrl_t nv_by_chip;	/* NVRAM auto selection by chip */
	conf_country_list_t country_list; /* Country list */
	int band;			/* Band, b:2.4G only, otherwise for auto */
	int mimo_bw_cap;			/* Bandwidth, 0:HT20ALL, 1: HT40ALL, 2:HT20IN2G_HT40PIN5G */
	int bw_cap_2g;			/* Bandwidth, 1:20MHz, 3: 20/40MHz, 7:20/40/80MHz */
	int bw_cap_5g;			/* Bandwidth, 1:20MHz, 3: 20/40MHz, 7:20/40/80MHz */
	wl_country_t cspec;		/* Country */
	wl_channel_list_t channels;	/* Support channels */
	uint roam_off;		/* Roaming, 0:enable, 1:disable */
	uint roam_off_suspend;		/* Roaming in suspend, 0:enable, 1:disable */
	int roam_trigger[2];		/* The RSSI threshold to trigger roaming */
	int roam_scan_period[2];	/* Roaming scan period */
	int roam_delta[2];			/* Roaming candidate qualification delta */
	int fullroamperiod;			/* Full Roaming period */
	uint keep_alive_period;		/* The perioid in ms to send keep alive packet */
	int force_wme_ac;
	wme_param_t wme;	/* WME parameters */
	int stbc;			/* STBC for Tx/Rx */
	int phy_oclscdenable;		/* phy_oclscdenable */
#ifdef PKT_FILTER_SUPPORT
	conf_pkt_filter_add_t pkt_filter_add;		/* Packet filter add */
	conf_pkt_filter_del_t pkt_filter_del;		/* Packet filter add */
	conf_pkt_filter_add_t magic_pkt_filter_add;		/* Magic Packet filter add */
#endif
	int srl;	/* short retry limit */
	int lrl;	/* long retry limit */
	uint bcn_timeout;	/* beacon timeout */
	int spect;
	int txbf;
	int lpc;
	int disable_proptx;
#ifdef BCMSDIO
	int bus_txglom;	/* bus:txglom */
	int use_rxchain;
	bool bus_rxglom; /* bus:rxglom */
	bool txglom_ext; /* Only for 43362/4330/43340/43341/43241 */
	/* terence 20161011:
	    1) conf->tx_max_offset = 1 to fix credict issue in adaptivity testing
	    2) conf->tx_max_offset = 1 will cause to UDP Tx not work in rxglom supported,
	        but not happened in sw txglom
	*/
	int tx_max_offset;
	uint txglomsize;
	int dhd_poll;
	/* terence 20161011: conf->txctl_tmo_fix = 1 to fix for "sched: RT throttling activated, "
	     this issue happened in tx tput. and tx cmd at the same time in inband interrupt mode
	*/
	bool txctl_tmo_fix;
	bool tx_in_rx; // Skip tx before rx, in order to get more glomed in tx
	bool txglom_mode;
	uint deferred_tx_len;
	bool swtxglom; /* SW TXGLOM */
	/*txglom_bucket_size:
	 * 43362/4330: 1680
	 * 43340/43341/43241: 1684
	 */
	int txglom_bucket_size;
	int txinrx_thres;
	int dhd_txminmax; // -1=DATABUFCNT(bus)
	uint sd_f2_blocksize;
	bool oob_enabled_later;
#endif
	int ampdu_ba_wsize;
	int ampdu_hostreorder;
	int dpc_cpucore;
	int frameburst;
	bool deepsleep;
	int pm;
	int pm_in_suspend;
	int pm2_sleep_ret;
#ifdef DHDTCPACK_SUPPRESS
	uint8 tcpack_sup_mode;
#endif
	int pktprio8021x;
	int rsdb_mode;
	int vhtmode;
	int num_different_channels;
	int xmit_in_suspend;
#ifdef IDHCPC
	int dhcpc_enable;
#endif
#ifdef IAPSTA_PREINIT
	char iapsta_init[50];
	char iapsta_config[300];
	char iapsta_enable[50];
#endif
	int tsq;
} dhd_conf_t;

#ifdef BCMSDIO
int dhd_conf_get_mac(dhd_pub_t *dhd, bcmsdh_info_t *sdh, uint8 *mac);
void dhd_conf_set_fw_name_by_mac(dhd_pub_t *dhd, bcmsdh_info_t *sdh, char *fw_path);
void dhd_conf_set_nv_name_by_mac(dhd_pub_t *dhd, bcmsdh_info_t *sdh, char *nv_path);
#if defined(HW_OOB) || defined(FORCE_WOWLAN)
void dhd_conf_set_hw_oob_intr(bcmsdh_info_t *sdh, uint chip);
#endif
void dhd_conf_set_txglom_params(dhd_pub_t *dhd, bool enable);
#endif
void dhd_conf_set_fw_name_by_chip(dhd_pub_t *dhd, char *fw_path, char *nv_path);
void dhd_conf_set_nv_name_by_chip(dhd_pub_t *dhd, char *nv_path);
void dhd_conf_set_conf_path_by_nv_path(dhd_pub_t *dhd, char *conf_path, char *nv_path);
#ifdef CONFIG_PATH_AUTO_SELECT
void dhd_conf_set_conf_name_by_chip(dhd_pub_t *dhd, char *conf_path);
#endif
int dhd_conf_set_intiovar(dhd_pub_t *dhd, uint cmd, char *name, int val, int def, bool down);
int dhd_conf_get_iovar(dhd_pub_t *dhd, int cmd, char *name, char *buf, int len, int ifidx);
uint dhd_conf_get_band(dhd_pub_t *dhd);
int dhd_conf_set_country(dhd_pub_t *dhd);
int dhd_conf_get_country(dhd_pub_t *dhd, wl_country_t *cspec);
int dhd_conf_get_country_from_config(dhd_pub_t *dhd, wl_country_t *cspec);
int dhd_conf_fix_country(dhd_pub_t *dhd);
bool dhd_conf_match_channel(dhd_pub_t *dhd, uint32 channel);
int dhd_conf_set_roam(dhd_pub_t *dhd);
void dhd_conf_set_bw_cap(dhd_pub_t *dhd);
void dhd_conf_get_wme(dhd_pub_t *dhd, edcf_acparam_t *acp);
void dhd_conf_set_wme(dhd_pub_t *dhd);
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
int dhd_conf_preinit(dhd_pub_t *dhd);
int dhd_conf_reset(dhd_pub_t *dhd);
int dhd_conf_attach(dhd_pub_t *dhd);
void dhd_conf_detach(dhd_pub_t *dhd);
void *dhd_get_pub(struct net_device *dev);
void *dhd_get_conf(struct net_device *dev);
#endif /* _dhd_config_ */
