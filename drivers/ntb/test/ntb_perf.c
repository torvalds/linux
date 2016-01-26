/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 *   redistributing this file, you may do so under either license.
 *
 *   GPL LICENSE SUMMARY
 *
 *   Copyright(c) 2015 Intel Corporation. All rights reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2015 Intel Corporation. All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copy
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *   PCIe NTB Perf Linux driver
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>
#include <linux/dmaengine.h>
#include <linux/delay.h>
#include <linux/sizes.h>
#include <linux/ntb.h>

#define DRIVER_NAME		"ntb_perf"
#define DRIVER_DESCRIPTION	"PCIe NTB Performance Measurement Tool"

#define DRIVER_LICENSE		"Dual BSD/GPL"
#define DRIVER_VERSION		"1.0"
#define DRIVER_AUTHOR		"Dave Jiang <dave.jiang@intel.com>"

#define PERF_LINK_DOWN_TIMEOUT	10
#define PERF_VERSION		0xffff0001
#define MAX_THREADS		32
#define MAX_TEST_SIZE		SZ_1M
#define MAX_SRCS		32
#define DMA_OUT_RESOURCE_TO	50
#define DMA_RETRIES		20
#define SZ_4G			(1ULL << 32)
#define MAX_SEG_ORDER		20 /* no larger than 1M for kmalloc buffer */

MODULE_LICENSE(DRIVER_LICENSE);
MODULE_VERSION(DRIVER_VERSION);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);

static struct dentry *perf_debugfs_dir;

static unsigned int seg_order = 19; /* 512K */
module_param(seg_order, uint, 0644);
MODULE_PARM_DESC(seg_order, "size order [n^2] of buffer segment for testing");

static unsigned int run_order = 32; /* 4G */
module_param(run_order, uint, 0644);
MODULE_PARM_DESC(run_order, "size order [n^2] of total data to transfer");

static bool use_dma; /* default to 0 */
module_param(use_dma, bool, 0644);
MODULE_PARM_DESC(use_dma, "Using DMA engine to measure performance");

struct perf_mw {
	phys_addr_t	phys_addr;
	resource_size_t	phys_size;
	resource_size_t	xlat_align;
	resource_size_t	xlat_align_size;
	void __iomem	*vbase;
	size_t		xlat_size;
	size_t		buf_size;
	void		*virt_addr;
	dma_addr_t	dma_addr;
};

struct perf_ctx;

struct pthr_ctx {
	struct task_struct	*thread;
	struct perf_ctx		*perf;
	atomic_t		dma_sync;
	struct dma_chan		*dma_chan;
	int			dma_prep_err;
	int			src_idx;
	void			*srcs[MAX_SRCS];
};

struct perf_ctx {
	struct ntb_dev		*ntb;
	spinlock_t		db_lock;
	struct perf_mw		mw;
	bool			link_is_up;
	struct work_struct	link_cleanup;
	struct delayed_work	link_work;
	struct dentry		*debugfs_node_dir;
	struct dentry		*debugfs_run;
	struct dentry		*debugfs_threads;
	u8			perf_threads;
	bool			run;
	struct pthr_ctx		pthr_ctx[MAX_THREADS];
	atomic_t		tsync;
};

enum {
	VERSION = 0,
	MW_SZ_HIGH,
	MW_SZ_LOW,
	SPAD_MSG,
	SPAD_ACK,
	MAX_SPAD
};

static void perf_link_event(void *ctx)
{
	struct perf_ctx *perf = ctx;

	if (ntb_link_is_up(perf->ntb, NULL, NULL) == 1)
		schedule_delayed_work(&perf->link_work, 2*HZ);
	else
		schedule_work(&perf->link_cleanup);
}

static void perf_db_event(void *ctx, int vec)
{
	struct perf_ctx *perf = ctx;
	u64 db_bits, db_mask;

	db_mask = ntb_db_vector_mask(perf->ntb, vec);
	db_bits = ntb_db_read(perf->ntb);

	dev_dbg(&perf->ntb->dev, "doorbell vec %d mask %#llx bits %#llx\n",
		vec, db_mask, db_bits);
}

static const struct ntb_ctx_ops perf_ops = {
	.link_event = perf_link_event,
	.db_event = perf_db_event,
};

