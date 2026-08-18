// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026, NVIDIA CORPORATION.  All rights reserved.
 */

#include <soc/tegra/mc.h>

#include <dt-bindings/memory/tegra234-mc.h>
#include <dt-bindings/memory/nvidia,tegra238-mc.h>
#include <linux/interconnect.h>
#include <linux/tegra-icc.h>

#include <soc/tegra/bpmp.h>
#include "mc.h"

static const struct tegra_mc_client tegra238_mc_clients[] = {
	{
		.id = TEGRA234_MEMORY_CLIENT_HDAR,
		.name = "hdar",
		.bpmp_id = TEGRA_ICC_BPMP_HDA,
		.type = TEGRA_ICC_ISO_AUDIO,
		.sid = TEGRA238_SID_HDA,
		.regs = {
			.sid = {
				.override = 0xa8,
				.security = 0xac,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_HDAW,
		.name = "hdaw",
		.bpmp_id = TEGRA_ICC_BPMP_HDA,
		.type = TEGRA_ICC_ISO_AUDIO,
		.sid = TEGRA238_SID_HDA,
		.regs = {
			.sid = {
				.override = 0x1a8,
				.security = 0x1ac,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_SDMMCRAB,
		.name = "sdmmcrab",
		.bpmp_id = TEGRA_ICC_BPMP_SDMMC_4,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA238_SID_SDMMC4A,
		.regs = {
			.sid = {
				.override = 0x318,
				.security = 0x31c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_SDMMCWAB,
		.name = "sdmmcwab",
		.bpmp_id = TEGRA_ICC_BPMP_SDMMC_4,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA238_SID_SDMMC4A,
		.regs = {
			.sid = {
				.override = 0x338,
				.security = 0x33c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_APER,
		.name = "aper",
		.bpmp_id = TEGRA_ICC_BPMP_APE,
		.type = TEGRA_ICC_ISO_AUDIO,
		.sid = TEGRA238_SID_ISO_APE0,
		.regs = {
			.sid = {
				.override = 0x3d0,
				.security = 0x3d4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_APEW,
		.name = "apew",
		.bpmp_id = TEGRA_ICC_BPMP_APE,
		.type = TEGRA_ICC_ISO_AUDIO,
		.sid = TEGRA238_SID_ISO_APE0,
		.regs = {
			.sid = {
				.override = 0x3d8,
				.security = 0x3dc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_NVDISPLAYR,
		.name = "nvdisplayr",
		.bpmp_id = TEGRA_ICC_BPMP_DISPLAY,
		.type = TEGRA_ICC_ISO_DISPLAY,
		.sid = TEGRA238_SID_ISO_NVDISPLAY,
		.regs = {
			.sid = {
				.override = 0x490,
				.security = 0x494,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_NVDISPLAYR1,
		.name = "nvdisplayr1",
		.bpmp_id = TEGRA_ICC_BPMP_DISPLAY,
		.type = TEGRA_ICC_ISO_DISPLAY,
		.sid = TEGRA238_SID_ISO_NVDISPLAY,
		.regs = {
			.sid = {
				.override = 0x508,
				.security = 0x50c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_BPMPR,
		.name = "bpmpr",
		.sid = TEGRA238_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x498,
				.security = 0x49c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_BPMPW,
		.name = "bpmpw",
		.sid = TEGRA238_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x4a0,
				.security = 0x4a4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_BPMPDMAR,
		.name = "bpmpdmar",
		.sid = TEGRA238_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x4a8,
				.security = 0x4ac,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_BPMPDMAW,
		.name = "bpmpdmaw",
		.sid = TEGRA238_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x4b0,
				.security = 0x4b4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_APEDMAR,
		.name = "apedmar",
		.bpmp_id = TEGRA_ICC_BPMP_APEDMA,
		.type = TEGRA_ICC_ISO_AUDIO,
		.sid = TEGRA238_SID_ISO_APE1,
		.regs = {
			.sid = {
				.override = 0x4f8,
				.security = 0x4fc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_APEDMAW,
		.name = "apedmaw",
		.bpmp_id = TEGRA_ICC_BPMP_APEDMA,
		.type = TEGRA_ICC_ISO_AUDIO,
		.sid = TEGRA238_SID_ISO_APE1,
		.regs = {
			.sid = {
				.override = 0x500,
				.security = 0x504,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_VICSRD,
		.name = "vicsrd",
		.bpmp_id = TEGRA_ICC_BPMP_VIC,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA238_SID_VIC,
		.regs = {
			.sid = {
				.override = 0x360,
				.security = 0x364,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_VICSWR,
		.name = "vicswr",
		.bpmp_id = TEGRA_ICC_BPMP_VIC,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA238_SID_VIC,
		.regs = {
			.sid = {
				.override = 0x368,
				.security = 0x36c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_NVDECSRD,
		.name = "nvdecsrd",
		.bpmp_id = TEGRA_ICC_BPMP_NVDEC,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA238_SID_NVDEC,
		.regs = {
			.sid = {
				.override = 0x3c0,
				.security = 0x3c4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_NVDECSWR,
		.name = "nvdecswr",
		.bpmp_id = TEGRA_ICC_BPMP_NVDEC,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA238_SID_NVDEC,
		.regs = {
			.sid = {
				.override = 0x3c8,
				.security = 0x3cc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_NVENCSRD,
		.name = "nvencsrd",
		.bpmp_id = TEGRA_ICC_BPMP_NVENC,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA238_SID_NVENC,
		.regs = {
			.sid = {
				.override = 0xe0,
				.security = 0xe4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_NVENCSWR,
		.name = "nvencswr",
		.bpmp_id = TEGRA_ICC_BPMP_NVENC,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA238_SID_NVENC,
		.regs = {
			.sid = {
				.override = 0x158,
				.security = 0x15c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE0R,
		.name = "pcie0r",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_0,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA238_SID_PCIE0,
		.regs = {
			.sid = {
				.override = 0x6c0,
				.security = 0x6c4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE0W,
		.name = "pcie0w",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_0,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA238_SID_PCIE0,
		.regs = {
			.sid = {
				.override = 0x6c8,
				.security = 0x6cc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE1R,
		.name = "pcie1r",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_1,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA238_SID_PCIE1,
		.regs = {
			.sid = {
				.override = 0x6d0,
				.security = 0x6d4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE1W,
		.name = "pcie1w",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_1,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA238_SID_PCIE1,
		.regs = {
			.sid = {
				.override = 0x6d8,
				.security = 0x6dc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE2AR,
		.name = "pcie2ar",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_2,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA238_SID_PCIE2,
		.regs = {
			.sid = {
				.override = 0x6e0,
				.security = 0x6e4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE2AW,
		.name = "pcie2aw",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_2,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA238_SID_PCIE2,
		.regs = {
			.sid = {
				.override = 0x6e8,
				.security = 0x6ec,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE3R,
		.name = "pcie3r",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_3,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA238_SID_PCIE3,
		.regs = {
			.sid = {
				.override = 0x6f0,
				.security = 0x6f4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE3W,
		.name = "pcie3w",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_3,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA238_SID_PCIE3,
		.regs = {
			.sid = {
				.override = 0x6f8,
				.security = 0x6fc,
			},
		},
	}, {
		.id = TEGRA_ICC_MC_CPU_CLUSTER0,
		.name = "sw_cluster0",
		.bpmp_id = TEGRA_ICC_BPMP_CPU_CLUSTER0,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA234_MEMORY_CLIENT_NVL1R,
		.name = "nvl1r",
		.bpmp_id = TEGRA_ICC_BPMP_GPU,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA234_MEMORY_CLIENT_NVL1W,
		.name = "nvl1w",
		.bpmp_id = TEGRA_ICC_BPMP_GPU,
		.type = TEGRA_ICC_NISO,
	}
};

static const struct tegra_mc_intmask tegra238_mc_intmasks[] = {
	{
		.reg = MC_INTMASK,
		.mask = MC_INT_DECERR_ROUTE_SANITY | MC_INT_DECERR_GENERALIZED_CARVEOUT |
			MC_INT_DECERR_MTS | MC_INT_SECERR_SEC | MC_INT_DECERR_VPR |
			MC_INT_SECURITY_VIOLATION | MC_INT_DECERR_EMEM,
	},
};

const struct tegra_mc_soc tegra238_mc_soc = {
	.num_clients = ARRAY_SIZE(tegra238_mc_clients),
	.clients = tegra238_mc_clients,
	.num_address_bits = 40,
	.num_channels = 8,
	.client_id_mask = 0x1ff,
	.intmasks = tegra238_mc_intmasks,
	.num_intmasks = ARRAY_SIZE(tegra238_mc_intmasks),
	.has_addr_hi_reg = true,
	.ops = &tegra186_mc_ops,
	.icc_ops = &tegra234_mc_icc_ops,
	.ch_intmask = 0x0000ff00,
	.global_intstatus_channel_shift = 8,
	.num_carveouts = 32,
	.regs = &tegra20_mc_regs,
	.handle_irq = tegra30_mc_irq_handlers,
	.num_interrupts = ARRAY_SIZE(tegra30_mc_irq_handlers),
	.mc_addr_hi_mask = 0x3,
	.mc_err_status_type_mask = (0x7 << 28),
};
