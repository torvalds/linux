/*
 * Wi-Fi Direct - P2P module
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
#include "eloop.h"
#include "common/ieee802_11_defs.h"
#include "common/ieee802_11_common.h"
#include "wps/wps_i.h"
#include "p2p_i.h"
#include "p2p.h"


static void p2p_state_timeout(void *eloop_ctx, void *timeout_ctx);
static void p2p_device_free(struct p2p_data *p2p, struct p2p_device *dev);
static void p2p_process_presence_req(struct p2p_data *p2p, const u8 *da,
				     const u8 *sa, const u8 *data, size_t len,
				     int rx_freq);
static void p2p_process_presence_resp(struct p2p_data *p2p, const u8 *da,
				      const u8 *sa, const u8 *data,
				      size_t len);
static void p2p_ext_listen_timeout(void *eloop_ctx, void *timeout_ctx);
static void p2p_scan_timeout(void *eloop_ctx, void *timeout_ctx);


/*
 * p2p_scan recovery timeout
 *
 * Many drivers are using 30 second timeout on scan results. Allow a bit larger
 * timeout for this to avoid hitting P2P timeout unnecessarily.
 */
#define P2P_SCAN_TIMEOUT 35

/**
 * P2P_PEER_EXPIRATION_AGE - Number of seconds after which inactive peer
 * entries will be removed
 */
#define P2P_PEER_EXPIRATION_AGE 300

#define P2P_PEER_EXPIRATION_INTERVAL (P2P_PEER_EXPIRATION_AGE / 2)

static void p2p_expire_peers(struct p2p_data *p2p)
{
	struct p2p_device *dev, *n;
	struct os_time now;

	os_get_time(&now);
	dl_list_for_each_safe(dev, n, &p2p->devices, struct p2p_device, list) {
		if (dev->last_seen.sec + P2P_PEER_EXPIRATION_AGE >= now.sec)
			continue;
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Expiring old peer "
			"entry " MACSTR, MAC2STR(dev->info.p2p_device_addr));
		dl_list_del(&dev->list);
		p2p_device_free(p2p, dev);
	}
}


static void p2p_expiration_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct p2p_data *p2p = eloop_ctx;
	p2p_expire_peers(p2p);
	eloop_register_timeout(P2P_PEER_EXPIRATION_INTERVAL, 0,
			       p2p_expiration_timeout, p2p, NULL);
}


static const char * p2p_state_txt(int state)
{
	switch (state) {
	case P2P_IDLE:
		return "IDLE";
	case P2P_SEARCH:
		return "SEARCH";
	case P2P_CONNECT:
		return "CONNECT";
	case P2P_CONNECT_LISTEN:
		return "CONNECT_LISTEN";
	case P2P_GO_NEG:
		return "GO_NEG";
	case P2P_LISTEN_ONLY:
		return "LISTEN_ONLY";
	case P2P_WAIT_PEER_CONNECT:
		return "WAIT_PEER_CONNECT";
	case P2P_WAIT_PEER_IDLE:
		return "WAIT_PEER_IDLE";
	case P2P_SD_DURING_FIND:
		return "SD_DURING_FIND";
	case P2P_PROVISIONING:
		return "PROVISIONING";
	case P2P_PD_DURING_FIND:
		return "PD_DURING_FIND";
	case P2P_INVITE:
		return "INVITE";
	case P2P_INVITE_LISTEN:
		return "INVITE_LISTEN";
	default:
		return "?";
	}
}


void p2p_set_state(struct p2p_data *p2p, int new_state)
{
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: State %s -> %s",
		p2p_state_txt(p2p->state), p2p_state_txt(new_state));
	p2p->state = new_state;
}


void p2p_set_timeout(struct p2p_data *p2p, unsigned int sec, unsigned int usec)
{
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Set timeout (state=%s): %u.%06u sec",
		p2p_state_txt(p2p->state), sec, usec);
	eloop_cancel_timeout(p2p_state_timeout, p2p, NULL);
	eloop_register_timeout(sec, usec, p2p_state_timeout, p2p, NULL);
}


void p2p_clear_timeout(struct p2p_data *p2p)
{
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Clear timeout (state=%s)",
		p2p_state_txt(p2p->state));
	eloop_cancel_timeout(p2p_state_timeout, p2p, NULL);
}


void p2p_go_neg_failed(struct p2p_data *p2p, struct p2p_device *peer,
		       int status)
{
	struct p2p_go_neg_results res;
	p2p_clear_timeout(p2p);
	p2p_set_state(p2p, P2P_IDLE);
	p2p->go_neg_peer = NULL;

	os_memset(&res, 0, sizeof(res));
	res.status = status;
	if (peer) {
		os_memcpy(res.peer_device_addr, peer->info.p2p_device_addr,
			  ETH_ALEN);
		os_memcpy(res.peer_interface_addr, peer->intended_addr,
			  ETH_ALEN);
	}
	p2p->cfg->go_neg_completed(p2p->cfg->cb_ctx, &res);
}


static void p2p_listen_in_find(struct p2p_data *p2p)
{
	unsigned int r, tu;
	int freq;
	struct wpabuf *ies;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Starting short listen state (state=%s)",
		p2p_state_txt(p2p->state));

	freq = p2p_channel_to_freq(p2p->cfg->country, p2p->cfg->reg_class,
				   p2p->cfg->channel);
	if (freq < 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unknown regulatory class/channel");
		return;
	}

	os_get_random((u8 *) &r, sizeof(r));
	tu = (r % ((p2p->max_disc_int - p2p->min_disc_int) + 1) +
	      p2p->min_disc_int) * 100;

	p2p->pending_listen_freq = freq;
	p2p->pending_listen_sec = 0;
	p2p->pending_listen_usec = 1024 * tu;

	ies = p2p_build_probe_resp_ies(p2p);
	if (ies == NULL)
		return;

	if (p2p->cfg->start_listen(p2p->cfg->cb_ctx, freq, 1024 * tu / 1000,
		    ies) < 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Failed to start listen mode");
		p2p->pending_listen_freq = 0;
	}
	wpabuf_free(ies);
}


int p2p_listen(struct p2p_data *p2p, unsigned int timeout)
{
	int freq;
	struct wpabuf *ies;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Going to listen(only) state");

	freq = p2p_channel_to_freq(p2p->cfg->country, p2p->cfg->reg_class,
				   p2p->cfg->channel);
	if (freq < 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unknown regulatory class/channel");
		return -1;
	}

	p2p->pending_listen_freq = freq;
	p2p->pending_listen_sec = timeout / 1000;
	p2p->pending_listen_usec = (timeout % 1000) * 1000;

	if (p2p->p2p_scan_running) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: p2p_scan running - delay start of listen state");
		p2p->start_after_scan = P2P_AFTER_SCAN_LISTEN;
		return 0;
	}

	ies = p2p_build_probe_resp_ies(p2p);
	if (ies == NULL)
		return -1;

	if (p2p->cfg->start_listen(p2p->cfg->cb_ctx, freq, timeout, ies) < 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Failed to start listen mode");
		p2p->pending_listen_freq = 0;
		wpabuf_free(ies);
		return -1;
	}
	wpabuf_free(ies);

	p2p_set_state(p2p, P2P_LISTEN_ONLY);

	return 0;
}


static void p2p_device_clear_reported(struct p2p_data *p2p)
{
	struct p2p_device *dev;
	dl_list_for_each(dev, &p2p->devices, struct p2p_device, list)
		dev->flags &= ~P2P_DEV_REPORTED;
}


/**
 * p2p_get_device - Fetch a peer entry
 * @p2p: P2P module context from p2p_init()
 * @addr: P2P Device Address of the peer
 * Returns: Pointer to the device entry or %NULL if not found
 */
struct p2p_device * p2p_get_device(struct p2p_data *p2p, const u8 *addr)
{
	struct p2p_device *dev;
	dl_list_for_each(dev, &p2p->devices, struct p2p_device, list) {
		if (os_memcmp(dev->info.p2p_device_addr, addr, ETH_ALEN) == 0)
			return dev;
	}
	return NULL;
}


/**
 * p2p_get_device_interface - Fetch a peer entry based on P2P Interface Address
 * @p2p: P2P module context from p2p_init()
 * @addr: P2P Interface Address of the peer
 * Returns: Pointer to the device entry or %NULL if not found
 */
struct p2p_device * p2p_get_device_interface(struct p2p_data *p2p,
					     const u8 *addr)
{
	struct p2p_device *dev;
	dl_list_for_each(dev, &p2p->devices, struct p2p_device, list) {
		if (os_memcmp(dev->interface_addr, addr, ETH_ALEN) == 0)
			return dev;
	}
	return NULL;
}


/**
 * p2p_create_device - Create a peer entry
 * @p2p: P2P module context from p2p_init()
 * @addr: P2P Device Address of the peer
 * Returns: Pointer to the device entry or %NULL on failure
 *
 * If there is already an entry for the peer, it will be returned instead of
 * creating a new one.
 */
static struct p2p_device * p2p_create_device(struct p2p_data *p2p,
					     const u8 *addr)
{
	struct p2p_device *dev, *oldest = NULL;
	size_t count = 0;

	dev = p2p_get_device(p2p, addr);
	if (dev)
		return dev;

	dl_list_for_each(dev, &p2p->devices, struct p2p_device, list) {
		count++;
		if (oldest == NULL ||
		    os_time_before(&dev->last_seen, &oldest->last_seen))
			oldest = dev;
	}
	if (count + 1 > p2p->cfg->max_peers && oldest) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Remove oldest peer entry to make room for a new "
			"peer");
		dl_list_del(&oldest->list);
		p2p_device_free(p2p, oldest);
	}

	dev = os_zalloc(sizeof(*dev));
	if (dev == NULL)
		return NULL;
	dl_list_add(&p2p->devices, &dev->list);
	os_memcpy(dev->info.p2p_device_addr, addr, ETH_ALEN);

	return dev;
}


static void p2p_copy_client_info(struct p2p_device *dev,
				 struct p2p_client_info *cli)
{
	os_memcpy(dev->info.device_name, cli->dev_name, cli->dev_name_len);
	dev->info.device_name[cli->dev_name_len] = '\0';
	dev->info.dev_capab = cli->dev_capab;
	dev->info.config_methods = cli->config_methods;
	os_memcpy(dev->info.pri_dev_type, cli->pri_dev_type, 8);
	dev->info.wps_sec_dev_type_list_len = 8 * cli->num_sec_dev_types;
	os_memcpy(dev->info.wps_sec_dev_type_list, cli->sec_dev_types,
		  dev->info.wps_sec_dev_type_list_len);
}


static int p2p_add_group_clients(struct p2p_data *p2p, const u8 *go_dev_addr,
				 const u8 *go_interface_addr, int freq,
				 const u8 *gi, size_t gi_len)
{
	struct p2p_group_info info;
	size_t c;
	struct p2p_device *dev;

	if (gi == NULL)
		return 0;

	if (p2p_group_info_parse(gi, gi_len, &info) < 0)
		return -1;

	/*
	 * Clear old data for this group; if the devices are still in the
	 * group, the information will be restored in the loop following this.
	 */
	dl_list_for_each(dev, &p2p->devices, struct p2p_device, list) {
		if (os_memcpy(dev->member_in_go_iface, go_interface_addr,
			      ETH_ALEN) == 0) {
			os_memset(dev->member_in_go_iface, 0, ETH_ALEN);
			os_memset(dev->member_in_go_dev, 0, ETH_ALEN);
		}
	}

	for (c = 0; c < info.num_clients; c++) {
		struct p2p_client_info *cli = &info.client[c];
		dev = p2p_get_device(p2p, cli->p2p_device_addr);
		if (dev) {
			/*
			 * Update information only if we have not received this
			 * directly from the client.
			 */
			if (dev->flags & (P2P_DEV_GROUP_CLIENT_ONLY |
					  P2P_DEV_PROBE_REQ_ONLY))
				p2p_copy_client_info(dev, cli);
			if (dev->flags & P2P_DEV_PROBE_REQ_ONLY) {
				dev->flags &= ~P2P_DEV_PROBE_REQ_ONLY;
			}
		} else {
			dev = p2p_create_device(p2p, cli->p2p_device_addr);
			if (dev == NULL)
				continue;
			dev->flags |= P2P_DEV_GROUP_CLIENT_ONLY;
			p2p_copy_client_info(dev, cli);
			dev->oper_freq = freq;
			p2p->cfg->dev_found(p2p->cfg->cb_ctx,
					    dev->info.p2p_device_addr,
					    &dev->info, 1);
			dev->flags |= P2P_DEV_REPORTED | P2P_DEV_REPORTED_ONCE;
		}

		os_memcpy(dev->interface_addr, cli->p2p_interface_addr,
			  ETH_ALEN);
		os_get_time(&dev->last_seen);
		os_memcpy(dev->member_in_go_dev, go_dev_addr, ETH_ALEN);
		os_memcpy(dev->member_in_go_iface, go_interface_addr,
			  ETH_ALEN);
	}

	return 0;
}


