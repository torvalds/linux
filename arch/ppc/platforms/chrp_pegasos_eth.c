/*
 *  arch/ppc/platforms/chrp_pegasos_eth.c
 *
 *  Copyright (C) 2005 Sven Luther <sl@bplan-gmbh.de>
 *  Thanks to :
 *	Dale Farnsworth <dale@farnsworth.org>
 *	Mark A. Greer <mgreer@mvista.com>
 *	Nicolas DET <nd@bplan-gmbh.de>
 *	Benjamin Herrenschmidt <benh@kernel.crashing.org>
 *  And anyone else who helped me on this.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/mv643xx.h>
#include <linux/pci.h>

/* Pegasos 2 specific Marvell MV 64361 gigabit ethernet port setup */
static struct resource mv643xx_eth_shared_resources[] = {
	[0] = {
		.name	= "ethernet shared base",
		.start	= 0xf1000000 + MV643XX_ETH_SHARED_REGS,
		.end	= 0xf1000000 + MV643XX_ETH_SHARED_REGS +
					MV643XX_ETH_SHARED_REGS_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device mv643xx_eth_shared_device = {
	.name		= MV643XX_ETH_SHARED_NAME,
	.id		= 0,
	.num_resources	= ARRAY_SIZE(mv643xx_eth_shared_resources),
	.resource	= mv643xx_eth_shared_resources,
};

static struct resource mv643xx_eth0_resources[] = {
	[0] = {
		.name	= "eth0 irq",
		.start	= 9,
		.end	= 9,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv643xx_eth_platform_data eth0_pd;

static struct platform_device eth0_device = {
	.name		= MV643XX_ETH_NAME,
	.id		= 0,
	.num_resources	= ARRAY_SIZE(mv643xx_eth0_resources),
	.resource	= mv643xx_eth0_resources,
	.dev = {
		.platform_data = &eth0_pd,
	},
};

static struct resource mv643xx_eth1_resources[] = {
	[0] = {
		.name	= "eth1 irq",
		.start	= 9,
		.end	= 9,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv643xx_eth_platform_data eth1_pd;

static struct platform_device eth1_device = {
	.name		= MV643XX_ETH_NAME,
	.id		= 1,
	.num_resources	= ARRAY_SIZE(mv643xx_eth1_resources),
	.resource	= mv643xx_eth1_resources,
	.dev = {
		.platform_data = &eth1_pd,
	},
};

static struct platform_device *mv643xx_eth_pd_devs[] __initdata = {
	&mv643xx_eth_shared_device,
	&eth0_device,
	&eth1_device,
};


int
mv643xx_eth_add_pds(void)
{
	int ret = 0;
	static struct pci_device_id pci_marvell_mv64360[] = {
		{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, PCI_DEVICE_ID_MARVELL_MV64360) },
		{ }
	};

	if (pci_dev_present(pci_marvell_mv64360)) {
		ret = platform_add_devices(mv643xx_eth_pd_devs, ARRAY_SIZE(mv643xx_eth_pd_devs));
	}
	return ret;
}
device_initcall(mv643xx_eth_add_pds);
