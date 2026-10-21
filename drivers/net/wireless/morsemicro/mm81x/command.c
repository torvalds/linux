// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2026 Morse Micro
 */

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "command.h"
#include "mac.h"
#include "ps.h"
#include "hif.h"

#define MM_MAX_COMMAND_RETRY 2
#define HOST_CMD_DEFAULT_TIMEOUT_MS 600
#define HOST_CMD_POWERSAVE_TIMEOUT_MS 2000

#define INIT_CMD_HDR(_req, _cmd, _vif_id)				\
	((struct host_cmd_header){					\
		.message_id = cpu_to_le16(_cmd),			\
		.len = cpu_to_le16(sizeof(_req) - sizeof((_req).hdr)),	\
		.vif_id = cpu_to_le16(_vif_id),				\
	})

struct host_cmd_resp_cb {
	int ret;
	u32 length;
	struct host_cmd_resp *dest_resp;
};

static int mm81x_cmd_tx(struct mm81x *mors, struct host_cmd_resp *resp,
			struct host_cmd_req *req, u32 length, u32 timeout)
{
	int cmd_len;
	int ret = 0;
	u16 host_id;
	int retry = 0;
	unsigned long wait_ret = 0;
	struct sk_buff *skb;
	struct mm81x_skbq *cmd_q = mm81x_hif_get_tx_cmd_queue(mors);
	struct host_cmd_resp_cb *resp_cb;
	DECLARE_COMPLETION_ONSTACK(cmd_comp);

	BUILD_BUG_ON(sizeof(struct host_cmd_resp_cb) >
		     IEEE80211_TX_INFO_DRIVER_DATA_SIZE);

	cmd_len = sizeof(*req) + le16_to_cpu(req->hdr.len);
	req->hdr.flags = cpu_to_le16(HOST_CMD_TYPE_REQ);

	mutex_lock(&mors->cmd_wait);
	mors->cmd_seq++;
	if (mors->cmd_seq > HOST_CMD_HOST_ID_SEQ_MAX)
		mors->cmd_seq = 1;
	host_id = mors->cmd_seq << HOST_CMD_HOST_ID_SEQ_SHIFT;

	mm81x_ps_disable(mors);

	do {
		req->hdr.host_id = cpu_to_le16(host_id | retry);

		skb = mm81x_skbq_alloc_skb(cmd_q, cmd_len);
		if (!skb) {
			ret = -ENOMEM;
			break;
		}

		memcpy(skb->data, req, cmd_len);
		resp_cb = (struct host_cmd_resp_cb *)IEEE80211_SKB_CB(skb)
				  ->driver_data;
		resp_cb->length = length;
		resp_cb->dest_resp = resp;

		dev_dbg(mors->dev, "CMD 0x%04x:%04x",
			le16_to_cpu(req->hdr.message_id),
			le16_to_cpu(req->hdr.host_id));

		mutex_lock(&mors->cmd_lock);
		mors->cmd_comp = &cmd_comp;
		if (retry > 0)
			reinit_completion(&cmd_comp);
		timeout = timeout ? timeout : HOST_CMD_DEFAULT_TIMEOUT_MS;
		ret = mm81x_skbq_skb_tx(cmd_q, &skb, NULL,
					MM81X_SKB_CHAN_COMMAND);
		mutex_unlock(&mors->cmd_lock);

		if (ret) {
			dev_err(mors->dev, "mm81x_skbq_tx fail: %d", ret);
			break;
		}

		wait_ret = wait_for_completion_timeout(
			&cmd_comp, msecs_to_jiffies(timeout));
		mutex_lock(&mors->cmd_lock);
		mors->cmd_comp = NULL;

		if (!wait_ret) {
			dev_err(mors->dev,
				"Try:%d Command %04x:%04x timeout after %u ms",
				retry, le16_to_cpu(req->hdr.message_id),
				le16_to_cpu(req->hdr.host_id), timeout);
			ret = -ETIMEDOUT;
		} else {
			ret = (length && resp) ? le32_to_cpu(resp->status) :
						 resp_cb->ret;
			if (ret > 0 || ret < -MAX_ERRNO)
				ret = -EIO;

			dev_dbg(mors->dev, "Command 0x%04x:%04x status 0x%08x",
				le16_to_cpu(req->hdr.message_id),
				le16_to_cpu(req->hdr.host_id), ret);
			if (ret) {
				dev_err(mors->dev,
					"Command 0x%04x:%04x error %d",
					le16_to_cpu(req->hdr.message_id),
					le16_to_cpu(req->hdr.host_id), ret);
			}
		}
		/* Free the command request */
		spin_lock_bh(&cmd_q->lock);
		mm81x_skbq_skb_finish(cmd_q, skb, NULL);
		spin_unlock_bh(&cmd_q->lock);
		mutex_unlock(&mors->cmd_lock);

		retry++;
	} while ((ret == -ETIMEDOUT) && retry < MM_MAX_COMMAND_RETRY);

