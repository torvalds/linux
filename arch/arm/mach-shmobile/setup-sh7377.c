/*
 * sh7377 processor support
 *
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
#include <linux/uio_driver.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/serial_sci.h>
#include <linux/sh_intc.h>
#include <linux/sh_timer.h>
#include <mach/hardware.h>
#include <mach/common.h>
#include <asm/mach/map.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

static struct map_desc sh7377_io_desc[] __initdata = {
	/* create a 1:1 entity map for 0xe6xxxxxx
	 * used by CPGA, INTC and PFC.
	 */
	{
		.virtual	= 0xe6000000,
		.pfn		= __phys_to_pfn(0xe6000000),
		.length		= 256 << 20,
		.type		= MT_DEVICE_NONSHARED
	},
};

void __init sh7377_map_io(void)
{
	iotable_init(sh7377_io_desc, ARRAY_SIZE(sh7377_io_desc));
}

/* SCIFA0 */
static struct plat_sci_port scif0_platform_data = {
	.mapbase	= 0xe6c40000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { evt2irq(0xc00), evt2irq(0xc00),
			    evt2irq(0xc00), evt2irq(0xc00) },
};

static struct platform_device scif0_device = {
	.name		= "sh-sci",
	.id		= 0,
	.dev		= {
		.platform_data	= &scif0_platform_data,
	},
};

/* SCIFA1 */
static struct plat_sci_port scif1_platform_data = {
	.mapbase	= 0xe6c50000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { evt2irq(0xc20), evt2irq(0xc20),
			    evt2irq(0xc20), evt2irq(0xc20) },
};

static struct platform_device scif1_device = {
	.name		= "sh-sci",
	.id		= 1,
	.dev		= {
		.platform_data	= &scif1_platform_data,
	},
};

/* SCIFA2 */
static struct plat_sci_port scif2_platform_data = {
	.mapbase	= 0xe6c60000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { evt2irq(0xc40), evt2irq(0xc40),
			    evt2irq(0xc40), evt2irq(0xc40) },
};

static struct platform_device scif2_device = {
	.name		= "sh-sci",
	.id		= 2,
	.dev		= {
		.platform_data	= &scif2_platform_data,
	},
};

/* SCIFA3 */
static struct plat_sci_port scif3_platform_data = {
	.mapbase	= 0xe6c70000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { evt2irq(0xc60), evt2irq(0xc60),
			    evt2irq(0xc60), evt2irq(0xc60) },
};

static struct platform_device scif3_device = {
	.name		= "sh-sci",
	.id		= 3,
	.dev		= {
		.platform_data	= &scif3_platform_data,
	},
};

/* SCIFA4 */
static struct plat_sci_port scif4_platform_data = {
	.mapbase	= 0xe6c80000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { evt2irq(0xd20), evt2irq(0xd20),
			    evt2irq(0xd20), evt2irq(0xd20) },
};

static struct platform_device scif4_device = {
	.name		= "sh-sci",
	.id		= 4,
	.dev		= {
		.platform_data	= &scif4_platform_data,
	},
};

/* SCIFA5 */
static struct plat_sci_port scif5_platform_data = {
	.mapbase	= 0xe6cb0000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { evt2irq(0xd40), evt2irq(0xd40),
			    evt2irq(0xd40), evt2irq(0xd40) },
};

static struct platform_device scif5_device = {
	.name		= "sh-sci",
	.id		= 5,
	.dev		= {
		.platform_data	= &scif5_platform_data,
	},
};

/* SCIFA6 */
static struct plat_sci_port scif6_platform_data = {
	.mapbase	= 0xe6cc0000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= { intcs_evt2irq(0x1a80), intcs_evt2irq(0x1a80),
			    intcs_evt2irq(0x1a80), intcs_evt2irq(0x1a80) },
};

static struct platform_device scif6_device = {
	.name		= "sh-sci",
	.id		= 6,
	.dev		= {
		.platform_data	= &scif6_platform_data,
	},
};

/* SCIFB */
static struct plat_sci_port scif7_platform_data = {
	.mapbase	= 0xe6c30000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFB,
	.irqs		= { evt2irq(0xd60), evt2irq(0xd60),
			    evt2irq(0xd60), evt2irq(0xd60) },
};

