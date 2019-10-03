// SPDX-License-Identifier: GPL-2.0
/*
 * SH7785 Setup
 *
 *  Copyright (C) 2007  Paul Mundt
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/serial_sci.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/sh_dma.h>
#include <linux/sh_timer.h>
#include <linux/sh_intc.h>
#include <asm/mmzone.h>
#include <asm/platform_early.h>
#include <cpu/dma-register.h>

static struct plat_sci_port scif0_platform_data = {
	.scscr		= SCSCR_REIE | SCSCR_CKE1,
	.type		= PORT_SCIF,
	.regtype	= SCIx_SH4_SCIF_FIFODATA_REGTYPE,
};

static struct resource scif0_resources[] = {
	DEFINE_RES_MEM(0xffea0000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0x700)),
};

static struct platform_device scif0_device = {
	.name		= "sh-sci",
	.id		= 0,
	.resource	= scif0_resources,
	.num_resources	= ARRAY_SIZE(scif0_resources),
	.dev		= {
		.platform_data	= &scif0_platform_data,
	},
};

static struct plat_sci_port scif1_platform_data = {
	.scscr		= SCSCR_REIE | SCSCR_CKE1,
	.type		= PORT_SCIF,
	.regtype	= SCIx_SH4_SCIF_FIFODATA_REGTYPE,
};

static struct resource scif1_resources[] = {
	DEFINE_RES_MEM(0xffeb0000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0x780)),
};

static struct platform_device scif1_device = {
	.name		= "sh-sci",
	.id		= 1,
	.resource	= scif1_resources,
	.num_resources	= ARRAY_SIZE(scif1_resources),
	.dev		= {
		.platform_data	= &scif1_platform_data,
	},
};

static struct plat_sci_port scif2_platform_data = {
	.scscr		= SCSCR_REIE | SCSCR_CKE1,
	.type		= PORT_SCIF,
	.regtype	= SCIx_SH4_SCIF_FIFODATA_REGTYPE,
};

static struct resource scif2_resources[] = {
	DEFINE_RES_MEM(0xffec0000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0x980)),
};

static struct platform_device scif2_device = {
	.name		= "sh-sci",
	.id		= 2,
	.resource	= scif2_resources,
	.num_resources	= ARRAY_SIZE(scif2_resources),
	.dev		= {
		.platform_data	= &scif2_platform_data,
	},
};

static struct plat_sci_port scif3_platform_data = {
	.scscr		= SCSCR_REIE | SCSCR_CKE1,
	.type		= PORT_SCIF,
	.regtype	= SCIx_SH4_SCIF_FIFODATA_REGTYPE,
};

static struct resource scif3_resources[] = {
	DEFINE_RES_MEM(0xffed0000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0x9a0)),
};

static struct platform_device scif3_device = {
	.name		= "sh-sci",
	.id		= 3,
	.resource	= scif3_resources,
	.num_resources	= ARRAY_SIZE(scif3_resources),
	.dev		= {
		.platform_data	= &scif3_platform_data,
	},
};

static struct plat_sci_port scif4_platform_data = {
	.scscr		= SCSCR_REIE | SCSCR_CKE1,
	.type		= PORT_SCIF,
	.regtype	= SCIx_SH4_SCIF_FIFODATA_REGTYPE,
};

static struct resource scif4_resources[] = {
	DEFINE_RES_MEM(0xffee0000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0x9c0)),
};

static struct platform_device scif4_device = {
	.name		= "sh-sci",
	.id		= 4,
	.resource	= scif4_resources,
	.num_resources	= ARRAY_SIZE(scif4_resources),
	.dev		= {
		.platform_data	= &scif4_platform_data,
	},
};

static struct plat_sci_port scif5_platform_data = {
	.scscr		= SCSCR_REIE | SCSCR_CKE1,
	.type		= PORT_SCIF,
	.regtype	= SCIx_SH4_SCIF_FIFODATA_REGTYPE,
};

static struct resource scif5_resources[] = {
	DEFINE_RES_MEM(0xffef0000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0x9e0)),
};

static struct platform_device scif5_device = {
	.name		= "sh-sci",
	.id		= 5,
	.resource	= scif5_resources,
	.num_resources	= ARRAY_SIZE(scif5_resources),
	.dev		= {
		.platform_data	= &scif5_platform_data,
	},
};

static struct sh_timer_config tmu0_platform_data = {
	.channels_mask = 7,
};

static struct resource tmu0_resources[] = {
	DEFINE_RES_MEM(0xffd80000, 0x30),
	DEFINE_RES_IRQ(evt2irq(0x580)),
	DEFINE_RES_IRQ(evt2irq(0x5a0)),
	DEFINE_RES_IRQ(evt2irq(0x5c0)),
};

static struct platform_device tmu0_device = {
	.name		= "sh-tmu",
	.id		= 0,
	.dev = {
		.platform_data	= &tmu0_platform_data,
	},
	.resource	= tmu0_resources,
	.num_resources	= ARRAY_SIZE(tmu0_resources),
};

static struct sh_timer_config tmu1_platform_data = {
	.channels_mask = 7,
};

static struct resource tmu1_resources[] = {
	DEFINE_RES_MEM(0xffdc0000, 0x2c),
	DEFINE_RES_IRQ(evt2irq(0xe00)),
	DEFINE_RES_IRQ(evt2irq(0xe20)),
	DEFINE_RES_IRQ(evt2irq(0xe40)),
};

static struct platform_device tmu1_device = {
	.name		= "sh-tmu",
	.id		= 1,
	.dev = {
		.platform_data	= &tmu1_platform_data,
	},
	.resource	= tmu1_resources,
	.num_resources	= ARRAY_SIZE(tmu1_resources),
};

/* DMA */
static const struct sh_dmae_channel sh7785_dmae0_channels[] = {
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

static const struct sh_dmae_channel sh7785_dmae1_channels[] = {
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

static const unsigned int ts_shift[] = TS_SHIFT;

static struct sh_dmae_pdata dma0_platform_data = {
	.channel	= sh7785_dmae0_channels,
	.channel_num	= ARRAY_SIZE(sh7785_dmae0_channels),
	.ts_low_shift	= CHCR_TS_LOW_SHIFT,
	.ts_low_mask	= CHCR_TS_LOW_MASK,
	.ts_high_shift	= CHCR_TS_HIGH_SHIFT,
	.ts_high_mask	= CHCR_TS_HIGH_MASK,
	.ts_shift	= ts_shift,
	.ts_shift_num	= ARRAY_SIZE(ts_shift),
	.dmaor_init	= DMAOR_INIT,
};

static struct sh_dmae_pdata dma1_platform_data = {
	.channel	= sh7785_dmae1_channels,
	.channel_num	= ARRAY_SIZE(sh7785_dmae1_channels),
	.ts_low_shift	= CHCR_TS_LOW_SHIFT,
	.ts_low_mask	= CHCR_TS_LOW_MASK,
	.ts_high_shift	= CHCR_TS_HIGH_SHIFT,
	.ts_high_mask	= CHCR_TS_HIGH_MASK,
	.ts_shift	= ts_shift,
	.ts_shift_num	= ARRAY_SIZE(ts_shift),
	.dmaor_init	= DMAOR_INIT,
};

static struct resource sh7785_dmae0_resources[] = {
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
		/*
		 * Real DMA error vector is 0x6e0, and channel
		 * vectors are 0x620-0x6c0
		 */
		.name	= "error_irq",
		.start	= evt2irq(0x620),
		.end	= evt2irq(0x620),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_SHAREABLE,
	},
};

static struct resource sh7785_dmae1_resources[] = {
	[0] = {
		/* Channel registers and DMAOR */
		.start	= 0xfcc08020,
		.end	= 0xfcc0808f,
		.flags	= IORESOURCE_MEM,
	},
	/* DMAC1 has no DMARS */
	{
		/*
		 * Real DMA error vector is 0x940, and channel
		 * vectors are 0x880-0x920
		 */
		.name	= "error_irq",
		.start	= evt2irq(0x880),
		.end	= evt2irq(0x880),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_SHAREABLE,
	},
};

static struct platform_device dma0_device = {
	.name           = "sh-dma-engine",
	.id             = 0,
	.resource	= sh7785_dmae0_resources,
	.num_resources	= ARRAY_SIZE(sh7785_dmae0_resources),
	.dev            = {
		.platform_data	= &dma0_platform_data,
	},
};

static struct platform_device dma1_device = {
	.name		= "sh-dma-engine",
	.id		= 1,
	.resource	= sh7785_dmae1_resources,
	.num_resources	= ARRAY_SIZE(sh7785_dmae1_resources),
	.dev		= {
		.platform_data	= &dma1_platform_data,
	},
};

static struct platform_device *sh7785_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&scif4_device,
	&scif5_device,
	&tmu0_device,
	&tmu1_device,
	&dma0_device,
	&dma1_device,
};

