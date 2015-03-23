/*
 * Testing driver interface for a simulated network driver
 * Copyright (c) 2004-2010, Jouni Malinen <j@w1.fi>
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

/* Make sure we get winsock2.h for Windows build to get sockaddr_storage */
#include "build_config.h"
#ifdef CONFIG_NATIVE_WINDOWS
#include <winsock2.h>
#endif /* CONFIG_NATIVE_WINDOWS */

#include "utils/includes.h"

#ifndef CONFIG_NATIVE_WINDOWS
#include <sys/un.h>
#include <dirent.h>
#include <sys/stat.h>
#define DRIVER_TEST_UNIX
#endif /* CONFIG_NATIVE_WINDOWS */

#include "utils/common.h"
#include "utils/eloop.h"
#include "utils/list.h"
#include "utils/trace.h"
#include "common/ieee802_11_defs.h"
#include "crypto/sha1.h"
#include "l2_packet/l2_packet.h"
#include "p2p/p2p.h"
#include "wps/wps.h"
#include "driver.h"


struct test_client_socket {
	struct test_client_socket *next;
	u8 addr[ETH_ALEN];
	struct sockaddr_un un;
	socklen_t unlen;
	struct test_driver_bss *bss;
};

struct test_driver_bss {
	struct wpa_driver_test_data *drv;
	struct dl_list list;
	void *bss_ctx;
	char ifname[IFNAMSIZ];
	u8 bssid[ETH_ALEN];
	u8 *ie;
	size_t ielen;
	u8 *wps_beacon_ie;
	size_t wps_beacon_ie_len;
	u8 *wps_probe_resp_ie;
	size_t wps_probe_resp_ie_len;
	u8 ssid[32];
	size_t ssid_len;
	int privacy;
};

struct wpa_driver_test_global {
	int bss_add_used;
	u8 req_addr[ETH_ALEN];
};

struct wpa_driver_test_data {
	struct wpa_driver_test_global *global;
	void *ctx;
	WPA_TRACE_REF(ctx);
	u8 own_addr[ETH_ALEN];
	int test_socket;
#ifdef DRIVER_TEST_UNIX
	struct sockaddr_un hostapd_addr;
#endif /* DRIVER_TEST_UNIX */
	int hostapd_addr_set;
	struct sockaddr_in hostapd_addr_udp;
	int hostapd_addr_udp_set;
	char *own_socket_path;
	char *test_dir;
#define MAX_SCAN_RESULTS 30
	struct wpa_scan_res *scanres[MAX_SCAN_RESULTS];
	size_t num_scanres;
	int use_associnfo;
	u8 assoc_wpa_ie[80];
	size_t assoc_wpa_ie_len;
	int use_mlme;
	int associated;
	u8 *probe_req_ie;
	size_t probe_req_ie_len;
	u8 probe_req_ssid[32];
	size_t probe_req_ssid_len;
	int ibss;
	int ap;

	struct test_client_socket *cli;
	struct dl_list bss;
	int udp_port;

	int alloc_iface_idx;

	int probe_req_report;
	unsigned int remain_on_channel_freq;
	unsigned int remain_on_channel_duration;

	int current_freq;

	struct p2p_data *p2p;
	unsigned int off_channel_freq;
	struct wpabuf *pending_action_tx;
	u8 pending_action_src[ETH_ALEN];
	u8 pending_action_dst[ETH_ALEN];
	u8 pending_action_bssid[ETH_ALEN];
	unsigned int pending_action_freq;
	unsigned int pending_listen_freq;
	unsigned int pending_listen_duration;
	int pending_p2p_scan;
	struct sockaddr *probe_from;
	socklen_t probe_from_len;
};


static void wpa_driver_test_deinit(void *priv);
static int wpa_driver_test_attach(struct wpa_driver_test_data *drv,
				  const char *dir, int ap);
static void wpa_driver_test_close_test_socket(
	struct wpa_driver_test_data *drv);
static void test_remain_on_channel_timeout(void *eloop_ctx, void *timeout_ctx);
static int wpa_driver_test_init_p2p(struct wpa_driver_test_data *drv);


static void test_driver_free_bss(struct test_driver_bss *bss)
{
	os_free(bss->ie);
	os_free(bss->wps_beacon_ie);
	os_free(bss->wps_probe_resp_ie);
	os_free(bss);
}


static void test_driver_free_bsses(struct wpa_driver_test_data *drv)
{
	struct test_driver_bss *bss, *tmp;

	dl_list_for_each_safe(bss, tmp, &drv->bss, struct test_driver_bss,
			      list) {
		dl_list_del(&bss->list);
		test_driver_free_bss(bss);
	}
}


static struct test_client_socket *
test_driver_get_cli(struct wpa_driver_test_data *drv, struct sockaddr_un *from,
		    socklen_t fromlen)
{
	struct test_client_socket *cli = drv->cli;

	while (cli) {
		if (cli->unlen == fromlen &&
		    strncmp(cli->un.sun_path, from->sun_path,
			    fromlen - sizeof(cli->un.sun_family)) == 0)
			return cli;
		cli = cli->next;
	}

	return NULL;
}


static int test_driver_send_eapol(void *priv, const u8 *addr, const u8 *data,
				  size_t data_len, int encrypt,
				  const u8 *own_addr, u32 flags)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	struct test_client_socket *cli;
	struct msghdr msg;
	struct iovec io[3];
	struct l2_ethhdr eth;

	if (drv->test_socket < 0)
		return -1;

	cli = drv->cli;
	while (cli) {
		if (memcmp(cli->addr, addr, ETH_ALEN) == 0)
			break;
		cli = cli->next;
	}

	if (!cli) {
		wpa_printf(MSG_DEBUG, "%s: no destination client entry",
			   __func__);
		return -1;
	}

	memcpy(eth.h_dest, addr, ETH_ALEN);
	memcpy(eth.h_source, own_addr, ETH_ALEN);
	eth.h_proto = host_to_be16(ETH_P_EAPOL);

	io[0].iov_base = "EAPOL ";
	io[0].iov_len = 6;
	io[1].iov_base = &eth;
	io[1].iov_len = sizeof(eth);
	io[2].iov_base = (u8 *) data;
	io[2].iov_len = data_len;

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = io;
	msg.msg_iovlen = 3;
	msg.msg_name = &cli->un;
	msg.msg_namelen = cli->unlen;
	return sendmsg(drv->test_socket, &msg, 0);
}


static int test_driver_send_ether(void *priv, const u8 *dst, const u8 *src,
				  u16 proto, const u8 *data, size_t data_len)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	struct msghdr msg;
	struct iovec io[3];
	struct l2_ethhdr eth;
	char desttxt[30];
	struct sockaddr_un addr;
	struct dirent *dent;
	DIR *dir;
	int ret = 0, broadcast = 0, count = 0;

	if (drv->test_socket < 0 || drv->test_dir == NULL) {
		wpa_printf(MSG_DEBUG, "%s: invalid parameters (sock=%d "
			   "test_dir=%p)",
			   __func__, drv->test_socket, drv->test_dir);
		return -1;
	}

	broadcast = memcmp(dst, "\xff\xff\xff\xff\xff\xff", ETH_ALEN) == 0;
	snprintf(desttxt, sizeof(desttxt), MACSTR, MAC2STR(dst));

	memcpy(eth.h_dest, dst, ETH_ALEN);
	memcpy(eth.h_source, src, ETH_ALEN);
	eth.h_proto = host_to_be16(proto);

	io[0].iov_base = "ETHER ";
	io[0].iov_len = 6;
	io[1].iov_base = &eth;
	io[1].iov_len = sizeof(eth);
	io[2].iov_base = (u8 *) data;
	io[2].iov_len = data_len;

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = io;
	msg.msg_iovlen = 3;

	dir = opendir(drv->test_dir);
	if (dir == NULL) {
		perror("test_driver: opendir");
		return -1;
	}
	while ((dent = readdir(dir))) {
#ifdef _DIRENT_HAVE_D_TYPE
		/* Skip the file if it is not a socket. Also accept
		 * DT_UNKNOWN (0) in case the C library or underlying file
		 * system does not support d_type. */
		if (dent->d_type != DT_SOCK && dent->d_type != DT_UNKNOWN)
			continue;
#endif /* _DIRENT_HAVE_D_TYPE */
		if (strcmp(dent->d_name, ".") == 0 ||
		    strcmp(dent->d_name, "..") == 0)
			continue;

		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/%s",
			 drv->test_dir, dent->d_name);

		if (strcmp(addr.sun_path, drv->own_socket_path) == 0)
			continue;
		if (!broadcast && strstr(dent->d_name, desttxt) == NULL)
			continue;

		wpa_printf(MSG_DEBUG, "%s: Send ether frame to %s",
			   __func__, dent->d_name);

		msg.msg_name = &addr;
		msg.msg_namelen = sizeof(addr);
		ret = sendmsg(drv->test_socket, &msg, 0);
		if (ret < 0)
			perror("driver_test: sendmsg");
		count++;
	}
	closedir(dir);

	if (!broadcast && count == 0) {
		wpa_printf(MSG_DEBUG, "%s: Destination " MACSTR " not found",
			   __func__, MAC2STR(dst));
		return -1;
	}

	return ret;
}


static int wpa_driver_test_send_mlme(void *priv, const u8 *data,
				     size_t data_len)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	struct msghdr msg;
	struct iovec io[2];
	const u8 *dest;
	struct sockaddr_un addr;
	struct dirent *dent;
	DIR *dir;
	int broadcast;
	int ret = 0;
	struct ieee80211_hdr *hdr;
	u16 fc;
	char cmd[50];
	int freq;
#ifdef HOSTAPD
	char desttxt[30];
#endif /* HOSTAPD */
	union wpa_event_data event;

	wpa_hexdump(MSG_MSGDUMP, "test_send_mlme", data, data_len);
	if (drv->test_socket < 0 || data_len < 10) {
		wpa_printf(MSG_DEBUG, "%s: invalid parameters (sock=%d len=%lu"
			   " test_dir=%p)",
			   __func__, drv->test_socket,
			   (unsigned long) data_len,
			   drv->test_dir);
		return -1;
	}

	dest = data + 4;
	broadcast = os_memcmp(dest, "\xff\xff\xff\xff\xff\xff", ETH_ALEN) == 0;

#ifdef HOSTAPD
	snprintf(desttxt, sizeof(desttxt), MACSTR, MAC2STR(dest));
#endif /* HOSTAPD */

	if (drv->remain_on_channel_freq)
		freq = drv->remain_on_channel_freq;
	else
		freq = drv->current_freq;
	wpa_printf(MSG_DEBUG, "test_driver(%s): MLME TX on freq %d MHz",
		   dbss->ifname, freq);
	os_snprintf(cmd, sizeof(cmd), "MLME freq=%d ", freq);
	io[0].iov_base = cmd;
	io[0].iov_len = os_strlen(cmd);
	io[1].iov_base = (void *) data;
	io[1].iov_len = data_len;

	os_memset(&msg, 0, sizeof(msg));
	msg.msg_iov = io;
	msg.msg_iovlen = 2;

#ifdef HOSTAPD
	if (drv->test_dir == NULL) {
		wpa_printf(MSG_DEBUG, "%s: test_dir == NULL", __func__);
		return -1;
	}

	dir = opendir(drv->test_dir);
	if (dir == NULL) {
		perror("test_driver: opendir");
		return -1;
	}
	while ((dent = readdir(dir))) {
#ifdef _DIRENT_HAVE_D_TYPE
		/* Skip the file if it is not a socket. Also accept
		 * DT_UNKNOWN (0) in case the C library or underlying file
		 * system does not support d_type. */
		if (dent->d_type != DT_SOCK && dent->d_type != DT_UNKNOWN)
			continue;
#endif /* _DIRENT_HAVE_D_TYPE */
		if (os_strcmp(dent->d_name, ".") == 0 ||
		    os_strcmp(dent->d_name, "..") == 0)
			continue;

		os_memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		os_snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/%s",
			    drv->test_dir, dent->d_name);

		if (os_strcmp(addr.sun_path, drv->own_socket_path) == 0)
			continue;
		if (!broadcast && os_strstr(dent->d_name, desttxt) == NULL)
			continue;

		wpa_printf(MSG_DEBUG, "%s: Send management frame to %s",
			   __func__, dent->d_name);

		msg.msg_name = &addr;
		msg.msg_namelen = sizeof(addr);
		ret = sendmsg(drv->test_socket, &msg, 0);
		if (ret < 0)
			perror("driver_test: sendmsg(test_socket)");
	}
	closedir(dir);
#else /* HOSTAPD */

	if (os_memcmp(dest, dbss->bssid, ETH_ALEN) == 0 ||
	    drv->test_dir == NULL) {
		if (drv->hostapd_addr_udp_set) {
			msg.msg_name = &drv->hostapd_addr_udp;
			msg.msg_namelen = sizeof(drv->hostapd_addr_udp);
		} else {
#ifdef DRIVER_TEST_UNIX
			msg.msg_name = &drv->hostapd_addr;
			msg.msg_namelen = sizeof(drv->hostapd_addr);
#endif /* DRIVER_TEST_UNIX */
		}
	} else if (broadcast) {
		dir = opendir(drv->test_dir);
		if (dir == NULL)
			return -1;
		while ((dent = readdir(dir))) {
#ifdef _DIRENT_HAVE_D_TYPE
			/* Skip the file if it is not a socket.
			 * Also accept DT_UNKNOWN (0) in case
			 * the C library or underlying file
			 * system does not support d_type. */
			if (dent->d_type != DT_SOCK &&
			    dent->d_type != DT_UNKNOWN)
				continue;
#endif /* _DIRENT_HAVE_D_TYPE */
			if (os_strcmp(dent->d_name, ".") == 0 ||
			    os_strcmp(dent->d_name, "..") == 0)
				continue;
			wpa_printf(MSG_DEBUG, "%s: Send broadcast MLME to %s",
				   __func__, dent->d_name);
			os_memset(&addr, 0, sizeof(addr));
			addr.sun_family = AF_UNIX;
			os_snprintf(addr.sun_path, sizeof(addr.sun_path),
				    "%s/%s", drv->test_dir, dent->d_name);

			msg.msg_name = &addr;
			msg.msg_namelen = sizeof(addr);

			ret = sendmsg(drv->test_socket, &msg, 0);
			if (ret < 0)
				perror("driver_test: sendmsg(test_socket)");
		}
		closedir(dir);
		return ret;
	} else {
		struct stat st;
		os_memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		os_snprintf(addr.sun_path, sizeof(addr.sun_path),
			    "%s/AP-" MACSTR, drv->test_dir, MAC2STR(dest));
		if (stat(addr.sun_path, &st) < 0) {
			os_snprintf(addr.sun_path, sizeof(addr.sun_path),
				    "%s/STA-" MACSTR,
				    drv->test_dir, MAC2STR(dest));
		}
		msg.msg_name = &addr;
		msg.msg_namelen = sizeof(addr);
	}

	if (sendmsg(drv->test_socket, &msg, 0) < 0) {
		perror("sendmsg(test_socket)");
		return -1;
	}
