/*
 * FST module implementation
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "fst/fst.h"
#include "fst/fst_internal.h"
#include "fst/fst_defs.h"
#include "fst/fst_ctrl_iface.h"

static int fst_global_initialized = 0;
struct dl_list fst_global_ctrls_list;


static void fst_ctrl_iface_notify_peer_state_change(struct fst_iface *iface,
						    Boolean connected,
						    const u8 *peer_addr)
{
	union fst_event_extra extra;

	extra.peer_state.connected = connected;
	os_strlcpy(extra.peer_state.ifname, fst_iface_get_name(iface),
		   sizeof(extra.peer_state.ifname));
	os_memcpy(extra.peer_state.addr, peer_addr, ETH_ALEN);

	foreach_fst_ctrl_call(on_event, EVENT_PEER_STATE_CHANGED,
			      iface, NULL, &extra);
}


struct fst_iface * fst_attach(const char *ifname, const u8 *own_addr,
			      const struct fst_wpa_obj *iface_obj,
			      const struct fst_iface_cfg *cfg)
{
	struct fst_group *g;
	struct fst_group *group = NULL;
	struct fst_iface *iface = NULL;
	Boolean new_group = FALSE;

	WPA_ASSERT(ifname != NULL);
	WPA_ASSERT(iface_obj != NULL);
	WPA_ASSERT(cfg != NULL);

	foreach_fst_group(g) {
		if (os_strcmp(cfg->group_id, fst_group_get_id(g)) == 0) {
			group = g;
			break;
		}
	}

	if (!group) {
		group = fst_group_create(cfg->group_id);
		if (!group) {
			fst_printf(MSG_ERROR, "%s: FST group cannot be created",
				   cfg->group_id);
			return NULL;
		}
		new_group = TRUE;
	}

	iface = fst_iface_create(group, ifname, own_addr, iface_obj, cfg);
	if (!iface) {
		fst_printf_group(group, MSG_ERROR, "cannot create iface for %s",
				 ifname);
		if (new_group)
			fst_group_delete(group);
		return NULL;
	}

	fst_group_attach_iface(group, iface);
	fst_group_update_ie(group);

	foreach_fst_ctrl_call(on_iface_added, iface);

	fst_printf_iface(iface, MSG_DEBUG,
			 "iface attached to group %s (prio=%d, llt=%d)",
			 cfg->group_id, cfg->priority, cfg->llt);

	return iface;
}


void fst_detach(struct fst_iface *iface)
{
	struct fst_group *group = fst_iface_get_group(iface);

	fst_printf_iface(iface, MSG_DEBUG, "iface detached from group %s",
			 fst_group_get_id(group));
	fst_session_global_on_iface_detached(iface);
	foreach_fst_ctrl_call(on_iface_removed, iface);
	fst_group_detach_iface(group, iface);
	fst_iface_delete(iface);
	fst_group_update_ie(group);
	fst_group_delete_if_empty(group);
}


int fst_global_init(void)
{
	dl_list_init(&fst_global_groups_list);
	dl_list_init(&fst_global_ctrls_list);
	fst_session_global_init();
	fst_global_initialized = 1;
	return 0;
}


void fst_global_deinit(void)
{
	struct fst_group *group;
	struct fst_ctrl_handle *h;

	if (!fst_global_initialized)
		return;

	fst_session_global_deinit();
	while ((group = fst_first_group()) != NULL)
		fst_group_delete(group);
	while ((h = dl_list_first(&fst_global_ctrls_list,
				  struct fst_ctrl_handle,
				  global_ctrls_lentry)))
		fst_global_del_ctrl(h);
	fst_global_initialized = 0;
}


struct fst_ctrl_handle * fst_global_add_ctrl(const struct fst_ctrl *ctrl)
{
	struct fst_ctrl_handle *h;

	if (!ctrl)
		return NULL;

	h = os_zalloc(sizeof(*h));
	if (!h)
		return NULL;

	if (ctrl->init && ctrl->init()) {
		os_free(h);
		return NULL;
	}

	h->ctrl = *ctrl;
	dl_list_add_tail(&fst_global_ctrls_list, &h->global_ctrls_lentry);

	return h;
}


void fst_global_del_ctrl(struct fst_ctrl_handle *h)
{
	dl_list_del(&h->global_ctrls_lentry);
	if (h->ctrl.deinit)
		h->ctrl.deinit();
	os_free(h);
}


void fst_rx_action(struct fst_iface *iface, const struct ieee80211_mgmt *mgmt,
		   size_t len)
{
	if (fst_iface_is_connected(iface, mgmt->sa, FALSE))
		fst_session_on_action_rx(iface, mgmt, len);
	else
		wpa_printf(MSG_DEBUG,
			   "FST: Ignore FST Action frame - no FST connection with "
			   MACSTR, MAC2STR(mgmt->sa));
}


void fst_notify_peer_connected(struct fst_iface *iface, const u8 *addr)
{
	if (is_zero_ether_addr(addr))
		return;

#ifndef HOSTAPD
	fst_group_update_ie(fst_iface_get_group(iface));
#endif /* HOSTAPD */

	fst_printf_iface(iface, MSG_DEBUG, MACSTR " became connected",
			 MAC2STR(addr));

	fst_ctrl_iface_notify_peer_state_change(iface, TRUE, addr);
}


void fst_notify_peer_disconnected(struct fst_iface *iface, const u8 *addr)
{
	if (is_zero_ether_addr(addr))
		return;

#ifndef HOSTAPD
	fst_group_update_ie(fst_iface_get_group(iface));
#endif /* HOSTAPD */

	fst_printf_iface(iface, MSG_DEBUG, MACSTR " became disconnected",
			 MAC2STR(addr));

	fst_ctrl_iface_notify_peer_state_change(iface, FALSE, addr);
}


Boolean fst_are_ifaces_aggregated(struct fst_iface *iface1,
				  struct fst_iface *iface2)
{
	return fst_iface_get_group(iface1) == fst_iface_get_group(iface2);
}


enum mb_band_id fst_hw_mode_to_band(enum hostapd_hw_mode mode)
{
	switch (mode) {
	case HOSTAPD_MODE_IEEE80211B:
	case HOSTAPD_MODE_IEEE80211G:
		return MB_BAND_ID_WIFI_2_4GHZ;
	case HOSTAPD_MODE_IEEE80211A:
		return MB_BAND_ID_WIFI_5GHZ;
	case HOSTAPD_MODE_IEEE80211AD:
		return MB_BAND_ID_WIFI_60GHZ;
	default:
		WPA_ASSERT(0);
		return MB_BAND_ID_WIFI_2_4GHZ;
	}
}
