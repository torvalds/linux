/*
 * Intel I/OAT DMA Linux driver
 * Copyright(c) 2004 - 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
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
#include "dma.h"
#include "registers.h"
#include "hw.h"

#include "../dmaengine.h"

int ioat_pending_level = 4;
module_param(ioat_pending_level, int, 0644);
MODULE_PARM_DESC(ioat_pending_level,
		 "high-water mark for pushing ioat descriptors (default: 4)");
int ioat_ring_alloc_order = 8;
module_param(ioat_ring_alloc_order, int, 0644);
MODULE_PARM_DESC(ioat_ring_alloc_order,
		 "ioat+: allocate 2^n descriptors per channel (default: 8 max: 16)");
static int ioat_ring_max_alloc_order = IOAT_MAX_ORDER;
module_param(ioat_ring_max_alloc_order, int, 0644);
MODULE_PARM_DESC(ioat_ring_max_alloc_order,
		 "ioat+: upper limit for ring size (default: 16)");
static char ioat_interrupt_style[32] = "msix";
module_param_string(ioat_interrupt_style, ioat_interrupt_style,
		    sizeof(ioat_interrupt_style), 0644);
MODULE_PARM_DESC(ioat_interrupt_style,
		 "set ioat interrupt style: msix (default), msi, intx");

/**
 * ioat_dma_do_interrupt - handler used for single vector interrupt mode
 * @irq: interrupt id
 * @data: interrupt data
 */
static irqreturn_t ioat_dma_do_interrupt(int irq, void *data)
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
static irqreturn_t ioat_dma_do_interrupt_msix(int irq, void *data)
{
	struct ioatdma_chan *ioat_chan = data;

	if (test_bit(IOAT_RUN, &ioat_chan->state))
		tasklet_schedule(&ioat_chan->cleanup_task);

	return IRQ_HANDLED;
}

/* common channel initialization */
void
ioat_init_channel(struct ioatdma_device *ioat_dma,
		  struct ioatdma_chan *ioat_chan, int idx)
{
	struct dma_device *dma = &ioat_dma->dma_dev;
	struct dma_chan *c = &ioat_chan->dma_chan;
	unsigned long data = (unsigned long) c;

	ioat_chan->ioat_dma = ioat_dma;
	ioat_chan->reg_base = ioat_dma->reg_base + (0x80 * (idx + 1));
	spin_lock_init(&ioat_chan->cleanup_lock);
	ioat_chan->dma_chan.device = dma;
	dma_cookie_init(&ioat_chan->dma_chan);
	list_add_tail(&ioat_chan->dma_chan.device_node, &dma->channels);
	ioat_dma->idx[idx] = ioat_chan;
	init_timer(&ioat_chan->timer);
	ioat_chan->timer.function = ioat_dma->timer_fn;
	ioat_chan->timer.data = data;
	tasklet_init(&ioat_chan->cleanup_task, ioat_dma->cleanup_fn, data);
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
	ioat_dma->cleanup_fn((unsigned long)&ioat_chan->dma_chan);
}

dma_addr_t ioat_get_current_completion(struct ioatdma_chan *ioat_chan)
{
	dma_addr_t phys_complete;
	u64 completion;

	completion = *ioat_chan->completion;
	phys_complete = ioat_chansts_to_addr(completion);

	dev_dbg(to_dev(ioat_chan), "%s: phys_complete: %#llx\n", __func__,
		(unsigned long long) phys_complete);

	if (is_ioat_halted(completion)) {
		u32 chanerr = readl(ioat_chan->reg_base + IOAT_CHANERR_OFFSET);

		dev_err(to_dev(ioat_chan), "Channel halted, chanerr = %x\n",
			chanerr);

		/* TODO do something to salvage the situation */
	}

	return phys_complete;
}

