// SPDX-License-Identifier: GPL-2.0
/*
 * TI j721e Cadence DSI wrapper
 *
 * Copyright (C) 2022 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Rahul T R <r-ravikumar@ti.com>
 */

#include <linux/io.h>
#include <linux/platform_device.h>

#include "cdns-dsi-j721e.h"

#define DSI_WRAP_REVISION		0x0
#define DSI_WRAP_DPI_CONTROL		0x4
#define DSI_WRAP_DSC_CONTROL		0x8
#define DSI_WRAP_DPI_SECURE		0xc
#define DSI_WRAP_DSI_0_ASF_STATUS	0x10

#define DSI_WRAP_DPI_0_EN		BIT(0)
#define DSI_WRAP_DSI2_MUX_SEL		BIT(4)

static int cdns_dsi_j721e_init(struct cdns_dsi *dsi)
{
	struct platform_device *pdev = to_platform_device(dsi->base.dev);

	dsi->j721e_regs = devm_platform_ioremap_resource(pdev, 1);
	return PTR_ERR_OR_ZERO(dsi->j721e_regs);
}

static void cdns_dsi_j721e_enable(struct cdns_dsi *dsi)
{
	/*
	 * Enable DPI0 as its input. DSS0 DPI2 is connected
	 * to DSI DPI0. This is the only supported configuration on
	 * J721E.
	 */
	writel(DSI_WRAP_DPI_0_EN, dsi->j721e_regs + DSI_WRAP_DPI_CONTROL);
}

static void cdns_dsi_j721e_disable(struct cdns_dsi *dsi)
{
	/* Put everything to defaults  */
	writel(0, dsi->j721e_regs + DSI_WRAP_DPI_CONTROL);
}

const struct cdns_dsi_platform_ops dsi_ti_j721e_ops = {
	.init = cdns_dsi_j721e_init,
	.enable = cdns_dsi_j721e_enable,
	.disable = cdns_dsi_j721e_disable,
};
