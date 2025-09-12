// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/if_vlan.h>
#include <linux/iopoll.h>
#include <net/ip6_checksum.h>
#include <net/ipv6.h>
#include <net/netdev_queues.h>

#include "hinic3_hwdev.h"
#include "hinic3_nic_cfg.h"
#include "hinic3_nic_dev.h"
#include "hinic3_nic_io.h"
#include "hinic3_tx.h"
#include "hinic3_wq.h"

#define MIN_SKB_LEN                32

int hinic3_alloc_txqs(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	u16 q_id, num_txqs = nic_dev->max_qps;
	struct pci_dev *pdev = nic_dev->pdev;
	struct hinic3_txq *txq;

	if (!num_txqs) {
		dev_err(hwdev->dev, "Cannot allocate zero size txqs\n");
		return -EINVAL;
	}

	nic_dev->txqs = kcalloc(num_txqs, sizeof(*nic_dev->txqs),  GFP_KERNEL);
	if (!nic_dev->txqs)
		return -ENOMEM;

	for (q_id = 0; q_id < num_txqs; q_id++) {
		txq = &nic_dev->txqs[q_id];
		txq->netdev = netdev;
		txq->q_id = q_id;
		txq->q_depth = nic_dev->q_params.sq_depth;
		txq->q_mask = nic_dev->q_params.sq_depth - 1;
		txq->dev = &pdev->dev;
	}

	return 0;
}

void hinic3_free_txqs(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);

	kfree(nic_dev->txqs);
}

static void hinic3_set_buf_desc(struct hinic3_sq_bufdesc *buf_descs,
				dma_addr_t addr, u32 len)
{
	buf_descs->hi_addr = cpu_to_le32(upper_32_bits(addr));
	buf_descs->lo_addr = cpu_to_le32(lower_32_bits(addr));
	buf_descs->len = cpu_to_le32(len);
}

static int hinic3_tx_map_skb(struct net_device *netdev, struct sk_buff *skb,
			     struct hinic3_txq *txq,
			     struct hinic3_tx_info *tx_info,
			     struct hinic3_sq_wqe_combo *wqe_combo)
{
	struct hinic3_sq_wqe_desc *wqe_desc = wqe_combo->ctrl_bd0;
	struct hinic3_sq_bufdesc *buf_desc = wqe_combo->bds_head;
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic3_dma_info *dma_info = tx_info->dma_info;
	struct pci_dev *pdev = nic_dev->pdev;
	skb_frag_t *frag;
	u32 i, idx;
	int err;

	dma_info[0].dma = dma_map_single(&pdev->dev, skb->data,
					 skb_headlen(skb), DMA_TO_DEVICE);
	if (dma_mapping_error(&pdev->dev, dma_info[0].dma))
		return -EFAULT;

	dma_info[0].len = skb_headlen(skb);

	wqe_desc->hi_addr = cpu_to_le32(upper_32_bits(dma_info[0].dma));
	wqe_desc->lo_addr = cpu_to_le32(lower_32_bits(dma_info[0].dma));

	wqe_desc->ctrl_len = cpu_to_le32(dma_info[0].len);

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		frag = &(skb_shinfo(skb)->frags[i]);
		if (unlikely(i == wqe_combo->first_bds_num))
			buf_desc = wqe_combo->bds_sec2;

		idx = i + 1;
		dma_info[idx].dma = skb_frag_dma_map(&pdev->dev, frag, 0,
						     skb_frag_size(frag),
						     DMA_TO_DEVICE);
		if (dma_mapping_error(&pdev->dev, dma_info[idx].dma)) {
			err = -EFAULT;
			goto err_unmap_page;
		}
		dma_info[idx].len = skb_frag_size(frag);

		hinic3_set_buf_desc(buf_desc, dma_info[idx].dma,
				    dma_info[idx].len);
		buf_desc++;
	}

	return 0;

err_unmap_page:
	while (idx > 1) {
		idx--;
		dma_unmap_page(&pdev->dev, dma_info[idx].dma,
			       dma_info[idx].len, DMA_TO_DEVICE);
	}
	dma_unmap_single(&pdev->dev, dma_info[0].dma, dma_info[0].len,
			 DMA_TO_DEVICE);

	return err;
}

