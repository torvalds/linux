/*
 * Copyright (C) 1999, 2000, 2006  MIPS Technologies, Inc.
 *	All rights reserved.
 *	Authors: Carsten Langgaard <carstenl@mips.com>
 *		 Maciej W. Rozycki <macro@mips.com>
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
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/kernel.h>

#include <asm/gdb-stub.h>
#include <asm/io.h>
#include <asm/irq_cpu.h>
#include <asm/msc01_ic.h>

#include <asm/mips-boards/atlas.h>
#include <asm/mips-boards/atlasint.h>
#include <asm/mips-boards/generic.h>

static struct atlas_ictrl_regs *atlas_hw0_icregs;

#if 0
#define DEBUG_INT(x...) printk(x)
#else
#define DEBUG_INT(x...)
#endif

void disable_atlas_irq(unsigned int irq_nr)
{
	atlas_hw0_icregs->intrsten = 1 << (irq_nr - ATLAS_INT_BASE);
	iob();
}

void enable_atlas_irq(unsigned int irq_nr)
{
	atlas_hw0_icregs->intseten = 1 << (irq_nr - ATLAS_INT_BASE);
	iob();
}

static void end_atlas_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_atlas_irq(irq);
}

static struct irq_chip atlas_irq_type = {
	.name = "Atlas",
	.ack = disable_atlas_irq,
	.mask = disable_atlas_irq,
	.mask_ack = disable_atlas_irq,
	.unmask = enable_atlas_irq,
	.eoi = enable_atlas_irq,
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

static inline void atlas_hw0_irqdispatch(void)
{
	unsigned long int_status;
	int irq;

	int_status = atlas_hw0_icregs->intstatus;

	/* if int_status == 0, then the interrupt has already been cleared */
	if (unlikely(int_status == 0))
		return;

	irq = ATLAS_INT_BASE + ls1bit32(int_status);

	DEBUG_INT("atlas_hw0_irqdispatch: irq=%d\n", irq);

	do_IRQ(irq);
}

static inline int clz(unsigned long x)
{
	__asm__(
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
 * IRQs on the Atlas board look basically like (all external interrupt
 * sources are combined together on hardware interrupt 0 (MIPS IRQ 2)):
 *
 *      MIPS IRQ        Source
 *      --------        ------
 *             0        Software 0 (reschedule IPI on MT)
 *             1        Software 1 (remote call IPI on MT)
 *             2        Combined Atlas hardware interrupt (hw0)
 *             3        Hardware (ignored)
 *             4        Hardware (ignored)
 *             5        Hardware (ignored)
 *             6        Hardware (ignored)
 *             7        R4k timer (what we use)
 *
 * We handle the IRQ according to _our_ priority which is:
 *
 * Highest ----     R4k Timer
 * Lowest  ----     Software 0
 *
 * then we just return, if multiple IRQs are pending then we will just take
 * another exception, big deal.
 */
asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_cause() & read_c0_status() & ST0_IM;
	int irq;

	irq = irq_ffs(pending);

	if (irq == MIPSCPU_INT_ATLAS)
		atlas_hw0_irqdispatch();
	else if (irq >= 0)
		do_IRQ(MIPS_CPU_IRQ_BASE + irq);
	else
		spurious_interrupt();
}

static inline void init_atlas_irqs(int base)
{
	int i;

	atlas_hw0_icregs = (struct atlas_ictrl_regs *)
			   ioremap(ATLAS_ICTRL_REGS_BASE,
				   sizeof(struct atlas_ictrl_regs *));

	/*
	 * Mask out all interrupt by writing "1" to all bit position in
	 * the interrupt reset reg.
	 */
	atlas_hw0_icregs->intrsten = 0xffffffff;

	for (i = ATLAS_INT_BASE; i <= ATLAS_INT_END; i++)
		set_irq_chip_and_handler(i, &atlas_irq_type, handle_level_irq);
}

static struct irqaction atlasirq = {
	.handler = no_action,
	.name = "Atlas cascade"
};

msc_irqmap_t __initdata msc_irqmap[] = {
	{MSC01C_INT_TMR,		MSC01_IRQ_EDGE, 0},
	{MSC01C_INT_PCI,		MSC01_IRQ_LEVEL, 0},
};
int __initdata msc_nr_irqs = ARRAY_SIZE(msc_irqmap);

msc_irqmap_t __initdata msc_eicirqmap[] = {
	{MSC01E_INT_SW0,		MSC01_IRQ_LEVEL, 0},
	{MSC01E_INT_SW1,		MSC01_IRQ_LEVEL, 0},
	{MSC01E_INT_ATLAS,		MSC01_IRQ_LEVEL, 0},
	{MSC01E_INT_TMR,		MSC01_IRQ_EDGE, 0},
	{MSC01E_INT_PCI,		MSC01_IRQ_LEVEL, 0},
	{MSC01E_INT_PERFCTR,		MSC01_IRQ_LEVEL, 0},
	{MSC01E_INT_CPUCTR,		MSC01_IRQ_LEVEL, 0}
};
int __initdata msc_nr_eicirqs = ARRAY_SIZE(msc_eicirqmap);

void __init arch_init_irq(void)
{
	init_atlas_irqs(ATLAS_INT_BASE);

	if (!cpu_has_veic)
		mips_cpu_irq_init();

	switch(mips_revision_corid) {
	case MIPS_REVISION_CORID_CORE_MSC:
	case MIPS_REVISION_CORID_CORE_FPGA2:
	case MIPS_REVISION_CORID_CORE_FPGA3:
	case MIPS_REVISION_CORID_CORE_FPGA4:
	case MIPS_REVISION_CORID_CORE_24K:
	case MIPS_REVISION_CORID_CORE_EMUL_MSC:
		if (cpu_has_veic)
			init_msc_irqs(MSC01E_INT_BASE, MSC01E_INT_BASE,
				      msc_eicirqmap, msc_nr_eicirqs);
		else
			init_msc_irqs(MSC01E_INT_BASE, MSC01C_INT_BASE,
				      msc_irqmap, msc_nr_irqs);
	}

	if (cpu_has_veic) {
		set_vi_handler(MSC01E_INT_ATLAS, atlas_hw0_irqdispatch);
		setup_irq(MSC01E_INT_BASE + MSC01E_INT_ATLAS, &atlasirq);
	} else if (cpu_has_vint) {
		set_vi_handler(MIPSCPU_INT_ATLAS, atlas_hw0_irqdispatch);
#ifdef CONFIG_MIPS_MT_SMTC
		setup_irq_smtc(MIPS_CPU_IRQ_BASE + MIPSCPU_INT_ATLAS,
			       &atlasirq, (0x100 << MIPSCPU_INT_ATLAS));
#else /* Not SMTC */
		setup_irq(MIPS_CPU_IRQ_BASE + MIPSCPU_INT_ATLAS, &atlasirq);
#endif /* CONFIG_MIPS_MT_SMTC */
	} else
		setup_irq(MIPS_CPU_IRQ_BASE + MIPSCPU_INT_ATLAS, &atlasirq);
}
