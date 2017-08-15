/*
 * Qualcomm Technologies HIDMA DMA engine interface
 *
 * Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Copyright (C) Freescale Semicondutor, Inc. 2007, 2008.
 * Copyright (C) Semihalf 2009
 * Copyright (C) Ilya Yanok, Emcraft Systems 2010
 * Copyright (C) Alexander Popov, Promcontroller 2014
 *
 * Written by Piotr Ziecik <kosmo@semihalf.com>. Hardware description
 * (defines, structures and comments) was taken from MPC5121 DMA driver
 * written by Hongjun Chen <hong-jun.chen@freescale.com>.
 *
 * Approved as OSADL project by a majority of OSADL members and funded
 * by OSADL membership fees in 2009;  for details see www.osadl.org.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 */

/* Linux Foundation elects GPLv2 license only. */

#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/of_dma.h>
#include <linux/property.h>
#include <linux/delay.h>
#include <linux/acpi.h>
#include <linux/irq.h>
#include <linux/atomic.h>
#include <linux/pm_runtime.h>
#include <linux/msi.h>

#include "../dmaengine.h"
#include "hidma.h"

/*
 * Default idle time is 2 seconds. This parameter can
 * be overridden by changing the following
 * /sys/bus/platform/devices/QCOM8061:<xy>/power/autosuspend_delay_ms
 * during kernel boot.
 */
#define HIDMA_AUTOSUSPEND_TIMEOUT		2000
#define HIDMA_ERR_INFO_SW			0xFF
#define HIDMA_ERR_CODE_UNEXPECTED_TERMINATE	0x0
#define HIDMA_NR_DEFAULT_DESC			10
#define HIDMA_MSI_INTS				11

static inline struct hidma_dev *to_hidma_dev(struct dma_device *dmadev)
{
	return container_of(dmadev, struct hidma_dev, ddev);
}

static inline
struct hidma_dev *to_hidma_dev_from_lldev(struct hidma_lldev **_lldevp)
{
	return container_of(_lldevp, struct hidma_dev, lldev);
}

static inline struct hidma_chan *to_hidma_chan(struct dma_chan *dmach)
{
	return container_of(dmach, struct hidma_chan, chan);
}

static inline
struct hidma_desc *to_hidma_desc(struct dma_async_tx_descriptor *t)
{
	return container_of(t, struct hidma_desc, desc);
}

static void hidma_free(struct hidma_dev *dmadev)
{
	INIT_LIST_HEAD(&dmadev->ddev.channels);
}

static unsigned int nr_desc_prm;
module_param(nr_desc_prm, uint, 0644);
MODULE_PARM_DESC(nr_desc_prm, "number of descriptors (default: 0)");


/* process completed descriptors */
static void hidma_process_completed(struct hidma_chan *mchan)
{
	struct dma_device *ddev = mchan->chan.device;
	struct hidma_dev *mdma = to_hidma_dev(ddev);
	struct dma_async_tx_descriptor *desc;
	dma_cookie_t last_cookie;
	struct hidma_desc *mdesc;
	struct hidma_desc *next;
	unsigned long irqflags;
	struct list_head list;

	INIT_LIST_HEAD(&list);

	/* Get all completed descriptors */
	spin_lock_irqsave(&mchan->lock, irqflags);
	list_splice_tail_init(&mchan->completed, &list);
	spin_unlock_irqrestore(&mchan->lock, irqflags);

	/* Execute callbacks and run dependencies */
	list_for_each_entry_safe(mdesc, next, &list, node) {
		enum dma_status llstat;
		struct dmaengine_desc_callback cb;
		struct dmaengine_result result;

		desc = &mdesc->desc;
		last_cookie = desc->cookie;

		spin_lock_irqsave(&mchan->lock, irqflags);
		dma_cookie_complete(desc);
		spin_unlock_irqrestore(&mchan->lock, irqflags);

		llstat = hidma_ll_status(mdma->lldev, mdesc->tre_ch);
		dmaengine_desc_get_callback(desc, &cb);

		dma_run_dependencies(desc);

		spin_lock_irqsave(&mchan->lock, irqflags);
		list_move(&mdesc->node, &mchan->free);

		if (llstat == DMA_COMPLETE) {
			mchan->last_success = last_cookie;
			result.result = DMA_TRANS_NOERROR;
		} else
			result.result = DMA_TRANS_ABORTED;

		spin_unlock_irqrestore(&mchan->lock, irqflags);

		dmaengine_desc_callback_invoke(&cb, &result);
	}
}

