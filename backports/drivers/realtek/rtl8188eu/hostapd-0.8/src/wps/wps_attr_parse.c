/*
 * Wi-Fi Protected Setup - attribute parsing
 * Copyright (c) 2008, Jouni Malinen <j@w1.fi>
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
#include "wps_i.h"

#ifndef CONFIG_WPS_STRICT
#define WPS_WORKAROUNDS
#endif /* CONFIG_WPS_STRICT */


static int wps_set_vendor_ext_wfa_subelem(struct wps_parse_attr *attr,
					  u8 id, u8 len, const u8 *pos)
{
	wpa_printf(MSG_EXCESSIVE, "WPS: WFA subelement id=%u len=%u",
		   id, len);
	switch (id) {
	case WFA_ELEM_VERSION2:
		if (len != 1) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Version2 length "
				   "%u", len);
			return -1;
		}
		attr->version2 = pos;
		break;
	case WFA_ELEM_AUTHORIZEDMACS:
		attr->authorized_macs = pos;
		attr->authorized_macs_len = len;
		break;
	case WFA_ELEM_NETWORK_KEY_SHAREABLE:
		if (len != 1) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Network Key "
				   "Shareable length %u", len);
			return -1;
		}
		attr->network_key_shareable = pos;
		break;
	case WFA_ELEM_REQUEST_TO_ENROLL:
		if (len != 1) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Request to Enroll "
				   "length %u", len);
			return -1;
		}
		attr->request_to_enroll = pos;
		break;
	case WFA_ELEM_SETTINGS_DELAY_TIME:
		if (len != 1) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Settings Delay "
				   "Time length %u", len);
			return -1;
		}
		attr->settings_delay_time = pos;
		break;
	default:
		wpa_printf(MSG_MSGDUMP, "WPS: Skipped unknown WFA Vendor "
			   "Extension subelement %u", id);
		break;
	}

	return 0;
}


static int wps_parse_vendor_ext_wfa(struct wps_parse_attr *attr, const u8 *pos,
				    u16 len)
{
	const u8 *end = pos + len;
	u8 id, elen;

	while (pos + 2 < end) {
		id = *pos++;
		elen = *pos++;
		if (pos + elen > end)
			break;
		if (wps_set_vendor_ext_wfa_subelem(attr, id, elen, pos) < 0)
			return -1;
		pos += elen;
	}

	return 0;
}


static int wps_parse_vendor_ext(struct wps_parse_attr *attr, const u8 *pos,
				u16 len)
{
	u32 vendor_id;

	if (len < 3) {
		wpa_printf(MSG_DEBUG, "WPS: Skip invalid Vendor Extension");
		return 0;
	}

	vendor_id = WPA_GET_BE24(pos);
	switch (vendor_id) {
	case WPS_VENDOR_ID_WFA:
		return wps_parse_vendor_ext_wfa(attr, pos + 3, len - 3);
	}

	/* Handle unknown vendor extensions */

	wpa_printf(MSG_MSGDUMP, "WPS: Unknown Vendor Extension (Vendor ID %u)",
		   vendor_id);

	if (len > WPS_MAX_VENDOR_EXT_LEN) {
		wpa_printf(MSG_DEBUG, "WPS: Too long Vendor Extension (%u)",
			   len);
		return -1;
	}

	if (attr->num_vendor_ext >= MAX_WPS_PARSE_VENDOR_EXT) {
		wpa_printf(MSG_DEBUG, "WPS: Skipped Vendor Extension "
			   "attribute (max %d vendor extensions)",
			   MAX_WPS_PARSE_VENDOR_EXT);
		return -1;
	}
	attr->vendor_ext[attr->num_vendor_ext] = pos;
	attr->vendor_ext_len[attr->num_vendor_ext] = len;
	attr->num_vendor_ext++;

	return 0;
}


