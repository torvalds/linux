/*
 * RADIUS message processing
 * Copyright (c) 2002-2009, 2011-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/wpabuf.h"
#include "crypto/md5.h"
#include "crypto/crypto.h"
#include "radius.h"


/**
 * struct radius_msg - RADIUS message structure for new and parsed messages
 */
struct radius_msg {
	/**
	 * buf - Allocated buffer for RADIUS message
	 */
	struct wpabuf *buf;

	/**
	 * hdr - Pointer to the RADIUS header in buf
	 */
	struct radius_hdr *hdr;

	/**
	 * attr_pos - Array of indexes to attributes
	 *
	 * The values are number of bytes from buf to the beginning of
	 * struct radius_attr_hdr.
	 */
	size_t *attr_pos;

	/**
	 * attr_size - Total size of the attribute pointer array
	 */
	size_t attr_size;

	/**
	 * attr_used - Total number of attributes in the array
	 */
	size_t attr_used;
};


struct radius_hdr * radius_msg_get_hdr(struct radius_msg *msg)
{
	return msg->hdr;
}


struct wpabuf * radius_msg_get_buf(struct radius_msg *msg)
{
	return msg->buf;
}


static struct radius_attr_hdr *
radius_get_attr_hdr(struct radius_msg *msg, int idx)
{
	return (struct radius_attr_hdr *)
		(wpabuf_mhead_u8(msg->buf) + msg->attr_pos[idx]);
}


static void radius_msg_set_hdr(struct radius_msg *msg, u8 code, u8 identifier)
{
	msg->hdr->code = code;
	msg->hdr->identifier = identifier;
}


static int radius_msg_initialize(struct radius_msg *msg)
{
	msg->attr_pos = os_calloc(RADIUS_DEFAULT_ATTR_COUNT,
				  sizeof(*msg->attr_pos));
	if (msg->attr_pos == NULL)
		return -1;

	msg->attr_size = RADIUS_DEFAULT_ATTR_COUNT;
	msg->attr_used = 0;

	return 0;
}


/**
 * radius_msg_new - Create a new RADIUS message
 * @code: Code for RADIUS header
 * @identifier: Identifier for RADIUS header
 * Returns: Context for RADIUS message or %NULL on failure
 *
 * The caller is responsible for freeing the returned data with
 * radius_msg_free().
 */
struct radius_msg * radius_msg_new(u8 code, u8 identifier)
{
	struct radius_msg *msg;

	msg = os_zalloc(sizeof(*msg));
	if (msg == NULL)
		return NULL;

	msg->buf = wpabuf_alloc(RADIUS_DEFAULT_MSG_SIZE);
	if (msg->buf == NULL || radius_msg_initialize(msg)) {
		radius_msg_free(msg);
		return NULL;
	}
	msg->hdr = wpabuf_put(msg->buf, sizeof(struct radius_hdr));

	radius_msg_set_hdr(msg, code, identifier);

	return msg;
}


/**
 * radius_msg_free - Free a RADIUS message
 * @msg: RADIUS message from radius_msg_new() or radius_msg_parse()
 */
void radius_msg_free(struct radius_msg *msg)
{
	if (msg == NULL)
		return;

	wpabuf_free(msg->buf);
	os_free(msg->attr_pos);
	os_free(msg);
}


static const char *radius_code_string(u8 code)
{
	switch (code) {
	case RADIUS_CODE_ACCESS_REQUEST: return "Access-Request";
	case RADIUS_CODE_ACCESS_ACCEPT: return "Access-Accept";
	case RADIUS_CODE_ACCESS_REJECT: return "Access-Reject";
	case RADIUS_CODE_ACCOUNTING_REQUEST: return "Accounting-Request";
	case RADIUS_CODE_ACCOUNTING_RESPONSE: return "Accounting-Response";
	case RADIUS_CODE_ACCESS_CHALLENGE: return "Access-Challenge";
	case RADIUS_CODE_STATUS_SERVER: return "Status-Server";
	case RADIUS_CODE_STATUS_CLIENT: return "Status-Client";
	case RADIUS_CODE_RESERVED: return "Reserved";
	case RADIUS_CODE_DISCONNECT_REQUEST: return "Disconnect-Request";
	case RADIUS_CODE_DISCONNECT_ACK: return "Disconnect-ACK";
	case RADIUS_CODE_DISCONNECT_NAK: return "Disconnect-NAK";
	case RADIUS_CODE_COA_REQUEST: return "CoA-Request";
	case RADIUS_CODE_COA_ACK: return "CoA-ACK";
	case RADIUS_CODE_COA_NAK: return "CoA-NAK";
	default: return "?Unknown?";
	}
}


struct radius_attr_type {
	u8 type;
	char *name;
	enum {
		RADIUS_ATTR_UNDIST, RADIUS_ATTR_TEXT, RADIUS_ATTR_IP,
		RADIUS_ATTR_HEXDUMP, RADIUS_ATTR_INT32, RADIUS_ATTR_IPV6
	} data_type;
};