/*
 * Called once for each submitted descriptor.
 * PM is locked once for each descriptor that is currently
 * in execution.
 */
static void hidma_callback(void *data)
{
	struct hidma_desc *mdesc = data;
	struct hidma_chan *mchan = to_hidma_chan(mdesc->desc.chan);
	struct dma_device *ddev = mchan->chan.device;
	struct hidma_dev *dmadev = to_hidma_dev(ddev);
	unsigned long irqflags;
	bool queued = false;

	spin_lock_irqsave(&mchan->lock, irqflags);
	if (mdesc->node.next) {
		/* Delete from the active list, add to completed list */
		list_move_tail(&mdesc->node, &mchan->completed);
		queued = true;

		/* calculate the next running descriptor */
		mchan->running = list_first_entry(&mchan->active,
						  struct hidma_desc, node);
	}
	spin_unlock_irqrestore(&mchan->lock, irqflags);

	hidma_process_completed(mchan);

	if (queued) {
		pm_runtime_mark_last_busy(dmadev->ddev.dev);
		pm_runtime_put_autosuspend(dmadev->ddev.dev);
	}
}

static int hidma_chan_init(struct hidma_dev *dmadev, u32 dma_sig)
{
	struct hidma_chan *mchan;
	struct dma_device *ddev;

	mchan = devm_kzalloc(dmadev->ddev.dev, sizeof(*mchan), GFP_KERNEL);
	if (!mchan)
		return -ENOMEM;

	ddev = &dmadev->ddev;
	mchan->dma_sig = dma_sig;
	mchan->dmadev = dmadev;
	mchan->chan.device = ddev;
	dma_cookie_init(&mchan->chan);

	INIT_LIST_HEAD(&mchan->free);
	INIT_LIST_HEAD(&mchan->prepared);
	INIT_LIST_HEAD(&mchan->active);
	INIT_LIST_HEAD(&mchan->completed);
	INIT_LIST_HEAD(&mchan->queued);

	spin_lock_init(&mchan->lock);
	list_add_tail(&mchan->chan.device_node, &ddev->channels);
	dmadev->ddev.chancnt++;
	return 0;
}

static void hidma_issue_task(unsigned long arg)
{
	struct hidma_dev *dmadev = (struct hidma_dev *)arg;

	pm_runtime_get_sync(dmadev->ddev.dev);
	hidma_ll_start(dmadev->lldev);
}

static void hidma_issue_pending(struct dma_chan *dmach)
{
	struct hidma_chan *mchan = to_hidma_chan(dmach);
	struct hidma_dev *dmadev = mchan->dmadev;
	unsigned long flags;
	struct hidma_desc *qdesc, *next;
	int status;

	spin_lock_irqsave(&mchan->lock, flags);
	list_for_each_entry_safe(qdesc, next, &mchan->queued, node) {
		hidma_ll_queue_request(dmadev->lldev, qdesc->tre_ch);
		list_move_tail(&qdesc->node, &mchan->active);
	}

	if (!mchan->running) {
		struct hidma_desc *desc = list_first_entry(&mchan->active,
							   struct hidma_desc,
							   node);
		mchan->running = desc;
	}
	spin_unlock_irqrestore(&mchan->lock, flags);

	/* PM will be released in hidma_callback function. */
	status = pm_runtime_get(dmadev->ddev.dev);
	if (status < 0)
		tasklet_schedule(&dmadev->task);
	else
		hidma_ll_start(dmadev->lldev);
}

