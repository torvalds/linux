/*
 * RADIUS Dynamic Authorization Server (DAS) (RFC 5176)
 * Copyright (c) 2012-2013, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <net/if.h>

#include "utils/common.h"
#include "utils/eloop.h"
#include "utils/ip_addr.h"
#include "radius.h"
#include "radius_das.h"


struct radius_das_data {
	int sock;
	u8 *shared_secret;
	size_t shared_secret_len;
	struct hostapd_ip_addr client_addr;
	unsigned int time_window;
	int require_event_timestamp;
	int require_message_authenticator;
	void *ctx;
	enum radius_das_res (*disconnect)(void *ctx,
					  struct radius_das_attrs *attr);
	enum radius_das_res (*coa)(void *ctx, struct radius_das_attrs *attr);
};


static struct radius_msg * radius_das_disconnect(struct radius_das_data *das,
						 struct radius_msg *msg,
						 const char *abuf,
						 int from_port)
{
	struct radius_hdr *hdr;
	struct radius_msg *reply;
	u8 allowed[] = {
		RADIUS_ATTR_USER_NAME,
		RADIUS_ATTR_NAS_IP_ADDRESS,
		RADIUS_ATTR_CALLING_STATION_ID,
		RADIUS_ATTR_NAS_IDENTIFIER,
		RADIUS_ATTR_ACCT_SESSION_ID,
		RADIUS_ATTR_ACCT_MULTI_SESSION_ID,
		RADIUS_ATTR_EVENT_TIMESTAMP,
		RADIUS_ATTR_MESSAGE_AUTHENTICATOR,
		RADIUS_ATTR_CHARGEABLE_USER_IDENTITY,
#ifdef CONFIG_IPV6
		RADIUS_ATTR_NAS_IPV6_ADDRESS,
#endif /* CONFIG_IPV6 */
		0
	};
	int error = 405;
	u8 attr;
	enum radius_das_res res;
	struct radius_das_attrs attrs;
	u8 *buf;
	size_t len;
	char tmp[100];
	u8 sta_addr[ETH_ALEN];

	hdr = radius_msg_get_hdr(msg);

	attr = radius_msg_find_unlisted_attr(msg, allowed);
	if (attr) {
		wpa_printf(MSG_INFO, "DAS: Unsupported attribute %u in "
			   "Disconnect-Request from %s:%d", attr,
			   abuf, from_port);
		error = 401;
		goto fail;
	}

	os_memset(&attrs, 0, sizeof(attrs));

	if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_NAS_IP_ADDRESS,
				    &buf, &len, NULL) == 0) {
		if (len != 4) {
			wpa_printf(MSG_INFO, "DAS: Invalid NAS-IP-Address from %s:%d",
				   abuf, from_port);
			error = 407;
			goto fail;
		}
		attrs.nas_ip_addr = buf;
	}

#ifdef CONFIG_IPV6
	if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_NAS_IPV6_ADDRESS,
				    &buf, &len, NULL) == 0) {
		if (len != 16) {
			wpa_printf(MSG_INFO, "DAS: Invalid NAS-IPv6-Address from %s:%d",
				   abuf, from_port);
			error = 407;
			goto fail;
		}
		attrs.nas_ipv6_addr = buf;
	}
