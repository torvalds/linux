/*
 *	drivers/pci/setup-res.c
 *
 * Extruded from code written by
 *      Dave Rusling (david.rusling@reo.mts.dec.com)
 *      David Mosberger (davidm@cs.arizona.edu)
 *	David Miller (davem@redhat.com)
 *
 * Support routines for initializing a PCI subsystem.
 */

/* fixed for multiple pci buses, 1999 Andrea Arcangeli <andrea@suse.de> */

/*
 * Nov 2000, Ivan Kokshaysky <ink@jurassic.park.msu.ru>
 *	     Resource sorting
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/cache.h>
#include <linux/slab.h>
#include "pci.h"


void
pci_update_resource(struct pci_dev *dev, struct resource *res, int resno)
{
	struct pci_bus_region region;
	u32 new, check, mask;
	int reg;

	/* Ignore resources for unimplemented BARs and unused resource slots
	   for 64 bit BARs. */
	if (!res->flags)
		return;

	pcibios_resource_to_bus(dev, &region, res);

	pr_debug("  got res [%lx:%lx] bus [%lx:%lx] flags %lx for "
		 "BAR %d of %s\n", res->start, res->end,
		 region.start, region.end, res->flags, resno, pci_name(dev));

	new = region.start | (res->flags & PCI_REGION_FLAG_MASK);
	if (res->flags & IORESOURCE_IO)
		mask = (u32)PCI_BASE_ADDRESS_IO_MASK;
	else
		mask = (u32)PCI_BASE_ADDRESS_MEM_MASK;

	if (resno < 6) {
		reg = PCI_BASE_ADDRESS_0 + 4 * resno;
	} else if (resno == PCI_ROM_RESOURCE) {
		if (!(res->flags & IORESOURCE_ROM_ENABLE))
			return;
		new |= PCI_ROM_ADDRESS_ENABLE;
		reg = dev->rom_base_reg;
	} else {
		/* Hmm, non-standard resource. */
	
		return;		/* kill uninitialised var warning */
	}

	pci_write_config_dword(dev, reg, new);
	pci_read_config_dword(dev, reg, &check);

	if ((new ^ check) & mask) {
		printk(KERN_ERR "PCI: Error while updating region "
		       "%s/%d (%08x != %08x)\n", pci_name(dev), resno,
		       new, check);
	}

	if ((new & (PCI_BASE_ADDRESS_SPACE|PCI_BASE_ADDRESS_MEM_TYPE_MASK)) ==
	    (PCI_BASE_ADDRESS_SPACE_MEMORY|PCI_BASE_ADDRESS_MEM_TYPE_64)) {
		new = region.start >> 16 >> 16;
		pci_write_config_dword(dev, reg + 4, new);
		pci_read_config_dword(dev, reg + 4, &check);
		if (check != new) {
			printk(KERN_ERR "PCI: Error updating region "
			       "%s/%d (high %08x != %08x)\n",
			       pci_name(dev), resno, new, check);
		}
	}
	res->flags &= ~IORESOURCE_UNSET;
	pr_debug("PCI: moved device %s resource %d (%lx) to %x\n",
		pci_name(dev), resno, res->flags,
		new & ~PCI_REGION_FLAG_MASK);
}

int __devinit
pci_claim_resource(struct pci_dev *dev, int resource)
{
	struct resource *res = &dev->resource[resource];
	struct resource *root = NULL;
	char *dtype = resource < PCI_BRIDGE_RESOURCES ? "device" : "bridge";
	int err;

	root = pcibios_select_root(dev, res);

	err = -EINVAL;
	if (root != NULL)
		err = insert_resource(root, res);

	if (err) {
		printk(KERN_ERR "PCI: %s region %d of %s %s [%lx:%lx]\n",
		       root ? "Address space collision on" :
			      "No parent found for",
		       resource, dtype, pci_name(dev), res->start, res->end);
	}

	return err;
}

int pci_assign_resource(struct pci_dev *dev, int resno)
{
	struct pci_bus *bus = dev->bus;
	struct resource *res = dev->resource + resno;
	unsigned long size, min, align;
	int ret;

	size = res->end - res->start + 1;
	min = (res->flags & IORESOURCE_IO) ? PCIBIOS_MIN_IO : PCIBIOS_MIN_MEM;
	/* The bridge resources are special, as their
	   size != alignment. Sizing routines return
	   required alignment in the "start" field. */
	align = (resno < PCI_BRIDGE_RESOURCES) ? size : res->start;

	/* First, try exact prefetching match.. */
	ret = pci_bus_alloc_resource(bus, res, size, align, min,
				     IORESOURCE_PREFETCH,
				     pcibios_align_resource, dev);

	if (ret < 0 && (res->flags & IORESOURCE_PREFETCH)) {
		/*
		 * That failed.
		 *
		 * But a prefetching area can handle a non-prefetching
		 * window (it will just not perform as well).
		 */
		ret = pci_bus_alloc_resource(bus, res, size, align, min, 0,
					     pcibios_align_resource, dev);
	}

	if (ret) {
		printk(KERN_ERR "PCI: Failed to allocate %s resource #%d:%lx@%lx for %s\n",
		       res->flags & IORESOURCE_IO ? "I/O" : "mem",
		       resno, size, res->start, pci_name(dev));
	} else if (resno < PCI_BRIDGE_RESOURCES) {
		pci_update_resource(dev, res, resno);
	}

	return ret;
}

/* Sort resources by alignment */
void __devinit
pdev_sort_resources(struct pci_dev *dev, struct resource_list *head)
{
	int i;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *r;
		struct resource_list *list, *tmp;
		unsigned long r_align;

		r = &dev->resource[i];
		r_align = r->end - r->start;
		
		if (!(r->flags) || r->parent)
			continue;
		if (!r_align) {
			printk(KERN_WARNING "PCI: Ignore bogus resource %d "
					    "[%lx:%lx] of %s\n",
					    i, r->start, r->end, pci_name(dev));
			continue;
		}
		r_align = (i < PCI_BRIDGE_RESOURCES) ? r_align + 1 : r->start;
		for (list = head; ; list = list->next) {
			unsigned long align = 0;
			struct resource_list *ln = list->next;
			int idx;

			if (ln) {
				idx = ln->res - &ln->dev->resource[0];
				align = (idx < PCI_BRIDGE_RESOURCES) ?
					ln->res->end - ln->res->start + 1 :
					ln->res->start;
			}
			if (r_align > align) {
				tmp = kmalloc(sizeof(*tmp), GFP_KERNEL);
				if (!tmp)
					panic("pdev_sort_resources(): "
					      "kmalloc() failed!\n");
				tmp->next = ln;
				tmp->res = r;
				tmp->dev = dev;
				list->next = tmp;
				break;
			}
		}
	}
}
