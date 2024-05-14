// SPDX-License-Identifier: GPL-2.0
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
#include <mach/shmin.h>
#include <asm/clock.h>
#include <asm/io.h>

#define PFC_PHCR	0xa400010eUL
#define INTC_ICR1	0xa4000010UL

static void __init init_shmin_irq(void)
{
	__raw_writew(0x2a00, PFC_PHCR);	// IRQ0-3=IRQ
	__raw_writew(0x0aaa, INTC_ICR1);	// IRQ0-3=IRQ-mode,Low-active.
	plat_irq_setup_pins(IRQ_MODE_IRQ);
}

static void __init shmin_setup(char **cmdline_p)
{
	__set_io_port_base(SHMIN_IO_BASE);
}

static struct sh_machine_vector mv_shmin __initmv = {
	.mv_name	= "SHMIN",
	.mv_setup	= shmin_setup,
	.mv_init_irq	= init_shmin_irq,
};
