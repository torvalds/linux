/*
 * Wi-Fi Direct - P2P Group Owner Negotiation
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
	size_t channels;
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
		wpa_msg(p2p->cfg->msg_ctx, MSG_INFO,
			"P2P: Mismatching country (ours=%c%c peer's=%c%c)",
			p2p->cfg->country[0], p2p->cfg->country[1],
			pos[0], pos[1]);
		return -1;
	}
	pos += 3;

	while (pos + 2 < end) {
		struct p2p_reg_class *cl = &ch->reg_class[ch->reg_classes];
		cl->reg_class = *pos++;
		if (pos + 1 + pos[0] > end) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_INFO,
				"P2P: Invalid peer Channel List");
			return -1;
		}
		channels = *pos++;
		cl->channels = channels > P2P_MAX_REG_CLASS_CHANNELS ?
			P2P_MAX_REG_CLASS_CHANNELS : channels;
		os_memcpy(cl->channel, pos, cl->channels);
		pos += channels;
		ch->reg_classes++;
		if (ch->reg_classes == P2P_MAX_REG_CLASSES)
			break;
	}

	p2p_channels_intersect(own, &dev->channels, &intersection);
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Own reg_classes %d "
		"peer reg_classes %d intersection reg_classes %d",
		(int) own->reg_classes,
		(int) dev->channels.reg_classes,
		(int) intersection.reg_classes);
	if (intersection.reg_classes == 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_INFO,
			"P2P: No common channels found");
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


static u16 p2p_wps_method_pw_id(enum p2p_wps_method wps_method)
{
	switch (wps_method) {
	case WPS_PIN_LABEL:
		return DEV_PW_DEFAULT;
	case WPS_PIN_DISPLAY:
		return DEV_PW_REGISTRAR_SPECIFIED;
	case WPS_PIN_KEYPAD:
		return DEV_PW_USER_SPECIFIED;
	case WPS_PBC:
		return DEV_PW_PUSHBUTTON;
	default:
		return DEV_PW_DEFAULT;
	}
}


static const char * p2p_wps_method_str(enum p2p_wps_method wps_method)
{
	switch (wps_method) {
	case WPS_PIN_LABEL:
		return "Label";
	case WPS_PIN_DISPLAY:
		return "Display";
	case WPS_PIN_KEYPAD:
		return "Keypad";
	case WPS_PBC:
		return "PBC";
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

	buf = wpabuf_alloc(1000);
	if (buf == NULL)
		return NULL;

	peer->dialog_token++;
	if (peer->dialog_token == 0)
		peer->dialog_token = 1;
	p2p_buf_add_public_action_hdr(buf, P2P_GO_NEG_REQ, peer->dialog_token);

	len = p2p_buf_add_ie_hdr(buf);
	group_capab = 0;
	if (peer->flags & P2P_DEV_PREFER_PERSISTENT_GROUP)
		group_capab |= P2P_GROUP_CAPAB_PERSISTENT_GROUP;
	if (p2p->cross_connect)
		group_capab |= P2P_GROUP_CAPAB_CROSS_CONN;
	if (p2p->cfg->p2p_intra_bss)
		group_capab |= P2P_GROUP_CAPAB_INTRA_BSS_DIST;
	p2p_buf_add_capability(buf, p2p->dev_capab, group_capab);
	p2p_buf_add_go_intent(buf, (p2p->go_intent << 1) |
			      p2p->next_tie_breaker);
	p2p->next_tie_breaker = !p2p->next_tie_breaker;
	p2p_buf_add_config_timeout(buf, 100, 20);
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

	/* WPS IE with Device Password ID attribute */
	p2p_build_wps_ie(p2p, buf, p2p_wps_method_pw_id(peer->wps_method), 0);

	return buf;
}


