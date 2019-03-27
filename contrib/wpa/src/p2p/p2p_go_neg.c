/*
 * Wi-Fi Direct - P2P Group Owner Negotiation
 * Copyright (c) 2009-2010, Atheros Communications
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "utils/eloop.h"
#include "common/ieee802_11_defs.h"
#include "common/wpa_ctrl.h"
#include "wps/wps_defs.h"
#include "p2p_i.h"
#include "p2p.h"


static int p2p_go_det(u8 own_intent, u8 peer_value)
{
	u8 peer_intent = peer_value >> 1;
	if (own_intent == peer_intent) {
		if (own_intent == P2P_MAX_GO_INTENT)
			return -1; /* both devices want to become GO */

		/* Use tie breaker bit to determine GO */
		return (peer_value & 0x01) ? 0 : 1;
	}

	return own_intent > peer_intent;
}


int p2p_peer_channels_check(struct p2p_data *p2p, struct p2p_channels *own,
			    struct p2p_device *dev,
			    const u8 *channel_list, size_t channel_list_len)
{
	const u8 *pos, *end;
	struct p2p_channels *ch;
	u8 channels;
	struct p2p_channels intersection;

	ch = &dev->channels;
	os_memset(ch, 0, sizeof(*ch));
	pos = channel_list;
	end = channel_list + channel_list_len;

	if (end - pos < 3)
		return -1;
	os_memcpy(dev->country, pos, 3);
	wpa_hexdump_ascii(MSG_DEBUG, "P2P: Peer country", pos, 3);
	if (pos[2] != 0x04 && os_memcmp(pos, p2p->cfg->country, 2) != 0) {
		p2p_info(p2p, "Mismatching country (ours=%c%c peer's=%c%c)",
			p2p->cfg->country[0], p2p->cfg->country[1],
			pos[0], pos[1]);
		return -1;
	}
	pos += 3;

	while (end - pos > 2) {
		struct p2p_reg_class *cl = &ch->reg_class[ch->reg_classes];
		cl->reg_class = *pos++;
		channels = *pos++;
		if (channels > end - pos) {
			p2p_info(p2p, "Invalid peer Channel List");
			return -1;
		}
		cl->channels = channels > P2P_MAX_REG_CLASS_CHANNELS ?
			P2P_MAX_REG_CLASS_CHANNELS : channels;
		os_memcpy(cl->channel, pos, cl->channels);
		pos += channels;
		ch->reg_classes++;
		if (ch->reg_classes == P2P_MAX_REG_CLASSES)
			break;
	}

	p2p_channels_intersect(own, &dev->channels, &intersection);
	p2p_dbg(p2p, "Own reg_classes %d peer reg_classes %d intersection reg_classes %d",
		(int) own->reg_classes,
		(int) dev->channels.reg_classes,
		(int) intersection.reg_classes);
	if (intersection.reg_classes == 0) {
		p2p_info(p2p, "No common channels found");
		return -1;
	}
	return 0;
}


static int p2p_peer_channels(struct p2p_data *p2p, struct p2p_device *dev,
			     const u8 *channel_list, size_t channel_list_len)
{
	return p2p_peer_channels_check(p2p, &p2p->channels, dev,
				       channel_list, channel_list_len);
}


u16 p2p_wps_method_pw_id(enum p2p_wps_method wps_method)
{
	switch (wps_method) {
	case WPS_PIN_DISPLAY:
		return DEV_PW_REGISTRAR_SPECIFIED;
	case WPS_PIN_KEYPAD:
		return DEV_PW_USER_SPECIFIED;
	case WPS_PBC:
		return DEV_PW_PUSHBUTTON;
	case WPS_NFC:
		return DEV_PW_NFC_CONNECTION_HANDOVER;
	case WPS_P2PS:
		return DEV_PW_P2PS_DEFAULT;
	default:
		return DEV_PW_DEFAULT;
	}
}


static const char * p2p_wps_method_str(enum p2p_wps_method wps_method)
{
	switch (wps_method) {
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
	default:
		return "??";
	}
}


static struct wpabuf * p2p_build_go_neg_req(struct p2p_data *p2p,
					    struct p2p_device *peer)
{
	struct wpabuf *buf;
	u8 *len;
	u8 group_capab;
	size_t extra = 0;
	u16 pw_id;

#ifdef CONFIG_WIFI_DISPLAY
	if (p2p->wfd_ie_go_neg)
		extra = wpabuf_len(p2p->wfd_ie_go_neg);
#endif /* CONFIG_WIFI_DISPLAY */

	if (p2p->vendor_elem && p2p->vendor_elem[VENDOR_ELEM_P2P_GO_NEG_REQ])
		extra += wpabuf_len(p2p->vendor_elem[VENDOR_ELEM_P2P_GO_NEG_REQ]);

	buf = wpabuf_alloc(1000 + extra);
	if (buf == NULL)
		return NULL;

	p2p_buf_add_public_action_hdr(buf, P2P_GO_NEG_REQ, peer->dialog_token);

	len = p2p_buf_add_ie_hdr(buf);
	group_capab = 0;
	if (peer->flags & P2P_DEV_PREFER_PERSISTENT_GROUP) {
		group_capab |= P2P_GROUP_CAPAB_PERSISTENT_GROUP;
		if (peer->flags & P2P_DEV_PREFER_PERSISTENT_RECONN)
			group_capab |= P2P_GROUP_CAPAB_PERSISTENT_RECONN;
	}
	if (p2p->cross_connect)
		group_capab |= P2P_GROUP_CAPAB_CROSS_CONN;
	if (p2p->cfg->p2p_intra_bss)
		group_capab |= P2P_GROUP_CAPAB_INTRA_BSS_DIST;
	p2p_buf_add_capability(buf, p2p->dev_capab &
			       ~P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY,
			       group_capab);
	p2p_buf_add_go_intent(buf, (p2p->go_intent << 1) | peer->tie_breaker);
	p2p_buf_add_config_timeout(buf, p2p->go_timeout, p2p->client_timeout);
	p2p_buf_add_listen_channel(buf, p2p->cfg->country, p2p->cfg->reg_class,
				   p2p->cfg->channel);
	if (p2p->ext_listen_interval)
		p2p_buf_add_ext_listen_timing(buf, p2p->ext_listen_period,
					      p2p->ext_listen_interval);
	p2p_buf_add_intended_addr(buf, p2p->intended_addr);
	p2p_buf_add_channel_list(buf, p2p->cfg->country, &p2p->channels);
	p2p_buf_add_device_info(buf, p2p, peer);
	p2p_buf_add_operating_channel(buf, p2p->cfg->country,
				      p2p->op_reg_class, p2p->op_channel);
	p2p_buf_update_ie_hdr(buf, len);

	p2p_buf_add_pref_channel_list(buf, p2p->pref_freq_list,
				      p2p->num_pref_freq);

	/* WPS IE with Device Password ID attribute */
	pw_id = p2p_wps_method_pw_id(peer->wps_method);
	if (peer->oob_pw_id)
		pw_id = peer->oob_pw_id;
	if (p2p_build_wps_ie(p2p, buf, pw_id, 0) < 0) {
		p2p_dbg(p2p, "Failed to build WPS IE for GO Negotiation Request");
		wpabuf_free(buf);
		return NULL;
	}

#ifdef CONFIG_WIFI_DISPLAY
	if (p2p->wfd_ie_go_neg)
		wpabuf_put_buf(buf, p2p->wfd_ie_go_neg);
#endif /* CONFIG_WIFI_DISPLAY */

