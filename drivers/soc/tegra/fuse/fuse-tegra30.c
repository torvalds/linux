// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2022, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
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
    defined(CONFIG_ARCH_TEGRA_186_SOC) || \
    defined(CONFIG_ARCH_TEGRA_194_SOC) || \
    defined(CONFIG_ARCH_TEGRA_234_SOC) || \
    defined(CONFIG_ARCH_TEGRA_241_SOC)
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

	err = pm_runtime_resume_and_get(fuse->dev);
	if (err)
		return 0;

	value = readl_relaxed(fuse->base + FUSE_BEGIN + offset);

	pm_runtime_put(fuse->dev);

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
	.soc_attr_group = &tegra_soc_attr_group,
	.clk_suspend_on = false,
};
#endif

#ifdef CONFIG_ARCH_TEGRA_114_SOC
static const struct nvmem_cell_info tegra114_fuse_cells[] = {
	{
		.name = "tsensor-cpu1",
		.offset = 0x084,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-cpu2",
		.offset = 0x088,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-common",
		.offset = 0x08c,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-cpu0",
		.offset = 0x098,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "xusb-pad-calibration",
		.offset = 0x0f0,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-cpu3",
		.offset = 0x12c,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-gpu",
		.offset = 0x154,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-mem0",
		.offset = 0x158,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-mem1",
		.offset = 0x15c,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-pllx",
		.offset = 0x160,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	},
};

static const struct nvmem_cell_lookup tegra114_fuse_lookups[] = {
	{
		.nvmem_name = "fuse",
		.cell_name = "xusb-pad-calibration",
		.dev_id = "7009f000.padctl",
		.con_id = "calibration",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "tsensor-common",
		.dev_id = "700e2000.thermal-sensor",
		.con_id = "common",
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

static const struct tegra_fuse_info tegra114_fuse_info = {
	.read = tegra30_fuse_read,
	.size = 0x2a0,
	.spare = 0x180,
};

const struct tegra_fuse_soc tegra114_fuse_soc = {
	.init = tegra30_fuse_init,
	.speedo_init = tegra114_init_speedo_data,
	.info = &tegra114_fuse_info,
	.lookups = tegra114_fuse_lookups,
	.num_lookups = ARRAY_SIZE(tegra114_fuse_lookups),
	.cells = tegra114_fuse_cells,
	.num_cells = ARRAY_SIZE(tegra114_fuse_cells),
	.soc_attr_group = &tegra_soc_attr_group,
	.clk_suspend_on = false,
};
#endif

#if defined(CONFIG_ARCH_TEGRA_124_SOC) || defined(CONFIG_ARCH_TEGRA_132_SOC)
static const struct nvmem_cell_info tegra124_fuse_cells[] = {
	{
		.name = "tsensor-cpu1",
		.offset = 0x084,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-cpu2",
		.offset = 0x088,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-cpu0",
		.offset = 0x098,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "xusb-pad-calibration",
		.offset = 0x0f0,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-cpu3",
		.offset = 0x12c,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "sata-calibration",
		.offset = 0x124,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-gpu",
		.offset = 0x154,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-mem0",
		.offset = 0x158,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-mem1",
		.offset = 0x15c,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-pllx",
		.offset = 0x160,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-common",
		.offset = 0x180,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-realignment",
		.offset = 0x1fc,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	},
};

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
	.cells = tegra124_fuse_cells,
	.num_cells = ARRAY_SIZE(tegra124_fuse_cells),
	.soc_attr_group = &tegra_soc_attr_group,
	.clk_suspend_on = true,
};
#endif

#if defined(CONFIG_ARCH_TEGRA_210_SOC)
static const struct nvmem_cell_info tegra210_fuse_cells[] = {
	{
		.name = "tsensor-cpu1",
		.offset = 0x084,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-cpu2",
		.offset = 0x088,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-cpu0",
		.offset = 0x098,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "xusb-pad-calibration",
		.offset = 0x0f0,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-cpu3",
		.offset = 0x12c,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "sata-calibration",
		.offset = 0x124,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-gpu",
		.offset = 0x154,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-mem0",
		.offset = 0x158,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-mem1",
		.offset = 0x15c,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-pllx",
		.offset = 0x160,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-common",
		.offset = 0x180,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "gpu-calibration",
		.offset = 0x204,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "xusb-pad-calibration-ext",
		.offset = 0x250,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	},
};

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
	.cells = tegra210_fuse_cells,
	.num_cells = ARRAY_SIZE(tegra210_fuse_cells),
	.num_lookups = ARRAY_SIZE(tegra210_fuse_lookups),
	.soc_attr_group = &tegra_soc_attr_group,
	.clk_suspend_on = false,
};
#endif

#if defined(CONFIG_ARCH_TEGRA_186_SOC)
static const struct nvmem_cell_info tegra186_fuse_cells[] = {
	{
		.name = "xusb-pad-calibration",
		.offset = 0x0f0,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "xusb-pad-calibration-ext",
		.offset = 0x250,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	},
};

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

static const struct nvmem_keepout tegra186_fuse_keepouts[] = {
	{ .start = 0x01c, .end = 0x0f0 },
	{ .start = 0x138, .end = 0x198 },
	{ .start = 0x1d8, .end = 0x250 },
	{ .start = 0x280, .end = 0x290 },
	{ .start = 0x340, .end = 0x344 }
};

static const struct tegra_fuse_info tegra186_fuse_info = {
	.read = tegra30_fuse_read,
	.size = 0x478,
	.spare = 0x280,
};