#endif /* HOSTAPD */

	hdr = (struct ieee80211_hdr *) data;
	fc = le_to_host16(hdr->frame_control);

	os_memset(&event, 0, sizeof(event));
	event.tx_status.type = WLAN_FC_GET_TYPE(fc);
	event.tx_status.stype = WLAN_FC_GET_STYPE(fc);
	event.tx_status.dst = hdr->addr1;
	event.tx_status.data = data;
	event.tx_status.data_len = data_len;
	event.tx_status.ack = ret >= 0;
	wpa_supplicant_event(drv->ctx, EVENT_TX_STATUS, &event);

#ifdef CONFIG_P2P
	if (drv->p2p &&
	    WLAN_FC_GET_TYPE(fc) == WLAN_FC_TYPE_MGMT &&
	    WLAN_FC_GET_STYPE(fc) == WLAN_FC_STYPE_ACTION) {
		if (drv->pending_action_tx == NULL) {
			wpa_printf(MSG_DEBUG, "P2P: Ignore Action TX status - "
				   "no pending operation");
			return ret;
		}

		if (os_memcmp(hdr->addr1, drv->pending_action_dst, ETH_ALEN) !=
		    0) {
			wpa_printf(MSG_DEBUG, "P2P: Ignore Action TX status - "
				   "unknown destination address");
			return ret;
		}

		wpabuf_free(drv->pending_action_tx);
		drv->pending_action_tx = NULL;

		p2p_send_action_cb(drv->p2p, drv->pending_action_freq,
				   drv->pending_action_dst,
				   drv->pending_action_src,
				   drv->pending_action_bssid,
				   ret >= 0);
	}
#endif /* CONFIG_P2P */

	return ret;
}


static void test_driver_scan(struct wpa_driver_test_data *drv,
			     struct sockaddr_un *from, socklen_t fromlen,
			     char *data)
{
	char buf[512], *pos, *end;
	int ret;
	struct test_driver_bss *bss;
	u8 sa[ETH_ALEN];
	u8 ie[512];
	size_t ielen;
	union wpa_event_data event;

	/* data: optional [ ' ' | STA-addr | ' ' | IEs(hex) ] */

	wpa_printf(MSG_DEBUG, "test_driver: SCAN");

	if (*data) {
		if (*data != ' ' ||
		    hwaddr_aton(data + 1, sa)) {
			wpa_printf(MSG_DEBUG, "test_driver: Unexpected SCAN "
				   "command format");
			return;
		}

		data += 18;
		while (*data == ' ')
			data++;
		ielen = os_strlen(data) / 2;
		if (ielen > sizeof(ie))
			ielen = sizeof(ie);
		if (hexstr2bin(data, ie, ielen) < 0)
			ielen = 0;

		wpa_printf(MSG_DEBUG, "test_driver: Scan from " MACSTR,
			   MAC2STR(sa));
		wpa_hexdump(MSG_MSGDUMP, "test_driver: scan IEs", ie, ielen);

		os_memset(&event, 0, sizeof(event));
		event.rx_probe_req.sa = sa;
		event.rx_probe_req.ie = ie;
		event.rx_probe_req.ie_len = ielen;
		wpa_supplicant_event(drv->ctx, EVENT_RX_PROBE_REQ, &event);
#ifdef CONFIG_P2P
		if (drv->p2p)
			p2p_probe_req_rx(drv->p2p, sa, ie, ielen);
#endif /* CONFIG_P2P */
	}

	dl_list_for_each(bss, &drv->bss, struct test_driver_bss, list) {
		pos = buf;
		end = buf + sizeof(buf);

		/* reply: SCANRESP BSSID SSID IEs */
		ret = snprintf(pos, end - pos, "SCANRESP " MACSTR " ",
			       MAC2STR(bss->bssid));
		if (ret < 0 || ret >= end - pos)
			return;
		pos += ret;
		pos += wpa_snprintf_hex(pos, end - pos,
					bss->ssid, bss->ssid_len);
		ret = snprintf(pos, end - pos, " ");
		if (ret < 0 || ret >= end - pos)
			return;
		pos += ret;
		pos += wpa_snprintf_hex(pos, end - pos, bss->ie, bss->ielen);
		pos += wpa_snprintf_hex(pos, end - pos, bss->wps_probe_resp_ie,
					bss->wps_probe_resp_ie_len);

		if (bss->privacy) {
			ret = snprintf(pos, end - pos, " PRIVACY");
			if (ret < 0 || ret >= end - pos)
				return;
			pos += ret;
		}

		sendto(drv->test_socket, buf, pos - buf, 0,
		       (struct sockaddr *) from, fromlen);
	}
}


static void test_driver_assoc(struct wpa_driver_test_data *drv,
			      struct sockaddr_un *from, socklen_t fromlen,
			      char *data)
{
	struct test_client_socket *cli;
	u8 ie[256], ssid[32];
	size_t ielen, ssid_len = 0;
	char *pos, *pos2, cmd[50];
	struct test_driver_bss *bss, *tmp;

	/* data: STA-addr SSID(hex) IEs(hex) */

	cli = os_zalloc(sizeof(*cli));
	if (cli == NULL)
		return;

	if (hwaddr_aton(data, cli->addr)) {
		printf("test_socket: Invalid MAC address '%s' in ASSOC\n",
		       data);
		os_free(cli);
		return;
	}
	pos = data + 17;
	while (*pos == ' ')
		pos++;
	pos2 = strchr(pos, ' ');
	ielen = 0;
	if (pos2) {
		ssid_len = (pos2 - pos) / 2;
		if (hexstr2bin(pos, ssid, ssid_len) < 0) {
			wpa_printf(MSG_DEBUG, "%s: Invalid SSID", __func__);
			os_free(cli);
			return;
		}
		wpa_hexdump_ascii(MSG_DEBUG, "test_driver_assoc: SSID",
				  ssid, ssid_len);

		pos = pos2 + 1;
		ielen = strlen(pos) / 2;
		if (ielen > sizeof(ie))
			ielen = sizeof(ie);
		if (hexstr2bin(pos, ie, ielen) < 0)
			ielen = 0;
	}

	bss = NULL;
	dl_list_for_each(tmp, &drv->bss, struct test_driver_bss, list) {
		if (tmp->ssid_len == ssid_len &&
		    os_memcmp(tmp->ssid, ssid, ssid_len) == 0) {
			bss = tmp;
			break;
		}
	}
	if (bss == NULL) {
		wpa_printf(MSG_DEBUG, "%s: No matching SSID found from "
			   "configured BSSes", __func__);
		os_free(cli);
		return;
	}

	cli->bss = bss;
	memcpy(&cli->un, from, sizeof(cli->un));
	cli->unlen = fromlen;
	cli->next = drv->cli;
	drv->cli = cli;
	wpa_hexdump_ascii(MSG_DEBUG, "test_socket: ASSOC sun_path",
			  (const u8 *) cli->un.sun_path,
			  cli->unlen - sizeof(cli->un.sun_family));

	snprintf(cmd, sizeof(cmd), "ASSOCRESP " MACSTR " 0",
		 MAC2STR(bss->bssid));
	sendto(drv->test_socket, cmd, strlen(cmd), 0,
	       (struct sockaddr *) from, fromlen);

	drv_event_assoc(bss->bss_ctx, cli->addr, ie, ielen, 0);
}


static void test_driver_disassoc(struct wpa_driver_test_data *drv,
				 struct sockaddr_un *from, socklen_t fromlen)
{
	struct test_client_socket *cli;

	cli = test_driver_get_cli(drv, from, fromlen);
	if (!cli)
		return;

	drv_event_disassoc(drv->ctx, cli->addr);
}


static void test_driver_eapol(struct wpa_driver_test_data *drv,
			      struct sockaddr_un *from, socklen_t fromlen,
			      u8 *data, size_t datalen)
{
#ifdef HOSTAPD
	struct test_client_socket *cli;
#endif /* HOSTAPD */
	const u8 *src = NULL;

	if (datalen > 14) {
		/* Skip Ethernet header */
		src = data + ETH_ALEN;
		wpa_printf(MSG_DEBUG, "test_driver: dst=" MACSTR " src="
			   MACSTR " proto=%04x",
			   MAC2STR(data), MAC2STR(src),
			   WPA_GET_BE16(data + 2 * ETH_ALEN));
		data += 14;
		datalen -= 14;
	}

#ifdef HOSTAPD
	cli = test_driver_get_cli(drv, from, fromlen);
	if (cli) {
		drv_event_eapol_rx(cli->bss->bss_ctx, cli->addr, data,
				   datalen);
	} else {
		wpa_printf(MSG_DEBUG, "test_socket: EAPOL from unknown "
			   "client");
	}
#else /* HOSTAPD */
	if (src)
		drv_event_eapol_rx(drv->ctx, src, data, datalen);
#endif /* HOSTAPD */
}


static void test_driver_ether(struct wpa_driver_test_data *drv,
			      struct sockaddr_un *from, socklen_t fromlen,
			      u8 *data, size_t datalen)
{
	struct l2_ethhdr *eth;

	if (datalen < sizeof(*eth))
		return;

	eth = (struct l2_ethhdr *) data;
	wpa_printf(MSG_DEBUG, "test_driver: RX ETHER dst=" MACSTR " src="
		   MACSTR " proto=%04x",
		   MAC2STR(eth->h_dest), MAC2STR(eth->h_source),
		   be_to_host16(eth->h_proto));

#ifdef CONFIG_IEEE80211R
	if (be_to_host16(eth->h_proto) == ETH_P_RRB) {
		union wpa_event_data ev;
		os_memset(&ev, 0, sizeof(ev));
		ev.ft_rrb_rx.src = eth->h_source;
		ev.ft_rrb_rx.data = data + sizeof(*eth);
		ev.ft_rrb_rx.data_len = datalen - sizeof(*eth);
	}
#endif /* CONFIG_IEEE80211R */
}


static void test_driver_mlme(struct wpa_driver_test_data *drv,
			     struct sockaddr_un *from, socklen_t fromlen,
			     u8 *data, size_t datalen)
{
	struct ieee80211_hdr *hdr;
	u16 fc;
	union wpa_event_data event;
	int freq = 0, own_freq;
	struct test_driver_bss *bss;

	bss = dl_list_first(&drv->bss, struct test_driver_bss, list);

	if (datalen > 6 && os_memcmp(data, "freq=", 5) == 0) {
		size_t pos;
		for (pos = 5; pos < datalen; pos++) {
			if (data[pos] == ' ')
				break;
		}
		if (pos < datalen) {
			freq = atoi((const char *) &data[5]);
			wpa_printf(MSG_DEBUG, "test_driver(%s): MLME RX on "
				   "freq %d MHz", bss->ifname, freq);
			pos++;
			data += pos;
			datalen -= pos;
		}
	}

	if (drv->remain_on_channel_freq)
		own_freq = drv->remain_on_channel_freq;
	else
		own_freq = drv->current_freq;

	if (freq && own_freq && freq != own_freq) {
		wpa_printf(MSG_DEBUG, "test_driver(%s): Ignore MLME RX on "
			   "another frequency %d MHz (own %d MHz)",
			   bss->ifname, freq, own_freq);
		return;
	}

	hdr = (struct ieee80211_hdr *) data;

	if (test_driver_get_cli(drv, from, fromlen) == NULL && datalen >= 16) {
		struct test_client_socket *cli;
		cli = os_zalloc(sizeof(*cli));
		if (cli == NULL)
			return;
		wpa_printf(MSG_DEBUG, "Adding client entry for " MACSTR,
			   MAC2STR(hdr->addr2));
		memcpy(cli->addr, hdr->addr2, ETH_ALEN);
		memcpy(&cli->un, from, sizeof(cli->un));
		cli->unlen = fromlen;
		cli->next = drv->cli;
		drv->cli = cli;
	}

	wpa_hexdump(MSG_MSGDUMP, "test_driver_mlme: received frame",
		    data, datalen);
	fc = le_to_host16(hdr->frame_control);
	if (WLAN_FC_GET_TYPE(fc) != WLAN_FC_TYPE_MGMT) {
		wpa_printf(MSG_ERROR, "%s: received non-mgmt frame",
			   __func__);
		return;
	}

	os_memset(&event, 0, sizeof(event));
	event.rx_mgmt.frame = data;
	event.rx_mgmt.frame_len = datalen;
	wpa_supplicant_event(drv->ctx, EVENT_RX_MGMT, &event);
}


static void test_driver_receive_unix(int sock, void *eloop_ctx, void *sock_ctx)
{
	struct wpa_driver_test_data *drv = eloop_ctx;
	char buf[2000];
	int res;
	struct sockaddr_un from;
	socklen_t fromlen = sizeof(from);

	res = recvfrom(sock, buf, sizeof(buf) - 1, 0,
		       (struct sockaddr *) &from, &fromlen);
	if (res < 0) {
		perror("recvfrom(test_socket)");
		return;
	}
	buf[res] = '\0';

	wpa_printf(MSG_DEBUG, "test_driver: received %u bytes", res);

	if (strncmp(buf, "SCAN", 4) == 0) {
		test_driver_scan(drv, &from, fromlen, buf + 4);
	} else if (strncmp(buf, "ASSOC ", 6) == 0) {
		test_driver_assoc(drv, &from, fromlen, buf + 6);
	} else if (strcmp(buf, "DISASSOC") == 0) {
		test_driver_disassoc(drv, &from, fromlen);
	} else if (strncmp(buf, "EAPOL ", 6) == 0) {
		test_driver_eapol(drv, &from, fromlen, (u8 *) buf + 6,
				  res - 6);
	} else if (strncmp(buf, "ETHER ", 6) == 0) {
		test_driver_ether(drv, &from, fromlen, (u8 *) buf + 6,
				  res - 6);
	} else if (strncmp(buf, "MLME ", 5) == 0) {
		test_driver_mlme(drv, &from, fromlen, (u8 *) buf + 5, res - 5);
	} else {
		wpa_hexdump_ascii(MSG_DEBUG, "Unknown test_socket command",
				  (u8 *) buf, res);
	}
}