	if (p2p->vendor_elem && p2p->vendor_elem[VENDOR_ELEM_P2P_GO_NEG_REQ])
		wpabuf_put_buf(buf, p2p->vendor_elem[VENDOR_ELEM_P2P_GO_NEG_REQ]);

	return buf;
}


int p2p_connect_send(struct p2p_data *p2p, struct p2p_device *dev)
{
	struct wpabuf *req;
	int freq;

	if (dev->flags & P2P_DEV_PD_BEFORE_GO_NEG) {
		u16 config_method;
		p2p_dbg(p2p, "Use PD-before-GO-Neg workaround for " MACSTR,
			MAC2STR(dev->info.p2p_device_addr));
		if (dev->wps_method == WPS_PIN_DISPLAY)
			config_method = WPS_CONFIG_KEYPAD;
		else if (dev->wps_method == WPS_PIN_KEYPAD)
			config_method = WPS_CONFIG_DISPLAY;
		else if (dev->wps_method == WPS_PBC)
			config_method = WPS_CONFIG_PUSHBUTTON;
		else if (dev->wps_method == WPS_P2PS)
			config_method = WPS_CONFIG_P2PS;
		else
			return -1;
		return p2p_prov_disc_req(p2p, dev->info.p2p_device_addr,
					 NULL, config_method, 0, 0, 1);
	}

	freq = dev->listen_freq > 0 ? dev->listen_freq : dev->oper_freq;
	if (dev->oob_go_neg_freq > 0)
		freq = dev->oob_go_neg_freq;
	if (freq <= 0) {
		p2p_dbg(p2p, "No Listen/Operating frequency known for the peer "
			MACSTR " to send GO Negotiation Request",
			MAC2STR(dev->info.p2p_device_addr));
		return -1;
	}

	req = p2p_build_go_neg_req(p2p, dev);
	if (req == NULL)
		return -1;
	p2p_dbg(p2p, "Sending GO Negotiation Request");
	p2p_set_state(p2p, P2P_CONNECT);
	p2p->pending_action_state = P2P_PENDING_GO_NEG_REQUEST;
	p2p->go_neg_peer = dev;
	eloop_cancel_timeout(p2p_go_neg_wait_timeout, p2p, NULL);
	dev->flags |= P2P_DEV_WAIT_GO_NEG_RESPONSE;
	dev->connect_reqs++;
	if (p2p_send_action(p2p, freq, dev->info.p2p_device_addr,
			    p2p->cfg->dev_addr, dev->info.p2p_device_addr,
			    wpabuf_head(req), wpabuf_len(req), 500) < 0) {
		p2p_dbg(p2p, "Failed to send Action frame");
		/* Use P2P find to recover and retry */
		p2p_set_timeout(p2p, 0, 0);
	} else
		dev->go_neg_req_sent++;

	wpabuf_free(req);

	return 0;
}


static struct wpabuf * p2p_build_go_neg_resp(struct p2p_data *p2p,
					     struct p2p_device *peer,
					     u8 dialog_token, u8 status,
					     u8 tie_breaker)
{
	struct wpabuf *buf;
	u8 *len;
	u8 group_capab;
	size_t extra = 0;
	u16 pw_id;

	p2p_dbg(p2p, "Building GO Negotiation Response");

#ifdef CONFIG_WIFI_DISPLAY
	if (p2p->wfd_ie_go_neg)
		extra = wpabuf_len(p2p->wfd_ie_go_neg);
#endif /* CONFIG_WIFI_DISPLAY */

	if (p2p->vendor_elem && p2p->vendor_elem[VENDOR_ELEM_P2P_GO_NEG_RESP])
		extra += wpabuf_len(p2p->vendor_elem[VENDOR_ELEM_P2P_GO_NEG_RESP]);

	buf = wpabuf_alloc(1000 + extra);
	if (buf == NULL)
		return NULL;

	p2p_buf_add_public_action_hdr(buf, P2P_GO_NEG_RESP, dialog_token);

	len = p2p_buf_add_ie_hdr(buf);
	p2p_buf_add_status(buf, status);
	group_capab = 0;
	if (peer && peer->go_state == LOCAL_GO) {
		if (peer->flags & P2P_DEV_PREFER_PERSISTENT_GROUP) {
			group_capab |= P2P_GROUP_CAPAB_PERSISTENT_GROUP;
			if (peer->flags & P2P_DEV_PREFER_PERSISTENT_RECONN)
				group_capab |=
					P2P_GROUP_CAPAB_PERSISTENT_RECONN;
		}
		if (p2p->cross_connect)
			group_capab |= P2P_GROUP_CAPAB_CROSS_CONN;
		if (p2p->cfg->p2p_intra_bss)
			group_capab |= P2P_GROUP_CAPAB_INTRA_BSS_DIST;
	}
	p2p_buf_add_capability(buf, p2p->dev_capab &
			       ~P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY,
			       group_capab);
	p2p_buf_add_go_intent(buf, (p2p->go_intent << 1) | tie_breaker);
	p2p_buf_add_config_timeout(buf, p2p->go_timeout, p2p->client_timeout);
	if (p2p->override_pref_op_class) {
		p2p_dbg(p2p, "Override operating channel preference");
		p2p_buf_add_operating_channel(buf, p2p->cfg->country,
					      p2p->override_pref_op_class,
					      p2p->override_pref_channel);
	} else if (peer && peer->go_state == REMOTE_GO && !p2p->num_pref_freq) {
		p2p_dbg(p2p, "Omit Operating Channel attribute");
	} else {
		p2p_buf_add_operating_channel(buf, p2p->cfg->country,
					      p2p->op_reg_class,
					      p2p->op_channel);
	}
	p2p_buf_add_intended_addr(buf, p2p->intended_addr);
	if (status || peer == NULL) {
		p2p_buf_add_channel_list(buf, p2p->cfg->country,
					 &p2p->channels);
	} else if (peer->go_state == REMOTE_GO) {
		p2p_buf_add_channel_list(buf, p2p->cfg->country,
					 &p2p->channels);
	} else {
		struct p2p_channels res;
		p2p_channels_intersect(&p2p->channels, &peer->channels,
				       &res);
		p2p_buf_add_channel_list(buf, p2p->cfg->country, &res);
	}
	p2p_buf_add_device_info(buf, p2p, peer);
	if (peer && peer->go_state == LOCAL_GO) {
		p2p_buf_add_group_id(buf, p2p->cfg->dev_addr, p2p->ssid,
				     p2p->ssid_len);
	}
	p2p_buf_update_ie_hdr(buf, len);

	/* WPS IE with Device Password ID attribute */
	pw_id = p2p_wps_method_pw_id(peer ? peer->wps_method : WPS_NOT_READY);
	if (peer && peer->oob_pw_id)
		pw_id = peer->oob_pw_id;
	if (p2p_build_wps_ie(p2p, buf, pw_id, 0) < 0) {
		p2p_dbg(p2p, "Failed to build WPS IE for GO Negotiation Response");
		wpabuf_free(buf);
		return NULL;
	}

#ifdef CONFIG_WIFI_DISPLAY
	if (p2p->wfd_ie_go_neg)
		wpabuf_put_buf(buf, p2p->wfd_ie_go_neg);
#endif /* CONFIG_WIFI_DISPLAY */

	if (p2p->vendor_elem && p2p->vendor_elem[VENDOR_ELEM_P2P_GO_NEG_RESP])
		wpabuf_put_buf(buf, p2p->vendor_elem[VENDOR_ELEM_P2P_GO_NEG_RESP]);

	return buf;
}


