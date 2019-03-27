/*
 * wpa_supplicant - P2P service discovery
 * Copyright (c) 2009-2010, Atheros Communications
 * Copyright (c) 2010-2014, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "p2p/p2p.h"
#include "wpa_supplicant_i.h"
#include "notify.h"
#include "p2p_supplicant.h"


/*
 * DNS Header section is used only to calculate compression pointers, so the
 * contents of this data does not matter, but the length needs to be reserved
 * in the virtual packet.
 */
#define DNS_HEADER_LEN 12

/*
 * 27-octet in-memory packet from P2P specification containing two implied
 * queries for _tcp.lcoal. PTR IN and _udp.local. PTR IN
 */
#define P2P_SD_IN_MEMORY_LEN 27

static int p2p_sd_dns_uncompress_label(char **upos, char *uend, u8 *start,
				       u8 **spos, const u8 *end)
{
	while (*spos < end) {
		u8 val = ((*spos)[0] & 0xc0) >> 6;
		int len;

		if (val == 1 || val == 2) {
			/* These are reserved values in RFC 1035 */
			wpa_printf(MSG_DEBUG, "P2P: Invalid domain name "
				   "sequence starting with 0x%x", val);
			return -1;
		}

		if (val == 3) {
			u16 offset;
			u8 *spos_tmp;

			/* Offset */
			if (end - *spos < 2) {
				wpa_printf(MSG_DEBUG, "P2P: No room for full "
					   "DNS offset field");
				return -1;
			}

			offset = (((*spos)[0] & 0x3f) << 8) | (*spos)[1];
			if (offset >= *spos - start) {
				wpa_printf(MSG_DEBUG, "P2P: Invalid DNS "
					   "pointer offset %u", offset);
				return -1;
			}

			(*spos) += 2;
			spos_tmp = start + offset;
			return p2p_sd_dns_uncompress_label(upos, uend, start,
							   &spos_tmp,
							   *spos - 2);
		}

		/* Label */
		len = (*spos)[0] & 0x3f;
		if (len == 0)
			return 0;

		(*spos)++;
		if (len > end - *spos) {
			wpa_printf(MSG_DEBUG, "P2P: Invalid domain name "
				   "sequence - no room for label with length "
				   "%u", len);
			return -1;
		}

		if (len + 2 > uend - *upos)
			return -2;

		os_memcpy(*upos, *spos, len);
		*spos += len;
		*upos += len;
		(*upos)[0] = '.';
		(*upos)++;
		(*upos)[0] = '\0';
	}

	return 0;
}


/* Uncompress domain names per RFC 1035 using the P2P SD in-memory packet.
 * Returns -1 on parsing error (invalid input sequence), -2 if output buffer is
 * not large enough */
static int p2p_sd_dns_uncompress(char *buf, size_t buf_len, const u8 *msg,
				 size_t msg_len, size_t offset)
{
	/* 27-octet in-memory packet from P2P specification */
	const char *prefix = "\x04_tcp\x05local\x00\x00\x0C\x00\x01"
		"\x04_udp\xC0\x11\x00\x0C\x00\x01";
	u8 *tmp, *end, *spos;
	char *upos, *uend;
	int ret = 0;

	if (buf_len < 2)
		return -1;
	if (offset > msg_len)
		return -1;

	tmp = os_malloc(DNS_HEADER_LEN + P2P_SD_IN_MEMORY_LEN + msg_len);
	if (tmp == NULL)
		return -1;
	spos = tmp + DNS_HEADER_LEN + P2P_SD_IN_MEMORY_LEN;
	end = spos + msg_len;
	spos += offset;

	os_memset(tmp, 0, DNS_HEADER_LEN);
	os_memcpy(tmp + DNS_HEADER_LEN, prefix, P2P_SD_IN_MEMORY_LEN);
	os_memcpy(tmp + DNS_HEADER_LEN + P2P_SD_IN_MEMORY_LEN, msg, msg_len);

	upos = buf;
	uend = buf + buf_len;

	ret = p2p_sd_dns_uncompress_label(&upos, uend, tmp, &spos, end);
	if (ret) {
		os_free(tmp);
		return ret;
	}

	if (upos == buf) {
		upos[0] = '.';
		upos[1] = '\0';
	} else if (upos[-1] == '.')
		upos[-1] = '\0';

	os_free(tmp);
	return 0;
}


static struct p2p_srv_bonjour *
wpas_p2p_service_get_bonjour(struct wpa_supplicant *wpa_s,
			     const struct wpabuf *query)
{
	struct p2p_srv_bonjour *bsrv;
	size_t len;

	len = wpabuf_len(query);
	dl_list_for_each(bsrv, &wpa_s->global->p2p_srv_bonjour,
			 struct p2p_srv_bonjour, list) {
		if (len == wpabuf_len(bsrv->query) &&
		    os_memcmp(wpabuf_head(query), wpabuf_head(bsrv->query),
			      len) == 0)
			return bsrv;
	}
	return NULL;
}


static struct p2p_srv_upnp *
wpas_p2p_service_get_upnp(struct wpa_supplicant *wpa_s, u8 version,
			  const char *service)
{
	struct p2p_srv_upnp *usrv;

	dl_list_for_each(usrv, &wpa_s->global->p2p_srv_upnp,
			 struct p2p_srv_upnp, list) {
		if (version == usrv->version &&
		    os_strcmp(service, usrv->service) == 0)
			return usrv;
	}
	return NULL;
}


