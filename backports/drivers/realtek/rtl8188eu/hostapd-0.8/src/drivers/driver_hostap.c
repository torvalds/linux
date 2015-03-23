/*
 * Driver interaction with Linux Host AP driver
 * Copyright (c) 2003-2005, Jouni Malinen <j@w1.fi>
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

#include "wireless_copy.h"
#include "common.h"
#include "driver.h"
#include "driver_wext.h"
#include "eloop.h"
#include "driver_hostap.h"


#ifdef HOSTAPD

#include <net/if_arp.h>
#include <netpacket/packet.h>

#include "priv_netlink.h"
#include "netlink.h"
#include "linux_ioctl.h"
#include "common/ieee802_11_defs.h"


/* MTU to be set for the wlan#ap device; this is mainly needed for IEEE 802.1X
 * frames that might be longer than normal default MTU and they are not
 * fragmented */
#define HOSTAPD_MTU 2290

static const u8 rfc1042_header[6] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };

struct hostap_driver_data {
	struct hostapd_data *hapd;

	char iface[IFNAMSIZ + 1];
	int sock; /* raw packet socket for driver access */
	int ioctl_sock; /* socket for ioctl() use */
	struct netlink_data *netlink;

	int we_version;

	u8 *generic_ie;
	size_t generic_ie_len;
	u8 *wps_ie;
	size_t wps_ie_len;
};


static int hostapd_ioctl(void *priv, struct prism2_hostapd_param *param,
			 int len);
static int hostap_set_iface_flags(void *priv, int dev_up);

static void handle_data(struct hostap_driver_data *drv, u8 *buf, size_t len,
			u16 stype)
{
	struct ieee80211_hdr *hdr;
	u16 fc, ethertype;
	u8 *pos, *sa;
	size_t left;
	union wpa_event_data event;

	if (len < sizeof(struct ieee80211_hdr))
		return;

	hdr = (struct ieee80211_hdr *) buf;
	fc = le_to_host16(hdr->frame_control);

	if ((fc & (WLAN_FC_FROMDS | WLAN_FC_TODS)) != WLAN_FC_TODS) {
		printf("Not ToDS data frame (fc=0x%04x)\n", fc);
		return;
	}

	sa = hdr->addr2;
	os_memset(&event, 0, sizeof(event));
	event.rx_from_unknown.frame = buf;
	event.rx_from_unknown.len = len;
	wpa_supplicant_event(drv->hapd, EVENT_RX_FROM_UNKNOWN, &event);

	pos = (u8 *) (hdr + 1);
	left = len - sizeof(*hdr);

	if (left < sizeof(rfc1042_header)) {
		printf("Too short data frame\n");
		return;
	}

	if (memcmp(pos, rfc1042_header, sizeof(rfc1042_header)) != 0) {
		printf("Data frame with no RFC1042 header\n");
		return;
	}
	pos += sizeof(rfc1042_header);
	left -= sizeof(rfc1042_header);

	if (left < 2) {
		printf("No ethertype in data frame\n");
		return;
	}

	ethertype = WPA_GET_BE16(pos);
	pos += 2;
	left -= 2;
	switch (ethertype) {
	case ETH_P_PAE:
		drv_event_eapol_rx(drv->hapd, sa, pos, left);
		break;

	default:
		printf("Unknown ethertype 0x%04x in data frame\n", ethertype);
		break;
	}
}


static void handle_tx_callback(struct hostap_driver_data *drv, u8 *buf,
			       size_t len, int ok)
{
	struct ieee80211_hdr *hdr;
	u16 fc;
	union wpa_event_data event;

	hdr = (struct ieee80211_hdr *) buf;
	fc = le_to_host16(hdr->frame_control);

	os_memset(&event, 0, sizeof(event));
	event.tx_status.type = WLAN_FC_GET_TYPE(fc);
	event.tx_status.stype = WLAN_FC_GET_STYPE(fc);
	event.tx_status.dst = hdr->addr1;
	event.tx_status.data = buf;
	event.tx_status.data_len = len;
	event.tx_status.ack = ok;
	wpa_supplicant_event(drv->hapd, EVENT_TX_STATUS, &event);
}


static void handle_frame(struct hostap_driver_data *drv, u8 *buf, size_t len)
{
	struct ieee80211_hdr *hdr;
	u16 fc, extra_len, type, stype;
	unsigned char *extra = NULL;
	size_t data_len = len;
	int ver;
	union wpa_event_data event;

	/* PSPOLL is only 16 bytes, but driver does not (at least yet) pass
	 * these to user space */
	if (len < 24) {
		wpa_printf(MSG_MSGDUMP, "handle_frame: too short (%lu)",
			   (unsigned long) len);
		return;
	}

	hdr = (struct ieee80211_hdr *) buf;
	fc = le_to_host16(hdr->frame_control);
	type = WLAN_FC_GET_TYPE(fc);
	stype = WLAN_FC_GET_STYPE(fc);

	if (type != WLAN_FC_TYPE_MGMT || stype != WLAN_FC_STYPE_BEACON) {
		wpa_hexdump(MSG_MSGDUMP, "Received management frame",
			    buf, len);
	}

	ver = fc & WLAN_FC_PVER;

	/* protocol version 3 is reserved for indicating extra data after the
	 * payload, version 2 for indicating ACKed frame (TX callbacks), and
	 * version 1 for indicating failed frame (no ACK, TX callbacks) */
	if (ver == 3) {
		u8 *pos = buf + len - 2;
		extra_len = WPA_GET_LE16(pos);
		printf("extra data in frame (elen=%d)\n", extra_len);
		if ((size_t) extra_len + 2 > len) {
			printf("  extra data overflow\n");
			return;
		}
		len -= extra_len + 2;
		extra = buf + len;
	} else if (ver == 1 || ver == 2) {
		handle_tx_callback(drv, buf, data_len, ver == 2 ? 1 : 0);
		return;
	} else if (ver != 0) {
		printf("unknown protocol version %d\n", ver);
		return;
	}

	switch (type) {
	case WLAN_FC_TYPE_MGMT:
		os_memset(&event, 0, sizeof(event));
		event.rx_mgmt.frame = buf;
		event.rx_mgmt.frame_len = data_len;
		wpa_supplicant_event(drv->hapd, EVENT_RX_MGMT, &event);
		break;
	case WLAN_FC_TYPE_CTRL:
		wpa_printf(MSG_DEBUG, "CTRL");
		break;
	case WLAN_FC_TYPE_DATA:
		wpa_printf(MSG_DEBUG, "DATA");
		handle_data(drv, buf, data_len, stype);
		break;
	default:
		wpa_printf(MSG_DEBUG, "unknown frame type %d", type);
		break;
	}
}


