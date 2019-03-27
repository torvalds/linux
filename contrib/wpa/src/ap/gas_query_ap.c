/*
 * Generic advertisement service (GAS) query (hostapd)
 * Copyright (c) 2009, Atheros Communications
 * Copyright (c) 2011-2017, Qualcomm Atheros, Inc.
 * Copyright (c) 2011-2014, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "utils/eloop.h"
#include "utils/list.h"
#include "common/ieee802_11_defs.h"
#include "common/gas.h"
#include "common/wpa_ctrl.h"
#include "hostapd.h"
#include "sta_info.h"
#include "ap_drv_ops.h"
#include "gas_query_ap.h"


/** GAS query timeout in seconds */
#define GAS_QUERY_TIMEOUT_PERIOD 2

/* GAS query wait-time / duration in ms */
#define GAS_QUERY_WAIT_TIME_INITIAL 1000
#define GAS_QUERY_WAIT_TIME_COMEBACK 150

/**
 * struct gas_query_pending - Pending GAS query
 */
struct gas_query_pending {
	struct dl_list list;
	struct gas_query_ap *gas;
	u8 addr[ETH_ALEN];
	u8 dialog_token;
	u8 next_frag_id;
	unsigned int wait_comeback:1;
	unsigned int offchannel_tx_started:1;
	unsigned int retry:1;
	int freq;
	u16 status_code;
	struct wpabuf *req;
	struct wpabuf *adv_proto;
	struct wpabuf *resp;
	struct os_reltime last_oper;
	void (*cb)(void *ctx, const u8 *dst, u8 dialog_token,
		   enum gas_query_ap_result result,
		   const struct wpabuf *adv_proto,
		   const struct wpabuf *resp, u16 status_code);
	void *ctx;
	u8 sa[ETH_ALEN];
};

/**
 * struct gas_query_ap - Internal GAS query data
 */
struct gas_query_ap {
	struct hostapd_data *hapd;
	void *msg_ctx;
	struct dl_list pending; /* struct gas_query_pending */
	struct gas_query_pending *current;
};


static void gas_query_tx_comeback_timeout(void *eloop_data, void *user_ctx);
static void gas_query_timeout(void *eloop_data, void *user_ctx);
static void gas_query_rx_comeback_timeout(void *eloop_data, void *user_ctx);
static void gas_query_tx_initial_req(struct gas_query_ap *gas,
				     struct gas_query_pending *query);
static int gas_query_new_dialog_token(struct gas_query_ap *gas, const u8 *dst);


static int ms_from_time(struct os_reltime *last)
{
	struct os_reltime now, res;

	os_get_reltime(&now);
	os_reltime_sub(&now, last, &res);
	return res.sec * 1000 + res.usec / 1000;
}


/**
 * gas_query_ap_init - Initialize GAS query component
 * @hapd: Pointer to hostapd data
 * Returns: Pointer to GAS query data or %NULL on failure
 */
struct gas_query_ap * gas_query_ap_init(struct hostapd_data *hapd,
					void *msg_ctx)
{
	struct gas_query_ap *gas;

	gas = os_zalloc(sizeof(*gas));
	if (!gas)
		return NULL;

	gas->hapd = hapd;
	gas->msg_ctx = msg_ctx;
	dl_list_init(&gas->pending);

	return gas;
}


static const char * gas_result_txt(enum gas_query_ap_result result)
{
	switch (result) {
	case GAS_QUERY_AP_SUCCESS:
		return "SUCCESS";
	case GAS_QUERY_AP_FAILURE:
		return "FAILURE";
	case GAS_QUERY_AP_TIMEOUT:
		return "TIMEOUT";
	case GAS_QUERY_AP_PEER_ERROR:
		return "PEER_ERROR";
	case GAS_QUERY_AP_INTERNAL_ERROR:
		return "INTERNAL_ERROR";
	case GAS_QUERY_AP_DELETED_AT_DEINIT:
		return "DELETED_AT_DEINIT";
	}

	return "N/A";
}


static void gas_query_free(struct gas_query_pending *query, int del_list)
{
	if (del_list)
		dl_list_del(&query->list);

	wpabuf_free(query->req);
	wpabuf_free(query->adv_proto);
	wpabuf_free(query->resp);
	os_free(query);
}


