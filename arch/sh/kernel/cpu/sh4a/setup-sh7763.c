/*
 * SH7763 Setup
 *
 *  Copyright (C) 2006  Paul Mundt
 *  Copyright (C) 2007  Yoshihiro Shimoda
 *  Copyright (C) 2008, 2009  Nobuhiro Iwamatsu
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/sh_timer.h>
#include <linux/io.h>
#include <linux/serial_sci.h>

static struct plat_sci_port scif0_platform_data = {
	.mapbase	= 0xffe00000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE | SCSCR_REIE,
	.scbrr_algo_id	= SCBRR_ALGO_2,
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
	.mapbase	= 0xffe08000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE | SCSCR_REIE,
	.scbrr_algo_id	= SCBRR_ALGO_2,
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

static struct plat_sci_port scif2_platform_data = {
	.mapbase	= 0xffe10000,
	.flags		= UPF_BOOT_AUTOCONF,
	.scscr		= SCSCR_RE | SCSCR_TE | SCSCR_REIE,
	.scbrr_algo_id	= SCBRR_ALGO_2,
	.type		= PORT_SCIF,
	.irqs		= { 104, 104, 104, 104 },
};

static struct platform_device scif2_device = {
	.name		= "sh-sci",
	.id		= 2,
	.dev		= {
		.platform_data	= &scif2_platform_data,
	},
};

static struct resource rtc_resources[] = {
	[0] = {
		.start	= 0xffe80000,
		.end	= 0xffe80000 + 0x58 - 1,
		.flags	= IORESOURCE_IO,
	},
	[1] = {
		/* Shared Period/Carry/Alarm IRQ */
		.start  = 20,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device rtc_device = {
	.name		= "sh-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rtc_resources),
	.resource	= rtc_resources,
};

