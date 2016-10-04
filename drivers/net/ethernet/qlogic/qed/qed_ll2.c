/* QLogic qed NIC Driver
 *
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/dma-mapping.h>
#include <linux/if_vlan.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <net/ipv6.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/qed/qed_ll2_if.h>
#include "qed.h"
#include "qed_cxt.h"
#include "qed_dev_api.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_int.h"
#include "qed_ll2.h"
#include "qed_mcp.h"
#include "qed_reg_addr.h"
#include "qed_sp.h"

#define QED_LL2_RX_REGISTERED(ll2)	((ll2)->rx_queue.b_cb_registred)
#define QED_LL2_TX_REGISTERED(ll2)	((ll2)->tx_queue.b_cb_registred)

#define QED_LL2_TX_SIZE (256)
#define QED_LL2_RX_SIZE (4096)

struct qed_cb_ll2_info {
	int rx_cnt;
	u32 rx_size;
	u8 handle;
	bool frags_mapped;

	/* Lock protecting LL2 buffer lists in sleepless context */
	spinlock_t lock;
	struct list_head list;

	const struct qed_ll2_cb_ops *cbs;
	void *cb_cookie;
};

struct qed_ll2_buffer {
	struct list_head list;
	void *data;
	dma_addr_t phys_addr;
};

static void qed_ll2b_complete_tx_packet(struct qed_hwfn *p_hwfn,
					u8 connection_handle,
					void *cookie,
					dma_addr_t first_frag_addr,
					bool b_last_fragment,
					bool b_last_packet)
{
	struct qed_dev *cdev = p_hwfn->cdev;
	struct sk_buff *skb = cookie;

	/* All we need to do is release the mapping */
	dma_unmap_single(&p_hwfn->cdev->pdev->dev, first_frag_addr,
			 skb_headlen(skb), DMA_TO_DEVICE);

	if (cdev->ll2->cbs && cdev->ll2->cbs->tx_cb)
		cdev->ll2->cbs->tx_cb(cdev->ll2->cb_cookie, skb,
				      b_last_fragment);

	if (cdev->ll2->frags_mapped)
		/* Case where mapped frags were received, need to
		 * free skb with nr_frags marked as 0
		 */
		skb_shinfo(skb)->nr_frags = 0;

	dev_kfree_skb_any(skb);
}

static int qed_ll2_alloc_buffer(struct qed_dev *cdev,
				u8 **data, dma_addr_t *phys_addr)
{
	*data = kmalloc(cdev->ll2->rx_size, GFP_ATOMIC);
	if (!(*data)) {
		DP_INFO(cdev, "Failed to allocate LL2 buffer data\n");
		return -ENOMEM;
	}

	*phys_addr = dma_map_single(&cdev->pdev->dev,
				    ((*data) + NET_SKB_PAD),
				    cdev->ll2->rx_size, DMA_FROM_DEVICE);
	if (dma_mapping_error(&cdev->pdev->dev, *phys_addr)) {
		DP_INFO(cdev, "Failed to map LL2 buffer data\n");
		kfree((*data));
		return -ENOMEM;
	}

	return 0;
}

static int qed_ll2_dealloc_buffer(struct qed_dev *cdev,
				 struct qed_ll2_buffer *buffer)
{
	spin_lock_bh(&cdev->ll2->lock);

	dma_unmap_single(&cdev->pdev->dev, buffer->phys_addr,
			 cdev->ll2->rx_size, DMA_FROM_DEVICE);
	kfree(buffer->data);
	list_del(&buffer->list);

	cdev->ll2->rx_cnt--;
	if (!cdev->ll2->rx_cnt)
		DP_INFO(cdev, "All LL2 entries were removed\n");

	spin_unlock_bh(&cdev->ll2->lock);

	return 0;
}

static void qed_ll2_kill_buffers(struct qed_dev *cdev)
{
	struct qed_ll2_buffer *buffer, *tmp_buffer;

	list_for_each_entry_safe(buffer, tmp_buffer, &cdev->ll2->list, list)
		qed_ll2_dealloc_buffer(cdev, buffer);
}

void qed_ll2b_complete_rx_packet(struct qed_hwfn *p_hwfn,
				 u8 connection_handle,
				 struct qed_ll2_rx_packet *p_pkt,
				 struct core_rx_fast_path_cqe *p_cqe,
				 bool b_last_packet)
{
	u16 packet_length = le16_to_cpu(p_cqe->packet_length);
	struct qed_ll2_buffer *buffer = p_pkt->cookie;
	struct qed_dev *cdev = p_hwfn->cdev;
	u16 vlan = le16_to_cpu(p_cqe->vlan);
	u32 opaque_data_0, opaque_data_1;
	u8 pad = p_cqe->placement_offset;
	dma_addr_t new_phys_addr;
	struct sk_buff *skb;
	bool reuse = false;
	int rc = -EINVAL;
	u8 *new_data;

	opaque_data_0 = le32_to_cpu(p_cqe->opaque_data.data[0]);
	opaque_data_1 = le32_to_cpu(p_cqe->opaque_data.data[1]);

	DP_VERBOSE(p_hwfn,
		   (NETIF_MSG_RX_STATUS | QED_MSG_STORAGE | NETIF_MSG_PKTDATA),
		   "Got an LL2 Rx completion: [Buffer at phys 0x%llx, offset 0x%02x] Length 0x%04x Parse_flags 0x%04x vlan 0x%04x Opaque data [0x%08x:0x%08x]\n",
		   (u64)p_pkt->rx_buf_addr, pad, packet_length,
		   le16_to_cpu(p_cqe->parse_flags.flags), vlan,
		   opaque_data_0, opaque_data_1);

	if ((cdev->dp_module & NETIF_MSG_PKTDATA) && buffer->data) {
		print_hex_dump(KERN_INFO, "",
			       DUMP_PREFIX_OFFSET, 16, 1,
			       buffer->data, packet_length, false);
	}

	/* Determine if data is valid */
	if (packet_length < ETH_HLEN)
		reuse = true;

	/* Allocate a replacement for buffer; Reuse upon failure */
	if (!reuse)
		rc = qed_ll2_alloc_buffer(p_hwfn->cdev, &new_data,
					  &new_phys_addr);

	/* If need to reuse or there's no replacement buffer, repost this */
	if (rc)
		goto out_post;

	skb = build_skb(buffer->data, 0);
	if (!skb) {
		rc = -ENOMEM;
		goto out_post;
	}

	pad += NET_SKB_PAD;
	skb_reserve(skb, pad);
	skb_put(skb, packet_length);
	skb_checksum_none_assert(skb);

	/* Get parital ethernet information instead of eth_type_trans(),
	 * Since we don't have an associated net_device.
	 */
	skb_reset_mac_header(skb);
	skb->protocol = eth_hdr(skb)->h_proto;

	/* Pass SKB onward */
	if (cdev->ll2->cbs && cdev->ll2->cbs->rx_cb) {
		if (vlan)
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vlan);
		cdev->ll2->cbs->rx_cb(cdev->ll2->cb_cookie, skb,
				      opaque_data_0, opaque_data_1);
	}

	/* Update Buffer information and update FW producer */
	buffer->data = new_data;
	buffer->phys_addr = new_phys_addr;

out_post:
	rc = qed_ll2_post_rx_buffer(QED_LEADING_HWFN(cdev), cdev->ll2->handle,
				    buffer->phys_addr, 0,  buffer, 1);

	if (rc)
		qed_ll2_dealloc_buffer(cdev, buffer);
}

static struct qed_ll2_info *__qed_ll2_handle_sanity(struct qed_hwfn *p_hwfn,
						    u8 connection_handle,
						    bool b_lock,
						    bool b_only_active)
{
	struct qed_ll2_info *p_ll2_conn, *p_ret = NULL;

	if (connection_handle >= QED_MAX_NUM_OF_LL2_CONNECTIONS)
		return NULL;

	if (!p_hwfn->p_ll2_info)
		return NULL;

	p_ll2_conn = &p_hwfn->p_ll2_info[connection_handle];

	if (b_only_active) {
		if (b_lock)
			mutex_lock(&p_ll2_conn->mutex);
		if (p_ll2_conn->b_active)
			p_ret = p_ll2_conn;
		if (b_lock)
			mutex_unlock(&p_ll2_conn->mutex);
	} else {
		p_ret = p_ll2_conn;
	}

	return p_ret;
}

static struct qed_ll2_info *qed_ll2_handle_sanity(struct qed_hwfn *p_hwfn,
						  u8 connection_handle)
{
	return __qed_ll2_handle_sanity(p_hwfn, connection_handle, false, true);
}

static struct qed_ll2_info *qed_ll2_handle_sanity_lock(struct qed_hwfn *p_hwfn,
						       u8 connection_handle)
{
	return __qed_ll2_handle_sanity(p_hwfn, connection_handle, true, true);
}

static struct qed_ll2_info *qed_ll2_handle_sanity_inactive(struct qed_hwfn
							   *p_hwfn,
							   u8 connection_handle)
{
	return __qed_ll2_handle_sanity(p_hwfn, connection_handle, false, false);
}

