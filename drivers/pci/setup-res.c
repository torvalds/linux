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
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/cache.h>
#include <linux/slab.h>
#include "pci.h"


void pci_update_resource(struct pci_dev *dev, int resno)
{
	struct pci_bus_region region;
	u32 new, check, mask;
	int reg;
	enum pci_bar_type type;
	struct resource *res = dev->resource + resno;

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

	new = region.start | (res->flags & PCI_REGION_FLAG_MASK);
	if (res->flags & IORESOURCE_IO)
		mask = (u32)PCI_BASE_ADDRESS_IO_MASK;
	else
		mask = (u32)PCI_BASE_ADDRESS_MEM_MASK;

	reg = pci_resource_bar(dev, resno, &type);
	if (!reg)
		return;
	if (type != pci_bar_unknown) {
		if (!(res->flags & IORESOURCE_ROM_ENABLE))
			return;
		new |= PCI_ROM_ADDRESS_ENABLE;
	}

	pci_write_config_dword(dev, reg, new);
	pci_read_config_dword(dev, reg, &check);

	if ((new ^ check) & mask) {
		dev_err(&dev->dev, "BAR %d: error updating (%#08x != %#08x)\n",
			resno, new, check);
	}

	if (res->flags & IORESOURCE_MEM_64) {
		new = region.start >> 16 >> 16;
		pci_write_config_dword(dev, reg + 4, new);
		pci_read_config_dword(dev, reg + 4, &check);
		if (check != new) {
			dev_err(&dev->dev, "BAR %d: error updating "
			       "(high %#08x != %#08x)\n", resno, new, check);
		}
	}
	res->flags &= ~IORESOURCE_UNSET;
	dev_info(&dev->dev, "BAR %d: set to %pR (PCI address [%#llx-%#llx])\n",
		 resno, res, (unsigned long long)region.start,
		 (unsigned long long)region.end);
}

int pci_claim_resource(struct pci_dev *dev, int resource)
{
	struct resource *res = &dev->resource[resource];
	struct resource *root, *conflict;

	root = pci_find_parent_resource(dev, res);
	if (!root) {
		dev_info(&dev->dev, "no compatible bridge window for %pR\n",
			 res);
		return -EINVAL;
	}

	conflict = request_resource_conflict(root, res);
	if (conflict) {
		dev_info(&dev->dev,
			 "address space collision: %pR conflicts with %s %pR\n",
			 res, conflict->name, conflict);
		return -EBUSY;
	}

	return 0;
}
EXPORT_SYMBOL(pci_claim_resource);

#ifdef CONFIG_PCI_QUIRKS
void pci_disable_bridge_window(struct pci_dev *dev)
{
	dev_info(&dev->dev, "disabling bridge mem windows\n");

	/* MMIO Base/Limit */
	pci_write_config_dword(dev, PCI_MEMORY_BASE, 0x0000fff0);

	/* Prefetchable MMIO Base/Limit */
	pci_write_config_dword(dev, PCI_PREF_LIMIT_UPPER32, 0);
	pci_write_config_dword(dev, PCI_PREF_MEMORY_BASE, 0x0000fff0);
	pci_write_config_dword(dev, PCI_PREF_BASE_UPPER32, 0xffffffff);
}
#endif	/* CONFIG_PCI_QUIRKS */



static int __pci_assign_resource(struct pci_bus *bus, struct pci_dev *dev,
		int resno, resource_size_t size, resource_size_t align)
{
	struct resource *res = dev->resource + resno;
	resource_size_t min;
	int ret;

	min = (res->flags & IORESOURCE_IO) ? PCIBIOS_MIN_IO : PCIBIOS_MIN_MEM;

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
	return ret;
}

