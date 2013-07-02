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
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel_stat.h>
#include <linux/kernel.h>
#include <linux/random.h>

#include <asm/traps.h>
#include <asm/i8259.h>
#include <asm/irq_cpu.h>
#include <asm/irq_regs.h>
#include <asm/mips-boards/malta.h>
#include <asm/mips-boards/maltaint.h>
#include <asm/mips-boards/piix4.h>
#include <asm/gt64120.h>
#include <asm/mips-boards/generic.h>
#include <asm/mips-boards/msc01_pci.h>
#include <asm/msc01_ic.h>
#include <asm/gic.h>
#include <asm/gcmpregs.h>
#include <asm/setup.h>

int gcmp_present = -1;
static unsigned long _msc01_biu_base;
static unsigned long _gcmp_base;
static unsigned int ipi_map[NR_CPUS];

static DEFINE_RAW_SPINLOCK(mips_irq_lock);

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
		printk(KERN_WARNING "Unknown system controller.\n");
		return -1;
	}
	return irq;
}

static inline int get_int(void)
{
	unsigned long flags;
	int irq;
	raw_spin_lock_irqsave(&mips_irq_lock, flags);

	irq = mips_pcibios_iack();

	/*
	 * The only way we can decide if an interrupt is spurious
	 * is by checking the 8259 registers.  This needs a spinlock
	 * on an SMP system,  so leave it up to the generic code...
	 */

	raw_spin_unlock_irqrestore(&mips_irq_lock, flags);

	return irq;
}

static void malta_hw0_irqdispatch(void)
{
	int irq;

	irq = get_int();
	if (irq < 0) {
		/* interrupt has already been cleared */
		return;
	}

	do_IRQ(MALTA_INT_BASE + irq);
}

static void malta_ipi_irqdispatch(void)
{
	int irq;

	if (gic_compare_int())
		do_IRQ(MIPS_GIC_IRQ_BASE);

	irq = gic_get_int();
	if (irq < 0)
		return;	 /* interrupt has already been cleared */

	do_IRQ(MIPS_GIC_IRQ_BASE + irq);
}

static void corehi_irqdispatch(void)
{
	unsigned int intedge, intsteer, pcicmd, pcibadaddr;
	unsigned int pcimstat, intisr, inten, intpol;
	unsigned int intrcause, datalo, datahi;
	struct pt_regs *regs = get_irq_regs();

	printk(KERN_EMERG "CoreHI interrupt, shouldn't happen, we die here!\n");
	printk(KERN_EMERG "epc	 : %08lx\nStatus: %08lx\n"
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
		printk(KERN_EMERG "GT_INTRCAUSE = %08x\n", intrcause);
		printk(KERN_EMERG "GT_CPUERR_ADDR = %02x%08x\n",
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
		printk(KERN_EMERG "BONITO_INTISR = %08x\n", intisr);
		printk(KERN_EMERG "BONITO_INTEN = %08x\n", inten);
		printk(KERN_EMERG "BONITO_INTPOL = %08x\n", intpol);
		printk(KERN_EMERG "BONITO_INTEDGE = %08x\n", intedge);
		printk(KERN_EMERG "BONITO_INTSTEER = %08x\n", intsteer);
		printk(KERN_EMERG "BONITO_PCICMD = %08x\n", pcicmd);
		printk(KERN_EMERG "BONITO_PCIBADADDR = %08x\n", pcibadaddr);
		printk(KERN_EMERG "BONITO_PCIMSTAT = %08x\n", pcimstat);
		break;
	}

	die("CoreHi interrupt", regs);
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
	/* t0 = t0 << 2; */
	a0 = a0 - t0;
	/* pending = pending << t0; */

	return a0;
#endif
}

/*
 * IRQs on the Malta board look basically (barring software IRQs which we
 * don't use at all and all external interrupt sources are combined together
 * on hardware interrupt 0 (MIPS IRQ 2)) like:
 *
 *	MIPS IRQ	Source
 *	--------	------
 *	       0	Software (ignored)
 *	       1	Software (ignored)
 *	       2	Combined hardware interrupt (hw0)
 *	       3	Hardware (ignored)
 *	       4	Hardware (ignored)
 *	       5	Hardware (ignored)
 *	       6	Hardware (ignored)
 *	       7	R4k timer (what we use)
 *
 * We handle the IRQ according to _our_ priority which is:
 *
 * Highest ----	    R4k Timer
 * Lowest  ----	    Combined hardware interrupt
 *
 * then we just return, if multiple IRQs are pending then we will just take
 * another exception, big deal.
 */

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_cause() & read_c0_status() & ST0_IM;
	int irq;

	if (unlikely(!pending)) {
		spurious_interrupt();
		return;
	}

	irq = irq_ffs(pending);

	if (irq == MIPSCPU_INT_I8259A)
		malta_hw0_irqdispatch();
	else if (gic_present && ((1 << irq) & ipi_map[smp_processor_id()]))
		malta_ipi_irqdispatch();
	else
		do_IRQ(MIPS_CPU_IRQ_BASE + irq);
}

