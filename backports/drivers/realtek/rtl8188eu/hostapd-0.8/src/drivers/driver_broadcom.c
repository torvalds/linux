/*
 * WPA Supplicant - driver interaction with old Broadcom wl.o driver
 * Copyright (c) 2004, Nikki Chumkov <nikki@gattaca.ru>
 * Copyright (c) 2004, Jouni Malinen <j@w1.fi>
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
 * Please note that the newer Broadcom driver ("hybrid Linux driver") supports
 * Linux wireless extensions and does not need (or even work) with this old
 * driver wrapper. Use driver_wext.c with that driver.
 */

#include "includes.h"

#include <sys/ioctl.h>

#include "common.h"

#if 0
#include <netpacket/packet.h>
#include <net/ethernet.h>     /* the L2 protocols */
#else
#include <linux/if_packet.h>
#include <linux/if_ether.h>   /* The L2 protocols */
#endif
#include <net/if.h>
#include <typedefs.h>

/* wlioctl.h is a Broadcom header file and it is available, e.g., from Linksys
 * WRT54G GPL tarball. */
#include <wlioctl.h>

#include "driver.h"
#include "eloop.h"

struct wpa_driver_broadcom_data {
	void *ctx;
	int ioctl_sock;
	int event_sock;
	char ifname[IFNAMSIZ + 1];
};


#ifndef WLC_DEAUTHENTICATE
#define WLC_DEAUTHENTICATE 143
#endif
#ifndef WLC_DEAUTHENTICATE_WITH_REASON
#define WLC_DEAUTHENTICATE_WITH_REASON 201
#endif
#ifndef WLC_SET_TKIP_COUNTERMEASURES
#define WLC_SET_TKIP_COUNTERMEASURES 202
#endif

#if !defined(PSK_ENABLED) /* NEW driver interface */
#define WL_VERSION 360130
/* wireless authentication bit vector */
#define WPA_ENABLED 1
#define PSK_ENABLED 2
                                                                                
#define WAUTH_WPA_ENABLED(wauth)  ((wauth) & WPA_ENABLED)
#define WAUTH_PSK_ENABLED(wauth)  ((wauth) & PSK_ENABLED)
#define WAUTH_ENABLED(wauth)    ((wauth) & (WPA_ENABLED | PSK_ENABLED))

#define WSEC_PRIMARY_KEY WL_PRIMARY_KEY

typedef wl_wsec_key_t wsec_key_t;
#endif

typedef struct {
	uint32 val;
	struct ether_addr ea;
	uint16 res;
} wlc_deauth_t;


static void wpa_driver_broadcom_scan_timeout(void *eloop_ctx,
					     void *timeout_ctx);

static int broadcom_ioctl(struct wpa_driver_broadcom_data *drv, int cmd,
			  void *buf, int len)
{
	struct ifreq ifr;
	wl_ioctl_t ioc;
	int ret = 0;

	wpa_printf(MSG_MSGDUMP, "BROADCOM: wlioctl(%s,%d,len=%d,val=%p)",
		   drv->ifname, cmd, len, buf);
	/* wpa_hexdump(MSG_MSGDUMP, "BROADCOM: wlioctl buf", buf, len); */

	ioc.cmd = cmd;
	ioc.buf = buf;
	ioc.len = len;
	os_strlcpy(ifr.ifr_name, drv->ifname, IFNAMSIZ);
	ifr.ifr_data = (caddr_t) &ioc;
	if ((ret = ioctl(drv->ioctl_sock, SIOCDEVPRIVATE, &ifr)) < 0) {
		if (cmd != WLC_GET_MAGIC)
			perror(ifr.ifr_name);
		wpa_printf(MSG_MSGDUMP, "BROADCOM: wlioctl cmd=%d res=%d",
			   cmd, ret);
	}

	return ret;
}

static int wpa_driver_broadcom_get_bssid(void *priv, u8 *bssid)
{
	struct wpa_driver_broadcom_data *drv = priv;
	if (broadcom_ioctl(drv, WLC_GET_BSSID, bssid, ETH_ALEN) == 0)
		return 0;
	
	os_memset(bssid, 0, ETH_ALEN);
	return -1;
}

static int wpa_driver_broadcom_get_ssid(void *priv, u8 *ssid)
{
	struct wpa_driver_broadcom_data *drv = priv;
	wlc_ssid_t s;
	
	if (broadcom_ioctl(drv, WLC_GET_SSID, &s, sizeof(s)) == -1)
		return -1;

	os_memcpy(ssid, s.SSID, s.SSID_len);
	return s.SSID_len;
}

