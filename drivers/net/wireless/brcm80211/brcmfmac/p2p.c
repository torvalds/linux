/*
 * Copyright (c) 2012 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <net/cfg80211.h>

#include <brcmu_wifi.h>
#include <brcmu_utils.h>
#include <defs.h>
#include <dhd.h>
#include <dhd_dbg.h>
#include "fwil.h"
#include "fwil_types.h"
#include "p2p.h"
#include "wl_cfg80211.h"

/* parameters used for p2p escan */
#define P2PAPI_SCAN_NPROBES 1
#define P2PAPI_SCAN_DWELL_TIME_MS 80
#define P2PAPI_SCAN_SOCIAL_DWELL_TIME_MS 40
#define P2PAPI_SCAN_HOME_TIME_MS 60
#define P2PAPI_SCAN_NPROBS_TIME_MS 30
#define P2PAPI_SCAN_AF_SEARCH_DWELL_TIME_MS 100
#define WL_SCAN_CONNECT_DWELL_TIME_MS 200
#define WL_SCAN_JOIN_PROBE_INTERVAL_MS 20

#define BRCMF_P2P_WILDCARD_SSID		"DIRECT-"
#define BRCMF_P2P_WILDCARD_SSID_LEN	(sizeof(BRCMF_P2P_WILDCARD_SSID) - 1)

#define SOCIAL_CHAN_1		1
#define SOCIAL_CHAN_2		6
#define SOCIAL_CHAN_3		11
#define IS_P2P_SOCIAL_CHANNEL(channel) ((channel == SOCIAL_CHAN_1) || \
					 (channel == SOCIAL_CHAN_2) || \
					 (channel == SOCIAL_CHAN_3))
#define BRCMF_P2P_TEMP_CHAN	SOCIAL_CHAN_3
#define SOCIAL_CHAN_CNT		3
#define AF_PEER_SEARCH_CNT	2

#define BRCMF_SCB_TIMEOUT_VALUE	20

#define P2P_VER			9	/* P2P version: 9=WiFi P2P v1.0 */
#define P2P_PUB_AF_CATEGORY	0x04
#define P2P_PUB_AF_ACTION	0x09
#define P2P_AF_CATEGORY		0x7f
#define P2P_OUI			"\x50\x6F\x9A"	/* P2P OUI */
#define P2P_OUI_LEN		3		/* P2P OUI length */

/* Action Frame Constants */
#define DOT11_ACTION_HDR_LEN	2	/* action frame category + action */
#define DOT11_ACTION_CAT_OFF	0	/* category offset */
#define DOT11_ACTION_ACT_OFF	1	/* action offset */

#define P2P_AF_DWELL_TIME		200
#define P2P_AF_MIN_DWELL_TIME		100
#define P2P_AF_MED_DWELL_TIME		400
#define P2P_AF_LONG_DWELL_TIME		1000
#define P2P_AF_TX_MAX_RETRY		1
#define P2P_AF_MAX_WAIT_TIME		2000
#define P2P_INVALID_CHANNEL		-1
#define P2P_CHANNEL_SYNC_RETRY		5
#define P2P_AF_FRM_SCAN_MAX_WAIT	1500
#define P2P_DEFAULT_SLEEP_TIME_VSDB	200

/* WiFi P2P Public Action Frame OUI Subtypes */
#define P2P_PAF_GON_REQ		0	/* Group Owner Negotiation Req */
#define P2P_PAF_GON_RSP		1	/* Group Owner Negotiation Rsp */
#define P2P_PAF_GON_CONF	2	/* Group Owner Negotiation Confirm */
#define P2P_PAF_INVITE_REQ	3	/* P2P Invitation Request */
#define P2P_PAF_INVITE_RSP	4	/* P2P Invitation Response */
#define P2P_PAF_DEVDIS_REQ	5	/* Device Discoverability Request */
#define P2P_PAF_DEVDIS_RSP	6	/* Device Discoverability Response */
#define P2P_PAF_PROVDIS_REQ	7	/* Provision Discovery Request */
#define P2P_PAF_PROVDIS_RSP	8	/* Provision Discovery Response */
#define P2P_PAF_SUBTYPE_INVALID	255	/* Invalid Subtype */

/* WiFi P2P Action Frame OUI Subtypes */
#define P2P_AF_NOTICE_OF_ABSENCE	0	/* Notice of Absence */
#define P2P_AF_PRESENCE_REQ		1	/* P2P Presence Request */
#define P2P_AF_PRESENCE_RSP		2	/* P2P Presence Response */
#define P2P_AF_GO_DISC_REQ		3	/* GO Discoverability Request */

/* P2P Service Discovery related */
#define P2PSD_ACTION_CATEGORY		0x04	/* Public action frame */
#define P2PSD_ACTION_ID_GAS_IREQ	0x0a	/* GAS Initial Request AF */
#define P2PSD_ACTION_ID_GAS_IRESP	0x0b	/* GAS Initial Response AF */
#define P2PSD_ACTION_ID_GAS_CREQ	0x0c	/* GAS Comback Request AF */
#define P2PSD_ACTION_ID_GAS_CRESP	0x0d	/* GAS Comback Response AF */

/**
 * struct brcmf_p2p_disc_st_le - set discovery state in firmware.
 *
 * @state: requested discovery state (see enum brcmf_p2p_disc_state).
 * @chspec: channel parameter for %WL_P2P_DISC_ST_LISTEN state.
 * @dwell: dwell time in ms for %WL_P2P_DISC_ST_LISTEN state.
 */
struct brcmf_p2p_disc_st_le {
	u8 state;
	__le16 chspec;
	__le16 dwell;
};

/**
 * enum brcmf_p2p_disc_state - P2P discovery state values
 *
 * @WL_P2P_DISC_ST_SCAN: P2P discovery with wildcard SSID and P2P IE.
 * @WL_P2P_DISC_ST_LISTEN: P2P discovery off-channel for specified time.
 * @WL_P2P_DISC_ST_SEARCH: P2P discovery with P2P wildcard SSID and P2P IE.
 */
enum brcmf_p2p_disc_state {
	WL_P2P_DISC_ST_SCAN,
	WL_P2P_DISC_ST_LISTEN,
	WL_P2P_DISC_ST_SEARCH
};

/**
 * struct brcmf_p2p_scan_le - P2P specific scan request.
 *
 * @type: type of scan method requested (values: 'E' or 'S').
 * @reserved: reserved (ignored).
 * @eparams: parameters used for type 'E'.
 * @sparams: parameters used for type 'S'.
 */
struct brcmf_p2p_scan_le {
	u8 type;
	u8 reserved[3];
	union {
		struct brcmf_escan_params_le eparams;
		struct brcmf_scan_params_le sparams;
	};
};

/**
 * struct brcmf_p2p_pub_act_frame - WiFi P2P Public Action Frame
 *
 * @category: P2P_PUB_AF_CATEGORY
 * @action: P2P_PUB_AF_ACTION
 * @oui[3]: P2P_OUI
 * @oui_type: OUI type - P2P_VER
 * @subtype: OUI subtype - P2P_TYPE_*
 * @dialog_token: nonzero, identifies req/rsp transaction
 * @elts[1]: Variable length information elements.
 */
struct brcmf_p2p_pub_act_frame {
	u8	category;
	u8	action;
	u8	oui[3];
	u8	oui_type;
	u8	subtype;
	u8	dialog_token;
	u8	elts[1];
};

/**
 * struct brcmf_p2p_action_frame - WiFi P2P Action Frame
 *
 * @category: P2P_AF_CATEGORY
 * @OUI[3]: OUI - P2P_OUI
 * @type: OUI Type - P2P_VER
 * @subtype: OUI Subtype - P2P_AF_*
 * @dialog_token: nonzero, identifies req/resp tranaction
 * @elts[1]: Variable length information elements.
 */
struct brcmf_p2p_action_frame {
	u8	category;
	u8	oui[3];
	u8	type;
	u8	subtype;
	u8	dialog_token;
	u8	elts[1];
};

/**
 * struct brcmf_p2psd_gas_pub_act_frame - Wi-Fi GAS Public Action Frame
 *
 * @category: 0x04 Public Action Frame
 * @action: 0x6c Advertisement Protocol
 * @dialog_token: nonzero, identifies req/rsp transaction
 * @query_data[1]: Query Data. SD gas ireq SD gas iresp
 */
struct brcmf_p2psd_gas_pub_act_frame {
	u8	category;
	u8	action;
	u8	dialog_token;
	u8	query_data[1];
};

/**
 * struct brcmf_config_af_params - Action Frame Parameters for tx.
 *
 * @mpc_onoff: To make sure to send successfully action frame, we have to
 *             turn off mpc  0: off, 1: on,  (-1): do nothing
 * @search_channel: 1: search peer's channel to send af
 * extra_listen: keep the dwell time to get af response frame.
 */
struct brcmf_config_af_params {
	s32 mpc_onoff;
	bool search_channel;
	bool extra_listen;
};

/**
 * brcmf_p2p_is_pub_action() - true if p2p public type frame.
 *
 * @frame: action frame data.
 * @frame_len: length of action frame data.
 *
 * Determine if action frame is p2p public action type
 */
static bool brcmf_p2p_is_pub_action(void *frame, u32 frame_len)
{
	struct brcmf_p2p_pub_act_frame *pact_frm;

	if (frame == NULL)
		return false;

	pact_frm = (struct brcmf_p2p_pub_act_frame *)frame;
	if (frame_len < sizeof(struct brcmf_p2p_pub_act_frame) - 1)
		return false;

	if (pact_frm->category == P2P_PUB_AF_CATEGORY &&
	    pact_frm->action == P2P_PUB_AF_ACTION &&
	    pact_frm->oui_type == P2P_VER &&
	    memcmp(pact_frm->oui, P2P_OUI, P2P_OUI_LEN) == 0)
		return true;

	return false;
}

/**
 * brcmf_p2p_is_p2p_action() - true if p2p action type frame.
 *
 * @frame: action frame data.
 * @frame_len: length of action frame data.
 *
 * Determine if action frame is p2p action type
 */
static bool brcmf_p2p_is_p2p_action(void *frame, u32 frame_len)
{
	struct brcmf_p2p_action_frame *act_frm;

	if (frame == NULL)
		return false;

	act_frm = (struct brcmf_p2p_action_frame *)frame;
	if (frame_len < sizeof(struct brcmf_p2p_action_frame) - 1)
		return false;

	if (act_frm->category == P2P_AF_CATEGORY &&
	    act_frm->type  == P2P_VER &&
	    memcmp(act_frm->oui, P2P_OUI, P2P_OUI_LEN) == 0)
		return true;

	return false;
}

/**
 * brcmf_p2p_is_gas_action() - true if p2p gas action type frame.
 *
 * @frame: action frame data.
 * @frame_len: length of action frame data.
 *
 * Determine if action frame is p2p gas action type
 */
static bool brcmf_p2p_is_gas_action(void *frame, u32 frame_len)
{
	struct brcmf_p2psd_gas_pub_act_frame *sd_act_frm;

	if (frame == NULL)
		return false;

	sd_act_frm = (struct brcmf_p2psd_gas_pub_act_frame *)frame;
	if (frame_len < sizeof(struct brcmf_p2psd_gas_pub_act_frame) - 1)
		return false;

	if (sd_act_frm->category != P2PSD_ACTION_CATEGORY)
		return false;

	if (sd_act_frm->action == P2PSD_ACTION_ID_GAS_IREQ ||
	    sd_act_frm->action == P2PSD_ACTION_ID_GAS_IRESP ||
	    sd_act_frm->action == P2PSD_ACTION_ID_GAS_CREQ ||
	    sd_act_frm->action == P2PSD_ACTION_ID_GAS_CRESP)
		return true;

	return false;
}

/**
 * brcmf_p2p_print_actframe() - debug print routine.
 *
 * @tx: Received or to be transmitted
 * @frame: action frame data.
 * @frame_len: length of action frame data.
 *
 * Print information about the p2p action frame
 */

#ifdef DEBUG