#ifdef CONFIG_MIPS_MT_SMP


#define GIC_MIPS_CPU_IPI_RESCHED_IRQ	3
#define GIC_MIPS_CPU_IPI_CALL_IRQ	4

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
	scheduler_ipi();

	return IRQ_HANDLED;
}

static irqreturn_t ipi_call_interrupt(int irq, void *dev_id)
{
	smp_call_function_interrupt();

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

static int gic_resched_int_base;
static int gic_call_int_base;
#define GIC_RESCHED_INT(cpu) (gic_resched_int_base+(cpu))
#define GIC_CALL_INT(cpu) (gic_call_int_base+(cpu))

unsigned int plat_ipi_call_int_xlate(unsigned int cpu)
{
	return GIC_CALL_INT(cpu);
}

unsigned int plat_ipi_resched_int_xlate(unsigned int cpu)
{
	return GIC_RESCHED_INT(cpu);
}

static struct irqaction i8259irq = {
	.handler = no_action,
	.name = "XT-PIC cascade",
	.flags = IRQF_NO_THREAD,
};

static struct irqaction corehi_irqaction = {
	.handler = no_action,
	.name = "CoreHi",
	.flags = IRQF_NO_THREAD,
};

static msc_irqmap_t __initdata msc_irqmap[] = {
	{MSC01C_INT_TMR,		MSC01_IRQ_EDGE, 0},
	{MSC01C_INT_PCI,		MSC01_IRQ_LEVEL, 0},
};
static int __initdata msc_nr_irqs = ARRAY_SIZE(msc_irqmap);

static msc_irqmap_t __initdata msc_eicirqmap[] = {
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

static int __initdata msc_nr_eicirqs = ARRAY_SIZE(msc_eicirqmap);

/*
 * This GIC specific tabular array defines the association between External
 * Interrupts and CPUs/Core Interrupts. The nature of the External
 * Interrupts is also defined here - polarity/trigger.
 */

#define GIC_CPU_NMI GIC_MAP_TO_NMI_MSK
#define X GIC_UNUSED

static struct gic_intr_map gic_intr_map[GIC_NUM_INTRS] = {
	{ X, X,		   X,		X,		0 },
	{ X, X,		   X,		X,		0 },
	{ X, X,		   X,		X,		0 },
	{ 0, GIC_CPU_INT0, GIC_POL_POS, GIC_TRIG_LEVEL, GIC_FLAG_TRANSPARENT },
	{ 0, GIC_CPU_INT1, GIC_POL_POS, GIC_TRIG_LEVEL, GIC_FLAG_TRANSPARENT },
	{ 0, GIC_CPU_INT2, GIC_POL_POS, GIC_TRIG_LEVEL, GIC_FLAG_TRANSPARENT },
	{ 0, GIC_CPU_INT3, GIC_POL_POS, GIC_TRIG_LEVEL, GIC_FLAG_TRANSPARENT },
	{ 0, GIC_CPU_INT4, GIC_POL_POS, GIC_TRIG_LEVEL, GIC_FLAG_TRANSPARENT },
	{ 0, GIC_CPU_INT3, GIC_POL_POS, GIC_TRIG_LEVEL, GIC_FLAG_TRANSPARENT },
	{ 0, GIC_CPU_INT3, GIC_POL_POS, GIC_TRIG_LEVEL, GIC_FLAG_TRANSPARENT },
	{ X, X,		   X,		X,		0 },
	{ X, X,		   X,		X,		0 },
	{ 0, GIC_CPU_INT3, GIC_POL_POS, GIC_TRIG_LEVEL, GIC_FLAG_TRANSPARENT },
	{ 0, GIC_CPU_NMI,  GIC_POL_POS, GIC_TRIG_LEVEL, GIC_FLAG_TRANSPARENT },
	{ 0, GIC_CPU_NMI,  GIC_POL_POS, GIC_TRIG_LEVEL, GIC_FLAG_TRANSPARENT },
	{ X, X,		   X,		X,		0 },
	/* The remainder of this table is initialised by fill_ipi_map */
};
#undef X

/*
 * GCMP needs to be detected before any SMP initialisation
 */
int __init gcmp_probe(unsigned long addr, unsigned long size)
{
	if (mips_revision_sconid != MIPS_REVISION_SCON_ROCIT) {
		gcmp_present = 0;
		return gcmp_present;
	}

	if (gcmp_present >= 0)
		return gcmp_present;

	_gcmp_base = (unsigned long) ioremap_nocache(GCMP_BASE_ADDR, GCMP_ADDRSPACE_SZ);
	_msc01_biu_base = (unsigned long) ioremap_nocache(MSC01_BIU_REG_BASE, MSC01_BIU_ADDRSPACE_SZ);
	gcmp_present = (GCMPGCB(GCMPB) & GCMP_GCB_GCMPB_GCMPBASE_MSK) == GCMP_BASE_ADDR;

	if (gcmp_present)
		pr_debug("GCMP present\n");
	return gcmp_present;
}

/* Return the number of IOCU's present */
int __init gcmp_niocu(void)
{
  return gcmp_present ?
    (GCMPGCB(GC) & GCMP_GCB_GC_NUMIOCU_MSK) >> GCMP_GCB_GC_NUMIOCU_SHF :
    0;
}

/* Set GCMP region attributes */
void __init gcmp_setregion(int region, unsigned long base,
			   unsigned long mask, int type)
{
	GCMPGCBn(CMxBASE, region) = base;
	GCMPGCBn(CMxMASK, region) = mask | type;
}

#if defined(CONFIG_MIPS_MT_SMP)
static void __init fill_ipi_map1(int baseintr, int cpu, int cpupin)
{
	int intr = baseintr + cpu;
	gic_intr_map[intr].cpunum = cpu;
	gic_intr_map[intr].pin = cpupin;
	gic_intr_map[intr].polarity = GIC_POL_POS;
	gic_intr_map[intr].trigtype = GIC_TRIG_EDGE;
	gic_intr_map[intr].flags = GIC_FLAG_IPI;
	ipi_map[cpu] |= (1 << (cpupin + 2));
}

static void __init fill_ipi_map(void)
{
	int cpu;

	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		fill_ipi_map1(gic_resched_int_base, cpu, GIC_CPU_INT1);
		fill_ipi_map1(gic_call_int_base, cpu, GIC_CPU_INT2);
	}
}
#endif

void __init arch_init_ipiirq(int irq, struct irqaction *action)
{
	setup_irq(irq, action);
	irq_set_handler(irq, handle_percpu_irq);
}

void __init arch_init_irq(void)
{
	init_i8259_irqs();

	if (!cpu_has_veic)
		mips_cpu_irq_init();

	if (gcmp_present)  {
		GCMPGCB(GICBA) = GIC_BASE_ADDR | GCMP_GCB_GICBA_EN_MSK;
		gic_present = 1;
	} else {
		if (mips_revision_sconid == MIPS_REVISION_SCON_ROCIT) {
			_msc01_biu_base = (unsigned long)
					ioremap_nocache(MSC01_BIU_REG_BASE,
						MSC01_BIU_ADDRSPACE_SZ);
			gic_present = (REG(_msc01_biu_base, MSC01_SC_CFG) &
					MSC01_SC_CFG_GICPRES_MSK) >>
					MSC01_SC_CFG_GICPRES_SHF;
		}
	}
	if (gic_present)
		pr_debug("GIC present\n");

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

	if (cpu_has_veic) {
		set_vi_handler(MSC01E_INT_I8259A, malta_hw0_irqdispatch);
		set_vi_handler(MSC01E_INT_COREHI, corehi_irqdispatch);
		setup_irq(MSC01E_INT_BASE+MSC01E_INT_I8259A, &i8259irq);
		setup_irq(MSC01E_INT_BASE+MSC01E_INT_COREHI, &corehi_irqaction);
	} else if (cpu_has_vint) {
		set_vi_handler(MIPSCPU_INT_I8259A, malta_hw0_irqdispatch);
		set_vi_handler(MIPSCPU_INT_COREHI, corehi_irqdispatch);
#ifdef CONFIG_MIPS_MT_SMTC
		setup_irq_smtc(MIPS_CPU_IRQ_BASE+MIPSCPU_INT_I8259A, &i8259irq,
			(0x100 << MIPSCPU_INT_I8259A));
		setup_irq_smtc(MIPS_CPU_IRQ_BASE+MIPSCPU_INT_COREHI,
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
		setup_irq(MIPS_CPU_IRQ_BASE+MIPSCPU_INT_I8259A, &i8259irq);
		setup_irq(MIPS_CPU_IRQ_BASE+MIPSCPU_INT_COREHI,
						&corehi_irqaction);
#endif /* CONFIG_MIPS_MT_SMTC */
	} else {
		setup_irq(MIPS_CPU_IRQ_BASE+MIPSCPU_INT_I8259A, &i8259irq);
		setup_irq(MIPS_CPU_IRQ_BASE+MIPSCPU_INT_COREHI,
						&corehi_irqaction);
	}

	if (gic_present) {
		/* FIXME */
		int i;
#if defined(CONFIG_MIPS_MT_SMP)
		gic_call_int_base = GIC_NUM_INTRS - NR_CPUS;
		gic_resched_int_base = gic_call_int_base - NR_CPUS;
		fill_ipi_map();
#endif
		gic_init(GIC_BASE_ADDR, GIC_ADDRSPACE_SZ, gic_intr_map,
				ARRAY_SIZE(gic_intr_map), MIPS_GIC_IRQ_BASE);
		if (!gcmp_present) {
			/* Enable the GIC */
			i = REG(_msc01_biu_base, MSC01_SC_CFG);
			REG(_msc01_biu_base, MSC01_SC_CFG) =
				(i | (0x1 << MSC01_SC_CFG_GICENA_SHF));
			pr_debug("GIC Enabled\n");
		}
#if defined(CONFIG_MIPS_MT_SMP)
		/* set up ipi interrupts */
		if (cpu_has_vint) {
			set_vi_handler(MIPSCPU_INT_IPI0, malta_ipi_irqdispatch);
			set_vi_handler(MIPSCPU_INT_IPI1, malta_ipi_irqdispatch);
		}
		/* Argh.. this really needs sorting out.. */
		printk("CPU%d: status register was %08x\n", smp_processor_id(), read_c0_status());
		write_c0_status(read_c0_status() | STATUSF_IP3 | STATUSF_IP4);
		printk("CPU%d: status register now %08x\n", smp_processor_id(), read_c0_status());
		write_c0_status(0x1100dc00);
		printk("CPU%d: status register frc %08x\n", smp_processor_id(), read_c0_status());
		for (i = 0; i < NR_CPUS; i++) {
			arch_init_ipiirq(MIPS_GIC_IRQ_BASE +
					 GIC_RESCHED_INT(i), &irq_resched);
			arch_init_ipiirq(MIPS_GIC_IRQ_BASE +
					 GIC_CALL_INT(i), &irq_call);
		}
#endif
	} else {
#if defined(CONFIG_MIPS_MT_SMP)
		/* set up ipi interrupts */
		if (cpu_has_veic) {
			set_vi_handler (MSC01E_INT_SW0, ipi_resched_dispatch);
			set_vi_handler (MSC01E_INT_SW1, ipi_call_dispatch);
			cpu_ipi_resched_irq = MSC01E_INT_SW0;
			cpu_ipi_call_irq = MSC01E_INT_SW1;
		} else {
			if (cpu_has_vint) {
				set_vi_handler (MIPS_CPU_IPI_RESCHED_IRQ, ipi_resched_dispatch);
				set_vi_handler (MIPS_CPU_IPI_CALL_IRQ, ipi_call_dispatch);
			}
			cpu_ipi_resched_irq = MIPS_CPU_IRQ_BASE + MIPS_CPU_IPI_RESCHED_IRQ;
			cpu_ipi_call_irq = MIPS_CPU_IRQ_BASE + MIPS_CPU_IPI_CALL_IRQ;
		}
		arch_init_ipiirq(cpu_ipi_resched_irq, &irq_resched);
		arch_init_ipiirq(cpu_ipi_call_irq, &irq_call);
#endif
	}
}

void malta_be_init(void)
{
	if (gcmp_present) {
		/* Could change CM error mask register */
	}
}


static char *tr[8] = {
	"mem",	"gcr",	"gic",	"mmio",
	"0x04", "0x05", "0x06", "0x07"
};

static char *mcmd[32] = {
	[0x00] = "0x00",
	[0x01] = "Legacy Write",
	[0x02] = "Legacy Read",
	[0x03] = "0x03",
	[0x04] = "0x04",
	[0x05] = "0x05",
	[0x06] = "0x06",
	[0x07] = "0x07",
	[0x08] = "Coherent Read Own",
	[0x09] = "Coherent Read Share",
	[0x0a] = "Coherent Read Discard",
	[0x0b] = "Coherent Ready Share Always",
	[0x0c] = "Coherent Upgrade",
	[0x0d] = "Coherent Writeback",
	[0x0e] = "0x0e",
	[0x0f] = "0x0f",
	[0x10] = "Coherent Copyback",
	[0x11] = "Coherent Copyback Invalidate",
	[0x12] = "Coherent Invalidate",
	[0x13] = "Coherent Write Invalidate",
	[0x14] = "Coherent Completion Sync",
	[0x15] = "0x15",
	[0x16] = "0x16",
	[0x17] = "0x17",
	[0x18] = "0x18",
	[0x19] = "0x19",
	[0x1a] = "0x1a",
	[0x1b] = "0x1b",
	[0x1c] = "0x1c",
	[0x1d] = "0x1d",
	[0x1e] = "0x1e",
	[0x1f] = "0x1f"
};

static char *core[8] = {
	"Invalid/OK",	"Invalid/Data",
	"Shared/OK",	"Shared/Data",
	"Modified/OK",	"Modified/Data",
	"Exclusive/OK", "Exclusive/Data"
};

static char *causes[32] = {
	"None", "GC_WR_ERR", "GC_RD_ERR", "COH_WR_ERR",
	"COH_RD_ERR", "MMIO_WR_ERR", "MMIO_RD_ERR", "0x07",
	"0x08", "0x09", "0x0a", "0x0b",
	"0x0c", "0x0d", "0x0e", "0x0f",
	"0x10", "0x11", "0x12", "0x13",
	"0x14", "0x15", "0x16", "INTVN_WR_ERR",
	"INTVN_RD_ERR", "0x19", "0x1a", "0x1b",
	"0x1c", "0x1d", "0x1e", "0x1f"
};

int malta_be_handler(struct pt_regs *regs, int is_fixup)
{
	/* This duplicates the handling in do_be which seems wrong */
	int retval = is_fixup ? MIPS_BE_FIXUP : MIPS_BE_FATAL;

	if (gcmp_present) {
		unsigned long cm_error = GCMPGCB(GCMEC);
		unsigned long cm_addr = GCMPGCB(GCMEA);
		unsigned long cm_other = GCMPGCB(GCMEO);
		unsigned long cause, ocause;
		char buf[256];

		cause = (cm_error & GCMP_GCB_GMEC_ERROR_TYPE_MSK);
		if (cause != 0) {
			cause >>= GCMP_GCB_GMEC_ERROR_TYPE_SHF;
			if (cause < 16) {
				unsigned long cca_bits = (cm_error >> 15) & 7;
				unsigned long tr_bits = (cm_error >> 12) & 7;
				unsigned long mcmd_bits = (cm_error >> 7) & 0x1f;
				unsigned long stag_bits = (cm_error >> 3) & 15;
				unsigned long sport_bits = (cm_error >> 0) & 7;

				snprintf(buf, sizeof(buf),
					 "CCA=%lu TR=%s MCmd=%s STag=%lu "
					 "SPort=%lu\n",
					 cca_bits, tr[tr_bits], mcmd[mcmd_bits],
					 stag_bits, sport_bits);
			} else {
				/* glob state & sresp together */
				unsigned long c3_bits = (cm_error >> 18) & 7;
				unsigned long c2_bits = (cm_error >> 15) & 7;
				unsigned long c1_bits = (cm_error >> 12) & 7;
				unsigned long c0_bits = (cm_error >> 9) & 7;
				unsigned long sc_bit = (cm_error >> 8) & 1;
				unsigned long mcmd_bits = (cm_error >> 3) & 0x1f;
				unsigned long sport_bits = (cm_error >> 0) & 7;
				snprintf(buf, sizeof(buf),
					 "C3=%s C2=%s C1=%s C0=%s SC=%s "
					 "MCmd=%s SPort=%lu\n",
					 core[c3_bits], core[c2_bits],
					 core[c1_bits], core[c0_bits],
					 sc_bit ? "True" : "False",
					 mcmd[mcmd_bits], sport_bits);
			}

			ocause = (cm_other & GCMP_GCB_GMEO_ERROR_2ND_MSK) >>
				 GCMP_GCB_GMEO_ERROR_2ND_SHF;

			printk("CM_ERROR=%08lx %s <%s>\n", cm_error,
			       causes[cause], buf);
			printk("CM_ADDR =%08lx\n", cm_addr);
			printk("CM_OTHER=%08lx %s\n", cm_other, causes[ocause]);

			/* reprime cause register */
			GCMPGCB(GCMEC) = 0;
		}
	}

	return retval;
}

void gic_enable_interrupt(int irq_vec)
{
	GIC_SET_INTR_MASK(irq_vec);
}

void gic_disable_interrupt(int irq_vec)
{
	GIC_CLR_INTR_MASK(irq_vec);
}

void gic_irq_ack(struct irq_data *d)
{
	int irq = (d->irq - gic_irq_base);

	GIC_CLR_INTR_MASK(irq);

	if (gic_irq_flags[irq] & GIC_TRIG_EDGE)
		GICWRITE(GIC_REG(SHARED, GIC_SH_WEDGE), irq);
}

void gic_finish_irq(struct irq_data *d)
{
	/* Enable interrupts. */
	GIC_SET_INTR_MASK(d->irq - gic_irq_base);
}

void __init gic_platform_init(int irqs, struct irq_chip *irq_controller)
{
	int i;

	for (i = gic_irq_base; i < (gic_irq_base + irqs); i++)
		irq_set_chip(i, irq_controller);
}
