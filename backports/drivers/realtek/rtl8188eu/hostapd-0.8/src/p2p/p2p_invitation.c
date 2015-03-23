/*
 * Wi-Fi Direct - P2P Invitation procedure
 * Copyright (c) 2010, Atheros Communications
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
#include "p2p_i.h"
#include "p2p.h"


static struct wpabuf * p2p_build_invitation_req(struct p2p_data *p2p,
						struct p2p_device *peer,
						const u8 *go_dev_addr)
{
	struct wpabuf *buf;
	u8 *len;
	const u8 *dev_addr;

	buf = wpabuf_alloc(1000);
	if (buf == NULL)
		return NULL;

	peer->dialog_token++;
	if (peer->dialog_token == 0)
		peer->dialog_token = 1;
	p2p_buf_add_public_action_hdr(buf, P2P_INVITATION_REQ,
				      peer->dialog_token);

	len = p2p_buf_add_ie_hdr(buf);
	if (p2p->inv_role == P2P_INVITE_ROLE_ACTIVE_GO || !p2p->inv_persistent)
		p2p_buf_add_config_timeout(buf, 0, 0);
	else
		p2p_buf_add_config_timeout(buf, 100, 20);
	p2p_buf_add_invitation_flags(buf, p2p->inv_persistent ?
				     P2P_INVITATION_FLAGS_TYPE : 0);
	p2p_buf_add_operating_channel(buf, p2p->cfg->country,
				      p2p->op_reg_class, p2p->op_channel);
	if (p2p->inv_bssid_set)
		p2p_buf_add_group_bssid(buf, p2p->inv_bssid);
	p2p_buf_add_channel_list(buf, p2p->cfg->country, &p2p->channels);
	if (go_dev_addr)
		dev_addr = go_dev_addr;
	else if (p2p->inv_role == P2P_INVITE_ROLE_CLIENT)
		dev_addr = peer->info.p2p_device_addr;
	else
		dev_addr = p2p->cfg->dev_addr;
	p2p_buf_add_group_id(buf, dev_addr, p2p->inv_ssid, p2p->inv_ssid_len);
	p2p_buf_add_device_info(buf, p2p, peer);
	p2p_buf_update_ie_hdr(buf, len);

	return buf;
}


static struct wpabuf * p2p_build_invitation_resp(struct p2p_data *p2p,
						 struct p2p_device *peer,
						 u8 dialog_token, u8 status,
						 const u8 *group_bssid,
						 u8 reg_class, u8 channel,
						 struct p2p_channels *channels)
{
	struct wpabuf *buf;
	u8 *len;

	buf = wpabuf_alloc(1000);
	if (buf == NULL)
		return NULL;

	p2p_buf_add_public_action_hdr(buf, P2P_INVITATION_RESP,
				      dialog_token);

	len = p2p_buf_add_ie_hdr(buf);
	p2p_buf_add_status(buf, status);
	p2p_buf_add_config_timeout(buf, 0, 0); /* FIX */
	if (reg_class && channel)
		p2p_buf_add_operating_channel(buf, p2p->cfg->country,
					      reg_class, channel);
	if (group_bssid)
		p2p_buf_add_group_bssid(buf, group_bssid);
	if (channels)
		p2p_buf_add_channel_list(buf, p2p->cfg->country, channels);
	p2p_buf_update_ie_hdr(buf, len);

	return buf;
}


