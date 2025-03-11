// SPDX-License-Identifier: GPL-2.0
/*
 * Interconnect framework driver for i.MX8MN SoC
 *
 * Copyright (c) 2019-2020, NXP
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <dt-bindings/interconnect/imx8mn.h>

#include "imx.h"

static const struct imx_icc_node_adj_desc imx8mn_dram_adj = {
	.bw_mul = 1,
	.bw_div = 4,
	.phandle_name = "fsl,ddrc",
};

static const struct imx_icc_node_adj_desc imx8mn_noc_adj = {
	.bw_mul = 1,
	.bw_div = 4,
	.main_noc = true,
};

/*
 * Describe bus masters, slaves and connections between them
 *
 * This is a simplified subset of the bus diagram, there are several other
 * PL301 nics which are skipped/merged into PL301_MAIN
 */
static struct imx_icc_node_desc nodes[] = {
	DEFINE_BUS_INTERCONNECT("NOC", IMX8MN_ICN_NOC, &imx8mn_noc_adj,
			IMX8MN_ICS_DRAM, IMX8MN_ICN_MAIN),

	DEFINE_BUS_SLAVE("DRAM", IMX8MN_ICS_DRAM, &imx8mn_dram_adj),
	DEFINE_BUS_SLAVE("OCRAM", IMX8MN_ICS_OCRAM, NULL),
	DEFINE_BUS_MASTER("A53", IMX8MN_ICM_A53, IMX8MN_ICN_NOC),

	/* GPUMIX */
	DEFINE_BUS_MASTER("GPU", IMX8MN_ICM_GPU, IMX8MN_ICN_GPU),
	DEFINE_BUS_INTERCONNECT("PL301_GPU", IMX8MN_ICN_GPU, NULL, IMX8MN_ICN_NOC),

	/* DISPLAYMIX */
	DEFINE_BUS_MASTER("CSI1", IMX8MN_ICM_CSI1, IMX8MN_ICN_MIPI),
	DEFINE_BUS_MASTER("CSI2", IMX8MN_ICM_CSI2, IMX8MN_ICN_MIPI),
	DEFINE_BUS_MASTER("ISI", IMX8MN_ICM_ISI, IMX8MN_ICN_MIPI),
	DEFINE_BUS_MASTER("LCDIF", IMX8MN_ICM_LCDIF, IMX8MN_ICN_MIPI),
	DEFINE_BUS_INTERCONNECT("PL301_MIPI", IMX8MN_ICN_MIPI, NULL, IMX8MN_ICN_NOC),

	/* USB goes straight to NOC */
	DEFINE_BUS_MASTER("USB", IMX8MN_ICM_USB, IMX8MN_ICN_NOC),

	/* Audio */
	DEFINE_BUS_MASTER("SDMA2", IMX8MN_ICM_SDMA2, IMX8MN_ICN_AUDIO),
	DEFINE_BUS_MASTER("SDMA3", IMX8MN_ICM_SDMA3, IMX8MN_ICN_AUDIO),
	DEFINE_BUS_INTERCONNECT("PL301_AUDIO", IMX8MN_ICN_AUDIO, NULL, IMX8MN_ICN_MAIN),

	/* Ethernet */
	DEFINE_BUS_MASTER("ENET", IMX8MN_ICM_ENET, IMX8MN_ICN_ENET),
	DEFINE_BUS_INTERCONNECT("PL301_ENET", IMX8MN_ICN_ENET, NULL, IMX8MN_ICN_MAIN),

	/* Other */
	DEFINE_BUS_MASTER("SDMA1", IMX8MN_ICM_SDMA1, IMX8MN_ICN_MAIN),
	DEFINE_BUS_MASTER("NAND", IMX8MN_ICM_NAND, IMX8MN_ICN_MAIN),
	DEFINE_BUS_MASTER("USDHC1", IMX8MN_ICM_USDHC1, IMX8MN_ICN_MAIN),
	DEFINE_BUS_MASTER("USDHC2", IMX8MN_ICM_USDHC2, IMX8MN_ICN_MAIN),
	DEFINE_BUS_MASTER("USDHC3", IMX8MN_ICM_USDHC3, IMX8MN_ICN_MAIN),
	DEFINE_BUS_INTERCONNECT("PL301_MAIN", IMX8MN_ICN_MAIN, NULL,
			IMX8MN_ICN_NOC, IMX8MN_ICS_OCRAM),
};

static int imx8mn_icc_probe(struct platform_device *pdev)
{
	return imx_icc_register(pdev, nodes, ARRAY_SIZE(nodes), NULL);
}

static struct platform_driver imx8mn_icc_driver = {
	.probe = imx8mn_icc_probe,
	.remove = imx_icc_unregister,
	.driver = {
		.name = "imx8mn-interconnect",
	},
};

module_platform_driver(imx8mn_icc_driver);
MODULE_ALIAS("platform:imx8mn-interconnect");
MODULE_AUTHOR("Leonard Crestez <leonard.crestez@nxp.com>");
MODULE_DESCRIPTION("Interconnect framework driver for i.MX8MN SoC");
MODULE_LICENSE("GPL v2");