static inline bool hidma_txn_is_success(dma_cookie_t cookie,
		dma_cookie_t last_success, dma_cookie_t last_used)
{
	if (last_success <= last_used) {
		if ((cookie <= last_success) || (cookie > last_used))
			return true;
	} else {
		if ((cookie <= last_success) && (cookie > last_used))
			return true;
	}
	return false;
}

static enum dma_status hidma_tx_status(struct dma_chan *dmach,
				       dma_cookie_t cookie,
				       struct dma_tx_state *txstate)
{
	struct hidma_chan *mchan = to_hidma_chan(dmach);
	enum dma_status ret;

	ret = dma_cookie_status(dmach, cookie, txstate);
	if (ret == DMA_COMPLETE) {
		bool is_success;

		is_success = hidma_txn_is_success(cookie, mchan->last_success,
						  dmach->cookie);
		return is_success ? ret : DMA_ERROR;
	}

	if (mchan->paused && (ret == DMA_IN_PROGRESS)) {
		unsigned long flags;
		dma_cookie_t runcookie;

		spin_lock_irqsave(&mchan->lock, flags);
		if (mchan->running)
			runcookie = mchan->running->desc.cookie;
		else
			runcookie = -EINVAL;

		if (runcookie == cookie)
			ret = DMA_PAUSED;

		spin_unlock_irqrestore(&mchan->lock, flags);
	}

	return ret;
}

/*
 * Submit descriptor to hardware.
 * Lock the PM for each descriptor we are sending.
 */
static dma_cookie_t hidma_tx_submit(struct dma_async_tx_descriptor *txd)
{
	struct hidma_chan *mchan = to_hidma_chan(txd->chan);
	struct hidma_dev *dmadev = mchan->dmadev;
	struct hidma_desc *mdesc;
	unsigned long irqflags;
	dma_cookie_t cookie;

	pm_runtime_get_sync(dmadev->ddev.dev);
	if (!hidma_ll_isenabled(dmadev->lldev)) {
		pm_runtime_mark_last_busy(dmadev->ddev.dev);
		pm_runtime_put_autosuspend(dmadev->ddev.dev);
		return -ENODEV;
	}
	pm_runtime_mark_last_busy(dmadev->ddev.dev);
	pm_runtime_put_autosuspend(dmadev->ddev.dev);

	mdesc = container_of(txd, struct hidma_desc, desc);
	spin_lock_irqsave(&mchan->lock, irqflags);

	/* Move descriptor to queued */
	list_move_tail(&mdesc->node, &mchan->queued);

	/* Update cookie */
	cookie = dma_cookie_assign(txd);

	spin_unlock_irqrestore(&mchan->lock, irqflags);

	return cookie;
}

static int hidma_alloc_chan_resources(struct dma_chan *dmach)
{
	struct hidma_chan *mchan = to_hidma_chan(dmach);
	struct hidma_dev *dmadev = mchan->dmadev;
	struct hidma_desc *mdesc, *tmp;
	unsigned long irqflags;
	LIST_HEAD(descs);
	unsigned int i;
	int rc = 0;

	if (mchan->allocated)
		return 0;

	/* Alloc descriptors for this channel */
	for (i = 0; i < dmadev->nr_descriptors; i++) {
		mdesc = kzalloc(sizeof(struct hidma_desc), GFP_NOWAIT);
		if (!mdesc) {
			rc = -ENOMEM;
			break;
		}
		dma_async_tx_descriptor_init(&mdesc->desc, dmach);
		mdesc->desc.tx_submit = hidma_tx_submit;

		rc = hidma_ll_request(dmadev->lldev, mchan->dma_sig,
				      "DMA engine", hidma_callback, mdesc,
				      &mdesc->tre_ch);
		if (rc) {
			dev_err(dmach->device->dev,
				"channel alloc failed at %u\n", i);
			kfree(mdesc);
			break;
		}
		list_add_tail(&mdesc->node, &descs);
	}

	if (rc) {
		/* return the allocated descriptors */
		list_for_each_entry_safe(mdesc, tmp, &descs, node) {
			hidma_ll_free(dmadev->lldev, mdesc->tre_ch);
			kfree(mdesc);
		}
		return rc;
	}

	spin_lock_irqsave(&mchan->lock, irqflags);
	list_splice_tail_init(&descs, &mchan->free);
	mchan->allocated = true;
	spin_unlock_irqrestore(&mchan->lock, irqflags);
	return 1;
}