static void wpas_sd_add_empty(struct wpabuf *resp, u8 srv_proto,
			      u8 srv_trans_id, u8 status)
{
	u8 *len_pos;

	if (wpabuf_tailroom(resp) < 5)
		return;

	/* Length (to be filled) */
	len_pos = wpabuf_put(resp, 2);
	wpabuf_put_u8(resp, srv_proto);
	wpabuf_put_u8(resp, srv_trans_id);
	/* Status Code */
	wpabuf_put_u8(resp, status);
	/* Response Data: empty */
	WPA_PUT_LE16(len_pos, (u8 *) wpabuf_put(resp, 0) - len_pos - 2);
}


static void wpas_sd_add_proto_not_avail(struct wpabuf *resp, u8 srv_proto,
					u8 srv_trans_id)
{
	wpas_sd_add_empty(resp, srv_proto, srv_trans_id,
			  P2P_SD_PROTO_NOT_AVAILABLE);
}


static void wpas_sd_add_bad_request(struct wpabuf *resp, u8 srv_proto,
				    u8 srv_trans_id)
{
	wpas_sd_add_empty(resp, srv_proto, srv_trans_id, P2P_SD_BAD_REQUEST);
}


static void wpas_sd_add_not_found(struct wpabuf *resp, u8 srv_proto,
				  u8 srv_trans_id)
{
	wpas_sd_add_empty(resp, srv_proto, srv_trans_id,
			  P2P_SD_REQUESTED_INFO_NOT_AVAILABLE);
}


static void wpas_sd_all_bonjour(struct wpa_supplicant *wpa_s,
				struct wpabuf *resp, u8 srv_trans_id)
{
	struct p2p_srv_bonjour *bsrv;
	u8 *len_pos;

	wpa_printf(MSG_DEBUG, "P2P: SD Request for all Bonjour services");

	if (dl_list_empty(&wpa_s->global->p2p_srv_bonjour)) {
		wpa_printf(MSG_DEBUG, "P2P: Bonjour protocol not available");
		return;
	}

	dl_list_for_each(bsrv, &wpa_s->global->p2p_srv_bonjour,
			 struct p2p_srv_bonjour, list) {
		if (wpabuf_tailroom(resp) <
		    5 + wpabuf_len(bsrv->query) + wpabuf_len(bsrv->resp))
			return;
		/* Length (to be filled) */
		len_pos = wpabuf_put(resp, 2);
		wpabuf_put_u8(resp, P2P_SERV_BONJOUR);
		wpabuf_put_u8(resp, srv_trans_id);
		/* Status Code */
		wpabuf_put_u8(resp, P2P_SD_SUCCESS);
		wpa_hexdump_ascii(MSG_DEBUG, "P2P: Matching Bonjour service",
				  wpabuf_head(bsrv->resp),
				  wpabuf_len(bsrv->resp));
		/* Response Data */
		wpabuf_put_buf(resp, bsrv->query); /* Key */
		wpabuf_put_buf(resp, bsrv->resp); /* Value */
		WPA_PUT_LE16(len_pos, (u8 *) wpabuf_put(resp, 0) - len_pos -
			     2);
	}
}


static int match_bonjour_query(struct p2p_srv_bonjour *bsrv, const u8 *query,
			       size_t query_len)
{
	char str_rx[256], str_srv[256];

	if (query_len < 3 || wpabuf_len(bsrv->query) < 3)
		return 0; /* Too short to include DNS Type and Version */
	if (os_memcmp(query + query_len - 3,
		      wpabuf_head_u8(bsrv->query) + wpabuf_len(bsrv->query) - 3,
		      3) != 0)
		return 0; /* Mismatch in DNS Type or Version */
	if (query_len == wpabuf_len(bsrv->query) &&
	    os_memcmp(query, wpabuf_head(bsrv->query), query_len - 3) == 0)
		return 1; /* Binary match */

	if (p2p_sd_dns_uncompress(str_rx, sizeof(str_rx), query, query_len - 3,
				  0))
		return 0; /* Failed to uncompress query */
	if (p2p_sd_dns_uncompress(str_srv, sizeof(str_srv),
				  wpabuf_head(bsrv->query),
				  wpabuf_len(bsrv->query) - 3, 0))
		return 0; /* Failed to uncompress service */

	return os_strcmp(str_rx, str_srv) == 0;
}


static void wpas_sd_req_bonjour(struct wpa_supplicant *wpa_s,
				struct wpabuf *resp, u8 srv_trans_id,
				const u8 *query, size_t query_len)
{
	struct p2p_srv_bonjour *bsrv;
	u8 *len_pos;
	int matches = 0;

	wpa_hexdump_ascii(MSG_DEBUG, "P2P: SD Request for Bonjour",
			  query, query_len);
	if (dl_list_empty(&wpa_s->global->p2p_srv_bonjour)) {
		wpa_printf(MSG_DEBUG, "P2P: Bonjour protocol not available");
		wpas_sd_add_proto_not_avail(resp, P2P_SERV_BONJOUR,
					    srv_trans_id);
		return;
	}

	if (query_len == 0) {
		wpas_sd_all_bonjour(wpa_s, resp, srv_trans_id);
		return;
	}

	dl_list_for_each(bsrv, &wpa_s->global->p2p_srv_bonjour,
			 struct p2p_srv_bonjour, list) {
		if (!match_bonjour_query(bsrv, query, query_len))
			continue;

		if (wpabuf_tailroom(resp) <
		    5 + query_len + wpabuf_len(bsrv->resp))
			return;

		matches++;

		/* Length (to be filled) */
		len_pos = wpabuf_put(resp, 2);
		wpabuf_put_u8(resp, P2P_SERV_BONJOUR);
		wpabuf_put_u8(resp, srv_trans_id);

		/* Status Code */
		wpabuf_put_u8(resp, P2P_SD_SUCCESS);
		wpa_hexdump_ascii(MSG_DEBUG, "P2P: Matching Bonjour service",
				  wpabuf_head(bsrv->resp),
				  wpabuf_len(bsrv->resp));

		/* Response Data */
		wpabuf_put_data(resp, query, query_len); /* Key */
		wpabuf_put_buf(resp, bsrv->resp); /* Value */

		WPA_PUT_LE16(len_pos, (u8 *) wpabuf_put(resp, 0) - len_pos - 2);
	}

	if (matches == 0) {
		wpa_printf(MSG_DEBUG, "P2P: Requested Bonjour service not "
			   "available");
		if (wpabuf_tailroom(resp) < 5)
			return;

		/* Length (to be filled) */
		len_pos = wpabuf_put(resp, 2);
		wpabuf_put_u8(resp, P2P_SERV_BONJOUR);
		wpabuf_put_u8(resp, srv_trans_id);

		/* Status Code */
		wpabuf_put_u8(resp, P2P_SD_REQUESTED_INFO_NOT_AVAILABLE);
		/* Response Data: empty */
		WPA_PUT_LE16(len_pos, (u8 *) wpabuf_put(resp, 0) - len_pos -
			     2);
	}
}


