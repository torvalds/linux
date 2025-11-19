// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 NVIDIA CORPORATION.  All rights reserved.
 */

#include <dt-bindings/memory/tegra210-mc.h>

#include "mc.h"

static const struct tegra_mc_client tegra210_mc_clients[] = {
	{
		.id = TEGRA210_MC_PTCR,
		.name = "ptcr",
		.swgroup = TEGRA_SWGROUP_PTC,
	}, {
		.id = TEGRA210_MC_DISPLAY0A,
		.name = "display0a",
		.swgroup = TEGRA_SWGROUP_DC,
		.regs = {
			.smmu = {
				.reg = 0x228,
				.bit = 1,
			},
			.la = {
				.reg = 0x2e8,
				.shift = 0,
				.mask = 0xff,
				.def = 0x1e,
			},
		},
	}, {
		.id = TEGRA210_MC_DISPLAY0AB,
		.name = "display0ab",
		.swgroup = TEGRA_SWGROUP_DCB,
		.regs = {
			.smmu = {
				.reg = 0x228,
				.bit = 2,
			},
			.la = {
				.reg = 0x2f4,
				.shift = 0,
				.mask = 0xff,
				.def = 0x1e,
			},
		},
	}, {
		.id = TEGRA210_MC_DISPLAY0B,
		.name = "display0b",
		.swgroup = TEGRA_SWGROUP_DC,
		.regs = {
			.smmu = {
				.reg = 0x228,
				.bit = 3,
			},
			.la = {
				.reg = 0x2e8,
				.shift = 16,
				.mask = 0xff,
				.def = 0x1e,
			},
		},
	}, {
		.id = TEGRA210_MC_DISPLAY0BB,
		.name = "display0bb",
		.swgroup = TEGRA_SWGROUP_DCB,
		.regs = {
			.smmu = {
				.reg = 0x228,
				.bit = 4,
			},
			.la = {
				.reg = 0x2f4,
				.shift = 16,
				.mask = 0xff,
				.def = 0x1e,
			},
		},
	}, {
		.id = TEGRA210_MC_DISPLAY0C,
		.name = "display0c",
		.swgroup = TEGRA_SWGROUP_DC,
		.regs = {
			.smmu = {
				.reg = 0x228,
				.bit = 5,
			},
			.la = {
				.reg = 0x2ec,
				.shift = 0,
				.mask = 0xff,
				.def = 0x1e,
			},
		},
	}, {
		.id = TEGRA210_MC_DISPLAY0CB,
		.name = "display0cb",
		.swgroup = TEGRA_SWGROUP_DCB,
		.regs = {
			.smmu = {
				.reg = 0x228,
				.bit = 6,
			},
			.la = {
				.reg = 0x2f8,
				.shift = 0,
				.mask = 0xff,
				.def = 0x1e,
			},
		},
	}, {
		.id = TEGRA210_MC_AFIR,
		.name = "afir",
		.swgroup = TEGRA_SWGROUP_AFI,
		.regs = {
			.smmu = {
				.reg = 0x228,
				.bit = 14,
			},
			.la = {
				.reg = 0x2e0,
				.shift = 0,
				.mask = 0xff,
				.def = 0x2e,
			},
		},
	}, {
		.id = TEGRA210_MC_AVPCARM7R,
		.name = "avpcarm7r",
		.swgroup = TEGRA_SWGROUP_AVPC,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_DISPLAYHC,
		.name = "displayhc",
		.swgroup = TEGRA_SWGROUP_DC,
		.regs = {
			.smmu = {
				.reg = 0x228,
				.bit = 16,
			},
			.la = {
				.reg = 0x2f0,
				.shift = 0,
				.mask = 0xff,
				.def = 0x1e,
			},
		},
	}, {
		.id = TEGRA210_MC_DISPLAYHCB,
		.name = "displayhcb",
		.swgroup = TEGRA_SWGROUP_DCB,
		.regs = {
			.smmu = {
				.reg = 0x228,
				.bit = 17,
			},
			.la = {
				.reg = 0x2fc,
				.shift = 0,
				.mask = 0xff,
				.def = 0x1e,
			},
		},
	}, {
		.id = TEGRA210_MC_HDAR,
		.name = "hdar",
		.swgroup = TEGRA_SWGROUP_HDA,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_HOST1XDMAR,
		.name = "host1xdmar",
		.swgroup = TEGRA_SWGROUP_HC,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_HOST1XR,
		.name = "host1xr",
		.swgroup = TEGRA_SWGROUP_HC,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_NVENCSRD,
		.name = "nvencsrd",
		.swgroup = TEGRA_SWGROUP_NVENC,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_PPCSAHBDMAR,
		.name = "ppcsahbdmar",
		.swgroup = TEGRA_SWGROUP_PPCS,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_PPCSAHBSLVR,
		.name = "ppcsahbslvr",
		.swgroup = TEGRA_SWGROUP_PPCS,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_SATAR,
		.name = "satar",
		.swgroup = TEGRA_SWGROUP_SATA,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_MPCORER,
		.name = "mpcorer",
		.swgroup = TEGRA_SWGROUP_MPCORE,
		.regs = {
			.la = {
				.reg = 0x320,
				.shift = 0,
				.mask = 0xff,
				.def = 0x04,
			},
		},
	}, {
		.id = TEGRA210_MC_NVENCSWR,
		.name = "nvencswr",
		.swgroup = TEGRA_SWGROUP_NVENC,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_AFIW,
		.name = "afiw",
		.swgroup = TEGRA_SWGROUP_AFI,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_AVPCARM7W,
		.name = "avpcarm7w",
		.swgroup = TEGRA_SWGROUP_AVPC,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_HDAW,
		.name = "hdaw",
		.swgroup = TEGRA_SWGROUP_HDA,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_HOST1XW,
		.name = "host1xw",
		.swgroup = TEGRA_SWGROUP_HC,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_MPCOREW,
		.name = "mpcorew",
		.swgroup = TEGRA_SWGROUP_MPCORE,
		.regs = {
			.la = {
				.reg = 0x320,
				.shift = 16,
				.mask = 0xff,
				.def = 0x80,
			},
		},
	}, {
		.id = TEGRA210_MC_PPCSAHBDMAW,
		.name = "ppcsahbdmaw",
		.swgroup = TEGRA_SWGROUP_PPCS,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_PPCSAHBSLVW,
		.name = "ppcsahbslvw",
		.swgroup = TEGRA_SWGROUP_PPCS,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_SATAW,
		.name = "sataw",
		.swgroup = TEGRA_SWGROUP_SATA,
		.regs = {
			.smmu = {
				.reg = 0x22c,
				.bit = 29,
			},
			.la = {
				.reg = 0x350,
				.shift = 16,
				.mask = 0xff,
				.def = 0x80,
			},
		},
	}, {
		.id = TEGRA210_MC_ISPRA,
		.name = "ispra",
		.swgroup = TEGRA_SWGROUP_ISP2,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_ISPWA,
		.name = "ispwa",
		.swgroup = TEGRA_SWGROUP_ISP2,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_ISPWB,
		.name = "ispwb",
		.swgroup = TEGRA_SWGROUP_ISP2,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_XUSB_HOSTR,
		.name = "xusb_hostr",
		.swgroup = TEGRA_SWGROUP_XUSB_HOST,
		.regs = {
			.smmu = {
				.reg = 0x230,
				.bit = 10,
			},
			.la = {
				.reg = 0x37c,
				.shift = 0,
				.mask = 0xff,
				.def = 0x7a,
			},
		},
	}, {
		.id = TEGRA210_MC_XUSB_HOSTW,
		.name = "xusb_hostw",
		.swgroup = TEGRA_SWGROUP_XUSB_HOST,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_XUSB_DEVR,
		.name = "xusb_devr",
		.swgroup = TEGRA_SWGROUP_XUSB_DEV,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_XUSB_DEVW,
		.name = "xusb_devw",
		.swgroup = TEGRA_SWGROUP_XUSB_DEV,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_ISPRAB,
		.name = "isprab",
		.swgroup = TEGRA_SWGROUP_ISP2B,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_ISPWAB,
		.name = "ispwab",
		.swgroup = TEGRA_SWGROUP_ISP2B,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_ISPWBB,
		.name = "ispwbb",
		.swgroup = TEGRA_SWGROUP_ISP2B,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_TSECSRD,
		.name = "tsecsrd",
		.swgroup = TEGRA_SWGROUP_TSEC,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_TSECSWR,
		.name = "tsecswr",
		.swgroup = TEGRA_SWGROUP_TSEC,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_A9AVPSCR,
		.name = "a9avpscr",
		.swgroup = TEGRA_SWGROUP_A9AVP,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_A9AVPSCW,
		.name = "a9avpscw",
		.swgroup = TEGRA_SWGROUP_A9AVP,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_GPUSRD,
		.name = "gpusrd",
		.swgroup = TEGRA_SWGROUP_GPU,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_GPUSWR,
		.name = "gpuswr",
		.swgroup = TEGRA_SWGROUP_GPU,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_DISPLAYT,
		.name = "displayt",
		.swgroup = TEGRA_SWGROUP_DC,
		.regs = {
			.smmu = {
				.reg = 0x230,
				.bit = 26,
			},
			.la = {
				.reg = 0x2f0,
				.shift = 16,
				.mask = 0xff,
				.def = 0x1e,
			},
		},
	}, {
		.id = TEGRA210_MC_SDMMCRA,
		.name = "sdmmcra",
		.swgroup = TEGRA_SWGROUP_SDMMC1A,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_SDMMCRAA,
		.name = "sdmmcraa",
		.swgroup = TEGRA_SWGROUP_SDMMC2A,
		.regs = {
			.smmu = {
				.reg = 0x234,
				.bit = 1,
			},
			.la = {
				.reg = 0x3bc,
				.shift = 0,
				.mask = 0xff,
				.def = 0x5a,
			},
		},
	}, {
		.id = TEGRA210_MC_SDMMCR,
		.name = "sdmmcr",
		.swgroup = TEGRA_SWGROUP_SDMMC3A,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_SDMMCRAB,
		.swgroup = TEGRA_SWGROUP_SDMMC4A,
		.name = "sdmmcrab",
		.regs = {
			.smmu = {
				.reg = 0x234,
				.bit = 3,
			},
			.la = {
				.reg = 0x3c4,
				.shift = 0,
				.mask = 0xff,
				.def = 0x5a,
			},
		},
	}, {
		.id = TEGRA210_MC_SDMMCWA,
		.name = "sdmmcwa",
		.swgroup = TEGRA_SWGROUP_SDMMC1A,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_SDMMCWAA,
		.name = "sdmmcwaa",
		.swgroup = TEGRA_SWGROUP_SDMMC2A,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_SDMMCW,
		.name = "sdmmcw",
		.swgroup = TEGRA_SWGROUP_SDMMC3A,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_SDMMCWAB,
		.name = "sdmmcwab",
		.swgroup = TEGRA_SWGROUP_SDMMC4A,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_VICSRD,
		.name = "vicsrd",
		.swgroup = TEGRA_SWGROUP_VIC,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_VICSWR,
		.name = "vicswr",
		.swgroup = TEGRA_SWGROUP_VIC,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_VIW,
		.name = "viw",
		.swgroup = TEGRA_SWGROUP_VI,
		.regs = {
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
		},
	}, {
		.id = TEGRA210_MC_DISPLAYD,
		.name = "displayd",
		.swgroup = TEGRA_SWGROUP_DC,
		.regs = {
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
	}, {
		.id = TEGRA210_MC_NVDECSRD,
		.name = "nvdecsrd",
		.swgroup = TEGRA_SWGROUP_NVDEC,
		.regs = {
			.smmu = {
				.reg = 0x234,
				.bit = 24,
			},
			.la = {
				.reg = 0x3d8,
				.shift = 0,
				.mask = 0xff,
				.def = 0x23,
			},
		},
	}, {
		.id = TEGRA210_MC_NVDECSWR,
		.name = "nvdecswr",
		.swgroup = TEGRA_SWGROUP_NVDEC,
		.regs = {
			.smmu = {
				.reg = 0x234,
				.bit = 25,
			},
			.la = {
				.reg = 0x3d8,
				.shift = 16,
				.mask = 0xff,
				.def = 0x80,
			},
		},
	}, {
		.id = TEGRA210_MC_APER,
		.name = "aper",
		.swgroup = TEGRA_SWGROUP_APE,
		.regs = {
			.smmu = {
				.reg = 0x234,
				.bit = 26,
			},
			.la = {
				.reg = 0x3dc,
				.shift = 0,
				.mask = 0xff,
				.def = 0xff,
			},
		},
	}, {
		.id = TEGRA210_MC_APEW,
		.name = "apew",
		.swgroup = TEGRA_SWGROUP_APE,
		.regs = {
			.smmu = {
				.reg = 0x234,
				.bit = 27,
			},
			.la = {
				.reg = 0x3dc,
				.shift = 16,
				.mask = 0xff,
				.def = 0x80,
			},
		},
	}, {
		.id = TEGRA210_MC_NVJPGRD,
		.name = "nvjpgsrd",
		.swgroup = TEGRA_SWGROUP_NVJPG,
		.regs = {
			.smmu = {
				.reg = 0x234,
				.bit = 30,
			},
			.la = {
				.reg = 0x3e4,
				.shift = 0,
				.mask = 0xff,
				.def = 0x23,
			},
		},
	}, {
		.id = TEGRA210_MC_NVJPGWR,
		.name = "nvjpgswr",
		.swgroup = TEGRA_SWGROUP_NVJPG,
		.regs = {
			.smmu = {
				.reg = 0x234,
				.bit = 31,
			},
			.la = {
				.reg = 0x3e4,
				.shift = 16,
				.mask = 0xff,
				.def = 0x80,
			},
		},
	}, {
		.id = TEGRA210_MC_SESRD,
		.name = "sesrd",
		.swgroup = TEGRA_SWGROUP_SE,
		.regs = {
			.smmu = {
				.reg = 0xb98,
				.bit = 0,
			},
			.la = {
				.reg = 0x3e0,
				.shift = 0,
				.mask = 0xff,
				.def = 0x2e,
			},
		},
	}, {
		.id = TEGRA210_MC_SESWR,
		.name = "seswr",
		.swgroup = TEGRA_SWGROUP_SE,
		.regs = {
			.smmu = {
				.reg = 0xb98,
				.bit = 1,
			},
			.la = {
				.reg = 0x3e0,
				.shift = 16,
				.mask = 0xff,
				.def = 0x80,
			},
		},
	}, {
		.id = TEGRA210_MC_AXIAPR,
		.name = "axiapr",
		.swgroup = TEGRA_SWGROUP_AXIAP,
		.regs = {
			.smmu = {
				.reg = 0xb98,
				.bit = 2,
			},
			.la = {
				.reg = 0x3a0,
				.shift = 0,
				.mask = 0xff,
				.def = 0xff,
			},
		},
	}, {
		.id = TEGRA210_MC_AXIAPW,
		.name = "axiapw",
		.swgroup = TEGRA_SWGROUP_AXIAP,
		.regs = {
			.smmu = {
				.reg = 0xb98,
				.bit = 3,
			},
			.la = {
				.reg = 0x3a0,
				.shift = 16,
				.mask = 0xff,
				.def = 0x80,
			},
		},
	}, {
		.id = TEGRA210_MC_ETRR,
		.name = "etrr",
		.swgroup = TEGRA_SWGROUP_ETR,
		.regs = {
			.smmu = {
				.reg = 0xb98,
				.bit = 4,
			},
			.la = {
				.reg = 0x3ec,
				.shift = 0,
				.mask = 0xff,
				.def = 0xff,
			},
		},
	}, {
		.id = TEGRA210_MC_ETRW,
		.name = "etrw",
		.swgroup = TEGRA_SWGROUP_ETR,
		.regs = {
			.smmu = {
				.reg = 0xb98,
				.bit = 5,
			},
			.la = {
				.reg = 0x3ec,
				.shift = 16,
				.mask = 0xff,
				.def = 0x80,
			},
		},
	}, {
		.id = TEGRA210_MC_TSECSRDB,
		.name = "tsecsrdb",
		.swgroup = TEGRA_SWGROUP_TSECB,
		.regs = {
			.smmu = {
				.reg = 0xb98,
				.bit = 6,
			},
			.la = {
				.reg = 0x3f0,
				.shift = 0,
				.mask = 0xff,
				.def = 0x9b,
			},
		},
	}, {
		.id = TEGRA210_MC_TSECSWRB,
		.name = "tsecswrb",
		.swgroup = TEGRA_SWGROUP_TSECB,
		.regs = {
			.smmu = {
				.reg = 0xb98,
				.bit = 7,
			},
			.la = {
				.reg = 0x3f0,
				.shift = 16,
				.mask = 0xff,
				.def = 0x80,
			},
		},
	}, {
		.id = TEGRA210_MC_GPUSRD2,
		.name = "gpusrd2",
		.swgroup = TEGRA_SWGROUP_GPU,
		.regs = {
			.smmu = {
				/* read-only */
				.reg = 0xb98,
				.bit = 8,
			},
			.la = {
				.reg = 0x3e8,
				.shift = 0,
				.mask = 0xff,
				.def = 0x1a,
			},
		},
	}, {
		.id = TEGRA210_MC_GPUSWR2,
		.name = "gpuswr2",
		.swgroup = TEGRA_SWGROUP_GPU,
		.regs = {
			.smmu = {
				/* read-only */
				.reg = 0xb98,
				.bit = 9,
			},
			.la = {
				.reg = 0x3e8,
				.shift = 16,
				.mask = 0xff,
				.def = 0x80,
			},
		},
	},
};

static const struct tegra_smmu_swgroup tegra210_swgroups[] = {
	{ .name = "afi",       .swgroup = TEGRA_SWGROUP_AFI,       .reg = 0x238 },
	{ .name = "avpc",      .swgroup = TEGRA_SWGROUP_AVPC,      .reg = 0x23c },
	{ .name = "dc",        .swgroup = TEGRA_SWGROUP_DC,        .reg = 0x240 },
	{ .name = "dcb",       .swgroup = TEGRA_SWGROUP_DCB,       .reg = 0x244 },
	{ .name = "hc",        .swgroup = TEGRA_SWGROUP_HC,        .reg = 0x250 },
	{ .name = "hda",       .swgroup = TEGRA_SWGROUP_HDA,       .reg = 0x254 },
	{ .name = "isp2",      .swgroup = TEGRA_SWGROUP_ISP2,      .reg = 0x258 },
	{ .name = "nvenc",     .swgroup = TEGRA_SWGROUP_NVENC,     .reg = 0x264 },
	{ .name = "nv",        .swgroup = TEGRA_SWGROUP_NV,        .reg = 0x268 },
	{ .name = "nv2",       .swgroup = TEGRA_SWGROUP_NV2,       .reg = 0x26c },
	{ .name = "ppcs",      .swgroup = TEGRA_SWGROUP_PPCS,      .reg = 0x270 },
	{ .name = "sata",      .swgroup = TEGRA_SWGROUP_SATA,      .reg = 0x274 },
	{ .name = "vi",        .swgroup = TEGRA_SWGROUP_VI,        .reg = 0x280 },
	{ .name = "vic",       .swgroup = TEGRA_SWGROUP_VIC,       .reg = 0x284 },
	{ .name = "xusb_host", .swgroup = TEGRA_SWGROUP_XUSB_HOST, .reg = 0x288 },
	{ .name = "xusb_dev",  .swgroup = TEGRA_SWGROUP_XUSB_DEV,  .reg = 0x28c },
	{ .name = "a9avp",     .swgroup = TEGRA_SWGROUP_A9AVP,     .reg = 0x290 },
	{ .name = "tsec",      .swgroup = TEGRA_SWGROUP_TSEC,      .reg = 0x294 },
	{ .name = "ppcs1",     .swgroup = TEGRA_SWGROUP_PPCS1,     .reg = 0x298 },
	{ .name = "dc1",       .swgroup = TEGRA_SWGROUP_DC1,       .reg = 0xa88 },
	{ .name = "sdmmc1a",   .swgroup = TEGRA_SWGROUP_SDMMC1A,   .reg = 0xa94 },
	{ .name = "sdmmc2a",   .swgroup = TEGRA_SWGROUP_SDMMC2A,   .reg = 0xa98 },
	{ .name = "sdmmc3a",   .swgroup = TEGRA_SWGROUP_SDMMC3A,   .reg = 0xa9c },
	{ .name = "sdmmc4a",   .swgroup = TEGRA_SWGROUP_SDMMC4A,   .reg = 0xaa0 },
	{ .name = "isp2b",     .swgroup = TEGRA_SWGROUP_ISP2B,     .reg = 0xaa4 },
	{ .name = "gpu",       .swgroup = TEGRA_SWGROUP_GPU,       .reg = 0xaac },
	{ .name = "ppcs2",     .swgroup = TEGRA_SWGROUP_PPCS2,     .reg = 0xab0 },
	{ .name = "nvdec",     .swgroup = TEGRA_SWGROUP_NVDEC,     .reg = 0xab4 },
	{ .name = "ape",       .swgroup = TEGRA_SWGROUP_APE,       .reg = 0xab8 },
	{ .name = "se",        .swgroup = TEGRA_SWGROUP_SE,        .reg = 0xabc },
	{ .name = "nvjpg",     .swgroup = TEGRA_SWGROUP_NVJPG,     .reg = 0xac0 },
	{ .name = "hc1",       .swgroup = TEGRA_SWGROUP_HC1,       .reg = 0xac4 },
	{ .name = "se1",       .swgroup = TEGRA_SWGROUP_SE1,       .reg = 0xac8 },
	{ .name = "axiap",     .swgroup = TEGRA_SWGROUP_AXIAP,     .reg = 0xacc },
	{ .name = "etr",       .swgroup = TEGRA_SWGROUP_ETR,       .reg = 0xad0 },
	{ .name = "tsecb",     .swgroup = TEGRA_SWGROUP_TSECB,     .reg = 0xad4 },
	{ .name = "tsec1",     .swgroup = TEGRA_SWGROUP_TSEC1,     .reg = 0xad8 },
	{ .name = "tsecb1",    .swgroup = TEGRA_SWGROUP_TSECB1,    .reg = 0xadc },
	{ .name = "nvdec1",    .swgroup = TEGRA_SWGROUP_NVDEC1,    .reg = 0xae0 },
};

static const unsigned int tegra210_group_display[] = {
	TEGRA_SWGROUP_DC,
	TEGRA_SWGROUP_DCB,
};

static const struct tegra_smmu_group_soc tegra210_groups[] = {
	{
		.name = "display",
		.swgroups = tegra210_group_display,
		.num_swgroups = ARRAY_SIZE(tegra210_group_display),
	},
};

static const struct tegra_smmu_soc tegra210_smmu_soc = {
	.clients = tegra210_mc_clients,
	.num_clients = ARRAY_SIZE(tegra210_mc_clients),
	.swgroups = tegra210_swgroups,
	.num_swgroups = ARRAY_SIZE(tegra210_swgroups),
	.groups = tegra210_groups,
	.num_groups = ARRAY_SIZE(tegra210_groups),
	.supports_round_robin_arbitration = true,
	.supports_request_limit = true,
	.num_tlb_lines = 48,
	.num_asids = 128,
};

#define TEGRA210_MC_RESET(_name, _control, _status, _bit)	\
	{							\
		.name = #_name,					\
		.id = TEGRA210_MC_RESET_##_name,		\
		.control = _control,				\
		.status = _status,				\
		.bit = _bit,					\
	}

static const struct tegra_mc_reset tegra210_mc_resets[] = {
	TEGRA210_MC_RESET(AFI,       0x200, 0x204,  0),
	TEGRA210_MC_RESET(AVPC,      0x200, 0x204,  1),
	TEGRA210_MC_RESET(DC,        0x200, 0x204,  2),
	TEGRA210_MC_RESET(DCB,       0x200, 0x204,  3),
	TEGRA210_MC_RESET(HC,        0x200, 0x204,  6),
	TEGRA210_MC_RESET(HDA,       0x200, 0x204,  7),
	TEGRA210_MC_RESET(ISP2,      0x200, 0x204,  8),
	TEGRA210_MC_RESET(MPCORE,    0x200, 0x204,  9),
	TEGRA210_MC_RESET(NVENC,     0x200, 0x204, 11),
	TEGRA210_MC_RESET(PPCS,      0x200, 0x204, 14),
	TEGRA210_MC_RESET(SATA,      0x200, 0x204, 15),
	TEGRA210_MC_RESET(VI,        0x200, 0x204, 17),
	TEGRA210_MC_RESET(VIC,       0x200, 0x204, 18),
	TEGRA210_MC_RESET(XUSB_HOST, 0x200, 0x204, 19),
	TEGRA210_MC_RESET(XUSB_DEV,  0x200, 0x204, 20),
	TEGRA210_MC_RESET(A9AVP,     0x200, 0x204, 21),
	TEGRA210_MC_RESET(TSEC,      0x200, 0x204, 22),
	TEGRA210_MC_RESET(SDMMC1,    0x200, 0x204, 29),
	TEGRA210_MC_RESET(SDMMC2,    0x200, 0x204, 30),
	TEGRA210_MC_RESET(SDMMC3,    0x200, 0x204, 31),
	TEGRA210_MC_RESET(SDMMC4,    0x970, 0x974,  0),
	TEGRA210_MC_RESET(ISP2B,     0x970, 0x974,  1),
	TEGRA210_MC_RESET(GPU,       0x970, 0x974,  2),
	TEGRA210_MC_RESET(NVDEC,     0x970, 0x974,  5),
	TEGRA210_MC_RESET(APE,       0x970, 0x974,  6),
	TEGRA210_MC_RESET(SE,        0x970, 0x974,  7),
	TEGRA210_MC_RESET(NVJPG,     0x970, 0x974,  8),
	TEGRA210_MC_RESET(AXIAP,     0x970, 0x974, 11),
	TEGRA210_MC_RESET(ETR,       0x970, 0x974, 12),
	TEGRA210_MC_RESET(TSECB,     0x970, 0x974, 13),
};

const struct tegra_mc_soc tegra210_mc_soc = {
	.clients = tegra210_mc_clients,
	.num_clients = ARRAY_SIZE(tegra210_mc_clients),
	.num_address_bits = 34,
	.atom_size = 64,
	.client_id_mask = 0xff,
	.smmu = &tegra210_smmu_soc,
	.intmask = MC_INT_DECERR_MTS | MC_INT_SECERR_SEC | MC_INT_DECERR_VPR |
		   MC_INT_INVALID_APB_ASID_UPDATE | MC_INT_INVALID_SMMU_PAGE |
		   MC_INT_SECURITY_VIOLATION | MC_INT_DECERR_EMEM,
	.reset_ops = &tegra_mc_reset_ops_common,
	.resets = tegra210_mc_resets,
	.num_resets = ARRAY_SIZE(tegra210_mc_resets),
	.ops = &tegra30_mc_ops,
};