static int test_driver_set_generic_elem(void *priv,
					const u8 *elem, size_t elem_len)
{
	struct test_driver_bss *bss = priv;

	os_free(bss->ie);

	if (elem == NULL) {
		bss->ie = NULL;
		bss->ielen = 0;
		return 0;
	}

	bss->ie = os_malloc(elem_len);
	if (bss->ie == NULL) {
		bss->ielen = 0;
		return -1;
	}

	memcpy(bss->ie, elem, elem_len);
	bss->ielen = elem_len;
	return 0;
}


static int test_driver_set_ap_wps_ie(void *priv, const struct wpabuf *beacon,
				     const struct wpabuf *proberesp,
				     const struct wpabuf *assocresp)
{
	struct test_driver_bss *bss = priv;

	if (beacon == NULL)
		wpa_printf(MSG_DEBUG, "test_driver: Clear Beacon WPS IE");
	else
		wpa_hexdump_buf(MSG_DEBUG, "test_driver: Beacon WPS IE",
				beacon);

	os_free(bss->wps_beacon_ie);

	if (beacon == NULL) {
		bss->wps_beacon_ie = NULL;
		bss->wps_beacon_ie_len = 0;
	} else {
		bss->wps_beacon_ie = os_malloc(wpabuf_len(beacon));
		if (bss->wps_beacon_ie == NULL) {
			bss->wps_beacon_ie_len = 0;
			return -1;
		}

		os_memcpy(bss->wps_beacon_ie, wpabuf_head(beacon),
			  wpabuf_len(beacon));
		bss->wps_beacon_ie_len = wpabuf_len(beacon);
	}

	if (proberesp == NULL)
		wpa_printf(MSG_DEBUG, "test_driver: Clear Probe Response WPS "
			   "IE");
	else
		wpa_hexdump_buf(MSG_DEBUG, "test_driver: Probe Response WPS "
				"IE", proberesp);

	os_free(bss->wps_probe_resp_ie);

	if (proberesp == NULL) {
		bss->wps_probe_resp_ie = NULL;
		bss->wps_probe_resp_ie_len = 0;
	} else {
		bss->wps_probe_resp_ie = os_malloc(wpabuf_len(proberesp));
		if (bss->wps_probe_resp_ie == NULL) {
			bss->wps_probe_resp_ie_len = 0;
			return -1;
		}

		os_memcpy(bss->wps_probe_resp_ie, wpabuf_head(proberesp),
			  wpabuf_len(proberesp));
		bss->wps_probe_resp_ie_len = wpabuf_len(proberesp);
	}

	return 0;
}


static int test_driver_sta_deauth(void *priv, const u8 *own_addr,
				  const u8 *addr, int reason)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	struct test_client_socket *cli;

	if (drv->test_socket < 0)
		return -1;

	cli = drv->cli;
	while (cli) {
		if (memcmp(cli->addr, addr, ETH_ALEN) == 0)
			break;
		cli = cli->next;
	}

	if (!cli)
		return -1;

	return sendto(drv->test_socket, "DEAUTH", 6, 0,
		      (struct sockaddr *) &cli->un, cli->unlen);
}


static int test_driver_sta_disassoc(void *priv, const u8 *own_addr,
				    const u8 *addr, int reason)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	struct test_client_socket *cli;

	if (drv->test_socket < 0)
		return -1;

	cli = drv->cli;
	while (cli) {
		if (memcmp(cli->addr, addr, ETH_ALEN) == 0)
			break;
		cli = cli->next;
	}

	if (!cli)
		return -1;

	return sendto(drv->test_socket, "DISASSOC", 8, 0,
		      (struct sockaddr *) &cli->un, cli->unlen);
}


static int test_driver_bss_add(void *priv, const char *ifname, const u8 *bssid,
			       void *bss_ctx, void **drv_priv)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	struct test_driver_bss *bss;

	wpa_printf(MSG_DEBUG, "%s(ifname=%s bssid=" MACSTR ")",
		   __func__, ifname, MAC2STR(bssid));

	bss = os_zalloc(sizeof(*bss));
	if (bss == NULL)
		return -1;

	bss->bss_ctx = bss_ctx;
	bss->drv = drv;
	os_strlcpy(bss->ifname, ifname, IFNAMSIZ);
	os_memcpy(bss->bssid, bssid, ETH_ALEN);

	dl_list_add(&drv->bss, &bss->list);
	if (drv->global) {
		drv->global->bss_add_used = 1;
		os_memcpy(drv->global->req_addr, bssid, ETH_ALEN);
	}

	if (drv_priv)
		*drv_priv = bss;

	return 0;
}


static int test_driver_bss_remove(void *priv, const char *ifname)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	struct test_driver_bss *bss;
	struct test_client_socket *cli, *prev_c;

	wpa_printf(MSG_DEBUG, "%s(ifname=%s)", __func__, ifname);

	dl_list_for_each(bss, &drv->bss, struct test_driver_bss, list) {
		if (strcmp(bss->ifname, ifname) != 0)
			continue;

		for (prev_c = NULL, cli = drv->cli; cli;
		     prev_c = cli, cli = cli->next) {
			if (cli->bss != bss)
				continue;
			if (prev_c)
				prev_c->next = cli->next;
			else
				drv->cli = cli->next;
			os_free(cli);
			break;
		}

		dl_list_del(&bss->list);
		test_driver_free_bss(bss);
		return 0;
	}

	return -1;
}


static int test_driver_if_add(void *priv, enum wpa_driver_if_type type,
			      const char *ifname, const u8 *addr,
			      void *bss_ctx, void **drv_priv,
			      char *force_ifname, u8 *if_addr,
			      const char *bridge)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;

	wpa_printf(MSG_DEBUG, "%s(type=%d ifname=%s bss_ctx=%p)",
		   __func__, type, ifname, bss_ctx);
	if (addr)
		os_memcpy(if_addr, addr, ETH_ALEN);
	else {
		drv->alloc_iface_idx++;
		if_addr[0] = 0x02; /* locally administered */
		sha1_prf(drv->own_addr, ETH_ALEN,
			 "hostapd test addr generation",
			 (const u8 *) &drv->alloc_iface_idx,
			 sizeof(drv->alloc_iface_idx),
			 if_addr + 1, ETH_ALEN - 1);
	}
	if (type == WPA_IF_AP_BSS || type == WPA_IF_P2P_GO ||
	    type == WPA_IF_P2P_CLIENT || type == WPA_IF_P2P_GROUP)
		return test_driver_bss_add(priv, ifname, if_addr, bss_ctx,
					   drv_priv);
	return 0;
}


static int test_driver_if_remove(void *priv, enum wpa_driver_if_type type,
				 const char *ifname)
{
	wpa_printf(MSG_DEBUG, "%s(type=%d ifname=%s)", __func__, type, ifname);
	if (type == WPA_IF_AP_BSS || type == WPA_IF_P2P_GO ||
	    type == WPA_IF_P2P_CLIENT || type == WPA_IF_P2P_GROUP)
		return test_driver_bss_remove(priv, ifname);
	return 0;
}


static int test_driver_valid_bss_mask(void *priv, const u8 *addr,
				      const u8 *mask)
{
	return 0;
}


static int test_driver_set_ssid(void *priv, const u8 *buf, int len)
{
	struct test_driver_bss *bss = priv;

	wpa_printf(MSG_DEBUG, "%s(ifname=%s)", __func__, bss->ifname);
	if (len < 0)
		return -1;
	wpa_hexdump_ascii(MSG_DEBUG, "test_driver_set_ssid: SSID", buf, len);

	if ((size_t) len > sizeof(bss->ssid))
		return -1;

	os_memcpy(bss->ssid, buf, len);
	bss->ssid_len = len;

	return 0;
}


static int test_driver_set_privacy(void *priv, int enabled)
{
	struct test_driver_bss *dbss = priv;

	wpa_printf(MSG_DEBUG, "%s(enabled=%d)",  __func__, enabled);
	dbss->privacy = enabled;

	return 0;
}


static int test_driver_set_sta_vlan(void *priv, const u8 *addr,
				    const char *ifname, int vlan_id)
{
	wpa_printf(MSG_DEBUG, "%s(addr=" MACSTR " ifname=%s vlan_id=%d)",
		   __func__, MAC2STR(addr), ifname, vlan_id);
	return 0;
}


static int test_driver_sta_add(void *priv,
			       struct hostapd_sta_add_params *params)
{
	struct test_driver_bss *bss = priv;
	struct wpa_driver_test_data *drv = bss->drv;
	struct test_client_socket *cli;

	wpa_printf(MSG_DEBUG, "%s(ifname=%s addr=" MACSTR " aid=%d "
		   "capability=0x%x listen_interval=%d)",
		   __func__, bss->ifname, MAC2STR(params->addr), params->aid,
		   params->capability, params->listen_interval);
	wpa_hexdump(MSG_DEBUG, "test_driver_sta_add - supp_rates",
		    params->supp_rates, params->supp_rates_len);

	cli = drv->cli;
	while (cli) {
		if (os_memcmp(cli->addr, params->addr, ETH_ALEN) == 0)
			break;
		cli = cli->next;
	}
	if (!cli) {
		wpa_printf(MSG_DEBUG, "%s: no matching client entry",
			   __func__);
		return -1;
	}

	cli->bss = bss;

	return 0;
}


static struct wpa_driver_test_data * test_alloc_data(void *ctx,
						     const char *ifname)
{
	struct wpa_driver_test_data *drv;
	struct test_driver_bss *bss;

	drv = os_zalloc(sizeof(struct wpa_driver_test_data));
	if (drv == NULL) {
		wpa_printf(MSG_ERROR, "Could not allocate memory for test "
			   "driver data");
		return NULL;
	}

	bss = os_zalloc(sizeof(struct test_driver_bss));
	if (bss == NULL) {
		os_free(drv);
		return NULL;
	}

	drv->ctx = ctx;
	wpa_trace_add_ref(drv, ctx, ctx);
	dl_list_init(&drv->bss);
	dl_list_add(&drv->bss, &bss->list);
	os_strlcpy(bss->ifname, ifname, IFNAMSIZ);
	bss->bss_ctx = ctx;
	bss->drv = drv;

	/* Generate a MAC address to help testing with multiple STAs */
	drv->own_addr[0] = 0x02; /* locally administered */
	sha1_prf((const u8 *) ifname, os_strlen(ifname),
		 "test mac addr generation",
		 NULL, 0, drv->own_addr + 1, ETH_ALEN - 1);

	return drv;
}


static void * test_driver_init(struct hostapd_data *hapd,
			       struct wpa_init_params *params)
{
	struct wpa_driver_test_data *drv;
	struct sockaddr_un addr_un;
	struct sockaddr_in addr_in;
	struct sockaddr *addr;
	socklen_t alen;
	struct test_driver_bss *bss;

	drv = test_alloc_data(hapd, params->ifname);
	if (drv == NULL)
		return NULL;
	drv->ap = 1;
	bss = dl_list_first(&drv->bss, struct test_driver_bss, list);

	bss->bss_ctx = hapd;
	os_memcpy(bss->bssid, drv->own_addr, ETH_ALEN);
	os_memcpy(params->own_addr, drv->own_addr, ETH_ALEN);

	if (params->test_socket) {
		if (os_strlen(params->test_socket) >=
		    sizeof(addr_un.sun_path)) {
			printf("Too long test_socket path\n");
			wpa_driver_test_deinit(bss);
			return NULL;
		}
		if (strncmp(params->test_socket, "DIR:", 4) == 0) {
			size_t len = strlen(params->test_socket) + 30;
			drv->test_dir = os_strdup(params->test_socket + 4);
			drv->own_socket_path = os_malloc(len);
			if (drv->own_socket_path) {
				snprintf(drv->own_socket_path, len,
					 "%s/AP-" MACSTR,
					 params->test_socket + 4,
					 MAC2STR(params->own_addr));
			}
		} else if (strncmp(params->test_socket, "UDP:", 4) == 0) {
			drv->udp_port = atoi(params->test_socket + 4);
		} else {
			drv->own_socket_path = os_strdup(params->test_socket);
		}
		if (drv->own_socket_path == NULL && drv->udp_port == 0) {
			wpa_driver_test_deinit(bss);
			return NULL;
		}

		drv->test_socket = socket(drv->udp_port ? PF_INET : PF_UNIX,
					  SOCK_DGRAM, 0);
		if (drv->test_socket < 0) {
			perror("socket");
			wpa_driver_test_deinit(bss);
			return NULL;
		}

		if (drv->udp_port) {
			os_memset(&addr_in, 0, sizeof(addr_in));
			addr_in.sin_family = AF_INET;
			addr_in.sin_port = htons(drv->udp_port);
			addr = (struct sockaddr *) &addr_in;
			alen = sizeof(addr_in);
		} else {
			os_memset(&addr_un, 0, sizeof(addr_un));
			addr_un.sun_family = AF_UNIX;
			os_strlcpy(addr_un.sun_path, drv->own_socket_path,
				   sizeof(addr_un.sun_path));
			addr = (struct sockaddr *) &addr_un;
			alen = sizeof(addr_un);
		}
		if (bind(drv->test_socket, addr, alen) < 0) {
			perror("bind(PF_UNIX)");
			close(drv->test_socket);
			if (drv->own_socket_path)
				unlink(drv->own_socket_path);
			wpa_driver_test_deinit(bss);
			return NULL;
		}
		eloop_register_read_sock(drv->test_socket,
					 test_driver_receive_unix, drv, NULL);
	} else
		drv->test_socket = -1;

	return bss;
}


static void wpa_driver_test_poll(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_driver_test_data *drv = eloop_ctx;

#ifdef DRIVER_TEST_UNIX
	if (drv->associated && drv->hostapd_addr_set) {
		struct stat st;
		if (stat(drv->hostapd_addr.sun_path, &st) < 0) {
			wpa_printf(MSG_DEBUG, "%s: lost connection to AP: %s",
				   __func__, strerror(errno));
			drv->associated = 0;
			wpa_supplicant_event(drv->ctx, EVENT_DISASSOC, NULL);
		}
	}
#endif /* DRIVER_TEST_UNIX */

	eloop_register_timeout(1, 0, wpa_driver_test_poll, drv, NULL);
}


static void wpa_driver_test_scan_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_driver_test_data *drv = eloop_ctx;
	wpa_printf(MSG_DEBUG, "Scan timeout - try to get results");
	if (drv->pending_p2p_scan && drv->p2p) {
#ifdef CONFIG_P2P
		size_t i;
		for (i = 0; i < drv->num_scanres; i++) {
			struct wpa_scan_res *bss = drv->scanres[i];
			if (p2p_scan_res_handler(drv->p2p, bss->bssid,
						 bss->freq, bss->level,
						 (const u8 *) (bss + 1),
						 bss->ie_len) > 0)
				return;
		}
		p2p_scan_res_handled(drv->p2p);
#endif /* CONFIG_P2P */
		return;
	}
	wpa_supplicant_event(timeout_ctx, EVENT_SCAN_RESULTS, NULL);
}


