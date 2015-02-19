/*
 * Support for PCI on Celleb platform.
 *
 * (C) Copyright 2006-2007 TOSHIBA CORPORATION
 *
 * This code is based on arch/powerpc/kernel/rtas_pci.c:
 *  Copyright (C) 2001 Dave Engebretsen, IBM Corporation
 *  Copyright (C) 2003 Anton Blanchard <anton@au.ibm.com>, IBM
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/pci_regs.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/ppc-pci.h>

#include "celleb_pci.h"

#define MAX_PCI_DEVICES    32
#define MAX_PCI_FUNCTIONS   8
#define MAX_PCI_BASE_ADDRS  3 /* use 64 bit address */

/* definition for fake pci configuration area for GbE, .... ,and etc. */

struct celleb_pci_resource {
	struct resource r[MAX_PCI_BASE_ADDRS];
};

struct celleb_pci_private {
	unsigned char *fake_config[MAX_PCI_DEVICES][MAX_PCI_FUNCTIONS];
	struct celleb_pci_resource *res[MAX_PCI_DEVICES][MAX_PCI_FUNCTIONS];
};

static inline u8 celleb_fake_config_readb(void *addr)
{
	u8 *p = addr;
	return *p;
}

static inline u16 celleb_fake_config_readw(void *addr)
{
	__le16 *p = addr;
	return le16_to_cpu(*p);
}

static inline u32 celleb_fake_config_readl(void *addr)
{
	__le32 *p = addr;
	return le32_to_cpu(*p);
}

static inline void celleb_fake_config_writeb(u32 val, void *addr)
{
	u8 *p = addr;
	*p = val;
}

static inline void celleb_fake_config_writew(u32 val, void *addr)
{
	__le16 val16;
	__le16 *p = addr;
	val16 = cpu_to_le16(val);
	*p = val16;
}

static inline void celleb_fake_config_writel(u32 val, void *addr)
{
	__le32 val32;
	__le32 *p = addr;
	val32 = cpu_to_le32(val);
	*p = val32;
}

static unsigned char *get_fake_config_start(struct pci_controller *hose,
					    int devno, int fn)
{
	struct celleb_pci_private *private = hose->private_data;

	if (private == NULL)
		return NULL;

	return private->fake_config[devno][fn];
}

static struct celleb_pci_resource *get_resource_start(
				struct pci_controller *hose,
				int devno, int fn)
{
	struct celleb_pci_private *private = hose->private_data;

	if (private == NULL)
		return NULL;

	return private->res[devno][fn];
}


static void celleb_config_read_fake(unsigned char *config, int where,
				    int size, u32 *val)
{
	char *p = config + where;

	switch (size) {
	case 1:
		*val = celleb_fake_config_readb(p);
		break;
	case 2:
		*val = celleb_fake_config_readw(p);
		break;
	case 4:
		*val = celleb_fake_config_readl(p);
		break;
	}
}

static void celleb_config_write_fake(unsigned char *config, int where,
				     int size, u32 val)
{
	char *p = config + where;

	switch (size) {
	case 1:
		celleb_fake_config_writeb(val, p);
		break;
	case 2:
		celleb_fake_config_writew(val, p);
		break;
	case 4:
		celleb_fake_config_writel(val, p);
		break;
	}
}

