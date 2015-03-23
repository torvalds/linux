/*
 * WPA Supplicant - driver interaction with MADWIFI 802.11 driver
 * Copyright (c) 2004, Sam Leffler <sam@errno.com>
 * Copyright (c) 2004, Video54 Technologies
 * Copyright (c) 2004-2007, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 *
 * While this driver wrapper supports both AP (hostapd) and station
 * (wpa_supplicant) operations, the station side is deprecated and
 * driver_wext.c should be used instead. This driver wrapper should only be
 * used with hostapd for AP mode functionality.
 */

#include "includes.h"
#include <sys/ioctl.h>

#include "common.h"
#include "driver.h"
#include "driver_wext.h"
#include "eloop.h"
#include "common/ieee802_11_defs.h"
#include "wireless_copy.h"

/*
 * Avoid conflicts with wpa_supplicant definitions by undefining a definition.
 */
#undef WME_OUI_TYPE

#include <include/compat.h>
#include <net80211/ieee80211.h>
#ifdef WME_NUM_AC
/* Assume this is built against BSD branch of madwifi driver. */
#define MADWIFI_BSD
#include <net80211/_ieee80211.h>
#endif /* WME_NUM_AC */
#include <net80211/ieee80211_crypto.h>
#include <net80211/ieee80211_ioctl.h>

#ifdef CONFIG_WPS
#ifdef IEEE80211_IOCTL_FILTERFRAME
#include <netpacket/packet.h>

#ifndef ETH_P_80211_RAW
#define ETH_P_80211_RAW 0x0019
#endif
#endif /* IEEE80211_IOCTL_FILTERFRAME */
#endif /* CONFIG_WPS */

/*
 * Avoid conflicts with hostapd definitions by undefining couple of defines
 * from madwifi header files.
 */
#undef RSN_VERSION
#undef WPA_VERSION
#undef WPA_OUI_TYPE
#undef WME_OUI_TYPE


#ifdef IEEE80211_IOCTL_SETWMMPARAMS
/* Assume this is built against madwifi-ng */
#define MADWIFI_NG
#endif /* IEEE80211_IOCTL_SETWMMPARAMS */

#define WPA_KEY_RSC_LEN 8

#ifdef HOSTAPD

#include "priv_netlink.h"
#include "netlink.h"
#include "linux_ioctl.h"
#include "l2_packet/l2_packet.h"


struct madwifi_driver_data {
	struct hostapd_data *hapd;		/* back pointer */

	char	iface[IFNAMSIZ + 1];
	int     ifindex;
	struct l2_packet_data *sock_xmit;	/* raw packet xmit socket */
	struct l2_packet_data *sock_recv;	/* raw packet recv socket */
	int	ioctl_sock;			/* socket for ioctl() use */
	struct netlink_data *netlink;
	int	we_version;
	u8	acct_mac[ETH_ALEN];
	struct hostap_sta_driver_data acct_data;

	struct l2_packet_data *sock_raw; /* raw 802.11 management frames */
};

static int madwifi_sta_deauth(void *priv, const u8 *own_addr, const u8 *addr,
			      int reason_code);

static int
set80211priv(struct madwifi_driver_data *drv, int op, void *data, int len)
{
	struct iwreq iwr;
	int do_inline = len < IFNAMSIZ;

	memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
#ifdef IEEE80211_IOCTL_FILTERFRAME
	/* FILTERFRAME must be NOT inline, regardless of size. */
	if (op == IEEE80211_IOCTL_FILTERFRAME)
		do_inline = 0;
#endif /* IEEE80211_IOCTL_FILTERFRAME */
	if (op == IEEE80211_IOCTL_SET_APPIEBUF)
		do_inline = 0;
	if (do_inline) {
		/*
		 * Argument data fits inline; put it there.
		 */
		memcpy(iwr.u.name, data, len);
	} else {
		/*
		 * Argument data too big for inline transfer; setup a
		 * parameter block instead; the kernel will transfer
		 * the data for the driver.
		 */
		iwr.u.data.pointer = data;
		iwr.u.data.length = len;
	}

	if (ioctl(drv->ioctl_sock, op, &iwr) < 0) {
#ifdef MADWIFI_NG
		int first = IEEE80211_IOCTL_SETPARAM;
		static const char *opnames[] = {
			"ioctl[IEEE80211_IOCTL_SETPARAM]",
			"ioctl[IEEE80211_IOCTL_GETPARAM]",
			"ioctl[IEEE80211_IOCTL_SETMODE]",
			"ioctl[IEEE80211_IOCTL_GETMODE]",
			"ioctl[IEEE80211_IOCTL_SETWMMPARAMS]",
			"ioctl[IEEE80211_IOCTL_GETWMMPARAMS]",
			"ioctl[IEEE80211_IOCTL_SETCHANLIST]",
			"ioctl[IEEE80211_IOCTL_GETCHANLIST]",
			"ioctl[IEEE80211_IOCTL_CHANSWITCH]",
			"ioctl[IEEE80211_IOCTL_GET_APPIEBUF]",
			"ioctl[IEEE80211_IOCTL_SET_APPIEBUF]",
			"ioctl[IEEE80211_IOCTL_GETSCANRESULTS]",
			"ioctl[IEEE80211_IOCTL_FILTERFRAME]",
			"ioctl[IEEE80211_IOCTL_GETCHANINFO]",
			"ioctl[IEEE80211_IOCTL_SETOPTIE]",
			"ioctl[IEEE80211_IOCTL_GETOPTIE]",
			"ioctl[IEEE80211_IOCTL_SETMLME]",
			NULL,
			"ioctl[IEEE80211_IOCTL_SETKEY]",
			NULL,
			"ioctl[IEEE80211_IOCTL_DELKEY]",
			NULL,
			"ioctl[IEEE80211_IOCTL_ADDMAC]",
			NULL,
			"ioctl[IEEE80211_IOCTL_DELMAC]",
			NULL,
			"ioctl[IEEE80211_IOCTL_WDSMAC]",
			NULL,
			"ioctl[IEEE80211_IOCTL_WDSDELMAC]",
			NULL,
			"ioctl[IEEE80211_IOCTL_KICKMAC]",
		};
#else /* MADWIFI_NG */
		int first = IEEE80211_IOCTL_SETPARAM;
		static const char *opnames[] = {
			"ioctl[IEEE80211_IOCTL_SETPARAM]",
			"ioctl[IEEE80211_IOCTL_GETPARAM]",
			"ioctl[IEEE80211_IOCTL_SETKEY]",
			"ioctl[SIOCIWFIRSTPRIV+3]",
			"ioctl[IEEE80211_IOCTL_DELKEY]",
			"ioctl[SIOCIWFIRSTPRIV+5]",
			"ioctl[IEEE80211_IOCTL_SETMLME]",
			"ioctl[SIOCIWFIRSTPRIV+7]",
			"ioctl[IEEE80211_IOCTL_SETOPTIE]",
			"ioctl[IEEE80211_IOCTL_GETOPTIE]",
			"ioctl[IEEE80211_IOCTL_ADDMAC]",
			"ioctl[SIOCIWFIRSTPRIV+11]",
			"ioctl[IEEE80211_IOCTL_DELMAC]",
			"ioctl[SIOCIWFIRSTPRIV+13]",
			"ioctl[IEEE80211_IOCTL_CHANLIST]",
			"ioctl[SIOCIWFIRSTPRIV+15]",
			"ioctl[IEEE80211_IOCTL_GETRSN]",
			"ioctl[SIOCIWFIRSTPRIV+17]",
			"ioctl[IEEE80211_IOCTL_GETKEY]",
		};
#endif /* MADWIFI_NG */
		int idx = op - first;
		if (first <= op &&
		    idx < (int) (sizeof(opnames) / sizeof(opnames[0])) &&
		    opnames[idx])
			perror(opnames[idx]);
		else
			perror("ioctl[unknown???]");
		return -1;
	}
	return 0;
}

static int
set80211param(struct madwifi_driver_data *drv, int op, int arg)
{
	struct iwreq iwr;

	memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
	iwr.u.mode = op;
	memcpy(iwr.u.name+sizeof(__u32), &arg, sizeof(arg));

	if (ioctl(drv->ioctl_sock, IEEE80211_IOCTL_SETPARAM, &iwr) < 0) {
		perror("ioctl[IEEE80211_IOCTL_SETPARAM]");
		wpa_printf(MSG_DEBUG, "%s: Failed to set parameter (op %d "
			   "arg %d)", __func__, op, arg);
		return -1;
	}
	return 0;
}

#ifndef CONFIG_NO_STDOUT_DEBUG
static const char *
ether_sprintf(const u8 *addr)
{
	static char buf[sizeof(MACSTR)];

	if (addr != NULL)
		snprintf(buf, sizeof(buf), MACSTR, MAC2STR(addr));
	else
		snprintf(buf, sizeof(buf), MACSTR, 0,0,0,0,0,0);
	return buf;
}
#endif /* CONFIG_NO_STDOUT_DEBUG */

