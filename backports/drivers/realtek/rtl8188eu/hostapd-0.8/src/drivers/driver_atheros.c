/*
 * hostapd / Driver interaction with Atheros driver
 * Copyright (c) 2004, Sam Leffler <sam@errno.com>
 * Copyright (c) 2004, Video54 Technologies
 * Copyright (c) 2005-2007, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2009, Atheros Communications
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

#include "includes.h"
#include <net/if.h>
#include <sys/ioctl.h>

#include "common.h"
#ifndef _BYTE_ORDER
#ifdef WORDS_BIGENDIAN
#define _BYTE_ORDER _BIG_ENDIAN
#else
#define _BYTE_ORDER _LITTLE_ENDIAN
#endif
#endif /* _BYTE_ORDER */

/*
 * Note, the ATH_WPS_IE setting must match with the driver build.. If the
 * driver does not include this, the IEEE80211_IOCTL_GETWPAIE ioctl will fail.
 */
#define ATH_WPS_IE

#include "os/linux/include/ieee80211_external.h"


#ifdef CONFIG_WPS
#include <netpacket/packet.h>

#ifndef ETH_P_80211_RAW
#define ETH_P_80211_RAW 0x0019
#endif
#endif /* CONFIG_WPS */

#include "wireless_copy.h"

#include "driver.h"
#include "eloop.h"
#include "priv_netlink.h"
#include "l2_packet/l2_packet.h"
#include "common/ieee802_11_defs.h"
#include "netlink.h"
#include "linux_ioctl.h"


struct atheros_driver_data {
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
	struct wpabuf *wpa_ie;
	struct wpabuf *wps_beacon_ie;
	struct wpabuf *wps_probe_resp_ie;
};

static int atheros_sta_deauth(void *priv, const u8 *own_addr, const u8 *addr,
			      int reason_code);
static int atheros_set_privacy(void *priv, int enabled);

static const char * athr_get_ioctl_name(int op)
{
	switch (op) {
	case IEEE80211_IOCTL_SETPARAM:
		return "SETPARAM";
	case IEEE80211_IOCTL_GETPARAM:
		return "GETPARAM";
	case IEEE80211_IOCTL_SETKEY:
		return "SETKEY";
	case IEEE80211_IOCTL_SETWMMPARAMS:
		return "SETWMMPARAMS";
	case IEEE80211_IOCTL_DELKEY:
		return "DELKEY";
	case IEEE80211_IOCTL_GETWMMPARAMS:
		return "GETWMMPARAMS";
	case IEEE80211_IOCTL_SETMLME:
		return "SETMLME";
	case IEEE80211_IOCTL_GETCHANINFO:
		return "GETCHANINFO";
	case IEEE80211_IOCTL_SETOPTIE:
		return "SETOPTIE";
	case IEEE80211_IOCTL_GETOPTIE:
		return "GETOPTIE";
	case IEEE80211_IOCTL_ADDMAC:
		return "ADDMAC";
	case IEEE80211_IOCTL_DELMAC:
		return "DELMAC";
	case IEEE80211_IOCTL_GETCHANLIST:
		return "GETCHANLIST";
	case IEEE80211_IOCTL_SETCHANLIST:
		return "SETCHANLIST";
	case IEEE80211_IOCTL_KICKMAC:
		return "KICKMAC";
	case IEEE80211_IOCTL_CHANSWITCH:
		return "CHANSWITCH";
	case IEEE80211_IOCTL_GETMODE:
		return "GETMODE";
	case IEEE80211_IOCTL_SETMODE:
		return "SETMODE";
	case IEEE80211_IOCTL_GET_APPIEBUF:
		return "GET_APPIEBUF";
	case IEEE80211_IOCTL_SET_APPIEBUF:
		return "SET_APPIEBUF";
	case IEEE80211_IOCTL_SET_ACPARAMS:
		return "SET_ACPARAMS";
	case IEEE80211_IOCTL_FILTERFRAME:
		return "FILTERFRAME";
	case IEEE80211_IOCTL_SET_RTPARAMS:
		return "SET_RTPARAMS";
	case IEEE80211_IOCTL_SET_MEDENYENTRY:
		return "SET_MEDENYENTRY";
	case IEEE80211_IOCTL_GET_MACADDR:
		return "GET_MACADDR";
	case IEEE80211_IOCTL_SET_HBRPARAMS:
		return "SET_HBRPARAMS";
	case IEEE80211_IOCTL_SET_RXTIMEOUT:
		return "SET_RXTIMEOUT";
	case IEEE80211_IOCTL_STA_STATS:
		return "STA_STATS";
	case IEEE80211_IOCTL_GETWPAIE:
		return "GETWPAIE";
	default:
		return "??";
	}
}


