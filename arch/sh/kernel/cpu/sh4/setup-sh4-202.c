/*
 * SH4-202 Setup
 *
 *  Copyright (C) 2006  Paul Mundt
 *  Copyright (C) 2009  Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/serial_sci.h>
#include <linux/sh_timer.h>
#include <linux/sh_intc.h>
#include <linux/io.h>

static struct plat_sci_port scif0_platform_data = {
	.mapbase	= 0xffe80000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE | SCSCR_REIE,
	.scbrr_algo_id	= SCBRR_ALGO_2,
	.type		= PORT_SCIF,
	.irqs		= { evt2irq(0x700),
			    evt2irq(0x720),
			    evt2irq(0x760),
			    evt2irq(0x740) },
};

static struct platform_device scif0_device = {
	.name		= "sh-sci",
	.id		= 0,
	.dev		= {
		.platform_data	= &scif0_platform_data,
	},
};

static struct sh_timer_config tmu0_platform_data = {
	.channel_offset = 0x04,
	.timer_bit = 0,
	.clockevent_rating = 200,
};

static struct resource tmu0_resources[] = {
	[0] = {
		.start	= 0xffd80008,
		.end	= 0xffd80013,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x400),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu0_device = {
	.name		= "sh_tmu",
	.id		= 0,
	.dev = {
		.platform_data	= &tmu0_platform_data,
	},
	.resource	= tmu0_resources,
	.num_resources	= ARRAY_SIZE(tmu0_resources),
};

static struct sh_timer_config tmu1_platform_data = {
	.channel_offset = 0x10,
	.timer_bit = 1,
	.clocksource_rating = 200,
};

static struct resource tmu1_resources[] = {
	[0] = {
		.start	= 0xffd80014,
		.end	= 0xffd8001f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x420),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu1_device = {
	.name		= "sh_tmu",
	.id		= 1,
	.dev = {
		.platform_data	= &tmu1_platform_data,
	},
	.resource	= tmu1_resources,
	.num_resources	= ARRAY_SIZE(tmu1_resources),
};

static struct sh_timer_config tmu2_platform_data = {
	.channel_offset = 0x1c,
	.timer_bit = 2,
};

static struct resource tmu2_resources[] = {
	[0] = {
		.start	= 0xffd80020,
		.end	= 0xffd8002f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x440),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu2_device = {
	.name		= "sh_tmu",
	.id		= 2,
	.dev = {
		.platform_data	= &tmu2_platform_data,
	},
	.resource	= tmu2_resources,
	.num_resources	= ARRAY_SIZE(tmu2_resources),
};

static struct platform_device *sh4202_devices[] __initdata = {
	&scif0_device,
	&tmu0_device,
	&tmu1_device,
	&tmu2_device,
};

static int __init sh4202_devices_setup(void)
{
	return platform_add_devices(sh4202_devices,
				    ARRAY_SIZE(sh4202_devices));
}
arch_initcall(sh4202_devices_setup);

static struct platform_device *sh4202_early_devices[] __initdata = {
	&scif0_device,
	&tmu0_device,
	&tmu1_device,
	&tmu2_device,
};

void __init plat_early_device_setup(void)
{
	early_platform_add_devices(sh4202_early_devices,
				   ARRAY_SIZE(sh4202_early_devices));
}

enum {
	UNUSED = 0,

	/* interrupt sources */
	IRL0, IRL1, IRL2, IRL3, /* only IRLM mode supported */
	HUDI, TMU0, TMU1, TMU2, RTC, SCIF, WDT,
};

static struct intc_vect vectors[] __initdata = {
	INTC_VECT(HUDI, 0x600),
	INTC_VECT(TMU0, 0x400), INTC_VECT(TMU1, 0x420),
	INTC_VECT(TMU2, 0x440), INTC_VECT(TMU2, 0x460),
	INTC_VECT(RTC, 0x480), INTC_VECT(RTC, 0x4a0),
	INTC_VECT(RTC, 0x4c0),
	INTC_VECT(SCIF, 0x700), INTC_VECT(SCIF, 0x720),
	INTC_VECT(SCIF, 0x740), INTC_VECT(SCIF, 0x760),
	INTC_VECT(WDT, 0x560),
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xffd00004, 0, 16, 4, /* IPRA */ { TMU0, TMU1, TMU2, RTC } },
	{ 0xffd00008, 0, 16, 4, /* IPRB */ { WDT, 0, 0, 0 } },
	{ 0xffd0000c, 0, 16, 4, /* IPRC */ { 0, 0, SCIF, HUDI } },
	{ 0xffd00010, 0, 16, 4, /* IPRD */ { IRL0, IRL1, IRL2, IRL3 } },
};

static DECLARE_INTC_DESC(intc_desc, "sh4-202", vectors, NULL,
			 NULL, prio_registers, NULL);

static struct intc_vect vectors_irlm[] __initdata = {
	INTC_VECT(IRL0, 0x240), INTC_VECT(IRL1, 0x2a0),
	INTC_VECT(IRL2, 0x300), INTC_VECT(IRL3, 0x360),
};

static DECLARE_INTC_DESC(intc_desc_irlm, "sh4-202_irlm", vectors_irlm, NULL,
			 NULL, prio_registers, NULL);

void __init plat_irq_setup(void)
{
	register_intc_controller(&intc_desc);
}

#define INTC_ICR	0xffd00000UL
#define INTC_ICR_IRLM   (1<<7)

void __init plat_irq_setup_pins(int mode)
{
	switch (mode) {
	case IRQ_MODE_IRQ: /* individual interrupt mode for IRL3-0 */
		__raw_writew(__raw_readw(INTC_ICR) | INTC_ICR_IRLM, INTC_ICR);
		register_intc_controller(&intc_desc_irlm);
		break;
	default:
		BUG();
	}
}
