/*
 * Emma Mobile EV2 processor support
 *
 * Copyright (C) 2012  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/io.h>
#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/emev2.h>
#include <mach/irqs.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/hardware/gic.h>

static struct map_desc emev2_io_desc[] __initdata = {
#ifdef CONFIG_SMP
	/* 128K entity map for 0xe0100000 (SMU) */
	{
		.virtual	= 0xe0100000,
		.pfn		= __phys_to_pfn(0xe0100000),
		.length		= SZ_128K,
		.type		= MT_DEVICE
	},
	/* 2M mapping for SCU + L2 controller */
	{
		.virtual	= 0xf0000000,
		.pfn		= __phys_to_pfn(0x1e000000),
		.length		= SZ_2M,
		.type		= MT_DEVICE
	},
#endif
};

void __init emev2_map_io(void)
{
	iotable_init(emev2_io_desc, ARRAY_SIZE(emev2_io_desc));
}

/* UART */
static struct resource uart0_resources[] = {
	[0]	= {
		.start	= 0xe1020000,
		.end	= 0xe1020037,
		.flags	= IORESOURCE_MEM,
	},
	[1]	= {
		.start	= 40,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device uart0_device = {
	.name		= "serial8250-em",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(uart0_resources),
	.resource	= uart0_resources,
};

static struct resource uart1_resources[] = {
	[0]	= {
		.start	= 0xe1030000,
		.end	= 0xe1030037,
		.flags	= IORESOURCE_MEM,
	},
	[1]	= {
		.start	= 41,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device uart1_device = {
	.name		= "serial8250-em",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(uart1_resources),
	.resource	= uart1_resources,
};

static struct resource uart2_resources[] = {
	[0]	= {
		.start	= 0xe1040000,
		.end	= 0xe1040037,
		.flags	= IORESOURCE_MEM,
	},
	[1]	= {
		.start	= 42,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device uart2_device = {
	.name		= "serial8250-em",
	.id		= 2,
	.num_resources	= ARRAY_SIZE(uart2_resources),
	.resource	= uart2_resources,
};

static struct resource uart3_resources[] = {
	[0]	= {
		.start	= 0xe1050000,
		.end	= 0xe1050037,
		.flags	= IORESOURCE_MEM,
	},
	[1]	= {
		.start	= 43,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device uart3_device = {
	.name		= "serial8250-em",
	.id		= 3,
	.num_resources	= ARRAY_SIZE(uart3_resources),
	.resource	= uart3_resources,
};

/* STI */
static struct resource sti_resources[] = {
	[0] = {
		.name	= "STI",
		.start	= 0xe0180000,
		.end	= 0xe0180053,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 157,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sti_device = {
	.name		= "em_sti",
	.id		= 0,
	.resource	= sti_resources,
	.num_resources	= ARRAY_SIZE(sti_resources),
};

static struct platform_device *emev2_early_devices[] __initdata = {
	&uart0_device,
	&uart1_device,
	&uart2_device,
	&uart3_device,
};

static struct platform_device *emev2_late_devices[] __initdata = {
	&sti_device,
};

void __init emev2_add_standard_devices(void)
{
	emev2_clock_init();

	platform_add_devices(emev2_early_devices,
			     ARRAY_SIZE(emev2_early_devices));

	platform_add_devices(emev2_late_devices,
			     ARRAY_SIZE(emev2_late_devices));
}

void __init emev2_add_early_devices(void)
{
	shmobile_setup_delay(533, 1, 3); /* Cortex-A9 @ 533MHz */

	early_platform_add_devices(emev2_early_devices,
				   ARRAY_SIZE(emev2_early_devices));

	/* setup early console here as well */
	shmobile_setup_console();
}

void __init emev2_init_irq(void)
{
	void __iomem *gic_dist_base;
	void __iomem *gic_cpu_base;

	/* Static mappings, never released */
	gic_dist_base = ioremap(0xe0028000, PAGE_SIZE);
	gic_cpu_base = ioremap(0xe0020000, PAGE_SIZE);
	BUG_ON(!gic_dist_base || !gic_cpu_base);

	/* Use GIC to handle interrupts */
	gic_init(0, 29, gic_dist_base, gic_cpu_base);
}
