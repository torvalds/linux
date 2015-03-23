/*
 * Wi-Fi Direct - P2P service discovery
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

#include "common.h"
#include "common/ieee802_11_defs.h"
#include "p2p_i.h"
#include "p2p.h"


struct p2p_sd_query * p2p_pending_sd_req(struct p2p_data *p2p,
					 struct p2p_device *dev)
{
	struct p2p_sd_query *q;

	if (!(dev->info.dev_capab & P2P_DEV_CAPAB_SERVICE_DISCOVERY))
		return 0; /* peer does not support SD */

	for (q = p2p->sd_queries; q; q = q->next) {
		if (q->for_all_peers && !(dev->flags & P2P_DEV_SD_INFO))
			return q;
		if (!q->for_all_peers &&
		    os_memcmp(q->peer, dev->info.p2p_device_addr, ETH_ALEN) ==
		    0)
			return q;
	}

	return NULL;
}


static int p2p_unlink_sd_query(struct p2p_data *p2p,
			       struct p2p_sd_query *query)
{
	struct p2p_sd_query *q, *prev;
	q = p2p->sd_queries;
	prev = NULL;
	while (q) {
		if (q == query) {
			if (prev)
				prev->next = q->next;
			else
				p2p->sd_queries = q->next;
			if (p2p->sd_query == query)
				p2p->sd_query = NULL;
			return 1;
		}
		prev = q;
		q = q->next;
	}
	return 0;
}


static void p2p_free_sd_query(struct p2p_sd_query *q)
{
	if (q == NULL)
		return;
	wpabuf_free(q->tlvs);
	os_free(q);
}


void p2p_free_sd_queries(struct p2p_data *p2p)
{
	struct p2p_sd_query *q, *prev;
	q = p2p->sd_queries;
	p2p->sd_queries = NULL;
	while (q) {
		prev = q;
		q = q->next;
		p2p_free_sd_query(prev);
	}
}


static struct wpabuf * p2p_build_sd_query(u16 update_indic,
					  struct wpabuf *tlvs)
{
	struct wpabuf *buf;
	u8 *len_pos, *len_pos2;

	buf = wpabuf_alloc(1000 + wpabuf_len(tlvs));
	if (buf == NULL)
		return NULL;

	wpabuf_put_u8(buf, WLAN_ACTION_PUBLIC);
	wpabuf_put_u8(buf, WLAN_PA_GAS_INITIAL_REQ);
	wpabuf_put_u8(buf, 0); /* Dialog Token */

	/* Advertisement Protocol IE */
	wpabuf_put_u8(buf, WLAN_EID_ADV_PROTO);
	wpabuf_put_u8(buf, 2); /* Length */
	wpabuf_put_u8(buf, 0); /* QueryRespLenLimit | PAME-BI */
	wpabuf_put_u8(buf, NATIVE_QUERY_PROTOCOL); /* Advertisement Protocol */

	/* Query Request */
	len_pos = wpabuf_put(buf, 2); /* Length (to be filled) */

	/* NQP Query Request Frame */
	wpabuf_put_le16(buf, NQP_VENDOR_SPECIFIC); /* Info ID */
	len_pos2 = wpabuf_put(buf, 2); /* Length (to be filled) */
	wpabuf_put_be24(buf, OUI_WFA);
	wpabuf_put_u8(buf, P2P_OUI_TYPE);
	wpabuf_put_le16(buf, update_indic); /* Service Update Indicator */
	wpabuf_put_buf(buf, tlvs);

	WPA_PUT_LE16(len_pos2, (u8 *) wpabuf_put(buf, 0) - len_pos2 - 2);
	WPA_PUT_LE16(len_pos, (u8 *) wpabuf_put(buf, 0) - len_pos - 2);

	return buf;
}


static struct wpabuf * p2p_build_gas_comeback_req(u8 dialog_token)
{
	struct wpabuf *buf;

	buf = wpabuf_alloc(3);
	if (buf == NULL)
		return NULL;

