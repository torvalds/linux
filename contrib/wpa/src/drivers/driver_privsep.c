/*
 * WPA Supplicant - privilege separated driver interface
 * Copyright (c) 2007-2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <sys/un.h>

#include "common.h"
#include "driver.h"
#include "eloop.h"
#include "common/privsep_commands.h"


struct wpa_driver_privsep_data {
	void *ctx;
	u8 own_addr[ETH_ALEN];
	int priv_socket;
	char *own_socket_path;
	int cmd_socket;
	char *own_cmd_path;
	struct sockaddr_un priv_addr;
	char ifname[16];
};


static int wpa_priv_reg_cmd(struct wpa_driver_privsep_data *drv, int cmd)
{
	int res;

	res = sendto(drv->priv_socket, &cmd, sizeof(cmd), 0,
		     (struct sockaddr *) &drv->priv_addr,
		     sizeof(drv->priv_addr));
	if (res < 0)
		wpa_printf(MSG_ERROR, "sendto: %s", strerror(errno));
	return res < 0 ? -1 : 0;
}


static int wpa_priv_cmd(struct wpa_driver_privsep_data *drv, int cmd,
			const void *data, size_t data_len,
			void *reply, size_t *reply_len)
{
	struct msghdr msg;
	struct iovec io[2];

	io[0].iov_base = &cmd;
	io[0].iov_len = sizeof(cmd);
	io[1].iov_base = (u8 *) data;
	io[1].iov_len = data_len;

	os_memset(&msg, 0, sizeof(msg));
	msg.msg_iov = io;
	msg.msg_iovlen = data ? 2 : 1;
	msg.msg_name = &drv->priv_addr;
	msg.msg_namelen = sizeof(drv->priv_addr);

	if (sendmsg(drv->cmd_socket, &msg, 0) < 0) {
		wpa_printf(MSG_ERROR, "sendmsg(cmd_socket): %s",
			   strerror(errno));
		return -1;
	}

	if (reply) {
		fd_set rfds;
		struct timeval tv;
		int res;

		FD_ZERO(&rfds);
		FD_SET(drv->cmd_socket, &rfds);
		tv.tv_sec = 5;
		tv.tv_usec = 0;
		res = select(drv->cmd_socket + 1, &rfds, NULL, NULL, &tv);
		if (res < 0 && errno != EINTR) {
			wpa_printf(MSG_ERROR, "select: %s", strerror(errno));
			return -1;
		}

		if (FD_ISSET(drv->cmd_socket, &rfds)) {
			res = recv(drv->cmd_socket, reply, *reply_len, 0);
			if (res < 0) {
				wpa_printf(MSG_ERROR, "recv: %s",
					   strerror(errno));
				return -1;
			}
			*reply_len = res;
		} else {
			wpa_printf(MSG_DEBUG, "PRIVSEP: Timeout while waiting "
				   "for reply (cmd=%d)", cmd);
			return -1;
		}
	}

	return 0;
}


static int wpa_driver_privsep_scan(void *priv,
				   struct wpa_driver_scan_params *params)
{
	struct wpa_driver_privsep_data *drv = priv;
	struct privsep_cmd_scan scan;
	size_t i;

	wpa_printf(MSG_DEBUG, "%s: priv=%p", __func__, priv);
	os_memset(&scan, 0, sizeof(scan));
	scan.num_ssids = params->num_ssids;
	for (i = 0; i < params->num_ssids; i++) {
		if (!params->ssids[i].ssid)
			continue;
		scan.ssid_lens[i] = params->ssids[i].ssid_len;
		os_memcpy(scan.ssids[i], params->ssids[i].ssid,
			  scan.ssid_lens[i]);
	}

	for (i = 0; i < PRIVSEP_MAX_SCAN_FREQS &&
		     params->freqs && params->freqs[i]; i++)
		scan.freqs[i] = params->freqs[i];
	scan.num_freqs = i;

	return wpa_priv_cmd(drv, PRIVSEP_CMD_SCAN, &scan, sizeof(scan),
			    NULL, NULL);
}


static struct wpa_scan_results *
wpa_driver_privsep_get_scan_results2(void *priv)
{
	struct wpa_driver_privsep_data *drv = priv;
	int res, num;
	u8 *buf, *pos, *end;
	size_t reply_len = 60000;
	struct wpa_scan_results *results;
	struct wpa_scan_res *r;

	buf = os_malloc(reply_len);
	if (buf == NULL)
		return NULL;
	res = wpa_priv_cmd(drv, PRIVSEP_CMD_GET_SCAN_RESULTS,
			   NULL, 0, buf, &reply_len);
	if (res < 0) {
		os_free(buf);
		return NULL;
	}

	wpa_printf(MSG_DEBUG, "privsep: Received %lu bytes of scan results",
		   (unsigned long) reply_len);
	if (reply_len < sizeof(int)) {
		wpa_printf(MSG_DEBUG, "privsep: Invalid scan result len %lu",
			   (unsigned long) reply_len);
		os_free(buf);
		return NULL;
	}

	pos = buf;
	end = buf + reply_len;
	os_memcpy(&num, pos, sizeof(int));
	if (num < 0 || num > 1000) {
		os_free(buf);
		return NULL;
	}
	pos += sizeof(int);

	results = os_zalloc(sizeof(*results));
	if (results == NULL) {
		os_free(buf);
		return NULL;
	}

	results->res = os_calloc(num, sizeof(struct wpa_scan_res *));
	if (results->res == NULL) {
		os_free(results);
		os_free(buf);
		return NULL;
	}

	while (results->num < (size_t) num && end - pos > (int) sizeof(int)) {
		int len;
		os_memcpy(&len, pos, sizeof(int));
		pos += sizeof(int);
		if (len < 0 || len > 10000 || len > end - pos)
			break;

		r = os_memdup(pos, len);
		if (r == NULL)
			break;
		pos += len;
		if (sizeof(*r) + r->ie_len + r->beacon_ie_len > (size_t) len) {
			wpa_printf(MSG_ERROR,
				   "privsep: Invalid scan result len (%d + %d + %d > %d)",
				   (int) sizeof(*r), (int) r->ie_len,
				   (int) r->beacon_ie_len, len);
			os_free(r);
			break;
		}

		results->res[results->num++] = r;
	}

	os_free(buf);
	return results;
}


static int wpa_driver_privsep_set_key(const char *ifname, void *priv,
				      enum wpa_alg alg, const u8 *addr,
				      int key_idx, int set_tx,
				      const u8 *seq, size_t seq_len,
				      const u8 *key, size_t key_len)
{
	struct wpa_driver_privsep_data *drv = priv;
	struct privsep_cmd_set_key cmd;

	wpa_printf(MSG_DEBUG, "%s: priv=%p alg=%d key_idx=%d set_tx=%d",
		   __func__, priv, alg, key_idx, set_tx);

	os_memset(&cmd, 0, sizeof(cmd));
	cmd.alg = alg;
	if (addr)
		os_memcpy(cmd.addr, addr, ETH_ALEN);
	else
		os_memset(cmd.addr, 0xff, ETH_ALEN);
	cmd.key_idx = key_idx;
	cmd.set_tx = set_tx;
	if (seq && seq_len > 0 && seq_len < sizeof(cmd.seq)) {
		os_memcpy(cmd.seq, seq, seq_len);
		cmd.seq_len = seq_len;
	}
	if (key && key_len > 0 && key_len < sizeof(cmd.key)) {
		os_memcpy(cmd.key, key, key_len);
		cmd.key_len = key_len;
	}

	return wpa_priv_cmd(drv, PRIVSEP_CMD_SET_KEY, &cmd, sizeof(cmd),
			    NULL, NULL);
}


static int wpa_driver_privsep_authenticate(
	void *priv, struct wpa_driver_auth_params *params)
{
	struct wpa_driver_privsep_data *drv = priv;
	struct privsep_cmd_authenticate *data;
	int i, res;
	size_t buflen;
	u8 *pos;

	wpa_printf(MSG_DEBUG, "%s: priv=%p freq=%d bssid=" MACSTR
		   " auth_alg=%d local_state_change=%d p2p=%d",
		   __func__, priv, params->freq, MAC2STR(params->bssid),
		   params->auth_alg, params->local_state_change, params->p2p);

	buflen = sizeof(*data) + params->ie_len + params->auth_data_len;
	data = os_zalloc(buflen);
	if (data == NULL)
		return -1;

	data->freq = params->freq;
	os_memcpy(data->bssid, params->bssid, ETH_ALEN);
	os_memcpy(data->ssid, params->ssid, params->ssid_len);
	data->ssid_len = params->ssid_len;
	data->auth_alg = params->auth_alg;
	data->ie_len = params->ie_len;
	for (i = 0; i < 4; i++) {
		if (params->wep_key[i])
			os_memcpy(data->wep_key[i], params->wep_key[i],
				  params->wep_key_len[i]);
		data->wep_key_len[i] = params->wep_key_len[i];
	}
	data->wep_tx_keyidx = params->wep_tx_keyidx;
	data->local_state_change = params->local_state_change;
	data->p2p = params->p2p;
	pos = (u8 *) (data + 1);
	if (params->ie_len) {
		os_memcpy(pos, params->ie, params->ie_len);
		pos += params->ie_len;
	}
	if (params->auth_data_len)
		os_memcpy(pos, params->auth_data, params->auth_data_len);

	res = wpa_priv_cmd(drv, PRIVSEP_CMD_AUTHENTICATE, data, buflen,
			   NULL, NULL);
	os_free(data);

	return res;
}


static int wpa_driver_privsep_associate(
	void *priv, struct wpa_driver_associate_params *params)
{
	struct wpa_driver_privsep_data *drv = priv;
	struct privsep_cmd_associate *data;
	int res;
	size_t buflen;

	wpa_printf(MSG_DEBUG, "%s: priv=%p freq=%d pairwise_suite=%d "
		   "group_suite=%d key_mgmt_suite=%d auth_alg=%d mode=%d",
		   __func__, priv, params->freq.freq, params->pairwise_suite,
		   params->group_suite, params->key_mgmt_suite,
		   params->auth_alg, params->mode);

	buflen = sizeof(*data) + params->wpa_ie_len;
	data = os_zalloc(buflen);
	if (data == NULL)
		return -1;

	if (params->bssid)
		os_memcpy(data->bssid, params->bssid, ETH_ALEN);
	os_memcpy(data->ssid, params->ssid, params->ssid_len);
	data->ssid_len = params->ssid_len;
	data->hwmode = params->freq.mode;
	data->freq = params->freq.freq;
	data->channel = params->freq.channel;
	data->pairwise_suite = params->pairwise_suite;
	data->group_suite = params->group_suite;
	data->key_mgmt_suite = params->key_mgmt_suite;
	data->auth_alg = params->auth_alg;
	data->mode = params->mode;
	data->wpa_ie_len = params->wpa_ie_len;
	if (params->wpa_ie)
		os_memcpy(data + 1, params->wpa_ie, params->wpa_ie_len);
	/* TODO: add support for other assoc parameters */

	res = wpa_priv_cmd(drv, PRIVSEP_CMD_ASSOCIATE, data, buflen,
			   NULL, NULL);
	os_free(data);

	return res;
}