int p2p_connect_send(struct p2p_data *p2p, struct p2p_device *dev)
{
	struct wpabuf *req;
	int freq;

	freq = dev->listen_freq > 0 ? dev->listen_freq : dev->oper_freq;
	if (freq <= 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No Listen/Operating frequency known for the "
			"peer " MACSTR " to send GO Negotiation Request",
			MAC2STR(dev->info.p2p_device_addr));
		return -1;
	}

	req = p2p_build_go_neg_req(p2p, dev);
	if (req == NULL)
		return -1;
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Sending GO Negotiation Request");
	p2p_set_state(p2p, P2P_CONNECT);
	p2p->pending_action_state = P2P_PENDING_GO_NEG_REQUEST;
	p2p->go_neg_peer = dev;
	dev->flags |= P2P_DEV_WAIT_GO_NEG_RESPONSE;
	dev->connect_reqs++;
	if (p2p_send_action(p2p, freq, dev->info.p2p_device_addr,
			    p2p->cfg->dev_addr, dev->info.p2p_device_addr,
			    wpabuf_head(req), wpabuf_len(req), 200) < 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Failed to send Action frame");
		/* Use P2P find to recover and retry */
		p2p_set_timeout(p2p, 0, 0);
	}

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

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Building GO Negotiation Response");
	buf = wpabuf_alloc(1000);
	if (buf == NULL)
		return NULL;

	p2p_buf_add_public_action_hdr(buf, P2P_GO_NEG_RESP, dialog_token);

	len = p2p_buf_add_ie_hdr(buf);
	p2p_buf_add_status(buf, status);
	group_capab = 0;
	if (peer && peer->go_state == LOCAL_GO) {
		if (peer->flags & P2P_DEV_PREFER_PERSISTENT_GROUP)
			group_capab |= P2P_GROUP_CAPAB_PERSISTENT_GROUP;
		if (p2p->cross_connect)
			group_capab |= P2P_GROUP_CAPAB_CROSS_CONN;
		if (p2p->cfg->p2p_intra_bss)
			group_capab |= P2P_GROUP_CAPAB_INTRA_BSS_DIST;
	}
	p2p_buf_add_capability(buf, p2p->dev_capab, group_capab);
	p2p_buf_add_go_intent(buf, (p2p->go_intent << 1) | tie_breaker);
	p2p_buf_add_config_timeout(buf, 100, 20);
	if (peer && peer->go_state == REMOTE_GO) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Omit Operating "
			"Channel attribute");
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
	p2p_build_wps_ie(p2p, buf,
			 p2p_wps_method_pw_id(peer ? peer->wps_method :
					      WPS_NOT_READY), 0);

	return buf;
}


static void p2p_reselect_channel(struct p2p_data *p2p,
				 struct p2p_channels *intersection)
{
	struct p2p_reg_class *cl;
	int freq;
	u8 op_reg_class, op_channel;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Selected operating "
		"channel (reg_class %u channel %u) not acceptable to the "
		"peer", p2p->op_reg_class, p2p->op_channel);

	/* First, try to pick the best channel from another band */
	freq = p2p_channel_to_freq(p2p->cfg->country, p2p->op_reg_class,
				   p2p->op_channel);
	if (freq >= 2400 && freq < 2500 && p2p->best_freq_5 > 0 &&
	    p2p_freq_to_channel(p2p->cfg->country, p2p->best_freq_5,
				&op_reg_class, &op_channel) == 0 &&
	    p2p_channels_includes(intersection, op_reg_class, op_channel)) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Pick best 5 GHz "
			"channel (reg_class %u channel %u) from intersection",
			op_reg_class, op_channel);
		p2p->op_reg_class = op_reg_class;
		p2p->op_channel = op_channel;
		return;
	}

	if (freq >= 4900 && freq < 6000 && p2p->best_freq_24 > 0 &&
	    p2p_freq_to_channel(p2p->cfg->country, p2p->best_freq_24,
				&op_reg_class, &op_channel) == 0 &&
	    p2p_channels_includes(intersection, op_reg_class, op_channel)) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Pick best 2.4 GHz "
			"channel (reg_class %u channel %u) from intersection",
			op_reg_class, op_channel);
		p2p->op_reg_class = op_reg_class;
		p2p->op_channel = op_channel;
		return;
	}

	/*
	 * Fall back to whatever is included in the channel intersection since
	 * no better options seems to be available.
	 */
	cl = &intersection->reg_class[0];
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Pick another channel "
		"(reg_class %u channel %u) from intersection",
		cl->reg_class, cl->channel[0]);
	p2p->op_reg_class = cl->reg_class;
	p2p->op_channel = cl->channel[0];
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

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Received GO Negotiation Request from " MACSTR
		"(freq=%d)", MAC2STR(sa), rx_freq);

	if (p2p_parse(data, len, &msg))
		return;

	if (!msg.capability) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Mandatory Capability attribute missing from GO "
			"Negotiation Request");