void p2p_process_invitation_req(struct p2p_data *p2p, const u8 *sa,
				const u8 *data, size_t len, int rx_freq)
{
	struct p2p_device *dev;
	struct p2p_message msg;
	struct wpabuf *resp = NULL;
	u8 status = P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE;
	int freq;
	int go = 0;
	u8 group_bssid[ETH_ALEN], *bssid;
	int op_freq = 0;
	u8 reg_class = 0, channel = 0;
	struct p2p_channels intersection, *channels = NULL;
	int persistent;

	os_memset(group_bssid, 0, sizeof(group_bssid));

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Received Invitation Request from " MACSTR " (freq=%d)",
		MAC2STR(sa), rx_freq);

	if (p2p_parse(data, len, &msg))
		return;

	dev = p2p_get_device(p2p, sa);
	if (dev == NULL || (dev->flags & P2P_DEV_PROBE_REQ_ONLY)) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Invitation Request from unknown peer "
			MACSTR, MAC2STR(sa));

		if (p2p_add_device(p2p, sa, rx_freq, 0, data + 1, len - 1)) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Invitation Request add device failed "
				MACSTR, MAC2STR(sa));
			status = P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE;
			goto fail;
		}

		dev = p2p_get_device(p2p, sa);
		if (dev == NULL) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Reject Invitation Request from unknown "
				"peer " MACSTR, MAC2STR(sa));
			status = P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE;
			goto fail;
		}
	}

	if (!msg.group_id || !msg.channel_list) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Mandatory attribute missing in Invitation "
			"Request from " MACSTR, MAC2STR(sa));
		status = P2P_SC_FAIL_INVALID_PARAMS;
		goto fail;
	}

	if (msg.invitation_flags)
		persistent = *msg.invitation_flags & P2P_INVITATION_FLAGS_TYPE;
	else {
		/* Invitation Flags is a mandatory attribute starting from P2P
		 * spec 1.06. As a backwards compatibility mechanism, assume
		 * the request was for a persistent group if the attribute is
		 * missing.
		 */
		wpa_printf(MSG_DEBUG, "P2P: Mandatory Invitation Flags "
			   "attribute missing from Invitation Request");
		persistent = 1;
	}

	if (p2p_peer_channels_check(p2p, &p2p->cfg->channels, dev,
				    msg.channel_list, msg.channel_list_len) <
	    0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No common channels found");
		status = P2P_SC_FAIL_NO_COMMON_CHANNELS;
		goto fail;
	}

	if (p2p->cfg->invitation_process) {
		status = p2p->cfg->invitation_process(
			p2p->cfg->cb_ctx, sa, msg.group_bssid, msg.group_id,
			msg.group_id + ETH_ALEN, msg.group_id_len - ETH_ALEN,
			&go, group_bssid, &op_freq, persistent);
	}

	if (op_freq) {
		if (p2p_freq_to_channel(p2p->cfg->country, op_freq,
					&reg_class, &channel) < 0) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Unknown forced freq %d MHz from "
				"invitation_process()", op_freq);
			status = P2P_SC_FAIL_NO_COMMON_CHANNELS;
			goto fail;
		}

		p2p_channels_intersect(&p2p->cfg->channels, &dev->channels,
				       &intersection);
		if (!p2p_channels_includes(&intersection, reg_class, channel))
		{
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: forced freq %d MHz not in the supported "
				"channels interaction", op_freq);
			status = P2P_SC_FAIL_NO_COMMON_CHANNELS;
			goto fail;
		}

		if (status == P2P_SC_SUCCESS)
			channels = &intersection;
	} else {
		op_freq = p2p_channel_to_freq(p2p->cfg->country,
					      p2p->cfg->op_reg_class,
					      p2p->cfg->op_channel);
		if (op_freq < 0) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Unknown operational channel "
				"(country=%c%c reg_class=%u channel=%u)",
				p2p->cfg->country[0], p2p->cfg->country[1],
				p2p->cfg->op_reg_class, p2p->cfg->op_channel);
			status = P2P_SC_FAIL_NO_COMMON_CHANNELS;
			goto fail;
		}

		p2p_channels_intersect(&p2p->cfg->channels, &dev->channels,
				       &intersection);
		if (status == P2P_SC_SUCCESS) {
			reg_class = p2p->cfg->op_reg_class;
			channel = p2p->cfg->op_channel;
			channels = &intersection;
		}
	}

