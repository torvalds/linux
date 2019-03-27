/*
 * Generic advertisement service (GAS) server
 * Copyright (c) 2017, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "utils/common.h"
#include "utils/list.h"
#include "utils/eloop.h"
#include "ieee802_11_defs.h"
#include "gas.h"
#include "gas_server.h"


#define MAX_ADV_PROTO_ID_LEN 10
#define GAS_QUERY_TIMEOUT 10

struct gas_server_handler {
	struct dl_list list;
	u8 adv_proto_id[MAX_ADV_PROTO_ID_LEN];
	u8 adv_proto_id_len;
	struct wpabuf * (*req_cb)(void *ctx, const u8 *sa,
				  const u8 *query, size_t query_len);
	void (*status_cb)(void *ctx, struct wpabuf *resp, int ok);
	void *ctx;
	struct gas_server *gas;
};

struct gas_server_response {
	struct dl_list list;
	size_t offset;
	u8 frag_id;
	struct wpabuf *resp;
	int freq;
	u8 dst[ETH_ALEN];
	u8 dialog_token;
	struct gas_server_handler *handler;
};

struct gas_server {
	struct dl_list handlers; /* struct gas_server_handler::list */
	struct dl_list responses; /* struct gas_server_response::list */
	void (*tx)(void *ctx, int freq, const u8 *da, struct wpabuf *resp,
		   unsigned int wait_time);
	void *ctx;
};

static void gas_server_free_response(struct gas_server_response *response);


static void gas_server_response_timeout(void *eloop_ctx, void *user_ctx)
{
	struct gas_server_response *response = eloop_ctx;

	wpa_printf(MSG_DEBUG, "GAS: Response @%p timeout for " MACSTR
		   " (dialog_token=%u freq=%d frag_id=%u sent=%lu/%lu) - drop pending data",
		   response, MAC2STR(response->dst), response->dialog_token,
		   response->freq, response->frag_id,
		   (unsigned long) response->offset,
		   (unsigned long) wpabuf_len(response->resp));
	response->handler->status_cb(response->handler->ctx,
				     response->resp, 0);
	response->resp = NULL;
	dl_list_del(&response->list);
	gas_server_free_response(response);
}


static void gas_server_free_response(struct gas_server_response *response)
{
	if (!response)
		return;
	wpa_printf(MSG_DEBUG, "DPP: Free GAS response @%p", response);
	eloop_cancel_timeout(gas_server_response_timeout, response, NULL);
	wpabuf_free(response->resp);
	os_free(response);
}


static void
gas_server_send_resp(struct gas_server *gas, struct gas_server_handler *handler,
		     const u8 *da, int freq, u8 dialog_token,
		     struct wpabuf *query_resp)
{
	size_t max_len = (freq > 56160) ? 928 : 1400;
	size_t hdr_len = 24 + 2 + 5 + 3 + handler->adv_proto_id_len + 2;
	size_t resp_frag_len;
	struct wpabuf *resp;
	u16 comeback_delay;
	struct gas_server_response *response;

	if (!query_resp)
		return;

	response = os_zalloc(sizeof(*response));
	if (!response) {
		wpabuf_free(query_resp);
		return;
	}
	wpa_printf(MSG_DEBUG, "DPP: Allocated GAS response @%p", response);
	response->freq = freq;
	response->handler = handler;
	os_memcpy(response->dst, da, ETH_ALEN);
	response->dialog_token = dialog_token;
	if (hdr_len + wpabuf_len(query_resp) > max_len) {
		/* Need to use comeback to initiate fragmentation */
		comeback_delay = 1;
		resp_frag_len = 0;
	} else {
		/* Full response fits into the initial response */
		comeback_delay = 0;
		resp_frag_len = wpabuf_len(query_resp);
	}

	resp = gas_build_initial_resp(dialog_token, WLAN_STATUS_SUCCESS,
				      comeback_delay,
				      handler->adv_proto_id_len +
				      resp_frag_len);
	if (!resp) {
		wpabuf_free(query_resp);
		gas_server_free_response(response);
		return;
	}

