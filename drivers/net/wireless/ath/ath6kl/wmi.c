/*
 * Copyright (c) 2004-2011 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/ip.h>
#include "core.h"
#include "debug.h"

static int ath6kl_wmi_sync_point(struct wmi *wmi);

static const s32 wmi_rate_tbl[][2] = {
	/* {W/O SGI, with SGI} */
	{1000, 1000},
	{2000, 2000},
	{5500, 5500},
	{11000, 11000},
	{6000, 6000},
	{9000, 9000},
	{12000, 12000},
	{18000, 18000},
	{24000, 24000},
	{36000, 36000},
	{48000, 48000},
	{54000, 54000},
	{6500, 7200},
	{13000, 14400},
	{19500, 21700},
	{26000, 28900},
	{39000, 43300},
	{52000, 57800},
	{58500, 65000},
	{65000, 72200},
	{13500, 15000},
	{27000, 30000},
	{40500, 45000},
	{54000, 60000},
	{81000, 90000},
	{108000, 120000},
	{121500, 135000},
	{135000, 150000},
	{0, 0}
};

/* 802.1d to AC mapping. Refer pg 57 of WMM-test-plan-v1.2 */
static const u8 up_to_ac[] = {
	WMM_AC_BE,
	WMM_AC_BK,
	WMM_AC_BK,
	WMM_AC_BE,
	WMM_AC_VI,
	WMM_AC_VI,
	WMM_AC_VO,
	WMM_AC_VO,
};

void ath6kl_wmi_set_control_ep(struct wmi *wmi, enum htc_endpoint_id ep_id)
{
	if (WARN_ON(ep_id == ENDPOINT_UNUSED || ep_id >= ENDPOINT_MAX))
		return;

	wmi->ep_id = ep_id;
}

enum htc_endpoint_id ath6kl_wmi_get_control_ep(struct wmi *wmi)
{
	return wmi->ep_id;
}

/*  Performs DIX to 802.3 encapsulation for transmit packets.
 *  Assumes the entire DIX header is contigous and that there is
 *  enough room in the buffer for a 802.3 mac header and LLC+SNAP headers.
 */
int ath6kl_wmi_dix_2_dot3(struct wmi *wmi, struct sk_buff *skb)
{
	struct ath6kl_llc_snap_hdr *llc_hdr;
	struct ethhdr *eth_hdr;
	size_t new_len;
	__be16 type;
	u8 *datap;
	u16 size;

	if (WARN_ON(skb == NULL))
		return -EINVAL;

	size = sizeof(struct ath6kl_llc_snap_hdr) + sizeof(struct wmi_data_hdr);
	if (skb_headroom(skb) < size)
		return -ENOMEM;

	eth_hdr = (struct ethhdr *) skb->data;
	type = eth_hdr->h_proto;

	if (!is_ethertype(be16_to_cpu(type))) {
		ath6kl_dbg(ATH6KL_DBG_WMI,
			"%s: pkt is already in 802.3 format\n", __func__);
		return 0;
	}

	new_len = skb->len - sizeof(*eth_hdr) + sizeof(*llc_hdr);

	skb_push(skb, sizeof(struct ath6kl_llc_snap_hdr));
	datap = skb->data;

	eth_hdr->h_proto = cpu_to_be16(new_len);

	memcpy(datap, eth_hdr, sizeof(*eth_hdr));

	llc_hdr = (struct ath6kl_llc_snap_hdr *)(datap + sizeof(*eth_hdr));
	llc_hdr->dsap = 0xAA;
	llc_hdr->ssap = 0xAA;
	llc_hdr->cntl = 0x03;
	llc_hdr->org_code[0] = 0x0;
	llc_hdr->org_code[1] = 0x0;
	llc_hdr->org_code[2] = 0x0;
	llc_hdr->eth_type = type;

	return 0;
}

static int ath6kl_wmi_meta_add(struct wmi *wmi, struct sk_buff *skb,
			       u8 *version, void *tx_meta_info)
{
	struct wmi_tx_meta_v1 *v1;
	struct wmi_tx_meta_v2 *v2;

	if (WARN_ON(skb == NULL || version == NULL))
		return -EINVAL;

	switch (*version) {
	case WMI_META_VERSION_1:
		skb_push(skb, WMI_MAX_TX_META_SZ);
		v1 = (struct wmi_tx_meta_v1 *) skb->data;
		v1->pkt_id = 0;
		v1->rate_plcy_id = 0;
		*version = WMI_META_VERSION_1;
		break;
	case WMI_META_VERSION_2:
		skb_push(skb, WMI_MAX_TX_META_SZ);
		v2 = (struct wmi_tx_meta_v2 *) skb->data;
		memcpy(v2, (struct wmi_tx_meta_v2 *) tx_meta_info,
		       sizeof(struct wmi_tx_meta_v2));
		break;
	}

	return 0;
}

int ath6kl_wmi_data_hdr_add(struct wmi *wmi, struct sk_buff *skb,
			    u8 msg_type, bool more_data,
			    enum wmi_data_hdr_data_type data_type,
			    u8 meta_ver, void *tx_meta_info)
{
	struct wmi_data_hdr *data_hdr;
	int ret;

	if (WARN_ON(skb == NULL))
		return -EINVAL;

	ret = ath6kl_wmi_meta_add(wmi, skb, &meta_ver, tx_meta_info);
	if (ret)
		return ret;

	skb_push(skb, sizeof(struct wmi_data_hdr));

	data_hdr = (struct wmi_data_hdr *)skb->data;
	memset(data_hdr, 0, sizeof(struct wmi_data_hdr));

	data_hdr->info = msg_type << WMI_DATA_HDR_MSG_TYPE_SHIFT;
	data_hdr->info |= data_type << WMI_DATA_HDR_DATA_TYPE_SHIFT;

	if (more_data)
		data_hdr->info |=
		    WMI_DATA_HDR_MORE_MASK << WMI_DATA_HDR_MORE_SHIFT;

	data_hdr->info2 = cpu_to_le16(meta_ver << WMI_DATA_HDR_META_SHIFT);
	data_hdr->info3 = 0;

	return 0;
}

static u8 ath6kl_wmi_determine_user_priority(u8 *pkt, u32 layer2_pri)
{
	struct iphdr *ip_hdr = (struct iphdr *) pkt;
	u8 ip_pri;

	/*
	 * Determine IPTOS priority
	 *
	 * IP-TOS - 8bits
	 *          : DSCP(6-bits) ECN(2-bits)
	 *          : DSCP - P2 P1 P0 X X X
	 * where (P2 P1 P0) form 802.1D
	 */
	ip_pri = ip_hdr->tos >> 5;
	ip_pri &= 0x7;

	if ((layer2_pri & 0x7) > ip_pri)
		return (u8) layer2_pri & 0x7;
	else
		return ip_pri;
}

int ath6kl_wmi_implicit_create_pstream(struct wmi *wmi, struct sk_buff *skb,
				       u32 layer2_priority, bool wmm_enabled,
				       u8 *ac)
{
	struct wmi_data_hdr *data_hdr;
	struct ath6kl_llc_snap_hdr *llc_hdr;
	struct wmi_create_pstream_cmd cmd;
	u32 meta_size, hdr_size;
	u16 ip_type = IP_ETHERTYPE;
	u8 stream_exist, usr_pri;
	u8 traffic_class = WMM_AC_BE;
	u8 *datap;

	if (WARN_ON(skb == NULL))
		return -EINVAL;

	datap = skb->data;
	data_hdr = (struct wmi_data_hdr *) datap;

	meta_size = ((le16_to_cpu(data_hdr->info2) >> WMI_DATA_HDR_META_SHIFT) &
		     WMI_DATA_HDR_META_MASK) ? WMI_MAX_TX_META_SZ : 0;

	if (!wmm_enabled) {
		/* If WMM is disabled all traffic goes as BE traffic */
		usr_pri = 0;
	} else {
		hdr_size = sizeof(struct ethhdr);

		llc_hdr = (struct ath6kl_llc_snap_hdr *)(datap +
							 sizeof(struct
								wmi_data_hdr) +
							 meta_size + hdr_size);

		if (llc_hdr->eth_type == htons(ip_type)) {
			/*
			 * Extract the endpoint info from the TOS field
			 * in the IP header.
			 */
			usr_pri =
			   ath6kl_wmi_determine_user_priority(((u8 *) llc_hdr) +
					sizeof(struct ath6kl_llc_snap_hdr),
					layer2_priority);
		} else
			usr_pri = layer2_priority & 0x7;
	}

	/* workaround for WMM S5 */
	if ((wmi->traffic_class == WMM_AC_VI) &&
	    ((usr_pri == 5) || (usr_pri == 4)))
		usr_pri = 1;

	/* Convert user priority to traffic class */
	traffic_class = up_to_ac[usr_pri & 0x7];

	wmi_data_hdr_set_up(data_hdr, usr_pri);

	spin_lock_bh(&wmi->lock);
	stream_exist = wmi->fat_pipe_exist;
	spin_unlock_bh(&wmi->lock);

	if (!(stream_exist & (1 << traffic_class))) {
		memset(&cmd, 0, sizeof(cmd));
		cmd.traffic_class = traffic_class;
		cmd.user_pri = usr_pri;
		cmd.inactivity_int =
			cpu_to_le32(WMI_IMPLICIT_PSTREAM_INACTIVITY_INT);
		/* Implicit streams are created with TSID 0xFF */
		cmd.tsid = WMI_IMPLICIT_PSTREAM;
		ath6kl_wmi_create_pstream_cmd(wmi, &cmd);
	}

	*ac = traffic_class;

	return 0;
}

int ath6kl_wmi_dot11_hdr_remove(struct wmi *wmi, struct sk_buff *skb)
{
	struct ieee80211_hdr_3addr *pwh, wh;
	struct ath6kl_llc_snap_hdr *llc_hdr;
	struct ethhdr eth_hdr;
	u32 hdr_size;
	u8 *datap;
	__le16 sub_type;

	if (WARN_ON(skb == NULL))
		return -EINVAL;

	datap = skb->data;
	pwh = (struct ieee80211_hdr_3addr *) datap;

	sub_type = pwh->frame_control & cpu_to_le16(IEEE80211_FCTL_STYPE);

	memcpy((u8 *) &wh, datap, sizeof(struct ieee80211_hdr_3addr));

	/* Strip off the 802.11 header */
	if (sub_type == cpu_to_le16(IEEE80211_STYPE_QOS_DATA)) {
		hdr_size = roundup(sizeof(struct ieee80211_qos_hdr),
				   sizeof(u32));
		skb_pull(skb, hdr_size);
	} else if (sub_type == cpu_to_le16(IEEE80211_STYPE_DATA))
		skb_pull(skb, sizeof(struct ieee80211_hdr_3addr));

	datap = skb->data;
	llc_hdr = (struct ath6kl_llc_snap_hdr *)(datap);

	eth_hdr.h_proto = llc_hdr->eth_type;
	memset(eth_hdr.h_dest, 0, sizeof(eth_hdr.h_dest));
	memset(eth_hdr.h_source, 0, sizeof(eth_hdr.h_source));

	switch ((le16_to_cpu(wh.frame_control)) &
		(IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS)) {
	case 0:
		memcpy(eth_hdr.h_dest, wh.addr1, ETH_ALEN);
		memcpy(eth_hdr.h_source, wh.addr2, ETH_ALEN);
		break;
	case IEEE80211_FCTL_TODS:
		memcpy(eth_hdr.h_dest, wh.addr3, ETH_ALEN);
		memcpy(eth_hdr.h_source, wh.addr2, ETH_ALEN);
		break;
	case IEEE80211_FCTL_FROMDS:
		memcpy(eth_hdr.h_dest, wh.addr1, ETH_ALEN);
		memcpy(eth_hdr.h_source, wh.addr3, ETH_ALEN);
		break;
	case IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS:
		break;
	}

	skb_pull(skb, sizeof(struct ath6kl_llc_snap_hdr));
	skb_push(skb, sizeof(eth_hdr));

	datap = skb->data;

	memcpy(datap, &eth_hdr, sizeof(eth_hdr));

	return 0;
}

/*
 * Performs 802.3 to DIX encapsulation for received packets.
 * Assumes the entire 802.3 header is contigous.
 */