static int wps_set_attr(struct wps_parse_attr *attr, u16 type,
			const u8 *pos, u16 len)
{
	switch (type) {
	case ATTR_VERSION:
		if (len != 1) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Version length %u",
				   len);
			return -1;
		}
		attr->version = pos;
		break;
	case ATTR_MSG_TYPE:
		if (len != 1) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Message Type "
				   "length %u", len);
			return -1;
		}
		attr->msg_type = pos;
		break;
	case ATTR_ENROLLEE_NONCE:
		if (len != WPS_NONCE_LEN) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Enrollee Nonce "
				   "length %u", len);
			return -1;
		}
		attr->enrollee_nonce = pos;
		break;
	case ATTR_REGISTRAR_NONCE:
		if (len != WPS_NONCE_LEN) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Registrar Nonce "
				   "length %u", len);
			return -1;
		}
		attr->registrar_nonce = pos;
		break;
	case ATTR_UUID_E:
		if (len != WPS_UUID_LEN) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid UUID-E length %u",
				   len);
			return -1;
		}
		attr->uuid_e = pos;
		break;
	case ATTR_UUID_R:
		if (len != WPS_UUID_LEN) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid UUID-R length %u",
				   len);
			return -1;
		}
		attr->uuid_r = pos;
		break;
	case ATTR_AUTH_TYPE_FLAGS:
		if (len != 2) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Authentication "
				   "Type Flags length %u", len);
			return -1;
		}
		attr->auth_type_flags = pos;
		break;
	case ATTR_ENCR_TYPE_FLAGS:
		if (len != 2) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Encryption Type "
				   "Flags length %u", len);
			return -1;
		}
		attr->encr_type_flags = pos;
		break;
	case ATTR_CONN_TYPE_FLAGS:
		if (len != 1) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Connection Type "
				   "Flags length %u", len);
			return -1;
		}
		attr->conn_type_flags = pos;
		break;
	case ATTR_CONFIG_METHODS:
		if (len != 2) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Config Methods "
				   "length %u", len);
			return -1;
		}
		attr->config_methods = pos;
		break;
	case ATTR_SELECTED_REGISTRAR_CONFIG_METHODS:
		if (len != 2) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Selected "
				   "Registrar Config Methods length %u", len);
			return -1;
		}
		attr->sel_reg_config_methods = pos;
		break;
	case ATTR_PRIMARY_DEV_TYPE:
		if (len != WPS_DEV_TYPE_LEN) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Primary Device "
				   "Type length %u", len);
			return -1;
		}
		attr->primary_dev_type = pos;
		break;
	case ATTR_RF_BANDS:
		if (len != 1) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid RF Bands length "
				   "%u", len);
			return -1;
		}
		attr->rf_bands = pos;
		break;
	case ATTR_ASSOC_STATE:
		if (len != 2) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Association State "
				   "length %u", len);
			return -1;
		}
		attr->assoc_state = pos;
		break;
	case ATTR_CONFIG_ERROR:
		if (len != 2) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Configuration "
				   "Error length %u", len);
			return -1;
		}
		attr->config_error = pos;
		break;
	case ATTR_DEV_PASSWORD_ID:
		if (len != 2) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Device Password "
				   "ID length %u", len);
			return -1;
		}
		attr->dev_password_id = pos;
		break;
	case ATTR_OOB_DEVICE_PASSWORD:
		if (len != WPS_OOB_DEVICE_PASSWORD_ATTR_LEN) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid OOB Device "
				   "Password length %u", len);
			return -1;
		}
		attr->oob_dev_password = pos;
		break;
	case ATTR_OS_VERSION:
		if (len != 4) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid OS Version length "
				   "%u", len);
			return -1;
		}
		attr->os_version = pos;
		break;
	case ATTR_WPS_STATE:
		if (len != 1) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Wi-Fi Protected "
				   "Setup State length %u", len);
			return -1;
		}
		attr->wps_state = pos;
		break;
	case ATTR_AUTHENTICATOR:
		if (len != WPS_AUTHENTICATOR_LEN) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Authenticator "
				   "length %u", len);
			return -1;
		}
		attr->authenticator = pos;
		break;
	case ATTR_R_HASH1:
		if (len != WPS_HASH_LEN) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid R-Hash1 length %u",
				   len);
			return -1;
		}
		attr->r_hash1 = pos;
		break;
	case ATTR_R_HASH2:
		if (len != WPS_HASH_LEN) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid R-Hash2 length %u",
				   len);
			return -1;
		}
		attr->r_hash2 = pos;
		break;
	case ATTR_E_HASH1:
		if (len != WPS_HASH_LEN) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid E-Hash1 length %u",
				   len);
			return -1;
		}
		attr->e_hash1 = pos;
		break;
	case ATTR_E_HASH2:
		if (len != WPS_HASH_LEN) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid E-Hash2 length %u",
				   len);
			return -1;
		}
		attr->e_hash2 = pos;
		break;
	case ATTR_R_SNONCE1:
		if (len != WPS_SECRET_NONCE_LEN) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid R-SNonce1 length "
				   "%u", len);
			return -1;
		}
		attr->r_snonce1 = pos;
		break;
	case ATTR_R_SNONCE2:
		if (len != WPS_SECRET_NONCE_LEN) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid R-SNonce2 length "
				   "%u", len);
			return -1;
		}
		attr->r_snonce2 = pos;
		break;
	case ATTR_E_SNONCE1:
		if (len != WPS_SECRET_NONCE_LEN) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid E-SNonce1 length "
				   "%u", len);
			return -1;
		}
		attr->e_snonce1 = pos;
		break;
	case ATTR_E_SNONCE2:
		if (len != WPS_SECRET_NONCE_LEN) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid E-SNonce2 length "
				   "%u", len);
			return -1;
		}
		attr->e_snonce2 = pos;
		break;
	case ATTR_KEY_WRAP_AUTH:
		if (len != WPS_KWA_LEN) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Key Wrap "
				   "Authenticator length %u", len);
			return -1;
		}
		attr->key_wrap_auth = pos;
		break;
	case ATTR_AUTH_TYPE:
		if (len != 2) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Authentication "
				   "Type length %u", len);
			return -1;
		}
		attr->auth_type = pos;
		break;
	case ATTR_ENCR_TYPE:
		if (len != 2) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Encryption "
				   "Type length %u", len);
			return -1;
		}
		attr->encr_type = pos;
		break;
	case ATTR_NETWORK_INDEX:
		if (len != 1) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Network Index "
				   "length %u", len);
			return -1;
		}
		attr->network_idx = pos;
		break;
	case ATTR_NETWORK_KEY_INDEX:
		if (len != 1) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Network Key Index "
				   "length %u", len);
			return -1;
		}
		attr->network_key_idx = pos;
		break;
	case ATTR_MAC_ADDR:
		if (len != ETH_ALEN) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid MAC Address "
				   "length %u", len);
			return -1;
		}
		attr->mac_addr = pos;
		break;
	case ATTR_KEY_PROVIDED_AUTO:
		if (len != 1) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Key Provided "
				   "Automatically length %u", len);
			return -1;
		}
		attr->key_prov_auto = pos;
		break;
	case ATTR_802_1X_ENABLED:
		if (len != 1) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid 802.1X Enabled "
				   "length %u", len);
			return -1;
		}
		attr->dot1x_enabled = pos;
		break;
	case ATTR_SELECTED_REGISTRAR:
		if (len != 1) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Selected Registrar"
				   " length %u", len);
			return -1;
		}
		attr->selected_registrar = pos;
		break;
	case ATTR_REQUEST_TYPE:
		if (len != 1) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Request Type "
				   "length %u", len);
			return -1;
		}
		attr->request_type = pos;
		break;
	case ATTR_RESPONSE_TYPE:
		if (len != 1) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Response Type "
				   "length %u", len);
			return -1;
		}
		attr->response_type = pos;
		break;
	case ATTR_MANUFACTURER:
		attr->manufacturer = pos;
		attr->manufacturer_len = len;
		break;
	case ATTR_MODEL_NAME:
		attr->model_name = pos;
		attr->model_name_len = len;
		break;
	case ATTR_MODEL_NUMBER:
		attr->model_number = pos;
		attr->model_number_len = len;
		break;
	case ATTR_SERIAL_NUMBER:
		attr->serial_number = pos;
		attr->serial_number_len = len;
		break;
	case ATTR_DEV_NAME:
		attr->dev_name = pos;
		attr->dev_name_len = len;
		break;
	case ATTR_PUBLIC_KEY:
		attr->public_key = pos;
		attr->public_key_len = len;
		break;
	case ATTR_ENCR_SETTINGS:
		attr->encr_settings = pos;
		attr->encr_settings_len = len;
		break;
	case ATTR_CRED:
		if (attr->num_cred >= MAX_CRED_COUNT) {
			wpa_printf(MSG_DEBUG, "WPS: Skipped Credential "
				   "attribute (max %d credentials)",
				   MAX_CRED_COUNT);
			break;
		}
		attr->cred[attr->num_cred] = pos;
		attr->cred_len[attr->num_cred] = len;
		attr->num_cred++;
		break;
	case ATTR_SSID:
		attr->ssid = pos;
		attr->ssid_len = len;
		break;
	case ATTR_NETWORK_KEY:
		attr->network_key = pos;
		attr->network_key_len = len;
		break;
	case ATTR_EAP_TYPE:
		attr->eap_type = pos;
		attr->eap_type_len = len;
		break;
	case ATTR_EAP_IDENTITY:
		attr->eap_identity = pos;
		attr->eap_identity_len = len;
		break;
	case ATTR_AP_SETUP_LOCKED:
		if (len != 1) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid AP Setup Locked "
				   "length %u", len);
			return -1;
		}
		attr->ap_setup_locked = pos;
		break;
	case ATTR_REQUESTED_DEV_TYPE:
		if (len != WPS_DEV_TYPE_LEN) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Requested Device "
				   "Type length %u", len);
			return -1;
		}
		if (attr->num_req_dev_type >= MAX_REQ_DEV_TYPE_COUNT) {
			wpa_printf(MSG_DEBUG, "WPS: Skipped Requested Device "
				   "Type attribute (max %u types)",
				   MAX_REQ_DEV_TYPE_COUNT);
			break;
		}
		attr->req_dev_type[attr->num_req_dev_type] = pos;
		attr->num_req_dev_type++;
		break;
	case ATTR_SECONDARY_DEV_TYPE_LIST:
		if (len > WPS_SEC_DEV_TYPE_MAX_LEN ||
		    (len % WPS_DEV_TYPE_LEN) > 0) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid Secondary Device "
				   "Type length %u", len);
			return -1;
		}
		attr->sec_dev_type_list = pos;
		attr->sec_dev_type_list_len = len;
		break;
	case ATTR_VENDOR_EXT:
		if (wps_parse_vendor_ext(attr, pos, len) < 0)
			return -1;
		break;
	default:
		wpa_printf(MSG_DEBUG, "WPS: Unsupported attribute type 0x%x "
			   "len=%u", type, len);
		break;
	}

	return 0;
}