static void wpas_sd_all_upnp(struct wpa_supplicant *wpa_s,
			     struct wpabuf *resp, u8 srv_trans_id)
{
	struct p2p_srv_upnp *usrv;
	u8 *len_pos;

	wpa_printf(MSG_DEBUG, "P2P: SD Request for all UPnP services");

	if (dl_list_empty(&wpa_s->global->p2p_srv_upnp)) {
		wpa_printf(MSG_DEBUG, "P2P: UPnP protocol not available");
		return;
	}

	dl_list_for_each(usrv, &wpa_s->global->p2p_srv_upnp,
			 struct p2p_srv_upnp, list) {
		if (wpabuf_tailroom(resp) < 5 + 1 + os_strlen(usrv->service))
			return;

		/* Length (to be filled) */
		len_pos = wpabuf_put(resp, 2);
		wpabuf_put_u8(resp, P2P_SERV_UPNP);
		wpabuf_put_u8(resp, srv_trans_id);

		/* Status Code */
		wpabuf_put_u8(resp, P2P_SD_SUCCESS);
		/* Response Data */
		wpabuf_put_u8(resp, usrv->version);
		wpa_printf(MSG_DEBUG, "P2P: Matching UPnP Service: %s",
			   usrv->service);
		wpabuf_put_str(resp, usrv->service);
		WPA_PUT_LE16(len_pos, (u8 *) wpabuf_put(resp, 0) - len_pos -
			     2);
	}
}


static void wpas_sd_req_upnp(struct wpa_supplicant *wpa_s,
			     struct wpabuf *resp, u8 srv_trans_id,
			     const u8 *query, size_t query_len)
{
	struct p2p_srv_upnp *usrv;
	u8 *len_pos;
	u8 version;
	char *str;
	int count = 0;

	wpa_hexdump_ascii(MSG_DEBUG, "P2P: SD Request for UPnP",
			  query, query_len);

	if (dl_list_empty(&wpa_s->global->p2p_srv_upnp)) {
		wpa_printf(MSG_DEBUG, "P2P: UPnP protocol not available");
		wpas_sd_add_proto_not_avail(resp, P2P_SERV_UPNP,
					    srv_trans_id);
		return;
	}

	if (query_len == 0) {
		wpas_sd_all_upnp(wpa_s, resp, srv_trans_id);
		return;
	}

	if (wpabuf_tailroom(resp) < 5)
		return;

	/* Length (to be filled) */
	len_pos = wpabuf_put(resp, 2);
	wpabuf_put_u8(resp, P2P_SERV_UPNP);
	wpabuf_put_u8(resp, srv_trans_id);

	version = query[0];
	str = os_malloc(query_len);
	if (str == NULL)
		return;
	os_memcpy(str, query + 1, query_len - 1);
	str[query_len - 1] = '\0';

	dl_list_for_each(usrv, &wpa_s->global->p2p_srv_upnp,
			 struct p2p_srv_upnp, list) {
		if (version != usrv->version)
			continue;

		if (os_strcmp(str, "ssdp:all") != 0 &&
		    os_strstr(usrv->service, str) == NULL)
			continue;

		if (wpabuf_tailroom(resp) < 2)
			break;
		if (count == 0) {
			/* Status Code */
			wpabuf_put_u8(resp, P2P_SD_SUCCESS);
			/* Response Data */
			wpabuf_put_u8(resp, version);
		} else
			wpabuf_put_u8(resp, ',');

		count++;

		wpa_printf(MSG_DEBUG, "P2P: Matching UPnP Service: %s",
			   usrv->service);
		if (wpabuf_tailroom(resp) < os_strlen(usrv->service))
			break;
		wpabuf_put_str(resp, usrv->service);
	}
	os_free(str);

	if (count == 0) {
		wpa_printf(MSG_DEBUG, "P2P: Requested UPnP service not "
			   "available");
		/* Status Code */
		wpabuf_put_u8(resp, P2P_SD_REQUESTED_INFO_NOT_AVAILABLE);
		/* Response Data: empty */
	}

	WPA_PUT_LE16(len_pos, (u8 *) wpabuf_put(resp, 0) - len_pos - 2);
}


