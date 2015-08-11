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
#include <linux/i7300_idle.h>
#include "dma.h"
#include "registers.h"
#include "hw.h"

#include "../dmaengine.h"

int ioat_pending_level = 4;
module_param(ioat_pending_level, int, 0644);
MODULE_PARM_DESC(ioat_pending_level,
		 "high-water mark for pushing ioat descriptors (default: 4)");

/**
 * ioat_dma_do_interrupt - handler used for single vector interrupt mode
 * @irq: interrupt id
 * @data: interrupt data
 */
static irqreturn_t ioat_dma_do_interrupt(int irq, void *data)
{
	struct ioatdma_device *instance = data;
	struct ioat_chan_common *chan;
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
		chan = ioat_chan_by_index(instance, bit);
		if (test_bit(IOAT_RUN, &chan->state))
			tasklet_schedule(&chan->cleanup_task);
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
	struct ioat_chan_common *chan = data;

	if (test_bit(IOAT_RUN, &chan->state))
		tasklet_schedule(&chan->cleanup_task);

	return IRQ_HANDLED;
}

/* common channel initialization */
void ioat_init_channel(struct ioatdma_device *device, struct ioat_chan_common *chan, int idx)
{
	struct dma_device *dma = &device->common;
	struct dma_chan *c = &chan->common;
	unsigned long data = (unsigned long) c;

	chan->device = device;
	chan->reg_base = device->reg_base + (0x80 * (idx + 1));
	spin_lock_init(&chan->cleanup_lock);
	chan->common.device = dma;
	dma_cookie_init(&chan->common);
	list_add_tail(&chan->common.device_node, &dma->channels);
	device->idx[idx] = chan;
	init_timer(&chan->timer);
	chan->timer.function = device->timer_fn;
	chan->timer.data = data;
	tasklet_init(&chan->cleanup_task, device->cleanup_fn, data);
}