bool ioat_cleanup_preamble(struct ioatdma_chan *ioat_chan,
			   dma_addr_t *phys_complete)
{
	*phys_complete = ioat_get_current_completion(ioat_chan);
	if (*phys_complete == ioat_chan->last_completion)
		return false;
	clear_bit(IOAT_COMPLETION_ACK, &ioat_chan->state);
	mod_timer(&ioat_chan->timer, jiffies + COMPLETION_TIMEOUT);

	return true;
}

enum dma_status
ioat_dma_tx_status(struct dma_chan *c, dma_cookie_t cookie,
		   struct dma_tx_state *txstate)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	struct ioatdma_device *ioat_dma = ioat_chan->ioat_dma;
	enum dma_status ret;

	ret = dma_cookie_status(c, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	ioat_dma->cleanup_fn((unsigned long) c);

	return dma_cookie_status(c, cookie, txstate);
}

/*
 * Perform a IOAT transaction to verify the HW works.
 */
#define IOAT_TEST_SIZE 2000

static void ioat_dma_test_callback(void *dma_async_param)
{
	struct completion *cmp = dma_async_param;

	complete(cmp);
}

/**
 * ioat_dma_self_test - Perform a IOAT transaction to verify the HW works.
 * @ioat_dma: dma device to be tested
 */
int ioat_dma_self_test(struct ioatdma_device *ioat_dma)
{
	int i;
	u8 *src;
	u8 *dest;
	struct dma_device *dma = &ioat_dma->dma_dev;
	struct device *dev = &ioat_dma->pdev->dev;
	struct dma_chan *dma_chan;
	struct dma_async_tx_descriptor *tx;
	dma_addr_t dma_dest, dma_src;
	dma_cookie_t cookie;
	int err = 0;
	struct completion cmp;
	unsigned long tmo;
	unsigned long flags;

	src = kzalloc(sizeof(u8) * IOAT_TEST_SIZE, GFP_KERNEL);
	if (!src)
		return -ENOMEM;
	dest = kzalloc(sizeof(u8) * IOAT_TEST_SIZE, GFP_KERNEL);
	if (!dest) {
		kfree(src);
		return -ENOMEM;
	}

	/* Fill in src buffer */
	for (i = 0; i < IOAT_TEST_SIZE; i++)
		src[i] = (u8)i;

	/* Start copy, using first DMA channel */
	dma_chan = container_of(dma->channels.next, struct dma_chan,
				device_node);
	if (dma->device_alloc_chan_resources(dma_chan) < 1) {
		dev_err(dev, "selftest cannot allocate chan resource\n");
		err = -ENODEV;
		goto out;
	}

	dma_src = dma_map_single(dev, src, IOAT_TEST_SIZE, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma_src)) {
		dev_err(dev, "mapping src buffer failed\n");
		goto free_resources;
	}
	dma_dest = dma_map_single(dev, dest, IOAT_TEST_SIZE, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, dma_dest)) {
		dev_err(dev, "mapping dest buffer failed\n");
		goto unmap_src;
	}
	flags = DMA_PREP_INTERRUPT;
	tx = ioat_dma->dma_dev.device_prep_dma_memcpy(dma_chan, dma_dest,
						      dma_src, IOAT_TEST_SIZE,
						      flags);
	if (!tx) {
		dev_err(dev, "Self-test prep failed, disabling\n");
		err = -ENODEV;
		goto unmap_dma;
	}

	async_tx_ack(tx);
	init_completion(&cmp);
	tx->callback = ioat_dma_test_callback;
	tx->callback_param = &cmp;
	cookie = tx->tx_submit(tx);
	if (cookie < 0) {
		dev_err(dev, "Self-test setup failed, disabling\n");
		err = -ENODEV;
		goto unmap_dma;
	}
	dma->device_issue_pending(dma_chan);

	tmo = wait_for_completion_timeout(&cmp, msecs_to_jiffies(3000));

	if (tmo == 0 ||
	    dma->device_tx_status(dma_chan, cookie, NULL)
					!= DMA_COMPLETE) {
		dev_err(dev, "Self-test copy timed out, disabling\n");
		err = -ENODEV;
		goto unmap_dma;
	}
	if (memcmp(src, dest, IOAT_TEST_SIZE)) {
		dev_err(dev, "Self-test copy failed compare, disabling\n");
		err = -ENODEV;
		goto free_resources;
	}