static void p2p_copy_wps_info(struct p2p_device *dev, int probe_req,
			      const struct p2p_message *msg)
{
	os_memcpy(dev->info.device_name, msg->device_name,
		  sizeof(dev->info.device_name));

	if (msg->manufacturer &&
	    msg->manufacturer_len < sizeof(dev->info.manufacturer)) {
		os_memset(dev->info.manufacturer, 0,
			  sizeof(dev->info.manufacturer));
		os_memcpy(dev->info.manufacturer, msg->manufacturer,
			  msg->manufacturer_len);
	}

	if (msg->model_name &&
	    msg->model_name_len < sizeof(dev->info.model_name)) {
		os_memset(dev->info.model_name, 0,
			  sizeof(dev->info.model_name));
		os_memcpy(dev->info.model_name, msg->model_name,
			  msg->model_name_len);
	}

	if (msg->model_number &&
	    msg->model_number_len < sizeof(dev->info.model_number)) {
		os_memset(dev->info.model_number, 0,
			  sizeof(dev->info.model_number));
		os_memcpy(dev->info.model_number, msg->model_number,
			  msg->model_number_len);
	}

	if (msg->serial_number &&
	    msg->serial_number_len < sizeof(dev->info.serial_number)) {
		os_memset(dev->info.serial_number, 0,
			  sizeof(dev->info.serial_number));
		os_memcpy(dev->info.serial_number, msg->serial_number,
			  msg->serial_number_len);
	}

	if (msg->pri_dev_type)
		os_memcpy(dev->info.pri_dev_type, msg->pri_dev_type,
			  sizeof(dev->info.pri_dev_type));
	else if (msg->wps_pri_dev_type)
		os_memcpy(dev->info.pri_dev_type, msg->wps_pri_dev_type,
			  sizeof(dev->info.pri_dev_type));

	if (msg->wps_sec_dev_type_list) {
		os_memcpy(dev->info.wps_sec_dev_type_list,
			  msg->wps_sec_dev_type_list,
			  msg->wps_sec_dev_type_list_len);
		dev->info.wps_sec_dev_type_list_len =
			msg->wps_sec_dev_type_list_len;
	}

	if (msg->capability) {
		dev->info.dev_capab = msg->capability[0];
		dev->info.group_capab = msg->capability[1];
	}

	if (msg->ext_listen_timing) {
		dev->ext_listen_period = WPA_GET_LE16(msg->ext_listen_timing);
		dev->ext_listen_interval =
			WPA_GET_LE16(msg->ext_listen_timing + 2);
	}

	if (!probe_req) {
		dev->info.config_methods = msg->config_methods ?
			msg->config_methods : msg->wps_config_methods;
	}
}


/**
 * p2p_add_device - Add peer entries based on scan results
 * @p2p: P2P module context from p2p_init()
 * @addr: Source address of Beacon or Probe Response frame (may be either
 *	P2P Device Address or P2P Interface Address)
 * @level: Signal level (signal strength of the received frame from the peer)
 * @freq: Frequency on which the Beacon or Probe Response frame was received
 * @ies: IEs from the Beacon or Probe Response frame
 * @ies_len: Length of ies buffer in octets
 * Returns: 0 on success, -1 on failure
 *
 * If the scan result is for a GO, the clients in the group will also be added
 * to the peer table. This function can also be used with some other frames
 * like Provision Discovery Request that contains P2P Capability and P2P Device
 * Info attributes.
 */
int p2p_add_device(struct p2p_data *p2p, const u8 *addr, int freq, int level,
		   const u8 *ies, size_t ies_len)
{
	struct p2p_device *dev;
	struct p2p_message msg;
	const u8 *p2p_dev_addr;
	int i;

	os_memset(&msg, 0, sizeof(msg));
	if (p2p_parse_ies(ies, ies_len, &msg)) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Failed to parse P2P IE for a device entry");
		p2p_parse_free(&msg);
		return -1;
	}

	if (msg.p2p_device_addr)
		p2p_dev_addr = msg.p2p_device_addr;
	else if (msg.device_id)
		p2p_dev_addr = msg.device_id;
	else {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Ignore scan data without P2P Device Info or "
			"P2P Device Id");
		p2p_parse_free(&msg);
		return -1;
	}

	if (!is_zero_ether_addr(p2p->peer_filter) &&
	    os_memcmp(p2p_dev_addr, p2p->peer_filter, ETH_ALEN) != 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Do not add peer "
			"filter for " MACSTR " due to peer filter",
			MAC2STR(p2p_dev_addr));
		return 0;
	}

	dev = p2p_create_device(p2p, p2p_dev_addr);
	if (dev == NULL) {
		p2p_parse_free(&msg);
		return -1;
	}
	os_get_time(&dev->last_seen);
	dev->flags &= ~(P2P_DEV_PROBE_REQ_ONLY | P2P_DEV_GROUP_CLIENT_ONLY);

	if (os_memcmp(addr, p2p_dev_addr, ETH_ALEN) != 0)
		os_memcpy(dev->interface_addr, addr, ETH_ALEN);
	if (msg.ssid &&
	    (msg.ssid[1] != P2P_WILDCARD_SSID_LEN ||
	     os_memcmp(msg.ssid + 2, P2P_WILDCARD_SSID, P2P_WILDCARD_SSID_LEN)
	     != 0)) {
		os_memcpy(dev->oper_ssid, msg.ssid + 2, msg.ssid[1]);
		dev->oper_ssid_len = msg.ssid[1];
	}

	if (freq >= 2412 && freq <= 2484 && msg.ds_params &&
	    *msg.ds_params >= 1 && *msg.ds_params <= 14) {
		int ds_freq;
		if (*msg.ds_params == 14)
			ds_freq = 2484;
		else
			ds_freq = 2407 + *msg.ds_params * 5;
		if (freq != ds_freq) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Update Listen frequency based on DS "
				"Parameter Set IE: %d -> %d MHz",
				freq, ds_freq);
			freq = ds_freq;
		}
	}

	if (dev->listen_freq && dev->listen_freq != freq) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Update Listen frequency based on scan "
			"results (" MACSTR " %d -> %d MHz (DS param %d)",
			MAC2STR(dev->info.p2p_device_addr), dev->listen_freq,
			freq, msg.ds_params ? *msg.ds_params : -1);
	}
	dev->listen_freq = freq;
	if (msg.group_info)
		dev->oper_freq = freq;
	dev->level = level;

	p2p_copy_wps_info(dev, 0, &msg);

	for (i = 0; i < P2P_MAX_WPS_VENDOR_EXT; i++) {
		wpabuf_free(dev->info.wps_vendor_ext[i]);
		dev->info.wps_vendor_ext[i] = NULL;
	}

	for (i = 0; i < P2P_MAX_WPS_VENDOR_EXT; i++) {
		if (msg.wps_vendor_ext[i] == NULL)
			break;
		dev->info.wps_vendor_ext[i] = wpabuf_alloc_copy(
			msg.wps_vendor_ext[i], msg.wps_vendor_ext_len[i]);
		if (dev->info.wps_vendor_ext[i] == NULL)
			break;
	}

	p2p_add_group_clients(p2p, p2p_dev_addr, addr, freq, msg.group_info,
			      msg.group_info_len);

	p2p_parse_free(&msg);

	if (p2p_pending_sd_req(p2p, dev))
		dev->flags |= P2P_DEV_SD_SCHEDULE;

	if (dev->flags & P2P_DEV_REPORTED)
		return 0;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Peer found with Listen frequency %d MHz", freq);
	if (dev->flags & P2P_DEV_USER_REJECTED) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Do not report rejected device");
		return 0;
	}

	p2p->cfg->dev_found(p2p->cfg->cb_ctx, addr, &dev->info,
			    !(dev->flags & P2P_DEV_REPORTED_ONCE));
	dev->flags |= P2P_DEV_REPORTED | P2P_DEV_REPORTED_ONCE;

	return 0;
}


static void p2p_device_free(struct p2p_data *p2p, struct p2p_device *dev)
{
	int i;

	if (p2p->go_neg_peer == dev)
		p2p->go_neg_peer = NULL;
	if (p2p->invite_peer == dev)
		p2p->invite_peer = NULL;
	if (p2p->sd_peer == dev)
		p2p->sd_peer = NULL;
	if (p2p->pending_client_disc_go == dev)
		p2p->pending_client_disc_go = NULL;

	p2p->cfg->dev_lost(p2p->cfg->cb_ctx, dev->info.p2p_device_addr);

	for (i = 0; i < P2P_MAX_WPS_VENDOR_EXT; i++) {
		wpabuf_free(dev->info.wps_vendor_ext[i]);
		dev->info.wps_vendor_ext[i] = NULL;
	}

	os_free(dev);
}


static int p2p_get_next_prog_freq(struct p2p_data *p2p)
{
	struct p2p_channels *c;
	struct p2p_reg_class *cla;
	size_t cl, ch;
	int found = 0;
	u8 reg_class;
	u8 channel;
	int freq;

	c = &p2p->cfg->channels;
	for (cl = 0; cl < c->reg_classes; cl++) {
		cla = &c->reg_class[cl];
		if (cla->reg_class != p2p->last_prog_scan_class)
			continue;
		for (ch = 0; ch < cla->channels; ch++) {
			if (cla->channel[ch] == p2p->last_prog_scan_chan) {
				found = 1;
				break;
			}
		}
		if (found)
			break;
	}

	if (!found) {
		/* Start from beginning */
		reg_class = c->reg_class[0].reg_class;
		channel = c->reg_class[0].channel[0];
	} else {
		/* Pick the next channel */
		ch++;
		if (ch == cla->channels) {
			cl++;
			if (cl == c->reg_classes)
				cl = 0;
			ch = 0;
		}
		reg_class = c->reg_class[cl].reg_class;
		channel = c->reg_class[cl].channel[ch];
	}

	freq = p2p_channel_to_freq(p2p->cfg->country, reg_class, channel);
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Next progressive search "
		"channel: reg_class %u channel %u -> %d MHz",
		reg_class, channel, freq);
	p2p->last_prog_scan_class = reg_class;
	p2p->last_prog_scan_chan = channel;

	if (freq == 2412 || freq == 2437 || freq == 2462)
		return 0; /* No need to add social channels */
	return freq;
}


static void p2p_search(struct p2p_data *p2p)
{
	int freq = 0;
	enum p2p_scan_type type;

	if (p2p->drv_in_listen) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Driver is still "
			"in Listen state - wait for it to end before "
			"continuing");
		return;
	}
	p2p->cfg->stop_listen(p2p->cfg->cb_ctx);

	if (p2p->go_neg_peer) {
		/*
		 * Only scan the known listen frequency of the peer
		 * during GO Negotiation start.
		 */
		freq = p2p->go_neg_peer->listen_freq;
		if (freq <= 0)
			freq = p2p->go_neg_peer->oper_freq;
		type = P2P_SCAN_SPECIFIC;
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Starting search "
			"for freq %u (GO Neg)", freq);
	} else if (p2p->invite_peer) {
		/*
		 * Only scan the known listen frequency of the peer
		 * during Invite start.
		 */
		freq = p2p->invite_peer->listen_freq;
		if (freq <= 0)
			freq = p2p->invite_peer->oper_freq;
		type = P2P_SCAN_SPECIFIC;
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Starting search "
			"for freq %u (Invite)", freq);
	} else if (p2p->find_type == P2P_FIND_PROGRESSIVE &&
		   (freq = p2p_get_next_prog_freq(p2p)) > 0) {
		type = P2P_SCAN_SOCIAL_PLUS_ONE;
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Starting search "
			"(+ freq %u)", freq);
	} else {
		type = P2P_SCAN_SOCIAL;
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Starting search");
	}

	if (p2p->cfg->p2p_scan(p2p->cfg->cb_ctx, type, freq,
			       p2p->num_req_dev_types, p2p->req_dev_types) < 0)
	{
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Scan request failed");
		p2p_continue_find(p2p);
	} else {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Running p2p_scan");
		p2p->p2p_scan_running = 1;
		eloop_cancel_timeout(p2p_scan_timeout, p2p, NULL);
		eloop_register_timeout(P2P_SCAN_TIMEOUT, 0, p2p_scan_timeout,
				       p2p, NULL);
	}
}


static void p2p_find_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct p2p_data *p2p = eloop_ctx;
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Find timeout -> stop");
	p2p_stop_find(p2p);
}


static int p2p_run_after_scan(struct p2p_data *p2p)
{
	struct p2p_device *dev;
	enum p2p_after_scan op;

	if (p2p->after_scan_tx) {
		int ret;
		/* TODO: schedule p2p_run_after_scan to be called from TX
		 * status callback(?) */
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Send pending "
			"Action frame at p2p_scan completion");
		ret = p2p->cfg->send_action(p2p->cfg->cb_ctx,
					    p2p->after_scan_tx->freq,
					    p2p->after_scan_tx->dst,
					    p2p->after_scan_tx->src,
					    p2p->after_scan_tx->bssid,
					    (u8 *) (p2p->after_scan_tx + 1),
					    p2p->after_scan_tx->len,
					    p2p->after_scan_tx->wait_time);
		os_free(p2p->after_scan_tx);
		p2p->after_scan_tx = NULL;
		return 1;
	}

	op = p2p->start_after_scan;
	p2p->start_after_scan = P2P_AFTER_SCAN_NOTHING;
	switch (op) {
	case P2P_AFTER_SCAN_NOTHING:
		break;
	case P2P_AFTER_SCAN_LISTEN:
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Start previously "
			"requested Listen state");
		p2p_listen(p2p, p2p->pending_listen_sec * 1000 +
			   p2p->pending_listen_usec / 1000);
		return 1;
	case P2P_AFTER_SCAN_CONNECT:
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Start previously "
			"requested connect with " MACSTR,
			MAC2STR(p2p->after_scan_peer));
		dev = p2p_get_device(p2p, p2p->after_scan_peer);
		if (dev == NULL) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Peer not "
				"known anymore");
			break;
		}
		p2p_connect_send(p2p, dev);
		return 1;
	}

	return 0;
}


static void p2p_scan_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct p2p_data *p2p = eloop_ctx;
	int running;
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: p2p_scan timeout "
		"(running=%d)", p2p->p2p_scan_running);
	running = p2p->p2p_scan_running;
	/* Make sure we recover from missed scan results callback */
	p2p->p2p_scan_running = 0;

	if (running)
		p2p_run_after_scan(p2p);
}


static void p2p_free_req_dev_types(struct p2p_data *p2p)
{
	p2p->num_req_dev_types = 0;
	os_free(p2p->req_dev_types);
	p2p->req_dev_types = NULL;
}


