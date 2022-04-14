/*
 * Linux cfg80211 driver - Android related functions
 *
 * Copyright (C) 2020, Broadcom.
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
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef _wl_android_
#define _wl_android_

#include <linux/module.h>
#include <linux/netdevice.h>
#include <wldev_common.h>
#include <dngl_stats.h>
#include <dhd.h>
#ifdef WL_EXT_IAPSTA
#ifdef WL_ESCAN
#include <wl_escan.h>
#endif /* WL_ESCAN */
#include <wl_iapsta.h>
#endif /* WL_IAPSTA */
#if defined(WL_EXT_IAPSTA) || defined(USE_IW) || defined(WL_ESCAN) || \
	(defined(WL_EXT_GENL) && defined(SENDPROB))
#ifndef WL_EVENT
#define WL_EVENT
#endif
#include <wl_event.h>
#endif
#include <wl_android_ext.h>

/* If any feature uses the Generic Netlink Interface, put it here to enable WL_GENL
 * automatically
 */
#if defined(WL_SDO) || defined(BT_WIFI_HANDOVER)
#define WL_GENL
#endif

#ifdef WL_GENL
#include <net/genetlink.h>
#endif

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

/**
 * Android platform dependent functions, feel free to add Android specific functions here
 * (save the macros in dhd). Please do NOT declare functions that are NOT exposed to dhd
 * or cfg, define them as static in wl_android.c
 */

/* message levels */
#define ANDROID_ERROR_LEVEL	(1 << 0)
#define ANDROID_TRACE_LEVEL	(1 << 1)
#define ANDROID_INFO_LEVEL	(1 << 2)
#define ANDROID_SCAN_LEVEL	(1 << 3)
#define ANDROID_DBG_LEVEL	(1 << 4)
#define ANDROID_TPUT_LEVEL	(1 << 8)
#define ANDROID_MSG_LEVEL	(1 << 0)