#ifdef DRIVER_TEST_UNIX
static void wpa_driver_scan_dir(struct wpa_driver_test_data *drv,
				const char *path)
{
	struct dirent *dent;
	DIR *dir;
	struct sockaddr_un addr;
	char cmd[512], *pos, *end;
	int ret;

	dir = opendir(path);
	if (dir == NULL)
		return;

	end = cmd + sizeof(cmd);
	pos = cmd;
	ret = os_snprintf(pos, end - pos, "SCAN " MACSTR,
			  MAC2STR(drv->own_addr));
	if (ret >= 0 && ret < end - pos)
		pos += ret;
	if (drv->probe_req_ie) {
		ret = os_snprintf(pos, end - pos, " ");
		if (ret >= 0 && ret < end - pos)
			pos += ret;
		pos += wpa_snprintf_hex(pos, end - pos, drv->probe_req_ie,
					drv->probe_req_ie_len);
	}
	if (drv->probe_req_ssid_len) {
		/* Add SSID IE */
		ret = os_snprintf(pos, end - pos, "%02x%02x",
				  WLAN_EID_SSID,
				  (unsigned int) drv->probe_req_ssid_len);
		if (ret >= 0 && ret < end - pos)
			pos += ret;
		pos += wpa_snprintf_hex(pos, end - pos, drv->probe_req_ssid,
					drv->probe_req_ssid_len);
	}
	end[-1] = '\0';

	while ((dent = readdir(dir))) {
		if (os_strncmp(dent->d_name, "AP-", 3) != 0 &&
		    os_strncmp(dent->d_name, "STA-", 4) != 0)
			continue;
		if (drv->own_socket_path) {
			size_t olen, dlen;
			olen = os_strlen(drv->own_socket_path);
			dlen = os_strlen(dent->d_name);
			if (olen >= dlen &&
			    os_strcmp(dent->d_name,
				      drv->own_socket_path + olen - dlen) == 0)
				continue;
		}
		wpa_printf(MSG_DEBUG, "%s: SCAN %s", __func__, dent->d_name);

		os_memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		os_snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/%s",
			    path, dent->d_name);

		if (sendto(drv->test_socket, cmd, os_strlen(cmd), 0,
			   (struct sockaddr *) &addr, sizeof(addr)) < 0) {
			perror("sendto(test_socket)");
		}
	}
	closedir(dir);
}
#endif /* DRIVER_TEST_UNIX */


static int wpa_driver_test_scan(void *priv,
				struct wpa_driver_scan_params *params)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	size_t i;

	wpa_printf(MSG_DEBUG, "%s: priv=%p", __func__, priv);

	os_free(drv->probe_req_ie);
	if (params->extra_ies) {
		drv->probe_req_ie = os_malloc(params->extra_ies_len);
		if (drv->probe_req_ie == NULL) {
			drv->probe_req_ie_len = 0;
			return -1;
		}
		os_memcpy(drv->probe_req_ie, params->extra_ies,
			  params->extra_ies_len);
		drv->probe_req_ie_len = params->extra_ies_len;
	} else {
		drv->probe_req_ie = NULL;
		drv->probe_req_ie_len = 0;
	}

	for (i = 0; i < params->num_ssids; i++)
		wpa_hexdump(MSG_DEBUG, "Scan SSID",
			    params->ssids[i].ssid, params->ssids[i].ssid_len);
	drv->probe_req_ssid_len = 0;
	if (params->num_ssids) {
		os_memcpy(drv->probe_req_ssid, params->ssids[0].ssid,
			  params->ssids[0].ssid_len);
		drv->probe_req_ssid_len = params->ssids[0].ssid_len;
	}
	wpa_hexdump(MSG_DEBUG, "Scan extra IE(s)",
		    params->extra_ies, params->extra_ies_len);

	drv->num_scanres = 0;

#ifdef DRIVER_TEST_UNIX
	if (drv->test_socket >= 0 && drv->test_dir)
		wpa_driver_scan_dir(drv, drv->test_dir);

	if (drv->test_socket >= 0 && drv->hostapd_addr_set &&
	    sendto(drv->test_socket, "SCAN", 4, 0,
		   (struct sockaddr *) &drv->hostapd_addr,
		   sizeof(drv->hostapd_addr)) < 0) {
		perror("sendto(test_socket)");
	}
#endif /* DRIVER_TEST_UNIX */

	if (drv->test_socket >= 0 && drv->hostapd_addr_udp_set &&
	    sendto(drv->test_socket, "SCAN", 4, 0,
		   (struct sockaddr *) &drv->hostapd_addr_udp,
		   sizeof(drv->hostapd_addr_udp)) < 0) {
		perror("sendto(test_socket)");
	}

	eloop_cancel_timeout(wpa_driver_test_scan_timeout, drv, drv->ctx);
	eloop_register_timeout(1, 0, wpa_driver_test_scan_timeout, drv,
			       drv->ctx);
	return 0;
}


static struct wpa_scan_results * wpa_driver_test_get_scan_results2(void *priv)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	struct wpa_scan_results *res;
	size_t i;

	res = os_zalloc(sizeof(*res));
	if (res == NULL)
		return NULL;

	res->res = os_zalloc(drv->num_scanres * sizeof(struct wpa_scan_res *));
	if (res->res == NULL) {
		os_free(res);
		return NULL;
	}

	for (i = 0; i < drv->num_scanres; i++) {
		struct wpa_scan_res *r;
		if (drv->scanres[i] == NULL)
			continue;
		r = os_malloc(sizeof(*r) + drv->scanres[i]->ie_len);
		if (r == NULL)
			break;
		os_memcpy(r, drv->scanres[i],
			  sizeof(*r) + drv->scanres[i]->ie_len);
		res->res[res->num++] = r;
	}

	return res;
}


static int wpa_driver_test_set_key(const char *ifname, void *priv,
				   enum wpa_alg alg, const u8 *addr,
				   int key_idx, int set_tx,
				   const u8 *seq, size_t seq_len,
				   const u8 *key, size_t key_len)
{
	wpa_printf(MSG_DEBUG, "%s: ifname=%s priv=%p alg=%d key_idx=%d "
		   "set_tx=%d",
		   __func__, ifname, priv, alg, key_idx, set_tx);
	if (addr)
		wpa_printf(MSG_DEBUG, "   addr=" MACSTR, MAC2STR(addr));
	if (seq)
		wpa_hexdump(MSG_DEBUG, "   seq", seq, seq_len);
	if (key)
		wpa_hexdump_key(MSG_DEBUG, "   key", key, key_len);
	return 0;
}


static int wpa_driver_update_mode(struct wpa_driver_test_data *drv, int ap)
{
	if (ap && !drv->ap) {
		wpa_driver_test_close_test_socket(drv);
		wpa_driver_test_attach(drv, drv->test_dir, 1);
		drv->ap = 1;
	} else if (!ap && drv->ap) {
		wpa_driver_test_close_test_socket(drv);
		wpa_driver_test_attach(drv, drv->test_dir, 0);
		drv->ap = 0;
	}

	return 0;
}


static int wpa_driver_test_associate(
	void *priv, struct wpa_driver_associate_params *params)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	wpa_printf(MSG_DEBUG, "%s: priv=%p freq=%d pairwise_suite=%d "
		   "group_suite=%d key_mgmt_suite=%d auth_alg=%d mode=%d",
		   __func__, priv, params->freq, params->pairwise_suite,
		   params->group_suite, params->key_mgmt_suite,
		   params->auth_alg, params->mode);
	wpa_driver_update_mode(drv, params->mode == IEEE80211_MODE_AP);
	if (params->bssid) {
		wpa_printf(MSG_DEBUG, "   bssid=" MACSTR,
			   MAC2STR(params->bssid));
	}
	if (params->ssid) {
		wpa_hexdump_ascii(MSG_DEBUG, "   ssid",
				  params->ssid, params->ssid_len);
	}
	if (params->wpa_ie) {
		wpa_hexdump(MSG_DEBUG, "   wpa_ie",
			    params->wpa_ie, params->wpa_ie_len);
		drv->assoc_wpa_ie_len = params->wpa_ie_len;
		if (drv->assoc_wpa_ie_len > sizeof(drv->assoc_wpa_ie))
			drv->assoc_wpa_ie_len = sizeof(drv->assoc_wpa_ie);
		os_memcpy(drv->assoc_wpa_ie, params->wpa_ie,
			  drv->assoc_wpa_ie_len);
	} else
		drv->assoc_wpa_ie_len = 0;

	wpa_driver_update_mode(drv, params->mode == IEEE80211_MODE_AP);

	drv->ibss = params->mode == IEEE80211_MODE_IBSS;
	dbss->privacy = params->key_mgmt_suite &
		(WPA_KEY_MGMT_IEEE8021X |
		 WPA_KEY_MGMT_PSK |
		 WPA_KEY_MGMT_WPA_NONE |
		 WPA_KEY_MGMT_FT_IEEE8021X |
		 WPA_KEY_MGMT_FT_PSK |
		 WPA_KEY_MGMT_IEEE8021X_SHA256 |
		 WPA_KEY_MGMT_PSK_SHA256);
	if (params->wep_key_len[params->wep_tx_keyidx])
		dbss->privacy = 1;

#ifdef DRIVER_TEST_UNIX
	if (drv->test_dir && params->bssid &&
	    params->mode != IEEE80211_MODE_IBSS) {
		os_memset(&drv->hostapd_addr, 0, sizeof(drv->hostapd_addr));
		drv->hostapd_addr.sun_family = AF_UNIX;
		os_snprintf(drv->hostapd_addr.sun_path,
			    sizeof(drv->hostapd_addr.sun_path),
			    "%s/AP-" MACSTR,
			    drv->test_dir, MAC2STR(params->bssid));
		drv->hostapd_addr_set = 1;
	}
#endif /* DRIVER_TEST_UNIX */

	if (params->mode == IEEE80211_MODE_AP) {
		os_memcpy(dbss->ssid, params->ssid, params->ssid_len);
		dbss->ssid_len = params->ssid_len;
		os_memcpy(dbss->bssid, drv->own_addr, ETH_ALEN);
		if (params->wpa_ie && params->wpa_ie_len) {
			dbss->ie = os_malloc(params->wpa_ie_len);
			if (dbss->ie) {
				os_memcpy(dbss->ie, params->wpa_ie,
					  params->wpa_ie_len);
				dbss->ielen = params->wpa_ie_len;
			}
		}
	} else if (drv->test_socket >= 0 &&
		   (drv->hostapd_addr_set || drv->hostapd_addr_udp_set)) {
		char cmd[200], *pos, *end;
		int ret;
		end = cmd + sizeof(cmd);
		pos = cmd;
		ret = os_snprintf(pos, end - pos, "ASSOC " MACSTR " ",
				  MAC2STR(drv->own_addr));
		if (ret >= 0 && ret < end - pos)
			pos += ret;
		pos += wpa_snprintf_hex(pos, end - pos, params->ssid,
					params->ssid_len);
		ret = os_snprintf(pos, end - pos, " ");
		if (ret >= 0 && ret < end - pos)
			pos += ret;
		pos += wpa_snprintf_hex(pos, end - pos, params->wpa_ie,
					params->wpa_ie_len);
		end[-1] = '\0';
#ifdef DRIVER_TEST_UNIX
		if (drv->hostapd_addr_set &&
		    sendto(drv->test_socket, cmd, os_strlen(cmd), 0,
			   (struct sockaddr *) &drv->hostapd_addr,
			   sizeof(drv->hostapd_addr)) < 0) {
			perror("sendto(test_socket)");
			return -1;
		}
#endif /* DRIVER_TEST_UNIX */
		if (drv->hostapd_addr_udp_set &&
		    sendto(drv->test_socket, cmd, os_strlen(cmd), 0,
			   (struct sockaddr *) &drv->hostapd_addr_udp,
			   sizeof(drv->hostapd_addr_udp)) < 0) {
			perror("sendto(test_socket)");
			return -1;
		}

		os_memcpy(dbss->ssid, params->ssid, params->ssid_len);
		dbss->ssid_len = params->ssid_len;
	} else {
		drv->associated = 1;
		if (params->mode == IEEE80211_MODE_IBSS) {
			os_memcpy(dbss->ssid, params->ssid, params->ssid_len);
			dbss->ssid_len = params->ssid_len;
			if (params->bssid)
				os_memcpy(dbss->bssid, params->bssid,
					  ETH_ALEN);
			else {
				os_get_random(dbss->bssid, ETH_ALEN);
				dbss->bssid[0] &= ~0x01;
				dbss->bssid[0] |= 0x02;
			}
		}
		wpa_supplicant_event(drv->ctx, EVENT_ASSOC, NULL);
	}

	return 0;
}


static int wpa_driver_test_get_bssid(void *priv, u8 *bssid)
{
	struct test_driver_bss *dbss = priv;
	os_memcpy(bssid, dbss->bssid, ETH_ALEN);
	return 0;
}


static int wpa_driver_test_get_ssid(void *priv, u8 *ssid)
{
	struct test_driver_bss *dbss = priv;
	os_memcpy(ssid, dbss->ssid, 32);
	return dbss->ssid_len;
}


static int wpa_driver_test_send_disassoc(struct wpa_driver_test_data *drv)
{
#ifdef DRIVER_TEST_UNIX
	if (drv->test_socket >= 0 &&
	    sendto(drv->test_socket, "DISASSOC", 8, 0,
		   (struct sockaddr *) &drv->hostapd_addr,
		   sizeof(drv->hostapd_addr)) < 0) {
		perror("sendto(test_socket)");
		return -1;
	}
#endif /* DRIVER_TEST_UNIX */
	if (drv->test_socket >= 0 && drv->hostapd_addr_udp_set &&
	    sendto(drv->test_socket, "DISASSOC", 8, 0,
		   (struct sockaddr *) &drv->hostapd_addr_udp,
		   sizeof(drv->hostapd_addr_udp)) < 0) {
		perror("sendto(test_socket)");
		return -1;
	}
	return 0;
}


static int wpa_driver_test_deauthenticate(void *priv, const u8 *addr,
					  int reason_code)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	wpa_printf(MSG_DEBUG, "%s addr=" MACSTR " reason_code=%d",
		   __func__, MAC2STR(addr), reason_code);
	os_memset(dbss->bssid, 0, ETH_ALEN);
	drv->associated = 0;
	wpa_supplicant_event(drv->ctx, EVENT_DISASSOC, NULL);
	return wpa_driver_test_send_disassoc(drv);
}


