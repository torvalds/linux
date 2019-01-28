/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/param.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/vmalloc.h>
#include "qed.h"
#include <linux/qed/qed_chain.h>
#include "qed_cxt.h"
#include "qed_dev_api.h"
#include <linux/qed/qed_eth_if.h>
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_int.h"
#include "qed_l2.h"
#include "qed_mcp.h"
#include "qed_reg_addr.h"
#include "qed_sp.h"
#include "qed_sriov.h"


#define QED_MAX_SGES_NUM 16
#define CRC32_POLY 0x1edc6f41

struct qed_l2_info {
	u32 queues;
	unsigned long **pp_qid_usage;

	/* The lock is meant to synchronize access to the qid usage */
	struct mutex lock;
};

int qed_l2_alloc(struct qed_hwfn *p_hwfn)
{
	struct qed_l2_info *p_l2_info;
	unsigned long **pp_qids;
	u32 i;

	if (!QED_IS_L2_PERSONALITY(p_hwfn))
		return 0;

	p_l2_info = kzalloc(sizeof(*p_l2_info), GFP_KERNEL);
	if (!p_l2_info)
		return -ENOMEM;
	p_hwfn->p_l2_info = p_l2_info;

	if (IS_PF(p_hwfn->cdev)) {
		p_l2_info->queues = RESC_NUM(p_hwfn, QED_L2_QUEUE);
	} else {
		u8 rx = 0, tx = 0;

		qed_vf_get_num_rxqs(p_hwfn, &rx);
		qed_vf_get_num_txqs(p_hwfn, &tx);

		p_l2_info->queues = max_t(u8, rx, tx);
	}

	pp_qids = kcalloc(p_l2_info->queues, sizeof(unsigned long *),
			  GFP_KERNEL);
	if (!pp_qids)
		return -ENOMEM;
	p_l2_info->pp_qid_usage = pp_qids;

	for (i = 0; i < p_l2_info->queues; i++) {
		pp_qids[i] = kzalloc(MAX_QUEUES_PER_QZONE / 8, GFP_KERNEL);
		if (!pp_qids[i])
			return -ENOMEM;
	}

	return 0;
}

void qed_l2_setup(struct qed_hwfn *p_hwfn)
{
	if (!QED_IS_L2_PERSONALITY(p_hwfn))
		return;

	mutex_init(&p_hwfn->p_l2_info->lock);
}

void qed_l2_free(struct qed_hwfn *p_hwfn)
{
	u32 i;

	if (!QED_IS_L2_PERSONALITY(p_hwfn))
		return;

	if (!p_hwfn->p_l2_info)
		return;

	if (!p_hwfn->p_l2_info->pp_qid_usage)
		goto out_l2_info;

	/* Free until hit first uninitialized entry */
	for (i = 0; i < p_hwfn->p_l2_info->queues; i++) {
		if (!p_hwfn->p_l2_info->pp_qid_usage[i])
			break;
		kfree(p_hwfn->p_l2_info->pp_qid_usage[i]);
	}

	kfree(p_hwfn->p_l2_info->pp_qid_usage);

out_l2_info:
	kfree(p_hwfn->p_l2_info);
	p_hwfn->p_l2_info = NULL;
}

static bool qed_eth_queue_qid_usage_add(struct qed_hwfn *p_hwfn,
					struct qed_queue_cid *p_cid)
{
	struct qed_l2_info *p_l2_info = p_hwfn->p_l2_info;
	u16 queue_id = p_cid->rel.queue_id;
	bool b_rc = true;
	u8 first;

	mutex_lock(&p_l2_info->lock);

	if (queue_id >= p_l2_info->queues) {
		DP_NOTICE(p_hwfn,
			  "Requested to increase usage for qzone %04x out of %08x\n",
			  queue_id, p_l2_info->queues);
		b_rc = false;
		goto out;
	}

	first = (u8)find_first_zero_bit(p_l2_info->pp_qid_usage[queue_id],
					MAX_QUEUES_PER_QZONE);
	if (first >= MAX_QUEUES_PER_QZONE) {
		b_rc = false;
		goto out;
	}

	__set_bit(first, p_l2_info->pp_qid_usage[queue_id]);
	p_cid->qid_usage_idx = first;

out:
	mutex_unlock(&p_l2_info->lock);
	return b_rc;
}

static void qed_eth_queue_qid_usage_del(struct qed_hwfn *p_hwfn,
					struct qed_queue_cid *p_cid)
{
	mutex_lock(&p_hwfn->p_l2_info->lock);

	clear_bit(p_cid->qid_usage_idx,
		  p_hwfn->p_l2_info->pp_qid_usage[p_cid->rel.queue_id]);

	mutex_unlock(&p_hwfn->p_l2_info->lock);
}

void qed_eth_queue_cid_release(struct qed_hwfn *p_hwfn,
			       struct qed_queue_cid *p_cid)
{
	bool b_legacy_vf = !!(p_cid->vf_legacy & QED_QCID_LEGACY_VF_CID);

	if (IS_PF(p_hwfn->cdev) && !b_legacy_vf)
		_qed_cxt_release_cid(p_hwfn, p_cid->cid, p_cid->vfid);

	/* For PF's VFs we maintain the index inside queue-zone in IOV */
	if (p_cid->vfid == QED_QUEUE_CID_SELF)
		qed_eth_queue_qid_usage_del(p_hwfn, p_cid);

	vfree(p_cid);
}

/* The internal is only meant to be directly called by PFs initializeing CIDs
 * for their VFs.
 */
static struct qed_queue_cid *
_qed_eth_queue_to_cid(struct qed_hwfn *p_hwfn,
		      u16 opaque_fid,
		      u32 cid,
		      struct qed_queue_start_common_params *p_params,
		      bool b_is_rx,
		      struct qed_queue_cid_vf_params *p_vf_params)
{
	struct qed_queue_cid *p_cid;
	int rc;

	p_cid = vzalloc(sizeof(*p_cid));
	if (!p_cid)
		return NULL;

	p_cid->opaque_fid = opaque_fid;
	p_cid->cid = cid;
	p_cid->p_owner = p_hwfn;

	/* Fill in parameters */
	p_cid->rel.vport_id = p_params->vport_id;
	p_cid->rel.queue_id = p_params->queue_id;
	p_cid->rel.stats_id = p_params->stats_id;
	p_cid->sb_igu_id = p_params->p_sb->igu_sb_id;
	p_cid->b_is_rx = b_is_rx;
	p_cid->sb_idx = p_params->sb_idx;

	/* Fill-in bits related to VFs' queues if information was provided */
	if (p_vf_params) {
		p_cid->vfid = p_vf_params->vfid;
		p_cid->vf_qid = p_vf_params->vf_qid;
		p_cid->vf_legacy = p_vf_params->vf_legacy;
	} else {
		p_cid->vfid = QED_QUEUE_CID_SELF;
	}

	/* Don't try calculating the absolute indices for VFs */
	if (IS_VF(p_hwfn->cdev)) {
		p_cid->abs = p_cid->rel;
		goto out;
	}

	/* Calculate the engine-absolute indices of the resources.
	 * This would guarantee they're valid later on.
	 * In some cases [SBs] we already have the right values.
	 */
	rc = qed_fw_vport(p_hwfn, p_cid->rel.vport_id, &p_cid->abs.vport_id);
	if (rc)
		goto fail;

	rc = qed_fw_l2_queue(p_hwfn, p_cid->rel.queue_id, &p_cid->abs.queue_id);
	if (rc)
		goto fail;

	/* In case of a PF configuring its VF's queues, the stats-id is already
	 * absolute [since there's a single index that's suitable per-VF].
	 */
	if (p_cid->vfid == QED_QUEUE_CID_SELF) {
		rc = qed_fw_vport(p_hwfn, p_cid->rel.stats_id,
				  &p_cid->abs.stats_id);
		if (rc)
			goto fail;
	} else {
		p_cid->abs.stats_id = p_cid->rel.stats_id;
	}

out:
	/* VF-images have provided the qid_usage_idx on their own.
	 * Otherwise, we need to allocate a unique one.
	 */
	if (!p_vf_params) {
		if (!qed_eth_queue_qid_usage_add(p_hwfn, p_cid))
			goto fail;
	} else {
		p_cid->qid_usage_idx = p_vf_params->qid_usage_idx;
	}

	DP_VERBOSE(p_hwfn,
		   QED_MSG_SP,
		   "opaque_fid: %04x CID %08x vport %02x [%02x] qzone %04x.%02x [%04x] stats %02x [%02x] SB %04x PI %02x\n",
		   p_cid->opaque_fid,
		   p_cid->cid,
		   p_cid->rel.vport_id,
		   p_cid->abs.vport_id,
		   p_cid->rel.queue_id,
		   p_cid->qid_usage_idx,
		   p_cid->abs.queue_id,
		   p_cid->rel.stats_id,
		   p_cid->abs.stats_id, p_cid->sb_igu_id, p_cid->sb_idx);

	return p_cid;

fail:
	vfree(p_cid);
	return NULL;
}

struct qed_queue_cid *
qed_eth_queue_to_cid(struct qed_hwfn *p_hwfn,
		     u16 opaque_fid,
		     struct qed_queue_start_common_params *p_params,
		     bool b_is_rx,
		     struct qed_queue_cid_vf_params *p_vf_params)
{
	struct qed_queue_cid *p_cid;
	u8 vfid = QED_CXT_PF_CID;
	bool b_legacy_vf = false;
	u32 cid = 0;

	/* In case of legacy VFs, The CID can be derived from the additional
	 * VF parameters - the VF assumes queue X uses CID X, so we can simply
	 * use the vf_qid for this purpose as well.
	 */
	if (p_vf_params) {
		vfid = p_vf_params->vfid;

		if (p_vf_params->vf_legacy & QED_QCID_LEGACY_VF_CID) {
			b_legacy_vf = true;
			cid = p_vf_params->vf_qid;
		}
	}

	/* Get a unique firmware CID for this queue, in case it's a PF.
	 * VF's don't need a CID as the queue configuration will be done
	 * by PF.
	 */
	if (IS_PF(p_hwfn->cdev) && !b_legacy_vf) {
		if (_qed_cxt_acquire_cid(p_hwfn, PROTOCOLID_ETH,
					 &cid, vfid)) {
			DP_NOTICE(p_hwfn, "Failed to acquire cid\n");
			return NULL;
		}
	}

	p_cid = _qed_eth_queue_to_cid(p_hwfn, opaque_fid, cid,
				      p_params, b_is_rx, p_vf_params);
	if (!p_cid && IS_PF(p_hwfn->cdev) && !b_legacy_vf)
		_qed_cxt_release_cid(p_hwfn, cid, vfid);

	return p_cid;
}

static struct qed_queue_cid *
qed_eth_queue_to_cid_pf(struct qed_hwfn *p_hwfn,
			u16 opaque_fid,
			bool b_is_rx,
			struct qed_queue_start_common_params *p_params)
{
	return qed_eth_queue_to_cid(p_hwfn, opaque_fid, p_params, b_is_rx,
				    NULL);
}

int qed_sp_eth_vport_start(struct qed_hwfn *p_hwfn,
			   struct qed_sp_vport_start_params *p_params)
{
	struct vport_start_ramrod_data *p_ramrod = NULL;
	struct qed_spq_entry *p_ent =  NULL;
	struct qed_sp_init_data init_data;
	u8 abs_vport_id = 0;
	int rc = -EINVAL;
	u16 rx_mode = 0;

	rc = qed_fw_vport(p_hwfn, p_params->vport_id, &abs_vport_id);
	if (rc)
		return rc;

	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_params->opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 ETH_RAMROD_VPORT_START,
				 PROTOCOLID_ETH, &init_data);
	if (rc)
		return rc;

	p_ramrod		= &p_ent->ramrod.vport_start;
	p_ramrod->vport_id	= abs_vport_id;

	p_ramrod->mtu			= cpu_to_le16(p_params->mtu);
	p_ramrod->handle_ptp_pkts	= p_params->handle_ptp_pkts;
	p_ramrod->inner_vlan_removal_en	= p_params->remove_inner_vlan;
	p_ramrod->drop_ttl0_en		= p_params->drop_ttl0;
	p_ramrod->untagged		= p_params->only_untagged;

	SET_FIELD(rx_mode, ETH_VPORT_RX_MODE_UCAST_DROP_ALL, 1);
	SET_FIELD(rx_mode, ETH_VPORT_RX_MODE_MCAST_DROP_ALL, 1);

	p_ramrod->rx_mode.state = cpu_to_le16(rx_mode);

	/* TPA related fields */
	memset(&p_ramrod->tpa_param, 0, sizeof(struct eth_vport_tpa_param));

	p_ramrod->tpa_param.max_buff_num = p_params->max_buffers_per_cqe;

	switch (p_params->tpa_mode) {
	case QED_TPA_MODE_GRO:
		p_ramrod->tpa_param.tpa_max_aggs_num = ETH_TPA_MAX_AGGS_NUM;
		p_ramrod->tpa_param.tpa_max_size = (u16)-1;
		p_ramrod->tpa_param.tpa_min_size_to_cont = p_params->mtu / 2;
		p_ramrod->tpa_param.tpa_min_size_to_start = p_params->mtu / 2;
		p_ramrod->tpa_param.tpa_ipv4_en_flg = 1;
		p_ramrod->tpa_param.tpa_ipv6_en_flg = 1;
		p_ramrod->tpa_param.tpa_pkt_split_flg = 1;
		p_ramrod->tpa_param.tpa_gro_consistent_flg = 1;
		break;
	default:
		break;
	}

	p_ramrod->tx_switching_en = p_params->tx_switching;

	p_ramrod->ctl_frame_mac_check_en = !!p_params->check_mac;
	p_ramrod->ctl_frame_ethtype_check_en = !!p_params->check_ethtype;

	/* Software Function ID in hwfn (PFs are 0 - 15, VFs are 16 - 135) */
	p_ramrod->sw_fid = qed_concrete_to_sw_fid(p_hwfn->cdev,
						  p_params->concrete_fid);

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int qed_sp_vport_start(struct qed_hwfn *p_hwfn,
			      struct qed_sp_vport_start_params *p_params)
{
	if (IS_VF(p_hwfn->cdev)) {
		return qed_vf_pf_vport_start(p_hwfn, p_params->vport_id,
					     p_params->mtu,
					     p_params->remove_inner_vlan,
					     p_params->tpa_mode,
					     p_params->max_buffers_per_cqe,
					     p_params->only_untagged);
	}

