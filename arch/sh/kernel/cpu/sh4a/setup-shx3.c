// SPDX-License-Identifier: GPL-2.0
/*
 * SH-X3 Prototype Setup
 *
 *  Copyright (C) 2007 - 2010  Paul Mundt
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/serial_sci.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/sh_timer.h>
#include <linux/sh_intc.h>
#include <cpu/shx3.h>
#include <asm/mmzone.h>
#include <asm/platform_early.h>

/*
 * This intentionally only registers SCIF ports 0, 1, and 3. SCIF 2
 * INTEVT values overlap with the FPU EXPEVT ones, requiring special
 * demuxing in the exception dispatch path.
 *
 * As this overlap is something that never should have made it in to
 * silicon in the first place, we just refuse to deal with the port at
 * all rather than adding infrastructure to hack around it.
 */
static struct plat_sci_port scif0_platform_data = {
	.scscr		= SCSCR_REIE,
	.type		= PORT_SCIF,
};

static struct resource scif0_resources[] = {
	DEFINE_RES_MEM(0xffc30000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0x700)),
	DEFINE_RES_IRQ(evt2irq(0x720)),
	DEFINE_RES_IRQ(evt2irq(0x760)),
	DEFINE_RES_IRQ(evt2irq(0x740)),
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
	.scscr		= SCSCR_REIE,
	.type		= PORT_SCIF,
};

static struct resource scif1_resources[] = {
	DEFINE_RES_MEM(0xffc40000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0x780)),
	DEFINE_RES_IRQ(evt2irq(0x7a0)),
	DEFINE_RES_IRQ(evt2irq(0x7e0)),
	DEFINE_RES_IRQ(evt2irq(0x7c0)),
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
	.scscr		= SCSCR_REIE,
	.type		= PORT_SCIF,
};

static struct resource scif2_resources[] = {
	DEFINE_RES_MEM(0xffc60000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0x880)),
	DEFINE_RES_IRQ(evt2irq(0x8a0)),
	DEFINE_RES_IRQ(evt2irq(0x8e0)),
	DEFINE_RES_IRQ(evt2irq(0x8c0)),
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

static struct sh_timer_config tmu0_platform_data = {
	.channels_mask = 7,
};

static struct resource tmu0_resources[] = {
	DEFINE_RES_MEM(0xffc10000, 0x30),
	DEFINE_RES_IRQ(evt2irq(0x400)),
	DEFINE_RES_IRQ(evt2irq(0x420)),
	DEFINE_RES_IRQ(evt2irq(0x440)),
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
	DEFINE_RES_MEM(0xffc20000, 0x2c),
	DEFINE_RES_IRQ(evt2irq(0x460)),
	DEFINE_RES_IRQ(evt2irq(0x480)),
	DEFINE_RES_IRQ(evt2irq(0x4a0)),
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

static struct platform_device *shx3_early_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&tmu0_device,
	&tmu1_device,
};

static int __init shx3_devices_setup(void)
{
	return platform_add_devices(shx3_early_devices,
				   ARRAY_SIZE(shx3_early_devices));
}
arch_initcall(shx3_devices_setup);

void __init plat_early_device_setup(void)
{
	sh_early_platform_add_devices(shx3_early_devices,
				   ARRAY_SIZE(shx3_early_devices));
}

enum {
	UNUSED = 0,

