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


void pci_update_resource(struct pci_dev *dev, struct resource *res, int resno)
{
	struct pci_bus_region region;
	u32 new, check, mask;
	int reg;

	/*
	 * Ignore resources for unimplemented BARs and unused resource slots
	 * for 64 bit BARs.
	 */
	if (!res->flags)
		return;

	/*
	 * Ignore non-moveable resources.  This might be legacy resources for
	 * which no functional BAR register exists or another important
	 * system resource we shouldn't move around.
	 */
	if (res->flags & IORESOURCE_PCI_FIXED)
		return;

	pcibios_resource_to_bus(dev, &region, res);

	dev_dbg(&dev->dev, "BAR %d: got res [%#llx-%#llx] bus [%#llx-%#llx] "
		"flags %#lx\n", resno,
		 (unsigned long long)res->start,
		 (unsigned long long)res->end,
		 (unsigned long long)region.start,
		 (unsigned long long)region.end,
		 (unsigned long)res->flags);

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
		dev_err(&dev->dev, "BAR %d: error updating (%#08x != %#08x)\n",
			resno, new, check);
	}

	if ((new & (PCI_BASE_ADDRESS_SPACE|PCI_BASE_ADDRESS_MEM_TYPE_MASK)) ==
	    (PCI_BASE_ADDRESS_SPACE_MEMORY|PCI_BASE_ADDRESS_MEM_TYPE_64)) {
		new = region.start >> 16 >> 16;
		pci_write_config_dword(dev, reg + 4, new);
		pci_read_config_dword(dev, reg + 4, &check);
		if (check != new) {
			dev_err(&dev->dev, "BAR %d: error updating "
			       "(high %#08x != %#08x)\n", resno, new, check);
		}
	}
	res->flags &= ~IORESOURCE_UNSET;
	dev_dbg(&dev->dev, "BAR %d: moved to bus [%#llx-%#llx] flags %#lx\n",
		resno, (unsigned long long)region.start,
		(unsigned long long)region.end, res->flags);
}

int pci_claim_resource(struct pci_dev *dev, int resource)
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
		dev_err(&dev->dev, "BAR %d: %s of %s [%#llx-%#llx]\n",
			resource,
			root ? "address space collision on" :
				"no parent found for",
			dtype,
			(unsigned long long)res->start,
			(unsigned long long)res->end);
	}

	return err;
}

int pci_assign_resource(struct pci_dev *dev, int resno)
{
	struct pci_bus *bus = dev->bus;
	struct resource *res = dev->resource + resno;
	resource_size_t size, min, align;
	int ret;

	size = res->end - res->start + 1;
	min = (res->flags & IORESOURCE_IO) ? PCIBIOS_MIN_IO : PCIBIOS_MIN_MEM;

	align = resource_alignment(res);
	if (!align) {
		dev_err(&dev->dev, "BAR %d: can't allocate resource (bogus "
			"alignment) [%#llx-%#llx] flags %#lx\n",
			resno, (unsigned long long)res->start,
			(unsigned long long)res->end, res->flags);
		return -EINVAL;
	}

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
		dev_err(&dev->dev, "BAR %d: can't allocate %s resource "
			"[%#llx-%#llx]\n", resno,
			res->flags & IORESOURCE_IO ? "I/O" : "mem",
			(unsigned long long)res->start,
			(unsigned long long)res->end);
	} else {
		res->flags &= ~IORESOURCE_STARTALIGN;
		if (resno < PCI_BRIDGE_RESOURCES)
			pci_update_resource(dev, res, resno);
	}

	return ret;
}

#if 0
int pci_assign_resource_fixed(struct pci_dev *dev, int resno)
{
	struct pci_bus *bus = dev->bus;
	struct resource *res = dev->resource + resno;
	unsigned int type_mask;
	int i, ret = -EBUSY;

	type_mask = IORESOURCE_IO | IORESOURCE_MEM | IORESOURCE_PREFETCH;

	for (i = 0; i < PCI_BUS_NUM_RESOURCES; i++) {
		struct resource *r = bus->resource[i];
		if (!r)
			continue;

		/* type_mask must match */
		if ((res->flags ^ r->flags) & type_mask)
			continue;

		ret = request_resource(r, res);

		if (ret == 0)
			break;
	}

	if (ret) {
		dev_err(&dev->dev, "BAR %d: can't allocate %s resource "
			"[%#llx-%#llx\n]", resno,
			res->flags & IORESOURCE_IO ? "I/O" : "mem",
			(unsigned long long)res->start,
			(unsigned long long)res->end);
	} else if (resno < PCI_BRIDGE_RESOURCES) {
		pci_update_resource(dev, res, resno);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(pci_assign_resource_fixed);
#endif

/* Sort resources by alignment */
void pdev_sort_resources(struct pci_dev *dev, struct resource_list *head)
{
	int i;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *r;
		struct resource_list *list, *tmp;
		resource_size_t r_align;

		r = &dev->resource[i];

		if (r->flags & IORESOURCE_PCI_FIXED)
			continue;

		if (!(r->flags) || r->parent)
			continue;

		r_align = resource_alignment(r);
		if (!r_align) {
			dev_warn(&dev->dev, "BAR %d: bogus alignment "
				"[%#llx-%#llx] flags %#lx\n",
				i, (unsigned long long)r->start,
				(unsigned long long)r->end, r->flags);
			continue;
		}
		for (list = head; ; list = list->next) {
			resource_size_t align = 0;
			struct resource_list *ln = list->next;

			if (ln)
				align = resource_alignment(ln->res);

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

int pci_enable_resources(struct pci_dev *dev, int mask)
{
	u16 cmd, old_cmd;
	int i;
	struct resource *r;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		if (!(mask & (1 << i)))
			continue;

		r = &dev->resource[i];

		if (!(r->flags & (IORESOURCE_IO | IORESOURCE_MEM)))
			continue;
		if ((i == PCI_ROM_RESOURCE) &&
				(!(r->flags & IORESOURCE_ROM_ENABLE)))
			continue;

		if (!r->parent) {
			dev_err(&dev->dev, "device not available because of "
				"BAR %d [%#llx-%#llx] collisions\n", i,
				(unsigned long long) r->start,
				(unsigned long long) r->end);
			return -EINVAL;
		}

		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}

	if (cmd != old_cmd) {
		dev_info(&dev->dev, "enabling device (%04x -> %04x)\n",
			 old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}