	return qed_sp_eth_vport_start(p_hwfn, p_params);
}

static int
qed_sp_vport_update_rss(struct qed_hwfn *p_hwfn,
			struct vport_update_ramrod_data *p_ramrod,
			struct qed_rss_params *p_rss)
{
	struct eth_vport_rss_config *p_config;
	u16 capabilities = 0;
	int i, table_size;
	int rc = 0;

	if (!p_rss) {
		p_ramrod->common.update_rss_flg = 0;
		return rc;
	}
	p_config = &p_ramrod->rss_config;

	BUILD_BUG_ON(QED_RSS_IND_TABLE_SIZE != ETH_RSS_IND_TABLE_ENTRIES_NUM);

	rc = qed_fw_rss_eng(p_hwfn, p_rss->rss_eng_id, &p_config->rss_id);
	if (rc)
		return rc;

	p_ramrod->common.update_rss_flg = p_rss->update_rss_config;
	p_config->update_rss_capabilities = p_rss->update_rss_capabilities;
	p_config->update_rss_ind_table = p_rss->update_rss_ind_table;
	p_config->update_rss_key = p_rss->update_rss_key;

	p_config->rss_mode = p_rss->rss_enable ?
			     ETH_VPORT_RSS_MODE_REGULAR :
			     ETH_VPORT_RSS_MODE_DISABLED;

	SET_FIELD(capabilities,
		  ETH_VPORT_RSS_CONFIG_IPV4_CAPABILITY,
		  !!(p_rss->rss_caps & QED_RSS_IPV4));
	SET_FIELD(capabilities,
		  ETH_VPORT_RSS_CONFIG_IPV6_CAPABILITY,
		  !!(p_rss->rss_caps & QED_RSS_IPV6));
	SET_FIELD(capabilities,
		  ETH_VPORT_RSS_CONFIG_IPV4_TCP_CAPABILITY,
		  !!(p_rss->rss_caps & QED_RSS_IPV4_TCP));
	SET_FIELD(capabilities,
		  ETH_VPORT_RSS_CONFIG_IPV6_TCP_CAPABILITY,
		  !!(p_rss->rss_caps & QED_RSS_IPV6_TCP));
	SET_FIELD(capabilities,
		  ETH_VPORT_RSS_CONFIG_IPV4_UDP_CAPABILITY,
		  !!(p_rss->rss_caps & QED_RSS_IPV4_UDP));
	SET_FIELD(capabilities,
		  ETH_VPORT_RSS_CONFIG_IPV6_UDP_CAPABILITY,
		  !!(p_rss->rss_caps & QED_RSS_IPV6_UDP));
	p_config->tbl_size = p_rss->rss_table_size_log;

	p_config->capabilities = cpu_to_le16(capabilities);

	DP_VERBOSE(p_hwfn, NETIF_MSG_IFUP,
		   "update rss flag %d, rss_mode = %d, update_caps = %d, capabilities = %d, update_ind = %d, update_rss_key = %d\n",
		   p_ramrod->common.update_rss_flg,
		   p_config->rss_mode,
		   p_config->update_rss_capabilities,
		   p_config->capabilities,
		   p_config->update_rss_ind_table, p_config->update_rss_key);

	table_size = min_t(int, QED_RSS_IND_TABLE_SIZE,
			   1 << p_config->tbl_size);
	for (i = 0; i < table_size; i++) {
		struct qed_queue_cid *p_queue = p_rss->rss_ind_table[i];

		if (!p_queue)
			return -EINVAL;

		p_config->indirection_table[i] =
		    cpu_to_le16(p_queue->abs.queue_id);
	}

	DP_VERBOSE(p_hwfn, NETIF_MSG_IFUP,
		   "Configured RSS indirection table [%d entries]:\n",
		   table_size);
	for (i = 0; i < QED_RSS_IND_TABLE_SIZE; i += 0x10) {
		DP_VERBOSE(p_hwfn,
			   NETIF_MSG_IFUP,
			   "%04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x\n",
			   le16_to_cpu(p_config->indirection_table[i]),
			   le16_to_cpu(p_config->indirection_table[i + 1]),
			   le16_to_cpu(p_config->indirection_table[i + 2]),
			   le16_to_cpu(p_config->indirection_table[i + 3]),
			   le16_to_cpu(p_config->indirection_table[i + 4]),
			   le16_to_cpu(p_config->indirection_table[i + 5]),
			   le16_to_cpu(p_config->indirection_table[i + 6]),
			   le16_to_cpu(p_config->indirection_table[i + 7]),
			   le16_to_cpu(p_config->indirection_table[i + 8]),
			   le16_to_cpu(p_config->indirection_table[i + 9]),
			   le16_to_cpu(p_config->indirection_table[i + 10]),
			   le16_to_cpu(p_config->indirection_table[i + 11]),
			   le16_to_cpu(p_config->indirection_table[i + 12]),
			   le16_to_cpu(p_config->indirection_table[i + 13]),
			   le16_to_cpu(p_config->indirection_table[i + 14]),
			   le16_to_cpu(p_config->indirection_table[i + 15]));
	}

	for (i = 0; i < 10; i++)
		p_config->rss_key[i] = cpu_to_le32(p_rss->rss_key[i]);

	return rc;
}

static void
qed_sp_update_accept_mode(struct qed_hwfn *p_hwfn,
			  struct vport_update_ramrod_data *p_ramrod,
			  struct qed_filter_accept_flags accept_flags)
{
	p_ramrod->common.update_rx_mode_flg =
		accept_flags.update_rx_mode_config;

	p_ramrod->common.update_tx_mode_flg =
		accept_flags.update_tx_mode_config;

	/* Set Rx mode accept flags */
	if (p_ramrod->common.update_rx_mode_flg) {
		u8 accept_filter = accept_flags.rx_accept_filter;
		u16 state = 0;

		SET_FIELD(state, ETH_VPORT_RX_MODE_UCAST_DROP_ALL,
			  !(!!(accept_filter & QED_ACCEPT_UCAST_MATCHED) ||
			    !!(accept_filter & QED_ACCEPT_UCAST_UNMATCHED)));

		SET_FIELD(state, ETH_VPORT_RX_MODE_UCAST_ACCEPT_UNMATCHED,
			  !!(accept_filter & QED_ACCEPT_UCAST_UNMATCHED));

		SET_FIELD(state, ETH_VPORT_RX_MODE_MCAST_DROP_ALL,
			  !(!!(accept_filter & QED_ACCEPT_MCAST_MATCHED) ||
			    !!(accept_filter & QED_ACCEPT_MCAST_UNMATCHED)));

		SET_FIELD(state, ETH_VPORT_RX_MODE_MCAST_ACCEPT_ALL,
			  (!!(accept_filter & QED_ACCEPT_MCAST_MATCHED) &&
			   !!(accept_filter & QED_ACCEPT_MCAST_UNMATCHED)));

		SET_FIELD(state, ETH_VPORT_RX_MODE_BCAST_ACCEPT_ALL,
			  !!(accept_filter & QED_ACCEPT_BCAST));

		SET_FIELD(state, ETH_VPORT_RX_MODE_ACCEPT_ANY_VNI,
			  !!(accept_filter & QED_ACCEPT_ANY_VNI));

		p_ramrod->rx_mode.state = cpu_to_le16(state);
		DP_VERBOSE(p_hwfn, QED_MSG_SP,
			   "p_ramrod->rx_mode.state = 0x%x\n", state);
	}

	/* Set Tx mode accept flags */
	if (p_ramrod->common.update_tx_mode_flg) {
		u8 accept_filter = accept_flags.tx_accept_filter;
		u16 state = 0;

		SET_FIELD(state, ETH_VPORT_TX_MODE_UCAST_DROP_ALL,
			  !!(accept_filter & QED_ACCEPT_NONE));

		SET_FIELD(state, ETH_VPORT_TX_MODE_MCAST_DROP_ALL,
			  !!(accept_filter & QED_ACCEPT_NONE));

		SET_FIELD(state, ETH_VPORT_TX_MODE_MCAST_ACCEPT_ALL,
			  (!!(accept_filter & QED_ACCEPT_MCAST_MATCHED) &&
			   !!(accept_filter & QED_ACCEPT_MCAST_UNMATCHED)));

		SET_FIELD(state, ETH_VPORT_TX_MODE_UCAST_ACCEPT_ALL,
			  (!!(accept_filter & QED_ACCEPT_UCAST_MATCHED) &&
			   !!(accept_filter & QED_ACCEPT_UCAST_UNMATCHED)));

		SET_FIELD(state, ETH_VPORT_TX_MODE_BCAST_ACCEPT_ALL,
			  !!(accept_filter & QED_ACCEPT_BCAST));

		p_ramrod->tx_mode.state = cpu_to_le16(state);
		DP_VERBOSE(p_hwfn, QED_MSG_SP,
			   "p_ramrod->tx_mode.state = 0x%x\n", state);
	}
}

static void
qed_sp_vport_update_sge_tpa(struct qed_hwfn *p_hwfn,
			    struct vport_update_ramrod_data *p_ramrod,
			    struct qed_sge_tpa_params *p_params)
{
	struct eth_vport_tpa_param *p_tpa;

	if (!p_params) {
		p_ramrod->common.update_tpa_param_flg = 0;
		p_ramrod->common.update_tpa_en_flg = 0;
		p_ramrod->common.update_tpa_param_flg = 0;
		return;
	}

	p_ramrod->common.update_tpa_en_flg = p_params->update_tpa_en_flg;
	p_tpa = &p_ramrod->tpa_param;
	p_tpa->tpa_ipv4_en_flg = p_params->tpa_ipv4_en_flg;
	p_tpa->tpa_ipv6_en_flg = p_params->tpa_ipv6_en_flg;
	p_tpa->tpa_ipv4_tunn_en_flg = p_params->tpa_ipv4_tunn_en_flg;
	p_tpa->tpa_ipv6_tunn_en_flg = p_params->tpa_ipv6_tunn_en_flg;

	p_ramrod->common.update_tpa_param_flg = p_params->update_tpa_param_flg;
	p_tpa->max_buff_num = p_params->max_buffers_per_cqe;
	p_tpa->tpa_pkt_split_flg = p_params->tpa_pkt_split_flg;
	p_tpa->tpa_hdr_data_split_flg = p_params->tpa_hdr_data_split_flg;
	p_tpa->tpa_gro_consistent_flg = p_params->tpa_gro_consistent_flg;
	p_tpa->tpa_max_aggs_num = p_params->tpa_max_aggs_num;
	p_tpa->tpa_max_size = p_params->tpa_max_size;
	p_tpa->tpa_min_size_to_start = p_params->tpa_min_size_to_start;
	p_tpa->tpa_min_size_to_cont = p_params->tpa_min_size_to_cont;
}

static void
qed_sp_update_mcast_bin(struct qed_hwfn *p_hwfn,
			struct vport_update_ramrod_data *p_ramrod,
			struct qed_sp_vport_update_params *p_params)
{
	int i;

	memset(&p_ramrod->approx_mcast.bins, 0,
	       sizeof(p_ramrod->approx_mcast.bins));

	if (!p_params->update_approx_mcast_flg)
		return;

	p_ramrod->common.update_approx_mcast_flg = 1;
	for (i = 0; i < ETH_MULTICAST_MAC_BINS_IN_REGS; i++) {
		u32 *p_bins = p_params->bins;

		p_ramrod->approx_mcast.bins[i] = cpu_to_le32(p_bins[i]);
	}
}

int qed_sp_vport_update(struct qed_hwfn *p_hwfn,
			struct qed_sp_vport_update_params *p_params,
			enum spq_mode comp_mode,
			struct qed_spq_comp_cb *p_comp_data)
{
	struct qed_rss_params *p_rss_params = p_params->rss_params;
	struct vport_update_ramrod_data_cmn *p_cmn;
	struct qed_sp_init_data init_data;
	struct vport_update_ramrod_data *p_ramrod = NULL;
	struct qed_spq_entry *p_ent = NULL;
	u8 abs_vport_id = 0, val;
	int rc = -EINVAL;

	if (IS_VF(p_hwfn->cdev)) {
		rc = qed_vf_pf_vport_update(p_hwfn, p_params);
		return rc;
	}

