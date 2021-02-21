// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>

#include <dt-bindings/memory/tegra124-mc.h>

#include "mc.h"

static const struct tegra_mc_client tegra124_mc_clients[] = {
	{
		.id = 0x00,
		.name = "ptcr",
		.swgroup = TEGRA_SWGROUP_PTC,
		.la = {
			.reg = 0x34c,
			.shift = 0,
			.mask = 0xff,
			.def = 0x0,
		},
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
			.def = 0xc2,
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
			.def = 0xc6,
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
			.def = 0x50,
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
			.def = 0x50,
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
			.def = 0x50,
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
			.def = 0x50,
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
			.def = 0x13,
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
			.def = 0x50,
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
			.def = 0x50,
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
			.def = 0x24,
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
			.def = 0x1e,
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
			.def = 0x23,
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
			.def = 0x49,
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
			.def = 0x1a,
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
			.def = 0x65,
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
			.def = 0x4f,
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
			.def = 0x3d,
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
			.def = 0x66,
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
			.def = 0xa5,
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
			.def = 0x80,
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
			.def = 0x80,
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
			.def = 0x80,
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
			.def = 0x80,
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
			.def = 0x80,
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
			.def = 0x80,
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
			.def = 0x80,
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
			.def = 0x65,
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
			.def = 0x80,
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
			.def = 0x80,
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
			.def = 0x80,
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
			.def = 0x80,
		},
	}, {
		.id = 0x44,
		.name = "ispra",
		.swgroup = TEGRA_SWGROUP_ISP2,
		.smmu = {
			.reg = 0x230,
			.bit = 4,
		},
		.la = {
			.reg = 0x370,
			.shift = 0,
			.mask = 0xff,
			.def = 0x18,
		},
	}, {
		.id = 0x46,
		.name = "ispwa",
		.swgroup = TEGRA_SWGROUP_ISP2,
		.smmu = {
			.reg = 0x230,
			.bit = 6,
		},
		.la = {
			.reg = 0x374,
			.shift = 0,
			.mask = 0xff,
			.def = 0x80,
		},
	}, {
		.id = 0x47,
		.name = "ispwb",
		.swgroup = TEGRA_SWGROUP_ISP2,
		.smmu = {
			.reg = 0x230,
			.bit = 7,
		},
		.la = {
			.reg = 0x374,
			.shift = 16,
			.mask = 0xff,
			.def = 0x80,
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
			.def = 0x39,
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
			.def = 0x80,
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
			.def = 0x39,
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
			.def = 0x80,
		},
	}, {
		.id = 0x4e,
		.name = "isprab",
		.swgroup = TEGRA_SWGROUP_ISP2B,
		.smmu = {
			.reg = 0x230,
			.bit = 14,
		},
		.la = {
			.reg = 0x384,
			.shift = 0,
			.mask = 0xff,
			.def = 0x18,
		},
	}, {
		.id = 0x50,
		.name = "ispwab",
		.swgroup = TEGRA_SWGROUP_ISP2B,
		.smmu = {
			.reg = 0x230,
			.bit = 16,
		},
		.la = {
			.reg = 0x388,
			.shift = 0,
			.mask = 0xff,
			.def = 0x80,
		},
	}, {
		.id = 0x51,
		.name = "ispwbb",
		.swgroup = TEGRA_SWGROUP_ISP2B,
		.smmu = {
			.reg = 0x230,
			.bit = 17,
		},
		.la = {
			.reg = 0x388,
			.shift = 16,
			.mask = 0xff,
			.def = 0x80,
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
			.def = 0x9b,
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
			.def = 0x80,
		},
	}, {
		.id = 0x56,
		.name = "a9avpscr",
		.swgroup = TEGRA_SWGROUP_A9AVP,
		.smmu = {
			.reg = 0x230,
			.bit = 22,
		},
		.la = {
			.reg = 0x3a4,
			.shift = 0,
			.mask = 0xff,
			.def = 0x04,
		},
	}, {
		.id = 0x57,
		.name = "a9avpscw",
		.swgroup = TEGRA_SWGROUP_A9AVP,
		.smmu = {
			.reg = 0x230,
			.bit = 23,
		},
		.la = {
			.reg = 0x3a4,
			.shift = 16,
			.mask = 0xff,
			.def = 0x80,
		},
	}, {
		.id = 0x58,
		.name = "gpusrd",
		.swgroup = TEGRA_SWGROUP_GPU,
		.smmu = {
			/* read-only */
			.reg = 0x230,
			.bit = 24,
		},
		.la = {
			.reg = 0x3c8,
			.shift = 0,
			.mask = 0xff,
			.def = 0x1a,
		},
	}, {
		.id = 0x59,
		.name = "gpuswr",
		.swgroup = TEGRA_SWGROUP_GPU,
		.smmu = {
			/* read-only */
			.reg = 0x230,
			.bit = 25,
		},
		.la = {
			.reg = 0x3c8,
			.shift = 16,
			.mask = 0xff,
			.def = 0x80,
		},
	}, {
		.id = 0x5a,
		.name = "displayt",
		.swgroup = TEGRA_SWGROUP_DC,
		.smmu = {
			.reg = 0x230,
			.bit = 26,
		},
		.la = {
			.reg = 0x2f0,
			.shift = 16,
			.mask = 0xff,
			.def = 0x50,
		},
	}, {
		.id = 0x60,
		.name = "sdmmcra",
		.swgroup = TEGRA_SWGROUP_SDMMC1A,
		.smmu = {
			.reg = 0x234,
			.bit = 0,
		},
		.la = {
			.reg = 0x3b8,
			.shift = 0,
			.mask = 0xff,
			.def = 0x49,
		},
	}, {
		.id = 0x61,
		.name = "sdmmcraa",
		.swgroup = TEGRA_SWGROUP_SDMMC2A,
		.smmu = {
			.reg = 0x234,
			.bit = 1,
		},
		.la = {
			.reg = 0x3bc,
			.shift = 0,
			.mask = 0xff,
			.def = 0x49,
		},
	}, {
		.id = 0x62,
		.name = "sdmmcr",
		.swgroup = TEGRA_SWGROUP_SDMMC3A,
		.smmu = {
			.reg = 0x234,
			.bit = 2,
		},
		.la = {
			.reg = 0x3c0,
			.shift = 0,
			.mask = 0xff,
			.def = 0x49,
		},
	}, {
		.id = 0x63,
		.swgroup = TEGRA_SWGROUP_SDMMC4A,
		.name = "sdmmcrab",
		.smmu = {
			.reg = 0x234,
			.bit = 3,
		},
		.la = {
			.reg = 0x3c4,
			.shift = 0,
			.mask = 0xff,
			.def = 0x49,
		},
	}, {
		.id = 0x64,
		.name = "sdmmcwa",
		.swgroup = TEGRA_SWGROUP_SDMMC1A,
		.smmu = {
			.reg = 0x234,
			.bit = 4,
		},
		.la = {
			.reg = 0x3b8,
			.shift = 16,
			.mask = 0xff,
			.def = 0x80,
		},
	}, {
		.id = 0x65,
		.name = "sdmmcwaa",
		.swgroup = TEGRA_SWGROUP_SDMMC2A,
		.smmu = {
			.reg = 0x234,
			.bit = 5,
		},
		.la = {
			.reg = 0x3bc,
			.shift = 16,
			.mask = 0xff,
			.def = 0x80,
		},
	}, {
		.id = 0x66,
		.name = "sdmmcw",
		.swgroup = TEGRA_SWGROUP_SDMMC3A,
		.smmu = {
			.reg = 0x234,
			.bit = 6,
		},
		.la = {
			.reg = 0x3c0,
			.shift = 16,
			.mask = 0xff,
			.def = 0x80,
		},
	}, {
		.id = 0x67,
		.name = "sdmmcwab",
		.swgroup = TEGRA_SWGROUP_SDMMC4A,
		.smmu = {
			.reg = 0x234,
			.bit = 7,
		},
		.la = {
			.reg = 0x3c4,
			.shift = 16,
			.mask = 0xff,
			.def = 0x80,
		},
	}, {
		.id = 0x6c,
		.name = "vicsrd",
		.swgroup = TEGRA_SWGROUP_VIC,
		.smmu = {
			.reg = 0x234,
			.bit = 12,
		},
		.la = {
			.reg = 0x394,
			.shift = 0,
			.mask = 0xff,
			.def = 0x1a,
		},
	}, {
		.id = 0x6d,
		.name = "vicswr",
		.swgroup = TEGRA_SWGROUP_VIC,
		.smmu = {
			.reg = 0x234,
			.bit = 13,
		},
		.la = {
			.reg = 0x394,
			.shift = 16,
			.mask = 0xff,
			.def = 0x80,
		},
	}, {
		.id = 0x72,
		.name = "viw",
		.swgroup = TEGRA_SWGROUP_VI,
		.smmu = {
			.reg = 0x234,
			.bit = 18,
		},
		.la = {
			.reg = 0x398,
			.shift = 0,
			.mask = 0xff,
			.def = 0x80,
		},
	}, {
		.id = 0x73,
		.name = "displayd",
		.swgroup = TEGRA_SWGROUP_DC,
		.smmu = {
			.reg = 0x234,
			.bit = 19,
		},
		.la = {
			.reg = 0x3c8,
			.shift = 0,
			.mask = 0xff,
			.def = 0x50,
		},
	},
};