int p2p_find(struct p2p_data *p2p, unsigned int timeout,
	     enum p2p_discovery_type type,
	     unsigned int num_req_dev_types, const u8 *req_dev_types)
{
	int res;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Starting find (type=%d)",
		type);
	if (p2p->p2p_scan_running) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: p2p_scan is "
			"already running");
	}

	p2p_free_req_dev_types(p2p);
	if (req_dev_types && num_req_dev_types) {
		p2p->req_dev_types = os_malloc(num_req_dev_types *
					       WPS_DEV_TYPE_LEN);
		if (p2p->req_dev_types == NULL)
			return -1;
		os_memcpy(p2p->req_dev_types, req_dev_types,
			  num_req_dev_types * WPS_DEV_TYPE_LEN);
		p2p->num_req_dev_types = num_req_dev_types;
	}

	p2p->start_after_scan = P2P_AFTER_SCAN_NOTHING;
	p2p_clear_timeout(p2p);
	p2p->cfg->stop_listen(p2p->cfg->cb_ctx);
	p2p->find_type = type;
	p2p_device_clear_reported(p2p);
	p2p_set_state(p2p, P2P_SEARCH);
	eloop_cancel_timeout(p2p_find_timeout, p2p, NULL);
	if (timeout)
		eloop_register_timeout(timeout, 0, p2p_find_timeout,
				       p2p, NULL);
	switch (type) {
	case P2P_FIND_START_WITH_FULL:
	case P2P_FIND_PROGRESSIVE:
		res = p2p->cfg->p2p_scan(p2p->cfg->cb_ctx, P2P_SCAN_FULL, 0,
					 p2p->num_req_dev_types,
					 p2p->req_dev_types);
		break;
	case P2P_FIND_ONLY_SOCIAL:
		res = p2p->cfg->p2p_scan(p2p->cfg->cb_ctx, P2P_SCAN_SOCIAL, 0,
					 p2p->num_req_dev_types,
					 p2p->req_dev_types);
		break;
	default:
		return -1;
	}

	if (res == 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Running p2p_scan");
		p2p->p2p_scan_running = 1;
		eloop_cancel_timeout(p2p_scan_timeout, p2p, NULL);
		eloop_register_timeout(P2P_SCAN_TIMEOUT, 0, p2p_scan_timeout,
				       p2p, NULL);
	} else {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Failed to start "
			"p2p_scan");
	}

	return res;
}


void p2p_stop_find_for_freq(struct p2p_data *p2p, int freq)
{
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Stopping find");
	eloop_cancel_timeout(p2p_find_timeout, p2p, NULL);
	p2p_clear_timeout(p2p);
	p2p_set_state(p2p, P2P_IDLE);
	p2p_free_req_dev_types(p2p);
	p2p->start_after_scan = P2P_AFTER_SCAN_NOTHING;
	p2p->go_neg_peer = NULL;
	p2p->sd_peer = NULL;
	p2p->invite_peer = NULL;
	if (freq > 0 && p2p->drv_in_listen == freq && p2p->in_listen) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Skip stop_listen "
			"since we are on correct channel for response");
		return;
	}
	p2p->cfg->stop_listen(p2p->cfg->cb_ctx);
}


void p2p_stop_find(struct p2p_data *p2p)
{
	p2p_stop_find_for_freq(p2p, 0);
}


static int p2p_prepare_channel(struct p2p_data *p2p, unsigned int force_freq)
{
	if (force_freq) {
		u8 op_reg_class, op_channel;
		if (p2p_freq_to_channel(p2p->cfg->country, force_freq,
					&op_reg_class, &op_channel) < 0) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Unsupported frequency %u MHz",
				force_freq);
			return -1;
		}
		if (!p2p_channels_includes(&p2p->cfg->channels, op_reg_class,
					   op_channel)) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Frequency %u MHz (oper_class %u "
				"channel %u) not allowed for P2P",
				force_freq, op_reg_class, op_channel);
			return -1;
		}
		p2p->op_reg_class = op_reg_class;
		p2p->op_channel = op_channel;
		p2p->channels.reg_classes = 1;
		p2p->channels.reg_class[0].channels = 1;
		p2p->channels.reg_class[0].reg_class = p2p->op_reg_class;
		p2p->channels.reg_class[0].channel[0] = p2p->op_channel;
	} else {
		u8 op_reg_class, op_channel;

		if (!p2p->cfg->cfg_op_channel && p2p->best_freq_overall > 0 &&
		    p2p_supported_freq(p2p, p2p->best_freq_overall) &&
		    p2p_freq_to_channel(p2p->cfg->country,
					p2p->best_freq_overall,
					&op_reg_class, &op_channel) == 0) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Select best overall channel as "
				"operating channel preference");
			p2p->op_reg_class = op_reg_class;
			p2p->op_channel = op_channel;
		} else if (!p2p->cfg->cfg_op_channel && p2p->best_freq_5 > 0 &&
			   p2p_supported_freq(p2p, p2p->best_freq_5) &&
			   p2p_freq_to_channel(p2p->cfg->country,
					       p2p->best_freq_5,
					       &op_reg_class, &op_channel) ==
			   0) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Select best 5 GHz channel as "
				"operating channel preference");
			p2p->op_reg_class = op_reg_class;
			p2p->op_channel = op_channel;
		} else if (!p2p->cfg->cfg_op_channel &&
			   p2p->best_freq_24 > 0 &&
			   p2p_supported_freq(p2p, p2p->best_freq_24) &&
			   p2p_freq_to_channel(p2p->cfg->country,
					       p2p->best_freq_24,
					       &op_reg_class, &op_channel) ==
			   0) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Select best 2.4 GHz channel as "
				"operating channel preference");
			p2p->op_reg_class = op_reg_class;
			p2p->op_channel = op_channel;
		} else {
			p2p->op_reg_class = p2p->cfg->op_reg_class;
			p2p->op_channel = p2p->cfg->op_channel;
		}

		os_memcpy(&p2p->channels, &p2p->cfg->channels,
			  sizeof(struct p2p_channels));
	}
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Own preference for operation channel: "
		"Operating Class %u Channel %u%s",
		p2p->op_reg_class, p2p->op_channel,
		force_freq ? " (forced)" : "");

	return 0;
}


int p2p_connect(struct p2p_data *p2p, const u8 *peer_addr,
		enum p2p_wps_method wps_method,
		int go_intent, const u8 *own_interface_addr,
		unsigned int force_freq, int persistent_group)
{
	struct p2p_device *dev;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Request to start group negotiation - peer=" MACSTR
		"  GO Intent=%d  Intended Interface Address=" MACSTR
		" wps_method=%d persistent_group=%d",
		MAC2STR(peer_addr), go_intent, MAC2STR(own_interface_addr),
		wps_method, persistent_group);

	if (p2p_prepare_channel(p2p, force_freq) < 0)
		return -1;

	dev = p2p_get_device(p2p, peer_addr);
	if (dev == NULL || (dev->flags & P2P_DEV_PROBE_REQ_ONLY)) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Cannot connect to unknown P2P Device " MACSTR,
			MAC2STR(peer_addr));
		return -1;
	}

	if (dev->flags & P2P_DEV_GROUP_CLIENT_ONLY) {
		if (!(dev->info.dev_capab &
		      P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY)) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Cannot connect to P2P Device " MACSTR
				" that is in a group and is not discoverable",
				MAC2STR(peer_addr));
			return -1;
		}
		if (dev->oper_freq <= 0) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Cannot connect to P2P Device " MACSTR
				" with incomplete information",
				MAC2STR(peer_addr));
			return -1;
		}

		/*
		 * First, try to connect directly. If the peer does not
		 * acknowledge frames, assume it is sleeping and use device
		 * discoverability via the GO at that point.
		 */
	}

	dev->flags &= ~P2P_DEV_NOT_YET_READY;
	dev->flags &= ~P2P_DEV_USER_REJECTED;
	dev->flags &= ~P2P_DEV_WAIT_GO_NEG_RESPONSE;
	dev->flags &= ~P2P_DEV_WAIT_GO_NEG_CONFIRM;
	dev->connect_reqs = 0;
	dev->go_neg_req_sent = 0;
	dev->go_state = UNKNOWN_GO;
	if (persistent_group)
		dev->flags |= P2P_DEV_PREFER_PERSISTENT_GROUP;
	else
		dev->flags &= ~P2P_DEV_PREFER_PERSISTENT_GROUP;
	p2p->go_intent = go_intent;
	os_memcpy(p2p->intended_addr, own_interface_addr, ETH_ALEN);

	if (p2p->state != P2P_IDLE)
		p2p_stop_find(p2p);

	if (p2p->after_scan_tx) {
		/*
		 * We need to drop the pending frame to avoid issues with the
		 * new GO Negotiation, e.g., when the pending frame was from a
		 * previous attempt at starting a GO Negotiation.
		 */
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Dropped "
			"previous pending Action frame TX that was waiting "
			"for p2p_scan completion");
		os_free(p2p->after_scan_tx);
		p2p->after_scan_tx = NULL;
	}

	dev->wps_method = wps_method;
	dev->status = P2P_SC_SUCCESS;

	if (force_freq)
		dev->flags |= P2P_DEV_FORCE_FREQ;
	else
		dev->flags &= ~P2P_DEV_FORCE_FREQ;

	if (p2p->p2p_scan_running) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: p2p_scan running - delay connect send");
		p2p->start_after_scan = P2P_AFTER_SCAN_CONNECT;
		os_memcpy(p2p->after_scan_peer, peer_addr, ETH_ALEN);
		return 0;
	}
	p2p->start_after_scan = P2P_AFTER_SCAN_NOTHING;

	return p2p_connect_send(p2p, dev);
}


int p2p_authorize(struct p2p_data *p2p, const u8 *peer_addr,
		  enum p2p_wps_method wps_method,
		  int go_intent, const u8 *own_interface_addr,
		  unsigned int force_freq, int persistent_group)
{
	struct p2p_device *dev;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Request to authorize group negotiation - peer=" MACSTR
		"  GO Intent=%d  Intended Interface Address=" MACSTR
		" wps_method=%d  persistent_group=%d",
		MAC2STR(peer_addr), go_intent, MAC2STR(own_interface_addr),
		wps_method, persistent_group);

	if (p2p_prepare_channel(p2p, force_freq) < 0)
		return -1;

	dev = p2p_get_device(p2p, peer_addr);
	if (dev == NULL) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Cannot authorize unknown P2P Device " MACSTR,
			MAC2STR(peer_addr));
		return -1;
	}

	dev->flags &= ~P2P_DEV_NOT_YET_READY;
	dev->flags &= ~P2P_DEV_USER_REJECTED;
	dev->go_neg_req_sent = 0;
	dev->go_state = UNKNOWN_GO;
	if (persistent_group)
		dev->flags |= P2P_DEV_PREFER_PERSISTENT_GROUP;
	else
		dev->flags &= ~P2P_DEV_PREFER_PERSISTENT_GROUP;
	p2p->go_intent = go_intent;
	os_memcpy(p2p->intended_addr, own_interface_addr, ETH_ALEN);

	dev->wps_method = wps_method;
	dev->status = P2P_SC_SUCCESS;

	if (force_freq)
		dev->flags |= P2P_DEV_FORCE_FREQ;
	else
		dev->flags &= ~P2P_DEV_FORCE_FREQ;

	return 0;
}


void p2p_add_dev_info(struct p2p_data *p2p, const u8 *addr,
		      struct p2p_device *dev, struct p2p_message *msg)
{
	os_get_time(&dev->last_seen);

	p2p_copy_wps_info(dev, 0, msg);

	if (msg->listen_channel) {
		int freq;
		freq = p2p_channel_to_freq((char *) msg->listen_channel,
					   msg->listen_channel[3],
					   msg->listen_channel[4]);
		if (freq < 0) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Unknown peer Listen channel: "
				"country=%c%c(0x%02x) reg_class=%u channel=%u",
				msg->listen_channel[0],
				msg->listen_channel[1],
				msg->listen_channel[2],
				msg->listen_channel[3],
				msg->listen_channel[4]);
		} else {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Update "
				"peer " MACSTR " Listen channel: %u -> %u MHz",
				MAC2STR(dev->info.p2p_device_addr),
				dev->listen_freq, freq);
			dev->listen_freq = freq;
		}
	}

	if (dev->flags & P2P_DEV_PROBE_REQ_ONLY) {
		dev->flags &= ~P2P_DEV_PROBE_REQ_ONLY;
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Completed device entry based on data from "
			"GO Negotiation Request");
	} else {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Created device entry based on GO Neg Req: "
			MACSTR " dev_capab=0x%x group_capab=0x%x name='%s' "
			"listen_freq=%d",
			MAC2STR(dev->info.p2p_device_addr),
			dev->info.dev_capab, dev->info.group_capab,
			dev->info.device_name, dev->listen_freq);
	}

	dev->flags &= ~P2P_DEV_GROUP_CLIENT_ONLY;

	if (dev->flags & P2P_DEV_USER_REJECTED) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Do not report rejected device");
		return;
	}

	p2p->cfg->dev_found(p2p->cfg->cb_ctx, addr, &dev->info,
			    !(dev->flags & P2P_DEV_REPORTED_ONCE));
	dev->flags |= P2P_DEV_REPORTED | P2P_DEV_REPORTED_ONCE;
}


void p2p_build_ssid(struct p2p_data *p2p, u8 *ssid, size_t *ssid_len)
{
	os_memcpy(ssid, P2P_WILDCARD_SSID, P2P_WILDCARD_SSID_LEN);
	p2p_random((char *) &ssid[P2P_WILDCARD_SSID_LEN], 2);
	os_memcpy(&ssid[P2P_WILDCARD_SSID_LEN + 2],
		  p2p->cfg->ssid_postfix, p2p->cfg->ssid_postfix_len);
	*ssid_len = P2P_WILDCARD_SSID_LEN + 2 + p2p->cfg->ssid_postfix_len;
}


int p2p_go_params(struct p2p_data *p2p, struct p2p_go_neg_results *params)
{
	p2p_build_ssid(p2p, params->ssid, &params->ssid_len);
	p2p_random(params->passphrase, 8);
	return 0;
}


