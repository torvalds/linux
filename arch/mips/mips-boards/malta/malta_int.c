/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000, 2001, 2004 MIPS Technologies, Inc.
 * Copyright (C) 2001 Ralf Baechle
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
 * Routines for generic manipulation of the interrupts found on the MIPS
 * Malta board.
 * The interrupt controller is located in the South Bridge a PIIX4 device
 * with two internal 82C95 interrupt controllers.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/kernel.h>
#include <linux/random.h>

#include <asm/i8259.h>
#include <asm/irq_cpu.h>
#include <asm/io.h>
#include <asm/irq_regs.h>
#include <asm/mips-boards/malta.h>
#include <asm/mips-boards/maltaint.h>
#include <asm/mips-boards/piix4.h>
#include <asm/gt64120.h>
#include <asm/mips-boards/generic.h>
#include <asm/mips-boards/msc01_pci.h>
#include <asm/msc01_ic.h>

static DEFINE_SPINLOCK(mips_irq_lock);

static inline int mips_pcibios_iack(void)
{
	int irq;
        u32 dummy;

	/*
	 * Determine highest priority pending interrupt by performing
	 * a PCI Interrupt Acknowledge cycle.
	 */
	switch (mips_revision_sconid) {
	case MIPS_REVISION_SCON_SOCIT:
	case MIPS_REVISION_SCON_ROCIT:
	case MIPS_REVISION_SCON_SOCITSC:
	case MIPS_REVISION_SCON_SOCITSCP:
	        MSC_READ(MSC01_PCI_IACK, irq);
		irq &= 0xff;
		break;
	case MIPS_REVISION_SCON_GT64120:
		irq = GT_READ(GT_PCI0_IACK_OFS);
		irq &= 0xff;
		break;
	case MIPS_REVISION_SCON_BONITO:
		/* The following will generate a PCI IACK cycle on the
		 * Bonito controller. It's a little bit kludgy, but it
		 * was the easiest way to implement it in hardware at
		 * the given time.
		 */
		BONITO_PCIMAP_CFG = 0x20000;

		/* Flush Bonito register block */
		dummy = BONITO_PCIMAP_CFG;
		iob();    /* sync */

		irq = readl((u32 *)_pcictrl_bonito_pcicfg);
		iob();    /* sync */
		irq &= 0xff;
		BONITO_PCIMAP_CFG = 0;
		break;
	default:
	        printk("Unknown system controller.\n");
		return -1;
	}
	return irq;
}

static inline int get_int(void)
{
	unsigned long flags;
	int irq;
	spin_lock_irqsave(&mips_irq_lock, flags);

	irq = mips_pcibios_iack();

	/*
	 * The only way we can decide if an interrupt is spurious
	 * is by checking the 8259 registers.  This needs a spinlock
	 * on an SMP system,  so leave it up to the generic code...
	 */

	spin_unlock_irqrestore(&mips_irq_lock, flags);

	return irq;
}

static void malta_hw0_irqdispatch(void)
{
	int irq;

	irq = get_int();
	if (irq < 0) {
		return;  /* interrupt has already been cleared */
	}

	do_IRQ(MALTA_INT_BASE + irq);
}

