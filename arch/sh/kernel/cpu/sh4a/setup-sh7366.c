/*
 * SH7366 Setup
 *
 *  Copyright (C) 2008 Renesas Solutions
 *
 * Based on linux/arch/sh/kernel/cpu/sh4a/setup-sh7722.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/serial_sci.h>

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= 0xffe00000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 80, 80, 80, 80 },
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

static struct platform_device *sh7366_devices[] __initdata = {
	&sci_device,
};

static int __init sh7366_devices_setup(void)
{
	return platform_add_devices(sh7366_devices,
				    ARRAY_SIZE(sh7366_devices));
}
__initcall(sh7366_devices_setup);

enum {
	UNUSED=0,

	/* interrupt sources */
	IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7,
	ICB,
	DMAC0, DMAC1, DMAC2, DMAC3,
	VIO_CEUI, VIO_BEUI, VIO_VEUI, VOU,
	MFI, VPU, USB,
	MMC_MMC1I, MMC_MMC2I, MMC_MMC3I,
	DMAC4, DMAC5, DMAC_DADERR,
	SCIF, SCIFA1, SCIFA2,
	DENC, MSIOF,
	FLCTL_FLSTEI, FLCTL_FLENDI, FLCTL_FLTREQ0I, FLCTL_FLTREQ1I,
	I2C_ALI, I2C_TACKI, I2C_WAITI, I2C_DTEI,
	SDHI0, SDHI1, SDHI2, SDHI3,
	CMT, TSIF, SIU,
	TMU0, TMU1, TMU2,
	VEU2, LCDC,

	/* interrupt groups */

	DMAC0123, VIOVOU, MMC, DMAC45, FLCTL, I2C, SDHI,
};

static struct intc_vect vectors[] __initdata = {
	INTC_VECT(IRQ0, 0x600), INTC_VECT(IRQ1, 0x620),
	INTC_VECT(IRQ2, 0x640), INTC_VECT(IRQ3, 0x660),
	INTC_VECT(IRQ4, 0x680), INTC_VECT(IRQ5, 0x6a0),
	INTC_VECT(IRQ6, 0x6c0), INTC_VECT(IRQ7, 0x6e0),
	INTC_VECT(ICB, 0x700),
	INTC_VECT(DMAC0, 0x800), INTC_VECT(DMAC1, 0x820),
	INTC_VECT(DMAC2, 0x840), INTC_VECT(DMAC3, 0x860),
	INTC_VECT(VIO_CEUI, 0x880), INTC_VECT(VIO_BEUI, 0x8a0),
	INTC_VECT(VIO_VEUI, 0x8c0), INTC_VECT(VOU, 0x8e0),
	INTC_VECT(MFI, 0x900), INTC_VECT(VPU, 0x980), INTC_VECT(USB, 0xa20),
	INTC_VECT(MMC_MMC1I, 0xb00), INTC_VECT(MMC_MMC2I, 0xb20),
	INTC_VECT(MMC_MMC3I, 0xb40),
	INTC_VECT(DMAC4, 0xb80), INTC_VECT(DMAC5, 0xba0),
	INTC_VECT(DMAC_DADERR, 0xbc0),
	INTC_VECT(SCIF, 0xc00), INTC_VECT(SCIFA1, 0xc20),
	INTC_VECT(SCIFA2, 0xc40),
	INTC_VECT(DENC, 0xc60), INTC_VECT(MSIOF, 0xc80),
	INTC_VECT(FLCTL_FLSTEI, 0xd80), INTC_VECT(FLCTL_FLENDI, 0xda0),
	INTC_VECT(FLCTL_FLTREQ0I, 0xdc0), INTC_VECT(FLCTL_FLTREQ1I, 0xde0),
	INTC_VECT(I2C_ALI, 0xe00), INTC_VECT(I2C_TACKI, 0xe20),
	INTC_VECT(I2C_WAITI, 0xe40), INTC_VECT(I2C_DTEI, 0xe60),
	INTC_VECT(SDHI0, 0xe80), INTC_VECT(SDHI1, 0xea0),
	INTC_VECT(SDHI2, 0xec0), INTC_VECT(SDHI3, 0xee0),
	INTC_VECT(CMT, 0xf00), INTC_VECT(TSIF, 0xf20),
	INTC_VECT(SIU, 0xf80),
	INTC_VECT(TMU0, 0x400), INTC_VECT(TMU1, 0x420),
	INTC_VECT(TMU2, 0x440),
	INTC_VECT(VEU2, 0x580), INTC_VECT(LCDC, 0x580),
};

