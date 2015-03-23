/*
 * Driver interaction with Linux Host AP driver
 * Copyright (c) 2002-2006, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifndef HOSTAP_DRIVER_H
#define HOSTAP_DRIVER_H

/* netdevice private ioctls (used, e.g., with iwpriv from user space) */

/* New wireless extensions API - SET/GET convention (even ioctl numbers are
 * root only)
 */
#define PRISM2_IOCTL_PRISM2_PARAM (SIOCIWFIRSTPRIV + 0)
#define PRISM2_IOCTL_GET_PRISM2_PARAM (SIOCIWFIRSTPRIV + 1)
#define PRISM2_IOCTL_WRITEMIF (SIOCIWFIRSTPRIV + 2)
#define PRISM2_IOCTL_READMIF (SIOCIWFIRSTPRIV + 3)
#define PRISM2_IOCTL_MONITOR (SIOCIWFIRSTPRIV + 4)
#define PRISM2_IOCTL_RESET (SIOCIWFIRSTPRIV + 6)
#define PRISM2_IOCTL_INQUIRE (SIOCIWFIRSTPRIV + 8)
#define PRISM2_IOCTL_WDS_ADD (SIOCIWFIRSTPRIV + 10)
#define PRISM2_IOCTL_WDS_DEL (SIOCIWFIRSTPRIV + 12)
#define PRISM2_IOCTL_SET_RID_WORD (SIOCIWFIRSTPRIV + 14)
#define PRISM2_IOCTL_MACCMD (SIOCIWFIRSTPRIV + 16)
#define PRISM2_IOCTL_ADDMAC (SIOCIWFIRSTPRIV + 18)
#define PRISM2_IOCTL_DELMAC (SIOCIWFIRSTPRIV + 20)
#define PRISM2_IOCTL_KICKMAC (SIOCIWFIRSTPRIV + 22)

/* following are not in SIOCGIWPRIV list; check permission in the driver code
 */
#define PRISM2_IOCTL_DOWNLOAD (SIOCDEVPRIVATE + 13)
#define PRISM2_IOCTL_HOSTAPD (SIOCDEVPRIVATE + 14)


/* PRISM2_IOCTL_PRISM2_PARAM ioctl() subtypes: */
enum {
	/* PRISM2_PARAM_PTYPE = 1, */ /* REMOVED 2003-10-22 */
	PRISM2_PARAM_TXRATECTRL = 2,
	PRISM2_PARAM_BEACON_INT = 3,
	PRISM2_PARAM_PSEUDO_IBSS = 4,
	PRISM2_PARAM_ALC = 5,
	/* PRISM2_PARAM_TXPOWER = 6, */ /* REMOVED 2003-10-22 */
	PRISM2_PARAM_DUMP = 7,
	PRISM2_PARAM_OTHER_AP_POLICY = 8,
	PRISM2_PARAM_AP_MAX_INACTIVITY = 9,
	PRISM2_PARAM_AP_BRIDGE_PACKETS = 10,
	PRISM2_PARAM_DTIM_PERIOD = 11,
	PRISM2_PARAM_AP_NULLFUNC_ACK = 12,
	PRISM2_PARAM_MAX_WDS = 13,
	PRISM2_PARAM_AP_AUTOM_AP_WDS = 14,
	PRISM2_PARAM_AP_AUTH_ALGS = 15,
	PRISM2_PARAM_MONITOR_ALLOW_FCSERR = 16,
	PRISM2_PARAM_HOST_ENCRYPT = 17,
	PRISM2_PARAM_HOST_DECRYPT = 18,
	PRISM2_PARAM_BUS_MASTER_THRESHOLD_RX = 19,
	PRISM2_PARAM_BUS_MASTER_THRESHOLD_TX = 20,
	PRISM2_PARAM_HOST_ROAMING = 21,
	PRISM2_PARAM_BCRX_STA_KEY = 22,
	PRISM2_PARAM_IEEE_802_1X = 23,
	PRISM2_PARAM_ANTSEL_TX = 24,
	PRISM2_PARAM_ANTSEL_RX = 25,
	PRISM2_PARAM_MONITOR_TYPE = 26,
	PRISM2_PARAM_WDS_TYPE = 27,
	PRISM2_PARAM_HOSTSCAN = 28,
	PRISM2_PARAM_AP_SCAN = 29,
	PRISM2_PARAM_ENH_SEC = 30,
	PRISM2_PARAM_IO_DEBUG = 31,
	PRISM2_PARAM_BASIC_RATES = 32,
	PRISM2_PARAM_OPER_RATES = 33,
	PRISM2_PARAM_HOSTAPD = 34,
	PRISM2_PARAM_HOSTAPD_STA = 35,
	PRISM2_PARAM_WPA = 36,
	PRISM2_PARAM_PRIVACY_INVOKED = 37,
	PRISM2_PARAM_TKIP_COUNTERMEASURES = 38,
	PRISM2_PARAM_DROP_UNENCRYPTED = 39,
	PRISM2_PARAM_SCAN_CHANNEL_MASK = 40,
};

enum { HOSTAP_ANTSEL_DO_NOT_TOUCH = 0, HOSTAP_ANTSEL_DIVERSITY = 1,
       HOSTAP_ANTSEL_LOW = 2, HOSTAP_ANTSEL_HIGH = 3 };


