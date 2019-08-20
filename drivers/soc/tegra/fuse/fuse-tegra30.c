// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2014, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/random.h>

#include <soc/tegra/fuse.h>

#include "fuse.h"

#define FUSE_BEGIN	0x100

/* Tegra30 and later */
#define FUSE_VENDOR_CODE	0x100
#define FUSE_FAB_CODE		0x104
#define FUSE_LOT_CODE_0		0x108
#define FUSE_LOT_CODE_1		0x10c
#define FUSE_WAFER_ID		0x110
#define FUSE_X_COORDINATE	0x114
#define FUSE_Y_COORDINATE	0x118

#define FUSE_HAS_REVISION_INFO	BIT(0)

#if defined(CONFIG_ARCH_TEGRA_3x_SOC) || \
    defined(CONFIG_ARCH_TEGRA_114_SOC) || \
    defined(CONFIG_ARCH_TEGRA_124_SOC) || \
    defined(CONFIG_ARCH_TEGRA_132_SOC) || \
    defined(CONFIG_ARCH_TEGRA_210_SOC) || \
    defined(CONFIG_ARCH_TEGRA_186_SOC)
static u32 tegra30_fuse_read_early(struct tegra_fuse *fuse, unsigned int offset)
{
	if (WARN_ON(!fuse->base))
		return 0;

	return readl_relaxed(fuse->base + FUSE_BEGIN + offset);
}

static u32 tegra30_fuse_read(struct tegra_fuse *fuse, unsigned int offset)
{
	u32 value;
	int err;

	err = clk_prepare_enable(fuse->clk);
	if (err < 0) {
		dev_err(fuse->dev, "failed to enable FUSE clock: %d\n", err);
		return 0;
	}

	value = readl_relaxed(fuse->base + FUSE_BEGIN + offset);

	clk_disable_unprepare(fuse->clk);

	return value;
}

static void __init tegra30_fuse_add_randomness(void)
{
	u32 randomness[12];

	randomness[0] = tegra_sku_info.sku_id;
	randomness[1] = tegra_read_straps();
	randomness[2] = tegra_read_chipid();
	randomness[3] = tegra_sku_info.cpu_process_id << 16;
	randomness[3] |= tegra_sku_info.soc_process_id;
	randomness[4] = tegra_sku_info.cpu_speedo_id << 16;
	randomness[4] |= tegra_sku_info.soc_speedo_id;
	randomness[5] = tegra_fuse_read_early(FUSE_VENDOR_CODE);
	randomness[6] = tegra_fuse_read_early(FUSE_FAB_CODE);
	randomness[7] = tegra_fuse_read_early(FUSE_LOT_CODE_0);
	randomness[8] = tegra_fuse_read_early(FUSE_LOT_CODE_1);
	randomness[9] = tegra_fuse_read_early(FUSE_WAFER_ID);
	randomness[10] = tegra_fuse_read_early(FUSE_X_COORDINATE);
	randomness[11] = tegra_fuse_read_early(FUSE_Y_COORDINATE);

	add_device_randomness(randomness, sizeof(randomness));
}

static void __init tegra30_fuse_init(struct tegra_fuse *fuse)
{
	fuse->read_early = tegra30_fuse_read_early;
	fuse->read = tegra30_fuse_read;

	tegra_init_revision();

	if (fuse->soc->speedo_init)
		fuse->soc->speedo_init(&tegra_sku_info);

	tegra30_fuse_add_randomness();
}
#endif

#ifdef CONFIG_ARCH_TEGRA_3x_SOC
static const struct tegra_fuse_info tegra30_fuse_info = {
	.read = tegra30_fuse_read,
	.size = 0x2a4,
	.spare = 0x144,
};

const struct tegra_fuse_soc tegra30_fuse_soc = {
	.init = tegra30_fuse_init,
	.speedo_init = tegra30_init_speedo_data,
	.info = &tegra30_fuse_info,
};
#endif

#ifdef CONFIG_ARCH_TEGRA_114_SOC
static const struct tegra_fuse_info tegra114_fuse_info = {
	.read = tegra30_fuse_read,
	.size = 0x2a0,
	.spare = 0x180,
};