static int __init sh7785_devices_setup(void)
{
	return platform_add_devices(sh7785_devices,
				    ARRAY_SIZE(sh7785_devices));
}
arch_initcall(sh7785_devices_setup);

static struct platform_device *sh7785_early_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&scif4_device,
	&scif5_device,
	&tmu0_device,
	&tmu1_device,
};

void __init plat_early_device_setup(void)
{
	sh_early_platform_add_devices(sh7785_early_devices,
				   ARRAY_SIZE(sh7785_early_devices));
}

enum {
	UNUSED = 0,

	/* interrupt sources */

	IRL0_LLLL, IRL0_LLLH, IRL0_LLHL, IRL0_LLHH,
	IRL0_LHLL, IRL0_LHLH, IRL0_LHHL, IRL0_LHHH,
	IRL0_HLLL, IRL0_HLLH, IRL0_HLHL, IRL0_HLHH,
	IRL0_HHLL, IRL0_HHLH, IRL0_HHHL,

	IRL4_LLLL, IRL4_LLLH, IRL4_LLHL, IRL4_LLHH,
	IRL4_LHLL, IRL4_LHLH, IRL4_LHHL, IRL4_LHHH,
	IRL4_HLLL, IRL4_HLLH, IRL4_HLHL, IRL4_HLHH,
	IRL4_HHLL, IRL4_HHLH, IRL4_HHHL,

	IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7,
	WDT, TMU0, TMU1, TMU2, TMU2_TICPI,
	HUDI, DMAC0, SCIF0, SCIF1, DMAC1, HSPI,
	SCIF2, SCIF3, SCIF4, SCIF5,
	PCISERR, PCIINTA, PCIINTB, PCIINTC, PCIINTD, PCIC5,
	SIOF, MMCIF, DU, GDTA,
	TMU3, TMU4, TMU5,
	SSI0, SSI1,
	HAC0, HAC1,
	FLCTL, GPIO,

	/* interrupt groups */

	TMU012,	TMU345
};

static struct intc_vect vectors[] __initdata = {
	INTC_VECT(WDT, 0x560),
	INTC_VECT(TMU0, 0x580), INTC_VECT(TMU1, 0x5a0),
	INTC_VECT(TMU2, 0x5c0), INTC_VECT(TMU2_TICPI, 0x5e0),
	INTC_VECT(HUDI, 0x600),
	INTC_VECT(DMAC0, 0x620), INTC_VECT(DMAC0, 0x640),
	INTC_VECT(DMAC0, 0x660), INTC_VECT(DMAC0, 0x680),
	INTC_VECT(DMAC0, 0x6a0), INTC_VECT(DMAC0, 0x6c0),
	INTC_VECT(DMAC0, 0x6e0),
	INTC_VECT(SCIF0, 0x700), INTC_VECT(SCIF0, 0x720),
	INTC_VECT(SCIF0, 0x740), INTC_VECT(SCIF0, 0x760),
	INTC_VECT(SCIF1, 0x780), INTC_VECT(SCIF1, 0x7a0),
	INTC_VECT(SCIF1, 0x7c0), INTC_VECT(SCIF1, 0x7e0),
	INTC_VECT(DMAC1, 0x880), INTC_VECT(DMAC1, 0x8a0),
	INTC_VECT(DMAC1, 0x8c0), INTC_VECT(DMAC1, 0x8e0),
	INTC_VECT(DMAC1, 0x900), INTC_VECT(DMAC1, 0x920),
	INTC_VECT(DMAC1, 0x940),
	INTC_VECT(HSPI, 0x960),
	INTC_VECT(SCIF2, 0x980), INTC_VECT(SCIF3, 0x9a0),
	INTC_VECT(SCIF4, 0x9c0), INTC_VECT(SCIF5, 0x9e0),
	INTC_VECT(PCISERR, 0xa00), INTC_VECT(PCIINTA, 0xa20),
	INTC_VECT(PCIINTB, 0xa40), INTC_VECT(PCIINTC, 0xa60),
	INTC_VECT(PCIINTD, 0xa80), INTC_VECT(PCIC5, 0xaa0),
	INTC_VECT(PCIC5, 0xac0), INTC_VECT(PCIC5, 0xae0),
	INTC_VECT(PCIC5, 0xb00), INTC_VECT(PCIC5, 0xb20),
	INTC_VECT(SIOF, 0xc00),
	INTC_VECT(MMCIF, 0xd00), INTC_VECT(MMCIF, 0xd20),
	INTC_VECT(MMCIF, 0xd40), INTC_VECT(MMCIF, 0xd60),
	INTC_VECT(DU, 0xd80),
	INTC_VECT(GDTA, 0xda0), INTC_VECT(GDTA, 0xdc0),
	INTC_VECT(GDTA, 0xde0),
	INTC_VECT(TMU3, 0xe00), INTC_VECT(TMU4, 0xe20),
	INTC_VECT(TMU5, 0xe40),
	INTC_VECT(SSI0, 0xe80), INTC_VECT(SSI1, 0xea0),
	INTC_VECT(HAC0, 0xec0), INTC_VECT(HAC1, 0xee0),
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
	{ 0xffd00044, 0xffd00064, 32, /* INTMSK0 / INTMSKCLR0 */
	  { IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7 } },

