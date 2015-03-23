/*
 * P2P - IE builder
 * Copyright (c) 2009-2010, Atheros Communications
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
#include "common/ieee802_11_defs.h"
#include "wps/wps_i.h"
#include "p2p_i.h"


void p2p_buf_add_action_hdr(struct wpabuf *buf, u8 subtype, u8 dialog_token)
{
	wpabuf_put_u8(buf, WLAN_ACTION_VENDOR_SPECIFIC);
	wpabuf_put_be24(buf, OUI_WFA);
	wpabuf_put_u8(buf, P2P_OUI_TYPE);

	wpabuf_put_u8(buf, subtype); /* OUI Subtype */
	wpabuf_put_u8(buf, dialog_token);
	wpa_printf(MSG_DEBUG, "P2P: * Dialog Token: %d", dialog_token);
}


void p2p_buf_add_public_action_hdr(struct wpabuf *buf, u8 subtype,
				   u8 dialog_token)
{
	wpabuf_put_u8(buf, WLAN_ACTION_PUBLIC);
	wpabuf_put_u8(buf, WLAN_PA_VENDOR_SPECIFIC);
	wpabuf_put_be24(buf, OUI_WFA);
	wpabuf_put_u8(buf, P2P_OUI_TYPE);

	wpabuf_put_u8(buf, subtype); /* OUI Subtype */
	wpabuf_put_u8(buf, dialog_token);
	wpa_printf(MSG_DEBUG, "P2P: * Dialog Token: %d", dialog_token);
}


u8 * p2p_buf_add_ie_hdr(struct wpabuf *buf)
{
	u8 *len;

	/* P2P IE header */
	wpabuf_put_u8(buf, WLAN_EID_VENDOR_SPECIFIC);
	len = wpabuf_put(buf, 1); /* IE length to be filled */
	wpabuf_put_be24(buf, OUI_WFA);
	wpabuf_put_u8(buf, P2P_OUI_TYPE);
	wpa_printf(MSG_DEBUG, "P2P: * P2P IE header");
	return len;
}


void p2p_buf_update_ie_hdr(struct wpabuf *buf, u8 *len)
{
	/* Update P2P IE Length */
	*len = (u8 *) wpabuf_put(buf, 0) - len - 1;
}


void p2p_buf_add_capability(struct wpabuf *buf, u8 dev_capab, u8 group_capab)
{
	/* P2P Capability */
	wpabuf_put_u8(buf, P2P_ATTR_CAPABILITY);
	wpabuf_put_le16(buf, 2);
	wpabuf_put_u8(buf, dev_capab); /* Device Capabilities */
	wpabuf_put_u8(buf, group_capab); /* Group Capabilities */
	wpa_printf(MSG_DEBUG, "P2P: * Capability dev=%02x group=%02x",
		   dev_capab, group_capab);
}


void p2p_buf_add_go_intent(struct wpabuf *buf, u8 go_intent)
{
	/* Group Owner Intent */
	wpabuf_put_u8(buf, P2P_ATTR_GROUP_OWNER_INTENT);
	wpabuf_put_le16(buf, 1);
	wpabuf_put_u8(buf, go_intent);
	wpa_printf(MSG_DEBUG, "P2P: * GO Intent: Intent %u Tie breaker %u",
		   go_intent >> 1, go_intent & 0x01);
}


void p2p_buf_add_listen_channel(struct wpabuf *buf, const char *country,
				u8 reg_class, u8 channel)
{
	/* Listen Channel */
	wpabuf_put_u8(buf, P2P_ATTR_LISTEN_CHANNEL);
	wpabuf_put_le16(buf, 5);
	wpabuf_put_data(buf, country, 3);
	wpabuf_put_u8(buf, reg_class); /* Regulatory Class */
	wpabuf_put_u8(buf, channel); /* Channel Number */
	wpa_printf(MSG_DEBUG, "P2P: * Listen Channel: Regulatory Class %u "
		   "Channel %u", reg_class, channel);
}


void p2p_buf_add_operating_channel(struct wpabuf *buf, const char *country,
				   u8 reg_class, u8 channel)
{
	/* Operating Channel */
	wpabuf_put_u8(buf, P2P_ATTR_OPERATING_CHANNEL);
	wpabuf_put_le16(buf, 5);
	wpabuf_put_data(buf, country, 3);
	wpabuf_put_u8(buf, reg_class); /* Regulatory Class */
	wpabuf_put_u8(buf, channel); /* Channel Number */
	wpa_printf(MSG_DEBUG, "P2P: * Operating Channel: Regulatory Class %u "
		   "Channel %u", reg_class, channel);
}