int ath6kl_wmi_dot3_2_dix(struct sk_buff *skb)
{
	struct ath6kl_llc_snap_hdr *llc_hdr;
	struct ethhdr eth_hdr;
	u8 *datap;

	if (WARN_ON(skb == NULL))
		return -EINVAL;

	datap = skb->data;

	memcpy(&eth_hdr, datap, sizeof(eth_hdr));

	llc_hdr = (struct ath6kl_llc_snap_hdr *) (datap + sizeof(eth_hdr));
	eth_hdr.h_proto = llc_hdr->eth_type;

	skb_pull(skb, sizeof(struct ath6kl_llc_snap_hdr));
	datap = skb->data;

	memcpy(datap, &eth_hdr, sizeof(eth_hdr));

	return 0;
}

int ath6kl_wmi_data_hdr_remove(struct wmi *wmi, struct sk_buff *skb)
{
	if (WARN_ON(skb == NULL))
		return -EINVAL;

	skb_pull(skb, sizeof(struct wmi_data_hdr));

	return 0;
}

void ath6kl_wmi_iterate_nodes(struct wmi *wmi,
			      void (*f) (void *arg, struct bss *),
			      void *arg)
{
	wlan_iterate_nodes(&wmi->scan_table, f, arg);
}

static void ath6kl_wmi_convert_bssinfo_hdr2_to_hdr(struct sk_buff *skb,
						   u8 *datap)
{
	struct wmi_bss_info_hdr2 bih2;
	struct wmi_bss_info_hdr *bih;

	memcpy(&bih2, datap, sizeof(struct wmi_bss_info_hdr2));

	skb_push(skb, 4);
	bih = (struct wmi_bss_info_hdr *) skb->data;

	bih->ch = bih2.ch;
	bih->frame_type = bih2.frame_type;
	bih->snr = bih2.snr;
	bih->rssi = a_cpu_to_sle16(bih2.snr - 95);
	bih->ie_mask = cpu_to_le32(le16_to_cpu(bih2.ie_mask));
	memcpy(bih->bssid, bih2.bssid, ETH_ALEN);
}

static int ath6kl_wmi_tx_complete_event_rx(u8 *datap, int len)
{
	struct tx_complete_msg_v1 *msg_v1;
	struct wmi_tx_complete_event *evt;
	int index;
	u16 size;

	evt = (struct wmi_tx_complete_event *) datap;

	ath6kl_dbg(ATH6KL_DBG_WMI, "comp: %d %d %d\n",
		   evt->num_msg, evt->msg_len, evt->msg_type);

	if (!AR_DBG_LVL_CHECK(ATH6KL_DBG_WMI))
		return 0;

	for (index = 0; index < evt->num_msg; index++) {
		size = sizeof(struct wmi_tx_complete_event) +
		    (index * sizeof(struct tx_complete_msg_v1));
		msg_v1 = (struct tx_complete_msg_v1 *)(datap + size);

		ath6kl_dbg(ATH6KL_DBG_WMI, "msg: %d %d %d %d\n",
			   msg_v1->status, msg_v1->pkt_id,
			   msg_v1->rate_idx, msg_v1->ack_failures);
	}

	return 0;
}

static inline struct sk_buff *ath6kl_wmi_get_new_buf(u32 size)
{
	struct sk_buff *skb;

	skb = ath6kl_buf_alloc(size);
	if (!skb)
		return NULL;

	skb_put(skb, size);
	if (size)
		memset(skb->data, 0, size);

	return skb;
}

/* Send a "simple" wmi command -- one with no arguments */
static int ath6kl_wmi_simple_cmd(struct wmi *wmi, enum wmi_cmd_id cmd_id)
{
	struct sk_buff *skb;
	int ret;

	skb = ath6kl_wmi_get_new_buf(0);
	if (!skb)
		return -ENOMEM;

	ret = ath6kl_wmi_cmd_send(wmi, skb, cmd_id, NO_SYNC_WMIFLAG);

	return ret;
}

static int ath6kl_wmi_ready_event_rx(struct wmi *wmi, u8 *datap, int len)
{
	struct wmi_ready_event_2 *ev = (struct wmi_ready_event_2 *) datap;

	if (len < sizeof(struct wmi_ready_event_2))
		return -EINVAL;

	wmi->ready = true;
	ath6kl_ready_event(wmi->parent_dev, ev->mac_addr,
			   le32_to_cpu(ev->sw_version),
			   le32_to_cpu(ev->abi_version));

	return 0;
}

static int ath6kl_wmi_connect_event_rx(struct wmi *wmi, u8 *datap, int len)
{
	struct wmi_connect_event *ev;
	u8 *pie, *peie;

	if (len < sizeof(struct wmi_connect_event))
		return -EINVAL;

	ev = (struct wmi_connect_event *) datap;

	ath6kl_dbg(ATH6KL_DBG_WMI, "%s: freq %d bssid %pM\n",
		   __func__, ev->ch, ev->bssid);

	memcpy(wmi->bssid, ev->bssid, ETH_ALEN);

	/* Start of assoc rsp IEs */
	pie = ev->assoc_info + ev->beacon_ie_len +
	      ev->assoc_req_len + (sizeof(u16) * 3); /* capinfo, status, aid */

	/* End of assoc rsp IEs */
	peie = ev->assoc_info + ev->beacon_ie_len + ev->assoc_req_len +
	    ev->assoc_resp_len;

	while (pie < peie) {
		switch (*pie) {
		case WLAN_EID_VENDOR_SPECIFIC:
			if (pie[1] > 3 && pie[2] == 0x00 && pie[3] == 0x50 &&
			    pie[4] == 0xf2 && pie[5] == WMM_OUI_TYPE) {
				/* WMM OUT (00:50:F2) */
				if (pie[1] > 5
				    && pie[6] == WMM_PARAM_OUI_SUBTYPE)
					wmi->is_wmm_enabled = true;
			}
			break;
		}

		if (wmi->is_wmm_enabled)
			break;

		pie += pie[1] + 2;
	}

	ath6kl_connect_event(wmi->parent_dev, le16_to_cpu(ev->ch), ev->bssid,
			     le16_to_cpu(ev->listen_intvl),
			     le16_to_cpu(ev->beacon_intvl),
			     le32_to_cpu(ev->nw_type),
			     ev->beacon_ie_len, ev->assoc_req_len,
			     ev->assoc_resp_len, ev->assoc_info);

	return 0;
}

static int ath6kl_wmi_disconnect_event_rx(struct wmi *wmi, u8 *datap, int len)
{
	struct wmi_disconnect_event *ev;
	wmi->traffic_class = 100;

	if (len < sizeof(struct wmi_disconnect_event))
		return -EINVAL;

	ev = (struct wmi_disconnect_event *) datap;
	memset(wmi->bssid, 0, sizeof(wmi->bssid));

	wmi->is_wmm_enabled = false;
	wmi->pair_crypto_type = NONE_CRYPT;
	wmi->grp_crypto_type = NONE_CRYPT;

	ath6kl_disconnect_event(wmi->parent_dev, ev->disconn_reason,
				ev->bssid, ev->assoc_resp_len, ev->assoc_info,
				le16_to_cpu(ev->proto_reason_status));

	return 0;
}

static int ath6kl_wmi_peer_node_event_rx(struct wmi *wmi, u8 *datap, int len)
{
	struct wmi_peer_node_event *ev;

	if (len < sizeof(struct wmi_peer_node_event))
		return -EINVAL;

	ev = (struct wmi_peer_node_event *) datap;

	if (ev->event_code == PEER_NODE_JOIN_EVENT)
		ath6kl_dbg(ATH6KL_DBG_WMI, "joined node with mac addr: %pM\n",
			   ev->peer_mac_addr);
	else if (ev->event_code == PEER_NODE_LEAVE_EVENT)
		ath6kl_dbg(ATH6KL_DBG_WMI, "left node with mac addr: %pM\n",
			   ev->peer_mac_addr);

	return 0;
}

static int ath6kl_wmi_tkip_micerr_event_rx(struct wmi *wmi, u8 *datap, int len)
{
	struct wmi_tkip_micerr_event *ev;

	if (len < sizeof(struct wmi_tkip_micerr_event))
		return -EINVAL;

	ev = (struct wmi_tkip_micerr_event *) datap;

	ath6kl_tkip_micerr_event(wmi->parent_dev, ev->key_id, ev->is_mcast);

	return 0;
}

static int ath6kl_wlan_parse_beacon(u8 *buf, int frame_len,
				    struct ath6kl_common_ie *cie)
{
	u8 *frm, *efrm;
	u8 elemid_ssid = false;

	frm = buf;
	efrm = (u8 *) (frm + frame_len);

	/*
	 * beacon/probe response frame format
	 *  [8] time stamp
	 *  [2] beacon interval
	 *  [2] capability information
	 *  [tlv] ssid
	 *  [tlv] supported rates
	 *  [tlv] country information
	 *  [tlv] parameter set (FH/DS)
	 *  [tlv] erp information
	 *  [tlv] extended supported rates
	 *  [tlv] WMM
	 *  [tlv] WPA or RSN
	 *  [tlv] Atheros Advanced Capabilities
	 */
	if ((efrm - frm) < 12)
		return -EINVAL;

	memset(cie, 0, sizeof(*cie));

	cie->ie_tstamp = frm;
	frm += 8;
	cie->ie_beaconInt = *(u16 *) frm;
	frm += 2;
	cie->ie_capInfo = *(u16 *) frm;
	frm += 2;
	cie->ie_chan = 0;

	while (frm < efrm) {
		switch (*frm) {
		case WLAN_EID_SSID:
			if (!elemid_ssid) {
				cie->ie_ssid = frm;
				elemid_ssid = true;
			}
			break;
		case WLAN_EID_SUPP_RATES:
			cie->ie_rates = frm;
			break;
		case WLAN_EID_COUNTRY:
			cie->ie_country = frm;
			break;
		case WLAN_EID_FH_PARAMS:
			break;
		case WLAN_EID_DS_PARAMS:
			cie->ie_chan = frm[2];
			break;
		case WLAN_EID_TIM:
			cie->ie_tim = frm;
			break;
		case WLAN_EID_IBSS_PARAMS:
			break;
		case WLAN_EID_EXT_SUPP_RATES:
			cie->ie_xrates = frm;
			break;
		case WLAN_EID_ERP_INFO:
			if (frm[1] != 1)
				return -EINVAL;

			cie->ie_erp = frm[2];
			break;
		case WLAN_EID_RSN:
			cie->ie_rsn = frm;
			break;
		case WLAN_EID_HT_CAPABILITY:
			cie->ie_htcap = frm;
			break;
		case WLAN_EID_HT_INFORMATION:
			cie->ie_htop = frm;
			break;
		case WLAN_EID_VENDOR_SPECIFIC:
			if (frm[1] > 3 && frm[2] == 0x00 && frm[3] == 0x50 &&
			    frm[4] == 0xf2) {
				/* OUT Type (00:50:F2) */

				if (frm[5] == WPA_OUI_TYPE) {
					/* WPA OUT */
					cie->ie_wpa = frm;
				} else if (frm[5] == WMM_OUI_TYPE) {
					/* WMM OUT */
					cie->ie_wmm = frm;
				} else if (frm[5] == WSC_OUT_TYPE) {
					/* WSC OUT */
					cie->ie_wsc = frm;
				}

			} else if (frm[1] > 3 && frm[2] == 0x00
				   && frm[3] == 0x03 && frm[4] == 0x7f
				   && frm[5] == ATH_OUI_TYPE) {
				/* Atheros OUI (00:03:7f) */
				cie->ie_ath = frm;
			}
			break;
		default:
			break;
		}
		frm += frm[1] + 2;
	}

	if ((cie->ie_rates == NULL)
	    || (cie->ie_rates[1] > ATH6KL_RATE_MAXSIZE))
		return -EINVAL;

	if ((cie->ie_ssid == NULL)
	    || (cie->ie_ssid[1] > IEEE80211_MAX_SSID_LEN))
		return -EINVAL;

	return 0;
}