	rc = qed_fw_vport(p_hwfn, p_params->vport_id, &abs_vport_id);
	if (rc)
		return rc;

	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_params->opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_data;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 ETH_RAMROD_VPORT_UPDATE,
				 PROTOCOLID_ETH, &init_data);
	if (rc)
		return rc;

	/* Copy input params to ramrod according to FW struct */
	p_ramrod = &p_ent->ramrod.vport_update;
	p_cmn = &p_ramrod->common;

	p_cmn->vport_id = abs_vport_id;
	p_cmn->rx_active_flg = p_params->vport_active_rx_flg;
	p_cmn->update_rx_active_flg = p_params->update_vport_active_rx_flg;
	p_cmn->tx_active_flg = p_params->vport_active_tx_flg;
	p_cmn->update_tx_active_flg = p_params->update_vport_active_tx_flg;
	p_cmn->accept_any_vlan = p_params->accept_any_vlan;
	val = p_params->update_accept_any_vlan_flg;
	p_cmn->update_accept_any_vlan_flg = val;

	p_cmn->inner_vlan_removal_en = p_params->inner_vlan_removal_flg;
	val = p_params->update_inner_vlan_removal_flg;
	p_cmn->update_inner_vlan_removal_en_flg = val;

	p_cmn->default_vlan_en = p_params->default_vlan_enable_flg;
	val = p_params->update_default_vlan_enable_flg;
	p_cmn->update_default_vlan_en_flg = val;

	p_cmn->default_vlan = cpu_to_le16(p_params->default_vlan);
	p_cmn->update_default_vlan_flg = p_params->update_default_vlan_flg;

	p_cmn->silent_vlan_removal_en = p_params->silent_vlan_removal_flg;

	p_ramrod->common.tx_switching_en = p_params->tx_switching_flg;
	p_cmn->update_tx_switching_en_flg = p_params->update_tx_switching_flg;

	p_cmn->anti_spoofing_en = p_params->anti_spoofing_en;
	val = p_params->update_anti_spoofing_en_flg;
	p_ramrod->common.update_anti_spoofing_en_flg = val;

	rc = qed_sp_vport_update_rss(p_hwfn, p_ramrod, p_rss_params);
	if (rc) {
		qed_sp_destroy_request(p_hwfn, p_ent);
		return rc;
	}

	/* Update mcast bins for VFs, PF doesn't use this functionality */
	qed_sp_update_mcast_bin(p_hwfn, p_ramrod, p_params);

	qed_sp_update_accept_mode(p_hwfn, p_ramrod, p_params->accept_flags);
	qed_sp_vport_update_sge_tpa(p_hwfn, p_ramrod, p_params->sge_tpa_params);
	return qed_spq_post(p_hwfn, p_ent, NULL);
}

int qed_sp_vport_stop(struct qed_hwfn *p_hwfn, u16 opaque_fid, u8 vport_id)
{
	struct vport_stop_ramrod_data *p_ramrod;
	struct qed_sp_init_data init_data;
	struct qed_spq_entry *p_ent;
	u8 abs_vport_id = 0;
	int rc;

	if (IS_VF(p_hwfn->cdev))
		return qed_vf_pf_vport_stop(p_hwfn);

	rc = qed_fw_vport(p_hwfn, vport_id, &abs_vport_id);
	if (rc)
		return rc;

	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 ETH_RAMROD_VPORT_STOP,
				 PROTOCOLID_ETH, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.vport_stop;
	p_ramrod->vport_id = abs_vport_id;

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int
qed_vf_pf_accept_flags(struct qed_hwfn *p_hwfn,
		       struct qed_filter_accept_flags *p_accept_flags)
{
	struct qed_sp_vport_update_params s_params;

	memset(&s_params, 0, sizeof(s_params));
	memcpy(&s_params.accept_flags, p_accept_flags,
	       sizeof(struct qed_filter_accept_flags));

	return qed_vf_pf_vport_update(p_hwfn, &s_params);
}

static int qed_filter_accept_cmd(struct qed_dev *cdev,
				 u8 vport,
				 struct qed_filter_accept_flags accept_flags,
				 u8 update_accept_any_vlan,
				 u8 accept_any_vlan,
				 enum spq_mode comp_mode,
				 struct qed_spq_comp_cb *p_comp_data)
{
	struct qed_sp_vport_update_params vport_update_params;
	int i, rc;

	/* Prepare and send the vport rx_mode change */
	memset(&vport_update_params, 0, sizeof(vport_update_params));
	vport_update_params.vport_id = vport;
	vport_update_params.accept_flags = accept_flags;
	vport_update_params.update_accept_any_vlan_flg = update_accept_any_vlan;
	vport_update_params.accept_any_vlan = accept_any_vlan;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		vport_update_params.opaque_fid = p_hwfn->hw_info.opaque_fid;

		if (IS_VF(cdev)) {
			rc = qed_vf_pf_accept_flags(p_hwfn, &accept_flags);
			if (rc)
				return rc;
			continue;
		}

		rc = qed_sp_vport_update(p_hwfn, &vport_update_params,
					 comp_mode, p_comp_data);
		if (rc) {
			DP_ERR(cdev, "Update rx_mode failed %d\n", rc);
			return rc;
		}

		DP_VERBOSE(p_hwfn, QED_MSG_SP,
			   "Accept filter configured, flags = [Rx]%x [Tx]%x\n",
			   accept_flags.rx_accept_filter,
			   accept_flags.tx_accept_filter);
		if (update_accept_any_vlan)
			DP_VERBOSE(p_hwfn, QED_MSG_SP,
				   "accept_any_vlan=%d configured\n",
				   accept_any_vlan);
	}

	return 0;
}

