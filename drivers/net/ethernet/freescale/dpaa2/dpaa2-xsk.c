// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2022 NXP
 */
#include <linux/filter.h>
#include <linux/compiler.h>
#include <linux/bpf_trace.h>
#include <net/xdp.h>
#include <net/xdp_sock_drv.h>

#include "dpaa2-eth.h"

static void dpaa2_eth_setup_consume_func(struct dpaa2_eth_priv *priv,
					 struct dpaa2_eth_channel *ch,
					 enum dpaa2_eth_fq_type type,
					 dpaa2_eth_consume_cb_t *consume)
{
	struct dpaa2_eth_fq *fq;
	int i;

	for (i = 0; i < priv->num_fqs; i++) {
		fq = &priv->fq[i];

		if (fq->type != type)
			continue;
		if (fq->channel != ch)
			continue;

		fq->consume = consume;
	}
}

static u32 dpaa2_xsk_run_xdp(struct dpaa2_eth_priv *priv,
			     struct dpaa2_eth_channel *ch,
			     struct dpaa2_eth_fq *rx_fq,
			     struct dpaa2_fd *fd, void *vaddr)
{
	dma_addr_t addr = dpaa2_fd_get_addr(fd);
	struct bpf_prog *xdp_prog;
	struct xdp_buff *xdp_buff;
	struct dpaa2_eth_swa *swa;
	u32 xdp_act = XDP_PASS;
	int err;

	xdp_prog = READ_ONCE(ch->xdp.prog);
	if (!xdp_prog)
		goto out;

	swa = (struct dpaa2_eth_swa *)(vaddr + DPAA2_ETH_RX_HWA_SIZE +
				       ch->xsk_pool->umem->headroom);
	xdp_buff = swa->xsk.xdp_buff;

	xdp_buff->data_hard_start = vaddr;
	xdp_buff->data = vaddr + dpaa2_fd_get_offset(fd);
	xdp_buff->data_end = xdp_buff->data + dpaa2_fd_get_len(fd);
	xdp_set_data_meta_invalid(xdp_buff);
	xdp_buff->rxq = &ch->xdp_rxq;

	xsk_buff_dma_sync_for_cpu(xdp_buff, ch->xsk_pool);
	xdp_act = bpf_prog_run_xdp(xdp_prog, xdp_buff);

	/* xdp.data pointer may have changed */
	dpaa2_fd_set_offset(fd, xdp_buff->data - vaddr);
	dpaa2_fd_set_len(fd, xdp_buff->data_end - xdp_buff->data);

	if (likely(xdp_act == XDP_REDIRECT)) {
		err = xdp_do_redirect(priv->net_dev, xdp_buff, xdp_prog);
		if (unlikely(err)) {
			ch->stats.xdp_drop++;
			dpaa2_eth_recycle_buf(priv, ch, addr);
		} else {
			ch->buf_count--;
			ch->stats.xdp_redirect++;
		}

		goto xdp_redir;
	}

	switch (xdp_act) {
	case XDP_PASS:
		break;
	case XDP_TX:
		dpaa2_eth_xdp_enqueue(priv, ch, fd, vaddr, rx_fq->flowid);
		break;
	default:
		bpf_warn_invalid_xdp_action(priv->net_dev, xdp_prog, xdp_act);
		fallthrough;
	case XDP_ABORTED:
		trace_xdp_exception(priv->net_dev, xdp_prog, xdp_act);
		fallthrough;
	case XDP_DROP:
		dpaa2_eth_recycle_buf(priv, ch, addr);
		ch->stats.xdp_drop++;
		break;
	}

xdp_redir:
	ch->xdp.res |= xdp_act;
out:
	return xdp_act;
}

/* Rx frame processing routine for the AF_XDP fast path */
static void dpaa2_xsk_rx(struct dpaa2_eth_priv *priv,
			 struct dpaa2_eth_channel *ch,
			 const struct dpaa2_fd *fd,
			 struct dpaa2_eth_fq *fq)
{
	dma_addr_t addr = dpaa2_fd_get_addr(fd);
	u8 fd_format = dpaa2_fd_get_format(fd);
	struct rtnl_link_stats64 *percpu_stats;
	u32 fd_length = dpaa2_fd_get_len(fd);
	struct sk_buff *skb;
	void *vaddr;
	u32 xdp_act;

	trace_dpaa2_rx_xsk_fd(priv->net_dev, fd);

