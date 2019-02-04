// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2014-2016 Freescale Semiconductor Inc.
 * Copyright 2016-2017 NXP
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/etherdevice.h>
#include <linux/of_net.h>
#include <linux/interrupt.h>
#include <linux/msi.h>
#include <linux/kthread.h>
#include <linux/iommu.h>
#include <linux/net_tstamp.h>
#include <linux/fsl/mc.h>
#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <net/sock.h>

#include "dpaa2-eth.h"

/* CREATE_TRACE_POINTS only needs to be defined once. Other dpa files
 * using trace events only need to #include <trace/events/sched.h>
 */
#define CREATE_TRACE_POINTS
#include "dpaa2-eth-trace.h"

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Freescale Semiconductor, Inc");
MODULE_DESCRIPTION("Freescale DPAA2 Ethernet Driver");

static void *dpaa2_iova_to_virt(struct iommu_domain *domain,
				dma_addr_t iova_addr)
{
	phys_addr_t phys_addr;

	phys_addr = domain ? iommu_iova_to_phys(domain, iova_addr) : iova_addr;

	return phys_to_virt(phys_addr);
}

static void validate_rx_csum(struct dpaa2_eth_priv *priv,
			     u32 fd_status,
			     struct sk_buff *skb)
{
	skb_checksum_none_assert(skb);

	/* HW checksum validation is disabled, nothing to do here */
	if (!(priv->net_dev->features & NETIF_F_RXCSUM))
		return;

	/* Read checksum validation bits */
	if (!((fd_status & DPAA2_FAS_L3CV) &&
	      (fd_status & DPAA2_FAS_L4CV)))
		return;

	/* Inform the stack there's no need to compute L3/L4 csum anymore */
	skb->ip_summed = CHECKSUM_UNNECESSARY;
}

/* Free a received FD.
 * Not to be used for Tx conf FDs or on any other paths.
 */
static void free_rx_fd(struct dpaa2_eth_priv *priv,
		       const struct dpaa2_fd *fd,
		       void *vaddr)
{
	struct device *dev = priv->net_dev->dev.parent;
	dma_addr_t addr = dpaa2_fd_get_addr(fd);
	u8 fd_format = dpaa2_fd_get_format(fd);
	struct dpaa2_sg_entry *sgt;
	void *sg_vaddr;
	int i;

	/* If single buffer frame, just free the data buffer */
	if (fd_format == dpaa2_fd_single)
		goto free_buf;
	else if (fd_format != dpaa2_fd_sg)
		/* We don't support any other format */
		return;

	/* For S/G frames, we first need to free all SG entries
	 * except the first one, which was taken care of already
	 */
	sgt = vaddr + dpaa2_fd_get_offset(fd);
	for (i = 1; i < DPAA2_ETH_MAX_SG_ENTRIES; i++) {
		addr = dpaa2_sg_get_addr(&sgt[i]);
		sg_vaddr = dpaa2_iova_to_virt(priv->iommu_domain, addr);
		dma_unmap_page(dev, addr, DPAA2_ETH_RX_BUF_SIZE,
			       DMA_BIDIRECTIONAL);

		free_pages((unsigned long)sg_vaddr, 0);
		if (dpaa2_sg_is_final(&sgt[i]))
			break;
	}

free_buf:
	free_pages((unsigned long)vaddr, 0);
}

/* Build a linear skb based on a single-buffer frame descriptor */
static struct sk_buff *build_linear_skb(struct dpaa2_eth_channel *ch,
					const struct dpaa2_fd *fd,
					void *fd_vaddr)
{
	struct sk_buff *skb = NULL;
	u16 fd_offset = dpaa2_fd_get_offset(fd);
	u32 fd_length = dpaa2_fd_get_len(fd);

	ch->buf_count--;

	skb = build_skb(fd_vaddr, DPAA2_ETH_RX_BUF_RAW_SIZE);
	if (unlikely(!skb))
		return NULL;

	skb_reserve(skb, fd_offset);
	skb_put(skb, fd_length);

	return skb;
}

/* Build a non linear (fragmented) skb based on a S/G table */
static struct sk_buff *build_frag_skb(struct dpaa2_eth_priv *priv,
				      struct dpaa2_eth_channel *ch,
				      struct dpaa2_sg_entry *sgt)
{
	struct sk_buff *skb = NULL;
	struct device *dev = priv->net_dev->dev.parent;
	void *sg_vaddr;
	dma_addr_t sg_addr;
	u16 sg_offset;
	u32 sg_length;
	struct page *page, *head_page;
	int page_offset;
	int i;

	for (i = 0; i < DPAA2_ETH_MAX_SG_ENTRIES; i++) {
		struct dpaa2_sg_entry *sge = &sgt[i];

		/* NOTE: We only support SG entries in dpaa2_sg_single format,
		 * but this is the only format we may receive from HW anyway
		 */

		/* Get the address and length from the S/G entry */
		sg_addr = dpaa2_sg_get_addr(sge);
		sg_vaddr = dpaa2_iova_to_virt(priv->iommu_domain, sg_addr);
		dma_unmap_page(dev, sg_addr, DPAA2_ETH_RX_BUF_SIZE,
			       DMA_BIDIRECTIONAL);

		sg_length = dpaa2_sg_get_len(sge);

		if (i == 0) {
			/* We build the skb around the first data buffer */
			skb = build_skb(sg_vaddr, DPAA2_ETH_RX_BUF_RAW_SIZE);
			if (unlikely(!skb)) {
				/* Free the first SG entry now, since we already
				 * unmapped it and obtained the virtual address
				 */
				free_pages((unsigned long)sg_vaddr, 0);

				/* We still need to subtract the buffers used
				 * by this FD from our software counter
				 */
				while (!dpaa2_sg_is_final(&sgt[i]) &&
				       i < DPAA2_ETH_MAX_SG_ENTRIES)
					i++;
				break;
			}

			sg_offset = dpaa2_sg_get_offset(sge);
			skb_reserve(skb, sg_offset);
			skb_put(skb, sg_length);
		} else {
			/* Rest of the data buffers are stored as skb frags */
			page = virt_to_page(sg_vaddr);
			head_page = virt_to_head_page(sg_vaddr);

			/* Offset in page (which may be compound).
			 * Data in subsequent SG entries is stored from the
			 * beginning of the buffer, so we don't need to add the
			 * sg_offset.
			 */
			page_offset = ((unsigned long)sg_vaddr &
				(PAGE_SIZE - 1)) +
				(page_address(page) - page_address(head_page));

			skb_add_rx_frag(skb, i - 1, head_page, page_offset,
					sg_length, DPAA2_ETH_RX_BUF_SIZE);
		}

		if (dpaa2_sg_is_final(sge))
			break;
	}

	WARN_ONCE(i == DPAA2_ETH_MAX_SG_ENTRIES, "Final bit not set in SGT");

	/* Count all data buffers + SG table buffer */
	ch->buf_count -= i + 2;

	return skb;
}

/* Free buffers acquired from the buffer pool or which were meant to
 * be released in the pool
 */
static void free_bufs(struct dpaa2_eth_priv *priv, u64 *buf_array, int count)
{
	struct device *dev = priv->net_dev->dev.parent;
	void *vaddr;
	int i;

	for (i = 0; i < count; i++) {
		vaddr = dpaa2_iova_to_virt(priv->iommu_domain, buf_array[i]);
		dma_unmap_page(dev, buf_array[i], DPAA2_ETH_RX_BUF_SIZE,
			       DMA_BIDIRECTIONAL);
		free_pages((unsigned long)vaddr, 0);
	}
}

static void xdp_release_buf(struct dpaa2_eth_priv *priv,
			    struct dpaa2_eth_channel *ch,
			    dma_addr_t addr)
{
	int err;

	ch->xdp.drop_bufs[ch->xdp.drop_cnt++] = addr;
	if (ch->xdp.drop_cnt < DPAA2_ETH_BUFS_PER_CMD)
		return;

	while ((err = dpaa2_io_service_release(ch->dpio, priv->bpid,
					       ch->xdp.drop_bufs,
					       ch->xdp.drop_cnt)) == -EBUSY)
		cpu_relax();

	if (err) {
		free_bufs(priv, ch->xdp.drop_bufs, ch->xdp.drop_cnt);
		ch->buf_count -= ch->xdp.drop_cnt;
	}

	ch->xdp.drop_cnt = 0;
}

static int xdp_enqueue(struct dpaa2_eth_priv *priv, struct dpaa2_fd *fd,
		       void *buf_start, u16 queue_id)
{
	struct dpaa2_eth_fq *fq;
	struct dpaa2_faead *faead;
	u32 ctrl, frc;
	int i, err;

	/* Mark the egress frame hardware annotation area as valid */
	frc = dpaa2_fd_get_frc(fd);
	dpaa2_fd_set_frc(fd, frc | DPAA2_FD_FRC_FAEADV);
	dpaa2_fd_set_ctrl(fd, DPAA2_FD_CTRL_ASAL);

	/* Instruct hardware to release the FD buffer directly into
	 * the buffer pool once transmission is completed, instead of
	 * sending a Tx confirmation frame to us
	 */
	ctrl = DPAA2_FAEAD_A4V | DPAA2_FAEAD_A2V | DPAA2_FAEAD_EBDDV;
	faead = dpaa2_get_faead(buf_start, false);
	faead->ctrl = cpu_to_le32(ctrl);
	faead->conf_fqid = 0;

	fq = &priv->fq[queue_id];
	for (i = 0; i < DPAA2_ETH_ENQUEUE_RETRIES; i++) {
		err = priv->enqueue(priv, fq, fd, 0);
		if (err != -EBUSY)
			break;
	}

	return err;
}

static u32 run_xdp(struct dpaa2_eth_priv *priv,
		   struct dpaa2_eth_channel *ch,
		   struct dpaa2_eth_fq *rx_fq,
		   struct dpaa2_fd *fd, void *vaddr)
{
	dma_addr_t addr = dpaa2_fd_get_addr(fd);
	struct rtnl_link_stats64 *percpu_stats;
	struct bpf_prog *xdp_prog;
	struct xdp_buff xdp;
	u32 xdp_act = XDP_PASS;
	int err;

	percpu_stats = this_cpu_ptr(priv->percpu_stats);

	rcu_read_lock();

	xdp_prog = READ_ONCE(ch->xdp.prog);
	if (!xdp_prog)
		goto out;

	xdp.data = vaddr + dpaa2_fd_get_offset(fd);
	xdp.data_end = xdp.data + dpaa2_fd_get_len(fd);
	xdp.data_hard_start = xdp.data - XDP_PACKET_HEADROOM;
	xdp_set_data_meta_invalid(&xdp);

	xdp_act = bpf_prog_run_xdp(xdp_prog, &xdp);

	/* xdp.data pointer may have changed */
	dpaa2_fd_set_offset(fd, xdp.data - vaddr);
	dpaa2_fd_set_len(fd, xdp.data_end - xdp.data);

	switch (xdp_act) {
	case XDP_PASS:
		break;
	case XDP_TX:
		err = xdp_enqueue(priv, fd, vaddr, rx_fq->flowid);
		if (err) {
			xdp_release_buf(priv, ch, addr);
			percpu_stats->tx_errors++;
			ch->stats.xdp_tx_err++;
		} else {
			percpu_stats->tx_packets++;
			percpu_stats->tx_bytes += dpaa2_fd_get_len(fd);
			ch->stats.xdp_tx++;
		}
		break;
	default:
		bpf_warn_invalid_xdp_action(xdp_act);
		/* fall through */
	case XDP_ABORTED:
		trace_xdp_exception(priv->net_dev, xdp_prog, xdp_act);
		/* fall through */
	case XDP_DROP:
		xdp_release_buf(priv, ch, addr);
		ch->stats.xdp_drop++;
		break;
	}

out:
	rcu_read_unlock();
	return xdp_act;
}

/* Main Rx frame processing routine */
static void dpaa2_eth_rx(struct dpaa2_eth_priv *priv,
			 struct dpaa2_eth_channel *ch,
			 const struct dpaa2_fd *fd,
			 struct dpaa2_eth_fq *fq)
{
	dma_addr_t addr = dpaa2_fd_get_addr(fd);
	u8 fd_format = dpaa2_fd_get_format(fd);
	void *vaddr;
	struct sk_buff *skb;
	struct rtnl_link_stats64 *percpu_stats;
	struct dpaa2_eth_drv_stats *percpu_extras;
	struct device *dev = priv->net_dev->dev.parent;
	struct dpaa2_fas *fas;
	void *buf_data;
	u32 status = 0;
	u32 xdp_act;

	/* Tracing point */
	trace_dpaa2_rx_fd(priv->net_dev, fd);

	vaddr = dpaa2_iova_to_virt(priv->iommu_domain, addr);
	dma_sync_single_for_cpu(dev, addr, DPAA2_ETH_RX_BUF_SIZE,
				DMA_BIDIRECTIONAL);

	fas = dpaa2_get_fas(vaddr, false);
	prefetch(fas);
	buf_data = vaddr + dpaa2_fd_get_offset(fd);
	prefetch(buf_data);

	percpu_stats = this_cpu_ptr(priv->percpu_stats);
	percpu_extras = this_cpu_ptr(priv->percpu_extras);

	if (fd_format == dpaa2_fd_single) {
		xdp_act = run_xdp(priv, ch, fq, (struct dpaa2_fd *)fd, vaddr);
		if (xdp_act != XDP_PASS) {
			percpu_stats->rx_packets++;
			percpu_stats->rx_bytes += dpaa2_fd_get_len(fd);
			return;
		}

		dma_unmap_page(dev, addr, DPAA2_ETH_RX_BUF_SIZE,
			       DMA_BIDIRECTIONAL);
		skb = build_linear_skb(ch, fd, vaddr);
	} else if (fd_format == dpaa2_fd_sg) {
		WARN_ON(priv->xdp_prog);

		dma_unmap_page(dev, addr, DPAA2_ETH_RX_BUF_SIZE,
			       DMA_BIDIRECTIONAL);
		skb = build_frag_skb(priv, ch, buf_data);
		free_pages((unsigned long)vaddr, 0);
		percpu_extras->rx_sg_frames++;
		percpu_extras->rx_sg_bytes += dpaa2_fd_get_len(fd);
	} else {
		/* We don't support any other format */
		goto err_frame_format;
	}