#ifdef CONFIG_P2P_STRICT
		goto fail;
#endif /* CONFIG_P2P_STRICT */
	}

	if (msg.go_intent)
		tie_breaker = *msg.go_intent & 0x01;
	else {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Mandatory GO Intent attribute missing from GO "
			"Negotiation Request");
#ifdef CONFIG_P2P_STRICT
		goto fail;
#endif /* CONFIG_P2P_STRICT */
	}

	if (!msg.config_timeout) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Mandatory Configuration Timeout attribute "
			"missing from GO Negotiation Request");
#ifdef CONFIG_P2P_STRICT
		goto fail;
#endif /* CONFIG_P2P_STRICT */
	}

	if (!msg.listen_channel) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No Listen Channel attribute received");
		goto fail;
	}
	if (!msg.operating_channel) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No Operating Channel attribute received");
		goto fail;
	}
	if (!msg.channel_list) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No Channel List attribute received");
		goto fail;
	}
	if (!msg.intended_addr) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No Intended P2P Interface Address attribute "
			"received");
		goto fail;
	}
	if (!msg.p2p_device_info) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No P2P Device Info attribute received");
		goto fail;
	}

	if (os_memcmp(msg.p2p_device_addr, sa, ETH_ALEN) != 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unexpected GO Negotiation Request SA=" MACSTR
			" != dev_addr=" MACSTR,
			MAC2STR(sa), MAC2STR(msg.p2p_device_addr));
		goto fail;
	}

	dev = p2p_get_device(p2p, sa);

	if (msg.status && *msg.status) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unexpected Status attribute (%d) in GO "
			"Negotiation Request", *msg.status);
		goto fail;
	}

	if (dev == NULL)
		dev = p2p_add_dev_from_go_neg_req(p2p, sa, &msg);
	else if (dev->flags & P2P_DEV_PROBE_REQ_ONLY)
		p2p_add_dev_info(p2p, sa, dev, &msg);
	if (dev && dev->flags & P2P_DEV_USER_REJECTED) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: User has rejected this peer");
		status = P2P_SC_FAIL_REJECTED_BY_USER;
	} else if (dev == NULL || dev->wps_method == WPS_NOT_READY) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Not ready for GO negotiation with " MACSTR,
			MAC2STR(sa));
		status = P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE;
		if (dev)
			dev->flags |= P2P_DEV_PEER_WAITING_RESPONSE;
		p2p->cfg->go_neg_req_rx(p2p->cfg->cb_ctx, sa,
					msg.dev_password_id);
	} else if (p2p->go_neg_peer && p2p->go_neg_peer != dev) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Already in Group Formation with another peer");
		status = P2P_SC_FAIL_UNABLE_TO_ACCOMMODATE;
	} else {
		int go;

		if (!p2p->go_neg_peer) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Starting "
				"GO Negotiation with previously authorized "
				"peer");
			if (!(dev->flags & P2P_DEV_FORCE_FREQ)) {
				wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
					"P2P: Use default channel settings");
				p2p->op_reg_class = p2p->cfg->op_reg_class;
				p2p->op_channel = p2p->cfg->op_channel;
				os_memcpy(&p2p->channels, &p2p->cfg->channels,
					  sizeof(struct p2p_channels));
			} else {
				wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
					"P2P: Use previously configured "
					"forced channel settings");
			}
		}

		dev->flags &= ~P2P_DEV_NOT_YET_READY;

		if (!msg.go_intent) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: No GO Intent attribute received");
			goto fail;
		}
		if ((*msg.go_intent >> 1) > P2P_MAX_GO_INTENT) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Invalid GO Intent value (%u) received",
				*msg.go_intent >> 1);
			goto fail;
		}

		if (dev->go_neg_req_sent &&
		    os_memcmp(sa, p2p->cfg->dev_addr, ETH_ALEN) > 0) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Do not reply since peer has higher "
				"address and GO Neg Request already sent");
			p2p_parse_free(&msg);
			return;
		}

		go = p2p_go_det(p2p->go_intent, *msg.go_intent);
		if (go < 0) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Incompatible GO Intent");
			status = P2P_SC_FAIL_BOTH_GO_INTENT_15;
			goto fail;
		}

		if (p2p_peer_channels(p2p, dev, msg.channel_list,
				      msg.channel_list_len) < 0) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: No common channels found");
			status = P2P_SC_FAIL_NO_COMMON_CHANNELS;
			goto fail;
		}

		switch (msg.dev_password_id) {
		case DEV_PW_DEFAULT:
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: PIN from peer Label");
			if (dev->wps_method != WPS_PIN_KEYPAD) {
				wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
					"P2P: We have wps_method=%s -> "
					"incompatible",
					p2p_wps_method_str(dev->wps_method));
				status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
				goto fail;
			}
			break;
		case DEV_PW_REGISTRAR_SPECIFIED:
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: PIN from peer Display");
			if (dev->wps_method != WPS_PIN_KEYPAD) {
				wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
					"P2P: We have wps_method=%s -> "
					"incompatible",
					p2p_wps_method_str(dev->wps_method));
				status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
				goto fail;
			}
			break;
		case DEV_PW_USER_SPECIFIED:
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Peer entered PIN on Keypad");
			if (dev->wps_method != WPS_PIN_LABEL &&
			    dev->wps_method != WPS_PIN_DISPLAY) {
				wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
					"P2P: We have wps_method=%s -> "
					"incompatible",
					p2p_wps_method_str(dev->wps_method));
				status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
				goto fail;
			}
			break;
		case DEV_PW_PUSHBUTTON:
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Peer using pushbutton");
			if (dev->wps_method != WPS_PBC) {
				wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
					"P2P: We have wps_method=%s -> "
					"incompatible",
					p2p_wps_method_str(dev->wps_method));
				status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
				goto fail;
			}
			break;
		default:
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Unsupported Device Password ID %d",
				msg.dev_password_id);
			status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
			goto fail;
		}

		if (go) {
			struct p2p_channels intersection;
			size_t i;
			p2p_channels_intersect(&p2p->channels, &dev->channels,
					       &intersection);
			if (intersection.reg_classes == 0 ||
			    intersection.reg_class[0].channels == 0) {
				status = P2P_SC_FAIL_NO_COMMON_CHANNELS;
				wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
					"P2P: No common channels found");
				goto fail;
			}
			for (i = 0; i < intersection.reg_classes; i++) {
				struct p2p_reg_class *c;
				c = &intersection.reg_class[i];
				wpa_printf(MSG_DEBUG, "P2P: reg_class %u",
					   c->reg_class);
				wpa_hexdump(MSG_DEBUG, "P2P: channels",
					    c->channel, c->channels);
			}
			if (!p2p_channels_includes(&intersection,
						   p2p->op_reg_class,
						   p2p->op_channel))
				p2p_reselect_channel(p2p, &intersection);

			p2p_build_ssid(p2p, p2p->ssid, &p2p->ssid_len);
		}

		dev->go_state = go ? LOCAL_GO : REMOTE_GO;
		dev->oper_freq = p2p_channel_to_freq((const char *)
						     msg.operating_channel,
						     msg.operating_channel[3],
						     msg.operating_channel[4]);
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Peer operating "
			"channel preference: %d MHz", dev->oper_freq);

		if (msg.config_timeout) {
			dev->go_timeout = msg.config_timeout[0];
			dev->client_timeout = msg.config_timeout[1];
		}

		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: GO Negotiation with " MACSTR, MAC2STR(sa));
		if (p2p->state != P2P_IDLE)
			p2p_stop_find_for_freq(p2p, rx_freq);
		p2p_set_state(p2p, P2P_GO_NEG);
		p2p_clear_timeout(p2p);
		dev->dialog_token = msg.dialog_token;
		os_memcpy(dev->intended_addr, msg.intended_addr, ETH_ALEN);
		p2p->go_neg_peer = dev;
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
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Sending GO Negotiation Response");
	if (rx_freq > 0)
		freq = rx_freq;
	else
		freq = p2p_channel_to_freq(p2p->cfg->country,
					   p2p->cfg->reg_class,
					   p2p->cfg->channel);
	if (freq < 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unknown regulatory class/channel");
		wpabuf_free(resp);
		return;
	}
	if (status == P2P_SC_SUCCESS) {
		p2p->pending_action_state = P2P_PENDING_GO_NEG_RESPONSE;
		dev->flags |= P2P_DEV_WAIT_GO_NEG_CONFIRM;
	} else
		p2p->pending_action_state =
			P2P_PENDING_GO_NEG_RESPONSE_FAILURE;
	if (p2p_send_action(p2p, freq, sa, p2p->cfg->dev_addr,
			    p2p->cfg->dev_addr,
			    wpabuf_head(resp), wpabuf_len(resp), 200) < 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Failed to send Action frame");
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

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Building GO Negotiation Confirm");
	buf = wpabuf_alloc(1000);
	if (buf == NULL)
		return NULL;

	p2p_buf_add_public_action_hdr(buf, P2P_GO_NEG_CONF, dialog_token);

	len = p2p_buf_add_ie_hdr(buf);
	p2p_buf_add_status(buf, status);
	group_capab = 0;
	if (peer->go_state == LOCAL_GO) {
		if (peer->flags & P2P_DEV_PREFER_PERSISTENT_GROUP)
			group_capab |= P2P_GROUP_CAPAB_PERSISTENT_GROUP;
		if (p2p->cross_connect)
			group_capab |= P2P_GROUP_CAPAB_CROSS_CONN;
		if (p2p->cfg->p2p_intra_bss)
			group_capab |= P2P_GROUP_CAPAB_INTRA_BSS_DIST;
	}
	p2p_buf_add_capability(buf, p2p->dev_capab, group_capab);
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

	return buf;
}