	{ 0xffd40080, 0xffd40084, 32, /* INTMSK2 / INTMSKCLR2 */
	  { IRL0_LLLL, IRL0_LLLH, IRL0_LLHL, IRL0_LLHH,
	    IRL0_LHLL, IRL0_LHLH, IRL0_LHHL, IRL0_LHHH,
	    IRL0_HLLL, IRL0_HLLH, IRL0_HLHL, IRL0_HLHH,
	    IRL0_HHLL, IRL0_HHLH, IRL0_HHHL, 0,
	    IRL4_LLLL, IRL4_LLLH, IRL4_LLHL, IRL4_LLHH,
	    IRL4_LHLL, IRL4_LHLH, IRL4_LHHL, IRL4_LHHH,
	    IRL4_HLLL, IRL4_HLLH, IRL4_HLHL, IRL4_HLHH,
	    IRL4_HHLL, IRL4_HHLH, IRL4_HHHL, 0, } },

	{ 0xffd40038, 0xffd4003c, 32, /* INT2MSKR / INT2MSKCR */
	  { 0, 0, 0, GDTA, DU, SSI0, SSI1, GPIO,
	    FLCTL, MMCIF, HSPI, SIOF, PCIC5, PCIINTD, PCIINTC, PCIINTB,
	    PCIINTA, PCISERR, HAC1, HAC0, DMAC1, DMAC0, HUDI, WDT,
	    SCIF5, SCIF4, SCIF3, SCIF2, SCIF1, SCIF0, TMU345, TMU012 } },
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xffd00010, 0, 32, 4, /* INTPRI */   { IRQ0, IRQ1, IRQ2, IRQ3,
						 IRQ4, IRQ5, IRQ6, IRQ7 } },
	{ 0xffd40000, 0, 32, 8, /* INT2PRI0 */ { TMU0, TMU1,
						 TMU2, TMU2_TICPI } },
	{ 0xffd40004, 0, 32, 8, /* INT2PRI1 */ { TMU3, TMU4, TMU5, } },
	{ 0xffd40008, 0, 32, 8, /* INT2PRI2 */ { SCIF0, SCIF1,
						 SCIF2, SCIF3 } },
	{ 0xffd4000c, 0, 32, 8, /* INT2PRI3 */ { SCIF4, SCIF5, WDT, } },
	{ 0xffd40010, 0, 32, 8, /* INT2PRI4 */ { HUDI, DMAC0, DMAC1, } },
	{ 0xffd40014, 0, 32, 8, /* INT2PRI5 */ { HAC0, HAC1,
						 PCISERR, PCIINTA } },
	{ 0xffd40018, 0, 32, 8, /* INT2PRI6 */ { PCIINTB, PCIINTC,
						 PCIINTD, PCIC5 } },
	{ 0xffd4001c, 0, 32, 8, /* INT2PRI7 */ { SIOF, HSPI, MMCIF, } },
	{ 0xffd40020, 0, 32, 8, /* INT2PRI8 */ { FLCTL, GPIO, SSI0, SSI1, } },
	{ 0xffd40024, 0, 32, 8, /* INT2PRI9 */ { DU, GDTA, } },
};

static DECLARE_INTC_DESC(intc_desc, "sh7785", vectors, groups,
			 mask_registers, prio_registers, NULL);

/* Support for external interrupt pins in IRQ mode */

static struct intc_vect vectors_irq0123[] __initdata = {
	INTC_VECT(IRQ0, 0x240), INTC_VECT(IRQ1, 0x280),
	INTC_VECT(IRQ2, 0x2c0), INTC_VECT(IRQ3, 0x300),
};

static struct intc_vect vectors_irq4567[] __initdata = {
	INTC_VECT(IRQ4, 0x340), INTC_VECT(IRQ5, 0x380),
	INTC_VECT(IRQ6, 0x3c0), INTC_VECT(IRQ7, 0x200),
};