static void brcmf_p2p_print_actframe(bool tx, void *frame, u32 frame_len)
{
	struct brcmf_p2p_pub_act_frame *pact_frm;
	struct brcmf_p2p_action_frame *act_frm;
	struct brcmf_p2psd_gas_pub_act_frame *sd_act_frm;

	if (!frame || frame_len <= 2)
		return;

	if (brcmf_p2p_is_pub_action(frame, frame_len)) {
		pact_frm = (struct brcmf_p2p_pub_act_frame *)frame;
		switch (pact_frm->subtype) {
		case P2P_PAF_GON_REQ:
			brcmf_dbg(TRACE, "%s P2P Group Owner Negotiation Req Frame\n",
				  (tx) ? "TX" : "RX");
			break;
		case P2P_PAF_GON_RSP:
			brcmf_dbg(TRACE, "%s P2P Group Owner Negotiation Rsp Frame\n",
				  (tx) ? "TX" : "RX");
			break;
		case P2P_PAF_GON_CONF:
			brcmf_dbg(TRACE, "%s P2P Group Owner Negotiation Confirm Frame\n",
				  (tx) ? "TX" : "RX");
			break;
		case P2P_PAF_INVITE_REQ:
			brcmf_dbg(TRACE, "%s P2P Invitation Request  Frame\n",
				  (tx) ? "TX" : "RX");
			break;
		case P2P_PAF_INVITE_RSP:
			brcmf_dbg(TRACE, "%s P2P Invitation Response Frame\n",
				  (tx) ? "TX" : "RX");
			break;
		case P2P_PAF_DEVDIS_REQ:
			brcmf_dbg(TRACE, "%s P2P Device Discoverability Request Frame\n",
				  (tx) ? "TX" : "RX");
			break;
		case P2P_PAF_DEVDIS_RSP:
			brcmf_dbg(TRACE, "%s P2P Device Discoverability Response Frame\n",
				  (tx) ? "TX" : "RX");
			break;
		case P2P_PAF_PROVDIS_REQ:
			brcmf_dbg(TRACE, "%s P2P Provision Discovery Request Frame\n",
				  (tx) ? "TX" : "RX");
			break;
		case P2P_PAF_PROVDIS_RSP:
			brcmf_dbg(TRACE, "%s P2P Provision Discovery Response Frame\n",
				  (tx) ? "TX" : "RX");
			break;
		default:
			brcmf_dbg(TRACE, "%s Unknown P2P Public Action Frame\n",
				  (tx) ? "TX" : "RX");
			break;
		}
	} else if (brcmf_p2p_is_p2p_action(frame, frame_len)) {
		act_frm = (struct brcmf_p2p_action_frame *)frame;
		switch (act_frm->subtype) {
		case P2P_AF_NOTICE_OF_ABSENCE:
			brcmf_dbg(TRACE, "%s P2P Notice of Absence Frame\n",
				  (tx) ? "TX" : "RX");
			break;
		case P2P_AF_PRESENCE_REQ:
			brcmf_dbg(TRACE, "%s P2P Presence Request Frame\n",
				  (tx) ? "TX" : "RX");
			break;
		case P2P_AF_PRESENCE_RSP:
			brcmf_dbg(TRACE, "%s P2P Presence Response Frame\n",
				  (tx) ? "TX" : "RX");
			break;
		case P2P_AF_GO_DISC_REQ:
			brcmf_dbg(TRACE, "%s P2P Discoverability Request Frame\n",
				  (tx) ? "TX" : "RX");
			break;
		default:
			brcmf_dbg(TRACE, "%s Unknown P2P Action Frame\n",
				  (tx) ? "TX" : "RX");
		}

	} else if (brcmf_p2p_is_gas_action(frame, frame_len)) {
		sd_act_frm = (struct brcmf_p2psd_gas_pub_act_frame *)frame;
		switch (sd_act_frm->action) {
		case P2PSD_ACTION_ID_GAS_IREQ:
			brcmf_dbg(TRACE, "%s P2P GAS Initial Request\n",
				  (tx) ? "TX" : "RX");
			break;
		case P2PSD_ACTION_ID_GAS_IRESP:
			brcmf_dbg(TRACE, "%s P2P GAS Initial Response\n",
				  (tx) ? "TX" : "RX");
			break;
		case P2PSD_ACTION_ID_GAS_CREQ:
			brcmf_dbg(TRACE, "%s P2P GAS Comback Request\n",
				  (tx) ? "TX" : "RX");
			break;
		case P2PSD_ACTION_ID_GAS_CRESP:
			brcmf_dbg(TRACE, "%s P2P GAS Comback Response\n",
				  (tx) ? "TX" : "RX");
			break;
		default:
			brcmf_dbg(TRACE, "%s Unknown P2P GAS Frame\n",
				  (tx) ? "TX" : "RX");
			break;
		}
	}
}

#else

static void brcmf_p2p_print_actframe(bool tx, void *frame, u32 frame_len)
{
}

#endif


/**
 * brcmf_p2p_set_firmware() - prepare firmware for peer-to-peer operation.
 *
 * @ifp: ifp to use for iovars (primary).
 * @p2p_mac: mac address to configure for p2p_da_override
 */
static int brcmf_p2p_set_firmware(struct brcmf_if *ifp, u8 *p2p_mac)
{
	s32 ret = 0;

	brcmf_fil_cmd_int_set(ifp, BRCMF_C_DOWN, 1);
	brcmf_fil_iovar_int_set(ifp, "apsta", 1);
	brcmf_fil_cmd_int_set(ifp, BRCMF_C_UP, 1);

	/* In case of COB type, firmware has default mac address
	 * After Initializing firmware, we have to set current mac address to
	 * firmware for P2P device address
	 */
	ret = brcmf_fil_iovar_data_set(ifp, "p2p_da_override", p2p_mac,
				       ETH_ALEN);
	if (ret)
		brcmf_err("failed to update device address ret %d\n", ret);

	return ret;
}

/**
 * brcmf_p2p_generate_bss_mac() - derive mac addresses for P2P.
 *
 * @p2p: P2P specific data.
 * @dev_addr: optional device address.
 *
 * P2P needs mac addresses for P2P device and interface. If no device
 * address it specified, these are derived from the primary net device, ie.
 * the permanent ethernet address of the device.
 */
static void brcmf_p2p_generate_bss_mac(struct brcmf_p2p_info *p2p, u8 *dev_addr)
{
	struct brcmf_if *pri_ifp = p2p->bss_idx[P2PAPI_BSSCFG_PRIMARY].vif->ifp;
	bool local_admin = false;

	if (!dev_addr || is_zero_ether_addr(dev_addr)) {
		dev_addr = pri_ifp->mac_addr;
		local_admin = true;
	}

	/* Generate the P2P Device Address.  This consists of the device's
	 * primary MAC address with the locally administered bit set.
	 */
	memcpy(p2p->dev_addr, dev_addr, ETH_ALEN);
	if (local_admin)
		p2p->dev_addr[0] |= 0x02;

	/* Generate the P2P Interface Address.  If the discovery and connection
	 * BSSCFGs need to simultaneously co-exist, then this address must be
	 * different from the P2P Device Address, but also locally administered.
	 */
	memcpy(p2p->int_addr, p2p->dev_addr, ETH_ALEN);
	p2p->int_addr[0] |= 0x02;
	p2p->int_addr[4] ^= 0x80;
}

/**
 * brcmf_p2p_scan_is_p2p_request() - is cfg80211 scan request a P2P scan.
 *
 * @request: the scan request as received from cfg80211.
 *
 * returns true if one of the ssids in the request matches the
 * P2P wildcard ssid; otherwise returns false.
 */
static bool brcmf_p2p_scan_is_p2p_request(struct cfg80211_scan_request *request)
{
	struct cfg80211_ssid *ssids = request->ssids;
	int i;

	for (i = 0; i < request->n_ssids; i++) {
		if (ssids[i].ssid_len != BRCMF_P2P_WILDCARD_SSID_LEN)
			continue;

		brcmf_dbg(INFO, "comparing ssid \"%s\"", ssids[i].ssid);
		if (!memcmp(BRCMF_P2P_WILDCARD_SSID, ssids[i].ssid,
			    BRCMF_P2P_WILDCARD_SSID_LEN))
			return true;
	}
	return false;
}

/**
 * brcmf_p2p_set_discover_state - set discover state in firmware.
 *
 * @ifp: low-level interface object.
 * @state: discover state to set.
 * @chanspec: channel parameters (for state @WL_P2P_DISC_ST_LISTEN only).
 * @listen_ms: duration to listen (for state @WL_P2P_DISC_ST_LISTEN only).
 */
static s32 brcmf_p2p_set_discover_state(struct brcmf_if *ifp, u8 state,
					u16 chanspec, u16 listen_ms)
{
	struct brcmf_p2p_disc_st_le discover_state;
	s32 ret = 0;
	brcmf_dbg(TRACE, "enter\n");

	discover_state.state = state;
	discover_state.chspec = cpu_to_le16(chanspec);
	discover_state.dwell = cpu_to_le16(listen_ms);
	ret = brcmf_fil_bsscfg_data_set(ifp, "p2p_state", &discover_state,
					sizeof(discover_state));
	return ret;
}

/**
 * brcmf_p2p_deinit_discovery() - disable P2P device discovery.
 *
 * @p2p: P2P specific data.
 *
 * Resets the discovery state and disables it in firmware.
 */
static s32 brcmf_p2p_deinit_discovery(struct brcmf_p2p_info *p2p)
{
	struct brcmf_cfg80211_vif *vif;

	brcmf_dbg(TRACE, "enter\n");

	/* Set the discovery state to SCAN */
	vif = p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif;
	(void)brcmf_p2p_set_discover_state(vif->ifp, WL_P2P_DISC_ST_SCAN, 0, 0);

	/* Disable P2P discovery in the firmware */
	vif = p2p->bss_idx[P2PAPI_BSSCFG_PRIMARY].vif;
	(void)brcmf_fil_iovar_int_set(vif->ifp, "p2p_disc", 0);

	return 0;
}

/**
 * brcmf_p2p_enable_discovery() - initialize and configure discovery.
 *
 * @p2p: P2P specific data.
 *
 * Initializes the discovery device and configure the virtual interface.
 */
static int brcmf_p2p_enable_discovery(struct brcmf_p2p_info *p2p)
{
	struct brcmf_cfg80211_vif *vif;
	s32 ret = 0;

	brcmf_dbg(TRACE, "enter\n");
	vif = p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif;
	if (!vif) {
		brcmf_err("P2P config device not available\n");
		ret = -EPERM;
		goto exit;
	}

	if (test_bit(BRCMF_P2P_STATUS_ENABLED, &p2p->status)) {
		brcmf_dbg(INFO, "P2P config device already configured\n");
		goto exit;
	}

	/* Re-initialize P2P Discovery in the firmware */
	vif = p2p->bss_idx[P2PAPI_BSSCFG_PRIMARY].vif;
	ret = brcmf_fil_iovar_int_set(vif->ifp, "p2p_disc", 1);
	if (ret < 0) {
		brcmf_err("set p2p_disc error\n");
		goto exit;
	}
	vif = p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif;
	ret = brcmf_p2p_set_discover_state(vif->ifp, WL_P2P_DISC_ST_SCAN, 0, 0);
	if (ret < 0) {
		brcmf_err("unable to set WL_P2P_DISC_ST_SCAN\n");
		goto exit;
	}

	/*
	 * Set wsec to any non-zero value in the discovery bsscfg
	 * to ensure our P2P probe responses have the privacy bit
	 * set in the 802.11 WPA IE. Some peer devices may not
	 * initiate WPS with us if this bit is not set.
	 */
	ret = brcmf_fil_bsscfg_int_set(vif->ifp, "wsec", AES_ENABLED);
	if (ret < 0) {
		brcmf_err("wsec error %d\n", ret);
		goto exit;
	}

	set_bit(BRCMF_P2P_STATUS_ENABLED, &p2p->status);
exit:
	return ret;
}