/*
 * Configure WPA parameters.
 */
static int
madwifi_configure_wpa(struct madwifi_driver_data *drv,
		      struct wpa_bss_params *params)
{
	int v;

	switch (params->wpa_group) {
	case WPA_CIPHER_CCMP:
		v = IEEE80211_CIPHER_AES_CCM;
		break;
	case WPA_CIPHER_TKIP:
		v = IEEE80211_CIPHER_TKIP;
		break;
	case WPA_CIPHER_WEP104:
		v = IEEE80211_CIPHER_WEP;
		break;
	case WPA_CIPHER_WEP40:
		v = IEEE80211_CIPHER_WEP;
		break;
	case WPA_CIPHER_NONE:
		v = IEEE80211_CIPHER_NONE;
		break;
	default:
		wpa_printf(MSG_ERROR, "Unknown group key cipher %u",
			   params->wpa_group);
		return -1;
	}
	wpa_printf(MSG_DEBUG, "%s: group key cipher=%d", __func__, v);
	if (set80211param(drv, IEEE80211_PARAM_MCASTCIPHER, v)) {
		printf("Unable to set group key cipher to %u\n", v);
		return -1;
	}
	if (v == IEEE80211_CIPHER_WEP) {
		/* key length is done only for specific ciphers */
		v = (params->wpa_group == WPA_CIPHER_WEP104 ? 13 : 5);
		if (set80211param(drv, IEEE80211_PARAM_MCASTKEYLEN, v)) {
			printf("Unable to set group key length to %u\n", v);
			return -1;
		}
	}

	v = 0;
	if (params->wpa_pairwise & WPA_CIPHER_CCMP)
		v |= 1<<IEEE80211_CIPHER_AES_CCM;
	if (params->wpa_pairwise & WPA_CIPHER_TKIP)
		v |= 1<<IEEE80211_CIPHER_TKIP;
	if (params->wpa_pairwise & WPA_CIPHER_NONE)
		v |= 1<<IEEE80211_CIPHER_NONE;
	wpa_printf(MSG_DEBUG, "%s: pairwise key ciphers=0x%x", __func__, v);
	if (set80211param(drv, IEEE80211_PARAM_UCASTCIPHERS, v)) {
		printf("Unable to set pairwise key ciphers to 0x%x\n", v);
		return -1;
	}

	wpa_printf(MSG_DEBUG, "%s: key management algorithms=0x%x",
		   __func__, params->wpa_key_mgmt);
	if (set80211param(drv, IEEE80211_PARAM_KEYMGTALGS,
			  params->wpa_key_mgmt)) {
		printf("Unable to set key management algorithms to 0x%x\n",
			params->wpa_key_mgmt);
		return -1;
	}

	v = 0;
	if (params->rsn_preauth)
		v |= BIT(0);
	wpa_printf(MSG_DEBUG, "%s: rsn capabilities=0x%x",
		   __func__, params->rsn_preauth);
	if (set80211param(drv, IEEE80211_PARAM_RSNCAPS, v)) {
		printf("Unable to set RSN capabilities to 0x%x\n", v);
		return -1;
	}

	wpa_printf(MSG_DEBUG, "%s: enable WPA=0x%x", __func__, params->wpa);
	if (set80211param(drv, IEEE80211_PARAM_WPA, params->wpa)) {
		printf("Unable to set WPA to %u\n", params->wpa);
		return -1;
	}
	return 0;
}

static int
madwifi_set_ieee8021x(void *priv, struct wpa_bss_params *params)
{
	struct madwifi_driver_data *drv = priv;

	wpa_printf(MSG_DEBUG, "%s: enabled=%d", __func__, params->enabled);

	if (!params->enabled) {
		/* XXX restore state */
		return set80211param(priv, IEEE80211_PARAM_AUTHMODE,
			IEEE80211_AUTH_AUTO);
	}
	if (!params->wpa && !params->ieee802_1x) {
		hostapd_logger(drv->hapd, NULL, HOSTAPD_MODULE_DRIVER,
			HOSTAPD_LEVEL_WARNING, "No 802.1X or WPA enabled!");
		return -1;
	}
	if (params->wpa && madwifi_configure_wpa(drv, params) != 0) {
		hostapd_logger(drv->hapd, NULL, HOSTAPD_MODULE_DRIVER,
			HOSTAPD_LEVEL_WARNING, "Error configuring WPA state!");
		return -1;
	}
	if (set80211param(priv, IEEE80211_PARAM_AUTHMODE,
		(params->wpa ? IEEE80211_AUTH_WPA : IEEE80211_AUTH_8021X))) {
		hostapd_logger(drv->hapd, NULL, HOSTAPD_MODULE_DRIVER,
			HOSTAPD_LEVEL_WARNING, "Error enabling WPA/802.1X!");
		return -1;
	}

	return 0;
}

static int
madwifi_set_privacy(void *priv, int enabled)
{
	struct madwifi_driver_data *drv = priv;

	wpa_printf(MSG_DEBUG, "%s: enabled=%d", __func__, enabled);

	return set80211param(drv, IEEE80211_PARAM_PRIVACY, enabled);
}

static int
madwifi_set_sta_authorized(void *priv, const u8 *addr, int authorized)
{
	struct madwifi_driver_data *drv = priv;
	struct ieee80211req_mlme mlme;
	int ret;

	wpa_printf(MSG_DEBUG, "%s: addr=%s authorized=%d",
		   __func__, ether_sprintf(addr), authorized);

	if (authorized)
		mlme.im_op = IEEE80211_MLME_AUTHORIZE;
	else
		mlme.im_op = IEEE80211_MLME_UNAUTHORIZE;
	mlme.im_reason = 0;
	memcpy(mlme.im_macaddr, addr, IEEE80211_ADDR_LEN);
	ret = set80211priv(drv, IEEE80211_IOCTL_SETMLME, &mlme, sizeof(mlme));
	if (ret < 0) {
		wpa_printf(MSG_DEBUG, "%s: Failed to %sauthorize STA " MACSTR,
			   __func__, authorized ? "" : "un", MAC2STR(addr));
	}

	return ret;
}

static int
madwifi_sta_set_flags(void *priv, const u8 *addr,
		      int total_flags, int flags_or, int flags_and)
{
	/* For now, only support setting Authorized flag */
	if (flags_or & WPA_STA_AUTHORIZED)
		return madwifi_set_sta_authorized(priv, addr, 1);
	if (!(flags_and & WPA_STA_AUTHORIZED))
		return madwifi_set_sta_authorized(priv, addr, 0);
	return 0;
}

static int
madwifi_del_key(void *priv, const u8 *addr, int key_idx)
{
	struct madwifi_driver_data *drv = priv;
	struct ieee80211req_del_key wk;
	int ret;

	wpa_printf(MSG_DEBUG, "%s: addr=%s key_idx=%d",
		   __func__, ether_sprintf(addr), key_idx);

	memset(&wk, 0, sizeof(wk));
	if (addr != NULL) {
		memcpy(wk.idk_macaddr, addr, IEEE80211_ADDR_LEN);
		wk.idk_keyix = (u8) IEEE80211_KEYIX_NONE;
	} else {
		wk.idk_keyix = key_idx;
	}

	ret = set80211priv(drv, IEEE80211_IOCTL_DELKEY, &wk, sizeof(wk));
	if (ret < 0) {
		wpa_printf(MSG_DEBUG, "%s: Failed to delete key (addr %s"
			   " key_idx %d)", __func__, ether_sprintf(addr),
			   key_idx);
	}

	return ret;
}