static int celleb_fake_pci_read_config(struct pci_bus *bus,
		unsigned int devfn, int where, int size, u32 *val)
{
	char *config;
	struct pci_controller *hose = pci_bus_to_host(bus);
	unsigned int devno = devfn >> 3;
	unsigned int fn = devfn & 0x7;

	/* allignment check */
	BUG_ON(where % size);

	pr_debug("    fake read: bus=0x%x, ", bus->number);
	config = get_fake_config_start(hose, devno, fn);

	pr_debug("devno=0x%x, where=0x%x, size=0x%x, ", devno, where, size);
	if (!config) {
		pr_debug("failed\n");
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	celleb_config_read_fake(config, where, size, val);
	pr_debug("val=0x%x\n", *val);

	return PCIBIOS_SUCCESSFUL;
}


static int celleb_fake_pci_write_config(struct pci_bus *bus,
		unsigned int devfn, int where, int size, u32 val)
{
	char *config;
	struct pci_controller *hose = pci_bus_to_host(bus);
	struct celleb_pci_resource *res;
	unsigned int devno = devfn >> 3;
	unsigned int fn = devfn & 0x7;

	/* allignment check */
	BUG_ON(where % size);

	config = get_fake_config_start(hose, devno, fn);

	if (!config)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (val == ~0) {
		int i = (where - PCI_BASE_ADDRESS_0) >> 3;

		switch (where) {
		case PCI_BASE_ADDRESS_0:
		case PCI_BASE_ADDRESS_2:
			if (size != 4)
				return PCIBIOS_DEVICE_NOT_FOUND;
			res = get_resource_start(hose, devno, fn);
			if (!res)
				return PCIBIOS_DEVICE_NOT_FOUND;
			celleb_config_write_fake(config, where, size,
					(res->r[i].end - res->r[i].start));
			return PCIBIOS_SUCCESSFUL;
		case PCI_BASE_ADDRESS_1:
		case PCI_BASE_ADDRESS_3:
		case PCI_BASE_ADDRESS_4:
		case PCI_BASE_ADDRESS_5:
			break;
		default:
			break;
		}
	}

	celleb_config_write_fake(config, where, size, val);
	pr_debug("    fake write: where=%x, size=%d, val=%x\n",
		 where, size, val);

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops celleb_fake_pci_ops = {
	.read = celleb_fake_pci_read_config,
	.write = celleb_fake_pci_write_config,
};

static inline void celleb_setup_pci_base_addrs(struct pci_controller *hose,
					unsigned int devno, unsigned int fn,
					unsigned int num_base_addr)
{
	u32 val;
	unsigned char *config;
	struct celleb_pci_resource *res;

	config = get_fake_config_start(hose, devno, fn);
	res = get_resource_start(hose, devno, fn);

	if (!config || !res)
		return;

	switch (num_base_addr) {
	case 3:
		val = (res->r[2].start & 0xfffffff0)
		    | PCI_BASE_ADDRESS_MEM_TYPE_64;
		celleb_config_write_fake(config, PCI_BASE_ADDRESS_4, 4, val);
		val = res->r[2].start >> 32;
		celleb_config_write_fake(config, PCI_BASE_ADDRESS_5, 4, val);
		/* FALLTHROUGH */
	case 2:
		val = (res->r[1].start & 0xfffffff0)
		    | PCI_BASE_ADDRESS_MEM_TYPE_64;
		celleb_config_write_fake(config, PCI_BASE_ADDRESS_2, 4, val);
		val = res->r[1].start >> 32;
		celleb_config_write_fake(config, PCI_BASE_ADDRESS_3, 4, val);
		/* FALLTHROUGH */
	case 1:
		val = (res->r[0].start & 0xfffffff0)
		    | PCI_BASE_ADDRESS_MEM_TYPE_64;
		celleb_config_write_fake(config, PCI_BASE_ADDRESS_0, 4, val);
		val = res->r[0].start >> 32;
		celleb_config_write_fake(config, PCI_BASE_ADDRESS_1, 4, val);
		break;
	}

	val = PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
	celleb_config_write_fake(config, PCI_COMMAND, 2, val);
}

static int __init celleb_setup_fake_pci_device(struct device_node *node,
					       struct pci_controller *hose)
{
	unsigned int rlen;
	int num_base_addr = 0;
	u32 val;
	const u32 *wi0, *wi1, *wi2, *wi3, *wi4;
	unsigned int devno, fn;
	struct celleb_pci_private *private = hose->private_data;
	unsigned char **config = NULL;
	struct celleb_pci_resource **res = NULL;
	const char *name;
	const unsigned long *li;
	int size, result;

	if (private == NULL) {
		printk(KERN_ERR "PCI: "
		       "memory space for pci controller is not assigned\n");
		goto error;
	}

	name = of_get_property(node, "model", &rlen);
	if (!name) {
		printk(KERN_ERR "PCI: model property not found.\n");
		goto error;
	}

	wi4 = of_get_property(node, "reg", &rlen);
	if (wi4 == NULL)
		goto error;

	devno = ((wi4[0] >> 8) & 0xff) >> 3;
	fn = (wi4[0] >> 8) & 0x7;

	pr_debug("PCI: celleb_setup_fake_pci() %s devno=%x fn=%x\n", name,
		 devno, fn);

	size = 256;
	config = &private->fake_config[devno][fn];
	*config = zalloc_maybe_bootmem(size, GFP_KERNEL);
	if (*config == NULL) {
		printk(KERN_ERR "PCI: "
		       "not enough memory for fake configuration space\n");
		goto error;
	}
	pr_debug("PCI: fake config area assigned 0x%016lx\n",
		 (unsigned long)*config);

	size = sizeof(struct celleb_pci_resource);
	res = &private->res[devno][fn];
	*res = zalloc_maybe_bootmem(size, GFP_KERNEL);
	if (*res == NULL) {
		printk(KERN_ERR
		       "PCI: not enough memory for resource data space\n");
		goto error;
	}
	pr_debug("PCI: res assigned 0x%016lx\n", (unsigned long)*res);

	wi0 = of_get_property(node, "device-id", NULL);
	wi1 = of_get_property(node, "vendor-id", NULL);
	wi2 = of_get_property(node, "class-code", NULL);
	wi3 = of_get_property(node, "revision-id", NULL);
	if (!wi0 || !wi1 || !wi2 || !wi3) {
		printk(KERN_ERR "PCI: Missing device tree properties.\n");
		goto error;
	}

	celleb_config_write_fake(*config, PCI_DEVICE_ID, 2, wi0[0] & 0xffff);
	celleb_config_write_fake(*config, PCI_VENDOR_ID, 2, wi1[0] & 0xffff);
	pr_debug("class-code = 0x%08x\n", wi2[0]);

	celleb_config_write_fake(*config, PCI_CLASS_PROG, 1, wi2[0] & 0xff);
	celleb_config_write_fake(*config, PCI_CLASS_DEVICE, 2,
				 (wi2[0] >> 8) & 0xffff);
	celleb_config_write_fake(*config, PCI_REVISION_ID, 1, wi3[0]);

	while (num_base_addr < MAX_PCI_BASE_ADDRS) {
		result = of_address_to_resource(node,
				num_base_addr, &(*res)->r[num_base_addr]);
		if (result)
			break;
		num_base_addr++;
	}

	celleb_setup_pci_base_addrs(hose, devno, fn, num_base_addr);

	li = of_get_property(node, "interrupts", &rlen);
	if (!li) {
		printk(KERN_ERR "PCI: interrupts not found.\n");
		goto error;
	}
	val = li[0];
	celleb_config_write_fake(*config, PCI_INTERRUPT_PIN, 1, 1);
	celleb_config_write_fake(*config, PCI_INTERRUPT_LINE, 1, val);

#ifdef DEBUG
	pr_debug("PCI: %s irq=%ld\n", name, li[0]);
	for (i = 0; i < 6; i++) {
		celleb_config_read_fake(*config,
					PCI_BASE_ADDRESS_0 + 0x4 * i, 4,
					&val);
		pr_debug("PCI: %s fn=%d base_address_%d=0x%x\n",
			 name, fn, i, val);
	}
#endif

	celleb_config_write_fake(*config, PCI_HEADER_TYPE, 1,
				 PCI_HEADER_TYPE_NORMAL);

	return 0;

error:
	if (mem_init_done) {
		if (config && *config)
			kfree(*config);
		if (res && *res)
			kfree(*res);

	} else {
		if (config && *config) {
			size = 256;
			memblock_free(__pa(*config), size);
		}
		if (res && *res) {
			size = sizeof(struct celleb_pci_resource);
			memblock_free(__pa(*res), size);
		}
	}

	return 1;
}

static int __init phb_set_bus_ranges(struct device_node *dev,
				     struct pci_controller *phb)
{
	const int *bus_range;
	unsigned int len;

	bus_range = of_get_property(dev, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int))
		return 1;

	phb->first_busno = bus_range[0];
	phb->last_busno = bus_range[1];

	return 0;
}

static void __init celleb_alloc_private_mem(struct pci_controller *hose)
{
	hose->private_data =
		zalloc_maybe_bootmem(sizeof(struct celleb_pci_private),
			GFP_KERNEL);
}

static int __init celleb_setup_fake_pci(struct device_node *dev,
					struct pci_controller *phb)
{
	struct device_node *node;

	phb->ops = &celleb_fake_pci_ops;
	celleb_alloc_private_mem(phb);

	for (node = of_get_next_child(dev, NULL);
	     node != NULL; node = of_get_next_child(dev, node))
		celleb_setup_fake_pci_device(node, phb);

	return 0;
}

static struct celleb_phb_spec celleb_fake_pci_spec __initdata = {
	.setup = celleb_setup_fake_pci,
};

static const struct of_device_id celleb_phb_match[] __initconst = {
	{
		.name = "pci-pseudo",
		.data = &celleb_fake_pci_spec,
	}, {
		.name = "epci",
		.data = &celleb_epci_spec,
	}, {
		.name = "pcie",
		.data = &celleb_pciex_spec,
	}, {
	},
};

int __init celleb_setup_phb(struct pci_controller *phb)
{
	struct device_node *dev = phb->dn;
	const struct of_device_id *match;
	const struct celleb_phb_spec *phb_spec;
	int rc;

	match = of_match_node(celleb_phb_match, dev);
	if (!match)
		return 1;

	phb_set_bus_ranges(dev, phb);
	phb->buid = 1;

	phb_spec = match->data;
	rc = (*phb_spec->setup)(dev, phb);
	if (rc)
		return 1;

	if (phb_spec->ops)
		iowa_register_bus(phb, phb_spec->ops,
				  phb_spec->iowa_init,
				  phb_spec->iowa_data);
	return 0;
}

int celleb_pci_probe_mode(struct pci_bus *bus)
{
	return PCI_PROBE_DEVTREE;
}
