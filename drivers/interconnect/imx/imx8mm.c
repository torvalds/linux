// SPDX-License-Identifier: GPL-2.0
/*
 * Interconnect framework driver for i.MX8MM SoC
 *
 * Copyright (c) 2019, BayLibre
 * Copyright (c) 2019-2020, NXP
 * Author: Alexandre Bailon <abailon@baylibre.com>
 * Author: Leonard Crestez <leonard.crestez@nxp.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <dt-bindings/interconnect/imx8mm.h>

#include "imx.h"

static const struct imx_icc_node_adj_desc imx8mm_dram_adj = {
	.bw_mul = 1,
	.bw_div = 16,
	.phandle_name = "fsl,ddrc",
};

static const struct imx_icc_node_adj_desc imx8mm_noc_adj = {
	.bw_mul = 1,
	.bw_div = 16,
	.main_noc = true,
};

/*
 * Describe bus masters, slaves and connections between them
 *
 * This is a simplified subset of the bus diagram, there are several other
 * PL301 nics which are skipped/merged into PL301_MAIN
 */
static struct imx_icc_node_desc nodes[] = {
	DEFINE_BUS_INTERCONNECT("NOC", IMX8MM_ICN_NOC, &imx8mm_noc_adj,
			IMX8MM_ICS_DRAM, IMX8MM_ICN_MAIN),

	DEFINE_BUS_SLAVE("DRAM", IMX8MM_ICS_DRAM, &imx8mm_dram_adj),
	DEFINE_BUS_SLAVE("OCRAM", IMX8MM_ICS_OCRAM, NULL),
	DEFINE_BUS_MASTER("A53", IMX8MM_ICM_A53, IMX8MM_ICN_NOC),

	/* VPUMIX */
	DEFINE_BUS_MASTER("VPU H1", IMX8MM_ICM_VPU_H1, IMX8MM_ICN_VIDEO),
	DEFINE_BUS_MASTER("VPU G1", IMX8MM_ICM_VPU_G1, IMX8MM_ICN_VIDEO),
	DEFINE_BUS_MASTER("VPU G2", IMX8MM_ICM_VPU_G2, IMX8MM_ICN_VIDEO),
	DEFINE_BUS_INTERCONNECT("PL301_VIDEO", IMX8MM_ICN_VIDEO, NULL, IMX8MM_ICN_NOC),

	/* GPUMIX */
	DEFINE_BUS_MASTER("GPU 2D", IMX8MM_ICM_GPU2D, IMX8MM_ICN_GPU),
	DEFINE_BUS_MASTER("GPU 3D", IMX8MM_ICM_GPU3D, IMX8MM_ICN_GPU),
	DEFINE_BUS_INTERCONNECT("PL301_GPU", IMX8MM_ICN_GPU, NULL, IMX8MM_ICN_NOC),

	/* DISPLAYMIX */
	DEFINE_BUS_MASTER("CSI", IMX8MM_ICM_CSI, IMX8MM_ICN_MIPI),
	DEFINE_BUS_MASTER("LCDIF", IMX8MM_ICM_LCDIF, IMX8MM_ICN_MIPI),
	DEFINE_BUS_INTERCONNECT("PL301_MIPI", IMX8MM_ICN_MIPI, NULL, IMX8MM_ICN_NOC),

	/* HSIO */
	DEFINE_BUS_MASTER("USB1", IMX8MM_ICM_USB1, IMX8MM_ICN_HSIO),
	DEFINE_BUS_MASTER("USB2", IMX8MM_ICM_USB2, IMX8MM_ICN_HSIO),
	DEFINE_BUS_MASTER("PCIE", IMX8MM_ICM_PCIE, IMX8MM_ICN_HSIO),
	DEFINE_BUS_INTERCONNECT("PL301_HSIO", IMX8MM_ICN_HSIO, NULL, IMX8MM_ICN_NOC),

	/* Audio */
	DEFINE_BUS_MASTER("SDMA2", IMX8MM_ICM_SDMA2, IMX8MM_ICN_AUDIO),
	DEFINE_BUS_MASTER("SDMA3", IMX8MM_ICM_SDMA3, IMX8MM_ICN_AUDIO),
	DEFINE_BUS_INTERCONNECT("PL301_AUDIO", IMX8MM_ICN_AUDIO, NULL, IMX8MM_ICN_MAIN),

	/* Ethernet */
	DEFINE_BUS_MASTER("ENET", IMX8MM_ICM_ENET, IMX8MM_ICN_ENET),
	DEFINE_BUS_INTERCONNECT("PL301_ENET", IMX8MM_ICN_ENET, NULL, IMX8MM_ICN_MAIN),

	/* Other */
	DEFINE_BUS_MASTER("SDMA1", IMX8MM_ICM_SDMA1, IMX8MM_ICN_MAIN),
	DEFINE_BUS_MASTER("NAND", IMX8MM_ICM_NAND, IMX8MM_ICN_MAIN),
	DEFINE_BUS_MASTER("USDHC1", IMX8MM_ICM_USDHC1, IMX8MM_ICN_MAIN),
	DEFINE_BUS_MASTER("USDHC2", IMX8MM_ICM_USDHC2, IMX8MM_ICN_MAIN),
	DEFINE_BUS_MASTER("USDHC3", IMX8MM_ICM_USDHC3, IMX8MM_ICN_MAIN),
	DEFINE_BUS_INTERCONNECT("PL301_MAIN", IMX8MM_ICN_MAIN, NULL,
			IMX8MM_ICN_NOC, IMX8MM_ICS_OCRAM),
};

static int imx8mm_icc_probe(struct platform_device *pdev)
{
	return imx_icc_register(pdev, nodes, ARRAY_SIZE(nodes), NULL);
}

static struct platform_driver imx8mm_icc_driver = {
	.probe = imx8mm_icc_probe,
	.remove_new = imx_icc_unregister,
	.driver = {
		.name = "imx8mm-interconnect",
	},
};

module_platform_driver(imx8mm_icc_driver);
MODULE_AUTHOR("Alexandre Bailon <abailon@baylibre.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:imx8mm-interconnect");
