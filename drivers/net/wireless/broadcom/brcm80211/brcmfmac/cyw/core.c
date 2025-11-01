// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2022 Broadcom Corporation
 */
#include <linux/errno.h>
#include <linux/types.h>
#include <core.h>
#include <bus.h>
#include <fwvid.h>
#include <fwil.h>
#include <fweh.h>

#include "vops.h"
#include "fwil_types.h"

/* event definitions */
#define BRCMF_CYW_E_EXT_AUTH_REQ	187
#define BRCMF_CYW_E_EXT_AUTH_FRAME_RX	188
#define BRCMF_CYW_E_MGMT_FRAME_TXS	189
#define BRCMF_CYW_E_MGMT_FRAME_TXS_OC	190
#define BRCMF_CYW_E_LAST		197

#define MGMT_AUTH_FRAME_DWELL_TIME	4000
#define MGMT_AUTH_FRAME_WAIT_TIME	(MGMT_AUTH_FRAME_DWELL_TIME + 100)

static int brcmf_cyw_set_sae_pwd(struct brcmf_if *ifp,
				 struct cfg80211_crypto_settings *crypto)
{
	struct brcmf_pub *drvr = ifp->drvr;
	struct brcmf_wsec_sae_pwd_le sae_pwd;
	u16 pwd_len = crypto->sae_pwd_len;
	int err;

	if (pwd_len > BRCMF_WSEC_MAX_SAE_PASSWORD_LEN) {
		bphy_err(drvr, "sae_password must be less than %d\n",
			 BRCMF_WSEC_MAX_SAE_PASSWORD_LEN);
		return -EINVAL;
	}

	sae_pwd.key_len = cpu_to_le16(pwd_len);
	memcpy(sae_pwd.key, crypto->sae_pwd, pwd_len);

	err = brcmf_fil_iovar_data_set(ifp, "sae_password", &sae_pwd,
				       sizeof(sae_pwd));
	if (err < 0)
		bphy_err(drvr, "failed to set SAE password in firmware (len=%u)\n",
			 pwd_len);

	return err;
}

static const struct brcmf_fweh_event_map brcmf_cyw_event_map = {
	.items = {
		{ BRCMF_E_EXT_AUTH_REQ, BRCMF_CYW_E_EXT_AUTH_REQ },
		{ BRCMF_E_EXT_AUTH_FRAME_RX, BRCMF_CYW_E_EXT_AUTH_FRAME_RX },
		{ BRCMF_E_MGMT_FRAME_TXSTATUS, BRCMF_CYW_E_MGMT_FRAME_TXS },
		{
			BRCMF_E_MGMT_FRAME_OFFCHAN_DONE,
			BRCMF_CYW_E_MGMT_FRAME_TXS_OC
		},
	},
	.n_items = 4
};

static int brcmf_cyw_alloc_fweh_info(struct brcmf_pub *drvr)
{
	struct brcmf_fweh_info *fweh;

	fweh = kzalloc(struct_size(fweh, evt_handler, BRCMF_CYW_E_LAST),
		       GFP_KERNEL);
	if (!fweh)
		return -ENOMEM;

	fweh->num_event_codes = BRCMF_CYW_E_LAST;
	fweh->event_map = &brcmf_cyw_event_map;
	drvr->fweh = fweh;
	return 0;
}

static int brcmf_cyw_activate_events(struct brcmf_if *ifp)
{
	struct brcmf_fweh_info *fweh = ifp->drvr->fweh;
	struct brcmf_eventmsgs_ext *eventmask_msg;
	u32 msglen;
	int err;

	msglen = sizeof(*eventmask_msg) + fweh->event_mask_len;
	eventmask_msg = kzalloc(msglen, GFP_KERNEL);
	if (!eventmask_msg)
		return -ENOMEM;
	eventmask_msg->ver = EVENTMSGS_VER;
	eventmask_msg->command = CYW_EVENTMSGS_SET_MASK;
	eventmask_msg->len = fweh->event_mask_len;
	memcpy(eventmask_msg->mask, fweh->event_mask, fweh->event_mask_len);

	err = brcmf_fil_iovar_data_set(ifp, "event_msgs_ext", eventmask_msg,
				       msglen);
	kfree(eventmask_msg);
	return err;
}

