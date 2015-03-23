/*
 * WPA Supplicant - driver interaction with BSD net80211 layer
 * Copyright (c) 2004, Sam Leffler <sam@errno.com>
 * Copyright (c) 2004, 2Wire, Inc
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
#include <sys/ioctl.h>
#include <sys/sysctl.h>

#include "common.h"
#include "driver.h"
#include "eloop.h"
#include "common/ieee802_11_defs.h"
#include "common/wpa_common.h"

#include <net/if.h>
#include <net/if_media.h>

#ifdef __NetBSD__
#include <net/if_ether.h>
#else
#include <net/ethernet.h>
#endif
#include <net/route.h>

#ifdef __DragonFly__
#include <netproto/802_11/ieee80211_ioctl.h>
#include <netproto/802_11/ieee80211_dragonfly.h>
#else /* __DragonFly__ */
#ifdef __GLIBC__
#include <netinet/ether.h>
#endif /* __GLIBC__ */
#include <net80211/ieee80211.h>
#include <net80211/ieee80211_ioctl.h>
#include <net80211/ieee80211_crypto.h>
#endif /* __DragonFly__ || __GLIBC__ */
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <net80211/ieee80211_freebsd.h>
#endif
#if __NetBSD__
#include <net80211/ieee80211_netbsd.h>
#endif

#include "l2_packet/l2_packet.h"

struct bsd_driver_data {
	struct hostapd_data *hapd;	/* back pointer */

	int	sock;			/* open socket for 802.11 ioctls */
	struct l2_packet_data *sock_xmit;/* raw packet xmit socket */
	int	route;			/* routing socket for events */
	char	ifname[IFNAMSIZ+1];	/* interface name */
	unsigned int ifindex;		/* interface index */
	void	*ctx;
	struct wpa_driver_capa capa;	/* driver capability */
	int	is_ap;			/* Access point mode */
	int	prev_roaming;	/* roaming state to restore on deinit */
	int	prev_privacy;	/* privacy state to restore on deinit */
	int	prev_wpa;	/* wpa state to restore on deinit */
};

/* Generic functions for hostapd and wpa_supplicant */

static int
bsd_set80211(void *priv, int op, int val, const void *arg, int arg_len)
{
	struct bsd_driver_data *drv = priv;
	struct ieee80211req ireq;

	os_memset(&ireq, 0, sizeof(ireq));
	os_strlcpy(ireq.i_name, drv->ifname, sizeof(ireq.i_name));
	ireq.i_type = op;
	ireq.i_val = val;
	ireq.i_data = (void *) arg;
	ireq.i_len = arg_len;

	if (ioctl(drv->sock, SIOCS80211, &ireq) < 0) {
		wpa_printf(MSG_ERROR, "ioctl[SIOCS80211, op=%u, val=%u, "
			   "arg_len=%u]: %s", op, val, arg_len,
			   strerror(errno));
		return -1;
	}
	return 0;
}

static int
bsd_get80211(void *priv, struct ieee80211req *ireq, int op, void *arg,
	     int arg_len)
{
	struct bsd_driver_data *drv = priv;

	os_memset(ireq, 0, sizeof(*ireq));
	os_strlcpy(ireq->i_name, drv->ifname, sizeof(ireq->i_name));
	ireq->i_type = op;
	ireq->i_len = arg_len;
	ireq->i_data = arg;

	if (ioctl(drv->sock, SIOCG80211, ireq) < 0) {
		wpa_printf(MSG_ERROR, "ioctl[SIOCS80211, op=%u, "
			   "arg_len=%u]: %s", op, arg_len, strerror(errno));
		return -1;
	}
	return 0;
}

static int
get80211var(struct bsd_driver_data *drv, int op, void *arg, int arg_len)
{
	struct ieee80211req ireq;

	if (bsd_get80211(drv, &ireq, op, arg, arg_len) < 0)
		return -1;
	return ireq.i_len;
}

static int
set80211var(struct bsd_driver_data *drv, int op, const void *arg, int arg_len)
{
	return bsd_set80211(drv, op, 0, arg, arg_len);
}

static int
set80211param(struct bsd_driver_data *drv, int op, int arg)
{
	return bsd_set80211(drv, op, arg, NULL, 0);
}

static int
bsd_get_ssid(void *priv, u8 *ssid, int len)
{
	struct bsd_driver_data *drv = priv;
#ifdef SIOCG80211NWID
	struct ieee80211_nwid nwid;
	struct ifreq ifr;

	os_memset(&ifr, 0, sizeof(ifr));
	os_strlcpy(ifr.ifr_name, drv->ifname, sizeof(ifr.ifr_name));
	ifr.ifr_data = (void *)&nwid;
	if (ioctl(drv->sock, SIOCG80211NWID, &ifr) < 0 ||
	    nwid.i_len > IEEE80211_NWID_LEN)
		return -1;
	os_memcpy(ssid, nwid.i_nwid, nwid.i_len);
	return nwid.i_len;
#else
	return get80211var(drv, IEEE80211_IOC_SSID, ssid, IEEE80211_NWID_LEN);
#endif
}

static int
bsd_set_ssid(void *priv, const u8 *ssid, int ssid_len)
{
	struct bsd_driver_data *drv = priv;
#ifdef SIOCS80211NWID
	struct ieee80211_nwid nwid;
	struct ifreq ifr;

	os_memcpy(nwid.i_nwid, ssid, ssid_len);
	nwid.i_len = ssid_len;
	os_memset(&ifr, 0, sizeof(ifr));
	os_strlcpy(ifr.ifr_name, drv->ifname, sizeof(ifr.ifr_name));
	ifr.ifr_data = (void *)&nwid;
	return ioctl(drv->sock, SIOCS80211NWID, &ifr);
#else
	return set80211var(drv, IEEE80211_IOC_SSID, ssid, ssid_len);
#endif
}

static int
bsd_get_if_media(void *priv)
{
	struct bsd_driver_data *drv = priv;
	struct ifmediareq ifmr;

	os_memset(&ifmr, 0, sizeof(ifmr));
	os_strlcpy(ifmr.ifm_name, drv->ifname, sizeof(ifmr.ifm_name));

	if (ioctl(drv->sock, SIOCGIFMEDIA, &ifmr) < 0) {
		wpa_printf(MSG_ERROR, "%s: SIOCGIFMEDIA %s", __func__,
			   strerror(errno));
		return -1;
	}

	return ifmr.ifm_current;
}

static int
bsd_set_if_media(void *priv, int media)
{
	struct bsd_driver_data *drv = priv;
	struct ifreq ifr;

	os_memset(&ifr, 0, sizeof(ifr));
	os_strlcpy(ifr.ifr_name, drv->ifname, sizeof(ifr.ifr_name));
	ifr.ifr_media = media;

	if (ioctl(drv->sock, SIOCSIFMEDIA, &ifr) < 0) {
		wpa_printf(MSG_ERROR, "%s: SIOCSIFMEDIA %s", __func__,
			   strerror(errno));
		return -1;
	}

	return 0;
}