void p2p_go_complete(struct p2p_data *p2p, struct p2p_device *peer)
{
	struct p2p_go_neg_results res;
	int go = peer->go_state == LOCAL_GO;
	struct p2p_channels intersection;
	int freqs;
	size_t i, j;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: GO Negotiation with " MACSTR " completed (%s will be "
		"GO)", MAC2STR(peer->info.p2p_device_addr),
		go ? "local end" : "peer");

	os_memset(&res, 0, sizeof(res));
	res.role_go = go;
	os_memcpy(res.peer_device_addr, peer->info.p2p_device_addr, ETH_ALEN);
	os_memcpy(res.peer_interface_addr, peer->intended_addr, ETH_ALEN);
	res.wps_method = peer->wps_method;
	if (peer->flags & P2P_DEV_PREFER_PERSISTENT_GROUP)
		res.persistent_group = 1;

	if (go) {
		/* Setup AP mode for WPS provisioning */
		res.freq = p2p_channel_to_freq(p2p->cfg->country,
					       p2p->op_reg_class,
					       p2p->op_channel);
		os_memcpy(res.ssid, p2p->ssid, p2p->ssid_len);
		res.ssid_len = p2p->ssid_len;
		p2p_random(res.passphrase, 8);
	} else {
		res.freq = peer->oper_freq;
		if (p2p->ssid_len) {
			os_memcpy(res.ssid, p2p->ssid, p2p->ssid_len);
			res.ssid_len = p2p->ssid_len;
		}
	}

	p2p_channels_intersect(&p2p->channels, &peer->channels,
			       &intersection);
	freqs = 0;
	for (i = 0; i < intersection.reg_classes; i++) {
		struct p2p_reg_class *c = &intersection.reg_class[i];
		if (freqs + 1 == P2P_MAX_CHANNELS)
			break;
		for (j = 0; j < c->channels; j++) {
			int freq;
			if (freqs + 1 == P2P_MAX_CHANNELS)
				break;
			freq = p2p_channel_to_freq(peer->country, c->reg_class,
						   c->channel[j]);
			if (freq < 0)
				continue;
			res.freq_list[freqs++] = freq;
		}
	}

	res.peer_config_timeout = go ? peer->client_timeout : peer->go_timeout;

	p2p_clear_timeout(p2p);
	peer->go_neg_req_sent = 0;
	peer->wps_method = WPS_NOT_READY;

	p2p_set_state(p2p, P2P_PROVISIONING);
	p2p->cfg->go_neg_completed(p2p->cfg->cb_ctx, &res);
}


static void p2p_rx_p2p_action(struct p2p_data *p2p, const u8 *sa,
			      const u8 *data, size_t len, int rx_freq)
{
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: RX P2P Public Action from " MACSTR, MAC2STR(sa));
	wpa_hexdump(MSG_MSGDUMP, "P2P: P2P Public Action contents", data, len);

	if (len < 1)
		return;

	switch (data[0]) {
	case P2P_GO_NEG_REQ:
		p2p_process_go_neg_req(p2p, sa, data + 1, len - 1, rx_freq);
		break;
	case P2P_GO_NEG_RESP:
		p2p_process_go_neg_resp(p2p, sa, data + 1, len - 1, rx_freq);
		break;
	case P2P_GO_NEG_CONF:
		p2p_process_go_neg_conf(p2p, sa, data + 1, len - 1);
		break;
	case P2P_INVITATION_REQ:
		p2p_process_invitation_req(p2p, sa, data + 1, len - 1,
					   rx_freq);
		break;
	case P2P_INVITATION_RESP:
		p2p_process_invitation_resp(p2p, sa, data + 1, len - 1);
		break;
	case P2P_PROV_DISC_REQ:
		p2p_process_prov_disc_req(p2p, sa, data + 1, len - 1, rx_freq);
		break;
	case P2P_PROV_DISC_RESP:
		p2p_process_prov_disc_resp(p2p, sa, data + 1, len - 1);
		break;
	case P2P_DEV_DISC_REQ:
		p2p_process_dev_disc_req(p2p, sa, data + 1, len - 1, rx_freq);
		break;
	case P2P_DEV_DISC_RESP:
		p2p_process_dev_disc_resp(p2p, sa, data + 1, len - 1);
		break;
	default:
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unsupported P2P Public Action frame type %d",
			data[0]);
		break;
	}
}


void p2p_rx_action_public(struct p2p_data *p2p, const u8 *da, const u8 *sa,
			  const u8 *bssid, const u8 *data, size_t len,
			  int freq)
{
	if (len < 1)
		return;

	switch (data[0]) {
	case WLAN_PA_VENDOR_SPECIFIC:
		data++;
		len--;
		if (len < 3)
			return;
		if (WPA_GET_BE24(data) != OUI_WFA)
			return;

		data += 3;
		len -= 3;
		if (len < 1)
			return;

		if (*data != P2P_OUI_TYPE)
			return;

		p2p_rx_p2p_action(p2p, sa, data + 1, len - 1, freq);
		break;
	case WLAN_PA_GAS_INITIAL_REQ:
		p2p_rx_gas_initial_req(p2p, sa, data + 1, len - 1, freq);
		break;
	case WLAN_PA_GAS_INITIAL_RESP:
		p2p_rx_gas_initial_resp(p2p, sa, data + 1, len - 1, freq);
		break;
	case WLAN_PA_GAS_COMEBACK_REQ:
		p2p_rx_gas_comeback_req(p2p, sa, data + 1, len - 1, freq);
		break;
	case WLAN_PA_GAS_COMEBACK_RESP:
		p2p_rx_gas_comeback_resp(p2p, sa, data + 1, len - 1, freq);
		break;
	}
}


void p2p_rx_action(struct p2p_data *p2p, const u8 *da, const u8 *sa,
		   const u8 *bssid, u8 category,
		   const u8 *data, size_t len, int freq)
{
	if (category == WLAN_ACTION_PUBLIC) {
		p2p_rx_action_public(p2p, da, sa, bssid, data, len, freq);
		return;
	}

	if (category != WLAN_ACTION_VENDOR_SPECIFIC)
		return;

	if (len < 4)
		return;

	if (WPA_GET_BE24(data) != OUI_WFA)
		return;
	data += 3;
	len -= 3;

	if (*data != P2P_OUI_TYPE)
		return;
	data++;
	len--;

	/* P2P action frame */
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: RX P2P Action from " MACSTR, MAC2STR(sa));
	wpa_hexdump(MSG_MSGDUMP, "P2P: P2P Action contents", data, len);

	if (len < 1)
		return;
	switch (data[0]) {
	case P2P_NOA:
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Received P2P Action - Notice of Absence");
		/* TODO */
		break;
	case P2P_PRESENCE_REQ:
		p2p_process_presence_req(p2p, da, sa, data + 1, len - 1, freq);
		break;
	case P2P_PRESENCE_RESP:
		p2p_process_presence_resp(p2p, da, sa, data + 1, len - 1);
		break;
	case P2P_GO_DISC_REQ:
		p2p_process_go_disc_req(p2p, da, sa, data + 1, len - 1, freq);
		break;
	default:
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Received P2P Action - unknown type %u", data[0]);
		break;
	}
}


static void p2p_go_neg_start(void *eloop_ctx, void *timeout_ctx)
{
	struct p2p_data *p2p = eloop_ctx;
	if (p2p->go_neg_peer == NULL)
		return;
	p2p->cfg->stop_listen(p2p->cfg->cb_ctx);
	p2p->go_neg_peer->status = P2P_SC_SUCCESS;
	p2p_connect_send(p2p, p2p->go_neg_peer);
}


static void p2p_invite_start(void *eloop_ctx, void *timeout_ctx)
{
	struct p2p_data *p2p = eloop_ctx;
	if (p2p->invite_peer == NULL)
		return;
	p2p->cfg->stop_listen(p2p->cfg->cb_ctx);
	p2p_invite_send(p2p, p2p->invite_peer, p2p->invite_go_dev_addr);
}


static void p2p_add_dev_from_probe_req(struct p2p_data *p2p, const u8 *addr,
				       const u8 *ie, size_t ie_len)
{
	struct p2p_message msg;
	struct p2p_device *dev;

	os_memset(&msg, 0, sizeof(msg));
	if (p2p_parse_ies(ie, ie_len, &msg) < 0 || msg.p2p_attributes == NULL)
	{
		p2p_parse_free(&msg);
		return; /* not a P2P probe */
	}

	if (msg.ssid == NULL || msg.ssid[1] != P2P_WILDCARD_SSID_LEN ||
	    os_memcmp(msg.ssid + 2, P2P_WILDCARD_SSID, P2P_WILDCARD_SSID_LEN)
	    != 0) {
		/* The Probe Request is not part of P2P Device Discovery. It is
		 * not known whether the source address of the frame is the P2P
		 * Device Address or P2P Interface Address. Do not add a new
		 * peer entry based on this frames.
		 */
		p2p_parse_free(&msg);
		return;
	}

	dev = p2p_get_device(p2p, addr);
	if (dev) {
		if (dev->country[0] == 0 && msg.listen_channel)
			os_memcpy(dev->country, msg.listen_channel, 3);
		p2p_parse_free(&msg);
		return; /* already known */
	}

	dev = p2p_create_device(p2p, addr);
	if (dev == NULL) {
		p2p_parse_free(&msg);
		return;
	}

	os_get_time(&dev->last_seen);
	dev->flags |= P2P_DEV_PROBE_REQ_ONLY;

	if (msg.listen_channel) {
		os_memcpy(dev->country, msg.listen_channel, 3);
		dev->listen_freq = p2p_channel_to_freq(dev->country,
						       msg.listen_channel[3],
						       msg.listen_channel[4]);
	}

	p2p_copy_wps_info(dev, 1, &msg);

	p2p_parse_free(&msg);

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Created device entry based on Probe Req: " MACSTR
		" dev_capab=0x%x group_capab=0x%x name='%s' listen_freq=%d",
		MAC2STR(dev->info.p2p_device_addr), dev->info.dev_capab,
		dev->info.group_capab, dev->info.device_name,
		dev->listen_freq);
}


struct p2p_device * p2p_add_dev_from_go_neg_req(struct p2p_data *p2p,
						const u8 *addr,
						struct p2p_message *msg)
{
	struct p2p_device *dev;

	dev = p2p_get_device(p2p, addr);
	if (dev) {
		os_get_time(&dev->last_seen);
		return dev; /* already known */
	}

	dev = p2p_create_device(p2p, addr);
	if (dev == NULL)
		return NULL;

	p2p_add_dev_info(p2p, addr, dev, msg);

	return dev;
}


static int dev_type_match(const u8 *dev_type, const u8 *req_dev_type)
{
	if (os_memcmp(dev_type, req_dev_type, WPS_DEV_TYPE_LEN) == 0)
		return 1;
	if (os_memcmp(dev_type, req_dev_type, 2) == 0 &&
	    WPA_GET_BE32(&req_dev_type[2]) == 0 &&
	    WPA_GET_BE16(&req_dev_type[6]) == 0)
		return 1; /* Category match with wildcard OUI/sub-category */
	return 0;
}


int dev_type_list_match(const u8 *dev_type, const u8 *req_dev_type[],
			size_t num_req_dev_type)
{
	size_t i;
	for (i = 0; i < num_req_dev_type; i++) {
		if (dev_type_match(dev_type, req_dev_type[i]))
			return 1;
	}
	return 0;
}


/**
 * p2p_match_dev_type - Match local device type with requested type
 * @p2p: P2P module context from p2p_init()
 * @wps: WPS TLVs from Probe Request frame (concatenated WPS IEs)
 * Returns: 1 on match, 0 on mismatch
 *
 * This function can be used to match the Requested Device Type attribute in
 * WPS IE with the local device types for deciding whether to reply to a Probe
 * Request frame.
 */
int p2p_match_dev_type(struct p2p_data *p2p, struct wpabuf *wps)
{
	struct wps_parse_attr attr;
	size_t i;

	if (wps_parse_msg(wps, &attr))
		return 1; /* assume no Requested Device Type attributes */

	if (attr.num_req_dev_type == 0)
		return 1; /* no Requested Device Type attributes -> match */

	if (dev_type_list_match(p2p->cfg->pri_dev_type, attr.req_dev_type,
				attr.num_req_dev_type))
		return 1; /* Own Primary Device Type matches */

	for (i = 0; i < p2p->cfg->num_sec_dev_types; i++)
		if (dev_type_list_match(p2p->cfg->sec_dev_type[i],
					attr.req_dev_type,
					attr.num_req_dev_type))
		return 1; /* Own Secondary Device Type matches */

	/* No matching device type found */
	return 0;
}


struct wpabuf * p2p_build_probe_resp_ies(struct p2p_data *p2p)
{
	struct wpabuf *buf;
	u8 *len;

	buf = wpabuf_alloc(1000);
	if (buf == NULL)
		return NULL;

	p2p_build_wps_ie(p2p, buf, DEV_PW_DEFAULT, 1);

	/* P2P IE */
	len = p2p_buf_add_ie_hdr(buf);
	p2p_buf_add_capability(buf, p2p->dev_capab, 0);
	if (p2p->ext_listen_interval)
		p2p_buf_add_ext_listen_timing(buf, p2p->ext_listen_period,
					      p2p->ext_listen_interval);
	p2p_buf_add_device_info(buf, p2p, NULL);
	p2p_buf_update_ie_hdr(buf, len);

	return buf;
}


