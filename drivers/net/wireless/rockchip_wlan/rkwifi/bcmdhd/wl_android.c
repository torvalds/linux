/*
 * Linux cfg80211 driver - Android related functions
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: wl_android.c 505064 2014-09-26 09:40:28Z $
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/netlink.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif

#include <wl_android.h>
#include <wldev_common.h>
#include <wlioctl.h>
#include <bcmutils.h>
#include <linux_osl.h>
#include <dhd_dbg.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_config.h>
#include <proto/bcmip.h>
#ifdef PNO_SUPPORT
#include <dhd_pno.h>
#endif
#ifdef BCMSDIO
#include <bcmsdbus.h>
#endif
#ifdef WL_CFG80211
#include <wl_cfg80211.h>
#endif
#ifdef WL_NAN
#include <wl_cfgnan.h>
#endif /* WL_NAN */

#ifndef WL_CFG80211
#define htod32(i) i
#define htod16(i) i
#define dtoh32(i) i
#define dtoh16(i) i
#define htodchanspec(i) i
#define dtohchanspec(i) i
#endif

/* message levels */
#define ANDROID_ERROR_LEVEL	0x0001
#define ANDROID_TRACE_LEVEL	0x0002
#define ANDROID_INFO_LEVEL	0x0004

uint android_msg_level = ANDROID_ERROR_LEVEL;

#define ANDROID_ERROR(x) \
	do { \
		if (android_msg_level & ANDROID_ERROR_LEVEL) { \
			printk(KERN_ERR "ANDROID-ERROR) ");	\
			printk x; \
		} \
	} while (0)
#define ANDROID_TRACE(x) \
	do { \
		if (android_msg_level & ANDROID_TRACE_LEVEL) { \
			printk(KERN_ERR "ANDROID-TRACE) ");	\
			printk x; \
		} \
	} while (0)
#define ANDROID_INFO(x) \
	do { \
		if (android_msg_level & ANDROID_INFO_LEVEL) { \
			printk(KERN_ERR "ANDROID-INFO) ");	\
			printk x; \
		} \
	} while (0)

/*
 * Android private command strings, PLEASE define new private commands here
 * so they can be updated easily in the future (if needed)
 */

#define CMD_START		"START"
#define CMD_STOP		"STOP"
#define	CMD_SCAN_ACTIVE		"SCAN-ACTIVE"
#define	CMD_SCAN_PASSIVE	"SCAN-PASSIVE"
#define CMD_RSSI		"RSSI"
#define CMD_LINKSPEED		"LINKSPEED"
#ifdef PKT_FILTER_SUPPORT
#define CMD_RXFILTER_START	"RXFILTER-START"
#define CMD_RXFILTER_STOP	"RXFILTER-STOP"
#define CMD_RXFILTER_ADD	"RXFILTER-ADD"
#define CMD_RXFILTER_REMOVE	"RXFILTER-REMOVE"
#if defined(CUSTOM_PLATFORM_NV_TEGRA)
#define CMD_PKT_FILTER_MODE		"PKT_FILTER_MODE"
#define CMD_PKT_FILTER_PORTS	"PKT_FILTER_PORTS"
#endif /* defined(CUSTOM_PLATFORM_NV_TEGRA) */
#endif /* PKT_FILTER_SUPPORT */
#define CMD_BTCOEXSCAN_START	"BTCOEXSCAN-START"
#define CMD_BTCOEXSCAN_STOP	"BTCOEXSCAN-STOP"
#define CMD_BTCOEXMODE		"BTCOEXMODE"
#define CMD_SETSUSPENDOPT	"SETSUSPENDOPT"
#define CMD_SETSUSPENDMODE      "SETSUSPENDMODE"
#define CMD_P2P_DEV_ADDR	"P2P_DEV_ADDR"
#define CMD_SETFWPATH		"SETFWPATH"
#define CMD_SETBAND		"SETBAND"
#define CMD_GETBAND		"GETBAND"
#define CMD_COUNTRY		"COUNTRY"
#define CMD_P2P_SET_NOA		"P2P_SET_NOA"
#if !defined WL_ENABLE_P2P_IF
#define CMD_P2P_GET_NOA			"P2P_GET_NOA"
#endif /* WL_ENABLE_P2P_IF */
#define CMD_P2P_SD_OFFLOAD		"P2P_SD_"
#define CMD_P2P_SET_PS		"P2P_SET_PS"
#define CMD_SET_AP_WPS_P2P_IE 		"SET_AP_WPS_P2P_IE"
#define CMD_SETROAMMODE 	"SETROAMMODE"
#define CMD_SETIBSSBEACONOUIDATA	"SETIBSSBEACONOUIDATA"
#define CMD_MIRACAST		"MIRACAST"
#define CMD_NAN		"NAN_"
#define CMD_GET_CHANNEL			"GET_CHANNEL"
#define CMD_SET_ROAM			"SET_ROAM_TRIGGER"
#define CMD_GET_ROAM			"GET_ROAM_TRIGGER"
#define CMD_GET_KEEP_ALIVE		"GET_KEEP_ALIVE"
#define CMD_GET_PM				"GET_PM"
#define CMD_SET_PM				"SET_PM"
#define CMD_MONITOR			"MONITOR"

#if defined(WL_SUPPORT_AUTO_CHANNEL)
#define CMD_GET_BEST_CHANNELS	"GET_BEST_CHANNELS"
#endif /* WL_SUPPORT_AUTO_CHANNEL */

#if defined(CUSTOM_PLATFORM_NV_TEGRA)
#define CMD_SETMIRACAST 	"SETMIRACAST"
#define CMD_ASSOCRESPIE 	"ASSOCRESPIE"
#define CMD_RXRATESTATS        "RXRATESTATS"
#endif /* defined(CUSTOM_PLATFORM_NV_TEGRA) */

#define CMD_KEEP_ALIVE		"KEEPALIVE"

/* CCX Private Commands */
#ifdef BCMCCX
#define CMD_GETCCKM_RN		"get cckm_rn"
#define CMD_SETCCKM_KRK		"set cckm_krk"
#define CMD_GET_ASSOC_RES_IES	"get assoc_res_ies"
#endif

#ifdef PNO_SUPPORT
#define CMD_PNOSSIDCLR_SET	"PNOSSIDCLR"
#define CMD_PNOSETUP_SET	"PNOSETUP "
#define CMD_PNOENABLE_SET	"PNOFORCE"
#define CMD_PNODEBUG_SET	"PNODEBUG"
#define CMD_WLS_BATCHING	"WLS_BATCHING"
#endif /* PNO_SUPPORT */

#define CMD_OKC_SET_PMK		"SET_PMK"
#define CMD_OKC_ENABLE		"OKC_ENABLE"

#define	CMD_HAPD_MAC_FILTER	"HAPD_MAC_FILTER"

#ifdef WLFBT
#define CMD_GET_FTKEY      "GET_FTKEY"
#endif

#ifdef WLAIBSS
#define CMD_SETIBSSTXFAILEVENT		"SETIBSSTXFAILEVENT"
#define CMD_GET_IBSS_PEER_INFO		"GETIBSSPEERINFO"
#define CMD_GET_IBSS_PEER_INFO_ALL	"GETIBSSPEERINFOALL"
#define CMD_SETIBSSROUTETABLE		"SETIBSSROUTETABLE"
#define CMD_SETIBSSAMPDU			"SETIBSSAMPDU"
#define CMD_SETIBSSANTENNAMODE		"SETIBSSANTENNAMODE"
#endif /* WLAIBSS */

#define CMD_ROAM_OFFLOAD			"SETROAMOFFLOAD"
#define CMD_ROAM_OFFLOAD_APLIST		"SETROAMOFFLAPLIST"
#define CMD_GET_LINK_STATUS			"GETLINKSTATUS"

#ifdef P2PRESP_WFDIE_SRC
#define CMD_P2P_SET_WFDIE_RESP      "P2P_SET_WFDIE_RESP"
#define CMD_P2P_GET_WFDIE_RESP      "P2P_GET_WFDIE_RESP"
#endif /* P2PRESP_WFDIE_SRC */

/* related with CMD_GET_LINK_STATUS */
#define WL_ANDROID_LINK_VHT					0x01
#define WL_ANDROID_LINK_MIMO					0x02
#define WL_ANDROID_LINK_AP_VHT_SUPPORT		0x04
#define WL_ANDROID_LINK_AP_MIMO_SUPPORT	0x08

/* miracast related definition */
#define MIRACAST_MODE_OFF	0
#define MIRACAST_MODE_SOURCE	1
#define MIRACAST_MODE_SINK	2

#ifndef MIRACAST_AMPDU_SIZE
#define MIRACAST_AMPDU_SIZE	8
#endif

#ifndef MIRACAST_MCHAN_ALGO
#define MIRACAST_MCHAN_ALGO     1
#endif

#ifndef MIRACAST_MCHAN_BW
#define MIRACAST_MCHAN_BW       25
#endif

#ifdef CONNECTION_STATISTICS
#define CMD_GET_CONNECTION_STATS	"GET_CONNECTION_STATS"

struct connection_stats {
	u32 txframe;
	u32 txbyte;
	u32 txerror;
	u32 rxframe;
	u32 rxbyte;
	u32 txfail;
	u32 txretry;
	u32 txretrie;
	u32 txrts;
	u32 txnocts;
	u32 txexptime;
	u32 txrate;
	u8	chan_idle;
};
#endif /* CONNECTION_STATISTICS */

static LIST_HEAD(miracast_resume_list);
#ifdef WL_CFG80211
static u8 miracast_cur_mode;
#endif

struct io_cfg {
	s8 *iovar;
	s32 param;
	u32 ioctl;
	void *arg;
	u32 len;
	struct list_head list;
};

typedef struct _android_wifi_priv_cmd {
	char *buf;
	int used_len;
	int total_len;
} android_wifi_priv_cmd;

#ifdef CONFIG_COMPAT
typedef struct _compat_android_wifi_priv_cmd {
	compat_caddr_t buf;
	int used_len;
	int total_len;
} compat_android_wifi_priv_cmd;
#endif /* CONFIG_COMPAT */

#if defined(BCMFW_ROAM_ENABLE)
#define CMD_SET_ROAMPREF	"SET_ROAMPREF"

#define MAX_NUM_SUITES		10
#define WIDTH_AKM_SUITE		8
#define JOIN_PREF_RSSI_LEN		0x02
#define JOIN_PREF_RSSI_SIZE		4	/* RSSI pref header size in bytes */
#define JOIN_PREF_WPA_HDR_SIZE		4 /* WPA pref header size in bytes */
#define JOIN_PREF_WPA_TUPLE_SIZE	12	/* Tuple size in bytes */
#define JOIN_PREF_MAX_WPA_TUPLES	16
#define MAX_BUF_SIZE		(JOIN_PREF_RSSI_SIZE + JOIN_PREF_WPA_HDR_SIZE +	\
				           (JOIN_PREF_WPA_TUPLE_SIZE * JOIN_PREF_MAX_WPA_TUPLES))
#endif /* BCMFW_ROAM_ENABLE */

#ifdef WL_GENL
static s32 wl_genl_handle_msg(struct sk_buff *skb, struct genl_info *info);
static int wl_genl_init(void);
static int wl_genl_deinit(void);

extern struct net init_net;
/* attribute policy: defines which attribute has which type (e.g int, char * etc)
 * possible values defined in net/netlink.h
 */
static struct nla_policy wl_genl_policy[BCM_GENL_ATTR_MAX + 1] = {
	[BCM_GENL_ATTR_STRING] = { .type = NLA_NUL_STRING },
	[BCM_GENL_ATTR_MSG] = { .type = NLA_BINARY },
};

#define WL_GENL_VER 1
/* family definition */
static struct genl_family wl_genl_family = {
	.id = GENL_ID_GENERATE,    /* Genetlink would generate the ID */
	.hdrsize = 0,
	.name = "bcm-genl",        /* Netlink I/F for Android */
	.version = WL_GENL_VER,     /* Version Number */
	.maxattr = BCM_GENL_ATTR_MAX,
};

/* commands: mapping between the command enumeration and the actual function */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0))
struct genl_ops wl_genl_ops[] = {
	{
	.cmd = BCM_GENL_CMD_MSG,
	.flags = 0,
	.policy = wl_genl_policy,
	.doit = wl_genl_handle_msg,
	.dumpit = NULL,
	},
};
#else
struct genl_ops wl_genl_ops = {
	.cmd = BCM_GENL_CMD_MSG,
	.flags = 0,
	.policy = wl_genl_policy,
	.doit = wl_genl_handle_msg,
	.dumpit = NULL,

};
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0) */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0))
static struct genl_multicast_group wl_genl_mcast[] = {
	 { .name = "bcm-genl-mcast", },
};
#else
static struct genl_multicast_group wl_genl_mcast = {
	.id = GENL_ID_GENERATE,    /* Genetlink would generate the ID */
	.name = "bcm-genl-mcast",
};
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0) */
#endif /* WL_GENL */

/**
 * Extern function declarations (TODO: move them to dhd_linux.h)
 */
int dhd_net_bus_devreset(struct net_device *dev, uint8 flag);
int dhd_dev_init_ioctl(struct net_device *dev);
#ifdef WL_CFG80211
int wl_cfg80211_get_p2p_dev_addr(struct net_device *net, struct ether_addr *p2pdev_addr);
int wl_cfg80211_set_btcoex_dhcp(struct net_device *dev, dhd_pub_t *dhd, char *command);
#else
int wl_cfg80211_get_p2p_dev_addr(struct net_device *net, struct ether_addr *p2pdev_addr)
{ return 0; }
int wl_cfg80211_set_p2p_noa(struct net_device *net, char* buf, int len)
{ return 0; }
int wl_cfg80211_get_p2p_noa(struct net_device *net, char* buf, int len)
{ return 0; }
int wl_cfg80211_set_p2p_ps(struct net_device *net, char* buf, int len)
{ return 0; }
#endif /* WL_CFG80211 */


#ifdef ENABLE_4335BT_WAR
extern int bcm_bt_lock(int cookie);
extern void bcm_bt_unlock(int cookie);
static int lock_cookie_wifi = 'W' | 'i'<<8 | 'F'<<16 | 'i'<<24;	/* cookie is "WiFi" */
#endif /* ENABLE_4335BT_WAR */

extern bool ap_fw_loaded;
extern char iface_name[IFNAMSIZ];

/**
 * Local (static) functions and variables
 */

/* Initialize g_wifi_on to 1 so dhd_bus_start will be called for the first
 * time (only) in dhd_open, subsequential wifi on will be handled by
 * wl_android_wifi_on
 */
int g_wifi_on = TRUE;

/**
 * Local (static) function definitions
 */
static int wl_android_get_link_speed(struct net_device *net, char *command, int total_len)
{
	int link_speed;
	int bytes_written;
	int error;

	error = wldev_get_link_speed(net, &link_speed);
	if (error)
		return -1;

	/* Convert Kbps to Android Mbps */
	link_speed = link_speed / 1000;
	bytes_written = snprintf(command, total_len, "LinkSpeed %d", link_speed);
	ANDROID_INFO(("%s: command result is %s\n", __FUNCTION__, command));
	return bytes_written;
}

static int wl_android_get_rssi(struct net_device *net, char *command, int total_len)
{
	wlc_ssid_t ssid = {0};
	int rssi;
	int bytes_written = 0;
	int error;

	error = wldev_get_rssi(net, &rssi);
	if (error)
		return -1;
#if defined(RSSIOFFSET)
	rssi = wl_update_rssi_offset(net, rssi);
#endif

	error = wldev_get_ssid(net, &ssid);
	if (error)
		return -1;
	if ((ssid.SSID_len == 0) || (ssid.SSID_len > DOT11_MAX_SSID_LEN)) {
		ANDROID_ERROR(("%s: wldev_get_ssid failed\n", __FUNCTION__));
	} else {
		memcpy(command, ssid.SSID, ssid.SSID_len);
		bytes_written = ssid.SSID_len;
	}
	bytes_written += snprintf(&command[bytes_written], total_len, " rssi %d", rssi);
	ANDROID_INFO(("%s: command result is %s (%d)\n", __FUNCTION__, command, bytes_written));
	return bytes_written;
}

static int wl_android_set_suspendopt(struct net_device *dev, char *command, int total_len)
{
	int suspend_flag;
	int ret_now;
	int ret = 0;

	suspend_flag = *(command + strlen(CMD_SETSUSPENDOPT) + 1) - '0';

	if (suspend_flag != 0)
		suspend_flag = 1;
	ret_now = net_os_set_suspend_disable(dev, suspend_flag);

	if (ret_now != suspend_flag) {
		if (!(ret = net_os_set_suspend(dev, ret_now, 1)))
			ANDROID_INFO(("%s: Suspend Flag %d -> %d\n",
				__FUNCTION__, ret_now, suspend_flag));
		else
			ANDROID_ERROR(("%s: failed %d\n", __FUNCTION__, ret));
	}
	return ret;
}

static int wl_android_set_suspendmode(struct net_device *dev, char *command, int total_len)
{
	int ret = 0;

#if !defined(CONFIG_HAS_EARLYSUSPEND) || !defined(DHD_USE_EARLYSUSPEND)
	int suspend_flag;

	suspend_flag = *(command + strlen(CMD_SETSUSPENDMODE) + 1) - '0';
	if (suspend_flag != 0)
		suspend_flag = 1;

	if (!(ret = net_os_set_suspend(dev, suspend_flag, 0)))
		ANDROID_INFO(("%s: Suspend Mode %d\n", __FUNCTION__, suspend_flag));
	else
		ANDROID_ERROR(("%s: failed %d\n", __FUNCTION__, ret));
#endif

	return ret;
}

