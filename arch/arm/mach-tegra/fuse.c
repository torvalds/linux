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

#include <mach/iomap.h>

#include "fuse.h"
#include "apbio.h"

#define FUSE_UID_LOW		0x108
#define FUSE_UID_HIGH		0x10c
#define FUSE_SKU_INFO		0x110
#define FUSE_SPARE_BIT		0x200

static const char *tegra_revision_name[TEGRA_REVISION_MAX] = {
	[TEGRA_REVISION_UNKNOWN] = "unknown",
	[TEGRA_REVISION_A02] = "A02",
	[TEGRA_REVISION_A03] = "A03",
	[TEGRA_REVISION_A03p] = "A03 prime",
};

u32 tegra_fuse_readl(unsigned long offset)
{
	return tegra_apb_readl(TEGRA_FUSE_BASE + offset);
}

void tegra_fuse_writel(u32 value, unsigned long offset)
{
	tegra_apb_writel(value, TEGRA_FUSE_BASE + offset);
}

static inline bool get_spare_fuse(int bit)
{
	return tegra_apb_readl(FUSE_SPARE_BIT + bit * 4);
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

unsigned long long tegra_chip_uid(void)
{
	unsigned long long lo, hi;

	lo = tegra_fuse_readl(FUSE_UID_LOW);
	hi = tegra_fuse_readl(FUSE_UID_HIGH);
	return (hi << 32ull) | lo;
}

int tegra_sku_id(void)
{
	int sku_id;
	u32 reg = tegra_fuse_readl(FUSE_SKU_INFO);
	sku_id = reg & 0xFF;
	return sku_id;
}

int tegra_cpu_process_id(void)
{
	int cpu_process_id;
	u32 reg = tegra_fuse_readl(FUSE_SPARE_BIT);
	cpu_process_id = (reg >> 6) & 3;
	return cpu_process_id;
}

int tegra_core_process_id(void)
{
	int core_process_id;
	u32 reg = tegra_fuse_readl(FUSE_SPARE_BIT);
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