static int wpa_driver_privsep_get_bssid(void *priv, u8 *bssid)
{
	struct wpa_driver_privsep_data *drv = priv;
	int res;
	size_t len = ETH_ALEN;

	res = wpa_priv_cmd(drv, PRIVSEP_CMD_GET_BSSID, NULL, 0, bssid, &len);
	if (res < 0 || len != ETH_ALEN)
		return -1;
	return 0;
}


static int wpa_driver_privsep_get_ssid(void *priv, u8 *ssid)
{
	struct wpa_driver_privsep_data *drv = priv;
	int res, ssid_len;
	u8 reply[sizeof(int) + SSID_MAX_LEN];
	size_t len = sizeof(reply);

	res = wpa_priv_cmd(drv, PRIVSEP_CMD_GET_SSID, NULL, 0, reply, &len);
	if (res < 0 || len < sizeof(int))
		return -1;
	os_memcpy(&ssid_len, reply, sizeof(int));
	if (ssid_len < 0 || ssid_len > SSID_MAX_LEN ||
	    sizeof(int) + ssid_len > len) {
		wpa_printf(MSG_DEBUG, "privsep: Invalid get SSID reply");
		return -1;
	}
	os_memcpy(ssid, &reply[sizeof(int)], ssid_len);
	return ssid_len;
}


