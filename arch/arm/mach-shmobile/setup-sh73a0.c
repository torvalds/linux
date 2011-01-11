/*
 * sh73a0 processor support
 *
 * Copyright (C) 2010  Takashi Yoshii
 * Copyright (C) 2010  Magnus Damm
 * Copyright (C) 2008  Yoshihiro Shimoda
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
#include <linux/serial_sci.h>
#include <linux/sh_intc.h>
#include <linux/sh_timer.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

static struct plat_sci_port scif0_platform_data = {
	.mapbase	= 0xe6c40000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIFA,
	.irqs		= { gic_spi(72), gic_spi(72),
			    gic_spi(72), gic_spi(72) },
};

static struct platform_device scif0_device = {
	.name		= "sh-sci",
	.id		= 0,
	.dev		= {
		.platform_data	= &scif0_platform_data,
	},
};

static struct plat_sci_port scif1_platform_data = {
	.mapbase	= 0xe6c50000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIFA,
	.irqs		= { gic_spi(73), gic_spi(73),
			    gic_spi(73), gic_spi(73) },
};

static struct platform_device scif1_device = {
	.name		= "sh-sci",
	.id		= 1,
	.dev		= {
		.platform_data	= &scif1_platform_data,
	},
};

static struct plat_sci_port scif2_platform_data = {
	.mapbase	= 0xe6c60000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIFA,
	.irqs		= { gic_spi(74), gic_spi(74),
			    gic_spi(74), gic_spi(74) },
};

static struct platform_device scif2_device = {
	.name		= "sh-sci",
	.id		= 2,
	.dev		= {
		.platform_data	= &scif2_platform_data,
	},
};

static struct plat_sci_port scif3_platform_data = {
	.mapbase	= 0xe6c70000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIFA,
	.irqs		= { gic_spi(75), gic_spi(75),
			    gic_spi(75), gic_spi(75) },
};

static struct platform_device scif3_device = {
	.name		= "sh-sci",
	.id		= 3,
	.dev		= {
		.platform_data	= &scif3_platform_data,
	},
};

static struct plat_sci_port scif4_platform_data = {
	.mapbase	= 0xe6c80000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIFA,
	.irqs		= { gic_spi(78), gic_spi(78),
			    gic_spi(78), gic_spi(78) },
};

static struct platform_device scif4_device = {
	.name		= "sh-sci",
	.id		= 4,
	.dev		= {
		.platform_data	= &scif4_platform_data,
	},
};

static struct plat_sci_port scif5_platform_data = {
	.mapbase	= 0xe6cb0000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIFA,
	.irqs		= { gic_spi(79), gic_spi(79),
			    gic_spi(79), gic_spi(79) },
};

static struct platform_device scif5_device = {
	.name		= "sh-sci",
	.id		= 5,
	.dev		= {
		.platform_data	= &scif5_platform_data,
	},
};

static struct plat_sci_port scif6_platform_data = {
	.mapbase	= 0xe6cc0000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIFA,
	.irqs		= { gic_spi(156), gic_spi(156),
			    gic_spi(156), gic_spi(156) },
};

static struct platform_device scif6_device = {
	.name		= "sh-sci",
	.id		= 6,
	.dev		= {
		.platform_data	= &scif6_platform_data,
	},
};

static struct plat_sci_port scif7_platform_data = {
	.mapbase	= 0xe6cd0000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIFA,
	.irqs		= { gic_spi(143), gic_spi(143),
			    gic_spi(143), gic_spi(143) },
};

static struct platform_device scif7_device = {
	.name		= "sh-sci",
	.id		= 7,
	.dev		= {
		.platform_data	= &scif7_platform_data,
	},
};

static struct plat_sci_port scif8_platform_data = {
	.mapbase	= 0xe6c30000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIFB,
	.irqs		= { gic_spi(80), gic_spi(80),
			    gic_spi(80), gic_spi(80) },
};

static struct platform_device scif8_device = {
	.name		= "sh-sci",
	.id		= 8,
	.dev		= {
		.platform_data	= &scif8_platform_data,
	},
};

static struct sh_timer_config cmt10_platform_data = {
	.name = "CMT10",
	.channel_offset = 0x10,
	.timer_bit = 0,
	.clockevent_rating = 125,
	.clocksource_rating = 125,
};

static struct resource cmt10_resources[] = {
	[0] = {
		.name	= "CMT10",
		.start	= 0xe6138010,
		.end	= 0xe613801b,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(65),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device cmt10_device = {
	.name		= "sh_cmt",
	.id		= 10,
	.dev = {
		.platform_data	= &cmt10_platform_data,
	},
	.resource	= cmt10_resources,
	.num_resources	= ARRAY_SIZE(cmt10_resources),
};

/* TMU */
static struct sh_timer_config tmu00_platform_data = {
	.name = "TMU00",
	.channel_offset = 0x4,
	.timer_bit = 0,
	.clockevent_rating = 200,
};