static void p2p_reply_probe(struct p2p_data *p2p, const u8 *addr, const u8 *ie,
			    size_t ie_len)
{
	struct ieee802_11_elems elems;
	struct wpabuf *buf;
	struct ieee80211_mgmt *resp;
	struct wpabuf *wps;
	struct wpabuf *ies;

	if (!p2p->in_listen || !p2p->drv_in_listen) {
		/* not in Listen state - ignore Probe Request */
		return;
	}

	if (ieee802_11_parse_elems((u8 *) ie, ie_len, &elems, 0) ==
	    ParseFailed) {
		/* Ignore invalid Probe Request frames */
		return;
	}

	if (elems.p2p == NULL) {
		/* not a P2P probe - ignore it */
		return;
	}

	if (elems.ssid == NULL || elems.ssid_len != P2P_WILDCARD_SSID_LEN ||
	    os_memcmp(elems.ssid, P2P_WILDCARD_SSID, P2P_WILDCARD_SSID_LEN) !=
	    0) {
		/* not using P2P Wildcard SSID - ignore */
		return;
	}

	/* Check Requested Device Type match */
	wps = ieee802_11_vendor_ie_concat(ie, ie_len, WPS_DEV_OUI_WFA);
	if (wps && !p2p_match_dev_type(p2p, wps)) {
		wpabuf_free(wps);
		/* No match with Requested Device Type */
		return;
	}
	wpabuf_free(wps);

	if (!p2p->cfg->send_probe_resp)
		return; /* Response generated elsewhere */

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Reply to P2P Probe Request in Listen state");

	/*
	 * We do not really have a specific BSS that this frame is advertising,
	 * so build a frame that has some information in valid format. This is
	 * really only used for discovery purposes, not to learn exact BSS
	 * parameters.
	 */
	ies = p2p_build_probe_resp_ies(p2p);
	if (ies == NULL)
		return;

	buf = wpabuf_alloc(200 + wpabuf_len(ies));
	if (buf == NULL) {
		wpabuf_free(ies);
		return;
	}

	resp = NULL;
	resp = wpabuf_put(buf, resp->u.probe_resp.variable - (u8 *) resp);

	resp->frame_control = host_to_le16((WLAN_FC_TYPE_MGMT << 2) |
					   (WLAN_FC_STYPE_PROBE_RESP << 4));
	os_memcpy(resp->da, addr, ETH_ALEN);
	os_memcpy(resp->sa, p2p->cfg->dev_addr, ETH_ALEN);
	os_memcpy(resp->bssid, p2p->cfg->dev_addr, ETH_ALEN);
	resp->u.probe_resp.beacon_int = host_to_le16(100);
	/* hardware or low-level driver will setup seq_ctrl and timestamp */
	resp->u.probe_resp.capab_info =
		host_to_le16(WLAN_CAPABILITY_SHORT_PREAMBLE |
			     WLAN_CAPABILITY_PRIVACY |
			     WLAN_CAPABILITY_SHORT_SLOT_TIME);

	wpabuf_put_u8(buf, WLAN_EID_SSID);
	wpabuf_put_u8(buf, P2P_WILDCARD_SSID_LEN);
	wpabuf_put_data(buf, P2P_WILDCARD_SSID, P2P_WILDCARD_SSID_LEN);

	wpabuf_put_u8(buf, WLAN_EID_SUPP_RATES);
	wpabuf_put_u8(buf, 8);
	wpabuf_put_u8(buf, (60 / 5) | 0x80);
	wpabuf_put_u8(buf, 90 / 5);
	wpabuf_put_u8(buf, (120 / 5) | 0x80);
	wpabuf_put_u8(buf, 180 / 5);
	wpabuf_put_u8(buf, (240 / 5) | 0x80);
	wpabuf_put_u8(buf, 360 / 5);
	wpabuf_put_u8(buf, 480 / 5);
	wpabuf_put_u8(buf, 540 / 5);

	wpabuf_put_u8(buf, WLAN_EID_DS_PARAMS);
	wpabuf_put_u8(buf, 1);
	wpabuf_put_u8(buf, p2p->cfg->channel);

	wpabuf_put_buf(buf, ies);
	wpabuf_free(ies);

	p2p->cfg->send_probe_resp(p2p->cfg->cb_ctx, buf);

	wpabuf_free(buf);
}


int p2p_probe_req_rx(struct p2p_data *p2p, const u8 *addr, const u8 *ie,
		     size_t ie_len)
{
	p2p_add_dev_from_probe_req(p2p, addr, ie, ie_len);

	p2p_reply_probe(p2p, addr, ie, ie_len);

	if ((p2p->state == P2P_CONNECT || p2p->state == P2P_CONNECT_LISTEN) &&
	    p2p->go_neg_peer &&
	    os_memcmp(addr, p2p->go_neg_peer->info.p2p_device_addr, ETH_ALEN)
	    == 0) {
		/* Received a Probe Request from GO Negotiation peer */
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Found GO Negotiation peer - try to start GO "
			"negotiation from timeout");
		eloop_register_timeout(0, 0, p2p_go_neg_start, p2p, NULL);
		return 1;
	}

	if ((p2p->state == P2P_INVITE || p2p->state == P2P_INVITE_LISTEN) &&
	    p2p->invite_peer &&
	    os_memcmp(addr, p2p->invite_peer->info.p2p_device_addr, ETH_ALEN)
	    == 0) {
		/* Received a Probe Request from Invite peer */
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Found Invite peer - try to start Invite from "
			"timeout");
		eloop_register_timeout(0, 0, p2p_invite_start, p2p, NULL);
		return 1;
	}

	return 0;
}


static int p2p_assoc_req_ie_wlan_ap(struct p2p_data *p2p, const u8 *bssid,
				    u8 *buf, size_t len, struct wpabuf *p2p_ie)
{
	struct wpabuf *tmp;
	u8 *lpos;
	size_t tmplen;
	int res;
	u8 group_capab;

	if (p2p_ie == NULL)
		return 0; /* WLAN AP is not a P2P manager */

	/*
	 * (Re)Association Request - P2P IE
	 * P2P Capability attribute (shall be present)
	 * P2P Interface attribute (present if concurrent device and
	 *	P2P Management is enabled)
	 */
	tmp = wpabuf_alloc(200);
	if (tmp == NULL)
		return -1;

	lpos = p2p_buf_add_ie_hdr(tmp);
	group_capab = 0;
	if (p2p->num_groups > 0) {
		group_capab |= P2P_GROUP_CAPAB_GROUP_OWNER;
		if ((p2p->dev_capab & P2P_DEV_CAPAB_CONCURRENT_OPER) &&
		    (p2p->dev_capab & P2P_DEV_CAPAB_INFRA_MANAGED) &&
		    p2p->cross_connect)
			group_capab |= P2P_GROUP_CAPAB_CROSS_CONN;
	}
	p2p_buf_add_capability(tmp, p2p->dev_capab, group_capab);
	if ((p2p->dev_capab & P2P_DEV_CAPAB_CONCURRENT_OPER) &&
	    (p2p->dev_capab & P2P_DEV_CAPAB_INFRA_MANAGED))
		p2p_buf_add_p2p_interface(tmp, p2p);
	p2p_buf_update_ie_hdr(tmp, lpos);

	tmplen = wpabuf_len(tmp);
	if (tmplen > len)
		res = -1;
	else {
		os_memcpy(buf, wpabuf_head(tmp), tmplen);
		res = tmplen;
	}
	wpabuf_free(tmp);

	return res;
}


int p2p_assoc_req_ie(struct p2p_data *p2p, const u8 *bssid, u8 *buf,
		     size_t len, int p2p_group, struct wpabuf *p2p_ie)
{
	struct wpabuf *tmp;
	u8 *lpos;
	struct p2p_device *peer;
	size_t tmplen;
	int res;

	if (!p2p_group)
		return p2p_assoc_req_ie_wlan_ap(p2p, bssid, buf, len, p2p_ie);

	/*
	 * (Re)Association Request - P2P IE
	 * P2P Capability attribute (shall be present)
	 * Extended Listen Timing (may be present)
	 * P2P Device Info attribute (shall be present)
	 */
	tmp = wpabuf_alloc(200);
	if (tmp == NULL)
		return -1;

	peer = bssid ? p2p_get_device(p2p, bssid) : NULL;

	lpos = p2p_buf_add_ie_hdr(tmp);
	p2p_buf_add_capability(tmp, p2p->dev_capab, 0);
	if (p2p->ext_listen_interval)
		p2p_buf_add_ext_listen_timing(tmp, p2p->ext_listen_period,
					      p2p->ext_listen_interval);
	p2p_buf_add_device_info(tmp, p2p, peer);
	p2p_buf_update_ie_hdr(tmp, lpos);

	tmplen = wpabuf_len(tmp);
	if (tmplen > len)
		res = -1;
	else {
		os_memcpy(buf, wpabuf_head(tmp), tmplen);
		res = tmplen;
	}
	wpabuf_free(tmp);

	return res;
}


int p2p_scan_result_text(const u8 *ies, size_t ies_len, char *buf, char *end)
{
	struct wpabuf *p2p_ie;
	int ret;

	p2p_ie = ieee802_11_vendor_ie_concat(ies, ies_len, P2P_IE_VENDOR_TYPE);
	if (p2p_ie == NULL)
		return 0;

	ret = p2p_attr_text(p2p_ie, buf, end);
	wpabuf_free(p2p_ie);
	return ret;
}


static void p2p_clear_go_neg(struct p2p_data *p2p)
{
	p2p->go_neg_peer = NULL;
	p2p_clear_timeout(p2p);
	p2p_set_state(p2p, P2P_IDLE);
}


void p2p_wps_success_cb(struct p2p_data *p2p, const u8 *mac_addr)
{
	if (p2p->go_neg_peer == NULL) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No pending Group Formation - "
			"ignore WPS registration success notification");
		return; /* No pending Group Formation */
	}

	if (os_memcmp(mac_addr, p2p->go_neg_peer->intended_addr, ETH_ALEN) !=
	    0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Ignore WPS registration success notification "
			"for " MACSTR " (GO Negotiation peer " MACSTR ")",
			MAC2STR(mac_addr),
			MAC2STR(p2p->go_neg_peer->intended_addr));
		return; /* Ignore unexpected peer address */
	}

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Group Formation completed successfully with " MACSTR,
		MAC2STR(mac_addr));

	p2p_clear_go_neg(p2p);
}


void p2p_group_formation_failed(struct p2p_data *p2p)
{
	if (p2p->go_neg_peer == NULL) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No pending Group Formation - "
			"ignore group formation failure notification");
		return; /* No pending Group Formation */
	}

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Group Formation failed with " MACSTR,
		MAC2STR(p2p->go_neg_peer->intended_addr));

	p2p_clear_go_neg(p2p);
}


struct p2p_data * p2p_init(const struct p2p_config *cfg)
{
	struct p2p_data *p2p;

	if (cfg->max_peers < 1)
		return NULL;

	p2p = os_zalloc(sizeof(*p2p) + sizeof(*cfg));
	if (p2p == NULL)
		return NULL;
	p2p->cfg = (struct p2p_config *) (p2p + 1);
	os_memcpy(p2p->cfg, cfg, sizeof(*cfg));
	if (cfg->dev_name)
		p2p->cfg->dev_name = os_strdup(cfg->dev_name);
	if (cfg->manufacturer)
		p2p->cfg->manufacturer = os_strdup(cfg->manufacturer);
	if (cfg->model_name)
		p2p->cfg->model_name = os_strdup(cfg->model_name);
	if (cfg->model_number)
		p2p->cfg->model_number = os_strdup(cfg->model_number);
	if (cfg->serial_number)
		p2p->cfg->serial_number = os_strdup(cfg->serial_number);

	p2p->min_disc_int = 1;
	p2p->max_disc_int = 3;

	os_get_random(&p2p->next_tie_breaker, 1);
	p2p->next_tie_breaker &= 0x01;
	if (cfg->sd_request)
		p2p->dev_capab |= P2P_DEV_CAPAB_SERVICE_DISCOVERY;
	p2p->dev_capab |= P2P_DEV_CAPAB_INVITATION_PROCEDURE;
	if (cfg->concurrent_operations)
		p2p->dev_capab |= P2P_DEV_CAPAB_CONCURRENT_OPER;
	p2p->dev_capab |= P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY;

	dl_list_init(&p2p->devices);

	eloop_register_timeout(P2P_PEER_EXPIRATION_INTERVAL, 0,
			       p2p_expiration_timeout, p2p, NULL);

	return p2p;
}


void p2p_deinit(struct p2p_data *p2p)
{
	eloop_cancel_timeout(p2p_expiration_timeout, p2p, NULL);
	eloop_cancel_timeout(p2p_ext_listen_timeout, p2p, NULL);
	eloop_cancel_timeout(p2p_scan_timeout, p2p, NULL);
	p2p_flush(p2p);
	p2p_free_req_dev_types(p2p);
	os_free(p2p->cfg->dev_name);
	os_free(p2p->cfg->manufacturer);
	os_free(p2p->cfg->model_name);
	os_free(p2p->cfg->model_number);
	os_free(p2p->cfg->serial_number);
	os_free(p2p->groups);
	wpabuf_free(p2p->sd_resp);
	os_free(p2p->after_scan_tx);
	p2p_remove_wps_vendor_extensions(p2p);
	os_free(p2p);
}


void p2p_flush(struct p2p_data *p2p)
{
	struct p2p_device *dev, *prev;
	p2p_clear_timeout(p2p);
	p2p_set_state(p2p, P2P_IDLE);
	p2p->start_after_scan = P2P_AFTER_SCAN_NOTHING;
	p2p->go_neg_peer = NULL;
	eloop_cancel_timeout(p2p_find_timeout, p2p, NULL);
	dl_list_for_each_safe(dev, prev, &p2p->devices, struct p2p_device,
			      list) {
		dl_list_del(&dev->list);
		p2p_device_free(p2p, dev);
	}
	p2p_free_sd_queries(p2p);
	os_free(p2p->after_scan_tx);
	p2p->after_scan_tx = NULL;
}


int p2p_unauthorize(struct p2p_data *p2p, const u8 *addr)
{
	struct p2p_device *dev;

	dev = p2p_get_device(p2p, addr);
	if (dev == NULL)
		return -1;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Unauthorizing " MACSTR,
		MAC2STR(addr));

	if (p2p->go_neg_peer == dev)
		p2p->go_neg_peer = NULL;

	dev->wps_method = WPS_NOT_READY;
	dev->flags &= ~P2P_DEV_WAIT_GO_NEG_RESPONSE;
	dev->flags &= ~P2P_DEV_WAIT_GO_NEG_CONFIRM;

	/* Check if after_scan_tx is for this peer. If so free it */
	if (p2p->after_scan_tx &&
	    os_memcmp(addr, p2p->after_scan_tx->dst, ETH_ALEN) == 0) {
		os_free(p2p->after_scan_tx);
		p2p->after_scan_tx = NULL;
	}

	return 0;
}


int p2p_set_dev_name(struct p2p_data *p2p, const char *dev_name)
{
	os_free(p2p->cfg->dev_name);
	if (dev_name) {
		p2p->cfg->dev_name = os_strdup(dev_name);
		if (p2p->cfg->dev_name == NULL)
			return -1;
	} else
		p2p->cfg->dev_name = NULL;
	return 0;
}


int p2p_set_manufacturer(struct p2p_data *p2p, const char *manufacturer)
{
	os_free(p2p->cfg->manufacturer);
	p2p->cfg->manufacturer = NULL;
	if (manufacturer) {
		p2p->cfg->manufacturer = os_strdup(manufacturer);
		if (p2p->cfg->manufacturer == NULL)
			return -1;
	}

	return 0;
}


int p2p_set_model_name(struct p2p_data *p2p, const char *model_name)
{
	os_free(p2p->cfg->model_name);
	p2p->cfg->model_name = NULL;
	if (model_name) {
		p2p->cfg->model_name = os_strdup(model_name);
		if (p2p->cfg->model_name == NULL)
			return -1;
	}

	return 0;
}


