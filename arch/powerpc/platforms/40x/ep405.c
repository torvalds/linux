/*
 * Architecture- / platform-specific boot-time initialization code for
 * IBM PowerPC 4xx based boards. Adapted from original
 * code by Gary Thomas, Cort Dougan <cort@fsmlabs.com>, and Dan Malek
 * <dan@net4x.com>.
 *
 * Copyright(c) 1999-2000 Grant Erickson <grant@lcse.umn.edu>
 *
 * Rewritten and ported to the merged powerpc tree:
 * Copyright 2007 IBM Corporation
 * Josh Boyer <jwboyer@linux.vnet.ibm.com>
 *
 * Adapted to EP405 by Ben. Herrenschmidt <benh@kernel.crashing.org>
 *
 * TODO: Wire up the PCI IRQ mux and the southbridge interrupts
 *
 * 2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/init.h>
#include <linux/of_platform.h>

#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/time.h>
#include <asm/uic.h>
#include <asm/pci-bridge.h>
#include <asm/ppc4xx.h>

static struct device_node *bcsr_node;
static void __iomem *bcsr_regs;

/* BCSR registers  */
#define BCSR_ID			0
#define BCSR_PCI_CTRL	       	1
#define BCSR_FLASH_NV_POR_CTRL	2
#define BCSR_FENET_UART_CTRL	3
#define BCSR_PCI_IRQ		4
#define BCSR_XIRQ_SELECT	5
#define BCSR_XIRQ_ROUTING	6
#define BCSR_XIRQ_STATUS	7
#define BCSR_XIRQ_STATUS2	8
#define BCSR_SW_STAT_LED_CTRL	9
#define BCSR_GPIO_IRQ_PAR_CTRL	10
/* there's more, can't be bothered typing them tho */


static __initdata struct of_device_id ep405_of_bus[] = {
	{ .compatible = "ibm,plb3", },
	{ .compatible = "ibm,opb", },
	{ .compatible = "ibm,ebc", },
	{},
};

static int __init ep405_device_probe(void)
{
	of_platform_bus_probe(NULL, ep405_of_bus, NULL);

	return 0;
}
machine_device_initcall(ep405, ep405_device_probe);

static void __init ep405_init_bcsr(void)
{
	const u8 *irq_routing;
	int i;

	/* Find the bloody thing & map it */
	bcsr_node = of_find_compatible_node(NULL, NULL, "ep405-bcsr");
	if (bcsr_node == NULL) {
		printk(KERN_ERR "EP405 BCSR not found !\n");
		return;
	}
	bcsr_regs = of_iomap(bcsr_node, 0);
	if (bcsr_regs == NULL) {
		printk(KERN_ERR "EP405 BCSR failed to map !\n");
		return;
	}

	/* Get the irq-routing property and apply the routing to the CPLD */
	irq_routing = of_get_property(bcsr_node, "irq-routing", NULL);
	if (irq_routing == NULL)
		return;
	for (i = 0; i < 16; i++) {
		u8 irq = irq_routing[i];
		out_8(bcsr_regs + BCSR_XIRQ_SELECT, i);
		out_8(bcsr_regs + BCSR_XIRQ_ROUTING, irq);
	}
	in_8(bcsr_regs + BCSR_XIRQ_SELECT);
	mb();
	out_8(bcsr_regs + BCSR_GPIO_IRQ_PAR_CTRL, 0xfe);
}

static void __init ep405_setup_arch(void)
{
	/* Find & init the BCSR CPLD */
	ep405_init_bcsr();

	ppc_pci_flags = PPC_PCI_REASSIGN_ALL_RSRC;
}

static int __init ep405_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (!of_flat_dt_is_compatible(root, "ep405"))
		return 0;

	return 1;
}

define_machine(ep405) {
	.name			= "EP405",
	.probe			= ep405_probe,
	.setup_arch		= ep405_setup_arch,
	.progress		= udbg_progress,
	.init_IRQ		= uic_init_tree,
	.get_irq		= uic_get_irq,
	.restart		= ppc4xx_reset_system,
	.calibrate_decr		= generic_calibrate_decr,
};
