/*
 * Wi-Fi Direct - P2P service discovery
 * Copyright (c) 2009, Atheros Communications
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "common/ieee802_11_defs.h"
#include "common/gas.h"
#include "p2p_i.h"
#include "p2p.h"


#ifdef CONFIG_WIFI_DISPLAY
static int wfd_wsd_supported(struct wpabuf *wfd)
{
	const u8 *pos, *end;
	u8 subelem;
	u16 len;

	if (wfd == NULL)
		return 0;

	pos = wpabuf_head(wfd);
	end = pos + wpabuf_len(wfd);

	while (end - pos >= 3) {
		subelem = *pos++;
		len = WPA_GET_BE16(pos);
		pos += 2;
		if (len > end - pos)
			break;

		if (subelem == WFD_SUBELEM_DEVICE_INFO && len >= 6) {
			u16 info = WPA_GET_BE16(pos);
			return !!(info & 0x0040);
		}

		pos += len;
	}

	return 0;
}
#endif /* CONFIG_WIFI_DISPLAY */

struct p2p_sd_query * p2p_pending_sd_req(struct p2p_data *p2p,
					 struct p2p_device *dev)
{
	struct p2p_sd_query *q;
	int wsd = 0;
	int count = 0;

	if (!(dev->info.dev_capab & P2P_DEV_CAPAB_SERVICE_DISCOVERY))
		return NULL; /* peer does not support SD */
#ifdef CONFIG_WIFI_DISPLAY
	if (wfd_wsd_supported(dev->info.wfd_subelems))
		wsd = 1;
#endif /* CONFIG_WIFI_DISPLAY */

	for (q = p2p->sd_queries; q; q = q->next) {
		/* Use WSD only if the peer indicates support or it */
		if (q->wsd && !wsd)
			continue;
		/* if the query is a broadcast query */
		if (q->for_all_peers) {
			/*
			 * check if there are any broadcast queries pending for
			 * this device
			 */
			if (dev->sd_pending_bcast_queries <= 0)
				return NULL;
			/* query number that needs to be send to the device */
			if (count == dev->sd_pending_bcast_queries - 1)
				goto found;
			count++;
		}
		if (!q->for_all_peers &&
		    os_memcmp(q->peer, dev->info.p2p_device_addr, ETH_ALEN) ==
		    0)
			goto found;
	}

	return NULL;

found:
	if (dev->sd_reqs > 100) {
		p2p_dbg(p2p, "Too many SD request attempts to " MACSTR
			" - skip remaining queries",
			MAC2STR(dev->info.p2p_device_addr));
		return NULL;
	}
	return q;
}


static void p2p_decrease_sd_bc_queries(struct p2p_data *p2p, int query_number)
{
	struct p2p_device *dev;

	p2p->num_p2p_sd_queries--;
	dl_list_for_each(dev, &p2p->devices, struct p2p_device, list) {
		if (query_number <= dev->sd_pending_bcast_queries - 1) {
			/*
			 * Query not yet sent to the device and it is to be
			 * removed, so update the pending count.
			*/
			dev->sd_pending_bcast_queries--;
		}
	}
}


static int p2p_unlink_sd_query(struct p2p_data *p2p,
			       struct p2p_sd_query *query)
{
	struct p2p_sd_query *q, *prev;
	int query_number = 0;

	q = p2p->sd_queries;
	prev = NULL;
	while (q) {
		if (q == query) {
			/* If the query is a broadcast query, decrease one from
			 * all the devices */
			if (query->for_all_peers)
				p2p_decrease_sd_bc_queries(p2p, query_number);
			if (prev)
				prev->next = q->next;
			else
				p2p->sd_queries = q->next;
			if (p2p->sd_query == query)
				p2p->sd_query = NULL;
			return 1;
		}
		if (q->for_all_peers)
			query_number++;
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
	p2p->num_p2p_sd_queries = 0;
}


static struct wpabuf * p2p_build_sd_query(u16 update_indic,
					  struct wpabuf *tlvs)
{
	struct wpabuf *buf;
	u8 *len_pos;

	buf = gas_anqp_build_initial_req(0, 100 + wpabuf_len(tlvs));
	if (buf == NULL)
		return NULL;