static const struct radius_attr_type radius_attrs[] =
{
	{ RADIUS_ATTR_USER_NAME, "User-Name", RADIUS_ATTR_TEXT },
	{ RADIUS_ATTR_USER_PASSWORD, "User-Password", RADIUS_ATTR_UNDIST },
	{ RADIUS_ATTR_NAS_IP_ADDRESS, "NAS-IP-Address", RADIUS_ATTR_IP },
	{ RADIUS_ATTR_NAS_PORT, "NAS-Port", RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_SERVICE_TYPE, "Service-Type", RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_FRAMED_IP_ADDRESS, "Framed-IP-Address", RADIUS_ATTR_IP },
	{ RADIUS_ATTR_FRAMED_MTU, "Framed-MTU", RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_REPLY_MESSAGE, "Reply-Message", RADIUS_ATTR_TEXT },
	{ RADIUS_ATTR_STATE, "State", RADIUS_ATTR_UNDIST },
	{ RADIUS_ATTR_CLASS, "Class", RADIUS_ATTR_UNDIST },
	{ RADIUS_ATTR_VENDOR_SPECIFIC, "Vendor-Specific", RADIUS_ATTR_UNDIST },
	{ RADIUS_ATTR_SESSION_TIMEOUT, "Session-Timeout", RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_IDLE_TIMEOUT, "Idle-Timeout", RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_TERMINATION_ACTION, "Termination-Action",
	  RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_CALLED_STATION_ID, "Called-Station-Id",
	  RADIUS_ATTR_TEXT },
	{ RADIUS_ATTR_CALLING_STATION_ID, "Calling-Station-Id",
	  RADIUS_ATTR_TEXT },
	{ RADIUS_ATTR_NAS_IDENTIFIER, "NAS-Identifier", RADIUS_ATTR_TEXT },
	{ RADIUS_ATTR_PROXY_STATE, "Proxy-State", RADIUS_ATTR_UNDIST },
	{ RADIUS_ATTR_ACCT_STATUS_TYPE, "Acct-Status-Type",
	  RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_ACCT_DELAY_TIME, "Acct-Delay-Time", RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_ACCT_INPUT_OCTETS, "Acct-Input-Octets",
	  RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_ACCT_OUTPUT_OCTETS, "Acct-Output-Octets",
	  RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_ACCT_SESSION_ID, "Acct-Session-Id", RADIUS_ATTR_TEXT },
	{ RADIUS_ATTR_ACCT_AUTHENTIC, "Acct-Authentic", RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_ACCT_SESSION_TIME, "Acct-Session-Time",
	  RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_ACCT_INPUT_PACKETS, "Acct-Input-Packets",
	  RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_ACCT_OUTPUT_PACKETS, "Acct-Output-Packets",
	  RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_ACCT_TERMINATE_CAUSE, "Acct-Terminate-Cause",
	  RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_ACCT_MULTI_SESSION_ID, "Acct-Multi-Session-Id",
	  RADIUS_ATTR_TEXT },
	{ RADIUS_ATTR_ACCT_LINK_COUNT, "Acct-Link-Count", RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_ACCT_INPUT_GIGAWORDS, "Acct-Input-Gigawords",
	  RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_ACCT_OUTPUT_GIGAWORDS, "Acct-Output-Gigawords",
	  RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_EVENT_TIMESTAMP, "Event-Timestamp",
	  RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_EGRESS_VLANID, "EGRESS-VLANID", RADIUS_ATTR_HEXDUMP },
	{ RADIUS_ATTR_NAS_PORT_TYPE, "NAS-Port-Type", RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_TUNNEL_TYPE, "Tunnel-Type", RADIUS_ATTR_HEXDUMP },
	{ RADIUS_ATTR_TUNNEL_MEDIUM_TYPE, "Tunnel-Medium-Type",
	  RADIUS_ATTR_HEXDUMP },
	{ RADIUS_ATTR_TUNNEL_PASSWORD, "Tunnel-Password",
	  RADIUS_ATTR_UNDIST },
	{ RADIUS_ATTR_CONNECT_INFO, "Connect-Info", RADIUS_ATTR_TEXT },
	{ RADIUS_ATTR_EAP_MESSAGE, "EAP-Message", RADIUS_ATTR_UNDIST },
	{ RADIUS_ATTR_MESSAGE_AUTHENTICATOR, "Message-Authenticator",
	  RADIUS_ATTR_UNDIST },
	{ RADIUS_ATTR_TUNNEL_PRIVATE_GROUP_ID, "Tunnel-Private-Group-Id",
	  RADIUS_ATTR_HEXDUMP },
	{ RADIUS_ATTR_ACCT_INTERIM_INTERVAL, "Acct-Interim-Interval",
	  RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_CHARGEABLE_USER_IDENTITY, "Chargeable-User-Identity",
	  RADIUS_ATTR_TEXT },
	{ RADIUS_ATTR_NAS_IPV6_ADDRESS, "NAS-IPv6-Address", RADIUS_ATTR_IPV6 },
	{ RADIUS_ATTR_ERROR_CAUSE, "Error-Cause", RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_EAP_KEY_NAME, "EAP-Key-Name", RADIUS_ATTR_HEXDUMP },
	{ RADIUS_ATTR_OPERATOR_NAME, "Operator-Name", RADIUS_ATTR_TEXT },
	{ RADIUS_ATTR_LOCATION_INFO, "Location-Information",
	  RADIUS_ATTR_HEXDUMP },
	{ RADIUS_ATTR_LOCATION_DATA, "Location-Data", RADIUS_ATTR_HEXDUMP },
	{ RADIUS_ATTR_BASIC_LOCATION_POLICY_RULES,
	  "Basic-Location-Policy-Rules", RADIUS_ATTR_HEXDUMP },
	{ RADIUS_ATTR_EXTENDED_LOCATION_POLICY_RULES,
	  "Extended-Location-Policy-Rules", RADIUS_ATTR_HEXDUMP },
	{ RADIUS_ATTR_LOCATION_CAPABLE, "Location-Capable", RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_REQUESTED_LOCATION_INFO, "Requested-Location-Info",
	  RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_MOBILITY_DOMAIN_ID, "Mobility-Domain-Id",
	  RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_WLAN_HESSID, "WLAN-HESSID", RADIUS_ATTR_TEXT },
	{ RADIUS_ATTR_WLAN_REASON_CODE, "WLAN-Reason-Code",
	  RADIUS_ATTR_INT32 },
	{ RADIUS_ATTR_WLAN_PAIRWISE_CIPHER, "WLAN-Pairwise-Cipher",
	  RADIUS_ATTR_HEXDUMP },
	{ RADIUS_ATTR_WLAN_GROUP_CIPHER, "WLAN-Group-Cipher",
	  RADIUS_ATTR_HEXDUMP },
	{ RADIUS_ATTR_WLAN_AKM_SUITE, "WLAN-AKM-Suite",
	  RADIUS_ATTR_HEXDUMP },
	{ RADIUS_ATTR_WLAN_GROUP_MGMT_CIPHER, "WLAN-Group-Mgmt-Pairwise-Cipher",
	  RADIUS_ATTR_HEXDUMP },
};
#define RADIUS_ATTRS ARRAY_SIZE(radius_attrs)


static const struct radius_attr_type *radius_get_attr_type(u8 type)
{
	size_t i;

	for (i = 0; i < RADIUS_ATTRS; i++) {
		if (type == radius_attrs[i].type)
			return &radius_attrs[i];
	}

	return NULL;
}


static void radius_msg_dump_attr(struct radius_attr_hdr *hdr)
{
	const struct radius_attr_type *attr;
	int len;
	unsigned char *pos;
	char buf[1000];

	attr = radius_get_attr_type(hdr->type);

	wpa_printf(MSG_INFO, "   Attribute %d (%s) length=%d",
		   hdr->type, attr ? attr->name : "?Unknown?", hdr->length);

	if (attr == NULL || hdr->length < sizeof(struct radius_attr_hdr))
		return;

	len = hdr->length - sizeof(struct radius_attr_hdr);
	pos = (unsigned char *) (hdr + 1);

	switch (attr->data_type) {
	case RADIUS_ATTR_TEXT:
		printf_encode(buf, sizeof(buf), pos, len);
		wpa_printf(MSG_INFO, "      Value: '%s'", buf);
		break;

	case RADIUS_ATTR_IP:
		if (len == 4) {
			struct in_addr addr;
			os_memcpy(&addr, pos, 4);
			wpa_printf(MSG_INFO, "      Value: %s",
				   inet_ntoa(addr));
		} else {
			wpa_printf(MSG_INFO, "      Invalid IP address length %d",
				   len);
		}
		break;

#ifdef CONFIG_IPV6
	case RADIUS_ATTR_IPV6:
		if (len == 16) {
			const char *atxt;
			struct in6_addr *addr = (struct in6_addr *) pos;
			atxt = inet_ntop(AF_INET6, addr, buf, sizeof(buf));
			wpa_printf(MSG_INFO, "      Value: %s",
				   atxt ? atxt : "?");
		} else {
			wpa_printf(MSG_INFO, "      Invalid IPv6 address length %d",
				   len);
		}
		break;
#endif /* CONFIG_IPV6 */

	case RADIUS_ATTR_HEXDUMP:
	case RADIUS_ATTR_UNDIST:
		wpa_snprintf_hex(buf, sizeof(buf), pos, len);
		wpa_printf(MSG_INFO, "      Value: %s", buf);
		break;

	case RADIUS_ATTR_INT32:
		if (len == 4)
			wpa_printf(MSG_INFO, "      Value: %u",
				   WPA_GET_BE32(pos));
		else
			wpa_printf(MSG_INFO, "      Invalid INT32 length %d",
				   len);
		break;

	default:
		break;
	}
}