/**
 * brcmf_p2p_escan() - initiate a P2P scan.
 *
 * @p2p: P2P specific data.
 * @num_chans: number of channels to scan.
 * @chanspecs: channel parameters for @num_chans channels.
 * @search_state: P2P discover state to use.
 * @action: scan action to pass to firmware.
 * @bss_type: type of P2P bss.
 */
static s32 brcmf_p2p_escan(struct brcmf_p2p_info *p2p, u32 num_chans,
			   u16 chanspecs[], s32 search_state, u16 action,
			   enum p2p_bss_type bss_type)
{
	s32 ret = 0;
	s32 memsize = offsetof(struct brcmf_p2p_scan_le,
			       eparams.params_le.channel_list);
	s32 nprobes;
	s32 active;
	u32 i;
	u8 *memblk;
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_p2p_scan_le *p2p_params;
	struct brcmf_scan_params_le *sparams;
	struct brcmf_ssid ssid;

	memsize += num_chans * sizeof(__le16);
	memblk = kzalloc(memsize, GFP_KERNEL);
	if (!memblk)
		return -ENOMEM;

	vif = p2p->bss_idx[bss_type].vif;
	if (vif == NULL) {
		brcmf_err("no vif for bss type %d\n", bss_type);
		ret = -EINVAL;
		goto exit;
	}

	switch (search_state) {
	case WL_P2P_DISC_ST_SEARCH:
		/*
		 * If we in SEARCH STATE, we don't need to set SSID explictly
		 * because dongle use P2P WILDCARD internally by default
		 */
		/* use null ssid */
		ssid.SSID_len = 0;
		memset(ssid.SSID, 0, sizeof(ssid.SSID));
		break;
	case WL_P2P_DISC_ST_SCAN:
		/*
		 * wpa_supplicant has p2p_find command with type social or
		 * progressive. For progressive, we need to set the ssid to
		 * P2P WILDCARD because we just do broadcast scan unless
		 * setting SSID.
		 */
		ssid.SSID_len = BRCMF_P2P_WILDCARD_SSID_LEN;
		memcpy(ssid.SSID, BRCMF_P2P_WILDCARD_SSID, ssid.SSID_len);
		break;
	default:
		brcmf_err(" invalid search state %d\n", search_state);
		ret = -EINVAL;
		goto exit;
	}

	brcmf_p2p_set_discover_state(vif->ifp, search_state, 0, 0);

	/*
	 * set p2p scan parameters.
	 */
	p2p_params = (struct brcmf_p2p_scan_le *)memblk;
	p2p_params->type = 'E';

	/* determine the scan engine parameters */
	sparams = &p2p_params->eparams.params_le;
	sparams->bss_type = DOT11_BSSTYPE_ANY;
	if (p2p->cfg->active_scan)
		sparams->scan_type = 0;
	else
		sparams->scan_type = 1;

	memset(&sparams->bssid, 0xFF, ETH_ALEN);
	if (ssid.SSID_len)
		memcpy(sparams->ssid_le.SSID, ssid.SSID, ssid.SSID_len);
	sparams->ssid_le.SSID_len = cpu_to_le32(ssid.SSID_len);
	sparams->home_time = cpu_to_le32(P2PAPI_SCAN_HOME_TIME_MS);

	/*
	 * SOCIAL_CHAN_CNT + 1 takes care of the Progressive scan
	 * supported by the supplicant.
	 */
	if (num_chans == SOCIAL_CHAN_CNT || num_chans == (SOCIAL_CHAN_CNT + 1))
		active = P2PAPI_SCAN_SOCIAL_DWELL_TIME_MS;
	else if (num_chans == AF_PEER_SEARCH_CNT)
		active = P2PAPI_SCAN_AF_SEARCH_DWELL_TIME_MS;
	else if (wl_get_vif_state_all(p2p->cfg, BRCMF_VIF_STATUS_CONNECTED))
		active = -1;
	else
		active = P2PAPI_SCAN_DWELL_TIME_MS;

	/* Override scan params to find a peer for a connection */
	if (num_chans == 1) {
		active = WL_SCAN_CONNECT_DWELL_TIME_MS;
		/* WAR to sync with presence period of VSDB GO.
		 * send probe request more frequently
		 */
		nprobes = active / WL_SCAN_JOIN_PROBE_INTERVAL_MS;
	} else {
		nprobes = active / P2PAPI_SCAN_NPROBS_TIME_MS;
	}

	if (nprobes <= 0)
		nprobes = 1;

	brcmf_dbg(INFO, "nprobes # %d, active_time %d\n", nprobes, active);
	sparams->active_time = cpu_to_le32(active);
	sparams->nprobes = cpu_to_le32(nprobes);
	sparams->passive_time = cpu_to_le32(-1);
	sparams->channel_num = cpu_to_le32(num_chans &
					   BRCMF_SCAN_PARAMS_COUNT_MASK);
	for (i = 0; i < num_chans; i++)
		sparams->channel_list[i] = cpu_to_le16(chanspecs[i]);

	/* set the escan specific parameters */
	p2p_params->eparams.version = cpu_to_le32(BRCMF_ESCAN_REQ_VERSION);
	p2p_params->eparams.action =  cpu_to_le16(action);
	p2p_params->eparams.sync_id = cpu_to_le16(0x1234);
	/* perform p2p scan on primary device */
	ret = brcmf_fil_bsscfg_data_set(vif->ifp, "p2p_scan", memblk, memsize);
	if (!ret)
		set_bit(BRCMF_SCAN_STATUS_BUSY, &p2p->cfg->scan_status);
exit:
	kfree(memblk);
	return ret;
}

/**
 * brcmf_p2p_run_escan() - escan callback for peer-to-peer.
 *
 * @cfg: driver private data for cfg80211 interface.
 * @ndev: net device for which scan is requested.
 * @request: scan request from cfg80211.
 * @action: scan action.
 *
 * Determines the P2P discovery state based to scan request parameters and
 * validates the channels in the request.
 */
static s32 brcmf_p2p_run_escan(struct brcmf_cfg80211_info *cfg,
			       struct brcmf_if *ifp,
			       struct cfg80211_scan_request *request,
			       u16 action)
{
	struct brcmf_p2p_info *p2p = &cfg->p2p;
	s32 err = 0;
	s32 search_state = WL_P2P_DISC_ST_SCAN;
	struct brcmf_cfg80211_vif *vif;
	struct net_device *dev = NULL;
	int i, num_nodfs = 0;
	u16 *chanspecs;

	brcmf_dbg(TRACE, "enter\n");

	if (!request) {
		err = -EINVAL;
		goto exit;
	}

	if (request->n_channels) {
		chanspecs = kcalloc(request->n_channels, sizeof(*chanspecs),
				    GFP_KERNEL);
		if (!chanspecs) {
			err = -ENOMEM;
			goto exit;
		}
		vif = p2p->bss_idx[P2PAPI_BSSCFG_CONNECTION].vif;
		if (vif)
			dev = vif->wdev.netdev;
		if (request->n_channels == 3 &&
		    request->channels[0]->hw_value == SOCIAL_CHAN_1 &&
		    request->channels[1]->hw_value == SOCIAL_CHAN_2 &&
		    request->channels[2]->hw_value == SOCIAL_CHAN_3) {
			/* SOCIAL CHANNELS 1, 6, 11 */
			search_state = WL_P2P_DISC_ST_SEARCH;
			brcmf_dbg(INFO, "P2P SEARCH PHASE START\n");
		} else if (dev != NULL && vif->mode == WL_MODE_AP) {
			/* If you are already a GO, then do SEARCH only */
			brcmf_dbg(INFO, "Already a GO. Do SEARCH Only\n");
			search_state = WL_P2P_DISC_ST_SEARCH;
		} else {
			brcmf_dbg(INFO, "P2P SCAN STATE START\n");
		}

		/*
		 * no P2P scanning on passive or DFS channels.
		 */
		for (i = 0; i < request->n_channels; i++) {
			struct ieee80211_channel *chan = request->channels[i];

			if (chan->flags & (IEEE80211_CHAN_RADAR |
					   IEEE80211_CHAN_PASSIVE_SCAN))
				continue;

			chanspecs[i] = channel_to_chanspec(&p2p->cfg->d11inf,
							   chan);
			brcmf_dbg(INFO, "%d: chan=%d, channel spec=%x\n",
				  num_nodfs, chan->hw_value, chanspecs[i]);
			num_nodfs++;
		}
		err = brcmf_p2p_escan(p2p, num_nodfs, chanspecs, search_state,
				      action, P2PAPI_BSSCFG_DEVICE);
	}
exit:
	if (err)
		brcmf_err("error (%d)\n", err);
	return err;
}


/**
 * brcmf_p2p_find_listen_channel() - find listen channel in ie string.
 *
 * @ie: string of information elements.
 * @ie_len: length of string.
 *
 * Scan ie for p2p ie and look for attribute 6 channel. If available determine
 * channel and return it.
 */
static s32 brcmf_p2p_find_listen_channel(const u8 *ie, u32 ie_len)
{
	u8 channel_ie[5];
	s32 listen_channel;
	s32 err;

	err = cfg80211_get_p2p_attr(ie, ie_len,
				    IEEE80211_P2P_ATTR_LISTEN_CHANNEL,
				    channel_ie, sizeof(channel_ie));
	if (err < 0)
		return err;

	/* listen channel subel length format:     */
	/* 3(country) + 1(op. class) + 1(chan num) */
	listen_channel = (s32)channel_ie[3 + 1];

	if (listen_channel == SOCIAL_CHAN_1 ||
	    listen_channel == SOCIAL_CHAN_2 ||
	    listen_channel == SOCIAL_CHAN_3) {
		brcmf_dbg(INFO, "Found my Listen Channel %d\n", listen_channel);
		return listen_channel;
	}

	return -EPERM;
}


/**
 * brcmf_p2p_scan_prep() - prepare scan based on request.
 *
 * @wiphy: wiphy device.
 * @request: scan request from cfg80211.
 * @vif: vif on which scan request is to be executed.
 *
 * Prepare the scan appropriately for type of scan requested. Overrides the
 * escan .run() callback for peer-to-peer scanning.
 */
int brcmf_p2p_scan_prep(struct wiphy *wiphy,
			struct cfg80211_scan_request *request,
			struct brcmf_cfg80211_vif *vif)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_p2p_info *p2p = &cfg->p2p;
	int err = 0;

	if (brcmf_p2p_scan_is_p2p_request(request)) {
		/* find my listen channel */
		err = brcmf_p2p_find_listen_channel(request->ie,
						    request->ie_len);
		if (err < 0)
			return err;

		p2p->afx_hdl.my_listen_chan = err;

		clear_bit(BRCMF_P2P_STATUS_GO_NEG_PHASE, &p2p->status);
		brcmf_dbg(INFO, "P2P: GO_NEG_PHASE status cleared\n");

		err = brcmf_p2p_enable_discovery(p2p);
		if (err)
			return err;

		vif = p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif;

		/* override .run_escan() callback. */
		cfg->escan_info.run = brcmf_p2p_run_escan;
	}
	err = brcmf_vif_set_mgmt_ie(vif, BRCMF_VNDR_IE_PRBREQ_FLAG,
				    request->ie, request->ie_len);
	return err;
}


/**
 * brcmf_p2p_discover_listen() - set firmware to discover listen state.
 *
 * @p2p: p2p device.
 * @channel: channel nr for discover listen.
 * @duration: time in ms to stay on channel.
 *
 */
static s32
brcmf_p2p_discover_listen(struct brcmf_p2p_info *p2p, u16 channel, u32 duration)
{
	struct brcmf_cfg80211_vif *vif;
	struct brcmu_chan ch;
	s32 err = 0;

	vif = p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif;
	if (!vif) {
		brcmf_err("Discovery is not set, so we have nothing to do\n");
		err = -EPERM;
		goto exit;
	}

	if (test_bit(BRCMF_P2P_STATUS_DISCOVER_LISTEN, &p2p->status)) {
		brcmf_err("Previous LISTEN is not completed yet\n");
		/* WAR: prevent cookie mismatch in wpa_supplicant return OK */
		goto exit;
	}

	ch.chnum = channel;
	ch.bw = BRCMU_CHAN_BW_20;
	p2p->cfg->d11inf.encchspec(&ch);
	err = brcmf_p2p_set_discover_state(vif->ifp, WL_P2P_DISC_ST_LISTEN,
					   ch.chspec, (u16)duration);
	if (!err) {
		set_bit(BRCMF_P2P_STATUS_DISCOVER_LISTEN, &p2p->status);
		p2p->remain_on_channel_cookie++;
	}
exit:
	return err;
}


