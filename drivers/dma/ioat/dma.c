// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel I/OAT DMA Linux driver
 * Copyright(c) 2004 - 2015 Intel Corporation.
 */

/*
 * This driver supports an Intel I/OAT DMA engine, which does asynchronous
 * copy operations.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/dmaengine.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/workqueue.h>
#include <linux/prefetch.h>
#include <linux/sizes.h>
#include "dma.h"
#include "registers.h"
#include "hw.h"

#include "../dmaengine.h"

static char *chanerr_str[] = {
	"DMA Transfer Source Address Error",
	"DMA Transfer Destination Address Error",
	"Next Descriptor Address Error",
	"Descriptor Error",
	"Chan Address Value Error",
	"CHANCMD Error",
	"Chipset Uncorrectable Data Integrity Error",
	"DMA Uncorrectable Data Integrity Error",
	"Read Data Error",
	"Write Data Error",
	"Descriptor Control Error",
	"Descriptor Transfer Size Error",
	"Completion Address Error",
	"Interrupt Configuration Error",
	"Super extended descriptor Address Error",
	"Unaffiliated Error",
	"CRC or XOR P Error",
	"XOR Q Error",
	"Descriptor Count Error",
	"DIF All F detect Error",
	"Guard Tag verification Error",
	"Application Tag verification Error",
	"Reference Tag verification Error",
	"Bundle Bit Error",
	"Result DIF All F detect Error",
	"Result Guard Tag verification Error",
	"Result Application Tag verification Error",
	"Result Reference Tag verification Error",
};

static void ioat_eh(struct ioatdma_chan *ioat_chan);

static void ioat_print_chanerrs(struct ioatdma_chan *ioat_chan, u32 chanerr)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(chanerr_str); i++) {
		if ((chanerr >> i) & 1) {
			dev_err(to_dev(ioat_chan), "Err(%d): %s\n",
				i, chanerr_str[i]);
		}
	}
}

/**
 * ioat_dma_do_interrupt - handler used for single vector interrupt mode
 * @irq: interrupt id
 * @data: interrupt data
 */
irqreturn_t ioat_dma_do_interrupt(int irq, void *data)
{
	struct ioatdma_device *instance = data;
	struct ioatdma_chan *ioat_chan;
	unsigned long attnstatus;
	int bit;
	u8 intrctrl;

	intrctrl = readb(instance->reg_base + IOAT_INTRCTRL_OFFSET);

	if (!(intrctrl & IOAT_INTRCTRL_MASTER_INT_EN))
		return IRQ_NONE;

	if (!(intrctrl & IOAT_INTRCTRL_INT_STATUS)) {
		writeb(intrctrl, instance->reg_base + IOAT_INTRCTRL_OFFSET);
		return IRQ_NONE;
	}

	attnstatus = readl(instance->reg_base + IOAT_ATTNSTATUS_OFFSET);
	for_each_set_bit(bit, &attnstatus, BITS_PER_LONG) {
		ioat_chan = ioat_chan_by_index(instance, bit);
		if (test_bit(IOAT_RUN, &ioat_chan->state))
			tasklet_schedule(&ioat_chan->cleanup_task);
	}

	writeb(intrctrl, instance->reg_base + IOAT_INTRCTRL_OFFSET);
	return IRQ_HANDLED;
}

/**
 * ioat_dma_do_interrupt_msix - handler used for vector-per-channel interrupt mode
 * @irq: interrupt id
 * @data: interrupt data
 */
irqreturn_t ioat_dma_do_interrupt_msix(int irq, void *data)
{
	struct ioatdma_chan *ioat_chan = data;

	if (test_bit(IOAT_RUN, &ioat_chan->state))
		tasklet_schedule(&ioat_chan->cleanup_task);

	return IRQ_HANDLED;
}

void ioat_stop(struct ioatdma_chan *ioat_chan)
{
	struct ioatdma_device *ioat_dma = ioat_chan->ioat_dma;
	struct pci_dev *pdev = ioat_dma->pdev;
	int chan_id = chan_num(ioat_chan);
	struct msix_entry *msix;

	/* 1/ stop irq from firing tasklets
	 * 2/ stop the tasklet from re-arming irqs
	 */
	clear_bit(IOAT_RUN, &ioat_chan->state);

	/* flush inflight interrupts */
	switch (ioat_dma->irq_mode) {
	case IOAT_MSIX:
		msix = &ioat_dma->msix_entries[chan_id];
		synchronize_irq(msix->vector);
		break;
	case IOAT_MSI:
	case IOAT_INTX:
		synchronize_irq(pdev->irq);
		break;
	default:
		break;
	}

	/* flush inflight timers */
	del_timer_sync(&ioat_chan->timer);

	/* flush inflight tasklet runs */
	tasklet_kill(&ioat_chan->cleanup_task);

	/* final cleanup now that everything is quiesced and can't re-arm */
	ioat_cleanup_event((unsigned long)&ioat_chan->dma_chan);
}