static struct dma_async_tx_descriptor *
hidma_prep_dma_memcpy(struct dma_chan *dmach, dma_addr_t dest, dma_addr_t src,
		size_t len, unsigned long flags)
{
	struct hidma_chan *mchan = to_hidma_chan(dmach);
	struct hidma_desc *mdesc = NULL;
	struct hidma_dev *mdma = mchan->dmadev;
	unsigned long irqflags;

	/* Get free descriptor */
	spin_lock_irqsave(&mchan->lock, irqflags);
	if (!list_empty(&mchan->free)) {
		mdesc = list_first_entry(&mchan->free, struct hidma_desc, node);
		list_del(&mdesc->node);
	}
	spin_unlock_irqrestore(&mchan->lock, irqflags);

	if (!mdesc)
		return NULL;

	hidma_ll_set_transfer_params(mdma->lldev, mdesc->tre_ch,
				     src, dest, len, flags);

	/* Place descriptor in prepared list */
	spin_lock_irqsave(&mchan->lock, irqflags);
	list_add_tail(&mdesc->node, &mchan->prepared);
	spin_unlock_irqrestore(&mchan->lock, irqflags);

	return &mdesc->desc;
}

static int hidma_terminate_channel(struct dma_chan *chan)
{
	struct hidma_chan *mchan = to_hidma_chan(chan);
	struct hidma_dev *dmadev = to_hidma_dev(mchan->chan.device);
	struct hidma_desc *tmp, *mdesc;
	unsigned long irqflags;
	LIST_HEAD(list);
	int rc;

	pm_runtime_get_sync(dmadev->ddev.dev);
	/* give completed requests a chance to finish */
	hidma_process_completed(mchan);

	spin_lock_irqsave(&mchan->lock, irqflags);
	mchan->last_success = 0;
	list_splice_init(&mchan->active, &list);
	list_splice_init(&mchan->prepared, &list);
	list_splice_init(&mchan->completed, &list);
	list_splice_init(&mchan->queued, &list);
	spin_unlock_irqrestore(&mchan->lock, irqflags);

	/* this suspends the existing transfer */
	rc = hidma_ll_disable(dmadev->lldev);
	if (rc) {
		dev_err(dmadev->ddev.dev, "channel did not pause\n");
		goto out;
	}

	/* return all user requests */
	list_for_each_entry_safe(mdesc, tmp, &list, node) {
		struct dma_async_tx_descriptor *txd = &mdesc->desc;

		dma_descriptor_unmap(txd);
		dmaengine_desc_get_callback_invoke(txd, NULL);
		dma_run_dependencies(txd);

		/* move myself to free_list */
		list_move(&mdesc->node, &mchan->free);
	}

	rc = hidma_ll_enable(dmadev->lldev);
out:
	pm_runtime_mark_last_busy(dmadev->ddev.dev);
	pm_runtime_put_autosuspend(dmadev->ddev.dev);
	return rc;
}

