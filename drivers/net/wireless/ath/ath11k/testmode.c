// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "testmode.h"
#include <net/netlink.h>
#include "debug.h"
#include "wmi.h"
#include "hw.h"
#include "core.h"
#include "testmode_i.h"

#define ATH11K_FTM_SEGHDR_CURRENT_SEQ		GENMASK(3, 0)
#define ATH11K_FTM_SEGHDR_TOTAL_SEGMENTS	GENMASK(7, 4)

static const struct nla_policy ath11k_tm_policy[ATH11K_TM_ATTR_MAX + 1] = {
	[ATH11K_TM_ATTR_CMD]		= { .type = NLA_U32 },
	[ATH11K_TM_ATTR_DATA]		= { .type = NLA_BINARY,
					    .len = ATH11K_TM_DATA_MAX_LEN },
	[ATH11K_TM_ATTR_WMI_CMDID]	= { .type = NLA_U32 },
	[ATH11K_TM_ATTR_VERSION_MAJOR]	= { .type = NLA_U32 },
	[ATH11K_TM_ATTR_VERSION_MINOR]	= { .type = NLA_U32 },
};

static struct ath11k *ath11k_tm_get_ar(struct ath11k_base *ab)
{
	struct ath11k_pdev *pdev;
	struct ath11k *ar = NULL;
	int i;

	for (i = 0; i < ab->num_radios; i++) {
		pdev = &ab->pdevs[i];
		ar = pdev->ar;

		if (ar && ar->state == ATH11K_STATE_FTM)
			break;
	}

	return ar;
}

/* This function handles unsegmented events. Data in various events are aggregated
 * in application layer, this event is unsegmented from host perspective.
 */
static void ath11k_tm_wmi_event_unsegmented(struct ath11k_base *ab, u32 cmd_id,
					    struct sk_buff *skb)
{
	struct sk_buff *nl_skb;
	struct ath11k *ar;

	ath11k_dbg(ab, ATH11K_DBG_TESTMODE,
		   "event wmi cmd_id %d skb length %d\n",
		   cmd_id, skb->len);
	ath11k_dbg_dump(ab, ATH11K_DBG_TESTMODE, NULL, "", skb->data, skb->len);

	ar = ath11k_tm_get_ar(ab);
	if (!ar) {
		ath11k_warn(ab, "testmode event not handled due to invalid pdev\n");
		return;
	}

	spin_lock_bh(&ar->data_lock);

	nl_skb = cfg80211_testmode_alloc_event_skb(ar->hw->wiphy,
						   2 * nla_total_size(sizeof(u32)) +
						   nla_total_size(skb->len),
						   GFP_ATOMIC);
	if (!nl_skb) {
		ath11k_warn(ab,
			    "failed to allocate skb for unsegmented testmode wmi event\n");
		goto out;
	}

	if (nla_put_u32(nl_skb, ATH11K_TM_ATTR_CMD, ATH11K_TM_CMD_WMI) ||
	    nla_put_u32(nl_skb, ATH11K_TM_ATTR_WMI_CMDID, cmd_id) ||
	    nla_put(nl_skb, ATH11K_TM_ATTR_DATA, skb->len, skb->data)) {
		ath11k_warn(ab, "failed to populate testmode unsegmented event\n");
		kfree_skb(nl_skb);
		goto out;
	}

	cfg80211_testmode_event(nl_skb, GFP_ATOMIC);
	spin_unlock_bh(&ar->data_lock);
	return;

out:
	spin_unlock_bh(&ar->data_lock);
	ath11k_warn(ab, "Failed to send testmode event to higher layers\n");
}

/* This function handles segmented events. Data of various events received
 * from firmware is aggregated and sent to application layer
 */