unmap_dma:
	dma_unmap_single(dev, dma_dest, IOAT_TEST_SIZE, DMA_FROM_DEVICE);
unmap_src:
	dma_unmap_single(dev, dma_src, IOAT_TEST_SIZE, DMA_TO_DEVICE);
free_resources:
	dma->device_free_chan_resources(dma_chan);
out:
	kfree(src);
	kfree(dest);
	return err;
}

/**
 * ioat_dma_setup_interrupts - setup interrupt handler
 * @ioat_dma: ioat dma device
 */
int ioat_dma_setup_interrupts(struct ioatdma_device *ioat_dma)
{
	struct ioatdma_chan *ioat_chan;
	struct pci_dev *pdev = ioat_dma->pdev;
	struct device *dev = &pdev->dev;
	struct msix_entry *msix;
	int i, j, msixcnt;
	int err = -EINVAL;
	u8 intrctrl = 0;

	if (!strcmp(ioat_interrupt_style, "msix"))
		goto msix;
	if (!strcmp(ioat_interrupt_style, "msi"))
		goto msi;
	if (!strcmp(ioat_interrupt_style, "intx"))
		goto intx;
	dev_err(dev, "invalid ioat_interrupt_style %s\n", ioat_interrupt_style);
	goto err_no_irq;

msix:
	/* The number of MSI-X vectors should equal the number of channels */
	msixcnt = ioat_dma->dma_dev.chancnt;
	for (i = 0; i < msixcnt; i++)
		ioat_dma->msix_entries[i].entry = i;

	err = pci_enable_msix_exact(pdev, ioat_dma->msix_entries, msixcnt);
	if (err)
		goto msi;

	for (i = 0; i < msixcnt; i++) {
		msix = &ioat_dma->msix_entries[i];
		ioat_chan = ioat_chan_by_index(ioat_dma, i);
		err = devm_request_irq(dev, msix->vector,
				       ioat_dma_do_interrupt_msix, 0,
				       "ioat-msix", ioat_chan);
		if (err) {
			for (j = 0; j < i; j++) {
				msix = &ioat_dma->msix_entries[j];
				ioat_chan = ioat_chan_by_index(ioat_dma, j);
				devm_free_irq(dev, msix->vector, ioat_chan);
			}
			goto msi;
		}
	}
	intrctrl |= IOAT_INTRCTRL_MSIX_VECTOR_CONTROL;
	ioat_dma->irq_mode = IOAT_MSIX;
	goto done;

msi:
	err = pci_enable_msi(pdev);
	if (err)
		goto intx;

	err = devm_request_irq(dev, pdev->irq, ioat_dma_do_interrupt, 0,
			       "ioat-msi", ioat_dma);
	if (err) {
		pci_disable_msi(pdev);
		goto intx;
	}
	ioat_dma->irq_mode = IOAT_MSI;
	goto done;

intx:
	err = devm_request_irq(dev, pdev->irq, ioat_dma_do_interrupt,
			       IRQF_SHARED, "ioat-intx", ioat_dma);
	if (err)
		goto err_no_irq;

	ioat_dma->irq_mode = IOAT_INTX;
done:
	if (ioat_dma->intr_quirk)
		ioat_dma->intr_quirk(ioat_dma);
	intrctrl |= IOAT_INTRCTRL_MASTER_INT_EN;
	writeb(intrctrl, ioat_dma->reg_base + IOAT_INTRCTRL_OFFSET);
	return 0;

err_no_irq:
	/* Disable all interrupt generation */
	writeb(0, ioat_dma->reg_base + IOAT_INTRCTRL_OFFSET);
	ioat_dma->irq_mode = IOAT_NOIRQ;
	dev_err(dev, "no usable interrupts\n");
	return err;
}
EXPORT_SYMBOL(ioat_dma_setup_interrupts);

