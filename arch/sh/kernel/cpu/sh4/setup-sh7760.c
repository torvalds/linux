// SPDX-License-Identifier: GPL-2.0
/*
 * SH7760 Setup
 *
 *  Copyright (C) 2006  Paul Mundt
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/sh_timer.h>
#include <linux/sh_intc.h>
#include <linux/serial_sci.h>
#include <linux/io.h>
#include <asm/platform_early.h>

enum {
	UNUSED = 0,

	/* interrupt sources */
	IRL0, IRL1, IRL2, IRL3,
	HUDI, GPIOI, DMAC,
	IRQ4, IRQ5, IRQ6, IRQ7,
	HCAN20, HCAN21,
	SSI0, SSI1,
	HAC0, HAC1,
	I2C0, I2C1,
	USB, LCDC,
	DMABRG0, DMABRG1, DMABRG2,
	SCIF0_ERI, SCIF0_RXI, SCIF0_BRI, SCIF0_TXI,
	SCIF1_ERI, SCIF1_RXI, SCIF1_BRI, SCIF1_TXI,
	SCIF2_ERI, SCIF2_RXI, SCIF2_BRI, SCIF2_TXI,
	SIM_ERI, SIM_RXI, SIM_TXI, SIM_TEI,
	HSPI,
	MMCIF0, MMCIF1, MMCIF2, MMCIF3,
	MFI, ADC, CMT,
	TMU0, TMU1, TMU2,
	WDT, REF,

	/* interrupt groups */
	DMABRG, SCIF0, SCIF1, SCIF2, SIM, MMCIF,
};

static struct intc_vect vectors[] __initdata = {
	INTC_VECT(HUDI, 0x600), INTC_VECT(GPIOI, 0x620),
	INTC_VECT(DMAC, 0x640), INTC_VECT(DMAC, 0x660),
	INTC_VECT(DMAC, 0x680), INTC_VECT(DMAC, 0x6a0),
	INTC_VECT(DMAC, 0x780), INTC_VECT(DMAC, 0x7a0),
	INTC_VECT(DMAC, 0x7c0), INTC_VECT(DMAC, 0x7e0),
	INTC_VECT(DMAC, 0x6c0),
	INTC_VECT(IRQ4, 0x800), INTC_VECT(IRQ5, 0x820),
	INTC_VECT(IRQ6, 0x840), INTC_VECT(IRQ6, 0x860),
	INTC_VECT(HCAN20, 0x900), INTC_VECT(HCAN21, 0x920),
	INTC_VECT(SSI0, 0x940), INTC_VECT(SSI1, 0x960),
	INTC_VECT(HAC0, 0x980), INTC_VECT(HAC1, 0x9a0),
	INTC_VECT(I2C0, 0x9c0), INTC_VECT(I2C1, 0x9e0),
	INTC_VECT(USB, 0xa00), INTC_VECT(LCDC, 0xa20),
	INTC_VECT(DMABRG0, 0xa80), INTC_VECT(DMABRG1, 0xaa0),
	INTC_VECT(DMABRG2, 0xac0),
	INTC_VECT(SCIF0_ERI, 0x880), INTC_VECT(SCIF0_RXI, 0x8a0),
	INTC_VECT(SCIF0_BRI, 0x8c0), INTC_VECT(SCIF0_TXI, 0x8e0),
	INTC_VECT(SCIF1_ERI, 0xb00), INTC_VECT(SCIF1_RXI, 0xb20),
	INTC_VECT(SCIF1_BRI, 0xb40), INTC_VECT(SCIF1_TXI, 0xb60),
	INTC_VECT(SCIF2_ERI, 0xb80), INTC_VECT(SCIF2_RXI, 0xba0),
	INTC_VECT(SCIF2_BRI, 0xbc0), INTC_VECT(SCIF2_TXI, 0xbe0),
	INTC_VECT(SIM_ERI, 0xc00), INTC_VECT(SIM_RXI, 0xc20),
	INTC_VECT(SIM_TXI, 0xc40), INTC_VECT(SIM_TEI, 0xc60),
	INTC_VECT(HSPI, 0xc80),
	INTC_VECT(MMCIF0, 0xd00), INTC_VECT(MMCIF1, 0xd20),
	INTC_VECT(MMCIF2, 0xd40), INTC_VECT(MMCIF3, 0xd60),
	INTC_VECT(MFI, 0xe80), /* 0xf80 according to data sheet */
	INTC_VECT(ADC, 0xf80), INTC_VECT(CMT, 0xfa0),
	INTC_VECT(TMU0, 0x400), INTC_VECT(TMU1, 0x420),
	INTC_VECT(TMU2, 0x440), INTC_VECT(TMU2, 0x460),
	INTC_VECT(WDT, 0x560),
	INTC_VECT(REF, 0x580), INTC_VECT(REF, 0x5a0),
};