	mm81x_ps_enable(mors);
	mutex_unlock(&mors->cmd_wait);

	if (ret == -ETIMEDOUT) {
		dev_err(mors->dev, "Command %02x:%02x timed out",
			le16_to_cpu(req->hdr.message_id),
			le16_to_cpu(req->hdr.host_id));
	} else if (ret != 0) {
		dev_err(mors->dev,
			"Command %02x:%02x failed with rc %d (0x%x)\n",
			le16_to_cpu(req->hdr.message_id),
			le16_to_cpu(req->hdr.host_id), ret, ret);
	}

	return ret;
}

int mm81x_cmd_resp_process(struct mm81x *mors, struct sk_buff *skb)
{
	int length, ret = -ESRCH; /* No such process */
	struct mm81x_skbq *cmd_q = mm81x_hif_get_tx_cmd_queue(mors);
	struct host_cmd_resp *src_resp = (struct host_cmd_resp *)(skb->data);
	struct sk_buff *cmd_skb = NULL;
	struct host_cmd_resp_cb *resp_cb;
	struct host_cmd_resp *dest_resp;
	struct host_cmd_req *req;
	u16 message_id = 0;
	u16 host_id = 0;
	u16 resp_message_id = le16_to_cpu(src_resp->hdr.message_id);
	u16 resp_host_id = le16_to_cpu(src_resp->hdr.host_id);
	bool is_late_response = false;

	dev_dbg(mors->dev, "EVT 0x%04x:0x%04x", resp_message_id, resp_host_id);

	if (!HOST_CMD_IS_RESP(src_resp)) {
		ret = mm81x_mac_event_recv(mors, skb);
		goto exit_free;
	}

	mutex_lock(&mors->cmd_lock);

	cmd_skb = mm81x_skbq_tx_pending(cmd_q);
	if (cmd_skb) {
		mm81x_skbq_pull_hdr_post_tx(cmd_skb);
		req = (struct host_cmd_req *)cmd_skb->data;
		message_id = le16_to_cpu(req->hdr.message_id);
		host_id = le16_to_cpu(req->hdr.host_id);
	}

	/*
	 * If there is no pending command or the sequence ID does not match,
	 * this is a late response for a timed out command which has been
	 * cleaned up, so just free up the response. If a command was retried,
	 * the response may be from the retry or from the original command
	 * (late response) but not from both because the firmware will silently
	 * drop a retry if it received the initial request. So a mismatched
	 * retry counter is treated as a matched command and response.
	 */
	if (!cmd_skb || message_id != resp_message_id ||
	    (host_id & HOST_CMD_HOST_ID_SEQ_MASK) !=
		    (resp_host_id & HOST_CMD_HOST_ID_SEQ_MASK)) {
		dev_err(mors->dev,
			"Late response for timed out req 0x%04x:%04x have 0x%04x:%04x 0x%04x",
			resp_message_id, resp_host_id, message_id, host_id,
			mors->cmd_seq);
		is_late_response = true;
		goto exit;
	}
	if ((host_id & HOST_CMD_HOST_ID_RETRY_MASK) !=
	    (resp_host_id & HOST_CMD_HOST_ID_RETRY_MASK))
		dev_dbg(mors->dev,
			"Command retry mismatch 0x%04x:%04x 0x%04x:%04x",
			message_id, host_id, resp_message_id, resp_host_id);

	resp_cb = (struct host_cmd_resp_cb *)IEEE80211_SKB_CB(cmd_skb)
			  ->driver_data;
	length = resp_cb->length;
	dest_resp = resp_cb->dest_resp;
	if (length >= sizeof(struct host_cmd_resp) && dest_resp) {
		ret = 0;
		length = min_t(int, length,
			       le16_to_cpu(src_resp->hdr.len) +
				       sizeof(struct host_cmd_header));
		memcpy(dest_resp, src_resp, length);
	} else {
		ret = le32_to_cpu(src_resp->status);
	}

	resp_cb->ret = ret;

exit:
	if (cmd_skb && !is_late_response) {
		/* Complete if not already timed out */
		if (mors->cmd_comp)
			complete(mors->cmd_comp);
	}

	mutex_unlock(&mors->cmd_lock);
exit_free:
	dev_kfree_skb(skb);
	return 0;
}