static void __ioat_issue_pending(struct ioatdma_chan *ioat_chan)
{
	ioat_chan->dmacount += ioat_ring_pending(ioat_chan);
	ioat_chan->issued = ioat_chan->head;
	writew(ioat_chan->dmacount,
	       ioat_chan->reg_base + IOAT_CHAN_DMACOUNT_OFFSET);
	dev_dbg(to_dev(ioat_chan),
		"%s: head: %#x tail: %#x issued: %#x count: %#x\n",
		__func__, ioat_chan->head, ioat_chan->tail,
		ioat_chan->issued, ioat_chan->dmacount);
}

void ioat_issue_pending(struct dma_chan *c)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);

	if (ioat_ring_pending(ioat_chan)) {
		spin_lock_bh(&ioat_chan->prep_lock);
		__ioat_issue_pending(ioat_chan);
		spin_unlock_bh(&ioat_chan->prep_lock);
	}
}

/**
 * ioat_update_pending - log pending descriptors
 * @ioat: ioat+ channel
 *
 * Check if the number of unsubmitted descriptors has exceeded the
 * watermark.  Called with prep_lock held
 */
static void ioat_update_pending(struct ioatdma_chan *ioat_chan)
{
	if (ioat_ring_pending(ioat_chan) > ioat_pending_level)
		__ioat_issue_pending(ioat_chan);
}

static void __ioat_start_null_desc(struct ioatdma_chan *ioat_chan)
{
	struct ioat_ring_ent *desc;
	struct ioat_dma_descriptor *hw;

	if (ioat_ring_space(ioat_chan) < 1) {
		dev_err(to_dev(ioat_chan),
			"Unable to start null desc - ring full\n");
		return;
	}

	dev_dbg(to_dev(ioat_chan),
		"%s: head: %#x tail: %#x issued: %#x\n",
		__func__, ioat_chan->head, ioat_chan->tail, ioat_chan->issued);
	desc = ioat_get_ring_ent(ioat_chan, ioat_chan->head);

	hw = desc->hw;
	hw->ctl = 0;
	hw->ctl_f.null = 1;
	hw->ctl_f.int_en = 1;
	hw->ctl_f.compl_write = 1;
	/* set size to non-zero value (channel returns error when size is 0) */
	hw->size = NULL_DESC_BUFFER_SIZE;
	hw->src_addr = 0;
	hw->dst_addr = 0;
	async_tx_ack(&desc->txd);
	ioat_set_chainaddr(ioat_chan, desc->txd.phys);
	dump_desc_dbg(ioat_chan, desc);
	/* make sure descriptors are written before we submit */
	wmb();
	ioat_chan->head += 1;
	__ioat_issue_pending(ioat_chan);
}

void ioat_start_null_desc(struct ioatdma_chan *ioat_chan)
{
	spin_lock_bh(&ioat_chan->prep_lock);
	if (!test_bit(IOAT_CHAN_DOWN, &ioat_chan->state))
		__ioat_start_null_desc(ioat_chan);
	spin_unlock_bh(&ioat_chan->prep_lock);
}

static void __ioat_restart_chan(struct ioatdma_chan *ioat_chan)
{
	/* set the tail to be re-issued */
	ioat_chan->issued = ioat_chan->tail;
	ioat_chan->dmacount = 0;
	mod_timer(&ioat_chan->timer, jiffies + COMPLETION_TIMEOUT);

	dev_dbg(to_dev(ioat_chan),
		"%s: head: %#x tail: %#x issued: %#x count: %#x\n",
		__func__, ioat_chan->head, ioat_chan->tail,
		ioat_chan->issued, ioat_chan->dmacount);

	if (ioat_ring_pending(ioat_chan)) {
		struct ioat_ring_ent *desc;

		desc = ioat_get_ring_ent(ioat_chan, ioat_chan->tail);
		ioat_set_chainaddr(ioat_chan, desc->txd.phys);
		__ioat_issue_pending(ioat_chan);
	} else
		__ioat_start_null_desc(ioat_chan);
}

static int ioat_quiesce(struct ioatdma_chan *ioat_chan, unsigned long tmo)
{
	unsigned long end = jiffies + tmo;
	int err = 0;
	u32 status;

	status = ioat_chansts(ioat_chan);
	if (is_ioat_active(status) || is_ioat_idle(status))
		ioat_suspend(ioat_chan);
	while (is_ioat_active(status) || is_ioat_idle(status)) {
		if (tmo && time_after(jiffies, end)) {
			err = -ETIMEDOUT;
			break;
		}
		status = ioat_chansts(ioat_chan);
		cpu_relax();
	}

	return err;
}

