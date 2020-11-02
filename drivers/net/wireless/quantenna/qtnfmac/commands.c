// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2015-2016 Quantenna Communications. All rights reserved. */

#include <linux/types.h>
#include <linux/skbuff.h>

#include "cfg80211.h"
#include "core.h"
#include "qlink.h"
#include "qlink_util.h"
#include "bus.h"
#include "commands.h"

/* Let device itself to select best values for current conditions */
#define QTNF_SCAN_TIME_AUTO	0

#define QTNF_SCAN_DWELL_ACTIVE_DEFAULT		90
#define QTNF_SCAN_DWELL_PASSIVE_DEFAULT		100
#define QTNF_SCAN_SAMPLE_DURATION_DEFAULT	QTNF_SCAN_TIME_AUTO

static int qtnf_cmd_check_reply_header(const struct qlink_resp *resp,
				       u16 cmd_id, u8 mac_id, u8 vif_id,
				       size_t resp_size)
{
	if (unlikely(le16_to_cpu(resp->cmd_id) != cmd_id)) {
		pr_warn("VIF%u.%u CMD%x: bad cmd_id in response: 0x%.4X\n",
			mac_id, vif_id, cmd_id, le16_to_cpu(resp->cmd_id));
		return -EINVAL;
	}

	if (unlikely(resp->macid != mac_id)) {
		pr_warn("VIF%u.%u CMD%x: bad MAC in response: %u\n",
			mac_id, vif_id, cmd_id, resp->macid);
		return -EINVAL;
	}

	if (unlikely(resp->vifid != vif_id)) {
		pr_warn("VIF%u.%u CMD%x: bad VIF in response: %u\n",
			mac_id, vif_id, cmd_id, resp->vifid);
		return -EINVAL;
	}

	if (unlikely(le16_to_cpu(resp->mhdr.len) < resp_size)) {
		pr_warn("VIF%u.%u CMD%x: bad response size %u < %zu\n",
			mac_id, vif_id, cmd_id,
			le16_to_cpu(resp->mhdr.len), resp_size);
		return -ENOSPC;
	}

	return 0;
}

static int qtnf_cmd_resp_result_decode(enum qlink_cmd_result qcode)
{
	switch (qcode) {
	case QLINK_CMD_RESULT_OK:
		return 0;
	case QLINK_CMD_RESULT_INVALID:
		return -EINVAL;
	case QLINK_CMD_RESULT_ENOTSUPP:
		return -ENOTSUPP;
	case QLINK_CMD_RESULT_ENOTFOUND:
		return -ENOENT;
	case QLINK_CMD_RESULT_EALREADY:
		return -EALREADY;
	case QLINK_CMD_RESULT_EADDRINUSE:
		return -EADDRINUSE;
	case QLINK_CMD_RESULT_EADDRNOTAVAIL:
		return -EADDRNOTAVAIL;
	case QLINK_CMD_RESULT_EBUSY:
		return -EBUSY;
	default:
		return -EFAULT;
	}
}

static int qtnf_cmd_send_with_reply(struct qtnf_bus *bus,
				    struct sk_buff *cmd_skb,
				    struct sk_buff **response_skb,
				    size_t const_resp_size,
				    size_t *var_resp_size)
{
	struct qlink_cmd *cmd;
	struct qlink_resp *resp = NULL;
	struct sk_buff *resp_skb = NULL;
	int resp_res = 0;
	u16 cmd_id;
	u8 mac_id;
	u8 vif_id;
	int ret;

	cmd = (struct qlink_cmd *)cmd_skb->data;
	cmd_id = le16_to_cpu(cmd->cmd_id);
	mac_id = cmd->macid;
	vif_id = cmd->vifid;
	cmd->mhdr.len = cpu_to_le16(cmd_skb->len);

	pr_debug("VIF%u.%u cmd=0x%.4X\n", mac_id, vif_id, cmd_id);

	if (!qtnf_fw_is_up(bus) && cmd_id != QLINK_CMD_FW_INIT) {
		pr_warn("VIF%u.%u: drop cmd 0x%.4X in fw state %d\n",
			mac_id, vif_id, cmd_id, bus->fw_state);
		dev_kfree_skb(cmd_skb);
		return -ENODEV;
	}

	ret = qtnf_trans_send_cmd_with_resp(bus, cmd_skb, &resp_skb);
	if (ret)
		goto out;

	if (WARN_ON(!resp_skb || !resp_skb->data)) {
		ret = -EFAULT;
		goto out;
	}

	resp = (struct qlink_resp *)resp_skb->data;
	resp_res = le16_to_cpu(resp->result);
	ret = qtnf_cmd_check_reply_header(resp, cmd_id, mac_id, vif_id,
					  const_resp_size);
	if (ret)
		goto out;

	/* Return length of variable part of response */
	if (response_skb && var_resp_size)
		*var_resp_size = le16_to_cpu(resp->mhdr.len) - const_resp_size;

out:
	if (response_skb)
		*response_skb = resp_skb;
	else
		consume_skb(resp_skb);

	if (!ret)
		return qtnf_cmd_resp_result_decode(resp_res);

	pr_warn("VIF%u.%u: cmd 0x%.4X failed: %d\n",
		mac_id, vif_id, cmd_id, ret);

	return ret;
}

static inline int qtnf_cmd_send(struct qtnf_bus *bus, struct sk_buff *cmd_skb)
{
	return qtnf_cmd_send_with_reply(bus, cmd_skb, NULL,
					sizeof(struct qlink_resp), NULL);
}

static struct sk_buff *qtnf_cmd_alloc_new_cmdskb(u8 macid, u8 vifid, u16 cmd_no,
						 size_t cmd_size)
{
	struct qlink_cmd *cmd;
	struct sk_buff *cmd_skb;

	cmd_skb = __dev_alloc_skb(sizeof(*cmd) +
				  QTNF_MAX_CMD_BUF_SIZE, GFP_KERNEL);
	if (unlikely(!cmd_skb)) {
		pr_err("VIF%u.%u CMD %u: alloc failed\n", macid, vifid, cmd_no);
		return NULL;
	}

	skb_put_zero(cmd_skb, cmd_size);

	cmd = (struct qlink_cmd *)cmd_skb->data;
	cmd->mhdr.len = cpu_to_le16(cmd_skb->len);
	cmd->mhdr.type = cpu_to_le16(QLINK_MSG_TYPE_CMD);
	cmd->cmd_id = cpu_to_le16(cmd_no);
	cmd->macid = macid;
	cmd->vifid = vifid;

	return cmd_skb;
}

static void qtnf_cmd_tlv_ie_set_add(struct sk_buff *cmd_skb, u8 frame_type,
				    const u8 *buf, size_t len)
{
	struct qlink_tlv_ie_set *tlv;

	tlv = (struct qlink_tlv_ie_set *)skb_put(cmd_skb, sizeof(*tlv) +
						 round_up(len, QLINK_ALIGN));
	tlv->hdr.type = cpu_to_le16(QTN_TLV_ID_IE_SET);
	tlv->hdr.len = cpu_to_le16(len + sizeof(*tlv) - sizeof(tlv->hdr));
	tlv->type = frame_type;
	tlv->flags = 0;

	if (len && buf)
		memcpy(tlv->ie_data, buf, len);
}

static bool qtnf_cmd_start_ap_can_fit(const struct qtnf_vif *vif,
				      const struct cfg80211_ap_settings *s)
{
	unsigned int len = sizeof(struct qlink_cmd_start_ap);

	len += round_up(s->ssid_len, QLINK_ALIGN);
	len += round_up(s->beacon.head_len, QLINK_ALIGN);
	len += round_up(s->beacon.tail_len, QLINK_ALIGN);
	len += round_up(s->beacon.beacon_ies_len, QLINK_ALIGN);
	len += round_up(s->beacon.proberesp_ies_len, QLINK_ALIGN);
	len += round_up(s->beacon.assocresp_ies_len, QLINK_ALIGN);
	len += round_up(s->beacon.probe_resp_len, QLINK_ALIGN);

	if (cfg80211_chandef_valid(&s->chandef))
		len += sizeof(struct qlink_tlv_chandef);

	if (s->acl) {
		unsigned int acl_len = struct_size(s->acl, mac_addrs,
						   s->acl->n_acl_entries);

		len += sizeof(struct qlink_tlv_hdr) +
			round_up(acl_len, QLINK_ALIGN);
	}

	if (len > (sizeof(struct qlink_cmd) + QTNF_MAX_CMD_BUF_SIZE)) {
		pr_err("VIF%u.%u: can not fit AP settings: %u\n",
		       vif->mac->macid, vif->vifid, len);
		return false;
	}

	return true;
}

static void qtnf_cmd_tlv_ie_ext_add(struct sk_buff *cmd_skb, u8 eid_ext,
				    const void *buf, size_t len)
{
	struct qlink_tlv_ext_ie *tlv;

	tlv = (struct qlink_tlv_ext_ie *)skb_put(cmd_skb, sizeof(*tlv) + len);
	tlv->hdr.type = cpu_to_le16(WLAN_EID_EXTENSION);
	tlv->hdr.len = cpu_to_le16(sizeof(*tlv) + len - sizeof(tlv->hdr));
	tlv->eid_ext = eid_ext;

	if (len && buf)
		memcpy(tlv->ie_data, buf, len);
}

int qtnf_cmd_send_start_ap(struct qtnf_vif *vif,
			   const struct cfg80211_ap_settings *s)
{
	struct sk_buff *cmd_skb;
	struct qlink_cmd_start_ap *cmd;
	struct qlink_auth_encr *aen;
	int ret;
	int i;

	if (!qtnf_cmd_start_ap_can_fit(vif, s))
		return -E2BIG;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_START_AP,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	cmd = (struct qlink_cmd_start_ap *)cmd_skb->data;
	cmd->dtim_period = s->dtim_period;
	cmd->beacon_interval = cpu_to_le16(s->beacon_interval);
	cmd->hidden_ssid = qlink_hidden_ssid_nl2q(s->hidden_ssid);
	cmd->inactivity_timeout = cpu_to_le16(s->inactivity_timeout);
	cmd->smps_mode = s->smps_mode;
	cmd->p2p_ctwindow = s->p2p_ctwindow;
	cmd->p2p_opp_ps = s->p2p_opp_ps;
	cmd->pbss = s->pbss;
	cmd->ht_required = s->ht_required;
	cmd->vht_required = s->vht_required;
	cmd->twt_responder = s->twt_responder;
	if (s->he_obss_pd.enable) {
		cmd->sr_params.sr_control |= QLINK_SR_SRG_INFORMATION_PRESENT;
		cmd->sr_params.srg_obss_pd_min_offset =
			s->he_obss_pd.min_offset;
		cmd->sr_params.srg_obss_pd_max_offset =
			s->he_obss_pd.max_offset;
	}

	aen = &cmd->aen;
	aen->auth_type = s->auth_type;
	aen->privacy = !!s->privacy;
	aen->wpa_versions = cpu_to_le32(s->crypto.wpa_versions);
	aen->cipher_group = cpu_to_le32(s->crypto.cipher_group);
	aen->n_ciphers_pairwise = cpu_to_le32(s->crypto.n_ciphers_pairwise);
	for (i = 0; i < QLINK_MAX_NR_CIPHER_SUITES; i++)
		aen->ciphers_pairwise[i] =
				cpu_to_le32(s->crypto.ciphers_pairwise[i]);
	aen->n_akm_suites = cpu_to_le32(s->crypto.n_akm_suites);
	for (i = 0; i < QLINK_MAX_NR_AKM_SUITES; i++)
		aen->akm_suites[i] = cpu_to_le32(s->crypto.akm_suites[i]);
	aen->control_port = s->crypto.control_port;
	aen->control_port_no_encrypt = s->crypto.control_port_no_encrypt;
	aen->control_port_ethertype =
		cpu_to_le16(be16_to_cpu(s->crypto.control_port_ethertype));

	if (s->ssid && s->ssid_len > 0 && s->ssid_len <= IEEE80211_MAX_SSID_LEN)
		qtnf_cmd_skb_put_tlv_arr(cmd_skb, WLAN_EID_SSID, s->ssid,
					 s->ssid_len);

	if (cfg80211_chandef_valid(&s->chandef)) {
		struct qlink_tlv_chandef *chtlv =
			(struct qlink_tlv_chandef *)skb_put(cmd_skb,
							    sizeof(*chtlv));

		chtlv->hdr.type = cpu_to_le16(QTN_TLV_ID_CHANDEF);
		chtlv->hdr.len = cpu_to_le16(sizeof(*chtlv) -
					     sizeof(chtlv->hdr));
		qlink_chandef_cfg2q(&s->chandef, &chtlv->chdef);
	}

	qtnf_cmd_tlv_ie_set_add(cmd_skb, QLINK_IE_SET_BEACON_HEAD,
				s->beacon.head, s->beacon.head_len);
	qtnf_cmd_tlv_ie_set_add(cmd_skb, QLINK_IE_SET_BEACON_TAIL,
				s->beacon.tail, s->beacon.tail_len);
	qtnf_cmd_tlv_ie_set_add(cmd_skb, QLINK_IE_SET_BEACON_IES,
				s->beacon.beacon_ies, s->beacon.beacon_ies_len);
	qtnf_cmd_tlv_ie_set_add(cmd_skb, QLINK_IE_SET_PROBE_RESP,
				s->beacon.probe_resp, s->beacon.probe_resp_len);
	qtnf_cmd_tlv_ie_set_add(cmd_skb, QLINK_IE_SET_PROBE_RESP_IES,
				s->beacon.proberesp_ies,
				s->beacon.proberesp_ies_len);
	qtnf_cmd_tlv_ie_set_add(cmd_skb, QLINK_IE_SET_ASSOC_RESP,
				s->beacon.assocresp_ies,
				s->beacon.assocresp_ies_len);

	if (s->ht_cap) {
		struct qlink_tlv_hdr *tlv = (struct qlink_tlv_hdr *)
			skb_put(cmd_skb, sizeof(*tlv) +
				round_up(sizeof(*s->ht_cap), QLINK_ALIGN));

		tlv->type = cpu_to_le16(WLAN_EID_HT_CAPABILITY);
		tlv->len = cpu_to_le16(sizeof(*s->ht_cap));
		memcpy(tlv->val, s->ht_cap, sizeof(*s->ht_cap));
	}

	if (s->vht_cap) {
		struct qlink_tlv_hdr *tlv = (struct qlink_tlv_hdr *)
			skb_put(cmd_skb, sizeof(*tlv) + sizeof(*s->vht_cap));

		tlv->type = cpu_to_le16(WLAN_EID_VHT_CAPABILITY);
		tlv->len = cpu_to_le16(sizeof(*s->vht_cap));
		memcpy(tlv->val, s->vht_cap, sizeof(*s->vht_cap));
	}

	if (s->he_cap)
		qtnf_cmd_tlv_ie_ext_add(cmd_skb, WLAN_EID_EXT_HE_CAPABILITY,
					s->he_cap, sizeof(*s->he_cap));

	if (s->acl) {
		size_t acl_size = struct_size(s->acl, mac_addrs,
					      s->acl->n_acl_entries);
		struct qlink_tlv_hdr *tlv =
			skb_put(cmd_skb,
				sizeof(*tlv) + round_up(acl_size, QLINK_ALIGN));

		tlv->type = cpu_to_le16(QTN_TLV_ID_ACL_DATA);
		tlv->len = cpu_to_le16(acl_size);
		qlink_acl_data_cfg2q(s->acl, (struct qlink_acl_data *)tlv->val);
	}

	qtnf_bus_lock(vif->mac->bus);
	ret = qtnf_cmd_send(vif->mac->bus, cmd_skb);
	if (ret)
		goto out;

	netif_carrier_on(vif->netdev);

out:
	qtnf_bus_unlock(vif->mac->bus);

	return ret;
}