	if (unlikely(!skb))
		goto err_build_skb;

	prefetch(skb->data);

	/* Get the timestamp value */
	if (priv->rx_tstamp) {
		struct skb_shared_hwtstamps *shhwtstamps = skb_hwtstamps(skb);
		__le64 *ts = dpaa2_get_ts(vaddr, false);
		u64 ns;

		memset(shhwtstamps, 0, sizeof(*shhwtstamps));

		ns = DPAA2_PTP_CLK_PERIOD_NS * le64_to_cpup(ts);
		shhwtstamps->hwtstamp = ns_to_ktime(ns);
	}

	/* Check if we need to validate the L4 csum */
	if (likely(dpaa2_fd_get_frc(fd) & DPAA2_FD_FRC_FASV)) {
		status = le32_to_cpu(fas->status);
		validate_rx_csum(priv, status, skb);
	}

	skb->protocol = eth_type_trans(skb, priv->net_dev);
	skb_record_rx_queue(skb, fq->flowid);

	percpu_stats->rx_packets++;
	percpu_stats->rx_bytes += dpaa2_fd_get_len(fd);

	napi_gro_receive(&ch->napi, skb);

	return;

err_build_skb:
	free_rx_fd(priv, fd, vaddr);
err_frame_format:
	percpu_stats->rx_dropped++;
}

/* Consume all frames pull-dequeued into the store. This is the simplest way to
 * make sure we don't accidentally issue another volatile dequeue which would
 * overwrite (leak) frames already in the store.
 *
 * Observance of NAPI budget is not our concern, leaving that to the caller.
 */
static int consume_frames(struct dpaa2_eth_channel *ch,
			  struct dpaa2_eth_fq **src)
{
	struct dpaa2_eth_priv *priv = ch->priv;
	struct dpaa2_eth_fq *fq = NULL;
	struct dpaa2_dq *dq;
	const struct dpaa2_fd *fd;
	int cleaned = 0;
	int is_last;

	do {
		dq = dpaa2_io_store_next(ch->store, &is_last);
		if (unlikely(!dq)) {
			/* If we're here, we *must* have placed a
			 * volatile dequeue comnmand, so keep reading through
			 * the store until we get some sort of valid response
			 * token (either a valid frame or an "empty dequeue")
			 */
			continue;
		}

		fd = dpaa2_dq_fd(dq);
		fq = (struct dpaa2_eth_fq *)(uintptr_t)dpaa2_dq_fqd_ctx(dq);

		fq->consume(priv, ch, fd, fq);
		cleaned++;
	} while (!is_last);

	if (!cleaned)
		return 0;

	fq->stats.frames += cleaned;

	/* A dequeue operation only pulls frames from a single queue
	 * into the store. Return the frame queue as an out param.
	 */
	if (src)
		*src = fq;

	return cleaned;
}

/* Configure the egress frame annotation for timestamp update */
static void enable_tx_tstamp(struct dpaa2_fd *fd, void *buf_start)
{
	struct dpaa2_faead *faead;
	u32 ctrl, frc;

	/* Mark the egress frame annotation area as valid */
	frc = dpaa2_fd_get_frc(fd);
	dpaa2_fd_set_frc(fd, frc | DPAA2_FD_FRC_FAEADV);

	/* Set hardware annotation size */
	ctrl = dpaa2_fd_get_ctrl(fd);
	dpaa2_fd_set_ctrl(fd, ctrl | DPAA2_FD_CTRL_ASAL);

	/* enable UPD (update prepanded data) bit in FAEAD field of
	 * hardware frame annotation area
	 */
	ctrl = DPAA2_FAEAD_A2V | DPAA2_FAEAD_UPDV | DPAA2_FAEAD_UPD;
	faead = dpaa2_get_faead(buf_start, true);
	faead->ctrl = cpu_to_le32(ctrl);
}

/* Create a frame descriptor based on a fragmented skb */
static int build_sg_fd(struct dpaa2_eth_priv *priv,
		       struct sk_buff *skb,
		       struct dpaa2_fd *fd)
{
	struct device *dev = priv->net_dev->dev.parent;
	void *sgt_buf = NULL;
	dma_addr_t addr;
	int nr_frags = skb_shinfo(skb)->nr_frags;
	struct dpaa2_sg_entry *sgt;
	int i, err;
	int sgt_buf_size;
	struct scatterlist *scl, *crt_scl;
	int num_sg;
	int num_dma_bufs;
	struct dpaa2_eth_swa *swa;

	/* Create and map scatterlist.
	 * We don't advertise NETIF_F_FRAGLIST, so skb_to_sgvec() will not have
	 * to go beyond nr_frags+1.
	 * Note: We don't support chained scatterlists
	 */
	if (unlikely(PAGE_SIZE / sizeof(struct scatterlist) < nr_frags + 1))
		return -EINVAL;

	scl = kcalloc(nr_frags + 1, sizeof(struct scatterlist), GFP_ATOMIC);
	if (unlikely(!scl))
		return -ENOMEM;

	sg_init_table(scl, nr_frags + 1);
	num_sg = skb_to_sgvec(skb, scl, 0, skb->len);
	num_dma_bufs = dma_map_sg(dev, scl, num_sg, DMA_BIDIRECTIONAL);
	if (unlikely(!num_dma_bufs)) {
		err = -ENOMEM;
		goto dma_map_sg_failed;
	}

	/* Prepare the HW SGT structure */
	sgt_buf_size = priv->tx_data_offset +
		       sizeof(struct dpaa2_sg_entry) *  num_dma_bufs;
	sgt_buf = netdev_alloc_frag(sgt_buf_size + DPAA2_ETH_TX_BUF_ALIGN);
	if (unlikely(!sgt_buf)) {
		err = -ENOMEM;
		goto sgt_buf_alloc_failed;
	}
	sgt_buf = PTR_ALIGN(sgt_buf, DPAA2_ETH_TX_BUF_ALIGN);
	memset(sgt_buf, 0, sgt_buf_size);

	sgt = (struct dpaa2_sg_entry *)(sgt_buf + priv->tx_data_offset);

	/* Fill in the HW SGT structure.
	 *
	 * sgt_buf is zeroed out, so the following fields are implicit
	 * in all sgt entries:
	 *   - offset is 0
	 *   - format is 'dpaa2_sg_single'
	 */
	for_each_sg(scl, crt_scl, num_dma_bufs, i) {
		dpaa2_sg_set_addr(&sgt[i], sg_dma_address(crt_scl));
		dpaa2_sg_set_len(&sgt[i], sg_dma_len(crt_scl));
	}
	dpaa2_sg_set_final(&sgt[i - 1], true);

	/* Store the skb backpointer in the SGT buffer.
	 * Fit the scatterlist and the number of buffers alongside the
	 * skb backpointer in the software annotation area. We'll need
	 * all of them on Tx Conf.
	 */
	swa = (struct dpaa2_eth_swa *)sgt_buf;
	swa->skb = skb;
	swa->scl = scl;
	swa->num_sg = num_sg;
	swa->sgt_size = sgt_buf_size;

	/* Separately map the SGT buffer */
	addr = dma_map_single(dev, sgt_buf, sgt_buf_size, DMA_BIDIRECTIONAL);
	if (unlikely(dma_mapping_error(dev, addr))) {
		err = -ENOMEM;
		goto dma_map_single_failed;
	}
	dpaa2_fd_set_offset(fd, priv->tx_data_offset);
	dpaa2_fd_set_format(fd, dpaa2_fd_sg);
	dpaa2_fd_set_addr(fd, addr);
	dpaa2_fd_set_len(fd, skb->len);
	dpaa2_fd_set_ctrl(fd, FD_CTRL_PTA);

	if (priv->tx_tstamp && skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)
		enable_tx_tstamp(fd, sgt_buf);

	return 0;

dma_map_single_failed:
	skb_free_frag(sgt_buf);
sgt_buf_alloc_failed:
	dma_unmap_sg(dev, scl, num_sg, DMA_BIDIRECTIONAL);
dma_map_sg_failed:
	kfree(scl);
	return err;
}

/* Create a frame descriptor based on a linear skb */
static int build_single_fd(struct dpaa2_eth_priv *priv,
			   struct sk_buff *skb,
			   struct dpaa2_fd *fd)
{
	struct device *dev = priv->net_dev->dev.parent;
	u8 *buffer_start, *aligned_start;
	struct sk_buff **skbh;
	dma_addr_t addr;

	buffer_start = skb->data - dpaa2_eth_needed_headroom(priv, skb);

	/* If there's enough room to align the FD address, do it.
	 * It will help hardware optimize accesses.
	 */
	aligned_start = PTR_ALIGN(buffer_start - DPAA2_ETH_TX_BUF_ALIGN,
				  DPAA2_ETH_TX_BUF_ALIGN);
	if (aligned_start >= skb->head)
		buffer_start = aligned_start;

	/* Store a backpointer to the skb at the beginning of the buffer
	 * (in the private data area) such that we can release it
	 * on Tx confirm
	 */
	skbh = (struct sk_buff **)buffer_start;
	*skbh = skb;

	addr = dma_map_single(dev, buffer_start,
			      skb_tail_pointer(skb) - buffer_start,
			      DMA_BIDIRECTIONAL);
	if (unlikely(dma_mapping_error(dev, addr)))
		return -ENOMEM;

	dpaa2_fd_set_addr(fd, addr);
	dpaa2_fd_set_offset(fd, (u16)(skb->data - buffer_start));
	dpaa2_fd_set_len(fd, skb->len);
	dpaa2_fd_set_format(fd, dpaa2_fd_single);
	dpaa2_fd_set_ctrl(fd, FD_CTRL_PTA);

	if (priv->tx_tstamp && skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)
		enable_tx_tstamp(fd, buffer_start);

	return 0;
}

/* FD freeing routine on the Tx path
 *
 * DMA-unmap and free FD and possibly SGT buffer allocated on Tx. The skb
 * back-pointed to is also freed.
 * This can be called either from dpaa2_eth_tx_conf() or on the error path of
 * dpaa2_eth_tx().
 */
static void free_tx_fd(const struct dpaa2_eth_priv *priv,
		       const struct dpaa2_fd *fd, bool in_napi)
{
	struct device *dev = priv->net_dev->dev.parent;
	dma_addr_t fd_addr;
	struct sk_buff **skbh, *skb;
	unsigned char *buffer_start;
	struct dpaa2_eth_swa *swa;
	u8 fd_format = dpaa2_fd_get_format(fd);

	fd_addr = dpaa2_fd_get_addr(fd);
	skbh = dpaa2_iova_to_virt(priv->iommu_domain, fd_addr);

	if (fd_format == dpaa2_fd_single) {
		skb = *skbh;
		buffer_start = (unsigned char *)skbh;
		/* Accessing the skb buffer is safe before dma unmap, because
		 * we didn't map the actual skb shell.
		 */
		dma_unmap_single(dev, fd_addr,
				 skb_tail_pointer(skb) - buffer_start,
				 DMA_BIDIRECTIONAL);
	} else if (fd_format == dpaa2_fd_sg) {
		swa = (struct dpaa2_eth_swa *)skbh;
		skb = swa->skb;

		/* Unmap the scatterlist */
		dma_unmap_sg(dev, swa->scl, swa->num_sg, DMA_BIDIRECTIONAL);
		kfree(swa->scl);

		/* Unmap the SGT buffer */
		dma_unmap_single(dev, fd_addr, swa->sgt_size,
				 DMA_BIDIRECTIONAL);
	} else {
		netdev_dbg(priv->net_dev, "Invalid FD format\n");
		return;
	}

	/* Get the timestamp value */
	if (priv->tx_tstamp && skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
		struct skb_shared_hwtstamps shhwtstamps;
		__le64 *ts = dpaa2_get_ts(skbh, true);
		u64 ns;

		memset(&shhwtstamps, 0, sizeof(shhwtstamps));

		ns = DPAA2_PTP_CLK_PERIOD_NS * le64_to_cpup(ts);
		shhwtstamps.hwtstamp = ns_to_ktime(ns);
		skb_tstamp_tx(skb, &shhwtstamps);
	}

	/* Free SGT buffer allocated on tx */
	if (fd_format != dpaa2_fd_single)
		skb_free_frag(skbh);

	/* Move on with skb release */
	napi_consume_skb(skb, in_napi);
}