static int wpa_driver_test_disassociate(void *priv, const u8 *addr,
					int reason_code)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	wpa_printf(MSG_DEBUG, "%s addr=" MACSTR " reason_code=%d",
		   __func__, MAC2STR(addr), reason_code);
	os_memset(dbss->bssid, 0, ETH_ALEN);
	drv->associated = 0;
	wpa_supplicant_event(drv->ctx, EVENT_DISASSOC, NULL);
	return wpa_driver_test_send_disassoc(drv);
}


static const u8 * wpa_scan_get_ie(const struct wpa_scan_res *res, u8 ie)
{
	const u8 *end, *pos;

	pos = (const u8 *) (res + 1);
	end = pos + res->ie_len;

	while (pos + 1 < end) {
		if (pos + 2 + pos[1] > end)
			break;
		if (pos[0] == ie)
			return pos;
		pos += 2 + pos[1];
	}

	return NULL;
}


static void wpa_driver_test_scanresp(struct wpa_driver_test_data *drv,
				     struct sockaddr *from,
				     socklen_t fromlen,
				     const char *data)
{
	struct wpa_scan_res *res;
	const char *pos, *pos2;
	size_t len;
	u8 *ie_pos, *ie_start, *ie_end;
#define MAX_IE_LEN 1000
	const u8 *ds_params;

	wpa_printf(MSG_DEBUG, "test_driver: SCANRESP %s", data);
	if (drv->num_scanres >= MAX_SCAN_RESULTS) {
		wpa_printf(MSG_DEBUG, "test_driver: No room for the new scan "
			   "result");
		return;
	}

	/* SCANRESP BSSID SSID IEs */

	res = os_zalloc(sizeof(*res) + MAX_IE_LEN);
	if (res == NULL)
		return;
	ie_start = ie_pos = (u8 *) (res + 1);
	ie_end = ie_pos + MAX_IE_LEN;

	if (hwaddr_aton(data, res->bssid)) {
		wpa_printf(MSG_DEBUG, "test_driver: invalid BSSID in scanres");
		os_free(res);
		return;
	}

	pos = data + 17;
	while (*pos == ' ')
		pos++;
	pos2 = os_strchr(pos, ' ');
	if (pos2 == NULL) {
		wpa_printf(MSG_DEBUG, "test_driver: invalid SSID termination "
			   "in scanres");
		os_free(res);
		return;
	}
	len = (pos2 - pos) / 2;
	if (len > 32)
		len = 32;
	/*
	 * Generate SSID IE from the SSID field since this IE is not included
	 * in the main IE field.
	 */
	*ie_pos++ = WLAN_EID_SSID;
	*ie_pos++ = len;
	if (hexstr2bin(pos, ie_pos, len) < 0) {
		wpa_printf(MSG_DEBUG, "test_driver: invalid SSID in scanres");
		os_free(res);
		return;
	}
	ie_pos += len;

	pos = pos2 + 1;
	pos2 = os_strchr(pos, ' ');
	if (pos2 == NULL)
		len = os_strlen(pos) / 2;
	else
		len = (pos2 - pos) / 2;
	if ((int) len > ie_end - ie_pos)
		len = ie_end - ie_pos;
	if (hexstr2bin(pos, ie_pos, len) < 0) {
		wpa_printf(MSG_DEBUG, "test_driver: invalid IEs in scanres");
		os_free(res);
		return;
	}
	ie_pos += len;
	res->ie_len = ie_pos - ie_start;

	if (pos2) {
		pos = pos2 + 1;
		while (*pos == ' ')
			pos++;
		if (os_strstr(pos, "PRIVACY"))
			res->caps |= IEEE80211_CAP_PRIVACY;
		if (os_strstr(pos, "IBSS"))
			res->caps |= IEEE80211_CAP_IBSS;
	}

	ds_params = wpa_scan_get_ie(res, WLAN_EID_DS_PARAMS);
	if (ds_params && ds_params[1] > 0) {
		if (ds_params[2] >= 1 && ds_params[2] <= 13)
			res->freq = 2407 + ds_params[2] * 5;
	}

	os_free(drv->scanres[drv->num_scanres]);
	drv->scanres[drv->num_scanres++] = res;
}


static void wpa_driver_test_assocresp(struct wpa_driver_test_data *drv,
				      struct sockaddr *from,
				      socklen_t fromlen,
				      const char *data)
{
	struct test_driver_bss *bss;

	bss = dl_list_first(&drv->bss, struct test_driver_bss, list);

	/* ASSOCRESP BSSID <res> */
	if (hwaddr_aton(data, bss->bssid)) {
		wpa_printf(MSG_DEBUG, "test_driver: invalid BSSID in "
			   "assocresp");
	}
	if (drv->use_associnfo) {
		union wpa_event_data event;
		os_memset(&event, 0, sizeof(event));
		event.assoc_info.req_ies = drv->assoc_wpa_ie;
		event.assoc_info.req_ies_len = drv->assoc_wpa_ie_len;
		wpa_supplicant_event(drv->ctx, EVENT_ASSOCINFO, &event);
	}
	drv->associated = 1;
	wpa_supplicant_event(drv->ctx, EVENT_ASSOC, NULL);
}


static void wpa_driver_test_disassoc(struct wpa_driver_test_data *drv,
				     struct sockaddr *from,
				     socklen_t fromlen)
{
	drv->associated = 0;
	wpa_supplicant_event(drv->ctx, EVENT_DISASSOC, NULL);
}


static void wpa_driver_test_eapol(struct wpa_driver_test_data *drv,
				  struct sockaddr *from,
				  socklen_t fromlen,
				  const u8 *data, size_t data_len)
{
	const u8 *src;
	struct test_driver_bss *bss;

	bss = dl_list_first(&drv->bss, struct test_driver_bss, list);

	if (data_len > 14) {
		/* Skip Ethernet header */
		src = data + ETH_ALEN;
		data += 14;
		data_len -= 14;
	} else
		src = bss->bssid;

	drv_event_eapol_rx(drv->ctx, src, data, data_len);
}


static void wpa_driver_test_mlme(struct wpa_driver_test_data *drv,
				 struct sockaddr *from,
				 socklen_t fromlen,
				 const u8 *data, size_t data_len)
{
	int freq = 0, own_freq;
	union wpa_event_data event;
	const struct ieee80211_mgmt *mgmt;
	u16 fc;
	struct test_driver_bss *bss;

	bss = dl_list_first(&drv->bss, struct test_driver_bss, list);
	if (data_len > 6 && os_memcmp(data, "freq=", 5) == 0) {
		size_t pos;
		for (pos = 5; pos < data_len; pos++) {
			if (data[pos] == ' ')
				break;
		}
		if (pos < data_len) {
			freq = atoi((const char *) &data[5]);
			wpa_printf(MSG_DEBUG, "test_driver(%s): MLME RX on "
				   "freq %d MHz", bss->ifname, freq);
			pos++;
			data += pos;
			data_len -= pos;
		}
	}

	if (drv->remain_on_channel_freq)
		own_freq = drv->remain_on_channel_freq;
	else
		own_freq = drv->current_freq;

	if (freq && own_freq && freq != own_freq) {
		wpa_printf(MSG_DEBUG, "test_driver(%s): Ignore MLME RX on "
			   "another frequency %d MHz (own %d MHz)",
			   bss->ifname, freq, own_freq);
		return;
	}

	os_memset(&event, 0, sizeof(event));
	event.mlme_rx.buf = data;
	event.mlme_rx.len = data_len;
	event.mlme_rx.freq = freq;
	wpa_supplicant_event(drv->ctx, EVENT_MLME_RX, &event);

	mgmt = (const struct ieee80211_mgmt *) data;
	fc = le_to_host16(mgmt->frame_control);

	if (drv->probe_req_report && data_len >= 24) {
		if (WLAN_FC_GET_TYPE(fc) == WLAN_FC_TYPE_MGMT &&
		    WLAN_FC_GET_STYPE(fc) == WLAN_FC_STYPE_PROBE_REQ) {
			os_memset(&event, 0, sizeof(event));
			event.rx_probe_req.sa = mgmt->sa;
			event.rx_probe_req.ie = mgmt->u.probe_req.variable;
			event.rx_probe_req.ie_len =
				data_len - (mgmt->u.probe_req.variable - data);
			wpa_supplicant_event(drv->ctx, EVENT_RX_PROBE_REQ,
					     &event);
#ifdef CONFIG_P2P
			if (drv->p2p)
				p2p_probe_req_rx(drv->p2p, mgmt->sa,
						 event.rx_probe_req.ie,
						 event.rx_probe_req.ie_len);
#endif /* CONFIG_P2P */
		}
	}

#ifdef CONFIG_P2P
	if (drv->p2p &&
	    WLAN_FC_GET_TYPE(fc) == WLAN_FC_TYPE_MGMT &&
	    WLAN_FC_GET_STYPE(fc) == WLAN_FC_STYPE_ACTION) {
		size_t hdr_len;
		hdr_len = (const u8 *)
			&mgmt->u.action.u.vs_public_action.action - data;
		p2p_rx_action(drv->p2p, mgmt->da, mgmt->sa, mgmt->bssid,
			      mgmt->u.action.category,
			      &mgmt->u.action.u.vs_public_action.action,
			      data_len - hdr_len, freq);
	}
#endif /* CONFIG_P2P */

}


static void wpa_driver_test_scan_cmd(struct wpa_driver_test_data *drv,
				     struct sockaddr *from,
				     socklen_t fromlen,
				     const u8 *data, size_t data_len)
{
	char buf[512], *pos, *end;
	int ret;
	struct test_driver_bss *bss;

	bss = dl_list_first(&drv->bss, struct test_driver_bss, list);

	/* data: optional [ STA-addr | ' ' | IEs(hex) ] */
#ifdef CONFIG_P2P
	if (drv->probe_req_report && drv->p2p && data_len) {
		const char *d = (const char *) data;
		u8 sa[ETH_ALEN];
		u8 ie[512];
		size_t ielen;

		if (hwaddr_aton(d, sa))
			return;
		d += 18;
		while (*d == ' ')
			d++;
		ielen = os_strlen(d) / 2;
		if (ielen > sizeof(ie))
			ielen = sizeof(ie);
		if (hexstr2bin(d, ie, ielen) < 0)
			ielen = 0;
		drv->probe_from = from;
		drv->probe_from_len = fromlen;
		p2p_probe_req_rx(drv->p2p, sa, ie, ielen);
		drv->probe_from = NULL;
	}
#endif /* CONFIG_P2P */

	if (!drv->ibss)
		return;

	pos = buf;
	end = buf + sizeof(buf);

	/* reply: SCANRESP BSSID SSID IEs */
	ret = snprintf(pos, end - pos, "SCANRESP " MACSTR " ",
		       MAC2STR(bss->bssid));
	if (ret < 0 || ret >= end - pos)
		return;
	pos += ret;
	pos += wpa_snprintf_hex(pos, end - pos,
				bss->ssid, bss->ssid_len);
	ret = snprintf(pos, end - pos, " ");
	if (ret < 0 || ret >= end - pos)
		return;
	pos += ret;
	pos += wpa_snprintf_hex(pos, end - pos, drv->assoc_wpa_ie,
				drv->assoc_wpa_ie_len);

	if (bss->privacy) {
		ret = snprintf(pos, end - pos, " PRIVACY");
		if (ret < 0 || ret >= end - pos)
			return;
		pos += ret;
	}

	ret = snprintf(pos, end - pos, " IBSS");
	if (ret < 0 || ret >= end - pos)
		return;
	pos += ret;

	sendto(drv->test_socket, buf, pos - buf, 0,
	       (struct sockaddr *) from, fromlen);
}


static void wpa_driver_test_receive_unix(int sock, void *eloop_ctx,
					 void *sock_ctx)
{
	struct wpa_driver_test_data *drv = eloop_ctx;
	char *buf;
	int res;
	struct sockaddr_storage from;
	socklen_t fromlen = sizeof(from);
	const size_t buflen = 2000;

	if (drv->ap) {
		test_driver_receive_unix(sock, eloop_ctx, sock_ctx);
		return;
	}

	buf = os_malloc(buflen);
	if (buf == NULL)
		return;
	res = recvfrom(sock, buf, buflen - 1, 0,
		       (struct sockaddr *) &from, &fromlen);
	if (res < 0) {
		perror("recvfrom(test_socket)");
		os_free(buf);
		return;
	}
	buf[res] = '\0';

	wpa_printf(MSG_DEBUG, "test_driver: received %u bytes", res);

	if (os_strncmp(buf, "SCANRESP ", 9) == 0) {
		wpa_driver_test_scanresp(drv, (struct sockaddr *) &from,
					 fromlen, buf + 9);
	} else if (os_strncmp(buf, "ASSOCRESP ", 10) == 0) {
		wpa_driver_test_assocresp(drv, (struct sockaddr *) &from,
					  fromlen, buf + 10);
	} else if (os_strcmp(buf, "DISASSOC") == 0) {
		wpa_driver_test_disassoc(drv, (struct sockaddr *) &from,
					 fromlen);
	} else if (os_strcmp(buf, "DEAUTH") == 0) {
		wpa_driver_test_disassoc(drv, (struct sockaddr *) &from,
					 fromlen);
	} else if (os_strncmp(buf, "EAPOL ", 6) == 0) {
		wpa_driver_test_eapol(drv, (struct sockaddr *) &from, fromlen,
				      (const u8 *) buf + 6, res - 6);
	} else if (os_strncmp(buf, "MLME ", 5) == 0) {
		wpa_driver_test_mlme(drv, (struct sockaddr *) &from, fromlen,
				     (const u8 *) buf + 5, res - 5);
	} else if (os_strncmp(buf, "SCAN ", 5) == 0) {
		wpa_driver_test_scan_cmd(drv, (struct sockaddr *) &from,
					 fromlen,
					 (const u8 *) buf + 5, res - 5);
	} else {
		wpa_hexdump_ascii(MSG_DEBUG, "Unknown test_socket command",
				  (u8 *) buf, res);
	}
	os_free(buf);
}