static void gas_query_done(struct gas_query_ap *gas,
			   struct gas_query_pending *query,
			   enum gas_query_ap_result result)
{
	wpa_msg(gas->msg_ctx, MSG_INFO, GAS_QUERY_DONE "addr=" MACSTR
		" dialog_token=%u freq=%d status_code=%u result=%s",
		MAC2STR(query->addr), query->dialog_token, query->freq,
		query->status_code, gas_result_txt(result));
	if (gas->current == query)
		gas->current = NULL;
	eloop_cancel_timeout(gas_query_tx_comeback_timeout, gas, query);
	eloop_cancel_timeout(gas_query_timeout, gas, query);
	eloop_cancel_timeout(gas_query_rx_comeback_timeout, gas, query);
	dl_list_del(&query->list);
	query->cb(query->ctx, query->addr, query->dialog_token, result,
		  query->adv_proto, query->resp, query->status_code);
	gas_query_free(query, 0);
}


/**
 * gas_query_ap_deinit - Deinitialize GAS query component
 * @gas: GAS query data from gas_query_init()
 */
void gas_query_ap_deinit(struct gas_query_ap *gas)
{
	struct gas_query_pending *query, *next;

	if (gas == NULL)
		return;

	dl_list_for_each_safe(query, next, &gas->pending,
			      struct gas_query_pending, list)
		gas_query_done(gas, query, GAS_QUERY_AP_DELETED_AT_DEINIT);

	os_free(gas);
}


static struct gas_query_pending *
gas_query_get_pending(struct gas_query_ap *gas, const u8 *addr, u8 dialog_token)
{
	struct gas_query_pending *q;
	dl_list_for_each(q, &gas->pending, struct gas_query_pending, list) {
		if (os_memcmp(q->addr, addr, ETH_ALEN) == 0 &&
		    q->dialog_token == dialog_token)
			return q;
	}
	return NULL;
}


static int gas_query_append(struct gas_query_pending *query, const u8 *data,
			    size_t len)
{
	if (wpabuf_resize(&query->resp, len) < 0) {
		wpa_printf(MSG_DEBUG, "GAS: No memory to store the response");
		return -1;
	}
	wpabuf_put_data(query->resp, data, len);
	return 0;
}


void gas_query_ap_tx_status(struct gas_query_ap *gas, const u8 *dst,
			    const u8 *data, size_t data_len, int ok)
{
	struct gas_query_pending *query;
	int dur;

	if (!gas || !gas->current) {
		wpa_printf(MSG_DEBUG, "GAS: Unexpected TX status: dst=" MACSTR
			   " ok=%d - no query in progress", MAC2STR(dst), ok);
		return;
	}

	query = gas->current;

	dur = ms_from_time(&query->last_oper);
	wpa_printf(MSG_DEBUG, "GAS: TX status: dst=" MACSTR
		   " ok=%d query=%p dialog_token=%u dur=%d ms",
		   MAC2STR(dst), ok, query, query->dialog_token, dur);
	if (os_memcmp(dst, query->addr, ETH_ALEN) != 0) {
		wpa_printf(MSG_DEBUG, "GAS: TX status for unexpected destination");
		return;
	}
	os_get_reltime(&query->last_oper);

	eloop_cancel_timeout(gas_query_timeout, gas, query);
	if (!ok) {
		wpa_printf(MSG_DEBUG, "GAS: No ACK to GAS request");
		eloop_register_timeout(0, 250000, gas_query_timeout,
				       gas, query);
	} else {
		eloop_register_timeout(GAS_QUERY_TIMEOUT_PERIOD, 0,
				       gas_query_timeout, gas, query);
	}
	if (query->wait_comeback && !query->retry) {
		eloop_cancel_timeout(gas_query_rx_comeback_timeout,
				     gas, query);
		eloop_register_timeout(
			0, (GAS_QUERY_WAIT_TIME_COMEBACK + 10) * 1000,
			gas_query_rx_comeback_timeout, gas, query);
	}
}


static int pmf_in_use(struct hostapd_data *hapd, const u8 *addr)
{
	struct sta_info *sta;

	sta = ap_get_sta(hapd, addr);
	return sta && (sta->flags & WLAN_STA_MFP);
}


static int gas_query_tx(struct gas_query_ap *gas,
			struct gas_query_pending *query,
			struct wpabuf *req, unsigned int wait_time)
{
	int res, prot = pmf_in_use(gas->hapd, query->addr);

