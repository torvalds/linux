/*
 * linux/arch/sh/boards/systemh/setup.c
 *
 * Copyright (C) 2000  Kazumoto Kojima
 * Copyright (C) 2003  Paul Mundt
 *
 * Hitachi SystemH Support.
 *
 * Modified for 7751 SystemH by Jonathan Short.
 *
 * Rewritten for 2.6 by Paul Mundt.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <asm/mach/7751systemh.h>
#include <asm/mach/io.h>
#include <asm/machvec.h>

extern void make_systemh_irq(unsigned int irq);

const char *get_system_type(void)
{
	return "7751 SystemH";
}

/*
 * Initialize IRQ setting
 */
void __init init_7751systemh_IRQ(void)
{
/*  	make_ipr_irq(10, BCR_ILCRD, 1, 0x0f-10); LAN */
/*  	make_ipr_irq(14, BCR_ILCRA, 2, 0x0f-4); */
	make_systemh_irq(0xb);	/* Ethernet interrupt */
}

struct sh_machine_vector mv_7751systemh __initmv = {
	.mv_nr_irqs		= 72,

	.mv_inb			= sh7751systemh_inb,
	.mv_inw			= sh7751systemh_inw,
	.mv_inl			= sh7751systemh_inl,
	.mv_outb		= sh7751systemh_outb,
	.mv_outw		= sh7751systemh_outw,
	.mv_outl		= sh7751systemh_outl,

	.mv_inb_p		= sh7751systemh_inb_p,
	.mv_inw_p		= sh7751systemh_inw,
	.mv_inl_p		= sh7751systemh_inl,
	.mv_outb_p		= sh7751systemh_outb_p,
	.mv_outw_p		= sh7751systemh_outw,
	.mv_outl_p		= sh7751systemh_outl,

	.mv_insb		= sh7751systemh_insb,
	.mv_insw		= sh7751systemh_insw,
	.mv_insl		= sh7751systemh_insl,
	.mv_outsb		= sh7751systemh_outsb,
	.mv_outsw		= sh7751systemh_outsw,
	.mv_outsl		= sh7751systemh_outsl,

	.mv_readb		= sh7751systemh_readb,
	.mv_readw		= sh7751systemh_readw,
	.mv_readl		= sh7751systemh_readl,
	.mv_writeb		= sh7751systemh_writeb,
	.mv_writew		= sh7751systemh_writew,
	.mv_writel		= sh7751systemh_writel,

	.mv_isa_port2addr	= sh7751systemh_isa_port2addr,

	.mv_init_irq		= init_7751systemh_IRQ,
};
ALIAS_MV(7751systemh)

int __init platform_setup(void)
{
	return 0;
}