static int hidma_terminate_all(struct dma_chan *chan)
{
	struct hidma_chan *mchan = to_hidma_chan(chan);
	struct hidma_dev *dmadev = to_hidma_dev(mchan->chan.device);
	int rc;

	rc = hidma_terminate_channel(chan);
	if (rc)
		return rc;

	/* reinitialize the hardware */
	pm_runtime_get_sync(dmadev->ddev.dev);
	rc = hidma_ll_setup(dmadev->lldev);
	pm_runtime_mark_last_busy(dmadev->ddev.dev);
	pm_runtime_put_autosuspend(dmadev->ddev.dev);
	return rc;
}

static void hidma_free_chan_resources(struct dma_chan *dmach)
{
	struct hidma_chan *mchan = to_hidma_chan(dmach);
	struct hidma_dev *mdma = mchan->dmadev;
	struct hidma_desc *mdesc, *tmp;
	unsigned long irqflags;
	LIST_HEAD(descs);

	/* terminate running transactions and free descriptors */
	hidma_terminate_channel(dmach);

	spin_lock_irqsave(&mchan->lock, irqflags);

	/* Move data */
	list_splice_tail_init(&mchan->free, &descs);

	/* Free descriptors */
	list_for_each_entry_safe(mdesc, tmp, &descs, node) {
		hidma_ll_free(mdma->lldev, mdesc->tre_ch);
		list_del(&mdesc->node);
		kfree(mdesc);
	}

	mchan->allocated = 0;
	spin_unlock_irqrestore(&mchan->lock, irqflags);
}

static int hidma_pause(struct dma_chan *chan)
{
	struct hidma_chan *mchan;
	struct hidma_dev *dmadev;

	mchan = to_hidma_chan(chan);
	dmadev = to_hidma_dev(mchan->chan.device);
	if (!mchan->paused) {
		pm_runtime_get_sync(dmadev->ddev.dev);
		if (hidma_ll_disable(dmadev->lldev))
			dev_warn(dmadev->ddev.dev, "channel did not stop\n");
		mchan->paused = true;
		pm_runtime_mark_last_busy(dmadev->ddev.dev);
		pm_runtime_put_autosuspend(dmadev->ddev.dev);
	}
	return 0;
}

static int hidma_resume(struct dma_chan *chan)
{
	struct hidma_chan *mchan;
	struct hidma_dev *dmadev;
	int rc = 0;

	mchan = to_hidma_chan(chan);
	dmadev = to_hidma_dev(mchan->chan.device);
	if (mchan->paused) {
		pm_runtime_get_sync(dmadev->ddev.dev);
		rc = hidma_ll_enable(dmadev->lldev);
		if (!rc)
			mchan->paused = false;
		else
			dev_err(dmadev->ddev.dev,
				"failed to resume the channel");
		pm_runtime_mark_last_busy(dmadev->ddev.dev);
		pm_runtime_put_autosuspend(dmadev->ddev.dev);
	}
	return rc;
}

static irqreturn_t hidma_chirq_handler(int chirq, void *arg)
{
	struct hidma_lldev *lldev = arg;

	/*
	 * All interrupts are request driven.
	 * HW doesn't send an interrupt by itself.
	 */
	return hidma_ll_inthandler(chirq, lldev);
}

#ifdef CONFIG_GENERIC_MSI_IRQ_DOMAIN
static irqreturn_t hidma_chirq_handler_msi(int chirq, void *arg)
{
	struct hidma_lldev **lldevp = arg;
	struct hidma_dev *dmadev = to_hidma_dev_from_lldev(lldevp);

	return hidma_ll_inthandler_msi(chirq, *lldevp,
				       1 << (chirq - dmadev->msi_virqbase));
}
#endif

static ssize_t hidma_show_values(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct hidma_dev *mdev = platform_get_drvdata(pdev);

	buf[0] = 0;

	if (strcmp(attr->attr.name, "chid") == 0)
		sprintf(buf, "%d\n", mdev->chidx);

	return strlen(buf);
}

