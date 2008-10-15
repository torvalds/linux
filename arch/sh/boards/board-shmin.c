/*
 * arch/sh/boards/shmin/setup.c
 *
 * Copyright (C) 2006 Takashi YOSHII
 *
 * SHMIN Support.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/machvec.h>
#include <asm/shmin.h>
#include <asm/clock.h>
#include <asm/io.h>

#define PFC_PHCR	0xa400010eUL
#define INTC_ICR1	0xa4000010UL

static void __init init_shmin_irq(void)
{
	ctrl_outw(0x2a00, PFC_PHCR);	// IRQ0-3=IRQ
	ctrl_outw(0x0aaa, INTC_ICR1);	// IRQ0-3=IRQ-mode,Low-active.
	plat_irq_setup_pins(IRQ_MODE_IRQ);
}

static void __iomem *shmin_ioport_map(unsigned long port, unsigned int size)
{
	static int dummy;

	if ((port & ~0x1f) == SHMIN_NE_BASE)
		return (void __iomem *)(SHMIN_IO_BASE + port);

	dummy = 0;

	return &dummy;

}

static struct sh_machine_vector mv_shmin __initmv = {
	.mv_name	= "SHMIN",
	.mv_init_irq	= init_shmin_irq,
	.mv_ioport_map	= shmin_ioport_map,
};
