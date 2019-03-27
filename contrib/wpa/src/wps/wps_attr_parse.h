/*
 * Wi-Fi Protected Setup - attribute parsing
 * Copyright (c) 2008-2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPS_ATTR_PARSE_H
#define WPS_ATTR_PARSE_H

#include "wps.h"

struct wps_parse_attr {
	/* fixed length fields */
	const u8 *version; /* 1 octet */
	const u8 *version2; /* 1 octet */
	const u8 *msg_type; /* 1 octet */
	const u8 *enrollee_nonce; /* WPS_NONCE_LEN (16) octets */
	const u8 *registrar_nonce; /* WPS_NONCE_LEN (16) octets */
	const u8 *uuid_r; /* WPS_UUID_LEN (16) octets */
	const u8 *uuid_e; /* WPS_UUID_LEN (16) octets */
	const u8 *auth_type_flags; /* 2 octets */
	const u8 *encr_type_flags; /* 2 octets */
	const u8 *conn_type_flags; /* 1 octet */
	const u8 *config_methods; /* 2 octets */
	const u8 *sel_reg_config_methods; /* 2 octets */
	const u8 *primary_dev_type; /* 8 octets */
	const u8 *rf_bands; /* 1 octet */
	const u8 *assoc_state; /* 2 octets */
	const u8 *config_error; /* 2 octets */
	const u8 *dev_password_id; /* 2 octets */
	const u8 *os_version; /* 4 octets */
	const u8 *wps_state; /* 1 octet */
	const u8 *authenticator; /* WPS_AUTHENTICATOR_LEN (8) octets */
	const u8 *r_hash1; /* WPS_HASH_LEN (32) octets */
	const u8 *r_hash2; /* WPS_HASH_LEN (32) octets */
	const u8 *e_hash1; /* WPS_HASH_LEN (32) octets */
	const u8 *e_hash2; /* WPS_HASH_LEN (32) octets */
	const u8 *r_snonce1; /* WPS_SECRET_NONCE_LEN (16) octets */
	const u8 *r_snonce2; /* WPS_SECRET_NONCE_LEN (16) octets */
	const u8 *e_snonce1; /* WPS_SECRET_NONCE_LEN (16) octets */
	const u8 *e_snonce2; /* WPS_SECRET_NONCE_LEN (16) octets */
	const u8 *key_wrap_auth; /* WPS_KWA_LEN (8) octets */
	const u8 *auth_type; /* 2 octets */
	const u8 *encr_type; /* 2 octets */
	const u8 *network_idx; /* 1 octet */
	const u8 *network_key_idx; /* 1 octet */
	const u8 *mac_addr; /* ETH_ALEN (6) octets */
	const u8 *selected_registrar; /* 1 octet (Bool) */
	const u8 *request_type; /* 1 octet */
	const u8 *response_type; /* 1 octet */
	const u8 *ap_setup_locked; /* 1 octet */
	const u8 *settings_delay_time; /* 1 octet */
	const u8 *network_key_shareable; /* 1 octet (Bool) */
	const u8 *request_to_enroll; /* 1 octet (Bool) */
	const u8 *ap_channel; /* 2 octets */
	const u8 *registrar_configuration_methods; /* 2 octets */

	/* variable length fields */
	const u8 *manufacturer;
	const u8 *model_name;
	const u8 *model_number;
	const u8 *serial_number;
	const u8 *dev_name;
	const u8 *public_key;
	const u8 *encr_settings;
	const u8 *ssid; /* <= 32 octets */
	const u8 *network_key; /* <= 64 octets */
	const u8 *authorized_macs; /* <= 30 octets */
	const u8 *sec_dev_type_list; /* <= 128 octets */
	const u8 *oob_dev_password; /* 38..54 octets */
	u16 manufacturer_len;
	u16 model_name_len;
	u16 model_number_len;
	u16 serial_number_len;
	u16 dev_name_len;
	u16 public_key_len;
	u16 encr_settings_len;
	u16 ssid_len;
	u16 network_key_len;
	u16 authorized_macs_len;
	u16 sec_dev_type_list_len;
	u16 oob_dev_password_len;

	/* attributes that can occur multiple times */
#define MAX_CRED_COUNT 10
#define MAX_REQ_DEV_TYPE_COUNT 10

	unsigned int num_cred;
	unsigned int num_req_dev_type;
	unsigned int num_vendor_ext;

	u16 cred_len[MAX_CRED_COUNT];
	u16 vendor_ext_len[MAX_WPS_PARSE_VENDOR_EXT];

	const u8 *cred[MAX_CRED_COUNT];
	const u8 *req_dev_type[MAX_REQ_DEV_TYPE_COUNT];
	const u8 *vendor_ext[MAX_WPS_PARSE_VENDOR_EXT];
};

int wps_parse_msg(const struct wpabuf *msg, struct wps_parse_attr *attr);

#endif /* WPS_ATTR_PARSE_H */