static void hinic3_tx_unmap_skb(struct net_device *netdev,
				struct sk_buff *skb,
				struct hinic3_dma_info *dma_info)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct pci_dev *pdev = nic_dev->pdev;
	int i;

	for (i = 0; i < skb_shinfo(skb)->nr_frags;) {
		i++;
		dma_unmap_page(&pdev->dev,
			       dma_info[i].dma,
			       dma_info[i].len, DMA_TO_DEVICE);
	}

	dma_unmap_single(&pdev->dev, dma_info[0].dma,
			 dma_info[0].len, DMA_TO_DEVICE);
}

static void free_all_tx_skbs(struct net_device *netdev, u32 sq_depth,
			     struct hinic3_tx_info *tx_info_arr)
{
	struct hinic3_tx_info *tx_info;
	u32 idx;

	for (idx = 0; idx < sq_depth; idx++) {
		tx_info = &tx_info_arr[idx];
		if (tx_info->skb) {
			hinic3_tx_unmap_skb(netdev, tx_info->skb,
					    tx_info->dma_info);
			dev_kfree_skb_any(tx_info->skb);
			tx_info->skb = NULL;
		}
	}
}

union hinic3_ip {
	struct iphdr   *v4;
	struct ipv6hdr *v6;
	unsigned char  *hdr;
};

union hinic3_l4 {
	struct tcphdr *tcp;
	struct udphdr *udp;
	unsigned char *hdr;
};

enum hinic3_l3_type {
	HINIC3_L3_UNKNOWN         = 0,
	HINIC3_L3_IP6_PKT         = 1,
	HINIC3_L3_IP4_PKT_NO_CSUM = 2,
	HINIC3_L3_IP4_PKT_CSUM    = 3,
};

enum hinic3_l4_offload_type {
	HINIC3_L4_OFFLOAD_DISABLE = 0,
	HINIC3_L4_OFFLOAD_TCP     = 1,
	HINIC3_L4_OFFLOAD_STCP    = 2,
	HINIC3_L4_OFFLOAD_UDP     = 3,
};

/* initialize l4 offset and offload */
static void get_inner_l4_info(struct sk_buff *skb, union hinic3_l4 *l4,
			      u8 l4_proto, u32 *offset,
			      enum hinic3_l4_offload_type *l4_offload)
{
	switch (l4_proto) {
	case IPPROTO_TCP:
		*l4_offload = HINIC3_L4_OFFLOAD_TCP;
		/* To be same with TSO, payload offset begins from payload */
		*offset = (l4->tcp->doff << TCP_HDR_DATA_OFF_UNIT_SHIFT) +
			   TRANSPORT_OFFSET(l4->hdr, skb);
		break;

	case IPPROTO_UDP:
		*l4_offload = HINIC3_L4_OFFLOAD_UDP;
		*offset = TRANSPORT_OFFSET(l4->hdr, skb);
		break;
	default:
		*l4_offload = HINIC3_L4_OFFLOAD_DISABLE;
		*offset = 0;
	}
}

static int hinic3_tx_csum(struct hinic3_txq *txq, struct hinic3_sq_task *task,
			  struct sk_buff *skb)
{
	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	if (skb->encapsulation) {
		union hinic3_ip ip;
		u8 l4_proto;

		task->pkt_info0 |= cpu_to_le32(SQ_TASK_INFO0_SET(1,
								 TUNNEL_FLAG));

		ip.hdr = skb_network_header(skb);
		if (ip.v4->version == 4) {
			l4_proto = ip.v4->protocol;
		} else if (ip.v4->version == 6) {
			union hinic3_l4 l4;
			unsigned char *exthdr;
			__be16 frag_off;

			exthdr = ip.hdr + sizeof(*ip.v6);
			l4_proto = ip.v6->nexthdr;
			l4.hdr = skb_transport_header(skb);
			if (l4.hdr != exthdr)
				ipv6_skip_exthdr(skb, exthdr - skb->data,
						 &l4_proto, &frag_off);
		} else {
			l4_proto = IPPROTO_RAW;
		}

		if (l4_proto != IPPROTO_UDP ||
		    ((struct udphdr *)skb_transport_header(skb))->dest !=
		    VXLAN_OFFLOAD_PORT_LE) {
			/* Unsupported tunnel packet, disable csum offload */
			skb_checksum_help(skb);
			return 0;
		}
	}