int p2p_set_model_number(struct p2p_data *p2p, const char *model_number)
{
	os_free(p2p->cfg->model_number);
	p2p->cfg->model_number = NULL;
	if (model_number) {
		p2p->cfg->model_number = os_strdup(model_number);
		if (p2p->cfg->model_number == NULL)
			return -1;
	}

	return 0;
}


int p2p_set_serial_number(struct p2p_data *p2p, const char *serial_number)
{
	os_free(p2p->cfg->serial_number);
	p2p->cfg->serial_number = NULL;
	if (serial_number) {
		p2p->cfg->serial_number = os_strdup(serial_number);
		if (p2p->cfg->serial_number == NULL)
			return -1;
	}

	return 0;
}


void p2p_set_config_methods(struct p2p_data *p2p, u16 config_methods)
{
	p2p->cfg->config_methods = config_methods;
}


void p2p_set_uuid(struct p2p_data *p2p, const u8 *uuid)
{
	os_memcpy(p2p->cfg->uuid, uuid, 16);
}


int p2p_set_pri_dev_type(struct p2p_data *p2p, const u8 *pri_dev_type)
{
	os_memcpy(p2p->cfg->pri_dev_type, pri_dev_type, 8);
	return 0;
}


int p2p_set_sec_dev_types(struct p2p_data *p2p, const u8 dev_types[][8],
			  size_t num_dev_types)
{
	if (num_dev_types > P2P_SEC_DEVICE_TYPES)
		num_dev_types = P2P_SEC_DEVICE_TYPES;
	p2p->cfg->num_sec_dev_types = num_dev_types;
	os_memcpy(p2p->cfg->sec_dev_type, dev_types, num_dev_types * 8);
	return 0;
}


void p2p_remove_wps_vendor_extensions(struct p2p_data *p2p)
{
	int i;

	for (i = 0; i < P2P_MAX_WPS_VENDOR_EXT; i++) {
		wpabuf_free(p2p->wps_vendor_ext[i]);
		p2p->wps_vendor_ext[i] = NULL;
	}
}


int p2p_add_wps_vendor_extension(struct p2p_data *p2p,
				 const struct wpabuf *vendor_ext)
{
	int i;

	if (vendor_ext == NULL)
		return -1;

	for (i = 0; i < P2P_MAX_WPS_VENDOR_EXT; i++) {
		if (p2p->wps_vendor_ext[i] == NULL)
			break;
	}
	if (i >= P2P_MAX_WPS_VENDOR_EXT)
		return -1;

	p2p->wps_vendor_ext[i] = wpabuf_dup(vendor_ext);
	if (p2p->wps_vendor_ext[i] == NULL)
		return -1;

	return 0;
}


int p2p_set_country(struct p2p_data *p2p, const char *country)
{
	os_memcpy(p2p->cfg->country, country, 3);
	return 0;
}


void p2p_continue_find(struct p2p_data *p2p)
{
	struct p2p_device *dev;
	p2p_set_state(p2p, P2P_SEARCH);
	dl_list_for_each(dev, &p2p->devices, struct p2p_device, list) {
		if (dev->flags & P2P_DEV_SD_SCHEDULE) {
			if (p2p_start_sd(p2p, dev) == 0)
				return;
			else
				break;
		} else if (dev->req_config_methods &&
			   !(dev->flags & P2P_DEV_PD_FOR_JOIN)) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Send "
				"pending Provisioning Discovery Request to "
				MACSTR " (config methods 0x%x)",
				MAC2STR(dev->info.p2p_device_addr),
				dev->req_config_methods);
			if (p2p_send_prov_disc_req(p2p, dev, 0) == 0)
				return;
		}
	}

	p2p_listen_in_find(p2p);
}


static void p2p_sd_cb(struct p2p_data *p2p, int success)
{
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Service Discovery Query TX callback: success=%d",
		success);
	p2p->pending_action_state = P2P_NO_PENDING_ACTION;

	if (!success) {
		if (p2p->sd_peer) {
			p2p->sd_peer->flags &= ~P2P_DEV_SD_SCHEDULE;
			p2p->sd_peer = NULL;
		}
		p2p_continue_find(p2p);
		return;
	}

	if (p2p->sd_peer == NULL) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No SD peer entry known");
		p2p_continue_find(p2p);
		return;
	}

	/* Wait for response from the peer */
	p2p_set_state(p2p, P2P_SD_DURING_FIND);
	p2p_set_timeout(p2p, 0, 200000);
}

static void p2p_prov_disc_cb(struct p2p_data *p2p, int success)
{
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Provision Discovery Request TX callback: success=%d",
		success);
	p2p->pending_action_state = P2P_NO_PENDING_ACTION;

	if (!success) {
		if (p2p->state != P2P_IDLE)
			p2p_continue_find(p2p);
		return;
	}

	/* Wait for response from the peer */
	if (p2p->state == P2P_SEARCH)
		p2p_set_state(p2p, P2P_PD_DURING_FIND);
	p2p_set_timeout(p2p, 0, 200000);
}


int p2p_scan_res_handler(struct p2p_data *p2p, const u8 *bssid, int freq,
			 int level, const u8 *ies, size_t ies_len)
{
	p2p_add_device(p2p, bssid, freq, level, ies, ies_len);

	if (p2p->go_neg_peer && p2p->state == P2P_SEARCH &&
	    os_memcmp(p2p->go_neg_peer->info.p2p_device_addr, bssid, ETH_ALEN)
	    == 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Found GO Negotiation peer - try to start GO "
			"negotiation");
		p2p_connect_send(p2p, p2p->go_neg_peer);
		return 1;
	}

	return 0;
}


void p2p_scan_res_handled(struct p2p_data *p2p)
{
	if (!p2p->p2p_scan_running) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: p2p_scan was not "
			"running, but scan results received");
	}
	p2p->p2p_scan_running = 0;
	eloop_cancel_timeout(p2p_scan_timeout, p2p, NULL);

	if (p2p_run_after_scan(p2p))
		return;
	if (p2p->state == P2P_SEARCH)
		p2p_continue_find(p2p);
}


void p2p_scan_ie(struct p2p_data *p2p, struct wpabuf *ies)
{
	u8 *len = p2p_buf_add_ie_hdr(ies);
	p2p_buf_add_capability(ies, p2p->dev_capab, 0);
	if (p2p->cfg->reg_class && p2p->cfg->channel)
		p2p_buf_add_listen_channel(ies, p2p->cfg->country,
					   p2p->cfg->reg_class,
					   p2p->cfg->channel);
	if (p2p->ext_listen_interval)
		p2p_buf_add_ext_listen_timing(ies, p2p->ext_listen_period,
					      p2p->ext_listen_interval);
	/* TODO: p2p_buf_add_operating_channel() if GO */
	p2p_buf_update_ie_hdr(ies, len);
}


int p2p_ie_text(struct wpabuf *p2p_ie, char *buf, char *end)
{
	return p2p_attr_text(p2p_ie, buf, end);
}


static void p2p_go_neg_req_cb(struct p2p_data *p2p, int success)
{
	struct p2p_device *dev = p2p->go_neg_peer;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: GO Negotiation Request TX callback: success=%d",
		success);

	if (dev == NULL) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No pending GO Negotiation");
		return;
	}

	if (success) {
		dev->go_neg_req_sent++;
		if (dev->flags & P2P_DEV_USER_REJECTED) {
			p2p_set_state(p2p, P2P_IDLE);
			return;
		}
	}

	if (!success &&
	    (dev->info.dev_capab & P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY) &&
	    !is_zero_ether_addr(dev->member_in_go_dev)) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Peer " MACSTR " did not acknowledge request - "
			"try to use device discoverability through its GO",
			MAC2STR(dev->info.p2p_device_addr));
		p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
		p2p_send_dev_disc_req(p2p, dev);
		return;
	}

	/*
	 * Use P2P find, if needed, to find the other device from its listen
	 * channel.
	 */
	p2p_set_state(p2p, P2P_CONNECT);
	p2p_set_timeout(p2p, 0, 100000);
}


static void p2p_go_neg_resp_cb(struct p2p_data *p2p, int success)
{
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: GO Negotiation Response TX callback: success=%d",
		success);
	if (!p2p->go_neg_peer && p2p->state == P2P_PROVISIONING) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Ignore TX callback event - GO Negotiation is "
			"not running anymore");
		return;
	}
	p2p_set_state(p2p, P2P_CONNECT);
	p2p_set_timeout(p2p, 0, 100000);
}


static void p2p_go_neg_resp_failure_cb(struct p2p_data *p2p, int success)
{
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: GO Negotiation Response (failure) TX callback: "
		"success=%d", success);
	if (p2p->go_neg_peer && p2p->go_neg_peer->status != P2P_SC_SUCCESS) {
		p2p_go_neg_failed(p2p, p2p->go_neg_peer,
				  p2p->go_neg_peer->status);
	}
}


static void p2p_go_neg_conf_cb(struct p2p_data *p2p,
			       enum p2p_send_action_result result)
{
	struct p2p_device *dev;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: GO Negotiation Confirm TX callback: result=%d",
		result);
	p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
	if (result == P2P_SEND_ACTION_FAILED) {
		p2p_go_neg_failed(p2p, p2p->go_neg_peer, -1);
		return;
	}
	if (result == P2P_SEND_ACTION_NO_ACK) {
		/*
		 * It looks like the TX status for GO Negotiation Confirm is
		 * often showing failure even when the peer has actually
		 * received the frame. Since the peer may change channels
		 * immediately after having received the frame, we may not see
		 * an Ack for retries, so just dropping a single frame may
		 * trigger this. To allow the group formation to succeed if the
		 * peer did indeed receive the frame, continue regardless of
		 * the TX status.
		 */
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Assume GO Negotiation Confirm TX was actually "
			"received by the peer even though Ack was not "
			"reported");
	}

	dev = p2p->go_neg_peer;
	if (dev == NULL)
		return;

	p2p_go_complete(p2p, dev);
}


void p2p_send_action_cb(struct p2p_data *p2p, unsigned int freq, const u8 *dst,
			const u8 *src, const u8 *bssid,
			enum p2p_send_action_result result)
{
	enum p2p_pending_action_state state;
	int success;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Action frame TX callback (state=%d freq=%u dst=" MACSTR
		" src=" MACSTR " bssid=" MACSTR " result=%d",
		p2p->pending_action_state, freq, MAC2STR(dst), MAC2STR(src),
		MAC2STR(bssid), result);
	success = result == P2P_SEND_ACTION_SUCCESS;
	state = p2p->pending_action_state;
	p2p->pending_action_state = P2P_NO_PENDING_ACTION;
	switch (state) {
	case P2P_NO_PENDING_ACTION:
		break;
	case P2P_PENDING_GO_NEG_REQUEST:
		p2p_go_neg_req_cb(p2p, success);
		break;
	case P2P_PENDING_GO_NEG_RESPONSE:
		p2p_go_neg_resp_cb(p2p, success);
		break;
	case P2P_PENDING_GO_NEG_RESPONSE_FAILURE:
		p2p_go_neg_resp_failure_cb(p2p, success);
		break;
	case P2P_PENDING_GO_NEG_CONFIRM:
		p2p_go_neg_conf_cb(p2p, result);
		break;
	case P2P_PENDING_SD:
		p2p_sd_cb(p2p, success);
		break;
	case P2P_PENDING_PD:
		p2p_prov_disc_cb(p2p, success);
		break;
	case P2P_PENDING_INVITATION_REQUEST:
		p2p_invitation_req_cb(p2p, success);
		break;
	case P2P_PENDING_INVITATION_RESPONSE:
		p2p_invitation_resp_cb(p2p, success);
		break;
	case P2P_PENDING_DEV_DISC_REQUEST:
		p2p_dev_disc_req_cb(p2p, success);
		break;
	case P2P_PENDING_DEV_DISC_RESPONSE:
		p2p_dev_disc_resp_cb(p2p, success);
		break;
	case P2P_PENDING_GO_DISC_REQ:
		p2p_go_disc_req_cb(p2p, success);
		break;
	}
}


void p2p_listen_cb(struct p2p_data *p2p, unsigned int freq,
		   unsigned int duration)
{
	if (freq == p2p->pending_client_disc_freq) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Client discoverability remain-awake completed");
		p2p->pending_client_disc_freq = 0;
		return;
	}

	if (freq != p2p->pending_listen_freq) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unexpected listen callback for freq=%u "
			"duration=%u (pending_listen_freq=%u)",
			freq, duration, p2p->pending_listen_freq);
		return;
	}

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Starting Listen timeout(%u,%u) on freq=%u based on "
		"callback",
		p2p->pending_listen_sec, p2p->pending_listen_usec,
		p2p->pending_listen_freq);
	p2p->in_listen = 1;
	p2p->drv_in_listen = freq;
	if (p2p->pending_listen_sec || p2p->pending_listen_usec) {
		/*
		 * Add 20 msec extra wait to avoid race condition with driver
		 * remain-on-channel end event, i.e., give driver more time to
		 * complete the operation before our timeout expires.
		 */
		p2p_set_timeout(p2p, p2p->pending_listen_sec,
				p2p->pending_listen_usec + 20000);
	}

	p2p->pending_listen_freq = 0;
}


int p2p_listen_end(struct p2p_data *p2p, unsigned int freq)
{
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Driver ended Listen "
		"state (freq=%u)", freq);
	p2p->drv_in_listen = 0;
	if (p2p->in_listen)
		return 0; /* Internal timeout will trigger the next step */

	if (p2p->state == P2P_CONNECT_LISTEN && p2p->go_neg_peer) {
		if (p2p->go_neg_peer->connect_reqs >= 120) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Timeout on sending GO Negotiation "
				"Request without getting response");
			p2p_go_neg_failed(p2p, p2p->go_neg_peer, -1);
			return 0;
		}

		p2p_set_state(p2p, P2P_CONNECT);
		p2p_connect_send(p2p, p2p->go_neg_peer);
		return 1;
	} else if (p2p->state == P2P_SEARCH) {
		p2p_search(p2p);
		return 1;
	}

	return 0;
}