static const char * athr_get_param_name(int op)
{
	switch (op) {
	case IEEE80211_IOC_MCASTCIPHER:
		return "MCASTCIPHER";
	case IEEE80211_PARAM_MCASTKEYLEN:
		return "MCASTKEYLEN";
	case IEEE80211_PARAM_UCASTCIPHERS:
		return "UCASTCIPHERS";
	case IEEE80211_PARAM_KEYMGTALGS:
		return "KEYMGTALGS";
	case IEEE80211_PARAM_RSNCAPS:
		return "RSNCAPS";
	case IEEE80211_PARAM_WPA:
		return "WPA";
	case IEEE80211_PARAM_AUTHMODE:
		return "AUTHMODE";
	case IEEE80211_PARAM_PRIVACY:
		return "PRIVACY";
	case IEEE80211_PARAM_COUNTERMEASURES:
		return "COUNTERMEASURES";
	default:
		return "??";
	}
}


static int
set80211priv(struct atheros_driver_data *drv, int op, void *data, int len)
{
	struct iwreq iwr;
	int do_inline = len < IFNAMSIZ;

	/* Certain ioctls must use the non-inlined method */
	if (op == IEEE80211_IOCTL_SET_APPIEBUF ||
	    op == IEEE80211_IOCTL_FILTERFRAME)
		do_inline = 0;

	memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
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
		wpa_printf(MSG_DEBUG, "atheros: %s: %s: ioctl op=0x%x "
			   "(%s) len=%d failed: %d (%s)",
			   __func__, drv->iface, op,
			   athr_get_ioctl_name(op),
			   len, errno, strerror(errno));
		return -1;
	}
	return 0;
}