	wpa_printf(MSG_DEBUG, "GAS: Send action frame to " MACSTR " len=%u "
		   "freq=%d prot=%d using src addr " MACSTR,
		   MAC2STR(query->addr), (unsigned int) wpabuf_len(req),
		   query->freq, prot, MAC2STR(query->sa));
	if (prot) {
		u8 *categ = wpabuf_mhead_u8(req);
		*categ = WLAN_ACTION_PROTECTED_DUAL;
	}
	os_get_reltime(&query->last_oper);
	res = hostapd_drv_send_action(gas->hapd, query->freq, wait_time,
				      query->addr, wpabuf_head(req),
				      wpabuf_len(req));
	return res;
}


static void gas_query_tx_comeback_req(struct gas_query_ap *gas,
				      struct gas_query_pending *query)
{
	struct wpabuf *req;
	unsigned int wait_time;

	req = gas_build_comeback_req(query->dialog_token);
	if (req == NULL) {
		gas_query_done(gas, query, GAS_QUERY_AP_INTERNAL_ERROR);
		return;
	}

	wait_time = (query->retry || !query->offchannel_tx_started) ?
		GAS_QUERY_WAIT_TIME_INITIAL : GAS_QUERY_WAIT_TIME_COMEBACK;

	if (gas_query_tx(gas, query, req, wait_time) < 0) {
		wpa_printf(MSG_DEBUG, "GAS: Failed to send Action frame to "
			   MACSTR, MAC2STR(query->addr));
		gas_query_done(gas, query, GAS_QUERY_AP_INTERNAL_ERROR);
	}

	wpabuf_free(req);
}


static void gas_query_rx_comeback_timeout(void *eloop_data, void *user_ctx)
{
	struct gas_query_ap *gas = eloop_data;
	struct gas_query_pending *query = user_ctx;
	int dialog_token;

	wpa_printf(MSG_DEBUG,
		   "GAS: No response to comeback request received (retry=%u)",
		   query->retry);
	if (gas->current != query || query->retry)
		return;
	dialog_token = gas_query_new_dialog_token(gas, query->addr);
	if (dialog_token < 0)
		return;
	wpa_printf(MSG_DEBUG,
		   "GAS: Retry GAS query due to comeback response timeout");
	query->retry = 1;
	query->dialog_token = dialog_token;
	*(wpabuf_mhead_u8(query->req) + 2) = dialog_token;
	query->wait_comeback = 0;
	query->next_frag_id = 0;
	wpabuf_free(query->adv_proto);
	query->adv_proto = NULL;
	eloop_cancel_timeout(gas_query_tx_comeback_timeout, gas, query);
	eloop_cancel_timeout(gas_query_timeout, gas, query);
	gas_query_tx_initial_req(gas, query);
}


static void gas_query_tx_comeback_timeout(void *eloop_data, void *user_ctx)
{
	struct gas_query_ap *gas = eloop_data;
	struct gas_query_pending *query = user_ctx;

	wpa_printf(MSG_DEBUG, "GAS: Comeback timeout for request to " MACSTR,
		   MAC2STR(query->addr));
	gas_query_tx_comeback_req(gas, query);
}


static void gas_query_tx_comeback_req_delay(struct gas_query_ap *gas,
					    struct gas_query_pending *query,
					    u16 comeback_delay)
{
	unsigned int secs, usecs;

	secs = (comeback_delay * 1024) / 1000000;
	usecs = comeback_delay * 1024 - secs * 1000000;
	wpa_printf(MSG_DEBUG, "GAS: Send comeback request to " MACSTR
		   " in %u secs %u usecs", MAC2STR(query->addr), secs, usecs);
	eloop_cancel_timeout(gas_query_tx_comeback_timeout, gas, query);
	eloop_register_timeout(secs, usecs, gas_query_tx_comeback_timeout,
			       gas, query);
}