/**
 * p2p_reselect_channel - Re-select operating channel based on peer information
 * @p2p: P2P module context from p2p_init()
 * @intersection: Support channel list intersection from local and peer
 *
 * This function is used to re-select the best channel after having received
 * information from the peer to allow supported channel lists to be intersected.
 * This can be used to improve initial channel selection done in
 * p2p_prepare_channel() prior to the start of GO Negotiation. In addition, this
 * can be used for Invitation case.
 */
void p2p_reselect_channel(struct p2p_data *p2p,
			  struct p2p_channels *intersection)
{
	struct p2p_reg_class *cl;
	int freq;
	u8 op_reg_class, op_channel;
	unsigned int i;
	const int op_classes_5ghz[] = { 124, 125, 115, 0 };
	const int op_classes_ht40[] = { 126, 127, 116, 117, 0 };
	const int op_classes_vht[] = { 128, 129, 130, 0 };

	if (p2p->own_freq_preference > 0 &&
	    p2p_freq_to_channel(p2p->own_freq_preference,
				&op_reg_class, &op_channel) == 0 &&
	    p2p_channels_includes(intersection, op_reg_class, op_channel)) {
		p2p_dbg(p2p, "Pick own channel preference (reg_class %u channel %u) from intersection",
			op_reg_class, op_channel);
		p2p->op_reg_class = op_reg_class;
		p2p->op_channel = op_channel;
		return;
	}

	if (p2p->best_freq_overall > 0 &&
	    p2p_freq_to_channel(p2p->best_freq_overall,
				&op_reg_class, &op_channel) == 0 &&
	    p2p_channels_includes(intersection, op_reg_class, op_channel)) {
		p2p_dbg(p2p, "Pick best overall channel (reg_class %u channel %u) from intersection",
			op_reg_class, op_channel);
		p2p->op_reg_class = op_reg_class;
		p2p->op_channel = op_channel;
		return;
	}

	/* First, try to pick the best channel from another band */
	freq = p2p_channel_to_freq(p2p->op_reg_class, p2p->op_channel);
	if (freq >= 2400 && freq < 2500 && p2p->best_freq_5 > 0 &&
	    !p2p_channels_includes(intersection, p2p->op_reg_class,
				   p2p->op_channel) &&
	    p2p_freq_to_channel(p2p->best_freq_5,
				&op_reg_class, &op_channel) == 0 &&
	    p2p_channels_includes(intersection, op_reg_class, op_channel)) {
		p2p_dbg(p2p, "Pick best 5 GHz channel (reg_class %u channel %u) from intersection",
			op_reg_class, op_channel);
		p2p->op_reg_class = op_reg_class;
		p2p->op_channel = op_channel;
		return;
	}

	if (freq >= 4900 && freq < 6000 && p2p->best_freq_24 > 0 &&
	    !p2p_channels_includes(intersection, p2p->op_reg_class,
				   p2p->op_channel) &&
	    p2p_freq_to_channel(p2p->best_freq_24,
				&op_reg_class, &op_channel) == 0 &&
	    p2p_channels_includes(intersection, op_reg_class, op_channel)) {
		p2p_dbg(p2p, "Pick best 2.4 GHz channel (reg_class %u channel %u) from intersection",
			op_reg_class, op_channel);
		p2p->op_reg_class = op_reg_class;
		p2p->op_channel = op_channel;
		return;
	}

	/* Select channel with highest preference if the peer supports it */
	for (i = 0; p2p->cfg->pref_chan && i < p2p->cfg->num_pref_chan; i++) {
		if (p2p_channels_includes(intersection,
					  p2p->cfg->pref_chan[i].op_class,
					  p2p->cfg->pref_chan[i].chan)) {
			p2p->op_reg_class = p2p->cfg->pref_chan[i].op_class;
			p2p->op_channel = p2p->cfg->pref_chan[i].chan;
			p2p_dbg(p2p, "Pick highest preferred channel (op_class %u channel %u) from intersection",
				p2p->op_reg_class, p2p->op_channel);
			return;
		}
	}

	/* Try a channel where we might be able to use VHT */
	if (p2p_channel_select(intersection, op_classes_vht,
			       &p2p->op_reg_class, &p2p->op_channel) == 0) {
		p2p_dbg(p2p, "Pick possible VHT channel (op_class %u channel %u) from intersection",
			p2p->op_reg_class, p2p->op_channel);
		return;
	}

	/* Try a channel where we might be able to use HT40 */
	if (p2p_channel_select(intersection, op_classes_ht40,
			       &p2p->op_reg_class, &p2p->op_channel) == 0) {
		p2p_dbg(p2p, "Pick possible HT40 channel (op_class %u channel %u) from intersection",
			p2p->op_reg_class, p2p->op_channel);
		return;
	}

	/* Prefer a 5 GHz channel */
	if (p2p_channel_select(intersection, op_classes_5ghz,
			       &p2p->op_reg_class, &p2p->op_channel) == 0) {
		p2p_dbg(p2p, "Pick possible 5 GHz channel (op_class %u channel %u) from intersection",
			p2p->op_reg_class, p2p->op_channel);
		return;
	}

	/*
	 * Try to see if the original channel is in the intersection. If
	 * so, no need to change anything, as it already contains some
	 * randomness.
	 */
	if (p2p_channels_includes(intersection, p2p->op_reg_class,
				  p2p->op_channel)) {
		p2p_dbg(p2p, "Using original operating class and channel (op_class %u channel %u) from intersection",
			p2p->op_reg_class, p2p->op_channel);
		return;
	}

	/*
	 * Fall back to whatever is included in the channel intersection since
	 * no better options seems to be available.
	 */
	cl = &intersection->reg_class[0];
	p2p_dbg(p2p, "Pick another channel (reg_class %u channel %u) from intersection",
		cl->reg_class, cl->channel[0]);
	p2p->op_reg_class = cl->reg_class;
	p2p->op_channel = cl->channel[0];
}


int p2p_go_select_channel(struct p2p_data *p2p, struct p2p_device *dev,
			  u8 *status)
{
	struct p2p_channels tmp, intersection;

	p2p_channels_dump(p2p, "own channels", &p2p->channels);
	p2p_channels_dump(p2p, "peer channels", &dev->channels);
	p2p_channels_intersect(&p2p->channels, &dev->channels, &tmp);
	p2p_channels_dump(p2p, "intersection", &tmp);
	p2p_channels_remove_freqs(&tmp, &p2p->no_go_freq);
	p2p_channels_dump(p2p, "intersection after no-GO removal", &tmp);
	p2p_channels_intersect(&tmp, &p2p->cfg->channels, &intersection);
	p2p_channels_dump(p2p, "intersection with local channel list",
			  &intersection);
	if (intersection.reg_classes == 0 ||
	    intersection.reg_class[0].channels == 0) {
		*status = P2P_SC_FAIL_NO_COMMON_CHANNELS;
		p2p_dbg(p2p, "No common channels found");
		return -1;
	}

	if (!p2p_channels_includes(&intersection, p2p->op_reg_class,
				   p2p->op_channel)) {
		if (dev->flags & P2P_DEV_FORCE_FREQ) {
			*status = P2P_SC_FAIL_NO_COMMON_CHANNELS;
			p2p_dbg(p2p, "Peer does not support the forced channel");
			return -1;
		}

		p2p_dbg(p2p, "Selected operating channel (op_class %u channel %u) not acceptable to the peer",
			p2p->op_reg_class, p2p->op_channel);
		p2p_reselect_channel(p2p, &intersection);
	} else if (!(dev->flags & P2P_DEV_FORCE_FREQ) &&
		   !p2p->cfg->cfg_op_channel) {
		p2p_dbg(p2p, "Try to optimize channel selection with peer information received; previously selected op_class %u channel %u",
			p2p->op_reg_class, p2p->op_channel);
		p2p_reselect_channel(p2p, &intersection);
	}

	if (!p2p->ssid_set) {
		p2p_build_ssid(p2p, p2p->ssid, &p2p->ssid_len);
		p2p->ssid_set = 1;
	}

	return 0;
}