#define WL_MSG(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_MSG_LEVEL) { \
			printk(KERN_ERR DHD_LOG_PREFIX "[%s] %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)

#define WL_MSG_PRINT_RATE_LIMIT_PERIOD 1000000000u /* 1s in units of ns */
#define WL_MSG_RLMT(name, cmp, size, arg1, args...) \
do {	\
	if (android_msg_level & ANDROID_MSG_LEVEL) {	\
		static uint64 __err_ts = 0; \
		static uint32 __err_cnt = 0; \
		uint64 __cur_ts = 0; \
		static uint8 static_tmp[size]; \
		__cur_ts = osl_localtime_ns(); \
		if (__err_ts == 0 || (__cur_ts > __err_ts && \
		(__cur_ts - __err_ts > WL_MSG_PRINT_RATE_LIMIT_PERIOD)) || \
		memcmp(&static_tmp, cmp, size)) { \
			__err_ts = __cur_ts; \
			memcpy(static_tmp, cmp, size); \
			printk(KERN_ERR DHD_LOG_PREFIX "[%s] %s : [%u times] " arg1, \
				name, __func__, __err_cnt, ## args); \
			__err_cnt = 0; \
		} else { \
			++__err_cnt; \
		} \
	}	\
} while (0)

/**
 * wl_android_init will be called from module init function (dhd_module_init now), similarly
 * wl_android_exit will be called from module exit function (dhd_module_cleanup now)
 */
int wl_android_init(void);
int wl_android_exit(void);
void wl_android_post_init(void);
void wl_android_set_wifi_on_flag(bool enable);
#if defined(WLAN_ACCEL_BOOT)
int wl_android_wifi_accel_on(struct net_device *dev, bool force_reg_on);
int wl_android_wifi_accel_off(struct net_device *dev, bool force_reg_on);
#endif /* WLAN_ACCEL_BOOT */
int wl_android_wifi_on(struct net_device *dev);
int wl_android_wifi_off(struct net_device *dev, bool on_failure);
int wl_android_priv_cmd(struct net_device *net, struct ifreq *ifr);
int wl_handle_private_cmd(struct net_device *net, char *command, u32 cmd_len);
#ifdef WL_CFG80211
int wl_android_set_spect(struct net_device *dev, int spect);
s32 wl_android_get_band_chanspecs(struct net_device *ndev, void *buf, s32 buflen,
	chanspec_band_t band, bool acs_req);
#endif

#ifdef WL_GENL
typedef struct bcm_event_hdr {
	u16 event_type;
	u16 len;
} bcm_event_hdr_t;

/* attributes (variables): the index in this enum is used as a reference for the type,
 *             userspace application has to indicate the corresponding type
 *             the policy is used for security considerations
 */
enum {
	BCM_GENL_ATTR_UNSPEC,
	BCM_GENL_ATTR_STRING,
	BCM_GENL_ATTR_MSG,
	__BCM_GENL_ATTR_MAX
};
#define BCM_GENL_ATTR_MAX (__BCM_GENL_ATTR_MAX - 1)

/* commands: enumeration of all commands (functions),
 * used by userspace application to identify command to be ececuted
 */
enum {
	BCM_GENL_CMD_UNSPEC,
	BCM_GENL_CMD_MSG,
	__BCM_GENL_CMD_MAX
};
#define BCM_GENL_CMD_MAX (__BCM_GENL_CMD_MAX - 1)

/* Enum values used by the BCM supplicant to identify the events */
enum {
	BCM_E_UNSPEC,
	BCM_E_SVC_FOUND,
	BCM_E_DEV_FOUND,
	BCM_E_DEV_LOST,
#ifdef BT_WIFI_HANDOVER
	BCM_E_DEV_BT_WIFI_HO_REQ,
#endif
	BCM_E_MAX
};

s32 wl_genl_send_msg(struct net_device *ndev, u32 event_type,
	const u8 *string, u16 len, u8 *hdr, u16 hdrlen);
#endif /* WL_GENL */
s32 wl_netlink_send_msg(int pid, int type, int seq, const void *data, size_t size);

/* hostap mac mode */
#define MACLIST_MODE_DISABLED   0
#define MACLIST_MODE_DENY       1
#define MACLIST_MODE_ALLOW      2

/* max number of assoc list */
#define MAX_NUM_OF_ASSOCLIST    64

/* Bandwidth */
#define WL_CH_BANDWIDTH_20MHZ 20
#define WL_CH_BANDWIDTH_40MHZ 40
#define WL_CH_BANDWIDTH_80MHZ 80
#define WL_CH_BANDWIDTH_160MHZ 160

/* max number of mac filter list
 * restrict max number to 10 as maximum cmd string size is 255
 */
#define MAX_NUM_MAC_FILT        10
#define	WL_GET_BAND(ch)	(((uint)(ch) <= CH_MAX_2G_CHANNEL) ?	\
	WLC_BAND_2G : WLC_BAND_5G)

/* SoftAP auto channel feature */
#define APCS_BAND_2G_LEGACY1	20
#define APCS_BAND_2G_LEGACY2	0
#define APCS_BAND_AUTO		"band=auto"
#define APCS_BAND_2G		"band=2g"
#define APCS_BAND_5G		"band=5g"
#define APCS_BAND_6G		"band=6g"
#define FREQ_STR		"freq="
#define APCS_MAX_2G_CHANNELS	11
#define APCS_MAX_RETRY		10
#define APCS_DEFAULT_2G_CH	1
#define APCS_DEFAULT_5G_CH	149
#define APCS_DEFAULT_6G_CH	5

int wl_android_set_ap_mac_list(struct net_device *dev, int macmode, struct maclist *maclist);
#ifdef WL_BCNRECV
extern int wl_android_bcnrecv_config(struct net_device *ndev, char *data,
		int total_len);
extern int wl_android_bcnrecv_stop(struct net_device *ndev, uint reason);
extern int wl_android_bcnrecv_resume(struct net_device *ndev);
extern int wl_android_bcnrecv_suspend(struct net_device *ndev);
extern int wl_android_bcnrecv_event(struct net_device *ndev,
		uint attr_type, uint status, uint reason, uint8 *data, uint data_len);
#endif /* WL_BCNRECV */
#ifdef WL_CAC_TS
#define TSPEC_UPLINK_DIRECTION (0 << 5)	/* uplink direction traffic stream */
#define TSPEC_DOWNLINK_DIRECTION (1 << 5)	/* downlink direction traffic stream */
#define TSPEC_BI_DIRECTION (3 << 5)	/* bi direction traffic stream */
#define TSPEC_EDCA_ACCESS (1 << 7)	/* EDCA access policy */
#define TSPEC_UAPSD_PSB (1 << 2)		/* U-APSD power saving behavior */
#define TSPEC_TSINFO_TID_SHIFT 1		/* TID Shift */
#define TSPEC_TSINFO_PRIO_SHIFT 3		/* PRIO Shift */
#define TSPEC_MAX_ACCESS_CATEGORY 3
#define TSPEC_MAX_USER_PRIO	7
#define TSPEC_MAX_DIALOG_TOKEN	255
#define TSPEC_MAX_SURPLUS_BW 12410
#define TSPEC_MIN_SURPLUS_BW 11210
#define TSPEC_MAX_MSDU_SIZE 1520
#define TSPEC_DEF_MEAN_DATA_RATE 120000
#define TSPEC_DEF_MIN_PHY_RATE 6000000
#define TSPEC_DEF_DIALOG_TOKEN 7
#endif /* WL_CAC_TS */

#ifdef WL_SUPPORT_AUTO_CHANNEL
#define WLC_ACS_BAND_INVALID	0xffffu
#endif /* WL_SUPPORT_AUTO_CHANNEL */
#define WL_PRIV_CMD_LEN 64
#endif /* _wl_android_ */
