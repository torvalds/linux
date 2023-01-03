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
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0RDA,
		.name = "dla0rda",
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x5f0,
				.security = 0x5f4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0FALRDB,
		.name = "dla0falrdb",
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x5f8,
				.security = 0x5fc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0WRA,
		.name = "dla0wra",
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x600,
				.security = 0x604,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0RDB,
		.name = "dla0rdb",
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x160,
				.security = 0x164,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0RDA1,
		.name = "dla0rda1",
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x748,
				.security = 0x74c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0FALWRB,
		.name = "dla0falwrb",
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x608,
				.security = 0x60c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0RDB1,
		.name = "dla0rdb1",
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x168,
				.security = 0x16c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0WRB,
		.name = "dla0wrb",
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x170,
				.security = 0x174,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1RDA,
		.name = "dla0rda",
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x610,
				.security = 0x614,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1FALRDB,
		.name = "dla0falrdb",
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x618,
				.security = 0x61c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1WRA,
		.name = "dla0wra",
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x620,
				.security = 0x624,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1RDB,
		.name = "dla0rdb",
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x178,
				.security = 0x17c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1RDA1,
		.name = "dla0rda1",
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x750,
				.security = 0x754,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1FALWRB,
		.name = "dla0falwrb",
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x628,
				.security = 0x62c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1RDB1,
		.name = "dla0rdb1",
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x370,
				.security = 0x374,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1WRB,
		.name = "dla0wrb",
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x378,
				.security = 0x37c,
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
	/*
	 * Additionally, there are lite carveouts but those are not currently
	 * supported.
	 */
	.num_carveouts = 32,
};
