// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2023, NVIDIA CORPORATION.  All rights reserved.
 */

#include <soc/tegra/mc.h>

#include <dt-bindings/memory/tegra234-mc.h>
#include <linux/interconnect.h>
#include <linux/tegra-icc.h>

#include <soc/tegra/bpmp.h>
#include "mc.h"

/*
 * MC Client entries are sorted in the increasing order of the
 * override and security register offsets.
 */
static const struct tegra_mc_client tegra234_mc_clients[] = {
	{
		.id = TEGRA234_MEMORY_CLIENT_HDAR,
		.name = "hdar",
		.bpmp_id = TEGRA_ICC_BPMP_HDA,
		.type = TEGRA_ICC_ISO_AUDIO,
		.sid = TEGRA234_SID_HDA,
		.regs = {
			.sid = {
				.override = 0xa8,
				.security = 0xac,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_NVENCSRD,
		.name = "nvencsrd",
		.bpmp_id = TEGRA_ICC_BPMP_NVENC,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_NVENC,
		.regs = {
			.sid = {
				.override = 0xe0,
				.security = 0xe4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE6AR,
		.name = "pcie6ar",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_6,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_PCIE6,
		.regs = {
			.sid = {
				.override = 0x140,
				.security = 0x144,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE6AW,
		.name = "pcie6aw",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_6,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_PCIE6,
		.regs = {
			.sid = {
				.override = 0x148,
				.security = 0x14c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE7AR,
		.name = "pcie7ar",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_7,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_PCIE7,
		.regs = {
			.sid = {
				.override = 0x150,
				.security = 0x154,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_NVENCSWR,
		.name = "nvencswr",
		.bpmp_id = TEGRA_ICC_BPMP_NVENC,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_NVENC,
		.regs = {
			.sid = {
				.override = 0x158,
				.security = 0x15c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0RDB,
		.name = "dla0rdb",
		.bpmp_id = TEGRA_ICC_BPMP_DLA_0,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x160,
				.security = 0x164,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0RDB1,
		.name = "dla0rdb1",
		.bpmp_id = TEGRA_ICC_BPMP_DLA_0,
		.type = TEGRA_ICC_NISO,
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
		.bpmp_id = TEGRA_ICC_BPMP_DLA_0,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x170,
				.security = 0x174,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1RDB,
		.name = "dla1rdb",
		.bpmp_id = TEGRA_ICC_BPMP_DLA_1,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x178,
				.security = 0x17c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE7AW,
		.name = "pcie7aw",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_7,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_PCIE7,
		.regs = {
			.sid = {
				.override = 0x180,
				.security = 0x184,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE8AR,
		.name = "pcie8ar",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_8,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_PCIE8,
		.regs = {
			.sid = {
				.override = 0x190,
				.security = 0x194,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_HDAW,
		.name = "hdaw",
		.bpmp_id = TEGRA_ICC_BPMP_HDA,
		.type = TEGRA_ICC_ISO_AUDIO,
		.sid = TEGRA234_SID_HDA,
		.regs = {
			.sid = {
				.override = 0x1a8,
				.security = 0x1ac,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE8AW,
		.name = "pcie8aw",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_8,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_PCIE8,
		.regs = {
			.sid = {
				.override = 0x1d8,
				.security = 0x1dc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE9AR,
		.name = "pcie9ar",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_9,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_PCIE9,
		.regs = {
			.sid = {
				.override = 0x1e0,
				.security = 0x1e4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE6AR1,
		.name = "pcie6ar1",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_6,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_PCIE6,
		.regs = {
			.sid = {
				.override = 0x1e8,
				.security = 0x1ec,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE9AW,
		.name = "pcie9aw",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_9,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_PCIE9,
		.regs = {
			.sid = {
				.override = 0x1f0,
				.security = 0x1f4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE10AR,
		.name = "pcie10ar",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_10,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_PCIE10,
		.regs = {
			.sid = {
				.override = 0x1f8,
				.security = 0x1fc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE10AW,
		.name = "pcie10aw",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_10,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_PCIE10,
		.regs = {
			.sid = {
				.override = 0x200,
				.security = 0x204,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE10AR1,
		.name = "pcie10ar1",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_10,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_PCIE10,
		.regs = {
			.sid = {
				.override = 0x240,
				.security = 0x244,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE7AR1,
		.name = "pcie7ar1",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_7,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_PCIE7,
		.regs = {
			.sid = {
				.override = 0x248,
				.security = 0x24c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_MGBEARD,
		.name = "mgbeard",
		.bpmp_id = TEGRA_ICC_BPMP_EQOS,
		.type = TEGRA_ICC_NISO,
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
		.bpmp_id = TEGRA_ICC_BPMP_EQOS,
		.type = TEGRA_ICC_NISO,
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
		.bpmp_id = TEGRA_ICC_BPMP_EQOS,
		.type = TEGRA_ICC_NISO,
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
		.bpmp_id = TEGRA_ICC_BPMP_EQOS,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_MGBE_VF3,
		.regs = {
			.sid = {
				.override = 0x2d8,
				.security = 0x2dc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_MGBEAWR,
		.bpmp_id = TEGRA_ICC_BPMP_EQOS,
		.type = TEGRA_ICC_NISO,
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
		.bpmp_id = TEGRA_ICC_BPMP_EQOS,
		.type = TEGRA_ICC_NISO,
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
		.bpmp_id = TEGRA_ICC_BPMP_EQOS,
		.type = TEGRA_ICC_NISO,
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
		.bpmp_id = TEGRA_ICC_BPMP_SDMMC_4,
		.type = TEGRA_ICC_NISO,
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
		.bpmp_id = TEGRA_ICC_BPMP_EQOS,
		.type = TEGRA_ICC_NISO,
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
		.bpmp_id = TEGRA_ICC_BPMP_SDMMC_4,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_SDMMC4,
		.regs = {
			.sid = {
				.override = 0x338,
				.security = 0x33c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_VICSRD,
		.name = "vicsrd",
		.bpmp_id = TEGRA_ICC_BPMP_VIC,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_VIC,
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
		.sid = TEGRA234_SID_VIC,
		.regs = {
			.sid = {
				.override = 0x368,
				.security = 0x36c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1RDB1,
		.name = "dla1rdb1",
		.bpmp_id = TEGRA_ICC_BPMP_DLA_1,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x370,
				.security = 0x374,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1WRB,
		.name = "dla1wrb",
		.bpmp_id = TEGRA_ICC_BPMP_DLA_1,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x378,
				.security = 0x37c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_VI2W,
		.name = "vi2w",
		.bpmp_id = TEGRA_ICC_BPMP_VI2,
		.type = TEGRA_ICC_ISO_VI,
		.sid = TEGRA234_SID_ISO_VI2,
		.regs = {
			.sid = {
				.override = 0x380,
				.security = 0x384,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_VI2FALR,
		.name = "vi2falr",
		.bpmp_id = TEGRA_ICC_BPMP_VI2FAL,
		.type = TEGRA_ICC_ISO_VIFAL,
		.sid = TEGRA234_SID_ISO_VI2FALC,
		.regs = {
			.sid = {
				.override = 0x388,
				.security = 0x38c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_VIW,
		.name = "viw",
		.bpmp_id = TEGRA_ICC_BPMP_VI,
		.type = TEGRA_ICC_ISO_VI,
		.sid = TEGRA234_SID_ISO_VI,
		.regs = {
			.sid = {
				.override = 0x390,
				.security = 0x394,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_NVDECSRD,
		.name = "nvdecsrd",
		.bpmp_id = TEGRA_ICC_BPMP_NVDEC,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_NVDEC,
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
		.sid = TEGRA234_SID_NVDEC,
		.regs = {
			.sid = {
				.override = 0x3c8,
				.security = 0x3cc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_APER,
		.name = "aper",
		.bpmp_id = TEGRA_ICC_BPMP_APE,
		.type = TEGRA_ICC_ISO_AUDIO,
		.sid = TEGRA234_SID_APE,
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
		.sid = TEGRA234_SID_APE,
		.regs = {
			.sid = {
				.override = 0x3d8,
				.security = 0x3dc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_VI2FALW,
		.name = "vi2falw",
		.bpmp_id = TEGRA_ICC_BPMP_VI2FAL,
		.type = TEGRA_ICC_ISO_VIFAL,
		.sid = TEGRA234_SID_ISO_VI2FALC,
		.regs = {
			.sid = {
				.override = 0x3e0,
				.security = 0x3e4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_NVJPGSRD,
		.name = "nvjpgsrd",
		.bpmp_id = TEGRA_ICC_BPMP_NVJPG_0,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_NVJPG,
		.regs = {
			.sid = {
				.override = 0x3f0,
				.security = 0x3f4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_NVJPGSWR,
		.name = "nvjpgswr",
		.bpmp_id = TEGRA_ICC_BPMP_NVJPG_0,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_NVJPG,
		.regs = {
			.sid = {
				.override = 0x3f8,
				.security = 0x3fc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_NVDISPLAYR,
		.name = "nvdisplayr",
		.bpmp_id = TEGRA_ICC_BPMP_DISPLAY,
		.type = TEGRA_ICC_ISO_DISPLAY,
		.sid = TEGRA234_SID_ISO_NVDISPLAY,
		.regs = {
			.sid = {
				.override = 0x490,
				.security = 0x494,
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
		.bpmp_id = TEGRA_ICC_BPMP_APEDMA,
		.type = TEGRA_ICC_ISO_AUDIO,
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
		.bpmp_id = TEGRA_ICC_BPMP_APEDMA,
		.type = TEGRA_ICC_ISO_AUDIO,
		.sid = TEGRA234_SID_APE,
		.regs = {
			.sid = {
				.override = 0x500,
				.security = 0x504,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_NVDISPLAYR1,
		.name = "nvdisplayr1",
		.bpmp_id = TEGRA_ICC_BPMP_DISPLAY,
		.type = TEGRA_ICC_ISO_DISPLAY,
		.sid = TEGRA234_SID_ISO_NVDISPLAY,
		.regs = {
			.sid = {
				.override = 0x508,
				.security = 0x50c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_VIFALR,
		.name = "vifalr",
		.bpmp_id = TEGRA_ICC_BPMP_VIFAL,
		.type = TEGRA_ICC_ISO_VIFAL,
		.sid = TEGRA234_SID_ISO_VIFALC,
		.regs = {
			.sid = {
				.override = 0x5e0,
				.security = 0x5e4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_VIFALW,
		.name = "vifalw",
		.bpmp_id = TEGRA_ICC_BPMP_VIFAL,
		.type = TEGRA_ICC_ISO_VIFAL,
		.sid = TEGRA234_SID_ISO_VIFALC,
		.regs = {
			.sid = {
				.override = 0x5e8,
				.security = 0x5ec,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0RDA,
		.name = "dla0rda",
		.bpmp_id = TEGRA_ICC_BPMP_DLA_0,
		.type = TEGRA_ICC_NISO,
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
		.bpmp_id = TEGRA_ICC_BPMP_DLA_0,
		.type = TEGRA_ICC_NISO,
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
		.bpmp_id = TEGRA_ICC_BPMP_DLA_0,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x600,
				.security = 0x604,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0FALWRB,
		.name = "dla0falwrb",
		.bpmp_id = TEGRA_ICC_BPMP_DLA_0,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x608,
				.security = 0x60c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1RDA,
		.name = "dla1rda",
		.bpmp_id = TEGRA_ICC_BPMP_DLA_1,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x610,
				.security = 0x614,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1FALRDB,
		.name = "dla1falrdb",
		.bpmp_id = TEGRA_ICC_BPMP_DLA_1,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x618,
				.security = 0x61c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1WRA,
		.name = "dla1wra",
		.bpmp_id = TEGRA_ICC_BPMP_DLA_1,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x620,
				.security = 0x624,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1FALWRB,
		.name = "dla1falwrb",
		.bpmp_id = TEGRA_ICC_BPMP_DLA_1,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x628,
				.security = 0x62c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_RCER,
		.name = "rcer",
		.bpmp_id = TEGRA_ICC_BPMP_RCE,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_RCE,
		.regs = {
			.sid = {
				.override = 0x690,
				.security = 0x694,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_RCEW,
		.name = "rcew",
		.bpmp_id = TEGRA_ICC_BPMP_RCE,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_RCE,
		.regs = {
			.sid = {
				.override = 0x698,
				.security = 0x69c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE0R,
		.name = "pcie0r",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_0,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_PCIE0,
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
		.sid = TEGRA234_SID_PCIE0,
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
		.sid = TEGRA234_SID_PCIE1,
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
		.sid = TEGRA234_SID_PCIE1,
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
		.sid = TEGRA234_SID_PCIE2,
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
		.sid = TEGRA234_SID_PCIE2,
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
		.sid = TEGRA234_SID_PCIE3,
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
		.sid = TEGRA234_SID_PCIE3,
		.regs = {
			.sid = {
				.override = 0x6f8,
				.security = 0x6fc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE4R,
		.name = "pcie4r",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_4,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_PCIE4,
		.regs = {
			.sid = {
				.override = 0x700,
				.security = 0x704,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE4W,
		.name = "pcie4w",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_4,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_PCIE4,
		.regs = {
			.sid = {
				.override = 0x708,
				.security = 0x70c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE5R,
		.name = "pcie5r",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_5,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_PCIE5,
		.regs = {
			.sid = {
				.override = 0x710,
				.security = 0x714,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE5W,
		.name = "pcie5w",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_5,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_PCIE5,
		.regs = {
			.sid = {
				.override = 0x718,
				.security = 0x71c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA0RDA1,
		.name = "dla0rda1",
		.bpmp_id = TEGRA_ICC_BPMP_DLA_0,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x748,
				.security = 0x74c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_DLA1RDA1,
		.name = "dla1rda1",
		.sid = TEGRA234_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x750,
				.security = 0x754,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_PCIE5R1,
		.name = "pcie5r1",
		.bpmp_id = TEGRA_ICC_BPMP_PCIE_5,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_PCIE5,
		.regs = {
			.sid = {
				.override = 0x778,
				.security = 0x77c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_NVJPG1SRD,
		.name = "nvjpg1srd",
		.bpmp_id = TEGRA_ICC_BPMP_NVJPG_1,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_NVJPG1,
		.regs = {
			.sid = {
				.override = 0x918,
				.security = 0x91c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_NVJPG1SWR,
		.name = "nvjpg1swr",
		.bpmp_id = TEGRA_ICC_BPMP_NVJPG_1,
		.type = TEGRA_ICC_NISO,
		.sid = TEGRA234_SID_NVJPG1,
		.regs = {
			.sid = {
				.override = 0x920,
				.security = 0x924,
			},
		},
	}, {
		.id = TEGRA_ICC_MC_CPU_CLUSTER0,
		.name = "sw_cluster0",
		.bpmp_id = TEGRA_ICC_BPMP_CPU_CLUSTER0,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA_ICC_MC_CPU_CLUSTER1,
		.name = "sw_cluster1",
		.bpmp_id = TEGRA_ICC_BPMP_CPU_CLUSTER1,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA_ICC_MC_CPU_CLUSTER2,
		.name = "sw_cluster2",
		.bpmp_id = TEGRA_ICC_BPMP_CPU_CLUSTER2,
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
	},
};

/*
 * tegra234_mc_icc_set() - Pass MC client info to the BPMP-FW
 * @src: ICC node for Memory Controller's (MC) Client
 * @dst: ICC node for Memory Controller (MC)
 *
 * Passing the current request info from the MC to the BPMP-FW where
 * LA and PTSA registers are accessed and the final EMC freq is set
 * based on client_id, type, latency and bandwidth.
 * icc_set_bw() makes set_bw calls for both MC and EMC providers in
 * sequence. Both the calls are protected by 'mutex_lock(&icc_lock)'.
 * So, the data passed won't be updated by concurrent set calls from
 * other clients.
 */
static int tegra234_mc_icc_set(struct icc_node *src, struct icc_node *dst)
{
	struct tegra_mc *mc = icc_provider_to_tegra_mc(dst->provider);
	struct mrq_bwmgr_int_request bwmgr_req = { 0 };
	struct mrq_bwmgr_int_response bwmgr_resp = { 0 };
	const struct tegra_mc_client *pclient = src->data;
	struct tegra_bpmp_message msg;
	int ret;

	/*
	 * Same Src and Dst node will happen during boot from icc_node_add().
	 * This can be used to pre-initialize and set bandwidth for all clients
	 * before their drivers are loaded. We are skipping this case as for us,
	 * the pre-initialization already happened in Bootloader(MB2) and BPMP-FW.
	 */
	if (src->id == dst->id)
		return 0;

	if (!mc->bwmgr_mrq_supported)
		return 0;

	if (!mc->bpmp) {
		dev_err(mc->dev, "BPMP reference NULL\n");
		return -ENOENT;
	}

	if (pclient->type == TEGRA_ICC_NISO)
		bwmgr_req.bwmgr_calc_set_req.niso_bw = src->avg_bw;
	else
		bwmgr_req.bwmgr_calc_set_req.iso_bw = src->avg_bw;

	bwmgr_req.bwmgr_calc_set_req.client_id = pclient->bpmp_id;

	bwmgr_req.cmd = CMD_BWMGR_INT_CALC_AND_SET;
	bwmgr_req.bwmgr_calc_set_req.mc_floor = src->peak_bw;
	bwmgr_req.bwmgr_calc_set_req.floor_unit = BWMGR_INT_UNIT_KBPS;

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_BWMGR_INT;
	msg.tx.data = &bwmgr_req;
	msg.tx.size = sizeof(bwmgr_req);
	msg.rx.data = &bwmgr_resp;
	msg.rx.size = sizeof(bwmgr_resp);

	if (pclient->bpmp_id >= TEGRA_ICC_BPMP_CPU_CLUSTER0 &&
	    pclient->bpmp_id <= TEGRA_ICC_BPMP_CPU_CLUSTER2)
		msg.flags = TEGRA_BPMP_MESSAGE_RESET;

	ret = tegra_bpmp_transfer(mc->bpmp, &msg);
	if (ret < 0) {
		dev_err(mc->dev, "BPMP transfer failed: %d\n", ret);
		goto error;
	}
	if (msg.rx.ret < 0) {
		pr_err("failed to set bandwidth for %u: %d\n",
		       bwmgr_req.bwmgr_calc_set_req.client_id, msg.rx.ret);
		ret = -EINVAL;
	}

error:
	return ret;
}

static int tegra234_mc_icc_aggregate(struct icc_node *node, u32 tag, u32 avg_bw,
				     u32 peak_bw, u32 *agg_avg, u32 *agg_peak)
{
	struct icc_provider *p = node->provider;
	struct tegra_mc *mc = icc_provider_to_tegra_mc(p);

	if (!mc->bwmgr_mrq_supported)
		return 0;

	if (node->id == TEGRA_ICC_MC_CPU_CLUSTER0 ||
	    node->id == TEGRA_ICC_MC_CPU_CLUSTER1 ||
	    node->id == TEGRA_ICC_MC_CPU_CLUSTER2) {
		if (mc)
			peak_bw = peak_bw * mc->num_channels;
	}

	*agg_avg += avg_bw;
	*agg_peak = max(*agg_peak, peak_bw);

	return 0;
}

static int tegra234_mc_icc_get_init_bw(struct icc_node *node, u32 *avg, u32 *peak)
{
	*avg = 0;
	*peak = 0;

	return 0;
}

static const struct tegra_mc_icc_ops tegra234_mc_icc_ops = {
	.xlate = tegra_mc_icc_xlate,
	.aggregate = tegra234_mc_icc_aggregate,
	.get_bw = tegra234_mc_icc_get_init_bw,
	.set = tegra234_mc_icc_set,
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
	.icc_ops = &tegra234_mc_icc_ops,
	.ch_intmask = 0x0000ff00,
	.global_intstatus_channel_shift = 8,
	/*
	 * Additionally, there are lite carveouts but those are not currently
	 * supported.
	 */
	.num_carveouts = 32,
};