int qed_eth_rxq_start_ramrod(struct qed_hwfn *p_hwfn,
			     struct qed_queue_cid *p_cid,
			     u16 bd_max_bytes,
			     dma_addr_t bd_chain_phys_addr,
			     dma_addr_t cqe_pbl_addr, u16 cqe_pbl_size)
{
	struct rx_queue_start_ramrod_data *p_ramrod = NULL;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = -EINVAL;

	DP_VERBOSE(p_hwfn, QED_MSG_SP,
		   "opaque_fid=0x%x, cid=0x%x, rx_qzone=0x%x, vport_id=0x%x, sb_id=0x%x\n",
		   p_cid->opaque_fid, p_cid->cid,
		   p_cid->abs.queue_id, p_cid->abs.vport_id, p_cid->sb_igu_id);

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = p_cid->cid;
	init_data.opaque_fid = p_cid->opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 ETH_RAMROD_RX_QUEUE_START,
				 PROTOCOLID_ETH, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.rx_queue_start;

	p_ramrod->sb_id = cpu_to_le16(p_cid->sb_igu_id);
	p_ramrod->sb_index = p_cid->sb_idx;
	p_ramrod->vport_id = p_cid->abs.vport_id;
	p_ramrod->stats_counter_id = p_cid->abs.stats_id;
	p_ramrod->rx_queue_id = cpu_to_le16(p_cid->abs.queue_id);
	p_ramrod->complete_cqe_flg = 0;
	p_ramrod->complete_event_flg = 1;

	p_ramrod->bd_max_bytes = cpu_to_le16(bd_max_bytes);
	DMA_REGPAIR_LE(p_ramrod->bd_base, bd_chain_phys_addr);

	p_ramrod->num_of_pbl_pages = cpu_to_le16(cqe_pbl_size);
	DMA_REGPAIR_LE(p_ramrod->cqe_pbl_addr, cqe_pbl_addr);

	if (p_cid->vfid != QED_QUEUE_CID_SELF) {
		bool b_legacy_vf = !!(p_cid->vf_legacy &
				      QED_QCID_LEGACY_VF_RX_PROD);

		p_ramrod->vf_rx_prod_index = p_cid->vf_qid;
		DP_VERBOSE(p_hwfn, QED_MSG_SP,
			   "Queue%s is meant for VF rxq[%02x]\n",
			   b_legacy_vf ? " [legacy]" : "", p_cid->vf_qid);
		p_ramrod->vf_rx_prod_use_zone_a = b_legacy_vf;
	}

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int
qed_eth_pf_rx_queue_start(struct qed_hwfn *p_hwfn,
			  struct qed_queue_cid *p_cid,
			  u16 bd_max_bytes,
			  dma_addr_t bd_chain_phys_addr,
			  dma_addr_t cqe_pbl_addr,
			  u16 cqe_pbl_size, void __iomem **pp_prod)
{
	u32 init_prod_val = 0;

	*pp_prod = p_hwfn->regview +
		   GTT_BAR0_MAP_REG_MSDM_RAM +
		    MSTORM_ETH_PF_PRODS_OFFSET(p_cid->abs.queue_id);

	/* Init the rcq, rx bd and rx sge (if valid) producers to 0 */
	__internal_ram_wr(p_hwfn, *pp_prod, sizeof(u32),
			  (u32 *)(&init_prod_val));

	return qed_eth_rxq_start_ramrod(p_hwfn, p_cid,
					bd_max_bytes,
					bd_chain_phys_addr,
					cqe_pbl_addr, cqe_pbl_size);
}

static int
qed_eth_rx_queue_start(struct qed_hwfn *p_hwfn,
		       u16 opaque_fid,
		       struct qed_queue_start_common_params *p_params,
		       u16 bd_max_bytes,
		       dma_addr_t bd_chain_phys_addr,
		       dma_addr_t cqe_pbl_addr,
		       u16 cqe_pbl_size,
		       struct qed_rxq_start_ret_params *p_ret_params)
{
	struct qed_queue_cid *p_cid;
	int rc;

	/* Allocate a CID for the queue */
	p_cid = qed_eth_queue_to_cid_pf(p_hwfn, opaque_fid, true, p_params);
	if (!p_cid)
		return -ENOMEM;

	if (IS_PF(p_hwfn->cdev)) {
		rc = qed_eth_pf_rx_queue_start(p_hwfn, p_cid,
					       bd_max_bytes,
					       bd_chain_phys_addr,
					       cqe_pbl_addr, cqe_pbl_size,
					       &p_ret_params->p_prod);
	} else {
		rc = qed_vf_pf_rxq_start(p_hwfn, p_cid,
					 bd_max_bytes,
					 bd_chain_phys_addr,
					 cqe_pbl_addr,
					 cqe_pbl_size, &p_ret_params->p_prod);
	}

	/* Provide the caller with a reference to as handler */
	if (rc)
		qed_eth_queue_cid_release(p_hwfn, p_cid);
	else
		p_ret_params->p_handle = (void *)p_cid;

	return rc;
}

int qed_sp_eth_rx_queues_update(struct qed_hwfn *p_hwfn,
				void **pp_rxq_handles,
				u8 num_rxqs,
				u8 complete_cqe_flg,
				u8 complete_event_flg,
				enum spq_mode comp_mode,
				struct qed_spq_comp_cb *p_comp_data)
{
	struct rx_queue_update_ramrod_data *p_ramrod = NULL;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	struct qed_queue_cid *p_cid;
	int rc = -EINVAL;
	u8 i;

	memset(&init_data, 0, sizeof(init_data));
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_data;

	for (i = 0; i < num_rxqs; i++) {
		p_cid = ((struct qed_queue_cid **)pp_rxq_handles)[i];

		/* Get SPQ entry */
		init_data.cid = p_cid->cid;
		init_data.opaque_fid = p_cid->opaque_fid;

		rc = qed_sp_init_request(p_hwfn, &p_ent,
					 ETH_RAMROD_RX_QUEUE_UPDATE,
					 PROTOCOLID_ETH, &init_data);
		if (rc)
			return rc;

		p_ramrod = &p_ent->ramrod.rx_queue_update;
		p_ramrod->vport_id = p_cid->abs.vport_id;

		p_ramrod->rx_queue_id = cpu_to_le16(p_cid->abs.queue_id);
		p_ramrod->complete_cqe_flg = complete_cqe_flg;
		p_ramrod->complete_event_flg = complete_event_flg;

		rc = qed_spq_post(p_hwfn, p_ent, NULL);
		if (rc)
			return rc;
	}

	return rc;
}

static int
qed_eth_pf_rx_queue_stop(struct qed_hwfn *p_hwfn,
			 struct qed_queue_cid *p_cid,
			 bool b_eq_completion_only, bool b_cqe_completion)
{
	struct rx_queue_stop_ramrod_data *p_ramrod = NULL;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc;

	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = p_cid->cid;
	init_data.opaque_fid = p_cid->opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 ETH_RAMROD_RX_QUEUE_STOP,
				 PROTOCOLID_ETH, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.rx_queue_stop;
	p_ramrod->vport_id = p_cid->abs.vport_id;
	p_ramrod->rx_queue_id = cpu_to_le16(p_cid->abs.queue_id);

	/* Cleaning the queue requires the completion to arrive there.
	 * In addition, VFs require the answer to come as eqe to PF.
	 */
	p_ramrod->complete_cqe_flg = ((p_cid->vfid == QED_QUEUE_CID_SELF) &&
				      !b_eq_completion_only) ||
				     b_cqe_completion;
	p_ramrod->complete_event_flg = (p_cid->vfid != QED_QUEUE_CID_SELF) ||
				       b_eq_completion_only;

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

int qed_eth_rx_queue_stop(struct qed_hwfn *p_hwfn,
			  void *p_rxq,
			  bool eq_completion_only, bool cqe_completion)
{
	struct qed_queue_cid *p_cid = (struct qed_queue_cid *)p_rxq;
	int rc = -EINVAL;

	if (IS_PF(p_hwfn->cdev))
		rc = qed_eth_pf_rx_queue_stop(p_hwfn, p_cid,
					      eq_completion_only,
					      cqe_completion);
	else
		rc = qed_vf_pf_rxq_stop(p_hwfn, p_cid, cqe_completion);

	if (!rc)
		qed_eth_queue_cid_release(p_hwfn, p_cid);
	return rc;
}

int
qed_eth_txq_start_ramrod(struct qed_hwfn *p_hwfn,
			 struct qed_queue_cid *p_cid,
			 dma_addr_t pbl_addr, u16 pbl_size, u16 pq_id)
{
	struct tx_queue_start_ramrod_data *p_ramrod = NULL;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = -EINVAL;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = p_cid->cid;
	init_data.opaque_fid = p_cid->opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 ETH_RAMROD_TX_QUEUE_START,
				 PROTOCOLID_ETH, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.tx_queue_start;
	p_ramrod->vport_id = p_cid->abs.vport_id;

	p_ramrod->sb_id = cpu_to_le16(p_cid->sb_igu_id);
	p_ramrod->sb_index = p_cid->sb_idx;
	p_ramrod->stats_counter_id = p_cid->abs.stats_id;

	p_ramrod->queue_zone_id = cpu_to_le16(p_cid->abs.queue_id);
	p_ramrod->same_as_last_id = cpu_to_le16(p_cid->abs.queue_id);

	p_ramrod->pbl_size = cpu_to_le16(pbl_size);
	DMA_REGPAIR_LE(p_ramrod->pbl_base_addr, pbl_addr);

	p_ramrod->qm_pq_id = cpu_to_le16(pq_id);

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int
qed_eth_pf_tx_queue_start(struct qed_hwfn *p_hwfn,
			  struct qed_queue_cid *p_cid,
			  u8 tc,
			  dma_addr_t pbl_addr,
			  u16 pbl_size, void __iomem **pp_doorbell)
{
	int rc;


	rc = qed_eth_txq_start_ramrod(p_hwfn, p_cid,
				      pbl_addr, pbl_size,
				      qed_get_cm_pq_idx_mcos(p_hwfn, tc));
	if (rc)
		return rc;

	/* Provide the caller with the necessary return values */
	*pp_doorbell = p_hwfn->doorbells +
		       qed_db_addr(p_cid->cid, DQ_DEMS_LEGACY);

	return 0;
}

static int
qed_eth_tx_queue_start(struct qed_hwfn *p_hwfn,
		       u16 opaque_fid,
		       struct qed_queue_start_common_params *p_params,
		       u8 tc,
		       dma_addr_t pbl_addr,
		       u16 pbl_size,
		       struct qed_txq_start_ret_params *p_ret_params)
{
	struct qed_queue_cid *p_cid;
	int rc;

	p_cid = qed_eth_queue_to_cid_pf(p_hwfn, opaque_fid, false, p_params);
	if (!p_cid)
		return -EINVAL;

	if (IS_PF(p_hwfn->cdev))
		rc = qed_eth_pf_tx_queue_start(p_hwfn, p_cid, tc,
					       pbl_addr, pbl_size,
					       &p_ret_params->p_doorbell);
	else
		rc = qed_vf_pf_txq_start(p_hwfn, p_cid,
					 pbl_addr, pbl_size,
					 &p_ret_params->p_doorbell);

	if (rc)
		qed_eth_queue_cid_release(p_hwfn, p_cid);
	else
		p_ret_params->p_handle = (void *)p_cid;

	return rc;
}

static int
qed_eth_pf_tx_queue_stop(struct qed_hwfn *p_hwfn, struct qed_queue_cid *p_cid)
{
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc;

	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = p_cid->cid;
	init_data.opaque_fid = p_cid->opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 ETH_RAMROD_TX_QUEUE_STOP,
				 PROTOCOLID_ETH, &init_data);
	if (rc)
		return rc;

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

int qed_eth_tx_queue_stop(struct qed_hwfn *p_hwfn, void *p_handle)
{
	struct qed_queue_cid *p_cid = (struct qed_queue_cid *)p_handle;
	int rc;

	if (IS_PF(p_hwfn->cdev))
		rc = qed_eth_pf_tx_queue_stop(p_hwfn, p_cid);
	else
		rc = qed_vf_pf_txq_stop(p_hwfn, p_cid);

	if (!rc)
		qed_eth_queue_cid_release(p_hwfn, p_cid);
	return rc;
}

static enum eth_filter_action qed_filter_action(enum qed_filter_opcode opcode)
{
	enum eth_filter_action action = MAX_ETH_FILTER_ACTION;

	switch (opcode) {
	case QED_FILTER_ADD:
		action = ETH_FILTER_ACTION_ADD;
		break;
	case QED_FILTER_REMOVE:
		action = ETH_FILTER_ACTION_REMOVE;
		break;
	case QED_FILTER_FLUSH:
		action = ETH_FILTER_ACTION_REMOVE_ALL;
		break;
	default:
		action = MAX_ETH_FILTER_ACTION;
	}

	return action;
}

static int
qed_filter_ucast_common(struct qed_hwfn *p_hwfn,
			u16 opaque_fid,
			struct qed_filter_ucast *p_filter_cmd,
			struct vport_filter_update_ramrod_data **pp_ramrod,
			struct qed_spq_entry **pp_ent,
			enum spq_mode comp_mode,
			struct qed_spq_comp_cb *p_comp_data)
{
	u8 vport_to_add_to = 0, vport_to_remove_from = 0;
	struct vport_filter_update_ramrod_data *p_ramrod;
	struct eth_filter_cmd *p_first_filter;
	struct eth_filter_cmd *p_second_filter;
	struct qed_sp_init_data init_data;
	enum eth_filter_action action;
	int rc;

	rc = qed_fw_vport(p_hwfn, p_filter_cmd->vport_to_remove_from,
			  &vport_to_remove_from);
	if (rc)
		return rc;

	rc = qed_fw_vport(p_hwfn, p_filter_cmd->vport_to_add_to,
			  &vport_to_add_to);
	if (rc)
		return rc;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_data;

	rc = qed_sp_init_request(p_hwfn, pp_ent,
				 ETH_RAMROD_FILTERS_UPDATE,
				 PROTOCOLID_ETH, &init_data);
	if (rc)
		return rc;

	*pp_ramrod = &(*pp_ent)->ramrod.vport_filter_update;
	p_ramrod = *pp_ramrod;
	p_ramrod->filter_cmd_hdr.rx = p_filter_cmd->is_rx_filter ? 1 : 0;
	p_ramrod->filter_cmd_hdr.tx = p_filter_cmd->is_tx_filter ? 1 : 0;

	switch (p_filter_cmd->opcode) {
	case QED_FILTER_REPLACE:
	case QED_FILTER_MOVE:
		p_ramrod->filter_cmd_hdr.cmd_cnt = 2; break;
	default:
		p_ramrod->filter_cmd_hdr.cmd_cnt = 1; break;
	}

	p_first_filter	= &p_ramrod->filter_cmds[0];
	p_second_filter = &p_ramrod->filter_cmds[1];

	switch (p_filter_cmd->type) {
	case QED_FILTER_MAC:
		p_first_filter->type = ETH_FILTER_TYPE_MAC; break;
	case QED_FILTER_VLAN:
		p_first_filter->type = ETH_FILTER_TYPE_VLAN; break;
	case QED_FILTER_MAC_VLAN:
		p_first_filter->type = ETH_FILTER_TYPE_PAIR; break;
	case QED_FILTER_INNER_MAC:
		p_first_filter->type = ETH_FILTER_TYPE_INNER_MAC; break;
	case QED_FILTER_INNER_VLAN:
		p_first_filter->type = ETH_FILTER_TYPE_INNER_VLAN; break;
	case QED_FILTER_INNER_PAIR:
		p_first_filter->type = ETH_FILTER_TYPE_INNER_PAIR; break;
	case QED_FILTER_INNER_MAC_VNI_PAIR:
		p_first_filter->type = ETH_FILTER_TYPE_INNER_MAC_VNI_PAIR;
		break;
	case QED_FILTER_MAC_VNI_PAIR:
		p_first_filter->type = ETH_FILTER_TYPE_MAC_VNI_PAIR; break;
	case QED_FILTER_VNI:
		p_first_filter->type = ETH_FILTER_TYPE_VNI; break;
	}

	if ((p_first_filter->type == ETH_FILTER_TYPE_MAC) ||
	    (p_first_filter->type == ETH_FILTER_TYPE_PAIR) ||
	    (p_first_filter->type == ETH_FILTER_TYPE_INNER_MAC) ||
	    (p_first_filter->type == ETH_FILTER_TYPE_INNER_PAIR) ||
	    (p_first_filter->type == ETH_FILTER_TYPE_INNER_MAC_VNI_PAIR) ||
	    (p_first_filter->type == ETH_FILTER_TYPE_MAC_VNI_PAIR)) {
		qed_set_fw_mac_addr(&p_first_filter->mac_msb,
				    &p_first_filter->mac_mid,
				    &p_first_filter->mac_lsb,
				    (u8 *)p_filter_cmd->mac);
	}

	if ((p_first_filter->type == ETH_FILTER_TYPE_VLAN) ||
	    (p_first_filter->type == ETH_FILTER_TYPE_PAIR) ||
	    (p_first_filter->type == ETH_FILTER_TYPE_INNER_VLAN) ||
	    (p_first_filter->type == ETH_FILTER_TYPE_INNER_PAIR))
		p_first_filter->vlan_id = cpu_to_le16(p_filter_cmd->vlan);

	if ((p_first_filter->type == ETH_FILTER_TYPE_INNER_MAC_VNI_PAIR) ||
	    (p_first_filter->type == ETH_FILTER_TYPE_MAC_VNI_PAIR) ||
	    (p_first_filter->type == ETH_FILTER_TYPE_VNI))
		p_first_filter->vni = cpu_to_le32(p_filter_cmd->vni);

	if (p_filter_cmd->opcode == QED_FILTER_MOVE) {
		p_second_filter->type = p_first_filter->type;
		p_second_filter->mac_msb = p_first_filter->mac_msb;
		p_second_filter->mac_mid = p_first_filter->mac_mid;
		p_second_filter->mac_lsb = p_first_filter->mac_lsb;
		p_second_filter->vlan_id = p_first_filter->vlan_id;
		p_second_filter->vni = p_first_filter->vni;

		p_first_filter->action = ETH_FILTER_ACTION_REMOVE;

		p_first_filter->vport_id = vport_to_remove_from;

		p_second_filter->action = ETH_FILTER_ACTION_ADD;
		p_second_filter->vport_id = vport_to_add_to;
	} else if (p_filter_cmd->opcode == QED_FILTER_REPLACE) {
		p_first_filter->vport_id = vport_to_add_to;
		memcpy(p_second_filter, p_first_filter,
		       sizeof(*p_second_filter));
		p_first_filter->action	= ETH_FILTER_ACTION_REMOVE_ALL;
		p_second_filter->action = ETH_FILTER_ACTION_ADD;
	} else {
		action = qed_filter_action(p_filter_cmd->opcode);

		if (action == MAX_ETH_FILTER_ACTION) {
			DP_NOTICE(p_hwfn,
				  "%d is not supported yet\n",
				  p_filter_cmd->opcode);
			qed_sp_destroy_request(p_hwfn, *pp_ent);
			return -EINVAL;
		}

		p_first_filter->action = action;
		p_first_filter->vport_id = (p_filter_cmd->opcode ==
					    QED_FILTER_REMOVE) ?
					   vport_to_remove_from :
					   vport_to_add_to;
	}

	return 0;
}

int qed_sp_eth_filter_ucast(struct qed_hwfn *p_hwfn,
			    u16 opaque_fid,
			    struct qed_filter_ucast *p_filter_cmd,
			    enum spq_mode comp_mode,
			    struct qed_spq_comp_cb *p_comp_data)
{
	struct vport_filter_update_ramrod_data	*p_ramrod	= NULL;
	struct qed_spq_entry			*p_ent		= NULL;
	struct eth_filter_cmd_header		*p_header;
	int					rc;

	rc = qed_filter_ucast_common(p_hwfn, opaque_fid, p_filter_cmd,
				     &p_ramrod, &p_ent,
				     comp_mode, p_comp_data);
	if (rc) {
		DP_ERR(p_hwfn, "Uni. filter command failed %d\n", rc);
		return rc;
	}
	p_header = &p_ramrod->filter_cmd_hdr;
	p_header->assert_on_error = p_filter_cmd->assert_on_error;

	rc = qed_spq_post(p_hwfn, p_ent, NULL);
	if (rc) {
		DP_ERR(p_hwfn, "Unicast filter ADD command failed %d\n", rc);
		return rc;
	}

	DP_VERBOSE(p_hwfn, QED_MSG_SP,
		   "Unicast filter configured, opcode = %s, type = %s, cmd_cnt = %d, is_rx_filter = %d, is_tx_filter = %d\n",
		   (p_filter_cmd->opcode == QED_FILTER_ADD) ? "ADD" :
		   ((p_filter_cmd->opcode == QED_FILTER_REMOVE) ?
		   "REMOVE" :
		   ((p_filter_cmd->opcode == QED_FILTER_MOVE) ?
		    "MOVE" : "REPLACE")),
		   (p_filter_cmd->type == QED_FILTER_MAC) ? "MAC" :
		   ((p_filter_cmd->type == QED_FILTER_VLAN) ?
		    "VLAN" : "MAC & VLAN"),
		   p_ramrod->filter_cmd_hdr.cmd_cnt,
		   p_filter_cmd->is_rx_filter,
		   p_filter_cmd->is_tx_filter);
	DP_VERBOSE(p_hwfn, QED_MSG_SP,
		   "vport_to_add_to = %d, vport_to_remove_from = %d, mac = %2x:%2x:%2x:%2x:%2x:%2x, vlan = %d\n",
		   p_filter_cmd->vport_to_add_to,
		   p_filter_cmd->vport_to_remove_from,
		   p_filter_cmd->mac[0],
		   p_filter_cmd->mac[1],
		   p_filter_cmd->mac[2],
		   p_filter_cmd->mac[3],
		   p_filter_cmd->mac[4],
		   p_filter_cmd->mac[5],
		   p_filter_cmd->vlan);

	return 0;
}

/*******************************************************************************
 * Description:
 *         Calculates crc 32 on a buffer
 *         Note: crc32_length MUST be aligned to 8
 * Return:
 ******************************************************************************/
static u32 qed_calc_crc32c(u8 *crc32_packet,
			   u32 crc32_length, u32 crc32_seed, u8 complement)
{
	u32 byte = 0, bit = 0, crc32_result = crc32_seed;
	u8 msb = 0, current_byte = 0;

	if ((!crc32_packet) ||
	    (crc32_length == 0) ||
	    ((crc32_length % 8) != 0))
		return crc32_result;
	for (byte = 0; byte < crc32_length; byte++) {
		current_byte = crc32_packet[byte];
		for (bit = 0; bit < 8; bit++) {
			msb = (u8)(crc32_result >> 31);
			crc32_result = crc32_result << 1;
			if (msb != (0x1 & (current_byte >> bit))) {
				crc32_result = crc32_result ^ CRC32_POLY;
				crc32_result |= 1; /*crc32_result[0] = 1;*/
			}
		}
	}
	return crc32_result;
}

static u32 qed_crc32c_le(u32 seed, u8 *mac, u32 len)
{
	u32 packet_buf[2] = { 0 };

	memcpy((u8 *)(&packet_buf[0]), &mac[0], 6);
	return qed_calc_crc32c((u8 *)packet_buf, 8, seed, 0);
}

u8 qed_mcast_bin_from_mac(u8 *mac)
{
	u32 crc = qed_crc32c_le(ETH_MULTICAST_BIN_FROM_MAC_SEED,
				mac, ETH_ALEN);

	return crc & 0xff;
}

static int
qed_sp_eth_filter_mcast(struct qed_hwfn *p_hwfn,
			u16 opaque_fid,
			struct qed_filter_mcast *p_filter_cmd,
			enum spq_mode comp_mode,
			struct qed_spq_comp_cb *p_comp_data)
{
	struct vport_update_ramrod_data *p_ramrod = NULL;
	u32 bins[ETH_MULTICAST_MAC_BINS_IN_REGS];
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	u8 abs_vport_id = 0;
	int rc, i;

	if (p_filter_cmd->opcode == QED_FILTER_ADD)
		rc = qed_fw_vport(p_hwfn, p_filter_cmd->vport_to_add_to,
				  &abs_vport_id);
	else
		rc = qed_fw_vport(p_hwfn, p_filter_cmd->vport_to_remove_from,
				  &abs_vport_id);
	if (rc)
		return rc;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_data;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 ETH_RAMROD_VPORT_UPDATE,
				 PROTOCOLID_ETH, &init_data);
	if (rc) {
		DP_ERR(p_hwfn, "Multi-cast command failed %d\n", rc);
		return rc;
	}

	p_ramrod = &p_ent->ramrod.vport_update;
	p_ramrod->common.update_approx_mcast_flg = 1;

	/* explicitly clear out the entire vector */
	memset(&p_ramrod->approx_mcast.bins, 0,
	       sizeof(p_ramrod->approx_mcast.bins));
	memset(bins, 0, sizeof(bins));
	/* filter ADD op is explicit set op and it removes
	 *  any existing filters for the vport
	 */
	if (p_filter_cmd->opcode == QED_FILTER_ADD) {
		for (i = 0; i < p_filter_cmd->num_mc_addrs; i++) {
			u32 bit, nbits;

			bit = qed_mcast_bin_from_mac(p_filter_cmd->mac[i]);
			nbits = sizeof(u32) * BITS_PER_BYTE;
			bins[bit / nbits] |= 1 << (bit % nbits);
		}

		/* Convert to correct endianity */
		for (i = 0; i < ETH_MULTICAST_MAC_BINS_IN_REGS; i++) {
			struct vport_update_ramrod_mcast *p_ramrod_bins;

			p_ramrod_bins = &p_ramrod->approx_mcast;
			p_ramrod_bins->bins[i] = cpu_to_le32(bins[i]);
		}
	}

	p_ramrod->common.vport_id = abs_vport_id;

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int qed_filter_mcast_cmd(struct qed_dev *cdev,
				struct qed_filter_mcast *p_filter_cmd,
				enum spq_mode comp_mode,
				struct qed_spq_comp_cb *p_comp_data)
{
	int rc = 0;
	int i;

	/* only ADD and REMOVE operations are supported for multi-cast */
	if ((p_filter_cmd->opcode != QED_FILTER_ADD &&
	     (p_filter_cmd->opcode != QED_FILTER_REMOVE)) ||
	    (p_filter_cmd->num_mc_addrs > QED_MAX_MC_ADDRS))
		return -EINVAL;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		u16 opaque_fid;

		if (IS_VF(cdev)) {
			qed_vf_pf_filter_mcast(p_hwfn, p_filter_cmd);
			continue;
		}

		opaque_fid = p_hwfn->hw_info.opaque_fid;

		rc = qed_sp_eth_filter_mcast(p_hwfn,
					     opaque_fid,
					     p_filter_cmd,
					     comp_mode, p_comp_data);
	}
	return rc;
}

static int qed_filter_ucast_cmd(struct qed_dev *cdev,
				struct qed_filter_ucast *p_filter_cmd,
				enum spq_mode comp_mode,
				struct qed_spq_comp_cb *p_comp_data)
{
	int rc = 0;
	int i;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];
		u16 opaque_fid;

		if (IS_VF(cdev)) {
			rc = qed_vf_pf_filter_ucast(p_hwfn, p_filter_cmd);
			continue;
		}

		opaque_fid = p_hwfn->hw_info.opaque_fid;

		rc = qed_sp_eth_filter_ucast(p_hwfn,
					     opaque_fid,
					     p_filter_cmd,
					     comp_mode, p_comp_data);
		if (rc)
			break;
	}

	return rc;
}

