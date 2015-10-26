/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
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
#include <linux/version.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include "qed.h"
#include <linux/qed/qed_chain.h>
#include "qed_cxt.h"
#include "qed_dev_api.h"
#include <linux/qed/qed_eth_if.h>
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_int.h"
#include "qed_reg_addr.h"
#include "qed_sp.h"

enum qed_rss_caps {
	QED_RSS_IPV4		= 0x1,
	QED_RSS_IPV6		= 0x2,
	QED_RSS_IPV4_TCP	= 0x4,
	QED_RSS_IPV6_TCP	= 0x8,
	QED_RSS_IPV4_UDP	= 0x10,
	QED_RSS_IPV6_UDP	= 0x20,
};

/* Should be the same as ETH_RSS_IND_TABLE_ENTRIES_NUM */
#define QED_RSS_IND_TABLE_SIZE 128
#define QED_RSS_KEY_SIZE 10 /* size in 32b chunks */

struct qed_rss_params {
	u8	update_rss_config;
	u8	rss_enable;
	u8	rss_eng_id;
	u8	update_rss_capabilities;
	u8	update_rss_ind_table;
	u8	update_rss_key;
	u8	rss_caps;
	u8	rss_table_size_log;
	u16	rss_ind_table[QED_RSS_IND_TABLE_SIZE];
	u32	rss_key[QED_RSS_KEY_SIZE];
};

enum qed_filter_opcode {
	QED_FILTER_ADD,
	QED_FILTER_REMOVE,
	QED_FILTER_MOVE,
	QED_FILTER_REPLACE,     /* Delete all MACs and add new one instead */
	QED_FILTER_FLUSH,       /* Removes all filters */
};

enum qed_filter_ucast_type {
	QED_FILTER_MAC,
	QED_FILTER_VLAN,
	QED_FILTER_MAC_VLAN,
	QED_FILTER_INNER_MAC,
	QED_FILTER_INNER_VLAN,
	QED_FILTER_INNER_PAIR,
	QED_FILTER_INNER_MAC_VNI_PAIR,
	QED_FILTER_MAC_VNI_PAIR,
	QED_FILTER_VNI,
};

struct qed_filter_ucast {
	enum qed_filter_opcode		opcode;
	enum qed_filter_ucast_type	type;
	u8				is_rx_filter;
	u8				is_tx_filter;
	u8				vport_to_add_to;
	u8				vport_to_remove_from;
	unsigned char			mac[ETH_ALEN];
	u8				assert_on_error;
	u16				vlan;
	u32				vni;
};

struct qed_filter_mcast {
	/* MOVE is not supported for multicast */
	enum qed_filter_opcode	opcode;
	u8			vport_to_add_to;
	u8			vport_to_remove_from;
	u8			num_mc_addrs;
#define QED_MAX_MC_ADDRS        64
	unsigned char		mac[QED_MAX_MC_ADDRS][ETH_ALEN];
};

struct qed_filter_accept_flags {
	u8	update_rx_mode_config;
	u8	update_tx_mode_config;
	u8	rx_accept_filter;
	u8	tx_accept_filter;
#define QED_ACCEPT_NONE         0x01
#define QED_ACCEPT_UCAST_MATCHED        0x02
#define QED_ACCEPT_UCAST_UNMATCHED      0x04
#define QED_ACCEPT_MCAST_MATCHED        0x08
#define QED_ACCEPT_MCAST_UNMATCHED      0x10
#define QED_ACCEPT_BCAST                0x20
};

struct qed_sp_vport_update_params {
	u16				opaque_fid;
	u8				vport_id;
	u8				update_vport_active_rx_flg;
	u8				vport_active_rx_flg;
	u8				update_vport_active_tx_flg;
	u8				vport_active_tx_flg;
	u8				update_approx_mcast_flg;
	unsigned long			bins[8];
	struct qed_rss_params		*rss_params;
	struct qed_filter_accept_flags	accept_flags;
};

#define QED_MAX_SGES_NUM 16
#define CRC32_POLY 0x1edc6f41

static int qed_sp_vport_start(struct qed_hwfn *p_hwfn,
			      u32 concrete_fid,
			      u16 opaque_fid,
			      u8 vport_id,
			      u16 mtu,
			      u8 drop_ttl0_flg,
			      u8 inner_vlan_removal_en_flg)
{
	struct qed_sp_init_request_params params;
	struct vport_start_ramrod_data *p_ramrod = NULL;
	struct qed_spq_entry *p_ent =  NULL;
	int rc = -EINVAL;
	u16 rx_mode = 0;
	u8 abs_vport_id = 0;

	rc = qed_fw_vport(p_hwfn, vport_id, &abs_vport_id);
	if (rc != 0)
		return rc;

	memset(&params, 0, sizeof(params));
	params.ramrod_data_size = sizeof(*p_ramrod);
	params.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 qed_spq_get_cid(p_hwfn),
				 opaque_fid,
				 ETH_RAMROD_VPORT_START,
				 PROTOCOLID_ETH,
				 &params);
	if (rc)
		return rc;

	p_ramrod		= &p_ent->ramrod.vport_start;
	p_ramrod->vport_id	= abs_vport_id;

	p_ramrod->mtu			= cpu_to_le16(mtu);
	p_ramrod->inner_vlan_removal_en = inner_vlan_removal_en_flg;
	p_ramrod->drop_ttl0_en		= drop_ttl0_flg;

	SET_FIELD(rx_mode, ETH_VPORT_RX_MODE_UCAST_DROP_ALL, 1);
	SET_FIELD(rx_mode, ETH_VPORT_RX_MODE_MCAST_DROP_ALL, 1);

	p_ramrod->rx_mode.state = cpu_to_le16(rx_mode);

	/* TPA related fields */
	memset(&p_ramrod->tpa_param, 0,
	       sizeof(struct eth_vport_tpa_param));

	/* Software Function ID in hwfn (PFs are 0 - 15, VFs are 16 - 135) */
	p_ramrod->sw_fid = qed_concrete_to_sw_fid(p_hwfn->cdev,
						  concrete_fid);

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int
qed_sp_vport_update_rss(struct qed_hwfn *p_hwfn,
			struct vport_update_ramrod_data *p_ramrod,
			struct qed_rss_params *p_params)
{
	struct eth_vport_rss_config *rss = &p_ramrod->rss_config;
	u16 abs_l2_queue = 0, capabilities = 0;
	int rc = 0, i;

