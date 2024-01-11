// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2021 NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <soc/tegra/mc.h>

#if defined(CONFIG_ARCH_TEGRA_186_SOC)
#include <dt-bindings/memory/tegra186-mc.h>
#endif

#include "mc.h"

#define MC_SID_STREAMID_OVERRIDE_MASK GENMASK(7, 0)
#define MC_SID_STREAMID_SECURITY_WRITE_ACCESS_DISABLED BIT(16)
#define MC_SID_STREAMID_SECURITY_OVERRIDE BIT(8)

static int tegra186_mc_probe(struct tegra_mc *mc)
{
	struct platform_device *pdev = to_platform_device(mc->dev);
	unsigned int i;
	char name[8];
	int err;

	mc->bcast_ch_regs = devm_platform_ioremap_resource_byname(pdev, "broadcast");
	if (IS_ERR(mc->bcast_ch_regs)) {
		if (PTR_ERR(mc->bcast_ch_regs) == -EINVAL) {
			dev_warn(&pdev->dev,
				 "Broadcast channel is missing, please update your device-tree\n");
			mc->bcast_ch_regs = NULL;
			goto populate;
		}

		return PTR_ERR(mc->bcast_ch_regs);
	}

	mc->ch_regs = devm_kcalloc(mc->dev, mc->soc->num_channels, sizeof(*mc->ch_regs),
				   GFP_KERNEL);
	if (!mc->ch_regs)
		return -ENOMEM;

	for (i = 0; i < mc->soc->num_channels; i++) {
		snprintf(name, sizeof(name), "ch%u", i);

		mc->ch_regs[i] = devm_platform_ioremap_resource_byname(pdev, name);
		if (IS_ERR(mc->ch_regs[i]))
			return PTR_ERR(mc->ch_regs[i]);
	}

populate:
	err = of_platform_populate(mc->dev->of_node, NULL, NULL, mc->dev);
	if (err < 0)
		return err;

	return 0;
}

static void tegra186_mc_remove(struct tegra_mc *mc)
{
	of_platform_depopulate(mc->dev);
}

#if IS_ENABLED(CONFIG_IOMMU_API)
static void tegra186_mc_client_sid_override(struct tegra_mc *mc,
					    const struct tegra_mc_client *client,
					    unsigned int sid)
{
	u32 value, old;

	if (client->regs.sid.security == 0 && client->regs.sid.override == 0)
		return;

	value = readl(mc->regs + client->regs.sid.security);
	if ((value & MC_SID_STREAMID_SECURITY_OVERRIDE) == 0) {
		/*
		 * If the secure firmware has locked this down the override
		 * for this memory client, there's nothing we can do here.
		 */
		if (value & MC_SID_STREAMID_SECURITY_WRITE_ACCESS_DISABLED)
			return;

		/*
		 * Otherwise, try to set the override itself. Typically the
		 * secure firmware will never have set this configuration.
		 * Instead, it will either have disabled write access to
		 * this field, or it will already have set an explicit
		 * override itself.
		 */
		WARN_ON((value & MC_SID_STREAMID_SECURITY_OVERRIDE) == 0);

		value |= MC_SID_STREAMID_SECURITY_OVERRIDE;
		writel(value, mc->regs + client->regs.sid.security);
	}

	value = readl(mc->regs + client->regs.sid.override);
	old = value & MC_SID_STREAMID_OVERRIDE_MASK;

	if (old != sid) {
		dev_dbg(mc->dev, "overriding SID %x for %s with %x\n", old,
			client->name, sid);
		writel(sid, mc->regs + client->regs.sid.override);
	}
}
#endif