/**
 * brcmf_p2p_remain_on_channel() - put device on channel and stay there.
 *
 * @wiphy: wiphy device.
 * @channel: channel to stay on.
 * @duration: time in ms to remain on channel.
 *
 */
int brcmf_p2p_remain_on_channel(struct wiphy *wiphy, struct wireless_dev *wdev,
				struct ieee80211_channel *channel,
				unsigned int duration, u64 *cookie)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_p2p_info *p2p = &cfg->p2p;
	s32 err;
	u16 channel_nr;

	channel_nr = ieee80211_frequency_to_channel(channel->center_freq);
	brcmf_dbg(TRACE, "Enter, channel: %d, duration ms (%d)\n", channel_nr,
		  duration);

	err = brcmf_p2p_enable_discovery(p2p);
	if (err)
		goto exit;
	err = brcmf_p2p_discover_listen(p2p, channel_nr, duration);
	if (err)
		goto exit;

	memcpy(&p2p->remain_on_channel, channel, sizeof(*channel));
	*cookie = p2p->remain_on_channel_cookie;
	cfg80211_ready_on_channel(wdev, *cookie, channel, duration, GFP_KERNEL);

exit:
	return err;
}


/**
 * brcmf_p2p_notify_listen_complete() - p2p listen has completed.
 *
 * @ifp: interfac control.
 * @e: event message. Not used, to make it usable for fweh event dispatcher.
 * @data: payload of message. Not used.
 *
 */
int brcmf_p2p_notify_listen_complete(struct brcmf_if *ifp,
				     const struct brcmf_event_msg *e,
				     void *data)
{
	struct brcmf_cfg80211_info *cfg = ifp->drvr->config;
	struct brcmf_p2p_info *p2p = &cfg->p2p;

	brcmf_dbg(TRACE, "Enter\n");
	if (test_and_clear_bit(BRCMF_P2P_STATUS_DISCOVER_LISTEN,
			       &p2p->status)) {
		if (test_and_clear_bit(BRCMF_P2P_STATUS_WAITING_NEXT_AF_LISTEN,
				       &p2p->status)) {
			clear_bit(BRCMF_P2P_STATUS_WAITING_NEXT_ACT_FRAME,
				  &p2p->status);
			brcmf_dbg(INFO, "Listen DONE, wake up wait_next_af\n");
			complete(&p2p->wait_next_af);
		}

		cfg80211_remain_on_channel_expired(&ifp->vif->wdev,
						   p2p->remain_on_channel_cookie,
						   &p2p->remain_on_channel,
						   GFP_KERNEL);
	}
	return 0;
}


/**
 * brcmf_p2p_cancel_remain_on_channel() - cancel p2p listen state.
 *
 * @ifp: interfac control.
 *
 */
void brcmf_p2p_cancel_remain_on_channel(struct brcmf_if *ifp)
{
	if (!ifp)
		return;
	brcmf_p2p_set_discover_state(ifp, WL_P2P_DISC_ST_SCAN, 0, 0);
	brcmf_p2p_notify_listen_complete(ifp, NULL, NULL);
}


/**
 * brcmf_p2p_act_frm_search() - search function for action frame.
 *
 * @p2p: p2p device.
 * channel: channel on which action frame is to be trasmitted.
 *
 * search function to reach at common channel to send action frame. When
 * channel is 0 then all social channels will be used to send af
 */
static s32 brcmf_p2p_act_frm_search(struct brcmf_p2p_info *p2p, u16 channel)
{
	s32 err;
	u32 channel_cnt;
	u16 *default_chan_list;
	u32 i;
	struct brcmu_chan ch;

	brcmf_dbg(TRACE, "Enter\n");

	if (channel)
		channel_cnt = AF_PEER_SEARCH_CNT;
	else
		channel_cnt = SOCIAL_CHAN_CNT;
	default_chan_list = kzalloc(channel_cnt * sizeof(*default_chan_list),
				    GFP_KERNEL);
	if (default_chan_list == NULL) {
		brcmf_err("channel list allocation failed\n");
		err = -ENOMEM;
		goto exit;
	}
	ch.bw = BRCMU_CHAN_BW_20;
	if (channel) {
		ch.chnum = channel;
		p2p->cfg->d11inf.encchspec(&ch);
		/* insert same channel to the chan_list */
		for (i = 0; i < channel_cnt; i++)
			default_chan_list[i] = ch.chspec;
	} else {
		ch.chnum = SOCIAL_CHAN_1;
		p2p->cfg->d11inf.encchspec(&ch);
		default_chan_list[0] = ch.chspec;
		ch.chnum = SOCIAL_CHAN_2;
		p2p->cfg->d11inf.encchspec(&ch);
		default_chan_list[1] = ch.chspec;
		ch.chnum = SOCIAL_CHAN_3;
		p2p->cfg->d11inf.encchspec(&ch);
		default_chan_list[2] = ch.chspec;
	}
	err = brcmf_p2p_escan(p2p, channel_cnt, default_chan_list,
			      WL_P2P_DISC_ST_SEARCH, WL_ESCAN_ACTION_START,
			      P2PAPI_BSSCFG_DEVICE);
	kfree(default_chan_list);
exit:
	return err;
}


/**
 * brcmf_p2p_afx_handler() - afx worker thread.
 *
 * @work:
 *
 */
static void brcmf_p2p_afx_handler(struct work_struct *work)
{
	struct afx_hdl *afx_hdl = container_of(work, struct afx_hdl, afx_work);
	struct brcmf_p2p_info *p2p = container_of(afx_hdl,
						  struct brcmf_p2p_info,
						  afx_hdl);
	s32 err;

	if (!afx_hdl->is_active)
		return;

	if (afx_hdl->is_listen && afx_hdl->my_listen_chan)
		/* 100ms ~ 300ms */
		err = brcmf_p2p_discover_listen(p2p, afx_hdl->my_listen_chan,
						100 * (1 + prandom_u32() % 3));
	else
		err = brcmf_p2p_act_frm_search(p2p, afx_hdl->peer_listen_chan);

	if (err) {
		brcmf_err("ERROR occurred! value is (%d)\n", err);
		if (test_bit(BRCMF_P2P_STATUS_FINDING_COMMON_CHANNEL,
			     &p2p->status))
			complete(&afx_hdl->act_frm_scan);
	}
}


/**
 * brcmf_p2p_af_searching_channel() - search channel.
 *
 * @p2p: p2p device info struct.
 *
 */
static s32 brcmf_p2p_af_searching_channel(struct brcmf_p2p_info *p2p)
{
	struct afx_hdl *afx_hdl = &p2p->afx_hdl;
	struct brcmf_cfg80211_vif *pri_vif;
	unsigned long duration;
	s32 retry;

	brcmf_dbg(TRACE, "Enter\n");

	pri_vif = p2p->bss_idx[P2PAPI_BSSCFG_PRIMARY].vif;

	INIT_COMPLETION(afx_hdl->act_frm_scan);
	set_bit(BRCMF_P2P_STATUS_FINDING_COMMON_CHANNEL, &p2p->status);
	afx_hdl->is_active = true;
	afx_hdl->peer_chan = P2P_INVALID_CHANNEL;

	/* Loop to wait until we find a peer's channel or the
	 * pending action frame tx is cancelled.
	 */
	retry = 0;
	duration = msecs_to_jiffies(P2P_AF_FRM_SCAN_MAX_WAIT);
	while ((retry < P2P_CHANNEL_SYNC_RETRY) &&
	       (afx_hdl->peer_chan == P2P_INVALID_CHANNEL)) {
		afx_hdl->is_listen = false;
		brcmf_dbg(TRACE, "Scheduling action frame for sending.. (%d)\n",
			  retry);
		/* search peer on peer's listen channel */
		schedule_work(&afx_hdl->afx_work);
		wait_for_completion_timeout(&afx_hdl->act_frm_scan, duration);
		if ((afx_hdl->peer_chan != P2P_INVALID_CHANNEL) ||
		    (!test_bit(BRCMF_P2P_STATUS_FINDING_COMMON_CHANNEL,
			       &p2p->status)))
			break;

		if (afx_hdl->my_listen_chan) {
			brcmf_dbg(TRACE, "Scheduling listen peer, channel=%d\n",
				  afx_hdl->my_listen_chan);
			/* listen on my listen channel */
			afx_hdl->is_listen = true;
			schedule_work(&afx_hdl->afx_work);
			wait_for_completion_timeout(&afx_hdl->act_frm_scan,
						    duration);
		}
		if ((afx_hdl->peer_chan != P2P_INVALID_CHANNEL) ||
		    (!test_bit(BRCMF_P2P_STATUS_FINDING_COMMON_CHANNEL,
			       &p2p->status)))
			break;
		retry++;

		/* if sta is connected or connecting, sleep for a while before
		 * retry af tx or finding a peer
		 */
		if (test_bit(BRCMF_VIF_STATUS_CONNECTED, &pri_vif->sme_state) ||
		    test_bit(BRCMF_VIF_STATUS_CONNECTING, &pri_vif->sme_state))
			msleep(P2P_DEFAULT_SLEEP_TIME_VSDB);
	}

	brcmf_dbg(TRACE, "Completed search/listen peer_chan=%d\n",
		  afx_hdl->peer_chan);
	afx_hdl->is_active = false;

	clear_bit(BRCMF_P2P_STATUS_FINDING_COMMON_CHANNEL, &p2p->status);

	return afx_hdl->peer_chan;
}


/**
 * brcmf_p2p_scan_finding_common_channel() - was escan used for finding channel
 *
 * @cfg: common configuration struct.
 * @bi: bss info struct, result from scan.
 *
 */
bool brcmf_p2p_scan_finding_common_channel(struct brcmf_cfg80211_info *cfg,
					   struct brcmf_bss_info_le *bi)

{
	struct brcmf_p2p_info *p2p = &cfg->p2p;
	struct afx_hdl *afx_hdl = &p2p->afx_hdl;
	struct brcmu_chan ch;
	u8 *ie;
	s32 err;
	u8 p2p_dev_addr[ETH_ALEN];

	if (!test_bit(BRCMF_P2P_STATUS_FINDING_COMMON_CHANNEL, &p2p->status))
		return false;

	if (bi == NULL) {
		brcmf_dbg(TRACE, "ACTION FRAME SCAN Done\n");
		if (afx_hdl->peer_chan == P2P_INVALID_CHANNEL)
			complete(&afx_hdl->act_frm_scan);
		return true;
	}

	ie = ((u8 *)bi) + le16_to_cpu(bi->ie_offset);
	memset(p2p_dev_addr, 0, sizeof(p2p_dev_addr));
	err = cfg80211_get_p2p_attr(ie, le32_to_cpu(bi->ie_length),
				    IEEE80211_P2P_ATTR_DEVICE_INFO,
				    p2p_dev_addr, sizeof(p2p_dev_addr));
	if (err < 0)
		err = cfg80211_get_p2p_attr(ie, le32_to_cpu(bi->ie_length),
					    IEEE80211_P2P_ATTR_DEVICE_ID,
					    p2p_dev_addr, sizeof(p2p_dev_addr));
	if ((err >= 0) &&
	    (!memcmp(p2p_dev_addr, afx_hdl->tx_dst_addr, ETH_ALEN))) {
		if (!bi->ctl_ch) {
			ch.chspec = le16_to_cpu(bi->chanspec);
			cfg->d11inf.decchspec(&ch);
			bi->ctl_ch = ch.chnum;
		}
		afx_hdl->peer_chan = bi->ctl_ch;
		brcmf_dbg(TRACE, "ACTION FRAME SCAN : Peer %pM found, channel : %d\n",
			  afx_hdl->tx_dst_addr, afx_hdl->peer_chan);
		complete(&afx_hdl->act_frm_scan);
	}
	return true;
}