void p2p_process_go_neg_resp(struct p2p_data *p2p, const u8 *sa,
			     const u8 *data, size_t len, int rx_freq)
{
	struct p2p_device *dev;
	struct wpabuf *conf;
	int go = -1;
	struct p2p_message msg;
	u8 status = P2P_SC_SUCCESS;
	int freq;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Received GO Negotiation Response from " MACSTR
		" (freq=%d)", MAC2STR(sa), rx_freq);
	dev = p2p_get_device(p2p, sa);
	if (dev == NULL || dev->wps_method == WPS_NOT_READY ||
	    dev != p2p->go_neg_peer) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Not ready for GO negotiation with " MACSTR,
			MAC2STR(sa));
		return;
	}

	if (p2p_parse(data, len, &msg))
		return;

	if (!(dev->flags & P2P_DEV_WAIT_GO_NEG_RESPONSE)) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Was not expecting GO Negotiation Response - "
			"ignore");
		p2p_parse_free(&msg);
		return;
	}
	dev->flags &= ~P2P_DEV_WAIT_GO_NEG_RESPONSE;

	if (msg.dialog_token != dev->dialog_token) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unexpected Dialog Token %u (expected %u)",
			msg.dialog_token, dev->dialog_token);
		p2p_parse_free(&msg);
		return;
	}

	if (!msg.status) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No Status attribute received");
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
	}
	if (*msg.status) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: GO Negotiation rejected: status %d",
			*msg.status);
		dev->go_neg_req_sent = 0;
		if (*msg.status == P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Wait for the peer to become ready for "
				"GO Negotiation");
			dev->flags |= P2P_DEV_NOT_YET_READY;
			dev->wait_count = 0;
			p2p_set_state(p2p, P2P_WAIT_PEER_IDLE);
			p2p_set_timeout(p2p, 0, 0);
		} else {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Stop GO Negotiation attempt");
			p2p_go_neg_failed(p2p, dev, *msg.status);
		}
		p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
		p2p_parse_free(&msg);
		return;
	}

	if (!msg.capability) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Mandatory Capability attribute missing from GO "
			"Negotiation Response");
