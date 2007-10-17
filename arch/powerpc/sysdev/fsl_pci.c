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

/* atmu setup for fsl pci/pcie controller */
void __init setup_pci_atmu(struct pci_controller *hose, struct resource *rsrc)
{
	struct ccsr_pci __iomem *pci;
	int i;

	pr_debug("PCI memory map start 0x%x, size 0x%x\n", rsrc->start,
			rsrc->end - rsrc->start + 1);
	pci = ioremap(rsrc->start, rsrc->end - rsrc->start + 1);

	/* Disable all windows (except powar0 since its ignored) */
	for(i = 1; i < 5; i++)
		out_be32(&pci->pow[i].powar, 0);
	for(i = 0; i < 3; i++)
		out_be32(&pci->piw[i].piwar, 0);

	/* Setup outbound MEM window */
	for(i = 0; i < 3; i++)
		if (hose->mem_resources[i].flags & IORESOURCE_MEM){
			pr_debug("PCI MEM resource start 0x%08x, size 0x%08x.\n",
				hose->mem_resources[i].start,
				hose->mem_resources[i].end
				  - hose->mem_resources[i].start + 1);
			out_be32(&pci->pow[i+1].potar,
				(hose->mem_resources[i].start >> 12)
				& 0x000fffff);
			out_be32(&pci->pow[i+1].potear, 0);
			out_be32(&pci->pow[i+1].powbar,
				(hose->mem_resources[i].start >> 12)
				& 0x000fffff);
			/* Enable, Mem R/W */
			out_be32(&pci->pow[i+1].powar, 0x80044000
				| (__ilog2(hose->mem_resources[i].end
				- hose->mem_resources[i].start + 1) - 1));
		}

	/* Setup outbound IO window */
	if (hose->io_resource.flags & IORESOURCE_IO){
		pr_debug("PCI IO resource start 0x%08x, size 0x%08x, phy base 0x%08x.\n",
			hose->io_resource.start,
			hose->io_resource.end - hose->io_resource.start + 1,
			hose->io_base_phys);
		out_be32(&pci->pow[i+1].potar, (hose->io_resource.start >> 12)
				& 0x000fffff);
		out_be32(&pci->pow[i+1].potear, 0);
		out_be32(&pci->pow[i+1].powbar, (hose->io_base_phys >> 12)
				& 0x000fffff);
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

static void __init quirk_fsl_pcie_transparent(struct pci_dev *dev)
{
	struct resource *res;
	int i, res_idx = PCI_BRIDGE_RESOURCES;
	struct pci_controller *hose;

	/* if we aren't a PCIe don't bother */
	if (!pci_find_capability(dev, PCI_CAP_ID_EXP))
		return ;

	/*
	 * Make the bridge be transparent.
	 */
	dev->transparent = 1;

	hose = pci_bus_to_host(dev->bus);
	if (!hose) {
		printk(KERN_ERR "Can't find hose for bus %d\n",
		       dev->bus->number);
		return;
	}

	/* Clear out any of the virtual P2P bridge registers */
	pci_write_config_word(dev, PCI_IO_BASE_UPPER16, 0);
	pci_write_config_word(dev, PCI_IO_LIMIT_UPPER16, 0);
	pci_write_config_byte(dev, PCI_IO_BASE, 0x10);
	pci_write_config_byte(dev, PCI_IO_LIMIT, 0);
	pci_write_config_word(dev, PCI_MEMORY_BASE, 0x10);
	pci_write_config_word(dev, PCI_MEMORY_LIMIT, 0);
	pci_write_config_word(dev, PCI_PREF_BASE_UPPER32, 0x0);
	pci_write_config_word(dev, PCI_PREF_LIMIT_UPPER32, 0x0);
	pci_write_config_word(dev, PCI_PREF_MEMORY_BASE, 0x10);
	pci_write_config_word(dev, PCI_PREF_MEMORY_LIMIT, 0);

	if (hose->io_resource.flags) {
		res = &dev->resource[res_idx++];
		res->start = hose->io_resource.start;
		res->end = hose->io_resource.end;
		res->flags = hose->io_resource.flags;
		update_bridge_resource(dev, res);
	}

	for (i = 0; i < 3; i++) {
		res = &dev->resource[res_idx + i];
		res->start = hose->mem_resources[i].start;
		res->end = hose->mem_resources[i].end;
		res->flags = hose->mem_resources[i].flags;
		update_bridge_resource(dev, res);
	}
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

	/* deal with bogus pci_bus when we don't have anything connected on PCIe */
	if (hose->indirect_type & PPC_INDIRECT_TYPE_NO_PCIE_LINK) {
		if (bus->parent) {
			for (i = 0; i < 4; ++i)
				bus->resource[i] = bus->parent->resource[i];
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

	pci_assign_all_buses = 1;
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

	printk(KERN_INFO "Found FSL PCI host bridge at 0x%016llx."
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

	return 0;
}

DECLARE_PCI_FIXUP_EARLY(0x1957, PCI_DEVICE_ID_MPC8548E, quirk_fsl_pcie_transparent);
DECLARE_PCI_FIXUP_EARLY(0x1957, PCI_DEVICE_ID_MPC8548, quirk_fsl_pcie_transparent);
DECLARE_PCI_FIXUP_EARLY(0x1957, PCI_DEVICE_ID_MPC8543E, quirk_fsl_pcie_transparent);
DECLARE_PCI_FIXUP_EARLY(0x1957, PCI_DEVICE_ID_MPC8543, quirk_fsl_pcie_transparent);
DECLARE_PCI_FIXUP_EARLY(0x1957, PCI_DEVICE_ID_MPC8547E, quirk_fsl_pcie_transparent);
DECLARE_PCI_FIXUP_EARLY(0x1957, PCI_DEVICE_ID_MPC8545E, quirk_fsl_pcie_transparent);
DECLARE_PCI_FIXUP_EARLY(0x1957, PCI_DEVICE_ID_MPC8545, quirk_fsl_pcie_transparent);
DECLARE_PCI_FIXUP_EARLY(0x1957, PCI_DEVICE_ID_MPC8568E, quirk_fsl_pcie_transparent);
DECLARE_PCI_FIXUP_EARLY(0x1957, PCI_DEVICE_ID_MPC8568, quirk_fsl_pcie_transparent);
DECLARE_PCI_FIXUP_EARLY(0x1957, PCI_DEVICE_ID_MPC8567E, quirk_fsl_pcie_transparent);
DECLARE_PCI_FIXUP_EARLY(0x1957, PCI_DEVICE_ID_MPC8567, quirk_fsl_pcie_transparent);
DECLARE_PCI_FIXUP_EARLY(0x1957, PCI_DEVICE_ID_MPC8533E, quirk_fsl_pcie_transparent);
DECLARE_PCI_FIXUP_EARLY(0x1957, PCI_DEVICE_ID_MPC8533, quirk_fsl_pcie_transparent);
DECLARE_PCI_FIXUP_EARLY(0x1957, PCI_DEVICE_ID_MPC8544E, quirk_fsl_pcie_transparent);
DECLARE_PCI_FIXUP_EARLY(0x1957, PCI_DEVICE_ID_MPC8544, quirk_fsl_pcie_transparent);
DECLARE_PCI_FIXUP_EARLY(0x1957, PCI_DEVICE_ID_MPC8572E, quirk_fsl_pcie_transparent);
DECLARE_PCI_FIXUP_EARLY(0x1957, PCI_DEVICE_ID_MPC8572, quirk_fsl_pcie_transparent);
DECLARE_PCI_FIXUP_EARLY(0x1957, PCI_DEVICE_ID_MPC8641, quirk_fsl_pcie_transparent);
DECLARE_PCI_FIXUP_EARLY(0x1957, PCI_DEVICE_ID_MPC8641D, quirk_fsl_pcie_transparent);
DECLARE_PCI_FIXUP_EARLY(0x1957, PCI_DEVICE_ID_MPC8610, quirk_fsl_pcie_transparent);