static const struct tegra_smmu_swgroup tegra124_swgroups[] = {
	{ .name = "dc",        .swgroup = TEGRA_SWGROUP_DC,        .reg = 0x240 },
	{ .name = "dcb",       .swgroup = TEGRA_SWGROUP_DCB,       .reg = 0x244 },
	{ .name = "afi",       .swgroup = TEGRA_SWGROUP_AFI,       .reg = 0x238 },
	{ .name = "avpc",      .swgroup = TEGRA_SWGROUP_AVPC,      .reg = 0x23c },
	{ .name = "hda",       .swgroup = TEGRA_SWGROUP_HDA,       .reg = 0x254 },
	{ .name = "hc",        .swgroup = TEGRA_SWGROUP_HC,        .reg = 0x250 },
	{ .name = "msenc",     .swgroup = TEGRA_SWGROUP_MSENC,     .reg = 0x264 },
	{ .name = "ppcs",      .swgroup = TEGRA_SWGROUP_PPCS,      .reg = 0x270 },
	{ .name = "sata",      .swgroup = TEGRA_SWGROUP_SATA,      .reg = 0x274 },
	{ .name = "vde",       .swgroup = TEGRA_SWGROUP_VDE,       .reg = 0x27c },
	{ .name = "isp2",      .swgroup = TEGRA_SWGROUP_ISP2,      .reg = 0x258 },
	{ .name = "xusb_host", .swgroup = TEGRA_SWGROUP_XUSB_HOST, .reg = 0x288 },
	{ .name = "xusb_dev",  .swgroup = TEGRA_SWGROUP_XUSB_DEV,  .reg = 0x28c },
	{ .name = "isp2b",     .swgroup = TEGRA_SWGROUP_ISP2B,     .reg = 0xaa4 },
	{ .name = "tsec",      .swgroup = TEGRA_SWGROUP_TSEC,      .reg = 0x294 },
	{ .name = "a9avp",     .swgroup = TEGRA_SWGROUP_A9AVP,     .reg = 0x290 },
	{ .name = "gpu",       .swgroup = TEGRA_SWGROUP_GPU,       .reg = 0xaac },
	{ .name = "sdmmc1a",   .swgroup = TEGRA_SWGROUP_SDMMC1A,   .reg = 0xa94 },
	{ .name = "sdmmc2a",   .swgroup = TEGRA_SWGROUP_SDMMC2A,   .reg = 0xa98 },
	{ .name = "sdmmc3a",   .swgroup = TEGRA_SWGROUP_SDMMC3A,   .reg = 0xa9c },
	{ .name = "sdmmc4a",   .swgroup = TEGRA_SWGROUP_SDMMC4A,   .reg = 0xaa0 },
	{ .name = "vic",       .swgroup = TEGRA_SWGROUP_VIC,       .reg = 0x284 },
	{ .name = "vi",        .swgroup = TEGRA_SWGROUP_VI,        .reg = 0x280 },
};

