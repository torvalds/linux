/*
 * SH7780 Setup
 *
 *  Copyright (C) 2006  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/io.h>
#include <linux/serial_sci.h>
#include <linux/sh_timer.h>
#include <asm/dma-sh.h>

static struct plat_sci_port scif0_platform_data = {
	.mapbase	= 0xffe00000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIF,
	.irqs		= { 40, 40, 40, 40 },
};

static struct platform_device scif0_device = {
	.name		= "sh-sci",
	.id		= 0,
	.dev		= {
		.platform_data	= &scif0_platform_data,
	},
};

static struct plat_sci_port scif1_platform_data = {
	.mapbase	= 0xffe10000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIF,
	.irqs		= { 76, 76, 76, 76 },
};

static struct platform_device scif1_device = {
	.name		= "sh-sci",
	.id		= 1,
	.dev		= {
		.platform_data	= &scif1_platform_data,
	},
};

static struct sh_timer_config tmu0_platform_data = {
	.name = "TMU0",
	.channel_offset = 0x04,
	.timer_bit = 0,
	.clk = "peripheral_clk",
	.clockevent_rating = 200,
};

static struct resource tmu0_resources[] = {
	[0] = {
		.name	= "TMU0",
		.start	= 0xffd80008,
		.end	= 0xffd80013,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 28,
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
	.name = "TMU1",
	.channel_offset = 0x10,
	.timer_bit = 1,
	.clk = "peripheral_clk",
	.clocksource_rating = 200,
};

static struct resource tmu1_resources[] = {
	[0] = {
		.name	= "TMU1",
		.start	= 0xffd80014,
		.end	= 0xffd8001f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 29,
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
	.name = "TMU2",
	.channel_offset = 0x1c,
	.timer_bit = 2,
	.clk = "peripheral_clk",
};

static struct resource tmu2_resources[] = {
	[0] = {
		.name	= "TMU2",
		.start	= 0xffd80020,
		.end	= 0xffd8002f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 30,
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

static struct sh_timer_config tmu3_platform_data = {
	.name = "TMU3",
	.channel_offset = 0x04,
	.timer_bit = 0,
	.clk = "peripheral_clk",
};

static struct resource tmu3_resources[] = {
	[0] = {
		.name	= "TMU3",
		.start	= 0xffdc0008,
		.end	= 0xffdc0013,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 96,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu3_device = {
	.name		= "sh_tmu",
	.id		= 3,
	.dev = {
		.platform_data	= &tmu3_platform_data,
	},
	.resource	= tmu3_resources,
	.num_resources	= ARRAY_SIZE(tmu3_resources),
};

static struct sh_timer_config tmu4_platform_data = {
	.name = "TMU4",
	.channel_offset = 0x10,
	.timer_bit = 1,
	.clk = "peripheral_clk",
};

static struct resource tmu4_resources[] = {
	[0] = {
		.name	= "TMU4",
		.start	= 0xffdc0014,
		.end	= 0xffdc001f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 97,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu4_device = {
	.name		= "sh_tmu",
	.id		= 4,
	.dev = {
		.platform_data	= &tmu4_platform_data,
	},
	.resource	= tmu4_resources,
	.num_resources	= ARRAY_SIZE(tmu4_resources),
};

static struct sh_timer_config tmu5_platform_data = {
	.name = "TMU5",
	.channel_offset = 0x1c,
	.timer_bit = 2,
	.clk = "peripheral_clk",
};

static struct resource tmu5_resources[] = {
	[0] = {
		.name	= "TMU5",
		.start	= 0xffdc0020,
		.end	= 0xffdc002b,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 98,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device tmu5_device = {
	.name		= "sh_tmu",
	.id		= 5,
	.dev = {
		.platform_data	= &tmu5_platform_data,
	},
	.resource	= tmu5_resources,
	.num_resources	= ARRAY_SIZE(tmu5_resources),
};

static struct resource rtc_resources[] = {
	[0] = {
		.start	= 0xffe80000,
		.end	= 0xffe80000 + 0x58 - 1,
		.flags	= IORESOURCE_IO,
	},
	[1] = {
		/* Shared Period/Carry/Alarm IRQ */
		.start	= 20,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device rtc_device = {
	.name		= "sh-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rtc_resources),
	.resource	= rtc_resources,
};

