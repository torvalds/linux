/*
 * SH7091/SH7750/SH7750S/SH7750R/SH7751/SH7751R Setup
 *
 *  Copyright (C) 2006  Paul Mundt
 *  Copyright (C) 2006  Jamie Lenehan
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/io.h>
#include <linux/sh_timer.h>
#include <linux/sh_intc.h>
#include <linux/serial_sci.h>
#include <generated/machtypes.h>

static struct resource rtc_resources[] = {
	[0] = {
		.start	= 0xffc80000,
		.end	= 0xffc80000 + 0x58 - 1,
		.flags	= IORESOURCE_IO,
	},
	[1] = {
		/* Shared Period/Carry/Alarm IRQ */
		.start	= evt2irq(0x480),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device rtc_device = {
	.name		= "sh-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rtc_resources),
	.resource	= rtc_resources,
};

static struct plat_sci_port sci_platform_data = {
	.port_reg	= 0xffe0001C,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_TE | SCSCR_RE,
	.type		= PORT_SCI,
	.regshift	= 2,
};

static struct resource sci_resources[] = {
	DEFINE_RES_MEM(0xffe00000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0x4e0)),
};

static struct platform_device sci_device = {
	.name		= "sh-sci",
	.id		= 0,
	.resource	= sci_resources,
	.num_resources	= ARRAY_SIZE(sci_resources),
	.dev		= {
		.platform_data	= &sci_platform_data,
	},
};

static struct plat_sci_port scif_platform_data = {
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_TE | SCSCR_RE | SCSCR_REIE,
	.type		= PORT_SCIF,
};

static struct resource scif_resources[] = {
	DEFINE_RES_MEM(0xffe80000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0x700)),
};

