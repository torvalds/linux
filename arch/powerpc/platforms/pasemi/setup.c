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
#include <linux/pci.h>

#include <asm/prom.h>
#include <asm/system.h>
#include <asm/iommu.h>
#include <asm/machdep.h>
#include <asm/mpic.h>
#include <asm/smp.h>
#include <asm/time.h>
#include <asm/of_platform.h>

#include "pasemi.h"

static void __iomem *reset_reg;

static void pas_restart(char *cmd)
{
	printk("Restarting...\n");
	while (1)
		out_le32(reset_reg, 0x6000000);
}

#ifdef CONFIG_SMP
static DEFINE_SPINLOCK(timebase_lock);

static void __devinit pas_give_timebase(void)
{
	unsigned long tb;

	spin_lock(&timebase_lock);
	mtspr(SPRN_TBCTL, TBCTL_FREEZE);
	tb = mftb();
	mtspr(SPRN_TBCTL, TBCTL_UPDATE_LOWER | (tb & 0xffffffff));
	mtspr(SPRN_TBCTL, TBCTL_UPDATE_UPPER | (tb >> 32));
	mtspr(SPRN_TBCTL, TBCTL_RESTART);
	spin_unlock(&timebase_lock);
	pr_debug("pas_give_timebase: cpu %d gave tb %lx\n",
		 smp_processor_id(), tb);
}

static void __devinit pas_take_timebase(void)
{
	pr_debug("pas_take_timebase: cpu %d has tb %lx\n",
		 smp_processor_id(), mftb());
}

struct smp_ops_t pas_smp_ops = {
	.probe		= smp_mpic_probe,
	.message_pass	= smp_mpic_message_pass,
	.kick_cpu	= smp_generic_kick_cpu,
	.setup_cpu	= smp_mpic_setup_cpu,
	.give_timebase	= pas_give_timebase,
	.take_timebase	= pas_take_timebase,
};
#endif /* CONFIG_SMP */

void __init pas_setup_arch(void)
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

	pasemi_idle_init();
}

static __init void pas_init_IRQ(void)
{
	struct device_node *np;
	struct device_node *root, *mpic_node;
	unsigned long openpic_addr;
	const unsigned int *opprop;
	int naddr, opplen;
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

	mpic = mpic_alloc(mpic_node, openpic_addr,
			  MPIC_PRIMARY|MPIC_LARGE_VECTORS|MPIC_WANTS_RESET,
			  0, 0, " PAS-OPIC  ");
	BUG_ON(!mpic);

	mpic_assign_isu(mpic, 0, openpic_addr + 0x10000);
	mpic_init(mpic);
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

	srr0 = regs->nip;
	srr1 = regs->msr;
	dsisr = mfspr(SPRN_DSISR);
	printk(KERN_ERR "Machine Check on CPU %d\n", cpu);
	printk(KERN_ERR "SRR0 0x%016lx SRR1 0x%016lx\n", srr0, srr1);
	printk(KERN_ERR "DSISR 0x%016lx DAR 0x%016lx\n", dsisr, regs->dar);
	printk(KERN_ERR "Cause:\n");

	if (srr1 & 0x200000)
		printk(KERN_ERR "Signalled by SDC\n");
	if (srr1 & 0x100000) {
		printk(KERN_ERR "Load/Store detected error:\n");
		if (dsisr & 0x8000)
			printk(KERN_ERR "D-cache ECC double-bit error or bus error\n");
		if (dsisr & 0x4000)
			printk(KERN_ERR "LSU snoop response error\n");
		if (dsisr & 0x2000)
			printk(KERN_ERR "MMU SLB multi-hit or invalid B field\n");
		if (dsisr & 0x1000)
			printk(KERN_ERR "Recoverable Duptags\n");
		if (dsisr & 0x800)
			printk(KERN_ERR "Recoverable D-cache parity error count overflow\n");
		if (dsisr & 0x400)
			printk(KERN_ERR "TLB parity error count overflow\n");
	}
	if (srr1 & 0x80000)
		printk(KERN_ERR "Bus Error\n");
	if (srr1 & 0x40000)
		printk(KERN_ERR "I-side SLB multiple hit\n");
	if (srr1 & 0x20000)
		printk(KERN_ERR "I-cache parity error hit\n");

	/* SRR1[62] is from MSR[62] if recoverable, so pass that back */
	return !!(srr1 & 0x2);
}

static void __init pas_init_early(void)
{
	iommu_init_early_pasemi();
}

static struct of_device_id pasemi_bus_ids[] = {
	{ .type = "sdc", },
	{},
};

static int __init pasemi_publish_devices(void)
{
	if (!machine_is(pasemi))
		return 0;

	/* Publish OF platform devices for SDC and other non-PCI devices */
	of_platform_bus_probe(NULL, pasemi_bus_ids, NULL);

	return 0;
}
device_initcall(pasemi_publish_devices);


/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init pas_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (!of_flat_dt_is_compatible(root, "PA6T-1682M"))
		return 0;

	hpte_init_native();

	alloc_iobmap_l2();

	return 1;
}

define_machine(pasemi) {
	.name			= "PA Semi PA6T-1682M",
	.probe			= pas_probe,
	.setup_arch		= pas_setup_arch,
	.init_early		= pas_init_early,
	.init_IRQ		= pas_init_IRQ,
	.get_irq		= mpic_get_irq,
	.restart		= pas_restart,
	.get_boot_time		= pas_get_boot_time,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= pas_progress,
	.machine_check_exception = pas_machine_check_handler,
};