	if (!p_params) {
		p_ramrod->common.update_rss_flg = 0;
		return rc;
	}

	BUILD_BUG_ON(QED_RSS_IND_TABLE_SIZE !=
		     ETH_RSS_IND_TABLE_ENTRIES_NUM);

	rc = qed_fw_rss_eng(p_hwfn, p_params->rss_eng_id, &rss->rss_id);
	if (rc)
		return rc;

	p_ramrod->common.update_rss_flg = p_params->update_rss_config;
	rss->update_rss_capabilities = p_params->update_rss_capabilities;
	rss->update_rss_ind_table = p_params->update_rss_ind_table;
	rss->update_rss_key = p_params->update_rss_key;

	rss->rss_mode = p_params->rss_enable ?
			ETH_VPORT_RSS_MODE_REGULAR :
			ETH_VPORT_RSS_MODE_DISABLED;

	SET_FIELD(capabilities,
		  ETH_VPORT_RSS_CONFIG_IPV4_CAPABILITY,
		  !!(p_params->rss_caps & QED_RSS_IPV4));
	SET_FIELD(capabilities,
		  ETH_VPORT_RSS_CONFIG_IPV6_CAPABILITY,
		  !!(p_params->rss_caps & QED_RSS_IPV6));
	SET_FIELD(capabilities,
		  ETH_VPORT_RSS_CONFIG_IPV4_TCP_CAPABILITY,
		  !!(p_params->rss_caps & QED_RSS_IPV4_TCP));
	SET_FIELD(capabilities,
		  ETH_VPORT_RSS_CONFIG_IPV6_TCP_CAPABILITY,
		  !!(p_params->rss_caps & QED_RSS_IPV6_TCP));
	SET_FIELD(capabilities,
		  ETH_VPORT_RSS_CONFIG_IPV4_UDP_CAPABILITY,
		  !!(p_params->rss_caps & QED_RSS_IPV4_UDP));
	SET_FIELD(capabilities,
		  ETH_VPORT_RSS_CONFIG_IPV6_UDP_CAPABILITY,
		  !!(p_params->rss_caps & QED_RSS_IPV6_UDP));
	rss->tbl_size = p_params->rss_table_size_log;

	rss->capabilities = cpu_to_le16(capabilities);

	DP_VERBOSE(p_hwfn, NETIF_MSG_IFUP,
		   "update rss flag %d, rss_mode = %d, update_caps = %d, capabilities = %d, update_ind = %d, update_rss_key = %d\n",
		   p_ramrod->common.update_rss_flg,
		   rss->rss_mode, rss->update_rss_capabilities,
		   capabilities, rss->update_rss_ind_table,
		   rss->update_rss_key);

	for (i = 0; i < QED_RSS_IND_TABLE_SIZE; i++) {
		rc = qed_fw_l2_queue(p_hwfn,
				     (u8)p_params->rss_ind_table[i],
				     &abs_l2_queue);
		if (rc)
			return rc;

		rss->indirection_table[i] = cpu_to_le16(abs_l2_queue);
		DP_VERBOSE(p_hwfn, NETIF_MSG_IFUP, "i= %d, queue = %d\n",
			   i, rss->indirection_table[i]);
	}

	for (i = 0; i < 10; i++)
		rss->rss_key[i] = cpu_to_le32(p_params->rss_key[i]);

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

		SET_FIELD(state, ETH_VPORT_TX_MODE_UCAST_ACCEPT_ALL,
			  (!!(accept_filter & QED_ACCEPT_UCAST_MATCHED) &&
			   !!(accept_filter & QED_ACCEPT_UCAST_UNMATCHED)));

		SET_FIELD(state, ETH_VPORT_TX_MODE_MCAST_DROP_ALL,
			  !!(accept_filter & QED_ACCEPT_NONE));

		SET_FIELD(state, ETH_VPORT_TX_MODE_MCAST_ACCEPT_ALL,
			  (!!(accept_filter & QED_ACCEPT_MCAST_MATCHED) &&
			   !!(accept_filter & QED_ACCEPT_MCAST_UNMATCHED)));

		SET_FIELD(state, ETH_VPORT_TX_MODE_BCAST_ACCEPT_ALL,
			  !!(accept_filter & QED_ACCEPT_BCAST));

		p_ramrod->tx_mode.state = cpu_to_le16(state);
		DP_VERBOSE(p_hwfn, QED_MSG_SP,
			   "p_ramrod->tx_mode.state = 0x%x\n", state);
	}
}

static void
qed_sp_update_mcast_bin(struct qed_hwfn *p_hwfn,
			struct vport_update_ramrod_data *p_ramrod,
			struct qed_sp_vport_update_params *p_params)
{
	int i;

	memset(&p_ramrod->approx_mcast.bins, 0,
	       sizeof(p_ramrod->approx_mcast.bins));

	if (p_params->update_approx_mcast_flg) {
		p_ramrod->common.update_approx_mcast_flg = 1;
		for (i = 0; i < ETH_MULTICAST_MAC_BINS_IN_REGS; i++) {
			u32 *p_bins = (u32 *)p_params->bins;
			__le32 val = cpu_to_le32(p_bins[i]);

			p_ramrod->approx_mcast.bins[i] = val;
		}
	}
}

static int
qed_sp_vport_update(struct qed_hwfn *p_hwfn,
		    struct qed_sp_vport_update_params *p_params,
		    enum spq_mode comp_mode,
		    struct qed_spq_comp_cb *p_comp_data)
{
	struct qed_rss_params *p_rss_params = p_params->rss_params;
	struct vport_update_ramrod_data_cmn *p_cmn;
	struct qed_sp_init_request_params sp_params;
	struct vport_update_ramrod_data *p_ramrod = NULL;
	struct qed_spq_entry *p_ent = NULL;
	u8 abs_vport_id = 0;
	int rc = -EINVAL;

	rc = qed_fw_vport(p_hwfn, p_params->vport_id, &abs_vport_id);
	if (rc != 0)
		return rc;