	wpabuf_put_u8(buf, WLAN_ACTION_PUBLIC);
	wpabuf_put_u8(buf, WLAN_PA_GAS_COMEBACK_REQ);
	wpabuf_put_u8(buf, dialog_token);

	return buf;
}


static void p2p_send_gas_comeback_req(struct p2p_data *p2p, const u8 *dst,
				      u8 dialog_token, int freq)
{
	struct wpabuf *req;

	req = p2p_build_gas_comeback_req(dialog_token);
	if (req == NULL)
		return;

	p2p->pending_action_state = P2P_NO_PENDING_ACTION;
	if (p2p_send_action(p2p, freq, dst, p2p->cfg->dev_addr, dst,
			    wpabuf_head(req), wpabuf_len(req), 200) < 0)
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Failed to send Action frame");

	wpabuf_free(req);
}


static struct wpabuf * p2p_build_sd_response(u8 dialog_token, u16 status_code,
					     u16 comeback_delay,
					     u16 update_indic,
					     const struct wpabuf *tlvs)
{
	struct wpabuf *buf;
	u8 *len_pos, *len_pos2;

	buf = wpabuf_alloc(1000 + (tlvs ? wpabuf_len(tlvs) : 0));
	if (buf == NULL)
		return NULL;

	wpabuf_put_u8(buf, WLAN_ACTION_PUBLIC);
	wpabuf_put_u8(buf, WLAN_PA_GAS_INITIAL_RESP);
	wpabuf_put_u8(buf, dialog_token);
	wpabuf_put_le16(buf, status_code);
	wpabuf_put_le16(buf, comeback_delay);

	/* Advertisement Protocol IE */
	wpabuf_put_u8(buf, WLAN_EID_ADV_PROTO);
	wpabuf_put_u8(buf, 2); /* Length */
	wpabuf_put_u8(buf, 0x7f); /* QueryRespLenLimit | PAME-BI */
	wpabuf_put_u8(buf, NATIVE_QUERY_PROTOCOL); /* Advertisement Protocol */

	/* Query Response */
	len_pos = wpabuf_put(buf, 2); /* Length (to be filled) */

	if (tlvs) {
		/* NQP Query Response Frame */
		wpabuf_put_le16(buf, NQP_VENDOR_SPECIFIC); /* Info ID */
		len_pos2 = wpabuf_put(buf, 2); /* Length (to be filled) */
		wpabuf_put_be24(buf, OUI_WFA);
		wpabuf_put_u8(buf, P2P_OUI_TYPE);
		 /* Service Update Indicator */
		wpabuf_put_le16(buf, update_indic);
		wpabuf_put_buf(buf, tlvs);

		WPA_PUT_LE16(len_pos2,
			     (u8 *) wpabuf_put(buf, 0) - len_pos2 - 2);
	}

	WPA_PUT_LE16(len_pos, (u8 *) wpabuf_put(buf, 0) - len_pos - 2);

	return buf;
}


static struct wpabuf * p2p_build_gas_comeback_resp(u8 dialog_token,
						   u16 status_code,
						   u16 update_indic,
						   const u8 *data, size_t len,
						   u8 frag_id, u8 more,
						   u16 total_len)
{
	struct wpabuf *buf;
	u8 *len_pos;

	buf = wpabuf_alloc(1000 + len);
	if (buf == NULL)
		return NULL;

	wpabuf_put_u8(buf, WLAN_ACTION_PUBLIC);
	wpabuf_put_u8(buf, WLAN_PA_GAS_COMEBACK_RESP);
	wpabuf_put_u8(buf, dialog_token);
	wpabuf_put_le16(buf, status_code);
	wpabuf_put_u8(buf, frag_id | (more ? 0x80 : 0));
	wpabuf_put_le16(buf, 0); /* Comeback Delay */

	/* Advertisement Protocol IE */
	wpabuf_put_u8(buf, WLAN_EID_ADV_PROTO);
	wpabuf_put_u8(buf, 2); /* Length */
	wpabuf_put_u8(buf, 0x7f); /* QueryRespLenLimit | PAME-BI */
	wpabuf_put_u8(buf, NATIVE_QUERY_PROTOCOL); /* Advertisement Protocol */

	/* Query Response */
	len_pos = wpabuf_put(buf, 2); /* Length (to be filled) */

	if (frag_id == 0) {
		/* NQP Query Response Frame */
		wpabuf_put_le16(buf, NQP_VENDOR_SPECIFIC); /* Info ID */
		wpabuf_put_le16(buf, 3 + 1 + 2 + total_len);
		wpabuf_put_be24(buf, OUI_WFA);
		wpabuf_put_u8(buf, P2P_OUI_TYPE);
		/* Service Update Indicator */
		wpabuf_put_le16(buf, update_indic);
	}

	wpabuf_put_data(buf, data, len);

	WPA_PUT_LE16(len_pos, (u8 *) wpabuf_put(buf, 0) - len_pos - 2);

	return buf;
}


