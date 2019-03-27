/*
 * Wi-Fi Protected Setup
 * Copyright (c) 2007-2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/dh_group5.h"
#include "common/ieee802_11_defs.h"
#include "wps_i.h"
#include "wps_dev_attr.h"


#ifdef CONFIG_WPS_TESTING
int wps_version_number = 0x20;
int wps_testing_dummy_cred = 0;
int wps_corrupt_pkhash = 0;
int wps_force_auth_types_in_use = 0;
u16 wps_force_auth_types = 0;
int wps_force_encr_types_in_use = 0;
u16 wps_force_encr_types = 0;
#endif /* CONFIG_WPS_TESTING */


/**
 * wps_init - Initialize WPS Registration protocol data
 * @cfg: WPS configuration
 * Returns: Pointer to allocated data or %NULL on failure
 *
 * This function is used to initialize WPS data for a registration protocol
 * instance (i.e., each run of registration protocol as a Registrar of
 * Enrollee. The caller is responsible for freeing this data after the
 * registration run has been completed by calling wps_deinit().
 */
struct wps_data * wps_init(const struct wps_config *cfg)
{
	struct wps_data *data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->wps = cfg->wps;
	data->registrar = cfg->registrar;
	if (cfg->registrar) {
		os_memcpy(data->uuid_r, cfg->wps->uuid, WPS_UUID_LEN);
	} else {
		os_memcpy(data->mac_addr_e, cfg->wps->dev.mac_addr, ETH_ALEN);
		os_memcpy(data->uuid_e, cfg->wps->uuid, WPS_UUID_LEN);
	}
	if (cfg->pin) {
		data->dev_pw_id = cfg->dev_pw_id;
		data->dev_password = os_memdup(cfg->pin, cfg->pin_len);
		if (data->dev_password == NULL) {
			os_free(data);
			return NULL;
		}
		data->dev_password_len = cfg->pin_len;
		wpa_hexdump_key(MSG_DEBUG, "WPS: AP PIN dev_password",
				data->dev_password, data->dev_password_len);
	}

#ifdef CONFIG_WPS_NFC
	if (cfg->pin == NULL &&
	    cfg->dev_pw_id == DEV_PW_NFC_CONNECTION_HANDOVER)
		data->dev_pw_id = cfg->dev_pw_id;

	if (cfg->wps->ap && !cfg->registrar && cfg->wps->ap_nfc_dev_pw_id) {
		/* Keep AP PIN as alternative Device Password */
		data->alt_dev_pw_id = data->dev_pw_id;
		data->alt_dev_password = data->dev_password;
		data->alt_dev_password_len = data->dev_password_len;

		data->dev_pw_id = cfg->wps->ap_nfc_dev_pw_id;
		data->dev_password =
			os_memdup(wpabuf_head(cfg->wps->ap_nfc_dev_pw),
				  wpabuf_len(cfg->wps->ap_nfc_dev_pw));
		if (data->dev_password == NULL) {
			os_free(data);
			return NULL;
		}
		data->dev_password_len = wpabuf_len(cfg->wps->ap_nfc_dev_pw);
		wpa_hexdump_key(MSG_DEBUG, "WPS: NFC dev_password",
			    data->dev_password, data->dev_password_len);
	}
#endif /* CONFIG_WPS_NFC */

	data->pbc = cfg->pbc;
	if (cfg->pbc) {
		/* Use special PIN '00000000' for PBC */
		data->dev_pw_id = DEV_PW_PUSHBUTTON;
		bin_clear_free(data->dev_password, data->dev_password_len);
		data->dev_password = (u8 *) os_strdup("00000000");
		if (data->dev_password == NULL) {
			os_free(data);
			return NULL;
		}
		data->dev_password_len = 8;
	}

	data->state = data->registrar ? RECV_M1 : SEND_M1;

