// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Ethernet driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include <linux/interrupt.h>
#include <linux/pci.h>
#include <net/tso.h>

#include "otx2_reg.h"
#include "otx2_common.h"
#include "otx2_struct.h"
#include "cn10k.h"

static void otx2_nix_rq_op_stats(struct queue_stats *stats,
				 struct otx2_nic *pfvf, int qidx)
{
	u64 incr = (u64)qidx << 32;
	u64 *ptr;

	ptr = (u64 *)otx2_get_regaddr(pfvf, NIX_LF_RQ_OP_OCTS);
	stats->bytes = otx2_atomic64_add(incr, ptr);

	ptr = (u64 *)otx2_get_regaddr(pfvf, NIX_LF_RQ_OP_PKTS);
	stats->pkts = otx2_atomic64_add(incr, ptr);
}

static void otx2_nix_sq_op_stats(struct queue_stats *stats,
				 struct otx2_nic *pfvf, int qidx)
{
	u64 incr = (u64)qidx << 32;
	u64 *ptr;

	ptr = (u64 *)otx2_get_regaddr(pfvf, NIX_LF_SQ_OP_OCTS);
	stats->bytes = otx2_atomic64_add(incr, ptr);

	ptr = (u64 *)otx2_get_regaddr(pfvf, NIX_LF_SQ_OP_PKTS);
	stats->pkts = otx2_atomic64_add(incr, ptr);
}

void otx2_update_lmac_stats(struct otx2_nic *pfvf)
{
	struct msg_req *req;

	if (!netif_running(pfvf->netdev))
		return;

	mutex_lock(&pfvf->mbox.lock);
	req = otx2_mbox_alloc_msg_cgx_stats(&pfvf->mbox);
	if (!req) {
		mutex_unlock(&pfvf->mbox.lock);
		return;
	}

	otx2_sync_mbox_msg(&pfvf->mbox);
	mutex_unlock(&pfvf->mbox.lock);
}

void otx2_update_lmac_fec_stats(struct otx2_nic *pfvf)
{
	struct msg_req *req;

	if (!netif_running(pfvf->netdev))
		return;
	mutex_lock(&pfvf->mbox.lock);
	req = otx2_mbox_alloc_msg_cgx_fec_stats(&pfvf->mbox);
	if (req)
		otx2_sync_mbox_msg(&pfvf->mbox);
	mutex_unlock(&pfvf->mbox.lock);
}

int otx2_update_rq_stats(struct otx2_nic *pfvf, int qidx)
{
	struct otx2_rcv_queue *rq = &pfvf->qset.rq[qidx];

	if (!pfvf->qset.rq)
		return 0;

	otx2_nix_rq_op_stats(&rq->stats, pfvf, qidx);
	return 1;
}

int otx2_update_sq_stats(struct otx2_nic *pfvf, int qidx)
{
	struct otx2_snd_queue *sq = &pfvf->qset.sq[qidx];

	if (!pfvf->qset.sq)
		return 0;

	otx2_nix_sq_op_stats(&sq->stats, pfvf, qidx);
	return 1;
}

void otx2_get_dev_stats(struct otx2_nic *pfvf)
{
	struct otx2_dev_stats *dev_stats = &pfvf->hw.dev_stats;

	dev_stats->rx_bytes = OTX2_GET_RX_STATS(RX_OCTS);
	dev_stats->rx_drops = OTX2_GET_RX_STATS(RX_DROP);
	dev_stats->rx_bcast_frames = OTX2_GET_RX_STATS(RX_BCAST);
	dev_stats->rx_mcast_frames = OTX2_GET_RX_STATS(RX_MCAST);
	dev_stats->rx_ucast_frames = OTX2_GET_RX_STATS(RX_UCAST);
	dev_stats->rx_frames = dev_stats->rx_bcast_frames +
			       dev_stats->rx_mcast_frames +
			       dev_stats->rx_ucast_frames;

	dev_stats->tx_bytes = OTX2_GET_TX_STATS(TX_OCTS);
	dev_stats->tx_drops = OTX2_GET_TX_STATS(TX_DROP);
	dev_stats->tx_bcast_frames = OTX2_GET_TX_STATS(TX_BCAST);
	dev_stats->tx_mcast_frames = OTX2_GET_TX_STATS(TX_MCAST);
	dev_stats->tx_ucast_frames = OTX2_GET_TX_STATS(TX_UCAST);
	dev_stats->tx_frames = dev_stats->tx_bcast_frames +
			       dev_stats->tx_mcast_frames +
			       dev_stats->tx_ucast_frames;
}

void otx2_get_stats64(struct net_device *netdev,
		      struct rtnl_link_stats64 *stats)
{
	struct otx2_nic *pfvf = netdev_priv(netdev);
	struct otx2_dev_stats *dev_stats;

	otx2_get_dev_stats(pfvf);

	dev_stats = &pfvf->hw.dev_stats;
	stats->rx_bytes = dev_stats->rx_bytes;
	stats->rx_packets = dev_stats->rx_frames;
	stats->rx_dropped = dev_stats->rx_drops;
	stats->multicast = dev_stats->rx_mcast_frames;

	stats->tx_bytes = dev_stats->tx_bytes;
	stats->tx_packets = dev_stats->tx_frames;
	stats->tx_dropped = dev_stats->tx_drops;
}
EXPORT_SYMBOL(otx2_get_stats64);

/* Sync MAC address with RVU AF */
static int otx2_hw_set_mac_addr(struct otx2_nic *pfvf, u8 *mac)
{
	struct nix_set_mac_addr *req;
	int err;

	mutex_lock(&pfvf->mbox.lock);
	req = otx2_mbox_alloc_msg_nix_set_mac_addr(&pfvf->mbox);
	if (!req) {
		mutex_unlock(&pfvf->mbox.lock);
		return -ENOMEM;
	}

	ether_addr_copy(req->mac_addr, mac);

	err = otx2_sync_mbox_msg(&pfvf->mbox);
	mutex_unlock(&pfvf->mbox.lock);
	return err;
}

static int otx2_hw_get_mac_addr(struct otx2_nic *pfvf,
				struct net_device *netdev)
{
	struct nix_get_mac_addr_rsp *rsp;
	struct mbox_msghdr *msghdr;
	struct msg_req *req;
	int err;

	mutex_lock(&pfvf->mbox.lock);
	req = otx2_mbox_alloc_msg_nix_get_mac_addr(&pfvf->mbox);
	if (!req) {
		mutex_unlock(&pfvf->mbox.lock);
		return -ENOMEM;
	}

	err = otx2_sync_mbox_msg(&pfvf->mbox);
	if (err) {
		mutex_unlock(&pfvf->mbox.lock);
		return err;
	}

	msghdr = otx2_mbox_get_rsp(&pfvf->mbox.mbox, 0, &req->hdr);
	if (IS_ERR(msghdr)) {
		mutex_unlock(&pfvf->mbox.lock);
		return PTR_ERR(msghdr);
	}
	rsp = (struct nix_get_mac_addr_rsp *)msghdr;
	eth_hw_addr_set(netdev, rsp->mac_addr);
	mutex_unlock(&pfvf->mbox.lock);

	return 0;
}