static int ath6kl_wmi_bssinfo_event_rx(struct wmi *wmi, u8 *datap, int len)
{
	struct bss *bss = NULL;
	struct wmi_bss_info_hdr *bih;
	u8 cached_ssid_len = 0;
	u8 cached_ssid[IEEE80211_MAX_SSID_LEN] = { 0 };
	u8 beacon_ssid_len = 0;
	u8 *buf, *ie_ssid;
	u8 *ni_buf;
	int buf_len;

	int ret;

	if (len <= sizeof(struct wmi_bss_info_hdr))
		return -EINVAL;

	bih = (struct wmi_bss_info_hdr *) datap;
	bss = wlan_find_node(&wmi->scan_table, bih->bssid);

	if (a_sle16_to_cpu(bih->rssi) > 0) {
		if (bss == NULL)
			return 0;
		else
			bih->rssi = a_cpu_to_sle16(bss->ni_rssi);
	}

	buf = datap + sizeof(struct wmi_bss_info_hdr);
	len -= sizeof(struct wmi_bss_info_hdr);

	ath6kl_dbg(ATH6KL_DBG_WMI,
		   "bss info evt - ch %u, rssi %02x, bssid \"%pM\"\n",
		   bih->ch, a_sle16_to_cpu(bih->rssi), bih->bssid);

	if (bss != NULL) {
		/*
		 * Free up the node. We are about to allocate a new node.
		 * In case of hidden AP, beacon will not have ssid,
		 * but a directed probe response will have it,
		 * so cache the probe-resp-ssid if already present.
		 */
		if (wmi->is_probe_ssid && (bih->frame_type == BEACON_FTYPE)) {
			ie_ssid = bss->ni_cie.ie_ssid;
			if (ie_ssid && (ie_ssid[1] <= IEEE80211_MAX_SSID_LEN) &&
			    (ie_ssid[2] != 0)) {
				cached_ssid_len = ie_ssid[1];
				memcpy(cached_ssid, ie_ssid + 2,
				       cached_ssid_len);
			}
		}

		/*
		 * Use the current average rssi of associated AP base on
		 * assumption
		 *   1. Most os with GUI will update RSSI by
		 *      ath6kl_wmi_get_stats_cmd() periodically.
		 *   2. ath6kl_wmi_get_stats_cmd(..) will be called when calling
		 *      ath6kl_wmi_startscan_cmd(...)
		 * The average value of RSSI give end-user better feeling for
		 * instance value of scan result. It also sync up RSSI info
		 * in GUI between scan result and RSSI signal icon.
		 */
		if (memcmp(wmi->bssid, bih->bssid, ETH_ALEN) == 0) {
			bih->rssi = a_cpu_to_sle16(bss->ni_rssi);
			bih->snr = bss->ni_snr;
		}

		wlan_node_reclaim(&wmi->scan_table, bss);
	}

	/*
	 * beacon/probe response frame format
	 *  [8] time stamp
	 *  [2] beacon interval
	 *  [2] capability information
	 *  [tlv] ssid
	 */
	beacon_ssid_len = buf[SSID_IE_LEN_INDEX];

	/*
	 * If ssid is cached for this hidden AP, then change
	 * buffer len accordingly.
	 */
	if (wmi->is_probe_ssid && (bih->frame_type == BEACON_FTYPE) &&
	    (cached_ssid_len != 0) &&
	    (beacon_ssid_len == 0 || (cached_ssid_len > beacon_ssid_len &&
				      buf[SSID_IE_LEN_INDEX + 1] == 0))) {

		len += (cached_ssid_len - beacon_ssid_len);
	}

	bss = wlan_node_alloc(len);
	if (!bss)
		return -ENOMEM;

	bss->ni_snr = bih->snr;
	bss->ni_rssi = a_sle16_to_cpu(bih->rssi);

	if (WARN_ON(!bss->ni_buf))
		return -EINVAL;

	/*
	 * In case of hidden AP, beacon will not have ssid,
	 * but a directed probe response will have it,
	 * so place the cached-ssid(probe-resp) in the bss info.
	 */
	if (wmi->is_probe_ssid && (bih->frame_type == BEACON_FTYPE) &&
	    (cached_ssid_len != 0) &&
	    (beacon_ssid_len == 0 || (beacon_ssid_len &&
				      buf[SSID_IE_LEN_INDEX + 1] == 0))) {
		ni_buf = bss->ni_buf;
		buf_len = len;

		/*
		 * Copy the first 14 bytes:
		 * time-stamp(8), beacon-interval(2),
		 * cap-info(2), ssid-id(1), ssid-len(1).
		 */
		memcpy(ni_buf, buf, SSID_IE_LEN_INDEX + 1);

		ni_buf[SSID_IE_LEN_INDEX] = cached_ssid_len;
		ni_buf += (SSID_IE_LEN_INDEX + 1);

		buf += (SSID_IE_LEN_INDEX + 1);
		buf_len -= (SSID_IE_LEN_INDEX + 1);

		memcpy(ni_buf, cached_ssid, cached_ssid_len);
		ni_buf += cached_ssid_len;

		buf += beacon_ssid_len;
		buf_len -= beacon_ssid_len;

		if (cached_ssid_len > beacon_ssid_len)
			buf_len -= (cached_ssid_len - beacon_ssid_len);

		memcpy(ni_buf, buf, buf_len);
	} else
		memcpy(bss->ni_buf, buf, len);

	bss->ni_framelen = len;

	ret = ath6kl_wlan_parse_beacon(bss->ni_buf, len, &bss->ni_cie);
	if (ret) {
		wlan_node_free(bss);
		return -EINVAL;
	}

	/*
	 * Update the frequency in ie_chan, overwriting of channel number
	 * which is done in ath6kl_wlan_parse_beacon
	 */
	bss->ni_cie.ie_chan = le16_to_cpu(bih->ch);
	wlan_setup_node(&wmi->scan_table, bss, bih->bssid);

	return 0;
}

static int ath6kl_wmi_opt_frame_event_rx(struct wmi *wmi, u8 *datap, int len)
{
	struct bss *bss;
	struct wmi_opt_rx_info_hdr *bih;
	u8 *buf;

	if (len <= sizeof(struct wmi_opt_rx_info_hdr))
		return -EINVAL;

	bih = (struct wmi_opt_rx_info_hdr *) datap;
	buf = datap + sizeof(struct wmi_opt_rx_info_hdr);
	len -= sizeof(struct wmi_opt_rx_info_hdr);

	ath6kl_dbg(ATH6KL_DBG_WMI, "opt frame event %2.2x:%2.2x\n",
		   bih->bssid[4], bih->bssid[5]);

	bss = wlan_find_node(&wmi->scan_table, bih->bssid);
	if (bss != NULL) {
		/* Free up the node. We are about to allocate a new node. */
		wlan_node_reclaim(&wmi->scan_table, bss);
	}

	bss = wlan_node_alloc(len);
	if (!bss)
		return -ENOMEM;

	bss->ni_snr = bih->snr;
	bss->ni_cie.ie_chan = le16_to_cpu(bih->ch);

	if (WARN_ON(!bss->ni_buf))
		return -EINVAL;

	memcpy(bss->ni_buf, buf, len);
	wlan_setup_node(&wmi->scan_table, bss, bih->bssid);

	return 0;
}

/* Inactivity timeout of a fatpipe(pstream) at the target */
static int ath6kl_wmi_pstream_timeout_event_rx(struct wmi *wmi, u8 *datap,
					       int len)
{
	struct wmi_pstream_timeout_event *ev;

	if (len < sizeof(struct wmi_pstream_timeout_event))
		return -EINVAL;

	ev = (struct wmi_pstream_timeout_event *) datap;

	/*
	 * When the pstream (fat pipe == AC) timesout, it means there were
	 * no thinStreams within this pstream & it got implicitly created
	 * due to data flow on this AC. We start the inactivity timer only
	 * for implicitly created pstream. Just reset the host state.
	 */
	spin_lock_bh(&wmi->lock);
	wmi->stream_exist_for_ac[ev->traffic_class] = 0;
	wmi->fat_pipe_exist &= ~(1 << ev->traffic_class);
	spin_unlock_bh(&wmi->lock);

	/* Indicate inactivity to driver layer for this fatpipe (pstream) */
	ath6kl_indicate_tx_activity(wmi->parent_dev, ev->traffic_class, false);

	return 0;
}

static int ath6kl_wmi_bitrate_reply_rx(struct wmi *wmi, u8 *datap, int len)
{
	struct wmi_bit_rate_reply *reply;
	s32 rate;
	u32 sgi, index;

	if (len < sizeof(struct wmi_bit_rate_reply))
		return -EINVAL;

	reply = (struct wmi_bit_rate_reply *) datap;

	ath6kl_dbg(ATH6KL_DBG_WMI, "rateindex %d\n", reply->rate_index);

	if (reply->rate_index == (s8) RATE_AUTO) {
		rate = RATE_AUTO;
	} else {
		index = reply->rate_index & 0x7f;
		sgi = (reply->rate_index & 0x80) ? 1 : 0;
		rate = wmi_rate_tbl[index][sgi];
	}

	ath6kl_wakeup_event(wmi->parent_dev);

	return 0;
}

static int ath6kl_wmi_ratemask_reply_rx(struct wmi *wmi, u8 *datap, int len)
{
	if (len < sizeof(struct wmi_fix_rates_reply))
		return -EINVAL;

	ath6kl_wakeup_event(wmi->parent_dev);

	return 0;
}

static int ath6kl_wmi_ch_list_reply_rx(struct wmi *wmi, u8 *datap, int len)
{
	if (len < sizeof(struct wmi_channel_list_reply))
		return -EINVAL;

	ath6kl_wakeup_event(wmi->parent_dev);

	return 0;
}

static int ath6kl_wmi_tx_pwr_reply_rx(struct wmi *wmi, u8 *datap, int len)
{
	struct wmi_tx_pwr_reply *reply;

	if (len < sizeof(struct wmi_tx_pwr_reply))
		return -EINVAL;

	reply = (struct wmi_tx_pwr_reply *) datap;
	ath6kl_txpwr_rx_evt(wmi->parent_dev, reply->dbM);

	return 0;
}

static int ath6kl_wmi_keepalive_reply_rx(struct wmi *wmi, u8 *datap, int len)
{
	if (len < sizeof(struct wmi_get_keepalive_cmd))
		return -EINVAL;

	ath6kl_wakeup_event(wmi->parent_dev);

	return 0;
}

static int ath6kl_wmi_scan_complete_rx(struct wmi *wmi, u8 *datap, int len)
{
	struct wmi_scan_complete_event *ev;

	ev = (struct wmi_scan_complete_event *) datap;

	if (a_sle32_to_cpu(ev->status) == 0)
		wlan_refresh_inactive_nodes(&wmi->scan_table);

	ath6kl_scan_complete_evt(wmi->parent_dev, a_sle32_to_cpu(ev->status));
	wmi->is_probe_ssid = false;

	return 0;
}

/*
 * Target is reporting a programming error.  This is for
 * developer aid only.  Target only checks a few common violations
 * and it is responsibility of host to do all error checking.
 * Behavior of target after wmi error event is undefined.
 * A reset is recommended.
 */
static int ath6kl_wmi_error_event_rx(struct wmi *wmi, u8 *datap, int len)
{
	const char *type = "unknown error";
	struct wmi_cmd_error_event *ev;
	ev = (struct wmi_cmd_error_event *) datap;

	switch (ev->err_code) {
	case INVALID_PARAM:
		type = "invalid parameter";
		break;
	case ILLEGAL_STATE:
		type = "invalid state";
		break;
	case INTERNAL_ERROR:
		type = "internal error";
		break;
	}

	ath6kl_dbg(ATH6KL_DBG_WMI, "programming error, cmd=%d %s\n",
		   ev->cmd_id, type);

	return 0;
}

static int ath6kl_wmi_stats_event_rx(struct wmi *wmi, u8 *datap, int len)
{
	ath6kl_tgt_stats_event(wmi->parent_dev, datap, len);

	return 0;
}

static u8 ath6kl_wmi_get_upper_threshold(s16 rssi,
					 struct sq_threshold_params *sq_thresh,
					 u32 size)
{
	u32 index;
	u8 threshold = (u8) sq_thresh->upper_threshold[size - 1];

	/* The list is already in sorted order. Get the next lower value */
	for (index = 0; index < size; index++) {
		if (rssi < sq_thresh->upper_threshold[index]) {
			threshold = (u8) sq_thresh->upper_threshold[index];
			break;
		}
	}

	return threshold;
}