	/* interrupt sources */
	IRL_LLLL, IRL_LLLH, IRL_LLHL, IRL_LLHH,
	IRL_LHLL, IRL_LHLH, IRL_LHHL, IRL_LHHH,
	IRL_HLLL, IRL_HLLH, IRL_HLHL, IRL_HLHH,
	IRL_HHLL, IRL_HHLH, IRL_HHHL,
	IRQ0, IRQ1, IRQ2, IRQ3,
	HUDII,
	TMU0, TMU1, TMU2, TMU3, TMU4, TMU5,
	PCII0, PCII1, PCII2, PCII3, PCII4,
	PCII5, PCII6, PCII7, PCII8, PCII9,
	SCIF0_ERI, SCIF0_RXI, SCIF0_BRI, SCIF0_TXI,
	SCIF1_ERI, SCIF1_RXI, SCIF1_BRI, SCIF1_TXI,
	SCIF2_ERI, SCIF2_RXI, SCIF2_BRI, SCIF2_TXI,
	SCIF3_ERI, SCIF3_RXI, SCIF3_BRI, SCIF3_TXI,
	DMAC0_DMINT0, DMAC0_DMINT1, DMAC0_DMINT2, DMAC0_DMINT3,
	DMAC0_DMINT4, DMAC0_DMINT5, DMAC0_DMAE,
	DU,
	DMAC1_DMINT6, DMAC1_DMINT7, DMAC1_DMINT8, DMAC1_DMINT9,
	DMAC1_DMINT10, DMAC1_DMINT11, DMAC1_DMAE,
	IIC, VIN0, VIN1, VCORE0, ATAPI,
	DTU0, DTU1, DTU2, DTU3,
	FE0, FE1,
	GPIO0, GPIO1, GPIO2, GPIO3,
	PAM, IRM,
	INTICI0, INTICI1, INTICI2, INTICI3,
	INTICI4, INTICI5, INTICI6, INTICI7,

	/* interrupt groups */
	IRL, PCII56789, SCIF0, SCIF1, SCIF2, SCIF3,
	DMAC0, DMAC1,
};

static struct intc_vect vectors[] __initdata = {
	INTC_VECT(HUDII, 0x3e0),
	INTC_VECT(TMU0, 0x400), INTC_VECT(TMU1, 0x420),
	INTC_VECT(TMU2, 0x440), INTC_VECT(TMU3, 0x460),
	INTC_VECT(TMU4, 0x480), INTC_VECT(TMU5, 0x4a0),
	INTC_VECT(PCII0, 0x500), INTC_VECT(PCII1, 0x520),
	INTC_VECT(PCII2, 0x540), INTC_VECT(PCII3, 0x560),
	INTC_VECT(PCII4, 0x580), INTC_VECT(PCII5, 0x5a0),
	INTC_VECT(PCII6, 0x5c0), INTC_VECT(PCII7, 0x5e0),
	INTC_VECT(PCII8, 0x600), INTC_VECT(PCII9, 0x620),
	INTC_VECT(SCIF0_ERI, 0x700), INTC_VECT(SCIF0_RXI, 0x720),
	INTC_VECT(SCIF0_BRI, 0x740), INTC_VECT(SCIF0_TXI, 0x760),
	INTC_VECT(SCIF1_ERI, 0x780), INTC_VECT(SCIF1_RXI, 0x7a0),
	INTC_VECT(SCIF1_BRI, 0x7c0), INTC_VECT(SCIF1_TXI, 0x7e0),
	INTC_VECT(SCIF3_ERI, 0x880), INTC_VECT(SCIF3_RXI, 0x8a0),
	INTC_VECT(SCIF3_BRI, 0x8c0), INTC_VECT(SCIF3_TXI, 0x8e0),
	INTC_VECT(DMAC0_DMINT0, 0x900), INTC_VECT(DMAC0_DMINT1, 0x920),
	INTC_VECT(DMAC0_DMINT2, 0x940), INTC_VECT(DMAC0_DMINT3, 0x960),
	INTC_VECT(DMAC0_DMINT4, 0x980), INTC_VECT(DMAC0_DMINT5, 0x9a0),
	INTC_VECT(DMAC0_DMAE, 0x9c0),
	INTC_VECT(DU, 0x9e0),
	INTC_VECT(DMAC1_DMINT6, 0xa00), INTC_VECT(DMAC1_DMINT7, 0xa20),
	INTC_VECT(DMAC1_DMINT8, 0xa40), INTC_VECT(DMAC1_DMINT9, 0xa60),
	INTC_VECT(DMAC1_DMINT10, 0xa80), INTC_VECT(DMAC1_DMINT11, 0xaa0),
	INTC_VECT(DMAC1_DMAE, 0xac0),
	INTC_VECT(IIC, 0xae0),
	INTC_VECT(VIN0, 0xb00), INTC_VECT(VIN1, 0xb20),
	INTC_VECT(VCORE0, 0xb00), INTC_VECT(ATAPI, 0xb60),
	INTC_VECT(DTU0, 0xc00), INTC_VECT(DTU0, 0xc20),
	INTC_VECT(DTU0, 0xc40),
	INTC_VECT(DTU1, 0xc60), INTC_VECT(DTU1, 0xc80),
	INTC_VECT(DTU1, 0xca0),
	INTC_VECT(DTU2, 0xcc0), INTC_VECT(DTU2, 0xce0),
	INTC_VECT(DTU2, 0xd00),
	INTC_VECT(DTU3, 0xd20), INTC_VECT(DTU3, 0xd40),
	INTC_VECT(DTU3, 0xd60),
	INTC_VECT(FE0, 0xe00), INTC_VECT(FE1, 0xe20),
	INTC_VECT(GPIO0, 0xe40), INTC_VECT(GPIO1, 0xe60),
	INTC_VECT(GPIO2, 0xe80), INTC_VECT(GPIO3, 0xea0),
	INTC_VECT(PAM, 0xec0), INTC_VECT(IRM, 0xee0),
	INTC_VECT(INTICI0, 0xf00), INTC_VECT(INTICI1, 0xf20),
	INTC_VECT(INTICI2, 0xf40), INTC_VECT(INTICI3, 0xf60),
	INTC_VECT(INTICI4, 0xf80), INTC_VECT(INTICI5, 0xfa0),
	INTC_VECT(INTICI6, 0xfc0), INTC_VECT(INTICI7, 0xfe0),
};