static void handle_read(int sock, void *eloop_ctx, void *sock_ctx)
{
	struct hostap_driver_data *drv = eloop_ctx;
	int len;
	unsigned char buf[3000];

	len = recv(sock, buf, sizeof(buf), 0);
	if (len < 0) {
		perror("recv");
		return;
	}

	handle_frame(drv, buf, len);
}


static int hostap_init_sockets(struct hostap_driver_data *drv, u8 *own_addr)
{
	struct ifreq ifr;
	struct sockaddr_ll addr;

	drv->sock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (drv->sock < 0) {
		perror("socket[PF_PACKET,SOCK_RAW]");
		return -1;
	}

	if (eloop_register_read_sock(drv->sock, handle_read, drv, NULL)) {
		printf("Could not register read socket\n");
		return -1;
	}

        memset(&ifr, 0, sizeof(ifr));
        snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%sap", drv->iface);
        if (ioctl(drv->sock, SIOCGIFINDEX, &ifr) != 0) {
		perror("ioctl(SIOCGIFINDEX)");
		return -1;
        }

	if (hostap_set_iface_flags(drv, 1)) {
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sll_family = AF_PACKET;
	addr.sll_ifindex = ifr.ifr_ifindex;
	wpa_printf(MSG_DEBUG, "Opening raw packet socket for ifindex %d",
		   addr.sll_ifindex);

	if (bind(drv->sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("bind");
		return -1;
	}

	return linux_get_ifhwaddr(drv->sock, drv->iface, own_addr);
}


static int hostap_send_mlme(void *priv, const u8 *msg, size_t len)
{
	struct hostap_driver_data *drv = priv;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) msg;
	int res;

	/* Request TX callback */
	hdr->frame_control |= host_to_le16(BIT(1));
	res = send(drv->sock, msg, len, 0);
	hdr->frame_control &= ~host_to_le16(BIT(1));

	return res;
}


static int hostap_send_eapol(void *priv, const u8 *addr, const u8 *data,
			     size_t data_len, int encrypt, const u8 *own_addr,
			     u32 flags)
{
	struct hostap_driver_data *drv = priv;
	struct ieee80211_hdr *hdr;
	size_t len;
	u8 *pos;
	int res;

	len = sizeof(*hdr) + sizeof(rfc1042_header) + 2 + data_len;
	hdr = os_zalloc(len);
	if (hdr == NULL) {
		printf("malloc() failed for hostapd_send_data(len=%lu)\n",
		       (unsigned long) len);
		return -1;
	}

	hdr->frame_control =
		IEEE80211_FC(WLAN_FC_TYPE_DATA, WLAN_FC_STYPE_DATA);
	hdr->frame_control |= host_to_le16(WLAN_FC_FROMDS);
	if (encrypt)
		hdr->frame_control |= host_to_le16(WLAN_FC_ISWEP);
	memcpy(hdr->IEEE80211_DA_FROMDS, addr, ETH_ALEN);
	memcpy(hdr->IEEE80211_BSSID_FROMDS, own_addr, ETH_ALEN);
	memcpy(hdr->IEEE80211_SA_FROMDS, own_addr, ETH_ALEN);

	pos = (u8 *) (hdr + 1);
	memcpy(pos, rfc1042_header, sizeof(rfc1042_header));
	pos += sizeof(rfc1042_header);
	*((u16 *) pos) = htons(ETH_P_PAE);
	pos += 2;
	memcpy(pos, data, data_len);

	res = hostap_send_mlme(drv, (u8 *) hdr, len);
	if (res < 0) {
		wpa_printf(MSG_ERROR, "hostap_send_eapol - packet len: %lu - "
			   "failed: %d (%s)",
			   (unsigned long) len, errno, strerror(errno));
	}
	free(hdr);

	return res;
}


static int hostap_sta_set_flags(void *priv, const u8 *addr,
				int total_flags, int flags_or, int flags_and)
{
	struct hostap_driver_data *drv = priv;
	struct prism2_hostapd_param param;

	if (flags_or & WPA_STA_AUTHORIZED)
		flags_or = BIT(5); /* WLAN_STA_AUTHORIZED */
	if (!(flags_and & WPA_STA_AUTHORIZED))
		flags_and = ~BIT(5);
	else
		flags_and = ~0;
	memset(&param, 0, sizeof(param));
	param.cmd = PRISM2_HOSTAPD_SET_FLAGS_STA;
	memcpy(param.sta_addr, addr, ETH_ALEN);
	param.u.set_flags_sta.flags_or = flags_or;
	param.u.set_flags_sta.flags_and = flags_and;
	return hostapd_ioctl(drv, &param, sizeof(param));
}


static int hostap_set_iface_flags(void *priv, int dev_up)
{
	struct hostap_driver_data *drv = priv;
	struct ifreq ifr;
	char ifname[IFNAMSIZ];

	os_snprintf(ifname, IFNAMSIZ, "%sap", drv->iface);
	if (linux_set_iface_flags(drv->ioctl_sock, ifname, dev_up) < 0)
		return -1;

	if (dev_up) {
		memset(&ifr, 0, sizeof(ifr));
		os_strlcpy(ifr.ifr_name, ifname, IFNAMSIZ);
		ifr.ifr_mtu = HOSTAPD_MTU;
		if (ioctl(drv->ioctl_sock, SIOCSIFMTU, &ifr) != 0) {
			perror("ioctl[SIOCSIFMTU]");
			printf("Setting MTU failed - trying to survive with "
			       "current value\n");
		}
	}

	return 0;
}


static int hostapd_ioctl(void *priv, struct prism2_hostapd_param *param,
			 int len)
{
	struct hostap_driver_data *drv = priv;
	struct iwreq iwr;

	memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
	iwr.u.data.pointer = (caddr_t) param;
	iwr.u.data.length = len;

	if (ioctl(drv->ioctl_sock, PRISM2_IOCTL_HOSTAPD, &iwr) < 0) {
		perror("ioctl[PRISM2_IOCTL_HOSTAPD]");
		return -1;
	}

	return 0;
}


static int wpa_driver_hostap_set_key(const char *ifname, void *priv,
				     enum wpa_alg alg, const u8 *addr,
				     int key_idx, int set_tx,
				     const u8 *seq, size_t seq_len,
				     const u8 *key, size_t key_len)
{
	struct hostap_driver_data *drv = priv;
	struct prism2_hostapd_param *param;
	u8 *buf;
	size_t blen;
	int ret = 0;

	blen = sizeof(*param) + key_len;
	buf = os_zalloc(blen);
	if (buf == NULL)
		return -1;

	param = (struct prism2_hostapd_param *) buf;
	param->cmd = PRISM2_SET_ENCRYPTION;
	if (addr == NULL)
		memset(param->sta_addr, 0xff, ETH_ALEN);
	else
		memcpy(param->sta_addr, addr, ETH_ALEN);
	switch (alg) {
	case WPA_ALG_NONE:
		os_strlcpy((char *) param->u.crypt.alg, "NONE",
			   HOSTAP_CRYPT_ALG_NAME_LEN);
		break;
	case WPA_ALG_WEP:
		os_strlcpy((char *) param->u.crypt.alg, "WEP",
			   HOSTAP_CRYPT_ALG_NAME_LEN);
		break;
	case WPA_ALG_TKIP:
		os_strlcpy((char *) param->u.crypt.alg, "TKIP",
			   HOSTAP_CRYPT_ALG_NAME_LEN);
		break;
	case WPA_ALG_CCMP:
		os_strlcpy((char *) param->u.crypt.alg, "CCMP",
			   HOSTAP_CRYPT_ALG_NAME_LEN);
		break;
	default:
		os_free(buf);
		return -1;
	}
	param->u.crypt.flags = set_tx ? HOSTAP_CRYPT_FLAG_SET_TX_KEY : 0;
	param->u.crypt.idx = key_idx;
	param->u.crypt.key_len = key_len;
	memcpy((u8 *) (param + 1), key, key_len);

	if (hostapd_ioctl(drv, param, blen)) {
		printf("Failed to set encryption.\n");
		ret = -1;
	}
	free(buf);

	return ret;
}


static int hostap_get_seqnum(const char *ifname, void *priv, const u8 *addr,
			     int idx, u8 *seq)
{
	struct hostap_driver_data *drv = priv;
	struct prism2_hostapd_param *param;
	u8 *buf;
	size_t blen;
	int ret = 0;

	blen = sizeof(*param) + 32;
	buf = os_zalloc(blen);
	if (buf == NULL)
		return -1;

	param = (struct prism2_hostapd_param *) buf;
	param->cmd = PRISM2_GET_ENCRYPTION;
	if (addr == NULL)
		memset(param->sta_addr, 0xff, ETH_ALEN);
	else
		memcpy(param->sta_addr, addr, ETH_ALEN);
	param->u.crypt.idx = idx;

	if (hostapd_ioctl(drv, param, blen)) {
		printf("Failed to get encryption.\n");
		ret = -1;
	} else {
		memcpy(seq, param->u.crypt.seq, 8);
	}
	free(buf);

	return ret;
}


static int hostap_ioctl_prism2param(void *priv, int param, int value)
{
	struct hostap_driver_data *drv = priv;
	struct iwreq iwr;
	int *i;

	memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
	i = (int *) iwr.u.name;
	*i++ = param;
	*i++ = value;

	if (ioctl(drv->ioctl_sock, PRISM2_IOCTL_PRISM2_PARAM, &iwr) < 0) {
		perror("ioctl[PRISM2_IOCTL_PRISM2_PARAM]");
		return -1;
	}

	return 0;
}


static int hostap_set_ieee8021x(void *priv, struct wpa_bss_params *params)
{
	struct hostap_driver_data *drv = priv;
	int enabled = params->enabled;

	/* enable kernel driver support for IEEE 802.1X */
	if (hostap_ioctl_prism2param(drv, PRISM2_PARAM_IEEE_802_1X, enabled)) {
		printf("Could not setup IEEE 802.1X support in kernel driver."
		       "\n");
		return -1;
	}

	if (!enabled)
		return 0;

	/* use host driver implementation of encryption to allow
	 * individual keys and passing plaintext EAPOL frames */
	if (hostap_ioctl_prism2param(drv, PRISM2_PARAM_HOST_DECRYPT, 1) ||
	    hostap_ioctl_prism2param(drv, PRISM2_PARAM_HOST_ENCRYPT, 1)) {
		printf("Could not setup host-based encryption in kernel "
		       "driver.\n");
		return -1;
	}

	return 0;
}


static int hostap_set_privacy(void *priv, int enabled)
{
	struct hostap_drvier_data *drv = priv;

	return hostap_ioctl_prism2param(drv, PRISM2_PARAM_PRIVACY_INVOKED,
					enabled);
}


static int hostap_set_ssid(void *priv, const u8 *buf, int len)
{
	struct hostap_driver_data *drv = priv;
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


static int hostap_flush(void *priv)
{
	struct hostap_driver_data *drv = priv;
	struct prism2_hostapd_param param;

	memset(&param, 0, sizeof(param));
	param.cmd = PRISM2_HOSTAPD_FLUSH;
	return hostapd_ioctl(drv, &param, sizeof(param));
}


static int hostap_read_sta_data(void *priv,
				struct hostap_sta_driver_data *data,
				const u8 *addr)
{
	struct hostap_driver_data *drv = priv;
	char buf[1024], line[128], *pos;
	FILE *f;
	unsigned long val;

	memset(data, 0, sizeof(*data));
	snprintf(buf, sizeof(buf), "/proc/net/hostap/%s/" MACSTR,
		 drv->iface, MAC2STR(addr));

	f = fopen(buf, "r");
	if (!f)
		return -1;
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
}


static int hostap_sta_add(void *priv, struct hostapd_sta_add_params *params)
{
	struct hostap_driver_data *drv = priv;
	struct prism2_hostapd_param param;
	int tx_supp_rates = 0;
	size_t i;

#define WLAN_RATE_1M BIT(0)
#define WLAN_RATE_2M BIT(1)
#define WLAN_RATE_5M5 BIT(2)
#define WLAN_RATE_11M BIT(3)

	for (i = 0; i < params->supp_rates_len; i++) {
		if ((params->supp_rates[i] & 0x7f) == 2)
			tx_supp_rates |= WLAN_RATE_1M;
		if ((params->supp_rates[i] & 0x7f) == 4)
			tx_supp_rates |= WLAN_RATE_2M;
		if ((params->supp_rates[i] & 0x7f) == 11)
			tx_supp_rates |= WLAN_RATE_5M5;
		if ((params->supp_rates[i] & 0x7f) == 22)
			tx_supp_rates |= WLAN_RATE_11M;
	}

	memset(&param, 0, sizeof(param));
	param.cmd = PRISM2_HOSTAPD_ADD_STA;
	memcpy(param.sta_addr, params->addr, ETH_ALEN);
	param.u.add_sta.aid = params->aid;
	param.u.add_sta.capability = params->capability;
	param.u.add_sta.tx_supp_rates = tx_supp_rates;
	return hostapd_ioctl(drv, &param, sizeof(param));
}


static int hostap_sta_remove(void *priv, const u8 *addr)
{
	struct hostap_driver_data *drv = priv;
	struct prism2_hostapd_param param;

	hostap_sta_set_flags(drv, addr, 0, 0, ~WPA_STA_AUTHORIZED);

	memset(&param, 0, sizeof(param));
	param.cmd = PRISM2_HOSTAPD_REMOVE_STA;
	memcpy(param.sta_addr, addr, ETH_ALEN);
	if (hostapd_ioctl(drv, &param, sizeof(param))) {
		printf("Could not remove station from kernel driver.\n");
		return -1;
	}
	return 0;
}


static int hostap_get_inact_sec(void *priv, const u8 *addr)
{
	struct hostap_driver_data *drv = priv;
	struct prism2_hostapd_param param;

	memset(&param, 0, sizeof(param));
	param.cmd = PRISM2_HOSTAPD_GET_INFO_STA;
	memcpy(param.sta_addr, addr, ETH_ALEN);
	if (hostapd_ioctl(drv, &param, sizeof(param))) {
		return -1;
	}

	return param.u.get_info_sta.inactive_sec;
}


static int hostap_sta_clear_stats(void *priv, const u8 *addr)
{
	struct hostap_driver_data *drv = priv;
	struct prism2_hostapd_param param;

	memset(&param, 0, sizeof(param));
	param.cmd = PRISM2_HOSTAPD_STA_CLEAR_STATS;
	memcpy(param.sta_addr, addr, ETH_ALEN);
	if (hostapd_ioctl(drv, &param, sizeof(param))) {
		return -1;
	}

	return 0;
}


static int hostapd_ioctl_set_generic_elem(struct hostap_driver_data *drv)
{
	struct prism2_hostapd_param *param;
	int res;
	size_t blen, elem_len;

	elem_len = drv->generic_ie_len + drv->wps_ie_len;
	blen = PRISM2_HOSTAPD_GENERIC_ELEMENT_HDR_LEN + elem_len;
	if (blen < sizeof(*param))
		blen = sizeof(*param);

	param = os_zalloc(blen);
	if (param == NULL)
		return -1;

	param->cmd = PRISM2_HOSTAPD_SET_GENERIC_ELEMENT;
	param->u.generic_elem.len = elem_len;
	if (drv->generic_ie) {
		os_memcpy(param->u.generic_elem.data, drv->generic_ie,
			  drv->generic_ie_len);
	}
	if (drv->wps_ie) {
		os_memcpy(&param->u.generic_elem.data[drv->generic_ie_len],
			  drv->wps_ie, drv->wps_ie_len);
	}
	wpa_hexdump(MSG_DEBUG, "hostap: Set generic IE",
		    param->u.generic_elem.data, elem_len);
	res = hostapd_ioctl(drv, param, blen);

	os_free(param);

	return res;
}


static int hostap_set_generic_elem(void *priv,
				   const u8 *elem, size_t elem_len)
{
	struct hostap_driver_data *drv = priv;

	os_free(drv->generic_ie);
	drv->generic_ie = NULL;
	drv->generic_ie_len = 0;
	if (elem) {
		drv->generic_ie = os_malloc(elem_len);
		if (drv->generic_ie == NULL)
			return -1;
		os_memcpy(drv->generic_ie, elem, elem_len);
		drv->generic_ie_len = elem_len;
	}

	return hostapd_ioctl_set_generic_elem(drv);
}


static int hostap_set_ap_wps_ie(void *priv, const struct wpabuf *beacon,
				const struct wpabuf *proberesp,
				const struct wpabuf *assocresp)
{
	struct hostap_driver_data *drv = priv;

	/*
	 * Host AP driver supports only one set of extra IEs, so we need to
	 * use the Probe Response IEs also for Beacon frames since they include
	 * more information.
	 */

	os_free(drv->wps_ie);
	drv->wps_ie = NULL;
	drv->wps_ie_len = 0;
	if (proberesp) {
		drv->wps_ie = os_malloc(wpabuf_len(proberesp));
		if (drv->wps_ie == NULL)
			return -1;
		os_memcpy(drv->wps_ie, wpabuf_head(proberesp),
			  wpabuf_len(proberesp));
		drv->wps_ie_len = wpabuf_len(proberesp);
	}

	return hostapd_ioctl_set_generic_elem(drv);
}


static void
hostapd_wireless_event_wireless_custom(struct hostap_driver_data *drv,
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
	}
}


static void hostapd_wireless_event_wireless(struct hostap_driver_data *drv,
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
		wpa_printf(MSG_DEBUG, "Wireless event: cmd=0x%x len=%d",
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
		case IWEVCUSTOM:
			if (custom + iwe->u.data.length > end)
				return;
			buf = malloc(iwe->u.data.length + 1);
			if (buf == NULL)
				return;
			memcpy(buf, custom, iwe->u.data.length);
			buf[iwe->u.data.length] = '\0';
			hostapd_wireless_event_wireless_custom(drv, buf);
			free(buf);
			break;
		}

		pos += iwe->len;
	}
}