const struct tegra_fuse_soc tegra186_fuse_soc = {
	.init = tegra30_fuse_init,
	.info = &tegra186_fuse_info,
	.lookups = tegra186_fuse_lookups,
	.num_lookups = ARRAY_SIZE(tegra186_fuse_lookups),
	.cells = tegra186_fuse_cells,
	.num_cells = ARRAY_SIZE(tegra186_fuse_cells),
	.keepouts = tegra186_fuse_keepouts,
	.num_keepouts = ARRAY_SIZE(tegra186_fuse_keepouts),
	.soc_attr_group = &tegra_soc_attr_group,
	.clk_suspend_on = false,
};
#endif

#if defined(CONFIG_ARCH_TEGRA_194_SOC)
static const struct nvmem_cell_info tegra194_fuse_cells[] = {
	{
		.name = "xusb-pad-calibration",
		.offset = 0x0f0,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "gpu-gcplex-config-fuse",
		.offset = 0x1c8,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "xusb-pad-calibration-ext",
		.offset = 0x250,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "gpu-pdi0",
		.offset = 0x300,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "gpu-pdi1",
		.offset = 0x304,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	},
};

static const struct nvmem_cell_lookup tegra194_fuse_lookups[] = {
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
	}, {
		.nvmem_name = "fuse",
		.cell_name = "gpu-gcplex-config-fuse",
		.dev_id = "17000000.gpu",
		.con_id = "gcplex-config-fuse",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "gpu-pdi0",
		.dev_id = "17000000.gpu",
		.con_id = "pdi0",
	}, {
		.nvmem_name = "fuse",
		.cell_name = "gpu-pdi1",
		.dev_id = "17000000.gpu",
		.con_id = "pdi1",
	},
};

static const struct nvmem_keepout tegra194_fuse_keepouts[] = {
	{ .start = 0x01c, .end = 0x0b8 },
	{ .start = 0x12c, .end = 0x198 },
	{ .start = 0x1a0, .end = 0x1bc },
	{ .start = 0x1d8, .end = 0x250 },
	{ .start = 0x270, .end = 0x290 },
	{ .start = 0x310, .end = 0x45c }
};

static const struct tegra_fuse_info tegra194_fuse_info = {
	.read = tegra30_fuse_read,
	.size = 0x650,
	.spare = 0x280,
};

const struct tegra_fuse_soc tegra194_fuse_soc = {
	.init = tegra30_fuse_init,
	.info = &tegra194_fuse_info,
	.lookups = tegra194_fuse_lookups,
	.num_lookups = ARRAY_SIZE(tegra194_fuse_lookups),
	.cells = tegra194_fuse_cells,
	.num_cells = ARRAY_SIZE(tegra194_fuse_cells),
	.keepouts = tegra194_fuse_keepouts,
	.num_keepouts = ARRAY_SIZE(tegra194_fuse_keepouts),
	.soc_attr_group = &tegra194_soc_attr_group,
	.clk_suspend_on = false,
};
#endif

#if defined(CONFIG_ARCH_TEGRA_234_SOC)
static const struct nvmem_cell_info tegra234_fuse_cells[] = {
	{
		.name = "xusb-pad-calibration",
		.offset = 0x0f0,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "xusb-pad-calibration-ext",
		.offset = 0x250,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	},
};

static const struct nvmem_cell_lookup tegra234_fuse_lookups[] = {
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

static const struct nvmem_keepout tegra234_fuse_keepouts[] = {
	{ .start = 0x01c, .end = 0x064 },
	{ .start = 0x084, .end = 0x0a0 },
	{ .start = 0x0a4, .end = 0x0c8 },
	{ .start = 0x12c, .end = 0x164 },
	{ .start = 0x16c, .end = 0x184 },
	{ .start = 0x190, .end = 0x198 },
	{ .start = 0x1a0, .end = 0x204 },
	{ .start = 0x21c, .end = 0x2f0 },
	{ .start = 0x310, .end = 0x3d8 },
	{ .start = 0x400, .end = 0x420 },
	{ .start = 0x444, .end = 0x490 },
	{ .start = 0x4bc, .end = 0x4f0 },
	{ .start = 0x4f8, .end = 0x54c },
	{ .start = 0x57c, .end = 0x7e8 },
	{ .start = 0x8d0, .end = 0x8d8 },
	{ .start = 0xacc, .end = 0xf00 }
};

static const struct tegra_fuse_info tegra234_fuse_info = {
	.read = tegra30_fuse_read,
	.size = 0xf90,
	.spare = 0x280,
};

const struct tegra_fuse_soc tegra234_fuse_soc = {
	.init = tegra30_fuse_init,
	.info = &tegra234_fuse_info,
	.lookups = tegra234_fuse_lookups,
	.num_lookups = ARRAY_SIZE(tegra234_fuse_lookups),
	.cells = tegra234_fuse_cells,
	.num_cells = ARRAY_SIZE(tegra234_fuse_cells),
	.keepouts = tegra234_fuse_keepouts,
	.num_keepouts = ARRAY_SIZE(tegra234_fuse_keepouts),
	.soc_attr_group = &tegra194_soc_attr_group,
	.clk_suspend_on = false,
};
#endif

#if defined(CONFIG_ARCH_TEGRA_241_SOC)
static const struct tegra_fuse_info tegra241_fuse_info = {
	.read = tegra30_fuse_read,
	.size = 0x16008,
	.spare = 0xcf0,
};

static const struct nvmem_keepout tegra241_fuse_keepouts[] = {
	{ .start = 0xc, .end = 0x1600c }
};

const struct tegra_fuse_soc tegra241_fuse_soc = {
	.init = tegra30_fuse_init,
	.info = &tegra241_fuse_info,
	.keepouts = tegra241_fuse_keepouts,
	.num_keepouts = ARRAY_SIZE(tegra241_fuse_keepouts),
	.soc_attr_group = &tegra194_soc_attr_group,
};
#endif
