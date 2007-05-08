/*
 * iop13xx tpmi device resources
 * Copyright (c) 2005-2006, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/sizes.h>

/* assumes CONTROLLER_ONLY# is never asserted in the ESSR register */
#define IOP13XX_TPMI_MMR(dev) 	IOP13XX_REG_ADDR32_PHYS(0x48000 + (dev << 12))
#define IOP13XX_TPMI_MEM(dev) 	IOP13XX_REG_ADDR32_PHYS(0x60000 + (dev << 13))
#define IOP13XX_TPMI_CTRL(dev)	IOP13XX_REG_ADDR32_PHYS(0x50000 + (dev << 10))
#define IOP13XX_TPMI_MMR_SIZE	    (SZ_4K - 1)
#define IOP13XX_TPMI_MEM_SIZE	    (255)
#define IOP13XX_TPMI_MEM_CTRL	    (SZ_1K - 1)
#define IOP13XX_TPMI_RESOURCE_MMR  0
#define IOP13XX_TPMI_RESOURCE_MEM  1
#define IOP13XX_TPMI_RESOURCE_CTRL 2
#define IOP13XX_TPMI_RESOURCE_IRQ  3

static struct resource iop13xx_tpmi_0_resources[] = {
	[IOP13XX_TPMI_RESOURCE_MMR] = {
		.start = IOP13XX_TPMI_MMR(4), /* tpmi0 starts at dev == 4 */
		.end = IOP13XX_TPMI_MMR(4) + IOP13XX_TPMI_MMR_SIZE,
		.flags = IORESOURCE_MEM,
	},
	[IOP13XX_TPMI_RESOURCE_MEM] = {
		.start = IOP13XX_TPMI_MEM(0),
		.end = IOP13XX_TPMI_MEM(0) + IOP13XX_TPMI_MEM_SIZE,
		.flags = IORESOURCE_MEM,
	},
	[IOP13XX_TPMI_RESOURCE_CTRL] = {
		.start = IOP13XX_TPMI_CTRL(0),
		.end = IOP13XX_TPMI_CTRL(0) + IOP13XX_TPMI_MEM_CTRL,
		.flags = IORESOURCE_MEM,
	},
	[IOP13XX_TPMI_RESOURCE_IRQ] = {
		.start = IRQ_IOP13XX_TPMI0_OUT,
		.end = IRQ_IOP13XX_TPMI0_OUT,
		.flags = IORESOURCE_IRQ
	}
};

static struct resource iop13xx_tpmi_1_resources[] = {
	[IOP13XX_TPMI_RESOURCE_MMR] = {
		.start = IOP13XX_TPMI_MMR(1),
		.end = IOP13XX_TPMI_MMR(1) + IOP13XX_TPMI_MMR_SIZE,
		.flags = IORESOURCE_MEM,
	},
	[IOP13XX_TPMI_RESOURCE_MEM] = {
		.start = IOP13XX_TPMI_MEM(1),
		.end = IOP13XX_TPMI_MEM(1) + IOP13XX_TPMI_MEM_SIZE,
		.flags = IORESOURCE_MEM,
	},
	[IOP13XX_TPMI_RESOURCE_CTRL] = {
		.start = IOP13XX_TPMI_CTRL(1),
		.end = IOP13XX_TPMI_CTRL(1) + IOP13XX_TPMI_MEM_CTRL,
		.flags = IORESOURCE_MEM,
	},
	[IOP13XX_TPMI_RESOURCE_IRQ] = {
		.start = IRQ_IOP13XX_TPMI1_OUT,
		.end = IRQ_IOP13XX_TPMI1_OUT,
		.flags = IORESOURCE_IRQ
	}
};

static struct resource iop13xx_tpmi_2_resources[] = {
	[IOP13XX_TPMI_RESOURCE_MMR] = {
		.start = IOP13XX_TPMI_MMR(2),
		.end = IOP13XX_TPMI_MMR(2) + IOP13XX_TPMI_MMR_SIZE,
		.flags = IORESOURCE_MEM,
	},
	[IOP13XX_TPMI_RESOURCE_MEM] = {
		.start = IOP13XX_TPMI_MEM(2),
		.end = IOP13XX_TPMI_MEM(2) + IOP13XX_TPMI_MEM_SIZE,
		.flags = IORESOURCE_MEM,
	},
	[IOP13XX_TPMI_RESOURCE_CTRL] = {
		.start = IOP13XX_TPMI_CTRL(2),
		.end = IOP13XX_TPMI_CTRL(2) + IOP13XX_TPMI_MEM_CTRL,
		.flags = IORESOURCE_MEM,
	},
	[IOP13XX_TPMI_RESOURCE_IRQ] = {
		.start = IRQ_IOP13XX_TPMI2_OUT,
		.end = IRQ_IOP13XX_TPMI2_OUT,
		.flags = IORESOURCE_IRQ
	}
};

