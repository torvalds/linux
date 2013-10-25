/*
 * arch/arm/mach-tegra/fuse.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (c) 2013, NVIDIA CORPORATION.  All rights reserved.
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
#include <linux/export.h>
#include <linux/random.h>
#include <linux/tegra-soc.h>

#include "fuse.h"
#include "iomap.h"
#include "apbio.h"

/* Tegra20 only */
#define FUSE_UID_LOW		0x108
#define FUSE_UID_HIGH		0x10c

/* Tegra30 and later */
#define FUSE_VENDOR_CODE	0x200
#define FUSE_FAB_CODE		0x204
#define FUSE_LOT_CODE_0		0x208
#define FUSE_LOT_CODE_1		0x20c
#define FUSE_WAFER_ID		0x210
#define FUSE_X_COORDINATE	0x214
#define FUSE_Y_COORDINATE	0x218

#define FUSE_SKU_INFO		0x110

#define TEGRA20_FUSE_SPARE_BIT		0x200
#define TEGRA30_FUSE_SPARE_BIT		0x244

int tegra_sku_id;
int tegra_cpu_process_id;
int tegra_core_process_id;
int tegra_chip_id;
int tegra_cpu_speedo_id;		/* only exist in Tegra30 and later */
int tegra_soc_speedo_id;
enum tegra_revision tegra_revision;

static int tegra_fuse_spare_bit;
static void (*tegra_init_speedo_data)(void);

/* The BCT to use at boot is specified by board straps that can be read
 * through a APB misc register and decoded. 2 bits, i.e. 4 possible BCTs.
 */
int tegra_bct_strapping;

#define STRAP_OPT 0x008
#define GMI_AD0 (1 << 4)
#define GMI_AD1 (1 << 5)
#define RAM_ID_MASK (GMI_AD0 | GMI_AD1)
#define RAM_CODE_SHIFT 4

static const char *tegra_revision_name[TEGRA_REVISION_MAX] = {
	[TEGRA_REVISION_UNKNOWN] = "unknown",
	[TEGRA_REVISION_A01]     = "A01",
	[TEGRA_REVISION_A02]     = "A02",
	[TEGRA_REVISION_A03]     = "A03",
	[TEGRA_REVISION_A03p]    = "A03 prime",
	[TEGRA_REVISION_A04]     = "A04",
};

u32 tegra_fuse_readl(unsigned long offset)
{
	return tegra_apb_readl(TEGRA_FUSE_BASE + offset);
}

bool tegra_spare_fuse(int bit)
{
	return tegra_fuse_readl(tegra_fuse_spare_bit + bit * 4);
}

static enum tegra_revision tegra_get_revision(u32 id)
{
	u32 minor_rev = (id >> 16) & 0xf;

	switch (minor_rev) {
	case 1:
		return TEGRA_REVISION_A01;
	case 2:
		return TEGRA_REVISION_A02;
	case 3:
		if (tegra_chip_id == TEGRA20 &&
			(tegra_spare_fuse(18) || tegra_spare_fuse(19)))
			return TEGRA_REVISION_A03p;
		else
			return TEGRA_REVISION_A03;
	case 4:
		return TEGRA_REVISION_A04;
	default:
		return TEGRA_REVISION_UNKNOWN;
	}
}

static void tegra_get_process_id(void)
{
	u32 reg;

	reg = tegra_fuse_readl(tegra_fuse_spare_bit);
	tegra_cpu_process_id = (reg >> 6) & 3;
	reg = tegra_fuse_readl(tegra_fuse_spare_bit);
	tegra_core_process_id = (reg >> 12) & 3;
}

u32 tegra_read_chipid(void)
{
	return readl_relaxed(IO_ADDRESS(TEGRA_APB_MISC_BASE) + 0x804);
}

static void __init tegra20_fuse_init_randomness(void)
{
	u32 randomness[2];

	randomness[0] = tegra_fuse_readl(FUSE_UID_LOW);
	randomness[1] = tegra_fuse_readl(FUSE_UID_HIGH);

	add_device_randomness(randomness, sizeof(randomness));
}

/* Applies to Tegra30 or later */
static void __init tegra30_fuse_init_randomness(void)
{
	u32 randomness[7];

	randomness[0] = tegra_fuse_readl(FUSE_VENDOR_CODE);
	randomness[1] = tegra_fuse_readl(FUSE_FAB_CODE);
	randomness[2] = tegra_fuse_readl(FUSE_LOT_CODE_0);
	randomness[3] = tegra_fuse_readl(FUSE_LOT_CODE_1);
	randomness[4] = tegra_fuse_readl(FUSE_WAFER_ID);
	randomness[5] = tegra_fuse_readl(FUSE_X_COORDINATE);
	randomness[6] = tegra_fuse_readl(FUSE_Y_COORDINATE);

	add_device_randomness(randomness, sizeof(randomness));
}

void __init tegra_init_fuse(void)
{
	u32 id;
	u32 randomness[5];

	u32 reg = readl(IO_ADDRESS(TEGRA_CLK_RESET_BASE + 0x48));
	reg |= 1 << 28;
	writel(reg, IO_ADDRESS(TEGRA_CLK_RESET_BASE + 0x48));

	reg = tegra_fuse_readl(FUSE_SKU_INFO);
	randomness[0] = reg;
	tegra_sku_id = reg & 0xFF;

	reg = tegra_apb_readl(TEGRA_APB_MISC_BASE + STRAP_OPT);
	randomness[1] = reg;
	tegra_bct_strapping = (reg & RAM_ID_MASK) >> RAM_CODE_SHIFT;

	id = tegra_read_chipid();
	randomness[2] = id;
	tegra_chip_id = (id >> 8) & 0xff;

	switch (tegra_chip_id) {
	case TEGRA20:
		tegra_fuse_spare_bit = TEGRA20_FUSE_SPARE_BIT;
		tegra_init_speedo_data = &tegra20_init_speedo_data;
		break;
	case TEGRA30:
		tegra_fuse_spare_bit = TEGRA30_FUSE_SPARE_BIT;
		tegra_init_speedo_data = &tegra30_init_speedo_data;
		break;
	case TEGRA114:
		tegra_init_speedo_data = &tegra114_init_speedo_data;
		break;
	default:
		pr_warn("Tegra: unknown chip id %d\n", tegra_chip_id);
		tegra_fuse_spare_bit = TEGRA20_FUSE_SPARE_BIT;
		tegra_init_speedo_data = &tegra_get_process_id;
	}

	tegra_revision = tegra_get_revision(id);
	tegra_init_speedo_data();
	randomness[3] = (tegra_cpu_process_id << 16) | tegra_core_process_id;
	randomness[4] = (tegra_cpu_speedo_id << 16) | tegra_soc_speedo_id;

	add_device_randomness(randomness, sizeof(randomness));
	switch (tegra_chip_id) {
	case TEGRA20:
		tegra20_fuse_init_randomness();
	case TEGRA30:
	case TEGRA114:
	default:
		tegra30_fuse_init_randomness();
	}

	pr_info("Tegra Revision: %s SKU: %d CPU Process: %d Core Process: %d\n",
		tegra_revision_name[tegra_revision],
		tegra_sku_id, tegra_cpu_process_id,
		tegra_core_process_id);
}

unsigned long long tegra_chip_uid(void)
{
	unsigned long long lo, hi;

	lo = tegra_fuse_readl(FUSE_UID_LOW);
	hi = tegra_fuse_readl(FUSE_UID_HIGH);
	return (hi << 32ull) | lo;
}
EXPORT_SYMBOL(tegra_chip_uid);