static void ioat_disable_interrupts(struct ioatdma_device *ioat_dma)
{
	/* Disable all interrupt generation */
	writeb(0, ioat_dma->reg_base + IOAT_INTRCTRL_OFFSET);
}

int ioat_probe(struct ioatdma_device *ioat_dma)
{
	int err = -ENODEV;
	struct dma_device *dma = &ioat_dma->dma_dev;
	struct pci_dev *pdev = ioat_dma->pdev;
	struct device *dev = &pdev->dev;

	/* DMA coherent memory pool for DMA descriptor allocations */
	ioat_dma->dma_pool = pci_pool_create("dma_desc_pool", pdev,
					     sizeof(struct ioat_dma_descriptor),
					     64, 0);
	if (!ioat_dma->dma_pool) {
		err = -ENOMEM;
		goto err_dma_pool;
	}

	ioat_dma->completion_pool = pci_pool_create("completion_pool", pdev,
						    sizeof(u64),
						    SMP_CACHE_BYTES,
						    SMP_CACHE_BYTES);

	if (!ioat_dma->completion_pool) {
		err = -ENOMEM;
		goto err_completion_pool;
	}

	ioat_dma->enumerate_channels(ioat_dma);

	dma_cap_set(DMA_MEMCPY, dma->cap_mask);
	dma->dev = &pdev->dev;

	if (!dma->chancnt) {
		dev_err(dev, "channel enumeration error\n");
		goto err_setup_interrupts;
	}

	err = ioat_dma_setup_interrupts(ioat_dma);
	if (err)
		goto err_setup_interrupts;

	err = ioat_dma->self_test(ioat_dma);
	if (err)
		goto err_self_test;

	return 0;

err_self_test:
	ioat_disable_interrupts(ioat_dma);
err_setup_interrupts:
	pci_pool_destroy(ioat_dma->completion_pool);
err_completion_pool:
	pci_pool_destroy(ioat_dma->dma_pool);
err_dma_pool:
	return err;
}

int ioat_register(struct ioatdma_device *ioat_dma)
{
	int err = dma_async_device_register(&ioat_dma->dma_dev);

	if (err) {
		ioat_disable_interrupts(ioat_dma);
		pci_pool_destroy(ioat_dma->completion_pool);
		pci_pool_destroy(ioat_dma->dma_pool);
	}

	return err;
}

void ioat_dma_remove(struct ioatdma_device *ioat_dma)
{
	struct dma_device *dma = &ioat_dma->dma_dev;

	ioat_disable_interrupts(ioat_dma);

	ioat_kobject_del(ioat_dma);

	dma_async_device_unregister(dma);

	pci_pool_destroy(ioat_dma->dma_pool);
	pci_pool_destroy(ioat_dma->completion_pool);

	INIT_LIST_HEAD(&dma->channels);
}

void __ioat_issue_pending(struct ioatdma_chan *ioat_chan)
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

static void ioat_start_null_desc(struct ioatdma_chan *ioat_chan)
{
	spin_lock_bh(&ioat_chan->prep_lock);
	__ioat_start_null_desc(ioat_chan);
	spin_unlock_bh(&ioat_chan->prep_lock);
}