int otx2_set_mac_address(struct net_device *netdev, void *p)
{
	struct otx2_nic *pfvf = netdev_priv(netdev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	if (!otx2_hw_set_mac_addr(pfvf, addr->sa_data)) {
		eth_hw_addr_set(netdev, addr->sa_data);
		/* update dmac field in vlan offload rule */
		if (netif_running(netdev) &&
		    pfvf->flags & OTX2_FLAG_RX_VLAN_SUPPORT)
			otx2_install_rxvlan_offload_flow(pfvf);
		/* update dmac address in ntuple and DMAC filter list */
		if (pfvf->flags & OTX2_FLAG_DMACFLTR_SUPPORT)
			otx2_dmacflt_update_pfmac_flow(pfvf);
	} else {
		return -EPERM;
	}

	return 0;
}
EXPORT_SYMBOL(otx2_set_mac_address);

int otx2_hw_set_mtu(struct otx2_nic *pfvf, int mtu)
{
	struct nix_frs_cfg *req;
	u16 maxlen;
	int err;

	maxlen = otx2_get_max_mtu(pfvf) + OTX2_ETH_HLEN + OTX2_HW_TIMESTAMP_LEN;

	mutex_lock(&pfvf->mbox.lock);
	req = otx2_mbox_alloc_msg_nix_set_hw_frs(&pfvf->mbox);
	if (!req) {
		mutex_unlock(&pfvf->mbox.lock);
		return -ENOMEM;
	}

	req->maxlen = pfvf->netdev->mtu + OTX2_ETH_HLEN + OTX2_HW_TIMESTAMP_LEN;

	/* Use max receive length supported by hardware for loopback devices */
	if (is_otx2_lbkvf(pfvf->pdev))
		req->maxlen = maxlen;

	err = otx2_sync_mbox_msg(&pfvf->mbox);
	mutex_unlock(&pfvf->mbox.lock);
	return err;
}

int otx2_config_pause_frm(struct otx2_nic *pfvf)
{
	struct cgx_pause_frm_cfg *req;
	int err;

	if (is_otx2_lbkvf(pfvf->pdev))
		return 0;

	mutex_lock(&pfvf->mbox.lock);
	req = otx2_mbox_alloc_msg_cgx_cfg_pause_frm(&pfvf->mbox);
	if (!req) {
		err = -ENOMEM;
		goto unlock;
	}

	req->rx_pause = !!(pfvf->flags & OTX2_FLAG_RX_PAUSE_ENABLED);
	req->tx_pause = !!(pfvf->flags & OTX2_FLAG_TX_PAUSE_ENABLED);
	req->set = 1;

	err = otx2_sync_mbox_msg(&pfvf->mbox);
unlock:
	mutex_unlock(&pfvf->mbox.lock);
	return err;
}
EXPORT_SYMBOL(otx2_config_pause_frm);

int otx2_set_flowkey_cfg(struct otx2_nic *pfvf)
{
	struct otx2_rss_info *rss = &pfvf->hw.rss_info;
	struct nix_rss_flowkey_cfg_rsp *rsp;
	struct nix_rss_flowkey_cfg *req;
	int err;

	mutex_lock(&pfvf->mbox.lock);
	req = otx2_mbox_alloc_msg_nix_rss_flowkey_cfg(&pfvf->mbox);
	if (!req) {
		mutex_unlock(&pfvf->mbox.lock);
		return -ENOMEM;
	}
	req->mcam_index = -1; /* Default or reserved index */
	req->flowkey_cfg = rss->flowkey_cfg;
	req->group = DEFAULT_RSS_CONTEXT_GROUP;

	err = otx2_sync_mbox_msg(&pfvf->mbox);
	if (err)
		goto fail;

	rsp = (struct nix_rss_flowkey_cfg_rsp *)
			otx2_mbox_get_rsp(&pfvf->mbox.mbox, 0, &req->hdr);
	if (IS_ERR(rsp)) {
		err = PTR_ERR(rsp);
		goto fail;
	}

	pfvf->hw.flowkey_alg_idx = rsp->alg_idx;
fail:
	mutex_unlock(&pfvf->mbox.lock);
	return err;
}

int otx2_set_rss_table(struct otx2_nic *pfvf, int ctx_id)
{
	struct otx2_rss_info *rss = &pfvf->hw.rss_info;
	const int index = rss->rss_size * ctx_id;
	struct mbox *mbox = &pfvf->mbox;
	struct otx2_rss_ctx *rss_ctx;
	struct nix_aq_enq_req *aq;
	int idx, err;

	mutex_lock(&mbox->lock);
	rss_ctx = rss->rss_ctx[ctx_id];
	/* Get memory to put this msg */
	for (idx = 0; idx < rss->rss_size; idx++) {
		aq = otx2_mbox_alloc_msg_nix_aq_enq(mbox);
		if (!aq) {
			/* The shared memory buffer can be full.
			 * Flush it and retry
			 */
			err = otx2_sync_mbox_msg(mbox);
			if (err) {
				mutex_unlock(&mbox->lock);
				return err;
			}
			aq = otx2_mbox_alloc_msg_nix_aq_enq(mbox);
			if (!aq) {
				mutex_unlock(&mbox->lock);
				return -ENOMEM;
			}
		}

		aq->rss.rq = rss_ctx->ind_tbl[idx];

		/* Fill AQ info */
		aq->qidx = index + idx;
		aq->ctype = NIX_AQ_CTYPE_RSS;
		aq->op = NIX_AQ_INSTOP_INIT;
	}
	err = otx2_sync_mbox_msg(mbox);
	mutex_unlock(&mbox->lock);
	return err;
}

void otx2_set_rss_key(struct otx2_nic *pfvf)
{
	struct otx2_rss_info *rss = &pfvf->hw.rss_info;
	u64 *key = (u64 *)&rss->key[4];
	int idx;

	/* 352bit or 44byte key needs to be configured as below
	 * NIX_LF_RX_SECRETX0 = key<351:288>
	 * NIX_LF_RX_SECRETX1 = key<287:224>
	 * NIX_LF_RX_SECRETX2 = key<223:160>
	 * NIX_LF_RX_SECRETX3 = key<159:96>
	 * NIX_LF_RX_SECRETX4 = key<95:32>
	 * NIX_LF_RX_SECRETX5<63:32> = key<31:0>
	 */
	otx2_write64(pfvf, NIX_LF_RX_SECRETX(5),
		     (u64)(*((u32 *)&rss->key)) << 32);
	idx = sizeof(rss->key) / sizeof(u64);
	while (idx > 0) {
		idx--;
		otx2_write64(pfvf, NIX_LF_RX_SECRETX(idx), *key++);
	}
}

int otx2_rss_init(struct otx2_nic *pfvf)
{
	struct otx2_rss_info *rss = &pfvf->hw.rss_info;
	struct otx2_rss_ctx *rss_ctx;
	int idx, ret = 0;

	rss->rss_size = sizeof(*rss->rss_ctx[DEFAULT_RSS_CONTEXT_GROUP]);

	/* Init RSS key if it is not setup already */
	if (!rss->enable)
		netdev_rss_key_fill(rss->key, sizeof(rss->key));
	otx2_set_rss_key(pfvf);

	if (!netif_is_rxfh_configured(pfvf->netdev)) {
		/* Set RSS group 0 as default indirection table */
		rss->rss_ctx[DEFAULT_RSS_CONTEXT_GROUP] = kzalloc(rss->rss_size,
								  GFP_KERNEL);
		if (!rss->rss_ctx[DEFAULT_RSS_CONTEXT_GROUP])
			return -ENOMEM;

		rss_ctx = rss->rss_ctx[DEFAULT_RSS_CONTEXT_GROUP];
		for (idx = 0; idx < rss->rss_size; idx++)
			rss_ctx->ind_tbl[idx] =
				ethtool_rxfh_indir_default(idx,
							   pfvf->hw.rx_queues);
	}
	ret = otx2_set_rss_table(pfvf, DEFAULT_RSS_CONTEXT_GROUP);
	if (ret)
		return ret;

	/* Flowkey or hash config to be used for generating flow tag */
	rss->flowkey_cfg = rss->enable ? rss->flowkey_cfg :
			   NIX_FLOW_KEY_TYPE_IPV4 | NIX_FLOW_KEY_TYPE_IPV6 |
			   NIX_FLOW_KEY_TYPE_TCP | NIX_FLOW_KEY_TYPE_UDP |
			   NIX_FLOW_KEY_TYPE_SCTP | NIX_FLOW_KEY_TYPE_VLAN |
			   NIX_FLOW_KEY_TYPE_IPV4_PROTO;

	ret = otx2_set_flowkey_cfg(pfvf);
	if (ret)
		return ret;

	rss->enable = true;
	return 0;
}

/* Setup UDP segmentation algorithm in HW */
static void otx2_setup_udp_segmentation(struct nix_lso_format_cfg *lso, bool v4)
{
	struct nix_lso_format *field;

	field = (struct nix_lso_format *)&lso->fields[0];
	lso->field_mask = GENMASK(18, 0);

	/* IP's Length field */
	field->layer = NIX_TXLAYER_OL3;
	/* In ipv4, length field is at offset 2 bytes, for ipv6 it's 4 */
	field->offset = v4 ? 2 : 4;
	field->sizem1 = 1; /* i.e 2 bytes */
	field->alg = NIX_LSOALG_ADD_PAYLEN;
	field++;

	/* No ID field in IPv6 header */
	if (v4) {
		/* Increment IPID */
		field->layer = NIX_TXLAYER_OL3;
		field->offset = 4;
		field->sizem1 = 1; /* i.e 2 bytes */
		field->alg = NIX_LSOALG_ADD_SEGNUM;
		field++;
	}

	/* Update length in UDP header */
	field->layer = NIX_TXLAYER_OL4;
	field->offset = 4;
	field->sizem1 = 1;
	field->alg = NIX_LSOALG_ADD_PAYLEN;
}

/* Setup segmentation algorithms in HW and retrieve algorithm index */
void otx2_setup_segmentation(struct otx2_nic *pfvf)
{
	struct nix_lso_format_cfg_rsp *rsp;
	struct nix_lso_format_cfg *lso;
	struct otx2_hw *hw = &pfvf->hw;
	int err;

	mutex_lock(&pfvf->mbox.lock);

	/* UDPv4 segmentation */
	lso = otx2_mbox_alloc_msg_nix_lso_format_cfg(&pfvf->mbox);
	if (!lso)
		goto fail;

	/* Setup UDP/IP header fields that HW should update per segment */
	otx2_setup_udp_segmentation(lso, true);

	err = otx2_sync_mbox_msg(&pfvf->mbox);
	if (err)
		goto fail;

	rsp = (struct nix_lso_format_cfg_rsp *)
			otx2_mbox_get_rsp(&pfvf->mbox.mbox, 0, &lso->hdr);
	if (IS_ERR(rsp))
		goto fail;

	hw->lso_udpv4_idx = rsp->lso_format_idx;

	/* UDPv6 segmentation */
	lso = otx2_mbox_alloc_msg_nix_lso_format_cfg(&pfvf->mbox);
	if (!lso)
		goto fail;

	/* Setup UDP/IP header fields that HW should update per segment */
	otx2_setup_udp_segmentation(lso, false);

	err = otx2_sync_mbox_msg(&pfvf->mbox);
	if (err)
		goto fail;

	rsp = (struct nix_lso_format_cfg_rsp *)
			otx2_mbox_get_rsp(&pfvf->mbox.mbox, 0, &lso->hdr);
	if (IS_ERR(rsp))
		goto fail;

	hw->lso_udpv6_idx = rsp->lso_format_idx;
	mutex_unlock(&pfvf->mbox.lock);
	return;
fail:
	mutex_unlock(&pfvf->mbox.lock);
	netdev_info(pfvf->netdev,
		    "Failed to get LSO index for UDP GSO offload, disabling\n");
	pfvf->netdev->hw_features &= ~NETIF_F_GSO_UDP_L4;
}

void otx2_config_irq_coalescing(struct otx2_nic *pfvf, int qidx)
{
	/* Configure CQE interrupt coalescing parameters
	 *
	 * HW triggers an irq when ECOUNT > cq_ecount_wait, hence
	 * set 1 less than cq_ecount_wait. And cq_time_wait is in
	 * usecs, convert that to 100ns count.
	 */
	otx2_write64(pfvf, NIX_LF_CINTX_WAIT(qidx),
		     ((u64)(pfvf->hw.cq_time_wait * 10) << 48) |
		     ((u64)pfvf->hw.cq_qcount_wait << 32) |
		     (pfvf->hw.cq_ecount_wait - 1));
}

int __otx2_alloc_rbuf(struct otx2_nic *pfvf, struct otx2_pool *pool,
		      dma_addr_t *dma)
{
	u8 *buf;

	buf = napi_alloc_frag_align(pool->rbsize, OTX2_ALIGN);
	if (unlikely(!buf))
		return -ENOMEM;

	*dma = dma_map_single_attrs(pfvf->dev, buf, pool->rbsize,
				    DMA_FROM_DEVICE, DMA_ATTR_SKIP_CPU_SYNC);
	if (unlikely(dma_mapping_error(pfvf->dev, *dma))) {
		page_frag_free(buf);
		return -ENOMEM;
	}

	return 0;
}

static int otx2_alloc_rbuf(struct otx2_nic *pfvf, struct otx2_pool *pool,
			   dma_addr_t *dma)
{
	int ret;

	local_bh_disable();
	ret = __otx2_alloc_rbuf(pfvf, pool, dma);
	local_bh_enable();
	return ret;
}

int otx2_alloc_buffer(struct otx2_nic *pfvf, struct otx2_cq_queue *cq,
		      dma_addr_t *dma)
{
	if (unlikely(__otx2_alloc_rbuf(pfvf, cq->rbpool, dma))) {
		struct refill_work *work;
		struct delayed_work *dwork;

		work = &pfvf->refill_wrk[cq->cq_idx];
		dwork = &work->pool_refill_work;
		/* Schedule a task if no other task is running */
		if (!cq->refill_task_sched) {
			cq->refill_task_sched = true;
			schedule_delayed_work(dwork,
					      msecs_to_jiffies(100));
		}
		return -ENOMEM;
	}
	return 0;
}

void otx2_tx_timeout(struct net_device *netdev, unsigned int txq)
{
	struct otx2_nic *pfvf = netdev_priv(netdev);

	schedule_work(&pfvf->reset_task);
}
EXPORT_SYMBOL(otx2_tx_timeout);

void otx2_get_mac_from_af(struct net_device *netdev)
{
	struct otx2_nic *pfvf = netdev_priv(netdev);
	int err;

	err = otx2_hw_get_mac_addr(pfvf, netdev);
	if (err)
		dev_warn(pfvf->dev, "Failed to read mac from hardware\n");

	/* If AF doesn't provide a valid MAC, generate a random one */
	if (!is_valid_ether_addr(netdev->dev_addr))
		eth_hw_addr_random(netdev);
}
EXPORT_SYMBOL(otx2_get_mac_from_af);

int otx2_txschq_config(struct otx2_nic *pfvf, int lvl, int prio, bool txschq_for_pfc)
{
	u16 (*schq_list)[MAX_TXSCHQ_PER_FUNC];
	struct otx2_hw *hw = &pfvf->hw;
	struct nix_txschq_config *req;
	u64 schq, parent;
	u64 dwrr_val;

	dwrr_val = mtu_to_dwrr_weight(pfvf, pfvf->tx_max_pktlen);

	req = otx2_mbox_alloc_msg_nix_txschq_cfg(&pfvf->mbox);
	if (!req)
		return -ENOMEM;

	req->lvl = lvl;
	req->num_regs = 1;

	schq_list = hw->txschq_list;
#ifdef CONFIG_DCB
	if (txschq_for_pfc)
		schq_list = pfvf->pfc_schq_list;
#endif

	schq = schq_list[lvl][prio];
	/* Set topology e.t.c configuration */
	if (lvl == NIX_TXSCH_LVL_SMQ) {
		req->reg[0] = NIX_AF_SMQX_CFG(schq);
		req->regval[0] = ((u64)pfvf->tx_max_pktlen << 8) | OTX2_MIN_MTU;
		req->regval[0] |= (0x20ULL << 51) | (0x80ULL << 39) |
				  (0x2ULL << 36);
		req->num_regs++;
		/* MDQ config */
		parent = schq_list[NIX_TXSCH_LVL_TL4][prio];
		req->reg[1] = NIX_AF_MDQX_PARENT(schq);
		req->regval[1] = parent << 16;
		req->num_regs++;
		/* Set DWRR quantum */
		req->reg[2] = NIX_AF_MDQX_SCHEDULE(schq);
		req->regval[2] =  dwrr_val;
	} else if (lvl == NIX_TXSCH_LVL_TL4) {
		parent = schq_list[NIX_TXSCH_LVL_TL3][prio];
		req->reg[0] = NIX_AF_TL4X_PARENT(schq);
		req->regval[0] = parent << 16;
		req->num_regs++;
		req->reg[1] = NIX_AF_TL4X_SCHEDULE(schq);
		req->regval[1] = dwrr_val;
	} else if (lvl == NIX_TXSCH_LVL_TL3) {
		parent = schq_list[NIX_TXSCH_LVL_TL2][prio];
		req->reg[0] = NIX_AF_TL3X_PARENT(schq);
		req->regval[0] = parent << 16;
		req->num_regs++;
		req->reg[1] = NIX_AF_TL3X_SCHEDULE(schq);
		req->regval[1] = dwrr_val;
		if (lvl == hw->txschq_link_cfg_lvl) {
			req->num_regs++;
			req->reg[2] = NIX_AF_TL3_TL2X_LINKX_CFG(schq, hw->tx_link);
			/* Enable this queue and backpressure
			 * and set relative channel
			 */
			req->regval[2] = BIT_ULL(13) | BIT_ULL(12) | prio;
		}
	} else if (lvl == NIX_TXSCH_LVL_TL2) {
		parent = schq_list[NIX_TXSCH_LVL_TL1][prio];
		req->reg[0] = NIX_AF_TL2X_PARENT(schq);
		req->regval[0] = parent << 16;

		req->num_regs++;
		req->reg[1] = NIX_AF_TL2X_SCHEDULE(schq);
		req->regval[1] = TXSCH_TL1_DFLT_RR_PRIO << 24 | dwrr_val;

		if (lvl == hw->txschq_link_cfg_lvl) {
			req->num_regs++;
			req->reg[2] = NIX_AF_TL3_TL2X_LINKX_CFG(schq, hw->tx_link);
			/* Enable this queue and backpressure
			 * and set relative channel
			 */
			req->regval[2] = BIT_ULL(13) | BIT_ULL(12) | prio;
		}
	} else if (lvl == NIX_TXSCH_LVL_TL1) {
		/* Default config for TL1.
		 * For VF this is always ignored.
		 */

		/* On CN10K, if RR_WEIGHT is greater than 16384, HW will
		 * clip it to 16384, so configuring a 24bit max value
		 * will work on both OTx2 and CN10K.
		 */
		req->reg[0] = NIX_AF_TL1X_SCHEDULE(schq);
		req->regval[0] = TXSCH_TL1_DFLT_RR_QTM;

		req->num_regs++;
		req->reg[1] = NIX_AF_TL1X_TOPOLOGY(schq);
		req->regval[1] = (TXSCH_TL1_DFLT_RR_PRIO << 1);

		req->num_regs++;
		req->reg[2] = NIX_AF_TL1X_CIR(schq);
		req->regval[2] = 0;
	}

	return otx2_sync_mbox_msg(&pfvf->mbox);
}
EXPORT_SYMBOL(otx2_txschq_config);

int otx2_smq_flush(struct otx2_nic *pfvf, int smq)
{
	struct nix_txschq_config *req;
	int rc;

	mutex_lock(&pfvf->mbox.lock);

	req = otx2_mbox_alloc_msg_nix_txschq_cfg(&pfvf->mbox);
	if (!req) {
		mutex_unlock(&pfvf->mbox.lock);
		return -ENOMEM;
	}

	req->lvl = NIX_TXSCH_LVL_SMQ;
	req->reg[0] = NIX_AF_SMQX_CFG(smq);
	req->regval[0] |= BIT_ULL(49);
	req->num_regs++;

	rc = otx2_sync_mbox_msg(&pfvf->mbox);
	mutex_unlock(&pfvf->mbox.lock);
	return rc;
}
EXPORT_SYMBOL(otx2_smq_flush);

int otx2_txsch_alloc(struct otx2_nic *pfvf)
{
	struct nix_txsch_alloc_req *req;
	struct nix_txsch_alloc_rsp *rsp;
	int lvl, schq, rc;

	/* Get memory to put this msg */
	req = otx2_mbox_alloc_msg_nix_txsch_alloc(&pfvf->mbox);
	if (!req)
		return -ENOMEM;

	/* Request one schq per level */
	for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++)
		req->schq[lvl] = 1;
	rc = otx2_sync_mbox_msg(&pfvf->mbox);
	if (rc)
		return rc;

	rsp = (struct nix_txsch_alloc_rsp *)
	      otx2_mbox_get_rsp(&pfvf->mbox.mbox, 0, &req->hdr);
	if (IS_ERR(rsp))
		return PTR_ERR(rsp);

	/* Setup transmit scheduler list */
	for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++)
		for (schq = 0; schq < rsp->schq[lvl]; schq++)
			pfvf->hw.txschq_list[lvl][schq] =
				rsp->schq_list[lvl][schq];

	pfvf->hw.txschq_link_cfg_lvl = rsp->link_cfg_lvl;

	return 0;
}