#endif /* CONFIG_IPV6 */

	if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_NAS_IDENTIFIER,
				    &buf, &len, NULL) == 0) {
		attrs.nas_identifier = buf;
		attrs.nas_identifier_len = len;
	}

	if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_CALLING_STATION_ID,
				    &buf, &len, NULL) == 0) {
		if (len >= sizeof(tmp))
			len = sizeof(tmp) - 1;
		os_memcpy(tmp, buf, len);
		tmp[len] = '\0';
		if (hwaddr_aton2(tmp, sta_addr) < 0) {
			wpa_printf(MSG_INFO, "DAS: Invalid Calling-Station-Id "
				   "'%s' from %s:%d", tmp, abuf, from_port);
			error = 407;
			goto fail;
		}
		attrs.sta_addr = sta_addr;
	}

	if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_USER_NAME,
				    &buf, &len, NULL) == 0) {
		attrs.user_name = buf;
		attrs.user_name_len = len;
	}

	if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_ACCT_SESSION_ID,
				    &buf, &len, NULL) == 0) {
		attrs.acct_session_id = buf;
		attrs.acct_session_id_len = len;
	}

	if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_ACCT_MULTI_SESSION_ID,
				    &buf, &len, NULL) == 0) {
		attrs.acct_multi_session_id = buf;
		attrs.acct_multi_session_id_len = len;
	}

	if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_CHARGEABLE_USER_IDENTITY,
				    &buf, &len, NULL) == 0) {
		attrs.cui = buf;
		attrs.cui_len = len;
	}

	res = das->disconnect(das->ctx, &attrs);
	switch (res) {
	case RADIUS_DAS_NAS_MISMATCH:
		wpa_printf(MSG_INFO, "DAS: NAS mismatch from %s:%d",
			   abuf, from_port);
		error = 403;
		break;
	case RADIUS_DAS_SESSION_NOT_FOUND:
		wpa_printf(MSG_INFO, "DAS: Session not found for request from "
			   "%s:%d", abuf, from_port);
		error = 503;
		break;
	case RADIUS_DAS_MULTI_SESSION_MATCH:
		wpa_printf(MSG_INFO,
			   "DAS: Multiple sessions match for request from %s:%d",
			   abuf, from_port);
		error = 508;
		break;
	case RADIUS_DAS_COA_FAILED:
		/* not used with Disconnect-Request */
		error = 405;
		break;
	case RADIUS_DAS_SUCCESS:
		error = 0;
		break;
	}

fail:
	reply = radius_msg_new(error ? RADIUS_CODE_DISCONNECT_NAK :
			       RADIUS_CODE_DISCONNECT_ACK, hdr->identifier);
	if (reply == NULL)
		return NULL;

	if (error) {
		if (!radius_msg_add_attr_int32(reply, RADIUS_ATTR_ERROR_CAUSE,
					       error)) {
			radius_msg_free(reply);
			return NULL;
		}
	}

	return reply;
}


static struct radius_msg * radius_das_coa(struct radius_das_data *das,
					  struct radius_msg *msg,
					  const char *abuf, int from_port)
{
	struct radius_hdr *hdr;
	struct radius_msg *reply;
	u8 allowed[] = {
		RADIUS_ATTR_USER_NAME,
		RADIUS_ATTR_NAS_IP_ADDRESS,
		RADIUS_ATTR_CALLING_STATION_ID,
		RADIUS_ATTR_NAS_IDENTIFIER,
		RADIUS_ATTR_ACCT_SESSION_ID,
		RADIUS_ATTR_ACCT_MULTI_SESSION_ID,
		RADIUS_ATTR_EVENT_TIMESTAMP,
		RADIUS_ATTR_MESSAGE_AUTHENTICATOR,
		RADIUS_ATTR_CHARGEABLE_USER_IDENTITY,
#ifdef CONFIG_HS20
		RADIUS_ATTR_VENDOR_SPECIFIC,
#endif /* CONFIG_HS20 */
#ifdef CONFIG_IPV6
		RADIUS_ATTR_NAS_IPV6_ADDRESS,
#endif /* CONFIG_IPV6 */
		0
	};
	int error = 405;
	u8 attr;
	enum radius_das_res res;
	struct radius_das_attrs attrs;
	u8 *buf;
	size_t len;
	char tmp[100];
	u8 sta_addr[ETH_ALEN];

	hdr = radius_msg_get_hdr(msg);

	if (!das->coa) {
		wpa_printf(MSG_INFO, "DAS: CoA not supported");
		goto fail;
	}

	attr = radius_msg_find_unlisted_attr(msg, allowed);
	if (attr) {
		wpa_printf(MSG_INFO,
			   "DAS: Unsupported attribute %u in CoA-Request from %s:%d",
			   attr, abuf, from_port);
		error = 401;
		goto fail;
	}

	os_memset(&attrs, 0, sizeof(attrs));

	if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_NAS_IP_ADDRESS,
				    &buf, &len, NULL) == 0) {
		if (len != 4) {
			wpa_printf(MSG_INFO, "DAS: Invalid NAS-IP-Address from %s:%d",
				   abuf, from_port);
			error = 407;
			goto fail;
		}
		attrs.nas_ip_addr = buf;
	}