	/* Advertisement Protocol element */
	wpabuf_put_u8(resp, WLAN_EID_ADV_PROTO);
	wpabuf_put_u8(resp, 1 + handler->adv_proto_id_len); /* Length */
	wpabuf_put_u8(resp, 0x7f);
	/* Advertisement Protocol ID */
	wpabuf_put_data(resp, handler->adv_proto_id, handler->adv_proto_id_len);

	/* Query Response Length */
	wpabuf_put_le16(resp, resp_frag_len);
	if (!comeback_delay)
		wpabuf_put_buf(resp, query_resp);

	if (comeback_delay) {
		wpa_printf(MSG_DEBUG,
			   "GAS: Need to fragment query response");
	} else {
		wpa_printf(MSG_DEBUG,
			   "GAS: Full query response fits in the GAS Initial Response frame");
	}
	response->offset = resp_frag_len;
	response->resp = query_resp;
	dl_list_add(&gas->responses, &response->list);
	gas->tx(gas->ctx, freq, da, resp, comeback_delay ? 2000 : 0);
	wpabuf_free(resp);
	eloop_register_timeout(GAS_QUERY_TIMEOUT, 0,
			       gas_server_response_timeout, response, NULL);
}


static int
gas_server_rx_initial_req(struct gas_server *gas, const u8 *da, const u8 *sa,
			  const u8 *bssid, int freq, u8 dialog_token,
			  const u8 *data, size_t len)
{
	const u8 *pos, *end, *adv_proto, *query_req;
	u8 adv_proto_len;
	u16 query_req_len;
	struct gas_server_handler *handler;
	struct wpabuf *resp;

	wpa_hexdump(MSG_MSGDUMP, "GAS: Received GAS Initial Request frame",
		    data, len);
	pos = data;
	end = data + len;

	if (end - pos < 2 || pos[0] != WLAN_EID_ADV_PROTO) {
		wpa_printf(MSG_DEBUG,
			   "GAS: No Advertisement Protocol element found");
		return -1;
	}
	pos++;
	adv_proto_len = *pos++;
	if (end - pos < adv_proto_len || adv_proto_len < 2) {
		wpa_printf(MSG_DEBUG,
			   "GAS: Truncated Advertisement Protocol element");
		return -1;
	}

	adv_proto = pos;
	pos += adv_proto_len;
	wpa_hexdump(MSG_MSGDUMP, "GAS: Advertisement Protocol element",
		    adv_proto, adv_proto_len);

	if (end - pos < 2) {
		wpa_printf(MSG_DEBUG, "GAS: No Query Request Length field");
		return -1;
	}
	query_req_len = WPA_GET_LE16(pos);
	pos += 2;
	if (end - pos < query_req_len) {
		wpa_printf(MSG_DEBUG, "GAS: Truncated Query Request field");
		return -1;
	}
	query_req = pos;
	pos += query_req_len;
	wpa_hexdump(MSG_MSGDUMP, "GAS: Query Request",
		    query_req, query_req_len);

	if (pos < end) {
		wpa_hexdump(MSG_MSGDUMP,
			    "GAS: Ignored extra data after Query Request field",
			    pos, end - pos);
	}

	dl_list_for_each(handler, &gas->handlers, struct gas_server_handler,
			 list) {
		if (adv_proto_len < 1 + handler->adv_proto_id_len ||
		    os_memcmp(adv_proto + 1, handler->adv_proto_id,
			      handler->adv_proto_id_len) != 0)
			continue;

		wpa_printf(MSG_DEBUG,
			   "GAS: Calling handler for the requested Advertisement Protocol ID");
		resp = handler->req_cb(handler->ctx, sa, query_req,
				       query_req_len);
		wpa_hexdump_buf(MSG_MSGDUMP, "GAS: Response from the handler",
				resp);
		gas_server_send_resp(gas, handler, sa, freq, dialog_token,
				     resp);
		return 0;
	}

	wpa_printf(MSG_DEBUG,
		   "GAS: No registered handler for the requested Advertisement Protocol ID");
	return -1;
}