	task->pkt_info0 |= cpu_to_le32(SQ_TASK_INFO0_SET(1, INNER_L4_EN));

	return 1;
}

static void get_inner_l3_l4_type(struct sk_buff *skb, union hinic3_ip *ip,
				 union hinic3_l4 *l4,
				 enum hinic3_l3_type *l3_type, u8 *l4_proto)
{
	unsigned char *exthdr;
	__be16 frag_off;

	if (ip->v4->version == 4) {
		*l3_type = HINIC3_L3_IP4_PKT_CSUM;
		*l4_proto = ip->v4->protocol;
	} else if (ip->v4->version == 6) {
		*l3_type = HINIC3_L3_IP6_PKT;
		exthdr = ip->hdr + sizeof(*ip->v6);
		*l4_proto = ip->v6->nexthdr;
		if (exthdr != l4->hdr) {
			ipv6_skip_exthdr(skb, exthdr - skb->data,
					 l4_proto, &frag_off);
		}
	} else {
		*l3_type = HINIC3_L3_UNKNOWN;
		*l4_proto = 0;
	}
}

static void hinic3_set_tso_info(struct hinic3_sq_task *task, __le32 *queue_info,
				enum hinic3_l4_offload_type l4_offload,
				u32 offset, u32 mss)
{
	if (l4_offload == HINIC3_L4_OFFLOAD_TCP) {
		*queue_info |= cpu_to_le32(SQ_CTRL_QUEUE_INFO_SET(1, TSO));
		task->pkt_info0 |= cpu_to_le32(SQ_TASK_INFO0_SET(1,
								 INNER_L4_EN));
	} else if (l4_offload == HINIC3_L4_OFFLOAD_UDP) {
		*queue_info |= cpu_to_le32(SQ_CTRL_QUEUE_INFO_SET(1, UFO));
		task->pkt_info0 |= cpu_to_le32(SQ_TASK_INFO0_SET(1,
								 INNER_L4_EN));
	}

	/* enable L3 calculation */
	task->pkt_info0 |= cpu_to_le32(SQ_TASK_INFO0_SET(1, INNER_L3_EN));

	*queue_info |= cpu_to_le32(SQ_CTRL_QUEUE_INFO_SET(offset >> 1, PLDOFF));

	/* set MSS value */
	*queue_info &= cpu_to_le32(~SQ_CTRL_QUEUE_INFO_MSS_MASK);
	*queue_info |= cpu_to_le32(SQ_CTRL_QUEUE_INFO_SET(mss, MSS));
}

static __sum16 csum_magic(union hinic3_ip *ip, unsigned short proto)
{
	return (ip->v4->version == 4) ?
		csum_tcpudp_magic(ip->v4->saddr, ip->v4->daddr, 0, proto, 0) :
		csum_ipv6_magic(&ip->v6->saddr, &ip->v6->daddr, 0, proto, 0);
}

static int hinic3_tso(struct hinic3_sq_task *task, __le32 *queue_info,
		      struct sk_buff *skb)
{
	enum hinic3_l4_offload_type l4_offload;
	enum hinic3_l3_type l3_type;
	union hinic3_ip ip;
	union hinic3_l4 l4;
	u8 l4_proto;
	u32 offset;
	int err;

	if (!skb_is_gso(skb))
		return 0;

	err = skb_cow_head(skb, 0);
	if (err < 0)
		return err;

	if (skb->encapsulation) {
		u32 gso_type = skb_shinfo(skb)->gso_type;
		/* L3 checksum is always enabled */
		task->pkt_info0 |= cpu_to_le32(SQ_TASK_INFO0_SET(1, OUT_L3_EN));
		task->pkt_info0 |= cpu_to_le32(SQ_TASK_INFO0_SET(1,
								 TUNNEL_FLAG));

		l4.hdr = skb_transport_header(skb);
		ip.hdr = skb_network_header(skb);

		if (gso_type & SKB_GSO_UDP_TUNNEL_CSUM) {
			l4.udp->check = ~csum_magic(&ip, IPPROTO_UDP);
			task->pkt_info0 |=
				cpu_to_le32(SQ_TASK_INFO0_SET(1, OUT_L4_EN));
		}

		ip.hdr = skb_inner_network_header(skb);
		l4.hdr = skb_inner_transport_header(skb);
	} else {
		ip.hdr = skb_network_header(skb);
		l4.hdr = skb_transport_header(skb);
	}