static void p2p_timeout_connect(struct p2p_data *p2p)
{
	p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
	p2p_set_state(p2p, P2P_CONNECT_LISTEN);
	p2p_listen_in_find(p2p);
}


static void p2p_timeout_connect_listen(struct p2p_data *p2p)
{
	if (p2p->go_neg_peer) {
		if (p2p->drv_in_listen) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Driver is "
				"still in Listen state; wait for it to "
				"complete");
			return;
		}

		if (p2p->go_neg_peer->connect_reqs >= 120) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Timeout on sending GO Negotiation "
				"Request without getting response");
			p2p_go_neg_failed(p2p, p2p->go_neg_peer, -1);
			return;
		}

		p2p_set_state(p2p, P2P_CONNECT);
		p2p_connect_send(p2p, p2p->go_neg_peer);
	} else
		p2p_set_state(p2p, P2P_IDLE);
}


static void p2p_timeout_wait_peer_connect(struct p2p_data *p2p)
{
	/*
	 * TODO: could remain constantly in Listen state for some time if there
	 * are no other concurrent uses for the radio. For now, go to listen
	 * state once per second to give other uses a chance to use the radio.
	 */
	p2p_set_state(p2p, P2P_WAIT_PEER_IDLE);
	p2p_set_timeout(p2p, 1, 0);
}


static void p2p_timeout_wait_peer_idle(struct p2p_data *p2p)
{
	struct p2p_device *dev = p2p->go_neg_peer;

	if (dev == NULL) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unknown GO Neg peer - stop GO Neg wait");
		return;
	}

	dev->wait_count++;
	if (dev->wait_count >= 120) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Timeout on waiting peer to become ready for GO "
			"Negotiation");
		p2p_go_neg_failed(p2p, dev, -1);
		return;
	}

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Go to Listen state while waiting for the peer to become "
		"ready for GO Negotiation");
	p2p_set_state(p2p, P2P_WAIT_PEER_CONNECT);
	p2p_listen_in_find(p2p);
}


static void p2p_timeout_sd_during_find(struct p2p_data *p2p)
{
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Service Discovery Query timeout");
	if (p2p->sd_peer) {
		p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
		p2p->sd_peer->flags &= ~P2P_DEV_SD_SCHEDULE;
		p2p->sd_peer = NULL;
	}
	p2p_continue_find(p2p);
}


static void p2p_timeout_prov_disc_during_find(struct p2p_data *p2p)
{
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Provision Discovery Request timeout");
	p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
	p2p_continue_find(p2p);
}


static void p2p_timeout_invite(struct p2p_data *p2p)
{
	p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
	p2p_set_state(p2p, P2P_INVITE_LISTEN);
	if (p2p->inv_role == P2P_INVITE_ROLE_ACTIVE_GO) {
		/*
		 * Better remain on operating channel instead of listen channel
		 * when running a group.
		 */
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Inviting in "
			"active GO role - wait on operating channel");
		p2p_set_timeout(p2p, 0, 100000);
		return;
	}
	p2p_listen_in_find(p2p);
}


static void p2p_timeout_invite_listen(struct p2p_data *p2p)
{
	if (p2p->invite_peer && p2p->invite_peer->invitation_reqs < 100) {
		p2p_set_state(p2p, P2P_INVITE);
		p2p_invite_send(p2p, p2p->invite_peer,
				p2p->invite_go_dev_addr);
	} else {
		if (p2p->invite_peer) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Invitation Request retry limit reached");
			if (p2p->cfg->invitation_result)
				p2p->cfg->invitation_result(
					p2p->cfg->cb_ctx, -1, NULL);
		}
		p2p_set_state(p2p, P2P_IDLE);
	}
}


static void p2p_state_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct p2p_data *p2p = eloop_ctx;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Timeout (state=%s)",
		p2p_state_txt(p2p->state));

	p2p->in_listen = 0;

	switch (p2p->state) {
	case P2P_IDLE:
		break;
	case P2P_SEARCH:
		p2p_search(p2p);
		break;
	case P2P_CONNECT:
		p2p_timeout_connect(p2p);
		break;
	case P2P_CONNECT_LISTEN:
		p2p_timeout_connect_listen(p2p);
		break;
	case P2P_GO_NEG:
		break;
	case P2P_LISTEN_ONLY:
		if (p2p->ext_listen_only) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Extended Listen Timing - Listen State "
				"completed");
			p2p->ext_listen_only = 0;
			p2p_set_state(p2p, P2P_IDLE);
		}
		break;
	case P2P_WAIT_PEER_CONNECT:
		p2p_timeout_wait_peer_connect(p2p);
		break;
	case P2P_WAIT_PEER_IDLE:
		p2p_timeout_wait_peer_idle(p2p);
		break;
	case P2P_SD_DURING_FIND:
		p2p_timeout_sd_during_find(p2p);
		break;
	case P2P_PROVISIONING:
		break;
	case P2P_PD_DURING_FIND:
		p2p_timeout_prov_disc_during_find(p2p);
		break;
	case P2P_INVITE:
		p2p_timeout_invite(p2p);
		break;
	case P2P_INVITE_LISTEN:
		p2p_timeout_invite_listen(p2p);
		break;
	}
}


int p2p_reject(struct p2p_data *p2p, const u8 *peer_addr)
{
	struct p2p_device *dev;

	dev = p2p_get_device(p2p, peer_addr);
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Local request to reject "
		"connection attempts by peer " MACSTR, MAC2STR(peer_addr));
	if (dev == NULL) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Peer " MACSTR
			" unknown", MAC2STR(peer_addr));
		return -1;
	}
	dev->status = P2P_SC_FAIL_REJECTED_BY_USER;
	dev->flags |= P2P_DEV_USER_REJECTED;
	return 0;
}


static const char * p2p_wps_method_text(enum p2p_wps_method method)
{
	switch (method) {
	case WPS_NOT_READY:
		return "not-ready";
	case WPS_PIN_LABEL:
		return "Label";
	case WPS_PIN_DISPLAY:
		return "Display";
	case WPS_PIN_KEYPAD:
		return "Keypad";
	case WPS_PBC:
		return "PBC";
	}

	return "??";
}


static const char * p2p_go_state_text(enum p2p_go_state go_state)
{
	switch (go_state) {
	case UNKNOWN_GO:
		return "unknown";
	case LOCAL_GO:
		return "local";
	case  REMOTE_GO:
		return "remote";
	}

	return "??";
}


int p2p_get_peer_info(struct p2p_data *p2p, const u8 *addr, int next,
		      char *buf, size_t buflen)
{
	struct p2p_device *dev;
	int res;
	char *pos, *end;
	struct os_time now;
	char devtype[WPS_DEV_TYPE_BUFSIZE];

	if (addr)
		dev = p2p_get_device(p2p, addr);
	else
		dev = dl_list_first(&p2p->devices, struct p2p_device, list);

	if (dev && next) {
		dev = dl_list_first(&dev->list, struct p2p_device, list);
		if (&dev->list == &p2p->devices)
			dev = NULL;
	}

	if (dev == NULL)
		return -1;

	pos = buf;
	end = buf + buflen;

	res = os_snprintf(pos, end - pos, MACSTR "\n",
			  MAC2STR(dev->info.p2p_device_addr));
	if (res < 0 || res >= end - pos)
		return pos - buf;
	pos += res;

	os_get_time(&now);
	res = os_snprintf(pos, end - pos,
			  "age=%d\n"
			  "listen_freq=%d\n"
			  "level=%d\n"
			  "wps_method=%s\n"
			  "interface_addr=" MACSTR "\n"
			  "member_in_go_dev=" MACSTR "\n"
			  "member_in_go_iface=" MACSTR "\n"
			  "pri_dev_type=%s\n"
			  "device_name=%s\n"
			  "manufacturer=%s\n"
			  "model_name=%s\n"
			  "model_number=%s\n"
			  "serial_number=%s\n"
			  "config_methods=0x%x\n"
			  "dev_capab=0x%x\n"
			  "group_capab=0x%x\n"
			  "go_neg_req_sent=%d\n"
			  "go_state=%s\n"
			  "dialog_token=%u\n"
			  "intended_addr=" MACSTR "\n"
			  "country=%c%c\n"
			  "oper_freq=%d\n"
			  "req_config_methods=0x%x\n"
			  "flags=%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n"
			  "status=%d\n"
			  "wait_count=%u\n"
			  "invitation_reqs=%u\n",
			  (int) (now.sec - dev->last_seen.sec),
			  dev->listen_freq,
			  dev->level,
			  p2p_wps_method_text(dev->wps_method),
			  MAC2STR(dev->interface_addr),
			  MAC2STR(dev->member_in_go_dev),
			  MAC2STR(dev->member_in_go_iface),
			  wps_dev_type_bin2str(dev->info.pri_dev_type,
					       devtype, sizeof(devtype)),
			  dev->info.device_name,
			  dev->info.manufacturer,
			  dev->info.model_name,
			  dev->info.model_number,
			  dev->info.serial_number,
			  dev->info.config_methods,
			  dev->info.dev_capab,
			  dev->info.group_capab,
			  dev->go_neg_req_sent,
			  p2p_go_state_text(dev->go_state),
			  dev->dialog_token,
			  MAC2STR(dev->intended_addr),
			  dev->country[0] ? dev->country[0] : '_',
			  dev->country[1] ? dev->country[1] : '_',
			  dev->oper_freq,
			  dev->req_config_methods,
			  dev->flags & P2P_DEV_PROBE_REQ_ONLY ?
			  "[PROBE_REQ_ONLY]" : "",
			  dev->flags & P2P_DEV_REPORTED ? "[REPORTED]" : "",
			  dev->flags & P2P_DEV_NOT_YET_READY ?
			  "[NOT_YET_READY]" : "",
			  dev->flags & P2P_DEV_SD_INFO ? "[SD_INFO]" : "",
			  dev->flags & P2P_DEV_SD_SCHEDULE ? "[SD_SCHEDULE]" :
			  "",
			  dev->flags & P2P_DEV_PD_PEER_DISPLAY ?
			  "[PD_PEER_DISPLAY]" : "",
			  dev->flags & P2P_DEV_PD_PEER_KEYPAD ?
			  "[PD_PEER_KEYPAD]" : "",
			  dev->flags & P2P_DEV_USER_REJECTED ?
			  "[USER_REJECTED]" : "",
			  dev->flags & P2P_DEV_PEER_WAITING_RESPONSE ?
			  "[PEER_WAITING_RESPONSE]" : "",
			  dev->flags & P2P_DEV_PREFER_PERSISTENT_GROUP ?
			  "[PREFER_PERSISTENT_GROUP]" : "",
			  dev->flags & P2P_DEV_WAIT_GO_NEG_RESPONSE ?
			  "[WAIT_GO_NEG_RESPONSE]" : "",
			  dev->flags & P2P_DEV_WAIT_GO_NEG_CONFIRM ?
			  "[WAIT_GO_NEG_CONFIRM]" : "",
			  dev->flags & P2P_DEV_GROUP_CLIENT_ONLY ?
			  "[GROUP_CLIENT_ONLY]" : "",
			  dev->flags & P2P_DEV_FORCE_FREQ ?
			  "[FORCE_FREQ]" : "",
			  dev->flags & P2P_DEV_PD_FOR_JOIN ?
			  "[PD_FOR_JOIN]" : "",
			  dev->status,
			  dev->wait_count,
			  dev->invitation_reqs);
	if (res < 0 || res >= end - pos)
		return pos - buf;
	pos += res;

	if (dev->ext_listen_period) {
		res = os_snprintf(pos, end - pos,
				  "ext_listen_period=%u\n"
				  "ext_listen_interval=%u\n",
				  dev->ext_listen_period,
				  dev->ext_listen_interval);
		if (res < 0 || res >= end - pos)
			return pos - buf;
		pos += res;
	}

	if (dev->oper_ssid_len) {
		res = os_snprintf(pos, end - pos,
				  "oper_ssid=%s\n",
				  wpa_ssid_txt(dev->oper_ssid,
					       dev->oper_ssid_len));
		if (res < 0 || res >= end - pos)
			return pos - buf;
		pos += res;
	}

	return pos - buf;
}


void p2p_set_client_discoverability(struct p2p_data *p2p, int enabled)
{
	if (enabled) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Client "
			"discoverability enabled");
		p2p->dev_capab |= P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY;
	} else {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Client "
			"discoverability disabled");
		p2p->dev_capab &= ~P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY;
	}
}


static struct wpabuf * p2p_build_presence_req(u32 duration1, u32 interval1,
					      u32 duration2, u32 interval2)
{
	struct wpabuf *req;
	struct p2p_noa_desc desc1, desc2, *ptr1 = NULL, *ptr2 = NULL;
	u8 *len;

	req = wpabuf_alloc(100);
	if (req == NULL)
		return NULL;

	if (duration1 || interval1) {
		os_memset(&desc1, 0, sizeof(desc1));
		desc1.count_type = 1;
		desc1.duration = duration1;
		desc1.interval = interval1;
		ptr1 = &desc1;

		if (duration2 || interval2) {
			os_memset(&desc2, 0, sizeof(desc2));
			desc2.count_type = 2;
			desc2.duration = duration2;
			desc2.interval = interval2;
			ptr2 = &desc2;
		}
	}

	p2p_buf_add_action_hdr(req, P2P_PRESENCE_REQ, 1);
	len = p2p_buf_add_ie_hdr(req);
	p2p_buf_add_noa(req, 0, 0, 0, ptr1, ptr2);
	p2p_buf_update_ie_hdr(req, len);

	return req;
}


int p2p_presence_req(struct p2p_data *p2p, const u8 *go_interface_addr,
		     const u8 *own_interface_addr, unsigned int freq,
		     u32 duration1, u32 interval1, u32 duration2,
		     u32 interval2)
{
	struct wpabuf *req;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Send Presence Request to "
		"GO " MACSTR " (own interface " MACSTR ") freq=%u dur1=%u "
		"int1=%u dur2=%u int2=%u",
		MAC2STR(go_interface_addr), MAC2STR(own_interface_addr),
		freq, duration1, interval1, duration2, interval2);

	req = p2p_build_presence_req(duration1, interval1, duration2,
				     interval2);
	if (req == NULL)
		return -1;

	p2p->pending_action_state = P2P_NO_PENDING_ACTION;
	if (p2p_send_action(p2p, freq, go_interface_addr, own_interface_addr,
			    go_interface_addr,
			    wpabuf_head(req), wpabuf_len(req), 200) < 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Failed to send Action frame");
	}
	wpabuf_free(req);

	return 0;
}