static int ioat_reset_sync(struct ioatdma_chan *ioat_chan, unsigned long tmo)
{
	unsigned long end = jiffies + tmo;
	int err = 0;

	ioat_reset(ioat_chan);
	while (ioat_reset_pending(ioat_chan)) {
		if (end && time_after(jiffies, end)) {
			err = -ETIMEDOUT;
			break;
		}
		cpu_relax();
	}

	return err;
}

static dma_cookie_t ioat_tx_submit_unlock(struct dma_async_tx_descriptor *tx)
	__releases(&ioat_chan->prep_lock)
{
	struct dma_chan *c = tx->chan;
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	dma_cookie_t cookie;

	cookie = dma_cookie_assign(tx);
	dev_dbg(to_dev(ioat_chan), "%s: cookie: %d\n", __func__, cookie);

	if (!test_and_set_bit(IOAT_CHAN_ACTIVE, &ioat_chan->state))
		mod_timer(&ioat_chan->timer, jiffies + COMPLETION_TIMEOUT);

	/* make descriptor updates visible before advancing ioat->head,
	 * this is purposefully not smp_wmb() since we are also
	 * publishing the descriptor updates to a dma device
	 */
	wmb();

	ioat_chan->head += ioat_chan->produce;

	ioat_update_pending(ioat_chan);
	spin_unlock_bh(&ioat_chan->prep_lock);

	return cookie;
}

static struct ioat_ring_ent *
ioat_alloc_ring_ent(struct dma_chan *chan, int idx, gfp_t flags)
{
	struct ioat_dma_descriptor *hw;
	struct ioat_ring_ent *desc;
	struct ioatdma_chan *ioat_chan = to_ioat_chan(chan);
	int chunk;
	dma_addr_t phys;
	u8 *pos;
	off_t offs;

	chunk = idx / IOAT_DESCS_PER_2M;
	idx &= (IOAT_DESCS_PER_2M - 1);
	offs = idx * IOAT_DESC_SZ;
	pos = (u8 *)ioat_chan->descs[chunk].virt + offs;
	phys = ioat_chan->descs[chunk].hw + offs;
	hw = (struct ioat_dma_descriptor *)pos;
	memset(hw, 0, sizeof(*hw));

	desc = kmem_cache_zalloc(ioat_cache, flags);
	if (!desc)
		return NULL;

	dma_async_tx_descriptor_init(&desc->txd, chan);
	desc->txd.tx_submit = ioat_tx_submit_unlock;
	desc->hw = hw;
	desc->txd.phys = phys;
	return desc;
}

void ioat_free_ring_ent(struct ioat_ring_ent *desc, struct dma_chan *chan)
{
	kmem_cache_free(ioat_cache, desc);
}

struct ioat_ring_ent **
ioat_alloc_ring(struct dma_chan *c, int order, gfp_t flags)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	struct ioatdma_device *ioat_dma = ioat_chan->ioat_dma;
	struct ioat_ring_ent **ring;
	int total_descs = 1 << order;
	int i, chunks;

	/* allocate the array to hold the software ring */
	ring = kcalloc(total_descs, sizeof(*ring), flags);
	if (!ring)
		return NULL;

	ioat_chan->desc_chunks = chunks = (total_descs * IOAT_DESC_SZ) / SZ_2M;

	for (i = 0; i < chunks; i++) {
		struct ioat_descs *descs = &ioat_chan->descs[i];

		descs->virt = dma_alloc_coherent(to_dev(ioat_chan),
						 SZ_2M, &descs->hw, flags);
		if (!descs->virt) {
			int idx;

			for (idx = 0; idx < i; idx++) {
				descs = &ioat_chan->descs[idx];
				dma_free_coherent(to_dev(ioat_chan), SZ_2M,
						  descs->virt, descs->hw);
				descs->virt = NULL;
				descs->hw = 0;
			}

			ioat_chan->desc_chunks = 0;
			kfree(ring);
			return NULL;
		}
	}

	for (i = 0; i < total_descs; i++) {
		ring[i] = ioat_alloc_ring_ent(c, i, flags);
		if (!ring[i]) {
			int idx;

			while (i--)
				ioat_free_ring_ent(ring[i], c);

			for (idx = 0; idx < ioat_chan->desc_chunks; idx++) {
				dma_free_coherent(to_dev(ioat_chan),
						  SZ_2M,
						  ioat_chan->descs[idx].virt,
						  ioat_chan->descs[idx].hw);
				ioat_chan->descs[idx].virt = NULL;
				ioat_chan->descs[idx].hw = 0;
			}

			ioat_chan->desc_chunks = 0;
			kfree(ring);
			return NULL;
		}
		set_desc_id(ring[i], i);
	}

	/* link descs */
	for (i = 0; i < total_descs-1; i++) {
		struct ioat_ring_ent *next = ring[i+1];
		struct ioat_dma_descriptor *hw = ring[i]->hw;

		hw->next = next->txd.phys;
	}
	ring[i]->hw->next = ring[0]->txd.phys;

	/* setup descriptor pre-fetching for v3.4 */
	if (ioat_dma->cap & IOAT_CAP_DPS) {
		u16 drsctl = IOAT_CHAN_DRSZ_2MB | IOAT_CHAN_DRS_EN;

		if (chunks == 1)
			drsctl |= IOAT_CHAN_DRS_AUTOWRAP;

		writew(drsctl, ioat_chan->reg_base + IOAT_CHAN_DRSCTL_OFFSET);

	}

	return ring;
}