#ifdef CONFIG_WIFI_DISPLAY
static void wpas_sd_req_wfd(struct wpa_supplicant *wpa_s,
			    struct wpabuf *resp, u8 srv_trans_id,
			    const u8 *query, size_t query_len)
{
	const u8 *pos;
	u8 role;
	u8 *len_pos;

	wpa_hexdump(MSG_DEBUG, "P2P: SD Request for WFD", query, query_len);

	if (!wpa_s->global->wifi_display) {
		wpa_printf(MSG_DEBUG, "P2P: WFD protocol not available");
		wpas_sd_add_proto_not_avail(resp, P2P_SERV_WIFI_DISPLAY,
					    srv_trans_id);
		return;
	}

	if (query_len < 1) {
		wpa_printf(MSG_DEBUG, "P2P: Missing WFD Requested Device "
			   "Role");
		return;
	}

	if (wpabuf_tailroom(resp) < 5)
		return;

	pos = query;
	role = *pos++;
	wpa_printf(MSG_DEBUG, "P2P: WSD for device role 0x%x", role);

	/* TODO: role specific handling */

	/* Length (to be filled) */
	len_pos = wpabuf_put(resp, 2);
	wpabuf_put_u8(resp, P2P_SERV_WIFI_DISPLAY);
	wpabuf_put_u8(resp, srv_trans_id);
	wpabuf_put_u8(resp, P2P_SD_SUCCESS); /* Status Code */

	while (pos < query + query_len) {
		if (*pos < MAX_WFD_SUBELEMS &&
		    wpa_s->global->wfd_subelem[*pos] &&
		    wpabuf_tailroom(resp) >=
		    wpabuf_len(wpa_s->global->wfd_subelem[*pos])) {
			wpa_printf(MSG_DEBUG, "P2P: Add WSD response "
				   "subelement %u", *pos);
			wpabuf_put_buf(resp, wpa_s->global->wfd_subelem[*pos]);
		}
		pos++;
	}

	WPA_PUT_LE16(len_pos, (u8 *) wpabuf_put(resp, 0) - len_pos - 2);
}
#endif /* CONFIG_WIFI_DISPLAY */


static int find_p2ps_substr(struct p2ps_advertisement *adv_data,
			    const u8 *needle, size_t needle_len)
{
	const u8 *haystack = (const u8 *) adv_data->svc_info;
	size_t haystack_len, i;

	/* Allow search term to be empty */
	if (!needle || !needle_len)
		return 1;

	if (!haystack)
		return 0;

	haystack_len = os_strlen(adv_data->svc_info);
	for (i = 0; i < haystack_len; i++) {
		if (haystack_len - i < needle_len)
			break;
		if (os_memcmp(haystack + i, needle, needle_len) == 0)
			return 1;
	}

	return 0;
}


static void wpas_sd_req_asp(struct wpa_supplicant *wpa_s,
			    struct wpabuf *resp, u8 srv_trans_id,
			    const u8 *query, size_t query_len)
{
	struct p2ps_advertisement *adv_data;
	const u8 *svc = &query[1];
	const u8 *info = NULL;
	size_t svc_len = query[0];
	size_t info_len = 0;
	int prefix = 0;
	u8 *count_pos = NULL;
	u8 *len_pos = NULL;

	wpa_hexdump(MSG_DEBUG, "P2P: SD Request for ASP", query, query_len);

	if (!wpa_s->global->p2p) {
		wpa_printf(MSG_DEBUG, "P2P: ASP protocol not available");
		wpas_sd_add_proto_not_avail(resp, P2P_SERV_P2PS, srv_trans_id);
		return;
	}

	/* Info block is optional */
	if (svc_len + 1 < query_len) {
		info = &svc[svc_len];
		info_len = *info++;
	}

	/* Range check length of svc string and info block */
	if (svc_len + (info_len ? info_len + 2 : 1) > query_len) {
		wpa_printf(MSG_DEBUG, "P2P: ASP bad request");
		wpas_sd_add_bad_request(resp, P2P_SERV_P2PS, srv_trans_id);
		return;
	}

	/* Detect and correct for prefix search */
	if (svc_len && svc[svc_len - 1] == '*') {
		prefix = 1;
		svc_len--;
	}

	for (adv_data = p2p_get_p2ps_adv_list(wpa_s->global->p2p);
	     adv_data; adv_data = adv_data->next) {
		/* If not a prefix match, reject length mismatches */
		if (!prefix && svc_len != os_strlen(adv_data->svc_name))
			continue;

		/* Search each service for request */
		if (os_memcmp(adv_data->svc_name, svc, svc_len) == 0 &&
		    find_p2ps_substr(adv_data, info, info_len)) {
			size_t len = os_strlen(adv_data->svc_name);
			size_t svc_info_len = 0;

			if (adv_data->svc_info)
				svc_info_len = os_strlen(adv_data->svc_info);

			if (len > 0xff || svc_info_len > 0xffff)
				return;

			/* Length & Count to be filled as we go */
			if (!len_pos && !count_pos) {
				if (wpabuf_tailroom(resp) <
				    len + svc_info_len + 16)
					return;

				len_pos = wpabuf_put(resp, 2);
				wpabuf_put_u8(resp, P2P_SERV_P2PS);
				wpabuf_put_u8(resp, srv_trans_id);
				/* Status Code */
				wpabuf_put_u8(resp, P2P_SD_SUCCESS);
				count_pos = wpabuf_put(resp, 1);
				*count_pos = 0;
			} else if (wpabuf_tailroom(resp) <
				   len + svc_info_len + 10)
				return;

			if (svc_info_len) {
				wpa_printf(MSG_DEBUG,
					   "P2P: Add Svc: %s info: %s",
					   adv_data->svc_name,
					   adv_data->svc_info);
			} else {
				wpa_printf(MSG_DEBUG, "P2P: Add Svc: %s",
					   adv_data->svc_name);
			}

			/* Advertisement ID */
			wpabuf_put_le32(resp, adv_data->id);

			/* Config Methods */
			wpabuf_put_be16(resp, adv_data->config_methods);

			/* Service Name */
			wpabuf_put_u8(resp, (u8) len);
			wpabuf_put_data(resp, adv_data->svc_name, len);

			/* Service State */
			wpabuf_put_u8(resp, adv_data->state);

			/* Service Information */
			wpabuf_put_le16(resp, (u16) svc_info_len);
			wpabuf_put_data(resp, adv_data->svc_info, svc_info_len);

			/* Update length and count */
			(*count_pos)++;
			WPA_PUT_LE16(len_pos,
				     (u8 *) wpabuf_put(resp, 0) - len_pos - 2);
		}
	}

	/* Return error if no matching svc found */
	if (count_pos == NULL) {
		wpa_printf(MSG_DEBUG, "P2P: ASP service not found");
		wpas_sd_add_not_found(resp, P2P_SERV_P2PS, srv_trans_id);
	}
}