	if (cfg->assoc_wps_ie) {
		struct wps_parse_attr attr;
		wpa_hexdump_buf(MSG_DEBUG, "WPS: WPS IE from (Re)AssocReq",
				cfg->assoc_wps_ie);
		if (wps_parse_msg(cfg->assoc_wps_ie, &attr) < 0) {
			wpa_printf(MSG_DEBUG, "WPS: Failed to parse WPS IE "
				   "from (Re)AssocReq");
		} else if (attr.request_type == NULL) {
			wpa_printf(MSG_DEBUG, "WPS: No Request Type attribute "
				   "in (Re)AssocReq WPS IE");
		} else {
			wpa_printf(MSG_DEBUG, "WPS: Request Type (from WPS IE "
				   "in (Re)AssocReq WPS IE): %d",
				   *attr.request_type);
			data->request_type = *attr.request_type;
		}
	}

	if (cfg->new_ap_settings) {
		data->new_ap_settings =
			os_memdup(cfg->new_ap_settings,
				  sizeof(*data->new_ap_settings));
		if (data->new_ap_settings == NULL) {
			bin_clear_free(data->dev_password,
				       data->dev_password_len);
			os_free(data);
			return NULL;
		}
	}

	if (cfg->peer_addr)
		os_memcpy(data->peer_dev.mac_addr, cfg->peer_addr, ETH_ALEN);
	if (cfg->p2p_dev_addr)
		os_memcpy(data->p2p_dev_addr, cfg->p2p_dev_addr, ETH_ALEN);

	data->use_psk_key = cfg->use_psk_key;
	data->pbc_in_m1 = cfg->pbc_in_m1;

	if (cfg->peer_pubkey_hash) {
		os_memcpy(data->peer_pubkey_hash, cfg->peer_pubkey_hash,
			  WPS_OOB_PUBKEY_HASH_LEN);
		data->peer_pubkey_hash_set = 1;
	}

	return data;
}


/**
 * wps_deinit - Deinitialize WPS Registration protocol data
 * @data: WPS Registration protocol data from wps_init()
 */
void wps_deinit(struct wps_data *data)
{
#ifdef CONFIG_WPS_NFC
	if (data->registrar && data->nfc_pw_token)
		wps_registrar_remove_nfc_pw_token(data->wps->registrar,
						  data->nfc_pw_token);
#endif /* CONFIG_WPS_NFC */

	if (data->wps_pin_revealed) {
		wpa_printf(MSG_DEBUG, "WPS: Full PIN information revealed and "
			   "negotiation failed");
		if (data->registrar)
			wps_registrar_invalidate_pin(data->wps->registrar,
						     data->uuid_e);
	} else if (data->registrar)
		wps_registrar_unlock_pin(data->wps->registrar, data->uuid_e);

	wpabuf_clear_free(data->dh_privkey);
	wpabuf_free(data->dh_pubkey_e);
	wpabuf_free(data->dh_pubkey_r);
	wpabuf_free(data->last_msg);
	bin_clear_free(data->dev_password, data->dev_password_len);
	bin_clear_free(data->alt_dev_password, data->alt_dev_password_len);
	bin_clear_free(data->new_psk, data->new_psk_len);
	wps_device_data_free(&data->peer_dev);
	bin_clear_free(data->new_ap_settings, sizeof(*data->new_ap_settings));
	dh5_free(data->dh_ctx);
	os_free(data);
}


/**
 * wps_process_msg - Process a WPS message
 * @wps: WPS Registration protocol data from wps_init()
 * @op_code: Message OP Code
 * @msg: Message data
 * Returns: Processing result
 *
 * This function is used to process WPS messages with OP Codes WSC_ACK,
 * WSC_NACK, WSC_MSG, and WSC_Done. The caller (e.g., EAP server/peer) is
 * responsible for reassembling the messages before calling this function.
 * Response to this message is built by calling wps_get_msg().
 */
