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
#define INTC_IPRC	0xa4000016UL

static struct ipr_data ipr_irq_table[] = {
	{ 32, 0, 0, 0 },
	{ 33, 0, 4, 0 },
	{ 34, 0, 8, 8 },
	{ 35, 0, 12, 0 },
};

static unsigned long ipr_offsets[] = {
	INTC_IPRC,
};

static struct ipr_desc ipr_irq_desc = {
	.ipr_offsets	= ipr_offsets,
	.nr_offsets	= ARRAY_SIZE(ipr_offsets),

	.ipr_data	= ipr_irq_table,
	.nr_irqs	= ARRAY_SIZE(ipr_irq_table),

	.chip = {
		.name	= "IPR-shmin",
	},
};

static void __init init_shmin_irq(void)
{
	ctrl_outw(0x2a00, PFC_PHCR);	// IRQ0-3=IRQ
	ctrl_outw(0x0aaa, INTC_ICR1);	// IRQ0-3=IRQ-mode,Low-active.
	register_ipr_controller(&ipr_irq_desc);
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