int p2p_start_sd(struct p2p_data *p2p, struct p2p_device *dev)
{
	struct wpabuf *req;
	int ret = 0;
	struct p2p_sd_query *query;
	int freq;

	freq = dev->listen_freq > 0 ? dev->listen_freq : dev->oper_freq;
	if (freq <= 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No Listen/Operating frequency known for the "
			"peer " MACSTR " to send SD Request",
			MAC2STR(dev->info.p2p_device_addr));
		return -1;
	}

	query = p2p_pending_sd_req(p2p, dev);
	if (query == NULL)
		return -1;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Start Service Discovery with " MACSTR,
		MAC2STR(dev->info.p2p_device_addr));

	req = p2p_build_sd_query(p2p->srv_update_indic, query->tlvs);
	if (req == NULL)
		return -1;

	p2p->sd_peer = dev;
	p2p->sd_query = query;
	p2p->pending_action_state = P2P_PENDING_SD;

	if (p2p_send_action(p2p, freq, dev->info.p2p_device_addr,
			    p2p->cfg->dev_addr, dev->info.p2p_device_addr,
			    wpabuf_head(req), wpabuf_len(req), 5000) < 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Failed to send Action frame");
		ret = -1;
	}

	wpabuf_free(req);

	return ret;
}


void p2p_rx_gas_initial_req(struct p2p_data *p2p, const u8 *sa,
			    const u8 *data, size_t len, int rx_freq)
{
	const u8 *pos = data;
	const u8 *end = data + len;
	const u8 *next;
	u8 dialog_token;
	u16 slen;
	int freq;
	u16 update_indic;


	if (p2p->cfg->sd_request == NULL)
		return;

	if (rx_freq > 0)
		freq = rx_freq;
	else
		freq = p2p_channel_to_freq(p2p->cfg->country,
					   p2p->cfg->reg_class,
					   p2p->cfg->channel);
	if (freq < 0)
		return;

	if (len < 1 + 2)
		return;

	dialog_token = *pos++;
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: GAS Initial Request from " MACSTR " (dialog token %u, "
		"freq %d)",
		MAC2STR(sa), dialog_token, rx_freq);

	if (*pos != WLAN_EID_ADV_PROTO) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unexpected IE in GAS Initial Request: %u", *pos);
		return;
	}
	pos++;

	slen = *pos++;
	next = pos + slen;
	if (next > end || slen < 2) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Invalid IE in GAS Initial Request");
		return;
	}
	pos++; /* skip QueryRespLenLimit and PAME-BI */

	if (*pos != NATIVE_QUERY_PROTOCOL) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unsupported GAS advertisement protocol id %u",
			*pos);
		return;
	}

	pos = next;
	/* Query Request */
	if (pos + 2 > end)
		return;
	slen = WPA_GET_LE16(pos);
	pos += 2;
	if (pos + slen > end)
		return;
	end = pos + slen;

	/* NQP Query Request */
	if (pos + 4 > end)
		return;
	if (WPA_GET_LE16(pos) != NQP_VENDOR_SPECIFIC) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unsupported NQP Info ID %u", WPA_GET_LE16(pos));
		return;
	}
	pos += 2;

	slen = WPA_GET_LE16(pos);
	pos += 2;
	if (pos + slen > end || slen < 3 + 1) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Invalid NQP Query Request length");
		return;
	}

	if (WPA_GET_BE24(pos) != OUI_WFA) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unsupported NQP OUI %06x", WPA_GET_BE24(pos));
		return;
	}
	pos += 3;

	if (*pos != P2P_OUI_TYPE) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unsupported NQP vendor type %u", *pos);
		return;
	}
	pos++;

	if (pos + 2 > end)
		return;
	update_indic = WPA_GET_LE16(pos);
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Service Update Indicator: %u", update_indic);
	pos += 2;

	p2p->cfg->sd_request(p2p->cfg->cb_ctx, freq, sa, dialog_token,
			     update_indic, pos, end - pos);
	/* the response will be indicated with a call to p2p_sd_response() */
}