static int wpa_driver_broadcom_set_wpa(void *priv, int enable)
{
	struct wpa_driver_broadcom_data *drv = priv;
	unsigned int wauth, wsec;
	struct ether_addr ea;

	os_memset(&ea, enable ? 0xff : 0, sizeof(ea));
	if (broadcom_ioctl(drv, WLC_GET_WPA_AUTH, &wauth, sizeof(wauth)) ==
	    -1 ||
	    broadcom_ioctl(drv, WLC_GET_WSEC, &wsec, sizeof(wsec)) == -1)
		return -1;

	if (enable) {
		wauth = PSK_ENABLED;
		wsec = TKIP_ENABLED;
	} else {
		wauth = 255;
		wsec &= ~(TKIP_ENABLED | AES_ENABLED);
	}

	if (broadcom_ioctl(drv, WLC_SET_WPA_AUTH, &wauth, sizeof(wauth)) ==
	    -1 ||
	    broadcom_ioctl(drv, WLC_SET_WSEC, &wsec, sizeof(wsec)) == -1)
		return -1;

	/* FIX: magic number / error handling? */
	broadcom_ioctl(drv, 122, &ea, sizeof(ea));

	return 0;
}

static int wpa_driver_broadcom_set_key(const char *ifname, void *priv,
				       enum wpa_alg alg,
				       const u8 *addr, int key_idx, int set_tx,
				       const u8 *seq, size_t seq_len,
				       const u8 *key, size_t key_len)
{
	struct wpa_driver_broadcom_data *drv = priv;
	int ret;
	wsec_key_t wkt;

	os_memset(&wkt, 0, sizeof wkt);
	wpa_printf(MSG_MSGDUMP, "BROADCOM: SET %sKEY[%d] alg=%d",
		   set_tx ? "PRIMARY " : "", key_idx, alg);
	if (key && key_len > 0)
		wpa_hexdump_key(MSG_MSGDUMP, "BROADCOM: key", key, key_len);

	switch (alg) {
	case WPA_ALG_NONE:
		wkt.algo = CRYPTO_ALGO_OFF;
		break;
	case WPA_ALG_WEP:
		wkt.algo = CRYPTO_ALGO_WEP128; /* CRYPTO_ALGO_WEP1? */
		break;
	case WPA_ALG_TKIP:
		wkt.algo = 0; /* CRYPTO_ALGO_TKIP? */
		break;
	case WPA_ALG_CCMP:
		wkt.algo = 0; /* CRYPTO_ALGO_AES_CCM;
			       * AES_OCB_MSDU, AES_OCB_MPDU? */
		break;
	default:
		wkt.algo = CRYPTO_ALGO_NALG;
		break;
	}

	if (seq && seq_len > 0)
		wpa_hexdump(MSG_MSGDUMP, "BROADCOM: SEQ", seq, seq_len);

	if (addr)
		wpa_hexdump(MSG_MSGDUMP, "BROADCOM: addr", addr, ETH_ALEN);

	wkt.index = key_idx;
	wkt.len = key_len;
	if (key && key_len > 0) {
		os_memcpy(wkt.data, key, key_len);
		if (key_len == 32) {
			/* hack hack hack XXX */
			os_memcpy(&wkt.data[16], &key[24], 8);
			os_memcpy(&wkt.data[24], &key[16], 8);
		}
	}
	/* wkt.algo = CRYPTO_ALGO_...; */
	wkt.flags = set_tx ? 0 : WSEC_PRIMARY_KEY;
	if (addr && set_tx)
		os_memcpy(&wkt.ea, addr, sizeof(wkt.ea));
	ret = broadcom_ioctl(drv, WLC_SET_KEY, &wkt, sizeof(wkt));
	if (addr && set_tx) {
		/* FIX: magic number / error handling? */
		broadcom_ioctl(drv, 121, &wkt.ea, sizeof(wkt.ea));
	}
	return ret;
}