static void hostapd_wireless_event_rtm_newlink(void *ctx,
					       struct ifinfomsg *ifi,
					       u8 *buf, size_t len)
{
	struct hostap_driver_data *drv = ctx;
	int attrlen, rta_len;
	struct rtattr *attr;

	/* TODO: use ifi->ifi_index to filter out wireless events from other
	 * interfaces */

	attrlen = len;
	attr = (struct rtattr *) buf;

	rta_len = RTA_ALIGN(sizeof(struct rtattr));
	while (RTA_OK(attr, attrlen)) {
		if (attr->rta_type == IFLA_WIRELESS) {
			hostapd_wireless_event_wireless(
				drv, ((char *) attr) + rta_len,
				attr->rta_len - rta_len);
		}
		attr = RTA_NEXT(attr, attrlen);
	}
}


static int hostap_get_we_version(struct hostap_driver_data *drv)
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


static int hostap_wireless_event_init(struct hostap_driver_data *drv)
{
	struct netlink_config *cfg;

	hostap_get_we_version(drv);

	cfg = os_zalloc(sizeof(*cfg));
	if (cfg == NULL)
		return -1;
	cfg->ctx = drv;
	cfg->newlink_cb = hostapd_wireless_event_rtm_newlink;
	drv->netlink = netlink_init(cfg);
	if (drv->netlink == NULL) {
		os_free(cfg);
		return -1;
	}

	return 0;
}