void p2p_sd_response(struct p2p_data *p2p, int freq, const u8 *dst,
		     u8 dialog_token, const struct wpabuf *resp_tlvs)
{
	struct wpabuf *resp;

	/* TODO: fix the length limit to match with the maximum frame length */
	if (wpabuf_len(resp_tlvs) > 1400) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: SD response long "
			"enough to require fragmentation");
		if (p2p->sd_resp) {
			/*
			 * TODO: Could consider storing the fragmented response
			 * separately for each peer to avoid having to drop old
			 * one if there is more than one pending SD query.
			 * Though, that would eat more memory, so there are
			 * also benefits to just using a single buffer.
			 */
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Drop "
				"previous SD response");
			wpabuf_free(p2p->sd_resp);
		}
		os_memcpy(p2p->sd_resp_addr, dst, ETH_ALEN);
		p2p->sd_resp_dialog_token = dialog_token;
		p2p->sd_resp = wpabuf_dup(resp_tlvs);
		p2p->sd_resp_pos = 0;
		p2p->sd_frag_id = 0;
		resp = p2p_build_sd_response(dialog_token, WLAN_STATUS_SUCCESS,
					     1, p2p->srv_update_indic, NULL);
	} else {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: SD response fits "
			"in initial response");
		resp = p2p_build_sd_response(dialog_token,
					     WLAN_STATUS_SUCCESS, 0,
					     p2p->srv_update_indic, resp_tlvs);
	}
	if (resp == NULL)
		return;

	p2p->pending_action_state = P2P_NO_PENDING_ACTION;
	if (p2p_send_action(p2p, freq, dst, p2p->cfg->dev_addr,
			    p2p->cfg->dev_addr,
			    wpabuf_head(resp), wpabuf_len(resp), 200) < 0)
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Failed to send Action frame");

	wpabuf_free(resp);
}