static int
set80211param(struct atheros_driver_data *drv, int op, int arg)
{
	struct iwreq iwr;

	memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
	iwr.u.mode = op;
	memcpy(iwr.u.name+sizeof(__u32), &arg, sizeof(arg));

	if (ioctl(drv->ioctl_sock, IEEE80211_IOCTL_SETPARAM, &iwr) < 0) {
		perror("ioctl[IEEE80211_IOCTL_SETPARAM]");
		wpa_printf(MSG_DEBUG, "%s: %s: Failed to set parameter (op %d "
			   "(%s) arg %d)", __func__, drv->iface, op,
			   athr_get_param_name(op), arg);
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
atheros_configure_wpa(struct atheros_driver_data *drv,
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
#ifdef CONFIG_IEEE80211W
	if (params->ieee80211w != NO_MGMT_FRAME_PROTECTION) {
		v |= BIT(7);
		if (params->ieee80211w == MGMT_FRAME_PROTECTION_REQUIRED)
			v |= BIT(6);
	}
#endif /* CONFIG_IEEE80211W */

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
atheros_set_ieee8021x(void *priv, struct wpa_bss_params *params)
{
	struct atheros_driver_data *drv = priv;

	wpa_printf(MSG_DEBUG, "%s: enabled=%d", __func__, params->enabled);

	if (!params->enabled) {
		/* XXX restore state */
		if (set80211param(priv, IEEE80211_PARAM_AUTHMODE,
				  IEEE80211_AUTH_AUTO) < 0)
			return -1;
		/* IEEE80211_AUTH_AUTO ends up enabling Privacy; clear that */
		return atheros_set_privacy(drv, 0);
	}
	if (!params->wpa && !params->ieee802_1x) {
		hostapd_logger(drv->hapd, NULL, HOSTAPD_MODULE_DRIVER,
			HOSTAPD_LEVEL_WARNING, "No 802.1X or WPA enabled!");
		return -1;
	}
	if (params->wpa && atheros_configure_wpa(drv, params) != 0) {
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
atheros_set_privacy(void *priv, int enabled)
{
	struct atheros_driver_data *drv = priv;

	wpa_printf(MSG_DEBUG, "%s: enabled=%d", __func__, enabled);

	return set80211param(drv, IEEE80211_PARAM_PRIVACY, enabled);
}

static int
atheros_set_sta_authorized(void *priv, const u8 *addr, int authorized)
{
	struct atheros_driver_data *drv = priv;
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
atheros_sta_set_flags(void *priv, const u8 *addr,
		      int total_flags, int flags_or, int flags_and)
{
	/* For now, only support setting Authorized flag */
	if (flags_or & WPA_STA_AUTHORIZED)
		return atheros_set_sta_authorized(priv, addr, 1);
	if (!(flags_and & WPA_STA_AUTHORIZED))
		return atheros_set_sta_authorized(priv, addr, 0);
	return 0;
}

static int
atheros_del_key(void *priv, const u8 *addr, int key_idx)
{
	struct atheros_driver_data *drv = priv;
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
atheros_set_key(const char *ifname, void *priv, enum wpa_alg alg,
		const u8 *addr, int key_idx, int set_tx, const u8 *seq,
		size_t seq_len, const u8 *key, size_t key_len)
{
	struct atheros_driver_data *drv = priv;
	struct ieee80211req_key wk;
	u_int8_t cipher;
	int ret;

	if (alg == WPA_ALG_NONE)
		return atheros_del_key(drv, addr, key_idx);

	wpa_printf(MSG_DEBUG, "%s: alg=%d addr=%s key_idx=%d",
		   __func__, alg, ether_sprintf(addr), key_idx);

	switch (alg) {
	case WPA_ALG_WEP:
		cipher = IEEE80211_CIPHER_WEP;
		break;
	case WPA_ALG_TKIP:
		cipher = IEEE80211_CIPHER_TKIP;
		break;
	case WPA_ALG_CCMP:
		cipher = IEEE80211_CIPHER_AES_CCM;
		break;
#ifdef CONFIG_IEEE80211W
	case WPA_ALG_IGTK:
		cipher = IEEE80211_CIPHER_AES_CMAC;
		break;
#endif /* CONFIG_IEEE80211W */
	default:
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
		if (set_tx)
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
atheros_get_seqnum(const char *ifname, void *priv, const u8 *addr, int idx,
		   u8 *seq)
{
	struct atheros_driver_data *drv = priv;
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
#ifndef WPA_KEY_RSC_LEN
#define WPA_KEY_RSC_LEN 8
#endif
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
atheros_flush(void *priv)
{
	u8 allsta[IEEE80211_ADDR_LEN];
	memset(allsta, 0xff, IEEE80211_ADDR_LEN);
	return atheros_sta_deauth(priv, NULL, allsta,
				  IEEE80211_REASON_AUTH_LEAVE);
}


static int
atheros_read_sta_driver_data(void *priv, struct hostap_sta_driver_data *data,
			     const u8 *addr)
{
	struct atheros_driver_data *drv = priv;
	struct ieee80211req_sta_stats stats;

	memset(data, 0, sizeof(*data));

	/*
	 * Fetch statistics for station from the system.
	 */
	memset(&stats, 0, sizeof(stats));
	memcpy(stats.is_u.macaddr, addr, IEEE80211_ADDR_LEN);
	if (set80211priv(drv, IEEE80211_IOCTL_STA_STATS,
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
}


static int
atheros_sta_clear_stats(void *priv, const u8 *addr)
{
	struct atheros_driver_data *drv = priv;
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
}


static int
atheros_set_opt_ie(void *priv, const u8 *ie, size_t ie_len)
{
	struct atheros_driver_data *drv = priv;
	u8 buf[512];
	struct ieee80211req_getset_appiebuf *app_ie;

	wpa_printf(MSG_DEBUG, "%s buflen = %lu", __func__,
		   (unsigned long) ie_len);

	wpabuf_free(drv->wpa_ie);
	drv->wpa_ie = wpabuf_alloc_copy(ie, ie_len);

	app_ie = (struct ieee80211req_getset_appiebuf *) buf;
	os_memcpy(&(app_ie->app_buf[0]), ie, ie_len);
	app_ie->app_buflen = ie_len;

	app_ie->app_frmtype = IEEE80211_APPIE_FRAME_BEACON;

	/* append WPS IE for Beacon */
	if (drv->wps_beacon_ie != NULL) {
		os_memcpy(&(app_ie->app_buf[ie_len]),
			  wpabuf_head(drv->wps_beacon_ie),
			  wpabuf_len(drv->wps_beacon_ie));
		app_ie->app_buflen = ie_len + wpabuf_len(drv->wps_beacon_ie);
	}
	set80211priv(drv, IEEE80211_IOCTL_SET_APPIEBUF, app_ie,
		     sizeof(struct ieee80211req_getset_appiebuf) +
		     app_ie->app_buflen);

	/* append WPS IE for Probe Response */
	app_ie->app_frmtype = IEEE80211_APPIE_FRAME_PROBE_RESP;
	if (drv->wps_probe_resp_ie != NULL) {
		os_memcpy(&(app_ie->app_buf[ie_len]),
			  wpabuf_head(drv->wps_probe_resp_ie),
			  wpabuf_len(drv->wps_probe_resp_ie));
		app_ie->app_buflen = ie_len +
			wpabuf_len(drv->wps_probe_resp_ie);
	} else
		app_ie->app_buflen = ie_len;
	set80211priv(drv, IEEE80211_IOCTL_SET_APPIEBUF, app_ie,
		     sizeof(struct ieee80211req_getset_appiebuf) +
		     app_ie->app_buflen);
	return 0;
}

static int
atheros_sta_deauth(void *priv, const u8 *own_addr, const u8 *addr,
		   int reason_code)
{
	struct atheros_driver_data *drv = priv;
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
atheros_sta_disassoc(void *priv, const u8 *own_addr, const u8 *addr,
		     int reason_code)
{
	struct atheros_driver_data *drv = priv;
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
static void atheros_raw_receive(void *ctx, const u8 *src_addr, const u8 *buf,
				size_t len)
{
	struct atheros_driver_data *drv = ctx;
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
#endif /* CONFIG_WPS */

static int atheros_receive_probe_req(struct atheros_driver_data *drv)
{
	int ret = 0;
#ifdef CONFIG_WPS
	struct ieee80211req_set_filter filt;

	wpa_printf(MSG_DEBUG, "%s Enter", __func__);
	filt.app_filterype = IEEE80211_FILTER_TYPE_PROBE_REQ;

	ret = set80211priv(drv, IEEE80211_IOCTL_FILTERFRAME, &filt,
			   sizeof(struct ieee80211req_set_filter));
	if (ret)
		return ret;

	drv->sock_raw = l2_packet_init(drv->iface, NULL, ETH_P_80211_RAW,
				       atheros_raw_receive, drv, 1);
	if (drv->sock_raw == NULL)
		return -1;
#endif /* CONFIG_WPS */
	return ret;
}

#ifdef CONFIG_WPS
static int
atheros_set_wps_ie(void *priv, const u8 *ie, size_t len, u32 frametype)
{
	struct atheros_driver_data *drv = priv;
	u8 buf[512];
	struct ieee80211req_getset_appiebuf *beac_ie;

	wpa_printf(MSG_DEBUG, "%s buflen = %lu", __func__,
		   (unsigned long) len);

	beac_ie = (struct ieee80211req_getset_appiebuf *) buf;
	beac_ie->app_frmtype = frametype;
	beac_ie->app_buflen = len;
	os_memcpy(&(beac_ie->app_buf[0]), ie, len);

	/* append the WPA/RSN IE if it is set already */
	if (((frametype == IEEE80211_APPIE_FRAME_BEACON) ||
	     (frametype == IEEE80211_APPIE_FRAME_PROBE_RESP)) &&
	    (drv->wpa_ie != NULL)) {
		os_memcpy(&(beac_ie->app_buf[len]), wpabuf_head(drv->wpa_ie),
			  wpabuf_len(drv->wpa_ie));
		beac_ie->app_buflen += wpabuf_len(drv->wpa_ie);
	}

	return set80211priv(drv, IEEE80211_IOCTL_SET_APPIEBUF, beac_ie,
			    sizeof(struct ieee80211req_getset_appiebuf) +
			    beac_ie->app_buflen);
}

static int
atheros_set_ap_wps_ie(void *priv, const struct wpabuf *beacon,
		      const struct wpabuf *proberesp,
		      const struct wpabuf *assocresp)
{
	struct atheros_driver_data *drv = priv;

	wpabuf_free(drv->wps_beacon_ie);
	drv->wps_beacon_ie = beacon ? wpabuf_dup(beacon) : NULL;
	wpabuf_free(drv->wps_probe_resp_ie);
	drv->wps_probe_resp_ie = proberesp ? wpabuf_dup(proberesp) : NULL;

	atheros_set_wps_ie(priv, assocresp ? wpabuf_head(assocresp) : NULL,
			   assocresp ? wpabuf_len(assocresp) : 0,
			   IEEE80211_APPIE_FRAME_ASSOC_RESP);
	if (atheros_set_wps_ie(priv, beacon ? wpabuf_head(beacon) : NULL,
			       beacon ? wpabuf_len(beacon) : 0,
			       IEEE80211_APPIE_FRAME_BEACON))
		return -1;
	return atheros_set_wps_ie(priv,
				  proberesp ? wpabuf_head(proberesp) : NULL,
				  proberesp ? wpabuf_len(proberesp): 0,
				  IEEE80211_APPIE_FRAME_PROBE_RESP);
}
#else /* CONFIG_WPS */
#define atheros_set_ap_wps_ie NULL
#endif /* CONFIG_WPS */

static void
atheros_new_sta(struct atheros_driver_data *drv, u8 addr[IEEE80211_ADDR_LEN])
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
		/*
		 * See ATH_WPS_IE comment in the beginning of the file for a
		 * possible cause for the failure..
		 */
		wpa_printf(MSG_DEBUG, "%s: Failed to get WPA/RSN IE: %s",
			   __func__, strerror(errno));
		goto no_ie;
	}
	wpa_hexdump(MSG_MSGDUMP, "atheros req WPA IE",
		    ie.wpa_ie, IEEE80211_MAX_OPT_IE);
	wpa_hexdump(MSG_MSGDUMP, "atheros req RSN IE",
		    ie.rsn_ie, IEEE80211_MAX_OPT_IE);
#ifdef ATH_WPS_IE
	wpa_hexdump(MSG_MSGDUMP, "atheros req WPS IE",
		    ie.wps_ie, IEEE80211_MAX_OPT_IE);
#endif /* ATH_WPS_IE */
	iebuf = ie.wpa_ie;
	/* atheros seems to return some random data if WPA/RSN IE is not set.
	 * Assume the IE was not included if the IE type is unknown. */
	if (iebuf[0] != WLAN_EID_VENDOR_SPECIFIC)
		iebuf[1] = 0;
	if (iebuf[1] == 0 && ie.rsn_ie[1] > 0) {
		/* atheros-ng svn #1453 added rsn_ie. Use it, if wpa_ie was not
		 * set. This is needed for WPA2. */
		iebuf = ie.rsn_ie;
		if (iebuf[0] != WLAN_EID_RSN)
			iebuf[1] = 0;
	}

	ielen = iebuf[1];

#ifdef ATH_WPS_IE
	/* if WPS IE is present, preference is given to WPS */
	if (ie.wps_ie &&
	    (ie.wps_ie[1] > 0 && (ie.wps_ie[0] == WLAN_EID_VENDOR_SPECIFIC))) {
		iebuf = ie.wps_ie;
		ielen = ie.wps_ie[1];
	}
#endif /* ATH_WPS_IE */

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
atheros_wireless_event_wireless_custom(struct atheros_driver_data *drv,
				       char *custom, char *end)
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
#ifdef CONFIG_WPS
	} else if (strncmp(custom, "PUSH-BUTTON.indication", 22) == 0) {
		/* Some atheros kernels send push button as a wireless event */
		/* PROBLEM! this event is received for ALL BSSs ...
		 * so all are enabled for WPS... ugh.
		 */
		wpa_supplicant_event(drv->hapd, EVENT_WPS_BUTTON_PUSHED, NULL);
	} else if (strncmp(custom, "Manage.prob_req ", 16) == 0) {
		/*
		 * Atheros driver uses a hack to pass Probe Request frames as a
		 * binary data in the custom wireless event. The old way (using
		 * packet sniffing) didn't work when bridging.
		 * Format: "Manage.prob_req <frame len>" | zero padding | frame
		 */
#define WPS_FRAM_TAG_SIZE 30 /* hardcoded in driver */
		int len = atoi(custom + 16);
		if (len < 0 || custom + WPS_FRAM_TAG_SIZE + len > end) {
			wpa_printf(MSG_DEBUG, "Invalid Manage.prob_req event "
				   "length %d", len);
			return;
		}
		atheros_raw_receive(drv, NULL,
				    (u8 *) custom + WPS_FRAM_TAG_SIZE, len);
#endif /* CONFIG_WPS */
	}
}

static void
atheros_wireless_event_wireless(struct atheros_driver_data *drv,
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
		     iwe->cmd == IWEVASSOCREQIE ||
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
			atheros_new_sta(drv, (u8 *) iwe->u.addr.sa_data);
			break;
		case IWEVASSOCREQIE:
			/* Driver hack.. Use IWEVASSOCREQIE to bypass
			 * IWEVCUSTOM size limitations. Need to handle this
			 * just like IWEVCUSTOM.
			 */
		case IWEVCUSTOM:
			if (custom + iwe->u.data.length > end)
				return;
			buf = malloc(iwe->u.data.length + 1);
			if (buf == NULL)
				return;		/* XXX */
			memcpy(buf, custom, iwe->u.data.length);
			buf[iwe->u.data.length] = '\0';
			atheros_wireless_event_wireless_custom(
				drv, buf, buf + iwe->u.data.length);
			free(buf);
			break;
		}

		pos += iwe->len;
	}
}


static void
atheros_wireless_event_rtm_newlink(void *ctx,
				   struct ifinfomsg *ifi, u8 *buf, size_t len)
{
	struct atheros_driver_data *drv = ctx;
	int attrlen, rta_len;
	struct rtattr *attr;

	if (ifi->ifi_index != drv->ifindex)
		return;

	attrlen = len;
	attr = (struct rtattr *) buf;

	rta_len = RTA_ALIGN(sizeof(struct rtattr));
	while (RTA_OK(attr, attrlen)) {
		if (attr->rta_type == IFLA_WIRELESS) {
			atheros_wireless_event_wireless(
				drv, ((char *) attr) + rta_len,
				attr->rta_len - rta_len);
		}
		attr = RTA_NEXT(attr, attrlen);
	}
}


static int
atheros_get_we_version(struct atheros_driver_data *drv)
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
atheros_wireless_event_init(struct atheros_driver_data *drv)
{
	struct netlink_config *cfg;

	atheros_get_we_version(drv);

	cfg = os_zalloc(sizeof(*cfg));
	if (cfg == NULL)
		return -1;
	cfg->ctx = drv;
	cfg->newlink_cb = atheros_wireless_event_rtm_newlink;
	drv->netlink = netlink_init(cfg);
	if (drv->netlink == NULL) {
		os_free(cfg);
		return -1;
	}

	return 0;
}


static int
atheros_send_eapol(void *priv, const u8 *addr, const u8 *data, size_t data_len,
		   int encrypt, const u8 *own_addr, u32 flags)
{
	struct atheros_driver_data *drv = priv;
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
	struct atheros_driver_data *drv = ctx;
	drv_event_eapol_rx(drv->hapd, src_addr, buf + sizeof(struct l2_ethhdr),
			   len - sizeof(struct l2_ethhdr));
}

static void *
atheros_init(struct hostapd_data *hapd, struct wpa_init_params *params)
{
	struct atheros_driver_data *drv;
	struct ifreq ifr;
	struct iwreq iwr;
	char brname[IFNAMSIZ];

	drv = os_zalloc(sizeof(struct atheros_driver_data));
	if (drv == NULL) {
		printf("Could not allocate memory for atheros driver data\n");
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
	atheros_set_privacy(drv, 0); /* default to no privacy */

	atheros_receive_probe_req(drv);

	if (atheros_wireless_event_init(drv))
		goto bad;

	return drv;
bad:
	if (drv->sock_recv != NULL && drv->sock_recv != drv->sock_xmit)
		l2_packet_deinit(drv->sock_recv);
	if (drv->sock_xmit != NULL)
		l2_packet_deinit(drv->sock_xmit);
	if (drv->ioctl_sock >= 0)
		close(drv->ioctl_sock);
	if (drv != NULL)
		free(drv);
	return NULL;
}


static void
atheros_deinit(void *priv)
{
	struct atheros_driver_data *drv = priv;

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
	wpabuf_free(drv->wpa_ie);
	wpabuf_free(drv->wps_beacon_ie);
	wpabuf_free(drv->wps_probe_resp_ie);
	free(drv);
}

static int
atheros_set_ssid(void *priv, const u8 *buf, int len)
{
	struct atheros_driver_data *drv = priv;
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
atheros_get_ssid(void *priv, u8 *buf, int len)
{
	struct atheros_driver_data *drv = priv;
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
atheros_set_countermeasures(void *priv, int enabled)
{
	struct atheros_driver_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s: enabled=%d", __FUNCTION__, enabled);
	return set80211param(drv, IEEE80211_PARAM_COUNTERMEASURES, enabled);
}

static int
atheros_commit(void *priv)
{
	struct atheros_driver_data *drv = priv;
	return linux_set_iface_flags(drv->ioctl_sock, drv->iface, 1);
}

const struct wpa_driver_ops wpa_driver_atheros_ops = {
	.name			= "atheros",
	.hapd_init		= atheros_init,
	.hapd_deinit		= atheros_deinit,
	.set_ieee8021x		= atheros_set_ieee8021x,
	.set_privacy		= atheros_set_privacy,
	.set_key		= atheros_set_key,
	.get_seqnum		= atheros_get_seqnum,
	.flush			= atheros_flush,
	.set_generic_elem	= atheros_set_opt_ie,
	.sta_set_flags		= atheros_sta_set_flags,
	.read_sta_data		= atheros_read_sta_driver_data,
	.hapd_send_eapol	= atheros_send_eapol,
	.sta_disassoc		= atheros_sta_disassoc,
	.sta_deauth		= atheros_sta_deauth,
	.hapd_set_ssid		= atheros_set_ssid,
	.hapd_get_ssid		= atheros_get_ssid,
	.set_countermeasures	= atheros_set_countermeasures,
	.sta_clear_stats	= atheros_sta_clear_stats,
	.commit			= atheros_commit,
	.set_ap_wps_ie		= atheros_set_ap_wps_ie,
};