static
int brcmf_cyw_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
		      struct cfg80211_mgmt_tx_params *params, u64 *cookie)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct ieee80211_channel *chan = params->chan;
	struct brcmf_pub *drvr = cfg->pub;
	const u8 *buf = params->buf;
	size_t len = params->len;
	const struct ieee80211_mgmt *mgmt;
	struct brcmf_cfg80211_vif *vif;
	s32 err = 0;
	bool ack = false;
	__le16 hw_ch;
	struct brcmf_mf_params_le *mf_params;
	u32 mf_params_len;
	s32 ready;

	brcmf_dbg(TRACE, "Enter\n");

	mgmt = (const struct ieee80211_mgmt *)buf;

	if (!ieee80211_is_auth(mgmt->frame_control))
		return brcmf_cfg80211_mgmt_tx(wiphy, wdev, params, cookie);

	*cookie = 0;
	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);

	reinit_completion(&vif->mgmt_tx);
	clear_bit(BRCMF_MGMT_TX_ACK, &vif->mgmt_tx_status);
	clear_bit(BRCMF_MGMT_TX_NOACK, &vif->mgmt_tx_status);
	clear_bit(BRCMF_MGMT_TX_OFF_CHAN_COMPLETED,
		  &vif->mgmt_tx_status);
	mf_params_len = offsetof(struct brcmf_mf_params_le, data) +
			(len - DOT11_MGMT_HDR_LEN);
	mf_params = kzalloc(mf_params_len, GFP_KERNEL);
	if (!mf_params)
		return -ENOMEM;

	mf_params->dwell_time = cpu_to_le32(MGMT_AUTH_FRAME_DWELL_TIME);
	mf_params->len = cpu_to_le16(len - DOT11_MGMT_HDR_LEN);
	mf_params->frame_control = mgmt->frame_control;

	if (chan) {
		hw_ch = cpu_to_le16(chan->hw_value);
	} else {
		err = brcmf_fil_cmd_data_get(vif->ifp, BRCMF_C_GET_CHANNEL,
					     &hw_ch, sizeof(hw_ch));
		if (err) {
			bphy_err(drvr, "unable to get current hw channel\n");
			goto free;
		}
	}
	mf_params->channel = hw_ch;

	memcpy(&mf_params->da[0], &mgmt->da[0], ETH_ALEN);
	memcpy(&mf_params->bssid[0], &mgmt->bssid[0], ETH_ALEN);
	mf_params->packet_id = cpu_to_le32(*cookie);
	memcpy(mf_params->data, &buf[DOT11_MGMT_HDR_LEN],
	       le16_to_cpu(mf_params->len));

	brcmf_dbg(TRACE, "Auth frame, cookie=%d, fc=%04x, len=%d, channel=%d\n",
		  le32_to_cpu(mf_params->packet_id),
		  le16_to_cpu(mf_params->frame_control),
		  le16_to_cpu(mf_params->len),
		  le16_to_cpu(mf_params->channel));

	vif->mgmt_tx_id = le32_to_cpu(mf_params->packet_id);
	set_bit(BRCMF_MGMT_TX_SEND_FRAME, &vif->mgmt_tx_status);

	err = brcmf_fil_bsscfg_data_set(vif->ifp, "mgmt_frame",
					mf_params, mf_params_len);
	if (err) {
		bphy_err(drvr, "Failed to send Auth frame: err=%d\n",
			 err);
		goto tx_status;
	}

	ready = wait_for_completion_timeout(&vif->mgmt_tx,
					    MGMT_AUTH_FRAME_WAIT_TIME);
	if (test_bit(BRCMF_MGMT_TX_ACK, &vif->mgmt_tx_status)) {
		brcmf_dbg(TRACE, "TX Auth frame operation is success\n");
		ack = true;
	} else {
		bphy_err(drvr, "TX Auth frame operation is %s: status=%ld)\n",
			 ready ? "failed" : "timedout", vif->mgmt_tx_status);
	}

tx_status:
	cfg80211_mgmt_tx_status(wdev, *cookie, buf, len, ack,
				GFP_KERNEL);
