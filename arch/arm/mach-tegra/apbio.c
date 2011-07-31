/*
 * arch/arm/mach-tegra/apbio.c
 *
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
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/sched.h>
#include <linux/mutex.h>

#include <mach/dma.h>
#include <mach/iomap.h>

#include "apbio.h"

static DEFINE_MUTEX(tegra_apb_dma_lock);

#ifdef CONFIG_TEGRA_SYSTEM_DMA
static struct tegra_dma_channel *tegra_apb_dma;
static u32 *tegra_apb_bb;
static dma_addr_t tegra_apb_bb_phys;
static DECLARE_COMPLETION(tegra_apb_wait);

static void apb_dma_complete(struct tegra_dma_req *req)
{
	complete(&tegra_apb_wait);
}

static inline u32 apb_readl(unsigned long offset)
{
	struct tegra_dma_req req;
	int ret;

	if (!tegra_apb_dma)
		return readl(IO_TO_VIRT(offset));

	mutex_lock(&tegra_apb_dma_lock);
	req.complete = apb_dma_complete;
	req.to_memory = 1;
	req.dest_addr = tegra_apb_bb_phys;
	req.dest_bus_width = 32;
	req.dest_wrap = 1;
	req.source_addr = offset;
	req.source_bus_width = 32;
	req.source_wrap = 4;
	req.req_sel = 0;
	req.size = 4;

	INIT_COMPLETION(tegra_apb_wait);

	tegra_dma_enqueue_req(tegra_apb_dma, &req);

	ret = wait_for_completion_timeout(&tegra_apb_wait,
		msecs_to_jiffies(50));

	if (WARN(ret == 0, "apb read dma timed out"))
		*(u32 *)tegra_apb_bb = 0;

	mutex_unlock(&tegra_apb_dma_lock);
	return *((u32 *)tegra_apb_bb);
}

static inline void apb_writel(u32 value, unsigned long offset)
{
	struct tegra_dma_req req;
	int ret;

	if (!tegra_apb_dma) {
		writel(value, IO_TO_VIRT(offset));
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
	req.req_sel = 0;
	req.size = 4;

	INIT_COMPLETION(tegra_apb_wait);

	tegra_dma_enqueue_req(tegra_apb_dma, &req);

	ret = wait_for_completion_timeout(&tegra_apb_wait,
		msecs_to_jiffies(50));

	mutex_unlock(&tegra_apb_dma_lock);
}
#else
static inline u32 apb_readl(unsigned long offset)
{
	return readl(IO_TO_VIRT(offset));
}

static inline void apb_writel(u32 value, unsigned long offset)
{
	writel(value, IO_TO_VIRT(offset));
}
#endif

u32 tegra_apb_readl(unsigned long offset)
{
	return apb_readl(offset);
}

void tegra_apb_writel(u32 value, unsigned long offset)
{
	apb_writel(value, offset);
}

void tegra_init_apb_dma(void)
{
#ifdef CONFIG_TEGRA_SYSTEM_DMA
	tegra_apb_dma = tegra_dma_allocate_channel(TEGRA_DMA_MODE_ONESHOT |
		TEGRA_DMA_SHARED);
	if (!tegra_apb_dma) {
		pr_err("%s: can not allocate dma channel\n", __func__);
		return;
	}

	tegra_apb_bb = dma_alloc_coherent(NULL, sizeof(u32),
		&tegra_apb_bb_phys, GFP_KERNEL);
	if (!tegra_apb_bb) {
		pr_err("%s: can not allocate bounce buffer\n", __func__);
		tegra_dma_free_channel(tegra_apb_dma);
		tegra_apb_dma = NULL;
		return;
	}
#endif
}