static void * hostap_init(struct hostapd_data *hapd,
			  struct wpa_init_params *params)
{
	struct hostap_driver_data *drv;

	drv = os_zalloc(sizeof(struct hostap_driver_data));
	if (drv == NULL) {
		printf("Could not allocate memory for hostapd driver data\n");
		return NULL;
	}

	drv->hapd = hapd;
	drv->ioctl_sock = drv->sock = -1;
	memcpy(drv->iface, params->ifname, sizeof(drv->iface));

	drv->ioctl_sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (drv->ioctl_sock < 0) {
		perror("socket[PF_INET,SOCK_DGRAM]");
		free(drv);
		return NULL;
	}

	if (hostap_ioctl_prism2param(drv, PRISM2_PARAM_HOSTAPD, 1)) {
		printf("Could not enable hostapd mode for interface %s\n",
		       drv->iface);
		close(drv->ioctl_sock);
		free(drv);
		return NULL;
	}

	if (hostap_init_sockets(drv, params->own_addr) ||
	    hostap_wireless_event_init(drv)) {
		close(drv->ioctl_sock);
		free(drv);
		return NULL;
	}

	return drv;
}


static void hostap_driver_deinit(void *priv)
{
	struct hostap_driver_data *drv = priv;

	netlink_deinit(drv->netlink);
	(void) hostap_set_iface_flags(drv, 0);
	(void) hostap_ioctl_prism2param(drv, PRISM2_PARAM_HOSTAPD, 0);
	(void) hostap_ioctl_prism2param(drv, PRISM2_PARAM_HOSTAPD_STA, 0);

	if (drv->ioctl_sock >= 0)
		close(drv->ioctl_sock);

	if (drv->sock >= 0)
		close(drv->sock);

	os_free(drv->generic_ie);
	os_free(drv->wps_ie);

	free(drv);
}


