/*
 * WPA Supplicant / privileged helper program
 * Copyright (c) 2007-2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#ifdef __linux__
#include <fcntl.h>
#endif /* __linux__ */
#include <sys/un.h>
#include <sys/stat.h>

#include "common.h"
#include "eloop.h"
#include "common/version.h"
#include "drivers/driver.h"
#include "l2_packet/l2_packet.h"
#include "common/privsep_commands.h"
#include "common/ieee802_11_defs.h"

#define WPA_PRIV_MAX_L2 3

struct wpa_priv_interface {
	struct wpa_priv_interface *next;
	char *driver_name;
	char *ifname;
	char *sock_name;
	int fd;

	void *ctx;

	const struct wpa_driver_ops *driver;
	void *drv_priv;
	void *drv_global_priv;
	struct sockaddr_un drv_addr;
	socklen_t drv_addr_len;
	int wpas_registered;

	struct l2_packet_data *l2[WPA_PRIV_MAX_L2];
	struct sockaddr_un l2_addr[WPA_PRIV_MAX_L2];
	socklen_t l2_addr_len[WPA_PRIV_MAX_L2];
	struct wpa_priv_l2 {
		struct wpa_priv_interface *parent;
		int idx;
	} l2_ctx[WPA_PRIV_MAX_L2];
};

struct wpa_priv_global {
	struct wpa_priv_interface *interfaces;
};


static void wpa_priv_cmd_register(struct wpa_priv_interface *iface,
				  struct sockaddr_un *from, socklen_t fromlen)
{
	int i;

	if (iface->drv_priv) {
		wpa_printf(MSG_DEBUG, "Cleaning up forgotten driver instance");
		if (iface->driver->deinit)
			iface->driver->deinit(iface->drv_priv);
		iface->drv_priv = NULL;
		if (iface->drv_global_priv) {
			iface->driver->global_deinit(iface->drv_global_priv);
			iface->drv_global_priv = NULL;
		}
		iface->wpas_registered = 0;
	}

	for (i = 0; i < WPA_PRIV_MAX_L2; i++) {
		if (iface->l2[i]) {
			wpa_printf(MSG_DEBUG,
				   "Cleaning up forgotten l2_packet instance");
			l2_packet_deinit(iface->l2[i]);
			iface->l2[i] = NULL;
		}
	}

	if (iface->driver->init2) {
		if (iface->driver->global_init) {
			iface->drv_global_priv =
				iface->driver->global_init(iface->ctx);
			if (!iface->drv_global_priv) {
				wpa_printf(MSG_INFO,
					   "Failed to initialize driver global context");
				return;
			}
		} else {
			iface->drv_global_priv = NULL;
		}
		iface->drv_priv = iface->driver->init2(iface, iface->ifname,
						       iface->drv_global_priv);
	} else if (iface->driver->init) {
		iface->drv_priv = iface->driver->init(iface, iface->ifname);
	} else {
		return;
	}
	if (iface->drv_priv == NULL) {
		wpa_printf(MSG_DEBUG, "Failed to initialize driver wrapper");
		return;
	}

	wpa_printf(MSG_DEBUG, "Driver wrapper '%s' initialized for interface "
		   "'%s'", iface->driver_name, iface->ifname);

	os_memcpy(&iface->drv_addr, from, fromlen);
	iface->drv_addr_len = fromlen;
	iface->wpas_registered = 1;

	if (iface->driver->set_param &&
	    iface->driver->set_param(iface->drv_priv, NULL) < 0) {
		wpa_printf(MSG_ERROR, "Driver interface rejected param");
	}
}


static void wpa_priv_cmd_unregister(struct wpa_priv_interface *iface,
				    struct sockaddr_un *from)
{
	if (iface->drv_priv) {
		if (iface->driver->deinit)
			iface->driver->deinit(iface->drv_priv);
		iface->drv_priv = NULL;
		if (iface->drv_global_priv) {
			iface->driver->global_deinit(iface->drv_global_priv);
			iface->drv_global_priv = NULL;
		}
		iface->wpas_registered = 0;
	}
}


static void wpa_priv_cmd_scan(struct wpa_priv_interface *iface,
			      void *buf, size_t len)
{
	struct wpa_driver_scan_params params;
	struct privsep_cmd_scan *scan;
	unsigned int i;
	int freqs[PRIVSEP_MAX_SCAN_FREQS + 1];

	if (iface->drv_priv == NULL)
		return;

	if (len < sizeof(*scan)) {
		wpa_printf(MSG_DEBUG, "Invalid scan request");
		return;
	}

	scan = buf;

	os_memset(&params, 0, sizeof(params));
	if (scan->num_ssids > WPAS_MAX_SCAN_SSIDS) {
		wpa_printf(MSG_DEBUG, "Invalid scan request (num_ssids)");
		return;
	}
	params.num_ssids = scan->num_ssids;
	for (i = 0; i < scan->num_ssids; i++) {
		params.ssids[i].ssid = scan->ssids[i];
		params.ssids[i].ssid_len = scan->ssid_lens[i];
	}

	if (scan->num_freqs > PRIVSEP_MAX_SCAN_FREQS) {
		wpa_printf(MSG_DEBUG, "Invalid scan request (num_freqs)");
		return;
	}
	if (scan->num_freqs) {
		for (i = 0; i < scan->num_freqs; i++)
			freqs[i] = scan->freqs[i];
		freqs[i] = 0;
		params.freqs = freqs;
	}

	if (iface->driver->scan2)
		iface->driver->scan2(iface->drv_priv, &params);
}


