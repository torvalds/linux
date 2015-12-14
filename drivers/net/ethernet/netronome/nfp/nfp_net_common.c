/*
 * Copyright (C) 2015 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
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

/*
 * nfp_net_common.c
 * Netronome network device driver: Common functions between PF and VF
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 *          Brad Petrus <brad.petrus@netronome.com>
 *          Chris Telfer <chris.telfer@netronome.com>
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/msi.h>
#include <linux/ethtool.h>
#include <linux/log2.h>
#include <linux/if_vlan.h>
#include <linux/random.h>

#include <linux/ktime.h>

#include <net/vxlan.h>

#include "nfp_net_ctrl.h"
#include "nfp_net.h"

/**
 * nfp_net_get_fw_version() - Read and parse the FW version
 * @fw_ver:	Output fw_version structure to read to
 * @ctrl_bar:	Mapped address of the control BAR
 */
void nfp_net_get_fw_version(struct nfp_net_fw_version *fw_ver,
			    void __iomem *ctrl_bar)
{
	u32 reg;

	reg = readl(ctrl_bar + NFP_NET_CFG_VERSION);
	put_unaligned_le32(reg, fw_ver);
}

/**
 * nfp_net_reconfig() - Reconfigure the firmware
 * @nn:      NFP Net device to reconfigure
 * @update:  The value for the update field in the BAR config
 *
 * Write the update word to the BAR and ping the reconfig queue.  The
 * poll until the firmware has acknowledged the update by zeroing the
 * update word.
 *
 * Return: Negative errno on error, 0 on success
 */
int nfp_net_reconfig(struct nfp_net *nn, u32 update)
{
	int cnt, ret = 0;
	u32 new;

	spin_lock_bh(&nn->reconfig_lock);

	nn_writel(nn, NFP_NET_CFG_UPDATE, update);
	/* ensure update is written before pinging HW */
	nn_pci_flush(nn);
	nfp_qcp_wr_ptr_add(nn->qcp_cfg, 1);

	/* Poll update field, waiting for NFP to ack the config */
	for (cnt = 0; ; cnt++) {
		new = nn_readl(nn, NFP_NET_CFG_UPDATE);
		if (new == 0)
			break;
		if (new & NFP_NET_CFG_UPDATE_ERR) {
			nn_err(nn, "Reconfig error: 0x%08x\n", new);
			ret = -EIO;
			break;
		} else if (cnt >= NFP_NET_POLL_TIMEOUT) {
			nn_err(nn, "Reconfig timeout for 0x%08x after %dms\n",
			       update, cnt);
			ret = -EIO;
			break;
		}
		mdelay(1);
	}

	spin_unlock_bh(&nn->reconfig_lock);
	return ret;
}

/* Interrupt configuration and handling
 */

/**
 * nfp_net_irq_unmask_msix() - Unmask MSI-X after automasking
 * @nn:       NFP Network structure
 * @entry_nr: MSI-X table entry
 *
 * Clear the MSI-X table mask bit for the given entry bypassing Linux irq
 * handling subsystem.  Use *only* to reenable automasked vectors.
 */
static void nfp_net_irq_unmask_msix(struct nfp_net *nn, unsigned int entry_nr)
{
	struct list_head *msi_head = &nn->pdev->dev.msi_list;
	struct msi_desc *entry;
	u32 off;

	/* All MSI-Xs have the same mask_base */
	entry = list_first_entry(msi_head, struct msi_desc, list);

	off = (PCI_MSIX_ENTRY_SIZE * entry_nr) +
		PCI_MSIX_ENTRY_VECTOR_CTRL;
	writel(0, entry->mask_base + off);
	readl(entry->mask_base);
}

/**
 * nfp_net_irq_unmask() - Unmask automasked interrupt
 * @nn:       NFP Network structure
 * @entry_nr: MSI-X table entry
 *
 * If MSI-X auto-masking is enabled clear the mask bit, otherwise
 * clear the ICR for the entry.
 */
static void nfp_net_irq_unmask(struct nfp_net *nn, unsigned int entry_nr)
{
	if (nn->ctrl & NFP_NET_CFG_CTRL_MSIXAUTO) {
		nfp_net_irq_unmask_msix(nn, entry_nr);
		return;
	}

	nn_writeb(nn, NFP_NET_CFG_ICR(entry_nr), NFP_NET_CFG_ICR_UNMASKED);
	nn_pci_flush(nn);
}

/**
 * nfp_net_msix_alloc() - Try to allocate MSI-X irqs
 * @nn:       NFP Network structure
 * @nr_vecs:  Number of MSI-X vectors to allocate
 *
 * For MSI-X we want at least NFP_NET_NON_Q_VECTORS + 1 vectors.
 *
 * Return: Number of MSI-X vectors obtained or 0 on error.
 */
static int nfp_net_msix_alloc(struct nfp_net *nn, int nr_vecs)
{
	struct pci_dev *pdev = nn->pdev;
	int nvecs;
	int i;

	for (i = 0; i < nr_vecs; i++)
		nn->irq_entries[i].entry = i;

	nvecs = pci_enable_msix_range(pdev, nn->irq_entries,
				      NFP_NET_NON_Q_VECTORS + 1, nr_vecs);
	if (nvecs < 0) {
		nn_warn(nn, "Failed to enable MSI-X. Wanted %d-%d (err=%d)\n",
			NFP_NET_NON_Q_VECTORS + 1, nr_vecs, nvecs);
		return 0;
	}

	return nvecs;
}

/**
 * nfp_net_irqs_wanted() - Work out how many interrupt vectors we want
 * @nn:       NFP Network structure
 *
 * We want a vector per CPU (or ring), whatever is smaller plus
 * NFP_NET_NON_Q_VECTORS for LSC etc.
 *
 * Return: Number of interrupts wanted
 */
static int nfp_net_irqs_wanted(struct nfp_net *nn)
{
	int ncpus;
	int vecs;

	ncpus = num_online_cpus();

	vecs = max_t(int, nn->num_tx_rings, nn->num_rx_rings);
	vecs = min_t(int, vecs, ncpus);

	return vecs + NFP_NET_NON_Q_VECTORS;
}

/**
 * nfp_net_irqs_alloc() - allocates MSI-X irqs
 * @nn:       NFP Network structure
 *
 * Return: Number of irqs obtained or 0 on error.
 */
int nfp_net_irqs_alloc(struct nfp_net *nn)
{
	int wanted_irqs;

	wanted_irqs = nfp_net_irqs_wanted(nn);

	nn->num_irqs = nfp_net_msix_alloc(nn, wanted_irqs);
	if (nn->num_irqs == 0) {
		nn_err(nn, "Failed to allocate MSI-X IRQs\n");
		return 0;
	}

	nn->num_r_vecs = nn->num_irqs - NFP_NET_NON_Q_VECTORS;

	if (nn->num_irqs < wanted_irqs)
		nn_warn(nn, "Unable to allocate %d vectors. Got %d instead\n",
			wanted_irqs, nn->num_irqs);

	return nn->num_irqs;
}

/**
 * nfp_net_irqs_disable() - Disable interrupts
 * @nn:       NFP Network structure
 *
 * Undoes what @nfp_net_irqs_alloc() does.
 */
void nfp_net_irqs_disable(struct nfp_net *nn)
{
	pci_disable_msix(nn->pdev);
}

/**
 * nfp_net_irq_rxtx() - Interrupt service routine for RX/TX rings.
 * @irq:      Interrupt
 * @data:     Opaque data structure
 *
 * Return: Indicate if the interrupt has been handled.
 */
static irqreturn_t nfp_net_irq_rxtx(int irq, void *data)
{
	struct nfp_net_r_vector *r_vec = data;

	napi_schedule_irqoff(&r_vec->napi);

	/* The FW auto-masks any interrupt, either via the MASK bit in
	 * the MSI-X table or via the per entry ICR field.  So there
	 * is no need to disable interrupts here.
	 */
	return IRQ_HANDLED;
}

/**
 * nfp_net_read_link_status() - Reread link status from control BAR
 * @nn:       NFP Network structure
 */
static void nfp_net_read_link_status(struct nfp_net *nn)
{
	unsigned long flags;
	bool link_up;
	u32 sts;

	spin_lock_irqsave(&nn->link_status_lock, flags);

	sts = nn_readl(nn, NFP_NET_CFG_STS);
	link_up = !!(sts & NFP_NET_CFG_STS_LINK);

	if (nn->link_up == link_up)
		goto out;

	nn->link_up = link_up;

	if (nn->link_up) {
		netif_carrier_on(nn->netdev);
		netdev_info(nn->netdev, "NIC Link is Up\n");
	} else {
		netif_carrier_off(nn->netdev);
		netdev_info(nn->netdev, "NIC Link is Down\n");
	}
out:
	spin_unlock_irqrestore(&nn->link_status_lock, flags);
}

/**
 * nfp_net_irq_lsc() - Interrupt service routine for link state changes
 * @irq:      Interrupt
 * @data:     Opaque data structure
 *
 * Return: Indicate if the interrupt has been handled.
 */
static irqreturn_t nfp_net_irq_lsc(int irq, void *data)
{
	struct nfp_net *nn = data;

	nfp_net_read_link_status(nn);

	nfp_net_irq_unmask(nn, NFP_NET_IRQ_LSC_IDX);

	return IRQ_HANDLED;
}

/**
 * nfp_net_irq_exn() - Interrupt service routine for exceptions
 * @irq:      Interrupt
 * @data:     Opaque data structure
 *
 * Return: Indicate if the interrupt has been handled.
 */
static irqreturn_t nfp_net_irq_exn(int irq, void *data)
{
	struct nfp_net *nn = data;

	nn_err(nn, "%s: UNIMPLEMENTED.\n", __func__);
	/* XXX TO BE IMPLEMENTED */
	return IRQ_HANDLED;
}

/**
 * nfp_net_tx_ring_init() - Fill in the boilerplate for a TX ring
 * @tx_ring:  TX ring structure
 */
static void nfp_net_tx_ring_init(struct nfp_net_tx_ring *tx_ring)
{
	struct nfp_net_r_vector *r_vec = tx_ring->r_vec;
	struct nfp_net *nn = r_vec->nfp_net;

	tx_ring->qcidx = tx_ring->idx * nn->stride_tx;
	tx_ring->qcp_q = nn->tx_bar + NFP_QCP_QUEUE_OFF(tx_ring->qcidx);
}

/**
 * nfp_net_rx_ring_init() - Fill in the boilerplate for a RX ring
 * @rx_ring:  RX ring structure
 */
static void nfp_net_rx_ring_init(struct nfp_net_rx_ring *rx_ring)
{
	struct nfp_net_r_vector *r_vec = rx_ring->r_vec;
	struct nfp_net *nn = r_vec->nfp_net;

	rx_ring->fl_qcidx = rx_ring->idx * nn->stride_rx;
	rx_ring->rx_qcidx = rx_ring->fl_qcidx + (nn->stride_rx - 1);

	rx_ring->qcp_fl = nn->rx_bar + NFP_QCP_QUEUE_OFF(rx_ring->fl_qcidx);
	rx_ring->qcp_rx = nn->rx_bar + NFP_QCP_QUEUE_OFF(rx_ring->rx_qcidx);
}

/**
 * nfp_net_irqs_assign() - Assign IRQs and setup rvecs.
 * @netdev:   netdev structure
 */