/**
 * brcmf_p2p_stop_wait_next_action_frame() - finish scan if af tx complete.
 *
 * @cfg: common configuration struct.
 *
 */
static void
brcmf_p2p_stop_wait_next_action_frame(struct brcmf_cfg80211_info *cfg)
{
	struct brcmf_p2p_info *p2p = &cfg->p2p;
	struct brcmf_if *ifp = cfg->escan_info.ifp;

	if (test_bit(BRCMF_P2P_STATUS_SENDING_ACT_FRAME, &p2p->status) &&
	    (test_bit(BRCMF_P2P_STATUS_ACTION_TX_COMPLETED, &p2p->status) ||
	     test_bit(BRCMF_P2P_STATUS_ACTION_TX_NOACK, &p2p->status))) {
		brcmf_dbg(TRACE, "*** Wake UP ** abort actframe iovar\n");
		/* if channel is not zero, "actfame" uses off channel scan.
		 * So abort scan for off channel completion.
		 */
		if (p2p->af_sent_channel)
			brcmf_notify_escan_complete(cfg, ifp, true, true);
	} else if (test_bit(BRCMF_P2P_STATUS_WAITING_NEXT_AF_LISTEN,
			    &p2p->status)) {
		brcmf_dbg(TRACE, "*** Wake UP ** abort listen for next af frame\n");
		/* So abort scan to cancel listen */
		brcmf_notify_escan_complete(cfg, ifp, true, true);
	}
}


/**
 * brcmf_p2p_gon_req_collision() - Check if go negotiaton collission
 *
 * @p2p: p2p device info struct.
 *
 * return true if recevied action frame is to be dropped.
 */
static bool
brcmf_p2p_gon_req_collision(struct brcmf_p2p_info *p2p, u8 *mac)
{
	struct brcmf_cfg80211_info *cfg = p2p->cfg;
	struct brcmf_if *ifp;

	brcmf_dbg(TRACE, "Enter\n");

	if (!test_bit(BRCMF_P2P_STATUS_WAITING_NEXT_ACT_FRAME, &p2p->status) ||
	    !p2p->gon_req_action)
		return false;

	brcmf_dbg(TRACE, "GO Negotiation Request COLLISION !!!\n");
	/* if sa(peer) addr is less than da(my) addr, then this device
	 * process peer's gon request and block to send gon req.
	 * if not (sa addr > da addr),
	 * this device will process gon request and drop gon req of peer.
	 */
	ifp = p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif->ifp;
	if (memcmp(mac, ifp->mac_addr, ETH_ALEN) < 0) {
		brcmf_dbg(INFO, "Block transmit gon req !!!\n");
		p2p->block_gon_req_tx = true;
		/* if we are finding a common channel for sending af,
		 * do not scan more to block to send current gon req
		 */
		if (test_and_clear_bit(BRCMF_P2P_STATUS_FINDING_COMMON_CHANNEL,
				       &p2p->status))
			complete(&p2p->afx_hdl.act_frm_scan);
		if (test_and_clear_bit(BRCMF_P2P_STATUS_WAITING_NEXT_ACT_FRAME,
				       &p2p->status))
			brcmf_p2p_stop_wait_next_action_frame(cfg);
		return false;
	}

	/* drop gon request of peer to process gon request by this device. */
	brcmf_dbg(INFO, "Drop received gon req !!!\n");

	return true;
}


/**
 * brcmf_p2p_notify_action_frame_rx() - received action frame.
 *
 * @ifp: interfac control.
 * @e: event message. Not used, to make it usable for fweh event dispatcher.
 * @data: payload of message, containing action frame data.
 *
 */
int brcmf_p2p_notify_action_frame_rx(struct brcmf_if *ifp,
				     const struct brcmf_event_msg *e,
				     void *data)
{
	struct brcmf_cfg80211_info *cfg = ifp->drvr->config;
	struct brcmf_p2p_info *p2p = &cfg->p2p;
	struct afx_hdl *afx_hdl = &p2p->afx_hdl;
	struct wireless_dev *wdev;
	u32 mgmt_frame_len = e->datalen - sizeof(struct brcmf_rx_mgmt_data);
	struct brcmf_rx_mgmt_data *rxframe = (struct brcmf_rx_mgmt_data *)data;
	u8 *frame = (u8 *)(rxframe + 1);
	struct brcmf_p2p_pub_act_frame *act_frm;
	struct brcmf_p2psd_gas_pub_act_frame *sd_act_frm;
	struct brcmu_chan ch;
	struct ieee80211_mgmt *mgmt_frame;
	s32 freq;
	u16 mgmt_type;
	u8 action;

	ch.chspec = be16_to_cpu(rxframe->chanspec);
	cfg->d11inf.decchspec(&ch);
	/* Check if wpa_supplicant has registered for this frame */
	brcmf_dbg(INFO, "ifp->vif->mgmt_rx_reg %04x\n", ifp->vif->mgmt_rx_reg);
	mgmt_type = (IEEE80211_STYPE_ACTION & IEEE80211_FCTL_STYPE) >> 4;
	if ((ifp->vif->mgmt_rx_reg & BIT(mgmt_type)) == 0)
		return 0;

	brcmf_p2p_print_actframe(false, frame, mgmt_frame_len);

	action = P2P_PAF_SUBTYPE_INVALID;
	if (brcmf_p2p_is_pub_action(frame, mgmt_frame_len)) {
		act_frm = (struct brcmf_p2p_pub_act_frame *)frame;
		action = act_frm->subtype;
		if ((action == P2P_PAF_GON_REQ) &&
		    (brcmf_p2p_gon_req_collision(p2p, (u8 *)e->addr))) {
			if (test_bit(BRCMF_P2P_STATUS_FINDING_COMMON_CHANNEL,
				     &p2p->status) &&
			    (memcmp(afx_hdl->tx_dst_addr, e->addr,
				    ETH_ALEN) == 0)) {
				afx_hdl->peer_chan = ch.chnum;
				brcmf_dbg(INFO, "GON request: Peer found, channel=%d\n",
					  afx_hdl->peer_chan);
				complete(&afx_hdl->act_frm_scan);
			}
			return 0;
		}
		/* After complete GO Negotiation, roll back to mpc mode */
		if ((action == P2P_PAF_GON_CONF) ||
		    (action == P2P_PAF_PROVDIS_RSP))
			brcmf_set_mpc(ifp, 1);
		if (action == P2P_PAF_GON_CONF) {
			brcmf_dbg(TRACE, "P2P: GO_NEG_PHASE status cleared\n");
			clear_bit(BRCMF_P2P_STATUS_GO_NEG_PHASE, &p2p->status);
		}
	} else if (brcmf_p2p_is_gas_action(frame, mgmt_frame_len)) {
		sd_act_frm = (struct brcmf_p2psd_gas_pub_act_frame *)frame;
		action = sd_act_frm->action;
	}

	if (test_bit(BRCMF_P2P_STATUS_WAITING_NEXT_ACT_FRAME, &p2p->status) &&
	    (p2p->next_af_subtype == action)) {
		brcmf_dbg(TRACE, "We got a right next frame! (%d)\n", action);
		clear_bit(BRCMF_P2P_STATUS_WAITING_NEXT_ACT_FRAME,
			  &p2p->status);
		/* Stop waiting for next AF. */
		brcmf_p2p_stop_wait_next_action_frame(cfg);
	}

	mgmt_frame = kzalloc(offsetof(struct ieee80211_mgmt, u) +
			     mgmt_frame_len, GFP_KERNEL);
	if (!mgmt_frame) {
		brcmf_err("No memory available for action frame\n");
		return -ENOMEM;
	}
	memcpy(mgmt_frame->da, ifp->mac_addr, ETH_ALEN);
	brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_BSSID, mgmt_frame->bssid,
			       ETH_ALEN);
	memcpy(mgmt_frame->sa, e->addr, ETH_ALEN);
	mgmt_frame->frame_control = cpu_to_le16(IEEE80211_STYPE_ACTION);
	memcpy(&mgmt_frame->u, frame, mgmt_frame_len);
	mgmt_frame_len += offsetof(struct ieee80211_mgmt, u);

	freq = ieee80211_channel_to_frequency(ch.chnum,
					      ch.band == BRCMU_CHAN_BAND_2G ?
					      IEEE80211_BAND_2GHZ :
					      IEEE80211_BAND_5GHZ);

	wdev = &ifp->vif->wdev;
	cfg80211_rx_mgmt(wdev, freq, 0, (u8 *)mgmt_frame, mgmt_frame_len,
			 GFP_ATOMIC);

	kfree(mgmt_frame);
	return 0;
}


/**
 * brcmf_p2p_notify_action_tx_complete() - transmit action frame complete
 *
 * @ifp: interfac control.
 * @e: event message. Not used, to make it usable for fweh event dispatcher.
 * @data: not used.
 *
 */
int brcmf_p2p_notify_action_tx_complete(struct brcmf_if *ifp,
					const struct brcmf_event_msg *e,
					void *data)
{
	struct brcmf_cfg80211_info *cfg = ifp->drvr->config;
	struct brcmf_p2p_info *p2p = &cfg->p2p;

	brcmf_dbg(INFO, "Enter: event %s, status=%d\n",
		  e->event_code == BRCMF_E_ACTION_FRAME_OFF_CHAN_COMPLETE ?
		  "ACTION_FRAME_OFF_CHAN_COMPLETE" : "ACTION_FRAME_COMPLETE",
		  e->status);

	if (!test_bit(BRCMF_P2P_STATUS_SENDING_ACT_FRAME, &p2p->status))
		return 0;

	if (e->event_code == BRCMF_E_ACTION_FRAME_COMPLETE) {
		if (e->status == BRCMF_E_STATUS_SUCCESS)
			set_bit(BRCMF_P2P_STATUS_ACTION_TX_COMPLETED,
				&p2p->status);
		else {
			set_bit(BRCMF_P2P_STATUS_ACTION_TX_NOACK, &p2p->status);
			/* If there is no ack, we don't need to wait for
			 * WLC_E_ACTION_FRAME_OFFCHAN_COMPLETE event
			 */
			brcmf_p2p_stop_wait_next_action_frame(cfg);
		}

	} else {
		complete(&p2p->send_af_done);
	}
	return 0;
}


/**
 * brcmf_p2p_tx_action_frame() - send action frame over fil.
 *
 * @p2p: p2p info struct for vif.
 * @af_params: action frame data/info.
 *
 * Send an action frame immediately without doing channel synchronization.
 *
 * This function waits for a completion event before returning.
 * The WLC_E_ACTION_FRAME_COMPLETE event will be received when the action
 * frame is transmitted.
 */
static s32 brcmf_p2p_tx_action_frame(struct brcmf_p2p_info *p2p,
				     struct brcmf_fil_af_params_le *af_params)
{
	struct brcmf_cfg80211_vif *vif;
	s32 err = 0;
	s32 timeout = 0;

	brcmf_dbg(TRACE, "Enter\n");

	INIT_COMPLETION(p2p->send_af_done);
	clear_bit(BRCMF_P2P_STATUS_ACTION_TX_COMPLETED, &p2p->status);
	clear_bit(BRCMF_P2P_STATUS_ACTION_TX_NOACK, &p2p->status);

	vif = p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif;
	err = brcmf_fil_bsscfg_data_set(vif->ifp, "actframe", af_params,
					sizeof(*af_params));
	if (err) {
		brcmf_err(" sending action frame has failed\n");
		goto exit;
	}

	p2p->af_sent_channel = le32_to_cpu(af_params->channel);
	p2p->af_tx_sent_jiffies = jiffies;

	timeout = wait_for_completion_timeout(&p2p->send_af_done,
					msecs_to_jiffies(P2P_AF_MAX_WAIT_TIME));