static int wpa_driver_privsep_deauthenticate(void *priv, const u8 *addr,
					  int reason_code)
{
	//struct wpa_driver_privsep_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s addr=" MACSTR " reason_code=%d",
		   __func__, MAC2STR(addr), reason_code);
	wpa_printf(MSG_DEBUG, "%s - TODO", __func__);
	return 0;
}


static void wpa_driver_privsep_event_auth(void *ctx, u8 *buf, size_t len)
{
	union wpa_event_data data;
	struct privsep_event_auth *auth;

	os_memset(&data, 0, sizeof(data));
	if (len < sizeof(*auth))
		return;
	auth = (struct privsep_event_auth *) buf;
	if (len < sizeof(*auth) + auth->ies_len)
		return;

	os_memcpy(data.auth.peer, auth->peer, ETH_ALEN);
	os_memcpy(data.auth.bssid, auth->bssid, ETH_ALEN);
	data.auth.auth_type = auth->auth_type;
	data.auth.auth_transaction = auth->auth_transaction;
	data.auth.status_code = auth->status_code;
	if (auth->ies_len) {
		data.auth.ies = (u8 *) (auth + 1);
		data.auth.ies_len = auth->ies_len;
	}

	wpa_supplicant_event(ctx, EVENT_AUTH, &data);
}