int mm81x_cmd_sta_state(struct mm81x *mors, struct mm81x_vif *mors_vif, u16 aid,
			struct ieee80211_sta *sta,
			enum ieee80211_sta_state state)
{
	struct host_cmd_req_set_sta_state req = {
		.hdr = INIT_CMD_HDR(req, HOST_CMD_ID_SET_STA_STATE,
				    mors_vif->id),
		.aid = cpu_to_le16(aid),
		.state = cpu_to_le16(state),
		.uapsd_queues = sta->uapsd_queues,
	};

	memcpy(req.sta_addr, sta->addr, sizeof(req.sta_addr));

	return mm81x_cmd_tx(mors, NULL, (struct host_cmd_req *)&req, 0, 0);
}

int mm81x_cmd_add_if(struct mm81x *mors, u16 *vif_id, const u8 *addr,
		     enum nl80211_iftype type)
{
	int ret;
	struct host_cmd_req_add_interface req = {
		.hdr = INIT_CMD_HDR(req, HOST_CMD_ID_ADD_INTERFACE, 0),
	};
	struct host_cmd_resp_add_interface resp;

	switch (type) {
	case NL80211_IFTYPE_STATION:
		req.interface_type = cpu_to_le32(HOST_CMD_INTERFACE_TYPE_STA);
		break;
	case NL80211_IFTYPE_AP:
		req.interface_type = cpu_to_le32(HOST_CMD_INTERFACE_TYPE_AP);
		break;
	default:
		return -EOPNOTSUPP;
	}

	memcpy(req.addr.octet, addr, sizeof(req.addr.octet));

	ret = mm81x_cmd_tx(mors, (struct host_cmd_resp *)&resp,
			   (struct host_cmd_req *)&req, sizeof(resp), 0);
	if (!ret)
		*vif_id = le16_to_cpu(resp.hdr.vif_id);

	return ret;
}

int mm81x_cmd_get_capabilities(struct mm81x *mors, u16 vif_id,
			       struct mm81x_fw_caps *capabilities)
{
	int ret;
	int i;
	struct host_cmd_req_get_capabilities req = {
		.hdr = INIT_CMD_HDR(req, HOST_CMD_ID_GET_CAPABILITIES, vif_id),
	};
	struct host_cmd_resp_get_capabilities rsp;

	ret = mm81x_cmd_tx(mors, (struct host_cmd_resp *)&rsp,
			   (struct host_cmd_req *)&req, sizeof(rsp), 0);
	if (ret)
		return ret;

