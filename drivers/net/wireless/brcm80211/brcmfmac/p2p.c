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
#define P2P_AF_TX_MAX_RETRY		5
#define P2P_AF_MAX_WAIT_TIME		2000
#define P2P_INVALID_CHANNEL		-1
#define P2P_CHANNEL_SYNC_RETRY		5
#define P2P_AF_FRM_SCAN_MAX_WAIT	1500

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
 * @max_tx_retry: max tx retry count if tx no ack.
 * @mpc_onoff: To make sure to send successfully action frame, we have to
 *             turn off mpc  0: off, 1: on,  (-1): do nothing
 */
struct brcmf_config_af_params {
	s32 max_tx_retry;
	s32 mpc_onoff;
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

/**
 * brcmf_p2p_set_firmware() - prepare firmware for peer-to-peer operation.
 *
 * @p2p: P2P specific data.
 */
static int brcmf_p2p_set_firmware(struct brcmf_p2p_info *p2p)
{
	struct net_device *ndev = cfg_to_ndev(p2p->cfg);
	u8 null_eth_addr[] = { 0, 0, 0, 0, 0, 0 };
	s32 ret = 0;

	brcmf_fil_iovar_int_set(netdev_priv(ndev), "apsta", 1);

	/* In case of COB type, firmware has default mac address
	 * After Initializing firmware, we have to set current mac address to
	 * firmware for P2P device address
	 */
	ret = brcmf_fil_iovar_data_set(netdev_priv(ndev), "p2p_da_override",
				       null_eth_addr, sizeof(null_eth_addr));
	if (ret)
		brcmf_err("failed to update device address ret %d\n", ret);

	return ret;
}

/**
 * brcmf_p2p_generate_bss_mac() - derive mac addresses for P2P.
 *
 * @p2p: P2P specific data.
 *
 * P2P needs mac addresses for P2P device and interface. These are
 * derived from the primary net device, ie. the permanent ethernet
 * address of the device.
 */
static void brcmf_p2p_generate_bss_mac(struct brcmf_p2p_info *p2p)
{
	/* Generate the P2P Device Address.  This consists of the device's
	 * primary MAC address with the locally administered bit set.
	 */
	memcpy(p2p->dev_addr, p2p->cfg->pub->mac, ETH_ALEN);
	p2p->dev_addr[0] |= 0x02;

	/* Generate the P2P Interface Address.  If the discovery and connection
	 * BSSCFGs need to simultaneously co-exist, then this address must be
	 * different from the P2P Device Address, but also locally administered.
	 */
	memcpy(p2p->int_addr, p2p->dev_addr, ETH_ALEN);
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
 * brcmf_p2p_init_discovery() - enable discovery in the firmware.
 *
 * @p2p: P2P specific data.
 *
 * Configures the firmware to allow P2P peer discovery. Creates the
 * virtual interface and consequently the P2P device for it.
 */
static s32 brcmf_p2p_init_discovery(struct brcmf_p2p_info *p2p)
{
	struct net_device *ndev = cfg_to_ndev(p2p->cfg);
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	struct p2p_bss *bss_dev;
	s32 index;
	s32 ret;

	brcmf_dbg(TRACE, "enter\n");

	bss_dev = &p2p->bss_idx[P2PAPI_BSSCFG_DEVICE];
	if (bss_dev->vif != NULL) {
		brcmf_dbg(INFO, "do nothing, already initialized\n");
		return 0;
	}

	/* Enable P2P Discovery in the firmware */
	ret = brcmf_fil_iovar_int_set(netdev_priv(ndev), "p2p_disc", 1);
	if (ret < 0) {
		brcmf_err("set discover error\n");
		return ret;
	}

	/* obtain bsscfg index for P2P discovery */
	ret = brcmf_fil_iovar_int_get(netdev_priv(ndev), "p2p_dev", &index);
	if (ret < 0) {
		brcmf_err("retrieving discover bsscfg index failed\n");
		return ret;
	}

	/*
	 * need brcmf_if for setting the discovery state.
	 */
	ifp = kzalloc(sizeof(*vif->ifp), GFP_KERNEL);
	if (!ifp) {
		brcmf_err("could not create discovery if\n");
		return -ENOMEM;
	}

	/* set required fields */
	ifp->drvr = p2p->cfg->pub;
	ifp->ifidx = 0;
	ifp->bssidx = index;

	/* Set the initial discovery state to SCAN */
	ret = brcmf_p2p_set_discover_state(ifp, WL_P2P_DISC_ST_SCAN, 0, 0);

	if (ret != 0) {
		brcmf_err("unable to set WL_P2P_DISC_ST_SCAN\n");
		(void)brcmf_fil_iovar_int_set(netdev_priv(ndev), "p2p_disc", 0);
		kfree(ifp);
		return ret;
	}

	/* create a vif for it */
	vif = brcmf_alloc_vif(p2p->cfg, NL80211_IFTYPE_P2P_DEVICE, false);
	if (IS_ERR(vif)) {
		brcmf_err("could not create discovery vif\n");
		kfree(ifp);
		return PTR_ERR(vif);
	}

	vif->ifp = ifp;
	ifp->vif = vif;
	bss_dev->vif = vif;

	return 0;
}

/**
 * brcmf_p2p_deinit_discovery() - disable P2P device discovery.
 *
 * @p2p: P2P specific data.
 *
 * Resets the discovery state and disables it in firmware. The virtual
 * interface and P2P device are freed.
 */
static s32 brcmf_p2p_deinit_discovery(struct brcmf_p2p_info *p2p)
{
	struct net_device *ndev = cfg_to_ndev(p2p->cfg);
	struct brcmf_if *ifp;
	struct p2p_bss *bss_dev;
	brcmf_dbg(TRACE, "enter\n");

	bss_dev = &p2p->bss_idx[P2PAPI_BSSCFG_DEVICE];
	ifp = bss_dev->vif->ifp;

	/* Set the discovery state to SCAN */
	(void)brcmf_p2p_set_discover_state(ifp, WL_P2P_DISC_ST_SCAN, 0, 0);

	/* Disable P2P discovery in the firmware */
	(void)brcmf_fil_iovar_int_set(netdev_priv(ndev), "p2p_disc", 0);

	/* remove discovery interface */
	brcmf_free_vif(bss_dev->vif);
	bss_dev->vif = NULL;
	kfree(ifp);

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
	if (vif) {
		brcmf_dbg(INFO, "DISCOVERY init already done\n");
		goto exit;
	}

	ret = brcmf_p2p_init_discovery(p2p);
	if (ret < 0) {
		brcmf_err("init discovery error %d\n", ret);
		goto exit;
	}

	vif = p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif;

	/*
	 * Set wsec to any non-zero value in the discovery bsscfg
	 * to ensure our P2P probe responses have the privacy bit
	 * set in the 802.11 WPA IE. Some peer devices may not
	 * initiate WPS with us if this bit is not set.
	 */
	ret = brcmf_fil_bsscfg_int_set(vif->ifp, "wsec", AES_ENABLED);
	if (ret < 0)
		brcmf_err("wsec error %d\n", ret);

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
			       struct net_device *ndev,
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

			chanspecs[i] = channel_to_chanspec(chan);
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
		err = cfg80211_get_p2p_attr(request->ie, request->ie_len,
				      IEEE80211_P2P_ATTR_LISTEN_CHANNEL,
				      &p2p->listen_channel, 1);
		if (err < 0)
			return err;

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
	struct brcmf_cfg80211_vif *vif;
	s32 err;
	u16 chanspec;

	brcmf_dbg(TRACE, "Enter, channel: %d, duration ms (%d)\n",
		  ieee80211_frequency_to_channel(channel->center_freq),
		  duration);

	*cookie = 0;
	err = brcmf_p2p_enable_discovery(p2p);
	if (err)
		goto exit;

	chanspec = channel_to_chanspec(channel);
	vif = p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif;
	err = brcmf_p2p_set_discover_state(vif->ifp, WL_P2P_DISC_ST_LISTEN,
					   chanspec, (u16)duration);
	if (err)
		goto exit;

	memcpy(&p2p->remain_on_channel, channel,
	       sizeof(p2p->remain_on_channel));

	set_bit(BRCMF_P2P_STATUS_REMAIN_ON_CHANNEL, &p2p->status);

exit:
	cfg80211_ready_on_channel(wdev, *cookie, channel, duration, GFP_KERNEL);
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
	if (test_and_clear_bit(BRCMF_P2P_STATUS_REMAIN_ON_CHANNEL,
			       &p2p->status))
		cfg80211_remain_on_channel_expired(&ifp->vif->wdev, 0,
						   &p2p->remain_on_channel,
						   GFP_KERNEL);
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
	struct wireless_dev *wdev;
	u32 mgmt_frame_len = e->datalen - sizeof(struct brcmf_rx_mgmt_data);
	struct brcmf_rx_mgmt_data *rxframe = (struct brcmf_rx_mgmt_data *)data;
	u16 chanspec = be16_to_cpu(rxframe->chanspec);
	struct ieee80211_mgmt *mgmt_frame;
	s32 err;
	s32 freq;
	u16 mgmt_type;

	/* Check if wpa_supplicant has registered for this frame */
	brcmf_dbg(INFO, "ifp->vif->mgmt_rx_reg %04x\n", ifp->vif->mgmt_rx_reg);
	mgmt_type = (IEEE80211_STYPE_ACTION & IEEE80211_FCTL_STYPE) >> 4;
	if ((ifp->vif->mgmt_rx_reg & BIT(mgmt_type)) == 0)
		return 0;

	brcmf_p2p_print_actframe(false, (u8 *)(rxframe + 1), mgmt_frame_len);

	mgmt_frame = kzalloc(offsetof(struct ieee80211_mgmt, u) +
			     mgmt_frame_len, GFP_KERNEL);
	if (!mgmt_frame) {
		brcmf_err("No memory available for action frame\n");
		return -ENOMEM;
	}
	memcpy(mgmt_frame->da, ifp->mac_addr, ETH_ALEN);
	err = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_BSSID, mgmt_frame->bssid,
				     ETH_ALEN);
	if (err < 0)
		brcmf_err("BRCMF_C_GET_BSSID error %d\n", err);
	memcpy(mgmt_frame->sa, e->addr, ETH_ALEN);
	mgmt_frame->frame_control = cpu_to_le16(IEEE80211_STYPE_ACTION);
	memcpy(&mgmt_frame->u, (u8 *)(rxframe + 1), mgmt_frame_len);
	mgmt_frame_len += offsetof(struct ieee80211_mgmt, u);

	freq = ieee80211_channel_to_frequency(CHSPEC_CHANNEL(chanspec),
					      CHSPEC_IS2G(chanspec) ?
					      IEEE80211_BAND_2GHZ :
					      IEEE80211_BAND_5GHZ);
	wdev = ifp->ndev->ieee80211_ptr;
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

	brcmf_dbg(INFO, "Enter: status %d\n", e->status);

	if (e->status == BRCMF_E_STATUS_SUCCESS)
		set_bit(BRCMF_P2P_STATUS_ACTION_TX_COMPLETED, &p2p->status);
	else
		set_bit(BRCMF_P2P_STATUS_ACTION_TX_NOACK, &p2p->status);
	/* for now complete the receiver process here !! */
	complete(&p2p->send_af_done);

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

	clear_bit(BRCMF_P2P_STATUS_ACTION_TX_COMPLETED, &p2p->status);
	clear_bit(BRCMF_P2P_STATUS_ACTION_TX_NOACK, &p2p->status);

	vif = p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif;
	err = brcmf_fil_bsscfg_data_set(vif->ifp, "actframe", af_params,
					sizeof(*af_params));
	if (err) {
		brcmf_err(" sending action frame has failed\n");
		goto exit;
	}

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

	action_frame = &af_params->action_frame;
	act_frm = (struct brcmf_p2p_pub_act_frame *)(action_frame->data);

	switch (act_frm->subtype) {
	case P2P_PAF_GON_REQ:
		brcmf_dbg(TRACE, "P2P: GO_NEG_PHASE status set\n");
		set_bit(BRCMF_P2P_STATUS_GO_NEG_PHASE, &p2p->status);
		config_af_params->mpc_onoff = 0;
		p2p->next_af_subtype = act_frm->subtype + 1;
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
		break;
	case P2P_PAF_INVITE_REQ:
		p2p->next_af_subtype = act_frm->subtype + 1;
		/* increase dwell time */
		af_params->dwell_time = cpu_to_le32(P2P_AF_MED_DWELL_TIME);
		break;
	case P2P_PAF_INVITE_RSP:
		/* minimize dwell time */
		af_params->dwell_time = cpu_to_le32(P2P_AF_MIN_DWELL_TIME);
		break;
	case P2P_PAF_DEVDIS_REQ:
		p2p->next_af_subtype = act_frm->subtype + 1;
		/* maximize dwell time to wait for RESP frame */
		af_params->dwell_time = cpu_to_le32(P2P_AF_LONG_DWELL_TIME);
		break;
	case P2P_PAF_DEVDIS_RSP:
		/* minimize dwell time */
		af_params->dwell_time = cpu_to_le32(P2P_AF_MIN_DWELL_TIME);
		break;
	case P2P_PAF_PROVDIS_REQ:
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
	struct brcmf_fil_action_frame_le *action_frame;
	struct brcmf_config_af_params config_af_params;
	u16 action_frame_len;
	bool ack = false;
	u8 category;
	u8 action;
	s32 tx_retry;

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

	/* config parameters */
	config_af_params.max_tx_retry = P2P_AF_TX_MAX_RETRY;
	config_af_params.mpc_onoff = -1;

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

	/* if scan is ongoing, abort current scan. */
	if (test_bit(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status))
		brcmf_abort_scanning(cfg);

	/* To make sure to send successfully action frame, turn off mpc */
	if (config_af_params.mpc_onoff == 0)
		brcmf_set_mpc(ndev, 0);

	/* if failed, retry it. tx_retry_max value is configure by .... */
	tx_retry = 0;
	while ((ack == false) && (tx_retry < config_af_params.max_tx_retry)) {
		ack = !brcmf_p2p_tx_action_frame(p2p, af_params);
		tx_retry++;
	}
	if (ack == false)
		brcmf_err("Failed to send Action Frame(retry %d)\n", tx_retry);

exit:
	/* if all done, turn mpc on again */
	if (config_af_params.mpc_onoff == 1)
		brcmf_set_mpc(ndev, 1);

	return ack;
}


/**
 * brcmf_p2p_attach() - attach for P2P.
 *
 * @cfg: driver private data for cfg80211 interface.
 */
void brcmf_p2p_attach(struct brcmf_cfg80211_info *cfg,
		      struct brcmf_cfg80211_vif *vif)
{
	struct brcmf_p2p_info *p2p;

	p2p = &cfg->p2p;

	p2p->cfg = cfg;
	p2p->bss_idx[P2PAPI_BSSCFG_PRIMARY].vif = vif;
	brcmf_p2p_generate_bss_mac(p2p);
	brcmf_p2p_set_firmware(p2p);
	init_completion(&p2p->send_af_done);
}

/**
 * brcmf_p2p_detach() - detach P2P.
 *
 * @p2p: P2P specific data.
 */
void brcmf_p2p_detach(struct brcmf_p2p_info *p2p)
{
	if (p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif != NULL) {
		brcmf_p2p_cancel_remain_on_channel(
			p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif->ifp);
		brcmf_p2p_deinit_discovery(p2p);
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
	struct brcmf_fil_chan_info_le ci;
	s32 err;

	ifp = p2p->bss_idx[P2PAPI_BSSCFG_PRIMARY].vif->ifp;

	*chanspec = 11 & WL_CHANSPEC_CHAN_MASK;

	err = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_CHANNEL, &ci, sizeof(ci));
	if (!err) {
		*chanspec = le32_to_cpu(ci.hw_channel) & WL_CHANSPEC_CHAN_MASK;
		if (*chanspec < CH_MAX_2G_CHANNEL)
			*chanspec |= WL_CHANSPEC_BAND_2G;
		else
			*chanspec |= WL_CHANSPEC_BAND_5G;
	}
	*chanspec |= WL_CHANSPEC_BW_20 | WL_CHANSPEC_CTL_SB_NONE;
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
	brcmf_notify_escan_complete(cfg, vif->ifp->ndev, true, true);
	vif = p2p->bss_idx[P2PAPI_BSSCFG_CONNECTION].vif;
	if (!vif) {
		brcmf_err("vif for P2PAPI_BSSCFG_CONNECTION does not exist\n");
		return -EPERM;
	}
	brcmf_set_mpc(vif->ifp->ndev, 0);

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
 * brcmf_p2p_add_vif() - create a new P2P virtual interface.
 *
 * @wiphy: wiphy device of new interface.
 * @name: name of the new interface.
 * @type: nl80211 interface type.
 * @flags: TBD
 * @params: TBD
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
	brcmf_cfg80211_vif_complete(cfg);
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
	brcmf_free_vif(vif);
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
	default:
		return -ENOTSUPP;
		break;
	}

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
	brcmf_free_vif(vif);
	cfg->p2p.bss_idx[P2PAPI_BSSCFG_CONNECTION].vif = NULL;

	return err;
}
