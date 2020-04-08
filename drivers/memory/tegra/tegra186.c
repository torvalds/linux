// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#if defined(CONFIG_ARCH_TEGRA_186_SOC)
#include <dt-bindings/memory/tegra186-mc.h>
#endif

#if defined(CONFIG_ARCH_TEGRA_194_SOC)
#include <dt-bindings/memory/tegra194-mc.h>
#endif

struct tegra186_mc_client {
	const char *name;
	unsigned int sid;
	struct {
		unsigned int override;
		unsigned int security;
	} regs;
};

struct tegra186_mc_soc {
	const struct tegra186_mc_client *clients;
	unsigned int num_clients;
};

struct tegra186_mc {
	struct device *dev;
	void __iomem *regs;

	const struct tegra186_mc_soc *soc;
};

static void tegra186_mc_program_sid(struct tegra186_mc *mc)
{
	unsigned int i;

	for (i = 0; i < mc->soc->num_clients; i++) {
		const struct tegra186_mc_client *client = &mc->soc->clients[i];
		u32 override, security;

		override = readl(mc->regs + client->regs.override);
		security = readl(mc->regs + client->regs.security);

		dev_dbg(mc->dev, "client %s: override: %x security: %x\n",
			client->name, override, security);

		dev_dbg(mc->dev, "setting SID %u for %s\n", client->sid,
			client->name);
		writel(client->sid, mc->regs + client->regs.override);

		override = readl(mc->regs + client->regs.override);
		security = readl(mc->regs + client->regs.security);

		dev_dbg(mc->dev, "client %s: override: %x security: %x\n",
			client->name, override, security);
	}
}