static void * wpa_driver_test_init2(void *ctx, const char *ifname,
				    void *global_priv)
{
	struct wpa_driver_test_data *drv;
	struct wpa_driver_test_global *global = global_priv;
	struct test_driver_bss *bss;

	drv = test_alloc_data(ctx, ifname);
	if (drv == NULL)
		return NULL;
	bss = dl_list_first(&drv->bss, struct test_driver_bss, list);
	drv->global = global_priv;
	drv->test_socket = -1;

	/* Set dummy BSSID and SSID for testing. */
	bss->bssid[0] = 0x02;
	bss->bssid[1] = 0x00;
	bss->bssid[2] = 0x00;
	bss->bssid[3] = 0x00;
	bss->bssid[4] = 0x00;
	bss->bssid[5] = 0x01;
	os_memcpy(bss->ssid, "test", 5);
	bss->ssid_len = 4;

	if (global->bss_add_used) {
		os_memcpy(drv->own_addr, global->req_addr, ETH_ALEN);
		global->bss_add_used = 0;
	}

	eloop_register_timeout(1, 0, wpa_driver_test_poll, drv, NULL);

	return bss;
}


static void wpa_driver_test_close_test_socket(struct wpa_driver_test_data *drv)
{
	if (drv->test_socket >= 0) {
		eloop_unregister_read_sock(drv->test_socket);
		close(drv->test_socket);
		drv->test_socket = -1;
	}

	if (drv->own_socket_path) {
		unlink(drv->own_socket_path);
		os_free(drv->own_socket_path);
		drv->own_socket_path = NULL;
	}
}


static void wpa_driver_test_deinit(void *priv)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	struct test_client_socket *cli, *prev;
	int i;

#ifdef CONFIG_P2P
	if (drv->p2p)
		p2p_deinit(drv->p2p);
	wpabuf_free(drv->pending_action_tx);
#endif /* CONFIG_P2P */

	cli = drv->cli;
	while (cli) {
		prev = cli;
		cli = cli->next;
		os_free(prev);
	}

#ifdef HOSTAPD
	/* There should be only one BSS remaining at this point. */
	if (dl_list_len(&drv->bss) != 1)
		wpa_printf(MSG_ERROR, "%s: %u remaining BSS entries",
			   __func__, dl_list_len(&drv->bss));
#endif /* HOSTAPD */

	test_driver_free_bsses(drv);

	wpa_driver_test_close_test_socket(drv);
	eloop_cancel_timeout(wpa_driver_test_scan_timeout, drv, drv->ctx);
	eloop_cancel_timeout(wpa_driver_test_poll, drv, NULL);
	eloop_cancel_timeout(test_remain_on_channel_timeout, drv, NULL);
	os_free(drv->test_dir);
	for (i = 0; i < MAX_SCAN_RESULTS; i++)
		os_free(drv->scanres[i]);
	os_free(drv->probe_req_ie);
	wpa_trace_remove_ref(drv, ctx, drv->ctx);
	os_free(drv);
}


static int wpa_driver_test_attach(struct wpa_driver_test_data *drv,
				  const char *dir, int ap)
{
#ifdef DRIVER_TEST_UNIX
	static unsigned int counter = 0;
	struct sockaddr_un addr;
	size_t len;

	os_free(drv->own_socket_path);
	if (dir) {
		len = os_strlen(dir) + 30;
		drv->own_socket_path = os_malloc(len);
		if (drv->own_socket_path == NULL)
			return -1;
		os_snprintf(drv->own_socket_path, len, "%s/%s-" MACSTR,
			    dir, ap ? "AP" : "STA", MAC2STR(drv->own_addr));
	} else {
		drv->own_socket_path = os_malloc(100);
		if (drv->own_socket_path == NULL)
			return -1;
		os_snprintf(drv->own_socket_path, 100,
			    "/tmp/wpa_supplicant_test-%d-%d",
			    getpid(), counter++);
	}

	drv->test_socket = socket(PF_UNIX, SOCK_DGRAM, 0);
	if (drv->test_socket < 0) {
		perror("socket(PF_UNIX)");
		os_free(drv->own_socket_path);
		drv->own_socket_path = NULL;
		return -1;
	}

	os_memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	os_strlcpy(addr.sun_path, drv->own_socket_path, sizeof(addr.sun_path));
	if (bind(drv->test_socket, (struct sockaddr *) &addr,
		 sizeof(addr)) < 0) {
		perror("bind(PF_UNIX)");
		close(drv->test_socket);
		unlink(drv->own_socket_path);
		os_free(drv->own_socket_path);
		drv->own_socket_path = NULL;
		return -1;
	}

	eloop_register_read_sock(drv->test_socket,
				 wpa_driver_test_receive_unix, drv, NULL);

	return 0;
#else /* DRIVER_TEST_UNIX */
	return -1;
#endif /* DRIVER_TEST_UNIX */
}


static int wpa_driver_test_attach_udp(struct wpa_driver_test_data *drv,
				      char *dst)
{
	char *pos;

	pos = os_strchr(dst, ':');
	if (pos == NULL)
		return -1;
	*pos++ = '\0';
	wpa_printf(MSG_DEBUG, "%s: addr=%s port=%s", __func__, dst, pos);

	drv->test_socket = socket(PF_INET, SOCK_DGRAM, 0);
	if (drv->test_socket < 0) {
		perror("socket(PF_INET)");
		return -1;
	}

	os_memset(&drv->hostapd_addr_udp, 0, sizeof(drv->hostapd_addr_udp));
	drv->hostapd_addr_udp.sin_family = AF_INET;
#if defined(CONFIG_NATIVE_WINDOWS) || defined(CONFIG_ANSI_C_EXTRA)
	{
		int a[4];
		u8 *pos;
		sscanf(dst, "%d.%d.%d.%d", &a[0], &a[1], &a[2], &a[3]);
		pos = (u8 *) &drv->hostapd_addr_udp.sin_addr;
		*pos++ = a[0];
		*pos++ = a[1];
		*pos++ = a[2];
		*pos++ = a[3];
	}
#else /* CONFIG_NATIVE_WINDOWS or CONFIG_ANSI_C_EXTRA */
	inet_aton(dst, &drv->hostapd_addr_udp.sin_addr);
#endif /* CONFIG_NATIVE_WINDOWS or CONFIG_ANSI_C_EXTRA */
	drv->hostapd_addr_udp.sin_port = htons(atoi(pos));

	drv->hostapd_addr_udp_set = 1;

	eloop_register_read_sock(drv->test_socket,
				 wpa_driver_test_receive_unix, drv, NULL);

	return 0;
}


static int wpa_driver_test_set_param(void *priv, const char *param)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	const char *pos;

	wpa_printf(MSG_DEBUG, "%s: param='%s'", __func__, param);
	if (param == NULL)
		return 0;

	wpa_driver_test_close_test_socket(drv);

#ifdef DRIVER_TEST_UNIX
	pos = os_strstr(param, "test_socket=");
	if (pos) {
		const char *pos2;
		size_t len;

		pos += 12;
		pos2 = os_strchr(pos, ' ');
		if (pos2)
			len = pos2 - pos;
		else
			len = os_strlen(pos);
		if (len > sizeof(drv->hostapd_addr.sun_path))
			return -1;
		os_memset(&drv->hostapd_addr, 0, sizeof(drv->hostapd_addr));
		drv->hostapd_addr.sun_family = AF_UNIX;
		os_memcpy(drv->hostapd_addr.sun_path, pos, len);
		drv->hostapd_addr_set = 1;
	}
#endif /* DRIVER_TEST_UNIX */

	pos = os_strstr(param, "test_dir=");
	if (pos) {
		char *end;
		os_free(drv->test_dir);
		drv->test_dir = os_strdup(pos + 9);
		if (drv->test_dir == NULL)
			return -1;
		end = os_strchr(drv->test_dir, ' ');
		if (end)
			*end = '\0';
		if (wpa_driver_test_attach(drv, drv->test_dir, 0))
			return -1;
	} else {
		pos = os_strstr(param, "test_udp=");
		if (pos) {
			char *dst, *epos;
			dst = os_strdup(pos + 9);
			if (dst == NULL)
				return -1;
			epos = os_strchr(dst, ' ');
			if (epos)
				*epos = '\0';
			if (wpa_driver_test_attach_udp(drv, dst))
				return -1;
			os_free(dst);
		} else if (wpa_driver_test_attach(drv, NULL, 0))
			return -1;
	}

	if (os_strstr(param, "use_associnfo=1")) {
		wpa_printf(MSG_DEBUG, "test_driver: Use AssocInfo events");
		drv->use_associnfo = 1;
	}

#ifdef CONFIG_CLIENT_MLME
	if (os_strstr(param, "use_mlme=1")) {
		wpa_printf(MSG_DEBUG, "test_driver: Use internal MLME");
		drv->use_mlme = 1;
	}
#endif /* CONFIG_CLIENT_MLME */

	if (os_strstr(param, "p2p_mgmt=1")) {
		wpa_printf(MSG_DEBUG, "test_driver: Use internal P2P "
			   "management");
		if (wpa_driver_test_init_p2p(drv) < 0)
			return -1;
	}

	return 0;
}


static const u8 * wpa_driver_test_get_mac_addr(void *priv)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	wpa_printf(MSG_DEBUG, "%s", __func__);
	return drv->own_addr;
}


static int wpa_driver_test_send_eapol(void *priv, const u8 *dest, u16 proto,
				      const u8 *data, size_t data_len)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	char *msg;
	size_t msg_len;
	struct l2_ethhdr eth;
	struct sockaddr *addr;
	socklen_t alen;
#ifdef DRIVER_TEST_UNIX
	struct sockaddr_un addr_un;
#endif /* DRIVER_TEST_UNIX */

	wpa_hexdump(MSG_MSGDUMP, "test_send_eapol TX frame", data, data_len);

	os_memset(&eth, 0, sizeof(eth));
	os_memcpy(eth.h_dest, dest, ETH_ALEN);
	os_memcpy(eth.h_source, drv->own_addr, ETH_ALEN);
	eth.h_proto = host_to_be16(proto);

	msg_len = 6 + sizeof(eth) + data_len;
	msg = os_malloc(msg_len);
	if (msg == NULL)
		return -1;
	os_memcpy(msg, "EAPOL ", 6);
	os_memcpy(msg + 6, &eth, sizeof(eth));
	os_memcpy(msg + 6 + sizeof(eth), data, data_len);

	if (os_memcmp(dest, dbss->bssid, ETH_ALEN) == 0 ||
	    drv->test_dir == NULL) {
		if (drv->hostapd_addr_udp_set) {
			addr = (struct sockaddr *) &drv->hostapd_addr_udp;
			alen = sizeof(drv->hostapd_addr_udp);
		} else {
#ifdef DRIVER_TEST_UNIX
			addr = (struct sockaddr *) &drv->hostapd_addr;
			alen = sizeof(drv->hostapd_addr);
#else /* DRIVER_TEST_UNIX */
			os_free(msg);
			return -1;
#endif /* DRIVER_TEST_UNIX */
		}
	} else {
#ifdef DRIVER_TEST_UNIX
		struct stat st;
		os_memset(&addr_un, 0, sizeof(addr_un));
		addr_un.sun_family = AF_UNIX;
		os_snprintf(addr_un.sun_path, sizeof(addr_un.sun_path),
			    "%s/STA-" MACSTR, drv->test_dir, MAC2STR(dest));
		if (stat(addr_un.sun_path, &st) < 0) {
			os_snprintf(addr_un.sun_path, sizeof(addr_un.sun_path),
				    "%s/AP-" MACSTR,
				    drv->test_dir, MAC2STR(dest));
		}
		addr = (struct sockaddr *) &addr_un;
		alen = sizeof(addr_un);
#else /* DRIVER_TEST_UNIX */
		os_free(msg);
		return -1;
#endif /* DRIVER_TEST_UNIX */
	}

	if (sendto(drv->test_socket, msg, msg_len, 0, addr, alen) < 0) {
		perror("sendmsg(test_socket)");
		os_free(msg);
		return -1;
	}

	os_free(msg);
	return 0;
}


static int wpa_driver_test_get_capa(void *priv, struct wpa_driver_capa *capa)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	os_memset(capa, 0, sizeof(*capa));
	capa->key_mgmt = WPA_DRIVER_CAPA_KEY_MGMT_WPA |
		WPA_DRIVER_CAPA_KEY_MGMT_WPA2 |
		WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK |
		WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK |
		WPA_DRIVER_CAPA_KEY_MGMT_WPA_NONE |
		WPA_DRIVER_CAPA_KEY_MGMT_FT |
		WPA_DRIVER_CAPA_KEY_MGMT_FT_PSK;
	capa->enc = WPA_DRIVER_CAPA_ENC_WEP40 |
		WPA_DRIVER_CAPA_ENC_WEP104 |
		WPA_DRIVER_CAPA_ENC_TKIP |
		WPA_DRIVER_CAPA_ENC_CCMP;
	capa->auth = WPA_DRIVER_AUTH_OPEN |
		WPA_DRIVER_AUTH_SHARED |
		WPA_DRIVER_AUTH_LEAP;
	if (drv->use_mlme)
		capa->flags |= WPA_DRIVER_FLAGS_USER_SPACE_MLME;
	if (drv->p2p)
		capa->flags |= WPA_DRIVER_FLAGS_P2P_MGMT;
	capa->flags |= WPA_DRIVER_FLAGS_AP;
	capa->flags |= WPA_DRIVER_FLAGS_P2P_CONCURRENT;
	capa->flags |= WPA_DRIVER_FLAGS_P2P_DEDICATED_INTERFACE;
	capa->flags |= WPA_DRIVER_FLAGS_P2P_CAPABLE;
	capa->max_scan_ssids = 2;
	capa->max_remain_on_chan = 60000;

	return 0;
}


static int wpa_driver_test_mlme_setprotection(void *priv, const u8 *addr,
					      int protect_type,
					      int key_type)
{
	wpa_printf(MSG_DEBUG, "%s: protect_type=%d key_type=%d",
		   __func__, protect_type, key_type);

	if (addr) {
		wpa_printf(MSG_DEBUG, "%s: addr=" MACSTR,
			   __func__, MAC2STR(addr));
	}

	return 0;
}


static int wpa_driver_test_set_channel(void *priv,
				       enum hostapd_hw_mode phymode,
				       int chan, int freq)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	wpa_printf(MSG_DEBUG, "%s: phymode=%d chan=%d freq=%d",
		   __func__, phymode, chan, freq);
	drv->current_freq = freq;
	return 0;
}


static int wpa_driver_test_mlme_add_sta(void *priv, const u8 *addr,
					const u8 *supp_rates,
					size_t supp_rates_len)
{
	wpa_printf(MSG_DEBUG, "%s: addr=" MACSTR, __func__, MAC2STR(addr));
	return 0;
}


static int wpa_driver_test_mlme_remove_sta(void *priv, const u8 *addr)
{
	wpa_printf(MSG_DEBUG, "%s: addr=" MACSTR, __func__, MAC2STR(addr));
	return 0;
}


static int wpa_driver_test_set_ssid(void *priv, const u8 *ssid,
				    size_t ssid_len)
{
	wpa_printf(MSG_DEBUG, "%s", __func__);
	return 0;
}