/**
 * ioat_check_space_lock - verify space and grab ring producer lock
 * @ioat: ioat,3 channel (ring) to operate on
 * @num_descs: allocation length
 */
int ioat_check_space_lock(struct ioatdma_chan *ioat_chan, int num_descs)
	__acquires(&ioat_chan->prep_lock)
{
	spin_lock_bh(&ioat_chan->prep_lock);
	/* never allow the last descriptor to be consumed, we need at
	 * least one free at all times to allow for on-the-fly ring
	 * resizing.
	 */
	if (likely(ioat_ring_space(ioat_chan) > num_descs)) {
		dev_dbg(to_dev(ioat_chan), "%s: num_descs: %d (%x:%x:%x)\n",
			__func__, num_descs, ioat_chan->head,
			ioat_chan->tail, ioat_chan->issued);
		ioat_chan->produce = num_descs;
		return 0;  /* with ioat->prep_lock held */
	}
	spin_unlock_bh(&ioat_chan->prep_lock);

	dev_dbg_ratelimited(to_dev(ioat_chan),
			    "%s: ring full! num_descs: %d (%x:%x:%x)\n",
			    __func__, num_descs, ioat_chan->head,
			    ioat_chan->tail, ioat_chan->issued);

	/* progress reclaim in the allocation failure case we may be
	 * called under bh_disabled so we need to trigger the timer
	 * event directly
	 */
	if (time_is_before_jiffies(ioat_chan->timer.expires)
	    && timer_pending(&ioat_chan->timer)) {
		mod_timer(&ioat_chan->timer, jiffies + COMPLETION_TIMEOUT);
		ioat_timer_event(&ioat_chan->timer);
	}

	return -ENOMEM;
}

static bool desc_has_ext(struct ioat_ring_ent *desc)
{
	struct ioat_dma_descriptor *hw = desc->hw;

	if (hw->ctl_f.op == IOAT_OP_XOR ||
	    hw->ctl_f.op == IOAT_OP_XOR_VAL) {
		struct ioat_xor_descriptor *xor = desc->xor;

		if (src_cnt_to_sw(xor->ctl_f.src_cnt) > 5)
			return true;
	} else if (hw->ctl_f.op == IOAT_OP_PQ ||
		   hw->ctl_f.op == IOAT_OP_PQ_VAL) {
		struct ioat_pq_descriptor *pq = desc->pq;

		if (src_cnt_to_sw(pq->ctl_f.src_cnt) > 3)
			return true;
	}

	return false;
}

static void
ioat_free_sed(struct ioatdma_device *ioat_dma, struct ioat_sed_ent *sed)
{
	if (!sed)
		return;

	dma_pool_free(ioat_dma->sed_hw_pool[sed->hw_pool], sed->hw, sed->dma);
	kmem_cache_free(ioat_sed_cache, sed);
}

static u64 ioat_get_current_completion(struct ioatdma_chan *ioat_chan)
{
	u64 phys_complete;
	u64 completion;

	completion = *ioat_chan->completion;
	phys_complete = ioat_chansts_to_addr(completion);

	dev_dbg(to_dev(ioat_chan), "%s: phys_complete: %#llx\n", __func__,
		(unsigned long long) phys_complete);

	return phys_complete;
}

static bool ioat_cleanup_preamble(struct ioatdma_chan *ioat_chan,
				   u64 *phys_complete)
{
	*phys_complete = ioat_get_current_completion(ioat_chan);
	if (*phys_complete == ioat_chan->last_completion)
		return false;

	clear_bit(IOAT_COMPLETION_ACK, &ioat_chan->state);
	mod_timer(&ioat_chan->timer, jiffies + COMPLETION_TIMEOUT);

	return true;
}