static int hostap_sta_deauth(void *priv, const u8 *own_addr, const u8 *addr,
			     int reason)
{
	struct hostap_driver_data *drv = priv;
	struct ieee80211_mgmt mgmt;

	if (is_broadcast_ether_addr(addr)) {
		/*
		 * New Prism2.5/3 STA firmware versions seem to have issues
		 * with this broadcast deauth frame. This gets the firmware in
		 * odd state where nothing works correctly, so let's skip
		 * sending this for the hostap driver.
		 */
		return 0;
	}

	memset(&mgmt, 0, sizeof(mgmt));
	mgmt.frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT,
					  WLAN_FC_STYPE_DEAUTH);
	memcpy(mgmt.da, addr, ETH_ALEN);
	memcpy(mgmt.sa, own_addr, ETH_ALEN);
	memcpy(mgmt.bssid, own_addr, ETH_ALEN);
	mgmt.u.deauth.reason_code = host_to_le16(reason);
	return hostap_send_mlme(drv, (u8 *) &mgmt, IEEE80211_HDRLEN +
				sizeof(mgmt.u.deauth));
}


static int hostap_sta_disassoc(void *priv, const u8 *own_addr, const u8 *addr,
			       int reason)
{
	struct hostap_driver_data *drv = priv;
	struct ieee80211_mgmt mgmt;

	memset(&mgmt, 0, sizeof(mgmt));
	mgmt.frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT,
					  WLAN_FC_STYPE_DISASSOC);
	memcpy(mgmt.da, addr, ETH_ALEN);
	memcpy(mgmt.sa, own_addr, ETH_ALEN);
	memcpy(mgmt.bssid, own_addr, ETH_ALEN);
	mgmt.u.disassoc.reason_code = host_to_le16(reason);
	return  hostap_send_mlme(drv, (u8 *) &mgmt, IEEE80211_HDRLEN +
				 sizeof(mgmt.u.disassoc));
}


static struct hostapd_hw_modes * hostap_get_hw_feature_data(void *priv,
							    u16 *num_modes,
							    u16 *flags)
{
	struct hostapd_hw_modes *mode;
	int i, clen, rlen;
	const short chan2freq[14] = {
		2412, 2417, 2422, 2427, 2432, 2437, 2442,
		2447, 2452, 2457, 2462, 2467, 2472, 2484
	};

	mode = os_zalloc(sizeof(struct hostapd_hw_modes));
	if (mode == NULL)
		return NULL;

	*num_modes = 1;
	*flags = 0;

	mode->mode = HOSTAPD_MODE_IEEE80211B;
	mode->num_channels = 14;
	mode->num_rates = 4;

	clen = mode->num_channels * sizeof(struct hostapd_channel_data);
	rlen = mode->num_rates * sizeof(int);

	mode->channels = os_zalloc(clen);
	mode->rates = os_zalloc(rlen);
	if (mode->channels == NULL || mode->rates == NULL) {
		os_free(mode->channels);
		os_free(mode->rates);
		os_free(mode);
		return NULL;
	}

	for (i = 0; i < 14; i++) {
		mode->channels[i].chan = i + 1;
		mode->channels[i].freq = chan2freq[i];
		/* TODO: Get allowed channel list from the driver */
		if (i >= 11)
			mode->channels[i].flag = HOSTAPD_CHAN_DISABLED;
	}

	mode->rates[0] = 10;
	mode->rates[1] = 20;
	mode->rates[2] = 55;
	mode->rates[3] = 110;

	return mode;
}