static u8 ath6kl_wmi_get_lower_threshold(s16 rssi,
					 struct sq_threshold_params *sq_thresh,
					 u32 size)
{
	u32 index;
	u8 threshold = (u8) sq_thresh->lower_threshold[size - 1];

	/* The list is already in sorted order. Get the next lower value */
	for (index = 0; index < size; index++) {
		if (rssi > sq_thresh->lower_threshold[index]) {
			threshold = (u8) sq_thresh->lower_threshold[index];
			break;
		}
	}

	return threshold;
}

static int ath6kl_wmi_send_rssi_threshold_params(struct wmi *wmi,
			struct wmi_rssi_threshold_params_cmd *rssi_cmd)
{
	struct sk_buff *skb;
	struct wmi_rssi_threshold_params_cmd *cmd;

	skb = ath6kl_wmi_get_new_buf(sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_rssi_threshold_params_cmd *) skb->data;
	memcpy(cmd, rssi_cmd, sizeof(struct wmi_rssi_threshold_params_cmd));

	return ath6kl_wmi_cmd_send(wmi, skb, WMI_RSSI_THRESHOLD_PARAMS_CMDID,
				   NO_SYNC_WMIFLAG);
}

static int ath6kl_wmi_rssi_threshold_event_rx(struct wmi *wmi, u8 *datap,
					      int len)
{
	struct wmi_rssi_threshold_event *reply;
	struct wmi_rssi_threshold_params_cmd cmd;
	struct sq_threshold_params *sq_thresh;
	enum wmi_rssi_threshold_val new_threshold;
	u8 upper_rssi_threshold, lower_rssi_threshold;
	s16 rssi;
	int ret;

	if (len < sizeof(struct wmi_rssi_threshold_event))
		return -EINVAL;

	reply = (struct wmi_rssi_threshold_event *) datap;
	new_threshold = (enum wmi_rssi_threshold_val) reply->range;
	rssi = a_sle16_to_cpu(reply->rssi);

	sq_thresh = &wmi->sq_threshld[SIGNAL_QUALITY_METRICS_RSSI];

	/*
	 * Identify the threshold breached and communicate that to the app.
	 * After that install a new set of thresholds based on the signal
	 * quality reported by the target
	 */
	if (new_threshold) {
		/* Upper threshold breached */
		if (rssi < sq_thresh->upper_threshold[0]) {
			ath6kl_dbg(ATH6KL_DBG_WMI,
				"spurious upper rssi threshold event: %d\n",
				rssi);
		} else if ((rssi < sq_thresh->upper_threshold[1]) &&
			   (rssi >= sq_thresh->upper_threshold[0])) {
			new_threshold = WMI_RSSI_THRESHOLD1_ABOVE;
		} else if ((rssi < sq_thresh->upper_threshold[2]) &&
			   (rssi >= sq_thresh->upper_threshold[1])) {
			new_threshold = WMI_RSSI_THRESHOLD2_ABOVE;
		} else if ((rssi < sq_thresh->upper_threshold[3]) &&
			   (rssi >= sq_thresh->upper_threshold[2])) {
			new_threshold = WMI_RSSI_THRESHOLD3_ABOVE;
		} else if ((rssi < sq_thresh->upper_threshold[4]) &&
			   (rssi >= sq_thresh->upper_threshold[3])) {
			new_threshold = WMI_RSSI_THRESHOLD4_ABOVE;
		} else if ((rssi < sq_thresh->upper_threshold[5]) &&
			   (rssi >= sq_thresh->upper_threshold[4])) {
			new_threshold = WMI_RSSI_THRESHOLD5_ABOVE;
		} else if (rssi >= sq_thresh->upper_threshold[5]) {
			new_threshold = WMI_RSSI_THRESHOLD6_ABOVE;
		}
	} else {
		/* Lower threshold breached */
		if (rssi > sq_thresh->lower_threshold[0]) {
			ath6kl_dbg(ATH6KL_DBG_WMI,
				"spurious lower rssi threshold event: %d %d\n",
				rssi, sq_thresh->lower_threshold[0]);
		} else if ((rssi > sq_thresh->lower_threshold[1]) &&
			   (rssi <= sq_thresh->lower_threshold[0])) {
			new_threshold = WMI_RSSI_THRESHOLD6_BELOW;
		} else if ((rssi > sq_thresh->lower_threshold[2]) &&
			   (rssi <= sq_thresh->lower_threshold[1])) {
			new_threshold = WMI_RSSI_THRESHOLD5_BELOW;
		} else if ((rssi > sq_thresh->lower_threshold[3]) &&
			   (rssi <= sq_thresh->lower_threshold[2])) {
			new_threshold = WMI_RSSI_THRESHOLD4_BELOW;
		} else if ((rssi > sq_thresh->lower_threshold[4]) &&
			   (rssi <= sq_thresh->lower_threshold[3])) {
			new_threshold = WMI_RSSI_THRESHOLD3_BELOW;
		} else if ((rssi > sq_thresh->lower_threshold[5]) &&
			   (rssi <= sq_thresh->lower_threshold[4])) {
			new_threshold = WMI_RSSI_THRESHOLD2_BELOW;
		} else if (rssi <= sq_thresh->lower_threshold[5]) {
			new_threshold = WMI_RSSI_THRESHOLD1_BELOW;
		}
	}

	/* Calculate and install the next set of thresholds */
	lower_rssi_threshold = ath6kl_wmi_get_lower_threshold(rssi, sq_thresh,
				       sq_thresh->lower_threshold_valid_count);
	upper_rssi_threshold = ath6kl_wmi_get_upper_threshold(rssi, sq_thresh,
				       sq_thresh->upper_threshold_valid_count);

	/* Issue a wmi command to install the thresholds */
	cmd.thresh_above1_val = a_cpu_to_sle16(upper_rssi_threshold);
	cmd.thresh_below1_val = a_cpu_to_sle16(lower_rssi_threshold);
	cmd.weight = sq_thresh->weight;
	cmd.poll_time = cpu_to_le32(sq_thresh->polling_interval);

	ret = ath6kl_wmi_send_rssi_threshold_params(wmi, &cmd);
	if (ret) {
		ath6kl_err("unable to configure rssi thresholds\n");
		return -EIO;
	}

	return 0;
}

static int ath6kl_wmi_cac_event_rx(struct wmi *wmi, u8 *datap, int len)
{
	struct wmi_cac_event *reply;
	struct ieee80211_tspec_ie *ts;
	u16 active_tsids, tsinfo;
	u8 tsid, index;
	u8 ts_id;

	if (len < sizeof(struct wmi_cac_event))
		return -EINVAL;

	reply = (struct wmi_cac_event *) datap;

	if ((reply->cac_indication == CAC_INDICATION_ADMISSION_RESP) &&
	    (reply->status_code != IEEE80211_TSPEC_STATUS_ADMISS_ACCEPTED)) {

		ts = (struct ieee80211_tspec_ie *) &(reply->tspec_suggestion);
		tsinfo = le16_to_cpu(ts->tsinfo);
		tsid = (tsinfo >> IEEE80211_WMM_IE_TSPEC_TID_SHIFT) &
			IEEE80211_WMM_IE_TSPEC_TID_MASK;

		ath6kl_wmi_delete_pstream_cmd(wmi, reply->ac, tsid);
	} else if (reply->cac_indication == CAC_INDICATION_NO_RESP) {
		/*
		 * Following assumes that there is only one outstanding
		 * ADDTS request when this event is received
		 */
		spin_lock_bh(&wmi->lock);
		active_tsids = wmi->stream_exist_for_ac[reply->ac];
		spin_unlock_bh(&wmi->lock);

		for (index = 0; index < sizeof(active_tsids) * 8; index++) {
			if ((active_tsids >> index) & 1)
				break;
		}
		if (index < (sizeof(active_tsids) * 8))
			ath6kl_wmi_delete_pstream_cmd(wmi, reply->ac, index);
	}

	/*
	 * Clear active tsids and Add missing handling
	 * for delete qos stream from AP
	 */
	else if (reply->cac_indication == CAC_INDICATION_DELETE) {

		ts = (struct ieee80211_tspec_ie *) &(reply->tspec_suggestion);
		tsinfo = le16_to_cpu(ts->tsinfo);
		ts_id = ((tsinfo >> IEEE80211_WMM_IE_TSPEC_TID_SHIFT) &
			 IEEE80211_WMM_IE_TSPEC_TID_MASK);

		spin_lock_bh(&wmi->lock);
		wmi->stream_exist_for_ac[reply->ac] &= ~(1 << ts_id);
		active_tsids = wmi->stream_exist_for_ac[reply->ac];
		spin_unlock_bh(&wmi->lock);

		/* Indicate stream inactivity to driver layer only if all tsids
		 * within this AC are deleted.
		 */
		if (!active_tsids) {
			ath6kl_indicate_tx_activity(wmi->parent_dev, reply->ac,
						    false);
			wmi->fat_pipe_exist &= ~(1 << reply->ac);
		}
	}

	return 0;
}

static int ath6kl_wmi_send_snr_threshold_params(struct wmi *wmi,
			struct wmi_snr_threshold_params_cmd *snr_cmd)
{
	struct sk_buff *skb;
	struct wmi_snr_threshold_params_cmd *cmd;

	skb = ath6kl_wmi_get_new_buf(sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_snr_threshold_params_cmd *) skb->data;
	memcpy(cmd, snr_cmd, sizeof(struct wmi_snr_threshold_params_cmd));

	return ath6kl_wmi_cmd_send(wmi, skb, WMI_SNR_THRESHOLD_PARAMS_CMDID,
				   NO_SYNC_WMIFLAG);
}

static int ath6kl_wmi_snr_threshold_event_rx(struct wmi *wmi, u8 *datap,
					     int len)
{
	struct wmi_snr_threshold_event *reply;
	struct sq_threshold_params *sq_thresh;
	struct wmi_snr_threshold_params_cmd cmd;
	enum wmi_snr_threshold_val new_threshold;
	u8 upper_snr_threshold, lower_snr_threshold;
	s16 snr;
	int ret;

	if (len < sizeof(struct wmi_snr_threshold_event))
		return -EINVAL;

	reply = (struct wmi_snr_threshold_event *) datap;

	new_threshold = (enum wmi_snr_threshold_val) reply->range;
	snr = reply->snr;

	sq_thresh = &wmi->sq_threshld[SIGNAL_QUALITY_METRICS_SNR];

	/*
	 * Identify the threshold breached and communicate that to the app.
	 * After that install a new set of thresholds based on the signal
	 * quality reported by the target.
	 */
	if (new_threshold) {
		/* Upper threshold breached */
		if (snr < sq_thresh->upper_threshold[0]) {
			ath6kl_dbg(ATH6KL_DBG_WMI,
				"spurious upper snr threshold event: %d\n",
				snr);
		} else if ((snr < sq_thresh->upper_threshold[1]) &&
			   (snr >= sq_thresh->upper_threshold[0])) {
			new_threshold = WMI_SNR_THRESHOLD1_ABOVE;
		} else if ((snr < sq_thresh->upper_threshold[2]) &&
			   (snr >= sq_thresh->upper_threshold[1])) {
			new_threshold = WMI_SNR_THRESHOLD2_ABOVE;
		} else if ((snr < sq_thresh->upper_threshold[3]) &&
			   (snr >= sq_thresh->upper_threshold[2])) {
			new_threshold = WMI_SNR_THRESHOLD3_ABOVE;
		} else if (snr >= sq_thresh->upper_threshold[3]) {
			new_threshold = WMI_SNR_THRESHOLD4_ABOVE;
		}
	} else {
		/* Lower threshold breached */
		if (snr > sq_thresh->lower_threshold[0]) {
			ath6kl_dbg(ATH6KL_DBG_WMI,
				"spurious lower snr threshold event: %d\n",
				sq_thresh->lower_threshold[0]);
		} else if ((snr > sq_thresh->lower_threshold[1]) &&
			   (snr <= sq_thresh->lower_threshold[0])) {
			new_threshold = WMI_SNR_THRESHOLD4_BELOW;
		} else if ((snr > sq_thresh->lower_threshold[2]) &&
			   (snr <= sq_thresh->lower_threshold[1])) {
			new_threshold = WMI_SNR_THRESHOLD3_BELOW;
		} else if ((snr > sq_thresh->lower_threshold[3]) &&
			   (snr <= sq_thresh->lower_threshold[2])) {
			new_threshold = WMI_SNR_THRESHOLD2_BELOW;
		} else if (snr <= sq_thresh->lower_threshold[3]) {
			new_threshold = WMI_SNR_THRESHOLD1_BELOW;
		}
	}

	/* Calculate and install the next set of thresholds */
	lower_snr_threshold = ath6kl_wmi_get_lower_threshold(snr, sq_thresh,
				       sq_thresh->lower_threshold_valid_count);
	upper_snr_threshold = ath6kl_wmi_get_upper_threshold(snr, sq_thresh,
				       sq_thresh->upper_threshold_valid_count);

	/* Issue a wmi command to install the thresholds */
	cmd.thresh_above1_val = upper_snr_threshold;
	cmd.thresh_below1_val = lower_snr_threshold;
	cmd.weight = sq_thresh->weight;
	cmd.poll_time = cpu_to_le32(sq_thresh->polling_interval);

	ath6kl_dbg(ATH6KL_DBG_WMI,
		   "snr: %d, threshold: %d, lower: %d, upper: %d\n",
		   snr, new_threshold,
		   lower_snr_threshold, upper_snr_threshold);

	ret = ath6kl_wmi_send_snr_threshold_params(wmi, &cmd);
	if (ret) {
		ath6kl_err("unable to configure snr threshold\n");
		return -EIO;
	}

	return 0;
}

