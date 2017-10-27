/*
 * Copyright (C) 2006-2007 PA Semi, Inc
 *
 * Authors: Kip Walker, PA Semi
 *	    Olof Johansson, PA Semi
 *
 * Maintained by: Olof Johansson <olof@lixom.net>
 *
 * Based on arch/powerpc/platforms/maple/setup.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/console.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/of_platform.h>
#include <linux/gfp.h>

#include <asm/prom.h>
#include <asm/iommu.h>
#include <asm/machdep.h>
#include <asm/mpic.h>
#include <asm/smp.h>
#include <asm/time.h>
#include <asm/mmu.h>
#include <asm/debug.h>

#include <pcmcia/ss.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

#include "pasemi.h"

/* SDC reset register, must be pre-mapped at reset time */
static void __iomem *reset_reg;

/* Various error status registers, must be pre-mapped at MCE time */

#define MAX_MCE_REGS	32
struct mce_regs {
	char *name;
	void __iomem *addr;
};

static struct mce_regs mce_regs[MAX_MCE_REGS];
static int num_mce_regs;
static int nmi_virq = 0;


static void __noreturn pas_restart(char *cmd)
{
	/* Need to put others cpu in hold loop so they're not sleeping */
	smp_send_stop();
	udelay(10000);
	printk("Restarting...\n");
	while (1)
		out_le32(reset_reg, 0x6000000);
}

#ifdef CONFIG_SMP
static arch_spinlock_t timebase_lock;
static unsigned long timebase;

static void pas_give_timebase(void)
{
	unsigned long flags;

	local_irq_save(flags);
	hard_irq_disable();
	arch_spin_lock(&timebase_lock);
	mtspr(SPRN_TBCTL, TBCTL_FREEZE);
	isync();
	timebase = get_tb();
	arch_spin_unlock(&timebase_lock);

	while (timebase)
		barrier();
	mtspr(SPRN_TBCTL, TBCTL_RESTART);
	local_irq_restore(flags);
}

static void pas_take_timebase(void)
{
	while (!timebase)
		smp_rmb();

	arch_spin_lock(&timebase_lock);
	set_tb(timebase >> 32, timebase & 0xffffffff);
	timebase = 0;
	arch_spin_unlock(&timebase_lock);
}

static struct smp_ops_t pas_smp_ops = {
	.probe		= smp_mpic_probe,
	.message_pass	= smp_mpic_message_pass,
	.kick_cpu	= smp_generic_kick_cpu,
	.setup_cpu	= smp_mpic_setup_cpu,
	.give_timebase	= pas_give_timebase,
	.take_timebase	= pas_take_timebase,
};
#endif /* CONFIG_SMP */

static void __init pas_setup_arch(void)
{
#ifdef CONFIG_SMP
	/* Setup SMP callback */
	smp_ops = &pas_smp_ops;
#endif
	/* Lookup PCI hosts */
	pas_pci_init();

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	/* Remap SDC register for doing reset */
	/* XXXOJN This should maybe come out of the device tree */
	reset_reg = ioremap(0xfc101100, 4);
}

static int __init pas_setup_mce_regs(void)
{
	struct pci_dev *dev;
	int reg;

	/* Remap various SoC status registers for use by the MCE handler */

	reg = 0;

	dev = pci_get_device(PCI_VENDOR_ID_PASEMI, 0xa00a, NULL);
	while (dev && reg < MAX_MCE_REGS) {
		mce_regs[reg].name = kasprintf(GFP_KERNEL,
						"mc%d_mcdebug_errsta", reg);
		mce_regs[reg].addr = pasemi_pci_getcfgaddr(dev, 0x730);
		dev = pci_get_device(PCI_VENDOR_ID_PASEMI, 0xa00a, dev);
		reg++;
	}

	dev = pci_get_device(PCI_VENDOR_ID_PASEMI, 0xa001, NULL);
	if (dev && reg+4 < MAX_MCE_REGS) {
		mce_regs[reg].name = "iobdbg_IntStatus1";
		mce_regs[reg].addr = pasemi_pci_getcfgaddr(dev, 0x438);
		reg++;
		mce_regs[reg].name = "iobdbg_IOCTbusIntDbgReg";
		mce_regs[reg].addr = pasemi_pci_getcfgaddr(dev, 0x454);
		reg++;
		mce_regs[reg].name = "iobiom_IntStatus";
		mce_regs[reg].addr = pasemi_pci_getcfgaddr(dev, 0xc10);
		reg++;
		mce_regs[reg].name = "iobiom_IntDbgReg";
		mce_regs[reg].addr = pasemi_pci_getcfgaddr(dev, 0xc1c);
		reg++;
	}

	dev = pci_get_device(PCI_VENDOR_ID_PASEMI, 0xa009, NULL);
	if (dev && reg+2 < MAX_MCE_REGS) {
		mce_regs[reg].name = "l2csts_IntStatus";
		mce_regs[reg].addr = pasemi_pci_getcfgaddr(dev, 0x200);
		reg++;
		mce_regs[reg].name = "l2csts_Cnt";
		mce_regs[reg].addr = pasemi_pci_getcfgaddr(dev, 0x214);
		reg++;
	}

	num_mce_regs = reg;

	return 0;
}
machine_device_initcall(pasemi, pas_setup_mce_regs);