void p2p_rx_gas_initial_resp(struct p2p_data *p2p, const u8 *sa,
			     const u8 *data, size_t len, int rx_freq)
{
	const u8 *pos = data;
	const u8 *end = data + len;
	const u8 *next;
	u8 dialog_token;
	u16 status_code;
	u16 comeback_delay;
	u16 slen;
	u16 update_indic;

	if (p2p->state != P2P_SD_DURING_FIND || p2p->sd_peer == NULL ||
	    os_memcmp(sa, p2p->sd_peer->info.p2p_device_addr, ETH_ALEN) != 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Ignore unexpected GAS Initial Response from "
			MACSTR, MAC2STR(sa));
		return;
	}
	p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
	p2p_clear_timeout(p2p);

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Received GAS Initial Response from " MACSTR " (len=%d)",
		MAC2STR(sa), (int) len);

	if (len < 5 + 2) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Too short GAS Initial Response frame");
		return;
	}

	dialog_token = *pos++;
	/* TODO: check dialog_token match */
	status_code = WPA_GET_LE16(pos);
	pos += 2;
	comeback_delay = WPA_GET_LE16(pos);
	pos += 2;
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: dialog_token=%u status_code=%u comeback_delay=%u",
		dialog_token, status_code, comeback_delay);
	if (status_code) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Service Discovery failed: status code %u",
			status_code);
		return;
	}

	if (*pos != WLAN_EID_ADV_PROTO) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unexpected IE in GAS Initial Response: %u",
			*pos);
		return;
	}
	pos++;

	slen = *pos++;
	next = pos + slen;
	if (next > end || slen < 2) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Invalid IE in GAS Initial Response");
		return;
	}
	pos++; /* skip QueryRespLenLimit and PAME-BI */

	if (*pos != NATIVE_QUERY_PROTOCOL) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unsupported GAS advertisement protocol id %u",
			*pos);
		return;
	}

	pos = next;
	/* Query Response */
	if (pos + 2 > end) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Too short Query "
			"Response");
		return;
	}
	slen = WPA_GET_LE16(pos);
	pos += 2;
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Query Response Length: %d",
		slen);
	if (pos + slen > end) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Not enough Query "
			"Response data");
		return;
	}
	end = pos + slen;

	if (comeback_delay) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Fragmented "
			"response - request fragments");
		if (p2p->sd_rx_resp) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Drop "
				"old SD reassembly buffer");
			wpabuf_free(p2p->sd_rx_resp);
			p2p->sd_rx_resp = NULL;
		}
		p2p_send_gas_comeback_req(p2p, sa, dialog_token, rx_freq);
		return;
	}

	/* NQP Query Response */
	if (pos + 4 > end)
		return;
	if (WPA_GET_LE16(pos) != NQP_VENDOR_SPECIFIC) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unsupported NQP Info ID %u", WPA_GET_LE16(pos));
		return;
	}
	pos += 2;

	slen = WPA_GET_LE16(pos);
	pos += 2;
	if (pos + slen > end || slen < 3 + 1) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Invalid NQP Query Response length");
		return;
	}

	if (WPA_GET_BE24(pos) != OUI_WFA) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unsupported NQP OUI %06x", WPA_GET_BE24(pos));
		return;
	}
	pos += 3;

	if (*pos != P2P_OUI_TYPE) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unsupported NQP vendor type %u", *pos);
		return;
	}
	pos++;

	if (pos + 2 > end)
		return;
	update_indic = WPA_GET_LE16(pos);
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Service Update Indicator: %u", update_indic);
	pos += 2;

	p2p->sd_peer->flags |= P2P_DEV_SD_INFO;
	p2p->sd_peer->flags &= ~P2P_DEV_SD_SCHEDULE;
	p2p->sd_peer = NULL;

	if (p2p->sd_query) {
		if (!p2p->sd_query->for_all_peers) {
			struct p2p_sd_query *q;
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Remove completed SD query %p",
				p2p->sd_query);
			q = p2p->sd_query;
			p2p_unlink_sd_query(p2p, p2p->sd_query);
			p2p_free_sd_query(q);
		}
		p2p->sd_query = NULL;
	}

	if (p2p->cfg->sd_response)
		p2p->cfg->sd_response(p2p->cfg->cb_ctx, sa, update_indic,
				      pos, end - pos);
	p2p_continue_find(p2p);
}


