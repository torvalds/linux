/*
 * Wi-Fi Direct - P2P module
 * Copyright (c) 2009-2010, Atheros Communications
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "eloop.h"
#include "common/defs.h"
#include "common/ieee802_11_defs.h"
#include "common/ieee802_11_common.h"
#include "common/wpa_ctrl.h"
#include "crypto/sha256.h"
#include "crypto/crypto.h"
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
#ifndef P2P_PEER_EXPIRATION_AGE
#define P2P_PEER_EXPIRATION_AGE 60
#endif /* P2P_PEER_EXPIRATION_AGE */


void p2p_expire_peers(struct p2p_data *p2p)
{
	struct p2p_device *dev, *n;
	struct os_reltime now;
	size_t i;

	os_get_reltime(&now);
	dl_list_for_each_safe(dev, n, &p2p->devices, struct p2p_device, list) {
		if (dev->last_seen.sec + P2P_PEER_EXPIRATION_AGE >= now.sec)
			continue;

		if (dev == p2p->go_neg_peer) {
			/*
			 * GO Negotiation is in progress with the peer, so
			 * don't expire the peer entry until GO Negotiation
			 * fails or times out.
			 */
			continue;
		}

		if (p2p->cfg->go_connected &&
		    p2p->cfg->go_connected(p2p->cfg->cb_ctx,
					   dev->info.p2p_device_addr)) {
			/*
			 * We are connected as a client to a group in which the
			 * peer is the GO, so do not expire the peer entry.
			 */
			os_get_reltime(&dev->last_seen);
			continue;
		}

		for (i = 0; i < p2p->num_groups; i++) {
			if (p2p_group_is_client_connected(
				    p2p->groups[i], dev->info.p2p_device_addr))
				break;
		}
		if (i < p2p->num_groups) {
			/*
			 * The peer is connected as a client in a group where
			 * we are the GO, so do not expire the peer entry.
			 */
			os_get_reltime(&dev->last_seen);
			continue;
		}

		p2p_dbg(p2p, "Expiring old peer entry " MACSTR,
			MAC2STR(dev->info.p2p_device_addr));
		dl_list_del(&dev->list);
		p2p_device_free(p2p, dev);
	}
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


const char * p2p_get_state_txt(struct p2p_data *p2p)
{
	return p2p_state_txt(p2p->state);
}


struct p2ps_advertisement * p2p_get_p2ps_adv_list(struct p2p_data *p2p)
{
	return p2p ? p2p->p2ps_adv_list : NULL;
}


void p2p_set_intended_addr(struct p2p_data *p2p, const u8 *intended_addr)
{
	if (p2p && intended_addr)
		os_memcpy(p2p->intended_addr, intended_addr, ETH_ALEN);
}


u16 p2p_get_provisioning_info(struct p2p_data *p2p, const u8 *addr)
{
	struct p2p_device *dev = NULL;

	if (!addr || !p2p)
		return 0;

	dev = p2p_get_device(p2p, addr);
	if (dev)
		return dev->wps_prov_info;
	else
		return 0;
}


void p2p_clear_provisioning_info(struct p2p_data *p2p, const u8 *addr)
{
	struct p2p_device *dev = NULL;

	if (!addr || !p2p)
		return;

	dev = p2p_get_device(p2p, addr);
	if (dev)
		dev->wps_prov_info = 0;
}


void p2p_set_state(struct p2p_data *p2p, int new_state)
{
	p2p_dbg(p2p, "State %s -> %s",
		p2p_state_txt(p2p->state), p2p_state_txt(new_state));
	p2p->state = new_state;

	if (new_state == P2P_IDLE && p2p->pending_channel) {
		p2p_dbg(p2p, "Apply change in listen channel");
		p2p->cfg->reg_class = p2p->pending_reg_class;
		p2p->cfg->channel = p2p->pending_channel;
		p2p->pending_reg_class = 0;
		p2p->pending_channel = 0;
	}
}


void p2p_set_timeout(struct p2p_data *p2p, unsigned int sec, unsigned int usec)
{
	p2p_dbg(p2p, "Set timeout (state=%s): %u.%06u sec",
		p2p_state_txt(p2p->state), sec, usec);
	eloop_cancel_timeout(p2p_state_timeout, p2p, NULL);
	eloop_register_timeout(sec, usec, p2p_state_timeout, p2p, NULL);
}


void p2p_clear_timeout(struct p2p_data *p2p)
{
	p2p_dbg(p2p, "Clear timeout (state=%s)", p2p_state_txt(p2p->state));
	eloop_cancel_timeout(p2p_state_timeout, p2p, NULL);
}


void p2p_go_neg_failed(struct p2p_data *p2p, int status)
{
	struct p2p_go_neg_results res;
	struct p2p_device *peer = p2p->go_neg_peer;

	if (!peer)
		return;

	eloop_cancel_timeout(p2p_go_neg_wait_timeout, p2p, NULL);
	if (p2p->state != P2P_SEARCH) {
		/*
		 * Clear timeouts related to GO Negotiation if no new p2p_find
		 * has been started.
		 */
		p2p_clear_timeout(p2p);
		p2p_set_state(p2p, P2P_IDLE);
	}

	peer->flags &= ~P2P_DEV_PEER_WAITING_RESPONSE;
	peer->wps_method = WPS_NOT_READY;
	peer->oob_pw_id = 0;
	wpabuf_free(peer->go_neg_conf);
	peer->go_neg_conf = NULL;
	p2p->go_neg_peer = NULL;

	os_memset(&res, 0, sizeof(res));
	res.status = status;
	os_memcpy(res.peer_device_addr, peer->info.p2p_device_addr, ETH_ALEN);
	os_memcpy(res.peer_interface_addr, peer->intended_addr, ETH_ALEN);
	p2p->cfg->go_neg_completed(p2p->cfg->cb_ctx, &res);
}


static void p2p_listen_in_find(struct p2p_data *p2p, int dev_disc)
{
	unsigned int r, tu;
	int freq;
	struct wpabuf *ies;

	p2p_dbg(p2p, "Starting short listen state (state=%s)",
		p2p_state_txt(p2p->state));

	if (p2p->pending_listen_freq) {
		/* We have a pending p2p_listen request */
		p2p_dbg(p2p, "p2p_listen command pending already");
		return;
	}

	freq = p2p_channel_to_freq(p2p->cfg->reg_class, p2p->cfg->channel);
	if (freq < 0) {
		p2p_dbg(p2p, "Unknown regulatory class/channel");
		return;
	}

	if (os_get_random((u8 *) &r, sizeof(r)) < 0)
		r = 0;
	tu = (r % ((p2p->max_disc_int - p2p->min_disc_int) + 1) +
	      p2p->min_disc_int) * 100;
	if (p2p->max_disc_tu >= 0 && tu > (unsigned int) p2p->max_disc_tu)
		tu = p2p->max_disc_tu;
	if (!dev_disc && tu < 100)
		tu = 100; /* Need to wait in non-device discovery use cases */
	if (p2p->cfg->max_listen && 1024 * tu / 1000 > p2p->cfg->max_listen)
		tu = p2p->cfg->max_listen * 1000 / 1024;

	if (tu == 0) {
		p2p_dbg(p2p, "Skip listen state since duration was 0 TU");
		p2p_set_timeout(p2p, 0, 0);
		return;
	}

	ies = p2p_build_probe_resp_ies(p2p, NULL, 0);
	if (ies == NULL)
		return;

	p2p->pending_listen_freq = freq;
	p2p->pending_listen_sec = 0;
	p2p->pending_listen_usec = 1024 * tu;

	if (p2p->cfg->start_listen(p2p->cfg->cb_ctx, freq, 1024 * tu / 1000,
		    ies) < 0) {
		p2p_dbg(p2p, "Failed to start listen mode");
		p2p->pending_listen_freq = 0;
	}
	wpabuf_free(ies);
}


int p2p_listen(struct p2p_data *p2p, unsigned int timeout)
{
	int freq;
	struct wpabuf *ies;

	p2p_dbg(p2p, "Going to listen(only) state");

	if (p2p->pending_listen_freq) {
		/* We have a pending p2p_listen request */
		p2p_dbg(p2p, "p2p_listen command pending already");
		return -1;
	}

	freq = p2p_channel_to_freq(p2p->cfg->reg_class, p2p->cfg->channel);
	if (freq < 0) {
		p2p_dbg(p2p, "Unknown regulatory class/channel");
		return -1;
	}

	p2p->pending_listen_sec = timeout / 1000;
	p2p->pending_listen_usec = (timeout % 1000) * 1000;

	if (p2p->p2p_scan_running) {
		if (p2p->start_after_scan == P2P_AFTER_SCAN_CONNECT) {
			p2p_dbg(p2p, "p2p_scan running - connect is already pending - skip listen");
			return 0;
		}
		p2p_dbg(p2p, "p2p_scan running - delay start of listen state");
		p2p->start_after_scan = P2P_AFTER_SCAN_LISTEN;
		return 0;
	}

	ies = p2p_build_probe_resp_ies(p2p, NULL, 0);
	if (ies == NULL)
		return -1;

	p2p->pending_listen_freq = freq;

	if (p2p->cfg->start_listen(p2p->cfg->cb_ctx, freq, timeout, ies) < 0) {
		p2p_dbg(p2p, "Failed to start listen mode");
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
	dl_list_for_each(dev, &p2p->devices, struct p2p_device, list) {
		dev->flags &= ~P2P_DEV_REPORTED;
		dev->sd_reqs = 0;
	}
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
		    os_reltime_before(&dev->last_seen, &oldest->last_seen))
			oldest = dev;
	}
	if (count + 1 > p2p->cfg->max_peers && oldest) {
		p2p_dbg(p2p, "Remove oldest peer entry to make room for a new peer");
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
	p2p_copy_filter_devname(dev->info.device_name,
				sizeof(dev->info.device_name),
				cli->dev_name, cli->dev_name_len);
	dev->info.dev_capab = cli->dev_capab;
	dev->info.config_methods = cli->config_methods;
	os_memcpy(dev->info.pri_dev_type, cli->pri_dev_type, 8);
	dev->info.wps_sec_dev_type_list_len = 8 * cli->num_sec_dev_types;
	os_memcpy(dev->info.wps_sec_dev_type_list, cli->sec_dev_types,
		  dev->info.wps_sec_dev_type_list_len);
}


static int p2p_add_group_clients(struct p2p_data *p2p, const u8 *go_dev_addr,
				 const u8 *go_interface_addr, int freq,
				 const u8 *gi, size_t gi_len,
				 struct os_reltime *rx_time)
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
		if (os_memcmp(dev->member_in_go_iface, go_interface_addr,
			      ETH_ALEN) == 0) {
			os_memset(dev->member_in_go_iface, 0, ETH_ALEN);
			os_memset(dev->member_in_go_dev, 0, ETH_ALEN);
		}
	}

	for (c = 0; c < info.num_clients; c++) {
		struct p2p_client_info *cli = &info.client[c];
		if (os_memcmp(cli->p2p_device_addr, p2p->cfg->dev_addr,
			      ETH_ALEN) == 0)
			continue; /* ignore our own entry */
		dev = p2p_get_device(p2p, cli->p2p_device_addr);
		if (dev) {
			if (dev->flags & (P2P_DEV_GROUP_CLIENT_ONLY |
					  P2P_DEV_PROBE_REQ_ONLY)) {
				/*
				 * Update information since we have not
				 * received this directly from the client.
				 */
				p2p_copy_client_info(dev, cli);
			} else {
				/*
				 * Need to update P2P Client Discoverability
				 * flag since it is valid only in P2P Group
				 * Info attribute.
				 */
				dev->info.dev_capab &=
					~P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY;
				dev->info.dev_capab |=
					cli->dev_capab &
					P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY;
			}
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
		os_memcpy(&dev->last_seen, rx_time, sizeof(struct os_reltime));
		os_memcpy(dev->member_in_go_dev, go_dev_addr, ETH_ALEN);
		os_memcpy(dev->member_in_go_iface, go_interface_addr,
			  ETH_ALEN);
		dev->flags |= P2P_DEV_LAST_SEEN_AS_GROUP_CLIENT;
	}

	return 0;
}


static void p2p_copy_wps_info(struct p2p_data *p2p, struct p2p_device *dev,
			      int probe_req, const struct p2p_message *msg)
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
		/*
		 * P2P Client Discoverability bit is reserved in all frames
		 * that use this function, so do not change its value here.
		 */
		dev->info.dev_capab &= P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY;
		dev->info.dev_capab |= msg->capability[0] &
			~P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY;
		dev->info.group_capab = msg->capability[1];
	}

	if (msg->ext_listen_timing) {
		dev->ext_listen_period = WPA_GET_LE16(msg->ext_listen_timing);
		dev->ext_listen_interval =
			WPA_GET_LE16(msg->ext_listen_timing + 2);
	}

	if (!probe_req) {
		u16 new_config_methods;
		new_config_methods = msg->config_methods ?
			msg->config_methods : msg->wps_config_methods;
		if (new_config_methods &&
		    dev->info.config_methods != new_config_methods) {
			p2p_dbg(p2p, "Update peer " MACSTR
				" config_methods 0x%x -> 0x%x",
				MAC2STR(dev->info.p2p_device_addr),
				dev->info.config_methods,
				new_config_methods);
			dev->info.config_methods = new_config_methods;
		}
	}
}


static void p2p_update_peer_vendor_elems(struct p2p_device *dev, const u8 *ies,
					 size_t ies_len)
{
	const u8 *pos, *end;
	u8 id, len;

	wpabuf_free(dev->info.vendor_elems);
	dev->info.vendor_elems = NULL;

	end = ies + ies_len;

	for (pos = ies; end - pos > 1; pos += len) {
		id = *pos++;
		len = *pos++;

		if (len > end - pos)
			break;

		if (id != WLAN_EID_VENDOR_SPECIFIC || len < 3)
			continue;

		if (len >= 4) {
			u32 type = WPA_GET_BE32(pos);

			if (type == WPA_IE_VENDOR_TYPE ||
			    type == WMM_IE_VENDOR_TYPE ||
			    type == WPS_IE_VENDOR_TYPE ||
			    type == P2P_IE_VENDOR_TYPE ||
			    type == WFD_IE_VENDOR_TYPE)
				continue;
		}

		/* Unknown vendor element - make raw IE data available */
		if (wpabuf_resize(&dev->info.vendor_elems, 2 + len) < 0)
			break;
		wpabuf_put_data(dev->info.vendor_elems, pos - 2, 2 + len);
	}
}


static int p2p_compare_wfd_info(struct p2p_device *dev,
			      const struct p2p_message *msg)
{
	if (dev->info.wfd_subelems && msg->wfd_subelems) {
		if (dev->info.wfd_subelems->used != msg->wfd_subelems->used)
			return 1;

		return os_memcmp(dev->info.wfd_subelems->buf,
				 msg->wfd_subelems->buf,
				 dev->info.wfd_subelems->used);
	}
	if (dev->info.wfd_subelems || msg->wfd_subelems)
		return 1;

	return 0;
}


/**
 * p2p_add_device - Add peer entries based on scan results or P2P frames
 * @p2p: P2P module context from p2p_init()
 * @addr: Source address of Beacon or Probe Response frame (may be either
 *	P2P Device Address or P2P Interface Address)
 * @level: Signal level (signal strength of the received frame from the peer)
 * @freq: Frequency on which the Beacon or Probe Response frame was received
 * @rx_time: Time when the result was received
 * @ies: IEs from the Beacon or Probe Response frame
 * @ies_len: Length of ies buffer in octets
 * @scan_res: Whether this was based on scan results
 * Returns: 0 on success, -1 on failure
 *
 * If the scan result is for a GO, the clients in the group will also be added
 * to the peer table. This function can also be used with some other frames
 * like Provision Discovery Request that contains P2P Capability and P2P Device
 * Info attributes.
 */
