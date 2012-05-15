/*
 * R8A7740 processor support
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
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
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/serial_sci.h>
#include <linux/sh_timer.h>
#include <mach/r8a7740.h>
#include <mach/common.h>
#include <asm/mach-types.h>
#include <asm/mach/map.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

static struct map_desc r8a7740_io_desc[] __initdata = {
	 /*
	  * for CPGA/INTC/PFC
	  * 0xe6000000-0xefffffff -> 0xe6000000-0xefffffff
	  */
	{
		.virtual	= 0xe6000000,
		.pfn		= __phys_to_pfn(0xe6000000),
		.length		= 160 << 20,
		.type		= MT_DEVICE_NONSHARED
	},
#ifdef CONFIG_CACHE_L2X0
	/*
	 * for l2x0_init()
	 * 0xf0100000-0xf0101000 -> 0xf0002000-0xf0003000
	 */
	{
		.virtual	= 0xf0002000,
		.pfn		= __phys_to_pfn(0xf0100000),
		.length		= PAGE_SIZE,
		.type		= MT_DEVICE_NONSHARED
	},
#endif
};

void __init r8a7740_map_io(void)
{
	iotable_init(r8a7740_io_desc, ARRAY_SIZE(r8a7740_io_desc));
}

/* SCIFA0 */
static struct plat_sci_port scif0_platform_data = {
	.mapbase	= 0xe6c40000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= SCIx_IRQ_MUXED(evt2irq(0x0c00)),
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
	.irqs		= SCIx_IRQ_MUXED(evt2irq(0x0c20)),
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
	.irqs		= SCIx_IRQ_MUXED(evt2irq(0x0c40)),
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
	.irqs		= SCIx_IRQ_MUXED(evt2irq(0x0c60)),
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
	.irqs		= SCIx_IRQ_MUXED(evt2irq(0x0d20)),
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
	.irqs		= SCIx_IRQ_MUXED(evt2irq(0x0d40)),
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
	.irqs		= SCIx_IRQ_MUXED(evt2irq(0x04c0)),
};

static struct platform_device scif6_device = {
	.name		= "sh-sci",
	.id		= 6,
	.dev		= {
		.platform_data	= &scif6_platform_data,
	},
};

/* SCIFA7 */
static struct plat_sci_port scif7_platform_data = {
	.mapbase	= 0xe6cd0000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFA,
	.irqs		= SCIx_IRQ_MUXED(evt2irq(0x04e0)),
};

static struct platform_device scif7_device = {
	.name		= "sh-sci",
	.id		= 7,
	.dev		= {
		.platform_data	= &scif7_platform_data,
	},
};

/* SCIFB */
static struct plat_sci_port scifb_platform_data = {
	.mapbase	= 0xe6c30000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE,
	.scbrr_algo_id	= SCBRR_ALGO_4,
	.type		= PORT_SCIFB,
	.irqs		= SCIx_IRQ_MUXED(evt2irq(0x0d60)),
};

static struct platform_device scifb_device = {
	.name		= "sh-sci",
	.id		= 8,
	.dev		= {
		.platform_data	= &scifb_platform_data,
	},
};

/* CMT */
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
		.start	= evt2irq(0x0b00),
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

static struct platform_device *r8a7740_early_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&scif4_device,
	&scif5_device,
	&scif6_device,
	&scif7_device,
	&scifb_device,
	&cmt10_device,
};

/* I2C */
static struct resource i2c0_resources[] = {
	[0] = {
		.name	= "IIC0",
		.start	= 0xfff20000,
		.end	= 0xfff20425 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= intcs_evt2irq(0xe00),
		.end	= intcs_evt2irq(0xe60),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource i2c1_resources[] = {
	[0] = {
		.name	= "IIC1",
		.start	= 0xe6c20000,
		.end	= 0xe6c20425 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0x780), /* IIC1_ALI1 */
		.end    = evt2irq(0x7e0), /* IIC1_DTEI1 */
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

static struct platform_device *r8a7740_late_devices[] __initdata = {
	&i2c0_device,
	&i2c1_device,
};

#define ICCR	0x0004
#define ICSTART	0x0070

#define i2c_read(reg, offset)		ioread8(reg + offset)
#define i2c_write(reg, offset, data)	iowrite8(data, reg + offset)

/*
 * r8a7740 chip has lasting errata on I2C I/O pad reset.
 * this is work-around for it.
 */
static void r8a7740_i2c_workaround(struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *reg;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!res)) {
		pr_err("r8a7740 i2c workaround fail (cannot find resource)\n");
		return;
	}

	reg = ioremap(res->start, resource_size(res));
	if (unlikely(!reg)) {
		pr_err("r8a7740 i2c workaround fail (cannot map IO)\n");
		return;
	}

	i2c_write(reg, ICCR, i2c_read(reg, ICCR) | 0x80);
	i2c_read(reg, ICCR); /* dummy read */

	i2c_write(reg, ICSTART, i2c_read(reg, ICSTART) | 0x10);
	i2c_read(reg, ICSTART); /* dummy read */

	mdelay(100);

	i2c_write(reg, ICCR, 0x01);
	i2c_read(reg, ICCR);
	i2c_write(reg, ICSTART, 0x00);
	i2c_read(reg, ICSTART);

	i2c_write(reg, ICCR, 0x10);
	mdelay(100);
	i2c_write(reg, ICCR, 0x00);
	mdelay(100);
	i2c_write(reg, ICCR, 0x10);
	mdelay(100);

	iounmap(reg);
}

void __init r8a7740_add_standard_devices(void)
{
	/* I2C work-around */
	r8a7740_i2c_workaround(&i2c0_device);
	r8a7740_i2c_workaround(&i2c1_device);

	platform_add_devices(r8a7740_early_devices,
			    ARRAY_SIZE(r8a7740_early_devices));
	platform_add_devices(r8a7740_late_devices,
			     ARRAY_SIZE(r8a7740_late_devices));
}

static void __init r8a7740_earlytimer_init(void)
{
	r8a7740_clock_init(0);
	shmobile_earlytimer_init();
}

void __init r8a7740_add_early_devices(void)
{
	early_platform_add_devices(r8a7740_early_devices,
				   ARRAY_SIZE(r8a7740_early_devices));

	/* setup early console here as well */
	shmobile_setup_console();

	/* override timer setup with soc-specific code */
	shmobile_timer.init = r8a7740_earlytimer_init;
}