static void corehi_irqdispatch(void)
{
	unsigned int intedge, intsteer, pcicmd, pcibadaddr;
        unsigned int pcimstat, intisr, inten, intpol;
	unsigned int intrcause,datalo,datahi;
	struct pt_regs *regs = get_irq_regs();

        printk("CoreHI interrupt, shouldn't happen, so we die here!!!\n");
        printk("epc   : %08lx\nStatus: %08lx\n"
	       "Cause : %08lx\nbadVaddr : %08lx\n",
	       regs->cp0_epc, regs->cp0_status,
	       regs->cp0_cause, regs->cp0_badvaddr);

	/* Read all the registers and then print them as there is a
	   problem with interspersed printk's upsetting the Bonito controller.
	   Do it for the others too.
	*/

	switch (mips_revision_sconid) {
        case MIPS_REVISION_SCON_SOCIT:
	case MIPS_REVISION_SCON_ROCIT:
	case MIPS_REVISION_SCON_SOCITSC:
	case MIPS_REVISION_SCON_SOCITSCP:
                ll_msc_irq();
                break;
        case MIPS_REVISION_SCON_GT64120:
                intrcause = GT_READ(GT_INTRCAUSE_OFS);
                datalo = GT_READ(GT_CPUERR_ADDRLO_OFS);
                datahi = GT_READ(GT_CPUERR_ADDRHI_OFS);
                printk("GT_INTRCAUSE = %08x\n", intrcause);
                printk("GT_CPUERR_ADDR = %02x%08x\n", datahi, datalo);
                break;
        case MIPS_REVISION_SCON_BONITO:
                pcibadaddr = BONITO_PCIBADADDR;
                pcimstat = BONITO_PCIMSTAT;
                intisr = BONITO_INTISR;
                inten = BONITO_INTEN;
                intpol = BONITO_INTPOL;
                intedge = BONITO_INTEDGE;
                intsteer = BONITO_INTSTEER;
                pcicmd = BONITO_PCICMD;
                printk("BONITO_INTISR = %08x\n", intisr);
                printk("BONITO_INTEN = %08x\n", inten);
                printk("BONITO_INTPOL = %08x\n", intpol);
                printk("BONITO_INTEDGE = %08x\n", intedge);
                printk("BONITO_INTSTEER = %08x\n", intsteer);
                printk("BONITO_PCICMD = %08x\n", pcicmd);
                printk("BONITO_PCIBADADDR = %08x\n", pcibadaddr);
                printk("BONITO_PCIMSTAT = %08x\n", pcimstat);
                break;
        }

        /* We die here*/
        die("CoreHi interrupt", regs);
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

	t0 = pending & 0xf000;
	t0 = t0 < 1;
	t0 = t0 << 2;
	a0 = a0 - t0;
	pending = pending << t0;

	t0 = pending & 0xc000;
	t0 = t0 < 1;
	t0 = t0 << 1;
	a0 = a0 - t0;
	pending = pending << t0;

	t0 = pending & 0x8000;
	t0 = t0 < 1;
	//t0 = t0 << 2;
	a0 = a0 - t0;
	//pending = pending << t0;

	return a0;
#endif
}

