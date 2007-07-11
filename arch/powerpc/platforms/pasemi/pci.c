/*
 * Copyright (C) 2006 PA Semi, Inc
 *
 * Authors: Kip Walker, PA Semi
 *	    Olof Johansson, PA Semi
 *
 * Maintained by: Olof Johansson <olof@lixom.net>
 *
 * Based on arch/powerpc/platforms/maple/pci.c
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


#include <linux/kernel.h>
#include <linux/pci.h>

#include <asm/pci-bridge.h>
#include <asm/machdep.h>

#include <asm/ppc-pci.h>

#define PA_PXP_CFA(bus, devfn, off) (((bus) << 20) | ((devfn) << 12) | (off))

static inline int pa_pxp_offset_valid(u8 bus, u8 devfn, int offset)
{
	/* Device 0 Function 0 is special: It's config space spans function 1 as
	 * well, so allow larger offset. It's really a two-function device but the
	 * second function does not probe.
	 */
	if (bus == 0 && devfn == 0)
		return offset < 8192;
	else
		return offset < 4096;
}

static void volatile __iomem *pa_pxp_cfg_addr(struct pci_controller *hose,
				       u8 bus, u8 devfn, int offset)
{
	return hose->cfg_data + PA_PXP_CFA(bus, devfn, offset);
}

static int pa_pxp_read_config(struct pci_bus *bus, unsigned int devfn,
			      int offset, int len, u32 *val)
{
	struct pci_controller *hose;
	void volatile __iomem *addr;

	hose = pci_bus_to_host(bus);
	if (!hose)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (!pa_pxp_offset_valid(bus->number, devfn, offset))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	addr = pa_pxp_cfg_addr(hose, bus->number, devfn, offset);

	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	switch (len) {
	case 1:
		*val = in_8(addr);
		break;
	case 2:
		*val = in_le16(addr);
		break;
	default:
		*val = in_le32(addr);
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int pa_pxp_write_config(struct pci_bus *bus, unsigned int devfn,
			       int offset, int len, u32 val)
{
	struct pci_controller *hose;
	void volatile __iomem *addr;

	hose = pci_bus_to_host(bus);
	if (!hose)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (!pa_pxp_offset_valid(bus->number, devfn, offset))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	addr = pa_pxp_cfg_addr(hose, bus->number, devfn, offset);

	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	switch (len) {
	case 1:
		out_8(addr, val);
		(void) in_8(addr);
		break;
	case 2:
		out_le16(addr, val);
		(void) in_le16(addr);
		break;
	default:
		out_le32(addr, val);
		(void) in_le32(addr);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops pa_pxp_ops = {
	pa_pxp_read_config,
	pa_pxp_write_config,
};

static void __init setup_pa_pxp(struct pci_controller *hose)
{
	hose->ops = &pa_pxp_ops;
	hose->cfg_data = ioremap(0xe0000000, 0x10000000);
}

static int __init pas_add_bridge(struct device_node *dev)
{
	struct pci_controller *hose;

	pr_debug("Adding PCI host bridge %s\n", dev->full_name);

	hose = pcibios_alloc_controller(dev);
	if (!hose)
		return -ENOMEM;

	hose->first_busno = 0;
	hose->last_busno = 0xff;

	setup_pa_pxp(hose);

	printk(KERN_INFO "Found PA-PXP PCI host bridge.\n");

	/* Interpret the "ranges" property */
	pci_process_bridge_OF_ranges(hose, dev, 1);

	return 0;
}

void __init pas_pci_init(void)
{
	struct device_node *np, *root;

	root = of_find_node_by_path("/");
	if (!root) {
		printk(KERN_CRIT "pas_pci_init: can't find root "
			"of device tree\n");
		return;
	}

	for (np = NULL; (np = of_get_next_child(root, np)) != NULL;)
		if (np->name && !strcmp(np->name, "pxp") && !pas_add_bridge(np))
			of_node_get(np);

	of_node_put(root);

	/* Setup the linkage between OF nodes and PHBs */
	pci_devs_phb_init();

	/* Use the common resource allocation mechanism */
	pci_probe_only = 1;
}