static struct resource tmu00_resources[] = {
	[0] = {
		.name	= "TMU00",
		.start	= 0xfff60008,
		.end	= 0xfff60013,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= intcs_evt2irq(0x0e80), /* TMU0_TUNI00 */
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu00_device = {
	.name		= "sh_tmu",
	.id		= 0,
	.dev = {
		.platform_data	= &tmu00_platform_data,
	},
	.resource	= tmu00_resources,
	.num_resources	= ARRAY_SIZE(tmu00_resources),
};

static struct sh_timer_config tmu01_platform_data = {
	.name = "TMU01",
	.channel_offset = 0x10,
	.timer_bit = 1,
	.clocksource_rating = 200,
};

static struct resource tmu01_resources[] = {
	[0] = {
		.name	= "TMU01",
		.start	= 0xfff60014,
		.end	= 0xfff6001f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= intcs_evt2irq(0x0ea0), /* TMU0_TUNI01 */
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu01_device = {
	.name		= "sh_tmu",
	.id		= 1,
	.dev = {
		.platform_data	= &tmu01_platform_data,
	},
	.resource	= tmu01_resources,
	.num_resources	= ARRAY_SIZE(tmu01_resources),
};

static struct resource i2c0_resources[] = {
	[0] = {
		.name	= "IIC0",
		.start	= 0xe6820000,
		.end	= 0xe6820425 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(167),
		.end	= gic_spi(170),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource i2c1_resources[] = {
	[0] = {
		.name	= "IIC1",
		.start	= 0xe6822000,
		.end	= 0xe6822425 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(51),
		.end	= gic_spi(54),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource i2c2_resources[] = {
	[0] = {
		.name	= "IIC2",
		.start	= 0xe6824000,
		.end	= 0xe6824425 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(171),
		.end	= gic_spi(174),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource i2c3_resources[] = {
	[0] = {
		.name	= "IIC3",
		.start	= 0xe6826000,
		.end	= 0xe6826425 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(183),
		.end	= gic_spi(186),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource i2c4_resources[] = {
	[0] = {
		.name	= "IIC4",
		.start	= 0xe6828000,
		.end	= 0xe6828425 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(187),
		.end	= gic_spi(190),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device i2c0_device = {
	.name		= "i2c-sh_mobile",
	.id		= 0,
	.resource	= i2c0_resources,
	.num_resources	= ARRAY_SIZE(i2c0_resources),
};

static struct platform_device i2c1_device = {
	.name		= "i2c-sh_mobile",
	.id		= 1,
	.resource	= i2c1_resources,
	.num_resources	= ARRAY_SIZE(i2c1_resources),
};

static struct platform_device i2c2_device = {
	.name		= "i2c-sh_mobile",
	.id		= 2,
	.resource	= i2c2_resources,
	.num_resources	= ARRAY_SIZE(i2c2_resources),
};

static struct platform_device i2c3_device = {
	.name		= "i2c-sh_mobile",
	.id		= 3,
	.resource	= i2c3_resources,
	.num_resources	= ARRAY_SIZE(i2c3_resources),
};

static struct platform_device i2c4_device = {
	.name		= "i2c-sh_mobile",
	.id		= 4,
	.resource	= i2c4_resources,
	.num_resources	= ARRAY_SIZE(i2c4_resources),
};

static struct platform_device *sh73a0_early_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&scif4_device,
	&scif5_device,
	&scif6_device,
	&scif7_device,
	&scif8_device,
	&cmt10_device,
	&tmu00_device,
	&tmu01_device,
};

static struct platform_device *sh73a0_late_devices[] __initdata = {
	&i2c0_device,
	&i2c1_device,
	&i2c2_device,
	&i2c3_device,
	&i2c4_device,
};

void __init sh73a0_add_standard_devices(void)
{
	platform_add_devices(sh73a0_early_devices,
			    ARRAY_SIZE(sh73a0_early_devices));
	platform_add_devices(sh73a0_late_devices,
			    ARRAY_SIZE(sh73a0_late_devices));
}

void __init sh73a0_add_early_devices(void)
{
	early_platform_add_devices(sh73a0_early_devices,
				   ARRAY_SIZE(sh73a0_early_devices));
}