/*
 * IRQs on the Malta board look basically (barring software IRQs which we
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

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_cause() & read_c0_status() & ST0_IM;
	int irq;

	irq = irq_ffs(pending);

	if (irq == MIPSCPU_INT_I8259A)
		malta_hw0_irqdispatch();
	else if (irq >= 0)
		do_IRQ(MIPS_CPU_IRQ_BASE + irq);
	else
		spurious_interrupt();
}

static struct irqaction i8259irq = {
	.handler = no_action,
	.name = "XT-PIC cascade"
};

static struct irqaction corehi_irqaction = {
	.handler = no_action,
	.name = "CoreHi"
};

msc_irqmap_t __initdata msc_irqmap[] = {
	{MSC01C_INT_TMR,		MSC01_IRQ_EDGE, 0},
	{MSC01C_INT_PCI,		MSC01_IRQ_LEVEL, 0},
};
int __initdata msc_nr_irqs = ARRAY_SIZE(msc_irqmap);

msc_irqmap_t __initdata msc_eicirqmap[] = {
	{MSC01E_INT_SW0,		MSC01_IRQ_LEVEL, 0},
	{MSC01E_INT_SW1,		MSC01_IRQ_LEVEL, 0},
	{MSC01E_INT_I8259A,		MSC01_IRQ_LEVEL, 0},
	{MSC01E_INT_SMI,		MSC01_IRQ_LEVEL, 0},
	{MSC01E_INT_COREHI,		MSC01_IRQ_LEVEL, 0},
	{MSC01E_INT_CORELO,		MSC01_IRQ_LEVEL, 0},
	{MSC01E_INT_TMR,		MSC01_IRQ_EDGE, 0},
	{MSC01E_INT_PCI,		MSC01_IRQ_LEVEL, 0},
	{MSC01E_INT_PERFCTR,		MSC01_IRQ_LEVEL, 0},
	{MSC01E_INT_CPUCTR,		MSC01_IRQ_LEVEL, 0}
};
int __initdata msc_nr_eicirqs = ARRAY_SIZE(msc_eicirqmap);

void __init arch_init_irq(void)
{
	init_i8259_irqs();

	if (!cpu_has_veic)
		mips_cpu_irq_init();

        switch(mips_revision_sconid) {
        case MIPS_REVISION_SCON_SOCIT:
        case MIPS_REVISION_SCON_ROCIT:
		if (cpu_has_veic)
			init_msc_irqs (MIPS_MSC01_IC_REG_BASE, MSC01E_INT_BASE, msc_eicirqmap, msc_nr_eicirqs);
		else
			init_msc_irqs (MIPS_MSC01_IC_REG_BASE, MSC01C_INT_BASE, msc_irqmap, msc_nr_irqs);
		break;

        case MIPS_REVISION_SCON_SOCITSC:
        case MIPS_REVISION_SCON_SOCITSCP:
		if (cpu_has_veic)
			init_msc_irqs (MIPS_SOCITSC_IC_REG_BASE, MSC01E_INT_BASE, msc_eicirqmap, msc_nr_eicirqs);
		else
			init_msc_irqs (MIPS_SOCITSC_IC_REG_BASE, MSC01C_INT_BASE, msc_irqmap, msc_nr_irqs);
	}

	if (cpu_has_veic) {
		set_vi_handler (MSC01E_INT_I8259A, malta_hw0_irqdispatch);
		set_vi_handler (MSC01E_INT_COREHI, corehi_irqdispatch);
		setup_irq (MSC01E_INT_BASE+MSC01E_INT_I8259A, &i8259irq);
		setup_irq (MSC01E_INT_BASE+MSC01E_INT_COREHI, &corehi_irqaction);
	}
	else if (cpu_has_vint) {
		set_vi_handler (MIPSCPU_INT_I8259A, malta_hw0_irqdispatch);
		set_vi_handler (MIPSCPU_INT_COREHI, corehi_irqdispatch);
#ifdef CONFIG_MIPS_MT_SMTC
		setup_irq_smtc (MIPS_CPU_IRQ_BASE+MIPSCPU_INT_I8259A, &i8259irq,
			(0x100 << MIPSCPU_INT_I8259A));
		setup_irq_smtc (MIPS_CPU_IRQ_BASE+MIPSCPU_INT_COREHI,
			&corehi_irqaction, (0x100 << MIPSCPU_INT_COREHI));
		/*
		 * Temporary hack to ensure that the subsidiary device
		 * interrupts coing in via the i8259A, but associated
		 * with low IRQ numbers, will restore the Status.IM
		 * value associated with the i8259A.
		 */
		{
			int i;

			for (i = 0; i < 16; i++)
				irq_hwmask[i] = (0x100 << MIPSCPU_INT_I8259A);
		}
#else /* Not SMTC */
		setup_irq (MIPS_CPU_IRQ_BASE+MIPSCPU_INT_I8259A, &i8259irq);
		setup_irq (MIPS_CPU_IRQ_BASE+MIPSCPU_INT_COREHI, &corehi_irqaction);
#endif /* CONFIG_MIPS_MT_SMTC */
	}
	else {
		setup_irq (MIPS_CPU_IRQ_BASE+MIPSCPU_INT_I8259A, &i8259irq);
		setup_irq (MIPS_CPU_IRQ_BASE+MIPSCPU_INT_COREHI, &corehi_irqaction);
	}
}
