// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2015-2018 Netronome Systems, Inc. */

/*
 * nfp_net_common.c
 * Netronome network device driver: Common functions between PF and VF
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 *          Brad Petrus <brad.petrus@netronome.com>
 *          Chris Telfer <chris.telfer@netronome.com>
 */

#include <linux/bitfield.h>
#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/mm.h>
#include <linux/overflow.h>
#include <linux/page_ref.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/msi.h>
#include <linux/ethtool.h>
#include <linux/log2.h>
#include <linux/if_vlan.h>
#include <linux/random.h>
#include <linux/vmalloc.h>
#include <linux/ktime.h>

#include <net/tls.h>
#include <net/vxlan.h>

#include "nfpcore/nfp_nsp.h"
#include "ccm.h"
#include "nfp_app.h"
#include "nfp_net_ctrl.h"
#include "nfp_net.h"
#include "nfp_net_sriov.h"
#include "nfp_port.h"
#include "crypto/crypto.h"
#include "crypto/fw.h"

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

static dma_addr_t nfp_net_dma_map_rx(struct nfp_net_dp *dp, void *frag)
{
	return dma_map_single_attrs(dp->dev, frag + NFP_NET_RX_BUF_HEADROOM,
				    dp->fl_bufsz - NFP_NET_RX_BUF_NON_DATA,
				    dp->rx_dma_dir, DMA_ATTR_SKIP_CPU_SYNC);
}

static void
nfp_net_dma_sync_dev_rx(const struct nfp_net_dp *dp, dma_addr_t dma_addr)
{
	dma_sync_single_for_device(dp->dev, dma_addr,
				   dp->fl_bufsz - NFP_NET_RX_BUF_NON_DATA,
				   dp->rx_dma_dir);
}

static void nfp_net_dma_unmap_rx(struct nfp_net_dp *dp, dma_addr_t dma_addr)
{
	dma_unmap_single_attrs(dp->dev, dma_addr,
			       dp->fl_bufsz - NFP_NET_RX_BUF_NON_DATA,
			       dp->rx_dma_dir, DMA_ATTR_SKIP_CPU_SYNC);
}

static void nfp_net_dma_sync_cpu_rx(struct nfp_net_dp *dp, dma_addr_t dma_addr,
				    unsigned int len)
{
	dma_sync_single_for_cpu(dp->dev, dma_addr - NFP_NET_RX_BUF_HEADROOM,
				len, dp->rx_dma_dir);
}

/* Firmware reconfig
 *
 * Firmware reconfig may take a while so we have two versions of it -
 * synchronous and asynchronous (posted).  All synchronous callers are holding
 * RTNL so we don't have to worry about serializing them.
 */
static void nfp_net_reconfig_start(struct nfp_net *nn, u32 update)
{
	nn_writel(nn, NFP_NET_CFG_UPDATE, update);
	/* ensure update is written before pinging HW */
	nn_pci_flush(nn);
	nfp_qcp_wr_ptr_add(nn->qcp_cfg, 1);
	nn->reconfig_in_progress_update = update;
}

/* Pass 0 as update to run posted reconfigs. */
static void nfp_net_reconfig_start_async(struct nfp_net *nn, u32 update)
{
	update |= nn->reconfig_posted;
	nn->reconfig_posted = 0;

	nfp_net_reconfig_start(nn, update);

	nn->reconfig_timer_active = true;
	mod_timer(&nn->reconfig_timer, jiffies + NFP_NET_POLL_TIMEOUT * HZ);
}

static bool nfp_net_reconfig_check_done(struct nfp_net *nn, bool last_check)
{
	u32 reg;

	reg = nn_readl(nn, NFP_NET_CFG_UPDATE);
	if (reg == 0)
		return true;
	if (reg & NFP_NET_CFG_UPDATE_ERR) {
		nn_err(nn, "Reconfig error (status: 0x%08x update: 0x%08x ctrl: 0x%08x)\n",
		       reg, nn->reconfig_in_progress_update,
		       nn_readl(nn, NFP_NET_CFG_CTRL));
		return true;
	} else if (last_check) {
		nn_err(nn, "Reconfig timeout (status: 0x%08x update: 0x%08x ctrl: 0x%08x)\n",
		       reg, nn->reconfig_in_progress_update,
		       nn_readl(nn, NFP_NET_CFG_CTRL));
		return true;
	}

	return false;
}

static bool __nfp_net_reconfig_wait(struct nfp_net *nn, unsigned long deadline)
{
	bool timed_out = false;
	int i;

	/* Poll update field, waiting for NFP to ack the config.
	 * Do an opportunistic wait-busy loop, afterward sleep.
	 */
	for (i = 0; i < 50; i++) {
		if (nfp_net_reconfig_check_done(nn, false))
			return false;
		udelay(4);
	}

	while (!nfp_net_reconfig_check_done(nn, timed_out)) {
		usleep_range(250, 500);
		timed_out = time_is_before_eq_jiffies(deadline);
	}

	return timed_out;
}

static int nfp_net_reconfig_wait(struct nfp_net *nn, unsigned long deadline)
{
	if (__nfp_net_reconfig_wait(nn, deadline))
		return -EIO;

	if (nn_readl(nn, NFP_NET_CFG_UPDATE) & NFP_NET_CFG_UPDATE_ERR)
		return -EIO;

	return 0;
}

static void nfp_net_reconfig_timer(struct timer_list *t)
{
	struct nfp_net *nn = from_timer(nn, t, reconfig_timer);

	spin_lock_bh(&nn->reconfig_lock);

	nn->reconfig_timer_active = false;

	/* If sync caller is present it will take over from us */
	if (nn->reconfig_sync_present)
		goto done;

	/* Read reconfig status and report errors */
	nfp_net_reconfig_check_done(nn, true);

	if (nn->reconfig_posted)
		nfp_net_reconfig_start_async(nn, 0);
done:
	spin_unlock_bh(&nn->reconfig_lock);
}

/**
 * nfp_net_reconfig_post() - Post async reconfig request
 * @nn:      NFP Net device to reconfigure
 * @update:  The value for the update field in the BAR config
 *
 * Record FW reconfiguration request.  Reconfiguration will be kicked off
 * whenever reconfiguration machinery is idle.  Multiple requests can be
 * merged together!
 */
static void nfp_net_reconfig_post(struct nfp_net *nn, u32 update)
{
	spin_lock_bh(&nn->reconfig_lock);

	/* Sync caller will kick off async reconf when it's done, just post */
	if (nn->reconfig_sync_present) {
		nn->reconfig_posted |= update;
		goto done;
	}

	/* Opportunistically check if the previous command is done */
	if (!nn->reconfig_timer_active ||
	    nfp_net_reconfig_check_done(nn, false))
		nfp_net_reconfig_start_async(nn, update);
	else
		nn->reconfig_posted |= update;
done:
	spin_unlock_bh(&nn->reconfig_lock);
}

static void nfp_net_reconfig_sync_enter(struct nfp_net *nn)
{
	bool cancelled_timer = false;
	u32 pre_posted_requests;

	spin_lock_bh(&nn->reconfig_lock);

	WARN_ON(nn->reconfig_sync_present);
	nn->reconfig_sync_present = true;

	if (nn->reconfig_timer_active) {
		nn->reconfig_timer_active = false;
		cancelled_timer = true;
	}
	pre_posted_requests = nn->reconfig_posted;
	nn->reconfig_posted = 0;

	spin_unlock_bh(&nn->reconfig_lock);

	if (cancelled_timer) {
		del_timer_sync(&nn->reconfig_timer);
		nfp_net_reconfig_wait(nn, nn->reconfig_timer.expires);
	}

	/* Run the posted reconfigs which were issued before we started */
	if (pre_posted_requests) {
		nfp_net_reconfig_start(nn, pre_posted_requests);
		nfp_net_reconfig_wait(nn, jiffies + HZ * NFP_NET_POLL_TIMEOUT);
	}
}

static void nfp_net_reconfig_wait_posted(struct nfp_net *nn)
{
	nfp_net_reconfig_sync_enter(nn);

	spin_lock_bh(&nn->reconfig_lock);
	nn->reconfig_sync_present = false;
	spin_unlock_bh(&nn->reconfig_lock);
}

/**
 * __nfp_net_reconfig() - Reconfigure the firmware
 * @nn:      NFP Net device to reconfigure
 * @update:  The value for the update field in the BAR config
 *
 * Write the update word to the BAR and ping the reconfig queue.  The
 * poll until the firmware has acknowledged the update by zeroing the
 * update word.
 *
 * Return: Negative errno on error, 0 on success
 */
int __nfp_net_reconfig(struct nfp_net *nn, u32 update)
{
	int ret;

	nfp_net_reconfig_sync_enter(nn);

	nfp_net_reconfig_start(nn, update);
	ret = nfp_net_reconfig_wait(nn, jiffies + HZ * NFP_NET_POLL_TIMEOUT);

	spin_lock_bh(&nn->reconfig_lock);

	if (nn->reconfig_posted)
		nfp_net_reconfig_start_async(nn, 0);

	nn->reconfig_sync_present = false;

	spin_unlock_bh(&nn->reconfig_lock);

	return ret;
}

int nfp_net_reconfig(struct nfp_net *nn, u32 update)
{
	int ret;

	nn_ctrl_bar_lock(nn);
	ret = __nfp_net_reconfig(nn, update);
	nn_ctrl_bar_unlock(nn);

	return ret;
}

int nfp_net_mbox_lock(struct nfp_net *nn, unsigned int data_size)
{
	if (nn->tlv_caps.mbox_len < NFP_NET_CFG_MBOX_SIMPLE_VAL + data_size) {
		nn_err(nn, "mailbox too small for %u of data (%u)\n",
		       data_size, nn->tlv_caps.mbox_len);
		return -EIO;
	}

	nn_ctrl_bar_lock(nn);
	return 0;
}

/**
 * nfp_net_mbox_reconfig() - Reconfigure the firmware via the mailbox
 * @nn:        NFP Net device to reconfigure
 * @mbox_cmd:  The value for the mailbox command
 *
 * Helper function for mailbox updates
 *
 * Return: Negative errno on error, 0 on success
 */
int nfp_net_mbox_reconfig(struct nfp_net *nn, u32 mbox_cmd)
{
	u32 mbox = nn->tlv_caps.mbox_off;
	int ret;

	nn_writeq(nn, mbox + NFP_NET_CFG_MBOX_SIMPLE_CMD, mbox_cmd);

	ret = __nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_MBOX);
	if (ret) {
		nn_err(nn, "Mailbox update error\n");
		return ret;
	}

	return -nn_readl(nn, mbox + NFP_NET_CFG_MBOX_SIMPLE_RET);
}

void nfp_net_mbox_reconfig_post(struct nfp_net *nn, u32 mbox_cmd)
{
	u32 mbox = nn->tlv_caps.mbox_off;

	nn_writeq(nn, mbox + NFP_NET_CFG_MBOX_SIMPLE_CMD, mbox_cmd);

	nfp_net_reconfig_post(nn, NFP_NET_CFG_UPDATE_MBOX);
}

int nfp_net_mbox_reconfig_wait_posted(struct nfp_net *nn)
{
	u32 mbox = nn->tlv_caps.mbox_off;

	nfp_net_reconfig_wait_posted(nn);

	return -nn_readl(nn, mbox + NFP_NET_CFG_MBOX_SIMPLE_RET);
}

int nfp_net_mbox_reconfig_and_unlock(struct nfp_net *nn, u32 mbox_cmd)
{
	int ret;

	ret = nfp_net_mbox_reconfig(nn, mbox_cmd);
	nn_ctrl_bar_unlock(nn);
	return ret;
}

/* Interrupt configuration and handling
 */

/**
 * nfp_net_irq_unmask() - Unmask automasked interrupt
 * @nn:       NFP Network structure
 * @entry_nr: MSI-X table entry
 *
 * Clear the ICR for the IRQ entry.
 */
static void nfp_net_irq_unmask(struct nfp_net *nn, unsigned int entry_nr)
{
	nn_writeb(nn, NFP_NET_CFG_ICR(entry_nr), NFP_NET_CFG_ICR_UNMASKED);
	nn_pci_flush(nn);
}

/**
 * nfp_net_irqs_alloc() - allocates MSI-X irqs
 * @pdev:        PCI device structure
 * @irq_entries: Array to be initialized and used to hold the irq entries
 * @min_irqs:    Minimal acceptable number of interrupts
 * @wanted_irqs: Target number of interrupts to allocate
 *
 * Return: Number of irqs obtained or 0 on error.
 */
unsigned int
nfp_net_irqs_alloc(struct pci_dev *pdev, struct msix_entry *irq_entries,
		   unsigned int min_irqs, unsigned int wanted_irqs)
{
	unsigned int i;
	int got_irqs;

	for (i = 0; i < wanted_irqs; i++)
		irq_entries[i].entry = i;

	got_irqs = pci_enable_msix_range(pdev, irq_entries,
					 min_irqs, wanted_irqs);
	if (got_irqs < 0) {
		dev_err(&pdev->dev, "Failed to enable %d-%d MSI-X (err=%d)\n",
			min_irqs, wanted_irqs, got_irqs);
		return 0;
	}

	if (got_irqs < wanted_irqs)
		dev_warn(&pdev->dev, "Unable to allocate %d IRQs got only %d\n",
			 wanted_irqs, got_irqs);

	return got_irqs;
}

/**
 * nfp_net_irqs_assign() - Assign interrupts allocated externally to netdev
 * @nn:		 NFP Network structure
 * @irq_entries: Table of allocated interrupts
 * @n:		 Size of @irq_entries (number of entries to grab)
 *
 * After interrupts are allocated with nfp_net_irqs_alloc() this function
 * should be called to assign them to a specific netdev (port).
 */
void
nfp_net_irqs_assign(struct nfp_net *nn, struct msix_entry *irq_entries,
		    unsigned int n)
{
	struct nfp_net_dp *dp = &nn->dp;

	nn->max_r_vecs = n - NFP_NET_NON_Q_VECTORS;
	dp->num_r_vecs = nn->max_r_vecs;

	memcpy(nn->irq_entries, irq_entries, sizeof(*irq_entries) * n);

	if (dp->num_rx_rings > dp->num_r_vecs ||
	    dp->num_tx_rings > dp->num_r_vecs)
		dev_warn(nn->dp.dev, "More rings (%d,%d) than vectors (%d).\n",
			 dp->num_rx_rings, dp->num_tx_rings,
			 dp->num_r_vecs);

	dp->num_rx_rings = min(dp->num_r_vecs, dp->num_rx_rings);
	dp->num_tx_rings = min(dp->num_r_vecs, dp->num_tx_rings);
	dp->num_stack_tx_rings = dp->num_tx_rings;
}

/**
 * nfp_net_irqs_disable() - Disable interrupts
 * @pdev:        PCI device structure
 *
 * Undoes what @nfp_net_irqs_alloc() does.
 */
