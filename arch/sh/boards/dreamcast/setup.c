/*
 * arch/sh/boards/dreamcast/setup.c
 *
 * Hardware support for the Sega Dreamcast.
 *
 * Copyright (c) 2001, 2002 M. R. Brown <mrbrown@linuxdc.org>
 * Copyright (c) 2002, 2003, 2004 Paul Mundt <lethal@linux-sh.org>
 *
 * This file is part of the LinuxDC project (www.linuxdc.org)
 *
 * Released under the terms of the GNU GPL v2.0.
 *
 * This file originally bore the message (with enclosed-$):
 *	Id: setup_dc.c,v 1.5 2001/05/24 05:09:16 mrbrown Exp
 *	SEGA Dreamcast support
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/device.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/rtc.h>
#include <asm/machvec.h>
#include <asm/mach/sysasic.h>

extern struct hw_interrupt_type systemasic_int;
extern void aica_time_init(void);
extern int gapspci_init(void);
extern int systemasic_irq_demux(int);

void *dreamcast_consistent_alloc(struct device *, size_t, dma_addr_t *, gfp_t);
int dreamcast_consistent_free(struct device *, size_t, void *, dma_addr_t);

static void __init dreamcast_setup(char **cmdline_p)
{
	int i;

	/* Mask all hardware events */
	/* XXX */

	/* Acknowledge any previous events */
	/* XXX */

	__set_io_port_base(0xa0000000);

	/* Assign all virtual IRQs to the System ASIC int. handler */
	for (i = HW_EVENT_IRQ_BASE; i < HW_EVENT_IRQ_MAX; i++)
		irq_desc[i].chip = &systemasic_int;

	board_time_init = aica_time_init;

#ifdef CONFIG_PCI
	if (gapspci_init() < 0)
		printk(KERN_WARNING "GAPSPCI was not detected.\n");
#endif
}

static struct sh_machine_vector mv_dreamcast __initmv = {
	.mv_name		= "Sega Dreamcast",
	.mv_setup		= dreamcast_setup,
	.mv_irq_demux		= systemasic_irq_demux,

#ifdef CONFIG_PCI
	.mv_consistent_alloc	= dreamcast_consistent_alloc,
	.mv_consistent_free	= dreamcast_consistent_free,
#endif
};
