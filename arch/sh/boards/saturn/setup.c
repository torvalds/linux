/* 
 * arch/sh/boards/saturn/setup.c
 *
 * Hardware support for the Sega Saturn.
 *
 * Copyright (c) 2002 Paul Mundt
 *
 * Released under the terms of the GNU GPL v2.0.
 */
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/machvec.h>
#include <asm/mach/io.h>

extern int saturn_irq_demux(int irq_nr);

const char *get_system_type(void)
{
	return "Sega Saturn";
}

/*
 * The Machine Vector
 */
struct sh_machine_vector mv_saturn __initmv = {
	.mv_nr_irqs		= 80,	/* Fix this later */

	.mv_isa_port2addr	= saturn_isa_port2addr,
	.mv_irq_demux		= saturn_irq_demux,

	.mv_ioremap		= saturn_ioremap,
	.mv_iounmap		= saturn_iounmap,
};

ALIAS_MV(saturn)

int __init platform_setup(void)
{
	return 0;
}

