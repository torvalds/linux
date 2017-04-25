/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000, 2001, 2004 MIPS Technologies, Inc.
 * Copyright (C) 2001 Ralf Baechle
 * Copyright (C) 2013 Imagination Technologies Ltd.
 *
 * Routines for generic manipulation of the interrupts found on the MIPS
 * Malta board. The interrupt controller is located in the South Bridge
 * a PIIX4 device with two internal 82C95 interrupt controllers.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irqchip/mips-gic.h>
#include <linux/of_irq.h>
#include <linux/kernel_stat.h>
#include <linux/kernel.h>
#include <linux/random.h>

#include <asm/traps.h>
#include <asm/i8259.h>
#include <asm/irq_cpu.h>
#include <asm/irq_regs.h>
#include <asm/mips-cm.h>
#include <asm/mips-boards/malta.h>
#include <asm/mips-boards/maltaint.h>
#include <asm/gt64120.h>
#include <asm/mips-boards/generic.h>
#include <asm/mips-boards/msc01_pci.h>
#include <asm/msc01_ic.h>
#include <asm/setup.h>
#include <asm/rtlx.h>

static inline int mips_pcibios_iack(void)
{
	int irq;

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
		(void) BONITO_PCIMAP_CFG;
		iob();	  /* sync */

		irq = __raw_readl((u32 *)_pcictrl_bonito_pcicfg);
		iob();	  /* sync */
		irq &= 0xff;
		BONITO_PCIMAP_CFG = 0;
		break;
	default:
		pr_emerg("Unknown system controller.\n");
		return -1;
	}
	return irq;
}

static void corehi_irqdispatch(void)
{
	unsigned int intedge, intsteer, pcicmd, pcibadaddr;
	unsigned int pcimstat, intisr, inten, intpol;
	unsigned int intrcause, datalo, datahi;
	struct pt_regs *regs = get_irq_regs();

	pr_emerg("CoreHI interrupt, shouldn't happen, we die here!\n");
	pr_emerg("epc	 : %08lx\nStatus: %08lx\n"
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
		pr_emerg("GT_INTRCAUSE = %08x\n", intrcause);
		pr_emerg("GT_CPUERR_ADDR = %02x%08x\n",
				datahi, datalo);
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
		pr_emerg("BONITO_INTISR = %08x\n", intisr);
		pr_emerg("BONITO_INTEN = %08x\n", inten);
		pr_emerg("BONITO_INTPOL = %08x\n", intpol);
		pr_emerg("BONITO_INTEDGE = %08x\n", intedge);
		pr_emerg("BONITO_INTSTEER = %08x\n", intsteer);
		pr_emerg("BONITO_PCICMD = %08x\n", pcicmd);
		pr_emerg("BONITO_PCIBADADDR = %08x\n", pcibadaddr);
		pr_emerg("BONITO_PCIMSTAT = %08x\n", pcimstat);
		break;
	}

	die("CoreHi interrupt", regs);
}

static irqreturn_t corehi_handler(int irq, void *dev_id)
{
	corehi_irqdispatch();
	return IRQ_HANDLED;
}

#ifdef CONFIG_MIPS_MT_SMP

#define MIPS_CPU_IPI_RESCHED_IRQ 0	/* SW int 0 for resched */
#define C_RESCHED C_SW0
#define MIPS_CPU_IPI_CALL_IRQ 1		/* SW int 1 for resched */
#define C_CALL C_SW1
static int cpu_ipi_resched_irq, cpu_ipi_call_irq;

static void ipi_resched_dispatch(void)
{
	do_IRQ(MIPS_CPU_IRQ_BASE + MIPS_CPU_IPI_RESCHED_IRQ);
}

static void ipi_call_dispatch(void)
{
	do_IRQ(MIPS_CPU_IRQ_BASE + MIPS_CPU_IPI_CALL_IRQ);
}

static irqreturn_t ipi_resched_interrupt(int irq, void *dev_id)
{
#ifdef CONFIG_MIPS_VPE_APSP_API_CMP
	if (aprp_hook)
		aprp_hook();
#endif

	scheduler_ipi();

	return IRQ_HANDLED;
}

static irqreturn_t ipi_call_interrupt(int irq, void *dev_id)
{
	generic_smp_call_function_interrupt();

	return IRQ_HANDLED;
}

static struct irqaction irq_resched = {
	.handler	= ipi_resched_interrupt,
	.flags		= IRQF_PERCPU,
	.name		= "IPI_resched"
};

