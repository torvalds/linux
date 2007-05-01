/*
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
#include <linux/platform_device.h>
#include <linux/mv643xx.h>
#include <linux/pci.h>

#define PEGASOS2_MARVELL_REGBASE 		(0xf1000000)
#define PEGASOS2_MARVELL_REGSIZE 		(0x00004000)
#define PEGASOS2_SRAM_BASE 			(0xf2000000)
#define PEGASOS2_SRAM_SIZE			(256*1024)

#define PEGASOS2_SRAM_BASE_ETH0			(PEGASOS2_SRAM_BASE)
#define PEGASOS2_SRAM_BASE_ETH1			(PEGASOS2_SRAM_BASE_ETH0 + (PEGASOS2_SRAM_SIZE / 2) )


#define PEGASOS2_SRAM_RXRING_SIZE		(PEGASOS2_SRAM_SIZE/4)
#define PEGASOS2_SRAM_TXRING_SIZE		(PEGASOS2_SRAM_SIZE/4)

#undef BE_VERBOSE

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


static struct mv643xx_eth_platform_data eth0_pd = {
	.port_number	= 0,
	.tx_sram_addr = PEGASOS2_SRAM_BASE_ETH0,
	.tx_sram_size = PEGASOS2_SRAM_TXRING_SIZE,
	.tx_queue_size = PEGASOS2_SRAM_TXRING_SIZE/16,

	.rx_sram_addr = PEGASOS2_SRAM_BASE_ETH0 + PEGASOS2_SRAM_TXRING_SIZE,
	.rx_sram_size = PEGASOS2_SRAM_RXRING_SIZE,
	.rx_queue_size = PEGASOS2_SRAM_RXRING_SIZE/16,
};

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

static struct mv643xx_eth_platform_data eth1_pd = {
	.port_number	= 1,
	.tx_sram_addr = PEGASOS2_SRAM_BASE_ETH1,
	.tx_sram_size = PEGASOS2_SRAM_TXRING_SIZE,
	.tx_queue_size = PEGASOS2_SRAM_TXRING_SIZE/16,

	.rx_sram_addr = PEGASOS2_SRAM_BASE_ETH1 + PEGASOS2_SRAM_TXRING_SIZE,
	.rx_sram_size = PEGASOS2_SRAM_RXRING_SIZE,
	.rx_queue_size = PEGASOS2_SRAM_RXRING_SIZE/16,
};

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

/***********/
/***********/
#define MV_READ(offset,val) 	{ val = readl(mv643xx_reg_base + offset); }
#define MV_WRITE(offset,data) writel(data, mv643xx_reg_base + offset)

static void __iomem *mv643xx_reg_base;

static int Enable_SRAM(void)
{
	u32 ALong;

	if (mv643xx_reg_base == NULL)
		mv643xx_reg_base = ioremap(PEGASOS2_MARVELL_REGBASE,
					PEGASOS2_MARVELL_REGSIZE);

	if (mv643xx_reg_base == NULL)
		return -ENOMEM;

#ifdef BE_VERBOSE
	printk("Pegasos II/Marvell MV64361: register remapped from %p to %p\n",
		(void *)PEGASOS2_MARVELL_REGBASE, (void *)mv643xx_reg_base);
#endif

	MV_WRITE(MV64340_SRAM_CONFIG, 0);

	MV_WRITE(MV64340_INTEGRATED_SRAM_BASE_ADDR, PEGASOS2_SRAM_BASE >> 16);

	MV_READ(MV64340_BASE_ADDR_ENABLE, ALong);
	ALong &= ~(1 << 19);
	MV_WRITE(MV64340_BASE_ADDR_ENABLE, ALong);

	ALong = 0x02;
	ALong |= PEGASOS2_SRAM_BASE & 0xffff0000;
	MV_WRITE(MV643XX_ETH_BAR_4, ALong);

	MV_WRITE(MV643XX_ETH_SIZE_REG_4, (PEGASOS2_SRAM_SIZE-1) & 0xffff0000);

	MV_READ(MV643XX_ETH_BASE_ADDR_ENABLE_REG, ALong);
	ALong &= ~(1 << 4);
	MV_WRITE(MV643XX_ETH_BASE_ADDR_ENABLE_REG, ALong);

#ifdef BE_VERBOSE
	printk("Pegasos II/Marvell MV64361: register unmapped\n");
	printk("Pegasos II/Marvell MV64361: SRAM at %p, size=%x\n", (void*) PEGASOS2_SRAM_BASE, PEGASOS2_SRAM_SIZE);
#endif

	iounmap(mv643xx_reg_base);
	mv643xx_reg_base = NULL;

	return 1;
}


/***********/
/***********/
int mv643xx_eth_add_pds(void)
{
	int ret = 0;
	static struct pci_device_id pci_marvell_mv64360[] = {
		{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, PCI_DEVICE_ID_MARVELL_MV64360) },
		{ }
	};

#ifdef BE_VERBOSE
	printk("Pegasos II/Marvell MV64361: init\n");
#endif

	if (pci_dev_present(pci_marvell_mv64360)) {
		ret = platform_add_devices(mv643xx_eth_pd_devs,
				ARRAY_SIZE(mv643xx_eth_pd_devs));

		if ( Enable_SRAM() < 0)
		{
			eth0_pd.tx_sram_addr = 0;
			eth0_pd.tx_sram_size = 0;
			eth0_pd.rx_sram_addr = 0;
			eth0_pd.rx_sram_size = 0;

			eth1_pd.tx_sram_addr = 0;
			eth1_pd.tx_sram_size = 0;
			eth1_pd.rx_sram_addr = 0;
			eth1_pd.rx_sram_size = 0;

#ifdef BE_VERBOSE
			printk("Pegasos II/Marvell MV64361: Can't enable the "
				"SRAM\n");
#endif
		}
	}

#ifdef BE_VERBOSE
	printk("Pegasos II/Marvell MV64361: init is over\n");
#endif

	return ret;
}

device_initcall(mv643xx_eth_add_pds);
