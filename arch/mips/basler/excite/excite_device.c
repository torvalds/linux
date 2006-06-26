/*
 *  Copyright (C) 2004 by Basler Vision Technologies AG
 *  Author: Thomas Koeller <thomas.koeller@baslerweb.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <asm/types.h>
#include <asm/rm9k-ocd.h>

#include <excite.h>
#include <rm9k_eth.h>
#include <rm9k_wdt.h>
#include <rm9k_xicap.h>
#include <excite_nandflash.h>

#include "excite_iodev.h"

#define RM9K_GE_UNIT	0
#define XICAP_UNIT	0
#define NAND_UNIT	0

#define DLL_TIMEOUT	3		/* seconds */


#define RINIT(__start__, __end__, __name__, __parent__) {	\
	.name	= __name__ "_0",				\
	.start	= (__start__),					\
	.end	= (__end__),					\
	.flags	= 0,						\
	.parent	= (__parent__)					\
}

#define RINIT_IRQ(__irq__, __name__) {	\
	.name	= __name__ "_0",	\
	.start	= (__irq__),		\
	.end	= (__irq__),		\
	.flags	= IORESOURCE_IRQ,	\
	.parent	= NULL			\
}



enum {
	slice_xicap,
	slice_eth
};



static struct resource
	excite_ctr_resource = {
		.name		= "GPI counters",
		.start		= 0,
		.end		= 5,
		.flags		= 0,
		.parent		= NULL,
		.sibling	= NULL,
		.child		= NULL
	},
	excite_gpislice_resource = {
		.name		= "GPI slices",
		.start		= 0,
		.end		= 1,
		.flags		= 0,
		.parent		= NULL,
		.sibling	= NULL,
		.child		= NULL
	},
	excite_mdio_channel_resource = {
		.name		= "MDIO channels",
		.start		= 0,
		.end		= 1,
		.flags		= 0,
		.parent		= NULL,
		.sibling	= NULL,
		.child		= NULL
	},
	excite_fifomem_resource = {
		.name		= "FIFO memory",
		.start		= 0,
		.end		= 767,
		.flags		= 0,
		.parent		= NULL,
		.sibling	= NULL,
		.child		= NULL
	},
	excite_scram_resource = {
		.name		= "Scratch RAM",
		.start		= EXCITE_PHYS_SCRAM,
		.end		= EXCITE_PHYS_SCRAM + EXCITE_SIZE_SCRAM - 1,
		.flags		= IORESOURCE_MEM,
		.parent		= NULL,
		.sibling	= NULL,
		.child		= NULL
	},
	excite_fpga_resource = {
		.name		= "System FPGA",
		.start		= EXCITE_PHYS_FPGA,
		.end		= EXCITE_PHYS_FPGA + EXCITE_SIZE_FPGA - 1,
		.flags		= IORESOURCE_MEM,
		.parent		= NULL,
		.sibling	= NULL,
		.child		= NULL
	},
	excite_nand_resource = {
		.name		= "NAND flash control",
		.start		= EXCITE_PHYS_NAND,
		.end		= EXCITE_PHYS_NAND + EXCITE_SIZE_NAND - 1,
		.flags		= IORESOURCE_MEM,
		.parent		= NULL,
		.sibling	= NULL,
		.child		= NULL
	},
	excite_titan_resource = {
		.name		= "TITAN registers",
		.start		= EXCITE_PHYS_TITAN,
		.end		= EXCITE_PHYS_TITAN + EXCITE_SIZE_TITAN - 1,
		.flags		= IORESOURCE_MEM,
		.parent		= NULL,
		.sibling	= NULL,
		.child		= NULL
	};



static void adjust_resources(struct resource *res, unsigned int n)
{
	struct resource *p;
	const unsigned long mask = IORESOURCE_IO | IORESOURCE_MEM
				   | IORESOURCE_IRQ | IORESOURCE_DMA;

	for (p = res; p < res + n; p++) {
		const struct resource * const parent = p->parent;
		if (parent) {
			p->start += parent->start;
			p->end   += parent->start;
			p->flags =  parent->flags & mask;
		}
	}
}



#if defined(CONFIG_EXCITE_FCAP_GPI) || defined(CONFIG_EXCITE_FCAP_GPI_MODULE)
static struct resource xicap_rsrc[] = {
	RINIT(0x4840, 0x486f, XICAP_RESOURCE_FIFO_RX, &excite_titan_resource),
	RINIT(0x4940, 0x494b, XICAP_RESOURCE_FIFO_TX, &excite_titan_resource),
	RINIT(0x5040, 0x5127, XICAP_RESOURCE_XDMA, &excite_titan_resource),
	RINIT(0x1000, 0x112f, XICAP_RESOURCE_PKTPROC, &excite_titan_resource),
	RINIT(0x1100, 0x110f, XICAP_RESOURCE_PKT_STREAM, &excite_fpga_resource),
	RINIT(0x0800, 0x0bff, XICAP_RESOURCE_DMADESC, &excite_scram_resource),
	RINIT(slice_xicap, slice_xicap, XICAP_RESOURCE_GPI_SLICE, &excite_gpislice_resource),
	RINIT(0x0100, 0x02ff, XICAP_RESOURCE_FIFO_BLK, &excite_fifomem_resource),
	RINIT_IRQ(TITAN_IRQ,  XICAP_RESOURCE_IRQ)
};