static inline void  hidma_sysfs_uninit(struct hidma_dev *dev)
{
	device_remove_file(dev->ddev.dev, dev->chid_attrs);
}

static struct device_attribute*
hidma_create_sysfs_entry(struct hidma_dev *dev, char *name, int mode)
{
	struct device_attribute *attrs;
	char *name_copy;

	attrs = devm_kmalloc(dev->ddev.dev, sizeof(struct device_attribute),
			     GFP_KERNEL);
	if (!attrs)
		return NULL;

	name_copy = devm_kstrdup(dev->ddev.dev, name, GFP_KERNEL);
	if (!name_copy)
		return NULL;

	attrs->attr.name = name_copy;
	attrs->attr.mode = mode;
	attrs->show = hidma_show_values;
	sysfs_attr_init(&attrs->attr);

	return attrs;
}

static int hidma_sysfs_init(struct hidma_dev *dev)
{
	dev->chid_attrs = hidma_create_sysfs_entry(dev, "chid", S_IRUGO);
	if (!dev->chid_attrs)
		return -ENOMEM;

	return device_create_file(dev->ddev.dev, dev->chid_attrs);
}

#ifdef CONFIG_GENERIC_MSI_IRQ_DOMAIN
static void hidma_write_msi_msg(struct msi_desc *desc, struct msi_msg *msg)
{
	struct device *dev = msi_desc_to_dev(desc);
	struct hidma_dev *dmadev = dev_get_drvdata(dev);

	if (!desc->platform.msi_index) {
		writel(msg->address_lo, dmadev->dev_evca + 0x118);
		writel(msg->address_hi, dmadev->dev_evca + 0x11C);
		writel(msg->data, dmadev->dev_evca + 0x120);
	}
}
#endif

static void hidma_free_msis(struct hidma_dev *dmadev)
{
#ifdef CONFIG_GENERIC_MSI_IRQ_DOMAIN
	struct device *dev = dmadev->ddev.dev;
	struct msi_desc *desc;

	/* free allocated MSI interrupts above */
	for_each_msi_entry(desc, dev)
		devm_free_irq(dev, desc->irq, &dmadev->lldev);

	platform_msi_domain_free_irqs(dev);
#endif
}

static int hidma_request_msi(struct hidma_dev *dmadev,
			     struct platform_device *pdev)
{
#ifdef CONFIG_GENERIC_MSI_IRQ_DOMAIN
	int rc;
	struct msi_desc *desc;
	struct msi_desc *failed_desc = NULL;

	rc = platform_msi_domain_alloc_irqs(&pdev->dev, HIDMA_MSI_INTS,
					    hidma_write_msi_msg);
	if (rc)
		return rc;

	for_each_msi_entry(desc, &pdev->dev) {
		if (!desc->platform.msi_index)
			dmadev->msi_virqbase = desc->irq;

		rc = devm_request_irq(&pdev->dev, desc->irq,
				       hidma_chirq_handler_msi,
				       0, "qcom-hidma-msi",
				       &dmadev->lldev);
		if (rc) {
			failed_desc = desc;
			break;
		}
	}

	if (rc) {
		/* free allocated MSI interrupts above */
		for_each_msi_entry(desc, &pdev->dev) {
			if (desc == failed_desc)
				break;
			devm_free_irq(&pdev->dev, desc->irq,
				      &dmadev->lldev);
		}
	} else {
		/* Add callback to free MSIs on teardown */
		hidma_ll_setup_irq(dmadev->lldev, true);

	}
	if (rc)
		dev_warn(&pdev->dev,
			 "failed to request MSI irq, falling back to wired IRQ\n");
	return rc;
#else
	return -EINVAL;
#endif
}