free:
	kfree(mf_params);
	return err;
}

static int
brcmf_cyw_external_auth(struct wiphy *wiphy, struct net_device *dev,
			struct cfg80211_external_auth_params *params)
{
	struct brcmf_if *ifp;
	struct brcmf_pub *drvr;
	struct brcmf_auth_req_status_le auth_status;
	int ret = 0;

	brcmf_dbg(TRACE, "Enter\n");

	ifp = netdev_priv(dev);
	drvr = ifp->drvr;
	if (params->status == WLAN_STATUS_SUCCESS) {
		auth_status.flags = cpu_to_le16(BRCMF_EXTAUTH_SUCCESS);
	} else {
		bphy_err(drvr, "External authentication failed: status=%d\n",
			 params->status);
		auth_status.flags = cpu_to_le16(BRCMF_EXTAUTH_FAIL);
	}

	memcpy(auth_status.peer_mac, params->bssid, ETH_ALEN);
	params->ssid.ssid_len = min_t(u8, params->ssid.ssid_len,
				      IEEE80211_MAX_SSID_LEN);
	auth_status.ssid_len = cpu_to_le32(params->ssid.ssid_len);
	memcpy(auth_status.ssid, params->ssid.ssid, params->ssid.ssid_len);

	ret = brcmf_fil_iovar_data_set(ifp, "auth_status", &auth_status,
				       sizeof(auth_status));
	if (ret < 0)
		bphy_err(drvr, "auth_status iovar failed: ret=%d\n", ret);

	return ret;
}

static void brcmf_cyw_get_cfg80211_ops(struct brcmf_pub *drvr)
{
	drvr->ops->mgmt_tx = brcmf_cyw_mgmt_tx;
	drvr->ops->external_auth = brcmf_cyw_external_auth;
}

static s32
brcmf_cyw_notify_ext_auth_req(struct brcmf_if *ifp,
			      const struct brcmf_event_msg *e, void *data)
{
	struct brcmf_pub *drvr = ifp->drvr;
	struct cfg80211_external_auth_params params;
	struct brcmf_auth_req_status_le *auth_req =
		(struct brcmf_auth_req_status_le *)data;
	s32 err = 0;

	brcmf_dbg(INFO, "Enter: event %s (%d) received\n",
		  brcmf_fweh_event_name(e->event_code), e->event_code);

	if (e->datalen < sizeof(*auth_req)) {
		bphy_err(drvr, "Event %s (%d) data too small. Ignore\n",
			 brcmf_fweh_event_name(e->event_code), e->event_code);
		return -EINVAL;
	}

	memset(&params, 0, sizeof(params));
	params.action = NL80211_EXTERNAL_AUTH_START;
	params.key_mgmt_suite = WLAN_AKM_SUITE_SAE;
	params.status = WLAN_STATUS_SUCCESS;
	params.ssid.ssid_len = min_t(u32, 32, le32_to_cpu(auth_req->ssid_len));
	memcpy(params.ssid.ssid, auth_req->ssid, params.ssid.ssid_len);
	memcpy(params.bssid, auth_req->peer_mac, ETH_ALEN);

	err = cfg80211_external_auth_request(ifp->ndev, &params, GFP_KERNEL);
	if (err)
		bphy_err(drvr, "Ext Auth request to supplicant failed (%d)\n",
			 err);

	return err;
}