void __ioat_restart_chan(struct ioatdma_chan *ioat_chan)
{
	/* set the tail to be re-issued */
	ioat_chan->issued = ioat_chan->tail;
	ioat_chan->dmacount = 0;
	set_bit(IOAT_COMPLETION_PENDING, &ioat_chan->state);
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

int ioat_quiesce(struct ioatdma_chan *ioat_chan, unsigned long tmo)
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

int ioat_reset_sync(struct ioatdma_chan *ioat_chan, unsigned long tmo)
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

/**
 * ioat_enumerate_channels - find and initialize the device's channels
 * @ioat_dma: the ioat dma device to be enumerated
 */
int ioat_enumerate_channels(struct ioatdma_device *ioat_dma)
{
	struct ioatdma_chan *ioat_chan;
	struct device *dev = &ioat_dma->pdev->dev;
	struct dma_device *dma = &ioat_dma->dma_dev;
	u8 xfercap_log;
	int i;

	INIT_LIST_HEAD(&dma->channels);
	dma->chancnt = readb(ioat_dma->reg_base + IOAT_CHANCNT_OFFSET);
	dma->chancnt &= 0x1f; /* bits [4:0] valid */
	if (dma->chancnt > ARRAY_SIZE(ioat_dma->idx)) {
		dev_warn(dev, "(%d) exceeds max supported channels (%zu)\n",
			 dma->chancnt, ARRAY_SIZE(ioat_dma->idx));
		dma->chancnt = ARRAY_SIZE(ioat_dma->idx);
	}
	xfercap_log = readb(ioat_dma->reg_base + IOAT_XFERCAP_OFFSET);
	xfercap_log &= 0x1f; /* bits [4:0] valid */
	if (xfercap_log == 0)
		return 0;
	dev_dbg(dev, "%s: xfercap = %d\n", __func__, 1 << xfercap_log);

	for (i = 0; i < dma->chancnt; i++) {
		ioat_chan = devm_kzalloc(dev, sizeof(*ioat_chan), GFP_KERNEL);
		if (!ioat_chan)
			break;

		ioat_init_channel(ioat_dma, ioat_chan, i);
		ioat_chan->xfercap_log = xfercap_log;
		spin_lock_init(&ioat_chan->prep_lock);
		if (ioat_dma->reset_hw(ioat_chan)) {
			i = 0;
			break;
		}
	}
	dma->chancnt = i;
	return i;
}

static dma_cookie_t ioat_tx_submit_unlock(struct dma_async_tx_descriptor *tx)
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
ioat_alloc_ring_ent(struct dma_chan *chan, gfp_t flags)
{
	struct ioat_dma_descriptor *hw;
	struct ioat_ring_ent *desc;
	struct ioatdma_device *ioat_dma;
	dma_addr_t phys;

	ioat_dma = to_ioatdma_device(chan->device);
	hw = pci_pool_alloc(ioat_dma->dma_pool, flags, &phys);
	if (!hw)
		return NULL;
	memset(hw, 0, sizeof(*hw));

	desc = kmem_cache_zalloc(ioat_cache, flags);
	if (!desc) {
		pci_pool_free(ioat_dma->dma_pool, hw, phys);
		return NULL;
	}

	dma_async_tx_descriptor_init(&desc->txd, chan);
	desc->txd.tx_submit = ioat_tx_submit_unlock;
	desc->hw = hw;
	desc->txd.phys = phys;
	return desc;
}

static void
ioat_free_ring_ent(struct ioat_ring_ent *desc, struct dma_chan *chan)
{
	struct ioatdma_device *ioat_dma;

	ioat_dma = to_ioatdma_device(chan->device);
	pci_pool_free(ioat_dma->dma_pool, desc->hw, desc->txd.phys);
	kmem_cache_free(ioat_cache, desc);
}

static struct ioat_ring_ent **
ioat_alloc_ring(struct dma_chan *c, int order, gfp_t flags)
{
	struct ioat_ring_ent **ring;
	int descs = 1 << order;
	int i;

	if (order > ioat_get_max_alloc_order())
		return NULL;

	/* allocate the array to hold the software ring */
	ring = kcalloc(descs, sizeof(*ring), flags);
	if (!ring)
		return NULL;
	for (i = 0; i < descs; i++) {
		ring[i] = ioat_alloc_ring_ent(c, flags);
		if (!ring[i]) {
			while (i--)
				ioat_free_ring_ent(ring[i], c);
			kfree(ring);
			return NULL;
		}
		set_desc_id(ring[i], i);
	}

	/* link descs */
	for (i = 0; i < descs-1; i++) {
		struct ioat_ring_ent *next = ring[i+1];
		struct ioat_dma_descriptor *hw = ring[i]->hw;

		hw->next = next->txd.phys;
	}
	ring[i]->hw->next = ring[0]->txd.phys;

	return ring;
}

/**
 * ioat_free_chan_resources - release all the descriptors
 * @chan: the channel to be cleaned
 */
void ioat_free_chan_resources(struct dma_chan *c)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	struct ioatdma_device *ioat_dma = ioat_chan->ioat_dma;
	struct ioat_ring_ent *desc;
	const int total_descs = 1 << ioat_chan->alloc_order;
	int descs;
	int i;

	/* Before freeing channel resources first check
	 * if they have been previously allocated for this channel.
	 */
	if (!ioat_chan->ring)
		return;

	ioat_stop(ioat_chan);
	ioat_dma->reset_hw(ioat_chan);

	spin_lock_bh(&ioat_chan->cleanup_lock);
	spin_lock_bh(&ioat_chan->prep_lock);
	descs = ioat_ring_space(ioat_chan);
	dev_dbg(to_dev(ioat_chan), "freeing %d idle descriptors\n", descs);
	for (i = 0; i < descs; i++) {
		desc = ioat_get_ring_ent(ioat_chan, ioat_chan->head + i);
		ioat_free_ring_ent(desc, c);
	}

	if (descs < total_descs)
		dev_err(to_dev(ioat_chan), "Freeing %d in use descriptors!\n",
			total_descs - descs);

	for (i = 0; i < total_descs - descs; i++) {
		desc = ioat_get_ring_ent(ioat_chan, ioat_chan->tail + i);
		dump_desc_dbg(ioat_chan, desc);
		ioat_free_ring_ent(desc, c);
	}

	kfree(ioat_chan->ring);
	ioat_chan->ring = NULL;
	ioat_chan->alloc_order = 0;
	pci_pool_free(ioat_dma->completion_pool, ioat_chan->completion,
		      ioat_chan->completion_dma);
	spin_unlock_bh(&ioat_chan->prep_lock);
	spin_unlock_bh(&ioat_chan->cleanup_lock);

	ioat_chan->last_completion = 0;
	ioat_chan->completion_dma = 0;
	ioat_chan->dmacount = 0;
}