int wps_parse_msg(const struct wpabuf *msg, struct wps_parse_attr *attr)
{
	const u8 *pos, *end;
	u16 type, len;
	u16 prev_type = 0;

	os_memset(attr, 0, sizeof(*attr));
	pos = wpabuf_head(msg);
	end = pos + wpabuf_len(msg);

	while (pos < end) {
		if (end - pos < 4) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid message - "
				   "%lu bytes remaining",
				   (unsigned long) (end - pos));
			return -1;
		}

		type = WPA_GET_BE16(pos);
		pos += 2;
		len = WPA_GET_BE16(pos);
		pos += 2;
		wpa_printf(MSG_EXCESSIVE, "WPS: attr type=0x%x len=%u",
			   type, len);
		if (len > end - pos) {
			wpa_printf(MSG_DEBUG, "WPS: Attribute overflow");
			wpa_hexdump_buf(MSG_MSGDUMP, "WPS: Message data", msg);
#ifdef WPS_WORKAROUNDS
			/*
			 * Some deployed APs seem to have a bug in encoding of
			 * Network Key attribute in the Credential attribute
			 * where they add an extra octet after the Network Key
			 * attribute at least when open network is being
			 * provisioned.
			 */
			if ((type & 0xff00) != 0x1000 &&
			    prev_type == ATTR_NETWORK_KEY) {
				wpa_printf(MSG_DEBUG, "WPS: Workaround - try "
					   "to skip unexpected octet after "
					   "Network Key");
				pos -= 3;
				continue;
			}
#endif /* WPS_WORKAROUNDS */
			return -1;
		}

#ifdef WPS_WORKAROUNDS
		if (type == 0 && len == 0) {
			/*
			 * Mac OS X 10.6 seems to be adding 0x00 padding to the
			 * end of M1. Skip those to avoid interop issues.
			 */
			int i;
			for (i = 0; i < end - pos; i++) {
				if (pos[i])
					break;
			}
			if (i == end - pos) {
				wpa_printf(MSG_DEBUG, "WPS: Workaround - skip "
					   "unexpected message padding");
				break;
			}
		}
#endif /* WPS_WORKAROUNDS */

		if (wps_set_attr(attr, type, pos, len) < 0)
			return -1;

		prev_type = type;
		pos += len;
	}

	return 0;
}