static int
wpa_driver_madwifi_set_key(const char *ifname, void *priv, enum wpa_alg alg,
			   const u8 *addr, int key_idx, int set_tx,
			   const u8 *seq, size_t seq_len,
			   const u8 *key, size_t key_len)
{
	struct madwifi_driver_data *drv = priv;
	struct ieee80211req_key wk;
	u_int8_t cipher;
	int ret;

	if (alg == WPA_ALG_NONE)
		return madwifi_del_key(drv, addr, key_idx);

	wpa_printf(MSG_DEBUG, "%s: alg=%d addr=%s key_idx=%d",
		   __func__, alg, ether_sprintf(addr), key_idx);

	if (alg == WPA_ALG_WEP)
		cipher = IEEE80211_CIPHER_WEP;
	else if (alg == WPA_ALG_TKIP)
		cipher = IEEE80211_CIPHER_TKIP;
	else if (alg == WPA_ALG_CCMP)
		cipher = IEEE80211_CIPHER_AES_CCM;
	else {
		printf("%s: unknown/unsupported algorithm %d\n",
			__func__, alg);
		return -1;
	}

	if (key_len > sizeof(wk.ik_keydata)) {
		printf("%s: key length %lu too big\n", __func__,
		       (unsigned long) key_len);
		return -3;
	}

	memset(&wk, 0, sizeof(wk));
	wk.ik_type = cipher;
	wk.ik_flags = IEEE80211_KEY_RECV | IEEE80211_KEY_XMIT;
	if (addr == NULL || is_broadcast_ether_addr(addr)) {
		memset(wk.ik_macaddr, 0xff, IEEE80211_ADDR_LEN);
		wk.ik_keyix = key_idx;
		wk.ik_flags |= IEEE80211_KEY_DEFAULT;
	} else {
		memcpy(wk.ik_macaddr, addr, IEEE80211_ADDR_LEN);
		wk.ik_keyix = IEEE80211_KEYIX_NONE;
	}
	wk.ik_keylen = key_len;
	memcpy(wk.ik_keydata, key, key_len);

	ret = set80211priv(drv, IEEE80211_IOCTL_SETKEY, &wk, sizeof(wk));
	if (ret < 0) {
		wpa_printf(MSG_DEBUG, "%s: Failed to set key (addr %s"
			   " key_idx %d alg %d key_len %lu set_tx %d)",
			   __func__, ether_sprintf(wk.ik_macaddr), key_idx,
			   alg, (unsigned long) key_len, set_tx);
	}

	return ret;
}


static int
madwifi_get_seqnum(const char *ifname, void *priv, const u8 *addr, int idx,
		   u8 *seq)
{
	struct madwifi_driver_data *drv = priv;
	struct ieee80211req_key wk;

	wpa_printf(MSG_DEBUG, "%s: addr=%s idx=%d",
		   __func__, ether_sprintf(addr), idx);

	memset(&wk, 0, sizeof(wk));
	if (addr == NULL)
		memset(wk.ik_macaddr, 0xff, IEEE80211_ADDR_LEN);
	else
		memcpy(wk.ik_macaddr, addr, IEEE80211_ADDR_LEN);
	wk.ik_keyix = idx;

	if (set80211priv(drv, IEEE80211_IOCTL_GETKEY, &wk, sizeof(wk))) {
		wpa_printf(MSG_DEBUG, "%s: Failed to get encryption data "
			   "(addr " MACSTR " key_idx %d)",
			   __func__, MAC2STR(wk.ik_macaddr), idx);
		return -1;
	}

#ifdef WORDS_BIGENDIAN
	{
		/*
		 * wk.ik_keytsc is in host byte order (big endian), need to
		 * swap it to match with the byte order used in WPA.
		 */
		int i;
		u8 tmp[WPA_KEY_RSC_LEN];
		memcpy(tmp, &wk.ik_keytsc, sizeof(wk.ik_keytsc));
		for (i = 0; i < WPA_KEY_RSC_LEN; i++) {
			seq[i] = tmp[WPA_KEY_RSC_LEN - i - 1];
		}
	}
#else /* WORDS_BIGENDIAN */
	memcpy(seq, &wk.ik_keytsc, sizeof(wk.ik_keytsc));
#endif /* WORDS_BIGENDIAN */
	return 0;
}


static int 
madwifi_flush(void *priv)
{
#ifdef MADWIFI_BSD
	u8 allsta[IEEE80211_ADDR_LEN];
	memset(allsta, 0xff, IEEE80211_ADDR_LEN);
	return madwifi_sta_deauth(priv, NULL, allsta,
				  IEEE80211_REASON_AUTH_LEAVE);
#else /* MADWIFI_BSD */
	return 0;		/* XXX */
#endif /* MADWIFI_BSD */
}


static int
madwifi_read_sta_driver_data(void *priv, struct hostap_sta_driver_data *data,
			     const u8 *addr)
{
	struct madwifi_driver_data *drv = priv;

#ifdef MADWIFI_BSD
	struct ieee80211req_sta_stats stats;

	memset(data, 0, sizeof(*data));

	/*
	 * Fetch statistics for station from the system.
	 */
	memset(&stats, 0, sizeof(stats));
	memcpy(stats.is_u.macaddr, addr, IEEE80211_ADDR_LEN);
	if (set80211priv(drv,
#ifdef MADWIFI_NG
			 IEEE80211_IOCTL_STA_STATS,
#else /* MADWIFI_NG */
			 IEEE80211_IOCTL_GETSTASTATS,
#endif /* MADWIFI_NG */
			 &stats, sizeof(stats))) {
		wpa_printf(MSG_DEBUG, "%s: Failed to fetch STA stats (addr "
			   MACSTR ")", __func__, MAC2STR(addr));
		if (memcmp(addr, drv->acct_mac, ETH_ALEN) == 0) {
			memcpy(data, &drv->acct_data, sizeof(*data));
			return 0;
		}

		printf("Failed to get station stats information element.\n");
		return -1;
	}

	data->rx_packets = stats.is_stats.ns_rx_data;
	data->rx_bytes = stats.is_stats.ns_rx_bytes;
	data->tx_packets = stats.is_stats.ns_tx_data;
	data->tx_bytes = stats.is_stats.ns_tx_bytes;
	return 0;

#else /* MADWIFI_BSD */

	char buf[1024], line[128], *pos;
	FILE *f;
	unsigned long val;

	memset(data, 0, sizeof(*data));
	snprintf(buf, sizeof(buf), "/proc/net/madwifi/%s/" MACSTR,
		 drv->iface, MAC2STR(addr));

	f = fopen(buf, "r");
	if (!f) {
		if (memcmp(addr, drv->acct_mac, ETH_ALEN) != 0)
			return -1;
		memcpy(data, &drv->acct_data, sizeof(*data));
		return 0;
	}
	/* Need to read proc file with in one piece, so use large enough
	 * buffer. */
	setbuffer(f, buf, sizeof(buf));

	while (fgets(line, sizeof(line), f)) {
		pos = strchr(line, '=');
		if (!pos)
			continue;
		*pos++ = '\0';
		val = strtoul(pos, NULL, 10);
		if (strcmp(line, "rx_packets") == 0)
			data->rx_packets = val;
		else if (strcmp(line, "tx_packets") == 0)
			data->tx_packets = val;
		else if (strcmp(line, "rx_bytes") == 0)
			data->rx_bytes = val;
		else if (strcmp(line, "tx_bytes") == 0)
			data->tx_bytes = val;
	}

	fclose(f);

	return 0;
#endif /* MADWIFI_BSD */
}


static int
madwifi_sta_clear_stats(void *priv, const u8 *addr)
{
#if defined(MADWIFI_BSD) && defined(IEEE80211_MLME_CLEAR_STATS)
	struct madwifi_driver_data *drv = priv;
	struct ieee80211req_mlme mlme;
	int ret;

	wpa_printf(MSG_DEBUG, "%s: addr=%s", __func__, ether_sprintf(addr));

	mlme.im_op = IEEE80211_MLME_CLEAR_STATS;
	memcpy(mlme.im_macaddr, addr, IEEE80211_ADDR_LEN);
	ret = set80211priv(drv, IEEE80211_IOCTL_SETMLME, &mlme,
			   sizeof(mlme));
	if (ret < 0) {
		wpa_printf(MSG_DEBUG, "%s: Failed to clear STA stats (addr "
			   MACSTR ")", __func__, MAC2STR(addr));
	}

	return ret;
#else /* MADWIFI_BSD && IEEE80211_MLME_CLEAR_STATS */
	return 0; /* FIX */
#endif /* MADWIFI_BSD && IEEE80211_MLME_CLEAR_STATS */
}


static int
madwifi_set_opt_ie(void *priv, const u8 *ie, size_t ie_len)
{
	/*
	 * Do nothing; we setup parameters at startup that define the
	 * contents of the beacon information element.
	 */
	return 0;
}

static int
madwifi_sta_deauth(void *priv, const u8 *own_addr, const u8 *addr,
		   int reason_code)
{
	struct madwifi_driver_data *drv = priv;
	struct ieee80211req_mlme mlme;
	int ret;

	wpa_printf(MSG_DEBUG, "%s: addr=%s reason_code=%d",
		   __func__, ether_sprintf(addr), reason_code);

	mlme.im_op = IEEE80211_MLME_DEAUTH;
	mlme.im_reason = reason_code;
	memcpy(mlme.im_macaddr, addr, IEEE80211_ADDR_LEN);
	ret = set80211priv(drv, IEEE80211_IOCTL_SETMLME, &mlme, sizeof(mlme));
	if (ret < 0) {
		wpa_printf(MSG_DEBUG, "%s: Failed to deauth STA (addr " MACSTR
			   " reason %d)",
			   __func__, MAC2STR(addr), reason_code);
	}

	return ret;
}