static void
desc_get_errstat(struct ioatdma_chan *ioat_chan, struct ioat_ring_ent *desc)
{
	struct ioat_dma_descriptor *hw = desc->hw;

	switch (hw->ctl_f.op) {
	case IOAT_OP_PQ_VAL:
	case IOAT_OP_PQ_VAL_16S:
	{
		struct ioat_pq_descriptor *pq = desc->pq;

		/* check if there's error written */
		if (!pq->dwbes_f.wbes)
			return;

		/* need to set a chanerr var for checking to clear later */

		if (pq->dwbes_f.p_val_err)
			*desc->result |= SUM_CHECK_P_RESULT;

		if (pq->dwbes_f.q_val_err)
			*desc->result |= SUM_CHECK_Q_RESULT;

		return;
	}
	default:
		return;
	}
}

/**
 * __cleanup - reclaim used descriptors
 * @ioat: channel (ring) to clean
 */
static void __cleanup(struct ioatdma_chan *ioat_chan, dma_addr_t phys_complete)
{
	struct ioatdma_device *ioat_dma = ioat_chan->ioat_dma;
	struct ioat_ring_ent *desc;
	bool seen_current = false;
	int idx = ioat_chan->tail, i;
	u16 active;

	dev_dbg(to_dev(ioat_chan), "%s: head: %#x tail: %#x issued: %#x\n",
		__func__, ioat_chan->head, ioat_chan->tail, ioat_chan->issued);

	/*
	 * At restart of the channel, the completion address and the
	 * channel status will be 0 due to starting a new chain. Since
	 * it's new chain and the first descriptor "fails", there is
	 * nothing to clean up. We do not want to reap the entire submitted
	 * chain due to this 0 address value and then BUG.
	 */
	if (!phys_complete)
		return;

	active = ioat_ring_active(ioat_chan);
	for (i = 0; i < active && !seen_current; i++) {
		struct dma_async_tx_descriptor *tx;

		prefetch(ioat_get_ring_ent(ioat_chan, idx + i + 1));
		desc = ioat_get_ring_ent(ioat_chan, idx + i);
		dump_desc_dbg(ioat_chan, desc);

		/* set err stat if we are using dwbes */
		if (ioat_dma->cap & IOAT_CAP_DWBES)
			desc_get_errstat(ioat_chan, desc);

		tx = &desc->txd;
		if (tx->cookie) {
			dma_cookie_complete(tx);
			dma_descriptor_unmap(tx);
			dmaengine_desc_get_callback_invoke(tx, NULL);
			tx->callback = NULL;
			tx->callback_result = NULL;
		}

		if (tx->phys == phys_complete)
			seen_current = true;

		/* skip extended descriptors */
		if (desc_has_ext(desc)) {
			BUG_ON(i + 1 >= active);
			i++;
		}

		/* cleanup super extended descriptors */
		if (desc->sed) {
			ioat_free_sed(ioat_dma, desc->sed);
			desc->sed = NULL;
		}
	}

	/* finish all descriptor reads before incrementing tail */
	smp_mb();
	ioat_chan->tail = idx + i;
	/* no active descs have written a completion? */
	BUG_ON(active && !seen_current);
	ioat_chan->last_completion = phys_complete;

	if (active - i == 0) {
		dev_dbg(to_dev(ioat_chan), "%s: cancel completion timeout\n",
			__func__);
		mod_timer(&ioat_chan->timer, jiffies + IDLE_TIMEOUT);
	}

	/* microsecond delay by sysfs variable  per pending descriptor */
	if (ioat_chan->intr_coalesce != ioat_chan->prev_intr_coalesce) {
		writew(min((ioat_chan->intr_coalesce * (active - i)),
		       IOAT_INTRDELAY_MASK),
		       ioat_chan->ioat_dma->reg_base + IOAT_INTRDELAY_OFFSET);
		ioat_chan->prev_intr_coalesce = ioat_chan->intr_coalesce;
	}
}

static void ioat_cleanup(struct ioatdma_chan *ioat_chan)
{
	u64 phys_complete;

	spin_lock_bh(&ioat_chan->cleanup_lock);

	if (ioat_cleanup_preamble(ioat_chan, &phys_complete))
		__cleanup(ioat_chan, phys_complete);

	if (is_ioat_halted(*ioat_chan->completion)) {
		u32 chanerr = readl(ioat_chan->reg_base + IOAT_CHANERR_OFFSET);

		if (chanerr &
		    (IOAT_CHANERR_HANDLE_MASK | IOAT_CHANERR_RECOVER_MASK)) {
			mod_timer(&ioat_chan->timer, jiffies + IDLE_TIMEOUT);
			ioat_eh(ioat_chan);
		}
	}

	spin_unlock_bh(&ioat_chan->cleanup_lock);
}

void ioat_cleanup_event(unsigned long data)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan((void *)data);

	ioat_cleanup(ioat_chan);
	if (!test_bit(IOAT_RUN, &ioat_chan->state))
		return;
	writew(IOAT_CHANCTRL_RUN, ioat_chan->reg_base + IOAT_CHANCTRL_OFFSET);
}