static void wpa_priv_get_scan_results2(struct wpa_priv_interface *iface,
				       struct sockaddr_un *from,
				       socklen_t fromlen)
{
	struct wpa_scan_results *res;
	u8 *buf = NULL, *pos, *end;
	int val;
	size_t i;

	res = iface->driver->get_scan_results2(iface->drv_priv);
	if (res == NULL)
		goto fail;

	buf = os_malloc(60000);
	if (buf == NULL)
		goto fail;
	pos = buf;
	end = buf + 60000;
	val = res->num;
	os_memcpy(pos, &val, sizeof(int));
	pos += sizeof(int);

	for (i = 0; i < res->num; i++) {
		struct wpa_scan_res *r = res->res[i];
		val = sizeof(*r) + r->ie_len + r->beacon_ie_len;
		if (end - pos < (int) sizeof(int) + val)
			break;
		os_memcpy(pos, &val, sizeof(int));
		pos += sizeof(int);
		os_memcpy(pos, r, val);
		pos += val;
	}

	sendto(iface->fd, buf, pos - buf, 0, (struct sockaddr *) from, fromlen);

	os_free(buf);
	wpa_scan_results_free(res);
	return;

fail:
	os_free(buf);
	wpa_scan_results_free(res);
	sendto(iface->fd, "", 0, 0, (struct sockaddr *) from, fromlen);
}


static void wpa_priv_cmd_get_scan_results(struct wpa_priv_interface *iface,
					  struct sockaddr_un *from,
					  socklen_t fromlen)
{
	if (iface->drv_priv == NULL)
		return;

	if (iface->driver->get_scan_results2)
		wpa_priv_get_scan_results2(iface, from, fromlen);
	else
		sendto(iface->fd, "", 0, 0, (struct sockaddr *) from, fromlen);
}


static void wpa_priv_cmd_authenticate(struct wpa_priv_interface *iface,
				      void *buf, size_t len)
{
	struct wpa_driver_auth_params params;
	struct privsep_cmd_authenticate *auth;
	int res, i;

	if (iface->drv_priv == NULL || iface->driver->authenticate == NULL)
		return;

	if (len < sizeof(*auth)) {
		wpa_printf(MSG_DEBUG, "Invalid authentication request");
		return;
	}

	auth = buf;
	if (sizeof(*auth) + auth->ie_len + auth->auth_data_len > len) {
		wpa_printf(MSG_DEBUG, "Authentication request overflow");
		return;
	}

	os_memset(&params, 0, sizeof(params));
	params.freq = auth->freq;
	params.bssid = auth->bssid;
	params.ssid = auth->ssid;
	if (auth->ssid_len > SSID_MAX_LEN)
		return;
	params.ssid_len = auth->ssid_len;
	params.auth_alg = auth->auth_alg;
	for (i = 0; i < 4; i++) {
		if (auth->wep_key_len[i]) {
			params.wep_key[i] = auth->wep_key[i];
			params.wep_key_len[i] = auth->wep_key_len[i];
		}
	}
	params.wep_tx_keyidx = auth->wep_tx_keyidx;
	params.local_state_change = auth->local_state_change;
	params.p2p = auth->p2p;
	if (auth->ie_len) {
		params.ie = (u8 *) (auth + 1);
		params.ie_len = auth->ie_len;
	}
	if (auth->auth_data_len) {
		params.auth_data = ((u8 *) (auth + 1)) + auth->ie_len;
		params.auth_data_len = auth->auth_data_len;
	}

	res = iface->driver->authenticate(iface->drv_priv, &params);
	wpa_printf(MSG_DEBUG, "drv->authenticate: res=%d", res);
}


static void wpa_priv_cmd_associate(struct wpa_priv_interface *iface,
				   void *buf, size_t len)
{
	struct wpa_driver_associate_params params;
	struct privsep_cmd_associate *assoc;
	u8 *bssid;
	int res;

	if (iface->drv_priv == NULL || iface->driver->associate == NULL)
		return;

	if (len < sizeof(*assoc)) {
		wpa_printf(MSG_DEBUG, "Invalid association request");
		return;
	}

	assoc = buf;
	if (sizeof(*assoc) + assoc->wpa_ie_len > len) {
		wpa_printf(MSG_DEBUG, "Association request overflow");
		return;
	}

	os_memset(&params, 0, sizeof(params));
	bssid = assoc->bssid;
	if (bssid[0] | bssid[1] | bssid[2] | bssid[3] | bssid[4] | bssid[5])
		params.bssid = bssid;
	params.ssid = assoc->ssid;
	if (assoc->ssid_len > SSID_MAX_LEN)
		return;
	params.ssid_len = assoc->ssid_len;
	params.freq.mode = assoc->hwmode;
	params.freq.freq = assoc->freq;
	params.freq.channel = assoc->channel;
	if (assoc->wpa_ie_len) {
		params.wpa_ie = (u8 *) (assoc + 1);
		params.wpa_ie_len = assoc->wpa_ie_len;
	}
	params.pairwise_suite = assoc->pairwise_suite;
	params.group_suite = assoc->group_suite;
	params.key_mgmt_suite = assoc->key_mgmt_suite;
	params.auth_alg = assoc->auth_alg;
	params.mode = assoc->mode;

	res = iface->driver->associate(iface->drv_priv, &params);
	wpa_printf(MSG_DEBUG, "drv->associate: res=%d", res);
}


