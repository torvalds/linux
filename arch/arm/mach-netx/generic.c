/*
 * arch/arm/mach-netx/generic.c
 *
 * Copyright (C) 2005 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/irqchip/arm-vic.h>
#include <linux/reboot.h>
#include <mach/hardware.h>
#include <asm/mach/map.h>
#include <mach/netx-regs.h>
#include <asm/mach/irq.h>

static struct map_desc netx_io_desc[] __initdata = {
	{
		.virtual        = NETX_IO_VIRT,
		.pfn            = __phys_to_pfn(NETX_IO_PHYS),
		.length         = NETX_IO_SIZE,
		.type           = MT_DEVICE
	}
};

void __init netx_map_io(void)
{
	iotable_init(netx_io_desc, ARRAY_SIZE(netx_io_desc));
}

static struct resource netx_rtc_resources[] = {
	[0] = {
		.start	= 0x00101200,
		.end	= 0x00101220,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device netx_rtc_device = {
	.name		= "netx-rtc",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(netx_rtc_resources),
	.resource	= netx_rtc_resources,
};

static struct platform_device *devices[] __initdata = {
	&netx_rtc_device,
};

#if 0
#define DEBUG_IRQ(fmt...)	printk(fmt)
#else
#define DEBUG_IRQ(fmt...)	while (0) {}
#endif

static void
netx_hif_demux_handler(unsigned int irq_unused, struct irq_desc *desc)
{
	unsigned int irq = NETX_IRQ_HIF_CHAINED(0);
	unsigned int stat;

	stat = ((readl(NETX_DPMAS_INT_EN) &
		readl(NETX_DPMAS_INT_STAT)) >> 24) & 0x1f;

	while (stat) {
		if (stat & 1) {
			DEBUG_IRQ("handling irq %d\n", irq);
			generic_handle_irq(irq);
		}
		irq++;
		stat >>= 1;
	}
}

static int
netx_hif_irq_type(struct irq_data *d, unsigned int type)
{
	unsigned int val, irq;

	val = readl(NETX_DPMAS_IF_CONF1);

	irq = d->irq - NETX_IRQ_HIF_CHAINED(0);

	if (type & IRQ_TYPE_EDGE_RISING) {
		DEBUG_IRQ("rising edges\n");
		val |= (1 << 26) << irq;
	}
	if (type & IRQ_TYPE_EDGE_FALLING) {
		DEBUG_IRQ("falling edges\n");
		val &= ~((1 << 26) << irq);
	}
	if (type & IRQ_TYPE_LEVEL_LOW) {
		DEBUG_IRQ("low level\n");
		val &= ~((1 << 26) << irq);
	}
	if (type & IRQ_TYPE_LEVEL_HIGH) {
		DEBUG_IRQ("high level\n");
		val |= (1 << 26) << irq;
	}

	writel(val, NETX_DPMAS_IF_CONF1);

	return 0;
}

static void
netx_hif_ack_irq(struct irq_data *d)
{
	unsigned int val, irq;

	irq = d->irq - NETX_IRQ_HIF_CHAINED(0);
	writel((1 << 24) << irq, NETX_DPMAS_INT_STAT);

	val = readl(NETX_DPMAS_INT_EN);
	val &= ~((1 << 24) << irq);
	writel(val, NETX_DPMAS_INT_EN);

	DEBUG_IRQ("%s: irq %d\n", __func__, d->irq);
}

static void
netx_hif_mask_irq(struct irq_data *d)
{
	unsigned int val, irq;

	irq = d->irq - NETX_IRQ_HIF_CHAINED(0);
	val = readl(NETX_DPMAS_INT_EN);
	val &= ~((1 << 24) << irq);
	writel(val, NETX_DPMAS_INT_EN);
	DEBUG_IRQ("%s: irq %d\n", __func__, d->irq);
}

static void
netx_hif_unmask_irq(struct irq_data *d)
{
	unsigned int val, irq;

	irq = d->irq - NETX_IRQ_HIF_CHAINED(0);
	val = readl(NETX_DPMAS_INT_EN);
	val |= (1 << 24) << irq;
	writel(val, NETX_DPMAS_INT_EN);
	DEBUG_IRQ("%s: irq %d\n", __func__, d->irq);
}

static struct irq_chip netx_hif_chip = {
	.irq_ack = netx_hif_ack_irq,
	.irq_mask = netx_hif_mask_irq,
	.irq_unmask = netx_hif_unmask_irq,
	.irq_set_type = netx_hif_irq_type,
};

void __init netx_init_irq(void)
{
	int irq;

	vic_init(io_p2v(NETX_PA_VIC), NETX_IRQ_VIC_START, ~0, 0);

	for (irq = NETX_IRQ_HIF_CHAINED(0); irq <= NETX_IRQ_HIF_LAST; irq++) {
		irq_set_chip_and_handler(irq, &netx_hif_chip,
					 handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}

	writel(NETX_DPMAS_INT_EN_GLB_EN, NETX_DPMAS_INT_EN);
	irq_set_chained_handler(NETX_IRQ_HIF, netx_hif_demux_handler);
}

static int __init netx_init(void)
{
	return platform_add_devices(devices, ARRAY_SIZE(devices));
}

subsys_initcall(netx_init);

void netx_restart(enum reboot_mode mode, const char *cmd)
{
	writel(NETX_SYSTEM_RES_CR_FIRMW_RES_EN | NETX_SYSTEM_RES_CR_FIRMW_RES,
	       NETX_SYSTEM_RES_CR);
}