#ifdef CONFIG_IPV6
	if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_NAS_IPV6_ADDRESS,
				    &buf, &len, NULL) == 0) {
		if (len != 16) {
			wpa_printf(MSG_INFO, "DAS: Invalid NAS-IPv6-Address from %s:%d",
				   abuf, from_port);
			error = 407;
			goto fail;
		}
		attrs.nas_ipv6_addr = buf;
	}
#endif /* CONFIG_IPV6 */

	if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_NAS_IDENTIFIER,
				    &buf, &len, NULL) == 0) {
		attrs.nas_identifier = buf;
		attrs.nas_identifier_len = len;
	}

	if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_CALLING_STATION_ID,
				    &buf, &len, NULL) == 0) {
		if (len >= sizeof(tmp))
			len = sizeof(tmp) - 1;
		os_memcpy(tmp, buf, len);
		tmp[len] = '\0';
		if (hwaddr_aton2(tmp, sta_addr) < 0) {
			wpa_printf(MSG_INFO, "DAS: Invalid Calling-Station-Id "
				   "'%s' from %s:%d", tmp, abuf, from_port);
			error = 407;
			goto fail;
		}
		attrs.sta_addr = sta_addr;
	}

	if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_USER_NAME,
				    &buf, &len, NULL) == 0) {
		attrs.user_name = buf;
		attrs.user_name_len = len;
	}

	if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_ACCT_SESSION_ID,
				    &buf, &len, NULL) == 0) {
		attrs.acct_session_id = buf;
		attrs.acct_session_id_len = len;
	}

	if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_ACCT_MULTI_SESSION_ID,
				    &buf, &len, NULL) == 0) {
		attrs.acct_multi_session_id = buf;
		attrs.acct_multi_session_id_len = len;
	}

	if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_CHARGEABLE_USER_IDENTITY,
				    &buf, &len, NULL) == 0) {
		attrs.cui = buf;
		attrs.cui_len = len;
	}

#ifdef CONFIG_HS20
	if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_VENDOR_SPECIFIC,
				    &buf, &len, NULL) == 0) {
		if (len < 10 || WPA_GET_BE32(buf) != RADIUS_VENDOR_ID_WFA ||
		    buf[4] != RADIUS_VENDOR_ATTR_WFA_HS20_T_C_FILTERING ||
		    buf[5] < 6) {
			wpa_printf(MSG_INFO,
				   "DAS: Unsupported attribute %u in CoA-Request from %s:%d",
				   attr, abuf, from_port);
			error = 401;
			goto fail;
		}
		attrs.hs20_t_c_filtering = &buf[6];
	}

	if (!attrs.hs20_t_c_filtering) {
			wpa_printf(MSG_INFO,
				   "DAS: No supported authorization change attribute in CoA-Request from %s:%d",
				   abuf, from_port);
			error = 402;
			goto fail;
	}