static void perf_copy_callback(void *data)
{
	struct pthr_ctx *pctx = data;

	atomic_dec(&pctx->dma_sync);
}

static ssize_t perf_copy(struct pthr_ctx *pctx, char __iomem *dst,
			 char *src, size_t size)
{
	struct perf_ctx *perf = pctx->perf;
	struct dma_async_tx_descriptor *txd;
	struct dma_chan *chan = pctx->dma_chan;
	struct dma_device *device;
	struct dmaengine_unmap_data *unmap;
	dma_cookie_t cookie;
	size_t src_off, dst_off;
	struct perf_mw *mw = &perf->mw;
	void __iomem *vbase;
	void __iomem *dst_vaddr;
	dma_addr_t dst_phys;
	int retries = 0;

	if (!use_dma) {
		memcpy_toio(dst, src, size);
		return size;
	}

	if (!chan) {
		dev_err(&perf->ntb->dev, "DMA engine does not exist\n");
		return -EINVAL;
	}

	device = chan->device;
	src_off = (uintptr_t)src & ~PAGE_MASK;
	dst_off = (uintptr_t __force)dst & ~PAGE_MASK;

	if (!is_dma_copy_aligned(device, src_off, dst_off, size))
		return -ENODEV;

	vbase = mw->vbase;
	dst_vaddr = dst;
	dst_phys = mw->phys_addr + (dst_vaddr - vbase);

	unmap = dmaengine_get_unmap_data(device->dev, 1, GFP_NOWAIT);
	if (!unmap)
		return -ENOMEM;

	unmap->len = size;
	unmap->addr[0] = dma_map_page(device->dev, virt_to_page(src),
				      src_off, size, DMA_TO_DEVICE);
	if (dma_mapping_error(device->dev, unmap->addr[0]))
		goto err_get_unmap;

	unmap->to_cnt = 1;

	do {
		txd = device->device_prep_dma_memcpy(chan, dst_phys,
						     unmap->addr[0],
						     size, DMA_PREP_INTERRUPT);
		if (!txd) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(DMA_OUT_RESOURCE_TO);
		}
	} while (!txd && (++retries < DMA_RETRIES));

	if (!txd) {
		pctx->dma_prep_err++;
		goto err_get_unmap;
	}

	txd->callback = perf_copy_callback;
	txd->callback_param = pctx;
	dma_set_unmap(txd, unmap);

	cookie = dmaengine_submit(txd);
	if (dma_submit_error(cookie))
		goto err_set_unmap;

	atomic_inc(&pctx->dma_sync);
	dma_async_issue_pending(chan);

	return size;

err_set_unmap:
	dmaengine_unmap_put(unmap);
err_get_unmap:
	dmaengine_unmap_put(unmap);
	return 0;
}

static int perf_move_data(struct pthr_ctx *pctx, char __iomem *dst, char *src,
			  u64 buf_size, u64 win_size, u64 total)
{
	int chunks, total_chunks, i;
	int copied_chunks = 0;
	u64 copied = 0, result;
	char __iomem *tmp = dst;
	u64 perf, diff_us;
	ktime_t kstart, kstop, kdiff;

	chunks = div64_u64(win_size, buf_size);
	total_chunks = div64_u64(total, buf_size);
	kstart = ktime_get();

	for (i = 0; i < total_chunks; i++) {
		result = perf_copy(pctx, tmp, src, buf_size);
		copied += result;
		copied_chunks++;
		if (copied_chunks == chunks) {
			tmp = dst;
			copied_chunks = 0;
		} else
			tmp += buf_size;

		/* Probably should schedule every 4GB to prevent soft hang. */
		if (((copied % SZ_4G) == 0) && !use_dma) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(1);
		}
	}

	if (use_dma) {
		pr_info("%s: All DMA descriptors submitted\n", current->comm);
		while (atomic_read(&pctx->dma_sync) != 0)
			msleep(20);
	}

	kstop = ktime_get();
	kdiff = ktime_sub(kstop, kstart);
	diff_us = ktime_to_us(kdiff);

	pr_info("%s: copied %llu bytes\n", current->comm, copied);

	pr_info("%s: lasted %llu usecs\n", current->comm, diff_us);

	perf = div64_u64(copied, diff_us);

	pr_info("%s: MBytes/s: %llu\n", current->comm, perf);

	return 0;
}