static void nfp_net_irqs_assign(struct net_device *netdev)
{
	struct nfp_net *nn = netdev_priv(netdev);
	struct nfp_net_r_vector *r_vec;
	int r;

	/* Assumes nn->num_tx_rings == nn->num_rx_rings */
	if (nn->num_tx_rings > nn->num_r_vecs) {
		nn_warn(nn, "More rings (%d) than vectors (%d).\n",
			nn->num_tx_rings, nn->num_r_vecs);
		nn->num_tx_rings = nn->num_r_vecs;
		nn->num_rx_rings = nn->num_r_vecs;
	}

	nn->lsc_handler = nfp_net_irq_lsc;
	nn->exn_handler = nfp_net_irq_exn;

	for (r = 0; r < nn->num_r_vecs; r++) {
		r_vec = &nn->r_vecs[r];
		r_vec->nfp_net = nn;
		r_vec->handler = nfp_net_irq_rxtx;
		r_vec->irq_idx = NFP_NET_NON_Q_VECTORS + r;

		cpumask_set_cpu(r, &r_vec->affinity_mask);

		r_vec->tx_ring = &nn->tx_rings[r];
		nn->tx_rings[r].idx = r;
		nn->tx_rings[r].r_vec = r_vec;
		nfp_net_tx_ring_init(r_vec->tx_ring);

		r_vec->rx_ring = &nn->rx_rings[r];
		nn->rx_rings[r].idx = r;
		nn->rx_rings[r].r_vec = r_vec;
		nfp_net_rx_ring_init(r_vec->rx_ring);
	}
}

/**
 * nfp_net_aux_irq_request() - Request an auxiliary interrupt (LSC or EXN)
 * @nn:		NFP Network structure
 * @ctrl_offset: Control BAR offset where IRQ configuration should be written
 * @format:	printf-style format to construct the interrupt name
 * @name:	Pointer to allocated space for interrupt name
 * @name_sz:	Size of space for interrupt name
 * @vector_idx:	Index of MSI-X vector used for this interrupt
 * @handler:	IRQ handler to register for this interrupt
 */
static int
nfp_net_aux_irq_request(struct nfp_net *nn, u32 ctrl_offset,
			const char *format, char *name, size_t name_sz,
			unsigned int vector_idx, irq_handler_t handler)
{
	struct msix_entry *entry;
	int err;

	entry = &nn->irq_entries[vector_idx];

	snprintf(name, name_sz, format, netdev_name(nn->netdev));
	err = request_irq(entry->vector, handler, 0, name, nn);
	if (err) {
		nn_err(nn, "Failed to request IRQ %d (err=%d).\n",
		       entry->vector, err);
		return err;
	}
	nn_writeb(nn, ctrl_offset, vector_idx);

	return 0;
}

/**
 * nfp_net_aux_irq_free() - Free an auxiliary interrupt (LSC or EXN)
 * @nn:		NFP Network structure
 * @ctrl_offset: Control BAR offset where IRQ configuration should be written
 * @vector_idx:	Index of MSI-X vector used for this interrupt
 */
static void nfp_net_aux_irq_free(struct nfp_net *nn, u32 ctrl_offset,
				 unsigned int vector_idx)
{
	nn_writeb(nn, ctrl_offset, 0xff);
	free_irq(nn->irq_entries[vector_idx].vector, nn);
}

/* Transmit
 *
 * One queue controller peripheral queue is used for transmit.  The
 * driver en-queues packets for transmit by advancing the write
 * pointer.  The device indicates that packets have transmitted by
 * advancing the read pointer.  The driver maintains a local copy of
 * the read and write pointer in @struct nfp_net_tx_ring.  The driver
 * keeps @wr_p in sync with the queue controller write pointer and can
 * determine how many packets have been transmitted by comparing its
 * copy of the read pointer @rd_p with the read pointer maintained by
 * the queue controller peripheral.
 */

/**
 * nfp_net_tx_full() - Check if the TX ring is full
 * @tx_ring: TX ring to check
 * @dcnt:    Number of descriptors that need to be enqueued (must be >= 1)
 *
 * This function checks, based on the *host copy* of read/write
 * pointer if a given TX ring is full.  The real TX queue may have
 * some newly made available slots.
 *
 * Return: True if the ring is full.
 */
static inline int nfp_net_tx_full(struct nfp_net_tx_ring *tx_ring, int dcnt)
{
	return (tx_ring->wr_p - tx_ring->rd_p) >= (tx_ring->cnt - dcnt);
}

/* Wrappers for deciding when to stop and restart TX queues */
static int nfp_net_tx_ring_should_wake(struct nfp_net_tx_ring *tx_ring)
{
	return !nfp_net_tx_full(tx_ring, MAX_SKB_FRAGS * 4);
}

static int nfp_net_tx_ring_should_stop(struct nfp_net_tx_ring *tx_ring)
{
	return nfp_net_tx_full(tx_ring, MAX_SKB_FRAGS + 1);
}

/**
 * nfp_net_tx_ring_stop() - stop tx ring
 * @nd_q:    netdev queue
 * @tx_ring: driver tx queue structure
 *
 * Safely stop TX ring.  Remember that while we are running .start_xmit()
 * someone else may be cleaning the TX ring completions so we need to be
 * extra careful here.
 */
static void nfp_net_tx_ring_stop(struct netdev_queue *nd_q,
				 struct nfp_net_tx_ring *tx_ring)
{
	netif_tx_stop_queue(nd_q);

	/* We can race with the TX completion out of NAPI so recheck */
	smp_mb();
	if (unlikely(nfp_net_tx_ring_should_wake(tx_ring)))
		netif_tx_start_queue(nd_q);
}

/**
 * nfp_net_tx_tso() - Set up Tx descriptor for LSO
 * @nn:  NFP Net device
 * @r_vec: per-ring structure
 * @txbuf: Pointer to driver soft TX descriptor
 * @txd: Pointer to HW TX descriptor
 * @skb: Pointer to SKB
 *
 * Set up Tx descriptor for LSO, do nothing for non-LSO skbs.
 * Return error on packet header greater than maximum supported LSO header size.
 */
static void nfp_net_tx_tso(struct nfp_net *nn, struct nfp_net_r_vector *r_vec,
			   struct nfp_net_tx_buf *txbuf,
			   struct nfp_net_tx_desc *txd, struct sk_buff *skb)
{
	u32 hdrlen;
	u16 mss;

	if (!skb_is_gso(skb))
		return;

	if (!skb->encapsulation)
		hdrlen = skb_transport_offset(skb) + tcp_hdrlen(skb);
	else
		hdrlen = skb_inner_transport_header(skb) - skb->data +
			inner_tcp_hdrlen(skb);

	txbuf->pkt_cnt = skb_shinfo(skb)->gso_segs;
	txbuf->real_len += hdrlen * (txbuf->pkt_cnt - 1);

	mss = skb_shinfo(skb)->gso_size & PCIE_DESC_TX_MSS_MASK;
	txd->l4_offset = hdrlen;
	txd->mss = cpu_to_le16(mss);
	txd->flags |= PCIE_DESC_TX_LSO;

	u64_stats_update_begin(&r_vec->tx_sync);
	r_vec->tx_lso++;
	u64_stats_update_end(&r_vec->tx_sync);
}

/**
 * nfp_net_tx_csum() - Set TX CSUM offload flags in TX descriptor
 * @nn:  NFP Net device
 * @r_vec: per-ring structure
 * @txbuf: Pointer to driver soft TX descriptor
 * @txd: Pointer to TX descriptor
 * @skb: Pointer to SKB
 *
 * This function sets the TX checksum flags in the TX descriptor based
 * on the configuration and the protocol of the packet to be transmitted.
 */
static void nfp_net_tx_csum(struct nfp_net *nn, struct nfp_net_r_vector *r_vec,
			    struct nfp_net_tx_buf *txbuf,
			    struct nfp_net_tx_desc *txd, struct sk_buff *skb)
{
	struct ipv6hdr *ipv6h;
	struct iphdr *iph;
	u8 l4_hdr;

	if (!(nn->ctrl & NFP_NET_CFG_CTRL_TXCSUM))
		return;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return;

	txd->flags |= PCIE_DESC_TX_CSUM;
	if (skb->encapsulation)
		txd->flags |= PCIE_DESC_TX_ENCAP;

	iph = skb->encapsulation ? inner_ip_hdr(skb) : ip_hdr(skb);
	ipv6h = skb->encapsulation ? inner_ipv6_hdr(skb) : ipv6_hdr(skb);

	if (iph->version == 4) {
		txd->flags |= PCIE_DESC_TX_IP4_CSUM;
		l4_hdr = iph->protocol;
	} else if (ipv6h->version == 6) {
		l4_hdr = ipv6h->nexthdr;
	} else {
		nn_warn_ratelimit(nn, "partial checksum but ipv=%x!\n",
				  iph->version);
		return;
	}

	switch (l4_hdr) {
	case IPPROTO_TCP:
		txd->flags |= PCIE_DESC_TX_TCP_CSUM;
		break;
	case IPPROTO_UDP:
		txd->flags |= PCIE_DESC_TX_UDP_CSUM;
		break;
	default:
		nn_warn_ratelimit(nn, "partial checksum but l4 proto=%x!\n",
				  l4_hdr);
		return;
	}

	u64_stats_update_begin(&r_vec->tx_sync);
	if (skb->encapsulation)
		r_vec->hw_csum_tx_inner += txbuf->pkt_cnt;
	else
		r_vec->hw_csum_tx += txbuf->pkt_cnt;
	u64_stats_update_end(&r_vec->tx_sync);
}

/**
 * nfp_net_tx() - Main transmit entry point
 * @skb:    SKB to transmit
 * @netdev: netdev structure
 *
 * Return: NETDEV_TX_OK on success.
 */