void otx2_txschq_free_one(struct otx2_nic *pfvf, u16 lvl, u16 schq)
{
	struct nix_txsch_free_req *free_req;
	int err;

	mutex_lock(&pfvf->mbox.lock);

	free_req = otx2_mbox_alloc_msg_nix_txsch_free(&pfvf->mbox);
	if (!free_req) {
		mutex_unlock(&pfvf->mbox.lock);
		netdev_err(pfvf->netdev,
			   "Failed alloc txschq free req\n");
		return;
	}

	free_req->schq_lvl = lvl;
	free_req->schq = schq;

	err = otx2_sync_mbox_msg(&pfvf->mbox);
	if (err) {
		netdev_err(pfvf->netdev,
			   "Failed stop txschq %d at level %d\n", schq, lvl);
	}

	mutex_unlock(&pfvf->mbox.lock);
}
EXPORT_SYMBOL(otx2_txschq_free_one);

void otx2_txschq_stop(struct otx2_nic *pfvf)
{
	int lvl, schq;

	/* free non QOS TLx nodes */
	for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++)
		otx2_txschq_free_one(pfvf, lvl,
				     pfvf->hw.txschq_list[lvl][0]);

	/* Clear the txschq list */
	for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
		for (schq = 0; schq < MAX_TXSCHQ_PER_FUNC; schq++)
			pfvf->hw.txschq_list[lvl][schq] = 0;
	}

}