static bool perf_dma_filter_fn(struct dma_chan *chan, void *node)
{
	return dev_to_node(&chan->dev->device) == (int)(unsigned long)node;
}

static int ntb_perf_thread(void *data)
{
	struct pthr_ctx *pctx = data;
	struct perf_ctx *perf = pctx->perf;
	struct pci_dev *pdev = perf->ntb->pdev;
	struct perf_mw *mw = &perf->mw;
	char __iomem *dst;
	u64 win_size, buf_size, total;
	void *src;
	int rc, node, i;
	struct dma_chan *dma_chan = NULL;

	pr_info("kthread %s starting...\n", current->comm);

	node = dev_to_node(&pdev->dev);

	if (use_dma && !pctx->dma_chan) {
		dma_cap_mask_t dma_mask;

		dma_cap_zero(dma_mask);
		dma_cap_set(DMA_MEMCPY, dma_mask);
		dma_chan = dma_request_channel(dma_mask, perf_dma_filter_fn,
					       (void *)(unsigned long)node);
		if (!dma_chan) {
			pr_warn("%s: cannot acquire DMA channel, quitting\n",
				current->comm);
			return -ENODEV;
		}
		pctx->dma_chan = dma_chan;
	}

	for (i = 0; i < MAX_SRCS; i++) {
		pctx->srcs[i] = kmalloc_node(MAX_TEST_SIZE, GFP_KERNEL, node);
		if (!pctx->srcs[i]) {
			rc = -ENOMEM;
			goto err;
		}
	}

	win_size = mw->phys_size;
	buf_size = 1ULL << seg_order;
	total = 1ULL << run_order;

	if (buf_size > MAX_TEST_SIZE)
		buf_size = MAX_TEST_SIZE;

	dst = (char __iomem *)mw->vbase;

	atomic_inc(&perf->tsync);
	while (atomic_read(&perf->tsync) != perf->perf_threads)
		schedule();

	src = pctx->srcs[pctx->src_idx];
	pctx->src_idx = (pctx->src_idx + 1) & (MAX_SRCS - 1);

	rc = perf_move_data(pctx, dst, src, buf_size, win_size, total);

	atomic_dec(&perf->tsync);

	if (rc < 0) {
		pr_err("%s: failed\n", current->comm);
		rc = -ENXIO;
		goto err;
	}

	for (i = 0; i < MAX_SRCS; i++) {
		kfree(pctx->srcs[i]);
		pctx->srcs[i] = NULL;
	}

	return 0;

err:
	for (i = 0; i < MAX_SRCS; i++) {
		kfree(pctx->srcs[i]);
		pctx->srcs[i] = NULL;
	}

	if (dma_chan) {
		dma_release_channel(dma_chan);
		pctx->dma_chan = NULL;
	}

	return rc;
}

static void perf_free_mw(struct perf_ctx *perf)
{
	struct perf_mw *mw = &perf->mw;
	struct pci_dev *pdev = perf->ntb->pdev;

	if (!mw->virt_addr)
		return;

	ntb_mw_clear_trans(perf->ntb, 0);
	dma_free_coherent(&pdev->dev, mw->buf_size,
			  mw->virt_addr, mw->dma_addr);
	mw->xlat_size = 0;
	mw->buf_size = 0;
	mw->virt_addr = NULL;
}

static int perf_set_mw(struct perf_ctx *perf, resource_size_t size)
{
	struct perf_mw *mw = &perf->mw;
	size_t xlat_size, buf_size;

	if (!size)
		return -EINVAL;

	xlat_size = round_up(size, mw->xlat_align_size);
	buf_size = round_up(size, mw->xlat_align);

	if (mw->xlat_size == xlat_size)
		return 0;

	if (mw->buf_size)
		perf_free_mw(perf);

	mw->xlat_size = xlat_size;
	mw->buf_size = buf_size;

	mw->virt_addr = dma_alloc_coherent(&perf->ntb->pdev->dev, buf_size,
					   &mw->dma_addr, GFP_KERNEL);
	if (!mw->virt_addr) {
		mw->xlat_size = 0;
		mw->buf_size = 0;
	}

	return 0;
}