static struct wpabuf * p2p_build_presence_resp(u8 status, const u8 *noa,
					       size_t noa_len, u8 dialog_token)
{
	struct wpabuf *resp;
	u8 *len;

	resp = wpabuf_alloc(100 + noa_len);
	if (resp == NULL)
		return NULL;

	p2p_buf_add_action_hdr(resp, P2P_PRESENCE_RESP, dialog_token);
	len = p2p_buf_add_ie_hdr(resp);
	p2p_buf_add_status(resp, status);
	if (noa) {
		wpabuf_put_u8(resp, P2P_ATTR_NOTICE_OF_ABSENCE);
		wpabuf_put_le16(resp, noa_len);
		wpabuf_put_data(resp, noa, noa_len);
	} else
		p2p_buf_add_noa(resp, 0, 0, 0, NULL, NULL);
	p2p_buf_update_ie_hdr(resp, len);

	return resp;
}


static void p2p_process_presence_req(struct p2p_data *p2p, const u8 *da,
				     const u8 *sa, const u8 *data, size_t len,
				     int rx_freq)
{
	struct p2p_message msg;
	u8 status;
	struct wpabuf *resp;
	size_t g;
	struct p2p_group *group = NULL;
	int parsed = 0;
	u8 noa[50];
	int noa_len;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Received P2P Action - P2P Presence Request");

	for (g = 0; g < p2p->num_groups; g++) {
		if (os_memcmp(da, p2p_group_get_interface_addr(p2p->groups[g]),
			      ETH_ALEN) == 0) {
			group = p2p->groups[g];
			break;
		}
	}
	if (group == NULL) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Ignore P2P Presence Request for unknown group "
			MACSTR, MAC2STR(da));
		return;
	}

	if (p2p_parse(data, len, &msg) < 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Failed to parse P2P Presence Request");
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
	}
	parsed = 1;

	if (msg.noa == NULL) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No NoA attribute in P2P Presence Request");
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
	}

	status = p2p_group_presence_req(group, sa, msg.noa, msg.noa_len);

fail:
	if (p2p->cfg->get_noa)
		noa_len = p2p->cfg->get_noa(p2p->cfg->cb_ctx, da, noa,
					    sizeof(noa));
	else
		noa_len = -1;
	resp = p2p_build_presence_resp(status, noa_len > 0 ? noa : NULL,
				       noa_len > 0 ? noa_len : 0,
				       msg.dialog_token);
	if (parsed)
		p2p_parse_free(&msg);
	if (resp == NULL)
		return;

	p2p->pending_action_state = P2P_NO_PENDING_ACTION;
	if (p2p_send_action(p2p, rx_freq, sa, da, da,
			    wpabuf_head(resp), wpabuf_len(resp), 200) < 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Failed to send Action frame");
	}
	wpabuf_free(resp);
}


static void p2p_process_presence_resp(struct p2p_data *p2p, const u8 *da,
				      const u8 *sa, const u8 *data, size_t len)
{
	struct p2p_message msg;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Received P2P Action - P2P Presence Response");

	if (p2p_parse(data, len, &msg) < 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Failed to parse P2P Presence Response");
		return;
	}

	if (msg.status == NULL || msg.noa == NULL) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No Status or NoA attribute in P2P Presence "
			"Response");
		p2p_parse_free(&msg);
		return;
	}

	if (*msg.status) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: P2P Presence Request was rejected: status %u",
			*msg.status);
		p2p_parse_free(&msg);
		return;
	}

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: P2P Presence Request was accepted");
	wpa_hexdump(MSG_DEBUG, "P2P: P2P Presence Response - NoA",
		    msg.noa, msg.noa_len);
	/* TODO: process NoA */
	p2p_parse_free(&msg);
}


static void p2p_ext_listen_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct p2p_data *p2p = eloop_ctx;

	if (p2p->ext_listen_interval) {
		/* Schedule next extended listen timeout */
		eloop_register_timeout(p2p->ext_listen_interval_sec,
				       p2p->ext_listen_interval_usec,
				       p2p_ext_listen_timeout, p2p, NULL);
	}

	if (p2p->state == P2P_LISTEN_ONLY && p2p->ext_listen_only) {
		/*
		 * This should not really happen, but it looks like the Listen
		 * command may fail is something else (e.g., a scan) was
		 * running at an inconvenient time. As a workaround, allow new
		 * Extended Listen operation to be started.
		 */
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Previous "
			"Extended Listen operation had not been completed - "
			"try again");
		p2p->ext_listen_only = 0;
		p2p_set_state(p2p, P2P_IDLE);
	}

	if (p2p->state != P2P_IDLE) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Skip Extended "
			"Listen timeout in active state (%s)",
			p2p_state_txt(p2p->state));
		return;
	}

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Extended Listen timeout");
	p2p->ext_listen_only = 1;
	if (p2p_listen(p2p, p2p->ext_listen_period) < 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Failed to start "
			"Listen state for Extended Listen Timing");
		p2p->ext_listen_only = 0;
	}
}


int p2p_ext_listen(struct p2p_data *p2p, unsigned int period,
		   unsigned int interval)
{
	if (period > 65535 || interval > 65535 || period > interval ||
	    (period == 0 && interval > 0) || (period > 0 && interval == 0)) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Invalid Extended Listen Timing request: "
			"period=%u interval=%u", period, interval);
		return -1;
	}

	eloop_cancel_timeout(p2p_ext_listen_timeout, p2p, NULL);

	if (interval == 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Disabling Extended Listen Timing");
		p2p->ext_listen_period = 0;
		p2p->ext_listen_interval = 0;
		return 0;
	}

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Enabling Extended Listen Timing: period %u msec, "
		"interval %u msec", period, interval);
	p2p->ext_listen_period = period;
	p2p->ext_listen_interval = interval;
	p2p->ext_listen_interval_sec = interval / 1000;
	p2p->ext_listen_interval_usec = (interval % 1000) * 1000;

	eloop_register_timeout(p2p->ext_listen_interval_sec,
			       p2p->ext_listen_interval_usec,
			       p2p_ext_listen_timeout, p2p, NULL);

	return 0;
}


void p2p_deauth_notif(struct p2p_data *p2p, const u8 *bssid, u16 reason_code,
		      const u8 *ie, size_t ie_len)
{
	struct p2p_message msg;

	if (bssid == NULL || ie == NULL)
		return;

	os_memset(&msg, 0, sizeof(msg));
	if (p2p_parse_ies(ie, ie_len, &msg))
		return;
	if (msg.minor_reason_code == NULL)
		return;

	wpa_msg(p2p->cfg->msg_ctx, MSG_INFO,
		"P2P: Deauthentication notification BSSID " MACSTR
		" reason_code=%u minor_reason_code=%u",
		MAC2STR(bssid), reason_code, *msg.minor_reason_code);

	p2p_parse_free(&msg);
}


void p2p_disassoc_notif(struct p2p_data *p2p, const u8 *bssid, u16 reason_code,
			const u8 *ie, size_t ie_len)
{
	struct p2p_message msg;

	if (bssid == NULL || ie == NULL)
		return;

	os_memset(&msg, 0, sizeof(msg));
	if (p2p_parse_ies(ie, ie_len, &msg))
		return;
	if (msg.minor_reason_code == NULL)
		return;

	wpa_msg(p2p->cfg->msg_ctx, MSG_INFO,
		"P2P: Disassociation notification BSSID " MACSTR
		" reason_code=%u minor_reason_code=%u",
		MAC2STR(bssid), reason_code, *msg.minor_reason_code);

	p2p_parse_free(&msg);
}


void p2p_set_managed_oper(struct p2p_data *p2p, int enabled)
{
	if (enabled) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Managed P2P "
			"Device operations enabled");
		p2p->dev_capab |= P2P_DEV_CAPAB_INFRA_MANAGED;
	} else {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Managed P2P "
			"Device operations disabled");
		p2p->dev_capab &= ~P2P_DEV_CAPAB_INFRA_MANAGED;
	}
}


int p2p_set_listen_channel(struct p2p_data *p2p, u8 reg_class, u8 channel)
{
	if (p2p_channel_to_freq(p2p->cfg->country, reg_class, channel) < 0)
		return -1;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Set Listen channel: "
		"reg_class %u channel %u", reg_class, channel);
	p2p->cfg->reg_class = reg_class;
	p2p->cfg->channel = channel;

	return 0;
}


int p2p_set_ssid_postfix(struct p2p_data *p2p, const u8 *postfix, size_t len)
{
	wpa_hexdump_ascii(MSG_DEBUG, "P2P: New SSID postfix", postfix, len);
	if (postfix == NULL) {
		p2p->cfg->ssid_postfix_len = 0;
		return 0;
	}
	if (len > sizeof(p2p->cfg->ssid_postfix))
		return -1;
	os_memcpy(p2p->cfg->ssid_postfix, postfix, len);
	p2p->cfg->ssid_postfix_len = len;
	return 0;
}


int p2p_get_interface_addr(struct p2p_data *p2p, const u8 *dev_addr,
			   u8 *iface_addr)
{
	struct p2p_device *dev = p2p_get_device(p2p, dev_addr);
	if (dev == NULL || is_zero_ether_addr(dev->interface_addr))
		return -1;
	os_memcpy(iface_addr, dev->interface_addr, ETH_ALEN);
	return 0;
}


int p2p_get_dev_addr(struct p2p_data *p2p, const u8 *iface_addr,
			   u8 *dev_addr)
{
	struct p2p_device *dev = p2p_get_device_interface(p2p, iface_addr);
	if (dev == NULL)
		return -1;
	os_memcpy(dev_addr, dev->info.p2p_device_addr, ETH_ALEN);
	return 0;
}


void p2p_set_peer_filter(struct p2p_data *p2p, const u8 *addr)
{
	os_memcpy(p2p->peer_filter, addr, ETH_ALEN);
	if (is_zero_ether_addr(p2p->peer_filter))
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Disable peer "
			"filter");
	else
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Enable peer "
			"filter for " MACSTR, MAC2STR(p2p->peer_filter));
}


void p2p_set_cross_connect(struct p2p_data *p2p, int enabled)
{
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Cross connection %s",
		enabled ? "enabled" : "disabled");
	if (p2p->cross_connect == enabled)
		return;
	p2p->cross_connect = enabled;
	/* TODO: may need to tear down any action group where we are GO(?) */
}


int p2p_get_oper_freq(struct p2p_data *p2p, const u8 *iface_addr)
{
	struct p2p_device *dev = p2p_get_device_interface(p2p, iface_addr);
	if (dev == NULL)
		return -1;
	if (dev->oper_freq <= 0)
		return -1;
	return dev->oper_freq;
}


void p2p_set_intra_bss_dist(struct p2p_data *p2p, int enabled)
{
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Intra BSS distribution %s",
		enabled ? "enabled" : "disabled");
	p2p->cfg->p2p_intra_bss = enabled;
}


void p2p_update_channel_list(struct p2p_data *p2p, struct p2p_channels *chan)
{
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Update channel list");
	os_memcpy(&p2p->cfg->channels, chan, sizeof(struct p2p_channels));
}


int p2p_send_action(struct p2p_data *p2p, unsigned int freq, const u8 *dst,
		    const u8 *src, const u8 *bssid, const u8 *buf,
		    size_t len, unsigned int wait_time)
{
	if (p2p->p2p_scan_running) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Delay Action "
			"frame TX until p2p_scan completes");
		if (p2p->after_scan_tx) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Dropped "
				"previous pending Action frame TX");
			os_free(p2p->after_scan_tx);
		}
		p2p->after_scan_tx = os_malloc(sizeof(*p2p->after_scan_tx) +
					       len);
		if (p2p->after_scan_tx == NULL)
			return -1;
		p2p->after_scan_tx->freq = freq;
		os_memcpy(p2p->after_scan_tx->dst, dst, ETH_ALEN);
		os_memcpy(p2p->after_scan_tx->src, src, ETH_ALEN);
		os_memcpy(p2p->after_scan_tx->bssid, bssid, ETH_ALEN);
		p2p->after_scan_tx->len = len;
		p2p->after_scan_tx->wait_time = wait_time;
		os_memcpy(p2p->after_scan_tx + 1, buf, len);
		return 0;
	}

	return p2p->cfg->send_action(p2p->cfg->cb_ctx, freq, dst, src, bssid,
				     buf, len, wait_time);
}


void p2p_set_best_channels(struct p2p_data *p2p, int freq_24, int freq_5,
			   int freq_overall)
{
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Best channel: 2.4 GHz: %d,"
		"  5 GHz: %d,  overall: %d", freq_24, freq_5, freq_overall);
	p2p->best_freq_24 = freq_24;
	p2p->best_freq_5 = freq_5;
	p2p->best_freq_overall = freq_overall;
}


const u8 * p2p_get_go_neg_peer(struct p2p_data *p2p)
{
	if (p2p == NULL || p2p->go_neg_peer == NULL)
		return NULL;
	return p2p->go_neg_peer->info.p2p_device_addr;
}


const struct p2p_peer_info *
p2p_get_peer_found(struct p2p_data *p2p, const u8 *addr, int next)
{
	struct p2p_device *dev;

	if (addr) {
		dev = p2p_get_device(p2p, addr);
		if (!dev)
			return NULL;

		if (!next) {
			if (dev->flags & P2P_DEV_PROBE_REQ_ONLY)
				return NULL;

			return &dev->info;
		} else {
			do {
				dev = dl_list_first(&dev->list,
						    struct p2p_device,
						    list);
				if (&dev->list == &p2p->devices)
					return NULL;
			} while (dev->flags & P2P_DEV_PROBE_REQ_ONLY);
		}
	} else {
		dev = dl_list_first(&p2p->devices, struct p2p_device, list);
		if (!dev)
			return NULL;
		while (dev->flags & P2P_DEV_PROBE_REQ_ONLY) {
			dev = dl_list_first(&dev->list,
					    struct p2p_device,
					    list);
			if (&dev->list == &p2p->devices)
				return NULL;
		}
	}

	return &dev->info;
}