const struct tegra_fuse_soc tegra114_fuse_soc = {
	.init = tegra30_fuse_init,
	.speedo_init = tegra114_init_speedo_data,
	.info = &tegra114_fuse_info,
};
#endif

#if defined(CONFIG_ARCH_TEGRA_124_SOC) || defined(CONFIG_ARCH_TEGRA_132_SOC)
static const struct nvmem_cell_lookup tegra124_fuse_lookups[] = {
	{
		.nvmem_name = "fuse",
		.cell_name = "xusb-pad-calibration",
		.dev_id = "7009f000.padctl",
		.con_id = "calibration",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "sata-calibration",
		.dev_id = "70020000.sata",
		.con_id = "calibration",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "tsensor-common",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "common",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "tsensor-realignment",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "realignment",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "tsensor-cpu0",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "cpu0",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "tsensor-cpu1",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "cpu1",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "tsensor-cpu2",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "cpu2",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "tsensor-cpu3",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "cpu3",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "tsensor-mem0",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "mem0",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "tsensor-mem1",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "mem1",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "tsensor-gpu",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "gpu",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "tsensor-pllx",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "pllx",
	},
};

static const struct tegra_fuse_info tegra124_fuse_info = {
	.read = tegra30_fuse_read,
	.size = 0x300,
	.spare = 0x200,
};

const struct tegra_fuse_soc tegra124_fuse_soc = {
	.init = tegra30_fuse_init,
	.speedo_init = tegra124_init_speedo_data,
	.info = &tegra124_fuse_info,
	.lookups = tegra124_fuse_lookups,
	.num_lookups = ARRAY_SIZE(tegra124_fuse_lookups),
};
#endif

#if defined(CONFIG_ARCH_TEGRA_210_SOC)
static const struct nvmem_cell_lookup tegra210_fuse_lookups[] = {
	{
		.nvmem_name = "fuse",
		.cell_name = "tsensor-cpu1",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "cpu1",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "tsensor-cpu2",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "cpu2",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "tsensor-cpu0",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "cpu0",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "xusb-pad-calibration",
		.dev_id = "7009f000.padctl",
		.con_id = "calibration",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "tsensor-cpu3",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "cpu3",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "sata-calibration",
		.dev_id = "70020000.sata",
		.con_id = "calibration",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "tsensor-gpu",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "gpu",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "tsensor-mem0",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "mem0",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "tsensor-mem1",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "mem1",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "tsensor-pllx",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "pllx",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "tsensor-common",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "common",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "gpu-calibration",
		.dev_id = "57000000.gpu",
		.con_id = "calibration",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "xusb-pad-calibration-ext",
		.dev_id = "7009f000.padctl",
		.con_id = "calibration-ext",
	},
};

static const struct tegra_fuse_info tegra210_fuse_info = {
	.read = tegra30_fuse_read,
	.size = 0x300,
	.spare = 0x280,
};

const struct tegra_fuse_soc tegra210_fuse_soc = {
	.init = tegra30_fuse_init,
	.speedo_init = tegra210_init_speedo_data,
	.info = &tegra210_fuse_info,
	.lookups = tegra210_fuse_lookups,
	.num_lookups = ARRAY_SIZE(tegra210_fuse_lookups),
};
#endif

#if defined(CONFIG_ARCH_TEGRA_186_SOC)
static const struct nvmem_cell_lookup tegra186_fuse_lookups[] = {
	{
		.nvmem_name = "fuse",
		.cell_name = "xusb-pad-calibration",
		.dev_id = "3520000.padctl",
		.con_id = "calibration",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "xusb-pad-calibration-ext",
		.dev_id = "3520000.padctl",
		.con_id = "calibration-ext",
	},
};

static const struct tegra_fuse_info tegra186_fuse_info = {
	.read = tegra30_fuse_read,
	.size = 0x300,
	.spare = 0x280,
};

const struct tegra_fuse_soc tegra186_fuse_soc = {
	.init = tegra30_fuse_init,
	.info = &tegra186_fuse_info,
	.lookups = tegra186_fuse_lookups,
	.num_lookups = ARRAY_SIZE(tegra186_fuse_lookups),
};
#endif