	if (test_bit(BRCMF_P2P_STATUS_ACTION_TX_COMPLETED, &p2p->status)) {
		brcmf_dbg(TRACE, "TX action frame operation is success\n");
	} else {
		err = -EIO;
		brcmf_dbg(TRACE, "TX action frame operation has failed\n");
	}
	/* clear status bit for action tx */
	clear_bit(BRCMF_P2P_STATUS_ACTION_TX_COMPLETED, &p2p->status);
	clear_bit(BRCMF_P2P_STATUS_ACTION_TX_NOACK, &p2p->status);

exit:
	return err;
}


/**
 * brcmf_p2p_pub_af_tx() - public action frame tx routine.
 *
 * @cfg: driver private data for cfg80211 interface.
 * @af_params: action frame data/info.
 * @config_af_params: configuration data for action frame.
 *
 * routine which transmits ation frame public type.
 */
static s32 brcmf_p2p_pub_af_tx(struct brcmf_cfg80211_info *cfg,
			       struct brcmf_fil_af_params_le *af_params,
			       struct brcmf_config_af_params *config_af_params)
{
	struct brcmf_p2p_info *p2p = &cfg->p2p;
	struct brcmf_fil_action_frame_le *action_frame;
	struct brcmf_p2p_pub_act_frame *act_frm;
	s32 err = 0;
	u16 ie_len;

	action_frame = &af_params->action_frame;
	act_frm = (struct brcmf_p2p_pub_act_frame *)(action_frame->data);

	config_af_params->extra_listen = true;

	switch (act_frm->subtype) {
	case P2P_PAF_GON_REQ:
		brcmf_dbg(TRACE, "P2P: GO_NEG_PHASE status set\n");
		set_bit(BRCMF_P2P_STATUS_GO_NEG_PHASE, &p2p->status);
		config_af_params->mpc_onoff = 0;
		config_af_params->search_channel = true;
		p2p->next_af_subtype = act_frm->subtype + 1;
		p2p->gon_req_action = true;
		/* increase dwell time to wait for RESP frame */
		af_params->dwell_time = cpu_to_le32(P2P_AF_MED_DWELL_TIME);
		break;
	case P2P_PAF_GON_RSP:
		p2p->next_af_subtype = act_frm->subtype + 1;
		/* increase dwell time to wait for CONF frame */
		af_params->dwell_time = cpu_to_le32(P2P_AF_MED_DWELL_TIME);
		break;
	case P2P_PAF_GON_CONF:
		/* If we reached till GO Neg confirmation reset the filter */
		brcmf_dbg(TRACE, "P2P: GO_NEG_PHASE status cleared\n");
		clear_bit(BRCMF_P2P_STATUS_GO_NEG_PHASE, &p2p->status);
		/* turn on mpc again if go nego is done */
		config_af_params->mpc_onoff = 1;
		/* minimize dwell time */
		af_params->dwell_time = cpu_to_le32(P2P_AF_MIN_DWELL_TIME);
		config_af_params->extra_listen = false;
		break;
	case P2P_PAF_INVITE_REQ:
		config_af_params->search_channel = true;
		p2p->next_af_subtype = act_frm->subtype + 1;
		/* increase dwell time */
		af_params->dwell_time = cpu_to_le32(P2P_AF_MED_DWELL_TIME);
		break;
	case P2P_PAF_INVITE_RSP:
		/* minimize dwell time */
		af_params->dwell_time = cpu_to_le32(P2P_AF_MIN_DWELL_TIME);
		config_af_params->extra_listen = false;
		break;
	case P2P_PAF_DEVDIS_REQ:
		config_af_params->search_channel = true;
		p2p->next_af_subtype = act_frm->subtype + 1;
		/* maximize dwell time to wait for RESP frame */
		af_params->dwell_time = cpu_to_le32(P2P_AF_LONG_DWELL_TIME);
		break;
	case P2P_PAF_DEVDIS_RSP:
		/* minimize dwell time */
		af_params->dwell_time = cpu_to_le32(P2P_AF_MIN_DWELL_TIME);
		config_af_params->extra_listen = false;
		break;
	case P2P_PAF_PROVDIS_REQ:
		ie_len = le16_to_cpu(action_frame->len) -
			 offsetof(struct brcmf_p2p_pub_act_frame, elts);
		if (cfg80211_get_p2p_attr(&act_frm->elts[0], ie_len,
					  IEEE80211_P2P_ATTR_GROUP_ID,
					  NULL, 0) < 0)
			config_af_params->search_channel = true;
		config_af_params->mpc_onoff = 0;
		p2p->next_af_subtype = act_frm->subtype + 1;
		/* increase dwell time to wait for RESP frame */
		af_params->dwell_time = cpu_to_le32(P2P_AF_MED_DWELL_TIME);
		break;
	case P2P_PAF_PROVDIS_RSP:
		/* wpa_supplicant send go nego req right after prov disc */
		p2p->next_af_subtype = P2P_PAF_GON_REQ;
		/* increase dwell time to MED level */
		af_params->dwell_time = cpu_to_le32(P2P_AF_MED_DWELL_TIME);
		config_af_params->extra_listen = false;
		break;
	default:
		brcmf_err("Unknown p2p pub act frame subtype: %d\n",
			  act_frm->subtype);
		err = -EINVAL;
	}
	return err;
}

/**
 * brcmf_p2p_send_action_frame() - send action frame .
 *
 * @cfg: driver private data for cfg80211 interface.
 * @ndev: net device to transmit on.
 * @af_params: configuration data for action frame.
 */
bool brcmf_p2p_send_action_frame(struct brcmf_cfg80211_info *cfg,
				 struct net_device *ndev,
				 struct brcmf_fil_af_params_le *af_params)
{
	struct brcmf_p2p_info *p2p = &cfg->p2p;
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_fil_action_frame_le *action_frame;
	struct brcmf_config_af_params config_af_params;
	struct afx_hdl *afx_hdl = &p2p->afx_hdl;
	u16 action_frame_len;
	bool ack = false;
	u8 category;
	u8 action;
	s32 tx_retry;
	s32 extra_listen_time;
	uint delta_ms;

	action_frame = &af_params->action_frame;
	action_frame_len = le16_to_cpu(action_frame->len);

	brcmf_p2p_print_actframe(true, action_frame->data, action_frame_len);

	/* Add the default dwell time. Dwell time to stay off-channel */
	/* to wait for a response action frame after transmitting an  */
	/* GO Negotiation action frame                                */
	af_params->dwell_time = cpu_to_le32(P2P_AF_DWELL_TIME);

	category = action_frame->data[DOT11_ACTION_CAT_OFF];
	action = action_frame->data[DOT11_ACTION_ACT_OFF];

	/* initialize variables */
	p2p->next_af_subtype = P2P_PAF_SUBTYPE_INVALID;
	p2p->gon_req_action = false;

	/* config parameters */
	config_af_params.mpc_onoff = -1;
	config_af_params.search_channel = false;
	config_af_params.extra_listen = false;

	if (brcmf_p2p_is_pub_action(action_frame->data, action_frame_len)) {
		/* p2p public action frame process */
		if (brcmf_p2p_pub_af_tx(cfg, af_params, &config_af_params)) {
			/* Just send unknown subtype frame with */
			/* default parameters.                  */
			brcmf_err("P2P Public action frame, unknown subtype.\n");
		}
	} else if (brcmf_p2p_is_gas_action(action_frame->data,
					   action_frame_len)) {
		/* service discovery process */
		if (action == P2PSD_ACTION_ID_GAS_IREQ ||
		    action == P2PSD_ACTION_ID_GAS_CREQ) {
			/* configure service discovery query frame */
			config_af_params.search_channel = true;

			/* save next af suptype to cancel */
			/* remaining dwell time           */
			p2p->next_af_subtype = action + 1;

			af_params->dwell_time =
				cpu_to_le32(P2P_AF_MED_DWELL_TIME);
		} else if (action == P2PSD_ACTION_ID_GAS_IRESP ||
			   action == P2PSD_ACTION_ID_GAS_CRESP) {
			/* configure service discovery response frame */
			af_params->dwell_time =
				cpu_to_le32(P2P_AF_MIN_DWELL_TIME);
		} else {
			brcmf_err("Unknown action type: %d\n", action);
			goto exit;
		}
	} else if (brcmf_p2p_is_p2p_action(action_frame->data,
					   action_frame_len)) {
		/* do not configure anything. it will be */
		/* sent with a default configuration     */
	} else {
		brcmf_err("Unknown Frame: category 0x%x, action 0x%x\n",
			  category, action);
		return false;
	}

	/* if connecting on primary iface, sleep for a while before sending
	 * af tx for VSDB
	 */
	if (test_bit(BRCMF_VIF_STATUS_CONNECTING,
		     &p2p->bss_idx[P2PAPI_BSSCFG_PRIMARY].vif->sme_state))
		msleep(50);

	/* if scan is ongoing, abort current scan. */
	if (test_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status))
		brcmf_abort_scanning(cfg);

	memcpy(afx_hdl->tx_dst_addr, action_frame->da, ETH_ALEN);

	/* To make sure to send successfully action frame, turn off mpc */
	if (config_af_params.mpc_onoff == 0)
		brcmf_set_mpc(ifp, 0);

	/* set status and destination address before sending af */
	if (p2p->next_af_subtype != P2P_PAF_SUBTYPE_INVALID) {
		/* set status to cancel the remained dwell time in rx process */
		set_bit(BRCMF_P2P_STATUS_WAITING_NEXT_ACT_FRAME, &p2p->status);
	}

	p2p->af_sent_channel = 0;
	set_bit(BRCMF_P2P_STATUS_SENDING_ACT_FRAME, &p2p->status);
	/* validate channel and p2p ies */
	if (config_af_params.search_channel &&
	    IS_P2P_SOCIAL_CHANNEL(le32_to_cpu(af_params->channel)) &&
	    p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif->saved_ie.probe_req_ie_len) {
		afx_hdl = &p2p->afx_hdl;
		afx_hdl->peer_listen_chan = le32_to_cpu(af_params->channel);

		if (brcmf_p2p_af_searching_channel(p2p) ==
							P2P_INVALID_CHANNEL) {
			brcmf_err("Couldn't find peer's channel.\n");
			goto exit;
		}

		/* Abort scan even for VSDB scenarios. Scan gets aborted in
		 * firmware but after the check of piggyback algorithm. To take
		 * care of current piggback algo, lets abort the scan here
		 * itself.
		 */
		brcmf_notify_escan_complete(cfg, ifp, true, true);

		/* update channel */
		af_params->channel = cpu_to_le32(afx_hdl->peer_chan);
	}

	tx_retry = 0;
	while (!p2p->block_gon_req_tx &&
	       (ack == false) && (tx_retry < P2P_AF_TX_MAX_RETRY)) {
		ack = !brcmf_p2p_tx_action_frame(p2p, af_params);
		tx_retry++;
	}
	if (ack == false) {
		brcmf_err("Failed to send Action Frame(retry %d)\n", tx_retry);
		clear_bit(BRCMF_P2P_STATUS_GO_NEG_PHASE, &p2p->status);
	}

exit:
	clear_bit(BRCMF_P2P_STATUS_SENDING_ACT_FRAME, &p2p->status);

	/* WAR: sometimes dongle does not keep the dwell time of 'actframe'.
	 * if we coundn't get the next action response frame and dongle does
	 * not keep the dwell time, go to listen state again to get next action
	 * response frame.
	 */
	if (ack && config_af_params.extra_listen && !p2p->block_gon_req_tx &&
	    test_bit(BRCMF_P2P_STATUS_WAITING_NEXT_ACT_FRAME, &p2p->status) &&
	    p2p->af_sent_channel == afx_hdl->my_listen_chan) {
		delta_ms = jiffies_to_msecs(jiffies - p2p->af_tx_sent_jiffies);
		if (le32_to_cpu(af_params->dwell_time) > delta_ms)
			extra_listen_time = le32_to_cpu(af_params->dwell_time) -
					    delta_ms;
		else
			extra_listen_time = 0;
		if (extra_listen_time > 50) {
			set_bit(BRCMF_P2P_STATUS_WAITING_NEXT_AF_LISTEN,
				&p2p->status);
			brcmf_dbg(INFO, "Wait more time! actual af time:%d, calculated extra listen:%d\n",
				  le32_to_cpu(af_params->dwell_time),
				  extra_listen_time);
			extra_listen_time += 100;
			if (!brcmf_p2p_discover_listen(p2p,
						       p2p->af_sent_channel,
						       extra_listen_time)) {
				unsigned long duration;

				extra_listen_time += 100;
				duration = msecs_to_jiffies(extra_listen_time);
				wait_for_completion_timeout(&p2p->wait_next_af,
							    duration);
			}
			clear_bit(BRCMF_P2P_STATUS_WAITING_NEXT_AF_LISTEN,
				  &p2p->status);
		}
	}

	if (p2p->block_gon_req_tx) {
		/* if ack is true, supplicant will wait more time(100ms).
		 * so we will return it as a success to get more time .
		 */
		p2p->block_gon_req_tx = false;
		ack = true;
	}

	clear_bit(BRCMF_P2P_STATUS_WAITING_NEXT_ACT_FRAME, &p2p->status);
	/* if all done, turn mpc on again */
	if (config_af_params.mpc_onoff == 1)
		brcmf_set_mpc(ifp, 1);

	return ack;
}