void otx2_sqb_flush(struct otx2_nic *pfvf)
{
	int qidx, sqe_tail, sqe_head;
	u64 incr, *ptr, val;
	int timeout = 1000;

	ptr = (u64 *)otx2_get_regaddr(pfvf, NIX_LF_SQ_OP_STATUS);
	for (qidx = 0; qidx < pfvf->hw.non_qos_queues; qidx++) {
		incr = (u64)qidx << 32;
		while (timeout) {
			val = otx2_atomic64_add(incr, ptr);
			sqe_head = (val >> 20) & 0x3F;
			sqe_tail = (val >> 28) & 0x3F;
			if (sqe_head == sqe_tail)
				break;
			usleep_range(1, 3);
			timeout--;
		}
	}
}

/* RED and drop levels of CQ on packet reception.
 * For CQ level is measure of emptiness ( 0x0 = full, 255 = empty).
 */
#define RQ_PASS_LVL_CQ(skid, qsize)	((((skid) + 16) * 256) / (qsize))
#define RQ_DROP_LVL_CQ(skid, qsize)	(((skid) * 256) / (qsize))

/* RED and drop levels of AURA for packet reception.
 * For AURA level is measure of fullness (0x0 = empty, 255 = full).
 * Eg: For RQ length 1K, for pass/drop level 204/230.
 * RED accepts pkts if free pointers > 102 & <= 205.
 * Drops pkts if free pointers < 102.
 */
#define RQ_BP_LVL_AURA   (255 - ((85 * 256) / 100)) /* BP when 85% is full */
#define RQ_PASS_LVL_AURA (255 - ((95 * 256) / 100)) /* RED when 95% is full */
#define RQ_DROP_LVL_AURA (255 - ((99 * 256) / 100)) /* Drop when 99% is full */

static int otx2_rq_init(struct otx2_nic *pfvf, u16 qidx, u16 lpb_aura)
{
	struct otx2_qset *qset = &pfvf->qset;
	struct nix_aq_enq_req *aq;

	/* Get memory to put this msg */
	aq = otx2_mbox_alloc_msg_nix_aq_enq(&pfvf->mbox);
	if (!aq)
		return -ENOMEM;

	aq->rq.cq = qidx;
	aq->rq.ena = 1;
	aq->rq.pb_caching = 1;
	aq->rq.lpb_aura = lpb_aura; /* Use large packet buffer aura */
	aq->rq.lpb_sizem1 = (DMA_BUFFER_LEN(pfvf->rbsize) / 8) - 1;
	aq->rq.xqe_imm_size = 0; /* Copying of packet to CQE not needed */
	aq->rq.flow_tagw = 32; /* Copy full 32bit flow_tag to CQE header */
	aq->rq.qint_idx = 0;
	aq->rq.lpb_drop_ena = 1; /* Enable RED dropping for AURA */
	aq->rq.xqe_drop_ena = 1; /* Enable RED dropping for CQ/SSO */
	aq->rq.xqe_pass = RQ_PASS_LVL_CQ(pfvf->hw.rq_skid, qset->rqe_cnt);
	aq->rq.xqe_drop = RQ_DROP_LVL_CQ(pfvf->hw.rq_skid, qset->rqe_cnt);
	aq->rq.lpb_aura_pass = RQ_PASS_LVL_AURA;
	aq->rq.lpb_aura_drop = RQ_DROP_LVL_AURA;

	/* Fill AQ info */
	aq->qidx = qidx;
	aq->ctype = NIX_AQ_CTYPE_RQ;
	aq->op = NIX_AQ_INSTOP_INIT;

	return otx2_sync_mbox_msg(&pfvf->mbox);
}

int otx2_sq_aq_init(void *dev, u16 qidx, u16 sqb_aura)
{
	struct otx2_nic *pfvf = dev;
	struct otx2_snd_queue *sq;
	struct nix_aq_enq_req *aq;

	sq = &pfvf->qset.sq[qidx];
	sq->lmt_addr = (__force u64 *)(pfvf->reg_base + LMT_LF_LMTLINEX(qidx));
	/* Get memory to put this msg */
	aq = otx2_mbox_alloc_msg_nix_aq_enq(&pfvf->mbox);
	if (!aq)
		return -ENOMEM;

	aq->sq.cq = pfvf->hw.rx_queues + qidx;
	aq->sq.max_sqe_size = NIX_MAXSQESZ_W16; /* 128 byte */
	aq->sq.cq_ena = 1;
	aq->sq.ena = 1;
	aq->sq.smq = otx2_get_smq_idx(pfvf, qidx);
	aq->sq.smq_rr_quantum = mtu_to_dwrr_weight(pfvf, pfvf->tx_max_pktlen);
	aq->sq.default_chan = pfvf->hw.tx_chan_base;
	aq->sq.sqe_stype = NIX_STYPE_STF; /* Cache SQB */
	aq->sq.sqb_aura = sqb_aura;
	aq->sq.sq_int_ena = NIX_SQINT_BITS;
	aq->sq.qint_idx = 0;
	/* Due pipelining impact minimum 2000 unused SQ CQE's
	 * need to maintain to avoid CQ overflow.
	 */
	aq->sq.cq_limit = ((SEND_CQ_SKID * 256) / (pfvf->qset.sqe_cnt));

	/* Fill AQ info */
	aq->qidx = qidx;
	aq->ctype = NIX_AQ_CTYPE_SQ;
	aq->op = NIX_AQ_INSTOP_INIT;

	return otx2_sync_mbox_msg(&pfvf->mbox);
}