static int ath6kl_wmi_aplist_event_rx(struct wmi *wmi, u8 *datap, int len)
{
	u16 ap_info_entry_size;
	struct wmi_aplist_event *ev = (struct wmi_aplist_event *) datap;
	struct wmi_ap_info_v1 *ap_info_v1;
	u8 index;

	if (len < sizeof(struct wmi_aplist_event) ||
	    ev->ap_list_ver != APLIST_VER1)
		return -EINVAL;

	ap_info_entry_size = sizeof(struct wmi_ap_info_v1);
	ap_info_v1 = (struct wmi_ap_info_v1 *) ev->ap_list;

	ath6kl_dbg(ATH6KL_DBG_WMI,
		   "number of APs in aplist event: %d\n", ev->num_ap);

	if (len < (int) (sizeof(struct wmi_aplist_event) +
			 (ev->num_ap - 1) * ap_info_entry_size))
		return -EINVAL;

	/* AP list version 1 contents */
	for (index = 0; index < ev->num_ap; index++) {
		ath6kl_dbg(ATH6KL_DBG_WMI, "AP#%d BSSID %pM Channel %d\n",
			   index, ap_info_v1->bssid, ap_info_v1->channel);
		ap_info_v1++;
	}

	return 0;
}

int ath6kl_wmi_cmd_send(struct wmi *wmi, struct sk_buff *skb,
			enum wmi_cmd_id cmd_id, enum wmi_sync_flag sync_flag)
{
	struct wmi_cmd_hdr *cmd_hdr;
	enum htc_endpoint_id ep_id = wmi->ep_id;
	int ret;

	if (WARN_ON(skb == NULL))
		return -EINVAL;

	if (sync_flag >= END_WMIFLAG) {
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	if ((sync_flag == SYNC_BEFORE_WMIFLAG) ||
	    (sync_flag == SYNC_BOTH_WMIFLAG)) {
		/*
		 * Make sure all data currently queued is transmitted before
		 * the cmd execution.  Establish a new sync point.
		 */
		ath6kl_wmi_sync_point(wmi);
	}

	skb_push(skb, sizeof(struct wmi_cmd_hdr));

	cmd_hdr = (struct wmi_cmd_hdr *) skb->data;
	cmd_hdr->cmd_id = cpu_to_le16(cmd_id);
	cmd_hdr->info1 = 0;	/* added for virtual interface */

	/* Only for OPT_TX_CMD, use BE endpoint. */
	if (cmd_id == WMI_OPT_TX_FRAME_CMDID) {
		ret = ath6kl_wmi_data_hdr_add(wmi, skb, OPT_MSGTYPE,
					      false, false, 0, NULL);
		if (ret) {
			dev_kfree_skb(skb);
			return ret;
		}
		ep_id = ath6kl_ac2_endpoint_id(wmi->parent_dev, WMM_AC_BE);
	}

	ath6kl_control_tx(wmi->parent_dev, skb, ep_id);

	if ((sync_flag == SYNC_AFTER_WMIFLAG) ||
	    (sync_flag == SYNC_BOTH_WMIFLAG)) {
		/*
		 * Make sure all new data queued waits for the command to
		 * execute. Establish a new sync point.
		 */
		ath6kl_wmi_sync_point(wmi);
	}

	return 0;
}

int ath6kl_wmi_connect_cmd(struct wmi *wmi, enum network_type nw_type,
			   enum dot11_auth_mode dot11_auth_mode,
			   enum auth_mode auth_mode,
			   enum crypto_type pairwise_crypto,
			   u8 pairwise_crypto_len,
			   enum crypto_type group_crypto,
			   u8 group_crypto_len, int ssid_len, u8 *ssid,
			   u8 *bssid, u16 channel, u32 ctrl_flags)
{
	struct sk_buff *skb;
	struct wmi_connect_cmd *cc;
	int ret;

	wmi->traffic_class = 100;

	if ((pairwise_crypto == NONE_CRYPT) && (group_crypto != NONE_CRYPT))
		return -EINVAL;

	if ((pairwise_crypto != NONE_CRYPT) && (group_crypto == NONE_CRYPT))
		return -EINVAL;

	skb = ath6kl_wmi_get_new_buf(sizeof(struct wmi_connect_cmd));
	if (!skb)
		return -ENOMEM;

	cc = (struct wmi_connect_cmd *) skb->data;

	if (ssid_len)
		memcpy(cc->ssid, ssid, ssid_len);

	cc->ssid_len = ssid_len;
	cc->nw_type = nw_type;
	cc->dot11_auth_mode = dot11_auth_mode;
	cc->auth_mode = auth_mode;
	cc->prwise_crypto_type = pairwise_crypto;
	cc->prwise_crypto_len = pairwise_crypto_len;
	cc->grp_crypto_type = group_crypto;
	cc->grp_crypto_len = group_crypto_len;
	cc->ch = cpu_to_le16(channel);
	cc->ctrl_flags = cpu_to_le32(ctrl_flags);

	if (bssid != NULL)
		memcpy(cc->bssid, bssid, ETH_ALEN);

	wmi->pair_crypto_type = pairwise_crypto;
	wmi->grp_crypto_type = group_crypto;

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_CONNECT_CMDID, NO_SYNC_WMIFLAG);

	return ret;
}

int ath6kl_wmi_reconnect_cmd(struct wmi *wmi, u8 *bssid, u16 channel)
{
	struct sk_buff *skb;
	struct wmi_reconnect_cmd *cc;
	int ret;

	wmi->traffic_class = 100;

	skb = ath6kl_wmi_get_new_buf(sizeof(struct wmi_reconnect_cmd));
	if (!skb)
		return -ENOMEM;

	cc = (struct wmi_reconnect_cmd *) skb->data;
	cc->channel = cpu_to_le16(channel);

	if (bssid != NULL)
		memcpy(cc->bssid, bssid, ETH_ALEN);

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_RECONNECT_CMDID,
				  NO_SYNC_WMIFLAG);

	return ret;
}

int ath6kl_wmi_disconnect_cmd(struct wmi *wmi)
{
	int ret;

	wmi->traffic_class = 100;

	/* Disconnect command does not need to do a SYNC before. */
	ret = ath6kl_wmi_simple_cmd(wmi, WMI_DISCONNECT_CMDID);

	return ret;
}

int ath6kl_wmi_startscan_cmd(struct wmi *wmi, enum wmi_scan_type scan_type,
			     u32 force_fgscan, u32 is_legacy,
			     u32 home_dwell_time, u32 force_scan_interval,
			     s8 num_chan, u16 *ch_list)
{
	struct sk_buff *skb;
	struct wmi_start_scan_cmd *sc;
	s8 size;
	int ret;

	size = sizeof(struct wmi_start_scan_cmd);

	if ((scan_type != WMI_LONG_SCAN) && (scan_type != WMI_SHORT_SCAN))
		return -EINVAL;

	if (num_chan > WMI_MAX_CHANNELS)
		return -EINVAL;

	if (num_chan)
		size += sizeof(u16) * (num_chan - 1);

	skb = ath6kl_wmi_get_new_buf(size);
	if (!skb)
		return -ENOMEM;

	sc = (struct wmi_start_scan_cmd *) skb->data;
	sc->scan_type = scan_type;
	sc->force_fg_scan = cpu_to_le32(force_fgscan);
	sc->is_legacy = cpu_to_le32(is_legacy);
	sc->home_dwell_time = cpu_to_le32(home_dwell_time);
	sc->force_scan_intvl = cpu_to_le32(force_scan_interval);
	sc->num_ch = num_chan;

	if (num_chan)
		memcpy(sc->ch_list, ch_list, num_chan * sizeof(u16));

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_START_SCAN_CMDID,
				  NO_SYNC_WMIFLAG);

	return ret;
}

int ath6kl_wmi_scanparams_cmd(struct wmi *wmi, u16 fg_start_sec,
			      u16 fg_end_sec, u16 bg_sec,
			      u16 minact_chdw_msec, u16 maxact_chdw_msec,
			      u16 pas_chdw_msec, u8 short_scan_ratio,
			      u8 scan_ctrl_flag, u32 max_dfsch_act_time,
			      u16 maxact_scan_per_ssid)
{
	struct sk_buff *skb;
	struct wmi_scan_params_cmd *sc;
	int ret;

	skb = ath6kl_wmi_get_new_buf(sizeof(*sc));
	if (!skb)
		return -ENOMEM;

	sc = (struct wmi_scan_params_cmd *) skb->data;
	sc->fg_start_period = cpu_to_le16(fg_start_sec);
	sc->fg_end_period = cpu_to_le16(fg_end_sec);
	sc->bg_period = cpu_to_le16(bg_sec);
	sc->minact_chdwell_time = cpu_to_le16(minact_chdw_msec);
	sc->maxact_chdwell_time = cpu_to_le16(maxact_chdw_msec);
	sc->pas_chdwell_time = cpu_to_le16(pas_chdw_msec);
	sc->short_scan_ratio = short_scan_ratio;
	sc->scan_ctrl_flags = scan_ctrl_flag;
	sc->max_dfsch_act_time = cpu_to_le32(max_dfsch_act_time);
	sc->maxact_scan_per_ssid = cpu_to_le16(maxact_scan_per_ssid);

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_SET_SCAN_PARAMS_CMDID,
				  NO_SYNC_WMIFLAG);
	return ret;
}

int ath6kl_wmi_bssfilter_cmd(struct wmi *wmi, u8 filter, u32 ie_mask)
{
	struct sk_buff *skb;
	struct wmi_bss_filter_cmd *cmd;
	int ret;

	if (filter >= LAST_BSS_FILTER)
		return -EINVAL;

	skb = ath6kl_wmi_get_new_buf(sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_bss_filter_cmd *) skb->data;
	cmd->bss_filter = filter;
	cmd->ie_mask = cpu_to_le32(ie_mask);

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_SET_BSS_FILTER_CMDID,
				  NO_SYNC_WMIFLAG);
	return ret;
}

int ath6kl_wmi_probedssid_cmd(struct wmi *wmi, u8 index, u8 flag,
			      u8 ssid_len, u8 *ssid)
{
	struct sk_buff *skb;
	struct wmi_probed_ssid_cmd *cmd;
	int ret;

	if (index > MAX_PROBED_SSID_INDEX)
		return -EINVAL;

	if (ssid_len > sizeof(cmd->ssid))
		return -EINVAL;

	if ((flag & (DISABLE_SSID_FLAG | ANY_SSID_FLAG)) && (ssid_len > 0))
		return -EINVAL;

	if ((flag & SPECIFIC_SSID_FLAG) && !ssid_len)
		return -EINVAL;

	if (flag & SPECIFIC_SSID_FLAG)
		wmi->is_probe_ssid = true;

	skb = ath6kl_wmi_get_new_buf(sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_probed_ssid_cmd *) skb->data;
	cmd->entry_index = index;
	cmd->flag = flag;
	cmd->ssid_len = ssid_len;
	memcpy(cmd->ssid, ssid, ssid_len);

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_SET_PROBED_SSID_CMDID,
				  NO_SYNC_WMIFLAG);
	return ret;
}

