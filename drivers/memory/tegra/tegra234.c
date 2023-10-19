// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2022, NVIDIA CORPORATION.  All rights reserved.
 */

#include <soc/tegra/mc.h>

#include <dt-bindings/memory/tegra234-mc.h>

#include "mc.h"

static const struct tegra_mc_client tegra234_mc_clients[] = {
	{
		.id = TEGRA234_MEMORY_CLIENT_MGBEARD,
		.name = "mgbeard",
		.sid = TEGRA234_SID_MGBE,
		.regs = {
			.sid = {
				.override = 0x2c0,
				.security = 0x2c4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_MGBEBRD,
		.name = "mgbebrd",
		.sid = TEGRA234_SID_MGBE_VF1,
		.regs = {
			.sid = {
				.override = 0x2c8,
				.security = 0x2cc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_MGBECRD,
		.name = "mgbecrd",
		.sid = TEGRA234_SID_MGBE_VF2,
		.regs = {
			.sid = {
				.override = 0x2d0,
				.security = 0x2d4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_MGBEDRD,
		.name = "mgbedrd",
		.sid = TEGRA234_SID_MGBE_VF3,
		.regs = {
			.sid = {
				.override = 0x2d8,
				.security = 0x2dc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_MGBEAWR,
		.name = "mgbeawr",
		.sid = TEGRA234_SID_MGBE,
		.regs = {
			.sid = {
				.override = 0x2e0,
				.security = 0x2e4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_MGBEBWR,
		.name = "mgbebwr",
		.sid = TEGRA234_SID_MGBE_VF1,
		.regs = {
			.sid = {
				.override = 0x2f8,
				.security = 0x2fc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_MGBECWR,
		.name = "mgbecwr",
		.sid = TEGRA234_SID_MGBE_VF2,
		.regs = {
			.sid = {
				.override = 0x308,
				.security = 0x30c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_SDMMCRAB,
		.name = "sdmmcrab",
		.sid = TEGRA234_SID_SDMMC4,
		.regs = {
			.sid = {
				.override = 0x318,
				.security = 0x31c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_MGBEDWR,
		.name = "mgbedwr",
		.sid = TEGRA234_SID_MGBE_VF3,
		.regs = {
			.sid = {
				.override = 0x328,
				.security = 0x32c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_SDMMCWAB,
		.name = "sdmmcwab",
		.sid = TEGRA234_SID_SDMMC4,
		.regs = {
			.sid = {
				.override = 0x338,
				.security = 0x33c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_BPMPR,
		.name = "bpmpr",
		.sid = TEGRA234_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x498,
				.security = 0x49c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_BPMPW,
		.name = "bpmpw",
		.sid = TEGRA234_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x4a0,
				.security = 0x4a4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_BPMPDMAR,
		.name = "bpmpdmar",
		.sid = TEGRA234_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x4a8,
				.security = 0x4ac,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_BPMPDMAW,
		.name = "bpmpdmaw",
		.sid = TEGRA234_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x4b0,
				.security = 0x4b4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_APEDMAR,
		.name = "apedmar",
		.sid = TEGRA234_SID_APE,
		.regs = {
			.sid = {
				.override = 0x4f8,
				.security = 0x4fc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_APEDMAW,
		.name = "apedmaw",
		.sid = TEGRA234_SID_APE,
		.regs = {
			.sid = {
				.override = 0x500,
				.security = 0x504,
			},
		},
	},
};

const struct tegra_mc_soc tegra234_mc_soc = {
	.num_clients = ARRAY_SIZE(tegra234_mc_clients),
	.clients = tegra234_mc_clients,
	.num_address_bits = 40,
	.num_channels = 16,
	.client_id_mask = 0x1ff,
	.intmask = MC_INT_DECERR_ROUTE_SANITY |
		   MC_INT_DECERR_GENERALIZED_CARVEOUT | MC_INT_DECERR_MTS |
		   MC_INT_SECERR_SEC | MC_INT_DECERR_VPR |
		   MC_INT_SECURITY_VIOLATION | MC_INT_DECERR_EMEM,
	.has_addr_hi_reg = true,
	.ops = &tegra186_mc_ops,
	.ch_intmask = 0x0000ff00,
	.global_intstatus_channel_shift = 8,
};
