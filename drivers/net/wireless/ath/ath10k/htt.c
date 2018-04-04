/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2017 Qualcomm Atheros, Inc.
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

#include <linux/slab.h>
#include <linux/if_ether.h>

#include "htt.h"
#include "core.h"
#include "debug.h"

static const enum htt_t2h_msg_type htt_main_t2h_msg_types[] = {
	[HTT_MAIN_T2H_MSG_TYPE_VERSION_CONF] = HTT_T2H_MSG_TYPE_VERSION_CONF,
	[HTT_MAIN_T2H_MSG_TYPE_RX_IND] = HTT_T2H_MSG_TYPE_RX_IND,
	[HTT_MAIN_T2H_MSG_TYPE_RX_FLUSH] = HTT_T2H_MSG_TYPE_RX_FLUSH,
	[HTT_MAIN_T2H_MSG_TYPE_PEER_MAP] = HTT_T2H_MSG_TYPE_PEER_MAP,
	[HTT_MAIN_T2H_MSG_TYPE_PEER_UNMAP] = HTT_T2H_MSG_TYPE_PEER_UNMAP,
	[HTT_MAIN_T2H_MSG_TYPE_RX_ADDBA] = HTT_T2H_MSG_TYPE_RX_ADDBA,
	[HTT_MAIN_T2H_MSG_TYPE_RX_DELBA] = HTT_T2H_MSG_TYPE_RX_DELBA,
	[HTT_MAIN_T2H_MSG_TYPE_TX_COMPL_IND] = HTT_T2H_MSG_TYPE_TX_COMPL_IND,
	[HTT_MAIN_T2H_MSG_TYPE_PKTLOG] = HTT_T2H_MSG_TYPE_PKTLOG,
	[HTT_MAIN_T2H_MSG_TYPE_STATS_CONF] = HTT_T2H_MSG_TYPE_STATS_CONF,
	[HTT_MAIN_T2H_MSG_TYPE_RX_FRAG_IND] = HTT_T2H_MSG_TYPE_RX_FRAG_IND,
	[HTT_MAIN_T2H_MSG_TYPE_SEC_IND] = HTT_T2H_MSG_TYPE_SEC_IND,
	[HTT_MAIN_T2H_MSG_TYPE_TX_INSPECT_IND] =
		HTT_T2H_MSG_TYPE_TX_INSPECT_IND,
	[HTT_MAIN_T2H_MSG_TYPE_MGMT_TX_COMPL_IND] =
		HTT_T2H_MSG_TYPE_MGMT_TX_COMPLETION,
	[HTT_MAIN_T2H_MSG_TYPE_TX_CREDIT_UPDATE_IND] =
		HTT_T2H_MSG_TYPE_TX_CREDIT_UPDATE_IND,
	[HTT_MAIN_T2H_MSG_TYPE_RX_PN_IND] = HTT_T2H_MSG_TYPE_RX_PN_IND,
	[HTT_MAIN_T2H_MSG_TYPE_RX_OFFLOAD_DELIVER_IND] =
		HTT_T2H_MSG_TYPE_RX_OFFLOAD_DELIVER_IND,
	[HTT_MAIN_T2H_MSG_TYPE_TEST] = HTT_T2H_MSG_TYPE_TEST,
};

static const enum htt_t2h_msg_type htt_10x_t2h_msg_types[] = {
	[HTT_10X_T2H_MSG_TYPE_VERSION_CONF] = HTT_T2H_MSG_TYPE_VERSION_CONF,
	[HTT_10X_T2H_MSG_TYPE_RX_IND] = HTT_T2H_MSG_TYPE_RX_IND,
	[HTT_10X_T2H_MSG_TYPE_RX_FLUSH] = HTT_T2H_MSG_TYPE_RX_FLUSH,
	[HTT_10X_T2H_MSG_TYPE_PEER_MAP] = HTT_T2H_MSG_TYPE_PEER_MAP,
	[HTT_10X_T2H_MSG_TYPE_PEER_UNMAP] = HTT_T2H_MSG_TYPE_PEER_UNMAP,
	[HTT_10X_T2H_MSG_TYPE_RX_ADDBA] = HTT_T2H_MSG_TYPE_RX_ADDBA,
	[HTT_10X_T2H_MSG_TYPE_RX_DELBA] = HTT_T2H_MSG_TYPE_RX_DELBA,
	[HTT_10X_T2H_MSG_TYPE_TX_COMPL_IND] = HTT_T2H_MSG_TYPE_TX_COMPL_IND,
	[HTT_10X_T2H_MSG_TYPE_PKTLOG] = HTT_T2H_MSG_TYPE_PKTLOG,
	[HTT_10X_T2H_MSG_TYPE_STATS_CONF] = HTT_T2H_MSG_TYPE_STATS_CONF,
	[HTT_10X_T2H_MSG_TYPE_RX_FRAG_IND] = HTT_T2H_MSG_TYPE_RX_FRAG_IND,
	[HTT_10X_T2H_MSG_TYPE_SEC_IND] = HTT_T2H_MSG_TYPE_SEC_IND,
	[HTT_10X_T2H_MSG_TYPE_RC_UPDATE_IND] = HTT_T2H_MSG_TYPE_RC_UPDATE_IND,
	[HTT_10X_T2H_MSG_TYPE_TX_INSPECT_IND] = HTT_T2H_MSG_TYPE_TX_INSPECT_IND,
	[HTT_10X_T2H_MSG_TYPE_TEST] = HTT_T2H_MSG_TYPE_TEST,
	[HTT_10X_T2H_MSG_TYPE_CHAN_CHANGE] = HTT_T2H_MSG_TYPE_CHAN_CHANGE,
	[HTT_10X_T2H_MSG_TYPE_AGGR_CONF] = HTT_T2H_MSG_TYPE_AGGR_CONF,
	[HTT_10X_T2H_MSG_TYPE_STATS_NOUPLOAD] = HTT_T2H_MSG_TYPE_STATS_NOUPLOAD,
	[HTT_10X_T2H_MSG_TYPE_MGMT_TX_COMPL_IND] =
		HTT_T2H_MSG_TYPE_MGMT_TX_COMPLETION,
};