void p2p_buf_add_channel_list(struct wpabuf *buf, const char *country,
			      struct p2p_channels *chan)
{
	u8 *len;
	size_t i;

	/* Channel List */
	wpabuf_put_u8(buf, P2P_ATTR_CHANNEL_LIST);
	len = wpabuf_put(buf, 2); /* IE length to be filled */
	wpabuf_put_data(buf, country, 3); /* Country String */

	for (i = 0; i < chan->reg_classes; i++) {
		struct p2p_reg_class *c = &chan->reg_class[i];
		wpabuf_put_u8(buf, c->reg_class);
		wpabuf_put_u8(buf, c->channels);
		wpabuf_put_data(buf, c->channel, c->channels);
	}

	/* Update attribute length */
	WPA_PUT_LE16(len, (u8 *) wpabuf_put(buf, 0) - len - 2);
	wpa_printf(MSG_DEBUG, "P2P: * Channel List");
}


void p2p_buf_add_status(struct wpabuf *buf, u8 status)
{
	/* Status */
	wpabuf_put_u8(buf, P2P_ATTR_STATUS);
	wpabuf_put_le16(buf, 1);
	wpabuf_put_u8(buf, status);
	wpa_printf(MSG_DEBUG, "P2P: * Status: %d", status);
}


void p2p_buf_add_device_info(struct wpabuf *buf, struct p2p_data *p2p,
			     struct p2p_device *peer)
{
	u8 *len;
	u16 methods;
	size_t nlen, i;

	/* P2P Device Info */
	wpabuf_put_u8(buf, P2P_ATTR_DEVICE_INFO);
	len = wpabuf_put(buf, 2); /* IE length to be filled */

	/* P2P Device address */
	wpabuf_put_data(buf, p2p->cfg->dev_addr, ETH_ALEN);

	/* Config Methods */
	methods = 0;
	if (peer && peer->wps_method != WPS_NOT_READY) {
		if (peer->wps_method == WPS_PBC)
			methods |= WPS_CONFIG_PUSHBUTTON;
		else if (peer->wps_method == WPS_PIN_LABEL)
			methods |= WPS_CONFIG_LABEL;
		else if (peer->wps_method == WPS_PIN_DISPLAY ||
			 peer->wps_method == WPS_PIN_KEYPAD)
			methods |= WPS_CONFIG_DISPLAY | WPS_CONFIG_KEYPAD;
	} else {
		methods |= WPS_CONFIG_PUSHBUTTON;
		methods |= WPS_CONFIG_DISPLAY | WPS_CONFIG_KEYPAD;
	}
	wpabuf_put_be16(buf, methods);

	/* Primary Device Type */
	wpabuf_put_data(buf, p2p->cfg->pri_dev_type,
			sizeof(p2p->cfg->pri_dev_type));

	/* Number of Secondary Device Types */
	wpabuf_put_u8(buf, p2p->cfg->num_sec_dev_types);

	/* Secondary Device Type List */
	for (i = 0; i < p2p->cfg->num_sec_dev_types; i++)
		wpabuf_put_data(buf, p2p->cfg->sec_dev_type[i],
				WPS_DEV_TYPE_LEN);

	/* Device Name */
	nlen = p2p->cfg->dev_name ? os_strlen(p2p->cfg->dev_name) : 0;
	wpabuf_put_be16(buf, ATTR_DEV_NAME);
	wpabuf_put_be16(buf, nlen);
	wpabuf_put_data(buf, p2p->cfg->dev_name, nlen);

	/* Update attribute length */
	WPA_PUT_LE16(len, (u8 *) wpabuf_put(buf, 0) - len - 2);
	wpa_printf(MSG_DEBUG, "P2P: * Device Info");
}


void p2p_buf_add_device_id(struct wpabuf *buf, const u8 *dev_addr)
{
	/* P2P Device ID */
	wpabuf_put_u8(buf, P2P_ATTR_DEVICE_ID);
	wpabuf_put_le16(buf, ETH_ALEN);
	wpabuf_put_data(buf, dev_addr, ETH_ALEN);
	wpa_printf(MSG_DEBUG, "P2P: * Device ID: " MACSTR, MAC2STR(dev_addr));
}


void p2p_buf_add_config_timeout(struct wpabuf *buf, u8 go_timeout,
				u8 client_timeout)
{
	/* Configuration Timeout */
	wpabuf_put_u8(buf, P2P_ATTR_CONFIGURATION_TIMEOUT);
	wpabuf_put_le16(buf, 2);
	wpabuf_put_u8(buf, go_timeout);
	wpabuf_put_u8(buf, client_timeout);
	wpa_printf(MSG_DEBUG, "P2P: * Configuration Timeout: GO %d (*10ms)  "
		   "client %d (*10ms)", go_timeout, client_timeout);
}


