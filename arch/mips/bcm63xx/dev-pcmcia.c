/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/bootinfo.h>
#include <linux/platform_device.h>
#include <bcm63xx_cs.h>
#include <bcm63xx_cpu.h>
#include <bcm63xx_dev_pcmcia.h>
#include <bcm63xx_io.h>
#include <bcm63xx_regs.h>

static struct resource pcmcia_resources[] = {
	/* pcmcia registers */
	{
		/* start & end filled at runtime */
		.flags		= IORESOURCE_MEM,
	},

	/* pcmcia memory zone resources */
	{
		.start		= BCM_PCMCIA_COMMON_BASE_PA,
		.end		= BCM_PCMCIA_COMMON_END_PA,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= BCM_PCMCIA_ATTR_BASE_PA,
		.end		= BCM_PCMCIA_ATTR_END_PA,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= BCM_PCMCIA_IO_BASE_PA,
		.end		= BCM_PCMCIA_IO_END_PA,
		.flags		= IORESOURCE_MEM,
	},

	/* PCMCIA irq */
	{
		/* start filled at runtime */
		.flags		= IORESOURCE_IRQ,
	},

	/* declare PCMCIA IO resource also */
	{
		.start		= BCM_PCMCIA_IO_BASE_PA,
		.end		= BCM_PCMCIA_IO_END_PA,
		.flags		= IORESOURCE_IO,
	},
};

static struct bcm63xx_pcmcia_platform_data pd;

static struct platform_device bcm63xx_pcmcia_device = {
	.name		= "bcm63xx_pcmcia",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(pcmcia_resources),
	.resource	= pcmcia_resources,
	.dev		= {
		.platform_data = &pd,
	},
};

static int __init config_pcmcia_cs(unsigned int cs,
				   u32 base, unsigned int size)
{
	int ret;

	ret = bcm63xx_set_cs_status(cs, 0);
	if (!ret)
		ret = bcm63xx_set_cs_base(cs, base, size);
	if (!ret)
		ret = bcm63xx_set_cs_status(cs, 1);
	return ret;
}

static const struct {
	unsigned int	cs;
	unsigned int	base;
	unsigned int	size;
} pcmcia_cs[3] __initconst = {
	{
		.cs	= MPI_CS_PCMCIA_COMMON,
		.base	= BCM_PCMCIA_COMMON_BASE_PA,
		.size	= BCM_PCMCIA_COMMON_SIZE
	},
	{
		.cs	= MPI_CS_PCMCIA_ATTR,
		.base	= BCM_PCMCIA_ATTR_BASE_PA,
		.size	= BCM_PCMCIA_ATTR_SIZE
	},
	{
		.cs	= MPI_CS_PCMCIA_IO,
		.base	= BCM_PCMCIA_IO_BASE_PA,
		.size	= BCM_PCMCIA_IO_SIZE
	},
};

int __init bcm63xx_pcmcia_register(void)
{
	int ret, i;

	if (!BCMCPU_IS_6348() && !BCMCPU_IS_6358())
		return 0;

	/* use correct pcmcia ready gpio depending on processor */
	switch (bcm63xx_get_cpu_id()) {
	case BCM6348_CPU_ID:
		pd.ready_gpio = 22;
		break;

	case BCM6358_CPU_ID:
		pd.ready_gpio = 18;
		break;

	default:
		return -ENODEV;
	}

	pcmcia_resources[0].start = bcm63xx_regset_address(RSET_PCMCIA);
	pcmcia_resources[0].end = pcmcia_resources[0].start +
		RSET_PCMCIA_SIZE - 1;
	pcmcia_resources[4].start = bcm63xx_get_irq_number(IRQ_PCMCIA);

	/* configure pcmcia chip selects */
	for (i = 0; i < 3; i++) {
		ret = config_pcmcia_cs(pcmcia_cs[i].cs,
				       pcmcia_cs[i].base,
				       pcmcia_cs[i].size);
		if (ret)
			goto out_err;
	}

	return platform_device_register(&bcm63xx_pcmcia_device);

out_err:
	pr_err("unable to set pcmcia chip select\n");
	return ret;
}
