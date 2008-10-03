/*
 * MPC85xx/86xx PCI/PCIE support routing.
 *
 * Copyright 2007 Freescale Semiconductor, Inc
 *
 * Initial author: Xianghua Xiao <x.xiao@freescale.com>
 * Recode: ZHANG WEI <wei.zhang@freescale.com>
 * Rewrite the routing for Frescale PCI and PCI Express
 * 	Roy Zang <tie-fei.zang@freescale.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

#if defined(CONFIG_PPC_85xx) || defined(CONFIG_PPC_86xx)
/* atmu setup for fsl pci/pcie controller */
void __init setup_pci_atmu(struct pci_controller *hose, struct resource *rsrc)
{
	struct ccsr_pci __iomem *pci;
	int i;

	pr_debug("PCI memory map start 0x%016llx, size 0x%016llx\n",
		    (u64)rsrc->start, (u64)rsrc->end - (u64)rsrc->start + 1);
	pci = ioremap(rsrc->start, rsrc->end - rsrc->start + 1);

	/* Disable all windows (except powar0 since its ignored) */
	for(i = 1; i < 5; i++)
		out_be32(&pci->pow[i].powar, 0);
	for(i = 0; i < 3; i++)
		out_be32(&pci->piw[i].piwar, 0);

	/* Setup outbound MEM window */
	for(i = 0; i < 3; i++)
		if (hose->mem_resources[i].flags & IORESOURCE_MEM){
			resource_size_t pci_addr_start =
				 hose->mem_resources[i].start -
				 hose->pci_mem_offset;
			pr_debug("PCI MEM resource start 0x%016llx, size 0x%016llx.\n",
				(u64)hose->mem_resources[i].start,
				(u64)hose->mem_resources[i].end
				  - (u64)hose->mem_resources[i].start + 1);
			out_be32(&pci->pow[i+1].potar, (pci_addr_start >> 12));
			out_be32(&pci->pow[i+1].potear, 0);
			out_be32(&pci->pow[i+1].powbar,
				(hose->mem_resources[i].start >> 12));
			/* Enable, Mem R/W */
			out_be32(&pci->pow[i+1].powar, 0x80044000
				| (__ilog2(hose->mem_resources[i].end
				- hose->mem_resources[i].start + 1) - 1));
		}

	/* Setup outbound IO window */
	if (hose->io_resource.flags & IORESOURCE_IO){
		pr_debug("PCI IO resource start 0x%016llx, size 0x%016llx, "
			 "phy base 0x%016llx.\n",
			(u64)hose->io_resource.start,
			(u64)hose->io_resource.end - (u64)hose->io_resource.start + 1,
			(u64)hose->io_base_phys);
		out_be32(&pci->pow[i+1].potar, (hose->io_resource.start >> 12));
		out_be32(&pci->pow[i+1].potear, 0);
		out_be32(&pci->pow[i+1].powbar, (hose->io_base_phys >> 12));
		/* Enable, IO R/W */
		out_be32(&pci->pow[i+1].powar, 0x80088000
			| (__ilog2(hose->io_resource.end
			- hose->io_resource.start + 1) - 1));
	}

	/* Setup 2G inbound Memory Window @ 1 */
	out_be32(&pci->piw[2].pitar, 0x00000000);
	out_be32(&pci->piw[2].piwbar,0x00000000);
	out_be32(&pci->piw[2].piwar, PIWAR_2G);
}

void __init setup_pci_cmd(struct pci_controller *hose)
{
	u16 cmd;
	int cap_x;

	early_read_config_word(hose, 0, 0, PCI_COMMAND, &cmd);
	cmd |= PCI_COMMAND_SERR | PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY
		| PCI_COMMAND_IO;
	early_write_config_word(hose, 0, 0, PCI_COMMAND, cmd);

	cap_x = early_find_capability(hose, 0, 0, PCI_CAP_ID_PCIX);
	if (cap_x) {
		int pci_x_cmd = cap_x + PCI_X_CMD;
		cmd = PCI_X_CMD_MAX_SPLIT | PCI_X_CMD_MAX_READ
			| PCI_X_CMD_ERO | PCI_X_CMD_DPERR_E;
		early_write_config_word(hose, 0, 0, pci_x_cmd, cmd);
	} else {
		early_write_config_byte(hose, 0, 0, PCI_LATENCY_TIMER, 0x80);
	}
}

static void __init setup_pci_pcsrbar(struct pci_controller *hose)
{
#ifdef CONFIG_PCI_MSI
	phys_addr_t immr_base;

	immr_base = get_immrbase();
	early_write_config_dword(hose, 0, 0, PCI_BASE_ADDRESS_0, immr_base);
#endif
}

static int fsl_pcie_bus_fixup;

static void __init quirk_fsl_pcie_header(struct pci_dev *dev)
{
	/* if we aren't a PCIe don't bother */
	if (!pci_find_capability(dev, PCI_CAP_ID_EXP))
		return ;

	dev->class = PCI_CLASS_BRIDGE_PCI << 8;
	fsl_pcie_bus_fixup = 1;
	return ;
}

int __init fsl_pcie_check_link(struct pci_controller *hose)
{
	u32 val;
	early_read_config_dword(hose, 0, 0, PCIE_LTSSM, &val);
	if (val < PCIE_LTSSM_L0)
		return 1;
	return 0;
}