static void wpas_sd_all_asp(struct wpa_supplicant *wpa_s,
			    struct wpabuf *resp, u8 srv_trans_id)
{
	/* Query data to add all P2PS advertisements:
	 *  - Service name length: 1
	 *  - Service name: '*'
	 *  - Service Information Request Length: 0
	 */
	const u8 q[] = { 1, (const u8) '*', 0 };

	if (p2p_get_p2ps_adv_list(wpa_s->global->p2p))
		wpas_sd_req_asp(wpa_s, resp, srv_trans_id, q, sizeof(q));
}


void wpas_sd_request(void *ctx, int freq, const u8 *sa, u8 dialog_token,
		     u16 update_indic, const u8 *tlvs, size_t tlvs_len)
{
	struct wpa_supplicant *wpa_s = ctx;
	const u8 *pos = tlvs;
	const u8 *end = tlvs + tlvs_len;
	const u8 *tlv_end;
	u16 slen;
	struct wpabuf *resp;
	u8 srv_proto, srv_trans_id;
	size_t buf_len;
	char *buf;

	wpa_hexdump(MSG_MSGDUMP, "P2P: Service Discovery Request TLVs",
		    tlvs, tlvs_len);
	buf_len = 2 * tlvs_len + 1;
	buf = os_malloc(buf_len);
	if (buf) {
		wpa_snprintf_hex(buf, buf_len, tlvs, tlvs_len);
		wpa_msg_ctrl(wpa_s, MSG_INFO, P2P_EVENT_SERV_DISC_REQ "%d "
			     MACSTR " %u %u %s",
			     freq, MAC2STR(sa), dialog_token, update_indic,
			     buf);
		os_free(buf);
	}

	if (wpa_s->p2p_sd_over_ctrl_iface) {
		wpas_notify_p2p_sd_request(wpa_s, freq, sa, dialog_token,
					   update_indic, tlvs, tlvs_len);
		return; /* to be processed by an external program */
	}

	resp = wpabuf_alloc(10000);
	if (resp == NULL)
		return;

	while (end - pos > 1) {
		wpa_printf(MSG_DEBUG, "P2P: Service Request TLV");
		slen = WPA_GET_LE16(pos);
		pos += 2;
		if (slen > end - pos || slen < 2) {
			wpa_printf(MSG_DEBUG, "P2P: Unexpected Query Data "
				   "length");
			wpabuf_free(resp);
			return;
		}
		tlv_end = pos + slen;

		srv_proto = *pos++;
		wpa_printf(MSG_DEBUG, "P2P: Service Protocol Type %u",
			   srv_proto);
		srv_trans_id = *pos++;
		wpa_printf(MSG_DEBUG, "P2P: Service Transaction ID %u",
			   srv_trans_id);

		wpa_hexdump(MSG_MSGDUMP, "P2P: Query Data",
			    pos, tlv_end - pos);


		if (wpa_s->force_long_sd) {
			wpa_printf(MSG_DEBUG, "P2P: SD test - force long "
				   "response");
			wpas_sd_all_bonjour(wpa_s, resp, srv_trans_id);
			wpas_sd_all_upnp(wpa_s, resp, srv_trans_id);
			wpas_sd_all_asp(wpa_s, resp, srv_trans_id);
			goto done;
		}

		switch (srv_proto) {
		case P2P_SERV_ALL_SERVICES:
			wpa_printf(MSG_DEBUG, "P2P: Service Discovery Request "
				   "for all services");
			if (dl_list_empty(&wpa_s->global->p2p_srv_upnp) &&
			    dl_list_empty(&wpa_s->global->p2p_srv_bonjour) &&
			    !p2p_get_p2ps_adv_list(wpa_s->global->p2p)) {
				wpa_printf(MSG_DEBUG, "P2P: No service "
					   "discovery protocols available");
				wpas_sd_add_proto_not_avail(
					resp, P2P_SERV_ALL_SERVICES,
					srv_trans_id);
				break;
			}
			wpas_sd_all_bonjour(wpa_s, resp, srv_trans_id);
			wpas_sd_all_upnp(wpa_s, resp, srv_trans_id);
			wpas_sd_all_asp(wpa_s, resp, srv_trans_id);
			break;
		case P2P_SERV_BONJOUR:
			wpas_sd_req_bonjour(wpa_s, resp, srv_trans_id,
					    pos, tlv_end - pos);
			break;
		case P2P_SERV_UPNP:
			wpas_sd_req_upnp(wpa_s, resp, srv_trans_id,
					 pos, tlv_end - pos);
			break;
#ifdef CONFIG_WIFI_DISPLAY
		case P2P_SERV_WIFI_DISPLAY:
			wpas_sd_req_wfd(wpa_s, resp, srv_trans_id,
					pos, tlv_end - pos);
			break;
#endif /* CONFIG_WIFI_DISPLAY */
		case P2P_SERV_P2PS:
			wpas_sd_req_asp(wpa_s, resp, srv_trans_id,
					pos, tlv_end - pos);
			break;
		default:
			wpa_printf(MSG_DEBUG, "P2P: Unavailable service "
				   "protocol %u", srv_proto);
			wpas_sd_add_proto_not_avail(resp, srv_proto,
						    srv_trans_id);
			break;
		}

		pos = tlv_end;
	}

done:
	wpas_notify_p2p_sd_request(wpa_s, freq, sa, dialog_token,
				   update_indic, tlvs, tlvs_len);

	wpas_p2p_sd_response(wpa_s, freq, sa, dialog_token, resp);

	wpabuf_free(resp);
}