static int otx2_sq_init(struct otx2_nic *pfvf, u16 qidx, u16 sqb_aura)
{
	struct otx2_qset *qset = &pfvf->qset;
	struct otx2_snd_queue *sq;
	struct otx2_pool *pool;
	int err;

	pool = &pfvf->qset.pool[sqb_aura];
	sq = &qset->sq[qidx];
	sq->sqe_size = NIX_SQESZ_W16 ? 64 : 128;
	sq->sqe_cnt = qset->sqe_cnt;

	err = qmem_alloc(pfvf->dev, &sq->sqe, 1, sq->sqe_size);
	if (err)
		return err;

	if (qidx < pfvf->hw.tx_queues) {
		err = qmem_alloc(pfvf->dev, &sq->tso_hdrs, qset->sqe_cnt,
				 TSO_HEADER_SIZE);
		if (err)
			return err;
	}

	sq->sqe_base = sq->sqe->base;
	sq->sg = kcalloc(qset->sqe_cnt, sizeof(struct sg_list), GFP_KERNEL);
	if (!sq->sg)
		return -ENOMEM;

	if (pfvf->ptp && qidx < pfvf->hw.tx_queues) {
		err = qmem_alloc(pfvf->dev, &sq->timestamps, qset->sqe_cnt,
				 sizeof(*sq->timestamps));
		if (err)
			return err;
	}

	sq->head = 0;
	sq->cons_head = 0;
	sq->sqe_per_sqb = (pfvf->hw.sqb_size / sq->sqe_size) - 1;
	sq->num_sqbs = (qset->sqe_cnt + sq->sqe_per_sqb) / sq->sqe_per_sqb;
	/* Set SQE threshold to 10% of total SQEs */
	sq->sqe_thresh = ((sq->num_sqbs * sq->sqe_per_sqb) * 10) / 100;
	sq->aura_id = sqb_aura;
	sq->aura_fc_addr = pool->fc_addr->base;
	sq->io_addr = (__force u64)otx2_get_regaddr(pfvf, NIX_LF_OP_SENDX(0));

	sq->stats.bytes = 0;
	sq->stats.pkts = 0;

	return pfvf->hw_ops->sq_aq_init(pfvf, qidx, sqb_aura);

}

static int otx2_cq_init(struct otx2_nic *pfvf, u16 qidx)
{
	struct otx2_qset *qset = &pfvf->qset;
	int err, pool_id, non_xdp_queues;
	struct nix_aq_enq_req *aq;
	struct otx2_cq_queue *cq;

	cq = &qset->cq[qidx];
	cq->cq_idx = qidx;
	non_xdp_queues = pfvf->hw.rx_queues + pfvf->hw.tx_queues;
	if (qidx < pfvf->hw.rx_queues) {
		cq->cq_type = CQ_RX;
		cq->cint_idx = qidx;
		cq->cqe_cnt = qset->rqe_cnt;
		if (pfvf->xdp_prog)
			xdp_rxq_info_reg(&cq->xdp_rxq, pfvf->netdev, qidx, 0);
	} else if (qidx < non_xdp_queues) {
		cq->cq_type = CQ_TX;
		cq->cint_idx = qidx - pfvf->hw.rx_queues;
		cq->cqe_cnt = qset->sqe_cnt;
	} else {
		cq->cq_type = CQ_XDP;
		cq->cint_idx = qidx - non_xdp_queues;
		cq->cqe_cnt = qset->sqe_cnt;
	}
	cq->cqe_size = pfvf->qset.xqe_size;

	/* Allocate memory for CQEs */
	err = qmem_alloc(pfvf->dev, &cq->cqe, cq->cqe_cnt, cq->cqe_size);
	if (err)
		return err;

	/* Save CQE CPU base for faster reference */
	cq->cqe_base = cq->cqe->base;
	/* In case where all RQs auras point to single pool,
	 * all CQs receive buffer pool also point to same pool.
	 */
	pool_id = ((cq->cq_type == CQ_RX) &&
		   (pfvf->hw.rqpool_cnt != pfvf->hw.rx_queues)) ? 0 : qidx;
	cq->rbpool = &qset->pool[pool_id];
	cq->refill_task_sched = false;

	/* Get memory to put this msg */
	aq = otx2_mbox_alloc_msg_nix_aq_enq(&pfvf->mbox);
	if (!aq)
		return -ENOMEM;

	aq->cq.ena = 1;
	aq->cq.qsize = Q_SIZE(cq->cqe_cnt, 4);
	aq->cq.caching = 1;
	aq->cq.base = cq->cqe->iova;
	aq->cq.cint_idx = cq->cint_idx;
	aq->cq.cq_err_int_ena = NIX_CQERRINT_BITS;
	aq->cq.qint_idx = 0;
	aq->cq.avg_level = 255;

	if (qidx < pfvf->hw.rx_queues) {
		aq->cq.drop = RQ_DROP_LVL_CQ(pfvf->hw.rq_skid, cq->cqe_cnt);
		aq->cq.drop_ena = 1;

		if (!is_otx2_lbkvf(pfvf->pdev)) {
			/* Enable receive CQ backpressure */
			aq->cq.bp_ena = 1;
#ifdef CONFIG_DCB
			aq->cq.bpid = pfvf->bpid[pfvf->queue_to_pfc_map[qidx]];
#else
			aq->cq.bpid = pfvf->bpid[0];
#endif

			/* Set backpressure level is same as cq pass level */
			aq->cq.bp = RQ_PASS_LVL_CQ(pfvf->hw.rq_skid, qset->rqe_cnt);
		}
	}

	/* Fill AQ info */
	aq->qidx = qidx;
	aq->ctype = NIX_AQ_CTYPE_CQ;
	aq->op = NIX_AQ_INSTOP_INIT;

	return otx2_sync_mbox_msg(&pfvf->mbox);
}

static void otx2_pool_refill_task(struct work_struct *work)
{
	struct otx2_cq_queue *cq;
	struct otx2_pool *rbpool;
	struct refill_work *wrk;
	int qidx, free_ptrs = 0;
	struct otx2_nic *pfvf;
	dma_addr_t bufptr;

	wrk = container_of(work, struct refill_work, pool_refill_work.work);
	pfvf = wrk->pf;
	qidx = wrk - pfvf->refill_wrk;
	cq = &pfvf->qset.cq[qidx];
	rbpool = cq->rbpool;
	free_ptrs = cq->pool_ptrs;

	while (cq->pool_ptrs) {
		if (otx2_alloc_rbuf(pfvf, rbpool, &bufptr)) {
			/* Schedule a WQ if we fails to free atleast half of the
			 * pointers else enable napi for this RQ.
			 */
			if (!((free_ptrs - cq->pool_ptrs) > free_ptrs / 2)) {
				struct delayed_work *dwork;

				dwork = &wrk->pool_refill_work;
				schedule_delayed_work(dwork,
						      msecs_to_jiffies(100));
			} else {
				cq->refill_task_sched = false;
			}
			return;
		}
		pfvf->hw_ops->aura_freeptr(pfvf, qidx, bufptr + OTX2_HEAD_ROOM);
		cq->pool_ptrs--;
	}
	cq->refill_task_sched = false;
}

int otx2_config_nix_queues(struct otx2_nic *pfvf)
{
	int qidx, err;

	/* Initialize RX queues */
	for (qidx = 0; qidx < pfvf->hw.rx_queues; qidx++) {
		u16 lpb_aura = otx2_get_pool_idx(pfvf, AURA_NIX_RQ, qidx);

		err = otx2_rq_init(pfvf, qidx, lpb_aura);
		if (err)
			return err;
	}

	/* Initialize TX queues */
	for (qidx = 0; qidx < pfvf->hw.non_qos_queues; qidx++) {
		u16 sqb_aura = otx2_get_pool_idx(pfvf, AURA_NIX_SQ, qidx);

		err = otx2_sq_init(pfvf, qidx, sqb_aura);
		if (err)
			return err;
	}

	/* Initialize completion queues */
	for (qidx = 0; qidx < pfvf->qset.cq_cnt; qidx++) {
		err = otx2_cq_init(pfvf, qidx);
		if (err)
			return err;
	}

	pfvf->cq_op_addr = (__force u64 *)otx2_get_regaddr(pfvf,
							   NIX_LF_CQ_OP_STATUS);

	/* Initialize work queue for receive buffer refill */
	pfvf->refill_wrk = devm_kcalloc(pfvf->dev, pfvf->qset.cq_cnt,
					sizeof(struct refill_work), GFP_KERNEL);
	if (!pfvf->refill_wrk)
		return -ENOMEM;

	for (qidx = 0; qidx < pfvf->qset.cq_cnt; qidx++) {
		pfvf->refill_wrk[qidx].pf = pfvf;
		INIT_DELAYED_WORK(&pfvf->refill_wrk[qidx].pool_refill_work,
				  otx2_pool_refill_task);
	}
	return 0;
}