static int wl_android_get_band(struct net_device *dev, char *command, int total_len)
{
	uint band;
	int bytes_written;
	int error;

	error = wldev_get_band(dev, &band);
	if (error)
		return -1;
	bytes_written = snprintf(command, total_len, "Band %d", band);
	return bytes_written;
}


#ifdef PNO_SUPPORT
#define PNO_PARAM_SIZE 50
#define VALUE_SIZE 50
#define LIMIT_STR_FMT  ("%50s %50s")
static int
wls_parse_batching_cmd(struct net_device *dev, char *command, int total_len)
{
	int err = BCME_OK;
	uint i, tokens;
	char *pos, *pos2, *token, *token2, *delim;
	char param[PNO_PARAM_SIZE], value[VALUE_SIZE];
	struct dhd_pno_batch_params batch_params;
	ANDROID_INFO(("%s: command=%s, len=%d\n", __FUNCTION__, command, total_len));
	if (total_len < strlen(CMD_WLS_BATCHING)) {
		ANDROID_ERROR(("%s argument=%d less min size\n", __FUNCTION__, total_len));
		err = BCME_ERROR;
		goto exit;
	}
	pos = command + strlen(CMD_WLS_BATCHING) + 1;
	memset(&batch_params, 0, sizeof(struct dhd_pno_batch_params));

	if (!strncmp(pos, PNO_BATCHING_SET, strlen(PNO_BATCHING_SET))) {
		pos += strlen(PNO_BATCHING_SET) + 1;
		while ((token = strsep(&pos, PNO_PARAMS_DELIMETER)) != NULL) {
			memset(param, 0, sizeof(param));
			memset(value, 0, sizeof(value));
			if (token == NULL || !*token)
				break;
			if (*token == '\0')
				continue;
			delim = strchr(token, PNO_PARAM_VALUE_DELLIMETER);
			if (delim != NULL)
				*delim = ' ';

			tokens = sscanf(token, LIMIT_STR_FMT, param, value);
			if (!strncmp(param, PNO_PARAM_SCANFREQ, strlen(PNO_PARAM_SCANFREQ))) {
				batch_params.scan_fr = simple_strtol(value, NULL, 0);
				ANDROID_INFO(("scan_freq : %d\n", batch_params.scan_fr));
			} else if (!strncmp(param, PNO_PARAM_BESTN, strlen(PNO_PARAM_BESTN))) {
				batch_params.bestn = simple_strtol(value, NULL, 0);
				ANDROID_INFO(("bestn : %d\n", batch_params.bestn));
			} else if (!strncmp(param, PNO_PARAM_MSCAN, strlen(PNO_PARAM_MSCAN))) {
				batch_params.mscan = simple_strtol(value, NULL, 0);
				ANDROID_INFO(("mscan : %d\n", batch_params.mscan));
			} else if (!strncmp(param, PNO_PARAM_CHANNEL, strlen(PNO_PARAM_CHANNEL))) {
				i = 0;
				pos2 = value;
				tokens = sscanf(value, "<%s>", value);
				if (tokens != 1) {
					err = BCME_ERROR;
					ANDROID_ERROR(("%s : invalid format for channel"
					" <> params\n", __FUNCTION__));
					goto exit;
				}
					while ((token2 = strsep(&pos2,
					PNO_PARAM_CHANNEL_DELIMETER)) != NULL) {
					if (token2 == NULL || !*token2)
						break;
					if (*token2 == '\0')
						continue;
					if (*token2 == 'A' || *token2 == 'B') {
						batch_params.band = (*token2 == 'A')?
							WLC_BAND_5G : WLC_BAND_2G;
						ANDROID_INFO(("band : %s\n",
							(*token2 == 'A')? "A" : "B"));
					} else {
						batch_params.chan_list[i++] =
						simple_strtol(token2, NULL, 0);
						batch_params.nchan++;
						ANDROID_INFO(("channel :%d\n",
						batch_params.chan_list[i-1]));
					}
				 }
			} else if (!strncmp(param, PNO_PARAM_RTT, strlen(PNO_PARAM_RTT))) {
				batch_params.rtt = simple_strtol(value, NULL, 0);
				ANDROID_INFO(("rtt : %d\n", batch_params.rtt));
			} else {
				ANDROID_ERROR(("%s : unknown param: %s\n", __FUNCTION__, param));
				err = BCME_ERROR;
				goto exit;
			}
		}
		err = dhd_dev_pno_set_for_batch(dev, &batch_params);
		if (err < 0) {
			ANDROID_ERROR(("failed to configure batch scan\n"));
		} else {
			memset(command, 0, total_len);
			err = sprintf(command, "%d", err);
		}
	} else if (!strncmp(pos, PNO_BATCHING_GET, strlen(PNO_BATCHING_GET))) {
		err = dhd_dev_pno_get_for_batch(dev, command, total_len);
		if (err < 0) {
			ANDROID_ERROR(("failed to getting batching results\n"));
		} else {
			err = strlen(command);
		}
	} else if (!strncmp(pos, PNO_BATCHING_STOP, strlen(PNO_BATCHING_STOP))) {
		err = dhd_dev_pno_stop_for_batch(dev);
		if (err < 0) {
			ANDROID_ERROR(("failed to stop batching scan\n"));
		} else {
			memset(command, 0, total_len);
			err = sprintf(command, "OK");
		}
	} else {
		ANDROID_ERROR(("%s : unknown command\n", __FUNCTION__));
		err = BCME_ERROR;
		goto exit;
	}
exit:
	return err;
}
#ifndef WL_SCHED_SCAN
static int wl_android_set_pno_setup(struct net_device *dev, char *command, int total_len)
{
	wlc_ssid_t ssids_local[MAX_PFN_LIST_COUNT];
	int res = -1;
	int nssid = 0;
	cmd_tlv_t *cmd_tlv_temp;
	char *str_ptr;
	int tlv_size_left;
	int pno_time = 0;
	int pno_repeat = 0;
	int pno_freq_expo_max = 0;

#ifdef PNO_SET_DEBUG
	int i;
	char pno_in_example[] = {
		'P', 'N', 'O', 'S', 'E', 'T', 'U', 'P', ' ',
		'S', '1', '2', '0',
		'S',
		0x05,
		'd', 'l', 'i', 'n', 'k',
		'S',
		0x04,
		'G', 'O', 'O', 'G',
		'T',
		'0', 'B',
		'R',
		'2',
		'M',
		'2',
		0x00
		};
#endif /* PNO_SET_DEBUG */
	ANDROID_INFO(("%s: command=%s, len=%d\n", __FUNCTION__, command, total_len));

	if (total_len < (strlen(CMD_PNOSETUP_SET) + sizeof(cmd_tlv_t))) {
		ANDROID_ERROR(("%s argument=%d less min size\n", __FUNCTION__, total_len));
		goto exit_proc;
	}
#ifdef PNO_SET_DEBUG
	memcpy(command, pno_in_example, sizeof(pno_in_example));
	total_len = sizeof(pno_in_example);
#endif
	str_ptr = command + strlen(CMD_PNOSETUP_SET);
	tlv_size_left = total_len - strlen(CMD_PNOSETUP_SET);

	cmd_tlv_temp = (cmd_tlv_t *)str_ptr;
	memset(ssids_local, 0, sizeof(ssids_local));

	if ((cmd_tlv_temp->prefix == PNO_TLV_PREFIX) &&
		(cmd_tlv_temp->version == PNO_TLV_VERSION) &&
		(cmd_tlv_temp->subtype == PNO_TLV_SUBTYPE_LEGACY_PNO)) {

		str_ptr += sizeof(cmd_tlv_t);
		tlv_size_left -= sizeof(cmd_tlv_t);

		if ((nssid = wl_iw_parse_ssid_list_tlv(&str_ptr, ssids_local,
			MAX_PFN_LIST_COUNT, &tlv_size_left)) <= 0) {
			ANDROID_ERROR(("SSID is not presented or corrupted ret=%d\n", nssid));
			goto exit_proc;
		} else {
			if ((str_ptr[0] != PNO_TLV_TYPE_TIME) || (tlv_size_left <= 1)) {
				ANDROID_ERROR(("%s scan duration corrupted field size %d\n",
					__FUNCTION__, tlv_size_left));
				goto exit_proc;
			}
			str_ptr++;
			pno_time = simple_strtoul(str_ptr, &str_ptr, 16);
			ANDROID_INFO(("%s: pno_time=%d\n", __FUNCTION__, pno_time));

			if (str_ptr[0] != 0) {
				if ((str_ptr[0] != PNO_TLV_FREQ_REPEAT)) {
					ANDROID_ERROR(("%s pno repeat : corrupted field\n",
						__FUNCTION__));
					goto exit_proc;
				}
				str_ptr++;
				pno_repeat = simple_strtoul(str_ptr, &str_ptr, 16);
				ANDROID_INFO(("%s :got pno_repeat=%d\n", __FUNCTION__, pno_repeat));
				if (str_ptr[0] != PNO_TLV_FREQ_EXPO_MAX) {
					ANDROID_ERROR(("%s FREQ_EXPO_MAX corrupted field size\n",
						__FUNCTION__));
					goto exit_proc;
				}
				str_ptr++;
				pno_freq_expo_max = simple_strtoul(str_ptr, &str_ptr, 16);
				ANDROID_INFO(("%s: pno_freq_expo_max=%d\n",
					__FUNCTION__, pno_freq_expo_max));
			}
		}
	} else {
		ANDROID_ERROR(("%s get wrong TLV command\n", __FUNCTION__));
		goto exit_proc;
	}

	res = dhd_dev_pno_set_for_ssid(dev, ssids_local, nssid, pno_time, pno_repeat,
		pno_freq_expo_max, NULL, 0);
exit_proc:
	return res;
}
#endif /* !WL_SCHED_SCAN */
#endif /* PNO_SUPPORT  */

static int wl_android_get_p2p_dev_addr(struct net_device *ndev, char *command, int total_len)
{
	int ret;
	int bytes_written = 0;

	ret = wl_cfg80211_get_p2p_dev_addr(ndev, (struct ether_addr*)command);
	if (ret)
		return 0;
	bytes_written = sizeof(struct ether_addr);
	return bytes_written;
}

#ifdef BCMCCX
static int wl_android_get_cckm_rn(struct net_device *dev, char *command)
{
	int error, rn;

	ANDROID_TRACE(("%s:wl_android_get_cckm_rn\n", dev->name));

	error = wldev_iovar_getint(dev, "cckm_rn", &rn);
	if (unlikely(error)) {
		ANDROID_ERROR(("wl_android_get_cckm_rn error (%d)\n", error));
		return -1;
	}
	memcpy(command, &rn, sizeof(int));

	return sizeof(int);
}

static int wl_android_set_cckm_krk(struct net_device *dev, char *command)
{
	int error;
	unsigned char key[16];
	static char iovar_buf[WLC_IOCTL_MEDLEN];

	ANDROID_TRACE(("%s: wl_iw_set_cckm_krk\n", dev->name));

	memset(iovar_buf, 0, sizeof(iovar_buf));
	memcpy(key, command+strlen("set cckm_krk")+1, 16);

	error = wldev_iovar_setbuf(dev, "cckm_krk", key, sizeof(key),
		iovar_buf, WLC_IOCTL_MEDLEN, NULL);
	if (unlikely(error))
	{
		ANDROID_ERROR((" cckm_krk set error (%d)\n", error));
		return -1;
	}
	return 0;
}

static int wl_android_get_assoc_res_ies(struct net_device *dev, char *command)
{
	int error;
	u8 buf[WL_ASSOC_INFO_MAX];
	wl_assoc_info_t assoc_info;
	u32 resp_ies_len = 0;
	int bytes_written = 0;

	ANDROID_TRACE(("%s: wl_iw_get_assoc_res_ies\n", dev->name));

	error = wldev_iovar_getbuf(dev, "assoc_info", NULL, 0, buf, WL_ASSOC_INFO_MAX, NULL);
	if (unlikely(error)) {
		ANDROID_ERROR(("could not get assoc info (%d)\n", error));
		return -1;
	}

	memcpy(&assoc_info, buf, sizeof(wl_assoc_info_t));
	assoc_info.req_len = htod32(assoc_info.req_len);
	assoc_info.resp_len = htod32(assoc_info.resp_len);
	assoc_info.flags = htod32(assoc_info.flags);

	if (assoc_info.resp_len) {
		resp_ies_len = assoc_info.resp_len - sizeof(struct dot11_assoc_resp);
	}

	/* first 4 bytes are ie len */
	memcpy(command, &resp_ies_len, sizeof(u32));
	bytes_written = sizeof(u32);

	/* get the association resp IE's if there are any */
	if (resp_ies_len) {
		error = wldev_iovar_getbuf(dev, "assoc_resp_ies", NULL, 0,
			buf, WL_ASSOC_INFO_MAX, NULL);
		if (unlikely(error)) {
			ANDROID_ERROR(("could not get assoc resp_ies (%d)\n", error));
			return -1;
		}

		memcpy(command+sizeof(u32), buf, resp_ies_len);
		bytes_written += resp_ies_len;
	}
	return bytes_written;
}

#endif /* BCMCCX */

int
wl_android_set_ap_mac_list(struct net_device *dev, int macmode, struct maclist *maclist)
{
	int i, j, match;
	int ret	= 0;
	char mac_buf[MAX_NUM_OF_ASSOCLIST *
		sizeof(struct ether_addr) + sizeof(uint)] = {0};
	struct maclist *assoc_maclist = (struct maclist *)mac_buf;

	/* set filtering mode */
	if ((ret = wldev_ioctl(dev, WLC_SET_MACMODE, &macmode, sizeof(macmode), true)) != 0) {
		ANDROID_ERROR(("%s : WLC_SET_MACMODE error=%d\n", __FUNCTION__, ret));
		return ret;
	}
	if (macmode != MACLIST_MODE_DISABLED) {
		/* set the MAC filter list */
		if ((ret = wldev_ioctl(dev, WLC_SET_MACLIST, maclist,
			sizeof(int) + sizeof(struct ether_addr) * maclist->count, true)) != 0) {
			ANDROID_ERROR(("%s : WLC_SET_MACLIST error=%d\n", __FUNCTION__, ret));
			return ret;
		}
		/* get the current list of associated STAs */
		assoc_maclist->count = MAX_NUM_OF_ASSOCLIST;
		if ((ret = wldev_ioctl(dev, WLC_GET_ASSOCLIST, assoc_maclist,
			sizeof(mac_buf), false)) != 0) {
			ANDROID_ERROR(("%s : WLC_GET_ASSOCLIST error=%d\n", __FUNCTION__, ret));
			return ret;
		}
		/* do we have any STA associated?  */
		if (assoc_maclist->count) {
			/* iterate each associated STA */
			for (i = 0; i < assoc_maclist->count; i++) {
				match = 0;
				/* compare with each entry */
				for (j = 0; j < maclist->count; j++) {
					ANDROID_INFO(("%s : associated="MACDBG " list="MACDBG "\n",
					__FUNCTION__, MAC2STRDBG(assoc_maclist->ea[i].octet),
					MAC2STRDBG(maclist->ea[j].octet)));
					if (memcmp(assoc_maclist->ea[i].octet,
						maclist->ea[j].octet, ETHER_ADDR_LEN) == 0) {
						match = 1;
						break;
					}
				}
				/* do conditional deauth */
				/*   "if not in the allow list" or "if in the deny list" */
				if ((macmode == MACLIST_MODE_ALLOW && !match) ||
					(macmode == MACLIST_MODE_DENY && match)) {
					scb_val_t scbval;

					scbval.val = htod32(1);
					memcpy(&scbval.ea, &assoc_maclist->ea[i],
						ETHER_ADDR_LEN);
					if ((ret = wldev_ioctl(dev,
						WLC_SCB_DEAUTHENTICATE_FOR_REASON,
						&scbval, sizeof(scb_val_t), true)) != 0)
						ANDROID_ERROR(("%s WLC_SCB_DEAUTHENTICATE error=%d\n",
							__FUNCTION__, ret));
				}
			}
		}
	}
	return ret;
}

/*
 * HAPD_MAC_FILTER mac_mode mac_cnt mac_addr1 mac_addr2
 *
 */
static int
wl_android_set_mac_address_filter(struct net_device *dev, const char* str)
{
	int i;
	int ret = 0;
	int macnum = 0;
	int macmode = MACLIST_MODE_DISABLED;
	struct maclist *list;
	char eabuf[ETHER_ADDR_STR_LEN];

	/* string should look like below (macmode/macnum/maclist) */
	/*   1 2 00:11:22:33:44:55 00:11:22:33:44:ff  */

	/* get the MAC filter mode */
	macmode = bcm_atoi(strsep((char**)&str, " "));

	if (macmode < MACLIST_MODE_DISABLED || macmode > MACLIST_MODE_ALLOW) {
		ANDROID_ERROR(("%s : invalid macmode %d\n", __FUNCTION__, macmode));
		return -1;
	}

	macnum = bcm_atoi(strsep((char**)&str, " "));
	if (macnum < 0 || macnum > MAX_NUM_MAC_FILT) {
		ANDROID_ERROR(("%s : invalid number of MAC address entries %d\n",
			__FUNCTION__, macnum));
		return -1;
	}
	/* allocate memory for the MAC list */
	list = (struct maclist*)kmalloc(sizeof(int) +
		sizeof(struct ether_addr) * macnum, GFP_KERNEL);
	if (!list) {
		ANDROID_ERROR(("%s : failed to allocate memory\n", __FUNCTION__));
		return -1;
	}
	/* prepare the MAC list */
	list->count = htod32(macnum);
	bzero((char *)eabuf, ETHER_ADDR_STR_LEN);
	for (i = 0; i < list->count; i++) {
		strncpy(eabuf, strsep((char**)&str, " "), ETHER_ADDR_STR_LEN - 1);
		if (!(ret = bcm_ether_atoe(eabuf, &list->ea[i]))) {
			ANDROID_ERROR(("%s : mac parsing err index=%d, addr=%s\n",
				__FUNCTION__, i, eabuf));
			list->count--;
			break;
		}
		ANDROID_INFO(("%s : %d/%d MACADDR=%s", __FUNCTION__, i, list->count, eabuf));
	}
	/* set the list */
	if ((ret = wl_android_set_ap_mac_list(dev, macmode, list)) != 0)
		ANDROID_ERROR(("%s : Setting MAC list failed error=%d\n", __FUNCTION__, ret));

	kfree(list);

	return 0;
}