/* ioat_alloc_chan_resources - allocate/initialize ioat descriptor ring
 * @chan: channel to be initialized
 */
int ioat_alloc_chan_resources(struct dma_chan *c)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	struct ioat_ring_ent **ring;
	u64 status;
	int order;
	int i = 0;
	u32 chanerr;

	/* have we already been set up? */
	if (ioat_chan->ring)
		return 1 << ioat_chan->alloc_order;

	/* Setup register to interrupt and write completion status on error */
	writew(IOAT_CHANCTRL_RUN, ioat_chan->reg_base + IOAT_CHANCTRL_OFFSET);

	/* allocate a completion writeback area */
	/* doing 2 32bit writes to mmio since 1 64b write doesn't work */
	ioat_chan->completion =
		pci_pool_alloc(ioat_chan->ioat_dma->completion_pool,
			       GFP_KERNEL, &ioat_chan->completion_dma);
	if (!ioat_chan->completion)
		return -ENOMEM;

	memset(ioat_chan->completion, 0, sizeof(*ioat_chan->completion));
	writel(((u64)ioat_chan->completion_dma) & 0x00000000FFFFFFFF,
	       ioat_chan->reg_base + IOAT_CHANCMP_OFFSET_LOW);
	writel(((u64)ioat_chan->completion_dma) >> 32,
	       ioat_chan->reg_base + IOAT_CHANCMP_OFFSET_HIGH);

	order = ioat_get_alloc_order();
	ring = ioat_alloc_ring(c, order, GFP_KERNEL);
	if (!ring)
		return -ENOMEM;

	spin_lock_bh(&ioat_chan->cleanup_lock);
	spin_lock_bh(&ioat_chan->prep_lock);
	ioat_chan->ring = ring;
	ioat_chan->head = 0;
	ioat_chan->issued = 0;
	ioat_chan->tail = 0;
	ioat_chan->alloc_order = order;
	set_bit(IOAT_RUN, &ioat_chan->state);
	spin_unlock_bh(&ioat_chan->prep_lock);
	spin_unlock_bh(&ioat_chan->cleanup_lock);

	ioat_start_null_desc(ioat_chan);

	/* check that we got off the ground */
	do {
		udelay(1);
		status = ioat_chansts(ioat_chan);
	} while (i++ < 20 && !is_ioat_active(status) && !is_ioat_idle(status));

	if (is_ioat_active(status) || is_ioat_idle(status))
		return 1 << ioat_chan->alloc_order;

	chanerr = readl(ioat_chan->reg_base + IOAT_CHANERR_OFFSET);

	dev_WARN(to_dev(ioat_chan),
		"failed to start channel chanerr: %#x\n", chanerr);
	ioat_free_chan_resources(c);
	return -EFAULT;
}