static int wpa_driver_test_set_bssid(void *priv, const u8 *bssid)
{
	wpa_printf(MSG_DEBUG, "%s: bssid=" MACSTR, __func__, MAC2STR(bssid));
	return 0;
}


static void * wpa_driver_test_global_init(void)
{
	struct wpa_driver_test_global *global;

	global = os_zalloc(sizeof(*global));
	return global;
}


static void wpa_driver_test_global_deinit(void *priv)
{
	struct wpa_driver_test_global *global = priv;
	os_free(global);
}


static struct wpa_interface_info *
wpa_driver_test_get_interfaces(void *global_priv)
{
	/* struct wpa_driver_test_global *global = priv; */
	struct wpa_interface_info *iface;

	iface = os_zalloc(sizeof(*iface));
	if (iface == NULL)
		return iface;
	iface->ifname = os_strdup("sta0");
	iface->desc = os_strdup("test interface 0");
	iface->drv_name = "test";
	iface->next = os_zalloc(sizeof(*iface));
	if (iface->next) {
		iface->next->ifname = os_strdup("sta1");
		iface->next->desc = os_strdup("test interface 1");
		iface->next->drv_name = "test";
	}

	return iface;
}


static struct hostapd_hw_modes *
wpa_driver_test_get_hw_feature_data(void *priv, u16 *num_modes, u16 *flags)
{
	struct hostapd_hw_modes *modes;
	size_t i;

	*num_modes = 3;
	*flags = 0;
	modes = os_zalloc(*num_modes * sizeof(struct hostapd_hw_modes));
	if (modes == NULL)
		return NULL;
	modes[0].mode = HOSTAPD_MODE_IEEE80211G;
	modes[0].num_channels = 11;
	modes[0].num_rates = 12;
	modes[0].channels =
		os_zalloc(11 * sizeof(struct hostapd_channel_data));
	modes[0].rates = os_zalloc(modes[0].num_rates * sizeof(int));
	if (modes[0].channels == NULL || modes[0].rates == NULL)
		goto fail;
	for (i = 0; i < 11; i++) {
		modes[0].channels[i].chan = i + 1;
		modes[0].channels[i].freq = 2412 + 5 * i;
		modes[0].channels[i].flag = 0;
	}
	modes[0].rates[0] = 10;
	modes[0].rates[1] = 20;
	modes[0].rates[2] = 55;
	modes[0].rates[3] = 110;
	modes[0].rates[4] = 60;
	modes[0].rates[5] = 90;
	modes[0].rates[6] = 120;
	modes[0].rates[7] = 180;
	modes[0].rates[8] = 240;
	modes[0].rates[9] = 360;
	modes[0].rates[10] = 480;
	modes[0].rates[11] = 540;

	modes[1].mode = HOSTAPD_MODE_IEEE80211B;
	modes[1].num_channels = 11;
	modes[1].num_rates = 4;
	modes[1].channels =
		os_zalloc(11 * sizeof(struct hostapd_channel_data));
	modes[1].rates = os_zalloc(modes[1].num_rates * sizeof(int));
	if (modes[1].channels == NULL || modes[1].rates == NULL)
		goto fail;
	for (i = 0; i < 11; i++) {
		modes[1].channels[i].chan = i + 1;
		modes[1].channels[i].freq = 2412 + 5 * i;
		modes[1].channels[i].flag = 0;
	}
	modes[1].rates[0] = 10;
	modes[1].rates[1] = 20;
	modes[1].rates[2] = 55;
	modes[1].rates[3] = 110;

	modes[2].mode = HOSTAPD_MODE_IEEE80211A;
	modes[2].num_channels = 1;
	modes[2].num_rates = 8;
	modes[2].channels = os_zalloc(sizeof(struct hostapd_channel_data));
	modes[2].rates = os_zalloc(modes[2].num_rates * sizeof(int));
	if (modes[2].channels == NULL || modes[2].rates == NULL)
		goto fail;
	modes[2].channels[0].chan = 60;
	modes[2].channels[0].freq = 5300;
	modes[2].channels[0].flag = 0;
	modes[2].rates[0] = 60;
	modes[2].rates[1] = 90;
	modes[2].rates[2] = 120;
	modes[2].rates[3] = 180;
	modes[2].rates[4] = 240;
	modes[2].rates[5] = 360;
	modes[2].rates[6] = 480;
	modes[2].rates[7] = 540;

	return modes;

fail:
	if (modes) {
		for (i = 0; i < *num_modes; i++) {
			os_free(modes[i].channels);
			os_free(modes[i].rates);
		}
		os_free(modes);
	}
	return NULL;
}


static int wpa_driver_test_set_freq(void *priv,
				    struct hostapd_freq_params *freq)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	wpa_printf(MSG_DEBUG, "test: set_freq %u MHz", freq->freq);
	drv->current_freq = freq->freq;
	return 0;
}


static int wpa_driver_test_send_action(void *priv, unsigned int freq,
				       unsigned int wait,
				       const u8 *dst, const u8 *src,
				       const u8 *bssid,
				       const u8 *data, size_t data_len)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	int ret = -1;
	u8 *buf;
	struct ieee80211_hdr *hdr;

	wpa_printf(MSG_DEBUG, "test: Send Action frame");

	if ((drv->remain_on_channel_freq &&
	     freq != drv->remain_on_channel_freq) ||
	    (drv->remain_on_channel_freq == 0 &&
	     freq != (unsigned int) drv->current_freq)) {
		wpa_printf(MSG_DEBUG, "test: Reject Action frame TX on "
			   "unexpected channel: freq=%u MHz (current_freq=%u "
			   "MHz, remain-on-channel freq=%u MHz)",
			   freq, drv->current_freq,
			   drv->remain_on_channel_freq);
		return -1;
	}

	buf = os_zalloc(24 + data_len);
	if (buf == NULL)
		return ret;
	os_memcpy(buf + 24, data, data_len);
	hdr = (struct ieee80211_hdr *) buf;
	hdr->frame_control =
		IEEE80211_FC(WLAN_FC_TYPE_MGMT, WLAN_FC_STYPE_ACTION);
	os_memcpy(hdr->addr1, dst, ETH_ALEN);
	os_memcpy(hdr->addr2, src, ETH_ALEN);
	os_memcpy(hdr->addr3, bssid, ETH_ALEN);

	ret = wpa_driver_test_send_mlme(priv, buf, 24 + data_len);
	os_free(buf);
	return ret;
}


#ifdef CONFIG_P2P
static void test_send_action_cb(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_driver_test_data *drv = eloop_ctx;
	int res;

	if (drv->pending_action_tx == NULL)
		return;

	if (drv->off_channel_freq != drv->pending_action_freq) {
		wpa_printf(MSG_DEBUG, "P2P: Pending Action frame TX "
			   "waiting for another freq=%u",
			   drv->pending_action_freq);
		return;
	}
	wpa_printf(MSG_DEBUG, "P2P: Sending pending Action frame to "
		   MACSTR, MAC2STR(drv->pending_action_dst));
	res = wpa_driver_test_send_action(drv, drv->pending_action_freq, 0,
					  drv->pending_action_dst,
					  drv->pending_action_src,
					  drv->pending_action_bssid,
					  wpabuf_head(drv->pending_action_tx),
					  wpabuf_len(drv->pending_action_tx));
}
#endif /* CONFIG_P2P */


static void test_remain_on_channel_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_driver_test_data *drv = eloop_ctx;
	union wpa_event_data data;

	wpa_printf(MSG_DEBUG, "test: Remain-on-channel timeout");

	os_memset(&data, 0, sizeof(data));
	data.remain_on_channel.freq = drv->remain_on_channel_freq;
	data.remain_on_channel.duration = drv->remain_on_channel_duration;

	if (drv->p2p)
		drv->off_channel_freq = 0;

	drv->remain_on_channel_freq = 0;

	wpa_supplicant_event(drv->ctx, EVENT_CANCEL_REMAIN_ON_CHANNEL, &data);
}


static int wpa_driver_test_remain_on_channel(void *priv, unsigned int freq,
					     unsigned int duration)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	union wpa_event_data data;

	wpa_printf(MSG_DEBUG, "%s(freq=%u, duration=%u)",
		   __func__, freq, duration);
	if (drv->remain_on_channel_freq &&
	    drv->remain_on_channel_freq != freq) {
		wpa_printf(MSG_DEBUG, "test: Refuse concurrent "
			   "remain_on_channel request");
		return -1;
	}

	drv->remain_on_channel_freq = freq;
	drv->remain_on_channel_duration = duration;
	eloop_cancel_timeout(test_remain_on_channel_timeout, drv, NULL);
	eloop_register_timeout(duration / 1000, (duration % 1000) * 1000,
			       test_remain_on_channel_timeout, drv, NULL);

	os_memset(&data, 0, sizeof(data));
	data.remain_on_channel.freq = freq;
	data.remain_on_channel.duration = duration;
	wpa_supplicant_event(drv->ctx, EVENT_REMAIN_ON_CHANNEL, &data);

#ifdef CONFIG_P2P
	if (drv->p2p) {
		drv->off_channel_freq = drv->remain_on_channel_freq;
		test_send_action_cb(drv, NULL);
		if (drv->off_channel_freq == drv->pending_listen_freq) {
			p2p_listen_cb(drv->p2p, drv->pending_listen_freq,
				      drv->pending_listen_duration);
			drv->pending_listen_freq = 0;
		}
	}
#endif /* CONFIG_P2P */

	return 0;
}


static int wpa_driver_test_cancel_remain_on_channel(void *priv)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	wpa_printf(MSG_DEBUG, "%s", __func__);
	if (!drv->remain_on_channel_freq)
		return -1;
	drv->remain_on_channel_freq = 0;
	eloop_cancel_timeout(test_remain_on_channel_timeout, drv, NULL);
	return 0;
}


static int wpa_driver_test_probe_req_report(void *priv, int report)
{
	struct test_driver_bss *dbss = priv;
	struct wpa_driver_test_data *drv = dbss->drv;
	wpa_printf(MSG_DEBUG, "%s(report=%d)", __func__, report);
	drv->probe_req_report = report;
	return 0;
}


#ifdef CONFIG_P2P

static int wpa_driver_test_p2p_find(void *priv, unsigned int timeout, int type)
{
	struct wpa_driver_test_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s(timeout=%u)", __func__, timeout);
	if (!drv->p2p)
		return -1;
	return p2p_find(drv->p2p, timeout, type, 0, NULL);
}


static int wpa_driver_test_p2p_stop_find(void *priv)
{
	struct wpa_driver_test_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s", __func__);
	if (!drv->p2p)
		return -1;
	p2p_stop_find(drv->p2p);
	return 0;
}


static int wpa_driver_test_p2p_listen(void *priv, unsigned int timeout)
{
	struct wpa_driver_test_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s(timeout=%u)", __func__, timeout);
	if (!drv->p2p)
		return -1;
	return p2p_listen(drv->p2p, timeout);
}


static int wpa_driver_test_p2p_connect(void *priv, const u8 *peer_addr,
				       int wps_method, int go_intent,
				       const u8 *own_interface_addr,
				       unsigned int force_freq,
				       int persistent_group)
{
	struct wpa_driver_test_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s(peer_addr=" MACSTR " wps_method=%d "
		   "go_intent=%d "
		   "own_interface_addr=" MACSTR " force_freq=%u "
		   "persistent_group=%d)",
		   __func__, MAC2STR(peer_addr), wps_method, go_intent,
		   MAC2STR(own_interface_addr), force_freq, persistent_group);
	if (!drv->p2p)
		return -1;
	return p2p_connect(drv->p2p, peer_addr, wps_method, go_intent,
			   own_interface_addr, force_freq, persistent_group);
}


static int wpa_driver_test_wps_success_cb(void *priv, const u8 *peer_addr)
{
	struct wpa_driver_test_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s(peer_addr=" MACSTR ")",
		   __func__, MAC2STR(peer_addr));
	if (!drv->p2p)
		return -1;
	p2p_wps_success_cb(drv->p2p, peer_addr);
	return 0;
}


static int wpa_driver_test_p2p_group_formation_failed(void *priv)
{
	struct wpa_driver_test_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s", __func__);
	if (!drv->p2p)
		return -1;
	p2p_group_formation_failed(drv->p2p);
	return 0;
}


static int wpa_driver_test_p2p_set_params(void *priv,
					  const struct p2p_params *params)
{
	struct wpa_driver_test_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s", __func__);
	if (!drv->p2p)
		return -1;
	if (p2p_set_dev_name(drv->p2p, params->dev_name) < 0 ||
	    p2p_set_pri_dev_type(drv->p2p, params->pri_dev_type) < 0 ||
	    p2p_set_sec_dev_types(drv->p2p, params->sec_dev_type,
				  params->num_sec_dev_types) < 0)
		return -1;
	return 0;
}


static int test_p2p_scan(void *ctx, enum p2p_scan_type type, int freq,
			 unsigned int num_req_dev_types,
			 const u8 *req_dev_types)
{
	struct wpa_driver_test_data *drv = ctx;
	struct wpa_driver_scan_params params;
	int ret;
	struct wpabuf *wps_ie, *ies;
	int social_channels[] = { 2412, 2437, 2462, 0, 0 };

	wpa_printf(MSG_DEBUG, "%s(type=%d freq=%d)",
		   __func__, type, freq);

	os_memset(&params, 0, sizeof(params));

	/* P2P Wildcard SSID */
	params.num_ssids = 1;
	params.ssids[0].ssid = (u8 *) P2P_WILDCARD_SSID;
	params.ssids[0].ssid_len = P2P_WILDCARD_SSID_LEN;

#if 0 /* TODO: WPS IE */
	wpa_s->wps->dev.p2p = 1;
	wps_ie = wps_build_probe_req_ie(0, &wpa_s->wps->dev, wpa_s->wps->uuid,
					WPS_REQ_ENROLLEE);
#else
	wps_ie = wpabuf_alloc(1);
#endif
	if (wps_ie == NULL)
		return -1;

	ies = wpabuf_alloc(wpabuf_len(wps_ie) + 100);
	if (ies == NULL) {
		wpabuf_free(wps_ie);
		return -1;
	}
	wpabuf_put_buf(ies, wps_ie);
	wpabuf_free(wps_ie);

	p2p_scan_ie(drv->p2p, ies);

	params.extra_ies = wpabuf_head(ies);
	params.extra_ies_len = wpabuf_len(ies);

	switch (type) {
	case P2P_SCAN_SOCIAL:
		params.freqs = social_channels;
		break;
	case P2P_SCAN_FULL:
		break;
	case P2P_SCAN_SPECIFIC:
		social_channels[0] = freq;
		social_channels[1] = 0;
		params.freqs = social_channels;
		break;
	case P2P_SCAN_SOCIAL_PLUS_ONE:
		social_channels[3] = freq;
		params.freqs = social_channels;
		break;
	}

	drv->pending_p2p_scan = 1;
	ret = wpa_driver_test_scan(drv, &params);

	wpabuf_free(ies);

	return ret;
}