static int ath11k_tm_process_event(struct ath11k_base *ab, u32 cmd_id,
				   const struct wmi_ftm_event_msg *ftm_msg,
				   u16 length)
{
	struct sk_buff *nl_skb;
	int ret = 0;
	struct ath11k *ar;
	u8 const *buf_pos;
	u16 datalen;
	u8 total_segments, current_seq;
	u32 data_pos;
	u32 pdev_id;

	ath11k_dbg(ab, ATH11K_DBG_TESTMODE,
		   "event wmi cmd_id %d ftm event msg %pK datalen %d\n",
		   cmd_id, ftm_msg, length);
	ath11k_dbg_dump(ab, ATH11K_DBG_TESTMODE, NULL, "", ftm_msg, length);
	pdev_id = DP_HW2SW_MACID(ftm_msg->seg_hdr.pdev_id);

	if (pdev_id >= ab->num_radios) {
		ath11k_warn(ab, "testmode event not handled due to invalid pdev id: %d\n",
			    pdev_id);
		return -EINVAL;
	}

	ar = ab->pdevs[pdev_id].ar;
	if (!ar) {
		ath11k_warn(ab, "testmode event not handled due to absence of pdev\n");
		return -ENODEV;
	}

	current_seq = FIELD_GET(ATH11K_FTM_SEGHDR_CURRENT_SEQ,
				ftm_msg->seg_hdr.segmentinfo);
	total_segments = FIELD_GET(ATH11K_FTM_SEGHDR_TOTAL_SEGMENTS,
				   ftm_msg->seg_hdr.segmentinfo);
	datalen = length - (sizeof(struct wmi_ftm_seg_hdr));
	buf_pos = ftm_msg->data;

	spin_lock_bh(&ar->data_lock);

	if (current_seq == 0) {
		ab->testmode.expected_seq = 0;
		ab->testmode.data_pos = 0;
	}

	data_pos = ab->testmode.data_pos;

	if ((data_pos + datalen) > ATH11K_FTM_EVENT_MAX_BUF_LENGTH) {
		ath11k_warn(ab, "Invalid ftm event length at %d: %d\n",
			    data_pos, datalen);
		ret = -EINVAL;
		goto out;
	}

	memcpy(&ab->testmode.eventdata[data_pos], buf_pos, datalen);
	data_pos += datalen;

	if (++ab->testmode.expected_seq != total_segments) {
		ab->testmode.data_pos = data_pos;
		ath11k_dbg(ab, ATH11K_DBG_TESTMODE,
			   "partial data received current_seq %d total_seg %d\n",
			    current_seq, total_segments);
		goto out;
	}

	ath11k_dbg(ab, ATH11K_DBG_TESTMODE,
		   "total data length pos %d len %d\n",
		    data_pos, ftm_msg->seg_hdr.len);
	nl_skb = cfg80211_testmode_alloc_event_skb(ar->hw->wiphy,
						   2 * nla_total_size(sizeof(u32)) +
						   nla_total_size(data_pos),
						   GFP_ATOMIC);
	if (!nl_skb) {
		ath11k_warn(ab,
			    "failed to allocate skb for segmented testmode wmi event\n");
		ret = -ENOMEM;
		goto out;
	}

	if (nla_put_u32(nl_skb, ATH11K_TM_ATTR_CMD,
			ATH11K_TM_CMD_WMI_FTM) ||
	    nla_put_u32(nl_skb, ATH11K_TM_ATTR_WMI_CMDID, cmd_id) ||
	    nla_put(nl_skb, ATH11K_TM_ATTR_DATA, data_pos,
		    &ab->testmode.eventdata[0])) {
		ath11k_warn(ab, "failed to populate segmented testmode event");
		kfree_skb(nl_skb);
		ret = -ENOBUFS;
		goto out;
	}

	cfg80211_testmode_event(nl_skb, GFP_ATOMIC);

out:
	spin_unlock_bh(&ar->data_lock);
	return ret;
}

static void ath11k_tm_wmi_event_segmented(struct ath11k_base *ab, u32 cmd_id,
					  struct sk_buff *skb)
{
	const void **tb;
	const struct wmi_ftm_event_msg *ev;
	u16 length;
	int ret;

	tb = ath11k_wmi_tlv_parse_alloc(ab, skb->data, skb->len, GFP_ATOMIC);
	if (IS_ERR(tb)) {
		ret = PTR_ERR(tb);
		ath11k_warn(ab, "failed to parse ftm event tlv: %d\n", ret);
		return;
	}

	ev = tb[WMI_TAG_ARRAY_BYTE];
	if (!ev) {
		ath11k_warn(ab, "failed to fetch ftm msg\n");
		kfree(tb);
		return;
	}

	length = skb->len - TLV_HDR_SIZE;
	ret = ath11k_tm_process_event(ab, cmd_id, ev, length);
	if (ret)
		ath11k_warn(ab, "Failed to process ftm event\n");

	kfree(tb);
}

void ath11k_tm_wmi_event(struct ath11k_base *ab, u32 cmd_id, struct sk_buff *skb)
{
	if (test_bit(ATH11K_FLAG_FTM_SEGMENTED, &ab->dev_flags))
		ath11k_tm_wmi_event_segmented(ab, cmd_id, skb);
	else
		ath11k_tm_wmi_event_unsegmented(ab, cmd_id, skb);
}

