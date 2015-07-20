/*
 * Copyright 2015 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>

#include "hardware.h"

#define DDRC_MSTR		0x0
#define	BM_DDRC_MSTR_DDR3	0x1
#define	BM_DDRC_MSTR_LPDDR2	0x4
#define	BM_DDRC_MSTR_LPDDR3	0x8

static int ddr_type;

static int imx_ddrc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	void __iomem *ddrc_base, *reg;
	u32 val;

	ddrc_base = of_iomap(np, 0);
	WARN_ON(!ddrc_base);

	reg = ddrc_base + DDRC_MSTR;
	/* Get ddr type */
	val = readl_relaxed(reg);
	val &= (BM_DDRC_MSTR_DDR3 | BM_DDRC_MSTR_LPDDR2
		| BM_DDRC_MSTR_LPDDR3);

	switch (val) {
	case BM_DDRC_MSTR_DDR3:
		pr_info("DDR type is DDR3!\n");
		ddr_type = IMX_DDR_TYPE_DDR3;
		break;
	case BM_DDRC_MSTR_LPDDR2:
		pr_info("DDR type is LPDDR2!\n");
		ddr_type = IMX_DDR_TYPE_LPDDR2;
		break;
	case BM_DDRC_MSTR_LPDDR3:
		pr_info("DDR type is LPDDR3!\n");
		ddr_type = IMX_DDR_TYPE_LPDDR3;
		break;
	default:
		break;
	}

	return 0;
}

int imx_ddrc_get_ddr_type(void)
{
	return ddr_type;
}

static struct of_device_id imx_ddrc_dt_ids[] = {
	{ .compatible = "fsl,imx7-ddrc", },
	{ /* sentinel */ }
};

static struct platform_driver imx_ddrc_driver = {
	.driver		= {
		.name	= "imx-ddrc",
		.owner	= THIS_MODULE,
		.of_match_table = imx_ddrc_dt_ids,
	},
	.probe		= imx_ddrc_probe,
};

static int __init imx_ddrc_init(void)
{
	return platform_driver_register(&imx_ddrc_driver);
}
postcore_initcall(imx_ddrc_init);