enum wps_process_res wps_process_msg(struct wps_data *wps,
				     enum wsc_op_code op_code,
				     const struct wpabuf *msg)
{
	if (wps->registrar)
		return wps_registrar_process_msg(wps, op_code, msg);
	else
		return wps_enrollee_process_msg(wps, op_code, msg);
}


/**
 * wps_get_msg - Build a WPS message
 * @wps: WPS Registration protocol data from wps_init()
 * @op_code: Buffer for returning message OP Code
 * Returns: The generated WPS message or %NULL on failure
 *
 * This function is used to build a response to a message processed by calling
 * wps_process_msg(). The caller is responsible for freeing the buffer.
 */
struct wpabuf * wps_get_msg(struct wps_data *wps, enum wsc_op_code *op_code)
{
	if (wps->registrar)
		return wps_registrar_get_msg(wps, op_code);
	else
		return wps_enrollee_get_msg(wps, op_code);
}


/**
 * wps_is_selected_pbc_registrar - Check whether WPS IE indicates active PBC
 * @msg: WPS IE contents from Beacon or Probe Response frame
 * Returns: 1 if PBC Registrar is active, 0 if not
 */
int wps_is_selected_pbc_registrar(const struct wpabuf *msg)
{
	struct wps_parse_attr attr;

	/*
	 * In theory, this could also verify that attr.sel_reg_config_methods
	 * includes WPS_CONFIG_PUSHBUTTON, but some deployed AP implementations
	 * do not set Selected Registrar Config Methods attribute properly, so
	 * it is safer to just use Device Password ID here.
	 */

	if (wps_parse_msg(msg, &attr) < 0 ||
	    !attr.selected_registrar || *attr.selected_registrar == 0 ||
	    !attr.dev_password_id ||
	    WPA_GET_BE16(attr.dev_password_id) != DEV_PW_PUSHBUTTON)
		return 0;

#ifdef CONFIG_WPS_STRICT
	if (!attr.sel_reg_config_methods ||
	    !(WPA_GET_BE16(attr.sel_reg_config_methods) &
	      WPS_CONFIG_PUSHBUTTON))
		return 0;
#endif /* CONFIG_WPS_STRICT */

	return 1;
}


static int is_selected_pin_registrar(struct wps_parse_attr *attr)
{
	/*
	 * In theory, this could also verify that attr.sel_reg_config_methods
	 * includes WPS_CONFIG_LABEL, WPS_CONFIG_DISPLAY, or WPS_CONFIG_KEYPAD,
	 * but some deployed AP implementations do not set Selected Registrar
	 * Config Methods attribute properly, so it is safer to just use
	 * Device Password ID here.
	 */

	if (!attr->selected_registrar || *attr->selected_registrar == 0)
		return 0;

	if (attr->dev_password_id != NULL &&
	    WPA_GET_BE16(attr->dev_password_id) == DEV_PW_PUSHBUTTON)
		return 0;

#ifdef CONFIG_WPS_STRICT
	if (!attr->sel_reg_config_methods ||
	    !(WPA_GET_BE16(attr->sel_reg_config_methods) &
	      (WPS_CONFIG_LABEL | WPS_CONFIG_DISPLAY | WPS_CONFIG_KEYPAD)))
		return 0;
#endif /* CONFIG_WPS_STRICT */

	return 1;
}


/**
 * wps_is_selected_pin_registrar - Check whether WPS IE indicates active PIN
 * @msg: WPS IE contents from Beacon or Probe Response frame
 * Returns: 1 if PIN Registrar is active, 0 if not
 */
int wps_is_selected_pin_registrar(const struct wpabuf *msg)
{
	struct wps_parse_attr attr;

	if (wps_parse_msg(msg, &attr) < 0)
		return 0;

	return is_selected_pin_registrar(&attr);
}