static struct intc_group groups[] __initdata = {
	INTC_GROUP(IRL, IRL_LLLL, IRL_LLLH, IRL_LLHL, IRL_LLHH,
		   IRL_LHLL, IRL_LHLH, IRL_LHHL, IRL_LHHH,
		   IRL_HLLL, IRL_HLLH, IRL_HLHL, IRL_HLHH,
		   IRL_HHLL, IRL_HHLH, IRL_HHHL),
	INTC_GROUP(PCII56789, PCII5, PCII6, PCII7, PCII8, PCII9),
	INTC_GROUP(SCIF0, SCIF0_ERI, SCIF0_RXI, SCIF0_BRI, SCIF0_TXI),
	INTC_GROUP(SCIF1, SCIF1_ERI, SCIF1_RXI, SCIF1_BRI, SCIF1_TXI),
	INTC_GROUP(SCIF3, SCIF3_ERI, SCIF3_RXI, SCIF3_BRI, SCIF3_TXI),
	INTC_GROUP(DMAC0, DMAC0_DMINT0, DMAC0_DMINT1, DMAC0_DMINT2,
		   DMAC0_DMINT3, DMAC0_DMINT4, DMAC0_DMINT5, DMAC0_DMAE),
	INTC_GROUP(DMAC1, DMAC1_DMINT6, DMAC1_DMINT7, DMAC1_DMINT8,
		   DMAC1_DMINT9, DMAC1_DMINT10, DMAC1_DMINT11),
};

#define INT2DISTCR0	0xfe4108a0
#define INT2DISTCR1	0xfe4108a4
#define INT2DISTCR2	0xfe4108a8