static const enum htt_t2h_msg_type htt_tlv_t2h_msg_types[] = {
	[HTT_TLV_T2H_MSG_TYPE_VERSION_CONF] = HTT_T2H_MSG_TYPE_VERSION_CONF,
	[HTT_TLV_T2H_MSG_TYPE_RX_IND] = HTT_T2H_MSG_TYPE_RX_IND,
	[HTT_TLV_T2H_MSG_TYPE_RX_FLUSH] = HTT_T2H_MSG_TYPE_RX_FLUSH,
	[HTT_TLV_T2H_MSG_TYPE_PEER_MAP] = HTT_T2H_MSG_TYPE_PEER_MAP,
	[HTT_TLV_T2H_MSG_TYPE_PEER_UNMAP] = HTT_T2H_MSG_TYPE_PEER_UNMAP,
	[HTT_TLV_T2H_MSG_TYPE_RX_ADDBA] = HTT_T2H_MSG_TYPE_RX_ADDBA,
	[HTT_TLV_T2H_MSG_TYPE_RX_DELBA] = HTT_T2H_MSG_TYPE_RX_DELBA,
	[HTT_TLV_T2H_MSG_TYPE_TX_COMPL_IND] = HTT_T2H_MSG_TYPE_TX_COMPL_IND,
	[HTT_TLV_T2H_MSG_TYPE_PKTLOG] = HTT_T2H_MSG_TYPE_PKTLOG,
	[HTT_TLV_T2H_MSG_TYPE_STATS_CONF] = HTT_T2H_MSG_TYPE_STATS_CONF,
	[HTT_TLV_T2H_MSG_TYPE_RX_FRAG_IND] = HTT_T2H_MSG_TYPE_RX_FRAG_IND,
	[HTT_TLV_T2H_MSG_TYPE_SEC_IND] = HTT_T2H_MSG_TYPE_SEC_IND,
	[HTT_TLV_T2H_MSG_TYPE_RC_UPDATE_IND] = HTT_T2H_MSG_TYPE_RC_UPDATE_IND,
	[HTT_TLV_T2H_MSG_TYPE_TX_INSPECT_IND] = HTT_T2H_MSG_TYPE_TX_INSPECT_IND,
	[HTT_TLV_T2H_MSG_TYPE_MGMT_TX_COMPL_IND] =
		HTT_T2H_MSG_TYPE_MGMT_TX_COMPLETION,
	[HTT_TLV_T2H_MSG_TYPE_TX_CREDIT_UPDATE_IND] =
		HTT_T2H_MSG_TYPE_TX_CREDIT_UPDATE_IND,
	[HTT_TLV_T2H_MSG_TYPE_RX_PN_IND] = HTT_T2H_MSG_TYPE_RX_PN_IND,
	[HTT_TLV_T2H_MSG_TYPE_RX_OFFLOAD_DELIVER_IND] =
		HTT_T2H_MSG_TYPE_RX_OFFLOAD_DELIVER_IND,
	[HTT_TLV_T2H_MSG_TYPE_RX_IN_ORD_PADDR_IND] =
		HTT_T2H_MSG_TYPE_RX_IN_ORD_PADDR_IND,
	[HTT_TLV_T2H_MSG_TYPE_WDI_IPA_OP_RESPONSE] =
		HTT_T2H_MSG_TYPE_WDI_IPA_OP_RESPONSE,
	[HTT_TLV_T2H_MSG_TYPE_CHAN_CHANGE] = HTT_T2H_MSG_TYPE_CHAN_CHANGE,
	[HTT_TLV_T2H_MSG_TYPE_RX_OFLD_PKT_ERR] =
		HTT_T2H_MSG_TYPE_RX_OFLD_PKT_ERR,
	[HTT_TLV_T2H_MSG_TYPE_TEST] = HTT_T2H_MSG_TYPE_TEST,
};