/* DMA */
static struct sh_dmae_channel sh7780_dmae0_channels[] = {
	{
		.offset = 0,
		.dmars = 0,
		.dmars_bit = 0,
	}, {
		.offset = 0x10,
		.dmars = 0,
		.dmars_bit = 8,
	}, {
		.offset = 0x20,
		.dmars = 4,
		.dmars_bit = 0,
	}, {
		.offset = 0x30,
		.dmars = 4,
		.dmars_bit = 8,
	}, {
		.offset = 0x50,
		.dmars = 8,
		.dmars_bit = 0,
	}, {
		.offset = 0x60,
		.dmars = 8,
		.dmars_bit = 8,
	}
};

static struct sh_dmae_channel sh7780_dmae1_channels[] = {
	{
		.offset = 0,
	}, {
		.offset = 0x10,
	}, {
		.offset = 0x20,
	}, {
		.offset = 0x30,
	}, {
		.offset = 0x50,
	}, {
		.offset = 0x60,
	}
};

static struct sh_dmae_pdata dma0_platform_data = {
	.channel	= sh7780_dmae0_channels,
	.channel_num	= ARRAY_SIZE(sh7780_dmae0_channels),
};

static struct sh_dmae_pdata dma1_platform_data = {
	.channel	= sh7780_dmae1_channels,
	.channel_num	= ARRAY_SIZE(sh7780_dmae1_channels),
};

static struct resource sh7780_dmae0_resources[] = {
	[0] = {
		/* Channel registers and DMAOR */
		.start	= 0xfc808020,
		.end	= 0xfc80808f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		/* DMARSx */
		.start	= 0xfc809000,
		.end	= 0xfc80900b,
		.flags	= IORESOURCE_MEM,
	},
	{
		/* Real DMA error IRQ is 38, and channel IRQs are 34-37, 44-45 */
		.start	= 34,
		.end	= 34,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_SHAREABLE,
	},
};

static struct resource sh7780_dmae1_resources[] = {
	[0] = {
		/* Channel registers and DMAOR */
		.start	= 0xfc818020,
		.end	= 0xfc81808f,
		.flags	= IORESOURCE_MEM,
	},
	/* DMAC1 has no DMARS */
	{
		/* Real DMA error IRQ is 38, and channel IRQs are 46-47, 92-95 */
		.start	= 46,
		.end	= 46,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_SHAREABLE,
	},
};

static struct platform_device dma0_device = {
	.name           = "sh-dma-engine",
	.id             = 0,
	.resource	= sh7780_dmae0_resources,
	.num_resources	= ARRAY_SIZE(sh7780_dmae0_resources),
	.dev            = {
		.platform_data	= &dma0_platform_data,
	},
};

static struct platform_device dma1_device = {
	.name		= "sh-dma-engine",
	.id		= 1,
	.resource	= sh7780_dmae1_resources,
	.num_resources	= ARRAY_SIZE(sh7780_dmae1_resources),
	.dev		= {
		.platform_data	= &dma1_platform_data,
	},
};

static struct platform_device *sh7780_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&tmu0_device,
	&tmu1_device,
	&tmu2_device,
	&tmu3_device,
	&tmu4_device,
	&tmu5_device,
	&rtc_device,
	&dma0_device,
	&dma1_device,
};

static int __init sh7780_devices_setup(void)
{
	return platform_add_devices(sh7780_devices,
				    ARRAY_SIZE(sh7780_devices));
}
arch_initcall(sh7780_devices_setup);
static struct platform_device *sh7780_early_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&tmu0_device,
	&tmu1_device,
	&tmu2_device,
	&tmu3_device,
	&tmu4_device,
	&tmu5_device,
};

void __init plat_early_device_setup(void)
{
	early_platform_add_devices(sh7780_early_devices,
				   ARRAY_SIZE(sh7780_early_devices));
}

enum {
	UNUSED = 0,

	/* interrupt sources */