static int test_send_action(void *ctx, unsigned int freq, const u8 *dst,
			    const u8 *src, const u8 *bssid, const u8 *buf,
			    size_t len, unsigned int wait_time)
{
	struct wpa_driver_test_data *drv = ctx;

	wpa_printf(MSG_DEBUG, "%s(freq=%u dst=" MACSTR " src=" MACSTR
		   " bssid=" MACSTR " len=%d",
		   __func__, freq, MAC2STR(dst), MAC2STR(src), MAC2STR(bssid),
		   (int) len);
	if (freq <= 0) {
		wpa_printf(MSG_WARNING, "P2P: No frequency specified for "
			   "action frame TX");
		return -1;
	}

	if (drv->pending_action_tx) {
		wpa_printf(MSG_DEBUG, "P2P: Dropped pending Action frame TX "
			   "to " MACSTR, MAC2STR(drv->pending_action_dst));
		wpabuf_free(drv->pending_action_tx);
	}
	drv->pending_action_tx = wpabuf_alloc(len);
	if (drv->pending_action_tx == NULL)
		return -1;
	wpabuf_put_data(drv->pending_action_tx, buf, len);
	os_memcpy(drv->pending_action_src, src, ETH_ALEN);
	os_memcpy(drv->pending_action_dst, dst, ETH_ALEN);
	os_memcpy(drv->pending_action_bssid, bssid, ETH_ALEN);
	drv->pending_action_freq = freq;

	if (drv->off_channel_freq == freq) {
		/* Already on requested channel; send immediately */
		/* TODO: Would there ever be need to extend the current
		 * duration on the channel? */
		eloop_cancel_timeout(test_send_action_cb, drv, NULL);
		eloop_register_timeout(0, 0, test_send_action_cb, drv, NULL);
		return 0;
	}

	wpa_printf(MSG_DEBUG, "P2P: Schedule Action frame to be transmitted "
		   "once the driver gets to the requested channel");
	if (wpa_driver_test_remain_on_channel(drv, freq, wait_time) < 0) {
		wpa_printf(MSG_DEBUG, "P2P: Failed to request driver "
			   "to remain on channel (%u MHz) for Action "
			   "Frame TX", freq);
		return -1;
	}

	return 0;
}


static void test_send_action_done(void *ctx)
{
	wpa_printf(MSG_DEBUG, "%s", __func__);
	/* TODO */
}


static void test_go_neg_completed(void *ctx, struct p2p_go_neg_results *res)
{
	struct wpa_driver_test_data *drv = ctx;
	union wpa_event_data event;
	wpa_printf(MSG_DEBUG, "%s", __func__);
	os_memset(&event, 0, sizeof(event));
	event.p2p_go_neg_completed.res = res;
	wpa_supplicant_event(drv->ctx, EVENT_P2P_GO_NEG_COMPLETED, &event);
}


static void test_go_neg_req_rx(void *ctx, const u8 *src, u16 dev_passwd_id)
{
	struct wpa_driver_test_data *drv = ctx;
	union wpa_event_data event;
	wpa_printf(MSG_DEBUG, "%s(src=" MACSTR ")", __func__, MAC2STR(src));
	os_memset(&event, 0, sizeof(event));
	event.p2p_go_neg_req_rx.src = src;
	event.p2p_go_neg_req_rx.dev_passwd_id = dev_passwd_id;
	wpa_supplicant_event(drv->ctx, EVENT_P2P_GO_NEG_REQ_RX, &event);
}


static void test_dev_found(void *ctx, const u8 *addr,
			   const struct p2p_peer_info *info, int new_device)
{
	struct wpa_driver_test_data *drv = ctx;
	union wpa_event_data event;
	char devtype[WPS_DEV_TYPE_BUFSIZE];
	wpa_printf(MSG_DEBUG, "%s(" MACSTR " p2p_dev_addr=" MACSTR
		   " pri_dev_type=%s name='%s' config_methods=0x%x "
		   "dev_capab=0x%x group_capab=0x%x)",
		   __func__, MAC2STR(addr), MAC2STR(info->p2p_device_addr),
		   wps_dev_type_bin2str(info->pri_dev_type, devtype,
					sizeof(devtype)),
		   info->device_name, info->config_methods, info->dev_capab,
		   info->group_capab);

	os_memset(&event, 0, sizeof(event));
	event.p2p_dev_found.addr = addr;
	event.p2p_dev_found.dev_addr = info->p2p_device_addr;
	event.p2p_dev_found.pri_dev_type = info->pri_dev_type;
	event.p2p_dev_found.dev_name = info->device_name;
	event.p2p_dev_found.config_methods = info->config_methods;
	event.p2p_dev_found.dev_capab = info->dev_capab;
	event.p2p_dev_found.group_capab = info->group_capab;
	wpa_supplicant_event(drv->ctx, EVENT_P2P_DEV_FOUND, &event);
}


static int test_start_listen(void *ctx, unsigned int freq,
			     unsigned int duration,
			     const struct wpabuf *probe_resp_ie)
{
	struct wpa_driver_test_data *drv = ctx;

	wpa_printf(MSG_DEBUG, "%s(freq=%u duration=%u)",
		   __func__, freq, duration);

	if (wpa_driver_test_probe_req_report(drv, 1) < 0)
		return -1;

	drv->pending_listen_freq = freq;
	drv->pending_listen_duration = duration;

	if (wpa_driver_test_remain_on_channel(drv, freq, duration) < 0) {
		drv->pending_listen_freq = 0;
		return -1;
	}

	return 0;
}


static void test_stop_listen(void *ctx)
{
	wpa_printf(MSG_DEBUG, "%s", __func__);
	/* TODO */
}


static int test_send_probe_resp(void *ctx, const struct wpabuf *buf)
{
	struct wpa_driver_test_data *drv = ctx;
	char resp[512], *pos, *end;
	int ret;
	const struct ieee80211_mgmt *mgmt;
	const u8 *ie, *ie_end;

	wpa_printf(MSG_DEBUG, "%s", __func__);
	wpa_hexdump_buf(MSG_MSGDUMP, "Probe Response", buf);
	if (wpabuf_len(buf) < 24)
		return -1;
	if (!drv->probe_from) {
		wpa_printf(MSG_DEBUG, "%s: probe_from not set", __func__);
		return -1;
	}

	pos = resp;
	end = resp + sizeof(resp);

	mgmt = wpabuf_head(buf);

	/* reply: SCANRESP BSSID SSID IEs */
	ret = os_snprintf(pos, end - pos, "SCANRESP " MACSTR " ",
			  MAC2STR(mgmt->bssid));
	if (ret < 0 || ret >= end - pos)
		return -1;
	pos += ret;

	ie = mgmt->u.probe_resp.variable;
	ie_end = wpabuf_head_u8(buf) + wpabuf_len(buf);
	if (ie_end - ie < 2 || ie[0] != WLAN_EID_SSID ||
	    ie + 2 + ie[1] > ie_end)
		return -1;
	pos += wpa_snprintf_hex(pos, end - pos, ie + 2, ie[1]);

	ret = os_snprintf(pos, end - pos, " ");
	if (ret < 0 || ret >= end - pos)
		return -1;
	pos += ret;
	pos += wpa_snprintf_hex(pos, end - pos, ie, ie_end - ie);

	sendto(drv->test_socket, resp, pos - resp, 0,
	       drv->probe_from, drv->probe_from_len);

	return 0;
}


static void test_sd_request(void *ctx, int freq, const u8 *sa, u8 dialog_token,
			    u16 update_indic, const u8 *tlvs, size_t tlvs_len)
{
	wpa_printf(MSG_DEBUG, "%s", __func__);
	/* TODO */
}


static void test_sd_response(void *ctx, const u8 *sa, u16 update_indic,
			     const u8 *tlvs, size_t tlvs_len)
{
	wpa_printf(MSG_DEBUG, "%s", __func__);
	/* TODO */
}


static void test_prov_disc_req(void *ctx, const u8 *peer, u16 config_methods,
			       const u8 *dev_addr, const u8 *pri_dev_type,
			       const char *dev_name, u16 supp_config_methods,
			       u8 dev_capab, u8 group_capab)
{
	wpa_printf(MSG_DEBUG, "%s(peer=" MACSTR " config_methods=0x%x)",
		   __func__, MAC2STR(peer), config_methods);
	/* TODO */
}


static void test_prov_disc_resp(void *ctx, const u8 *peer, u16 config_methods)
{
	wpa_printf(MSG_DEBUG, "%s(peer=" MACSTR " config_methods=0x%x)",
		   __func__, MAC2STR(peer), config_methods);
	/* TODO */
}

#endif /* CONFIG_P2P */


static int wpa_driver_test_init_p2p(struct wpa_driver_test_data *drv)
{
#ifdef CONFIG_P2P
	struct p2p_config p2p;
	unsigned int r;
	int i;

	os_memset(&p2p, 0, sizeof(p2p));
	p2p.msg_ctx = drv->ctx;
	p2p.cb_ctx = drv;
	p2p.p2p_scan = test_p2p_scan;
	p2p.send_action = test_send_action;
	p2p.send_action_done = test_send_action_done;
	p2p.go_neg_completed = test_go_neg_completed;
	p2p.go_neg_req_rx = test_go_neg_req_rx;
	p2p.dev_found = test_dev_found;
	p2p.start_listen = test_start_listen;
	p2p.stop_listen = test_stop_listen;
	p2p.send_probe_resp = test_send_probe_resp;
	p2p.sd_request = test_sd_request;
	p2p.sd_response = test_sd_response;
	p2p.prov_disc_req = test_prov_disc_req;
	p2p.prov_disc_resp = test_prov_disc_resp;

	os_memcpy(p2p.dev_addr, drv->own_addr, ETH_ALEN);

	p2p.reg_class = 12; /* TODO: change depending on location */
	/*
	 * Pick one of the social channels randomly as the listen
	 * channel.
	 */
	os_get_random((u8 *) &r, sizeof(r));
	p2p.channel = 1 + (r % 3) * 5;

	/* TODO: change depending on location */
	p2p.op_reg_class = 12;
	/*
	 * For initial tests, pick the operation channel randomly.
	 * TODO: Use scan results (etc.) to select the best channel.
	 */
	p2p.op_channel = 1 + r % 11;

	os_memcpy(p2p.country, "US ", 3);

	/* FIX: fetch available channels from the driver */
	p2p.channels.reg_classes = 1;
	p2p.channels.reg_class[0].reg_class = 12; /* US/12 = 2.4 GHz band */
	p2p.channels.reg_class[0].channels = 11;
	for (i = 0; i < 11; i++)
		p2p.channels.reg_class[0].channel[i] = i + 1;

	p2p.max_peers = 100;

	drv->p2p = p2p_init(&p2p);
	if (drv->p2p == NULL)
		return -1;
	return 0;
#else /* CONFIG_P2P */
	wpa_printf(MSG_INFO, "driver_test: P2P support not included");
	return -1;
#endif /* CONFIG_P2P */
}


const struct wpa_driver_ops wpa_driver_test_ops = {
	"test",
	"wpa_supplicant test driver",
	.hapd_init = test_driver_init,
	.hapd_deinit = wpa_driver_test_deinit,
	.hapd_send_eapol = test_driver_send_eapol,
	.send_mlme = wpa_driver_test_send_mlme,
	.set_generic_elem = test_driver_set_generic_elem,
	.sta_deauth = test_driver_sta_deauth,
	.sta_disassoc = test_driver_sta_disassoc,
	.get_hw_feature_data = wpa_driver_test_get_hw_feature_data,
	.if_add = test_driver_if_add,
	.if_remove = test_driver_if_remove,
	.valid_bss_mask = test_driver_valid_bss_mask,
	.hapd_set_ssid = test_driver_set_ssid,
	.set_privacy = test_driver_set_privacy,
	.set_sta_vlan = test_driver_set_sta_vlan,
	.sta_add = test_driver_sta_add,
	.send_ether = test_driver_send_ether,
	.set_ap_wps_ie = test_driver_set_ap_wps_ie,
	.get_bssid = wpa_driver_test_get_bssid,
	.get_ssid = wpa_driver_test_get_ssid,
	.set_key = wpa_driver_test_set_key,
	.deinit = wpa_driver_test_deinit,
	.set_param = wpa_driver_test_set_param,
	.deauthenticate = wpa_driver_test_deauthenticate,
	.disassociate = wpa_driver_test_disassociate,
	.associate = wpa_driver_test_associate,
	.get_capa = wpa_driver_test_get_capa,
	.get_mac_addr = wpa_driver_test_get_mac_addr,
	.send_eapol = wpa_driver_test_send_eapol,
	.mlme_setprotection = wpa_driver_test_mlme_setprotection,
	.set_channel = wpa_driver_test_set_channel,
	.set_ssid = wpa_driver_test_set_ssid,
	.set_bssid = wpa_driver_test_set_bssid,
	.mlme_add_sta = wpa_driver_test_mlme_add_sta,
	.mlme_remove_sta = wpa_driver_test_mlme_remove_sta,
	.get_scan_results2 = wpa_driver_test_get_scan_results2,
	.global_init = wpa_driver_test_global_init,
	.global_deinit = wpa_driver_test_global_deinit,
	.init2 = wpa_driver_test_init2,
	.get_interfaces = wpa_driver_test_get_interfaces,
	.scan2 = wpa_driver_test_scan,
	.set_freq = wpa_driver_test_set_freq,
	.send_action = wpa_driver_test_send_action,
	.remain_on_channel = wpa_driver_test_remain_on_channel,
	.cancel_remain_on_channel = wpa_driver_test_cancel_remain_on_channel,
	.probe_req_report = wpa_driver_test_probe_req_report,
#ifdef CONFIG_P2P
	.p2p_find = wpa_driver_test_p2p_find,
	.p2p_stop_find = wpa_driver_test_p2p_stop_find,
	.p2p_listen = wpa_driver_test_p2p_listen,
	.p2p_connect = wpa_driver_test_p2p_connect,
	.wps_success_cb = wpa_driver_test_wps_success_cb,
	.p2p_group_formation_failed =
	wpa_driver_test_p2p_group_formation_failed,
	.p2p_set_params = wpa_driver_test_p2p_set_params,
#endif /* CONFIG_P2P */
};
