/*
 * Wi-Fi Direct - P2P Invitation procedure
 * Copyright (c) 2010, Atheros Communications
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "common/ieee802_11_defs.h"
#include "common/wpa_ctrl.h"
#include "p2p_i.h"
#include "p2p.h"


static struct wpabuf * p2p_build_invitation_req(struct p2p_data *p2p,
						struct p2p_device *peer,
						const u8 *go_dev_addr,
						int dev_pw_id)
{
	struct wpabuf *buf;
	u8 *len;
	const u8 *dev_addr;
	size_t extra = 0;

#ifdef CONFIG_WIFI_DISPLAY
	struct wpabuf *wfd_ie = p2p->wfd_ie_invitation;
	if (wfd_ie && p2p->inv_role == P2P_INVITE_ROLE_ACTIVE_GO) {
		size_t i;
		for (i = 0; i < p2p->num_groups; i++) {
			struct p2p_group *g = p2p->groups[i];
			struct wpabuf *ie;
			if (os_memcmp(p2p_group_get_interface_addr(g),
				      p2p->inv_bssid, ETH_ALEN) != 0)
				continue;
			ie = p2p_group_get_wfd_ie(g);
			if (ie) {
				wfd_ie = ie;
				break;
			}
		}
	}
	if (wfd_ie)
		extra = wpabuf_len(wfd_ie);
#endif /* CONFIG_WIFI_DISPLAY */

	if (p2p->vendor_elem && p2p->vendor_elem[VENDOR_ELEM_P2P_INV_REQ])
		extra += wpabuf_len(p2p->vendor_elem[VENDOR_ELEM_P2P_INV_REQ]);

	buf = wpabuf_alloc(1000 + extra);
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
		p2p_buf_add_config_timeout(buf, p2p->go_timeout,
					   p2p->client_timeout);
	p2p_buf_add_invitation_flags(buf, p2p->inv_persistent ?
				     P2P_INVITATION_FLAGS_TYPE : 0);
	if (p2p->inv_role != P2P_INVITE_ROLE_CLIENT ||
	    !(peer->flags & P2P_DEV_NO_PREF_CHAN))
		p2p_buf_add_operating_channel(buf, p2p->cfg->country,
					      p2p->op_reg_class,
					      p2p->op_channel);
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

	p2p_buf_add_pref_channel_list(buf, p2p->pref_freq_list,
				      p2p->num_pref_freq);

#ifdef CONFIG_WIFI_DISPLAY
	if (wfd_ie)
		wpabuf_put_buf(buf, wfd_ie);
#endif /* CONFIG_WIFI_DISPLAY */

	if (p2p->vendor_elem && p2p->vendor_elem[VENDOR_ELEM_P2P_INV_REQ])
		wpabuf_put_buf(buf, p2p->vendor_elem[VENDOR_ELEM_P2P_INV_REQ]);

	if (dev_pw_id >= 0) {
		/* WSC IE in Invitation Request for NFC static handover */
		p2p_build_wps_ie(p2p, buf, dev_pw_id, 0);
	}

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
	size_t extra = 0;

#ifdef CONFIG_WIFI_DISPLAY
	struct wpabuf *wfd_ie = p2p->wfd_ie_invitation;
	if (wfd_ie && group_bssid) {
		size_t i;
		for (i = 0; i < p2p->num_groups; i++) {
			struct p2p_group *g = p2p->groups[i];
			struct wpabuf *ie;
			if (os_memcmp(p2p_group_get_interface_addr(g),
				      group_bssid, ETH_ALEN) != 0)
				continue;
			ie = p2p_group_get_wfd_ie(g);
			if (ie) {
				wfd_ie = ie;
				break;
			}
		}
	}
	if (wfd_ie)
		extra = wpabuf_len(wfd_ie);