	memset(&sp_params, 0, sizeof(sp_params));
	sp_params.ramrod_data_size = sizeof(*p_ramrod);
	sp_params.comp_mode = comp_mode;
	sp_params.p_comp_data = p_comp_data;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 qed_spq_get_cid(p_hwfn),
				 p_params->opaque_fid,
				 ETH_RAMROD_VPORT_UPDATE,
				 PROTOCOLID_ETH,
				 &sp_params);
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

	rc = qed_sp_vport_update_rss(p_hwfn, p_ramrod, p_rss_params);
	if (rc) {
		/* Return spq entry which is taken in qed_sp_init_request()*/
		qed_spq_return_entry(p_hwfn, p_ent);
		return rc;
	}

	/* Update mcast bins for VFs, PF doesn't use this functionality */
	qed_sp_update_mcast_bin(p_hwfn, p_ramrod, p_params);

	qed_sp_update_accept_mode(p_hwfn, p_ramrod, p_params->accept_flags);
	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int qed_sp_vport_stop(struct qed_hwfn *p_hwfn,
			     u16 opaque_fid,
			     u8 vport_id)
{
	struct qed_sp_init_request_params sp_params;
	struct vport_stop_ramrod_data *p_ramrod;
	struct qed_spq_entry *p_ent;
	u8 abs_vport_id = 0;
	int rc;

	rc = qed_fw_vport(p_hwfn, vport_id, &abs_vport_id);
	if (rc != 0)
		return rc;

	memset(&sp_params, 0, sizeof(sp_params));
	sp_params.ramrod_data_size = sizeof(*p_ramrod);
	sp_params.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 qed_spq_get_cid(p_hwfn),
				 opaque_fid,
				 ETH_RAMROD_VPORT_STOP,
				 PROTOCOLID_ETH,
				 &sp_params);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.vport_stop;
	p_ramrod->vport_id = abs_vport_id;

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int qed_filter_accept_cmd(struct qed_dev *cdev,
				 u8 vport,
				 struct qed_filter_accept_flags accept_flags,
				 enum spq_mode comp_mode,
				 struct qed_spq_comp_cb *p_comp_data)
{
	struct qed_sp_vport_update_params vport_update_params;
	int i, rc;

	/* Prepare and send the vport rx_mode change */
	memset(&vport_update_params, 0, sizeof(vport_update_params));
	vport_update_params.vport_id = vport;
	vport_update_params.accept_flags = accept_flags;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		vport_update_params.opaque_fid = p_hwfn->hw_info.opaque_fid;

		rc = qed_sp_vport_update(p_hwfn, &vport_update_params,
					 comp_mode, p_comp_data);
		if (rc != 0) {
			DP_ERR(cdev, "Update rx_mode failed %d\n", rc);
			return rc;
		}

		DP_VERBOSE(p_hwfn, QED_MSG_SP,
			   "Accept filter configured, flags = [Rx]%x [Tx]%x\n",
			   accept_flags.rx_accept_filter,
			   accept_flags.tx_accept_filter);
	}

	return 0;
}

static int qed_sp_release_queue_cid(
	struct qed_hwfn *p_hwfn,
	struct qed_hw_cid_data *p_cid_data)
{
	if (!p_cid_data->b_cid_allocated)
		return 0;

	qed_cxt_release_cid(p_hwfn, p_cid_data->cid);

	p_cid_data->b_cid_allocated = false;

	return 0;
}

static int
qed_sp_eth_rxq_start_ramrod(struct qed_hwfn *p_hwfn,
			    u16 opaque_fid,
			    u32 cid,
			    struct qed_queue_start_common_params *params,
			    u8 stats_id,
			    u16 bd_max_bytes,
			    dma_addr_t bd_chain_phys_addr,
			    dma_addr_t cqe_pbl_addr,
			    u16 cqe_pbl_size)
{
	struct rx_queue_start_ramrod_data *p_ramrod = NULL;
	struct qed_sp_init_request_params sp_params;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_hw_cid_data *p_rx_cid;
	u16 abs_rx_q_id = 0;
	u8 abs_vport_id = 0;
	int rc = -EINVAL;

	/* Store information for the stop */
	p_rx_cid		= &p_hwfn->p_rx_cids[params->queue_id];
	p_rx_cid->cid		= cid;
	p_rx_cid->opaque_fid	= opaque_fid;
	p_rx_cid->vport_id	= params->vport_id;

	rc = qed_fw_vport(p_hwfn, params->vport_id, &abs_vport_id);
	if (rc != 0)
		return rc;

	rc = qed_fw_l2_queue(p_hwfn, params->queue_id, &abs_rx_q_id);
	if (rc != 0)
		return rc;

	DP_VERBOSE(p_hwfn, QED_MSG_SP,
		   "opaque_fid=0x%x, cid=0x%x, rx_qid=0x%x, vport_id=0x%x, sb_id=0x%x\n",
		   opaque_fid, cid, params->queue_id, params->vport_id,
		   params->sb);

	memset(&sp_params, 0, sizeof(params));
	sp_params.comp_mode = QED_SPQ_MODE_EBLOCK;
	sp_params.ramrod_data_size = sizeof(*p_ramrod);

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 cid, opaque_fid,
				 ETH_RAMROD_RX_QUEUE_START,
				 PROTOCOLID_ETH,
				 &sp_params);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.rx_queue_start;

	p_ramrod->sb_id			= cpu_to_le16(params->sb);
	p_ramrod->sb_index		= params->sb_idx;
	p_ramrod->vport_id		= abs_vport_id;
	p_ramrod->stats_counter_id	= stats_id;
	p_ramrod->rx_queue_id		= cpu_to_le16(abs_rx_q_id);
	p_ramrod->complete_cqe_flg	= 0;
	p_ramrod->complete_event_flg	= 1;

	p_ramrod->bd_max_bytes	= cpu_to_le16(bd_max_bytes);
	p_ramrod->bd_base.hi	= DMA_HI_LE(bd_chain_phys_addr);
	p_ramrod->bd_base.lo	= DMA_LO_LE(bd_chain_phys_addr);

	p_ramrod->num_of_pbl_pages	= cpu_to_le16(cqe_pbl_size);
	p_ramrod->cqe_pbl_addr.hi	= DMA_HI_LE(cqe_pbl_addr);
	p_ramrod->cqe_pbl_addr.lo	= DMA_LO_LE(cqe_pbl_addr);

	rc = qed_spq_post(p_hwfn, p_ent, NULL);

	return rc;
}