static struct intc_group groups[] __initdata = {
	INTC_GROUP(DMABRG, DMABRG0, DMABRG1, DMABRG2),
	INTC_GROUP(SCIF0, SCIF0_ERI, SCIF0_RXI, SCIF0_BRI, SCIF0_TXI),
	INTC_GROUP(SCIF1, SCIF1_ERI, SCIF1_RXI, SCIF1_BRI, SCIF1_TXI),
	INTC_GROUP(SCIF2, SCIF2_ERI, SCIF2_RXI, SCIF2_BRI, SCIF2_TXI),
	INTC_GROUP(SIM, SIM_ERI, SIM_RXI, SIM_TXI, SIM_TEI),
	INTC_GROUP(MMCIF, MMCIF0, MMCIF1, MMCIF2, MMCIF3),
};

static struct intc_mask_reg mask_registers[] __initdata = {
	{ 0xfe080040, 0xfe080060, 32, /* INTMSK00 / INTMSKCLR00 */
	  { IRQ4, IRQ5, IRQ6, IRQ7, 0, 0, HCAN20, HCAN21,
	    SSI0, SSI1, HAC0, HAC1, I2C0, I2C1, USB, LCDC,
	    0, DMABRG0, DMABRG1, DMABRG2,
	    SCIF0_ERI, SCIF0_RXI, SCIF0_BRI, SCIF0_TXI,
	    SCIF1_ERI, SCIF1_RXI, SCIF1_BRI, SCIF1_TXI,
	    SCIF2_ERI, SCIF2_RXI, SCIF2_BRI, SCIF2_TXI, } },
	{ 0xfe080044, 0xfe080064, 32, /* INTMSK04 / INTMSKCLR04 */
	  { 0, 0, 0, 0, 0, 0, 0, 0,
	    SIM_ERI, SIM_RXI, SIM_TXI, SIM_TEI,
	    HSPI, MMCIF0, MMCIF1, MMCIF2,
	    MMCIF3, 0, 0, 0, 0, 0, 0, 0,
	    0, MFI, 0, 0, 0, 0, ADC, CMT, } },
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xffd00004, 0, 16, 4, /* IPRA */ { TMU0, TMU1, TMU2 } },
	{ 0xffd00008, 0, 16, 4, /* IPRB */ { WDT, REF, 0, 0 } },
	{ 0xffd0000c, 0, 16, 4, /* IPRC */ { GPIOI, DMAC, 0, HUDI } },
	{ 0xffd00010, 0, 16, 4, /* IPRD */ { IRL0, IRL1, IRL2, IRL3 } },
	{ 0xfe080000, 0, 32, 4, /* INTPRI00 */ { IRQ4, IRQ5, IRQ6, IRQ7 } },
	{ 0xfe080004, 0, 32, 4, /* INTPRI04 */ { HCAN20, HCAN21, SSI0, SSI1,
						 HAC0, HAC1, I2C0, I2C1 } },
	{ 0xfe080008, 0, 32, 4, /* INTPRI08 */ { USB, LCDC, DMABRG, SCIF0,
						 SCIF1, SCIF2, SIM, HSPI } },
	{ 0xfe08000c, 0, 32, 4, /* INTPRI0C */ { 0, 0, MMCIF, 0,
						 MFI, 0, ADC, CMT } },
};

