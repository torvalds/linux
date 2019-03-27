/*
 * Generic advertisement service (GAS) (IEEE 802.11u)
 * Copyright (c) 2009, Atheros Communications
 * Copyright (c) 2011-2012, Qualcomm Atheros
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "ieee802_11_defs.h"
#include "gas.h"


static struct wpabuf *
gas_build_req(u8 action, u8 dialog_token, size_t size)
{
	struct wpabuf *buf;

	buf = wpabuf_alloc(100 + size);
	if (buf == NULL)
		return NULL;

	wpabuf_put_u8(buf, WLAN_ACTION_PUBLIC);
	wpabuf_put_u8(buf, action);
	wpabuf_put_u8(buf, dialog_token);

	return buf;
}


struct wpabuf * gas_build_initial_req(u8 dialog_token, size_t size)
{
	return gas_build_req(WLAN_PA_GAS_INITIAL_REQ, dialog_token,
			     size);
}


struct wpabuf * gas_build_comeback_req(u8 dialog_token)
{
	return gas_build_req(WLAN_PA_GAS_COMEBACK_REQ, dialog_token, 0);
}


static struct wpabuf *
gas_build_resp(u8 action, u8 dialog_token, u16 status_code, u8 frag_id,
	       u8 more, u16 comeback_delay, size_t size)
{
	struct wpabuf *buf;

	buf = wpabuf_alloc(100 + size);
	if (buf == NULL)
		return NULL;

	wpabuf_put_u8(buf, WLAN_ACTION_PUBLIC);
	wpabuf_put_u8(buf, action);
	wpabuf_put_u8(buf, dialog_token);
	wpabuf_put_le16(buf, status_code);
	if (action == WLAN_PA_GAS_COMEBACK_RESP)
		wpabuf_put_u8(buf, frag_id | (more ? 0x80 : 0));
	wpabuf_put_le16(buf, comeback_delay);

	return buf;
}


struct wpabuf *
gas_build_initial_resp(u8 dialog_token, u16 status_code, u16 comeback_delay,
		       size_t size)
{
	return gas_build_resp(WLAN_PA_GAS_INITIAL_RESP, dialog_token,
			      status_code, 0, 0, comeback_delay, size);
}


struct wpabuf *
gas_build_comeback_resp(u8 dialog_token, u16 status_code, u8 frag_id, u8 more,
			u16 comeback_delay, size_t size)
{
	return gas_build_resp(WLAN_PA_GAS_COMEBACK_RESP, dialog_token,
			      status_code, frag_id, more, comeback_delay,
			      size);
}


/**
 * gas_add_adv_proto_anqp - Add an Advertisement Protocol element
 * @buf: Buffer to which the element is added
 * @query_resp_len_limit: Query Response Length Limit in units of 256 octets
 * @pame_bi: Pre-Association Message Exchange BSSID Independent (0/1)
 *
 *
 * @query_resp_len_limit is 0 for request and 1-0x7f for response. 0x7f means
 * that the maximum limit is determined by the maximum allowable number of
 * fragments in the GAS Query Response Fragment ID.
 */
static void gas_add_adv_proto_anqp(struct wpabuf *buf, u8 query_resp_len_limit,
				   u8 pame_bi)
{
	/* Advertisement Protocol IE */
	wpabuf_put_u8(buf, WLAN_EID_ADV_PROTO);
	wpabuf_put_u8(buf, 2); /* Length */
	wpabuf_put_u8(buf, (query_resp_len_limit & 0x7f) |
		      (pame_bi ? 0x80 : 0));
	/* Advertisement Protocol */
	wpabuf_put_u8(buf, ACCESS_NETWORK_QUERY_PROTOCOL);
}


struct wpabuf * gas_anqp_build_initial_req(u8 dialog_token, size_t size)
{
	struct wpabuf *buf;

	buf = gas_build_initial_req(dialog_token, 4 + size);
	if (buf == NULL)
		return NULL;

	gas_add_adv_proto_anqp(buf, 0, 0);

	wpabuf_put(buf, 2); /* Query Request Length to be filled */

	return buf;
}