#endif /* CONFIG_HS20 */

	res = das->coa(das->ctx, &attrs);
	switch (res) {
	case RADIUS_DAS_NAS_MISMATCH:
		wpa_printf(MSG_INFO, "DAS: NAS mismatch from %s:%d",
			   abuf, from_port);
		error = 403;
		break;
	case RADIUS_DAS_SESSION_NOT_FOUND:
		wpa_printf(MSG_INFO,
			   "DAS: Session not found for request from %s:%d",
			   abuf, from_port);
		error = 503;
		break;
	case RADIUS_DAS_MULTI_SESSION_MATCH:
		wpa_printf(MSG_INFO,
			   "DAS: Multiple sessions match for request from %s:%d",
			   abuf, from_port);
		error = 508;
		break;
	case RADIUS_DAS_COA_FAILED:
		wpa_printf(MSG_INFO, "DAS: CoA failed for request from %s:%d",
			   abuf, from_port);
		error = 407;
		break;
	case RADIUS_DAS_SUCCESS:
		error = 0;
		break;
	}

fail:
	reply = radius_msg_new(error ? RADIUS_CODE_COA_NAK :
			       RADIUS_CODE_COA_ACK, hdr->identifier);
	if (!reply)
		return NULL;

	if (error &&
	    !radius_msg_add_attr_int32(reply, RADIUS_ATTR_ERROR_CAUSE, error)) {
		radius_msg_free(reply);
		return NULL;
	}

	return reply;
}


static void radius_das_receive(int sock, void *eloop_ctx, void *sock_ctx)
{
	struct radius_das_data *das = eloop_ctx;
	u8 buf[1500];
	union {
		struct sockaddr_storage ss;
		struct sockaddr_in sin;
#ifdef CONFIG_IPV6
		struct sockaddr_in6 sin6;
#endif /* CONFIG_IPV6 */
	} from;
	char abuf[50];
	int from_port = 0;
	socklen_t fromlen;
	int len;
	struct radius_msg *msg, *reply = NULL;
	struct radius_hdr *hdr;
	struct wpabuf *rbuf;
	u32 val;
	int res;
	struct os_time now;

	fromlen = sizeof(from);
	len = recvfrom(sock, buf, sizeof(buf), 0,
		       (struct sockaddr *) &from.ss, &fromlen);
	if (len < 0) {
		wpa_printf(MSG_ERROR, "DAS: recvfrom: %s", strerror(errno));
		return;
	}

	os_strlcpy(abuf, inet_ntoa(from.sin.sin_addr), sizeof(abuf));
	from_port = ntohs(from.sin.sin_port);

	wpa_printf(MSG_DEBUG, "DAS: Received %d bytes from %s:%d",
		   len, abuf, from_port);
	if (das->client_addr.u.v4.s_addr &&
	    das->client_addr.u.v4.s_addr != from.sin.sin_addr.s_addr) {
		wpa_printf(MSG_DEBUG, "DAS: Drop message from unknown client");
		return;
	}

	msg = radius_msg_parse(buf, len);
	if (msg == NULL) {
		wpa_printf(MSG_DEBUG, "DAS: Parsing incoming RADIUS packet "
			   "from %s:%d failed", abuf, from_port);
		return;
	}

	if (wpa_debug_level <= MSG_MSGDUMP)
		radius_msg_dump(msg);

	if (radius_msg_verify_das_req(msg, das->shared_secret,
				       das->shared_secret_len,
				       das->require_message_authenticator)) {
		wpa_printf(MSG_DEBUG,
			   "DAS: Invalid authenticator or Message-Authenticator in packet from %s:%d - drop",
			   abuf, from_port);
		goto fail;
	}

	os_get_time(&now);
	res = radius_msg_get_attr(msg, RADIUS_ATTR_EVENT_TIMESTAMP,
				  (u8 *) &val, 4);
	if (res == 4) {
		u32 timestamp = ntohl(val);
		if ((unsigned int) abs((int) (now.sec - timestamp)) >
		    das->time_window) {
			wpa_printf(MSG_DEBUG, "DAS: Unacceptable "
				   "Event-Timestamp (%u; local time %u) in "
				   "packet from %s:%d - drop",
				   timestamp, (unsigned int) now.sec,
				   abuf, from_port);
			goto fail;
		}
	} else if (das->require_event_timestamp) {
		wpa_printf(MSG_DEBUG, "DAS: Missing Event-Timestamp in packet "
			   "from %s:%d - drop", abuf, from_port);
		goto fail;
	}

	hdr = radius_msg_get_hdr(msg);

	switch (hdr->code) {
	case RADIUS_CODE_DISCONNECT_REQUEST:
		reply = radius_das_disconnect(das, msg, abuf, from_port);
		break;
	case RADIUS_CODE_COA_REQUEST:
		reply = radius_das_coa(das, msg, abuf, from_port);
		break;
	default:
		wpa_printf(MSG_DEBUG, "DAS: Unexpected RADIUS code %u in "
			   "packet from %s:%d",
			   hdr->code, abuf, from_port);
	}

	if (reply) {
		wpa_printf(MSG_DEBUG, "DAS: Reply to %s:%d", abuf, from_port);

		if (!radius_msg_add_attr_int32(reply,
					       RADIUS_ATTR_EVENT_TIMESTAMP,
					       now.sec)) {
			wpa_printf(MSG_DEBUG, "DAS: Failed to add "
				   "Event-Timestamp attribute");
		}

		if (radius_msg_finish_das_resp(reply, das->shared_secret,
					       das->shared_secret_len, hdr) <
		    0) {
			wpa_printf(MSG_DEBUG, "DAS: Failed to add "
				   "Message-Authenticator attribute");
		}

		if (wpa_debug_level <= MSG_MSGDUMP)
			radius_msg_dump(reply);

		rbuf = radius_msg_get_buf(reply);
		res = sendto(das->sock, wpabuf_head(rbuf),
			     wpabuf_len(rbuf), 0,
			     (struct sockaddr *) &from.ss, fromlen);
		if (res < 0) {
			wpa_printf(MSG_ERROR, "DAS: sendto(to %s:%d): %s",
				   abuf, from_port, strerror(errno));
		}
	}

fail:
	radius_msg_free(msg);
	radius_msg_free(reply);
}