/* PRISM2_IOCTL_MACCMD ioctl() subcommands: */
enum { AP_MAC_CMD_POLICY_OPEN = 0, AP_MAC_CMD_POLICY_ALLOW = 1,
       AP_MAC_CMD_POLICY_DENY = 2, AP_MAC_CMD_FLUSH = 3,
       AP_MAC_CMD_KICKALL = 4 };


/* PRISM2_IOCTL_DOWNLOAD ioctl() dl_cmd: */
enum {
	PRISM2_DOWNLOAD_VOLATILE = 1 /* RAM */,
	/* Note! Old versions of prism2_srec have a fatal error in CRC-16
	 * calculation, which will corrupt all non-volatile downloads.
	 * PRISM2_DOWNLOAD_NON_VOLATILE used to be 2, but it is now 3 to
	 * prevent use of old versions of prism2_srec for non-volatile
	 * download. */
	PRISM2_DOWNLOAD_NON_VOLATILE = 3 /* FLASH */,
	PRISM2_DOWNLOAD_VOLATILE_GENESIS = 4 /* RAM in Genesis mode */,
	/* Persistent versions of volatile download commands (keep firmware
	 * data in memory and automatically re-download after hw_reset */
	PRISM2_DOWNLOAD_VOLATILE_PERSISTENT = 5,
	PRISM2_DOWNLOAD_VOLATILE_GENESIS_PERSISTENT = 6,
};

struct prism2_download_param {
	u32 dl_cmd;
	u32 start_addr;
	u32 num_areas;
	struct prism2_download_area {
		u32 addr; /* wlan card address */
		u32 len;
		caddr_t ptr; /* pointer to data in user space */
	} data[0];
};

#define PRISM2_MAX_DOWNLOAD_AREA_LEN 131072
#define PRISM2_MAX_DOWNLOAD_LEN 262144


/* PRISM2_IOCTL_HOSTAPD ioctl() cmd: */
enum {
	PRISM2_HOSTAPD_FLUSH = 1,
	PRISM2_HOSTAPD_ADD_STA = 2,
	PRISM2_HOSTAPD_REMOVE_STA = 3,
	PRISM2_HOSTAPD_GET_INFO_STA = 4,
	/* REMOVED: PRISM2_HOSTAPD_RESET_TXEXC_STA = 5, */
	PRISM2_SET_ENCRYPTION = 6,
	PRISM2_GET_ENCRYPTION = 7,
	PRISM2_HOSTAPD_SET_FLAGS_STA = 8,
	PRISM2_HOSTAPD_GET_RID = 9,
	PRISM2_HOSTAPD_SET_RID = 10,
	PRISM2_HOSTAPD_SET_ASSOC_AP_ADDR = 11,
	PRISM2_HOSTAPD_SET_GENERIC_ELEMENT = 12,
	PRISM2_HOSTAPD_MLME = 13,
	PRISM2_HOSTAPD_SCAN_REQ = 14,
	PRISM2_HOSTAPD_STA_CLEAR_STATS = 15,
};

#define PRISM2_HOSTAPD_MAX_BUF_SIZE 1024
#define PRISM2_HOSTAPD_RID_HDR_LEN \
((size_t) (&((struct prism2_hostapd_param *) 0)->u.rid.data))
#define PRISM2_HOSTAPD_GENERIC_ELEMENT_HDR_LEN \
((size_t) (&((struct prism2_hostapd_param *) 0)->u.generic_elem.data))

/* Maximum length for algorithm names (-1 for nul termination) used in ioctl()
 */
#define HOSTAP_CRYPT_ALG_NAME_LEN 16


struct prism2_hostapd_param {
	u32 cmd;
	u8 sta_addr[ETH_ALEN];
	union {
		struct {
			u16 aid;
			u16 capability;
			u8 tx_supp_rates;
		} add_sta;
		struct {
			u32 inactive_sec;
		} get_info_sta;
		struct {
			u8 alg[HOSTAP_CRYPT_ALG_NAME_LEN];
			u32 flags;
			u32 err;
			u8 idx;
			u8 seq[8]; /* sequence counter (set: RX, get: TX) */
			u16 key_len;
			u8 key[0];
		} crypt;
		struct {
			u32 flags_and;
			u32 flags_or;
		} set_flags_sta;
		struct {
			u16 rid;
			u16 len;
			u8 data[0];
		} rid;
		struct {
			u8 len;
			u8 data[0];
		} generic_elem;
		struct {
#define MLME_STA_DEAUTH 0
#define MLME_STA_DISASSOC 1
			u16 cmd;
			u16 reason_code;
		} mlme;
		struct {
			u8 ssid_len;
			u8 ssid[32];
		} scan_req;
	} u;
};

#define HOSTAP_CRYPT_FLAG_SET_TX_KEY BIT(0)
#define HOSTAP_CRYPT_FLAG_PERMANENT BIT(1)

#define HOSTAP_CRYPT_ERR_UNKNOWN_ALG 2
#define HOSTAP_CRYPT_ERR_UNKNOWN_ADDR 3
#define HOSTAP_CRYPT_ERR_CRYPT_INIT_FAILED 4
#define HOSTAP_CRYPT_ERR_KEY_SET_FAILED 5
#define HOSTAP_CRYPT_ERR_TX_KEY_SET_FAILED 6
#define HOSTAP_CRYPT_ERR_CARD_CONF_FAILED 7

#endif /* HOSTAP_DRIVER_H */