/* Statistics related code */
static void __qed_get_vport_pstats_addrlen(struct qed_hwfn *p_hwfn,
					   u32 *p_addr,
					   u32 *p_len, u16 statistics_bin)
{
	if (IS_PF(p_hwfn->cdev)) {
		*p_addr = BAR0_MAP_REG_PSDM_RAM +
		    PSTORM_QUEUE_STAT_OFFSET(statistics_bin);
		*p_len = sizeof(struct eth_pstorm_per_queue_stat);
	} else {
		struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;
		struct pfvf_acquire_resp_tlv *p_resp = &p_iov->acquire_resp;

		*p_addr = p_resp->pfdev_info.stats_info.pstats.address;
		*p_len = p_resp->pfdev_info.stats_info.pstats.len;
	}
}

static void __qed_get_vport_pstats(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   struct qed_eth_stats *p_stats,
				   u16 statistics_bin)
{
	struct eth_pstorm_per_queue_stat pstats;
	u32 pstats_addr = 0, pstats_len = 0;

	__qed_get_vport_pstats_addrlen(p_hwfn, &pstats_addr, &pstats_len,
				       statistics_bin);

	memset(&pstats, 0, sizeof(pstats));
	qed_memcpy_from(p_hwfn, p_ptt, &pstats, pstats_addr, pstats_len);

	p_stats->common.tx_ucast_bytes +=
	    HILO_64_REGPAIR(pstats.sent_ucast_bytes);
	p_stats->common.tx_mcast_bytes +=
	    HILO_64_REGPAIR(pstats.sent_mcast_bytes);
	p_stats->common.tx_bcast_bytes +=
	    HILO_64_REGPAIR(pstats.sent_bcast_bytes);
	p_stats->common.tx_ucast_pkts +=
	    HILO_64_REGPAIR(pstats.sent_ucast_pkts);
	p_stats->common.tx_mcast_pkts +=
	    HILO_64_REGPAIR(pstats.sent_mcast_pkts);
	p_stats->common.tx_bcast_pkts +=
	    HILO_64_REGPAIR(pstats.sent_bcast_pkts);
	p_stats->common.tx_err_drop_pkts +=
	    HILO_64_REGPAIR(pstats.error_drop_pkts);
}

static void __qed_get_vport_tstats(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   struct qed_eth_stats *p_stats,
				   u16 statistics_bin)
{
	struct tstorm_per_port_stat tstats;
	u32 tstats_addr, tstats_len;

	if (IS_PF(p_hwfn->cdev)) {
		tstats_addr = BAR0_MAP_REG_TSDM_RAM +
		    TSTORM_PORT_STAT_OFFSET(MFW_PORT(p_hwfn));
		tstats_len = sizeof(struct tstorm_per_port_stat);
	} else {
		struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;
		struct pfvf_acquire_resp_tlv *p_resp = &p_iov->acquire_resp;

		tstats_addr = p_resp->pfdev_info.stats_info.tstats.address;
		tstats_len = p_resp->pfdev_info.stats_info.tstats.len;
	}

	memset(&tstats, 0, sizeof(tstats));
	qed_memcpy_from(p_hwfn, p_ptt, &tstats, tstats_addr, tstats_len);

	p_stats->common.mftag_filter_discards +=
	    HILO_64_REGPAIR(tstats.mftag_filter_discard);
	p_stats->common.mac_filter_discards +=
	    HILO_64_REGPAIR(tstats.eth_mac_filter_discard);
	p_stats->common.gft_filter_drop +=
		HILO_64_REGPAIR(tstats.eth_gft_drop_pkt);
}

static void __qed_get_vport_ustats_addrlen(struct qed_hwfn *p_hwfn,
					   u32 *p_addr,
					   u32 *p_len, u16 statistics_bin)
{
	if (IS_PF(p_hwfn->cdev)) {
		*p_addr = BAR0_MAP_REG_USDM_RAM +
		    USTORM_QUEUE_STAT_OFFSET(statistics_bin);
		*p_len = sizeof(struct eth_ustorm_per_queue_stat);
	} else {
		struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;
		struct pfvf_acquire_resp_tlv *p_resp = &p_iov->acquire_resp;

		*p_addr = p_resp->pfdev_info.stats_info.ustats.address;
		*p_len = p_resp->pfdev_info.stats_info.ustats.len;
	}
}

static void __qed_get_vport_ustats(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   struct qed_eth_stats *p_stats,
				   u16 statistics_bin)
{
	struct eth_ustorm_per_queue_stat ustats;
	u32 ustats_addr = 0, ustats_len = 0;

	__qed_get_vport_ustats_addrlen(p_hwfn, &ustats_addr, &ustats_len,
				       statistics_bin);

	memset(&ustats, 0, sizeof(ustats));
	qed_memcpy_from(p_hwfn, p_ptt, &ustats, ustats_addr, ustats_len);

	p_stats->common.rx_ucast_bytes +=
	    HILO_64_REGPAIR(ustats.rcv_ucast_bytes);
	p_stats->common.rx_mcast_bytes +=
	    HILO_64_REGPAIR(ustats.rcv_mcast_bytes);
	p_stats->common.rx_bcast_bytes +=
	    HILO_64_REGPAIR(ustats.rcv_bcast_bytes);
	p_stats->common.rx_ucast_pkts += HILO_64_REGPAIR(ustats.rcv_ucast_pkts);
	p_stats->common.rx_mcast_pkts += HILO_64_REGPAIR(ustats.rcv_mcast_pkts);
	p_stats->common.rx_bcast_pkts += HILO_64_REGPAIR(ustats.rcv_bcast_pkts);
}

static void __qed_get_vport_mstats_addrlen(struct qed_hwfn *p_hwfn,
					   u32 *p_addr,
					   u32 *p_len, u16 statistics_bin)
{
	if (IS_PF(p_hwfn->cdev)) {
		*p_addr = BAR0_MAP_REG_MSDM_RAM +
		    MSTORM_QUEUE_STAT_OFFSET(statistics_bin);
		*p_len = sizeof(struct eth_mstorm_per_queue_stat);
	} else {
		struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;
		struct pfvf_acquire_resp_tlv *p_resp = &p_iov->acquire_resp;

		*p_addr = p_resp->pfdev_info.stats_info.mstats.address;
		*p_len = p_resp->pfdev_info.stats_info.mstats.len;
	}
}

static void __qed_get_vport_mstats(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   struct qed_eth_stats *p_stats,
				   u16 statistics_bin)
{
	struct eth_mstorm_per_queue_stat mstats;
	u32 mstats_addr = 0, mstats_len = 0;

	__qed_get_vport_mstats_addrlen(p_hwfn, &mstats_addr, &mstats_len,
				       statistics_bin);

	memset(&mstats, 0, sizeof(mstats));
	qed_memcpy_from(p_hwfn, p_ptt, &mstats, mstats_addr, mstats_len);

	p_stats->common.no_buff_discards +=
	    HILO_64_REGPAIR(mstats.no_buff_discard);
	p_stats->common.packet_too_big_discard +=
	    HILO_64_REGPAIR(mstats.packet_too_big_discard);
	p_stats->common.ttl0_discard += HILO_64_REGPAIR(mstats.ttl0_discard);
	p_stats->common.tpa_coalesced_pkts +=
	    HILO_64_REGPAIR(mstats.tpa_coalesced_pkts);
	p_stats->common.tpa_coalesced_events +=
	    HILO_64_REGPAIR(mstats.tpa_coalesced_events);
	p_stats->common.tpa_aborts_num +=
	    HILO_64_REGPAIR(mstats.tpa_aborts_num);
	p_stats->common.tpa_coalesced_bytes +=
	    HILO_64_REGPAIR(mstats.tpa_coalesced_bytes);
}

static void __qed_get_vport_port_stats(struct qed_hwfn *p_hwfn,
				       struct qed_ptt *p_ptt,
				       struct qed_eth_stats *p_stats)
{
	struct qed_eth_stats_common *p_common = &p_stats->common;
	struct port_stats port_stats;
	int j;

	memset(&port_stats, 0, sizeof(port_stats));

	qed_memcpy_from(p_hwfn, p_ptt, &port_stats,
			p_hwfn->mcp_info->port_addr +
			offsetof(struct public_port, stats),
			sizeof(port_stats));