void radius_msg_dump(struct radius_msg *msg)
{
	size_t i;

	wpa_printf(MSG_INFO, "RADIUS message: code=%d (%s) identifier=%d length=%d",
		   msg->hdr->code, radius_code_string(msg->hdr->code),
		   msg->hdr->identifier, be_to_host16(msg->hdr->length));

	for (i = 0; i < msg->attr_used; i++) {
		struct radius_attr_hdr *attr = radius_get_attr_hdr(msg, i);
		radius_msg_dump_attr(attr);
	}
}


int radius_msg_finish(struct radius_msg *msg, const u8 *secret,
		      size_t secret_len)
{
	if (secret) {
		u8 auth[MD5_MAC_LEN];
		struct radius_attr_hdr *attr;

		os_memset(auth, 0, MD5_MAC_LEN);
		attr = radius_msg_add_attr(msg,
					   RADIUS_ATTR_MESSAGE_AUTHENTICATOR,
					   auth, MD5_MAC_LEN);
		if (attr == NULL) {
			wpa_printf(MSG_WARNING, "RADIUS: Could not add "
				   "Message-Authenticator");
			return -1;
		}
		msg->hdr->length = host_to_be16(wpabuf_len(msg->buf));
		hmac_md5(secret, secret_len, wpabuf_head(msg->buf),
			 wpabuf_len(msg->buf), (u8 *) (attr + 1));
	} else
		msg->hdr->length = host_to_be16(wpabuf_len(msg->buf));

	if (wpabuf_len(msg->buf) > 0xffff) {
		wpa_printf(MSG_WARNING, "RADIUS: Too long message (%lu)",
			   (unsigned long) wpabuf_len(msg->buf));
		return -1;
	}
	return 0;
}


int radius_msg_finish_srv(struct radius_msg *msg, const u8 *secret,
			  size_t secret_len, const u8 *req_authenticator)
{
	u8 auth[MD5_MAC_LEN];
	struct radius_attr_hdr *attr;
	const u8 *addr[4];
	size_t len[4];

	os_memset(auth, 0, MD5_MAC_LEN);
	attr = radius_msg_add_attr(msg, RADIUS_ATTR_MESSAGE_AUTHENTICATOR,
				   auth, MD5_MAC_LEN);
	if (attr == NULL) {
		wpa_printf(MSG_ERROR, "WARNING: Could not add Message-Authenticator");
		return -1;
	}
	msg->hdr->length = host_to_be16(wpabuf_len(msg->buf));
	os_memcpy(msg->hdr->authenticator, req_authenticator,
		  sizeof(msg->hdr->authenticator));
	hmac_md5(secret, secret_len, wpabuf_head(msg->buf),
		 wpabuf_len(msg->buf), (u8 *) (attr + 1));

	/* ResponseAuth = MD5(Code+ID+Length+RequestAuth+Attributes+Secret) */
	addr[0] = (u8 *) msg->hdr;
	len[0] = 1 + 1 + 2;
	addr[1] = req_authenticator;
	len[1] = MD5_MAC_LEN;
	addr[2] = wpabuf_head_u8(msg->buf) + sizeof(struct radius_hdr);
	len[2] = wpabuf_len(msg->buf) - sizeof(struct radius_hdr);
	addr[3] = secret;
	len[3] = secret_len;
	md5_vector(4, addr, len, msg->hdr->authenticator);

	if (wpabuf_len(msg->buf) > 0xffff) {
		wpa_printf(MSG_WARNING, "RADIUS: Too long message (%lu)",
			   (unsigned long) wpabuf_len(msg->buf));
		return -1;
	}
	return 0;
}


int radius_msg_finish_das_resp(struct radius_msg *msg, const u8 *secret,
			       size_t secret_len,
			       const struct radius_hdr *req_hdr)
{
	const u8 *addr[2];
	size_t len[2];
	u8 auth[MD5_MAC_LEN];
	struct radius_attr_hdr *attr;

	os_memset(auth, 0, MD5_MAC_LEN);
	attr = radius_msg_add_attr(msg, RADIUS_ATTR_MESSAGE_AUTHENTICATOR,
				   auth, MD5_MAC_LEN);
	if (attr == NULL) {
		wpa_printf(MSG_WARNING, "Could not add Message-Authenticator");
		return -1;
	}

	msg->hdr->length = host_to_be16(wpabuf_len(msg->buf));
	os_memcpy(msg->hdr->authenticator, req_hdr->authenticator, 16);
	hmac_md5(secret, secret_len, wpabuf_head(msg->buf),
		 wpabuf_len(msg->buf), (u8 *) (attr + 1));

	/* ResponseAuth = MD5(Code+ID+Length+RequestAuth+Attributes+Secret) */
	addr[0] = wpabuf_head_u8(msg->buf);
	len[0] = wpabuf_len(msg->buf);
	addr[1] = secret;
	len[1] = secret_len;
	if (md5_vector(2, addr, len, msg->hdr->authenticator) < 0)
		return -1;

	if (wpabuf_len(msg->buf) > 0xffff) {
		wpa_printf(MSG_WARNING, "RADIUS: Too long message (%lu)",
			   (unsigned long) wpabuf_len(msg->buf));
		return -1;
	}
	return 0;
}


void radius_msg_finish_acct(struct radius_msg *msg, const u8 *secret,
			    size_t secret_len)
{
	const u8 *addr[2];
	size_t len[2];

	msg->hdr->length = host_to_be16(wpabuf_len(msg->buf));
	os_memset(msg->hdr->authenticator, 0, MD5_MAC_LEN);
	addr[0] = wpabuf_head(msg->buf);
	len[0] = wpabuf_len(msg->buf);
	addr[1] = secret;
	len[1] = secret_len;
	md5_vector(2, addr, len, msg->hdr->authenticator);

	if (wpabuf_len(msg->buf) > 0xffff) {
		wpa_printf(MSG_WARNING, "RADIUS: Too long messages (%lu)",
			   (unsigned long) wpabuf_len(msg->buf));
	}
}


void radius_msg_finish_acct_resp(struct radius_msg *msg, const u8 *secret,
				 size_t secret_len, const u8 *req_authenticator)
{
	const u8 *addr[2];
	size_t len[2];

	msg->hdr->length = host_to_be16(wpabuf_len(msg->buf));
	os_memcpy(msg->hdr->authenticator, req_authenticator, MD5_MAC_LEN);
	addr[0] = wpabuf_head(msg->buf);
	len[0] = wpabuf_len(msg->buf);
	addr[1] = secret;
	len[1] = secret_len;
	md5_vector(2, addr, len, msg->hdr->authenticator);

	if (wpabuf_len(msg->buf) > 0xffff) {
		wpa_printf(MSG_WARNING, "RADIUS: Too long messages (%lu)",
			   (unsigned long) wpabuf_len(msg->buf));
	}
}


int radius_msg_verify_acct_req(struct radius_msg *msg, const u8 *secret,
			       size_t secret_len)
{
	const u8 *addr[4];
	size_t len[4];
	u8 zero[MD5_MAC_LEN];
	u8 hash[MD5_MAC_LEN];

	os_memset(zero, 0, sizeof(zero));
	addr[0] = (u8 *) msg->hdr;
	len[0] = sizeof(struct radius_hdr) - MD5_MAC_LEN;
	addr[1] = zero;
	len[1] = MD5_MAC_LEN;
	addr[2] = (u8 *) (msg->hdr + 1);
	len[2] = wpabuf_len(msg->buf) - sizeof(struct radius_hdr);
	addr[3] = secret;
	len[3] = secret_len;
	md5_vector(4, addr, len, hash);
	return os_memcmp_const(msg->hdr->authenticator, hash, MD5_MAC_LEN) != 0;
}