	get_inner_l3_l4_type(skb, &ip, &l4, &l3_type, &l4_proto);

	if (l4_proto == IPPROTO_TCP)
		l4.tcp->check = ~csum_magic(&ip, IPPROTO_TCP);

	get_inner_l4_info(skb, &l4, l4_proto, &offset, &l4_offload);

	hinic3_set_tso_info(task, queue_info, l4_offload, offset,
			    skb_shinfo(skb)->gso_size);

	return 1;
}

static void hinic3_set_vlan_tx_offload(struct hinic3_sq_task *task,
				       u16 vlan_tag, u8 vlan_tpid)
{
	/* vlan_tpid: 0=select TPID0 in IPSU, 1=select TPID1 in IPSU
	 * 2=select TPID2 in IPSU, 3=select TPID3 in IPSU,
	 * 4=select TPID4 in IPSU
	 */
	task->vlan_offload =
		cpu_to_le32(SQ_TASK_INFO3_SET(vlan_tag, VLAN_TAG) |
			    SQ_TASK_INFO3_SET(vlan_tpid, VLAN_TPID) |
			    SQ_TASK_INFO3_SET(1, VLAN_TAG_VALID));
}

static u32 hinic3_tx_offload(struct sk_buff *skb, struct hinic3_sq_task *task,
			     __le32 *queue_info, struct hinic3_txq *txq)
{
	u32 offload = 0;
	int tso_cs_en;

	task->pkt_info0 = 0;
	task->ip_identify = 0;
	task->rsvd = 0;
	task->vlan_offload = 0;

	tso_cs_en = hinic3_tso(task, queue_info, skb);
	if (tso_cs_en < 0) {
		offload = HINIC3_TX_OFFLOAD_INVALID;
		return offload;
	} else if (tso_cs_en) {
		offload |= HINIC3_TX_OFFLOAD_TSO;
	} else {
		tso_cs_en = hinic3_tx_csum(txq, task, skb);
		if (tso_cs_en)
			offload |= HINIC3_TX_OFFLOAD_CSUM;
	}

#define VLAN_INSERT_MODE_MAX 5
	if (unlikely(skb_vlan_tag_present(skb))) {
		/* select vlan insert mode by qid, default 802.1Q Tag type */
		hinic3_set_vlan_tx_offload(task, skb_vlan_tag_get(skb),
					   txq->q_id % VLAN_INSERT_MODE_MAX);
		offload |= HINIC3_TX_OFFLOAD_VLAN;
	}

	if (unlikely(SQ_CTRL_QUEUE_INFO_GET(*queue_info, PLDOFF) >
		     SQ_CTRL_MAX_PLDOFF)) {
		offload = HINIC3_TX_OFFLOAD_INVALID;
		return offload;
	}

	return offload;
}

static u16 hinic3_get_and_update_sq_owner(struct hinic3_io_queue *sq,
					  u16 curr_pi, u16 wqebb_cnt)
{
	u16 owner = sq->owner;

	if (unlikely(curr_pi + wqebb_cnt >= sq->wq.q_depth))
		sq->owner = !sq->owner;

	return owner;
}

static u16 hinic3_set_wqe_combo(struct hinic3_txq *txq,
				struct hinic3_sq_wqe_combo *wqe_combo,
				u32 offload, u16 num_sge, u16 *curr_pi)
{
	struct hinic3_sq_bufdesc *first_part_wqebbs, *second_part_wqebbs;
	u16 first_part_wqebbs_num, tmp_pi;

	wqe_combo->ctrl_bd0 = hinic3_wq_get_one_wqebb(&txq->sq->wq, curr_pi);
	if (!offload && num_sge == 1) {
		wqe_combo->wqe_type = SQ_WQE_COMPACT_TYPE;
		return hinic3_get_and_update_sq_owner(txq->sq, *curr_pi, 1);
	}

	wqe_combo->wqe_type = SQ_WQE_EXTENDED_TYPE;

	if (offload) {
		wqe_combo->task = hinic3_wq_get_one_wqebb(&txq->sq->wq,
							  &tmp_pi);
		wqe_combo->task_type = SQ_WQE_TASKSECT_16BYTES;
	} else {
		wqe_combo->task_type = SQ_WQE_TASKSECT_46BITS;
	}

	if (num_sge > 1) {
		/* first wqebb contain bd0, and bd size is equal to sq wqebb
		 * size, so we use (num_sge - 1) as wanted weqbb_cnt
		 */
		hinic3_wq_get_multi_wqebbs(&txq->sq->wq, num_sge - 1, &tmp_pi,
					   &first_part_wqebbs,
					   &second_part_wqebbs,
					   &first_part_wqebbs_num);
		wqe_combo->bds_head = first_part_wqebbs;
		wqe_combo->bds_sec2 = second_part_wqebbs;
		wqe_combo->first_bds_num = first_part_wqebbs_num;
	}

	return hinic3_get_and_update_sq_owner(txq->sq, *curr_pi,
					      num_sge + !!offload);
}