static int nfp_net_tx(struct sk_buff *skb, struct net_device *netdev)
{
	struct nfp_net *nn = netdev_priv(netdev);
	const struct skb_frag_struct *frag;
	struct nfp_net_r_vector *r_vec;
	struct nfp_net_tx_desc *txd, txdg;
	struct nfp_net_tx_buf *txbuf;
	struct nfp_net_tx_ring *tx_ring;
	struct netdev_queue *nd_q;
	dma_addr_t dma_addr;
	unsigned int fsize;
	int f, nr_frags;
	int wr_idx;
	u16 qidx;

	qidx = skb_get_queue_mapping(skb);
	tx_ring = &nn->tx_rings[qidx];
	r_vec = tx_ring->r_vec;
	nd_q = netdev_get_tx_queue(nn->netdev, qidx);

	nr_frags = skb_shinfo(skb)->nr_frags;

	if (unlikely(nfp_net_tx_full(tx_ring, nr_frags + 1))) {
		nn_warn_ratelimit(nn, "TX ring %d busy. wrp=%u rdp=%u\n",
				  qidx, tx_ring->wr_p, tx_ring->rd_p);
		netif_tx_stop_queue(nd_q);
		u64_stats_update_begin(&r_vec->tx_sync);
		r_vec->tx_busy++;
		u64_stats_update_end(&r_vec->tx_sync);
		return NETDEV_TX_BUSY;
	}

	/* Start with the head skbuf */
	dma_addr = dma_map_single(&nn->pdev->dev, skb->data, skb_headlen(skb),
				  DMA_TO_DEVICE);
	if (dma_mapping_error(&nn->pdev->dev, dma_addr))
		goto err_free;

	wr_idx = tx_ring->wr_p % tx_ring->cnt;

	/* Stash the soft descriptor of the head then initialize it */
	txbuf = &tx_ring->txbufs[wr_idx];
	txbuf->skb = skb;
	txbuf->dma_addr = dma_addr;
	txbuf->fidx = -1;
	txbuf->pkt_cnt = 1;
	txbuf->real_len = skb->len;

	/* Build TX descriptor */
	txd = &tx_ring->txds[wr_idx];
	txd->offset_eop = (nr_frags == 0) ? PCIE_DESC_TX_EOP : 0;
	txd->dma_len = cpu_to_le16(skb_headlen(skb));
	nfp_desc_set_dma_addr(txd, dma_addr);
	txd->data_len = cpu_to_le16(skb->len);

	txd->flags = 0;
	txd->mss = 0;
	txd->l4_offset = 0;

	nfp_net_tx_tso(nn, r_vec, txbuf, txd, skb);

	nfp_net_tx_csum(nn, r_vec, txbuf, txd, skb);

	if (skb_vlan_tag_present(skb) && nn->ctrl & NFP_NET_CFG_CTRL_TXVLAN) {
		txd->flags |= PCIE_DESC_TX_VLAN;
		txd->vlan = cpu_to_le16(skb_vlan_tag_get(skb));
	}

	/* Gather DMA */
	if (nr_frags > 0) {
		/* all descs must match except for in addr, length and eop */
		txdg = *txd;

		for (f = 0; f < nr_frags; f++) {
			frag = &skb_shinfo(skb)->frags[f];
			fsize = skb_frag_size(frag);

			dma_addr = skb_frag_dma_map(&nn->pdev->dev, frag, 0,
						    fsize, DMA_TO_DEVICE);
			if (dma_mapping_error(&nn->pdev->dev, dma_addr))
				goto err_unmap;

			wr_idx = (wr_idx + 1) % tx_ring->cnt;
			tx_ring->txbufs[wr_idx].skb = skb;
			tx_ring->txbufs[wr_idx].dma_addr = dma_addr;
			tx_ring->txbufs[wr_idx].fidx = f;

			txd = &tx_ring->txds[wr_idx];
			*txd = txdg;
			txd->dma_len = cpu_to_le16(fsize);
			nfp_desc_set_dma_addr(txd, dma_addr);
			txd->offset_eop =
				(f == nr_frags - 1) ? PCIE_DESC_TX_EOP : 0;
		}

		u64_stats_update_begin(&r_vec->tx_sync);
		r_vec->tx_gather++;
		u64_stats_update_end(&r_vec->tx_sync);
	}

	netdev_tx_sent_queue(nd_q, txbuf->real_len);

	tx_ring->wr_p += nr_frags + 1;
	if (nfp_net_tx_ring_should_stop(tx_ring))
		nfp_net_tx_ring_stop(nd_q, tx_ring);

	tx_ring->wr_ptr_add += nr_frags + 1;
	if (!skb->xmit_more || netif_xmit_stopped(nd_q)) {
		/* force memory write before we let HW know */
		wmb();
		nfp_qcp_wr_ptr_add(tx_ring->qcp_q, tx_ring->wr_ptr_add);
		tx_ring->wr_ptr_add = 0;
	}

	skb_tx_timestamp(skb);

	return NETDEV_TX_OK;

err_unmap:
	--f;
	while (f >= 0) {
		frag = &skb_shinfo(skb)->frags[f];
		dma_unmap_page(&nn->pdev->dev,
			       tx_ring->txbufs[wr_idx].dma_addr,
			       skb_frag_size(frag), DMA_TO_DEVICE);
		tx_ring->txbufs[wr_idx].skb = NULL;
		tx_ring->txbufs[wr_idx].dma_addr = 0;
		tx_ring->txbufs[wr_idx].fidx = -2;
		wr_idx = wr_idx - 1;
		if (wr_idx < 0)
			wr_idx += tx_ring->cnt;
	}
	dma_unmap_single(&nn->pdev->dev, tx_ring->txbufs[wr_idx].dma_addr,
			 skb_headlen(skb), DMA_TO_DEVICE);
	tx_ring->txbufs[wr_idx].skb = NULL;
	tx_ring->txbufs[wr_idx].dma_addr = 0;
	tx_ring->txbufs[wr_idx].fidx = -2;
err_free:
	nn_warn_ratelimit(nn, "Failed to map DMA TX buffer\n");
	u64_stats_update_begin(&r_vec->tx_sync);
	r_vec->tx_errors++;
	u64_stats_update_end(&r_vec->tx_sync);
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

/**
 * nfp_net_tx_complete() - Handled completed TX packets
 * @tx_ring:   TX ring structure
 *
 * Return: Number of completed TX descriptors
 */
static void nfp_net_tx_complete(struct nfp_net_tx_ring *tx_ring)
{
	struct nfp_net_r_vector *r_vec = tx_ring->r_vec;
	struct nfp_net *nn = r_vec->nfp_net;
	const struct skb_frag_struct *frag;
	struct netdev_queue *nd_q;
	u32 done_pkts = 0, done_bytes = 0;
	struct sk_buff *skb;
	int todo, nr_frags;
	u32 qcp_rd_p;
	int fidx;
	int idx;

	/* Work out how many descriptors have been transmitted */
	qcp_rd_p = nfp_qcp_rd_ptr_read(tx_ring->qcp_q);

	if (qcp_rd_p == tx_ring->qcp_rd_p)
		return;

	if (qcp_rd_p > tx_ring->qcp_rd_p)
		todo = qcp_rd_p - tx_ring->qcp_rd_p;
	else
		todo = qcp_rd_p + tx_ring->cnt - tx_ring->qcp_rd_p;

	while (todo--) {
		idx = tx_ring->rd_p % tx_ring->cnt;
		tx_ring->rd_p++;

		skb = tx_ring->txbufs[idx].skb;
		if (!skb)
			continue;

		nr_frags = skb_shinfo(skb)->nr_frags;
		fidx = tx_ring->txbufs[idx].fidx;

		if (fidx == -1) {
			/* unmap head */
			dma_unmap_single(&nn->pdev->dev,
					 tx_ring->txbufs[idx].dma_addr,
					 skb_headlen(skb), DMA_TO_DEVICE);

			done_pkts += tx_ring->txbufs[idx].pkt_cnt;
			done_bytes += tx_ring->txbufs[idx].real_len;
		} else {
			/* unmap fragment */
			frag = &skb_shinfo(skb)->frags[fidx];
			dma_unmap_page(&nn->pdev->dev,
				       tx_ring->txbufs[idx].dma_addr,
				       skb_frag_size(frag), DMA_TO_DEVICE);
		}

		/* check for last gather fragment */
		if (fidx == nr_frags - 1)
			dev_kfree_skb_any(skb);

		tx_ring->txbufs[idx].dma_addr = 0;
		tx_ring->txbufs[idx].skb = NULL;
		tx_ring->txbufs[idx].fidx = -2;
	}

	tx_ring->qcp_rd_p = qcp_rd_p;

	u64_stats_update_begin(&r_vec->tx_sync);
	r_vec->tx_bytes += done_bytes;
	r_vec->tx_pkts += done_pkts;
	u64_stats_update_end(&r_vec->tx_sync);

	nd_q = netdev_get_tx_queue(nn->netdev, tx_ring->idx);
	netdev_tx_completed_queue(nd_q, done_pkts, done_bytes);
	if (nfp_net_tx_ring_should_wake(tx_ring)) {
		/* Make sure TX thread will see updated tx_ring->rd_p */
		smp_mb();

		if (unlikely(netif_tx_queue_stopped(nd_q)))
			netif_tx_wake_queue(nd_q);
	}

	WARN_ONCE(tx_ring->wr_p - tx_ring->rd_p > tx_ring->cnt,
		  "TX ring corruption rd_p=%u wr_p=%u cnt=%u\n",
		  tx_ring->rd_p, tx_ring->wr_p, tx_ring->cnt);
}

/**
 * nfp_net_tx_flush() - Free any untransmitted buffers currently on the TX ring
 * @tx_ring:     TX ring structure
 *
 * Assumes that the device is stopped
 */
static void nfp_net_tx_flush(struct nfp_net_tx_ring *tx_ring)
{
	struct nfp_net_r_vector *r_vec = tx_ring->r_vec;
	struct nfp_net *nn = r_vec->nfp_net;
	struct pci_dev *pdev = nn->pdev;
	const struct skb_frag_struct *frag;
	struct netdev_queue *nd_q;
	struct sk_buff *skb;
	int nr_frags;
	int fidx;
	int idx;

	while (tx_ring->rd_p != tx_ring->wr_p) {
		idx = tx_ring->rd_p % tx_ring->cnt;

		skb = tx_ring->txbufs[idx].skb;
		if (skb) {
			nr_frags = skb_shinfo(skb)->nr_frags;
			fidx = tx_ring->txbufs[idx].fidx;

			if (fidx == -1) {
				/* unmap head */
				dma_unmap_single(&pdev->dev,
						 tx_ring->txbufs[idx].dma_addr,
						 skb_headlen(skb),
						 DMA_TO_DEVICE);
			} else {
				/* unmap fragment */
				frag = &skb_shinfo(skb)->frags[fidx];
				dma_unmap_page(&pdev->dev,
					       tx_ring->txbufs[idx].dma_addr,
					       skb_frag_size(frag),
					       DMA_TO_DEVICE);
			}

			/* check for last gather fragment */
			if (fidx == nr_frags - 1)
				dev_kfree_skb_any(skb);

			tx_ring->txbufs[idx].dma_addr = 0;
			tx_ring->txbufs[idx].skb = NULL;
			tx_ring->txbufs[idx].fidx = -2;
		}

		memset(&tx_ring->txds[idx], 0, sizeof(tx_ring->txds[idx]));

		tx_ring->qcp_rd_p++;
		tx_ring->rd_p++;
	}

	nd_q = netdev_get_tx_queue(nn->netdev, tx_ring->idx);
	netdev_tx_reset_queue(nd_q);
}

static void nfp_net_tx_timeout(struct net_device *netdev)
{
	struct nfp_net *nn = netdev_priv(netdev);
	int i;

	for (i = 0; i < nn->num_tx_rings; i++) {
		if (!netif_tx_queue_stopped(netdev_get_tx_queue(netdev, i)))
			continue;
		nn_warn(nn, "TX timeout on ring: %d\n", i);
	}
	nn_warn(nn, "TX watchdog timeout\n");
}

/* Receive processing
 */

/**
 * nfp_net_rx_space() - return the number of free slots on the RX ring
 * @rx_ring:   RX ring structure
 *
 * Make sure we leave at least one slot free.
 *
 * Return: True if there is space on the RX ring
 */
static inline int nfp_net_rx_space(struct nfp_net_rx_ring *rx_ring)
{
	return (rx_ring->cnt - 1) - (rx_ring->wr_p - rx_ring->rd_p);
}

/**
 * nfp_net_rx_alloc_one() - Allocate and map skb for RX
 * @rx_ring:	RX ring structure of the skb
 * @dma_addr:	Pointer to storage for DMA address (output param)
 *
 * This function will allcate a new skb, map it for DMA.
 *
 * Return: allocated skb or NULL on failure.
 */
static struct sk_buff *
nfp_net_rx_alloc_one(struct nfp_net_rx_ring *rx_ring, dma_addr_t *dma_addr)
{
	struct nfp_net *nn = rx_ring->r_vec->nfp_net;
	struct sk_buff *skb;

	skb = netdev_alloc_skb(nn->netdev, nn->fl_bufsz);
	if (!skb) {
		nn_warn_ratelimit(nn, "Failed to alloc receive SKB\n");
		return NULL;
	}

	*dma_addr = dma_map_single(&nn->pdev->dev, skb->data,
				  nn->fl_bufsz, DMA_FROM_DEVICE);
	if (dma_mapping_error(&nn->pdev->dev, *dma_addr)) {
		dev_kfree_skb_any(skb);
		nn_warn_ratelimit(nn, "Failed to map DMA RX buffer\n");
		return NULL;
	}

	return skb;
}

/**
 * nfp_net_rx_give_one() - Put mapped skb on the software and hardware rings
 * @rx_ring:	RX ring structure
 * @skb:	Skb to put on rings
 * @dma_addr:	DMA address of skb mapping
 */
static void nfp_net_rx_give_one(struct nfp_net_rx_ring *rx_ring,
				struct sk_buff *skb, dma_addr_t dma_addr)
{
	unsigned int wr_idx;

	wr_idx = rx_ring->wr_p % rx_ring->cnt;

	/* Stash SKB and DMA address away */
	rx_ring->rxbufs[wr_idx].skb = skb;
	rx_ring->rxbufs[wr_idx].dma_addr = dma_addr;

	/* Fill freelist descriptor */
	rx_ring->rxds[wr_idx].fld.reserved = 0;
	rx_ring->rxds[wr_idx].fld.meta_len_dd = 0;
	nfp_desc_set_dma_addr(&rx_ring->rxds[wr_idx].fld, dma_addr);

	rx_ring->wr_p++;
	rx_ring->wr_ptr_add++;
	if (rx_ring->wr_ptr_add >= NFP_NET_FL_BATCH) {
		/* Update write pointer of the freelist queue. Make
		 * sure all writes are flushed before telling the hardware.
		 */
		wmb();
		nfp_qcp_wr_ptr_add(rx_ring->qcp_fl, rx_ring->wr_ptr_add);
		rx_ring->wr_ptr_add = 0;
	}
}

/**
 * nfp_net_rx_flush() - Free any buffers currently on the RX ring
 * @rx_ring:  RX ring to remove buffers from
 *
 * Assumes that the device is stopped
 */
static void nfp_net_rx_flush(struct nfp_net_rx_ring *rx_ring)
{
	struct nfp_net *nn = rx_ring->r_vec->nfp_net;
	struct pci_dev *pdev = nn->pdev;
	int idx;

	while (rx_ring->rd_p != rx_ring->wr_p) {
		idx = rx_ring->rd_p % rx_ring->cnt;

		if (rx_ring->rxbufs[idx].skb) {
			dma_unmap_single(&pdev->dev,
					 rx_ring->rxbufs[idx].dma_addr,
					 nn->fl_bufsz, DMA_FROM_DEVICE);
			dev_kfree_skb_any(rx_ring->rxbufs[idx].skb);
			rx_ring->rxbufs[idx].dma_addr = 0;
			rx_ring->rxbufs[idx].skb = NULL;
		}

		memset(&rx_ring->rxds[idx], 0, sizeof(rx_ring->rxds[idx]));

		rx_ring->rd_p++;
	}
}

/**
 * nfp_net_rx_fill_freelist() - Attempt filling freelist with RX buffers
 * @rx_ring: RX ring to fill
 *
 * Try to fill as many buffers as possible into freelist.  Return
 * number of buffers added.
 *
 * Return: Number of freelist buffers added.
 */
static int nfp_net_rx_fill_freelist(struct nfp_net_rx_ring *rx_ring)
{
	struct sk_buff *skb;
	dma_addr_t dma_addr;

	while (nfp_net_rx_space(rx_ring)) {
		skb = nfp_net_rx_alloc_one(rx_ring, &dma_addr);
		if (!skb) {
			nfp_net_rx_flush(rx_ring);
			return -ENOMEM;
		}
		nfp_net_rx_give_one(rx_ring, skb, dma_addr);
	}

	return 0;
}

/**
 * nfp_net_rx_csum_has_errors() - group check if rxd has any csum errors
 * @flags: RX descriptor flags field in CPU byte order
 */
static int nfp_net_rx_csum_has_errors(u16 flags)
{
	u16 csum_all_checked, csum_all_ok;

	csum_all_checked = flags & __PCIE_DESC_RX_CSUM_ALL;
	csum_all_ok = flags & __PCIE_DESC_RX_CSUM_ALL_OK;

	return csum_all_checked != (csum_all_ok << PCIE_DESC_RX_CSUM_OK_SHIFT);
}

/**
 * nfp_net_rx_csum() - set SKB checksum field based on RX descriptor flags
 * @nn:  NFP Net device
 * @r_vec: per-ring structure
 * @rxd: Pointer to RX descriptor
 * @skb: Pointer to SKB
 */
static void nfp_net_rx_csum(struct nfp_net *nn, struct nfp_net_r_vector *r_vec,
			    struct nfp_net_rx_desc *rxd, struct sk_buff *skb)
{
	skb_checksum_none_assert(skb);

	if (!(nn->netdev->features & NETIF_F_RXCSUM))
		return;

	if (nfp_net_rx_csum_has_errors(le16_to_cpu(rxd->rxd.flags))) {
		u64_stats_update_begin(&r_vec->rx_sync);
		r_vec->hw_csum_rx_error++;
		u64_stats_update_end(&r_vec->rx_sync);
		return;
	}

	/* Assume that the firmware will never report inner CSUM_OK unless outer
	 * L4 headers were successfully parsed. FW will always report zero UDP
	 * checksum as CSUM_OK.
	 */
	if (rxd->rxd.flags & PCIE_DESC_RX_TCP_CSUM_OK ||
	    rxd->rxd.flags & PCIE_DESC_RX_UDP_CSUM_OK) {
		__skb_incr_checksum_unnecessary(skb);
		u64_stats_update_begin(&r_vec->rx_sync);
		r_vec->hw_csum_rx_ok++;
		u64_stats_update_end(&r_vec->rx_sync);
	}

	if (rxd->rxd.flags & PCIE_DESC_RX_I_TCP_CSUM_OK ||
	    rxd->rxd.flags & PCIE_DESC_RX_I_UDP_CSUM_OK) {
		__skb_incr_checksum_unnecessary(skb);
		u64_stats_update_begin(&r_vec->rx_sync);
		r_vec->hw_csum_rx_inner_ok++;
		u64_stats_update_end(&r_vec->rx_sync);
	}
}

/**
 * nfp_net_set_hash() - Set SKB hash data
 * @netdev: adapter's net_device structure
 * @skb:   SKB to set the hash data on
 * @rxd:   RX descriptor
 *
 * The RSS hash and hash-type are pre-pended to the packet data.
 * Extract and decode it and set the skb fields.
 */
static void nfp_net_set_hash(struct net_device *netdev, struct sk_buff *skb,
			     struct nfp_net_rx_desc *rxd)
{
	struct nfp_net_rx_hash *rx_hash;

	if (!(rxd->rxd.flags & PCIE_DESC_RX_RSS) ||
	    !(netdev->features & NETIF_F_RXHASH))
		return;

	rx_hash = (struct nfp_net_rx_hash *)(skb->data - sizeof(*rx_hash));

	switch (be32_to_cpu(rx_hash->hash_type)) {
	case NFP_NET_RSS_IPV4:
	case NFP_NET_RSS_IPV6:
	case NFP_NET_RSS_IPV6_EX:
		skb_set_hash(skb, be32_to_cpu(rx_hash->hash), PKT_HASH_TYPE_L3);
		break;
	default:
		skb_set_hash(skb, be32_to_cpu(rx_hash->hash), PKT_HASH_TYPE_L4);
		break;
	}
}

/**
 * nfp_net_rx() - receive up to @budget packets on @rx_ring
 * @rx_ring:   RX ring to receive from
 * @budget:    NAPI budget
 *
 * Note, this function is separated out from the napi poll function to
 * more cleanly separate packet receive code from other bookkeeping
 * functions performed in the napi poll function.
 *
 * There are differences between the NFP-3200 firmware and the
 * NFP-6000 firmware.  The NFP-3200 firmware uses a dedicated RX queue
 * to indicate that new packets have arrived.  The NFP-6000 does not
 * have this queue and uses the DD bit in the RX descriptor. This
 * method cannot be used on the NFP-3200 as it causes a race
 * condition: The RX ring write pointer on the NFP-3200 is updated
 * after packets (and descriptors) have been DMAed.  If the DD bit is
 * used and subsequently the read pointer is updated this may lead to
 * the RX queue to underflow (if the firmware has not yet update the
 * write pointer).  Therefore we use slightly ugly conditional code
 * below to handle the differences.  We may, in the future update the
 * NFP-3200 firmware to behave the same as the firmware on the
 * NFP-6000.
 *
 * Return: Number of packets received.
 */
static int nfp_net_rx(struct nfp_net_rx_ring *rx_ring, int budget)
{
	struct nfp_net_r_vector *r_vec = rx_ring->r_vec;
	struct nfp_net *nn = r_vec->nfp_net;
	unsigned int data_len, meta_len;
	int avail = 0, pkts_polled = 0;
	struct sk_buff *skb, *new_skb;
	struct nfp_net_rx_desc *rxd;
	dma_addr_t new_dma_addr;
	u32 qcp_wr_p;
	int idx;

	if (nn->is_nfp3200) {
		/* Work out how many packets arrived */
		qcp_wr_p = nfp_qcp_wr_ptr_read(rx_ring->qcp_rx);
		idx = rx_ring->rd_p % rx_ring->cnt;

		if (qcp_wr_p == idx)
			/* No new packets */
			return 0;

		if (qcp_wr_p > idx)
			avail = qcp_wr_p - idx;
		else
			avail = qcp_wr_p + rx_ring->cnt - idx;
	} else {
		avail = budget + 1;
	}

	while (avail > 0 && pkts_polled < budget) {
		idx = rx_ring->rd_p % rx_ring->cnt;

		rxd = &rx_ring->rxds[idx];
		if (!(rxd->rxd.meta_len_dd & PCIE_DESC_RX_DD)) {
			if (nn->is_nfp3200)
				nn_dbg(nn, "RX descriptor not valid (DD)%d:%u rxd[0]=%#x rxd[1]=%#x\n",
				       rx_ring->idx, idx,
				       rxd->vals[0], rxd->vals[1]);
			break;
		}
		/* Memory barrier to ensure that we won't do other reads
		 * before the DD bit.
		 */
		dma_rmb();

		rx_ring->rd_p++;
		pkts_polled++;
		avail--;

		skb = rx_ring->rxbufs[idx].skb;

		new_skb = nfp_net_rx_alloc_one(rx_ring, &new_dma_addr);
		if (!new_skb) {
			nfp_net_rx_give_one(rx_ring, rx_ring->rxbufs[idx].skb,
					    rx_ring->rxbufs[idx].dma_addr);
			u64_stats_update_begin(&r_vec->rx_sync);
			r_vec->rx_drops++;
			u64_stats_update_end(&r_vec->rx_sync);
			continue;
		}

		dma_unmap_single(&nn->pdev->dev,
				 rx_ring->rxbufs[idx].dma_addr,
				 nn->fl_bufsz, DMA_FROM_DEVICE);

		nfp_net_rx_give_one(rx_ring, new_skb, new_dma_addr);

		meta_len = rxd->rxd.meta_len_dd & PCIE_DESC_RX_META_LEN_MASK;
		data_len = le16_to_cpu(rxd->rxd.data_len);

		if (WARN_ON_ONCE(data_len > nn->fl_bufsz)) {
			dev_kfree_skb_any(skb);
			continue;
		}

		if (nn->rx_offset == NFP_NET_CFG_RX_OFFSET_DYNAMIC) {
			/* The packet data starts after the metadata */
			skb_reserve(skb, meta_len);
		} else {
			/* The packet data starts at a fixed offset */
			skb_reserve(skb, nn->rx_offset);
		}

		/* Adjust the SKB for the dynamic meta data pre-pended */
		skb_put(skb, data_len - meta_len);

		nfp_net_set_hash(nn->netdev, skb, rxd);

		/* Pad small frames to minimum */
		if (skb_put_padto(skb, 60))
			break;

		/* Stats update */
		u64_stats_update_begin(&r_vec->rx_sync);
		r_vec->rx_pkts++;
		r_vec->rx_bytes += skb->len;
		u64_stats_update_end(&r_vec->rx_sync);

		skb_record_rx_queue(skb, rx_ring->idx);
		skb->protocol = eth_type_trans(skb, nn->netdev);

		nfp_net_rx_csum(nn, r_vec, rxd, skb);

		if (rxd->rxd.flags & PCIE_DESC_RX_VLAN)
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
					       le16_to_cpu(rxd->rxd.vlan));

		napi_gro_receive(&rx_ring->r_vec->napi, skb);
	}

	if (nn->is_nfp3200)
		nfp_qcp_rd_ptr_add(rx_ring->qcp_rx, pkts_polled);

	return pkts_polled;
}

