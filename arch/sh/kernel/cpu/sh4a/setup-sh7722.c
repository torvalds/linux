/*
 * SH7722 Setup
 *
 *  Copyright (C) 2006 - 2007  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/serial_sci.h>
#include <linux/mm.h>
#include <asm/mmzone.h>

static struct resource usbf_resources[] = {
	[0] = {
		.name	= "m66592_udc",
		.start	= 0xA4480000,
		.end	= 0xA44800FF,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "m66592_udc",
		.start	= 65,
		.end	= 65,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device usbf_device = {
	.name		= "m66592_udc",
	.id		= -1,
	.dev = {
		.dma_mask		= NULL,
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(usbf_resources),
	.resource	= usbf_resources,
};

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= 0xffe00000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 80, 80, 80, 80 },
	},
	{
		.mapbase	= 0xffe10000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 81, 81, 81, 81 },
	},
	{
		.mapbase	= 0xffe20000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 82, 82, 82, 82 },
	},
	{
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

static struct platform_device *sh7722_devices[] __initdata = {
	&usbf_device,
	&sci_device,
};

static int __init sh7722_devices_setup(void)
{
	return platform_add_devices(sh7722_devices,
				    ARRAY_SIZE(sh7722_devices));
}
__initcall(sh7722_devices_setup);

enum {
	UNUSED=0,

	/* interrupt sources */
	IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7,
	HUDI,
	SIM_ERI, SIM_RXI, SIM_TXI, SIM_TEI,
	RTC_ATI, RTC_PRI, RTC_CUI,
	DMAC0, DMAC1, DMAC2, DMAC3,
	VIO_CEUI, VIO_BEUI, VIO_VEUI, VOU,
	VPU, TPU,
	USB_USBI0, USB_USBI1,
	DMAC4, DMAC5, DMAC_DADERR,
	KEYSC,
	SCIF0, SCIF1, SCIF2, SIOF0, SIOF1, SIO,
	FLCTL_FLSTEI, FLCTL_FLENDI, FLCTL_FLTREQ0I, FLCTL_FLTREQ1I,
	I2C_ALI, I2C_TACKI, I2C_WAITI, I2C_DTEI,
	SDHI0, SDHI1, SDHI2, SDHI3,
	CMT, TSIF, SIU, TWODG,
	TMU0, TMU1, TMU2,
	IRDA, JPU, LCDC,

	/* interrupt groups */

	SIM, RTC, DMAC0123, VIOVOU, USB, DMAC45, FLCTL, I2C, SDHI,
};

static struct intc_vect vectors[] __initdata = {
	INTC_VECT(IRQ0, 0x600), INTC_VECT(IRQ1, 0x620),
	INTC_VECT(IRQ2, 0x640), INTC_VECT(IRQ3, 0x660),
	INTC_VECT(IRQ4, 0x680), INTC_VECT(IRQ5, 0x6a0),
	INTC_VECT(IRQ6, 0x6c0), INTC_VECT(IRQ7, 0x6e0),
	INTC_VECT(SIM_ERI, 0x700), INTC_VECT(SIM_RXI, 0x720),
	INTC_VECT(SIM_TXI, 0x740), INTC_VECT(SIM_TEI, 0x760),
	INTC_VECT(RTC_ATI, 0x780), INTC_VECT(RTC_PRI, 0x7a0),
	INTC_VECT(RTC_CUI, 0x7c0),
	INTC_VECT(DMAC0, 0x800), INTC_VECT(DMAC1, 0x820),
	INTC_VECT(DMAC2, 0x840), INTC_VECT(DMAC3, 0x860),
	INTC_VECT(VIO_CEUI, 0x880), INTC_VECT(VIO_BEUI, 0x8a0),
	INTC_VECT(VIO_VEUI, 0x8c0), INTC_VECT(VOU, 0x8e0),
	INTC_VECT(VPU, 0x980), INTC_VECT(TPU, 0x9a0),
	INTC_VECT(USB_USBI0, 0xa20), INTC_VECT(USB_USBI1, 0xa40),
	INTC_VECT(DMAC4, 0xb80), INTC_VECT(DMAC5, 0xba0),
	INTC_VECT(DMAC_DADERR, 0xbc0), INTC_VECT(KEYSC, 0xbe0),
	INTC_VECT(SCIF0, 0xc00), INTC_VECT(SCIF1, 0xc20),
	INTC_VECT(SCIF2, 0xc40), INTC_VECT(SIOF0, 0xc80),
	INTC_VECT(SIOF1, 0xca0), INTC_VECT(SIO, 0xd00),
	INTC_VECT(FLCTL_FLSTEI, 0xd80), INTC_VECT(FLCTL_FLENDI, 0xda0),
	INTC_VECT(FLCTL_FLTREQ0I, 0xdc0), INTC_VECT(FLCTL_FLTREQ1I, 0xde0),
	INTC_VECT(I2C_ALI, 0xe00), INTC_VECT(I2C_TACKI, 0xe20),
	INTC_VECT(I2C_WAITI, 0xe40), INTC_VECT(I2C_DTEI, 0xe60),
	INTC_VECT(SDHI0, 0xe80), INTC_VECT(SDHI1, 0xea0),
	INTC_VECT(SDHI2, 0xec0), INTC_VECT(SDHI3, 0xee0),
	INTC_VECT(CMT, 0xf00), INTC_VECT(TSIF, 0xf20),
	INTC_VECT(SIU, 0xf80), INTC_VECT(TWODG, 0xfa0),
	INTC_VECT(TMU0, 0x400), INTC_VECT(TMU1, 0x420),
	INTC_VECT(TMU2, 0x440), INTC_VECT(IRDA, 0x480),
	INTC_VECT(JPU, 0x560), INTC_VECT(LCDC, 0x580),
};