static void hinic3_prepare_sq_ctrl(struct hinic3_sq_wqe_combo *wqe_combo,
				   __le32 queue_info, int nr_descs, u16 owner)
{
	struct hinic3_sq_wqe_desc *wqe_desc = wqe_combo->ctrl_bd0;

	if (wqe_combo->wqe_type == SQ_WQE_COMPACT_TYPE) {
		wqe_desc->ctrl_len |=
			cpu_to_le32(SQ_CTRL_SET(SQ_NORMAL_WQE, DATA_FORMAT) |
				    SQ_CTRL_SET(wqe_combo->wqe_type, EXTENDED) |
				    SQ_CTRL_SET(owner, OWNER));

		/* compact wqe queue_info will transfer to chip */
		wqe_desc->queue_info = 0;
		return;
	}

	wqe_desc->ctrl_len |=
		cpu_to_le32(SQ_CTRL_SET(nr_descs, BUFDESC_NUM) |
			    SQ_CTRL_SET(wqe_combo->task_type, TASKSECT_LEN) |
			    SQ_CTRL_SET(SQ_NORMAL_WQE, DATA_FORMAT) |
			    SQ_CTRL_SET(wqe_combo->wqe_type, EXTENDED) |
			    SQ_CTRL_SET(owner, OWNER));

	wqe_desc->queue_info = queue_info;
	wqe_desc->queue_info |= cpu_to_le32(SQ_CTRL_QUEUE_INFO_SET(1, UC));

	if (!SQ_CTRL_QUEUE_INFO_GET(wqe_desc->queue_info, MSS)) {
		wqe_desc->queue_info |=
		    cpu_to_le32(SQ_CTRL_QUEUE_INFO_SET(HINIC3_TX_MSS_DEFAULT, MSS));
	} else if (SQ_CTRL_QUEUE_INFO_GET(wqe_desc->queue_info, MSS) <
		   HINIC3_TX_MSS_MIN) {
		/* mss should not be less than 80 */
		wqe_desc->queue_info &=
		    cpu_to_le32(~SQ_CTRL_QUEUE_INFO_MSS_MASK);
		wqe_desc->queue_info |=
		    cpu_to_le32(SQ_CTRL_QUEUE_INFO_SET(HINIC3_TX_MSS_MIN, MSS));
	}
}

static netdev_tx_t hinic3_send_one_skb(struct sk_buff *skb,
				       struct net_device *netdev,
				       struct hinic3_txq *txq)
{
	struct hinic3_sq_wqe_combo wqe_combo = {};
	struct hinic3_tx_info *tx_info;
	struct hinic3_sq_task task;
	u16 wqebb_cnt, num_sge;
	__le32 queue_info = 0;
	u16 saved_wq_prod_idx;
	u16 owner, pi = 0;
	u8 saved_sq_owner;
	u32 offload;
	int err;

	if (unlikely(skb->len < MIN_SKB_LEN)) {
		if (skb_pad(skb, MIN_SKB_LEN - skb->len))
			goto err_out;

		skb->len = MIN_SKB_LEN;
	}

	num_sge = skb_shinfo(skb)->nr_frags + 1;
	/* assume normal wqe format + 1 wqebb for task info */
	wqebb_cnt = num_sge + 1;

	if (unlikely(hinic3_wq_free_wqebbs(&txq->sq->wq) < wqebb_cnt)) {
		if (likely(wqebb_cnt > txq->tx_stop_thrs))
			txq->tx_stop_thrs = min(wqebb_cnt, txq->tx_start_thrs);

		netif_subqueue_try_stop(netdev, txq->sq->q_id,
					hinic3_wq_free_wqebbs(&txq->sq->wq),
					txq->tx_start_thrs);

		return NETDEV_TX_BUSY;
	}