void nfp_net_irqs_disable(struct pci_dev *pdev)
{
	pci_disable_msix(pdev);
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

static irqreturn_t nfp_ctrl_irq_rxtx(int irq, void *data)
{
	struct nfp_net_r_vector *r_vec = data;

	tasklet_schedule(&r_vec->tasklet);

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
	if (nn->port)
		set_bit(NFP_PORT_CHANGED, &nn->port->flags);

	if (nn->link_up) {
		netif_carrier_on(nn->dp.netdev);
		netdev_info(nn->dp.netdev, "NIC Link is Up\n");
	} else {
		netif_carrier_off(nn->dp.netdev);
		netdev_info(nn->dp.netdev, "NIC Link is Down\n");
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
	struct msix_entry *entry;

	entry = &nn->irq_entries[NFP_NET_IRQ_LSC_IDX];

	nfp_net_read_link_status(nn);

	nfp_net_irq_unmask(nn, entry->entry);

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
 * @r_vec:    IRQ vector servicing this ring
 * @idx:      Ring index
 * @is_xdp:   Is this an XDP TX ring?
 */
static void
nfp_net_tx_ring_init(struct nfp_net_tx_ring *tx_ring,
		     struct nfp_net_r_vector *r_vec, unsigned int idx,
		     bool is_xdp)
{
	struct nfp_net *nn = r_vec->nfp_net;

	tx_ring->idx = idx;
	tx_ring->r_vec = r_vec;
	tx_ring->is_xdp = is_xdp;
	u64_stats_init(&tx_ring->r_vec->tx_sync);

	tx_ring->qcidx = tx_ring->idx * nn->stride_tx;
	tx_ring->qcp_q = nn->tx_bar + NFP_QCP_QUEUE_OFF(tx_ring->qcidx);
}

/**
 * nfp_net_rx_ring_init() - Fill in the boilerplate for a RX ring
 * @rx_ring:  RX ring structure
 * @r_vec:    IRQ vector servicing this ring
 * @idx:      Ring index
 */
static void
nfp_net_rx_ring_init(struct nfp_net_rx_ring *rx_ring,
		     struct nfp_net_r_vector *r_vec, unsigned int idx)
{
	struct nfp_net *nn = r_vec->nfp_net;

	rx_ring->idx = idx;
	rx_ring->r_vec = r_vec;
	u64_stats_init(&rx_ring->r_vec->rx_sync);

	rx_ring->fl_qcidx = rx_ring->idx * nn->stride_rx;
	rx_ring->qcp_fl = nn->rx_bar + NFP_QCP_QUEUE_OFF(rx_ring->fl_qcidx);
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

	snprintf(name, name_sz, format, nfp_net_name(nn));
	err = request_irq(entry->vector, handler, 0, name, nn);
	if (err) {
		nn_err(nn, "Failed to request IRQ %d (err=%d).\n",
		       entry->vector, err);
		return err;
	}
	nn_writeb(nn, ctrl_offset, entry->entry);
	nfp_net_irq_unmask(nn, entry->entry);

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
	nn_pci_flush(nn);
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
static int nfp_net_tx_full(struct nfp_net_tx_ring *tx_ring, int dcnt)
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
 * @r_vec: per-ring structure
 * @txbuf: Pointer to driver soft TX descriptor
 * @txd: Pointer to HW TX descriptor
 * @skb: Pointer to SKB
 * @md_bytes: Prepend length
 *
 * Set up Tx descriptor for LSO, do nothing for non-LSO skbs.
 * Return error on packet header greater than maximum supported LSO header size.
 */
static void nfp_net_tx_tso(struct nfp_net_r_vector *r_vec,
			   struct nfp_net_tx_buf *txbuf,
			   struct nfp_net_tx_desc *txd, struct sk_buff *skb,
			   u32 md_bytes)
{
	u32 l3_offset, l4_offset, hdrlen;
	u16 mss;

	if (!skb_is_gso(skb))
		return;

	if (!skb->encapsulation) {
		l3_offset = skb_network_offset(skb);
		l4_offset = skb_transport_offset(skb);
		hdrlen = skb_transport_offset(skb) + tcp_hdrlen(skb);
	} else {
		l3_offset = skb_inner_network_offset(skb);
		l4_offset = skb_inner_transport_offset(skb);
		hdrlen = skb_inner_transport_header(skb) - skb->data +
			inner_tcp_hdrlen(skb);
	}

	txbuf->pkt_cnt = skb_shinfo(skb)->gso_segs;
	txbuf->real_len += hdrlen * (txbuf->pkt_cnt - 1);

	mss = skb_shinfo(skb)->gso_size & PCIE_DESC_TX_MSS_MASK;
	txd->l3_offset = l3_offset - md_bytes;
	txd->l4_offset = l4_offset - md_bytes;
	txd->lso_hdrlen = hdrlen - md_bytes;
	txd->mss = cpu_to_le16(mss);
	txd->flags |= PCIE_DESC_TX_LSO;

	u64_stats_update_begin(&r_vec->tx_sync);
	r_vec->tx_lso++;
	u64_stats_update_end(&r_vec->tx_sync);
}

/**
 * nfp_net_tx_csum() - Set TX CSUM offload flags in TX descriptor
 * @dp:  NFP Net data path struct
 * @r_vec: per-ring structure
 * @txbuf: Pointer to driver soft TX descriptor
 * @txd: Pointer to TX descriptor
 * @skb: Pointer to SKB
 *
 * This function sets the TX checksum flags in the TX descriptor based
 * on the configuration and the protocol of the packet to be transmitted.
 */
static void nfp_net_tx_csum(struct nfp_net_dp *dp,
			    struct nfp_net_r_vector *r_vec,
			    struct nfp_net_tx_buf *txbuf,
			    struct nfp_net_tx_desc *txd, struct sk_buff *skb)
{
	struct ipv6hdr *ipv6h;
	struct iphdr *iph;
	u8 l4_hdr;

	if (!(dp->ctrl & NFP_NET_CFG_CTRL_TXCSUM))
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
		nn_dp_warn(dp, "partial checksum but ipv=%x!\n", iph->version);
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
		nn_dp_warn(dp, "partial checksum but l4 proto=%x!\n", l4_hdr);
		return;
	}

	u64_stats_update_begin(&r_vec->tx_sync);
	if (skb->encapsulation)
		r_vec->hw_csum_tx_inner += txbuf->pkt_cnt;
	else
		r_vec->hw_csum_tx += txbuf->pkt_cnt;
	u64_stats_update_end(&r_vec->tx_sync);
}

static struct sk_buff *
nfp_net_tls_tx(struct nfp_net_dp *dp, struct nfp_net_r_vector *r_vec,
	       struct sk_buff *skb, u64 *tls_handle, int *nr_frags)
{
#ifdef CONFIG_TLS_DEVICE
	struct nfp_net_tls_offload_ctx *ntls;
	struct sk_buff *nskb;
	bool resync_pending;
	u32 datalen, seq;

	if (likely(!dp->ktls_tx))
		return skb;
	if (!skb->sk || !tls_is_sk_tx_device_offloaded(skb->sk))
		return skb;

	datalen = skb->len - (skb_transport_offset(skb) + tcp_hdrlen(skb));
	seq = ntohl(tcp_hdr(skb)->seq);
	ntls = tls_driver_ctx(skb->sk, TLS_OFFLOAD_CTX_DIR_TX);
	resync_pending = tls_offload_tx_resync_pending(skb->sk);
	if (unlikely(resync_pending || ntls->next_seq != seq)) {
		/* Pure ACK out of order already */
		if (!datalen)
			return skb;

		u64_stats_update_begin(&r_vec->tx_sync);
		r_vec->tls_tx_fallback++;
		u64_stats_update_end(&r_vec->tx_sync);

		nskb = tls_encrypt_skb(skb);
		if (!nskb) {
			u64_stats_update_begin(&r_vec->tx_sync);
			r_vec->tls_tx_no_fallback++;
			u64_stats_update_end(&r_vec->tx_sync);
			return NULL;
		}
		/* encryption wasn't necessary */
		if (nskb == skb)
			return skb;
		/* we don't re-check ring space */
		if (unlikely(skb_is_nonlinear(nskb))) {
			nn_dp_warn(dp, "tls_encrypt_skb() produced fragmented frame\n");
			u64_stats_update_begin(&r_vec->tx_sync);
			r_vec->tx_errors++;
			u64_stats_update_end(&r_vec->tx_sync);
			dev_kfree_skb_any(nskb);
			return NULL;
		}

		/* jump forward, a TX may have gotten lost, need to sync TX */
		if (!resync_pending && seq - ntls->next_seq < U32_MAX / 4)
			tls_offload_tx_resync_request(nskb->sk, seq,
						      ntls->next_seq);

		*nr_frags = 0;
		return nskb;
	}

	if (datalen) {
		u64_stats_update_begin(&r_vec->tx_sync);
		if (!skb_is_gso(skb))
			r_vec->hw_tls_tx++;
		else
			r_vec->hw_tls_tx += skb_shinfo(skb)->gso_segs;
		u64_stats_update_end(&r_vec->tx_sync);
	}

	memcpy(tls_handle, ntls->fw_handle, sizeof(ntls->fw_handle));
	ntls->next_seq += datalen;
#endif
	return skb;
}

static void nfp_net_tls_tx_undo(struct sk_buff *skb, u64 tls_handle)
{
#ifdef CONFIG_TLS_DEVICE
	struct nfp_net_tls_offload_ctx *ntls;
	u32 datalen, seq;

	if (!tls_handle)
		return;
	if (WARN_ON_ONCE(!skb->sk || !tls_is_sk_tx_device_offloaded(skb->sk)))
		return;

	datalen = skb->len - (skb_transport_offset(skb) + tcp_hdrlen(skb));
	seq = ntohl(tcp_hdr(skb)->seq);

	ntls = tls_driver_ctx(skb->sk, TLS_OFFLOAD_CTX_DIR_TX);
	if (ntls->next_seq == seq + datalen)
		ntls->next_seq = seq;
	else
		WARN_ON_ONCE(1);
#endif
}

static void nfp_net_tx_xmit_more_flush(struct nfp_net_tx_ring *tx_ring)
{
	wmb();
	nfp_qcp_wr_ptr_add(tx_ring->qcp_q, tx_ring->wr_ptr_add);
	tx_ring->wr_ptr_add = 0;
}

static int nfp_net_prep_tx_meta(struct sk_buff *skb, u64 tls_handle)
{
	struct metadata_dst *md_dst = skb_metadata_dst(skb);
	unsigned char *data;
	u32 meta_id = 0;
	int md_bytes;

	if (likely(!md_dst && !tls_handle))
		return 0;
	if (unlikely(md_dst && md_dst->type != METADATA_HW_PORT_MUX)) {
		if (!tls_handle)
			return 0;
		md_dst = NULL;
	}

	md_bytes = 4 + !!md_dst * 4 + !!tls_handle * 8;

	if (unlikely(skb_cow_head(skb, md_bytes)))
		return -ENOMEM;

	meta_id = 0;
	data = skb_push(skb, md_bytes) + md_bytes;
	if (md_dst) {
		data -= 4;
		put_unaligned_be32(md_dst->u.port_info.port_id, data);
		meta_id = NFP_NET_META_PORTID;
	}
	if (tls_handle) {
		/* conn handle is opaque, we just use u64 to be able to quickly
		 * compare it to zero
		 */
		data -= 8;
		memcpy(data, &tls_handle, sizeof(tls_handle));
		meta_id <<= NFP_NET_META_FIELD_SIZE;
		meta_id |= NFP_NET_META_CONN_HANDLE;
	}

	data -= 4;
	put_unaligned_be32(meta_id, data);

	return md_bytes;
}

/**
 * nfp_net_tx() - Main transmit entry point
 * @skb:    SKB to transmit
 * @netdev: netdev structure
 *
 * Return: NETDEV_TX_OK on success.
 */
static netdev_tx_t nfp_net_tx(struct sk_buff *skb, struct net_device *netdev)
{
	struct nfp_net *nn = netdev_priv(netdev);
	const skb_frag_t *frag;
	int f, nr_frags, wr_idx, md_bytes;
	struct nfp_net_tx_ring *tx_ring;
	struct nfp_net_r_vector *r_vec;
	struct nfp_net_tx_buf *txbuf;
	struct nfp_net_tx_desc *txd;
	struct netdev_queue *nd_q;
	struct nfp_net_dp *dp;
	dma_addr_t dma_addr;
	unsigned int fsize;
	u64 tls_handle = 0;
	u16 qidx;

	dp = &nn->dp;
	qidx = skb_get_queue_mapping(skb);
	tx_ring = &dp->tx_rings[qidx];
	r_vec = tx_ring->r_vec;

	nr_frags = skb_shinfo(skb)->nr_frags;

	if (unlikely(nfp_net_tx_full(tx_ring, nr_frags + 1))) {
		nn_dp_warn(dp, "TX ring %d busy. wrp=%u rdp=%u\n",
			   qidx, tx_ring->wr_p, tx_ring->rd_p);
		nd_q = netdev_get_tx_queue(dp->netdev, qidx);
		netif_tx_stop_queue(nd_q);
		nfp_net_tx_xmit_more_flush(tx_ring);
		u64_stats_update_begin(&r_vec->tx_sync);
		r_vec->tx_busy++;
		u64_stats_update_end(&r_vec->tx_sync);
		return NETDEV_TX_BUSY;
	}

	skb = nfp_net_tls_tx(dp, r_vec, skb, &tls_handle, &nr_frags);
	if (unlikely(!skb)) {
		nfp_net_tx_xmit_more_flush(tx_ring);
		return NETDEV_TX_OK;
	}

	md_bytes = nfp_net_prep_tx_meta(skb, tls_handle);
	if (unlikely(md_bytes < 0))
		goto err_flush;

	/* Start with the head skbuf */
	dma_addr = dma_map_single(dp->dev, skb->data, skb_headlen(skb),
				  DMA_TO_DEVICE);
	if (dma_mapping_error(dp->dev, dma_addr))
		goto err_dma_err;

	wr_idx = D_IDX(tx_ring, tx_ring->wr_p);

	/* Stash the soft descriptor of the head then initialize it */
	txbuf = &tx_ring->txbufs[wr_idx];
	txbuf->skb = skb;
	txbuf->dma_addr = dma_addr;
	txbuf->fidx = -1;
	txbuf->pkt_cnt = 1;
	txbuf->real_len = skb->len;

	/* Build TX descriptor */
	txd = &tx_ring->txds[wr_idx];
	txd->offset_eop = (nr_frags ? 0 : PCIE_DESC_TX_EOP) | md_bytes;
	txd->dma_len = cpu_to_le16(skb_headlen(skb));
	nfp_desc_set_dma_addr(txd, dma_addr);
	txd->data_len = cpu_to_le16(skb->len);

	txd->flags = 0;
	txd->mss = 0;
	txd->lso_hdrlen = 0;

	/* Do not reorder - tso may adjust pkt cnt, vlan may override fields */
	nfp_net_tx_tso(r_vec, txbuf, txd, skb, md_bytes);
	nfp_net_tx_csum(dp, r_vec, txbuf, txd, skb);
	if (skb_vlan_tag_present(skb) && dp->ctrl & NFP_NET_CFG_CTRL_TXVLAN) {
		txd->flags |= PCIE_DESC_TX_VLAN;
		txd->vlan = cpu_to_le16(skb_vlan_tag_get(skb));
	}

	/* Gather DMA */
	if (nr_frags > 0) {
		__le64 second_half;

		/* all descs must match except for in addr, length and eop */
		second_half = txd->vals8[1];

		for (f = 0; f < nr_frags; f++) {
			frag = &skb_shinfo(skb)->frags[f];
			fsize = skb_frag_size(frag);

			dma_addr = skb_frag_dma_map(dp->dev, frag, 0,
						    fsize, DMA_TO_DEVICE);
			if (dma_mapping_error(dp->dev, dma_addr))
				goto err_unmap;

			wr_idx = D_IDX(tx_ring, wr_idx + 1);
			tx_ring->txbufs[wr_idx].skb = skb;
			tx_ring->txbufs[wr_idx].dma_addr = dma_addr;
			tx_ring->txbufs[wr_idx].fidx = f;

			txd = &tx_ring->txds[wr_idx];
			txd->dma_len = cpu_to_le16(fsize);
			nfp_desc_set_dma_addr(txd, dma_addr);
			txd->offset_eop = md_bytes |
				((f == nr_frags - 1) ? PCIE_DESC_TX_EOP : 0);
			txd->vals8[1] = second_half;
		}

		u64_stats_update_begin(&r_vec->tx_sync);
		r_vec->tx_gather++;
		u64_stats_update_end(&r_vec->tx_sync);
	}

	skb_tx_timestamp(skb);

	nd_q = netdev_get_tx_queue(dp->netdev, tx_ring->idx);

	tx_ring->wr_p += nr_frags + 1;
	if (nfp_net_tx_ring_should_stop(tx_ring))
		nfp_net_tx_ring_stop(nd_q, tx_ring);

	tx_ring->wr_ptr_add += nr_frags + 1;
	if (__netdev_tx_sent_queue(nd_q, txbuf->real_len, netdev_xmit_more()))
		nfp_net_tx_xmit_more_flush(tx_ring);

	return NETDEV_TX_OK;

err_unmap:
	while (--f >= 0) {
		frag = &skb_shinfo(skb)->frags[f];
		dma_unmap_page(dp->dev, tx_ring->txbufs[wr_idx].dma_addr,
			       skb_frag_size(frag), DMA_TO_DEVICE);
		tx_ring->txbufs[wr_idx].skb = NULL;
		tx_ring->txbufs[wr_idx].dma_addr = 0;
		tx_ring->txbufs[wr_idx].fidx = -2;
		wr_idx = wr_idx - 1;
		if (wr_idx < 0)
			wr_idx += tx_ring->cnt;
	}
	dma_unmap_single(dp->dev, tx_ring->txbufs[wr_idx].dma_addr,
			 skb_headlen(skb), DMA_TO_DEVICE);
	tx_ring->txbufs[wr_idx].skb = NULL;
	tx_ring->txbufs[wr_idx].dma_addr = 0;
	tx_ring->txbufs[wr_idx].fidx = -2;
err_dma_err:
	nn_dp_warn(dp, "Failed to map DMA TX buffer\n");
err_flush:
	nfp_net_tx_xmit_more_flush(tx_ring);
	u64_stats_update_begin(&r_vec->tx_sync);
	r_vec->tx_errors++;
	u64_stats_update_end(&r_vec->tx_sync);
	nfp_net_tls_tx_undo(skb, tls_handle);
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

/**
 * nfp_net_tx_complete() - Handled completed TX packets
 * @tx_ring:	TX ring structure
 * @budget:	NAPI budget (only used as bool to determine if in NAPI context)
 */
static void nfp_net_tx_complete(struct nfp_net_tx_ring *tx_ring, int budget)
{
	struct nfp_net_r_vector *r_vec = tx_ring->r_vec;
	struct nfp_net_dp *dp = &r_vec->nfp_net->dp;
	struct netdev_queue *nd_q;
	u32 done_pkts = 0, done_bytes = 0;
	u32 qcp_rd_p;
	int todo;

	if (tx_ring->wr_p == tx_ring->rd_p)
		return;

	/* Work out how many descriptors have been transmitted */
	qcp_rd_p = nfp_qcp_rd_ptr_read(tx_ring->qcp_q);

	if (qcp_rd_p == tx_ring->qcp_rd_p)
		return;

	todo = D_IDX(tx_ring, qcp_rd_p - tx_ring->qcp_rd_p);

	while (todo--) {
		const skb_frag_t *frag;
		struct nfp_net_tx_buf *tx_buf;
		struct sk_buff *skb;
		int fidx, nr_frags;
		int idx;

		idx = D_IDX(tx_ring, tx_ring->rd_p++);
		tx_buf = &tx_ring->txbufs[idx];

		skb = tx_buf->skb;
		if (!skb)
			continue;

		nr_frags = skb_shinfo(skb)->nr_frags;
		fidx = tx_buf->fidx;

		if (fidx == -1) {
			/* unmap head */
			dma_unmap_single(dp->dev, tx_buf->dma_addr,
					 skb_headlen(skb), DMA_TO_DEVICE);

			done_pkts += tx_buf->pkt_cnt;
			done_bytes += tx_buf->real_len;
		} else {
			/* unmap fragment */
			frag = &skb_shinfo(skb)->frags[fidx];
			dma_unmap_page(dp->dev, tx_buf->dma_addr,
				       skb_frag_size(frag), DMA_TO_DEVICE);
		}

		/* check for last gather fragment */
		if (fidx == nr_frags - 1)
			napi_consume_skb(skb, budget);

		tx_buf->dma_addr = 0;
		tx_buf->skb = NULL;
		tx_buf->fidx = -2;
	}

	tx_ring->qcp_rd_p = qcp_rd_p;

	u64_stats_update_begin(&r_vec->tx_sync);
	r_vec->tx_bytes += done_bytes;
	r_vec->tx_pkts += done_pkts;
	u64_stats_update_end(&r_vec->tx_sync);

	if (!dp->netdev)
		return;

	nd_q = netdev_get_tx_queue(dp->netdev, tx_ring->idx);
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

static bool nfp_net_xdp_complete(struct nfp_net_tx_ring *tx_ring)
{
	struct nfp_net_r_vector *r_vec = tx_ring->r_vec;
	u32 done_pkts = 0, done_bytes = 0;
	bool done_all;
	int idx, todo;
	u32 qcp_rd_p;

	/* Work out how many descriptors have been transmitted */
	qcp_rd_p = nfp_qcp_rd_ptr_read(tx_ring->qcp_q);

	if (qcp_rd_p == tx_ring->qcp_rd_p)
		return true;

	todo = D_IDX(tx_ring, qcp_rd_p - tx_ring->qcp_rd_p);

	done_all = todo <= NFP_NET_XDP_MAX_COMPLETE;
	todo = min(todo, NFP_NET_XDP_MAX_COMPLETE);

	tx_ring->qcp_rd_p = D_IDX(tx_ring, tx_ring->qcp_rd_p + todo);

	done_pkts = todo;
	while (todo--) {
		idx = D_IDX(tx_ring, tx_ring->rd_p);
		tx_ring->rd_p++;

		done_bytes += tx_ring->txbufs[idx].real_len;
	}

	u64_stats_update_begin(&r_vec->tx_sync);
	r_vec->tx_bytes += done_bytes;
	r_vec->tx_pkts += done_pkts;
	u64_stats_update_end(&r_vec->tx_sync);

	WARN_ONCE(tx_ring->wr_p - tx_ring->rd_p > tx_ring->cnt,
		  "XDP TX ring corruption rd_p=%u wr_p=%u cnt=%u\n",
		  tx_ring->rd_p, tx_ring->wr_p, tx_ring->cnt);

	return done_all;
}

/**
 * nfp_net_tx_ring_reset() - Free any untransmitted buffers and reset pointers
 * @dp:		NFP Net data path struct
 * @tx_ring:	TX ring structure
 *
 * Assumes that the device is stopped, must be idempotent.
 */
static void
nfp_net_tx_ring_reset(struct nfp_net_dp *dp, struct nfp_net_tx_ring *tx_ring)
{
	const skb_frag_t *frag;
	struct netdev_queue *nd_q;

	while (!tx_ring->is_xdp && tx_ring->rd_p != tx_ring->wr_p) {
		struct nfp_net_tx_buf *tx_buf;
		struct sk_buff *skb;
		int idx, nr_frags;

		idx = D_IDX(tx_ring, tx_ring->rd_p);
		tx_buf = &tx_ring->txbufs[idx];

		skb = tx_ring->txbufs[idx].skb;
		nr_frags = skb_shinfo(skb)->nr_frags;

		if (tx_buf->fidx == -1) {
			/* unmap head */
			dma_unmap_single(dp->dev, tx_buf->dma_addr,
					 skb_headlen(skb), DMA_TO_DEVICE);
		} else {
			/* unmap fragment */
			frag = &skb_shinfo(skb)->frags[tx_buf->fidx];
			dma_unmap_page(dp->dev, tx_buf->dma_addr,
				       skb_frag_size(frag), DMA_TO_DEVICE);
		}

		/* check for last gather fragment */
		if (tx_buf->fidx == nr_frags - 1)
			dev_kfree_skb_any(skb);

		tx_buf->dma_addr = 0;
		tx_buf->skb = NULL;
		tx_buf->fidx = -2;

		tx_ring->qcp_rd_p++;
		tx_ring->rd_p++;
	}

	memset(tx_ring->txds, 0, tx_ring->size);
	tx_ring->wr_p = 0;
	tx_ring->rd_p = 0;
	tx_ring->qcp_rd_p = 0;
	tx_ring->wr_ptr_add = 0;

	if (tx_ring->is_xdp || !dp->netdev)
		return;

	nd_q = netdev_get_tx_queue(dp->netdev, tx_ring->idx);
	netdev_tx_reset_queue(nd_q);
}

static void nfp_net_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct nfp_net *nn = netdev_priv(netdev);

	nn_warn(nn, "TX watchdog timeout on ring: %u\n", txqueue);
}

/* Receive processing
 */
static unsigned int
nfp_net_calc_fl_bufsz(struct nfp_net_dp *dp)
{
	unsigned int fl_bufsz;

	fl_bufsz = NFP_NET_RX_BUF_HEADROOM;
	fl_bufsz += dp->rx_dma_off;
	if (dp->rx_offset == NFP_NET_CFG_RX_OFFSET_DYNAMIC)
		fl_bufsz += NFP_NET_MAX_PREPEND;
	else
		fl_bufsz += dp->rx_offset;
	fl_bufsz += ETH_HLEN + VLAN_HLEN * 2 + dp->mtu;

	fl_bufsz = SKB_DATA_ALIGN(fl_bufsz);
	fl_bufsz += SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	return fl_bufsz;
}

static void
nfp_net_free_frag(void *frag, bool xdp)
{
	if (!xdp)
		skb_free_frag(frag);
	else
		__free_page(virt_to_page(frag));
}

/**
 * nfp_net_rx_alloc_one() - Allocate and map page frag for RX
 * @dp:		NFP Net data path struct
 * @dma_addr:	Pointer to storage for DMA address (output param)
 *
 * This function will allcate a new page frag, map it for DMA.
 *
 * Return: allocated page frag or NULL on failure.
 */
static void *nfp_net_rx_alloc_one(struct nfp_net_dp *dp, dma_addr_t *dma_addr)
{
	void *frag;

	if (!dp->xdp_prog) {
		frag = netdev_alloc_frag(dp->fl_bufsz);
	} else {
		struct page *page;

		page = alloc_page(GFP_KERNEL);
		frag = page ? page_address(page) : NULL;
	}
	if (!frag) {
		nn_dp_warn(dp, "Failed to alloc receive page frag\n");
		return NULL;
	}

	*dma_addr = nfp_net_dma_map_rx(dp, frag);
	if (dma_mapping_error(dp->dev, *dma_addr)) {
		nfp_net_free_frag(frag, dp->xdp_prog);
		nn_dp_warn(dp, "Failed to map DMA RX buffer\n");
		return NULL;
	}

	return frag;
}

static void *nfp_net_napi_alloc_one(struct nfp_net_dp *dp, dma_addr_t *dma_addr)
{
	void *frag;

	if (!dp->xdp_prog) {
		frag = napi_alloc_frag(dp->fl_bufsz);
		if (unlikely(!frag))
			return NULL;
	} else {
		struct page *page;

		page = dev_alloc_page();
		if (unlikely(!page))
			return NULL;
		frag = page_address(page);
	}

	*dma_addr = nfp_net_dma_map_rx(dp, frag);
	if (dma_mapping_error(dp->dev, *dma_addr)) {
		nfp_net_free_frag(frag, dp->xdp_prog);
		nn_dp_warn(dp, "Failed to map DMA RX buffer\n");
		return NULL;
	}

	return frag;
}

/**
 * nfp_net_rx_give_one() - Put mapped skb on the software and hardware rings
 * @dp:		NFP Net data path struct
 * @rx_ring:	RX ring structure
 * @frag:	page fragment buffer
 * @dma_addr:	DMA address of skb mapping
 */
static void nfp_net_rx_give_one(const struct nfp_net_dp *dp,
				struct nfp_net_rx_ring *rx_ring,
				void *frag, dma_addr_t dma_addr)
{
	unsigned int wr_idx;

	wr_idx = D_IDX(rx_ring, rx_ring->wr_p);

	nfp_net_dma_sync_dev_rx(dp, dma_addr);

	/* Stash SKB and DMA address away */
	rx_ring->rxbufs[wr_idx].frag = frag;
	rx_ring->rxbufs[wr_idx].dma_addr = dma_addr;

	/* Fill freelist descriptor */
	rx_ring->rxds[wr_idx].fld.reserved = 0;
	rx_ring->rxds[wr_idx].fld.meta_len_dd = 0;
	nfp_desc_set_dma_addr(&rx_ring->rxds[wr_idx].fld,
			      dma_addr + dp->rx_dma_off);

	rx_ring->wr_p++;
	if (!(rx_ring->wr_p % NFP_NET_FL_BATCH)) {
		/* Update write pointer of the freelist queue. Make
		 * sure all writes are flushed before telling the hardware.
		 */
		wmb();
		nfp_qcp_wr_ptr_add(rx_ring->qcp_fl, NFP_NET_FL_BATCH);
	}
}

/**
 * nfp_net_rx_ring_reset() - Reflect in SW state of freelist after disable
 * @rx_ring:	RX ring structure
 *
 * Assumes that the device is stopped, must be idempotent.
 */
static void nfp_net_rx_ring_reset(struct nfp_net_rx_ring *rx_ring)
{
	unsigned int wr_idx, last_idx;

	/* wr_p == rd_p means ring was never fed FL bufs.  RX rings are always
	 * kept at cnt - 1 FL bufs.
	 */
	if (rx_ring->wr_p == 0 && rx_ring->rd_p == 0)
		return;

	/* Move the empty entry to the end of the list */
	wr_idx = D_IDX(rx_ring, rx_ring->wr_p);
	last_idx = rx_ring->cnt - 1;
	rx_ring->rxbufs[wr_idx].dma_addr = rx_ring->rxbufs[last_idx].dma_addr;
	rx_ring->rxbufs[wr_idx].frag = rx_ring->rxbufs[last_idx].frag;
	rx_ring->rxbufs[last_idx].dma_addr = 0;
	rx_ring->rxbufs[last_idx].frag = NULL;

	memset(rx_ring->rxds, 0, rx_ring->size);
	rx_ring->wr_p = 0;
	rx_ring->rd_p = 0;
}

/**
 * nfp_net_rx_ring_bufs_free() - Free any buffers currently on the RX ring
 * @dp:		NFP Net data path struct
 * @rx_ring:	RX ring to remove buffers from
 *
 * Assumes that the device is stopped and buffers are in [0, ring->cnt - 1)
 * entries.  After device is disabled nfp_net_rx_ring_reset() must be called
 * to restore required ring geometry.
 */
static void
nfp_net_rx_ring_bufs_free(struct nfp_net_dp *dp,
			  struct nfp_net_rx_ring *rx_ring)
{
	unsigned int i;

	for (i = 0; i < rx_ring->cnt - 1; i++) {
		/* NULL skb can only happen when initial filling of the ring
		 * fails to allocate enough buffers and calls here to free
		 * already allocated ones.
		 */
		if (!rx_ring->rxbufs[i].frag)
			continue;

		nfp_net_dma_unmap_rx(dp, rx_ring->rxbufs[i].dma_addr);
		nfp_net_free_frag(rx_ring->rxbufs[i].frag, dp->xdp_prog);
		rx_ring->rxbufs[i].dma_addr = 0;
		rx_ring->rxbufs[i].frag = NULL;
	}
}

/**
 * nfp_net_rx_ring_bufs_alloc() - Fill RX ring with buffers (don't give to FW)
 * @dp:		NFP Net data path struct
 * @rx_ring:	RX ring to remove buffers from
 */
static int
nfp_net_rx_ring_bufs_alloc(struct nfp_net_dp *dp,
			   struct nfp_net_rx_ring *rx_ring)
{
	struct nfp_net_rx_buf *rxbufs;
	unsigned int i;

	rxbufs = rx_ring->rxbufs;

	for (i = 0; i < rx_ring->cnt - 1; i++) {
		rxbufs[i].frag = nfp_net_rx_alloc_one(dp, &rxbufs[i].dma_addr);
		if (!rxbufs[i].frag) {
			nfp_net_rx_ring_bufs_free(dp, rx_ring);
			return -ENOMEM;
		}
	}

	return 0;
}

/**
 * nfp_net_rx_ring_fill_freelist() - Give buffers from the ring to FW
 * @dp:	     NFP Net data path struct
 * @rx_ring: RX ring to fill
 */
static void
nfp_net_rx_ring_fill_freelist(struct nfp_net_dp *dp,
			      struct nfp_net_rx_ring *rx_ring)
{
	unsigned int i;

	for (i = 0; i < rx_ring->cnt - 1; i++)
		nfp_net_rx_give_one(dp, rx_ring, rx_ring->rxbufs[i].frag,
				    rx_ring->rxbufs[i].dma_addr);
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
 * @dp:  NFP Net data path struct
 * @r_vec: per-ring structure
 * @rxd: Pointer to RX descriptor
 * @meta: Parsed metadata prepend
 * @skb: Pointer to SKB
 */
static void nfp_net_rx_csum(struct nfp_net_dp *dp,
			    struct nfp_net_r_vector *r_vec,
			    struct nfp_net_rx_desc *rxd,
			    struct nfp_meta_parsed *meta, struct sk_buff *skb)
{
	skb_checksum_none_assert(skb);

	if (!(dp->netdev->features & NETIF_F_RXCSUM))
		return;

	if (meta->csum_type) {
		skb->ip_summed = meta->csum_type;
		skb->csum = meta->csum;
		u64_stats_update_begin(&r_vec->rx_sync);
		r_vec->hw_csum_rx_complete++;
		u64_stats_update_end(&r_vec->rx_sync);
		return;
	}

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

static void
nfp_net_set_hash(struct net_device *netdev, struct nfp_meta_parsed *meta,
		 unsigned int type, __be32 *hash)
{
	if (!(netdev->features & NETIF_F_RXHASH))
		return;

	switch (type) {
	case NFP_NET_RSS_IPV4:
	case NFP_NET_RSS_IPV6:
	case NFP_NET_RSS_IPV6_EX:
		meta->hash_type = PKT_HASH_TYPE_L3;
		break;
	default:
		meta->hash_type = PKT_HASH_TYPE_L4;
		break;
	}

	meta->hash = get_unaligned_be32(hash);
}

static void
nfp_net_set_hash_desc(struct net_device *netdev, struct nfp_meta_parsed *meta,
		      void *data, struct nfp_net_rx_desc *rxd)
{
	struct nfp_net_rx_hash *rx_hash = data;

	if (!(rxd->rxd.flags & PCIE_DESC_RX_RSS))
		return;

	nfp_net_set_hash(netdev, meta, get_unaligned_be32(&rx_hash->hash_type),
			 &rx_hash->hash);
}

static bool
nfp_net_parse_meta(struct net_device *netdev, struct nfp_meta_parsed *meta,
		   void *data, void *pkt, unsigned int pkt_len, int meta_len)
{
	u32 meta_info;

	meta_info = get_unaligned_be32(data);
	data += 4;

	while (meta_info) {
		switch (meta_info & NFP_NET_META_FIELD_MASK) {
		case NFP_NET_META_HASH:
			meta_info >>= NFP_NET_META_FIELD_SIZE;
			nfp_net_set_hash(netdev, meta,
					 meta_info & NFP_NET_META_FIELD_MASK,
					 (__be32 *)data);
			data += 4;
			break;
		case NFP_NET_META_MARK:
			meta->mark = get_unaligned_be32(data);
			data += 4;
			break;
		case NFP_NET_META_PORTID:
			meta->portid = get_unaligned_be32(data);
			data += 4;
			break;
		case NFP_NET_META_CSUM:
			meta->csum_type = CHECKSUM_COMPLETE;
			meta->csum =
				(__force __wsum)__get_unaligned_cpu32(data);
			data += 4;
			break;
		case NFP_NET_META_RESYNC_INFO:
			if (nfp_net_tls_rx_resync_req(netdev, data, pkt,
						      pkt_len))
				return NULL;
			data += sizeof(struct nfp_net_tls_resync_req);
			break;
		default:
			return true;
		}

		meta_info >>= NFP_NET_META_FIELD_SIZE;
	}

	return data != pkt;
}

static void
nfp_net_rx_drop(const struct nfp_net_dp *dp, struct nfp_net_r_vector *r_vec,
		struct nfp_net_rx_ring *rx_ring, struct nfp_net_rx_buf *rxbuf,
		struct sk_buff *skb)
{
	u64_stats_update_begin(&r_vec->rx_sync);
	r_vec->rx_drops++;
	/* If we have both skb and rxbuf the replacement buffer allocation
	 * must have failed, count this as an alloc failure.
	 */
	if (skb && rxbuf)
		r_vec->rx_replace_buf_alloc_fail++;
	u64_stats_update_end(&r_vec->rx_sync);

	/* skb is build based on the frag, free_skb() would free the frag
	 * so to be able to reuse it we need an extra ref.
	 */
	if (skb && rxbuf && skb->head == rxbuf->frag)
		page_ref_inc(virt_to_head_page(rxbuf->frag));
	if (rxbuf)
		nfp_net_rx_give_one(dp, rx_ring, rxbuf->frag, rxbuf->dma_addr);
	if (skb)
		dev_kfree_skb_any(skb);
}

static bool
nfp_net_tx_xdp_buf(struct nfp_net_dp *dp, struct nfp_net_rx_ring *rx_ring,
		   struct nfp_net_tx_ring *tx_ring,
		   struct nfp_net_rx_buf *rxbuf, unsigned int dma_off,
		   unsigned int pkt_len, bool *completed)
{
	unsigned int dma_map_sz = dp->fl_bufsz - NFP_NET_RX_BUF_NON_DATA;
	struct nfp_net_tx_buf *txbuf;
	struct nfp_net_tx_desc *txd;
	int wr_idx;

	/* Reject if xdp_adjust_tail grow packet beyond DMA area */
	if (pkt_len + dma_off > dma_map_sz)
		return false;

	if (unlikely(nfp_net_tx_full(tx_ring, 1))) {
		if (!*completed) {
			nfp_net_xdp_complete(tx_ring);
			*completed = true;
		}

		if (unlikely(nfp_net_tx_full(tx_ring, 1))) {
			nfp_net_rx_drop(dp, rx_ring->r_vec, rx_ring, rxbuf,
					NULL);
			return false;
		}
	}

	wr_idx = D_IDX(tx_ring, tx_ring->wr_p);

	/* Stash the soft descriptor of the head then initialize it */
	txbuf = &tx_ring->txbufs[wr_idx];

	nfp_net_rx_give_one(dp, rx_ring, txbuf->frag, txbuf->dma_addr);

	txbuf->frag = rxbuf->frag;
	txbuf->dma_addr = rxbuf->dma_addr;
	txbuf->fidx = -1;
	txbuf->pkt_cnt = 1;
	txbuf->real_len = pkt_len;

	dma_sync_single_for_device(dp->dev, rxbuf->dma_addr + dma_off,
				   pkt_len, DMA_BIDIRECTIONAL);

	/* Build TX descriptor */
	txd = &tx_ring->txds[wr_idx];
	txd->offset_eop = PCIE_DESC_TX_EOP;
	txd->dma_len = cpu_to_le16(pkt_len);
	nfp_desc_set_dma_addr(txd, rxbuf->dma_addr + dma_off);
	txd->data_len = cpu_to_le16(pkt_len);

	txd->flags = 0;
	txd->mss = 0;
	txd->lso_hdrlen = 0;

	tx_ring->wr_p++;
	tx_ring->wr_ptr_add++;
	return true;
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
 * Return: Number of packets received.
 */
static int nfp_net_rx(struct nfp_net_rx_ring *rx_ring, int budget)
{
	struct nfp_net_r_vector *r_vec = rx_ring->r_vec;
	struct nfp_net_dp *dp = &r_vec->nfp_net->dp;
	struct nfp_net_tx_ring *tx_ring;
	struct bpf_prog *xdp_prog;
	bool xdp_tx_cmpl = false;
	unsigned int true_bufsz;
	struct sk_buff *skb;
	int pkts_polled = 0;
	struct xdp_buff xdp;
	int idx;

	rcu_read_lock();
	xdp_prog = READ_ONCE(dp->xdp_prog);
	true_bufsz = xdp_prog ? PAGE_SIZE : dp->fl_bufsz;
	xdp.frame_sz = PAGE_SIZE - NFP_NET_RX_BUF_HEADROOM;
	xdp.rxq = &rx_ring->xdp_rxq;
	tx_ring = r_vec->xdp_ring;

	while (pkts_polled < budget) {
		unsigned int meta_len, data_len, meta_off, pkt_len, pkt_off;
		struct nfp_net_rx_buf *rxbuf;
		struct nfp_net_rx_desc *rxd;
		struct nfp_meta_parsed meta;
		bool redir_egress = false;
		struct net_device *netdev;
		dma_addr_t new_dma_addr;
		u32 meta_len_xdp = 0;
		void *new_frag;

		idx = D_IDX(rx_ring, rx_ring->rd_p);

		rxd = &rx_ring->rxds[idx];
		if (!(rxd->rxd.meta_len_dd & PCIE_DESC_RX_DD))
			break;

		/* Memory barrier to ensure that we won't do other reads
		 * before the DD bit.
		 */
		dma_rmb();

		memset(&meta, 0, sizeof(meta));

		rx_ring->rd_p++;
		pkts_polled++;

		rxbuf =	&rx_ring->rxbufs[idx];
		/*         < meta_len >
		 *  <-- [rx_offset] -->
		 *  ---------------------------------------------------------
		 * | [XX] |  metadata  |             packet           | XXXX |
		 *  ---------------------------------------------------------
		 *         <---------------- data_len --------------->
		 *
		 * The rx_offset is fixed for all packets, the meta_len can vary
		 * on a packet by packet basis. If rx_offset is set to zero
		 * (_RX_OFFSET_DYNAMIC) metadata starts at the beginning of the
		 * buffer and is immediately followed by the packet (no [XX]).
		 */
		meta_len = rxd->rxd.meta_len_dd & PCIE_DESC_RX_META_LEN_MASK;
		data_len = le16_to_cpu(rxd->rxd.data_len);
		pkt_len = data_len - meta_len;

		pkt_off = NFP_NET_RX_BUF_HEADROOM + dp->rx_dma_off;
		if (dp->rx_offset == NFP_NET_CFG_RX_OFFSET_DYNAMIC)
			pkt_off += meta_len;
		else
			pkt_off += dp->rx_offset;
		meta_off = pkt_off - meta_len;

		/* Stats update */
		u64_stats_update_begin(&r_vec->rx_sync);
		r_vec->rx_pkts++;
		r_vec->rx_bytes += pkt_len;
		u64_stats_update_end(&r_vec->rx_sync);

		if (unlikely(meta_len > NFP_NET_MAX_PREPEND ||
			     (dp->rx_offset && meta_len > dp->rx_offset))) {
			nn_dp_warn(dp, "oversized RX packet metadata %u\n",
				   meta_len);
			nfp_net_rx_drop(dp, r_vec, rx_ring, rxbuf, NULL);
			continue;
		}

		nfp_net_dma_sync_cpu_rx(dp, rxbuf->dma_addr + meta_off,
					data_len);

		if (!dp->chained_metadata_format) {
			nfp_net_set_hash_desc(dp->netdev, &meta,
					      rxbuf->frag + meta_off, rxd);
		} else if (meta_len) {
			if (unlikely(nfp_net_parse_meta(dp->netdev, &meta,
							rxbuf->frag + meta_off,
							rxbuf->frag + pkt_off,
							pkt_len, meta_len))) {
				nn_dp_warn(dp, "invalid RX packet metadata\n");
				nfp_net_rx_drop(dp, r_vec, rx_ring, rxbuf,
						NULL);
				continue;
			}
		}

		if (xdp_prog && !meta.portid) {
			void *orig_data = rxbuf->frag + pkt_off;
			unsigned int dma_off;
			int act;

			xdp.data_hard_start = rxbuf->frag + NFP_NET_RX_BUF_HEADROOM;
			xdp.data = orig_data;
			xdp.data_meta = orig_data;
			xdp.data_end = orig_data + pkt_len;

			act = bpf_prog_run_xdp(xdp_prog, &xdp);

			pkt_len = xdp.data_end - xdp.data;
			pkt_off += xdp.data - orig_data;

			switch (act) {
			case XDP_PASS:
				meta_len_xdp = xdp.data - xdp.data_meta;
				break;
			case XDP_TX:
				dma_off = pkt_off - NFP_NET_RX_BUF_HEADROOM;
				if (unlikely(!nfp_net_tx_xdp_buf(dp, rx_ring,
								 tx_ring, rxbuf,
								 dma_off,
								 pkt_len,
								 &xdp_tx_cmpl)))
					trace_xdp_exception(dp->netdev,
							    xdp_prog, act);
				continue;
			default:
				bpf_warn_invalid_xdp_action(act);
				fallthrough;
			case XDP_ABORTED:
				trace_xdp_exception(dp->netdev, xdp_prog, act);
				fallthrough;
			case XDP_DROP:
				nfp_net_rx_give_one(dp, rx_ring, rxbuf->frag,
						    rxbuf->dma_addr);
				continue;
			}
		}

		if (likely(!meta.portid)) {
			netdev = dp->netdev;
		} else if (meta.portid == NFP_META_PORT_ID_CTRL) {
			struct nfp_net *nn = netdev_priv(dp->netdev);

			nfp_app_ctrl_rx_raw(nn->app, rxbuf->frag + pkt_off,
					    pkt_len);
			nfp_net_rx_give_one(dp, rx_ring, rxbuf->frag,
					    rxbuf->dma_addr);
			continue;
		} else {
			struct nfp_net *nn;

			nn = netdev_priv(dp->netdev);
			netdev = nfp_app_dev_get(nn->app, meta.portid,
						 &redir_egress);
			if (unlikely(!netdev)) {
				nfp_net_rx_drop(dp, r_vec, rx_ring, rxbuf,
						NULL);
				continue;
			}

			if (nfp_netdev_is_nfp_repr(netdev))
				nfp_repr_inc_rx_stats(netdev, pkt_len);
		}

		skb = build_skb(rxbuf->frag, true_bufsz);
		if (unlikely(!skb)) {
			nfp_net_rx_drop(dp, r_vec, rx_ring, rxbuf, NULL);
			continue;
		}
		new_frag = nfp_net_napi_alloc_one(dp, &new_dma_addr);
		if (unlikely(!new_frag)) {
			nfp_net_rx_drop(dp, r_vec, rx_ring, rxbuf, skb);
			continue;
		}

		nfp_net_dma_unmap_rx(dp, rxbuf->dma_addr);

		nfp_net_rx_give_one(dp, rx_ring, new_frag, new_dma_addr);

		skb_reserve(skb, pkt_off);
		skb_put(skb, pkt_len);

		skb->mark = meta.mark;
		skb_set_hash(skb, meta.hash, meta.hash_type);

		skb_record_rx_queue(skb, rx_ring->idx);
		skb->protocol = eth_type_trans(skb, netdev);

		nfp_net_rx_csum(dp, r_vec, rxd, &meta, skb);

#ifdef CONFIG_TLS_DEVICE
		if (rxd->rxd.flags & PCIE_DESC_RX_DECRYPTED) {
			skb->decrypted = true;
			u64_stats_update_begin(&r_vec->rx_sync);
			r_vec->hw_tls_rx++;
			u64_stats_update_end(&r_vec->rx_sync);
		}
#endif

		if (rxd->rxd.flags & PCIE_DESC_RX_VLAN)
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
					       le16_to_cpu(rxd->rxd.vlan));
		if (meta_len_xdp)
			skb_metadata_set(skb, meta_len_xdp);

		if (likely(!redir_egress)) {
			napi_gro_receive(&rx_ring->r_vec->napi, skb);
		} else {
			skb->dev = netdev;
			skb_reset_network_header(skb);
			__skb_push(skb, ETH_HLEN);
			dev_queue_xmit(skb);
		}
	}

	if (xdp_prog) {
		if (tx_ring->wr_ptr_add)
			nfp_net_tx_xmit_more_flush(tx_ring);
		else if (unlikely(tx_ring->wr_p != tx_ring->rd_p) &&
			 !xdp_tx_cmpl)
			if (!nfp_net_xdp_complete(tx_ring))
				pkts_polled = budget;
	}
	rcu_read_unlock();

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
	unsigned int pkts_polled = 0;

	if (r_vec->tx_ring)
		nfp_net_tx_complete(r_vec->tx_ring, budget);
	if (r_vec->rx_ring)
		pkts_polled = nfp_net_rx(r_vec->rx_ring, budget);

	if (pkts_polled < budget)
		if (napi_complete_done(napi, pkts_polled))
			nfp_net_irq_unmask(r_vec->nfp_net, r_vec->irq_entry);

	return pkts_polled;
}

/* Control device data path
 */

static bool
nfp_ctrl_tx_one(struct nfp_net *nn, struct nfp_net_r_vector *r_vec,
		struct sk_buff *skb, bool old)
{
	unsigned int real_len = skb->len, meta_len = 0;
	struct nfp_net_tx_ring *tx_ring;
	struct nfp_net_tx_buf *txbuf;
	struct nfp_net_tx_desc *txd;
	struct nfp_net_dp *dp;
	dma_addr_t dma_addr;
	int wr_idx;

	dp = &r_vec->nfp_net->dp;
	tx_ring = r_vec->tx_ring;

	if (WARN_ON_ONCE(skb_shinfo(skb)->nr_frags)) {
		nn_dp_warn(dp, "Driver's CTRL TX does not implement gather\n");
		goto err_free;
	}

	if (unlikely(nfp_net_tx_full(tx_ring, 1))) {
		u64_stats_update_begin(&r_vec->tx_sync);
		r_vec->tx_busy++;
		u64_stats_update_end(&r_vec->tx_sync);
		if (!old)
			__skb_queue_tail(&r_vec->queue, skb);
		else
			__skb_queue_head(&r_vec->queue, skb);
		return true;
	}

	if (nfp_app_ctrl_has_meta(nn->app)) {
		if (unlikely(skb_headroom(skb) < 8)) {
			nn_dp_warn(dp, "CTRL TX on skb without headroom\n");
			goto err_free;
		}
		meta_len = 8;
		put_unaligned_be32(NFP_META_PORT_ID_CTRL, skb_push(skb, 4));
		put_unaligned_be32(NFP_NET_META_PORTID, skb_push(skb, 4));
	}

	/* Start with the head skbuf */
	dma_addr = dma_map_single(dp->dev, skb->data, skb_headlen(skb),
				  DMA_TO_DEVICE);
	if (dma_mapping_error(dp->dev, dma_addr))
		goto err_dma_warn;

	wr_idx = D_IDX(tx_ring, tx_ring->wr_p);

	/* Stash the soft descriptor of the head then initialize it */
	txbuf = &tx_ring->txbufs[wr_idx];
	txbuf->skb = skb;
	txbuf->dma_addr = dma_addr;
	txbuf->fidx = -1;
	txbuf->pkt_cnt = 1;
	txbuf->real_len = real_len;

	/* Build TX descriptor */
	txd = &tx_ring->txds[wr_idx];
	txd->offset_eop = meta_len | PCIE_DESC_TX_EOP;
	txd->dma_len = cpu_to_le16(skb_headlen(skb));
	nfp_desc_set_dma_addr(txd, dma_addr);
	txd->data_len = cpu_to_le16(skb->len);

	txd->flags = 0;
	txd->mss = 0;
	txd->lso_hdrlen = 0;

	tx_ring->wr_p++;
	tx_ring->wr_ptr_add++;
	nfp_net_tx_xmit_more_flush(tx_ring);

	return false;

err_dma_warn:
	nn_dp_warn(dp, "Failed to DMA map TX CTRL buffer\n");
err_free:
	u64_stats_update_begin(&r_vec->tx_sync);
	r_vec->tx_errors++;
	u64_stats_update_end(&r_vec->tx_sync);
	dev_kfree_skb_any(skb);
	return false;
}

bool __nfp_ctrl_tx(struct nfp_net *nn, struct sk_buff *skb)
{
	struct nfp_net_r_vector *r_vec = &nn->r_vecs[0];

	return nfp_ctrl_tx_one(nn, r_vec, skb, false);
}

bool nfp_ctrl_tx(struct nfp_net *nn, struct sk_buff *skb)
{
	struct nfp_net_r_vector *r_vec = &nn->r_vecs[0];
	bool ret;

	spin_lock_bh(&r_vec->lock);
	ret = nfp_ctrl_tx_one(nn, r_vec, skb, false);
	spin_unlock_bh(&r_vec->lock);

	return ret;
}

static void __nfp_ctrl_tx_queued(struct nfp_net_r_vector *r_vec)
{
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(&r_vec->queue)))
		if (nfp_ctrl_tx_one(r_vec->nfp_net, r_vec, skb, true))
			return;
}

static bool
nfp_ctrl_meta_ok(struct nfp_net *nn, void *data, unsigned int meta_len)
{
	u32 meta_type, meta_tag;

	if (!nfp_app_ctrl_has_meta(nn->app))
		return !meta_len;

	if (meta_len != 8)
		return false;

	meta_type = get_unaligned_be32(data);
	meta_tag = get_unaligned_be32(data + 4);

	return (meta_type == NFP_NET_META_PORTID &&
		meta_tag == NFP_META_PORT_ID_CTRL);
}

static bool
nfp_ctrl_rx_one(struct nfp_net *nn, struct nfp_net_dp *dp,
		struct nfp_net_r_vector *r_vec, struct nfp_net_rx_ring *rx_ring)
{
	unsigned int meta_len, data_len, meta_off, pkt_len, pkt_off;
	struct nfp_net_rx_buf *rxbuf;
	struct nfp_net_rx_desc *rxd;
	dma_addr_t new_dma_addr;
	struct sk_buff *skb;
	void *new_frag;
	int idx;

	idx = D_IDX(rx_ring, rx_ring->rd_p);

	rxd = &rx_ring->rxds[idx];
	if (!(rxd->rxd.meta_len_dd & PCIE_DESC_RX_DD))
		return false;

	/* Memory barrier to ensure that we won't do other reads
	 * before the DD bit.
	 */
	dma_rmb();

	rx_ring->rd_p++;

	rxbuf =	&rx_ring->rxbufs[idx];
	meta_len = rxd->rxd.meta_len_dd & PCIE_DESC_RX_META_LEN_MASK;
	data_len = le16_to_cpu(rxd->rxd.data_len);
	pkt_len = data_len - meta_len;

	pkt_off = NFP_NET_RX_BUF_HEADROOM + dp->rx_dma_off;
	if (dp->rx_offset == NFP_NET_CFG_RX_OFFSET_DYNAMIC)
		pkt_off += meta_len;
	else
		pkt_off += dp->rx_offset;
	meta_off = pkt_off - meta_len;

	/* Stats update */
	u64_stats_update_begin(&r_vec->rx_sync);
	r_vec->rx_pkts++;
	r_vec->rx_bytes += pkt_len;
	u64_stats_update_end(&r_vec->rx_sync);

	nfp_net_dma_sync_cpu_rx(dp, rxbuf->dma_addr + meta_off,	data_len);

	if (unlikely(!nfp_ctrl_meta_ok(nn, rxbuf->frag + meta_off, meta_len))) {
		nn_dp_warn(dp, "incorrect metadata for ctrl packet (%d)\n",
			   meta_len);
		nfp_net_rx_drop(dp, r_vec, rx_ring, rxbuf, NULL);
		return true;
	}

	skb = build_skb(rxbuf->frag, dp->fl_bufsz);
	if (unlikely(!skb)) {
		nfp_net_rx_drop(dp, r_vec, rx_ring, rxbuf, NULL);
		return true;
	}
	new_frag = nfp_net_napi_alloc_one(dp, &new_dma_addr);
	if (unlikely(!new_frag)) {
		nfp_net_rx_drop(dp, r_vec, rx_ring, rxbuf, skb);
		return true;
	}

	nfp_net_dma_unmap_rx(dp, rxbuf->dma_addr);

	nfp_net_rx_give_one(dp, rx_ring, new_frag, new_dma_addr);

	skb_reserve(skb, pkt_off);
	skb_put(skb, pkt_len);

	nfp_app_ctrl_rx(nn->app, skb);

	return true;
}

static bool nfp_ctrl_rx(struct nfp_net_r_vector *r_vec)
{
	struct nfp_net_rx_ring *rx_ring = r_vec->rx_ring;
	struct nfp_net *nn = r_vec->nfp_net;
	struct nfp_net_dp *dp = &nn->dp;
	unsigned int budget = 512;

	while (nfp_ctrl_rx_one(nn, dp, r_vec, rx_ring) && budget--)
		continue;

	return budget;
}

static void nfp_ctrl_poll(struct tasklet_struct *t)
{
	struct nfp_net_r_vector *r_vec = from_tasklet(r_vec, t, tasklet);

	spin_lock(&r_vec->lock);
	nfp_net_tx_complete(r_vec->tx_ring, 0);
	__nfp_ctrl_tx_queued(r_vec);
	spin_unlock(&r_vec->lock);

	if (nfp_ctrl_rx(r_vec)) {
		nfp_net_irq_unmask(r_vec->nfp_net, r_vec->irq_entry);
	} else {
		tasklet_schedule(&r_vec->tasklet);
		nn_dp_warn(&r_vec->nfp_net->dp,
			   "control message budget exceeded!\n");
	}
}

/* Setup and Configuration
 */

/**
 * nfp_net_vecs_init() - Assign IRQs and setup rvecs.
 * @nn:		NFP Network structure
 */
static void nfp_net_vecs_init(struct nfp_net *nn)
{
	struct nfp_net_r_vector *r_vec;
	int r;

	nn->lsc_handler = nfp_net_irq_lsc;
	nn->exn_handler = nfp_net_irq_exn;

	for (r = 0; r < nn->max_r_vecs; r++) {
		struct msix_entry *entry;

		entry = &nn->irq_entries[NFP_NET_NON_Q_VECTORS + r];

		r_vec = &nn->r_vecs[r];
		r_vec->nfp_net = nn;
		r_vec->irq_entry = entry->entry;
		r_vec->irq_vector = entry->vector;

		if (nn->dp.netdev) {
			r_vec->handler = nfp_net_irq_rxtx;
		} else {
			r_vec->handler = nfp_ctrl_irq_rxtx;

			__skb_queue_head_init(&r_vec->queue);
			spin_lock_init(&r_vec->lock);
			tasklet_setup(&r_vec->tasklet, nfp_ctrl_poll);
			tasklet_disable(&r_vec->tasklet);
		}

		cpumask_set_cpu(r, &r_vec->affinity_mask);
	}
}

/**
 * nfp_net_tx_ring_free() - Free resources allocated to a TX ring
 * @tx_ring:   TX ring to free
 */
static void nfp_net_tx_ring_free(struct nfp_net_tx_ring *tx_ring)
{
	struct nfp_net_r_vector *r_vec = tx_ring->r_vec;
	struct nfp_net_dp *dp = &r_vec->nfp_net->dp;

	kvfree(tx_ring->txbufs);

	if (tx_ring->txds)
		dma_free_coherent(dp->dev, tx_ring->size,
				  tx_ring->txds, tx_ring->dma);

	tx_ring->cnt = 0;
	tx_ring->txbufs = NULL;
	tx_ring->txds = NULL;
	tx_ring->dma = 0;
	tx_ring->size = 0;
}

/**
 * nfp_net_tx_ring_alloc() - Allocate resource for a TX ring
 * @dp:        NFP Net data path struct
 * @tx_ring:   TX Ring structure to allocate
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int
nfp_net_tx_ring_alloc(struct nfp_net_dp *dp, struct nfp_net_tx_ring *tx_ring)
{
	struct nfp_net_r_vector *r_vec = tx_ring->r_vec;

	tx_ring->cnt = dp->txd_cnt;

	tx_ring->size = array_size(tx_ring->cnt, sizeof(*tx_ring->txds));
	tx_ring->txds = dma_alloc_coherent(dp->dev, tx_ring->size,
					   &tx_ring->dma,
					   GFP_KERNEL | __GFP_NOWARN);
	if (!tx_ring->txds) {
		netdev_warn(dp->netdev, "failed to allocate TX descriptor ring memory, requested descriptor count: %d, consider lowering descriptor count\n",
			    tx_ring->cnt);
		goto err_alloc;
	}

	tx_ring->txbufs = kvcalloc(tx_ring->cnt, sizeof(*tx_ring->txbufs),
				   GFP_KERNEL);
	if (!tx_ring->txbufs)
		goto err_alloc;

	if (!tx_ring->is_xdp && dp->netdev)
		netif_set_xps_queue(dp->netdev, &r_vec->affinity_mask,
				    tx_ring->idx);

	return 0;

err_alloc:
	nfp_net_tx_ring_free(tx_ring);
	return -ENOMEM;
}

static void
nfp_net_tx_ring_bufs_free(struct nfp_net_dp *dp,
			  struct nfp_net_tx_ring *tx_ring)
{
	unsigned int i;

	if (!tx_ring->is_xdp)
		return;

	for (i = 0; i < tx_ring->cnt; i++) {
		if (!tx_ring->txbufs[i].frag)
			return;

		nfp_net_dma_unmap_rx(dp, tx_ring->txbufs[i].dma_addr);
		__free_page(virt_to_page(tx_ring->txbufs[i].frag));
	}
}

static int
nfp_net_tx_ring_bufs_alloc(struct nfp_net_dp *dp,
			   struct nfp_net_tx_ring *tx_ring)
{
	struct nfp_net_tx_buf *txbufs = tx_ring->txbufs;
	unsigned int i;

	if (!tx_ring->is_xdp)
		return 0;

	for (i = 0; i < tx_ring->cnt; i++) {
		txbufs[i].frag = nfp_net_rx_alloc_one(dp, &txbufs[i].dma_addr);
		if (!txbufs[i].frag) {
			nfp_net_tx_ring_bufs_free(dp, tx_ring);
			return -ENOMEM;
		}
	}

	return 0;
}

static int nfp_net_tx_rings_prepare(struct nfp_net *nn, struct nfp_net_dp *dp)
{
	unsigned int r;

	dp->tx_rings = kcalloc(dp->num_tx_rings, sizeof(*dp->tx_rings),
			       GFP_KERNEL);
	if (!dp->tx_rings)
		return -ENOMEM;

	for (r = 0; r < dp->num_tx_rings; r++) {
		int bias = 0;

		if (r >= dp->num_stack_tx_rings)
			bias = dp->num_stack_tx_rings;

		nfp_net_tx_ring_init(&dp->tx_rings[r], &nn->r_vecs[r - bias],
				     r, bias);

		if (nfp_net_tx_ring_alloc(dp, &dp->tx_rings[r]))
			goto err_free_prev;

		if (nfp_net_tx_ring_bufs_alloc(dp, &dp->tx_rings[r]))
			goto err_free_ring;
	}

	return 0;

err_free_prev:
	while (r--) {
		nfp_net_tx_ring_bufs_free(dp, &dp->tx_rings[r]);
err_free_ring:
		nfp_net_tx_ring_free(&dp->tx_rings[r]);
	}
	kfree(dp->tx_rings);
	return -ENOMEM;
}

static void nfp_net_tx_rings_free(struct nfp_net_dp *dp)
{
	unsigned int r;

	for (r = 0; r < dp->num_tx_rings; r++) {
		nfp_net_tx_ring_bufs_free(dp, &dp->tx_rings[r]);
		nfp_net_tx_ring_free(&dp->tx_rings[r]);
	}

	kfree(dp->tx_rings);
}

/**
 * nfp_net_rx_ring_free() - Free resources allocated to a RX ring
 * @rx_ring:  RX ring to free
 */
static void nfp_net_rx_ring_free(struct nfp_net_rx_ring *rx_ring)
{
	struct nfp_net_r_vector *r_vec = rx_ring->r_vec;
	struct nfp_net_dp *dp = &r_vec->nfp_net->dp;

	if (dp->netdev)
		xdp_rxq_info_unreg(&rx_ring->xdp_rxq);
	kvfree(rx_ring->rxbufs);

	if (rx_ring->rxds)
		dma_free_coherent(dp->dev, rx_ring->size,
				  rx_ring->rxds, rx_ring->dma);

	rx_ring->cnt = 0;
	rx_ring->rxbufs = NULL;
	rx_ring->rxds = NULL;
	rx_ring->dma = 0;
	rx_ring->size = 0;
}

/**
 * nfp_net_rx_ring_alloc() - Allocate resource for a RX ring
 * @dp:	      NFP Net data path struct
 * @rx_ring:  RX ring to allocate
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int
nfp_net_rx_ring_alloc(struct nfp_net_dp *dp, struct nfp_net_rx_ring *rx_ring)
{
	int err;

	if (dp->netdev) {
		err = xdp_rxq_info_reg(&rx_ring->xdp_rxq, dp->netdev,
				       rx_ring->idx);
		if (err < 0)
			return err;
	}

	rx_ring->cnt = dp->rxd_cnt;
	rx_ring->size = array_size(rx_ring->cnt, sizeof(*rx_ring->rxds));
	rx_ring->rxds = dma_alloc_coherent(dp->dev, rx_ring->size,
					   &rx_ring->dma,
					   GFP_KERNEL | __GFP_NOWARN);
	if (!rx_ring->rxds) {
		netdev_warn(dp->netdev, "failed to allocate RX descriptor ring memory, requested descriptor count: %d, consider lowering descriptor count\n",
			    rx_ring->cnt);
		goto err_alloc;
	}

	rx_ring->rxbufs = kvcalloc(rx_ring->cnt, sizeof(*rx_ring->rxbufs),
				   GFP_KERNEL);
	if (!rx_ring->rxbufs)
		goto err_alloc;

	return 0;

err_alloc:
	nfp_net_rx_ring_free(rx_ring);
	return -ENOMEM;
}

static int nfp_net_rx_rings_prepare(struct nfp_net *nn, struct nfp_net_dp *dp)
{
	unsigned int r;

	dp->rx_rings = kcalloc(dp->num_rx_rings, sizeof(*dp->rx_rings),
			       GFP_KERNEL);
	if (!dp->rx_rings)
		return -ENOMEM;

	for (r = 0; r < dp->num_rx_rings; r++) {
		nfp_net_rx_ring_init(&dp->rx_rings[r], &nn->r_vecs[r], r);

		if (nfp_net_rx_ring_alloc(dp, &dp->rx_rings[r]))
			goto err_free_prev;

		if (nfp_net_rx_ring_bufs_alloc(dp, &dp->rx_rings[r]))
			goto err_free_ring;
	}

	return 0;

err_free_prev:
	while (r--) {
		nfp_net_rx_ring_bufs_free(dp, &dp->rx_rings[r]);
err_free_ring:
		nfp_net_rx_ring_free(&dp->rx_rings[r]);
	}
	kfree(dp->rx_rings);
	return -ENOMEM;
}

static void nfp_net_rx_rings_free(struct nfp_net_dp *dp)
{
	unsigned int r;

	for (r = 0; r < dp->num_rx_rings; r++) {
		nfp_net_rx_ring_bufs_free(dp, &dp->rx_rings[r]);
		nfp_net_rx_ring_free(&dp->rx_rings[r]);
	}

	kfree(dp->rx_rings);
}

static void
nfp_net_vector_assign_rings(struct nfp_net_dp *dp,
			    struct nfp_net_r_vector *r_vec, int idx)
{
	r_vec->rx_ring = idx < dp->num_rx_rings ? &dp->rx_rings[idx] : NULL;
	r_vec->tx_ring =
		idx < dp->num_stack_tx_rings ? &dp->tx_rings[idx] : NULL;

	r_vec->xdp_ring = idx < dp->num_tx_rings - dp->num_stack_tx_rings ?
		&dp->tx_rings[dp->num_stack_tx_rings + idx] : NULL;
}

static int
nfp_net_prepare_vector(struct nfp_net *nn, struct nfp_net_r_vector *r_vec,
		       int idx)
{
	int err;

	/* Setup NAPI */
	if (nn->dp.netdev)
		netif_napi_add(nn->dp.netdev, &r_vec->napi,
			       nfp_net_poll, NAPI_POLL_WEIGHT);
	else
		tasklet_enable(&r_vec->tasklet);

	snprintf(r_vec->name, sizeof(r_vec->name),
		 "%s-rxtx-%d", nfp_net_name(nn), idx);
	err = request_irq(r_vec->irq_vector, r_vec->handler, 0, r_vec->name,
			  r_vec);
	if (err) {
		if (nn->dp.netdev)
			netif_napi_del(&r_vec->napi);
		else
			tasklet_disable(&r_vec->tasklet);

		nn_err(nn, "Error requesting IRQ %d\n", r_vec->irq_vector);
		return err;
	}
	disable_irq(r_vec->irq_vector);

	irq_set_affinity_hint(r_vec->irq_vector, &r_vec->affinity_mask);

	nn_dbg(nn, "RV%02d: irq=%03d/%03d\n", idx, r_vec->irq_vector,
	       r_vec->irq_entry);

	return 0;
}

static void
nfp_net_cleanup_vector(struct nfp_net *nn, struct nfp_net_r_vector *r_vec)
{
	irq_set_affinity_hint(r_vec->irq_vector, NULL);
	if (nn->dp.netdev)
		netif_napi_del(&r_vec->napi);
	else
		tasklet_disable(&r_vec->tasklet);

	free_irq(r_vec->irq_vector, r_vec);
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

	for (i = 0; i < nfp_net_rss_key_sz(nn); i += 4)
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
	factor = nn->tlv_caps.me_freq_mhz / 16;

	/* copy RX interrupt coalesce parameters */
	value = (nn->rx_coalesce_max_frames << 16) |
		(factor * nn->rx_coalesce_usecs);
	for (i = 0; i < nn->dp.num_rx_rings; i++)
		nn_writel(nn, NFP_NET_CFG_RXR_IRQ_MOD(i), value);

	/* copy TX interrupt coalesce parameters */
	value = (nn->tx_coalesce_max_frames << 16) |
		(factor * nn->tx_coalesce_usecs);
	for (i = 0; i < nn->dp.num_tx_rings; i++)
		nn_writel(nn, NFP_NET_CFG_TXR_IRQ_MOD(i), value);
}

/**
 * nfp_net_write_mac_addr() - Write mac address to the device control BAR
 * @nn:      NFP Net device to reconfigure
 * @addr:    MAC address to write
 *
 * Writes the MAC address from the netdev to the device control BAR.  Does not
 * perform the required reconfig.  We do a bit of byte swapping dance because
 * firmware is LE.
 */
static void nfp_net_write_mac_addr(struct nfp_net *nn, const u8 *addr)
{
	nn_writel(nn, NFP_NET_CFG_MACADDR + 0, get_unaligned_be32(addr));
	nn_writew(nn, NFP_NET_CFG_MACADDR + 6, get_unaligned_be16(addr + 4));
}

static void nfp_net_vec_clear_ring_data(struct nfp_net *nn, unsigned int idx)
{
	nn_writeq(nn, NFP_NET_CFG_RXR_ADDR(idx), 0);
	nn_writeb(nn, NFP_NET_CFG_RXR_SZ(idx), 0);
	nn_writeb(nn, NFP_NET_CFG_RXR_VEC(idx), 0);

	nn_writeq(nn, NFP_NET_CFG_TXR_ADDR(idx), 0);
	nn_writeb(nn, NFP_NET_CFG_TXR_SZ(idx), 0);
	nn_writeb(nn, NFP_NET_CFG_TXR_VEC(idx), 0);
}

/**
 * nfp_net_clear_config_and_disable() - Clear control BAR and disable NFP
 * @nn:      NFP Net device to reconfigure
 *
 * Warning: must be fully idempotent.
 */
static void nfp_net_clear_config_and_disable(struct nfp_net *nn)
{
	u32 new_ctrl, update;
	unsigned int r;
	int err;

	new_ctrl = nn->dp.ctrl;
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
	if (err)
		nn_err(nn, "Could not disable device: %d\n", err);

	for (r = 0; r < nn->dp.num_rx_rings; r++)
		nfp_net_rx_ring_reset(&nn->dp.rx_rings[r]);
	for (r = 0; r < nn->dp.num_tx_rings; r++)
		nfp_net_tx_ring_reset(&nn->dp, &nn->dp.tx_rings[r]);
	for (r = 0; r < nn->dp.num_r_vecs; r++)
		nfp_net_vec_clear_ring_data(nn, r);

	nn->dp.ctrl = new_ctrl;
}

static void
nfp_net_rx_ring_hw_cfg_write(struct nfp_net *nn,
			     struct nfp_net_rx_ring *rx_ring, unsigned int idx)
{
	/* Write the DMA address, size and MSI-X info to the device */
	nn_writeq(nn, NFP_NET_CFG_RXR_ADDR(idx), rx_ring->dma);
	nn_writeb(nn, NFP_NET_CFG_RXR_SZ(idx), ilog2(rx_ring->cnt));
	nn_writeb(nn, NFP_NET_CFG_RXR_VEC(idx), rx_ring->r_vec->irq_entry);
}

static void
nfp_net_tx_ring_hw_cfg_write(struct nfp_net *nn,
			     struct nfp_net_tx_ring *tx_ring, unsigned int idx)
{
	nn_writeq(nn, NFP_NET_CFG_TXR_ADDR(idx), tx_ring->dma);
	nn_writeb(nn, NFP_NET_CFG_TXR_SZ(idx), ilog2(tx_ring->cnt));
	nn_writeb(nn, NFP_NET_CFG_TXR_VEC(idx), tx_ring->r_vec->irq_entry);
}

/**
 * nfp_net_set_config_and_enable() - Write control BAR and enable NFP
 * @nn:      NFP Net device to reconfigure
 */
static int nfp_net_set_config_and_enable(struct nfp_net *nn)
{
	u32 bufsz, new_ctrl, update = 0;
	unsigned int r;
	int err;

	new_ctrl = nn->dp.ctrl;

	if (nn->dp.ctrl & NFP_NET_CFG_CTRL_RSS_ANY) {
		nfp_net_rss_write_key(nn);
		nfp_net_rss_write_itbl(nn);
		nn_writel(nn, NFP_NET_CFG_RSS_CTRL, nn->rss_cfg);
		update |= NFP_NET_CFG_UPDATE_RSS;
	}

	if (nn->dp.ctrl & NFP_NET_CFG_CTRL_IRQMOD) {
		nfp_net_coalesce_write_cfg(nn);
		update |= NFP_NET_CFG_UPDATE_IRQMOD;
	}

	for (r = 0; r < nn->dp.num_tx_rings; r++)
		nfp_net_tx_ring_hw_cfg_write(nn, &nn->dp.tx_rings[r], r);
	for (r = 0; r < nn->dp.num_rx_rings; r++)
		nfp_net_rx_ring_hw_cfg_write(nn, &nn->dp.rx_rings[r], r);

	nn_writeq(nn, NFP_NET_CFG_TXRS_ENABLE, nn->dp.num_tx_rings == 64 ?
		  0xffffffffffffffffULL : ((u64)1 << nn->dp.num_tx_rings) - 1);

	nn_writeq(nn, NFP_NET_CFG_RXRS_ENABLE, nn->dp.num_rx_rings == 64 ?
		  0xffffffffffffffffULL : ((u64)1 << nn->dp.num_rx_rings) - 1);

	if (nn->dp.netdev)
		nfp_net_write_mac_addr(nn, nn->dp.netdev->dev_addr);

	nn_writel(nn, NFP_NET_CFG_MTU, nn->dp.mtu);

	bufsz = nn->dp.fl_bufsz - nn->dp.rx_dma_off - NFP_NET_RX_BUF_NON_DATA;
	nn_writel(nn, NFP_NET_CFG_FLBUFSZ, bufsz);

	/* Enable device */
	new_ctrl |= NFP_NET_CFG_CTRL_ENABLE;
	update |= NFP_NET_CFG_UPDATE_GEN;
	update |= NFP_NET_CFG_UPDATE_MSIX;
	update |= NFP_NET_CFG_UPDATE_RING;
	if (nn->cap & NFP_NET_CFG_CTRL_RINGCFG)
		new_ctrl |= NFP_NET_CFG_CTRL_RINGCFG;

	nn_writel(nn, NFP_NET_CFG_CTRL, new_ctrl);
	err = nfp_net_reconfig(nn, update);
	if (err) {
		nfp_net_clear_config_and_disable(nn);
		return err;
	}

	nn->dp.ctrl = new_ctrl;

	for (r = 0; r < nn->dp.num_rx_rings; r++)
		nfp_net_rx_ring_fill_freelist(&nn->dp, &nn->dp.rx_rings[r]);

	return 0;
}

/**
 * nfp_net_close_stack() - Quiesce the stack (part of close)
 * @nn:	     NFP Net device to reconfigure
 */
static void nfp_net_close_stack(struct nfp_net *nn)
{
	unsigned int r;

	disable_irq(nn->irq_entries[NFP_NET_IRQ_LSC_IDX].vector);
	netif_carrier_off(nn->dp.netdev);
	nn->link_up = false;

	for (r = 0; r < nn->dp.num_r_vecs; r++) {
		disable_irq(nn->r_vecs[r].irq_vector);
		napi_disable(&nn->r_vecs[r].napi);
	}

	netif_tx_disable(nn->dp.netdev);
}

/**
 * nfp_net_close_free_all() - Free all runtime resources
 * @nn:      NFP Net device to reconfigure
 */
static void nfp_net_close_free_all(struct nfp_net *nn)
{
	unsigned int r;

	nfp_net_tx_rings_free(&nn->dp);
	nfp_net_rx_rings_free(&nn->dp);

	for (r = 0; r < nn->dp.num_r_vecs; r++)
		nfp_net_cleanup_vector(nn, &nn->r_vecs[r]);

	nfp_net_aux_irq_free(nn, NFP_NET_CFG_LSC, NFP_NET_IRQ_LSC_IDX);
	nfp_net_aux_irq_free(nn, NFP_NET_CFG_EXN, NFP_NET_IRQ_EXN_IDX);
}

/**
 * nfp_net_netdev_close() - Called when the device is downed
 * @netdev:      netdev structure
 */
static int nfp_net_netdev_close(struct net_device *netdev)
{
	struct nfp_net *nn = netdev_priv(netdev);

	/* Step 1: Disable RX and TX rings from the Linux kernel perspective
	 */
	nfp_net_close_stack(nn);

	/* Step 2: Tell NFP
	 */
	nfp_net_clear_config_and_disable(nn);
	nfp_port_configure(netdev, false);

	/* Step 3: Free resources
	 */
	nfp_net_close_free_all(nn);

	nn_dbg(nn, "%s down", netdev->name);
	return 0;
}

void nfp_ctrl_close(struct nfp_net *nn)
{
	int r;

	rtnl_lock();

	for (r = 0; r < nn->dp.num_r_vecs; r++) {
		disable_irq(nn->r_vecs[r].irq_vector);
		tasklet_disable(&nn->r_vecs[r].tasklet);
	}

	nfp_net_clear_config_and_disable(nn);

	nfp_net_close_free_all(nn);

	rtnl_unlock();
}

/**
 * nfp_net_open_stack() - Start the device from stack's perspective
 * @nn:      NFP Net device to reconfigure
 */
static void nfp_net_open_stack(struct nfp_net *nn)
{
	unsigned int r;

	for (r = 0; r < nn->dp.num_r_vecs; r++) {
		napi_enable(&nn->r_vecs[r].napi);
		enable_irq(nn->r_vecs[r].irq_vector);
	}

	netif_tx_wake_all_queues(nn->dp.netdev);

	enable_irq(nn->irq_entries[NFP_NET_IRQ_LSC_IDX].vector);
	nfp_net_read_link_status(nn);
}

static int nfp_net_open_alloc_all(struct nfp_net *nn)
{
	int err, r;

	err = nfp_net_aux_irq_request(nn, NFP_NET_CFG_EXN, "%s-exn",
				      nn->exn_name, sizeof(nn->exn_name),
				      NFP_NET_IRQ_EXN_IDX, nn->exn_handler);
	if (err)
		return err;
	err = nfp_net_aux_irq_request(nn, NFP_NET_CFG_LSC, "%s-lsc",
				      nn->lsc_name, sizeof(nn->lsc_name),
				      NFP_NET_IRQ_LSC_IDX, nn->lsc_handler);
	if (err)
		goto err_free_exn;
	disable_irq(nn->irq_entries[NFP_NET_IRQ_LSC_IDX].vector);

	for (r = 0; r < nn->dp.num_r_vecs; r++) {
		err = nfp_net_prepare_vector(nn, &nn->r_vecs[r], r);
		if (err)
			goto err_cleanup_vec_p;
	}

	err = nfp_net_rx_rings_prepare(nn, &nn->dp);
	if (err)
		goto err_cleanup_vec;

	err = nfp_net_tx_rings_prepare(nn, &nn->dp);
	if (err)
		goto err_free_rx_rings;

	for (r = 0; r < nn->max_r_vecs; r++)
		nfp_net_vector_assign_rings(&nn->dp, &nn->r_vecs[r], r);

	return 0;

err_free_rx_rings:
	nfp_net_rx_rings_free(&nn->dp);
err_cleanup_vec:
	r = nn->dp.num_r_vecs;
err_cleanup_vec_p:
	while (r--)
		nfp_net_cleanup_vector(nn, &nn->r_vecs[r]);
	nfp_net_aux_irq_free(nn, NFP_NET_CFG_LSC, NFP_NET_IRQ_LSC_IDX);
err_free_exn:
	nfp_net_aux_irq_free(nn, NFP_NET_CFG_EXN, NFP_NET_IRQ_EXN_IDX);
	return err;
}

static int nfp_net_netdev_open(struct net_device *netdev)
{
	struct nfp_net *nn = netdev_priv(netdev);
	int err;

	/* Step 1: Allocate resources for rings and the like
	 * - Request interrupts
	 * - Allocate RX and TX ring resources
	 * - Setup initial RSS table
	 */
	err = nfp_net_open_alloc_all(nn);
	if (err)
		return err;

	err = netif_set_real_num_tx_queues(netdev, nn->dp.num_stack_tx_rings);
	if (err)
		goto err_free_all;

	err = netif_set_real_num_rx_queues(netdev, nn->dp.num_rx_rings);
	if (err)
		goto err_free_all;

	/* Step 2: Configure the NFP
	 * - Ifup the physical interface if it exists
	 * - Enable rings from 0 to tx_rings/rx_rings - 1.
	 * - Write MAC address (in case it changed)
	 * - Set the MTU
	 * - Set the Freelist buffer size
	 * - Enable the FW
	 */
	err = nfp_port_configure(netdev, true);
	if (err)
		goto err_free_all;

	err = nfp_net_set_config_and_enable(nn);
	if (err)
		goto err_port_disable;

	/* Step 3: Enable for kernel
	 * - put some freelist descriptors on each RX ring
	 * - enable NAPI on each ring
	 * - enable all TX queues
	 * - set link state
	 */
	nfp_net_open_stack(nn);

	return 0;

err_port_disable:
	nfp_port_configure(netdev, false);
err_free_all:
	nfp_net_close_free_all(nn);
	return err;
}

int nfp_ctrl_open(struct nfp_net *nn)
{
	int err, r;

	/* ring dumping depends on vNICs being opened/closed under rtnl */
	rtnl_lock();

	err = nfp_net_open_alloc_all(nn);
	if (err)
		goto err_unlock;

	err = nfp_net_set_config_and_enable(nn);
	if (err)
		goto err_free_all;

	for (r = 0; r < nn->dp.num_r_vecs; r++)
		enable_irq(nn->r_vecs[r].irq_vector);

	rtnl_unlock();

	return 0;

err_free_all:
	nfp_net_close_free_all(nn);
err_unlock:
	rtnl_unlock();
	return err;
}

static void nfp_net_set_rx_mode(struct net_device *netdev)
{
	struct nfp_net *nn = netdev_priv(netdev);
	u32 new_ctrl;

	new_ctrl = nn->dp.ctrl;

	if (!netdev_mc_empty(netdev) || netdev->flags & IFF_ALLMULTI)
		new_ctrl |= nn->cap & NFP_NET_CFG_CTRL_L2MC;
	else
		new_ctrl &= ~NFP_NET_CFG_CTRL_L2MC;

	if (netdev->flags & IFF_PROMISC) {
		if (nn->cap & NFP_NET_CFG_CTRL_PROMISC)
			new_ctrl |= NFP_NET_CFG_CTRL_PROMISC;
		else
			nn_warn(nn, "FW does not support promiscuous mode\n");
	} else {
		new_ctrl &= ~NFP_NET_CFG_CTRL_PROMISC;
	}

	if (new_ctrl == nn->dp.ctrl)
		return;

	nn_writel(nn, NFP_NET_CFG_CTRL, new_ctrl);
	nfp_net_reconfig_post(nn, NFP_NET_CFG_UPDATE_GEN);

	nn->dp.ctrl = new_ctrl;
}

static void nfp_net_rss_init_itbl(struct nfp_net *nn)
{
	int i;

	for (i = 0; i < sizeof(nn->rss_itbl); i++)
		nn->rss_itbl[i] =
			ethtool_rxfh_indir_default(i, nn->dp.num_rx_rings);
}

static void nfp_net_dp_swap(struct nfp_net *nn, struct nfp_net_dp *dp)
{
	struct nfp_net_dp new_dp = *dp;

	*dp = nn->dp;
	nn->dp = new_dp;

	nn->dp.netdev->mtu = new_dp.mtu;

	if (!netif_is_rxfh_configured(nn->dp.netdev))
		nfp_net_rss_init_itbl(nn);
}

static int nfp_net_dp_swap_enable(struct nfp_net *nn, struct nfp_net_dp *dp)
{
	unsigned int r;
	int err;

	nfp_net_dp_swap(nn, dp);

	for (r = 0; r <	nn->max_r_vecs; r++)
		nfp_net_vector_assign_rings(&nn->dp, &nn->r_vecs[r], r);

	err = netif_set_real_num_rx_queues(nn->dp.netdev, nn->dp.num_rx_rings);
	if (err)
		return err;

	if (nn->dp.netdev->real_num_tx_queues != nn->dp.num_stack_tx_rings) {
		err = netif_set_real_num_tx_queues(nn->dp.netdev,
						   nn->dp.num_stack_tx_rings);
		if (err)
			return err;
	}

	return nfp_net_set_config_and_enable(nn);
}

struct nfp_net_dp *nfp_net_clone_dp(struct nfp_net *nn)
{
	struct nfp_net_dp *new;

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return NULL;

	*new = nn->dp;

	/* Clear things which need to be recomputed */
	new->fl_bufsz = 0;
	new->tx_rings = NULL;
	new->rx_rings = NULL;
	new->num_r_vecs = 0;
	new->num_stack_tx_rings = 0;

	return new;
}

static int
nfp_net_check_config(struct nfp_net *nn, struct nfp_net_dp *dp,
		     struct netlink_ext_ack *extack)
{
	/* XDP-enabled tests */
	if (!dp->xdp_prog)
		return 0;
	if (dp->fl_bufsz > PAGE_SIZE) {
		NL_SET_ERR_MSG_MOD(extack, "MTU too large w/ XDP enabled");
		return -EINVAL;
	}
	if (dp->num_tx_rings > nn->max_tx_rings) {
		NL_SET_ERR_MSG_MOD(extack, "Insufficient number of TX rings w/ XDP enabled");
		return -EINVAL;
	}

	return 0;
}

int nfp_net_ring_reconfig(struct nfp_net *nn, struct nfp_net_dp *dp,
			  struct netlink_ext_ack *extack)
{
	int r, err;

	dp->fl_bufsz = nfp_net_calc_fl_bufsz(dp);

	dp->num_stack_tx_rings = dp->num_tx_rings;
	if (dp->xdp_prog)
		dp->num_stack_tx_rings -= dp->num_rx_rings;

	dp->num_r_vecs = max(dp->num_rx_rings, dp->num_stack_tx_rings);

	err = nfp_net_check_config(nn, dp, extack);
	if (err)
		goto exit_free_dp;

	if (!netif_running(dp->netdev)) {
		nfp_net_dp_swap(nn, dp);
		err = 0;
		goto exit_free_dp;
	}

	/* Prepare new rings */
	for (r = nn->dp.num_r_vecs; r < dp->num_r_vecs; r++) {
		err = nfp_net_prepare_vector(nn, &nn->r_vecs[r], r);
		if (err) {
			dp->num_r_vecs = r;
			goto err_cleanup_vecs;
		}
	}

	err = nfp_net_rx_rings_prepare(nn, dp);
	if (err)
		goto err_cleanup_vecs;

	err = nfp_net_tx_rings_prepare(nn, dp);
	if (err)
		goto err_free_rx;

	/* Stop device, swap in new rings, try to start the firmware */
	nfp_net_close_stack(nn);
	nfp_net_clear_config_and_disable(nn);

	err = nfp_net_dp_swap_enable(nn, dp);
	if (err) {
		int err2;

		nfp_net_clear_config_and_disable(nn);

		/* Try with old configuration and old rings */
		err2 = nfp_net_dp_swap_enable(nn, dp);
		if (err2)
			nn_err(nn, "Can't restore ring config - FW communication failed (%d,%d)\n",
			       err, err2);
	}
	for (r = dp->num_r_vecs - 1; r >= nn->dp.num_r_vecs; r--)
		nfp_net_cleanup_vector(nn, &nn->r_vecs[r]);

	nfp_net_rx_rings_free(dp);
	nfp_net_tx_rings_free(dp);

	nfp_net_open_stack(nn);
exit_free_dp:
	kfree(dp);

	return err;

err_free_rx:
	nfp_net_rx_rings_free(dp);
err_cleanup_vecs:
	for (r = dp->num_r_vecs - 1; r >= nn->dp.num_r_vecs; r--)
		nfp_net_cleanup_vector(nn, &nn->r_vecs[r]);
	kfree(dp);
	return err;
}

static int nfp_net_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct nfp_net *nn = netdev_priv(netdev);
	struct nfp_net_dp *dp;
	int err;

	err = nfp_app_check_mtu(nn->app, netdev, new_mtu);
	if (err)
		return err;

	dp = nfp_net_clone_dp(nn);
	if (!dp)
		return -ENOMEM;

	dp->mtu = new_mtu;

	return nfp_net_ring_reconfig(nn, dp, NULL);
}

static int
nfp_net_vlan_rx_add_vid(struct net_device *netdev, __be16 proto, u16 vid)
{
	const u32 cmd = NFP_NET_CFG_MBOX_CMD_CTAG_FILTER_ADD;
	struct nfp_net *nn = netdev_priv(netdev);
	int err;

	/* Priority tagged packets with vlan id 0 are processed by the
	 * NFP as untagged packets
	 */
	if (!vid)
		return 0;

	err = nfp_net_mbox_lock(nn, NFP_NET_CFG_VLAN_FILTER_SZ);
	if (err)
		return err;

	nn_writew(nn, nn->tlv_caps.mbox_off + NFP_NET_CFG_VLAN_FILTER_VID, vid);
	nn_writew(nn, nn->tlv_caps.mbox_off + NFP_NET_CFG_VLAN_FILTER_PROTO,
		  ETH_P_8021Q);

	return nfp_net_mbox_reconfig_and_unlock(nn, cmd);
}

static int
nfp_net_vlan_rx_kill_vid(struct net_device *netdev, __be16 proto, u16 vid)
{
	const u32 cmd = NFP_NET_CFG_MBOX_CMD_CTAG_FILTER_KILL;
	struct nfp_net *nn = netdev_priv(netdev);
	int err;

	/* Priority tagged packets with vlan id 0 are processed by the
	 * NFP as untagged packets
	 */
	if (!vid)
		return 0;

	err = nfp_net_mbox_lock(nn, NFP_NET_CFG_VLAN_FILTER_SZ);
	if (err)
		return err;

	nn_writew(nn, nn->tlv_caps.mbox_off + NFP_NET_CFG_VLAN_FILTER_VID, vid);
	nn_writew(nn, nn->tlv_caps.mbox_off + NFP_NET_CFG_VLAN_FILTER_PROTO,
		  ETH_P_8021Q);

	return nfp_net_mbox_reconfig_and_unlock(nn, cmd);
}

static void nfp_net_stat64(struct net_device *netdev,
			   struct rtnl_link_stats64 *stats)
{
	struct nfp_net *nn = netdev_priv(netdev);
	int r;

	/* Collect software stats */
	for (r = 0; r < nn->max_r_vecs; r++) {
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

	/* Add in device stats */
	stats->multicast += nn_readq(nn, NFP_NET_CFG_STATS_RX_MC_FRAMES);
	stats->rx_dropped += nn_readq(nn, NFP_NET_CFG_STATS_RX_DISCARDS);
	stats->rx_errors += nn_readq(nn, NFP_NET_CFG_STATS_RX_ERRORS);

	stats->tx_dropped += nn_readq(nn, NFP_NET_CFG_STATS_TX_DISCARDS);
	stats->tx_errors += nn_readq(nn, NFP_NET_CFG_STATS_TX_ERRORS);
}

static int nfp_net_set_features(struct net_device *netdev,
				netdev_features_t features)
{
	netdev_features_t changed = netdev->features ^ features;
	struct nfp_net *nn = netdev_priv(netdev);
	u32 new_ctrl;
	int err;

	/* Assume this is not called with features we have not advertised */

	new_ctrl = nn->dp.ctrl;

	if (changed & NETIF_F_RXCSUM) {
		if (features & NETIF_F_RXCSUM)
			new_ctrl |= nn->cap & NFP_NET_CFG_CTRL_RXCSUM_ANY;
		else
			new_ctrl &= ~NFP_NET_CFG_CTRL_RXCSUM_ANY;
	}

	if (changed & (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM)) {
		if (features & (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM))
			new_ctrl |= NFP_NET_CFG_CTRL_TXCSUM;
		else
			new_ctrl &= ~NFP_NET_CFG_CTRL_TXCSUM;
	}

	if (changed & (NETIF_F_TSO | NETIF_F_TSO6)) {
		if (features & (NETIF_F_TSO | NETIF_F_TSO6))
			new_ctrl |= nn->cap & NFP_NET_CFG_CTRL_LSO2 ?:
					      NFP_NET_CFG_CTRL_LSO;
		else
			new_ctrl &= ~NFP_NET_CFG_CTRL_LSO_ANY;
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

	if (changed & NETIF_F_HW_VLAN_CTAG_FILTER) {
		if (features & NETIF_F_HW_VLAN_CTAG_FILTER)
			new_ctrl |= NFP_NET_CFG_CTRL_CTAG_FILTER;
		else
			new_ctrl &= ~NFP_NET_CFG_CTRL_CTAG_FILTER;
	}

	if (changed & NETIF_F_SG) {
		if (features & NETIF_F_SG)
			new_ctrl |= NFP_NET_CFG_CTRL_GATHER;
		else
			new_ctrl &= ~NFP_NET_CFG_CTRL_GATHER;
	}

	err = nfp_port_set_features(netdev, features);
	if (err)
		return err;

	nn_dbg(nn, "Feature change 0x%llx -> 0x%llx (changed=0x%llx)\n",
	       netdev->features, features, changed);

	if (new_ctrl == nn->dp.ctrl)
		return 0;

	nn_dbg(nn, "NIC ctrl: 0x%x -> 0x%x\n", nn->dp.ctrl, new_ctrl);
	nn_writel(nn, NFP_NET_CFG_CTRL, new_ctrl);
	err = nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_GEN);
	if (err)
		return err;

	nn->dp.ctrl = new_ctrl;

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

		/* Assume worst case scenario of having longest possible
		 * metadata prepend - 8B
		 */
		if (unlikely(hdrlen > NFP_NET_LSO_MAX_HDR_SZ - 8))
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
		return features & ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);
	}

	if (skb->inner_protocol_type != ENCAP_TYPE_ETHER ||
	    skb->inner_protocol != htons(ETH_P_TEB) ||
	    (l4_hdr != IPPROTO_UDP && l4_hdr != IPPROTO_GRE) ||
	    (l4_hdr == IPPROTO_UDP &&
	     (skb_inner_mac_header(skb) - skb_transport_header(skb) !=
	      sizeof(struct udphdr) + sizeof(struct vxlanhdr))))
		return features & ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);

	return features;
}