static struct intc_sense_reg sense_registers[] __initdata = {
	{ 0xffd0001c, 32, 2, /* ICR1 */   { IRQ0, IRQ1, IRQ2, IRQ3,
					    IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static struct intc_mask_reg ack_registers[] __initdata = {
	{ 0xffd00024, 0, 32, /* INTREQ */
	  { IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static DECLARE_INTC_DESC_ACK(intc_desc_irq0123, "sh7785-irq0123",
			     vectors_irq0123, NULL, mask_registers,
			     prio_registers, sense_registers, ack_registers);

static DECLARE_INTC_DESC_ACK(intc_desc_irq4567, "sh7785-irq4567",
			     vectors_irq4567, NULL, mask_registers,
			     prio_registers, sense_registers, ack_registers);

/* External interrupt pins in IRL mode */

static struct intc_vect vectors_irl0123[] __initdata = {
	INTC_VECT(IRL0_LLLL, 0x200), INTC_VECT(IRL0_LLLH, 0x220),
	INTC_VECT(IRL0_LLHL, 0x240), INTC_VECT(IRL0_LLHH, 0x260),
	INTC_VECT(IRL0_LHLL, 0x280), INTC_VECT(IRL0_LHLH, 0x2a0),
	INTC_VECT(IRL0_LHHL, 0x2c0), INTC_VECT(IRL0_LHHH, 0x2e0),
	INTC_VECT(IRL0_HLLL, 0x300), INTC_VECT(IRL0_HLLH, 0x320),
	INTC_VECT(IRL0_HLHL, 0x340), INTC_VECT(IRL0_HLHH, 0x360),
	INTC_VECT(IRL0_HHLL, 0x380), INTC_VECT(IRL0_HHLH, 0x3a0),
	INTC_VECT(IRL0_HHHL, 0x3c0),
};

static struct intc_vect vectors_irl4567[] __initdata = {
	INTC_VECT(IRL4_LLLL, 0xb00), INTC_VECT(IRL4_LLLH, 0xb20),
	INTC_VECT(IRL4_LLHL, 0xb40), INTC_VECT(IRL4_LLHH, 0xb60),
	INTC_VECT(IRL4_LHLL, 0xb80), INTC_VECT(IRL4_LHLH, 0xba0),
	INTC_VECT(IRL4_LHHL, 0xbc0), INTC_VECT(IRL4_LHHH, 0xbe0),
	INTC_VECT(IRL4_HLLL, 0xc00), INTC_VECT(IRL4_HLLH, 0xc20),
	INTC_VECT(IRL4_HLHL, 0xc40), INTC_VECT(IRL4_HLHH, 0xc60),
	INTC_VECT(IRL4_HHLL, 0xc80), INTC_VECT(IRL4_HHLH, 0xca0),
	INTC_VECT(IRL4_HHHL, 0xcc0),
};

static DECLARE_INTC_DESC(intc_desc_irl0123, "sh7785-irl0123", vectors_irl0123,
			 NULL, mask_registers, NULL, NULL);

static DECLARE_INTC_DESC(intc_desc_irl4567, "sh7785-irl4567", vectors_irl4567,
			 NULL, mask_registers, NULL, NULL);

#define INTC_ICR0	0xffd00000
#define INTC_INTMSK0	0xffd00044
#define INTC_INTMSK1	0xffd00048
#define INTC_INTMSK2	0xffd40080
#define INTC_INTMSKCLR1	0xffd00068
#define INTC_INTMSKCLR2	0xffd40084

void __init plat_irq_setup(void)
{
	/* disable IRQ3-0 + IRQ7-4 */
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
	case IRQ_MODE_IRQ7654:
		/* select IRQ mode for IRL7-4 */
		__raw_writel(__raw_readl(INTC_ICR0) | 0x00400000, INTC_ICR0);
		register_intc_controller(&intc_desc_irq4567);
		break;
	case IRQ_MODE_IRQ3210:
		/* select IRQ mode for IRL3-0 */
		__raw_writel(__raw_readl(INTC_ICR0) | 0x00800000, INTC_ICR0);
		register_intc_controller(&intc_desc_irq0123);
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
		register_intc_controller(&intc_desc_irl4567);
		break;
	case IRQ_MODE_IRL3210_MASK:
		/* enable IRL0-3 and mask using cpu intc controller */
		__raw_writel(0x80000000, INTC_INTMSKCLR1);
		register_intc_controller(&intc_desc_irl0123);
		break;
	default:
		BUG();
	}
}

void __init plat_mem_setup(void)
{
	/* Register the URAM space as Node 1 */
	setup_bootmem_node(1, 0xe55f0000, 0xe5610000);
}