int radius_msg_verify_das_req(struct radius_msg *msg, const u8 *secret,
			      size_t secret_len,
			      int require_message_authenticator)
{
	const u8 *addr[4];
	size_t len[4];
	u8 zero[MD5_MAC_LEN];
	u8 hash[MD5_MAC_LEN];
	u8 auth[MD5_MAC_LEN], orig[MD5_MAC_LEN];
	u8 orig_authenticator[16];

	struct radius_attr_hdr *attr = NULL, *tmp;
	size_t i;

	os_memset(zero, 0, sizeof(zero));
	addr[0] = (u8 *) msg->hdr;
	len[0] = sizeof(struct radius_hdr) - MD5_MAC_LEN;
	addr[1] = zero;
	len[1] = MD5_MAC_LEN;
	addr[2] = (u8 *) (msg->hdr + 1);
	len[2] = wpabuf_len(msg->buf) - sizeof(struct radius_hdr);
	addr[3] = secret;
	len[3] = secret_len;
	md5_vector(4, addr, len, hash);
	if (os_memcmp_const(msg->hdr->authenticator, hash, MD5_MAC_LEN) != 0)
		return 1;

	for (i = 0; i < msg->attr_used; i++) {
		tmp = radius_get_attr_hdr(msg, i);
		if (tmp->type == RADIUS_ATTR_MESSAGE_AUTHENTICATOR) {
			if (attr != NULL) {
				wpa_printf(MSG_WARNING, "Multiple "
					   "Message-Authenticator attributes "
					   "in RADIUS message");
				return 1;
			}
			attr = tmp;
		}
	}

	if (attr == NULL) {
		if (require_message_authenticator) {
			wpa_printf(MSG_WARNING,
				   "Missing Message-Authenticator attribute in RADIUS message");
			return 1;
		}
		return 0;
	}

	os_memcpy(orig, attr + 1, MD5_MAC_LEN);
	os_memset(attr + 1, 0, MD5_MAC_LEN);
	os_memcpy(orig_authenticator, msg->hdr->authenticator,
		  sizeof(orig_authenticator));
	os_memset(msg->hdr->authenticator, 0,
		  sizeof(msg->hdr->authenticator));
	hmac_md5(secret, secret_len, wpabuf_head(msg->buf),
		 wpabuf_len(msg->buf), auth);
	os_memcpy(attr + 1, orig, MD5_MAC_LEN);
	os_memcpy(msg->hdr->authenticator, orig_authenticator,
		  sizeof(orig_authenticator));

	return os_memcmp_const(orig, auth, MD5_MAC_LEN) != 0;
}


static int radius_msg_add_attr_to_array(struct radius_msg *msg,
					struct radius_attr_hdr *attr)
{
	if (msg->attr_used >= msg->attr_size) {
		size_t *nattr_pos;
		int nlen = msg->attr_size * 2;

		nattr_pos = os_realloc_array(msg->attr_pos, nlen,
					     sizeof(*msg->attr_pos));
		if (nattr_pos == NULL)
			return -1;

		msg->attr_pos = nattr_pos;
		msg->attr_size = nlen;
	}

	msg->attr_pos[msg->attr_used++] =
		(unsigned char *) attr - wpabuf_head_u8(msg->buf);

	return 0;
}


struct radius_attr_hdr *radius_msg_add_attr(struct radius_msg *msg, u8 type,
					    const u8 *data, size_t data_len)
{
	size_t buf_needed;
	struct radius_attr_hdr *attr;

	if (TEST_FAIL())
		return NULL;

	if (data_len > RADIUS_MAX_ATTR_LEN) {
		wpa_printf(MSG_ERROR, "radius_msg_add_attr: too long attribute (%lu bytes)",
		       (unsigned long) data_len);
		return NULL;
	}

	buf_needed = sizeof(*attr) + data_len;

	if (wpabuf_tailroom(msg->buf) < buf_needed) {
		/* allocate more space for message buffer */
		if (wpabuf_resize(&msg->buf, buf_needed) < 0)
			return NULL;
		msg->hdr = wpabuf_mhead(msg->buf);
	}

	attr = wpabuf_put(msg->buf, sizeof(struct radius_attr_hdr));
	attr->type = type;
	attr->length = sizeof(*attr) + data_len;
	wpabuf_put_data(msg->buf, data, data_len);

	if (radius_msg_add_attr_to_array(msg, attr))
		return NULL;

	return attr;
}


/**
 * radius_msg_parse - Parse a RADIUS message
 * @data: RADIUS message to be parsed
 * @len: Length of data buffer in octets
 * Returns: Parsed RADIUS message or %NULL on failure
 *
 * This parses a RADIUS message and makes a copy of its data. The caller is
 * responsible for freeing the returned data with radius_msg_free().
 */
struct radius_msg * radius_msg_parse(const u8 *data, size_t len)
{
	struct radius_msg *msg;
	struct radius_hdr *hdr;
	struct radius_attr_hdr *attr;
	size_t msg_len;
	unsigned char *pos, *end;

	if (data == NULL || len < sizeof(*hdr))
		return NULL;

	hdr = (struct radius_hdr *) data;

	msg_len = be_to_host16(hdr->length);
	if (msg_len < sizeof(*hdr) || msg_len > len) {
		wpa_printf(MSG_INFO, "RADIUS: Invalid message length");
		return NULL;
	}

	if (msg_len < len) {
		wpa_printf(MSG_DEBUG, "RADIUS: Ignored %lu extra bytes after "
			   "RADIUS message", (unsigned long) len - msg_len);
	}

	msg = os_zalloc(sizeof(*msg));
	if (msg == NULL)
		return NULL;

	msg->buf = wpabuf_alloc_copy(data, msg_len);
	if (msg->buf == NULL || radius_msg_initialize(msg)) {
		radius_msg_free(msg);
		return NULL;
	}
	msg->hdr = wpabuf_mhead(msg->buf);

	/* parse attributes */
	pos = wpabuf_mhead_u8(msg->buf) + sizeof(struct radius_hdr);
	end = wpabuf_mhead_u8(msg->buf) + wpabuf_len(msg->buf);
	while (pos < end) {
		if ((size_t) (end - pos) < sizeof(*attr))
			goto fail;

		attr = (struct radius_attr_hdr *) pos;

		if (attr->length > end - pos || attr->length < sizeof(*attr))
			goto fail;

		/* TODO: check that attr->length is suitable for attr->type */

		if (radius_msg_add_attr_to_array(msg, attr))
			goto fail;

		pos += attr->length;
	}

	return msg;

 fail:
	radius_msg_free(msg);
	return NULL;
}


int radius_msg_add_eap(struct radius_msg *msg, const u8 *data, size_t data_len)
{
	const u8 *pos = data;
	size_t left = data_len;

	while (left > 0) {
		int len;
		if (left > RADIUS_MAX_ATTR_LEN)
			len = RADIUS_MAX_ATTR_LEN;
		else
			len = left;

		if (!radius_msg_add_attr(msg, RADIUS_ATTR_EAP_MESSAGE,
					 pos, len))
			return 0;

		pos += len;
		left -= len;
	}

	return 1;
}