int qtnf_cmd_send_stop_ap(struct qtnf_vif *vif)
{
	struct sk_buff *cmd_skb;
	int ret;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_STOP_AP,
					    sizeof(struct qlink_cmd));
	if (!cmd_skb)
		return -ENOMEM;

	qtnf_bus_lock(vif->mac->bus);
	ret = qtnf_cmd_send(vif->mac->bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(vif->mac->bus);

	return ret;
}

int qtnf_cmd_send_register_mgmt(struct qtnf_vif *vif, u16 frame_type, bool reg)
{
	struct sk_buff *cmd_skb;
	struct qlink_cmd_mgmt_frame_register *cmd;
	int ret;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_REGISTER_MGMT,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	qtnf_bus_lock(vif->mac->bus);

	cmd = (struct qlink_cmd_mgmt_frame_register *)cmd_skb->data;
	cmd->frame_type = cpu_to_le16(frame_type);
	cmd->do_register = reg;

	ret = qtnf_cmd_send(vif->mac->bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(vif->mac->bus);

	return ret;
}

int qtnf_cmd_send_frame(struct qtnf_vif *vif, u32 cookie, u16 flags,
			u16 freq, const u8 *buf, size_t len)
{
	struct sk_buff *cmd_skb;
	struct qlink_cmd_frame_tx *cmd;
	int ret;

	if (sizeof(*cmd) + len > QTNF_MAX_CMD_BUF_SIZE) {
		pr_warn("VIF%u.%u: frame is too big: %zu\n", vif->mac->macid,
			vif->vifid, len);
		return -E2BIG;
	}

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_SEND_FRAME,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	qtnf_bus_lock(vif->mac->bus);

	cmd = (struct qlink_cmd_frame_tx *)cmd_skb->data;
	cmd->cookie = cpu_to_le32(cookie);
	cmd->freq = cpu_to_le16(freq);
	cmd->flags = cpu_to_le16(flags);

	if (len && buf)
		qtnf_cmd_skb_put_buffer(cmd_skb, buf, len);

	ret = qtnf_cmd_send(vif->mac->bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(vif->mac->bus);

	return ret;
}

int qtnf_cmd_send_mgmt_set_appie(struct qtnf_vif *vif, u8 frame_type,
				 const u8 *buf, size_t len)
{
	struct sk_buff *cmd_skb;
	int ret;

	if (len > QTNF_MAX_CMD_BUF_SIZE) {
		pr_warn("VIF%u.%u: %u frame is too big: %zu\n", vif->mac->macid,
			vif->vifid, frame_type, len);
		return -E2BIG;
	}

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_MGMT_SET_APPIE,
					    sizeof(struct qlink_cmd));
	if (!cmd_skb)
		return -ENOMEM;

	qtnf_cmd_tlv_ie_set_add(cmd_skb, frame_type, buf, len);

	qtnf_bus_lock(vif->mac->bus);
	ret = qtnf_cmd_send(vif->mac->bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(vif->mac->bus);

	return ret;
}

static void
qtnf_sta_info_parse_rate(struct rate_info *rate_dst,
			 const struct qlink_sta_info_rate *rate_src)
{
	rate_dst->legacy = get_unaligned_le16(&rate_src->rate) * 10;

	rate_dst->mcs = rate_src->mcs;
	rate_dst->nss = rate_src->nss;
	rate_dst->flags = 0;

	switch (rate_src->bw) {
	case QLINK_CHAN_WIDTH_5:
		rate_dst->bw = RATE_INFO_BW_5;
		break;
	case QLINK_CHAN_WIDTH_10:
		rate_dst->bw = RATE_INFO_BW_10;
		break;
	case QLINK_CHAN_WIDTH_20:
	case QLINK_CHAN_WIDTH_20_NOHT:
		rate_dst->bw = RATE_INFO_BW_20;
		break;
	case QLINK_CHAN_WIDTH_40:
		rate_dst->bw = RATE_INFO_BW_40;
		break;
	case QLINK_CHAN_WIDTH_80:
		rate_dst->bw = RATE_INFO_BW_80;
		break;
	case QLINK_CHAN_WIDTH_160:
		rate_dst->bw = RATE_INFO_BW_160;
		break;
	default:
		rate_dst->bw = 0;
		break;
	}

	if (rate_src->flags & QLINK_STA_INFO_RATE_FLAG_HT_MCS)
		rate_dst->flags |= RATE_INFO_FLAGS_MCS;
	else if (rate_src->flags & QLINK_STA_INFO_RATE_FLAG_VHT_MCS)
		rate_dst->flags |= RATE_INFO_FLAGS_VHT_MCS;
	else if (rate_src->flags & QLINK_STA_INFO_RATE_FLAG_HE_MCS)
		rate_dst->flags |= RATE_INFO_FLAGS_HE_MCS;

	if (rate_src->flags & QLINK_STA_INFO_RATE_FLAG_SHORT_GI)
		rate_dst->flags |= RATE_INFO_FLAGS_SHORT_GI;
}

static void
qtnf_sta_info_parse_flags(struct nl80211_sta_flag_update *dst,
			  const struct qlink_sta_info_state *src)
{
	u32 mask, value;

	dst->mask = 0;
	dst->set = 0;

	mask = le32_to_cpu(src->mask);
	value = le32_to_cpu(src->value);

	if (mask & QLINK_STA_FLAG_AUTHORIZED) {
		dst->mask |= BIT(NL80211_STA_FLAG_AUTHORIZED);
		if (value & QLINK_STA_FLAG_AUTHORIZED)
			dst->set |= BIT(NL80211_STA_FLAG_AUTHORIZED);
	}

	if (mask & QLINK_STA_FLAG_SHORT_PREAMBLE) {
		dst->mask |= BIT(NL80211_STA_FLAG_SHORT_PREAMBLE);
		if (value & QLINK_STA_FLAG_SHORT_PREAMBLE)
			dst->set |= BIT(NL80211_STA_FLAG_SHORT_PREAMBLE);
	}

	if (mask & QLINK_STA_FLAG_WME) {
		dst->mask |= BIT(NL80211_STA_FLAG_WME);
		if (value & QLINK_STA_FLAG_WME)
			dst->set |= BIT(NL80211_STA_FLAG_WME);
	}

	if (mask & QLINK_STA_FLAG_MFP) {
		dst->mask |= BIT(NL80211_STA_FLAG_MFP);
		if (value & QLINK_STA_FLAG_MFP)
			dst->set |= BIT(NL80211_STA_FLAG_MFP);
	}

	if (mask & QLINK_STA_FLAG_AUTHENTICATED) {
		dst->mask |= BIT(NL80211_STA_FLAG_AUTHENTICATED);
		if (value & QLINK_STA_FLAG_AUTHENTICATED)
			dst->set |= BIT(NL80211_STA_FLAG_AUTHENTICATED);
	}

	if (mask & QLINK_STA_FLAG_TDLS_PEER) {
		dst->mask |= BIT(NL80211_STA_FLAG_TDLS_PEER);
		if (value & QLINK_STA_FLAG_TDLS_PEER)
			dst->set |= BIT(NL80211_STA_FLAG_TDLS_PEER);
	}

	if (mask & QLINK_STA_FLAG_ASSOCIATED) {
		dst->mask |= BIT(NL80211_STA_FLAG_ASSOCIATED);
		if (value & QLINK_STA_FLAG_ASSOCIATED)
			dst->set |= BIT(NL80211_STA_FLAG_ASSOCIATED);
	}
}

static void
qtnf_cmd_sta_info_parse(struct station_info *sinfo, const u8 *data,
			size_t resp_size)
{
	const struct qlink_tlv_hdr *tlv;
	const struct qlink_sta_stats *stats = NULL;
	const u8 *map = NULL;
	unsigned int map_len = 0;
	unsigned int stats_len = 0;
	u16 tlv_len;

#define qtnf_sta_stat_avail(stat_name, bitn)	\
	(qtnf_utils_is_bit_set(map, bitn, map_len) && \
	 (offsetofend(struct qlink_sta_stats, stat_name) <= stats_len))

	qlink_for_each_tlv(tlv, data, resp_size) {
		tlv_len = le16_to_cpu(tlv->len);

		switch (le16_to_cpu(tlv->type)) {
		case QTN_TLV_ID_BITMAP:
			map_len = tlv_len;
			map = tlv->val;
			break;
		case QTN_TLV_ID_STA_STATS:
			stats_len = tlv_len;
			stats = (const struct qlink_sta_stats *)tlv->val;
			break;
		default:
			break;
		}
	}

	if (!qlink_tlv_parsing_ok(tlv, data, resp_size)) {
		pr_err("Malformed TLV buffer\n");
		return;
	}

	if (!map || !stats)
		return;

	if (qtnf_sta_stat_avail(inactive_time, QLINK_STA_INFO_INACTIVE_TIME)) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_INACTIVE_TIME);
		sinfo->inactive_time = le32_to_cpu(stats->inactive_time);
	}

	if (qtnf_sta_stat_avail(connected_time,
				QLINK_STA_INFO_CONNECTED_TIME)) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_CONNECTED_TIME);
		sinfo->connected_time = le32_to_cpu(stats->connected_time);
	}

	if (qtnf_sta_stat_avail(signal, QLINK_STA_INFO_SIGNAL)) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL);
		sinfo->signal = stats->signal - QLINK_RSSI_OFFSET;
	}

	if (qtnf_sta_stat_avail(signal_avg, QLINK_STA_INFO_SIGNAL_AVG)) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL_AVG);
		sinfo->signal_avg = stats->signal_avg - QLINK_RSSI_OFFSET;
	}

	if (qtnf_sta_stat_avail(rxrate, QLINK_STA_INFO_RX_BITRATE)) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_BITRATE);
		qtnf_sta_info_parse_rate(&sinfo->rxrate, &stats->rxrate);
	}

	if (qtnf_sta_stat_avail(txrate, QLINK_STA_INFO_TX_BITRATE)) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);
		qtnf_sta_info_parse_rate(&sinfo->txrate, &stats->txrate);
	}

	if (qtnf_sta_stat_avail(sta_flags, QLINK_STA_INFO_STA_FLAGS)) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_STA_FLAGS);
		qtnf_sta_info_parse_flags(&sinfo->sta_flags, &stats->sta_flags);
	}

	if (qtnf_sta_stat_avail(rx_bytes, QLINK_STA_INFO_RX_BYTES)) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_BYTES);
		sinfo->rx_bytes = le64_to_cpu(stats->rx_bytes);
	}

	if (qtnf_sta_stat_avail(tx_bytes, QLINK_STA_INFO_TX_BYTES)) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BYTES);
		sinfo->tx_bytes = le64_to_cpu(stats->tx_bytes);
	}

	if (qtnf_sta_stat_avail(rx_bytes, QLINK_STA_INFO_RX_BYTES64)) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_BYTES64);
		sinfo->rx_bytes = le64_to_cpu(stats->rx_bytes);
	}

	if (qtnf_sta_stat_avail(tx_bytes, QLINK_STA_INFO_TX_BYTES64)) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BYTES64);
		sinfo->tx_bytes = le64_to_cpu(stats->tx_bytes);
	}

	if (qtnf_sta_stat_avail(rx_packets, QLINK_STA_INFO_RX_PACKETS)) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_PACKETS);
		sinfo->rx_packets = le32_to_cpu(stats->rx_packets);
	}

	if (qtnf_sta_stat_avail(tx_packets, QLINK_STA_INFO_TX_PACKETS)) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_PACKETS);
		sinfo->tx_packets = le32_to_cpu(stats->tx_packets);
	}

	if (qtnf_sta_stat_avail(rx_beacon, QLINK_STA_INFO_BEACON_RX)) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_BEACON_RX);
		sinfo->rx_beacon = le64_to_cpu(stats->rx_beacon);
	}

	if (qtnf_sta_stat_avail(rx_dropped_misc, QLINK_STA_INFO_RX_DROP_MISC)) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_DROP_MISC);
		sinfo->rx_dropped_misc = le32_to_cpu(stats->rx_dropped_misc);
	}

	if (qtnf_sta_stat_avail(tx_failed, QLINK_STA_INFO_TX_FAILED)) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_FAILED);
		sinfo->tx_failed = le32_to_cpu(stats->tx_failed);
	}