static int
madwifi_sta_disassoc(void *priv, const u8 *own_addr, const u8 *addr,
		     int reason_code)
{
	struct madwifi_driver_data *drv = priv;
	struct ieee80211req_mlme mlme;
	int ret;

	wpa_printf(MSG_DEBUG, "%s: addr=%s reason_code=%d",
		   __func__, ether_sprintf(addr), reason_code);

	mlme.im_op = IEEE80211_MLME_DISASSOC;
	mlme.im_reason = reason_code;
	memcpy(mlme.im_macaddr, addr, IEEE80211_ADDR_LEN);
	ret = set80211priv(drv, IEEE80211_IOCTL_SETMLME, &mlme, sizeof(mlme));
	if (ret < 0) {
		wpa_printf(MSG_DEBUG, "%s: Failed to disassoc STA (addr "
			   MACSTR " reason %d)",
			   __func__, MAC2STR(addr), reason_code);
	}

	return ret;
}

#ifdef CONFIG_WPS
#ifdef IEEE80211_IOCTL_FILTERFRAME
static void madwifi_raw_receive(void *ctx, const u8 *src_addr, const u8 *buf,
				size_t len)
{
	struct madwifi_driver_data *drv = ctx;
	const struct ieee80211_mgmt *mgmt;
	u16 fc;
	union wpa_event_data event;

	/* Send Probe Request information to WPS processing */

	if (len < IEEE80211_HDRLEN + sizeof(mgmt->u.probe_req))
		return;
	mgmt = (const struct ieee80211_mgmt *) buf;

	fc = le_to_host16(mgmt->frame_control);
	if (WLAN_FC_GET_TYPE(fc) != WLAN_FC_TYPE_MGMT ||
	    WLAN_FC_GET_STYPE(fc) != WLAN_FC_STYPE_PROBE_REQ)
		return;

	os_memset(&event, 0, sizeof(event));
	event.rx_probe_req.sa = mgmt->sa;
	event.rx_probe_req.ie = mgmt->u.probe_req.variable;
	event.rx_probe_req.ie_len =
		len - (IEEE80211_HDRLEN + sizeof(mgmt->u.probe_req));
	wpa_supplicant_event(drv->hapd, EVENT_RX_PROBE_REQ, &event);
}
#endif /* IEEE80211_IOCTL_FILTERFRAME */
#endif /* CONFIG_WPS */

static int madwifi_receive_probe_req(struct madwifi_driver_data *drv)
{
	int ret = 0;
#ifdef CONFIG_WPS
#ifdef IEEE80211_IOCTL_FILTERFRAME
	struct ieee80211req_set_filter filt;

	wpa_printf(MSG_DEBUG, "%s Enter", __func__);
	filt.app_filterype = IEEE80211_FILTER_TYPE_PROBE_REQ;

	ret = set80211priv(drv, IEEE80211_IOCTL_FILTERFRAME, &filt,
			   sizeof(struct ieee80211req_set_filter));
	if (ret)
		return ret;

	drv->sock_raw = l2_packet_init(drv->iface, NULL, ETH_P_80211_RAW,
				       madwifi_raw_receive, drv, 1);
	if (drv->sock_raw == NULL)
		return -1;
#endif /* IEEE80211_IOCTL_FILTERFRAME */
#endif /* CONFIG_WPS */
	return ret;
}

#ifdef CONFIG_WPS
static int
madwifi_set_wps_ie(void *priv, const u8 *ie, size_t len, u32 frametype)
{
	struct madwifi_driver_data *drv = priv;
	u8 buf[256];
	struct ieee80211req_getset_appiebuf *beac_ie;

	wpa_printf(MSG_DEBUG, "%s buflen = %lu", __func__,
		   (unsigned long) len);

	beac_ie = (struct ieee80211req_getset_appiebuf *) buf;
	beac_ie->app_frmtype = frametype;
	beac_ie->app_buflen = len;
	memcpy(&(beac_ie->app_buf[0]), ie, len);

	return set80211priv(drv, IEEE80211_IOCTL_SET_APPIEBUF, beac_ie,
			    sizeof(struct ieee80211req_getset_appiebuf) + len);
}

static int
madwifi_set_ap_wps_ie(void *priv, const struct wpabuf *beacon,
		      const struct wpabuf *proberesp,
		      const struct wpabuf *assocresp)
{
	if (madwifi_set_wps_ie(priv, beacon ? wpabuf_head(beacon) : NULL,
			       beacon ? wpabuf_len(beacon) : 0,
			       IEEE80211_APPIE_FRAME_BEACON) < 0)
		return -1;
	return madwifi_set_wps_ie(priv,
				  proberesp ? wpabuf_head(proberesp) : NULL,
				  proberesp ? wpabuf_len(proberesp) : 0,
				  IEEE80211_APPIE_FRAME_PROBE_RESP);
}
#else /* CONFIG_WPS */
#define madwifi_set_ap_wps_ie NULL
#endif /* CONFIG_WPS */

static void
madwifi_new_sta(struct madwifi_driver_data *drv, u8 addr[IEEE80211_ADDR_LEN])
{
	struct hostapd_data *hapd = drv->hapd;
	struct ieee80211req_wpaie ie;
	int ielen = 0;
	u8 *iebuf = NULL;

	/*
	 * Fetch negotiated WPA/RSN parameters from the system.
	 */
	memset(&ie, 0, sizeof(ie));
	memcpy(ie.wpa_macaddr, addr, IEEE80211_ADDR_LEN);
	if (set80211priv(drv, IEEE80211_IOCTL_GETWPAIE, &ie, sizeof(ie))) {
		wpa_printf(MSG_DEBUG, "%s: Failed to get WPA/RSN IE",
			   __func__);
		goto no_ie;
	}
	wpa_hexdump(MSG_MSGDUMP, "madwifi req WPA IE",
		    ie.wpa_ie, IEEE80211_MAX_OPT_IE);
	iebuf = ie.wpa_ie;
	/* madwifi seems to return some random data if WPA/RSN IE is not set.
	 * Assume the IE was not included if the IE type is unknown. */
	if (iebuf[0] != WLAN_EID_VENDOR_SPECIFIC)
		iebuf[1] = 0;
#ifdef MADWIFI_NG
	wpa_hexdump(MSG_MSGDUMP, "madwifi req RSN IE",
		    ie.rsn_ie, IEEE80211_MAX_OPT_IE);
	if (iebuf[1] == 0 && ie.rsn_ie[1] > 0) {
		/* madwifi-ng svn #1453 added rsn_ie. Use it, if wpa_ie was not
		 * set. This is needed for WPA2. */
		iebuf = ie.rsn_ie;
		if (iebuf[0] != WLAN_EID_RSN)
			iebuf[1] = 0;
	}
#endif /* MADWIFI_NG */

	ielen = iebuf[1];
	if (ielen == 0)
		iebuf = NULL;
	else
		ielen += 2;

no_ie:
	drv_event_assoc(hapd, addr, iebuf, ielen, 0);

	if (memcmp(addr, drv->acct_mac, ETH_ALEN) == 0) {
		/* Cached accounting data is not valid anymore. */
		memset(drv->acct_mac, 0, ETH_ALEN);
		memset(&drv->acct_data, 0, sizeof(drv->acct_data));
	}
}

static void
madwifi_wireless_event_wireless_custom(struct madwifi_driver_data *drv,
				       char *custom)
{
	wpa_printf(MSG_DEBUG, "Custom wireless event: '%s'", custom);

	if (strncmp(custom, "MLME-MICHAELMICFAILURE.indication", 33) == 0) {
		char *pos;
		u8 addr[ETH_ALEN];
		pos = strstr(custom, "addr=");
		if (pos == NULL) {
			wpa_printf(MSG_DEBUG,
				   "MLME-MICHAELMICFAILURE.indication "
				   "without sender address ignored");
			return;
		}
		pos += 5;
		if (hwaddr_aton(pos, addr) == 0) {
			union wpa_event_data data;
			os_memset(&data, 0, sizeof(data));
			data.michael_mic_failure.unicast = 1;
			data.michael_mic_failure.src = addr;
			wpa_supplicant_event(drv->hapd,
					     EVENT_MICHAEL_MIC_FAILURE, &data);
		} else {
			wpa_printf(MSG_DEBUG,
				   "MLME-MICHAELMICFAILURE.indication "
				   "with invalid MAC address");
		}
	} else if (strncmp(custom, "STA-TRAFFIC-STAT", 16) == 0) {
		char *key, *value;
		u32 val;
		key = custom;
		while ((key = strchr(key, '\n')) != NULL) {
			key++;
			value = strchr(key, '=');
			if (value == NULL)
				continue;
			*value++ = '\0';
			val = strtoul(value, NULL, 10);
			if (strcmp(key, "mac") == 0)
				hwaddr_aton(value, drv->acct_mac);
			else if (strcmp(key, "rx_packets") == 0)
				drv->acct_data.rx_packets = val;
			else if (strcmp(key, "tx_packets") == 0)
				drv->acct_data.tx_packets = val;
			else if (strcmp(key, "rx_bytes") == 0)
				drv->acct_data.rx_bytes = val;
			else if (strcmp(key, "tx_bytes") == 0)
				drv->acct_data.tx_bytes = val;
			key = value;
		}
	}
}

