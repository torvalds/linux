/*
 * Copyright (C) 2001 Dave Engebretsen, IBM Corporation
 * Copyright (C) 2003 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * RTAS specific routines for PCI.
 *
 * Based on code from pci.c, chrp_pci.c and pSeries_pci.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
#include <linux/threads.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/iommu.h>
#include <asm/rtas.h>
#include <asm/mpic.h>
#include <asm/ppc-pci.h>

/* RTAS tokens */
static int read_pci_config;
static int write_pci_config;
static int ibm_read_pci_config;
static int ibm_write_pci_config;

static inline int config_access_valid(struct pci_dn *dn, int where)
{
	if (where < 256)
		return 1;
	if (where < 4096 && dn->pci_ext_config_space)
		return 1;

	return 0;
}

static int of_device_available(struct device_node * dn)
{
        char * status;

        status = get_property(dn, "status", NULL);

        if (!status)
                return 1;

        if (!strcmp(status, "okay"))
                return 1;

        return 0;
}

int rtas_read_config(struct pci_dn *pdn, int where, int size, u32 *val)
{
	int returnval = -1;
	unsigned long buid, addr;
	int ret;

	if (!pdn)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (!config_access_valid(pdn, where))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	addr = ((where & 0xf00) << 20) | (pdn->busno << 16) |
		(pdn->devfn << 8) | (where & 0xff);
	buid = pdn->phb->buid;
	if (buid) {
		ret = rtas_call(ibm_read_pci_config, 4, 2, &returnval,
				addr, BUID_HI(buid), BUID_LO(buid), size);
	} else {
		ret = rtas_call(read_pci_config, 2, 2, &returnval, addr, size);
	}
	*val = returnval;

	if (ret)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (returnval == EEH_IO_ERROR_VALUE(size) &&
	    eeh_dn_check_failure (pdn->node, NULL))
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

static int rtas_pci_read_config(struct pci_bus *bus,
				unsigned int devfn,
				int where, int size, u32 *val)
{
	struct device_node *busdn, *dn;

	if (bus->self)
		busdn = pci_device_to_OF_node(bus->self);
	else
		busdn = bus->sysdata;	/* must be a phb */

	/* Search only direct children of the bus */
	for (dn = busdn->child; dn; dn = dn->sibling) {
		struct pci_dn *pdn = PCI_DN(dn);
		if (pdn && pdn->devfn == devfn
		    && of_device_available(dn))
			return rtas_read_config(pdn, where, size, val);
	}

	return PCIBIOS_DEVICE_NOT_FOUND;
}

int rtas_write_config(struct pci_dn *pdn, int where, int size, u32 val)
{
	unsigned long buid, addr;
	int ret;

	if (!pdn)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (!config_access_valid(pdn, where))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	addr = ((where & 0xf00) << 20) | (pdn->busno << 16) |
		(pdn->devfn << 8) | (where & 0xff);
	buid = pdn->phb->buid;
	if (buid) {
		ret = rtas_call(ibm_write_pci_config, 5, 1, NULL, addr,
			BUID_HI(buid), BUID_LO(buid), size, (ulong) val);
	} else {
		ret = rtas_call(write_pci_config, 3, 1, NULL, addr, size, (ulong)val);
	}

	if (ret)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

static int rtas_pci_write_config(struct pci_bus *bus,
				 unsigned int devfn,
				 int where, int size, u32 val)
{
	struct device_node *busdn, *dn;

	if (bus->self)
		busdn = pci_device_to_OF_node(bus->self);
	else
		busdn = bus->sysdata;	/* must be a phb */

	/* Search only direct children of the bus */
	for (dn = busdn->child; dn; dn = dn->sibling) {
		struct pci_dn *pdn = PCI_DN(dn);
		if (pdn && pdn->devfn == devfn
		    && of_device_available(dn))
			return rtas_write_config(pdn, where, size, val);
	}
	return PCIBIOS_DEVICE_NOT_FOUND;
}

struct pci_ops rtas_pci_ops = {
	rtas_pci_read_config,
	rtas_pci_write_config
};

int is_python(struct device_node *dev)
{
	char *model = (char *)get_property(dev, "model", NULL);

	if (model && strstr(model, "Python"))
		return 1;

	return 0;
}

static void python_countermeasures(struct device_node *dev)
{
	struct resource registers;
	void __iomem *chip_regs;
	volatile u32 val;

	if (of_address_to_resource(dev, 0, &registers)) {
		printk(KERN_ERR "Can't get address for Python workarounds !\n");
		return;
	}

	/* Python's register file is 1 MB in size. */
	chip_regs = ioremap(registers.start & ~(0xfffffUL), 0x100000);

	/*
	 * Firmware doesn't always clear this bit which is critical
	 * for good performance - Anton
	 */

#define PRG_CL_RESET_VALID 0x00010000

	val = in_be32(chip_regs + 0xf6030);
	if (val & PRG_CL_RESET_VALID) {
		printk(KERN_INFO "Python workaround: ");
		val &= ~PRG_CL_RESET_VALID;
		out_be32(chip_regs + 0xf6030, val);
		/*
		 * We must read it back for changes to
		 * take effect
		 */
		val = in_be32(chip_regs + 0xf6030);
		printk("reg0: %x\n", val);
	}

	iounmap(chip_regs);
}

void __init init_pci_config_tokens (void)
{
	read_pci_config = rtas_token("read-pci-config");
	write_pci_config = rtas_token("write-pci-config");
	ibm_read_pci_config = rtas_token("ibm,read-pci-config");
	ibm_write_pci_config = rtas_token("ibm,write-pci-config");
}

unsigned long __devinit get_phb_buid (struct device_node *phb)
{
	int addr_cells;
	unsigned int *buid_vals;
	unsigned int len;
	unsigned long buid;

	if (ibm_read_pci_config == -1) return 0;

	/* PHB's will always be children of the root node,
	 * or so it is promised by the current firmware. */
	if (phb->parent == NULL)
		return 0;
	if (phb->parent->parent)
		return 0;

	buid_vals = (unsigned int *) get_property(phb, "reg", &len);
	if (buid_vals == NULL)
		return 0;

	addr_cells = prom_n_addr_cells(phb);
	if (addr_cells == 1) {
		buid = (unsigned long) buid_vals[0];
	} else {
		buid = (((unsigned long)buid_vals[0]) << 32UL) |
			(((unsigned long)buid_vals[1]) & 0xffffffff);
	}
	return buid;
}

static int phb_set_bus_ranges(struct device_node *dev,
			      struct pci_controller *phb)
{
	int *bus_range;
	unsigned int len;

	bus_range = (int *) get_property(dev, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int)) {
		return 1;
 	}

	phb->first_busno =  bus_range[0];
	phb->last_busno  =  bus_range[1];

	return 0;
}

int __devinit setup_phb(struct device_node *dev, struct pci_controller *phb)
{
	if (is_python(dev))
		python_countermeasures(dev);

	if (phb_set_bus_ranges(dev, phb))
		return 1;

	phb->ops = &rtas_pci_ops;
	phb->buid = get_phb_buid(dev);

	return 0;
}

unsigned long __init find_and_init_phbs(void)
{
	struct device_node *node;
	struct pci_controller *phb;
	unsigned int index;
	unsigned int root_size_cells = 0;
	unsigned int *opprop = NULL;
	struct device_node *root = of_find_node_by_path("/");

	if (ppc64_interrupt_controller == IC_OPEN_PIC) {
		opprop = (unsigned int *)get_property(root,
				"platform-open-pic", NULL);
	}

	root_size_cells = prom_n_size_cells(root);

	index = 0;

	for (node = of_get_next_child(root, NULL);
	     node != NULL;
	     node = of_get_next_child(root, node)) {
		if (node->type == NULL || strcmp(node->type, "pci") != 0)
			continue;

		phb = pcibios_alloc_controller(node);
		if (!phb)
			continue;
		setup_phb(node, phb);
		pci_process_bridge_OF_ranges(phb, node, 0);
		pci_setup_phb_io(phb, index == 0);
#ifdef CONFIG_PPC_PSERIES
		/* XXX This code need serious fixing ... --BenH */
		if (ppc64_interrupt_controller == IC_OPEN_PIC && pSeries_mpic) {
			int addr = root_size_cells * (index + 2) - 1;
			mpic_assign_isu(pSeries_mpic, index, opprop[addr]);
		}
#endif
		index++;
	}

	of_node_put(root);
	pci_devs_phb_init();

	/*
	 * pci_probe_only and pci_assign_all_buses can be set via properties
	 * in chosen.
	 */
	if (of_chosen) {
		int *prop;

		prop = (int *)get_property(of_chosen, "linux,pci-probe-only",
					   NULL);
		if (prop)
			pci_probe_only = *prop;

		prop = (int *)get_property(of_chosen,
					   "linux,pci-assign-all-buses", NULL);
		if (prop)
			pci_assign_all_buses = *prop;
	}

	return 0;
}

/* RPA-specific bits for removing PHBs */
int pcibios_remove_root_bus(struct pci_controller *phb)
{
	struct pci_bus *b = phb->bus;
	struct resource *res;
	int rc, i;

	res = b->resource[0];
	if (!res->flags) {
		printk(KERN_ERR "%s: no IO resource for PHB %s\n", __FUNCTION__,
				b->name);
		return 1;
	}

	rc = unmap_bus_range(b);
	if (rc) {
		printk(KERN_ERR "%s: failed to unmap IO on bus %s\n",
			__FUNCTION__, b->name);
		return 1;
	}

	if (release_resource(res)) {
		printk(KERN_ERR "%s: failed to release IO on bus %s\n",
				__FUNCTION__, b->name);
		return 1;
	}

	for (i = 1; i < 3; ++i) {
		res = b->resource[i];
		if (!res->flags && i == 0) {
			printk(KERN_ERR "%s: no MEM resource for PHB %s\n",
				__FUNCTION__, b->name);
			return 1;
		}
		if (res->flags && release_resource(res)) {
			printk(KERN_ERR
			       "%s: failed to release IO %d on bus %s\n",
				__FUNCTION__, i, b->name);
			return 1;
		}
	}

	list_del(&phb->list_node);
	pcibios_free_controller(phb);

	return 0;
}
EXPORT_SYMBOL(pcibios_remove_root_bus);
