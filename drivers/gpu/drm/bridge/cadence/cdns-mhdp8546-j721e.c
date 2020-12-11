// SPDX-License-Identifier: GPL-2.0
/*
 * TI j721e Cadence MHDP8546 DP wrapper
 *
 * Copyright (C) 2020 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Jyri Sarha <jsarha@ti.com>
 */

#include <linux/io.h>
#include <linux/platform_device.h>

#include "cdns-mhdp8546-j721e.h"

#define	REVISION			0x00
#define	DPTX_IPCFG			0x04
#define	ECC_MEM_CFG			0x08
#define	DPTX_DSC_CFG			0x0c
#define	DPTX_SRC_CFG			0x10
#define	DPTX_VIF_SECURE_MODE_CFG	0x14
#define	DPTX_VIF_CONN_STATUS		0x18
#define	PHY_CLK_STATUS			0x1c

#define DPTX_SRC_AIF_EN			BIT(16)
#define DPTX_SRC_VIF_3_IN30B		BIT(11)
#define DPTX_SRC_VIF_2_IN30B		BIT(10)
#define DPTX_SRC_VIF_1_IN30B		BIT(9)
#define DPTX_SRC_VIF_0_IN30B		BIT(8)
#define DPTX_SRC_VIF_3_SEL_DPI5		BIT(7)
#define DPTX_SRC_VIF_3_SEL_DPI3		0
#define DPTX_SRC_VIF_2_SEL_DPI4		BIT(6)
#define DPTX_SRC_VIF_2_SEL_DPI2		0
#define DPTX_SRC_VIF_1_SEL_DPI3		BIT(5)
#define DPTX_SRC_VIF_1_SEL_DPI1		0
#define DPTX_SRC_VIF_0_SEL_DPI2		BIT(4)
#define DPTX_SRC_VIF_0_SEL_DPI0		0
#define DPTX_SRC_VIF_3_EN		BIT(3)
#define DPTX_SRC_VIF_2_EN		BIT(2)
#define DPTX_SRC_VIF_1_EN		BIT(1)
#define DPTX_SRC_VIF_0_EN		BIT(0)

/* TODO turn DPTX_IPCFG fw_mem_clk_en at pm_runtime_suspend. */

static int cdns_mhdp_j721e_init(struct cdns_mhdp_device *mhdp)
{
	struct platform_device *pdev = to_platform_device(mhdp->dev);

	mhdp->j721e_regs = devm_platform_ioremap_resource(pdev, 1);
	return PTR_ERR_OR_ZERO(mhdp->j721e_regs);
}

static void cdns_mhdp_j721e_enable(struct cdns_mhdp_device *mhdp)
{
	/*
	 * Enable VIF_0 and select DPI2 as its input. DSS0 DPI0 is connected
	 * to eDP DPI2. This is the only supported SST configuration on
	 * J721E.
	 */
	writel(DPTX_SRC_VIF_0_EN | DPTX_SRC_VIF_0_SEL_DPI2,
	       mhdp->j721e_regs + DPTX_SRC_CFG);
}

static void cdns_mhdp_j721e_disable(struct cdns_mhdp_device *mhdp)
{
	/* Put everything to defaults  */
	writel(0, mhdp->j721e_regs + DPTX_DSC_CFG);
}

const struct mhdp_platform_ops mhdp_ti_j721e_ops = {
	.init = cdns_mhdp_j721e_init,
	.enable = cdns_mhdp_j721e_enable,
	.disable = cdns_mhdp_j721e_disable,
};

const struct drm_bridge_timings mhdp_ti_j721e_bridge_timings = {
	.input_bus_flags = DRM_BUS_FLAG_PIXDATA_SAMPLE_NEGEDGE |
			   DRM_BUS_FLAG_SYNC_SAMPLE_NEGEDGE |
			   DRM_BUS_FLAG_DE_HIGH,
};