/**
 * Global function definitions (declared in wl_android.h)
 */

int wl_android_wifi_on(struct net_device *dev)
{
	int ret = 0;
#ifdef CONFIG_MACH_UNIVERSAL5433
	int retry;
	/* Do not retry old revision Helsinki Prime */
	if (!check_rev()) {
		retry = 1;
	} else {
		retry = POWERUP_MAX_RETRY;
	}
#else
	int retry = POWERUP_MAX_RETRY;
#endif /* CONFIG_MACH_UNIVERSAL5433 */

	if (!dev) {
		ANDROID_ERROR(("%s: dev is null\n", __FUNCTION__));
		return -EINVAL;
	}

	printf("%s in 1\n", __FUNCTION__);
	dhd_net_if_lock(dev);
	printf("%s in 2: g_wifi_on=%d\n", __FUNCTION__, g_wifi_on);
	if (!g_wifi_on) {
		do {
			dhd_net_wifi_platform_set_power(dev, TRUE, WIFI_TURNON_DELAY);
#ifdef BCMSDIO
			ret = dhd_net_bus_resume(dev, 0);
#endif /* BCMSDIO */
#ifdef BCMPCIE
			ret = dhd_net_bus_devreset(dev, FALSE);
#endif /* BCMPCIE */
			if (ret == 0)
				break;
			ANDROID_ERROR(("\nfailed to power up wifi chip, retry again (%d left) **\n\n",
				retry));
#ifdef BCMPCIE
			dhd_net_bus_devreset(dev, TRUE);
#endif /* BCMPCIE */
			dhd_net_wifi_platform_set_power(dev, FALSE, WIFI_TURNOFF_DELAY);
		} while (retry-- > 0);
		if (ret != 0) {
			ANDROID_ERROR(("\nfailed to power up wifi chip, max retry reached **\n\n"));
			goto exit;
		}
#ifdef BCMSDIO
		ret = dhd_net_bus_devreset(dev, FALSE);
		if (ret)
			goto err;
		dhd_net_bus_resume(dev, 1);
#endif /* BCMSDIO */

#ifndef BCMPCIE
		if (!ret) {
			if (dhd_dev_init_ioctl(dev) < 0) {
				ret = -EFAULT;
				goto err;
			}
		}
#endif /* !BCMPCIE */
		g_wifi_on = TRUE;
	}

exit:
	printf("%s: Success\n", __FUNCTION__);
	dhd_net_if_unlock(dev);
	return ret;

#ifdef BCMSDIO
err:
	dhd_net_bus_devreset(dev, TRUE);
	dhd_net_bus_suspend(dev);
	dhd_net_wifi_platform_set_power(dev, FALSE, WIFI_TURNOFF_DELAY);
	printf("%s: Failed\n", __FUNCTION__);
	dhd_net_if_unlock(dev);
	return ret;
#endif
}

int wl_android_wifi_off(struct net_device *dev)
{
	int ret = 0;

	if (!dev) {
		ANDROID_ERROR(("%s: dev is null\n", __FUNCTION__));
		return -EINVAL;
	}

	printf("%s in 1\n", __FUNCTION__);
	dhd_net_if_lock(dev);
	printf("%s in 2: g_wifi_on=%d\n", __FUNCTION__, g_wifi_on);
	if (g_wifi_on) {
#if defined(BCMSDIO) || defined(BCMPCIE)
		ret = dhd_net_bus_devreset(dev, TRUE);
#ifdef BCMSDIO
		dhd_net_bus_suspend(dev);
#endif /* BCMSDIO */
#endif /* BCMSDIO || BCMPCIE */
		dhd_net_wifi_platform_set_power(dev, FALSE, WIFI_TURNOFF_DELAY);
		g_wifi_on = FALSE;
	}
	printf("%s out\n", __FUNCTION__);
	dhd_net_if_unlock(dev);

	return ret;
}

static int wl_android_set_fwpath(struct net_device *net, char *command, int total_len)
{
	if ((strlen(command) - strlen(CMD_SETFWPATH)) > MOD_PARAM_PATHLEN)
		return -1;
	return dhd_net_set_fw_path(net, command + strlen(CMD_SETFWPATH) + 1);
}

#ifdef CONNECTION_STATISTICS
static int
wl_chanim_stats(struct net_device *dev, u8 *chan_idle)
{
	int err;
	wl_chanim_stats_t *list;
	/* Parameter _and_ returned buffer of chanim_stats. */
	wl_chanim_stats_t param;
	u8 result[WLC_IOCTL_SMLEN];
	chanim_stats_t *stats;

	memset(&param, 0, sizeof(param));
	memset(result, 0, sizeof(result));

	param.buflen = htod32(sizeof(wl_chanim_stats_t));
	param.count = htod32(WL_CHANIM_COUNT_ONE);

	if ((err = wldev_iovar_getbuf(dev, "chanim_stats", (char*)&param, sizeof(wl_chanim_stats_t),
		(char*)result, sizeof(result), 0)) < 0) {
		ANDROID_ERROR(("Failed to get chanim results %d \n", err));
		return err;
	}

	list = (wl_chanim_stats_t*)result;

	list->buflen = dtoh32(list->buflen);
	list->version = dtoh32(list->version);
	list->count = dtoh32(list->count);

	if (list->buflen == 0) {
		list->version = 0;
		list->count = 0;
	} else if (list->version != WL_CHANIM_STATS_VERSION) {
		ANDROID_ERROR(("Sorry, firmware has wl_chanim_stats version %d "
			"but driver supports only version %d.\n",
				list->version, WL_CHANIM_STATS_VERSION));
		list->buflen = 0;
		list->count = 0;
	}

	stats = list->stats;
	stats->glitchcnt = dtoh32(stats->glitchcnt);
	stats->badplcp = dtoh32(stats->badplcp);
	stats->chanspec = dtoh16(stats->chanspec);
	stats->timestamp = dtoh32(stats->timestamp);
	stats->chan_idle = dtoh32(stats->chan_idle);

	ANDROID_INFO(("chanspec: 0x%4x glitch: %d badplcp: %d idle: %d timestamp: %d\n",
		stats->chanspec, stats->glitchcnt, stats->badplcp, stats->chan_idle,
		stats->timestamp));

	*chan_idle = stats->chan_idle;

	return (err);
}

static int
wl_android_get_connection_stats(struct net_device *dev, char *command, int total_len)
{
	wl_cnt_t* cnt = NULL;
	int link_speed = 0;
	struct connection_stats *output;
	unsigned int bufsize = 0;
	int bytes_written = 0;
	int ret = 0;

	ANDROID_INFO(("%s: enter Get Connection Stats\n", __FUNCTION__));

	if (total_len <= 0) {
		ANDROID_ERROR(("%s: invalid buffer size %d\n", __FUNCTION__, total_len));
		goto error;
	}

	bufsize = total_len;
	if (bufsize < sizeof(struct connection_stats)) {
		ANDROID_ERROR(("%s: not enough buffer size, provided=%u, requires=%u\n",
			__FUNCTION__, bufsize,
			sizeof(struct connection_stats)));
		goto error;
	}

	if ((cnt = kmalloc(sizeof(*cnt), GFP_KERNEL)) == NULL) {
		ANDROID_ERROR(("kmalloc failed\n"));
		return -1;
	}
	memset(cnt, 0, sizeof(*cnt));

	ret = wldev_iovar_getbuf(dev, "counters", NULL, 0, (char *)cnt, sizeof(wl_cnt_t), NULL);
	if (ret) {
		ANDROID_ERROR(("%s: wldev_iovar_getbuf() failed, ret=%d\n",
			__FUNCTION__, ret));
		goto error;
	}

	if (dtoh16(cnt->version) > WL_CNT_T_VERSION) {
		ANDROID_ERROR(("%s: incorrect version of wl_cnt_t, expected=%u got=%u\n",
			__FUNCTION__,  WL_CNT_T_VERSION, cnt->version));
		goto error;
	}

	/* link_speed is in kbps */
	ret = wldev_get_link_speed(dev, &link_speed);
	if (ret || link_speed < 0) {
		ANDROID_ERROR(("%s: wldev_get_link_speed() failed, ret=%d, speed=%d\n",
			__FUNCTION__, ret, link_speed));
		goto error;
	}

	output = (struct connection_stats *)command;
	output->txframe   = dtoh32(cnt->txframe);
	output->txbyte    = dtoh32(cnt->txbyte);
	output->txerror   = dtoh32(cnt->txerror);
	output->rxframe   = dtoh32(cnt->rxframe);
	output->rxbyte    = dtoh32(cnt->rxbyte);
	output->txfail    = dtoh32(cnt->txfail);
	output->txretry   = dtoh32(cnt->txretry);
	output->txretrie  = dtoh32(cnt->txretrie);
	output->txrts     = dtoh32(cnt->txrts);
	output->txnocts   = dtoh32(cnt->txnocts);
	output->txexptime = dtoh32(cnt->txexptime);
	output->txrate    = link_speed;

	/* Channel idle ratio. */
	if (wl_chanim_stats(dev, &(output->chan_idle)) < 0) {
		output->chan_idle = 0;
	};

	kfree(cnt);

	bytes_written = sizeof(struct connection_stats);
	return bytes_written;

error:
	if (cnt) {
		kfree(cnt);
	}
	return -1;
}
#endif /* CONNECTION_STATISTICS */

static int
wl_android_set_pmk(struct net_device *dev, char *command, int total_len)
{
	uchar pmk[33];
	int error = 0;
	char smbuf[WLC_IOCTL_SMLEN];
#ifdef OKC_DEBUG
	int i = 0;
#endif

	bzero(pmk, sizeof(pmk));
	memcpy((char *)pmk, command + strlen("SET_PMK "), 32);
	error = wldev_iovar_setbuf(dev, "okc_info_pmk", pmk, 32, smbuf, sizeof(smbuf), NULL);
	if (error) {
		ANDROID_ERROR(("Failed to set PMK for OKC, error = %d\n", error));
	}
#ifdef OKC_DEBUG
	ANDROID_ERROR(("PMK is "));
	for (i = 0; i < 32; i++)
		ANDROID_ERROR(("%02X ", pmk[i]));

	ANDROID_ERROR(("\n"));
#endif
	return error;
}

static int
wl_android_okc_enable(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	char okc_enable = 0;

	okc_enable = command[strlen(CMD_OKC_ENABLE) + 1] - '0';
	error = wldev_iovar_setint(dev, "okc_enable", okc_enable);
	if (error) {
		ANDROID_ERROR(("Failed to %s OKC, error = %d\n",
			okc_enable ? "enable" : "disable", error));
	}

	wldev_iovar_setint(dev, "ccx_enable", 0);

	return error;
}



int wl_android_set_roam_mode(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	int mode = 0;

	if (sscanf(command, "%*s %d", &mode) != 1) {
		ANDROID_ERROR(("%s: Failed to get Parameter\n", __FUNCTION__));
		return -1;
	}

	error = wldev_iovar_setint(dev, "roam_off", mode);
	if (error) {
		ANDROID_ERROR(("%s: Failed to set roaming Mode %d, error = %d\n",
		__FUNCTION__, mode, error));
		return -1;
	}
	else
		ANDROID_ERROR(("%s: succeeded to set roaming Mode %d, error = %d\n",
		__FUNCTION__, mode, error));
	return 0;
}

#ifdef WL_CFG80211
int wl_android_set_ibss_beacon_ouidata(struct net_device *dev, char *command, int total_len)
{
	char ie_buf[VNDR_IE_MAX_LEN];
	char *ioctl_buf = NULL;
	char hex[] = "XX";
	char *pcmd = NULL;
	int ielen = 0, datalen = 0, idx = 0, tot_len = 0;
	vndr_ie_setbuf_t *vndr_ie = NULL;
	s32 iecount;
	uint32 pktflag;
	u16 kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	s32 err = BCME_OK;

	/* Check the VSIE (Vendor Specific IE) which was added.
	 *  If exist then send IOVAR to delete it
	 */
	if (wl_cfg80211_ibss_vsie_delete(dev) != BCME_OK) {
		return -EINVAL;
	}

	pcmd = command + strlen(CMD_SETIBSSBEACONOUIDATA) + 1;
	for (idx = 0; idx < DOT11_OUI_LEN; idx++) {
		hex[0] = *pcmd++;
		hex[1] = *pcmd++;
		ie_buf[idx] =  (uint8)simple_strtoul(hex, NULL, 16);
	}
	pcmd++;
	while ((*pcmd != '\0') && (idx < VNDR_IE_MAX_LEN)) {
		hex[0] = *pcmd++;
		hex[1] = *pcmd++;
		ie_buf[idx++] =  (uint8)simple_strtoul(hex, NULL, 16);
		datalen++;
	}
	tot_len = sizeof(vndr_ie_setbuf_t) + (datalen - 1);
	vndr_ie = (vndr_ie_setbuf_t *) kzalloc(tot_len, kflags);
	if (!vndr_ie) {
		ANDROID_ERROR(("IE memory alloc failed\n"));
		return -ENOMEM;
	}
	/* Copy the vndr_ie SET command ("add"/"del") to the buffer */
	strncpy(vndr_ie->cmd, "add", VNDR_IE_CMD_LEN - 1);
	vndr_ie->cmd[VNDR_IE_CMD_LEN - 1] = '\0';

	/* Set the IE count - the buffer contains only 1 IE */
	iecount = htod32(1);
	memcpy((void *)&vndr_ie->vndr_ie_buffer.iecount, &iecount, sizeof(s32));

	/* Set packet flag to indicate that BEACON's will contain this IE */
	pktflag = htod32(VNDR_IE_BEACON_FLAG | VNDR_IE_PRBRSP_FLAG);
	memcpy((void *)&vndr_ie->vndr_ie_buffer.vndr_ie_list[0].pktflag, &pktflag,
		sizeof(u32));
	/* Set the IE ID */
	vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.id = (uchar) DOT11_MNG_PROPR_ID;

	memcpy(&vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.oui, &ie_buf,
		DOT11_OUI_LEN);
	memcpy(&vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.data,
		&ie_buf[DOT11_OUI_LEN], datalen);

	ielen = DOT11_OUI_LEN + datalen;
	vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.len = (uchar) ielen;

	ioctl_buf = kmalloc(WLC_IOCTL_MEDLEN, GFP_KERNEL);
	if (!ioctl_buf) {
		ANDROID_ERROR(("ioctl memory alloc failed\n"));
		if (vndr_ie) {
			kfree(vndr_ie);
		}
		return -ENOMEM;
	}
	memset(ioctl_buf, 0, WLC_IOCTL_MEDLEN);	/* init the buffer */
	err = wldev_iovar_setbuf(dev, "ie", vndr_ie, tot_len, ioctl_buf, WLC_IOCTL_MEDLEN, NULL);


	if (err != BCME_OK) {
		err = -EINVAL;
		if (vndr_ie) {
			kfree(vndr_ie);
		}
	}
	else {
		/* do NOT free 'vndr_ie' for the next process */
		wl_cfg80211_ibss_vsie_set_buffer(vndr_ie, tot_len);
	}

	if (ioctl_buf) {
		kfree(ioctl_buf);
	}

	return err;
}
#endif