	p_common->rx_64_byte_packets += port_stats.eth.r64;
	p_common->rx_65_to_127_byte_packets += port_stats.eth.r127;
	p_common->rx_128_to_255_byte_packets += port_stats.eth.r255;
	p_common->rx_256_to_511_byte_packets += port_stats.eth.r511;
	p_common->rx_512_to_1023_byte_packets += port_stats.eth.r1023;
	p_common->rx_1024_to_1518_byte_packets += port_stats.eth.r1518;
	p_common->rx_crc_errors += port_stats.eth.rfcs;
	p_common->rx_mac_crtl_frames += port_stats.eth.rxcf;
	p_common->rx_pause_frames += port_stats.eth.rxpf;
	p_common->rx_pfc_frames += port_stats.eth.rxpp;
	p_common->rx_align_errors += port_stats.eth.raln;
	p_common->rx_carrier_errors += port_stats.eth.rfcr;
	p_common->rx_oversize_packets += port_stats.eth.rovr;
	p_common->rx_jabbers += port_stats.eth.rjbr;
	p_common->rx_undersize_packets += port_stats.eth.rund;
	p_common->rx_fragments += port_stats.eth.rfrg;
	p_common->tx_64_byte_packets += port_stats.eth.t64;
	p_common->tx_65_to_127_byte_packets += port_stats.eth.t127;
	p_common->tx_128_to_255_byte_packets += port_stats.eth.t255;
	p_common->tx_256_to_511_byte_packets += port_stats.eth.t511;
	p_common->tx_512_to_1023_byte_packets += port_stats.eth.t1023;
	p_common->tx_1024_to_1518_byte_packets += port_stats.eth.t1518;
	p_common->tx_pause_frames += port_stats.eth.txpf;
	p_common->tx_pfc_frames += port_stats.eth.txpp;
	p_common->rx_mac_bytes += port_stats.eth.rbyte;
	p_common->rx_mac_uc_packets += port_stats.eth.rxuca;
	p_common->rx_mac_mc_packets += port_stats.eth.rxmca;
	p_common->rx_mac_bc_packets += port_stats.eth.rxbca;
	p_common->rx_mac_frames_ok += port_stats.eth.rxpok;
	p_common->tx_mac_bytes += port_stats.eth.tbyte;
	p_common->tx_mac_uc_packets += port_stats.eth.txuca;
	p_common->tx_mac_mc_packets += port_stats.eth.txmca;
	p_common->tx_mac_bc_packets += port_stats.eth.txbca;
	p_common->tx_mac_ctrl_frames += port_stats.eth.txcf;
	for (j = 0; j < 8; j++) {
		p_common->brb_truncates += port_stats.brb.brb_truncate[j];
		p_common->brb_discards += port_stats.brb.brb_discard[j];
	}

	if (QED_IS_BB(p_hwfn->cdev)) {
		struct qed_eth_stats_bb *p_bb = &p_stats->bb;

		p_bb->rx_1519_to_1522_byte_packets +=
		    port_stats.eth.u0.bb0.r1522;
		p_bb->rx_1519_to_2047_byte_packets +=
		    port_stats.eth.u0.bb0.r2047;
		p_bb->rx_2048_to_4095_byte_packets +=
		    port_stats.eth.u0.bb0.r4095;
		p_bb->rx_4096_to_9216_byte_packets +=
		    port_stats.eth.u0.bb0.r9216;
		p_bb->rx_9217_to_16383_byte_packets +=
		    port_stats.eth.u0.bb0.r16383;
		p_bb->tx_1519_to_2047_byte_packets +=
		    port_stats.eth.u1.bb1.t2047;
		p_bb->tx_2048_to_4095_byte_packets +=
		    port_stats.eth.u1.bb1.t4095;
		p_bb->tx_4096_to_9216_byte_packets +=
		    port_stats.eth.u1.bb1.t9216;
		p_bb->tx_9217_to_16383_byte_packets +=
		    port_stats.eth.u1.bb1.t16383;
		p_bb->tx_lpi_entry_count += port_stats.eth.u2.bb2.tlpiec;
		p_bb->tx_total_collisions += port_stats.eth.u2.bb2.tncl;
	} else {
		struct qed_eth_stats_ah *p_ah = &p_stats->ah;

		p_ah->rx_1519_to_max_byte_packets +=
		    port_stats.eth.u0.ah0.r1519_to_max;
		p_ah->tx_1519_to_max_byte_packets =
		    port_stats.eth.u1.ah1.t1519_to_max;
	}

	p_common->link_change_count = qed_rd(p_hwfn, p_ptt,
					     p_hwfn->mcp_info->port_addr +
					     offsetof(struct public_port,
						      link_change_count));
}

static void __qed_get_vport_stats(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  struct qed_eth_stats *stats,
				  u16 statistics_bin, bool b_get_port_stats)
{
	__qed_get_vport_mstats(p_hwfn, p_ptt, stats, statistics_bin);
	__qed_get_vport_ustats(p_hwfn, p_ptt, stats, statistics_bin);
	__qed_get_vport_tstats(p_hwfn, p_ptt, stats, statistics_bin);
	__qed_get_vport_pstats(p_hwfn, p_ptt, stats, statistics_bin);

	if (b_get_port_stats && p_hwfn->mcp_info)
		__qed_get_vport_port_stats(p_hwfn, p_ptt, stats);
}

static void _qed_get_vport_stats(struct qed_dev *cdev,
				 struct qed_eth_stats *stats)
{
	u8 fw_vport = 0;
	int i;

	memset(stats, 0, sizeof(*stats));

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];
		struct qed_ptt *p_ptt = IS_PF(cdev) ? qed_ptt_acquire(p_hwfn)
						    :  NULL;

		if (IS_PF(cdev)) {
			/* The main vport index is relative first */
			if (qed_fw_vport(p_hwfn, 0, &fw_vport)) {
				DP_ERR(p_hwfn, "No vport available!\n");
				goto out;
			}
		}

		if (IS_PF(cdev) && !p_ptt) {
			DP_ERR(p_hwfn, "Failed to acquire ptt\n");
			continue;
		}

		__qed_get_vport_stats(p_hwfn, p_ptt, stats, fw_vport,
				      IS_PF(cdev) ? true : false);

out:
		if (IS_PF(cdev) && p_ptt)
			qed_ptt_release(p_hwfn, p_ptt);
	}
}

void qed_get_vport_stats(struct qed_dev *cdev, struct qed_eth_stats *stats)
{
	u32 i;

	if (!cdev) {
		memset(stats, 0, sizeof(*stats));
		return;
	}

	_qed_get_vport_stats(cdev, stats);

	if (!cdev->reset_stats)
		return;

	/* Reduce the statistics baseline */
	for (i = 0; i < sizeof(struct qed_eth_stats) / sizeof(u64); i++)
		((u64 *)stats)[i] -= ((u64 *)cdev->reset_stats)[i];
}

/* zeroes V-PORT specific portion of stats (Port stats remains untouched) */
void qed_reset_vport_stats(struct qed_dev *cdev)
{
	int i;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];
		struct eth_mstorm_per_queue_stat mstats;
		struct eth_ustorm_per_queue_stat ustats;
		struct eth_pstorm_per_queue_stat pstats;
		struct qed_ptt *p_ptt = IS_PF(cdev) ? qed_ptt_acquire(p_hwfn)
						    : NULL;
		u32 addr = 0, len = 0;

		if (IS_PF(cdev) && !p_ptt) {
			DP_ERR(p_hwfn, "Failed to acquire ptt\n");
			continue;
		}

		memset(&mstats, 0, sizeof(mstats));
		__qed_get_vport_mstats_addrlen(p_hwfn, &addr, &len, 0);
		qed_memcpy_to(p_hwfn, p_ptt, addr, &mstats, len);

		memset(&ustats, 0, sizeof(ustats));
		__qed_get_vport_ustats_addrlen(p_hwfn, &addr, &len, 0);
		qed_memcpy_to(p_hwfn, p_ptt, addr, &ustats, len);

		memset(&pstats, 0, sizeof(pstats));
		__qed_get_vport_pstats_addrlen(p_hwfn, &addr, &len, 0);
		qed_memcpy_to(p_hwfn, p_ptt, addr, &pstats, len);

		if (IS_PF(cdev))
			qed_ptt_release(p_hwfn, p_ptt);
	}

	/* PORT statistics are not necessarily reset, so we need to
	 * read and create a baseline for future statistics.
	 * Link change stat is maintained by MFW, return its value as is.
	 */
	if (!cdev->reset_stats) {
		DP_INFO(cdev, "Reset stats not allocated\n");
	} else {
		_qed_get_vport_stats(cdev, cdev->reset_stats);
		cdev->reset_stats->common.link_change_count = 0;
	}
}

static enum gft_profile_type
qed_arfs_mode_to_hsi(enum qed_filter_config_mode mode)
{
	if (mode == QED_FILTER_CONFIG_MODE_5_TUPLE)
		return GFT_PROFILE_TYPE_4_TUPLE;
	if (mode == QED_FILTER_CONFIG_MODE_IP_DEST)
		return GFT_PROFILE_TYPE_IP_DST_ADDR;
	if (mode == QED_FILTER_CONFIG_MODE_IP_SRC)
		return GFT_PROFILE_TYPE_IP_SRC_ADDR;
	return GFT_PROFILE_TYPE_L4_DST_PORT;
}

void qed_arfs_mode_configure(struct qed_hwfn *p_hwfn,
			     struct qed_ptt *p_ptt,
			     struct qed_arfs_config_params *p_cfg_params)
{
	if (p_cfg_params->mode != QED_FILTER_CONFIG_MODE_DISABLE) {
		qed_gft_config(p_hwfn, p_ptt, p_hwfn->rel_pf_id,
			       p_cfg_params->tcp,
			       p_cfg_params->udp,
			       p_cfg_params->ipv4,
			       p_cfg_params->ipv6,
			       qed_arfs_mode_to_hsi(p_cfg_params->mode));
		DP_VERBOSE(p_hwfn,
			   QED_MSG_SP,
			   "Configured Filtering: tcp = %s, udp = %s, ipv4 = %s, ipv6 =%s mode=%08x\n",
			   p_cfg_params->tcp ? "Enable" : "Disable",
			   p_cfg_params->udp ? "Enable" : "Disable",
			   p_cfg_params->ipv4 ? "Enable" : "Disable",
			   p_cfg_params->ipv6 ? "Enable" : "Disable",
			   (u32)p_cfg_params->mode);
	} else {
		DP_VERBOSE(p_hwfn, QED_MSG_SP, "Disabled Filtering\n");
		qed_gft_disable(p_hwfn, p_ptt, p_hwfn->rel_pf_id);
	}
}

int
qed_configure_rfs_ntuple_filter(struct qed_hwfn *p_hwfn,
				struct qed_spq_comp_cb *p_cb,
				struct qed_ntuple_filter_params *p_params)
{
	struct rx_update_gft_filter_data *p_ramrod = NULL;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	u16 abs_rx_q_id = 0;
	u8 abs_vport_id = 0;
	int rc = -EINVAL;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);

	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;

	if (p_cb) {
		init_data.comp_mode = QED_SPQ_MODE_CB;
		init_data.p_comp_data = p_cb;
	} else {
		init_data.comp_mode = QED_SPQ_MODE_EBLOCK;
	}

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 ETH_RAMROD_GFT_UPDATE_FILTER,
				 PROTOCOLID_ETH, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.rx_update_gft;

	DMA_REGPAIR_LE(p_ramrod->pkt_hdr_addr, p_params->addr);
	p_ramrod->pkt_hdr_length = cpu_to_le16(p_params->length);

	if (p_params->b_is_drop) {
		p_ramrod->vport_id = cpu_to_le16(ETH_GFT_TRASHCAN_VPORT);
	} else {
		rc = qed_fw_vport(p_hwfn, p_params->vport_id, &abs_vport_id);
		if (rc)
			goto err;

		if (p_params->qid != QED_RFS_NTUPLE_QID_RSS) {
			rc = qed_fw_l2_queue(p_hwfn, p_params->qid,
					     &abs_rx_q_id);
			if (rc)
				goto err;

			p_ramrod->rx_qid_valid = 1;
			p_ramrod->rx_qid = cpu_to_le16(abs_rx_q_id);
		}

		p_ramrod->vport_id = cpu_to_le16((u16)abs_vport_id);
	}

	p_ramrod->flow_id_valid = 0;
	p_ramrod->flow_id = 0;
	p_ramrod->filter_action = p_params->b_is_add ? GFT_ADD_FILTER
	    : GFT_DELETE_FILTER;

	DP_VERBOSE(p_hwfn, QED_MSG_SP,
		   "V[%0x], Q[%04x] - %s filter from 0x%llx [length %04xb]\n",
		   abs_vport_id, abs_rx_q_id,
		   p_params->b_is_add ? "Adding" : "Removing",
		   (u64)p_params->addr, p_params->length);

	return qed_spq_post(p_hwfn, p_ent, NULL);

err:
	qed_sp_destroy_request(p_hwfn, p_ent);
	return rc;
}

int qed_get_rxq_coalesce(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 struct qed_queue_cid *p_cid, u16 *p_rx_coal)
{
	u32 coalesce, address, is_valid;
	struct cau_sb_entry sb_entry;
	u8 timer_res;
	int rc;

	rc = qed_dmae_grc2host(p_hwfn, p_ptt, CAU_REG_SB_VAR_MEMORY +
			       p_cid->sb_igu_id * sizeof(u64),
			       (u64)(uintptr_t)&sb_entry, 2, 0);
	if (rc) {
		DP_ERR(p_hwfn, "dmae_grc2host failed %d\n", rc);
		return rc;
	}

	timer_res = GET_FIELD(sb_entry.params, CAU_SB_ENTRY_TIMER_RES0);

	address = BAR0_MAP_REG_USDM_RAM +
		  USTORM_ETH_QUEUE_ZONE_OFFSET(p_cid->abs.queue_id);
	coalesce = qed_rd(p_hwfn, p_ptt, address);

	is_valid = GET_FIELD(coalesce, COALESCING_TIMESET_VALID);
	if (!is_valid)
		return -EINVAL;

	coalesce = GET_FIELD(coalesce, COALESCING_TIMESET_TIMESET);
	*p_rx_coal = (u16)(coalesce << timer_res);

	return 0;
}

int qed_get_txq_coalesce(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 struct qed_queue_cid *p_cid, u16 *p_tx_coal)
{
	u32 coalesce, address, is_valid;
	struct cau_sb_entry sb_entry;
	u8 timer_res;
	int rc;

	rc = qed_dmae_grc2host(p_hwfn, p_ptt, CAU_REG_SB_VAR_MEMORY +
			       p_cid->sb_igu_id * sizeof(u64),
			       (u64)(uintptr_t)&sb_entry, 2, 0);
	if (rc) {
		DP_ERR(p_hwfn, "dmae_grc2host failed %d\n", rc);
		return rc;
	}

	timer_res = GET_FIELD(sb_entry.params, CAU_SB_ENTRY_TIMER_RES1);

	address = BAR0_MAP_REG_XSDM_RAM +
		  XSTORM_ETH_QUEUE_ZONE_OFFSET(p_cid->abs.queue_id);
	coalesce = qed_rd(p_hwfn, p_ptt, address);

	is_valid = GET_FIELD(coalesce, COALESCING_TIMESET_VALID);
	if (!is_valid)
		return -EINVAL;

	coalesce = GET_FIELD(coalesce, COALESCING_TIMESET_TIMESET);
	*p_tx_coal = (u16)(coalesce << timer_res);

	return 0;
}