fail:
	if (go && status == P2P_SC_SUCCESS && !is_zero_ether_addr(group_bssid))
		bssid = group_bssid;
	else
		bssid = NULL;
	resp = p2p_build_invitation_resp(p2p, dev, msg.dialog_token, status,
					 bssid, reg_class, channel, channels);

	if (resp == NULL)
		goto out;

	if (rx_freq > 0)
		freq = rx_freq;
	else
		freq = p2p_channel_to_freq(p2p->cfg->country,
					   p2p->cfg->reg_class,
					   p2p->cfg->channel);
	if (freq < 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Unknown regulatory class/channel");
		goto out;
	}

	/*
	 * Store copy of invitation data to be used when processing TX status
	 * callback for the Acton frame.
	 */
	os_memcpy(p2p->inv_sa, sa, ETH_ALEN);
	if (msg.group_bssid) {
		os_memcpy(p2p->inv_group_bssid, msg.group_bssid, ETH_ALEN);
		p2p->inv_group_bssid_ptr = p2p->inv_group_bssid;
	} else
		p2p->inv_group_bssid_ptr = NULL;
	if (msg.group_id_len - ETH_ALEN <= 32) {
		os_memcpy(p2p->inv_ssid, msg.group_id + ETH_ALEN,
			  msg.group_id_len - ETH_ALEN);
		p2p->inv_ssid_len = msg.group_id_len - ETH_ALEN;
	}
	os_memcpy(p2p->inv_go_dev_addr, msg.group_id, ETH_ALEN);
	p2p->inv_status = status;
	p2p->inv_op_freq = op_freq;

	p2p->pending_action_state = P2P_PENDING_INVITATION_RESPONSE;
	if (p2p_send_action(p2p, freq, sa, p2p->cfg->dev_addr,
			    p2p->cfg->dev_addr,
			    wpabuf_head(resp), wpabuf_len(resp), 200) < 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Failed to send Action frame");
	}

out:
	wpabuf_free(resp);
	p2p_parse_free(&msg);
}


void p2p_process_invitation_resp(struct p2p_data *p2p, const u8 *sa,
				 const u8 *data, size_t len)
{
	struct p2p_device *dev;
	struct p2p_message msg;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Received Invitation Response from " MACSTR,
		MAC2STR(sa));

	dev = p2p_get_device(p2p, sa);
	if (dev == NULL) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Ignore Invitation Response from unknown peer "
			MACSTR, MAC2STR(sa));
		return;
	}

	if (dev != p2p->invite_peer) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Ignore unexpected Invitation Response from peer "
			MACSTR, MAC2STR(sa));
		return;
	}

	if (p2p_parse(data, len, &msg))
		return;

	if (!msg.status) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Mandatory Status attribute missing in "
			"Invitation Response from " MACSTR, MAC2STR(sa));
		p2p_parse_free(&msg);
		return;
	}

	if (p2p->cfg->invitation_result)
		p2p->cfg->invitation_result(p2p->cfg->cb_ctx, *msg.status,
					    msg.group_bssid);

	p2p_parse_free(&msg);

	p2p_clear_timeout(p2p);
	p2p_set_state(p2p, P2P_IDLE);
	p2p->invite_peer = NULL;
}


int p2p_invite_send(struct p2p_data *p2p, struct p2p_device *dev,
		    const u8 *go_dev_addr)
{
	struct wpabuf *req;
	int freq;

	freq = dev->listen_freq > 0 ? dev->listen_freq : dev->oper_freq;
	if (freq <= 0) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No Listen/Operating frequency known for the "
			"peer " MACSTR " to send Invitation Request",
			MAC2STR(dev->info.p2p_device_addr));
		return -1;
	}

	req = p2p_build_invitation_req(p2p, dev, go_dev_addr);
	if (req == NULL)
		return -1;
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Sending Invitation Request");
	p2p_set_state(p2p, P2P_INVITE);
	p2p->pending_action_state = P2P_PENDING_INVITATION_REQUEST;
	p2p->invite_peer = dev;
	dev->invitation_reqs++;
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


