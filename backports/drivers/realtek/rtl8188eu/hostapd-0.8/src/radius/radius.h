/*
 * RADIUS message processing
 * Copyright (c) 2002-2009, Jouni Malinen <j@w1.fi>
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

#ifndef RADIUS_H
#define RADIUS_H

/* RFC 2865 - RADIUS */

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif /* _MSC_VER */

struct radius_hdr {
	u8 code;
	u8 identifier;
	u16 length; /* including this header */
	u8 authenticator[16];
	/* followed by length-20 octets of attributes */
} STRUCT_PACKED;

enum { RADIUS_CODE_ACCESS_REQUEST = 1,
       RADIUS_CODE_ACCESS_ACCEPT = 2,
       RADIUS_CODE_ACCESS_REJECT = 3,
       RADIUS_CODE_ACCOUNTING_REQUEST = 4,
       RADIUS_CODE_ACCOUNTING_RESPONSE = 5,
       RADIUS_CODE_ACCESS_CHALLENGE = 11,
       RADIUS_CODE_STATUS_SERVER = 12,
       RADIUS_CODE_STATUS_CLIENT = 13,
       RADIUS_CODE_RESERVED = 255
};

struct radius_attr_hdr {
	u8 type;
	u8 length; /* including this header */
	/* followed by length-2 octets of attribute value */
} STRUCT_PACKED;

#define RADIUS_MAX_ATTR_LEN (255 - sizeof(struct radius_attr_hdr))

enum { RADIUS_ATTR_USER_NAME = 1,
       RADIUS_ATTR_USER_PASSWORD = 2,
       RADIUS_ATTR_NAS_IP_ADDRESS = 4,
       RADIUS_ATTR_NAS_PORT = 5,
       RADIUS_ATTR_FRAMED_MTU = 12,
       RADIUS_ATTR_REPLY_MESSAGE = 18,
       RADIUS_ATTR_STATE = 24,
       RADIUS_ATTR_CLASS = 25,
       RADIUS_ATTR_VENDOR_SPECIFIC = 26,
       RADIUS_ATTR_SESSION_TIMEOUT = 27,
       RADIUS_ATTR_IDLE_TIMEOUT = 28,
       RADIUS_ATTR_TERMINATION_ACTION = 29,
       RADIUS_ATTR_CALLED_STATION_ID = 30,
       RADIUS_ATTR_CALLING_STATION_ID = 31,
       RADIUS_ATTR_NAS_IDENTIFIER = 32,
       RADIUS_ATTR_PROXY_STATE = 33,
       RADIUS_ATTR_ACCT_STATUS_TYPE = 40,
       RADIUS_ATTR_ACCT_DELAY_TIME = 41,
       RADIUS_ATTR_ACCT_INPUT_OCTETS = 42,
       RADIUS_ATTR_ACCT_OUTPUT_OCTETS = 43,
       RADIUS_ATTR_ACCT_SESSION_ID = 44,
       RADIUS_ATTR_ACCT_AUTHENTIC = 45,
       RADIUS_ATTR_ACCT_SESSION_TIME = 46,
       RADIUS_ATTR_ACCT_INPUT_PACKETS = 47,
       RADIUS_ATTR_ACCT_OUTPUT_PACKETS = 48,
       RADIUS_ATTR_ACCT_TERMINATE_CAUSE = 49,
       RADIUS_ATTR_ACCT_MULTI_SESSION_ID = 50,
       RADIUS_ATTR_ACCT_LINK_COUNT = 51,
       RADIUS_ATTR_ACCT_INPUT_GIGAWORDS = 52,
       RADIUS_ATTR_ACCT_OUTPUT_GIGAWORDS = 53,
       RADIUS_ATTR_EVENT_TIMESTAMP = 55,
       RADIUS_ATTR_NAS_PORT_TYPE = 61,
       RADIUS_ATTR_TUNNEL_TYPE = 64,
       RADIUS_ATTR_TUNNEL_MEDIUM_TYPE = 65,
       RADIUS_ATTR_CONNECT_INFO = 77,
       RADIUS_ATTR_EAP_MESSAGE = 79,
       RADIUS_ATTR_MESSAGE_AUTHENTICATOR = 80,
       RADIUS_ATTR_TUNNEL_PRIVATE_GROUP_ID = 81,
       RADIUS_ATTR_ACCT_INTERIM_INTERVAL = 85,
       RADIUS_ATTR_CHARGEABLE_USER_IDENTITY = 89,
       RADIUS_ATTR_NAS_IPV6_ADDRESS = 95
};