void p2p_buf_add_intended_addr(struct wpabuf *buf, const u8 *interface_addr)
{
	/* Intended P2P Interface Address */
	wpabuf_put_u8(buf, P2P_ATTR_INTENDED_INTERFACE_ADDR);
	wpabuf_put_le16(buf, ETH_ALEN);
	wpabuf_put_data(buf, interface_addr, ETH_ALEN);
	wpa_printf(MSG_DEBUG, "P2P: * Intended P2P Interface Address " MACSTR,
		   MAC2STR(interface_addr));
}


void p2p_buf_add_group_bssid(struct wpabuf *buf, const u8 *bssid)
{
	/* P2P Group BSSID */
	wpabuf_put_u8(buf, P2P_ATTR_GROUP_BSSID);
	wpabuf_put_le16(buf, ETH_ALEN);
	wpabuf_put_data(buf, bssid, ETH_ALEN);
	wpa_printf(MSG_DEBUG, "P2P: * P2P Group BSSID " MACSTR,
		   MAC2STR(bssid));
}


void p2p_buf_add_group_id(struct wpabuf *buf, const u8 *dev_addr,
			  const u8 *ssid, size_t ssid_len)
{
	/* P2P Group ID */
	wpabuf_put_u8(buf, P2P_ATTR_GROUP_ID);
	wpabuf_put_le16(buf, ETH_ALEN + ssid_len);
	wpabuf_put_data(buf, dev_addr, ETH_ALEN);
	wpabuf_put_data(buf, ssid, ssid_len);
	wpa_printf(MSG_DEBUG, "P2P: * P2P Group ID " MACSTR,
		   MAC2STR(dev_addr));
}


void p2p_buf_add_invitation_flags(struct wpabuf *buf, u8 flags)
{
	/* Invitation Flags */
	wpabuf_put_u8(buf, P2P_ATTR_INVITATION_FLAGS);
	wpabuf_put_le16(buf, 1);
	wpabuf_put_u8(buf, flags);
	wpa_printf(MSG_DEBUG, "P2P: * Invitation Flags: bitmap 0x%x", flags);
}


static void p2p_buf_add_noa_desc(struct wpabuf *buf, struct p2p_noa_desc *desc)
{
	if (desc == NULL)
		return;

	wpabuf_put_u8(buf, desc->count_type);
	wpabuf_put_le32(buf, desc->duration);
	wpabuf_put_le32(buf, desc->interval);
	wpabuf_put_le32(buf, desc->start_time);
}


void p2p_buf_add_noa(struct wpabuf *buf, u8 noa_index, u8 opp_ps, u8 ctwindow,
		     struct p2p_noa_desc *desc1, struct p2p_noa_desc *desc2)
{
	/* Notice of Absence */
	wpabuf_put_u8(buf, P2P_ATTR_NOTICE_OF_ABSENCE);
	wpabuf_put_le16(buf, 2 + (desc1 ? 13 : 0) + (desc2 ? 13 : 0));
	wpabuf_put_u8(buf, noa_index);
	wpabuf_put_u8(buf, (opp_ps ? 0x80 : 0) | (ctwindow & 0x7f));
	p2p_buf_add_noa_desc(buf, desc1);
	p2p_buf_add_noa_desc(buf, desc2);
	wpa_printf(MSG_DEBUG, "P2P: * Notice of Absence");
}


void p2p_buf_add_ext_listen_timing(struct wpabuf *buf, u16 period,
				   u16 interval)
{
	/* Extended Listen Timing */
	wpabuf_put_u8(buf, P2P_ATTR_EXT_LISTEN_TIMING);
	wpabuf_put_le16(buf, 4);
	wpabuf_put_le16(buf, period);
	wpabuf_put_le16(buf, interval);
	wpa_printf(MSG_DEBUG, "P2P: * Extended Listen Timing (period %u msec  "
		   "interval %u msec)", period, interval);
}


void p2p_buf_add_p2p_interface(struct wpabuf *buf, struct p2p_data *p2p)
{
	/* P2P Interface */
	wpabuf_put_u8(buf, P2P_ATTR_INTERFACE);
	wpabuf_put_le16(buf, ETH_ALEN + 1 + ETH_ALEN);
	/* P2P Device address */
	wpabuf_put_data(buf, p2p->cfg->dev_addr, ETH_ALEN);
	/*
	 * FIX: Fetch interface address list from driver. Do not include
	 * the P2P Device address if it is never used as interface address.
	 */
	/* P2P Interface Address Count */
	wpabuf_put_u8(buf, 1);
	wpabuf_put_data(buf, p2p->cfg->dev_addr, ETH_ALEN);
}


