/*
 * Wi-Fi Direct - P2P group operations
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
#include "common/ieee802_11_common.h"
#include "wps/wps_defs.h"
#include "wps/wps_i.h"
#include "p2p_i.h"
#include "p2p.h"


struct p2p_group_member {
	struct p2p_group_member *next;
	u8 addr[ETH_ALEN]; /* P2P Interface Address */
	u8 dev_addr[ETH_ALEN]; /* P2P Device Address */
	struct wpabuf *p2p_ie;
	struct wpabuf *client_info;
	u8 dev_capab;
};

/**
 * struct p2p_group - Internal P2P module per-group data
 */
struct p2p_group {
	struct p2p_data *p2p;
	struct p2p_group_config *cfg;
	struct p2p_group_member *members;
	unsigned int num_members;
	int group_formation;
	int beacon_update;
	struct wpabuf *noa;
};


static void p2p_group_update_ies(struct p2p_group *group);


struct p2p_group * p2p_group_init(struct p2p_data *p2p,
				  struct p2p_group_config *config)
{
	struct p2p_group *group, **groups;

	group = os_zalloc(sizeof(*group));
	if (group == NULL)
		return NULL;

	groups = os_realloc(p2p->groups, (p2p->num_groups + 1) *
			    sizeof(struct p2p_group *));
	if (groups == NULL) {
		os_free(group);
		return NULL;
	}
	groups[p2p->num_groups++] = group;
	p2p->groups = groups;

	group->p2p = p2p;
	group->cfg = config;
	group->group_formation = 1;
	group->beacon_update = 1;
	p2p_group_update_ies(group);
	group->cfg->idle_update(group->cfg->cb_ctx, 1);

	return group;
}


static void p2p_group_free_member(struct p2p_group_member *m)
{
	wpabuf_free(m->p2p_ie);
	wpabuf_free(m->client_info);
	os_free(m);
}


static void p2p_group_free_members(struct p2p_group *group)
{
	struct p2p_group_member *m, *prev;
	m = group->members;
	group->members = NULL;
	group->num_members = 0;
	while (m) {
		prev = m;
		m = m->next;
		p2p_group_free_member(prev);
	}
}


void p2p_group_deinit(struct p2p_group *group)
{
	size_t g;
	struct p2p_data *p2p;

	if (group == NULL)
		return;

	p2p = group->p2p;

	for (g = 0; g < p2p->num_groups; g++) {
		if (p2p->groups[g] == group) {
			while (g + 1 < p2p->num_groups) {
				p2p->groups[g] = p2p->groups[g + 1];
				g++;
			}
			p2p->num_groups--;
			break;
		}
	}

	p2p_group_free_members(group);
	os_free(group->cfg);
	wpabuf_free(group->noa);
	os_free(group);
}


static void p2p_client_info(struct wpabuf *ie, struct p2p_group_member *m)
{
	if (m->client_info == NULL)
		return;
	if (wpabuf_tailroom(ie) < wpabuf_len(m->client_info) + 1)
		return;
	wpabuf_put_buf(ie, m->client_info);
}


static void p2p_group_add_common_ies(struct p2p_group *group,
				     struct wpabuf *ie)
{
	u8 dev_capab = 0, group_capab = 0;

	/* P2P Capability */
	dev_capab |= P2P_DEV_CAPAB_SERVICE_DISCOVERY;
	dev_capab |= P2P_DEV_CAPAB_INVITATION_PROCEDURE;
	group_capab |= P2P_GROUP_CAPAB_GROUP_OWNER;
	if (group->cfg->persistent_group)
		group_capab |= P2P_GROUP_CAPAB_PERSISTENT_GROUP;
	if (group->p2p->cfg->p2p_intra_bss)
		group_capab |= P2P_GROUP_CAPAB_INTRA_BSS_DIST;
	if (group->group_formation)
		group_capab |= P2P_GROUP_CAPAB_GROUP_FORMATION;
	if (group->p2p->cross_connect)
		group_capab |= P2P_GROUP_CAPAB_CROSS_CONN;
	if (group->num_members >= group->cfg->max_clients)
		group_capab |= P2P_GROUP_CAPAB_GROUP_LIMIT;
	p2p_buf_add_capability(ie, dev_capab, group_capab);
}


