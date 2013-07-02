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
#include <linux/platform_data/gpio-em.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/irqchip/arm-gic.h>
#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/emev2.h>
#include <mach/irqs.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

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


/* GIO */
static struct gpio_em_config gio0_config = {
	.gpio_base = 0,
	.irq_base = EMEV2_GPIO_IRQ(0),
	.number_of_pins = 32,
};

static struct resource gio0_resources[] = {
	[0] = {
		.name	= "GIO_000",
		.start	= 0xe0050000,
		.end	= 0xe005002b,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "GIO_000",
		.start	= 0xe0050040,
		.end	= 0xe005005f,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= 99,
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.start	= 100,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device gio0_device = {
	.name		= "em_gio",
	.id		= 0,
	.resource	= gio0_resources,
	.num_resources	= ARRAY_SIZE(gio0_resources),
	.dev		= {
		.platform_data	= &gio0_config,
	},
};

static struct gpio_em_config gio1_config = {
	.gpio_base = 32,
	.irq_base = EMEV2_GPIO_IRQ(32),
	.number_of_pins = 32,
};

static struct resource gio1_resources[] = {
	[0] = {
		.name	= "GIO_032",
		.start	= 0xe0050080,
		.end	= 0xe00500ab,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "GIO_032",
		.start	= 0xe00500c0,
		.end	= 0xe00500df,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= 101,
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.start	= 102,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device gio1_device = {
	.name		= "em_gio",
	.id		= 1,
	.resource	= gio1_resources,
	.num_resources	= ARRAY_SIZE(gio1_resources),
	.dev		= {
		.platform_data	= &gio1_config,
	},
};

static struct gpio_em_config gio2_config = {
	.gpio_base = 64,
	.irq_base = EMEV2_GPIO_IRQ(64),
	.number_of_pins = 32,
};

static struct resource gio2_resources[] = {
	[0] = {
		.name	= "GIO_064",
		.start	= 0xe0050100,
		.end	= 0xe005012b,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "GIO_064",
		.start	= 0xe0050140,
		.end	= 0xe005015f,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= 103,
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.start	= 104,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device gio2_device = {
	.name		= "em_gio",
	.id		= 2,
	.resource	= gio2_resources,
	.num_resources	= ARRAY_SIZE(gio2_resources),
	.dev		= {
		.platform_data	= &gio2_config,
	},
};

static struct gpio_em_config gio3_config = {
	.gpio_base = 96,
	.irq_base = EMEV2_GPIO_IRQ(96),
	.number_of_pins = 32,
};

static struct resource gio3_resources[] = {
	[0] = {
		.name	= "GIO_096",
		.start	= 0xe0050180,
		.end	= 0xe00501ab,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "GIO_096",
		.start	= 0xe00501c0,
		.end	= 0xe00501df,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= 105,
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.start	= 106,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device gio3_device = {
	.name		= "em_gio",
	.id		= 3,
	.resource	= gio3_resources,
	.num_resources	= ARRAY_SIZE(gio3_resources),
	.dev		= {
		.platform_data	= &gio3_config,
	},
};

static struct gpio_em_config gio4_config = {
	.gpio_base = 128,
	.irq_base = EMEV2_GPIO_IRQ(128),
	.number_of_pins = 31,
};

static struct resource gio4_resources[] = {
	[0] = {
		.name	= "GIO_128",
		.start	= 0xe0050200,
		.end	= 0xe005022b,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "GIO_128",
		.start	= 0xe0050240,
		.end	= 0xe005025f,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= 107,
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.start	= 108,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device gio4_device = {
	.name		= "em_gio",
	.id		= 4,
	.resource	= gio4_resources,
	.num_resources	= ARRAY_SIZE(gio4_resources),
	.dev		= {
		.platform_data	= &gio4_config,
	},
};

static struct resource pmu_resources[] = {
	[0] = {
		.start	= 152,
		.end	= 152,
		.flags	= IORESOURCE_IRQ,
	},
	[1] = {
		.start	= 153,
		.end	= 153,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device pmu_device = {
	.name		= "arm-pmu",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(pmu_resources),
	.resource	= pmu_resources,
};

static struct platform_device *emev2_devices[] __initdata = {
	&uart0_device,
	&uart1_device,
	&uart2_device,
	&uart3_device,
	&sti_device,
	&gio0_device,
	&gio1_device,
	&gio2_device,
	&gio3_device,
	&gio4_device,
	&pmu_device,
};

void __init emev2_add_standard_devices(void)
{
	emev2_clock_init();

	platform_add_devices(emev2_devices, ARRAY_SIZE(emev2_devices));
}

void __init emev2_init_delay(void)
{
	shmobile_setup_delay(533, 1, 3); /* Cortex-A9 @ 533MHz */
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

#ifdef CONFIG_USE_OF

static const char *emev2_boards_compat_dt[] __initdata = {
	"renesas,emev2",
	NULL,
};

DT_MACHINE_START(EMEV2_DT, "Generic Emma Mobile EV2 (Flattened Device Tree)")
	.smp		= smp_ops(emev2_smp_ops),
	.init_early	= emev2_init_delay,
	.nr_irqs	= NR_IRQS_LEGACY,
	.dt_compat	= emev2_boards_compat_dt,
MACHINE_END

#endif /* CONFIG_USE_OF */