static void wpa_driver_privsep_event_assoc(void *ctx,
					   enum wpa_event_type event,
					   u8 *buf, size_t len)
{
	union wpa_event_data data;
	int inc_data = 0;
	u8 *pos, *end;
	int ie_len;

	os_memset(&data, 0, sizeof(data));

	pos = buf;
	end = buf + len;

	if (end - pos < (int) sizeof(int))
		return;
	os_memcpy(&ie_len, pos, sizeof(int));
	pos += sizeof(int);
	if (ie_len < 0 || ie_len > end - pos)
		return;
	if (ie_len) {
		data.assoc_info.req_ies = pos;
		data.assoc_info.req_ies_len = ie_len;
		pos += ie_len;
		inc_data = 1;
	}

	wpa_supplicant_event(ctx, event, inc_data ? &data : NULL);
}


static void wpa_driver_privsep_event_interface_status(void *ctx, u8 *buf,
						      size_t len)
{
	union wpa_event_data data;
	int ievent;

	if (len < sizeof(int) ||
	    len - sizeof(int) > sizeof(data.interface_status.ifname))
		return;

	os_memcpy(&ievent, buf, sizeof(int));

	os_memset(&data, 0, sizeof(data));
	data.interface_status.ievent = ievent;
	os_memcpy(data.interface_status.ifname, buf + sizeof(int),
		  len - sizeof(int));
	wpa_supplicant_event(ctx, EVENT_INTERFACE_STATUS, &data);
}


static void wpa_driver_privsep_event_michael_mic_failure(
	void *ctx, u8 *buf, size_t len)
{
	union wpa_event_data data;

	if (len != sizeof(int))
		return;

	os_memset(&data, 0, sizeof(data));
	os_memcpy(&data.michael_mic_failure.unicast, buf, sizeof(int));
	wpa_supplicant_event(ctx, EVENT_MICHAEL_MIC_FAILURE, &data);
}


static void wpa_driver_privsep_event_pmkid_candidate(void *ctx, u8 *buf,
						     size_t len)
{
	union wpa_event_data data;

	if (len != sizeof(struct pmkid_candidate))
		return;

	os_memset(&data, 0, sizeof(data));
	os_memcpy(&data.pmkid_candidate, buf, len);
	wpa_supplicant_event(ctx, EVENT_PMKID_CANDIDATE, &data);
}