static void
madwifi_wireless_event_wireless(struct madwifi_driver_data *drv,
					    char *data, int len)
{
	struct iw_event iwe_buf, *iwe = &iwe_buf;
	char *pos, *end, *custom, *buf;

	pos = data;
	end = data + len;

	while (pos + IW_EV_LCP_LEN <= end) {
		/* Event data may be unaligned, so make a local, aligned copy
		 * before processing. */
		memcpy(&iwe_buf, pos, IW_EV_LCP_LEN);
		wpa_printf(MSG_MSGDUMP, "Wireless event: cmd=0x%x len=%d",
			   iwe->cmd, iwe->len);
		if (iwe->len <= IW_EV_LCP_LEN)
			return;

		custom = pos + IW_EV_POINT_LEN;
		if (drv->we_version > 18 &&
		    (iwe->cmd == IWEVMICHAELMICFAILURE ||
		     iwe->cmd == IWEVCUSTOM)) {
			/* WE-19 removed the pointer from struct iw_point */
			char *dpos = (char *) &iwe_buf.u.data.length;
			int dlen = dpos - (char *) &iwe_buf;
			memcpy(dpos, pos + IW_EV_LCP_LEN,
			       sizeof(struct iw_event) - dlen);
		} else {
			memcpy(&iwe_buf, pos, sizeof(struct iw_event));
			custom += IW_EV_POINT_OFF;
		}

		switch (iwe->cmd) {
		case IWEVEXPIRED:
			drv_event_disassoc(drv->hapd,
					   (u8 *) iwe->u.addr.sa_data);
			break;
		case IWEVREGISTERED:
			madwifi_new_sta(drv, (u8 *) iwe->u.addr.sa_data);
			break;
		case IWEVCUSTOM:
			if (custom + iwe->u.data.length > end)
				return;
			buf = malloc(iwe->u.data.length + 1);
			if (buf == NULL)
				return;		/* XXX */
			memcpy(buf, custom, iwe->u.data.length);
			buf[iwe->u.data.length] = '\0';
			madwifi_wireless_event_wireless_custom(drv, buf);
			free(buf);
			break;
		}

		pos += iwe->len;
	}
}


static void
madwifi_wireless_event_rtm_newlink(void *ctx, struct ifinfomsg *ifi,
				   u8 *buf, size_t len)
{
	struct madwifi_driver_data *drv = ctx;
	int attrlen, rta_len;
	struct rtattr *attr;

	if (ifi->ifi_index != drv->ifindex)
		return;

	attrlen = len;
	attr = (struct rtattr *) buf;

	rta_len = RTA_ALIGN(sizeof(struct rtattr));
	while (RTA_OK(attr, attrlen)) {
		if (attr->rta_type == IFLA_WIRELESS) {
			madwifi_wireless_event_wireless(
				drv, ((char *) attr) + rta_len,
				attr->rta_len - rta_len);
		}
		attr = RTA_NEXT(attr, attrlen);
	}
}


static int
madwifi_get_we_version(struct madwifi_driver_data *drv)
{
	struct iw_range *range;
	struct iwreq iwr;
	int minlen;
	size_t buflen;

	drv->we_version = 0;

	/*
	 * Use larger buffer than struct iw_range in order to allow the
	 * structure to grow in the future.
	 */
	buflen = sizeof(struct iw_range) + 500;
	range = os_zalloc(buflen);
	if (range == NULL)
		return -1;

	memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
	iwr.u.data.pointer = (caddr_t) range;
	iwr.u.data.length = buflen;

	minlen = ((char *) &range->enc_capa) - (char *) range +
		sizeof(range->enc_capa);

	if (ioctl(drv->ioctl_sock, SIOCGIWRANGE, &iwr) < 0) {
		perror("ioctl[SIOCGIWRANGE]");
		free(range);
		return -1;
	} else if (iwr.u.data.length >= minlen &&
		   range->we_version_compiled >= 18) {
		wpa_printf(MSG_DEBUG, "SIOCGIWRANGE: WE(compiled)=%d "
			   "WE(source)=%d enc_capa=0x%x",
			   range->we_version_compiled,
			   range->we_version_source,
			   range->enc_capa);
		drv->we_version = range->we_version_compiled;
	}

	free(range);
	return 0;
}


static int
madwifi_wireless_event_init(struct madwifi_driver_data *drv)
{
	struct netlink_config *cfg;

	madwifi_get_we_version(drv);

	cfg = os_zalloc(sizeof(*cfg));
	if (cfg == NULL)
		return -1;
	cfg->ctx = drv;
	cfg->newlink_cb = madwifi_wireless_event_rtm_newlink;
	drv->netlink = netlink_init(cfg);
	if (drv->netlink == NULL) {
		os_free(cfg);
		return -1;
	}

	return 0;
}


static int
madwifi_send_eapol(void *priv, const u8 *addr, const u8 *data, size_t data_len,
		   int encrypt, const u8 *own_addr, u32 flags)
{
	struct madwifi_driver_data *drv = priv;
	unsigned char buf[3000];
	unsigned char *bp = buf;
	struct l2_ethhdr *eth;
	size_t len;
	int status;

	/*
	 * Prepend the Ethernet header.  If the caller left us
	 * space at the front we could just insert it but since
	 * we don't know we copy to a local buffer.  Given the frequency
	 * and size of frames this probably doesn't matter.
	 */
	len = data_len + sizeof(struct l2_ethhdr);
	if (len > sizeof(buf)) {
		bp = malloc(len);
		if (bp == NULL) {
			printf("EAPOL frame discarded, cannot malloc temp "
			       "buffer of size %lu!\n", (unsigned long) len);
			return -1;
		}
	}
	eth = (struct l2_ethhdr *) bp;
	memcpy(eth->h_dest, addr, ETH_ALEN);
	memcpy(eth->h_source, own_addr, ETH_ALEN);
	eth->h_proto = host_to_be16(ETH_P_EAPOL);
	memcpy(eth+1, data, data_len);

	wpa_hexdump(MSG_MSGDUMP, "TX EAPOL", bp, len);

	status = l2_packet_send(drv->sock_xmit, addr, ETH_P_EAPOL, bp, len);

	if (bp != buf)
		free(bp);
	return status;
}

static void
handle_read(void *ctx, const u8 *src_addr, const u8 *buf, size_t len)
{
	struct madwifi_driver_data *drv = ctx;
	drv_event_eapol_rx(drv->hapd, src_addr, buf + sizeof(struct l2_ethhdr),
			   len - sizeof(struct l2_ethhdr));
}

static void *
madwifi_init(struct hostapd_data *hapd, struct wpa_init_params *params)
{
	struct madwifi_driver_data *drv;
	struct ifreq ifr;
	struct iwreq iwr;
	char brname[IFNAMSIZ];

	drv = os_zalloc(sizeof(struct madwifi_driver_data));
	if (drv == NULL) {
		printf("Could not allocate memory for madwifi driver data\n");
		return NULL;
	}

	drv->hapd = hapd;
	drv->ioctl_sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (drv->ioctl_sock < 0) {
		perror("socket[PF_INET,SOCK_DGRAM]");
		goto bad;
	}
	memcpy(drv->iface, params->ifname, sizeof(drv->iface));

	memset(&ifr, 0, sizeof(ifr));
	os_strlcpy(ifr.ifr_name, drv->iface, sizeof(ifr.ifr_name));
	if (ioctl(drv->ioctl_sock, SIOCGIFINDEX, &ifr) != 0) {
		perror("ioctl(SIOCGIFINDEX)");
		goto bad;
	}
	drv->ifindex = ifr.ifr_ifindex;

	drv->sock_xmit = l2_packet_init(drv->iface, NULL, ETH_P_EAPOL,
					handle_read, drv, 1);
	if (drv->sock_xmit == NULL)
		goto bad;
	if (l2_packet_get_own_addr(drv->sock_xmit, params->own_addr))
		goto bad;
	if (params->bridge[0]) {
		wpa_printf(MSG_DEBUG, "Configure bridge %s for EAPOL traffic.",
			   params->bridge[0]);
		drv->sock_recv = l2_packet_init(params->bridge[0], NULL,
						ETH_P_EAPOL, handle_read, drv,
						1);
		if (drv->sock_recv == NULL)
			goto bad;
	} else if (linux_br_get(brname, drv->iface) == 0) {
		wpa_printf(MSG_DEBUG, "Interface in bridge %s; configure for "
			   "EAPOL receive", brname);
		drv->sock_recv = l2_packet_init(brname, NULL, ETH_P_EAPOL,
						handle_read, drv, 1);
		if (drv->sock_recv == NULL)
			goto bad;
	} else
		drv->sock_recv = drv->sock_xmit;

	memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->iface, IFNAMSIZ);

	iwr.u.mode = IW_MODE_MASTER;

	if (ioctl(drv->ioctl_sock, SIOCSIWMODE, &iwr) < 0) {
		perror("ioctl[SIOCSIWMODE]");
		printf("Could not set interface to master mode!\n");
		goto bad;
	}

	/* mark down during setup */
	linux_set_iface_flags(drv->ioctl_sock, drv->iface, 0);
	madwifi_set_privacy(drv, 0); /* default to no privacy */

	madwifi_receive_probe_req(drv);

	if (madwifi_wireless_event_init(drv))
		goto bad;

	return drv;