#if defined(BCMFW_ROAM_ENABLE)
static int
wl_android_set_roampref(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	char smbuf[WLC_IOCTL_SMLEN];
	uint8 buf[MAX_BUF_SIZE];
	uint8 *pref = buf;
	char *pcmd;
	int num_ucipher_suites = 0;
	int num_akm_suites = 0;
	wpa_suite_t ucipher_suites[MAX_NUM_SUITES];
	wpa_suite_t akm_suites[MAX_NUM_SUITES];
	int num_tuples = 0;
	int total_bytes = 0;
	int total_len_left;
	int i, j;
	char hex[] = "XX";

	pcmd = command + strlen(CMD_SET_ROAMPREF) + 1;
	total_len_left = total_len - strlen(CMD_SET_ROAMPREF) + 1;

	num_akm_suites = simple_strtoul(pcmd, NULL, 16);
	/* Increment for number of AKM suites field + space */
	pcmd += 3;
	total_len_left -= 3;

	/* check to make sure pcmd does not overrun */
	if (total_len_left < (num_akm_suites * WIDTH_AKM_SUITE))
		return -1;

	memset(buf, 0, sizeof(buf));
	memset(akm_suites, 0, sizeof(akm_suites));
	memset(ucipher_suites, 0, sizeof(ucipher_suites));

	/* Save the AKM suites passed in the command */
	for (i = 0; i < num_akm_suites; i++) {
		/* Store the MSB first, as required by join_pref */
		for (j = 0; j < 4; j++) {
			hex[0] = *pcmd++;
			hex[1] = *pcmd++;
			buf[j] = (uint8)simple_strtoul(hex, NULL, 16);
		}
		memcpy((uint8 *)&akm_suites[i], buf, sizeof(uint32));
	}

	total_len_left -= (num_akm_suites * WIDTH_AKM_SUITE);
	num_ucipher_suites = simple_strtoul(pcmd, NULL, 16);
	/* Increment for number of cipher suites field + space */
	pcmd += 3;
	total_len_left -= 3;

	if (total_len_left < (num_ucipher_suites * WIDTH_AKM_SUITE))
		return -1;

	/* Save the cipher suites passed in the command */
	for (i = 0; i < num_ucipher_suites; i++) {
		/* Store the MSB first, as required by join_pref */
		for (j = 0; j < 4; j++) {
			hex[0] = *pcmd++;
			hex[1] = *pcmd++;
			buf[j] = (uint8)simple_strtoul(hex, NULL, 16);
		}
		memcpy((uint8 *)&ucipher_suites[i], buf, sizeof(uint32));
	}

	/* Join preference for RSSI
	 * Type	  : 1 byte (0x01)
	 * Length : 1 byte (0x02)
	 * Value  : 2 bytes	(reserved)
	 */
	*pref++ = WL_JOIN_PREF_RSSI;
	*pref++ = JOIN_PREF_RSSI_LEN;
	*pref++ = 0;
	*pref++ = 0;

	/* Join preference for WPA
	 * Type	  : 1 byte (0x02)
	 * Length : 1 byte (not used)
	 * Value  : (variable length)
	 *		reserved: 1 byte
	 *      count	: 1 byte (no of tuples)
	 *		Tuple1	: 12 bytes
	 *			akm[4]
	 *			ucipher[4]
	 *			mcipher[4]
	 *		Tuple2	: 12 bytes
	 *		Tuplen	: 12 bytes
	 */
	num_tuples = num_akm_suites * num_ucipher_suites;
	if (num_tuples != 0) {
		if (num_tuples <= JOIN_PREF_MAX_WPA_TUPLES) {
			*pref++ = WL_JOIN_PREF_WPA;
			*pref++ = 0;
			*pref++ = 0;
			*pref++ = (uint8)num_tuples;
			total_bytes = JOIN_PREF_RSSI_SIZE + JOIN_PREF_WPA_HDR_SIZE +
				(JOIN_PREF_WPA_TUPLE_SIZE * num_tuples);
		} else {
			ANDROID_ERROR(("%s: Too many wpa configs for join_pref \n", __FUNCTION__));
			return -1;
		}
	} else {
		/* No WPA config, configure only RSSI preference */
		total_bytes = JOIN_PREF_RSSI_SIZE;
	}

	/* akm-ucipher-mcipher tuples in the format required for join_pref */
	for (i = 0; i < num_ucipher_suites; i++) {
		for (j = 0; j < num_akm_suites; j++) {
			memcpy(pref, (uint8 *)&akm_suites[j], WPA_SUITE_LEN);
			pref += WPA_SUITE_LEN;
			memcpy(pref, (uint8 *)&ucipher_suites[i], WPA_SUITE_LEN);
			pref += WPA_SUITE_LEN;
			/* Set to 0 to match any available multicast cipher */
			memset(pref, 0, WPA_SUITE_LEN);
			pref += WPA_SUITE_LEN;
		}
	}

	prhex("join pref", (uint8 *)buf, total_bytes);
	error = wldev_iovar_setbuf(dev, "join_pref", buf, total_bytes, smbuf, sizeof(smbuf), NULL);
	if (error) {
		ANDROID_ERROR(("Failed to set join_pref, error = %d\n", error));
	}
	return error;
}
#endif /* defined(BCMFW_ROAM_ENABLE */

#ifdef WL_CFG80211
static int
wl_android_iolist_add(struct net_device *dev, struct list_head *head, struct io_cfg *config)
{
	struct io_cfg *resume_cfg;
	s32 ret;

	resume_cfg = kzalloc(sizeof(struct io_cfg), GFP_KERNEL);
	if (!resume_cfg)
		return -ENOMEM;

	if (config->iovar) {
		ret = wldev_iovar_getint(dev, config->iovar, &resume_cfg->param);
		if (ret) {
			ANDROID_ERROR(("%s: Failed to get current %s value\n",
				__FUNCTION__, config->iovar));
			goto error;
		}

		ret = wldev_iovar_setint(dev, config->iovar, config->param);
		if (ret) {
			ANDROID_ERROR(("%s: Failed to set %s to %d\n", __FUNCTION__,
				config->iovar, config->param));
			goto error;
		}

		resume_cfg->iovar = config->iovar;
	} else {
		resume_cfg->arg = kzalloc(config->len, GFP_KERNEL);
		if (!resume_cfg->arg) {
			ret = -ENOMEM;
			goto error;
		}
		ret = wldev_ioctl(dev, config->ioctl, resume_cfg->arg, config->len, false);
		if (ret) {
			ANDROID_ERROR(("%s: Failed to get ioctl %d\n", __FUNCTION__,
				config->ioctl));
			goto error;
		}
		ret = wldev_ioctl(dev, config->ioctl + 1, config->arg, config->len, true);
		if (ret) {
			ANDROID_ERROR(("%s: Failed to set %s to %d\n", __FUNCTION__,
				config->iovar, config->param));
			goto error;
		}
		if (config->ioctl + 1 == WLC_SET_PM)
			wl_cfg80211_update_power_mode(dev);
		resume_cfg->ioctl = config->ioctl;
		resume_cfg->len = config->len;
	}

	list_add(&resume_cfg->list, head);

	return 0;
error:
	kfree(resume_cfg->arg);
	kfree(resume_cfg);
	return ret;
}

static void
wl_android_iolist_resume(struct net_device *dev, struct list_head *head)
{
	struct io_cfg *config;
	struct list_head *cur, *q;
	s32 ret = 0;

	list_for_each_safe(cur, q, head) {
		config = list_entry(cur, struct io_cfg, list);
		if (config->iovar) {
			if (!ret)
				ret = wldev_iovar_setint(dev, config->iovar,
					config->param);
		} else {
			if (!ret)
				ret = wldev_ioctl(dev, config->ioctl + 1,
					config->arg, config->len, true);
			if (config->ioctl + 1 == WLC_SET_PM)
				wl_cfg80211_update_power_mode(dev);
			kfree(config->arg);
		}
		list_del(cur);
		kfree(config);
	}
}

static int
wl_android_set_miracast(struct net_device *dev, char *command, int total_len)
{
	int mode, val;
	int ret = 0;
	struct io_cfg config;

	if (sscanf(command, "%*s %d", &mode) != 1) {
		ANDROID_ERROR(("%s: Failed to get Parameter\n", __FUNCTION__));
		return -1;
	}

	ANDROID_INFO(("%s: enter miracast mode %d\n", __FUNCTION__, mode));

	if (miracast_cur_mode == mode)
		return 0;

	wl_android_iolist_resume(dev, &miracast_resume_list);
	miracast_cur_mode = MIRACAST_MODE_OFF;

	switch (mode) {
	case MIRACAST_MODE_SOURCE:
		/* setting mchan_algo to platform specific value */
		config.iovar = "mchan_algo";

		ret = wldev_ioctl(dev, WLC_GET_BCNPRD, &val, sizeof(int), false);
		if (!ret && val > 100) {
			config.param = 0;
			ANDROID_ERROR(("%s: Connected station's beacon interval: "
				"%d and set mchan_algo to %d \n",
				__FUNCTION__, val, config.param));
		}
		else {
			config.param = MIRACAST_MCHAN_ALGO;
		}
		ret = wl_android_iolist_add(dev, &miracast_resume_list, &config);
		if (ret)
			goto resume;

		/* setting mchan_bw to platform specific value */
		config.iovar = "mchan_bw";
		config.param = MIRACAST_MCHAN_BW;
		ret = wl_android_iolist_add(dev, &miracast_resume_list, &config);
		if (ret)
			goto resume;

		/* setting apmdu to platform specific value */
		config.iovar = "ampdu_mpdu";
		config.param = MIRACAST_AMPDU_SIZE;
		ret = wl_android_iolist_add(dev, &miracast_resume_list, &config);
		if (ret)
			goto resume;
		/* FALLTROUGH */
		/* Source mode shares most configurations with sink mode.
		 * Fall through here to avoid code duplication
		 */
	case MIRACAST_MODE_SINK:
		/* disable internal roaming */
		config.iovar = "roam_off";
		config.param = 1;
		ret = wl_android_iolist_add(dev, &miracast_resume_list, &config);
		if (ret)
			goto resume;
		/* tunr off pm */
		val = 0;
		config.iovar = NULL;
		config.ioctl = WLC_GET_PM;
		config.arg = &val;
		config.len = sizeof(int);
		ret = wl_android_iolist_add(dev, &miracast_resume_list, &config);
		if (ret)
			goto resume;

		break;
	case MIRACAST_MODE_OFF:
	default:
		break;
	}
	miracast_cur_mode = mode;

	return 0;

resume:
	ANDROID_ERROR(("%s: turnoff miracast mode because of err%d\n", __FUNCTION__, ret));
	wl_android_iolist_resume(dev, &miracast_resume_list);
	return ret;
}
#endif

#define NETLINK_OXYGEN     30
#define AIBSS_BEACON_TIMEOUT	10

static struct sock *nl_sk = NULL;

static void wl_netlink_recv(struct sk_buff *skb)
{
	ANDROID_ERROR(("netlink_recv called\n"));
}

static int wl_netlink_init(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	struct netlink_kernel_cfg cfg = {
		.input	= wl_netlink_recv,
	};
#endif

	if (nl_sk != NULL) {
		ANDROID_ERROR(("nl_sk already exist\n"));
		return BCME_ERROR;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
	nl_sk = netlink_kernel_create(&init_net, NETLINK_OXYGEN,
		0, wl_netlink_recv, NULL, THIS_MODULE);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0))
	nl_sk = netlink_kernel_create(&init_net, NETLINK_OXYGEN, THIS_MODULE, &cfg);
#else
	nl_sk = netlink_kernel_create(&init_net, NETLINK_OXYGEN, &cfg);
#endif

	if (nl_sk == NULL) {
		ANDROID_ERROR(("nl_sk is not ready\n"));
		return BCME_ERROR;
	}

	return BCME_OK;
}

static void wl_netlink_deinit(void)
{
	if (nl_sk) {
		netlink_kernel_release(nl_sk);
		nl_sk = NULL;
	}
}

s32
wl_netlink_send_msg(int pid, int type, int seq, void *data, size_t size)
{
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh = NULL;
	int ret = -1;

	if (nl_sk == NULL) {
		ANDROID_ERROR(("nl_sk was not initialized\n"));
		goto nlmsg_failure;
	}

	skb = alloc_skb(NLMSG_SPACE(size), GFP_ATOMIC);
	if (skb == NULL) {
		ANDROID_ERROR(("failed to allocate memory\n"));
		goto nlmsg_failure;
	}

	nlh = nlmsg_put(skb, 0, 0, 0, size, 0);
	if (nlh == NULL) {
		ANDROID_ERROR(("failed to build nlmsg, skb_tailroom:%d, nlmsg_total_size:%d\n",
			skb_tailroom(skb), nlmsg_total_size(size)));
		dev_kfree_skb(skb);
		goto nlmsg_failure;
	}

	memcpy(nlmsg_data(nlh), data, size);
	nlh->nlmsg_seq = seq;
	nlh->nlmsg_type = type;

	/* netlink_unicast() takes ownership of the skb and frees it itself. */
	ret = netlink_unicast(nl_sk, skb, pid, 0);
	ANDROID_TRACE(("netlink_unicast() pid=%d, ret=%d\n", pid, ret));

nlmsg_failure:
	return ret;
}

#ifdef WLAIBSS
static int wl_android_set_ibss_txfail_event(struct net_device *dev, char *command, int total_len)
{
	int err = 0;
	int retry = 0;
	int pid = 0;
	aibss_txfail_config_t txfail_config = {0, 0, 0, 0};
	char smbuf[WLC_IOCTL_SMLEN];

	if (sscanf(command, CMD_SETIBSSTXFAILEVENT " %d %d", &retry, &pid) <= 0) {
		ANDROID_ERROR(("Failed to get Parameter from : %s\n", command));
		return -1;
	}

	/* set pid, and if the event was happened, let's send a notification through netlink */
	wl_cfg80211_set_txfail_pid(pid);

	/* If retry value is 0, it disables the functionality for TX Fail. */
	if (retry > 0) {
		txfail_config.max_tx_retry = retry;
		txfail_config.bcn_timeout = 0;	/* 0 : disable tx fail from beacon */
	}
	txfail_config.version = AIBSS_TXFAIL_CONFIG_VER_0;
	txfail_config.len = sizeof(txfail_config);

	err = wldev_iovar_setbuf(dev, "aibss_txfail_config", (void *) &txfail_config,
		sizeof(aibss_txfail_config_t), smbuf, WLC_IOCTL_SMLEN, NULL);
	ANDROID_TRACE(("retry=%d, pid=%d, err=%d\n", retry, pid, err));

	return ((err == 0)?total_len:err);
}

static int wl_android_get_ibss_peer_info(struct net_device *dev, char *command,
	int total_len, bool bAll)
{
	int error;
	int bytes_written = 0;
	void *buf = NULL;
	bss_peer_list_info_t peer_list_info;
	bss_peer_info_t *peer_info;
	int i;
	bool found = false;
	struct ether_addr mac_ea;

	ANDROID_TRACE(("get ibss peer info(%s)\n", bAll?"true":"false"));

	if (!bAll) {
		if (sscanf (command, "GETIBSSPEERINFO %02x:%02x:%02x:%02x:%02x:%02x",
			(unsigned int *)&mac_ea.octet[0], (unsigned int *)&mac_ea.octet[1],
			(unsigned int *)&mac_ea.octet[2], (unsigned int *)&mac_ea.octet[3],
			(unsigned int *)&mac_ea.octet[4], (unsigned int *)&mac_ea.octet[5]) != 6) {
			ANDROID_TRACE(("invalid MAC address\n"));
			return -1;
		}
	}

	if ((buf = kmalloc(WLC_IOCTL_MAXLEN, GFP_KERNEL)) == NULL) {
		ANDROID_ERROR(("kmalloc failed\n"));
		return -1;
	}

	error = wldev_iovar_getbuf(dev, "bss_peer_info", NULL, 0, buf, WLC_IOCTL_MAXLEN, NULL);
	if (unlikely(error)) {
		ANDROID_ERROR(("could not get ibss peer info (%d)\n", error));
		kfree(buf);
		return -1;
	}

	memcpy(&peer_list_info, buf, sizeof(peer_list_info));
	peer_list_info.version = htod16(peer_list_info.version);
	peer_list_info.bss_peer_info_len = htod16(peer_list_info.bss_peer_info_len);
	peer_list_info.count = htod32(peer_list_info.count);

	ANDROID_TRACE(("ver:%d, len:%d, count:%d\n", peer_list_info.version,
		peer_list_info.bss_peer_info_len, peer_list_info.count));

	if (peer_list_info.count > 0) {
		if (bAll)
			bytes_written += sprintf(&command[bytes_written], "%u ",
				peer_list_info.count);

		peer_info = (bss_peer_info_t *) ((void *)buf + BSS_PEER_LIST_INFO_FIXED_LEN);


		for (i = 0; i < peer_list_info.count; i++) {

			ANDROID_TRACE(("index:%d rssi:%d, tx:%u, rx:%u\n", i, peer_info->rssi,
				peer_info->tx_rate, peer_info->rx_rate));

			if (!bAll &&
				memcmp(&mac_ea, &peer_info->ea, sizeof(struct ether_addr)) == 0) {
				found = true;
			}

			if (bAll || found) {
				bytes_written += sprintf(&command[bytes_written], MACF,
					ETHER_TO_MACF(peer_info->ea));
				bytes_written += sprintf(&command[bytes_written], " %u %d ",
					peer_info->tx_rate/1000, peer_info->rssi);
			}

			if (found)
				break;

			peer_info = (bss_peer_info_t *)((void *)peer_info+sizeof(bss_peer_info_t));
		}
	}
	else {
		ANDROID_ERROR(("could not get ibss peer info : no item\n"));
	}
	bytes_written += sprintf(&command[bytes_written], "%s", "\0");

	ANDROID_TRACE(("command(%u):%s\n", total_len, command));
	ANDROID_TRACE(("bytes_written:%d\n", bytes_written));

	kfree(buf);
	return bytes_written;
}