static void wpa_driver_privsep_event_ft_response(void *ctx, u8 *buf,
						 size_t len)
{
	union wpa_event_data data;

	if (len < sizeof(int) + ETH_ALEN)
		return;

	os_memset(&data, 0, sizeof(data));
	os_memcpy(&data.ft_ies.ft_action, buf, sizeof(int));
	os_memcpy(data.ft_ies.target_ap, buf + sizeof(int), ETH_ALEN);
	data.ft_ies.ies = buf + sizeof(int) + ETH_ALEN;
	data.ft_ies.ies_len = len - sizeof(int) - ETH_ALEN;
	wpa_supplicant_event(ctx, EVENT_FT_RESPONSE, &data);
}


static void wpa_driver_privsep_event_rx_eapol(void *ctx, u8 *buf, size_t len)
{
	if (len < ETH_ALEN)
		return;
	drv_event_eapol_rx(ctx, buf, buf + ETH_ALEN, len - ETH_ALEN);
}


static void wpa_driver_privsep_receive(int sock, void *eloop_ctx,
				       void *sock_ctx)
{
	struct wpa_driver_privsep_data *drv = eloop_ctx;
	u8 *buf, *event_buf;
	size_t event_len;
	int res, event;
	enum privsep_event e;
	struct sockaddr_un from;
	socklen_t fromlen = sizeof(from);
	const size_t buflen = 2000;

	buf = os_malloc(buflen);
	if (buf == NULL)
		return;
	res = recvfrom(sock, buf, buflen, 0,
		       (struct sockaddr *) &from, &fromlen);
	if (res < 0) {
		wpa_printf(MSG_ERROR, "recvfrom(priv_socket): %s",
			   strerror(errno));
		os_free(buf);
		return;
	}

	wpa_printf(MSG_DEBUG, "privsep_driver: received %u bytes", res);

	if (res < (int) sizeof(int)) {
		wpa_printf(MSG_DEBUG, "Too short event message (len=%d)", res);
		return;
	}

	os_memcpy(&event, buf, sizeof(int));
	event_buf = &buf[sizeof(int)];
	event_len = res - sizeof(int);
	wpa_printf(MSG_DEBUG, "privsep: Event %d received (len=%lu)",
		   event, (unsigned long) event_len);

	e = event;
	switch (e) {
	case PRIVSEP_EVENT_SCAN_RESULTS:
		wpa_supplicant_event(drv->ctx, EVENT_SCAN_RESULTS, NULL);
		break;
	case PRIVSEP_EVENT_SCAN_STARTED:
		wpa_supplicant_event(drv->ctx, EVENT_SCAN_STARTED, NULL);
		break;
	case PRIVSEP_EVENT_ASSOC:
		wpa_driver_privsep_event_assoc(drv->ctx, EVENT_ASSOC,
					       event_buf, event_len);
		break;
	case PRIVSEP_EVENT_DISASSOC:
		wpa_supplicant_event(drv->ctx, EVENT_DISASSOC, NULL);
		break;
	case PRIVSEP_EVENT_ASSOCINFO:
		wpa_driver_privsep_event_assoc(drv->ctx, EVENT_ASSOCINFO,
					       event_buf, event_len);
		break;
	case PRIVSEP_EVENT_MICHAEL_MIC_FAILURE:
		wpa_driver_privsep_event_michael_mic_failure(
			drv->ctx, event_buf, event_len);
		break;
	case PRIVSEP_EVENT_INTERFACE_STATUS:
		wpa_driver_privsep_event_interface_status(drv->ctx, event_buf,
							  event_len);
		break;
	case PRIVSEP_EVENT_PMKID_CANDIDATE:
		wpa_driver_privsep_event_pmkid_candidate(drv->ctx, event_buf,
							 event_len);
		break;
	case PRIVSEP_EVENT_FT_RESPONSE:
		wpa_driver_privsep_event_ft_response(drv->ctx, event_buf,
						     event_len);
		break;
	case PRIVSEP_EVENT_RX_EAPOL:
		wpa_driver_privsep_event_rx_eapol(drv->ctx, event_buf,
						  event_len);
		break;
	case PRIVSEP_EVENT_AUTH:
		wpa_driver_privsep_event_auth(drv->ctx, event_buf, event_len);
		break;
	}

	os_free(buf);
}