static void p2p_check_pref_chan_no_recv(struct p2p_data *p2p, int go,
					struct p2p_device *dev,
					struct p2p_message *msg,
					unsigned freq_list[], unsigned int size)
{
	u8 op_class, op_channel;
	unsigned int oper_freq = 0, i, j;
	int found = 0;

	p2p_dbg(p2p,
		"Peer didn't provide a preferred frequency list, see if any of our preferred channels are supported by peer device");

	/*
	 * Search for a common channel in our preferred frequency list which is
	 * also supported by the peer device.
	 */
	for (i = 0; i < size && !found; i++) {
		/* Make sure that the common frequency is supported by peer. */
		oper_freq = freq_list[i];
		if (p2p_freq_to_channel(oper_freq, &op_class,
					&op_channel) < 0)
			continue; /* cannot happen due to earlier check */
		for (j = 0; j < msg->channel_list_len; j++) {

			if (op_channel != msg->channel_list[j])
				continue;

			p2p->op_reg_class = op_class;
			p2p->op_channel = op_channel;
			os_memcpy(&p2p->channels, &p2p->cfg->channels,
				  sizeof(struct p2p_channels));
			found = 1;
			break;
		}
	}

	if (found) {
		p2p_dbg(p2p,
			"Freq %d MHz is a preferred channel and is also supported by peer, use it as the operating channel",
			oper_freq);
	} else {
		p2p_dbg(p2p,
			"None of our preferred channels are supported by peer!");
	}
}


static void p2p_check_pref_chan_recv(struct p2p_data *p2p, int go,
				     struct p2p_device *dev,
				     struct p2p_message *msg,
				     unsigned freq_list[], unsigned int size)
{
	u8 op_class, op_channel;
	unsigned int oper_freq = 0, i, j;
	int found = 0;

	/*
	 * Peer device supports a Preferred Frequency List.
	 * Search for a common channel in the preferred frequency lists
	 * of both peer and local devices.
	 */
	for (i = 0; i < size && !found; i++) {
		for (j = 2; j < (msg->pref_freq_list_len / 2); j++) {
			oper_freq = p2p_channel_to_freq(
				msg->pref_freq_list[2 * j],
				msg->pref_freq_list[2 * j + 1]);
			if (freq_list[i] != oper_freq)
				continue;
			if (p2p_freq_to_channel(oper_freq, &op_class,
						&op_channel) < 0)
				continue; /* cannot happen */
			p2p->op_reg_class = op_class;
			p2p->op_channel = op_channel;
			os_memcpy(&p2p->channels, &p2p->cfg->channels,
				  sizeof(struct p2p_channels));
			found = 1;
			break;
		}
	}

	if (found) {
		p2p_dbg(p2p,
			"Freq %d MHz is a common preferred channel for both peer and local, use it as operating channel",
			oper_freq);
	} else {
		p2p_dbg(p2p, "No common preferred channels found!");
	}
}


void p2p_check_pref_chan(struct p2p_data *p2p, int go,
			 struct p2p_device *dev, struct p2p_message *msg)
{
	unsigned int freq_list[P2P_MAX_PREF_CHANNELS], size;
	unsigned int i;
	u8 op_class, op_channel;
	char txt[100], *pos, *end;
	int res;

	/*
	 * Use the preferred channel list from the driver only if there is no
	 * forced_freq, e.g., P2P_CONNECT freq=..., and no preferred operating
	 * channel hardcoded in the configuration file.
	 */
	if (!p2p->cfg->get_pref_freq_list || p2p->cfg->num_pref_chan ||
	    (dev->flags & P2P_DEV_FORCE_FREQ) || p2p->cfg->cfg_op_channel)
		return;

	/* Obtain our preferred frequency list from driver based on P2P role. */
	size = P2P_MAX_PREF_CHANNELS;
	if (p2p->cfg->get_pref_freq_list(p2p->cfg->cb_ctx, go, &size,
					 freq_list))
		return;
	/* Filter out frequencies that are not acceptable for P2P use */
	i = 0;
	while (i < size) {
		if (p2p_freq_to_channel(freq_list[i], &op_class,
					&op_channel) < 0 ||
		    (!p2p_channels_includes(&p2p->cfg->channels,
					    op_class, op_channel) &&
		     (go || !p2p_channels_includes(&p2p->cfg->cli_channels,
						   op_class, op_channel)))) {
			p2p_dbg(p2p,
				"Ignore local driver frequency preference %u MHz since it is not acceptable for P2P use (go=%d)",
				freq_list[i], go);
			if (size - i - 1 > 0)
				os_memmove(&freq_list[i], &freq_list[i + 1], size - i - 1);
			size--;
			continue;
		}

		/* Preferred frequency is acceptable for P2P use */
		i++;
	}

	pos = txt;
	end = pos + sizeof(txt);
	for (i = 0; i < size; i++) {
		res = os_snprintf(pos, end - pos, " %u", freq_list[i]);
		if (os_snprintf_error(end - pos, res))
			break;
		pos += res;
	}
	*pos = '\0';
	p2p_dbg(p2p, "Local driver frequency preference (size=%u):%s",
		size, txt);

	/*
	 * Check if peer's preference of operating channel is in
	 * our preferred channel list.
	 */
	for (i = 0; i < size; i++) {
		if (freq_list[i] == (unsigned int) dev->oper_freq)
			break;
	}
	if (i != size &&
	    p2p_freq_to_channel(freq_list[i], &op_class, &op_channel) == 0) {
		/* Peer operating channel preference matches our preference */
		p2p->op_reg_class = op_class;
		p2p->op_channel = op_channel;
		os_memcpy(&p2p->channels, &p2p->cfg->channels,
			  sizeof(struct p2p_channels));
		return;
	}

	p2p_dbg(p2p,
		"Peer operating channel preference: %d MHz is not in our preferred channel list",
		dev->oper_freq);

	/*
	  Check if peer's preferred channel list is
	  * _not_ included in the GO Negotiation Request or Invitation Request.
	  */
	if (msg->pref_freq_list_len == 0)
		p2p_check_pref_chan_no_recv(p2p, go, dev, msg, freq_list, size);
	else
		p2p_check_pref_chan_recv(p2p, go, dev, msg, freq_list, size);
}