static void p2p_group_add_noa(struct wpabuf *ie, struct wpabuf *noa)
{
	if (noa == NULL)
		return;
	/* Notice of Absence */
	wpabuf_put_u8(ie, P2P_ATTR_NOTICE_OF_ABSENCE);
	wpabuf_put_le16(ie, wpabuf_len(noa));
	wpabuf_put_buf(ie, noa);
}


static struct wpabuf * p2p_group_build_beacon_ie(struct p2p_group *group)
{
	struct wpabuf *ie;
	u8 *len;

	ie = wpabuf_alloc(257);
	if (ie == NULL)
		return NULL;

	len = p2p_buf_add_ie_hdr(ie);
	p2p_group_add_common_ies(group, ie);
	p2p_buf_add_device_id(ie, group->p2p->cfg->dev_addr);
	p2p_group_add_noa(ie, group->noa);
	p2p_buf_update_ie_hdr(ie, len);

	return ie;
}


static struct wpabuf * p2p_group_build_probe_resp_ie(struct p2p_group *group)
{
	u8 *group_info;
	struct wpabuf *ie;
	struct p2p_group_member *m;
	u8 *len;

	ie = wpabuf_alloc(257);
	if (ie == NULL)
		return NULL;

	len = p2p_buf_add_ie_hdr(ie);

	p2p_group_add_common_ies(group, ie);
	p2p_group_add_noa(ie, group->noa);

	/* P2P Device Info */
	p2p_buf_add_device_info(ie, group->p2p, NULL);

	/* P2P Group Info */
	group_info = wpabuf_put(ie, 0);
	wpabuf_put_u8(ie, P2P_ATTR_GROUP_INFO);
	wpabuf_put_le16(ie, 0); /* Length to be filled */
	for (m = group->members; m; m = m->next)
		p2p_client_info(ie, m);
	WPA_PUT_LE16(group_info + 1,
		     (u8 *) wpabuf_put(ie, 0) - group_info - 3);

	p2p_buf_update_ie_hdr(ie, len);
	return ie;
}


static void p2p_group_update_ies(struct p2p_group *group)
{
	struct wpabuf *beacon_ie;
	struct wpabuf *probe_resp_ie;

	probe_resp_ie = p2p_group_build_probe_resp_ie(group);
	if (probe_resp_ie == NULL)
		return;
	wpa_hexdump_buf(MSG_MSGDUMP, "P2P: Update GO Probe Response P2P IE",
			probe_resp_ie);

	if (group->beacon_update) {
		beacon_ie = p2p_group_build_beacon_ie(group);
		if (beacon_ie)
			group->beacon_update = 0;
		wpa_hexdump_buf(MSG_MSGDUMP, "P2P: Update GO Beacon P2P IE",
				beacon_ie);
	} else
		beacon_ie = NULL;

	group->cfg->ie_update(group->cfg->cb_ctx, beacon_ie, probe_resp_ie);
}


/**
 * p2p_build_client_info - Build P2P Client Info Descriptor
 * @addr: MAC address of the peer device
 * @p2p_ie: P2P IE from (Re)Association Request
 * @dev_capab: Buffer for returning Device Capability
 * @dev_addr: Buffer for returning P2P Device Address
 * Returns: P2P Client Info Descriptor or %NULL on failure
 *
 * This function builds P2P Client Info Descriptor based on the information
 * available from (Re)Association Request frame. Group owner can use this to
 * build the P2P Group Info attribute for Probe Response frames.
 */