static int
nfp_net_get_phys_port_name(struct net_device *netdev, char *name, size_t len)
{
	struct nfp_net *nn = netdev_priv(netdev);
	int n;

	/* If port is defined, devlink_port is registered and devlink core
	 * is taking care of name formatting.
	 */
	if (nn->port)
		return -EOPNOTSUPP;

	if (nn->dp.is_vf || nn->vnic_no_name)
		return -EOPNOTSUPP;

	n = snprintf(name, len, "n%d", nn->id);
	if (n >= len)
		return -EINVAL;

	return 0;
}

static int nfp_net_xdp_setup_drv(struct nfp_net *nn, struct netdev_bpf *bpf)
{
	struct bpf_prog *prog = bpf->prog;
	struct nfp_net_dp *dp;
	int err;

	if (!prog == !nn->dp.xdp_prog) {
		WRITE_ONCE(nn->dp.xdp_prog, prog);
		xdp_attachment_setup(&nn->xdp, bpf);
		return 0;
	}

	dp = nfp_net_clone_dp(nn);
	if (!dp)
		return -ENOMEM;

	dp->xdp_prog = prog;
	dp->num_tx_rings += prog ? nn->dp.num_rx_rings : -nn->dp.num_rx_rings;
	dp->rx_dma_dir = prog ? DMA_BIDIRECTIONAL : DMA_FROM_DEVICE;
	dp->rx_dma_off = prog ? XDP_PACKET_HEADROOM - nn->dp.rx_offset : 0;

	/* We need RX reconfig to remap the buffers (BIDIR vs FROM_DEV) */
	err = nfp_net_ring_reconfig(nn, dp, bpf->extack);
	if (err)
		return err;

	xdp_attachment_setup(&nn->xdp, bpf);
	return 0;
}