static const enum htt_t2h_msg_type htt_10_4_t2h_msg_types[] = {
	[HTT_10_4_T2H_MSG_TYPE_VERSION_CONF] = HTT_T2H_MSG_TYPE_VERSION_CONF,
	[HTT_10_4_T2H_MSG_TYPE_RX_IND] = HTT_T2H_MSG_TYPE_RX_IND,
	[HTT_10_4_T2H_MSG_TYPE_RX_FLUSH] = HTT_T2H_MSG_TYPE_RX_FLUSH,
	[HTT_10_4_T2H_MSG_TYPE_PEER_MAP] = HTT_T2H_MSG_TYPE_PEER_MAP,
	[HTT_10_4_T2H_MSG_TYPE_PEER_UNMAP] = HTT_T2H_MSG_TYPE_PEER_UNMAP,
	[HTT_10_4_T2H_MSG_TYPE_RX_ADDBA] = HTT_T2H_MSG_TYPE_RX_ADDBA,
	[HTT_10_4_T2H_MSG_TYPE_RX_DELBA] = HTT_T2H_MSG_TYPE_RX_DELBA,
	[HTT_10_4_T2H_MSG_TYPE_TX_COMPL_IND] = HTT_T2H_MSG_TYPE_TX_COMPL_IND,
	[HTT_10_4_T2H_MSG_TYPE_PKTLOG] = HTT_T2H_MSG_TYPE_PKTLOG,
	[HTT_10_4_T2H_MSG_TYPE_STATS_CONF] = HTT_T2H_MSG_TYPE_STATS_CONF,
	[HTT_10_4_T2H_MSG_TYPE_RX_FRAG_IND] = HTT_T2H_MSG_TYPE_RX_FRAG_IND,
	[HTT_10_4_T2H_MSG_TYPE_SEC_IND] = HTT_T2H_MSG_TYPE_SEC_IND,
	[HTT_10_4_T2H_MSG_TYPE_RC_UPDATE_IND] = HTT_T2H_MSG_TYPE_RC_UPDATE_IND,
	[HTT_10_4_T2H_MSG_TYPE_TX_INSPECT_IND] =
				HTT_T2H_MSG_TYPE_TX_INSPECT_IND,
	[HTT_10_4_T2H_MSG_TYPE_MGMT_TX_COMPL_IND] =
				HTT_T2H_MSG_TYPE_MGMT_TX_COMPLETION,
	[HTT_10_4_T2H_MSG_TYPE_CHAN_CHANGE] = HTT_T2H_MSG_TYPE_CHAN_CHANGE,
	[HTT_10_4_T2H_MSG_TYPE_TX_CREDIT_UPDATE_IND] =
				HTT_T2H_MSG_TYPE_TX_CREDIT_UPDATE_IND,
	[HTT_10_4_T2H_MSG_TYPE_RX_PN_IND] = HTT_T2H_MSG_TYPE_RX_PN_IND,
	[HTT_10_4_T2H_MSG_TYPE_RX_OFFLOAD_DELIVER_IND] =
				HTT_T2H_MSG_TYPE_RX_OFFLOAD_DELIVER_IND,
	[HTT_10_4_T2H_MSG_TYPE_TEST] = HTT_T2H_MSG_TYPE_TEST,
	[HTT_10_4_T2H_MSG_TYPE_EN_STATS] = HTT_T2H_MSG_TYPE_EN_STATS,
	[HTT_10_4_T2H_MSG_TYPE_AGGR_CONF] = HTT_T2H_MSG_TYPE_AGGR_CONF,
	[HTT_10_4_T2H_MSG_TYPE_TX_FETCH_IND] =
				HTT_T2H_MSG_TYPE_TX_FETCH_IND,
	[HTT_10_4_T2H_MSG_TYPE_TX_FETCH_CONFIRM] =
				HTT_T2H_MSG_TYPE_TX_FETCH_CONFIRM,
	[HTT_10_4_T2H_MSG_TYPE_STATS_NOUPLOAD] =
				HTT_T2H_MSG_TYPE_STATS_NOUPLOAD,
	[HTT_10_4_T2H_MSG_TYPE_TX_MODE_SWITCH_IND] =
				HTT_T2H_MSG_TYPE_TX_MODE_SWITCH_IND,
	[HTT_10_4_T2H_MSG_TYPE_PEER_STATS] =
				HTT_T2H_MSG_TYPE_PEER_STATS,
};