static netdev_tx_t dpaa2_eth_tx(struct sk_buff *skb, struct net_device *net_dev)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);
	struct dpaa2_fd fd;
	struct rtnl_link_stats64 *percpu_stats;
	struct dpaa2_eth_drv_stats *percpu_extras;
	struct dpaa2_eth_fq *fq;
	struct netdev_queue *nq;
	u16 queue_mapping;
	unsigned int needed_headroom;
	u32 fd_len;
	int err, i;

	percpu_stats = this_cpu_ptr(priv->percpu_stats);
	percpu_extras = this_cpu_ptr(priv->percpu_extras);

	needed_headroom = dpaa2_eth_needed_headroom(priv, skb);
	if (skb_headroom(skb) < needed_headroom) {
		struct sk_buff *ns;

		ns = skb_realloc_headroom(skb, needed_headroom);
		if (unlikely(!ns)) {
			percpu_stats->tx_dropped++;
			goto err_alloc_headroom;
		}
		percpu_extras->tx_reallocs++;

		if (skb->sk)
			skb_set_owner_w(ns, skb->sk);

		dev_kfree_skb(skb);
		skb = ns;
	}

	/* We'll be holding a back-reference to the skb until Tx Confirmation;
	 * we don't want that overwritten by a concurrent Tx with a cloned skb.
	 */
	skb = skb_unshare(skb, GFP_ATOMIC);
	if (unlikely(!skb)) {
		/* skb_unshare() has already freed the skb */
		percpu_stats->tx_dropped++;
		return NETDEV_TX_OK;
	}

	/* Setup the FD fields */
	memset(&fd, 0, sizeof(fd));

	if (skb_is_nonlinear(skb)) {
		err = build_sg_fd(priv, skb, &fd);
		percpu_extras->tx_sg_frames++;
		percpu_extras->tx_sg_bytes += skb->len;
	} else {
		err = build_single_fd(priv, skb, &fd);
	}

	if (unlikely(err)) {
		percpu_stats->tx_dropped++;
		goto err_build_fd;
	}

	/* Tracing point */
	trace_dpaa2_tx_fd(net_dev, &fd);

	/* TxConf FQ selection relies on queue id from the stack.
	 * In case of a forwarded frame from another DPNI interface, we choose
	 * a queue affined to the same core that processed the Rx frame
	 */
	queue_mapping = skb_get_queue_mapping(skb);
	fq = &priv->fq[queue_mapping];
	for (i = 0; i < DPAA2_ETH_ENQUEUE_RETRIES; i++) {
		err = priv->enqueue(priv, fq, &fd, 0);
		if (err != -EBUSY)
			break;
	}
	percpu_extras->tx_portal_busy += i;
	if (unlikely(err < 0)) {
		percpu_stats->tx_errors++;
		/* Clean up everything, including freeing the skb */
		free_tx_fd(priv, &fd, false);
	} else {
		fd_len = dpaa2_fd_get_len(&fd);
		percpu_stats->tx_packets++;
		percpu_stats->tx_bytes += fd_len;

		nq = netdev_get_tx_queue(net_dev, queue_mapping);
		netdev_tx_sent_queue(nq, fd_len);
	}

	return NETDEV_TX_OK;

err_build_fd:
err_alloc_headroom:
	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

/* Tx confirmation frame processing routine */
static void dpaa2_eth_tx_conf(struct dpaa2_eth_priv *priv,
			      struct dpaa2_eth_channel *ch __always_unused,
			      const struct dpaa2_fd *fd,
			      struct dpaa2_eth_fq *fq)
{
	struct rtnl_link_stats64 *percpu_stats;
	struct dpaa2_eth_drv_stats *percpu_extras;
	u32 fd_len = dpaa2_fd_get_len(fd);
	u32 fd_errors;

	/* Tracing point */
	trace_dpaa2_tx_conf_fd(priv->net_dev, fd);

	percpu_extras = this_cpu_ptr(priv->percpu_extras);
	percpu_extras->tx_conf_frames++;
	percpu_extras->tx_conf_bytes += fd_len;

	fq->dq_frames++;
	fq->dq_bytes += fd_len;

	/* Check frame errors in the FD field */
	fd_errors = dpaa2_fd_get_ctrl(fd) & DPAA2_FD_TX_ERR_MASK;
	free_tx_fd(priv, fd, true);

	if (likely(!fd_errors))
		return;

	if (net_ratelimit())
		netdev_dbg(priv->net_dev, "TX frame FD error: 0x%08x\n",
			   fd_errors);

	percpu_stats = this_cpu_ptr(priv->percpu_stats);
	/* Tx-conf logically pertains to the egress path. */
	percpu_stats->tx_errors++;
}

static int set_rx_csum(struct dpaa2_eth_priv *priv, bool enable)
{
	int err;

	err = dpni_set_offload(priv->mc_io, 0, priv->mc_token,
			       DPNI_OFF_RX_L3_CSUM, enable);
	if (err) {
		netdev_err(priv->net_dev,
			   "dpni_set_offload(RX_L3_CSUM) failed\n");
		return err;
	}

	err = dpni_set_offload(priv->mc_io, 0, priv->mc_token,
			       DPNI_OFF_RX_L4_CSUM, enable);
	if (err) {
		netdev_err(priv->net_dev,
			   "dpni_set_offload(RX_L4_CSUM) failed\n");
		return err;
	}

	return 0;
}

static int set_tx_csum(struct dpaa2_eth_priv *priv, bool enable)
{
	int err;

	err = dpni_set_offload(priv->mc_io, 0, priv->mc_token,
			       DPNI_OFF_TX_L3_CSUM, enable);
	if (err) {
		netdev_err(priv->net_dev, "dpni_set_offload(TX_L3_CSUM) failed\n");
		return err;
	}

	err = dpni_set_offload(priv->mc_io, 0, priv->mc_token,
			       DPNI_OFF_TX_L4_CSUM, enable);
	if (err) {
		netdev_err(priv->net_dev, "dpni_set_offload(TX_L4_CSUM) failed\n");
		return err;
	}

	return 0;
}

/* Perform a single release command to add buffers
 * to the specified buffer pool
 */
static int add_bufs(struct dpaa2_eth_priv *priv,
		    struct dpaa2_eth_channel *ch, u16 bpid)
{
	struct device *dev = priv->net_dev->dev.parent;
	u64 buf_array[DPAA2_ETH_BUFS_PER_CMD];
	struct page *page;
	dma_addr_t addr;
	int i, err;

	for (i = 0; i < DPAA2_ETH_BUFS_PER_CMD; i++) {
		/* Allocate buffer visible to WRIOP + skb shared info +
		 * alignment padding
		 */
		/* allocate one page for each Rx buffer. WRIOP sees
		 * the entire page except for a tailroom reserved for
		 * skb shared info
		 */
		page = dev_alloc_pages(0);
		if (!page)
			goto err_alloc;

		addr = dma_map_page(dev, page, 0, DPAA2_ETH_RX_BUF_SIZE,
				    DMA_BIDIRECTIONAL);
		if (unlikely(dma_mapping_error(dev, addr)))
			goto err_map;

		buf_array[i] = addr;

		/* tracing point */
		trace_dpaa2_eth_buf_seed(priv->net_dev,
					 page, DPAA2_ETH_RX_BUF_RAW_SIZE,
					 addr, DPAA2_ETH_RX_BUF_SIZE,
					 bpid);
	}

release_bufs:
	/* In case the portal is busy, retry until successful */
	while ((err = dpaa2_io_service_release(ch->dpio, bpid,
					       buf_array, i)) == -EBUSY)
		cpu_relax();

	/* If release command failed, clean up and bail out;
	 * not much else we can do about it
	 */
	if (err) {
		free_bufs(priv, buf_array, i);
		return 0;
	}

	return i;

err_map:
	__free_pages(page, 0);
err_alloc:
	/* If we managed to allocate at least some buffers,
	 * release them to hardware
	 */
	if (i)
		goto release_bufs;

	return 0;
}

static int seed_pool(struct dpaa2_eth_priv *priv, u16 bpid)
{
	int i, j;
	int new_count;

	/* This is the lazy seeding of Rx buffer pools.
	 * dpaa2_add_bufs() is also used on the Rx hotpath and calls
	 * napi_alloc_frag(). The trouble with that is that it in turn ends up
	 * calling this_cpu_ptr(), which mandates execution in atomic context.
	 * Rather than splitting up the code, do a one-off preempt disable.
	 */
	preempt_disable();
	for (j = 0; j < priv->num_channels; j++) {
		for (i = 0; i < DPAA2_ETH_NUM_BUFS;
		     i += DPAA2_ETH_BUFS_PER_CMD) {
			new_count = add_bufs(priv, priv->channel[j], bpid);
			priv->channel[j]->buf_count += new_count;

			if (new_count < DPAA2_ETH_BUFS_PER_CMD) {
				preempt_enable();
				return -ENOMEM;
			}
		}
	}
	preempt_enable();

	return 0;
}

/**
 * Drain the specified number of buffers from the DPNI's private buffer pool.
 * @count must not exceeed DPAA2_ETH_BUFS_PER_CMD
 */
static void drain_bufs(struct dpaa2_eth_priv *priv, int count)
{
	u64 buf_array[DPAA2_ETH_BUFS_PER_CMD];
	int ret;

	do {
		ret = dpaa2_io_service_acquire(NULL, priv->bpid,
					       buf_array, count);
		if (ret < 0) {
			netdev_err(priv->net_dev, "dpaa2_io_service_acquire() failed\n");
			return;
		}
		free_bufs(priv, buf_array, ret);
	} while (ret);
}

static void drain_pool(struct dpaa2_eth_priv *priv)
{
	int i;

	drain_bufs(priv, DPAA2_ETH_BUFS_PER_CMD);
	drain_bufs(priv, 1);

	for (i = 0; i < priv->num_channels; i++)
		priv->channel[i]->buf_count = 0;
}

/* Function is called from softirq context only, so we don't need to guard
 * the access to percpu count
 */
static int refill_pool(struct dpaa2_eth_priv *priv,
		       struct dpaa2_eth_channel *ch,
		       u16 bpid)
{
	int new_count;

	if (likely(ch->buf_count >= DPAA2_ETH_REFILL_THRESH))
		return 0;

	do {
		new_count = add_bufs(priv, ch, bpid);
		if (unlikely(!new_count)) {
			/* Out of memory; abort for now, we'll try later on */
			break;
		}
		ch->buf_count += new_count;
	} while (ch->buf_count < DPAA2_ETH_NUM_BUFS);

	if (unlikely(ch->buf_count < DPAA2_ETH_NUM_BUFS))
		return -ENOMEM;

	return 0;
}

static int pull_channel(struct dpaa2_eth_channel *ch)
{
	int err;
	int dequeues = -1;

	/* Retry while portal is busy */
	do {
		err = dpaa2_io_service_pull_channel(ch->dpio, ch->ch_id,
						    ch->store);
		dequeues++;
		cpu_relax();
	} while (err == -EBUSY);

	ch->stats.dequeue_portal_busy += dequeues;
	if (unlikely(err))
		ch->stats.pull_err++;

	return err;
}

/* NAPI poll routine
 *
 * Frames are dequeued from the QMan channel associated with this NAPI context.
 * Rx, Tx confirmation and (if configured) Rx error frames all count
 * towards the NAPI budget.
 */
static int dpaa2_eth_poll(struct napi_struct *napi, int budget)
{
	struct dpaa2_eth_channel *ch;
	struct dpaa2_eth_priv *priv;
	int rx_cleaned = 0, txconf_cleaned = 0;
	struct dpaa2_eth_fq *fq, *txc_fq = NULL;
	struct netdev_queue *nq;
	int store_cleaned, work_done;
	int err;

	ch = container_of(napi, struct dpaa2_eth_channel, napi);
	priv = ch->priv;

	do {
		err = pull_channel(ch);
		if (unlikely(err))
			break;

		/* Refill pool if appropriate */
		refill_pool(priv, ch, priv->bpid);

		store_cleaned = consume_frames(ch, &fq);
		if (!store_cleaned)
			break;
		if (fq->type == DPAA2_RX_FQ) {
			rx_cleaned += store_cleaned;
		} else {
			txconf_cleaned += store_cleaned;
			/* We have a single Tx conf FQ on this channel */
			txc_fq = fq;
		}

		/* If we either consumed the whole NAPI budget with Rx frames
		 * or we reached the Tx confirmations threshold, we're done.
		 */
		if (rx_cleaned >= budget ||
		    txconf_cleaned >= DPAA2_ETH_TXCONF_PER_NAPI) {
			work_done = budget;
			goto out;
		}
	} while (store_cleaned);

	/* We didn't consume the entire budget, so finish napi and
	 * re-enable data availability notifications
	 */
	napi_complete_done(napi, rx_cleaned);
	do {
		err = dpaa2_io_service_rearm(ch->dpio, &ch->nctx);
		cpu_relax();
	} while (err == -EBUSY);
	WARN_ONCE(err, "CDAN notifications rearm failed on core %d",
		  ch->nctx.desired_cpu);

	work_done = max(rx_cleaned, 1);

out:
	if (txc_fq) {
		nq = netdev_get_tx_queue(priv->net_dev, txc_fq->flowid);
		netdev_tx_completed_queue(nq, txc_fq->dq_frames,
					  txc_fq->dq_bytes);
		txc_fq->dq_frames = 0;
		txc_fq->dq_bytes = 0;
	}

	return work_done;
}

static void enable_ch_napi(struct dpaa2_eth_priv *priv)
{
	struct dpaa2_eth_channel *ch;
	int i;

	for (i = 0; i < priv->num_channels; i++) {
		ch = priv->channel[i];
		napi_enable(&ch->napi);
	}
}

static void disable_ch_napi(struct dpaa2_eth_priv *priv)
{
	struct dpaa2_eth_channel *ch;
	int i;

	for (i = 0; i < priv->num_channels; i++) {
		ch = priv->channel[i];
		napi_disable(&ch->napi);
	}
}

static int link_state_update(struct dpaa2_eth_priv *priv)
{
	struct dpni_link_state state = {0};
	int err;

	err = dpni_get_link_state(priv->mc_io, 0, priv->mc_token, &state);
	if (unlikely(err)) {
		netdev_err(priv->net_dev,
			   "dpni_get_link_state() failed\n");
		return err;
	}

	/* Chech link state; speed / duplex changes are not treated yet */
	if (priv->link_state.up == state.up)
		return 0;

	priv->link_state = state;
	if (state.up) {
		netif_carrier_on(priv->net_dev);
		netif_tx_start_all_queues(priv->net_dev);
	} else {
		netif_tx_stop_all_queues(priv->net_dev);
		netif_carrier_off(priv->net_dev);
	}

	netdev_info(priv->net_dev, "Link Event: state %s\n",
		    state.up ? "up" : "down");

	return 0;
}

