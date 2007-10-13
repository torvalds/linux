/*
 * SH7785 Setup
 *
 *  Copyright (C) 2007  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <asm/mmzone.h>
#include <asm/sci.h>

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= 0xffea0000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 40, 41, 43, 42 },
	}, {
		.mapbase	= 0xffeb0000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 44, 45, 47, 46 },
	},

	/*
	 * The rest of these all have multiplexed IRQs
	 */
	{
		.mapbase	= 0xffec0000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 60, 60, 60, 60 },
	}, {
		.mapbase	= 0xffed0000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 61, 61, 61, 61 },
	}, {
		.mapbase	= 0xffee0000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 62, 62, 62, 62 },
	}, {
		.mapbase	= 0xffef0000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 63, 63, 63, 63 },
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

static struct platform_device *sh7785_devices[] __initdata = {
	&sci_device,
};

static int __init sh7785_devices_setup(void)
{
	return platform_add_devices(sh7785_devices,
				    ARRAY_SIZE(sh7785_devices));
}
__initcall(sh7785_devices_setup);

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
	WDT,
	TMU0, TMU1, TMU2, TMU2_TICPI,
	HUDI,
	DMAC0_DMINT0, DMAC0_DMINT1, DMAC0_DMINT2, DMAC0_DMINT3,
	DMAC0_DMINT4, DMAC0_DMINT5, DMAC0_DMAE,
	SCIF0_ERI, SCIF0_RXI, SCIF0_BRI, SCIF0_TXI,
	SCIF1_ERI, SCIF1_RXI, SCIF1_BRI, SCIF1_TXI,
	DMAC1_DMINT6, DMAC1_DMINT7, DMAC1_DMINT8, DMAC1_DMINT9,
	DMAC1_DMINT10, DMAC1_DMINT11, DMAC1_DMAE,
	HSPI,
	SCIF2, SCIF3, SCIF4, SCIF5,
	PCISERR, PCIINTA, PCIINTB, PCIINTC, PCIINTD,
	PCIERR, PCIPWD3, PCIPWD2, PCIPWD1, PCIPWD0,
	SIOF,
	MMCIF_FSTAT, MMCIF_TRAN, MMCIF_ERR, MMCIF_FRDY,
	DU,
	GDTA_GACLI, GDTA_GAMCI, GDTA_GAERI,
	TMU3, TMU4, TMU5,
	SSI0, SSI1,
	HAC0, HAC1,
	FLCTL_FLSTE, FLCTL_FLEND, FLCTL_FLTRQ0, FLCTL_FLTRQ1,
	GPIOI0, GPIOI1, GPIOI2, GPIOI3,

	/* interrupt groups */

	TMU012, DMAC0, SCIF0, SCIF1, DMAC1,
	PCIC5, MMCIF, GDTA, TMU345, FLCTL, GPIO
};