/**
 * nfp_net_poll() - napi poll function
 * @napi:    NAPI structure
 * @budget:  NAPI budget
 *
 * Return: number of packets polled.
 */
static int nfp_net_poll(struct napi_struct *napi, int budget)
{
	struct nfp_net_r_vector *r_vec =
		container_of(napi, struct nfp_net_r_vector, napi);
	struct nfp_net_rx_ring *rx_ring = r_vec->rx_ring;
	struct nfp_net_tx_ring *tx_ring = r_vec->tx_ring;
	struct nfp_net *nn = r_vec->nfp_net;
	struct netdev_queue *txq;
	unsigned int pkts_polled;

	tx_ring = &nn->tx_rings[rx_ring->idx];
	txq = netdev_get_tx_queue(nn->netdev, tx_ring->idx);
	nfp_net_tx_complete(tx_ring);

	pkts_polled = nfp_net_rx(rx_ring, budget);

	if (pkts_polled < budget) {
		napi_complete_done(napi, pkts_polled);
		nfp_net_irq_unmask(nn, r_vec->irq_idx);
	}

	return pkts_polled;
}

/* Setup and Configuration
 */

/**
 * nfp_net_tx_ring_free() - Free resources allocated to a TX ring
 * @tx_ring:   TX ring to free
 */