static struct wpabuf * p2p_build_client_info(const u8 *addr,
					     struct wpabuf *p2p_ie,
					     u8 *dev_capab, u8 *dev_addr)
{
	const u8 *spos;
	struct p2p_message msg;
	u8 *len_pos;
	struct wpabuf *buf;

	if (p2p_ie == NULL)
		return NULL;

	os_memset(&msg, 0, sizeof(msg));
	if (p2p_parse_p2p_ie(p2p_ie, &msg) ||
	    msg.capability == NULL || msg.p2p_device_info == NULL)
		return NULL;

	buf = wpabuf_alloc(ETH_ALEN + 1 + 1 + msg.p2p_device_info_len);
	if (buf == NULL)
		return NULL;

	*dev_capab = msg.capability[0];
	os_memcpy(dev_addr, msg.p2p_device_addr, ETH_ALEN);

	spos = msg.p2p_device_info; /* P2P Device address */

	/* P2P Client Info Descriptor */
	/* Length to be set */
	len_pos = wpabuf_put(buf, 1);
	/* P2P Device address */
	wpabuf_put_data(buf, spos, ETH_ALEN);
	/* P2P Interface address */
	wpabuf_put_data(buf, addr, ETH_ALEN);
	/* Device Capability Bitmap */
	wpabuf_put_u8(buf, msg.capability[0]);
	/*
	 * Config Methods, Primary Device Type, Number of Secondary Device
	 * Types, Secondary Device Type List, Device Name copied from
	 * Device Info
	 */
	wpabuf_put_data(buf, spos + ETH_ALEN,
			msg.p2p_device_info_len - ETH_ALEN);

	*len_pos = wpabuf_len(buf) - 1;


	return buf;
}


int p2p_group_notif_assoc(struct p2p_group *group, const u8 *addr,
			  const u8 *ie, size_t len)
{
	struct p2p_group_member *m;

	if (group == NULL)
		return -1;

	m = os_zalloc(sizeof(*m));
	if (m == NULL)
		return -1;
	os_memcpy(m->addr, addr, ETH_ALEN);
	m->p2p_ie = ieee802_11_vendor_ie_concat(ie, len, P2P_IE_VENDOR_TYPE);
	if (m->p2p_ie) {
		m->client_info = p2p_build_client_info(addr, m->p2p_ie,
						       &m->dev_capab,
						       m->dev_addr);
	}

	m->next = group->members;
	group->members = m;
	group->num_members++;
	wpa_msg(group->p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Add client " MACSTR
		" to group (p2p=%d client_info=%d); num_members=%u/%u",
		MAC2STR(addr), m->p2p_ie ? 1 : 0, m->client_info ? 1 : 0,
		group->num_members, group->cfg->max_clients);
	if (group->num_members == group->cfg->max_clients)
		group->beacon_update = 1;
	p2p_group_update_ies(group);
	if (group->num_members == 1)
		group->cfg->idle_update(group->cfg->cb_ctx, 0);

	return 0;
}


struct wpabuf * p2p_group_assoc_resp_ie(struct p2p_group *group, u8 status)
{
	struct wpabuf *resp;
	u8 *rlen;

	/*
	 * (Re)Association Response - P2P IE
	 * Status attribute (shall be present when association request is
	 *	denied)
	 * Extended Listen Timing (may be present)
	 */
	resp = wpabuf_alloc(20);
	if (resp == NULL)
		return NULL;
	rlen = p2p_buf_add_ie_hdr(resp);
	if (status != P2P_SC_SUCCESS)
		p2p_buf_add_status(resp, status);
	p2p_buf_update_ie_hdr(resp, rlen);

	return resp;
}


void p2p_group_notif_disassoc(struct p2p_group *group, const u8 *addr)
{
	struct p2p_group_member *m, *prev;

	if (group == NULL)
		return;

	m = group->members;
	prev = NULL;
	while (m) {
		if (os_memcmp(m->addr, addr, ETH_ALEN) == 0)
			break;
		prev = m;
		m = m->next;
	}

	if (m) {
		if (prev)
			prev->next = m->next;
		else
			group->members = m->next;
		p2p_group_free_member(m);
		group->num_members--;
		wpa_msg(group->p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Remove "
			"client " MACSTR " from group; num_members=%u/%u",
			MAC2STR(addr), group->num_members,
			group->cfg->max_clients);
		if (group->num_members == group->cfg->max_clients - 1)
			group->beacon_update = 1;
		p2p_group_update_ies(group);
		if (group->num_members == 0)
			group->cfg->idle_update(group->cfg->cb_ctx, 1);
	}
}