int wl_android_set_ibss_routetable(struct net_device *dev, char *command, int total_len)
{

	char *pcmd = command;
	char *str = NULL;

	ibss_route_tbl_t *route_tbl = NULL;
	char *ioctl_buf = NULL;
	u16 kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	s32 err = BCME_OK;
	uint32 route_tbl_len;
	uint32 entries;
	char *endptr;
	uint32 i = 0;
	struct ipv4_addr  dipaddr;
	struct ether_addr ea;

	route_tbl_len = sizeof(ibss_route_tbl_t) +
		(MAX_IBSS_ROUTE_TBL_ENTRY - 1) * sizeof(ibss_route_entry_t);
	route_tbl = (ibss_route_tbl_t *)kzalloc(route_tbl_len, kflags);
	if (!route_tbl) {
		ANDROID_ERROR(("Route TBL alloc failed\n"));
		return -ENOMEM;
	}
	ioctl_buf = kzalloc(WLC_IOCTL_MEDLEN, GFP_KERNEL);
	if (!ioctl_buf) {
		ANDROID_ERROR(("ioctl memory alloc failed\n"));
		if (route_tbl) {
			kfree(route_tbl);
		}
		return -ENOMEM;
	}
	memset(ioctl_buf, 0, WLC_IOCTL_MEDLEN);

	/* drop command */
	str = bcmstrtok(&pcmd, " ", NULL);

	/* get count */
	str = bcmstrtok(&pcmd, " ",  NULL);
	if (!str) {
		ANDROID_ERROR(("Invalid number parameter %s\n", str));
		err = -EINVAL;
		goto exit;
	}
	entries = bcm_strtoul(str, &endptr, 0);
	if (*endptr != '\0') {
		ANDROID_ERROR(("Invalid number parameter %s\n", str));
		err = -EINVAL;
		goto exit;
	}
	ANDROID_INFO(("Routing table count:%d\n", entries));
	route_tbl->num_entry = entries;

	for (i = 0; i < entries; i++) {
		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str || !bcm_atoipv4(str, &dipaddr)) {
			ANDROID_ERROR(("Invalid ip string %s\n", str));
			err = -EINVAL;
			goto exit;
		}


		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str || !bcm_ether_atoe(str, &ea)) {
			ANDROID_ERROR(("Invalid ethernet string %s\n", str));
			err = -EINVAL;
			goto exit;
		}
		bcopy(&dipaddr, &route_tbl->route_entry[i].ipv4_addr, IPV4_ADDR_LEN);
		bcopy(&ea, &route_tbl->route_entry[i].nexthop, ETHER_ADDR_LEN);
	}

	route_tbl_len = sizeof(ibss_route_tbl_t) +
		((!entries?0:(entries - 1)) * sizeof(ibss_route_entry_t));
	err = wldev_iovar_setbuf(dev, "ibss_route_tbl",
		route_tbl, route_tbl_len, ioctl_buf, WLC_IOCTL_MEDLEN, NULL);
	if (err != BCME_OK) {
		ANDROID_ERROR(("Fail to set iovar %d\n", err));
		err = -EINVAL;
	}

exit:
	if (route_tbl)
		kfree(route_tbl);
	if (ioctl_buf)
		kfree(ioctl_buf);
	return err;

}

int
wl_android_set_ibss_ampdu(struct net_device *dev, char *command, int total_len)
{
	char *pcmd = command;
	char *str = NULL, *endptr = NULL;
	struct ampdu_aggr aggr;
	char smbuf[WLC_IOCTL_SMLEN];
	int idx;
	int err = 0;
	int wme_AC2PRIO[AC_COUNT][2] = {
		{PRIO_8021D_VO, PRIO_8021D_NC},		/* AC_VO - 3 */
		{PRIO_8021D_CL, PRIO_8021D_VI},		/* AC_VI - 2 */
		{PRIO_8021D_BK, PRIO_8021D_NONE},	/* AC_BK - 1 */
		{PRIO_8021D_BE, PRIO_8021D_EE}};	/* AC_BE - 0 */

	ANDROID_TRACE(("set ibss ampdu:%s\n", command));

	memset(&aggr, 0, sizeof(aggr));
	/* Cofigure all priorities */
	aggr.conf_TID_bmap = NBITMASK(NUMPRIO);

	/* acquire parameters */
	/* drop command */
	str = bcmstrtok(&pcmd, " ", NULL);

	for (idx = 0; idx < AC_COUNT; idx++) {
		bool on;
		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str) {
			ANDROID_ERROR(("Invalid parameter : %s\n", pcmd));
			return -EINVAL;
		}
		on = bcm_strtoul(str, &endptr, 0) ? TRUE : FALSE;
		if (*endptr != '\0') {
			ANDROID_ERROR(("Invalid number format %s\n", str));
			return -EINVAL;
		}
		if (on) {
			setbit(&aggr.enab_TID_bmap, wme_AC2PRIO[idx][0]);
			setbit(&aggr.enab_TID_bmap, wme_AC2PRIO[idx][1]);
		}
	}

	err = wldev_iovar_setbuf(dev, "ampdu_txaggr", (void *)&aggr,
	sizeof(aggr), smbuf, WLC_IOCTL_SMLEN, NULL);

	return ((err == 0) ? total_len : err);
}

int wl_android_set_ibss_antenna(struct net_device *dev, char *command, int total_len)
{
	char *pcmd = command;
	char *str = NULL;
	int txchain, rxchain;
	int err = 0;

	ANDROID_TRACE(("set ibss antenna:%s\n", command));

	/* acquire parameters */
	/* drop command */
	str = bcmstrtok(&pcmd, " ", NULL);

	/* TX chain */
	str = bcmstrtok(&pcmd, " ", NULL);
	if (!str) {
		ANDROID_ERROR(("Invalid parameter : %s\n", pcmd));
		return -EINVAL;
	}
	txchain = bcm_atoi(str);

	/* RX chain */
	str = bcmstrtok(&pcmd, " ", NULL);
	if (!str) {
		ANDROID_ERROR(("Invalid parameter : %s\n", pcmd));
		return -EINVAL;
	}
	rxchain = bcm_atoi(str);

	err = wldev_iovar_setint(dev, "txchain", txchain);
	if (err != 0)
		return err;
	err = wldev_iovar_setint(dev, "rxchain", rxchain);
	return ((err == 0)?total_len:err);
}
#endif /* WLAIBSS */

int wl_keep_alive_set(struct net_device *dev, char* extra, int total_len)
{
	char 				buf[256];
	const char 			*str;
	wl_mkeep_alive_pkt_t	mkeep_alive_pkt;
	wl_mkeep_alive_pkt_t	*mkeep_alive_pktp;
	int					buf_len;
	int					str_len;
	int res 				= -1;
	uint period_msec = 0;

	if (extra == NULL)
	{
		 ANDROID_ERROR(("%s: extra is NULL\n", __FUNCTION__));
		 return -1;
	}
	if (sscanf(extra, "%d", &period_msec) != 1)
	{
		 ANDROID_ERROR(("%s: sscanf error. check period_msec value\n", __FUNCTION__));
		 return -EINVAL;
	}
	ANDROID_ERROR(("%s: period_msec is %d\n", __FUNCTION__, period_msec));

	memset(&mkeep_alive_pkt, 0, sizeof(wl_mkeep_alive_pkt_t));

	str = "mkeep_alive";
	str_len = strlen(str);
	strncpy(buf, str, str_len);
	buf[ str_len ] = '\0';
	mkeep_alive_pktp = (wl_mkeep_alive_pkt_t *) (buf + str_len + 1);
	mkeep_alive_pkt.period_msec = period_msec;
	buf_len = str_len + 1;
	mkeep_alive_pkt.version = htod16(WL_MKEEP_ALIVE_VERSION);
	mkeep_alive_pkt.length = htod16(WL_MKEEP_ALIVE_FIXED_LEN);

	/* Setup keep alive zero for null packet generation */
	mkeep_alive_pkt.keep_alive_id = 0;
	mkeep_alive_pkt.len_bytes = 0;
	buf_len += WL_MKEEP_ALIVE_FIXED_LEN;
	/* Keep-alive attributes are set in local	variable (mkeep_alive_pkt), and
	 * then memcpy'ed into buffer (mkeep_alive_pktp) since there is no
	 * guarantee that the buffer is properly aligned.
	 */
	memcpy((char *)mkeep_alive_pktp, &mkeep_alive_pkt, WL_MKEEP_ALIVE_FIXED_LEN);

	if ((res = wldev_ioctl(dev, WLC_SET_VAR, buf, buf_len, TRUE)) < 0)
	{
		ANDROID_ERROR(("%s:keep_alive set failed. res[%d]\n", __FUNCTION__, res));
	}
	else
	{
		ANDROID_ERROR(("%s:keep_alive set ok. res[%d]\n", __FUNCTION__, res));
	}

	return res;
}


static const char *
get_string_by_separator(char *result, int result_len, const char *src, char separator)
{
	char *end = result + result_len - 1;
	while ((result != end) && (*src != separator) && (*src)) {
		*result++ = *src++;
	}
	*result = 0;
	if (*src == separator)
		++src;
	return src;
}

int
wl_android_set_roam_offload_bssid_list(struct net_device *dev, const char *cmd)
{
	char sbuf[32];
	int i, cnt, size, err, ioctl_buf_len;
	roamoffl_bssid_list_t *bssid_list;
	const char *str = cmd;
	char *ioctl_buf;

	str = get_string_by_separator(sbuf, 32, str, ',');
	cnt = bcm_atoi(sbuf);
	cnt = MIN(cnt, MAX_ROAMOFFL_BSSID_NUM);
	size = sizeof(int) + sizeof(struct ether_addr) * cnt;
	ANDROID_ERROR(("ROAM OFFLOAD BSSID LIST %d BSSIDs, size %d\n", cnt, size));
	bssid_list = kmalloc(size, GFP_KERNEL);
	if (bssid_list == NULL) {
		ANDROID_ERROR(("%s: memory alloc for bssid list(%d) failed\n",
			__FUNCTION__, size));
		return -ENOMEM;
	}
	ioctl_buf_len = size + 64;
	ioctl_buf = kmalloc(ioctl_buf_len, GFP_KERNEL);
	if (ioctl_buf == NULL) {
		ANDROID_ERROR(("%s: memory alloc for ioctl_buf(%d) failed\n",
			__FUNCTION__, ioctl_buf_len));
		kfree(bssid_list);
		return -ENOMEM;
	}

	for (i = 0; i < cnt; i++) {
		str = get_string_by_separator(sbuf, 32, str, ',');
		if (bcm_ether_atoe(sbuf, &bssid_list->bssid[i]) == 0) {
			ANDROID_ERROR(("%s: Invalid station MAC Address!!!\n", __FUNCTION__));
			kfree(bssid_list);
			kfree(ioctl_buf);
			return -1;
		}
	}

	bssid_list->cnt = cnt;
	err = wldev_iovar_setbuf(dev, "roamoffl_bssid_list",
		bssid_list, size, ioctl_buf, ioctl_buf_len, NULL);
	kfree(bssid_list);
	kfree(ioctl_buf);

	return err;
}

#ifdef P2PRESP_WFDIE_SRC
static int wl_android_get_wfdie_resp(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	int bytes_written = 0;
	int only_resp_wfdsrc = 0;

	error = wldev_iovar_getint(dev, "p2p_only_resp_wfdsrc", &only_resp_wfdsrc);
	if (error) {
		ANDROID_ERROR(("%s: Failed to get the mode for only_resp_wfdsrc, error = %d\n",
			__FUNCTION__, error));
		return -1;
	}

	bytes_written = snprintf(command, total_len, "%s %d",
		CMD_P2P_GET_WFDIE_RESP, only_resp_wfdsrc);

	return bytes_written;
}

static int wl_android_set_wfdie_resp(struct net_device *dev, int only_resp_wfdsrc)
{
	int error = 0;

	error = wldev_iovar_setint(dev, "p2p_only_resp_wfdsrc", only_resp_wfdsrc);
	if (error) {
		ANDROID_ERROR(("%s: Failed to set only_resp_wfdsrc %d, error = %d\n",
			__FUNCTION__, only_resp_wfdsrc, error));
		return -1;
	}

	return 0;
}
#endif /* P2PRESP_WFDIE_SRC */

static int wl_android_get_link_status(struct net_device *dev, char *command,
	int total_len)
{
	int bytes_written, error, result = 0, single_stream, stf = -1, i, nss = 0, mcs_map;
	uint32 rspec;
	uint encode, rate, txexp;
	struct wl_bss_info *bi;
	int datalen = sizeof(uint32) + sizeof(wl_bss_info_t);
	char buf[datalen];

	/* get BSS information */
	*(u32 *) buf = htod32(datalen);
	error = wldev_ioctl(dev, WLC_GET_BSS_INFO, (void *)buf, datalen, false);
	if (unlikely(error)) {
		ANDROID_ERROR(("Could not get bss info %d\n", error));
		return -1;
	}

	bi = (struct wl_bss_info *) (buf + sizeof(uint32));

	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		if (bi->BSSID.octet[i] > 0) {
			break;
		}
	}

	if (i == ETHER_ADDR_LEN) {
		ANDROID_TRACE(("No BSSID\n"));
		return -1;
	}

	/* check VHT capability at beacon */
	if (bi->vht_cap) {
		if (CHSPEC_IS5G(bi->chanspec)) {
			result |= WL_ANDROID_LINK_AP_VHT_SUPPORT;
		}
	}

	/* get a rspec (radio spectrum) rate */
	error = wldev_iovar_getint(dev, "nrate", &rspec);
	if (unlikely(error) || rspec == 0) {
		ANDROID_ERROR(("get link status error (%d)\n", error));
		return -1;
	}

	encode = (rspec & WL_RSPEC_ENCODING_MASK);
	rate = (rspec & WL_RSPEC_RATE_MASK);
	txexp = (rspec & WL_RSPEC_TXEXP_MASK) >> WL_RSPEC_TXEXP_SHIFT;

	switch (encode) {
	case WL_RSPEC_ENCODE_HT:
		/* check Rx MCS Map for HT */
		for (i = 0; i < MAX_STREAMS_SUPPORTED; i++) {
			int8 bitmap = 0xFF;
			if (i == MAX_STREAMS_SUPPORTED-1) {
				bitmap = 0x7F;
			}
			if (bi->basic_mcs[i] & bitmap) {
				nss++;
			}
		}
		break;
	case WL_RSPEC_ENCODE_VHT:
		/* check Rx MCS Map for VHT */
		for (i = 1; i <= VHT_CAP_MCS_MAP_NSS_MAX; i++) {
			mcs_map = VHT_MCS_MAP_GET_MCS_PER_SS(i, dtoh16(bi->vht_rxmcsmap));
			if (mcs_map != VHT_CAP_MCS_MAP_NONE) {
				nss++;
			}
		}
		break;
	}

	/* check MIMO capability with nss in beacon */
	if (nss > 1) {
		result |= WL_ANDROID_LINK_AP_MIMO_SUPPORT;
	}

	single_stream = (encode == WL_RSPEC_ENCODE_RATE) ||
		((encode == WL_RSPEC_ENCODE_HT) && rate < 8) ||
		((encode == WL_RSPEC_ENCODE_VHT) &&
		((rspec & WL_RSPEC_VHT_NSS_MASK) >> WL_RSPEC_VHT_NSS_SHIFT) == 1);

	if (txexp == 0) {
		if ((rspec & WL_RSPEC_STBC) && single_stream) {
			stf = OLD_NRATE_STF_STBC;
		} else {
			stf = (single_stream) ? OLD_NRATE_STF_SISO : OLD_NRATE_STF_SDM;
		}
	} else if (txexp == 1 && single_stream) {
		stf = OLD_NRATE_STF_CDD;
	}

	/* check 11ac (VHT) */
	if (encode == WL_RSPEC_ENCODE_VHT) {
		if (CHSPEC_IS5G(bi->chanspec)) {
			result |= WL_ANDROID_LINK_VHT;
		}
	}

	/* check MIMO */
	if (result & WL_ANDROID_LINK_AP_MIMO_SUPPORT) {
		switch (stf) {
		case OLD_NRATE_STF_SISO:
			break;
		case OLD_NRATE_STF_CDD:
		case OLD_NRATE_STF_STBC:
			result |= WL_ANDROID_LINK_MIMO;
			break;
		case OLD_NRATE_STF_SDM:
			if (!single_stream) {
				result |= WL_ANDROID_LINK_MIMO;
			}
			break;
		}
	}

	ANDROID_TRACE(("%s:result=%d, stf=%d, single_stream=%d, mcs map=%d\n",
		__FUNCTION__, result, stf, single_stream, nss));

	bytes_written = sprintf(command, "%s %d", CMD_GET_LINK_STATUS, result);

	return bytes_written;
}

int
wl_android_get_channel(
struct net_device *dev, char* command, int total_len)
{
	int ret;
	channel_info_t ci;
	int bytes_written = 0;

	if (!(ret = wldev_ioctl(dev, WLC_GET_CHANNEL, &ci, sizeof(channel_info_t), FALSE))) {
		ANDROID_TRACE(("hw_channel %d\n", ci.hw_channel));
		ANDROID_TRACE(("target_channel %d\n", ci.target_channel));
		ANDROID_TRACE(("scan_channel %d\n", ci.scan_channel));
		bytes_written = snprintf(command, sizeof(channel_info_t)+2, "channel %d", ci.hw_channel);
		ANDROID_TRACE(("%s: command result is %s\n", __FUNCTION__, command));
	}

	return bytes_written;
}

int
wl_android_set_roam_trigger(
struct net_device *dev, char* command, int total_len)
{
	int ret = 0;
	int roam_trigger[2];

	sscanf(command, "%*s %10d", &roam_trigger[0]);
	roam_trigger[1] = WLC_BAND_ALL;

	ret = wldev_ioctl(dev, WLC_SET_ROAM_TRIGGER, roam_trigger, sizeof(roam_trigger), 1);
	if (ret)
		ANDROID_ERROR(("WLC_SET_ROAM_TRIGGER ERROR %d ret=%d\n", roam_trigger[0], ret));

	return ret;
}

int
wl_android_get_roam_trigger(
struct net_device *dev, char *command, int total_len)
{
	int ret;
	int bytes_written;
	int roam_trigger[2] = {0, 0};
	int trigger[2]= {0, 0};