static void perf_link_work(struct work_struct *work)
{
	struct perf_ctx *perf =
		container_of(work, struct perf_ctx, link_work.work);
	struct ntb_dev *ndev = perf->ntb;
	struct pci_dev *pdev = ndev->pdev;
	u32 val;
	u64 size;
	int rc;

	dev_dbg(&perf->ntb->pdev->dev, "%s called\n", __func__);

	size = perf->mw.phys_size;
	ntb_peer_spad_write(ndev, MW_SZ_HIGH, upper_32_bits(size));
	ntb_peer_spad_write(ndev, MW_SZ_LOW, lower_32_bits(size));
	ntb_peer_spad_write(ndev, VERSION, PERF_VERSION);

	/* now read what peer wrote */
	val = ntb_spad_read(ndev, VERSION);
	if (val != PERF_VERSION) {
		dev_dbg(&pdev->dev, "Remote version = %#x\n", val);
		goto out;
	}

	val = ntb_spad_read(ndev, MW_SZ_HIGH);
	size = (u64)val << 32;

	val = ntb_spad_read(ndev, MW_SZ_LOW);
	size |= val;

	dev_dbg(&pdev->dev, "Remote MW size = %#llx\n", size);

	rc = perf_set_mw(perf, size);
	if (rc)
		goto out1;

	perf->link_is_up = true;

	return;

out1:
	perf_free_mw(perf);

out:
	if (ntb_link_is_up(ndev, NULL, NULL) == 1)
		schedule_delayed_work(&perf->link_work,
				      msecs_to_jiffies(PERF_LINK_DOWN_TIMEOUT));
}

static void perf_link_cleanup(struct work_struct *work)
{
	struct perf_ctx *perf = container_of(work,
					     struct perf_ctx,
					     link_cleanup);

	dev_dbg(&perf->ntb->pdev->dev, "%s called\n", __func__);

	if (!perf->link_is_up)
		cancel_delayed_work_sync(&perf->link_work);
}

static int perf_setup_mw(struct ntb_dev *ntb, struct perf_ctx *perf)
{
	struct perf_mw *mw;
	int rc;

	mw = &perf->mw;

	rc = ntb_mw_get_range(ntb, 0, &mw->phys_addr, &mw->phys_size,
			      &mw->xlat_align, &mw->xlat_align_size);
	if (rc)
		return rc;

	perf->mw.vbase = ioremap_wc(mw->phys_addr, mw->phys_size);
	if (!mw->vbase)
		return -ENOMEM;

	return 0;
}

static ssize_t debugfs_run_read(struct file *filp, char __user *ubuf,
				size_t count, loff_t *offp)
{
	struct perf_ctx *perf = filp->private_data;
	char *buf;
	ssize_t ret, out_offset;

	if (!perf)
		return 0;

	buf = kmalloc(64, GFP_KERNEL);
	out_offset = snprintf(buf, 64, "%d\n", perf->run);
	ret = simple_read_from_buffer(ubuf, count, offp, buf, out_offset);
	kfree(buf);

	return ret;
}

static ssize_t debugfs_run_write(struct file *filp, const char __user *ubuf,
				 size_t count, loff_t *offp)
{
	struct perf_ctx *perf = filp->private_data;
	int node, i;

	if (!perf->link_is_up)
		return 0;

	if (perf->perf_threads == 0)
		return 0;

	if (atomic_read(&perf->tsync) == 0)
		perf->run = false;

	if (perf->run) {
		/* lets stop the threads */
		perf->run = false;
		for (i = 0; i < MAX_THREADS; i++) {
			if (perf->pthr_ctx[i].thread) {
				kthread_stop(perf->pthr_ctx[i].thread);
				perf->pthr_ctx[i].thread = NULL;
			} else
				break;
		}
	} else {
		perf->run = true;

		if (perf->perf_threads > MAX_THREADS) {
			perf->perf_threads = MAX_THREADS;
			pr_info("Reset total threads to: %u\n", MAX_THREADS);
		}

		/* no greater than 1M */
		if (seg_order > MAX_SEG_ORDER) {
			seg_order = MAX_SEG_ORDER;
			pr_info("Fix seg_order to %u\n", seg_order);
		}

		if (run_order < seg_order) {
			run_order = seg_order;
			pr_info("Fix run_order to %u\n", run_order);
		}

		node = dev_to_node(&perf->ntb->pdev->dev);
		/* launch kernel thread */
		for (i = 0; i < perf->perf_threads; i++) {
			struct pthr_ctx *pctx;

			pctx = &perf->pthr_ctx[i];
			atomic_set(&pctx->dma_sync, 0);
			pctx->perf = perf;
			pctx->thread =
				kthread_create_on_node(ntb_perf_thread,
						       (void *)pctx,
						       node, "ntb_perf %d", i);
			if (pctx->thread)
				wake_up_process(pctx->thread);
			else {
				perf->run = false;
				for (i = 0; i < MAX_THREADS; i++) {
					if (pctx->thread) {
						kthread_stop(pctx->thread);
						pctx->thread = NULL;
					}
				}
			}

			if (perf->run == false)
				return -ENXIO;
		}

	}

	return count;
}