static struct resource iop13xx_tpmi_3_resources[] = {
	[IOP13XX_TPMI_RESOURCE_MMR] = {
		.start = IOP13XX_TPMI_MMR(3),
		.end = IOP13XX_TPMI_MMR(3) + IOP13XX_TPMI_MMR_SIZE,
		.flags = IORESOURCE_MEM,
	},
	[IOP13XX_TPMI_RESOURCE_MEM] = {
		.start = IOP13XX_TPMI_MEM(3),
		.end = IOP13XX_TPMI_MEM(3) + IOP13XX_TPMI_MEM_SIZE,
		.flags = IORESOURCE_MEM,
	},
	[IOP13XX_TPMI_RESOURCE_CTRL] = {
		.start = IOP13XX_TPMI_CTRL(3),
		.end = IOP13XX_TPMI_CTRL(3) + IOP13XX_TPMI_MEM_CTRL,
		.flags = IORESOURCE_MEM,
	},
	[IOP13XX_TPMI_RESOURCE_IRQ] = {
		.start = IRQ_IOP13XX_TPMI3_OUT,
		.end = IRQ_IOP13XX_TPMI3_OUT,
		.flags = IORESOURCE_IRQ
	}
};

u64 iop13xx_tpmi_mask = DMA_64BIT_MASK;
static struct platform_device iop13xx_tpmi_0_device = {
	.name = "iop-tpmi",
	.id = 0,
	.num_resources = 4,
	.resource = iop13xx_tpmi_0_resources,
	.dev = {
		.dma_mask          = &iop13xx_tpmi_mask,
		.coherent_dma_mask = DMA_64BIT_MASK,
	},
};

static struct platform_device iop13xx_tpmi_1_device = {
	.name = "iop-tpmi",
	.id = 1,
	.num_resources = 4,
	.resource = iop13xx_tpmi_1_resources,
	.dev = {
		.dma_mask          = &iop13xx_tpmi_mask,
		.coherent_dma_mask = DMA_64BIT_MASK,
	},
};

static struct platform_device iop13xx_tpmi_2_device = {
	.name = "iop-tpmi",
	.id = 2,
	.num_resources = 4,
	.resource = iop13xx_tpmi_2_resources,
	.dev = {
		.dma_mask          = &iop13xx_tpmi_mask,
		.coherent_dma_mask = DMA_64BIT_MASK,
	},
};

static struct platform_device iop13xx_tpmi_3_device = {
	.name = "iop-tpmi",
	.id = 3,
	.num_resources = 4,
	.resource = iop13xx_tpmi_3_resources,
	.dev = {
		.dma_mask          = &iop13xx_tpmi_mask,
		.coherent_dma_mask = DMA_64BIT_MASK,
	},
};

__init void iop13xx_add_tpmi_devices(void)
{
	unsigned short device_id;

	/* tpmi's not present on iop341 or iop342 */
	if (__raw_readl(IOP13XX_ESSR0) & IOP13XX_INTERFACE_SEL_PCIX)
		/* ATUE must be present */
		device_id = __raw_readw(IOP13XX_ATUE_DID);
	else
		/* ATUX must be present */
		device_id = __raw_readw(IOP13XX_ATUX_DID);

	switch (device_id) {
	/* iop34[1|2] 0-tpmi */
	case 0x3380:
	case 0x3384:
	case 0x3388:
	case 0x338c:
	case 0x3382:
	case 0x3386:
	case 0x338a:
	case 0x338e:
		return;
	/* iop348 1-tpmi */
	case 0x3310:
	case 0x3312:
	case 0x3314:
	case 0x3318:
	case 0x331a:
	case 0x331c:
	case 0x33c0:
	case 0x33c2:
	case 0x33c4:
	case 0x33c8:
	case 0x33ca:
	case 0x33cc:
	case 0x33b0:
	case 0x33b2:
	case 0x33b4:
	case 0x33b8:
	case 0x33ba:
	case 0x33bc:
	case 0x3320:
	case 0x3322:
	case 0x3324:
	case 0x3328:
	case 0x332a:
	case 0x332c:
		platform_device_register(&iop13xx_tpmi_0_device);
		return;
	default:
		platform_device_register(&iop13xx_tpmi_0_device);
		platform_device_register(&iop13xx_tpmi_1_device);
		platform_device_register(&iop13xx_tpmi_2_device);
		platform_device_register(&iop13xx_tpmi_3_device);
		return;
	}
}
