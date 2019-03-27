/*
 * Driver interaction with OpenBSD net80211 layer
 * Copyright (c) 2013, Mark Kettenis
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <sys/ioctl.h>

#include <net/if.h>
#include <net80211/ieee80211.h>
#include <net80211/ieee80211_crypto.h>
#include <net80211/ieee80211_ioctl.h>

#include "common.h"
#include "driver.h"

struct openbsd_driver_data {
	char ifname[IFNAMSIZ + 1];
	void *ctx;

	int sock;			/* open socket for 802.11 ioctls */
};


static int
wpa_driver_openbsd_get_ssid(void *priv, u8 *ssid)
{
	struct openbsd_driver_data *drv = priv;
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
}

static int
wpa_driver_openbsd_get_bssid(void *priv, u8 *bssid)
{
	struct openbsd_driver_data *drv = priv;
	struct ieee80211_bssid id;

	os_strlcpy(id.i_name, drv->ifname, sizeof(id.i_name));
	if (ioctl(drv->sock, SIOCG80211BSSID, &id) < 0)
		return -1;

	os_memcpy(bssid, id.i_bssid, IEEE80211_ADDR_LEN);
	return 0;
}


static int
wpa_driver_openbsd_get_capa(void *priv, struct wpa_driver_capa *capa)
{
	os_memset(capa, 0, sizeof(*capa));
	capa->flags = WPA_DRIVER_FLAGS_4WAY_HANDSHAKE;
	return 0;
}


static int
wpa_driver_openbsd_set_key(const char *ifname, void *priv, enum wpa_alg alg,
	    const unsigned char *addr, int key_idx, int set_tx, const u8 *seq,
	    size_t seq_len, const u8 *key, size_t key_len)
{
	struct openbsd_driver_data *drv = priv;
	struct ieee80211_keyavail keyavail;

	if (alg != WPA_ALG_PMK || key_len > IEEE80211_PMK_LEN)
		return -1;

	memset(&keyavail, 0, sizeof(keyavail));
	os_strlcpy(keyavail.i_name, drv->ifname, sizeof(keyavail.i_name));
	if (wpa_driver_openbsd_get_bssid(priv, keyavail.i_macaddr) < 0)
		return -1;
	memcpy(keyavail.i_key, key, key_len);

	if (ioctl(drv->sock, SIOCS80211KEYAVAIL, &keyavail) < 0)
		return -1;

	return 0;
}

static void *
wpa_driver_openbsd_init(void *ctx, const char *ifname)
{
	struct openbsd_driver_data *drv;

	drv = os_zalloc(sizeof(*drv));
	if (drv == NULL)
		return NULL;

	drv->sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (drv->sock < 0)
		goto fail;

	drv->ctx = ctx;
	os_strlcpy(drv->ifname, ifname, sizeof(drv->ifname));

	return drv;

fail:
	os_free(drv);
	return NULL;
}


static void
wpa_driver_openbsd_deinit(void *priv)
{
	struct openbsd_driver_data *drv = priv;

	close(drv->sock);
	os_free(drv);
}


const struct wpa_driver_ops wpa_driver_openbsd_ops = {
	.name = "openbsd",
	.desc = "OpenBSD 802.11 support",
	.get_ssid = wpa_driver_openbsd_get_ssid,
	.get_bssid = wpa_driver_openbsd_get_bssid,
	.get_capa = wpa_driver_openbsd_get_capa,
	.set_key = wpa_driver_openbsd_set_key,
	.init = wpa_driver_openbsd_init,
	.deinit = wpa_driver_openbsd_deinit,
};
