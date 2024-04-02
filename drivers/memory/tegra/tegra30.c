// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/device.h>
#include <linux/of.h>
#include <linux/slab.h>

#include <dt-bindings/memory/tegra30-mc.h>

#include "mc.h"

static const unsigned long tegra30_mc_emem_regs[] = {
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
	MC_EMEM_ARB_RING1_THROTTLE,
};

static const struct tegra_mc_client tegra30_mc_clients[] = {
	{
		.id = 0x00,
		.name = "ptcr",
		.swgroup = TEGRA_SWGROUP_PTC,
		.regs = {
			.la = {
				.reg = 0x34c,
				.shift = 0,
				.mask = 0xff,
				.def = 0x0,
			},
		},
		.fifo_size = 16 * 2,
	}, {
		.id = 0x01,
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
				.def = 0x4e,
			},
		},
		.fifo_size = 16 * 128,
	}, {
		.id = 0x02,
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
				.def = 0x4e,
			},
		},
		.fifo_size = 16 * 128,
	}, {
		.id = 0x03,
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
				.def = 0x4e,
			},
		},
		.fifo_size = 16 * 64,
	}, {
		.id = 0x04,
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
				.def = 0x4e,
			},
		},
		.fifo_size = 16 * 64,
	}, {
		.id = 0x05,
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
				.def = 0x4e,
			},
		},
		.fifo_size = 16 * 128,
	}, {
		.id = 0x06,
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
				.def = 0x4e,
			},
		},
		.fifo_size = 16 * 128,
	}, {
		.id = 0x07,
		.name = "display1b",
		.swgroup = TEGRA_SWGROUP_DC,
		.regs = {
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
		},
		.fifo_size = 16 * 64,
	}, {
		.id = 0x08,
		.name = "display1bb",
		.swgroup = TEGRA_SWGROUP_DCB,
		.regs = {
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
		},
		.fifo_size = 16 * 64,
	}, {
		.id = 0x09,
		.name = "eppup",
		.swgroup = TEGRA_SWGROUP_EPP,
		.regs = {
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
		},
		.fifo_size = 16 * 8,
	}, {
		.id = 0x0a,
		.name = "g2pr",
		.swgroup = TEGRA_SWGROUP_G2,
		.regs = {
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
		},
		.fifo_size = 16 * 64,
	}, {
		.id = 0x0b,
		.name = "g2sr",
		.swgroup = TEGRA_SWGROUP_G2,
		.regs = {
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
		},
		.fifo_size = 16 * 64,
	}, {
		.id = 0x0c,
		.name = "mpeunifbr",
		.swgroup = TEGRA_SWGROUP_MPE,
		.regs = {
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
		},
		.fifo_size = 16 * 8,
	}, {
		.id = 0x0d,
		.name = "viruv",
		.swgroup = TEGRA_SWGROUP_VI,
		.regs = {
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
		},
		.fifo_size = 16 * 8,
	}, {
		.id = 0x0e,
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
				.def = 0x10,
			},
		},
		.fifo_size = 16 * 32,
	}, {
		.id = 0x0f,
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
		.fifo_size = 16 * 2,
	}, {
		.id = 0x10,
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
				.def = 0xff,
			},
		},
		.fifo_size = 16 * 2,
	}, {
		.id = 0x11,
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
				.def = 0xff,
			},
		},
		.fifo_size = 16 * 2,
	}, {
		.id = 0x12,
		.name = "fdcdrd",
		.swgroup = TEGRA_SWGROUP_NV,
		.regs = {
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
		},
		.fifo_size = 16 * 48,
	}, {
		.id = 0x13,
		.name = "fdcdrd2",
		.swgroup = TEGRA_SWGROUP_NV2,
		.regs = {
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
		},
		.fifo_size = 16 * 48,
	}, {
		.id = 0x14,
		.name = "g2dr",
		.swgroup = TEGRA_SWGROUP_G2,
		.regs = {
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
		},
		.fifo_size = 16 * 48,
	}, {
		.id = 0x15,
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
				.def = 0xff,
			},
		},
		.fifo_size = 16 * 16,
	}, {
		.id = 0x16,
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
				.def = 0x05,
			},
		},
		.fifo_size = 16 * 16,
	}, {
		.id = 0x17,
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
		.fifo_size = 16 * 8,
	}, {
		.id = 0x18,
		.name = "idxsrd",
		.swgroup = TEGRA_SWGROUP_NV,
		.regs = {
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
		},
		.fifo_size = 16 * 64,
	}, {
		.id = 0x19,
		.name = "idxsrd2",
		.swgroup = TEGRA_SWGROUP_NV2,
		.regs = {
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
		},
		.fifo_size = 16 * 64,
	}, {
		.id = 0x1a,
		.name = "mpe_ipred",
		.swgroup = TEGRA_SWGROUP_MPE,
		.regs = {
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
		},
		.fifo_size = 16 * 2,
	}, {
		.id = 0x1b,
		.name = "mpeamemrd",
		.swgroup = TEGRA_SWGROUP_MPE,
		.regs = {
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
		},
		.fifo_size = 16 * 64,
	}, {
		.id = 0x1c,
		.name = "mpecsrd",
		.swgroup = TEGRA_SWGROUP_MPE,
		.regs = {
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
		},
		.fifo_size = 16 * 8,
	}, {
		.id = 0x1d,
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
				.def = 0x10,
			},
		},
		.fifo_size = 16 * 2,
	}, {
		.id = 0x1e,
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
				.def = 0x12,
			},
		},
		.fifo_size = 16 * 8,
	}, {
		.id = 0x1f,
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
				.def = 0x33,
			},
		},
		.fifo_size = 16 * 32,
	}, {
		.id = 0x20,
		.name = "texsrd",
		.swgroup = TEGRA_SWGROUP_NV,
		.regs = {
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
		},
		.fifo_size = 16 * 64,
	}, {
		.id = 0x21,
		.name = "texsrd2",
		.swgroup = TEGRA_SWGROUP_NV2,
		.regs = {
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
		},
		.fifo_size = 16 * 64,
	}, {
		.id = 0x22,
		.name = "vdebsevr",
		.swgroup = TEGRA_SWGROUP_VDE,
		.regs = {
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
		},
		.fifo_size = 16 * 8,
	}, {
		.id = 0x23,
		.name = "vdember",
		.swgroup = TEGRA_SWGROUP_VDE,
		.regs = {
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
		},
		.fifo_size = 16 * 4,
	}, {
		.id = 0x24,
		.name = "vdemcer",
		.swgroup = TEGRA_SWGROUP_VDE,
		.regs = {
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
		},
		.fifo_size = 16 * 16,
	}, {
		.id = 0x25,
		.name = "vdetper",
		.swgroup = TEGRA_SWGROUP_VDE,
		.regs = {
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
		},
		.fifo_size = 16 * 16,
	}, {
		.id = 0x26,
		.name = "mpcorelpr",
		.swgroup = TEGRA_SWGROUP_MPCORELP,
		.regs = {
			.la = {
				.reg = 0x324,
				.shift = 0,
				.mask = 0xff,
				.def = 0x04,
			},
		},
		.fifo_size = 16 * 14,
	}, {
		.id = 0x27,
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
		.fifo_size = 16 * 14,
	}, {
		.id = 0x28,
		.name = "eppu",
		.swgroup = TEGRA_SWGROUP_EPP,
		.regs = {
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
		},
		.fifo_size = 16 * 64,
	}, {
		.id = 0x29,
		.name = "eppv",
		.swgroup = TEGRA_SWGROUP_EPP,
		.regs = {
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
		},
		.fifo_size = 16 * 64,
	}, {
		.id = 0x2a,
		.name = "eppy",
		.swgroup = TEGRA_SWGROUP_EPP,
		.regs = {
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
		},
		.fifo_size = 16 * 64,
	}, {
		.id = 0x2b,
		.name = "mpeunifbw",
		.swgroup = TEGRA_SWGROUP_MPE,
		.regs = {
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
		},
		.fifo_size = 16 * 8,
	}, {
		.id = 0x2c,
		.name = "viwsb",
		.swgroup = TEGRA_SWGROUP_VI,
		.regs = {
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
		},
		.fifo_size = 16 * 64,
	}, {
		.id = 0x2d,
		.name = "viwu",
		.swgroup = TEGRA_SWGROUP_VI,
		.regs = {
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
		},
		.fifo_size = 16 * 64,
	}, {
		.id = 0x2e,
		.name = "viwv",
		.swgroup = TEGRA_SWGROUP_VI,
		.regs = {
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
		},
		.fifo_size = 16 * 64,
	}, {
		.id = 0x2f,
		.name = "viwy",
		.swgroup = TEGRA_SWGROUP_VI,
		.regs = {
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
		},
		.fifo_size = 16 * 64,
	}, {
		.id = 0x30,
		.name = "g2dw",
		.swgroup = TEGRA_SWGROUP_G2,
		.regs = {
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
		},
		.fifo_size = 16 * 128,
	}, {
		.id = 0x31,
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
				.def = 0x0c,
			},
		},
		.fifo_size = 16 * 32,
	}, {
		.id = 0x32,
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
				.def = 0x0e,
			},
		},
		.fifo_size = 16 * 2,
	}, {
		.id = 0x33,
		.name = "fdcdwr",
		.swgroup = TEGRA_SWGROUP_NV,
		.regs = {
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
		},
		.fifo_size = 16 * 48,
	}, {
		.id = 0x34,
		.name = "fdcdwr2",
		.swgroup = TEGRA_SWGROUP_NV2,
		.regs = {
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
		},
		.fifo_size = 16 * 48,
	}, {
		.id = 0x35,
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
				.def = 0xff,
			},
		},
		.fifo_size = 16 * 16,
	}, {
		.id = 0x36,
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
				.def = 0x10,
			},
		},
		.fifo_size = 16 * 32,
	}, {
		.id = 0x37,
		.name = "ispw",
		.swgroup = TEGRA_SWGROUP_ISP,
		.regs = {
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
		},
		.fifo_size = 16 * 64,
	}, {
		.id = 0x38,
		.name = "mpcorelpw",
		.swgroup = TEGRA_SWGROUP_MPCORELP,
		.regs = {
			.la = {
				.reg = 0x324,
				.shift = 16,
				.mask = 0xff,
				.def = 0x0e,
			},
		},
		.fifo_size = 16 * 24,
	}, {
		.id = 0x39,
		.name = "mpcorew",
		.swgroup = TEGRA_SWGROUP_MPCORE,
		.regs = {
			.la = {
				.reg = 0x320,
				.shift = 16,
				.mask = 0xff,
				.def = 0x0e,
			},
		},
		.fifo_size = 16 * 24,
	}, {
		.id = 0x3a,
		.name = "mpecswr",
		.swgroup = TEGRA_SWGROUP_MPE,
		.regs = {
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
		},
		.fifo_size = 16 * 8,
	}, {
		.id = 0x3b,
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
				.def = 0x10,
			},
		},
		.fifo_size = 16 * 2,
	}, {
		.id = 0x3c,
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
				.def = 0x06,
			},
		},
		.fifo_size = 16 * 4,
	}, {
		.id = 0x3d,
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
				.def = 0x33,
			},
		},
		.fifo_size = 16 * 32,
	}, {
		.id = 0x3e,
		.name = "vdebsevw",
		.swgroup = TEGRA_SWGROUP_VDE,
		.regs = {
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
		},
		.fifo_size = 16 * 4,
	}, {
		.id = 0x3f,
		.name = "vdedbgw",
		.swgroup = TEGRA_SWGROUP_VDE,
		.regs = {
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
		},
		.fifo_size = 16 * 16,
	}, {
		.id = 0x40,
		.name = "vdembew",
		.swgroup = TEGRA_SWGROUP_VDE,
		.regs = {
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
		},
		.fifo_size = 16 * 2,
	}, {
		.id = 0x41,
		.name = "vdetpmw",
		.swgroup = TEGRA_SWGROUP_VDE,
		.regs = {
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
		.fifo_size = 16 * 16,
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

static const unsigned int tegra30_group_drm[] = {
	TEGRA_SWGROUP_DC,
	TEGRA_SWGROUP_DCB,
	TEGRA_SWGROUP_G2,
	TEGRA_SWGROUP_NV,
	TEGRA_SWGROUP_NV2,
};

static const struct tegra_smmu_group_soc tegra30_groups[] = {
	{
		.name = "drm",
		.swgroups = tegra30_group_drm,
		.num_swgroups = ARRAY_SIZE(tegra30_group_drm),
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

static void tegra30_mc_tune_client_latency(struct tegra_mc *mc,
					   const struct tegra_mc_client *client,
					   unsigned int bandwidth_mbytes_sec)
{
	u32 arb_tolerance_compensation_nsec, arb_tolerance_compensation_div;
	unsigned int fifo_size = client->fifo_size;
	u32 arb_nsec, la_ticks, value;

	/* see 18.4.1 Client Configuration in Tegra3 TRM v03p */
	if (bandwidth_mbytes_sec)
		arb_nsec = fifo_size * NSEC_PER_USEC / bandwidth_mbytes_sec;
	else
		arb_nsec = U32_MAX;

	/*
	 * Latency allowness should be set with consideration for the module's
	 * latency tolerance and internal buffering capabilities.
	 *
	 * Display memory clients use isochronous transfers and have very low
	 * tolerance to a belated transfers. Hence we need to compensate the
	 * memory arbitration imperfection for them in order to prevent FIFO
	 * underflow condition when memory bus is busy.
	 *
	 * VI clients also need a stronger compensation.
	 */
	switch (client->swgroup) {
	case TEGRA_SWGROUP_MPCORE:
	case TEGRA_SWGROUP_PTC:
		/*
		 * We always want lower latency for these clients, hence
		 * don't touch them.
		 */
		return;

	case TEGRA_SWGROUP_DC:
	case TEGRA_SWGROUP_DCB:
		arb_tolerance_compensation_nsec = 1050;
		arb_tolerance_compensation_div = 2;
		break;

	case TEGRA_SWGROUP_VI:
		arb_tolerance_compensation_nsec = 1050;
		arb_tolerance_compensation_div = 1;
		break;

	default:
		arb_tolerance_compensation_nsec = 150;
		arb_tolerance_compensation_div = 1;
		break;
	}

	if (arb_nsec > arb_tolerance_compensation_nsec)
		arb_nsec -= arb_tolerance_compensation_nsec;
	else
		arb_nsec = 0;

	arb_nsec /= arb_tolerance_compensation_div;

	/*
	 * Latency allowance is a number of ticks a request from a particular
	 * client may wait in the EMEM arbiter before it becomes a high-priority
	 * request.
	 */
	la_ticks = arb_nsec / mc->tick;
	la_ticks = min(la_ticks, client->regs.la.mask);

	value = mc_readl(mc, client->regs.la.reg);
	value &= ~(client->regs.la.mask << client->regs.la.shift);
	value |= la_ticks << client->regs.la.shift;
	mc_writel(mc, value, client->regs.la.reg);
}

static int tegra30_mc_icc_set(struct icc_node *src, struct icc_node *dst)
{
	struct tegra_mc *mc = icc_provider_to_tegra_mc(src->provider);
	const struct tegra_mc_client *client = &mc->soc->clients[src->id];
	u64 peak_bandwidth = icc_units_to_bps(src->peak_bw);

	/*
	 * Skip pre-initialization that is done by icc_node_add(), which sets
	 * bandwidth to maximum for all clients before drivers are loaded.
	 *
	 * This doesn't make sense for us because we don't have drivers for all
	 * clients and it's okay to keep configuration left from bootloader
	 * during boot, at least for today.
	 */
	if (src == dst)
		return 0;

	/* convert bytes/sec to megabytes/sec */
	do_div(peak_bandwidth, 1000000);

	tegra30_mc_tune_client_latency(mc, client, peak_bandwidth);

	return 0;
}

static int tegra30_mc_icc_aggreate(struct icc_node *node, u32 tag, u32 avg_bw,
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
tegra30_mc_of_icc_xlate_extended(const struct of_phandle_args *spec, void *data)
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

static const struct tegra_mc_icc_ops tegra30_mc_icc_ops = {
	.xlate_extended = tegra30_mc_of_icc_xlate_extended,
	.aggregate = tegra30_mc_icc_aggreate,
	.set = tegra30_mc_icc_set,
};

const struct tegra_mc_soc tegra30_mc_soc = {
	.clients = tegra30_mc_clients,
	.num_clients = ARRAY_SIZE(tegra30_mc_clients),
	.num_address_bits = 32,
	.atom_size = 16,
	.client_id_mask = 0x7f,
	.smmu = &tegra30_smmu_soc,
	.emem_regs = tegra30_mc_emem_regs,
	.num_emem_regs = ARRAY_SIZE(tegra30_mc_emem_regs),
	.intmask = MC_INT_INVALID_SMMU_PAGE | MC_INT_SECURITY_VIOLATION |
		   MC_INT_DECERR_EMEM,
	.reset_ops = &tegra_mc_reset_ops_common,
	.resets = tegra30_mc_resets,
	.num_resets = ARRAY_SIZE(tegra30_mc_resets),
	.icc_ops = &tegra30_mc_icc_ops,
	.ops = &tegra30_mc_ops,
};