static int pci_revert_fw_address(struct resource *res, struct pci_dev *dev, 
		int resno, resource_size_t size)
{
	struct resource *root, *conflict;
	resource_size_t start, end;
	int ret = 0;

	if (res->flags & IORESOURCE_IO)
		root = &ioport_resource;
	else
		root = &iomem_resource;

	start = res->start;
	end = res->end;
	res->start = dev->fw_addr[resno];
	res->end = res->start + size - 1;
	dev_info(&dev->dev, "BAR %d: trying firmware assignment %pR\n",
		 resno, res);
	conflict = request_resource_conflict(root, res);
	if (conflict) {
		dev_info(&dev->dev,
			 "BAR %d: %pR conflicts with %s %pR\n", resno,
			 res, conflict->name, conflict);
		res->start = start;
		res->end = end;
		ret = 1;
	}
	return ret;
}

static int _pci_assign_resource(struct pci_dev *dev, int resno, int size, resource_size_t min_align)
{
	struct resource *res = dev->resource + resno;
	struct pci_bus *bus;
	int ret;
	char *type;

	bus = dev->bus;
	while ((ret = __pci_assign_resource(bus, dev, resno, size, min_align))) {
		if (!bus->parent || !bus->self->transparent)
			break;
		bus = bus->parent;
	}

	if (ret) {
		if (res->flags & IORESOURCE_MEM)
			if (res->flags & IORESOURCE_PREFETCH)
				type = "mem pref";
			else
				type = "mem";
		else if (res->flags & IORESOURCE_IO)
			type = "io";
		else
			type = "unknown";
		dev_info(&dev->dev,
			 "BAR %d: can't assign %s (size %#llx)\n",
			 resno, type, (unsigned long long) resource_size(res));
	}

	return ret;
}

int pci_reassign_resource(struct pci_dev *dev, int resno, resource_size_t addsize,
			resource_size_t min_align)
{
	struct resource *res = dev->resource + resno;
	resource_size_t new_size;
	int ret;

	if (!res->parent) {
		dev_info(&dev->dev, "BAR %d: can't reassign an unassigned resouce %pR "
			 "\n", resno, res);
		return -EINVAL;
	}

	new_size = resource_size(res) + addsize + min_align;
	ret = _pci_assign_resource(dev, resno, new_size, min_align);
	if (!ret) {
		res->flags &= ~IORESOURCE_STARTALIGN;
		dev_info(&dev->dev, "BAR %d: assigned %pR\n", resno, res);
		if (resno < PCI_BRIDGE_RESOURCES)
			pci_update_resource(dev, resno);
	}
	return ret;
}

int pci_assign_resource(struct pci_dev *dev, int resno)
{
	struct resource *res = dev->resource + resno;
	resource_size_t align, size;
	struct pci_bus *bus;
	int ret;

	align = pci_resource_alignment(dev, res);
	if (!align) {
		dev_info(&dev->dev, "BAR %d: can't assign %pR "
			 "(bogus alignment)\n", resno, res);
		return -EINVAL;
	}

	bus = dev->bus;
	size = resource_size(res);
	ret = _pci_assign_resource(dev, resno, size, align);

	/*
	 * If we failed to assign anything, let's try the address
	 * where firmware left it.  That at least has a chance of
	 * working, which is better than just leaving it disabled.
	 */
	if (ret < 0 && dev->fw_addr[resno])
		ret = pci_revert_fw_address(res, dev, resno, size);

	if (!ret) {
		res->flags &= ~IORESOURCE_STARTALIGN;
		dev_info(&dev->dev, "BAR %d: assigned %pR\n", resno, res);
		if (resno < PCI_BRIDGE_RESOURCES)
			pci_update_resource(dev, resno);
	}
	return ret;
}


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

		r_align = pci_resource_alignment(dev, r);
		if (!r_align) {
			dev_warn(&dev->dev, "BAR %d: %pR has bogus alignment\n",
				 i, r);
			continue;
		}
		for (list = head; ; list = list->next) {
			resource_size_t align = 0;
			struct resource_list *ln = list->next;

			if (ln)
				align = pci_resource_alignment(ln->dev, ln->res);

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
			dev_err(&dev->dev, "device not available "
				"(can't reserve %pR)\n", r);
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