static void gas_query_rx_initial(struct gas_query_ap *gas,
				 struct gas_query_pending *query,
				 const u8 *adv_proto, const u8 *resp,
				 size_t len, u16 comeback_delay)
{
	wpa_printf(MSG_DEBUG, "GAS: Received initial response from "
		   MACSTR " (dialog_token=%u comeback_delay=%u)",
		   MAC2STR(query->addr), query->dialog_token, comeback_delay);

	query->adv_proto = wpabuf_alloc_copy(adv_proto, 2 + adv_proto[1]);
	if (query->adv_proto == NULL) {
		gas_query_done(gas, query, GAS_QUERY_AP_INTERNAL_ERROR);
		return;
	}

	if (comeback_delay) {
		eloop_cancel_timeout(gas_query_timeout, gas, query);
		query->wait_comeback = 1;
		gas_query_tx_comeback_req_delay(gas, query, comeback_delay);
		return;
	}

	/* Query was completed without comeback mechanism */
	if (gas_query_append(query, resp, len) < 0) {
		gas_query_done(gas, query, GAS_QUERY_AP_INTERNAL_ERROR);
		return;
	}

	gas_query_done(gas, query, GAS_QUERY_AP_SUCCESS);
}


static void gas_query_rx_comeback(struct gas_query_ap *gas,
				  struct gas_query_pending *query,
				  const u8 *adv_proto, const u8 *resp,
				  size_t len, u8 frag_id, u8 more_frags,
				  u16 comeback_delay)
{
	wpa_printf(MSG_DEBUG, "GAS: Received comeback response from "
		   MACSTR " (dialog_token=%u frag_id=%u more_frags=%u "
		   "comeback_delay=%u)",
		   MAC2STR(query->addr), query->dialog_token, frag_id,
		   more_frags, comeback_delay);
	eloop_cancel_timeout(gas_query_rx_comeback_timeout, gas, query);

	if ((size_t) 2 + adv_proto[1] != wpabuf_len(query->adv_proto) ||
	    os_memcmp(adv_proto, wpabuf_head(query->adv_proto),
		      wpabuf_len(query->adv_proto)) != 0) {
		wpa_printf(MSG_DEBUG, "GAS: Advertisement Protocol changed "
			   "between initial and comeback response from "
			   MACSTR, MAC2STR(query->addr));
		gas_query_done(gas, query, GAS_QUERY_AP_PEER_ERROR);
		return;
	}

	if (comeback_delay) {
		if (frag_id) {
			wpa_printf(MSG_DEBUG, "GAS: Invalid comeback response "
				   "with non-zero frag_id and comeback_delay "
				   "from " MACSTR, MAC2STR(query->addr));
			gas_query_done(gas, query, GAS_QUERY_AP_PEER_ERROR);
			return;
		}
		gas_query_tx_comeback_req_delay(gas, query, comeback_delay);
		return;
	}

	if (frag_id != query->next_frag_id) {
		wpa_printf(MSG_DEBUG, "GAS: Unexpected frag_id in response "
			   "from " MACSTR, MAC2STR(query->addr));
		if (frag_id + 1 == query->next_frag_id) {
			wpa_printf(MSG_DEBUG, "GAS: Drop frame as possible "
				   "retry of previous fragment");
			return;
		}
		gas_query_done(gas, query, GAS_QUERY_AP_PEER_ERROR);
		return;
	}
	query->next_frag_id++;

	if (gas_query_append(query, resp, len) < 0) {
		gas_query_done(gas, query, GAS_QUERY_AP_INTERNAL_ERROR);
		return;
	}

	if (more_frags) {
		gas_query_tx_comeback_req(gas, query);
		return;
	}

	gas_query_done(gas, query, GAS_QUERY_AP_SUCCESS);
}


/**
 * gas_query_ap_rx - Indicate reception of a Public Action or Protected Dual
 *	frame
 * @gas: GAS query data from gas_query_init()
 * @sa: Source MAC address of the Action frame
 * @categ: Category of the Action frame
 * @data: Payload of the Action frame
 * @len: Length of @data
 * @freq: Frequency (in MHz) on which the frame was received
 * Returns: 0 if the Public Action frame was a GAS frame or -1 if not
 */
