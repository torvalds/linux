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
	{
		.start		= -1, /* filled at runtime */
		.end		= -1, /* filled at runtime */
		.flags		= IORESOURCE_MEM,
	},
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

static struct resource enetsw_res[] = {
	{
		/* start & end filled at runtime */
		.flags		= IORESOURCE_MEM,
	},
	{
		/* start filled at runtime */
		.flags		= IORESOURCE_IRQ,
	},
	{
		/* start filled at runtime */
		.flags		= IORESOURCE_IRQ,
	},
};

static struct bcm63xx_enetsw_platform_data enetsw_pd;

static struct platform_device bcm63xx_enetsw_device = {
	.name		= "bcm63xx_enetsw",
	.num_resources	= ARRAY_SIZE(enetsw_res),
	.resource	= enetsw_res,
	.dev		= {
		.platform_data = &enetsw_pd,
	},
};

static int __init register_shared(void)
{
	int ret, chan_count;

	if (shared_device_registered)
		return 0;

	shared_res[0].start = bcm63xx_regset_address(RSET_ENETDMA);
	shared_res[0].end = shared_res[0].start;
	shared_res[0].end += (RSET_ENETDMA_SIZE)  - 1;

	if (BCMCPU_IS_6328() || BCMCPU_IS_6362() || BCMCPU_IS_6368())
		chan_count = 32;
	else
		chan_count = 16;

	shared_res[1].start = bcm63xx_regset_address(RSET_ENETDMAC);
	shared_res[1].end = shared_res[1].start;
	shared_res[1].end += RSET_ENETDMAC_SIZE(chan_count)  - 1;

	shared_res[2].start = bcm63xx_regset_address(RSET_ENETDMAS);
	shared_res[2].end = shared_res[2].start;
	shared_res[2].end += RSET_ENETDMAS_SIZE(chan_count)  - 1;

	ret = platform_device_register(&bcm63xx_enet_shared_device);
	if (ret)
		return ret;
	shared_device_registered = 1;

	return 0;
}

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

	ret = register_shared();
	if (ret)
		return ret;

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

int __init
bcm63xx_enetsw_register(const struct bcm63xx_enetsw_platform_data *pd)
{
	int ret;

	if (!BCMCPU_IS_6328() && !BCMCPU_IS_6362() && !BCMCPU_IS_6368())
		return -ENODEV;

	ret = register_shared();
	if (ret)
		return ret;

	enetsw_res[0].start = bcm63xx_regset_address(RSET_ENETSW);
	enetsw_res[0].end = enetsw_res[0].start;
	enetsw_res[0].end += RSET_ENETSW_SIZE - 1;
	enetsw_res[1].start = bcm63xx_get_irq_number(IRQ_ENETSW_RXDMA0);
	enetsw_res[2].start = bcm63xx_get_irq_number(IRQ_ENETSW_TXDMA0);
	if (!enetsw_res[2].start)
		enetsw_res[2].start = -1;

	memcpy(bcm63xx_enetsw_device.dev.platform_data, pd, sizeof(*pd));

	if (BCMCPU_IS_6328())
		enetsw_pd.num_ports = ENETSW_PORTS_6328;
	else if (BCMCPU_IS_6362() || BCMCPU_IS_6368())
		enetsw_pd.num_ports = ENETSW_PORTS_6368;

	ret = platform_device_register(&bcm63xx_enetsw_device);
	if (ret)
		return ret;

	return 0;
}