static const struct file_operations ntb_perf_debugfs_run = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = debugfs_run_read,
	.write = debugfs_run_write,
};

static int perf_debugfs_setup(struct perf_ctx *perf)
{
	struct pci_dev *pdev = perf->ntb->pdev;

	if (!debugfs_initialized())
		return -ENODEV;

	if (!perf_debugfs_dir) {
		perf_debugfs_dir = debugfs_create_dir(KBUILD_MODNAME, NULL);
		if (!perf_debugfs_dir)
			return -ENODEV;
	}

	perf->debugfs_node_dir = debugfs_create_dir(pci_name(pdev),
						    perf_debugfs_dir);
	if (!perf->debugfs_node_dir)
		return -ENODEV;

	perf->debugfs_run = debugfs_create_file("run", S_IRUSR | S_IWUSR,
						perf->debugfs_node_dir, perf,
						&ntb_perf_debugfs_run);
	if (!perf->debugfs_run)
		return -ENODEV;

	perf->debugfs_threads = debugfs_create_u8("threads", S_IRUSR | S_IWUSR,
						  perf->debugfs_node_dir,
						  &perf->perf_threads);
	if (!perf->debugfs_threads)
		return -ENODEV;

	return 0;
}

static int perf_probe(struct ntb_client *client, struct ntb_dev *ntb)
{
	struct pci_dev *pdev = ntb->pdev;
	struct perf_ctx *perf;
	int node;
	int rc = 0;

	node = dev_to_node(&pdev->dev);

	perf = kzalloc_node(sizeof(*perf), GFP_KERNEL, node);
	if (!perf) {
		rc = -ENOMEM;
		goto err_perf;
	}

	perf->ntb = ntb;
	perf->perf_threads = 1;
	atomic_set(&perf->tsync, 0);
	perf->run = false;
	spin_lock_init(&perf->db_lock);
	perf_setup_mw(ntb, perf);
	INIT_DELAYED_WORK(&perf->link_work, perf_link_work);
	INIT_WORK(&perf->link_cleanup, perf_link_cleanup);

	rc = ntb_set_ctx(ntb, perf, &perf_ops);
	if (rc)
		goto err_ctx;

	perf->link_is_up = false;
	ntb_link_enable(ntb, NTB_SPEED_AUTO, NTB_WIDTH_AUTO);
	ntb_link_event(ntb);

	rc = perf_debugfs_setup(perf);
	if (rc)
		goto err_ctx;

	return 0;

err_ctx:
	cancel_delayed_work_sync(&perf->link_work);
	cancel_work_sync(&perf->link_cleanup);
	kfree(perf);
err_perf:
	return rc;
}

static void perf_remove(struct ntb_client *client, struct ntb_dev *ntb)
{
	struct perf_ctx *perf = ntb->ctx;
	int i;

	dev_dbg(&perf->ntb->dev, "%s called\n", __func__);

	cancel_delayed_work_sync(&perf->link_work);
	cancel_work_sync(&perf->link_cleanup);

	ntb_clear_ctx(ntb);
	ntb_link_disable(ntb);

	debugfs_remove_recursive(perf_debugfs_dir);
	perf_debugfs_dir = NULL;

	if (use_dma) {
		for (i = 0; i < MAX_THREADS; i++) {
			struct pthr_ctx *pctx = &perf->pthr_ctx[i];

			if (pctx->dma_chan)
				dma_release_channel(pctx->dma_chan);
		}
	}

	kfree(perf);
}

static struct ntb_client perf_client = {
	.ops = {
		.probe = perf_probe,
		.remove = perf_remove,
	},
};
module_ntb_client(perf_client);