/* Termination-Action */
#define RADIUS_TERMINATION_ACTION_DEFAULT 0
#define RADIUS_TERMINATION_ACTION_RADIUS_REQUEST 1

/* NAS-Port-Type */
#define RADIUS_NAS_PORT_TYPE_IEEE_802_11 19

/* Acct-Status-Type */
#define RADIUS_ACCT_STATUS_TYPE_START 1
#define RADIUS_ACCT_STATUS_TYPE_STOP 2
#define RADIUS_ACCT_STATUS_TYPE_INTERIM_UPDATE 3
#define RADIUS_ACCT_STATUS_TYPE_ACCOUNTING_ON 7
#define RADIUS_ACCT_STATUS_TYPE_ACCOUNTING_OFF 8

/* Acct-Authentic */
#define RADIUS_ACCT_AUTHENTIC_RADIUS 1
#define RADIUS_ACCT_AUTHENTIC_LOCAL 2
#define RADIUS_ACCT_AUTHENTIC_REMOTE 3

/* Acct-Terminate-Cause */
#define RADIUS_ACCT_TERMINATE_CAUSE_USER_REQUEST 1
#define RADIUS_ACCT_TERMINATE_CAUSE_LOST_CARRIER 2
#define RADIUS_ACCT_TERMINATE_CAUSE_LOST_SERVICE 3
#define RADIUS_ACCT_TERMINATE_CAUSE_IDLE_TIMEOUT 4
#define RADIUS_ACCT_TERMINATE_CAUSE_SESSION_TIMEOUT 5
#define RADIUS_ACCT_TERMINATE_CAUSE_ADMIN_RESET 6
#define RADIUS_ACCT_TERMINATE_CAUSE_ADMIN_REBOOT 7
#define RADIUS_ACCT_TERMINATE_CAUSE_PORT_ERROR 8
#define RADIUS_ACCT_TERMINATE_CAUSE_NAS_ERROR 9
#define RADIUS_ACCT_TERMINATE_CAUSE_NAS_REQUEST 10
#define RADIUS_ACCT_TERMINATE_CAUSE_NAS_REBOOT 11
#define RADIUS_ACCT_TERMINATE_CAUSE_PORT_UNNEEDED 12
#define RADIUS_ACCT_TERMINATE_CAUSE_PORT_PREEMPTED 13
#define RADIUS_ACCT_TERMINATE_CAUSE_PORT_SUSPENDED 14
#define RADIUS_ACCT_TERMINATE_CAUSE_SERVICE_UNAVAILABLE 15
#define RADIUS_ACCT_TERMINATE_CAUSE_CALLBACK 16
#define RADIUS_ACCT_TERMINATE_CAUSE_USER_ERROR 17
#define RADIUS_ACCT_TERMINATE_CAUSE_HOST_REQUEST 18

#define RADIUS_TUNNEL_TAGS 32

/* Tunnel-Type */
#define RADIUS_TUNNEL_TYPE_PPTP 1
#define RADIUS_TUNNEL_TYPE_L2TP 3
#define RADIUS_TUNNEL_TYPE_IPIP 7
#define RADIUS_TUNNEL_TYPE_GRE 10
#define RADIUS_TUNNEL_TYPE_VLAN 13

/* Tunnel-Medium-Type */
#define RADIUS_TUNNEL_MEDIUM_TYPE_IPV4 1
#define RADIUS_TUNNEL_MEDIUM_TYPE_IPV6 2
#define RADIUS_TUNNEL_MEDIUM_TYPE_802 6


struct radius_attr_vendor {
	u8 vendor_type;
	u8 vendor_length;
} STRUCT_PACKED;

#define RADIUS_VENDOR_ID_CISCO 9
#define RADIUS_CISCO_AV_PAIR 1

/* RFC 2548 - Microsoft Vendor-specific RADIUS Attributes */
#define RADIUS_VENDOR_ID_MICROSOFT 311

enum { RADIUS_VENDOR_ATTR_MS_MPPE_SEND_KEY = 16,
       RADIUS_VENDOR_ATTR_MS_MPPE_RECV_KEY = 17
};

#ifdef _MSC_VER
#pragma pack(pop)
#endif /* _MSC_VER */

struct radius_ms_mppe_keys {
	u8 *send;
	size_t send_len;
	u8 *recv;
	size_t recv_len;
};