static int radius_das_open_socket(int port)
{
	int s;
	struct sockaddr_in addr;

	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
		wpa_printf(MSG_INFO, "RADIUS DAS: socket: %s", strerror(errno));
		return -1;
	}

	os_memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (bind(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		wpa_printf(MSG_INFO, "RADIUS DAS: bind: %s", strerror(errno));
		close(s);
		return -1;
	}

	return s;
}


struct radius_das_data *
radius_das_init(struct radius_das_conf *conf)
{
	struct radius_das_data *das;

	if (conf->port == 0 || conf->shared_secret == NULL ||
	    conf->client_addr == NULL)
		return NULL;

	das = os_zalloc(sizeof(*das));
	if (das == NULL)
		return NULL;

	das->time_window = conf->time_window;
	das->require_event_timestamp = conf->require_event_timestamp;
	das->require_message_authenticator =
		conf->require_message_authenticator;
	das->ctx = conf->ctx;
	das->disconnect = conf->disconnect;
	das->coa = conf->coa;

	os_memcpy(&das->client_addr, conf->client_addr,
		  sizeof(das->client_addr));

	das->shared_secret = os_memdup(conf->shared_secret,
				       conf->shared_secret_len);
	if (das->shared_secret == NULL) {
		radius_das_deinit(das);
		return NULL;
	}
	das->shared_secret_len = conf->shared_secret_len;

	das->sock = radius_das_open_socket(conf->port);
	if (das->sock < 0) {
		wpa_printf(MSG_ERROR, "Failed to open UDP socket for RADIUS "
			   "DAS");
		radius_das_deinit(das);
		return NULL;
	}

	if (eloop_register_read_sock(das->sock, radius_das_receive, das, NULL))
	{
		radius_das_deinit(das);
		return NULL;
	}

	return das;
}


void radius_das_deinit(struct radius_das_data *das)
{
	if (das == NULL)
		return;

	if (das->sock >= 0) {
		eloop_unregister_read_sock(das->sock);
		close(das->sock);
	}

	os_free(das->shared_secret);
	os_free(das);
}