static void * wpa_driver_privsep_init(void *ctx, const char *ifname)
{
	struct wpa_driver_privsep_data *drv;

	drv = os_zalloc(sizeof(*drv));
	if (drv == NULL)
		return NULL;
	drv->ctx = ctx;
	drv->priv_socket = -1;
	drv->cmd_socket = -1;
	os_strlcpy(drv->ifname, ifname, sizeof(drv->ifname));

	return drv;
}


static void wpa_driver_privsep_deinit(void *priv)
{
	struct wpa_driver_privsep_data *drv = priv;

	if (drv->priv_socket >= 0) {
		wpa_priv_reg_cmd(drv, PRIVSEP_CMD_UNREGISTER);
		eloop_unregister_read_sock(drv->priv_socket);
		close(drv->priv_socket);
	}

	if (drv->own_socket_path) {
		unlink(drv->own_socket_path);
		os_free(drv->own_socket_path);
	}

	if (drv->cmd_socket >= 0) {
		eloop_unregister_read_sock(drv->cmd_socket);
		close(drv->cmd_socket);
	}

	if (drv->own_cmd_path) {
		unlink(drv->own_cmd_path);
		os_free(drv->own_cmd_path);
	}

	os_free(drv);
}


static int wpa_driver_privsep_set_param(void *priv, const char *param)
{
	struct wpa_driver_privsep_data *drv = priv;
	const char *pos;
	char *own_dir, *priv_dir;
	static unsigned int counter = 0;
	size_t len;
	struct sockaddr_un addr;

	wpa_printf(MSG_DEBUG, "%s: param='%s'", __func__, param);
	if (param == NULL)
		pos = NULL;
	else
		pos = os_strstr(param, "own_dir=");
	if (pos) {
		char *end;
		own_dir = os_strdup(pos + 8);
		if (own_dir == NULL)
			return -1;
		end = os_strchr(own_dir, ' ');
		if (end)
			*end = '\0';
	} else {
		own_dir = os_strdup("/tmp");
		if (own_dir == NULL)
			return -1;
	}

	if (param == NULL)
		pos = NULL;
	else
		pos = os_strstr(param, "priv_dir=");
	if (pos) {
		char *end;
		priv_dir = os_strdup(pos + 9);
		if (priv_dir == NULL) {
			os_free(own_dir);
			return -1;
		}
		end = os_strchr(priv_dir, ' ');
		if (end)
			*end = '\0';
	} else {
		priv_dir = os_strdup("/var/run/wpa_priv");
		if (priv_dir == NULL) {
			os_free(own_dir);
			return -1;
		}
	}

	len = os_strlen(own_dir) + 50;
	drv->own_socket_path = os_malloc(len);
	if (drv->own_socket_path == NULL) {
		os_free(priv_dir);
		os_free(own_dir);
		return -1;
	}
	os_snprintf(drv->own_socket_path, len, "%s/wpa_privsep-%d-%d",
		    own_dir, getpid(), counter++);

	len = os_strlen(own_dir) + 50;
	drv->own_cmd_path = os_malloc(len);
	if (drv->own_cmd_path == NULL) {
		os_free(drv->own_socket_path);
		drv->own_socket_path = NULL;
		os_free(priv_dir);
		os_free(own_dir);
		return -1;
	}
	os_snprintf(drv->own_cmd_path, len, "%s/wpa_privsep-%d-%d",
		    own_dir, getpid(), counter++);

	os_free(own_dir);

	drv->priv_addr.sun_family = AF_UNIX;
	os_snprintf(drv->priv_addr.sun_path, sizeof(drv->priv_addr.sun_path),
		    "%s/%s", priv_dir, drv->ifname);
	os_free(priv_dir);

	drv->priv_socket = socket(PF_UNIX, SOCK_DGRAM, 0);
	if (drv->priv_socket < 0) {
		wpa_printf(MSG_ERROR, "socket(PF_UNIX): %s", strerror(errno));
		os_free(drv->own_socket_path);
		drv->own_socket_path = NULL;
		return -1;
	}

	os_memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	os_strlcpy(addr.sun_path, drv->own_socket_path, sizeof(addr.sun_path));
	if (bind(drv->priv_socket, (struct sockaddr *) &addr, sizeof(addr)) <
	    0) {
		wpa_printf(MSG_ERROR,
			   "privsep-set-params priv-sock: bind(PF_UNIX): %s",
			   strerror(errno));
		close(drv->priv_socket);
		drv->priv_socket = -1;
		unlink(drv->own_socket_path);
		os_free(drv->own_socket_path);
		drv->own_socket_path = NULL;
		return -1;
	}

	eloop_register_read_sock(drv->priv_socket, wpa_driver_privsep_receive,
				 drv, NULL);

	drv->cmd_socket = socket(PF_UNIX, SOCK_DGRAM, 0);
	if (drv->cmd_socket < 0) {
		wpa_printf(MSG_ERROR, "socket(PF_UNIX): %s", strerror(errno));
		os_free(drv->own_cmd_path);
		drv->own_cmd_path = NULL;
		return -1;
	}

	os_memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	os_strlcpy(addr.sun_path, drv->own_cmd_path, sizeof(addr.sun_path));
	if (bind(drv->cmd_socket, (struct sockaddr *) &addr, sizeof(addr)) < 0)
	{
		wpa_printf(MSG_ERROR,
			   "privsep-set-params cmd-sock: bind(PF_UNIX): %s",
			   strerror(errno));
		close(drv->cmd_socket);
		drv->cmd_socket = -1;
		unlink(drv->own_cmd_path);
		os_free(drv->own_cmd_path);
		drv->own_cmd_path = NULL;
		return -1;
	}

	if (wpa_priv_reg_cmd(drv, PRIVSEP_CMD_REGISTER) < 0) {
		wpa_printf(MSG_ERROR, "Failed to register with wpa_priv");
		return -1;
	}

	return 0;
}