/**
 * p2p_match_dev_type_member - Match client device type with requested type
 * @m: Group member
 * @wps: WPS TLVs from Probe Request frame (concatenated WPS IEs)
 * Returns: 1 on match, 0 on mismatch
 *
 * This function can be used to match the Requested Device Type attribute in
 * WPS IE with the device types of a group member for deciding whether a GO
 * should reply to a Probe Request frame.
 */
static int p2p_match_dev_type_member(struct p2p_group_member *m,
				     struct wpabuf *wps)
{
	const u8 *pos, *end;
	struct wps_parse_attr attr;
	u8 num_sec;

	if (m->client_info == NULL || wps == NULL)
		return 0;

	pos = wpabuf_head(m->client_info);
	end = pos + wpabuf_len(m->client_info);

	pos += 1 + 2 * ETH_ALEN + 1 + 2;
	if (end - pos < WPS_DEV_TYPE_LEN + 1)
		return 0;

	if (wps_parse_msg(wps, &attr))
		return 1; /* assume no Requested Device Type attributes */

	if (attr.num_req_dev_type == 0)
		return 1; /* no Requested Device Type attributes -> match */

	if (dev_type_list_match(pos, attr.req_dev_type, attr.num_req_dev_type))
		return 1; /* Match with client Primary Device Type */

	pos += WPS_DEV_TYPE_LEN;
	num_sec = *pos++;
	if (end - pos < num_sec * WPS_DEV_TYPE_LEN)
		return 0;
	while (num_sec > 0) {
		num_sec--;
		if (dev_type_list_match(pos, attr.req_dev_type,
					attr.num_req_dev_type))
			return 1; /* Match with client Secondary Device Type */
		pos += WPS_DEV_TYPE_LEN;
	}

	/* No matching device type found */
	return 0;
}


int p2p_group_match_dev_type(struct p2p_group *group, struct wpabuf *wps)
{
	struct p2p_group_member *m;

	if (p2p_match_dev_type(group->p2p, wps))
		return 1; /* Match with own device type */

	for (m = group->members; m; m = m->next) {
		if (p2p_match_dev_type_member(m, wps))
			return 1; /* Match with group client device type */
	}

	/* No match with Requested Device Type */
	return 0;
}


void p2p_group_notif_formation_done(struct p2p_group *group)
{
	if (group == NULL)
		return;
	group->group_formation = 0;
	group->beacon_update = 1;
	p2p_group_update_ies(group);
}


int p2p_group_notif_noa(struct p2p_group *group, const u8 *noa,
			size_t noa_len)
{
	if (noa == NULL) {
		wpabuf_free(group->noa);
		group->noa = NULL;
	} else {
		if (group->noa) {
			if (wpabuf_size(group->noa) >= noa_len) {
				group->noa->size = 0;
				wpabuf_put_data(group->noa, noa, noa_len);
			} else {
				wpabuf_free(group->noa);
				group->noa = NULL;
			}
		}

		if (!group->noa) {
			group->noa = wpabuf_alloc_copy(noa, noa_len);
			if (group->noa == NULL)
				return -1;
		}
	}

	group->beacon_update = 1;
	p2p_group_update_ies(group);
	return 0;
}


static struct p2p_group_member * p2p_group_get_client(struct p2p_group *group,
						      const u8 *dev_id)
{
	struct p2p_group_member *m;

	for (m = group->members; m; m = m->next) {
		if (os_memcmp(dev_id, m->dev_addr, ETH_ALEN) == 0)
			return m;
	}

	return NULL;
}


static struct p2p_group_member * p2p_group_get_client_iface(
	struct p2p_group *group, const u8 *interface_addr)
{
	struct p2p_group_member *m;