bad:
	if (drv->sock_xmit != NULL)
		l2_packet_deinit(drv->sock_xmit);
	if (drv->ioctl_sock >= 0)
		close(drv->ioctl_sock);
	if (drv != NULL)
		free(drv);
	return NULL;
}


static void
madwifi_deinit(void *priv)
{
	struct madwifi_driver_data *drv = priv;

	netlink_deinit(drv->netlink);
	(void) linux_set_iface_flags(drv->ioctl_sock, drv->iface, 0);
	if (drv->ioctl_sock >= 0)
		close(drv->ioctl_sock);
	if (drv->sock_recv != NULL && drv->sock_recv != drv->sock_xmit)
		l2_packet_deinit(drv->sock_recv);
	if (drv->sock_xmit != NULL)
		l2_packet_deinit(drv->sock_xmit);
	if (drv->sock_raw)
		l2_packet_deinit(drv->sock_raw);
	free(drv);
}

static int
madwifi_set_ssid(void *priv, const u8 *buf, int len)
{
	struct madwifi_driver_data *drv = priv;
	struct iwreq iwr;

	memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
	iwr.u.essid.flags = 1; /* SSID active */
	iwr.u.essid.pointer = (caddr_t) buf;
	iwr.u.essid.length = len + 1;

	if (ioctl(drv->ioctl_sock, SIOCSIWESSID, &iwr) < 0) {
		perror("ioctl[SIOCSIWESSID]");
		printf("len=%d\n", len);
		return -1;
	}
	return 0;
}

static int
madwifi_get_ssid(void *priv, u8 *buf, int len)
{
	struct madwifi_driver_data *drv = priv;
	struct iwreq iwr;
	int ret = 0;

	memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
	iwr.u.essid.pointer = (caddr_t) buf;
	iwr.u.essid.length = len;

	if (ioctl(drv->ioctl_sock, SIOCGIWESSID, &iwr) < 0) {
		perror("ioctl[SIOCGIWESSID]");
		ret = -1;
	} else
		ret = iwr.u.essid.length;

	return ret;
}

static int
madwifi_set_countermeasures(void *priv, int enabled)
{
	struct madwifi_driver_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s: enabled=%d", __FUNCTION__, enabled);
	return set80211param(drv, IEEE80211_PARAM_COUNTERMEASURES, enabled);
}

static int
madwifi_commit(void *priv)
{
	struct madwifi_driver_data *drv = priv;
	return linux_set_iface_flags(drv->ioctl_sock, drv->iface, 1);
}

#else /* HOSTAPD */

struct wpa_driver_madwifi_data {
	void *wext; /* private data for driver_wext */
	void *ctx;
	char ifname[IFNAMSIZ + 1];
	int sock;
};

static int wpa_driver_madwifi_set_auth_alg(void *priv, int auth_alg);
static int wpa_driver_madwifi_set_probe_req_ie(void *priv, const u8 *ies,
					       size_t ies_len);


static int
set80211priv(struct wpa_driver_madwifi_data *drv, int op, void *data, int len,
	     int show_err)
{
	struct iwreq iwr;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	if (len < IFNAMSIZ &&
	    op != IEEE80211_IOCTL_SET_APPIEBUF) {
		/*
		 * Argument data fits inline; put it there.
		 */
		os_memcpy(iwr.u.name, data, len);
	} else {
		/*
		 * Argument data too big for inline transfer; setup a
		 * parameter block instead; the kernel will transfer
		 * the data for the driver.
		 */
		iwr.u.data.pointer = data;
		iwr.u.data.length = len;
	}

	if (ioctl(drv->sock, op, &iwr) < 0) {
		if (show_err) {
#ifdef MADWIFI_NG
			int first = IEEE80211_IOCTL_SETPARAM;
			int last = IEEE80211_IOCTL_KICKMAC;
			static const char *opnames[] = {
				"ioctl[IEEE80211_IOCTL_SETPARAM]",
				"ioctl[IEEE80211_IOCTL_GETPARAM]",
				"ioctl[IEEE80211_IOCTL_SETMODE]",
				"ioctl[IEEE80211_IOCTL_GETMODE]",
				"ioctl[IEEE80211_IOCTL_SETWMMPARAMS]",
				"ioctl[IEEE80211_IOCTL_GETWMMPARAMS]",
				"ioctl[IEEE80211_IOCTL_SETCHANLIST]",
				"ioctl[IEEE80211_IOCTL_GETCHANLIST]",
				"ioctl[IEEE80211_IOCTL_CHANSWITCH]",
				NULL,
				"ioctl[IEEE80211_IOCTL_SET_APPIEBUF]",
				"ioctl[IEEE80211_IOCTL_GETSCANRESULTS]",
				NULL,
				"ioctl[IEEE80211_IOCTL_GETCHANINFO]",
				"ioctl[IEEE80211_IOCTL_SETOPTIE]",
				"ioctl[IEEE80211_IOCTL_GETOPTIE]",
				"ioctl[IEEE80211_IOCTL_SETMLME]",
				NULL,
				"ioctl[IEEE80211_IOCTL_SETKEY]",
				NULL,
				"ioctl[IEEE80211_IOCTL_DELKEY]",
				NULL,
				"ioctl[IEEE80211_IOCTL_ADDMAC]",
				NULL,
				"ioctl[IEEE80211_IOCTL_DELMAC]",
				NULL,
				"ioctl[IEEE80211_IOCTL_WDSMAC]",
				NULL,
				"ioctl[IEEE80211_IOCTL_WDSDELMAC]",
				NULL,
				"ioctl[IEEE80211_IOCTL_KICKMAC]",
			};
#else /* MADWIFI_NG */
			int first = IEEE80211_IOCTL_SETPARAM;
			int last = IEEE80211_IOCTL_CHANLIST;
			static const char *opnames[] = {
				"ioctl[IEEE80211_IOCTL_SETPARAM]",
				"ioctl[IEEE80211_IOCTL_GETPARAM]",
				"ioctl[IEEE80211_IOCTL_SETKEY]",
				"ioctl[IEEE80211_IOCTL_GETKEY]",
				"ioctl[IEEE80211_IOCTL_DELKEY]",
				NULL,
				"ioctl[IEEE80211_IOCTL_SETMLME]",
				NULL,
				"ioctl[IEEE80211_IOCTL_SETOPTIE]",
				"ioctl[IEEE80211_IOCTL_GETOPTIE]",
				"ioctl[IEEE80211_IOCTL_ADDMAC]",
				NULL,
				"ioctl[IEEE80211_IOCTL_DELMAC]",
				NULL,
				"ioctl[IEEE80211_IOCTL_CHANLIST]",
			};
#endif /* MADWIFI_NG */
			int idx = op - first;
			if (first <= op && op <= last &&
			    idx < (int) (sizeof(opnames) / sizeof(opnames[0]))
			    && opnames[idx])
				perror(opnames[idx]);
			else
				perror("ioctl[unknown???]");
		}
		return -1;
	}
	return 0;
}

static int
set80211param(struct wpa_driver_madwifi_data *drv, int op, int arg,
	      int show_err)
{
	struct iwreq iwr;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.mode = op;
	os_memcpy(iwr.u.name+sizeof(u32), &arg, sizeof(arg));

	if (ioctl(drv->sock, IEEE80211_IOCTL_SETPARAM, &iwr) < 0) {
		if (show_err) 
			perror("ioctl[IEEE80211_IOCTL_SETPARAM]");
		return -1;
	}
	return 0;
}