static void wpas_sd_p2ps_serv_response(struct wpa_supplicant *wpa_s,
				       const u8 *sa, u8 srv_trans_id,
				       const u8 *pos, const u8 *tlv_end)
{
	u8 left = *pos++;
	u32 adv_id;
	u8 svc_status;
	u16 config_methods;
	char svc_str[256];

	while (left-- && pos < tlv_end) {
		char *buf = NULL;
		size_t buf_len;
		u8 svc_len;

		/* Sanity check fixed length+svc_str */
		if (6 >= tlv_end - pos)
			break;
		svc_len = pos[6];
		if (svc_len + 10 > tlv_end - pos)
			break;

		/* Advertisement ID */
		adv_id = WPA_GET_LE32(pos);
		pos += sizeof(u32);

		/* Config Methods */
		config_methods = WPA_GET_BE16(pos);
		pos += sizeof(u16);

		/* Service Name */
		pos++; /* svc_len */
		os_memcpy(svc_str, pos, svc_len);
		svc_str[svc_len] = '\0';
		pos += svc_len;

		/* Service Status */
		svc_status = *pos++;

		/* Service Information Length */
		buf_len = WPA_GET_LE16(pos);
		pos += sizeof(u16);

		/* Sanity check buffer length */
		if (buf_len > (unsigned int) (tlv_end - pos))
			break;

		if (buf_len) {
			buf = os_zalloc(2 * buf_len + 1);
			if (buf) {
				utf8_escape((const char *) pos, buf_len, buf,
					    2 * buf_len + 1);
			}
		}

		pos += buf_len;

		if (buf) {
			wpa_msg_global(wpa_s, MSG_INFO, P2P_EVENT_SERV_ASP_RESP
				       MACSTR " %x %x %x %x %s '%s'",
				       MAC2STR(sa), srv_trans_id, adv_id,
				       svc_status, config_methods, svc_str,
				       buf);
			os_free(buf);
		} else {
			wpa_msg_global(wpa_s, MSG_INFO, P2P_EVENT_SERV_ASP_RESP
				       MACSTR " %x %x %x %x %s",
				       MAC2STR(sa), srv_trans_id, adv_id,
				       svc_status, config_methods, svc_str);
		}
	}
}


void wpas_sd_response(void *ctx, const u8 *sa, u16 update_indic,
		      const u8 *tlvs, size_t tlvs_len)
{
	struct wpa_supplicant *wpa_s = ctx;
	const u8 *pos = tlvs;
	const u8 *end = tlvs + tlvs_len;
	const u8 *tlv_end;
	u16 slen;
	size_t buf_len;
	char *buf;

	wpa_hexdump(MSG_MSGDUMP, "P2P: Service Discovery Response TLVs",
		    tlvs, tlvs_len);
	if (tlvs_len > 1500) {
		/* TODO: better way for handling this */
		wpa_msg_ctrl(wpa_s, MSG_INFO,
			     P2P_EVENT_SERV_DISC_RESP MACSTR
			     " %u <long response: %u bytes>",
			     MAC2STR(sa), update_indic,
			     (unsigned int) tlvs_len);
	} else {
		buf_len = 2 * tlvs_len + 1;
		buf = os_malloc(buf_len);
		if (buf) {
			wpa_snprintf_hex(buf, buf_len, tlvs, tlvs_len);
			wpa_msg_ctrl(wpa_s, MSG_INFO,
				     P2P_EVENT_SERV_DISC_RESP MACSTR " %u %s",
				     MAC2STR(sa), update_indic, buf);
			os_free(buf);
		}
	}

	while (end - pos >= 2) {
		u8 srv_proto, srv_trans_id, status;

		wpa_printf(MSG_DEBUG, "P2P: Service Response TLV");
		slen = WPA_GET_LE16(pos);
		pos += 2;
		if (slen > end - pos || slen < 3) {
			wpa_printf(MSG_DEBUG, "P2P: Unexpected Response Data "
				   "length");
			return;
		}
		tlv_end = pos + slen;

		srv_proto = *pos++;
		wpa_printf(MSG_DEBUG, "P2P: Service Protocol Type %u",
			   srv_proto);
		srv_trans_id = *pos++;
		wpa_printf(MSG_DEBUG, "P2P: Service Transaction ID %u",
			   srv_trans_id);
		status = *pos++;
		wpa_printf(MSG_DEBUG, "P2P: Status Code ID %u",
			   status);

		wpa_hexdump(MSG_MSGDUMP, "P2P: Response Data",
			    pos, tlv_end - pos);

		if (srv_proto == P2P_SERV_P2PS && pos < tlv_end) {
			wpas_sd_p2ps_serv_response(wpa_s, sa, srv_trans_id,
						   pos, tlv_end);
		}

		pos = tlv_end;
	}

	wpas_notify_p2p_sd_response(wpa_s, sa, update_indic, tlvs, tlvs_len);
}