static int nfp_net_xdp_setup_hw(struct nfp_net *nn, struct netdev_bpf *bpf)
{
	int err;

	err = nfp_app_xdp_offload(nn->app, nn, bpf->prog, bpf->extack);
	if (err)
		return err;

	xdp_attachment_setup(&nn->xdp_hw, bpf);
	return 0;
}

static int nfp_net_xdp(struct net_device *netdev, struct netdev_bpf *xdp)
{
	struct nfp_net *nn = netdev_priv(netdev);

	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return nfp_net_xdp_setup_drv(nn, xdp);
	case XDP_SETUP_PROG_HW:
		return nfp_net_xdp_setup_hw(nn, xdp);
	default:
		return nfp_app_bpf(nn->app, nn, xdp);
	}
}

static int nfp_net_set_mac_address(struct net_device *netdev, void *addr)
{
	struct nfp_net *nn = netdev_priv(netdev);
	struct sockaddr *saddr = addr;
	int err;

	err = eth_prepare_mac_addr_change(netdev, addr);
	if (err)
		return err;

	nfp_net_write_mac_addr(nn, saddr->sa_data);

	err = nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_MACADDR);
	if (err)
		return err;

	eth_commit_mac_addr_change(netdev, addr);

	return 0;
}

const struct net_device_ops nfp_net_netdev_ops = {
	.ndo_init		= nfp_app_ndo_init,
	.ndo_uninit		= nfp_app_ndo_uninit,
	.ndo_open		= nfp_net_netdev_open,
	.ndo_stop		= nfp_net_netdev_close,
	.ndo_start_xmit		= nfp_net_tx,
	.ndo_get_stats64	= nfp_net_stat64,
	.ndo_vlan_rx_add_vid	= nfp_net_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= nfp_net_vlan_rx_kill_vid,
	.ndo_set_vf_mac         = nfp_app_set_vf_mac,
	.ndo_set_vf_vlan        = nfp_app_set_vf_vlan,
	.ndo_set_vf_spoofchk    = nfp_app_set_vf_spoofchk,
	.ndo_set_vf_trust	= nfp_app_set_vf_trust,
	.ndo_get_vf_config	= nfp_app_get_vf_config,
	.ndo_set_vf_link_state  = nfp_app_set_vf_link_state,
	.ndo_setup_tc		= nfp_port_setup_tc,
	.ndo_tx_timeout		= nfp_net_tx_timeout,
	.ndo_set_rx_mode	= nfp_net_set_rx_mode,
	.ndo_change_mtu		= nfp_net_change_mtu,
	.ndo_set_mac_address	= nfp_net_set_mac_address,
	.ndo_set_features	= nfp_net_set_features,
	.ndo_features_check	= nfp_net_features_check,
	.ndo_get_phys_port_name	= nfp_net_get_phys_port_name,
	.ndo_udp_tunnel_add	= udp_tunnel_nic_add_port,
	.ndo_udp_tunnel_del	= udp_tunnel_nic_del_port,
	.ndo_bpf		= nfp_net_xdp,
	.ndo_get_devlink_port	= nfp_devlink_get_devlink_port,
};