void p2p_process_go_neg_req(struct p2p_data *p2p, const u8 *sa,
			    const u8 *data, size_t len, int rx_freq)
{
	struct p2p_device *dev = NULL;
	struct wpabuf *resp;
	struct p2p_message msg;
	u8 status = P2P_SC_FAIL_INVALID_PARAMS;
	int tie_breaker = 0;
	int freq;

	p2p_dbg(p2p, "Received GO Negotiation Request from " MACSTR "(freq=%d)",
		MAC2STR(sa), rx_freq);

	if (p2p_parse(data, len, &msg))
		return;

	if (!msg.capability) {
		p2p_dbg(p2p, "Mandatory Capability attribute missing from GO Negotiation Request");
#ifdef CONFIG_P2P_STRICT
		goto fail;
#endif /* CONFIG_P2P_STRICT */
	}

	if (msg.go_intent)
		tie_breaker = *msg.go_intent & 0x01;
	else {
		p2p_dbg(p2p, "Mandatory GO Intent attribute missing from GO Negotiation Request");
#ifdef CONFIG_P2P_STRICT
		goto fail;
#endif /* CONFIG_P2P_STRICT */
	}

	if (!msg.config_timeout) {
		p2p_dbg(p2p, "Mandatory Configuration Timeout attribute missing from GO Negotiation Request");
#ifdef CONFIG_P2P_STRICT
		goto fail;
#endif /* CONFIG_P2P_STRICT */
	}

	if (!msg.listen_channel) {
		p2p_dbg(p2p, "No Listen Channel attribute received");
		goto fail;
	}
	if (!msg.operating_channel) {
		p2p_dbg(p2p, "No Operating Channel attribute received");
		goto fail;
	}
	if (!msg.channel_list) {
		p2p_dbg(p2p, "No Channel List attribute received");
		goto fail;
	}
	if (!msg.intended_addr) {
		p2p_dbg(p2p, "No Intended P2P Interface Address attribute received");
		goto fail;
	}
	if (!msg.p2p_device_info) {
		p2p_dbg(p2p, "No P2P Device Info attribute received");
		goto fail;
	}

	if (os_memcmp(msg.p2p_device_addr, sa, ETH_ALEN) != 0) {
		p2p_dbg(p2p, "Unexpected GO Negotiation Request SA=" MACSTR
			" != dev_addr=" MACSTR,
			MAC2STR(sa), MAC2STR(msg.p2p_device_addr));
		goto fail;
	}

	dev = p2p_get_device(p2p, sa);

	if (msg.status && *msg.status) {
		p2p_dbg(p2p, "Unexpected Status attribute (%d) in GO Negotiation Request",
			*msg.status);
		if (dev && p2p->go_neg_peer == dev &&
		    *msg.status == P2P_SC_FAIL_REJECTED_BY_USER) {
			/*
			 * This mechanism for using Status attribute in GO
			 * Negotiation Request is not compliant with the P2P
			 * specification, but some deployed devices use it to
			 * indicate rejection of GO Negotiation in a case where
			 * they have sent out GO Negotiation Response with
			 * status 1. The P2P specification explicitly disallows
			 * this. To avoid unnecessary interoperability issues
			 * and extra frames, mark the pending negotiation as
			 * failed and do not reply to this GO Negotiation
			 * Request frame.
			 */
			p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
			p2p_go_neg_failed(p2p, *msg.status);
			p2p_parse_free(&msg);
			return;
		}
		goto fail;
	}

	if (dev == NULL)
		dev = p2p_add_dev_from_go_neg_req(p2p, sa, &msg);
	else if ((dev->flags & P2P_DEV_PROBE_REQ_ONLY) ||
		  !(dev->flags & P2P_DEV_REPORTED))
		p2p_add_dev_info(p2p, sa, dev, &msg);
	else if (!dev->listen_freq && !dev->oper_freq) {
		/*
		 * This may happen if the peer entry was added based on PD
		 * Request and no Probe Request/Response frame has been received
		 * from this peer (or that information has timed out).
		 */
		p2p_dbg(p2p, "Update peer " MACSTR
			" based on GO Neg Req since listen/oper freq not known",
			MAC2STR(dev->info.p2p_device_addr));
		p2p_add_dev_info(p2p, sa, dev, &msg);
	}

	if (p2p->go_neg_peer && p2p->go_neg_peer == dev)
		eloop_cancel_timeout(p2p_go_neg_wait_timeout, p2p, NULL);

	if (dev && dev->flags & P2P_DEV_USER_REJECTED) {
		p2p_dbg(p2p, "User has rejected this peer");
		status = P2P_SC_FAIL_REJECTED_BY_USER;
	} else if (dev == NULL ||
		   (dev->wps_method == WPS_NOT_READY &&
		    (p2p->authorized_oob_dev_pw_id == 0 ||
		     p2p->authorized_oob_dev_pw_id !=
		     msg.dev_password_id))) {
		p2p_dbg(p2p, "Not ready for GO negotiation with " MACSTR,
			MAC2STR(sa));
		status = P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE;
		p2p->cfg->go_neg_req_rx(p2p->cfg->cb_ctx, sa,
					msg.dev_password_id,
					msg.go_intent ? (*msg.go_intent >> 1) :
					0);
	} else if (p2p->go_neg_peer && p2p->go_neg_peer != dev) {
		p2p_dbg(p2p, "Already in Group Formation with another peer");
		status = P2P_SC_FAIL_UNABLE_TO_ACCOMMODATE;
	} else {
		int go;

		if (!p2p->go_neg_peer) {
			p2p_dbg(p2p, "Starting GO Negotiation with previously authorized peer");
			if (!(dev->flags & P2P_DEV_FORCE_FREQ)) {
				p2p_dbg(p2p, "Use default channel settings");
				p2p->op_reg_class = p2p->cfg->op_reg_class;
				p2p->op_channel = p2p->cfg->op_channel;
				os_memcpy(&p2p->channels, &p2p->cfg->channels,
					  sizeof(struct p2p_channels));
			} else {
				p2p_dbg(p2p, "Use previously configured forced channel settings");
			}
		}

		dev->flags &= ~P2P_DEV_NOT_YET_READY;

		if (!msg.go_intent) {
			p2p_dbg(p2p, "No GO Intent attribute received");
			goto fail;
		}
		if ((*msg.go_intent >> 1) > P2P_MAX_GO_INTENT) {
			p2p_dbg(p2p, "Invalid GO Intent value (%u) received",
				*msg.go_intent >> 1);
			goto fail;
		}

		if (dev->go_neg_req_sent &&
		    os_memcmp(sa, p2p->cfg->dev_addr, ETH_ALEN) > 0) {
			p2p_dbg(p2p, "Do not reply since peer has higher address and GO Neg Request already sent");
			p2p_parse_free(&msg);
			return;
		}

		if (dev->go_neg_req_sent &&
		    (dev->flags & P2P_DEV_PEER_WAITING_RESPONSE)) {
			p2p_dbg(p2p,
				"Do not reply since peer is waiting for us to start a new GO Negotiation and GO Neg Request already sent");
			p2p_parse_free(&msg);
			return;
		}

		go = p2p_go_det(p2p->go_intent, *msg.go_intent);
		if (go < 0) {
			p2p_dbg(p2p, "Incompatible GO Intent");
			status = P2P_SC_FAIL_BOTH_GO_INTENT_15;
			goto fail;
		}

		if (p2p_peer_channels(p2p, dev, msg.channel_list,
				      msg.channel_list_len) < 0) {
			p2p_dbg(p2p, "No common channels found");
			status = P2P_SC_FAIL_NO_COMMON_CHANNELS;
			goto fail;
		}

		switch (msg.dev_password_id) {
		case DEV_PW_REGISTRAR_SPECIFIED:
			p2p_dbg(p2p, "PIN from peer Display");
			if (dev->wps_method != WPS_PIN_KEYPAD) {
				p2p_dbg(p2p, "We have wps_method=%s -> incompatible",
					p2p_wps_method_str(dev->wps_method));
				status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
				goto fail;
			}
			break;
		case DEV_PW_USER_SPECIFIED:
			p2p_dbg(p2p, "Peer entered PIN on Keypad");
			if (dev->wps_method != WPS_PIN_DISPLAY) {
				p2p_dbg(p2p, "We have wps_method=%s -> incompatible",
					p2p_wps_method_str(dev->wps_method));
				status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
				goto fail;
			}
			break;
		case DEV_PW_PUSHBUTTON:
			p2p_dbg(p2p, "Peer using pushbutton");
			if (dev->wps_method != WPS_PBC) {
				p2p_dbg(p2p, "We have wps_method=%s -> incompatible",
					p2p_wps_method_str(dev->wps_method));
				status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
				goto fail;
			}
			break;
		case DEV_PW_P2PS_DEFAULT:
			p2p_dbg(p2p, "Peer using P2PS pin");
			if (dev->wps_method != WPS_P2PS) {
				p2p_dbg(p2p,
					"We have wps_method=%s -> incompatible",
					p2p_wps_method_str(dev->wps_method));
				status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
				goto fail;
			}
			break;
		default:
			if (msg.dev_password_id &&
			    msg.dev_password_id == dev->oob_pw_id) {
				p2p_dbg(p2p, "Peer using NFC");
				if (dev->wps_method != WPS_NFC) {
					p2p_dbg(p2p, "We have wps_method=%s -> incompatible",
						p2p_wps_method_str(
							dev->wps_method));
					status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
					goto fail;
				}
				break;
			}
#ifdef CONFIG_WPS_NFC
			if (p2p->authorized_oob_dev_pw_id &&
			    msg.dev_password_id ==
			    p2p->authorized_oob_dev_pw_id) {
				p2p_dbg(p2p, "Using static handover with our device password from NFC Tag");
				dev->wps_method = WPS_NFC;
				dev->oob_pw_id = p2p->authorized_oob_dev_pw_id;
				break;
			}
#endif /* CONFIG_WPS_NFC */
			p2p_dbg(p2p, "Unsupported Device Password ID %d",
				msg.dev_password_id);
			status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
			goto fail;
		}

		if (go && p2p_go_select_channel(p2p, dev, &status) < 0)
			goto fail;

		dev->go_state = go ? LOCAL_GO : REMOTE_GO;
		dev->oper_freq = p2p_channel_to_freq(msg.operating_channel[3],
						     msg.operating_channel[4]);
		p2p_dbg(p2p, "Peer operating channel preference: %d MHz",
			dev->oper_freq);

		/*
		 * Use the driver preferred frequency list extension if
		 * supported.
		 */
		p2p_check_pref_chan(p2p, go, dev, &msg);

		if (msg.config_timeout) {
			dev->go_timeout = msg.config_timeout[0];
			dev->client_timeout = msg.config_timeout[1];
		}

		p2p_dbg(p2p, "GO Negotiation with " MACSTR, MAC2STR(sa));
		if (p2p->state != P2P_IDLE)
			p2p_stop_find_for_freq(p2p, rx_freq);
		p2p_set_state(p2p, P2P_GO_NEG);
		p2p_clear_timeout(p2p);
		dev->dialog_token = msg.dialog_token;
		os_memcpy(dev->intended_addr, msg.intended_addr, ETH_ALEN);
		p2p->go_neg_peer = dev;
		eloop_cancel_timeout(p2p_go_neg_wait_timeout, p2p, NULL);
		status = P2P_SC_SUCCESS;
	}

fail:
	if (dev)
		dev->status = status;
	resp = p2p_build_go_neg_resp(p2p, dev, msg.dialog_token, status,
				     !tie_breaker);
	p2p_parse_free(&msg);
	if (resp == NULL)
		return;
	p2p_dbg(p2p, "Sending GO Negotiation Response");
	if (rx_freq > 0)
		freq = rx_freq;
	else
		freq = p2p_channel_to_freq(p2p->cfg->reg_class,
					   p2p->cfg->channel);
	if (freq < 0) {
		p2p_dbg(p2p, "Unknown regulatory class/channel");
		wpabuf_free(resp);
		return;
	}
	if (status == P2P_SC_SUCCESS) {
		p2p->pending_action_state = P2P_PENDING_GO_NEG_RESPONSE;
		dev->flags |= P2P_DEV_WAIT_GO_NEG_CONFIRM;
		if (os_memcmp(sa, p2p->cfg->dev_addr, ETH_ALEN) < 0) {
			/*
			 * Peer has smaller address, so the GO Negotiation
			 * Response from us is expected to complete
			 * negotiation. Ignore a GO Negotiation Response from
			 * the peer if it happens to be received after this
			 * point due to a race condition in GO Negotiation
			 * Request transmission and processing.
			 */
			dev->flags &= ~P2P_DEV_WAIT_GO_NEG_RESPONSE;
		}
	} else
		p2p->pending_action_state =
			P2P_PENDING_GO_NEG_RESPONSE_FAILURE;
	if (p2p_send_action(p2p, freq, sa, p2p->cfg->dev_addr,
			    p2p->cfg->dev_addr,
			    wpabuf_head(resp), wpabuf_len(resp), 100) < 0) {
		p2p_dbg(p2p, "Failed to send Action frame");
	}

	wpabuf_free(resp);
}