static void nfp_net_tx_ring_free(struct nfp_net_tx_ring *tx_ring)
{
	struct nfp_net_r_vector *r_vec = tx_ring->r_vec;
	struct nfp_net *nn = r_vec->nfp_net;
	struct pci_dev *pdev = nn->pdev;

	nn_writeq(nn, NFP_NET_CFG_TXR_ADDR(tx_ring->idx), 0);
	nn_writeb(nn, NFP_NET_CFG_TXR_SZ(tx_ring->idx), 0);
	nn_writeb(nn, NFP_NET_CFG_TXR_VEC(tx_ring->idx), 0);

	kfree(tx_ring->txbufs);

	if (tx_ring->txds)
		dma_free_coherent(&pdev->dev, tx_ring->size,
				  tx_ring->txds, tx_ring->dma);

	tx_ring->cnt = 0;
	tx_ring->wr_p = 0;
	tx_ring->rd_p = 0;
	tx_ring->qcp_rd_p = 0;

	tx_ring->txbufs = NULL;
	tx_ring->txds = NULL;
	tx_ring->dma = 0;
	tx_ring->size = 0;
}

/**
 * nfp_net_tx_ring_alloc() - Allocate resource for a TX ring
 * @tx_ring:   TX Ring structure to allocate
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int nfp_net_tx_ring_alloc(struct nfp_net_tx_ring *tx_ring)
{
	struct nfp_net_r_vector *r_vec = tx_ring->r_vec;
	struct nfp_net *nn = r_vec->nfp_net;
	struct pci_dev *pdev = nn->pdev;
	int sz;

	tx_ring->cnt = nn->txd_cnt;

	tx_ring->size = sizeof(*tx_ring->txds) * tx_ring->cnt;
	tx_ring->txds = dma_zalloc_coherent(&pdev->dev, tx_ring->size,
					    &tx_ring->dma, GFP_KERNEL);
	if (!tx_ring->txds)
		goto err_alloc;

	sz = sizeof(*tx_ring->txbufs) * tx_ring->cnt;
	tx_ring->txbufs = kzalloc(sz, GFP_KERNEL);
	if (!tx_ring->txbufs)
		goto err_alloc;

	/* Write the DMA address, size and MSI-X info to the device */
	nn_writeq(nn, NFP_NET_CFG_TXR_ADDR(tx_ring->idx), tx_ring->dma);
	nn_writeb(nn, NFP_NET_CFG_TXR_SZ(tx_ring->idx), ilog2(tx_ring->cnt));
	nn_writeb(nn, NFP_NET_CFG_TXR_VEC(tx_ring->idx), r_vec->irq_idx);

	netif_set_xps_queue(nn->netdev, &r_vec->affinity_mask, tx_ring->idx);

	nn_dbg(nn, "TxQ%02d: QCidx=%02d cnt=%d dma=%#llx host=%p\n",
	       tx_ring->idx, tx_ring->qcidx,
	       tx_ring->cnt, (unsigned long long)tx_ring->dma, tx_ring->txds);

	return 0;

err_alloc:
	nfp_net_tx_ring_free(tx_ring);
	return -ENOMEM;
}

/**
 * nfp_net_rx_ring_free() - Free resources allocated to a RX ring
 * @rx_ring:  RX ring to free
 */
static void nfp_net_rx_ring_free(struct nfp_net_rx_ring *rx_ring)
{
	struct nfp_net_r_vector *r_vec = rx_ring->r_vec;
	struct nfp_net *nn = r_vec->nfp_net;
	struct pci_dev *pdev = nn->pdev;

	nn_writeq(nn, NFP_NET_CFG_RXR_ADDR(rx_ring->idx), 0);
	nn_writeb(nn, NFP_NET_CFG_RXR_SZ(rx_ring->idx), 0);
	nn_writeb(nn, NFP_NET_CFG_RXR_VEC(rx_ring->idx), 0);

	kfree(rx_ring->rxbufs);

	if (rx_ring->rxds)
		dma_free_coherent(&pdev->dev, rx_ring->size,
				  rx_ring->rxds, rx_ring->dma);

	rx_ring->cnt = 0;
	rx_ring->wr_p = 0;
	rx_ring->rd_p = 0;

	rx_ring->rxbufs = NULL;
	rx_ring->rxds = NULL;
	rx_ring->dma = 0;
	rx_ring->size = 0;
}

/**
 * nfp_net_rx_ring_alloc() - Allocate resource for a RX ring
 * @rx_ring:  RX ring to allocate
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int nfp_net_rx_ring_alloc(struct nfp_net_rx_ring *rx_ring)
{
	struct nfp_net_r_vector *r_vec = rx_ring->r_vec;
	struct nfp_net *nn = r_vec->nfp_net;
	struct pci_dev *pdev = nn->pdev;
	int sz;

	rx_ring->cnt = nn->rxd_cnt;

	rx_ring->size = sizeof(*rx_ring->rxds) * rx_ring->cnt;
	rx_ring->rxds = dma_zalloc_coherent(&pdev->dev, rx_ring->size,
					    &rx_ring->dma, GFP_KERNEL);
	if (!rx_ring->rxds)
		goto err_alloc;

	sz = sizeof(*rx_ring->rxbufs) * rx_ring->cnt;
	rx_ring->rxbufs = kzalloc(sz, GFP_KERNEL);
	if (!rx_ring->rxbufs)
		goto err_alloc;

	/* Write the DMA address, size and MSI-X info to the device */
	nn_writeq(nn, NFP_NET_CFG_RXR_ADDR(rx_ring->idx), rx_ring->dma);
	nn_writeb(nn, NFP_NET_CFG_RXR_SZ(rx_ring->idx), ilog2(rx_ring->cnt));
	nn_writeb(nn, NFP_NET_CFG_RXR_VEC(rx_ring->idx), r_vec->irq_idx);

	nn_dbg(nn, "RxQ%02d: FlQCidx=%02d RxQCidx=%02d cnt=%d dma=%#llx host=%p\n",
	       rx_ring->idx, rx_ring->fl_qcidx, rx_ring->rx_qcidx,
	       rx_ring->cnt, (unsigned long long)rx_ring->dma, rx_ring->rxds);

	return 0;

err_alloc:
	nfp_net_rx_ring_free(rx_ring);
	return -ENOMEM;
}

static void __nfp_net_free_rings(struct nfp_net *nn, unsigned int n_free)
{
	struct nfp_net_r_vector *r_vec;
	struct msix_entry *entry;

	while (n_free--) {
		r_vec = &nn->r_vecs[n_free];
		entry = &nn->irq_entries[r_vec->irq_idx];

		nfp_net_rx_ring_free(r_vec->rx_ring);
		nfp_net_tx_ring_free(r_vec->tx_ring);

		irq_set_affinity_hint(entry->vector, NULL);
		free_irq(entry->vector, r_vec);

		netif_napi_del(&r_vec->napi);
	}
}

/**
 * nfp_net_free_rings() - Free all ring resources
 * @nn:      NFP Net device to reconfigure
 */
static void nfp_net_free_rings(struct nfp_net *nn)
{
	__nfp_net_free_rings(nn, nn->num_r_vecs);
}

/**
 * nfp_net_alloc_rings() - Allocate resources for RX and TX rings
 * @nn:      NFP Net device to reconfigure
 *
 * Return: 0 on success or negative errno on error.
 */
static int nfp_net_alloc_rings(struct nfp_net *nn)
{
	struct nfp_net_r_vector *r_vec;
	struct msix_entry *entry;
	int err;
	int r;

	for (r = 0; r < nn->num_r_vecs; r++) {
		r_vec = &nn->r_vecs[r];
		entry = &nn->irq_entries[r_vec->irq_idx];

		/* Setup NAPI */
		netif_napi_add(nn->netdev, &r_vec->napi,
			       nfp_net_poll, NAPI_POLL_WEIGHT);

		snprintf(r_vec->name, sizeof(r_vec->name),
			 "%s-rxtx-%d", nn->netdev->name, r);
		err = request_irq(entry->vector, r_vec->handler, 0,
				  r_vec->name, r_vec);
		if (err) {
			nn_dbg(nn, "Error requesting IRQ %d\n", entry->vector);
			goto err_napi_del;
		}

		irq_set_affinity_hint(entry->vector, &r_vec->affinity_mask);

		nn_dbg(nn, "RV%02d: irq=%03d/%03d\n",
		       r, entry->vector, entry->entry);

		/* Allocate TX ring resources */
		err = nfp_net_tx_ring_alloc(r_vec->tx_ring);
		if (err)
			goto err_free_irq;

		/* Allocate RX ring resources */
		err = nfp_net_rx_ring_alloc(r_vec->rx_ring);
		if (err)
			goto err_free_tx;
	}

	return 0;

err_free_tx:
	nfp_net_tx_ring_free(r_vec->tx_ring);
err_free_irq:
	irq_set_affinity_hint(entry->vector, NULL);
	free_irq(entry->vector, r_vec);
err_napi_del:
	netif_napi_del(&r_vec->napi);
	__nfp_net_free_rings(nn, r);
	return err;
}

