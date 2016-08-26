/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqchip/mips-gic.h>
#include <linux/irqdomain.h>
#include <linux/leds.h>
#include <linux/mtd/physmap.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <asm/mips-boards/sead3int.h>

static struct mtd_partition sead3_mtd_partitions[] = {
	{
		.name =		"User FS",
		.offset =	0x00000000,
		.size =		0x01fc0000,
	}, {
		.name =		"Board Config",
		.offset =	0x01fc0000,
		.size =		0x00040000,
		.mask_flags =	MTD_WRITEABLE
	},
};

static struct physmap_flash_data sead3_flash_data = {
	.width		= 4,
	.nr_parts	= ARRAY_SIZE(sead3_mtd_partitions),
	.parts		= sead3_mtd_partitions
};

static struct resource sead3_flash_resource = {
	.start		= 0x1c000000,
	.end		= 0x1dffffff,
	.flags		= IORESOURCE_MEM
};

static struct platform_device sead3_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &sead3_flash_data,
	},
	.num_resources	= 1,
	.resource	= &sead3_flash_resource,
};

#define LEDFLAGS(bits, shift)		\
	((bits << 8) | (shift << 8))

#define LEDBITS(id, shift, bits)	\
	.name = id #shift,		\
	.flags = LEDFLAGS(bits, shift)

static struct led_info led_data_info[] = {
	{ LEDBITS("bit", 0, 1) },
	{ LEDBITS("bit", 1, 1) },
	{ LEDBITS("bit", 2, 1) },
	{ LEDBITS("bit", 3, 1) },
	{ LEDBITS("bit", 4, 1) },
	{ LEDBITS("bit", 5, 1) },
	{ LEDBITS("bit", 6, 1) },
	{ LEDBITS("bit", 7, 1) },
	{ LEDBITS("all", 0, 8) },
};

static struct led_platform_data led_data = {
	.num_leds	= ARRAY_SIZE(led_data_info),
	.leds		= led_data_info
};

static struct resource pled_resources[] = {
	{
		.start	= 0x1f000210,
		.end	= 0x1f000217,
		.flags	= IORESOURCE_MEM
	}
};

static struct platform_device pled_device = {
	.name			= "sead3::pled",
	.id			= 0,
	.dev			= {
		.platform_data	= &led_data,
	},
	.num_resources		= ARRAY_SIZE(pled_resources),
	.resource		= pled_resources
};


static struct resource fled_resources[] = {
	{
		.start			= 0x1f000218,
		.end			= 0x1f00021f,
		.flags			= IORESOURCE_MEM
	}
};

static struct platform_device fled_device = {
	.name			= "sead3::fled",
	.id			= 0,
	.dev			= {
		.platform_data	= &led_data,
	},
	.num_resources		= ARRAY_SIZE(fled_resources),
	.resource		= fled_resources
};

static struct platform_device sead3_led_device = {
        .name   = "sead3-led",
        .id     = -1,
};

static struct resource ehci_resources[] = {
	{
		.start			= 0x1b200000,
		.end			= 0x1b200fff,
		.flags			= IORESOURCE_MEM
	}, {
		.flags			= IORESOURCE_IRQ
	}
};

static u64 sead3_usbdev_dma_mask = DMA_BIT_MASK(32);

static struct platform_device ehci_device = {
	.name		= "sead3-ehci",
	.id		= 0,
	.dev		= {
		.dma_mask		= &sead3_usbdev_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32)
	},
	.num_resources	= ARRAY_SIZE(ehci_resources),
	.resource	= ehci_resources
};

static struct platform_device *sead3_platform_devices[] __initdata = {
	&sead3_flash,
	&pled_device,
	&fled_device,
	&sead3_led_device,
	&ehci_device,
};

static int __init sead3_platforms_device_init(void)
{
	const char *intc_compat;
	struct device_node *node;
	struct irq_domain *irqd;

	if (gic_present)
		intc_compat = "mti,gic";
	else
		intc_compat = "mti,cpu-interrupt-controller";

	node = of_find_compatible_node(NULL, NULL, intc_compat);
	if (!node) {
		pr_err("unable to find interrupt controller DT node\n");
		return -ENODEV;
	}

	irqd = irq_find_host(node);
	if (!irqd) {
		pr_err("unable to find interrupt controller IRQ domain\n");
		return -ENODEV;
	}

	if (gic_present) {
		ehci_resources[1].start =
			irq_create_mapping(irqd, GIC_INT_EHCI);
	} else {
		ehci_resources[1].start =
			irq_create_mapping(irqd, CPU_INT_EHCI);
	}

	return platform_add_devices(sead3_platform_devices,
				    ARRAY_SIZE(sead3_platform_devices));
}

device_initcall(sead3_platforms_device_init);