static struct intc_vect vectors[] __initdata = {
	INTC_VECT(WDT, 0x560),
	INTC_VECT(TMU0, 0x580), INTC_VECT(TMU1, 0x5a0),
	INTC_VECT(TMU2, 0x5c0), INTC_VECT(TMU2_TICPI, 0x5e0),
	INTC_VECT(HUDI, 0x600),
	INTC_VECT(DMAC0_DMINT0, 0x620), INTC_VECT(DMAC0_DMINT1, 0x640),
	INTC_VECT(DMAC0_DMINT2, 0x660), INTC_VECT(DMAC0_DMINT3, 0x680),
	INTC_VECT(DMAC0_DMINT4, 0x6a0), INTC_VECT(DMAC0_DMINT5, 0x6c0),
	INTC_VECT(DMAC0_DMAE, 0x6e0),
	INTC_VECT(SCIF0_ERI, 0x700), INTC_VECT(SCIF0_RXI, 0x720),
	INTC_VECT(SCIF0_BRI, 0x740), INTC_VECT(SCIF0_TXI, 0x760),
	INTC_VECT(SCIF1_ERI, 0x780), INTC_VECT(SCIF1_RXI, 0x7a0),
	INTC_VECT(SCIF1_BRI, 0x7c0), INTC_VECT(SCIF1_TXI, 0x7e0),
	INTC_VECT(DMAC1_DMINT6, 0x880), INTC_VECT(DMAC1_DMINT7, 0x8a0),
	INTC_VECT(DMAC1_DMINT8, 0x8c0), INTC_VECT(DMAC1_DMINT9, 0x8e0),
	INTC_VECT(DMAC1_DMINT10, 0x900), INTC_VECT(DMAC1_DMINT11, 0x920),
	INTC_VECT(DMAC1_DMAE, 0x940),
	INTC_VECT(HSPI, 0x960),
	INTC_VECT(SCIF2, 0x980), INTC_VECT(SCIF3, 0x9a0),
	INTC_VECT(SCIF4, 0x9c0), INTC_VECT(SCIF5, 0x9e0),
	INTC_VECT(PCISERR, 0xa00), INTC_VECT(PCIINTA, 0xa20),
	INTC_VECT(PCIINTB, 0xa40), INTC_VECT(PCIINTC, 0xa60),
	INTC_VECT(PCIINTD, 0xa80), INTC_VECT(PCIERR, 0xaa0),
	INTC_VECT(PCIPWD3, 0xac0), INTC_VECT(PCIPWD2, 0xae0),
	INTC_VECT(PCIPWD1, 0xb00), INTC_VECT(PCIPWD0, 0xb20),
	INTC_VECT(SIOF, 0xc00),
	INTC_VECT(MMCIF_FSTAT, 0xd00), INTC_VECT(MMCIF_TRAN, 0xd20),
	INTC_VECT(MMCIF_ERR, 0xd40), INTC_VECT(MMCIF_FRDY, 0xd60),
	INTC_VECT(DU, 0xd80),
	INTC_VECT(GDTA_GACLI, 0xda0), INTC_VECT(GDTA_GAMCI, 0xdc0),
	INTC_VECT(GDTA_GAERI, 0xde0),
	INTC_VECT(TMU3, 0xe00), INTC_VECT(TMU4, 0xe20),
	INTC_VECT(TMU5, 0xe40),
	INTC_VECT(SSI0, 0xe80), INTC_VECT(SSI1, 0xea0),
	INTC_VECT(HAC0, 0xec0), INTC_VECT(HAC1, 0xee0),
	INTC_VECT(FLCTL_FLSTE, 0xf00), INTC_VECT(FLCTL_FLEND, 0xf20),
	INTC_VECT(FLCTL_FLTRQ0, 0xf40), INTC_VECT(FLCTL_FLTRQ1, 0xf60),
	INTC_VECT(GPIOI0, 0xf80), INTC_VECT(GPIOI1, 0xfa0),
	INTC_VECT(GPIOI2, 0xfc0), INTC_VECT(GPIOI3, 0xfe0),
};

static struct intc_group groups[] __initdata = {
	INTC_GROUP(TMU012, TMU0, TMU1, TMU2, TMU2_TICPI),
	INTC_GROUP(DMAC0, DMAC0_DMINT0, DMAC0_DMINT1, DMAC0_DMINT2,
		   DMAC0_DMINT3, DMAC0_DMINT4, DMAC0_DMINT5, DMAC0_DMAE),
	INTC_GROUP(SCIF0, SCIF0_ERI, SCIF0_RXI, SCIF0_BRI, SCIF0_TXI),
	INTC_GROUP(SCIF1, SCIF1_ERI, SCIF1_RXI, SCIF1_BRI, SCIF1_TXI),
	INTC_GROUP(DMAC1, DMAC1_DMINT6, DMAC1_DMINT7, DMAC1_DMINT8,
		   DMAC1_DMINT9, DMAC1_DMINT10, DMAC1_DMINT11, DMAC1_DMAE),
	INTC_GROUP(PCIC5, PCIERR, PCIPWD3, PCIPWD2, PCIPWD1, PCIPWD0),
	INTC_GROUP(MMCIF, MMCIF_FSTAT, MMCIF_TRAN, MMCIF_ERR, MMCIF_FRDY),
	INTC_GROUP(GDTA, GDTA_GACLI, GDTA_GAMCI, GDTA_GAERI),
	INTC_GROUP(TMU345, TMU3, TMU4, TMU5),
	INTC_GROUP(FLCTL, FLCTL_FLSTE, FLCTL_FLEND,
		   FLCTL_FLTRQ0, FLCTL_FLTRQ1),
	INTC_GROUP(GPIO, GPIOI0, GPIOI1, GPIOI2, GPIOI3),
};