static void qed_ll2_txq_flush(struct qed_hwfn *p_hwfn, u8 connection_handle)
{
	bool b_last_packet = false, b_last_frag = false;
	struct qed_ll2_tx_packet *p_pkt = NULL;
	struct qed_ll2_info *p_ll2_conn;
	struct qed_ll2_tx_queue *p_tx;
	dma_addr_t tx_frag;

	p_ll2_conn = qed_ll2_handle_sanity_inactive(p_hwfn, connection_handle);
	if (!p_ll2_conn)
		return;

	p_tx = &p_ll2_conn->tx_queue;

	while (!list_empty(&p_tx->active_descq)) {
		p_pkt = list_first_entry(&p_tx->active_descq,
					 struct qed_ll2_tx_packet, list_entry);
		if (!p_pkt)
			break;

		list_del(&p_pkt->list_entry);
		b_last_packet = list_empty(&p_tx->active_descq);
		list_add_tail(&p_pkt->list_entry, &p_tx->free_descq);
		p_tx->cur_completing_packet = *p_pkt;
		p_tx->cur_completing_bd_idx = 1;
		b_last_frag = p_tx->cur_completing_bd_idx == p_pkt->bd_used;
		tx_frag = p_pkt->bds_set[0].tx_frag;
		if (p_ll2_conn->gsi_enable)
			qed_ll2b_release_tx_gsi_packet(p_hwfn,
						       p_ll2_conn->my_id,
						       p_pkt->cookie,
						       tx_frag,
						       b_last_frag,
						       b_last_packet);
		else
			qed_ll2b_complete_tx_packet(p_hwfn,
						    p_ll2_conn->my_id,
						    p_pkt->cookie,
						    tx_frag,
						    b_last_frag,
						    b_last_packet);

	}
}

static int qed_ll2_txq_completion(struct qed_hwfn *p_hwfn, void *p_cookie)
{
	struct qed_ll2_info *p_ll2_conn = p_cookie;
	struct qed_ll2_tx_queue *p_tx = &p_ll2_conn->tx_queue;
	u16 new_idx = 0, num_bds = 0, num_bds_in_packet = 0;
	struct qed_ll2_tx_packet *p_pkt;
	bool b_last_frag = false;
	unsigned long flags;
	dma_addr_t tx_frag;
	int rc = -EINVAL;

	spin_lock_irqsave(&p_tx->lock, flags);
	if (p_tx->b_completing_packet) {
		rc = -EBUSY;
		goto out;
	}

	new_idx = le16_to_cpu(*p_tx->p_fw_cons);
	num_bds = ((s16)new_idx - (s16)p_tx->bds_idx);
	while (num_bds) {
		if (list_empty(&p_tx->active_descq))
			goto out;

		p_pkt = list_first_entry(&p_tx->active_descq,
					 struct qed_ll2_tx_packet, list_entry);
		if (!p_pkt)
			goto out;

		p_tx->b_completing_packet = true;
		p_tx->cur_completing_packet = *p_pkt;
		num_bds_in_packet = p_pkt->bd_used;
		list_del(&p_pkt->list_entry);

		if (num_bds < num_bds_in_packet) {
			DP_NOTICE(p_hwfn,
				  "Rest of BDs does not cover whole packet\n");
			goto out;
		}

		num_bds -= num_bds_in_packet;
		p_tx->bds_idx += num_bds_in_packet;
		while (num_bds_in_packet--)
			qed_chain_consume(&p_tx->txq_chain);

		p_tx->cur_completing_bd_idx = 1;
		b_last_frag = p_tx->cur_completing_bd_idx == p_pkt->bd_used;
		list_add_tail(&p_pkt->list_entry, &p_tx->free_descq);

		spin_unlock_irqrestore(&p_tx->lock, flags);
		tx_frag = p_pkt->bds_set[0].tx_frag;
		if (p_ll2_conn->gsi_enable)
			qed_ll2b_complete_tx_gsi_packet(p_hwfn,
							p_ll2_conn->my_id,
							p_pkt->cookie,
							tx_frag,
							b_last_frag, !num_bds);
		else
			qed_ll2b_complete_tx_packet(p_hwfn,
						    p_ll2_conn->my_id,
						    p_pkt->cookie,
						    tx_frag,
						    b_last_frag, !num_bds);
		spin_lock_irqsave(&p_tx->lock, flags);
	}

	p_tx->b_completing_packet = false;
	rc = 0;
out:
	spin_unlock_irqrestore(&p_tx->lock, flags);
	return rc;
}

static int
qed_ll2_rxq_completion_gsi(struct qed_hwfn *p_hwfn,
			   struct qed_ll2_info *p_ll2_info,
			   union core_rx_cqe_union *p_cqe,
			   unsigned long lock_flags, bool b_last_cqe)
{
	struct qed_ll2_rx_queue *p_rx = &p_ll2_info->rx_queue;
	struct qed_ll2_rx_packet *p_pkt = NULL;
	u16 packet_length, parse_flags, vlan;
	u32 src_mac_addrhi;
	u16 src_mac_addrlo;

	if (!list_empty(&p_rx->active_descq))
		p_pkt = list_first_entry(&p_rx->active_descq,
					 struct qed_ll2_rx_packet, list_entry);
	if (!p_pkt) {
		DP_NOTICE(p_hwfn,
			  "GSI Rx completion but active_descq is empty\n");
		return -EIO;
	}

	list_del(&p_pkt->list_entry);
	parse_flags = le16_to_cpu(p_cqe->rx_cqe_gsi.parse_flags.flags);
	packet_length = le16_to_cpu(p_cqe->rx_cqe_gsi.data_length);
	vlan = le16_to_cpu(p_cqe->rx_cqe_gsi.vlan);
	src_mac_addrhi = le32_to_cpu(p_cqe->rx_cqe_gsi.src_mac_addrhi);
	src_mac_addrlo = le16_to_cpu(p_cqe->rx_cqe_gsi.src_mac_addrlo);
	if (qed_chain_consume(&p_rx->rxq_chain) != p_pkt->rxq_bd)
		DP_NOTICE(p_hwfn,
			  "Mismatch between active_descq and the LL2 Rx chain\n");
	list_add_tail(&p_pkt->list_entry, &p_rx->free_descq);

	spin_unlock_irqrestore(&p_rx->lock, lock_flags);
	qed_ll2b_complete_rx_gsi_packet(p_hwfn,
					p_ll2_info->my_id,
					p_pkt->cookie,
					p_pkt->rx_buf_addr,
					packet_length,
					p_cqe->rx_cqe_gsi.data_length_error,
					parse_flags,
					vlan,
					src_mac_addrhi,
					src_mac_addrlo, b_last_cqe);
	spin_lock_irqsave(&p_rx->lock, lock_flags);

	return 0;
}

static int qed_ll2_rxq_completion_reg(struct qed_hwfn *p_hwfn,
				      struct qed_ll2_info *p_ll2_conn,
				      union core_rx_cqe_union *p_cqe,
				      unsigned long lock_flags,
				      bool b_last_cqe)
{
	struct qed_ll2_rx_queue *p_rx = &p_ll2_conn->rx_queue;
	struct qed_ll2_rx_packet *p_pkt = NULL;

	if (!list_empty(&p_rx->active_descq))
		p_pkt = list_first_entry(&p_rx->active_descq,
					 struct qed_ll2_rx_packet, list_entry);
	if (!p_pkt) {
		DP_NOTICE(p_hwfn,
			  "LL2 Rx completion but active_descq is empty\n");
		return -EIO;
	}
	list_del(&p_pkt->list_entry);

	if (qed_chain_consume(&p_rx->rxq_chain) != p_pkt->rxq_bd)
		DP_NOTICE(p_hwfn,
			  "Mismatch between active_descq and the LL2 Rx chain\n");
	list_add_tail(&p_pkt->list_entry, &p_rx->free_descq);

	spin_unlock_irqrestore(&p_rx->lock, lock_flags);
	qed_ll2b_complete_rx_packet(p_hwfn, p_ll2_conn->my_id,
				    p_pkt, &p_cqe->rx_cqe_fp, b_last_cqe);
	spin_lock_irqsave(&p_rx->lock, lock_flags);

	return 0;
}