int qed_get_queue_coalesce(struct qed_hwfn *p_hwfn, u16 *p_coal, void *handle)
{
	struct qed_queue_cid *p_cid = handle;
	struct qed_ptt *p_ptt;
	int rc = 0;

	if (IS_VF(p_hwfn->cdev)) {
		rc = qed_vf_pf_get_coalesce(p_hwfn, p_coal, p_cid);
		if (rc)
			DP_NOTICE(p_hwfn, "Unable to read queue coalescing\n");

		return rc;
	}

	p_ptt = qed_ptt_acquire(p_hwfn);
	if (!p_ptt)
		return -EAGAIN;

	if (p_cid->b_is_rx) {
		rc = qed_get_rxq_coalesce(p_hwfn, p_ptt, p_cid, p_coal);
		if (rc)
			goto out;
	} else {
		rc = qed_get_txq_coalesce(p_hwfn, p_ptt, p_cid, p_coal);
		if (rc)
			goto out;
	}

out:
	qed_ptt_release(p_hwfn, p_ptt);

	return rc;
}

static int qed_fill_eth_dev_info(struct qed_dev *cdev,
				 struct qed_dev_eth_info *info)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	int i;

	memset(info, 0, sizeof(*info));

	if (IS_PF(cdev)) {
		int max_vf_vlan_filters = 0;
		int max_vf_mac_filters = 0;

		info->num_tc = p_hwfn->hw_info.num_hw_tc;

		if (cdev->int_params.out.int_mode == QED_INT_MODE_MSIX) {
			u16 num_queues = 0;

			/* Since the feature controls only queue-zones,
			 * make sure we have the contexts [rx, tx, xdp] to
			 * match.
			 */
			for_each_hwfn(cdev, i) {
				struct qed_hwfn *hwfn = &cdev->hwfns[i];
				u16 l2_queues = (u16)FEAT_NUM(hwfn,
							      QED_PF_L2_QUE);
				u16 cids;

				cids = hwfn->pf_params.eth_pf_params.num_cons;
				num_queues += min_t(u16, l2_queues, cids / 3);
			}

			/* queues might theoretically be >256, but interrupts'
			 * upper-limit guarantes that it would fit in a u8.
			 */
			if (cdev->int_params.fp_msix_cnt) {
				u8 irqs = cdev->int_params.fp_msix_cnt;

				info->num_queues = (u8)min_t(u16,
							     num_queues, irqs);
			}
		} else {
			info->num_queues = cdev->num_hwfns;
		}

		if (IS_QED_SRIOV(cdev)) {
			max_vf_vlan_filters = cdev->p_iov_info->total_vfs *
					      QED_ETH_VF_NUM_VLAN_FILTERS;
			max_vf_mac_filters = cdev->p_iov_info->total_vfs *
					     QED_ETH_VF_NUM_MAC_FILTERS;
		}
		info->num_vlan_filters = RESC_NUM(QED_LEADING_HWFN(cdev),
						  QED_VLAN) -
					 max_vf_vlan_filters;
		info->num_mac_filters = RESC_NUM(QED_LEADING_HWFN(cdev),
						 QED_MAC) -
					max_vf_mac_filters;

		ether_addr_copy(info->port_mac,
				cdev->hwfns[0].hw_info.hw_mac_addr);

		info->xdp_supported = true;
	} else {
		u16 total_cids = 0;

		info->num_tc = 1;

		/* Determine queues &  XDP support */
		for_each_hwfn(cdev, i) {
			struct qed_hwfn *p_hwfn = &cdev->hwfns[i];
			u8 queues, cids;

			qed_vf_get_num_cids(p_hwfn, &cids);
			qed_vf_get_num_rxqs(p_hwfn, &queues);
			info->num_queues += queues;
			total_cids += cids;
		}

		/* Enable VF XDP in case PF guarntees sufficient connections */
		if (total_cids >= info->num_queues * 3)
			info->xdp_supported = true;

		qed_vf_get_num_vlan_filters(&cdev->hwfns[0],
					    (u8 *)&info->num_vlan_filters);
		qed_vf_get_num_mac_filters(&cdev->hwfns[0],
					   (u8 *)&info->num_mac_filters);
		qed_vf_get_port_mac(&cdev->hwfns[0], info->port_mac);

		info->is_legacy = !!cdev->hwfns[0].vf_iov_info->b_pre_fp_hsi;
	}

	qed_fill_dev_info(cdev, &info->common);

	if (IS_VF(cdev))
		eth_zero_addr(info->common.hw_mac);

	return 0;
}

static void qed_register_eth_ops(struct qed_dev *cdev,
				 struct qed_eth_cb_ops *ops, void *cookie)
{
	cdev->protocol_ops.eth = ops;
	cdev->ops_cookie = cookie;

	/* For VF, we start bulletin reading */
	if (IS_VF(cdev))
		qed_vf_start_iov_wq(cdev);
}

static bool qed_check_mac(struct qed_dev *cdev, u8 *mac)
{
	if (IS_PF(cdev))
		return true;

	return qed_vf_check_mac(&cdev->hwfns[0], mac);
}

static int qed_start_vport(struct qed_dev *cdev,
			   struct qed_start_vport_params *params)
{
	int rc, i;

	for_each_hwfn(cdev, i) {
		struct qed_sp_vport_start_params start = { 0 };
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		start.tpa_mode = params->gro_enable ? QED_TPA_MODE_GRO :
							QED_TPA_MODE_NONE;
		start.remove_inner_vlan = params->remove_inner_vlan;
		start.only_untagged = true;	/* untagged only */
		start.drop_ttl0 = params->drop_ttl0;
		start.opaque_fid = p_hwfn->hw_info.opaque_fid;
		start.concrete_fid = p_hwfn->hw_info.concrete_fid;
		start.handle_ptp_pkts = params->handle_ptp_pkts;
		start.vport_id = params->vport_id;
		start.max_buffers_per_cqe = 16;
		start.mtu = params->mtu;

		rc = qed_sp_vport_start(p_hwfn, &start);
		if (rc) {
			DP_ERR(cdev, "Failed to start VPORT\n");
			return rc;
		}

		rc = qed_hw_start_fastpath(p_hwfn);
		if (rc) {
			DP_ERR(cdev, "Failed to start VPORT fastpath\n");
			return rc;
		}

		DP_VERBOSE(cdev, (QED_MSG_SPQ | NETIF_MSG_IFUP),
			   "Started V-PORT %d with MTU %d\n",
			   start.vport_id, start.mtu);
	}

	if (params->clear_stats)
		qed_reset_vport_stats(cdev);

	return 0;
}

static int qed_stop_vport(struct qed_dev *cdev, u8 vport_id)
{
	int rc, i;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		rc = qed_sp_vport_stop(p_hwfn,
				       p_hwfn->hw_info.opaque_fid, vport_id);

		if (rc) {
			DP_ERR(cdev, "Failed to stop VPORT\n");
			return rc;
		}
	}
	return 0;
}

static int qed_update_vport_rss(struct qed_dev *cdev,
				struct qed_update_vport_rss_params *input,
				struct qed_rss_params *rss)
{
	int i, fn;

	/* Update configuration with what's correct regardless of CMT */
	rss->update_rss_config = 1;
	rss->rss_enable = 1;
	rss->update_rss_capabilities = 1;
	rss->update_rss_ind_table = 1;
	rss->update_rss_key = 1;
	rss->rss_caps = input->rss_caps;
	memcpy(rss->rss_key, input->rss_key, QED_RSS_KEY_SIZE * sizeof(u32));

	/* In regular scenario, we'd simply need to take input handlers.
	 * But in CMT, we'd have to split the handlers according to the
	 * engine they were configured on. We'd then have to understand
	 * whether RSS is really required, since 2-queues on CMT doesn't
	 * require RSS.
	 */
	if (cdev->num_hwfns == 1) {
		memcpy(rss->rss_ind_table,
		       input->rss_ind_table,
		       QED_RSS_IND_TABLE_SIZE * sizeof(void *));
		rss->rss_table_size_log = 7;
		return 0;
	}

	/* Start by copying the non-spcific information to the 2nd copy */
	memcpy(&rss[1], &rss[0], sizeof(struct qed_rss_params));

	/* CMT should be round-robin */
	for (i = 0; i < QED_RSS_IND_TABLE_SIZE; i++) {
		struct qed_queue_cid *cid = input->rss_ind_table[i];
		struct qed_rss_params *t_rss;

		if (cid->p_owner == QED_LEADING_HWFN(cdev))
			t_rss = &rss[0];
		else
			t_rss = &rss[1];

		t_rss->rss_ind_table[i / cdev->num_hwfns] = cid;
	}

	/* Make sure RSS is actually required */
	for_each_hwfn(cdev, fn) {
		for (i = 1; i < QED_RSS_IND_TABLE_SIZE / cdev->num_hwfns; i++) {
			if (rss[fn].rss_ind_table[i] !=
			    rss[fn].rss_ind_table[0])
				break;
		}
		if (i == QED_RSS_IND_TABLE_SIZE / cdev->num_hwfns) {
			DP_VERBOSE(cdev, NETIF_MSG_IFUP,
				   "CMT - 1 queue per-hwfn; Disabling RSS\n");
			return -EINVAL;
		}
		rss[fn].rss_table_size_log = 6;
	}

	return 0;
}

static int qed_update_vport(struct qed_dev *cdev,
			    struct qed_update_vport_params *params)
{
	struct qed_sp_vport_update_params sp_params;
	struct qed_rss_params *rss;
	int rc = 0, i;

	if (!cdev)
		return -ENODEV;

	rss = vzalloc(array_size(sizeof(*rss), cdev->num_hwfns));
	if (!rss)
		return -ENOMEM;

	memset(&sp_params, 0, sizeof(sp_params));

	/* Translate protocol params into sp params */
	sp_params.vport_id = params->vport_id;
	sp_params.update_vport_active_rx_flg = params->update_vport_active_flg;
	sp_params.update_vport_active_tx_flg = params->update_vport_active_flg;
	sp_params.vport_active_rx_flg = params->vport_active_flg;
	sp_params.vport_active_tx_flg = params->vport_active_flg;
	sp_params.update_tx_switching_flg = params->update_tx_switching_flg;
	sp_params.tx_switching_flg = params->tx_switching_flg;
	sp_params.accept_any_vlan = params->accept_any_vlan;
	sp_params.update_accept_any_vlan_flg =
		params->update_accept_any_vlan_flg;

	/* Prepare the RSS configuration */
	if (params->update_rss_flg)
		if (qed_update_vport_rss(cdev, &params->rss_params, rss))
			params->update_rss_flg = 0;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		if (params->update_rss_flg)
			sp_params.rss_params = &rss[i];

		sp_params.opaque_fid = p_hwfn->hw_info.opaque_fid;
		rc = qed_sp_vport_update(p_hwfn, &sp_params,
					 QED_SPQ_MODE_EBLOCK,
					 NULL);
		if (rc) {
			DP_ERR(cdev, "Failed to update VPORT\n");
			goto out;
		}

		DP_VERBOSE(cdev, (QED_MSG_SPQ | NETIF_MSG_IFUP),
			   "Updated V-PORT %d: active_flag %d [update %d]\n",
			   params->vport_id, params->vport_active_flg,
			   params->update_vport_active_flg);
	}

out:
	vfree(rss);
	return rc;
}

static int qed_start_rxq(struct qed_dev *cdev,
			 u8 rss_num,
			 struct qed_queue_start_common_params *p_params,
			 u16 bd_max_bytes,
			 dma_addr_t bd_chain_phys_addr,
			 dma_addr_t cqe_pbl_addr,
			 u16 cqe_pbl_size,
			 struct qed_rxq_start_ret_params *ret_params)
{
	struct qed_hwfn *p_hwfn;
	int rc, hwfn_index;

	hwfn_index = rss_num % cdev->num_hwfns;
	p_hwfn = &cdev->hwfns[hwfn_index];

	p_params->queue_id = p_params->queue_id / cdev->num_hwfns;
	p_params->stats_id = p_params->vport_id;

	rc = qed_eth_rx_queue_start(p_hwfn,
				    p_hwfn->hw_info.opaque_fid,
				    p_params,
				    bd_max_bytes,
				    bd_chain_phys_addr,
				    cqe_pbl_addr, cqe_pbl_size, ret_params);
	if (rc) {
		DP_ERR(cdev, "Failed to start RXQ#%d\n", p_params->queue_id);
		return rc;
	}

	DP_VERBOSE(cdev, (QED_MSG_SPQ | NETIF_MSG_IFUP),
		   "Started RX-Q %d [rss_num %d] on V-PORT %d and SB igu %d\n",
		   p_params->queue_id, rss_num, p_params->vport_id,
		   p_params->p_sb->igu_sb_id);

	return 0;
}

static int qed_stop_rxq(struct qed_dev *cdev, u8 rss_id, void *handle)
{
	int rc, hwfn_index;
	struct qed_hwfn *p_hwfn;

	hwfn_index = rss_id % cdev->num_hwfns;
	p_hwfn = &cdev->hwfns[hwfn_index];

	rc = qed_eth_rx_queue_stop(p_hwfn, handle, false, false);
	if (rc) {
		DP_ERR(cdev, "Failed to stop RXQ#%02x\n", rss_id);
		return rc;
	}

	return 0;
}