static struct resource usb_ohci_resources[] = {
	[0] = {
		.start	= 0xffec8000,
		.end	= 0xffec80ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 83,
		.end	= 83,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 usb_ohci_dma_mask = 0xffffffffUL;
static struct platform_device usb_ohci_device = {
	.name		= "sh_ohci",
	.id		= -1,
	.dev = {
		.dma_mask		= &usb_ohci_dma_mask,
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(usb_ohci_resources),
	.resource	= usb_ohci_resources,
};

static struct resource usbf_resources[] = {
	[0] = {
		.start	= 0xffec0000,
		.end	= 0xffec00ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 84,
		.end	= 84,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device usbf_device = {
	.name		= "sh_udc",
	.id		= -1,
	.dev = {
		.dma_mask		= NULL,
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(usbf_resources),
	.resource	= usbf_resources,
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
	.channel_offset = 0x04,
	.timer_bit = 0,
};

static struct resource tmu3_resources[] = {
	[0] = {
		.start	= 0xffd88008,
		.end	= 0xffd88013,
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
	.channel_offset = 0x10,
	.timer_bit = 1,
};

static struct resource tmu4_resources[] = {
	[0] = {
		.start	= 0xffd88014,
		.end	= 0xffd8801f,
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
	.channel_offset = 0x1c,
	.timer_bit = 2,
};

static struct resource tmu5_resources[] = {
	[0] = {
		.start	= 0xffd88020,
		.end	= 0xffd8802b,
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

static struct platform_device *sh7763_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&tmu0_device,
	&tmu1_device,
	&tmu2_device,
	&tmu3_device,
	&tmu4_device,
	&tmu5_device,
	&rtc_device,
	&usb_ohci_device,
	&usbf_device,
};

static int __init sh7763_devices_setup(void)
{
	return platform_add_devices(sh7763_devices,
				    ARRAY_SIZE(sh7763_devices));
}
arch_initcall(sh7763_devices_setup);

static struct platform_device *sh7763_early_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&tmu0_device,
	&tmu1_device,
	&tmu2_device,
	&tmu3_device,
	&tmu4_device,
	&tmu5_device,
};

void __init plat_early_device_setup(void)
{
	early_platform_add_devices(sh7763_early_devices,
				   ARRAY_SIZE(sh7763_early_devices));
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
	HUDI, LCDC, DMAC, SCIF0, IIC0, IIC1, CMT, GETHER, HAC,
	PCISERR, PCIINTA, PCIINTB, PCIINTC, PCIINTD, PCIC5,
	STIF0, STIF1, SCIF1, SIOF0, SIOF1, SIOF2,
	USBH, USBF, TPU, PCC, MMCIF, SIM,
	TMU3, TMU4, TMU5, ADC, SSI0, SSI1, SSI2, SSI3,
	SCIF2, GPIO,

	/* interrupt groups */

	TMU012, TMU345,
};

static struct intc_vect vectors[] __initdata = {
	INTC_VECT(RTC, 0x480), INTC_VECT(RTC, 0x4a0),
	INTC_VECT(RTC, 0x4c0),
	INTC_VECT(WDT, 0x560), INTC_VECT(TMU0, 0x580),
	INTC_VECT(TMU1, 0x5a0), INTC_VECT(TMU2, 0x5c0),
	INTC_VECT(TMU2_TICPI, 0x5e0), INTC_VECT(HUDI, 0x600),
	INTC_VECT(LCDC, 0x620),
	INTC_VECT(DMAC, 0x640), INTC_VECT(DMAC, 0x660),
	INTC_VECT(DMAC, 0x680), INTC_VECT(DMAC, 0x6a0),
	INTC_VECT(DMAC, 0x6c0),
	INTC_VECT(SCIF0, 0x700), INTC_VECT(SCIF0, 0x720),
	INTC_VECT(SCIF0, 0x740), INTC_VECT(SCIF0, 0x760),
	INTC_VECT(DMAC, 0x780), INTC_VECT(DMAC, 0x7a0),
	INTC_VECT(IIC0, 0x8A0), INTC_VECT(IIC1, 0x8C0),
	INTC_VECT(CMT, 0x900), INTC_VECT(GETHER, 0x920),
	INTC_VECT(GETHER, 0x940), INTC_VECT(GETHER, 0x960),
	INTC_VECT(HAC, 0x980),
	INTC_VECT(PCISERR, 0xa00), INTC_VECT(PCIINTA, 0xa20),
	INTC_VECT(PCIINTB, 0xa40), INTC_VECT(PCIINTC, 0xa60),
	INTC_VECT(PCIINTD, 0xa80), INTC_VECT(PCIC5, 0xaa0),
	INTC_VECT(PCIC5, 0xac0), INTC_VECT(PCIC5, 0xae0),
	INTC_VECT(PCIC5, 0xb00), INTC_VECT(PCIC5, 0xb20),
	INTC_VECT(STIF0, 0xb40), INTC_VECT(STIF1, 0xb60),
	INTC_VECT(SCIF1, 0xb80), INTC_VECT(SCIF1, 0xba0),
	INTC_VECT(SCIF1, 0xbc0), INTC_VECT(SCIF1, 0xbe0),
	INTC_VECT(SIOF0, 0xc00), INTC_VECT(SIOF1, 0xc20),
	INTC_VECT(USBH, 0xc60), INTC_VECT(USBF, 0xc80),
	INTC_VECT(USBF, 0xca0),
	INTC_VECT(TPU, 0xcc0), INTC_VECT(PCC, 0xce0),
	INTC_VECT(MMCIF, 0xd00), INTC_VECT(MMCIF, 0xd20),
	INTC_VECT(MMCIF, 0xd40), INTC_VECT(MMCIF, 0xd60),
	INTC_VECT(SIM, 0xd80), INTC_VECT(SIM, 0xda0),
	INTC_VECT(SIM, 0xdc0), INTC_VECT(SIM, 0xde0),
	INTC_VECT(TMU3, 0xe00), INTC_VECT(TMU4, 0xe20),
	INTC_VECT(TMU5, 0xe40), INTC_VECT(ADC, 0xe60),
	INTC_VECT(SSI0, 0xe80), INTC_VECT(SSI1, 0xea0),
	INTC_VECT(SSI2, 0xec0), INTC_VECT(SSI3, 0xee0),
	INTC_VECT(SCIF2, 0xf00), INTC_VECT(SCIF2, 0xf20),
	INTC_VECT(SCIF2, 0xf40), INTC_VECT(SCIF2, 0xf60),
	INTC_VECT(GPIO, 0xf80), INTC_VECT(GPIO, 0xfa0),
	INTC_VECT(GPIO, 0xfc0), INTC_VECT(GPIO, 0xfe0),
};

static struct intc_group groups[] __initdata = {
	INTC_GROUP(TMU012, TMU0, TMU1, TMU2, TMU2_TICPI),
	INTC_GROUP(TMU345, TMU3, TMU4, TMU5),
};

static struct intc_mask_reg mask_registers[] __initdata = {
	{ 0xffd40038, 0xffd4003c, 32, /* INT2MSKR / INT2MSKCR */
	  { 0, 0, 0, 0, 0, 0, GPIO, 0,
	    SSI0, MMCIF, 0, SIOF0, PCIC5, PCIINTD, PCIINTC, PCIINTB,
	    PCIINTA, PCISERR, HAC, CMT, 0, 0, 0, DMAC,
	    HUDI, 0, WDT, SCIF1, SCIF0, RTC, TMU345, TMU012 } },
	{ 0xffd400d0, 0xffd400d4, 32, /* INT2MSKR1 / INT2MSKCR1 */
	  { 0, 0, 0, 0, 0, 0, SCIF2, USBF,
	    0, 0, STIF1, STIF0, 0, 0, USBH, GETHER,
	    PCC, 0, 0, ADC, TPU, SIM, SIOF2, SIOF1,
	    LCDC, 0, IIC1, IIC0, SSI3, SSI2, SSI1, 0 } },
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xffd40000, 0, 32, 8, /* INT2PRI0 */ { TMU0, TMU1,
						 TMU2, TMU2_TICPI } },
	{ 0xffd40004, 0, 32, 8, /* INT2PRI1 */ { TMU3, TMU4, TMU5, RTC } },
	{ 0xffd40008, 0, 32, 8, /* INT2PRI2 */ { SCIF0, SCIF1, WDT } },
	{ 0xffd4000c, 0, 32, 8, /* INT2PRI3 */ { HUDI, DMAC, ADC } },
	{ 0xffd40010, 0, 32, 8, /* INT2PRI4 */ { CMT, HAC,
						 PCISERR, PCIINTA } },
	{ 0xffd40014, 0, 32, 8, /* INT2PRI5 */ { PCIINTB, PCIINTC,
						 PCIINTD, PCIC5 } },
	{ 0xffd40018, 0, 32, 8, /* INT2PRI6 */ { SIOF0, USBF, MMCIF, SSI0 } },
	{ 0xffd4001c, 0, 32, 8, /* INT2PRI7 */ { SCIF2, GPIO } },
	{ 0xffd400a0, 0, 32, 8, /* INT2PRI8 */ { SSI3, SSI2, SSI1, 0 } },
	{ 0xffd400a4, 0, 32, 8, /* INT2PRI9 */ { LCDC, 0, IIC1, IIC0 } },
	{ 0xffd400a8, 0, 32, 8, /* INT2PRI10 */ { TPU, SIM, SIOF2, SIOF1 } },
	{ 0xffd400ac, 0, 32, 8, /* INT2PRI11 */ { PCC } },
	{ 0xffd400b0, 0, 32, 8, /* INT2PRI12 */ { 0, 0, USBH, GETHER } },
	{ 0xffd400b4, 0, 32, 8, /* INT2PRI13 */ { 0, 0, STIF1, STIF0 } },
};

static DECLARE_INTC_DESC(intc_desc, "sh7763", vectors, groups,
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

static DECLARE_INTC_DESC_ACK(intc_irq_desc, "sh7763-irq", irq_vectors,
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

static DECLARE_INTC_DESC(intc_irl7654_desc, "sh7763-irl7654", irl_vectors,
			NULL, irl7654_mask_registers, NULL, NULL);

static DECLARE_INTC_DESC(intc_irl3210_desc, "sh7763-irl3210", irl_vectors,
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