	offload = hinic3_tx_offload(skb, &task, &queue_info, txq);
	if (unlikely(offload == HINIC3_TX_OFFLOAD_INVALID)) {
		goto err_drop_pkt;
	} else if (!offload) {
		wqebb_cnt -= 1;
		if (unlikely(num_sge == 1 &&
			     skb->len > HINIC3_COMPACT_WQEE_SKB_MAX_LEN))
			goto err_drop_pkt;
	}

	saved_wq_prod_idx = txq->sq->wq.prod_idx;
	saved_sq_owner = txq->sq->owner;

	owner = hinic3_set_wqe_combo(txq, &wqe_combo, offload, num_sge, &pi);
	if (offload)
		*wqe_combo.task = task;

	tx_info = &txq->tx_info[pi];
	tx_info->skb = skb;
	tx_info->wqebb_cnt = wqebb_cnt;

	err = hinic3_tx_map_skb(netdev, skb, txq, tx_info, &wqe_combo);
	if (err) {
		/* Rollback work queue to reclaim the wqebb we did not use */
		txq->sq->wq.prod_idx = saved_wq_prod_idx;
		txq->sq->owner = saved_sq_owner;
		goto err_drop_pkt;
	}

	netif_subqueue_sent(netdev, txq->sq->q_id, skb->len);
	netif_subqueue_maybe_stop(netdev, txq->sq->q_id,
				  hinic3_wq_free_wqebbs(&txq->sq->wq),
				  txq->tx_stop_thrs,
				  txq->tx_start_thrs);

	hinic3_prepare_sq_ctrl(&wqe_combo, queue_info, num_sge, owner);
	hinic3_write_db(txq->sq, 0, DB_CFLAG_DP_SQ,
			hinic3_get_sq_local_pi(txq->sq));

	return NETDEV_TX_OK;

err_drop_pkt:
	dev_kfree_skb_any(skb);

err_out:
	return NETDEV_TX_OK;
}

netdev_tx_t hinic3_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	u16 q_id = skb_get_queue_mapping(skb);

	if (unlikely(!netif_carrier_ok(netdev)))
		goto err_drop_pkt;

	if (unlikely(q_id >= nic_dev->q_params.num_qps))
		goto err_drop_pkt;

	return hinic3_send_one_skb(skb, netdev, &nic_dev->txqs[q_id]);

err_drop_pkt:
	dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

static bool is_hw_complete_sq_process(struct hinic3_io_queue *sq)
{
	u16 sw_pi, hw_ci;

	sw_pi = hinic3_get_sq_local_pi(sq);
	hw_ci = hinic3_get_sq_hw_ci(sq);

	return sw_pi == hw_ci;
}

#define HINIC3_FLUSH_QUEUE_POLL_SLEEP_US   10000
#define HINIC3_FLUSH_QUEUE_POLL_TIMEOUT_US 10000000
static int hinic3_stop_sq(struct hinic3_txq *txq)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(txq->netdev);
	int err, rc;

	err = read_poll_timeout(hinic3_force_drop_tx_pkt, rc,
				is_hw_complete_sq_process(txq->sq) || rc,
				HINIC3_FLUSH_QUEUE_POLL_SLEEP_US,
				HINIC3_FLUSH_QUEUE_POLL_TIMEOUT_US,
				true, nic_dev->hwdev);
	if (rc)
		return rc;
	else
		return err;
}

/* packet transmission should be stopped before calling this function */
void hinic3_flush_txqs(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	u16 qid;
	int err;

	for (qid = 0; qid < nic_dev->q_params.num_qps; qid++) {
		err = hinic3_stop_sq(&nic_dev->txqs[qid]);
		netdev_tx_reset_subqueue(netdev, qid);
		if (err)
			netdev_err(netdev, "Failed to stop sq%u\n", qid);
	}
}

#define HINIC3_BDS_PER_SQ_WQEBB \
	(HINIC3_SQ_WQEBB_SIZE / sizeof(struct hinic3_sq_bufdesc))

