/*
 * Copyright (C) 2014 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/of.h>
#include <linux/mm.h>

#include <dt-bindings/memory/tegra30-mc.h>

#include "mc.h"

static const struct tegra_mc_client tegra30_mc_clients[] = {
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
		.id = 0x07,
		.name = "display1b",
		.swgroup = TEGRA_SWGROUP_DC,
		.smmu = {
			.reg = 0x228,
			.bit = 7,
		},
		.la = {
			.reg = 0x2ec,
			.shift = 16,
			.mask = 0xff,
			.def = 0x4e,
		},
	}, {
		.id = 0x08,
		.name = "display1bb",
		.swgroup = TEGRA_SWGROUP_DCB,
		.smmu = {
			.reg = 0x228,
			.bit = 8,
		},
		.la = {
			.reg = 0x2f8,
			.shift = 16,
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
			.def = 0x17,
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
		.id = 0x0c,
		.name = "mpeunifbr",
		.swgroup = TEGRA_SWGROUP_MPE,
		.smmu = {
			.reg = 0x228,
			.bit = 12,
		},
		.la = {
			.reg = 0x328,
			.shift = 0,
			.mask = 0xff,
			.def = 0x50,
		},
	}, {
		.id = 0x0d,
		.name = "viruv",
		.swgroup = TEGRA_SWGROUP_VI,
		.smmu = {
			.reg = 0x228,
			.bit = 13,
		},
		.la = {
			.reg = 0x364,
			.shift = 0,
			.mask = 0xff,
			.def = 0x2c,
		},
	}, {
		.id = 0x0e,
		.name = "afir",
		.swgroup = TEGRA_SWGROUP_AFI,
		.smmu = {
			.reg = 0x228,
			.bit = 14,
		},
		.la = {
			.reg = 0x2e0,
			.shift = 0,
			.mask = 0xff,
			.def = 0x10,
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
			.def = 0xff,
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
			.def = 0xff,
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
			.def = 0x0a,
		},
	}, {
		.id = 0x13,
		.name = "fdcdrd2",
		.swgroup = TEGRA_SWGROUP_NV2,
		.smmu = {
			.reg = 0x228,
			.bit = 19,
		},
		.la = {
			.reg = 0x33c,
			.shift = 0,
			.mask = 0xff,
			.def = 0x0a,
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
			.def = 0x05,
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
			.def = 0x50,
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
			.def = 0x13,
		},
	}, {
		.id = 0x19,
		.name = "idxsrd2",
		.swgroup = TEGRA_SWGROUP_NV2,
		.smmu = {
			.reg = 0x228,
			.bit = 25,
		},
		.la = {
			.reg = 0x33c,
			.shift = 16,
			.mask = 0xff,
			.def = 0x13,
		},
	}, {
		.id = 0x1a,
		.name = "mpe_ipred",
		.swgroup = TEGRA_SWGROUP_MPE,
		.smmu = {
			.reg = 0x228,
			.bit = 26,
		},
		.la = {
			.reg = 0x328,
			.shift = 16,
			.mask = 0xff,
			.def = 0x80,
		},
	}, {
		.id = 0x1b,
		.name = "mpeamemrd",
		.swgroup = TEGRA_SWGROUP_MPE,
		.smmu = {
			.reg = 0x228,
			.bit = 27,
		},
		.la = {
			.reg = 0x32c,
			.shift = 0,
			.mask = 0xff,
			.def = 0x42,
		},
	}, {
		.id = 0x1c,
		.name = "mpecsrd",
		.swgroup = TEGRA_SWGROUP_MPE,
		.smmu = {
			.reg = 0x228,
			.bit = 28,
		},
		.la = {
			.reg = 0x32c,
			.shift = 16,
			.mask = 0xff,
			.def = 0xff,
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
			.def = 0x10,
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
			.def = 0x12,
		},
	}, {
		.id = 0x1f,
		.name = "satar",
		.swgroup = TEGRA_SWGROUP_SATA,
		.smmu = {
			.reg = 0x228,
			.bit = 31,
		},
		.la = {
			.reg = 0x350,
			.shift = 0,
			.mask = 0xff,
			.def = 0x33,
		},
	}, {
		.id = 0x20,
		.name = "texsrd",
		.swgroup = TEGRA_SWGROUP_NV,
		.smmu = {
			.reg = 0x22c,
			.bit = 0,
		},
		.la = {
			.reg = 0x338,
			.shift = 0,
			.mask = 0xff,
			.def = 0x13,
		},
	}, {
		.id = 0x21,
		.name = "texsrd2",
		.swgroup = TEGRA_SWGROUP_NV2,
		.smmu = {
			.reg = 0x22c,
			.bit = 1,
		},
		.la = {
			.reg = 0x340,
			.shift = 0,
			.mask = 0xff,
			.def = 0x13,
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
			.def = 0xd0,
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
			.def = 0x2a,
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
			.def = 0x74,
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
			.def = 0x6c,
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
		.name = "mpeunifbw",
		.swgroup = TEGRA_SWGROUP_MPE,
		.smmu = {
			.reg = 0x22c,
			.bit = 11,
		},
		.la = {
			.reg = 0x330,
			.shift = 0,
			.mask = 0xff,
			.def = 0x13,
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
			.shift = 16,
			.mask = 0xff,
			.def = 0x12,
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
			.def = 0xb2,
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
			.def = 0xb2,
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
			.def = 0x12,
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
		.id = 0x31,
		.name = "afiw",
		.swgroup = TEGRA_SWGROUP_AFI,
		.smmu = {
			.reg = 0x22c,
			.bit = 17,
		},
		.la = {
			.reg = 0x2e0,
			.shift = 16,
			.mask = 0xff,
			.def = 0x0c,
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
			.def = 0x0a,
		},
	}, {
		.id = 0x34,
		.name = "fdcwr2",
		.swgroup = TEGRA_SWGROUP_NV2,
		.smmu = {
			.reg = 0x22c,
			.bit = 20,
		},
		.la = {
			.reg = 0x340,
			.shift = 16,
			.mask = 0xff,
			.def = 0x0a,
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
			.def = 0x10,
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
			.def = 0x0e,
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
		.id = 0x3a,
		.name = "mpecswr",
		.swgroup = TEGRA_SWGROUP_MPE,
		.smmu = {
			.reg = 0x22c,
			.bit = 26,
		},
		.la = {
			.reg = 0x330,
			.shift = 16,
			.mask = 0xff,
			.def = 0xff,
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
			.def = 0x10,
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
			.def = 0x06,
		},
	}, {
		.id = 0x3d,
		.name = "sataw",
		.swgroup = TEGRA_SWGROUP_SATA,
		.smmu = {
			.reg = 0x22c,
			.bit = 29,
		},
		.la = {
			.reg = 0x350,
			.shift = 16,
			.mask = 0xff,
			.def = 0x33,
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
			.def = 0x42,
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
			.def = 0x2a,
		},
	},
};

static const struct tegra_smmu_swgroup tegra30_swgroups[] = {
	{ .name = "dc",   .swgroup = TEGRA_SWGROUP_DC,   .reg = 0x240 },
	{ .name = "dcb",  .swgroup = TEGRA_SWGROUP_DCB,  .reg = 0x244 },
	{ .name = "epp",  .swgroup = TEGRA_SWGROUP_EPP,  .reg = 0x248 },
	{ .name = "g2",   .swgroup = TEGRA_SWGROUP_G2,   .reg = 0x24c },
	{ .name = "mpe",  .swgroup = TEGRA_SWGROUP_MPE,  .reg = 0x264 },
	{ .name = "vi",   .swgroup = TEGRA_SWGROUP_VI,   .reg = 0x280 },
	{ .name = "afi",  .swgroup = TEGRA_SWGROUP_AFI,  .reg = 0x238 },
	{ .name = "avpc", .swgroup = TEGRA_SWGROUP_AVPC, .reg = 0x23c },
	{ .name = "nv",   .swgroup = TEGRA_SWGROUP_NV,   .reg = 0x268 },
	{ .name = "nv2",  .swgroup = TEGRA_SWGROUP_NV2,  .reg = 0x26c },
	{ .name = "hda",  .swgroup = TEGRA_SWGROUP_HDA,  .reg = 0x254 },
	{ .name = "hc",   .swgroup = TEGRA_SWGROUP_HC,   .reg = 0x250 },
	{ .name = "ppcs", .swgroup = TEGRA_SWGROUP_PPCS, .reg = 0x270 },
	{ .name = "sata", .swgroup = TEGRA_SWGROUP_SATA, .reg = 0x278 },
	{ .name = "vde",  .swgroup = TEGRA_SWGROUP_VDE,  .reg = 0x27c },
	{ .name = "isp",  .swgroup = TEGRA_SWGROUP_ISP,  .reg = 0x258 },
};

static const unsigned int tegra30_group_display[] = {
	TEGRA_SWGROUP_DC,
	TEGRA_SWGROUP_DCB,
};

static const struct tegra_smmu_group_soc tegra30_groups[] = {
	{
		.name = "display",
		.swgroups = tegra30_group_display,
		.num_swgroups = ARRAY_SIZE(tegra30_group_display),
	},
};

static const struct tegra_smmu_soc tegra30_smmu_soc = {
	.clients = tegra30_mc_clients,
	.num_clients = ARRAY_SIZE(tegra30_mc_clients),
	.swgroups = tegra30_swgroups,
	.num_swgroups = ARRAY_SIZE(tegra30_swgroups),
	.groups = tegra30_groups,
	.num_groups = ARRAY_SIZE(tegra30_groups),
	.supports_round_robin_arbitration = false,
	.supports_request_limit = false,
	.num_tlb_lines = 16,
	.num_asids = 4,
};

#define TEGRA30_MC_RESET(_name, _control, _status, _bit)	\
	{							\
		.name = #_name,					\
		.id = TEGRA30_MC_RESET_##_name,			\
		.control = _control,				\
		.status = _status,				\
		.bit = _bit,					\
	}

static const struct tegra_mc_reset tegra30_mc_resets[] = {
	TEGRA30_MC_RESET(AFI,      0x200, 0x204,  0),
	TEGRA30_MC_RESET(AVPC,     0x200, 0x204,  1),
	TEGRA30_MC_RESET(DC,       0x200, 0x204,  2),
	TEGRA30_MC_RESET(DCB,      0x200, 0x204,  3),
	TEGRA30_MC_RESET(EPP,      0x200, 0x204,  4),
	TEGRA30_MC_RESET(2D,       0x200, 0x204,  5),
	TEGRA30_MC_RESET(HC,       0x200, 0x204,  6),
	TEGRA30_MC_RESET(HDA,      0x200, 0x204,  7),
	TEGRA30_MC_RESET(ISP,      0x200, 0x204,  8),
	TEGRA30_MC_RESET(MPCORE,   0x200, 0x204,  9),
	TEGRA30_MC_RESET(MPCORELP, 0x200, 0x204, 10),
	TEGRA30_MC_RESET(MPE,      0x200, 0x204, 11),
	TEGRA30_MC_RESET(3D,       0x200, 0x204, 12),
	TEGRA30_MC_RESET(3D2,      0x200, 0x204, 13),
	TEGRA30_MC_RESET(PPCS,     0x200, 0x204, 14),
	TEGRA30_MC_RESET(SATA,     0x200, 0x204, 15),
	TEGRA30_MC_RESET(VDE,      0x200, 0x204, 16),
	TEGRA30_MC_RESET(VI,       0x200, 0x204, 17),
};

const struct tegra_mc_soc tegra30_mc_soc = {
	.clients = tegra30_mc_clients,
	.num_clients = ARRAY_SIZE(tegra30_mc_clients),
	.num_address_bits = 32,
	.atom_size = 16,
	.client_id_mask = 0x7f,
	.smmu = &tegra30_smmu_soc,
	.intmask = MC_INT_INVALID_SMMU_PAGE | MC_INT_SECURITY_VIOLATION |
		   MC_INT_DECERR_EMEM,
	.reset_ops = &terga_mc_reset_ops_common,
	.resets = tegra30_mc_resets,
	.num_resets = ARRAY_SIZE(tegra30_mc_resets),
};
