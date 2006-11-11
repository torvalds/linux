/*
 * Copyright (C) 2006 PA Semi, Inc
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

#include <asm/prom.h>
#include <asm/system.h>
#include <asm/iommu.h>
#include <asm/machdep.h>
#include <asm/mpic.h>
#include <asm/smp.h>
#include <asm/time.h>

#include "pasemi.h"

static void pas_restart(char *cmd)
{
	printk("restart unimplemented, looping...\n");
	for (;;) ;
}

static void pas_power_off(void)
{
	printk("power off unimplemented, looping...\n");
	for (;;) ;
}

static void pas_halt(void)
{
	pas_power_off();
}

#ifdef CONFIG_SMP
struct smp_ops_t pas_smp_ops = {
	.probe		= smp_mpic_probe,
	.message_pass	= smp_mpic_message_pass,
	.kick_cpu	= smp_generic_kick_cpu,
	.setup_cpu	= smp_mpic_setup_cpu,
	.give_timebase	= smp_generic_give_timebase,
	.take_timebase	= smp_generic_take_timebase,
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

	printk(KERN_DEBUG "Using default idle loop\n");
}

static void iommu_dev_setup_null(struct pci_dev *dev) { }
static void iommu_bus_setup_null(struct pci_bus *bus) { }

static void __init pas_init_early(void)
{
	/* No iommu code yet */
	ppc_md.iommu_dev_setup = iommu_dev_setup_null;
	ppc_md.iommu_bus_setup = iommu_bus_setup_null;
	pci_direct_iommu_init();
}

/* No legacy IO on our parts */
static int pas_check_legacy_ioport(unsigned int baseport)
{
	return -ENODEV;
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
		if (device_is_compatible(np, "open-pic")) {
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
	naddr = prom_n_addr_cells(root);
	opprop = get_property(root, "platform-open-pic", &opplen);
	if (!opprop) {
		printk(KERN_ERR "No platform-open-pic property.\n");
		of_node_put(root);
		return;
	}
	openpic_addr = of_read_number(opprop, naddr);
	printk(KERN_DEBUG "OpenPIC addr: %lx\n", openpic_addr);
	of_node_put(root);

	mpic = mpic_alloc(mpic_node, openpic_addr, MPIC_PRIMARY, 0, 0,
			  " PAS-OPIC  ");
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


/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init pas_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (!of_flat_dt_is_compatible(root, "PA6T-1682M"))
		return 0;

	hpte_init_native();

	return 1;
}

define_machine(pas) {
	.name			= "PA Semi PA6T-1682M",
	.probe			= pas_probe,
	.setup_arch		= pas_setup_arch,
	.init_early		= pas_init_early,
	.init_IRQ		= pas_init_IRQ,
	.get_irq		= mpic_get_irq,
	.restart		= pas_restart,
	.power_off		= pas_power_off,
	.halt			= pas_halt,
	.get_boot_time		= pas_get_boot_time,
	.calibrate_decr		= generic_calibrate_decr,
	.check_legacy_ioport    = pas_check_legacy_ioport,
	.progress		= pas_progress,
};