static struct platform_device xicap_pdev = {
	.name		= XICAP_NAME,
	.id		= XICAP_UNIT,
	.num_resources	= ARRAY_SIZE(xicap_rsrc),
	.resource	= xicap_rsrc
};

/*
 * Create a platform device for the GPI port that receives the
 * image data from the embedded camera.
 */
static int __init xicap_devinit(void)
{
	unsigned long tend;
	u32 reg;
	int retval;

	adjust_resources(xicap_rsrc, ARRAY_SIZE(xicap_rsrc));

	/* Power up the slice and configure it. */
	reg = titan_readl(CPTC1R);
	reg &= ~(0x11100 << slice_xicap);
	titan_writel(reg, CPTC1R);

	/* Enable slice & DLL. */
	reg= titan_readl(CPRR);
	reg &= ~(0x00030003 << (slice_xicap * 2));
	titan_writel(reg, CPRR);

	/* Wait for DLLs to lock */
	tend = jiffies + DLL_TIMEOUT * HZ;
	while (time_before(jiffies, tend)) {
		if (!(~titan_readl(CPDSR) & (0x1 << (slice_xicap * 4))))
			break;
		yield();
	}

	if (~titan_readl(CPDSR) & (0x1 << (slice_xicap * 4))) {
		printk(KERN_ERR "%s: DLL not locked after %u seconds\n",
		       xicap_pdev.name, DLL_TIMEOUT);
		retval = -ETIME;
	} else {
		/* Register platform device */
		retval = platform_device_register(&xicap_pdev);
	}

	return retval;
}

device_initcall(xicap_devinit);
#endif /* defined(CONFIG_EXCITE_FCAP_GPI) || defined(CONFIG_EXCITE_FCAP_GPI_MODULE) */



#if defined(CONFIG_WDT_RM9K_GPI) || defined(CONFIG_WDT_RM9K_GPI_MODULE)
static struct resource wdt_rsrc[] = {
	RINIT(0, 0, WDT_RESOURCE_COUNTER, &excite_ctr_resource),
	RINIT(0x0084, 0x008f, WDT_RESOURCE_REGS, &excite_titan_resource),
	RINIT_IRQ(TITAN_IRQ,  WDT_RESOURCE_IRQ)
};

static struct platform_device wdt_pdev = {
	.name		= WDT_NAME,
	.id		= -1,
	.num_resources	= ARRAY_SIZE(wdt_rsrc),
	.resource	= wdt_rsrc
};

/*
 * Create a platform device for the GPI port that receives the
 * image data from the embedded camera.
 */
static int __init wdt_devinit(void)
{
	adjust_resources(wdt_rsrc, ARRAY_SIZE(wdt_rsrc));
	return platform_device_register(&wdt_pdev);
}

device_initcall(wdt_devinit);
#endif /* defined(CONFIG_WDT_RM9K_GPI) || defined(CONFIG_WDT_RM9K_GPI_MODULE) */



static struct resource excite_nandflash_rsrc[] = {
 	RINIT(0x2000, 0x201f, EXCITE_NANDFLASH_RESOURCE_REGS,  &excite_nand_resource)
};

static struct platform_device excite_nandflash_pdev = {
	.name		= "excite_nand",
	.id		= NAND_UNIT,
	.num_resources	= ARRAY_SIZE(excite_nandflash_rsrc),
	.resource	= excite_nandflash_rsrc
};

/*
 * Create a platform device for the access to the nand-flash
 * port
 */
static int __init excite_nandflash_devinit(void)
{
	adjust_resources(excite_nandflash_rsrc, ARRAY_SIZE(excite_nandflash_rsrc));

        /* nothing to be done here */

        /* Register platform device */
	return platform_device_register(&excite_nandflash_pdev);
}

device_initcall(excite_nandflash_devinit);



static struct resource iodev_rsrc[] = {
	RINIT_IRQ(FPGA1_IRQ,  IODEV_RESOURCE_IRQ)
};

static struct platform_device io_pdev = {
	.name		= IODEV_NAME,
	.id		= -1,
	.num_resources	= ARRAY_SIZE(iodev_rsrc),
	.resource	= iodev_rsrc
};