static int
wpa_driver_madwifi_set_wpa_ie(struct wpa_driver_madwifi_data *drv,
			      const u8 *wpa_ie, size_t wpa_ie_len)
{
	struct iwreq iwr;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	/* NB: SETOPTIE is not fixed-size so must not be inlined */
	iwr.u.data.pointer = (void *) wpa_ie;
	iwr.u.data.length = wpa_ie_len;

	if (ioctl(drv->sock, IEEE80211_IOCTL_SETOPTIE, &iwr) < 0) {
		perror("ioctl[IEEE80211_IOCTL_SETOPTIE]");
		return -1;
	}
	return 0;
}

static int
wpa_driver_madwifi_del_key(struct wpa_driver_madwifi_data *drv, int key_idx,
			   const u8 *addr)
{
	struct ieee80211req_del_key wk;

	wpa_printf(MSG_DEBUG, "%s: keyidx=%d", __FUNCTION__, key_idx);
	os_memset(&wk, 0, sizeof(wk));
	wk.idk_keyix = key_idx;
	if (addr != NULL)
		os_memcpy(wk.idk_macaddr, addr, IEEE80211_ADDR_LEN);

	return set80211priv(drv, IEEE80211_IOCTL_DELKEY, &wk, sizeof(wk), 1);
}

static int
wpa_driver_madwifi_set_key(const char *ifname, void *priv, enum wpa_alg alg,
			   const u8 *addr, int key_idx, int set_tx,
			   const u8 *seq, size_t seq_len,
			   const u8 *key, size_t key_len)
{
	struct wpa_driver_madwifi_data *drv = priv;
	struct ieee80211req_key wk;
	char *alg_name;
	u_int8_t cipher;

	if (alg == WPA_ALG_NONE)
		return wpa_driver_madwifi_del_key(drv, key_idx, addr);

	switch (alg) {
	case WPA_ALG_WEP:
		if (addr == NULL || os_memcmp(addr, "\xff\xff\xff\xff\xff\xff",
					      ETH_ALEN) == 0) {
			/*
			 * madwifi did not seem to like static WEP key
			 * configuration with IEEE80211_IOCTL_SETKEY, so use
			 * Linux wireless extensions ioctl for this.
			 */
			return wpa_driver_wext_set_key(ifname, drv->wext, alg,
						       addr, key_idx, set_tx,
						       seq, seq_len,
						       key, key_len);
		}
		alg_name = "WEP";
		cipher = IEEE80211_CIPHER_WEP;
		break;
	case WPA_ALG_TKIP:
		alg_name = "TKIP";
		cipher = IEEE80211_CIPHER_TKIP;
		break;
	case WPA_ALG_CCMP:
		alg_name = "CCMP";
		cipher = IEEE80211_CIPHER_AES_CCM;
		break;
	default:
		wpa_printf(MSG_DEBUG, "%s: unknown/unsupported algorithm %d",
			__FUNCTION__, alg);
		return -1;
	}

	wpa_printf(MSG_DEBUG, "%s: alg=%s key_idx=%d set_tx=%d seq_len=%lu "
		   "key_len=%lu", __FUNCTION__, alg_name, key_idx, set_tx,
		   (unsigned long) seq_len, (unsigned long) key_len);

	if (seq_len > sizeof(u_int64_t)) {
		wpa_printf(MSG_DEBUG, "%s: seq_len %lu too big",
			   __FUNCTION__, (unsigned long) seq_len);
		return -2;
	}
	if (key_len > sizeof(wk.ik_keydata)) {
		wpa_printf(MSG_DEBUG, "%s: key length %lu too big",
			   __FUNCTION__, (unsigned long) key_len);
		return -3;
	}

	os_memset(&wk, 0, sizeof(wk));
	wk.ik_type = cipher;
	wk.ik_flags = IEEE80211_KEY_RECV;
	if (addr == NULL ||
	    os_memcmp(addr, "\xff\xff\xff\xff\xff\xff", ETH_ALEN) == 0)
		wk.ik_flags |= IEEE80211_KEY_GROUP;
	if (set_tx) {
		wk.ik_flags |= IEEE80211_KEY_XMIT | IEEE80211_KEY_DEFAULT;
		os_memcpy(wk.ik_macaddr, addr, IEEE80211_ADDR_LEN);
	} else
		os_memset(wk.ik_macaddr, 0, IEEE80211_ADDR_LEN);
	wk.ik_keyix = key_idx;
	wk.ik_keylen = key_len;
#ifdef WORDS_BIGENDIAN
	if (seq) {
		size_t i;
		u8 tmp[WPA_KEY_RSC_LEN];
		os_memset(tmp, 0, sizeof(tmp));
		for (i = 0; i < seq_len; i++)
			tmp[WPA_KEY_RSC_LEN - i - 1] = seq[i];
		os_memcpy(&wk.ik_keyrsc, tmp, WPA_KEY_RSC_LEN);
	}
#else /* WORDS_BIGENDIAN */
	if (seq)
		os_memcpy(&wk.ik_keyrsc, seq, seq_len);
#endif /* WORDS_BIGENDIAN */
	os_memcpy(wk.ik_keydata, key, key_len);

	return set80211priv(drv, IEEE80211_IOCTL_SETKEY, &wk, sizeof(wk), 1);
}

static int
wpa_driver_madwifi_set_countermeasures(void *priv, int enabled)
{
	struct wpa_driver_madwifi_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s: enabled=%d", __FUNCTION__, enabled);
	return set80211param(drv, IEEE80211_PARAM_COUNTERMEASURES, enabled, 1);
}

static int
wpa_driver_madwifi_deauthenticate(void *priv, const u8 *addr, int reason_code)
{
	struct wpa_driver_madwifi_data *drv = priv;
	struct ieee80211req_mlme mlme;

	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
	mlme.im_op = IEEE80211_MLME_DEAUTH;
	mlme.im_reason = reason_code;
	os_memcpy(mlme.im_macaddr, addr, IEEE80211_ADDR_LEN);
	return set80211priv(drv, IEEE80211_IOCTL_SETMLME, &mlme, sizeof(mlme), 1);
}

static int
wpa_driver_madwifi_disassociate(void *priv, const u8 *addr, int reason_code)
{
	struct wpa_driver_madwifi_data *drv = priv;
	struct ieee80211req_mlme mlme;

	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
	mlme.im_op = IEEE80211_MLME_DISASSOC;
	mlme.im_reason = reason_code;
	os_memcpy(mlme.im_macaddr, addr, IEEE80211_ADDR_LEN);
	return set80211priv(drv, IEEE80211_IOCTL_SETMLME, &mlme, sizeof(mlme), 1);
}

static int
wpa_driver_madwifi_associate(void *priv,
			     struct wpa_driver_associate_params *params)
{
	struct wpa_driver_madwifi_data *drv = priv;
	struct ieee80211req_mlme mlme;
	int ret = 0, privacy = 1;

	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);

	if (set80211param(drv, IEEE80211_PARAM_DROPUNENCRYPTED,
			  params->drop_unencrypted, 1) < 0)
		ret = -1;
	if (wpa_driver_madwifi_set_auth_alg(drv, params->auth_alg) < 0)
		ret = -1;

	/*
	 * NB: Don't need to set the freq or cipher-related state as
	 *     this is implied by the bssid which is used to locate
	 *     the scanned node state which holds it.  The ssid is
	 *     needed to disambiguate an AP that broadcasts multiple
	 *     ssid's but uses the same bssid.
	 */
	/* XXX error handling is wrong but unclear what to do... */
	if (wpa_driver_madwifi_set_wpa_ie(drv, params->wpa_ie,
					  params->wpa_ie_len) < 0)
		ret = -1;

	if (params->pairwise_suite == CIPHER_NONE &&
	    params->group_suite == CIPHER_NONE &&
	    params->key_mgmt_suite == KEY_MGMT_NONE &&
	    params->wpa_ie_len == 0)
		privacy = 0;

	if (set80211param(drv, IEEE80211_PARAM_PRIVACY, privacy, 1) < 0)
		ret = -1;

	if (params->wpa_ie_len &&
	    set80211param(drv, IEEE80211_PARAM_WPA,
			  params->wpa_ie[0] == WLAN_EID_RSN ? 2 : 1, 1) < 0)
		ret = -1;

	if (params->bssid == NULL) {
		/* ap_scan=2 mode - driver takes care of AP selection and
		 * roaming */
		/* FIX: this does not seem to work; would probably need to
		 * change something in the driver */
		if (set80211param(drv, IEEE80211_PARAM_ROAMING, 0, 1) < 0)
			ret = -1;

		if (wpa_driver_wext_set_ssid(drv->wext, params->ssid,
					     params->ssid_len) < 0)
			ret = -1;
	} else {
		if (set80211param(drv, IEEE80211_PARAM_ROAMING, 2, 1) < 0)
			ret = -1;
		if (wpa_driver_wext_set_ssid(drv->wext, params->ssid,
					     params->ssid_len) < 0)
			ret = -1;
		os_memset(&mlme, 0, sizeof(mlme));
		mlme.im_op = IEEE80211_MLME_ASSOC;
		os_memcpy(mlme.im_macaddr, params->bssid, IEEE80211_ADDR_LEN);
		if (set80211priv(drv, IEEE80211_IOCTL_SETMLME, &mlme,
				 sizeof(mlme), 1) < 0) {
			wpa_printf(MSG_DEBUG, "%s: SETMLME[ASSOC] failed",
				   __func__);
			ret = -1;
		}
	}

	return ret;
}