static struct intc_group groups[] __initdata = {
	INTC_GROUP(SIM, SIM_ERI, SIM_RXI, SIM_TXI, SIM_TEI),
	INTC_GROUP(RTC, RTC_ATI, RTC_PRI, RTC_CUI),
	INTC_GROUP(DMAC0123, DMAC0, DMAC1, DMAC2, DMAC3),
	INTC_GROUP(VIOVOU, VIO_CEUI, VIO_BEUI, VIO_VEUI, VOU),
	INTC_GROUP(USB, USB_USBI0, USB_USBI1),
	INTC_GROUP(DMAC45, DMAC4, DMAC5, DMAC_DADERR),
	INTC_GROUP(FLCTL, FLCTL_FLSTEI, FLCTL_FLENDI,
		   FLCTL_FLTREQ0I, FLCTL_FLTREQ1I),
	INTC_GROUP(I2C, I2C_ALI, I2C_TACKI, I2C_WAITI, I2C_DTEI),
	INTC_GROUP(SDHI, SDHI0, SDHI1, SDHI2, SDHI3),
};

static struct intc_mask_reg mask_registers[] __initdata = {
	{ 0xa4080080, 0xa40800c0, 8, /* IMR0 / IMCR0 */
	  { } },
	{ 0xa4080084, 0xa40800c4, 8, /* IMR1 / IMCR1 */
	  { VOU, VIO_VEUI, VIO_BEUI, VIO_CEUI, DMAC3, DMAC2, DMAC1, DMAC0 } },
	{ 0xa4080088, 0xa40800c8, 8, /* IMR2 / IMCR2 */
	  { 0, 0, 0, VPU, } },
	{ 0xa408008c, 0xa40800cc, 8, /* IMR3 / IMCR3 */
	  { SIM_TEI, SIM_TXI, SIM_RXI, SIM_ERI, 0, 0, 0, IRDA } },
	{ 0xa4080090, 0xa40800d0, 8, /* IMR4 / IMCR4 */
	  { 0, TMU2, TMU1, TMU0, JPU, 0, 0, LCDC } },
	{ 0xa4080094, 0xa40800d4, 8, /* IMR5 / IMCR5 */
	  { KEYSC, DMAC_DADERR, DMAC5, DMAC4, 0, SCIF2, SCIF1, SCIF0 } },
	{ 0xa4080098, 0xa40800d8, 8, /* IMR6 / IMCR6 */
	  { 0, 0, 0, SIO, 0, 0, SIOF1, SIOF0 } },
	{ 0xa408009c, 0xa40800dc, 8, /* IMR7 / IMCR7 */
	  { I2C_DTEI, I2C_WAITI, I2C_TACKI, I2C_ALI,
	    FLCTL_FLTREQ1I, FLCTL_FLTREQ0I, FLCTL_FLENDI, FLCTL_FLSTEI } },
	{ 0xa40800a0, 0xa40800e0, 8, /* IMR8 / IMCR8 */
	  { SDHI3, SDHI2, SDHI1, SDHI0, 0, 0, TWODG, SIU } },
	{ 0xa40800a4, 0xa40800e4, 8, /* IMR9 / IMCR9 */
	  { 0, 0, 0, CMT, 0, USB_USBI1, USB_USBI0, } },
	{ 0xa40800a8, 0xa40800e8, 8, /* IMR10 / IMCR10 */
	  { } },
	{ 0xa40800ac, 0xa40800ec, 8, /* IMR11 / IMCR11 */
	  { 0, RTC_CUI, RTC_PRI, RTC_ATI, 0, TPU, 0, TSIF } },
	{ 0xa4140044, 0xa4140064, 8, /* INTMSK00 / INTMSKCLR00 */
	  { IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xa4080000, 0, 16, 4, /* IPRA */ { TMU0, TMU1, TMU2, IRDA } },
	{ 0xa4080004, 0, 16, 4, /* IPRB */ { JPU, LCDC, SIM } },
	{ 0xa4080008, 0, 16, 4, /* IPRC */ { } },
	{ 0xa408000c, 0, 16, 4, /* IPRD */ { } },
	{ 0xa4080010, 0, 16, 4, /* IPRE */ { DMAC0123, VIOVOU, 0, VPU } },
	{ 0xa4080014, 0, 16, 4, /* IPRF */ { KEYSC, DMAC45, USB, CMT } },
	{ 0xa4080018, 0, 16, 4, /* IPRG */ { SCIF0, SCIF1, SCIF2 } },
	{ 0xa408001c, 0, 16, 4, /* IPRH */ { SIOF0, SIOF1, FLCTL, I2C } },
	{ 0xa4080020, 0, 16, 4, /* IPRI */ { SIO, 0, TSIF, RTC } },
	{ 0xa4080024, 0, 16, 4, /* IPRJ */ { 0, 0, SIU } },
	{ 0xa4080028, 0, 16, 4, /* IPRK */ { 0, 0, 0, SDHI } },
	{ 0xa408002c, 0, 16, 4, /* IPRL */ { TWODG, 0, TPU } },
	{ 0xa4140010, 0, 32, 4, /* INTPRI00 */
	  { IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static struct intc_sense_reg sense_registers[] __initdata = {
	{ 0xa414001c, 16, 2, /* ICR1 */
	  { IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static DECLARE_INTC_DESC(intc_desc, "sh7722", vectors, groups,
			 mask_registers, prio_registers, sense_registers);

void __init plat_irq_setup(void)
{
	register_intc_controller(&intc_desc);
}

void __init plat_mem_setup(void)
{
	/* Register the URAM space as Node 1 */
	setup_bootmem_node(1, 0x055f0000, 0x05610000);
}