static void wpa_priv_cmd_get_bssid(struct wpa_priv_interface *iface,
				   struct sockaddr_un *from, socklen_t fromlen)
{
	u8 bssid[ETH_ALEN];

	if (iface->drv_priv == NULL)
		goto fail;

	if (iface->driver->get_bssid == NULL ||
	    iface->driver->get_bssid(iface->drv_priv, bssid) < 0)
		goto fail;

	sendto(iface->fd, bssid, ETH_ALEN, 0, (struct sockaddr *) from,
	       fromlen);
	return;

fail:
	sendto(iface->fd, "", 0, 0, (struct sockaddr *) from, fromlen);
}


static void wpa_priv_cmd_get_ssid(struct wpa_priv_interface *iface,
				  struct sockaddr_un *from, socklen_t fromlen)
{
	u8 ssid[sizeof(int) + SSID_MAX_LEN];
	int res;

	if (iface->drv_priv == NULL)
		goto fail;

	if (iface->driver->get_ssid == NULL)
		goto fail;

	os_memset(ssid, 0, sizeof(ssid));
	res = iface->driver->get_ssid(iface->drv_priv, &ssid[sizeof(int)]);
	if (res < 0 || res > SSID_MAX_LEN)
		goto fail;
	os_memcpy(ssid, &res, sizeof(int));

	sendto(iface->fd, ssid, sizeof(ssid), 0, (struct sockaddr *) from,
	       fromlen);
	return;

fail:
	sendto(iface->fd, "", 0, 0, (struct sockaddr *) from, fromlen);
}


static void wpa_priv_cmd_set_key(struct wpa_priv_interface *iface,
				 void *buf, size_t len)
{
	struct privsep_cmd_set_key *params;
	int res;

	if (iface->drv_priv == NULL || iface->driver->set_key == NULL)
		return;

	if (len != sizeof(*params)) {
		wpa_printf(MSG_DEBUG, "Invalid set_key request");
		return;
	}

	params = buf;

	res = iface->driver->set_key(iface->ifname, iface->drv_priv,
				     params->alg,
				     params->addr, params->key_idx,
				     params->set_tx,
				     params->seq_len ? params->seq : NULL,
				     params->seq_len,
				     params->key_len ? params->key : NULL,
				     params->key_len);
	wpa_printf(MSG_DEBUG, "drv->set_key: res=%d", res);
}


static void wpa_priv_cmd_get_capa(struct wpa_priv_interface *iface,
				  struct sockaddr_un *from, socklen_t fromlen)
{
	struct wpa_driver_capa capa;

	if (iface->drv_priv == NULL)
		goto fail;

	if (iface->driver->get_capa == NULL ||
	    iface->driver->get_capa(iface->drv_priv, &capa) < 0)
		goto fail;

	/* For now, no support for passing extended_capa pointers */
	capa.extended_capa = NULL;
	capa.extended_capa_mask = NULL;
	capa.extended_capa_len = 0;
	sendto(iface->fd, &capa, sizeof(capa), 0, (struct sockaddr *) from,
	       fromlen);
	return;

fail:
	sendto(iface->fd, "", 0, 0, (struct sockaddr *) from, fromlen);
}


static void wpa_priv_l2_rx(void *ctx, const u8 *src_addr, const u8 *buf,
			   size_t len)
{
	struct wpa_priv_l2 *l2_ctx = ctx;
	struct wpa_priv_interface *iface = l2_ctx->parent;
	struct msghdr msg;
	struct iovec io[2];

	io[0].iov_base = (u8 *) src_addr;
	io[0].iov_len = ETH_ALEN;
	io[1].iov_base = (u8 *) buf;
	io[1].iov_len = len;

	os_memset(&msg, 0, sizeof(msg));
	msg.msg_iov = io;
	msg.msg_iovlen = 2;
	msg.msg_name = &iface->l2_addr[l2_ctx->idx];
	msg.msg_namelen = iface->l2_addr_len[l2_ctx->idx];

	if (sendmsg(iface->fd, &msg, 0) < 0) {
		wpa_printf(MSG_ERROR, "sendmsg(l2 rx): %s", strerror(errno));
	}
}


static int wpa_priv_allowed_l2_proto(u16 proto)
{
	return proto == ETH_P_EAPOL || proto == ETH_P_RSN_PREAUTH ||
		proto == ETH_P_80211_ENCAP;
}