int p2p_add_device(struct p2p_data *p2p, const u8 *addr, int freq,
		   struct os_reltime *rx_time, int level, const u8 *ies,
		   size_t ies_len, int scan_res)
{
	struct p2p_device *dev;
	struct p2p_message msg;
	const u8 *p2p_dev_addr;
	int wfd_changed;
	int dev_name_changed;
	int i;
	struct os_reltime time_now;

	os_memset(&msg, 0, sizeof(msg));
	if (p2p_parse_ies(ies, ies_len, &msg)) {
		p2p_dbg(p2p, "Failed to parse P2P IE for a device entry");
		p2p_parse_free(&msg);
		return -1;
	}

	if (msg.p2p_device_addr)
		p2p_dev_addr = msg.p2p_device_addr;
	else if (msg.device_id)
		p2p_dev_addr = msg.device_id;
	else {
		p2p_dbg(p2p, "Ignore scan data without P2P Device Info or P2P Device Id");
		p2p_parse_free(&msg);
		return -1;
	}

	if (!is_zero_ether_addr(p2p->peer_filter) &&
	    os_memcmp(p2p_dev_addr, p2p->peer_filter, ETH_ALEN) != 0) {
		p2p_dbg(p2p, "Do not add peer filter for " MACSTR
			" due to peer filter", MAC2STR(p2p_dev_addr));
		p2p_parse_free(&msg);
		return 0;
	}

	dev = p2p_create_device(p2p, p2p_dev_addr);
	if (dev == NULL) {
		p2p_parse_free(&msg);
		return -1;
	}

	if (rx_time == NULL) {
		os_get_reltime(&time_now);
		rx_time = &time_now;
	}

	/*
	 * Update the device entry only if the new peer
	 * entry is newer than the one previously stored, or if
	 * the device was previously seen as a P2P Client in a group
	 * and the new entry isn't older than a threshold.
	 */
	if (dev->last_seen.sec > 0 &&
	    os_reltime_before(rx_time, &dev->last_seen) &&
	    (!(dev->flags & P2P_DEV_LAST_SEEN_AS_GROUP_CLIENT) ||
	     os_reltime_expired(&dev->last_seen, rx_time,
				P2P_DEV_GROUP_CLIENT_RESP_THRESHOLD))) {
		p2p_dbg(p2p,
			"Do not update peer entry based on old frame (rx_time=%u.%06u last_seen=%u.%06u flags=0x%x)",
			(unsigned int) rx_time->sec,
			(unsigned int) rx_time->usec,
			(unsigned int) dev->last_seen.sec,
			(unsigned int) dev->last_seen.usec,
			dev->flags);
		p2p_parse_free(&msg);
		return -1;
	}

	os_memcpy(&dev->last_seen, rx_time, sizeof(struct os_reltime));

	dev->flags &= ~(P2P_DEV_PROBE_REQ_ONLY | P2P_DEV_GROUP_CLIENT_ONLY |
			P2P_DEV_LAST_SEEN_AS_GROUP_CLIENT);

	if (os_memcmp(addr, p2p_dev_addr, ETH_ALEN) != 0)
		os_memcpy(dev->interface_addr, addr, ETH_ALEN);
	if (msg.ssid &&
	    msg.ssid[1] <= sizeof(dev->oper_ssid) &&
	    (msg.ssid[1] != P2P_WILDCARD_SSID_LEN ||
	     os_memcmp(msg.ssid + 2, P2P_WILDCARD_SSID, P2P_WILDCARD_SSID_LEN)
	     != 0)) {
		os_memcpy(dev->oper_ssid, msg.ssid + 2, msg.ssid[1]);
		dev->oper_ssid_len = msg.ssid[1];
	}

	wpabuf_free(dev->info.p2ps_instance);
	dev->info.p2ps_instance = NULL;
	if (msg.adv_service_instance && msg.adv_service_instance_len)
		dev->info.p2ps_instance = wpabuf_alloc_copy(
			msg.adv_service_instance, msg.adv_service_instance_len);

	if (freq >= 2412 && freq <= 2484 && msg.ds_params &&
	    *msg.ds_params >= 1 && *msg.ds_params <= 14) {
		int ds_freq;
		if (*msg.ds_params == 14)
			ds_freq = 2484;
		else
			ds_freq = 2407 + *msg.ds_params * 5;
		if (freq != ds_freq) {
			p2p_dbg(p2p, "Update Listen frequency based on DS Parameter Set IE: %d -> %d MHz",
				freq, ds_freq);
			freq = ds_freq;
		}
	}

	if (dev->listen_freq && dev->listen_freq != freq && scan_res) {
		p2p_dbg(p2p, "Update Listen frequency based on scan results ("
			MACSTR " %d -> %d MHz (DS param %d)",
			MAC2STR(dev->info.p2p_device_addr), dev->listen_freq,
			freq, msg.ds_params ? *msg.ds_params : -1);
	}
	if (scan_res) {
		dev->listen_freq = freq;
		if (msg.group_info)
			dev->oper_freq = freq;
	}
	dev->info.level = level;

	dev_name_changed = os_strncmp(dev->info.device_name, msg.device_name,
				      WPS_DEV_NAME_MAX_LEN) != 0;

	p2p_copy_wps_info(p2p, dev, 0, &msg);

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

	wfd_changed = p2p_compare_wfd_info(dev, &msg);

	if (wfd_changed) {
		wpabuf_free(dev->info.wfd_subelems);
		if (msg.wfd_subelems)
			dev->info.wfd_subelems = wpabuf_dup(msg.wfd_subelems);
		else
			dev->info.wfd_subelems = NULL;
	}

	if (scan_res) {
		p2p_add_group_clients(p2p, p2p_dev_addr, addr, freq,
				      msg.group_info, msg.group_info_len,
				      rx_time);
	}

	p2p_parse_free(&msg);

	p2p_update_peer_vendor_elems(dev, ies, ies_len);

	if (dev->flags & P2P_DEV_REPORTED && !wfd_changed &&
	    !dev_name_changed &&
	    (!msg.adv_service_instance ||
	     (dev->flags & P2P_DEV_P2PS_REPORTED)))
		return 0;

	p2p_dbg(p2p, "Peer found with Listen frequency %d MHz (rx_time=%u.%06u)",
		freq, (unsigned int) rx_time->sec,
		(unsigned int) rx_time->usec);
	if (dev->flags & P2P_DEV_USER_REJECTED) {
		p2p_dbg(p2p, "Do not report rejected device");
		return 0;
	}

	if (dev->info.config_methods == 0 &&
	    (freq == 2412 || freq == 2437 || freq == 2462)) {
		/*
		 * If we have only seen a Beacon frame from a GO, we do not yet
		 * know what WPS config methods it supports. Since some
		 * applications use config_methods value from P2P-DEVICE-FOUND
		 * events, postpone reporting this peer until we've fully
		 * discovered its capabilities.
		 *
		 * At least for now, do this only if the peer was detected on
		 * one of the social channels since that peer can be easily be
		 * found again and there are no limitations of having to use
		 * passive scan on this channels, so this can be done through
		 * Probe Response frame that includes the config_methods
		 * information.
		 */
		p2p_dbg(p2p, "Do not report peer " MACSTR
			" with unknown config methods", MAC2STR(addr));
		return 0;
	}

	p2p->cfg->dev_found(p2p->cfg->cb_ctx, addr, &dev->info,
			    !(dev->flags & P2P_DEV_REPORTED_ONCE));
	dev->flags |= P2P_DEV_REPORTED | P2P_DEV_REPORTED_ONCE;

	if (msg.adv_service_instance)
		dev->flags |= P2P_DEV_P2PS_REPORTED;

	return 0;
}


static void p2p_device_free(struct p2p_data *p2p, struct p2p_device *dev)
{
	int i;

	if (p2p->go_neg_peer == dev) {
		/*
		 * If GO Negotiation is in progress, report that it has failed.
		 */
		p2p_go_neg_failed(p2p, -1);
	}
	if (p2p->invite_peer == dev)
		p2p->invite_peer = NULL;
	if (p2p->sd_peer == dev)
		p2p->sd_peer = NULL;
	if (p2p->pending_client_disc_go == dev)
		p2p->pending_client_disc_go = NULL;

	/* dev_lost() device, but only if it was previously dev_found() */
	if (dev->flags & P2P_DEV_REPORTED_ONCE)
		p2p->cfg->dev_lost(p2p->cfg->cb_ctx,
				   dev->info.p2p_device_addr);

	for (i = 0; i < P2P_MAX_WPS_VENDOR_EXT; i++) {
		wpabuf_free(dev->info.wps_vendor_ext[i]);
		dev->info.wps_vendor_ext[i] = NULL;
	}

	wpabuf_free(dev->info.wfd_subelems);
	wpabuf_free(dev->info.vendor_elems);
	wpabuf_free(dev->go_neg_conf);
	wpabuf_free(dev->info.p2ps_instance);

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

	freq = p2p_channel_to_freq(reg_class, channel);
	p2p_dbg(p2p, "Next progressive search channel: reg_class %u channel %u -> %d MHz",
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
	u16 pw_id = DEV_PW_DEFAULT;
	int res;

	if (p2p->drv_in_listen) {
		p2p_dbg(p2p, "Driver is still in Listen state - wait for it to end before continuing");
		return;
	}
	p2p->cfg->stop_listen(p2p->cfg->cb_ctx);

	if (p2p->find_pending_full &&
	    (p2p->find_type == P2P_FIND_PROGRESSIVE ||
	     p2p->find_type == P2P_FIND_START_WITH_FULL)) {
		type = P2P_SCAN_FULL;
		p2p_dbg(p2p, "Starting search (pending full scan)");
		p2p->find_pending_full = 0;
	} else if ((p2p->find_type == P2P_FIND_PROGRESSIVE &&
	    (freq = p2p_get_next_prog_freq(p2p)) > 0) ||
	    (p2p->find_type == P2P_FIND_START_WITH_FULL &&
	     (freq = p2p->find_specified_freq) > 0)) {
		type = P2P_SCAN_SOCIAL_PLUS_ONE;
		p2p_dbg(p2p, "Starting search (+ freq %u)", freq);
	} else {
		type = P2P_SCAN_SOCIAL;
		p2p_dbg(p2p, "Starting search");
	}

	res = p2p->cfg->p2p_scan(p2p->cfg->cb_ctx, type, freq,
				 p2p->num_req_dev_types, p2p->req_dev_types,
				 p2p->find_dev_id, pw_id);
	if (res < 0) {
		p2p_dbg(p2p, "Scan request schedule failed");
		p2p_continue_find(p2p);
	}
}


static void p2p_find_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct p2p_data *p2p = eloop_ctx;
	p2p_dbg(p2p, "Find timeout -> stop");
	p2p_stop_find(p2p);
}


void p2p_notify_scan_trigger_status(struct p2p_data *p2p, int status)
{
	if (status != 0) {
		p2p_dbg(p2p, "Scan request failed");
		/* Do continue find even for the first p2p_find_scan */
		p2p_continue_find(p2p);
	} else {
		p2p_dbg(p2p, "Running p2p_scan");
		p2p->p2p_scan_running = 1;
		eloop_cancel_timeout(p2p_scan_timeout, p2p, NULL);
		eloop_register_timeout(P2P_SCAN_TIMEOUT, 0, p2p_scan_timeout,
				       p2p, NULL);
	}
}