#ifdef CONFIG_P2P_STRICT
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
#endif /* CONFIG_P2P_STRICT */
	}

	if (!msg.p2p_device_info) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Mandatory P2P Device Info attribute missing "
			"from GO Negotiation Response");
#ifdef CONFIG_P2P_STRICT
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
#endif /* CONFIG_P2P_STRICT */
	}

	if (!msg.intended_addr) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No Intended P2P Interface Address attribute "
			"received");
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
	}

	if (!msg.go_intent) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No GO Intent attribute received");
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
	}
	if ((*msg.go_intent >> 1) > P2P_MAX_GO_INTENT) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Invalid GO Intent value (%u) received",
			*msg.go_intent >> 1);
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
	}

	go = p2p_go_det(p2p->go_intent, *msg.go_intent);
	if (go < 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Incompatible GO Intent");
		status = P2P_SC_FAIL_INCOMPATIBLE_PARAMS;
		goto fail;
	}

	if (!go && msg.group_id) {
		/* Store SSID for Provisioning step */
		p2p->ssid_len = msg.group_id_len - ETH_ALEN;
		os_memcpy(p2p->ssid, msg.group_id + ETH_ALEN, p2p->ssid_len);
	} else if (!go) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Mandatory P2P Group ID attribute missing from "
			"GO Negotiation Response");
		p2p->ssid_len = 0;