	IRL_LLLL, IRL_LLLH, IRL_LLHL, IRL_LLHH,
	IRL_LHLL, IRL_LHLH, IRL_LHHL, IRL_LHHH,
	IRL_HLLL, IRL_HLLH, IRL_HLHL, IRL_HLHH,
	IRL_HHLL, IRL_HHLH, IRL_HHHL,

	IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7,
	RTC, WDT, TMU0, TMU1, TMU2, TMU2_TICPI,
	HUDI, DMAC0, SCIF0, DMAC1, CMT, HAC,
	PCISERR, PCIINTA, PCIINTB, PCIINTC, PCIINTD, PCIC5,
	SCIF1, SIOF, HSPI, MMCIF, TMU3, TMU4, TMU5, SSI, FLCTL,	GPIO,

	/* interrupt groups */

	TMU012,	TMU345,
};

static struct intc_vect vectors[] __initdata = {
	INTC_VECT(RTC, 0x480), INTC_VECT(RTC, 0x4a0),
	INTC_VECT(RTC, 0x4c0),
	INTC_VECT(WDT, 0x560),
	INTC_VECT(TMU0, 0x580), INTC_VECT(TMU1, 0x5a0),
	INTC_VECT(TMU2, 0x5c0), INTC_VECT(TMU2_TICPI, 0x5e0),
	INTC_VECT(HUDI, 0x600),
	INTC_VECT(DMAC0, 0x640), INTC_VECT(DMAC0, 0x660),
	INTC_VECT(DMAC0, 0x680), INTC_VECT(DMAC0, 0x6a0),
	INTC_VECT(DMAC0, 0x6c0),
	INTC_VECT(SCIF0, 0x700), INTC_VECT(SCIF0, 0x720),
	INTC_VECT(SCIF0, 0x740), INTC_VECT(SCIF0, 0x760),
	INTC_VECT(DMAC0, 0x780), INTC_VECT(DMAC0, 0x7a0),
	INTC_VECT(DMAC1, 0x7c0), INTC_VECT(DMAC1, 0x7e0),
	INTC_VECT(CMT, 0x900), INTC_VECT(HAC, 0x980),
	INTC_VECT(PCISERR, 0xa00), INTC_VECT(PCIINTA, 0xa20),
	INTC_VECT(PCIINTB, 0xa40), INTC_VECT(PCIINTC, 0xa60),
	INTC_VECT(PCIINTD, 0xa80), INTC_VECT(PCIC5, 0xaa0),
	INTC_VECT(PCIC5, 0xac0), INTC_VECT(PCIC5, 0xae0),
	INTC_VECT(PCIC5, 0xb00), INTC_VECT(PCIC5, 0xb20),
	INTC_VECT(SCIF1, 0xb80), INTC_VECT(SCIF1, 0xba0),
	INTC_VECT(SCIF1, 0xbc0), INTC_VECT(SCIF1, 0xbe0),
	INTC_VECT(SIOF, 0xc00), INTC_VECT(HSPI, 0xc80),
	INTC_VECT(MMCIF, 0xd00), INTC_VECT(MMCIF, 0xd20),
	INTC_VECT(MMCIF, 0xd40), INTC_VECT(MMCIF, 0xd60),
	INTC_VECT(DMAC1, 0xd80), INTC_VECT(DMAC1, 0xda0),
	INTC_VECT(DMAC1, 0xdc0), INTC_VECT(DMAC1, 0xde0),
	INTC_VECT(TMU3, 0xe00), INTC_VECT(TMU4, 0xe20),
	INTC_VECT(TMU5, 0xe40),
	INTC_VECT(SSI, 0xe80),
	INTC_VECT(FLCTL, 0xf00), INTC_VECT(FLCTL, 0xf20),
	INTC_VECT(FLCTL, 0xf40), INTC_VECT(FLCTL, 0xf60),
	INTC_VECT(GPIO, 0xf80), INTC_VECT(GPIO, 0xfa0),
	INTC_VECT(GPIO, 0xfc0), INTC_VECT(GPIO, 0xfe0),
};

static struct intc_group groups[] __initdata = {
	INTC_GROUP(TMU012, TMU0, TMU1, TMU2, TMU2_TICPI),
	INTC_GROUP(TMU345, TMU3, TMU4, TMU5),
};

