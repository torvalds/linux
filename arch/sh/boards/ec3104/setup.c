/*
 * linux/arch/sh/boards/ec3104/setup.c
 *  EC3104 companion chip support
 *
 * Copyright (C) 2000 Philipp Rumpf <prumpf@tux.org>
 *
 */
/* EC3104 note:
 * This code was written without any documentation about the EC3104 chip.  While
 * I hope I got most of the basic functionality right, the register names I use
 * are most likely completely different from those in the chip documentation.
 *
 * If you have any further information about the EC3104, please tell me
 * (prumpf@tux.org).
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/types.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/machvec.h>
#include <asm/mach/ec3104.h>

static void __init ec3104_setup(char **cmdline_p)
{
	char str[8];
	int i;

	for (i=0; i<8; i++)
		str[i] = ctrl_readb(EC3104_BASE + i);

	for (i = EC3104_IRQBASE; i < EC3104_IRQBASE + 32; i++)
		irq_desc[i].handler = &ec3104_int;

	printk("initializing EC3104 \"%.8s\" at %08x, IRQ %d, IRQ base %d\n",
	       str, EC3104_BASE, EC3104_IRQ, EC3104_IRQBASE);

	/* mask all interrupts.  this should have been done by the boot
	 * loader for us but we want to be sure ... */
	ctrl_writel(0xffffffff, EC3104_IMR);
}

/*
 * The Machine Vector
 */
struct sh_machine_vector mv_ec3104 __initmv = {
	.mv_name	= "EC3104",
	.mv_setup	= ec3104_setup,
	.mv_nr_irqs	= 96,

	.mv_inb		= ec3104_inb,
	.mv_inw		= ec3104_inw,
	.mv_inl		= ec3104_inl,
	.mv_outb	= ec3104_outb,
	.mv_outw	= ec3104_outw,
	.mv_outl	= ec3104_outl,

	.mv_irq_demux	= ec3104_irq_demux,
};
ALIAS_MV(ec3104)