static int p2p_run_after_scan(struct p2p_data *p2p)
{
	struct p2p_device *dev;
	enum p2p_after_scan op;

	if (p2p->after_scan_tx) {
		p2p->after_scan_tx_in_progress = 1;
		p2p_dbg(p2p, "Send pending Action frame at p2p_scan completion");
		p2p->cfg->send_action(p2p->cfg->cb_ctx,
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
		p2p_dbg(p2p, "Start previously requested Listen state");
		p2p_listen(p2p, p2p->pending_listen_sec * 1000 +
			   p2p->pending_listen_usec / 1000);
		return 1;
	case P2P_AFTER_SCAN_CONNECT:
		p2p_dbg(p2p, "Start previously requested connect with " MACSTR,
			MAC2STR(p2p->after_scan_peer));
		dev = p2p_get_device(p2p, p2p->after_scan_peer);
		if (dev == NULL) {
			p2p_dbg(p2p, "Peer not known anymore");
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
	p2p_dbg(p2p, "p2p_scan timeout (running=%d)", p2p->p2p_scan_running);
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


static int p2ps_gen_hash(struct p2p_data *p2p, const char *str, u8 *hash)
{
	u8 buf[SHA256_MAC_LEN];
	char str_buf[256];
	const u8 *adv_array;
	size_t i, adv_len;

	if (!str || !hash)
		return 0;

	if (!str[0]) {
		os_memcpy(hash, p2p->wild_card_hash, P2PS_HASH_LEN);
		return 1;
	}

	adv_array = (u8 *) str_buf;
	adv_len = os_strlen(str);
	if (adv_len >= sizeof(str_buf))
		return 0;

	for (i = 0; i < adv_len; i++) {
		if (str[i] >= 'A' && str[i] <= 'Z')
			str_buf[i] = str[i] - 'A' + 'a';
		else
			str_buf[i] = str[i];
	}

	if (sha256_vector(1, &adv_array, &adv_len, buf))
		return 0;

	os_memcpy(hash, buf, P2PS_HASH_LEN);
	return 1;
}


int p2p_find(struct p2p_data *p2p, unsigned int timeout,
	     enum p2p_discovery_type type,
	     unsigned int num_req_dev_types, const u8 *req_dev_types,
	     const u8 *dev_id, unsigned int search_delay,
	     u8 seek_count, const char **seek, int freq)
{
	int res;

	p2p_dbg(p2p, "Starting find (type=%d)", type);
	os_get_reltime(&p2p->find_start);
	if (p2p->p2p_scan_running) {
		p2p_dbg(p2p, "p2p_scan is already running");
	}

	p2p_free_req_dev_types(p2p);
	if (req_dev_types && num_req_dev_types) {
		p2p->req_dev_types = os_memdup(req_dev_types,
					       num_req_dev_types *
					       WPS_DEV_TYPE_LEN);
		if (p2p->req_dev_types == NULL)
			return -1;
		p2p->num_req_dev_types = num_req_dev_types;
	}

	if (dev_id) {
		os_memcpy(p2p->find_dev_id_buf, dev_id, ETH_ALEN);
		p2p->find_dev_id = p2p->find_dev_id_buf;
	} else
		p2p->find_dev_id = NULL;

	if (seek_count == 0 || !seek) {
		/* Not an ASP search */
		p2p->p2ps_seek = 0;
	} else if (seek_count == 1 && seek && (!seek[0] || !seek[0][0])) {
		/*
		 * An empty seek string means no hash values, but still an ASP
		 * search.
		 */
		p2p_dbg(p2p, "ASP search");
		p2p->p2ps_seek_count = 0;
		p2p->p2ps_seek = 1;
	} else if (seek && seek_count <= P2P_MAX_QUERY_HASH) {
		u8 buf[P2PS_HASH_LEN];
		int i, count = 0;

		for (i = 0; i < seek_count; i++) {
			if (!p2ps_gen_hash(p2p, seek[i], buf))
				continue;

			p2p_dbg(p2p, "Seek service %s hash " MACSTR,
				seek[i], MAC2STR(buf));
			os_memcpy(&p2p->p2ps_seek_hash[count * P2PS_HASH_LEN],
				  buf, P2PS_HASH_LEN);
			count++;
		}

		p2p->p2ps_seek_count = count;
		p2p->p2ps_seek = 1;
	} else {
		p2p->p2ps_seek_count = 0;
		p2p->p2ps_seek = 1;
	}

	/* Special case to perform wildcard search */
	if (p2p->p2ps_seek_count == 0 && p2p->p2ps_seek) {
		p2p->p2ps_seek_count = 1;
		os_memcpy(&p2p->p2ps_seek_hash, p2p->wild_card_hash,
			  P2PS_HASH_LEN);
	}

	p2p->start_after_scan = P2P_AFTER_SCAN_NOTHING;
	p2p_clear_timeout(p2p);
	if (p2p->pending_listen_freq) {
		p2p_dbg(p2p, "Clear pending_listen_freq for p2p_find");
		p2p->pending_listen_freq = 0;
	}
	p2p->cfg->stop_listen(p2p->cfg->cb_ctx);
	p2p->find_pending_full = 0;
	p2p->find_type = type;
	if (freq != 2412 && freq != 2437 && freq != 2462 && freq != 60480)
		p2p->find_specified_freq = freq;
	else
		p2p->find_specified_freq = 0;
	p2p_device_clear_reported(p2p);
	os_memset(p2p->sd_query_no_ack, 0, ETH_ALEN);
	p2p_set_state(p2p, P2P_SEARCH);
	p2p->search_delay = search_delay;
	p2p->in_search_delay = 0;
	eloop_cancel_timeout(p2p_find_timeout, p2p, NULL);
	p2p->last_p2p_find_timeout = timeout;
	if (timeout)
		eloop_register_timeout(timeout, 0, p2p_find_timeout,
				       p2p, NULL);
	switch (type) {
	case P2P_FIND_START_WITH_FULL:
		if (freq > 0) {
			/*
			 * Start with the specified channel and then move to
			 * scans for social channels and this specific channel.
			 */
			res = p2p->cfg->p2p_scan(p2p->cfg->cb_ctx,
						 P2P_SCAN_SPECIFIC, freq,
						 p2p->num_req_dev_types,
						 p2p->req_dev_types, dev_id,
						 DEV_PW_DEFAULT);
			break;
		}
		/* fall through */
	case P2P_FIND_PROGRESSIVE:
		res = p2p->cfg->p2p_scan(p2p->cfg->cb_ctx, P2P_SCAN_FULL, 0,
					 p2p->num_req_dev_types,
					 p2p->req_dev_types, dev_id,
					 DEV_PW_DEFAULT);
		break;
	case P2P_FIND_ONLY_SOCIAL:
		res = p2p->cfg->p2p_scan(p2p->cfg->cb_ctx, P2P_SCAN_SOCIAL, 0,
					 p2p->num_req_dev_types,
					 p2p->req_dev_types, dev_id,
					 DEV_PW_DEFAULT);
		break;
	default:
		return -1;
	}

	if (res != 0 && p2p->p2p_scan_running) {
		p2p_dbg(p2p, "Failed to start p2p_scan - another p2p_scan was already running");
		/* wait for the previous p2p_scan to complete */
		if (type == P2P_FIND_PROGRESSIVE ||
		    (type == P2P_FIND_START_WITH_FULL && freq == 0))
			p2p->find_pending_full = 1;
		res = 0; /* do not report failure */
	} else if (res != 0) {
		p2p_dbg(p2p, "Failed to start p2p_scan");
		p2p_set_state(p2p, P2P_IDLE);
		eloop_cancel_timeout(p2p_find_timeout, p2p, NULL);
	}

	return res;
}


void p2p_stop_find_for_freq(struct p2p_data *p2p, int freq)
{
	p2p_dbg(p2p, "Stopping find");
	eloop_cancel_timeout(p2p_find_timeout, p2p, NULL);
	p2p_clear_timeout(p2p);
	if (p2p->state == P2P_SEARCH || p2p->state == P2P_SD_DURING_FIND)
		p2p->cfg->find_stopped(p2p->cfg->cb_ctx);

	p2p->p2ps_seek_count = 0;

	p2p_set_state(p2p, P2P_IDLE);
	p2p_free_req_dev_types(p2p);
	p2p->start_after_scan = P2P_AFTER_SCAN_NOTHING;
	if (p2p->go_neg_peer)
		p2p->go_neg_peer->flags &= ~P2P_DEV_PEER_WAITING_RESPONSE;
	p2p->go_neg_peer = NULL;
	p2p->sd_peer = NULL;
	p2p->invite_peer = NULL;
	p2p_stop_listen_for_freq(p2p, freq);
	p2p->send_action_in_progress = 0;
}


void p2p_stop_listen_for_freq(struct p2p_data *p2p, int freq)
{
	if (freq > 0 && p2p->drv_in_listen == freq && p2p->in_listen) {
		p2p_dbg(p2p, "Skip stop_listen since we are on correct channel for response");
		return;
	}
	if (p2p->in_listen) {
		p2p->in_listen = 0;
		p2p_clear_timeout(p2p);
	}
	if (p2p->drv_in_listen) {
		/*
		 * The driver may not deliver callback to p2p_listen_end()
		 * when the operation gets canceled, so clear the internal
		 * variable that is tracking driver state.
		 */
		p2p_dbg(p2p, "Clear drv_in_listen (%d)", p2p->drv_in_listen);
		p2p->drv_in_listen = 0;
	}
	p2p->cfg->stop_listen(p2p->cfg->cb_ctx);
}


void p2p_stop_listen(struct p2p_data *p2p)
{
	if (p2p->state != P2P_LISTEN_ONLY) {
		p2p_dbg(p2p, "Skip stop_listen since not in listen_only state.");
		return;
	}

	p2p_stop_listen_for_freq(p2p, 0);
	p2p_set_state(p2p, P2P_IDLE);
}


void p2p_stop_find(struct p2p_data *p2p)
{
	p2p->pending_listen_freq = 0;
	p2p_stop_find_for_freq(p2p, 0);
}


static int p2p_prepare_channel_pref(struct p2p_data *p2p,
				    unsigned int force_freq,
				    unsigned int pref_freq, int go)
{
	u8 op_class, op_channel;
	unsigned int freq = force_freq ? force_freq : pref_freq;

	p2p_dbg(p2p, "Prepare channel pref - force_freq=%u pref_freq=%u go=%d",
		force_freq, pref_freq, go);
	if (p2p_freq_to_channel(freq, &op_class, &op_channel) < 0) {
		p2p_dbg(p2p, "Unsupported frequency %u MHz", freq);
		return -1;
	}

	if (!p2p_channels_includes(&p2p->cfg->channels, op_class, op_channel) &&
	    (go || !p2p_channels_includes(&p2p->cfg->cli_channels, op_class,
					  op_channel))) {
		p2p_dbg(p2p, "Frequency %u MHz (oper_class %u channel %u) not allowed for P2P",
			freq, op_class, op_channel);
		return -1;
	}

	p2p->op_reg_class = op_class;
	p2p->op_channel = op_channel;

	if (force_freq) {
		p2p->channels.reg_classes = 1;
		p2p->channels.reg_class[0].channels = 1;
		p2p->channels.reg_class[0].reg_class = p2p->op_reg_class;
		p2p->channels.reg_class[0].channel[0] = p2p->op_channel;
	} else {
		os_memcpy(&p2p->channels, &p2p->cfg->channels,
			  sizeof(struct p2p_channels));
	}

	return 0;
}


static void p2p_prepare_channel_best(struct p2p_data *p2p)
{
	u8 op_class, op_channel;
	const int op_classes_5ghz[] = { 124, 125, 115, 0 };
	const int op_classes_ht40[] = { 126, 127, 116, 117, 0 };
	const int op_classes_vht[] = { 128, 0 };

	p2p_dbg(p2p, "Prepare channel best");

	if (!p2p->cfg->cfg_op_channel && p2p->best_freq_overall > 0 &&
	    p2p_supported_freq(p2p, p2p->best_freq_overall) &&
	    p2p_freq_to_channel(p2p->best_freq_overall, &op_class, &op_channel)
	    == 0) {
		p2p_dbg(p2p, "Select best overall channel as operating channel preference");
		p2p->op_reg_class = op_class;
		p2p->op_channel = op_channel;
	} else if (!p2p->cfg->cfg_op_channel && p2p->best_freq_5 > 0 &&
		   p2p_supported_freq(p2p, p2p->best_freq_5) &&
		   p2p_freq_to_channel(p2p->best_freq_5, &op_class, &op_channel)
		   == 0) {
		p2p_dbg(p2p, "Select best 5 GHz channel as operating channel preference");
		p2p->op_reg_class = op_class;
		p2p->op_channel = op_channel;
	} else if (!p2p->cfg->cfg_op_channel && p2p->best_freq_24 > 0 &&
		   p2p_supported_freq(p2p, p2p->best_freq_24) &&
		   p2p_freq_to_channel(p2p->best_freq_24, &op_class,
				       &op_channel) == 0) {
		p2p_dbg(p2p, "Select best 2.4 GHz channel as operating channel preference");
		p2p->op_reg_class = op_class;
		p2p->op_channel = op_channel;
	} else if (p2p->cfg->num_pref_chan > 0 &&
		   p2p_channels_includes(&p2p->cfg->channels,
					 p2p->cfg->pref_chan[0].op_class,
					 p2p->cfg->pref_chan[0].chan)) {
		p2p_dbg(p2p, "Select first pref_chan entry as operating channel preference");
		p2p->op_reg_class = p2p->cfg->pref_chan[0].op_class;
		p2p->op_channel = p2p->cfg->pref_chan[0].chan;
	} else if (p2p_channel_select(&p2p->cfg->channels, op_classes_vht,
				      &p2p->op_reg_class, &p2p->op_channel) ==
		   0) {
		p2p_dbg(p2p, "Select possible VHT channel (op_class %u channel %u) as operating channel preference",
			p2p->op_reg_class, p2p->op_channel);
	} else if (p2p_channel_select(&p2p->cfg->channels, op_classes_ht40,
				      &p2p->op_reg_class, &p2p->op_channel) ==
		   0) {
		p2p_dbg(p2p, "Select possible HT40 channel (op_class %u channel %u) as operating channel preference",
			p2p->op_reg_class, p2p->op_channel);
	} else if (p2p_channel_select(&p2p->cfg->channels, op_classes_5ghz,
				      &p2p->op_reg_class, &p2p->op_channel) ==
		   0) {
		p2p_dbg(p2p, "Select possible 5 GHz channel (op_class %u channel %u) as operating channel preference",
			p2p->op_reg_class, p2p->op_channel);
	} else if (p2p_channels_includes(&p2p->cfg->channels,
					 p2p->cfg->op_reg_class,
					 p2p->cfg->op_channel)) {
		p2p_dbg(p2p, "Select pre-configured channel as operating channel preference");
		p2p->op_reg_class = p2p->cfg->op_reg_class;
		p2p->op_channel = p2p->cfg->op_channel;
	} else if (p2p_channel_random_social(&p2p->cfg->channels,
					     &p2p->op_reg_class,
					     &p2p->op_channel) == 0) {
		p2p_dbg(p2p, "Select random available social channel (op_class %u channel %u) as operating channel preference",
			p2p->op_reg_class, p2p->op_channel);
	} else {
		/* Select any random available channel from the first available
		 * operating class */
		p2p_channel_select(&p2p->cfg->channels, NULL,
				   &p2p->op_reg_class,
				   &p2p->op_channel);
		p2p_dbg(p2p, "Select random available channel %d from operating class %d as operating channel preference",
			p2p->op_channel, p2p->op_reg_class);
	}

	os_memcpy(&p2p->channels, &p2p->cfg->channels,
		  sizeof(struct p2p_channels));
}


/**
 * p2p_prepare_channel - Select operating channel for GO Negotiation or P2PS PD
 * @p2p: P2P module context from p2p_init()
 * @dev: Selected peer device
 * @force_freq: Forced frequency in MHz or 0 if not forced
 * @pref_freq: Preferred frequency in MHz or 0 if no preference
 * @go: Whether the local end will be forced to be GO
 * Returns: 0 on success, -1 on failure (channel not supported for P2P)
 *
 * This function is used to do initial operating channel selection for GO
 * Negotiation prior to having received peer information or for P2PS PD
 * signalling. The selected channel may be further optimized in
 * p2p_reselect_channel() once the peer information is available.
 */
int p2p_prepare_channel(struct p2p_data *p2p, struct p2p_device *dev,
			unsigned int force_freq, unsigned int pref_freq, int go)
{
	p2p_dbg(p2p, "Prepare channel - force_freq=%u pref_freq=%u go=%d",
		force_freq, pref_freq, go);
	if (force_freq || pref_freq) {
		if (p2p_prepare_channel_pref(p2p, force_freq, pref_freq, go) <
		    0)
			return -1;
	} else {
		p2p_prepare_channel_best(p2p);
	}
	p2p_channels_dump(p2p, "prepared channels", &p2p->channels);
	if (go)
		p2p_channels_remove_freqs(&p2p->channels, &p2p->no_go_freq);
	else if (!force_freq)
		p2p_channels_union_inplace(&p2p->channels,
					   &p2p->cfg->cli_channels);
	p2p_channels_dump(p2p, "after go/cli filter/add", &p2p->channels);

	p2p_dbg(p2p, "Own preference for operation channel: Operating Class %u Channel %u%s",
		p2p->op_reg_class, p2p->op_channel,
		force_freq ? " (forced)" : "");

	if (force_freq)
		dev->flags |= P2P_DEV_FORCE_FREQ;
	else
		dev->flags &= ~P2P_DEV_FORCE_FREQ;

	return 0;
}


static void p2p_set_dev_persistent(struct p2p_device *dev,
				   int persistent_group)
{
	switch (persistent_group) {
	case 0:
		dev->flags &= ~(P2P_DEV_PREFER_PERSISTENT_GROUP |
				P2P_DEV_PREFER_PERSISTENT_RECONN);
		break;
	case 1:
		dev->flags |= P2P_DEV_PREFER_PERSISTENT_GROUP;
		dev->flags &= ~P2P_DEV_PREFER_PERSISTENT_RECONN;
		break;
	case 2:
		dev->flags |= P2P_DEV_PREFER_PERSISTENT_GROUP |
			P2P_DEV_PREFER_PERSISTENT_RECONN;
		break;
	}
}


int p2p_connect(struct p2p_data *p2p, const u8 *peer_addr,
		enum p2p_wps_method wps_method,
		int go_intent, const u8 *own_interface_addr,
		unsigned int force_freq, int persistent_group,
		const u8 *force_ssid, size_t force_ssid_len,
		int pd_before_go_neg, unsigned int pref_freq, u16 oob_pw_id)
{
	struct p2p_device *dev;

	p2p_dbg(p2p, "Request to start group negotiation - peer=" MACSTR
		"  GO Intent=%d  Intended Interface Address=" MACSTR
		" wps_method=%d persistent_group=%d pd_before_go_neg=%d "
		"oob_pw_id=%u",
		MAC2STR(peer_addr), go_intent, MAC2STR(own_interface_addr),
		wps_method, persistent_group, pd_before_go_neg, oob_pw_id);

	dev = p2p_get_device(p2p, peer_addr);
	if (dev == NULL || (dev->flags & P2P_DEV_PROBE_REQ_ONLY)) {
		p2p_dbg(p2p, "Cannot connect to unknown P2P Device " MACSTR,
			MAC2STR(peer_addr));
		return -1;
	}

	if (p2p_prepare_channel(p2p, dev, force_freq, pref_freq,
				go_intent == 15) < 0)
		return -1;

	if (dev->flags & P2P_DEV_GROUP_CLIENT_ONLY) {
		if (!(dev->info.dev_capab &
		      P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY)) {
			p2p_dbg(p2p, "Cannot connect to P2P Device " MACSTR
				" that is in a group and is not discoverable",
				MAC2STR(peer_addr));
			return -1;
		}
		if (dev->oper_freq <= 0) {
			p2p_dbg(p2p, "Cannot connect to P2P Device " MACSTR
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

	p2p->ssid_set = 0;
	if (force_ssid) {
		wpa_hexdump_ascii(MSG_DEBUG, "P2P: Forced SSID",
				  force_ssid, force_ssid_len);
		os_memcpy(p2p->ssid, force_ssid, force_ssid_len);
		p2p->ssid_len = force_ssid_len;
		p2p->ssid_set = 1;
	}

	dev->flags &= ~P2P_DEV_NOT_YET_READY;
	dev->flags &= ~P2P_DEV_USER_REJECTED;
	dev->flags &= ~P2P_DEV_WAIT_GO_NEG_RESPONSE;
	dev->flags &= ~P2P_DEV_WAIT_GO_NEG_CONFIRM;
	if (pd_before_go_neg)
		dev->flags |= P2P_DEV_PD_BEFORE_GO_NEG;
	else {
		dev->flags &= ~P2P_DEV_PD_BEFORE_GO_NEG;
		/*
		 * Assign dialog token and tie breaker here to use the same
		 * values in each retry within the same GO Negotiation exchange.
		 */
		dev->dialog_token++;
		if (dev->dialog_token == 0)
			dev->dialog_token = 1;
		dev->tie_breaker = p2p->next_tie_breaker;
		p2p->next_tie_breaker = !p2p->next_tie_breaker;
	}
	dev->connect_reqs = 0;
	dev->go_neg_req_sent = 0;
	dev->go_state = UNKNOWN_GO;
	p2p_set_dev_persistent(dev, persistent_group);
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
		p2p_dbg(p2p, "Dropped previous pending Action frame TX that was waiting for p2p_scan completion");
		os_free(p2p->after_scan_tx);
		p2p->after_scan_tx = NULL;
	}

	dev->wps_method = wps_method;
	dev->oob_pw_id = oob_pw_id;
	dev->status = P2P_SC_SUCCESS;

	if (p2p->p2p_scan_running) {
		p2p_dbg(p2p, "p2p_scan running - delay connect send");
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
		  unsigned int force_freq, int persistent_group,
		  const u8 *force_ssid, size_t force_ssid_len,
		  unsigned int pref_freq, u16 oob_pw_id)
{
	struct p2p_device *dev;

	p2p_dbg(p2p, "Request to authorize group negotiation - peer=" MACSTR
		"  GO Intent=%d  Intended Interface Address=" MACSTR
		" wps_method=%d  persistent_group=%d oob_pw_id=%u",
		MAC2STR(peer_addr), go_intent, MAC2STR(own_interface_addr),
		wps_method, persistent_group, oob_pw_id);

	dev = p2p_get_device(p2p, peer_addr);
	if (dev == NULL) {
		p2p_dbg(p2p, "Cannot authorize unknown P2P Device " MACSTR,
			MAC2STR(peer_addr));
		return -1;
	}

	if (p2p_prepare_channel(p2p, dev, force_freq, pref_freq, go_intent ==
				15) < 0)
		return -1;

	p2p->ssid_set = 0;
	if (force_ssid) {
		wpa_hexdump_ascii(MSG_DEBUG, "P2P: Forced SSID",
				  force_ssid, force_ssid_len);
		os_memcpy(p2p->ssid, force_ssid, force_ssid_len);
		p2p->ssid_len = force_ssid_len;
		p2p->ssid_set = 1;
	}

	dev->flags &= ~P2P_DEV_NOT_YET_READY;
	dev->flags &= ~P2P_DEV_USER_REJECTED;
	dev->go_neg_req_sent = 0;
	dev->go_state = UNKNOWN_GO;
	p2p_set_dev_persistent(dev, persistent_group);
	p2p->go_intent = go_intent;
	os_memcpy(p2p->intended_addr, own_interface_addr, ETH_ALEN);

	dev->wps_method = wps_method;
	dev->oob_pw_id = oob_pw_id;
	dev->status = P2P_SC_SUCCESS;

	return 0;
}


void p2p_add_dev_info(struct p2p_data *p2p, const u8 *addr,
		      struct p2p_device *dev, struct p2p_message *msg)
{
	os_get_reltime(&dev->last_seen);

	p2p_copy_wps_info(p2p, dev, 0, msg);

	if (msg->listen_channel) {
		int freq;
		freq = p2p_channel_to_freq(msg->listen_channel[3],
					   msg->listen_channel[4]);
		if (freq < 0) {
			p2p_dbg(p2p, "Unknown peer Listen channel: "
				"country=%c%c(0x%02x) reg_class=%u channel=%u",
				msg->listen_channel[0],
				msg->listen_channel[1],
				msg->listen_channel[2],
				msg->listen_channel[3],
				msg->listen_channel[4]);
		} else {
			p2p_dbg(p2p, "Update peer " MACSTR
				" Listen channel: %u -> %u MHz",
				MAC2STR(dev->info.p2p_device_addr),
				dev->listen_freq, freq);
			dev->listen_freq = freq;
		}
	}

	if (msg->wfd_subelems) {
		wpabuf_free(dev->info.wfd_subelems);
		dev->info.wfd_subelems = wpabuf_dup(msg->wfd_subelems);
	}

	if (dev->flags & P2P_DEV_PROBE_REQ_ONLY) {
		dev->flags &= ~P2P_DEV_PROBE_REQ_ONLY;
		p2p_dbg(p2p, "Completed device entry based on data from GO Negotiation Request");
	} else {
		p2p_dbg(p2p, "Created device entry based on GO Neg Req: "
			MACSTR " dev_capab=0x%x group_capab=0x%x name='%s' "
			"listen_freq=%d",
			MAC2STR(dev->info.p2p_device_addr),
			dev->info.dev_capab, dev->info.group_capab,
			dev->info.device_name, dev->listen_freq);
	}

	dev->flags &= ~P2P_DEV_GROUP_CLIENT_ONLY;

	if (dev->flags & P2P_DEV_USER_REJECTED) {
		p2p_dbg(p2p, "Do not report rejected device");
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
	if (p2p->ssid_set) {
		os_memcpy(params->ssid, p2p->ssid, p2p->ssid_len);
		params->ssid_len = p2p->ssid_len;
	} else {
		p2p_build_ssid(p2p, params->ssid, &params->ssid_len);
	}
	p2p->ssid_set = 0;

	p2p_random(params->passphrase, p2p->cfg->passphrase_len);
	return 0;
}


void p2p_go_complete(struct p2p_data *p2p, struct p2p_device *peer)
{
	struct p2p_go_neg_results res;
	int go = peer->go_state == LOCAL_GO;
	struct p2p_channels intersection;

	p2p_dbg(p2p, "GO Negotiation with " MACSTR " completed (%s will be GO)",
		MAC2STR(peer->info.p2p_device_addr), go ? "local end" : "peer");

	os_memset(&res, 0, sizeof(res));
	res.role_go = go;
	os_memcpy(res.peer_device_addr, peer->info.p2p_device_addr, ETH_ALEN);
	os_memcpy(res.peer_interface_addr, peer->intended_addr, ETH_ALEN);
	res.wps_method = peer->wps_method;
	if (peer->flags & P2P_DEV_PREFER_PERSISTENT_GROUP) {
		if (peer->flags & P2P_DEV_PREFER_PERSISTENT_RECONN)
			res.persistent_group = 2;
		else
			res.persistent_group = 1;
	}

	if (go) {
		/* Setup AP mode for WPS provisioning */
		res.freq = p2p_channel_to_freq(p2p->op_reg_class,
					       p2p->op_channel);
		os_memcpy(res.ssid, p2p->ssid, p2p->ssid_len);
		res.ssid_len = p2p->ssid_len;
		p2p_random(res.passphrase, p2p->cfg->passphrase_len);
	} else {
		res.freq = peer->oper_freq;
		if (p2p->ssid_len) {
			os_memcpy(res.ssid, p2p->ssid, p2p->ssid_len);
			res.ssid_len = p2p->ssid_len;
		}
	}

	p2p_channels_dump(p2p, "own channels", &p2p->channels);
	p2p_channels_dump(p2p, "peer channels", &peer->channels);
	p2p_channels_intersect(&p2p->channels, &peer->channels,
			       &intersection);
	if (go) {
		p2p_channels_remove_freqs(&intersection, &p2p->no_go_freq);
		p2p_channels_dump(p2p, "intersection after no-GO removal",
				  &intersection);
	}

	p2p_channels_to_freqs(&intersection, res.freq_list,
			      P2P_MAX_CHANNELS);

	res.peer_config_timeout = go ? peer->client_timeout : peer->go_timeout;

	p2p_clear_timeout(p2p);
	p2p->ssid_set = 0;
	peer->go_neg_req_sent = 0;
	peer->flags &= ~P2P_DEV_PEER_WAITING_RESPONSE;
	peer->wps_method = WPS_NOT_READY;
	peer->oob_pw_id = 0;
	wpabuf_free(peer->go_neg_conf);
	peer->go_neg_conf = NULL;

	p2p_set_state(p2p, P2P_PROVISIONING);
	p2p->cfg->go_neg_completed(p2p->cfg->cb_ctx, &res);
}


static void p2p_rx_p2p_action(struct p2p_data *p2p, const u8 *sa,
			      const u8 *data, size_t len, int rx_freq)
{
	p2p_dbg(p2p, "RX P2P Public Action from " MACSTR, MAC2STR(sa));
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
		p2p_dbg(p2p, "Unsupported P2P Public Action frame type %d",
			data[0]);
		break;
	}
}


static void p2p_rx_action_public(struct p2p_data *p2p, const u8 *da,
				 const u8 *sa, const u8 *bssid, const u8 *data,
				 size_t len, int freq)
{
	if (len < 1)
		return;

	switch (data[0]) {
	case WLAN_PA_VENDOR_SPECIFIC:
		data++;
		len--;
		if (len < 4)
			return;
		if (WPA_GET_BE32(data) != P2P_IE_VENDOR_TYPE)
			return;

		data += 4;
		len -= 4;

		p2p_rx_p2p_action(p2p, sa, data, len, freq);
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

	if (WPA_GET_BE32(data) != P2P_IE_VENDOR_TYPE)
		return;
	data += 4;
	len -= 4;

	/* P2P action frame */
	p2p_dbg(p2p, "RX P2P Action from " MACSTR, MAC2STR(sa));
	wpa_hexdump(MSG_MSGDUMP, "P2P: P2P Action contents", data, len);

	if (len < 1)
		return;
	switch (data[0]) {
	case P2P_NOA:
		p2p_dbg(p2p, "Received P2P Action - Notice of Absence");
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
		p2p_dbg(p2p, "Received P2P Action - unknown type %u", data[0]);
		break;
	}
}


static void p2p_go_neg_start(void *eloop_ctx, void *timeout_ctx)
{
	struct p2p_data *p2p = eloop_ctx;
	if (p2p->go_neg_peer == NULL)
		return;
	if (p2p->pending_listen_freq) {
		p2p_dbg(p2p, "Clear pending_listen_freq for p2p_go_neg_start");
		p2p->pending_listen_freq = 0;
	}
	p2p->cfg->stop_listen(p2p->cfg->cb_ctx);
	p2p->go_neg_peer->status = P2P_SC_SUCCESS;
	/*
	 * Set new timeout to make sure a previously set one does not expire
	 * too quickly while waiting for the GO Negotiation to complete.
	 */
	p2p_set_timeout(p2p, 0, 500000);
	p2p_connect_send(p2p, p2p->go_neg_peer);
}


static void p2p_invite_start(void *eloop_ctx, void *timeout_ctx)
{
	struct p2p_data *p2p = eloop_ctx;
	if (p2p->invite_peer == NULL)
		return;
	if (p2p->pending_listen_freq) {
		p2p_dbg(p2p, "Clear pending_listen_freq for p2p_invite_start");
		p2p->pending_listen_freq = 0;
	}
	p2p->cfg->stop_listen(p2p->cfg->cb_ctx);
	p2p_invite_send(p2p, p2p->invite_peer, p2p->invite_go_dev_addr,
			p2p->invite_dev_pw_id);
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
		if (msg.listen_channel) {
			int freq;

			if (dev->country[0] == 0)
				os_memcpy(dev->country, msg.listen_channel, 3);

			freq = p2p_channel_to_freq(msg.listen_channel[3],
						   msg.listen_channel[4]);

			if (freq > 0 && dev->listen_freq != freq) {
				p2p_dbg(p2p,
					"Updated peer " MACSTR " Listen channel (Probe Request): %d -> %d MHz",
					MAC2STR(addr), dev->listen_freq, freq);
				dev->listen_freq = freq;
			}
		}

		os_get_reltime(&dev->last_seen);
		p2p_parse_free(&msg);
		return; /* already known */
	}

	dev = p2p_create_device(p2p, addr);
	if (dev == NULL) {
		p2p_parse_free(&msg);
		return;
	}

	os_get_reltime(&dev->last_seen);
	dev->flags |= P2P_DEV_PROBE_REQ_ONLY;

	if (msg.listen_channel) {
		os_memcpy(dev->country, msg.listen_channel, 3);
		dev->listen_freq = p2p_channel_to_freq(msg.listen_channel[3],
						       msg.listen_channel[4]);
	}

	p2p_copy_wps_info(p2p, dev, 1, &msg);

	if (msg.wfd_subelems) {
		wpabuf_free(dev->info.wfd_subelems);
		dev->info.wfd_subelems = wpabuf_dup(msg.wfd_subelems);
	}

	p2p_parse_free(&msg);

	p2p_dbg(p2p, "Created device entry based on Probe Req: " MACSTR
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
		os_get_reltime(&dev->last_seen);
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

	for (i = 0; i < p2p->cfg->num_sec_dev_types; i++) {
		if (dev_type_list_match(p2p->cfg->sec_dev_type[i],
					attr.req_dev_type,
					attr.num_req_dev_type))
			return 1; /* Own Secondary Device Type matches */
	}

	/* No matching device type found */
	return 0;
}


struct wpabuf * p2p_build_probe_resp_ies(struct p2p_data *p2p,
					 const u8 *query_hash,
					 u8 query_count)
{
	struct wpabuf *buf;
	u8 *len;
	int pw_id = -1;
	size_t extra = 0;

#ifdef CONFIG_WIFI_DISPLAY
	if (p2p->wfd_ie_probe_resp)
		extra = wpabuf_len(p2p->wfd_ie_probe_resp);
#endif /* CONFIG_WIFI_DISPLAY */

	if (p2p->vendor_elem && p2p->vendor_elem[VENDOR_ELEM_PROBE_RESP_P2P])
		extra += wpabuf_len(p2p->vendor_elem[VENDOR_ELEM_PROBE_RESP_P2P]);

	if (query_count)
		extra += MAX_SVC_ADV_IE_LEN;

	buf = wpabuf_alloc(1000 + extra);
	if (buf == NULL)
		return NULL;

	if (p2p->go_neg_peer) {
		/* Advertise immediate availability of WPS credential */
		pw_id = p2p_wps_method_pw_id(p2p->go_neg_peer->wps_method);
	}

	if (p2p_build_wps_ie(p2p, buf, pw_id, 1) < 0) {
		p2p_dbg(p2p, "Failed to build WPS IE for Probe Response");
		wpabuf_free(buf);
		return NULL;
	}

#ifdef CONFIG_WIFI_DISPLAY
	if (p2p->wfd_ie_probe_resp)
		wpabuf_put_buf(buf, p2p->wfd_ie_probe_resp);
#endif /* CONFIG_WIFI_DISPLAY */

	if (p2p->vendor_elem && p2p->vendor_elem[VENDOR_ELEM_PROBE_RESP_P2P])
		wpabuf_put_buf(buf,
			       p2p->vendor_elem[VENDOR_ELEM_PROBE_RESP_P2P]);

	/* P2P IE */
	len = p2p_buf_add_ie_hdr(buf);
	p2p_buf_add_capability(buf, p2p->dev_capab &
			       ~P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY, 0);
	if (p2p->ext_listen_interval)
		p2p_buf_add_ext_listen_timing(buf, p2p->ext_listen_period,
					      p2p->ext_listen_interval);
	p2p_buf_add_device_info(buf, p2p, NULL);
	p2p_buf_update_ie_hdr(buf, len);

	if (query_count) {
		p2p_buf_add_service_instance(buf, p2p, query_count, query_hash,
					     p2p->p2ps_adv_list);
	}

	return buf;
}

static int p2p_build_probe_resp_buf(struct p2p_data *p2p, struct wpabuf *buf,
				    struct wpabuf *ies,
				    const u8 *addr, int rx_freq)
{
	struct ieee80211_mgmt *resp;
	u8 channel, op_class;

	resp = wpabuf_put(buf, offsetof(struct ieee80211_mgmt,
					u.probe_resp.variable));

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

	if (!rx_freq) {
		channel = p2p->cfg->channel;
	} else if (p2p_freq_to_channel(rx_freq, &op_class, &channel)) {
		p2p_err(p2p, "Failed to convert freq to channel");
		return -1;
	}

	wpabuf_put_u8(buf, WLAN_EID_DS_PARAMS);
	wpabuf_put_u8(buf, 1);
	wpabuf_put_u8(buf, channel);

	wpabuf_put_buf(buf, ies);

	return 0;
}

static int p2p_service_find_asp(struct p2p_data *p2p, const u8 *hash)
{
	struct p2ps_advertisement *adv_data;
	int any_wfa;

	p2p_dbg(p2p, "ASP find - ASP list: %p", p2p->p2ps_adv_list);

	/* Wildcard org.wi-fi.wfds matches any WFA spec defined service */
	any_wfa = os_memcmp(hash, p2p->wild_card_hash, P2PS_HASH_LEN) == 0;

	adv_data = p2p->p2ps_adv_list;
	while (adv_data) {
		if (os_memcmp(hash, adv_data->hash, P2PS_HASH_LEN) == 0)
			return 1; /* exact hash match */
		if (any_wfa &&
		    os_strncmp(adv_data->svc_name, P2PS_WILD_HASH_STR,
			       os_strlen(P2PS_WILD_HASH_STR)) == 0)
			return 1; /* WFA service match */
		adv_data = adv_data->next;
	}

	return 0;
}


static enum p2p_probe_req_status
p2p_reply_probe(struct p2p_data *p2p, const u8 *addr, const u8 *dst,
		const u8 *bssid, const u8 *ie, size_t ie_len,
		unsigned int rx_freq)
{
	struct ieee802_11_elems elems;
	struct wpabuf *buf;
	struct p2p_message msg;
	struct wpabuf *ies;

	if (ieee802_11_parse_elems((u8 *) ie, ie_len, &elems, 0) ==
	    ParseFailed) {
		/* Ignore invalid Probe Request frames */
		p2p_dbg(p2p, "Could not parse Probe Request frame - ignore it");
		return P2P_PREQ_MALFORMED;
	}

	if (elems.p2p == NULL) {
		/* not a P2P probe - ignore it */
		p2p_dbg(p2p, "Not a P2P probe - ignore it");
		return P2P_PREQ_NOT_P2P;
	}

	if (dst && !is_broadcast_ether_addr(dst) &&
	    os_memcmp(dst, p2p->cfg->dev_addr, ETH_ALEN) != 0) {
		/* Not sent to the broadcast address or our P2P Device Address
		 */
		p2p_dbg(p2p, "Probe Req DA " MACSTR " not ours - ignore it",
			MAC2STR(dst));
		return P2P_PREQ_NOT_PROCESSED;
	}

	if (bssid && !is_broadcast_ether_addr(bssid)) {
		/* Not sent to the Wildcard BSSID */
		p2p_dbg(p2p, "Probe Req BSSID " MACSTR " not wildcard - ignore it",
			MAC2STR(bssid));
		return P2P_PREQ_NOT_PROCESSED;
	}

	if (elems.ssid == NULL || elems.ssid_len != P2P_WILDCARD_SSID_LEN ||
	    os_memcmp(elems.ssid, P2P_WILDCARD_SSID, P2P_WILDCARD_SSID_LEN) !=
	    0) {
		/* not using P2P Wildcard SSID - ignore */
		p2p_dbg(p2p, "Probe Req not using P2P Wildcard SSID - ignore it");
		return P2P_PREQ_NOT_PROCESSED;
	}

	if (supp_rates_11b_only(&elems)) {
		/* Indicates support for 11b rates only */
		p2p_dbg(p2p, "Probe Req with 11b rates only supported - ignore it");
		return P2P_PREQ_NOT_P2P;
	}

	os_memset(&msg, 0, sizeof(msg));
	if (p2p_parse_ies(ie, ie_len, &msg) < 0) {
		/* Could not parse P2P attributes */
		p2p_dbg(p2p, "Could not parse P2P attributes in Probe Req - ignore it");
		return P2P_PREQ_NOT_P2P;
	}

	if (msg.service_hash && msg.service_hash_count) {
		const u8 *hash = msg.service_hash;
		u8 i;
		int p2ps_svc_found = 0;

		p2p_dbg(p2p, "in_listen=%d drv_in_listen=%d when received P2PS Probe Request at %u MHz; own Listen channel %u, pending listen freq %u MHz",
			p2p->in_listen, p2p->drv_in_listen, rx_freq,
			p2p->cfg->channel, p2p->pending_listen_freq);

		if (!p2p->in_listen && !p2p->drv_in_listen &&
		    p2p->pending_listen_freq && rx_freq &&
		    rx_freq != p2p->pending_listen_freq) {
			p2p_dbg(p2p, "Do not reply to Probe Request frame that was received on %u MHz while waiting to start Listen state on %u MHz",
				rx_freq, p2p->pending_listen_freq);
			p2p_parse_free(&msg);
			return P2P_PREQ_NOT_LISTEN;
		}

		for (i = 0; i < msg.service_hash_count; i++) {
			if (p2p_service_find_asp(p2p, hash)) {
				p2p_dbg(p2p, "Service Hash match found: "
					MACSTR, MAC2STR(hash));
				p2ps_svc_found = 1;
				break;
			}
			hash += P2PS_HASH_LEN;
		}

		/* Probed hash unknown */
		if (!p2ps_svc_found) {
			p2p_dbg(p2p, "No Service Hash match found");
			p2p_parse_free(&msg);
			return P2P_PREQ_NOT_PROCESSED;
		}
	} else {
		/* This is not a P2PS Probe Request */
		p2p_dbg(p2p, "No P2PS Hash in Probe Request");

		if (!p2p->in_listen || !p2p->drv_in_listen) {
			/* not in Listen state - ignore Probe Request */
			p2p_dbg(p2p, "Not in Listen state (in_listen=%d drv_in_listen=%d) - ignore Probe Request",
				p2p->in_listen, p2p->drv_in_listen);
			p2p_parse_free(&msg);
			return P2P_PREQ_NOT_LISTEN;
		}
	}

	if (msg.device_id &&
	    os_memcmp(msg.device_id, p2p->cfg->dev_addr, ETH_ALEN) != 0) {
		/* Device ID did not match */
		p2p_dbg(p2p, "Probe Req requested Device ID " MACSTR " did not match - ignore it",
			MAC2STR(msg.device_id));
		p2p_parse_free(&msg);
		return P2P_PREQ_NOT_PROCESSED;
	}

	/* Check Requested Device Type match */
	if (msg.wps_attributes &&
	    !p2p_match_dev_type(p2p, msg.wps_attributes)) {
		/* No match with Requested Device Type */
		p2p_dbg(p2p, "Probe Req requestred Device Type did not match - ignore it");
		p2p_parse_free(&msg);
		return P2P_PREQ_NOT_PROCESSED;
	}

	if (!p2p->cfg->send_probe_resp) {
		/* Response generated elsewhere */
		p2p_dbg(p2p, "Probe Resp generated elsewhere - do not generate additional response");
		p2p_parse_free(&msg);
		return P2P_PREQ_NOT_PROCESSED;
	}

	p2p_dbg(p2p, "Reply to P2P Probe Request in Listen state");

	/*
	 * We do not really have a specific BSS that this frame is advertising,
	 * so build a frame that has some information in valid format. This is
	 * really only used for discovery purposes, not to learn exact BSS
	 * parameters.
	 */
	ies = p2p_build_probe_resp_ies(p2p, msg.service_hash,
				       msg.service_hash_count);
	p2p_parse_free(&msg);
	if (ies == NULL)
		return P2P_PREQ_NOT_PROCESSED;

	buf = wpabuf_alloc(200 + wpabuf_len(ies));
	if (buf == NULL) {
		wpabuf_free(ies);
		return P2P_PREQ_NOT_PROCESSED;
	}

	if (p2p_build_probe_resp_buf(p2p, buf, ies, addr, rx_freq)) {
		wpabuf_free(ies);
		wpabuf_free(buf);
		return P2P_PREQ_NOT_PROCESSED;
	}

	wpabuf_free(ies);

	p2p->cfg->send_probe_resp(p2p->cfg->cb_ctx, buf, rx_freq);

	wpabuf_free(buf);

	return P2P_PREQ_PROCESSED;
}


enum p2p_probe_req_status
p2p_probe_req_rx(struct p2p_data *p2p, const u8 *addr, const u8 *dst,
		 const u8 *bssid, const u8 *ie, size_t ie_len,
		 unsigned int rx_freq, int p2p_lo_started)
{
	enum p2p_probe_req_status res;

	p2p_add_dev_from_probe_req(p2p, addr, ie, ie_len);

	if (p2p_lo_started) {
		p2p_dbg(p2p,
			"Probe Response is offloaded, do not reply Probe Request");
		return P2P_PREQ_PROCESSED;
	}

	res = p2p_reply_probe(p2p, addr, dst, bssid, ie, ie_len, rx_freq);
	if (res != P2P_PREQ_PROCESSED && res != P2P_PREQ_NOT_PROCESSED)
		return res;

	/*
	 * Activate a pending GO Negotiation/Invite flow if a received Probe
	 * Request frame is from an expected peer. Some devices may share the
	 * same address for P2P and non-P2P STA running simultaneously. The
	 * P2P_PREQ_PROCESSED and P2P_PREQ_NOT_PROCESSED p2p_reply_probe()
	 * return values verified above ensure we are handling a Probe Request
	 * frame from a P2P peer.
	 */
	if ((p2p->state == P2P_CONNECT || p2p->state == P2P_CONNECT_LISTEN) &&
	    p2p->go_neg_peer &&
	    os_memcmp(addr, p2p->go_neg_peer->info.p2p_device_addr, ETH_ALEN)
	    == 0 &&
	    !(p2p->go_neg_peer->flags & P2P_DEV_WAIT_GO_NEG_CONFIRM)) {
		/* Received a Probe Request from GO Negotiation peer */
		p2p_dbg(p2p, "Found GO Negotiation peer - try to start GO negotiation from timeout");
		eloop_cancel_timeout(p2p_go_neg_start, p2p, NULL);
		eloop_register_timeout(0, 0, p2p_go_neg_start, p2p, NULL);
		return res;
	}

	if ((p2p->state == P2P_INVITE || p2p->state == P2P_INVITE_LISTEN) &&
	    p2p->invite_peer &&
	    (p2p->invite_peer->flags & P2P_DEV_WAIT_INV_REQ_ACK) &&
	    os_memcmp(addr, p2p->invite_peer->info.p2p_device_addr, ETH_ALEN)
	    == 0) {
		/* Received a Probe Request from Invite peer */
		p2p_dbg(p2p, "Found Invite peer - try to start Invite from timeout");
		eloop_cancel_timeout(p2p_invite_start, p2p, NULL);
		eloop_register_timeout(0, 0, p2p_invite_start, p2p, NULL);
		return res;
	}

	return res;
}


static int p2p_assoc_req_ie_wlan_ap(struct p2p_data *p2p, const u8 *bssid,
				    u8 *buf, size_t len, struct wpabuf *p2p_ie)
{
	struct wpabuf *tmp;
	u8 *lpos;
	size_t tmplen;
	int res;
	u8 group_capab;
	struct p2p_message msg;

	if (p2p_ie == NULL)
		return 0; /* WLAN AP is not a P2P manager */

	os_memset(&msg, 0, sizeof(msg));
	if (p2p_parse_p2p_ie(p2p_ie, &msg) < 0)
		return 0;

	p2p_dbg(p2p, "BSS P2P manageability %s",
		msg.manageability ? "enabled" : "disabled");

	if (!msg.manageability)
		return 0;

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
	size_t extra = 0;

	if (!p2p_group)
		return p2p_assoc_req_ie_wlan_ap(p2p, bssid, buf, len, p2p_ie);

#ifdef CONFIG_WIFI_DISPLAY
	if (p2p->wfd_ie_assoc_req)
		extra = wpabuf_len(p2p->wfd_ie_assoc_req);
#endif /* CONFIG_WIFI_DISPLAY */

	if (p2p->vendor_elem && p2p->vendor_elem[VENDOR_ELEM_P2P_ASSOC_REQ])
		extra += wpabuf_len(p2p->vendor_elem[VENDOR_ELEM_P2P_ASSOC_REQ]);

	/*
	 * (Re)Association Request - P2P IE
	 * P2P Capability attribute (shall be present)
	 * Extended Listen Timing (may be present)
	 * P2P Device Info attribute (shall be present)
	 */
	tmp = wpabuf_alloc(200 + extra);
	if (tmp == NULL)
		return -1;

#ifdef CONFIG_WIFI_DISPLAY
	if (p2p->wfd_ie_assoc_req)
		wpabuf_put_buf(tmp, p2p->wfd_ie_assoc_req);
#endif /* CONFIG_WIFI_DISPLAY */

	if (p2p->vendor_elem && p2p->vendor_elem[VENDOR_ELEM_P2P_ASSOC_REQ])
		wpabuf_put_buf(tmp,
			       p2p->vendor_elem[VENDOR_ELEM_P2P_ASSOC_REQ]);

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


struct p2ps_advertisement *
p2p_service_p2ps_id(struct p2p_data *p2p, u32 adv_id)
{
	struct p2ps_advertisement *adv_data;

	if (!p2p)
		return NULL;

	adv_data = p2p->p2ps_adv_list;
	while (adv_data) {
		if (adv_data->id == adv_id)
			return adv_data;
		adv_data = adv_data->next;
	}

	return NULL;
}


int p2p_service_del_asp(struct p2p_data *p2p, u32 adv_id)
{
	struct p2ps_advertisement *adv_data;
	struct p2ps_advertisement **prior;

	if (!p2p)
		return -1;

	adv_data = p2p->p2ps_adv_list;
	prior = &p2p->p2ps_adv_list;
	while (adv_data) {
		if (adv_data->id == adv_id) {
			p2p_dbg(p2p, "Delete ASP adv_id=0x%x", adv_id);
			*prior = adv_data->next;
			os_free(adv_data);
			return 0;
		}
		prior = &adv_data->next;
		adv_data = adv_data->next;
	}

	return -1;
}


int p2p_service_add_asp(struct p2p_data *p2p, int auto_accept, u32 adv_id,
			const char *adv_str, u8 svc_state, u16 config_methods,
			const char *svc_info, const u8 *cpt_priority)
{
	struct p2ps_advertisement *adv_data, *tmp, **prev;
	u8 buf[P2PS_HASH_LEN];
	size_t adv_data_len, adv_len, info_len = 0;
	int i;

	if (!p2p || !adv_str || !adv_str[0] || !cpt_priority)
		return -1;

	if (!(config_methods & p2p->cfg->config_methods)) {
		p2p_dbg(p2p, "Config methods not supported svc: 0x%x dev: 0x%x",
			config_methods, p2p->cfg->config_methods);
		return -1;
	}

	if (!p2ps_gen_hash(p2p, adv_str, buf))
		return -1;

	if (svc_info)
		info_len = os_strlen(svc_info);
	adv_len = os_strlen(adv_str);
	adv_data_len = sizeof(struct p2ps_advertisement) + adv_len + 1 +
		info_len + 1;

	adv_data = os_zalloc(adv_data_len);
	if (!adv_data)
		return -1;

	os_memcpy(adv_data->hash, buf, P2PS_HASH_LEN);
	adv_data->id = adv_id;
	adv_data->state = svc_state;
	adv_data->config_methods = config_methods & p2p->cfg->config_methods;
	adv_data->auto_accept = (u8) auto_accept;
	os_memcpy(adv_data->svc_name, adv_str, adv_len);

	for (i = 0; cpt_priority[i] && i < P2PS_FEATURE_CAPAB_CPT_MAX; i++) {
		adv_data->cpt_priority[i] = cpt_priority[i];
		adv_data->cpt_mask |= cpt_priority[i];
	}

	if (svc_info && info_len) {
		adv_data->svc_info = &adv_data->svc_name[adv_len + 1];
		os_memcpy(adv_data->svc_info, svc_info, info_len);
	}

	/*
	 * Group Advertisements by service string. They do not need to be
	 * sorted, but groups allow easier Probe Response instance grouping
	 */
	tmp = p2p->p2ps_adv_list;
	prev = &p2p->p2ps_adv_list;
	while (tmp) {
		if (tmp->id == adv_data->id) {
			if (os_strcmp(tmp->svc_name, adv_data->svc_name) != 0) {
				os_free(adv_data);
				return -1;
			}
			adv_data->next = tmp->next;
			*prev = adv_data;
			os_free(tmp);
			goto inserted;
		} else {
			if (os_strcmp(tmp->svc_name, adv_data->svc_name) == 0) {
				adv_data->next = tmp->next;
				tmp->next = adv_data;
				goto inserted;
			}
		}
		prev = &tmp->next;
		tmp = tmp->next;
	}

	/* No svc_name match found */
	adv_data->next = p2p->p2ps_adv_list;
	p2p->p2ps_adv_list = adv_data;

inserted:
	p2p_dbg(p2p,
		"Added ASP advertisement adv_id=0x%x config_methods=0x%x svc_state=0x%x adv_str='%s' cpt_mask=0x%x",
		adv_id, adv_data->config_methods, svc_state, adv_str,
		adv_data->cpt_mask);

	return 0;
}


void p2p_service_flush_asp(struct p2p_data *p2p)
{
	struct p2ps_advertisement *adv, *prev;

	if (!p2p)
		return;

	adv = p2p->p2ps_adv_list;
	while (adv) {
		prev = adv;
		adv = adv->next;
		os_free(prev);
	}

	p2p->p2ps_adv_list = NULL;
	p2ps_prov_free(p2p);
	p2p_dbg(p2p, "All ASP advertisements flushed");
}


int p2p_parse_dev_addr_in_p2p_ie(struct wpabuf *p2p_ie, u8 *dev_addr)
{
	struct p2p_message msg;

	os_memset(&msg, 0, sizeof(msg));
	if (p2p_parse_p2p_ie(p2p_ie, &msg))
		return -1;

	if (msg.p2p_device_addr) {
		os_memcpy(dev_addr, msg.p2p_device_addr, ETH_ALEN);
		return 0;
	} else if (msg.device_id) {
		os_memcpy(dev_addr, msg.device_id, ETH_ALEN);
		return 0;
	}
	return -1;
}


int p2p_parse_dev_addr(const u8 *ies, size_t ies_len, u8 *dev_addr)
{
	struct wpabuf *p2p_ie;
	int ret;

	p2p_ie = ieee802_11_vendor_ie_concat(ies, ies_len,
					     P2P_IE_VENDOR_TYPE);
	if (p2p_ie == NULL)
		return -1;
	ret = p2p_parse_dev_addr_in_p2p_ie(p2p_ie, dev_addr);
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
		p2p_dbg(p2p, "No pending Group Formation - ignore WPS registration success notification");
		return; /* No pending Group Formation */
	}

	if (os_memcmp(mac_addr, p2p->go_neg_peer->intended_addr, ETH_ALEN) !=
	    0) {
		p2p_dbg(p2p, "Ignore WPS registration success notification for "
			MACSTR " (GO Negotiation peer " MACSTR ")",
			MAC2STR(mac_addr),
			MAC2STR(p2p->go_neg_peer->intended_addr));
		return; /* Ignore unexpected peer address */
	}

	p2p_dbg(p2p, "Group Formation completed successfully with " MACSTR,
		MAC2STR(mac_addr));

	p2p_clear_go_neg(p2p);
}


void p2p_group_formation_failed(struct p2p_data *p2p)
{
	if (p2p->go_neg_peer == NULL) {
		p2p_dbg(p2p, "No pending Group Formation - ignore group formation failure notification");
		return; /* No pending Group Formation */
	}

	p2p_dbg(p2p, "Group Formation failed with " MACSTR,
		MAC2STR(p2p->go_neg_peer->intended_addr));

	p2p_clear_go_neg(p2p);
}


struct p2p_data * p2p_init(const struct p2p_config *cfg)
{
	struct p2p_data *p2p;

	if (cfg->max_peers < 1 ||
	    cfg->passphrase_len < 8 || cfg->passphrase_len > 63)
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
	if (cfg->pref_chan) {
		p2p->cfg->pref_chan = os_malloc(cfg->num_pref_chan *
						sizeof(struct p2p_channel));
		if (p2p->cfg->pref_chan) {
			os_memcpy(p2p->cfg->pref_chan, cfg->pref_chan,
				  cfg->num_pref_chan *
				  sizeof(struct p2p_channel));
		} else
			p2p->cfg->num_pref_chan = 0;
	}

	p2ps_gen_hash(p2p, P2PS_WILD_HASH_STR, p2p->wild_card_hash);

	p2p->min_disc_int = 1;
	p2p->max_disc_int = 3;
	p2p->max_disc_tu = -1;

	if (os_get_random(&p2p->next_tie_breaker, 1) < 0)
		p2p->next_tie_breaker = 0;
	p2p->next_tie_breaker &= 0x01;
	if (cfg->sd_request)
		p2p->dev_capab |= P2P_DEV_CAPAB_SERVICE_DISCOVERY;
	p2p->dev_capab |= P2P_DEV_CAPAB_INVITATION_PROCEDURE;
	if (cfg->concurrent_operations)
		p2p->dev_capab |= P2P_DEV_CAPAB_CONCURRENT_OPER;
	p2p->dev_capab |= P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY;

	dl_list_init(&p2p->devices);

	p2p->go_timeout = 100;
	p2p->client_timeout = 20;
	p2p->num_p2p_sd_queries = 0;

	p2p_dbg(p2p, "initialized");
	p2p_channels_dump(p2p, "channels", &p2p->cfg->channels);
	p2p_channels_dump(p2p, "cli_channels", &p2p->cfg->cli_channels);

	return p2p;
}


void p2p_deinit(struct p2p_data *p2p)
{
#ifdef CONFIG_WIFI_DISPLAY
	wpabuf_free(p2p->wfd_ie_beacon);
	wpabuf_free(p2p->wfd_ie_probe_req);
	wpabuf_free(p2p->wfd_ie_probe_resp);
	wpabuf_free(p2p->wfd_ie_assoc_req);
	wpabuf_free(p2p->wfd_ie_invitation);
	wpabuf_free(p2p->wfd_ie_prov_disc_req);
	wpabuf_free(p2p->wfd_ie_prov_disc_resp);
	wpabuf_free(p2p->wfd_ie_go_neg);
	wpabuf_free(p2p->wfd_dev_info);
	wpabuf_free(p2p->wfd_assoc_bssid);
	wpabuf_free(p2p->wfd_coupled_sink_info);
	wpabuf_free(p2p->wfd_r2_dev_info);
#endif /* CONFIG_WIFI_DISPLAY */

	eloop_cancel_timeout(p2p_scan_timeout, p2p, NULL);
	eloop_cancel_timeout(p2p_go_neg_start, p2p, NULL);
	eloop_cancel_timeout(p2p_go_neg_wait_timeout, p2p, NULL);
	p2p_flush(p2p);
	p2p_free_req_dev_types(p2p);
	os_free(p2p->cfg->dev_name);
	os_free(p2p->cfg->manufacturer);
	os_free(p2p->cfg->model_name);
	os_free(p2p->cfg->model_number);
	os_free(p2p->cfg->serial_number);
	os_free(p2p->cfg->pref_chan);
	os_free(p2p->groups);
	p2ps_prov_free(p2p);
	wpabuf_free(p2p->sd_resp);
	p2p_remove_wps_vendor_extensions(p2p);
	os_free(p2p->no_go_freq.range);
	p2p_service_flush_asp(p2p);

	os_free(p2p);
}


void p2p_flush(struct p2p_data *p2p)
{
	struct p2p_device *dev, *prev;

	p2p_ext_listen(p2p, 0, 0);
	p2p_stop_find(p2p);
	dl_list_for_each_safe(dev, prev, &p2p->devices, struct p2p_device,
			      list) {
		dl_list_del(&dev->list);
		p2p_device_free(p2p, dev);
	}
	p2p_free_sd_queries(p2p);
	os_free(p2p->after_scan_tx);
	p2p->after_scan_tx = NULL;
	p2p->ssid_set = 0;
	p2ps_prov_free(p2p);
	p2p_reset_pending_pd(p2p);
	p2p->override_pref_op_class = 0;
	p2p->override_pref_channel = 0;
}


int p2p_unauthorize(struct p2p_data *p2p, const u8 *addr)
{
	struct p2p_device *dev;

	dev = p2p_get_device(p2p, addr);
	if (dev == NULL)
		return -1;

	p2p_dbg(p2p, "Unauthorizing " MACSTR, MAC2STR(addr));

	if (p2p->go_neg_peer == dev) {
		eloop_cancel_timeout(p2p_go_neg_wait_timeout, p2p, NULL);
		p2p->go_neg_peer = NULL;
	}

	dev->wps_method = WPS_NOT_READY;
	dev->oob_pw_id = 0;
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


static int p2p_pre_find_operation(struct p2p_data *p2p, struct p2p_device *dev)
{
	int res;

	if (dev->sd_pending_bcast_queries == 0) {
		/* Initialize with total number of registered broadcast
		 * SD queries. */
		dev->sd_pending_bcast_queries = p2p->num_p2p_sd_queries;
	}

	res = p2p_start_sd(p2p, dev);
	if (res == -2)
		return -2;
	if (res == 0)
		return 1;

	if (dev->req_config_methods &&
	    !(dev->flags & P2P_DEV_PD_FOR_JOIN)) {
		p2p_dbg(p2p, "Send pending Provision Discovery Request to "
			MACSTR " (config methods 0x%x)",
			MAC2STR(dev->info.p2p_device_addr),
			dev->req_config_methods);
		if (p2p_send_prov_disc_req(p2p, dev, 0, 0) == 0)
			return 1;
	}

	return 0;
}


void p2p_continue_find(struct p2p_data *p2p)
{
	struct p2p_device *dev;
	int found, res;

	p2p_set_state(p2p, P2P_SEARCH);

	/* Continue from the device following the last iteration */
	found = 0;
	dl_list_for_each(dev, &p2p->devices, struct p2p_device, list) {
		if (dev == p2p->last_p2p_find_oper) {
			found = 1;
			continue;
		}
		if (!found)
			continue;
		res = p2p_pre_find_operation(p2p, dev);
		if (res > 0) {
			p2p->last_p2p_find_oper = dev;
			return;
		}
		if (res == -2)
			goto skip_sd;
	}

	/*
	 * Wrap around to the beginning of the list and continue until the last
	 * iteration device.
	 */
	dl_list_for_each(dev, &p2p->devices, struct p2p_device, list) {
		res = p2p_pre_find_operation(p2p, dev);
		if (res > 0) {
			p2p->last_p2p_find_oper = dev;
			return;
		}
		if (res == -2)
			goto skip_sd;
		if (dev == p2p->last_p2p_find_oper)
			break;
	}

skip_sd:
	os_memset(p2p->sd_query_no_ack, 0, ETH_ALEN);
	p2p_listen_in_find(p2p, 1);
}


static void p2p_sd_cb(struct p2p_data *p2p, int success)
{
	p2p_dbg(p2p, "Service Discovery Query TX callback: success=%d",
		success);
	p2p->pending_action_state = P2P_NO_PENDING_ACTION;

	if (!success) {
		if (p2p->sd_peer) {
			if (is_zero_ether_addr(p2p->sd_query_no_ack)) {
				os_memcpy(p2p->sd_query_no_ack,
					  p2p->sd_peer->info.p2p_device_addr,
					  ETH_ALEN);
				p2p_dbg(p2p,
					"First SD Query no-ACK in this search iteration: "
					MACSTR, MAC2STR(p2p->sd_query_no_ack));
			}
			p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
		}
		p2p->sd_peer = NULL;
		if (p2p->state != P2P_IDLE)
			p2p_continue_find(p2p);
		return;
	}

	if (p2p->sd_peer == NULL) {
		p2p_dbg(p2p, "No SD peer entry known");
		if (p2p->state != P2P_IDLE)
			p2p_continue_find(p2p);
		return;
	}

	if (p2p->sd_query && p2p->sd_query->for_all_peers) {
		/* Update the pending broadcast SD query count for this device
		 */
		p2p->sd_peer->sd_pending_bcast_queries--;

		/*
		 * If there are no pending broadcast queries for this device,
		 * mark it as done (-1).
		 */
		if (p2p->sd_peer->sd_pending_bcast_queries == 0)
			p2p->sd_peer->sd_pending_bcast_queries = -1;
	}

	/* Wait for response from the peer */
	p2p_set_state(p2p, P2P_SD_DURING_FIND);
	p2p_set_timeout(p2p, 0, 200000);
}


/**
 * p2p_retry_pd - Retry any pending provision disc requests in IDLE state
 * @p2p: P2P module context from p2p_init()
 */
static void p2p_retry_pd(struct p2p_data *p2p)
{
	struct p2p_device *dev;

	/*
	 * Retry the prov disc req attempt only for the peer that the user had
	 * requested.
	 */

	dl_list_for_each(dev, &p2p->devices, struct p2p_device, list) {
		if (os_memcmp(p2p->pending_pd_devaddr,
			      dev->info.p2p_device_addr, ETH_ALEN) != 0)
			continue;
		if (!dev->req_config_methods)
			continue;

		p2p_dbg(p2p, "Send pending Provision Discovery Request to "
			MACSTR " (config methods 0x%x)",
			MAC2STR(dev->info.p2p_device_addr),
			dev->req_config_methods);
		p2p_send_prov_disc_req(p2p, dev,
				       dev->flags & P2P_DEV_PD_FOR_JOIN,
				       p2p->pd_force_freq);
		return;
	}
}


static void p2p_prov_disc_cb(struct p2p_data *p2p, int success)
{
	p2p_dbg(p2p, "Provision Discovery Request TX callback: success=%d",
		success);

	/*
	 * Postpone resetting the pending action state till after we actually
	 * time out. This allows us to take some action like notifying any
	 * interested parties about no response to the request.
	 *
	 * When the timer (below) goes off we check in IDLE, SEARCH, or
	 * LISTEN_ONLY state, which are the only allowed states to issue a PD
	 * requests in, if this was still pending and then raise notification.
	 */

	if (!success) {
		p2p->pending_action_state = P2P_NO_PENDING_ACTION;

		if (p2p->user_initiated_pd &&
		    (p2p->state == P2P_SEARCH || p2p->state == P2P_LISTEN_ONLY))
		{
			/* Retry request from timeout to avoid busy loops */
			p2p->pending_action_state = P2P_PENDING_PD;
			p2p_set_timeout(p2p, 0, 50000);
		} else if (p2p->state != P2P_IDLE)
			p2p_continue_find(p2p);
		else if (p2p->user_initiated_pd) {
			p2p->pending_action_state = P2P_PENDING_PD;
			p2p_set_timeout(p2p, 0, 300000);
		}
		return;
	}

	/*
	 * If after PD Request the peer doesn't expect to receive PD Response
	 * the PD Request ACK indicates a completion of the current PD. This
	 * happens only on the advertiser side sending the follow-on PD Request
	 * with the status different than 12 (Success: accepted by user).
	 */
	if (p2p->p2ps_prov && !p2p->p2ps_prov->pd_seeker &&
	    p2p->p2ps_prov->status != P2P_SC_SUCCESS_DEFERRED) {
		p2p_dbg(p2p, "P2PS PD completion on Follow-on PD Request ACK");

		if (p2p->send_action_in_progress) {
			p2p->send_action_in_progress = 0;
			p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
		}

		p2p->pending_action_state = P2P_NO_PENDING_ACTION;

		if (p2p->cfg->p2ps_prov_complete) {
			p2p->cfg->p2ps_prov_complete(
				p2p->cfg->cb_ctx,
				p2p->p2ps_prov->status,
				p2p->p2ps_prov->adv_mac,
				p2p->p2ps_prov->adv_mac,
				p2p->p2ps_prov->session_mac,
				NULL, p2p->p2ps_prov->adv_id,
				p2p->p2ps_prov->session_id,
				0, 0, NULL, 0, 0, 0,
				NULL, NULL, 0, 0, NULL, 0);
		}

		if (p2p->user_initiated_pd)
			p2p_reset_pending_pd(p2p);

		p2ps_prov_free(p2p);
		return;
	}

	/*
	 * This postponing, of resetting pending_action_state, needs to be
	 * done only for user initiated PD requests and not internal ones.
	 */
	if (p2p->user_initiated_pd)
		p2p->pending_action_state = P2P_PENDING_PD;
	else
		p2p->pending_action_state = P2P_NO_PENDING_ACTION;

	/* Wait for response from the peer */
	if (p2p->state == P2P_SEARCH)
		p2p_set_state(p2p, P2P_PD_DURING_FIND);
	p2p_set_timeout(p2p, 0, 200000);
}


static int p2p_check_after_scan_tx_continuation(struct p2p_data *p2p)
{
	if (p2p->after_scan_tx_in_progress) {
		p2p->after_scan_tx_in_progress = 0;
		if (p2p->start_after_scan != P2P_AFTER_SCAN_NOTHING &&
		    p2p_run_after_scan(p2p))
			return 1;
		if (p2p->state == P2P_SEARCH) {
			p2p_dbg(p2p, "Continue find after after_scan_tx completion");
			p2p_continue_find(p2p);
		}
	}

	return 0;
}


static void p2p_prov_disc_resp_cb(struct p2p_data *p2p, int success)
{
	p2p_dbg(p2p, "Provision Discovery Response TX callback: success=%d",
		success);

	if (p2p->send_action_in_progress) {
		p2p->send_action_in_progress = 0;
		p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
	}

	p2p->pending_action_state = P2P_NO_PENDING_ACTION;

	if (!success)
		goto continue_search;

	if (!p2p->cfg->prov_disc_resp_cb ||
	    p2p->cfg->prov_disc_resp_cb(p2p->cfg->cb_ctx) < 1)
		goto continue_search;

	p2p_dbg(p2p,
		"Post-Provision Discovery operations started - do not try to continue other P2P operations");
	return;

continue_search:
	p2p_check_after_scan_tx_continuation(p2p);
}


int p2p_scan_res_handler(struct p2p_data *p2p, const u8 *bssid, int freq,
			 struct os_reltime *rx_time, int level, const u8 *ies,
			 size_t ies_len)
{
	if (os_reltime_before(rx_time, &p2p->find_start)) {
		/*
		 * The driver may have cached (e.g., in cfg80211 BSS table) the
		 * scan results for relatively long time. To avoid reporting
		 * stale information, update P2P peers only based on results
		 * that have based on frames received after the last p2p_find
		 * operation was started.
		 */
		p2p_dbg(p2p, "Ignore old scan result for " MACSTR
			" (rx_time=%u.%06u find_start=%u.%06u)",
			MAC2STR(bssid), (unsigned int) rx_time->sec,
			(unsigned int) rx_time->usec,
			(unsigned int) p2p->find_start.sec,
			(unsigned int) p2p->find_start.usec);
		return 0;
	}

	p2p_add_device(p2p, bssid, freq, rx_time, level, ies, ies_len, 1);

	return 0;
}


void p2p_scan_res_handled(struct p2p_data *p2p)
{
	if (!p2p->p2p_scan_running) {
		p2p_dbg(p2p, "p2p_scan was not running, but scan results received");
	}
	p2p->p2p_scan_running = 0;
	eloop_cancel_timeout(p2p_scan_timeout, p2p, NULL);

	if (p2p_run_after_scan(p2p))
		return;
	if (p2p->state == P2P_SEARCH)
		p2p_continue_find(p2p);
}


void p2p_scan_ie(struct p2p_data *p2p, struct wpabuf *ies, const u8 *dev_id,
		 unsigned int bands)
{
	u8 dev_capab;
	u8 *len;

#ifdef CONFIG_WIFI_DISPLAY
	if (p2p->wfd_ie_probe_req)
		wpabuf_put_buf(ies, p2p->wfd_ie_probe_req);
#endif /* CONFIG_WIFI_DISPLAY */

	if (p2p->vendor_elem && p2p->vendor_elem[VENDOR_ELEM_PROBE_REQ_P2P])
		wpabuf_put_buf(ies,
			       p2p->vendor_elem[VENDOR_ELEM_PROBE_REQ_P2P]);

	len = p2p_buf_add_ie_hdr(ies);

	dev_capab = p2p->dev_capab & ~P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY;

	/* P2PS requires Probe Request frames to include SD bit */
	if (p2p->p2ps_seek && p2p->p2ps_seek_count)
		dev_capab |= P2P_DEV_CAPAB_SERVICE_DISCOVERY;

	p2p_buf_add_capability(ies, dev_capab, 0);

	if (dev_id)
		p2p_buf_add_device_id(ies, dev_id);
	if (p2p->cfg->reg_class && p2p->cfg->channel)
		p2p_buf_add_listen_channel(ies, p2p->cfg->country,
					   p2p->cfg->reg_class,
					   p2p->cfg->channel);
	if (p2p->ext_listen_interval)
		p2p_buf_add_ext_listen_timing(ies, p2p->ext_listen_period,
					      p2p->ext_listen_interval);

	if (bands & BAND_60_GHZ)
		p2p_buf_add_device_info(ies, p2p, NULL);

	if (p2p->p2ps_seek && p2p->p2ps_seek_count)
		p2p_buf_add_service_hash(ies, p2p);

	/* TODO: p2p_buf_add_operating_channel() if GO */
	p2p_buf_update_ie_hdr(ies, len);
}


size_t p2p_scan_ie_buf_len(struct p2p_data *p2p)
{
	size_t len = 100;

#ifdef CONFIG_WIFI_DISPLAY
	if (p2p && p2p->wfd_ie_probe_req)
		len += wpabuf_len(p2p->wfd_ie_probe_req);
#endif /* CONFIG_WIFI_DISPLAY */

	if (p2p && p2p->vendor_elem &&
	    p2p->vendor_elem[VENDOR_ELEM_PROBE_REQ_P2P])
		len += wpabuf_len(p2p->vendor_elem[VENDOR_ELEM_PROBE_REQ_P2P]);

	return len;
}


int p2p_ie_text(struct wpabuf *p2p_ie, char *buf, char *end)
{
	return p2p_attr_text(p2p_ie, buf, end);
}


static void p2p_go_neg_req_cb(struct p2p_data *p2p, int success)
{
	struct p2p_device *dev = p2p->go_neg_peer;
	int timeout;

	p2p_dbg(p2p, "GO Negotiation Request TX callback: success=%d", success);

	if (dev == NULL) {
		p2p_dbg(p2p, "No pending GO Negotiation");
		return;
	}

	if (success) {
		if (dev->flags & P2P_DEV_USER_REJECTED) {
			p2p_set_state(p2p, P2P_IDLE);
			return;
		}
	} else if (dev->go_neg_req_sent) {
		/* Cancel the increment from p2p_connect_send() on failure */
		dev->go_neg_req_sent--;
	}

	if (!success &&
	    (dev->info.dev_capab & P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY) &&
	    !is_zero_ether_addr(dev->member_in_go_dev)) {
		p2p_dbg(p2p, "Peer " MACSTR " did not acknowledge request - try to use device discoverability through its GO",
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
	timeout = success ? 500000 : 100000;
	if (!success && p2p->go_neg_peer &&
	    (p2p->go_neg_peer->flags & P2P_DEV_PEER_WAITING_RESPONSE)) {
		unsigned int r;
		/*
		 * Peer is expected to wait our response and we will skip the
		 * listen phase. Add some randomness to the wait time here to
		 * make it less likely to hit cases where we could end up in
		 * sync with peer not listening.
		 */
		if (os_get_random((u8 *) &r, sizeof(r)) < 0)
			r = 0;
		timeout += r % 100000;
	}
	p2p_set_timeout(p2p, 0, timeout);
}


static void p2p_go_neg_resp_cb(struct p2p_data *p2p, int success)
{
	p2p_dbg(p2p, "GO Negotiation Response TX callback: success=%d",
		success);
	if (!p2p->go_neg_peer && p2p->state == P2P_PROVISIONING) {
		p2p_dbg(p2p, "Ignore TX callback event - GO Negotiation is not running anymore");
		return;
	}
	p2p_set_state(p2p, P2P_CONNECT);
	p2p_set_timeout(p2p, 0, 500000);
}


static void p2p_go_neg_resp_failure_cb(struct p2p_data *p2p, int success,
				       const u8 *addr)
{
	p2p_dbg(p2p, "GO Negotiation Response (failure) TX callback: success=%d", success);
	if (p2p->go_neg_peer && p2p->go_neg_peer->status != P2P_SC_SUCCESS) {
		p2p_go_neg_failed(p2p, p2p->go_neg_peer->status);
		return;
	}

	if (success) {
		struct p2p_device *dev;
		dev = p2p_get_device(p2p, addr);
		if (dev &&
		    dev->status == P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE)
			dev->flags |= P2P_DEV_PEER_WAITING_RESPONSE;
	}

	if (p2p->state == P2P_SEARCH || p2p->state == P2P_SD_DURING_FIND)
		p2p_continue_find(p2p);
}


static void p2p_go_neg_conf_cb(struct p2p_data *p2p,
			       enum p2p_send_action_result result)
{
	struct p2p_device *dev;

	p2p_dbg(p2p, "GO Negotiation Confirm TX callback: result=%d", result);
	if (result == P2P_SEND_ACTION_FAILED) {
		p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
		p2p_go_neg_failed(p2p, -1);
		return;
	}

	dev = p2p->go_neg_peer;

	if (result == P2P_SEND_ACTION_NO_ACK) {
		/*
		 * Retry GO Negotiation Confirmation
		 * P2P_GO_NEG_CNF_MAX_RETRY_COUNT times if we did not receive
		 * ACK for confirmation.
		 */
		if (dev && dev->go_neg_conf &&
		    dev->go_neg_conf_sent <= P2P_GO_NEG_CNF_MAX_RETRY_COUNT) {
			p2p_dbg(p2p, "GO Negotiation Confirm retry %d",
				dev->go_neg_conf_sent);
			p2p->pending_action_state = P2P_PENDING_GO_NEG_CONFIRM;
			if (p2p_send_action(p2p, dev->go_neg_conf_freq,
					    dev->info.p2p_device_addr,
					    p2p->cfg->dev_addr,
					    dev->info.p2p_device_addr,
					    wpabuf_head(dev->go_neg_conf),
					    wpabuf_len(dev->go_neg_conf), 0) >=
			    0) {
				dev->go_neg_conf_sent++;
				return;
			}
			p2p_dbg(p2p, "Failed to re-send Action frame");

			/*
			 * Continue with the assumption that the first attempt
			 * went through and just the ACK frame was lost.
			 */
		}

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
		p2p_dbg(p2p, "Assume GO Negotiation Confirm TX was actually received by the peer even though Ack was not reported");
	}

	p2p->cfg->send_action_done(p2p->cfg->cb_ctx);

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

	p2p_dbg(p2p, "Action frame TX callback (state=%d freq=%u dst=" MACSTR
		" src=" MACSTR " bssid=" MACSTR " result=%d p2p_state=%s)",
		p2p->pending_action_state, freq, MAC2STR(dst), MAC2STR(src),
		MAC2STR(bssid), result, p2p_state_txt(p2p->state));
	success = result == P2P_SEND_ACTION_SUCCESS;
	state = p2p->pending_action_state;
	p2p->pending_action_state = P2P_NO_PENDING_ACTION;
	switch (state) {
	case P2P_NO_PENDING_ACTION:
		if (p2p->send_action_in_progress) {
			p2p->send_action_in_progress = 0;
			p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
		}
		p2p_check_after_scan_tx_continuation(p2p);
		break;
	case P2P_PENDING_GO_NEG_REQUEST:
		p2p_go_neg_req_cb(p2p, success);
		break;
	case P2P_PENDING_GO_NEG_RESPONSE:
		p2p_go_neg_resp_cb(p2p, success);
		break;
	case P2P_PENDING_GO_NEG_RESPONSE_FAILURE:
		p2p_go_neg_resp_failure_cb(p2p, success, dst);
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
	case P2P_PENDING_PD_RESPONSE:
		p2p_prov_disc_resp_cb(p2p, success);
		break;
	case P2P_PENDING_INVITATION_REQUEST:
		p2p_invitation_req_cb(p2p, success);
		break;
	case P2P_PENDING_INVITATION_RESPONSE:
		p2p_invitation_resp_cb(p2p, success);
		if (p2p->inv_status != P2P_SC_SUCCESS)
			p2p_check_after_scan_tx_continuation(p2p);
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

	p2p->after_scan_tx_in_progress = 0;
}


void p2p_listen_cb(struct p2p_data *p2p, unsigned int freq,
		   unsigned int duration)
{
	if (freq == p2p->pending_client_disc_freq) {
		p2p_dbg(p2p, "Client discoverability remain-awake completed");
		p2p->pending_client_disc_freq = 0;
		return;
	}

	if (freq != p2p->pending_listen_freq) {
		p2p_dbg(p2p, "Unexpected listen callback for freq=%u duration=%u (pending_listen_freq=%u)",
			freq, duration, p2p->pending_listen_freq);
		return;
	}

	p2p_dbg(p2p, "Starting Listen timeout(%u,%u) on freq=%u based on callback",
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
	p2p_dbg(p2p, "Driver ended Listen state (freq=%u)", freq);
	p2p->drv_in_listen = 0;
	if (p2p->in_listen)
		return 0; /* Internal timeout will trigger the next step */

	if (p2p->state == P2P_WAIT_PEER_CONNECT && p2p->go_neg_peer &&
	    p2p->pending_listen_freq) {
		/*
		 * Better wait a bit if the driver is unable to start
		 * offchannel operation for some reason to continue with
		 * P2P_WAIT_PEER_(IDLE/CONNECT) state transitions.
		 */
		p2p_dbg(p2p,
			"Listen operation did not seem to start - delay idle phase to avoid busy loop");
		p2p_set_timeout(p2p, 0, 100000);
		return 1;
	}

	if (p2p->state == P2P_CONNECT_LISTEN && p2p->go_neg_peer) {
		if (p2p->go_neg_peer->connect_reqs >= 120) {
			p2p_dbg(p2p, "Timeout on sending GO Negotiation Request without getting response");
			p2p_go_neg_failed(p2p, -1);
			return 0;
		}

		p2p_set_state(p2p, P2P_CONNECT);
		p2p_connect_send(p2p, p2p->go_neg_peer);
		return 1;
	} else if (p2p->state == P2P_SEARCH) {
		if (p2p->p2p_scan_running) {
			 /*
			  * Search is already in progress. This can happen if
			  * an Action frame RX is reported immediately after
			  * the end of a remain-on-channel operation and the
			  * response frame to that is sent using an offchannel
			  * operation while in p2p_find. Avoid an attempt to
			  * restart a scan here.
			  */
			p2p_dbg(p2p, "p2p_scan already in progress - do not try to start a new one");
			return 1;
		}
		if (p2p->pending_listen_freq) {
			/*
			 * Better wait a bit if the driver is unable to start
			 * offchannel operation for some reason. p2p_search()
			 * will be started from internal timeout.
			 */
			p2p_dbg(p2p, "Listen operation did not seem to start - delay search phase to avoid busy loop");
			p2p_set_timeout(p2p, 0, 100000);
			return 1;
		}
		if (p2p->search_delay) {
			p2p_dbg(p2p, "Delay search operation by %u ms",
				p2p->search_delay);
			p2p_set_timeout(p2p, p2p->search_delay / 1000,
					(p2p->search_delay % 1000) * 1000);
			return 1;
		}
		p2p_search(p2p);
		return 1;
	}

	return 0;
}


static void p2p_timeout_connect(struct p2p_data *p2p)
{
	p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
	if (p2p->go_neg_peer &&
	    (p2p->go_neg_peer->flags & P2P_DEV_WAIT_GO_NEG_CONFIRM)) {
		p2p_dbg(p2p, "Wait for GO Negotiation Confirm timed out - assume GO Negotiation failed");
		p2p_go_neg_failed(p2p, -1);
		return;
	}
	if (p2p->go_neg_peer &&
	    (p2p->go_neg_peer->flags & P2P_DEV_PEER_WAITING_RESPONSE) &&
	    p2p->go_neg_peer->connect_reqs < 120) {
		p2p_dbg(p2p, "Peer expected to wait our response - skip listen");
		p2p_connect_send(p2p, p2p->go_neg_peer);
		return;
	}
	if (p2p->go_neg_peer && p2p->go_neg_peer->oob_go_neg_freq > 0) {
		p2p_dbg(p2p, "Skip connect-listen since GO Neg channel known (OOB)");
		p2p_set_state(p2p, P2P_CONNECT_LISTEN);
		p2p_set_timeout(p2p, 0, 30000);
		return;
	}
	p2p_set_state(p2p, P2P_CONNECT_LISTEN);
	p2p_listen_in_find(p2p, 0);
}


static void p2p_timeout_connect_listen(struct p2p_data *p2p)
{
	if (p2p->go_neg_peer) {
		if (p2p->drv_in_listen) {
			p2p_dbg(p2p, "Driver is still in Listen state; wait for it to complete");
			return;
		}

		if (p2p->go_neg_peer->connect_reqs >= 120) {
			p2p_dbg(p2p, "Timeout on sending GO Negotiation Request without getting response");
			p2p_go_neg_failed(p2p, -1);
			return;
		}

		p2p_set_state(p2p, P2P_CONNECT);
		p2p_connect_send(p2p, p2p->go_neg_peer);
	} else
		p2p_set_state(p2p, P2P_IDLE);
}


static void p2p_timeout_wait_peer_connect(struct p2p_data *p2p)
{
	p2p_set_state(p2p, P2P_WAIT_PEER_IDLE);

	if (p2p->cfg->is_concurrent_session_active &&
	    p2p->cfg->is_concurrent_session_active(p2p->cfg->cb_ctx))
		p2p_set_timeout(p2p, 0, 500000);
	else
		p2p_set_timeout(p2p, 0, 200000);
}


static void p2p_timeout_wait_peer_idle(struct p2p_data *p2p)
{
	struct p2p_device *dev = p2p->go_neg_peer;

	if (dev == NULL) {
		p2p_dbg(p2p, "Unknown GO Neg peer - stop GO Neg wait");
		return;
	}

	p2p_dbg(p2p, "Go to Listen state while waiting for the peer to become ready for GO Negotiation");
	p2p_set_state(p2p, P2P_WAIT_PEER_CONNECT);
	p2p_listen_in_find(p2p, 0);
}


static void p2p_timeout_sd_during_find(struct p2p_data *p2p)
{
	p2p_dbg(p2p, "Service Discovery Query timeout");
	if (p2p->sd_peer) {
		p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
		p2p->sd_peer = NULL;
	}
	p2p_continue_find(p2p);
}


static void p2p_timeout_prov_disc_during_find(struct p2p_data *p2p)
{
	p2p_dbg(p2p, "Provision Discovery Request timeout");
	p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
	p2p_continue_find(p2p);
}


static void p2p_timeout_prov_disc_req(struct p2p_data *p2p)
{
	u32 adv_id = 0;
	u8 *adv_mac = NULL;

	p2p->pending_action_state = P2P_NO_PENDING_ACTION;

	/*
	 * For user initiated PD requests that we have not gotten any responses
	 * for while in IDLE state, we retry them a couple of times before
	 * giving up.
	 */
	if (!p2p->user_initiated_pd)
		return;

	p2p_dbg(p2p, "User initiated Provision Discovery Request timeout");

	if (p2p->pd_retries) {
		p2p->pd_retries--;
		p2p_retry_pd(p2p);
	} else {
		struct p2p_device *dev;
		int for_join = 0;

		dl_list_for_each(dev, &p2p->devices, struct p2p_device, list) {
			if (os_memcmp(p2p->pending_pd_devaddr,
				      dev->info.p2p_device_addr, ETH_ALEN) != 0)
				continue;
			if (dev->req_config_methods &&
			    (dev->flags & P2P_DEV_PD_FOR_JOIN))
				for_join = 1;
		}

		if (p2p->p2ps_prov) {
			adv_id = p2p->p2ps_prov->adv_id;
			adv_mac = p2p->p2ps_prov->adv_mac;
		}

		if (p2p->cfg->prov_disc_fail)
			p2p->cfg->prov_disc_fail(p2p->cfg->cb_ctx,
						 p2p->pending_pd_devaddr,
						 for_join ?
						 P2P_PROV_DISC_TIMEOUT_JOIN :
						 P2P_PROV_DISC_TIMEOUT,
						 adv_id, adv_mac, NULL);
		p2p_reset_pending_pd(p2p);
	}
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
		p2p_dbg(p2p, "Inviting in active GO role - wait on operating channel");
		p2p_set_timeout(p2p, 0, 100000);
		return;
	}
	p2p_listen_in_find(p2p, 0);
}


static void p2p_timeout_invite_listen(struct p2p_data *p2p)
{
	if (p2p->invite_peer && p2p->invite_peer->invitation_reqs < 100) {
		p2p_set_state(p2p, P2P_INVITE);
		p2p_invite_send(p2p, p2p->invite_peer,
				p2p->invite_go_dev_addr, p2p->invite_dev_pw_id);
	} else {
		if (p2p->invite_peer) {
			p2p_dbg(p2p, "Invitation Request retry limit reached");
			if (p2p->cfg->invitation_result)
				p2p->cfg->invitation_result(
					p2p->cfg->cb_ctx, -1, NULL, NULL,
					p2p->invite_peer->info.p2p_device_addr,
					0, 0);
		}
		p2p_set_state(p2p, P2P_IDLE);
	}
}


static void p2p_state_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct p2p_data *p2p = eloop_ctx;

	p2p_dbg(p2p, "Timeout (state=%s)", p2p_state_txt(p2p->state));

	p2p->in_listen = 0;
	if (p2p->drv_in_listen) {
		p2p_dbg(p2p, "Driver is still in listen state - stop it");
		p2p->cfg->stop_listen(p2p->cfg->cb_ctx);
	}

	switch (p2p->state) {
	case P2P_IDLE:
		/* Check if we timed out waiting for PD req */
		if (p2p->pending_action_state == P2P_PENDING_PD)
			p2p_timeout_prov_disc_req(p2p);
		break;
	case P2P_SEARCH:
		/* Check if we timed out waiting for PD req */
		if (p2p->pending_action_state == P2P_PENDING_PD)
			p2p_timeout_prov_disc_req(p2p);
		if (p2p->search_delay && !p2p->in_search_delay) {
			p2p_dbg(p2p, "Delay search operation by %u ms",
				p2p->search_delay);
			p2p->in_search_delay = 1;
			p2p_set_timeout(p2p, p2p->search_delay / 1000,
					(p2p->search_delay % 1000) * 1000);
			break;
		}
		p2p->in_search_delay = 0;
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
		/* Check if we timed out waiting for PD req */
		if (p2p->pending_action_state == P2P_PENDING_PD)
			p2p_timeout_prov_disc_req(p2p);

		if (p2p->ext_listen_only) {
			p2p_dbg(p2p, "Extended Listen Timing - Listen State completed");
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
	p2p_dbg(p2p, "Local request to reject connection attempts by peer "
		MACSTR, MAC2STR(peer_addr));
	if (dev == NULL) {
		p2p_dbg(p2p, "Peer " MACSTR " unknown", MAC2STR(peer_addr));
		return -1;
	}
	dev->status = P2P_SC_FAIL_REJECTED_BY_USER;
	dev->flags |= P2P_DEV_USER_REJECTED;
	return 0;
}


const char * p2p_wps_method_text(enum p2p_wps_method method)
{
	switch (method) {
	case WPS_NOT_READY:
		return "not-ready";
	case WPS_PIN_DISPLAY:
		return "Display";
	case WPS_PIN_KEYPAD:
		return "Keypad";
	case WPS_PBC:
		return "PBC";
	case WPS_NFC:
		return "NFC";
	case WPS_P2PS:
		return "P2PS";
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


const struct p2p_peer_info * p2p_get_peer_info(struct p2p_data *p2p,
					       const u8 *addr, int next)
{
	struct p2p_device *dev;

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
		return NULL;

	return &dev->info;
}


int p2p_get_peer_info_txt(const struct p2p_peer_info *info,
			  char *buf, size_t buflen)
{
	struct p2p_device *dev;
	int res;
	char *pos, *end;
	struct os_reltime now;

	if (info == NULL)
		return -1;

	dev = (struct p2p_device *) (((u8 *) info) -
				     offsetof(struct p2p_device, info));

	pos = buf;
	end = buf + buflen;

	os_get_reltime(&now);
	res = os_snprintf(pos, end - pos,
			  "age=%d\n"
			  "listen_freq=%d\n"
			  "wps_method=%s\n"
			  "interface_addr=" MACSTR "\n"
			  "member_in_go_dev=" MACSTR "\n"
			  "member_in_go_iface=" MACSTR "\n"
			  "go_neg_req_sent=%d\n"
			  "go_state=%s\n"
			  "dialog_token=%u\n"
			  "intended_addr=" MACSTR "\n"
			  "country=%c%c\n"
			  "oper_freq=%d\n"
			  "req_config_methods=0x%x\n"
			  "flags=%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n"
			  "status=%d\n"
			  "invitation_reqs=%u\n",
			  (int) (now.sec - dev->last_seen.sec),
			  dev->listen_freq,
			  p2p_wps_method_text(dev->wps_method),
			  MAC2STR(dev->interface_addr),
			  MAC2STR(dev->member_in_go_dev),
			  MAC2STR(dev->member_in_go_iface),
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
			  dev->flags & P2P_DEV_PD_PEER_DISPLAY ?
			  "[PD_PEER_DISPLAY]" : "",
			  dev->flags & P2P_DEV_PD_PEER_KEYPAD ?
			  "[PD_PEER_KEYPAD]" : "",
			  dev->flags & P2P_DEV_PD_PEER_P2PS ?
			  "[PD_PEER_P2PS]" : "",
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
			  dev->flags & P2P_DEV_LAST_SEEN_AS_GROUP_CLIENT ?
			  "[LAST_SEEN_AS_GROUP_CLIENT]" : "",
			  dev->status,
			  dev->invitation_reqs);
	if (os_snprintf_error(end - pos, res))
		return pos - buf;
	pos += res;

	if (dev->ext_listen_period) {
		res = os_snprintf(pos, end - pos,
				  "ext_listen_period=%u\n"
				  "ext_listen_interval=%u\n",
				  dev->ext_listen_period,
				  dev->ext_listen_interval);
		if (os_snprintf_error(end - pos, res))
			return pos - buf;
		pos += res;
	}

	if (dev->oper_ssid_len) {
		res = os_snprintf(pos, end - pos,
				  "oper_ssid=%s\n",
				  wpa_ssid_txt(dev->oper_ssid,
					       dev->oper_ssid_len));
		if (os_snprintf_error(end - pos, res))
			return pos - buf;
		pos += res;
	}

#ifdef CONFIG_WIFI_DISPLAY
	if (dev->info.wfd_subelems) {
		res = os_snprintf(pos, end - pos, "wfd_subelems=");
		if (os_snprintf_error(end - pos, res))
			return pos - buf;
		pos += res;

		pos += wpa_snprintf_hex(pos, end - pos,
					wpabuf_head(dev->info.wfd_subelems),
					wpabuf_len(dev->info.wfd_subelems));

		res = os_snprintf(pos, end - pos, "\n");
		if (os_snprintf_error(end - pos, res))
			return pos - buf;
		pos += res;
	}
#endif /* CONFIG_WIFI_DISPLAY */

	return pos - buf;
}


int p2p_peer_known(struct p2p_data *p2p, const u8 *addr)
{
	return p2p_get_device(p2p, addr) != NULL;
}


void p2p_set_client_discoverability(struct p2p_data *p2p, int enabled)
{
	if (enabled) {
		p2p_dbg(p2p, "Client discoverability enabled");
		p2p->dev_capab |= P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY;
	} else {
		p2p_dbg(p2p, "Client discoverability disabled");
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

	p2p_dbg(p2p, "Send Presence Request to GO " MACSTR
		" (own interface " MACSTR ") freq=%u dur1=%u int1=%u "
		"dur2=%u int2=%u",
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
		p2p_dbg(p2p, "Failed to send Action frame");
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

	p2p_dbg(p2p, "Received P2P Action - P2P Presence Request");

	for (g = 0; g < p2p->num_groups; g++) {
		if (os_memcmp(da, p2p_group_get_interface_addr(p2p->groups[g]),
			      ETH_ALEN) == 0) {
			group = p2p->groups[g];
			break;
		}
	}
	if (group == NULL) {
		p2p_dbg(p2p, "Ignore P2P Presence Request for unknown group "
			MACSTR, MAC2STR(da));
		return;
	}

	if (p2p_parse(data, len, &msg) < 0) {
		p2p_dbg(p2p, "Failed to parse P2P Presence Request");
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
	}
	parsed = 1;

	if (msg.noa == NULL) {
		p2p_dbg(p2p, "No NoA attribute in P2P Presence Request");
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
		p2p_dbg(p2p, "Failed to send Action frame");
	}
	wpabuf_free(resp);
}


static void p2p_process_presence_resp(struct p2p_data *p2p, const u8 *da,
				      const u8 *sa, const u8 *data, size_t len)
{
	struct p2p_message msg;

	p2p_dbg(p2p, "Received P2P Action - P2P Presence Response");

	if (p2p_parse(data, len, &msg) < 0) {
		p2p_dbg(p2p, "Failed to parse P2P Presence Response");
		return;
	}

	if (msg.status == NULL || msg.noa == NULL) {
		p2p_dbg(p2p, "No Status or NoA attribute in P2P Presence Response");
		p2p_parse_free(&msg);
		return;
	}

	if (p2p->cfg->presence_resp) {
		p2p->cfg->presence_resp(p2p->cfg->cb_ctx, sa, *msg.status,
					msg.noa, msg.noa_len);
	}

	if (*msg.status) {
		p2p_dbg(p2p, "P2P Presence Request was rejected: status %u",
			*msg.status);
		p2p_parse_free(&msg);
		return;
	}

	p2p_dbg(p2p, "P2P Presence Request was accepted");
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

	if ((p2p->cfg->is_p2p_in_progress &&
	     p2p->cfg->is_p2p_in_progress(p2p->cfg->cb_ctx)) ||
	    (p2p->pending_action_state == P2P_PENDING_PD &&
	     p2p->pd_retries > 0)) {
		p2p_dbg(p2p, "Operation in progress - skip Extended Listen timeout (%s)",
			p2p_state_txt(p2p->state));
		return;
	}

	if (p2p->state == P2P_LISTEN_ONLY && p2p->ext_listen_only) {
		/*
		 * This should not really happen, but it looks like the Listen
		 * command may fail is something else (e.g., a scan) was
		 * running at an inconvenient time. As a workaround, allow new
		 * Extended Listen operation to be started.
		 */
		p2p_dbg(p2p, "Previous Extended Listen operation had not been completed - try again");
		p2p->ext_listen_only = 0;
		p2p_set_state(p2p, P2P_IDLE);
	}

	if (p2p->state != P2P_IDLE) {
		p2p_dbg(p2p, "Skip Extended Listen timeout in active state (%s)", p2p_state_txt(p2p->state));
		return;
	}

	p2p_dbg(p2p, "Extended Listen timeout");
	p2p->ext_listen_only = 1;
	if (p2p_listen(p2p, p2p->ext_listen_period) < 0) {
		p2p_dbg(p2p, "Failed to start Listen state for Extended Listen Timing");
		p2p->ext_listen_only = 0;
	}
}


int p2p_ext_listen(struct p2p_data *p2p, unsigned int period,
		   unsigned int interval)
{
	if (period > 65535 || interval > 65535 || period > interval ||
	    (period == 0 && interval > 0) || (period > 0 && interval == 0)) {
		p2p_dbg(p2p, "Invalid Extended Listen Timing request: period=%u interval=%u",
			period, interval);
		return -1;
	}

	eloop_cancel_timeout(p2p_ext_listen_timeout, p2p, NULL);

	if (interval == 0) {
		p2p_dbg(p2p, "Disabling Extended Listen Timing");
		p2p->ext_listen_period = 0;
		p2p->ext_listen_interval = 0;
		return 0;
	}

	p2p_dbg(p2p, "Enabling Extended Listen Timing: period %u msec, interval %u msec",
		period, interval);
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
	if (msg.minor_reason_code == NULL) {
		p2p_parse_free(&msg);
		return;
	}

	p2p_dbg(p2p, "Deauthentication notification BSSID " MACSTR
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
	if (msg.minor_reason_code == NULL) {
		p2p_parse_free(&msg);
		return;
	}

	p2p_dbg(p2p, "Disassociation notification BSSID " MACSTR
		" reason_code=%u minor_reason_code=%u",
		MAC2STR(bssid), reason_code, *msg.minor_reason_code);

	p2p_parse_free(&msg);
}


void p2p_set_managed_oper(struct p2p_data *p2p, int enabled)
{
	if (enabled) {
		p2p_dbg(p2p, "Managed P2P Device operations enabled");
		p2p->dev_capab |= P2P_DEV_CAPAB_INFRA_MANAGED;
	} else {
		p2p_dbg(p2p, "Managed P2P Device operations disabled");
		p2p->dev_capab &= ~P2P_DEV_CAPAB_INFRA_MANAGED;
	}
}


int p2p_config_get_random_social(struct p2p_config *p2p, u8 *op_class,
				 u8 *op_channel)
{
	return p2p_channel_random_social(&p2p->channels, op_class, op_channel);
}


int p2p_set_listen_channel(struct p2p_data *p2p, u8 reg_class, u8 channel,
			   u8 forced)
{
	if (p2p_channel_to_freq(reg_class, channel) < 0)
		return -1;

	/*
	 * Listen channel was set in configuration or set by control interface;
	 * cannot override it.
	 */
	if (p2p->cfg->channel_forced && forced == 0) {
		p2p_dbg(p2p,
			"Listen channel was previously configured - do not override based on optimization");
		return -1;
	}

	p2p_dbg(p2p, "Set Listen channel: reg_class %u channel %u",
		reg_class, channel);

	if (p2p->state == P2P_IDLE) {
		p2p->cfg->reg_class = reg_class;
		p2p->cfg->channel = channel;
		p2p->cfg->channel_forced = forced;
	} else {
		p2p_dbg(p2p, "Defer setting listen channel");
		p2p->pending_reg_class = reg_class;
		p2p->pending_channel = channel;
		p2p->pending_channel_forced = forced;
	}

	return 0;
}


u8 p2p_get_listen_channel(struct p2p_data *p2p)
{
	return p2p->cfg->channel;
}


int p2p_set_ssid_postfix(struct p2p_data *p2p, const u8 *postfix, size_t len)
{
	p2p_dbg(p2p, "New SSID postfix: %s", wpa_ssid_txt(postfix, len));
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


int p2p_set_oper_channel(struct p2p_data *p2p, u8 op_reg_class, u8 op_channel,
			 int cfg_op_channel)
{
	if (p2p_channel_to_freq(op_reg_class, op_channel) < 0)
		return -1;

	p2p_dbg(p2p, "Set Operating channel: reg_class %u channel %u",
		op_reg_class, op_channel);
	p2p->cfg->op_reg_class = op_reg_class;
	p2p->cfg->op_channel = op_channel;
	p2p->cfg->cfg_op_channel = cfg_op_channel;
	return 0;
}


int p2p_set_pref_chan(struct p2p_data *p2p, unsigned int num_pref_chan,
		      const struct p2p_channel *pref_chan)
{
	struct p2p_channel *n;

	if (pref_chan) {
		n = os_memdup(pref_chan,
			      num_pref_chan * sizeof(struct p2p_channel));
		if (n == NULL)
			return -1;
	} else
		n = NULL;

	os_free(p2p->cfg->pref_chan);
	p2p->cfg->pref_chan = n;
	p2p->cfg->num_pref_chan = num_pref_chan;

	return 0;
}


int p2p_set_no_go_freq(struct p2p_data *p2p,
		       const struct wpa_freq_range_list *list)
{
	struct wpa_freq_range *tmp;

	if (list == NULL || list->num == 0) {
		os_free(p2p->no_go_freq.range);
		p2p->no_go_freq.range = NULL;
		p2p->no_go_freq.num = 0;
		return 0;
	}

	tmp = os_calloc(list->num, sizeof(struct wpa_freq_range));
	if (tmp == NULL)
		return -1;
	os_memcpy(tmp, list->range, list->num * sizeof(struct wpa_freq_range));
	os_free(p2p->no_go_freq.range);
	p2p->no_go_freq.range = tmp;
	p2p->no_go_freq.num = list->num;
	p2p_dbg(p2p, "Updated no GO chan list");

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
		p2p_dbg(p2p, "Disable peer filter");
	else
		p2p_dbg(p2p, "Enable peer filter for " MACSTR,
			MAC2STR(p2p->peer_filter));
}


void p2p_set_cross_connect(struct p2p_data *p2p, int enabled)
{
	p2p_dbg(p2p, "Cross connection %s", enabled ? "enabled" : "disabled");
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
	p2p_dbg(p2p, "Intra BSS distribution %s",
		enabled ? "enabled" : "disabled");
	p2p->cfg->p2p_intra_bss = enabled;
}


void p2p_update_channel_list(struct p2p_data *p2p,
			     const struct p2p_channels *chan,
			     const struct p2p_channels *cli_chan)
{
	p2p_dbg(p2p, "Update channel list");
	os_memcpy(&p2p->cfg->channels, chan, sizeof(struct p2p_channels));
	p2p_channels_dump(p2p, "channels", &p2p->cfg->channels);
	os_memcpy(&p2p->cfg->cli_channels, cli_chan,
		  sizeof(struct p2p_channels));
	p2p_channels_dump(p2p, "cli_channels", &p2p->cfg->cli_channels);
}


int p2p_send_action(struct p2p_data *p2p, unsigned int freq, const u8 *dst,
		    const u8 *src, const u8 *bssid, const u8 *buf,
		    size_t len, unsigned int wait_time)
{
	if (p2p->p2p_scan_running) {
		p2p_dbg(p2p, "Delay Action frame TX until p2p_scan completes");
		if (p2p->after_scan_tx) {
			p2p_dbg(p2p, "Dropped previous pending Action frame TX");
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
	p2p_dbg(p2p, "Best channel: 2.4 GHz: %d,  5 GHz: %d,  overall: %d",
		freq_24, freq_5, freq_overall);
	p2p->best_freq_24 = freq_24;
	p2p->best_freq_5 = freq_5;
	p2p->best_freq_overall = freq_overall;
}


void p2p_set_own_freq_preference(struct p2p_data *p2p, int freq)
{
	p2p_dbg(p2p, "Own frequency preference: %d MHz", freq);
	p2p->own_freq_preference = freq;
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
				if (!dev || &dev->list == &p2p->devices)
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
			if (!dev || &dev->list == &p2p->devices)
				return NULL;
		}
	}

	return &dev->info;
}


int p2p_in_progress(struct p2p_data *p2p)
{
	if (p2p == NULL)
		return 0;
	if (p2p->state == P2P_SEARCH)
		return 2;
	return p2p->state != P2P_IDLE && p2p->state != P2P_PROVISIONING;
}


void p2p_set_config_timeout(struct p2p_data *p2p, u8 go_timeout,
			    u8 client_timeout)
{
	if (p2p) {
		p2p->go_timeout = go_timeout;
		p2p->client_timeout = client_timeout;
	}
}


#ifdef CONFIG_WIFI_DISPLAY

static void p2p_update_wfd_ie_groups(struct p2p_data *p2p)
{
	size_t g;
	struct p2p_group *group;

	for (g = 0; g < p2p->num_groups; g++) {
		group = p2p->groups[g];
		p2p_group_force_beacon_update_ies(group);
	}
}


int p2p_set_wfd_ie_beacon(struct p2p_data *p2p, struct wpabuf *ie)
{
	wpabuf_free(p2p->wfd_ie_beacon);
	p2p->wfd_ie_beacon = ie;
	p2p_update_wfd_ie_groups(p2p);
	return 0;
}


int p2p_set_wfd_ie_probe_req(struct p2p_data *p2p, struct wpabuf *ie)
{
	wpabuf_free(p2p->wfd_ie_probe_req);
	p2p->wfd_ie_probe_req = ie;
	return 0;
}


int p2p_set_wfd_ie_probe_resp(struct p2p_data *p2p, struct wpabuf *ie)
{
	wpabuf_free(p2p->wfd_ie_probe_resp);
	p2p->wfd_ie_probe_resp = ie;
	p2p_update_wfd_ie_groups(p2p);
	return 0;
}


int p2p_set_wfd_ie_assoc_req(struct p2p_data *p2p, struct wpabuf *ie)
{
	wpabuf_free(p2p->wfd_ie_assoc_req);
	p2p->wfd_ie_assoc_req = ie;
	return 0;
}


int p2p_set_wfd_ie_invitation(struct p2p_data *p2p, struct wpabuf *ie)
{
	wpabuf_free(p2p->wfd_ie_invitation);
	p2p->wfd_ie_invitation = ie;
	return 0;
}


int p2p_set_wfd_ie_prov_disc_req(struct p2p_data *p2p, struct wpabuf *ie)
{
	wpabuf_free(p2p->wfd_ie_prov_disc_req);
	p2p->wfd_ie_prov_disc_req = ie;
	return 0;
}


int p2p_set_wfd_ie_prov_disc_resp(struct p2p_data *p2p, struct wpabuf *ie)
{
	wpabuf_free(p2p->wfd_ie_prov_disc_resp);
	p2p->wfd_ie_prov_disc_resp = ie;
	return 0;
}


int p2p_set_wfd_ie_go_neg(struct p2p_data *p2p, struct wpabuf *ie)
{
	wpabuf_free(p2p->wfd_ie_go_neg);
	p2p->wfd_ie_go_neg = ie;
	return 0;
}


int p2p_set_wfd_dev_info(struct p2p_data *p2p, const struct wpabuf *elem)
{
	wpabuf_free(p2p->wfd_dev_info);
	if (elem) {
		p2p->wfd_dev_info = wpabuf_dup(elem);
		if (p2p->wfd_dev_info == NULL)
			return -1;
	} else
		p2p->wfd_dev_info = NULL;

	return 0;
}


int p2p_set_wfd_r2_dev_info(struct p2p_data *p2p, const struct wpabuf *elem)
{
	wpabuf_free(p2p->wfd_r2_dev_info);
	if (elem) {
		p2p->wfd_r2_dev_info = wpabuf_dup(elem);
		if (p2p->wfd_r2_dev_info == NULL)
			return -1;
	} else
		p2p->wfd_r2_dev_info = NULL;

	return 0;
}


int p2p_set_wfd_assoc_bssid(struct p2p_data *p2p, const struct wpabuf *elem)
{
	wpabuf_free(p2p->wfd_assoc_bssid);
	if (elem) {
		p2p->wfd_assoc_bssid = wpabuf_dup(elem);
		if (p2p->wfd_assoc_bssid == NULL)
			return -1;
	} else
		p2p->wfd_assoc_bssid = NULL;

	return 0;
}


int p2p_set_wfd_coupled_sink_info(struct p2p_data *p2p,
				  const struct wpabuf *elem)
{
	wpabuf_free(p2p->wfd_coupled_sink_info);
	if (elem) {
		p2p->wfd_coupled_sink_info = wpabuf_dup(elem);
		if (p2p->wfd_coupled_sink_info == NULL)
			return -1;
	} else
		p2p->wfd_coupled_sink_info = NULL;

	return 0;
}

#endif /* CONFIG_WIFI_DISPLAY */


int p2p_set_disc_int(struct p2p_data *p2p, int min_disc_int, int max_disc_int,
		     int max_disc_tu)
{
	if (min_disc_int > max_disc_int || min_disc_int < 0 || max_disc_int < 0)
		return -1;

	p2p->min_disc_int = min_disc_int;
	p2p->max_disc_int = max_disc_int;
	p2p->max_disc_tu = max_disc_tu;
	p2p_dbg(p2p, "Set discoverable interval: min=%d max=%d max_tu=%d",
		min_disc_int, max_disc_int, max_disc_tu);

	return 0;
}


void p2p_dbg(struct p2p_data *p2p, const char *fmt, ...)
{
	va_list ap;
	char buf[500];

	if (!p2p->cfg->debug_print)
		return;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	buf[sizeof(buf) - 1] = '\0';
	va_end(ap);
	p2p->cfg->debug_print(p2p->cfg->cb_ctx, MSG_DEBUG, buf);
}


void p2p_info(struct p2p_data *p2p, const char *fmt, ...)
{
	va_list ap;
	char buf[500];

	if (!p2p->cfg->debug_print)
		return;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	buf[sizeof(buf) - 1] = '\0';
	va_end(ap);
	p2p->cfg->debug_print(p2p->cfg->cb_ctx, MSG_INFO, buf);
}


void p2p_err(struct p2p_data *p2p, const char *fmt, ...)
{
	va_list ap;
	char buf[500];

	if (!p2p->cfg->debug_print)
		return;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	buf[sizeof(buf) - 1] = '\0';
	va_end(ap);
	p2p->cfg->debug_print(p2p->cfg->cb_ctx, MSG_ERROR, buf);
}


void p2p_loop_on_known_peers(struct p2p_data *p2p,
			     void (*peer_callback)(struct p2p_peer_info *peer,
						   void *user_data),
			     void *user_data)
{
	struct p2p_device *dev, *n;

	dl_list_for_each_safe(dev, n, &p2p->devices, struct p2p_device, list) {
		peer_callback(&dev->info, user_data);
	}
}


#ifdef CONFIG_WPS_NFC

static struct wpabuf * p2p_build_nfc_handover(struct p2p_data *p2p,
					      int client_freq,
					      const u8 *go_dev_addr,
					      const u8 *ssid, size_t ssid_len)
{
	struct wpabuf *buf;
	u8 op_class, channel;
	enum p2p_role_indication role = P2P_DEVICE_NOT_IN_GROUP;

	buf = wpabuf_alloc(1000);
	if (buf == NULL)
		return NULL;

	op_class = p2p->cfg->reg_class;
	channel = p2p->cfg->channel;

	p2p_buf_add_capability(buf, p2p->dev_capab &
			       ~P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY, 0);
	p2p_buf_add_device_info(buf, p2p, NULL);

	if (p2p->num_groups > 0) {
		int freq = p2p_group_get_freq(p2p->groups[0]);
		role = P2P_GO_IN_A_GROUP;
		if (p2p_freq_to_channel(freq, &op_class, &channel) < 0) {
			p2p_dbg(p2p,
				"Unknown GO operating frequency %d MHz for NFC handover",
				freq);
			wpabuf_free(buf);
			return NULL;
		}
	} else if (client_freq > 0) {
		role = P2P_CLIENT_IN_A_GROUP;
		if (p2p_freq_to_channel(client_freq, &op_class, &channel) < 0) {
			p2p_dbg(p2p,
				"Unknown client operating frequency %d MHz for NFC handover",
				client_freq);
			wpabuf_free(buf);
			return NULL;
		}
	}

	p2p_buf_add_oob_go_neg_channel(buf, p2p->cfg->country, op_class,
				       channel, role);

	if (p2p->num_groups > 0) {
		/* Limit number of clients to avoid very long message */
		p2p_buf_add_group_info(p2p->groups[0], buf, 5);
		p2p_group_buf_add_id(p2p->groups[0], buf);
	} else if (client_freq > 0 &&
		   go_dev_addr && !is_zero_ether_addr(go_dev_addr) &&
		   ssid && ssid_len > 0) {
		/*
		 * Add the optional P2P Group ID to indicate in which group this
		 * device is a P2P Client.
		 */
		p2p_buf_add_group_id(buf, go_dev_addr, ssid, ssid_len);
	}

	return buf;
}


struct wpabuf * p2p_build_nfc_handover_req(struct p2p_data *p2p,
					   int client_freq,
					   const u8 *go_dev_addr,
					   const u8 *ssid, size_t ssid_len)
{
	return p2p_build_nfc_handover(p2p, client_freq, go_dev_addr, ssid,
				      ssid_len);
}


struct wpabuf * p2p_build_nfc_handover_sel(struct p2p_data *p2p,
					   int client_freq,
					   const u8 *go_dev_addr,
					   const u8 *ssid, size_t ssid_len)
{
	return p2p_build_nfc_handover(p2p, client_freq, go_dev_addr, ssid,
				      ssid_len);
}


int p2p_process_nfc_connection_handover(struct p2p_data *p2p,
					struct p2p_nfc_params *params)
{
	struct p2p_message msg;
	struct p2p_device *dev;
	const u8 *p2p_dev_addr;
	int freq;
	enum p2p_role_indication role;

	params->next_step = NO_ACTION;

	if (p2p_parse_ies_separate(params->wsc_attr, params->wsc_len,
				   params->p2p_attr, params->p2p_len, &msg)) {
		p2p_dbg(p2p, "Failed to parse WSC/P2P attributes from NFC");
		p2p_parse_free(&msg);
		return -1;
	}

	if (msg.p2p_device_addr)
		p2p_dev_addr = msg.p2p_device_addr;
	else if (msg.device_id)
		p2p_dev_addr = msg.device_id;
	else {
		p2p_dbg(p2p, "Ignore scan data without P2P Device Info or P2P Device Id");
		p2p_parse_free(&msg);
		return -1;
	}

	if (msg.oob_dev_password) {
		os_memcpy(params->oob_dev_pw, msg.oob_dev_password,
			  msg.oob_dev_password_len);
		params->oob_dev_pw_len = msg.oob_dev_password_len;
	}

	dev = p2p_create_device(p2p, p2p_dev_addr);
	if (dev == NULL) {
		p2p_parse_free(&msg);
		return -1;
	}

	params->peer = &dev->info;

	os_get_reltime(&dev->last_seen);
	dev->flags &= ~(P2P_DEV_PROBE_REQ_ONLY | P2P_DEV_GROUP_CLIENT_ONLY);
	p2p_copy_wps_info(p2p, dev, 0, &msg);

	if (!msg.oob_go_neg_channel) {
		p2p_dbg(p2p, "OOB GO Negotiation Channel attribute not included");
		p2p_parse_free(&msg);
		return -1;
	}

	if (msg.oob_go_neg_channel[3] == 0 &&
	    msg.oob_go_neg_channel[4] == 0)
		freq = 0;
	else
		freq = p2p_channel_to_freq(msg.oob_go_neg_channel[3],
					   msg.oob_go_neg_channel[4]);
	if (freq < 0) {
		p2p_dbg(p2p, "Unknown peer OOB GO Neg channel");
		p2p_parse_free(&msg);
		return -1;
	}
	role = msg.oob_go_neg_channel[5];

	if (role == P2P_GO_IN_A_GROUP) {
		p2p_dbg(p2p, "Peer OOB GO operating channel: %u MHz", freq);
		params->go_freq = freq;
	} else if (role == P2P_CLIENT_IN_A_GROUP) {
		p2p_dbg(p2p, "Peer (client) OOB GO operating channel: %u MHz",
			freq);
		params->go_freq = freq;
	} else
		p2p_dbg(p2p, "Peer OOB GO Neg channel: %u MHz", freq);
	dev->oob_go_neg_freq = freq;

	if (!params->sel && role != P2P_GO_IN_A_GROUP) {
		freq = p2p_channel_to_freq(p2p->cfg->reg_class,
					   p2p->cfg->channel);
		if (freq < 0) {
			p2p_dbg(p2p, "Own listen channel not known");
			p2p_parse_free(&msg);
			return -1;
		}
		p2p_dbg(p2p, "Use own Listen channel as OOB GO Neg channel: %u MHz", freq);
		dev->oob_go_neg_freq = freq;
	}

	if (msg.group_id) {
		os_memcpy(params->go_dev_addr, msg.group_id, ETH_ALEN);
		params->go_ssid_len = msg.group_id_len - ETH_ALEN;
		os_memcpy(params->go_ssid, msg.group_id + ETH_ALEN,
			  params->go_ssid_len);
	}

	if (dev->flags & P2P_DEV_USER_REJECTED) {
		p2p_dbg(p2p, "Do not report rejected device");
		p2p_parse_free(&msg);
		return 0;
	}

	if (!(dev->flags & P2P_DEV_REPORTED)) {
		p2p->cfg->dev_found(p2p->cfg->cb_ctx, p2p_dev_addr, &dev->info,
				    !(dev->flags & P2P_DEV_REPORTED_ONCE));
		dev->flags |= P2P_DEV_REPORTED | P2P_DEV_REPORTED_ONCE;
	}
	p2p_parse_free(&msg);

	if (role == P2P_GO_IN_A_GROUP && p2p->num_groups > 0)
		params->next_step = BOTH_GO;
	else if (role == P2P_GO_IN_A_GROUP)
		params->next_step = JOIN_GROUP;
	else if (role == P2P_CLIENT_IN_A_GROUP) {
		dev->flags |= P2P_DEV_GROUP_CLIENT_ONLY;
		params->next_step = PEER_CLIENT;
	} else if (p2p->num_groups > 0)
		params->next_step = AUTH_JOIN;
	else if (params->sel)
		params->next_step = INIT_GO_NEG;
	else
		params->next_step = RESP_GO_NEG;

	return 0;
}


void p2p_set_authorized_oob_dev_pw_id(struct p2p_data *p2p, u16 dev_pw_id,
				      int go_intent,
				      const u8 *own_interface_addr)
{

	p2p->authorized_oob_dev_pw_id = dev_pw_id;
	if (dev_pw_id == 0) {
		p2p_dbg(p2p, "NFC OOB Password unauthorized for static handover");
		return;
	}

	p2p_dbg(p2p, "NFC OOB Password (id=%u) authorized for static handover",
		dev_pw_id);

	p2p->go_intent = go_intent;
	os_memcpy(p2p->intended_addr, own_interface_addr, ETH_ALEN);
}

#endif /* CONFIG_WPS_NFC */


int p2p_set_passphrase_len(struct p2p_data *p2p, unsigned int len)
{
	if (len < 8 || len > 63)
		return -1;
	p2p->cfg->passphrase_len = len;
	return 0;
}


void p2p_set_vendor_elems(struct p2p_data *p2p, struct wpabuf **vendor_elem)
{
	p2p->vendor_elem = vendor_elem;
}


void p2p_go_neg_wait_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct p2p_data *p2p = eloop_ctx;

	p2p_dbg(p2p,
		"Timeout on waiting peer to become ready for GO Negotiation");
	p2p_go_neg_failed(p2p, -1);
}


void p2p_set_own_pref_freq_list(struct p2p_data *p2p,
				const unsigned int *pref_freq_list,
				unsigned int size)
{
	unsigned int i;

	if (size > P2P_MAX_PREF_CHANNELS)
		size = P2P_MAX_PREF_CHANNELS;
	p2p->num_pref_freq = size;
	for (i = 0; i < size; i++) {
		p2p->pref_freq_list[i] = pref_freq_list[i];
		p2p_dbg(p2p, "Own preferred frequency list[%u]=%u MHz",
			i, p2p->pref_freq_list[i]);
	}
}


void p2p_set_override_pref_op_chan(struct p2p_data *p2p, u8 op_class,
				   u8 chan)
{
	p2p->override_pref_op_class = op_class;
	p2p->override_pref_channel = chan;
}


struct wpabuf * p2p_build_probe_resp_template(struct p2p_data *p2p,
					      unsigned int freq)
{
	struct wpabuf *ies, *buf;
	u8 addr[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	int ret;

	ies = p2p_build_probe_resp_ies(p2p, NULL, 0);
	if (!ies) {
		wpa_printf(MSG_ERROR,
			   "CTRL: Failed to build Probe Response IEs");
		return NULL;
	}

	buf = wpabuf_alloc(200 + wpabuf_len(ies));
	if (!buf) {
		wpabuf_free(ies);
		return NULL;
	}

	ret = p2p_build_probe_resp_buf(p2p, buf, ies, addr, freq);
	wpabuf_free(ies);
	if (ret) {
		wpabuf_free(buf);
		return NULL;
	}

	return buf;
}