static struct irqaction irq_call = {
	.handler	= ipi_call_interrupt,
	.flags		= IRQF_PERCPU,
	.name		= "IPI_call"
};
#endif /* CONFIG_MIPS_MT_SMP */

static struct irqaction corehi_irqaction = {
	.handler = corehi_handler,
	.name = "CoreHi",
	.flags = IRQF_NO_THREAD,
};

static msc_irqmap_t msc_irqmap[] __initdata = {
	{MSC01C_INT_TMR,		MSC01_IRQ_EDGE, 0},
	{MSC01C_INT_PCI,		MSC01_IRQ_LEVEL, 0},
};
static int msc_nr_irqs __initdata = ARRAY_SIZE(msc_irqmap);

static msc_irqmap_t msc_eicirqmap[] __initdata = {
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

static int msc_nr_eicirqs __initdata = ARRAY_SIZE(msc_eicirqmap);

void __init arch_init_ipiirq(int irq, struct irqaction *action)
{
	setup_irq(irq, action);
	irq_set_handler(irq, handle_percpu_irq);
}

void __init arch_init_irq(void)
{
	int corehi_irq;

	/*
	 * Preallocate the i8259's expected virq's here. Since irqchip_init()
	 * will probe the irqchips in hierarchial order, i8259 is probed last.
	 * If anything allocates a virq before the i8259 is probed, it will
	 * be given one of the i8259's expected range and consequently setup
	 * of the i8259 will fail.
	 */
	WARN(irq_alloc_descs(I8259A_IRQ_BASE, I8259A_IRQ_BASE,
			    16, numa_node_id()) < 0,
		"Cannot reserve i8259 virqs at IRQ%d\n", I8259A_IRQ_BASE);

	i8259_set_poll(mips_pcibios_iack);
	irqchip_init();

	switch (mips_revision_sconid) {
	case MIPS_REVISION_SCON_SOCIT:
	case MIPS_REVISION_SCON_ROCIT:
		if (cpu_has_veic)
			init_msc_irqs(MIPS_MSC01_IC_REG_BASE,
					MSC01E_INT_BASE, msc_eicirqmap,
					msc_nr_eicirqs);
		else
			init_msc_irqs(MIPS_MSC01_IC_REG_BASE,
					MSC01C_INT_BASE, msc_irqmap,
					msc_nr_irqs);
		break;

	case MIPS_REVISION_SCON_SOCITSC:
	case MIPS_REVISION_SCON_SOCITSCP:
		if (cpu_has_veic)
			init_msc_irqs(MIPS_SOCITSC_IC_REG_BASE,
					MSC01E_INT_BASE, msc_eicirqmap,
					msc_nr_eicirqs);
		else
			init_msc_irqs(MIPS_SOCITSC_IC_REG_BASE,
					MSC01C_INT_BASE, msc_irqmap,
					msc_nr_irqs);
	}

	if (gic_present) {
		corehi_irq = MIPS_CPU_IRQ_BASE + MIPSCPU_INT_COREHI;
	} else {
#if defined(CONFIG_MIPS_MT_SMP)
		/* set up ipi interrupts */
		if (cpu_has_veic) {
			set_vi_handler (MSC01E_INT_SW0, ipi_resched_dispatch);
			set_vi_handler (MSC01E_INT_SW1, ipi_call_dispatch);
			cpu_ipi_resched_irq = MSC01E_INT_SW0;
			cpu_ipi_call_irq = MSC01E_INT_SW1;
		} else {
			cpu_ipi_resched_irq = MIPS_CPU_IRQ_BASE +
				MIPS_CPU_IPI_RESCHED_IRQ;
			cpu_ipi_call_irq = MIPS_CPU_IRQ_BASE +
				MIPS_CPU_IPI_CALL_IRQ;
		}
		arch_init_ipiirq(cpu_ipi_resched_irq, &irq_resched);
		arch_init_ipiirq(cpu_ipi_call_irq, &irq_call);
#endif
		if (cpu_has_veic) {
			set_vi_handler(MSC01E_INT_COREHI,
				       corehi_irqdispatch);
			corehi_irq = MSC01E_INT_BASE + MSC01E_INT_COREHI;
		} else {
			corehi_irq = MIPS_CPU_IRQ_BASE + MIPSCPU_INT_COREHI;
		}
	}

	setup_irq(corehi_irq, &corehi_irqaction);
}
