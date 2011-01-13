/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <bcm63xx_dev_enet.h>
#include <bcm63xx_io.h>
#include <bcm63xx_regs.h>

static struct resource shared_res[] = {
	{
		.start		= -1, /* filled at runtime */
		.end		= -1, /* filled at runtime */
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device bcm63xx_enet_shared_device = {
	.name		= "bcm63xx_enet_shared",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(shared_res),
	.resource	= shared_res,
};

static int shared_device_registered;

static struct resource enet0_res[] = {
	{
		.start		= -1, /* filled at runtime */
		.end		= -1, /* filled at runtime */
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= -1, /* filled at runtime */
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= -1, /* filled at runtime */
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= -1, /* filled at runtime */
		.flags		= IORESOURCE_IRQ,
	},
};

static struct bcm63xx_enet_platform_data enet0_pd;

static struct platform_device bcm63xx_enet0_device = {
	.name		= "bcm63xx_enet",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(enet0_res),
	.resource	= enet0_res,
	.dev		= {
		.platform_data = &enet0_pd,
	},
};

static struct resource enet1_res[] = {
	{
		.start		= -1, /* filled at runtime */
		.end		= -1, /* filled at runtime */
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= -1, /* filled at runtime */
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= -1, /* filled at runtime */
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= -1, /* filled at runtime */
		.flags		= IORESOURCE_IRQ,
	},
};

static struct bcm63xx_enet_platform_data enet1_pd;

static struct platform_device bcm63xx_enet1_device = {
	.name		= "bcm63xx_enet",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(enet1_res),
	.resource	= enet1_res,
	.dev		= {
		.platform_data = &enet1_pd,
	},
};

int __init bcm63xx_enet_register(int unit,
				 const struct bcm63xx_enet_platform_data *pd)
{
	struct platform_device *pdev;
	struct bcm63xx_enet_platform_data *dpd;
	int ret;

	if (unit > 1)
		return -ENODEV;

	if (unit == 1 && BCMCPU_IS_6338())
		return -ENODEV;

	if (!shared_device_registered) {
		shared_res[0].start = bcm63xx_regset_address(RSET_ENETDMA);
		shared_res[0].end = shared_res[0].start;
		if (BCMCPU_IS_6338())
			shared_res[0].end += (RSET_ENETDMA_SIZE / 2)  - 1;
		else
			shared_res[0].end += (RSET_ENETDMA_SIZE)  - 1;

		ret = platform_device_register(&bcm63xx_enet_shared_device);
		if (ret)
			return ret;
		shared_device_registered = 1;
	}

	if (unit == 0) {
		enet0_res[0].start = bcm63xx_regset_address(RSET_ENET0);
		enet0_res[0].end = enet0_res[0].start;
		enet0_res[0].end += RSET_ENET_SIZE - 1;
		enet0_res[1].start = bcm63xx_get_irq_number(IRQ_ENET0);
		enet0_res[2].start = bcm63xx_get_irq_number(IRQ_ENET0_RXDMA);
		enet0_res[3].start = bcm63xx_get_irq_number(IRQ_ENET0_TXDMA);
		pdev = &bcm63xx_enet0_device;
	} else {
		enet1_res[0].start = bcm63xx_regset_address(RSET_ENET1);
		enet1_res[0].end = enet1_res[0].start;
		enet1_res[0].end += RSET_ENET_SIZE - 1;
		enet1_res[1].start = bcm63xx_get_irq_number(IRQ_ENET1);
		enet1_res[2].start = bcm63xx_get_irq_number(IRQ_ENET1_RXDMA);
		enet1_res[3].start = bcm63xx_get_irq_number(IRQ_ENET1_TXDMA);
		pdev = &bcm63xx_enet1_device;
	}

	/* copy given platform data */
	dpd = pdev->dev.platform_data;
	memcpy(dpd, pd, sizeof(*pd));

	/* adjust them in case internal phy is used */
	if (dpd->use_internal_phy) {

		/* internal phy only exists for enet0 */
		if (unit == 1)
			return -ENODEV;

		dpd->phy_id = 1;
		dpd->has_phy_interrupt = 1;
		dpd->phy_interrupt = bcm63xx_get_irq_number(IRQ_ENET_PHY);
	}

	ret = platform_device_register(pdev);
	if (ret)
		return ret;
	return 0;
}