static void wpa_priv_cmd_l2_register(struct wpa_priv_interface *iface,
				     struct sockaddr_un *from,
				     socklen_t fromlen,
				     void *buf, size_t len)
{
	int *reg_cmd = buf;
	u8 own_addr[ETH_ALEN];
	int res;
	u16 proto;
	int idx;

	if (len != 2 * sizeof(int)) {
		wpa_printf(MSG_DEBUG, "Invalid l2_register length %lu",
			   (unsigned long) len);
		return;
	}

	proto = reg_cmd[0];
	if (!wpa_priv_allowed_l2_proto(proto)) {
		wpa_printf(MSG_DEBUG, "Refused l2_packet connection for "
			   "ethertype 0x%x", proto);
		return;
	}

	for (idx = 0; idx < WPA_PRIV_MAX_L2; idx++) {
		if (!iface->l2[idx])
			break;
	}
	if (idx == WPA_PRIV_MAX_L2) {
		wpa_printf(MSG_DEBUG, "No free l2_packet connection found");
		return;
	}

	os_memcpy(&iface->l2_addr[idx], from, fromlen);
	iface->l2_addr_len[idx] = fromlen;

	iface->l2_ctx[idx].idx = idx;
	iface->l2_ctx[idx].parent = iface;
	iface->l2[idx] = l2_packet_init(iface->ifname, NULL, proto,
					wpa_priv_l2_rx, &iface->l2_ctx[idx],
					reg_cmd[1]);
	if (!iface->l2[idx]) {
		wpa_printf(MSG_DEBUG, "Failed to initialize l2_packet "
			   "instance for protocol %d", proto);
		return;
	}

	if (l2_packet_get_own_addr(iface->l2[idx], own_addr) < 0) {
		wpa_printf(MSG_DEBUG, "Failed to get own address from "
			   "l2_packet");
		l2_packet_deinit(iface->l2[idx]);
		iface->l2[idx] = NULL;
		return;
	}

	res = sendto(iface->fd, own_addr, ETH_ALEN, 0,
		     (struct sockaddr *) from, fromlen);
	wpa_printf(MSG_DEBUG, "L2 registration[idx=%d]: res=%d", idx, res);
}


static void wpa_priv_cmd_l2_unregister(struct wpa_priv_interface *iface,
				       struct sockaddr_un *from,
				       socklen_t fromlen)
{
	int idx;

	for (idx = 0; idx < WPA_PRIV_MAX_L2; idx++) {
		if (iface->l2_addr_len[idx] == fromlen &&
		    os_memcmp(&iface->l2_addr[idx], from, fromlen) == 0)
			break;
	}
	if (idx == WPA_PRIV_MAX_L2) {
		wpa_printf(MSG_DEBUG,
			   "No registered l2_packet socket found for unregister request");
		return;
	}

	if (iface->l2[idx]) {
		l2_packet_deinit(iface->l2[idx]);
		iface->l2[idx] = NULL;
	}
}


static void wpa_priv_cmd_l2_notify_auth_start(struct wpa_priv_interface *iface,
					      struct sockaddr_un *from)
{
	int idx;

	for (idx = 0; idx < WPA_PRIV_MAX_L2; idx++) {
		if (iface->l2[idx])
			l2_packet_notify_auth_start(iface->l2[idx]);
	}
}


static void wpa_priv_cmd_l2_send(struct wpa_priv_interface *iface,
				 struct sockaddr_un *from, socklen_t fromlen,
				 void *buf, size_t len)
{
	u8 *dst_addr;
	u16 proto;
	int res;
	int idx;

	for (idx = 0; idx < WPA_PRIV_MAX_L2; idx++) {
		if (iface->l2_addr_len[idx] == fromlen &&
		    os_memcmp(&iface->l2_addr[idx], from, fromlen) == 0)
			break;
	}
	if (idx == WPA_PRIV_MAX_L2) {
		wpa_printf(MSG_DEBUG,
			   "No registered l2_packet socket found for send request");
		return;
	}

	if (iface->l2[idx] == NULL)
		return;

	if (len < ETH_ALEN + 2) {
		wpa_printf(MSG_DEBUG, "Too short L2 send packet (len=%lu)",
			   (unsigned long) len);
		return;
	}

	dst_addr = buf;
	os_memcpy(&proto, buf + ETH_ALEN, 2);

	if (!wpa_priv_allowed_l2_proto(proto)) {
		wpa_printf(MSG_DEBUG, "Refused l2_packet send for ethertype "
			   "0x%x", proto);
		return;
	}

	res = l2_packet_send(iface->l2[idx], dst_addr, proto,
			     buf + ETH_ALEN + 2, len - ETH_ALEN - 2);
	wpa_printf(MSG_DEBUG, "L2 send[idx=%d]: res=%d", idx, res);
}


static void wpa_priv_cmd_set_country(struct wpa_priv_interface *iface,
				     char *buf)
{
	if (iface->drv_priv == NULL || iface->driver->set_country == NULL ||
	    *buf == '\0')
		return;

	iface->driver->set_country(iface->drv_priv, buf);
}