int hinic3_alloc_txqs_res(struct net_device *netdev, u16 num_sq,
			  u32 sq_depth, struct hinic3_dyna_txq_res *txqs_res)
{
	struct hinic3_dyna_txq_res *tqres;
	int idx;

	for (idx = 0; idx < num_sq; idx++) {
		tqres = &txqs_res[idx];

		tqres->tx_info = kcalloc(sq_depth, sizeof(*tqres->tx_info),
					 GFP_KERNEL);
		if (!tqres->tx_info)
			goto err_free_tqres;

		tqres->bds = kcalloc(sq_depth * HINIC3_BDS_PER_SQ_WQEBB +
				     HINIC3_MAX_SQ_SGE, sizeof(*tqres->bds),
				     GFP_KERNEL);
		if (!tqres->bds) {
			kfree(tqres->tx_info);
			goto err_free_tqres;
		}
	}

	return 0;

err_free_tqres:
	while (idx > 0) {
		idx--;
		tqres = &txqs_res[idx];

		kfree(tqres->bds);
		kfree(tqres->tx_info);
	}

	return -ENOMEM;
}

void hinic3_free_txqs_res(struct net_device *netdev, u16 num_sq,
			  u32 sq_depth, struct hinic3_dyna_txq_res *txqs_res)
{
	struct hinic3_dyna_txq_res *tqres;
	int idx;

	for (idx = 0; idx < num_sq; idx++) {
		tqres = &txqs_res[idx];

		free_all_tx_skbs(netdev, sq_depth, tqres->tx_info);
		kfree(tqres->bds);
		kfree(tqres->tx_info);
	}
}

int hinic3_configure_txqs(struct net_device *netdev, u16 num_sq,
			  u32 sq_depth, struct hinic3_dyna_txq_res *txqs_res)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic3_dyna_txq_res *tqres;
	struct hinic3_txq *txq;
	u16 q_id;
	u32 idx;

	for (q_id = 0; q_id < num_sq; q_id++) {
		txq = &nic_dev->txqs[q_id];
		tqres = &txqs_res[q_id];

		txq->q_depth = sq_depth;
		txq->q_mask = sq_depth - 1;

		txq->tx_stop_thrs = min(HINIC3_DEFAULT_STOP_THRS,
					sq_depth / 20);
		txq->tx_start_thrs = min(HINIC3_DEFAULT_START_THRS,
					 sq_depth / 10);

		txq->tx_info = tqres->tx_info;
		for (idx = 0; idx < sq_depth; idx++)
			txq->tx_info[idx].dma_info =
				&tqres->bds[idx * HINIC3_BDS_PER_SQ_WQEBB];

		txq->sq = &nic_dev->nic_io->sq[q_id];
	}

	return 0;
}

bool hinic3_tx_poll(struct hinic3_txq *txq, int budget)
{
	struct net_device *netdev = txq->netdev;
	u16 hw_ci, sw_ci, q_id = txq->sq->q_id;
	struct hinic3_tx_info *tx_info;
	unsigned int bytes_compl = 0;
	unsigned int pkts = 0;
	u16 wqebb_cnt = 0;

	hw_ci = hinic3_get_sq_hw_ci(txq->sq);
	dma_rmb();
	sw_ci = hinic3_get_sq_local_ci(txq->sq);

	do {
		tx_info = &txq->tx_info[sw_ci];

		/* Did all wqebb of this wqe complete? */
		if (hw_ci == sw_ci ||
		    ((hw_ci - sw_ci) & txq->q_mask) < tx_info->wqebb_cnt)
			break;

		sw_ci = (sw_ci + tx_info->wqebb_cnt) & txq->q_mask;
		net_prefetch(&txq->tx_info[sw_ci]);

		wqebb_cnt += tx_info->wqebb_cnt;
		bytes_compl += tx_info->skb->len;
		pkts++;

		hinic3_tx_unmap_skb(netdev, tx_info->skb, tx_info->dma_info);
		napi_consume_skb(tx_info->skb, budget);
		tx_info->skb = NULL;
	} while (likely(pkts < HINIC3_TX_POLL_WEIGHT));

	hinic3_wq_put_wqebbs(&txq->sq->wq, wqebb_cnt);

	netif_subqueue_completed_wake(netdev, q_id, pkts, bytes_compl,
				      hinic3_wq_free_wqebbs(&txq->sq->wq),
				      txq->tx_start_thrs);

	return pkts == HINIC3_TX_POLL_WEIGHT;
}