int ath10k_htt_connect(struct ath10k_htt *htt)
{
	struct ath10k_htc_svc_conn_req conn_req;
	struct ath10k_htc_svc_conn_resp conn_resp;
	int status;

	memset(&conn_req, 0, sizeof(conn_req));
	memset(&conn_resp, 0, sizeof(conn_resp));

	conn_req.ep_ops.ep_tx_complete = ath10k_htt_htc_tx_complete;
	conn_req.ep_ops.ep_rx_complete = ath10k_htt_htc_t2h_msg_handler;

	/* connect to control service */
	conn_req.service_id = ATH10K_HTC_SVC_ID_HTT_DATA_MSG;

	status = ath10k_htc_connect_service(&htt->ar->htc, &conn_req,
					    &conn_resp);

	if (status)
		return status;

	htt->eid = conn_resp.eid;

	return 0;
}

int ath10k_htt_init(struct ath10k *ar)
{
	struct ath10k_htt *htt = &ar->htt;

	htt->ar = ar;

	/*
	 * Prefetch enough data to satisfy target
	 * classification engine.
	 * This is for LL chips. HL chips will probably
	 * transfer all frame in the tx fragment.
	 */
	htt->prefetch_len =
		36 + /* 802.11 + qos + ht */
		4 + /* 802.1q */
		8 + /* llc snap */
		2; /* ip4 dscp or ip6 priority */

	switch (ar->running_fw->fw_file.htt_op_version) {
	case ATH10K_FW_HTT_OP_VERSION_10_4:
		ar->htt.t2h_msg_types = htt_10_4_t2h_msg_types;
		ar->htt.t2h_msg_types_max = HTT_10_4_T2H_NUM_MSGS;
		break;
	case ATH10K_FW_HTT_OP_VERSION_10_1:
		ar->htt.t2h_msg_types = htt_10x_t2h_msg_types;
		ar->htt.t2h_msg_types_max = HTT_10X_T2H_NUM_MSGS;
		break;
	case ATH10K_FW_HTT_OP_VERSION_TLV:
		ar->htt.t2h_msg_types = htt_tlv_t2h_msg_types;
		ar->htt.t2h_msg_types_max = HTT_TLV_T2H_NUM_MSGS;
		break;
	case ATH10K_FW_HTT_OP_VERSION_MAIN:
		ar->htt.t2h_msg_types = htt_main_t2h_msg_types;
		ar->htt.t2h_msg_types_max = HTT_MAIN_T2H_NUM_MSGS;
		break;
	case ATH10K_FW_HTT_OP_VERSION_MAX:
	case ATH10K_FW_HTT_OP_VERSION_UNSET:
		WARN_ON(1);
		return -EINVAL;
	}
	ath10k_htt_set_tx_ops(htt);
	ath10k_htt_set_rx_ops(htt);

	return 0;
}

#define HTT_TARGET_VERSION_TIMEOUT_HZ (3 * HZ)

static int ath10k_htt_verify_version(struct ath10k_htt *htt)
{
	struct ath10k *ar = htt->ar;

	ath10k_dbg(ar, ATH10K_DBG_BOOT, "htt target version %d.%d\n",
		   htt->target_version_major, htt->target_version_minor);

	if (htt->target_version_major != 2 &&
	    htt->target_version_major != 3) {
		ath10k_err(ar, "unsupported htt major version %d. supported versions are 2 and 3\n",
			   htt->target_version_major);
		return -ENOTSUPP;
	}

	return 0;
}

int ath10k_htt_setup(struct ath10k_htt *htt)
{
	struct ath10k *ar = htt->ar;
	int status;

	init_completion(&htt->target_version_received);

	status = ath10k_htt_h2t_ver_req_msg(htt);
	if (status)
		return status;

	status = wait_for_completion_timeout(&htt->target_version_received,
					     HTT_TARGET_VERSION_TIMEOUT_HZ);
	if (status == 0) {
		ath10k_warn(ar, "htt version request timed out\n");
		return -ETIMEDOUT;
	}

	status = ath10k_htt_verify_version(htt);
	if (status) {
		ath10k_warn(ar, "failed to verify htt version: %d\n",
			    status);
		return status;
	}

	status = htt->tx_ops->htt_send_frag_desc_bank_cfg(htt);
	if (status)
		return status;

	status = htt->tx_ops->htt_send_rx_ring_cfg(htt);
	if (status) {
		ath10k_warn(ar, "failed to setup rx ring: %d\n",
			    status);
		return status;
	}

	status = ath10k_htt_h2t_aggr_cfg_msg(htt,
					     htt->max_num_ampdu,
					     htt->max_num_amsdu);
	if (status) {
		ath10k_warn(ar, "failed to setup amsdu/ampdu limit: %d\n",
			    status);
		return status;
	}

	return 0;
}