int ath6kl_wmi_listeninterval_cmd(struct wmi *wmi, u16 listen_interval,
				  u16 listen_beacons)
{
	struct sk_buff *skb;
	struct wmi_listen_int_cmd *cmd;
	int ret;

	skb = ath6kl_wmi_get_new_buf(sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_listen_int_cmd *) skb->data;
	cmd->listen_intvl = cpu_to_le16(listen_interval);
	cmd->num_beacons = cpu_to_le16(listen_beacons);

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_SET_LISTEN_INT_CMDID,
				  NO_SYNC_WMIFLAG);
	return ret;
}

int ath6kl_wmi_powermode_cmd(struct wmi *wmi, u8 pwr_mode)
{
	struct sk_buff *skb;
	struct wmi_power_mode_cmd *cmd;
	int ret;

	skb = ath6kl_wmi_get_new_buf(sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_power_mode_cmd *) skb->data;
	cmd->pwr_mode = pwr_mode;
	wmi->pwr_mode = pwr_mode;

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_SET_POWER_MODE_CMDID,
				  NO_SYNC_WMIFLAG);
	return ret;
}

int ath6kl_wmi_pmparams_cmd(struct wmi *wmi, u16 idle_period,
			    u16 ps_poll_num, u16 dtim_policy,
			    u16 tx_wakeup_policy, u16 num_tx_to_wakeup,
			    u16 ps_fail_event_policy)
{
	struct sk_buff *skb;
	struct wmi_power_params_cmd *pm;
	int ret;

	skb = ath6kl_wmi_get_new_buf(sizeof(*pm));
	if (!skb)
		return -ENOMEM;

	pm = (struct wmi_power_params_cmd *)skb->data;
	pm->idle_period = cpu_to_le16(idle_period);
	pm->pspoll_number = cpu_to_le16(ps_poll_num);
	pm->dtim_policy = cpu_to_le16(dtim_policy);
	pm->tx_wakeup_policy = cpu_to_le16(tx_wakeup_policy);
	pm->num_tx_to_wakeup = cpu_to_le16(num_tx_to_wakeup);
	pm->ps_fail_event_policy = cpu_to_le16(ps_fail_event_policy);

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_SET_POWER_PARAMS_CMDID,
				  NO_SYNC_WMIFLAG);
	return ret;
}

int ath6kl_wmi_disctimeout_cmd(struct wmi *wmi, u8 timeout)
{
	struct sk_buff *skb;
	struct wmi_disc_timeout_cmd *cmd;
	int ret;

	skb = ath6kl_wmi_get_new_buf(sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_disc_timeout_cmd *) skb->data;
	cmd->discon_timeout = timeout;

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_SET_DISC_TIMEOUT_CMDID,
				  NO_SYNC_WMIFLAG);
	return ret;
}

int ath6kl_wmi_addkey_cmd(struct wmi *wmi, u8 key_index,
			  enum crypto_type key_type,
			  u8 key_usage, u8 key_len,
			  u8 *key_rsc, u8 *key_material,
			  u8 key_op_ctrl, u8 *mac_addr,
			  enum wmi_sync_flag sync_flag)
{
	struct sk_buff *skb;
	struct wmi_add_cipher_key_cmd *cmd;
	int ret;

	if ((key_index > WMI_MAX_KEY_INDEX) || (key_len > WMI_MAX_KEY_LEN) ||
	    (key_material == NULL))
		return -EINVAL;

	if ((WEP_CRYPT != key_type) && (NULL == key_rsc))
		return -EINVAL;

	skb = ath6kl_wmi_get_new_buf(sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_add_cipher_key_cmd *) skb->data;
	cmd->key_index = key_index;
	cmd->key_type = key_type;
	cmd->key_usage = key_usage;
	cmd->key_len = key_len;
	memcpy(cmd->key, key_material, key_len);

	if (key_rsc != NULL)
		memcpy(cmd->key_rsc, key_rsc, sizeof(cmd->key_rsc));

	cmd->key_op_ctrl = key_op_ctrl;

	if (mac_addr)
		memcpy(cmd->key_mac_addr, mac_addr, ETH_ALEN);

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_ADD_CIPHER_KEY_CMDID,
				  sync_flag);

	return ret;
}

int ath6kl_wmi_add_krk_cmd(struct wmi *wmi, u8 *krk)
{
	struct sk_buff *skb;
	struct wmi_add_krk_cmd *cmd;
	int ret;

	skb = ath6kl_wmi_get_new_buf(sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_add_krk_cmd *) skb->data;
	memcpy(cmd->krk, krk, WMI_KRK_LEN);

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_ADD_KRK_CMDID, NO_SYNC_WMIFLAG);

	return ret;
}

int ath6kl_wmi_deletekey_cmd(struct wmi *wmi, u8 key_index)
{
	struct sk_buff *skb;
	struct wmi_delete_cipher_key_cmd *cmd;
	int ret;

	if (key_index > WMI_MAX_KEY_INDEX)
		return -EINVAL;

	skb = ath6kl_wmi_get_new_buf(sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_delete_cipher_key_cmd *) skb->data;
	cmd->key_index = key_index;

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_DELETE_CIPHER_KEY_CMDID,
				  NO_SYNC_WMIFLAG);

	return ret;
}

int ath6kl_wmi_setpmkid_cmd(struct wmi *wmi, const u8 *bssid,
			    const u8 *pmkid, bool set)
{
	struct sk_buff *skb;
	struct wmi_setpmkid_cmd *cmd;
	int ret;

	if (bssid == NULL)
		return -EINVAL;

	if (set && pmkid == NULL)
		return -EINVAL;

	skb = ath6kl_wmi_get_new_buf(sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_setpmkid_cmd *) skb->data;
	memcpy(cmd->bssid, bssid, ETH_ALEN);
	if (set) {
		memcpy(cmd->pmkid, pmkid, sizeof(cmd->pmkid));
		cmd->enable = PMKID_ENABLE;
	} else {
		memset(cmd->pmkid, 0, sizeof(cmd->pmkid));
		cmd->enable = PMKID_DISABLE;
	}

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_SET_PMKID_CMDID,
				  NO_SYNC_WMIFLAG);

	return ret;
}

static int ath6kl_wmi_data_sync_send(struct wmi *wmi, struct sk_buff *skb,
			      enum htc_endpoint_id ep_id)
{
	struct wmi_data_hdr *data_hdr;
	int ret;

	if (WARN_ON(skb == NULL || ep_id == wmi->ep_id))
		return -EINVAL;

	skb_push(skb, sizeof(struct wmi_data_hdr));

	data_hdr = (struct wmi_data_hdr *) skb->data;
	data_hdr->info = SYNC_MSGTYPE << WMI_DATA_HDR_MSG_TYPE_SHIFT;
	data_hdr->info3 = 0;

	ret = ath6kl_control_tx(wmi->parent_dev, skb, ep_id);

	return ret;
}

static int ath6kl_wmi_sync_point(struct wmi *wmi)
{
	struct sk_buff *skb;
	struct wmi_sync_cmd *cmd;
	struct wmi_data_sync_bufs data_sync_bufs[WMM_NUM_AC];
	enum htc_endpoint_id ep_id;
	u8 index, num_pri_streams = 0;
	int ret = 0;

	memset(data_sync_bufs, 0, sizeof(data_sync_bufs));

	spin_lock_bh(&wmi->lock);

	for (index = 0; index < WMM_NUM_AC; index++) {
		if (wmi->fat_pipe_exist & (1 << index)) {
			num_pri_streams++;
			data_sync_bufs[num_pri_streams - 1].traffic_class =
			    index;
		}
	}

	spin_unlock_bh(&wmi->lock);

	skb = ath6kl_wmi_get_new_buf(sizeof(*cmd));
	if (!skb) {
		ret = -ENOMEM;
		goto free_skb;
	}

	cmd = (struct wmi_sync_cmd *) skb->data;

	/*
	 * In the SYNC cmd sent on the control Ep, send a bitmap
	 * of the data eps on which the Data Sync will be sent
	 */
	cmd->data_sync_map = wmi->fat_pipe_exist;

	for (index = 0; index < num_pri_streams; index++) {
		data_sync_bufs[index].skb = ath6kl_buf_alloc(0);
		if (data_sync_bufs[index].skb == NULL) {
			ret = -ENOMEM;
			break;
		}
	}

	/*
	 * If buffer allocation for any of the dataSync fails,
	 * then do not send the Synchronize cmd on the control ep
	 */
	if (ret)
		goto free_skb;

	/*
	 * Send sync cmd followed by sync data messages on all
	 * endpoints being used
	 */
	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_SYNCHRONIZE_CMDID,
				  NO_SYNC_WMIFLAG);

	if (ret)
		goto free_skb;

	/* cmd buffer sent, we no longer own it */
	skb = NULL;

	for (index = 0; index < num_pri_streams; index++) {

		if (WARN_ON(!data_sync_bufs[index].skb))
			break;

		ep_id = ath6kl_ac2_endpoint_id(wmi->parent_dev,
					       data_sync_bufs[index].
					       traffic_class);
		ret =
		    ath6kl_wmi_data_sync_send(wmi, data_sync_bufs[index].skb,
					      ep_id);

		if (ret)
			break;

		data_sync_bufs[index].skb = NULL;
	}

free_skb:
	/* free up any resources left over (possibly due to an error) */
	if (skb)
		dev_kfree_skb(skb);

	for (index = 0; index < num_pri_streams; index++) {
		if (data_sync_bufs[index].skb != NULL) {
			dev_kfree_skb((struct sk_buff *)data_sync_bufs[index].
				      skb);
		}
	}

	return ret;
}

int ath6kl_wmi_create_pstream_cmd(struct wmi *wmi,
				  struct wmi_create_pstream_cmd *params)
{
	struct sk_buff *skb;
	struct wmi_create_pstream_cmd *cmd;
	u8 fatpipe_exist_for_ac = 0;
	s32 min_phy = 0;
	s32 nominal_phy = 0;
	int ret;

	if (!((params->user_pri < 8) &&
	      (params->user_pri <= 0x7) &&
	      (up_to_ac[params->user_pri & 0x7] == params->traffic_class) &&
	      (params->traffic_direc == UPLINK_TRAFFIC ||
	       params->traffic_direc == DNLINK_TRAFFIC ||
	       params->traffic_direc == BIDIR_TRAFFIC) &&
	      (params->traffic_type == TRAFFIC_TYPE_APERIODIC ||
	       params->traffic_type == TRAFFIC_TYPE_PERIODIC) &&
	      (params->voice_psc_cap == DISABLE_FOR_THIS_AC ||
	       params->voice_psc_cap == ENABLE_FOR_THIS_AC ||
	       params->voice_psc_cap == ENABLE_FOR_ALL_AC) &&
	      (params->tsid == WMI_IMPLICIT_PSTREAM ||
	       params->tsid <= WMI_MAX_THINSTREAM))) {
		return -EINVAL;
	}

	/*
	 * Check nominal PHY rate is >= minimalPHY,
	 * so that DUT can allow TSRS IE
	 */

	/* Get the physical rate (units of bps) */
	min_phy = ((le32_to_cpu(params->min_phy_rate) / 1000) / 1000);

	/* Check minimal phy < nominal phy rate */
	if (params->nominal_phy >= min_phy) {
		/* unit of 500 kbps */
		nominal_phy = (params->nominal_phy * 1000) / 500;
		ath6kl_dbg(ATH6KL_DBG_WMI,
			   "TSRS IE enabled::MinPhy %x->NominalPhy ===> %x\n",
			   min_phy, nominal_phy);

		params->nominal_phy = nominal_phy;
	} else {
		params->nominal_phy = 0;
	}