static int
bsd_set_mediaopt(void *priv, uint32_t mask, uint32_t mode)
{
	int media = bsd_get_if_media(priv);

	if (media < 0)
		return -1;
	media &= ~mask;
	media |= mode;
	if (bsd_set_if_media(priv, media) < 0)
		return -1;
	return 0;
}

static int
bsd_del_key(void *priv, const u8 *addr, int key_idx)
{
	struct ieee80211req_del_key wk;

	os_memset(&wk, 0, sizeof(wk));
	if (addr == NULL) {
		wpa_printf(MSG_DEBUG, "%s: key_idx=%d", __func__, key_idx);
		wk.idk_keyix = key_idx;
	} else {
		wpa_printf(MSG_DEBUG, "%s: addr=" MACSTR, __func__,
			   MAC2STR(addr));
		os_memcpy(wk.idk_macaddr, addr, IEEE80211_ADDR_LEN);
		wk.idk_keyix = (u_int8_t) IEEE80211_KEYIX_NONE;	/* XXX */
	}

	return set80211var(priv, IEEE80211_IOC_DELKEY, &wk, sizeof(wk));
}

static int
bsd_send_mlme_param(void *priv, const u8 op, const u16 reason, const u8 *addr)
{
	struct ieee80211req_mlme mlme;

	os_memset(&mlme, 0, sizeof(mlme));
	mlme.im_op = op;
	mlme.im_reason = reason;
	os_memcpy(mlme.im_macaddr, addr, IEEE80211_ADDR_LEN);
	return set80211var(priv, IEEE80211_IOC_MLME, &mlme, sizeof(mlme));
}

static int
bsd_ctrl_iface(void *priv, int enable)
{
	struct bsd_driver_data *drv = priv;
	struct ifreq ifr;

	os_memset(&ifr, 0, sizeof(ifr));
	os_strlcpy(ifr.ifr_name, drv->ifname, sizeof(ifr.ifr_name));

	if (ioctl(drv->sock, SIOCGIFFLAGS, &ifr) < 0) {
		perror("ioctl[SIOCGIFFLAGS]");
		return -1;
	}

	if (enable)
		ifr.ifr_flags |= IFF_UP;
	else
		ifr.ifr_flags &= ~IFF_UP;

	if (ioctl(drv->sock, SIOCSIFFLAGS, &ifr) < 0) {
		perror("ioctl[SIOCSIFFLAGS]");
		return -1;
	}

	return 0;
}

static int
bsd_set_key(const char *ifname, void *priv, enum wpa_alg alg,
	    const unsigned char *addr, int key_idx, int set_tx, const u8 *seq,
	    size_t seq_len, const u8 *key, size_t key_len)
{
	struct ieee80211req_key wk;

	wpa_printf(MSG_DEBUG, "%s: alg=%d addr=%p key_idx=%d set_tx=%d "
		   "seq_len=%zu key_len=%zu", __func__, alg, addr, key_idx,
		   set_tx, seq_len, key_len);

	if (alg == WPA_ALG_NONE) {
#ifndef HOSTAPD
		if (addr == NULL || is_broadcast_ether_addr(addr))
			return bsd_del_key(priv, NULL, key_idx);
		else
#endif /* HOSTAPD */
			return bsd_del_key(priv, addr, key_idx);
	}

	os_memset(&wk, 0, sizeof(wk));
	switch (alg) {
	case WPA_ALG_WEP:
		wk.ik_type = IEEE80211_CIPHER_WEP;
		break;
	case WPA_ALG_TKIP:
		wk.ik_type = IEEE80211_CIPHER_TKIP;
		break;
	case WPA_ALG_CCMP:
		wk.ik_type = IEEE80211_CIPHER_AES_CCM;
		break;
	default:
		wpa_printf(MSG_ERROR, "%s: unknown alg=%d", __func__, alg);
		return -1;
	}

	wk.ik_flags = IEEE80211_KEY_RECV;
	if (set_tx)
		wk.ik_flags |= IEEE80211_KEY_XMIT;

	if (addr == NULL) {
		os_memset(wk.ik_macaddr, 0xff, IEEE80211_ADDR_LEN);
		wk.ik_keyix = key_idx;
	} else {
		os_memcpy(wk.ik_macaddr, addr, IEEE80211_ADDR_LEN);
		/*
		 * Deduce whether group/global or unicast key by checking
		 * the address (yech).  Note also that we can only mark global
		 * keys default; doing this for a unicast key is an error.
		 */
		if (is_broadcast_ether_addr(addr)) {
			wk.ik_flags |= IEEE80211_KEY_GROUP;
			wk.ik_keyix = key_idx;
		} else {
			wk.ik_keyix = key_idx == 0 ? IEEE80211_KEYIX_NONE :
				key_idx;
		}
	}
	if (wk.ik_keyix != IEEE80211_KEYIX_NONE && set_tx)
		wk.ik_flags |= IEEE80211_KEY_DEFAULT;
	wk.ik_keylen = key_len;
	if (seq)
		os_memcpy(&wk.ik_keyrsc, seq, seq_len);
	os_memcpy(wk.ik_keydata, key, key_len);

	return set80211var(priv, IEEE80211_IOC_WPAKEY, &wk, sizeof(wk));
}