/**
 * nfp_net_rss_write_itbl() - Write RSS indirection table to device
 * @nn:      NFP Net device to reconfigure
 */
void nfp_net_rss_write_itbl(struct nfp_net *nn)
{
	int i;

	for (i = 0; i < NFP_NET_CFG_RSS_ITBL_SZ; i += 4)
		nn_writel(nn, NFP_NET_CFG_RSS_ITBL + i,
			  get_unaligned_le32(nn->rss_itbl + i));
}

/**
 * nfp_net_rss_write_key() - Write RSS hash key to device
 * @nn:      NFP Net device to reconfigure
 */
void nfp_net_rss_write_key(struct nfp_net *nn)
{
	int i;

	for (i = 0; i < NFP_NET_CFG_RSS_KEY_SZ; i += 4)
		nn_writel(nn, NFP_NET_CFG_RSS_KEY + i,
			  get_unaligned_le32(nn->rss_key + i));
}

/**
 * nfp_net_coalesce_write_cfg() - Write irq coalescence configuration to HW
 * @nn:      NFP Net device to reconfigure
 */
void nfp_net_coalesce_write_cfg(struct nfp_net *nn)
{
	u8 i;
	u32 factor;
	u32 value;

	/* Compute factor used to convert coalesce '_usecs' parameters to
	 * ME timestamp ticks.  There are 16 ME clock cycles for each timestamp
	 * count.
	 */
	factor = nn->me_freq_mhz / 16;

	/* copy RX interrupt coalesce parameters */
	value = (nn->rx_coalesce_max_frames << 16) |
		(factor * nn->rx_coalesce_usecs);
	for (i = 0; i < nn->num_r_vecs; i++)
		nn_writel(nn, NFP_NET_CFG_RXR_IRQ_MOD(i), value);

	/* copy TX interrupt coalesce parameters */
	value = (nn->tx_coalesce_max_frames << 16) |
		(factor * nn->tx_coalesce_usecs);
	for (i = 0; i < nn->num_r_vecs; i++)
		nn_writel(nn, NFP_NET_CFG_TXR_IRQ_MOD(i), value);
}

/**
 * nfp_net_write_mac_addr() - Write mac address to device registers
 * @nn:      NFP Net device to reconfigure
 * @mac:     Six-byte MAC address to be written
 *
 * We do a bit of byte swapping dance because firmware is LE.
 */
static void nfp_net_write_mac_addr(struct nfp_net *nn, const u8 *mac)
{
	nn_writel(nn, NFP_NET_CFG_MACADDR + 0,
		  get_unaligned_be32(nn->netdev->dev_addr));
	/* We can't do writew for NFP-3200 compatibility */
	nn_writel(nn, NFP_NET_CFG_MACADDR + 4,
		  get_unaligned_be16(nn->netdev->dev_addr + 4) << 16);
}

/**
 * nfp_net_clear_config_and_disable() - Clear control BAR and disable NFP
 * @nn:      NFP Net device to reconfigure
 */
static void nfp_net_clear_config_and_disable(struct nfp_net *nn)
{
	u32 new_ctrl, update;
	int err;

	new_ctrl = nn->ctrl;
	new_ctrl &= ~NFP_NET_CFG_CTRL_ENABLE;
	update = NFP_NET_CFG_UPDATE_GEN;
	update |= NFP_NET_CFG_UPDATE_MSIX;
	update |= NFP_NET_CFG_UPDATE_RING;

	if (nn->cap & NFP_NET_CFG_CTRL_RINGCFG)
		new_ctrl &= ~NFP_NET_CFG_CTRL_RINGCFG;

	nn_writeq(nn, NFP_NET_CFG_TXRS_ENABLE, 0);
	nn_writeq(nn, NFP_NET_CFG_RXRS_ENABLE, 0);

	nn_writel(nn, NFP_NET_CFG_CTRL, new_ctrl);
	err = nfp_net_reconfig(nn, update);
	if (err) {
		nn_err(nn, "Could not disable device: %d\n", err);
		return;
	}

	nn->ctrl = new_ctrl;
}

/**
 * nfp_net_start_vec() - Start ring vector
 * @nn:      NFP Net device structure
 * @r_vec:   Ring vector to be started
 */
static int nfp_net_start_vec(struct nfp_net *nn, struct nfp_net_r_vector *r_vec)
{
	unsigned int irq_vec;
	int err = 0;

	irq_vec = nn->irq_entries[r_vec->irq_idx].vector;

	disable_irq(irq_vec);

	err = nfp_net_rx_fill_freelist(r_vec->rx_ring);
	if (err) {
		nn_err(nn, "RV%02d: couldn't allocate enough buffers\n",
		       r_vec->irq_idx);
		goto out;
	}

	napi_enable(&r_vec->napi);
out:
	enable_irq(irq_vec);

	return err;
}

static int nfp_net_netdev_open(struct net_device *netdev)
{
	struct nfp_net *nn = netdev_priv(netdev);
	int err, r;
	u32 update = 0;
	u32 new_ctrl;

	if (nn->ctrl & NFP_NET_CFG_CTRL_ENABLE) {
		nn_err(nn, "Dev is already enabled: 0x%08x\n", nn->ctrl);
		return -EBUSY;
	}

	new_ctrl = nn->ctrl;

	/* Step 1: Allocate resources for rings and the like
	 * - Request interrupts
	 * - Allocate RX and TX ring resources
	 * - Setup initial RSS table
	 */
	err = nfp_net_aux_irq_request(nn, NFP_NET_CFG_EXN, "%s-exn",
				      nn->exn_name, sizeof(nn->exn_name),
				      NFP_NET_IRQ_EXN_IDX, nn->exn_handler);
	if (err)
		return err;

	err = nfp_net_alloc_rings(nn);
	if (err)
		goto err_free_exn;

	err = netif_set_real_num_tx_queues(netdev, nn->num_tx_rings);
	if (err)
		goto err_free_rings;

	err = netif_set_real_num_rx_queues(netdev, nn->num_rx_rings);
	if (err)
		goto err_free_rings;

	if (nn->cap & NFP_NET_CFG_CTRL_RSS) {
		nfp_net_rss_write_key(nn);
		nfp_net_rss_write_itbl(nn);
		nn_writel(nn, NFP_NET_CFG_RSS_CTRL, nn->rss_cfg);
		update |= NFP_NET_CFG_UPDATE_RSS;
	}

	if (nn->cap & NFP_NET_CFG_CTRL_IRQMOD) {
		nfp_net_coalesce_write_cfg(nn);

		new_ctrl |= NFP_NET_CFG_CTRL_IRQMOD;
		update |= NFP_NET_CFG_UPDATE_IRQMOD;
	}

	/* Step 2: Configure the NFP
	 * - Enable rings from 0 to tx_rings/rx_rings - 1.
	 * - Write MAC address (in case it changed)
	 * - Set the MTU
	 * - Set the Freelist buffer size
	 * - Enable the FW
	 */
	nn_writeq(nn, NFP_NET_CFG_TXRS_ENABLE, nn->num_tx_rings == 64 ?
		  0xffffffffffffffffULL : ((u64)1 << nn->num_tx_rings) - 1);

	nn_writeq(nn, NFP_NET_CFG_RXRS_ENABLE, nn->num_rx_rings == 64 ?
		  0xffffffffffffffffULL : ((u64)1 << nn->num_rx_rings) - 1);

	nfp_net_write_mac_addr(nn, netdev->dev_addr);

	nn_writel(nn, NFP_NET_CFG_MTU, netdev->mtu);
	nn_writel(nn, NFP_NET_CFG_FLBUFSZ, nn->fl_bufsz);

	/* Enable device */
	new_ctrl |= NFP_NET_CFG_CTRL_ENABLE;
	update |= NFP_NET_CFG_UPDATE_GEN;
	update |= NFP_NET_CFG_UPDATE_MSIX;
	update |= NFP_NET_CFG_UPDATE_RING;
	if (nn->cap & NFP_NET_CFG_CTRL_RINGCFG)
		new_ctrl |= NFP_NET_CFG_CTRL_RINGCFG;

	nn_writel(nn, NFP_NET_CFG_CTRL, new_ctrl);
	err = nfp_net_reconfig(nn, update);
	if (err)
		goto err_clear_config;

	nn->ctrl = new_ctrl;

	/* Since reconfiguration requests while NFP is down are ignored we
	 * have to wipe the entire VXLAN configuration and reinitialize it.
	 */
	if (nn->ctrl & NFP_NET_CFG_CTRL_VXLAN) {
		memset(&nn->vxlan_ports, 0, sizeof(nn->vxlan_ports));
		memset(&nn->vxlan_usecnt, 0, sizeof(nn->vxlan_usecnt));
		vxlan_get_rx_port(netdev);
	}

	/* Step 3: Enable for kernel
	 * - put some freelist descriptors on each RX ring
	 * - enable NAPI on each ring
	 * - enable all TX queues
	 * - set link state
	 */
	for (r = 0; r < nn->num_r_vecs; r++) {
		err = nfp_net_start_vec(nn, &nn->r_vecs[r]);
		if (err)
			goto err_disable_napi;
	}

	netif_tx_wake_all_queues(netdev);

	err = nfp_net_aux_irq_request(nn, NFP_NET_CFG_LSC, "%s-lsc",
				      nn->lsc_name, sizeof(nn->lsc_name),
				      NFP_NET_IRQ_LSC_IDX, nn->lsc_handler);
	if (err)
		goto err_stop_tx;
	nfp_net_read_link_status(nn);

	return 0;

err_stop_tx:
	netif_tx_disable(netdev);
	for (r = 0; r < nn->num_r_vecs; r++)
		nfp_net_tx_flush(nn->r_vecs[r].tx_ring);
err_disable_napi:
	while (r--) {
		napi_disable(&nn->r_vecs[r].napi);
		nfp_net_rx_flush(nn->r_vecs[r].rx_ring);
	}
err_clear_config:
	nfp_net_clear_config_and_disable(nn);
err_free_rings:
	nfp_net_free_rings(nn);
err_free_exn:
	nfp_net_aux_irq_free(nn, NFP_NET_CFG_EXN, NFP_NET_IRQ_EXN_IDX);
	return err;
}

/**
 * nfp_net_netdev_close() - Called when the device is downed
 * @netdev:      netdev structure
 */
static int nfp_net_netdev_close(struct net_device *netdev)
{
	struct nfp_net *nn = netdev_priv(netdev);
	int r;

	if (!(nn->ctrl & NFP_NET_CFG_CTRL_ENABLE)) {
		nn_err(nn, "Dev is not up: 0x%08x\n", nn->ctrl);
		return 0;
	}

	/* Step 1: Disable RX and TX rings from the Linux kernel perspective
	 */
	nfp_net_aux_irq_free(nn, NFP_NET_CFG_LSC, NFP_NET_IRQ_LSC_IDX);
	netif_carrier_off(netdev);
	nn->link_up = false;

	for (r = 0; r < nn->num_r_vecs; r++)
		napi_disable(&nn->r_vecs[r].napi);

	netif_tx_disable(netdev);

	/* Step 2: Tell NFP
	 */
	nfp_net_clear_config_and_disable(nn);

	/* Step 3: Free resources
	 */
	for (r = 0; r < nn->num_r_vecs; r++) {
		nfp_net_rx_flush(nn->r_vecs[r].rx_ring);
		nfp_net_tx_flush(nn->r_vecs[r].tx_ring);
	}

	nfp_net_free_rings(nn);
	nfp_net_aux_irq_free(nn, NFP_NET_CFG_EXN, NFP_NET_IRQ_EXN_IDX);

	nn_dbg(nn, "%s down", netdev->name);
	return 0;
}

