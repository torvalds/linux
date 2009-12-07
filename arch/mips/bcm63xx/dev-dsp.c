/*
 * Broadcom BCM63xx VoIP DSP registration
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2009 Florian Fainelli <florian@openwrt.org>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <bcm63xx_cpu.h>
#include <bcm63xx_dev_dsp.h>
#include <bcm63xx_regs.h>
#include <bcm63xx_io.h>

static struct resource voip_dsp_resources[] = {
	{
		.start		= -1, /* filled at runtime */
		.end		= -1, /* filled at runtime */
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= -1, /* filled at runtime */
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device bcm63xx_voip_dsp_device = {
	.name		= "bcm63xx-voip-dsp",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(voip_dsp_resources),
	.resource	= voip_dsp_resources,
};

int __init bcm63xx_dsp_register(const struct bcm63xx_dsp_platform_data *pd)
{
	struct bcm63xx_dsp_platform_data *dpd;
	u32 val;

	/* Get the memory window */
	val = bcm_mpi_readl(MPI_CSBASE_REG(pd->cs - 1));
	val &= MPI_CSBASE_BASE_MASK;
	voip_dsp_resources[0].start = val;
	voip_dsp_resources[0].end = val + 0xFFFFFFF;
	voip_dsp_resources[1].start = pd->ext_irq;

	/* copy given platform data */
	dpd = bcm63xx_voip_dsp_device.dev.platform_data;
	memcpy(dpd, pd, sizeof (*pd));

	return platform_device_register(&bcm63xx_voip_dsp_device);
}
