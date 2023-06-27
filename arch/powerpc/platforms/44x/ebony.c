// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Ebony board specific routines
 *
 * Matt Porter <mporter@kernel.crashing.org>
 * Copyright 2002-2005 MontaVista Software Inc.
 *
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 * Copyright (c) 2003-2005 Zultys Technologies
 *
 * Rewritten and ported to the merged powerpc tree:
 * Copyright 2007 David Gibson <dwg@au1.ibm.com>, IBM Corporation.
 */

#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/rtc.h>

#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/time.h>
#include <asm/uic.h>
#include <asm/pci-bridge.h>
#include <asm/ppc4xx.h>

static const struct of_device_id ebony_of_bus[] __initconst = {
	{ .compatible = "ibm,plb4", },
	{ .compatible = "ibm,opb", },
	{ .compatible = "ibm,ebc", },
	{},
};

static int __init ebony_device_probe(void)
{
	of_platform_bus_probe(NULL, ebony_of_bus, NULL);
	of_instantiate_rtc();

	return 0;
}
machine_device_initcall(ebony, ebony_device_probe);

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init ebony_probe(void)
{
	pci_set_flags(PCI_REASSIGN_ALL_RSRC);

	return 1;
}

define_machine(ebony) {
	.name			= "Ebony",
	.compatible		= "ibm,ebony",
	.probe			= ebony_probe,
	.progress		= udbg_progress,
	.init_IRQ		= uic_init_tree,
	.get_irq		= uic_get_irq,
	.restart		= ppc4xx_reset_system,
};