static int qed_ll2_rxq_completion(struct qed_hwfn *p_hwfn, void *cookie)
{
	struct qed_ll2_info *p_ll2_conn = cookie;
	struct qed_ll2_rx_queue *p_rx = &p_ll2_conn->rx_queue;
	union core_rx_cqe_union *cqe = NULL;
	u16 cq_new_idx = 0, cq_old_idx = 0;
	unsigned long flags = 0;
	int rc = 0;

	spin_lock_irqsave(&p_rx->lock, flags);
	cq_new_idx = le16_to_cpu(*p_rx->p_fw_cons);
	cq_old_idx = qed_chain_get_cons_idx(&p_rx->rcq_chain);

	while (cq_new_idx != cq_old_idx) {
		bool b_last_cqe = (cq_new_idx == cq_old_idx);

		cqe = qed_chain_consume(&p_rx->rcq_chain);
		cq_old_idx = qed_chain_get_cons_idx(&p_rx->rcq_chain);

		DP_VERBOSE(p_hwfn,
			   QED_MSG_LL2,
			   "LL2 [sw. cons %04x, fw. at %04x] - Got Packet of type %02x\n",
			   cq_old_idx, cq_new_idx, cqe->rx_cqe_sp.type);

		switch (cqe->rx_cqe_sp.type) {
		case CORE_RX_CQE_TYPE_SLOW_PATH:
			DP_NOTICE(p_hwfn, "LL2 - unexpected Rx CQE slowpath\n");
			rc = -EINVAL;
			break;
		case CORE_RX_CQE_TYPE_GSI_OFFLOAD:
			rc = qed_ll2_rxq_completion_gsi(p_hwfn, p_ll2_conn,
							cqe, flags, b_last_cqe);
			break;
		case CORE_RX_CQE_TYPE_REGULAR:
			rc = qed_ll2_rxq_completion_reg(p_hwfn, p_ll2_conn,
							cqe, flags, b_last_cqe);
			break;
		default:
			rc = -EIO;
		}
	}

	spin_unlock_irqrestore(&p_rx->lock, flags);
	return rc;
}

void qed_ll2_rxq_flush(struct qed_hwfn *p_hwfn, u8 connection_handle)
{
	struct qed_ll2_info *p_ll2_conn = NULL;
	struct qed_ll2_rx_packet *p_pkt = NULL;
	struct qed_ll2_rx_queue *p_rx;

	p_ll2_conn = qed_ll2_handle_sanity_inactive(p_hwfn, connection_handle);
	if (!p_ll2_conn)
		return;

	p_rx = &p_ll2_conn->rx_queue;

	while (!list_empty(&p_rx->active_descq)) {
		dma_addr_t rx_buf_addr;
		void *cookie;
		bool b_last;

		p_pkt = list_first_entry(&p_rx->active_descq,
					 struct qed_ll2_rx_packet, list_entry);
		if (!p_pkt)
			break;

		list_del(&p_pkt->list_entry);
		list_add_tail(&p_pkt->list_entry, &p_rx->free_descq);

		rx_buf_addr = p_pkt->rx_buf_addr;
		cookie = p_pkt->cookie;

		b_last = list_empty(&p_rx->active_descq);
	}
}

