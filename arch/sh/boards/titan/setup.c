/*
 * arch/sh/boards/titan/setup.c - Setup for Titan
 *
 *  Copyright (C) 2006  Jamie Lenehan
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/titan.h>
#include <asm/io.h>

static struct ipr_data ipr_irq_table[] = {
	/* IRQ, IPR idx, shift, prio */
	{ TITAN_IRQ_WAN,   3, 12, 8 },	/* eth0 (WAN) */
	{ TITAN_IRQ_LAN,   3,  8, 8 },	/* eth1 (LAN) */
	{ TITAN_IRQ_MPCIA, 3,  4, 8 },	/* mPCI A (top) */
	{ TITAN_IRQ_USB,   3,  0, 8 },	/* mPCI B (bottom), USB */
};

static unsigned long ipr_offsets[] = { /* stolen from setup-sh7750.c */
	0xffd00004UL,	/* 0: IPRA */
	0xffd00008UL,	/* 1: IPRB */
	0xffd0000cUL,	/* 2: IPRC */
	0xffd00010UL,	/* 3: IPRD */
};

static struct ipr_desc ipr_irq_desc = {
	.ipr_offsets	= ipr_offsets,
	.nr_offsets	= ARRAY_SIZE(ipr_offsets),

	.ipr_data	= ipr_irq_table,
	.nr_irqs	= ARRAY_SIZE(ipr_irq_table),

	.chip = {
		.name	= "IPR-titan",
	},
};
static void __init init_titan_irq(void)
{
	/* enable individual interrupt mode for externals */
	ipr_irq_enable_irlm();
	/* register ipr irqs */
	register_ipr_controller(&ipr_irq_desc);
}

static struct sh_machine_vector mv_titan __initmv = {
	.mv_name =	"Titan",

	.mv_inb =	titan_inb,
	.mv_inw =	titan_inw,
	.mv_inl =	titan_inl,
	.mv_outb =	titan_outb,
	.mv_outw =	titan_outw,
	.mv_outl =	titan_outl,

	.mv_inb_p =	titan_inb_p,
	.mv_inw_p =	titan_inw,
	.mv_inl_p =	titan_inl,
	.mv_outb_p =	titan_outb_p,
	.mv_outw_p =	titan_outw,
	.mv_outl_p =	titan_outl,

	.mv_insl =	titan_insl,
	.mv_outsl =	titan_outsl,

	.mv_ioport_map = titan_ioport_map,

	.mv_init_irq =	init_titan_irq,
};