static s32
brcmf_notify_auth_frame_rx(struct brcmf_if *ifp,
			   const struct brcmf_event_msg *e, void *data)
{
	struct brcmf_pub *drvr = ifp->drvr;
	struct brcmf_cfg80211_info *cfg = drvr->config;
	struct wireless_dev *wdev;
	u32 mgmt_frame_len = e->datalen - sizeof(struct brcmf_rx_mgmt_data);
	struct brcmf_rx_mgmt_data *rxframe = (struct brcmf_rx_mgmt_data *)data;
	u8 *frame = (u8 *)(rxframe + 1);
	struct brcmu_chan ch;
	struct ieee80211_mgmt *mgmt_frame;
	s32 freq;

	brcmf_dbg(INFO, "Enter: event %s (%d) received\n",
		  brcmf_fweh_event_name(e->event_code), e->event_code);

	if (e->datalen < sizeof(*rxframe)) {
		bphy_err(drvr, "Event %s (%d) data too small. Ignore\n",
			 brcmf_fweh_event_name(e->event_code), e->event_code);
		return -EINVAL;
	}

	wdev = &ifp->vif->wdev;
	WARN_ON(!wdev);

	ch.chspec = be16_to_cpu(rxframe->chanspec);
	cfg->d11inf.decchspec(&ch);

	mgmt_frame = kzalloc(mgmt_frame_len, GFP_KERNEL);
	if (!mgmt_frame)
		return -ENOMEM;

	mgmt_frame->frame_control = cpu_to_le16(IEEE80211_STYPE_AUTH);
	memcpy(mgmt_frame->da, ifp->mac_addr, ETH_ALEN);
	memcpy(mgmt_frame->sa, e->addr, ETH_ALEN);
	brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_BSSID, mgmt_frame->bssid,
			       ETH_ALEN);
	frame += offsetof(struct ieee80211_mgmt, u);
	memcpy(&mgmt_frame->u, frame,
	       mgmt_frame_len - offsetof(struct ieee80211_mgmt, u));

	freq = ieee80211_channel_to_frequency(ch.control_ch_num,
					      ch.band == BRCMU_CHAN_BAND_2G ?
					      NL80211_BAND_2GHZ :
					      NL80211_BAND_5GHZ);

	cfg80211_rx_mgmt(wdev, freq, 0, (u8 *)mgmt_frame, mgmt_frame_len,
			 NL80211_RXMGMT_FLAG_EXTERNAL_AUTH);
	kfree(mgmt_frame);
	return 0;
}

static s32
brcmf_notify_mgmt_tx_status(struct brcmf_if *ifp,
			    const struct brcmf_event_msg *e, void *data)
{
	struct brcmf_cfg80211_vif *vif = ifp->vif;
	u32 *packet_id = (u32 *)data;

	brcmf_dbg(INFO, "Enter: event %s (%d), status=%d\n",
		  brcmf_fweh_event_name(e->event_code), e->event_code,
		  e->status);

	if (!test_bit(BRCMF_MGMT_TX_SEND_FRAME, &vif->mgmt_tx_status) ||
	    (*packet_id != vif->mgmt_tx_id))
		return 0;

	if (e->event_code == BRCMF_E_MGMT_FRAME_TXSTATUS) {
		if (e->status == BRCMF_E_STATUS_SUCCESS)
			set_bit(BRCMF_MGMT_TX_ACK, &vif->mgmt_tx_status);
		else
			set_bit(BRCMF_MGMT_TX_NOACK, &vif->mgmt_tx_status);
	} else {
		set_bit(BRCMF_MGMT_TX_OFF_CHAN_COMPLETED, &vif->mgmt_tx_status);
	}

	complete(&vif->mgmt_tx);
	return 0;
}

static void brcmf_cyw_register_event_handlers(struct brcmf_pub *drvr)
{
	brcmf_fweh_register(drvr, BRCMF_E_EXT_AUTH_REQ,
			    brcmf_cyw_notify_ext_auth_req);
	brcmf_fweh_register(drvr, BRCMF_E_EXT_AUTH_FRAME_RX,
			    brcmf_notify_auth_frame_rx);
	brcmf_fweh_register(drvr, BRCMF_E_MGMT_FRAME_TXSTATUS,
			    brcmf_notify_mgmt_tx_status);
	brcmf_fweh_register(drvr, BRCMF_E_MGMT_FRAME_OFFCHAN_DONE,
			    brcmf_notify_mgmt_tx_status);
}

const struct brcmf_fwvid_ops brcmf_cyw_ops = {
	.set_sae_password = brcmf_cyw_set_sae_pwd,
	.alloc_fweh_info = brcmf_cyw_alloc_fweh_info,
	.activate_events = brcmf_cyw_activate_events,
	.get_cfg80211_ops = brcmf_cyw_get_cfg80211_ops,
	.register_event_handlers = brcmf_cyw_register_event_handlers,
};