	skb = ath6kl_wmi_get_new_buf(sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	ath6kl_dbg(ATH6KL_DBG_WMI,
		   "sending create_pstream_cmd: ac=%d  tsid:%d\n",
		   params->traffic_class, params->tsid);

	cmd = (struct wmi_create_pstream_cmd *) skb->data;
	memcpy(cmd, params, sizeof(*cmd));

	/* This is an implicitly created Fat pipe */
	if ((u32) params->tsid == (u32) WMI_IMPLICIT_PSTREAM) {
		spin_lock_bh(&wmi->lock);
		fatpipe_exist_for_ac = (wmi->fat_pipe_exist &
					(1 << params->traffic_class));
		wmi->fat_pipe_exist |= (1 << params->traffic_class);
		spin_unlock_bh(&wmi->lock);
	} else {
		/* explicitly created thin stream within a fat pipe */
		spin_lock_bh(&wmi->lock);
		fatpipe_exist_for_ac = (wmi->fat_pipe_exist &
					(1 << params->traffic_class));
		wmi->stream_exist_for_ac[params->traffic_class] |=
		    (1 << params->tsid);
		/*
		 * If a thinstream becomes active, the fat pipe automatically
		 * becomes active
		 */
		wmi->fat_pipe_exist |= (1 << params->traffic_class);
		spin_unlock_bh(&wmi->lock);
	}

	/*
	 * Indicate activty change to driver layer only if this is the
	 * first TSID to get created in this AC explicitly or an implicit
	 * fat pipe is getting created.
	 */
	if (!fatpipe_exist_for_ac)
		ath6kl_indicate_tx_activity(wmi->parent_dev,
					    params->traffic_class, true);

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_CREATE_PSTREAM_CMDID,
				  NO_SYNC_WMIFLAG);
	return ret;
}

int ath6kl_wmi_delete_pstream_cmd(struct wmi *wmi, u8 traffic_class, u8 tsid)
{
	struct sk_buff *skb;
	struct wmi_delete_pstream_cmd *cmd;
	u16 active_tsids = 0;
	int ret;

	if (traffic_class > 3) {
		ath6kl_err("invalid traffic class: %d\n", traffic_class);
		return -EINVAL;
	}

	skb = ath6kl_wmi_get_new_buf(sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_delete_pstream_cmd *) skb->data;
	cmd->traffic_class = traffic_class;
	cmd->tsid = tsid;

	spin_lock_bh(&wmi->lock);
	active_tsids = wmi->stream_exist_for_ac[traffic_class];
	spin_unlock_bh(&wmi->lock);

	if (!(active_tsids & (1 << tsid))) {
		dev_kfree_skb(skb);
		ath6kl_dbg(ATH6KL_DBG_WMI,
			   "TSID %d doesn't exist for traffic class: %d\n",
			   tsid, traffic_class);
		return -ENODATA;
	}

	ath6kl_dbg(ATH6KL_DBG_WMI,
		   "sending delete_pstream_cmd: traffic class: %d tsid=%d\n",
		   traffic_class, tsid);

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_DELETE_PSTREAM_CMDID,
				  SYNC_BEFORE_WMIFLAG);

	spin_lock_bh(&wmi->lock);
	wmi->stream_exist_for_ac[traffic_class] &= ~(1 << tsid);
	active_tsids = wmi->stream_exist_for_ac[traffic_class];
	spin_unlock_bh(&wmi->lock);

	/*
	 * Indicate stream inactivity to driver layer only if all tsids
	 * within this AC are deleted.
	 */
	if (!active_tsids) {
		ath6kl_indicate_tx_activity(wmi->parent_dev,
					    traffic_class, false);
		wmi->fat_pipe_exist &= ~(1 << traffic_class);
	}

	return ret;
}

int ath6kl_wmi_set_ip_cmd(struct wmi *wmi, struct wmi_set_ip_cmd *ip_cmd)
{
	struct sk_buff *skb;
	struct wmi_set_ip_cmd *cmd;
	int ret;

	/* Multicast address are not valid */
	if ((*((u8 *) &ip_cmd->ips[0]) >= 0xE0) ||
	    (*((u8 *) &ip_cmd->ips[1]) >= 0xE0))
		return -EINVAL;

	skb = ath6kl_wmi_get_new_buf(sizeof(struct wmi_set_ip_cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_set_ip_cmd *) skb->data;
	memcpy(cmd, ip_cmd, sizeof(struct wmi_set_ip_cmd));

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_SET_IP_CMDID, NO_SYNC_WMIFLAG);
	return ret;
}

static int ath6kl_wmi_get_wow_list_event_rx(struct wmi *wmi, u8 * datap,
					    int len)
{
	if (len < sizeof(struct wmi_get_wow_list_reply))
		return -EINVAL;

	return 0;
}

static int ath6kl_wmi_cmd_send_xtnd(struct wmi *wmi, struct sk_buff *skb,
				    enum wmix_command_id cmd_id,
				    enum wmi_sync_flag sync_flag)
{
	struct wmix_cmd_hdr *cmd_hdr;
	int ret;

	skb_push(skb, sizeof(struct wmix_cmd_hdr));

	cmd_hdr = (struct wmix_cmd_hdr *) skb->data;
	cmd_hdr->cmd_id = cpu_to_le32(cmd_id);

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_EXTENSION_CMDID, sync_flag);

	return ret;
}

int ath6kl_wmi_get_challenge_resp_cmd(struct wmi *wmi, u32 cookie, u32 source)
{
	struct sk_buff *skb;
	struct wmix_hb_challenge_resp_cmd *cmd;
	int ret;

	skb = ath6kl_wmi_get_new_buf(sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmix_hb_challenge_resp_cmd *) skb->data;
	cmd->cookie = cpu_to_le32(cookie);
	cmd->source = cpu_to_le32(source);

	ret = ath6kl_wmi_cmd_send_xtnd(wmi, skb, WMIX_HB_CHALLENGE_RESP_CMDID,
				       NO_SYNC_WMIFLAG);
	return ret;
}

int ath6kl_wmi_get_stats_cmd(struct wmi *wmi)
{
	return ath6kl_wmi_simple_cmd(wmi, WMI_GET_STATISTICS_CMDID);
}

int ath6kl_wmi_set_tx_pwr_cmd(struct wmi *wmi, u8 dbM)
{
	struct sk_buff *skb;
	struct wmi_set_tx_pwr_cmd *cmd;
	int ret;

	skb = ath6kl_wmi_get_new_buf(sizeof(struct wmi_set_tx_pwr_cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_set_tx_pwr_cmd *) skb->data;
	cmd->dbM = dbM;

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_SET_TX_PWR_CMDID,
				  NO_SYNC_WMIFLAG);

	return ret;
}

int ath6kl_wmi_get_tx_pwr_cmd(struct wmi *wmi)
{
	return ath6kl_wmi_simple_cmd(wmi, WMI_GET_TX_PWR_CMDID);
}

void ath6kl_wmi_get_current_bssid(struct wmi *wmi, u8 *bssid)
{
	if (bssid)
		memcpy(bssid, wmi->bssid, ETH_ALEN);
}

int ath6kl_wmi_set_lpreamble_cmd(struct wmi *wmi, u8 status, u8 preamble_policy)
{
	struct sk_buff *skb;
	struct wmi_set_lpreamble_cmd *cmd;
	int ret;

	skb = ath6kl_wmi_get_new_buf(sizeof(struct wmi_set_lpreamble_cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_set_lpreamble_cmd *) skb->data;
	cmd->status = status;
	cmd->preamble_policy = preamble_policy;

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_SET_LPREAMBLE_CMDID,
				  NO_SYNC_WMIFLAG);
	return ret;
}

int ath6kl_wmi_set_rts_cmd(struct wmi *wmi, u16 threshold)
{
	struct sk_buff *skb;
	struct wmi_set_rts_cmd *cmd;
	int ret;

	skb = ath6kl_wmi_get_new_buf(sizeof(struct wmi_set_rts_cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_set_rts_cmd *) skb->data;
	cmd->threshold = cpu_to_le16(threshold);

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_SET_RTS_CMDID, NO_SYNC_WMIFLAG);
	return ret;
}

int ath6kl_wmi_set_wmm_txop(struct wmi *wmi, enum wmi_txop_cfg cfg)
{
	struct sk_buff *skb;
	struct wmi_set_wmm_txop_cmd *cmd;
	int ret;

	if (!((cfg == WMI_TXOP_DISABLED) || (cfg == WMI_TXOP_ENABLED)))
		return -EINVAL;

	skb = ath6kl_wmi_get_new_buf(sizeof(struct wmi_set_wmm_txop_cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_set_wmm_txop_cmd *) skb->data;
	cmd->txop_enable = cfg;

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_SET_WMM_TXOP_CMDID,
				  NO_SYNC_WMIFLAG);
	return ret;
}

int ath6kl_wmi_set_keepalive_cmd(struct wmi *wmi, u8 keep_alive_intvl)
{
	struct sk_buff *skb;
	struct wmi_set_keepalive_cmd *cmd;
	int ret;

	skb = ath6kl_wmi_get_new_buf(sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_set_keepalive_cmd *) skb->data;
	cmd->keep_alive_intvl = keep_alive_intvl;
	wmi->keep_alive_intvl = keep_alive_intvl;

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_SET_KEEPALIVE_CMDID,
				  NO_SYNC_WMIFLAG);
	return ret;
}

s32 ath6kl_wmi_get_rate(s8 rate_index)
{
	if (rate_index == RATE_AUTO)
		return 0;

	return wmi_rate_tbl[(u32) rate_index][0];
}

void ath6kl_wmi_node_return(struct wmi *wmi, struct bss *bss)
{
	if (bss)
		wlan_node_return(&wmi->scan_table, bss);
}

struct bss *ath6kl_wmi_find_ssid_node(struct wmi *wmi, u8 * ssid,
				      u32 ssid_len, bool is_wpa2,
				      bool match_ssid)
{
	struct bss *node = NULL;

	node = wlan_find_ssid_node(&wmi->scan_table, ssid,
				  ssid_len, is_wpa2, match_ssid);
	return node;
}

struct bss *ath6kl_wmi_find_node(struct wmi *wmi, const u8 * mac_addr)
{
	struct bss *ni = NULL;

	ni = wlan_find_node(&wmi->scan_table, mac_addr);

	return ni;
}

void ath6kl_wmi_node_free(struct wmi *wmi, const u8 * mac_addr)
{
	struct bss *ni = NULL;

	ni = wlan_find_node(&wmi->scan_table, mac_addr);
	if (ni != NULL)
		wlan_node_reclaim(&wmi->scan_table, ni);

	return;
}

static int ath6kl_wmi_get_pmkid_list_event_rx(struct wmi *wmi, u8 *datap,
					      u32 len)
{
	struct wmi_pmkid_list_reply *reply;
	u32 expected_len;

	if (len < sizeof(struct wmi_pmkid_list_reply))
		return -EINVAL;

	reply = (struct wmi_pmkid_list_reply *)datap;
	expected_len = sizeof(reply->num_pmkid) +
		le32_to_cpu(reply->num_pmkid) * WMI_PMKID_LEN;

	if (len < expected_len)
		return -EINVAL;

	return 0;
}

static int ath6kl_wmi_addba_req_event_rx(struct wmi *wmi, u8 *datap, int len)
{
	struct wmi_addba_req_event *cmd = (struct wmi_addba_req_event *) datap;

	aggr_recv_addba_req_evt(wmi->parent_dev, cmd->tid,
				le16_to_cpu(cmd->st_seq_no), cmd->win_sz);

	return 0;
}

static int ath6kl_wmi_delba_req_event_rx(struct wmi *wmi, u8 *datap, int len)
{
	struct wmi_delba_event *cmd = (struct wmi_delba_event *) datap;

	aggr_recv_delba_req_evt(wmi->parent_dev, cmd->tid);

	return 0;
}

/*  AP mode functions */
static int ath6kl_wmi_pspoll_event_rx(struct wmi *wmi, u8 *datap, int len)
{
	struct wmi_pspoll_event *ev;

	if (len < sizeof(struct wmi_pspoll_event))
		return -EINVAL;

	ev = (struct wmi_pspoll_event *) datap;

	ath6kl_pspoll_event(wmi->parent_dev, le16_to_cpu(ev->aid));

	return 0;
}

