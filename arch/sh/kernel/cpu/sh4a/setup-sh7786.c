/*
 * SH7786 Setup
 *
 * Copyright (C) 2009  Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
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
#include <asm/mmzone.h>

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= 0xffea0000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 40, 41, 43, 42 },
	},
	/*
	 * The rest of these all have multiplexed IRQs
	 */
	{
		.mapbase	= 0xffeb0000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 44, 44, 44, 44 },
	}, {
		.mapbase	= 0xffec0000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 50, 50, 50, 50 },
	}, {
		.mapbase	= 0xffed0000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 51, 51, 51, 51 },
	}, {
		.mapbase	= 0xffee0000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 52, 52, 52, 52 },
	}, {
		.mapbase	= 0xffef0000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 53, 53, 53, 53 },
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

static struct resource usb_ohci_resources[] = {
	[0] = {
		.start	= 0xffe70400,
		.end	= 0xffe704ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 77,
		.end	= 77,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 usb_ohci_dma_mask = DMA_BIT_MASK(32);
static struct platform_device usb_ohci_device = {
	.name		= "sh_ohci",
	.id		= -1,
	.dev = {
		.dma_mask		= &usb_ohci_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(usb_ohci_resources),
	.resource	= usb_ohci_resources,
};

static struct platform_device *sh7786_devices[] __initdata = {
	&sci_device,
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
	 * see "USB Inital Settings" for detail
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

static int __init sh7786_devices_setup(void)
{
	sh7786_usb_setup();
	return platform_add_devices(sh7786_devices,
				    ARRAY_SIZE(sh7786_devices));
}
device_initcall(sh7786_devices_setup);

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
	INTC0, INTC1, INTC2, INTC3, INTC4, INTC5, INTC6, INTC7,

	/* interrupt groups */
};

static struct intc_vect vectors[] __initdata = {
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
};

/* FIXME: Main CPU support only now */
#if 1 /* Main CPU */
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
#else /* Sub CPU */
#define CnINTMSK0	0xfe410034
#define CnINTMSK1	0xfe410044
#define CnINTMSKCLR0	0xfe410054
#define CnINTMSKCLR1	0xfe410064
#define CnINT2MSKR0	0xfe410b20
#define CnINT2MSKR1	0xfe410b24
#define CnINT2MSKR2	0xfe410b28
#define CnINT2MSKR3	0xfe410b2c
#define CnINT2MSKCR0	0xfe410b30
#define CnINT2MSKCR1	0xfe410b34
#define CnINT2MSKCR2	0xfe410b38
#define CnINT2MSKCR3	0xfe410b3c
#endif

#define INTMSK2		0xfe410068
#define INTMSKCLR2	0xfe41006c

static struct intc_mask_reg mask_registers[] __initdata = {
	{ CnINTMSK0, CnINTMSKCLR0, 32,
	  { IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7 } },
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
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, WDT } },
	{ CnINT2MSKR1, CnINT2MSKCR1, 32,
	  { TMU0_0, TMU0_1, TMU0_2, TMU0_3, TMU1_0, TMU1_1, TMU1_2, 0,
	    DMAC0_0, DMAC0_1, DMAC0_2, DMAC0_3, DMAC0_4, DMAC0_5, DMAC0_6,
	    HUDI1, HUDI0,
	    DMAC1_0, DMAC1_1, DMAC1_2, DMAC1_3,
	    HPB_0, HPB_1, HPB_2,
	    SCIF0_0, SCIF0_1, SCIF0_2, SCIF0_3,
	    SCIF1,
	    TMU2, TMU3, 0, } },
	{ CnINT2MSKR2, CnINT2MSKCR2, 32,
	  { 0, 0, SCIF2, SCIF3, SCIF4, SCIF5,
	    Eth_0, Eth_1,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	    PCIeC0_0, PCIeC0_1, PCIeC0_2,
	    PCIeC1_0, PCIeC1_1, PCIeC1_2,
	    USB, 0, 0 } },
	{ CnINT2MSKR3, CnINT2MSKCR3, 32,
	  { 0, 0, 0, 0, 0, 0,
	    I2C0, I2C1,
	    DU, SSI0, SSI1, SSI2, SSI3,
	    PCIeC2_0, PCIeC2_1, PCIeC2_2,
	    HAC0, HAC1,
	    FLCTL, 0,
	    HSPI, GPIO0, GPIO1, Thermal,
	    0, 0, 0, 0, 0, 0, 0, 0 } },
};

static struct intc_prio_reg prio_registers[] __initdata = {
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
};

static DECLARE_INTC_DESC(intc_desc, "sh7786", vectors, NULL,
			 mask_registers, prio_registers, NULL);

/* Support for external interrupt pins in IRQ mode */

static struct intc_vect vectors_irq0123[] __initdata = {
	INTC_VECT(IRQ0, 0x200), INTC_VECT(IRQ1, 0x240),
	INTC_VECT(IRQ2, 0x280), INTC_VECT(IRQ3, 0x2c0),
};

static struct intc_vect vectors_irq4567[] __initdata = {
	INTC_VECT(IRQ4, 0x300), INTC_VECT(IRQ5, 0x340),
	INTC_VECT(IRQ6, 0x380), INTC_VECT(IRQ7, 0x3c0),
};

static struct intc_sense_reg sense_registers[] __initdata = {
	{ 0xfe41001c, 32, 2, /* ICR1 */   { IRQ0, IRQ1, IRQ2, IRQ3,
					    IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static struct intc_mask_reg ack_registers[] __initdata = {
	{ 0xfe410024, 0, 32, /* INTREQ */
	  { IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static DECLARE_INTC_DESC_ACK(intc_desc_irq0123, "sh7786-irq0123",
			     vectors_irq0123, NULL, mask_registers,
			     prio_registers, sense_registers, ack_registers);

static DECLARE_INTC_DESC_ACK(intc_desc_irq4567, "sh7786-irq4567",
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
			 NULL, mask_registers, NULL, NULL);

static DECLARE_INTC_DESC(intc_desc_irl4567, "sh7786-irl4567", vectors_irl4567,
			 NULL, mask_registers, NULL, NULL);

#define INTC_ICR0	0xfe410000
#define INTC_INTMSK0	CnINTMSK0
#define INTC_INTMSK1	CnINTMSK1
#define INTC_INTMSK2	INTMSK2
#define INTC_INTMSKCLR1	CnINTMSKCLR1
#define INTC_INTMSKCLR2	INTMSKCLR2

void __init plat_irq_setup(void)
{
	/* disable IRQ3-0 + IRQ7-4 */
	ctrl_outl(0xff000000, INTC_INTMSK0);

	/* disable IRL3-0 + IRL7-4 */
	ctrl_outl(0xc0000000, INTC_INTMSK1);
	ctrl_outl(0xfffefffe, INTC_INTMSK2);

	/* select IRL mode for IRL3-0 + IRL7-4 */
	ctrl_outl(ctrl_inl(INTC_ICR0) & ~0x00c00000, INTC_ICR0);

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
}