static int
qed_sp_eth_rx_queue_start(struct qed_hwfn *p_hwfn,
			  u16 opaque_fid,
			  struct qed_queue_start_common_params *params,
			  u16 bd_max_bytes,
			  dma_addr_t bd_chain_phys_addr,
			  dma_addr_t cqe_pbl_addr,
			  u16 cqe_pbl_size,
			  void __iomem **pp_prod)
{
	struct qed_hw_cid_data *p_rx_cid;
	u64 init_prod_val = 0;
	u16 abs_l2_queue = 0;
	u8 abs_stats_id = 0;
	int rc;

	rc = qed_fw_l2_queue(p_hwfn, params->queue_id, &abs_l2_queue);
	if (rc != 0)
		return rc;

	rc = qed_fw_vport(p_hwfn, params->vport_id, &abs_stats_id);
	if (rc != 0)
		return rc;

	*pp_prod = (u8 __iomem *)p_hwfn->regview +
				 GTT_BAR0_MAP_REG_MSDM_RAM +
				 MSTORM_PRODS_OFFSET(abs_l2_queue);

	/* Init the rcq, rx bd and rx sge (if valid) producers to 0 */
	__internal_ram_wr(p_hwfn, *pp_prod, sizeof(u64),
			  (u32 *)(&init_prod_val));

	/* Allocate a CID for the queue */
	p_rx_cid = &p_hwfn->p_rx_cids[params->queue_id];
	rc = qed_cxt_acquire_cid(p_hwfn, PROTOCOLID_ETH,
				 &p_rx_cid->cid);
	if (rc) {
		DP_NOTICE(p_hwfn, "Failed to acquire cid\n");
		return rc;
	}
	p_rx_cid->b_cid_allocated = true;

	rc = qed_sp_eth_rxq_start_ramrod(p_hwfn,
					 opaque_fid,
					 p_rx_cid->cid,
					 params,
					 abs_stats_id,
					 bd_max_bytes,
					 bd_chain_phys_addr,
					 cqe_pbl_addr,
					 cqe_pbl_size);

	if (rc != 0)
		qed_sp_release_queue_cid(p_hwfn, p_rx_cid);

	return rc;
}

static int qed_sp_eth_rx_queue_stop(struct qed_hwfn *p_hwfn,
				    u16 rx_queue_id,
				    bool eq_completion_only,
				    bool cqe_completion)
{
	struct qed_hw_cid_data *p_rx_cid = &p_hwfn->p_rx_cids[rx_queue_id];
	struct rx_queue_stop_ramrod_data *p_ramrod = NULL;
	struct qed_sp_init_request_params sp_params;
	struct qed_spq_entry *p_ent = NULL;
	u16 abs_rx_q_id = 0;
	int rc = -EINVAL;

	memset(&sp_params, 0, sizeof(sp_params));
	sp_params.ramrod_data_size = sizeof(*p_ramrod);
	sp_params.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 p_rx_cid->cid,
				 p_rx_cid->opaque_fid,
				 ETH_RAMROD_RX_QUEUE_STOP,
				 PROTOCOLID_ETH,
				 &sp_params);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.rx_queue_stop;

	qed_fw_vport(p_hwfn, p_rx_cid->vport_id, &p_ramrod->vport_id);
	qed_fw_l2_queue(p_hwfn, rx_queue_id, &abs_rx_q_id);
	p_ramrod->rx_queue_id = cpu_to_le16(abs_rx_q_id);

	/* Cleaning the queue requires the completion to arrive there.
	 * In addition, VFs require the answer to come as eqe to PF.
	 */
	p_ramrod->complete_cqe_flg =
		(!!(p_rx_cid->opaque_fid == p_hwfn->hw_info.opaque_fid) &&
		 !eq_completion_only) || cqe_completion;
	p_ramrod->complete_event_flg =
		!(p_rx_cid->opaque_fid == p_hwfn->hw_info.opaque_fid) ||
		eq_completion_only;

	rc = qed_spq_post(p_hwfn, p_ent, NULL);
	if (rc)
		return rc;

	return qed_sp_release_queue_cid(p_hwfn, p_rx_cid);
}

static int
qed_sp_eth_txq_start_ramrod(struct qed_hwfn  *p_hwfn,
			    u16  opaque_fid,
			    u32  cid,
			    struct qed_queue_start_common_params *p_params,
			    u8  stats_id,
			    dma_addr_t pbl_addr,
			    u16 pbl_size,
			    union qed_qm_pq_params *p_pq_params)
{
	struct tx_queue_start_ramrod_data *p_ramrod = NULL;
	struct qed_sp_init_request_params sp_params;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_hw_cid_data *p_tx_cid;
	u8 abs_vport_id;
	int rc = -EINVAL;
	u16 pq_id;

	/* Store information for the stop */
	p_tx_cid = &p_hwfn->p_tx_cids[p_params->queue_id];
	p_tx_cid->cid		= cid;
	p_tx_cid->opaque_fid	= opaque_fid;

	rc = qed_fw_vport(p_hwfn, p_params->vport_id, &abs_vport_id);
	if (rc)
		return rc;