static void ioat_restart_channel(struct ioatdma_chan *ioat_chan)
{
	u64 phys_complete;

	/* set the completion address register again */
	writel(lower_32_bits(ioat_chan->completion_dma),
	       ioat_chan->reg_base + IOAT_CHANCMP_OFFSET_LOW);
	writel(upper_32_bits(ioat_chan->completion_dma),
	       ioat_chan->reg_base + IOAT_CHANCMP_OFFSET_HIGH);

	ioat_quiesce(ioat_chan, 0);
	if (ioat_cleanup_preamble(ioat_chan, &phys_complete))
		__cleanup(ioat_chan, phys_complete);

	__ioat_restart_chan(ioat_chan);
}


static void ioat_abort_descs(struct ioatdma_chan *ioat_chan)
{
	struct ioatdma_device *ioat_dma = ioat_chan->ioat_dma;
	struct ioat_ring_ent *desc;
	u16 active;
	int idx = ioat_chan->tail, i;

	/*
	 * We assume that the failed descriptor has been processed.
	 * Now we are just returning all the remaining submitted
	 * descriptors to abort.
	 */
	active = ioat_ring_active(ioat_chan);

	/* we skip the failed descriptor that tail points to */
	for (i = 1; i < active; i++) {
		struct dma_async_tx_descriptor *tx;

		prefetch(ioat_get_ring_ent(ioat_chan, idx + i + 1));
		desc = ioat_get_ring_ent(ioat_chan, idx + i);

		tx = &desc->txd;
		if (tx->cookie) {
			struct dmaengine_result res;

			dma_cookie_complete(tx);
			dma_descriptor_unmap(tx);
			res.result = DMA_TRANS_ABORTED;
			dmaengine_desc_get_callback_invoke(tx, &res);
			tx->callback = NULL;
			tx->callback_result = NULL;
		}

		/* skip extended descriptors */
		if (desc_has_ext(desc)) {
			WARN_ON(i + 1 >= active);
			i++;
		}

		/* cleanup super extended descriptors */
		if (desc->sed) {
			ioat_free_sed(ioat_dma, desc->sed);
			desc->sed = NULL;
		}
	}

	smp_mb(); /* finish all descriptor reads before incrementing tail */
	ioat_chan->tail = idx + active;

	desc = ioat_get_ring_ent(ioat_chan, ioat_chan->tail);
	ioat_chan->last_completion = *ioat_chan->completion = desc->txd.phys;
}

static void ioat_eh(struct ioatdma_chan *ioat_chan)
{
	struct pci_dev *pdev = to_pdev(ioat_chan);
	struct ioat_dma_descriptor *hw;
	struct dma_async_tx_descriptor *tx;
	u64 phys_complete;
	struct ioat_ring_ent *desc;
	u32 err_handled = 0;
	u32 chanerr_int;
	u32 chanerr;
	bool abort = false;
	struct dmaengine_result res;

	/* cleanup so tail points to descriptor that caused the error */
	if (ioat_cleanup_preamble(ioat_chan, &phys_complete))
		__cleanup(ioat_chan, phys_complete);

	chanerr = readl(ioat_chan->reg_base + IOAT_CHANERR_OFFSET);
	pci_read_config_dword(pdev, IOAT_PCI_CHANERR_INT_OFFSET, &chanerr_int);

	dev_dbg(to_dev(ioat_chan), "%s: error = %x:%x\n",
		__func__, chanerr, chanerr_int);

	desc = ioat_get_ring_ent(ioat_chan, ioat_chan->tail);
	hw = desc->hw;
	dump_desc_dbg(ioat_chan, desc);

	switch (hw->ctl_f.op) {
	case IOAT_OP_XOR_VAL:
		if (chanerr & IOAT_CHANERR_XOR_P_OR_CRC_ERR) {
			*desc->result |= SUM_CHECK_P_RESULT;
			err_handled |= IOAT_CHANERR_XOR_P_OR_CRC_ERR;
		}
		break;
	case IOAT_OP_PQ_VAL:
	case IOAT_OP_PQ_VAL_16S:
		if (chanerr & IOAT_CHANERR_XOR_P_OR_CRC_ERR) {
			*desc->result |= SUM_CHECK_P_RESULT;
			err_handled |= IOAT_CHANERR_XOR_P_OR_CRC_ERR;
		}
		if (chanerr & IOAT_CHANERR_XOR_Q_ERR) {
			*desc->result |= SUM_CHECK_Q_RESULT;
			err_handled |= IOAT_CHANERR_XOR_Q_ERR;
		}
		break;
	}

	if (chanerr & IOAT_CHANERR_RECOVER_MASK) {
		if (chanerr & IOAT_CHANERR_READ_DATA_ERR) {
			res.result = DMA_TRANS_READ_FAILED;
			err_handled |= IOAT_CHANERR_READ_DATA_ERR;
		} else if (chanerr & IOAT_CHANERR_WRITE_DATA_ERR) {
			res.result = DMA_TRANS_WRITE_FAILED;
			err_handled |= IOAT_CHANERR_WRITE_DATA_ERR;
		}

		abort = true;
	} else
		res.result = DMA_TRANS_NOERROR;

	/* fault on unhandled error or spurious halt */
	if (chanerr ^ err_handled || chanerr == 0) {
		dev_err(to_dev(ioat_chan), "%s: fatal error (%x:%x)\n",
			__func__, chanerr, err_handled);
		dev_err(to_dev(ioat_chan), "Errors handled:\n");
		ioat_print_chanerrs(ioat_chan, err_handled);
		dev_err(to_dev(ioat_chan), "Errors not handled:\n");
		ioat_print_chanerrs(ioat_chan, (chanerr & ~err_handled));

		BUG();
	}

	/* cleanup the faulty descriptor since we are continuing */
	tx = &desc->txd;
	if (tx->cookie) {
		dma_cookie_complete(tx);
		dma_descriptor_unmap(tx);
		dmaengine_desc_get_callback_invoke(tx, &res);
		tx->callback = NULL;
		tx->callback_result = NULL;
	}

	/* mark faulting descriptor as complete */
	*ioat_chan->completion = desc->txd.phys;

	spin_lock_bh(&ioat_chan->prep_lock);
	/* we need abort all descriptors */
	if (abort) {
		ioat_abort_descs(ioat_chan);
		/* clean up the channel, we could be in weird state */
		ioat_reset_hw(ioat_chan);
	}

	writel(chanerr, ioat_chan->reg_base + IOAT_CHANERR_OFFSET);
	pci_write_config_dword(pdev, IOAT_PCI_CHANERR_INT_OFFSET, chanerr_int);

	ioat_restart_channel(ioat_chan);
	spin_unlock_bh(&ioat_chan->prep_lock);
}