struct radius_msg;

/* Default size to be allocated for new RADIUS messages */
#define RADIUS_DEFAULT_MSG_SIZE 1024

/* Default size to be allocated for attribute array */
#define RADIUS_DEFAULT_ATTR_COUNT 16


/* MAC address ASCII format for IEEE 802.1X use
 * (draft-congdon-radius-8021x-20.txt) */
#define RADIUS_802_1X_ADDR_FORMAT "%02X-%02X-%02X-%02X-%02X-%02X"
/* MAC address ASCII format for non-802.1X use */
#define RADIUS_ADDR_FORMAT "%02x%02x%02x%02x%02x%02x"

struct radius_hdr * radius_msg_get_hdr(struct radius_msg *msg);
struct wpabuf * radius_msg_get_buf(struct radius_msg *msg);
struct radius_msg * radius_msg_new(u8 code, u8 identifier);
void radius_msg_free(struct radius_msg *msg);
void radius_msg_dump(struct radius_msg *msg);
int radius_msg_finish(struct radius_msg *msg, const u8 *secret,
		      size_t secret_len);
int radius_msg_finish_srv(struct radius_msg *msg, const u8 *secret,
			  size_t secret_len, const u8 *req_authenticator);
void radius_msg_finish_acct(struct radius_msg *msg, const u8 *secret,
			    size_t secret_len);
struct radius_attr_hdr * radius_msg_add_attr(struct radius_msg *msg, u8 type,
					     const u8 *data, size_t data_len);
struct radius_msg * radius_msg_parse(const u8 *data, size_t len);
int radius_msg_add_eap(struct radius_msg *msg, const u8 *data,
		       size_t data_len);
u8 *radius_msg_get_eap(struct radius_msg *msg, size_t *len);
int radius_msg_verify(struct radius_msg *msg, const u8 *secret,
		      size_t secret_len, struct radius_msg *sent_msg,
		      int auth);
int radius_msg_verify_msg_auth(struct radius_msg *msg, const u8 *secret,
			       size_t secret_len, const u8 *req_auth);
int radius_msg_copy_attr(struct radius_msg *dst, struct radius_msg *src,
			 u8 type);
void radius_msg_make_authenticator(struct radius_msg *msg,
				   const u8 *data, size_t len);
struct radius_ms_mppe_keys *
radius_msg_get_ms_keys(struct radius_msg *msg, struct radius_msg *sent_msg,
		       const u8 *secret, size_t secret_len);
struct radius_ms_mppe_keys *
radius_msg_get_cisco_keys(struct radius_msg *msg, struct radius_msg *sent_msg,
			  const u8 *secret, size_t secret_len);
int radius_msg_add_mppe_keys(struct radius_msg *msg,
			     const u8 *req_authenticator,
			     const u8 *secret, size_t secret_len,
			     const u8 *send_key, size_t send_key_len,
			     const u8 *recv_key, size_t recv_key_len);
struct radius_attr_hdr *
radius_msg_add_attr_user_password(struct radius_msg *msg,
				  const u8 *data, size_t data_len,
				  const u8 *secret, size_t secret_len);
int radius_msg_get_attr(struct radius_msg *msg, u8 type, u8 *buf, size_t len);
int radius_msg_get_vlanid(struct radius_msg *msg);

static inline int radius_msg_add_attr_int32(struct radius_msg *msg, u8 type,
					    u32 value)
{
	u32 val = htonl(value);
	return radius_msg_add_attr(msg, type, (u8 *) &val, 4) != NULL;
}

static inline int radius_msg_get_attr_int32(struct radius_msg *msg, u8 type,
					    u32 *value)
{
	u32 val;
	int res;
	res = radius_msg_get_attr(msg, type, (u8 *) &val, 4);
	if (res != 4)
		return -1;

	*value = ntohl(val);
	return 0;
}
int radius_msg_get_attr_ptr(struct radius_msg *msg, u8 type, u8 **buf,
			    size_t *len, const u8 *start);
int radius_msg_count_attr(struct radius_msg *msg, u8 type, int min_len);


struct radius_attr_data {
	u8 *data;
	size_t len;
};

struct radius_class_data {
	struct radius_attr_data *attr;
	size_t count;
};

void radius_free_class(struct radius_class_data *c);
int radius_copy_class(struct radius_class_data *dst,
		      const struct radius_class_data *src);

#endif /* RADIUS_H */