u64 wpas_p2p_sd_request(struct wpa_supplicant *wpa_s, const u8 *dst,
			const struct wpabuf *tlvs)
{
	if (wpa_s->global->p2p_disabled || wpa_s->global->p2p == NULL)
		return 0;
	return (uintptr_t) p2p_sd_request(wpa_s->global->p2p, dst, tlvs);
}


u64 wpas_p2p_sd_request_upnp(struct wpa_supplicant *wpa_s, const u8 *dst,
			     u8 version, const char *query)
{
	struct wpabuf *tlvs;
	u64 ret;

	tlvs = wpabuf_alloc(2 + 1 + 1 + 1 + os_strlen(query));
	if (tlvs == NULL)
		return 0;
	wpabuf_put_le16(tlvs, 1 + 1 + 1 + os_strlen(query));
	wpabuf_put_u8(tlvs, P2P_SERV_UPNP); /* Service Protocol Type */
	wpabuf_put_u8(tlvs, 1); /* Service Transaction ID */
	wpabuf_put_u8(tlvs, version);
	wpabuf_put_str(tlvs, query);
	ret = wpas_p2p_sd_request(wpa_s, dst, tlvs);
	wpabuf_free(tlvs);
	return ret;
}


u64 wpas_p2p_sd_request_asp(struct wpa_supplicant *wpa_s, const u8 *dst, u8 id,
			    const char *svc_str, const char *info_substr)
{
	struct wpabuf *tlvs;
	size_t plen, svc_len, substr_len = 0;
	u64 ret;

	svc_len = os_strlen(svc_str);
	if (info_substr)
		substr_len = os_strlen(info_substr);

	if (svc_len > 0xff || substr_len > 0xff)
		return 0;

	plen = 1 + 1 + 1 + svc_len + 1 + substr_len;
	tlvs = wpabuf_alloc(2 + plen);
	if (tlvs == NULL)
		return 0;

	wpabuf_put_le16(tlvs, plen);
	wpabuf_put_u8(tlvs, P2P_SERV_P2PS);
	wpabuf_put_u8(tlvs, id); /* Service Transaction ID */
	wpabuf_put_u8(tlvs, (u8) svc_len); /* Service String Length */
	wpabuf_put_data(tlvs, svc_str, svc_len);
	wpabuf_put_u8(tlvs, (u8) substr_len); /* Info Substring Length */
	wpabuf_put_data(tlvs, info_substr, substr_len);
	ret = wpas_p2p_sd_request(wpa_s, dst, tlvs);
	wpabuf_free(tlvs);

	return ret;
}


#ifdef CONFIG_WIFI_DISPLAY

static u64 wpas_p2p_sd_request_wfd(struct wpa_supplicant *wpa_s, const u8 *dst,
				   const struct wpabuf *tlvs)
{
	if (wpa_s->global->p2p_disabled || wpa_s->global->p2p == NULL)
		return 0;
	return (uintptr_t) p2p_sd_request_wfd(wpa_s->global->p2p, dst, tlvs);
}


#define MAX_WFD_SD_SUBELEMS 20

static void wfd_add_sd_req_role(struct wpabuf *tlvs, u8 id, u8 role,
				const char *subelems)
{
	u8 *len;
	const char *pos;
	int val;
	int count = 0;

	len = wpabuf_put(tlvs, 2);
	wpabuf_put_u8(tlvs, P2P_SERV_WIFI_DISPLAY); /* Service Protocol Type */
	wpabuf_put_u8(tlvs, id); /* Service Transaction ID */

	wpabuf_put_u8(tlvs, role);

	pos = subelems;
	while (*pos) {
		val = atoi(pos);
		if (val >= 0 && val < 256) {
			wpabuf_put_u8(tlvs, val);
			count++;
			if (count == MAX_WFD_SD_SUBELEMS)
				break;
		}
		pos = os_strchr(pos + 1, ',');
		if (pos == NULL)
			break;
		pos++;
	}

	WPA_PUT_LE16(len, (u8 *) wpabuf_put(tlvs, 0) - len - 2);
}


u64 wpas_p2p_sd_request_wifi_display(struct wpa_supplicant *wpa_s,
				     const u8 *dst, const char *role)
{
	struct wpabuf *tlvs;
	u64 ret;
	const char *subelems;
	u8 id = 1;

	subelems = os_strchr(role, ' ');
	if (subelems == NULL)
		return 0;
	subelems++;

	tlvs = wpabuf_alloc(4 * (2 + 1 + 1 + 1 + MAX_WFD_SD_SUBELEMS));
	if (tlvs == NULL)
		return 0;

	if (os_strstr(role, "[source]"))
		wfd_add_sd_req_role(tlvs, id++, 0x00, subelems);
	if (os_strstr(role, "[pri-sink]"))
		wfd_add_sd_req_role(tlvs, id++, 0x01, subelems);
	if (os_strstr(role, "[sec-sink]"))
		wfd_add_sd_req_role(tlvs, id++, 0x02, subelems);
	if (os_strstr(role, "[source+sink]"))
		wfd_add_sd_req_role(tlvs, id++, 0x03, subelems);

	ret = wpas_p2p_sd_request_wfd(wpa_s, dst, tlvs);
	wpabuf_free(tlvs);
	return ret;
}

