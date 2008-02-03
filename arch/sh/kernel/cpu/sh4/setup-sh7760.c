/*
 * SH7760 Setup
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
#include <asm/sci.h>

enum {
	UNUSED = 0,

	/* interrupt sources */
	IRL0, IRL1, IRL2, IRL3,
	HUDI, GPIOI,
	DMAC_DMTE0, DMAC_DMTE1, DMAC_DMTE2, DMAC_DMTE3,
	DMAC_DMTE4, DMAC_DMTE5, DMAC_DMTE6, DMAC_DMTE7,
	DMAC_DMAE,
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
	TMU0, TMU1, TMU2_TUNI, TMU2_TICPI,
	WDT,
	REF_RCMI, REF_ROVI,

	/* interrupt groups */
	DMAC, DMABRG, SCIF0, SCIF1, SCIF2, SIM, MMCIF, TMU2, REF,
};

static struct intc_vect vectors[] __initdata = {
	INTC_VECT(HUDI, 0x600), INTC_VECT(GPIOI, 0x620),
	INTC_VECT(DMAC_DMTE0, 0x640), INTC_VECT(DMAC_DMTE1, 0x660),
	INTC_VECT(DMAC_DMTE2, 0x680), INTC_VECT(DMAC_DMTE3, 0x6a0),
	INTC_VECT(DMAC_DMTE4, 0x780), INTC_VECT(DMAC_DMTE5, 0x7a0),
	INTC_VECT(DMAC_DMTE6, 0x7c0), INTC_VECT(DMAC_DMTE7, 0x7e0),
	INTC_VECT(DMAC_DMAE, 0x6c0),
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
	INTC_VECT(TMU2_TUNI, 0x440), INTC_VECT(TMU2_TICPI, 0x460),
	INTC_VECT(WDT, 0x560),
	INTC_VECT(REF_RCMI, 0x580), INTC_VECT(REF_ROVI, 0x5a0),
};

static struct intc_group groups[] __initdata = {
	INTC_GROUP(DMAC, DMAC_DMTE0, DMAC_DMTE1, DMAC_DMTE2,
		   DMAC_DMTE3, DMAC_DMTE4, DMAC_DMTE5,
		   DMAC_DMTE6, DMAC_DMTE7, DMAC_DMAE),
	INTC_GROUP(DMABRG, DMABRG0, DMABRG1, DMABRG2),
	INTC_GROUP(SCIF0, SCIF0_ERI, SCIF0_RXI, SCIF0_BRI, SCIF0_TXI),
	INTC_GROUP(SCIF1, SCIF1_ERI, SCIF1_RXI, SCIF1_BRI, SCIF1_TXI),
	INTC_GROUP(SCIF2, SCIF2_ERI, SCIF2_RXI, SCIF2_BRI, SCIF2_TXI),
	INTC_GROUP(SIM, SIM_ERI, SIM_RXI, SIM_TXI, SIM_TEI),
	INTC_GROUP(MMCIF, MMCIF0, MMCIF1, MMCIF2, MMCIF3),
	INTC_GROUP(TMU2, TMU2_TUNI, TMU2_TICPI),
	INTC_GROUP(REF, REF_RCMI, REF_ROVI),
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

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= 0xfe600000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 52, 53, 55, 54 },
	}, {
		.mapbase	= 0xfe610000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 72, 73, 75, 74 },
	}, {
		.mapbase	= 0xfe620000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 76, 77, 79, 78 },
	}, {
		.mapbase	= 0xfe480000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCI,
		.irqs		= { 80, 81, 82, 0 },
	}, {
		.flags = 0,
	}
};

static struct platform_device sci_device = {
	.name		= "sh-sci",
	.id		= -1,
	.dev		= {
		.platform_data	= sci_platform_data,
	},
};

static struct platform_device *sh7760_devices[] __initdata = {
	&sci_device,
};

static int __init sh7760_devices_setup(void)
{
	return platform_add_devices(sh7760_devices,
				    ARRAY_SIZE(sh7760_devices));
}
__initcall(sh7760_devices_setup);

void __init plat_irq_setup_pins(int mode)
{
	switch (mode) {
	case IRQ_MODE_IRQ:
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