static const unsigned int tegra124_group_drm[] = {
	TEGRA_SWGROUP_DC,
	TEGRA_SWGROUP_DCB,
	TEGRA_SWGROUP_VIC,
};

static const struct tegra_smmu_group_soc tegra124_groups[] = {
	{
		.name = "drm",
		.swgroups = tegra124_group_drm,
		.num_swgroups = ARRAY_SIZE(tegra124_group_drm),
	},
};

#define TEGRA124_MC_RESET(_name, _control, _status, _bit)	\
	{							\
		.name = #_name,					\
		.id = TEGRA124_MC_RESET_##_name,		\
		.control = _control,				\
		.status = _status,				\
		.bit = _bit,					\
	}

static const struct tegra_mc_reset tegra124_mc_resets[] = {
	TEGRA124_MC_RESET(AFI,       0x200, 0x204,  0),
	TEGRA124_MC_RESET(AVPC,      0x200, 0x204,  1),
	TEGRA124_MC_RESET(DC,        0x200, 0x204,  2),
	TEGRA124_MC_RESET(DCB,       0x200, 0x204,  3),
	TEGRA124_MC_RESET(HC,        0x200, 0x204,  6),
	TEGRA124_MC_RESET(HDA,       0x200, 0x204,  7),
	TEGRA124_MC_RESET(ISP2,      0x200, 0x204,  8),
	TEGRA124_MC_RESET(MPCORE,    0x200, 0x204,  9),
	TEGRA124_MC_RESET(MPCORELP,  0x200, 0x204, 10),
	TEGRA124_MC_RESET(MSENC,     0x200, 0x204, 11),
	TEGRA124_MC_RESET(PPCS,      0x200, 0x204, 14),
	TEGRA124_MC_RESET(SATA,      0x200, 0x204, 15),
	TEGRA124_MC_RESET(VDE,       0x200, 0x204, 16),
	TEGRA124_MC_RESET(VI,        0x200, 0x204, 17),
	TEGRA124_MC_RESET(VIC,       0x200, 0x204, 18),
	TEGRA124_MC_RESET(XUSB_HOST, 0x200, 0x204, 19),
	TEGRA124_MC_RESET(XUSB_DEV,  0x200, 0x204, 20),
	TEGRA124_MC_RESET(TSEC,      0x200, 0x204, 21),
	TEGRA124_MC_RESET(SDMMC1,    0x200, 0x204, 22),
	TEGRA124_MC_RESET(SDMMC2,    0x200, 0x204, 23),
	TEGRA124_MC_RESET(SDMMC3,    0x200, 0x204, 25),
	TEGRA124_MC_RESET(SDMMC4,    0x970, 0x974,  0),
	TEGRA124_MC_RESET(ISP2B,     0x970, 0x974,  1),
	TEGRA124_MC_RESET(GPU,       0x970, 0x974,  2),
};