static void check_active(struct ioatdma_chan *ioat_chan)
{
	if (ioat_ring_active(ioat_chan)) {
		mod_timer(&ioat_chan->timer, jiffies + COMPLETION_TIMEOUT);
		return;
	}

	if (test_and_clear_bit(IOAT_CHAN_ACTIVE, &ioat_chan->state))
		mod_timer(&ioat_chan->timer, jiffies + IDLE_TIMEOUT);
}

void ioat_timer_event(struct timer_list *t)
{
	struct ioatdma_chan *ioat_chan = from_timer(ioat_chan, t, timer);
	dma_addr_t phys_complete;
	u64 status;

	status = ioat_chansts(ioat_chan);

	/* when halted due to errors check for channel
	 * programming errors before advancing the completion state
	 */
	if (is_ioat_halted(status)) {
		u32 chanerr;

		chanerr = readl(ioat_chan->reg_base + IOAT_CHANERR_OFFSET);
		dev_err(to_dev(ioat_chan), "%s: Channel halted (%x)\n",
			__func__, chanerr);
		dev_err(to_dev(ioat_chan), "Errors:\n");
		ioat_print_chanerrs(ioat_chan, chanerr);

		if (test_bit(IOAT_RUN, &ioat_chan->state)) {
			spin_lock_bh(&ioat_chan->cleanup_lock);
			spin_lock_bh(&ioat_chan->prep_lock);
			set_bit(IOAT_CHAN_DOWN, &ioat_chan->state);
			spin_unlock_bh(&ioat_chan->prep_lock);

			ioat_abort_descs(ioat_chan);
			dev_warn(to_dev(ioat_chan), "Reset channel...\n");
			ioat_reset_hw(ioat_chan);
			dev_warn(to_dev(ioat_chan), "Restart channel...\n");
			ioat_restart_channel(ioat_chan);

			spin_lock_bh(&ioat_chan->prep_lock);
			clear_bit(IOAT_CHAN_DOWN, &ioat_chan->state);
			spin_unlock_bh(&ioat_chan->prep_lock);
			spin_unlock_bh(&ioat_chan->cleanup_lock);
		}

		return;
	}

	spin_lock_bh(&ioat_chan->cleanup_lock);

	/* handle the no-actives case */
	if (!ioat_ring_active(ioat_chan)) {
		spin_lock_bh(&ioat_chan->prep_lock);
		check_active(ioat_chan);
		spin_unlock_bh(&ioat_chan->prep_lock);
		spin_unlock_bh(&ioat_chan->cleanup_lock);
		return;
	}

	/* if we haven't made progress and we have already
	 * acknowledged a pending completion once, then be more
	 * forceful with a restart
	 */
	if (ioat_cleanup_preamble(ioat_chan, &phys_complete))
		__cleanup(ioat_chan, phys_complete);
	else if (test_bit(IOAT_COMPLETION_ACK, &ioat_chan->state)) {
		u32 chanerr;

		chanerr = readl(ioat_chan->reg_base + IOAT_CHANERR_OFFSET);
		dev_err(to_dev(ioat_chan), "CHANSTS: %#Lx CHANERR: %#x\n",
			status, chanerr);
		dev_err(to_dev(ioat_chan), "Errors:\n");
		ioat_print_chanerrs(ioat_chan, chanerr);

		dev_dbg(to_dev(ioat_chan), "Active descriptors: %d\n",
			ioat_ring_active(ioat_chan));

		spin_lock_bh(&ioat_chan->prep_lock);
		set_bit(IOAT_CHAN_DOWN, &ioat_chan->state);
		spin_unlock_bh(&ioat_chan->prep_lock);

		ioat_abort_descs(ioat_chan);
		dev_warn(to_dev(ioat_chan), "Resetting channel...\n");
		ioat_reset_hw(ioat_chan);
		dev_warn(to_dev(ioat_chan), "Restarting channel...\n");
		ioat_restart_channel(ioat_chan);

		spin_lock_bh(&ioat_chan->prep_lock);
		clear_bit(IOAT_CHAN_DOWN, &ioat_chan->state);
		spin_unlock_bh(&ioat_chan->prep_lock);
		spin_unlock_bh(&ioat_chan->cleanup_lock);
		return;
	} else
		set_bit(IOAT_COMPLETION_ACK, &ioat_chan->state);

	mod_timer(&ioat_chan->timer, jiffies + COMPLETION_TIMEOUT);
	spin_unlock_bh(&ioat_chan->cleanup_lock);
}

