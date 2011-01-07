/*
 * arch/arm/mach-tegra/fuse.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@android.com>
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

#include "fuse.h"

#define FUSE_UID_LOW		0x108
#define FUSE_UID_HIGH		0x10c
#define FUSE_SKU_INFO		0x110
#define FUSE_SPARE_BIT		0x200

static DEFINE_MUTEX(tegra_fuse_dma_lock);

#ifdef CONFIG_TEGRA_SYSTEM_DMA
static struct tegra_dma_channel *tegra_fuse_dma;
static u32 *tegra_fuse_bb;
static dma_addr_t tegra_fuse_bb_phys;
static DECLARE_COMPLETION(tegra_fuse_wait);

static const char *tegra_revision_name[TEGRA_REVISION_MAX] = {
	[TEGRA_REVISION_UNKNOWN] = "unknown",
	[TEGRA_REVISION_A02] = "A02",
	[TEGRA_REVISION_A03] = "A03",
	[TEGRA_REVISION_A03p] = "A03 prime",
};

static void fuse_dma_complete(struct tegra_dma_req *req)
{
	complete(&tegra_fuse_wait);
}

static inline u32 fuse_readl(unsigned long offset)
{
	struct tegra_dma_req req;
	int ret;

	if (!tegra_fuse_dma)
		return readl(IO_TO_VIRT(TEGRA_FUSE_BASE + offset));

	mutex_lock(&tegra_fuse_dma_lock);
	req.complete = fuse_dma_complete;
	req.to_memory = 1;
	req.dest_addr = tegra_fuse_bb_phys;
	req.dest_bus_width = 32;
	req.dest_wrap = 1;
	req.source_addr = TEGRA_FUSE_BASE + offset;
	req.source_bus_width = 32;
	req.source_wrap = 4;
	req.req_sel = 0;
	req.size = 4;

	INIT_COMPLETION(tegra_fuse_wait);

	tegra_dma_enqueue_req(tegra_fuse_dma, &req);

	ret = wait_for_completion_timeout(&tegra_fuse_wait,
		msecs_to_jiffies(50));

	if (WARN(ret == 0, "fuse read dma timed out"))
		*(u32 *)tegra_fuse_bb = 0;

	mutex_unlock(&tegra_fuse_dma_lock);
	return *((u32 *)tegra_fuse_bb);
}

static inline void fuse_writel(u32 value, unsigned long offset)
{
	struct tegra_dma_req req;
	int ret;

	if (!tegra_fuse_dma) {
		writel(value, IO_TO_VIRT(TEGRA_FUSE_BASE + offset));
		return;
	}

	mutex_lock(&tegra_fuse_dma_lock);
	*((u32 *)tegra_fuse_bb) = value;
	req.complete = fuse_dma_complete;
	req.to_memory = 0;
	req.dest_addr = TEGRA_FUSE_BASE + offset;
	req.dest_wrap = 4;
	req.dest_bus_width = 32;
	req.source_addr = tegra_fuse_bb_phys;
	req.source_bus_width = 32;
	req.source_wrap = 1;
	req.req_sel = 0;
	req.size = 4;

	INIT_COMPLETION(tegra_fuse_wait);

	tegra_dma_enqueue_req(tegra_fuse_dma, &req);

	ret = wait_for_completion_timeout(&tegra_fuse_wait,
		msecs_to_jiffies(50));

	mutex_unlock(&tegra_fuse_dma_lock);
}
#else
static inline u32 fuse_readl(unsigned long offset)
{
	return readl(IO_TO_VIRT(TEGRA_FUSE_BASE + offset));
}

static inline void fuse_writel(u32 value, unsigned long offset)
{
	writel(value, IO_TO_VIRT(TEGRA_FUSE_BASE + offset));
}
#endif

u32 tegra_fuse_readl(unsigned long offset)
{
	return fuse_readl(offset);
}

void tegra_fuse_writel(u32 value, unsigned long offset)
{
	fuse_writel(value, offset);
}

static inline bool get_spare_fuse(int bit)
{
	return fuse_readl(FUSE_SPARE_BIT + bit * 4);
}

void tegra_init_fuse(void)
{
	u32 reg = readl(IO_TO_VIRT(TEGRA_CLK_RESET_BASE + 0x48));
	reg |= 1 << 28;
	writel(reg, IO_TO_VIRT(TEGRA_CLK_RESET_BASE + 0x48));

	pr_info("Tegra Revision: %s SKU: %d CPU Process: %d Core Process: %d\n",
		tegra_revision_name[tegra_get_revision()],
		tegra_sku_id(), tegra_cpu_process_id(),
		tegra_core_process_id());
}

void tegra_init_fuse_dma(void)
{
#ifdef CONFIG_TEGRA_SYSTEM_DMA
	tegra_fuse_dma = tegra_dma_allocate_channel(TEGRA_DMA_MODE_ONESHOT |
		TEGRA_DMA_SHARED);
	if (!tegra_fuse_dma) {
		pr_err("%s: can not allocate dma channel\n", __func__);
		return;
	}

	tegra_fuse_bb = dma_alloc_coherent(NULL, sizeof(u32),
		&tegra_fuse_bb_phys, GFP_KERNEL);
	if (!tegra_fuse_bb) {
		pr_err("%s: can not allocate bounce buffer\n", __func__);
		tegra_dma_free_channel(tegra_fuse_dma);
		tegra_fuse_dma = NULL;
		return;
	}
#endif
}

unsigned long long tegra_chip_uid(void)
{
	unsigned long long lo, hi;

	lo = fuse_readl(FUSE_UID_LOW);
	hi = fuse_readl(FUSE_UID_HIGH);
	return (hi << 32ull) | lo;
}

int tegra_sku_id(void)
{
	int sku_id;
	u32 reg = fuse_readl(FUSE_SKU_INFO);
	sku_id = reg & 0xFF;
	return sku_id;
}

int tegra_cpu_process_id(void)
{
	int cpu_process_id;
	u32 reg = fuse_readl(FUSE_SPARE_BIT);
	cpu_process_id = (reg >> 6) & 3;
	return cpu_process_id;
}

int tegra_core_process_id(void)
{
	int core_process_id;
	u32 reg = fuse_readl(FUSE_SPARE_BIT);
	core_process_id = (reg >> 12) & 3;
	return core_process_id;
}

enum tegra_revision tegra_get_revision(void)
{
	void __iomem *chip_id = IO_ADDRESS(TEGRA_APB_MISC_BASE) + 0x804;
	u32 id = readl(chip_id);

	switch ((id >> 16) & 0xf) {
	case 2:
		return TEGRA_REVISION_A02;
	case 3:
		if (get_spare_fuse(18) || get_spare_fuse(19))
			return TEGRA_REVISION_A03p;
		else
			return TEGRA_REVISION_A03;
	default:
		return TEGRA_REVISION_UNKNOWN;
	}
}
