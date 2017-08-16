/*
 * SH7786 Setup
 *
 * Copyright (C) 2009 - 2011  Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 * Paul Mundt <paul.mundt@renesas.com>
 *
 * Based on SH7785 Setup
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
#include <linux/serial_sci.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/sh_timer.h>
#include <linux/sh_dma.h>
#include <linux/sh_intc.h>
#include <linux/usb/ohci_pdriver.h>
#include <cpu/dma-register.h>
#include <asm/mmzone.h>

static struct plat_sci_port scif0_platform_data = {
	.scscr		= SCSCR_REIE | SCSCR_CKE1,
	.type		= PORT_SCIF,
	.regtype	= SCIx_SH4_SCIF_FIFODATA_REGTYPE,
};

static struct resource scif0_resources[] = {
	DEFINE_RES_MEM(0xffea0000, 0x100),
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

/*
 * The rest of these all have multiplexed IRQs
 */
static struct plat_sci_port scif1_platform_data = {
	.scscr		= SCSCR_REIE | SCSCR_CKE1,
	.type		= PORT_SCIF,
	.regtype	= SCIx_SH4_SCIF_FIFODATA_REGTYPE,
};

static struct resource scif1_resources[] = {
	DEFINE_RES_MEM(0xffeb0000, 0x100),
	DEFINE_RES_IRQ(evt2irq(0x780)),
};