#ifdef CONFIG_P2P_STRICT
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
#endif /* CONFIG_P2P_STRICT */
	}

	if (!msg.config_timeout) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Mandatory Configuration Timeout attribute "
			"missing from GO Negotiation Response");
#ifdef CONFIG_P2P_STRICT
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
#endif /* CONFIG_P2P_STRICT */
	} else {
		dev->go_timeout = msg.config_timeout[0];
		dev->client_timeout = msg.config_timeout[1];
	}

	if (!msg.operating_channel && !go) {
		/*
		 * Note: P2P Client may omit Operating Channel attribute to
		 * indicate it does not have a preference.
		 */
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No Operating Channel attribute received");
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
	}
	if (!msg.channel_list) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No Channel List attribute received");
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
	}

	if (p2p_peer_channels(p2p, dev, msg.channel_list,
			      msg.channel_list_len) < 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No common channels found");
		status = P2P_SC_FAIL_NO_COMMON_CHANNELS;
		goto fail;
	}

	if (msg.operating_channel) {
		dev->oper_freq = p2p_channel_to_freq((const char *)
						     msg.operating_channel,
						     msg.operating_channel[3],
						     msg.operating_channel[4]);
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Peer operating "
			"channel preference: %d MHz", dev->oper_freq);
	} else
		dev->oper_freq = 0;

	switch (msg.dev_password_id) {
	case DEV_PW_DEFAULT:
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: PIN from peer Label");
		if (dev->wps_method != WPS_PIN_KEYPAD) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: We have wps_method=%s -> "
				"incompatible",
				p2p_wps_method_str(dev->wps_method));
			status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
			goto fail;
		}
		break;
	case DEV_PW_REGISTRAR_SPECIFIED:
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: PIN from peer Display");
		if (dev->wps_method != WPS_PIN_KEYPAD) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: We have wps_method=%s -> "
				"incompatible",
				p2p_wps_method_str(dev->wps_method));
			status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
			goto fail;
		}
		break;
	case DEV_PW_USER_SPECIFIED:
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Peer entered PIN on Keypad");
		if (dev->wps_method != WPS_PIN_LABEL &&
		    dev->wps_method != WPS_PIN_DISPLAY) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: We have wps_method=%s -> "
				"incompatible",
				p2p_wps_method_str(dev->wps_method));
			status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
			goto fail;
		}
		break;
	case DEV_PW_PUSHBUTTON:
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Peer using pushbutton");
		if (dev->wps_method != WPS_PBC) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: We have wps_method=%s -> "
				"incompatible",
				p2p_wps_method_str(dev->wps_method));
			status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
			goto fail;
		}
		break;
	default:
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unsupported Device Password ID %d",
			msg.dev_password_id);
		status = P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD;
		goto fail;
	}

	if (go) {
		struct p2p_channels intersection;
		size_t i;
		p2p_channels_intersect(&p2p->channels, &dev->channels,
				       &intersection);
		if (intersection.reg_classes == 0 ||
		    intersection.reg_class[0].channels == 0) {
			status = P2P_SC_FAIL_NO_COMMON_CHANNELS;
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: No common channels found");
			goto fail;
		}
		for (i = 0; i < intersection.reg_classes; i++) {
			struct p2p_reg_class *c;
			c = &intersection.reg_class[i];
			wpa_printf(MSG_DEBUG, "P2P: reg_class %u",
				   c->reg_class);
			wpa_hexdump(MSG_DEBUG, "P2P: channels",
				    c->channel, c->channels);
		}
		if (!p2p_channels_includes(&intersection, p2p->op_reg_class,
					   p2p->op_channel))
			p2p_reselect_channel(p2p, &intersection);

		p2p_build_ssid(p2p, p2p->ssid, &p2p->ssid_len);
	}

	p2p_set_state(p2p, P2P_GO_NEG);
	p2p_clear_timeout(p2p);

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: GO Negotiation with " MACSTR, MAC2STR(sa));
	os_memcpy(dev->intended_addr, msg.intended_addr, ETH_ALEN);