static int ath11k_tm_cmd_get_version(struct ath11k *ar, struct nlattr *tb[])
{
	struct sk_buff *skb;
	int ret;

	ath11k_dbg(ar->ab, ATH11K_DBG_TESTMODE,
		   "cmd get version_major %d version_minor %d\n",
		   ATH11K_TESTMODE_VERSION_MAJOR,
		   ATH11K_TESTMODE_VERSION_MINOR);

	skb = cfg80211_testmode_alloc_reply_skb(ar->hw->wiphy,
						nla_total_size(sizeof(u32)));
	if (!skb)
		return -ENOMEM;

	ret = nla_put_u32(skb, ATH11K_TM_ATTR_VERSION_MAJOR,
			  ATH11K_TESTMODE_VERSION_MAJOR);
	if (ret) {
		kfree_skb(skb);
		return ret;
	}

	ret = nla_put_u32(skb, ATH11K_TM_ATTR_VERSION_MINOR,
			  ATH11K_TESTMODE_VERSION_MINOR);
	if (ret) {
		kfree_skb(skb);
		return ret;
	}

	return cfg80211_testmode_reply(skb);
}

static int ath11k_tm_cmd_testmode_start(struct ath11k *ar, struct nlattr *tb[])
{
	int ret;

	mutex_lock(&ar->conf_mutex);

	if (ar->state == ATH11K_STATE_FTM) {
		ret = -EALREADY;
		goto err;
	}

	/* start utf only when the driver is not in use  */
	if (ar->state != ATH11K_STATE_OFF) {
		ret = -EBUSY;
		goto err;
	}

	ar->ab->testmode.eventdata = kzalloc(ATH11K_FTM_EVENT_MAX_BUF_LENGTH,
					     GFP_KERNEL);
	if (!ar->ab->testmode.eventdata) {
		ret = -ENOMEM;
		goto err;
	}

	ar->state = ATH11K_STATE_FTM;
	ar->ftm_msgref = 0;

	mutex_unlock(&ar->conf_mutex);

	ath11k_dbg(ar->ab, ATH11K_DBG_TESTMODE, "cmd start\n");
	return 0;

err:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static int ath11k_tm_cmd_wmi(struct ath11k *ar, struct nlattr *tb[],
			     struct ieee80211_vif *vif)
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct sk_buff *skb;
	struct ath11k_vif *arvif;
	u32 cmd_id, buf_len;
	int ret, tag;
	void *buf;
	u32 *ptr;

	mutex_lock(&ar->conf_mutex);

	if (!tb[ATH11K_TM_ATTR_DATA]) {
		ret = -EINVAL;
		goto out;
	}

	if (!tb[ATH11K_TM_ATTR_WMI_CMDID]) {
		ret = -EINVAL;
		goto out;
	}

	buf = nla_data(tb[ATH11K_TM_ATTR_DATA]);
	buf_len = nla_len(tb[ATH11K_TM_ATTR_DATA]);
	if (!buf_len) {
		ath11k_warn(ar->ab, "No data present in testmode wmi command\n");
		ret = -EINVAL;
		goto out;
	}

	cmd_id = nla_get_u32(tb[ATH11K_TM_ATTR_WMI_CMDID]);

	/* Make sure that the buffer length is long enough to
	 * hold TLV and pdev/vdev id.
	 */
	if (buf_len < sizeof(struct wmi_tlv) + sizeof(u32)) {
		ret = -EINVAL;
		goto out;
	}

	ptr = buf;
	tag = FIELD_GET(WMI_TLV_TAG, *ptr);

	/* pdev/vdev id start after TLV header */
	ptr++;

	if (tag == WMI_TAG_PDEV_SET_PARAM_CMD)
		*ptr = ar->pdev->pdev_id;

	if (ar->ab->fw_mode != ATH11K_FIRMWARE_MODE_FTM &&
	    (tag == WMI_TAG_VDEV_SET_PARAM_CMD || tag == WMI_TAG_UNIT_TEST_CMD)) {
		if (vif) {
			arvif = ath11k_vif_to_arvif(vif);
			*ptr = arvif->vdev_id;
		} else {
			ret = -EINVAL;
			goto out;
		}
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_TESTMODE,
		   "cmd wmi cmd_id %d buf length %d\n",
		   cmd_id, buf_len);

	ath11k_dbg_dump(ar->ab, ATH11K_DBG_TESTMODE, NULL, "", buf, buf_len);

	skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, buf_len);
	if (!skb) {
		ret = -ENOMEM;
		goto out;
	}

	memcpy(skb->data, buf, buf_len);

	ret = ath11k_wmi_cmd_send(wmi, skb, cmd_id);
	if (ret) {
		dev_kfree_skb(skb);
		ath11k_warn(ar->ab, "failed to transmit wmi command (testmode): %d\n",
			    ret);
		goto out;
	}

	ret = 0;

out:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static int ath11k_tm_cmd_wmi_ftm(struct ath11k *ar, struct nlattr *tb[])
{
	struct ath11k_pdev_wmi *wmi = ar->wmi;
	struct ath11k_base *ab = ar->ab;
	struct sk_buff *skb;
	u32 cmd_id, buf_len, hdr_info;
	int ret;
	void *buf;
	u8 segnumber = 0, seginfo;
	u16 chunk_len, total_bytes, num_segments;
	u8 *bufpos;
	struct wmi_ftm_cmd *ftm_cmd;

	set_bit(ATH11K_FLAG_FTM_SEGMENTED, &ab->dev_flags);

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH11K_STATE_FTM) {
		ret = -ENETDOWN;
		goto out;
	}

	if (!tb[ATH11K_TM_ATTR_DATA]) {
		ret = -EINVAL;
		goto out;
	}

	buf = nla_data(tb[ATH11K_TM_ATTR_DATA]);
	buf_len = nla_len(tb[ATH11K_TM_ATTR_DATA]);
	cmd_id = WMI_PDEV_UTF_CMDID;

	ath11k_dbg(ar->ab, ATH11K_DBG_TESTMODE,
		   "cmd wmi ftm cmd_id %d buffer length %d\n",
		   cmd_id, buf_len);
	ath11k_dbg_dump(ar->ab, ATH11K_DBG_TESTMODE, NULL, "", buf, buf_len);

	bufpos = buf;
	total_bytes = buf_len;
	num_segments = total_bytes / MAX_WMI_UTF_LEN;

	if (buf_len - (num_segments * MAX_WMI_UTF_LEN))
		num_segments++;

	while (buf_len) {
		chunk_len = min_t(u16, buf_len, MAX_WMI_UTF_LEN);

		skb = ath11k_wmi_alloc_skb(wmi->wmi_ab, (chunk_len +
					   sizeof(struct wmi_ftm_cmd)));
		if (!skb) {
			ret = -ENOMEM;
			goto out;
		}

		ftm_cmd = (struct wmi_ftm_cmd *)skb->data;
		hdr_info = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_BYTE) |
			   FIELD_PREP(WMI_TLV_LEN, (chunk_len +
				      sizeof(struct wmi_ftm_seg_hdr)));
		ftm_cmd->tlv_header = hdr_info;
		ftm_cmd->seg_hdr.len = total_bytes;
		ftm_cmd->seg_hdr.msgref = ar->ftm_msgref;
		seginfo = FIELD_PREP(ATH11K_FTM_SEGHDR_TOTAL_SEGMENTS, num_segments) |
			  FIELD_PREP(ATH11K_FTM_SEGHDR_CURRENT_SEQ, segnumber);
		ftm_cmd->seg_hdr.segmentinfo = seginfo;
		segnumber++;

		memcpy(&ftm_cmd->data, bufpos, chunk_len);

		ret = ath11k_wmi_cmd_send(wmi, skb, cmd_id);
		if (ret) {
			ath11k_warn(ar->ab, "failed to send wmi ftm command: %d\n", ret);
			goto out;
		}

		buf_len -= chunk_len;
		bufpos += chunk_len;
	}

	ar->ftm_msgref++;
	ret = 0;

out:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

int ath11k_tm_cmd(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  void *data, int len)
{
	struct ath11k *ar = hw->priv;
	struct nlattr *tb[ATH11K_TM_ATTR_MAX + 1];
	int ret;

	ret = nla_parse(tb, ATH11K_TM_ATTR_MAX, data, len, ath11k_tm_policy,
			NULL);
	if (ret)
		return ret;

	if (!tb[ATH11K_TM_ATTR_CMD])
		return -EINVAL;

	switch (nla_get_u32(tb[ATH11K_TM_ATTR_CMD])) {
	case ATH11K_TM_CMD_GET_VERSION:
		return ath11k_tm_cmd_get_version(ar, tb);
	case ATH11K_TM_CMD_WMI:
		return ath11k_tm_cmd_wmi(ar, tb, vif);
	case ATH11K_TM_CMD_TESTMODE_START:
		return ath11k_tm_cmd_testmode_start(ar, tb);
	case ATH11K_TM_CMD_WMI_FTM:
		return ath11k_tm_cmd_wmi_ftm(ar, tb);
	default:
		return -EOPNOTSUPP;
	}
}