static void wpa_priv_receive(int sock, void *eloop_ctx, void *sock_ctx)
{
	struct wpa_priv_interface *iface = eloop_ctx;
	char buf[2000], *pos;
	void *cmd_buf;
	size_t cmd_len;
	int res, cmd;
	struct sockaddr_un from;
	socklen_t fromlen = sizeof(from);

	res = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *) &from,
		       &fromlen);
	if (res < 0) {
		wpa_printf(MSG_ERROR, "recvfrom: %s", strerror(errno));
		return;
	}

	if (res < (int) sizeof(int)) {
		wpa_printf(MSG_DEBUG, "Too short command (len=%d)", res);
		return;
	}

	os_memcpy(&cmd, buf, sizeof(int));
	wpa_printf(MSG_DEBUG, "Command %d for interface %s",
		   cmd, iface->ifname);
	cmd_buf = &buf[sizeof(int)];
	cmd_len = res - sizeof(int);

	switch (cmd) {
	case PRIVSEP_CMD_REGISTER:
		wpa_priv_cmd_register(iface, &from, fromlen);
		break;
	case PRIVSEP_CMD_UNREGISTER:
		wpa_priv_cmd_unregister(iface, &from);
		break;
	case PRIVSEP_CMD_SCAN:
		wpa_priv_cmd_scan(iface, cmd_buf, cmd_len);
		break;
	case PRIVSEP_CMD_GET_SCAN_RESULTS:
		wpa_priv_cmd_get_scan_results(iface, &from, fromlen);
		break;
	case PRIVSEP_CMD_ASSOCIATE:
		wpa_priv_cmd_associate(iface, cmd_buf, cmd_len);
		break;
	case PRIVSEP_CMD_GET_BSSID:
		wpa_priv_cmd_get_bssid(iface, &from, fromlen);
		break;
	case PRIVSEP_CMD_GET_SSID:
		wpa_priv_cmd_get_ssid(iface, &from, fromlen);
		break;
	case PRIVSEP_CMD_SET_KEY:
		wpa_priv_cmd_set_key(iface, cmd_buf, cmd_len);
		break;
	case PRIVSEP_CMD_GET_CAPA:
		wpa_priv_cmd_get_capa(iface, &from, fromlen);
		break;
	case PRIVSEP_CMD_L2_REGISTER:
		wpa_priv_cmd_l2_register(iface, &from, fromlen,
					 cmd_buf, cmd_len);
		break;
	case PRIVSEP_CMD_L2_UNREGISTER:
		wpa_priv_cmd_l2_unregister(iface, &from, fromlen);
		break;
	case PRIVSEP_CMD_L2_NOTIFY_AUTH_START:
		wpa_priv_cmd_l2_notify_auth_start(iface, &from);
		break;
	case PRIVSEP_CMD_L2_SEND:
		wpa_priv_cmd_l2_send(iface, &from, fromlen, cmd_buf, cmd_len);
		break;
	case PRIVSEP_CMD_SET_COUNTRY:
		pos = cmd_buf;
		if (pos + cmd_len >= buf + sizeof(buf))
			break;
		pos[cmd_len] = '\0';
		wpa_priv_cmd_set_country(iface, pos);
		break;
	case PRIVSEP_CMD_AUTHENTICATE:
		wpa_priv_cmd_authenticate(iface, cmd_buf, cmd_len);
		break;
	}
}


static void wpa_priv_interface_deinit(struct wpa_priv_interface *iface)
{
	int i;

	if (iface->drv_priv) {
		if (iface->driver->deinit)
			iface->driver->deinit(iface->drv_priv);
		if (iface->drv_global_priv)
			iface->driver->global_deinit(iface->drv_global_priv);
	}

	if (iface->fd >= 0) {
		eloop_unregister_read_sock(iface->fd);
		close(iface->fd);
		unlink(iface->sock_name);
	}

	for (i = 0; i < WPA_PRIV_MAX_L2; i++) {
		if (iface->l2[i])
			l2_packet_deinit(iface->l2[i]);
	}

	os_free(iface->ifname);
	os_free(iface->driver_name);
	os_free(iface->sock_name);
	os_free(iface);
}