static int dpaa2_eth_open(struct net_device *net_dev)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);
	int err;

	err = seed_pool(priv, priv->bpid);
	if (err) {
		/* Not much to do; the buffer pool, though not filled up,
		 * may still contain some buffers which would enable us
		 * to limp on.
		 */
		netdev_err(net_dev, "Buffer seeding failed for DPBP %d (bpid=%d)\n",
			   priv->dpbp_dev->obj_desc.id, priv->bpid);
	}

	/* We'll only start the txqs when the link is actually ready; make sure
	 * we don't race against the link up notification, which may come
	 * immediately after dpni_enable();
	 */
	netif_tx_stop_all_queues(net_dev);
	enable_ch_napi(priv);
	/* Also, explicitly set carrier off, otherwise netif_carrier_ok() will
	 * return true and cause 'ip link show' to report the LOWER_UP flag,
	 * even though the link notification wasn't even received.
	 */
	netif_carrier_off(net_dev);

	err = dpni_enable(priv->mc_io, 0, priv->mc_token);
	if (err < 0) {
		netdev_err(net_dev, "dpni_enable() failed\n");
		goto enable_err;
	}

	/* If the DPMAC object has already processed the link up interrupt,
	 * we have to learn the link state ourselves.
	 */
	err = link_state_update(priv);
	if (err < 0) {
		netdev_err(net_dev, "Can't update link state\n");
		goto link_state_err;
	}

	return 0;

link_state_err:
enable_err:
	disable_ch_napi(priv);
	drain_pool(priv);
	return err;
}

/* Total number of in-flight frames on ingress queues */
static u32 ingress_fq_count(struct dpaa2_eth_priv *priv)
{
	struct dpaa2_eth_fq *fq;
	u32 fcnt = 0, bcnt = 0, total = 0;
	int i, err;

	for (i = 0; i < priv->num_fqs; i++) {
		fq = &priv->fq[i];
		err = dpaa2_io_query_fq_count(NULL, fq->fqid, &fcnt, &bcnt);
		if (err) {
			netdev_warn(priv->net_dev, "query_fq_count failed");
			break;
		}
		total += fcnt;
	}

	return total;
}

static void wait_for_fq_empty(struct dpaa2_eth_priv *priv)
{
	int retries = 10;
	u32 pending;

	do {
		pending = ingress_fq_count(priv);
		if (pending)
			msleep(100);
	} while (pending && --retries);
}

static int dpaa2_eth_stop(struct net_device *net_dev)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);
	int dpni_enabled = 0;
	int retries = 10;

	netif_tx_stop_all_queues(net_dev);
	netif_carrier_off(net_dev);

	/* On dpni_disable(), the MC firmware will:
	 * - stop MAC Rx and wait for all Rx frames to be enqueued to software
	 * - cut off WRIOP dequeues from egress FQs and wait until transmission
	 * of all in flight Tx frames is finished (and corresponding Tx conf
	 * frames are enqueued back to software)
	 *
	 * Before calling dpni_disable(), we wait for all Tx frames to arrive
	 * on WRIOP. After it finishes, wait until all remaining frames on Rx
	 * and Tx conf queues are consumed on NAPI poll.
	 */
	msleep(500);

	do {
		dpni_disable(priv->mc_io, 0, priv->mc_token);
		dpni_is_enabled(priv->mc_io, 0, priv->mc_token, &dpni_enabled);
		if (dpni_enabled)
			/* Allow the hardware some slack */
			msleep(100);
	} while (dpni_enabled && --retries);
	if (!retries) {
		netdev_warn(net_dev, "Retry count exceeded disabling DPNI\n");
		/* Must go on and disable NAPI nonetheless, so we don't crash at
		 * the next "ifconfig up"
		 */
	}

	wait_for_fq_empty(priv);
	disable_ch_napi(priv);

	/* Empty the buffer pool */
	drain_pool(priv);

	return 0;
}

static int dpaa2_eth_set_addr(struct net_device *net_dev, void *addr)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);
	struct device *dev = net_dev->dev.parent;
	int err;

	err = eth_mac_addr(net_dev, addr);
	if (err < 0) {
		dev_err(dev, "eth_mac_addr() failed (%d)\n", err);
		return err;
	}

	err = dpni_set_primary_mac_addr(priv->mc_io, 0, priv->mc_token,
					net_dev->dev_addr);
	if (err) {
		dev_err(dev, "dpni_set_primary_mac_addr() failed (%d)\n", err);
		return err;
	}

	return 0;
}

/** Fill in counters maintained by the GPP driver. These may be different from
 * the hardware counters obtained by ethtool.
 */
static void dpaa2_eth_get_stats(struct net_device *net_dev,
				struct rtnl_link_stats64 *stats)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);
	struct rtnl_link_stats64 *percpu_stats;
	u64 *cpustats;
	u64 *netstats = (u64 *)stats;
	int i, j;
	int num = sizeof(struct rtnl_link_stats64) / sizeof(u64);

	for_each_possible_cpu(i) {
		percpu_stats = per_cpu_ptr(priv->percpu_stats, i);
		cpustats = (u64 *)percpu_stats;
		for (j = 0; j < num; j++)
			netstats[j] += cpustats[j];
	}
}

/* Copy mac unicast addresses from @net_dev to @priv.
 * Its sole purpose is to make dpaa2_eth_set_rx_mode() more readable.
 */
static void add_uc_hw_addr(const struct net_device *net_dev,
			   struct dpaa2_eth_priv *priv)
{
	struct netdev_hw_addr *ha;
	int err;

	netdev_for_each_uc_addr(ha, net_dev) {
		err = dpni_add_mac_addr(priv->mc_io, 0, priv->mc_token,
					ha->addr);
		if (err)
			netdev_warn(priv->net_dev,
				    "Could not add ucast MAC %pM to the filtering table (err %d)\n",
				    ha->addr, err);
	}
}

/* Copy mac multicast addresses from @net_dev to @priv
 * Its sole purpose is to make dpaa2_eth_set_rx_mode() more readable.
 */
static void add_mc_hw_addr(const struct net_device *net_dev,
			   struct dpaa2_eth_priv *priv)
{
	struct netdev_hw_addr *ha;
	int err;

	netdev_for_each_mc_addr(ha, net_dev) {
		err = dpni_add_mac_addr(priv->mc_io, 0, priv->mc_token,
					ha->addr);
		if (err)
			netdev_warn(priv->net_dev,
				    "Could not add mcast MAC %pM to the filtering table (err %d)\n",
				    ha->addr, err);
	}
}

static void dpaa2_eth_set_rx_mode(struct net_device *net_dev)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);
	int uc_count = netdev_uc_count(net_dev);
	int mc_count = netdev_mc_count(net_dev);
	u8 max_mac = priv->dpni_attrs.mac_filter_entries;
	u32 options = priv->dpni_attrs.options;
	u16 mc_token = priv->mc_token;
	struct fsl_mc_io *mc_io = priv->mc_io;
	int err;

	/* Basic sanity checks; these probably indicate a misconfiguration */
	if (options & DPNI_OPT_NO_MAC_FILTER && max_mac != 0)
		netdev_info(net_dev,
			    "mac_filter_entries=%d, DPNI_OPT_NO_MAC_FILTER option must be disabled\n",
			    max_mac);

	/* Force promiscuous if the uc or mc counts exceed our capabilities. */
	if (uc_count > max_mac) {
		netdev_info(net_dev,
			    "Unicast addr count reached %d, max allowed is %d; forcing promisc\n",
			    uc_count, max_mac);
		goto force_promisc;
	}
	if (mc_count + uc_count > max_mac) {
		netdev_info(net_dev,
			    "Unicast + multicast addr count reached %d, max allowed is %d; forcing promisc\n",
			    uc_count + mc_count, max_mac);
		goto force_mc_promisc;
	}

	/* Adjust promisc settings due to flag combinations */
	if (net_dev->flags & IFF_PROMISC)
		goto force_promisc;
	if (net_dev->flags & IFF_ALLMULTI) {
		/* First, rebuild unicast filtering table. This should be done
		 * in promisc mode, in order to avoid frame loss while we
		 * progressively add entries to the table.
		 * We don't know whether we had been in promisc already, and
		 * making an MC call to find out is expensive; so set uc promisc
		 * nonetheless.
		 */
		err = dpni_set_unicast_promisc(mc_io, 0, mc_token, 1);
		if (err)
			netdev_warn(net_dev, "Can't set uc promisc\n");

		/* Actual uc table reconstruction. */
		err = dpni_clear_mac_filters(mc_io, 0, mc_token, 1, 0);
		if (err)
			netdev_warn(net_dev, "Can't clear uc filters\n");
		add_uc_hw_addr(net_dev, priv);

		/* Finally, clear uc promisc and set mc promisc as requested. */
		err = dpni_set_unicast_promisc(mc_io, 0, mc_token, 0);
		if (err)
			netdev_warn(net_dev, "Can't clear uc promisc\n");
		goto force_mc_promisc;
	}

	/* Neither unicast, nor multicast promisc will be on... eventually.
	 * For now, rebuild mac filtering tables while forcing both of them on.
	 */
	err = dpni_set_unicast_promisc(mc_io, 0, mc_token, 1);
	if (err)
		netdev_warn(net_dev, "Can't set uc promisc (%d)\n", err);
	err = dpni_set_multicast_promisc(mc_io, 0, mc_token, 1);
	if (err)
		netdev_warn(net_dev, "Can't set mc promisc (%d)\n", err);

	/* Actual mac filtering tables reconstruction */
	err = dpni_clear_mac_filters(mc_io, 0, mc_token, 1, 1);
	if (err)
		netdev_warn(net_dev, "Can't clear mac filters\n");
	add_mc_hw_addr(net_dev, priv);
	add_uc_hw_addr(net_dev, priv);

	/* Now we can clear both ucast and mcast promisc, without risking
	 * to drop legitimate frames anymore.
	 */
	err = dpni_set_unicast_promisc(mc_io, 0, mc_token, 0);
	if (err)
		netdev_warn(net_dev, "Can't clear ucast promisc\n");
	err = dpni_set_multicast_promisc(mc_io, 0, mc_token, 0);
	if (err)
		netdev_warn(net_dev, "Can't clear mcast promisc\n");

	return;

force_promisc:
	err = dpni_set_unicast_promisc(mc_io, 0, mc_token, 1);
	if (err)
		netdev_warn(net_dev, "Can't set ucast promisc\n");
force_mc_promisc:
	err = dpni_set_multicast_promisc(mc_io, 0, mc_token, 1);
	if (err)
		netdev_warn(net_dev, "Can't set mcast promisc\n");
}

static int dpaa2_eth_set_features(struct net_device *net_dev,
				  netdev_features_t features)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);
	netdev_features_t changed = features ^ net_dev->features;
	bool enable;
	int err;

	if (changed & NETIF_F_RXCSUM) {
		enable = !!(features & NETIF_F_RXCSUM);
		err = set_rx_csum(priv, enable);
		if (err)
			return err;
	}

	if (changed & (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM)) {
		enable = !!(features & (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM));
		err = set_tx_csum(priv, enable);
		if (err)
			return err;
	}

	return 0;
}

static int dpaa2_eth_ts_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct dpaa2_eth_priv *priv = netdev_priv(dev);
	struct hwtstamp_config config;

	if (copy_from_user(&config, rq->ifr_data, sizeof(config)))
		return -EFAULT;

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
		priv->tx_tstamp = false;
		break;
	case HWTSTAMP_TX_ON:
		priv->tx_tstamp = true;
		break;
	default:
		return -ERANGE;
	}

	if (config.rx_filter == HWTSTAMP_FILTER_NONE) {
		priv->rx_tstamp = false;
	} else {
		priv->rx_tstamp = true;
		/* TS is set for all frame types, not only those requested */
		config.rx_filter = HWTSTAMP_FILTER_ALL;
	}

	return copy_to_user(rq->ifr_data, &config, sizeof(config)) ?
			-EFAULT : 0;
}

static int dpaa2_eth_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	if (cmd == SIOCSHWTSTAMP)
		return dpaa2_eth_ts_ioctl(dev, rq, cmd);

	return -EINVAL;
}

static bool xdp_mtu_valid(struct dpaa2_eth_priv *priv, int mtu)
{
	int mfl, linear_mfl;

	mfl = DPAA2_ETH_L2_MAX_FRM(mtu);
	linear_mfl = DPAA2_ETH_RX_BUF_SIZE - DPAA2_ETH_RX_HWA_SIZE -
		     dpaa2_eth_rx_head_room(priv) - XDP_PACKET_HEADROOM;

	if (mfl > linear_mfl) {
		netdev_warn(priv->net_dev, "Maximum MTU for XDP is %d\n",
			    linear_mfl - VLAN_ETH_HLEN);
		return false;
	}

	return true;
}

static int set_rx_mfl(struct dpaa2_eth_priv *priv, int mtu, bool has_xdp)
{
	int mfl, err;

	/* We enforce a maximum Rx frame length based on MTU only if we have
	 * an XDP program attached (in order to avoid Rx S/G frames).
	 * Otherwise, we accept all incoming frames as long as they are not
	 * larger than maximum size supported in hardware
	 */
	if (has_xdp)
		mfl = DPAA2_ETH_L2_MAX_FRM(mtu);
	else
		mfl = DPAA2_ETH_MFL;

	err = dpni_set_max_frame_length(priv->mc_io, 0, priv->mc_token, mfl);
	if (err) {
		netdev_err(priv->net_dev, "dpni_set_max_frame_length failed\n");
		return err;
	}

	return 0;
}

static int dpaa2_eth_change_mtu(struct net_device *dev, int new_mtu)
{
	struct dpaa2_eth_priv *priv = netdev_priv(dev);
	int err;

	if (!priv->xdp_prog)
		goto out;

	if (!xdp_mtu_valid(priv, new_mtu))
		return -EINVAL;

	err = set_rx_mfl(priv, new_mtu, true);
	if (err)
		return err;

out:
	dev->mtu = new_mtu;
	return 0;
}