	roam_trigger[1] = WLC_BAND_2G;
	ret = wldev_ioctl(dev, WLC_GET_ROAM_TRIGGER, roam_trigger, sizeof(roam_trigger), 0);
	if (!ret)
		trigger[0] = roam_trigger[0];
	else
		ANDROID_ERROR(("2G WLC_GET_ROAM_TRIGGER ERROR %d ret=%d\n", roam_trigger[0], ret));

	roam_trigger[1] = WLC_BAND_5G;
	ret = wldev_ioctl(dev, WLC_GET_ROAM_TRIGGER, roam_trigger, sizeof(roam_trigger), 0);
	if (!ret)
		trigger[1] = roam_trigger[0];
	else
		ANDROID_ERROR(("5G WLC_GET_ROAM_TRIGGER ERROR %d ret=%d\n", roam_trigger[0], ret));

	ANDROID_TRACE(("roam_trigger %d %d\n", trigger[0], trigger[1]));
	bytes_written = snprintf(command, total_len, "%d %d", trigger[0], trigger[1]);

	return bytes_written;
}

s32
wl_android_get_keep_alive(struct net_device *dev, char *command, int total_len) {

	wl_mkeep_alive_pkt_t *mkeep_alive_pktp;
	int bytes_written = -1;
	int res = -1, len, i = 0;
	char* str = "mkeep_alive";

	ANDROID_TRACE(("%s: command = %s\n", __FUNCTION__, command));

	len = WLC_IOCTL_MEDLEN;
	mkeep_alive_pktp = kmalloc(len, GFP_KERNEL);
	memset(mkeep_alive_pktp, 0, len);
	strcpy((char*)mkeep_alive_pktp, str);

	if ((res = wldev_ioctl(dev, WLC_GET_VAR, mkeep_alive_pktp, len, FALSE))<0) {
		ANDROID_ERROR(("%s: GET mkeep_alive ERROR %d\n", __FUNCTION__, res));
		goto exit;
	} else {
		printf("Id            :%d\n"
			   "Period (msec) :%d\n"
			   "Length        :%d\n"
			   "Packet        :0x",
			   mkeep_alive_pktp->keep_alive_id,
			   dtoh32(mkeep_alive_pktp->period_msec),
			   dtoh16(mkeep_alive_pktp->len_bytes));
		for (i=0; i<mkeep_alive_pktp->len_bytes; i++) {
			printf("%02x", mkeep_alive_pktp->data[i]);
		}
		printf("\n");
	}
	bytes_written = snprintf(command, total_len, "mkeep_alive_period_msec %d ", dtoh32(mkeep_alive_pktp->period_msec));
	bytes_written += snprintf(command+bytes_written, total_len, "0x");
	for (i=0; i<mkeep_alive_pktp->len_bytes; i++) {
		bytes_written += snprintf(command+bytes_written, total_len, "%x", mkeep_alive_pktp->data[i]);
	}
	ANDROID_TRACE(("%s: command result is %s\n", __FUNCTION__, command));

exit:
	kfree(mkeep_alive_pktp);
	return bytes_written;
}

int
wl_android_set_pm(struct net_device *dev,char *command, int total_len)
{
	int pm, ret = -1;

	ANDROID_TRACE(("%s: cmd %s\n", __FUNCTION__, command));

	sscanf(command, "%*s %d", &pm);

	ret = wldev_ioctl(dev, WLC_SET_PM, &pm, sizeof(pm), FALSE);
	if (ret)
		ANDROID_ERROR(("WLC_SET_PM ERROR %d ret=%d\n", pm, ret));

	return ret;
}

int
wl_android_get_pm(struct net_device *dev,char *command, int total_len)
{

	int ret = 0;
	int pm_local;
	char *pm;
	int bytes_written=-1;

	ret = wldev_ioctl(dev, WLC_GET_PM, &pm_local, sizeof(pm_local),FALSE);
	if (!ret) {
		ANDROID_TRACE(("%s: PM = %d\n", __func__, pm_local));
		if (pm_local == PM_OFF)
			pm = "PM_OFF";
		else if(pm_local == PM_MAX)
			pm = "PM_MAX";
		else if(pm_local == PM_FAST)
			pm = "PM_FAST";
		else {
			pm_local = 0;
			pm = "Invalid";
		}
		bytes_written = snprintf(command, total_len, "PM %s", pm);
		ANDROID_TRACE(("%s: command result is %s\n", __FUNCTION__, command));
	}
	return bytes_written;
}

static int
wl_android_set_monitor(struct net_device *dev, char *command, int total_len)
{
	int val;
	int ret = 0;
	int bytes_written;

	sscanf(command, "%*s %d", &val);
	bytes_written = wldev_ioctl(dev, WLC_SET_MONITOR, &val, sizeof(int), 1);
	if (bytes_written)
		ANDROID_ERROR(("WLC_SET_MONITOR ERROR %d ret=%d\n", val, ret));
	return bytes_written;
}

int wl_android_priv_cmd(struct net_device *net, struct ifreq *ifr, int cmd)
{
#define PRIVATE_COMMAND_MAX_LEN	8192
	int ret = 0;
	char *command = NULL;
	int bytes_written = 0;
	android_wifi_priv_cmd priv_cmd;

	net_os_wake_lock(net);

	if (!ifr->ifr_data) {
		ret = -EINVAL;
		goto exit;
	}

#ifdef CONFIG_COMPAT
	if (is_compat_task()) {
		compat_android_wifi_priv_cmd compat_priv_cmd;
		if (copy_from_user(&compat_priv_cmd, ifr->ifr_data,
			sizeof(compat_android_wifi_priv_cmd))) {
			ret = -EFAULT;
			goto exit;

		}
		priv_cmd.buf = compat_ptr(compat_priv_cmd.buf);
		priv_cmd.used_len = compat_priv_cmd.used_len;
		priv_cmd.total_len = compat_priv_cmd.total_len;
	} else
#endif /* CONFIG_COMPAT */
	{
		if (copy_from_user(&priv_cmd, ifr->ifr_data, sizeof(android_wifi_priv_cmd))) {
			ret = -EFAULT;
			goto exit;
		}
	}
	if ((priv_cmd.total_len > PRIVATE_COMMAND_MAX_LEN) || (priv_cmd.total_len < 0)) {
		ANDROID_ERROR(("%s: too long priavte command\n", __FUNCTION__));
		ret = -EINVAL;
		goto exit;
	}
	command = kmalloc((priv_cmd.total_len + 1), GFP_KERNEL);
	if (!command)
	{
		ANDROID_ERROR(("%s: failed to allocate memory\n", __FUNCTION__));
		ret = -ENOMEM;
		goto exit;
	}
	if (copy_from_user(command, priv_cmd.buf, priv_cmd.total_len)) {
		ret = -EFAULT;
		goto exit;
	}
	command[priv_cmd.total_len] = '\0';

	ANDROID_INFO(("%s: Android private cmd \"%s\" on %s\n", __FUNCTION__, command, ifr->ifr_name));

	if (strnicmp(command, CMD_START, strlen(CMD_START)) == 0) {
		ANDROID_INFO(("%s, Received regular START command\n", __FUNCTION__));
		bytes_written = wl_android_wifi_on(net);
	}
	else if (strnicmp(command, CMD_SETFWPATH, strlen(CMD_SETFWPATH)) == 0) {
		bytes_written = wl_android_set_fwpath(net, command, priv_cmd.total_len);
	}

	if (!g_wifi_on) {
		ANDROID_ERROR(("%s: Ignore private cmd \"%s\" - iface %s is down\n",
			__FUNCTION__, command, ifr->ifr_name));
		ret = 0;
		goto exit;
	}

	if (strnicmp(command, CMD_STOP, strlen(CMD_STOP)) == 0) {
		bytes_written = wl_android_wifi_off(net);
	}
	else if (strnicmp(command, CMD_SCAN_ACTIVE, strlen(CMD_SCAN_ACTIVE)) == 0) {
		/* TBD: SCAN-ACTIVE */
	}
	else if (strnicmp(command, CMD_SCAN_PASSIVE, strlen(CMD_SCAN_PASSIVE)) == 0) {
		/* TBD: SCAN-PASSIVE */
	}
	else if (strnicmp(command, CMD_RSSI, strlen(CMD_RSSI)) == 0) {
		bytes_written = wl_android_get_rssi(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_LINKSPEED, strlen(CMD_LINKSPEED)) == 0) {
		bytes_written = wl_android_get_link_speed(net, command, priv_cmd.total_len);
	}
#ifdef PKT_FILTER_SUPPORT
	else if (strnicmp(command, CMD_RXFILTER_START, strlen(CMD_RXFILTER_START)) == 0) {
		bytes_written = net_os_enable_packet_filter(net, 1);
	}
	else if (strnicmp(command, CMD_RXFILTER_STOP, strlen(CMD_RXFILTER_STOP)) == 0) {
		bytes_written = net_os_enable_packet_filter(net, 0);
	}
	else if (strnicmp(command, CMD_RXFILTER_ADD, strlen(CMD_RXFILTER_ADD)) == 0) {
		int filter_num = *(command + strlen(CMD_RXFILTER_ADD) + 1) - '0';
		bytes_written = net_os_rxfilter_add_remove(net, TRUE, filter_num);
	}
	else if (strnicmp(command, CMD_RXFILTER_REMOVE, strlen(CMD_RXFILTER_REMOVE)) == 0) {
		int filter_num = *(command + strlen(CMD_RXFILTER_REMOVE) + 1) - '0';
		bytes_written = net_os_rxfilter_add_remove(net, FALSE, filter_num);
	}
#if defined(CUSTOM_PLATFORM_NV_TEGRA)
	else if (strnicmp(command, CMD_PKT_FILTER_MODE, strlen(CMD_PKT_FILTER_MODE)) == 0) {
		dhd_set_packet_filter_mode(net, &command[strlen(CMD_PKT_FILTER_MODE) + 1]);
	} else if (strnicmp(command, CMD_PKT_FILTER_PORTS, strlen(CMD_PKT_FILTER_PORTS)) == 0) {
		bytes_written = dhd_set_packet_filter_ports(net,
			&command[strlen(CMD_PKT_FILTER_PORTS) + 1]);
		ret = bytes_written;
	}
#endif /* defined(CUSTOM_PLATFORM_NV_TEGRA) */
#endif /* PKT_FILTER_SUPPORT */
	else if (strnicmp(command, CMD_BTCOEXSCAN_START, strlen(CMD_BTCOEXSCAN_START)) == 0) {
		/* TBD: BTCOEXSCAN-START */
	}
	else if (strnicmp(command, CMD_BTCOEXSCAN_STOP, strlen(CMD_BTCOEXSCAN_STOP)) == 0) {
		/* TBD: BTCOEXSCAN-STOP */
	}
	else if (strnicmp(command, CMD_BTCOEXMODE, strlen(CMD_BTCOEXMODE)) == 0) {
#ifdef WL_CFG80211
		void *dhdp = wl_cfg80211_get_dhdp();
		bytes_written = wl_cfg80211_set_btcoex_dhcp(net, dhdp, command);
#else
#ifdef PKT_FILTER_SUPPORT
		uint mode = *(command + strlen(CMD_BTCOEXMODE) + 1) - '0';

		if (mode == 1)
			net_os_enable_packet_filter(net, 0); /* DHCP starts */
		else
			net_os_enable_packet_filter(net, 1); /* DHCP ends */
#endif /* PKT_FILTER_SUPPORT */
#endif /* WL_CFG80211 */
	}
	else if (strnicmp(command, CMD_SETSUSPENDOPT, strlen(CMD_SETSUSPENDOPT)) == 0) {
		bytes_written = wl_android_set_suspendopt(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SETSUSPENDMODE, strlen(CMD_SETSUSPENDMODE)) == 0) {
		bytes_written = wl_android_set_suspendmode(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SETBAND, strlen(CMD_SETBAND)) == 0) {
		uint band = *(command + strlen(CMD_SETBAND) + 1) - '0';
#ifdef WL_HOST_BAND_MGMT
		s32 ret = 0;
		if ((ret = wl_cfg80211_set_band(net, band)) < 0) {
			if (ret == BCME_UNSUPPORTED) {
				/* If roam_var is unsupported, fallback to the original method */
				ANDROID_ERROR(("WL_HOST_BAND_MGMT defined, "
					"but roam_band iovar unsupported in the firmware\n"));
			} else {
				bytes_written = -1;
				goto exit;
			}
		}
		if ((band == WLC_BAND_AUTO) || (ret == BCME_UNSUPPORTED))
			bytes_written = wldev_set_band(net, band);
#else
		bytes_written = wldev_set_band(net, band);
#endif /* WL_HOST_BAND_MGMT */
	}
	else if (strnicmp(command, CMD_GETBAND, strlen(CMD_GETBAND)) == 0) {
		bytes_written = wl_android_get_band(net, command, priv_cmd.total_len);
	}
#ifdef WL_CFG80211
	/* CUSTOMER_SET_COUNTRY feature is define for only GGSM model */
	else if (strnicmp(command, CMD_COUNTRY, strlen(CMD_COUNTRY)) == 0) {
		char *country_code = command + strlen(CMD_COUNTRY) + 1;
#ifdef CUSTOMER_HW5
		/* Customer_hw5 want to keep connections */
		bytes_written = wldev_set_country(net, country_code, true, false);
#else
		bytes_written = wldev_set_country(net, country_code, true, true);
#endif
	}
#endif /* WL_CFG80211 */


#ifdef PNO_SUPPORT
	else if (strnicmp(command, CMD_PNOSSIDCLR_SET, strlen(CMD_PNOSSIDCLR_SET)) == 0) {
		bytes_written = dhd_dev_pno_stop_for_ssid(net);
	}
#ifndef WL_SCHED_SCAN
	else if (strnicmp(command, CMD_PNOSETUP_SET, strlen(CMD_PNOSETUP_SET)) == 0) {
		bytes_written = wl_android_set_pno_setup(net, command, priv_cmd.total_len);
	}
#endif /* !WL_SCHED_SCAN */
	else if (strnicmp(command, CMD_PNOENABLE_SET, strlen(CMD_PNOENABLE_SET)) == 0) {
		int enable = *(command + strlen(CMD_PNOENABLE_SET) + 1) - '0';
		bytes_written = (enable)? 0 : dhd_dev_pno_stop_for_ssid(net);
	}
	else if (strnicmp(command, CMD_WLS_BATCHING, strlen(CMD_WLS_BATCHING)) == 0) {
		bytes_written = wls_parse_batching_cmd(net, command, priv_cmd.total_len);
	}
#endif /* PNO_SUPPORT */
	else if (strnicmp(command, CMD_P2P_DEV_ADDR, strlen(CMD_P2P_DEV_ADDR)) == 0) {
		bytes_written = wl_android_get_p2p_dev_addr(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_P2P_SET_NOA, strlen(CMD_P2P_SET_NOA)) == 0) {
		int skip = strlen(CMD_P2P_SET_NOA) + 1;
		bytes_written = wl_cfg80211_set_p2p_noa(net, command + skip,
			priv_cmd.total_len - skip);
	}
#ifdef WL_SDO
	else if (strnicmp(command, CMD_P2P_SD_OFFLOAD, strlen(CMD_P2P_SD_OFFLOAD)) == 0) {
		u8 *buf = command;
		u8 *cmd_id = NULL;
		int len;

		cmd_id = strsep((char **)&buf, " ");
		/* if buf == NULL, means no arg */
		if (buf == NULL)
			len = 0;
		else
			len = strlen(buf);

		bytes_written = wl_cfg80211_sd_offload(net, cmd_id, buf, len);
	}
#endif /* WL_SDO */
#ifdef WL_NAN
	else if (strnicmp(command, CMD_NAN, strlen(CMD_NAN)) == 0) {
		bytes_written = wl_cfg80211_nan_cmd_handler(net, command,
			priv_cmd.total_len);
	}
#endif /* WL_NAN */
#if !defined WL_ENABLE_P2P_IF
	else if (strnicmp(command, CMD_P2P_GET_NOA, strlen(CMD_P2P_GET_NOA)) == 0) {
		bytes_written = wl_cfg80211_get_p2p_noa(net, command, priv_cmd.total_len);
	}
#endif /* WL_ENABLE_P2P_IF */
	else if (strnicmp(command, CMD_P2P_SET_PS, strlen(CMD_P2P_SET_PS)) == 0) {
		int skip = strlen(CMD_P2P_SET_PS) + 1;
		bytes_written = wl_cfg80211_set_p2p_ps(net, command + skip,
			priv_cmd.total_len - skip);
	}
#ifdef WL_CFG80211
	else if (strnicmp(command, CMD_SET_AP_WPS_P2P_IE,
		strlen(CMD_SET_AP_WPS_P2P_IE)) == 0) {
		int skip = strlen(CMD_SET_AP_WPS_P2P_IE) + 3;
		bytes_written = wl_cfg80211_set_wps_p2p_ie(net, command + skip,
			priv_cmd.total_len - skip, *(command + skip - 2) - '0');
	}
#ifdef WLFBT
	else if (strnicmp(command, CMD_GET_FTKEY, strlen(CMD_GET_FTKEY)) == 0) {
		wl_cfg80211_get_fbt_key(command);
		bytes_written = FBT_KEYLEN;
	}
#endif /* WLFBT */
#endif /* WL_CFG80211 */
	else if (strnicmp(command, CMD_OKC_SET_PMK, strlen(CMD_OKC_SET_PMK)) == 0)
		bytes_written = wl_android_set_pmk(net, command, priv_cmd.total_len);
	else if (strnicmp(command, CMD_OKC_ENABLE, strlen(CMD_OKC_ENABLE)) == 0)
		bytes_written = wl_android_okc_enable(net, command, priv_cmd.total_len);
#ifdef BCMCCX
	else if (strnicmp(command, CMD_GETCCKM_RN, strlen(CMD_GETCCKM_RN)) == 0) {
		bytes_written = wl_android_get_cckm_rn(net, command);
	}
	else if (strnicmp(command, CMD_SETCCKM_KRK, strlen(CMD_SETCCKM_KRK)) == 0) {
		bytes_written = wl_android_set_cckm_krk(net, command);
	}
	else if (strnicmp(command, CMD_GET_ASSOC_RES_IES, strlen(CMD_GET_ASSOC_RES_IES)) == 0) {
		bytes_written = wl_android_get_assoc_res_ies(net, command);
	}
#endif /* BCMCCX */
#if defined(WL_SUPPORT_AUTO_CHANNEL)
	else if (strnicmp(command, CMD_GET_BEST_CHANNELS,
		strlen(CMD_GET_BEST_CHANNELS)) == 0) {
		bytes_written = wl_cfg80211_get_best_channels(net, command,
			priv_cmd.total_len);
	}
#endif /* WL_SUPPORT_AUTO_CHANNEL */
	else if (strnicmp(command, CMD_HAPD_MAC_FILTER, strlen(CMD_HAPD_MAC_FILTER)) == 0) {
		int skip = strlen(CMD_HAPD_MAC_FILTER) + 1;
		wl_android_set_mac_address_filter(net, (const char*)command+skip);
	}
	else if (strnicmp(command, CMD_SETROAMMODE, strlen(CMD_SETROAMMODE)) == 0)
		bytes_written = wl_android_set_roam_mode(net, command, priv_cmd.total_len);
#if defined(BCMFW_ROAM_ENABLE)
	else if (strnicmp(command, CMD_SET_ROAMPREF, strlen(CMD_SET_ROAMPREF)) == 0) {
		bytes_written = wl_android_set_roampref(net, command, priv_cmd.total_len);
	}
#endif /* BCMFW_ROAM_ENABLE */
#ifdef WL_CFG80211
	else if (strnicmp(command, CMD_MIRACAST, strlen(CMD_MIRACAST)) == 0)
		bytes_written = wl_android_set_miracast(net, command, priv_cmd.total_len);
#if defined(CUSTOM_PLATFORM_NV_TEGRA)
	else if (strnicmp(command, CMD_SETMIRACAST, strlen(CMD_SETMIRACAST)) == 0)
		bytes_written = wldev_miracast_tuning(net, command, priv_cmd.total_len);
	else if (strnicmp(command, CMD_ASSOCRESPIE, strlen(CMD_ASSOCRESPIE)) == 0)
		bytes_written = wldev_get_assoc_resp_ie(net, command, priv_cmd.total_len);
	else if (strnicmp(command, CMD_RXRATESTATS, strlen(CMD_RXRATESTATS)) == 0)
		bytes_written = wldev_get_rx_rate_stats(net, command, priv_cmd.total_len);
#endif /* defined(CUSTOM_PLATFORM_NV_TEGRA) */
	else if (strnicmp(command, CMD_SETIBSSBEACONOUIDATA, strlen(CMD_SETIBSSBEACONOUIDATA)) == 0)
		bytes_written = wl_android_set_ibss_beacon_ouidata(net,
		command, priv_cmd.total_len);
#endif
#ifdef WLAIBSS
	else if (strnicmp(command, CMD_SETIBSSTXFAILEVENT,
		strlen(CMD_SETIBSSTXFAILEVENT)) == 0)
		bytes_written = wl_android_set_ibss_txfail_event(net, command, priv_cmd.total_len);
	else if (strnicmp(command, CMD_GET_IBSS_PEER_INFO_ALL,
		strlen(CMD_GET_IBSS_PEER_INFO_ALL)) == 0)
		bytes_written = wl_android_get_ibss_peer_info(net, command, priv_cmd.total_len,
			TRUE);
	else if (strnicmp(command, CMD_GET_IBSS_PEER_INFO,
		strlen(CMD_GET_IBSS_PEER_INFO)) == 0)
		bytes_written = wl_android_get_ibss_peer_info(net, command, priv_cmd.total_len,
			FALSE);
	else if (strnicmp(command, CMD_SETIBSSROUTETABLE,
		strlen(CMD_SETIBSSROUTETABLE)) == 0)
		bytes_written = wl_android_set_ibss_routetable(net, command,
			priv_cmd.total_len);
	else if (strnicmp(command, CMD_SETIBSSAMPDU, strlen(CMD_SETIBSSAMPDU)) == 0)
		bytes_written = wl_android_set_ibss_ampdu(net, command, priv_cmd.total_len);
	else if (strnicmp(command, CMD_SETIBSSANTENNAMODE, strlen(CMD_SETIBSSANTENNAMODE)) == 0)
		bytes_written = wl_android_set_ibss_antenna(net, command, priv_cmd.total_len);
#endif /* WLAIBSS */
	else if (strnicmp(command, CMD_KEEP_ALIVE, strlen(CMD_KEEP_ALIVE)) == 0) {
		int skip = strlen(CMD_KEEP_ALIVE) + 1;
		bytes_written = wl_keep_alive_set(net, command + skip, priv_cmd.total_len - skip);
	}
#ifdef WL_CFG80211
	else if (strnicmp(command, CMD_ROAM_OFFLOAD, strlen(CMD_ROAM_OFFLOAD)) == 0) {
		int enable = *(command + strlen(CMD_ROAM_OFFLOAD) + 1) - '0';
		bytes_written = wl_cfg80211_enable_roam_offload(net, enable);
	}
	else if (strnicmp(command, CMD_ROAM_OFFLOAD_APLIST, strlen(CMD_ROAM_OFFLOAD_APLIST)) == 0) {
		bytes_written = wl_android_set_roam_offload_bssid_list(net,
			command + strlen(CMD_ROAM_OFFLOAD_APLIST) + 1);
	}
#endif
#ifdef P2PRESP_WFDIE_SRC
	else if (strnicmp(command, CMD_P2P_SET_WFDIE_RESP,
		strlen(CMD_P2P_SET_WFDIE_RESP)) == 0) {
		int mode = *(command + strlen(CMD_P2P_SET_WFDIE_RESP) + 1) - '0';
		bytes_written = wl_android_set_wfdie_resp(net, mode);
	} else if (strnicmp(command, CMD_P2P_GET_WFDIE_RESP,
		strlen(CMD_P2P_GET_WFDIE_RESP)) == 0) {
		bytes_written = wl_android_get_wfdie_resp(net, command, priv_cmd.total_len);
	}
#endif /* P2PRESP_WFDIE_SRC */
	else if (strnicmp(command, CMD_GET_LINK_STATUS, strlen(CMD_GET_LINK_STATUS)) == 0) {
		bytes_written = wl_android_get_link_status(net, command, priv_cmd.total_len);
	}
#ifdef CONNECTION_STATISTICS
	else if (strnicmp(command, CMD_GET_CONNECTION_STATS,
		strlen(CMD_GET_CONNECTION_STATS)) == 0) {
		bytes_written = wl_android_get_connection_stats(net, command,
			priv_cmd.total_len);
	}
#endif
	else if(strnicmp(command, CMD_GET_CHANNEL, strlen(CMD_GET_CHANNEL)) == 0) {
		bytes_written = wl_android_get_channel(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SET_ROAM, strlen(CMD_SET_ROAM)) == 0) {
		bytes_written = wl_android_set_roam_trigger(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_GET_ROAM, strlen(CMD_GET_ROAM)) == 0) {
		bytes_written = wl_android_get_roam_trigger(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_GET_KEEP_ALIVE, strlen(CMD_GET_KEEP_ALIVE)) == 0) {
		int skip = strlen(CMD_GET_KEEP_ALIVE) + 1;
		bytes_written = wl_android_get_keep_alive(net, command+skip, priv_cmd.total_len-skip);
	}
	else if (strnicmp(command, CMD_GET_PM, strlen(CMD_GET_PM)) == 0) {
		bytes_written = wl_android_get_pm(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SET_PM, strlen(CMD_SET_PM)) == 0) {
		bytes_written = wl_android_set_pm(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_MONITOR, strlen(CMD_MONITOR)) == 0) {
		bytes_written = wl_android_set_monitor(net, command, priv_cmd.total_len);
	} else {
		ANDROID_ERROR(("Unknown PRIVATE command %s - ignored\n", command));
		snprintf(command, 3, "OK");
		bytes_written = strlen("OK");
	}

	if (bytes_written >= 0) {
		if ((bytes_written == 0) && (priv_cmd.total_len > 0))
			command[0] = '\0';
		if (bytes_written >= priv_cmd.total_len) {
			ANDROID_ERROR(("%s: bytes_written = %d\n", __FUNCTION__, bytes_written));
			bytes_written = priv_cmd.total_len;
		} else {
			bytes_written++;
		}
		priv_cmd.used_len = bytes_written;
		if (copy_to_user(priv_cmd.buf, command, bytes_written)) {
			ANDROID_ERROR(("%s: failed to copy data to user buffer\n", __FUNCTION__));
			ret = -EFAULT;
		}
	}
	else {
		ret = bytes_written;
	}

exit:
	net_os_wake_unlock(net);
	if (command) {
		kfree(command);
	}

	return ret;
}