#undef qtnf_sta_stat_avail
}

int qtnf_cmd_get_sta_info(struct qtnf_vif *vif, const u8 *sta_mac,
			  struct station_info *sinfo)
{
	struct sk_buff *cmd_skb, *resp_skb = NULL;
	struct qlink_cmd_get_sta_info *cmd;
	const struct qlink_resp_get_sta_info *resp;
	size_t var_resp_len = 0;
	int ret = 0;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_GET_STA_INFO,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	qtnf_bus_lock(vif->mac->bus);

	cmd = (struct qlink_cmd_get_sta_info *)cmd_skb->data;
	ether_addr_copy(cmd->sta_addr, sta_mac);

	ret = qtnf_cmd_send_with_reply(vif->mac->bus, cmd_skb, &resp_skb,
				       sizeof(*resp), &var_resp_len);
	if (ret)
		goto out;

	resp = (const struct qlink_resp_get_sta_info *)resp_skb->data;

	if (!ether_addr_equal(sta_mac, resp->sta_addr)) {
		pr_err("VIF%u.%u: wrong mac in reply: %pM != %pM\n",
		       vif->mac->macid, vif->vifid, resp->sta_addr, sta_mac);
		ret = -EINVAL;
		goto out;
	}

	qtnf_cmd_sta_info_parse(sinfo, resp->info, var_resp_len);

out:
	qtnf_bus_unlock(vif->mac->bus);
	consume_skb(resp_skb);

	return ret;
}

static int qtnf_cmd_send_add_change_intf(struct qtnf_vif *vif,
					 enum nl80211_iftype iftype,
					 int use4addr,
					 u8 *mac_addr,
					 enum qlink_cmd_type cmd_type)
{
	struct sk_buff *cmd_skb, *resp_skb = NULL;
	struct qlink_cmd_manage_intf *cmd;
	const struct qlink_resp_manage_intf *resp;
	int ret = 0;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    cmd_type,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	qtnf_bus_lock(vif->mac->bus);

	cmd = (struct qlink_cmd_manage_intf *)cmd_skb->data;
	cmd->intf_info.use4addr = use4addr;

	switch (iftype) {
	case NL80211_IFTYPE_AP:
		cmd->intf_info.if_type = cpu_to_le16(QLINK_IFTYPE_AP);
		break;
	case NL80211_IFTYPE_STATION:
		cmd->intf_info.if_type = cpu_to_le16(QLINK_IFTYPE_STATION);
		break;
	default:
		pr_err("VIF%u.%u: unsupported type %d\n", vif->mac->macid,
		       vif->vifid, iftype);
		ret = -EINVAL;
		goto out;
	}

	if (mac_addr)
		ether_addr_copy(cmd->intf_info.mac_addr, mac_addr);
	else
		eth_zero_addr(cmd->intf_info.mac_addr);

	ret = qtnf_cmd_send_with_reply(vif->mac->bus, cmd_skb, &resp_skb,
				       sizeof(*resp), NULL);
	if (ret)
		goto out;

	resp = (const struct qlink_resp_manage_intf *)resp_skb->data;
	ether_addr_copy(vif->mac_addr, resp->intf_info.mac_addr);

out:
	qtnf_bus_unlock(vif->mac->bus);
	consume_skb(resp_skb);

	return ret;
}

int qtnf_cmd_send_add_intf(struct qtnf_vif *vif, enum nl80211_iftype iftype,
			   int use4addr, u8 *mac_addr)
{
	return qtnf_cmd_send_add_change_intf(vif, iftype, use4addr, mac_addr,
			QLINK_CMD_ADD_INTF);
}

int qtnf_cmd_send_change_intf_type(struct qtnf_vif *vif,
				   enum nl80211_iftype iftype,
				   int use4addr,
				   u8 *mac_addr)
{
	int ret;

	ret = qtnf_cmd_send_add_change_intf(vif, iftype, use4addr, mac_addr,
					    QLINK_CMD_CHANGE_INTF);

	/* Regulatory settings may be different for different interface types */
	if (ret == 0 && vif->wdev.iftype != iftype) {
		enum nl80211_band band;
		struct wiphy *wiphy = priv_to_wiphy(vif->mac);

		for (band = 0; band < NUM_NL80211_BANDS; ++band) {
			if (!wiphy->bands[band])
				continue;

			qtnf_cmd_band_info_get(vif->mac, wiphy->bands[band]);
		}
	}

	return ret;
}