static int nfp_udp_tunnel_sync(struct net_device *netdev, unsigned int table)
{
	struct nfp_net *nn = netdev_priv(netdev);
	int i;

	BUILD_BUG_ON(NFP_NET_N_VXLAN_PORTS & 1);
	for (i = 0; i < NFP_NET_N_VXLAN_PORTS; i += 2) {
		struct udp_tunnel_info ti0, ti1;

		udp_tunnel_nic_get_port(netdev, table, i, &ti0);
		udp_tunnel_nic_get_port(netdev, table, i + 1, &ti1);

		nn_writel(nn, NFP_NET_CFG_VXLAN_PORT + i * sizeof(ti0.port),
			  be16_to_cpu(ti1.port) << 16 | be16_to_cpu(ti0.port));
	}

	return nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_VXLAN);
}

static const struct udp_tunnel_nic_info nfp_udp_tunnels = {
	.sync_table     = nfp_udp_tunnel_sync,
	.flags          = UDP_TUNNEL_NIC_INFO_MAY_SLEEP |
			  UDP_TUNNEL_NIC_INFO_OPEN_ONLY,
	.tables         = {
		{
			.n_entries      = NFP_NET_N_VXLAN_PORTS,
			.tunnel_types   = UDP_TUNNEL_TYPE_VXLAN,
		},
	},
};