static void nfp_net_set_rx_mode(struct net_device *netdev)
{
	struct nfp_net *nn = netdev_priv(netdev);
	u32 new_ctrl;

	new_ctrl = nn->ctrl;

	if (netdev->flags & IFF_PROMISC) {
		if (nn->cap & NFP_NET_CFG_CTRL_PROMISC)
			new_ctrl |= NFP_NET_CFG_CTRL_PROMISC;
		else
			nn_warn(nn, "FW does not support promiscuous mode\n");
	} else {
		new_ctrl &= ~NFP_NET_CFG_CTRL_PROMISC;
	}

	if (new_ctrl == nn->ctrl)
		return;

	nn_writel(nn, NFP_NET_CFG_CTRL, new_ctrl);
	if (nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_GEN))
		return;

	nn->ctrl = new_ctrl;
}

static int nfp_net_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct nfp_net *nn = netdev_priv(netdev);
	u32 tmp;

	nn_dbg(nn, "New MTU = %d\n", new_mtu);

	if (new_mtu < 68 || new_mtu > nn->max_mtu) {
		nn_err(nn, "New MTU (%d) is not valid\n", new_mtu);
		return -EINVAL;
	}

	netdev->mtu = new_mtu;

	/* Freelist buffer size rounded up to the nearest 1K */
	tmp = new_mtu + ETH_HLEN + VLAN_HLEN + NFP_NET_MAX_PREPEND;
	nn->fl_bufsz = roundup(tmp, 1024);

	/* restart if running */
	if (netif_running(netdev)) {
		nfp_net_netdev_close(netdev);
		nfp_net_netdev_open(netdev);
	}

	return 0;
}

static struct rtnl_link_stats64 *nfp_net_stat64(struct net_device *netdev,
						struct rtnl_link_stats64 *stats)
{
	struct nfp_net *nn = netdev_priv(netdev);
	int r;

	for (r = 0; r < nn->num_r_vecs; r++) {
		struct nfp_net_r_vector *r_vec = &nn->r_vecs[r];
		u64 data[3];
		unsigned int start;

		do {
			start = u64_stats_fetch_begin(&r_vec->rx_sync);
			data[0] = r_vec->rx_pkts;
			data[1] = r_vec->rx_bytes;
			data[2] = r_vec->rx_drops;
		} while (u64_stats_fetch_retry(&r_vec->rx_sync, start));
		stats->rx_packets += data[0];
		stats->rx_bytes += data[1];
		stats->rx_dropped += data[2];

		do {
			start = u64_stats_fetch_begin(&r_vec->tx_sync);
			data[0] = r_vec->tx_pkts;
			data[1] = r_vec->tx_bytes;
			data[2] = r_vec->tx_errors;
		} while (u64_stats_fetch_retry(&r_vec->tx_sync, start));
		stats->tx_packets += data[0];
		stats->tx_bytes += data[1];
		stats->tx_errors += data[2];
	}

	return stats;
}

static int nfp_net_set_features(struct net_device *netdev,
				netdev_features_t features)
{
	netdev_features_t changed = netdev->features ^ features;
	struct nfp_net *nn = netdev_priv(netdev);
	u32 new_ctrl;
	int err;

	/* Assume this is not called with features we have not advertised */

	new_ctrl = nn->ctrl;

	if (changed & NETIF_F_RXCSUM) {
		if (features & NETIF_F_RXCSUM)
			new_ctrl |= NFP_NET_CFG_CTRL_RXCSUM;
		else
			new_ctrl &= ~NFP_NET_CFG_CTRL_RXCSUM;
	}

	if (changed & (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM)) {
		if (features & (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM))
			new_ctrl |= NFP_NET_CFG_CTRL_TXCSUM;
		else
			new_ctrl &= ~NFP_NET_CFG_CTRL_TXCSUM;
	}

	if (changed & (NETIF_F_TSO | NETIF_F_TSO6)) {
		if (features & (NETIF_F_TSO | NETIF_F_TSO6))
			new_ctrl |= NFP_NET_CFG_CTRL_LSO;
		else
			new_ctrl &= ~NFP_NET_CFG_CTRL_LSO;
	}

	if (changed & NETIF_F_HW_VLAN_CTAG_RX) {
		if (features & NETIF_F_HW_VLAN_CTAG_RX)
			new_ctrl |= NFP_NET_CFG_CTRL_RXVLAN;
		else
			new_ctrl &= ~NFP_NET_CFG_CTRL_RXVLAN;
	}

	if (changed & NETIF_F_HW_VLAN_CTAG_TX) {
		if (features & NETIF_F_HW_VLAN_CTAG_TX)
			new_ctrl |= NFP_NET_CFG_CTRL_TXVLAN;
		else
			new_ctrl &= ~NFP_NET_CFG_CTRL_TXVLAN;
	}

	if (changed & NETIF_F_SG) {
		if (features & NETIF_F_SG)
			new_ctrl |= NFP_NET_CFG_CTRL_GATHER;
		else
			new_ctrl &= ~NFP_NET_CFG_CTRL_GATHER;
	}

	nn_dbg(nn, "Feature change 0x%llx -> 0x%llx (changed=0x%llx)\n",
	       netdev->features, features, changed);

	if (new_ctrl == nn->ctrl)
		return 0;

	nn_dbg(nn, "NIC ctrl: 0x%x -> 0x%x\n", nn->ctrl, new_ctrl);
	nn_writel(nn, NFP_NET_CFG_CTRL, new_ctrl);
	err = nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_GEN);
	if (err)
		return err;

	nn->ctrl = new_ctrl;

	return 0;
}

static netdev_features_t
nfp_net_features_check(struct sk_buff *skb, struct net_device *dev,
		       netdev_features_t features)
{
	u8 l4_hdr;

	/* We can't do TSO over double tagged packets (802.1AD) */
	features &= vlan_features_check(skb, features);

	if (!skb->encapsulation)
		return features;

	/* Ensure that inner L4 header offset fits into TX descriptor field */
	if (skb_is_gso(skb)) {
		u32 hdrlen;

		hdrlen = skb_inner_transport_header(skb) - skb->data +
			inner_tcp_hdrlen(skb);

		if (unlikely(hdrlen > NFP_NET_LSO_MAX_HDR_SZ))
			features &= ~NETIF_F_GSO_MASK;
	}

	/* VXLAN/GRE check */
	switch (vlan_get_protocol(skb)) {
	case htons(ETH_P_IP):
		l4_hdr = ip_hdr(skb)->protocol;
		break;
	case htons(ETH_P_IPV6):
		l4_hdr = ipv6_hdr(skb)->nexthdr;
		break;
	default:
		return features & ~(NETIF_F_ALL_CSUM | NETIF_F_GSO_MASK);
	}

	if (skb->inner_protocol_type != ENCAP_TYPE_ETHER ||
	    skb->inner_protocol != htons(ETH_P_TEB) ||
	    (l4_hdr != IPPROTO_UDP && l4_hdr != IPPROTO_GRE) ||
	    (l4_hdr == IPPROTO_UDP &&
	     (skb_inner_mac_header(skb) - skb_transport_header(skb) !=
	      sizeof(struct udphdr) + sizeof(struct vxlanhdr))))
		return features & ~(NETIF_F_ALL_CSUM | NETIF_F_GSO_MASK);

	return features;
}

/**
 * nfp_net_set_vxlan_port() - set vxlan port in SW and reconfigure HW
 * @nn:   NFP Net device to reconfigure
 * @idx:  Index into the port table where new port should be written
 * @port: UDP port to configure (pass zero to remove VXLAN port)
 */
static void nfp_net_set_vxlan_port(struct nfp_net *nn, int idx, __be16 port)
{
	int i;

	nn->vxlan_ports[idx] = port;

	if (!(nn->ctrl & NFP_NET_CFG_CTRL_VXLAN))
		return;

	BUILD_BUG_ON(NFP_NET_N_VXLAN_PORTS & 1);
	for (i = 0; i < NFP_NET_N_VXLAN_PORTS; i += 2)
		nn_writel(nn, NFP_NET_CFG_VXLAN_PORT + i * sizeof(port),
			  be16_to_cpu(nn->vxlan_ports[i + 1]) << 16 |
			  be16_to_cpu(nn->vxlan_ports[i]));

	nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_VXLAN);
}

/**
 * nfp_net_find_vxlan_idx() - find table entry of the port or a free one
 * @nn:   NFP Network structure
 * @port: UDP port to look for
 *
 * Return: if the port is already in the table -- it's position;
 *	   if the port is not in the table -- free position to use;
 *	   if the table is full -- -ENOSPC.
 */
static int nfp_net_find_vxlan_idx(struct nfp_net *nn, __be16 port)
{
	int i, free_idx = -ENOSPC;

	for (i = 0; i < NFP_NET_N_VXLAN_PORTS; i++) {
		if (nn->vxlan_ports[i] == port)
			return i;
		if (!nn->vxlan_usecnt[i])
			free_idx = i;
	}

	return free_idx;
}

static void nfp_net_add_vxlan_port(struct net_device *netdev,
				   sa_family_t sa_family, __be16 port)
{
	struct nfp_net *nn = netdev_priv(netdev);
	int idx;

	idx = nfp_net_find_vxlan_idx(nn, port);
	if (idx == -ENOSPC)
		return;

	if (!nn->vxlan_usecnt[idx]++)
		nfp_net_set_vxlan_port(nn, idx, port);
}

static void nfp_net_del_vxlan_port(struct net_device *netdev,
				   sa_family_t sa_family, __be16 port)
{
	struct nfp_net *nn = netdev_priv(netdev);
	int idx;

	idx = nfp_net_find_vxlan_idx(nn, port);
	if (!nn->vxlan_usecnt[idx] || idx == -ENOSPC)
		return;

	if (!--nn->vxlan_usecnt[idx])
		nfp_net_set_vxlan_port(nn, idx, 0);
}

static const struct net_device_ops nfp_net_netdev_ops = {
	.ndo_open		= nfp_net_netdev_open,
	.ndo_stop		= nfp_net_netdev_close,
	.ndo_start_xmit		= nfp_net_tx,
	.ndo_get_stats64	= nfp_net_stat64,
	.ndo_tx_timeout		= nfp_net_tx_timeout,
	.ndo_set_rx_mode	= nfp_net_set_rx_mode,
	.ndo_change_mtu		= nfp_net_change_mtu,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_set_features	= nfp_net_set_features,
	.ndo_features_check	= nfp_net_features_check,
	.ndo_add_vxlan_port     = nfp_net_add_vxlan_port,
	.ndo_del_vxlan_port     = nfp_net_del_vxlan_port,
};

/**
 * nfp_net_info() - Print general info about the NIC
 * @nn:      NFP Net device to reconfigure
 */