#endif /* CONFIG_WIFI_DISPLAY */


int wpas_p2p_sd_cancel_request(struct wpa_supplicant *wpa_s, u64 req)
{
	if (wpa_s->global->p2p_disabled || wpa_s->global->p2p == NULL)
		return -1;
	return p2p_sd_cancel_request(wpa_s->global->p2p,
				     (void *) (uintptr_t) req);
}


void wpas_p2p_sd_response(struct wpa_supplicant *wpa_s, int freq,
			  const u8 *dst, u8 dialog_token,
			  const struct wpabuf *resp_tlvs)
{
	if (wpa_s->global->p2p_disabled || wpa_s->global->p2p == NULL)
		return;
	p2p_sd_response(wpa_s->global->p2p, freq, dst, dialog_token,
			resp_tlvs);
}


void wpas_p2p_sd_service_update(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->global->p2p)
		p2p_sd_service_update(wpa_s->global->p2p);
}


static void wpas_p2p_srv_bonjour_free(struct p2p_srv_bonjour *bsrv)
{
	dl_list_del(&bsrv->list);
	wpabuf_free(bsrv->query);
	wpabuf_free(bsrv->resp);
	os_free(bsrv);
}


static void wpas_p2p_srv_upnp_free(struct p2p_srv_upnp *usrv)
{
	dl_list_del(&usrv->list);
	os_free(usrv->service);
	os_free(usrv);
}


void wpas_p2p_service_flush(struct wpa_supplicant *wpa_s)
{
	struct p2p_srv_bonjour *bsrv, *bn;
	struct p2p_srv_upnp *usrv, *un;

	dl_list_for_each_safe(bsrv, bn, &wpa_s->global->p2p_srv_bonjour,
			      struct p2p_srv_bonjour, list)
		wpas_p2p_srv_bonjour_free(bsrv);

	dl_list_for_each_safe(usrv, un, &wpa_s->global->p2p_srv_upnp,
			      struct p2p_srv_upnp, list)
		wpas_p2p_srv_upnp_free(usrv);

	wpas_p2p_service_flush_asp(wpa_s);
	wpas_p2p_sd_service_update(wpa_s);
}


int wpas_p2p_service_p2ps_id_exists(struct wpa_supplicant *wpa_s, u32 adv_id)
{
	if (adv_id == 0)
		return 1;

	if (p2p_service_p2ps_id(wpa_s->global->p2p, adv_id))
		return 1;

	return 0;
}


int wpas_p2p_service_del_asp(struct wpa_supplicant *wpa_s, u32 adv_id)
{
	int ret;

	ret = p2p_service_del_asp(wpa_s->global->p2p, adv_id);
	if (ret == 0)
		wpas_p2p_sd_service_update(wpa_s);
	return ret;
}


int wpas_p2p_service_add_asp(struct wpa_supplicant *wpa_s,
			     int auto_accept, u32 adv_id,
			     const char *adv_str, u8 svc_state,
			     u16 config_methods, const char *svc_info,
			     const u8 *cpt_priority)
{
	int ret;

	ret = p2p_service_add_asp(wpa_s->global->p2p, auto_accept, adv_id,
				  adv_str, svc_state, config_methods,
				  svc_info, cpt_priority);
	if (ret == 0)
		wpas_p2p_sd_service_update(wpa_s);
	return ret;
}


void wpas_p2p_service_flush_asp(struct wpa_supplicant *wpa_s)
{
	p2p_service_flush_asp(wpa_s->global->p2p);
}


int wpas_p2p_service_add_bonjour(struct wpa_supplicant *wpa_s,
				 struct wpabuf *query, struct wpabuf *resp)
{
	struct p2p_srv_bonjour *bsrv;

	bsrv = os_zalloc(sizeof(*bsrv));
	if (bsrv == NULL)
		return -1;
	bsrv->query = query;
	bsrv->resp = resp;
	dl_list_add(&wpa_s->global->p2p_srv_bonjour, &bsrv->list);

	wpas_p2p_sd_service_update(wpa_s);
	return 0;
}


int wpas_p2p_service_del_bonjour(struct wpa_supplicant *wpa_s,
				 const struct wpabuf *query)
{
	struct p2p_srv_bonjour *bsrv;

	bsrv = wpas_p2p_service_get_bonjour(wpa_s, query);
	if (bsrv == NULL)
		return -1;
	wpas_p2p_srv_bonjour_free(bsrv);
	wpas_p2p_sd_service_update(wpa_s);
	return 0;
}


int wpas_p2p_service_add_upnp(struct wpa_supplicant *wpa_s, u8 version,
			      const char *service)
{
	struct p2p_srv_upnp *usrv;

	if (wpas_p2p_service_get_upnp(wpa_s, version, service))
		return 0; /* Already listed */
	usrv = os_zalloc(sizeof(*usrv));
	if (usrv == NULL)
		return -1;
	usrv->version = version;
	usrv->service = os_strdup(service);
	if (usrv->service == NULL) {
		os_free(usrv);
		return -1;
	}
	dl_list_add(&wpa_s->global->p2p_srv_upnp, &usrv->list);

	wpas_p2p_sd_service_update(wpa_s);
	return 0;
}


int wpas_p2p_service_del_upnp(struct wpa_supplicant *wpa_s, u8 version,
			      const char *service)
{
	struct p2p_srv_upnp *usrv;

	usrv = wpas_p2p_service_get_upnp(wpa_s, version, service);
	if (usrv == NULL)
		return -1;
	wpas_p2p_srv_upnp_free(usrv);
	wpas_p2p_sd_service_update(wpa_s);
	return 0;
}