	memset(&sp_params, 0, sizeof(sp_params));
	sp_params.ramrod_data_size = sizeof(*p_ramrod);
	sp_params.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent, cid,
				 opaque_fid,
				 ETH_RAMROD_TX_QUEUE_START,
				 PROTOCOLID_ETH,
				 &sp_params);
	if (rc)
		return rc;

	p_ramrod		= &p_ent->ramrod.tx_queue_start;
	p_ramrod->vport_id	= abs_vport_id;

	p_ramrod->sb_id			= cpu_to_le16(p_params->sb);
	p_ramrod->sb_index		= p_params->sb_idx;
	p_ramrod->stats_counter_id	= stats_id;
	p_ramrod->tc			= p_pq_params->eth.tc;

	p_ramrod->pbl_size		= cpu_to_le16(pbl_size);
	p_ramrod->pbl_base_addr.hi	= DMA_HI_LE(pbl_addr);
	p_ramrod->pbl_base_addr.lo	= DMA_LO_LE(pbl_addr);

	pq_id			= qed_get_qm_pq(p_hwfn,
						PROTOCOLID_ETH,
						p_pq_params);
	p_ramrod->qm_pq_id	= cpu_to_le16(pq_id);

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int
qed_sp_eth_tx_queue_start(struct qed_hwfn *p_hwfn,
			  u16 opaque_fid,
			  struct qed_queue_start_common_params *p_params,
			  dma_addr_t pbl_addr,
			  u16 pbl_size,
			  void __iomem **pp_doorbell)
{
	struct qed_hw_cid_data *p_tx_cid;
	union qed_qm_pq_params pq_params;
	u8 abs_stats_id = 0;
	int rc;

	rc = qed_fw_vport(p_hwfn, p_params->vport_id, &abs_stats_id);
	if (rc)
		return rc;

	p_tx_cid = &p_hwfn->p_tx_cids[p_params->queue_id];
	memset(p_tx_cid, 0, sizeof(*p_tx_cid));
	memset(&pq_params, 0, sizeof(pq_params));

	/* Allocate a CID for the queue */
	rc = qed_cxt_acquire_cid(p_hwfn, PROTOCOLID_ETH,
				 &p_tx_cid->cid);
	if (rc) {
		DP_NOTICE(p_hwfn, "Failed to acquire cid\n");
		return rc;
	}
	p_tx_cid->b_cid_allocated = true;

	DP_VERBOSE(p_hwfn, QED_MSG_SP,
		   "opaque_fid=0x%x, cid=0x%x, tx_qid=0x%x, vport_id=0x%x, sb_id=0x%x\n",
		   opaque_fid, p_tx_cid->cid,
		   p_params->queue_id, p_params->vport_id, p_params->sb);

	rc = qed_sp_eth_txq_start_ramrod(p_hwfn,
					 opaque_fid,
					 p_tx_cid->cid,
					 p_params,
					 abs_stats_id,
					 pbl_addr,
					 pbl_size,
					 &pq_params);

	*pp_doorbell = (u8 __iomem *)p_hwfn->doorbells +
				     qed_db_addr(p_tx_cid->cid, DQ_DEMS_LEGACY);

	if (rc)
		qed_sp_release_queue_cid(p_hwfn, p_tx_cid);

	return rc;
}

static int qed_sp_eth_tx_queue_stop(struct qed_hwfn *p_hwfn,
				    u16 tx_queue_id)
{
	struct qed_hw_cid_data *p_tx_cid = &p_hwfn->p_tx_cids[tx_queue_id];
	struct qed_sp_init_request_params sp_params;
	struct qed_spq_entry *p_ent = NULL;
	int rc = -EINVAL;

	memset(&sp_params, 0, sizeof(sp_params));
	sp_params.ramrod_data_size = sizeof(struct tx_queue_stop_ramrod_data);
	sp_params.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 p_tx_cid->cid,
				 p_tx_cid->opaque_fid,
				 ETH_RAMROD_TX_QUEUE_STOP,
				 PROTOCOLID_ETH,
				 &sp_params);
	if (rc)
		return rc;

	rc = qed_spq_post(p_hwfn, p_ent, NULL);
	if (rc)
		return rc;

	return qed_sp_release_queue_cid(p_hwfn, p_tx_cid);
}

static enum eth_filter_action
qed_filter_action(enum qed_filter_opcode opcode)
{
	enum eth_filter_action action = MAX_ETH_FILTER_ACTION;

	switch (opcode) {
	case QED_FILTER_ADD:
		action = ETH_FILTER_ACTION_ADD;
		break;
	case QED_FILTER_REMOVE:
		action = ETH_FILTER_ACTION_REMOVE;
		break;
	case QED_FILTER_REPLACE:
	case QED_FILTER_FLUSH:
		action = ETH_FILTER_ACTION_REPLACE;
		break;
	default:
		action = MAX_ETH_FILTER_ACTION;
	}

	return action;
}