int otx2_config_nix(struct otx2_nic *pfvf)
{
	struct nix_lf_alloc_req  *nixlf;
	struct nix_lf_alloc_rsp *rsp;
	int err;

	pfvf->qset.xqe_size = pfvf->hw.xqe_size;

	/* Get memory to put this msg */
	nixlf = otx2_mbox_alloc_msg_nix_lf_alloc(&pfvf->mbox);
	if (!nixlf)
		return -ENOMEM;

	/* Set RQ/SQ/CQ counts */
	nixlf->rq_cnt = pfvf->hw.rx_queues;
	nixlf->sq_cnt = pfvf->hw.non_qos_queues;
	nixlf->cq_cnt = pfvf->qset.cq_cnt;
	nixlf->rss_sz = MAX_RSS_INDIR_TBL_SIZE;
	nixlf->rss_grps = MAX_RSS_GROUPS;
	nixlf->xqe_sz = pfvf->hw.xqe_size == 128 ? NIX_XQESZ_W16 : NIX_XQESZ_W64;
	/* We don't know absolute NPA LF idx attached.
	 * AF will replace 'RVU_DEFAULT_PF_FUNC' with
	 * NPA LF attached to this RVU PF/VF.
	 */
	nixlf->npa_func = RVU_DEFAULT_PF_FUNC;
	/* Disable alignment pad, enable L2 length check,
	 * enable L4 TCP/UDP checksum verification.
	 */
	nixlf->rx_cfg = BIT_ULL(33) | BIT_ULL(35) | BIT_ULL(37);

	err = otx2_sync_mbox_msg(&pfvf->mbox);
	if (err)
		return err;

	rsp = (struct nix_lf_alloc_rsp *)otx2_mbox_get_rsp(&pfvf->mbox.mbox, 0,
							   &nixlf->hdr);
	if (IS_ERR(rsp))
		return PTR_ERR(rsp);

	if (rsp->qints < 1)
		return -ENXIO;

	return rsp->hdr.rc;
}

void otx2_sq_free_sqbs(struct otx2_nic *pfvf)
{
	struct otx2_qset *qset = &pfvf->qset;
	struct otx2_hw *hw = &pfvf->hw;
	struct otx2_snd_queue *sq;
	int sqb, qidx;
	u64 iova, pa;

	for (qidx = 0; qidx < hw->non_qos_queues; qidx++) {
		sq = &qset->sq[qidx];
		if (!sq->sqb_ptrs)
			continue;
		for (sqb = 0; sqb < sq->sqb_count; sqb++) {
			if (!sq->sqb_ptrs[sqb])
				continue;
			iova = sq->sqb_ptrs[sqb];
			pa = otx2_iova_to_phys(pfvf->iommu_domain, iova);
			dma_unmap_page_attrs(pfvf->dev, iova, hw->sqb_size,
					     DMA_FROM_DEVICE,
					     DMA_ATTR_SKIP_CPU_SYNC);
			put_page(virt_to_page(phys_to_virt(pa)));
		}
		sq->sqb_count = 0;
	}
}

void otx2_free_aura_ptr(struct otx2_nic *pfvf, int type)
{
	int pool_id, pool_start = 0, pool_end = 0, size = 0;
	u64 iova, pa;

	if (type == AURA_NIX_SQ) {
		pool_start = otx2_get_pool_idx(pfvf, type, 0);
		pool_end =  pool_start + pfvf->hw.sqpool_cnt;
		size = pfvf->hw.sqb_size;
	}
	if (type == AURA_NIX_RQ) {
		pool_start = otx2_get_pool_idx(pfvf, type, 0);
		pool_end = pfvf->hw.rqpool_cnt;
		size = pfvf->rbsize;
	}

	/* Free SQB and RQB pointers from the aura pool */
	for (pool_id = pool_start; pool_id < pool_end; pool_id++) {
		iova = otx2_aura_allocptr(pfvf, pool_id);
		while (iova) {
			if (type == AURA_NIX_RQ)
				iova -= OTX2_HEAD_ROOM;

			pa = otx2_iova_to_phys(pfvf->iommu_domain, iova);
			dma_unmap_page_attrs(pfvf->dev, iova, size,
					     DMA_FROM_DEVICE,
					     DMA_ATTR_SKIP_CPU_SYNC);
			put_page(virt_to_page(phys_to_virt(pa)));
			iova = otx2_aura_allocptr(pfvf, pool_id);
		}
	}
}

void otx2_aura_pool_free(struct otx2_nic *pfvf)
{
	struct otx2_pool *pool;
	int pool_id;

	if (!pfvf->qset.pool)
		return;

	for (pool_id = 0; pool_id < pfvf->hw.pool_cnt; pool_id++) {
		pool = &pfvf->qset.pool[pool_id];
		qmem_free(pfvf->dev, pool->stack);
		qmem_free(pfvf->dev, pool->fc_addr);
	}
	devm_kfree(pfvf->dev, pfvf->qset.pool);
	pfvf->qset.pool = NULL;
}

static int otx2_aura_init(struct otx2_nic *pfvf, int aura_id,
			  int pool_id, int numptrs)
{
	struct npa_aq_enq_req *aq;
	struct otx2_pool *pool;
	int err;

	pool = &pfvf->qset.pool[pool_id];

	/* Allocate memory for HW to update Aura count.
	 * Alloc one cache line, so that it fits all FC_STYPE modes.
	 */
	if (!pool->fc_addr) {
		err = qmem_alloc(pfvf->dev, &pool->fc_addr, 1, OTX2_ALIGN);
		if (err)
			return err;
	}

	/* Initialize this aura's context via AF */
	aq = otx2_mbox_alloc_msg_npa_aq_enq(&pfvf->mbox);
	if (!aq) {
		/* Shared mbox memory buffer is full, flush it and retry */
		err = otx2_sync_mbox_msg(&pfvf->mbox);
		if (err)
			return err;
		aq = otx2_mbox_alloc_msg_npa_aq_enq(&pfvf->mbox);
		if (!aq)
			return -ENOMEM;
	}

	aq->aura_id = aura_id;
	/* Will be filled by AF with correct pool context address */
	aq->aura.pool_addr = pool_id;
	aq->aura.pool_caching = 1;
	aq->aura.shift = ilog2(numptrs) - 8;
	aq->aura.count = numptrs;
	aq->aura.limit = numptrs;
	aq->aura.avg_level = 255;
	aq->aura.ena = 1;
	aq->aura.fc_ena = 1;
	aq->aura.fc_addr = pool->fc_addr->iova;
	aq->aura.fc_hyst_bits = 0; /* Store count on all updates */

	/* Enable backpressure for RQ aura */
	if (aura_id < pfvf->hw.rqpool_cnt && !is_otx2_lbkvf(pfvf->pdev)) {
		aq->aura.bp_ena = 0;
		/* If NIX1 LF is attached then specify NIX1_RX.
		 *
		 * Below NPA_AURA_S[BP_ENA] is set according to the
		 * NPA_BPINTF_E enumeration given as:
		 * 0x0 + a*0x1 where 'a' is 0 for NIX0_RX and 1 for NIX1_RX so
		 * NIX0_RX is 0x0 + 0*0x1 = 0
		 * NIX1_RX is 0x0 + 1*0x1 = 1
		 * But in HRM it is given that
		 * "NPA_AURA_S[BP_ENA](w1[33:32]) - Enable aura backpressure to
		 * NIX-RX based on [BP] level. One bit per NIX-RX; index
		 * enumerated by NPA_BPINTF_E."
		 */
		if (pfvf->nix_blkaddr == BLKADDR_NIX1)
			aq->aura.bp_ena = 1;
#ifdef CONFIG_DCB
		aq->aura.nix0_bpid = pfvf->bpid[pfvf->queue_to_pfc_map[aura_id]];
#else
		aq->aura.nix0_bpid = pfvf->bpid[0];
#endif

		/* Set backpressure level for RQ's Aura */
		aq->aura.bp = RQ_BP_LVL_AURA;
	}

	/* Fill AQ info */
	aq->ctype = NPA_AQ_CTYPE_AURA;
	aq->op = NPA_AQ_INSTOP_INIT;

	return 0;
}

static int otx2_pool_init(struct otx2_nic *pfvf, u16 pool_id,
			  int stack_pages, int numptrs, int buf_size)
{
	struct npa_aq_enq_req *aq;
	struct otx2_pool *pool;
	int err;

	pool = &pfvf->qset.pool[pool_id];
	/* Alloc memory for stack which is used to store buffer pointers */
	err = qmem_alloc(pfvf->dev, &pool->stack,
			 stack_pages, pfvf->hw.stack_pg_bytes);
	if (err)
		return err;

	pool->rbsize = buf_size;

	/* Initialize this pool's context via AF */
	aq = otx2_mbox_alloc_msg_npa_aq_enq(&pfvf->mbox);
	if (!aq) {
		/* Shared mbox memory buffer is full, flush it and retry */
		err = otx2_sync_mbox_msg(&pfvf->mbox);
		if (err) {
			qmem_free(pfvf->dev, pool->stack);
			return err;
		}
		aq = otx2_mbox_alloc_msg_npa_aq_enq(&pfvf->mbox);
		if (!aq) {
			qmem_free(pfvf->dev, pool->stack);
			return -ENOMEM;
		}
	}

	aq->aura_id = pool_id;
	aq->pool.stack_base = pool->stack->iova;
	aq->pool.stack_caching = 1;
	aq->pool.ena = 1;
	aq->pool.buf_size = buf_size / 128;
	aq->pool.stack_max_pages = stack_pages;
	aq->pool.shift = ilog2(numptrs) - 8;
	aq->pool.ptr_start = 0;
	aq->pool.ptr_end = ~0ULL;

	/* Fill AQ info */
	aq->ctype = NPA_AQ_CTYPE_POOL;
	aq->op = NPA_AQ_INSTOP_INIT;

	return 0;
}