/**
 * brcmf_p2p_notify_rx_mgmt_p2p_probereq() - Event handler for p2p probe req.
 *
 * @ifp: interface pointer for which event was received.
 * @e: even message.
 * @data: payload of event message (probe request).
 */
s32 brcmf_p2p_notify_rx_mgmt_p2p_probereq(struct brcmf_if *ifp,
					  const struct brcmf_event_msg *e,
					  void *data)
{
	struct brcmf_cfg80211_info *cfg = ifp->drvr->config;
	struct brcmf_p2p_info *p2p = &cfg->p2p;
	struct afx_hdl *afx_hdl = &p2p->afx_hdl;
	struct brcmf_cfg80211_vif *vif = ifp->vif;
	struct brcmf_rx_mgmt_data *rxframe = (struct brcmf_rx_mgmt_data *)data;
	u16 chanspec = be16_to_cpu(rxframe->chanspec);
	struct brcmu_chan ch;
	u8 *mgmt_frame;
	u32 mgmt_frame_len;
	s32 freq;
	u16 mgmt_type;

	brcmf_dbg(INFO, "Enter: event %d reason %d\n", e->event_code,
		  e->reason);

	ch.chspec = be16_to_cpu(rxframe->chanspec);
	cfg->d11inf.decchspec(&ch);

	if (test_bit(BRCMF_P2P_STATUS_FINDING_COMMON_CHANNEL, &p2p->status) &&
	    (memcmp(afx_hdl->tx_dst_addr, e->addr, ETH_ALEN) == 0)) {
		afx_hdl->peer_chan = ch.chnum;
		brcmf_dbg(INFO, "PROBE REQUEST: Peer found, channel=%d\n",
			  afx_hdl->peer_chan);
		complete(&afx_hdl->act_frm_scan);
	}

	/* Firmware sends us two proberesponses for each idx one. At the */
	/* moment anything but bsscfgidx 0 is passed up to supplicant    */
	if (e->bsscfgidx == 0)
		return 0;

	/* Filter any P2P probe reqs arriving during the GO-NEG Phase */
	if (test_bit(BRCMF_P2P_STATUS_GO_NEG_PHASE, &p2p->status)) {
		brcmf_dbg(INFO, "Filtering P2P probe_req in GO-NEG phase\n");
		return 0;
	}

	/* Check if wpa_supplicant has registered for this frame */
	brcmf_dbg(INFO, "vif->mgmt_rx_reg %04x\n", vif->mgmt_rx_reg);
	mgmt_type = (IEEE80211_STYPE_PROBE_REQ & IEEE80211_FCTL_STYPE) >> 4;
	if ((vif->mgmt_rx_reg & BIT(mgmt_type)) == 0)
		return 0;

	mgmt_frame = (u8 *)(rxframe + 1);
	mgmt_frame_len = e->datalen - sizeof(*rxframe);
	freq = ieee80211_channel_to_frequency(ch.chnum,
					      ch.band == BRCMU_CHAN_BAND_2G ?
					      IEEE80211_BAND_2GHZ :
					      IEEE80211_BAND_5GHZ);

	cfg80211_rx_mgmt(&vif->wdev, freq, 0, mgmt_frame, mgmt_frame_len,
			 GFP_ATOMIC);

	brcmf_dbg(INFO, "mgmt_frame_len (%d) , e->datalen (%d), chanspec (%04x), freq (%d)\n",
		  mgmt_frame_len, e->datalen, chanspec, freq);

	return 0;
}


/**
 * brcmf_p2p_attach() - attach for P2P.
 *
 * @cfg: driver private data for cfg80211 interface.
 */
s32 brcmf_p2p_attach(struct brcmf_cfg80211_info *cfg)
{
	struct brcmf_if *pri_ifp;
	struct brcmf_if *p2p_ifp;
	struct brcmf_cfg80211_vif *p2p_vif;
	struct brcmf_p2p_info *p2p;
	struct brcmf_pub *drvr;
	s32 bssidx;
	s32 err = 0;

	p2p = &cfg->p2p;
	p2p->cfg = cfg;

	drvr = cfg->pub;

	pri_ifp = drvr->iflist[0];
	p2p_ifp = drvr->iflist[1];

	p2p->bss_idx[P2PAPI_BSSCFG_PRIMARY].vif = pri_ifp->vif;

	if (p2p_ifp) {
		p2p_vif = brcmf_alloc_vif(cfg, NL80211_IFTYPE_P2P_DEVICE,
					  false);
		if (IS_ERR(p2p_vif)) {
			brcmf_err("could not create discovery vif\n");
			err = -ENOMEM;
			goto exit;
		}

		p2p_vif->ifp = p2p_ifp;
		p2p_ifp->vif = p2p_vif;
		p2p_vif->wdev.netdev = p2p_ifp->ndev;
		p2p_ifp->ndev->ieee80211_ptr = &p2p_vif->wdev;
		SET_NETDEV_DEV(p2p_ifp->ndev, wiphy_dev(cfg->wiphy));

		p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif = p2p_vif;

		brcmf_p2p_generate_bss_mac(p2p, NULL);
		memcpy(p2p_ifp->mac_addr, p2p->dev_addr, ETH_ALEN);
		brcmf_p2p_set_firmware(pri_ifp, p2p->dev_addr);

		/* Initialize P2P Discovery in the firmware */
		err = brcmf_fil_iovar_int_set(pri_ifp, "p2p_disc", 1);
		if (err < 0) {
			brcmf_err("set p2p_disc error\n");
			brcmf_free_vif(cfg, p2p_vif);
			goto exit;
		}
		/* obtain bsscfg index for P2P discovery */
		err = brcmf_fil_iovar_int_get(pri_ifp, "p2p_dev", &bssidx);
		if (err < 0) {
			brcmf_err("retrieving discover bsscfg index failed\n");
			brcmf_free_vif(cfg, p2p_vif);
			goto exit;
		}
		/* Verify that firmware uses same bssidx as driver !! */
		if (p2p_ifp->bssidx != bssidx) {
			brcmf_err("Incorrect bssidx=%d, compared to p2p_ifp->bssidx=%d\n",
				  bssidx, p2p_ifp->bssidx);
			brcmf_free_vif(cfg, p2p_vif);
			goto exit;
		}

		init_completion(&p2p->send_af_done);
		INIT_WORK(&p2p->afx_hdl.afx_work, brcmf_p2p_afx_handler);
		init_completion(&p2p->afx_hdl.act_frm_scan);
		init_completion(&p2p->wait_next_af);
	}
exit:
	return err;
}


/**
 * brcmf_p2p_detach() - detach P2P.
 *
 * @p2p: P2P specific data.
 */
void brcmf_p2p_detach(struct brcmf_p2p_info *p2p)
{
	struct brcmf_cfg80211_vif *vif;

	vif = p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif;
	if (vif != NULL) {
		brcmf_p2p_cancel_remain_on_channel(vif->ifp);
		brcmf_p2p_deinit_discovery(p2p);
		/* remove discovery interface */
		brcmf_free_vif(p2p->cfg, vif);
		p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif = NULL;
	}
	/* just set it all to zero */
	memset(p2p, 0, sizeof(*p2p));
}

/**
 * brcmf_p2p_get_current_chanspec() - Get current operation channel.
 *
 * @p2p: P2P specific data.
 * @chanspec: chanspec to be returned.
 */
static void brcmf_p2p_get_current_chanspec(struct brcmf_p2p_info *p2p,
					   u16 *chanspec)
{
	struct brcmf_if *ifp;
	u8 mac_addr[ETH_ALEN];
	struct brcmu_chan ch;
	struct brcmf_bss_info_le *bi;
	u8 *buf;

	ifp = p2p->bss_idx[P2PAPI_BSSCFG_PRIMARY].vif->ifp;

	if (brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_BSSID, mac_addr,
				   ETH_ALEN) == 0) {
		buf = kzalloc(WL_BSS_INFO_MAX, GFP_KERNEL);
		if (buf != NULL) {
			*(__le32 *)buf = cpu_to_le32(WL_BSS_INFO_MAX);
			if (brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_BSS_INFO,
						   buf, WL_BSS_INFO_MAX) == 0) {
				bi = (struct brcmf_bss_info_le *)(buf + 4);
				*chanspec = le16_to_cpu(bi->chanspec);
				kfree(buf);
				return;
			}
			kfree(buf);
		}
	}
	/* Use default channel for P2P */
	ch.chnum = BRCMF_P2P_TEMP_CHAN;
	ch.bw = BRCMU_CHAN_BW_20;
	p2p->cfg->d11inf.encchspec(&ch);
	*chanspec = ch.chspec;
}

/**
 * Change a P2P Role.
 * Parameters:
 * @mac: MAC address of the BSS to change a role
 * Returns 0 if success.
 */
int brcmf_p2p_ifchange(struct brcmf_cfg80211_info *cfg,
		       enum brcmf_fil_p2p_if_types if_type)
{
	struct brcmf_p2p_info *p2p = &cfg->p2p;
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_fil_p2p_if_le if_request;
	s32 err;
	u16 chanspec;

	brcmf_dbg(TRACE, "Enter\n");

	vif = p2p->bss_idx[P2PAPI_BSSCFG_PRIMARY].vif;
	if (!vif) {
		brcmf_err("vif for P2PAPI_BSSCFG_PRIMARY does not exist\n");
		return -EPERM;
	}
	brcmf_notify_escan_complete(cfg, vif->ifp, true, true);
	vif = p2p->bss_idx[P2PAPI_BSSCFG_CONNECTION].vif;
	if (!vif) {
		brcmf_err("vif for P2PAPI_BSSCFG_CONNECTION does not exist\n");
		return -EPERM;
	}
	brcmf_set_mpc(vif->ifp, 0);

	/* In concurrency case, STA may be already associated in a particular */
	/* channel. so retrieve the current channel of primary interface and  */
	/* then start the virtual interface on that.                          */
	brcmf_p2p_get_current_chanspec(p2p, &chanspec);

	if_request.type = cpu_to_le16((u16)if_type);
	if_request.chspec = cpu_to_le16(chanspec);
	memcpy(if_request.addr, p2p->int_addr, sizeof(if_request.addr));

	brcmf_cfg80211_arm_vif_event(cfg, vif);
	err = brcmf_fil_iovar_data_set(vif->ifp, "p2p_ifupd", &if_request,
				       sizeof(if_request));
	if (err) {
		brcmf_err("p2p_ifupd FAILED, err=%d\n", err);
		brcmf_cfg80211_arm_vif_event(cfg, NULL);
		return err;
	}
	err = brcmf_cfg80211_wait_vif_event_timeout(cfg, BRCMF_E_IF_CHANGE,
						    msecs_to_jiffies(1500));
	brcmf_cfg80211_arm_vif_event(cfg, NULL);
	if (!err)  {
		brcmf_err("No BRCMF_E_IF_CHANGE event received\n");
		return -EIO;
	}

	err = brcmf_fil_cmd_int_set(vif->ifp, BRCMF_C_SET_SCB_TIMEOUT,
				    BRCMF_SCB_TIMEOUT_VALUE);