static void qed_set_fw_mac_addr(__le16 *fw_msb,
				__le16 *fw_mid,
				__le16 *fw_lsb,
				u8 *mac)
{
	((u8 *)fw_msb)[0] = mac[1];
	((u8 *)fw_msb)[1] = mac[0];
	((u8 *)fw_mid)[0] = mac[3];
	((u8 *)fw_mid)[1] = mac[2];
	((u8 *)fw_lsb)[0] = mac[5];
	((u8 *)fw_lsb)[1] = mac[4];
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
	struct qed_sp_init_request_params sp_params;
	struct eth_filter_cmd *p_first_filter;
	struct eth_filter_cmd *p_second_filter;
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

	memset(&sp_params, 0, sizeof(sp_params));
	sp_params.ramrod_data_size = sizeof(**pp_ramrod);
	sp_params.comp_mode = comp_mode;
	sp_params.p_comp_data = p_comp_data;

	rc = qed_sp_init_request(p_hwfn, pp_ent,
				 qed_spq_get_cid(p_hwfn),
				 opaque_fid,
				 ETH_RAMROD_FILTERS_UPDATE,
				 PROTOCOLID_ETH,
				 &sp_params);
	if (rc)
		return rc;

	*pp_ramrod = &(*pp_ent)->ramrod.vport_filter_update;
	p_ramrod = *pp_ramrod;
	p_ramrod->filter_cmd_hdr.rx = p_filter_cmd->is_rx_filter ? 1 : 0;
	p_ramrod->filter_cmd_hdr.tx = p_filter_cmd->is_tx_filter ? 1 : 0;

	switch (p_filter_cmd->opcode) {
	case QED_FILTER_FLUSH:
		p_ramrod->filter_cmd_hdr.cmd_cnt = 0; break;
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
		p_second_filter->type		= p_first_filter->type;
		p_second_filter->mac_msb	= p_first_filter->mac_msb;
		p_second_filter->mac_mid	= p_first_filter->mac_mid;
		p_second_filter->mac_lsb	= p_first_filter->mac_lsb;
		p_second_filter->vlan_id	= p_first_filter->vlan_id;
		p_second_filter->vni		= p_first_filter->vni;

		p_first_filter->action = ETH_FILTER_ACTION_REMOVE;

		p_first_filter->vport_id = vport_to_remove_from;

		p_second_filter->action		= ETH_FILTER_ACTION_ADD;
		p_second_filter->vport_id	= vport_to_add_to;
	} else {
		action = qed_filter_action(p_filter_cmd->opcode);

		if (action == MAX_ETH_FILTER_ACTION) {
			DP_NOTICE(p_hwfn,
				  "%d is not supported yet\n",
				  p_filter_cmd->opcode);
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

static int qed_sp_eth_filter_ucast(struct qed_hwfn *p_hwfn,
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
	if (rc != 0) {
		DP_ERR(p_hwfn, "Uni. filter command failed %d\n", rc);
		return rc;
	}
	p_header = &p_ramrod->filter_cmd_hdr;
	p_header->assert_on_error = p_filter_cmd->assert_on_error;

	rc = qed_spq_post(p_hwfn, p_ent, NULL);
	if (rc != 0) {
		DP_ERR(p_hwfn,
		       "Unicast filter ADD command failed %d\n",
		       rc);
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
			   u32 crc32_length,
			   u32 crc32_seed,
			   u8 complement)
{
	u32 byte = 0;
	u32 bit = 0;
	u8 msb = 0;
	u8 current_byte = 0;
	u32 crc32_result = crc32_seed;

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

static inline u32 qed_crc32c_le(u32 seed,
				u8 *mac,
				u32 len)
{
	u32 packet_buf[2] = { 0 };

	memcpy((u8 *)(&packet_buf[0]), &mac[0], 6);
	return qed_calc_crc32c((u8 *)packet_buf, 8, seed, 0);
}

static u8 qed_mcast_bin_from_mac(u8 *mac)
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
	unsigned long bins[ETH_MULTICAST_MAC_BINS_IN_REGS];
	struct vport_update_ramrod_data *p_ramrod = NULL;
	struct qed_sp_init_request_params sp_params;
	struct qed_spq_entry *p_ent = NULL;
	u8 abs_vport_id = 0;
	int rc, i;

	if (p_filter_cmd->opcode == QED_FILTER_ADD) {
		rc = qed_fw_vport(p_hwfn, p_filter_cmd->vport_to_add_to,
				  &abs_vport_id);
		if (rc)
			return rc;
	} else {
		rc = qed_fw_vport(p_hwfn, p_filter_cmd->vport_to_remove_from,
				  &abs_vport_id);
		if (rc)
			return rc;
	}

	memset(&sp_params, 0, sizeof(sp_params));
	sp_params.ramrod_data_size = sizeof(*p_ramrod);
	sp_params.comp_mode = comp_mode;
	sp_params.p_comp_data = p_comp_data;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 qed_spq_get_cid(p_hwfn),
				 p_hwfn->hw_info.opaque_fid,
				 ETH_RAMROD_VPORT_UPDATE,
				 PROTOCOLID_ETH,
				 &sp_params);

	if (rc) {
		DP_ERR(p_hwfn, "Multi-cast command failed %d\n", rc);
		return rc;
	}

	p_ramrod = &p_ent->ramrod.vport_update;
	p_ramrod->common.update_approx_mcast_flg = 1;

	/* explicitly clear out the entire vector */
	memset(&p_ramrod->approx_mcast.bins, 0,
	       sizeof(p_ramrod->approx_mcast.bins));
	memset(bins, 0, sizeof(unsigned long) *
	       ETH_MULTICAST_MAC_BINS_IN_REGS);
	/* filter ADD op is explicit set op and it removes
	 *  any existing filters for the vport
	 */
	if (p_filter_cmd->opcode == QED_FILTER_ADD) {
		for (i = 0; i < p_filter_cmd->num_mc_addrs; i++) {
			u32 bit;

			bit = qed_mcast_bin_from_mac(p_filter_cmd->mac[i]);
			__set_bit(bit, bins);
		}

		/* Convert to correct endianity */
		for (i = 0; i < ETH_MULTICAST_MAC_BINS_IN_REGS; i++) {
			u32 *p_bins = (u32 *)bins;
			struct vport_update_ramrod_mcast *approx_mcast;

			approx_mcast = &p_ramrod->approx_mcast;
			approx_mcast->bins[i] = cpu_to_le32(p_bins[i]);
		}
	}

	p_ramrod->common.vport_id = abs_vport_id;

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int
qed_filter_mcast_cmd(struct qed_dev *cdev,
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

		if (rc != 0)
			break;

		opaque_fid = p_hwfn->hw_info.opaque_fid;

		rc = qed_sp_eth_filter_mcast(p_hwfn,
					     opaque_fid,
					     p_filter_cmd,
					     comp_mode,
					     p_comp_data);
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

		if (rc != 0)
			break;

		opaque_fid = p_hwfn->hw_info.opaque_fid;

		rc = qed_sp_eth_filter_ucast(p_hwfn,
					     opaque_fid,
					     p_filter_cmd,
					     comp_mode,
					     p_comp_data);
	}

	return rc;
}

static int qed_fill_eth_dev_info(struct qed_dev *cdev,
				 struct qed_dev_eth_info *info)
{
	int i;

	memset(info, 0, sizeof(*info));

	info->num_tc = 1;

	if (cdev->int_params.out.int_mode == QED_INT_MODE_MSIX) {
		for_each_hwfn(cdev, i)
			info->num_queues += FEAT_NUM(&cdev->hwfns[i],
						     QED_PF_L2_QUE);
		if (cdev->int_params.fp_msix_cnt)
			info->num_queues = min_t(u8, info->num_queues,
						 cdev->int_params.fp_msix_cnt);
	} else {
		info->num_queues = cdev->num_hwfns;
	}

	info->num_vlan_filters = RESC_NUM(&cdev->hwfns[0], QED_VLAN);
	ether_addr_copy(info->port_mac,
			cdev->hwfns[0].hw_info.hw_mac_addr);

	qed_fill_dev_info(cdev, &info->common);

	return 0;
}

static void qed_register_eth_ops(struct qed_dev *cdev,
				 struct qed_eth_cb_ops *ops,
				 void *cookie)
{
	cdev->protocol_ops.eth	= ops;
	cdev->ops_cookie	= cookie;
}

static int qed_start_vport(struct qed_dev *cdev,
			   u8 vport_id,
			   u16 mtu,
			   u8 drop_ttl0_flg,
			   u8 inner_vlan_removal_en_flg)
{
	int rc, i;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		rc = qed_sp_vport_start(p_hwfn,
					p_hwfn->hw_info.concrete_fid,
					p_hwfn->hw_info.opaque_fid,
					vport_id,
					mtu,
					drop_ttl0_flg,
					inner_vlan_removal_en_flg);

		if (rc) {
			DP_ERR(cdev, "Failed to start VPORT\n");
			return rc;
		}

		qed_hw_start_fastpath(p_hwfn);

		DP_VERBOSE(cdev, (QED_MSG_SPQ | NETIF_MSG_IFUP),
			   "Started V-PORT %d with MTU %d\n",
			   vport_id, mtu);
	}

	qed_reset_vport_stats(cdev);

	return 0;
}

static int qed_stop_vport(struct qed_dev *cdev,
			  u8 vport_id)
{
	int rc, i;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		rc = qed_sp_vport_stop(p_hwfn,
				       p_hwfn->hw_info.opaque_fid,
				       vport_id);

		if (rc) {
			DP_ERR(cdev, "Failed to stop VPORT\n");
			return rc;
		}
	}
	return 0;
}

static int qed_update_vport(struct qed_dev *cdev,
			    struct qed_update_vport_params *params)
{
	struct qed_sp_vport_update_params sp_params;
	struct qed_rss_params sp_rss_params;
	int rc, i;

	if (!cdev)
		return -ENODEV;

	memset(&sp_params, 0, sizeof(sp_params));
	memset(&sp_rss_params, 0, sizeof(sp_rss_params));

	/* Translate protocol params into sp params */
	sp_params.vport_id = params->vport_id;
	sp_params.update_vport_active_rx_flg =
		params->update_vport_active_flg;
	sp_params.update_vport_active_tx_flg =
		params->update_vport_active_flg;
	sp_params.vport_active_rx_flg = params->vport_active_flg;
	sp_params.vport_active_tx_flg = params->vport_active_flg;

	/* RSS - is a bit tricky, since upper-layer isn't familiar with hwfns.
	 * We need to re-fix the rss values per engine for CMT.
	 */
	if (cdev->num_hwfns > 1 && params->update_rss_flg) {
		struct qed_update_vport_rss_params *rss =
			&params->rss_params;
		int k, max = 0;

		/* Find largest entry, since it's possible RSS needs to
		 * be disabled [in case only 1 queue per-hwfn]
		 */
		for (k = 0; k < QED_RSS_IND_TABLE_SIZE; k++)
			max = (max > rss->rss_ind_table[k]) ?
				max : rss->rss_ind_table[k];

		/* Either fix RSS values or disable RSS */
		if (cdev->num_hwfns < max + 1) {
			int divisor = (max + cdev->num_hwfns - 1) /
				cdev->num_hwfns;

			DP_VERBOSE(cdev, (QED_MSG_SPQ | NETIF_MSG_IFUP),
				   "CMT - fixing RSS values (modulo %02x)\n",
				   divisor);

			for (k = 0; k < QED_RSS_IND_TABLE_SIZE; k++)
				rss->rss_ind_table[k] =
					rss->rss_ind_table[k] % divisor;
		} else {
			DP_VERBOSE(cdev, (QED_MSG_SPQ | NETIF_MSG_IFUP),
				   "CMT - 1 queue per-hwfn; Disabling RSS\n");
			params->update_rss_flg = 0;
		}
	}

	/* Now, update the RSS configuration for actual configuration */
	if (params->update_rss_flg) {
		sp_rss_params.update_rss_config = 1;
		sp_rss_params.rss_enable = 1;
		sp_rss_params.update_rss_capabilities = 1;
		sp_rss_params.update_rss_ind_table = 1;
		sp_rss_params.update_rss_key = 1;
		sp_rss_params.rss_caps = QED_RSS_IPV4 |
					 QED_RSS_IPV6 |
					 QED_RSS_IPV4_TCP | QED_RSS_IPV6_TCP;
		sp_rss_params.rss_table_size_log = 7; /* 2^7 = 128 */
		memcpy(sp_rss_params.rss_ind_table,
		       params->rss_params.rss_ind_table,
		       QED_RSS_IND_TABLE_SIZE * sizeof(u16));
		memcpy(sp_rss_params.rss_key, params->rss_params.rss_key,
		       QED_RSS_KEY_SIZE * sizeof(u32));
	}
	sp_params.rss_params = &sp_rss_params;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		sp_params.opaque_fid = p_hwfn->hw_info.opaque_fid;
		rc = qed_sp_vport_update(p_hwfn, &sp_params,
					 QED_SPQ_MODE_EBLOCK,
					 NULL);
		if (rc) {
			DP_ERR(cdev, "Failed to update VPORT\n");
			return rc;
		}

		DP_VERBOSE(cdev, (QED_MSG_SPQ | NETIF_MSG_IFUP),
			   "Updated V-PORT %d: active_flag %d [update %d]\n",
			   params->vport_id, params->vport_active_flg,
			   params->update_vport_active_flg);
	}

	return 0;
}