static int tegra124_mc_icc_set(struct icc_node *src, struct icc_node *dst)
{
	/* TODO: program PTSA */
	return 0;
}

static int tegra124_mc_icc_aggreate(struct icc_node *node, u32 tag, u32 avg_bw,
				    u32 peak_bw, u32 *agg_avg, u32 *agg_peak)
{
	/*
	 * ISO clients need to reserve extra bandwidth up-front because
	 * there could be high bandwidth pressure during initial filling
	 * of the client's FIFO buffers.  Secondly, we need to take into
	 * account impurities of the memory subsystem.
	 */
	if (tag & TEGRA_MC_ICC_TAG_ISO)
		peak_bw = tegra_mc_scale_percents(peak_bw, 400);

	*agg_avg += avg_bw;
	*agg_peak = max(*agg_peak, peak_bw);

	return 0;
}

static struct icc_node_data *
tegra124_mc_of_icc_xlate_extended(struct of_phandle_args *spec, void *data)
{
	struct tegra_mc *mc = icc_provider_to_tegra_mc(data);
	const struct tegra_mc_client *client;
	unsigned int i, idx = spec->args[0];
	struct icc_node_data *ndata;
	struct icc_node *node;

	list_for_each_entry(node, &mc->provider.nodes, node_list) {
		if (node->id != idx)
			continue;

		ndata = kzalloc(sizeof(*ndata), GFP_KERNEL);
		if (!ndata)
			return ERR_PTR(-ENOMEM);

		client = &mc->soc->clients[idx];
		ndata->node = node;

		switch (client->swgroup) {
		case TEGRA_SWGROUP_DC:
		case TEGRA_SWGROUP_DCB:
		case TEGRA_SWGROUP_PTC:
		case TEGRA_SWGROUP_VI:
			/* these clients are isochronous by default */
			ndata->tag = TEGRA_MC_ICC_TAG_ISO;
			break;

		default:
			ndata->tag = TEGRA_MC_ICC_TAG_DEFAULT;
			break;
		}

		return ndata;
	}

	for (i = 0; i < mc->soc->num_clients; i++) {
		if (mc->soc->clients[i].id == idx)
			return ERR_PTR(-EPROBE_DEFER);
	}

	dev_err(mc->dev, "invalid ICC client ID %u\n", idx);

	return ERR_PTR(-EINVAL);
}

static const struct tegra_mc_icc_ops tegra124_mc_icc_ops = {
	.xlate_extended = tegra124_mc_of_icc_xlate_extended,
	.aggregate = tegra124_mc_icc_aggreate,
	.set = tegra124_mc_icc_set,
};