static struct intc_prio priorities[] __initdata = {
	INTC_PRIO(SCIF0, 3),
	INTC_PRIO(SCIF1, 3),
	INTC_PRIO(SCIF2, 3),
	INTC_PRIO(SCIF3, 3),
	INTC_PRIO(SCIF4, 3),
	INTC_PRIO(SCIF5, 3),
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

static DECLARE_INTC_DESC(intc_desc, "sh7785", vectors, groups, priorities,
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

static DECLARE_INTC_DESC(intc_desc_irq0123, "sh7785-irq0123", vectors_irq0123,
			 NULL, NULL, mask_registers, prio_registers,
			 sense_registers);

static DECLARE_INTC_DESC(intc_desc_irq4567, "sh7785-irq4567", vectors_irq4567,
			 NULL, NULL, mask_registers, prio_registers,
			 sense_registers);

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
			 NULL, NULL, mask_registers, NULL, NULL);

static DECLARE_INTC_DESC(intc_desc_irl4567, "sh7785-irl4567", vectors_irl4567,
			 NULL, NULL, mask_registers, NULL, NULL);

#define INTC_ICR0	0xffd00000
#define INTC_INTMSK0	0xffd00044
#define INTC_INTMSK1	0xffd00048
#define INTC_INTMSK2	0xffd40080
#define INTC_INTMSKCLR1	0xffd00068
#define INTC_INTMSKCLR2	0xffd40084

void __init plat_irq_setup(void)
{
	/* disable IRQ3-0 + IRQ7-4 */
	ctrl_outl(0xff000000, INTC_INTMSK0);

	/* disable IRL3-0 + IRL7-4 */
	ctrl_outl(0xc0000000, INTC_INTMSK1);
	ctrl_outl(0xfffefffe, INTC_INTMSK2);

	/* select IRL mode for IRL3-0 + IRL7-4 */
	ctrl_outl(ctrl_inl(INTC_ICR0) & ~0x00c00000, INTC_ICR0);

	/* disable holding function, ie enable "SH-4 Mode" */
	ctrl_outl(ctrl_inl(INTC_ICR0) | 0x00200000, INTC_ICR0);

	register_intc_controller(&intc_desc);
}

void __init plat_irq_setup_pins(int mode)
{
	switch (mode) {
	case IRQ_MODE_IRQ7654:
		/* select IRQ mode for IRL7-4 */
		ctrl_outl(ctrl_inl(INTC_ICR0) | 0x00400000, INTC_ICR0);
		register_intc_controller(&intc_desc_irq4567);
		break;
	case IRQ_MODE_IRQ3210:
		/* select IRQ mode for IRL3-0 */
		ctrl_outl(ctrl_inl(INTC_ICR0) | 0x00800000, INTC_ICR0);
		register_intc_controller(&intc_desc_irq0123);
		break;
	case IRQ_MODE_IRL7654:
		/* enable IRL7-4 but don't provide any masking */
		ctrl_outl(0x40000000, INTC_INTMSKCLR1);
		ctrl_outl(0x0000fffe, INTC_INTMSKCLR2);
		break;
	case IRQ_MODE_IRL3210:
		/* enable IRL0-3 but don't provide any masking */
		ctrl_outl(0x80000000, INTC_INTMSKCLR1);
		ctrl_outl(0xfffe0000, INTC_INTMSKCLR2);
		break;
	case IRQ_MODE_IRL7654_MASK:
		/* enable IRL7-4 and mask using cpu intc controller */
		ctrl_outl(0x40000000, INTC_INTMSKCLR1);
		register_intc_controller(&intc_desc_irl4567);
		break;
	case IRQ_MODE_IRL3210_MASK:
		/* enable IRL0-3 and mask using cpu intc controller */
		ctrl_outl(0x80000000, INTC_INTMSKCLR1);
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