	return err;
}

static int brcmf_p2p_request_p2p_if(struct brcmf_p2p_info *p2p,
				    struct brcmf_if *ifp, u8 ea[ETH_ALEN],
				    enum brcmf_fil_p2p_if_types iftype)
{
	struct brcmf_fil_p2p_if_le if_request;
	int err;
	u16 chanspec;

	/* we need a default channel */
	brcmf_p2p_get_current_chanspec(p2p, &chanspec);

	/* fill the firmware request */
	memcpy(if_request.addr, ea, ETH_ALEN);
	if_request.type = cpu_to_le16((u16)iftype);
	if_request.chspec = cpu_to_le16(chanspec);

	err = brcmf_fil_iovar_data_set(ifp, "p2p_ifadd", &if_request,
				       sizeof(if_request));
	if (err)
		return err;

	return err;
}

static int brcmf_p2p_disable_p2p_if(struct brcmf_cfg80211_vif *vif)
{
	struct brcmf_cfg80211_info *cfg = wdev_to_cfg(&vif->wdev);
	struct net_device *pri_ndev = cfg_to_ndev(cfg);
	struct brcmf_if *ifp = netdev_priv(pri_ndev);
	u8 *addr = vif->wdev.netdev->dev_addr;

	return brcmf_fil_iovar_data_set(ifp, "p2p_ifdis", addr, ETH_ALEN);
}

static int brcmf_p2p_release_p2p_if(struct brcmf_cfg80211_vif *vif)
{
	struct brcmf_cfg80211_info *cfg = wdev_to_cfg(&vif->wdev);
	struct net_device *pri_ndev = cfg_to_ndev(cfg);
	struct brcmf_if *ifp = netdev_priv(pri_ndev);
	u8 *addr = vif->wdev.netdev->dev_addr;

	return brcmf_fil_iovar_data_set(ifp, "p2p_ifdel", addr, ETH_ALEN);
}

/**
 * brcmf_p2p_create_p2pdev() - create a P2P_DEVICE virtual interface.
 *
 * @p2p: P2P specific data.
 * @wiphy: wiphy device of new interface.
 * @addr: mac address for this new interface.
 */
static struct wireless_dev *brcmf_p2p_create_p2pdev(struct brcmf_p2p_info *p2p,
						    struct wiphy *wiphy,
						    u8 *addr)
{
	struct brcmf_cfg80211_vif *p2p_vif;
	struct brcmf_if *p2p_ifp;
	struct brcmf_if *pri_ifp;
	int err;
	u32 bssidx;

	if (p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif)
		return ERR_PTR(-ENOSPC);

	p2p_vif = brcmf_alloc_vif(p2p->cfg, NL80211_IFTYPE_P2P_DEVICE,
				  false);
	if (IS_ERR(p2p_vif)) {
		brcmf_err("could not create discovery vif\n");
		return (struct wireless_dev *)p2p_vif;
	}

	pri_ifp = p2p->bss_idx[P2PAPI_BSSCFG_PRIMARY].vif->ifp;
	brcmf_p2p_generate_bss_mac(p2p, addr);
	brcmf_p2p_set_firmware(pri_ifp, p2p->dev_addr);

	brcmf_cfg80211_arm_vif_event(p2p->cfg, p2p_vif);

	/* Initialize P2P Discovery in the firmware */
	err = brcmf_fil_iovar_int_set(pri_ifp, "p2p_disc", 1);
	if (err < 0) {
		brcmf_err("set p2p_disc error\n");
		brcmf_cfg80211_arm_vif_event(p2p->cfg, NULL);
		goto fail;
	}

	/* wait for firmware event */
	err = brcmf_cfg80211_wait_vif_event_timeout(p2p->cfg, BRCMF_E_IF_ADD,
						    msecs_to_jiffies(1500));
	brcmf_cfg80211_arm_vif_event(p2p->cfg, NULL);
	if (!err) {
		brcmf_err("timeout occurred\n");
		err = -EIO;
		goto fail;
	}

	/* discovery interface created */
	p2p_ifp = p2p_vif->ifp;
	p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif = p2p_vif;
	memcpy(p2p_ifp->mac_addr, p2p->dev_addr, ETH_ALEN);
	memcpy(&p2p_vif->wdev.address, p2p->dev_addr, sizeof(p2p->dev_addr));

	/* verify bsscfg index for P2P discovery */
	err = brcmf_fil_iovar_int_get(pri_ifp, "p2p_dev", &bssidx);
	if (err < 0) {
		brcmf_err("retrieving discover bsscfg index failed\n");
		goto fail;
	}

	WARN_ON(p2p_ifp->bssidx != bssidx);

	init_completion(&p2p->send_af_done);
	INIT_WORK(&p2p->afx_hdl.afx_work, brcmf_p2p_afx_handler);
	init_completion(&p2p->afx_hdl.act_frm_scan);
	init_completion(&p2p->wait_next_af);

	return &p2p_vif->wdev;

fail:
	brcmf_free_vif(p2p->cfg, p2p_vif);
	return ERR_PTR(err);
}

/**
 * brcmf_p2p_delete_p2pdev() - delete P2P_DEVICE virtual interface.
 *
 * @vif: virtual interface object to delete.
 */
static void brcmf_p2p_delete_p2pdev(struct brcmf_cfg80211_info *cfg,
				    struct brcmf_cfg80211_vif *vif)
{
	cfg80211_unregister_wdev(&vif->wdev);
	cfg->p2p.bss_idx[P2PAPI_BSSCFG_DEVICE].vif = NULL;
	brcmf_free_vif(cfg, vif);
}

/**
 * brcmf_p2p_add_vif() - create a new P2P virtual interface.
 *
 * @wiphy: wiphy device of new interface.
 * @name: name of the new interface.
 * @type: nl80211 interface type.
 * @flags: not used.
 * @params: contains mac address for P2P device.
 */
struct wireless_dev *brcmf_p2p_add_vif(struct wiphy *wiphy, const char *name,
				       enum nl80211_iftype type, u32 *flags,
				       struct vif_params *params)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_if *ifp = netdev_priv(cfg_to_ndev(cfg));
	struct brcmf_cfg80211_vif *vif;
	enum brcmf_fil_p2p_if_types iftype;
	enum wl_mode mode;
	int err;

	if (brcmf_cfg80211_vif_event_armed(cfg))
		return ERR_PTR(-EBUSY);

	brcmf_dbg(INFO, "adding vif \"%s\" (type=%d)\n", name, type);

	switch (type) {
	case NL80211_IFTYPE_P2P_CLIENT:
		iftype = BRCMF_FIL_P2P_IF_CLIENT;
		mode = WL_MODE_BSS;
		break;
	case NL80211_IFTYPE_P2P_GO:
		iftype = BRCMF_FIL_P2P_IF_GO;
		mode = WL_MODE_AP;
		break;
	case NL80211_IFTYPE_P2P_DEVICE:
		return brcmf_p2p_create_p2pdev(&cfg->p2p, wiphy,
					       params->macaddr);
	default:
		return ERR_PTR(-EOPNOTSUPP);
	}

	vif = brcmf_alloc_vif(cfg, type, false);
	if (IS_ERR(vif))
		return (struct wireless_dev *)vif;
	brcmf_cfg80211_arm_vif_event(cfg, vif);

	err = brcmf_p2p_request_p2p_if(&cfg->p2p, ifp, cfg->p2p.int_addr,
				       iftype);
	if (err) {
		brcmf_cfg80211_arm_vif_event(cfg, NULL);
		goto fail;
	}

	/* wait for firmware event */
	err = brcmf_cfg80211_wait_vif_event_timeout(cfg, BRCMF_E_IF_ADD,
						    msecs_to_jiffies(1500));
	brcmf_cfg80211_arm_vif_event(cfg, NULL);
	if (!err) {
		brcmf_err("timeout occurred\n");
		err = -EIO;
		goto fail;
	}

	/* interface created in firmware */
	ifp = vif->ifp;
	if (!ifp) {
		brcmf_err("no if pointer provided\n");
		err = -ENOENT;
		goto fail;
	}

	strncpy(ifp->ndev->name, name, sizeof(ifp->ndev->name) - 1);
	err = brcmf_net_attach(ifp, true);
	if (err) {
		brcmf_err("Registering netdevice failed\n");
		goto fail;
	}
	cfg->p2p.bss_idx[P2PAPI_BSSCFG_CONNECTION].vif = vif;
	/* Disable firmware roaming for P2P interface  */
	brcmf_fil_iovar_int_set(ifp, "roam_off", 1);
	if (iftype == BRCMF_FIL_P2P_IF_GO) {
		/* set station timeout for p2p */
		brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCB_TIMEOUT,
				      BRCMF_SCB_TIMEOUT_VALUE);
	}
	return &ifp->vif->wdev;

fail:
	brcmf_free_vif(cfg, vif);
	return ERR_PTR(err);
}

/**
 * brcmf_p2p_del_vif() - delete a P2P virtual interface.
 *
 * @wiphy: wiphy device of interface.
 * @wdev: wireless device of interface.
 *
 * TODO: not yet supported.
 */
int brcmf_p2p_del_vif(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct brcmf_cfg80211_info *cfg = wiphy_priv(wiphy);
	struct brcmf_p2p_info *p2p = &cfg->p2p;
	struct brcmf_cfg80211_vif *vif;
	unsigned long jiffie_timeout = msecs_to_jiffies(1500);
	bool wait_for_disable = false;
	int err;

	brcmf_dbg(TRACE, "delete P2P vif\n");
	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);

	switch (vif->wdev.iftype) {
	case NL80211_IFTYPE_P2P_CLIENT:
		if (test_bit(BRCMF_VIF_STATUS_DISCONNECTING, &vif->sme_state))
			wait_for_disable = true;
		break;

	case NL80211_IFTYPE_P2P_GO:
		if (!brcmf_p2p_disable_p2p_if(vif))
			wait_for_disable = true;
		break;

	case NL80211_IFTYPE_P2P_DEVICE:
		brcmf_p2p_delete_p2pdev(cfg, vif);
		return 0;
	default:
		return -ENOTSUPP;
		break;
	}

	clear_bit(BRCMF_P2P_STATUS_GO_NEG_PHASE, &p2p->status);
	brcmf_dbg(INFO, "P2P: GO_NEG_PHASE status cleared\n");

	if (wait_for_disable)
		wait_for_completion_timeout(&cfg->vif_disabled,
					    msecs_to_jiffies(500));

	brcmf_vif_clear_mgmt_ies(vif);

	brcmf_cfg80211_arm_vif_event(cfg, vif);
	err = brcmf_p2p_release_p2p_if(vif);
	if (!err) {
		/* wait for firmware event */
		err = brcmf_cfg80211_wait_vif_event_timeout(cfg, BRCMF_E_IF_DEL,
							    jiffie_timeout);
		if (!err)
			err = -EIO;
		else
			err = 0;
	}
	brcmf_cfg80211_arm_vif_event(cfg, NULL);
	brcmf_free_vif(cfg, vif);
	p2p->bss_idx[P2PAPI_BSSCFG_CONNECTION].vif = NULL;

	return err;
}

int brcmf_p2p_start_device(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_p2p_info *p2p = &cfg->p2p;
	struct brcmf_cfg80211_vif *vif;
	int err;

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	mutex_lock(&cfg->usr_sync);
	err = brcmf_p2p_enable_discovery(p2p);
	if (!err)
		set_bit(BRCMF_VIF_STATUS_READY, &vif->sme_state);
	mutex_unlock(&cfg->usr_sync);
	return err;
}

void brcmf_p2p_stop_device(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct brcmf_p2p_info *p2p = &cfg->p2p;
	struct brcmf_cfg80211_vif *vif;

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	mutex_lock(&cfg->usr_sync);
	(void)brcmf_p2p_deinit_discovery(p2p);
	brcmf_abort_scanning(cfg);
	clear_bit(BRCMF_VIF_STATUS_READY, &vif->sme_state);
	mutex_unlock(&cfg->usr_sync);
}