/*
 * Create a platform device for the external I/O ports.
 */
static int __init io_devinit(void)
{
	adjust_resources(iodev_rsrc, ARRAY_SIZE(iodev_rsrc));
	return platform_device_register(&io_pdev);
}

device_initcall(io_devinit);




#if defined(CONFIG_RM9K_GE) || defined(CONFIG_RM9K_GE_MODULE)
static struct resource rm9k_ge_rsrc[] = {
	RINIT(0x2200, 0x27ff, RM9K_GE_RESOURCE_MAC, &excite_titan_resource),
	RINIT(0x1800, 0x1fff, RM9K_GE_RESOURCE_MSTAT, &excite_titan_resource),
	RINIT(0x2000, 0x212f, RM9K_GE_RESOURCE_PKTPROC, &excite_titan_resource),
	RINIT(0x5140, 0x5227, RM9K_GE_RESOURCE_XDMA, &excite_titan_resource),
	RINIT(0x4870, 0x489f, RM9K_GE_RESOURCE_FIFO_RX, &excite_titan_resource),
	RINIT(0x494c, 0x4957, RM9K_GE_RESOURCE_FIFO_TX, &excite_titan_resource),
	RINIT(0x0000, 0x007f, RM9K_GE_RESOURCE_FIFOMEM_RX, &excite_fifomem_resource),
	RINIT(0x0080, 0x00ff, RM9K_GE_RESOURCE_FIFOMEM_TX, &excite_fifomem_resource),
	RINIT(0x0180, 0x019f, RM9K_GE_RESOURCE_PHY, &excite_titan_resource),
	RINIT(0x0000, 0x03ff, RM9K_GE_RESOURCE_DMADESC_RX, &excite_scram_resource),
	RINIT(0x0400, 0x07ff, RM9K_GE_RESOURCE_DMADESC_TX, &excite_scram_resource),
	RINIT(slice_eth, slice_eth, RM9K_GE_RESOURCE_GPI_SLICE, &excite_gpislice_resource),
	RINIT(0, 0, RM9K_GE_RESOURCE_MDIO_CHANNEL, &excite_mdio_channel_resource),
	RINIT_IRQ(TITAN_IRQ,  RM9K_GE_RESOURCE_IRQ_MAIN),
	RINIT_IRQ(PHY_IRQ, RM9K_GE_RESOURCE_IRQ_PHY)
};

static struct platform_device rm9k_ge_pdev = {
	.name		= RM9K_GE_NAME,
	.id		= RM9K_GE_UNIT,
	.num_resources	= ARRAY_SIZE(rm9k_ge_rsrc),
	.resource	= rm9k_ge_rsrc
};



/*
 * Create a platform device for the Ethernet port.
 */
static int __init rm9k_ge_devinit(void)
{
	u32 reg;

	adjust_resources(rm9k_ge_rsrc, ARRAY_SIZE(rm9k_ge_rsrc));

	/* Power up the slice and configure it. */
	reg = titan_readl(CPTC1R);
	reg &= ~(0x11000 << slice_eth);
	reg |= 0x100 << slice_eth;
	titan_writel(reg, CPTC1R);

	/* Take the MAC out of reset, reset the DLLs. */
	reg = titan_readl(CPRR);
	reg &= ~(0x00030000 << (slice_eth * 2));
	reg |= 0x3 << (slice_eth * 2);
	titan_writel(reg, CPRR);

	return platform_device_register(&rm9k_ge_pdev);
}

device_initcall(rm9k_ge_devinit);
#endif /* defined(CONFIG_RM9K_GE) || defined(CONFIG_RM9K_GE_MODULE) */



static int __init excite_setup_devs(void)
{
	int res;
	u32 reg;

	/* Enable xdma and fifo interrupts */
	reg = titan_readl(0x0050);
	titan_writel(reg | 0x18000000, 0x0050);

	res = request_resource(&iomem_resource, &excite_titan_resource);
	if (res)
		return res;
	res = request_resource(&iomem_resource, &excite_scram_resource);
	if (res)
		return res;
	res = request_resource(&iomem_resource, &excite_fpga_resource);
	if (res)
		return res;
	res = request_resource(&iomem_resource, &excite_nand_resource);
	if (res)
		return res;
	excite_fpga_resource.flags = excite_fpga_resource.parent->flags &
				   ( IORESOURCE_IO | IORESOURCE_MEM
				   | IORESOURCE_IRQ | IORESOURCE_DMA);
	excite_nand_resource.flags = excite_nand_resource.parent->flags &
				   ( IORESOURCE_IO | IORESOURCE_MEM
				   | IORESOURCE_IRQ | IORESOURCE_DMA);

	return 0;
}

arch_initcall(excite_setup_devs);