int qtnf_cmd_send_del_intf(struct qtnf_vif *vif)
{
	struct sk_buff *cmd_skb;
	struct qlink_cmd_manage_intf *cmd;
	int ret = 0;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_DEL_INTF,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	qtnf_bus_lock(vif->mac->bus);

	cmd = (struct qlink_cmd_manage_intf *)cmd_skb->data;

	switch (vif->wdev.iftype) {
	case NL80211_IFTYPE_AP:
		cmd->intf_info.if_type = cpu_to_le16(QLINK_IFTYPE_AP);
		break;
	case NL80211_IFTYPE_STATION:
		cmd->intf_info.if_type = cpu_to_le16(QLINK_IFTYPE_STATION);
		break;
	default:
		pr_warn("VIF%u.%u: unsupported iftype %d\n", vif->mac->macid,
			vif->vifid, vif->wdev.iftype);
		dev_kfree_skb(cmd_skb);
		ret = -EINVAL;
		goto out;
	}

	eth_zero_addr(cmd->intf_info.mac_addr);

	ret = qtnf_cmd_send(vif->mac->bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(vif->mac->bus);
	return ret;
}

static int
qtnf_cmd_resp_proc_hw_info(struct qtnf_bus *bus,
			   const struct qlink_resp_get_hw_info *resp,
			   size_t info_len)
{
	struct qtnf_hw_info *hwinfo = &bus->hw_info;
	const struct qlink_tlv_hdr *tlv;
	const char *bld_name = NULL;
	const char *bld_rev = NULL;
	const char *bld_type = NULL;
	const char *bld_label = NULL;
	u32 bld_tmstamp = 0;
	u32 plat_id = 0;
	const char *hw_id = NULL;
	const char *calibration_ver = NULL;
	const char *uboot_ver = NULL;
	u32 hw_ver = 0;
	u16 tlv_type;
	u16 tlv_len;

	hwinfo->num_mac = resp->num_mac;
	hwinfo->mac_bitmap = resp->mac_bitmap;
	hwinfo->fw_ver = le32_to_cpu(resp->fw_ver);
	hwinfo->total_tx_chain = resp->total_tx_chain;
	hwinfo->total_rx_chain = resp->total_rx_chain;

	bld_tmstamp = le32_to_cpu(resp->bld_tmstamp);
	plat_id = le32_to_cpu(resp->plat_id);
	hw_ver = le32_to_cpu(resp->hw_ver);

	qlink_for_each_tlv(tlv, resp->info, info_len) {
		tlv_type = le16_to_cpu(tlv->type);
		tlv_len = le16_to_cpu(tlv->len);

		switch (tlv_type) {
		case QTN_TLV_ID_BUILD_NAME:
			bld_name = (const void *)tlv->val;
			break;
		case QTN_TLV_ID_BUILD_REV:
			bld_rev = (const void *)tlv->val;
			break;
		case QTN_TLV_ID_BUILD_TYPE:
			bld_type = (const void *)tlv->val;
			break;
		case QTN_TLV_ID_BUILD_LABEL:
			bld_label = (const void *)tlv->val;
			break;
		case QTN_TLV_ID_HW_ID:
			hw_id = (const void *)tlv->val;
			break;
		case QTN_TLV_ID_CALIBRATION_VER:
			calibration_ver = (const void *)tlv->val;
			break;
		case QTN_TLV_ID_UBOOT_VER:
			uboot_ver = (const void *)tlv->val;
			break;
		case QTN_TLV_ID_BITMAP:
			memcpy(hwinfo->hw_capab, tlv->val,
			       min(sizeof(hwinfo->hw_capab), (size_t)tlv_len));
			break;
		default:
			break;
		}
	}

	if (!qlink_tlv_parsing_ok(tlv, resp->info, info_len)) {
		pr_err("Malformed TLV buffer\n");
		return -EINVAL;
	}

	pr_info("\nBuild name:            %s\n"
		"Build revision:        %s\n"
		"Build type:            %s\n"
		"Build label:           %s\n"
		"Build timestamp:       %lu\n"
		"Platform ID:           %lu\n"
		"Hardware ID:           %s\n"
		"Calibration version:   %s\n"
		"U-Boot version:        %s\n"
		"Hardware version:      0x%08x\n"
		"Qlink ver:             %u.%u\n"
		"MACs map:              %#x\n"
		"Chains Rx-Tx:          %ux%u\n"
		"FW version:            0x%x\n",
		bld_name, bld_rev, bld_type, bld_label,
		(unsigned long)bld_tmstamp,
		(unsigned long)plat_id,
		hw_id, calibration_ver, uboot_ver, hw_ver,
		QLINK_VER_MAJOR(bus->hw_info.ql_proto_ver),
		QLINK_VER_MINOR(bus->hw_info.ql_proto_ver),
		hwinfo->mac_bitmap,
		hwinfo->total_rx_chain, hwinfo->total_tx_chain,
		hwinfo->fw_ver);

	strlcpy(hwinfo->fw_version, bld_label, sizeof(hwinfo->fw_version));
	hwinfo->hw_version = hw_ver;

	return 0;
}

static void
qtnf_parse_wowlan_info(struct qtnf_wmac *mac,
		       const struct qlink_wowlan_capab_data *wowlan)
{
	struct qtnf_mac_info *mac_info = &mac->macinfo;
	const struct qlink_wowlan_support *data1;
	struct wiphy_wowlan_support *supp;

	supp = kzalloc(sizeof(*supp), GFP_KERNEL);
	if (!supp)
		return;

	switch (le16_to_cpu(wowlan->version)) {
	case 0x1:
		data1 = (struct qlink_wowlan_support *)wowlan->data;

		supp->flags = WIPHY_WOWLAN_MAGIC_PKT | WIPHY_WOWLAN_DISCONNECT;
		supp->n_patterns = le32_to_cpu(data1->n_patterns);
		supp->pattern_max_len = le32_to_cpu(data1->pattern_max_len);
		supp->pattern_min_len = le32_to_cpu(data1->pattern_min_len);

		mac_info->wowlan = supp;
		break;
	default:
		pr_warn("MAC%u: unsupported WoWLAN version 0x%x\n",
			mac->macid, le16_to_cpu(wowlan->version));
		kfree(supp);
		break;
	}
}

static int
qtnf_parse_variable_mac_info(struct qtnf_wmac *mac,
			     const struct qlink_resp_get_mac_info *resp,
			     size_t tlv_buf_size)
{
	struct ieee80211_iface_combination *comb = mac->macinfo.if_comb;
	size_t n_comb = 0;
	struct ieee80211_iface_limit *limits;
	const struct qlink_iface_limit_record *rec;
	const struct qlink_iface_limit *lim;
	const struct qlink_wowlan_capab_data *wowlan;
	u16 rec_len;
	u16 tlv_type;
	u16 tlv_value_len;
	const struct qlink_tlv_hdr *tlv;
	u8 *ext_capa = NULL;
	u8 *ext_capa_mask = NULL;
	u8 ext_capa_len = 0;
	u8 ext_capa_mask_len = 0;
	int i = 0;
	struct ieee80211_reg_rule *rule;
	unsigned int rule_idx = 0;
	const struct qlink_tlv_reg_rule *tlv_rule;

	if (WARN_ON(resp->n_reg_rules > NL80211_MAX_SUPP_REG_RULES))
		return -E2BIG;

	mac->rd = kzalloc(struct_size(mac->rd, reg_rules, resp->n_reg_rules),
			  GFP_KERNEL);
	if (!mac->rd)
		return -ENOMEM;

	mac->rd->n_reg_rules = resp->n_reg_rules;
	mac->rd->alpha2[0] = resp->alpha2[0];
	mac->rd->alpha2[1] = resp->alpha2[1];

	switch (resp->dfs_region) {
	case QLINK_DFS_FCC:
		mac->rd->dfs_region = NL80211_DFS_FCC;
		break;
	case QLINK_DFS_ETSI:
		mac->rd->dfs_region = NL80211_DFS_ETSI;
		break;
	case QLINK_DFS_JP:
		mac->rd->dfs_region = NL80211_DFS_JP;
		break;
	case QLINK_DFS_UNSET:
	default:
		mac->rd->dfs_region = NL80211_DFS_UNSET;
		break;
	}

	qlink_for_each_tlv(tlv, resp->var_info, tlv_buf_size) {
		tlv_type = le16_to_cpu(tlv->type);
		tlv_value_len = le16_to_cpu(tlv->len);

		switch (tlv_type) {
		case QTN_TLV_ID_IFACE_LIMIT:
			if (unlikely(!comb)) {
				pr_warn("MAC%u: no combinations advertised\n",
					mac->macid);
				return -EINVAL;
			}

			if (n_comb >= mac->macinfo.n_if_comb) {
				pr_warn("MAC%u: combinations count exceeded\n",
					mac->macid);
				n_comb++;
				break;
			}

			rec = (void *)tlv->val;
			rec_len = sizeof(*rec) + rec->n_limits * sizeof(*lim);

			if (unlikely(tlv_value_len != rec_len)) {
				pr_warn("MAC%u: record %zu size mismatch\n",
					mac->macid, n_comb);
				return -EINVAL;
			}

			limits = kcalloc(rec->n_limits, sizeof(*limits),
					 GFP_KERNEL);
			if (!limits)
				return -ENOMEM;

			comb[n_comb].num_different_channels =
				rec->num_different_channels;
			comb[n_comb].max_interfaces =
				le16_to_cpu(rec->max_interfaces);
			comb[n_comb].n_limits = rec->n_limits;
			comb[n_comb].limits = limits;

			for (i = 0; i < rec->n_limits; i++) {
				lim = &rec->limits[i];
				limits[i].max = le16_to_cpu(lim->max_num);
				limits[i].types =
					qlink_iface_type_to_nl_mask(le16_to_cpu(lim->type));
				pr_debug("MAC%u: comb[%zu]: MAX:%u TYPES:%.4X\n",
					 mac->macid, n_comb,
					 limits[i].max, limits[i].types);
			}

			n_comb++;
			break;
		case WLAN_EID_EXT_CAPABILITY:
			if (unlikely(tlv_value_len > U8_MAX))
				return -EINVAL;
			ext_capa = (u8 *)tlv->val;
			ext_capa_len = tlv_value_len;
			break;
		case QTN_TLV_ID_EXT_CAPABILITY_MASK:
			if (unlikely(tlv_value_len > U8_MAX))
				return -EINVAL;
			ext_capa_mask = (u8 *)tlv->val;
			ext_capa_mask_len = tlv_value_len;
			break;
		case QTN_TLV_ID_WOWLAN_CAPAB:
			if (tlv_value_len < sizeof(*wowlan))
				return -EINVAL;

			wowlan = (void *)tlv->val;
			if (!le16_to_cpu(wowlan->len)) {
				pr_warn("MAC%u: skip empty WoWLAN data\n",
					mac->macid);
				break;
			}

			rec_len = sizeof(*wowlan) + le16_to_cpu(wowlan->len);
			if (unlikely(tlv_value_len != rec_len)) {
				pr_warn("MAC%u: WoWLAN data size mismatch\n",
					mac->macid);
				return -EINVAL;
			}

			kfree(mac->macinfo.wowlan);
			mac->macinfo.wowlan = NULL;
			qtnf_parse_wowlan_info(mac, wowlan);
			break;
		case QTN_TLV_ID_REG_RULE:
			if (rule_idx >= resp->n_reg_rules) {
				pr_warn("unexpected number of rules: %u\n",
					resp->n_reg_rules);
				return -EINVAL;
			}

			if (tlv_value_len != sizeof(*tlv_rule) - sizeof(*tlv)) {
				pr_warn("malformed TLV 0x%.2X; LEN: %u\n",
					tlv_type, tlv_value_len);
				return -EINVAL;
			}

			tlv_rule = (const struct qlink_tlv_reg_rule *)tlv;
			rule = &mac->rd->reg_rules[rule_idx++];
			qlink_utils_regrule_q2nl(rule, tlv_rule);
			break;
		default:
			pr_warn("MAC%u: unknown TLV type %u\n",
				mac->macid, tlv_type);
			break;
		}
	}

	if (!qlink_tlv_parsing_ok(tlv, resp->var_info, tlv_buf_size)) {
		pr_err("Malformed TLV buffer\n");
		return -EINVAL;
	}

	if (mac->macinfo.n_if_comb != n_comb) {
		pr_err("MAC%u: combination mismatch: reported=%zu parsed=%zu\n",
		       mac->macid, mac->macinfo.n_if_comb, n_comb);
		return -EINVAL;
	}

	if (ext_capa_len != ext_capa_mask_len) {
		pr_err("MAC%u: ext_capa/_mask lengths mismatch: %u != %u\n",
		       mac->macid, ext_capa_len, ext_capa_mask_len);
		return -EINVAL;
	}

	if (rule_idx != resp->n_reg_rules) {
		pr_warn("unexpected number of rules: expected %u got %u\n",
			resp->n_reg_rules, rule_idx);
		return -EINVAL;
	}

	if (ext_capa_len > 0) {
		ext_capa = kmemdup(ext_capa, ext_capa_len, GFP_KERNEL);
		if (!ext_capa)
			return -ENOMEM;

		ext_capa_mask =
			kmemdup(ext_capa_mask, ext_capa_mask_len, GFP_KERNEL);
		if (!ext_capa_mask) {
			kfree(ext_capa);
			return -ENOMEM;
		}
	} else {
		ext_capa = NULL;
		ext_capa_mask = NULL;
	}

	qtnf_mac_ext_caps_free(mac);
	mac->macinfo.extended_capabilities = ext_capa;
	mac->macinfo.extended_capabilities_mask = ext_capa_mask;
	mac->macinfo.extended_capabilities_len = ext_capa_len;

	return 0;
}

static int
qtnf_cmd_resp_proc_mac_info(struct qtnf_wmac *mac,
			    const struct qlink_resp_get_mac_info *resp_info)
{
	struct qtnf_mac_info *mac_info;
	struct qtnf_vif *vif;

	qtnf_mac_iface_comb_free(mac);

	mac_info = &mac->macinfo;

	mac_info->bands_cap = resp_info->bands_cap;
	ether_addr_copy(mac->macaddr, resp_info->dev_mac);

	vif = qtnf_mac_get_base_vif(mac);
	if (vif)
		ether_addr_copy(vif->mac_addr, mac->macaddr);
	else
		pr_err("could not get valid base vif\n");

	mac_info->num_tx_chain = resp_info->num_tx_chain;
	mac_info->num_rx_chain = resp_info->num_rx_chain;

	mac_info->max_ap_assoc_sta = le16_to_cpu(resp_info->max_ap_assoc_sta);
	mac_info->radar_detect_widths =
			qlink_chan_width_mask_to_nl(le16_to_cpu(
					resp_info->radar_detect_widths));
	mac_info->max_acl_mac_addrs = le16_to_cpu(resp_info->max_acl_mac_addrs);
	mac_info->frag_thr = le32_to_cpu(resp_info->frag_threshold);
	mac_info->rts_thr = le32_to_cpu(resp_info->rts_threshold);
	mac_info->sretry_limit = resp_info->retry_short;
	mac_info->lretry_limit = resp_info->retry_long;
	mac_info->coverage_class = resp_info->coverage_class;
	mac_info->max_scan_ssids = resp_info->max_scan_ssids;

	memcpy(&mac_info->ht_cap_mod_mask, &resp_info->ht_cap_mod_mask,
	       sizeof(mac_info->ht_cap_mod_mask));
	memcpy(&mac_info->vht_cap_mod_mask, &resp_info->vht_cap_mod_mask,
	       sizeof(mac_info->vht_cap_mod_mask));

	mac_info->n_if_comb = resp_info->n_iface_combinations;
	mac_info->if_comb = kcalloc(mac->macinfo.n_if_comb,
				    sizeof(*mac->macinfo.if_comb),
				    GFP_KERNEL);

	if (!mac->macinfo.if_comb)
		return -ENOMEM;

	return 0;
}

static void qtnf_cmd_resp_band_fill_htcap(const u8 *info,
					  struct ieee80211_sta_ht_cap *bcap)
{
	const struct ieee80211_ht_cap *ht_cap =
		(const struct ieee80211_ht_cap *)info;

	bcap->ht_supported = true;
	bcap->cap = le16_to_cpu(ht_cap->cap_info);
	bcap->ampdu_factor =
		ht_cap->ampdu_params_info & IEEE80211_HT_AMPDU_PARM_FACTOR;
	bcap->ampdu_density =
		(ht_cap->ampdu_params_info & IEEE80211_HT_AMPDU_PARM_DENSITY) >>
		IEEE80211_HT_AMPDU_PARM_DENSITY_SHIFT;
	memcpy(&bcap->mcs, &ht_cap->mcs, sizeof(bcap->mcs));
}

static void qtnf_cmd_resp_band_fill_vhtcap(const u8 *info,
					   struct ieee80211_sta_vht_cap *bcap)
{
	const struct ieee80211_vht_cap *vht_cap =
		(const struct ieee80211_vht_cap *)info;

	bcap->vht_supported = true;
	bcap->cap = le32_to_cpu(vht_cap->vht_cap_info);
	memcpy(&bcap->vht_mcs, &vht_cap->supp_mcs, sizeof(bcap->vht_mcs));
}

static void qtnf_cmd_conv_iftype(struct ieee80211_sband_iftype_data
				  *iftype_data,
				  const struct qlink_sband_iftype_data
				  *qlink_data)
{
	iftype_data->types_mask = le16_to_cpu(qlink_data->types_mask);

	iftype_data->he_cap.has_he = true;
	memcpy(&iftype_data->he_cap.he_cap_elem, &qlink_data->he_cap_elem,
	       sizeof(qlink_data->he_cap_elem));
	memcpy(iftype_data->he_cap.ppe_thres, qlink_data->ppe_thres,
	       ARRAY_SIZE(qlink_data->ppe_thres));

	iftype_data->he_cap.he_mcs_nss_supp.rx_mcs_80 =
		qlink_data->he_mcs_nss_supp.rx_mcs_80;
	iftype_data->he_cap.he_mcs_nss_supp.tx_mcs_80 =
		qlink_data->he_mcs_nss_supp.tx_mcs_80;
	iftype_data->he_cap.he_mcs_nss_supp.rx_mcs_160 =
		qlink_data->he_mcs_nss_supp.rx_mcs_160;
	iftype_data->he_cap.he_mcs_nss_supp.tx_mcs_160 =
		qlink_data->he_mcs_nss_supp.tx_mcs_160;
	iftype_data->he_cap.he_mcs_nss_supp.rx_mcs_80p80 =
		qlink_data->he_mcs_nss_supp.rx_mcs_80p80;
	iftype_data->he_cap.he_mcs_nss_supp.tx_mcs_80p80 =
		qlink_data->he_mcs_nss_supp.tx_mcs_80p80;
}

static int qtnf_cmd_band_fill_iftype(const u8 *data,
				     struct ieee80211_supported_band *band)
{
	unsigned int i;
	struct ieee80211_sband_iftype_data *iftype_data;
	const struct qlink_tlv_iftype_data *tlv =
		(const struct qlink_tlv_iftype_data *)data;
	size_t payload_len = tlv->n_iftype_data * sizeof(*tlv->iftype_data) +
		sizeof(*tlv) -
		sizeof(struct qlink_tlv_hdr);

	if (tlv->hdr.len != cpu_to_le16(payload_len)) {
		pr_err("bad IFTYPE_DATA TLV len %u\n", tlv->hdr.len);
		return -EINVAL;
	}

	kfree(band->iftype_data);
	band->iftype_data = NULL;
	band->n_iftype_data = tlv->n_iftype_data;
	if (band->n_iftype_data == 0)
		return 0;

	iftype_data = kcalloc(band->n_iftype_data, sizeof(*iftype_data),
			      GFP_KERNEL);
	if (!iftype_data) {
		band->n_iftype_data = 0;
		return -ENOMEM;
	}
	band->iftype_data = iftype_data;

	for (i = 0; i < band->n_iftype_data; i++)
		qtnf_cmd_conv_iftype(iftype_data++, &tlv->iftype_data[i]);

	return 0;
}

static int
qtnf_cmd_resp_fill_band_info(struct ieee80211_supported_band *band,
			     struct qlink_resp_band_info_get *resp,
			     size_t payload_len)
{
	u16 tlv_type;
	size_t tlv_dlen;
	const struct qlink_tlv_hdr *tlv;
	const struct qlink_channel *qchan;
	struct ieee80211_channel *chan;
	unsigned int chidx = 0;
	u32 qflags;
	int ret = -EINVAL;

	memset(&band->ht_cap, 0, sizeof(band->ht_cap));
	memset(&band->vht_cap, 0, sizeof(band->vht_cap));

	if (band->channels) {
		if (band->n_channels == resp->num_chans) {
			memset(band->channels, 0,
			       sizeof(*band->channels) * band->n_channels);
		} else {
			kfree(band->channels);
			band->n_channels = 0;
			band->channels = NULL;
		}
	}

	band->n_channels = resp->num_chans;
	if (band->n_channels == 0)
		return 0;

	if (!band->channels)
		band->channels = kcalloc(band->n_channels, sizeof(*chan),
					 GFP_KERNEL);
	if (!band->channels) {
		band->n_channels = 0;
		return -ENOMEM;
	}

	qlink_for_each_tlv(tlv, resp->info, payload_len) {
		tlv_type = le16_to_cpu(tlv->type);
		tlv_dlen = le16_to_cpu(tlv->len);

		switch (tlv_type) {
		case QTN_TLV_ID_CHANNEL:
			if (unlikely(tlv_dlen != sizeof(*qchan))) {
				pr_err("invalid channel TLV len %zu\n",
				       tlv_dlen);
				goto error_ret;
			}

			if (chidx == band->n_channels) {
				pr_err("too many channel TLVs\n");
				goto error_ret;
			}

			qchan = (const struct qlink_channel *)tlv->val;
			chan = &band->channels[chidx++];
			qflags = le32_to_cpu(qchan->flags);

			chan->hw_value = le16_to_cpu(qchan->hw_value);
			chan->band = band->band;
			chan->center_freq = le16_to_cpu(qchan->center_freq);
			chan->max_antenna_gain = (int)qchan->max_antenna_gain;
			chan->max_power = (int)qchan->max_power;
			chan->max_reg_power = (int)qchan->max_reg_power;
			chan->beacon_found = qchan->beacon_found;
			chan->dfs_cac_ms = le32_to_cpu(qchan->dfs_cac_ms);
			chan->flags = 0;

			if (qflags & QLINK_CHAN_DISABLED)
				chan->flags |= IEEE80211_CHAN_DISABLED;

			if (qflags & QLINK_CHAN_NO_IR)
				chan->flags |= IEEE80211_CHAN_NO_IR;

			if (qflags & QLINK_CHAN_NO_HT40PLUS)
				chan->flags |= IEEE80211_CHAN_NO_HT40PLUS;

			if (qflags & QLINK_CHAN_NO_HT40MINUS)
				chan->flags |= IEEE80211_CHAN_NO_HT40MINUS;

			if (qflags & QLINK_CHAN_NO_OFDM)
				chan->flags |= IEEE80211_CHAN_NO_OFDM;

			if (qflags & QLINK_CHAN_NO_80MHZ)
				chan->flags |= IEEE80211_CHAN_NO_80MHZ;

			if (qflags & QLINK_CHAN_NO_160MHZ)
				chan->flags |= IEEE80211_CHAN_NO_160MHZ;

			if (qflags & QLINK_CHAN_INDOOR_ONLY)
				chan->flags |= IEEE80211_CHAN_INDOOR_ONLY;

			if (qflags & QLINK_CHAN_IR_CONCURRENT)
				chan->flags |= IEEE80211_CHAN_IR_CONCURRENT;

			if (qflags & QLINK_CHAN_NO_20MHZ)
				chan->flags |= IEEE80211_CHAN_NO_20MHZ;

			if (qflags & QLINK_CHAN_NO_10MHZ)
				chan->flags |= IEEE80211_CHAN_NO_10MHZ;

			if (qflags & QLINK_CHAN_RADAR) {
				chan->flags |= IEEE80211_CHAN_RADAR;
				chan->dfs_state_entered = jiffies;

				if (qchan->dfs_state == QLINK_DFS_USABLE)
					chan->dfs_state = NL80211_DFS_USABLE;
				else if (qchan->dfs_state ==
					QLINK_DFS_AVAILABLE)
					chan->dfs_state = NL80211_DFS_AVAILABLE;
				else
					chan->dfs_state =
						NL80211_DFS_UNAVAILABLE;
			}

			pr_debug("chan=%d flags=%#x max_pow=%d max_reg_pow=%d\n",
				 chan->hw_value, chan->flags, chan->max_power,
				 chan->max_reg_power);
			break;
		case WLAN_EID_HT_CAPABILITY:
			if (unlikely(tlv_dlen !=
				     sizeof(struct ieee80211_ht_cap))) {
				pr_err("bad HTCAP TLV len %zu\n", tlv_dlen);
				goto error_ret;
			}

			qtnf_cmd_resp_band_fill_htcap(tlv->val, &band->ht_cap);
			break;
		case WLAN_EID_VHT_CAPABILITY:
			if (unlikely(tlv_dlen !=
				     sizeof(struct ieee80211_vht_cap))) {
				pr_err("bad VHTCAP TLV len %zu\n", tlv_dlen);
				goto error_ret;
			}

			qtnf_cmd_resp_band_fill_vhtcap(tlv->val,
						       &band->vht_cap);
			break;
		case QTN_TLV_ID_IFTYPE_DATA:
			ret = qtnf_cmd_band_fill_iftype((const uint8_t *)tlv,
							band);
			if (ret)
				goto error_ret;
			break;
		default:
			pr_warn("unknown TLV type: %#x\n", tlv_type);
			break;
		}
	}

	if (!qlink_tlv_parsing_ok(tlv, resp->info, payload_len)) {
		pr_err("Malformed TLV buffer\n");
		goto error_ret;
	}

	if (band->n_channels != chidx) {
		pr_err("channel count mismatch: reported=%d, parsed=%d\n",
		       band->n_channels, chidx);
		goto error_ret;
	}

	return 0;

error_ret:
	kfree(band->channels);
	band->channels = NULL;
	band->n_channels = 0;

	return ret;
}

int qtnf_cmd_get_mac_info(struct qtnf_wmac *mac)
{
	struct sk_buff *cmd_skb, *resp_skb = NULL;
	const struct qlink_resp_get_mac_info *resp;
	size_t var_data_len = 0;
	int ret = 0;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(mac->macid, QLINK_VIFID_RSVD,
					    QLINK_CMD_MAC_INFO,
					    sizeof(struct qlink_cmd));
	if (!cmd_skb)
		return -ENOMEM;

	qtnf_bus_lock(mac->bus);
	ret = qtnf_cmd_send_with_reply(mac->bus, cmd_skb, &resp_skb,
				       sizeof(*resp), &var_data_len);
	if (ret)
		goto out;

	resp = (const struct qlink_resp_get_mac_info *)resp_skb->data;
	ret = qtnf_cmd_resp_proc_mac_info(mac, resp);
	if (ret)
		goto out;

	ret = qtnf_parse_variable_mac_info(mac, resp, var_data_len);

out:
	qtnf_bus_unlock(mac->bus);
	consume_skb(resp_skb);

	return ret;
}

int qtnf_cmd_get_hw_info(struct qtnf_bus *bus)
{
	struct sk_buff *cmd_skb, *resp_skb = NULL;
	const struct qlink_resp_get_hw_info *resp;
	size_t info_len = 0;
	int ret = 0;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(QLINK_MACID_RSVD, QLINK_VIFID_RSVD,
					    QLINK_CMD_GET_HW_INFO,
					    sizeof(struct qlink_cmd));
	if (!cmd_skb)
		return -ENOMEM;

	qtnf_bus_lock(bus);
	ret = qtnf_cmd_send_with_reply(bus, cmd_skb, &resp_skb,
				       sizeof(*resp), &info_len);
	if (ret)
		goto out;

	resp = (const struct qlink_resp_get_hw_info *)resp_skb->data;
	ret = qtnf_cmd_resp_proc_hw_info(bus, resp, info_len);

out:
	qtnf_bus_unlock(bus);
	consume_skb(resp_skb);

	return ret;
}

int qtnf_cmd_band_info_get(struct qtnf_wmac *mac,
			   struct ieee80211_supported_band *band)
{
	struct sk_buff *cmd_skb, *resp_skb = NULL;
	struct qlink_cmd_band_info_get *cmd;
	struct qlink_resp_band_info_get *resp;
	size_t info_len = 0;
	int ret = 0;
	u8 qband = qlink_utils_band_cfg2q(band->band);

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(mac->macid, 0,
					    QLINK_CMD_BAND_INFO_GET,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	cmd = (struct qlink_cmd_band_info_get *)cmd_skb->data;
	cmd->band = qband;

	qtnf_bus_lock(mac->bus);
	ret = qtnf_cmd_send_with_reply(mac->bus, cmd_skb, &resp_skb,
				       sizeof(*resp), &info_len);
	if (ret)
		goto out;

	resp = (struct qlink_resp_band_info_get *)resp_skb->data;
	if (resp->band != qband) {
		pr_err("MAC%u: reply band %u != cmd band %u\n", mac->macid,
		       resp->band, qband);
		ret = -EINVAL;
		goto out;
	}

	ret = qtnf_cmd_resp_fill_band_info(band, resp, info_len);

out:
	qtnf_bus_unlock(mac->bus);
	consume_skb(resp_skb);

	return ret;
}

int qtnf_cmd_send_update_phy_params(struct qtnf_wmac *mac, u32 changed)
{
	struct wiphy *wiphy = priv_to_wiphy(mac);
	struct sk_buff *cmd_skb;
	int ret = 0;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(mac->macid, 0,
					    QLINK_CMD_PHY_PARAMS_SET,
					    sizeof(struct qlink_cmd));
	if (!cmd_skb)
		return -ENOMEM;

	qtnf_bus_lock(mac->bus);

	if (changed & WIPHY_PARAM_FRAG_THRESHOLD)
		qtnf_cmd_skb_put_tlv_u32(cmd_skb, QTN_TLV_ID_FRAG_THRESH,
					 wiphy->frag_threshold);
	if (changed & WIPHY_PARAM_RTS_THRESHOLD)
		qtnf_cmd_skb_put_tlv_u32(cmd_skb, QTN_TLV_ID_RTS_THRESH,
					 wiphy->rts_threshold);
	if (changed & WIPHY_PARAM_COVERAGE_CLASS)
		qtnf_cmd_skb_put_tlv_u32(cmd_skb, QTN_TLV_ID_COVERAGE_CLASS,
					 wiphy->coverage_class);

	if (changed & WIPHY_PARAM_RETRY_LONG)
		qtnf_cmd_skb_put_tlv_u32(cmd_skb, QTN_TLV_ID_LRETRY_LIMIT,
					 wiphy->retry_long);

	if (changed & WIPHY_PARAM_RETRY_SHORT)
		qtnf_cmd_skb_put_tlv_u32(cmd_skb, QTN_TLV_ID_SRETRY_LIMIT,
					 wiphy->retry_short);

	ret = qtnf_cmd_send(mac->bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(mac->bus);

	return ret;
}

int qtnf_cmd_send_init_fw(struct qtnf_bus *bus)
{
	struct sk_buff *resp_skb = NULL;
	struct qlink_resp_init_fw *resp;
	struct qlink_cmd_init_fw *cmd;
	struct sk_buff *cmd_skb;
	size_t info_len = 0;
	int ret;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(QLINK_MACID_RSVD, QLINK_VIFID_RSVD,
					    QLINK_CMD_FW_INIT,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	cmd = (struct qlink_cmd_init_fw *)cmd_skb->data;
	cmd->qlink_proto_ver = cpu_to_le32(QLINK_PROTO_VER);

	qtnf_bus_lock(bus);
	ret = qtnf_cmd_send_with_reply(bus, cmd_skb, &resp_skb,
				       sizeof(*resp), &info_len);
	qtnf_bus_unlock(bus);

	if (ret)
		goto out;

	resp = (struct qlink_resp_init_fw *)resp_skb->data;
	bus->hw_info.ql_proto_ver = le32_to_cpu(resp->qlink_proto_ver);

out:
	consume_skb(resp_skb);
	return ret;
}

void qtnf_cmd_send_deinit_fw(struct qtnf_bus *bus)
{
	struct sk_buff *cmd_skb;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(QLINK_MACID_RSVD, QLINK_VIFID_RSVD,
					    QLINK_CMD_FW_DEINIT,
					    sizeof(struct qlink_cmd));
	if (!cmd_skb)
		return;

	qtnf_bus_lock(bus);
	qtnf_cmd_send(bus, cmd_skb);
	qtnf_bus_unlock(bus);
}

int qtnf_cmd_send_add_key(struct qtnf_vif *vif, u8 key_index, bool pairwise,
			  const u8 *mac_addr, struct key_params *params)
{
	struct sk_buff *cmd_skb;
	struct qlink_cmd_add_key *cmd;
	int ret = 0;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_ADD_KEY,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	qtnf_bus_lock(vif->mac->bus);

	cmd = (struct qlink_cmd_add_key *)cmd_skb->data;

	if (mac_addr)
		ether_addr_copy(cmd->addr, mac_addr);
	else
		eth_broadcast_addr(cmd->addr);

	cmd->cipher = cpu_to_le32(params->cipher);
	cmd->key_index = key_index;
	cmd->pairwise = pairwise;

	if (params->key && params->key_len > 0)
		qtnf_cmd_skb_put_tlv_arr(cmd_skb, QTN_TLV_ID_KEY,
					 params->key,
					 params->key_len);

	if (params->seq && params->seq_len > 0)
		qtnf_cmd_skb_put_tlv_arr(cmd_skb, QTN_TLV_ID_SEQ,
					 params->seq,
					 params->seq_len);

	ret = qtnf_cmd_send(vif->mac->bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(vif->mac->bus);

	return ret;
}

int qtnf_cmd_send_del_key(struct qtnf_vif *vif, u8 key_index, bool pairwise,
			  const u8 *mac_addr)
{
	struct sk_buff *cmd_skb;
	struct qlink_cmd_del_key *cmd;
	int ret = 0;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_DEL_KEY,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	qtnf_bus_lock(vif->mac->bus);

	cmd = (struct qlink_cmd_del_key *)cmd_skb->data;

	if (mac_addr)
		ether_addr_copy(cmd->addr, mac_addr);
	else
		eth_broadcast_addr(cmd->addr);

	cmd->key_index = key_index;
	cmd->pairwise = pairwise;

	ret = qtnf_cmd_send(vif->mac->bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(vif->mac->bus);

	return ret;
}

int qtnf_cmd_send_set_default_key(struct qtnf_vif *vif, u8 key_index,
				  bool unicast, bool multicast)
{
	struct sk_buff *cmd_skb;
	struct qlink_cmd_set_def_key *cmd;
	int ret = 0;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_SET_DEFAULT_KEY,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	qtnf_bus_lock(vif->mac->bus);

	cmd = (struct qlink_cmd_set_def_key *)cmd_skb->data;
	cmd->key_index = key_index;
	cmd->unicast = unicast;
	cmd->multicast = multicast;

	ret = qtnf_cmd_send(vif->mac->bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(vif->mac->bus);

	return ret;
}

int qtnf_cmd_send_set_default_mgmt_key(struct qtnf_vif *vif, u8 key_index)
{
	struct sk_buff *cmd_skb;
	struct qlink_cmd_set_def_mgmt_key *cmd;
	int ret = 0;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_SET_DEFAULT_MGMT_KEY,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	qtnf_bus_lock(vif->mac->bus);

	cmd = (struct qlink_cmd_set_def_mgmt_key *)cmd_skb->data;
	cmd->key_index = key_index;

	ret = qtnf_cmd_send(vif->mac->bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(vif->mac->bus);

	return ret;
}

static u32 qtnf_encode_sta_flags(u32 flags)
{
	u32 code = 0;

	if (flags & BIT(NL80211_STA_FLAG_AUTHORIZED))
		code |= QLINK_STA_FLAG_AUTHORIZED;
	if (flags & BIT(NL80211_STA_FLAG_SHORT_PREAMBLE))
		code |= QLINK_STA_FLAG_SHORT_PREAMBLE;
	if (flags & BIT(NL80211_STA_FLAG_WME))
		code |= QLINK_STA_FLAG_WME;
	if (flags & BIT(NL80211_STA_FLAG_MFP))
		code |= QLINK_STA_FLAG_MFP;
	if (flags & BIT(NL80211_STA_FLAG_AUTHENTICATED))
		code |= QLINK_STA_FLAG_AUTHENTICATED;
	if (flags & BIT(NL80211_STA_FLAG_TDLS_PEER))
		code |= QLINK_STA_FLAG_TDLS_PEER;
	if (flags & BIT(NL80211_STA_FLAG_ASSOCIATED))
		code |= QLINK_STA_FLAG_ASSOCIATED;
	return code;
}

int qtnf_cmd_send_change_sta(struct qtnf_vif *vif, const u8 *mac,
			     struct station_parameters *params)
{
	struct sk_buff *cmd_skb;
	struct qlink_cmd_change_sta *cmd;
	int ret = 0;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_CHANGE_STA,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	qtnf_bus_lock(vif->mac->bus);

	cmd = (struct qlink_cmd_change_sta *)cmd_skb->data;
	ether_addr_copy(cmd->sta_addr, mac);
	cmd->flag_update.mask =
		cpu_to_le32(qtnf_encode_sta_flags(params->sta_flags_mask));
	cmd->flag_update.value =
		cpu_to_le32(qtnf_encode_sta_flags(params->sta_flags_set));

	switch (vif->wdev.iftype) {
	case NL80211_IFTYPE_AP:
		cmd->if_type = cpu_to_le16(QLINK_IFTYPE_AP);
		break;
	case NL80211_IFTYPE_STATION:
		cmd->if_type = cpu_to_le16(QLINK_IFTYPE_STATION);
		break;
	default:
		pr_err("unsupported iftype %d\n", vif->wdev.iftype);
		dev_kfree_skb(cmd_skb);
		ret = -EINVAL;
		goto out;
	}

	ret = qtnf_cmd_send(vif->mac->bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(vif->mac->bus);

	return ret;
}

int qtnf_cmd_send_del_sta(struct qtnf_vif *vif,
			  struct station_del_parameters *params)
{
	struct sk_buff *cmd_skb;
	struct qlink_cmd_del_sta *cmd;
	int ret = 0;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_DEL_STA,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	qtnf_bus_lock(vif->mac->bus);

	cmd = (struct qlink_cmd_del_sta *)cmd_skb->data;

	if (params->mac)
		ether_addr_copy(cmd->sta_addr, params->mac);
	else
		eth_broadcast_addr(cmd->sta_addr);	/* flush all stations */

	cmd->subtype = params->subtype;
	cmd->reason_code = cpu_to_le16(params->reason_code);

	ret = qtnf_cmd_send(vif->mac->bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(vif->mac->bus);

	return ret;
}

static void qtnf_cmd_channel_tlv_add(struct sk_buff *cmd_skb,
				     const struct ieee80211_channel *sc)
{
	struct qlink_tlv_channel *tlv;
	struct qlink_channel *qch;

	tlv = skb_put_zero(cmd_skb, sizeof(*tlv));
	qch = &tlv->chan;
	tlv->hdr.type = cpu_to_le16(QTN_TLV_ID_CHANNEL);
	tlv->hdr.len = cpu_to_le16(sizeof(*qch));

	qch->center_freq = cpu_to_le16(sc->center_freq);
	qch->hw_value = cpu_to_le16(sc->hw_value);
	qch->band = qlink_utils_band_cfg2q(sc->band);
	qch->max_power = sc->max_power;
	qch->max_reg_power = sc->max_reg_power;
	qch->max_antenna_gain = sc->max_antenna_gain;
	qch->beacon_found = sc->beacon_found;
	qch->dfs_state = qlink_utils_dfs_state_cfg2q(sc->dfs_state);
	qch->flags = cpu_to_le32(qlink_utils_chflags_cfg2q(sc->flags));
}

static void qtnf_cmd_randmac_tlv_add(struct sk_buff *cmd_skb,
				     const u8 *mac_addr,
				     const u8 *mac_addr_mask)
{
	struct qlink_random_mac_addr *randmac;
	struct qlink_tlv_hdr *hdr =
		skb_put(cmd_skb, sizeof(*hdr) + sizeof(*randmac));

	hdr->type = cpu_to_le16(QTN_TLV_ID_RANDOM_MAC_ADDR);
	hdr->len = cpu_to_le16(sizeof(*randmac));
	randmac = (struct qlink_random_mac_addr *)hdr->val;

	memcpy(randmac->mac_addr, mac_addr, ETH_ALEN);
	memcpy(randmac->mac_addr_mask, mac_addr_mask, ETH_ALEN);
}

int qtnf_cmd_send_scan(struct qtnf_wmac *mac)
{
	struct cfg80211_scan_request *scan_req = mac->scan_req;
	u16 dwell_passive = QTNF_SCAN_DWELL_PASSIVE_DEFAULT;
	u16 dwell_active = QTNF_SCAN_DWELL_ACTIVE_DEFAULT;
	struct wireless_dev *wdev = scan_req->wdev;
	struct ieee80211_channel *sc;
	struct qlink_cmd_scan *cmd;
	struct sk_buff *cmd_skb;
	int n_channels = 0;
	u64 flags = 0;
	int count;
	int ret;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(mac->macid, QLINK_VIFID_RSVD,
					    QLINK_CMD_SCAN,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	cmd = (struct qlink_cmd_scan *)cmd_skb->data;

	if (scan_req->duration) {
		dwell_active = scan_req->duration;
		dwell_passive = scan_req->duration;
	} else if (wdev->iftype == NL80211_IFTYPE_STATION &&
		   wdev->current_bss) {
		/* let device select dwell based on traffic conditions */
		dwell_active = QTNF_SCAN_TIME_AUTO;
		dwell_passive = QTNF_SCAN_TIME_AUTO;
	}

	cmd->n_ssids = cpu_to_le16(scan_req->n_ssids);
	for (count = 0; count < scan_req->n_ssids; ++count) {
		qtnf_cmd_skb_put_tlv_arr(cmd_skb, WLAN_EID_SSID,
					 scan_req->ssids[count].ssid,
					 scan_req->ssids[count].ssid_len);
	}

	if (scan_req->ie_len != 0)
		qtnf_cmd_tlv_ie_set_add(cmd_skb, QLINK_IE_SET_PROBE_REQ,
					scan_req->ie, scan_req->ie_len);

	for (count = 0; count < scan_req->n_channels; ++count) {
		sc = scan_req->channels[count];
		if (sc->flags & IEEE80211_CHAN_DISABLED)
			continue;

		pr_debug("[MAC%u] scan chan=%d, freq=%d, flags=%#x\n",
			 mac->macid, sc->hw_value, sc->center_freq,
			 sc->flags);

		qtnf_cmd_channel_tlv_add(cmd_skb, sc);
		++n_channels;
	}

	if (scan_req->flags & NL80211_SCAN_FLAG_FLUSH)
		flags |= QLINK_SCAN_FLAG_FLUSH;

	if (scan_req->duration_mandatory)
		flags |= QLINK_SCAN_FLAG_DURATION_MANDATORY;

	cmd->n_channels = cpu_to_le16(n_channels);
	cmd->active_dwell = cpu_to_le16(dwell_active);
	cmd->passive_dwell = cpu_to_le16(dwell_passive);
	cmd->sample_duration = cpu_to_le16(QTNF_SCAN_SAMPLE_DURATION_DEFAULT);
	cmd->flags = cpu_to_le64(flags);

	pr_debug("[MAC%u] %s scan dwell active=%u passive=%u duration=%u\n",
		 mac->macid,
		 scan_req->duration_mandatory ? "mandatory" : "max",
		 dwell_active, dwell_passive,
		 QTNF_SCAN_SAMPLE_DURATION_DEFAULT);

	if (scan_req->flags & NL80211_SCAN_FLAG_RANDOM_ADDR) {
		pr_debug("[MAC%u] scan with random addr=%pM, mask=%pM\n",
			 mac->macid,
			 scan_req->mac_addr, scan_req->mac_addr_mask);
		qtnf_cmd_randmac_tlv_add(cmd_skb, scan_req->mac_addr,
					 scan_req->mac_addr_mask);
	}

	qtnf_bus_lock(mac->bus);
	ret = qtnf_cmd_send(mac->bus, cmd_skb);
	qtnf_bus_unlock(mac->bus);

	return ret;
}

int qtnf_cmd_send_connect(struct qtnf_vif *vif,
			  struct cfg80211_connect_params *sme)
{
	struct sk_buff *cmd_skb;
	struct qlink_cmd_connect *cmd;
	struct qlink_auth_encr *aen;
	int ret;
	int i;
	u32 connect_flags = 0;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_CONNECT,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	cmd = (struct qlink_cmd_connect *)cmd_skb->data;

	ether_addr_copy(cmd->bssid, vif->bssid);

	if (sme->bssid_hint)
		ether_addr_copy(cmd->bssid_hint, sme->bssid_hint);
	else
		eth_zero_addr(cmd->bssid_hint);

	if (sme->prev_bssid)
		ether_addr_copy(cmd->prev_bssid, sme->prev_bssid);
	else
		eth_zero_addr(cmd->prev_bssid);

	if ((sme->bg_scan_period >= 0) &&
	    (sme->bg_scan_period <= SHRT_MAX))
		cmd->bg_scan_period = cpu_to_le16(sme->bg_scan_period);
	else
		cmd->bg_scan_period = cpu_to_le16(-1); /* use default value */

	if (sme->flags & ASSOC_REQ_DISABLE_HT)
		connect_flags |= QLINK_STA_CONNECT_DISABLE_HT;
	if (sme->flags & ASSOC_REQ_DISABLE_VHT)
		connect_flags |= QLINK_STA_CONNECT_DISABLE_VHT;
	if (sme->flags & ASSOC_REQ_USE_RRM)
		connect_flags |= QLINK_STA_CONNECT_USE_RRM;

	cmd->flags = cpu_to_le32(connect_flags);
	memcpy(&cmd->ht_capa, &sme->ht_capa, sizeof(cmd->ht_capa));
	memcpy(&cmd->ht_capa_mask, &sme->ht_capa_mask,
	       sizeof(cmd->ht_capa_mask));
	memcpy(&cmd->vht_capa, &sme->vht_capa, sizeof(cmd->vht_capa));
	memcpy(&cmd->vht_capa_mask, &sme->vht_capa_mask,
	       sizeof(cmd->vht_capa_mask));
	cmd->pbss = sme->pbss;

	aen = &cmd->aen;
	aen->auth_type = sme->auth_type;
	aen->privacy = !!sme->privacy;
	cmd->mfp = sme->mfp;
	aen->wpa_versions = cpu_to_le32(sme->crypto.wpa_versions);
	aen->cipher_group = cpu_to_le32(sme->crypto.cipher_group);
	aen->n_ciphers_pairwise = cpu_to_le32(sme->crypto.n_ciphers_pairwise);

	for (i = 0; i < QLINK_MAX_NR_CIPHER_SUITES; i++)
		aen->ciphers_pairwise[i] =
			cpu_to_le32(sme->crypto.ciphers_pairwise[i]);

	aen->n_akm_suites = cpu_to_le32(sme->crypto.n_akm_suites);

	for (i = 0; i < QLINK_MAX_NR_AKM_SUITES; i++)
		aen->akm_suites[i] = cpu_to_le32(sme->crypto.akm_suites[i]);

	aen->control_port = sme->crypto.control_port;
	aen->control_port_no_encrypt =
		sme->crypto.control_port_no_encrypt;
	aen->control_port_ethertype =
		cpu_to_le16(be16_to_cpu(sme->crypto.control_port_ethertype));

	qtnf_cmd_skb_put_tlv_arr(cmd_skb, WLAN_EID_SSID, sme->ssid,
				 sme->ssid_len);

	if (sme->ie_len != 0)
		qtnf_cmd_tlv_ie_set_add(cmd_skb, QLINK_IE_SET_ASSOC_REQ,
					sme->ie, sme->ie_len);

	if (sme->channel)
		qtnf_cmd_channel_tlv_add(cmd_skb, sme->channel);

	qtnf_bus_lock(vif->mac->bus);
	ret = qtnf_cmd_send(vif->mac->bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(vif->mac->bus);

	return ret;
}

int qtnf_cmd_send_external_auth(struct qtnf_vif *vif,
				struct cfg80211_external_auth_params *auth)
{
	struct sk_buff *cmd_skb;
	struct qlink_cmd_external_auth *cmd;
	int ret;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_EXTERNAL_AUTH,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	cmd = (struct qlink_cmd_external_auth *)cmd_skb->data;

	ether_addr_copy(cmd->peer, auth->bssid);
	cmd->status = cpu_to_le16(auth->status);

	qtnf_bus_lock(vif->mac->bus);
	ret = qtnf_cmd_send(vif->mac->bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(vif->mac->bus);

	return ret;
}

int qtnf_cmd_send_disconnect(struct qtnf_vif *vif, u16 reason_code)
{
	struct sk_buff *cmd_skb;
	struct qlink_cmd_disconnect *cmd;
	int ret;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_DISCONNECT,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	qtnf_bus_lock(vif->mac->bus);

	cmd = (struct qlink_cmd_disconnect *)cmd_skb->data;
	cmd->reason = cpu_to_le16(reason_code);

	ret = qtnf_cmd_send(vif->mac->bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(vif->mac->bus);

	return ret;
}

int qtnf_cmd_send_updown_intf(struct qtnf_vif *vif, bool up)
{
	struct sk_buff *cmd_skb;
	struct qlink_cmd_updown *cmd;
	int ret;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_UPDOWN_INTF,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	cmd = (struct qlink_cmd_updown *)cmd_skb->data;
	cmd->if_up = !!up;

	qtnf_bus_lock(vif->mac->bus);
	ret = qtnf_cmd_send(vif->mac->bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(vif->mac->bus);

	return ret;
}

int qtnf_cmd_reg_notify(struct qtnf_wmac *mac, struct regulatory_request *req,
			bool slave_radar, bool dfs_offload)
{
	struct wiphy *wiphy = priv_to_wiphy(mac);
	struct qtnf_bus *bus = mac->bus;
	struct sk_buff *cmd_skb;
	int ret;
	struct qlink_cmd_reg_notify *cmd;
	enum nl80211_band band;
	const struct ieee80211_supported_band *cfg_band;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(mac->macid, QLINK_VIFID_RSVD,
					    QLINK_CMD_REG_NOTIFY,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	cmd = (struct qlink_cmd_reg_notify *)cmd_skb->data;
	cmd->alpha2[0] = req->alpha2[0];
	cmd->alpha2[1] = req->alpha2[1];

	switch (req->initiator) {
	case NL80211_REGDOM_SET_BY_CORE:
		cmd->initiator = QLINK_REGDOM_SET_BY_CORE;
		break;
	case NL80211_REGDOM_SET_BY_USER:
		cmd->initiator = QLINK_REGDOM_SET_BY_USER;
		break;
	case NL80211_REGDOM_SET_BY_DRIVER:
		cmd->initiator = QLINK_REGDOM_SET_BY_DRIVER;
		break;
	case NL80211_REGDOM_SET_BY_COUNTRY_IE:
		cmd->initiator = QLINK_REGDOM_SET_BY_COUNTRY_IE;
		break;
	}

	switch (req->user_reg_hint_type) {
	case NL80211_USER_REG_HINT_USER:
		cmd->user_reg_hint_type = QLINK_USER_REG_HINT_USER;
		break;
	case NL80211_USER_REG_HINT_CELL_BASE:
		cmd->user_reg_hint_type = QLINK_USER_REG_HINT_CELL_BASE;
		break;
	case NL80211_USER_REG_HINT_INDOOR:
		cmd->user_reg_hint_type = QLINK_USER_REG_HINT_INDOOR;
		break;
	}

	switch (req->dfs_region) {
	case NL80211_DFS_FCC:
		cmd->dfs_region = QLINK_DFS_FCC;
		break;
	case NL80211_DFS_ETSI:
		cmd->dfs_region = QLINK_DFS_ETSI;
		break;
	case NL80211_DFS_JP:
		cmd->dfs_region = QLINK_DFS_JP;
		break;
	default:
		cmd->dfs_region = QLINK_DFS_UNSET;
		break;
	}

	cmd->slave_radar = slave_radar;
	cmd->dfs_offload = dfs_offload;
	cmd->num_channels = 0;

	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		unsigned int i;

		cfg_band = wiphy->bands[band];
		if (!cfg_band)
			continue;

		cmd->num_channels += cfg_band->n_channels;

		for (i = 0; i < cfg_band->n_channels; ++i) {
			qtnf_cmd_channel_tlv_add(cmd_skb,
						 &cfg_band->channels[i]);
		}
	}

	qtnf_bus_lock(bus);
	ret = qtnf_cmd_send(bus, cmd_skb);
	qtnf_bus_unlock(bus);

	return ret;
}

static int
qtnf_cmd_resp_proc_chan_stat_info(struct survey_info *survey,
				  const u8 *payload, size_t payload_len)
{
	const struct qlink_chan_stats *stats = NULL;
	const struct qlink_tlv_hdr *tlv;
	u16 tlv_value_len;
	u16 tlv_type;
	const u8 *map = NULL;
	unsigned int map_len = 0;
	unsigned int stats_len = 0;

	qlink_for_each_tlv(tlv, payload, payload_len) {
		tlv_type = le16_to_cpu(tlv->type);
		tlv_value_len = le16_to_cpu(tlv->len);

		switch (tlv_type) {
		case QTN_TLV_ID_BITMAP:
			map = tlv->val;
			map_len = tlv_value_len;
			break;
		case QTN_TLV_ID_CHANNEL_STATS:
			stats = (struct qlink_chan_stats *)tlv->val;
			stats_len = tlv_value_len;
			break;
		default:
			pr_info("Unknown TLV type: %#x\n", tlv_type);
			break;
		}
	}

	if (!qlink_tlv_parsing_ok(tlv, payload, payload_len)) {
		pr_err("Malformed TLV buffer\n");
		return -EINVAL;
	}

	if (!map || !stats)
		return 0;

#define qtnf_chan_stat_avail(stat_name, bitn)	\
	(qtnf_utils_is_bit_set(map, bitn, map_len) && \
	 (offsetofend(struct qlink_chan_stats, stat_name) <= stats_len))

	if (qtnf_chan_stat_avail(time_on, QLINK_CHAN_STAT_TIME_ON)) {
		survey->filled |= SURVEY_INFO_TIME;
		survey->time = le64_to_cpu(stats->time_on);
	}

	if (qtnf_chan_stat_avail(time_tx, QLINK_CHAN_STAT_TIME_TX)) {
		survey->filled |= SURVEY_INFO_TIME_TX;
		survey->time_tx = le64_to_cpu(stats->time_tx);
	}

	if (qtnf_chan_stat_avail(time_rx, QLINK_CHAN_STAT_TIME_RX)) {
		survey->filled |= SURVEY_INFO_TIME_RX;
		survey->time_rx = le64_to_cpu(stats->time_rx);
	}

	if (qtnf_chan_stat_avail(cca_busy, QLINK_CHAN_STAT_CCA_BUSY)) {
		survey->filled |= SURVEY_INFO_TIME_BUSY;
		survey->time_busy = le64_to_cpu(stats->cca_busy);
	}

	if (qtnf_chan_stat_avail(cca_busy_ext, QLINK_CHAN_STAT_CCA_BUSY_EXT)) {
		survey->filled |= SURVEY_INFO_TIME_EXT_BUSY;
		survey->time_ext_busy = le64_to_cpu(stats->cca_busy_ext);
	}

	if (qtnf_chan_stat_avail(time_scan, QLINK_CHAN_STAT_TIME_SCAN)) {
		survey->filled |= SURVEY_INFO_TIME_SCAN;
		survey->time_scan = le64_to_cpu(stats->time_scan);
	}

	if (qtnf_chan_stat_avail(chan_noise, QLINK_CHAN_STAT_CHAN_NOISE)) {
		survey->filled |= SURVEY_INFO_NOISE_DBM;
		survey->noise = stats->chan_noise;
	}

#undef qtnf_chan_stat_avail

	return 0;
}

int qtnf_cmd_get_chan_stats(struct qtnf_wmac *mac, u32 chan_freq,
			    struct survey_info *survey)
{
	struct sk_buff *cmd_skb, *resp_skb = NULL;
	struct qlink_cmd_get_chan_stats *cmd;
	struct qlink_resp_get_chan_stats *resp;
	size_t var_data_len = 0;
	int ret = 0;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(mac->macid, QLINK_VIFID_RSVD,
					    QLINK_CMD_CHAN_STATS,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	cmd = (struct qlink_cmd_get_chan_stats *)cmd_skb->data;
	cmd->channel_freq = cpu_to_le32(chan_freq);

	qtnf_bus_lock(mac->bus);
	ret = qtnf_cmd_send_with_reply(mac->bus, cmd_skb, &resp_skb,
				       sizeof(*resp), &var_data_len);
	qtnf_bus_unlock(mac->bus);

	if (ret)
		goto out;

	resp = (struct qlink_resp_get_chan_stats *)resp_skb->data;

	if (le32_to_cpu(resp->chan_freq) != chan_freq) {
		pr_err("[MAC%u] channel stats freq %u != requested %u\n",
		       mac->macid, le32_to_cpu(resp->chan_freq), chan_freq);
		ret = -EINVAL;
		goto out;
	}

	ret = qtnf_cmd_resp_proc_chan_stat_info(survey, resp->info,
						var_data_len);

out:
	consume_skb(resp_skb);

	return ret;
}

int qtnf_cmd_send_chan_switch(struct qtnf_vif *vif,
			      struct cfg80211_csa_settings *params)
{
	struct qtnf_wmac *mac = vif->mac;
	struct qlink_cmd_chan_switch *cmd;
	struct sk_buff *cmd_skb;
	int ret;
	u64 flags = 0;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(mac->macid, vif->vifid,
					    QLINK_CMD_CHAN_SWITCH,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	if (params->radar_required)
		flags |= QLINK_CHAN_SW_RADAR_REQUIRED;

	if (params->block_tx)
		flags |= QLINK_CHAN_SW_BLOCK_TX;

	cmd = (struct qlink_cmd_chan_switch *)cmd_skb->data;
	qlink_chandef_cfg2q(&params->chandef, &cmd->channel);
	cmd->flags = cpu_to_le64(flags);
	cmd->beacon_count = params->count;

	qtnf_bus_lock(mac->bus);
	ret = qtnf_cmd_send(mac->bus, cmd_skb);
	qtnf_bus_unlock(mac->bus);

	return ret;
}

int qtnf_cmd_get_channel(struct qtnf_vif *vif, struct cfg80211_chan_def *chdef)
{
	struct qtnf_bus *bus = vif->mac->bus;
	const struct qlink_resp_channel_get *resp;
	struct sk_buff *cmd_skb;
	struct sk_buff *resp_skb = NULL;
	int ret;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_CHAN_GET,
					    sizeof(struct qlink_cmd));
	if (!cmd_skb)
		return -ENOMEM;

	qtnf_bus_lock(bus);
	ret = qtnf_cmd_send_with_reply(bus, cmd_skb, &resp_skb,
				       sizeof(*resp), NULL);
	if (ret)
		goto out;

	resp = (const struct qlink_resp_channel_get *)resp_skb->data;
	qlink_chandef_q2cfg(priv_to_wiphy(vif->mac), &resp->chan, chdef);

out:
	qtnf_bus_unlock(bus);
	consume_skb(resp_skb);

	return ret;
}

int qtnf_cmd_start_cac(const struct qtnf_vif *vif,
		       const struct cfg80211_chan_def *chdef,
		       u32 cac_time_ms)
{
	struct qtnf_bus *bus = vif->mac->bus;
	struct sk_buff *cmd_skb;
	struct qlink_cmd_start_cac *cmd;
	int ret;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_START_CAC,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	cmd = (struct qlink_cmd_start_cac *)cmd_skb->data;
	cmd->cac_time_ms = cpu_to_le32(cac_time_ms);
	qlink_chandef_cfg2q(chdef, &cmd->chan);

	qtnf_bus_lock(bus);
	ret = qtnf_cmd_send(bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(bus);

	return ret;
}

int qtnf_cmd_set_mac_acl(const struct qtnf_vif *vif,
			 const struct cfg80211_acl_data *params)
{
	struct qtnf_bus *bus = vif->mac->bus;
	struct sk_buff *cmd_skb;
	struct qlink_tlv_hdr *tlv;
	size_t acl_size = struct_size(params, mac_addrs, params->n_acl_entries);
	int ret;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_SET_MAC_ACL,
					    sizeof(struct qlink_cmd));
	if (!cmd_skb)
		return -ENOMEM;

	tlv = skb_put(cmd_skb, sizeof(*tlv) + round_up(acl_size, QLINK_ALIGN));
	tlv->type = cpu_to_le16(QTN_TLV_ID_ACL_DATA);
	tlv->len = cpu_to_le16(acl_size);
	qlink_acl_data_cfg2q(params, (struct qlink_acl_data *)tlv->val);

	qtnf_bus_lock(bus);
	ret = qtnf_cmd_send(bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(bus);

	return ret;
}

int qtnf_cmd_send_pm_set(const struct qtnf_vif *vif, u8 pm_mode, int timeout)
{
	struct qtnf_bus *bus = vif->mac->bus;
	struct sk_buff *cmd_skb;
	struct qlink_cmd_pm_set *cmd;
	int ret = 0;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_PM_SET, sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	cmd = (struct qlink_cmd_pm_set *)cmd_skb->data;
	cmd->pm_mode = pm_mode;
	cmd->pm_standby_timer = cpu_to_le32(timeout);

	qtnf_bus_lock(bus);

	ret = qtnf_cmd_send(bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(bus);

	return ret;
}

int qtnf_cmd_get_tx_power(const struct qtnf_vif *vif, int *dbm)
{
	struct qtnf_bus *bus = vif->mac->bus;
	const struct qlink_resp_txpwr *resp;
	struct sk_buff *resp_skb = NULL;
	struct qlink_cmd_txpwr *cmd;
	struct sk_buff *cmd_skb;
	int ret = 0;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_TXPWR, sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	cmd = (struct qlink_cmd_txpwr *)cmd_skb->data;
	cmd->op_type = QLINK_TXPWR_GET;

	qtnf_bus_lock(bus);

	ret = qtnf_cmd_send_with_reply(bus, cmd_skb, &resp_skb,
				       sizeof(*resp), NULL);
	if (ret)
		goto out;

	resp = (const struct qlink_resp_txpwr *)resp_skb->data;
	*dbm = MBM_TO_DBM(le32_to_cpu(resp->txpwr));

out:
	qtnf_bus_unlock(bus);
	consume_skb(resp_skb);

	return ret;
}

int qtnf_cmd_set_tx_power(const struct qtnf_vif *vif,
			  enum nl80211_tx_power_setting type, int mbm)
{
	struct qtnf_bus *bus = vif->mac->bus;
	const struct qlink_resp_txpwr *resp;
	struct sk_buff *resp_skb = NULL;
	struct qlink_cmd_txpwr *cmd;
	struct sk_buff *cmd_skb;
	int ret = 0;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_TXPWR, sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	cmd = (struct qlink_cmd_txpwr *)cmd_skb->data;
	cmd->op_type = QLINK_TXPWR_SET;
	cmd->txpwr_setting = type;
	cmd->txpwr = cpu_to_le32(mbm);

	qtnf_bus_lock(bus);

	ret = qtnf_cmd_send_with_reply(bus, cmd_skb, &resp_skb,
				       sizeof(*resp), NULL);

	qtnf_bus_unlock(bus);
	consume_skb(resp_skb);

	return ret;
}

int qtnf_cmd_send_wowlan_set(const struct qtnf_vif *vif,
			     const struct cfg80211_wowlan *wowl)
{
	struct qtnf_bus *bus = vif->mac->bus;
	struct sk_buff *cmd_skb;
	struct qlink_cmd_wowlan_set *cmd;
	u32 triggers = 0;
	int count = 0;
	int ret = 0;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_WOWLAN_SET, sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	qtnf_bus_lock(bus);

	cmd = (struct qlink_cmd_wowlan_set *)cmd_skb->data;

	if (wowl) {
		if (wowl->disconnect)
			triggers |=  QLINK_WOWLAN_TRIG_DISCONNECT;

		if (wowl->magic_pkt)
			triggers |= QLINK_WOWLAN_TRIG_MAGIC_PKT;

		if (wowl->n_patterns && wowl->patterns) {
			triggers |= QLINK_WOWLAN_TRIG_PATTERN_PKT;
			while (count < wowl->n_patterns) {
				qtnf_cmd_skb_put_tlv_arr(cmd_skb,
					QTN_TLV_ID_WOWLAN_PATTERN,
					wowl->patterns[count].pattern,
					wowl->patterns[count].pattern_len);
				count++;
			}
		}
	}

	cmd->triggers = cpu_to_le32(triggers);

	ret = qtnf_cmd_send(bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(bus);
	return ret;
}

int qtnf_cmd_netdev_changeupper(const struct qtnf_vif *vif, int br_domain)
{
	struct qtnf_bus *bus = vif->mac->bus;
	struct sk_buff *cmd_skb;
	struct qlink_cmd_ndev_changeupper *cmd;
	int ret;

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_NDEV_EVENT,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	pr_debug("[VIF%u.%u] set broadcast domain to %d\n",
		 vif->mac->macid, vif->vifid, br_domain);

	cmd = (struct qlink_cmd_ndev_changeupper *)cmd_skb->data;
	cmd->nehdr.event = cpu_to_le16(QLINK_NDEV_EVENT_CHANGEUPPER);
	cmd->upper_type = QLINK_NDEV_UPPER_TYPE_BRIDGE;
	cmd->br_domain = cpu_to_le32(br_domain);

	qtnf_bus_lock(bus);
	ret = qtnf_cmd_send(bus, cmd_skb);
	qtnf_bus_unlock(bus);

	if (ret)
		pr_err("[VIF%u.%u] failed to set broadcast domain\n",
		       vif->mac->macid, vif->vifid);

	return ret;
}

int qtnf_cmd_send_update_owe(struct qtnf_vif *vif,
			     struct cfg80211_update_owe_info *owe)
{
	struct qlink_cmd_update_owe *cmd;
	struct sk_buff *cmd_skb;
	int ret;

	if (sizeof(*cmd) + owe->ie_len > QTNF_MAX_CMD_BUF_SIZE) {
		pr_warn("VIF%u.%u: OWE update IEs too big: %zu\n",
			vif->mac->macid, vif->vifid, owe->ie_len);
		return -E2BIG;
	}

	cmd_skb = qtnf_cmd_alloc_new_cmdskb(vif->mac->macid, vif->vifid,
					    QLINK_CMD_UPDATE_OWE,
					    sizeof(*cmd));
	if (!cmd_skb)
		return -ENOMEM;

	cmd = (struct qlink_cmd_update_owe *)cmd_skb->data;
	ether_addr_copy(cmd->peer, owe->peer);
	cmd->status = cpu_to_le16(owe->status);
	if (owe->ie_len && owe->ie)
		qtnf_cmd_skb_put_buffer(cmd_skb, owe->ie, owe->ie_len);

	qtnf_bus_lock(vif->mac->bus);
	ret = qtnf_cmd_send(vif->mac->bus, cmd_skb);
	if (ret)
		goto out;

out:
	qtnf_bus_unlock(vif->mac->bus);

	return ret;
}