static int update_rx_buffer_headroom(struct dpaa2_eth_priv *priv, bool has_xdp)
{
	struct dpni_buffer_layout buf_layout = {0};
	int err;

	err = dpni_get_buffer_layout(priv->mc_io, 0, priv->mc_token,
				     DPNI_QUEUE_RX, &buf_layout);
	if (err) {
		netdev_err(priv->net_dev, "dpni_get_buffer_layout failed\n");
		return err;
	}

	/* Reserve extra headroom for XDP header size changes */
	buf_layout.data_head_room = dpaa2_eth_rx_head_room(priv) +
				    (has_xdp ? XDP_PACKET_HEADROOM : 0);
	buf_layout.options = DPNI_BUF_LAYOUT_OPT_DATA_HEAD_ROOM;
	err = dpni_set_buffer_layout(priv->mc_io, 0, priv->mc_token,
				     DPNI_QUEUE_RX, &buf_layout);
	if (err) {
		netdev_err(priv->net_dev, "dpni_set_buffer_layout failed\n");
		return err;
	}

	return 0;
}

static int setup_xdp(struct net_device *dev, struct bpf_prog *prog)
{
	struct dpaa2_eth_priv *priv = netdev_priv(dev);
	struct dpaa2_eth_channel *ch;
	struct bpf_prog *old;
	bool up, need_update;
	int i, err;

	if (prog && !xdp_mtu_valid(priv, dev->mtu))
		return -EINVAL;

	if (prog) {
		prog = bpf_prog_add(prog, priv->num_channels);
		if (IS_ERR(prog))
			return PTR_ERR(prog);
	}

	up = netif_running(dev);
	need_update = (!!priv->xdp_prog != !!prog);

	if (up)
		dpaa2_eth_stop(dev);

	/* While in xdp mode, enforce a maximum Rx frame size based on MTU.
	 * Also, when switching between xdp/non-xdp modes we need to reconfigure
	 * our Rx buffer layout. Buffer pool was drained on dpaa2_eth_stop,
	 * so we are sure no old format buffers will be used from now on.
	 */
	if (need_update) {
		err = set_rx_mfl(priv, dev->mtu, !!prog);
		if (err)
			goto out_err;
		err = update_rx_buffer_headroom(priv, !!prog);
		if (err)
			goto out_err;
	}

	old = xchg(&priv->xdp_prog, prog);
	if (old)
		bpf_prog_put(old);

	for (i = 0; i < priv->num_channels; i++) {
		ch = priv->channel[i];
		old = xchg(&ch->xdp.prog, prog);
		if (old)
			bpf_prog_put(old);
	}

	if (up) {
		err = dpaa2_eth_open(dev);
		if (err)
			return err;
	}

	return 0;

out_err:
	if (prog)
		bpf_prog_sub(prog, priv->num_channels);
	if (up)
		dpaa2_eth_open(dev);

	return err;
}