#else /* HOSTAPD */

struct wpa_driver_hostap_data {
	void *wext; /* private data for driver_wext */
	void *ctx;
	char ifname[IFNAMSIZ + 1];
	int sock;
	int current_mode; /* infra/adhoc */
};


static int wpa_driver_hostap_set_auth_alg(void *priv, int auth_alg);


static int hostapd_ioctl(struct wpa_driver_hostap_data *drv,
			 struct prism2_hostapd_param *param,
			 int len, int show_err)
{
	struct iwreq iwr;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.data.pointer = (caddr_t) param;
	iwr.u.data.length = len;

	if (ioctl(drv->sock, PRISM2_IOCTL_HOSTAPD, &iwr) < 0) {
		int ret = errno;
		if (show_err)
			perror("ioctl[PRISM2_IOCTL_HOSTAPD]");
		return ret;
	}

	return 0;
}


static int wpa_driver_hostap_set_wpa_ie(struct wpa_driver_hostap_data *drv,
					const u8 *wpa_ie, size_t wpa_ie_len)
{
	struct prism2_hostapd_param *param;
	int res;
	size_t blen = PRISM2_HOSTAPD_GENERIC_ELEMENT_HDR_LEN + wpa_ie_len;
	if (blen < sizeof(*param))
		blen = sizeof(*param);

	param = os_zalloc(blen);
	if (param == NULL)
		return -1;

	param->cmd = PRISM2_HOSTAPD_SET_GENERIC_ELEMENT;
	param->u.generic_elem.len = wpa_ie_len;
	os_memcpy(param->u.generic_elem.data, wpa_ie, wpa_ie_len);
	res = hostapd_ioctl(drv, param, blen, 1);

	os_free(param);

	return res;
}


static int prism2param(struct wpa_driver_hostap_data *drv, int param,
		       int value)
{
	struct iwreq iwr;
	int *i, ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	i = (int *) iwr.u.name;
	*i++ = param;
	*i++ = value;

	if (ioctl(drv->sock, PRISM2_IOCTL_PRISM2_PARAM, &iwr) < 0) {
		perror("ioctl[PRISM2_IOCTL_PRISM2_PARAM]");
		ret = -1;
	}
	return ret;
}


static int wpa_driver_hostap_set_wpa(void *priv, int enabled)
{
	struct wpa_driver_hostap_data *drv = priv;
	int ret = 0;

	wpa_printf(MSG_DEBUG, "%s: enabled=%d", __FUNCTION__, enabled);

	if (!enabled && wpa_driver_hostap_set_wpa_ie(drv, NULL, 0) < 0)
		ret = -1;
	if (prism2param(drv, PRISM2_PARAM_HOST_ROAMING, enabled ? 2 : 0) < 0)
		ret = -1;
	if (prism2param(drv, PRISM2_PARAM_WPA, enabled) < 0)
		ret = -1;

	return ret;
}


static void show_set_key_error(struct prism2_hostapd_param *param)
{
	switch (param->u.crypt.err) {
	case HOSTAP_CRYPT_ERR_UNKNOWN_ALG:
		wpa_printf(MSG_INFO, "Unknown algorithm '%s'.",
			   param->u.crypt.alg);
		wpa_printf(MSG_INFO, "You may need to load kernel module to "
			   "register that algorithm.");
		wpa_printf(MSG_INFO, "E.g., 'modprobe hostap_crypt_wep' for "
			   "WEP.");
		break;
	case HOSTAP_CRYPT_ERR_UNKNOWN_ADDR:
		wpa_printf(MSG_INFO, "Unknown address " MACSTR ".",
			   MAC2STR(param->sta_addr));
		break;
	case HOSTAP_CRYPT_ERR_CRYPT_INIT_FAILED:
		wpa_printf(MSG_INFO, "Crypt algorithm initialization failed.");
		break;
	case HOSTAP_CRYPT_ERR_KEY_SET_FAILED:
		wpa_printf(MSG_INFO, "Key setting failed.");
		break;
	case HOSTAP_CRYPT_ERR_TX_KEY_SET_FAILED:
		wpa_printf(MSG_INFO, "TX key index setting failed.");
		break;
	case HOSTAP_CRYPT_ERR_CARD_CONF_FAILED:
		wpa_printf(MSG_INFO, "Card configuration failed.");
		break;
	}
}


static int wpa_driver_hostap_set_key(const char *ifname, void *priv,
				     enum wpa_alg alg, const u8 *addr,
				     int key_idx, int set_tx,
				     const u8 *seq, size_t seq_len,
				     const u8 *key, size_t key_len)
{
	struct wpa_driver_hostap_data *drv = priv;
	struct prism2_hostapd_param *param;
	u8 *buf;
	size_t blen;
	int ret = 0;
	char *alg_name;

	switch (alg) {
	case WPA_ALG_NONE:
		alg_name = "none";
		break;
	case WPA_ALG_WEP:
		alg_name = "WEP";
		break;
	case WPA_ALG_TKIP:
		alg_name = "TKIP";
		break;
	case WPA_ALG_CCMP:
		alg_name = "CCMP";
		break;
	default:
		return -1;
	}

	wpa_printf(MSG_DEBUG, "%s: alg=%s key_idx=%d set_tx=%d seq_len=%lu "
		   "key_len=%lu", __FUNCTION__, alg_name, key_idx, set_tx,
		   (unsigned long) seq_len, (unsigned long) key_len);

	if (seq_len > 8)
		return -2;

	blen = sizeof(*param) + key_len;
	buf = os_zalloc(blen);
	if (buf == NULL)
		return -1;

	param = (struct prism2_hostapd_param *) buf;
	param->cmd = PRISM2_SET_ENCRYPTION;
	/* TODO: In theory, STA in client mode can use five keys; four default
	 * keys for receiving (with keyidx 0..3) and one individual key for
	 * both transmitting and receiving (keyidx 0) _unicast_ packets. Now,
	 * keyidx 0 is reserved for this unicast use and default keys can only
	 * use keyidx 1..3 (i.e., default key with keyidx 0 is not supported).
	 * This should be fine for more or less all cases, but for completeness
	 * sake, the driver could be enhanced to support the missing key. */
#if 0
	if (addr == NULL)
		os_memset(param->sta_addr, 0xff, ETH_ALEN);
	else
		os_memcpy(param->sta_addr, addr, ETH_ALEN);
#else
	os_memset(param->sta_addr, 0xff, ETH_ALEN);
#endif
	os_strlcpy((char *) param->u.crypt.alg, alg_name,
		   HOSTAP_CRYPT_ALG_NAME_LEN);
	param->u.crypt.flags = set_tx ? HOSTAP_CRYPT_FLAG_SET_TX_KEY : 0;
	param->u.crypt.idx = key_idx;
	if (seq)
		os_memcpy(param->u.crypt.seq, seq, seq_len);
	param->u.crypt.key_len = key_len;
	os_memcpy((u8 *) (param + 1), key, key_len);

	if (hostapd_ioctl(drv, param, blen, 1)) {
		wpa_printf(MSG_WARNING, "Failed to set encryption.");
		show_set_key_error(param);
		ret = -1;
	}
	os_free(buf);

	return ret;
}