static void
gas_server_handle_rx_comeback_req(struct gas_server_response *response)
{
	struct gas_server_handler *handler = response->handler;
	struct gas_server *gas = handler->gas;
	size_t max_len = (response->freq > 56160) ? 928 : 1400;
	size_t hdr_len = 24 + 2 + 6 + 3 + handler->adv_proto_id_len + 2;
	size_t remaining, resp_frag_len;
	struct wpabuf *resp;

	remaining = wpabuf_len(response->resp) - response->offset;
	if (hdr_len + remaining > max_len)
		resp_frag_len = max_len - hdr_len;
	else
		resp_frag_len = remaining;
	wpa_printf(MSG_DEBUG,
		   "GAS: Sending out %u/%u remaining Query Response octets",
		   (unsigned int) resp_frag_len, (unsigned int) remaining);

	resp = gas_build_comeback_resp(response->dialog_token,
				       WLAN_STATUS_SUCCESS,
				       response->frag_id++,
				       resp_frag_len < remaining, 0,
				       handler->adv_proto_id_len +
				       resp_frag_len);
	if (!resp) {
		dl_list_del(&response->list);
		gas_server_free_response(response);
		return;
	}

	/* Advertisement Protocol element */
	wpabuf_put_u8(resp, WLAN_EID_ADV_PROTO);
	wpabuf_put_u8(resp, 1 + handler->adv_proto_id_len); /* Length */
	wpabuf_put_u8(resp, 0x7f);
	/* Advertisement Protocol ID */
	wpabuf_put_data(resp, handler->adv_proto_id, handler->adv_proto_id_len);

	/* Query Response Length */
	wpabuf_put_le16(resp, resp_frag_len);
	wpabuf_put_data(resp, wpabuf_head_u8(response->resp) + response->offset,
			resp_frag_len);

	response->offset += resp_frag_len;

	gas->tx(gas->ctx, response->freq, response->dst, resp,
		remaining > resp_frag_len ? 2000 : 0);
	wpabuf_free(resp);
}


static int
gas_server_rx_comeback_req(struct gas_server *gas, const u8 *da, const u8 *sa,
			   const u8 *bssid, int freq, u8 dialog_token)
{
	struct gas_server_response *response;

	dl_list_for_each(response, &gas->responses, struct gas_server_response,
			 list) {
		if (response->dialog_token != dialog_token ||
		    os_memcmp(sa, response->dst, ETH_ALEN) != 0)
			continue;
		gas_server_handle_rx_comeback_req(response);
		return 0;
	}

	wpa_printf(MSG_DEBUG, "GAS: No pending GAS response for " MACSTR
		   " (dialog token %u)", MAC2STR(sa), dialog_token);
	return -1;
}


/**
 * gas_query_rx - Indicate reception of a Public Action or Protected Dual frame
 * @gas: GAS query data from gas_server_init()
 * @da: Destination MAC address of the Action frame
 * @sa: Source MAC address of the Action frame
 * @bssid: BSSID of the Action frame
 * @categ: Category of the Action frame
 * @data: Payload of the Action frame
 * @len: Length of @data
 * @freq: Frequency (in MHz) on which the frame was received
 * Returns: 0 if the Public Action frame was a GAS request frame or -1 if not
 */
int gas_server_rx(struct gas_server *gas, const u8 *da, const u8 *sa,
		  const u8 *bssid, u8 categ, const u8 *data, size_t len,
		  int freq)
{
	u8 action, dialog_token;
	const u8 *pos, *end;

	if (!gas || len < 2)
		return -1;

	if (categ == WLAN_ACTION_PROTECTED_DUAL)
		return -1; /* Not supported for now */

	pos = data;
	end = data + len;
	action = *pos++;
	dialog_token = *pos++;

	if (action != WLAN_PA_GAS_INITIAL_REQ &&
	    action != WLAN_PA_GAS_COMEBACK_REQ)
		return -1; /* Not a GAS request */

	wpa_printf(MSG_DEBUG, "GAS: Received GAS %s Request frame DA=" MACSTR
		   " SA=" MACSTR " BSSID=" MACSTR
		   " freq=%d dialog_token=%u len=%u",
		   action == WLAN_PA_GAS_INITIAL_REQ ? "Initial" : "Comeback",
		   MAC2STR(da), MAC2STR(sa), MAC2STR(bssid), freq, dialog_token,
		   (unsigned int) len);

	if (action == WLAN_PA_GAS_INITIAL_REQ)
		return gas_server_rx_initial_req(gas, da, sa, bssid,
						 freq, dialog_token,
						 pos, end - pos);
	return gas_server_rx_comeback_req(gas, da, sa, bssid,
					  freq, dialog_token);
}