static int dpaa2_eth_xdp(struct net_device *dev, struct netdev_bpf *xdp)
{
	struct dpaa2_eth_priv *priv = netdev_priv(dev);

	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return setup_xdp(dev, xdp->prog);
	case XDP_QUERY_PROG:
		xdp->prog_id = priv->xdp_prog ? priv->xdp_prog->aux->id : 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct net_device_ops dpaa2_eth_ops = {
	.ndo_open = dpaa2_eth_open,
	.ndo_start_xmit = dpaa2_eth_tx,
	.ndo_stop = dpaa2_eth_stop,
	.ndo_set_mac_address = dpaa2_eth_set_addr,
	.ndo_get_stats64 = dpaa2_eth_get_stats,
	.ndo_set_rx_mode = dpaa2_eth_set_rx_mode,
	.ndo_set_features = dpaa2_eth_set_features,
	.ndo_do_ioctl = dpaa2_eth_ioctl,
	.ndo_change_mtu = dpaa2_eth_change_mtu,
	.ndo_bpf = dpaa2_eth_xdp,
};

static void cdan_cb(struct dpaa2_io_notification_ctx *ctx)
{
	struct dpaa2_eth_channel *ch;

	ch = container_of(ctx, struct dpaa2_eth_channel, nctx);

	/* Update NAPI statistics */
	ch->stats.cdan++;

	napi_schedule_irqoff(&ch->napi);
}

/* Allocate and configure a DPCON object */
static struct fsl_mc_device *setup_dpcon(struct dpaa2_eth_priv *priv)
{
	struct fsl_mc_device *dpcon;
	struct device *dev = priv->net_dev->dev.parent;
	struct dpcon_attr attrs;
	int err;

	err = fsl_mc_object_allocate(to_fsl_mc_device(dev),
				     FSL_MC_POOL_DPCON, &dpcon);
	if (err) {
		if (err == -ENXIO)
			err = -EPROBE_DEFER;
		else
			dev_info(dev, "Not enough DPCONs, will go on as-is\n");
		return ERR_PTR(err);
	}

	err = dpcon_open(priv->mc_io, 0, dpcon->obj_desc.id, &dpcon->mc_handle);
	if (err) {
		dev_err(dev, "dpcon_open() failed\n");
		goto free;
	}

	err = dpcon_reset(priv->mc_io, 0, dpcon->mc_handle);
	if (err) {
		dev_err(dev, "dpcon_reset() failed\n");
		goto close;
	}

	err = dpcon_get_attributes(priv->mc_io, 0, dpcon->mc_handle, &attrs);
	if (err) {
		dev_err(dev, "dpcon_get_attributes() failed\n");
		goto close;
	}

	err = dpcon_enable(priv->mc_io, 0, dpcon->mc_handle);
	if (err) {
		dev_err(dev, "dpcon_enable() failed\n");
		goto close;
	}

	return dpcon;

close:
	dpcon_close(priv->mc_io, 0, dpcon->mc_handle);
free:
	fsl_mc_object_free(dpcon);

	return NULL;
}

static void free_dpcon(struct dpaa2_eth_priv *priv,
		       struct fsl_mc_device *dpcon)
{
	dpcon_disable(priv->mc_io, 0, dpcon->mc_handle);
	dpcon_close(priv->mc_io, 0, dpcon->mc_handle);
	fsl_mc_object_free(dpcon);
}

static struct dpaa2_eth_channel *
alloc_channel(struct dpaa2_eth_priv *priv)
{
	struct dpaa2_eth_channel *channel;
	struct dpcon_attr attr;
	struct device *dev = priv->net_dev->dev.parent;
	int err;

	channel = kzalloc(sizeof(*channel), GFP_KERNEL);
	if (!channel)
		return NULL;

	channel->dpcon = setup_dpcon(priv);
	if (IS_ERR_OR_NULL(channel->dpcon)) {
		err = PTR_ERR(channel->dpcon);
		goto err_setup;
	}

	err = dpcon_get_attributes(priv->mc_io, 0, channel->dpcon->mc_handle,
				   &attr);
	if (err) {
		dev_err(dev, "dpcon_get_attributes() failed\n");
		goto err_get_attr;
	}

	channel->dpcon_id = attr.id;
	channel->ch_id = attr.qbman_ch_id;
	channel->priv = priv;

	return channel;

err_get_attr:
	free_dpcon(priv, channel->dpcon);
err_setup:
	kfree(channel);
	return ERR_PTR(err);
}

static void free_channel(struct dpaa2_eth_priv *priv,
			 struct dpaa2_eth_channel *channel)
{
	free_dpcon(priv, channel->dpcon);
	kfree(channel);
}

/* DPIO setup: allocate and configure QBMan channels, setup core affinity
 * and register data availability notifications
 */
static int setup_dpio(struct dpaa2_eth_priv *priv)
{
	struct dpaa2_io_notification_ctx *nctx;
	struct dpaa2_eth_channel *channel;
	struct dpcon_notification_cfg dpcon_notif_cfg;
	struct device *dev = priv->net_dev->dev.parent;
	int i, err;

	/* We want the ability to spread ingress traffic (RX, TX conf) to as
	 * many cores as possible, so we need one channel for each core
	 * (unless there's fewer queues than cores, in which case the extra
	 * channels would be wasted).
	 * Allocate one channel per core and register it to the core's
	 * affine DPIO. If not enough channels are available for all cores
	 * or if some cores don't have an affine DPIO, there will be no
	 * ingress frame processing on those cores.
	 */
	cpumask_clear(&priv->dpio_cpumask);
	for_each_online_cpu(i) {
		/* Try to allocate a channel */
		channel = alloc_channel(priv);
		if (IS_ERR_OR_NULL(channel)) {
			err = PTR_ERR(channel);
			if (err != -EPROBE_DEFER)
				dev_info(dev,
					 "No affine channel for cpu %d and above\n", i);
			goto err_alloc_ch;
		}

		priv->channel[priv->num_channels] = channel;

		nctx = &channel->nctx;
		nctx->is_cdan = 1;
		nctx->cb = cdan_cb;
		nctx->id = channel->ch_id;
		nctx->desired_cpu = i;

		/* Register the new context */
		channel->dpio = dpaa2_io_service_select(i);
		err = dpaa2_io_service_register(channel->dpio, nctx);
		if (err) {
			dev_dbg(dev, "No affine DPIO for cpu %d\n", i);
			/* If no affine DPIO for this core, there's probably
			 * none available for next cores either. Signal we want
			 * to retry later, in case the DPIO devices weren't
			 * probed yet.
			 */
			err = -EPROBE_DEFER;
			goto err_service_reg;
		}

		/* Register DPCON notification with MC */
		dpcon_notif_cfg.dpio_id = nctx->dpio_id;
		dpcon_notif_cfg.priority = 0;
		dpcon_notif_cfg.user_ctx = nctx->qman64;
		err = dpcon_set_notification(priv->mc_io, 0,
					     channel->dpcon->mc_handle,
					     &dpcon_notif_cfg);
		if (err) {
			dev_err(dev, "dpcon_set_notification failed()\n");
			goto err_set_cdan;
		}

		/* If we managed to allocate a channel and also found an affine
		 * DPIO for this core, add it to the final mask
		 */
		cpumask_set_cpu(i, &priv->dpio_cpumask);
		priv->num_channels++;

		/* Stop if we already have enough channels to accommodate all
		 * RX and TX conf queues
		 */
		if (priv->num_channels == priv->dpni_attrs.num_queues)
			break;
	}

	return 0;

err_set_cdan:
	dpaa2_io_service_deregister(channel->dpio, nctx);
err_service_reg:
	free_channel(priv, channel);
err_alloc_ch:
	if (err == -EPROBE_DEFER)
		return err;

	if (cpumask_empty(&priv->dpio_cpumask)) {
		dev_err(dev, "No cpu with an affine DPIO/DPCON\n");
		return -ENODEV;
	}

	dev_info(dev, "Cores %*pbl available for processing ingress traffic\n",
		 cpumask_pr_args(&priv->dpio_cpumask));

	return 0;
}

static void free_dpio(struct dpaa2_eth_priv *priv)
{
	int i;
	struct dpaa2_eth_channel *ch;

	/* deregister CDAN notifications and free channels */
	for (i = 0; i < priv->num_channels; i++) {
		ch = priv->channel[i];
		dpaa2_io_service_deregister(ch->dpio, &ch->nctx);
		free_channel(priv, ch);
	}
}

static struct dpaa2_eth_channel *get_affine_channel(struct dpaa2_eth_priv *priv,
						    int cpu)
{
	struct device *dev = priv->net_dev->dev.parent;
	int i;

	for (i = 0; i < priv->num_channels; i++)
		if (priv->channel[i]->nctx.desired_cpu == cpu)
			return priv->channel[i];

	/* We should never get here. Issue a warning and return
	 * the first channel, because it's still better than nothing
	 */
	dev_warn(dev, "No affine channel found for cpu %d\n", cpu);

	return priv->channel[0];
}

static void set_fq_affinity(struct dpaa2_eth_priv *priv)
{
	struct device *dev = priv->net_dev->dev.parent;
	struct cpumask xps_mask;
	struct dpaa2_eth_fq *fq;
	int rx_cpu, txc_cpu;
	int i, err;

	/* For each FQ, pick one channel/CPU to deliver frames to.
	 * This may well change at runtime, either through irqbalance or
	 * through direct user intervention.
	 */
	rx_cpu = txc_cpu = cpumask_first(&priv->dpio_cpumask);

	for (i = 0; i < priv->num_fqs; i++) {
		fq = &priv->fq[i];
		switch (fq->type) {
		case DPAA2_RX_FQ:
			fq->target_cpu = rx_cpu;
			rx_cpu = cpumask_next(rx_cpu, &priv->dpio_cpumask);
			if (rx_cpu >= nr_cpu_ids)
				rx_cpu = cpumask_first(&priv->dpio_cpumask);
			break;
		case DPAA2_TX_CONF_FQ:
			fq->target_cpu = txc_cpu;

			/* Tell the stack to affine to txc_cpu the Tx queue
			 * associated with the confirmation one
			 */
			cpumask_clear(&xps_mask);
			cpumask_set_cpu(txc_cpu, &xps_mask);
			err = netif_set_xps_queue(priv->net_dev, &xps_mask,
						  fq->flowid);
			if (err)
				dev_err(dev, "Error setting XPS queue\n");

			txc_cpu = cpumask_next(txc_cpu, &priv->dpio_cpumask);
			if (txc_cpu >= nr_cpu_ids)
				txc_cpu = cpumask_first(&priv->dpio_cpumask);
			break;
		default:
			dev_err(dev, "Unknown FQ type: %d\n", fq->type);
		}
		fq->channel = get_affine_channel(priv, fq->target_cpu);
	}
}

static void setup_fqs(struct dpaa2_eth_priv *priv)
{
	int i;

	/* We have one TxConf FQ per Tx flow.
	 * The number of Tx and Rx queues is the same.
	 * Tx queues come first in the fq array.
	 */
	for (i = 0; i < dpaa2_eth_queue_count(priv); i++) {
		priv->fq[priv->num_fqs].type = DPAA2_TX_CONF_FQ;
		priv->fq[priv->num_fqs].consume = dpaa2_eth_tx_conf;
		priv->fq[priv->num_fqs++].flowid = (u16)i;
	}

	for (i = 0; i < dpaa2_eth_queue_count(priv); i++) {
		priv->fq[priv->num_fqs].type = DPAA2_RX_FQ;
		priv->fq[priv->num_fqs].consume = dpaa2_eth_rx;
		priv->fq[priv->num_fqs++].flowid = (u16)i;
	}

	/* For each FQ, decide on which core to process incoming frames */
	set_fq_affinity(priv);
}

/* Allocate and configure one buffer pool for each interface */
static int setup_dpbp(struct dpaa2_eth_priv *priv)
{
	int err;
	struct fsl_mc_device *dpbp_dev;
	struct device *dev = priv->net_dev->dev.parent;
	struct dpbp_attr dpbp_attrs;

	err = fsl_mc_object_allocate(to_fsl_mc_device(dev), FSL_MC_POOL_DPBP,
				     &dpbp_dev);
	if (err) {
		if (err == -ENXIO)
			err = -EPROBE_DEFER;
		else
			dev_err(dev, "DPBP device allocation failed\n");
		return err;
	}

	priv->dpbp_dev = dpbp_dev;

	err = dpbp_open(priv->mc_io, 0, priv->dpbp_dev->obj_desc.id,
			&dpbp_dev->mc_handle);
	if (err) {
		dev_err(dev, "dpbp_open() failed\n");
		goto err_open;
	}

	err = dpbp_reset(priv->mc_io, 0, dpbp_dev->mc_handle);
	if (err) {
		dev_err(dev, "dpbp_reset() failed\n");
		goto err_reset;
	}

	err = dpbp_enable(priv->mc_io, 0, dpbp_dev->mc_handle);
	if (err) {
		dev_err(dev, "dpbp_enable() failed\n");
		goto err_enable;
	}

	err = dpbp_get_attributes(priv->mc_io, 0, dpbp_dev->mc_handle,
				  &dpbp_attrs);
	if (err) {
		dev_err(dev, "dpbp_get_attributes() failed\n");
		goto err_get_attr;
	}
	priv->bpid = dpbp_attrs.bpid;

	return 0;

err_get_attr:
	dpbp_disable(priv->mc_io, 0, dpbp_dev->mc_handle);
err_enable:
err_reset:
	dpbp_close(priv->mc_io, 0, dpbp_dev->mc_handle);
err_open:
	fsl_mc_object_free(dpbp_dev);

	return err;
}

static void free_dpbp(struct dpaa2_eth_priv *priv)
{
	drain_pool(priv);
	dpbp_disable(priv->mc_io, 0, priv->dpbp_dev->mc_handle);
	dpbp_close(priv->mc_io, 0, priv->dpbp_dev->mc_handle);
	fsl_mc_object_free(priv->dpbp_dev);
}

static int set_buffer_layout(struct dpaa2_eth_priv *priv)
{
	struct device *dev = priv->net_dev->dev.parent;
	struct dpni_buffer_layout buf_layout = {0};
	u16 rx_buf_align;
	int err;

	/* We need to check for WRIOP version 1.0.0, but depending on the MC
	 * version, this number is not always provided correctly on rev1.
	 * We need to check for both alternatives in this situation.
	 */
	if (priv->dpni_attrs.wriop_version == DPAA2_WRIOP_VERSION(0, 0, 0) ||
	    priv->dpni_attrs.wriop_version == DPAA2_WRIOP_VERSION(1, 0, 0))
		rx_buf_align = DPAA2_ETH_RX_BUF_ALIGN_REV1;
	else
		rx_buf_align = DPAA2_ETH_RX_BUF_ALIGN;

	/* tx buffer */
	buf_layout.private_data_size = DPAA2_ETH_SWA_SIZE;
	buf_layout.pass_timestamp = true;
	buf_layout.options = DPNI_BUF_LAYOUT_OPT_PRIVATE_DATA_SIZE |
			     DPNI_BUF_LAYOUT_OPT_TIMESTAMP;
	err = dpni_set_buffer_layout(priv->mc_io, 0, priv->mc_token,
				     DPNI_QUEUE_TX, &buf_layout);
	if (err) {
		dev_err(dev, "dpni_set_buffer_layout(TX) failed\n");
		return err;
	}

	/* tx-confirm buffer */
	buf_layout.options = DPNI_BUF_LAYOUT_OPT_TIMESTAMP;
	err = dpni_set_buffer_layout(priv->mc_io, 0, priv->mc_token,
				     DPNI_QUEUE_TX_CONFIRM, &buf_layout);
	if (err) {
		dev_err(dev, "dpni_set_buffer_layout(TX_CONF) failed\n");
		return err;
	}

	/* Now that we've set our tx buffer layout, retrieve the minimum
	 * required tx data offset.
	 */
	err = dpni_get_tx_data_offset(priv->mc_io, 0, priv->mc_token,
				      &priv->tx_data_offset);
	if (err) {
		dev_err(dev, "dpni_get_tx_data_offset() failed\n");
		return err;
	}

	if ((priv->tx_data_offset % 64) != 0)
		dev_warn(dev, "Tx data offset (%d) not a multiple of 64B\n",
			 priv->tx_data_offset);

	/* rx buffer */
	buf_layout.pass_frame_status = true;
	buf_layout.pass_parser_result = true;
	buf_layout.data_align = rx_buf_align;
	buf_layout.data_head_room = dpaa2_eth_rx_head_room(priv);
	buf_layout.private_data_size = 0;
	buf_layout.options = DPNI_BUF_LAYOUT_OPT_PARSER_RESULT |
			     DPNI_BUF_LAYOUT_OPT_FRAME_STATUS |
			     DPNI_BUF_LAYOUT_OPT_DATA_ALIGN |
			     DPNI_BUF_LAYOUT_OPT_DATA_HEAD_ROOM |
			     DPNI_BUF_LAYOUT_OPT_TIMESTAMP;
	err = dpni_set_buffer_layout(priv->mc_io, 0, priv->mc_token,
				     DPNI_QUEUE_RX, &buf_layout);
	if (err) {
		dev_err(dev, "dpni_set_buffer_layout(RX) failed\n");
		return err;
	}

	return 0;
}

#define DPNI_ENQUEUE_FQID_VER_MAJOR	7
#define DPNI_ENQUEUE_FQID_VER_MINOR	9

static inline int dpaa2_eth_enqueue_qd(struct dpaa2_eth_priv *priv,
				       struct dpaa2_eth_fq *fq,
				       struct dpaa2_fd *fd, u8 prio)
{
	return dpaa2_io_service_enqueue_qd(fq->channel->dpio,
					   priv->tx_qdid, prio,
					   fq->tx_qdbin, fd);
}

static inline int dpaa2_eth_enqueue_fq(struct dpaa2_eth_priv *priv,
				       struct dpaa2_eth_fq *fq,
				       struct dpaa2_fd *fd,
				       u8 prio __always_unused)
{
	return dpaa2_io_service_enqueue_fq(fq->channel->dpio,
					   fq->tx_fqid, fd);
}

static void set_enqueue_mode(struct dpaa2_eth_priv *priv)
{
	if (dpaa2_eth_cmp_dpni_ver(priv, DPNI_ENQUEUE_FQID_VER_MAJOR,
				   DPNI_ENQUEUE_FQID_VER_MINOR) < 0)
		priv->enqueue = dpaa2_eth_enqueue_qd;
	else
		priv->enqueue = dpaa2_eth_enqueue_fq;
}

/* Configure the DPNI object this interface is associated with */
static int setup_dpni(struct fsl_mc_device *ls_dev)
{
	struct device *dev = &ls_dev->dev;
	struct dpaa2_eth_priv *priv;
	struct net_device *net_dev;
	int err;

	net_dev = dev_get_drvdata(dev);
	priv = netdev_priv(net_dev);

	/* get a handle for the DPNI object */
	err = dpni_open(priv->mc_io, 0, ls_dev->obj_desc.id, &priv->mc_token);
	if (err) {
		dev_err(dev, "dpni_open() failed\n");
		return err;
	}

	/* Check if we can work with this DPNI object */
	err = dpni_get_api_version(priv->mc_io, 0, &priv->dpni_ver_major,
				   &priv->dpni_ver_minor);
	if (err) {
		dev_err(dev, "dpni_get_api_version() failed\n");
		goto close;
	}
	if (dpaa2_eth_cmp_dpni_ver(priv, DPNI_VER_MAJOR, DPNI_VER_MINOR) < 0) {
		dev_err(dev, "DPNI version %u.%u not supported, need >= %u.%u\n",
			priv->dpni_ver_major, priv->dpni_ver_minor,
			DPNI_VER_MAJOR, DPNI_VER_MINOR);
		err = -ENOTSUPP;
		goto close;
	}

	ls_dev->mc_io = priv->mc_io;
	ls_dev->mc_handle = priv->mc_token;

	err = dpni_reset(priv->mc_io, 0, priv->mc_token);
	if (err) {
		dev_err(dev, "dpni_reset() failed\n");
		goto close;
	}

	err = dpni_get_attributes(priv->mc_io, 0, priv->mc_token,
				  &priv->dpni_attrs);
	if (err) {
		dev_err(dev, "dpni_get_attributes() failed (err=%d)\n", err);
		goto close;
	}

	err = set_buffer_layout(priv);
	if (err)
		goto close;

	set_enqueue_mode(priv);

	priv->cls_rules = devm_kzalloc(dev, sizeof(struct dpaa2_eth_cls_rule) *
				       dpaa2_eth_fs_count(priv), GFP_KERNEL);
	if (!priv->cls_rules)
		goto close;

	return 0;

close:
	dpni_close(priv->mc_io, 0, priv->mc_token);

	return err;
}

static void free_dpni(struct dpaa2_eth_priv *priv)
{
	int err;

	err = dpni_reset(priv->mc_io, 0, priv->mc_token);
	if (err)
		netdev_warn(priv->net_dev, "dpni_reset() failed (err %d)\n",
			    err);

	dpni_close(priv->mc_io, 0, priv->mc_token);
}

static int setup_rx_flow(struct dpaa2_eth_priv *priv,
			 struct dpaa2_eth_fq *fq)
{
	struct device *dev = priv->net_dev->dev.parent;
	struct dpni_queue queue;
	struct dpni_queue_id qid;
	struct dpni_taildrop td;
	int err;

	err = dpni_get_queue(priv->mc_io, 0, priv->mc_token,
			     DPNI_QUEUE_RX, 0, fq->flowid, &queue, &qid);
	if (err) {
		dev_err(dev, "dpni_get_queue(RX) failed\n");
		return err;
	}

	fq->fqid = qid.fqid;

	queue.destination.id = fq->channel->dpcon_id;
	queue.destination.type = DPNI_DEST_DPCON;
	queue.destination.priority = 1;
	queue.user_context = (u64)(uintptr_t)fq;
	err = dpni_set_queue(priv->mc_io, 0, priv->mc_token,
			     DPNI_QUEUE_RX, 0, fq->flowid,
			     DPNI_QUEUE_OPT_USER_CTX | DPNI_QUEUE_OPT_DEST,
			     &queue);
	if (err) {
		dev_err(dev, "dpni_set_queue(RX) failed\n");
		return err;
	}

	td.enable = 1;
	td.threshold = DPAA2_ETH_TAILDROP_THRESH;
	err = dpni_set_taildrop(priv->mc_io, 0, priv->mc_token, DPNI_CP_QUEUE,
				DPNI_QUEUE_RX, 0, fq->flowid, &td);
	if (err) {
		dev_err(dev, "dpni_set_threshold() failed\n");
		return err;
	}

	return 0;
}

static int setup_tx_flow(struct dpaa2_eth_priv *priv,
			 struct dpaa2_eth_fq *fq)
{
	struct device *dev = priv->net_dev->dev.parent;
	struct dpni_queue queue;
	struct dpni_queue_id qid;
	int err;

	err = dpni_get_queue(priv->mc_io, 0, priv->mc_token,
			     DPNI_QUEUE_TX, 0, fq->flowid, &queue, &qid);
	if (err) {
		dev_err(dev, "dpni_get_queue(TX) failed\n");
		return err;
	}

	fq->tx_qdbin = qid.qdbin;
	fq->tx_fqid = qid.fqid;

	err = dpni_get_queue(priv->mc_io, 0, priv->mc_token,
			     DPNI_QUEUE_TX_CONFIRM, 0, fq->flowid,
			     &queue, &qid);
	if (err) {
		dev_err(dev, "dpni_get_queue(TX_CONF) failed\n");
		return err;
	}

	fq->fqid = qid.fqid;

	queue.destination.id = fq->channel->dpcon_id;
	queue.destination.type = DPNI_DEST_DPCON;
	queue.destination.priority = 0;
	queue.user_context = (u64)(uintptr_t)fq;
	err = dpni_set_queue(priv->mc_io, 0, priv->mc_token,
			     DPNI_QUEUE_TX_CONFIRM, 0, fq->flowid,
			     DPNI_QUEUE_OPT_USER_CTX | DPNI_QUEUE_OPT_DEST,
			     &queue);
	if (err) {
		dev_err(dev, "dpni_set_queue(TX_CONF) failed\n");
		return err;
	}

	return 0;
}

/* Supported header fields for Rx hash distribution key */
static const struct dpaa2_eth_dist_fields dist_fields[] = {
	{
		/* L2 header */
		.rxnfc_field = RXH_L2DA,
		.cls_prot = NET_PROT_ETH,
		.cls_field = NH_FLD_ETH_DA,
		.size = 6,
	}, {
		.cls_prot = NET_PROT_ETH,
		.cls_field = NH_FLD_ETH_SA,
		.size = 6,
	}, {
		/* This is the last ethertype field parsed:
		 * depending on frame format, it can be the MAC ethertype
		 * or the VLAN etype.
		 */
		.cls_prot = NET_PROT_ETH,
		.cls_field = NH_FLD_ETH_TYPE,
		.size = 2,
	}, {
		/* VLAN header */
		.rxnfc_field = RXH_VLAN,
		.cls_prot = NET_PROT_VLAN,
		.cls_field = NH_FLD_VLAN_TCI,
		.size = 2,
	}, {
		/* IP header */
		.rxnfc_field = RXH_IP_SRC,
		.cls_prot = NET_PROT_IP,
		.cls_field = NH_FLD_IP_SRC,
		.size = 4,
	}, {
		.rxnfc_field = RXH_IP_DST,
		.cls_prot = NET_PROT_IP,
		.cls_field = NH_FLD_IP_DST,
		.size = 4,
	}, {
		.rxnfc_field = RXH_L3_PROTO,
		.cls_prot = NET_PROT_IP,
		.cls_field = NH_FLD_IP_PROTO,
		.size = 1,
	}, {
		/* Using UDP ports, this is functionally equivalent to raw
		 * byte pairs from L4 header.
		 */
		.rxnfc_field = RXH_L4_B_0_1,
		.cls_prot = NET_PROT_UDP,
		.cls_field = NH_FLD_UDP_PORT_SRC,
		.size = 2,
	}, {
		.rxnfc_field = RXH_L4_B_2_3,
		.cls_prot = NET_PROT_UDP,
		.cls_field = NH_FLD_UDP_PORT_DST,
		.size = 2,
	},
};

/* Configure the Rx hash key using the legacy API */
static int config_legacy_hash_key(struct dpaa2_eth_priv *priv, dma_addr_t key)
{
	struct device *dev = priv->net_dev->dev.parent;
	struct dpni_rx_tc_dist_cfg dist_cfg;
	int err;

	memset(&dist_cfg, 0, sizeof(dist_cfg));

	dist_cfg.key_cfg_iova = key;
	dist_cfg.dist_size = dpaa2_eth_queue_count(priv);
	dist_cfg.dist_mode = DPNI_DIST_MODE_HASH;

	err = dpni_set_rx_tc_dist(priv->mc_io, 0, priv->mc_token, 0, &dist_cfg);
	if (err)
		dev_err(dev, "dpni_set_rx_tc_dist failed\n");

	return err;
}

/* Configure the Rx hash key using the new API */
static int config_hash_key(struct dpaa2_eth_priv *priv, dma_addr_t key)
{
	struct device *dev = priv->net_dev->dev.parent;
	struct dpni_rx_dist_cfg dist_cfg;
	int err;

	memset(&dist_cfg, 0, sizeof(dist_cfg));

	dist_cfg.key_cfg_iova = key;
	dist_cfg.dist_size = dpaa2_eth_queue_count(priv);
	dist_cfg.enable = 1;

	err = dpni_set_rx_hash_dist(priv->mc_io, 0, priv->mc_token, &dist_cfg);
	if (err)
		dev_err(dev, "dpni_set_rx_hash_dist failed\n");

	return err;
}

/* Configure the Rx flow classification key */
static int config_cls_key(struct dpaa2_eth_priv *priv, dma_addr_t key)
{
	struct device *dev = priv->net_dev->dev.parent;
	struct dpni_rx_dist_cfg dist_cfg;
	int err;

	memset(&dist_cfg, 0, sizeof(dist_cfg));

	dist_cfg.key_cfg_iova = key;
	dist_cfg.dist_size = dpaa2_eth_queue_count(priv);
	dist_cfg.enable = 1;

	err = dpni_set_rx_fs_dist(priv->mc_io, 0, priv->mc_token, &dist_cfg);
	if (err)
		dev_err(dev, "dpni_set_rx_fs_dist failed\n");

	return err;
}

/* Size of the Rx flow classification key */
int dpaa2_eth_cls_key_size(void)
{
	int i, size = 0;

	for (i = 0; i < ARRAY_SIZE(dist_fields); i++)
		size += dist_fields[i].size;

	return size;
}

/* Offset of header field in Rx classification key */
int dpaa2_eth_cls_fld_off(int prot, int field)
{
	int i, off = 0;

	for (i = 0; i < ARRAY_SIZE(dist_fields); i++) {
		if (dist_fields[i].cls_prot == prot &&
		    dist_fields[i].cls_field == field)
			return off;
		off += dist_fields[i].size;
	}

	WARN_ONCE(1, "Unsupported header field used for Rx flow cls\n");
	return 0;
}

/* Set Rx distribution (hash or flow classification) key
 * flags is a combination of RXH_ bits
 */
static int dpaa2_eth_set_dist_key(struct net_device *net_dev,
				  enum dpaa2_eth_rx_dist type, u64 flags)
{
	struct device *dev = net_dev->dev.parent;
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);
	struct dpkg_profile_cfg cls_cfg;
	u32 rx_hash_fields = 0;
	dma_addr_t key_iova;
	u8 *dma_mem;
	int i;
	int err = 0;

	memset(&cls_cfg, 0, sizeof(cls_cfg));

	for (i = 0; i < ARRAY_SIZE(dist_fields); i++) {
		struct dpkg_extract *key =
			&cls_cfg.extracts[cls_cfg.num_extracts];

		/* For Rx hashing key we set only the selected fields.
		 * For Rx flow classification key we set all supported fields
		 */
		if (type == DPAA2_ETH_RX_DIST_HASH) {
			if (!(flags & dist_fields[i].rxnfc_field))
				continue;
			rx_hash_fields |= dist_fields[i].rxnfc_field;
		}

		if (cls_cfg.num_extracts >= DPKG_MAX_NUM_OF_EXTRACTS) {
			dev_err(dev, "error adding key extraction rule, too many rules?\n");
			return -E2BIG;
		}

		key->type = DPKG_EXTRACT_FROM_HDR;
		key->extract.from_hdr.prot = dist_fields[i].cls_prot;
		key->extract.from_hdr.type = DPKG_FULL_FIELD;
		key->extract.from_hdr.field = dist_fields[i].cls_field;
		cls_cfg.num_extracts++;
	}

	dma_mem = kzalloc(DPAA2_CLASSIFIER_DMA_SIZE, GFP_KERNEL);
	if (!dma_mem)
		return -ENOMEM;

	err = dpni_prepare_key_cfg(&cls_cfg, dma_mem);
	if (err) {
		dev_err(dev, "dpni_prepare_key_cfg error %d\n", err);
		goto free_key;
	}

	/* Prepare for setting the rx dist */
	key_iova = dma_map_single(dev, dma_mem, DPAA2_CLASSIFIER_DMA_SIZE,
				  DMA_TO_DEVICE);
	if (dma_mapping_error(dev, key_iova)) {
		dev_err(dev, "DMA mapping failed\n");
		err = -ENOMEM;
		goto free_key;
	}

	if (type == DPAA2_ETH_RX_DIST_HASH) {
		if (dpaa2_eth_has_legacy_dist(priv))
			err = config_legacy_hash_key(priv, key_iova);
		else
			err = config_hash_key(priv, key_iova);
	} else {
		err = config_cls_key(priv, key_iova);
	}

	dma_unmap_single(dev, key_iova, DPAA2_CLASSIFIER_DMA_SIZE,
			 DMA_TO_DEVICE);
	if (!err && type == DPAA2_ETH_RX_DIST_HASH)
		priv->rx_hash_fields = rx_hash_fields;

free_key:
	kfree(dma_mem);
	return err;
}