bool reshape_ring(struct ioatdma_chan *ioat_chan, int order)
{
	/* reshape differs from normal ring allocation in that we want
	 * to allocate a new software ring while only
	 * extending/truncating the hardware ring
	 */
	struct dma_chan *c = &ioat_chan->dma_chan;
	const u32 curr_size = ioat_ring_size(ioat_chan);
	const u16 active = ioat_ring_active(ioat_chan);
	const u32 new_size = 1 << order;
	struct ioat_ring_ent **ring;
	u32 i;

	if (order > ioat_get_max_alloc_order())
		return false;

	/* double check that we have at least 1 free descriptor */
	if (active == curr_size)
		return false;

	/* when shrinking, verify that we can hold the current active
	 * set in the new ring
	 */
	if (active >= new_size)
		return false;

	/* allocate the array to hold the software ring */
	ring = kcalloc(new_size, sizeof(*ring), GFP_NOWAIT);
	if (!ring)
		return false;

	/* allocate/trim descriptors as needed */
	if (new_size > curr_size) {
		/* copy current descriptors to the new ring */
		for (i = 0; i < curr_size; i++) {
			u16 curr_idx = (ioat_chan->tail+i) & (curr_size-1);
			u16 new_idx = (ioat_chan->tail+i) & (new_size-1);

			ring[new_idx] = ioat_chan->ring[curr_idx];
			set_desc_id(ring[new_idx], new_idx);
		}

		/* add new descriptors to the ring */
		for (i = curr_size; i < new_size; i++) {
			u16 new_idx = (ioat_chan->tail+i) & (new_size-1);

			ring[new_idx] = ioat_alloc_ring_ent(c, GFP_NOWAIT);
			if (!ring[new_idx]) {
				while (i--) {
					u16 new_idx = (ioat_chan->tail+i) &
						       (new_size-1);

					ioat_free_ring_ent(ring[new_idx], c);
				}
				kfree(ring);
				return false;
			}
			set_desc_id(ring[new_idx], new_idx);
		}

		/* hw link new descriptors */
		for (i = curr_size-1; i < new_size; i++) {
			u16 new_idx = (ioat_chan->tail+i) & (new_size-1);
			struct ioat_ring_ent *next =
				ring[(new_idx+1) & (new_size-1)];
			struct ioat_dma_descriptor *hw = ring[new_idx]->hw;

			hw->next = next->txd.phys;
		}
	} else {
		struct ioat_dma_descriptor *hw;
		struct ioat_ring_ent *next;

		/* copy current descriptors to the new ring, dropping the
		 * removed descriptors
		 */
		for (i = 0; i < new_size; i++) {
			u16 curr_idx = (ioat_chan->tail+i) & (curr_size-1);
			u16 new_idx = (ioat_chan->tail+i) & (new_size-1);

			ring[new_idx] = ioat_chan->ring[curr_idx];
			set_desc_id(ring[new_idx], new_idx);
		}

		/* free deleted descriptors */
		for (i = new_size; i < curr_size; i++) {
			struct ioat_ring_ent *ent;

			ent = ioat_get_ring_ent(ioat_chan, ioat_chan->tail+i);
			ioat_free_ring_ent(ent, c);
		}

		/* fix up hardware ring */
		hw = ring[(ioat_chan->tail+new_size-1) & (new_size-1)]->hw;
		next = ring[(ioat_chan->tail+new_size) & (new_size-1)];
		hw->next = next->txd.phys;
	}

	dev_dbg(to_dev(ioat_chan), "%s: allocated %d descriptors\n",
		__func__, new_size);

	kfree(ioat_chan->ring);
	ioat_chan->ring = ring;
	ioat_chan->alloc_order = order;

	return true;
}