/**
 * nfp_net_info() - Print general info about the NIC
 * @nn:      NFP Net device to reconfigure
 */
void nfp_net_info(struct nfp_net *nn)
{
	nn_info(nn, "Netronome NFP-6xxx %sNetdev: TxQs=%d/%d RxQs=%d/%d\n",
		nn->dp.is_vf ? "VF " : "",
		nn->dp.num_tx_rings, nn->max_tx_rings,
		nn->dp.num_rx_rings, nn->max_rx_rings);
	nn_info(nn, "VER: %d.%d.%d.%d, Maximum supported MTU: %d\n",
		nn->fw_ver.resv, nn->fw_ver.class,
		nn->fw_ver.major, nn->fw_ver.minor,
		nn->max_mtu);
	nn_info(nn, "CAP: %#x %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
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
		nn->cap & NFP_NET_CFG_CTRL_LSO      ? "TSO1 "     : "",
		nn->cap & NFP_NET_CFG_CTRL_LSO2     ? "TSO2 "     : "",
		nn->cap & NFP_NET_CFG_CTRL_RSS      ? "RSS1 "     : "",
		nn->cap & NFP_NET_CFG_CTRL_RSS2     ? "RSS2 "     : "",
		nn->cap & NFP_NET_CFG_CTRL_CTAG_FILTER ? "CTAG_FILTER " : "",
		nn->cap & NFP_NET_CFG_CTRL_MSIXAUTO ? "AUTOMASK " : "",
		nn->cap & NFP_NET_CFG_CTRL_IRQMOD   ? "IRQMOD "   : "",
		nn->cap & NFP_NET_CFG_CTRL_VXLAN    ? "VXLAN "    : "",
		nn->cap & NFP_NET_CFG_CTRL_NVGRE    ? "NVGRE "	  : "",
		nn->cap & NFP_NET_CFG_CTRL_CSUM_COMPLETE ?
						      "RXCSUM_COMPLETE " : "",
		nn->cap & NFP_NET_CFG_CTRL_LIVE_ADDR ? "LIVE_ADDR " : "",
		nfp_app_extra_cap(nn->app, nn));
}