int dpaa2_eth_set_hash(struct net_device *net_dev, u64 flags)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);

	if (!dpaa2_eth_hash_enabled(priv))
		return -EOPNOTSUPP;

	return dpaa2_eth_set_dist_key(net_dev, DPAA2_ETH_RX_DIST_HASH, flags);
}

static int dpaa2_eth_set_cls(struct dpaa2_eth_priv *priv)
{
	struct device *dev = priv->net_dev->dev.parent;

	/* Check if we actually support Rx flow classification */
	if (dpaa2_eth_has_legacy_dist(priv)) {
		dev_dbg(dev, "Rx cls not supported by current MC version\n");
		return -EOPNOTSUPP;
	}

	if (priv->dpni_attrs.options & DPNI_OPT_NO_FS ||
	    !(priv->dpni_attrs.options & DPNI_OPT_HAS_KEY_MASKING)) {
		dev_dbg(dev, "Rx cls disabled in DPNI options\n");
		return -EOPNOTSUPP;
	}

	if (!dpaa2_eth_hash_enabled(priv)) {
		dev_dbg(dev, "Rx cls disabled for single queue DPNIs\n");
		return -EOPNOTSUPP;
	}

	priv->rx_cls_enabled = 1;

	return dpaa2_eth_set_dist_key(priv->net_dev, DPAA2_ETH_RX_DIST_CLS, 0);
}

/* Bind the DPNI to its needed objects and resources: buffer pool, DPIOs,
 * frame queues and channels
 */
static int bind_dpni(struct dpaa2_eth_priv *priv)
{
	struct net_device *net_dev = priv->net_dev;
	struct device *dev = net_dev->dev.parent;
	struct dpni_pools_cfg pools_params;
	struct dpni_error_cfg err_cfg;
	int err = 0;
	int i;

	pools_params.num_dpbp = 1;
	pools_params.pools[0].dpbp_id = priv->dpbp_dev->obj_desc.id;
	pools_params.pools[0].backup_pool = 0;
	pools_params.pools[0].buffer_size = DPAA2_ETH_RX_BUF_SIZE;
	err = dpni_set_pools(priv->mc_io, 0, priv->mc_token, &pools_params);
	if (err) {
		dev_err(dev, "dpni_set_pools() failed\n");
		return err;
	}

	/* have the interface implicitly distribute traffic based on
	 * the default hash key
	 */
	err = dpaa2_eth_set_hash(net_dev, DPAA2_RXH_DEFAULT);
	if (err && err != -EOPNOTSUPP)
		dev_err(dev, "Failed to configure hashing\n");

	/* Configure the flow classification key; it includes all
	 * supported header fields and cannot be modified at runtime
	 */
	err = dpaa2_eth_set_cls(priv);
	if (err && err != -EOPNOTSUPP)
		dev_err(dev, "Failed to configure Rx classification key\n");

	/* Configure handling of error frames */
	err_cfg.errors = DPAA2_FAS_RX_ERR_MASK;
	err_cfg.set_frame_annotation = 1;
	err_cfg.error_action = DPNI_ERROR_ACTION_DISCARD;
	err = dpni_set_errors_behavior(priv->mc_io, 0, priv->mc_token,
				       &err_cfg);
	if (err) {
		dev_err(dev, "dpni_set_errors_behavior failed\n");
		return err;
	}

	/* Configure Rx and Tx conf queues to generate CDANs */
	for (i = 0; i < priv->num_fqs; i++) {
		switch (priv->fq[i].type) {
		case DPAA2_RX_FQ:
			err = setup_rx_flow(priv, &priv->fq[i]);
			break;
		case DPAA2_TX_CONF_FQ:
			err = setup_tx_flow(priv, &priv->fq[i]);
			break;
		default:
			dev_err(dev, "Invalid FQ type %d\n", priv->fq[i].type);
			return -EINVAL;
		}
		if (err)
			return err;
	}

	err = dpni_get_qdid(priv->mc_io, 0, priv->mc_token,
			    DPNI_QUEUE_TX, &priv->tx_qdid);
	if (err) {
		dev_err(dev, "dpni_get_qdid() failed\n");
		return err;
	}

	return 0;
}

/* Allocate rings for storing incoming frame descriptors */
static int alloc_rings(struct dpaa2_eth_priv *priv)
{
	struct net_device *net_dev = priv->net_dev;
	struct device *dev = net_dev->dev.parent;
	int i;

	for (i = 0; i < priv->num_channels; i++) {
		priv->channel[i]->store =
			dpaa2_io_store_create(DPAA2_ETH_STORE_SIZE, dev);
		if (!priv->channel[i]->store) {
			netdev_err(net_dev, "dpaa2_io_store_create() failed\n");
			goto err_ring;
		}
	}

	return 0;

err_ring:
	for (i = 0; i < priv->num_channels; i++) {
		if (!priv->channel[i]->store)
			break;
		dpaa2_io_store_destroy(priv->channel[i]->store);
	}

	return -ENOMEM;
}

static void free_rings(struct dpaa2_eth_priv *priv)
{
	int i;

	for (i = 0; i < priv->num_channels; i++)
		dpaa2_io_store_destroy(priv->channel[i]->store);
}