void p2p_invitation_req_cb(struct p2p_data *p2p, int success)
{
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Invitation Request TX callback: success=%d", success);

	if (p2p->invite_peer == NULL) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: No pending Invite");
		return;
	}

	/*
	 * Use P2P find, if needed, to find the other device from its listen
	 * channel.
	 */
	p2p_set_state(p2p, P2P_INVITE);
	p2p_set_timeout(p2p, 0, 100000);
}


void p2p_invitation_resp_cb(struct p2p_data *p2p, int success)
{
	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Invitation Response TX callback: success=%d", success);
	p2p->cfg->send_action_done(p2p->cfg->cb_ctx);

	if (success && p2p->cfg->invitation_received) {
		p2p->cfg->invitation_received(p2p->cfg->cb_ctx,
					      p2p->inv_sa,
					      p2p->inv_group_bssid_ptr,
					      p2p->inv_ssid, p2p->inv_ssid_len,
					      p2p->inv_go_dev_addr,
					      p2p->inv_status,
					      p2p->inv_op_freq);
	}
}


int p2p_invite(struct p2p_data *p2p, const u8 *peer, enum p2p_invite_role role,
	       const u8 *bssid, const u8 *ssid, size_t ssid_len,
	       unsigned int force_freq, const u8 *go_dev_addr,
	       int persistent_group)
{
	struct p2p_device *dev;

	wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
		"P2P: Request to invite peer " MACSTR " role=%d persistent=%d "
		"force_freq=%u",
		MAC2STR(peer), role, persistent_group, force_freq);
	if (bssid)
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Invitation for BSSID " MACSTR, MAC2STR(bssid));
	if (go_dev_addr) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Invitation for GO Device Address " MACSTR,
			MAC2STR(go_dev_addr));
		os_memcpy(p2p->invite_go_dev_addr_buf, go_dev_addr, ETH_ALEN);
		p2p->invite_go_dev_addr = p2p->invite_go_dev_addr_buf;
	} else
		p2p->invite_go_dev_addr = NULL;
	wpa_hexdump_ascii(MSG_DEBUG, "P2P: Invitation for SSID",
			  ssid, ssid_len);

	dev = p2p_get_device(p2p, peer);
	if (dev == NULL || (dev->listen_freq <= 0 && dev->oper_freq <= 0)) {
		wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
			"P2P: Cannot invite unknown P2P Device " MACSTR,
			MAC2STR(peer));
		return -1;
	}

	if (dev->flags & P2P_DEV_GROUP_CLIENT_ONLY) {
		if (!(dev->info.dev_capab &
		      P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY)) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Cannot invite a P2P Device " MACSTR
				" that is in a group and is not discoverable",
				MAC2STR(peer));
		}
		/* TODO: use device discoverability request through GO */
	}

	dev->invitation_reqs = 0;

	if (force_freq) {
		if (p2p_freq_to_channel(p2p->cfg->country, force_freq,
					&p2p->op_reg_class, &p2p->op_channel) <
		    0) {
			wpa_msg(p2p->cfg->msg_ctx, MSG_DEBUG,
				"P2P: Unsupported frequency %u MHz",
				force_freq);
			return -1;
		}
		p2p->channels.reg_classes = 1;
		p2p->channels.reg_class[0].channels = 1;
		p2p->channels.reg_class[0].reg_class = p2p->op_reg_class;
		p2p->channels.reg_class[0].channel[0] = p2p->op_channel;
	} else {
		p2p->op_reg_class = p2p->cfg->op_reg_class;
		p2p->op_channel = p2p->cfg->op_channel;
		os_memcpy(&p2p->channels, &p2p->cfg->channels,
			  sizeof(struct p2p_channels));
	}

	if (p2p->state != P2P_IDLE)
		p2p_stop_find(p2p);

	p2p->inv_role = role;
	p2p->inv_bssid_set = bssid != NULL;
	if (bssid)
		os_memcpy(p2p->inv_bssid, bssid, ETH_ALEN);
	os_memcpy(p2p->inv_ssid, ssid, ssid_len);
	p2p->inv_ssid_len = ssid_len;
	p2p->inv_persistent = persistent_group;
	return p2p_invite_send(p2p, dev, go_dev_addr);
}