int wl_android_init(void)
{
	int ret = 0;

#ifdef ENABLE_INSMOD_NO_FW_LOAD
	dhd_download_fw_on_driverload = FALSE;
#endif /* ENABLE_INSMOD_NO_FW_LOAD */
	if (!iface_name[0]) {
		memset(iface_name, 0, IFNAMSIZ);
		bcm_strncpy_s(iface_name, IFNAMSIZ, "wlan", IFNAMSIZ);
	}

#ifdef WL_GENL
	wl_genl_init();
#endif
	wl_netlink_init();

	return ret;
}

int wl_android_exit(void)
{
	int ret = 0;
	struct io_cfg *cur, *q;

#ifdef WL_GENL
	wl_genl_deinit();
#endif /* WL_GENL */
	wl_netlink_deinit();

	list_for_each_entry_safe(cur, q, &miracast_resume_list, list) {
		list_del(&cur->list);
		kfree(cur);
	}

	return ret;
}

void wl_android_post_init(void)
{

#ifdef ENABLE_4335BT_WAR
	bcm_bt_unlock(lock_cookie_wifi);
	printk("%s: btlock released\n", __FUNCTION__);
#endif /* ENABLE_4335BT_WAR */

	if (!dhd_download_fw_on_driverload)
		g_wifi_on = FALSE;
}

#ifdef WL_GENL
/* Generic Netlink Initializaiton */
static int wl_genl_init(void)
{
	int ret;

	ANDROID_TRACE(("GEN Netlink Init\n\n"));

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
	/* register new family */
	ret = genl_register_family(&wl_genl_family);
	if (ret != 0)
		goto failure;

	/* register functions (commands) of the new family */
	ret = genl_register_ops(&wl_genl_family, &wl_genl_ops);
	if (ret != 0) {
		ANDROID_ERROR(("register ops failed: %i\n", ret));
		genl_unregister_family(&wl_genl_family);
		goto failure;
	}

	ret = genl_register_mc_group(&wl_genl_family, &wl_genl_mcast);
#else
	ret = genl_register_family_with_ops_groups(&wl_genl_family, wl_genl_ops, wl_genl_mcast);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0) */
	if (ret != 0) {
		ANDROID_ERROR(("register mc_group failed: %i\n", ret));
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
		genl_unregister_ops(&wl_genl_family, &wl_genl_ops);
#endif
		genl_unregister_family(&wl_genl_family);
		goto failure;
	}

	return 0;

failure:
	ANDROID_ERROR(("Registering Netlink failed!!\n"));
	return -1;
}

/* Generic netlink deinit */
static int wl_genl_deinit(void)
{

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
	if (genl_unregister_ops(&wl_genl_family, &wl_genl_ops) < 0)
		ANDROID_ERROR(("Unregister wl_genl_ops failed\n"));
#endif
	if (genl_unregister_family(&wl_genl_family) < 0)
		ANDROID_ERROR(("Unregister wl_genl_ops failed\n"));

	return 0;
}

s32 wl_event_to_bcm_event(u16 event_type)
{
	u16 event = -1;

	switch (event_type) {
		case WLC_E_SERVICE_FOUND:
			event = BCM_E_SVC_FOUND;
			break;
		case WLC_E_P2PO_ADD_DEVICE:
			event = BCM_E_DEV_FOUND;
			break;
		case WLC_E_P2PO_DEL_DEVICE:
			event = BCM_E_DEV_LOST;
			break;
	/* Above events are supported from BCM Supp ver 47 Onwards */
#ifdef BT_WIFI_HANDOVER
		case WLC_E_BT_WIFI_HANDOVER_REQ:
			event = BCM_E_DEV_BT_WIFI_HO_REQ;
			break;
#endif /* BT_WIFI_HANDOVER */

		default:
			ANDROID_ERROR(("Event not supported\n"));
	}

	return event;
}

s32
wl_genl_send_msg(
	struct net_device *ndev,
	u32 event_type,
	u8 *buf,
	u16 len,
	u8 *subhdr,
	u16 subhdr_len)
{
	int ret = 0;
	struct sk_buff *skb = NULL;
	void *msg;
	u32 attr_type = 0;
	bcm_event_hdr_t *hdr = NULL;
	int mcast = 1; /* By default sent as mutlicast type */
	int pid = 0;
	u8 *ptr = NULL, *p = NULL;
	u32 tot_len = sizeof(bcm_event_hdr_t) + subhdr_len + len;
	u16 kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;


	ANDROID_TRACE(("Enter \n"));

	/* Decide between STRING event and Data event */
	if (event_type == 0)
		attr_type = BCM_GENL_ATTR_STRING;
	else
		attr_type = BCM_GENL_ATTR_MSG;

	skb = genlmsg_new(NLMSG_GOODSIZE, kflags);
	if (skb == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	msg = genlmsg_put(skb, 0, 0, &wl_genl_family, 0, BCM_GENL_CMD_MSG);
	if (msg == NULL) {
		ret = -ENOMEM;
		goto out;
	}


	if (attr_type == BCM_GENL_ATTR_STRING) {
		/* Add a BCM_GENL_MSG attribute. Since it is specified as a string.
		 * make sure it is null terminated
		 */
		if (subhdr || subhdr_len) {
			ANDROID_ERROR(("No sub hdr support for the ATTR STRING type \n"));
			ret =  -EINVAL;
			goto out;
		}

		ret = nla_put_string(skb, BCM_GENL_ATTR_STRING, buf);
		if (ret != 0) {
			ANDROID_ERROR(("nla_put_string failed\n"));
			goto out;
		}
	} else {
		/* ATTR_MSG */

		/* Create a single buffer for all */
		p = ptr = kzalloc(tot_len, kflags);
		if (!ptr) {
			ret = -ENOMEM;
			ANDROID_ERROR(("ENOMEM!!\n"));
			goto out;
		}

		/* Include the bcm event header */
		hdr = (bcm_event_hdr_t *)ptr;
		hdr->event_type = wl_event_to_bcm_event(event_type);
		hdr->len = len + subhdr_len;
		ptr += sizeof(bcm_event_hdr_t);

		/* Copy subhdr (if any) */
		if (subhdr && subhdr_len) {
			memcpy(ptr, subhdr, subhdr_len);
			ptr += subhdr_len;
		}

		/* Copy the data */
		if (buf && len) {
			memcpy(ptr, buf, len);
		}

		ret = nla_put(skb, BCM_GENL_ATTR_MSG, tot_len, p);
		if (ret != 0) {
			ANDROID_ERROR(("nla_put_string failed\n"));
			goto out;
		}
	}

	if (mcast) {
		int err = 0;
		/* finalize the message */
		genlmsg_end(skb, msg);
		/* NETLINK_CB(skb).dst_group = 1; */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0)
		if ((err = genlmsg_multicast(skb, 0, wl_genl_mcast.id, GFP_ATOMIC)) < 0)
#else
		if ((err = genlmsg_multicast(&wl_genl_family, skb, 0, 0, GFP_ATOMIC)) < 0)
#endif
			ANDROID_ERROR(("genlmsg_multicast for attr(%d) failed. Error:%d \n",
				attr_type, err));
		else
			ANDROID_TRACE(("Multicast msg sent successfully. attr_type:%d len:%d \n",
				attr_type, tot_len));
	} else {
		NETLINK_CB(skb).dst_group = 0; /* Not in multicast group */

		/* finalize the message */
		genlmsg_end(skb, msg);

		/* send the message back */
		if (genlmsg_unicast(&init_net, skb, pid) < 0)
			ANDROID_ERROR(("genlmsg_unicast failed\n"));
	}

out:
	if (p)
		kfree(p);
	if (ret)
		nlmsg_free(skb);

	return ret;
}

static s32
wl_genl_handle_msg(
	struct sk_buff *skb,
	struct genl_info *info)
{
	struct nlattr *na;
	u8 *data = NULL;

	ANDROID_TRACE(("Enter \n"));

	if (info == NULL) {
		return -EINVAL;
	}

	na = info->attrs[BCM_GENL_ATTR_MSG];
	if (!na) {
		ANDROID_ERROR(("nlattribute NULL\n"));
		return -EINVAL;
	}