	/* ANQP Query Request Frame */
	len_pos = gas_anqp_add_element(buf, ANQP_VENDOR_SPECIFIC);
	wpabuf_put_be32(buf, P2P_IE_VENDOR_TYPE);
	wpabuf_put_le16(buf, update_indic); /* Service Update Indicator */
	wpabuf_put_buf(buf, tlvs);
	gas_anqp_set_element_len(buf, len_pos);

	gas_anqp_set_len(buf);

	return buf;
}


static void p2p_send_gas_comeback_req(struct p2p_data *p2p, const u8 *dst,
				      u8 dialog_token, int freq)
{
	struct wpabuf *req;

	req = gas_build_comeback_req(dialog_token);
	if (req == NULL)
		return;

	p2p->pending_action_state = P2P_NO_PENDING_ACTION;
	if (p2p_send_action(p2p, freq, dst, p2p->cfg->dev_addr, dst,
			    wpabuf_head(req), wpabuf_len(req), 200) < 0)
		p2p_dbg(p2p, "Failed to send Action frame");

	wpabuf_free(req);
}


static struct wpabuf * p2p_build_sd_response(u8 dialog_token, u16 status_code,
					     u16 comeback_delay,
					     u16 update_indic,
					     const struct wpabuf *tlvs)
{
	struct wpabuf *buf;
	u8 *len_pos;

	buf = gas_anqp_build_initial_resp(dialog_token, status_code,
					  comeback_delay,
					  100 + (tlvs ? wpabuf_len(tlvs) : 0));
	if (buf == NULL)
		return NULL;

	if (tlvs) {
		/* ANQP Query Response Frame */
		len_pos = gas_anqp_add_element(buf, ANQP_VENDOR_SPECIFIC);
		wpabuf_put_be32(buf, P2P_IE_VENDOR_TYPE);
		 /* Service Update Indicator */
		wpabuf_put_le16(buf, update_indic);
		wpabuf_put_buf(buf, tlvs);
		gas_anqp_set_element_len(buf, len_pos);
	}

	gas_anqp_set_len(buf);

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

	buf = gas_anqp_build_comeback_resp(dialog_token, status_code, frag_id,
					   more, 0, 100 + len);
	if (buf == NULL)
		return NULL;

	if (frag_id == 0) {
		/* ANQP Query Response Frame */
		wpabuf_put_le16(buf, ANQP_VENDOR_SPECIFIC); /* Info ID */
		wpabuf_put_le16(buf, 3 + 1 + 2 + total_len);
		wpabuf_put_be32(buf, P2P_IE_VENDOR_TYPE);
		/* Service Update Indicator */
		wpabuf_put_le16(buf, update_indic);
	}

	wpabuf_put_data(buf, data, len);
	gas_anqp_set_len(buf);

	return buf;
}


int p2p_start_sd(struct p2p_data *p2p, struct p2p_device *dev)
{
	struct wpabuf *req;
	int ret = 0;
	struct p2p_sd_query *query;
	int freq;
	unsigned int wait_time;

	freq = dev->listen_freq > 0 ? dev->listen_freq : dev->oper_freq;
	if (freq <= 0) {
		p2p_dbg(p2p, "No Listen/Operating frequency known for the peer "
			MACSTR " to send SD Request",
			MAC2STR(dev->info.p2p_device_addr));
		return -1;
	}

	query = p2p_pending_sd_req(p2p, dev);
	if (query == NULL)
		return -1;
	if (p2p->state == P2P_SEARCH &&
	    os_memcmp(p2p->sd_query_no_ack, dev->info.p2p_device_addr,
		      ETH_ALEN) == 0) {
		p2p_dbg(p2p, "Do not start Service Discovery with " MACSTR
			" due to it being the first no-ACK peer in this search iteration",
			MAC2STR(dev->info.p2p_device_addr));
		return -2;
	}

	p2p_dbg(p2p, "Start Service Discovery with " MACSTR,
		MAC2STR(dev->info.p2p_device_addr));

	req = p2p_build_sd_query(p2p->srv_update_indic, query->tlvs);
	if (req == NULL)
		return -1;

	dev->sd_reqs++;
	p2p->sd_peer = dev;
	p2p->sd_query = query;
	p2p->pending_action_state = P2P_PENDING_SD;

	wait_time = 5000;
	if (p2p->cfg->max_listen && wait_time > p2p->cfg->max_listen)
		wait_time = p2p->cfg->max_listen;
	if (p2p_send_action(p2p, freq, dev->info.p2p_device_addr,
			    p2p->cfg->dev_addr, dev->info.p2p_device_addr,
			    wpabuf_head(req), wpabuf_len(req), wait_time) < 0) {
		p2p_dbg(p2p, "Failed to send Action frame");
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
		freq = p2p_channel_to_freq(p2p->cfg->reg_class,
					   p2p->cfg->channel);
	if (freq < 0)
		return;

	if (len < 1 + 2)
		return;

	dialog_token = *pos++;
	p2p_dbg(p2p, "GAS Initial Request from " MACSTR
		" (dialog token %u, freq %d)",
		MAC2STR(sa), dialog_token, rx_freq);

	if (*pos != WLAN_EID_ADV_PROTO) {
		p2p_dbg(p2p, "Unexpected IE in GAS Initial Request: %u", *pos);
		return;
	}
	pos++;

	slen = *pos++;
	if (slen > end - pos || slen < 2) {
		p2p_dbg(p2p, "Invalid IE in GAS Initial Request");
		return;
	}
	next = pos + slen;
	pos++; /* skip QueryRespLenLimit and PAME-BI */

	if (*pos != ACCESS_NETWORK_QUERY_PROTOCOL) {
		p2p_dbg(p2p, "Unsupported GAS advertisement protocol id %u",
			*pos);
		return;
	}

	pos = next;
	/* Query Request */
	if (end - pos < 2)
		return;
	slen = WPA_GET_LE16(pos);
	pos += 2;
	if (slen > end - pos)
		return;
	end = pos + slen;

	/* ANQP Query Request */
	if (end - pos < 4)
		return;
	if (WPA_GET_LE16(pos) != ANQP_VENDOR_SPECIFIC) {
		p2p_dbg(p2p, "Unsupported ANQP Info ID %u", WPA_GET_LE16(pos));
		return;
	}
	pos += 2;

	slen = WPA_GET_LE16(pos);
	pos += 2;
	if (slen > end - pos || slen < 3 + 1) {
		p2p_dbg(p2p, "Invalid ANQP Query Request length");
		return;
	}

	if (WPA_GET_BE32(pos) != P2P_IE_VENDOR_TYPE) {
		p2p_dbg(p2p, "Unsupported ANQP vendor OUI-type %08x",
			WPA_GET_BE32(pos));
		return;
	}
	pos += 4;

	if (end - pos < 2)
		return;
	update_indic = WPA_GET_LE16(pos);
	p2p_dbg(p2p, "Service Update Indicator: %u", update_indic);
	pos += 2;

	p2p->cfg->sd_request(p2p->cfg->cb_ctx, freq, sa, dialog_token,
			     update_indic, pos, end - pos);
	/* the response will be indicated with a call to p2p_sd_response() */
}


void p2p_sd_response(struct p2p_data *p2p, int freq, const u8 *dst,
		     u8 dialog_token, const struct wpabuf *resp_tlvs)
{
	struct wpabuf *resp;
	size_t max_len;
	unsigned int wait_time = 200;

	/*
	 * In the 60 GHz, we have a smaller maximum frame length for management
	 * frames.
	 */
	max_len = (freq > 56160) ? 928 : 1400;

	/* TODO: fix the length limit to match with the maximum frame length */
	if (wpabuf_len(resp_tlvs) > max_len) {
		p2p_dbg(p2p, "SD response long enough to require fragmentation");
		if (p2p->sd_resp) {
			/*
			 * TODO: Could consider storing the fragmented response
			 * separately for each peer to avoid having to drop old
			 * one if there is more than one pending SD query.
			 * Though, that would eat more memory, so there are
			 * also benefits to just using a single buffer.
			 */
			p2p_dbg(p2p, "Drop previous SD response");
			wpabuf_free(p2p->sd_resp);
		}
		p2p->sd_resp = wpabuf_dup(resp_tlvs);
		if (p2p->sd_resp == NULL) {
			p2p_err(p2p, "Failed to allocate SD response fragmentation area");
			return;
		}
		os_memcpy(p2p->sd_resp_addr, dst, ETH_ALEN);
		p2p->sd_resp_dialog_token = dialog_token;
		p2p->sd_resp_pos = 0;
		p2p->sd_frag_id = 0;
		resp = p2p_build_sd_response(dialog_token, WLAN_STATUS_SUCCESS,
					     1, p2p->srv_update_indic, NULL);
	} else {
		p2p_dbg(p2p, "SD response fits in initial response");
		wait_time = 0; /* no more SD frames in the sequence */
		resp = p2p_build_sd_response(dialog_token,
					     WLAN_STATUS_SUCCESS, 0,
					     p2p->srv_update_indic, resp_tlvs);
	}
	if (resp == NULL)
		return;

	p2p->pending_action_state = P2P_NO_PENDING_ACTION;
	if (p2p_send_action(p2p, freq, dst, p2p->cfg->dev_addr,
			    p2p->cfg->dev_addr,
			    wpabuf_head(resp), wpabuf_len(resp), wait_time) < 0)
		p2p_dbg(p2p, "Failed to send Action frame");

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
		p2p_dbg(p2p, "Ignore unexpected GAS Initial Response from "
			MACSTR, MAC2STR(sa));
		return;
	}
	p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
	p2p_clear_timeout(p2p);

	p2p_dbg(p2p, "Received GAS Initial Response from " MACSTR " (len=%d)",
		MAC2STR(sa), (int) len);

	if (len < 5 + 2) {
		p2p_dbg(p2p, "Too short GAS Initial Response frame");
		return;
	}

	dialog_token = *pos++;
	/* TODO: check dialog_token match */
	status_code = WPA_GET_LE16(pos);
	pos += 2;
	comeback_delay = WPA_GET_LE16(pos);
	pos += 2;
	p2p_dbg(p2p, "dialog_token=%u status_code=%u comeback_delay=%u",
		dialog_token, status_code, comeback_delay);
	if (status_code) {
		p2p_dbg(p2p, "Service Discovery failed: status code %u",
			status_code);
		return;
	}

	if (*pos != WLAN_EID_ADV_PROTO) {
		p2p_dbg(p2p, "Unexpected IE in GAS Initial Response: %u", *pos);
		return;
	}
	pos++;

	slen = *pos++;
	if (slen > end - pos || slen < 2) {
		p2p_dbg(p2p, "Invalid IE in GAS Initial Response");
		return;
	}
	next = pos + slen;
	pos++; /* skip QueryRespLenLimit and PAME-BI */

	if (*pos != ACCESS_NETWORK_QUERY_PROTOCOL) {
		p2p_dbg(p2p, "Unsupported GAS advertisement protocol id %u",
			*pos);
		return;
	}

	pos = next;
	/* Query Response */
	if (end - pos < 2) {
		p2p_dbg(p2p, "Too short Query Response");
		return;
	}
	slen = WPA_GET_LE16(pos);
	pos += 2;
	p2p_dbg(p2p, "Query Response Length: %d", slen);
	if (slen > end - pos) {
		p2p_dbg(p2p, "Not enough Query Response data");
		return;
	}
	end = pos + slen;

	if (comeback_delay) {
		p2p_dbg(p2p, "Fragmented response - request fragments");
		if (p2p->sd_rx_resp) {
			p2p_dbg(p2p, "Drop old SD reassembly buffer");
			wpabuf_free(p2p->sd_rx_resp);
			p2p->sd_rx_resp = NULL;
		}
		p2p_send_gas_comeback_req(p2p, sa, dialog_token, rx_freq);
		return;
	}

	/* ANQP Query Response */
	if (end - pos < 4)
		return;
	if (WPA_GET_LE16(pos) != ANQP_VENDOR_SPECIFIC) {
		p2p_dbg(p2p, "Unsupported ANQP Info ID %u", WPA_GET_LE16(pos));
		return;
	}
	pos += 2;

	slen = WPA_GET_LE16(pos);
	pos += 2;
	if (slen > end - pos || slen < 3 + 1) {
		p2p_dbg(p2p, "Invalid ANQP Query Response length");
		return;
	}

	if (WPA_GET_BE32(pos) != P2P_IE_VENDOR_TYPE) {
		p2p_dbg(p2p, "Unsupported ANQP vendor OUI-type %08x",
			WPA_GET_BE32(pos));
		return;
	}
	pos += 4;

	if (end - pos < 2)
		return;
	update_indic = WPA_GET_LE16(pos);
	p2p_dbg(p2p, "Service Update Indicator: %u", update_indic);
	pos += 2;

	p2p->sd_peer = NULL;

	if (p2p->sd_query) {
		if (!p2p->sd_query->for_all_peers) {
			struct p2p_sd_query *q;
			p2p_dbg(p2p, "Remove completed SD query %p",
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
	size_t frag_len, max_len;
	int more = 0;
	unsigned int wait_time = 200;

	wpa_hexdump(MSG_DEBUG, "P2P: RX GAS Comeback Request", data, len);
	if (len < 1)
		return;
	dialog_token = *data;
	p2p_dbg(p2p, "Dialog Token: %u", dialog_token);
	if (dialog_token != p2p->sd_resp_dialog_token) {
		p2p_dbg(p2p, "No pending SD response fragment for dialog token %u",
			dialog_token);
		return;
	}

	if (p2p->sd_resp == NULL) {
		p2p_dbg(p2p, "No pending SD response fragment available");
		return;
	}
	if (os_memcmp(sa, p2p->sd_resp_addr, ETH_ALEN) != 0) {
		p2p_dbg(p2p, "No pending SD response fragment for " MACSTR,
			MAC2STR(sa));
		return;
	}

	/*
	 * In the 60 GHz, we have a smaller maximum frame length for management
	 * frames.
	 */
	max_len = (rx_freq > 56160) ? 928 : 1400;
	frag_len = wpabuf_len(p2p->sd_resp) - p2p->sd_resp_pos;
	if (frag_len > max_len) {
		frag_len = max_len;
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
	p2p_dbg(p2p, "Send GAS Comeback Response (frag_id %d more=%d frag_len=%d)",
		p2p->sd_frag_id, more, (int) frag_len);
	p2p->sd_frag_id++;
	p2p->sd_resp_pos += frag_len;

	if (more) {
		p2p_dbg(p2p, "%d more bytes remain to be sent",
			(int) (wpabuf_len(p2p->sd_resp) - p2p->sd_resp_pos));
	} else {
		p2p_dbg(p2p, "All fragments of SD response sent");
		wpabuf_free(p2p->sd_resp);
		p2p->sd_resp = NULL;
		wait_time = 0; /* no more SD frames in the sequence */
	}

	p2p->pending_action_state = P2P_NO_PENDING_ACTION;
	if (p2p_send_action(p2p, rx_freq, sa, p2p->cfg->dev_addr,
			    p2p->cfg->dev_addr,
			    wpabuf_head(resp), wpabuf_len(resp), wait_time) < 0)
		p2p_dbg(p2p, "Failed to send Action frame");

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
		p2p_dbg(p2p, "Ignore unexpected GAS Comeback Response from "
			MACSTR, MAC2STR(sa));
		return;
	}
	p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
	p2p_clear_timeout(p2p);

	p2p_dbg(p2p, "Received GAS Comeback Response from " MACSTR " (len=%d)",
		MAC2STR(sa), (int) len);

	if (len < 6 + 2) {
		p2p_dbg(p2p, "Too short GAS Comeback Response frame");
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
	p2p_dbg(p2p, "dialog_token=%u status_code=%u frag_id=%d more_frags=%d "
		"comeback_delay=%u",
		dialog_token, status_code, frag_id, more_frags,
		comeback_delay);
	/* TODO: check frag_id match */
	if (status_code) {
		p2p_dbg(p2p, "Service Discovery failed: status code %u",
			status_code);
		return;
	}

	if (*pos != WLAN_EID_ADV_PROTO) {
		p2p_dbg(p2p, "Unexpected IE in GAS Comeback Response: %u",
			*pos);
		return;
	}
	pos++;

	slen = *pos++;
	if (slen > end - pos || slen < 2) {
		p2p_dbg(p2p, "Invalid IE in GAS Comeback Response");
		return;
	}
	next = pos + slen;
	pos++; /* skip QueryRespLenLimit and PAME-BI */

	if (*pos != ACCESS_NETWORK_QUERY_PROTOCOL) {
		p2p_dbg(p2p, "Unsupported GAS advertisement protocol id %u",
			*pos);
		return;
	}

	pos = next;
	/* Query Response */
	if (end - pos < 2) {
		p2p_dbg(p2p, "Too short Query Response");
		return;
	}
	slen = WPA_GET_LE16(pos);
	pos += 2;
	p2p_dbg(p2p, "Query Response Length: %d", slen);
	if (slen > end - pos) {
		p2p_dbg(p2p, "Not enough Query Response data");
		return;
	}
	if (slen == 0) {
		p2p_dbg(p2p, "No Query Response data");
		return;
	}
	end = pos + slen;

	if (p2p->sd_rx_resp) {
		 /*
		  * ANQP header is only included in the first fragment; rest of
		  * the fragments start with continue TLVs.
		  */
		goto skip_nqp_header;
	}

	/* ANQP Query Response */
	if (end - pos < 4)
		return;
	if (WPA_GET_LE16(pos) != ANQP_VENDOR_SPECIFIC) {
		p2p_dbg(p2p, "Unsupported ANQP Info ID %u", WPA_GET_LE16(pos));
		return;
	}
	pos += 2;

	slen = WPA_GET_LE16(pos);
	pos += 2;
	p2p_dbg(p2p, "ANQP Query Response length: %u", slen);
	if (slen < 3 + 1) {
		p2p_dbg(p2p, "Invalid ANQP Query Response length");
		return;
	}
	if (end - pos < 4)
		return;

	if (WPA_GET_BE32(pos) != P2P_IE_VENDOR_TYPE) {
		p2p_dbg(p2p, "Unsupported ANQP vendor OUI-type %08x",
			WPA_GET_BE32(pos));
		return;
	}
	pos += 4;

	if (end - pos < 2)
		return;
	p2p->sd_rx_update_indic = WPA_GET_LE16(pos);
	p2p_dbg(p2p, "Service Update Indicator: %u", p2p->sd_rx_update_indic);
	pos += 2;

skip_nqp_header:
	if (wpabuf_resize(&p2p->sd_rx_resp, end - pos) < 0)
		return;
	wpabuf_put_data(p2p->sd_rx_resp, pos, end - pos);
	p2p_dbg(p2p, "Current SD reassembly buffer length: %u",
		(unsigned int) wpabuf_len(p2p->sd_rx_resp));

	if (more_frags) {
		p2p_dbg(p2p, "More fragments remains");
		/* TODO: what would be a good size limit? */
		if (wpabuf_len(p2p->sd_rx_resp) > 64000) {
			wpabuf_free(p2p->sd_rx_resp);
			p2p->sd_rx_resp = NULL;
			p2p_dbg(p2p, "Too long SD response - drop it");
			return;
		}
		p2p_send_gas_comeback_req(p2p, sa, dialog_token, rx_freq);
		return;
	}

	p2p->sd_peer = NULL;

	if (p2p->sd_query) {
		if (!p2p->sd_query->for_all_peers) {
			struct p2p_sd_query *q;
			p2p_dbg(p2p, "Remove completed SD query %p",
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
	p2p_dbg(p2p, "Added SD Query %p", q);

	if (dst == NULL) {
		struct p2p_device *dev;

		p2p->num_p2p_sd_queries++;

		/* Update all the devices for the newly added broadcast query */
		dl_list_for_each(dev, &p2p->devices, struct p2p_device, list) {
			if (dev->sd_pending_bcast_queries <= 0)
				dev->sd_pending_bcast_queries = 1;
			else
				dev->sd_pending_bcast_queries++;
		}
	}

	return q;
}


#ifdef CONFIG_WIFI_DISPLAY
void * p2p_sd_request_wfd(struct p2p_data *p2p, const u8 *dst,
			  const struct wpabuf *tlvs)
{
	struct p2p_sd_query *q;
	q = p2p_sd_request(p2p, dst, tlvs);
	if (q)
		q->wsd = 1;
	return q;
}
#endif /* CONFIG_WIFI_DISPLAY */


void p2p_sd_service_update(struct p2p_data *p2p)
{
	p2p->srv_update_indic++;
}


int p2p_sd_cancel_request(struct p2p_data *p2p, void *req)
{
	if (p2p_unlink_sd_query(p2p, req)) {
		p2p_dbg(p2p, "Cancel pending SD query %p", req);
		p2p_free_sd_query(req);
		return 0;
	}
	return -1;
}
