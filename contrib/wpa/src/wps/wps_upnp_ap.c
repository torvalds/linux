/*
 * Wi-Fi Protected Setup - UPnP AP functionality
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "eloop.h"
#include "uuid.h"
#include "wps_i.h"
#include "wps_upnp.h"
#include "wps_upnp_i.h"


static void upnp_er_set_selected_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct subscription *s = eloop_ctx;
	struct wps_registrar *reg = timeout_ctx;
	wpa_printf(MSG_DEBUG, "WPS: SetSelectedRegistrar from ER timed out");
	s->selected_registrar = 0;
	wps_registrar_selected_registrar_changed(reg, 0);
}


int upnp_er_set_selected_registrar(struct wps_registrar *reg,
				   struct subscription *s,
				   const struct wpabuf *msg)
{
	struct wps_parse_attr attr;

	wpa_hexdump_buf(MSG_MSGDUMP, "WPS: SetSelectedRegistrar attributes",
			msg);
	if (wps_validate_upnp_set_selected_registrar(msg) < 0 ||
	    wps_parse_msg(msg, &attr) < 0)
		return -1;

	s->reg = reg;
	eloop_cancel_timeout(upnp_er_set_selected_timeout, s, reg);

	os_memset(s->authorized_macs, 0, sizeof(s->authorized_macs));
	if (attr.selected_registrar == NULL || *attr.selected_registrar == 0) {
		wpa_printf(MSG_DEBUG, "WPS: SetSelectedRegistrar: Disable "
			   "Selected Registrar");
		s->selected_registrar = 0;
	} else {
		s->selected_registrar = 1;
		s->dev_password_id = attr.dev_password_id ?
			WPA_GET_BE16(attr.dev_password_id) : DEV_PW_DEFAULT;
		s->config_methods = attr.sel_reg_config_methods ?
			WPA_GET_BE16(attr.sel_reg_config_methods) : -1;
		if (attr.authorized_macs) {
			int count = attr.authorized_macs_len / ETH_ALEN;
			if (count > WPS_MAX_AUTHORIZED_MACS)
				count = WPS_MAX_AUTHORIZED_MACS;
			os_memcpy(s->authorized_macs, attr.authorized_macs,
				  count * ETH_ALEN);
		} else if (!attr.version2) {
			wpa_printf(MSG_DEBUG, "WPS: Add broadcast "
				   "AuthorizedMACs for WPS 1.0 ER");
			os_memset(s->authorized_macs, 0xff, ETH_ALEN);
		}
		eloop_register_timeout(WPS_PBC_WALK_TIME, 0,
				       upnp_er_set_selected_timeout, s, reg);
	}

	wps_registrar_selected_registrar_changed(reg, 0);

	return 0;
}


void upnp_er_remove_notification(struct wps_registrar *reg,
				 struct subscription *s)
{
	s->selected_registrar = 0;
	eloop_cancel_timeout(upnp_er_set_selected_timeout, s, reg);
	if (reg)
		wps_registrar_selected_registrar_changed(reg, 0);
}