static struct resource scif1_demux_resources[] = {
	DEFINE_RES_MEM(0xffeb0000, 0x100),
	/* Placeholders, see sh7786_devices_setup() */
	DEFINE_RES_IRQ(0),
	DEFINE_RES_IRQ(0),
	DEFINE_RES_IRQ(0),
	DEFINE_RES_IRQ(0),
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
	DEFINE_RES_IRQ(evt2irq(0x840)),
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
	DEFINE_RES_IRQ(evt2irq(0x860)),
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
	DEFINE_RES_IRQ(evt2irq(0x880)),
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
	DEFINE_RES_IRQ(evt2irq(0x8a0)),
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
	DEFINE_RES_MEM(0xffda0000, 0x2c),
	DEFINE_RES_IRQ(evt2irq(0x480)),
	DEFINE_RES_IRQ(evt2irq(0x4a0)),
	DEFINE_RES_IRQ(evt2irq(0x4c0)),
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

static struct sh_timer_config tmu2_platform_data = {
	.channels_mask = 7,
};

static struct resource tmu2_resources[] = {
	DEFINE_RES_MEM(0xffdc0000, 0x2c),
	DEFINE_RES_IRQ(evt2irq(0x7a0)),
	DEFINE_RES_IRQ(evt2irq(0x7a0)),
	DEFINE_RES_IRQ(evt2irq(0x7a0)),
};

static struct platform_device tmu2_device = {
	.name		= "sh-tmu",
	.id		= 2,
	.dev = {
		.platform_data	= &tmu2_platform_data,
	},
	.resource	= tmu2_resources,
	.num_resources	= ARRAY_SIZE(tmu2_resources),
};

static struct sh_timer_config tmu3_platform_data = {
	.channels_mask = 7,
};

static struct resource tmu3_resources[] = {
	DEFINE_RES_MEM(0xffde0000, 0x2c),
	DEFINE_RES_IRQ(evt2irq(0x7c0)),
	DEFINE_RES_IRQ(evt2irq(0x7c0)),
	DEFINE_RES_IRQ(evt2irq(0x7c0)),
};

static struct platform_device tmu3_device = {
	.name		= "sh-tmu",
	.id		= 3,
	.dev = {
		.platform_data	= &tmu3_platform_data,
	},
	.resource	= tmu3_resources,
	.num_resources	= ARRAY_SIZE(tmu3_resources),
};

static const struct sh_dmae_channel dmac0_channels[] = {
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

static const unsigned int ts_shift[] = TS_SHIFT;

static struct sh_dmae_pdata dma0_platform_data = {
	.channel	= dmac0_channels,
	.channel_num	= ARRAY_SIZE(dmac0_channels),
	.ts_low_shift	= CHCR_TS_LOW_SHIFT,
	.ts_low_mask	= CHCR_TS_LOW_MASK,
	.ts_high_shift	= CHCR_TS_HIGH_SHIFT,
	.ts_high_mask	= CHCR_TS_HIGH_MASK,
	.ts_shift	= ts_shift,
	.ts_shift_num	= ARRAY_SIZE(ts_shift),
	.dmaor_init	= DMAOR_INIT,
};

/* Resource order important! */
static struct resource dmac0_resources[] = {
	{
		/* Channel registers and DMAOR */
		.start	= 0xfe008020,
		.end	= 0xfe00808f,
		.flags	= IORESOURCE_MEM,
	}, {
		/* DMARSx */
		.start	= 0xfe009000,
		.end	= 0xfe00900b,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "error_irq",
		.start	= evt2irq(0x5c0),
		.end	= evt2irq(0x5c0),
		.flags	= IORESOURCE_IRQ,
	}, {
		/* IRQ for channels 0-5 */
		.start	= evt2irq(0x500),
		.end	= evt2irq(0x5a0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device dma0_device = {
	.name		= "sh-dma-engine",
	.id		= 0,
	.resource	= dmac0_resources,
	.num_resources	= ARRAY_SIZE(dmac0_resources),
	.dev		= {
		.platform_data	= &dma0_platform_data,
	},
};

#define USB_EHCI_START 0xffe70000
#define USB_OHCI_START 0xffe70400

static struct resource usb_ehci_resources[] = {
	[0] = {
		.start	= USB_EHCI_START,
		.end	= USB_EHCI_START + 0x3ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0xba0),
		.end	= evt2irq(0xba0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device usb_ehci_device = {
	.name		= "sh_ehci",
	.id		= -1,
	.dev = {
		.dma_mask		= &usb_ehci_device.dev.coherent_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(usb_ehci_resources),
	.resource	= usb_ehci_resources,
};

static struct resource usb_ohci_resources[] = {
	[0] = {
		.start	= USB_OHCI_START,
		.end	= USB_OHCI_START + 0x3ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0xba0),
		.end	= evt2irq(0xba0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct usb_ohci_pdata usb_ohci_pdata;

static struct platform_device usb_ohci_device = {
	.name		= "ohci-platform",
	.id		= -1,
	.dev = {
		.dma_mask		= &usb_ohci_device.dev.coherent_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &usb_ohci_pdata,
	},
	.num_resources	= ARRAY_SIZE(usb_ohci_resources),
	.resource	= usb_ohci_resources,
};

static struct platform_device *sh7786_early_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&scif4_device,
	&scif5_device,
	&tmu0_device,
	&tmu1_device,
	&tmu2_device,
};

static struct platform_device *sh7786_devices[] __initdata = {
	&dma0_device,
	&usb_ehci_device,
	&usb_ohci_device,
};

/*
 * Please call this function if your platform board
 * use external clock for USB
 * */
#define USBCTL0		0xffe70858
#define CLOCK_MODE_MASK 0xffffff7f
#define EXT_CLOCK_MODE  0x00000080

void __init sh7786_usb_use_exclock(void)
{
	u32 val = __raw_readl(USBCTL0) & CLOCK_MODE_MASK;
	__raw_writel(val | EXT_CLOCK_MODE, USBCTL0);
}

#define USBINITREG1	0xffe70094
#define USBINITREG2	0xffe7009c
#define USBINITVAL1	0x00ff0040
#define USBINITVAL2	0x00000001

#define USBPCTL1	0xffe70804
#define USBST		0xffe70808
#define PHY_ENB		0x00000001
#define PLL_ENB		0x00000002
#define PHY_RST		0x00000004
#define ACT_PLL_STATUS	0xc0000000

static void __init sh7786_usb_setup(void)
{
	int i = 1000000;

	/*
	 * USB initial settings
	 *
	 * The following settings are necessary
	 * for using the USB modules.
	 *
	 * see "USB Initial Settings" for detail
	 */
	__raw_writel(USBINITVAL1, USBINITREG1);
	__raw_writel(USBINITVAL2, USBINITREG2);

	/*
	 * Set the PHY and PLL enable bit
	 */
	__raw_writel(PHY_ENB | PLL_ENB, USBPCTL1);
	while (i--) {
		if (ACT_PLL_STATUS == (__raw_readl(USBST) & ACT_PLL_STATUS)) {
			/* Set the PHY RST bit */
			__raw_writel(PHY_ENB | PLL_ENB | PHY_RST, USBPCTL1);
			printk(KERN_INFO "sh7786 usb setup done\n");
			break;
		}
		cpu_relax();
	}
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
	WDT,
	TMU0_0, TMU0_1, TMU0_2, TMU0_3,
	TMU1_0, TMU1_1, TMU1_2,
	DMAC0_0, DMAC0_1, DMAC0_2, DMAC0_3, DMAC0_4, DMAC0_5, DMAC0_6,
	HUDI1, HUDI0,
	DMAC1_0, DMAC1_1, DMAC1_2, DMAC1_3,
	HPB_0, HPB_1, HPB_2,
	SCIF0_0, SCIF0_1, SCIF0_2, SCIF0_3,
	SCIF1,
	TMU2, TMU3,
	SCIF2, SCIF3, SCIF4, SCIF5,
	Eth_0, Eth_1,
	PCIeC0_0, PCIeC0_1, PCIeC0_2,
	PCIeC1_0, PCIeC1_1, PCIeC1_2,
	USB,
	I2C0, I2C1,
	DU,
	SSI0, SSI1, SSI2, SSI3,
	PCIeC2_0, PCIeC2_1, PCIeC2_2,
	HAC0, HAC1,
	FLCTL,
	HSPI,
	GPIO0, GPIO1,
	Thermal,
	INTICI0, INTICI1, INTICI2, INTICI3,
	INTICI4, INTICI5, INTICI6, INTICI7,

	/* Muxed sub-events */
	TXI1, BRI1, RXI1, ERI1,
};

static struct intc_vect sh7786_vectors[] __initdata = {
	INTC_VECT(WDT, 0x3e0),
	INTC_VECT(TMU0_0, 0x400), INTC_VECT(TMU0_1, 0x420),
	INTC_VECT(TMU0_2, 0x440), INTC_VECT(TMU0_3, 0x460),
	INTC_VECT(TMU1_0, 0x480), INTC_VECT(TMU1_1, 0x4a0),
	INTC_VECT(TMU1_2, 0x4c0),
	INTC_VECT(DMAC0_0, 0x500), INTC_VECT(DMAC0_1, 0x520),
	INTC_VECT(DMAC0_2, 0x540), INTC_VECT(DMAC0_3, 0x560),
	INTC_VECT(DMAC0_4, 0x580), INTC_VECT(DMAC0_5, 0x5a0),
	INTC_VECT(DMAC0_6, 0x5c0),
	INTC_VECT(HUDI1, 0x5e0), INTC_VECT(HUDI0, 0x600),
	INTC_VECT(DMAC1_0, 0x620), INTC_VECT(DMAC1_1, 0x640),
	INTC_VECT(DMAC1_2, 0x660), INTC_VECT(DMAC1_3, 0x680),
	INTC_VECT(HPB_0, 0x6a0), INTC_VECT(HPB_1, 0x6c0),
	INTC_VECT(HPB_2, 0x6e0),
	INTC_VECT(SCIF0_0, 0x700), INTC_VECT(SCIF0_1, 0x720),
	INTC_VECT(SCIF0_2, 0x740), INTC_VECT(SCIF0_3, 0x760),
	INTC_VECT(SCIF1, 0x780),
	INTC_VECT(TMU2, 0x7a0), INTC_VECT(TMU3, 0x7c0),
	INTC_VECT(SCIF2, 0x840), INTC_VECT(SCIF3, 0x860),
	INTC_VECT(SCIF4, 0x880), INTC_VECT(SCIF5, 0x8a0),
	INTC_VECT(Eth_0, 0x8c0), INTC_VECT(Eth_1, 0x8e0),
	INTC_VECT(PCIeC0_0, 0xae0), INTC_VECT(PCIeC0_1, 0xb00),
	INTC_VECT(PCIeC0_2, 0xb20),
	INTC_VECT(PCIeC1_0, 0xb40), INTC_VECT(PCIeC1_1, 0xb60),
	INTC_VECT(PCIeC1_2, 0xb80),
	INTC_VECT(USB, 0xba0),
	INTC_VECT(I2C0, 0xcc0), INTC_VECT(I2C1, 0xce0),
	INTC_VECT(DU, 0xd00),
	INTC_VECT(SSI0, 0xd20), INTC_VECT(SSI1, 0xd40),
	INTC_VECT(SSI2, 0xd60), INTC_VECT(SSI3, 0xd80),
	INTC_VECT(PCIeC2_0, 0xda0), INTC_VECT(PCIeC2_1, 0xdc0),
	INTC_VECT(PCIeC2_2, 0xde0),
	INTC_VECT(HAC0, 0xe00), INTC_VECT(HAC1, 0xe20),
	INTC_VECT(FLCTL, 0xe40),
	INTC_VECT(HSPI, 0xe80),
	INTC_VECT(GPIO0, 0xea0), INTC_VECT(GPIO1, 0xec0),
	INTC_VECT(Thermal, 0xee0),
	INTC_VECT(INTICI0, 0xf00), INTC_VECT(INTICI1, 0xf20),
	INTC_VECT(INTICI2, 0xf40), INTC_VECT(INTICI3, 0xf60),
	INTC_VECT(INTICI4, 0xf80), INTC_VECT(INTICI5, 0xfa0),
	INTC_VECT(INTICI6, 0xfc0), INTC_VECT(INTICI7, 0xfe0),
};

#define CnINTMSK0	0xfe410030
#define CnINTMSK1	0xfe410040
#define CnINTMSKCLR0	0xfe410050
#define CnINTMSKCLR1	0xfe410060
#define CnINT2MSKR0	0xfe410a20
#define CnINT2MSKR1	0xfe410a24
#define CnINT2MSKR2	0xfe410a28
#define CnINT2MSKR3	0xfe410a2c
#define CnINT2MSKCR0	0xfe410a30
#define CnINT2MSKCR1	0xfe410a34
#define CnINT2MSKCR2	0xfe410a38
#define CnINT2MSKCR3	0xfe410a3c
#define INTMSK2		0xfe410068
#define INTMSKCLR2	0xfe41006c

#define INTDISTCR0	0xfe4100b0
#define INTDISTCR1	0xfe4100b4
#define INT2DISTCR0	0xfe410900
#define INT2DISTCR1	0xfe410904
#define INT2DISTCR2	0xfe410908
#define INT2DISTCR3	0xfe41090c

static struct intc_mask_reg sh7786_mask_registers[] __initdata = {
	{ CnINTMSK0, CnINTMSKCLR0, 32,
	  { IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7 },
	    INTC_SMP_BALANCING(INTDISTCR0) },
	{ INTMSK2, INTMSKCLR2, 32,
	  { IRL0_LLLL, IRL0_LLLH, IRL0_LLHL, IRL0_LLHH,
	    IRL0_LHLL, IRL0_LHLH, IRL0_LHHL, IRL0_LHHH,
	    IRL0_HLLL, IRL0_HLLH, IRL0_HLHL, IRL0_HLHH,
	    IRL0_HHLL, IRL0_HHLH, IRL0_HHHL, 0,
	    IRL4_LLLL, IRL4_LLLH, IRL4_LLHL, IRL4_LLHH,
	    IRL4_LHLL, IRL4_LHLH, IRL4_LHHL, IRL4_LHHH,
	    IRL4_HLLL, IRL4_HLLH, IRL4_HLHL, IRL4_HLHH,
	    IRL4_HHLL, IRL4_HHLH, IRL4_HHHL, 0, } },
	{ CnINT2MSKR0, CnINT2MSKCR0 , 32,
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, WDT },
	    INTC_SMP_BALANCING(INT2DISTCR0) },
	{ CnINT2MSKR1, CnINT2MSKCR1, 32,
	  { TMU0_0, TMU0_1, TMU0_2, TMU0_3, TMU1_0, TMU1_1, TMU1_2, 0,
	    DMAC0_0, DMAC0_1, DMAC0_2, DMAC0_3, DMAC0_4, DMAC0_5, DMAC0_6,
	    HUDI1, HUDI0,
	    DMAC1_0, DMAC1_1, DMAC1_2, DMAC1_3,
	    HPB_0, HPB_1, HPB_2,
	    SCIF0_0, SCIF0_1, SCIF0_2, SCIF0_3,
	    SCIF1,
	    TMU2, TMU3, 0, }, INTC_SMP_BALANCING(INT2DISTCR1) },
	{ CnINT2MSKR2, CnINT2MSKCR2, 32,
	  { 0, 0, SCIF2, SCIF3, SCIF4, SCIF5,
	    Eth_0, Eth_1,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	    PCIeC0_0, PCIeC0_1, PCIeC0_2,
	    PCIeC1_0, PCIeC1_1, PCIeC1_2,
	    USB, 0, 0 }, INTC_SMP_BALANCING(INT2DISTCR2) },
	{ CnINT2MSKR3, CnINT2MSKCR3, 32,
	  { 0, 0, 0, 0, 0, 0,
	    I2C0, I2C1,
	    DU, SSI0, SSI1, SSI2, SSI3,
	    PCIeC2_0, PCIeC2_1, PCIeC2_2,
	    HAC0, HAC1,
	    FLCTL, 0,
	    HSPI, GPIO0, GPIO1, Thermal,
	    0, 0, 0, 0, 0, 0, 0, 0 }, INTC_SMP_BALANCING(INT2DISTCR3) },
};

static struct intc_prio_reg sh7786_prio_registers[] __initdata = {
	{ 0xfe410010, 0, 32, 4, /* INTPRI */   { IRQ0, IRQ1, IRQ2, IRQ3,
						 IRQ4, IRQ5, IRQ6, IRQ7 } },
	{ 0xfe410800, 0, 32, 8, /* INT2PRI0 */ { 0, 0, 0, WDT } },
	{ 0xfe410804, 0, 32, 8, /* INT2PRI1 */ { TMU0_0, TMU0_1,
						 TMU0_2, TMU0_3 } },
	{ 0xfe410808, 0, 32, 8, /* INT2PRI2 */ { TMU1_0, TMU1_1,
						 TMU1_2, 0 } },
	{ 0xfe41080c, 0, 32, 8, /* INT2PRI3 */ { DMAC0_0, DMAC0_1,
						 DMAC0_2, DMAC0_3 } },
	{ 0xfe410810, 0, 32, 8, /* INT2PRI4 */ { DMAC0_4, DMAC0_5,
						 DMAC0_6, HUDI1 } },
	{ 0xfe410814, 0, 32, 8, /* INT2PRI5 */ { HUDI0, DMAC1_0,
						 DMAC1_1, DMAC1_2 } },
	{ 0xfe410818, 0, 32, 8, /* INT2PRI6 */ { DMAC1_3, HPB_0,
						 HPB_1, HPB_2 } },
	{ 0xfe41081c, 0, 32, 8, /* INT2PRI7 */ { SCIF0_0, SCIF0_1,
						 SCIF0_2, SCIF0_3 } },
	{ 0xfe410820, 0, 32, 8, /* INT2PRI8 */ { SCIF1, TMU2, TMU3, 0 } },
	{ 0xfe410824, 0, 32, 8, /* INT2PRI9 */ { 0, 0, SCIF2, SCIF3 } },
	{ 0xfe410828, 0, 32, 8, /* INT2PRI10 */ { SCIF4, SCIF5,
						  Eth_0, Eth_1 } },
	{ 0xfe41082c, 0, 32, 8, /* INT2PRI11 */ { 0, 0, 0, 0 } },
	{ 0xfe410830, 0, 32, 8, /* INT2PRI12 */ { 0, 0, 0, 0 } },
	{ 0xfe410834, 0, 32, 8, /* INT2PRI13 */ { 0, 0, 0, 0 } },
	{ 0xfe410838, 0, 32, 8, /* INT2PRI14 */ { 0, 0, 0, PCIeC0_0 } },
	{ 0xfe41083c, 0, 32, 8, /* INT2PRI15 */ { PCIeC0_1, PCIeC0_2,
						  PCIeC1_0, PCIeC1_1 } },
	{ 0xfe410840, 0, 32, 8, /* INT2PRI16 */ { PCIeC1_2, USB, 0, 0 } },
	{ 0xfe410844, 0, 32, 8, /* INT2PRI17 */ { 0, 0, 0, 0 } },
	{ 0xfe410848, 0, 32, 8, /* INT2PRI18 */ { 0, 0, I2C0, I2C1 } },
	{ 0xfe41084c, 0, 32, 8, /* INT2PRI19 */ { DU, SSI0, SSI1, SSI2 } },
	{ 0xfe410850, 0, 32, 8, /* INT2PRI20 */ { SSI3, PCIeC2_0,
						  PCIeC2_1, PCIeC2_2 } },
	{ 0xfe410854, 0, 32, 8, /* INT2PRI21 */ { HAC0, HAC1, FLCTL, 0 } },
	{ 0xfe410858, 0, 32, 8, /* INT2PRI22 */ { HSPI, GPIO0,
						  GPIO1, Thermal } },
	{ 0xfe41085c, 0, 32, 8, /* INT2PRI23 */ { 0, 0, 0, 0 } },
	{ 0xfe410860, 0, 32, 8, /* INT2PRI24 */ { 0, 0, 0, 0 } },
	{ 0xfe410090, 0xfe4100a0, 32, 4, /* CnICIPRI / CnICIPRICLR */
	  { INTICI7, INTICI6, INTICI5, INTICI4,
	    INTICI3, INTICI2, INTICI1, INTICI0 }, INTC_SMP(4, 2) },
};

static struct intc_subgroup sh7786_subgroups[] __initdata = {
	{ 0xfe410c20, 32, SCIF1,
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, TXI1, BRI1, RXI1, ERI1 } },
};

static struct intc_desc sh7786_intc_desc __initdata = {
	.name		= "sh7786",
	.hw		= {
		.vectors	= sh7786_vectors,
		.nr_vectors	= ARRAY_SIZE(sh7786_vectors),
		.mask_regs	= sh7786_mask_registers,
		.nr_mask_regs	= ARRAY_SIZE(sh7786_mask_registers),
		.subgroups	= sh7786_subgroups,
		.nr_subgroups	= ARRAY_SIZE(sh7786_subgroups),
		.prio_regs	= sh7786_prio_registers,
		.nr_prio_regs	= ARRAY_SIZE(sh7786_prio_registers),
	},
};

/* Support for external interrupt pins in IRQ mode */
static struct intc_vect vectors_irq0123[] __initdata = {
	INTC_VECT(IRQ0, 0x200), INTC_VECT(IRQ1, 0x240),
	INTC_VECT(IRQ2, 0x280), INTC_VECT(IRQ3, 0x2c0),
};

static struct intc_vect vectors_irq4567[] __initdata = {
	INTC_VECT(IRQ4, 0x300), INTC_VECT(IRQ5, 0x340),
	INTC_VECT(IRQ6, 0x380), INTC_VECT(IRQ7, 0x3c0),
};

static struct intc_sense_reg sh7786_sense_registers[] __initdata = {
	{ 0xfe41001c, 32, 2, /* ICR1 */   { IRQ0, IRQ1, IRQ2, IRQ3,
					    IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static struct intc_mask_reg sh7786_ack_registers[] __initdata = {
	{ 0xfe410024, 0, 32, /* INTREQ */
	  { IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static DECLARE_INTC_DESC_ACK(intc_desc_irq0123, "sh7786-irq0123",
			     vectors_irq0123, NULL, sh7786_mask_registers,
			     sh7786_prio_registers, sh7786_sense_registers,
			     sh7786_ack_registers);

static DECLARE_INTC_DESC_ACK(intc_desc_irq4567, "sh7786-irq4567",
			     vectors_irq4567, NULL, sh7786_mask_registers,
			     sh7786_prio_registers, sh7786_sense_registers,
			     sh7786_ack_registers);

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
	INTC_VECT(IRL4_LLLL, 0x900), INTC_VECT(IRL4_LLLH, 0x920),
	INTC_VECT(IRL4_LLHL, 0x940), INTC_VECT(IRL4_LLHH, 0x960),
	INTC_VECT(IRL4_LHLL, 0x980), INTC_VECT(IRL4_LHLH, 0x9a0),
	INTC_VECT(IRL4_LHHL, 0x9c0), INTC_VECT(IRL4_LHHH, 0x9e0),
	INTC_VECT(IRL4_HLLL, 0xa00), INTC_VECT(IRL4_HLLH, 0xa20),
	INTC_VECT(IRL4_HLHL, 0xa40), INTC_VECT(IRL4_HLHH, 0xa60),
	INTC_VECT(IRL4_HHLL, 0xa80), INTC_VECT(IRL4_HHLH, 0xaa0),
	INTC_VECT(IRL4_HHHL, 0xac0),
};

static DECLARE_INTC_DESC(intc_desc_irl0123, "sh7786-irl0123", vectors_irl0123,
			 NULL, sh7786_mask_registers, NULL, NULL);

static DECLARE_INTC_DESC(intc_desc_irl4567, "sh7786-irl4567", vectors_irl4567,
			 NULL, sh7786_mask_registers, NULL, NULL);

#define INTC_ICR0	0xfe410000
#define INTC_INTMSK0	CnINTMSK0
#define INTC_INTMSK1	CnINTMSK1
#define INTC_INTMSK2	INTMSK2
#define INTC_INTMSKCLR1	CnINTMSKCLR1
#define INTC_INTMSKCLR2	INTMSKCLR2

void __init plat_irq_setup(void)
{
	/* disable IRQ3-0 + IRQ7-4 */
	__raw_writel(0xff000000, INTC_INTMSK0);

	/* disable IRL3-0 + IRL7-4 */
	__raw_writel(0xc0000000, INTC_INTMSK1);
	__raw_writel(0xfffefffe, INTC_INTMSK2);

	/* select IRL mode for IRL3-0 + IRL7-4 */
	__raw_writel(__raw_readl(INTC_ICR0) & ~0x00c00000, INTC_ICR0);

	register_intc_controller(&sh7786_intc_desc);
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
}

static int __init sh7786_devices_setup(void)
{
	int ret, irq;

	sh7786_usb_setup();

	/*
	 * De-mux SCIF1 IRQs if possible
	 */
	irq = intc_irq_lookup(sh7786_intc_desc.name, TXI1);
	if (irq > 0) {
		scif1_demux_resources[1].start =
			intc_irq_lookup(sh7786_intc_desc.name, ERI1);
		scif1_demux_resources[2].start =
			intc_irq_lookup(sh7786_intc_desc.name, RXI1);
		scif1_demux_resources[3].start = irq;
		scif1_demux_resources[4].start =
			intc_irq_lookup(sh7786_intc_desc.name, BRI1);

		scif1_device.resource = scif1_demux_resources;
		scif1_device.num_resources = ARRAY_SIZE(scif1_demux_resources);
	}

	ret = platform_add_devices(sh7786_early_devices,
				   ARRAY_SIZE(sh7786_early_devices));
	if (unlikely(ret != 0))
		return ret;

	return platform_add_devices(sh7786_devices,
				    ARRAY_SIZE(sh7786_devices));
}
arch_initcall(sh7786_devices_setup);

void __init plat_early_device_setup(void)
{
	early_platform_add_devices(sh7786_early_devices,
				   ARRAY_SIZE(sh7786_early_devices));
}
