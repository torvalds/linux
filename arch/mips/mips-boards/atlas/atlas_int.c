/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 * Routines for generic manipulation of the interrupts found on the MIPS
 * Atlas board.
 *
 */
#include <linux/compiler.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/mips-boards/atlas.h>
#include <asm/mips-boards/atlasint.h>
#include <asm/gdb-stub.h>


static struct atlas_ictrl_regs *atlas_hw0_icregs;

#if 0
#define DEBUG_INT(x...) printk(x)
#else
#define DEBUG_INT(x...)
#endif

void disable_atlas_irq(unsigned int irq_nr)
{
	atlas_hw0_icregs->intrsten = (1 << (irq_nr-ATLASINT_BASE));
	iob();
}

void enable_atlas_irq(unsigned int irq_nr)
{
	atlas_hw0_icregs->intseten = (1 << (irq_nr-ATLASINT_BASE));
	iob();
}

static unsigned int startup_atlas_irq(unsigned int irq)
{
	enable_atlas_irq(irq);
	return 0; /* never anything pending */
}

#define shutdown_atlas_irq	disable_atlas_irq

#define mask_and_ack_atlas_irq disable_atlas_irq

static void end_atlas_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_atlas_irq(irq);
}

static struct hw_interrupt_type atlas_irq_type = {
	.typename = "Atlas",
	.startup = startup_atlas_irq,
	.shutdown = shutdown_atlas_irq,
	.enable = enable_atlas_irq,
	.disable = disable_atlas_irq,
	.ack = mask_and_ack_atlas_irq,
	.end = end_atlas_irq,
};

static inline int ls1bit32(unsigned int x)
{
	int b = 31, s;

	s = 16; if (x << 16 == 0) s = 0; b -= s; x <<= s;
	s =  8; if (x <<  8 == 0) s = 0; b -= s; x <<= s;
	s =  4; if (x <<  4 == 0) s = 0; b -= s; x <<= s;
	s =  2; if (x <<  2 == 0) s = 0; b -= s; x <<= s;
	s =  1; if (x <<  1 == 0) s = 0; b -= s;

	return b;
}

static inline void atlas_hw0_irqdispatch(struct pt_regs *regs)
{
	unsigned long int_status;
	int irq;

	int_status = atlas_hw0_icregs->intstatus;

	/* if int_status == 0, then the interrupt has already been cleared */
	if (unlikely(int_status == 0))
		return;

	irq = ATLASINT_BASE + ls1bit32(int_status);

	DEBUG_INT("atlas_hw0_irqdispatch: irq=%d\n", irq);

	do_IRQ(irq, regs);
}

static inline int clz(unsigned long x)
{
	__asm__ (
	"	.set	push					\n"
	"	.set	mips32					\n"
	"	clz	%0, %1					\n"
	"	.set	pop					\n"
	: "=r" (x)
	: "r" (x));

	return x;
}

/*
 * Version of ffs that only looks at bits 12..15.
 */
static inline unsigned int irq_ffs(unsigned int pending)
{
#if defined(CONFIG_CPU_MIPS32) || defined(CONFIG_CPU_MIPS64)
	return -clz(pending) + 31 - CAUSEB_IP;
#else
	unsigned int a0 = 7;
	unsigned int t0;

	t0 = s0 & 0xf000;
	t0 = t0 < 1;
	t0 = t0 << 2;
	a0 = a0 - t0;
	s0 = s0 << t0;

	t0 = s0 & 0xc000;
	t0 = t0 < 1;
	t0 = t0 << 1;
	a0 = a0 - t0;
	s0 = s0 << t0;

	t0 = s0 & 0x8000;
	t0 = t0 < 1;
	//t0 = t0 << 2;
	a0 = a0 - t0;
	//s0 = s0 << t0;

	return a0;
#endif
}

/*
 * IRQs on the Atlas board look basically (barring software IRQs which we
 * don't use at all and all external interrupt sources are combined together
 * on hardware interrupt 0 (MIPS IRQ 2)) like:
 *
 *	MIPS IRQ	Source
 *      --------        ------
 *             0	Software (ignored)
 *             1        Software (ignored)
 *             2        Combined hardware interrupt (hw0)
 *             3        Hardware (ignored)
 *             4        Hardware (ignored)
 *             5        Hardware (ignored)
 *             6        Hardware (ignored)
 *             7        R4k timer (what we use)
 *
 * We handle the IRQ according to _our_ priority which is:
 *
 * Highest ----     R4k Timer
 * Lowest  ----     Combined hardware interrupt
 *
 * then we just return, if multiple IRQs are pending then we will just take
 * another exception, big deal.
 */
asmlinkage void plat_irq_dispatch(struct pt_regs *regs)
{
	unsigned int pending = read_c0_cause() & read_c0_status() & ST0_IM;
	int irq;

	irq = irq_ffs(pending);

	if (irq == MIPSCPU_INT_ATLAS)
		atlas_hw0_irqdispatch(regs);
	else if (irq > 0)
		do_IRQ(MIPSCPU_INT_BASE + irq, regs);
	else
		spurious_interrupt(regs);
}

void __init arch_init_irq(void)
{
	int i;

	atlas_hw0_icregs = (struct atlas_ictrl_regs *)ioremap (ATLAS_ICTRL_REGS_BASE, sizeof(struct atlas_ictrl_regs *));

	/*
	 * Mask out all interrupt by writing "1" to all bit position in
	 * the interrupt reset reg.
	 */
	atlas_hw0_icregs->intrsten = 0xffffffff;

	for (i = ATLASINT_BASE; i <= ATLASINT_END; i++) {
		irq_desc[i].status	= IRQ_DISABLED;
		irq_desc[i].action	= 0;
		irq_desc[i].depth	= 1;
		irq_desc[i].handler	= &atlas_irq_type;
		spin_lock_init(&irq_desc[i].lock);
	}
}