/**
 * ioat_check_space_lock - verify space and grab ring producer lock
 * @ioat: ioat,3 channel (ring) to operate on
 * @num_descs: allocation length
 */
int ioat_check_space_lock(struct ioatdma_chan *ioat_chan, int num_descs)
{
	bool retry;

 retry:
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
	retry = test_and_set_bit(IOAT_RESHAPE_PENDING, &ioat_chan->state);
	spin_unlock_bh(&ioat_chan->prep_lock);

	/* is another cpu already trying to expand the ring? */
	if (retry)
		goto retry;

	spin_lock_bh(&ioat_chan->cleanup_lock);
	spin_lock_bh(&ioat_chan->prep_lock);
	retry = reshape_ring(ioat_chan, ioat_chan->alloc_order + 1);
	clear_bit(IOAT_RESHAPE_PENDING, &ioat_chan->state);
	spin_unlock_bh(&ioat_chan->prep_lock);
	spin_unlock_bh(&ioat_chan->cleanup_lock);

	/* if we were able to expand the ring retry the allocation */
	if (retry)
		goto retry;

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
		struct ioatdma_device *ioat_dma = ioat_chan->ioat_dma;

		mod_timer(&ioat_chan->timer, jiffies + COMPLETION_TIMEOUT);
		ioat_dma->timer_fn((unsigned long)ioat_chan);
	}

	return -ENOMEM;
}

struct dma_async_tx_descriptor *
ioat_dma_prep_memcpy_lock(struct dma_chan *c, dma_addr_t dma_dest,
			   dma_addr_t dma_src, size_t len, unsigned long flags)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	struct ioat_dma_descriptor *hw;
	struct ioat_ring_ent *desc;
	dma_addr_t dst = dma_dest;
	dma_addr_t src = dma_src;
	size_t total_len = len;
	int num_descs, idx, i;

	num_descs = ioat_xferlen_to_descs(ioat_chan, len);
	if (likely(num_descs) &&
	    ioat_check_space_lock(ioat_chan, num_descs) == 0)
		idx = ioat_chan->head;
	else
		return NULL;
	i = 0;
	do {
		size_t copy = min_t(size_t, len, 1 << ioat_chan->xfercap_log);

		desc = ioat_get_ring_ent(ioat_chan, idx + i);
		hw = desc->hw;

		hw->size = copy;
		hw->ctl = 0;
		hw->src_addr = src;
		hw->dst_addr = dst;

		len -= copy;
		dst += copy;
		src += copy;
		dump_desc_dbg(ioat_chan, desc);
	} while (++i < num_descs);

	desc->txd.flags = flags;
	desc->len = total_len;
	hw->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
	hw->ctl_f.fence = !!(flags & DMA_PREP_FENCE);
	hw->ctl_f.compl_write = 1;
	dump_desc_dbg(ioat_chan, desc);
	/* we leave the channel locked to ensure in order submission */

	return &desc->txd;
}
