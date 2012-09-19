/*
 * Copyright (C) 2010 NVIDIA Corporation.
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <mach/iomap.h>
#include <linux/of.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/sched.h>
#include <linux/mutex.h>

#include <mach/dma.h>

#include "apbio.h"

#if defined(CONFIG_TEGRA_SYSTEM_DMA) || defined(CONFIG_TEGRA20_APB_DMA)
static DEFINE_MUTEX(tegra_apb_dma_lock);
static u32 *tegra_apb_bb;
static dma_addr_t tegra_apb_bb_phys;
static DECLARE_COMPLETION(tegra_apb_wait);

static u32 tegra_apb_readl_direct(unsigned long offset);
static void tegra_apb_writel_direct(u32 value, unsigned long offset);

#if defined(CONFIG_TEGRA_SYSTEM_DMA)
static struct tegra_dma_channel *tegra_apb_dma;

bool tegra_apb_init(void)
{
	struct tegra_dma_channel *ch;

	mutex_lock(&tegra_apb_dma_lock);

	/* Check to see if we raced to setup */
	if (tegra_apb_dma)
		goto out;

	ch = tegra_dma_allocate_channel(TEGRA_DMA_MODE_ONESHOT |
		TEGRA_DMA_SHARED);

	if (!ch)
		goto out_fail;

	tegra_apb_bb = dma_alloc_coherent(NULL, sizeof(u32),
		&tegra_apb_bb_phys, GFP_KERNEL);
	if (!tegra_apb_bb) {
		pr_err("%s: can not allocate bounce buffer\n", __func__);
		tegra_dma_free_channel(ch);
		goto out_fail;
	}

	tegra_apb_dma = ch;
out:
	mutex_unlock(&tegra_apb_dma_lock);
	return true;

out_fail:
	mutex_unlock(&tegra_apb_dma_lock);
	return false;
}

static void apb_dma_complete(struct tegra_dma_req *req)
{
	complete(&tegra_apb_wait);
}

static u32 tegra_apb_readl_using_dma(unsigned long offset)
{
	struct tegra_dma_req req;
	int ret;

	if (!tegra_apb_dma && !tegra_apb_init())
		return tegra_apb_readl_direct(offset);

	mutex_lock(&tegra_apb_dma_lock);
	req.complete = apb_dma_complete;
	req.to_memory = 1;
	req.dest_addr = tegra_apb_bb_phys;
	req.dest_bus_width = 32;
	req.dest_wrap = 1;
	req.source_addr = offset;
	req.source_bus_width = 32;
	req.source_wrap = 4;
	req.req_sel = TEGRA_DMA_REQ_SEL_CNTR;
	req.size = 4;

	INIT_COMPLETION(tegra_apb_wait);

	tegra_dma_enqueue_req(tegra_apb_dma, &req);

	ret = wait_for_completion_timeout(&tegra_apb_wait,
		msecs_to_jiffies(50));

	if (WARN(ret == 0, "apb read dma timed out")) {
		tegra_dma_dequeue_req(tegra_apb_dma, &req);
		*(u32 *)tegra_apb_bb = 0;
	}

	mutex_unlock(&tegra_apb_dma_lock);
	return *((u32 *)tegra_apb_bb);
}

static void tegra_apb_writel_using_dma(u32 value, unsigned long offset)
{
	struct tegra_dma_req req;
	int ret;

	if (!tegra_apb_dma && !tegra_apb_init()) {
		tegra_apb_writel_direct(value, offset);
		return;
	}

	mutex_lock(&tegra_apb_dma_lock);
	*((u32 *)tegra_apb_bb) = value;
	req.complete = apb_dma_complete;
	req.to_memory = 0;
	req.dest_addr = offset;
	req.dest_wrap = 4;
	req.dest_bus_width = 32;
	req.source_addr = tegra_apb_bb_phys;
	req.source_bus_width = 32;
	req.source_wrap = 1;
	req.req_sel = TEGRA_DMA_REQ_SEL_CNTR;
	req.size = 4;

	INIT_COMPLETION(tegra_apb_wait);

	tegra_dma_enqueue_req(tegra_apb_dma, &req);

	ret = wait_for_completion_timeout(&tegra_apb_wait,
		msecs_to_jiffies(50));

	if (WARN(ret == 0, "apb write dma timed out"))
		tegra_dma_dequeue_req(tegra_apb_dma, &req);

	mutex_unlock(&tegra_apb_dma_lock);
}

#else
static struct dma_chan *tegra_apb_dma_chan;
static struct dma_slave_config dma_sconfig;