struct wpabuf * gas_anqp_build_initial_resp(u8 dialog_token, u16 status_code,
					    u16 comeback_delay, size_t size)
{
	struct wpabuf *buf;

	buf = gas_build_initial_resp(dialog_token, status_code, comeback_delay,
				     4 + size);
	if (buf == NULL)
		return NULL;

	gas_add_adv_proto_anqp(buf, 0x7f, 0);

	wpabuf_put(buf, 2); /* Query Response Length to be filled */

	return buf;
}


struct wpabuf * gas_anqp_build_initial_resp_buf(u8 dialog_token,
						u16 status_code,
						u16 comeback_delay,
						struct wpabuf *payload)
{
	struct wpabuf *buf;

	buf = gas_anqp_build_initial_resp(dialog_token, status_code,
					  comeback_delay,
					  payload ? wpabuf_len(payload) : 0);
	if (buf == NULL)
		return NULL;

	if (payload)
		wpabuf_put_buf(buf, payload);

	gas_anqp_set_len(buf);

	return buf;
}


struct wpabuf * gas_anqp_build_comeback_resp(u8 dialog_token, u16 status_code,
					     u8 frag_id, u8 more,
					     u16 comeback_delay, size_t size)
{
	struct wpabuf *buf;

	buf = gas_build_comeback_resp(dialog_token, status_code,
				      frag_id, more, comeback_delay, 4 + size);
	if (buf == NULL)
		return NULL;

	gas_add_adv_proto_anqp(buf, 0x7f, 0);

	wpabuf_put(buf, 2); /* Query Response Length to be filled */

	return buf;
}


struct wpabuf * gas_anqp_build_comeback_resp_buf(u8 dialog_token,
						 u16 status_code,
						 u8 frag_id, u8 more,
						 u16 comeback_delay,
						 struct wpabuf *payload)
{
	struct wpabuf *buf;

	buf = gas_anqp_build_comeback_resp(dialog_token, status_code, frag_id,
					   more, comeback_delay,
					   payload ? wpabuf_len(payload) : 0);
	if (buf == NULL)
		return NULL;

	if (payload)
		wpabuf_put_buf(buf, payload);

	gas_anqp_set_len(buf);

	return buf;
}


/**
 * gas_anqp_set_len - Set Query Request/Response Length
 * @buf: GAS message
 *
 * This function is used to update the Query Request/Response Length field once
 * the payload has been filled.
 */
void gas_anqp_set_len(struct wpabuf *buf)
{
	u8 action;
	size_t offset;
	u8 *len;

	if (buf == NULL || wpabuf_len(buf) < 2)
		return;

	action = *(wpabuf_head_u8(buf) + 1);
	switch (action) {
	case WLAN_PA_GAS_INITIAL_REQ:
		offset = 3 + 4;
		break;
	case WLAN_PA_GAS_INITIAL_RESP:
		offset = 7 + 4;
		break;
	case WLAN_PA_GAS_COMEBACK_RESP:
		offset = 8 + 4;
		break;
	default:
		return;
	}

	if (wpabuf_len(buf) < offset + 2)
		return;

	len = wpabuf_mhead_u8(buf) + offset;
	WPA_PUT_LE16(len, (u8 *) wpabuf_put(buf, 0) - len - 2);
}


/**
 * gas_anqp_add_element - Add ANQP element header
 * @buf: GAS message
 * @info_id: ANQP Info ID
 * Returns: Pointer to the Length field for gas_anqp_set_element_len()
 */
u8 * gas_anqp_add_element(struct wpabuf *buf, u16 info_id)
{
	wpabuf_put_le16(buf, info_id);
	return wpabuf_put(buf, 2); /* Length to be filled */
}


/**
 * gas_anqp_set_element_len - Update ANQP element Length field
 * @buf: GAS message
 * @len_pos: Length field position from gas_anqp_add_element()
 *
 * This function is called after the ANQP element payload has been added to the
 * buffer.
 */
void gas_anqp_set_element_len(struct wpabuf *buf, u8 *len_pos)
{
	WPA_PUT_LE16(len_pos, (u8 *) wpabuf_put(buf, 0) - len_pos - 2);
}