#ifdef CONFIG_ARCH_TEGRA_124_SOC
static const unsigned long tegra124_mc_emem_regs[] = {
	MC_EMEM_ARB_CFG,
	MC_EMEM_ARB_OUTSTANDING_REQ,
	MC_EMEM_ARB_TIMING_RCD,
	MC_EMEM_ARB_TIMING_RP,
	MC_EMEM_ARB_TIMING_RC,
	MC_EMEM_ARB_TIMING_RAS,
	MC_EMEM_ARB_TIMING_FAW,
	MC_EMEM_ARB_TIMING_RRD,
	MC_EMEM_ARB_TIMING_RAP2PRE,
	MC_EMEM_ARB_TIMING_WAP2PRE,
	MC_EMEM_ARB_TIMING_R2R,
	MC_EMEM_ARB_TIMING_W2W,
	MC_EMEM_ARB_TIMING_R2W,
	MC_EMEM_ARB_TIMING_W2R,
	MC_EMEM_ARB_DA_TURNS,
	MC_EMEM_ARB_DA_COVERS,
	MC_EMEM_ARB_MISC0,
	MC_EMEM_ARB_MISC1,
	MC_EMEM_ARB_RING1_THROTTLE
};

static const struct tegra_smmu_soc tegra124_smmu_soc = {
	.clients = tegra124_mc_clients,
	.num_clients = ARRAY_SIZE(tegra124_mc_clients),
	.swgroups = tegra124_swgroups,
	.num_swgroups = ARRAY_SIZE(tegra124_swgroups),
	.groups = tegra124_groups,
	.num_groups = ARRAY_SIZE(tegra124_groups),
	.supports_round_robin_arbitration = true,
	.supports_request_limit = true,
	.num_tlb_lines = 32,
	.num_asids = 128,
};

const struct tegra_mc_soc tegra124_mc_soc = {
	.clients = tegra124_mc_clients,
	.num_clients = ARRAY_SIZE(tegra124_mc_clients),
	.num_address_bits = 34,
	.atom_size = 32,
	.client_id_mask = 0x7f,
	.smmu = &tegra124_smmu_soc,
	.emem_regs = tegra124_mc_emem_regs,
	.num_emem_regs = ARRAY_SIZE(tegra124_mc_emem_regs),
	.intmask = MC_INT_DECERR_MTS | MC_INT_SECERR_SEC | MC_INT_DECERR_VPR |
		   MC_INT_INVALID_APB_ASID_UPDATE | MC_INT_INVALID_SMMU_PAGE |
		   MC_INT_SECURITY_VIOLATION | MC_INT_DECERR_EMEM,
	.reset_ops = &tegra_mc_reset_ops_common,
	.resets = tegra124_mc_resets,
	.num_resets = ARRAY_SIZE(tegra124_mc_resets),
	.icc_ops = &tegra124_mc_icc_ops,
};
#endif /* CONFIG_ARCH_TEGRA_124_SOC */

#ifdef CONFIG_ARCH_TEGRA_132_SOC
static const struct tegra_smmu_soc tegra132_smmu_soc = {
	.clients = tegra124_mc_clients,
	.num_clients = ARRAY_SIZE(tegra124_mc_clients),
	.swgroups = tegra124_swgroups,
	.num_swgroups = ARRAY_SIZE(tegra124_swgroups),
	.groups = tegra124_groups,
	.num_groups = ARRAY_SIZE(tegra124_groups),
	.supports_round_robin_arbitration = true,
	.supports_request_limit = true,
	.num_tlb_lines = 32,
	.num_asids = 128,
};

const struct tegra_mc_soc tegra132_mc_soc = {
	.clients = tegra124_mc_clients,
	.num_clients = ARRAY_SIZE(tegra124_mc_clients),
	.num_address_bits = 34,
	.atom_size = 32,
	.client_id_mask = 0x7f,
	.smmu = &tegra132_smmu_soc,
	.intmask = MC_INT_DECERR_MTS | MC_INT_SECERR_SEC | MC_INT_DECERR_VPR |
		   MC_INT_INVALID_APB_ASID_UPDATE | MC_INT_INVALID_SMMU_PAGE |
		   MC_INT_SECURITY_VIOLATION | MC_INT_DECERR_EMEM,
	.reset_ops = &tegra_mc_reset_ops_common,
	.resets = tegra124_mc_resets,
	.num_resets = ARRAY_SIZE(tegra124_mc_resets),
	.icc_ops = &tegra124_mc_icc_ops,
};
#endif /* CONFIG_ARCH_TEGRA_132_SOC */