static bool hidma_msi_capable(struct device *dev)
{
	struct acpi_device *adev = ACPI_COMPANION(dev);
	const char *of_compat;
	int ret = -EINVAL;

	if (!adev || acpi_disabled) {
		ret = device_property_read_string(dev, "compatible",
						  &of_compat);
		if (ret)
			return false;

		ret = strcmp(of_compat, "qcom,hidma-1.1");
	} else {
#ifdef CONFIG_ACPI
		ret = strcmp(acpi_device_hid(adev), "QCOM8062");
#endif
	}
	return ret == 0;
}

static int hidma_probe(struct platform_device *pdev)
{
	struct hidma_dev *dmadev;
	struct resource *trca_resource;
	struct resource *evca_resource;
	int chirq;
	void __iomem *evca;
	void __iomem *trca;
	int rc;
	bool msi;

	pm_runtime_set_autosuspend_delay(&pdev->dev, HIDMA_AUTOSUSPEND_TIMEOUT);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	trca_resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	trca = devm_ioremap_resource(&pdev->dev, trca_resource);
	if (IS_ERR(trca)) {
		rc = -ENOMEM;
		goto bailout;
	}

	evca_resource = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	evca = devm_ioremap_resource(&pdev->dev, evca_resource);
	if (IS_ERR(evca)) {
		rc = -ENOMEM;
		goto bailout;
	}

	/*
	 * This driver only handles the channel IRQs.
	 * Common IRQ is handled by the management driver.
	 */
	chirq = platform_get_irq(pdev, 0);
	if (chirq < 0) {
		rc = -ENODEV;
		goto bailout;
	}

	dmadev = devm_kzalloc(&pdev->dev, sizeof(*dmadev), GFP_KERNEL);
	if (!dmadev) {
		rc = -ENOMEM;
		goto bailout;
	}

	INIT_LIST_HEAD(&dmadev->ddev.channels);
	spin_lock_init(&dmadev->lock);
	dmadev->ddev.dev = &pdev->dev;
	pm_runtime_get_sync(dmadev->ddev.dev);

	dma_cap_set(DMA_MEMCPY, dmadev->ddev.cap_mask);
	if (WARN_ON(!pdev->dev.dma_mask)) {
		rc = -ENXIO;
		goto dmafree;
	}

	dmadev->dev_evca = evca;
	dmadev->evca_resource = evca_resource;
	dmadev->dev_trca = trca;
	dmadev->trca_resource = trca_resource;
	dmadev->ddev.device_prep_dma_memcpy = hidma_prep_dma_memcpy;
	dmadev->ddev.device_alloc_chan_resources = hidma_alloc_chan_resources;
	dmadev->ddev.device_free_chan_resources = hidma_free_chan_resources;
	dmadev->ddev.device_tx_status = hidma_tx_status;
	dmadev->ddev.device_issue_pending = hidma_issue_pending;
	dmadev->ddev.device_pause = hidma_pause;
	dmadev->ddev.device_resume = hidma_resume;
	dmadev->ddev.device_terminate_all = hidma_terminate_all;
	dmadev->ddev.copy_align = 8;

	/*
	 * Determine the MSI capability of the platform. Old HW doesn't
	 * support MSI.
	 */
	msi = hidma_msi_capable(&pdev->dev);

	device_property_read_u32(&pdev->dev, "desc-count",
				 &dmadev->nr_descriptors);

	if (nr_desc_prm) {
		dev_info(&pdev->dev, "overriding number of descriptors as %d\n",
			 nr_desc_prm);
		dmadev->nr_descriptors = nr_desc_prm;
	}

	if (!dmadev->nr_descriptors)
		dmadev->nr_descriptors = HIDMA_NR_DEFAULT_DESC;

	dmadev->chidx = readl(dmadev->dev_trca + 0x28);

	/* Set DMA mask to 64 bits. */
	rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (rc) {
		dev_warn(&pdev->dev, "unable to set coherent mask to 64");
		rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (rc)
			goto dmafree;
	}

	dmadev->lldev = hidma_ll_init(dmadev->ddev.dev,
				      dmadev->nr_descriptors, dmadev->dev_trca,
				      dmadev->dev_evca, dmadev->chidx);
	if (!dmadev->lldev) {
		rc = -EPROBE_DEFER;
		goto dmafree;
	}

	platform_set_drvdata(pdev, dmadev);
	if (msi)
		rc = hidma_request_msi(dmadev, pdev);

	if (!msi || rc) {
		hidma_ll_setup_irq(dmadev->lldev, false);
		rc = devm_request_irq(&pdev->dev, chirq, hidma_chirq_handler,
				      0, "qcom-hidma", dmadev->lldev);
		if (rc)
			goto uninit;
	}

	INIT_LIST_HEAD(&dmadev->ddev.channels);
	rc = hidma_chan_init(dmadev, 0);
	if (rc)
		goto uninit;

	rc = dma_async_device_register(&dmadev->ddev);
	if (rc)
		goto uninit;

	dmadev->irq = chirq;
	tasklet_init(&dmadev->task, hidma_issue_task, (unsigned long)dmadev);
	hidma_debug_init(dmadev);
	hidma_sysfs_init(dmadev);
	dev_info(&pdev->dev, "HI-DMA engine driver registration complete\n");
	pm_runtime_mark_last_busy(dmadev->ddev.dev);
	pm_runtime_put_autosuspend(dmadev->ddev.dev);
	return 0;

uninit:
	if (msi)
		hidma_free_msis(dmadev);

	hidma_debug_uninit(dmadev);
	hidma_ll_uninit(dmadev->lldev);
dmafree:
	if (dmadev)
		hidma_free(dmadev);
bailout:
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return rc;
}

