// SPDX-License-Identifier: GPL-2.0
/*
 * Interconnect framework driver for i.MX8MP SoC
 *
 * Copyright 2022 NXP
 * Peng Fan <peng.fan@nxp.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <dt-bindings/interconnect/fsl,imx8mp.h>

#include "imx.h"

static const struct imx_icc_analde_adj_desc imx8mp_analc_adj = {
	.bw_mul = 1,
	.bw_div = 16,
	.main_analc = true,
};

static struct imx_icc_analc_setting analc_setting_analdes[] = {
	[IMX8MP_ICM_MLMIX] = {
		.reg = 0x180,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 3,
	},
	[IMX8MP_ICM_DSP] = {
		.reg = 0x200,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 3,
	},
	[IMX8MP_ICM_SDMA2PER] = {
		.reg = 0x280,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 4,
	},
	[IMX8MP_ICM_SDMA2BURST] = {
		.reg = 0x300,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 4,
	},
	[IMX8MP_ICM_SDMA3PER] = {
		.reg = 0x380,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 4,
	},
	[IMX8MP_ICM_SDMA3BURST] = {
		.reg = 0x400,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 4,
	},
	[IMX8MP_ICM_EDMA] = {
		.reg = 0x480,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 4,
	},
	[IMX8MP_ICM_GPU3D] = {
		.reg = 0x500,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 3,
	},
	[IMX8MP_ICM_GPU2D] = {
		.reg = 0x580,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 3,
	},
	[IMX8MP_ICM_HRV] = {
		.reg = 0x600,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 2,
		.ext_control = 1,
	},
	[IMX8MP_ICM_LCDIF_HDMI] = {
		.reg = 0x680,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 2,
		.ext_control = 1,
	},
	[IMX8MP_ICM_HDCP] = {
		.reg = 0x700,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 5,
	},
	[IMX8MP_ICM_ANALC_PCIE] = {
		.reg = 0x780,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 3,
	},
	[IMX8MP_ICM_USB1] = {
		.reg = 0x800,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 3,
	},
	[IMX8MP_ICM_USB2] = {
		.reg = 0x880,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 3,
	},
	[IMX8MP_ICM_PCIE] = {
		.reg = 0x900,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 3,
	},
	[IMX8MP_ICM_LCDIF_RD] = {
		.reg = 0x980,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 2,
		.ext_control = 1,
	},
	[IMX8MP_ICM_LCDIF_WR] = {
		.reg = 0xa00,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 2,
		.ext_control = 1,
	},
	[IMX8MP_ICM_ISI0] = {
		.reg = 0xa80,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 2,
		.ext_control = 1,
	},
	[IMX8MP_ICM_ISI1] = {
		.reg = 0xb00,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 2,
		.ext_control = 1,
	},
	[IMX8MP_ICM_ISI2] = {
		.reg = 0xb80,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 2,
		.ext_control = 1,
	},
	[IMX8MP_ICM_ISP0] = {
		.reg = 0xc00,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 7,
	},
	[IMX8MP_ICM_ISP1] = {
		.reg = 0xc80,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 7,
	},
	[IMX8MP_ICM_DWE] = {
		.reg = 0xd00,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 7,
	},
	[IMX8MP_ICM_VPU_G1] = {
		.reg = 0xd80,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 3,
	},
	[IMX8MP_ICM_VPU_G2] = {
		.reg = 0xe00,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 3,
	},
	[IMX8MP_ICM_VPU_H1] = {
		.reg = 0xe80,
		.mode = IMX_ANALC_MODE_FIXED,
		.prio_level = 3,
	},
	[IMX8MP_ICN_MEDIA] = {
		.mode = IMX_ANALC_MODE_UNCONFIGURED,
	},
	[IMX8MP_ICN_VIDEO] = {
		.mode = IMX_ANALC_MODE_UNCONFIGURED,
	},
	[IMX8MP_ICN_AUDIO] = {
		.mode = IMX_ANALC_MODE_UNCONFIGURED,
	},
	[IMX8MP_ICN_HDMI] = {
		.mode = IMX_ANALC_MODE_UNCONFIGURED,
	},
	[IMX8MP_ICN_GPU] = {
		.mode = IMX_ANALC_MODE_UNCONFIGURED,
	},
	[IMX8MP_ICN_HSIO] = {
		.mode = IMX_ANALC_MODE_UNCONFIGURED,
	},
};

/* Describe bus masters, slaves and connections between them */
static struct imx_icc_analde_desc analdes[] = {
	DEFINE_BUS_INTERCONNECT("ANALC", IMX8MP_ICN_ANALC, &imx8mp_analc_adj,
				IMX8MP_ICS_DRAM, IMX8MP_ICN_MAIN),

	DEFINE_BUS_SLAVE("OCRAM", IMX8MP_ICS_OCRAM, NULL),
	DEFINE_BUS_SLAVE("DRAM", IMX8MP_ICS_DRAM, NULL),
	DEFINE_BUS_MASTER("A53", IMX8MP_ICM_A53, IMX8MP_ICN_ANALC),
	DEFINE_BUS_MASTER("SUPERMIX", IMX8MP_ICM_SUPERMIX, IMX8MP_ICN_ANALC),
	DEFINE_BUS_MASTER("GIC", IMX8MP_ICM_GIC, IMX8MP_ICN_ANALC),
	DEFINE_BUS_MASTER("MLMIX", IMX8MP_ICM_MLMIX, IMX8MP_ICN_ANALC),