static void gas_server_handle_tx_status(struct gas_server_response *response,
					int ack)
{
	if (ack && response->offset < wpabuf_len(response->resp)) {
		wpa_printf(MSG_DEBUG,
			   "GAS: More fragments remaining - keep pending entry");
		return;
	}

	if (!ack)
		wpa_printf(MSG_DEBUG,
			   "GAS: No ACK received - drop pending entry");
	else
		wpa_printf(MSG_DEBUG,
			   "GAS: Last fragment of the response sent out - drop pending entry");

	response->handler->status_cb(response->handler->ctx,
				     response->resp, ack);
	response->resp = NULL;
	dl_list_del(&response->list);
	gas_server_free_response(response);
}


void gas_server_tx_status(struct gas_server *gas, const u8 *dst, const u8 *data,
			  size_t data_len, int ack)
{
	const u8 *pos;
	u8 action, code, dialog_token;
	struct gas_server_response *response;

	if (data_len < 24 + 3)
		return;
	pos = data + 24;
	action = *pos++;
	code = *pos++;
	dialog_token = *pos++;
	if (action != WLAN_ACTION_PUBLIC ||
	    (code != WLAN_PA_GAS_INITIAL_RESP &&
	     code != WLAN_PA_GAS_COMEBACK_RESP))
		return;
	wpa_printf(MSG_DEBUG, "GAS: TX status dst=" MACSTR
		   " ack=%d %s dialog_token=%u",
		   MAC2STR(dst), ack,
		   code == WLAN_PA_GAS_INITIAL_RESP ? "initial" : "comeback",
		   dialog_token);
	dl_list_for_each(response, &gas->responses, struct gas_server_response,
			 list) {
		if (response->dialog_token != dialog_token ||
		    os_memcmp(dst, response->dst, ETH_ALEN) != 0)
			continue;
		gas_server_handle_tx_status(response, ack);
		return;
	}

	wpa_printf(MSG_DEBUG, "GAS: No pending response matches TX status");
}


struct gas_server * gas_server_init(void *ctx,
				    void (*tx)(void *ctx, int freq,
					       const u8 *da,
					       struct wpabuf *buf,
					       unsigned int wait_time))
{
	struct gas_server *gas;

	gas = os_zalloc(sizeof(*gas));
	if (!gas)
		return NULL;
	gas->ctx = ctx;
	gas->tx = tx;
	dl_list_init(&gas->handlers);
	dl_list_init(&gas->responses);
	return gas;
}


void gas_server_deinit(struct gas_server *gas)
{
	struct gas_server_handler *handler, *tmp;
	struct gas_server_response *response, *tmp_r;

	if (!gas)
		return;

	dl_list_for_each_safe(handler, tmp, &gas->handlers,
			      struct gas_server_handler, list) {
		dl_list_del(&handler->list);
		os_free(handler);
	}

	dl_list_for_each_safe(response, tmp_r, &gas->responses,
			      struct gas_server_response, list) {
		dl_list_del(&response->list);
		gas_server_free_response(response);
	}

	os_free(gas);
}


int gas_server_register(struct gas_server *gas,
			const u8 *adv_proto_id, u8 adv_proto_id_len,
			struct wpabuf *
			(*req_cb)(void *ctx, const u8 *sa,
				  const u8 *query, size_t query_len),
			void (*status_cb)(void *ctx, struct wpabuf *resp,
					  int ok),
			void *ctx)
{
	struct gas_server_handler *handler;

	if (!gas || adv_proto_id_len > MAX_ADV_PROTO_ID_LEN)
		return -1;
	handler = os_zalloc(sizeof(*handler));
	if (!handler)
		return -1;

	os_memcpy(handler->adv_proto_id, adv_proto_id, adv_proto_id_len);
	handler->adv_proto_id_len = adv_proto_id_len;
	handler->req_cb = req_cb;
	handler->status_cb = status_cb;
	handler->ctx = ctx;
	handler->gas = gas;
	dl_list_add(&gas->handlers, &handler->list);

	return 0;
}
