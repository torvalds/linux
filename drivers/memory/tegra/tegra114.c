/*
 * Copyright (C) 2014 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/of.h>
#include <linux/mm.h>

#include <asm/cacheflush.h>

#include <dt-bindings/memory/tegra114-mc.h>

#include "mc.h"

static const struct tegra_mc_client tegra114_mc_clients[] = {
	{
		.id = 0x00,
		.name = "ptcr",
		.swgroup = TEGRA_SWGROUP_PTC,
	}, {
		.id = 0x01,
		.name = "display0a",
		.swgroup = TEGRA_SWGROUP_DC,
		.smmu = {
			.reg = 0x228,
			.bit = 1,
		},
		.la = {
			.reg = 0x2e8,
			.shift = 0,
			.mask = 0xff,
			.def = 0x4e,
		},
	}, {
		.id = 0x02,
		.name = "display0ab",
		.swgroup = TEGRA_SWGROUP_DCB,
		.smmu = {
			.reg = 0x228,
			.bit = 2,
		},
		.la = {
			.reg = 0x2f4,
			.shift = 0,
			.mask = 0xff,
			.def = 0x4e,
		},
	}, {
		.id = 0x03,
		.name = "display0b",
		.swgroup = TEGRA_SWGROUP_DC,
		.smmu = {
			.reg = 0x228,
			.bit = 3,
		},
		.la = {
			.reg = 0x2e8,
			.shift = 16,
			.mask = 0xff,
			.def = 0x4e,
		},
	}, {
		.id = 0x04,
		.name = "display0bb",
		.swgroup = TEGRA_SWGROUP_DCB,
		.smmu = {
			.reg = 0x228,
			.bit = 4,
		},
		.la = {
			.reg = 0x2f4,
			.shift = 16,
			.mask = 0xff,
			.def = 0x4e,
		},
	}, {
		.id = 0x05,
		.name = "display0c",
		.swgroup = TEGRA_SWGROUP_DC,
		.smmu = {
			.reg = 0x228,
			.bit = 5,
		},
		.la = {
			.reg = 0x2ec,
			.shift = 0,
			.mask = 0xff,
			.def = 0x4e,
		},
	}, {
		.id = 0x06,
		.name = "display0cb",
		.swgroup = TEGRA_SWGROUP_DCB,
		.smmu = {
			.reg = 0x228,
			.bit = 6,
		},
		.la = {
			.reg = 0x2f8,
			.shift = 0,
			.mask = 0xff,
			.def = 0x4e,
		},
	}, {
		.id = 0x09,
		.name = "eppup",
		.swgroup = TEGRA_SWGROUP_EPP,
		.smmu = {
			.reg = 0x228,
			.bit = 9,
		},
		.la = {
			.reg = 0x300,
			.shift = 0,
			.mask = 0xff,
			.def = 0x33,
		},
	}, {
		.id = 0x0a,
		.name = "g2pr",
		.swgroup = TEGRA_SWGROUP_G2,
		.smmu = {
			.reg = 0x228,
			.bit = 10,
		},
		.la = {
			.reg = 0x308,
			.shift = 0,
			.mask = 0xff,
			.def = 0x09,
		},
	}, {
		.id = 0x0b,
		.name = "g2sr",
		.swgroup = TEGRA_SWGROUP_G2,
		.smmu = {
			.reg = 0x228,
			.bit = 11,
		},
		.la = {
			.reg = 0x308,
			.shift = 16,
			.mask = 0xff,
			.def = 0x09,
		},
	}, {
		.id = 0x0f,
		.name = "avpcarm7r",
		.swgroup = TEGRA_SWGROUP_AVPC,
		.smmu = {
			.reg = 0x228,
			.bit = 15,
		},
		.la = {
			.reg = 0x2e4,
			.shift = 0,
			.mask = 0xff,
			.def = 0x04,
		},
	}, {
		.id = 0x10,
		.name = "displayhc",
		.swgroup = TEGRA_SWGROUP_DC,
		.smmu = {
			.reg = 0x228,
			.bit = 16,
		},
		.la = {
			.reg = 0x2f0,
			.shift = 0,
			.mask = 0xff,
			.def = 0x68,
		},
	}, {
		.id = 0x11,
		.name = "displayhcb",
		.swgroup = TEGRA_SWGROUP_DCB,
		.smmu = {
			.reg = 0x228,
			.bit = 17,
		},
		.la = {
			.reg = 0x2fc,
			.shift = 0,
			.mask = 0xff,
			.def = 0x68,
		},
	}, {
		.id = 0x12,
		.name = "fdcdrd",
		.swgroup = TEGRA_SWGROUP_NV,
		.smmu = {
			.reg = 0x228,
			.bit = 18,
		},
		.la = {
			.reg = 0x334,
			.shift = 0,
			.mask = 0xff,
			.def = 0x0c,
		},
	}, {
		.id = 0x13,
		.name = "fdcdrd2",
		.swgroup = TEGRA_SWGROUP_NV,
		.smmu = {
			.reg = 0x228,
			.bit = 19,
		},
		.la = {
			.reg = 0x33c,
			.shift = 0,
			.mask = 0xff,
			.def = 0x0c,
		},
	}, {
		.id = 0x14,
		.name = "g2dr",
		.swgroup = TEGRA_SWGROUP_G2,
		.smmu = {
			.reg = 0x228,
			.bit = 20,
		},
		.la = {
			.reg = 0x30c,
			.shift = 0,
			.mask = 0xff,
			.def = 0x0a,
		},
	}, {
		.id = 0x15,
		.name = "hdar",
		.swgroup = TEGRA_SWGROUP_HDA,
		.smmu = {
			.reg = 0x228,
			.bit = 21,
		},
		.la = {
			.reg = 0x318,
			.shift = 0,
			.mask = 0xff,
			.def = 0xff,
		},
	}, {
		.id = 0x16,
		.name = "host1xdmar",
		.swgroup = TEGRA_SWGROUP_HC,
		.smmu = {
			.reg = 0x228,
			.bit = 22,
		},
		.la = {
			.reg = 0x310,
			.shift = 0,
			.mask = 0xff,
			.def = 0x10,
		},
	}, {
		.id = 0x17,
		.name = "host1xr",
		.swgroup = TEGRA_SWGROUP_HC,
		.smmu = {
			.reg = 0x228,
			.bit = 23,
		},
		.la = {
			.reg = 0x310,
			.shift = 16,
			.mask = 0xff,
			.def = 0xa5,
		},
	}, {
		.id = 0x18,
		.name = "idxsrd",
		.swgroup = TEGRA_SWGROUP_NV,
		.smmu = {
			.reg = 0x228,
			.bit = 24,
		},
		.la = {
			.reg = 0x334,
			.shift = 16,
			.mask = 0xff,
			.def = 0x0b,
		},
	}, {
		.id = 0x1c,
		.name = "msencsrd",
		.swgroup = TEGRA_SWGROUP_MSENC,
		.smmu = {
			.reg = 0x228,
			.bit = 28,
		},
		.la = {
			.reg = 0x328,
			.shift = 0,
			.mask = 0xff,
			.def = 0x80,
		},
	}, {
		.id = 0x1d,
		.name = "ppcsahbdmar",
		.swgroup = TEGRA_SWGROUP_PPCS,
		.smmu = {
			.reg = 0x228,
			.bit = 29,
		},
		.la = {
			.reg = 0x344,
			.shift = 0,
			.mask = 0xff,
			.def = 0x50,
		},
	}, {
		.id = 0x1e,
		.name = "ppcsahbslvr",
		.swgroup = TEGRA_SWGROUP_PPCS,
		.smmu = {
			.reg = 0x228,
			.bit = 30,
		},
		.la = {
			.reg = 0x344,
			.shift = 16,
			.mask = 0xff,
			.def = 0xe8,
		},
	}, {
		.id = 0x20,
		.name = "texl2srd",
		.swgroup = TEGRA_SWGROUP_NV,
		.smmu = {
			.reg = 0x22c,
			.bit = 0,
		},
		.la = {
			.reg = 0x338,
			.shift = 0,
			.mask = 0xff,
			.def = 0x0c,
		},
	}, {
		.id = 0x22,
		.name = "vdebsevr",
		.swgroup = TEGRA_SWGROUP_VDE,
		.smmu = {
			.reg = 0x22c,
			.bit = 2,
		},
		.la = {
			.reg = 0x354,
			.shift = 0,
			.mask = 0xff,
			.def = 0xff,
		},
	}, {
		.id = 0x23,
		.name = "vdember",
		.swgroup = TEGRA_SWGROUP_VDE,
		.smmu = {
			.reg = 0x22c,
			.bit = 3,
		},
		.la = {
			.reg = 0x354,
			.shift = 16,
			.mask = 0xff,
			.def = 0xff,
		},
	}, {
		.id = 0x24,
		.name = "vdemcer",
		.swgroup = TEGRA_SWGROUP_VDE,
		.smmu = {
			.reg = 0x22c,
			.bit = 4,
		},
		.la = {
			.reg = 0x358,
			.shift = 0,
			.mask = 0xff,
			.def = 0xb8,
		},
	}, {
		.id = 0x25,
		.name = "vdetper",
		.swgroup = TEGRA_SWGROUP_VDE,
		.smmu = {
			.reg = 0x22c,
			.bit = 5,
		},
		.la = {
			.reg = 0x358,
			.shift = 16,
			.mask = 0xff,
			.def = 0xee,
		},
	}, {
		.id = 0x26,
		.name = "mpcorelpr",
		.swgroup = TEGRA_SWGROUP_MPCORELP,
		.la = {
			.reg = 0x324,
			.shift = 0,
			.mask = 0xff,
			.def = 0x04,
		},
	}, {
		.id = 0x27,
		.name = "mpcorer",
		.swgroup = TEGRA_SWGROUP_MPCORE,
		.la = {
			.reg = 0x320,
			.shift = 0,
			.mask = 0xff,
			.def = 0x04,
		},
	}, {
		.id = 0x28,
		.name = "eppu",
		.swgroup = TEGRA_SWGROUP_EPP,
		.smmu = {
			.reg = 0x22c,
			.bit = 8,
		},
		.la = {
			.reg = 0x300,
			.shift = 16,
			.mask = 0xff,
			.def = 0x33,
		},
	}, {
		.id = 0x29,
		.name = "eppv",
		.swgroup = TEGRA_SWGROUP_EPP,
		.smmu = {
			.reg = 0x22c,
			.bit = 9,
		},
		.la = {
			.reg = 0x304,
			.shift = 0,
			.mask = 0xff,
			.def = 0x6c,
		},
	}, {
		.id = 0x2a,
		.name = "eppy",
		.swgroup = TEGRA_SWGROUP_EPP,
		.smmu = {
			.reg = 0x22c,
			.bit = 10,
		},
		.la = {
			.reg = 0x304,
			.shift = 16,
			.mask = 0xff,
			.def = 0x6c,
		},
	}, {
		.id = 0x2b,
		.name = "msencswr",
		.swgroup = TEGRA_SWGROUP_MSENC,
		.smmu = {
			.reg = 0x22c,
			.bit = 11,
		},
		.la = {
			.reg = 0x328,
			.shift = 16,
			.mask = 0xff,
			.def = 0x80,
		},
	}, {
		.id = 0x2c,
		.name = "viwsb",
		.swgroup = TEGRA_SWGROUP_VI,
		.smmu = {
			.reg = 0x22c,
			.bit = 12,
		},
		.la = {
			.reg = 0x364,
			.shift = 0,
			.mask = 0xff,
			.def = 0x47,
		},
	}, {
		.id = 0x2d,
		.name = "viwu",
		.swgroup = TEGRA_SWGROUP_VI,
		.smmu = {
			.reg = 0x22c,
			.bit = 13,
		},
		.la = {
			.reg = 0x368,
			.shift = 0,
			.mask = 0xff,
			.def = 0xff,
		},
	}, {
		.id = 0x2e,
		.name = "viwv",
		.swgroup = TEGRA_SWGROUP_VI,
		.smmu = {
			.reg = 0x22c,
			.bit = 14,
		},
		.la = {
			.reg = 0x368,
			.shift = 16,
			.mask = 0xff,
			.def = 0xff,
		},
	}, {
		.id = 0x2f,
		.name = "viwy",
		.swgroup = TEGRA_SWGROUP_VI,
		.smmu = {
			.reg = 0x22c,
			.bit = 15,
		},
		.la = {
			.reg = 0x36c,
			.shift = 0,
			.mask = 0xff,
			.def = 0x47,
		},
	}, {
		.id = 0x30,
		.name = "g2dw",
		.swgroup = TEGRA_SWGROUP_G2,
		.smmu = {
			.reg = 0x22c,
			.bit = 16,
		},
		.la = {
			.reg = 0x30c,
			.shift = 16,
			.mask = 0xff,
			.def = 0x9,
		},
	}, {
		.id = 0x32,
		.name = "avpcarm7w",
		.swgroup = TEGRA_SWGROUP_AVPC,
		.smmu = {
			.reg = 0x22c,
			.bit = 18,
		},
		.la = {
			.reg = 0x2e4,
			.shift = 16,
			.mask = 0xff,
			.def = 0x0e,
		},
	}, {
		.id = 0x33,
		.name = "fdcdwr",
		.swgroup = TEGRA_SWGROUP_NV,
		.smmu = {
			.reg = 0x22c,
			.bit = 19,
		},
		.la = {
			.reg = 0x338,
			.shift = 16,
			.mask = 0xff,
			.def = 0x10,
		},
	}, {
		.id = 0x34,
		.name = "fdcwr2",
		.swgroup = TEGRA_SWGROUP_NV,
		.smmu = {
			.reg = 0x22c,
			.bit = 20,
		},
		.la = {
			.reg = 0x340,
			.shift = 0,
			.mask = 0xff,
			.def = 0x10,
		},
	}, {
		.id = 0x35,
		.name = "hdaw",
		.swgroup = TEGRA_SWGROUP_HDA,
		.smmu = {
			.reg = 0x22c,
			.bit = 21,
		},
		.la = {
			.reg = 0x318,
			.shift = 16,
			.mask = 0xff,
			.def = 0xff,
		},
	}, {
		.id = 0x36,
		.name = "host1xw",
		.swgroup = TEGRA_SWGROUP_HC,
		.smmu = {
			.reg = 0x22c,
			.bit = 22,
		},
		.la = {
			.reg = 0x314,
			.shift = 0,
			.mask = 0xff,
			.def = 0x25,
		},
	}, {
		.id = 0x37,
		.name = "ispw",
		.swgroup = TEGRA_SWGROUP_ISP,
		.smmu = {
			.reg = 0x22c,
			.bit = 23,
		},
		.la = {
			.reg = 0x31c,
			.shift = 0,
			.mask = 0xff,
			.def = 0xff,
		},
	}, {
		.id = 0x38,
		.name = "mpcorelpw",
		.swgroup = TEGRA_SWGROUP_MPCORELP,
		.la = {
			.reg = 0x324,
			.shift = 16,
			.mask = 0xff,
			.def = 0x80,
		},
	}, {
		.id = 0x39,
		.name = "mpcorew",
		.swgroup = TEGRA_SWGROUP_MPCORE,
		.la = {
			.reg = 0x320,
			.shift = 16,
			.mask = 0xff,
			.def = 0x0e,
		},
	}, {
		.id = 0x3b,
		.name = "ppcsahbdmaw",
		.swgroup = TEGRA_SWGROUP_PPCS,
		.smmu = {
			.reg = 0x22c,
			.bit = 27,
		},
		.la = {
			.reg = 0x348,
			.shift = 0,
			.mask = 0xff,
			.def = 0xa5,
		},
	}, {
		.id = 0x3c,
		.name = "ppcsahbslvw",
		.swgroup = TEGRA_SWGROUP_PPCS,
		.smmu = {
			.reg = 0x22c,
			.bit = 28,
		},
		.la = {
			.reg = 0x348,
			.shift = 16,
			.mask = 0xff,
			.def = 0xe8,
		},
	}, {
		.id = 0x3e,
		.name = "vdebsevw",
		.swgroup = TEGRA_SWGROUP_VDE,
		.smmu = {
			.reg = 0x22c,
			.bit = 30,
		},
		.la = {
			.reg = 0x35c,
			.shift = 0,
			.mask = 0xff,
			.def = 0xff,
		},
	}, {
		.id = 0x3f,
		.name = "vdedbgw",
		.swgroup = TEGRA_SWGROUP_VDE,
		.smmu = {
			.reg = 0x22c,
			.bit = 31,
		},
		.la = {
			.reg = 0x35c,
			.shift = 16,
			.mask = 0xff,
			.def = 0xff,
		},
	}, {
		.id = 0x40,
		.name = "vdembew",
		.swgroup = TEGRA_SWGROUP_VDE,
		.smmu = {
			.reg = 0x230,
			.bit = 0,
		},
		.la = {
			.reg = 0x360,
			.shift = 0,
			.mask = 0xff,
			.def = 0x89,
		},
	}, {
		.id = 0x41,
		.name = "vdetpmw",
		.swgroup = TEGRA_SWGROUP_VDE,
		.smmu = {
			.reg = 0x230,
			.bit = 1,
		},
		.la = {
			.reg = 0x360,
			.shift = 16,
			.mask = 0xff,
			.def = 0x59,
		},
	}, {
		.id = 0x4a,
		.name = "xusb_hostr",
		.swgroup = TEGRA_SWGROUP_XUSB_HOST,
		.smmu = {
			.reg = 0x230,
			.bit = 10,
		},
		.la = {
			.reg = 0x37c,
			.shift = 0,
			.mask = 0xff,
			.def = 0xa5,
		},
	}, {
		.id = 0x4b,
		.name = "xusb_hostw",
		.swgroup = TEGRA_SWGROUP_XUSB_HOST,
		.smmu = {
			.reg = 0x230,
			.bit = 11,
		},
		.la = {
			.reg = 0x37c,
			.shift = 16,
			.mask = 0xff,
			.def = 0xa5,
		},
	}, {
		.id = 0x4c,
		.name = "xusb_devr",
		.swgroup = TEGRA_SWGROUP_XUSB_DEV,
		.smmu = {
			.reg = 0x230,
			.bit = 12,
		},
		.la = {
			.reg = 0x380,
			.shift = 0,
			.mask = 0xff,
			.def = 0xa5,
		},
	}, {
		.id = 0x4d,
		.name = "xusb_devw",
		.swgroup = TEGRA_SWGROUP_XUSB_DEV,
		.smmu = {
			.reg = 0x230,
			.bit = 13,
		},
		.la = {
			.reg = 0x380,
			.shift = 16,
			.mask = 0xff,
			.def = 0xa5,
		},
	}, {
		.id = 0x4e,
		.name = "fdcdwr3",
		.swgroup = TEGRA_SWGROUP_NV,
		.smmu = {
			.reg = 0x230,
			.bit = 14,
		},
		.la = {
			.reg = 0x388,
			.shift = 0,
			.mask = 0xff,
			.def = 0x10,
		},
	}, {
		.id = 0x4f,
		.name = "fdcdrd3",
		.swgroup = TEGRA_SWGROUP_NV,
		.smmu = {
			.reg = 0x230,
			.bit = 15,
		},
		.la = {
			.reg = 0x384,
			.shift = 0,
			.mask = 0xff,
			.def = 0x0c,
		},
	}, {
		.id = 0x50,
		.name = "fdcwr4",
		.swgroup = TEGRA_SWGROUP_NV,
		.smmu = {
			.reg = 0x230,
			.bit = 16,
		},
		.la = {
			.reg = 0x388,
			.shift = 16,
			.mask = 0xff,
			.def = 0x10,
		},
	}, {
		.id = 0x51,
		.name = "fdcrd4",
		.swgroup = TEGRA_SWGROUP_NV,
		.smmu = {
			.reg = 0x230,
			.bit = 17,
		},
		.la = {
			.reg = 0x384,
			.shift = 16,
			.mask = 0xff,
			.def = 0x0c,
		},
	}, {
		.id = 0x52,
		.name = "emucifr",
		.swgroup = TEGRA_SWGROUP_EMUCIF,
		.la = {
			.reg = 0x38c,
			.shift = 0,
			.mask = 0xff,
			.def = 0x04,
		},
	}, {
		.id = 0x53,
		.name = "emucifw",
		.swgroup = TEGRA_SWGROUP_EMUCIF,
		.la = {
			.reg = 0x38c,
			.shift = 16,
			.mask = 0xff,
			.def = 0x0e,
		},
	}, {
		.id = 0x54,
		.name = "tsecsrd",
		.swgroup = TEGRA_SWGROUP_TSEC,
		.smmu = {
			.reg = 0x230,
			.bit = 20,
		},
		.la = {
			.reg = 0x390,
			.shift = 0,
			.mask = 0xff,
			.def = 0x50,
		},
	}, {
		.id = 0x55,
		.name = "tsecswr",
		.swgroup = TEGRA_SWGROUP_TSEC,
		.smmu = {
			.reg = 0x230,
			.bit = 21,
		},
		.la = {
			.reg = 0x390,
			.shift = 16,
			.mask = 0xff,
			.def = 0x50,
		},
	},
};

static const struct tegra_smmu_swgroup tegra114_swgroups[] = {
	{ .name = "dc",        .swgroup = TEGRA_SWGROUP_DC,        .reg = 0x240 },
	{ .name = "dcb",       .swgroup = TEGRA_SWGROUP_DCB,       .reg = 0x244 },
	{ .name = "epp",       .swgroup = TEGRA_SWGROUP_EPP,       .reg = 0x248 },
	{ .name = "g2",        .swgroup = TEGRA_SWGROUP_G2,        .reg = 0x24c },
	{ .name = "avpc",      .swgroup = TEGRA_SWGROUP_AVPC,      .reg = 0x23c },
	{ .name = "nv",        .swgroup = TEGRA_SWGROUP_NV,        .reg = 0x268 },
	{ .name = "hda",       .swgroup = TEGRA_SWGROUP_HDA,       .reg = 0x254 },
	{ .name = "hc",        .swgroup = TEGRA_SWGROUP_HC,        .reg = 0x250 },
	{ .name = "msenc",     .swgroup = TEGRA_SWGROUP_MSENC,     .reg = 0x264 },
	{ .name = "ppcs",      .swgroup = TEGRA_SWGROUP_PPCS,      .reg = 0x270 },
	{ .name = "vde",       .swgroup = TEGRA_SWGROUP_VDE,       .reg = 0x27c },
	{ .name = "vi",        .swgroup = TEGRA_SWGROUP_VI,        .reg = 0x280 },
	{ .name = "isp",       .swgroup = TEGRA_SWGROUP_ISP,       .reg = 0x258 },
	{ .name = "xusb_host", .swgroup = TEGRA_SWGROUP_XUSB_HOST, .reg = 0x288 },
	{ .name = "xusb_dev",  .swgroup = TEGRA_SWGROUP_XUSB_DEV,  .reg = 0x28c },
	{ .name = "tsec",      .swgroup = TEGRA_SWGROUP_TSEC,      .reg = 0x294 },
};

static void tegra114_flush_dcache(struct page *page, unsigned long offset,
				  size_t size)
{
	phys_addr_t phys = page_to_phys(page) + offset;
	void *virt = page_address(page) + offset;

	__cpuc_flush_dcache_area(virt, size);
	outer_flush_range(phys, phys + size);
}

static const struct tegra_smmu_ops tegra114_smmu_ops = {
	.flush_dcache = tegra114_flush_dcache,
};

static const struct tegra_smmu_soc tegra114_smmu_soc = {
	.clients = tegra114_mc_clients,
	.num_clients = ARRAY_SIZE(tegra114_mc_clients),
	.swgroups = tegra114_swgroups,
	.num_swgroups = ARRAY_SIZE(tegra114_swgroups),
	.supports_round_robin_arbitration = false,
	.supports_request_limit = false,
	.num_asids = 4,
	.ops = &tegra114_smmu_ops,
};

const struct tegra_mc_soc tegra114_mc_soc = {
	.clients = tegra114_mc_clients,
	.num_clients = ARRAY_SIZE(tegra114_mc_clients),
	.num_address_bits = 32,
	.atom_size = 32,
	.smmu = &tegra114_smmu_soc,
};