static struct intc_mask_reg mask_registers[] __initdata = {
	{ 0xffd40038, 0xffd4003c, 32, /* INT2MSKR / INT2MSKCR */
	  { 0, 0, 0, 0, 0, 0, GPIO, FLCTL,
	    SSI, MMCIF, HSPI, SIOF, PCIC5, PCIINTD, PCIINTC, PCIINTB,
	    PCIINTA, PCISERR, HAC, CMT, 0, 0, DMAC1, DMAC0,
	    HUDI, 0, WDT, SCIF1, SCIF0, RTC, TMU345, TMU012 } },
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xffd40000, 0, 32, 8, /* INT2PRI0 */ { TMU0, TMU1,
						 TMU2, TMU2_TICPI } },
	{ 0xffd40004, 0, 32, 8, /* INT2PRI1 */ { TMU3, TMU4, TMU5, RTC } },
	{ 0xffd40008, 0, 32, 8, /* INT2PRI2 */ { SCIF0, SCIF1, WDT } },
	{ 0xffd4000c, 0, 32, 8, /* INT2PRI3 */ { HUDI, DMAC0, DMAC1 } },
	{ 0xffd40010, 0, 32, 8, /* INT2PRI4 */ { CMT, HAC,
						 PCISERR, PCIINTA, } },
	{ 0xffd40014, 0, 32, 8, /* INT2PRI5 */ { PCIINTB, PCIINTC,
						 PCIINTD, PCIC5 } },
	{ 0xffd40018, 0, 32, 8, /* INT2PRI6 */ { SIOF, HSPI, MMCIF, SSI } },
	{ 0xffd4001c, 0, 32, 8, /* INT2PRI7 */ { FLCTL, GPIO } },
};

static DECLARE_INTC_DESC(intc_desc, "sh7780", vectors, groups,
			 mask_registers, prio_registers, NULL);

/* Support for external interrupt pins in IRQ mode */

static struct intc_vect irq_vectors[] __initdata = {
	INTC_VECT(IRQ0, 0x240), INTC_VECT(IRQ1, 0x280),
	INTC_VECT(IRQ2, 0x2c0), INTC_VECT(IRQ3, 0x300),
	INTC_VECT(IRQ4, 0x340), INTC_VECT(IRQ5, 0x380),
	INTC_VECT(IRQ6, 0x3c0), INTC_VECT(IRQ7, 0x200),
};