#if defined(CONFIG_ARCH_TEGRA_186_SOC)
static const struct tegra186_mc_client tegra186_mc_clients[] = {
	{
		.name = "ptcr",
		.sid = TEGRA186_SID_PASSTHROUGH,
		.regs = {
			.override = 0x000,
			.security = 0x004,
		},
	}, {
		.name = "afir",
		.sid = TEGRA186_SID_AFI,
		.regs = {
			.override = 0x070,
			.security = 0x074,
		},
	}, {
		.name = "hdar",
		.sid = TEGRA186_SID_HDA,
		.regs = {
			.override = 0x0a8,
			.security = 0x0ac,
		},
	}, {
		.name = "host1xdmar",
		.sid = TEGRA186_SID_HOST1X,
		.regs = {
			.override = 0x0b0,
			.security = 0x0b4,
		},
	}, {
		.name = "nvencsrd",
		.sid = TEGRA186_SID_NVENC,
		.regs = {
			.override = 0x0e0,
			.security = 0x0e4,
		},
	}, {
		.name = "satar",
		.sid = TEGRA186_SID_SATA,
		.regs = {
			.override = 0x0f8,
			.security = 0x0fc,
		},
	}, {
		.name = "mpcorer",
		.sid = TEGRA186_SID_PASSTHROUGH,
		.regs = {
			.override = 0x138,
			.security = 0x13c,
		},
	}, {
		.name = "nvencswr",
		.sid = TEGRA186_SID_NVENC,
		.regs = {
			.override = 0x158,
			.security = 0x15c,
		},
	}, {
		.name = "afiw",
		.sid = TEGRA186_SID_AFI,
		.regs = {
			.override = 0x188,
			.security = 0x18c,
		},
	}, {
		.name = "hdaw",
		.sid = TEGRA186_SID_HDA,
		.regs = {
			.override = 0x1a8,
			.security = 0x1ac,
		},
	}, {
		.name = "mpcorew",
		.sid = TEGRA186_SID_PASSTHROUGH,
		.regs = {
			.override = 0x1c8,
			.security = 0x1cc,
		},
	}, {
		.name = "sataw",
		.sid = TEGRA186_SID_SATA,
		.regs = {
			.override = 0x1e8,
			.security = 0x1ec,
		},
	}, {
		.name = "ispra",
		.sid = TEGRA186_SID_ISP,
		.regs = {
			.override = 0x220,
			.security = 0x224,
		},
	}, {
		.name = "ispwa",
		.sid = TEGRA186_SID_ISP,
		.regs = {
			.override = 0x230,
			.security = 0x234,
		},
	}, {
		.name = "ispwb",
		.sid = TEGRA186_SID_ISP,
		.regs = {
			.override = 0x238,
			.security = 0x23c,
		},
	}, {
		.name = "xusb_hostr",
		.sid = TEGRA186_SID_XUSB_HOST,
		.regs = {
			.override = 0x250,
			.security = 0x254,
		},
	}, {
		.name = "xusb_hostw",
		.sid = TEGRA186_SID_XUSB_HOST,
		.regs = {
			.override = 0x258,
			.security = 0x25c,
		},
	}, {
		.name = "xusb_devr",
		.sid = TEGRA186_SID_XUSB_DEV,
		.regs = {
			.override = 0x260,
			.security = 0x264,
		},
	}, {
		.name = "xusb_devw",
		.sid = TEGRA186_SID_XUSB_DEV,
		.regs = {
			.override = 0x268,
			.security = 0x26c,
		},
	}, {
		.name = "tsecsrd",
		.sid = TEGRA186_SID_TSEC,
		.regs = {
			.override = 0x2a0,
			.security = 0x2a4,
		},
	}, {
		.name = "tsecswr",
		.sid = TEGRA186_SID_TSEC,
		.regs = {
			.override = 0x2a8,
			.security = 0x2ac,
		},
	}, {
		.name = "gpusrd",
		.sid = TEGRA186_SID_GPU,
		.regs = {
			.override = 0x2c0,
			.security = 0x2c4,
		},
	}, {
		.name = "gpuswr",
		.sid = TEGRA186_SID_GPU,
		.regs = {
			.override = 0x2c8,
			.security = 0x2cc,
		},
	}, {
		.name = "sdmmcra",
		.sid = TEGRA186_SID_SDMMC1,
		.regs = {
			.override = 0x300,
			.security = 0x304,
		},
	}, {
		.name = "sdmmcraa",
		.sid = TEGRA186_SID_SDMMC2,
		.regs = {
			.override = 0x308,
			.security = 0x30c,
		},
	}, {
		.name = "sdmmcr",
		.sid = TEGRA186_SID_SDMMC3,
		.regs = {
			.override = 0x310,
			.security = 0x314,
		},
	}, {
		.name = "sdmmcrab",
		.sid = TEGRA186_SID_SDMMC4,
		.regs = {
			.override = 0x318,
			.security = 0x31c,
		},
	}, {
		.name = "sdmmcwa",
		.sid = TEGRA186_SID_SDMMC1,
		.regs = {
			.override = 0x320,
			.security = 0x324,
		},
	}, {
		.name = "sdmmcwaa",
		.sid = TEGRA186_SID_SDMMC2,
		.regs = {
			.override = 0x328,
			.security = 0x32c,
		},
	}, {
		.name = "sdmmcw",
		.sid = TEGRA186_SID_SDMMC3,
		.regs = {
			.override = 0x330,
			.security = 0x334,
		},
	}, {
		.name = "sdmmcwab",
		.sid = TEGRA186_SID_SDMMC4,
		.regs = {
			.override = 0x338,
			.security = 0x33c,
		},
	}, {
		.name = "vicsrd",
		.sid = TEGRA186_SID_VIC,
		.regs = {
			.override = 0x360,
			.security = 0x364,
		},
	}, {
		.name = "vicswr",
		.sid = TEGRA186_SID_VIC,
		.regs = {
			.override = 0x368,
			.security = 0x36c,
		},
	}, {
		.name = "viw",
		.sid = TEGRA186_SID_VI,
		.regs = {
			.override = 0x390,
			.security = 0x394,
		},
	}, {
		.name = "nvdecsrd",
		.sid = TEGRA186_SID_NVDEC,
		.regs = {
			.override = 0x3c0,
			.security = 0x3c4,
		},
	}, {
		.name = "nvdecswr",
		.sid = TEGRA186_SID_NVDEC,
		.regs = {
			.override = 0x3c8,
			.security = 0x3cc,
		},
	}, {
		.name = "aper",
		.sid = TEGRA186_SID_APE,
		.regs = {
			.override = 0x3d0,
			.security = 0x3d4,
		},
	}, {
		.name = "apew",
		.sid = TEGRA186_SID_APE,
		.regs = {
			.override = 0x3d8,
			.security = 0x3dc,
		},
	}, {
		.name = "nvjpgsrd",
		.sid = TEGRA186_SID_NVJPG,
		.regs = {
			.override = 0x3f0,
			.security = 0x3f4,
		},
	}, {
		.name = "nvjpgswr",
		.sid = TEGRA186_SID_NVJPG,
		.regs = {
			.override = 0x3f8,
			.security = 0x3fc,
		},
	}, {
		.name = "sesrd",
		.sid = TEGRA186_SID_SE,
		.regs = {
			.override = 0x400,
			.security = 0x404,
		},
	}, {
		.name = "seswr",
		.sid = TEGRA186_SID_SE,
		.regs = {
			.override = 0x408,
			.security = 0x40c,
		},
	}, {
		.name = "etrr",
		.sid = TEGRA186_SID_ETR,
		.regs = {
			.override = 0x420,
			.security = 0x424,
		},
	}, {
		.name = "etrw",
		.sid = TEGRA186_SID_ETR,
		.regs = {
			.override = 0x428,
			.security = 0x42c,
		},
	}, {
		.name = "tsecsrdb",
		.sid = TEGRA186_SID_TSECB,
		.regs = {
			.override = 0x430,
			.security = 0x434,
		},
	}, {
		.name = "tsecswrb",
		.sid = TEGRA186_SID_TSECB,
		.regs = {
			.override = 0x438,
			.security = 0x43c,
		},
	}, {
		.name = "gpusrd2",
		.sid = TEGRA186_SID_GPU,
		.regs = {
			.override = 0x440,
			.security = 0x444,
		},
	}, {
		.name = "gpuswr2",
		.sid = TEGRA186_SID_GPU,
		.regs = {
			.override = 0x448,
			.security = 0x44c,
		},
	}, {
		.name = "axisr",
		.sid = TEGRA186_SID_GPCDMA_0,
		.regs = {
			.override = 0x460,
			.security = 0x464,
		},
	}, {
		.name = "axisw",
		.sid = TEGRA186_SID_GPCDMA_0,
		.regs = {
			.override = 0x468,
			.security = 0x46c,
		},
	}, {
		.name = "eqosr",
		.sid = TEGRA186_SID_EQOS,
		.regs = {
			.override = 0x470,
			.security = 0x474,
		},
	}, {
		.name = "eqosw",
		.sid = TEGRA186_SID_EQOS,
		.regs = {
			.override = 0x478,
			.security = 0x47c,
		},
	}, {
		.name = "ufshcr",
		.sid = TEGRA186_SID_UFSHC,
		.regs = {
			.override = 0x480,
			.security = 0x484,
		},
	}, {
		.name = "ufshcw",
		.sid = TEGRA186_SID_UFSHC,
		.regs = {
			.override = 0x488,
			.security = 0x48c,
		},
	}, {
		.name = "nvdisplayr",
		.sid = TEGRA186_SID_NVDISPLAY,
		.regs = {
			.override = 0x490,
			.security = 0x494,
		},
	}, {
		.name = "bpmpr",
		.sid = TEGRA186_SID_BPMP,
		.regs = {
			.override = 0x498,
			.security = 0x49c,
		},
	}, {
		.name = "bpmpw",
		.sid = TEGRA186_SID_BPMP,
		.regs = {
			.override = 0x4a0,
			.security = 0x4a4,
		},
	}, {
		.name = "bpmpdmar",
		.sid = TEGRA186_SID_BPMP,
		.regs = {
			.override = 0x4a8,
			.security = 0x4ac,
		},
	}, {
		.name = "bpmpdmaw",
		.sid = TEGRA186_SID_BPMP,
		.regs = {
			.override = 0x4b0,
			.security = 0x4b4,
		},
	}, {
		.name = "aonr",
		.sid = TEGRA186_SID_AON,
		.regs = {
			.override = 0x4b8,
			.security = 0x4bc,
		},
	}, {
		.name = "aonw",
		.sid = TEGRA186_SID_AON,
		.regs = {
			.override = 0x4c0,
			.security = 0x4c4,
		},
	}, {
		.name = "aondmar",
		.sid = TEGRA186_SID_AON,
		.regs = {
			.override = 0x4c8,
			.security = 0x4cc,
		},
	}, {
		.name = "aondmaw",
		.sid = TEGRA186_SID_AON,
		.regs = {
			.override = 0x4d0,
			.security = 0x4d4,
		},
	}, {
		.name = "scer",
		.sid = TEGRA186_SID_SCE,
		.regs = {
			.override = 0x4d8,
			.security = 0x4dc,
		},
	}, {
		.name = "scew",
		.sid = TEGRA186_SID_SCE,
		.regs = {
			.override = 0x4e0,
			.security = 0x4e4,
		},
	}, {
		.name = "scedmar",
		.sid = TEGRA186_SID_SCE,
		.regs = {
			.override = 0x4e8,
			.security = 0x4ec,
		},
	}, {
		.name = "scedmaw",
		.sid = TEGRA186_SID_SCE,
		.regs = {
			.override = 0x4f0,
			.security = 0x4f4,
		},
	}, {
		.name = "apedmar",
		.sid = TEGRA186_SID_APE,
		.regs = {
			.override = 0x4f8,
			.security = 0x4fc,
		},
	}, {
		.name = "apedmaw",
		.sid = TEGRA186_SID_APE,
		.regs = {
			.override = 0x500,
			.security = 0x504,
		},
	}, {
		.name = "nvdisplayr1",
		.sid = TEGRA186_SID_NVDISPLAY,
		.regs = {
			.override = 0x508,
			.security = 0x50c,
		},
	}, {
		.name = "vicsrd1",
		.sid = TEGRA186_SID_VIC,
		.regs = {
			.override = 0x510,
			.security = 0x514,
		},
	}, {
		.name = "nvdecsrd1",
		.sid = TEGRA186_SID_NVDEC,
		.regs = {
			.override = 0x518,
			.security = 0x51c,
		},
	},
};

