/*
 * linux/arch/sh/boards/renesas/systemh/setup.c
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
#include <asm/machvec.h>
#include <asm/systemh7751.h>

extern void make_systemh_irq(unsigned int irq);

/*
 * Initialize IRQ setting
 */
static void __init sh7751systemh_init_irq(void)
{
	make_systemh_irq(0xb);	/* Ethernet interrupt */
}

struct sh_machine_vector mv_7751systemh __initmv = {
	.mv_name		= "7751 SystemH",
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

	.mv_init_irq		= sh7751systemh_init_irq,
};
ALIAS_MV(7751systemh)