static struct platform_device scif7_device = {
	.name		= "sh-sci",
	.id		= 7,
	.dev		= {
		.platform_data	= &scif7_platform_data,
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
		.start	= evt2irq(0xb00), /* CMT1_CMT10 */
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

/* VPU */
static struct uio_info vpu_platform_data = {
	.name = "VPU5HG",
	.version = "0",
	.irq = intcs_evt2irq(0x980),
};

static struct resource vpu_resources[] = {
	[0] = {
		.name	= "VPU",
		.start	= 0xfe900000,
		.end	= 0xfe900157,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device vpu_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 0,
	.dev = {
		.platform_data	= &vpu_platform_data,
	},
	.resource	= vpu_resources,
	.num_resources	= ARRAY_SIZE(vpu_resources),
};

/* VEU0 */
static struct uio_info veu0_platform_data = {
	.name = "VEU0",
	.version = "0",
	.irq = intcs_evt2irq(0x700),
};

static struct resource veu0_resources[] = {
	[0] = {
		.name	= "VEU0",
		.start	= 0xfe920000,
		.end	= 0xfe9200cb,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device veu0_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 1,
	.dev = {
		.platform_data	= &veu0_platform_data,
	},
	.resource	= veu0_resources,
	.num_resources	= ARRAY_SIZE(veu0_resources),
};

/* VEU1 */
static struct uio_info veu1_platform_data = {
	.name = "VEU1",
	.version = "0",
	.irq = intcs_evt2irq(0x720),
};

static struct resource veu1_resources[] = {
	[0] = {
		.name	= "VEU1",
		.start	= 0xfe924000,
		.end	= 0xfe9240cb,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device veu1_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 2,
	.dev = {
		.platform_data	= &veu1_platform_data,
	},
	.resource	= veu1_resources,
	.num_resources	= ARRAY_SIZE(veu1_resources),
};

/* VEU2 */
static struct uio_info veu2_platform_data = {
	.name = "VEU2",
	.version = "0",
	.irq = intcs_evt2irq(0x740),
};

static struct resource veu2_resources[] = {
	[0] = {
		.name	= "VEU2",
		.start	= 0xfe928000,
		.end	= 0xfe928307,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device veu2_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 3,
	.dev = {
		.platform_data	= &veu2_platform_data,
	},
	.resource	= veu2_resources,
	.num_resources	= ARRAY_SIZE(veu2_resources),
};

/* VEU3 */
static struct uio_info veu3_platform_data = {
	.name = "VEU3",
	.version = "0",
	.irq = intcs_evt2irq(0x760),
};

static struct resource veu3_resources[] = {
	[0] = {
		.name	= "VEU3",
		.start	= 0xfe92c000,
		.end	= 0xfe92c307,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device veu3_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 4,
	.dev = {
		.platform_data	= &veu3_platform_data,
	},
	.resource	= veu3_resources,
	.num_resources	= ARRAY_SIZE(veu3_resources),
};

/* JPU */
static struct uio_info jpu_platform_data = {
	.name = "JPU",
	.version = "0",
	.irq = intcs_evt2irq(0x560),
};

static struct resource jpu_resources[] = {
	[0] = {
		.name	= "JPU",
		.start	= 0xfe980000,
		.end	= 0xfe9902d3,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device jpu_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 5,
	.dev = {
		.platform_data	= &jpu_platform_data,
	},
	.resource	= jpu_resources,
	.num_resources	= ARRAY_SIZE(jpu_resources),
};

/* SPU2DSP0 */
static struct uio_info spu0_platform_data = {
	.name = "SPU2DSP0",
	.version = "0",
	.irq = evt2irq(0x1800),
};

static struct resource spu0_resources[] = {
	[0] = {
		.name	= "SPU2DSP0",
		.start	= 0xfe200000,
		.end	= 0xfe2fffff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device spu0_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 6,
	.dev = {
		.platform_data	= &spu0_platform_data,
	},
	.resource	= spu0_resources,
	.num_resources	= ARRAY_SIZE(spu0_resources),
};

/* SPU2DSP1 */
static struct uio_info spu1_platform_data = {
	.name = "SPU2DSP1",
	.version = "0",
	.irq = evt2irq(0x1820),
};

static struct resource spu1_resources[] = {
	[0] = {
		.name	= "SPU2DSP1",
		.start	= 0xfe300000,
		.end	= 0xfe3fffff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device spu1_device = {
	.name		= "uio_pdrv_genirq",
	.id		= 7,
	.dev = {
		.platform_data	= &spu1_platform_data,
	},
	.resource	= spu1_resources,
	.num_resources	= ARRAY_SIZE(spu1_resources),
};

static struct platform_device *sh7377_early_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&scif4_device,
	&scif5_device,
	&scif6_device,
	&scif7_device,
	&cmt10_device,
};

static struct platform_device *sh7377_devices[] __initdata = {
	&vpu_device,
	&veu0_device,
	&veu1_device,
	&veu2_device,
	&veu3_device,
	&jpu_device,
	&spu0_device,
	&spu1_device,
};

void __init sh7377_add_standard_devices(void)
{
	platform_add_devices(sh7377_early_devices,
			    ARRAY_SIZE(sh7377_early_devices));

	platform_add_devices(sh7377_devices,
			    ARRAY_SIZE(sh7377_devices));
}

static void __init sh7377_earlytimer_init(void)
{
	sh7377_clock_init();
	shmobile_earlytimer_init();
}

#define SMSTPCR3 0xe615013c
#define SMSTPCR3_CMT1 (1 << 29)

void __init sh7377_add_early_devices(void)
{
	/* enable clock to CMT1 */
	__raw_writel(__raw_readl(SMSTPCR3) & ~SMSTPCR3_CMT1, SMSTPCR3);

	early_platform_add_devices(sh7377_early_devices,
				   ARRAY_SIZE(sh7377_early_devices));

	/* setup early console here as well */
	shmobile_setup_console();

	/* override timer setup with soc-specific code */
	shmobile_timer.init = sh7377_earlytimer_init;
}