static int qed_start_rxq(struct qed_dev *cdev,
			 struct qed_queue_start_common_params *params,
			 u16 bd_max_bytes,
			 dma_addr_t bd_chain_phys_addr,
			 dma_addr_t cqe_pbl_addr,
			 u16 cqe_pbl_size,
			 void __iomem **pp_prod)
{
	int rc, hwfn_index;
	struct qed_hwfn *p_hwfn;

	hwfn_index = params->rss_id % cdev->num_hwfns;
	p_hwfn = &cdev->hwfns[hwfn_index];

	/* Fix queue ID in 100g mode */
	params->queue_id /= cdev->num_hwfns;

	rc = qed_sp_eth_rx_queue_start(p_hwfn,
				       p_hwfn->hw_info.opaque_fid,
				       params,
				       bd_max_bytes,
				       bd_chain_phys_addr,
				       cqe_pbl_addr,
				       cqe_pbl_size,
				       pp_prod);

	if (rc) {
		DP_ERR(cdev, "Failed to start RXQ#%d\n", params->queue_id);
		return rc;
	}

	DP_VERBOSE(cdev, (QED_MSG_SPQ | NETIF_MSG_IFUP),
		   "Started RX-Q %d [rss %d] on V-PORT %d and SB %d\n",
		   params->queue_id, params->rss_id, params->vport_id,
		   params->sb);

	return 0;
}