static int wpa_driver_hostap_set_countermeasures(void *priv, int enabled)
{
	struct wpa_driver_hostap_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s: enabled=%d", __FUNCTION__, enabled);
	return prism2param(drv, PRISM2_PARAM_TKIP_COUNTERMEASURES, enabled);
}


static int wpa_driver_hostap_reset(struct wpa_driver_hostap_data *drv,
				   int type)
{
	struct iwreq iwr;
	int *i, ret = 0;

	wpa_printf(MSG_DEBUG, "%s: type=%d", __FUNCTION__, type);

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	i = (int *) iwr.u.name;
	*i++ = type;

	if (ioctl(drv->sock, PRISM2_IOCTL_RESET, &iwr) < 0) {
	        perror("ioctl[PRISM2_IOCTL_RESET]");
	        ret = -1;
	}
	return ret;
}


static int wpa_driver_hostap_mlme(struct wpa_driver_hostap_data *drv,
				  const u8 *addr, int cmd, int reason_code)
{
	struct prism2_hostapd_param param;
	int ret;

	/* There does not seem to be a better way of deauthenticating or
	 * disassociating with Prism2/2.5/3 than sending the management frame
	 * and then resetting the Port0 to make sure both the AP and the STA
	 * end up in disconnected state. */
	os_memset(&param, 0, sizeof(param));
	param.cmd = PRISM2_HOSTAPD_MLME;
	os_memcpy(param.sta_addr, addr, ETH_ALEN);
	param.u.mlme.cmd = cmd;
	param.u.mlme.reason_code = reason_code;
	ret = hostapd_ioctl(drv, &param, sizeof(param), 1);
	if (ret == 0) {
		os_sleep(0, 100000);
		ret = wpa_driver_hostap_reset(drv, 2);
	}
	return ret;
}


static int wpa_driver_hostap_deauthenticate(void *priv, const u8 *addr,
					    int reason_code)
{
	struct wpa_driver_hostap_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
	return wpa_driver_hostap_mlme(drv, addr, MLME_STA_DEAUTH,
				      reason_code);
}


static int wpa_driver_hostap_disassociate(void *priv, const u8 *addr,
					  int reason_code)
{
	struct wpa_driver_hostap_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
	return wpa_driver_hostap_mlme(drv, addr, MLME_STA_DISASSOC,
				      reason_code);
}


static int
wpa_driver_hostap_associate(void *priv,
			    struct wpa_driver_associate_params *params)
{
	struct wpa_driver_hostap_data *drv = priv;
	int ret = 0;
	int allow_unencrypted_eapol;

	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);

	if (prism2param(drv, PRISM2_PARAM_DROP_UNENCRYPTED,
			params->drop_unencrypted) < 0)
		ret = -1;
	if (wpa_driver_hostap_set_auth_alg(drv, params->auth_alg) < 0)
		ret = -1;
	if (params->mode != drv->current_mode) {
		/* At the moment, Host AP driver requires host_roaming=2 for
		 * infrastructure mode and host_roaming=0 for adhoc. */
		if (prism2param(drv, PRISM2_PARAM_HOST_ROAMING,
				params->mode == IEEE80211_MODE_IBSS ? 0 : 2) <
		    0) {
			wpa_printf(MSG_DEBUG, "%s: failed to set host_roaming",
				   __func__);
		}
		drv->current_mode = params->mode;
	}

	if (prism2param(drv, PRISM2_PARAM_PRIVACY_INVOKED,
			params->key_mgmt_suite != KEY_MGMT_NONE) < 0)
		ret = -1;
	if (wpa_driver_hostap_set_wpa_ie(drv, params->wpa_ie,
					 params->wpa_ie_len) < 0)
		ret = -1;
	if (wpa_driver_wext_set_mode(drv->wext, params->mode) < 0)
		ret = -1;
	if (params->freq &&
	    wpa_driver_wext_set_freq(drv->wext, params->freq) < 0)
		ret = -1;
	if (wpa_driver_wext_set_ssid(drv->wext, params->ssid, params->ssid_len)
	    < 0)
		ret = -1;
	if (wpa_driver_wext_set_bssid(drv->wext, params->bssid) < 0)
		ret = -1;

	/* Allow unencrypted EAPOL messages even if pairwise keys are set when
	 * not using WPA. IEEE 802.1X specifies that these frames are not
	 * encrypted, but WPA encrypts them when pairwise keys are in use. */
	if (params->key_mgmt_suite == KEY_MGMT_802_1X ||
	    params->key_mgmt_suite == KEY_MGMT_PSK)
		allow_unencrypted_eapol = 0;
	else
		allow_unencrypted_eapol = 1;
	
	if (prism2param(drv, PRISM2_PARAM_IEEE_802_1X,
			allow_unencrypted_eapol) < 0) {
		wpa_printf(MSG_DEBUG, "hostap: Failed to configure "
			   "ieee_802_1x param");
		/* Ignore this error.. driver_hostap.c can also be used with
		 * other drivers that do not support this prism2_param. */
	}

	return ret;
}


static int wpa_driver_hostap_scan(void *priv,
				  struct wpa_driver_scan_params *params)
{
	struct wpa_driver_hostap_data *drv = priv;
	struct prism2_hostapd_param param;
	int ret;
	const u8 *ssid = params->ssids[0].ssid;
	size_t ssid_len = params->ssids[0].ssid_len;