static int wpa_driver_privsep_get_capa(void *priv,
				       struct wpa_driver_capa *capa)
{
	struct wpa_driver_privsep_data *drv = priv;
	int res;
	size_t len = sizeof(*capa);

	res = wpa_priv_cmd(drv, PRIVSEP_CMD_GET_CAPA, NULL, 0, capa, &len);
	if (res < 0 || len != sizeof(*capa))
		return -1;
	/* For now, no support for passing extended_capa pointers */
	capa->extended_capa = NULL;
	capa->extended_capa_mask = NULL;
	capa->extended_capa_len = 0;
	return 0;
}


static const u8 * wpa_driver_privsep_get_mac_addr(void *priv)
{
	struct wpa_driver_privsep_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s", __func__);
	return drv->own_addr;
}


static int wpa_driver_privsep_set_country(void *priv, const char *alpha2)
{
	struct wpa_driver_privsep_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s country='%s'", __func__, alpha2);
	return wpa_priv_cmd(drv, PRIVSEP_CMD_SET_COUNTRY, alpha2,
			    os_strlen(alpha2), NULL, NULL);
}


struct wpa_driver_ops wpa_driver_privsep_ops = {
	"privsep",
	"wpa_supplicant privilege separated driver",
	.get_bssid = wpa_driver_privsep_get_bssid,
	.get_ssid = wpa_driver_privsep_get_ssid,
	.set_key = wpa_driver_privsep_set_key,
	.init = wpa_driver_privsep_init,
	.deinit = wpa_driver_privsep_deinit,
	.set_param = wpa_driver_privsep_set_param,
	.scan2 = wpa_driver_privsep_scan,
	.deauthenticate = wpa_driver_privsep_deauthenticate,
	.authenticate = wpa_driver_privsep_authenticate,
	.associate = wpa_driver_privsep_associate,
	.get_capa = wpa_driver_privsep_get_capa,
	.get_mac_addr = wpa_driver_privsep_get_mac_addr,
	.get_scan_results2 = wpa_driver_privsep_get_scan_results2,
	.set_country = wpa_driver_privsep_set_country,
};


const struct wpa_driver_ops *const wpa_drivers[] =
{
	&wpa_driver_privsep_ops,
	NULL
};
