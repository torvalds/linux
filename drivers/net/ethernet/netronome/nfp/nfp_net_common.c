// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2015-2019 Netronome Systems, Inc. */

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
#include <linux/ethtool.h>
#include <linux/log2.h>
#include <linux/if_vlan.h>
#include <linux/if_bridge.h>
#include <linux/random.h>
#include <linux/vmalloc.h>
#include <linux/ktime.h>

#include <net/tls.h>
#include <net/vxlan.h>
#include <net/xdp_sock_drv.h>
#include <net/xfrm.h>

#include "nfpcore/nfp_dev.h"
#include "nfpcore/nfp_nsp.h"
#include "ccm.h"
#include "nfp_app.h"
#include "nfp_net_ctrl.h"
#include "nfp_net.h"
#include "nfp_net_dp.h"
#include "nfp_net_sriov.h"
#include "nfp_net_xsk.h"
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

u32 nfp_qcp_queue_offset(const struct nfp_dev_info *dev_info, u16 queue)
{
	queue &= dev_info->qc_idx_mask;
	return dev_info->qc_addr_offset + NFP_QCP_QUEUE_ADDR_SZ * queue;
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

	/* Currently we cannot tell if it's a rx or tx interrupt,
	 * since dim does not need accurate event_ctr to calculate,
	 * we just use this counter for both rx and tx dim.
	 */
	r_vec->event_ctr++;

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
	u16 sts;

	spin_lock_irqsave(&nn->link_status_lock, flags);

	sts = nn_readw(nn, NFP_NET_CFG_STS);
	link_up = !!(sts & NFP_NET_CFG_STS_LINK);

	if (nn->link_up == link_up)
		goto out;

	nn->link_up = link_up;
	if (nn->port) {
		set_bit(NFP_PORT_CHANGED, &nn->port->flags);
		if (nn->port->link_cb)
			nn->port->link_cb(nn->port);
	}

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

struct sk_buff *
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

	datalen = skb->len - skb_tcp_all_headers(skb);
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

void nfp_net_tls_tx_undo(struct sk_buff *skb, u64 tls_handle)
{
#ifdef CONFIG_TLS_DEVICE
	struct nfp_net_tls_offload_ctx *ntls;
	u32 datalen, seq;

	if (!tls_handle)
		return;
	if (WARN_ON_ONCE(!skb->sk || !tls_is_sk_tx_device_offloaded(skb->sk)))
		return;

	datalen = skb->len - skb_tcp_all_headers(skb);
	seq = ntohl(tcp_hdr(skb)->seq);

	ntls = tls_driver_ctx(skb->sk, TLS_OFFLOAD_CTX_DIR_TX);
	if (ntls->next_seq == seq + datalen)
		ntls->next_seq = seq;
	else
		WARN_ON_ONCE(1);
#endif
}

static void nfp_net_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct nfp_net *nn = netdev_priv(netdev);

	nn_warn(nn, "TX watchdog timeout on ring: %u\n", txqueue);
}

/* Receive processing */
static unsigned int
nfp_net_calc_fl_bufsz_data(struct nfp_net_dp *dp)
{
	unsigned int fl_bufsz = 0;

	if (dp->rx_offset == NFP_NET_CFG_RX_OFFSET_DYNAMIC)
		fl_bufsz += NFP_NET_MAX_PREPEND;
	else
		fl_bufsz += dp->rx_offset;
	fl_bufsz += ETH_HLEN + VLAN_HLEN * 2 + dp->mtu;

	return fl_bufsz;
}

static unsigned int nfp_net_calc_fl_bufsz(struct nfp_net_dp *dp)
{
	unsigned int fl_bufsz;

	fl_bufsz = NFP_NET_RX_BUF_HEADROOM;
	fl_bufsz += dp->rx_dma_off;
	fl_bufsz += nfp_net_calc_fl_bufsz_data(dp);

	fl_bufsz = SKB_DATA_ALIGN(fl_bufsz);
	fl_bufsz += SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	return fl_bufsz;
}

static unsigned int nfp_net_calc_fl_bufsz_xsk(struct nfp_net_dp *dp)
{
	unsigned int fl_bufsz;

	fl_bufsz = XDP_PACKET_HEADROOM;
	fl_bufsz += nfp_net_calc_fl_bufsz_data(dp);

	return fl_bufsz;
}

/* Setup and Configuration
 */

/**
 * nfp_net_vecs_init() - Assign IRQs and setup rvecs.
 * @nn:		NFP Network structure
 */
static void nfp_net_vecs_init(struct nfp_net *nn)
{
	int numa_node = dev_to_node(&nn->pdev->dev);
	struct nfp_net_r_vector *r_vec;
	unsigned int r;

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
			tasklet_setup(&r_vec->tasklet, nn->dp.ops->ctrl_poll);
			tasklet_disable(&r_vec->tasklet);
		}

		cpumask_set_cpu(cpumask_local_spread(r, numa_node), &r_vec->affinity_mask);
	}
}

