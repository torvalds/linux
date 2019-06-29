// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Common PowerQUICC II code.
 *
 * Author: Scott Wood <scottwood@freescale.com>
 * Copyright (c) 2007 Freescale Semiconductor
 *
 * Based on code by Vitaly Bordug <vbordug@ru.mvista.com>
 * pq2_restart fix by Wade Farnsworth <wfarnsworth@mvista.com>
 * Copyright (c) 2006 MontaVista Software, Inc.
 */

#include <asm/cpm2.h>
#include <asm/io.h>
#include <asm/pci-bridge.h>

#include <platforms/82xx/pq2.h>

#define RMR_CSRE 0x00000001

void __noreturn pq2_restart(char *cmd)
{
	local_irq_disable();
	setbits32(&cpm2_immr->im_clkrst.car_rmr, RMR_CSRE);

	/* Clear the ME,EE,IR & DR bits in MSR to cause checkstop */
	mtmsr(mfmsr() & ~(MSR_ME | MSR_EE | MSR_IR | MSR_DR));
	in_8(&cpm2_immr->im_clkrst.res[0]);

	panic("Restart failed\n");
}

#ifdef CONFIG_PCI
static int pq2_pci_exclude_device(struct pci_controller *hose,
                                  u_char bus, u8 devfn)
{
	if (bus == 0 && PCI_SLOT(devfn) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	else
		return PCIBIOS_SUCCESSFUL;
}

static void __init pq2_pci_add_bridge(struct device_node *np)
{
	struct pci_controller *hose;
	struct resource r;

	if (of_address_to_resource(np, 0, &r) || r.end - r.start < 0x10b)
		goto err;

	pci_add_flags(PCI_REASSIGN_ALL_BUS);

	hose = pcibios_alloc_controller(np);
	if (!hose)
		return;

	hose->dn = np;

	setup_indirect_pci(hose, r.start + 0x100, r.start + 0x104, 0);
	pci_process_bridge_OF_ranges(hose, np, 1);

	return;

err:
	printk(KERN_ERR "No valid PCI reg property in device tree\n");
}

void __init pq2_init_pci(void)
{
	struct device_node *np;

	ppc_md.pci_exclude_device = pq2_pci_exclude_device;

	for_each_compatible_node(np, NULL, "fsl,pq2-pci")
		pq2_pci_add_bridge(np);
}
#endif