struct wpabuf * radius_msg_get_eap(struct radius_msg *msg)
{
	struct wpabuf *eap;
	size_t len, i;
	struct radius_attr_hdr *attr;

	if (msg == NULL)
		return NULL;

	len = 0;
	for (i = 0; i < msg->attr_used; i++) {
		attr = radius_get_attr_hdr(msg, i);
		if (attr->type == RADIUS_ATTR_EAP_MESSAGE &&
		    attr->length > sizeof(struct radius_attr_hdr))
			len += attr->length - sizeof(struct radius_attr_hdr);
	}

	if (len == 0)
		return NULL;

	eap = wpabuf_alloc(len);
	if (eap == NULL)
		return NULL;

	for (i = 0; i < msg->attr_used; i++) {
		attr = radius_get_attr_hdr(msg, i);
		if (attr->type == RADIUS_ATTR_EAP_MESSAGE &&
		    attr->length > sizeof(struct radius_attr_hdr)) {
			int flen = attr->length - sizeof(*attr);
			wpabuf_put_data(eap, attr + 1, flen);
		}
	}

	return eap;
}


int radius_msg_verify_msg_auth(struct radius_msg *msg, const u8 *secret,
			       size_t secret_len, const u8 *req_auth)
{
	u8 auth[MD5_MAC_LEN], orig[MD5_MAC_LEN];
	u8 orig_authenticator[16];
	struct radius_attr_hdr *attr = NULL, *tmp;
	size_t i;

	for (i = 0; i < msg->attr_used; i++) {
		tmp = radius_get_attr_hdr(msg, i);
		if (tmp->type == RADIUS_ATTR_MESSAGE_AUTHENTICATOR) {
			if (attr != NULL) {
				wpa_printf(MSG_INFO, "Multiple Message-Authenticator attributes in RADIUS message");
				return 1;
			}
			attr = tmp;
		}
	}

	if (attr == NULL) {
		wpa_printf(MSG_INFO, "No Message-Authenticator attribute found");
		return 1;
	}

	os_memcpy(orig, attr + 1, MD5_MAC_LEN);
	os_memset(attr + 1, 0, MD5_MAC_LEN);
	if (req_auth) {
		os_memcpy(orig_authenticator, msg->hdr->authenticator,
			  sizeof(orig_authenticator));
		os_memcpy(msg->hdr->authenticator, req_auth,
			  sizeof(msg->hdr->authenticator));
	}
	if (hmac_md5(secret, secret_len, wpabuf_head(msg->buf),
		     wpabuf_len(msg->buf), auth) < 0)
		return 1;
	os_memcpy(attr + 1, orig, MD5_MAC_LEN);
	if (req_auth) {
		os_memcpy(msg->hdr->authenticator, orig_authenticator,
			  sizeof(orig_authenticator));
	}

	if (os_memcmp_const(orig, auth, MD5_MAC_LEN) != 0) {
		wpa_printf(MSG_INFO, "Invalid Message-Authenticator!");
		return 1;
	}

	return 0;
}


int radius_msg_verify(struct radius_msg *msg, const u8 *secret,
		      size_t secret_len, struct radius_msg *sent_msg, int auth)
{
	const u8 *addr[4];
	size_t len[4];
	u8 hash[MD5_MAC_LEN];

	if (sent_msg == NULL) {
		wpa_printf(MSG_INFO, "No matching Access-Request message found");
		return 1;
	}

	if (auth &&
	    radius_msg_verify_msg_auth(msg, secret, secret_len,
				       sent_msg->hdr->authenticator)) {
		return 1;
	}

	/* ResponseAuth = MD5(Code+ID+Length+RequestAuth+Attributes+Secret) */
	addr[0] = (u8 *) msg->hdr;
	len[0] = 1 + 1 + 2;
	addr[1] = sent_msg->hdr->authenticator;
	len[1] = MD5_MAC_LEN;
	addr[2] = wpabuf_head_u8(msg->buf) + sizeof(struct radius_hdr);
	len[2] = wpabuf_len(msg->buf) - sizeof(struct radius_hdr);
	addr[3] = secret;
	len[3] = secret_len;
	if (md5_vector(4, addr, len, hash) < 0 ||
	    os_memcmp_const(hash, msg->hdr->authenticator, MD5_MAC_LEN) != 0) {
		wpa_printf(MSG_INFO, "Response Authenticator invalid!");
		return 1;
	}

	return 0;
}


int radius_msg_copy_attr(struct radius_msg *dst, struct radius_msg *src,
			 u8 type)
{
	struct radius_attr_hdr *attr;
	size_t i;
	int count = 0;

	for (i = 0; i < src->attr_used; i++) {
		attr = radius_get_attr_hdr(src, i);
		if (attr->type == type && attr->length >= sizeof(*attr)) {
			if (!radius_msg_add_attr(dst, type, (u8 *) (attr + 1),
						 attr->length - sizeof(*attr)))
				return -1;
			count++;
		}
	}

	return count;
}


/* Create Request Authenticator. The value should be unique over the lifetime
 * of the shared secret between authenticator and authentication server.
 */
int radius_msg_make_authenticator(struct radius_msg *msg)
{
	return os_get_random((u8 *) &msg->hdr->authenticator,
			     sizeof(msg->hdr->authenticator));
}


/* Get Vendor-specific RADIUS Attribute from a parsed RADIUS message.
 * Returns the Attribute payload and sets alen to indicate the length of the
 * payload if a vendor attribute with subtype is found, otherwise returns NULL.
 * The returned payload is allocated with os_malloc() and caller must free it
 * by calling os_free().
 */
static u8 *radius_msg_get_vendor_attr(struct radius_msg *msg, u32 vendor,
				      u8 subtype, size_t *alen)
{
	u8 *data, *pos;
	size_t i, len;

	if (msg == NULL)
		return NULL;

	for (i = 0; i < msg->attr_used; i++) {
		struct radius_attr_hdr *attr = radius_get_attr_hdr(msg, i);
		size_t left;
		u32 vendor_id;
		struct radius_attr_vendor *vhdr;

		if (attr->type != RADIUS_ATTR_VENDOR_SPECIFIC ||
		    attr->length < sizeof(*attr))
			continue;

		left = attr->length - sizeof(*attr);
		if (left < 4)
			continue;

		pos = (u8 *) (attr + 1);

		os_memcpy(&vendor_id, pos, 4);
		pos += 4;
		left -= 4;

		if (ntohl(vendor_id) != vendor)
			continue;

		while (left >= sizeof(*vhdr)) {
			vhdr = (struct radius_attr_vendor *) pos;
			if (vhdr->vendor_length > left ||
			    vhdr->vendor_length < sizeof(*vhdr)) {
				break;
			}
			if (vhdr->vendor_type != subtype) {
				pos += vhdr->vendor_length;
				left -= vhdr->vendor_length;
				continue;
			}

			len = vhdr->vendor_length - sizeof(*vhdr);
			data = os_memdup(pos + sizeof(*vhdr), len);
			if (data == NULL)
				return NULL;
			if (alen)
				*alen = len;
			return data;
		}
	}

	return NULL;
}