	for (m = group->members; m; m = m->next) {
		if (os_memcmp(interface_addr, m->addr, ETH_ALEN) == 0)
			return m;
	}

	return NULL;
}


static struct wpabuf * p2p_build_go_disc_req(void)
{
	struct wpabuf *buf;

	buf = wpabuf_alloc(100);
	if (buf == NULL)
		return NULL;

	p2p_buf_add_action_hdr(buf, P2P_GO_DISC_REQ, 0);

	return buf;
}


int p2p_group_go_discover(struct p2p_group *group, const u8 *dev_id,
			  const u8 *searching_dev, int rx_freq)
{
	struct p2p_group_member *m;
	struct wpabuf *req;
	struct p2p_data *p2p = group->p2p;
	int freq;

	m = p2p_group_get_client(group, dev_id);
	if (m == NULL || m->client_info == NULL) {
		wpa_printf(MSG_DEBUG, "P2P: Requested client was not in this "
			   "group " MACSTR,
			   MAC2STR(group->cfg->interface_addr));
		return -1;
	}

	if (!(m->dev_capab & P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY)) {
		wpa_printf(MSG_DEBUG, "P2P: Requested client does not support "
			   "client discoverability");
		return -1;
	}

	wpa_printf(MSG_DEBUG, "P2P: Schedule GO Discoverability Request to be "
		   "sent to " MACSTR, MAC2STR(dev_id));

	req = p2p_build_go_disc_req();
	if (req == NULL)
		return -1;

	/* TODO: Should really use group operating frequency here */
	freq = rx_freq;

	p2p->pending_action_state = P2P_PENDING_GO_DISC_REQ;
	if (p2p->cfg->send_action(p2p->cfg->cb_ctx, freq, m->addr,
				  group->cfg->interface_addr,
				  group->cfg->interface_addr,
				  wpabuf_head(req), wpabuf_len(req), 200) < 0)
	{
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Failed to send Action frame");
	}

	wpabuf_free(req);

	return 0;
}


const u8 * p2p_group_get_interface_addr(struct p2p_group *group)
{
	return group->cfg->interface_addr;
}


u8 p2p_group_presence_req(struct p2p_group *group,
			  const u8 *client_interface_addr,
			  const u8 *noa, size_t noa_len)
{
	struct p2p_group_member *m;
	u8 curr_noa[50];
	int curr_noa_len;

	m = p2p_group_get_client_iface(group, client_interface_addr);
	if (m == NULL || m->client_info == NULL) {
		wpa_printf(MSG_DEBUG, "P2P: Client was not in this group");
		return P2P_SC_FAIL_UNABLE_TO_ACCOMMODATE;
	}

	wpa_hexdump(MSG_DEBUG, "P2P: Presence Request NoA", noa, noa_len);

	if (group->p2p->cfg->get_noa)
		curr_noa_len = group->p2p->cfg->get_noa(
			group->p2p->cfg->cb_ctx, group->cfg->interface_addr,
			curr_noa, sizeof(curr_noa));
	else
		curr_noa_len = -1;
	if (curr_noa_len < 0)
		wpa_printf(MSG_DEBUG, "P2P: Failed to fetch current NoA");
	else if (curr_noa_len == 0)
		wpa_printf(MSG_DEBUG, "P2P: No NoA being advertized");
	else
		wpa_hexdump(MSG_DEBUG, "P2P: Current NoA", curr_noa,
			    curr_noa_len);

	/* TODO: properly process request and store copy */
	if (curr_noa_len > 0)
		return P2P_SC_FAIL_UNABLE_TO_ACCOMMODATE;

	return P2P_SC_SUCCESS;
}


unsigned int p2p_get_group_num_members(struct p2p_group *group)
{
	return group->num_members;
}


const u8 * p2p_iterate_group_members(struct p2p_group *group, void **next)
{
	struct p2p_group_member *iter = *next;

	if (!iter)
		iter = group->members;
	else
		iter = iter->next;

	*next = iter;

	if (!iter)
		return NULL;

	return iter->addr;
}
