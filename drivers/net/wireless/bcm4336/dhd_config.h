
#ifndef _dhd_config_
#define _dhd_config_

#include <bcmdevs.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <wlioctl.h>
#include <proto/802.11.h>

#define FW_PATH_AUTO_SELECT 1
extern char firmware_path[MOD_PARAM_PATHLEN];
extern int disable_proptx;
extern uint dhd_doflow;

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

/* channel list */
typedef struct wl_channel_list {
	/* in - # of channels, out - # of entries */
	uint32 count;
	/* variable length channel list */
	uint32 channel[WL_NUMCHANNELS];
} wl_channel_list_t;

typedef struct wmes_param {
	int aifsn[AC_COUNT];
	int cwmin[AC_COUNT];
	int cwmax[AC_COUNT];
} wme_param_t;

#ifdef PKT_FILTER_SUPPORT
#define DHD_CONF_FILTER_MAX	8
/* filter list */
#define PKT_FILTER_LEN 150
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

typedef struct dhd_conf {
	uint	chip;			/* chip number */
	uint	chiprev;		/* chip revision */
	wl_mac_list_ctrl_t fw_by_mac;	/* Firmware auto selection by MAC */
	wl_mac_list_ctrl_t nv_by_mac;	/* NVRAM auto selection by MAC */
	char fw_path[MOD_PARAM_PATHLEN];		/* Firmware path */
	char nv_path[MOD_PARAM_PATHLEN];		/* NVRAM path */
	uint band;			/* Band, b:2.4G only, otherwise for auto */
	int mimo_bw_cap;			/* Bandwidth, 0:HT20ALL, 1: HT40ALL, 2:HT20IN2G_HT40PIN5G */
	wl_country_t cspec;		/* Country */
	wl_channel_list_t channels;	/* Support channels */
	uint roam_off;		/* Roaming, 0:enable, 1:disable */
	uint roam_off_suspend;		/* Roaming in suspend, 0:enable, 1:disable */
	int roam_trigger[2];		/* The RSSI threshold to trigger roaming */
	int roam_scan_period[2];	/* Roaming scan period */
	int roam_delta[2];			/* Roaming candidate qualification delta */
	int fullroamperiod;			/* Full Roaming period */
	uint keep_alive_period;		/* The perioid in ms to send keep alive packet */
	uint force_wme_ac;
	wme_param_t wme;	/* WME parameters */
	int stbc;			/* STBC for Tx/Rx */
	int phy_oclscdenable;		/* phy_oclscdenable */
#ifdef PKT_FILTER_SUPPORT
	conf_pkt_filter_add_t pkt_filter_add;		/* Packet filter add */
	conf_pkt_filter_del_t pkt_filter_del;		/* Packet filter add */
#endif
	int srl;	/* short retry limit */
	int lrl;	/* long retry limit */
	uint bcn_timeout;	/* beacon timeout */
	uint32 bus_txglom;	/* bus:txglom */
	uint32 ampdu_ba_wsize;
	bool kso_enable;
	int spect;
} dhd_conf_t;

int dhd_conf_get_mac(dhd_pub_t *dhd, bcmsdh_info_t *sdh, uint8 *mac);
void dhd_conf_set_fw_name_by_mac(dhd_pub_t *dhd, bcmsdh_info_t *sdh, char *fw_path);
void dhd_conf_set_nv_name_by_mac(dhd_pub_t *dhd, bcmsdh_info_t *sdh, char *nv_path);
void dhd_conf_set_fw_name_by_chip(dhd_pub_t *dhd, char *fw_path);
#if defined(HW_OOB)
void dhd_conf_set_hw_oob_intr(bcmsdh_info_t *sdh, uint chip);
#endif
void dhd_conf_set_fw_path(dhd_pub_t *dhd, char *fw_path);
void dhd_conf_set_nv_path(dhd_pub_t *dhd, char *nv_path);
int dhd_conf_set_band(dhd_pub_t *dhd);
uint dhd_conf_get_band(dhd_pub_t *dhd);
int dhd_conf_set_country(dhd_pub_t *dhd);
int dhd_conf_get_country(dhd_pub_t *dhd, wl_country_t *cspec);
int dhd_conf_fix_country(dhd_pub_t *dhd);
bool dhd_conf_match_channel(dhd_pub_t *dhd, uint32 channel);
int dhd_conf_set_roam(dhd_pub_t *dhd);
void dhd_conf_set_mimo_bw_cap(dhd_pub_t *dhd);
void dhd_conf_force_wme(dhd_pub_t *dhd);
void dhd_conf_get_wme(dhd_pub_t *dhd, edcf_acparam_t *acp);
void dhd_conf_set_wme(dhd_pub_t *dhd);
void dhd_conf_set_stbc(dhd_pub_t *dhd);
void dhd_conf_set_phyoclscdenable(dhd_pub_t *dhd);
void dhd_conf_add_pkt_filter(dhd_pub_t *dhd);
bool dhd_conf_del_pkt_filter(dhd_pub_t *dhd, uint32 id);
void dhd_conf_discard_pkt_filter(dhd_pub_t *dhd);
void dhd_conf_set_srl(dhd_pub_t *dhd);
void dhd_conf_set_lrl(dhd_pub_t *dhd);
void dhd_conf_set_glom(dhd_pub_t *dhd);
void dhd_conf_set_ampdu_ba_wsize(dhd_pub_t *dhd);
void dhd_conf_set_spect(dhd_pub_t *dhd);
int dhd_conf_read_config(dhd_pub_t *dhd);
int dhd_conf_set_chiprev(dhd_pub_t *dhd, uint chip, uint chiprev);
uint dhd_conf_get_chip(void *context);
uint dhd_conf_get_chiprev(void *context);
int dhd_conf_preinit(dhd_pub_t *dhd);
int dhd_conf_reset(dhd_pub_t *dhd);
int dhd_conf_attach(dhd_pub_t *dhd);
void dhd_conf_detach(dhd_pub_t *dhd);
void *dhd_get_pub(struct net_device *dev);

#endif /* _dhd_config_ */