static u8 * decrypt_ms_key(const u8 *key, size_t len,
			   const u8 *req_authenticator,
			   const u8 *secret, size_t secret_len, size_t *reslen)
{
	u8 *plain, *ppos, *res;
	const u8 *pos;
	size_t left, plen;
	u8 hash[MD5_MAC_LEN];
	int i, first = 1;
	const u8 *addr[3];
	size_t elen[3];

	/* key: 16-bit salt followed by encrypted key info */

	if (len < 2 + 16) {
		wpa_printf(MSG_DEBUG, "RADIUS: %s: Len is too small: %d",
			   __func__, (int) len);
		return NULL;
	}

	pos = key + 2;
	left = len - 2;
	if (left % 16) {
		wpa_printf(MSG_INFO, "RADIUS: Invalid ms key len %lu",
			   (unsigned long) left);
		return NULL;
	}

	plen = left;
	ppos = plain = os_malloc(plen);
	if (plain == NULL)
		return NULL;
	plain[0] = 0;

	while (left > 0) {
		/* b(1) = MD5(Secret + Request-Authenticator + Salt)
		 * b(i) = MD5(Secret + c(i - 1)) for i > 1 */

		addr[0] = secret;
		elen[0] = secret_len;
		if (first) {
			addr[1] = req_authenticator;
			elen[1] = MD5_MAC_LEN;
			addr[2] = key;
			elen[2] = 2; /* Salt */
		} else {
			addr[1] = pos - MD5_MAC_LEN;
			elen[1] = MD5_MAC_LEN;
		}
		if (md5_vector(first ? 3 : 2, addr, elen, hash) < 0) {
			os_free(plain);
			return NULL;
		}
		first = 0;

		for (i = 0; i < MD5_MAC_LEN; i++)
			*ppos++ = *pos++ ^ hash[i];
		left -= MD5_MAC_LEN;
	}

	if (plain[0] == 0 || plain[0] > plen - 1) {
		wpa_printf(MSG_INFO, "RADIUS: Failed to decrypt MPPE key");
		os_free(plain);
		return NULL;
	}

	res = os_memdup(plain + 1, plain[0]);
	if (res == NULL) {
		os_free(plain);
		return NULL;
	}
	if (reslen)
		*reslen = plain[0];
	os_free(plain);
	return res;
}


static void encrypt_ms_key(const u8 *key, size_t key_len, u16 salt,
			   const u8 *req_authenticator,
			   const u8 *secret, size_t secret_len,
			   u8 *ebuf, size_t *elen)
{
	int i, len, first = 1;
	u8 hash[MD5_MAC_LEN], saltbuf[2], *pos;
	const u8 *addr[3];
	size_t _len[3];

	WPA_PUT_BE16(saltbuf, salt);

	len = 1 + key_len;
	if (len & 0x0f) {
		len = (len & 0xf0) + 16;
	}
	os_memset(ebuf, 0, len);
	ebuf[0] = key_len;
	os_memcpy(ebuf + 1, key, key_len);

	*elen = len;

	pos = ebuf;
	while (len > 0) {
		/* b(1) = MD5(Secret + Request-Authenticator + Salt)
		 * b(i) = MD5(Secret + c(i - 1)) for i > 1 */
		addr[0] = secret;
		_len[0] = secret_len;
		if (first) {
			addr[1] = req_authenticator;
			_len[1] = MD5_MAC_LEN;
			addr[2] = saltbuf;
			_len[2] = sizeof(saltbuf);
		} else {
			addr[1] = pos - MD5_MAC_LEN;
			_len[1] = MD5_MAC_LEN;
		}
		md5_vector(first ? 3 : 2, addr, _len, hash);
		first = 0;

		for (i = 0; i < MD5_MAC_LEN; i++)
			*pos++ ^= hash[i];

		len -= MD5_MAC_LEN;
	}
}


struct radius_ms_mppe_keys *
radius_msg_get_ms_keys(struct radius_msg *msg, struct radius_msg *sent_msg,
		       const u8 *secret, size_t secret_len)
{
	u8 *key;
	size_t keylen;
	struct radius_ms_mppe_keys *keys;

	if (msg == NULL || sent_msg == NULL)
		return NULL;

	keys = os_zalloc(sizeof(*keys));
	if (keys == NULL)
		return NULL;

	key = radius_msg_get_vendor_attr(msg, RADIUS_VENDOR_ID_MICROSOFT,
					 RADIUS_VENDOR_ATTR_MS_MPPE_SEND_KEY,
					 &keylen);
	if (key) {
		keys->send = decrypt_ms_key(key, keylen,
					    sent_msg->hdr->authenticator,
					    secret, secret_len,
					    &keys->send_len);
		if (!keys->send) {
			wpa_printf(MSG_DEBUG,
				   "RADIUS: Failed to decrypt send key");
		}
		os_free(key);
	}

	key = radius_msg_get_vendor_attr(msg, RADIUS_VENDOR_ID_MICROSOFT,
					 RADIUS_VENDOR_ATTR_MS_MPPE_RECV_KEY,
					 &keylen);
	if (key) {
		keys->recv = decrypt_ms_key(key, keylen,
					    sent_msg->hdr->authenticator,
					    secret, secret_len,
					    &keys->recv_len);
		if (!keys->recv) {
			wpa_printf(MSG_DEBUG,
				   "RADIUS: Failed to decrypt recv key");
		}
		os_free(key);
	}

	return keys;
}


struct radius_ms_mppe_keys *
radius_msg_get_cisco_keys(struct radius_msg *msg, struct radius_msg *sent_msg,
			  const u8 *secret, size_t secret_len)
{
	u8 *key;
	size_t keylen;
	struct radius_ms_mppe_keys *keys;

	if (msg == NULL || sent_msg == NULL)
		return NULL;

	keys = os_zalloc(sizeof(*keys));
	if (keys == NULL)
		return NULL;

	key = radius_msg_get_vendor_attr(msg, RADIUS_VENDOR_ID_CISCO,
					 RADIUS_CISCO_AV_PAIR, &keylen);
	if (key && keylen == 51 &&
	    os_memcmp(key, "leap:session-key=", 17) == 0) {
		keys->recv = decrypt_ms_key(key + 17, keylen - 17,
					    sent_msg->hdr->authenticator,
					    secret, secret_len,
					    &keys->recv_len);
	}
	os_free(key);

	return keys;
}


