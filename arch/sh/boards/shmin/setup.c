/*
 * arch/sh/boards/shmin/setup.c
 *
 * Copyright (C) 2006 Takashi YOSHII
 *
 * SHMIN Support.
 */
#include <linux/init.h>
#include <asm/machvec.h>
#include <asm/shmin.h>
#include <asm/clock.h>
#include <asm/irq.h>
#include <asm/io.h>

#define PFC_PHCR	0xa400010eUL
#define INTC_ICR1	0xa4000010UL
#define INTC_IPRC	0xa4000016UL

static struct ipr_data shmin_ipr_map[] = {
	{ .irq=32, .addr=INTC_IPRC, .shift= 0, .priority=0 },
	{ .irq=33, .addr=INTC_IPRC, .shift= 4, .priority=0 },
	{ .irq=34, .addr=INTC_IPRC, .shift= 8, .priority=8 },
	{ .irq=35, .addr=INTC_IPRC, .shift=12, .priority=0 },
};

static void __init init_shmin_irq(void)
{
	ctrl_outw(0x2a00, PFC_PHCR);	// IRQ0-3=IRQ
	ctrl_outw(0x0aaa, INTC_ICR1);	// IRQ0-3=IRQ-mode,Low-active.
	make_ipr_irq(shmin_ipr_map, ARRAY_SIZE(shmin_ipr_map));
}

static void __iomem *shmin_ioport_map(unsigned long port, unsigned int size)
{
	static int dummy;

	if ((port & ~0x1f) == SHMIN_NE_BASE)
		return (void __iomem *)(SHMIN_IO_BASE + port);

	dummy = 0;

	return &dummy;

}

struct sh_machine_vector mv_shmin __initmv = {
	.mv_name	= "SHMIN",
	.mv_init_irq	= init_shmin_irq,
	.mv_ioport_map	= shmin_ioport_map,
};
ALIAS_MV(shmin)
