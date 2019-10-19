// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

#include <dt-bindings/memory/tegra186-mc.h>

struct tegra_mc {
	struct device *dev;
	void __iomem *regs;
};

struct tegra_mc_client {
	const char *name;
	unsigned int sid;
	struct {
		unsigned int override;
		unsigned int security;
	} regs;
};

static const struct tegra_mc_client tegra186_mc_clients[] = {
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

static int tegra186_mc_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct tegra_mc *mc;
	unsigned int i;
	int err = 0;

	mc = devm_kzalloc(&pdev->dev, sizeof(*mc), GFP_KERNEL);
	if (!mc)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mc->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mc->regs))
		return PTR_ERR(mc->regs);

	mc->dev = &pdev->dev;

	for (i = 0; i < ARRAY_SIZE(tegra186_mc_clients); i++) {
		const struct tegra_mc_client *client = &tegra186_mc_clients[i];
		u32 override, security;

		override = readl(mc->regs + client->regs.override);
		security = readl(mc->regs + client->regs.security);

		dev_dbg(&pdev->dev, "client %s: override: %x security: %x\n",
			client->name, override, security);

		dev_dbg(&pdev->dev, "setting SID %u for %s\n", client->sid,
			client->name);
		writel(client->sid, mc->regs + client->regs.override);

		override = readl(mc->regs + client->regs.override);
		security = readl(mc->regs + client->regs.security);

		dev_dbg(&pdev->dev, "client %s: override: %x security: %x\n",
			client->name, override, security);
	}

	platform_set_drvdata(pdev, mc);

	return err;
}

static const struct of_device_id tegra186_mc_of_match[] = {
	{ .compatible = "nvidia,tegra186-mc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tegra186_mc_of_match);

static struct platform_driver tegra186_mc_driver = {
	.driver = {
		.name = "tegra186-mc",
		.of_match_table = tegra186_mc_of_match,
		.suppress_bind_attrs = true,
	},
	.prevent_deferred_probe = true,
	.probe = tegra186_mc_probe,
};
module_platform_driver(tegra186_mc_driver);

MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra186 Memory Controller driver");
MODULE_LICENSE("GPL v2");