/**
 * nfp_net_alloc() - Allocate netdev and related structure
 * @pdev:         PCI device
 * @ctrl_bar:     PCI IOMEM with vNIC config memory
 * @needs_netdev: Whether to allocate a netdev for this vNIC
 * @max_tx_rings: Maximum number of TX rings supported by device
 * @max_rx_rings: Maximum number of RX rings supported by device
 *
 * This function allocates a netdev device and fills in the initial
 * part of the @struct nfp_net structure.  In case of control device
 * nfp_net structure is allocated without the netdev.
 *
 * Return: NFP Net device structure, or ERR_PTR on error.
 */
struct nfp_net *
nfp_net_alloc(struct pci_dev *pdev, void __iomem *ctrl_bar, bool needs_netdev,
	      unsigned int max_tx_rings, unsigned int max_rx_rings)
{
	struct nfp_net *nn;
	int err;

	if (needs_netdev) {
		struct net_device *netdev;

		netdev = alloc_etherdev_mqs(sizeof(struct nfp_net),
					    max_tx_rings, max_rx_rings);
		if (!netdev)
			return ERR_PTR(-ENOMEM);

		SET_NETDEV_DEV(netdev, &pdev->dev);
		nn = netdev_priv(netdev);
		nn->dp.netdev = netdev;
	} else {
		nn = vzalloc(sizeof(*nn));
		if (!nn)
			return ERR_PTR(-ENOMEM);
	}

	nn->dp.dev = &pdev->dev;
	nn->dp.ctrl_bar = ctrl_bar;
	nn->pdev = pdev;

	nn->max_tx_rings = max_tx_rings;
	nn->max_rx_rings = max_rx_rings;

	nn->dp.num_tx_rings = min_t(unsigned int,
				    max_tx_rings, num_online_cpus());
	nn->dp.num_rx_rings = min_t(unsigned int, max_rx_rings,
				 netif_get_num_default_rss_queues());

	nn->dp.num_r_vecs = max(nn->dp.num_tx_rings, nn->dp.num_rx_rings);
	nn->dp.num_r_vecs = min_t(unsigned int,
				  nn->dp.num_r_vecs, num_online_cpus());

	nn->dp.txd_cnt = NFP_NET_TX_DESCS_DEFAULT;
	nn->dp.rxd_cnt = NFP_NET_RX_DESCS_DEFAULT;

	sema_init(&nn->bar_lock, 1);

	spin_lock_init(&nn->reconfig_lock);
	spin_lock_init(&nn->link_status_lock);

	timer_setup(&nn->reconfig_timer, nfp_net_reconfig_timer, 0);

	err = nfp_net_tlv_caps_parse(&nn->pdev->dev, nn->dp.ctrl_bar,
				     &nn->tlv_caps);
	if (err)
		goto err_free_nn;

	err = nfp_ccm_mbox_alloc(nn);
	if (err)
		goto err_free_nn;

	return nn;

err_free_nn:
	if (nn->dp.netdev)
		free_netdev(nn->dp.netdev);
	else
		vfree(nn);
	return ERR_PTR(err);
}