static DECLARE_INTC_DESC(intc_desc, "sh7760", vectors, groups,
			 mask_registers, prio_registers, NULL);

static struct intc_vect vectors_irq[] __initdata = {
	INTC_VECT(IRL0, 0x240), INTC_VECT(IRL1, 0x2a0),
	INTC_VECT(IRL2, 0x300), INTC_VECT(IRL3, 0x360),
};

static DECLARE_INTC_DESC(intc_desc_irq, "sh7760-irq", vectors_irq, groups,
			 mask_registers, prio_registers, NULL);

static struct plat_sci_port scif0_platform_data = {
	.scscr		= SCSCR_REIE,
	.type		= PORT_SCIF,
	.regtype	= SCIx_SH4_SCIF_FIFODATA_REGTYPE,
};

static struct resource scif0_resources[] = {
	DEFINE_RES_MEM(0xfe600000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0x880)),
	DEFINE_RES_IRQ(evt2irq(0x8a0)),
	DEFINE_RES_IRQ(evt2irq(0x8e0)),
	DEFINE_RES_IRQ(evt2irq(0x8c0)),
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
	.type		= PORT_SCIF,
	.scscr		= SCSCR_REIE,
	.regtype	= SCIx_SH4_SCIF_FIFODATA_REGTYPE,
};

static struct resource scif1_resources[] = {
	DEFINE_RES_MEM(0xfe610000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0xb00)),
	DEFINE_RES_IRQ(evt2irq(0xb20)),
	DEFINE_RES_IRQ(evt2irq(0xb60)),
	DEFINE_RES_IRQ(evt2irq(0xb40)),
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
	.regtype	= SCIx_SH4_SCIF_FIFODATA_REGTYPE,
};

static struct resource scif2_resources[] = {
	DEFINE_RES_MEM(0xfe620000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0xb80)),
	DEFINE_RES_IRQ(evt2irq(0xba0)),
	DEFINE_RES_IRQ(evt2irq(0xbe0)),
	DEFINE_RES_IRQ(evt2irq(0xbc0)),
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
	/*
	 * This is actually a SIM card module serial port, based on an SCI with
	 * additional registers. The sh-sci driver doesn't support the SIM port
	 * type, declare it as a SCI. Don't declare the additional registers in
	 * the memory resource or the driver will compute an incorrect regshift
	 * value.
	 */
	.type		= PORT_SCI,
};

static struct resource scif3_resources[] = {
	DEFINE_RES_MEM(0xfe480000, 0x10),
	DEFINE_RES_IRQ(evt2irq(0xc00)),
	DEFINE_RES_IRQ(evt2irq(0xc20)),
	DEFINE_RES_IRQ(evt2irq(0xc40)),
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

static struct sh_timer_config tmu0_platform_data = {
	.channels_mask = 7,
};

static struct resource tmu0_resources[] = {
	DEFINE_RES_MEM(0xffd80000, 0x30),
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


static struct platform_device *sh7760_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&tmu0_device,
};

static int __init sh7760_devices_setup(void)
{
	return platform_add_devices(sh7760_devices,
				    ARRAY_SIZE(sh7760_devices));
}
arch_initcall(sh7760_devices_setup);

static struct platform_device *sh7760_early_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&tmu0_device,
};

void __init plat_early_device_setup(void)
{
	sh_early_platform_add_devices(sh7760_early_devices,
				   ARRAY_SIZE(sh7760_early_devices));
}

#define INTC_ICR	0xffd00000UL
#define INTC_ICR_IRLM	(1 << 7)

void __init plat_irq_setup_pins(int mode)
{
	switch (mode) {
	case IRQ_MODE_IRQ:
		__raw_writew(__raw_readw(INTC_ICR) | INTC_ICR_IRLM, INTC_ICR);
		register_intc_controller(&intc_desc_irq);
		break;
	default:
		BUG();
	}
}

void __init plat_irq_setup(void)
{
	register_intc_controller(&intc_desc);
}