static __init void pas_init_IRQ(void)
{
	struct device_node *np;
	struct device_node *root, *mpic_node;
	unsigned long openpic_addr;
	const unsigned int *opprop;
	int naddr, opplen;
	int mpic_flags;
	const unsigned int *nmiprop;
	struct mpic *mpic;

	mpic_node = NULL;

	for_each_node_by_type(np, "interrupt-controller")
		if (of_device_is_compatible(np, "open-pic")) {
			mpic_node = np;
			break;
		}
	if (!mpic_node)
		for_each_node_by_type(np, "open-pic") {
			mpic_node = np;
			break;
		}
	if (!mpic_node) {
		printk(KERN_ERR
			"Failed to locate the MPIC interrupt controller\n");
		return;
	}

	/* Find address list in /platform-open-pic */
	root = of_find_node_by_path("/");
	naddr = of_n_addr_cells(root);
	opprop = of_get_property(root, "platform-open-pic", &opplen);
	if (!opprop) {
		printk(KERN_ERR "No platform-open-pic property.\n");
		of_node_put(root);
		return;
	}
	openpic_addr = of_read_number(opprop, naddr);
	printk(KERN_DEBUG "OpenPIC addr: %lx\n", openpic_addr);

	mpic_flags = MPIC_LARGE_VECTORS | MPIC_NO_BIAS | MPIC_NO_RESET;

	nmiprop = of_get_property(mpic_node, "nmi-source", NULL);
	if (nmiprop)
		mpic_flags |= MPIC_ENABLE_MCK;

	mpic = mpic_alloc(mpic_node, openpic_addr,
			  mpic_flags, 0, 0, "PASEMI-OPIC");
	BUG_ON(!mpic);

	mpic_assign_isu(mpic, 0, mpic->paddr + 0x10000);
	mpic_init(mpic);
	/* The NMI/MCK source needs to be prio 15 */
	if (nmiprop) {
		nmi_virq = irq_create_mapping(NULL, *nmiprop);
		mpic_irq_set_priority(nmi_virq, 15);
		irq_set_irq_type(nmi_virq, IRQ_TYPE_EDGE_RISING);
		mpic_unmask_irq(irq_get_irq_data(nmi_virq));
	}

	of_node_put(mpic_node);
	of_node_put(root);
}

static void __init pas_progress(char *s, unsigned short hex)
{
	printk("[%04x] : %s\n", hex, s ? s : "");
}