	data = (char *)nla_data(na);
	if (!data) {
		ANDROID_ERROR(("Invalid data\n"));
		return -EINVAL;
	} else {
		/* Handle the data */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)) || defined(WL_COMPAT_WIRELESS)
		ANDROID_TRACE(("%s: Data received from pid (%d) \n", __func__,
			info->snd_pid));
#else
		ANDROID_TRACE(("%s: Data received from pid (%d) \n", __func__,
			info->snd_portid));
#endif /* (LINUX_VERSION < VERSION(3, 7, 0) || WL_COMPAT_WIRELESS */
	}

	return 0;
}
#endif /* WL_GENL */


#if defined(RSSIAVG)
void
wl_free_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl)
{
	wl_rssi_cache_t *node, *cur, **rssi_head;
	int i=0;

	rssi_head = &rssi_cache_ctrl->m_cache_head;
	node = *rssi_head;

	for (;node;) {
		ANDROID_INFO(("%s: Free %d with BSSID %pM\n",
			__FUNCTION__, i, &node->BSSID));
		cur = node;
		node = cur->next;
		kfree(cur);
		i++;
	}
	*rssi_head = NULL;
}

void
wl_delete_dirty_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl)
{
	wl_rssi_cache_t *node, *prev, **rssi_head;
	int i = -1, tmp = 0;
	struct timeval now;

	do_gettimeofday(&now);

	rssi_head = &rssi_cache_ctrl->m_cache_head;
	node = *rssi_head;
	prev = node;
	for (;node;) {
		i++;
		if (now.tv_sec > node->tv.tv_sec) {
			if (node == *rssi_head) {
				tmp = 1;
				*rssi_head = node->next;
			} else {
				tmp = 0;
				prev->next = node->next;
			}
			ANDROID_INFO(("%s: Del %d with BSSID %pM\n",
				__FUNCTION__, i, &node->BSSID));
			kfree(node);
			if (tmp == 1) {
				node = *rssi_head;
				prev = node;
			} else {
				node = prev->next;
			}
			continue;
		}
		prev = node;
		node = node->next;
	}
}

void
wl_delete_disconnected_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl, u8 *bssid)
{
	wl_rssi_cache_t *node, *prev, **rssi_head;
	int i = -1, tmp = 0;

	rssi_head = &rssi_cache_ctrl->m_cache_head;
	node = *rssi_head;
	prev = node;
	for (;node;) {
		i++;
		if (!memcmp(&node->BSSID, bssid, ETHER_ADDR_LEN)) {
			if (node == *rssi_head) {
				tmp = 1;
				*rssi_head = node->next;
			} else {
				tmp = 0;
				prev->next = node->next;
			}
			ANDROID_INFO(("%s: Del %d with BSSID %pM\n",
				__FUNCTION__, i, &node->BSSID));
			kfree(node);
			if (tmp == 1) {
				node = *rssi_head;
				prev = node;
			} else {
				node = prev->next;
			}
			continue;
		}
		prev = node;
		node = node->next;
	}
}

void
wl_reset_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl)
{
	wl_rssi_cache_t *node, **rssi_head;

	rssi_head = &rssi_cache_ctrl->m_cache_head;

	/* reset dirty */
	node = *rssi_head;
	for (;node;) {
		node->dirty += 1;
		node = node->next;
	}
}

int
wl_update_connected_rssi_cache(struct net_device *net, wl_rssi_cache_ctrl_t *rssi_cache_ctrl, int *rssi_avg)
{
	wl_rssi_cache_t *node, *prev, *leaf, **rssi_head;
	int j, k=0;
	int rssi, error=0;
	struct ether_addr bssid;
	struct timeval now, timeout;

	if (!g_wifi_on)
		return 0;

	error = wldev_ioctl(net, WLC_GET_BSSID, &bssid, sizeof(bssid), false);
	if (error == BCME_NOTASSOCIATED) {
		ANDROID_INFO(("%s: Not Associated! res:%d\n", __FUNCTION__, error));
		return 0;
	}
	if (error) {
		ANDROID_ERROR(("Could not get bssid (%d)\n", error));
	}
	error = wldev_get_rssi(net, &rssi);
	if (error) {
		ANDROID_ERROR(("Could not get rssi (%d)\n", error));
		return error;
	}

	do_gettimeofday(&now);
	timeout.tv_sec = now.tv_sec + RSSICACHE_TIMEOUT;
	if (timeout.tv_sec < now.tv_sec) {
		/*
		 * Integer overflow - assume long enough timeout to be assumed
		 * to be infinite, i.e., the timeout would never happen.
		 */
		ANDROID_TRACE(("%s: Too long timeout (secs=%d) to ever happen - now=%lu, timeout=%lu",
			__FUNCTION__, RSSICACHE_TIMEOUT, now.tv_sec, timeout.tv_sec));
	}

	/* update RSSI */
	rssi_head = &rssi_cache_ctrl->m_cache_head;
	node = *rssi_head;
	prev = NULL;
	for (;node;) {
		if (!memcmp(&node->BSSID, &bssid, ETHER_ADDR_LEN)) {
			ANDROID_INFO(("%s: Update %d with BSSID %pM, RSSI=%d\n",
				__FUNCTION__, k, &bssid, rssi));
			for (j=0; j<RSSIAVG_LEN-1; j++)
				node->RSSI[j] = node->RSSI[j+1];
			node->RSSI[j] = rssi;
			node->dirty = 0;
			node->tv = timeout;
			goto exit;
		}
		prev = node;
		node = node->next;
		k++;
	}

	leaf = kmalloc(sizeof(wl_rssi_cache_t), GFP_KERNEL);
	if (!leaf) {
		ANDROID_ERROR(("%s: Memory alloc failure %d\n",
			__FUNCTION__, sizeof(wl_rssi_cache_t)));
		return 0;
	}
	ANDROID_INFO(("%s: Add %d with cached BSSID %pM, RSSI=%d in the leaf\n",
			__FUNCTION__, k, &bssid, rssi));

	leaf->next = NULL;
	leaf->dirty = 0;
	leaf->tv = timeout;
	memcpy(&leaf->BSSID, &bssid, ETHER_ADDR_LEN);
	for (j=0; j<RSSIAVG_LEN; j++)
		leaf->RSSI[j] = rssi;

	if (!prev)
		*rssi_head = leaf;
	else
		prev->next = leaf;

exit:
	*rssi_avg = (int)wl_get_avg_rssi(rssi_cache_ctrl, &bssid);

	return error;
}

void
wl_update_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl, wl_scan_results_t *ss_list)
{
	wl_rssi_cache_t *node, *prev, *leaf, **rssi_head;
	wl_bss_info_t *bi = NULL;
	int i, j, k;
	struct timeval now, timeout;

	if (!ss_list->count)
		return;

	do_gettimeofday(&now);
	timeout.tv_sec = now.tv_sec + RSSICACHE_TIMEOUT;
	if (timeout.tv_sec < now.tv_sec) {
		/*
		 * Integer overflow - assume long enough timeout to be assumed
		 * to be infinite, i.e., the timeout would never happen.
		 */
		ANDROID_TRACE(("%s: Too long timeout (secs=%d) to ever happen - now=%lu, timeout=%lu",
			__FUNCTION__, RSSICACHE_TIMEOUT, now.tv_sec, timeout.tv_sec));
	}

	rssi_head = &rssi_cache_ctrl->m_cache_head;

	/* update RSSI */
	for (i = 0; i < ss_list->count; i++) {
		node = *rssi_head;
		prev = NULL;
		k = 0;
		bi = bi ? (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length)) : ss_list->bss_info;
		for (;node;) {
			if (!memcmp(&node->BSSID, &bi->BSSID, ETHER_ADDR_LEN)) {
				ANDROID_INFO(("%s: Update %d with BSSID %pM, RSSI=%d, SSID \"%s\"\n",
					__FUNCTION__, k, &bi->BSSID, dtoh16(bi->RSSI), bi->SSID));
				for (j=0; j<RSSIAVG_LEN-1; j++)
					node->RSSI[j] = node->RSSI[j+1];
				node->RSSI[j] = dtoh16(bi->RSSI);
				node->dirty = 0;
				node->tv = timeout;
				break;
			}
			prev = node;
			node = node->next;
			k++;
		}

		if (node)
			continue;

		leaf = kmalloc(sizeof(wl_rssi_cache_t), GFP_KERNEL);
		if (!leaf) {
			ANDROID_ERROR(("%s: Memory alloc failure %d\n",
				__FUNCTION__, sizeof(wl_rssi_cache_t)));
			return;
		}
		ANDROID_INFO(("%s: Add %d with cached BSSID %pM, RSSI=%d, SSID \"%s\" in the leaf\n",
				__FUNCTION__, k, &bi->BSSID, dtoh16(bi->RSSI), bi->SSID));

		leaf->next = NULL;
		leaf->dirty = 0;
		leaf->tv = timeout;
		memcpy(&leaf->BSSID, &bi->BSSID, ETHER_ADDR_LEN);
		for (j=0; j<RSSIAVG_LEN; j++)
			leaf->RSSI[j] = dtoh16(bi->RSSI);

		if (!prev)
			*rssi_head = leaf;
		else
			prev->next = leaf;
	}
}

int16
wl_get_avg_rssi(wl_rssi_cache_ctrl_t *rssi_cache_ctrl, void *addr)
{
	wl_rssi_cache_t *node, **rssi_head;
	int j, rssi_sum, rssi=RSSI_MINVAL;

	rssi_head = &rssi_cache_ctrl->m_cache_head;

	node = *rssi_head;
	for (;node;) {
		if (!memcmp(&node->BSSID, addr, ETHER_ADDR_LEN)) {
			rssi_sum = 0;
			rssi = 0;
			for (j=0; j<RSSIAVG_LEN; j++)
				rssi_sum += node->RSSI[RSSIAVG_LEN-j-1];
			rssi = rssi_sum / j;
			break;
		}
		node = node->next;
	}
	rssi = MIN(rssi, RSSI_MAXVAL);
	if (rssi == RSSI_MINVAL) {
		ANDROID_ERROR(("%s: BSSID %pM does not in RSSI cache\n",
		__FUNCTION__, addr));
	}
	return (int16)rssi;
}
#endif

#if defined(RSSIOFFSET)
int
wl_update_rssi_offset(struct net_device *net, int rssi)
{
	uint chip, chiprev;

	if (!g_wifi_on)
		return rssi;

	chip = dhd_conf_get_chip(dhd_get_pub(net));
	chiprev = dhd_conf_get_chiprev(dhd_get_pub(net));
	if (chip == BCM4330_CHIP_ID && chiprev == BCM4330B2_CHIP_REV) {
#if defined(RSSIOFFSET_NEW)
		int j;
		for (j=0; j<RSSI_OFFSET; j++) {
			if (rssi - (RSSI_OFFSET_MINVAL+RSSI_OFFSET_INTVAL*(j+1)) < 0)
				break;
		}
		rssi += j;
#else
		rssi += RSSI_OFFSET;
#endif
	}
	return MIN(rssi, RSSI_MAXVAL);
}
#endif

#if defined(BSSCACHE)
#define WLC_IW_SS_CACHE_CTRL_FIELD_MAXLEN	32

void
wl_free_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl)
{
	wl_bss_cache_t *node, *cur, **bss_head;
	int i=0;

	ANDROID_TRACE(("%s called\n", __FUNCTION__));

	bss_head = &bss_cache_ctrl->m_cache_head;
	node = *bss_head;

	for (;node;) {
		ANDROID_TRACE(("%s: Free %d with BSSID %pM\n",
			__FUNCTION__, i, &node->results.bss_info->BSSID));
		cur = node;
		node = cur->next;
		kfree(cur);
		i++;
	}
	*bss_head = NULL;
}

void
wl_delete_dirty_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl)
{
	wl_bss_cache_t *node, *prev, **bss_head;
	int i = -1, tmp = 0;
	struct timeval now;

	do_gettimeofday(&now);

	bss_head = &bss_cache_ctrl->m_cache_head;
	node = *bss_head;
	prev = node;
	for (;node;) {
		i++;
		if (now.tv_sec > node->tv.tv_sec) {
			if (node == *bss_head) {
				tmp = 1;
				*bss_head = node->next;
			} else {
				tmp = 0;
				prev->next = node->next;
			}
			ANDROID_TRACE(("%s: Del %d with BSSID %pM, RSSI=%d, SSID \"%s\"\n",
				__FUNCTION__, i, &node->results.bss_info->BSSID,
				dtoh16(node->results.bss_info->RSSI), node->results.bss_info->SSID));
			kfree(node);
			if (tmp == 1) {
				node = *bss_head;
				prev = node;
			} else {
				node = prev->next;
			}
			continue;
		}
		prev = node;
		node = node->next;
	}
}

void
wl_delete_disconnected_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl, u8 *bssid)
{
	wl_bss_cache_t *node, *prev, **bss_head;
	int i = -1, tmp = 0;

	bss_head = &bss_cache_ctrl->m_cache_head;
	node = *bss_head;
	prev = node;
	for (;node;) {
		i++;
		if (!memcmp(&node->results.bss_info->BSSID, bssid, ETHER_ADDR_LEN)) {
			if (node == *bss_head) {
				tmp = 1;
				*bss_head = node->next;
			} else {
				tmp = 0;
				prev->next = node->next;
			}
			ANDROID_TRACE(("%s: Del %d with BSSID %pM, RSSI=%d, SSID \"%s\"\n",
				__FUNCTION__, i, &node->results.bss_info->BSSID,
				dtoh16(node->results.bss_info->RSSI), node->results.bss_info->SSID));
			kfree(node);
			if (tmp == 1) {
				node = *bss_head;
				prev = node;
			} else {
				node = prev->next;
			}
			continue;
		}
		prev = node;
		node = node->next;
	}
}

void
wl_reset_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl)
{
	wl_bss_cache_t *node, **bss_head;

	bss_head = &bss_cache_ctrl->m_cache_head;

	/* reset dirty */
	node = *bss_head;
	for (;node;) {
		node->dirty += 1;
		node = node->next;
	}
}

void
wl_update_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl, wl_scan_results_t *ss_list)
{
	wl_bss_cache_t *node, *prev, *leaf, *tmp, **bss_head;
	wl_bss_info_t *bi = NULL;
	int i, k=0;
	struct timeval now, timeout;

	if (!ss_list->count)
		return;

	do_gettimeofday(&now);
	timeout.tv_sec = now.tv_sec + BSSCACHE_TIMEOUT;
	if (timeout.tv_sec < now.tv_sec) {
		/*
		 * Integer overflow - assume long enough timeout to be assumed
		 * to be infinite, i.e., the timeout would never happen.
		 */
		ANDROID_TRACE(("%s: Too long timeout (secs=%d) to ever happen - now=%lu, timeout=%lu",
			__FUNCTION__, BSSCACHE_TIMEOUT, now.tv_sec, timeout.tv_sec));
	}

	bss_head = &bss_cache_ctrl->m_cache_head;

	for (i=0; i < ss_list->count; i++) {
		node = *bss_head;
		prev = NULL;
		bi = bi ? (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length)) : ss_list->bss_info;

		for (;node;) {
			if (!memcmp(&node->results.bss_info->BSSID, &bi->BSSID, ETHER_ADDR_LEN)) {
				tmp = node;
				leaf = kmalloc(dtoh32(bi->length) + WLC_IW_SS_CACHE_CTRL_FIELD_MAXLEN, GFP_KERNEL);
				if (!leaf) {
					ANDROID_ERROR(("%s: Memory alloc failure %d and keep old BSS info\n",
						__FUNCTION__, dtoh32(bi->length) + WLC_IW_SS_CACHE_CTRL_FIELD_MAXLEN));
					break;
				}

				memcpy(leaf->results.bss_info, bi, dtoh32(bi->length));
				leaf->next = node->next;
				leaf->dirty = 0;
				leaf->tv = timeout;
				leaf->results.count = 1;
				leaf->results.version = ss_list->version;
				ANDROID_TRACE(("%s: Update %d with BSSID %pM, RSSI=%d, SSID \"%s\", length=%d\n",
					__FUNCTION__, k, &bi->BSSID, dtoh16(bi->RSSI), bi->SSID, dtoh32(bi->length)));
				if (!prev)
					*bss_head = leaf;
				else
					prev->next = leaf;
				node = leaf;
				prev = node;

				kfree(tmp);
				k++;
				break;
			}
			prev = node;
			node = node->next;
		}

		if (node)
			continue;

		leaf = kmalloc(dtoh32(bi->length) + WLC_IW_SS_CACHE_CTRL_FIELD_MAXLEN, GFP_KERNEL);
		if (!leaf) {
			ANDROID_ERROR(("%s: Memory alloc failure %d\n", __FUNCTION__,
				dtoh32(bi->length) + WLC_IW_SS_CACHE_CTRL_FIELD_MAXLEN));
			return;
		}
		ANDROID_TRACE(("%s: Add %d with cached BSSID %pM, RSSI=%d, SSID \"%s\" in the leaf\n",
				__FUNCTION__, k, &bi->BSSID, dtoh16(bi->RSSI), bi->SSID));

		memcpy(leaf->results.bss_info, bi, dtoh32(bi->length));
		leaf->next = NULL;
		leaf->dirty = 0;
		leaf->tv = timeout;
		leaf->results.count = 1;
		leaf->results.version = ss_list->version;
		k++;

		if (!prev)
			*bss_head = leaf;
		else
			prev->next = leaf;
	}
}

void
wl_release_bss_cache_ctrl(wl_bss_cache_ctrl_t *bss_cache_ctrl)
{
	ANDROID_TRACE(("%s:\n", __FUNCTION__));
	wl_free_bss_cache(bss_cache_ctrl);
}
#endif