static int ath6kl_wmi_dtimexpiry_event_rx(struct wmi *wmi, u8 *datap, int len)
{
	ath6kl_dtimexpiry_event(wmi->parent_dev);

	return 0;
}

int ath6kl_wmi_set_pvb_cmd(struct wmi *wmi, u16 aid, bool flag)
{
	struct sk_buff *skb;
	struct wmi_ap_set_pvb_cmd *cmd;
	int ret;

	skb = ath6kl_wmi_get_new_buf(sizeof(struct wmi_ap_set_pvb_cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_ap_set_pvb_cmd *) skb->data;
	cmd->aid = cpu_to_le16(aid);
	cmd->flag = cpu_to_le32(flag);

	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_AP_SET_PVB_CMDID,
				  NO_SYNC_WMIFLAG);

	return 0;
}

int ath6kl_wmi_set_rx_frame_format_cmd(struct wmi *wmi, u8 rx_meta_ver,
				       bool rx_dot11_hdr, bool defrag_on_host)
{
	struct sk_buff *skb;
	struct wmi_rx_frame_format_cmd *cmd;
	int ret;

	skb = ath6kl_wmi_get_new_buf(sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_rx_frame_format_cmd *) skb->data;
	cmd->dot11_hdr = rx_dot11_hdr ? 1 : 0;
	cmd->defrag_on_host = defrag_on_host ? 1 : 0;
	cmd->meta_ver = rx_meta_ver;

	/* Delete the local aggr state, on host */
	ret = ath6kl_wmi_cmd_send(wmi, skb, WMI_RX_FRAME_FORMAT_CMDID,
				  NO_SYNC_WMIFLAG);

	return ret;
}

static int ath6kl_wmi_control_rx_xtnd(struct wmi *wmi, struct sk_buff *skb)
{
	struct wmix_cmd_hdr *cmd;
	u32 len;
	u16 id;
	u8 *datap;
	int ret = 0;

	if (skb->len < sizeof(struct wmix_cmd_hdr)) {
		ath6kl_err("bad packet 1\n");
		wmi->stat.cmd_len_err++;
		return -EINVAL;
	}

	cmd = (struct wmix_cmd_hdr *) skb->data;
	id = le32_to_cpu(cmd->cmd_id);

	skb_pull(skb, sizeof(struct wmix_cmd_hdr));

	datap = skb->data;
	len = skb->len;

	switch (id) {
	case WMIX_HB_CHALLENGE_RESP_EVENTID:
		break;
	case WMIX_DBGLOG_EVENTID:
		break;
	default:
		ath6kl_err("unknown cmd id 0x%x\n", id);
		wmi->stat.cmd_id_err++;
		ret = -EINVAL;
		break;
	}

	return ret;
}

/* Control Path */
int ath6kl_wmi_control_rx(struct wmi *wmi, struct sk_buff *skb)
{
	struct wmi_cmd_hdr *cmd;
	u32 len;
	u16 id;
	u8 *datap;
	int ret = 0;

	if (WARN_ON(skb == NULL))
		return -EINVAL;

	if (skb->len < sizeof(struct wmi_cmd_hdr)) {
		ath6kl_err("bad packet 1\n");
		dev_kfree_skb(skb);
		wmi->stat.cmd_len_err++;
		return -EINVAL;
	}

	cmd = (struct wmi_cmd_hdr *) skb->data;
	id = le16_to_cpu(cmd->cmd_id);

	skb_pull(skb, sizeof(struct wmi_cmd_hdr));

	datap = skb->data;
	len = skb->len;

	ath6kl_dbg(ATH6KL_DBG_WMI, "%s: wmi id: %d\n", __func__, id);
	ath6kl_dbg_dump(ATH6KL_DBG_RAW_BYTES, "msg payload ", datap, len);

	switch (id) {
	case WMI_GET_BITRATE_CMDID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_GET_BITRATE_CMDID\n");
		ret = ath6kl_wmi_bitrate_reply_rx(wmi, datap, len);
		break;
	case WMI_GET_CHANNEL_LIST_CMDID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_GET_CHANNEL_LIST_CMDID\n");
		ret = ath6kl_wmi_ch_list_reply_rx(wmi, datap, len);
		break;
	case WMI_GET_TX_PWR_CMDID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_GET_TX_PWR_CMDID\n");
		ret = ath6kl_wmi_tx_pwr_reply_rx(wmi, datap, len);
		break;
	case WMI_READY_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_READY_EVENTID\n");
		ret = ath6kl_wmi_ready_event_rx(wmi, datap, len);
		break;
	case WMI_CONNECT_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_CONNECT_EVENTID\n");
		ret = ath6kl_wmi_connect_event_rx(wmi, datap, len);
		break;
	case WMI_DISCONNECT_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_DISCONNECT_EVENTID\n");
		ret = ath6kl_wmi_disconnect_event_rx(wmi, datap, len);
		break;
	case WMI_PEER_NODE_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_PEER_NODE_EVENTID\n");
		ret = ath6kl_wmi_peer_node_event_rx(wmi, datap, len);
		break;
	case WMI_TKIP_MICERR_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_TKIP_MICERR_EVENTID\n");
		ret = ath6kl_wmi_tkip_micerr_event_rx(wmi, datap, len);
		break;
	case WMI_BSSINFO_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_BSSINFO_EVENTID\n");
		ath6kl_wmi_convert_bssinfo_hdr2_to_hdr(skb, datap);
		ret = ath6kl_wmi_bssinfo_event_rx(wmi, skb->data, skb->len);
		break;
	case WMI_REGDOMAIN_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_REGDOMAIN_EVENTID\n");
		break;
	case WMI_PSTREAM_TIMEOUT_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_PSTREAM_TIMEOUT_EVENTID\n");
		ret = ath6kl_wmi_pstream_timeout_event_rx(wmi, datap, len);
		break;
	case WMI_NEIGHBOR_REPORT_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_NEIGHBOR_REPORT_EVENTID\n");
		break;
	case WMI_SCAN_COMPLETE_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_SCAN_COMPLETE_EVENTID\n");
		ret = ath6kl_wmi_scan_complete_rx(wmi, datap, len);
		break;
	case WMI_CMDERROR_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_CMDERROR_EVENTID\n");
		ret = ath6kl_wmi_error_event_rx(wmi, datap, len);
		break;
	case WMI_REPORT_STATISTICS_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_REPORT_STATISTICS_EVENTID\n");
		ret = ath6kl_wmi_stats_event_rx(wmi, datap, len);
		break;
	case WMI_RSSI_THRESHOLD_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_RSSI_THRESHOLD_EVENTID\n");
		ret = ath6kl_wmi_rssi_threshold_event_rx(wmi, datap, len);
		break;
	case WMI_ERROR_REPORT_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_ERROR_REPORT_EVENTID\n");
		break;
	case WMI_OPT_RX_FRAME_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_OPT_RX_FRAME_EVENTID\n");
		ret = ath6kl_wmi_opt_frame_event_rx(wmi, datap, len);
		break;
	case WMI_REPORT_ROAM_TBL_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_REPORT_ROAM_TBL_EVENTID\n");
		break;
	case WMI_EXTENSION_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_EXTENSION_EVENTID\n");
		ret = ath6kl_wmi_control_rx_xtnd(wmi, skb);
		break;
	case WMI_CAC_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_CAC_EVENTID\n");
		ret = ath6kl_wmi_cac_event_rx(wmi, datap, len);
		break;
	case WMI_CHANNEL_CHANGE_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_CHANNEL_CHANGE_EVENTID\n");
		break;
	case WMI_REPORT_ROAM_DATA_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_REPORT_ROAM_DATA_EVENTID\n");
		break;
	case WMI_GET_FIXRATES_CMDID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_GET_FIXRATES_CMDID\n");
		ret = ath6kl_wmi_ratemask_reply_rx(wmi, datap, len);
		break;
	case WMI_TX_RETRY_ERR_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_TX_RETRY_ERR_EVENTID\n");
		break;
	case WMI_SNR_THRESHOLD_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_SNR_THRESHOLD_EVENTID\n");
		ret = ath6kl_wmi_snr_threshold_event_rx(wmi, datap, len);
		break;
	case WMI_LQ_THRESHOLD_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_LQ_THRESHOLD_EVENTID\n");
		break;
	case WMI_APLIST_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_APLIST_EVENTID\n");
		ret = ath6kl_wmi_aplist_event_rx(wmi, datap, len);
		break;
	case WMI_GET_KEEPALIVE_CMDID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_GET_KEEPALIVE_CMDID\n");
		ret = ath6kl_wmi_keepalive_reply_rx(wmi, datap, len);
		break;
	case WMI_GET_WOW_LIST_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_GET_WOW_LIST_EVENTID\n");
		ret = ath6kl_wmi_get_wow_list_event_rx(wmi, datap, len);
		break;
	case WMI_GET_PMKID_LIST_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_GET_PMKID_LIST_EVENTID\n");
		ret = ath6kl_wmi_get_pmkid_list_event_rx(wmi, datap, len);
		break;
	case WMI_PSPOLL_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_PSPOLL_EVENTID\n");
		ret = ath6kl_wmi_pspoll_event_rx(wmi, datap, len);
		break;
	case WMI_DTIMEXPIRY_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_DTIMEXPIRY_EVENTID\n");
		ret = ath6kl_wmi_dtimexpiry_event_rx(wmi, datap, len);
		break;
	case WMI_SET_PARAMS_REPLY_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_SET_PARAMS_REPLY_EVENTID\n");
		break;
	case WMI_ADDBA_REQ_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_ADDBA_REQ_EVENTID\n");
		ret = ath6kl_wmi_addba_req_event_rx(wmi, datap, len);
		break;
	case WMI_ADDBA_RESP_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_ADDBA_RESP_EVENTID\n");
		break;
	case WMI_DELBA_REQ_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_DELBA_REQ_EVENTID\n");
		ret = ath6kl_wmi_delba_req_event_rx(wmi, datap, len);
		break;
	case WMI_REPORT_BTCOEX_CONFIG_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI,
			   "WMI_REPORT_BTCOEX_CONFIG_EVENTID\n");
		break;
	case WMI_REPORT_BTCOEX_STATS_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI,
			   "WMI_REPORT_BTCOEX_STATS_EVENTID\n");
		break;
	case WMI_TX_COMPLETE_EVENTID:
		ath6kl_dbg(ATH6KL_DBG_WMI, "WMI_TX_COMPLETE_EVENTID\n");
		ret = ath6kl_wmi_tx_complete_event_rx(datap, len);
		break;
	default:
		ath6kl_dbg(ATH6KL_DBG_WMI, "unknown cmd id 0x%x\n", id);
		wmi->stat.cmd_id_err++;
		ret = -EINVAL;
		break;
	}

	dev_kfree_skb(skb);

	return ret;
}

static void ath6kl_wmi_qos_state_init(struct wmi *wmi)
{
	if (!wmi)
		return;

	spin_lock_bh(&wmi->lock);

	wmi->fat_pipe_exist = 0;
	memset(wmi->stream_exist_for_ac, 0, sizeof(wmi->stream_exist_for_ac));

	spin_unlock_bh(&wmi->lock);
}

void *ath6kl_wmi_init(void *dev)
{
	struct wmi *wmi;

	wmi = kzalloc(sizeof(struct wmi), GFP_KERNEL);
	if (!wmi)
		return NULL;

	spin_lock_init(&wmi->lock);

	wmi->parent_dev = dev;

	wlan_node_table_init(wmi, &wmi->scan_table);
	ath6kl_wmi_qos_state_init(wmi);

	wmi->pwr_mode = REC_POWER;
	wmi->phy_mode = WMI_11G_MODE;

	wmi->pair_crypto_type = NONE_CRYPT;
	wmi->grp_crypto_type = NONE_CRYPT;

	wmi->ht_allowed[A_BAND_24GHZ] = 1;
	wmi->ht_allowed[A_BAND_5GHZ] = 1;

	return wmi;
}

void ath6kl_wmi_shutdown(struct wmi *wmi)
{
	if (!wmi)
		return;

	wlan_node_table_cleanup(&wmi->scan_table);
	kfree(wmi);
}