static int pas_machine_check_handler(struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	unsigned long srr0, srr1, dsisr;
	int dump_slb = 0;
	int i;

	srr0 = regs->nip;
	srr1 = regs->msr;

	if (nmi_virq && mpic_get_mcirq() == nmi_virq) {
		printk(KERN_ERR "NMI delivered\n");
		debugger(regs);
		mpic_end_irq(irq_get_irq_data(nmi_virq));
		goto out;
	}

	dsisr = mfspr(SPRN_DSISR);
	printk(KERN_ERR "Machine Check on CPU %d\n", cpu);
	printk(KERN_ERR "SRR0  0x%016lx SRR1 0x%016lx\n", srr0, srr1);
	printk(KERN_ERR "DSISR 0x%016lx DAR  0x%016lx\n", dsisr, regs->dar);
	printk(KERN_ERR "BER   0x%016lx MER  0x%016lx\n", mfspr(SPRN_PA6T_BER),
		mfspr(SPRN_PA6T_MER));
	printk(KERN_ERR "IER   0x%016lx DER  0x%016lx\n", mfspr(SPRN_PA6T_IER),
		mfspr(SPRN_PA6T_DER));
	printk(KERN_ERR "Cause:\n");

	if (srr1 & 0x200000)
		printk(KERN_ERR "Signalled by SDC\n");

	if (srr1 & 0x100000) {
		printk(KERN_ERR "Load/Store detected error:\n");
		if (dsisr & 0x8000)
			printk(KERN_ERR "D-cache ECC double-bit error or bus error\n");
		if (dsisr & 0x4000)
			printk(KERN_ERR "LSU snoop response error\n");
		if (dsisr & 0x2000) {
			printk(KERN_ERR "MMU SLB multi-hit or invalid B field\n");
			dump_slb = 1;
		}
		if (dsisr & 0x1000)
			printk(KERN_ERR "Recoverable Duptags\n");
		if (dsisr & 0x800)
			printk(KERN_ERR "Recoverable D-cache parity error count overflow\n");
		if (dsisr & 0x400)
			printk(KERN_ERR "TLB parity error count overflow\n");
	}

	if (srr1 & 0x80000)
		printk(KERN_ERR "Bus Error\n");

	if (srr1 & 0x40000) {
		printk(KERN_ERR "I-side SLB multiple hit\n");
		dump_slb = 1;
	}

	if (srr1 & 0x20000)
		printk(KERN_ERR "I-cache parity error hit\n");

	if (num_mce_regs == 0)
		printk(KERN_ERR "No MCE registers mapped yet, can't dump\n");
	else
		printk(KERN_ERR "SoC debug registers:\n");

	for (i = 0; i < num_mce_regs; i++)
		printk(KERN_ERR "%s: 0x%08x\n", mce_regs[i].name,
			in_le32(mce_regs[i].addr));

	if (dump_slb) {
		unsigned long e, v;
		int i;

		printk(KERN_ERR "slb contents:\n");
		for (i = 0; i < mmu_slb_size; i++) {
			asm volatile("slbmfee  %0,%1" : "=r" (e) : "r" (i));
			asm volatile("slbmfev  %0,%1" : "=r" (v) : "r" (i));
			printk(KERN_ERR "%02d %016lx %016lx\n", i, e, v);
		}
	}

out:
	/* SRR1[62] is from MSR[62] if recoverable, so pass that back */
	return !!(srr1 & 0x2);
}

#ifdef CONFIG_PCMCIA
static int pcmcia_notify(struct notifier_block *nb, unsigned long action,
			 void *data)
{
	struct device *dev = data;
	struct device *parent;
	struct pcmcia_device *pdev = to_pcmcia_dev(dev);

	/* We are only intereted in device addition */
	if (action != BUS_NOTIFY_ADD_DEVICE)
		return 0;

	parent = pdev->socket->dev.parent;

	/* We know electra_cf devices will always have of_node set, since
	 * electra_cf is an of_platform driver.
	 */
	if (!parent->of_node)
		return 0;

	if (!of_device_is_compatible(parent->of_node, "electra-cf"))
		return 0;

	/* We use the direct ops for localbus */
	dev->dma_ops = &dma_direct_ops;

	return 0;
}

static struct notifier_block pcmcia_notifier = {
	.notifier_call = pcmcia_notify,
};

static inline void pasemi_pcmcia_init(void)
{
	extern struct bus_type pcmcia_bus_type;

	bus_register_notifier(&pcmcia_bus_type, &pcmcia_notifier);
}

#else

static inline void pasemi_pcmcia_init(void)
{
}

#endif


static const struct of_device_id pasemi_bus_ids[] = {
	/* Unfortunately needed for legacy firmwares */
	{ .type = "localbus", },
	{ .type = "sdc", },
	/* These are the proper entries, which newer firmware uses */
	{ .compatible = "pasemi,localbus", },
	{ .compatible = "pasemi,sdc", },
	{},
};

static int __init pasemi_publish_devices(void)
{
	pasemi_pcmcia_init();

	/* Publish OF platform devices for SDC and other non-PCI devices */
	of_platform_bus_probe(NULL, pasemi_bus_ids, NULL);

	return 0;
}
machine_device_initcall(pasemi, pasemi_publish_devices);


/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init pas_probe(void)
{
	if (!of_machine_is_compatible("PA6T-1682M") &&
	    !of_machine_is_compatible("pasemi,pwrficient"))
		return 0;

	iommu_init_early_pasemi();

	return 1;
}

define_machine(pasemi) {
	.name			= "PA Semi PWRficient",
	.probe			= pas_probe,
	.setup_arch		= pas_setup_arch,
	.init_IRQ		= pas_init_IRQ,
	.get_irq		= mpic_get_irq,
	.restart		= pas_restart,
	.get_boot_time		= pas_get_boot_time,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= pas_progress,
	.machine_check_exception = pas_machine_check_handler,
};