static void wpa_driver_broadcom_event_receive(int sock, void *ctx,
					      void *sock_ctx)
{
	char buf[8192];
	int left;
	wl_wpa_header_t *wwh;
	union wpa_event_data data;
	u8 *resp_ies = NULL;

	if ((left = recv(sock, buf, sizeof buf, 0)) < 0)
		return;

	wpa_hexdump(MSG_DEBUG, "RECEIVE EVENT", (u8 *) buf, left);

	if ((size_t) left < sizeof(wl_wpa_header_t))
		return;

	wwh = (wl_wpa_header_t *) buf;

	if (wwh->snap.type != WL_WPA_ETHER_TYPE)
		return;
	if (os_memcmp(&wwh->snap, wl_wpa_snap_template, 6) != 0)
		return;

	os_memset(&data, 0, sizeof(data));

	switch (wwh->type) {
	case WLC_ASSOC_MSG:
		left -= WL_WPA_HEADER_LEN;
		wpa_printf(MSG_DEBUG, "BROADCOM: ASSOC MESSAGE (left: %d)",
			   left);
		if (left > 0) {
			resp_ies = os_malloc(left);
			if (resp_ies == NULL)
				return;
			os_memcpy(resp_ies, buf + WL_WPA_HEADER_LEN, left);
			data.assoc_info.resp_ies = resp_ies;
			data.assoc_info.resp_ies_len = left;
		}

		wpa_supplicant_event(ctx, EVENT_ASSOC, &data);
		os_free(resp_ies);
		break;
	case WLC_DISASSOC_MSG:
		wpa_printf(MSG_DEBUG, "BROADCOM: DISASSOC MESSAGE");
		wpa_supplicant_event(ctx, EVENT_DISASSOC, NULL);
		break;
	case WLC_PTK_MIC_MSG:
		wpa_printf(MSG_DEBUG, "BROADCOM: PTK MIC MSG MESSAGE");
		data.michael_mic_failure.unicast = 1;
		wpa_supplicant_event(ctx, EVENT_MICHAEL_MIC_FAILURE, &data);
		break;
	case WLC_GTK_MIC_MSG:
		wpa_printf(MSG_DEBUG, "BROADCOM: GTK MIC MSG MESSAGE");
		data.michael_mic_failure.unicast = 0;
		wpa_supplicant_event(ctx, EVENT_MICHAEL_MIC_FAILURE, &data);
		break;
	default:
		wpa_printf(MSG_DEBUG, "BROADCOM: UNKNOWN MESSAGE (%d)",
			   wwh->type);
		break;
	}
}	

static void * wpa_driver_broadcom_init(void *ctx, const char *ifname)
{
	int s;
	struct sockaddr_ll ll;
	struct wpa_driver_broadcom_data *drv;
	struct ifreq ifr;

	/* open socket to kernel */
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("socket");
		return NULL;
	}
	/* do it */
	os_strlcpy(ifr.ifr_name, ifname, IFNAMSIZ);
	if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
		perror(ifr.ifr_name);
		return NULL;
	}


	drv = os_zalloc(sizeof(*drv));
	if (drv == NULL)
		return NULL;
	drv->ctx = ctx;
	os_strlcpy(drv->ifname, ifname, sizeof(drv->ifname));
	drv->ioctl_sock = s;

	s = socket(PF_PACKET, SOCK_RAW, ntohs(ETH_P_802_2));
	if (s < 0) {
		perror("socket(PF_PACKET, SOCK_RAW, ntohs(ETH_P_802_2))");
		close(drv->ioctl_sock);
		os_free(drv);
		return NULL;
	}

	os_memset(&ll, 0, sizeof(ll));
	ll.sll_family = AF_PACKET;
	ll.sll_protocol = ntohs(ETH_P_802_2);
	ll.sll_ifindex = ifr.ifr_ifindex;
	ll.sll_hatype = 0;
	ll.sll_pkttype = PACKET_HOST;
	ll.sll_halen = 0;

	if (bind(s, (struct sockaddr *) &ll, sizeof(ll)) < 0) {
		perror("bind(netlink)");
		close(s);
		close(drv->ioctl_sock);
		os_free(drv);
		return NULL;
	}

	eloop_register_read_sock(s, wpa_driver_broadcom_event_receive, ctx,
				 NULL);
	drv->event_sock = s;
	wpa_driver_broadcom_set_wpa(drv, 1);

	return drv;
}

static void wpa_driver_broadcom_deinit(void *priv)
{
	struct wpa_driver_broadcom_data *drv = priv;
	wpa_driver_broadcom_set_wpa(drv, 0);
	eloop_cancel_timeout(wpa_driver_broadcom_scan_timeout, drv, drv->ctx);
	eloop_unregister_read_sock(drv->event_sock);
	close(drv->event_sock);
	close(drv->ioctl_sock);
	os_free(drv);
}

