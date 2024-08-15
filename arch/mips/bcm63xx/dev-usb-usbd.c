/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 * Copyright (C) 2012 Kevin Cernekee <cernekee@gmail.com>
 * Copyright (C) 2012 Broadcom Corporation
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <bcm63xx_cpu.h>
#include <bcm63xx_dev_usb_usbd.h>

#define NUM_MMIO		2
#define NUM_IRQ			7

static struct resource usbd_resources[NUM_MMIO + NUM_IRQ];

static u64 usbd_dmamask = DMA_BIT_MASK(32);

static struct platform_device bcm63xx_usbd_device = {
	.name		= "bcm63xx_udc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(usbd_resources),
	.resource	= usbd_resources,
	.dev		= {
		.dma_mask		= &usbd_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

int __init bcm63xx_usbd_register(const struct bcm63xx_usbd_platform_data *pd)
{
	const int irq_list[NUM_IRQ] = { IRQ_USBD,
		IRQ_USBD_RXDMA0, IRQ_USBD_TXDMA0,
		IRQ_USBD_RXDMA1, IRQ_USBD_TXDMA1,
		IRQ_USBD_RXDMA2, IRQ_USBD_TXDMA2 };
	int i;

	if (!BCMCPU_IS_6328() && !BCMCPU_IS_6368())
		return 0;

	usbd_resources[0].start = bcm63xx_regset_address(RSET_USBD);
	usbd_resources[0].end = usbd_resources[0].start + RSET_USBD_SIZE - 1;
	usbd_resources[0].flags = IORESOURCE_MEM;

	usbd_resources[1].start = bcm63xx_regset_address(RSET_USBDMA);
	usbd_resources[1].end = usbd_resources[1].start + RSET_USBDMA_SIZE - 1;
	usbd_resources[1].flags = IORESOURCE_MEM;

	for (i = 0; i < NUM_IRQ; i++) {
		struct resource *r = &usbd_resources[NUM_MMIO + i];

		r->start = r->end = bcm63xx_get_irq_number(irq_list[i]);
		r->flags = IORESOURCE_IRQ;
	}

	platform_device_add_data(&bcm63xx_usbd_device, pd, sizeof(*pd));

	return platform_device_register(&bcm63xx_usbd_device);
}