static int qed_stop_rxq(struct qed_dev *cdev,
			struct qed_stop_rxq_params *params)
{
	int rc, hwfn_index;
	struct qed_hwfn *p_hwfn;

	hwfn_index	= params->rss_id % cdev->num_hwfns;
	p_hwfn		= &cdev->hwfns[hwfn_index];

	rc = qed_sp_eth_rx_queue_stop(p_hwfn,
				      params->rx_queue_id / cdev->num_hwfns,
				      params->eq_completion_only,
				      false);
	if (rc) {
		DP_ERR(cdev, "Failed to stop RXQ#%d\n", params->rx_queue_id);
		return rc;
	}

	return 0;
}

static int qed_start_txq(struct qed_dev *cdev,
			 struct qed_queue_start_common_params *p_params,
			 dma_addr_t pbl_addr,
			 u16 pbl_size,
			 void __iomem **pp_doorbell)
{
	struct qed_hwfn *p_hwfn;
	int rc, hwfn_index;

	hwfn_index	= p_params->rss_id % cdev->num_hwfns;
	p_hwfn		= &cdev->hwfns[hwfn_index];

	/* Fix queue ID in 100g mode */
	p_params->queue_id /= cdev->num_hwfns;

	rc = qed_sp_eth_tx_queue_start(p_hwfn,
				       p_hwfn->hw_info.opaque_fid,
				       p_params,
				       pbl_addr,
				       pbl_size,
				       pp_doorbell);

	if (rc) {
		DP_ERR(cdev, "Failed to start TXQ#%d\n", p_params->queue_id);
		return rc;
	}

	DP_VERBOSE(cdev, (QED_MSG_SPQ | NETIF_MSG_IFUP),
		   "Started TX-Q %d [rss %d] on V-PORT %d and SB %d\n",
		   p_params->queue_id, p_params->rss_id, p_params->vport_id,
		   p_params->sb);

	return 0;
}

#define QED_HW_STOP_RETRY_LIMIT (10)
static int qed_fastpath_stop(struct qed_dev *cdev)
{
	qed_hw_stop_fastpath(cdev);

	return 0;
}

static int qed_stop_txq(struct qed_dev *cdev,
			struct qed_stop_txq_params *params)
{
	struct qed_hwfn *p_hwfn;
	int rc, hwfn_index;

	hwfn_index	= params->rss_id % cdev->num_hwfns;
	p_hwfn		= &cdev->hwfns[hwfn_index];

	rc = qed_sp_eth_tx_queue_stop(p_hwfn,
				      params->tx_queue_id / cdev->num_hwfns);
	if (rc) {
		DP_ERR(cdev, "Failed to stop TXQ#%d\n", params->tx_queue_id);
		return rc;
	}

	return 0;
}

static int qed_configure_filter_rx_mode(struct qed_dev *cdev,
					enum qed_filter_rx_mode_type type)
{
	struct qed_filter_accept_flags accept_flags;

	memset(&accept_flags, 0, sizeof(accept_flags));

	accept_flags.update_rx_mode_config	= 1;
	accept_flags.update_tx_mode_config	= 1;
	accept_flags.rx_accept_filter		= QED_ACCEPT_UCAST_MATCHED |
						  QED_ACCEPT_MCAST_MATCHED |
						  QED_ACCEPT_BCAST;
	accept_flags.tx_accept_filter = QED_ACCEPT_UCAST_MATCHED |
					QED_ACCEPT_MCAST_MATCHED |
					QED_ACCEPT_BCAST;

	if (type == QED_FILTER_RX_MODE_TYPE_PROMISC)
		accept_flags.rx_accept_filter |= QED_ACCEPT_UCAST_UNMATCHED |
						 QED_ACCEPT_MCAST_UNMATCHED;
	else if (type == QED_FILTER_RX_MODE_TYPE_MULTI_PROMISC)
		accept_flags.rx_accept_filter |= QED_ACCEPT_MCAST_UNMATCHED;

	return qed_filter_accept_cmd(cdev, 0, accept_flags,
				     QED_SPQ_MODE_CB, NULL);
}

static int qed_configure_filter_ucast(struct qed_dev *cdev,
				      struct qed_filter_ucast_params *params)
{
	struct qed_filter_ucast ucast;

	if (!params->vlan_valid && !params->mac_valid) {
		DP_NOTICE(
			cdev,
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

	return qed_filter_mcast_cmd(cdev, &mcast,
				    QED_SPQ_MODE_CB, NULL);
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
		DP_NOTICE(cdev, "Unknown filter type %d\n",
			  (int)params->type);
		return -EINVAL;
	}
}

static int qed_fp_cqe_completion(struct qed_dev *dev,
				 u8 rss_id,
				 struct eth_slow_path_rx_cqe *cqe)
{
	return qed_eth_cqe_completion(&dev->hwfns[rss_id % dev->num_hwfns],
				      cqe);
}

static const struct qed_eth_ops qed_eth_ops_pass = {
	.common = &qed_common_ops_pass,
	.fill_dev_info = &qed_fill_eth_dev_info,
	.register_ops = &qed_register_eth_ops,
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
};

const struct qed_eth_ops *qed_get_eth_ops(u32 version)
{
	if (version != QED_ETH_INTERFACE_VERSION) {
		pr_notice("Cannot supply ethtool operations [%08x != %08x]\n",
			  version, QED_ETH_INTERFACE_VERSION);
		return NULL;
	}

	return &qed_eth_ops_pass;
}
EXPORT_SYMBOL(qed_get_eth_ops);

void qed_put_eth_ops(void)
{
	/* TODO - reference count for module? */
}
EXPORT_SYMBOL(qed_put_eth_ops);