static int qed_sp_ll2_rx_queue_start(struct qed_hwfn *p_hwfn,
				     struct qed_ll2_info *p_ll2_conn,
				     u8 action_on_error)
{
	enum qed_ll2_conn_type conn_type = p_ll2_conn->conn_type;
	struct qed_ll2_rx_queue *p_rx = &p_ll2_conn->rx_queue;
	struct core_rx_start_ramrod_data *p_ramrod = NULL;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	u16 cqe_pbl_size;
	int rc = 0;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = p_ll2_conn->cid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 CORE_RAMROD_RX_QUEUE_START,
				 PROTOCOLID_CORE, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.core_rx_queue_start;

	p_ramrod->sb_id = cpu_to_le16(qed_int_get_sp_sb_id(p_hwfn));
	p_ramrod->sb_index = p_rx->rx_sb_index;
	p_ramrod->complete_event_flg = 1;

	p_ramrod->mtu = cpu_to_le16(p_ll2_conn->mtu);
	DMA_REGPAIR_LE(p_ramrod->bd_base,
		       p_rx->rxq_chain.p_phys_addr);
	cqe_pbl_size = (u16)qed_chain_get_page_cnt(&p_rx->rcq_chain);
	p_ramrod->num_of_pbl_pages = cpu_to_le16(cqe_pbl_size);
	DMA_REGPAIR_LE(p_ramrod->cqe_pbl_addr,
		       qed_chain_get_pbl_phys(&p_rx->rcq_chain));

	p_ramrod->drop_ttl0_flg = p_ll2_conn->rx_drop_ttl0_flg;
	p_ramrod->inner_vlan_removal_en = p_ll2_conn->rx_vlan_removal_en;
	p_ramrod->queue_id = p_ll2_conn->queue_id;
	p_ramrod->main_func_queue = 1;

	if ((IS_MF_DEFAULT(p_hwfn) || IS_MF_SI(p_hwfn)) &&
	    p_ramrod->main_func_queue && (conn_type != QED_LL2_TYPE_ROCE)) {
		p_ramrod->mf_si_bcast_accept_all = 1;
		p_ramrod->mf_si_mcast_accept_all = 1;
	} else {
		p_ramrod->mf_si_bcast_accept_all = 0;
		p_ramrod->mf_si_mcast_accept_all = 0;
	}

	p_ramrod->action_on_error.error_type = action_on_error;
	p_ramrod->gsi_offload_flag = p_ll2_conn->gsi_enable;
	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int qed_sp_ll2_tx_queue_start(struct qed_hwfn *p_hwfn,
				     struct qed_ll2_info *p_ll2_conn)
{
	enum qed_ll2_conn_type conn_type = p_ll2_conn->conn_type;
	struct qed_ll2_tx_queue *p_tx = &p_ll2_conn->tx_queue;
	struct core_tx_start_ramrod_data *p_ramrod = NULL;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	union qed_qm_pq_params pq_params;
	u16 pq_id = 0, pbl_size;
	int rc = -EINVAL;

	if (!QED_LL2_TX_REGISTERED(p_ll2_conn))
		return 0;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = p_ll2_conn->cid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 CORE_RAMROD_TX_QUEUE_START,
				 PROTOCOLID_CORE, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.core_tx_queue_start;

	p_ramrod->sb_id = cpu_to_le16(qed_int_get_sp_sb_id(p_hwfn));
	p_ramrod->sb_index = p_tx->tx_sb_index;
	p_ramrod->mtu = cpu_to_le16(p_ll2_conn->mtu);
	p_ll2_conn->tx_stats_en = 1;
	p_ramrod->stats_en = p_ll2_conn->tx_stats_en;
	p_ramrod->stats_id = p_ll2_conn->tx_stats_id;

	DMA_REGPAIR_LE(p_ramrod->pbl_base_addr,
		       qed_chain_get_pbl_phys(&p_tx->txq_chain));
	pbl_size = qed_chain_get_page_cnt(&p_tx->txq_chain);
	p_ramrod->pbl_size = cpu_to_le16(pbl_size);

	memset(&pq_params, 0, sizeof(pq_params));
	pq_params.core.tc = p_ll2_conn->tx_tc;
	pq_id = qed_get_qm_pq(p_hwfn, PROTOCOLID_CORE, &pq_params);
	p_ramrod->qm_pq_id = cpu_to_le16(pq_id);

	switch (conn_type) {
	case QED_LL2_TYPE_ISCSI:
	case QED_LL2_TYPE_ISCSI_OOO:
		p_ramrod->conn_type = PROTOCOLID_ISCSI;
		break;
	case QED_LL2_TYPE_ROCE:
		p_ramrod->conn_type = PROTOCOLID_ROCE;
		break;
	default:
		p_ramrod->conn_type = PROTOCOLID_ETH;
		DP_NOTICE(p_hwfn, "Unknown connection type: %d\n", conn_type);
	}

	p_ramrod->gsi_offload_flag = p_ll2_conn->gsi_enable;
	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int qed_sp_ll2_rx_queue_stop(struct qed_hwfn *p_hwfn,
				    struct qed_ll2_info *p_ll2_conn)
{
	struct core_rx_stop_ramrod_data *p_ramrod = NULL;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = -EINVAL;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = p_ll2_conn->cid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 CORE_RAMROD_RX_QUEUE_STOP,
				 PROTOCOLID_CORE, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.core_rx_queue_stop;

	p_ramrod->complete_event_flg = 1;
	p_ramrod->queue_id = p_ll2_conn->queue_id;

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int qed_sp_ll2_tx_queue_stop(struct qed_hwfn *p_hwfn,
				    struct qed_ll2_info *p_ll2_conn)
{
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = -EINVAL;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = p_ll2_conn->cid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 CORE_RAMROD_TX_QUEUE_STOP,
				 PROTOCOLID_CORE, &init_data);
	if (rc)
		return rc;

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int
qed_ll2_acquire_connection_rx(struct qed_hwfn *p_hwfn,
			      struct qed_ll2_info *p_ll2_info, u16 rx_num_desc)
{
	struct qed_ll2_rx_packet *p_descq;
	u32 capacity;
	int rc = 0;

	if (!rx_num_desc)
		goto out;

	rc = qed_chain_alloc(p_hwfn->cdev,
			     QED_CHAIN_USE_TO_CONSUME_PRODUCE,
			     QED_CHAIN_MODE_NEXT_PTR,
			     QED_CHAIN_CNT_TYPE_U16,
			     rx_num_desc,
			     sizeof(struct core_rx_bd),
			     &p_ll2_info->rx_queue.rxq_chain);
	if (rc) {
		DP_NOTICE(p_hwfn, "Failed to allocate ll2 rxq chain\n");
		goto out;
	}

	capacity = qed_chain_get_capacity(&p_ll2_info->rx_queue.rxq_chain);
	p_descq = kcalloc(capacity, sizeof(struct qed_ll2_rx_packet),
			  GFP_KERNEL);
	if (!p_descq) {
		rc = -ENOMEM;
		DP_NOTICE(p_hwfn, "Failed to allocate ll2 Rx desc\n");
		goto out;
	}
	p_ll2_info->rx_queue.descq_array = p_descq;

	rc = qed_chain_alloc(p_hwfn->cdev,
			     QED_CHAIN_USE_TO_CONSUME_PRODUCE,
			     QED_CHAIN_MODE_PBL,
			     QED_CHAIN_CNT_TYPE_U16,
			     rx_num_desc,
			     sizeof(struct core_rx_fast_path_cqe),
			     &p_ll2_info->rx_queue.rcq_chain);
	if (rc) {
		DP_NOTICE(p_hwfn, "Failed to allocate ll2 rcq chain\n");
		goto out;
	}

	DP_VERBOSE(p_hwfn, QED_MSG_LL2,
		   "Allocated LL2 Rxq [Type %08x] with 0x%08x buffers\n",
		   p_ll2_info->conn_type, rx_num_desc);

out:
	return rc;
}

static int qed_ll2_acquire_connection_tx(struct qed_hwfn *p_hwfn,
					 struct qed_ll2_info *p_ll2_info,
					 u16 tx_num_desc)
{
	struct qed_ll2_tx_packet *p_descq;
	u32 capacity;
	int rc = 0;

	if (!tx_num_desc)
		goto out;

	rc = qed_chain_alloc(p_hwfn->cdev,
			     QED_CHAIN_USE_TO_CONSUME_PRODUCE,
			     QED_CHAIN_MODE_PBL,
			     QED_CHAIN_CNT_TYPE_U16,
			     tx_num_desc,
			     sizeof(struct core_tx_bd),
			     &p_ll2_info->tx_queue.txq_chain);
	if (rc)
		goto out;

	capacity = qed_chain_get_capacity(&p_ll2_info->tx_queue.txq_chain);
	p_descq = kcalloc(capacity, sizeof(struct qed_ll2_tx_packet),
			  GFP_KERNEL);
	if (!p_descq) {
		rc = -ENOMEM;
		goto out;
	}
	p_ll2_info->tx_queue.descq_array = p_descq;

	DP_VERBOSE(p_hwfn, QED_MSG_LL2,
		   "Allocated LL2 Txq [Type %08x] with 0x%08x buffers\n",
		   p_ll2_info->conn_type, tx_num_desc);

out:
	if (rc)
		DP_NOTICE(p_hwfn,
			  "Can't allocate memory for Tx LL2 with 0x%08x buffers\n",
			  tx_num_desc);
	return rc;
}

int qed_ll2_acquire_connection(struct qed_hwfn *p_hwfn,
			       struct qed_ll2_info *p_params,
			       u16 rx_num_desc,
			       u16 tx_num_desc,
			       u8 *p_connection_handle)
{
	qed_int_comp_cb_t comp_rx_cb, comp_tx_cb;
	struct qed_ll2_info *p_ll2_info = NULL;
	int rc;
	u8 i;

	if (!p_connection_handle || !p_hwfn->p_ll2_info)
		return -EINVAL;

	/* Find a free connection to be used */
	for (i = 0; (i < QED_MAX_NUM_OF_LL2_CONNECTIONS); i++) {
		mutex_lock(&p_hwfn->p_ll2_info[i].mutex);
		if (p_hwfn->p_ll2_info[i].b_active) {
			mutex_unlock(&p_hwfn->p_ll2_info[i].mutex);
			continue;
		}

		p_hwfn->p_ll2_info[i].b_active = true;
		p_ll2_info = &p_hwfn->p_ll2_info[i];
		mutex_unlock(&p_hwfn->p_ll2_info[i].mutex);
		break;
	}
	if (!p_ll2_info)
		return -EBUSY;

	p_ll2_info->conn_type = p_params->conn_type;
	p_ll2_info->mtu = p_params->mtu;
	p_ll2_info->rx_drop_ttl0_flg = p_params->rx_drop_ttl0_flg;
	p_ll2_info->rx_vlan_removal_en = p_params->rx_vlan_removal_en;
	p_ll2_info->tx_tc = p_params->tx_tc;
	p_ll2_info->tx_dest = p_params->tx_dest;
	p_ll2_info->ai_err_packet_too_big = p_params->ai_err_packet_too_big;
	p_ll2_info->ai_err_no_buf = p_params->ai_err_no_buf;
	p_ll2_info->gsi_enable = p_params->gsi_enable;

	rc = qed_ll2_acquire_connection_rx(p_hwfn, p_ll2_info, rx_num_desc);
	if (rc)
		goto q_allocate_fail;

	rc = qed_ll2_acquire_connection_tx(p_hwfn, p_ll2_info, tx_num_desc);
	if (rc)
		goto q_allocate_fail;

	/* Register callbacks for the Rx/Tx queues */
	comp_rx_cb = qed_ll2_rxq_completion;
	comp_tx_cb = qed_ll2_txq_completion;

	if (rx_num_desc) {
		qed_int_register_cb(p_hwfn, comp_rx_cb,
				    &p_hwfn->p_ll2_info[i],
				    &p_ll2_info->rx_queue.rx_sb_index,
				    &p_ll2_info->rx_queue.p_fw_cons);
		p_ll2_info->rx_queue.b_cb_registred = true;
	}

	if (tx_num_desc) {
		qed_int_register_cb(p_hwfn,
				    comp_tx_cb,
				    &p_hwfn->p_ll2_info[i],
				    &p_ll2_info->tx_queue.tx_sb_index,
				    &p_ll2_info->tx_queue.p_fw_cons);
		p_ll2_info->tx_queue.b_cb_registred = true;
	}

	*p_connection_handle = i;
	return rc;

q_allocate_fail:
	qed_ll2_release_connection(p_hwfn, i);
	return -ENOMEM;
}

static int qed_ll2_establish_connection_rx(struct qed_hwfn *p_hwfn,
					   struct qed_ll2_info *p_ll2_conn)
{
	u8 action_on_error = 0;

	if (!QED_LL2_RX_REGISTERED(p_ll2_conn))
		return 0;

	DIRECT_REG_WR(p_ll2_conn->rx_queue.set_prod_addr, 0x0);

	SET_FIELD(action_on_error,
		  CORE_RX_ACTION_ON_ERROR_PACKET_TOO_BIG,
		  p_ll2_conn->ai_err_packet_too_big);
	SET_FIELD(action_on_error,
		  CORE_RX_ACTION_ON_ERROR_NO_BUFF, p_ll2_conn->ai_err_no_buf);

	return qed_sp_ll2_rx_queue_start(p_hwfn, p_ll2_conn, action_on_error);
}

int qed_ll2_establish_connection(struct qed_hwfn *p_hwfn, u8 connection_handle)
{
	struct qed_ll2_info *p_ll2_conn;
	struct qed_ll2_rx_queue *p_rx;
	struct qed_ll2_tx_queue *p_tx;
	int rc = -EINVAL;
	u32 i, capacity;
	u8 qid;

	p_ll2_conn = qed_ll2_handle_sanity_lock(p_hwfn, connection_handle);
	if (!p_ll2_conn)
		return -EINVAL;
	p_rx = &p_ll2_conn->rx_queue;
	p_tx = &p_ll2_conn->tx_queue;

	qed_chain_reset(&p_rx->rxq_chain);
	qed_chain_reset(&p_rx->rcq_chain);
	INIT_LIST_HEAD(&p_rx->active_descq);
	INIT_LIST_HEAD(&p_rx->free_descq);
	INIT_LIST_HEAD(&p_rx->posting_descq);
	spin_lock_init(&p_rx->lock);
	capacity = qed_chain_get_capacity(&p_rx->rxq_chain);
	for (i = 0; i < capacity; i++)
		list_add_tail(&p_rx->descq_array[i].list_entry,
			      &p_rx->free_descq);
	*p_rx->p_fw_cons = 0;

	qed_chain_reset(&p_tx->txq_chain);
	INIT_LIST_HEAD(&p_tx->active_descq);
	INIT_LIST_HEAD(&p_tx->free_descq);
	INIT_LIST_HEAD(&p_tx->sending_descq);
	spin_lock_init(&p_tx->lock);
	capacity = qed_chain_get_capacity(&p_tx->txq_chain);
	for (i = 0; i < capacity; i++)
		list_add_tail(&p_tx->descq_array[i].list_entry,
			      &p_tx->free_descq);
	p_tx->cur_completing_bd_idx = 0;
	p_tx->bds_idx = 0;
	p_tx->b_completing_packet = false;
	p_tx->cur_send_packet = NULL;
	p_tx->cur_send_frag_num = 0;
	p_tx->cur_completing_frag_num = 0;
	*p_tx->p_fw_cons = 0;

	qed_cxt_acquire_cid(p_hwfn, PROTOCOLID_CORE, &p_ll2_conn->cid);

	qid = p_hwfn->hw_info.resc_start[QED_LL2_QUEUE] + connection_handle;
	p_ll2_conn->queue_id = qid;
	p_ll2_conn->tx_stats_id = qid;
	p_rx->set_prod_addr = (u8 __iomem *)p_hwfn->regview +
					    GTT_BAR0_MAP_REG_TSDM_RAM +
					    TSTORM_LL2_RX_PRODS_OFFSET(qid);
	p_tx->doorbell_addr = (u8 __iomem *)p_hwfn->doorbells +
					    qed_db_addr(p_ll2_conn->cid,
							DQ_DEMS_LEGACY);

	rc = qed_ll2_establish_connection_rx(p_hwfn, p_ll2_conn);
	if (rc)
		return rc;

	rc = qed_sp_ll2_tx_queue_start(p_hwfn, p_ll2_conn);
	if (rc)
		return rc;

	if (p_hwfn->hw_info.personality != QED_PCI_ETH_ROCE)
		qed_wr(p_hwfn, p_hwfn->p_main_ptt, PRS_REG_USE_LIGHT_L2, 1);

	return rc;
}

static void qed_ll2_post_rx_buffer_notify_fw(struct qed_hwfn *p_hwfn,
					     struct qed_ll2_rx_queue *p_rx,
					     struct qed_ll2_rx_packet *p_curp)
{
	struct qed_ll2_rx_packet *p_posting_packet = NULL;
	struct core_ll2_rx_prod rx_prod = { 0, 0, 0 };
	bool b_notify_fw = false;
	u16 bd_prod, cq_prod;

	/* This handles the flushing of already posted buffers */
	while (!list_empty(&p_rx->posting_descq)) {
		p_posting_packet = list_first_entry(&p_rx->posting_descq,
						    struct qed_ll2_rx_packet,
						    list_entry);
		list_del(&p_posting_packet->list_entry);
		list_add_tail(&p_posting_packet->list_entry,
			      &p_rx->active_descq);
		b_notify_fw = true;
	}

	/* This handles the supplied packet [if there is one] */
	if (p_curp) {
		list_add_tail(&p_curp->list_entry, &p_rx->active_descq);
		b_notify_fw = true;
	}

	if (!b_notify_fw)
		return;

	bd_prod = qed_chain_get_prod_idx(&p_rx->rxq_chain);
	cq_prod = qed_chain_get_prod_idx(&p_rx->rcq_chain);
	rx_prod.bd_prod = cpu_to_le16(bd_prod);
	rx_prod.cqe_prod = cpu_to_le16(cq_prod);
	DIRECT_REG_WR(p_rx->set_prod_addr, *((u32 *)&rx_prod));
}

int qed_ll2_post_rx_buffer(struct qed_hwfn *p_hwfn,
			   u8 connection_handle,
			   dma_addr_t addr,
			   u16 buf_len, void *cookie, u8 notify_fw)
{
	struct core_rx_bd_with_buff_len *p_curb = NULL;
	struct qed_ll2_rx_packet *p_curp = NULL;
	struct qed_ll2_info *p_ll2_conn;
	struct qed_ll2_rx_queue *p_rx;
	unsigned long flags;
	void *p_data;
	int rc = 0;

	p_ll2_conn = qed_ll2_handle_sanity(p_hwfn, connection_handle);
	if (!p_ll2_conn)
		return -EINVAL;
	p_rx = &p_ll2_conn->rx_queue;

	spin_lock_irqsave(&p_rx->lock, flags);
	if (!list_empty(&p_rx->free_descq))
		p_curp = list_first_entry(&p_rx->free_descq,
					  struct qed_ll2_rx_packet, list_entry);
	if (p_curp) {
		if (qed_chain_get_elem_left(&p_rx->rxq_chain) &&
		    qed_chain_get_elem_left(&p_rx->rcq_chain)) {
			p_data = qed_chain_produce(&p_rx->rxq_chain);
			p_curb = (struct core_rx_bd_with_buff_len *)p_data;
			qed_chain_produce(&p_rx->rcq_chain);
		}
	}

	/* If we're lacking entires, let's try to flush buffers to FW */
	if (!p_curp || !p_curb) {
		rc = -EBUSY;
		p_curp = NULL;
		goto out_notify;
	}

	/* We have an Rx packet we can fill */
	DMA_REGPAIR_LE(p_curb->addr, addr);
	p_curb->buff_length = cpu_to_le16(buf_len);
	p_curp->rx_buf_addr = addr;
	p_curp->cookie = cookie;
	p_curp->rxq_bd = p_curb;
	p_curp->buf_length = buf_len;
	list_del(&p_curp->list_entry);

	/* Check if we only want to enqueue this packet without informing FW */
	if (!notify_fw) {
		list_add_tail(&p_curp->list_entry, &p_rx->posting_descq);
		goto out;
	}

out_notify:
	qed_ll2_post_rx_buffer_notify_fw(p_hwfn, p_rx, p_curp);
out:
	spin_unlock_irqrestore(&p_rx->lock, flags);
	return rc;
}

static void qed_ll2_prepare_tx_packet_set(struct qed_hwfn *p_hwfn,
					  struct qed_ll2_tx_queue *p_tx,
					  struct qed_ll2_tx_packet *p_curp,
					  u8 num_of_bds,
					  dma_addr_t first_frag,
					  u16 first_frag_len, void *p_cookie,
					  u8 notify_fw)
{
	list_del(&p_curp->list_entry);
	p_curp->cookie = p_cookie;
	p_curp->bd_used = num_of_bds;
	p_curp->notify_fw = notify_fw;
	p_tx->cur_send_packet = p_curp;
	p_tx->cur_send_frag_num = 0;

	p_curp->bds_set[p_tx->cur_send_frag_num].tx_frag = first_frag;
	p_curp->bds_set[p_tx->cur_send_frag_num].frag_len = first_frag_len;
	p_tx->cur_send_frag_num++;
}

static void qed_ll2_prepare_tx_packet_set_bd(struct qed_hwfn *p_hwfn,
					     struct qed_ll2_info *p_ll2,
					     struct qed_ll2_tx_packet *p_curp,
					     u8 num_of_bds,
					     enum core_tx_dest tx_dest,
					     u16 vlan,
					     u8 bd_flags,
					     u16 l4_hdr_offset_w,
					     enum core_roce_flavor_type type,
					     dma_addr_t first_frag,
					     u16 first_frag_len)
{
	struct qed_chain *p_tx_chain = &p_ll2->tx_queue.txq_chain;
	u16 prod_idx = qed_chain_get_prod_idx(p_tx_chain);
	struct core_tx_bd *start_bd = NULL;
	u16 frag_idx;

	start_bd = (struct core_tx_bd *)qed_chain_produce(p_tx_chain);
	start_bd->nw_vlan_or_lb_echo = cpu_to_le16(vlan);
	SET_FIELD(start_bd->bitfield1, CORE_TX_BD_L4_HDR_OFFSET_W,
		  cpu_to_le16(l4_hdr_offset_w));
	SET_FIELD(start_bd->bitfield1, CORE_TX_BD_TX_DST, tx_dest);
	start_bd->bd_flags.as_bitfield = bd_flags;
	start_bd->bd_flags.as_bitfield |= CORE_TX_BD_FLAGS_START_BD_MASK <<
	    CORE_TX_BD_FLAGS_START_BD_SHIFT;
	SET_FIELD(start_bd->bitfield0, CORE_TX_BD_NBDS, num_of_bds);
	DMA_REGPAIR_LE(start_bd->addr, first_frag);
	start_bd->nbytes = cpu_to_le16(first_frag_len);

	SET_FIELD(start_bd->bd_flags.as_bitfield, CORE_TX_BD_FLAGS_ROCE_FLAV,
		  type);

	DP_VERBOSE(p_hwfn,
		   (NETIF_MSG_TX_QUEUED | QED_MSG_LL2),
		   "LL2 [q 0x%02x cid 0x%08x type 0x%08x] Tx Producer at [0x%04x] - set with a %04x bytes %02x BDs buffer at %08x:%08x\n",
		   p_ll2->queue_id,
		   p_ll2->cid,
		   p_ll2->conn_type,
		   prod_idx,
		   first_frag_len,
		   num_of_bds,
		   le32_to_cpu(start_bd->addr.hi),
		   le32_to_cpu(start_bd->addr.lo));

	if (p_ll2->tx_queue.cur_send_frag_num == num_of_bds)
		return;

	/* Need to provide the packet with additional BDs for frags */
	for (frag_idx = p_ll2->tx_queue.cur_send_frag_num;
	     frag_idx < num_of_bds; frag_idx++) {
		struct core_tx_bd **p_bd = &p_curp->bds_set[frag_idx].txq_bd;

		*p_bd = (struct core_tx_bd *)qed_chain_produce(p_tx_chain);
		(*p_bd)->bd_flags.as_bitfield = 0;
		(*p_bd)->bitfield1 = 0;
		(*p_bd)->bitfield0 = 0;
		p_curp->bds_set[frag_idx].tx_frag = 0;
		p_curp->bds_set[frag_idx].frag_len = 0;
	}
}

/* This should be called while the Txq spinlock is being held */
static void qed_ll2_tx_packet_notify(struct qed_hwfn *p_hwfn,
				     struct qed_ll2_info *p_ll2_conn)
{
	bool b_notify = p_ll2_conn->tx_queue.cur_send_packet->notify_fw;
	struct qed_ll2_tx_queue *p_tx = &p_ll2_conn->tx_queue;
	struct qed_ll2_tx_packet *p_pkt = NULL;
	struct core_db_data db_msg = { 0, 0, 0 };
	u16 bd_prod;

	/* If there are missing BDs, don't do anything now */
	if (p_ll2_conn->tx_queue.cur_send_frag_num !=
	    p_ll2_conn->tx_queue.cur_send_packet->bd_used)
		return;

	/* Push the current packet to the list and clean after it */
	list_add_tail(&p_ll2_conn->tx_queue.cur_send_packet->list_entry,
		      &p_ll2_conn->tx_queue.sending_descq);
	p_ll2_conn->tx_queue.cur_send_packet = NULL;
	p_ll2_conn->tx_queue.cur_send_frag_num = 0;

	/* Notify FW of packet only if requested to */
	if (!b_notify)
		return;

	bd_prod = qed_chain_get_prod_idx(&p_ll2_conn->tx_queue.txq_chain);

	while (!list_empty(&p_tx->sending_descq)) {
		p_pkt = list_first_entry(&p_tx->sending_descq,
					 struct qed_ll2_tx_packet, list_entry);
		if (!p_pkt)
			break;

		list_del(&p_pkt->list_entry);
		list_add_tail(&p_pkt->list_entry, &p_tx->active_descq);
	}

	SET_FIELD(db_msg.params, CORE_DB_DATA_DEST, DB_DEST_XCM);
	SET_FIELD(db_msg.params, CORE_DB_DATA_AGG_CMD, DB_AGG_CMD_SET);
	SET_FIELD(db_msg.params, CORE_DB_DATA_AGG_VAL_SEL,
		  DQ_XCM_CORE_TX_BD_PROD_CMD);
	db_msg.agg_flags = DQ_XCM_CORE_DQ_CF_CMD;
	db_msg.spq_prod = cpu_to_le16(bd_prod);

	/* Make sure the BDs data is updated before ringing the doorbell */
	wmb();

	DIRECT_REG_WR(p_tx->doorbell_addr, *((u32 *)&db_msg));

	DP_VERBOSE(p_hwfn,
		   (NETIF_MSG_TX_QUEUED | QED_MSG_LL2),
		   "LL2 [q 0x%02x cid 0x%08x type 0x%08x] Doorbelled [producer 0x%04x]\n",
		   p_ll2_conn->queue_id,
		   p_ll2_conn->cid, p_ll2_conn->conn_type, db_msg.spq_prod);
}

int qed_ll2_prepare_tx_packet(struct qed_hwfn *p_hwfn,
			      u8 connection_handle,
			      u8 num_of_bds,
			      u16 vlan,
			      u8 bd_flags,
			      u16 l4_hdr_offset_w,
			      enum qed_ll2_roce_flavor_type qed_roce_flavor,
			      dma_addr_t first_frag,
			      u16 first_frag_len, void *cookie, u8 notify_fw)
{
	struct qed_ll2_tx_packet *p_curp = NULL;
	struct qed_ll2_info *p_ll2_conn = NULL;
	enum core_roce_flavor_type roce_flavor;
	struct qed_ll2_tx_queue *p_tx;
	struct qed_chain *p_tx_chain;
	unsigned long flags;
	int rc = 0;

	p_ll2_conn = qed_ll2_handle_sanity(p_hwfn, connection_handle);
	if (!p_ll2_conn)
		return -EINVAL;
	p_tx = &p_ll2_conn->tx_queue;
	p_tx_chain = &p_tx->txq_chain;

	if (num_of_bds > CORE_LL2_TX_MAX_BDS_PER_PACKET)
		return -EIO;

	spin_lock_irqsave(&p_tx->lock, flags);
	if (p_tx->cur_send_packet) {
		rc = -EEXIST;
		goto out;
	}

	/* Get entry, but only if we have tx elements for it */
	if (!list_empty(&p_tx->free_descq))
		p_curp = list_first_entry(&p_tx->free_descq,
					  struct qed_ll2_tx_packet, list_entry);
	if (p_curp && qed_chain_get_elem_left(p_tx_chain) < num_of_bds)
		p_curp = NULL;

	if (!p_curp) {
		rc = -EBUSY;
		goto out;
	}

	if (qed_roce_flavor == QED_LL2_ROCE) {
		roce_flavor = CORE_ROCE;
	} else if (qed_roce_flavor == QED_LL2_RROCE) {
		roce_flavor = CORE_RROCE;
	} else {
		rc = -EINVAL;
		goto out;
	}

	/* Prepare packet and BD, and perhaps send a doorbell to FW */
	qed_ll2_prepare_tx_packet_set(p_hwfn, p_tx, p_curp,
				      num_of_bds, first_frag,
				      first_frag_len, cookie, notify_fw);
	qed_ll2_prepare_tx_packet_set_bd(p_hwfn, p_ll2_conn, p_curp,
					 num_of_bds, CORE_TX_DEST_NW,
					 vlan, bd_flags, l4_hdr_offset_w,
					 roce_flavor,
					 first_frag, first_frag_len);

	qed_ll2_tx_packet_notify(p_hwfn, p_ll2_conn);

out:
	spin_unlock_irqrestore(&p_tx->lock, flags);
	return rc;
}

int qed_ll2_set_fragment_of_tx_packet(struct qed_hwfn *p_hwfn,
				      u8 connection_handle,
				      dma_addr_t addr, u16 nbytes)
{
	struct qed_ll2_tx_packet *p_cur_send_packet = NULL;
	struct qed_ll2_info *p_ll2_conn = NULL;
	u16 cur_send_frag_num = 0;
	struct core_tx_bd *p_bd;
	unsigned long flags;

	p_ll2_conn = qed_ll2_handle_sanity(p_hwfn, connection_handle);
	if (!p_ll2_conn)
		return -EINVAL;

	if (!p_ll2_conn->tx_queue.cur_send_packet)
		return -EINVAL;

	p_cur_send_packet = p_ll2_conn->tx_queue.cur_send_packet;
	cur_send_frag_num = p_ll2_conn->tx_queue.cur_send_frag_num;

	if (cur_send_frag_num >= p_cur_send_packet->bd_used)
		return -EINVAL;

	/* Fill the BD information, and possibly notify FW */
	p_bd = p_cur_send_packet->bds_set[cur_send_frag_num].txq_bd;
	DMA_REGPAIR_LE(p_bd->addr, addr);
	p_bd->nbytes = cpu_to_le16(nbytes);
	p_cur_send_packet->bds_set[cur_send_frag_num].tx_frag = addr;
	p_cur_send_packet->bds_set[cur_send_frag_num].frag_len = nbytes;

	p_ll2_conn->tx_queue.cur_send_frag_num++;

	spin_lock_irqsave(&p_ll2_conn->tx_queue.lock, flags);
	qed_ll2_tx_packet_notify(p_hwfn, p_ll2_conn);
	spin_unlock_irqrestore(&p_ll2_conn->tx_queue.lock, flags);

	return 0;
}

int qed_ll2_terminate_connection(struct qed_hwfn *p_hwfn, u8 connection_handle)
{
	struct qed_ll2_info *p_ll2_conn = NULL;
	int rc = -EINVAL;

	p_ll2_conn = qed_ll2_handle_sanity_lock(p_hwfn, connection_handle);
	if (!p_ll2_conn)
		return -EINVAL;

	/* Stop Tx & Rx of connection, if needed */
	if (QED_LL2_TX_REGISTERED(p_ll2_conn)) {
		rc = qed_sp_ll2_tx_queue_stop(p_hwfn, p_ll2_conn);
		if (rc)
			return rc;
		qed_ll2_txq_flush(p_hwfn, connection_handle);
	}

	if (QED_LL2_RX_REGISTERED(p_ll2_conn)) {
		rc = qed_sp_ll2_rx_queue_stop(p_hwfn, p_ll2_conn);
		if (rc)
			return rc;
		qed_ll2_rxq_flush(p_hwfn, connection_handle);
	}

	return rc;
}

void qed_ll2_release_connection(struct qed_hwfn *p_hwfn, u8 connection_handle)
{
	struct qed_ll2_info *p_ll2_conn = NULL;

	p_ll2_conn = qed_ll2_handle_sanity(p_hwfn, connection_handle);
	if (!p_ll2_conn)
		return;

	if (QED_LL2_RX_REGISTERED(p_ll2_conn)) {
		p_ll2_conn->rx_queue.b_cb_registred = false;
		qed_int_unregister_cb(p_hwfn, p_ll2_conn->rx_queue.rx_sb_index);
	}

	if (QED_LL2_TX_REGISTERED(p_ll2_conn)) {
		p_ll2_conn->tx_queue.b_cb_registred = false;
		qed_int_unregister_cb(p_hwfn, p_ll2_conn->tx_queue.tx_sb_index);
	}

	kfree(p_ll2_conn->tx_queue.descq_array);
	qed_chain_free(p_hwfn->cdev, &p_ll2_conn->tx_queue.txq_chain);

	kfree(p_ll2_conn->rx_queue.descq_array);
	qed_chain_free(p_hwfn->cdev, &p_ll2_conn->rx_queue.rxq_chain);
	qed_chain_free(p_hwfn->cdev, &p_ll2_conn->rx_queue.rcq_chain);

	qed_cxt_release_cid(p_hwfn, p_ll2_conn->cid);

	mutex_lock(&p_ll2_conn->mutex);
	p_ll2_conn->b_active = false;
	mutex_unlock(&p_ll2_conn->mutex);
}

struct qed_ll2_info *qed_ll2_alloc(struct qed_hwfn *p_hwfn)
{
	struct qed_ll2_info *p_ll2_connections;
	u8 i;

	/* Allocate LL2's set struct */
	p_ll2_connections = kcalloc(QED_MAX_NUM_OF_LL2_CONNECTIONS,
				    sizeof(struct qed_ll2_info), GFP_KERNEL);
	if (!p_ll2_connections) {
		DP_NOTICE(p_hwfn, "Failed to allocate `struct qed_ll2'\n");
		return NULL;
	}

	for (i = 0; i < QED_MAX_NUM_OF_LL2_CONNECTIONS; i++)
		p_ll2_connections[i].my_id = i;

	return p_ll2_connections;
}

void qed_ll2_setup(struct qed_hwfn *p_hwfn,
		   struct qed_ll2_info *p_ll2_connections)
{
	int i;

	for (i = 0; i < QED_MAX_NUM_OF_LL2_CONNECTIONS; i++)
		mutex_init(&p_ll2_connections[i].mutex);
}

void qed_ll2_free(struct qed_hwfn *p_hwfn,
		  struct qed_ll2_info *p_ll2_connections)
{
	kfree(p_ll2_connections);
}

static void _qed_ll2_get_tstats(struct qed_hwfn *p_hwfn,
				struct qed_ptt *p_ptt,
				struct qed_ll2_info *p_ll2_conn,
				struct qed_ll2_stats *p_stats)
{
	struct core_ll2_tstorm_per_queue_stat tstats;
	u8 qid = p_ll2_conn->queue_id;
	u32 tstats_addr;

	memset(&tstats, 0, sizeof(tstats));
	tstats_addr = BAR0_MAP_REG_TSDM_RAM +
		      CORE_LL2_TSTORM_PER_QUEUE_STAT_OFFSET(qid);
	qed_memcpy_from(p_hwfn, p_ptt, &tstats, tstats_addr, sizeof(tstats));

	p_stats->packet_too_big_discard =
			HILO_64_REGPAIR(tstats.packet_too_big_discard);
	p_stats->no_buff_discard = HILO_64_REGPAIR(tstats.no_buff_discard);
}

static void _qed_ll2_get_ustats(struct qed_hwfn *p_hwfn,
				struct qed_ptt *p_ptt,
				struct qed_ll2_info *p_ll2_conn,
				struct qed_ll2_stats *p_stats)
{
	struct core_ll2_ustorm_per_queue_stat ustats;
	u8 qid = p_ll2_conn->queue_id;
	u32 ustats_addr;

	memset(&ustats, 0, sizeof(ustats));
	ustats_addr = BAR0_MAP_REG_USDM_RAM +
		      CORE_LL2_USTORM_PER_QUEUE_STAT_OFFSET(qid);
	qed_memcpy_from(p_hwfn, p_ptt, &ustats, ustats_addr, sizeof(ustats));

	p_stats->rcv_ucast_bytes = HILO_64_REGPAIR(ustats.rcv_ucast_bytes);
	p_stats->rcv_mcast_bytes = HILO_64_REGPAIR(ustats.rcv_mcast_bytes);
	p_stats->rcv_bcast_bytes = HILO_64_REGPAIR(ustats.rcv_bcast_bytes);
	p_stats->rcv_ucast_pkts = HILO_64_REGPAIR(ustats.rcv_ucast_pkts);
	p_stats->rcv_mcast_pkts = HILO_64_REGPAIR(ustats.rcv_mcast_pkts);
	p_stats->rcv_bcast_pkts = HILO_64_REGPAIR(ustats.rcv_bcast_pkts);
}

static void _qed_ll2_get_pstats(struct qed_hwfn *p_hwfn,
				struct qed_ptt *p_ptt,
				struct qed_ll2_info *p_ll2_conn,
				struct qed_ll2_stats *p_stats)
{
	struct core_ll2_pstorm_per_queue_stat pstats;
	u8 stats_id = p_ll2_conn->tx_stats_id;
	u32 pstats_addr;

	memset(&pstats, 0, sizeof(pstats));
	pstats_addr = BAR0_MAP_REG_PSDM_RAM +
		      CORE_LL2_PSTORM_PER_QUEUE_STAT_OFFSET(stats_id);
	qed_memcpy_from(p_hwfn, p_ptt, &pstats, pstats_addr, sizeof(pstats));

	p_stats->sent_ucast_bytes = HILO_64_REGPAIR(pstats.sent_ucast_bytes);
	p_stats->sent_mcast_bytes = HILO_64_REGPAIR(pstats.sent_mcast_bytes);
	p_stats->sent_bcast_bytes = HILO_64_REGPAIR(pstats.sent_bcast_bytes);
	p_stats->sent_ucast_pkts = HILO_64_REGPAIR(pstats.sent_ucast_pkts);
	p_stats->sent_mcast_pkts = HILO_64_REGPAIR(pstats.sent_mcast_pkts);
	p_stats->sent_bcast_pkts = HILO_64_REGPAIR(pstats.sent_bcast_pkts);
}

int qed_ll2_get_stats(struct qed_hwfn *p_hwfn,
		      u8 connection_handle, struct qed_ll2_stats *p_stats)
{
	struct qed_ll2_info *p_ll2_conn = NULL;
	struct qed_ptt *p_ptt;

	memset(p_stats, 0, sizeof(*p_stats));

	if ((connection_handle >= QED_MAX_NUM_OF_LL2_CONNECTIONS) ||
	    !p_hwfn->p_ll2_info)
		return -EINVAL;

	p_ll2_conn = &p_hwfn->p_ll2_info[connection_handle];

	p_ptt = qed_ptt_acquire(p_hwfn);
	if (!p_ptt) {
		DP_ERR(p_hwfn, "Failed to acquire ptt\n");
		return -EINVAL;
	}

	_qed_ll2_get_tstats(p_hwfn, p_ptt, p_ll2_conn, p_stats);
	_qed_ll2_get_ustats(p_hwfn, p_ptt, p_ll2_conn, p_stats);
	if (p_ll2_conn->tx_stats_en)
		_qed_ll2_get_pstats(p_hwfn, p_ptt, p_ll2_conn, p_stats);

	qed_ptt_release(p_hwfn, p_ptt);
	return 0;
}

static void qed_ll2_register_cb_ops(struct qed_dev *cdev,
				    const struct qed_ll2_cb_ops *ops,
				    void *cookie)
{
	cdev->ll2->cbs = ops;
	cdev->ll2->cb_cookie = cookie;
}

static int qed_ll2_start(struct qed_dev *cdev, struct qed_ll2_params *params)
{
	struct qed_ll2_info ll2_info;
	struct qed_ll2_buffer *buffer;
	enum qed_ll2_conn_type conn_type;
	struct qed_ptt *p_ptt;
	int rc, i;

	/* Initialize LL2 locks & lists */
	INIT_LIST_HEAD(&cdev->ll2->list);
	spin_lock_init(&cdev->ll2->lock);
	cdev->ll2->rx_size = NET_SKB_PAD + ETH_HLEN +
			     L1_CACHE_BYTES + params->mtu;
	cdev->ll2->frags_mapped = params->frags_mapped;

	/*Allocate memory for LL2 */
	DP_INFO(cdev, "Allocating LL2 buffers of size %08x bytes\n",
		cdev->ll2->rx_size);
	for (i = 0; i < QED_LL2_RX_SIZE; i++) {
		buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
		if (!buffer) {
			DP_INFO(cdev, "Failed to allocate LL2 buffers\n");
			goto fail;
		}

		rc = qed_ll2_alloc_buffer(cdev, (u8 **)&buffer->data,
					  &buffer->phys_addr);
		if (rc) {
			kfree(buffer);
			goto fail;
		}

		list_add_tail(&buffer->list, &cdev->ll2->list);
	}

	switch (QED_LEADING_HWFN(cdev)->hw_info.personality) {
	case QED_PCI_ISCSI:
		conn_type = QED_LL2_TYPE_ISCSI;
		break;
	case QED_PCI_ETH_ROCE:
		conn_type = QED_LL2_TYPE_ROCE;
		break;
	default:
		conn_type = QED_LL2_TYPE_TEST;
	}

	/* Prepare the temporary ll2 information */
	memset(&ll2_info, 0, sizeof(ll2_info));
	ll2_info.conn_type = conn_type;
	ll2_info.mtu = params->mtu;
	ll2_info.rx_drop_ttl0_flg = params->drop_ttl0_packets;
	ll2_info.rx_vlan_removal_en = params->rx_vlan_stripping;
	ll2_info.tx_tc = 0;
	ll2_info.tx_dest = CORE_TX_DEST_NW;
	ll2_info.gsi_enable = 1;

	rc = qed_ll2_acquire_connection(QED_LEADING_HWFN(cdev), &ll2_info,
					QED_LL2_RX_SIZE, QED_LL2_TX_SIZE,
					&cdev->ll2->handle);
	if (rc) {
		DP_INFO(cdev, "Failed to acquire LL2 connection\n");
		goto fail;
	}

	rc = qed_ll2_establish_connection(QED_LEADING_HWFN(cdev),
					  cdev->ll2->handle);
	if (rc) {
		DP_INFO(cdev, "Failed to establish LL2 connection\n");
		goto release_fail;
	}

	/* Post all Rx buffers to FW */
	spin_lock_bh(&cdev->ll2->lock);
	list_for_each_entry(buffer, &cdev->ll2->list, list) {
		rc = qed_ll2_post_rx_buffer(QED_LEADING_HWFN(cdev),
					    cdev->ll2->handle,
					    buffer->phys_addr, 0, buffer, 1);
		if (rc) {
			DP_INFO(cdev,
				"Failed to post an Rx buffer; Deleting it\n");
			dma_unmap_single(&cdev->pdev->dev, buffer->phys_addr,
					 cdev->ll2->rx_size, DMA_FROM_DEVICE);
			kfree(buffer->data);
			list_del(&buffer->list);
			kfree(buffer);
		} else {
			cdev->ll2->rx_cnt++;
		}
	}
	spin_unlock_bh(&cdev->ll2->lock);

	if (!cdev->ll2->rx_cnt) {
		DP_INFO(cdev, "Failed passing even a single Rx buffer\n");
		goto release_terminate;
	}

	if (!is_valid_ether_addr(params->ll2_mac_address)) {
		DP_INFO(cdev, "Invalid Ethernet address\n");
		goto release_terminate;
	}

	p_ptt = qed_ptt_acquire(QED_LEADING_HWFN(cdev));
	if (!p_ptt) {
		DP_INFO(cdev, "Failed to acquire PTT\n");
		goto release_terminate;
	}

	rc = qed_llh_add_mac_filter(QED_LEADING_HWFN(cdev), p_ptt,
				    params->ll2_mac_address);
	qed_ptt_release(QED_LEADING_HWFN(cdev), p_ptt);
	if (rc) {
		DP_ERR(cdev, "Failed to allocate LLH filter\n");
		goto release_terminate_all;
	}

	ether_addr_copy(cdev->ll2_mac_address, params->ll2_mac_address);

	return 0;

release_terminate_all:

release_terminate:
	qed_ll2_terminate_connection(QED_LEADING_HWFN(cdev), cdev->ll2->handle);
release_fail:
	qed_ll2_release_connection(QED_LEADING_HWFN(cdev), cdev->ll2->handle);
fail:
	qed_ll2_kill_buffers(cdev);
	cdev->ll2->handle = QED_LL2_UNUSED_HANDLE;
	return -EINVAL;
}

static int qed_ll2_stop(struct qed_dev *cdev)
{
	struct qed_ptt *p_ptt;
	int rc;

	if (cdev->ll2->handle == QED_LL2_UNUSED_HANDLE)
		return 0;

	p_ptt = qed_ptt_acquire(QED_LEADING_HWFN(cdev));
	if (!p_ptt) {
		DP_INFO(cdev, "Failed to acquire PTT\n");
		goto fail;
	}

	qed_llh_remove_mac_filter(QED_LEADING_HWFN(cdev), p_ptt,
				  cdev->ll2_mac_address);
	qed_ptt_release(QED_LEADING_HWFN(cdev), p_ptt);
	eth_zero_addr(cdev->ll2_mac_address);

	rc = qed_ll2_terminate_connection(QED_LEADING_HWFN(cdev),
					  cdev->ll2->handle);
	if (rc)
		DP_INFO(cdev, "Failed to terminate LL2 connection\n");

	qed_ll2_kill_buffers(cdev);

	qed_ll2_release_connection(QED_LEADING_HWFN(cdev), cdev->ll2->handle);
	cdev->ll2->handle = QED_LL2_UNUSED_HANDLE;

	return rc;
fail:
	return -EINVAL;
}

static int qed_ll2_start_xmit(struct qed_dev *cdev, struct sk_buff *skb)
{
	const skb_frag_t *frag;
	int rc = -EINVAL, i;
	dma_addr_t mapping;
	u16 vlan = 0;
	u8 flags = 0;

	if (unlikely(skb->ip_summed != CHECKSUM_NONE)) {
		DP_INFO(cdev, "Cannot transmit a checksumed packet\n");
		return -EINVAL;
	}

	if (1 + skb_shinfo(skb)->nr_frags > CORE_LL2_TX_MAX_BDS_PER_PACKET) {
		DP_ERR(cdev, "Cannot transmit a packet with %d fragments\n",
		       1 + skb_shinfo(skb)->nr_frags);
		return -EINVAL;
	}

	mapping = dma_map_single(&cdev->pdev->dev, skb->data,
				 skb->len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(&cdev->pdev->dev, mapping))) {
		DP_NOTICE(cdev, "SKB mapping failed\n");
		return -EINVAL;
	}

	/* Request HW to calculate IP csum */
	if (!((vlan_get_protocol(skb) == htons(ETH_P_IPV6)) &&
	      ipv6_hdr(skb)->nexthdr == NEXTHDR_IPV6))
		flags |= BIT(CORE_TX_BD_FLAGS_IP_CSUM_SHIFT);

	if (skb_vlan_tag_present(skb)) {
		vlan = skb_vlan_tag_get(skb);
		flags |= BIT(CORE_TX_BD_FLAGS_VLAN_INSERTION_SHIFT);
	}

	rc = qed_ll2_prepare_tx_packet(QED_LEADING_HWFN(cdev),
				       cdev->ll2->handle,
				       1 + skb_shinfo(skb)->nr_frags,
				       vlan, flags, 0, 0 /* RoCE FLAVOR */,
				       mapping, skb->len, skb, 1);
	if (rc)
		goto err;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		frag = &skb_shinfo(skb)->frags[i];
		if (!cdev->ll2->frags_mapped) {
			mapping = skb_frag_dma_map(&cdev->pdev->dev, frag, 0,
						   skb_frag_size(frag),
						   DMA_TO_DEVICE);

			if (unlikely(dma_mapping_error(&cdev->pdev->dev,
						       mapping))) {
				DP_NOTICE(cdev,
					  "Unable to map frag - dropping packet\n");
				goto err;
			}
		} else {
			mapping = page_to_phys(skb_frag_page(frag)) |
			    frag->page_offset;
		}

		rc = qed_ll2_set_fragment_of_tx_packet(QED_LEADING_HWFN(cdev),
						       cdev->ll2->handle,
						       mapping,
						       skb_frag_size(frag));

		/* if failed not much to do here, partial packet has been posted
		 * we can't free memory, will need to wait for completion.
		 */
		if (rc)
			goto err2;
	}

	return 0;

err:
	dma_unmap_single(&cdev->pdev->dev, mapping, skb->len, DMA_TO_DEVICE);

err2:
	return rc;
}

static int qed_ll2_stats(struct qed_dev *cdev, struct qed_ll2_stats *stats)
{
	if (!cdev->ll2)
		return -EINVAL;

	return qed_ll2_get_stats(QED_LEADING_HWFN(cdev),
				 cdev->ll2->handle, stats);
}

const struct qed_ll2_ops qed_ll2_ops_pass = {
	.start = &qed_ll2_start,
	.stop = &qed_ll2_stop,
	.start_xmit = &qed_ll2_start_xmit,
	.register_cb_ops = &qed_ll2_register_cb_ops,
	.get_stats = &qed_ll2_stats,
};

int qed_ll2_alloc_if(struct qed_dev *cdev)
{
	cdev->ll2 = kzalloc(sizeof(*cdev->ll2), GFP_KERNEL);
	return cdev->ll2 ? 0 : -ENOMEM;
}

void qed_ll2_dealloc_if(struct qed_dev *cdev)
{
	kfree(cdev->ll2);
	cdev->ll2 = NULL;
}