	vaddr = dpaa2_iova_to_virt(priv->iommu_domain, addr);
	percpu_stats = this_cpu_ptr(priv->percpu_stats);

	if (fd_format != dpaa2_fd_single) {
		WARN_ON(priv->xdp_prog);
		/* AF_XDP doesn't support any other formats */
		goto err_frame_format;
	}

	xdp_act = dpaa2_xsk_run_xdp(priv, ch, fq, (struct dpaa2_fd *)fd, vaddr);
	if (xdp_act != XDP_PASS) {
		percpu_stats->rx_packets++;
		percpu_stats->rx_bytes += dpaa2_fd_get_len(fd);
		return;
	}

	/* Build skb */
	skb = dpaa2_eth_alloc_skb(priv, ch, fd, fd_length, vaddr);
	if (!skb)
		/* Nothing else we can do, recycle the buffer and
		 * drop the frame.
		 */
		goto err_alloc_skb;

	/* Send the skb to the Linux networking stack */
	dpaa2_eth_receive_skb(priv, ch, fd, vaddr, fq, percpu_stats, skb);

	return;

err_alloc_skb:
	dpaa2_eth_recycle_buf(priv, ch, addr);
err_frame_format:
	percpu_stats->rx_dropped++;
}

static void dpaa2_xsk_set_bp_per_qdbin(struct dpaa2_eth_priv *priv,
				       struct dpni_pools_cfg *pools_params)
{
	int curr_bp = 0, i, j;

	pools_params->pool_options = DPNI_POOL_ASSOC_QDBIN;
	for (i = 0; i < priv->num_bps; i++) {
		for (j = 0; j < priv->num_channels; j++)
			if (priv->bp[i] == priv->channel[j]->bp)
				pools_params->pools[curr_bp].priority_mask |= (1 << j);
		if (!pools_params->pools[curr_bp].priority_mask)
			continue;

		pools_params->pools[curr_bp].dpbp_id = priv->bp[i]->bpid;
		pools_params->pools[curr_bp].buffer_size = priv->rx_buf_size;
		pools_params->pools[curr_bp++].backup_pool = 0;
	}
	pools_params->num_dpbp = curr_bp;
}

static int dpaa2_xsk_disable_pool(struct net_device *dev, u16 qid)
{
	struct xsk_buff_pool *pool = xsk_get_pool_from_qid(dev, qid);
	struct dpaa2_eth_priv *priv = netdev_priv(dev);
	struct dpni_pools_cfg pools_params = { 0 };
	struct dpaa2_eth_channel *ch;
	int err;
	bool up;

	ch = priv->channel[qid];
	if (!ch->xsk_pool)
		return -EINVAL;

	up = netif_running(dev);
	if (up)
		dev_close(dev);

	xsk_pool_dma_unmap(pool, 0);
	err = xdp_rxq_info_reg_mem_model(&ch->xdp_rxq,
					 MEM_TYPE_PAGE_ORDER0, NULL);
	if (err)
		netdev_err(dev, "xsk_rxq_info_reg_mem_model() failed (err = %d)\n",
			   err);

	dpaa2_eth_free_dpbp(priv, ch->bp);

	ch->xsk_zc = false;
	ch->xsk_pool = NULL;
	ch->xsk_tx_pkts_sent = 0;
	ch->bp = priv->bp[DPAA2_ETH_DEFAULT_BP_IDX];

	dpaa2_eth_setup_consume_func(priv, ch, DPAA2_RX_FQ, dpaa2_eth_rx);

	dpaa2_xsk_set_bp_per_qdbin(priv, &pools_params);
	err = dpni_set_pools(priv->mc_io, 0, priv->mc_token, &pools_params);
	if (err)
		netdev_err(dev, "dpni_set_pools() failed\n");

	if (up) {
		err = dev_open(dev, NULL);
		if (err)
			return err;
	}

	return 0;
}