static struct wpabuf * p2p_build_go_neg_conf(struct p2p_data *p2p,
					     struct p2p_device *peer,
					     u8 dialog_token, u8 status,
					     const u8 *resp_chan, int go)
{
	struct wpabuf *buf;
	u8 *len;
	struct p2p_channels res;
	u8 group_capab;
	size_t extra = 0;

	p2p_dbg(p2p, "Building GO Negotiation Confirm");

#ifdef CONFIG_WIFI_DISPLAY
	if (p2p->wfd_ie_go_neg)
		extra = wpabuf_len(p2p->wfd_ie_go_neg);
#endif /* CONFIG_WIFI_DISPLAY */

	if (p2p->vendor_elem && p2p->vendor_elem[VENDOR_ELEM_P2P_GO_NEG_CONF])
		extra += wpabuf_len(p2p->vendor_elem[VENDOR_ELEM_P2P_GO_NEG_CONF]);

	buf = wpabuf_alloc(1000 + extra);
	if (buf == NULL)
		return NULL;

	p2p_buf_add_public_action_hdr(buf, P2P_GO_NEG_CONF, dialog_token);

	len = p2p_buf_add_ie_hdr(buf);
	p2p_buf_add_status(buf, status);
	group_capab = 0;
	if (peer->go_state == LOCAL_GO) {
		if (peer->flags & P2P_DEV_PREFER_PERSISTENT_GROUP) {
			group_capab |= P2P_GROUP_CAPAB_PERSISTENT_GROUP;
			if (peer->flags & P2P_DEV_PREFER_PERSISTENT_RECONN)
				group_capab |=
					P2P_GROUP_CAPAB_PERSISTENT_RECONN;
		}
		if (p2p->cross_connect)
			group_capab |= P2P_GROUP_CAPAB_CROSS_CONN;
		if (p2p->cfg->p2p_intra_bss)
			group_capab |= P2P_GROUP_CAPAB_INTRA_BSS_DIST;
	}
	p2p_buf_add_capability(buf, p2p->dev_capab &
			       ~P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY,
			       group_capab);
	if (go || resp_chan == NULL)
		p2p_buf_add_operating_channel(buf, p2p->cfg->country,
					      p2p->op_reg_class,
					      p2p->op_channel);
	else
		p2p_buf_add_operating_channel(buf, (const char *) resp_chan,
					      resp_chan[3], resp_chan[4]);
	p2p_channels_intersect(&p2p->channels, &peer->channels, &res);
	p2p_buf_add_channel_list(buf, p2p->cfg->country, &res);
	if (go) {
		p2p_buf_add_group_id(buf, p2p->cfg->dev_addr, p2p->ssid,
				     p2p->ssid_len);
	}
	p2p_buf_update_ie_hdr(buf, len);

#ifdef CONFIG_WIFI_DISPLAY
	if (p2p->wfd_ie_go_neg)
		wpabuf_put_buf(buf, p2p->wfd_ie_go_neg);
#endif /* CONFIG_WIFI_DISPLAY */

	if (p2p->vendor_elem && p2p->vendor_elem[VENDOR_ELEM_P2P_GO_NEG_CONF])
		wpabuf_put_buf(buf, p2p->vendor_elem[VENDOR_ELEM_P2P_GO_NEG_CONF]);

	return buf;
}