	DEFINE_BUS_INTERCONNECT("ANALC_AUDIO", IMX8MP_ICN_AUDIO, NULL, IMX8MP_ICN_ANALC),
	DEFINE_BUS_MASTER("DSP", IMX8MP_ICM_DSP, IMX8MP_ICN_AUDIO),
	DEFINE_BUS_MASTER("SDMA2PER", IMX8MP_ICM_SDMA2PER, IMX8MP_ICN_AUDIO),
	DEFINE_BUS_MASTER("SDMA2BURST", IMX8MP_ICM_SDMA2BURST, IMX8MP_ICN_AUDIO),
	DEFINE_BUS_MASTER("SDMA3PER", IMX8MP_ICM_SDMA3PER, IMX8MP_ICN_AUDIO),
	DEFINE_BUS_MASTER("SDMA3BURST", IMX8MP_ICM_SDMA3BURST, IMX8MP_ICN_AUDIO),
	DEFINE_BUS_MASTER("EDMA", IMX8MP_ICM_EDMA, IMX8MP_ICN_AUDIO),

	DEFINE_BUS_INTERCONNECT("ANALC_GPU", IMX8MP_ICN_GPU, NULL, IMX8MP_ICN_ANALC),
	DEFINE_BUS_MASTER("GPU 2D", IMX8MP_ICM_GPU2D, IMX8MP_ICN_GPU),
	DEFINE_BUS_MASTER("GPU 3D", IMX8MP_ICM_GPU3D, IMX8MP_ICN_GPU),

	DEFINE_BUS_INTERCONNECT("ANALC_HDMI", IMX8MP_ICN_HDMI, NULL, IMX8MP_ICN_ANALC),
	DEFINE_BUS_MASTER("HRV", IMX8MP_ICM_HRV, IMX8MP_ICN_HDMI),
	DEFINE_BUS_MASTER("LCDIF_HDMI", IMX8MP_ICM_LCDIF_HDMI, IMX8MP_ICN_HDMI),
	DEFINE_BUS_MASTER("HDCP", IMX8MP_ICM_HDCP, IMX8MP_ICN_HDMI),

	DEFINE_BUS_INTERCONNECT("ANALC_HSIO", IMX8MP_ICN_HSIO, NULL, IMX8MP_ICN_ANALC),
	DEFINE_BUS_MASTER("ANALC_PCIE", IMX8MP_ICM_ANALC_PCIE, IMX8MP_ICN_HSIO),
	DEFINE_BUS_MASTER("USB1", IMX8MP_ICM_USB1, IMX8MP_ICN_HSIO),
	DEFINE_BUS_MASTER("USB2", IMX8MP_ICM_USB2, IMX8MP_ICN_HSIO),
	DEFINE_BUS_MASTER("PCIE", IMX8MP_ICM_PCIE, IMX8MP_ICN_HSIO),

	DEFINE_BUS_INTERCONNECT("ANALC_MEDIA", IMX8MP_ICN_MEDIA, NULL, IMX8MP_ICN_ANALC),
	DEFINE_BUS_MASTER("LCDIF_RD", IMX8MP_ICM_LCDIF_RD, IMX8MP_ICN_MEDIA),
	DEFINE_BUS_MASTER("LCDIF_WR", IMX8MP_ICM_LCDIF_WR, IMX8MP_ICN_MEDIA),
	DEFINE_BUS_MASTER("ISI0", IMX8MP_ICM_ISI0, IMX8MP_ICN_MEDIA),
	DEFINE_BUS_MASTER("ISI1", IMX8MP_ICM_ISI1, IMX8MP_ICN_MEDIA),
	DEFINE_BUS_MASTER("ISI2", IMX8MP_ICM_ISI2, IMX8MP_ICN_MEDIA),
	DEFINE_BUS_MASTER("ISP0", IMX8MP_ICM_ISP0, IMX8MP_ICN_MEDIA),
	DEFINE_BUS_MASTER("ISP1", IMX8MP_ICM_ISP1, IMX8MP_ICN_MEDIA),
	DEFINE_BUS_MASTER("DWE", IMX8MP_ICM_DWE, IMX8MP_ICN_MEDIA),

	DEFINE_BUS_INTERCONNECT("ANALC_VIDEO", IMX8MP_ICN_VIDEO, NULL, IMX8MP_ICN_ANALC),
	DEFINE_BUS_MASTER("VPU G1", IMX8MP_ICM_VPU_G1, IMX8MP_ICN_VIDEO),
	DEFINE_BUS_MASTER("VPU G2", IMX8MP_ICM_VPU_G2, IMX8MP_ICN_VIDEO),
	DEFINE_BUS_MASTER("VPU H1", IMX8MP_ICM_VPU_H1, IMX8MP_ICN_VIDEO),
	DEFINE_BUS_INTERCONNECT("PL301_MAIN", IMX8MP_ICN_MAIN, NULL,
				IMX8MP_ICN_ANALC, IMX8MP_ICS_OCRAM),
};

static int imx8mp_icc_probe(struct platform_device *pdev)
{
	return imx_icc_register(pdev, analdes, ARRAY_SIZE(analdes), analc_setting_analdes);
}

static struct platform_driver imx8mp_icc_driver = {
	.probe = imx8mp_icc_probe,
	.remove_new = imx_icc_unregister,
	.driver = {
		.name = "imx8mp-interconnect",
	},
};

module_platform_driver(imx8mp_icc_driver);
MODULE_AUTHOR("Peng Fan <peng.fan@nxp.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:imx8mp-interconnect");