	capabilities->ampdu_mss = rsp.capabilities.ampdu_mss;
	capabilities->mm81x_mmss_offset = rsp.morse_mmss_offset;
	capabilities->beamformee_sts_capability =
		rsp.capabilities.beamformee_sts_capability;
	capabilities->maximum_ampdu_length_exponent =
		rsp.capabilities.maximum_ampdu_length_exponent;
	capabilities->number_sounding_dimensions =
		rsp.capabilities.number_sounding_dimensions;
	for (i = 0; i < FW_CAPABILITIES_FLAGS_WIDTH; i++)
		capabilities->flags[i] = le32_to_cpu(rsp.capabilities.flags[i]);

	return ret;
}

int mm81x_cmd_get_max_txpower(struct mm81x *mors, s32 *out_power_mbm)
{
	int ret;
	struct host_cmd_req_get_max_txpower req = {
		.hdr = INIT_CMD_HDR(req, HOST_CMD_ID_GET_MAX_TXPOWER, 0),
	};
	struct host_cmd_resp_get_max_txpower resp;

	ret = mm81x_cmd_tx(mors, (struct host_cmd_resp *)&resp,
			   (struct host_cmd_req *)&req, sizeof(resp), 0);
	if (!ret)
		*out_power_mbm = QDBM_TO_MBM(le32_to_cpu(resp.power_qdbm));

	return ret;
}