enum dma_status
ioat_tx_status(struct dma_chan *c, dma_cookie_t cookie,
		struct dma_tx_state *txstate)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	enum dma_status ret;

	ret = dma_cookie_status(c, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	ioat_cleanup(ioat_chan);

	return dma_cookie_status(c, cookie, txstate);
}

int ioat_reset_hw(struct ioatdma_chan *ioat_chan)
{
	/* throw away whatever the channel was doing and get it
	 * initialized, with ioat3 specific workarounds
	 */
	struct ioatdma_device *ioat_dma = ioat_chan->ioat_dma;
	struct pci_dev *pdev = ioat_dma->pdev;
	u32 chanerr;
	u16 dev_id;
	int err;

	ioat_quiesce(ioat_chan, msecs_to_jiffies(100));

	chanerr = readl(ioat_chan->reg_base + IOAT_CHANERR_OFFSET);
	writel(chanerr, ioat_chan->reg_base + IOAT_CHANERR_OFFSET);

	if (ioat_dma->version < IOAT_VER_3_3) {
		/* clear any pending errors */
		err = pci_read_config_dword(pdev,
				IOAT_PCI_CHANERR_INT_OFFSET, &chanerr);
		if (err) {
			dev_err(&pdev->dev,
				"channel error register unreachable\n");
			return err;
		}
		pci_write_config_dword(pdev,
				IOAT_PCI_CHANERR_INT_OFFSET, chanerr);

		/* Clear DMAUNCERRSTS Cfg-Reg Parity Error status bit
		 * (workaround for spurious config parity error after restart)
		 */
		pci_read_config_word(pdev, IOAT_PCI_DEVICE_ID_OFFSET, &dev_id);
		if (dev_id == PCI_DEVICE_ID_INTEL_IOAT_TBG0) {
			pci_write_config_dword(pdev,
					       IOAT_PCI_DMAUNCERRSTS_OFFSET,
					       0x10);
		}
	}

	if (is_bwd_ioat(pdev) && (ioat_dma->irq_mode == IOAT_MSIX)) {
		ioat_dma->msixtba0 = readq(ioat_dma->reg_base + 0x1000);
		ioat_dma->msixdata0 = readq(ioat_dma->reg_base + 0x1008);
		ioat_dma->msixpba = readq(ioat_dma->reg_base + 0x1800);
	}


	err = ioat_reset_sync(ioat_chan, msecs_to_jiffies(200));
	if (!err) {
		if (is_bwd_ioat(pdev) && (ioat_dma->irq_mode == IOAT_MSIX)) {
			writeq(ioat_dma->msixtba0, ioat_dma->reg_base + 0x1000);
			writeq(ioat_dma->msixdata0, ioat_dma->reg_base + 0x1008);
			writeq(ioat_dma->msixpba, ioat_dma->reg_base + 0x1800);
		}
	}

	if (err)
		dev_err(&pdev->dev, "Failed to reset: %d\n", err);

	return err;
}
