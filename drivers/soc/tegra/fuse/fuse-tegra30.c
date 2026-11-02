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
    defined(CONFIG_ARCH_TEGRA_241_SOC) || \
    defined(CONFIG_ARCH_TEGRA_264_SOC)
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

static void __init tegra30_fuse_init(struct tegra_fuse *fuse)
{
	fuse->read_early = tegra30_fuse_read_early;
	fuse->read = tegra30_fuse_read;

	tegra_init_revision();

	if (fuse->soc->speedo_init)
		fuse->soc->speedo_init(&tegra_sku_info);
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

#ifdef CONFIG_ARCH_TEGRA_264_SOC
static const struct nvmem_keepout tegra264_fuse_keepouts[] = {
	{ .start = 0x0019c, .end = 0x001a0 },
	{ .start = 0x00308, .end = 0x00310 },
	{ .start = 0x0032c, .end = 0x00334 },
	{ .start = 0x00350, .end = 0x00354 },
	{ .start = 0x00494, .end = 0x0049c },
	{ .start = 0x0078c, .end = 0x00790 },
	{ .start = 0x007a0, .end = 0x007a8 },
	{ .start = 0x007dc, .end = 0x007e4 },
	{ .start = 0x008d8, .end = 0x008dc },
	{ .start = 0x0096c, .end = 0x00970 },
	{ .start = 0x00974, .end = 0x0097c },
	{ .start = 0x009f4, .end = 0x009f8 },
	{ .start = 0x00a14, .end = 0x00a20 },
	{ .start = 0x00a44, .end = 0x00a4c },
	{ .start = 0x00a50, .end = 0x00a58 },
	{ .start = 0x00a5c, .end = 0x00a64 },
	{ .start = 0x00a68, .end = 0x00a70 },
	{ .start = 0x00acc, .end = 0x00ad0 },
	{ .start = 0x00b0c, .end = 0x00b18 },
	{ .start = 0x00c80, .end = 0x00c8c },
	{ .start = 0x00dac, .end = 0x00db4 },
	{ .start = 0x00db8, .end = 0x00dbc },
	{ .start = 0x00e0c, .end = 0x00e10 },
	{ .start = 0x00fd0, .end = 0x01f00 },
	{ .start = 0x01f1c, .end = 0x10064 },
	{ .start = 0x10084, .end = 0x100a0 },
	{ .start = 0x100a4, .end = 0x1019c },
	{ .start = 0x101a0, .end = 0x101cc },
	{ .start = 0x101d0, .end = 0x10308 },
	{ .start = 0x10310, .end = 0x1055c },
	{ .start = 0x1057c, .end = 0x106b0 },
	{ .start = 0x106b4, .end = 0x11008 },
	{ .start = 0x1100c, .end = 0x11018 },
	{ .start = 0x11020, .end = 0x110b8 },
	{ .start = 0x110c4, .end = 0x110c8 },
	{ .start = 0x110e8, .end = 0x110ec },
	{ .start = 0x110f0, .end = 0x11124 },
	{ .start = 0x11128, .end = 0x11168 },
	{ .start = 0x1116c, .end = 0x111b8 },
	{ .start = 0x111bc, .end = 0x111e4 },
	{ .start = 0x111e8, .end = 0x111ec },
	{ .start = 0x111f0, .end = 0x1121c },
	{ .start = 0x11250, .end = 0x1133c },
	{ .start = 0x11340, .end = 0x113c8 },
	{ .start = 0x113d0, .end = 0x11420 },
	{ .start = 0x11430, .end = 0x11440 },
	{ .start = 0x11444, .end = 0x11468 },
	{ .start = 0x1146c, .end = 0x114a8 },
	{ .start = 0x114ac, .end = 0x115cc },
	{ .start = 0x115d0, .end = 0x115dc },
	{ .start = 0x115e0, .end = 0x116b4 },
	{ .start = 0x116bc, .end = 0x11740 },
	{ .start = 0x11744, .end = 0x117b8 },
	{ .start = 0x117bc, .end = 0x11810 },
	{ .start = 0x11814, .end = 0x11850 },
	{ .start = 0x11858, .end = 0x118a0 },
	{ .start = 0x118ac, .end = 0x11b48 },
	{ .start = 0x11b50, .end = 0x11b5c },
	{ .start = 0x11b60, .end = 0x11bf8 },
	{ .start = 0x11bfc, .end = 0x11c18 },
	{ .start = 0x11c40, .end = 0x12000 },
	{ .start = 0x12004, .end = 0x12010 },
	{ .start = 0x12014, .end = 0x120fc },
	{ .start = 0x1211c, .end = 0x12120 },
	{ .start = 0x12124, .end = 0x12144 },
	{ .start = 0x1214c, .end = 0x12300 },
	{ .start = 0x12308, .end = 0x1236c },
	{ .start = 0x12370, .end = 0x126ac },
	{ .start = 0x126b0, .end = 0x12c60 },
	{ .start = 0x12c64, .end = 0x12d94 },
	{ .start = 0x12da4, .end = 0x13004 },
	{ .start = 0x13008, .end = 0x1300c },
	{ .start = 0x13010, .end = 0x13014 },
	{ .start = 0x13018, .end = 0x13020 },
	{ .start = 0x13064, .end = 0x13084 },
	{ .start = 0x130a0, .end = 0x130a4 },
	{ .start = 0x130b8, .end = 0x130c4 },
	{ .start = 0x130c8, .end = 0x130e8 },
	{ .start = 0x130ec, .end = 0x130f0 },
	{ .start = 0x130fc, .end = 0x1311c },
	{ .start = 0x13120, .end = 0x13128 },
	{ .start = 0x13144, .end = 0x1314c },
	{ .start = 0x13168, .end = 0x1316c },
	{ .start = 0x1319c, .end = 0x131a0 },
	{ .start = 0x131ac, .end = 0x131c0 },
	{ .start = 0x131cc, .end = 0x131d0 },
	{ .start = 0x131e4, .end = 0x131e8 },
	{ .start = 0x131ec, .end = 0x131f0 },
	{ .start = 0x1321c, .end = 0x13250 },
	{ .start = 0x132f0, .end = 0x13310 },
	{ .start = 0x1332c, .end = 0x13334 },
	{ .start = 0x1333c, .end = 0x13344 },
	{ .start = 0x13350, .end = 0x13354 },
	{ .start = 0x1336c, .end = 0x13370 },
	{ .start = 0x133ac, .end = 0x133b0 },
	{ .start = 0x133c8, .end = 0x133d0 },
	{ .start = 0x13420, .end = 0x13430 },
	{ .start = 0x13440, .end = 0x13444 },
	{ .start = 0x13468, .end = 0x13470 },
	{ .start = 0x13494, .end = 0x1349c },
	{ .start = 0x134a8, .end = 0x134ac },
	{ .start = 0x13544, .end = 0x1354c },
	{ .start = 0x1355c, .end = 0x1357c },
	{ .start = 0x135cc, .end = 0x135e0 },
	{ .start = 0x135e4, .end = 0x13608 },
	{ .start = 0x1360c, .end = 0x13620 },
	{ .start = 0x136ac, .end = 0x136bc },
	{ .start = 0x136c0, .end = 0x136c8 },
	{ .start = 0x13714, .end = 0x13718 },
	{ .start = 0x13724, .end = 0x13728 },
	{ .start = 0x1372c, .end = 0x13730 },
	{ .start = 0x13734, .end = 0x1373c },
	{ .start = 0x13740, .end = 0x13744 },
	{ .start = 0x13754, .end = 0x13758 },
	{ .start = 0x13760, .end = 0x13784 },
	{ .start = 0x1378c, .end = 0x13790 },
	{ .start = 0x137a0, .end = 0x137a8 },
	{ .start = 0x137b8, .end = 0x137bc },
	{ .start = 0x137dc, .end = 0x137e4 },
	{ .start = 0x137ec, .end = 0x137f0 },
	{ .start = 0x13800, .end = 0x13818 },
	{ .start = 0x13820, .end = 0x13828 },
	{ .start = 0x13830, .end = 0x13858 },
	{ .start = 0x138a0, .end = 0x138b0 },
	{ .start = 0x138d0, .end = 0x138dc },
	{ .start = 0x1396c, .end = 0x13970 },
	{ .start = 0x13974, .end = 0x1397c },
	{ .start = 0x139f4, .end = 0x139f8 },
	{ .start = 0x13a14, .end = 0x13a20 },
	{ .start = 0x13a44, .end = 0x13a4c },
	{ .start = 0x13a50, .end = 0x13a58 },
	{ .start = 0x13a5c, .end = 0x13a64 },
	{ .start = 0x13a68, .end = 0x13a70 },
	{ .start = 0x13acc, .end = 0x13ad8 },
	{ .start = 0x13b0c, .end = 0x13b18 },
	{ .start = 0x13b48, .end = 0x13b50 },
	{ .start = 0x13b5c, .end = 0x13b60 },
	{ .start = 0x13bf8, .end = 0x13bfc },
	{ .start = 0x13c18, .end = 0x13c40 },
	{ .start = 0x13c60, .end = 0x13c64 },
	{ .start = 0x13c74, .end = 0x13c8c },
	{ .start = 0x13d94, .end = 0x13da4 },
	{ .start = 0x13dac, .end = 0x13db4 },
	{ .start = 0x13db8, .end = 0x13dbc },
	{ .start = 0x13df4, .end = 0x13e04 },
	{ .start = 0x13e0c, .end = 0x13e10 },
};

static const struct tegra_fuse_info tegra264_fuse_info = {
	.read = tegra30_fuse_read,
	.size = 0x13eb8,
};

const struct tegra_fuse_soc tegra264_fuse_soc = {
	.init = tegra30_fuse_init,
	.info = &tegra264_fuse_info,
	.keepouts = tegra264_fuse_keepouts,
	.num_keepouts = ARRAY_SIZE(tegra264_fuse_keepouts),
	.soc_attr_group = &tegra194_soc_attr_group,
	.clk_suspend_on = false,
};
#endif