static int set_mac_addr(struct dpaa2_eth_priv *priv)
{
	struct net_device *net_dev = priv->net_dev;
	struct device *dev = net_dev->dev.parent;
	u8 mac_addr[ETH_ALEN], dpni_mac_addr[ETH_ALEN];
	int err;

	/* Get firmware address, if any */
	err = dpni_get_port_mac_addr(priv->mc_io, 0, priv->mc_token, mac_addr);
	if (err) {
		dev_err(dev, "dpni_get_port_mac_addr() failed\n");
		return err;
	}

	/* Get DPNI attributes address, if any */
	err = dpni_get_primary_mac_addr(priv->mc_io, 0, priv->mc_token,
					dpni_mac_addr);
	if (err) {
		dev_err(dev, "dpni_get_primary_mac_addr() failed\n");
		return err;
	}

	/* First check if firmware has any address configured by bootloader */
	if (!is_zero_ether_addr(mac_addr)) {
		/* If the DPMAC addr != DPNI addr, update it */
		if (!ether_addr_equal(mac_addr, dpni_mac_addr)) {
			err = dpni_set_primary_mac_addr(priv->mc_io, 0,
							priv->mc_token,
							mac_addr);
			if (err) {
				dev_err(dev, "dpni_set_primary_mac_addr() failed\n");
				return err;
			}
		}
		memcpy(net_dev->dev_addr, mac_addr, net_dev->addr_len);
	} else if (is_zero_ether_addr(dpni_mac_addr)) {
		/* No MAC address configured, fill in net_dev->dev_addr
		 * with a random one
		 */
		eth_hw_addr_random(net_dev);
		dev_dbg_once(dev, "device(s) have all-zero hwaddr, replaced with random\n");

		err = dpni_set_primary_mac_addr(priv->mc_io, 0, priv->mc_token,
						net_dev->dev_addr);
		if (err) {
			dev_err(dev, "dpni_set_primary_mac_addr() failed\n");
			return err;
		}

		/* Override NET_ADDR_RANDOM set by eth_hw_addr_random(); for all
		 * practical purposes, this will be our "permanent" mac address,
		 * at least until the next reboot. This move will also permit
		 * register_netdevice() to properly fill up net_dev->perm_addr.
		 */
		net_dev->addr_assign_type = NET_ADDR_PERM;
	} else {
		/* NET_ADDR_PERM is default, all we have to do is
		 * fill in the device addr.
		 */
		memcpy(net_dev->dev_addr, dpni_mac_addr, net_dev->addr_len);
	}

	return 0;
}

static int netdev_init(struct net_device *net_dev)
{
	struct device *dev = net_dev->dev.parent;
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);
	u32 options = priv->dpni_attrs.options;
	u64 supported = 0, not_supported = 0;
	u8 bcast_addr[ETH_ALEN];
	u8 num_queues;
	int err;

	net_dev->netdev_ops = &dpaa2_eth_ops;
	net_dev->ethtool_ops = &dpaa2_ethtool_ops;

	err = set_mac_addr(priv);
	if (err)
		return err;

	/* Explicitly add the broadcast address to the MAC filtering table */
	eth_broadcast_addr(bcast_addr);
	err = dpni_add_mac_addr(priv->mc_io, 0, priv->mc_token, bcast_addr);
	if (err) {
		dev_err(dev, "dpni_add_mac_addr() failed\n");
		return err;
	}

	/* Set MTU upper limit; lower limit is 68B (default value) */
	net_dev->max_mtu = DPAA2_ETH_MAX_MTU;
	err = dpni_set_max_frame_length(priv->mc_io, 0, priv->mc_token,
					DPAA2_ETH_MFL);
	if (err) {
		dev_err(dev, "dpni_set_max_frame_length() failed\n");
		return err;
	}

	/* Set actual number of queues in the net device */
	num_queues = dpaa2_eth_queue_count(priv);
	err = netif_set_real_num_tx_queues(net_dev, num_queues);
	if (err) {
		dev_err(dev, "netif_set_real_num_tx_queues() failed\n");
		return err;
	}
	err = netif_set_real_num_rx_queues(net_dev, num_queues);
	if (err) {
		dev_err(dev, "netif_set_real_num_rx_queues() failed\n");
		return err;
	}

	/* Capabilities listing */
	supported |= IFF_LIVE_ADDR_CHANGE;

	if (options & DPNI_OPT_NO_MAC_FILTER)
		not_supported |= IFF_UNICAST_FLT;
	else
		supported |= IFF_UNICAST_FLT;

	net_dev->priv_flags |= supported;
	net_dev->priv_flags &= ~not_supported;

	/* Features */
	net_dev->features = NETIF_F_RXCSUM |
			    NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
			    NETIF_F_SG | NETIF_F_HIGHDMA |
			    NETIF_F_LLTX;
	net_dev->hw_features = net_dev->features;

	return 0;
}

static int poll_link_state(void *arg)
{
	struct dpaa2_eth_priv *priv = (struct dpaa2_eth_priv *)arg;
	int err;

	while (!kthread_should_stop()) {
		err = link_state_update(priv);
		if (unlikely(err))
			return err;

		msleep(DPAA2_ETH_LINK_STATE_REFRESH);
	}

	return 0;
}

static irqreturn_t dpni_irq0_handler_thread(int irq_num, void *arg)
{
	u32 status = ~0;
	struct device *dev = (struct device *)arg;
	struct fsl_mc_device *dpni_dev = to_fsl_mc_device(dev);
	struct net_device *net_dev = dev_get_drvdata(dev);
	int err;

	err = dpni_get_irq_status(dpni_dev->mc_io, 0, dpni_dev->mc_handle,
				  DPNI_IRQ_INDEX, &status);
	if (unlikely(err)) {
		netdev_err(net_dev, "Can't get irq status (err %d)\n", err);
		return IRQ_HANDLED;
	}

	if (status & DPNI_IRQ_EVENT_LINK_CHANGED)
		link_state_update(netdev_priv(net_dev));

	return IRQ_HANDLED;
}

static int setup_irqs(struct fsl_mc_device *ls_dev)
{
	int err = 0;
	struct fsl_mc_device_irq *irq;

	err = fsl_mc_allocate_irqs(ls_dev);
	if (err) {
		dev_err(&ls_dev->dev, "MC irqs allocation failed\n");
		return err;
	}

	irq = ls_dev->irqs[0];
	err = devm_request_threaded_irq(&ls_dev->dev, irq->msi_desc->irq,
					NULL, dpni_irq0_handler_thread,
					IRQF_NO_SUSPEND | IRQF_ONESHOT,
					dev_name(&ls_dev->dev), &ls_dev->dev);
	if (err < 0) {
		dev_err(&ls_dev->dev, "devm_request_threaded_irq(): %d\n", err);
		goto free_mc_irq;
	}

	err = dpni_set_irq_mask(ls_dev->mc_io, 0, ls_dev->mc_handle,
				DPNI_IRQ_INDEX, DPNI_IRQ_EVENT_LINK_CHANGED);
	if (err < 0) {
		dev_err(&ls_dev->dev, "dpni_set_irq_mask(): %d\n", err);
		goto free_irq;
	}

	err = dpni_set_irq_enable(ls_dev->mc_io, 0, ls_dev->mc_handle,
				  DPNI_IRQ_INDEX, 1);
	if (err < 0) {
		dev_err(&ls_dev->dev, "dpni_set_irq_enable(): %d\n", err);
		goto free_irq;
	}

	return 0;

free_irq:
	devm_free_irq(&ls_dev->dev, irq->msi_desc->irq, &ls_dev->dev);
free_mc_irq:
	fsl_mc_free_irqs(ls_dev);

	return err;
}

static void add_ch_napi(struct dpaa2_eth_priv *priv)
{
	int i;
	struct dpaa2_eth_channel *ch;

	for (i = 0; i < priv->num_channels; i++) {
		ch = priv->channel[i];
		/* NAPI weight *MUST* be a multiple of DPAA2_ETH_STORE_SIZE */
		netif_napi_add(priv->net_dev, &ch->napi, dpaa2_eth_poll,
			       NAPI_POLL_WEIGHT);
	}
}

static void del_ch_napi(struct dpaa2_eth_priv *priv)
{
	int i;
	struct dpaa2_eth_channel *ch;

	for (i = 0; i < priv->num_channels; i++) {
		ch = priv->channel[i];
		netif_napi_del(&ch->napi);
	}
}

static int dpaa2_eth_probe(struct fsl_mc_device *dpni_dev)
{
	struct device *dev;
	struct net_device *net_dev = NULL;
	struct dpaa2_eth_priv *priv = NULL;
	int err = 0;

	dev = &dpni_dev->dev;

	/* Net device */
	net_dev = alloc_etherdev_mq(sizeof(*priv), DPAA2_ETH_MAX_TX_QUEUES);
	if (!net_dev) {
		dev_err(dev, "alloc_etherdev_mq() failed\n");
		return -ENOMEM;
	}

	SET_NETDEV_DEV(net_dev, dev);
	dev_set_drvdata(dev, net_dev);

	priv = netdev_priv(net_dev);
	priv->net_dev = net_dev;

	priv->iommu_domain = iommu_get_domain_for_dev(dev);

	/* Obtain a MC portal */
	err = fsl_mc_portal_allocate(dpni_dev, FSL_MC_IO_ATOMIC_CONTEXT_PORTAL,
				     &priv->mc_io);
	if (err) {
		if (err == -ENXIO)
			err = -EPROBE_DEFER;
		else
			dev_err(dev, "MC portal allocation failed\n");
		goto err_portal_alloc;
	}

	/* MC objects initialization and configuration */
	err = setup_dpni(dpni_dev);
	if (err)
		goto err_dpni_setup;

	err = setup_dpio(priv);
	if (err)
		goto err_dpio_setup;

	setup_fqs(priv);

	err = setup_dpbp(priv);
	if (err)
		goto err_dpbp_setup;

	err = bind_dpni(priv);
	if (err)
		goto err_bind;

	/* Add a NAPI context for each channel */
	add_ch_napi(priv);

	/* Percpu statistics */
	priv->percpu_stats = alloc_percpu(*priv->percpu_stats);
	if (!priv->percpu_stats) {
		dev_err(dev, "alloc_percpu(percpu_stats) failed\n");
		err = -ENOMEM;
		goto err_alloc_percpu_stats;
	}
	priv->percpu_extras = alloc_percpu(*priv->percpu_extras);
	if (!priv->percpu_extras) {
		dev_err(dev, "alloc_percpu(percpu_extras) failed\n");
		err = -ENOMEM;
		goto err_alloc_percpu_extras;
	}

	err = netdev_init(net_dev);
	if (err)
		goto err_netdev_init;

	/* Configure checksum offload based on current interface flags */
	err = set_rx_csum(priv, !!(net_dev->features & NETIF_F_RXCSUM));
	if (err)
		goto err_csum;

	err = set_tx_csum(priv, !!(net_dev->features &
				   (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM)));
	if (err)
		goto err_csum;

	err = alloc_rings(priv);
	if (err)
		goto err_alloc_rings;

	err = setup_irqs(dpni_dev);
	if (err) {
		netdev_warn(net_dev, "Failed to set link interrupt, fall back to polling\n");
		priv->poll_thread = kthread_run(poll_link_state, priv,
						"%s_poll_link", net_dev->name);
		if (IS_ERR(priv->poll_thread)) {
			dev_err(dev, "Error starting polling thread\n");
			goto err_poll_thread;
		}
		priv->do_link_poll = true;
	}

	err = register_netdev(net_dev);
	if (err < 0) {
		dev_err(dev, "register_netdev() failed\n");
		goto err_netdev_reg;
	}

#ifdef CONFIG_DEBUG_FS
	dpaa2_dbg_add(priv);
#endif

	dev_info(dev, "Probed interface %s\n", net_dev->name);
	return 0;

err_netdev_reg:
	if (priv->do_link_poll)
		kthread_stop(priv->poll_thread);
	else
		fsl_mc_free_irqs(dpni_dev);
err_poll_thread:
	free_rings(priv);
err_alloc_rings:
err_csum:
err_netdev_init:
	free_percpu(priv->percpu_extras);
err_alloc_percpu_extras:
	free_percpu(priv->percpu_stats);
err_alloc_percpu_stats:
	del_ch_napi(priv);
err_bind:
	free_dpbp(priv);
err_dpbp_setup:
	free_dpio(priv);
err_dpio_setup:
	free_dpni(priv);
err_dpni_setup:
	fsl_mc_portal_free(priv->mc_io);
err_portal_alloc:
	dev_set_drvdata(dev, NULL);
	free_netdev(net_dev);

	return err;
}

static int dpaa2_eth_remove(struct fsl_mc_device *ls_dev)
{
	struct device *dev;
	struct net_device *net_dev;
	struct dpaa2_eth_priv *priv;

	dev = &ls_dev->dev;
	net_dev = dev_get_drvdata(dev);
	priv = netdev_priv(net_dev);

#ifdef CONFIG_DEBUG_FS
	dpaa2_dbg_remove(priv);
#endif
	unregister_netdev(net_dev);

	if (priv->do_link_poll)
		kthread_stop(priv->poll_thread);
	else
		fsl_mc_free_irqs(ls_dev);

	free_rings(priv);
	free_percpu(priv->percpu_stats);
	free_percpu(priv->percpu_extras);

	del_ch_napi(priv);
	free_dpbp(priv);
	free_dpio(priv);
	free_dpni(priv);

	fsl_mc_portal_free(priv->mc_io);

	free_netdev(net_dev);

	dev_dbg(net_dev->dev.parent, "Removed interface %s\n", net_dev->name);

	return 0;
}

static const struct fsl_mc_device_id dpaa2_eth_match_id_table[] = {
	{
		.vendor = FSL_MC_VENDOR_FREESCALE,
		.obj_type = "dpni",
	},
	{ .vendor = 0x0 }
};
MODULE_DEVICE_TABLE(fslmc, dpaa2_eth_match_id_table);

static struct fsl_mc_driver dpaa2_eth_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
	},
	.probe = dpaa2_eth_probe,
	.remove = dpaa2_eth_remove,
	.match_id_table = dpaa2_eth_match_id_table
};

static int __init dpaa2_eth_driver_init(void)
{
	int err;

	dpaa2_eth_dbg_init();
	err = fsl_mc_driver_register(&dpaa2_eth_driver);
	if (err) {
		dpaa2_eth_dbg_exit();
		return err;
	}

	return 0;
}

static void __exit dpaa2_eth_driver_exit(void)
{
	dpaa2_eth_dbg_exit();
	fsl_mc_driver_unregister(&dpaa2_eth_driver);
}

module_init(dpaa2_eth_driver_init);
module_exit(dpaa2_eth_driver_exit);