int radius_msg_add_mppe_keys(struct radius_msg *msg,
			     const u8 *req_authenticator,
			     const u8 *secret, size_t secret_len,
			     const u8 *send_key, size_t send_key_len,
			     const u8 *recv_key, size_t recv_key_len)
{
	struct radius_attr_hdr *attr;
	u32 vendor_id = htonl(RADIUS_VENDOR_ID_MICROSOFT);
	u8 *buf;
	struct radius_attr_vendor *vhdr;
	u8 *pos;
	size_t elen;
	int hlen;
	u16 salt;

	hlen = sizeof(vendor_id) + sizeof(*vhdr) + 2;

	/* MS-MPPE-Send-Key */
	buf = os_malloc(hlen + send_key_len + 16);
	if (buf == NULL) {
		return 0;
	}
	pos = buf;
	os_memcpy(pos, &vendor_id, sizeof(vendor_id));
	pos += sizeof(vendor_id);
	vhdr = (struct radius_attr_vendor *) pos;
	vhdr->vendor_type = RADIUS_VENDOR_ATTR_MS_MPPE_SEND_KEY;
	pos = (u8 *) (vhdr + 1);
	if (os_get_random((u8 *) &salt, sizeof(salt)) < 0) {
		os_free(buf);
		return 0;
	}
	salt |= 0x8000;
	WPA_PUT_BE16(pos, salt);
	pos += 2;
	encrypt_ms_key(send_key, send_key_len, salt, req_authenticator, secret,
		       secret_len, pos, &elen);
	vhdr->vendor_length = hlen + elen - sizeof(vendor_id);

	attr = radius_msg_add_attr(msg, RADIUS_ATTR_VENDOR_SPECIFIC,
				   buf, hlen + elen);
	os_free(buf);
	if (attr == NULL) {
		return 0;
	}

	/* MS-MPPE-Recv-Key */
	buf = os_malloc(hlen + recv_key_len + 16);
	if (buf == NULL) {
		return 0;
	}
	pos = buf;
	os_memcpy(pos, &vendor_id, sizeof(vendor_id));
	pos += sizeof(vendor_id);
	vhdr = (struct radius_attr_vendor *) pos;
	vhdr->vendor_type = RADIUS_VENDOR_ATTR_MS_MPPE_RECV_KEY;
	pos = (u8 *) (vhdr + 1);
	salt ^= 1;
	WPA_PUT_BE16(pos, salt);
	pos += 2;
	encrypt_ms_key(recv_key, recv_key_len, salt, req_authenticator, secret,
		       secret_len, pos, &elen);
	vhdr->vendor_length = hlen + elen - sizeof(vendor_id);

	attr = radius_msg_add_attr(msg, RADIUS_ATTR_VENDOR_SPECIFIC,
				   buf, hlen + elen);
	os_free(buf);
	if (attr == NULL) {
		return 0;
	}

	return 1;
}


int radius_msg_add_wfa(struct radius_msg *msg, u8 subtype, const u8 *data,
		       size_t len)
{
	struct radius_attr_hdr *attr;
	u8 *buf, *pos;
	size_t alen;

	alen = 4 + 2 + len;
	buf = os_malloc(alen);
	if (buf == NULL)
		return 0;
	pos = buf;
	WPA_PUT_BE32(pos, RADIUS_VENDOR_ID_WFA);
	pos += 4;
	*pos++ = subtype;
	*pos++ = 2 + len;
	os_memcpy(pos, data, len);
	attr = radius_msg_add_attr(msg, RADIUS_ATTR_VENDOR_SPECIFIC,
				   buf, alen);
	os_free(buf);
	if (attr == NULL)
		return 0;

	return 1;
}


int radius_user_password_hide(struct radius_msg *msg,
			      const u8 *data, size_t data_len,
			      const u8 *secret, size_t secret_len,
			      u8 *buf, size_t buf_len)
{
	size_t padlen, i, pos;
	const u8 *addr[2];
	size_t len[2];
	u8 hash[16];

	if (data_len + 16 > buf_len)
		return -1;

	os_memcpy(buf, data, data_len);

	padlen = data_len % 16;
	if (padlen && data_len < buf_len) {
		padlen = 16 - padlen;
		os_memset(buf + data_len, 0, padlen);
		buf_len = data_len + padlen;
	} else {
		buf_len = data_len;
	}

	addr[0] = secret;
	len[0] = secret_len;
	addr[1] = msg->hdr->authenticator;
	len[1] = 16;
	md5_vector(2, addr, len, hash);

	for (i = 0; i < 16; i++)
		buf[i] ^= hash[i];
	pos = 16;

	while (pos < buf_len) {
		addr[0] = secret;
		len[0] = secret_len;
		addr[1] = &buf[pos - 16];
		len[1] = 16;
		md5_vector(2, addr, len, hash);

		for (i = 0; i < 16; i++)
			buf[pos + i] ^= hash[i];

		pos += 16;
	}

	return buf_len;
}


/* Add User-Password attribute to a RADIUS message and encrypt it as specified
 * in RFC 2865, Chap. 5.2 */
struct radius_attr_hdr *
radius_msg_add_attr_user_password(struct radius_msg *msg,
				  const u8 *data, size_t data_len,
				  const u8 *secret, size_t secret_len)
{
	u8 buf[128];
	int res;

	res = radius_user_password_hide(msg, data, data_len,
					secret, secret_len, buf, sizeof(buf));
	if (res < 0)
		return NULL;

	return radius_msg_add_attr(msg, RADIUS_ATTR_USER_PASSWORD,
				   buf, res);
}


int radius_msg_get_attr(struct radius_msg *msg, u8 type, u8 *buf, size_t len)
{
	struct radius_attr_hdr *attr = NULL, *tmp;
	size_t i, dlen;

	for (i = 0; i < msg->attr_used; i++) {
		tmp = radius_get_attr_hdr(msg, i);
		if (tmp->type == type) {
			attr = tmp;
			break;
		}
	}

	if (!attr || attr->length < sizeof(*attr))
		return -1;

	dlen = attr->length - sizeof(*attr);
	if (buf)
		os_memcpy(buf, (attr + 1), dlen > len ? len : dlen);
	return dlen;
}


int radius_msg_get_attr_ptr(struct radius_msg *msg, u8 type, u8 **buf,
			    size_t *len, const u8 *start)
{
	size_t i;
	struct radius_attr_hdr *attr = NULL, *tmp;

	for (i = 0; i < msg->attr_used; i++) {
		tmp = radius_get_attr_hdr(msg, i);
		if (tmp->type == type &&
		    (start == NULL || (u8 *) tmp > start)) {
			attr = tmp;
			break;
		}
	}

	if (!attr || attr->length < sizeof(*attr))
		return -1;

	*buf = (u8 *) (attr + 1);
	*len = attr->length - sizeof(*attr);
	return 0;
}


int radius_msg_count_attr(struct radius_msg *msg, u8 type, int min_len)
{
	size_t i;
	int count;

	for (count = 0, i = 0; i < msg->attr_used; i++) {
		struct radius_attr_hdr *attr = radius_get_attr_hdr(msg, i);
		if (attr->type == type &&
		    attr->length >= sizeof(struct radius_attr_hdr) + min_len)
			count++;
	}

	return count;
}


struct radius_tunnel_attrs {
	int tag_used;
	int type; /* Tunnel-Type */
	int medium_type; /* Tunnel-Medium-Type */
	int vlanid;
};


static int cmp_int(const void *a, const void *b)
{
	int x, y;

	x = *((int *) a);
	y = *((int *) b);
	return (x - y);
}


/**
 * radius_msg_get_vlanid - Parse RADIUS attributes for VLAN tunnel information
 * The k tagged vlans found are sorted by vlan_id and stored in the first k
 * items of tagged.
 *
 * @msg: RADIUS message
 * @untagged: Pointer to store untagged vid
 * @numtagged: Size of tagged
 * @tagged: Pointer to store tagged list
 *
 * Returns: 0 if neither tagged nor untagged configuration is found, 1 otherwise
 */