static struct intc_mask_reg mask_registers[] __initdata = {
	{ 0xfe410030, 0xfe410050, 32, /* CnINTMSK0 / CnINTMSKCLR0 */
	  { IRQ0, IRQ1, IRQ2, IRQ3 } },
	{ 0xfe410040, 0xfe410060, 32, /* CnINTMSK1 / CnINTMSKCLR1 */
	  { IRL } },
	{ 0xfe410820, 0xfe410850, 32, /* CnINT2MSK0 / CnINT2MSKCLR0 */
	  { FE1, FE0, 0, ATAPI, VCORE0, VIN1, VIN0, IIC,
	    DU, GPIO3, GPIO2, GPIO1, GPIO0, PAM, 0, 0,
	    0, 0, 0, 0, 0, 0, 0, 0, /* HUDI bits ignored */
	    0, TMU5, TMU4, TMU3, TMU2, TMU1, TMU0, 0, },
	    INTC_SMP_BALANCING(INT2DISTCR0) },
	{ 0xfe410830, 0xfe410860, 32, /* CnINT2MSK1 / CnINT2MSKCLR1 */
	  { 0, 0, 0, 0, DTU3, DTU2, DTU1, DTU0, /* IRM bits ignored */
	    PCII9, PCII8, PCII7, PCII6, PCII5, PCII4, PCII3, PCII2,
	    PCII1, PCII0, DMAC1_DMAE, DMAC1_DMINT11,
	    DMAC1_DMINT10, DMAC1_DMINT9, DMAC1_DMINT8, DMAC1_DMINT7,
	    DMAC1_DMINT6, DMAC0_DMAE, DMAC0_DMINT5, DMAC0_DMINT4,
	    DMAC0_DMINT3, DMAC0_DMINT2, DMAC0_DMINT1, DMAC0_DMINT0 },
	    INTC_SMP_BALANCING(INT2DISTCR1) },
	{ 0xfe410840, 0xfe410870, 32, /* CnINT2MSK2 / CnINT2MSKCLR2 */
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	    SCIF3_TXI, SCIF3_BRI, SCIF3_RXI, SCIF3_ERI,
	    SCIF2_TXI, SCIF2_BRI, SCIF2_RXI, SCIF2_ERI,
	    SCIF1_TXI, SCIF1_BRI, SCIF1_RXI, SCIF1_ERI,
	    SCIF0_TXI, SCIF0_BRI, SCIF0_RXI, SCIF0_ERI },
	    INTC_SMP_BALANCING(INT2DISTCR2) },
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xfe410010, 0, 32, 4, /* INTPRI */ { IRQ0, IRQ1, IRQ2, IRQ3 } },

	{ 0xfe410800, 0, 32, 4, /* INT2PRI0 */ { 0, HUDII, TMU5, TMU4,
						 TMU3, TMU2, TMU1, TMU0 } },
	{ 0xfe410804, 0, 32, 4, /* INT2PRI1 */ { DTU3, DTU2, DTU1, DTU0,
						 SCIF3, SCIF2,
						 SCIF1, SCIF0 } },
	{ 0xfe410808, 0, 32, 4, /* INT2PRI2 */ { DMAC1, DMAC0,
						 PCII56789, PCII4,
						 PCII3, PCII2,
						 PCII1, PCII0 } },
	{ 0xfe41080c, 0, 32, 4, /* INT2PRI3 */ { FE1, FE0, ATAPI, VCORE0,
						 VIN1, VIN0, IIC, DU} },
	{ 0xfe410810, 0, 32, 4, /* INT2PRI4 */ { 0, 0, PAM, GPIO3,
						 GPIO2, GPIO1, GPIO0, IRM } },
	{ 0xfe410090, 0xfe4100a0, 32, 4, /* CnICIPRI / CnICIPRICLR */
	  { INTICI7, INTICI6, INTICI5, INTICI4,
	    INTICI3, INTICI2, INTICI1, INTICI0 }, INTC_SMP(4, 4) },
};

static DECLARE_INTC_DESC(intc_desc, "shx3", vectors, groups,
			 mask_registers, prio_registers, NULL);