void nfp_net_info(struct nfp_net *nn)
{
	nn_info(nn, "Netronome %s %sNetdev: TxQs=%d/%d RxQs=%d/%d\n",
		nn->is_nfp3200 ? "NFP-32xx" : "NFP-6xxx",
		nn->is_vf ? "VF " : "",
		nn->num_tx_rings, nn->max_tx_rings,
		nn->num_rx_rings, nn->max_rx_rings);
	nn_info(nn, "VER: %d.%d.%d.%d, Maximum supported MTU: %d\n",
		nn->fw_ver.resv, nn->fw_ver.class,
		nn->fw_ver.major, nn->fw_ver.minor,
		nn->max_mtu);
	nn_info(nn, "CAP: %#x %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
		nn->cap,
		nn->cap & NFP_NET_CFG_CTRL_PROMISC  ? "PROMISC "  : "",
		nn->cap & NFP_NET_CFG_CTRL_L2BC     ? "L2BCFILT " : "",
		nn->cap & NFP_NET_CFG_CTRL_L2MC     ? "L2MCFILT " : "",
		nn->cap & NFP_NET_CFG_CTRL_RXCSUM   ? "RXCSUM "   : "",
		nn->cap & NFP_NET_CFG_CTRL_TXCSUM   ? "TXCSUM "   : "",
		nn->cap & NFP_NET_CFG_CTRL_RXVLAN   ? "RXVLAN "   : "",
		nn->cap & NFP_NET_CFG_CTRL_TXVLAN   ? "TXVLAN "   : "",
		nn->cap & NFP_NET_CFG_CTRL_SCATTER  ? "SCATTER "  : "",
		nn->cap & NFP_NET_CFG_CTRL_GATHER   ? "GATHER "   : "",
		nn->cap & NFP_NET_CFG_CTRL_LSO      ? "TSO "      : "",
		nn->cap & NFP_NET_CFG_CTRL_RSS      ? "RSS "      : "",
		nn->cap & NFP_NET_CFG_CTRL_L2SWITCH ? "L2SWITCH " : "",
		nn->cap & NFP_NET_CFG_CTRL_MSIXAUTO ? "AUTOMASK " : "",
		nn->cap & NFP_NET_CFG_CTRL_IRQMOD   ? "IRQMOD "   : "",
		nn->cap & NFP_NET_CFG_CTRL_VXLAN    ? "VXLAN "    : "",
		nn->cap & NFP_NET_CFG_CTRL_NVGRE    ? "NVGRE "	  : "");
}

/**
 * nfp_net_netdev_alloc() - Allocate netdev and related structure
 * @pdev:         PCI device
 * @max_tx_rings: Maximum number of TX rings supported by device
 * @max_rx_rings: Maximum number of RX rings supported by device
 *
 * This function allocates a netdev device and fills in the initial
 * part of the @struct nfp_net structure.
 *
 * Return: NFP Net device structure, or ERR_PTR on error.
 */
struct nfp_net *nfp_net_netdev_alloc(struct pci_dev *pdev,
				     int max_tx_rings, int max_rx_rings)
{
	struct net_device *netdev;
	struct nfp_net *nn;
	int nqs;

	netdev = alloc_etherdev_mqs(sizeof(struct nfp_net),
				    max_tx_rings, max_rx_rings);
	if (!netdev)
		return ERR_PTR(-ENOMEM);

	SET_NETDEV_DEV(netdev, &pdev->dev);
	nn = netdev_priv(netdev);

	nn->netdev = netdev;
	nn->pdev = pdev;

	nn->max_tx_rings = max_tx_rings;
	nn->max_rx_rings = max_rx_rings;

	nqs = netif_get_num_default_rss_queues();
	nn->num_tx_rings = min_t(int, nqs, max_tx_rings);
	nn->num_rx_rings = min_t(int, nqs, max_rx_rings);

	nn->txd_cnt = NFP_NET_TX_DESCS_DEFAULT;
	nn->rxd_cnt = NFP_NET_RX_DESCS_DEFAULT;

	spin_lock_init(&nn->reconfig_lock);
	spin_lock_init(&nn->link_status_lock);

	return nn;
}

/**
 * nfp_net_netdev_free() - Undo what @nfp_net_netdev_alloc() did
 * @nn:      NFP Net device to reconfigure
 */
void nfp_net_netdev_free(struct nfp_net *nn)
{
	free_netdev(nn->netdev);
}

/**
 * nfp_net_rss_init() - Set the initial RSS parameters
 * @nn:	     NFP Net device to reconfigure
 */
static void nfp_net_rss_init(struct nfp_net *nn)
{
	int i;

	netdev_rss_key_fill(nn->rss_key, NFP_NET_CFG_RSS_KEY_SZ);

	for (i = 0; i < sizeof(nn->rss_itbl); i++)
		nn->rss_itbl[i] =
			ethtool_rxfh_indir_default(i, nn->num_rx_rings);

	/* Enable IPv4/IPv6 TCP by default */
	nn->rss_cfg = NFP_NET_CFG_RSS_IPV4_TCP |
		      NFP_NET_CFG_RSS_IPV6_TCP |
		      NFP_NET_CFG_RSS_TOEPLITZ |
		      NFP_NET_CFG_RSS_MASK;
}

/**
 * nfp_net_irqmod_init() - Set the initial IRQ moderation parameters
 * @nn:	     NFP Net device to reconfigure
 */
static void nfp_net_irqmod_init(struct nfp_net *nn)
{
	nn->rx_coalesce_usecs      = 50;
	nn->rx_coalesce_max_frames = 64;
	nn->tx_coalesce_usecs      = 50;
	nn->tx_coalesce_max_frames = 64;
}

/**
 * nfp_net_netdev_init() - Initialise/finalise the netdev structure
 * @netdev:      netdev structure
 *
 * Return: 0 on success or negative errno on error.
 */
int nfp_net_netdev_init(struct net_device *netdev)
{
	struct nfp_net *nn = netdev_priv(netdev);
	int err;

	/* Get some of the read-only fields from the BAR */
	nn->cap = nn_readl(nn, NFP_NET_CFG_CAP);
	nn->max_mtu = nn_readl(nn, NFP_NET_CFG_MAX_MTU);

	nfp_net_write_mac_addr(nn, nn->netdev->dev_addr);

	/* Set default MTU and Freelist buffer size */
	if (nn->max_mtu < NFP_NET_DEFAULT_MTU)
		netdev->mtu = nn->max_mtu;
	else
		netdev->mtu = NFP_NET_DEFAULT_MTU;
	nn->fl_bufsz = NFP_NET_DEFAULT_RX_BUFSZ;

	/* Advertise/enable offloads based on capabilities
	 *
	 * Note: netdev->features show the currently enabled features
	 * and netdev->hw_features advertises which features are
	 * supported.  By default we enable most features.
	 */
	netdev->hw_features = NETIF_F_HIGHDMA;
	if (nn->cap & NFP_NET_CFG_CTRL_RXCSUM) {
		netdev->hw_features |= NETIF_F_RXCSUM;
		nn->ctrl |= NFP_NET_CFG_CTRL_RXCSUM;
	}
	if (nn->cap & NFP_NET_CFG_CTRL_TXCSUM) {
		netdev->hw_features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;
		nn->ctrl |= NFP_NET_CFG_CTRL_TXCSUM;
	}
	if (nn->cap & NFP_NET_CFG_CTRL_GATHER) {
		netdev->hw_features |= NETIF_F_SG;
		nn->ctrl |= NFP_NET_CFG_CTRL_GATHER;
	}
	if ((nn->cap & NFP_NET_CFG_CTRL_LSO) && nn->fw_ver.major > 2) {
		netdev->hw_features |= NETIF_F_TSO | NETIF_F_TSO6;
		nn->ctrl |= NFP_NET_CFG_CTRL_LSO;
	}
	if (nn->cap & NFP_NET_CFG_CTRL_RSS) {
		netdev->hw_features |= NETIF_F_RXHASH;
		nfp_net_rss_init(nn);
		nn->ctrl |= NFP_NET_CFG_CTRL_RSS;
	}
	if (nn->cap & NFP_NET_CFG_CTRL_VXLAN &&
	    nn->cap & NFP_NET_CFG_CTRL_NVGRE) {
		if (nn->cap & NFP_NET_CFG_CTRL_LSO)
			netdev->hw_features |= NETIF_F_GSO_GRE |
					       NETIF_F_GSO_UDP_TUNNEL;
		nn->ctrl |= NFP_NET_CFG_CTRL_VXLAN | NFP_NET_CFG_CTRL_NVGRE;

		netdev->hw_enc_features = netdev->hw_features;
	}

	netdev->vlan_features = netdev->hw_features;

	if (nn->cap & NFP_NET_CFG_CTRL_RXVLAN) {
		netdev->hw_features |= NETIF_F_HW_VLAN_CTAG_RX;
		nn->ctrl |= NFP_NET_CFG_CTRL_RXVLAN;
	}
	if (nn->cap & NFP_NET_CFG_CTRL_TXVLAN) {
		netdev->hw_features |= NETIF_F_HW_VLAN_CTAG_TX;
		nn->ctrl |= NFP_NET_CFG_CTRL_TXVLAN;
	}

	netdev->features = netdev->hw_features;

	/* Advertise but disable TSO by default. */
	netdev->features &= ~(NETIF_F_TSO | NETIF_F_TSO6);

	/* Allow L2 Broadcast and Multicast through by default, if supported */
	if (nn->cap & NFP_NET_CFG_CTRL_L2BC)
		nn->ctrl |= NFP_NET_CFG_CTRL_L2BC;
	if (nn->cap & NFP_NET_CFG_CTRL_L2MC)
		nn->ctrl |= NFP_NET_CFG_CTRL_L2MC;

	/* Allow IRQ moderation, if supported */
	if (nn->cap & NFP_NET_CFG_CTRL_IRQMOD) {
		nfp_net_irqmod_init(nn);
		nn->ctrl |= NFP_NET_CFG_CTRL_IRQMOD;
	}

	/* On NFP-3200 enable MSI-X auto-masking, if supported and the
	 * interrupts are not shared.
	 */
	if (nn->is_nfp3200 && nn->cap & NFP_NET_CFG_CTRL_MSIXAUTO)
		nn->ctrl |= NFP_NET_CFG_CTRL_MSIXAUTO;

	/* On NFP4000/NFP6000, determine RX packet/metadata boundary offset */
	if (nn->fw_ver.major >= 2)
		nn->rx_offset = nn_readl(nn, NFP_NET_CFG_RX_OFFSET);
	else
		nn->rx_offset = NFP_NET_RX_OFFSET;

	/* Stash the re-configuration queue away.  First odd queue in TX Bar */
	nn->qcp_cfg = nn->tx_bar + NFP_QCP_QUEUE_ADDR_SZ;

	/* Make sure the FW knows the netdev is supposed to be disabled here */
	nn_writel(nn, NFP_NET_CFG_CTRL, 0);
	nn_writeq(nn, NFP_NET_CFG_TXRS_ENABLE, 0);
	nn_writeq(nn, NFP_NET_CFG_RXRS_ENABLE, 0);
	err = nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_RING |
				   NFP_NET_CFG_UPDATE_GEN);
	if (err)
		return err;

	/* Finalise the netdev setup */
	ether_setup(netdev);
	netdev->netdev_ops = &nfp_net_netdev_ops;
	netdev->watchdog_timeo = msecs_to_jiffies(5 * 1000);

	nfp_net_set_ethtool_ops(netdev);
	nfp_net_irqs_assign(netdev);

	return register_netdev(netdev);
}

/**
 * nfp_net_netdev_clean() - Undo what nfp_net_netdev_init() did.
 * @netdev:      netdev structure
 */
void nfp_net_netdev_clean(struct net_device *netdev)
{
	unregister_netdev(netdev);
}