static const struct tegra186_mc_soc tegra186_mc_soc = {
	.num_clients = ARRAY_SIZE(tegra186_mc_clients),
	.clients = tegra186_mc_clients,
};
#endif

#if defined(CONFIG_ARCH_TEGRA_194_SOC)
static const struct tegra186_mc_client tegra194_mc_clients[] = {
	{
		.name = "ptcr",
		.sid = TEGRA194_SID_PASSTHROUGH,
		.regs = {
			.override = 0x000,
			.security = 0x004,
		},
	}, {
		.name = "miu7r",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.override = 0x008,
			.security = 0x00c,
		},
	}, {
		.name = "miu7w",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.override = 0x010,
			.security = 0x014,
		},
	}, {
		.name = "hdar",
		.sid = TEGRA194_SID_HDA,
		.regs = {
			.override = 0x0a8,
			.security = 0x0ac,
		},
	}, {
		.name = "host1xdmar",
		.sid = TEGRA194_SID_HOST1X,
		.regs = {
			.override = 0x0b0,
			.security = 0x0b4,
		},
	}, {
		.name = "nvencsrd",
		.sid = TEGRA194_SID_NVENC,
		.regs = {
			.override = 0x0e0,
			.security = 0x0e4,
		},
	}, {
		.name = "satar",
		.sid = TEGRA194_SID_SATA,
		.regs = {
			.override = 0x0f8,
			.security = 0x0fc,
		},
	}, {
		.name = "mpcorer",
		.sid = TEGRA194_SID_PASSTHROUGH,
		.regs = {
			.override = 0x138,
			.security = 0x13c,
		},
	}, {
		.name = "nvencswr",
		.sid = TEGRA194_SID_NVENC,
		.regs = {
			.override = 0x158,
			.security = 0x15c,
		},
	}, {
		.name = "hdaw",
		.sid = TEGRA194_SID_HDA,
		.regs = {
			.override = 0x1a8,
			.security = 0x1ac,
		},
	}, {
		.name = "mpcorew",
		.sid = TEGRA194_SID_PASSTHROUGH,
		.regs = {
			.override = 0x1c8,
			.security = 0x1cc,
		},
	}, {
		.name = "sataw",
		.sid = TEGRA194_SID_SATA,
		.regs = {
			.override = 0x1e8,
			.security = 0x1ec,
		},
	}, {
		.name = "ispra",
		.sid = TEGRA194_SID_ISP,
		.regs = {
			.override = 0x220,
			.security = 0x224,
		},
	}, {
		.name = "ispfalr",
		.sid = TEGRA194_SID_ISP_FALCON,
		.regs = {
			.override = 0x228,
			.security = 0x22c,
		},
	}, {
		.name = "ispwa",
		.sid = TEGRA194_SID_ISP,
		.regs = {
			.override = 0x230,
			.security = 0x234,
		},
	}, {
		.name = "ispwb",
		.sid = TEGRA194_SID_ISP,
		.regs = {
			.override = 0x238,
			.security = 0x23c,
		},
	}, {
		.name = "xusb_hostr",
		.sid = TEGRA194_SID_XUSB_HOST,
		.regs = {
			.override = 0x250,
			.security = 0x254,
		},
	}, {
		.name = "xusb_hostw",
		.sid = TEGRA194_SID_XUSB_HOST,
		.regs = {
			.override = 0x258,
			.security = 0x25c,
		},
	}, {
		.name = "xusb_devr",
		.sid = TEGRA194_SID_XUSB_DEV,
		.regs = {
			.override = 0x260,
			.security = 0x264,
		},
	}, {
		.name = "xusb_devw",
		.sid = TEGRA194_SID_XUSB_DEV,
		.regs = {
			.override = 0x268,
			.security = 0x26c,
		},
	}, {
		.name = "sdmmcra",
		.sid = TEGRA194_SID_SDMMC1,
		.regs = {
			.override = 0x300,
			.security = 0x304,
		},
	}, {
		.name = "sdmmcr",
		.sid = TEGRA194_SID_SDMMC3,
		.regs = {
			.override = 0x310,
			.security = 0x314,
		},
	}, {
		.name = "sdmmcrab",
		.sid = TEGRA194_SID_SDMMC4,
		.regs = {
			.override = 0x318,
			.security = 0x31c,
		},
	}, {
		.name = "sdmmcwa",
		.sid = TEGRA194_SID_SDMMC1,
		.regs = {
			.override = 0x320,
			.security = 0x324,
		},
	}, {
		.name = "sdmmcw",
		.sid = TEGRA194_SID_SDMMC3,
		.regs = {
			.override = 0x330,
			.security = 0x334,
		},
	}, {
		.name = "sdmmcwab",
		.sid = TEGRA194_SID_SDMMC4,
		.regs = {
			.override = 0x338,
			.security = 0x33c,
		},
	}, {
		.name = "vicsrd",
		.sid = TEGRA194_SID_VIC,
		.regs = {
			.override = 0x360,
			.security = 0x364,
		},
	}, {
		.name = "vicswr",
		.sid = TEGRA194_SID_VIC,
		.regs = {
			.override = 0x368,
			.security = 0x36c,
		},
	}, {
		.name = "viw",
		.sid = TEGRA194_SID_VI,
		.regs = {
			.override = 0x390,
			.security = 0x394,
		},
	}, {
		.name = "nvdecsrd",
		.sid = TEGRA194_SID_NVDEC,
		.regs = {
			.override = 0x3c0,
			.security = 0x3c4,
		},
	}, {
		.name = "nvdecswr",
		.sid = TEGRA194_SID_NVDEC,
		.regs = {
			.override = 0x3c8,
			.security = 0x3cc,
		},
	}, {
		.name = "aper",
		.sid = TEGRA194_SID_APE,
		.regs = {
			.override = 0x3c0,
			.security = 0x3c4,
		},
	}, {
		.name = "apew",
		.sid = TEGRA194_SID_APE,
		.regs = {
			.override = 0x3d0,
			.security = 0x3d4,
		},
	}, {
		.name = "nvjpgsrd",
		.sid = TEGRA194_SID_NVJPG,
		.regs = {
			.override = 0x3f0,
			.security = 0x3f4,
		},
	}, {
		.name = "nvjpgswr",
		.sid = TEGRA194_SID_NVJPG,
		.regs = {
			.override = 0x3f0,
			.security = 0x3f4,
		},
	}, {
		.name = "axiapr",
		.sid = TEGRA194_SID_PASSTHROUGH,
		.regs = {
			.override = 0x410,
			.security = 0x414,
		},
	}, {
		.name = "axiapw",
		.sid = TEGRA194_SID_PASSTHROUGH,
		.regs = {
			.override = 0x418,
			.security = 0x41c,
		},
	}, {
		.name = "etrr",
		.sid = TEGRA194_SID_ETR,
		.regs = {
			.override = 0x420,
			.security = 0x424,
		},
	}, {
		.name = "etrw",
		.sid = TEGRA194_SID_ETR,
		.regs = {
			.override = 0x428,
			.security = 0x42c,
		},
	}, {
		.name = "axisr",
		.sid = TEGRA194_SID_PASSTHROUGH,
		.regs = {
			.override = 0x460,
			.security = 0x464,
		},
	}, {
		.name = "axisw",
		.sid = TEGRA194_SID_PASSTHROUGH,
		.regs = {
			.override = 0x468,
			.security = 0x46c,
		},
	}, {
		.name = "eqosr",
		.sid = TEGRA194_SID_EQOS,
		.regs = {
			.override = 0x470,
			.security = 0x474,
		},
	}, {
		.name = "eqosw",
		.sid = TEGRA194_SID_EQOS,
		.regs = {
			.override = 0x478,
			.security = 0x47c,
		},
	}, {
		.name = "ufshcr",
		.sid = TEGRA194_SID_UFSHC,
		.regs = {
			.override = 0x480,
			.security = 0x484,
		},
	}, {
		.name = "ufshcw",
		.sid = TEGRA194_SID_UFSHC,
		.regs = {
			.override = 0x488,
			.security = 0x48c,
		},
	}, {
		.name = "nvdisplayr",
		.sid = TEGRA194_SID_NVDISPLAY,
		.regs = {
			.override = 0x490,
			.security = 0x494,
		},
	}, {
		.name = "bpmpr",
		.sid = TEGRA194_SID_BPMP,
		.regs = {
			.override = 0x498,
			.security = 0x49c,
		},
	}, {
		.name = "bpmpw",
		.sid = TEGRA194_SID_BPMP,
		.regs = {
			.override = 0x4a0,
			.security = 0x4a4,
		},
	}, {
		.name = "bpmpdmar",
		.sid = TEGRA194_SID_BPMP,
		.regs = {
			.override = 0x4a8,
			.security = 0x4ac,
		},
	}, {
		.name = "bpmpdmaw",
		.sid = TEGRA194_SID_BPMP,
		.regs = {
			.override = 0x4b0,
			.security = 0x4b4,
		},
	}, {
		.name = "aonr",
		.sid = TEGRA194_SID_AON,
		.regs = {
			.override = 0x4b8,
			.security = 0x4bc,
		},
	}, {
		.name = "aonw",
		.sid = TEGRA194_SID_AON,
		.regs = {
			.override = 0x4c0,
			.security = 0x4c4,
		},
	}, {
		.name = "aondmar",
		.sid = TEGRA194_SID_AON,
		.regs = {
			.override = 0x4c8,
			.security = 0x4cc,
		},
	}, {
		.name = "aondmaw",
		.sid = TEGRA194_SID_AON,
		.regs = {
			.override = 0x4d0,
			.security = 0x4d4,
		},
	}, {
		.name = "scer",
		.sid = TEGRA194_SID_SCE,
		.regs = {
			.override = 0x4d8,
			.security = 0x4dc,
		},
	}, {
		.name = "scew",
		.sid = TEGRA194_SID_SCE,
		.regs = {
			.override = 0x4e0,
			.security = 0x4e4,
		},
	}, {
		.name = "scedmar",
		.sid = TEGRA194_SID_SCE,
		.regs = {
			.override = 0x4e8,
			.security = 0x4ec,
		},
	}, {
		.name = "scedmaw",
		.sid = TEGRA194_SID_SCE,
		.regs = {
			.override = 0x4f0,
			.security = 0x4f4,
		},
	}, {
		.name = "apedmar",
		.sid = TEGRA194_SID_APE,
		.regs = {
			.override = 0x4f8,
			.security = 0x4fc,
		},
	}, {
		.name = "apedmaw",
		.sid = TEGRA194_SID_APE,
		.regs = {
			.override = 0x500,
			.security = 0x504,
		},
	}, {
		.name = "nvdisplayr1",
		.sid = TEGRA194_SID_NVDISPLAY,
		.regs = {
			.override = 0x508,
			.security = 0x50c,
		},
	}, {
		.name = "vicsrd1",
		.sid = TEGRA194_SID_VIC,
		.regs = {
			.override = 0x510,
			.security = 0x514,
		},
	}, {
		.name = "nvdecsrd1",
		.sid = TEGRA194_SID_NVDEC,
		.regs = {
			.override = 0x518,
			.security = 0x51c,
		},
	}, {
		.name = "miu0r",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.override = 0x530,
			.security = 0x534,
		},
	}, {
		.name = "miu0w",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.override = 0x538,
			.security = 0x53c,
		},
	}, {
		.name = "miu1r",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.override = 0x540,
			.security = 0x544,
		},
	}, {
		.name = "miu1w",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.override = 0x548,
			.security = 0x54c,
		},
	}, {
		.name = "miu2r",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.override = 0x570,
			.security = 0x574,
		},
	}, {
		.name = "miu2w",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.override = 0x578,
			.security = 0x57c,
		},
	}, {
		.name = "miu3r",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.override = 0x580,
			.security = 0x584,
		},
	}, {
		.name = "miu3w",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.override = 0x588,
			.security = 0x58c,
		},
	}, {
		.name = "miu4r",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.override = 0x590,
			.security = 0x594,
		},
	}, {
		.name = "miu4w",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.override = 0x598,
			.security = 0x59c,
		},
	}, {
		.name = "dpmur",
		.sid = TEGRA194_SID_PASSTHROUGH,
		.regs = {
			.override = 0x598,
			.security = 0x59c,
		},
	}, {
		.name = "vifalr",
		.sid = TEGRA194_SID_VI_FALCON,
		.regs = {
			.override = 0x5e0,
			.security = 0x5e4,
		},
	}, {
		.name = "vifalw",
		.sid = TEGRA194_SID_VI_FALCON,
		.regs = {
			.override = 0x5e8,
			.security = 0x5ec,
		},
	}, {
		.name = "dla0rda",
		.sid = TEGRA194_SID_NVDLA0,
		.regs = {
			.override = 0x5f0,
			.security = 0x5f4,
		},
	}, {
		.name = "dla0falrdb",
		.sid = TEGRA194_SID_NVDLA0,
		.regs = {
			.override = 0x5f8,
			.security = 0x5fc,
		},
	}, {
		.name = "dla0wra",
		.sid = TEGRA194_SID_NVDLA0,
		.regs = {
			.override = 0x600,
			.security = 0x604,
		},
	}, {
		.name = "dla0falwrb",
		.sid = TEGRA194_SID_NVDLA0,
		.regs = {
			.override = 0x608,
			.security = 0x60c,
		},
	}, {
		.name = "dla1rda",
		.sid = TEGRA194_SID_NVDLA1,
		.regs = {
			.override = 0x610,
			.security = 0x614,
		},
	}, {
		.name = "dla1falrdb",
		.sid = TEGRA194_SID_NVDLA1,
		.regs = {
			.override = 0x618,
			.security = 0x61c,
		},
	}, {
		.name = "dla1wra",
		.sid = TEGRA194_SID_NVDLA1,
		.regs = {
			.override = 0x620,
			.security = 0x624,
		},
	}, {
		.name = "dla1falwrb",
		.sid = TEGRA194_SID_NVDLA1,
		.regs = {
			.override = 0x628,
			.security = 0x62c,
		},
	}, {
		.name = "pva0rda",
		.sid = TEGRA194_SID_PVA0,
		.regs = {
			.override = 0x630,
			.security = 0x634,
		},
	}, {
		.name = "pva0rdb",
		.sid = TEGRA194_SID_PVA0,
		.regs = {
			.override = 0x638,
			.security = 0x63c,
		},
	}, {
		.name = "pva0rdc",
		.sid = TEGRA194_SID_PVA0,
		.regs = {
			.override = 0x640,
			.security = 0x644,
		},
	}, {
		.name = "pva0wra",
		.sid = TEGRA194_SID_PVA0,
		.regs = {
			.override = 0x648,
			.security = 0x64c,
		},
	}, {
		.name = "pva0wrb",
		.sid = TEGRA194_SID_PVA0,
		.regs = {
			.override = 0x650,
			.security = 0x654,
		},
	}, {
		.name = "pva0wrc",
		.sid = TEGRA194_SID_PVA0,
		.regs = {
			.override = 0x658,
			.security = 0x65c,
		},
	}, {
		.name = "pva1rda",
		.sid = TEGRA194_SID_PVA1,
		.regs = {
			.override = 0x660,
			.security = 0x664,
		},
	}, {
		.name = "pva1rdb",
		.sid = TEGRA194_SID_PVA1,
		.regs = {
			.override = 0x668,
			.security = 0x66c,
		},
	}, {
		.name = "pva1rdc",
		.sid = TEGRA194_SID_PVA1,
		.regs = {
			.override = 0x670,
			.security = 0x674,
		},
	}, {
		.name = "pva1wra",
		.sid = TEGRA194_SID_PVA1,
		.regs = {
			.override = 0x678,
			.security = 0x67c,
		},
	}, {
		.name = "pva1wrb",
		.sid = TEGRA194_SID_PVA1,
		.regs = {
			.override = 0x680,
			.security = 0x684,
		},
	}, {
		.name = "pva1wrc",
		.sid = TEGRA194_SID_PVA1,
		.regs = {
			.override = 0x688,
			.security = 0x68c,
		},
	}, {
		.name = "rcer",
		.sid = TEGRA194_SID_RCE,
		.regs = {
			.override = 0x690,
			.security = 0x694,
		},
	}, {
		.name = "rcew",
		.sid = TEGRA194_SID_RCE,
		.regs = {
			.override = 0x698,
			.security = 0x69c,
		},
	}, {
		.name = "rcedmar",
		.sid = TEGRA194_SID_RCE,
		.regs = {
			.override = 0x6a0,
			.security = 0x6a4,
		},
	}, {
		.name = "rcedmaw",
		.sid = TEGRA194_SID_RCE,
		.regs = {
			.override = 0x6a8,
			.security = 0x6ac,
		},
	}, {
		.name = "nvenc1srd",
		.sid = TEGRA194_SID_NVENC1,
		.regs = {
			.override = 0x6b0,
			.security = 0x6b4,
		},
	}, {
		.name = "nvenc1swr",
		.sid = TEGRA194_SID_NVENC1,
		.regs = {
			.override = 0x6b8,
			.security = 0x6bc,
		},
	}, {
		.name = "pcie0r",
		.sid = TEGRA194_SID_PCIE0,
		.regs = {
			.override = 0x6c0,
			.security = 0x6c4,
		},
	}, {
		.name = "pcie0w",
		.sid = TEGRA194_SID_PCIE0,
		.regs = {
			.override = 0x6c8,
			.security = 0x6cc,
		},
	}, {
		.name = "pcie1r",
		.sid = TEGRA194_SID_PCIE1,
		.regs = {
			.override = 0x6d0,
			.security = 0x6d4,
		},
	}, {
		.name = "pcie1w",
		.sid = TEGRA194_SID_PCIE1,
		.regs = {
			.override = 0x6d8,
			.security = 0x6dc,
		},
	}, {
		.name = "pcie2ar",
		.sid = TEGRA194_SID_PCIE2,
		.regs = {
			.override = 0x6e0,
			.security = 0x6e4,
		},
	}, {
		.name = "pcie2aw",
		.sid = TEGRA194_SID_PCIE2,
		.regs = {
			.override = 0x6e8,
			.security = 0x6ec,
		},
	}, {
		.name = "pcie3r",
		.sid = TEGRA194_SID_PCIE3,
		.regs = {
			.override = 0x6f0,
			.security = 0x6f4,
		},
	}, {
		.name = "pcie3w",
		.sid = TEGRA194_SID_PCIE3,
		.regs = {
			.override = 0x6f8,
			.security = 0x6fc,
		},
	}, {
		.name = "pcie4r",
		.sid = TEGRA194_SID_PCIE4,
		.regs = {
			.override = 0x700,
			.security = 0x704,
		},
	}, {
		.name = "pcie4w",
		.sid = TEGRA194_SID_PCIE4,
		.regs = {
			.override = 0x708,
			.security = 0x70c,
		},
	}, {
		.name = "pcie5r",
		.sid = TEGRA194_SID_PCIE5,
		.regs = {
			.override = 0x710,
			.security = 0x714,
		},
	}, {
		.name = "pcie5w",
		.sid = TEGRA194_SID_PCIE5,
		.regs = {
			.override = 0x718,
			.security = 0x71c,
		},
	}, {
		.name = "ispfalw",
		.sid = TEGRA194_SID_ISP_FALCON,
		.regs = {
			.override = 0x720,
			.security = 0x724,
		},
	}, {
		.name = "dla0rda1",
		.sid = TEGRA194_SID_NVDLA0,
		.regs = {
			.override = 0x748,
			.security = 0x74c,
		},
	}, {
		.name = "dla1rda1",
		.sid = TEGRA194_SID_NVDLA1,
		.regs = {
			.override = 0x750,
			.security = 0x754,
		},
	}, {
		.name = "pva0rda1",
		.sid = TEGRA194_SID_PVA0,
		.regs = {
			.override = 0x758,
			.security = 0x75c,
		},
	}, {
		.name = "pva0rdb1",
		.sid = TEGRA194_SID_PVA0,
		.regs = {
			.override = 0x760,
			.security = 0x764,
		},
	}, {
		.name = "pva1rda1",
		.sid = TEGRA194_SID_PVA1,
		.regs = {
			.override = 0x768,
			.security = 0x76c,
		},
	}, {
		.name = "pva1rdb1",
		.sid = TEGRA194_SID_PVA1,
		.regs = {
			.override = 0x770,
			.security = 0x774,
		},
	}, {
		.name = "pcie5r1",
		.sid = TEGRA194_SID_PCIE5,
		.regs = {
			.override = 0x778,
			.security = 0x77c,
		},
	}, {
		.name = "nvencsrd1",
		.sid = TEGRA194_SID_NVENC,
		.regs = {
			.override = 0x780,
			.security = 0x784,
		},
	}, {
		.name = "nvenc1srd1",
		.sid = TEGRA194_SID_NVENC1,
		.regs = {
			.override = 0x788,
			.security = 0x78c,
		},
	}, {
		.name = "ispra1",
		.sid = TEGRA194_SID_ISP,
		.regs = {
			.override = 0x790,
			.security = 0x794,
		},
	}, {
		.name = "pcie0r1",
		.sid = TEGRA194_SID_PCIE0,
		.regs = {
			.override = 0x798,
			.security = 0x79c,
		},
	}, {
		.name = "nvdec1srd",
		.sid = TEGRA194_SID_NVDEC1,
		.regs = {
			.override = 0x7c8,
			.security = 0x7cc,
		},
	}, {
		.name = "nvdec1srd1",
		.sid = TEGRA194_SID_NVDEC1,
		.regs = {
			.override = 0x7d0,
			.security = 0x7d4,
		},
	}, {
		.name = "nvdec1swr",
		.sid = TEGRA194_SID_NVDEC1,
		.regs = {
			.override = 0x7d8,
			.security = 0x7dc,
		},
	}, {
		.name = "miu5r",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.override = 0x7e0,
			.security = 0x7e4,
		},
	}, {
		.name = "miu5w",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.override = 0x7e8,
			.security = 0x7ec,
		},
	}, {
		.name = "miu6r",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.override = 0x7f0,
			.security = 0x7f4,
		},
	}, {
		.name = "miu6w",
		.sid = TEGRA194_SID_MIU,
		.regs = {
			.override = 0x7f8,
			.security = 0x7fc,
		},
	},
};