static int dpaa2_xsk_enable_pool(struct net_device *dev,
				 struct xsk_buff_pool *pool,
				 u16 qid)
{
	struct dpaa2_eth_priv *priv = netdev_priv(dev);
	struct dpni_pools_cfg pools_params = { 0 };
	struct dpaa2_eth_channel *ch;
	int err, err2;
	bool up;

	if (priv->dpni_attrs.wriop_version < DPAA2_WRIOP_VERSION(3, 0, 0)) {
		netdev_err(dev, "AF_XDP zero-copy not supported on devices <= WRIOP(3, 0, 0)\n");
		return -EOPNOTSUPP;
	}

	if (priv->dpni_attrs.num_queues > 8) {
		netdev_err(dev, "AF_XDP zero-copy not supported on DPNI with more then 8 queues\n");
		return -EOPNOTSUPP;
	}

	up = netif_running(dev);
	if (up)
		dev_close(dev);

	err = xsk_pool_dma_map(pool, priv->net_dev->dev.parent, 0);
	if (err) {
		netdev_err(dev, "xsk_pool_dma_map() failed (err = %d)\n",
			   err);
		goto err_dma_unmap;
	}

	ch = priv->channel[qid];
	err = xdp_rxq_info_reg_mem_model(&ch->xdp_rxq, MEM_TYPE_XSK_BUFF_POOL, NULL);
	if (err) {
		netdev_err(dev, "xdp_rxq_info_reg_mem_model() failed (err = %d)\n", err);
		goto err_mem_model;
	}
	xsk_pool_set_rxq_info(pool, &ch->xdp_rxq);

	priv->bp[priv->num_bps] = dpaa2_eth_allocate_dpbp(priv);
	if (IS_ERR(priv->bp[priv->num_bps])) {
		err = PTR_ERR(priv->bp[priv->num_bps]);
		goto err_bp_alloc;
	}
	ch->xsk_zc = true;
	ch->xsk_pool = pool;
	ch->bp = priv->bp[priv->num_bps++];

	dpaa2_eth_setup_consume_func(priv, ch, DPAA2_RX_FQ, dpaa2_xsk_rx);

	dpaa2_xsk_set_bp_per_qdbin(priv, &pools_params);
	err = dpni_set_pools(priv->mc_io, 0, priv->mc_token, &pools_params);
	if (err) {
		netdev_err(dev, "dpni_set_pools() failed\n");
		goto err_set_pools;
	}

	if (up) {
		err = dev_open(dev, NULL);
		if (err)
			return err;
	}

	return 0;

err_set_pools:
	err2 = dpaa2_xsk_disable_pool(dev, qid);
	if (err2)
		netdev_err(dev, "dpaa2_xsk_disable_pool() failed %d\n", err2);
err_bp_alloc:
	err2 = xdp_rxq_info_reg_mem_model(&priv->channel[qid]->xdp_rxq,
					  MEM_TYPE_PAGE_ORDER0, NULL);
	if (err2)
		netdev_err(dev, "xsk_rxq_info_reg_mem_model() failed with %d)\n", err2);
err_mem_model:
	xsk_pool_dma_unmap(pool, 0);
err_dma_unmap:
	if (up)
		dev_open(dev, NULL);

	return err;
}

int dpaa2_xsk_setup_pool(struct net_device *dev, struct xsk_buff_pool *pool, u16 qid)
{
	return pool ? dpaa2_xsk_enable_pool(dev, pool, qid) :
		      dpaa2_xsk_disable_pool(dev, qid);
}

int dpaa2_xsk_wakeup(struct net_device *dev, u32 qid, u32 flags)
{
	struct dpaa2_eth_priv *priv = netdev_priv(dev);
	struct dpaa2_eth_channel *ch = priv->channel[qid];

	if (!priv->link_state.up)
		return -ENETDOWN;

	if (!priv->xdp_prog)
		return -EINVAL;

	if (!ch->xsk_zc)
		return -EINVAL;

	/* We do not have access to a per channel SW interrupt, so instead we
	 * schedule a NAPI instance.
	 */
	if (!napi_if_scheduled_mark_missed(&ch->napi))
		napi_schedule(&ch->napi);

	return 0;
}

static int dpaa2_xsk_tx_build_fd(struct dpaa2_eth_priv *priv,
				 struct dpaa2_eth_channel *ch,
				 struct dpaa2_fd *fd,
				 struct xdp_desc *xdp_desc)
{
	struct device *dev = priv->net_dev->dev.parent;
	struct dpaa2_sg_entry *sgt;
	struct dpaa2_eth_swa *swa;
	void *sgt_buf = NULL;
	dma_addr_t sgt_addr;
	int sgt_buf_size;
	dma_addr_t addr;
	int err = 0;