static void p2p_add_wps_string(struct wpabuf *buf, enum wps_attribute attr,
			       const char *val)
{
	size_t len;

	wpabuf_put_be16(buf, attr);
	len = val ? os_strlen(val) : 0;
#ifndef CONFIG_WPS_STRICT
	if (len == 0) {
		/*
		 * Some deployed WPS implementations fail to parse zeor-length
		 * attributes. As a workaround, send a space character if the
		 * device attribute string is empty.
		 */
		wpabuf_put_be16(buf, 1);
		wpabuf_put_u8(buf, ' ');
		return;
	}
#endif /* CONFIG_WPS_STRICT */
	wpabuf_put_be16(buf, len);
	if (val)
		wpabuf_put_data(buf, val, len);
}


void p2p_build_wps_ie(struct p2p_data *p2p, struct wpabuf *buf, u16 pw_id,
		      int all_attr)
{
	u8 *len;
	int i;

	wpabuf_put_u8(buf, WLAN_EID_VENDOR_SPECIFIC);
	len = wpabuf_put(buf, 1);
	wpabuf_put_be32(buf, WPS_DEV_OUI_WFA);

	wps_build_version(buf);

	if (all_attr) {
		wpabuf_put_be16(buf, ATTR_WPS_STATE);
		wpabuf_put_be16(buf, 1);
		wpabuf_put_u8(buf, WPS_STATE_NOT_CONFIGURED);
	}

	/* Device Password ID */
	wpabuf_put_be16(buf, ATTR_DEV_PASSWORD_ID);
	wpabuf_put_be16(buf, 2);
	wpa_printf(MSG_DEBUG, "P2P: WPS IE Device Password ID: %d", pw_id);
	wpabuf_put_be16(buf, pw_id);

	if (all_attr) {
		wpabuf_put_be16(buf, ATTR_RESPONSE_TYPE);
		wpabuf_put_be16(buf, 1);
		wpabuf_put_u8(buf, WPS_RESP_ENROLLEE_INFO);

		wps_build_uuid_e(buf, p2p->cfg->uuid);
		p2p_add_wps_string(buf, ATTR_MANUFACTURER,
				   p2p->cfg->manufacturer);
		p2p_add_wps_string(buf, ATTR_MODEL_NAME, p2p->cfg->model_name);
		p2p_add_wps_string(buf, ATTR_MODEL_NUMBER,
				   p2p->cfg->model_number);
		p2p_add_wps_string(buf, ATTR_SERIAL_NUMBER,
				   p2p->cfg->serial_number);

		wpabuf_put_be16(buf, ATTR_PRIMARY_DEV_TYPE);
		wpabuf_put_be16(buf, WPS_DEV_TYPE_LEN);
		wpabuf_put_data(buf, p2p->cfg->pri_dev_type, WPS_DEV_TYPE_LEN);

		p2p_add_wps_string(buf, ATTR_DEV_NAME, p2p->cfg->dev_name);

		wpabuf_put_be16(buf, ATTR_CONFIG_METHODS);
		wpabuf_put_be16(buf, 2);
		wpabuf_put_be16(buf, p2p->cfg->config_methods);
	}

	wps_build_wfa_ext(buf, 0, NULL, 0);

	if (all_attr && p2p->cfg->num_sec_dev_types) {
		wpabuf_put_be16(buf, ATTR_SECONDARY_DEV_TYPE_LIST);
		wpabuf_put_be16(buf, WPS_DEV_TYPE_LEN *
				p2p->cfg->num_sec_dev_types);
		wpabuf_put_data(buf, p2p->cfg->sec_dev_type,
				WPS_DEV_TYPE_LEN *
				p2p->cfg->num_sec_dev_types);
	}

	/* Add the WPS vendor extensions */
	for (i = 0; i < P2P_MAX_WPS_VENDOR_EXT; i++) {
		if (p2p->wps_vendor_ext[i] == NULL)
			break;
		if (wpabuf_tailroom(buf) <
		    4 + wpabuf_len(p2p->wps_vendor_ext[i]))
			continue;
		wpabuf_put_be16(buf, ATTR_VENDOR_EXT);
		wpabuf_put_be16(buf, wpabuf_len(p2p->wps_vendor_ext[i]));
		wpabuf_put_buf(buf, p2p->wps_vendor_ext[i]);
	}

	p2p_buf_update_ie_hdr(buf, len);
}