int gas_query_ap_rx(struct gas_query_ap *gas, const u8 *sa, u8 categ,
		    const u8 *data, size_t len, int freq)
{
	struct gas_query_pending *query;
	u8 action, dialog_token, frag_id = 0, more_frags = 0;
	u16 comeback_delay, resp_len;
	const u8 *pos, *adv_proto;
	int prot, pmf;
	unsigned int left;

	if (!gas || len < 4)
		return -1;

	pos = data;
	action = *pos++;
	dialog_token = *pos++;

	if (action != WLAN_PA_GAS_INITIAL_RESP &&
	    action != WLAN_PA_GAS_COMEBACK_RESP)
		return -1; /* Not a GAS response */

	prot = categ == WLAN_ACTION_PROTECTED_DUAL;
	pmf = pmf_in_use(gas->hapd, sa);
	if (prot && !pmf) {
		wpa_printf(MSG_DEBUG, "GAS: Drop unexpected protected GAS frame when PMF is disabled");
		return 0;
	}
	if (!prot && pmf) {
		wpa_printf(MSG_DEBUG, "GAS: Drop unexpected unprotected GAS frame when PMF is enabled");
		return 0;
	}

	query = gas_query_get_pending(gas, sa, dialog_token);
	if (query == NULL) {
		wpa_printf(MSG_DEBUG, "GAS: No pending query found for " MACSTR
			   " dialog token %u", MAC2STR(sa), dialog_token);
		return -1;
	}

	wpa_printf(MSG_DEBUG, "GAS: Response in %d ms from " MACSTR,
		   ms_from_time(&query->last_oper), MAC2STR(sa));

	if (query->wait_comeback && action == WLAN_PA_GAS_INITIAL_RESP) {
		wpa_printf(MSG_DEBUG, "GAS: Unexpected initial response from "
			   MACSTR " dialog token %u when waiting for comeback "
			   "response", MAC2STR(sa), dialog_token);
		return 0;
	}

	if (!query->wait_comeback && action == WLAN_PA_GAS_COMEBACK_RESP) {
		wpa_printf(MSG_DEBUG, "GAS: Unexpected comeback response from "
			   MACSTR " dialog token %u when waiting for initial "
			   "response", MAC2STR(sa), dialog_token);
		return 0;
	}

	query->status_code = WPA_GET_LE16(pos);
	pos += 2;

	if (query->status_code == WLAN_STATUS_QUERY_RESP_OUTSTANDING &&
	    action == WLAN_PA_GAS_COMEBACK_RESP) {
		wpa_printf(MSG_DEBUG, "GAS: Allow non-zero status for outstanding comeback response");
	} else if (query->status_code != WLAN_STATUS_SUCCESS) {
		wpa_printf(MSG_DEBUG, "GAS: Query to " MACSTR " dialog token "
			   "%u failed - status code %u",
			   MAC2STR(sa), dialog_token, query->status_code);
		gas_query_done(gas, query, GAS_QUERY_AP_FAILURE);
		return 0;
	}

	if (action == WLAN_PA_GAS_COMEBACK_RESP) {
		if (pos + 1 > data + len)
			return 0;
		frag_id = *pos & 0x7f;
		more_frags = (*pos & 0x80) >> 7;
		pos++;
	}

	/* Comeback Delay */
	if (pos + 2 > data + len)
		return 0;
	comeback_delay = WPA_GET_LE16(pos);
	pos += 2;

	/* Advertisement Protocol element */
	if (pos + 2 > data + len || pos + 2 + pos[1] > data + len) {
		wpa_printf(MSG_DEBUG, "GAS: No room for Advertisement "
			   "Protocol element in the response from " MACSTR,
			   MAC2STR(sa));
		return 0;
	}

	if (*pos != WLAN_EID_ADV_PROTO) {
		wpa_printf(MSG_DEBUG, "GAS: Unexpected Advertisement "
			   "Protocol element ID %u in response from " MACSTR,
			   *pos, MAC2STR(sa));
		return 0;
	}

	adv_proto = pos;
	pos += 2 + pos[1];

	/* Query Response Length */
	if (pos + 2 > data + len) {
		wpa_printf(MSG_DEBUG, "GAS: No room for GAS Response Length");
		return 0;
	}
	resp_len = WPA_GET_LE16(pos);
	pos += 2;

	left = data + len - pos;
	if (resp_len > left) {
		wpa_printf(MSG_DEBUG, "GAS: Truncated Query Response in "
			   "response from " MACSTR, MAC2STR(sa));
		return 0;
	}

	if (resp_len < left) {
		wpa_printf(MSG_DEBUG, "GAS: Ignore %u octets of extra data "
			   "after Query Response from " MACSTR,
			   left - resp_len, MAC2STR(sa));
	}

	if (action == WLAN_PA_GAS_COMEBACK_RESP)
		gas_query_rx_comeback(gas, query, adv_proto, pos, resp_len,
				      frag_id, more_frags, comeback_delay);
	else
		gas_query_rx_initial(gas, query, adv_proto, pos, resp_len,
				     comeback_delay);

	return 0;
}