int radius_msg_get_vlanid(struct radius_msg *msg, int *untagged, int numtagged,
			  int *tagged)
{
	struct radius_tunnel_attrs tunnel[RADIUS_TUNNEL_TAGS], *tun;
	size_t i;
	struct radius_attr_hdr *attr = NULL;
	const u8 *data;
	char buf[10];
	size_t dlen;
	int j, taggedidx = 0, vlan_id;

	os_memset(&tunnel, 0, sizeof(tunnel));
	for (j = 0; j < numtagged; j++)
		tagged[j] = 0;
	*untagged = 0;

	for (i = 0; i < msg->attr_used; i++) {
		attr = radius_get_attr_hdr(msg, i);
		if (attr->length < sizeof(*attr))
			return -1;
		data = (const u8 *) (attr + 1);
		dlen = attr->length - sizeof(*attr);
		if (attr->length < 3)
			continue;
		if (data[0] >= RADIUS_TUNNEL_TAGS)
			tun = &tunnel[0];
		else
			tun = &tunnel[data[0]];

		switch (attr->type) {
		case RADIUS_ATTR_TUNNEL_TYPE:
			if (attr->length != 6)
				break;
			tun->tag_used++;
			tun->type = WPA_GET_BE24(data + 1);
			break;
		case RADIUS_ATTR_TUNNEL_MEDIUM_TYPE:
			if (attr->length != 6)
				break;
			tun->tag_used++;
			tun->medium_type = WPA_GET_BE24(data + 1);
			break;
		case RADIUS_ATTR_TUNNEL_PRIVATE_GROUP_ID:
			if (data[0] < RADIUS_TUNNEL_TAGS) {
				data++;
				dlen--;
			}
			if (dlen >= sizeof(buf))
				break;
			os_memcpy(buf, data, dlen);
			buf[dlen] = '\0';
			vlan_id = atoi(buf);
			if (vlan_id <= 0)
				break;
			tun->tag_used++;
			tun->vlanid = vlan_id;
			break;
		case RADIUS_ATTR_EGRESS_VLANID: /* RFC 4675 */
			if (attr->length != 6)
				break;
			vlan_id = WPA_GET_BE24(data + 1);
			if (vlan_id <= 0)
				break;
			if (data[0] == 0x32)
				*untagged = vlan_id;
			else if (data[0] == 0x31 && tagged &&
				 taggedidx < numtagged)
				tagged[taggedidx++] = vlan_id;
			break;
		}
	}

	/* Use tunnel with the lowest tag for untagged VLAN id */
	for (i = 0; i < RADIUS_TUNNEL_TAGS; i++) {
		tun = &tunnel[i];
		if (tun->tag_used &&
		    tun->type == RADIUS_TUNNEL_TYPE_VLAN &&
		    tun->medium_type == RADIUS_TUNNEL_MEDIUM_TYPE_802 &&
		    tun->vlanid > 0) {
			*untagged = tun->vlanid;
			break;
		}
	}

	if (taggedidx)
		qsort(tagged, taggedidx, sizeof(int), cmp_int);

	if (*untagged > 0 || taggedidx)
		return 1;
	return 0;
}


/**
 * radius_msg_get_tunnel_password - Parse RADIUS attribute Tunnel-Password
 * @msg: Received RADIUS message
 * @keylen: Length of returned password
 * @secret: RADIUS shared secret
 * @secret_len: Length of secret
 * @sent_msg: Sent RADIUS message
 * @n: Number of password attribute to return (starting with 0)
 * Returns: Pointer to n-th password (free with os_free) or %NULL
 */
char * radius_msg_get_tunnel_password(struct radius_msg *msg, int *keylen,
				      const u8 *secret, size_t secret_len,
				      struct radius_msg *sent_msg, size_t n)
{
	u8 *buf = NULL;
	size_t buflen;
	const u8 *salt;
	u8 *str;
	const u8 *addr[3];
	size_t len[3];
	u8 hash[16];
	u8 *pos;
	size_t i, j = 0;
	struct radius_attr_hdr *attr;
	const u8 *data;
	size_t dlen;
	const u8 *fdata = NULL; /* points to found item */
	size_t fdlen = -1;
	char *ret = NULL;

	/* find n-th valid Tunnel-Password attribute */
	for (i = 0; i < msg->attr_used; i++) {
		attr = radius_get_attr_hdr(msg, i);
		if (attr == NULL ||
		    attr->type != RADIUS_ATTR_TUNNEL_PASSWORD) {
			continue;
		}
		if (attr->length <= 5)
			continue;
		data = (const u8 *) (attr + 1);
		dlen = attr->length - sizeof(*attr);
		if (dlen <= 3 || dlen % 16 != 3)
			continue;
		j++;
		if (j <= n)
			continue;

		fdata = data;
		fdlen = dlen;
		break;
	}
	if (fdata == NULL)
		goto out;

	/* alloc writable memory for decryption */
	buf = os_memdup(fdata, fdlen);
	if (buf == NULL)
		goto out;
	buflen = fdlen;

	/* init pointers */
	salt = buf + 1;
	str = buf + 3;

	/* decrypt blocks */
	pos = buf + buflen - 16; /* last block */
	while (pos >= str + 16) { /* all but the first block */
		addr[0] = secret;
		len[0] = secret_len;
		addr[1] = pos - 16;
		len[1] = 16;
		md5_vector(2, addr, len, hash);

		for (i = 0; i < 16; i++)
			pos[i] ^= hash[i];

		pos -= 16;
	}

	/* decrypt first block */
	if (str != pos)
		goto out;
	addr[0] = secret;
	len[0] = secret_len;
	addr[1] = sent_msg->hdr->authenticator;
	len[1] = 16;
	addr[2] = salt;
	len[2] = 2;
	md5_vector(3, addr, len, hash);

	for (i = 0; i < 16; i++)
		pos[i] ^= hash[i];

	/* derive plaintext length from first subfield */
	*keylen = (unsigned char) str[0];
	if ((u8 *) (str + *keylen) >= (u8 *) (buf + buflen)) {
		/* decryption error - invalid key length */
		goto out;
	}
	if (*keylen == 0) {
		/* empty password */
		goto out;
	}

	/* copy passphrase into new buffer */
	ret = os_malloc(*keylen);
	if (ret)
		os_memcpy(ret, str + 1, *keylen);

out:
	/* return new buffer */
	os_free(buf);
	return ret;
}


void radius_free_class(struct radius_class_data *c)
{
	size_t i;
	if (c == NULL)
		return;
	for (i = 0; i < c->count; i++)
		os_free(c->attr[i].data);
	os_free(c->attr);
	c->attr = NULL;
	c->count = 0;
}


int radius_copy_class(struct radius_class_data *dst,
		      const struct radius_class_data *src)
{
	size_t i;

	if (src->attr == NULL)
		return 0;

	dst->attr = os_calloc(src->count, sizeof(struct radius_attr_data));
	if (dst->attr == NULL)
		return -1;

	dst->count = 0;

	for (i = 0; i < src->count; i++) {
		dst->attr[i].data = os_memdup(src->attr[i].data,
					      src->attr[i].len);
		if (dst->attr[i].data == NULL)
			break;
		dst->count++;
		dst->attr[i].len = src->attr[i].len;
	}

	return 0;
}


u8 radius_msg_find_unlisted_attr(struct radius_msg *msg, u8 *attrs)
{
	size_t i, j;
	struct radius_attr_hdr *attr;

	for (i = 0; i < msg->attr_used; i++) {
		attr = radius_get_attr_hdr(msg, i);

		for (j = 0; attrs[j]; j++) {
			if (attr->type == attrs[j])
				break;
		}

		if (attrs[j] == 0)
			return attr->type; /* unlisted attr */
	}

	return 0;
}


int radius_gen_session_id(u8 *id, size_t len)
{
	/*
	 * Acct-Session-Id and Acct-Multi-Session-Id should be globally and
	 * temporarily unique. A high quality random number is required
	 * therefore. This could be be improved by switching to a GUID.
	 */
	return os_get_random(id, len);
}