void p2p_process_go_neg_resp(struct p2p_data *p2p, const u8 *sa,
			     const u8 *data, size_t len, int rx_freq)
{
	struct p2p_device *dev;
	int go = -1;
	struct p2p_message msg;
	u8 status = P2P_SC_SUCCESS;
	int freq;

	p2p_dbg(p2p, "Received GO Negotiation Response from " MACSTR
		" (freq=%d)", MAC2STR(sa), rx_freq);
	dev = p2p_get_device(p2p, sa);
	if (dev == NULL || dev->wps_method == WPS_NOT_READY ||
	    dev != p2p->go_neg_peer) {
		p2p_dbg(p2p, "Not ready for GO negotiation with " MACSTR,
			MAC2STR(sa));
		return;
	}

	if (p2p_parse(data, len, &msg))
		return;

	if (!(dev->flags & P2P_DEV_WAIT_GO_NEG_RESPONSE)) {
		p2p_dbg(p2p, "Was not expecting GO Negotiation Response - ignore");
		p2p_parse_free(&msg);
		return;
	}
	dev->flags &= ~P2P_DEV_WAIT_GO_NEG_RESPONSE;

	if (msg.dialog_token != dev->dialog_token) {
		p2p_dbg(p2p, "Unexpected Dialog Token %u (expected %u)",
			msg.dialog_token, dev->dialog_token);
		p2p_parse_free(&msg);
		return;
	}

	if (!msg.status) {
		p2p_dbg(p2p, "No Status attribute received");
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
	}
	if (*msg.status) {
		p2p_dbg(p2p, "GO Negotiation rejected: status %d", *msg.status);
		dev->go_neg_req_sent = 0;
		if (*msg.status == P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE) {
			p2p_dbg(p2p, "Wait for the peer to become ready for GO Negotiation");
			dev->flags |= P2P_DEV_NOT_YET_READY;
			eloop_cancel_timeout(p2p_go_neg_wait_timeout, p2p,
					     NULL);
			eloop_register_timeout(120, 0, p2p_go_neg_wait_timeout,
					       p2p, NULL);
			if (p2p->state == P2P_CONNECT_LISTEN)
				p2p_set_state(p2p, P2P_WAIT_PEER_CONNECT);
			else
				p2p_set_state(p2p, P2P_WAIT_PEER_IDLE);
			p2p_set_timeout(p2p, 0, 0);
		} else {
			p2p_dbg(p2p, "Stop GO Negotiation attempt");
			p2p_go_neg_failed(p2p, *msg.status);
		}
		p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
		p2p_parse_free(&msg);
		return;
	}

	if (!msg.capability) {
		p2p_dbg(p2p, "Mandatory Capability attribute missing from GO Negotiation Response");
#ifdef CONFIG_P2P_STRICT
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
#endif /* CONFIG_P2P_STRICT */
	}

	if (!msg.p2p_device_info) {
		p2p_dbg(p2p, "Mandatory P2P Device Info attribute missing from GO Negotiation Response");
#ifdef CONFIG_P2P_STRICT
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
#endif /* CONFIG_P2P_STRICT */
	}

	if (!msg.intended_addr) {
		p2p_dbg(p2p, "No Intended P2P Interface Address attribute received");
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
	}

	if (!msg.go_intent) {
		p2p_dbg(p2p, "No GO Intent attribute received");
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
	}
	if ((*msg.go_intent >> 1) > P2P_MAX_GO_INTENT) {
		p2p_dbg(p2p, "Invalid GO Intent value (%u) received",
			*msg.go_intent >> 1);
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
	}

	go = p2p_go_det(p2p->go_intent, *msg.go_intent);
	if (go < 0) {
		p2p_dbg(p2p, "Incompatible GO Intent");
		status = P2P_SC_FAIL_INCOMPATIBLE_PARAMS;
		goto fail;
	}

	if (!go && msg.group_id) {
		/* Store SSID for Provisioning step */
		p2p->ssid_len = msg.group_id_len - ETH_ALEN;
		os_memcpy(p2p->ssid, msg.group_id + ETH_ALEN, p2p->ssid_len);
	} else if (!go) {
		p2p_dbg(p2p, "Mandatory P2P Group ID attribute missing from GO Negotiation Response");
		p2p->ssid_len = 0;
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
	}

	if (!msg.config_timeout) {
		p2p_dbg(p2p, "Mandatory Configuration Timeout attribute missing from GO Negotiation Response");
#ifdef CONFIG_P2P_STRICT
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
#endif /* CONFIG_P2P_STRICT */
	} else {
		dev->go_timeout = msg.config_timeout[0];
		dev->client_timeout = msg.config_timeout[1];
	}

	if (msg.wfd_subelems) {
		wpabuf_free(dev->info.wfd_subelems);
		dev->info.wfd_subelems = wpabuf_dup(msg.wfd_subelems);
	}

	if (!msg.operating_channel && !go) {
		/*
		 * Note: P2P Client may omit Operating Channel attribute to
		 * indicate it does not have a preference.
		 */
		p2p_dbg(p2p, "No Operating Channel attribute received");
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
	}
	if (!msg.channel_list) {
		p2p_dbg(p2p, "No Channel List attribute received");
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
	}

	if (p2p_peer_channels(p2p, dev, msg.channel_list,
			      msg.channel_list_len) < 0) {
		p2p_dbg(p2p, "No common channels found");
		status = P2P_SC_FAIL_NO_COMMON_CHANNELS;
		goto fail;
	}

	if (msg.operating_channel) {
		dev->oper_freq = p2p_channel_to_freq(msg.operating_channel[3],
						     msg.operating_channel[4]);
		p2p_dbg(p2p, "Peer operating channel preference: %d MHz",
			dev->oper_freq);
	} else
		dev->oper_freq = 0;

	switch (msg.dev_password_id) {
	case DEV_PW_REGISTRAR_SPECIFIED:
		p2p_dbg(p2p, "PIN from peer Display");
		if (dev->wps_method != WPS_PIN_KEYPAD) {
			p2p_dbg(p2p, "We have wps_method=%s -> incompatible",
				p2p_wps_method_str(dev->wps_method));
			status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
			goto fail;
		}
		break;
	case DEV_PW_USER_SPECIFIED:
		p2p_dbg(p2p, "Peer entered PIN on Keypad");
		if (dev->wps_method != WPS_PIN_DISPLAY) {
			p2p_dbg(p2p, "We have wps_method=%s -> incompatible",
				p2p_wps_method_str(dev->wps_method));
			status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
			goto fail;
		}
		break;
	case DEV_PW_PUSHBUTTON:
		p2p_dbg(p2p, "Peer using pushbutton");
		if (dev->wps_method != WPS_PBC) {
			p2p_dbg(p2p, "We have wps_method=%s -> incompatible",
				p2p_wps_method_str(dev->wps_method));
			status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
			goto fail;
		}
		break;
	case DEV_PW_P2PS_DEFAULT:
		p2p_dbg(p2p, "P2P: Peer using P2PS default pin");
		if (dev->wps_method != WPS_P2PS) {
			p2p_dbg(p2p, "We have wps_method=%s -> incompatible",
				p2p_wps_method_str(dev->wps_method));
			status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
			goto fail;
		}
		break;
	default:
		if (msg.dev_password_id &&
		    msg.dev_password_id == dev->oob_pw_id) {
			p2p_dbg(p2p, "Peer using NFC");
			if (dev->wps_method != WPS_NFC) {
				p2p_dbg(p2p, "We have wps_method=%s -> incompatible",
					p2p_wps_method_str(dev->wps_method));
				status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
				goto fail;
			}
			break;
		}
		p2p_dbg(p2p, "Unsupported Device Password ID %d",
			msg.dev_password_id);
		status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
		goto fail;
	}

	if (go && p2p_go_select_channel(p2p, dev, &status) < 0)
		goto fail;

	/*
	 * Use the driver preferred frequency list extension if local device is
	 * GO.
	 */
	if (go)
		p2p_check_pref_chan(p2p, go, dev, &msg);

	p2p_set_state(p2p, P2P_GO_NEG);
	p2p_clear_timeout(p2p);

	p2p_dbg(p2p, "GO Negotiation with " MACSTR, MAC2STR(sa));
	os_memcpy(dev->intended_addr, msg.intended_addr, ETH_ALEN);

fail:
	/* Store GO Negotiation Confirmation to allow retransmission */
	wpabuf_free(dev->go_neg_conf);
	dev->go_neg_conf = p2p_build_go_neg_conf(p2p, dev, msg.dialog_token,
						 status, msg.operating_channel,
						 go);
	p2p_parse_free(&msg);
	if (dev->go_neg_conf == NULL)
		return;
	p2p_dbg(p2p, "Sending GO Negotiation Confirm");
	if (status == P2P_SC_SUCCESS) {
		p2p->pending_action_state = P2P_PENDING_GO_NEG_CONFIRM;
		dev->go_state = go ? LOCAL_GO : REMOTE_GO;
	} else
		p2p->pending_action_state = P2P_NO_PENDING_ACTION;
	if (rx_freq > 0)
		freq = rx_freq;
	else
		freq = dev->listen_freq;

	dev->go_neg_conf_freq = freq;
	dev->go_neg_conf_sent = 0;

	if (p2p_send_action(p2p, freq, sa, p2p->cfg->dev_addr, sa,
			    wpabuf_head(dev->go_neg_conf),
			    wpabuf_len(dev->go_neg_conf), 50) < 0) {
		p2p_dbg(p2p, "Failed to send Action frame");
		p2p_go_neg_failed(p2p, -1);
		p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
	} else
		dev->go_neg_conf_sent++;
	if (status != P2P_SC_SUCCESS) {
		p2p_dbg(p2p, "GO Negotiation failed");
		p2p_go_neg_failed(p2p, status);
	}
}