static int
bsd_configure_wpa(void *priv, struct wpa_bss_params *params)
{
#ifndef IEEE80211_IOC_APPIE
	static const char *ciphernames[] =
		{ "WEP", "TKIP", "AES-OCB", "AES-CCM", "CKIP", "NONE" };
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
		printf("Unknown group key cipher %u\n",
			params->wpa_group);
		return -1;
	}
	wpa_printf(MSG_DEBUG, "%s: group key cipher=%s (%u)",
		   __func__, ciphernames[v], v);
	if (set80211param(priv, IEEE80211_IOC_MCASTCIPHER, v)) {
		printf("Unable to set group key cipher to %u (%s)\n",
			v, ciphernames[v]);
		return -1;
	}
	if (v == IEEE80211_CIPHER_WEP) {
		/* key length is done only for specific ciphers */
		v = (params->wpa_group == WPA_CIPHER_WEP104 ? 13 : 5);
		if (set80211param(priv, IEEE80211_IOC_MCASTKEYLEN, v)) {
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
	if (set80211param(priv, IEEE80211_IOC_UCASTCIPHERS, v)) {
		printf("Unable to set pairwise key ciphers to 0x%x\n", v);
		return -1;
	}

	wpa_printf(MSG_DEBUG, "%s: key management algorithms=0x%x",
		   __func__, params->wpa_key_mgmt);
	if (set80211param(priv, IEEE80211_IOC_KEYMGTALGS,
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
	if (set80211param(priv, IEEE80211_IOC_RSNCAPS, v)) {
		printf("Unable to set RSN capabilities to 0x%x\n", v);
		return -1;
	}
#endif /* IEEE80211_IOC_APPIE */

	wpa_printf(MSG_DEBUG, "%s: enable WPA= 0x%x", __func__, params->wpa);
	if (set80211param(priv, IEEE80211_IOC_WPA, params->wpa)) {
		printf("Unable to set WPA to %u\n", params->wpa);
		return -1;
	}
	return 0;
}

static int
bsd_set_ieee8021x(void *priv, struct wpa_bss_params *params)
{
	wpa_printf(MSG_DEBUG, "%s: enabled=%d", __func__, params->enabled);

	if (!params->enabled) {
		/* XXX restore state */
		return set80211param(priv, IEEE80211_IOC_AUTHMODE,
				     IEEE80211_AUTH_AUTO);
	}
	if (!params->wpa && !params->ieee802_1x) {
		wpa_printf(MSG_ERROR, "%s: No 802.1X or WPA enabled",
			   __func__);
		return -1;
	}
	if (params->wpa && bsd_configure_wpa(priv, params) != 0) {
		wpa_printf(MSG_ERROR, "%s: Failed to configure WPA state",
			   __func__);
		return -1;
	}
	if (set80211param(priv, IEEE80211_IOC_AUTHMODE,
		(params->wpa ? IEEE80211_AUTH_WPA : IEEE80211_AUTH_8021X))) {
		wpa_printf(MSG_ERROR, "%s: Failed to enable WPA/802.1X",
			   __func__);
		return -1;
	}
	return bsd_ctrl_iface(priv, 1);
}

static int
bsd_set_sta_authorized(void *priv, const u8 *addr,
		       int total_flags, int flags_or, int flags_and)
{
	int authorized = -1;

	/* For now, only support setting Authorized flag */
	if (flags_or & WPA_STA_AUTHORIZED)
		authorized = 1;
	if (!(flags_and & WPA_STA_AUTHORIZED))
		authorized = 0;

	if (authorized < 0)
		return 0;

	return bsd_send_mlme_param(priv, authorized ?
				   IEEE80211_MLME_AUTHORIZE :
				   IEEE80211_MLME_UNAUTHORIZE, 0, addr);
}

static void
bsd_new_sta(void *priv, void *ctx, u8 addr[IEEE80211_ADDR_LEN])
{
	struct ieee80211req_wpaie ie;
	int ielen = 0;
	u8 *iebuf = NULL;

	/*
	 * Fetch and validate any negotiated WPA/RSN parameters.
	 */
	memset(&ie, 0, sizeof(ie));
	memcpy(ie.wpa_macaddr, addr, IEEE80211_ADDR_LEN);
	if (get80211var(priv, IEEE80211_IOC_WPAIE, &ie, sizeof(ie)) < 0) {
		printf("Failed to get WPA/RSN information element.\n");
		goto no_ie;
	}
	iebuf = ie.wpa_ie;
	ielen = ie.wpa_ie[1];
	if (ielen == 0)
		iebuf = NULL;
	else
		ielen += 2;

no_ie:
	drv_event_assoc(ctx, addr, iebuf, ielen, 0);
}

static int
bsd_send_eapol(void *priv, const u8 *addr, const u8 *data, size_t data_len,
	       int encrypt, const u8 *own_addr, u32 flags)
{
	struct bsd_driver_data *drv = priv;

	wpa_hexdump(MSG_MSGDUMP, "TX EAPOL", data, data_len);

	return l2_packet_send(drv->sock_xmit, addr, ETH_P_EAPOL, data,
			      data_len);
}

static int
bsd_set_freq(void *priv, struct hostapd_freq_params *freq)
{
	struct bsd_driver_data *drv = priv;
#ifdef SIOCS80211CHANNEL
	struct ieee80211chanreq creq;
#endif /* SIOCS80211CHANNEL */
	u32 mode;
	int channel = freq->channel;

	if (channel < 14) {
		mode =
#ifdef CONFIG_IEEE80211N
			freq->ht_enabled ? IFM_IEEE80211_11NG :
#endif /* CONFIG_IEEE80211N */
		        IFM_IEEE80211_11G;
	} else if (channel == 14) {
		mode = IFM_IEEE80211_11B;
	} else {
		mode =
#ifdef CONFIG_IEEE80211N
			freq->ht_enabled ? IFM_IEEE80211_11NA :
#endif /* CONFIG_IEEE80211N */
			IFM_IEEE80211_11A;
	}
	if (bsd_set_mediaopt(drv, IFM_MMASK, mode) < 0) {
		wpa_printf(MSG_ERROR, "%s: failed to set modulation mode",
			   __func__);
		return -1;
	}

#ifdef SIOCS80211CHANNEL
	os_memset(&creq, 0, sizeof(creq));
	os_strlcpy(creq.i_name, drv->ifname, sizeof(creq.i_name));
	creq.i_channel = (u_int16_t)channel;
	return ioctl(drv->sock, SIOCS80211CHANNEL, &creq);
#else /* SIOCS80211CHANNEL */
	return set80211param(priv, IEEE80211_IOC_CHANNEL, channel);
#endif /* SIOCS80211CHANNEL */
}

static int
bsd_set_opt_ie(void *priv, const u8 *ie, size_t ie_len)
{
#ifdef IEEE80211_IOC_APPIE
	wpa_printf(MSG_DEBUG, "%s: set WPA+RSN ie (len %lu)", __func__,
		   (unsigned long)ie_len);
	return bsd_set80211(priv, IEEE80211_IOC_APPIE, IEEE80211_APPIE_WPA,
			    ie, ie_len);
#endif /* IEEE80211_IOC_APPIE */
	return 0;
}

static int
rtbuf_len(void)
{
	size_t len;

	int mib[6] = {CTL_NET, AF_ROUTE, 0, AF_INET, NET_RT_DUMP, 0};

	if (sysctl(mib, 6, NULL, &len, NULL, 0) < 0) {
		wpa_printf(MSG_WARNING, "%s failed: %s\n", __func__,
			   strerror(errno));
		len = 2048;
	}

	return len;
}

#ifdef HOSTAPD

/*
 * Avoid conflicts with hostapd definitions by undefining couple of defines
 * from net80211 header files.
 */
#undef RSN_VERSION
#undef WPA_VERSION
#undef WPA_OUI_TYPE

static int bsd_sta_deauth(void *priv, const u8 *own_addr, const u8 *addr,
			  int reason_code);

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

static int
bsd_set_privacy(void *priv, int enabled)
{
	wpa_printf(MSG_DEBUG, "%s: enabled=%d", __func__, enabled);

	return set80211param(priv, IEEE80211_IOC_PRIVACY, enabled);
}

static int
bsd_get_seqnum(const char *ifname, void *priv, const u8 *addr, int idx,
	       u8 *seq)
{
	struct ieee80211req_key wk;

	wpa_printf(MSG_DEBUG, "%s: addr=%s idx=%d",
		   __func__, ether_sprintf(addr), idx);

	memset(&wk, 0, sizeof(wk));
	if (addr == NULL)
		memset(wk.ik_macaddr, 0xff, IEEE80211_ADDR_LEN);
	else
		memcpy(wk.ik_macaddr, addr, IEEE80211_ADDR_LEN);
	wk.ik_keyix = idx;

	if (get80211var(priv, IEEE80211_IOC_WPAKEY, &wk, sizeof(wk)) < 0) {
		printf("Failed to get encryption.\n");
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
bsd_flush(void *priv)
{
	u8 allsta[IEEE80211_ADDR_LEN];

	memset(allsta, 0xff, IEEE80211_ADDR_LEN);
	return bsd_sta_deauth(priv, NULL, allsta, IEEE80211_REASON_AUTH_LEAVE);
}


static int
bsd_read_sta_driver_data(void *priv, struct hostap_sta_driver_data *data,
			 const u8 *addr)
{
	struct ieee80211req_sta_stats stats;

	memcpy(stats.is_u.macaddr, addr, IEEE80211_ADDR_LEN);
	if (get80211var(priv, IEEE80211_IOC_STA_STATS, &stats, sizeof(stats))
	    > 0) {
		/* XXX? do packets counts include non-data frames? */
		data->rx_packets = stats.is_stats.ns_rx_data;
		data->rx_bytes = stats.is_stats.ns_rx_bytes;
		data->tx_packets = stats.is_stats.ns_tx_data;
		data->tx_bytes = stats.is_stats.ns_tx_bytes;
	}
	return 0;
}

static int
bsd_sta_deauth(void *priv, const u8 *own_addr, const u8 *addr, int reason_code)
{
	return bsd_send_mlme_param(priv, IEEE80211_MLME_DEAUTH, reason_code,
				   addr);
}

static int
bsd_sta_disassoc(void *priv, const u8 *own_addr, const u8 *addr,
		 int reason_code)
{
	return bsd_send_mlme_param(priv, IEEE80211_MLME_DISASSOC, reason_code,
				   addr);
}

static void
bsd_wireless_event_receive(int sock, void *ctx, void *sock_ctx)
{
	struct bsd_driver_data *drv = ctx;
	char *buf;
	struct if_announcemsghdr *ifan;
	struct rt_msghdr *rtm;
	struct ieee80211_michael_event *mic;
	struct ieee80211_join_event *join;
	struct ieee80211_leave_event *leave;
	int n, len;
	union wpa_event_data data;

	len = rtbuf_len();

	buf = os_malloc(len);
	if (buf == NULL) {
		wpa_printf(MSG_ERROR, "%s os_malloc() failed\n", __func__);
		return;
	}

	n = read(sock, buf, len);
	if (n < 0) {
		if (errno != EINTR && errno != EAGAIN)
			wpa_printf(MSG_ERROR, "%s read() failed: %s\n",
				   __func__, strerror(errno));
		os_free(buf);
		return;
	}

	rtm = (struct rt_msghdr *) buf;
	if (rtm->rtm_version != RTM_VERSION) {
		wpa_printf(MSG_DEBUG, "Invalid routing message version=%d",
			   rtm->rtm_version);
		os_free(buf);
		return;
	}
	ifan = (struct if_announcemsghdr *) rtm;
	switch (rtm->rtm_type) {
	case RTM_IEEE80211:
		switch (ifan->ifan_what) {
		case RTM_IEEE80211_ASSOC:
		case RTM_IEEE80211_REASSOC:
		case RTM_IEEE80211_DISASSOC:
		case RTM_IEEE80211_SCAN:
			break;
		case RTM_IEEE80211_LEAVE:
			leave = (struct ieee80211_leave_event *) &ifan[1];
			drv_event_disassoc(drv->hapd, leave->iev_addr);
			break;
		case RTM_IEEE80211_JOIN:
#ifdef RTM_IEEE80211_REJOIN
		case RTM_IEEE80211_REJOIN:
#endif
			join = (struct ieee80211_join_event *) &ifan[1];
			bsd_new_sta(drv, drv->hapd, join->iev_addr);
			break;
		case RTM_IEEE80211_REPLAY:
			/* ignore */
			break;
		case RTM_IEEE80211_MICHAEL:
			mic = (struct ieee80211_michael_event *) &ifan[1];
			wpa_printf(MSG_DEBUG,
				"Michael MIC failure wireless event: "
				"keyix=%u src_addr=" MACSTR, mic->iev_keyix,
				MAC2STR(mic->iev_src));
			os_memset(&data, 0, sizeof(data));
			data.michael_mic_failure.unicast = 1;
			data.michael_mic_failure.src = mic->iev_src;
			wpa_supplicant_event(drv->hapd,
					     EVENT_MICHAEL_MIC_FAILURE, &data);
			break;
		}
		break;
	}
	os_free(buf);
}

static void
handle_read(void *ctx, const u8 *src_addr, const u8 *buf, size_t len)
{
	struct bsd_driver_data *drv = ctx;
	drv_event_eapol_rx(drv->hapd, src_addr, buf, len);
}

static void *
bsd_init(struct hostapd_data *hapd, struct wpa_init_params *params)
{
	struct bsd_driver_data *drv;

	drv = os_zalloc(sizeof(struct bsd_driver_data));
	if (drv == NULL) {
		printf("Could not allocate memory for bsd driver data\n");
		goto bad;
	}

	drv->hapd = hapd;
	drv->sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (drv->sock < 0) {
		perror("socket[PF_INET,SOCK_DGRAM]");
		goto bad;
	}
	os_strlcpy(drv->ifname, params->ifname, sizeof(drv->ifname));

	drv->sock_xmit = l2_packet_init(drv->ifname, NULL, ETH_P_EAPOL,
					handle_read, drv, 0);
	if (drv->sock_xmit == NULL)
		goto bad;
	if (l2_packet_get_own_addr(drv->sock_xmit, params->own_addr))
		goto bad;

	/* mark down during setup */
	if (bsd_ctrl_iface(drv, 0) < 0)
		goto bad;

	drv->route = socket(PF_ROUTE, SOCK_RAW, 0);
	if (drv->route < 0) {
		perror("socket(PF_ROUTE,SOCK_RAW)");
		goto bad;
	}
	eloop_register_read_sock(drv->route, bsd_wireless_event_receive, drv,
				 NULL);

	if (bsd_set_mediaopt(drv, IFM_OMASK, IFM_IEEE80211_HOSTAP) < 0) {
		wpa_printf(MSG_ERROR, "%s: failed to set operation mode",
			   __func__);
		goto bad;
	}

	return drv;
bad:
	if (drv->sock_xmit != NULL)
		l2_packet_deinit(drv->sock_xmit);
	if (drv->sock >= 0)
		close(drv->sock);
	if (drv != NULL)
		os_free(drv);
	return NULL;
}


static void
bsd_deinit(void *priv)
{
	struct bsd_driver_data *drv = priv;

	if (drv->route >= 0) {
		eloop_unregister_read_sock(drv->route);
		close(drv->route);
	}
	bsd_ctrl_iface(drv, 0);
	if (drv->sock >= 0)
		close(drv->sock);
	if (drv->sock_xmit != NULL)
		l2_packet_deinit(drv->sock_xmit);
	os_free(drv);
}

#else /* HOSTAPD */

static int
get80211param(struct bsd_driver_data *drv, int op)
{
	struct ieee80211req ireq;

	if (bsd_get80211(drv, &ireq, op, NULL, 0) < 0)
		return -1;
	return ireq.i_val;
}

static int
wpa_driver_bsd_get_bssid(void *priv, u8 *bssid)
{
	struct bsd_driver_data *drv = priv;
#ifdef SIOCG80211BSSID
	struct ieee80211_bssid bs;

	os_strlcpy(bs.i_name, drv->ifname, sizeof(bs.i_name));
	if (ioctl(drv->sock, SIOCG80211BSSID, &bs) < 0)
		return -1;
	os_memcpy(bssid, bs.i_bssid, sizeof(bs.i_bssid));
	return 0;
#else
	return get80211var(drv, IEEE80211_IOC_BSSID,
		bssid, IEEE80211_ADDR_LEN) < 0 ? -1 : 0;
#endif
}

static int
wpa_driver_bsd_get_ssid(void *priv, u8 *ssid)
{
	struct bsd_driver_data *drv = priv;
	return bsd_get_ssid(drv, ssid, 0);
}

static int
wpa_driver_bsd_set_wpa_ie(struct bsd_driver_data *drv, const u8 *wpa_ie,
			  size_t wpa_ie_len)
{
#ifdef IEEE80211_IOC_APPIE
	return bsd_set_opt_ie(drv, wpa_ie, wpa_ie_len);
#else /* IEEE80211_IOC_APPIE */
	return set80211var(drv, IEEE80211_IOC_OPTIE, wpa_ie, wpa_ie_len);
#endif /* IEEE80211_IOC_APPIE */
}

static int
wpa_driver_bsd_set_wpa_internal(void *priv, int wpa, int privacy)
{
	int ret = 0;

	wpa_printf(MSG_DEBUG, "%s: wpa=%d privacy=%d",
		__FUNCTION__, wpa, privacy);

	if (!wpa && wpa_driver_bsd_set_wpa_ie(priv, NULL, 0) < 0)
		ret = -1;
	if (set80211param(priv, IEEE80211_IOC_PRIVACY, privacy) < 0)
		ret = -1;
	if (set80211param(priv, IEEE80211_IOC_WPA, wpa) < 0)
		ret = -1;

	return ret;
}

static int
wpa_driver_bsd_set_wpa(void *priv, int enabled)
{
	wpa_printf(MSG_DEBUG, "%s: enabled=%d", __FUNCTION__, enabled);

	return wpa_driver_bsd_set_wpa_internal(priv, enabled ? 3 : 0, enabled);
}

static int
wpa_driver_bsd_set_countermeasures(void *priv, int enabled)
{
	wpa_printf(MSG_DEBUG, "%s: enabled=%d", __func__, enabled);
	return set80211param(priv, IEEE80211_IOC_COUNTERMEASURES, enabled);
}


static int
wpa_driver_bsd_set_drop_unencrypted(void *priv, int enabled)
{
	wpa_printf(MSG_DEBUG, "%s: enabled=%d", __func__, enabled);
	return set80211param(priv, IEEE80211_IOC_DROPUNENCRYPTED, enabled);
}

static int
wpa_driver_bsd_deauthenticate(void *priv, const u8 *addr, int reason_code)
{
	return bsd_send_mlme_param(priv, IEEE80211_MLME_DEAUTH, reason_code,
				   addr);
}

static int
wpa_driver_bsd_disassociate(void *priv, const u8 *addr, int reason_code)
{
	return bsd_send_mlme_param(priv, IEEE80211_MLME_DISASSOC, reason_code,
				   addr);
}

static int
wpa_driver_bsd_set_auth_alg(void *priv, int auth_alg)
{
	int authmode;

	if ((auth_alg & WPA_AUTH_ALG_OPEN) &&
	    (auth_alg & WPA_AUTH_ALG_SHARED))
		authmode = IEEE80211_AUTH_AUTO;
	else if (auth_alg & WPA_AUTH_ALG_SHARED)
		authmode = IEEE80211_AUTH_SHARED;
	else
		authmode = IEEE80211_AUTH_OPEN;

	return set80211param(priv, IEEE80211_IOC_AUTHMODE, authmode);
}

static void
handle_read(void *ctx, const u8 *src_addr, const u8 *buf, size_t len)
{
	struct bsd_driver_data *drv = ctx;

	drv_event_eapol_rx(drv->ctx, src_addr, buf, len);
}

static int
wpa_driver_bsd_associate(void *priv, struct wpa_driver_associate_params *params)
{
	struct bsd_driver_data *drv = priv;
	struct ieee80211req_mlme mlme;
	u32 mode;
	int privacy;
	int ret = 0;

	wpa_printf(MSG_DEBUG,
		"%s: ssid '%.*s' wpa ie len %u pairwise %u group %u key mgmt %u"
		, __func__
		   , (unsigned int) params->ssid_len, params->ssid
		, (unsigned int) params->wpa_ie_len
		, params->pairwise_suite
		, params->group_suite
		, params->key_mgmt_suite
	);

	switch (params->mode) {
	case IEEE80211_MODE_INFRA:
		mode = 0 /* STA */;
		break;
	case IEEE80211_MODE_IBSS:
		mode = IFM_IEEE80211_IBSS;
		break;
	case IEEE80211_MODE_AP:
		mode = IFM_IEEE80211_HOSTAP;
		break;
	default:
		wpa_printf(MSG_ERROR, "%s: unknown operation mode", __func__);
		return -1;
	}
	if (bsd_set_mediaopt(drv, IFM_OMASK, mode) < 0) {
		wpa_printf(MSG_ERROR, "%s: failed to set operation mode",
			   __func__);
		return -1;
	}

	if (params->mode == IEEE80211_MODE_AP) {
		drv->sock_xmit = l2_packet_init(drv->ifname, NULL, ETH_P_EAPOL,
						handle_read, drv, 0);
		if (drv->sock_xmit == NULL)
			return -1;
		drv->is_ap = 1;
		return 0;
	}

	if (wpa_driver_bsd_set_drop_unencrypted(drv, params->drop_unencrypted)
	    < 0)
		ret = -1;
	if (wpa_driver_bsd_set_auth_alg(drv, params->auth_alg) < 0)
		ret = -1;
	/* XXX error handling is wrong but unclear what to do... */
	if (wpa_driver_bsd_set_wpa_ie(drv, params->wpa_ie, params->wpa_ie_len) < 0)
		return -1;

	privacy = !(params->pairwise_suite == CIPHER_NONE &&
	    params->group_suite == CIPHER_NONE &&
	    params->key_mgmt_suite == KEY_MGMT_NONE &&
	    params->wpa_ie_len == 0);
	wpa_printf(MSG_DEBUG, "%s: set PRIVACY %u", __func__, privacy);

	if (set80211param(drv, IEEE80211_IOC_PRIVACY, privacy) < 0)
		return -1;

	if (params->wpa_ie_len &&
	    set80211param(drv, IEEE80211_IOC_WPA,
			  params->wpa_ie[0] == WLAN_EID_RSN ? 2 : 1) < 0)
		return -1;

	os_memset(&mlme, 0, sizeof(mlme));
	mlme.im_op = IEEE80211_MLME_ASSOC;
	if (params->ssid != NULL)
		os_memcpy(mlme.im_ssid, params->ssid, params->ssid_len);
	mlme.im_ssid_len = params->ssid_len;
	if (params->bssid != NULL)
		os_memcpy(mlme.im_macaddr, params->bssid, IEEE80211_ADDR_LEN);
	if (set80211var(drv, IEEE80211_IOC_MLME, &mlme, sizeof(mlme)) < 0)
		return -1;
	return ret;
}

static int
wpa_driver_bsd_scan(void *priv, struct wpa_driver_scan_params *params)
{
	struct bsd_driver_data *drv = priv;
#ifdef IEEE80211_IOC_SCAN_MAX_SSID
	struct ieee80211_scan_req sr;
	int i;
#endif /* IEEE80211_IOC_SCAN_MAX_SSID */

	if (bsd_set_mediaopt(drv, IFM_OMASK, 0 /* STA */) < 0) {
		wpa_printf(MSG_ERROR, "%s: failed to set operation mode",
			   __func__);
		return -1;
	}

	if (set80211param(drv, IEEE80211_IOC_ROAMING,
			  IEEE80211_ROAMING_MANUAL) < 0) {
		wpa_printf(MSG_ERROR, "%s: failed to set "
			   "wpa_supplicant-based roaming: %s", __func__,
			   strerror(errno));
		return -1;
	}

	if (wpa_driver_bsd_set_wpa(drv, 1) < 0) {
		wpa_printf(MSG_ERROR, "%s: failed to set wpa: %s", __func__,
			   strerror(errno));
		return -1;
	}

	/* NB: interface must be marked UP to do a scan */
	if (bsd_ctrl_iface(drv, 1) < 0)
		return -1;

#ifdef IEEE80211_IOC_SCAN_MAX_SSID
	os_memset(&sr, 0, sizeof(sr));
	sr.sr_flags = IEEE80211_IOC_SCAN_ACTIVE | IEEE80211_IOC_SCAN_ONCE |
		IEEE80211_IOC_SCAN_NOJOIN;
	sr.sr_duration = IEEE80211_IOC_SCAN_FOREVER;
	if (params->num_ssids > 0) {
		sr.sr_nssid = params->num_ssids;
#if 0
		/* Boundary check is done by upper layer */
		if (sr.sr_nssid > IEEE80211_IOC_SCAN_MAX_SSID)
			sr.sr_nssid = IEEE80211_IOC_SCAN_MAX_SSID;
#endif

		/* NB: check scan cache first */
		sr.sr_flags |= IEEE80211_IOC_SCAN_CHECK;
	}
	for (i = 0; i < sr.sr_nssid; i++) {
		sr.sr_ssid[i].len = params->ssids[i].ssid_len;
		os_memcpy(sr.sr_ssid[i].ssid, params->ssids[i].ssid,
			  sr.sr_ssid[i].len);
	}

	/* NB: net80211 delivers a scan complete event so no need to poll */
	return set80211var(drv, IEEE80211_IOC_SCAN_REQ, &sr, sizeof(sr));
#else /* IEEE80211_IOC_SCAN_MAX_SSID */
	/* set desired ssid before scan */
	if (bsd_set_ssid(drv, params->ssids[0].ssid,
			 params->ssids[0].ssid_len) < 0)
		return -1;

	/* NB: net80211 delivers a scan complete event so no need to poll */
	return set80211param(drv, IEEE80211_IOC_SCAN_REQ, 0);
#endif /* IEEE80211_IOC_SCAN_MAX_SSID */
}

static void
wpa_driver_bsd_event_receive(int sock, void *ctx, void *sock_ctx)
{
	struct bsd_driver_data *drv = sock_ctx;
	char *buf;
	struct if_announcemsghdr *ifan;
	struct if_msghdr *ifm;
	struct rt_msghdr *rtm;
	union wpa_event_data event;
	struct ieee80211_michael_event *mic;
	struct ieee80211_leave_event *leave;
	struct ieee80211_join_event *join;
	int n, len;

	len = rtbuf_len();

	buf = os_malloc(len);
	if (buf == NULL) {
		wpa_printf(MSG_ERROR, "%s os_malloc() failed\n", __func__);
		return;
	}

	n = read(sock, buf, len);
	if (n < 0) {
		if (errno != EINTR && errno != EAGAIN)
			wpa_printf(MSG_ERROR, "%s read() failed: %s\n",
				   __func__, strerror(errno));
		os_free(buf);
		return;
	}

	rtm = (struct rt_msghdr *) buf;
	if (rtm->rtm_version != RTM_VERSION) {
		wpa_printf(MSG_DEBUG, "Invalid routing message version=%d",
			   rtm->rtm_version);
		os_free(buf);
		return;
	}
	os_memset(&event, 0, sizeof(event));
	switch (rtm->rtm_type) {
	case RTM_IFANNOUNCE:
		ifan = (struct if_announcemsghdr *) rtm;
		if (ifan->ifan_index != drv->ifindex)
			break;
		os_strlcpy(event.interface_status.ifname, drv->ifname,
			   sizeof(event.interface_status.ifname));
		switch (ifan->ifan_what) {
		case IFAN_DEPARTURE:
			event.interface_status.ievent = EVENT_INTERFACE_REMOVED;
		default:
			os_free(buf);
			return;
		}
		wpa_printf(MSG_DEBUG, "RTM_IFANNOUNCE: Interface '%s' %s",
			   event.interface_status.ifname,
			   ifan->ifan_what == IFAN_DEPARTURE ?
				"removed" : "added");
		wpa_supplicant_event(ctx, EVENT_INTERFACE_STATUS, &event);
		break;
	case RTM_IEEE80211:
		ifan = (struct if_announcemsghdr *) rtm;
		if (ifan->ifan_index != drv->ifindex)
			break;
		switch (ifan->ifan_what) {
		case RTM_IEEE80211_ASSOC:
		case RTM_IEEE80211_REASSOC:
			if (drv->is_ap)
				break;
			wpa_supplicant_event(ctx, EVENT_ASSOC, NULL);
			break;
		case RTM_IEEE80211_DISASSOC:
			if (drv->is_ap)
				break;
			wpa_supplicant_event(ctx, EVENT_DISASSOC, NULL);
			break;
		case RTM_IEEE80211_SCAN:
			if (drv->is_ap)
				break;
			wpa_supplicant_event(ctx, EVENT_SCAN_RESULTS, NULL);
			break;
		case RTM_IEEE80211_LEAVE:
			leave = (struct ieee80211_leave_event *) &ifan[1];
			drv_event_disassoc(ctx, leave->iev_addr);
			break;
		case RTM_IEEE80211_JOIN:
#ifdef RTM_IEEE80211_REJOIN
		case RTM_IEEE80211_REJOIN:
#endif
			join = (struct ieee80211_join_event *) &ifan[1];
			bsd_new_sta(drv, ctx, join->iev_addr);
			break;
		case RTM_IEEE80211_REPLAY:
			/* ignore */
			break;
		case RTM_IEEE80211_MICHAEL:
			mic = (struct ieee80211_michael_event *) &ifan[1];
			wpa_printf(MSG_DEBUG,
				"Michael MIC failure wireless event: "
				"keyix=%u src_addr=" MACSTR, mic->iev_keyix,
				MAC2STR(mic->iev_src));

			os_memset(&event, 0, sizeof(event));
			event.michael_mic_failure.unicast =
				!IEEE80211_IS_MULTICAST(mic->iev_dst);
			wpa_supplicant_event(ctx, EVENT_MICHAEL_MIC_FAILURE,
				&event);
			break;
		}
		break;
	case RTM_IFINFO:
		ifm = (struct if_msghdr *) rtm;
		if (ifm->ifm_index != drv->ifindex)
			break;
		if ((rtm->rtm_flags & RTF_UP) == 0) {
			os_strlcpy(event.interface_status.ifname, drv->ifname,
				   sizeof(event.interface_status.ifname));
			event.interface_status.ievent = EVENT_INTERFACE_REMOVED;
			wpa_printf(MSG_DEBUG, "RTM_IFINFO: Interface '%s' DOWN",
				   event.interface_status.ifname);
			wpa_supplicant_event(ctx, EVENT_INTERFACE_STATUS, &event);
		}
		break;
	}
	os_free(buf);
}

static void
wpa_driver_bsd_add_scan_entry(struct wpa_scan_results *res,
			      struct ieee80211req_scan_result *sr)
{
	struct wpa_scan_res *result, **tmp;
	size_t extra_len;
	u8 *pos;

	extra_len = 2 + sr->isr_ssid_len;
	extra_len += 2 + sr->isr_nrates;
	extra_len += 3; /* ERP IE */
	extra_len += sr->isr_ie_len;

	result = os_zalloc(sizeof(*result) + extra_len);
	if (result == NULL)
		return;
	os_memcpy(result->bssid, sr->isr_bssid, ETH_ALEN);
	result->freq = sr->isr_freq;
	result->beacon_int = sr->isr_intval;
	result->caps = sr->isr_capinfo;
	result->qual = sr->isr_rssi;
	result->noise = sr->isr_noise;

	pos = (u8 *)(result + 1);

	*pos++ = WLAN_EID_SSID;
	*pos++ = sr->isr_ssid_len;
	os_memcpy(pos, sr + 1, sr->isr_ssid_len);
	pos += sr->isr_ssid_len;

	/*
	 * Deal all rates as supported rate.
	 * Because net80211 doesn't report extended supported rate or not.
	 */
	*pos++ = WLAN_EID_SUPP_RATES;
	*pos++ = sr->isr_nrates;
	os_memcpy(pos, sr->isr_rates, sr->isr_nrates);
	pos += sr->isr_nrates;

	*pos++ = WLAN_EID_ERP_INFO;
	*pos++ = 1;
	*pos++ = sr->isr_erp;

	os_memcpy(pos, (u8 *)(sr + 1) + sr->isr_ssid_len, sr->isr_ie_len);
	pos += sr->isr_ie_len;

	result->ie_len = pos - (u8 *)(result + 1);

	tmp = os_realloc(res->res,
			 (res->num + 1) * sizeof(struct wpa_scan_res *));
	if (tmp == NULL) {
		os_free(result);
		return;
	}
	tmp[res->num++] = result;
	res->res = tmp;
}

struct wpa_scan_results *
wpa_driver_bsd_get_scan_results2(void *priv)
{
	struct ieee80211req_scan_result *sr;
	struct wpa_scan_results *res;
	int len, rest;
	uint8_t buf[24*1024], *pos;

	len = get80211var(priv, IEEE80211_IOC_SCAN_RESULTS, buf, 24*1024);
	if (len < 0)
		return NULL;

	res = os_zalloc(sizeof(*res));
	if (res == NULL)
		return NULL;

	pos = buf;
	rest = len;
	while (rest >= sizeof(struct ieee80211req_scan_result)) {
		sr = (struct ieee80211req_scan_result *)pos;
		wpa_driver_bsd_add_scan_entry(res, sr);
		pos += sr->isr_len;
		rest -= sr->isr_len;
	}

	wpa_printf(MSG_DEBUG, "Received %d bytes of scan results (%lu BSSes)",
		   len, (unsigned long)res->num);

	return res;
}

static int wpa_driver_bsd_capa(struct bsd_driver_data *drv)
{
#ifdef IEEE80211_IOC_DEVCAPS
/* kernel definitions copied from net80211/ieee80211_var.h */
#define IEEE80211_CIPHER_WEP            0
#define IEEE80211_CIPHER_TKIP           1
#define IEEE80211_CIPHER_AES_CCM        3
#define IEEE80211_CRYPTO_WEP            (1<<IEEE80211_CIPHER_WEP)
#define IEEE80211_CRYPTO_TKIP           (1<<IEEE80211_CIPHER_TKIP)
#define IEEE80211_CRYPTO_AES_CCM        (1<<IEEE80211_CIPHER_AES_CCM)
#define IEEE80211_C_HOSTAP      0x00000400      /* CAPABILITY: HOSTAP avail */
#define IEEE80211_C_WPA1        0x00800000      /* CAPABILITY: WPA1 avail */
#define IEEE80211_C_WPA2        0x01000000      /* CAPABILITY: WPA2 avail */
	struct ieee80211_devcaps_req devcaps;

	if (get80211var(drv, IEEE80211_IOC_DEVCAPS, &devcaps,
			sizeof(devcaps)) < 0) {
		wpa_printf(MSG_ERROR, "failed to IEEE80211_IOC_DEVCAPS: %s",
			   strerror(errno));
		return -1;
	}

	wpa_printf(MSG_DEBUG, "%s: drivercaps=0x%08x,cryptocaps=0x%08x",
		   __func__, devcaps.dc_drivercaps, devcaps.dc_cryptocaps);

	if (devcaps.dc_drivercaps & IEEE80211_C_WPA1)
		drv->capa.key_mgmt = WPA_DRIVER_CAPA_KEY_MGMT_WPA |
			WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK;
	if (devcaps.dc_drivercaps & IEEE80211_C_WPA2)
		drv->capa.key_mgmt = WPA_DRIVER_CAPA_KEY_MGMT_WPA2 |
			WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK;

	if (devcaps.dc_cryptocaps & IEEE80211_CRYPTO_WEP)
		drv->capa.enc |= WPA_DRIVER_CAPA_ENC_WEP40 |
			WPA_DRIVER_CAPA_ENC_WEP104;
	if (devcaps.dc_cryptocaps & IEEE80211_CRYPTO_TKIP)
		drv->capa.enc |= WPA_DRIVER_CAPA_ENC_TKIP;
	if (devcaps.dc_cryptocaps & IEEE80211_CRYPTO_AES_CCM)
		drv->capa.enc |= WPA_DRIVER_CAPA_ENC_CCMP;

	if (devcaps.dc_drivercaps & IEEE80211_C_HOSTAP)
		drv->capa.flags |= WPA_DRIVER_FLAGS_AP;
#undef IEEE80211_CIPHER_WEP
#undef IEEE80211_CIPHER_TKIP
#undef IEEE80211_CIPHER_AES_CCM
#undef IEEE80211_CRYPTO_WEP
#undef IEEE80211_CRYPTO_TKIP
#undef IEEE80211_CRYPTO_AES_CCM
#undef IEEE80211_C_HOSTAP
#undef IEEE80211_C_WPA1
#undef IEEE80211_C_WPA2
#else /* IEEE80211_IOC_DEVCAPS */
	/* For now, assume TKIP, CCMP, WPA, WPA2 are supported */
	drv->capa.key_mgmt = WPA_DRIVER_CAPA_KEY_MGMT_WPA |
		WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK |
		WPA_DRIVER_CAPA_KEY_MGMT_WPA2 |
		WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK;
	drv->capa.enc = WPA_DRIVER_CAPA_ENC_WEP40 |
		WPA_DRIVER_CAPA_ENC_WEP104 |
		WPA_DRIVER_CAPA_ENC_TKIP |
		WPA_DRIVER_CAPA_ENC_CCMP;
	drv->capa.flags |= WPA_DRIVER_FLAGS_AP;
#endif /* IEEE80211_IOC_DEVCAPS */
#ifdef IEEE80211_IOC_SCAN_MAX_SSID
	drv->capa.max_scan_ssids = IEEE80211_IOC_SCAN_MAX_SSID;
#else /* IEEE80211_IOC_SCAN_MAX_SSID */
	drv->capa.max_scan_ssids = 1;
#endif /* IEEE80211_IOC_SCAN_MAX_SSID */
	drv->capa.auth = WPA_DRIVER_AUTH_OPEN |
		WPA_DRIVER_AUTH_SHARED |
		WPA_DRIVER_AUTH_LEAP;
	return 0;
}

static void *
wpa_driver_bsd_init(void *ctx, const char *ifname)
{
#define	GETPARAM(drv, param, v) \
	(((v) = get80211param(drv, param)) != -1)
	struct bsd_driver_data *drv;

	drv = os_zalloc(sizeof(*drv));
	if (drv == NULL)
		return NULL;
	/*
	 * NB: We require the interface name be mappable to an index.
	 *     This implies we do not support having wpa_supplicant
	 *     wait for an interface to appear.  This seems ok; that
	 *     doesn't belong here; it's really the job of devd.
	 */
	drv->ifindex = if_nametoindex(ifname);
	if (drv->ifindex == 0) {
		wpa_printf(MSG_DEBUG, "%s: interface %s does not exist",
			   __func__, ifname);
		goto fail1;
	}
	drv->sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (drv->sock < 0)
		goto fail1;
	drv->route = socket(PF_ROUTE, SOCK_RAW, 0);
	if (drv->route < 0)
		goto fail;
	eloop_register_read_sock(drv->route,
		wpa_driver_bsd_event_receive, ctx, drv);

	drv->ctx = ctx;
	os_strlcpy(drv->ifname, ifname, sizeof(drv->ifname));

	/* Down interface during setup. */
	if (bsd_ctrl_iface(drv, 0) < 0)
		goto fail;

	if (!GETPARAM(drv, IEEE80211_IOC_ROAMING, drv->prev_roaming)) {
		wpa_printf(MSG_DEBUG, "%s: failed to get roaming state: %s",
			__func__, strerror(errno));
		goto fail;
	}
	if (!GETPARAM(drv, IEEE80211_IOC_PRIVACY, drv->prev_privacy)) {
		wpa_printf(MSG_DEBUG, "%s: failed to get privacy state: %s",
			__func__, strerror(errno));
		goto fail;
	}
	if (!GETPARAM(drv, IEEE80211_IOC_WPA, drv->prev_wpa)) {
		wpa_printf(MSG_DEBUG, "%s: failed to get wpa state: %s",
			__func__, strerror(errno));
		goto fail;
	}

	if (wpa_driver_bsd_capa(drv))
		goto fail;

	return drv;
fail:
	close(drv->sock);
fail1:
	os_free(drv);
	return NULL;
#undef GETPARAM
}

static void
wpa_driver_bsd_deinit(void *priv)
{
	struct bsd_driver_data *drv = priv;

	wpa_driver_bsd_set_wpa(drv, 0);
	eloop_unregister_read_sock(drv->route);

	/* NB: mark interface down */
	bsd_ctrl_iface(drv, 0);

	wpa_driver_bsd_set_wpa_internal(drv, drv->prev_wpa, drv->prev_privacy);
	if (set80211param(drv, IEEE80211_IOC_ROAMING, drv->prev_roaming) < 0)
		wpa_printf(MSG_DEBUG, "%s: failed to restore roaming state",
			__func__);

	if (drv->sock_xmit != NULL)
		l2_packet_deinit(drv->sock_xmit);
	(void) close(drv->route);		/* ioctl socket */
	(void) close(drv->sock);		/* event socket */
	os_free(drv);
}

static int
wpa_driver_bsd_get_capa(void *priv, struct wpa_driver_capa *capa)
{
	struct bsd_driver_data *drv = priv;

	os_memcpy(capa, &drv->capa, sizeof(*capa));
	return 0;
}
#endif /* HOSTAPD */


const struct wpa_driver_ops wpa_driver_bsd_ops = {
	.name			= "bsd",
	.desc			= "BSD 802.11 support",
#ifdef HOSTAPD
	.hapd_init		= bsd_init,
	.hapd_deinit		= bsd_deinit,
	.set_privacy		= bsd_set_privacy,
	.get_seqnum		= bsd_get_seqnum,
	.flush			= bsd_flush,
	.read_sta_data		= bsd_read_sta_driver_data,
	.sta_disassoc		= bsd_sta_disassoc,
	.sta_deauth		= bsd_sta_deauth,
#else /* HOSTAPD */
	.init			= wpa_driver_bsd_init,
	.deinit			= wpa_driver_bsd_deinit,
	.get_bssid		= wpa_driver_bsd_get_bssid,
	.get_ssid		= wpa_driver_bsd_get_ssid,
	.set_countermeasures	= wpa_driver_bsd_set_countermeasures,
	.scan2			= wpa_driver_bsd_scan,
	.get_scan_results2	= wpa_driver_bsd_get_scan_results2,
	.deauthenticate		= wpa_driver_bsd_deauthenticate,
	.disassociate		= wpa_driver_bsd_disassociate,
	.associate		= wpa_driver_bsd_associate,
	.get_capa		= wpa_driver_bsd_get_capa,
#endif /* HOSTAPD */
	.set_freq		= bsd_set_freq,
	.set_key		= bsd_set_key,
	.set_ieee8021x		= bsd_set_ieee8021x,
	.hapd_set_ssid		= bsd_set_ssid,
	.hapd_get_ssid		= bsd_get_ssid,
	.hapd_send_eapol	= bsd_send_eapol,
	.sta_set_flags		= bsd_set_sta_authorized,
	.set_generic_elem	= bsd_set_opt_ie,
};