fail:
	conf = p2p_build_go_neg_conf(p2p, dev, msg.dialog_token, status,
				     msg.operating_channel, go);
	p2p_parse_free(&msg);
	if (conf == NULL)
		return;
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Sending GO Negotiation Confirm");
	if (status == P2P_SC_SUCCESS) {
		p2p->pending_action_state = P2P_PENDING_GO_NEG_CONFIRM;
		dev->go_state = go ? LOCAL_GO : REMOTE_GO;
	} else
		p2p->pending_action_state = P2P_NO_PENDING_ACTION;
	if (rx_freq > 0)
		freq = rx_freq;
	else
		freq = dev->listen_freq;
	if (p2p_send_action(p2p, freq, sa, p2p->cfg->dev_addr, sa,
			    wpabuf_head(conf), wpabuf_len(conf), 200) < 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Failed to send Action frame");
		p2p_go_neg_failed(p2p, dev, -1);
	}
	wpabuf_free(conf);
}


void p2p_process_go_neg_conf(struct p2p_data *p2p, const u8 *sa,
			     const u8 *data, size_t len)
{
	struct p2p_device *dev;
	struct p2p_message msg;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Received GO Negotiation Confirm from " MACSTR,
		MAC2STR(sa));
	dev = p2p_get_device(p2p, sa);
	if (dev == NULL || dev->wps_method == WPS_NOT_READY ||
	    dev != p2p->go_neg_peer) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Not ready for GO negotiation with " MACSTR,
			MAC2STR(sa));
		return;
	}

	if (p2p->pending_action_state == P2P_PENDING_GO_NEG_RESPONSE) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG, "P2P: Stopped waiting "
			"for TX status on GO Negotiation Response since we "
			"already received Confirmation");
		p2p->pending_action_state = P2P_NO_PENDING_ACTION;
	}

	if (p2p_parse(data, len, &msg))
		return;

	if (!(dev->flags & P2P_DEV_WAIT_GO_NEG_CONFIRM)) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Was not expecting GO Negotiation Confirm - "
			"ignore");
		return;
	}
	dev->flags &= ~P2P_DEV_WAIT_GO_NEG_CONFIRM;

	if (msg.dialog_token != dev->dialog_token) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unexpected Dialog Token %u (expected %u)",
			msg.dialog_token, dev->dialog_token);
		p2p_parse_free(&msg);
		return;
	}

	if (!msg.status) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No Status attribute received");
		p2p_parse_free(&msg);
		return;
	}
	if (*msg.status) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: GO Negotiation rejected: status %d",
			*msg.status);
		p2p_parse_free(&msg);
		return;
	}

	if (dev->go_state == REMOTE_GO && msg.group_id) {
		/* Store SSID for Provisioning step */
		p2p->ssid_len = msg.group_id_len - ETH_ALEN;
		os_memcpy(p2p->ssid, msg.group_id + ETH_ALEN, p2p->ssid_len);
	} else if (dev->go_state == REMOTE_GO) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Mandatory P2P Group ID attribute missing from "
			"GO Negotiation Confirmation");
		p2p->ssid_len = 0;
#ifdef CONFIG_P2P_STRICT
		p2p_parse_free(&msg);
		return;
#endif /* CONFIG_P2P_STRICT */
	}

	if (!msg.operating_channel) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Mandatory Operating Channel attribute missing "
			"from GO Negotiation Confirmation");
#ifdef CONFIG_P2P_STRICT
		p2p_parse_free(&msg);
		return;
#endif /* CONFIG_P2P_STRICT */
	}

	if (!msg.channel_list) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Mandatory Operating Channel attribute missing "
			"from GO Negotiation Confirmation");
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
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unexpected GO Neg state - do not know which end "
			"becomes GO");
		return;
	}

	p2p_go_complete(p2p, dev);
}