void p2p_rx_gas_comeback_req(struct p2p_data *p2p, const u8 *sa,
			     const u8 *data, size_t len, int rx_freq)
{
	struct wpabuf *resp;
	u8 dialog_token;
	size_t frag_len;
	int more = 0;

	wpa_hexdump(MSG_DEBUG, "P2P: RX GAS Comeback Request", data, len);
	if (len < 1)
		return;
	dialog_token = *data;
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Dialog Token: %u",
		dialog_token);
	if (dialog_token != p2p->sd_resp_dialog_token) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: No pending SD "
			"response fragment for dialog token %u", dialog_token);
		return;
	}

	if (p2p->sd_resp == NULL) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: No pending SD "
			"response fragment available");
		return;
	}
	if (os_memcmp(sa, p2p->sd_resp_addr, ETH_ALEN) != 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: No pending SD "
			"response fragment for " MACSTR, MAC2STR(sa));
		return;
	}

	frag_len = wpabuf_len(p2p->sd_resp) - p2p->sd_resp_pos;
	if (frag_len > 1400) {
		frag_len = 1400;
		more = 1;
	}
	resp = p2p_build_gas_comeback_resp(dialog_token, WLAN_STATUS_SUCCESS,
					   p2p->srv_update_indic,
					   wpabuf_head_u8(p2p->sd_resp) +
					   p2p->sd_resp_pos, frag_len,
					   p2p->sd_frag_id, more,
					   wpabuf_len(p2p->sd_resp));
	if (resp == NULL)
		return;
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Send GAS Comeback "
		"Response (frag_id %d more=%d frag_len=%d)",
		p2p->sd_frag_id, more, (int) frag_len);
	p2p->sd_frag_id++;
	p2p->sd_resp_pos += frag_len;

	if (more) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: %d more bytes "
			"remain to be sent",
			(int) (wpabuf_len(p2p->sd_resp) - p2p->sd_resp_pos));
	} else {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: All fragments of "
			"SD response sent");
		wpabuf_free(p2p->sd_resp);
		p2p->sd_resp = NULL;
	}

	p2p->pending_action_state = P2P_NO_PENDING_ACTION;
	if (p2p_send_action(p2p, rx_freq, sa, p2p->cfg->dev_addr,
			    p2p->cfg->dev_addr,
			    wpabuf_head(resp), wpabuf_len(resp), 200) < 0)
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Failed to send Action frame");

	wpabuf_free(resp);
}


void p2p_rx_gas_comeback_resp(struct p2p_data *p2p, const u8 *sa,
			      const u8 *data, size_t len, int rx_freq)
{
	const u8 *pos = data;
	const u8 *end = data + len;
	const u8 *next;
	u8 dialog_token;
	u16 status_code;
	u8 frag_id;
	u8 more_frags;
	u16 comeback_delay;
	u16 slen;

	wpa_hexdump(MSG_DEBUG, "P2P: RX GAS Comeback Response", data, len);

	if (p2p->state != P2P_SD_DURING_FIND || p2p->sd_peer == NULL ||
	    os_memcmp(sa, p2p->sd_peer->info.p2p_device_addr, ETH_ALEN) != 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Ignore unexpected GAS Comeback Response from "
			MACSTR, MAC2STR(sa));
		return;
	}
	p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
	p2p_clear_timeout(p2p);

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Received GAS Comeback Response from " MACSTR " (len=%d)",
		MAC2STR(sa), (int) len);

	if (len < 6 + 2) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Too short GAS Comeback Response frame");
		return;
	}

	dialog_token = *pos++;
	/* TODO: check dialog_token match */
	status_code = WPA_GET_LE16(pos);
	pos += 2;
	frag_id = *pos & 0x7f;
	more_frags = (*pos & 0x80) >> 7;
	pos++;
	comeback_delay = WPA_GET_LE16(pos);
	pos += 2;
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: dialog_token=%u status_code=%u frag_id=%d more_frags=%d "
		"comeback_delay=%u",
		dialog_token, status_code, frag_id, more_frags,
		comeback_delay);
	/* TODO: check frag_id match */
	if (status_code) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Service Discovery failed: status code %u",
			status_code);
		return;
	}

	if (*pos != WLAN_EID_ADV_PROTO) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unexpected IE in GAS Comeback Response: %u",
			*pos);
		return;
	}
	pos++;

	slen = *pos++;
	next = pos + slen;
	if (next > end || slen < 2) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Invalid IE in GAS Comeback Response");
		return;
	}
	pos++; /* skip QueryRespLenLimit and PAME-BI */

	if (*pos != NATIVE_QUERY_PROTOCOL) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unsupported GAS advertisement protocol id %u",
			*pos);
		return;
	}

	pos = next;
	/* Query Response */
	if (pos + 2 > end) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Too short Query "
			"Response");
		return;
	}
	slen = WPA_GET_LE16(pos);
	pos += 2;
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Query Response Length: %d",
		slen);
	if (pos + slen > end) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Not enough Query "
			"Response data");
		return;
	}
	if (slen == 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: No Query Response "
			"data");
		return;
	}
	end = pos + slen;

	if (p2p->sd_rx_resp) {
		 /*
		  * NQP header is only included in the first fragment; rest of
		  * the fragments start with continue TLVs.
		  */
		goto skip_nqp_header;
	}

	/* NQP Query Response */
	if (pos + 4 > end)
		return;
	if (WPA_GET_LE16(pos) != NQP_VENDOR_SPECIFIC) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unsupported NQP Info ID %u", WPA_GET_LE16(pos));
		return;
	}
	pos += 2;

	slen = WPA_GET_LE16(pos);
	pos += 2;
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: NQP Query Response "
		"length: %u", slen);
	if (slen < 3 + 1) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Invalid NQP Query Response length");
		return;
	}
	if (pos + 4 > end)
		return;

	if (WPA_GET_BE24(pos) != OUI_WFA) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unsupported NQP OUI %06x", WPA_GET_BE24(pos));
		return;
	}
	pos += 3;

	if (*pos != P2P_OUI_TYPE) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unsupported NQP vendor type %u", *pos);
		return;
	}
	pos++;

	if (pos + 2 > end)
		return;
	p2p->sd_rx_update_indic = WPA_GET_LE16(pos);
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Service Update Indicator: %u", p2p->sd_rx_update_indic);
	pos += 2;