void ioat_stop(struct ioat_chan_common *chan)
{
	struct ioatdma_device *device = chan->device;
	struct pci_dev *pdev = device->pdev;
	int chan_id = chan_num(chan);
	struct msix_entry *msix;

	/* 1/ stop irq from firing tasklets
	 * 2/ stop the tasklet from re-arming irqs
	 */
	clear_bit(IOAT_RUN, &chan->state);

	/* flush inflight interrupts */
	switch (device->irq_mode) {
	case IOAT_MSIX:
		msix = &device->msix_entries[chan_id];
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
	del_timer_sync(&chan->timer);

	/* flush inflight tasklet runs */
	tasklet_kill(&chan->cleanup_task);

	/* final cleanup now that everything is quiesced and can't re-arm */
	device->cleanup_fn((unsigned long) &chan->common);
}

dma_addr_t ioat_get_current_completion(struct ioat_chan_common *chan)
{
	dma_addr_t phys_complete;
	u64 completion;

	completion = *chan->completion;
	phys_complete = ioat_chansts_to_addr(completion);

	dev_dbg(to_dev(chan), "%s: phys_complete: %#llx\n", __func__,
		(unsigned long long) phys_complete);

	if (is_ioat_halted(completion)) {
		u32 chanerr = readl(chan->reg_base + IOAT_CHANERR_OFFSET);
		dev_err(to_dev(chan), "Channel halted, chanerr = %x\n",
			chanerr);

		/* TODO do something to salvage the situation */
	}

	return phys_complete;
}

bool ioat_cleanup_preamble(struct ioat_chan_common *chan,
			   dma_addr_t *phys_complete)
{
	*phys_complete = ioat_get_current_completion(chan);
	if (*phys_complete == chan->last_completion)
		return false;
	clear_bit(IOAT_COMPLETION_ACK, &chan->state);
	mod_timer(&chan->timer, jiffies + COMPLETION_TIMEOUT);

	return true;
}

enum dma_status
ioat_dma_tx_status(struct dma_chan *c, dma_cookie_t cookie,
		   struct dma_tx_state *txstate)
{
	struct ioat_chan_common *chan = to_chan_common(c);
	struct ioatdma_device *device = chan->device;
	enum dma_status ret;

	ret = dma_cookie_status(c, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	device->cleanup_fn((unsigned long) c);

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
 * @device: device to be tested
 */
int ioat_dma_self_test(struct ioatdma_device *device)
{
	int i;
	u8 *src;
	u8 *dest;
	struct dma_device *dma = &device->common;
	struct device *dev = &device->pdev->dev;
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
	tx = device->common.device_prep_dma_memcpy(dma_chan, dma_dest, dma_src,
						   IOAT_TEST_SIZE, flags);
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

static char ioat_interrupt_style[32] = "msix";
module_param_string(ioat_interrupt_style, ioat_interrupt_style,
		    sizeof(ioat_interrupt_style), 0644);
MODULE_PARM_DESC(ioat_interrupt_style,
		 "set ioat interrupt style: msix (default), msi, intx");

/**
 * ioat_dma_setup_interrupts - setup interrupt handler
 * @device: ioat device
 */
int ioat_dma_setup_interrupts(struct ioatdma_device *device)
{
	struct ioat_chan_common *chan;
	struct pci_dev *pdev = device->pdev;
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
	msixcnt = device->common.chancnt;
	for (i = 0; i < msixcnt; i++)
		device->msix_entries[i].entry = i;

	err = pci_enable_msix_exact(pdev, device->msix_entries, msixcnt);
	if (err)
		goto msi;

	for (i = 0; i < msixcnt; i++) {
		msix = &device->msix_entries[i];
		chan = ioat_chan_by_index(device, i);
		err = devm_request_irq(dev, msix->vector,
				       ioat_dma_do_interrupt_msix, 0,
				       "ioat-msix", chan);
		if (err) {
			for (j = 0; j < i; j++) {
				msix = &device->msix_entries[j];
				chan = ioat_chan_by_index(device, j);
				devm_free_irq(dev, msix->vector, chan);
			}
			goto msi;
		}
	}
	intrctrl |= IOAT_INTRCTRL_MSIX_VECTOR_CONTROL;
	device->irq_mode = IOAT_MSIX;
	goto done;

msi:
	err = pci_enable_msi(pdev);
	if (err)
		goto intx;

	err = devm_request_irq(dev, pdev->irq, ioat_dma_do_interrupt, 0,
			       "ioat-msi", device);
	if (err) {
		pci_disable_msi(pdev);
		goto intx;
	}
	device->irq_mode = IOAT_MSI;
	goto done;

intx:
	err = devm_request_irq(dev, pdev->irq, ioat_dma_do_interrupt,
			       IRQF_SHARED, "ioat-intx", device);
	if (err)
		goto err_no_irq;

	device->irq_mode = IOAT_INTX;
done:
	if (device->intr_quirk)
		device->intr_quirk(device);
	intrctrl |= IOAT_INTRCTRL_MASTER_INT_EN;
	writeb(intrctrl, device->reg_base + IOAT_INTRCTRL_OFFSET);
	return 0;

err_no_irq:
	/* Disable all interrupt generation */
	writeb(0, device->reg_base + IOAT_INTRCTRL_OFFSET);
	device->irq_mode = IOAT_NOIRQ;
	dev_err(dev, "no usable interrupts\n");
	return err;
}
EXPORT_SYMBOL(ioat_dma_setup_interrupts);

static void ioat_disable_interrupts(struct ioatdma_device *device)
{
	/* Disable all interrupt generation */
	writeb(0, device->reg_base + IOAT_INTRCTRL_OFFSET);
}

int ioat_probe(struct ioatdma_device *device)
{
	int err = -ENODEV;
	struct dma_device *dma = &device->common;
	struct pci_dev *pdev = device->pdev;
	struct device *dev = &pdev->dev;

	/* DMA coherent memory pool for DMA descriptor allocations */
	device->dma_pool = pci_pool_create("dma_desc_pool", pdev,
					   sizeof(struct ioat_dma_descriptor),
					   64, 0);
	if (!device->dma_pool) {
		err = -ENOMEM;
		goto err_dma_pool;
	}

	device->completion_pool = pci_pool_create("completion_pool", pdev,
						  sizeof(u64), SMP_CACHE_BYTES,
						  SMP_CACHE_BYTES);

	if (!device->completion_pool) {
		err = -ENOMEM;
		goto err_completion_pool;
	}

	device->enumerate_channels(device);

	dma_cap_set(DMA_MEMCPY, dma->cap_mask);
	dma->dev = &pdev->dev;

	if (!dma->chancnt) {
		dev_err(dev, "channel enumeration error\n");
		goto err_setup_interrupts;
	}

	err = ioat_dma_setup_interrupts(device);
	if (err)
		goto err_setup_interrupts;

	err = device->self_test(device);
	if (err)
		goto err_self_test;

	return 0;

err_self_test:
	ioat_disable_interrupts(device);
err_setup_interrupts:
	pci_pool_destroy(device->completion_pool);
err_completion_pool:
	pci_pool_destroy(device->dma_pool);
err_dma_pool:
	return err;
}

int ioat_register(struct ioatdma_device *device)
{
	int err = dma_async_device_register(&device->common);

	if (err) {
		ioat_disable_interrupts(device);
		pci_pool_destroy(device->completion_pool);
		pci_pool_destroy(device->dma_pool);
	}

	return err;
}

static ssize_t cap_show(struct dma_chan *c, char *page)
{
	struct dma_device *dma = c->device;

	return sprintf(page, "copy%s%s%s%s%s\n",
		       dma_has_cap(DMA_PQ, dma->cap_mask) ? " pq" : "",
		       dma_has_cap(DMA_PQ_VAL, dma->cap_mask) ? " pq_val" : "",
		       dma_has_cap(DMA_XOR, dma->cap_mask) ? " xor" : "",
		       dma_has_cap(DMA_XOR_VAL, dma->cap_mask) ? " xor_val" : "",
		       dma_has_cap(DMA_INTERRUPT, dma->cap_mask) ? " intr" : "");

}
struct ioat_sysfs_entry ioat_cap_attr = __ATTR_RO(cap);

static ssize_t version_show(struct dma_chan *c, char *page)
{
	struct dma_device *dma = c->device;
	struct ioatdma_device *device = to_ioatdma_device(dma);

	return sprintf(page, "%d.%d\n",
		       device->version >> 4, device->version & 0xf);
}
struct ioat_sysfs_entry ioat_version_attr = __ATTR_RO(version);

static ssize_t
ioat_attr_show(struct kobject *kobj, struct attribute *attr, char *page)
{
	struct ioat_sysfs_entry *entry;
	struct ioat_chan_common *chan;

	entry = container_of(attr, struct ioat_sysfs_entry, attr);
	chan = container_of(kobj, struct ioat_chan_common, kobj);

	if (!entry->show)
		return -EIO;
	return entry->show(&chan->common, page);
}

const struct sysfs_ops ioat_sysfs_ops = {
	.show	= ioat_attr_show,
};

void ioat_kobject_add(struct ioatdma_device *device, struct kobj_type *type)
{
	struct dma_device *dma = &device->common;
	struct dma_chan *c;

	list_for_each_entry(c, &dma->channels, device_node) {
		struct ioat_chan_common *chan = to_chan_common(c);
		struct kobject *parent = &c->dev->device.kobj;
		int err;

		err = kobject_init_and_add(&chan->kobj, type, parent, "quickdata");
		if (err) {
			dev_warn(to_dev(chan),
				 "sysfs init error (%d), continuing...\n", err);
			kobject_put(&chan->kobj);
			set_bit(IOAT_KOBJ_INIT_FAIL, &chan->state);
		}
	}
}

void ioat_kobject_del(struct ioatdma_device *device)
{
	struct dma_device *dma = &device->common;
	struct dma_chan *c;

	list_for_each_entry(c, &dma->channels, device_node) {
		struct ioat_chan_common *chan = to_chan_common(c);

		if (!test_bit(IOAT_KOBJ_INIT_FAIL, &chan->state)) {
			kobject_del(&chan->kobj);
			kobject_put(&chan->kobj);
		}
	}
}

void ioat_dma_remove(struct ioatdma_device *device)
{
	struct dma_device *dma = &device->common;

	ioat_disable_interrupts(device);

	ioat_kobject_del(device);

	dma_async_device_unregister(dma);

	pci_pool_destroy(device->dma_pool);
	pci_pool_destroy(device->completion_pool);

	INIT_LIST_HEAD(&dma->channels);
}