void fsl_pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_controller *hose = (struct pci_controller *) bus->sysdata;
	int i;

	if ((bus->parent == hose->bus) &&
	    ((fsl_pcie_bus_fixup &&
	      early_find_capability(hose, 0, 0, PCI_CAP_ID_EXP)) ||
	     (hose->indirect_type & PPC_INDIRECT_TYPE_NO_PCIE_LINK)))
	{
		for (i = 0; i < 4; ++i) {
			struct resource *res = bus->resource[i];
			struct resource *par = bus->parent->resource[i];
			if (res) {
				res->start = 0;
				res->end   = 0;
				res->flags = 0;
			}
			if (res && par) {
				res->start = par->start;
				res->end   = par->end;
				res->flags = par->flags;
			}
		}
	}
}

int __init fsl_add_bridge(struct device_node *dev, int is_primary)
{
	int len;
	struct pci_controller *hose;
	struct resource rsrc;
	const int *bus_range;

	pr_debug("Adding PCI host bridge %s\n", dev->full_name);

	/* Fetch host bridge registers address */
	if (of_address_to_resource(dev, 0, &rsrc)) {
		printk(KERN_WARNING "Can't get pci register base!");
		return -ENOMEM;
	}

	/* Get bus range if any */
	bus_range = of_get_property(dev, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int))
		printk(KERN_WARNING "Can't get bus-range for %s, assume"
			" bus 0\n", dev->full_name);

	ppc_pci_flags |= PPC_PCI_REASSIGN_ALL_BUS;
	hose = pcibios_alloc_controller(dev);
	if (!hose)
		return -ENOMEM;

	hose->first_busno = bus_range ? bus_range[0] : 0x0;
	hose->last_busno = bus_range ? bus_range[1] : 0xff;

	setup_indirect_pci(hose, rsrc.start, rsrc.start + 0x4,
		PPC_INDIRECT_TYPE_BIG_ENDIAN);
	setup_pci_cmd(hose);

	/* check PCI express link status */
	if (early_find_capability(hose, 0, 0, PCI_CAP_ID_EXP)) {
		hose->indirect_type |= PPC_INDIRECT_TYPE_EXT_REG |
			PPC_INDIRECT_TYPE_SURPRESS_PRIMARY_BUS;
		if (fsl_pcie_check_link(hose))
			hose->indirect_type |= PPC_INDIRECT_TYPE_NO_PCIE_LINK;
	}

	printk(KERN_INFO "Found FSL PCI host bridge at 0x%016llx. "
		"Firmware bus number: %d->%d\n",
		(unsigned long long)rsrc.start, hose->first_busno,
		hose->last_busno);

	pr_debug(" ->Hose at 0x%p, cfg_addr=0x%p,cfg_data=0x%p\n",
		hose, hose->cfg_addr, hose->cfg_data);

	/* Interpret the "ranges" property */
	/* This also maps the I/O region and sets isa_io/mem_base */
	pci_process_bridge_OF_ranges(hose, dev, is_primary);

	/* Setup PEX window registers */
	setup_pci_atmu(hose, &rsrc);

	/* Setup PEXCSRBAR */
	setup_pci_pcsrbar(hose);
	return 0;
}

DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8548E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8548, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8543E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8543, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8547E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8545E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8545, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8568E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8568, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8567E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8567, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8533E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8533, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8544E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8544, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8572E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8572, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8536E, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8536, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8641, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8641D, quirk_fsl_pcie_header);
DECLARE_PCI_FIXUP_HEADER(0x1957, PCI_DEVICE_ID_MPC8610, quirk_fsl_pcie_header);
#endif /* CONFIG_PPC_85xx || CONFIG_PPC_86xx */

#if defined(CONFIG_PPC_83xx)
int __init mpc83xx_add_bridge(struct device_node *dev)
{
	int len;
	struct pci_controller *hose;
	struct resource rsrc;
	const int *bus_range;
	int primary = 1, has_address = 0;
	phys_addr_t immr = get_immrbase();

	pr_debug("Adding PCI host bridge %s\n", dev->full_name);

	/* Fetch host bridge registers address */
	has_address = (of_address_to_resource(dev, 0, &rsrc) == 0);

	/* Get bus range if any */
	bus_range = of_get_property(dev, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int)) {
		printk(KERN_WARNING "Can't get bus-range for %s, assume"
		       " bus 0\n", dev->full_name);
	}

	ppc_pci_flags |= PPC_PCI_REASSIGN_ALL_BUS;
	hose = pcibios_alloc_controller(dev);
	if (!hose)
		return -ENOMEM;

	hose->first_busno = bus_range ? bus_range[0] : 0;
	hose->last_busno = bus_range ? bus_range[1] : 0xff;

	/* MPC83xx supports up to two host controllers one at 0x8500 from immrbar
	 * the other at 0x8600, we consider the 0x8500 the primary controller
	 */
	/* PCI 1 */
	if ((rsrc.start & 0xfffff) == 0x8500) {
		setup_indirect_pci(hose, immr + 0x8300, immr + 0x8304, 0);
	}
	/* PCI 2 */
	if ((rsrc.start & 0xfffff) == 0x8600) {
		setup_indirect_pci(hose, immr + 0x8380, immr + 0x8384, 0);
		primary = 0;
	}

	printk(KERN_INFO "Found MPC83xx PCI host bridge at 0x%016llx. "
	       "Firmware bus number: %d->%d\n",
	       (unsigned long long)rsrc.start, hose->first_busno,
	       hose->last_busno);

	pr_debug(" ->Hose at 0x%p, cfg_addr=0x%p,cfg_data=0x%p\n",
	    hose, hose->cfg_addr, hose->cfg_data);

	/* Interpret the "ranges" property */
	/* This also maps the I/O region and sets isa_io/mem_base */
	pci_process_bridge_OF_ranges(hose, dev, primary);

	return 0;
}
#endif /* CONFIG_PPC_83xx */