int otx2_sq_aura_pool_init(struct otx2_nic *pfvf)
{
	int qidx, pool_id, stack_pages, num_sqbs;
	struct otx2_qset *qset = &pfvf->qset;
	struct otx2_hw *hw = &pfvf->hw;
	struct otx2_snd_queue *sq;
	struct otx2_pool *pool;
	dma_addr_t bufptr;
	int err, ptr;

	/* Calculate number of SQBs needed.
	 *
	 * For a 128byte SQE, and 4K size SQB, 31 SQEs will fit in one SQB.
	 * Last SQE is used for pointing to next SQB.
	 */
	num_sqbs = (hw->sqb_size / 128) - 1;
	num_sqbs = (qset->sqe_cnt + num_sqbs) / num_sqbs;

	/* Get no of stack pages needed */
	stack_pages =
		(num_sqbs + hw->stack_pg_ptrs - 1) / hw->stack_pg_ptrs;

	for (qidx = 0; qidx < hw->non_qos_queues; qidx++) {
		pool_id = otx2_get_pool_idx(pfvf, AURA_NIX_SQ, qidx);
		/* Initialize aura context */
		err = otx2_aura_init(pfvf, pool_id, pool_id, num_sqbs);
		if (err)
			goto fail;

		/* Initialize pool context */
		err = otx2_pool_init(pfvf, pool_id, stack_pages,
				     num_sqbs, hw->sqb_size);
		if (err)
			goto fail;
	}

	/* Flush accumulated messages */
	err = otx2_sync_mbox_msg(&pfvf->mbox);
	if (err)
		goto fail;

	/* Allocate pointers and free them to aura/pool */
	for (qidx = 0; qidx < hw->non_qos_queues; qidx++) {
		pool_id = otx2_get_pool_idx(pfvf, AURA_NIX_SQ, qidx);
		pool = &pfvf->qset.pool[pool_id];

		sq = &qset->sq[qidx];
		sq->sqb_count = 0;
		sq->sqb_ptrs = kcalloc(num_sqbs, sizeof(*sq->sqb_ptrs), GFP_KERNEL);
		if (!sq->sqb_ptrs) {
			err = -ENOMEM;
			goto err_mem;
		}

		for (ptr = 0; ptr < num_sqbs; ptr++) {
			err = otx2_alloc_rbuf(pfvf, pool, &bufptr);
			if (err)
				goto err_mem;
			pfvf->hw_ops->aura_freeptr(pfvf, pool_id, bufptr);
			sq->sqb_ptrs[sq->sqb_count++] = (u64)bufptr;
		}
	}

err_mem:
	return err ? -ENOMEM : 0;

fail:
	otx2_mbox_reset(&pfvf->mbox.mbox, 0);
	otx2_aura_pool_free(pfvf);
	return err;
}

int otx2_rq_aura_pool_init(struct otx2_nic *pfvf)
{
	struct otx2_hw *hw = &pfvf->hw;
	int stack_pages, pool_id, rq;
	struct otx2_pool *pool;
	int err, ptr, num_ptrs;
	dma_addr_t bufptr;

	num_ptrs = pfvf->qset.rqe_cnt;

	stack_pages =
		(num_ptrs + hw->stack_pg_ptrs - 1) / hw->stack_pg_ptrs;

	for (rq = 0; rq < hw->rx_queues; rq++) {
		pool_id = otx2_get_pool_idx(pfvf, AURA_NIX_RQ, rq);
		/* Initialize aura context */
		err = otx2_aura_init(pfvf, pool_id, pool_id, num_ptrs);
		if (err)
			goto fail;
	}
	for (pool_id = 0; pool_id < hw->rqpool_cnt; pool_id++) {
		err = otx2_pool_init(pfvf, pool_id, stack_pages,
				     num_ptrs, pfvf->rbsize);
		if (err)
			goto fail;
	}

	/* Flush accumulated messages */
	err = otx2_sync_mbox_msg(&pfvf->mbox);
	if (err)
		goto fail;

	/* Allocate pointers and free them to aura/pool */
	for (pool_id = 0; pool_id < hw->rqpool_cnt; pool_id++) {
		pool = &pfvf->qset.pool[pool_id];
		for (ptr = 0; ptr < num_ptrs; ptr++) {
			err = otx2_alloc_rbuf(pfvf, pool, &bufptr);
			if (err)
				return -ENOMEM;
			pfvf->hw_ops->aura_freeptr(pfvf, pool_id,
						   bufptr + OTX2_HEAD_ROOM);
		}
	}
	return 0;
fail:
	otx2_mbox_reset(&pfvf->mbox.mbox, 0);
	otx2_aura_pool_free(pfvf);
	return err;
}

int otx2_config_npa(struct otx2_nic *pfvf)
{
	struct otx2_qset *qset = &pfvf->qset;
	struct npa_lf_alloc_req  *npalf;
	struct otx2_hw *hw = &pfvf->hw;
	int aura_cnt;

	/* Pool - Stack of free buffer pointers
	 * Aura - Alloc/frees pointers from/to pool for NIX DMA.
	 */

	if (!hw->pool_cnt)
		return -EINVAL;

	qset->pool = devm_kcalloc(pfvf->dev, hw->pool_cnt,
				  sizeof(struct otx2_pool), GFP_KERNEL);
	if (!qset->pool)
		return -ENOMEM;

	/* Get memory to put this msg */
	npalf = otx2_mbox_alloc_msg_npa_lf_alloc(&pfvf->mbox);
	if (!npalf)
		return -ENOMEM;

	/* Set aura and pool counts */
	npalf->nr_pools = hw->pool_cnt;
	aura_cnt = ilog2(roundup_pow_of_two(hw->pool_cnt));
	npalf->aura_sz = (aura_cnt >= ilog2(128)) ? (aura_cnt - 6) : 1;

	return otx2_sync_mbox_msg(&pfvf->mbox);
}

int otx2_detach_resources(struct mbox *mbox)
{
	struct rsrc_detach *detach;

	mutex_lock(&mbox->lock);
	detach = otx2_mbox_alloc_msg_detach_resources(mbox);
	if (!detach) {
		mutex_unlock(&mbox->lock);
		return -ENOMEM;
	}

	/* detach all */
	detach->partial = false;

	/* Send detach request to AF */
	otx2_mbox_msg_send(&mbox->mbox, 0);
	mutex_unlock(&mbox->lock);
	return 0;
}
EXPORT_SYMBOL(otx2_detach_resources);