	if (ssid == NULL) {
		/* Use standard Linux Wireless Extensions ioctl if possible
		 * because some drivers using hostap code in wpa_supplicant
		 * might not support Host AP specific scan request (with SSID
		 * info). */
		return wpa_driver_wext_scan(drv->wext, params);
	}

	if (ssid_len > 32)
		ssid_len = 32;

	os_memset(&param, 0, sizeof(param));
	param.cmd = PRISM2_HOSTAPD_SCAN_REQ;
	param.u.scan_req.ssid_len = ssid_len;
	os_memcpy(param.u.scan_req.ssid, ssid, ssid_len);
	ret = hostapd_ioctl(drv, &param, sizeof(param), 1);

	/* Not all drivers generate "scan completed" wireless event, so try to
	 * read results after a timeout. */
	eloop_cancel_timeout(wpa_driver_wext_scan_timeout, drv->wext,
			     drv->ctx);
	eloop_register_timeout(3, 0, wpa_driver_wext_scan_timeout, drv->wext,
			       drv->ctx);

	return ret;
}


static int wpa_driver_hostap_set_auth_alg(void *priv, int auth_alg)
{
	struct wpa_driver_hostap_data *drv = priv;
	int algs = 0;

	if (auth_alg & WPA_AUTH_ALG_OPEN)
		algs |= 1;
	if (auth_alg & WPA_AUTH_ALG_SHARED)
		algs |= 2;
	if (auth_alg & WPA_AUTH_ALG_LEAP)
		algs |= 4;
	if (algs == 0)
		algs = 1; /* at least one algorithm should be set */

	return prism2param(drv, PRISM2_PARAM_AP_AUTH_ALGS, algs);
}


static int wpa_driver_hostap_get_bssid(void *priv, u8 *bssid)
{
	struct wpa_driver_hostap_data *drv = priv;
	return wpa_driver_wext_get_bssid(drv->wext, bssid);
}


static int wpa_driver_hostap_get_ssid(void *priv, u8 *ssid)
{
	struct wpa_driver_hostap_data *drv = priv;
	return wpa_driver_wext_get_ssid(drv->wext, ssid);
}


static struct wpa_scan_results * wpa_driver_hostap_get_scan_results(void *priv)
{
	struct wpa_driver_hostap_data *drv = priv;
	return wpa_driver_wext_get_scan_results(drv->wext);
}


static int wpa_driver_hostap_set_operstate(void *priv, int state)
{
	struct wpa_driver_hostap_data *drv = priv;
	return wpa_driver_wext_set_operstate(drv->wext, state);
}


static void * wpa_driver_hostap_init(void *ctx, const char *ifname)
{
	struct wpa_driver_hostap_data *drv;

	drv = os_zalloc(sizeof(*drv));
	if (drv == NULL)
		return NULL;
	drv->wext = wpa_driver_wext_init(ctx, ifname);
	if (drv->wext == NULL) {
		os_free(drv);
		return NULL;
	}

	drv->ctx = ctx;
	os_strlcpy(drv->ifname, ifname, sizeof(drv->ifname));
	drv->sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (drv->sock < 0) {
		perror("socket");
		wpa_driver_wext_deinit(drv->wext);
		os_free(drv);
		return NULL;
	}

	if (os_strncmp(ifname, "wlan", 4) == 0) {
		/*
		 * Host AP driver may use both wlan# and wifi# interface in
		 * wireless events.
		 */
		char ifname2[IFNAMSIZ + 1];
		os_strlcpy(ifname2, ifname, sizeof(ifname2));
		os_memcpy(ifname2, "wifi", 4);
		wpa_driver_wext_alternative_ifindex(drv->wext, ifname2);
	}

	wpa_driver_hostap_set_wpa(drv, 1);

	return drv;
}


static void wpa_driver_hostap_deinit(void *priv)
{
	struct wpa_driver_hostap_data *drv = priv;
	wpa_driver_hostap_set_wpa(drv, 0);
	wpa_driver_wext_deinit(drv->wext);
	close(drv->sock);
	os_free(drv);
}

#endif /* HOSTAPD */


const struct wpa_driver_ops wpa_driver_hostap_ops = {
	.name = "hostap",
	.desc = "Host AP driver (Intersil Prism2/2.5/3)",
	.set_key = wpa_driver_hostap_set_key,
#ifdef HOSTAPD
	.hapd_init = hostap_init,
	.hapd_deinit = hostap_driver_deinit,
	.set_ieee8021x = hostap_set_ieee8021x,
	.set_privacy = hostap_set_privacy,
	.get_seqnum = hostap_get_seqnum,
	.flush = hostap_flush,
	.set_generic_elem = hostap_set_generic_elem,
	.read_sta_data = hostap_read_sta_data,
	.hapd_send_eapol = hostap_send_eapol,
	.sta_set_flags = hostap_sta_set_flags,
	.sta_deauth = hostap_sta_deauth,
	.sta_disassoc = hostap_sta_disassoc,
	.sta_remove = hostap_sta_remove,
	.hapd_set_ssid = hostap_set_ssid,
	.send_mlme = hostap_send_mlme,
	.sta_add = hostap_sta_add,
	.get_inact_sec = hostap_get_inact_sec,
	.sta_clear_stats = hostap_sta_clear_stats,
	.get_hw_feature_data = hostap_get_hw_feature_data,
	.set_ap_wps_ie = hostap_set_ap_wps_ie,
#else /* HOSTAPD */
	.get_bssid = wpa_driver_hostap_get_bssid,
	.get_ssid = wpa_driver_hostap_get_ssid,
	.set_countermeasures = wpa_driver_hostap_set_countermeasures,
	.scan2 = wpa_driver_hostap_scan,
	.get_scan_results2 = wpa_driver_hostap_get_scan_results,
	.deauthenticate = wpa_driver_hostap_deauthenticate,
	.disassociate = wpa_driver_hostap_disassociate,
	.associate = wpa_driver_hostap_associate,
	.init = wpa_driver_hostap_init,
	.deinit = wpa_driver_hostap_deinit,
	.set_operstate = wpa_driver_hostap_set_operstate,
#endif /* HOSTAPD */
};