static void hidma_shutdown(struct platform_device *pdev)
{
	struct hidma_dev *dmadev = platform_get_drvdata(pdev);

	dev_info(dmadev->ddev.dev, "HI-DMA engine shutdown\n");

	pm_runtime_get_sync(dmadev->ddev.dev);
	if (hidma_ll_disable(dmadev->lldev))
		dev_warn(dmadev->ddev.dev, "channel did not stop\n");
	pm_runtime_mark_last_busy(dmadev->ddev.dev);
	pm_runtime_put_autosuspend(dmadev->ddev.dev);

}

static int hidma_remove(struct platform_device *pdev)
{
	struct hidma_dev *dmadev = platform_get_drvdata(pdev);

	pm_runtime_get_sync(dmadev->ddev.dev);
	dma_async_device_unregister(&dmadev->ddev);
	if (!dmadev->lldev->msi_support)
		devm_free_irq(dmadev->ddev.dev, dmadev->irq, dmadev->lldev);
	else
		hidma_free_msis(dmadev);

	tasklet_kill(&dmadev->task);
	hidma_sysfs_uninit(dmadev);
	hidma_debug_uninit(dmadev);
	hidma_ll_uninit(dmadev->lldev);
	hidma_free(dmadev);

	dev_info(&pdev->dev, "HI-DMA engine removed\n");
	pm_runtime_put_sync_suspend(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_ACPI)
static const struct acpi_device_id hidma_acpi_ids[] = {
	{"QCOM8061"},
	{"QCOM8062"},
	{},
};
MODULE_DEVICE_TABLE(acpi, hidma_acpi_ids);
#endif

static const struct of_device_id hidma_match[] = {
	{.compatible = "qcom,hidma-1.0",},
	{.compatible = "qcom,hidma-1.1",},
	{},
};
MODULE_DEVICE_TABLE(of, hidma_match);

static struct platform_driver hidma_driver = {
	.probe = hidma_probe,
	.remove = hidma_remove,
	.shutdown = hidma_shutdown,
	.driver = {
		   .name = "hidma",
		   .of_match_table = hidma_match,
		   .acpi_match_table = ACPI_PTR(hidma_acpi_ids),
	},
};

module_platform_driver(hidma_driver);
MODULE_LICENSE("GPL v2");