static int wpa_driver_broadcom_set_countermeasures(void *priv,
						   int enabled)
{
#if 0
	struct wpa_driver_broadcom_data *drv = priv;
	/* FIX: ? */
	return broadcom_ioctl(drv, WLC_SET_TKIP_COUNTERMEASURES, &enabled,
			      sizeof(enabled));
#else
	return 0;
#endif
}

static int wpa_driver_broadcom_set_drop_unencrypted(void *priv, int enabled)
{
	struct wpa_driver_broadcom_data *drv = priv;
	/* SET_EAP_RESTRICT, SET_WEP_RESTRICT */
	int _restrict = (enabled ? 1 : 0);
	
	if (broadcom_ioctl(drv, WLC_SET_WEP_RESTRICT, 
			   &_restrict, sizeof(_restrict)) < 0 ||
	    broadcom_ioctl(drv, WLC_SET_EAP_RESTRICT,
			   &_restrict, sizeof(_restrict)) < 0)
		return -1;

	return 0;
}

static void wpa_driver_broadcom_scan_timeout(void *eloop_ctx,
					     void *timeout_ctx)
{
	wpa_printf(MSG_DEBUG, "Scan timeout - try to get results");
	wpa_supplicant_event(timeout_ctx, EVENT_SCAN_RESULTS, NULL);
}

static int wpa_driver_broadcom_scan(void *priv,
				    struct wpa_driver_scan_params *params)
{
	struct wpa_driver_broadcom_data *drv = priv;
	wlc_ssid_t wst = { 0, "" };
	const u8 *ssid = params->ssids[0].ssid;
	size_t ssid_len = params->ssids[0].ssid_len;

	if (ssid && ssid_len > 0 && ssid_len <= sizeof(wst.SSID)) {
		wst.SSID_len = ssid_len;
		os_memcpy(wst.SSID, ssid, ssid_len);
	}
	
	if (broadcom_ioctl(drv, WLC_SCAN, &wst, sizeof(wst)) < 0)
		return -1;

	eloop_cancel_timeout(wpa_driver_broadcom_scan_timeout, drv, drv->ctx);
	eloop_register_timeout(3, 0, wpa_driver_broadcom_scan_timeout, drv,
			       drv->ctx);
	return 0;
}


static const int frequency_list[] = { 
	2412, 2417, 2422, 2427, 2432, 2437, 2442,
	2447, 2452, 2457, 2462, 2467, 2472, 2484 
};

struct bss_ie_hdr {
	u8 elem_id;
	u8 len;
	u8 oui[3];
	/* u8 oui_type; */
	/* u16 version; */
} __attribute__ ((packed));

static struct wpa_scan_results *
wpa_driver_broadcom_get_scan_results(void *priv)
{
	struct wpa_driver_broadcom_data *drv = priv;
	char *buf;
	wl_scan_results_t *wsr;
	wl_bss_info_t *wbi;
	size_t ap_num;
	struct wpa_scan_results *res;

	buf = os_malloc(WLC_IOCTL_MAXLEN);
	if (buf == NULL)
		return NULL;

	wsr = (wl_scan_results_t *) buf;

	wsr->buflen = WLC_IOCTL_MAXLEN - sizeof(wsr);
	wsr->version = 107;
	wsr->count = 0;

	if (broadcom_ioctl(drv, WLC_SCAN_RESULTS, buf, WLC_IOCTL_MAXLEN) < 0) {
		os_free(buf);
		return NULL;
	}

	res = os_zalloc(sizeof(*res));
	if (res == NULL) {
		os_free(buf);
		return NULL;
	}

	res->res = os_zalloc(wsr->count * sizeof(struct wpa_scan_res *));
	if (res->res == NULL) {
		os_free(res);
		os_free(buf);
		return NULL;
	}

	for (ap_num = 0, wbi = wsr->bss_info; ap_num < wsr->count; ++ap_num) {
		struct wpa_scan_res *r;
		r = os_malloc(sizeof(*r) + wbi->ie_length);
		if (r == NULL)
			break;
		res->res[res->num++] = r;

		os_memcpy(r->bssid, &wbi->BSSID, ETH_ALEN);
		r->freq = frequency_list[wbi->channel - 1];
		/* get ie's */
		os_memcpy(r + 1, wbi + 1, wbi->ie_length);
		r->ie_len = wbi->ie_length;

		wbi = (wl_bss_info_t *) ((u8 *) wbi + wbi->length);
	}

	wpa_printf(MSG_MSGDUMP, "Received %d bytes of scan results (%lu "
		   "BSSes)",
		   wsr->buflen, (unsigned long) ap_num);
	
	os_free(buf);
	return res;
	}