static void gas_query_timeout(void *eloop_data, void *user_ctx)
{
	struct gas_query_ap *gas = eloop_data;
	struct gas_query_pending *query = user_ctx;

	wpa_printf(MSG_DEBUG, "GAS: No response received for query to " MACSTR
		   " dialog token %u",
		   MAC2STR(query->addr), query->dialog_token);
	gas_query_done(gas, query, GAS_QUERY_AP_TIMEOUT);
}


static int gas_query_dialog_token_available(struct gas_query_ap *gas,
					    const u8 *dst, u8 dialog_token)
{
	struct gas_query_pending *q;
	dl_list_for_each(q, &gas->pending, struct gas_query_pending, list) {
		if (os_memcmp(dst, q->addr, ETH_ALEN) == 0 &&
		    dialog_token == q->dialog_token)
			return 0;
	}

	return 1;
}


static void gas_query_tx_initial_req(struct gas_query_ap *gas,
				     struct gas_query_pending *query)
{
	if (gas_query_tx(gas, query, query->req,
			 GAS_QUERY_WAIT_TIME_INITIAL) < 0) {
		wpa_printf(MSG_DEBUG, "GAS: Failed to send Action frame to "
			   MACSTR, MAC2STR(query->addr));
		gas_query_done(gas, query, GAS_QUERY_AP_INTERNAL_ERROR);
		return;
	}
	gas->current = query;

	wpa_printf(MSG_DEBUG, "GAS: Starting query timeout for dialog token %u",
		   query->dialog_token);
	eloop_register_timeout(GAS_QUERY_TIMEOUT_PERIOD, 0,
			       gas_query_timeout, gas, query);
}


static int gas_query_new_dialog_token(struct gas_query_ap *gas, const u8 *dst)
{
	static int next_start = 0;
	int dialog_token;

	for (dialog_token = 0; dialog_token < 256; dialog_token++) {
		if (gas_query_dialog_token_available(
			    gas, dst, (next_start + dialog_token) % 256))
			break;
	}
	if (dialog_token == 256)
		return -1; /* Too many pending queries */
	dialog_token = (next_start + dialog_token) % 256;
	next_start = (dialog_token + 1) % 256;
	return dialog_token;
}


/**
 * gas_query_ap_req - Request a GAS query
 * @gas: GAS query data from gas_query_init()
 * @dst: Destination MAC address for the query
 * @freq: Frequency (in MHz) for the channel on which to send the query
 * @req: GAS query payload (to be freed by gas_query module in case of success
 *	return)
 * @cb: Callback function for reporting GAS query result and response
 * @ctx: Context pointer to use with the @cb call
 * Returns: dialog token (>= 0) on success or -1 on failure
 */
int gas_query_ap_req(struct gas_query_ap *gas, const u8 *dst, int freq,
		     struct wpabuf *req,
		     void (*cb)(void *ctx, const u8 *dst, u8 dialog_token,
				enum gas_query_ap_result result,
				const struct wpabuf *adv_proto,
				const struct wpabuf *resp, u16 status_code),
		     void *ctx)
{
	struct gas_query_pending *query;
	int dialog_token;

	if (!gas || wpabuf_len(req) < 3)
		return -1;

	dialog_token = gas_query_new_dialog_token(gas, dst);
	if (dialog_token < 0)
		return -1;

	query = os_zalloc(sizeof(*query));
	if (query == NULL)
		return -1;

	query->gas = gas;
	os_memcpy(query->addr, dst, ETH_ALEN);
	query->dialog_token = dialog_token;
	query->freq = freq;
	query->cb = cb;
	query->ctx = ctx;
	query->req = req;
	dl_list_add(&gas->pending, &query->list);

	*(wpabuf_mhead_u8(req) + 2) = dialog_token;

	wpa_msg(gas->msg_ctx, MSG_INFO, GAS_QUERY_START "addr=" MACSTR
		" dialog_token=%u freq=%d",
		MAC2STR(query->addr), query->dialog_token, query->freq);

	gas_query_tx_initial_req(gas, query);

	return dialog_token;
}