int mm81x_cmd_hw_scan(struct mm81x *mors, struct mm81x_hw_scan_params *params,
		      bool store)
{
	int ret;
	struct host_cmd_req_hw_scan *req;
	size_t cmd_size;
	u8 *buf;
	u32 flags = 0;

	cmd_size = mm81x_hw_scan_h_get_cmd_size(params);
	cmd_size = ROUND_BYTES_TO_WORD(cmd_size);

	req = kzalloc(cmd_size, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	buf = req->variable;

	if (store)
		flags = HOST_CMD_HW_SCAN_FLAGS_STORE;
	else if (params->operation == MM81X_HW_SCAN_OP_START)
		flags |= HOST_CMD_HW_SCAN_FLAGS_START;
	else if (params->operation == MM81X_HW_SCAN_OP_STOP)
		flags |= HOST_CMD_HW_SCAN_FLAGS_ABORT;

	flags |= HOST_CMD_HW_SCAN_FLAGS_1MHZ_PROBES;

	if (params->operation == MM81X_HW_SCAN_OP_START) {
		req->dwell_time_ms = cpu_to_le32(params->dwell_time_ms);
		buf = mm81x_hw_scan_h_insert_tlvs(params, buf);
	}

	req->flags = cpu_to_le32(flags);
	req->hdr = INIT_CMD_HDR((*req), HOST_CMD_ID_HW_SCAN, 0);
	req->hdr.len = cpu_to_le16((u16)((buf - (u8 *)req) - sizeof(req->hdr)));
	ret = mm81x_cmd_tx(mors, NULL, (struct host_cmd_req *)req, 0, 0);
	kfree(req);

	return ret;
}

int mm81x_cmd_set_txpower(struct mm81x *mors, s32 *out_power_mbm,
			  int txpower_mbm)
{
	int ret;
	struct host_cmd_req_set_txpower req = {
		.hdr = INIT_CMD_HDR(req, HOST_CMD_ID_SET_TXPOWER, 0),
		.power_qdbm = cpu_to_le32(MBM_TO_QDBM(txpower_mbm)),
	};
	struct host_cmd_resp_set_txpower resp;

	ret = mm81x_cmd_tx(mors, (struct host_cmd_resp *)&resp,
			   (struct host_cmd_req *)&req, sizeof(resp), 0);
	if (!ret)
		*out_power_mbm = QDBM_TO_MBM(le32_to_cpu(resp.power_qdbm));

	return ret;
}

int mm81x_cmd_set_channel(struct mm81x *mors, u32 op_chan_freq_hz,
			  u8 pri_1mhz_chan_idx, u8 op_bw_mhz, u8 pri_bw_mhz,
			  s32 *power_mbm)
{
	int ret;
	struct host_cmd_req_set_channel req = {
		.hdr = INIT_CMD_HDR(req, HOST_CMD_ID_SET_CHANNEL, 0),
		.op_chan_freq_hz = cpu_to_le32(op_chan_freq_hz),
		.op_bw_mhz = op_bw_mhz,
		.pri_bw_mhz = pri_bw_mhz,
		.pri_1mhz_chan_idx = pri_1mhz_chan_idx,
		.dot11_mode = HOST_CMD_DOT11_PROTO_MODE_AH,
	};
	struct host_cmd_resp_set_channel resp;

	ret = mm81x_cmd_tx(mors, (struct host_cmd_resp *)&resp,
			   (struct host_cmd_req *)&req, sizeof(resp), 0);
	if (!ret)
		*power_mbm = QDBM_TO_MBM(le32_to_cpu(resp.power_qdbm));

	return ret;
}

int mm81x_cmd_disable_key(struct mm81x *mors, struct mm81x_vif *mors_vif,
			  u16 aid, struct ieee80211_key_conf *key)
{
	struct host_cmd_req_disable_key req = {
		.hdr = INIT_CMD_HDR(req, HOST_CMD_ID_DISABLE_KEY, mors_vif->id),
		.aid = cpu_to_le32(aid),
		.key_idx = key->hw_key_idx,
		.key_type =
			cpu_to_le32((key->flags & IEEE80211_KEY_FLAG_PAIRWISE) ?
					    HOST_CMD_TEMPORAL_KEY_TYPE_PTK :
					    HOST_CMD_TEMPORAL_KEY_TYPE_GTK),
	};

	return mm81x_cmd_tx(mors, NULL, (struct host_cmd_req *)&req, 0, 0);
}

int mm81x_cmd_install_key(struct mm81x *mors, struct mm81x_vif *mors_vif,
			  u16 aid, struct ieee80211_key_conf *key,
			  enum host_cmd_key_cipher cipher,
			  enum host_cmd_aes_key_len length)
{
	int ret;
	struct host_cmd_req_install_key req = {
		.hdr = INIT_CMD_HDR(req, HOST_CMD_ID_INSTALL_KEY, mors_vif->id),
		.pn = cpu_to_le64(atomic64_read(&key->tx_pn)),
		.aid = cpu_to_le32(aid),
		.cipher = cipher,
		.key_length = length,
		.key_idx = key->keyidx,
		.key_type = (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) ?
				    HOST_CMD_TEMPORAL_KEY_TYPE_PTK :
				    HOST_CMD_TEMPORAL_KEY_TYPE_GTK,
	};
	struct host_cmd_resp_install_key resp;

	if (key->keylen > sizeof(req.key))
		return -EINVAL;

	memcpy(req.key, key->key, key->keylen);

	ret = mm81x_cmd_tx(mors, (struct host_cmd_resp *)&resp,
			   (struct host_cmd_req *)&req, sizeof(resp), 0);
	if (!ret) {
		key->hw_key_idx = resp.key_idx;
		dev_dbg(mors->dev, "Installed key @ hw index: %d",
			resp.key_idx);
	}

	return ret;
}

int mm81x_cmd_cfg_multicast_filter(struct mm81x *mors,
				   struct mm81x_vif *mors_vif)
{
	struct host_cmd_req_mcast_filter *req;
	struct mcast_filter *filter = mors->mcast_filter;
	u16 filter_list_len = sizeof(filter->addr_list[0]) * filter->count;
	u16 alloc_len = filter_list_len + sizeof(*req);
	int ret = 0;

	req = kzalloc(alloc_len, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	req->hdr = INIT_CMD_HDR((*req), HOST_CMD_ID_MCAST_FILTER, mors_vif->id);
	req->hdr.len = cpu_to_le16(alloc_len - sizeof(req->hdr));
	req->count = filter->count;
	memcpy(req->hw_addr, filter->addr_list, filter_list_len);

	ret = mm81x_cmd_tx(mors, NULL, (struct host_cmd_req *)req, 0, 0);
	kfree(req);
	return ret;
}

int mm81x_cmd_cfg_bss(struct mm81x *mors, u16 vif_id, u16 beacon_int,
		      u16 dtim_period, u32 cssid)
{
	struct host_cmd_req_bss_config req = {
		.hdr = INIT_CMD_HDR(req, HOST_CMD_ID_BSS_CONFIG, vif_id),
		.beacon_interval_tu = cpu_to_le16(beacon_int),
		.cssid = cpu_to_le32(cssid),
		.dtim_period = cpu_to_le16(dtim_period),
	};

	return mm81x_cmd_tx(mors, NULL, (struct host_cmd_req *)&req, 0, 0);
}

int mm81x_cmd_config_beacon_timer(struct mm81x *mors, void *mm81x_vif,
				  bool enabled)
{
	struct mm81x_vif *vif = mm81x_vif;
	struct host_cmd_req_bss_beacon_config req = {
		.hdr = INIT_CMD_HDR(req, HOST_CMD_ID_BSS_BEACON_CONFIG,
				    vif->id),
		.enable = enabled,
	};

	return mm81x_cmd_tx(mors, NULL, (struct host_cmd_req *)&req, 0, 0);
}

int mm81x_cmd_set_ps(struct mm81x *mors, bool enabled)
{
	struct host_cmd_req_config_ps req = {
		.hdr = INIT_CMD_HDR(req, HOST_CMD_ID_CONFIG_PS, 0),
		.enabled = (u8)enabled,
	};

	return mm81x_cmd_tx(mors, NULL, (struct host_cmd_req *)&req, 0,
			    HOST_CMD_POWERSAVE_TIMEOUT_MS);
}

int mm81x_cmd_cfg_qos(struct mm81x *mors, struct mm81x_queue_params *params)
{
	struct host_cmd_req_set_qos_params req = {
		.hdr = INIT_CMD_HDR(req, HOST_CMD_ID_SET_QOS_PARAMS, 0),
		.uapsd = params->uapsd,
		.queue_idx = params->aci,
		.aifs_slot_count = params->aifs,
		.contention_window_min = cpu_to_le16(params->cw_min),
		.contention_window_max = cpu_to_le16(params->cw_max),
		.max_txop_usec = cpu_to_le32(params->txop),
	};

	return mm81x_cmd_tx(mors, NULL, (struct host_cmd_req *)&req, 0, 0);
}

int mm81x_cmd_rm_if(struct mm81x *mors, u16 vif_id)
{
	struct host_cmd_req_remove_interface req = {
		.hdr = INIT_CMD_HDR(req, HOST_CMD_ID_REMOVE_INTERFACE, vif_id),
	};

	return mm81x_cmd_tx(mors, NULL, (struct host_cmd_req *)&req, 0, 0);
}

int mm81x_cmd_set_frag_threshold(struct mm81x *mors, u32 frag_threshold)
{
	struct host_cmd_req_get_set_generic_param req = {
		.hdr = INIT_CMD_HDR(req, HOST_CMD_ID_GET_SET_GENERIC_PARAM, 0),
		.param_id = cpu_to_le32(HOST_CMD_PARAM_ID_FRAGMENT_THRESHOLD),
		.action = cpu_to_le32(HOST_CMD_PARAM_ACTION_SET),
		.value = cpu_to_le32(frag_threshold),
	};

	return mm81x_cmd_tx(mors, NULL, (struct host_cmd_req *)&req, 0, 0);
}

int mm81x_cmd_get_disabled_channels(
	struct mm81x *mors, struct host_cmd_resp_get_disabled_channels *resp,
	uint resp_len)
{
	struct host_cmd_req req = {
		.hdr = INIT_CMD_HDR(req, HOST_CMD_ID_GET_DISABLED_CHANNELS, 0),
	};

	return mm81x_cmd_tx(mors, (struct host_cmd_resp *)resp, &req, resp_len,
			    0);
}