static int wpa_driver_broadcom_deauthenticate(void *priv, const u8 *addr,
					      int reason_code)
{
	struct wpa_driver_broadcom_data *drv = priv;
	wlc_deauth_t wdt;
	wdt.val = reason_code;
	os_memcpy(&wdt.ea, addr, sizeof wdt.ea);
	wdt.res = 0x7fff;
	return broadcom_ioctl(drv, WLC_DEAUTHENTICATE_WITH_REASON, &wdt,
			      sizeof(wdt));
}

static int wpa_driver_broadcom_disassociate(void *priv, const u8 *addr,
					    int reason_code)
{
	struct wpa_driver_broadcom_data *drv = priv;
	return broadcom_ioctl(drv, WLC_DISASSOC, NULL, 0);
}

static int
wpa_driver_broadcom_associate(void *priv,
			      struct wpa_driver_associate_params *params)
{
	struct wpa_driver_broadcom_data *drv = priv;
	wlc_ssid_t s;
	int infra = 1;
	int auth = 0;
	int wsec = 4;
	int dummy;
	int wpa_auth;
	int ret;

	ret = wpa_driver_broadcom_set_drop_unencrypted(
		drv, params->drop_unencrypted);

	s.SSID_len = params->ssid_len;
	os_memcpy(s.SSID, params->ssid, params->ssid_len);

	switch (params->pairwise_suite) {
	case CIPHER_WEP40:
	case CIPHER_WEP104:
		wsec = 1;
		break;

	case CIPHER_TKIP:
		wsec = 2;
		break;

	case CIPHER_CCMP:
		wsec = 4;
		break;

	default:
		wsec = 0;
		break;
	}

	switch (params->key_mgmt_suite) {
	case KEY_MGMT_802_1X:
		wpa_auth = 1;
		break;

	case KEY_MGMT_PSK:
		wpa_auth = 2;
		break;

	default:
		wpa_auth = 255;
		break;
	}

	/* printf("broadcom_associate: %u %u %u\n", pairwise_suite,
	 * group_suite, key_mgmt_suite);
	 * broadcom_ioctl(ifname, WLC_GET_WSEC, &wsec, sizeof(wsec));
	 * wl join uses wlc_sec_wep here, not wlc_set_wsec */

	if (broadcom_ioctl(drv, WLC_SET_WSEC, &wsec, sizeof(wsec)) < 0 ||
	    broadcom_ioctl(drv, WLC_SET_WPA_AUTH, &wpa_auth,
			   sizeof(wpa_auth)) < 0 ||
	    broadcom_ioctl(drv, WLC_GET_WEP, &dummy, sizeof(dummy)) < 0 ||
	    broadcom_ioctl(drv, WLC_SET_INFRA, &infra, sizeof(infra)) < 0 ||
	    broadcom_ioctl(drv, WLC_SET_AUTH, &auth, sizeof(auth)) < 0 ||
	    broadcom_ioctl(drv, WLC_SET_WEP, &wsec, sizeof(wsec)) < 0 ||
	    broadcom_ioctl(drv, WLC_SET_SSID, &s, sizeof(s)) < 0)
		return -1;

	return ret;
}

const struct wpa_driver_ops wpa_driver_broadcom_ops = {
	.name = "broadcom",
	.desc = "Broadcom wl.o driver",
	.get_bssid = wpa_driver_broadcom_get_bssid,
	.get_ssid = wpa_driver_broadcom_get_ssid,
	.set_key = wpa_driver_broadcom_set_key,
	.init = wpa_driver_broadcom_init,
	.deinit = wpa_driver_broadcom_deinit,
	.set_countermeasures = wpa_driver_broadcom_set_countermeasures,
	.scan2 = wpa_driver_broadcom_scan,
	.get_scan_results2 = wpa_driver_broadcom_get_scan_results,
	.deauthenticate = wpa_driver_broadcom_deauthenticate,
	.disassociate = wpa_driver_broadcom_disassociate,
	.associate = wpa_driver_broadcom_associate,
};
