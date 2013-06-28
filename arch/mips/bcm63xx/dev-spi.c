/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2009-2011 Florian Fainelli <florian@openwrt.org>
 * Copyright (C) 2010 Tanguy Bouzeloc <tanguy.bouzeloc@efixo.com>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>

#include <bcm63xx_cpu.h>
#include <bcm63xx_dev_spi.h>
#include <bcm63xx_regs.h>

#ifdef BCMCPU_RUNTIME_DETECT
/*
 * register offsets
 */
static const unsigned long bcm6348_regs_spi[] = {
	__GEN_SPI_REGS_TABLE(6348)
};

static const unsigned long bcm6358_regs_spi[] = {
	__GEN_SPI_REGS_TABLE(6358)
};

const unsigned long *bcm63xx_regs_spi;
EXPORT_SYMBOL(bcm63xx_regs_spi);

static __init void bcm63xx_spi_regs_init(void)
{
	if (BCMCPU_IS_6338() || BCMCPU_IS_6348())
		bcm63xx_regs_spi = bcm6348_regs_spi;
	if (BCMCPU_IS_6358() || BCMCPU_IS_6362() || BCMCPU_IS_6368())
		bcm63xx_regs_spi = bcm6358_regs_spi;
}
#else
static __init void bcm63xx_spi_regs_init(void) { }
#endif

static struct resource spi_resources[] = {
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

static struct bcm63xx_spi_pdata spi_pdata = {
	.bus_num		= 0,
	.num_chipselect		= 8,
};

static struct platform_device bcm63xx_spi_device = {
	.name		= "bcm63xx-spi",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(spi_resources),
	.resource	= spi_resources,
	.dev		= {
		.platform_data = &spi_pdata,
	},
};

int __init bcm63xx_spi_register(void)
{
	if (BCMCPU_IS_6328() || BCMCPU_IS_6345())
		return -ENODEV;

	spi_resources[0].start = bcm63xx_regset_address(RSET_SPI);
	spi_resources[0].end = spi_resources[0].start;
	spi_resources[1].start = bcm63xx_get_irq_number(IRQ_SPI);

	if (BCMCPU_IS_6338() || BCMCPU_IS_6348()) {
		spi_resources[0].end += BCM_6348_RSET_SPI_SIZE - 1;
		spi_pdata.fifo_size = SPI_6348_MSG_DATA_SIZE;
		spi_pdata.msg_type_shift = SPI_6348_MSG_TYPE_SHIFT;
		spi_pdata.msg_ctl_width = SPI_6348_MSG_CTL_WIDTH;
	}

	if (BCMCPU_IS_6358() || BCMCPU_IS_6362() || BCMCPU_IS_6368()) {
		spi_resources[0].end += BCM_6358_RSET_SPI_SIZE - 1;
		spi_pdata.fifo_size = SPI_6358_MSG_DATA_SIZE;
		spi_pdata.msg_type_shift = SPI_6358_MSG_TYPE_SHIFT;
		spi_pdata.msg_ctl_width = SPI_6358_MSG_CTL_WIDTH;
	}

	bcm63xx_spi_regs_init();

	return platform_device_register(&bcm63xx_spi_device);
}