/**
 * nfp_net_free() - Undo what @nfp_net_alloc() did
 * @nn:      NFP Net device to reconfigure
 */
void nfp_net_free(struct nfp_net *nn)
{
	WARN_ON(timer_pending(&nn->reconfig_timer) || nn->reconfig_posted);
	nfp_ccm_mbox_free(nn);

	if (nn->dp.netdev)
		free_netdev(nn->dp.netdev);
	else
		vfree(nn);
}

/**
 * nfp_net_rss_key_sz() - Get current size of the RSS key
 * @nn:		NFP Net device instance
 *
 * Return: size of the RSS key for currently selected hash function.
 */
unsigned int nfp_net_rss_key_sz(struct nfp_net *nn)
{
	switch (nn->rss_hfunc) {
	case ETH_RSS_HASH_TOP:
		return NFP_NET_CFG_RSS_KEY_SZ;
	case ETH_RSS_HASH_XOR:
		return 0;
	case ETH_RSS_HASH_CRC32:
		return 4;
	}

	nn_warn(nn, "Unknown hash function: %u\n", nn->rss_hfunc);
	return 0;
}

/**
 * nfp_net_rss_init() - Set the initial RSS parameters
 * @nn:	     NFP Net device to reconfigure
 */
static void nfp_net_rss_init(struct nfp_net *nn)
{
	unsigned long func_bit, rss_cap_hfunc;
	u32 reg;

	/* Read the RSS function capability and select first supported func */
	reg = nn_readl(nn, NFP_NET_CFG_RSS_CAP);
	rss_cap_hfunc =	FIELD_GET(NFP_NET_CFG_RSS_CAP_HFUNC, reg);
	if (!rss_cap_hfunc)
		rss_cap_hfunc =	FIELD_GET(NFP_NET_CFG_RSS_CAP_HFUNC,
					  NFP_NET_CFG_RSS_TOEPLITZ);

	func_bit = find_first_bit(&rss_cap_hfunc, NFP_NET_CFG_RSS_HFUNCS);
	if (func_bit == NFP_NET_CFG_RSS_HFUNCS) {
		dev_warn(nn->dp.dev,
			 "Bad RSS config, defaulting to Toeplitz hash\n");
		func_bit = ETH_RSS_HASH_TOP_BIT;
	}
	nn->rss_hfunc = 1 << func_bit;

	netdev_rss_key_fill(nn->rss_key, nfp_net_rss_key_sz(nn));

	nfp_net_rss_init_itbl(nn);

	/* Enable IPv4/IPv6 TCP by default */
	nn->rss_cfg = NFP_NET_CFG_RSS_IPV4_TCP |
		      NFP_NET_CFG_RSS_IPV6_TCP |
		      FIELD_PREP(NFP_NET_CFG_RSS_HFUNC, nn->rss_hfunc) |
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

static void nfp_net_netdev_init(struct nfp_net *nn)
{
	struct net_device *netdev = nn->dp.netdev;

	nfp_net_write_mac_addr(nn, nn->dp.netdev->dev_addr);

	netdev->mtu = nn->dp.mtu;

	/* Advertise/enable offloads based on capabilities
	 *
	 * Note: netdev->features show the currently enabled features
	 * and netdev->hw_features advertises which features are
	 * supported.  By default we enable most features.
	 */
	if (nn->cap & NFP_NET_CFG_CTRL_LIVE_ADDR)
		netdev->priv_flags |= IFF_LIVE_ADDR_CHANGE;

	netdev->hw_features = NETIF_F_HIGHDMA;
	if (nn->cap & NFP_NET_CFG_CTRL_RXCSUM_ANY) {
		netdev->hw_features |= NETIF_F_RXCSUM;
		nn->dp.ctrl |= nn->cap & NFP_NET_CFG_CTRL_RXCSUM_ANY;
	}
	if (nn->cap & NFP_NET_CFG_CTRL_TXCSUM) {
		netdev->hw_features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;
		nn->dp.ctrl |= NFP_NET_CFG_CTRL_TXCSUM;
	}
	if (nn->cap & NFP_NET_CFG_CTRL_GATHER) {
		netdev->hw_features |= NETIF_F_SG;
		nn->dp.ctrl |= NFP_NET_CFG_CTRL_GATHER;
	}
	if ((nn->cap & NFP_NET_CFG_CTRL_LSO && nn->fw_ver.major > 2) ||
	    nn->cap & NFP_NET_CFG_CTRL_LSO2) {
		netdev->hw_features |= NETIF_F_TSO | NETIF_F_TSO6;
		nn->dp.ctrl |= nn->cap & NFP_NET_CFG_CTRL_LSO2 ?:
					 NFP_NET_CFG_CTRL_LSO;
	}
	if (nn->cap & NFP_NET_CFG_CTRL_RSS_ANY)
		netdev->hw_features |= NETIF_F_RXHASH;
	if (nn->cap & NFP_NET_CFG_CTRL_VXLAN) {
		if (nn->cap & NFP_NET_CFG_CTRL_LSO)
			netdev->hw_features |= NETIF_F_GSO_UDP_TUNNEL;
		netdev->udp_tunnel_nic_info = &nfp_udp_tunnels;
		nn->dp.ctrl |= NFP_NET_CFG_CTRL_VXLAN;
	}
	if (nn->cap & NFP_NET_CFG_CTRL_NVGRE) {
		if (nn->cap & NFP_NET_CFG_CTRL_LSO)
			netdev->hw_features |= NETIF_F_GSO_GRE;
		nn->dp.ctrl |= NFP_NET_CFG_CTRL_NVGRE;
	}
	if (nn->cap & (NFP_NET_CFG_CTRL_VXLAN | NFP_NET_CFG_CTRL_NVGRE))
		netdev->hw_enc_features = netdev->hw_features;

	netdev->vlan_features = netdev->hw_features;

	if (nn->cap & NFP_NET_CFG_CTRL_RXVLAN) {
		netdev->hw_features |= NETIF_F_HW_VLAN_CTAG_RX;
		nn->dp.ctrl |= NFP_NET_CFG_CTRL_RXVLAN;
	}
	if (nn->cap & NFP_NET_CFG_CTRL_TXVLAN) {
		if (nn->cap & NFP_NET_CFG_CTRL_LSO2) {
			nn_warn(nn, "Device advertises both TSO2 and TXVLAN. Refusing to enable TXVLAN.\n");
		} else {
			netdev->hw_features |= NETIF_F_HW_VLAN_CTAG_TX;
			nn->dp.ctrl |= NFP_NET_CFG_CTRL_TXVLAN;
		}
	}
	if (nn->cap & NFP_NET_CFG_CTRL_CTAG_FILTER) {
		netdev->hw_features |= NETIF_F_HW_VLAN_CTAG_FILTER;
		nn->dp.ctrl |= NFP_NET_CFG_CTRL_CTAG_FILTER;
	}

	netdev->features = netdev->hw_features;

	if (nfp_app_has_tc(nn->app) && nn->port)
		netdev->hw_features |= NETIF_F_HW_TC;

	/* Advertise but disable TSO by default. */
	netdev->features &= ~(NETIF_F_TSO | NETIF_F_TSO6);
	nn->dp.ctrl &= ~NFP_NET_CFG_CTRL_LSO_ANY;

	/* Finalise the netdev setup */
	netdev->netdev_ops = &nfp_net_netdev_ops;
	netdev->watchdog_timeo = msecs_to_jiffies(5 * 1000);

	/* MTU range: 68 - hw-specific max */
	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = nn->max_mtu;

	netdev->gso_max_segs = NFP_NET_LSO_MAX_SEGS;

	netif_carrier_off(netdev);

	nfp_net_set_ethtool_ops(netdev);
}

static int nfp_net_read_caps(struct nfp_net *nn)
{
	/* Get some of the read-only fields from the BAR */
	nn->cap = nn_readl(nn, NFP_NET_CFG_CAP);
	nn->max_mtu = nn_readl(nn, NFP_NET_CFG_MAX_MTU);

	/* ABI 4.x and ctrl vNIC always use chained metadata, in other cases
	 * we allow use of non-chained metadata if RSS(v1) is the only
	 * advertised capability requiring metadata.
	 */
	nn->dp.chained_metadata_format = nn->fw_ver.major == 4 ||
					 !nn->dp.netdev ||
					 !(nn->cap & NFP_NET_CFG_CTRL_RSS) ||
					 nn->cap & NFP_NET_CFG_CTRL_CHAIN_META;
	/* RSS(v1) uses non-chained metadata format, except in ABI 4.x where
	 * it has the same meaning as RSSv2.
	 */
	if (nn->dp.chained_metadata_format && nn->fw_ver.major != 4)
		nn->cap &= ~NFP_NET_CFG_CTRL_RSS;

	/* Determine RX packet/metadata boundary offset */
	if (nn->fw_ver.major >= 2) {
		u32 reg;

		reg = nn_readl(nn, NFP_NET_CFG_RX_OFFSET);
		if (reg > NFP_NET_MAX_PREPEND) {
			nn_err(nn, "Invalid rx offset: %d\n", reg);
			return -EINVAL;
		}
		nn->dp.rx_offset = reg;
	} else {
		nn->dp.rx_offset = NFP_NET_RX_OFFSET;
	}

	/* For control vNICs mask out the capabilities app doesn't want. */
	if (!nn->dp.netdev)
		nn->cap &= nn->app->type->ctrl_cap_mask;

	return 0;
}

/**
 * nfp_net_init() - Initialise/finalise the nfp_net structure
 * @nn:		NFP Net device structure
 *
 * Return: 0 on success or negative errno on error.
 */
int nfp_net_init(struct nfp_net *nn)
{
	int err;

	nn->dp.rx_dma_dir = DMA_FROM_DEVICE;

	err = nfp_net_read_caps(nn);
	if (err)
		return err;

	/* Set default MTU and Freelist buffer size */
	if (!nfp_net_is_data_vnic(nn) && nn->app->ctrl_mtu) {
		nn->dp.mtu = min(nn->app->ctrl_mtu, nn->max_mtu);
	} else if (nn->max_mtu < NFP_NET_DEFAULT_MTU) {
		nn->dp.mtu = nn->max_mtu;
	} else {
		nn->dp.mtu = NFP_NET_DEFAULT_MTU;
	}
	nn->dp.fl_bufsz = nfp_net_calc_fl_bufsz(&nn->dp);

	if (nfp_app_ctrl_uses_data_vnics(nn->app))
		nn->dp.ctrl |= nn->cap & NFP_NET_CFG_CTRL_CMSG_DATA;

	if (nn->cap & NFP_NET_CFG_CTRL_RSS_ANY) {
		nfp_net_rss_init(nn);
		nn->dp.ctrl |= nn->cap & NFP_NET_CFG_CTRL_RSS2 ?:
					 NFP_NET_CFG_CTRL_RSS;
	}

	/* Allow L2 Broadcast and Multicast through by default, if supported */
	if (nn->cap & NFP_NET_CFG_CTRL_L2BC)
		nn->dp.ctrl |= NFP_NET_CFG_CTRL_L2BC;

	/* Allow IRQ moderation, if supported */
	if (nn->cap & NFP_NET_CFG_CTRL_IRQMOD) {
		nfp_net_irqmod_init(nn);
		nn->dp.ctrl |= NFP_NET_CFG_CTRL_IRQMOD;
	}

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

	if (nn->dp.netdev) {
		nfp_net_netdev_init(nn);

		err = nfp_ccm_mbox_init(nn);
		if (err)
			return err;

		err = nfp_net_tls_init(nn);
		if (err)
			goto err_clean_mbox;
	}

	nfp_net_vecs_init(nn);

	if (!nn->dp.netdev)
		return 0;
	return register_netdev(nn->dp.netdev);

err_clean_mbox:
	nfp_ccm_mbox_clean(nn);
	return err;
}

/**
 * nfp_net_clean() - Undo what nfp_net_init() did.
 * @nn:		NFP Net device structure
 */
void nfp_net_clean(struct nfp_net *nn)
{
	if (!nn->dp.netdev)
		return;

	unregister_netdev(nn->dp.netdev);
	nfp_ccm_mbox_clean(nn);
	nfp_net_reconfig_wait_posted(nn);
}