static int
wpa_driver_madwifi_set_auth_alg(void *priv, int auth_alg)
{
	struct wpa_driver_madwifi_data *drv = priv;
	int authmode;

	if ((auth_alg & WPA_AUTH_ALG_OPEN) &&
	    (auth_alg & WPA_AUTH_ALG_SHARED))
		authmode = IEEE80211_AUTH_AUTO;
	else if (auth_alg & WPA_AUTH_ALG_SHARED)
		authmode = IEEE80211_AUTH_SHARED;
	else
		authmode = IEEE80211_AUTH_OPEN;

	return set80211param(drv, IEEE80211_PARAM_AUTHMODE, authmode, 1);
}

static int
wpa_driver_madwifi_scan(void *priv, struct wpa_driver_scan_params *params)
{
	struct wpa_driver_madwifi_data *drv = priv;
	struct iwreq iwr;
	int ret = 0;
	const u8 *ssid = params->ssids[0].ssid;
	size_t ssid_len = params->ssids[0].ssid_len;

	wpa_driver_madwifi_set_probe_req_ie(drv, params->extra_ies,
					    params->extra_ies_len);

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);

	/* set desired ssid before scan */
	/* FIX: scan should not break the current association, so using
	 * set_ssid may not be the best way of doing this.. */
	if (wpa_driver_wext_set_ssid(drv->wext, ssid, ssid_len) < 0)
		ret = -1;

	if (ioctl(drv->sock, SIOCSIWSCAN, &iwr) < 0) {
		perror("ioctl[SIOCSIWSCAN]");
		ret = -1;
	}

	/*
	 * madwifi delivers a scan complete event so no need to poll, but
	 * register a backup timeout anyway to make sure that we recover even
	 * if the driver does not send this event for any reason. This timeout
	 * will only be used if the event is not delivered (event handler will
	 * cancel the timeout).
	 */
	eloop_cancel_timeout(wpa_driver_wext_scan_timeout, drv->wext,
			     drv->ctx);
	eloop_register_timeout(30, 0, wpa_driver_wext_scan_timeout, drv->wext,
			       drv->ctx);

	return ret;
}

static int wpa_driver_madwifi_get_bssid(void *priv, u8 *bssid)
{
	struct wpa_driver_madwifi_data *drv = priv;
	return wpa_driver_wext_get_bssid(drv->wext, bssid);
}


static int wpa_driver_madwifi_get_ssid(void *priv, u8 *ssid)
{
	struct wpa_driver_madwifi_data *drv = priv;
	return wpa_driver_wext_get_ssid(drv->wext, ssid);
}


static struct wpa_scan_results *
wpa_driver_madwifi_get_scan_results(void *priv)
{
	struct wpa_driver_madwifi_data *drv = priv;
	return wpa_driver_wext_get_scan_results(drv->wext);
}


static int wpa_driver_madwifi_set_operstate(void *priv, int state)
{
	struct wpa_driver_madwifi_data *drv = priv;
	return wpa_driver_wext_set_operstate(drv->wext, state);
}


static int wpa_driver_madwifi_set_probe_req_ie(void *priv, const u8 *ies,
					       size_t ies_len)
{
	struct ieee80211req_getset_appiebuf *probe_req_ie;
	int ret;

	probe_req_ie = os_malloc(sizeof(*probe_req_ie) + ies_len);
	if (probe_req_ie == NULL)
		return -1;

	probe_req_ie->app_frmtype = IEEE80211_APPIE_FRAME_PROBE_REQ;
	probe_req_ie->app_buflen = ies_len;
	os_memcpy(probe_req_ie->app_buf, ies, ies_len);

	ret = set80211priv(priv, IEEE80211_IOCTL_SET_APPIEBUF, probe_req_ie,
			   sizeof(struct ieee80211req_getset_appiebuf) +
			   ies_len, 1);

	os_free(probe_req_ie);

	return ret;
}


static void * wpa_driver_madwifi_init(void *ctx, const char *ifname)
{
	struct wpa_driver_madwifi_data *drv;

	drv = os_zalloc(sizeof(*drv));
	if (drv == NULL)
		return NULL;
	drv->wext = wpa_driver_wext_init(ctx, ifname);
	if (drv->wext == NULL)
		goto fail;

	drv->ctx = ctx;
	os_strlcpy(drv->ifname, ifname, sizeof(drv->ifname));
	drv->sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (drv->sock < 0)
		goto fail2;

	if (set80211param(drv, IEEE80211_PARAM_ROAMING, 2, 1) < 0) {
		wpa_printf(MSG_DEBUG, "%s: failed to set wpa_supplicant-based "
			   "roaming", __FUNCTION__);
		goto fail3;
	}

	if (set80211param(drv, IEEE80211_PARAM_WPA, 3, 1) < 0) {
		wpa_printf(MSG_DEBUG, "%s: failed to enable WPA support",
			   __FUNCTION__);
		goto fail3;
	}

	return drv;

fail3:
	close(drv->sock);
fail2:
	wpa_driver_wext_deinit(drv->wext);
fail:
	os_free(drv);
	return NULL;
}


static void wpa_driver_madwifi_deinit(void *priv)
{
	struct wpa_driver_madwifi_data *drv = priv;

	if (wpa_driver_madwifi_set_wpa_ie(drv, NULL, 0) < 0) {
		wpa_printf(MSG_DEBUG, "%s: failed to clear WPA IE",
			   __FUNCTION__);
	}
	if (set80211param(drv, IEEE80211_PARAM_ROAMING, 0, 1) < 0) {
		wpa_printf(MSG_DEBUG, "%s: failed to enable driver-based "
			   "roaming", __FUNCTION__);
	}
	if (set80211param(drv, IEEE80211_PARAM_PRIVACY, 0, 1) < 0) {
		wpa_printf(MSG_DEBUG, "%s: failed to disable forced Privacy "
			   "flag", __FUNCTION__);
	}
	if (set80211param(drv, IEEE80211_PARAM_WPA, 0, 1) < 0) {
		wpa_printf(MSG_DEBUG, "%s: failed to disable WPA",
			   __FUNCTION__);
	}

	wpa_driver_wext_deinit(drv->wext);

	close(drv->sock);
	os_free(drv);
}

#endif /* HOSTAPD */


const struct wpa_driver_ops wpa_driver_madwifi_ops = {
	.name			= "madwifi",
	.desc			= "MADWIFI 802.11 support (Atheros, etc.)",
	.set_key		= wpa_driver_madwifi_set_key,
#ifdef HOSTAPD
	.hapd_init		= madwifi_init,
	.hapd_deinit		= madwifi_deinit,
	.set_ieee8021x		= madwifi_set_ieee8021x,
	.set_privacy		= madwifi_set_privacy,
	.get_seqnum		= madwifi_get_seqnum,
	.flush			= madwifi_flush,
	.set_generic_elem	= madwifi_set_opt_ie,
	.sta_set_flags		= madwifi_sta_set_flags,
	.read_sta_data		= madwifi_read_sta_driver_data,
	.hapd_send_eapol	= madwifi_send_eapol,
	.sta_disassoc		= madwifi_sta_disassoc,
	.sta_deauth		= madwifi_sta_deauth,
	.hapd_set_ssid		= madwifi_set_ssid,
	.hapd_get_ssid		= madwifi_get_ssid,
	.hapd_set_countermeasures	= madwifi_set_countermeasures,
	.sta_clear_stats        = madwifi_sta_clear_stats,
	.commit			= madwifi_commit,
	.set_ap_wps_ie		= madwifi_set_ap_wps_ie,
#else /* HOSTAPD */
	.get_bssid		= wpa_driver_madwifi_get_bssid,
	.get_ssid		= wpa_driver_madwifi_get_ssid,
	.init			= wpa_driver_madwifi_init,
	.deinit			= wpa_driver_madwifi_deinit,
	.set_countermeasures	= wpa_driver_madwifi_set_countermeasures,
	.scan2			= wpa_driver_madwifi_scan,
	.get_scan_results2	= wpa_driver_madwifi_get_scan_results,
	.deauthenticate		= wpa_driver_madwifi_deauthenticate,
	.disassociate		= wpa_driver_madwifi_disassociate,
	.associate		= wpa_driver_madwifi_associate,
	.set_operstate		= wpa_driver_madwifi_set_operstate,
#endif /* HOSTAPD */
};