/* Support for external interrupt pins in IRQ mode */
static struct intc_vect vectors_irq[] __initdata = {
	INTC_VECT(IRQ0, 0x240), INTC_VECT(IRQ1, 0x280),
	INTC_VECT(IRQ2, 0x2c0), INTC_VECT(IRQ3, 0x300),
};

static struct intc_sense_reg sense_registers[] __initdata = {
	{ 0xfe41001c, 32, 2, /* ICR1 */   { IRQ0, IRQ1, IRQ2, IRQ3 } },
};

static DECLARE_INTC_DESC(intc_desc_irq, "shx3-irq", vectors_irq, groups,
			 mask_registers, prio_registers, sense_registers);

/* External interrupt pins in IRL mode */
static struct intc_vect vectors_irl[] __initdata = {
	INTC_VECT(IRL_LLLL, 0x200), INTC_VECT(IRL_LLLH, 0x220),
	INTC_VECT(IRL_LLHL, 0x240), INTC_VECT(IRL_LLHH, 0x260),
	INTC_VECT(IRL_LHLL, 0x280), INTC_VECT(IRL_LHLH, 0x2a0),
	INTC_VECT(IRL_LHHL, 0x2c0), INTC_VECT(IRL_LHHH, 0x2e0),
	INTC_VECT(IRL_HLLL, 0x300), INTC_VECT(IRL_HLLH, 0x320),
	INTC_VECT(IRL_HLHL, 0x340), INTC_VECT(IRL_HLHH, 0x360),
	INTC_VECT(IRL_HHLL, 0x380), INTC_VECT(IRL_HHLH, 0x3a0),
	INTC_VECT(IRL_HHHL, 0x3c0),
};

static DECLARE_INTC_DESC(intc_desc_irl, "shx3-irl", vectors_irl, groups,
			 mask_registers, prio_registers, NULL);

void __init plat_irq_setup_pins(int mode)
{
	int ret = 0;

	switch (mode) {
	case IRQ_MODE_IRQ:
		ret |= gpio_request(GPIO_FN_IRQ3, intc_desc_irq.name);
		ret |= gpio_request(GPIO_FN_IRQ2, intc_desc_irq.name);
		ret |= gpio_request(GPIO_FN_IRQ1, intc_desc_irq.name);
		ret |= gpio_request(GPIO_FN_IRQ0, intc_desc_irq.name);

		if (unlikely(ret)) {
			pr_err("Failed to set IRQ mode\n");
			return;
		}

		register_intc_controller(&intc_desc_irq);
		break;
	case IRQ_MODE_IRL3210:
		ret |= gpio_request(GPIO_FN_IRL3, intc_desc_irl.name);
		ret |= gpio_request(GPIO_FN_IRL2, intc_desc_irl.name);
		ret |= gpio_request(GPIO_FN_IRL1, intc_desc_irl.name);
		ret |= gpio_request(GPIO_FN_IRL0, intc_desc_irl.name);

		if (unlikely(ret)) {
			pr_err("Failed to set IRL mode\n");
			return;
		}

		register_intc_controller(&intc_desc_irl);
		break;
	default:
		BUG();
	}
}

void __init plat_irq_setup(void)
{
	register_intc_controller(&intc_desc);
}

void __init plat_mem_setup(void)
{
	unsigned int nid = 1;

	/* Register CPU#0 URAM space as Node 1 */
	setup_bootmem_node(nid++, 0x145f0000, 0x14610000);	/* CPU0 */

#if 0
	/* XXX: Not yet.. */
	setup_bootmem_node(nid++, 0x14df0000, 0x14e10000);	/* CPU1 */
	setup_bootmem_node(nid++, 0x155f0000, 0x15610000);	/* CPU2 */
	setup_bootmem_node(nid++, 0x15df0000, 0x15e10000);	/* CPU3 */
#endif

	setup_bootmem_node(nid++, 0x16000000, 0x16020000);	/* CSM */
}