static void
nfp_net_napi_add(struct nfp_net_dp *dp, struct nfp_net_r_vector *r_vec, int idx)
{
	if (dp->netdev)
		netif_napi_add(dp->netdev, &r_vec->napi,
			       nfp_net_has_xsk_pool_slow(dp, idx) ? dp->ops->xsk_poll : dp->ops->poll);
	else
		tasklet_enable(&r_vec->tasklet);
}

static void
nfp_net_napi_del(struct nfp_net_dp *dp, struct nfp_net_r_vector *r_vec)
{
	if (dp->netdev)
		netif_napi_del(&r_vec->napi);
	else
		tasklet_disable(&r_vec->tasklet);
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

	if (nfp_net_has_xsk_pool_slow(dp, idx) || r_vec->xsk_pool) {
		r_vec->xsk_pool = dp->xdp_prog ? dp->xsk_pools[idx] : NULL;

		if (r_vec->xsk_pool)
			xsk_pool_set_rxq_info(r_vec->xsk_pool,
					      &r_vec->rx_ring->xdp_rxq);

		nfp_net_napi_del(dp, r_vec);
		nfp_net_napi_add(dp, r_vec, idx);
	}
}

static int
nfp_net_prepare_vector(struct nfp_net *nn, struct nfp_net_r_vector *r_vec,
		       int idx)
{
	int err;

	nfp_net_napi_add(&nn->dp, r_vec, idx);

	snprintf(r_vec->name, sizeof(r_vec->name),
		 "%s-rxtx-%d", nfp_net_name(nn), idx);
	err = request_irq(r_vec->irq_vector, r_vec->handler, 0, r_vec->name,
			  r_vec);
	if (err) {
		nfp_net_napi_del(&nn->dp, r_vec);
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
	nfp_net_napi_del(&nn->dp, r_vec);
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

	for (r = 0; r < nn->dp.num_rx_rings; r++) {
		nfp_net_rx_ring_reset(&nn->dp.rx_rings[r]);
		if (nfp_net_has_xsk_pool_slow(&nn->dp, nn->dp.rx_rings[r].idx))
			nfp_net_xsk_rx_bufs_free(&nn->dp.rx_rings[r]);
	}
	for (r = 0; r < nn->dp.num_tx_rings; r++)
		nfp_net_tx_ring_reset(&nn->dp, &nn->dp.tx_rings[r]);
	for (r = 0; r < nn->dp.num_r_vecs; r++)
		nfp_net_vec_clear_ring_data(nn, r);

	nn->dp.ctrl = new_ctrl;
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

	nn_writeq(nn, NFP_NET_CFG_TXRS_ENABLE,
		  U64_MAX >> (64 - nn->dp.num_tx_rings));

	nn_writeq(nn, NFP_NET_CFG_RXRS_ENABLE,
		  U64_MAX >> (64 - nn->dp.num_rx_rings));

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
	nn_writel(nn, NFP_NET_CFG_CTRL_WORD1, nn->dp.ctrl_w1);
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
	struct nfp_net_r_vector *r_vec;
	unsigned int r;

	disable_irq(nn->irq_entries[NFP_NET_IRQ_LSC_IDX].vector);
	netif_carrier_off(nn->dp.netdev);
	nn->link_up = false;

	for (r = 0; r < nn->dp.num_r_vecs; r++) {
		r_vec = &nn->r_vecs[r];

		disable_irq(r_vec->irq_vector);
		napi_disable(&r_vec->napi);

		if (r_vec->rx_ring)
			cancel_work_sync(&r_vec->rx_dim.work);

		if (r_vec->tx_ring)
			cancel_work_sync(&r_vec->tx_dim.work);
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

static void nfp_net_rx_dim_work(struct work_struct *work)
{
	struct nfp_net_r_vector *r_vec;
	unsigned int factor, value;
	struct dim_cq_moder moder;
	struct nfp_net *nn;
	struct dim *dim;

	dim = container_of(work, struct dim, work);
	moder = net_dim_get_rx_moderation(dim->mode, dim->profile_ix);
	r_vec = container_of(dim, struct nfp_net_r_vector, rx_dim);
	nn = r_vec->nfp_net;

	/* Compute factor used to convert coalesce '_usecs' parameters to
	 * ME timestamp ticks.  There are 16 ME clock cycles for each timestamp
	 * count.
	 */
	factor = nn->tlv_caps.me_freq_mhz / 16;
	if (nfp_net_coalesce_para_check(factor * moder.usec, moder.pkts))
		return;

	/* copy RX interrupt coalesce parameters */
	value = (moder.pkts << 16) | (factor * moder.usec);
	nn_writel(nn, NFP_NET_CFG_RXR_IRQ_MOD(r_vec->rx_ring->idx), value);
	(void)nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_IRQMOD);

	dim->state = DIM_START_MEASURE;
}

static void nfp_net_tx_dim_work(struct work_struct *work)
{
	struct nfp_net_r_vector *r_vec;
	unsigned int factor, value;
	struct dim_cq_moder moder;
	struct nfp_net *nn;
	struct dim *dim;

	dim = container_of(work, struct dim, work);
	moder = net_dim_get_tx_moderation(dim->mode, dim->profile_ix);
	r_vec = container_of(dim, struct nfp_net_r_vector, tx_dim);
	nn = r_vec->nfp_net;

	/* Compute factor used to convert coalesce '_usecs' parameters to
	 * ME timestamp ticks.  There are 16 ME clock cycles for each timestamp
	 * count.
	 */
	factor = nn->tlv_caps.me_freq_mhz / 16;
	if (nfp_net_coalesce_para_check(factor * moder.usec, moder.pkts))
		return;

	/* copy TX interrupt coalesce parameters */
	value = (moder.pkts << 16) | (factor * moder.usec);
	nn_writel(nn, NFP_NET_CFG_TXR_IRQ_MOD(r_vec->tx_ring->idx), value);
	(void)nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_IRQMOD);

	dim->state = DIM_START_MEASURE;
}

/**
 * nfp_net_open_stack() - Start the device from stack's perspective
 * @nn:      NFP Net device to reconfigure
 */
static void nfp_net_open_stack(struct nfp_net *nn)
{
	struct nfp_net_r_vector *r_vec;
	unsigned int r;

	for (r = 0; r < nn->dp.num_r_vecs; r++) {
		r_vec = &nn->r_vecs[r];

		if (r_vec->rx_ring) {
			INIT_WORK(&r_vec->rx_dim.work, nfp_net_rx_dim_work);
			r_vec->rx_dim.mode = DIM_CQ_PERIOD_MODE_START_FROM_EQE;
		}

		if (r_vec->tx_ring) {
			INIT_WORK(&r_vec->tx_dim.work, nfp_net_tx_dim_work);
			r_vec->tx_dim.mode = DIM_CQ_PERIOD_MODE_START_FROM_EQE;
		}

		napi_enable(&r_vec->napi);
		enable_irq(r_vec->irq_vector);
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

int nfp_net_sched_mbox_amsg_work(struct nfp_net *nn, u32 cmd, const void *data, size_t len,
				 int (*cb)(struct nfp_net *, struct nfp_mbox_amsg_entry *))
{
	struct nfp_mbox_amsg_entry *entry;

	entry = kmalloc(sizeof(*entry) + len, GFP_ATOMIC);
	if (!entry)
		return -ENOMEM;

	memcpy(entry->msg, data, len);
	entry->cmd = cmd;
	entry->cfg = cb;

	spin_lock_bh(&nn->mbox_amsg.lock);
	list_add_tail(&entry->list, &nn->mbox_amsg.list);
	spin_unlock_bh(&nn->mbox_amsg.lock);

	schedule_work(&nn->mbox_amsg.work);

	return 0;
}

static void nfp_net_mbox_amsg_work(struct work_struct *work)
{
	struct nfp_net *nn = container_of(work, struct nfp_net, mbox_amsg.work);
	struct nfp_mbox_amsg_entry *entry, *tmp;
	struct list_head tmp_list;

	INIT_LIST_HEAD(&tmp_list);

	spin_lock_bh(&nn->mbox_amsg.lock);
	list_splice_init(&nn->mbox_amsg.list, &tmp_list);
	spin_unlock_bh(&nn->mbox_amsg.lock);

	list_for_each_entry_safe(entry, tmp, &tmp_list, list) {
		int err = entry->cfg(nn, entry);

		if (err)
			nn_err(nn, "Config cmd %d to HW failed %d.\n", entry->cmd, err);

		list_del(&entry->list);
		kfree(entry);
	}
}

static int nfp_net_mc_cfg(struct nfp_net *nn, struct nfp_mbox_amsg_entry *entry)
{
	unsigned char *addr = entry->msg;
	int ret;

	ret = nfp_net_mbox_lock(nn, NFP_NET_CFG_MULTICAST_SZ);
	if (ret)
		return ret;

	nn_writel(nn, nn->tlv_caps.mbox_off + NFP_NET_CFG_MULTICAST_MAC_HI,
		  get_unaligned_be32(addr));
	nn_writew(nn, nn->tlv_caps.mbox_off + NFP_NET_CFG_MULTICAST_MAC_LO,
		  get_unaligned_be16(addr + 4));

	return nfp_net_mbox_reconfig_and_unlock(nn, entry->cmd);
}

static int nfp_net_mc_sync(struct net_device *netdev, const unsigned char *addr)
{
	struct nfp_net *nn = netdev_priv(netdev);

	if (netdev_mc_count(netdev) > NFP_NET_CFG_MAC_MC_MAX) {
		nn_err(nn, "Requested number of MC addresses (%d) exceeds maximum (%d).\n",
		       netdev_mc_count(netdev), NFP_NET_CFG_MAC_MC_MAX);
		return -EINVAL;
	}

	return nfp_net_sched_mbox_amsg_work(nn, NFP_NET_CFG_MBOX_CMD_MULTICAST_ADD, addr,
					    NFP_NET_CFG_MULTICAST_SZ, nfp_net_mc_cfg);
}

static int nfp_net_mc_unsync(struct net_device *netdev, const unsigned char *addr)
{
	struct nfp_net *nn = netdev_priv(netdev);

	return nfp_net_sched_mbox_amsg_work(nn, NFP_NET_CFG_MBOX_CMD_MULTICAST_DEL, addr,
					    NFP_NET_CFG_MULTICAST_SZ, nfp_net_mc_cfg);
}

static void nfp_net_set_rx_mode(struct net_device *netdev)
{
	struct nfp_net *nn = netdev_priv(netdev);
	u32 new_ctrl, new_ctrl_w1;

	new_ctrl = nn->dp.ctrl;
	new_ctrl_w1 = nn->dp.ctrl_w1;

	if (!netdev_mc_empty(netdev) || netdev->flags & IFF_ALLMULTI)
		new_ctrl |= nn->cap & NFP_NET_CFG_CTRL_L2MC;
	else
		new_ctrl &= ~NFP_NET_CFG_CTRL_L2MC;

	if (netdev->flags & IFF_ALLMULTI)
		new_ctrl_w1 &= ~NFP_NET_CFG_CTRL_MCAST_FILTER;
	else
		new_ctrl_w1 |= nn->cap_w1 & NFP_NET_CFG_CTRL_MCAST_FILTER;

	if (netdev->flags & IFF_PROMISC) {
		if (nn->cap & NFP_NET_CFG_CTRL_PROMISC)
			new_ctrl |= NFP_NET_CFG_CTRL_PROMISC;
		else
			nn_warn(nn, "FW does not support promiscuous mode\n");
	} else {
		new_ctrl &= ~NFP_NET_CFG_CTRL_PROMISC;
	}

	if ((nn->cap_w1 & NFP_NET_CFG_CTRL_MCAST_FILTER) &&
	    __dev_mc_sync(netdev, nfp_net_mc_sync, nfp_net_mc_unsync))
		netdev_err(netdev, "Sync mc address failed\n");

	if (new_ctrl == nn->dp.ctrl && new_ctrl_w1 == nn->dp.ctrl_w1)
		return;

	if (new_ctrl != nn->dp.ctrl)
		nn_writel(nn, NFP_NET_CFG_CTRL, new_ctrl);
	if (new_ctrl_w1 != nn->dp.ctrl_w1)
		nn_writel(nn, NFP_NET_CFG_CTRL_WORD1, new_ctrl_w1);
	nfp_net_reconfig_post(nn, NFP_NET_CFG_UPDATE_GEN);

	nn->dp.ctrl = new_ctrl;
	nn->dp.ctrl_w1 = new_ctrl_w1;
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

	err = netif_set_real_num_queues(nn->dp.netdev,
					nn->dp.num_stack_tx_rings,
					nn->dp.num_rx_rings);
	if (err)
		return err;

	return nfp_net_set_config_and_enable(nn);
}

struct nfp_net_dp *nfp_net_clone_dp(struct nfp_net *nn)
{
	struct nfp_net_dp *new;

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return NULL;

	*new = nn->dp;

	new->xsk_pools = kmemdup(new->xsk_pools,
				 array_size(nn->max_r_vecs,
					    sizeof(new->xsk_pools)),
				 GFP_KERNEL);
	if (!new->xsk_pools) {
		kfree(new);
		return NULL;
	}

	/* Clear things which need to be recomputed */
	new->fl_bufsz = 0;
	new->tx_rings = NULL;
	new->rx_rings = NULL;
	new->num_r_vecs = 0;
	new->num_stack_tx_rings = 0;
	new->txrwb = NULL;
	new->txrwb_dma = 0;

	return new;
}

static void nfp_net_free_dp(struct nfp_net_dp *dp)
{
	kfree(dp->xsk_pools);
	kfree(dp);
}

static int
nfp_net_check_config(struct nfp_net *nn, struct nfp_net_dp *dp,
		     struct netlink_ext_ack *extack)
{
	unsigned int r, xsk_min_fl_bufsz;

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

	xsk_min_fl_bufsz = nfp_net_calc_fl_bufsz_xsk(dp);
	for (r = 0; r < nn->max_r_vecs; r++) {
		if (!dp->xsk_pools[r])
			continue;

		if (xsk_pool_get_rx_frame_size(dp->xsk_pools[r]) < xsk_min_fl_bufsz) {
			NL_SET_ERR_MSG_MOD(extack,
					   "XSK buffer pool chunk size too small");
			return -EINVAL;
		}
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
	nfp_net_free_dp(dp);

	return err;

err_free_rx:
	nfp_net_rx_rings_free(dp);
err_cleanup_vecs:
	for (r = dp->num_r_vecs - 1; r >= nn->dp.num_r_vecs; r--)
		nfp_net_cleanup_vector(nn, &nn->r_vecs[r]);
	nfp_net_free_dp(dp);
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
			new_ctrl |= nn->cap & NFP_NET_CFG_CTRL_RXVLAN_V2 ?:
				    NFP_NET_CFG_CTRL_RXVLAN;
		else
			new_ctrl &= ~NFP_NET_CFG_CTRL_RXVLAN_ANY;
	}

	if (changed & NETIF_F_HW_VLAN_CTAG_TX) {
		if (features & NETIF_F_HW_VLAN_CTAG_TX)
			new_ctrl |= nn->cap & NFP_NET_CFG_CTRL_TXVLAN_V2 ?:
				    NFP_NET_CFG_CTRL_TXVLAN;
		else
			new_ctrl &= ~NFP_NET_CFG_CTRL_TXVLAN_ANY;
	}

	if (changed & NETIF_F_HW_VLAN_CTAG_FILTER) {
		if (features & NETIF_F_HW_VLAN_CTAG_FILTER)
			new_ctrl |= NFP_NET_CFG_CTRL_CTAG_FILTER;
		else
			new_ctrl &= ~NFP_NET_CFG_CTRL_CTAG_FILTER;
	}

	if (changed & NETIF_F_HW_VLAN_STAG_RX) {
		if (features & NETIF_F_HW_VLAN_STAG_RX)
			new_ctrl |= NFP_NET_CFG_CTRL_RXQINQ;
		else
			new_ctrl &= ~NFP_NET_CFG_CTRL_RXQINQ;
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
nfp_net_fix_features(struct net_device *netdev,
		     netdev_features_t features)
{
	if ((features & NETIF_F_HW_VLAN_CTAG_RX) &&
	    (features & NETIF_F_HW_VLAN_STAG_RX)) {
		if (netdev->features & NETIF_F_HW_VLAN_CTAG_RX) {
			features &= ~NETIF_F_HW_VLAN_CTAG_RX;
			netdev->wanted_features &= ~NETIF_F_HW_VLAN_CTAG_RX;
			netdev_warn(netdev,
				    "S-tag and C-tag stripping can't be enabled at the same time. Enabling S-tag stripping and disabling C-tag stripping\n");
		} else if (netdev->features & NETIF_F_HW_VLAN_STAG_RX) {
			features &= ~NETIF_F_HW_VLAN_STAG_RX;
			netdev->wanted_features &= ~NETIF_F_HW_VLAN_STAG_RX;
			netdev_warn(netdev,
				    "S-tag and C-tag stripping can't be enabled at the same time. Enabling C-tag stripping and disabling S-tag stripping\n");
		}
	}
	return features;
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

		hdrlen = skb_inner_tcp_all_headers(skb);

		/* Assume worst case scenario of having longest possible
		 * metadata prepend - 8B
		 */
		if (unlikely(hdrlen > NFP_NET_LSO_MAX_HDR_SZ - 8))
			features &= ~NETIF_F_GSO_MASK;
	}

	if (xfrm_offload(skb))
		return features;

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
	case XDP_SETUP_XSK_POOL:
		return nfp_net_xsk_setup_pool(netdev, xdp->xsk.pool,
					      xdp->xsk.queue_id);
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

static int nfp_net_bridge_getlink(struct sk_buff *skb, u32 pid, u32 seq,
				  struct net_device *dev, u32 filter_mask,
				  int nlflags)
{
	struct nfp_net *nn = netdev_priv(dev);
	u16 mode;

	if (!(nn->cap & NFP_NET_CFG_CTRL_VEPA))
		return -EOPNOTSUPP;

	mode = (nn->dp.ctrl & NFP_NET_CFG_CTRL_VEPA) ?
	       BRIDGE_MODE_VEPA : BRIDGE_MODE_VEB;

	return ndo_dflt_bridge_getlink(skb, pid, seq, dev, mode, 0, 0,
				       nlflags, filter_mask, NULL);
}

static int nfp_net_bridge_setlink(struct net_device *dev, struct nlmsghdr *nlh,
				  u16 flags, struct netlink_ext_ack *extack)
{
	struct nfp_net *nn = netdev_priv(dev);
	struct nlattr *attr, *br_spec;
	int rem, err;
	u32 new_ctrl;
	u16 mode;

	if (!(nn->cap & NFP_NET_CFG_CTRL_VEPA))
		return -EOPNOTSUPP;

	br_spec = nlmsg_find_attr(nlh, sizeof(struct ifinfomsg), IFLA_AF_SPEC);
	if (!br_spec)
		return -EINVAL;

	nla_for_each_nested(attr, br_spec, rem) {
		if (nla_type(attr) != IFLA_BRIDGE_MODE)
			continue;

		if (nla_len(attr) < sizeof(mode))
			return -EINVAL;

		new_ctrl = nn->dp.ctrl;
		mode = nla_get_u16(attr);
		if (mode == BRIDGE_MODE_VEPA)
			new_ctrl |= NFP_NET_CFG_CTRL_VEPA;
		else if (mode == BRIDGE_MODE_VEB)
			new_ctrl &= ~NFP_NET_CFG_CTRL_VEPA;
		else
			return -EOPNOTSUPP;

		if (new_ctrl == nn->dp.ctrl)
			return 0;

		nn_writel(nn, NFP_NET_CFG_CTRL, new_ctrl);
		err = nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_GEN);
		if (!err)
			nn->dp.ctrl = new_ctrl;

		return err;
	}

	return -EINVAL;
}

const struct net_device_ops nfp_nfd3_netdev_ops = {
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
	.ndo_set_vf_rate	= nfp_app_set_vf_rate,
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
	.ndo_fix_features	= nfp_net_fix_features,
	.ndo_features_check	= nfp_net_features_check,
	.ndo_get_phys_port_name	= nfp_net_get_phys_port_name,
	.ndo_bpf		= nfp_net_xdp,
	.ndo_xsk_wakeup		= nfp_net_xsk_wakeup,
	.ndo_bridge_getlink     = nfp_net_bridge_getlink,
	.ndo_bridge_setlink     = nfp_net_bridge_setlink,
};

const struct net_device_ops nfp_nfdk_netdev_ops = {
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
	.ndo_set_vf_rate	= nfp_app_set_vf_rate,
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
	.ndo_fix_features	= nfp_net_fix_features,
	.ndo_features_check	= nfp_net_features_check,
	.ndo_get_phys_port_name	= nfp_net_get_phys_port_name,
	.ndo_bpf		= nfp_net_xdp,
	.ndo_bridge_getlink     = nfp_net_bridge_getlink,
	.ndo_bridge_setlink     = nfp_net_bridge_setlink,
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
	nn_info(nn, "NFP-6xxx %sNetdev: TxQs=%d/%d RxQs=%d/%d\n",
		nn->dp.is_vf ? "VF " : "",
		nn->dp.num_tx_rings, nn->max_tx_rings,
		nn->dp.num_rx_rings, nn->max_rx_rings);
	nn_info(nn, "VER: %d.%d.%d.%d, Maximum supported MTU: %d\n",
		nn->fw_ver.extend, nn->fw_ver.class,
		nn->fw_ver.major, nn->fw_ver.minor,
		nn->max_mtu);
	nn_info(nn, "CAP: %#x %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
		nn->cap,
		nn->cap & NFP_NET_CFG_CTRL_PROMISC  ? "PROMISC "  : "",
		nn->cap & NFP_NET_CFG_CTRL_L2BC     ? "L2BCFILT " : "",
		nn->cap & NFP_NET_CFG_CTRL_L2MC     ? "L2MCFILT " : "",
		nn->cap & NFP_NET_CFG_CTRL_RXCSUM   ? "RXCSUM "   : "",
		nn->cap & NFP_NET_CFG_CTRL_TXCSUM   ? "TXCSUM "   : "",
		nn->cap & NFP_NET_CFG_CTRL_RXVLAN   ? "RXVLAN "   : "",
		nn->cap & NFP_NET_CFG_CTRL_TXVLAN   ? "TXVLAN "   : "",
		nn->cap & NFP_NET_CFG_CTRL_RXQINQ   ? "RXQINQ "   : "",
		nn->cap & NFP_NET_CFG_CTRL_RXVLAN_V2 ? "RXVLANv2 "   : "",
		nn->cap & NFP_NET_CFG_CTRL_TXVLAN_V2   ? "TXVLANv2 "   : "",
		nn->cap & NFP_NET_CFG_CTRL_SCATTER  ? "SCATTER "  : "",
		nn->cap & NFP_NET_CFG_CTRL_GATHER   ? "GATHER "   : "",
		nn->cap & NFP_NET_CFG_CTRL_LSO      ? "TSO1 "     : "",
		nn->cap & NFP_NET_CFG_CTRL_LSO2     ? "TSO2 "     : "",
		nn->cap & NFP_NET_CFG_CTRL_RSS      ? "RSS1 "     : "",
		nn->cap & NFP_NET_CFG_CTRL_RSS2     ? "RSS2 "     : "",
		nn->cap & NFP_NET_CFG_CTRL_CTAG_FILTER ? "CTAG_FILTER " : "",
		nn->cap & NFP_NET_CFG_CTRL_MSIXAUTO ? "AUTOMASK " : "",
		nn->cap & NFP_NET_CFG_CTRL_IRQMOD   ? "IRQMOD "   : "",
		nn->cap & NFP_NET_CFG_CTRL_TXRWB    ? "TXRWB "    : "",
		nn->cap & NFP_NET_CFG_CTRL_VEPA     ? "VEPA "     : "",
		nn->cap & NFP_NET_CFG_CTRL_VXLAN    ? "VXLAN "    : "",
		nn->cap & NFP_NET_CFG_CTRL_NVGRE    ? "NVGRE "	  : "",
		nn->cap & NFP_NET_CFG_CTRL_CSUM_COMPLETE ?
						      "RXCSUM_COMPLETE " : "",
		nn->cap & NFP_NET_CFG_CTRL_LIVE_ADDR ? "LIVE_ADDR " : "",
		nn->cap_w1 & NFP_NET_CFG_CTRL_MCAST_FILTER ? "MULTICAST_FILTER " : "",
		nfp_app_extra_cap(nn->app, nn));
}

/**
 * nfp_net_alloc() - Allocate netdev and related structure
 * @pdev:         PCI device
 * @dev_info:     NFP ASIC params
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
nfp_net_alloc(struct pci_dev *pdev, const struct nfp_dev_info *dev_info,
	      void __iomem *ctrl_bar, bool needs_netdev,
	      unsigned int max_tx_rings, unsigned int max_rx_rings)
{
	u64 dma_mask = dma_get_mask(&pdev->dev);
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
	nn->dev_info = dev_info;
	nn->pdev = pdev;
	nfp_net_get_fw_version(&nn->fw_ver, ctrl_bar);

	switch (FIELD_GET(NFP_NET_CFG_VERSION_DP_MASK, nn->fw_ver.extend)) {
	case NFP_NET_CFG_VERSION_DP_NFD3:
		nn->dp.ops = &nfp_nfd3_ops;
		break;
	case NFP_NET_CFG_VERSION_DP_NFDK:
		if (nn->fw_ver.major < 5) {
			dev_err(&pdev->dev,
				"NFDK must use ABI 5 or newer, found: %d\n",
				nn->fw_ver.major);
			err = -EINVAL;
			goto err_free_nn;
		}
		nn->dp.ops = &nfp_nfdk_ops;
		break;
	default:
		err = -EINVAL;
		goto err_free_nn;
	}

	if ((dma_mask & nn->dp.ops->dma_mask) != dma_mask) {
		dev_err(&pdev->dev,
			"DMA mask of loaded firmware: %llx, required DMA mask: %llx\n",
			nn->dp.ops->dma_mask, dma_mask);
		err = -EINVAL;
		goto err_free_nn;
	}

	nn->max_tx_rings = max_tx_rings;
	nn->max_rx_rings = max_rx_rings;

	nn->dp.num_tx_rings = min_t(unsigned int,
				    max_tx_rings, num_online_cpus());
	nn->dp.num_rx_rings = min_t(unsigned int, max_rx_rings,
				 netif_get_num_default_rss_queues());

	nn->dp.num_r_vecs = max(nn->dp.num_tx_rings, nn->dp.num_rx_rings);
	nn->dp.num_r_vecs = min_t(unsigned int,
				  nn->dp.num_r_vecs, num_online_cpus());
	nn->max_r_vecs = nn->dp.num_r_vecs;

	nn->dp.xsk_pools = kcalloc(nn->max_r_vecs, sizeof(nn->dp.xsk_pools),
				   GFP_KERNEL);
	if (!nn->dp.xsk_pools) {
		err = -ENOMEM;
		goto err_free_nn;
	}

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

	kfree(nn->dp.xsk_pools);
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

	nn->rx_coalesce_adapt_on   = true;
	nn->tx_coalesce_adapt_on   = true;
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

#ifdef CONFIG_NFP_NET_IPSEC
	if (nn->cap_w1 & NFP_NET_CFG_CTRL_IPSEC)
		netdev->hw_features |= NETIF_F_HW_ESP | NETIF_F_HW_ESP_TX_CSUM;
#endif

	if (nn->cap & NFP_NET_CFG_CTRL_VXLAN) {
		if (nn->cap & NFP_NET_CFG_CTRL_LSO) {
			netdev->hw_features |= NETIF_F_GSO_UDP_TUNNEL |
					       NETIF_F_GSO_UDP_TUNNEL_CSUM |
					       NETIF_F_GSO_PARTIAL;
			netdev->gso_partial_features = NETIF_F_GSO_UDP_TUNNEL_CSUM;
		}
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

	if (nn->cap & NFP_NET_CFG_CTRL_RXVLAN_ANY) {
		netdev->hw_features |= NETIF_F_HW_VLAN_CTAG_RX;
		nn->dp.ctrl |= nn->cap & NFP_NET_CFG_CTRL_RXVLAN_V2 ?:
			       NFP_NET_CFG_CTRL_RXVLAN;
	}
	if (nn->cap & NFP_NET_CFG_CTRL_TXVLAN_ANY) {
		if (nn->cap & NFP_NET_CFG_CTRL_LSO2) {
			nn_warn(nn, "Device advertises both TSO2 and TXVLAN. Refusing to enable TXVLAN.\n");
		} else {
			netdev->hw_features |= NETIF_F_HW_VLAN_CTAG_TX;
			nn->dp.ctrl |= nn->cap & NFP_NET_CFG_CTRL_TXVLAN_V2 ?:
				       NFP_NET_CFG_CTRL_TXVLAN;
		}
	}
	if (nn->cap & NFP_NET_CFG_CTRL_CTAG_FILTER) {
		netdev->hw_features |= NETIF_F_HW_VLAN_CTAG_FILTER;
		nn->dp.ctrl |= NFP_NET_CFG_CTRL_CTAG_FILTER;
	}
	if (nn->cap & NFP_NET_CFG_CTRL_RXQINQ) {
		netdev->hw_features |= NETIF_F_HW_VLAN_STAG_RX;
		nn->dp.ctrl |= NFP_NET_CFG_CTRL_RXQINQ;
	}

	netdev->features = netdev->hw_features;

	if (nfp_app_has_tc(nn->app) && nn->port)
		netdev->hw_features |= NETIF_F_HW_TC;

	/* C-Tag strip and S-Tag strip can't be supported simultaneously,
	 * so enable C-Tag strip and disable S-Tag strip by default.
	 */
	netdev->features &= ~NETIF_F_HW_VLAN_STAG_RX;
	nn->dp.ctrl &= ~NFP_NET_CFG_CTRL_RXQINQ;

	netdev->xdp_features = NETDEV_XDP_ACT_BASIC;
	if (nn->app && nn->app->type->id == NFP_APP_BPF_NIC)
		netdev->xdp_features |= NETDEV_XDP_ACT_HW_OFFLOAD;

	/* Finalise the netdev setup */
	switch (nn->dp.ops->version) {
	case NFP_NFD_VER_NFD3:
		netdev->netdev_ops = &nfp_nfd3_netdev_ops;
		netdev->xdp_features |= NETDEV_XDP_ACT_XSK_ZEROCOPY;
		break;
	case NFP_NFD_VER_NFDK:
		netdev->netdev_ops = &nfp_nfdk_netdev_ops;
		break;
	}

	netdev->watchdog_timeo = msecs_to_jiffies(5 * 1000);

	/* MTU range: 68 - hw-specific max */
	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = nn->max_mtu;

	netif_set_tso_max_segs(netdev, NFP_NET_LSO_MAX_SEGS);

	netif_carrier_off(netdev);

	nfp_net_set_ethtool_ops(netdev);
}

static int nfp_net_read_caps(struct nfp_net *nn)
{
	/* Get some of the read-only fields from the BAR */
	nn->cap = nn_readl(nn, NFP_NET_CFG_CAP);
	nn->cap_w1 = nn_readl(nn, NFP_NET_CFG_CAP_WORD1);
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

	/* Mask out NFD-version-specific features */
	nn->cap &= nn->dp.ops->cap_mask;

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

	/* Enable TX pointer writeback, if supported */
	if (nn->cap & NFP_NET_CFG_CTRL_TXRWB)
		nn->dp.ctrl |= NFP_NET_CFG_CTRL_TXRWB;

	if (nn->cap_w1 & NFP_NET_CFG_CTRL_MCAST_FILTER)
		nn->dp.ctrl_w1 |= NFP_NET_CFG_CTRL_MCAST_FILTER;

	/* Stash the re-configuration queue away.  First odd queue in TX Bar */
	nn->qcp_cfg = nn->tx_bar + NFP_QCP_QUEUE_ADDR_SZ;

	/* Make sure the FW knows the netdev is supposed to be disabled here */
	nn_writel(nn, NFP_NET_CFG_CTRL, 0);
	nn_writeq(nn, NFP_NET_CFG_TXRS_ENABLE, 0);
	nn_writeq(nn, NFP_NET_CFG_RXRS_ENABLE, 0);
	nn_writel(nn, NFP_NET_CFG_CTRL_WORD1, 0);
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

		nfp_net_ipsec_init(nn);
	}

	nfp_net_vecs_init(nn);

	if (!nn->dp.netdev)
		return 0;

	spin_lock_init(&nn->mbox_amsg.lock);
	INIT_LIST_HEAD(&nn->mbox_amsg.list);
	INIT_WORK(&nn->mbox_amsg.work, nfp_net_mbox_amsg_work);

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
	nfp_net_ipsec_clean(nn);
	nfp_ccm_mbox_clean(nn);
	flush_work(&nn->mbox_amsg.work);
	nfp_net_reconfig_wait_posted(nn);
}