static struct wpa_priv_interface *
wpa_priv_interface_init(void *ctx, const char *dir, const char *params)
{
	struct wpa_priv_interface *iface;
	char *pos;
	size_t len;
	struct sockaddr_un addr;
	int i;

	pos = os_strchr(params, ':');
	if (pos == NULL)
		return NULL;

	iface = os_zalloc(sizeof(*iface));
	if (iface == NULL)
		return NULL;
	iface->fd = -1;
	iface->ctx = ctx;

	len = pos - params;
	iface->driver_name = dup_binstr(params, len);
	if (iface->driver_name == NULL) {
		wpa_priv_interface_deinit(iface);
		return NULL;
	}

	for (i = 0; wpa_drivers[i]; i++) {
		if (os_strcmp(iface->driver_name,
			      wpa_drivers[i]->name) == 0) {
			iface->driver = wpa_drivers[i];
			break;
		}
	}
	if (iface->driver == NULL) {
		wpa_printf(MSG_ERROR, "Unsupported driver '%s'",
			   iface->driver_name);
		wpa_priv_interface_deinit(iface);
		return NULL;
	}

	pos++;
	iface->ifname = os_strdup(pos);
	if (iface->ifname == NULL) {
		wpa_priv_interface_deinit(iface);
		return NULL;
	}

	len = os_strlen(dir) + 1 + os_strlen(iface->ifname);
	iface->sock_name = os_malloc(len + 1);
	if (iface->sock_name == NULL) {
		wpa_priv_interface_deinit(iface);
		return NULL;
	}

	os_snprintf(iface->sock_name, len + 1, "%s/%s", dir, iface->ifname);
	if (os_strlen(iface->sock_name) >= sizeof(addr.sun_path)) {
		wpa_priv_interface_deinit(iface);
		return NULL;
	}

	iface->fd = socket(PF_UNIX, SOCK_DGRAM, 0);
	if (iface->fd < 0) {
		wpa_printf(MSG_ERROR, "socket(PF_UNIX): %s", strerror(errno));
		wpa_priv_interface_deinit(iface);
		return NULL;
	}

	os_memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	os_strlcpy(addr.sun_path, iface->sock_name, sizeof(addr.sun_path));

	if (bind(iface->fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		wpa_printf(MSG_DEBUG, "bind(PF_UNIX) failed: %s",
			   strerror(errno));
		if (connect(iface->fd, (struct sockaddr *) &addr,
			    sizeof(addr)) < 0) {
			wpa_printf(MSG_DEBUG, "Socket exists, but does not "
				   "allow connections - assuming it was "
				   "leftover from forced program termination");
			if (unlink(iface->sock_name) < 0) {
				wpa_printf(MSG_ERROR,
					   "Could not unlink existing ctrl_iface socket '%s': %s",
					   iface->sock_name, strerror(errno));
				goto fail;
			}
			if (bind(iface->fd, (struct sockaddr *) &addr,
				 sizeof(addr)) < 0) {
				wpa_printf(MSG_ERROR,
					   "wpa-priv-iface-init: bind(PF_UNIX): %s",
					   strerror(errno));
				goto fail;
			}
			wpa_printf(MSG_DEBUG, "Successfully replaced leftover "
				   "socket '%s'", iface->sock_name);
		} else {
			wpa_printf(MSG_INFO, "Socket exists and seems to be "
				   "in use - cannot override it");
			wpa_printf(MSG_INFO, "Delete '%s' manually if it is "
				   "not used anymore", iface->sock_name);
			goto fail;
		}
	}

	if (chmod(iface->sock_name, S_IRWXU | S_IRWXG | S_IRWXO) < 0) {
		wpa_printf(MSG_ERROR, "chmod: %s", strerror(errno));
		goto fail;
	}

	eloop_register_read_sock(iface->fd, wpa_priv_receive, iface, NULL);

	return iface;

fail:
	wpa_priv_interface_deinit(iface);
	return NULL;
}


static int wpa_priv_send_event(struct wpa_priv_interface *iface, int event,
			       const void *data, size_t data_len)
{
	struct msghdr msg;
	struct iovec io[2];

	io[0].iov_base = &event;
	io[0].iov_len = sizeof(event);
	io[1].iov_base = (u8 *) data;
	io[1].iov_len = data_len;

	os_memset(&msg, 0, sizeof(msg));
	msg.msg_iov = io;
	msg.msg_iovlen = data ? 2 : 1;
	msg.msg_name = &iface->drv_addr;
	msg.msg_namelen = iface->drv_addr_len;

	if (sendmsg(iface->fd, &msg, 0) < 0) {
		wpa_printf(MSG_ERROR, "sendmsg(wpas_socket): %s",
			   strerror(errno));
		return -1;
	}

	return 0;
}


static void wpa_priv_send_auth(struct wpa_priv_interface *iface,
			       union wpa_event_data *data)
{
	size_t buflen = sizeof(struct privsep_event_auth) + data->auth.ies_len;
	struct privsep_event_auth *auth;
	u8 *buf, *pos;

	buf = os_zalloc(buflen);
	if (buf == NULL)
		return;

	auth = (struct privsep_event_auth *) buf;
	pos = (u8 *) (auth + 1);

	os_memcpy(auth->peer, data->auth.peer, ETH_ALEN);
	os_memcpy(auth->bssid, data->auth.bssid, ETH_ALEN);
	auth->auth_type = data->auth.auth_type;
	auth->auth_transaction = data->auth.auth_transaction;
	auth->status_code = data->auth.status_code;
	if (data->auth.ies) {
		os_memcpy(pos, data->auth.ies, data->auth.ies_len);
		auth->ies_len = data->auth.ies_len;
	}

	wpa_priv_send_event(iface, PRIVSEP_EVENT_AUTH, buf, buflen);

	os_free(buf);
}


static void wpa_priv_send_assoc(struct wpa_priv_interface *iface, int event,
				union wpa_event_data *data)
{
	size_t buflen = 3 * sizeof(int);
	u8 *buf, *pos;
	int len;

	if (data) {
		buflen += data->assoc_info.req_ies_len +
			data->assoc_info.resp_ies_len +
			data->assoc_info.beacon_ies_len;
	}

	buf = os_malloc(buflen);
	if (buf == NULL)
		return;

	pos = buf;

	if (data && data->assoc_info.req_ies) {
		len = data->assoc_info.req_ies_len;
		os_memcpy(pos, &len, sizeof(int));
		pos += sizeof(int);
		os_memcpy(pos, data->assoc_info.req_ies, len);
		pos += len;
	} else {
		len = 0;
		os_memcpy(pos, &len, sizeof(int));
		pos += sizeof(int);
	}

	if (data && data->assoc_info.resp_ies) {
		len = data->assoc_info.resp_ies_len;
		os_memcpy(pos, &len, sizeof(int));
		pos += sizeof(int);
		os_memcpy(pos, data->assoc_info.resp_ies, len);
		pos += len;
	} else {
		len = 0;
		os_memcpy(pos, &len, sizeof(int));
		pos += sizeof(int);
	}

	if (data && data->assoc_info.beacon_ies) {
		len = data->assoc_info.beacon_ies_len;
		os_memcpy(pos, &len, sizeof(int));
		pos += sizeof(int);
		os_memcpy(pos, data->assoc_info.beacon_ies, len);
		pos += len;
	} else {
		len = 0;
		os_memcpy(pos, &len, sizeof(int));
		pos += sizeof(int);
	}

	wpa_priv_send_event(iface, event, buf, buflen);

	os_free(buf);
}


static void wpa_priv_send_interface_status(struct wpa_priv_interface *iface,
					   union wpa_event_data *data)
{
	int ievent;
	size_t len, maxlen;
	u8 *buf;
	char *ifname;

	if (data == NULL)
		return;

	ievent = data->interface_status.ievent;
	maxlen = sizeof(data->interface_status.ifname);
	ifname = data->interface_status.ifname;
	for (len = 0; len < maxlen && ifname[len]; len++)
		;

	buf = os_malloc(sizeof(int) + len);
	if (buf == NULL)
		return;

	os_memcpy(buf, &ievent, sizeof(int));
	os_memcpy(buf + sizeof(int), ifname, len);

	wpa_priv_send_event(iface, PRIVSEP_EVENT_INTERFACE_STATUS,
			    buf, sizeof(int) + len);

	os_free(buf);

}


static void wpa_priv_send_ft_response(struct wpa_priv_interface *iface,
				      union wpa_event_data *data)
{
	size_t len;
	u8 *buf, *pos;

	if (data == NULL || data->ft_ies.ies == NULL)
		return;

	len = sizeof(int) + ETH_ALEN + data->ft_ies.ies_len;
	buf = os_malloc(len);
	if (buf == NULL)
		return;

	pos = buf;
	os_memcpy(pos, &data->ft_ies.ft_action, sizeof(int));
	pos += sizeof(int);
	os_memcpy(pos, data->ft_ies.target_ap, ETH_ALEN);
	pos += ETH_ALEN;
	os_memcpy(pos, data->ft_ies.ies, data->ft_ies.ies_len);

	wpa_priv_send_event(iface, PRIVSEP_EVENT_FT_RESPONSE, buf, len);

	os_free(buf);

}


void wpa_supplicant_event(void *ctx, enum wpa_event_type event,
			  union wpa_event_data *data)
{
	struct wpa_priv_interface *iface = ctx;

	wpa_printf(MSG_DEBUG, "%s - event=%d", __func__, event);

	if (!iface->wpas_registered) {
		wpa_printf(MSG_DEBUG, "Driver event received, but "
			   "wpa_supplicant not registered");
		return;
	}

	switch (event) {
	case EVENT_ASSOC:
		wpa_priv_send_assoc(iface, PRIVSEP_EVENT_ASSOC, data);
		break;
	case EVENT_DISASSOC:
		wpa_priv_send_event(iface, PRIVSEP_EVENT_DISASSOC, NULL, 0);
		break;
	case EVENT_ASSOCINFO:
		if (data == NULL)
			return;
		wpa_priv_send_assoc(iface, PRIVSEP_EVENT_ASSOCINFO, data);
		break;
	case EVENT_MICHAEL_MIC_FAILURE:
		if (data == NULL)
			return;
		wpa_priv_send_event(iface, PRIVSEP_EVENT_MICHAEL_MIC_FAILURE,
				    &data->michael_mic_failure.unicast,
				    sizeof(int));
		break;
	case EVENT_SCAN_STARTED:
		wpa_priv_send_event(iface, PRIVSEP_EVENT_SCAN_STARTED, NULL,
				    0);
		break;
	case EVENT_SCAN_RESULTS:
		wpa_priv_send_event(iface, PRIVSEP_EVENT_SCAN_RESULTS, NULL,
				    0);
		break;
	case EVENT_INTERFACE_STATUS:
		wpa_priv_send_interface_status(iface, data);
		break;
	case EVENT_PMKID_CANDIDATE:
		if (data == NULL)
			return;
		wpa_priv_send_event(iface, PRIVSEP_EVENT_PMKID_CANDIDATE,
				    &data->pmkid_candidate,
				    sizeof(struct pmkid_candidate));
		break;
	case EVENT_FT_RESPONSE:
		wpa_priv_send_ft_response(iface, data);
		break;
	case EVENT_AUTH:
		wpa_priv_send_auth(iface, data);
		break;
	default:
		wpa_printf(MSG_DEBUG, "Unsupported driver event %d (%s) - TODO",
			   event, event_to_string(event));
		break;
	}
}


void wpa_supplicant_event_global(void *ctx, enum wpa_event_type event,
				 union wpa_event_data *data)
{
	struct wpa_priv_global *global = ctx;
	struct wpa_priv_interface *iface;

	if (event != EVENT_INTERFACE_STATUS)
		return;

	for (iface = global->interfaces; iface; iface = iface->next) {
		if (os_strcmp(iface->ifname, data->interface_status.ifname) ==
		    0)
			break;
	}
	if (iface && iface->driver->get_ifindex) {
		unsigned int ifindex;

		ifindex = iface->driver->get_ifindex(iface->drv_priv);
		if (ifindex != data->interface_status.ifindex) {
			wpa_printf(MSG_DEBUG,
				   "%s: interface status ifindex %d mismatch (%d)",
				   iface->ifname, ifindex,
				   data->interface_status.ifindex);
			return;
		}
	}
	if (iface)
		wpa_supplicant_event(iface, event, data);
}


void wpa_supplicant_rx_eapol(void *ctx, const u8 *src_addr,
			     const u8 *buf, size_t len)
{
	struct wpa_priv_interface *iface = ctx;
	struct msghdr msg;
	struct iovec io[3];
	int event = PRIVSEP_EVENT_RX_EAPOL;

	wpa_printf(MSG_DEBUG, "RX EAPOL from driver");
	io[0].iov_base = &event;
	io[0].iov_len = sizeof(event);
	io[1].iov_base = (u8 *) src_addr;
	io[1].iov_len = ETH_ALEN;
	io[2].iov_base = (u8 *) buf;
	io[2].iov_len = len;

	os_memset(&msg, 0, sizeof(msg));
	msg.msg_iov = io;
	msg.msg_iovlen = 3;
	msg.msg_name = &iface->drv_addr;
	msg.msg_namelen = iface->drv_addr_len;

	if (sendmsg(iface->fd, &msg, 0) < 0)
		wpa_printf(MSG_ERROR, "sendmsg(wpas_socket): %s",
			   strerror(errno));
}


static void wpa_priv_terminate(int sig, void *signal_ctx)
{
	wpa_printf(MSG_DEBUG, "wpa_priv termination requested");
	eloop_terminate();
}


static void wpa_priv_fd_workaround(void)
{
#ifdef __linux__
	int s, i;
	/* When started from pcmcia-cs scripts, wpa_supplicant might start with
	 * fd 0, 1, and 2 closed. This will cause some issues because many
	 * places in wpa_supplicant are still printing out to stdout. As a
	 * workaround, make sure that fd's 0, 1, and 2 are not used for other
	 * sockets. */
	for (i = 0; i < 3; i++) {
		s = open("/dev/null", O_RDWR);
		if (s > 2) {
			close(s);
			break;
		}
	}
#endif /* __linux__ */
}


static void usage(void)
{
	printf("wpa_priv v" VERSION_STR "\n"
	       "Copyright (c) 2007-2017, Jouni Malinen <j@w1.fi> and "
	       "contributors\n"
	       "\n"
	       "usage:\n"
	       "  wpa_priv [-Bdd] [-c<ctrl dir>] [-P<pid file>] "
	       "<driver:ifname> \\\n"
	       "           [driver:ifname ...]\n");
}


int main(int argc, char *argv[])
{
	int c, i;
	int ret = -1;
	char *pid_file = NULL;
	int daemonize = 0;
	char *ctrl_dir = "/var/run/wpa_priv";
	struct wpa_priv_global global;
	struct wpa_priv_interface *iface;

	if (os_program_init())
		return -1;

	wpa_priv_fd_workaround();

	os_memset(&global, 0, sizeof(global));
	global.interfaces = NULL;

	for (;;) {
		c = getopt(argc, argv, "Bc:dP:");
		if (c < 0)
			break;
		switch (c) {
		case 'B':
			daemonize++;
			break;
		case 'c':
			ctrl_dir = optarg;
			break;
		case 'd':
			wpa_debug_level--;
			break;
		case 'P':
			pid_file = os_rel2abs_path(optarg);
			break;
		default:
			usage();
			goto out2;
		}
	}

	if (optind >= argc) {
		usage();
		goto out2;
	}

	wpa_printf(MSG_DEBUG, "wpa_priv control directory: '%s'", ctrl_dir);

	if (eloop_init()) {
		wpa_printf(MSG_ERROR, "Failed to initialize event loop");
		goto out2;
	}

	for (i = optind; i < argc; i++) {
		wpa_printf(MSG_DEBUG, "Adding driver:interface %s", argv[i]);
		iface = wpa_priv_interface_init(&global, ctrl_dir, argv[i]);
		if (iface == NULL)
			goto out;
		iface->next = global.interfaces;
		global.interfaces = iface;
	}

	if (daemonize && os_daemonize(pid_file) && eloop_sock_requeue())
		goto out;

	eloop_register_signal_terminate(wpa_priv_terminate, NULL);
	eloop_run();

	ret = 0;

out:
	iface = global.interfaces;
	while (iface) {
		struct wpa_priv_interface *prev = iface;
		iface = iface->next;
		wpa_priv_interface_deinit(prev);
	}

	eloop_destroy();

out2:
	if (daemonize)
		os_daemonize_terminate(pid_file);
	os_free(pid_file);
	os_program_deinit();

	return ret;
}