static struct intc_group groups[] __initdata = {
	INTC_GROUP(DMAC0123, DMAC0, DMAC1, DMAC2, DMAC3),
	INTC_GROUP(VIOVOU, VIO_CEUI, VIO_BEUI, VIO_VEUI, VOU),
	INTC_GROUP(MMC, MMC_MMC1I, MMC_MMC2I, MMC_MMC3I),
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
	  { 0, 0, 0, VPU, 0, 0, 0, MFI } },
	{ 0xa408008c, 0xa40800cc, 8, /* IMR3 / IMCR3 */
	  { 0, 0, 0, ICB } },
	{ 0xa4080090, 0xa40800d0, 8, /* IMR4 / IMCR4 */
	  { 0, TMU2, TMU1, TMU0, VEU2, 0, 0, LCDC } },
	{ 0xa4080094, 0xa40800d4, 8, /* IMR5 / IMCR5 */
	  { 0, DMAC_DADERR, DMAC5, DMAC4, DENC, SCIFA2, SCIFA1, SCIF } },
	{ 0xa4080098, 0xa40800d8, 8, /* IMR6 / IMCR6 */
	  { 0, 0, 0, 0, 0, 0, 0, MSIOF } },
	{ 0xa408009c, 0xa40800dc, 8, /* IMR7 / IMCR7 */
	  { I2C_DTEI, I2C_WAITI, I2C_TACKI, I2C_ALI,
	    FLCTL_FLTREQ1I, FLCTL_FLTREQ0I, FLCTL_FLENDI, FLCTL_FLSTEI } },
	{ 0xa40800a0, 0xa40800e0, 8, /* IMR8 / IMCR8 */
	  { SDHI3, SDHI2, SDHI1, SDHI0, 0, 0, 0, SIU } },
	{ 0xa40800a4, 0xa40800e4, 8, /* IMR9 / IMCR9 */
	  { 0, 0, 0, CMT, 0, USB, } },
	{ 0xa40800a8, 0xa40800e8, 8, /* IMR10 / IMCR10 */
	  { 0, MMC_MMC3I, MMC_MMC2I, MMC_MMC1I } },
	{ 0xa40800ac, 0xa40800ec, 8, /* IMR11 / IMCR11 */
	  { 0, 0, 0, 0, 0, 0, 0, TSIF } },
	{ 0xa4140044, 0xa4140064, 8, /* INTMSK00 / INTMSKCLR00 */
	  { IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xa4080000, 0, 16, 4, /* IPRA */ { TMU0, TMU1, TMU2 } },
	{ 0xa4080004, 0, 16, 4, /* IPRB */ { VEU2, LCDC, ICB } },
	{ 0xa4080008, 0, 16, 4, /* IPRC */ { } },
	{ 0xa408000c, 0, 16, 4, /* IPRD */ { } },
	{ 0xa4080010, 0, 16, 4, /* IPRE */ { DMAC0123, VIOVOU, MFI, VPU } },
	{ 0xa4080014, 0, 16, 4, /* IPRF */ { 0, DMAC45, USB, CMT } },
	{ 0xa4080018, 0, 16, 4, /* IPRG */ { SCIF, SCIFA1, SCIFA2, DENC } },
	{ 0xa408001c, 0, 16, 4, /* IPRH */ { MSIOF, 0, FLCTL, I2C } },
	{ 0xa4080020, 0, 16, 4, /* IPRI */ { 0, 0, TSIF, } },
	{ 0xa4080024, 0, 16, 4, /* IPRJ */ { 0, 0, SIU } },
	{ 0xa4080028, 0, 16, 4, /* IPRK */ { 0, MMC, 0, SDHI } },
	{ 0xa408002c, 0, 16, 4, /* IPRL */ { } },
	{ 0xa4140010, 0, 32, 4, /* INTPRI00 */
	  { IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static struct intc_sense_reg sense_registers[] __initdata = {
	{ 0xa414001c, 16, 2, /* ICR1 */
	  { IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5, IRQ6, IRQ7 } },
};

static DECLARE_INTC_DESC(intc_desc, "sh7366", vectors, groups,
			 mask_registers, prio_registers, sense_registers);

void __init plat_irq_setup(void)
{
	register_intc_controller(&intc_desc);
}

void __init plat_mem_setup(void)
{
	/* TODO: Register Node 1 */
}