static int tegra186_mc_probe_device(struct tegra_mc *mc, struct device *dev)
{
#if IS_ENABLED(CONFIG_IOMMU_API)
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct of_phandle_args args;
	unsigned int i, index = 0;

	while (!of_parse_phandle_with_args(dev->of_node, "interconnects", "#interconnect-cells",
					   index, &args)) {
		if (args.np == mc->dev->of_node && args.args_count != 0) {
			for (i = 0; i < mc->soc->num_clients; i++) {
				const struct tegra_mc_client *client = &mc->soc->clients[i];

				if (client->id == args.args[0]) {
					u32 sid = fwspec->ids[0] & MC_SID_STREAMID_OVERRIDE_MASK;

					tegra186_mc_client_sid_override(mc, client, sid);
				}
			}
		}

		index++;
	}
#endif

	return 0;
}

static int tegra186_mc_resume(struct tegra_mc *mc)
{
#if IS_ENABLED(CONFIG_IOMMU_API)
	unsigned int i;

	for (i = 0; i < mc->soc->num_clients; i++) {
		const struct tegra_mc_client *client = &mc->soc->clients[i];

		tegra186_mc_client_sid_override(mc, client, client->sid);
	}
#endif

	return 0;
}

const struct tegra_mc_ops tegra186_mc_ops = {
	.probe = tegra186_mc_probe,
	.remove = tegra186_mc_remove,
	.resume = tegra186_mc_resume,
	.probe_device = tegra186_mc_probe_device,
	.handle_irq = tegra30_mc_handle_irq,
};