	/* Prepare the HW SGT structure */
	sgt_buf_size = priv->tx_data_offset + sizeof(struct dpaa2_sg_entry);
	sgt_buf = dpaa2_eth_sgt_get(priv);
	if (unlikely(!sgt_buf))
		return -ENOMEM;
	sgt = (struct dpaa2_sg_entry *)(sgt_buf + priv->tx_data_offset);

	/* Get the address of the XSK Tx buffer */
	addr = xsk_buff_raw_get_dma(ch->xsk_pool, xdp_desc->addr);
	xsk_buff_raw_dma_sync_for_device(ch->xsk_pool, addr, xdp_desc->len);

	/* Fill in the HW SGT structure */
	dpaa2_sg_set_addr(sgt, addr);
	dpaa2_sg_set_len(sgt, xdp_desc->len);
	dpaa2_sg_set_final(sgt, true);

	/* Store the necessary info in the SGT buffer */
	swa = (struct dpaa2_eth_swa *)sgt_buf;
	swa->type = DPAA2_ETH_SWA_XSK;
	swa->xsk.sgt_size = sgt_buf_size;

	/* Separately map the SGT buffer */
	sgt_addr = dma_map_single(dev, sgt_buf, sgt_buf_size, DMA_BIDIRECTIONAL);
	if (unlikely(dma_mapping_error(dev, sgt_addr))) {
		err = -ENOMEM;
		goto sgt_map_failed;
	}

	/* Initialize FD fields */
	memset(fd, 0, sizeof(struct dpaa2_fd));
	dpaa2_fd_set_offset(fd, priv->tx_data_offset);
	dpaa2_fd_set_format(fd, dpaa2_fd_sg);
	dpaa2_fd_set_addr(fd, sgt_addr);
	dpaa2_fd_set_len(fd, xdp_desc->len);
	dpaa2_fd_set_ctrl(fd, FD_CTRL_PTA);

	return 0;

sgt_map_failed:
	dpaa2_eth_sgt_recycle(priv, sgt_buf);

	return err;
}

bool dpaa2_xsk_tx(struct dpaa2_eth_priv *priv,
		  struct dpaa2_eth_channel *ch)
{
	struct xdp_desc *xdp_descs = ch->xsk_pool->tx_descs;
	struct dpaa2_eth_drv_stats *percpu_extras;
	struct rtnl_link_stats64 *percpu_stats;
	int budget = DPAA2_ETH_TX_ZC_PER_NAPI;
	int total_enqueued, enqueued;
	int retries, max_retries;
	struct dpaa2_eth_fq *fq;
	struct dpaa2_fd *fds;
	int batch, i, err;

	percpu_stats = this_cpu_ptr(priv->percpu_stats);
	percpu_extras = this_cpu_ptr(priv->percpu_extras);
	fds = (this_cpu_ptr(priv->fd))->array;

	/* Use the FQ with the same idx as the affine CPU */
	fq = &priv->fq[ch->nctx.desired_cpu];

	batch = xsk_tx_peek_release_desc_batch(ch->xsk_pool, budget);
	if (!batch)
		return false;

	/* Create a FD for each XSK frame to be sent */
	for (i = 0; i < batch; i++) {
		err = dpaa2_xsk_tx_build_fd(priv, ch, &fds[i], &xdp_descs[i]);
		if (err) {
			batch = i;
			break;
		}

		trace_dpaa2_tx_xsk_fd(priv->net_dev, &fds[i]);
	}

	/* Enqueue all the created FDs */
	max_retries = batch * DPAA2_ETH_ENQUEUE_RETRIES;
	total_enqueued = 0;
	enqueued = 0;
	retries = 0;
	while (total_enqueued < batch && retries < max_retries) {
		err = priv->enqueue(priv, fq, &fds[total_enqueued], 0,
				    batch - total_enqueued, &enqueued);
		if (err == -EBUSY) {
			retries++;
			continue;
		}

		total_enqueued += enqueued;
	}
	percpu_extras->tx_portal_busy += retries;

	/* Update statistics */
	percpu_stats->tx_packets += total_enqueued;
	for (i = 0; i < total_enqueued; i++)
		percpu_stats->tx_bytes += dpaa2_fd_get_len(&fds[i]);
	for (i = total_enqueued; i < batch; i++) {
		dpaa2_eth_free_tx_fd(priv, ch, fq, &fds[i], false);
		percpu_stats->tx_errors++;
	}

	xsk_tx_release(ch->xsk_pool);

	return total_enqueued == budget ? true : false;
}
