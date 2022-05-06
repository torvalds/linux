// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2021 NVIDIA CORPORATION.  All rights reserved.
 */

#include <soc/tegra/mc.h>

#include <dt-bindings/memory/tegra194-mc.h>

#include "mc.h"

static const struct tegra_mc_client tegra194_mc_clients[] = {
	{
		.id = TEGRA194_MEMORY_CLIENT_PTCR,
		.name = "ptcr",
		.sid = TEGRA194_SID_PASSTHROUGH,
		.regs = {
			.sid = {
				.override = 0x000,
				.security = 0x004,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_MIU7R,
		.name = "miu7r",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.sid = {
				.override = 0x008,
				.security = 0x00c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_MIU7W,
		.name = "miu7w",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.sid = {
				.override = 0x010,
				.security = 0x014,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_HDAR,
		.name = "hdar",
		.sid = TEGRA194_SID_HDA,
		.regs = {
			.sid = {
				.override = 0x0a8,
				.security = 0x0ac,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_HOST1XDMAR,
		.name = "host1xdmar",
		.sid = TEGRA194_SID_HOST1X,
		.regs = {
			.sid = {
				.override = 0x0b0,
				.security = 0x0b4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_NVENCSRD,
		.name = "nvencsrd",
		.sid = TEGRA194_SID_NVENC,
		.regs = {
			.sid = {
				.override = 0x0e0,
				.security = 0x0e4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_SATAR,
		.name = "satar",
		.sid = TEGRA194_SID_SATA,
		.regs = {
			.sid = {
				.override = 0x0f8,
				.security = 0x0fc,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_MPCORER,
		.name = "mpcorer",
		.sid = TEGRA194_SID_PASSTHROUGH,
		.regs = {
			.sid = {
				.override = 0x138,
				.security = 0x13c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_NVENCSWR,
		.name = "nvencswr",
		.sid = TEGRA194_SID_NVENC,
		.regs = {
			.sid = {
				.override = 0x158,
				.security = 0x15c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_HDAW,
		.name = "hdaw",
		.sid = TEGRA194_SID_HDA,
		.regs = {
			.sid = {
				.override = 0x1a8,
				.security = 0x1ac,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_MPCOREW,
		.name = "mpcorew",
		.sid = TEGRA194_SID_PASSTHROUGH,
		.regs = {
			.sid = {
				.override = 0x1c8,
				.security = 0x1cc,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_SATAW,
		.name = "sataw",
		.sid = TEGRA194_SID_SATA,
		.regs = {
			.sid = {
				.override = 0x1e8,
				.security = 0x1ec,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_ISPRA,
		.name = "ispra",
		.sid = TEGRA194_SID_ISP,
		.regs = {
			.sid = {
				.override = 0x220,
				.security = 0x224,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_ISPFALR,
		.name = "ispfalr",
		.sid = TEGRA194_SID_ISP_FALCON,
		.regs = {
			.sid = {
				.override = 0x228,
				.security = 0x22c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_ISPWA,
		.name = "ispwa",
		.sid = TEGRA194_SID_ISP,
		.regs = {
			.sid = {
				.override = 0x230,
				.security = 0x234,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_ISPWB,
		.name = "ispwb",
		.sid = TEGRA194_SID_ISP,
		.regs = {
			.sid = {
				.override = 0x238,
				.security = 0x23c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_XUSB_HOSTR,
		.name = "xusb_hostr",
		.sid = TEGRA194_SID_XUSB_HOST,
		.regs = {
			.sid = {
				.override = 0x250,
				.security = 0x254,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_XUSB_HOSTW,
		.name = "xusb_hostw",
		.sid = TEGRA194_SID_XUSB_HOST,
		.regs = {
			.sid = {
				.override = 0x258,
				.security = 0x25c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_XUSB_DEVR,
		.name = "xusb_devr",
		.sid = TEGRA194_SID_XUSB_DEV,
		.regs = {
			.sid = {
				.override = 0x260,
				.security = 0x264,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_XUSB_DEVW,
		.name = "xusb_devw",
		.sid = TEGRA194_SID_XUSB_DEV,
		.regs = {
			.sid = {
				.override = 0x268,
				.security = 0x26c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_SDMMCRA,
		.name = "sdmmcra",
		.sid = TEGRA194_SID_SDMMC1,
		.regs = {
			.sid = {
				.override = 0x300,
				.security = 0x304,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_SDMMCR,
		.name = "sdmmcr",
		.sid = TEGRA194_SID_SDMMC3,
		.regs = {
			.sid = {
				.override = 0x310,
				.security = 0x314,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_SDMMCRAB,
		.name = "sdmmcrab",
		.sid = TEGRA194_SID_SDMMC4,
		.regs = {
			.sid = {
				.override = 0x318,
				.security = 0x31c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_SDMMCWA,
		.name = "sdmmcwa",
		.sid = TEGRA194_SID_SDMMC1,
		.regs = {
			.sid = {
				.override = 0x320,
				.security = 0x324,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_SDMMCW,
		.name = "sdmmcw",
		.sid = TEGRA194_SID_SDMMC3,
		.regs = {
			.sid = {
				.override = 0x330,
				.security = 0x334,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_SDMMCWAB,
		.name = "sdmmcwab",
		.sid = TEGRA194_SID_SDMMC4,
		.regs = {
			.sid = {
				.override = 0x338,
				.security = 0x33c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_VICSRD,
		.name = "vicsrd",
		.sid = TEGRA194_SID_VIC,
		.regs = {
			.sid = {
				.override = 0x360,
				.security = 0x364,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_VICSWR,
		.name = "vicswr",
		.sid = TEGRA194_SID_VIC,
		.regs = {
			.sid = {
				.override = 0x368,
				.security = 0x36c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_VIW,
		.name = "viw",
		.sid = TEGRA194_SID_VI,
		.regs = {
			.sid = {
				.override = 0x390,
				.security = 0x394,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_NVDECSRD,
		.name = "nvdecsrd",
		.sid = TEGRA194_SID_NVDEC,
		.regs = {
			.sid = {
				.override = 0x3c0,
				.security = 0x3c4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_NVDECSWR,
		.name = "nvdecswr",
		.sid = TEGRA194_SID_NVDEC,
		.regs = {
			.sid = {
				.override = 0x3c8,
				.security = 0x3cc,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_APER,
		.name = "aper",
		.sid = TEGRA194_SID_APE,
		.regs = {
			.sid = {
				.override = 0x3c0,
				.security = 0x3c4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_APEW,
		.name = "apew",
		.sid = TEGRA194_SID_APE,
		.regs = {
			.sid = {
				.override = 0x3d0,
				.security = 0x3d4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_NVJPGSRD,
		.name = "nvjpgsrd",
		.sid = TEGRA194_SID_NVJPG,
		.regs = {
			.sid = {
				.override = 0x3f0,
				.security = 0x3f4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_NVJPGSWR,
		.name = "nvjpgswr",
		.sid = TEGRA194_SID_NVJPG,
		.regs = {
			.sid = {
				.override = 0x3f0,
				.security = 0x3f4,
			},
		},
	}, {
		.name = "axiapr",
		.id = TEGRA194_MEMORY_CLIENT_AXIAPR,
		.sid = TEGRA194_SID_PASSTHROUGH,
		.regs = {
			.sid = {
				.override = 0x410,
				.security = 0x414,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_AXIAPW,
		.name = "axiapw",
		.sid = TEGRA194_SID_PASSTHROUGH,
		.regs = {
			.sid = {
				.override = 0x418,
				.security = 0x41c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_ETRR,
		.name = "etrr",
		.sid = TEGRA194_SID_ETR,
		.regs = {
			.sid = {
				.override = 0x420,
				.security = 0x424,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_ETRW,
		.name = "etrw",
		.sid = TEGRA194_SID_ETR,
		.regs = {
			.sid = {
				.override = 0x428,
				.security = 0x42c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_AXISR,
		.name = "axisr",
		.sid = TEGRA194_SID_PASSTHROUGH,
		.regs = {
			.sid = {
				.override = 0x460,
				.security = 0x464,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_AXISW,
		.name = "axisw",
		.sid = TEGRA194_SID_PASSTHROUGH,
		.regs = {
			.sid = {
				.override = 0x468,
				.security = 0x46c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_EQOSR,
		.name = "eqosr",
		.sid = TEGRA194_SID_EQOS,
		.regs = {
			.sid = {
				.override = 0x470,
				.security = 0x474,
			},
		},
	}, {
		.name = "eqosw",
		.id = TEGRA194_MEMORY_CLIENT_EQOSW,
		.sid = TEGRA194_SID_EQOS,
		.regs = {
			.sid = {
				.override = 0x478,
				.security = 0x47c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_UFSHCR,
		.name = "ufshcr",
		.sid = TEGRA194_SID_UFSHC,
		.regs = {
			.sid = {
				.override = 0x480,
				.security = 0x484,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_UFSHCW,
		.name = "ufshcw",
		.sid = TEGRA194_SID_UFSHC,
		.regs = {
			.sid = {
				.override = 0x488,
				.security = 0x48c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_NVDISPLAYR,
		.name = "nvdisplayr",
		.sid = TEGRA194_SID_NVDISPLAY,
		.regs = {
			.sid = {
				.override = 0x490,
				.security = 0x494,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_BPMPR,
		.name = "bpmpr",
		.sid = TEGRA194_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x498,
				.security = 0x49c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_BPMPW,
		.name = "bpmpw",
		.sid = TEGRA194_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x4a0,
				.security = 0x4a4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_BPMPDMAR,
		.name = "bpmpdmar",
		.sid = TEGRA194_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x4a8,
				.security = 0x4ac,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_BPMPDMAW,
		.name = "bpmpdmaw",
		.sid = TEGRA194_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x4b0,
				.security = 0x4b4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_AONR,
		.name = "aonr",
		.sid = TEGRA194_SID_AON,
		.regs = {
			.sid = {
				.override = 0x4b8,
				.security = 0x4bc,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_AONW,
		.name = "aonw",
		.sid = TEGRA194_SID_AON,
		.regs = {
			.sid = {
				.override = 0x4c0,
				.security = 0x4c4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_AONDMAR,
		.name = "aondmar",
		.sid = TEGRA194_SID_AON,
		.regs = {
			.sid = {
				.override = 0x4c8,
				.security = 0x4cc,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_AONDMAW,
		.name = "aondmaw",
		.sid = TEGRA194_SID_AON,
		.regs = {
			.sid = {
				.override = 0x4d0,
				.security = 0x4d4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_SCER,
		.name = "scer",
		.sid = TEGRA194_SID_SCE,
		.regs = {
			.sid = {
				.override = 0x4d8,
				.security = 0x4dc,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_SCEW,
		.name = "scew",
		.sid = TEGRA194_SID_SCE,
		.regs = {
			.sid = {
				.override = 0x4e0,
				.security = 0x4e4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_SCEDMAR,
		.name = "scedmar",
		.sid = TEGRA194_SID_SCE,
		.regs = {
			.sid = {
				.override = 0x4e8,
				.security = 0x4ec,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_SCEDMAW,
		.name = "scedmaw",
		.sid = TEGRA194_SID_SCE,
		.regs = {
			.sid = {
				.override = 0x4f0,
				.security = 0x4f4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_APEDMAR,
		.name = "apedmar",
		.sid = TEGRA194_SID_APE,
		.regs = {
			.sid = {
				.override = 0x4f8,
				.security = 0x4fc,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_APEDMAW,
		.name = "apedmaw",
		.sid = TEGRA194_SID_APE,
		.regs = {
			.sid = {
				.override = 0x500,
				.security = 0x504,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_NVDISPLAYR1,
		.name = "nvdisplayr1",
		.sid = TEGRA194_SID_NVDISPLAY,
		.regs = {
			.sid = {
				.override = 0x508,
				.security = 0x50c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_VICSRD1,
		.name = "vicsrd1",
		.sid = TEGRA194_SID_VIC,
		.regs = {
			.sid = {
				.override = 0x510,
				.security = 0x514,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_NVDECSRD1,
		.name = "nvdecsrd1",
		.sid = TEGRA194_SID_NVDEC,
		.regs = {
			.sid = {
				.override = 0x518,
				.security = 0x51c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_MIU0R,
		.name = "miu0r",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.sid = {
				.override = 0x530,
				.security = 0x534,
			},
		},
	}, {
		.name = "miu0w",
		.id = TEGRA194_MEMORY_CLIENT_MIU0W,
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.sid = {
				.override = 0x538,
				.security = 0x53c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_MIU1R,
		.name = "miu1r",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.sid = {
				.override = 0x540,
				.security = 0x544,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_MIU1W,
		.name = "miu1w",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.sid = {
				.override = 0x548,
				.security = 0x54c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_MIU2R,
		.name = "miu2r",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.sid = {
				.override = 0x570,
				.security = 0x574,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_MIU2W,
		.name = "miu2w",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.sid = {
				.override = 0x578,
				.security = 0x57c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_MIU3R,
		.name = "miu3r",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.sid = {
				.override = 0x580,
				.security = 0x584,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_MIU3W,
		.name = "miu3w",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.sid = {
				.override = 0x588,
				.security = 0x58c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_MIU4R,
		.name = "miu4r",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.sid = {
				.override = 0x590,
				.security = 0x594,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_MIU4W,
		.name = "miu4w",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.sid = {
				.override = 0x598,
				.security = 0x59c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_DPMUR,
		.name = "dpmur",
		.sid = TEGRA194_SID_PASSTHROUGH,
		.regs = {
			.sid = {
				.override = 0x598,
				.security = 0x59c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_VIFALR,
		.name = "vifalr",
		.sid = TEGRA194_SID_VI_FALCON,
		.regs = {
			.sid = {
				.override = 0x5e0,
				.security = 0x5e4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_VIFALW,
		.name = "vifalw",
		.sid = TEGRA194_SID_VI_FALCON,
		.regs = {
			.sid = {
				.override = 0x5e8,
				.security = 0x5ec,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_DLA0RDA,
		.name = "dla0rda",
		.sid = TEGRA194_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x5f0,
				.security = 0x5f4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_DLA0FALRDB,
		.name = "dla0falrdb",
		.sid = TEGRA194_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x5f8,
				.security = 0x5fc,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_DLA0WRA,
		.name = "dla0wra",
		.sid = TEGRA194_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x600,
				.security = 0x604,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_DLA0FALWRB,
		.name = "dla0falwrb",
		.sid = TEGRA194_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x608,
				.security = 0x60c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_DLA1RDA,
		.name = "dla1rda",
		.sid = TEGRA194_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x610,
				.security = 0x614,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_DLA1FALRDB,
		.name = "dla1falrdb",
		.sid = TEGRA194_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x618,
				.security = 0x61c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_DLA1WRA,
		.name = "dla1wra",
		.sid = TEGRA194_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x620,
				.security = 0x624,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_DLA1FALWRB,
		.name = "dla1falwrb",
		.sid = TEGRA194_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x628,
				.security = 0x62c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PVA0RDA,
		.name = "pva0rda",
		.sid = TEGRA194_SID_PVA0,
		.regs = {
			.sid = {
				.override = 0x630,
				.security = 0x634,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PVA0RDB,
		.name = "pva0rdb",
		.sid = TEGRA194_SID_PVA0,
		.regs = {
			.sid = {
				.override = 0x638,
				.security = 0x63c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PVA0RDC,
		.name = "pva0rdc",
		.sid = TEGRA194_SID_PVA0,
		.regs = {
			.sid = {
				.override = 0x640,
				.security = 0x644,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PVA0WRA,
		.name = "pva0wra",
		.sid = TEGRA194_SID_PVA0,
		.regs = {
			.sid = {
				.override = 0x648,
				.security = 0x64c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PVA0WRB,
		.name = "pva0wrb",
		.sid = TEGRA194_SID_PVA0,
		.regs = {
			.sid = {
				.override = 0x650,
				.security = 0x654,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PVA0WRC,
		.name = "pva0wrc",
		.sid = TEGRA194_SID_PVA0,
		.regs = {
			.sid = {
				.override = 0x658,
				.security = 0x65c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PVA1RDA,
		.name = "pva1rda",
		.sid = TEGRA194_SID_PVA1,
		.regs = {
			.sid = {
				.override = 0x660,
				.security = 0x664,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PVA1RDB,
		.name = "pva1rdb",
		.sid = TEGRA194_SID_PVA1,
		.regs = {
			.sid = {
				.override = 0x668,
				.security = 0x66c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PVA1RDC,
		.name = "pva1rdc",
		.sid = TEGRA194_SID_PVA1,
		.regs = {
			.sid = {
				.override = 0x670,
				.security = 0x674,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PVA1WRA,
		.name = "pva1wra",
		.sid = TEGRA194_SID_PVA1,
		.regs = {
			.sid = {
				.override = 0x678,
				.security = 0x67c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PVA1WRB,
		.name = "pva1wrb",
		.sid = TEGRA194_SID_PVA1,
		.regs = {
			.sid = {
				.override = 0x680,
				.security = 0x684,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PVA1WRC,
		.name = "pva1wrc",
		.sid = TEGRA194_SID_PVA1,
		.regs = {
			.sid = {
				.override = 0x688,
				.security = 0x68c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_RCER,
		.name = "rcer",
		.sid = TEGRA194_SID_RCE,
		.regs = {
			.sid = {
				.override = 0x690,
				.security = 0x694,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_RCEW,
		.name = "rcew",
		.sid = TEGRA194_SID_RCE,
		.regs = {
			.sid = {
				.override = 0x698,
				.security = 0x69c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_RCEDMAR,
		.name = "rcedmar",
		.sid = TEGRA194_SID_RCE,
		.regs = {
			.sid = {
				.override = 0x6a0,
				.security = 0x6a4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_RCEDMAW,
		.name = "rcedmaw",
		.sid = TEGRA194_SID_RCE,
		.regs = {
			.sid = {
				.override = 0x6a8,
				.security = 0x6ac,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_NVENC1SRD,
		.name = "nvenc1srd",
		.sid = TEGRA194_SID_NVENC1,
		.regs = {
			.sid = {
				.override = 0x6b0,
				.security = 0x6b4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_NVENC1SWR,
		.name = "nvenc1swr",
		.sid = TEGRA194_SID_NVENC1,
		.regs = {
			.sid = {
				.override = 0x6b8,
				.security = 0x6bc,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PCIE0R,
		.name = "pcie0r",
		.sid = TEGRA194_SID_PCIE0,
		.regs = {
			.sid = {
				.override = 0x6c0,
				.security = 0x6c4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PCIE0W,
		.name = "pcie0w",
		.sid = TEGRA194_SID_PCIE0,
		.regs = {
			.sid = {
				.override = 0x6c8,
				.security = 0x6cc,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PCIE1R,
		.name = "pcie1r",
		.sid = TEGRA194_SID_PCIE1,
		.regs = {
			.sid = {
				.override = 0x6d0,
				.security = 0x6d4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PCIE1W,
		.name = "pcie1w",
		.sid = TEGRA194_SID_PCIE1,
		.regs = {
			.sid = {
				.override = 0x6d8,
				.security = 0x6dc,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PCIE2AR,
		.name = "pcie2ar",
		.sid = TEGRA194_SID_PCIE2,
		.regs = {
			.sid = {
				.override = 0x6e0,
				.security = 0x6e4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PCIE2AW,
		.name = "pcie2aw",
		.sid = TEGRA194_SID_PCIE2,
		.regs = {
			.sid = {
				.override = 0x6e8,
				.security = 0x6ec,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PCIE3R,
		.name = "pcie3r",
		.sid = TEGRA194_SID_PCIE3,
		.regs = {
			.sid = {
				.override = 0x6f0,
				.security = 0x6f4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PCIE3W,
		.name = "pcie3w",
		.sid = TEGRA194_SID_PCIE3,
		.regs = {
			.sid = {
				.override = 0x6f8,
				.security = 0x6fc,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PCIE4R,
		.name = "pcie4r",
		.sid = TEGRA194_SID_PCIE4,
		.regs = {
			.sid = {
				.override = 0x700,
				.security = 0x704,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PCIE4W,
		.name = "pcie4w",
		.sid = TEGRA194_SID_PCIE4,
		.regs = {
			.sid = {
				.override = 0x708,
				.security = 0x70c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PCIE5R,
		.name = "pcie5r",
		.sid = TEGRA194_SID_PCIE5,
		.regs = {
			.sid = {
				.override = 0x710,
				.security = 0x714,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PCIE5W,
		.name = "pcie5w",
		.sid = TEGRA194_SID_PCIE5,
		.regs = {
			.sid = {
				.override = 0x718,
				.security = 0x71c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_ISPFALW,
		.name = "ispfalw",
		.sid = TEGRA194_SID_ISP_FALCON,
		.regs = {
			.sid = {
				.override = 0x720,
				.security = 0x724,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_DLA0RDA1,
		.name = "dla0rda1",
		.sid = TEGRA194_SID_NVDLA0,
		.regs = {
			.sid = {
				.override = 0x748,
				.security = 0x74c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_DLA1RDA1,
		.name = "dla1rda1",
		.sid = TEGRA194_SID_NVDLA1,
		.regs = {
			.sid = {
				.override = 0x750,
				.security = 0x754,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PVA0RDA1,
		.name = "pva0rda1",
		.sid = TEGRA194_SID_PVA0,
		.regs = {
			.sid = {
				.override = 0x758,
				.security = 0x75c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PVA0RDB1,
		.name = "pva0rdb1",
		.sid = TEGRA194_SID_PVA0,
		.regs = {
			.sid = {
				.override = 0x760,
				.security = 0x764,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PVA1RDA1,
		.name = "pva1rda1",
		.sid = TEGRA194_SID_PVA1,
		.regs = {
			.sid = {
				.override = 0x768,
				.security = 0x76c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PVA1RDB1,
		.name = "pva1rdb1",
		.sid = TEGRA194_SID_PVA1,
		.regs = {
			.sid = {
				.override = 0x770,
				.security = 0x774,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PCIE5R1,
		.name = "pcie5r1",
		.sid = TEGRA194_SID_PCIE5,
		.regs = {
			.sid = {
				.override = 0x778,
				.security = 0x77c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_NVENCSRD1,
		.name = "nvencsrd1",
		.sid = TEGRA194_SID_NVENC,
		.regs = {
			.sid = {
				.override = 0x780,
				.security = 0x784,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_NVENC1SRD1,
		.name = "nvenc1srd1",
		.sid = TEGRA194_SID_NVENC1,
		.regs = {
			.sid = {
				.override = 0x788,
				.security = 0x78c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_ISPRA1,
		.name = "ispra1",
		.sid = TEGRA194_SID_ISP,
		.regs = {
			.sid = {
				.override = 0x790,
				.security = 0x794,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_PCIE0R1,
		.name = "pcie0r1",
		.sid = TEGRA194_SID_PCIE0,
		.regs = {
			.sid = {
				.override = 0x798,
				.security = 0x79c,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_NVDEC1SRD,
		.name = "nvdec1srd",
		.sid = TEGRA194_SID_NVDEC1,
		.regs = {
			.sid = {
				.override = 0x7c8,
				.security = 0x7cc,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_NVDEC1SRD1,
		.name = "nvdec1srd1",
		.sid = TEGRA194_SID_NVDEC1,
		.regs = {
			.sid = {
				.override = 0x7d0,
				.security = 0x7d4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_NVDEC1SWR,
		.name = "nvdec1swr",
		.sid = TEGRA194_SID_NVDEC1,
		.regs = {
			.sid = {
				.override = 0x7d8,
				.security = 0x7dc,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_MIU5R,
		.name = "miu5r",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.sid = {
				.override = 0x7e0,
				.security = 0x7e4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_MIU5W,
		.name = "miu5w",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.sid = {
				.override = 0x7e8,
				.security = 0x7ec,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_MIU6R,
		.name = "miu6r",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.sid = {
				.override = 0x7f0,
				.security = 0x7f4,
			},
		},
	}, {
		.id = TEGRA194_MEMORY_CLIENT_MIU6W,
		.name = "miu6w",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.sid = {
				.override = 0x7f8,
				.security = 0x7fc,
			},
		},
	},
};

const struct tegra_mc_soc tegra194_mc_soc = {
	.num_clients = ARRAY_SIZE(tegra194_mc_clients),
	.clients = tegra194_mc_clients,
	.num_address_bits = 40,
	.num_channels = 16,
	.ops = &tegra186_mc_ops,
};