int otx2_attach_npa_nix(struct otx2_nic *pfvf)
{
	struct rsrc_attach *attach;
	struct msg_req *msix;
	int err;

	mutex_lock(&pfvf->mbox.lock);
	/* Get memory to put this msg */
	attach = otx2_mbox_alloc_msg_attach_resources(&pfvf->mbox);
	if (!attach) {
		mutex_unlock(&pfvf->mbox.lock);
		return -ENOMEM;
	}

	attach->npalf = true;
	attach->nixlf = true;

	/* Send attach request to AF */
	err = otx2_sync_mbox_msg(&pfvf->mbox);
	if (err) {
		mutex_unlock(&pfvf->mbox.lock);
		return err;
	}

	pfvf->nix_blkaddr = BLKADDR_NIX0;

	/* If the platform has two NIX blocks then LF may be
	 * allocated from NIX1.
	 */
	if (otx2_read64(pfvf, RVU_PF_BLOCK_ADDRX_DISC(BLKADDR_NIX1)) & 0x1FFULL)
		pfvf->nix_blkaddr = BLKADDR_NIX1;

	/* Get NPA and NIX MSIX vector offsets */
	msix = otx2_mbox_alloc_msg_msix_offset(&pfvf->mbox);
	if (!msix) {
		mutex_unlock(&pfvf->mbox.lock);
		return -ENOMEM;
	}

	err = otx2_sync_mbox_msg(&pfvf->mbox);
	if (err) {
		mutex_unlock(&pfvf->mbox.lock);
		return err;
	}
	mutex_unlock(&pfvf->mbox.lock);

	if (pfvf->hw.npa_msixoff == MSIX_VECTOR_INVALID ||
	    pfvf->hw.nix_msixoff == MSIX_VECTOR_INVALID) {
		dev_err(pfvf->dev,
			"RVUPF: Invalid MSIX vector offset for NPA/NIX\n");
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(otx2_attach_npa_nix);

void otx2_ctx_disable(struct mbox *mbox, int type, bool npa)
{
	struct hwctx_disable_req *req;

	mutex_lock(&mbox->lock);
	/* Request AQ to disable this context */
	if (npa)
		req = otx2_mbox_alloc_msg_npa_hwctx_disable(mbox);
	else
		req = otx2_mbox_alloc_msg_nix_hwctx_disable(mbox);

	if (!req) {
		mutex_unlock(&mbox->lock);
		return;
	}

	req->ctype = type;

	if (otx2_sync_mbox_msg(mbox))
		dev_err(mbox->pfvf->dev, "%s failed to disable context\n",
			__func__);

	mutex_unlock(&mbox->lock);
}

int otx2_nix_config_bp(struct otx2_nic *pfvf, bool enable)
{
	struct nix_bp_cfg_req *req;

	if (enable)
		req = otx2_mbox_alloc_msg_nix_bp_enable(&pfvf->mbox);
	else
		req = otx2_mbox_alloc_msg_nix_bp_disable(&pfvf->mbox);

	if (!req)
		return -ENOMEM;

	req->chan_base = 0;
#ifdef CONFIG_DCB
	req->chan_cnt = pfvf->pfc_en ? IEEE_8021QAZ_MAX_TCS : 1;
	req->bpid_per_chan = pfvf->pfc_en ? 1 : 0;
#else
	req->chan_cnt =  1;
	req->bpid_per_chan = 0;
#endif


	return otx2_sync_mbox_msg(&pfvf->mbox);
}
EXPORT_SYMBOL(otx2_nix_config_bp);

/* Mbox message handlers */
void mbox_handler_cgx_stats(struct otx2_nic *pfvf,
			    struct cgx_stats_rsp *rsp)
{
	int id;

	for (id = 0; id < CGX_RX_STATS_COUNT; id++)
		pfvf->hw.cgx_rx_stats[id] = rsp->rx_stats[id];
	for (id = 0; id < CGX_TX_STATS_COUNT; id++)
		pfvf->hw.cgx_tx_stats[id] = rsp->tx_stats[id];
}

void mbox_handler_cgx_fec_stats(struct otx2_nic *pfvf,
				struct cgx_fec_stats_rsp *rsp)
{
	pfvf->hw.cgx_fec_corr_blks += rsp->fec_corr_blks;
	pfvf->hw.cgx_fec_uncorr_blks += rsp->fec_uncorr_blks;
}

void mbox_handler_npa_lf_alloc(struct otx2_nic *pfvf,
			       struct npa_lf_alloc_rsp *rsp)
{
	pfvf->hw.stack_pg_ptrs = rsp->stack_pg_ptrs;
	pfvf->hw.stack_pg_bytes = rsp->stack_pg_bytes;
}
EXPORT_SYMBOL(mbox_handler_npa_lf_alloc);

void mbox_handler_nix_lf_alloc(struct otx2_nic *pfvf,
			       struct nix_lf_alloc_rsp *rsp)
{
	pfvf->hw.sqb_size = rsp->sqb_size;
	pfvf->hw.rx_chan_base = rsp->rx_chan_base;
	pfvf->hw.tx_chan_base = rsp->tx_chan_base;
	pfvf->hw.lso_tsov4_idx = rsp->lso_tsov4_idx;
	pfvf->hw.lso_tsov6_idx = rsp->lso_tsov6_idx;
	pfvf->hw.cgx_links = rsp->cgx_links;
	pfvf->hw.lbk_links = rsp->lbk_links;
	pfvf->hw.tx_link = rsp->tx_link;
}
EXPORT_SYMBOL(mbox_handler_nix_lf_alloc);

void mbox_handler_msix_offset(struct otx2_nic *pfvf,
			      struct msix_offset_rsp *rsp)
{
	pfvf->hw.npa_msixoff = rsp->npa_msixoff;
	pfvf->hw.nix_msixoff = rsp->nix_msixoff;
}
EXPORT_SYMBOL(mbox_handler_msix_offset);

void mbox_handler_nix_bp_enable(struct otx2_nic *pfvf,
				struct nix_bp_cfg_rsp *rsp)
{
	int chan, chan_id;

	for (chan = 0; chan < rsp->chan_cnt; chan++) {
		chan_id = ((rsp->chan_bpid[chan] >> 10) & 0x7F);
		pfvf->bpid[chan_id] = rsp->chan_bpid[chan] & 0x3FF;
	}
}
EXPORT_SYMBOL(mbox_handler_nix_bp_enable);

void otx2_free_cints(struct otx2_nic *pfvf, int n)
{
	struct otx2_qset *qset = &pfvf->qset;
	struct otx2_hw *hw = &pfvf->hw;
	int irq, qidx;

	for (qidx = 0, irq = hw->nix_msixoff + NIX_LF_CINT_VEC_START;
	     qidx < n;
	     qidx++, irq++) {
		int vector = pci_irq_vector(pfvf->pdev, irq);

		irq_set_affinity_hint(vector, NULL);
		free_cpumask_var(hw->affinity_mask[irq]);
		free_irq(vector, &qset->napi[qidx]);
	}
}

void otx2_set_cints_affinity(struct otx2_nic *pfvf)
{
	struct otx2_hw *hw = &pfvf->hw;
	int vec, cpu, irq, cint;

	vec = hw->nix_msixoff + NIX_LF_CINT_VEC_START;
	cpu = cpumask_first(cpu_online_mask);

	/* CQ interrupts */
	for (cint = 0; cint < pfvf->hw.cint_cnt; cint++, vec++) {
		if (!alloc_cpumask_var(&hw->affinity_mask[vec], GFP_KERNEL))
			return;

		cpumask_set_cpu(cpu, hw->affinity_mask[vec]);

		irq = pci_irq_vector(pfvf->pdev, vec);
		irq_set_affinity_hint(irq, hw->affinity_mask[vec]);

		cpu = cpumask_next(cpu, cpu_online_mask);
		if (unlikely(cpu >= nr_cpu_ids))
			cpu = 0;
	}
}

u16 otx2_get_max_mtu(struct otx2_nic *pfvf)
{
	struct nix_hw_info *rsp;
	struct msg_req *req;
	u16 max_mtu;
	int rc;

	mutex_lock(&pfvf->mbox.lock);

	req = otx2_mbox_alloc_msg_nix_get_hw_info(&pfvf->mbox);
	if (!req) {
		rc =  -ENOMEM;
		goto out;
	}

	rc = otx2_sync_mbox_msg(&pfvf->mbox);
	if (!rc) {
		rsp = (struct nix_hw_info *)
		       otx2_mbox_get_rsp(&pfvf->mbox.mbox, 0, &req->hdr);

		/* HW counts VLAN insertion bytes (8 for double tag)
		 * irrespective of whether SQE is requesting to insert VLAN
		 * in the packet or not. Hence these 8 bytes have to be
		 * discounted from max packet size otherwise HW will throw
		 * SMQ errors
		 */
		max_mtu = rsp->max_mtu - 8 - OTX2_ETH_HLEN;

		/* Also save DWRR MTU, needed for DWRR weight calculation */
		pfvf->hw.dwrr_mtu = rsp->rpm_dwrr_mtu;
		if (!pfvf->hw.dwrr_mtu)
			pfvf->hw.dwrr_mtu = 1;
	}

out:
	mutex_unlock(&pfvf->mbox.lock);
	if (rc) {
		dev_warn(pfvf->dev,
			 "Failed to get MTU from hardware setting default value(1500)\n");
		max_mtu = 1500;
	}
	return max_mtu;
}
EXPORT_SYMBOL(otx2_get_max_mtu);

int otx2_handle_ntuple_tc_features(struct net_device *netdev, netdev_features_t features)
{
	netdev_features_t changed = features ^ netdev->features;
	struct otx2_nic *pfvf = netdev_priv(netdev);
	bool ntuple = !!(features & NETIF_F_NTUPLE);
	bool tc = !!(features & NETIF_F_HW_TC);

	if ((changed & NETIF_F_NTUPLE) && !ntuple)
		otx2_destroy_ntuple_flows(pfvf);

	if ((changed & NETIF_F_NTUPLE) && ntuple) {
		if (!pfvf->flow_cfg->max_flows) {
			netdev_err(netdev,
				   "Can't enable NTUPLE, MCAM entries not allocated\n");
			return -EINVAL;
		}
	}

	if ((changed & NETIF_F_HW_TC) && tc) {
		if (!pfvf->flow_cfg->max_flows) {
			netdev_err(netdev,
				   "Can't enable TC, MCAM entries not allocated\n");
			return -EINVAL;
		}
	}

	if ((changed & NETIF_F_HW_TC) && !tc &&
	    pfvf->flow_cfg && pfvf->flow_cfg->nr_flows) {
		netdev_err(netdev, "Can't disable TC hardware offload while flows are active\n");
		return -EBUSY;
	}

	if ((changed & NETIF_F_NTUPLE) && ntuple &&
	    (netdev->features & NETIF_F_HW_TC) && !(changed & NETIF_F_HW_TC)) {
		netdev_err(netdev,
			   "Can't enable NTUPLE when TC is active, disable TC and retry\n");
		return -EINVAL;
	}

	if ((changed & NETIF_F_HW_TC) && tc &&
	    (netdev->features & NETIF_F_NTUPLE) && !(changed & NETIF_F_NTUPLE)) {
		netdev_err(netdev,
			   "Can't enable TC when NTUPLE is active, disable NTUPLE and retry\n");
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(otx2_handle_ntuple_tc_features);

#define M(_name, _id, _fn_name, _req_type, _rsp_type)			\
int __weak								\
otx2_mbox_up_handler_ ## _fn_name(struct otx2_nic *pfvf,		\
				struct _req_type *req,			\
				struct _rsp_type *rsp)			\
{									\
	/* Nothing to do here */					\
	return 0;							\
}									\
EXPORT_SYMBOL(otx2_mbox_up_handler_ ## _fn_name);
MBOX_UP_CGX_MESSAGES
MBOX_UP_MCS_MESSAGES
#undef M