#if defined(CONFIG_ARCH_TEGRA_186_SOC)
static const struct tegra_mc_client tegra186_mc_clients[] = {
	{
		.id = TEGRA186_MEMORY_CLIENT_PTCR,
		.name = "ptcr",
		.sid = TEGRA186_SID_PASSTHROUGH,
		.regs = {
			.sid = {
				.override = 0x000,
				.security = 0x004,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_AFIR,
		.name = "afir",
		.sid = TEGRA186_SID_AFI,
		.regs = {
			.sid = {
				.override = 0x070,
				.security = 0x074,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_HDAR,
		.name = "hdar",
		.sid = TEGRA186_SID_HDA,
		.regs = {
			.sid = {
				.override = 0x0a8,
				.security = 0x0ac,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_HOST1XDMAR,
		.name = "host1xdmar",
		.sid = TEGRA186_SID_HOST1X,
		.regs = {
			.sid = {
				.override = 0x0b0,
				.security = 0x0b4,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_NVENCSRD,
		.name = "nvencsrd",
		.sid = TEGRA186_SID_NVENC,
		.regs = {
			.sid = {
				.override = 0x0e0,
				.security = 0x0e4,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_SATAR,
		.name = "satar",
		.sid = TEGRA186_SID_SATA,
		.regs = {
			.sid = {
				.override = 0x0f8,
				.security = 0x0fc,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_MPCORER,
		.name = "mpcorer",
		.sid = TEGRA186_SID_PASSTHROUGH,
		.regs = {
			.sid = {
				.override = 0x138,
				.security = 0x13c,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_NVENCSWR,
		.name = "nvencswr",
		.sid = TEGRA186_SID_NVENC,
		.regs = {
			.sid = {
				.override = 0x158,
				.security = 0x15c,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_AFIW,
		.name = "afiw",
		.sid = TEGRA186_SID_AFI,
		.regs = {
			.sid = {
				.override = 0x188,
				.security = 0x18c,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_HDAW,
		.name = "hdaw",
		.sid = TEGRA186_SID_HDA,
		.regs = {
			.sid = {
				.override = 0x1a8,
				.security = 0x1ac,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_MPCOREW,
		.name = "mpcorew",
		.sid = TEGRA186_SID_PASSTHROUGH,
		.regs = {
			.sid = {
				.override = 0x1c8,
				.security = 0x1cc,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_SATAW,
		.name = "sataw",
		.sid = TEGRA186_SID_SATA,
		.regs = {
			.sid = {
				.override = 0x1e8,
				.security = 0x1ec,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_ISPRA,
		.name = "ispra",
		.sid = TEGRA186_SID_ISP,
		.regs = {
			.sid = {
				.override = 0x220,
				.security = 0x224,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_ISPWA,
		.name = "ispwa",
		.sid = TEGRA186_SID_ISP,
		.regs = {
			.sid = {
				.override = 0x230,
				.security = 0x234,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_ISPWB,
		.name = "ispwb",
		.sid = TEGRA186_SID_ISP,
		.regs = {
			.sid = {
				.override = 0x238,
				.security = 0x23c,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_XUSB_HOSTR,
		.name = "xusb_hostr",
		.sid = TEGRA186_SID_XUSB_HOST,
		.regs = {
			.sid = {
				.override = 0x250,
				.security = 0x254,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_XUSB_HOSTW,
		.name = "xusb_hostw",
		.sid = TEGRA186_SID_XUSB_HOST,
		.regs = {
			.sid = {
				.override = 0x258,
				.security = 0x25c,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_XUSB_DEVR,
		.name = "xusb_devr",
		.sid = TEGRA186_SID_XUSB_DEV,
		.regs = {
			.sid = {
				.override = 0x260,
				.security = 0x264,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_XUSB_DEVW,
		.name = "xusb_devw",
		.sid = TEGRA186_SID_XUSB_DEV,
		.regs = {
			.sid = {
				.override = 0x268,
				.security = 0x26c,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_TSECSRD,
		.name = "tsecsrd",
		.sid = TEGRA186_SID_TSEC,
		.regs = {
			.sid = {
				.override = 0x2a0,
				.security = 0x2a4,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_TSECSWR,
		.name = "tsecswr",
		.sid = TEGRA186_SID_TSEC,
		.regs = {
			.sid = {
				.override = 0x2a8,
				.security = 0x2ac,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_GPUSRD,
		.name = "gpusrd",
		.sid = TEGRA186_SID_GPU,
		.regs = {
			.sid = {
				.override = 0x2c0,
				.security = 0x2c4,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_GPUSWR,
		.name = "gpuswr",
		.sid = TEGRA186_SID_GPU,
		.regs = {
			.sid = {
				.override = 0x2c8,
				.security = 0x2cc,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_SDMMCRA,
		.name = "sdmmcra",
		.sid = TEGRA186_SID_SDMMC1,
		.regs = {
			.sid = {
				.override = 0x300,
				.security = 0x304,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_SDMMCRAA,
		.name = "sdmmcraa",
		.sid = TEGRA186_SID_SDMMC2,
		.regs = {
			.sid = {
				.override = 0x308,
				.security = 0x30c,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_SDMMCR,
		.name = "sdmmcr",
		.sid = TEGRA186_SID_SDMMC3,
		.regs = {
			.sid = {
				.override = 0x310,
				.security = 0x314,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_SDMMCRAB,
		.name = "sdmmcrab",
		.sid = TEGRA186_SID_SDMMC4,
		.regs = {
			.sid = {
				.override = 0x318,
				.security = 0x31c,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_SDMMCWA,
		.name = "sdmmcwa",
		.sid = TEGRA186_SID_SDMMC1,
		.regs = {
			.sid = {
				.override = 0x320,
				.security = 0x324,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_SDMMCWAA,
		.name = "sdmmcwaa",
		.sid = TEGRA186_SID_SDMMC2,
		.regs = {
			.sid = {
				.override = 0x328,
				.security = 0x32c,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_SDMMCW,
		.name = "sdmmcw",
		.sid = TEGRA186_SID_SDMMC3,
		.regs = {
			.sid = {
				.override = 0x330,
				.security = 0x334,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_SDMMCWAB,
		.name = "sdmmcwab",
		.sid = TEGRA186_SID_SDMMC4,
		.regs = {
			.sid = {
				.override = 0x338,
				.security = 0x33c,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_VICSRD,
		.name = "vicsrd",
		.sid = TEGRA186_SID_VIC,
		.regs = {
			.sid = {
				.override = 0x360,
				.security = 0x364,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_VICSWR,
		.name = "vicswr",
		.sid = TEGRA186_SID_VIC,
		.regs = {
			.sid = {
				.override = 0x368,
				.security = 0x36c,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_VIW,
		.name = "viw",
		.sid = TEGRA186_SID_VI,
		.regs = {
			.sid = {
				.override = 0x390,
				.security = 0x394,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_NVDECSRD,
		.name = "nvdecsrd",
		.sid = TEGRA186_SID_NVDEC,
		.regs = {
			.sid = {
				.override = 0x3c0,
				.security = 0x3c4,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_NVDECSWR,
		.name = "nvdecswr",
		.sid = TEGRA186_SID_NVDEC,
		.regs = {
			.sid = {
				.override = 0x3c8,
				.security = 0x3cc,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_APER,
		.name = "aper",
		.sid = TEGRA186_SID_APE,
		.regs = {
			.sid = {
				.override = 0x3d0,
				.security = 0x3d4,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_APEW,
		.name = "apew",
		.sid = TEGRA186_SID_APE,
		.regs = {
			.sid = {
				.override = 0x3d8,
				.security = 0x3dc,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_NVJPGSRD,
		.name = "nvjpgsrd",
		.sid = TEGRA186_SID_NVJPG,
		.regs = {
			.sid = {
				.override = 0x3f0,
				.security = 0x3f4,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_NVJPGSWR,
		.name = "nvjpgswr",
		.sid = TEGRA186_SID_NVJPG,
		.regs = {
			.sid = {
				.override = 0x3f8,
				.security = 0x3fc,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_SESRD,
		.name = "sesrd",
		.sid = TEGRA186_SID_SE,
		.regs = {
			.sid = {
				.override = 0x400,
				.security = 0x404,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_SESWR,
		.name = "seswr",
		.sid = TEGRA186_SID_SE,
		.regs = {
			.sid = {
				.override = 0x408,
				.security = 0x40c,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_ETRR,
		.name = "etrr",
		.sid = TEGRA186_SID_ETR,
		.regs = {
			.sid = {
				.override = 0x420,
				.security = 0x424,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_ETRW,
		.name = "etrw",
		.sid = TEGRA186_SID_ETR,
		.regs = {
			.sid = {
				.override = 0x428,
				.security = 0x42c,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_TSECSRDB,
		.name = "tsecsrdb",
		.sid = TEGRA186_SID_TSECB,
		.regs = {
			.sid = {
				.override = 0x430,
				.security = 0x434,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_TSECSWRB,
		.name = "tsecswrb",
		.sid = TEGRA186_SID_TSECB,
		.regs = {
			.sid = {
				.override = 0x438,
				.security = 0x43c,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_GPUSRD2,
		.name = "gpusrd2",
		.sid = TEGRA186_SID_GPU,
		.regs = {
			.sid = {
				.override = 0x440,
				.security = 0x444,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_GPUSWR2,
		.name = "gpuswr2",
		.sid = TEGRA186_SID_GPU,
		.regs = {
			.sid = {
				.override = 0x448,
				.security = 0x44c,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_AXISR,
		.name = "axisr",
		.sid = TEGRA186_SID_GPCDMA_0,
		.regs = {
			.sid = {
				.override = 0x460,
				.security = 0x464,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_AXISW,
		.name = "axisw",
		.sid = TEGRA186_SID_GPCDMA_0,
		.regs = {
			.sid = {
				.override = 0x468,
				.security = 0x46c,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_EQOSR,
		.name = "eqosr",
		.sid = TEGRA186_SID_EQOS,
		.regs = {
			.sid = {
				.override = 0x470,
				.security = 0x474,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_EQOSW,
		.name = "eqosw",
		.sid = TEGRA186_SID_EQOS,
		.regs = {
			.sid = {
				.override = 0x478,
				.security = 0x47c,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_UFSHCR,
		.name = "ufshcr",
		.sid = TEGRA186_SID_UFSHC,
		.regs = {
			.sid = {
				.override = 0x480,
				.security = 0x484,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_UFSHCW,
		.name = "ufshcw",
		.sid = TEGRA186_SID_UFSHC,
		.regs = {
			.sid = {
				.override = 0x488,
				.security = 0x48c,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_NVDISPLAYR,
		.name = "nvdisplayr",
		.sid = TEGRA186_SID_NVDISPLAY,
		.regs = {
			.sid = {
				.override = 0x490,
				.security = 0x494,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_BPMPR,
		.name = "bpmpr",
		.sid = TEGRA186_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x498,
				.security = 0x49c,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_BPMPW,
		.name = "bpmpw",
		.sid = TEGRA186_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x4a0,
				.security = 0x4a4,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_BPMPDMAR,
		.name = "bpmpdmar",
		.sid = TEGRA186_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x4a8,
				.security = 0x4ac,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_BPMPDMAW,
		.name = "bpmpdmaw",
		.sid = TEGRA186_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x4b0,
				.security = 0x4b4,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_AONR,
		.name = "aonr",
		.sid = TEGRA186_SID_AON,
		.regs = {
			.sid = {
				.override = 0x4b8,
				.security = 0x4bc,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_AONW,
		.name = "aonw",
		.sid = TEGRA186_SID_AON,
		.regs = {
			.sid = {
				.override = 0x4c0,
				.security = 0x4c4,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_AONDMAR,
		.name = "aondmar",
		.sid = TEGRA186_SID_AON,
		.regs = {
			.sid = {
				.override = 0x4c8,
				.security = 0x4cc,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_AONDMAW,
		.name = "aondmaw",
		.sid = TEGRA186_SID_AON,
		.regs = {
			.sid = {
				.override = 0x4d0,
				.security = 0x4d4,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_SCER,
		.name = "scer",
		.sid = TEGRA186_SID_SCE,
		.regs = {
			.sid = {
				.override = 0x4d8,
				.security = 0x4dc,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_SCEW,
		.name = "scew",
		.sid = TEGRA186_SID_SCE,
		.regs = {
			.sid = {
				.override = 0x4e0,
				.security = 0x4e4,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_SCEDMAR,
		.name = "scedmar",
		.sid = TEGRA186_SID_SCE,
		.regs = {
			.sid = {
				.override = 0x4e8,
				.security = 0x4ec,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_SCEDMAW,
		.name = "scedmaw",
		.sid = TEGRA186_SID_SCE,
		.regs = {
			.sid = {
				.override = 0x4f0,
				.security = 0x4f4,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_APEDMAR,
		.name = "apedmar",
		.sid = TEGRA186_SID_APE,
		.regs = {
			.sid = {
				.override = 0x4f8,
				.security = 0x4fc,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_APEDMAW,
		.name = "apedmaw",
		.sid = TEGRA186_SID_APE,
		.regs = {
			.sid = {
				.override = 0x500,
				.security = 0x504,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_NVDISPLAYR1,
		.name = "nvdisplayr1",
		.sid = TEGRA186_SID_NVDISPLAY,
		.regs = {
			.sid = {
				.override = 0x508,
				.security = 0x50c,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_VICSRD1,
		.name = "vicsrd1",
		.sid = TEGRA186_SID_VIC,
		.regs = {
			.sid = {
				.override = 0x510,
				.security = 0x514,
			},
		},
	}, {
		.id = TEGRA186_MEMORY_CLIENT_NVDECSRD1,
		.name = "nvdecsrd1",
		.sid = TEGRA186_SID_NVDEC,
		.regs = {
			.sid = {
				.override = 0x518,
				.security = 0x51c,
			},
		},
	},
};

const struct tegra_mc_soc tegra186_mc_soc = {
	.num_clients = ARRAY_SIZE(tegra186_mc_clients),
	.clients = tegra186_mc_clients,
	.num_address_bits = 40,
	.num_channels = 4,
	.client_id_mask = 0xff,
	.intmask = MC_INT_DECERR_GENERALIZED_CARVEOUT | MC_INT_DECERR_MTS |
		   MC_INT_SECERR_SEC | MC_INT_DECERR_VPR |
		   MC_INT_SECURITY_VIOLATION | MC_INT_DECERR_EMEM,
	.ops = &tegra186_mc_ops,
	.ch_intmask = 0x0000000f,
	.global_intstatus_channel_shift = 0,
};
#endif
