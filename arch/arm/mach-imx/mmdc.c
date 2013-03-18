/*
 * Copyright 2011 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
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

#define MMDC_MAPSR		0x404
#define BP_MMDC_MAPSR_PSD	0
#define BP_MMDC_MAPSR_PSS	4

static int imx_mmdc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	void __iomem *mmdc_base, *reg;
	u32 val;
	int timeout = 0x400;

	mmdc_base = of_iomap(np, 0);
	WARN_ON(!mmdc_base);

	reg = mmdc_base + MMDC_MAPSR;

	/* Enable automatic power saving */
	val = readl_relaxed(reg);
	val &= ~(1 << BP_MMDC_MAPSR_PSD);
	writel_relaxed(val, reg);

	/* Ensure it's successfully enabled */
	while (!(readl_relaxed(reg) & 1 << BP_MMDC_MAPSR_PSS) && --timeout)
		cpu_relax();

	if (unlikely(!timeout)) {
		pr_warn("%s: failed to enable automatic power saving\n",
			__func__);
		return -EBUSY;
	}

	return 0;
}

static struct of_device_id imx_mmdc_dt_ids[] = {
	{ .compatible = "fsl,imx6q-mmdc", },
	{ /* sentinel */ }
};

static struct platform_driver imx_mmdc_driver = {
	.driver		= {
		.name	= "imx-mmdc",
		.owner	= THIS_MODULE,
		.of_match_table = imx_mmdc_dt_ids,
	},
	.probe		= imx_mmdc_probe,
};

static int __init imx_mmdc_init(void)
{
	return platform_driver_register(&imx_mmdc_driver);
}
postcore_initcall(imx_mmdc_init);