/**
 * wps_is_addr_authorized - Check whether WPS IE authorizes MAC address
 * @msg: WPS IE contents from Beacon or Probe Response frame
 * @addr: MAC address to search for
 * @ver1_compat: Whether to use version 1 compatibility mode
 * Returns: 2 if the specified address is explicit authorized, 1 if address is
 * authorized (broadcast), 0 if not
 */
int wps_is_addr_authorized(const struct wpabuf *msg, const u8 *addr,
			   int ver1_compat)
{
	struct wps_parse_attr attr;
	unsigned int i;
	const u8 *pos;
	const u8 bcast[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	if (wps_parse_msg(msg, &attr) < 0)
		return 0;

	if (!attr.version2 && ver1_compat) {
		/*
		 * Version 1.0 AP - AuthorizedMACs not used, so revert back to
		 * old mechanism of using SelectedRegistrar.
		 */
		return is_selected_pin_registrar(&attr);
	}

	if (!attr.authorized_macs)
		return 0;

	pos = attr.authorized_macs;
	for (i = 0; i < attr.authorized_macs_len / ETH_ALEN; i++) {
		if (os_memcmp(pos, addr, ETH_ALEN) == 0)
			return 2;
		if (os_memcmp(pos, bcast, ETH_ALEN) == 0)
			return 1;
		pos += ETH_ALEN;
	}

	return 0;
}


/**
 * wps_ap_priority_compar - Prioritize WPS IE from two APs
 * @wps_a: WPS IE contents from Beacon or Probe Response frame
 * @wps_b: WPS IE contents from Beacon or Probe Response frame
 * Returns: 1 if wps_b is considered more likely selection for WPS
 * provisioning, -1 if wps_a is considered more like, or 0 if no preference
 */
int wps_ap_priority_compar(const struct wpabuf *wps_a,
			   const struct wpabuf *wps_b)
{
	struct wps_parse_attr attr;
	int sel_a, sel_b;

	if (wps_a == NULL || wps_parse_msg(wps_a, &attr) < 0)
		return 1;
	sel_a = attr.selected_registrar && *attr.selected_registrar != 0;

	if (wps_b == NULL || wps_parse_msg(wps_b, &attr) < 0)
		return -1;
	sel_b = attr.selected_registrar && *attr.selected_registrar != 0;

	if (sel_a && !sel_b)
		return -1;
	if (!sel_a && sel_b)
		return 1;

	return 0;
}


/**
 * wps_get_uuid_e - Get UUID-E from WPS IE
 * @msg: WPS IE contents from Beacon or Probe Response frame
 * Returns: Pointer to UUID-E or %NULL if not included
 *
 * The returned pointer is to the msg contents and it remains valid only as
 * long as the msg buffer is valid.
 */
const u8 * wps_get_uuid_e(const struct wpabuf *msg)
{
	struct wps_parse_attr attr;

	if (wps_parse_msg(msg, &attr) < 0)
		return NULL;
	return attr.uuid_e;
}


/**
 * wps_is_20 - Check whether WPS attributes claim support for WPS 2.0
 */
int wps_is_20(const struct wpabuf *msg)
{
	struct wps_parse_attr attr;

	if (msg == NULL || wps_parse_msg(msg, &attr) < 0)
		return 0;
	return attr.version2 != NULL;
}


/**
 * wps_build_assoc_req_ie - Build WPS IE for (Re)Association Request
 * @req_type: Value for Request Type attribute
 * Returns: WPS IE or %NULL on failure
 *
 * The caller is responsible for freeing the buffer.
 */
struct wpabuf * wps_build_assoc_req_ie(enum wps_request_type req_type)
{
	struct wpabuf *ie;
	u8 *len;

	wpa_printf(MSG_DEBUG, "WPS: Building WPS IE for (Re)Association "
		   "Request");
	ie = wpabuf_alloc(100);
	if (ie == NULL)
		return NULL;

	wpabuf_put_u8(ie, WLAN_EID_VENDOR_SPECIFIC);
	len = wpabuf_put(ie, 1);
	wpabuf_put_be32(ie, WPS_DEV_OUI_WFA);

	if (wps_build_version(ie) ||
	    wps_build_req_type(ie, req_type) ||
	    wps_build_wfa_ext(ie, 0, NULL, 0)) {
		wpabuf_free(ie);
		return NULL;
	}

	*len = wpabuf_len(ie) - 2;

	return ie;
}


/**
 * wps_build_assoc_resp_ie - Build WPS IE for (Re)Association Response
 * Returns: WPS IE or %NULL on failure
 *
 * The caller is responsible for freeing the buffer.
 */
struct wpabuf * wps_build_assoc_resp_ie(void)
{
	struct wpabuf *ie;
	u8 *len;

	wpa_printf(MSG_DEBUG, "WPS: Building WPS IE for (Re)Association "
		   "Response");
	ie = wpabuf_alloc(100);
	if (ie == NULL)
		return NULL;

	wpabuf_put_u8(ie, WLAN_EID_VENDOR_SPECIFIC);
	len = wpabuf_put(ie, 1);
	wpabuf_put_be32(ie, WPS_DEV_OUI_WFA);

	if (wps_build_version(ie) ||
	    wps_build_resp_type(ie, WPS_RESP_AP) ||
	    wps_build_wfa_ext(ie, 0, NULL, 0)) {
		wpabuf_free(ie);
		return NULL;
	}

	*len = wpabuf_len(ie) - 2;

	return ie;
}


/**
 * wps_build_probe_req_ie - Build WPS IE for Probe Request
 * @pw_id: Password ID (DEV_PW_PUSHBUTTON for active PBC and DEV_PW_DEFAULT for
 * most other use cases)
 * @dev: Device attributes
 * @uuid: Own UUID
 * @req_type: Value for Request Type attribute
 * @num_req_dev_types: Number of requested device types
 * @req_dev_types: Requested device types (8 * num_req_dev_types octets) or
 *	%NULL if none
 * Returns: WPS IE or %NULL on failure
 *
 * The caller is responsible for freeing the buffer.
 */
struct wpabuf * wps_build_probe_req_ie(u16 pw_id, struct wps_device_data *dev,
				       const u8 *uuid,
				       enum wps_request_type req_type,
				       unsigned int num_req_dev_types,
				       const u8 *req_dev_types)
{
	struct wpabuf *ie;

	wpa_printf(MSG_DEBUG, "WPS: Building WPS IE for Probe Request");

	ie = wpabuf_alloc(500);
	if (ie == NULL)
		return NULL;

	if (wps_build_version(ie) ||
	    wps_build_req_type(ie, req_type) ||
	    wps_build_config_methods(ie, dev->config_methods) ||
	    wps_build_uuid_e(ie, uuid) ||
	    wps_build_primary_dev_type(dev, ie) ||
	    wps_build_rf_bands(dev, ie, 0) ||
	    wps_build_assoc_state(NULL, ie) ||
	    wps_build_config_error(ie, WPS_CFG_NO_ERROR) ||
	    wps_build_dev_password_id(ie, pw_id) ||
	    wps_build_manufacturer(dev, ie) ||
	    wps_build_model_name(dev, ie) ||
	    wps_build_model_number(dev, ie) ||
	    wps_build_dev_name(dev, ie) ||
	    wps_build_wfa_ext(ie, req_type == WPS_REQ_ENROLLEE, NULL, 0) ||
	    wps_build_req_dev_type(dev, ie, num_req_dev_types, req_dev_types)
	    ||
	    wps_build_secondary_dev_type(dev, ie)
		) {
		wpabuf_free(ie);
		return NULL;
	}

	return wps_ie_encapsulate(ie);
}


void wps_free_pending_msgs(struct upnp_pending_message *msgs)
{
	struct upnp_pending_message *p, *prev;
	p = msgs;
	while (p) {
		prev = p;
		p = p->next;
		wpabuf_free(prev->msg);
		os_free(prev);
	}
}


int wps_attr_text(struct wpabuf *data, char *buf, char *end)
{
	struct wps_parse_attr attr;
	char *pos = buf;
	int ret;

	if (wps_parse_msg(data, &attr) < 0)
		return -1;

	if (attr.wps_state) {
		if (*attr.wps_state == WPS_STATE_NOT_CONFIGURED)
			ret = os_snprintf(pos, end - pos,
					  "wps_state=unconfigured\n");
		else if (*attr.wps_state == WPS_STATE_CONFIGURED)
			ret = os_snprintf(pos, end - pos,
					  "wps_state=configured\n");
		else
			ret = 0;
		if (os_snprintf_error(end - pos, ret))
			return pos - buf;
		pos += ret;
	}

	if (attr.ap_setup_locked && *attr.ap_setup_locked) {
		ret = os_snprintf(pos, end - pos,
				  "wps_ap_setup_locked=1\n");
		if (os_snprintf_error(end - pos, ret))
			return pos - buf;
		pos += ret;
	}

	if (attr.selected_registrar && *attr.selected_registrar) {
		ret = os_snprintf(pos, end - pos,
				  "wps_selected_registrar=1\n");
		if (os_snprintf_error(end - pos, ret))
			return pos - buf;
		pos += ret;
	}

	if (attr.dev_password_id) {
		ret = os_snprintf(pos, end - pos,
				  "wps_device_password_id=%u\n",
				  WPA_GET_BE16(attr.dev_password_id));
		if (os_snprintf_error(end - pos, ret))
			return pos - buf;
		pos += ret;
	}

	if (attr.sel_reg_config_methods) {
		ret = os_snprintf(pos, end - pos,
				  "wps_selected_registrar_config_methods="
				  "0x%04x\n",
				  WPA_GET_BE16(attr.sel_reg_config_methods));
		if (os_snprintf_error(end - pos, ret))
			return pos - buf;
		pos += ret;
	}

	if (attr.primary_dev_type) {
		char devtype[WPS_DEV_TYPE_BUFSIZE];
		ret = os_snprintf(pos, end - pos,
				  "wps_primary_device_type=%s\n",
				  wps_dev_type_bin2str(attr.primary_dev_type,
						       devtype,
						       sizeof(devtype)));
		if (os_snprintf_error(end - pos, ret))
			return pos - buf;
		pos += ret;
	}

	if (attr.dev_name) {
		char *str = os_malloc(attr.dev_name_len + 1);
		size_t i;
		if (str == NULL)
			return pos - buf;
		for (i = 0; i < attr.dev_name_len; i++) {
			if (attr.dev_name[i] == 0 ||
			    is_ctrl_char(attr.dev_name[i]))
				str[i] = '_';
			else
				str[i] = attr.dev_name[i];
		}
		str[i] = '\0';
		ret = os_snprintf(pos, end - pos, "wps_device_name=%s\n", str);
		os_free(str);
		if (os_snprintf_error(end - pos, ret))
			return pos - buf;
		pos += ret;
	}

	if (attr.config_methods) {
		ret = os_snprintf(pos, end - pos,
				  "wps_config_methods=0x%04x\n",
				  WPA_GET_BE16(attr.config_methods));
		if (os_snprintf_error(end - pos, ret))
			return pos - buf;
		pos += ret;
	}

	return pos - buf;
}


const char * wps_ei_str(enum wps_error_indication ei)
{
	switch (ei) {
	case WPS_EI_NO_ERROR:
		return "No Error";
	case WPS_EI_SECURITY_TKIP_ONLY_PROHIBITED:
		return "TKIP Only Prohibited";
	case WPS_EI_SECURITY_WEP_PROHIBITED:
		return "WEP Prohibited";
	case WPS_EI_AUTH_FAILURE:
		return "Authentication Failure";
	default:
		return "Unknown";
	}
}