static const struct tegra186_mc_soc tegra194_mc_soc = {
	.num_clients = ARRAY_SIZE(tegra194_mc_clients),
	.clients = tegra194_mc_clients,
};
#endif

static int tegra186_mc_probe(struct platform_device *pdev)
{
	struct tegra186_mc *mc;
	struct resource *res;
	int err;

	mc = devm_kzalloc(&pdev->dev, sizeof(*mc), GFP_KERNEL);
	if (!mc)
		return -ENOMEM;

	mc->soc = of_device_get_match_data(&pdev->dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mc->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mc->regs))
		return PTR_ERR(mc->regs);

	mc->dev = &pdev->dev;

	err = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (err < 0)
		return err;

	platform_set_drvdata(pdev, mc);
	tegra186_mc_program_sid(mc);

	return 0;
}

static int tegra186_mc_remove(struct platform_device *pdev)
{
	struct tegra186_mc *mc = platform_get_drvdata(pdev);

	of_platform_depopulate(mc->dev);

	return 0;
}

static const struct of_device_id tegra186_mc_of_match[] = {
#if defined(CONFIG_ARCH_TEGRA_186_SOC)
	{ .compatible = "nvidia,tegra186-mc", .data = &tegra186_mc_soc },
#endif
#if defined(CONFIG_ARCH_TEGRA_194_SOC)
	{ .compatible = "nvidia,tegra194-mc", .data = &tegra194_mc_soc },
#endif
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tegra186_mc_of_match);

static int __maybe_unused tegra186_mc_suspend(struct device *dev)
{
	return 0;
}

static int __maybe_unused tegra186_mc_resume(struct device *dev)
{
	struct tegra186_mc *mc = dev_get_drvdata(dev);

	tegra186_mc_program_sid(mc);

	return 0;
}

static const struct dev_pm_ops tegra186_mc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(tegra186_mc_suspend, tegra186_mc_resume)
};

static struct platform_driver tegra186_mc_driver = {
	.driver = {
		.name = "tegra186-mc",
		.of_match_table = tegra186_mc_of_match,
		.pm = &tegra186_mc_pm_ops,
		.suppress_bind_attrs = true,
	},
	.probe = tegra186_mc_probe,
	.remove = tegra186_mc_remove,
};
module_platform_driver(tegra186_mc_driver);

MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra186 Memory Controller driver");
MODULE_LICENSE("GPL v2");