skip_nqp_header:
	if (wpabuf_resize(&p2p->sd_rx_resp, end - pos) < 0)
		return;
	wpabuf_put_data(p2p->sd_rx_resp, pos, end - pos);
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Current SD reassembly "
		"buffer length: %u",
		(unsigned int) wpabuf_len(p2p->sd_rx_resp));

	if (more_frags) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: More fragments "
			"remains");
		/* TODO: what would be a good size limit? */
		if (wpabuf_len(p2p->sd_rx_resp) > 64000) {
			wpabuf_free(p2p->sd_rx_resp);
			p2p->sd_rx_resp = NULL;
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Too long "
				"SD response - drop it");
			return;
		}
		p2p_send_gas_comeback_req(p2p, sa, dialog_token, rx_freq);
		return;
	}

	p2p->sd_peer->flags |= P2P_DEV_SD_INFO;
	p2p->sd_peer->flags &= ~P2P_DEV_SD_SCHEDULE;
	p2p->sd_peer = NULL;

	if (p2p->sd_query) {
		if (!p2p->sd_query->for_all_peers) {
			struct p2p_sd_query *q;
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Remove completed SD query %p",
				p2p->sd_query);
			q = p2p->sd_query;
			p2p_unlink_sd_query(p2p, p2p->sd_query);
			p2p_free_sd_query(q);
		}
		p2p->sd_query = NULL;
	}

	if (p2p->cfg->sd_response)
		p2p->cfg->sd_response(p2p->cfg->cb_ctx, sa,
				      p2p->sd_rx_update_indic,
				      wpabuf_head(p2p->sd_rx_resp),
				      wpabuf_len(p2p->sd_rx_resp));
	wpabuf_free(p2p->sd_rx_resp);
	p2p->sd_rx_resp = NULL;

	p2p_continue_find(p2p);
}


void * p2p_sd_request(struct p2p_data *p2p, const u8 *dst,
		      const struct wpabuf *tlvs)
{
	struct p2p_sd_query *q;

	q = os_zalloc(sizeof(*q));
	if (q == NULL)
		return NULL;

	if (dst)
		os_memcpy(q->peer, dst, ETH_ALEN);
	else
		q->for_all_peers = 1;

	q->tlvs = wpabuf_dup(tlvs);
	if (q->tlvs == NULL) {
		p2p_free_sd_query(q);
		return NULL;
	}

	q->next = p2p->sd_queries;
	p2p->sd_queries = q;
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Added SD Query %p", q);

	return q;
}


void p2p_sd_service_update(struct p2p_data *p2p)
{
	p2p->srv_update_indic++;
}


int p2p_sd_cancel_request(struct p2p_data *p2p, void *req)
{
	if (p2p_unlink_sd_query(p2p, req)) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Cancel pending SD query %p", req);
		p2p_free_sd_query(req);
		return 0;
	}
	return -1;
}