#endif /* CONFIG_WIFI_DISPLAY */

	if (p2p->vendor_elem && p2p->vendor_elem[VENDOR_ELEM_P2P_INV_RESP])
		extra += wpabuf_len(p2p->vendor_elem[VENDOR_ELEM_P2P_INV_RESP]);

	buf = wpabuf_alloc(1000 + extra);
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

#ifdef CONFIG_WIFI_DISPLAY
	if (wfd_ie)
		wpabuf_put_buf(buf, wfd_ie);
#endif /* CONFIG_WIFI_DISPLAY */

	if (p2p->vendor_elem && p2p->vendor_elem[VENDOR_ELEM_P2P_INV_RESP])
		wpabuf_put_buf(buf, p2p->vendor_elem[VENDOR_ELEM_P2P_INV_RESP]);

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
	struct p2p_channels all_channels, intersection, *channels = NULL;
	int persistent;

	os_memset(group_bssid, 0, sizeof(group_bssid));

	p2p_dbg(p2p, "Received Invitation Request from " MACSTR " (freq=%d)",
		MAC2STR(sa), rx_freq);

	if (p2p_parse(data, len, &msg))
		return;

	dev = p2p_get_device(p2p, sa);
	if (dev == NULL || (dev->flags & P2P_DEV_PROBE_REQ_ONLY)) {
		p2p_dbg(p2p, "Invitation Request from unknown peer " MACSTR,
			MAC2STR(sa));

		if (p2p_add_device(p2p, sa, rx_freq, NULL, 0, data + 1, len - 1,
				   0)) {
			p2p_dbg(p2p, "Invitation Request add device failed "
				MACSTR, MAC2STR(sa));
			status = P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE;
			goto fail;
		}

		dev = p2p_get_device(p2p, sa);
		if (dev == NULL) {
			p2p_dbg(p2p, "Reject Invitation Request from unknown peer "
				MACSTR, MAC2STR(sa));
			status = P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE;
			goto fail;
		}
	}

	if (!msg.group_id || !msg.channel_list) {
		p2p_dbg(p2p, "Mandatory attribute missing in Invitation Request from "
			MACSTR, MAC2STR(sa));
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
		p2p_dbg(p2p, "Mandatory Invitation Flags attribute missing from Invitation Request");
		persistent = 1;
	}

	p2p_channels_union(&p2p->cfg->channels, &p2p->cfg->cli_channels,
			   &all_channels);

	if (p2p_peer_channels_check(p2p, &all_channels, dev,
				    msg.channel_list, msg.channel_list_len) <
	    0) {
		p2p_dbg(p2p, "No common channels found");
		status = P2P_SC_FAIL_NO_COMMON_CHANNELS;
		goto fail;
	}

	p2p_channels_dump(p2p, "own channels", &p2p->cfg->channels);
	p2p_channels_dump(p2p, "own client channels", &all_channels);
	p2p_channels_dump(p2p, "peer channels", &dev->channels);
	p2p_channels_intersect(&all_channels, &dev->channels,
			       &intersection);
	p2p_channels_dump(p2p, "intersection", &intersection);

	if (p2p->cfg->invitation_process) {
		status = p2p->cfg->invitation_process(
			p2p->cfg->cb_ctx, sa, msg.group_bssid, msg.group_id,
			msg.group_id + ETH_ALEN, msg.group_id_len - ETH_ALEN,
			&go, group_bssid, &op_freq, persistent, &intersection,
			msg.dev_password_id_present ? msg.dev_password_id : -1);
	}

	if (go) {
		p2p_channels_intersect(&p2p->cfg->channels, &dev->channels,
				       &intersection);
		p2p_channels_dump(p2p, "intersection(GO)", &intersection);
		if (intersection.reg_classes == 0) {
			p2p_dbg(p2p, "No common channels found (GO)");
			status = P2P_SC_FAIL_NO_COMMON_CHANNELS;
			goto fail;
		}
	}

	if (op_freq) {
		p2p_dbg(p2p, "Invitation processing forced frequency %d MHz",
			op_freq);
		if (p2p_freq_to_channel(op_freq, &reg_class, &channel) < 0) {
			p2p_dbg(p2p, "Unknown forced freq %d MHz from invitation_process()",
				op_freq);
			status = P2P_SC_FAIL_NO_COMMON_CHANNELS;
			goto fail;
		}

		if (!p2p_channels_includes(&intersection, reg_class, channel))
		{
			p2p_dbg(p2p, "forced freq %d MHz not in the supported channels intersection",
				op_freq);
			status = P2P_SC_FAIL_NO_COMMON_CHANNELS;
			goto fail;
		}

		if (status == P2P_SC_SUCCESS)
			channels = &intersection;
	} else {
		p2p_dbg(p2p, "No forced channel from invitation processing - figure out best one to use");

		/* Default to own configuration as a starting point */
		p2p->op_reg_class = p2p->cfg->op_reg_class;
		p2p->op_channel = p2p->cfg->op_channel;
		p2p_dbg(p2p, "Own default op_class %d channel %d",
			p2p->op_reg_class, p2p->op_channel);

		/* Use peer preference if specified and compatible */
		if (msg.operating_channel) {
			int req_freq;
			req_freq = p2p_channel_to_freq(
				msg.operating_channel[3],
				msg.operating_channel[4]);
			p2p_dbg(p2p, "Peer operating channel preference: %d MHz",
				req_freq);
			if (req_freq > 0 &&
			    p2p_channels_includes(&intersection,
						  msg.operating_channel[3],
						  msg.operating_channel[4])) {
				p2p->op_reg_class = msg.operating_channel[3];
				p2p->op_channel = msg.operating_channel[4];
				p2p_dbg(p2p, "Use peer preference op_class %d channel %d",
					p2p->op_reg_class, p2p->op_channel);
			} else {
				p2p_dbg(p2p, "Cannot use peer channel preference");
			}
		}

		/* Reselect the channel only for the case of the GO */
		if (go &&
		    !p2p_channels_includes(&intersection, p2p->op_reg_class,
					   p2p->op_channel)) {
			p2p_dbg(p2p, "Initially selected channel (op_class %d channel %d) not in channel intersection - try to reselect",
				p2p->op_reg_class, p2p->op_channel);
			p2p_reselect_channel(p2p, &intersection);
			p2p_dbg(p2p, "Re-selection result: op_class %d channel %d",
				p2p->op_reg_class, p2p->op_channel);
			if (!p2p_channels_includes(&intersection,
						   p2p->op_reg_class,
						   p2p->op_channel)) {
				p2p_dbg(p2p, "Peer does not support selected operating channel (reg_class=%u channel=%u)",
					p2p->op_reg_class, p2p->op_channel);
				status = P2P_SC_FAIL_NO_COMMON_CHANNELS;
				goto fail;
			}
		} else if (go && !(dev->flags & P2P_DEV_FORCE_FREQ) &&
			   !p2p->cfg->cfg_op_channel) {
			p2p_dbg(p2p, "Try to reselect channel selection with peer information received; previously selected op_class %u channel %u",
				p2p->op_reg_class, p2p->op_channel);
			p2p_reselect_channel(p2p, &intersection);
		}

		/*
		 * Use the driver preferred frequency list extension if
		 * supported.
		 */
		p2p_check_pref_chan(p2p, go, dev, &msg);

		op_freq = p2p_channel_to_freq(p2p->op_reg_class,
					      p2p->op_channel);
		if (op_freq < 0) {
			p2p_dbg(p2p, "Unknown operational channel (country=%c%c reg_class=%u channel=%u)",
				p2p->cfg->country[0], p2p->cfg->country[1],
				p2p->op_reg_class, p2p->op_channel);
			status = P2P_SC_FAIL_NO_COMMON_CHANNELS;
			goto fail;
		}
		p2p_dbg(p2p, "Selected operating channel - %d MHz", op_freq);

		if (status == P2P_SC_SUCCESS) {
			reg_class = p2p->op_reg_class;
			channel = p2p->op_channel;
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
		freq = p2p_channel_to_freq(p2p->cfg->reg_class,
					   p2p->cfg->channel);
	if (freq < 0) {
		p2p_dbg(p2p, "Unknown regulatory class/channel");
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
	if (msg.group_id) {
		if (msg.group_id_len - ETH_ALEN <= SSID_MAX_LEN) {
			os_memcpy(p2p->inv_ssid, msg.group_id + ETH_ALEN,
				  msg.group_id_len - ETH_ALEN);
			p2p->inv_ssid_len = msg.group_id_len - ETH_ALEN;
		}
		os_memcpy(p2p->inv_go_dev_addr, msg.group_id, ETH_ALEN);
	} else {
		p2p->inv_ssid_len = 0;
		os_memset(p2p->inv_go_dev_addr, 0, ETH_ALEN);
	}
	p2p->inv_status = status;
	p2p->inv_op_freq = op_freq;

	p2p->pending_action_state = P2P_PENDING_INVITATION_RESPONSE;
	if (p2p_send_action(p2p, freq, sa, p2p->cfg->dev_addr,
			    p2p->cfg->dev_addr,
			    wpabuf_head(resp), wpabuf_len(resp), 50) < 0) {
		p2p_dbg(p2p, "Failed to send Action frame");
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
	struct p2p_channels intersection, *channels = NULL;

	p2p_dbg(p2p, "Received Invitation Response from " MACSTR,
		MAC2STR(sa));

	dev = p2p_get_device(p2p, sa);
	if (dev == NULL) {
		p2p_dbg(p2p, "Ignore Invitation Response from unknown peer "
			MACSTR, MAC2STR(sa));
		p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
		return;
	}

	if (dev != p2p->invite_peer) {
		p2p_dbg(p2p, "Ignore unexpected Invitation Response from peer "
			MACSTR, MAC2STR(sa));
		p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
		return;
	}

	if (p2p_parse(data, len, &msg)) {
		p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
		return;
	}

	if (!msg.status) {
		p2p_dbg(p2p, "Mandatory Status attribute missing in Invitation Response from "
			MACSTR, MAC2STR(sa));
		p2p_parse_free(&msg);
		p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
		return;
	}

	/*
	 * We should not really receive a replayed response twice since
	 * duplicate frames are supposed to be dropped. However, not all drivers
	 * do that for pre-association frames. We did not use to verify dialog
	 * token matches for invitation response frames, but that check can be
	 * safely used to drop a replayed response to the previous Invitation
	 * Request in case the suggested operating channel was changed. This
	 * allows a duplicated reject frame to be dropped with the assumption
	 * that the real response follows after it.
	 */
	if (*msg.status == P2P_SC_FAIL_NO_COMMON_CHANNELS &&
	    p2p->retry_invite_req_sent &&
	    msg.dialog_token != dev->dialog_token) {
		p2p_dbg(p2p, "Unexpected Dialog Token %u (expected %u)",
			msg.dialog_token, dev->dialog_token);
		p2p_parse_free(&msg);
		return;
	}

	if (*msg.status == P2P_SC_FAIL_NO_COMMON_CHANNELS &&
	    p2p->retry_invite_req &&
	    p2p_channel_random_social(&p2p->cfg->channels, &p2p->op_reg_class,
				      &p2p->op_channel) == 0) {
		p2p->retry_invite_req = 0;
		p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
		p2p->cfg->stop_listen(p2p->cfg->cb_ctx);
		p2p_set_state(p2p, P2P_INVITE);
		p2p_dbg(p2p, "Resend Invitation Request setting op_class %u channel %u as operating channel",
			p2p->op_reg_class, p2p->op_channel);
		p2p->retry_invite_req_sent = 1;
		p2p_invite_send(p2p, p2p->invite_peer, p2p->invite_go_dev_addr,
				p2p->invite_dev_pw_id);
		p2p_parse_free(&msg);
		return;
	}
	p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
	p2p->retry_invite_req = 0;

	if (!msg.channel_list && *msg.status == P2P_SC_SUCCESS) {
		p2p_dbg(p2p, "Mandatory Channel List attribute missing in Invitation Response from "
			MACSTR, MAC2STR(sa));
#ifdef CONFIG_P2P_STRICT
		p2p_parse_free(&msg);
		return;
#endif /* CONFIG_P2P_STRICT */
		/* Try to survive without peer channel list */
		channels = &p2p->channels;
	} else if (!msg.channel_list) {
		/* Non-success cases are not required to include Channel List */
		channels = &p2p->channels;
	} else if (p2p_peer_channels_check(p2p, &p2p->channels, dev,
					   msg.channel_list,
					   msg.channel_list_len) < 0) {
		p2p_dbg(p2p, "No common channels found");
		p2p_parse_free(&msg);
		return;
	} else {
		p2p_channels_intersect(&p2p->channels, &dev->channels,
				       &intersection);
		channels = &intersection;
	}

	if (p2p->cfg->invitation_result) {
		int peer_oper_freq = 0;
		int freq = p2p_channel_to_freq(p2p->op_reg_class,
					       p2p->op_channel);
		if (freq < 0)
			freq = 0;

		if (msg.operating_channel) {
			peer_oper_freq = p2p_channel_to_freq(
				msg.operating_channel[3],
				msg.operating_channel[4]);
			if (peer_oper_freq < 0)
				peer_oper_freq = 0;
		}

		/*
		 * Use the driver preferred frequency list extension if
		 * supported.
		 */
		p2p_check_pref_chan(p2p, 0, dev, &msg);

		p2p->cfg->invitation_result(p2p->cfg->cb_ctx, *msg.status,
					    msg.group_bssid, channels, sa,
					    freq, peer_oper_freq);
	}

	p2p_parse_free(&msg);

	p2p_clear_timeout(p2p);
	p2p_set_state(p2p, P2P_IDLE);
	p2p->invite_peer = NULL;
}


int p2p_invite_send(struct p2p_data *p2p, struct p2p_device *dev,
		    const u8 *go_dev_addr, int dev_pw_id)
{
	struct wpabuf *req;
	int freq;

	freq = dev->listen_freq > 0 ? dev->listen_freq : dev->oper_freq;
	if (freq <= 0)
		freq = dev->oob_go_neg_freq;
	if (freq <= 0) {
		p2p_dbg(p2p, "No Listen/Operating frequency known for the peer "
			MACSTR " to send Invitation Request",
			MAC2STR(dev->info.p2p_device_addr));
		return -1;
	}

	req = p2p_build_invitation_req(p2p, dev, go_dev_addr, dev_pw_id);
	if (req == NULL)
		return -1;
	if (p2p->state != P2P_IDLE)
		p2p_stop_listen_for_freq(p2p, freq);
	p2p_dbg(p2p, "Sending Invitation Request");
	p2p_set_state(p2p, P2P_INVITE);
	p2p->pending_action_state = P2P_PENDING_INVITATION_REQUEST;
	p2p->invite_peer = dev;
	dev->invitation_reqs++;
	if (p2p_send_action(p2p, freq, dev->info.p2p_device_addr,
			    p2p->cfg->dev_addr, dev->info.p2p_device_addr,
			    wpabuf_head(req), wpabuf_len(req), 500) < 0) {
		p2p_dbg(p2p, "Failed to send Action frame");
		/* Use P2P find to recover and retry */
		p2p_set_timeout(p2p, 0, 0);
	} else {
		dev->flags |= P2P_DEV_WAIT_INV_REQ_ACK;
	}

	wpabuf_free(req);

	return 0;
}


void p2p_invitation_req_cb(struct p2p_data *p2p, int success)
{
	p2p_dbg(p2p, "Invitation Request TX callback: success=%d", success);

	if (p2p->invite_peer == NULL) {
		p2p_dbg(p2p, "No pending Invite");
		return;
	}

	if (success)
		p2p->invite_peer->flags &= ~P2P_DEV_WAIT_INV_REQ_ACK;

	/*
	 * Use P2P find, if needed, to find the other device from its listen
	 * channel.
	 */
	p2p_set_state(p2p, P2P_INVITE);
	p2p_set_timeout(p2p, 0, success ? 500000 : 100000);
}


void p2p_invitation_resp_cb(struct p2p_data *p2p, int success)
{
	p2p_dbg(p2p, "Invitation Response TX callback: success=%d", success);
	p2p->cfg->send_action_done(p2p->cfg->cb_ctx);

	if (!success)
		p2p_dbg(p2p, "Assume Invitation Response was actually received by the peer even though Ack was not reported");

	if (p2p->cfg->invitation_received) {
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
	       int persistent_group, unsigned int pref_freq, int dev_pw_id)
{
	struct p2p_device *dev;

	p2p_dbg(p2p, "Request to invite peer " MACSTR " role=%d persistent=%d "
		"force_freq=%u",
		MAC2STR(peer), role, persistent_group, force_freq);
	if (bssid)
		p2p_dbg(p2p, "Invitation for BSSID " MACSTR, MAC2STR(bssid));
	if (go_dev_addr) {
		p2p_dbg(p2p, "Invitation for GO Device Address " MACSTR,
			MAC2STR(go_dev_addr));
		os_memcpy(p2p->invite_go_dev_addr_buf, go_dev_addr, ETH_ALEN);
		p2p->invite_go_dev_addr = p2p->invite_go_dev_addr_buf;
	} else
		p2p->invite_go_dev_addr = NULL;
	wpa_hexdump_ascii(MSG_DEBUG, "Invitation for SSID",
			  ssid, ssid_len);
	if (dev_pw_id >= 0) {
		p2p_dbg(p2p, "Invitation to use Device Password ID %d",
			dev_pw_id);
	}
	p2p->invite_dev_pw_id = dev_pw_id;
	p2p->retry_invite_req = role == P2P_INVITE_ROLE_GO &&
		persistent_group && !force_freq;
	p2p->retry_invite_req_sent = 0;

	dev = p2p_get_device(p2p, peer);
	if (dev == NULL || (dev->listen_freq <= 0 && dev->oper_freq <= 0 &&
			    dev->oob_go_neg_freq <= 0)) {
		p2p_dbg(p2p, "Cannot invite unknown P2P Device " MACSTR,
			MAC2STR(peer));
		return -1;
	}

	if (p2p_prepare_channel(p2p, dev, force_freq, pref_freq,
				role != P2P_INVITE_ROLE_CLIENT) < 0)
		return -1;

	if (persistent_group && role == P2P_INVITE_ROLE_CLIENT && !force_freq &&
	    !pref_freq)
		dev->flags |= P2P_DEV_NO_PREF_CHAN;
	else
		dev->flags &= ~P2P_DEV_NO_PREF_CHAN;

	if (dev->flags & P2P_DEV_GROUP_CLIENT_ONLY) {
		if (!(dev->info.dev_capab &
		      P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY)) {
			p2p_dbg(p2p, "Cannot invite a P2P Device " MACSTR
				" that is in a group and is not discoverable",
				MAC2STR(peer));
		}
		/* TODO: use device discoverability request through GO */
	}

	dev->invitation_reqs = 0;

	if (p2p->state != P2P_IDLE)
		p2p_stop_find(p2p);

	p2p->inv_role = role;
	p2p->inv_bssid_set = bssid != NULL;
	if (bssid)
		os_memcpy(p2p->inv_bssid, bssid, ETH_ALEN);
	os_memcpy(p2p->inv_ssid, ssid, ssid_len);
	p2p->inv_ssid_len = ssid_len;
	p2p->inv_persistent = persistent_group;
	return p2p_invite_send(p2p, dev, go_dev_addr, dev_pw_id);
}