static struct platform_device scif_device = {
	.name		= "sh-sci",
	.id		= 1,
	.resource	= scif_resources,
	.num_resources	= ARRAY_SIZE(scif_resources),
	.dev		= {
		.platform_data	= &scif_platform_data,
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

/* SH7750R, SH7751 and SH7751R all have two extra timer channels */
#if defined(CONFIG_CPU_SUBTYPE_SH7750R) || \
	defined(CONFIG_CPU_SUBTYPE_SH7751) || \
	defined(CONFIG_CPU_SUBTYPE_SH7751R)

static struct sh_timer_config tmu1_platform_data = {
	.channels_mask = 3,
};

static struct resource tmu1_resources[] = {
	DEFINE_RES_MEM(0xfe100000, 0x20),
	DEFINE_RES_IRQ(evt2irq(0xb00)),
	DEFINE_RES_IRQ(evt2irq(0xb80)),
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

#endif

static struct platform_device *sh7750_devices[] __initdata = {
	&rtc_device,
	&tmu0_device,
#if defined(CONFIG_CPU_SUBTYPE_SH7750R) || \
	defined(CONFIG_CPU_SUBTYPE_SH7751) || \
	defined(CONFIG_CPU_SUBTYPE_SH7751R)
	&tmu1_device,
#endif
};

static int __init sh7750_devices_setup(void)
{
	if (mach_is_rts7751r2d()) {
		platform_device_register(&scif_device);
	} else {
		platform_device_register(&sci_device);
		platform_device_register(&scif_device);
	}

	return platform_add_devices(sh7750_devices,
				    ARRAY_SIZE(sh7750_devices));
}
arch_initcall(sh7750_devices_setup);

static struct platform_device *sh7750_early_devices[] __initdata = {
	&tmu0_device,
#if defined(CONFIG_CPU_SUBTYPE_SH7750R) || \
	defined(CONFIG_CPU_SUBTYPE_SH7751) || \
	defined(CONFIG_CPU_SUBTYPE_SH7751R)
	&tmu1_device,
#endif
};

void __init plat_early_device_setup(void)
{
	struct platform_device *dev[1];

	if (mach_is_rts7751r2d()) {
		scif_platform_data.scscr |= SCSCR_CKE1;
		dev[0] = &scif_device;
		early_platform_add_devices(dev, 1);
	} else {
		dev[0] = &sci_device;
		early_platform_add_devices(dev, 1);
		dev[0] = &scif_device;
		early_platform_add_devices(dev, 1);
	}

	early_platform_add_devices(sh7750_early_devices,
				   ARRAY_SIZE(sh7750_early_devices));
}

enum {
	UNUSED = 0,

	/* interrupt sources */
	IRL0, IRL1, IRL2, IRL3, /* only IRLM mode supported */
	HUDI, GPIOI, DMAC,
	PCIC0_PCISERR, PCIC1_PCIERR, PCIC1_PCIPWDWN, PCIC1_PCIPWON,
	PCIC1_PCIDMA0, PCIC1_PCIDMA1, PCIC1_PCIDMA2, PCIC1_PCIDMA3,
	TMU3, TMU4, TMU0, TMU1, TMU2, RTC, SCI1, SCIF, WDT, REF,

	/* interrupt groups */
	PCIC1,
};

static struct intc_vect vectors[] __initdata = {
	INTC_VECT(HUDI, 0x600), INTC_VECT(GPIOI, 0x620),
	INTC_VECT(TMU0, 0x400), INTC_VECT(TMU1, 0x420),
	INTC_VECT(TMU2, 0x440), INTC_VECT(TMU2, 0x460),
	INTC_VECT(RTC, 0x480), INTC_VECT(RTC, 0x4a0),
	INTC_VECT(RTC, 0x4c0),
	INTC_VECT(SCI1, 0x4e0), INTC_VECT(SCI1, 0x500),
	INTC_VECT(SCI1, 0x520), INTC_VECT(SCI1, 0x540),
	INTC_VECT(SCIF, 0x700), INTC_VECT(SCIF, 0x720),
	INTC_VECT(SCIF, 0x740), INTC_VECT(SCIF, 0x760),
	INTC_VECT(WDT, 0x560),
	INTC_VECT(REF, 0x580), INTC_VECT(REF, 0x5a0),
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xffd00004, 0, 16, 4, /* IPRA */ { TMU0, TMU1, TMU2, RTC } },
	{ 0xffd00008, 0, 16, 4, /* IPRB */ { WDT, REF, SCI1, 0 } },
	{ 0xffd0000c, 0, 16, 4, /* IPRC */ { GPIOI, DMAC, SCIF, HUDI } },
	{ 0xffd00010, 0, 16, 4, /* IPRD */ { IRL0, IRL1, IRL2, IRL3 } },
	{ 0xfe080000, 0, 32, 4, /* INTPRI00 */ { 0, 0, 0, 0,
						 TMU4, TMU3,
						 PCIC1, PCIC0_PCISERR } },
};

static DECLARE_INTC_DESC(intc_desc, "sh7750", vectors, NULL,
			 NULL, prio_registers, NULL);

/* SH7750, SH7750S, SH7751 and SH7091 all have 4-channel DMA controllers */
#if defined(CONFIG_CPU_SUBTYPE_SH7750) || \
	defined(CONFIG_CPU_SUBTYPE_SH7750S) || \
	defined(CONFIG_CPU_SUBTYPE_SH7751) || \
	defined(CONFIG_CPU_SUBTYPE_SH7091)
static struct intc_vect vectors_dma4[] __initdata = {
	INTC_VECT(DMAC, 0x640), INTC_VECT(DMAC, 0x660),
	INTC_VECT(DMAC, 0x680), INTC_VECT(DMAC, 0x6a0),
	INTC_VECT(DMAC, 0x6c0),
};

static DECLARE_INTC_DESC(intc_desc_dma4, "sh7750_dma4",
			 vectors_dma4, NULL,
			 NULL, prio_registers, NULL);
#endif

/* SH7750R and SH7751R both have 8-channel DMA controllers */
#if defined(CONFIG_CPU_SUBTYPE_SH7750R) || defined(CONFIG_CPU_SUBTYPE_SH7751R)
static struct intc_vect vectors_dma8[] __initdata = {
	INTC_VECT(DMAC, 0x640), INTC_VECT(DMAC, 0x660),
	INTC_VECT(DMAC, 0x680), INTC_VECT(DMAC, 0x6a0),
	INTC_VECT(DMAC, 0x780), INTC_VECT(DMAC, 0x7a0),
	INTC_VECT(DMAC, 0x7c0), INTC_VECT(DMAC, 0x7e0),
	INTC_VECT(DMAC, 0x6c0),
};

static DECLARE_INTC_DESC(intc_desc_dma8, "sh7750_dma8",
			 vectors_dma8, NULL,
			 NULL, prio_registers, NULL);
#endif

/* SH7750R, SH7751 and SH7751R all have two extra timer channels */
#if defined(CONFIG_CPU_SUBTYPE_SH7750R) || \
	defined(CONFIG_CPU_SUBTYPE_SH7751) || \
	defined(CONFIG_CPU_SUBTYPE_SH7751R)
static struct intc_vect vectors_tmu34[] __initdata = {
	INTC_VECT(TMU3, 0xb00), INTC_VECT(TMU4, 0xb80),
};

static struct intc_mask_reg mask_registers[] __initdata = {
	{ 0xfe080040, 0xfe080060, 32, /* INTMSK00 / INTMSKCLR00 */
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	    0, 0, 0, 0, 0, 0, TMU4, TMU3,
	    PCIC1_PCIERR, PCIC1_PCIPWDWN, PCIC1_PCIPWON,
	    PCIC1_PCIDMA0, PCIC1_PCIDMA1, PCIC1_PCIDMA2,
	    PCIC1_PCIDMA3, PCIC0_PCISERR } },
};

static DECLARE_INTC_DESC(intc_desc_tmu34, "sh7750_tmu34",
			 vectors_tmu34, NULL,
			 mask_registers, prio_registers, NULL);
#endif

/* SH7750S, SH7750R, SH7751 and SH7751R all have IRLM priority registers */
static struct intc_vect vectors_irlm[] __initdata = {
	INTC_VECT(IRL0, 0x240), INTC_VECT(IRL1, 0x2a0),
	INTC_VECT(IRL2, 0x300), INTC_VECT(IRL3, 0x360),
};

static DECLARE_INTC_DESC(intc_desc_irlm, "sh7750_irlm", vectors_irlm, NULL,
			 NULL, prio_registers, NULL);

/* SH7751 and SH7751R both have PCI */
#if defined(CONFIG_CPU_SUBTYPE_SH7751) || defined(CONFIG_CPU_SUBTYPE_SH7751R)
static struct intc_vect vectors_pci[] __initdata = {
	INTC_VECT(PCIC0_PCISERR, 0xa00), INTC_VECT(PCIC1_PCIERR, 0xae0),
	INTC_VECT(PCIC1_PCIPWDWN, 0xac0), INTC_VECT(PCIC1_PCIPWON, 0xaa0),
	INTC_VECT(PCIC1_PCIDMA0, 0xa80), INTC_VECT(PCIC1_PCIDMA1, 0xa60),
	INTC_VECT(PCIC1_PCIDMA2, 0xa40), INTC_VECT(PCIC1_PCIDMA3, 0xa20),
};

static struct intc_group groups_pci[] __initdata = {
	INTC_GROUP(PCIC1, PCIC1_PCIERR, PCIC1_PCIPWDWN, PCIC1_PCIPWON,
		   PCIC1_PCIDMA0, PCIC1_PCIDMA1, PCIC1_PCIDMA2, PCIC1_PCIDMA3),
};

static DECLARE_INTC_DESC(intc_desc_pci, "sh7750_pci", vectors_pci, groups_pci,
			 mask_registers, prio_registers, NULL);
#endif

#if defined(CONFIG_CPU_SUBTYPE_SH7750) || \
	defined(CONFIG_CPU_SUBTYPE_SH7750S) || \
	defined(CONFIG_CPU_SUBTYPE_SH7091)
void __init plat_irq_setup(void)
{
	/*
	 * same vectors for SH7750, SH7750S and SH7091 except for IRLM,
	 * see below..
	 */
	register_intc_controller(&intc_desc);
	register_intc_controller(&intc_desc_dma4);
}
#endif

#if defined(CONFIG_CPU_SUBTYPE_SH7750R)
void __init plat_irq_setup(void)
{
	register_intc_controller(&intc_desc);
	register_intc_controller(&intc_desc_dma8);
	register_intc_controller(&intc_desc_tmu34);
}
#endif

#if defined(CONFIG_CPU_SUBTYPE_SH7751)
void __init plat_irq_setup(void)
{
	register_intc_controller(&intc_desc);
	register_intc_controller(&intc_desc_dma4);
	register_intc_controller(&intc_desc_tmu34);
	register_intc_controller(&intc_desc_pci);
}
#endif

#if defined(CONFIG_CPU_SUBTYPE_SH7751R)
void __init plat_irq_setup(void)
{
	register_intc_controller(&intc_desc);
	register_intc_controller(&intc_desc_dma8);
	register_intc_controller(&intc_desc_tmu34);
	register_intc_controller(&intc_desc_pci);
}
#endif

#define INTC_ICR	0xffd00000UL
#define INTC_ICR_IRLM   (1<<7)

void __init plat_irq_setup_pins(int mode)
{
#if defined(CONFIG_CPU_SUBTYPE_SH7750) || defined(CONFIG_CPU_SUBTYPE_SH7091)
	BUG(); /* impossible to mask interrupts on SH7750 and SH7091 */
	return;
#endif

	switch (mode) {
	case IRQ_MODE_IRQ: /* individual interrupt mode for IRL3-0 */
		__raw_writew(__raw_readw(INTC_ICR) | INTC_ICR_IRLM, INTC_ICR);
		register_intc_controller(&intc_desc_irlm);
		break;
	default:
		BUG();
	}
}