bool tegra_apb_dma_init(void)
{
	dma_cap_mask_t mask;

	mutex_lock(&tegra_apb_dma_lock);

	/* Check to see if we raced to setup */
	if (tegra_apb_dma_chan)
		goto skip_init;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	tegra_apb_dma_chan = dma_request_channel(mask, NULL, NULL);
	if (!tegra_apb_dma_chan) {
		/*
		 * This is common until the device is probed, so don't
		 * shout about it.
		 */
		pr_debug("%s: can not allocate dma channel\n", __func__);
		goto err_dma_alloc;
	}

	tegra_apb_bb = dma_alloc_coherent(NULL, sizeof(u32),
		&tegra_apb_bb_phys, GFP_KERNEL);
	if (!tegra_apb_bb) {
		pr_err("%s: can not allocate bounce buffer\n", __func__);
		goto err_buff_alloc;
	}

	dma_sconfig.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	dma_sconfig.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	dma_sconfig.slave_id = TEGRA_DMA_REQ_SEL_CNTR;
	dma_sconfig.src_maxburst = 1;
	dma_sconfig.dst_maxburst = 1;

skip_init:
	mutex_unlock(&tegra_apb_dma_lock);
	return true;

err_buff_alloc:
	dma_release_channel(tegra_apb_dma_chan);
	tegra_apb_dma_chan = NULL;

err_dma_alloc:
	mutex_unlock(&tegra_apb_dma_lock);
	return false;
}

static void apb_dma_complete(void *args)
{
	complete(&tegra_apb_wait);
}

static int do_dma_transfer(unsigned long apb_add,
		enum dma_transfer_direction dir)
{
	struct dma_async_tx_descriptor *dma_desc;
	int ret;

	if (dir == DMA_DEV_TO_MEM)
		dma_sconfig.src_addr = apb_add;
	else
		dma_sconfig.dst_addr = apb_add;

	ret = dmaengine_slave_config(tegra_apb_dma_chan, &dma_sconfig);
	if (ret)
		return ret;

	dma_desc = dmaengine_prep_slave_single(tegra_apb_dma_chan,
			tegra_apb_bb_phys, sizeof(u32), dir,
			DMA_PREP_INTERRUPT |  DMA_CTRL_ACK);
	if (!dma_desc)
		return -EINVAL;

	dma_desc->callback = apb_dma_complete;
	dma_desc->callback_param = NULL;

	INIT_COMPLETION(tegra_apb_wait);

	dmaengine_submit(dma_desc);
	dma_async_issue_pending(tegra_apb_dma_chan);
	ret = wait_for_completion_timeout(&tegra_apb_wait,
		msecs_to_jiffies(50));

	if (WARN(ret == 0, "apb read dma timed out")) {
		dmaengine_terminate_all(tegra_apb_dma_chan);
		return -EFAULT;
	}
	return 0;
}

static u32 tegra_apb_readl_using_dma(unsigned long offset)
{
	int ret;

	if (!tegra_apb_dma_chan && !tegra_apb_dma_init())
		return tegra_apb_readl_direct(offset);

	mutex_lock(&tegra_apb_dma_lock);
	ret = do_dma_transfer(offset, DMA_DEV_TO_MEM);
	if (ret < 0) {
		pr_err("error in reading offset 0x%08lx using dma\n", offset);
		*(u32 *)tegra_apb_bb = 0;
	}
	mutex_unlock(&tegra_apb_dma_lock);
	return *((u32 *)tegra_apb_bb);
}

static void tegra_apb_writel_using_dma(u32 value, unsigned long offset)
{
	int ret;

	if (!tegra_apb_dma_chan && !tegra_apb_dma_init()) {
		tegra_apb_writel_direct(value, offset);
		return;
	}

	mutex_lock(&tegra_apb_dma_lock);
	*((u32 *)tegra_apb_bb) = value;
	ret = do_dma_transfer(offset, DMA_MEM_TO_DEV);
	if (ret < 0)
		pr_err("error in writing offset 0x%08lx using dma\n", offset);
	mutex_unlock(&tegra_apb_dma_lock);
}
#endif
#else
#define tegra_apb_readl_using_dma tegra_apb_readl_direct
#define tegra_apb_writel_using_dma tegra_apb_writel_direct
#endif

typedef u32 (*apbio_read_fptr)(unsigned long offset);
typedef void (*apbio_write_fptr)(u32 value, unsigned long offset);

static apbio_read_fptr apbio_read;
static apbio_write_fptr apbio_write;

static u32 tegra_apb_readl_direct(unsigned long offset)
{
	return readl(IO_TO_VIRT(offset));
}

static void tegra_apb_writel_direct(u32 value, unsigned long offset)
{
	writel(value, IO_TO_VIRT(offset));
}

void tegra_apb_io_init(void)
{
	/* Need to use dma only when it is Tegra20 based platform */
	if (of_machine_is_compatible("nvidia,tegra20") ||
			!of_have_populated_dt()) {
		apbio_read = tegra_apb_readl_using_dma;
		apbio_write = tegra_apb_writel_using_dma;
	} else {
		apbio_read = tegra_apb_readl_direct;
		apbio_write = tegra_apb_writel_direct;
	}
}

u32 tegra_apb_readl(unsigned long offset)
{
	return apbio_read(offset);
}

void tegra_apb_writel(u32 value, unsigned long offset)
{
	apbio_write(value, offset);
}