void p2p_process_go_neg_conf(struct p2p_data *p2p, const u8 *sa,
			     const u8 *data, size_t len)
{
	struct p2p_device *dev;
	struct p2p_message msg;

	p2p_dbg(p2p, "Received GO Negotiation Confirm from " MACSTR,
		MAC2STR(sa));
	dev = p2p_get_device(p2p, sa);
	if (dev == NULL || dev->wps_method == WPS_NOT_READY ||
	    dev != p2p->go_neg_peer) {
		p2p_dbg(p2p, "Not ready for GO negotiation with " MACSTR,
			MAC2STR(sa));
		return;
	}

	if (p2p->pending_action_state == P2P_PENDING_GO_NEG_RESPONSE) {
		p2p_dbg(p2p, "Stopped waiting for TX status on GO Negotiation Response since we already received Confirmation");
		p2p->pending_action_state = P2P_NO_PENDING_ACTION;
	}

	if (p2p_parse(data, len, &msg))
		return;

	if (!(dev->flags & P2P_DEV_WAIT_GO_NEG_CONFIRM)) {
		p2p_dbg(p2p, "Was not expecting GO Negotiation Confirm - ignore");
		p2p_parse_free(&msg);
		return;
	}
	dev->flags &= ~P2P_DEV_WAIT_GO_NEG_CONFIRM;
	p2p->cfg->send_action_done(p2p->cfg->cb_ctx);

	if (msg.dialog_token != dev->dialog_token) {
		p2p_dbg(p2p, "Unexpected Dialog Token %u (expected %u)",
			msg.dialog_token, dev->dialog_token);
		p2p_parse_free(&msg);
		return;
	}

	if (!msg.status) {
		p2p_dbg(p2p, "No Status attribute received");
		p2p_parse_free(&msg);
		return;
	}
	if (*msg.status) {
		p2p_dbg(p2p, "GO Negotiation rejected: status %d", *msg.status);
		p2p_go_neg_failed(p2p, *msg.status);
		p2p_parse_free(&msg);
		return;
	}

	if (dev->go_state == REMOTE_GO && msg.group_id) {
		/* Store SSID for Provisioning step */
		p2p->ssid_len = msg.group_id_len - ETH_ALEN;
		os_memcpy(p2p->ssid, msg.group_id + ETH_ALEN, p2p->ssid_len);
	} else if (dev->go_state == REMOTE_GO) {
		p2p_dbg(p2p, "Mandatory P2P Group ID attribute missing from GO Negotiation Confirmation");
		p2p->ssid_len = 0;
		p2p_go_neg_failed(p2p, P2P_SC_FAIL_INVALID_PARAMS);
		p2p_parse_free(&msg);
		return;
	}

	if (!msg.operating_channel) {
		p2p_dbg(p2p, "Mandatory Operating Channel attribute missing from GO Negotiation Confirmation");
#ifdef CONFIG_P2P_STRICT
		p2p_parse_free(&msg);
		return;
#endif /* CONFIG_P2P_STRICT */
	} else if (dev->go_state == REMOTE_GO) {
		int oper_freq = p2p_channel_to_freq(msg.operating_channel[3],
						    msg.operating_channel[4]);
		if (oper_freq != dev->oper_freq) {
			p2p_dbg(p2p, "Updated peer (GO) operating channel preference from %d MHz to %d MHz",
				dev->oper_freq, oper_freq);
			dev->oper_freq = oper_freq;
		}
	}

	if (!msg.channel_list) {
		p2p_dbg(p2p, "Mandatory Operating Channel attribute missing from GO Negotiation Confirmation");
#ifdef CONFIG_P2P_STRICT
		p2p_parse_free(&msg);
		return;
#endif /* CONFIG_P2P_STRICT */
	}

	p2p_parse_free(&msg);

	if (dev->go_state == UNKNOWN_GO) {
		/*
		 * This should not happen since GO negotiation has already
		 * been completed.
		 */
		p2p_dbg(p2p, "Unexpected GO Neg state - do not know which end becomes GO");
		return;
	}

	/*
	 * The peer could have missed our ctrl::ack frame for GO Negotiation
	 * Confirm and continue retransmitting the frame. To reduce the
	 * likelihood of the peer not getting successful TX status for the
	 * GO Negotiation Confirm frame, wait a short time here before starting
	 * the group so that we will remain on the current channel to
	 * acknowledge any possible retransmission from the peer.
	 */
	p2p_dbg(p2p, "20 ms wait on current channel before starting group");
	os_sleep(0, 20000);

	p2p_go_complete(p2p, dev);
}