static int qed_start_txq(struct qed_dev *cdev,
			 u8 rss_num,
			 struct qed_queue_start_common_params *p_params,
			 dma_addr_t pbl_addr,
			 u16 pbl_size,
			 struct qed_txq_start_ret_params *ret_params)
{
	struct qed_hwfn *p_hwfn;
	int rc, hwfn_index;

	hwfn_index = rss_num % cdev->num_hwfns;
	p_hwfn = &cdev->hwfns[hwfn_index];
	p_params->queue_id = p_params->queue_id / cdev->num_hwfns;
	p_params->stats_id = p_params->vport_id;

	rc = qed_eth_tx_queue_start(p_hwfn,
				    p_hwfn->hw_info.opaque_fid,
				    p_params, p_params->tc,
				    pbl_addr, pbl_size, ret_params);

	if (rc) {
		DP_ERR(cdev, "Failed to start TXQ#%d\n", p_params->queue_id);
		return rc;
	}

	DP_VERBOSE(cdev, (QED_MSG_SPQ | NETIF_MSG_IFUP),
		   "Started TX-Q %d [rss_num %d] on V-PORT %d and SB igu %d\n",
		   p_params->queue_id, rss_num, p_params->vport_id,
		   p_params->p_sb->igu_sb_id);

	return 0;
}

#define QED_HW_STOP_RETRY_LIMIT (10)
static int qed_fastpath_stop(struct qed_dev *cdev)
{
	int rc;

	rc = qed_hw_stop_fastpath(cdev);
	if (rc) {
		DP_ERR(cdev, "Failed to stop Fastpath\n");
		return rc;
	}

	return 0;
}

static int qed_stop_txq(struct qed_dev *cdev, u8 rss_id, void *handle)
{
	struct qed_hwfn *p_hwfn;
	int rc, hwfn_index;

	hwfn_index = rss_id % cdev->num_hwfns;
	p_hwfn = &cdev->hwfns[hwfn_index];

	rc = qed_eth_tx_queue_stop(p_hwfn, handle);
	if (rc) {
		DP_ERR(cdev, "Failed to stop TXQ#%02x\n", rss_id);
		return rc;
	}

	return 0;
}

static int qed_tunn_configure(struct qed_dev *cdev,
			      struct qed_tunn_params *tunn_params)
{
	struct qed_tunnel_info tunn_info;
	int i, rc;

	memset(&tunn_info, 0, sizeof(tunn_info));
	if (tunn_params->update_vxlan_port) {
		tunn_info.vxlan_port.b_update_port = true;
		tunn_info.vxlan_port.port = tunn_params->vxlan_port;
	}

	if (tunn_params->update_geneve_port) {
		tunn_info.geneve_port.b_update_port = true;
		tunn_info.geneve_port.port = tunn_params->geneve_port;
	}

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *hwfn = &cdev->hwfns[i];
		struct qed_ptt *p_ptt;
		struct qed_tunnel_info *tun;

		tun = &hwfn->cdev->tunnel;
		if (IS_PF(cdev)) {
			p_ptt = qed_ptt_acquire(hwfn);
			if (!p_ptt)
				return -EAGAIN;
		} else {
			p_ptt = NULL;
		}

		rc = qed_sp_pf_update_tunn_cfg(hwfn, p_ptt, &tunn_info,
					       QED_SPQ_MODE_EBLOCK, NULL);
		if (rc) {
			if (IS_PF(cdev))
				qed_ptt_release(hwfn, p_ptt);
			return rc;
		}

		if (IS_PF_SRIOV(hwfn)) {
			u16 vxlan_port, geneve_port;
			int j;

			vxlan_port = tun->vxlan_port.port;
			geneve_port = tun->geneve_port.port;

			qed_for_each_vf(hwfn, j) {
				qed_iov_bulletin_set_udp_ports(hwfn, j,
							       vxlan_port,
							       geneve_port);
			}

			qed_schedule_iov(hwfn, QED_IOV_WQ_BULLETIN_UPDATE_FLAG);
		}
		if (IS_PF(cdev))
			qed_ptt_release(hwfn, p_ptt);
	}

	return 0;
}

static int qed_configure_filter_rx_mode(struct qed_dev *cdev,
					enum qed_filter_rx_mode_type type)
{
	struct qed_filter_accept_flags accept_flags;

	memset(&accept_flags, 0, sizeof(accept_flags));

	accept_flags.update_rx_mode_config = 1;
	accept_flags.update_tx_mode_config = 1;
	accept_flags.rx_accept_filter = QED_ACCEPT_UCAST_MATCHED |
					QED_ACCEPT_MCAST_MATCHED |
					QED_ACCEPT_BCAST;
	accept_flags.tx_accept_filter = QED_ACCEPT_UCAST_MATCHED |
					QED_ACCEPT_MCAST_MATCHED |
					QED_ACCEPT_BCAST;

	if (type == QED_FILTER_RX_MODE_TYPE_PROMISC) {
		accept_flags.rx_accept_filter |= QED_ACCEPT_UCAST_UNMATCHED |
						 QED_ACCEPT_MCAST_UNMATCHED;
		accept_flags.tx_accept_filter |= QED_ACCEPT_UCAST_UNMATCHED |
						 QED_ACCEPT_MCAST_UNMATCHED;
	} else if (type == QED_FILTER_RX_MODE_TYPE_MULTI_PROMISC) {
		accept_flags.rx_accept_filter |= QED_ACCEPT_MCAST_UNMATCHED;
		accept_flags.tx_accept_filter |= QED_ACCEPT_MCAST_UNMATCHED;
	}

	return qed_filter_accept_cmd(cdev, 0, accept_flags, false, false,
				     QED_SPQ_MODE_CB, NULL);
}

static int qed_configure_filter_ucast(struct qed_dev *cdev,
				      struct qed_filter_ucast_params *params)
{
	struct qed_filter_ucast ucast;

	if (!params->vlan_valid && !params->mac_valid) {
		DP_NOTICE(cdev,
			  "Tried configuring a unicast filter, but both MAC and VLAN are not set\n");
		return -EINVAL;
	}

	memset(&ucast, 0, sizeof(ucast));
	switch (params->type) {
	case QED_FILTER_XCAST_TYPE_ADD:
		ucast.opcode = QED_FILTER_ADD;
		break;
	case QED_FILTER_XCAST_TYPE_DEL:
		ucast.opcode = QED_FILTER_REMOVE;
		break;
	case QED_FILTER_XCAST_TYPE_REPLACE:
		ucast.opcode = QED_FILTER_REPLACE;
		break;
	default:
		DP_NOTICE(cdev, "Unknown unicast filter type %d\n",
			  params->type);
	}

	if (params->vlan_valid && params->mac_valid) {
		ucast.type = QED_FILTER_MAC_VLAN;
		ether_addr_copy(ucast.mac, params->mac);
		ucast.vlan = params->vlan;
	} else if (params->mac_valid) {
		ucast.type = QED_FILTER_MAC;
		ether_addr_copy(ucast.mac, params->mac);
	} else {
		ucast.type = QED_FILTER_VLAN;
		ucast.vlan = params->vlan;
	}

	ucast.is_rx_filter = true;
	ucast.is_tx_filter = true;

	return qed_filter_ucast_cmd(cdev, &ucast, QED_SPQ_MODE_CB, NULL);
}

static int qed_configure_filter_mcast(struct qed_dev *cdev,
				      struct qed_filter_mcast_params *params)
{
	struct qed_filter_mcast mcast;
	int i;

	memset(&mcast, 0, sizeof(mcast));
	switch (params->type) {
	case QED_FILTER_XCAST_TYPE_ADD:
		mcast.opcode = QED_FILTER_ADD;
		break;
	case QED_FILTER_XCAST_TYPE_DEL:
		mcast.opcode = QED_FILTER_REMOVE;
		break;
	default:
		DP_NOTICE(cdev, "Unknown multicast filter type %d\n",
			  params->type);
	}

	mcast.num_mc_addrs = params->num;
	for (i = 0; i < mcast.num_mc_addrs; i++)
		ether_addr_copy(mcast.mac[i], params->mac[i]);

	return qed_filter_mcast_cmd(cdev, &mcast, QED_SPQ_MODE_CB, NULL);
}

static int qed_configure_filter(struct qed_dev *cdev,
				struct qed_filter_params *params)
{
	enum qed_filter_rx_mode_type accept_flags;

	switch (params->type) {
	case QED_FILTER_TYPE_UCAST:
		return qed_configure_filter_ucast(cdev, &params->filter.ucast);
	case QED_FILTER_TYPE_MCAST:
		return qed_configure_filter_mcast(cdev, &params->filter.mcast);
	case QED_FILTER_TYPE_RX_MODE:
		accept_flags = params->filter.accept_flags;
		return qed_configure_filter_rx_mode(cdev, accept_flags);
	default:
		DP_NOTICE(cdev, "Unknown filter type %d\n", (int)params->type);
		return -EINVAL;
	}
}

static int qed_configure_arfs_searcher(struct qed_dev *cdev,
				       enum qed_filter_config_mode mode)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	struct qed_arfs_config_params arfs_config_params;

	memset(&arfs_config_params, 0, sizeof(arfs_config_params));
	arfs_config_params.tcp = true;
	arfs_config_params.udp = true;
	arfs_config_params.ipv4 = true;
	arfs_config_params.ipv6 = true;
	arfs_config_params.mode = mode;
	qed_arfs_mode_configure(p_hwfn, p_hwfn->p_arfs_ptt,
				&arfs_config_params);
	return 0;
}

static void
qed_arfs_sp_response_handler(struct qed_hwfn *p_hwfn,
			     void *cookie,
			     union event_ring_data *data, u8 fw_return_code)
{
	struct qed_common_cb_ops *op = p_hwfn->cdev->protocol_ops.common;
	void *dev = p_hwfn->cdev->ops_cookie;

	op->arfs_filter_op(dev, cookie, fw_return_code);
}

static int
qed_ntuple_arfs_filter_config(struct qed_dev *cdev,
			      void *cookie,
			      struct qed_ntuple_filter_params *params)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	struct qed_spq_comp_cb cb;
	int rc = -EINVAL;

	cb.function = qed_arfs_sp_response_handler;
	cb.cookie = cookie;

	if (params->b_is_vf) {
		if (!qed_iov_is_valid_vfid(p_hwfn, params->vf_id, false,
					   false)) {
			DP_INFO(p_hwfn, "vfid 0x%02x is out of bounds\n",
				params->vf_id);
			return rc;
		}

		params->vport_id = params->vf_id + 1;
		params->qid = QED_RFS_NTUPLE_QID_RSS;
	}

	rc = qed_configure_rfs_ntuple_filter(p_hwfn, &cb, params);
	if (rc)
		DP_NOTICE(p_hwfn,
			  "Failed to issue a-RFS filter configuration\n");
	else
		DP_VERBOSE(p_hwfn, NETIF_MSG_DRV,
			   "Successfully issued a-RFS filter configuration\n");

	return rc;
}

static int qed_get_coalesce(struct qed_dev *cdev, u16 *coal, void *handle)
{
	struct qed_queue_cid *p_cid = handle;
	struct qed_hwfn *p_hwfn;
	int rc;

	p_hwfn = p_cid->p_owner;
	rc = qed_get_queue_coalesce(p_hwfn, coal, handle);
	if (rc)
		DP_NOTICE(p_hwfn, "Unable to read queue coalescing\n");

	return rc;
}

static int qed_fp_cqe_completion(struct qed_dev *dev,
				 u8 rss_id, struct eth_slow_path_rx_cqe *cqe)
{
	return qed_eth_cqe_completion(&dev->hwfns[rss_id % dev->num_hwfns],
				      cqe);
}

static int qed_req_bulletin_update_mac(struct qed_dev *cdev, u8 *mac)
{
	int i, ret;

	if (IS_PF(cdev))
		return 0;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		ret = qed_vf_pf_bulletin_update_mac(p_hwfn, mac);
		if (ret)
			return ret;
	}

	return 0;
}

#ifdef CONFIG_QED_SRIOV
extern const struct qed_iov_hv_ops qed_iov_ops_pass;
#endif

#ifdef CONFIG_DCB
extern const struct qed_eth_dcbnl_ops qed_dcbnl_ops_pass;
#endif

extern const struct qed_eth_ptp_ops qed_ptp_ops_pass;

static const struct qed_eth_ops qed_eth_ops_pass = {
	.common = &qed_common_ops_pass,
#ifdef CONFIG_QED_SRIOV
	.iov = &qed_iov_ops_pass,
#endif
#ifdef CONFIG_DCB
	.dcb = &qed_dcbnl_ops_pass,
#endif
	.ptp = &qed_ptp_ops_pass,
	.fill_dev_info = &qed_fill_eth_dev_info,
	.register_ops = &qed_register_eth_ops,
	.check_mac = &qed_check_mac,
	.vport_start = &qed_start_vport,
	.vport_stop = &qed_stop_vport,
	.vport_update = &qed_update_vport,
	.q_rx_start = &qed_start_rxq,
	.q_rx_stop = &qed_stop_rxq,
	.q_tx_start = &qed_start_txq,
	.q_tx_stop = &qed_stop_txq,
	.filter_config = &qed_configure_filter,
	.fastpath_stop = &qed_fastpath_stop,
	.eth_cqe_completion = &qed_fp_cqe_completion,
	.get_vport_stats = &qed_get_vport_stats,
	.tunn_config = &qed_tunn_configure,
	.ntuple_filter_config = &qed_ntuple_arfs_filter_config,
	.configure_arfs_searcher = &qed_configure_arfs_searcher,
	.get_coalesce = &qed_get_coalesce,
	.req_bulletin_update_mac = &qed_req_bulletin_update_mac,
};

const struct qed_eth_ops *qed_get_eth_ops(void)
{
	return &qed_eth_ops_pass;
}
EXPORT_SYMBOL(qed_get_eth_ops);

void qed_put_eth_ops(void)
{
	/* TODO - reference count for module? */
}
EXPORT_SYMBOL(qed_put_eth_ops);