static struct intc_mask_reg irq_mask_registers[] __initdata = {
	{ 0xffd00044, 0xffd00064, 32, /* INTMSK0 / INTMSKCLR0 */
	  { IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static struct intc_prio_reg irq_prio_registers[] __initdata = {
	{ 0xffd00010, 0, 32, 4, /* INTPRI */ { IRQ0, IRQ1, IRQ2, IRQ3,
					       IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static struct intc_sense_reg irq_sense_registers[] __initdata = {
	{ 0xffd0001c, 32, 2, /* ICR1 */   { IRQ0, IRQ1, IRQ2, IRQ3,
					    IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static struct intc_mask_reg irq_ack_registers[] __initdata = {
	{ 0xffd00024, 0, 32, /* INTREQ */
	  { IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static DECLARE_INTC_DESC_ACK(intc_irq_desc, "sh7780-irq", irq_vectors,
			     NULL, irq_mask_registers, irq_prio_registers,
			     irq_sense_registers, irq_ack_registers);

/* External interrupt pins in IRL mode */

static struct intc_vect irl_vectors[] __initdata = {
	INTC_VECT(IRL_LLLL, 0x200), INTC_VECT(IRL_LLLH, 0x220),
	INTC_VECT(IRL_LLHL, 0x240), INTC_VECT(IRL_LLHH, 0x260),
	INTC_VECT(IRL_LHLL, 0x280), INTC_VECT(IRL_LHLH, 0x2a0),
	INTC_VECT(IRL_LHHL, 0x2c0), INTC_VECT(IRL_LHHH, 0x2e0),
	INTC_VECT(IRL_HLLL, 0x300), INTC_VECT(IRL_HLLH, 0x320),
	INTC_VECT(IRL_HLHL, 0x340), INTC_VECT(IRL_HLHH, 0x360),
	INTC_VECT(IRL_HHLL, 0x380), INTC_VECT(IRL_HHLH, 0x3a0),
	INTC_VECT(IRL_HHHL, 0x3c0),
};

static struct intc_mask_reg irl3210_mask_registers[] __initdata = {
	{ 0xffd40080, 0xffd40084, 32, /* INTMSK2 / INTMSKCLR2 */
	  { IRL_LLLL, IRL_LLLH, IRL_LLHL, IRL_LLHH,
	    IRL_LHLL, IRL_LHLH, IRL_LHHL, IRL_LHHH,
	    IRL_HLLL, IRL_HLLH, IRL_HLHL, IRL_HLHH,
	    IRL_HHLL, IRL_HHLH, IRL_HHHL, } },
};

static struct intc_mask_reg irl7654_mask_registers[] __initdata = {
	{ 0xffd40080, 0xffd40084, 32, /* INTMSK2 / INTMSKCLR2 */
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	    IRL_LLLL, IRL_LLLH, IRL_LLHL, IRL_LLHH,
	    IRL_LHLL, IRL_LHLH, IRL_LHHL, IRL_LHHH,
	    IRL_HLLL, IRL_HLLH, IRL_HLHL, IRL_HLHH,
	    IRL_HHLL, IRL_HHLH, IRL_HHHL, } },
};

static DECLARE_INTC_DESC(intc_irl7654_desc, "sh7780-irl7654", irl_vectors,
			 NULL, irl7654_mask_registers, NULL, NULL);

static DECLARE_INTC_DESC(intc_irl3210_desc, "sh7780-irl3210", irl_vectors,
			 NULL, irl3210_mask_registers, NULL, NULL);

#define INTC_ICR0	0xffd00000
#define INTC_INTMSK0	0xffd00044
#define INTC_INTMSK1	0xffd00048
#define INTC_INTMSK2	0xffd40080
#define INTC_INTMSKCLR1	0xffd00068
#define INTC_INTMSKCLR2	0xffd40084

void __init plat_irq_setup(void)
{
	/* disable IRQ7-0 */
	__raw_writel(0xff000000, INTC_INTMSK0);

	/* disable IRL3-0 + IRL7-4 */
	__raw_writel(0xc0000000, INTC_INTMSK1);
	__raw_writel(0xfffefffe, INTC_INTMSK2);

	/* select IRL mode for IRL3-0 + IRL7-4 */
	__raw_writel(__raw_readl(INTC_ICR0) & ~0x00c00000, INTC_ICR0);

	/* disable holding function, ie enable "SH-4 Mode" */
	__raw_writel(__raw_readl(INTC_ICR0) | 0x00200000, INTC_ICR0);

	register_intc_controller(&intc_desc);
}

void __init plat_irq_setup_pins(int mode)
{
	switch (mode) {
	case IRQ_MODE_IRQ:
		/* select IRQ mode for IRL3-0 + IRL7-4 */
		__raw_writel(__raw_readl(INTC_ICR0) | 0x00c00000, INTC_ICR0);
		register_intc_controller(&intc_irq_desc);
		break;
	case IRQ_MODE_IRL7654:
		/* enable IRL7-4 but don't provide any masking */
		__raw_writel(0x40000000, INTC_INTMSKCLR1);
		__raw_writel(0x0000fffe, INTC_INTMSKCLR2);
		break;
	case IRQ_MODE_IRL3210:
		/* enable IRL0-3 but don't provide any masking */
		__raw_writel(0x80000000, INTC_INTMSKCLR1);
		__raw_writel(0xfffe0000, INTC_INTMSKCLR2);
		break;
	case IRQ_MODE_IRL7654_MASK:
		/* enable IRL7-4 and mask using cpu intc controller */
		__raw_writel(0x40000000, INTC_INTMSKCLR1);
		register_intc_controller(&intc_irl7654_desc);
		break;
	case IRQ_MODE_IRL3210_MASK:
		/* enable IRL0-3 and mask using cpu intc controller */
		__raw_writel(0x80000000, INTC_INTMSKCLR1);
		register_intc_controller(&intc_irl3210_desc);
		break;
	default:
		BUG();
	}
}
