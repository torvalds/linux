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

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/cache.h>
#include <linux/slab.h>
#include "pci.h"

static void pci_std_update_resource(struct pci_dev *dev, int resno)
{
	struct pci_bus_region region;
	bool disable;
	u16 cmd;
	u32 new, check, mask;
	int reg;
	struct resource *res = dev->resource + resno;

	if (dev->is_virtfn) {
		dev_warn(&dev->dev, "can't update VF BAR%d\n", resno);
		return;
	}

	/*
	 * Ignore resources for unimplemented BARs and unused resource slots
	 * for 64 bit BARs.
	 */
	if (!res->flags)
		return;

	if (res->flags & IORESOURCE_UNSET)
		return;

	/*
	 * Ignore non-moveable resources.  This might be legacy resources for
	 * which no functional BAR register exists or another important
	 * system resource we shouldn't move around.
	 */
	if (res->flags & IORESOURCE_PCI_FIXED)
		return;

	pcibios_resource_to_bus(dev->bus, &region, res);
	new = region.start;

	if (res->flags & IORESOURCE_IO) {
		mask = (u32)PCI_BASE_ADDRESS_IO_MASK;
		new |= res->flags & ~PCI_BASE_ADDRESS_IO_MASK;
	} else if (resno == PCI_ROM_RESOURCE) {
		mask = (u32)PCI_ROM_ADDRESS_MASK;
	} else {
		mask = (u32)PCI_BASE_ADDRESS_MEM_MASK;
		new |= res->flags & ~PCI_BASE_ADDRESS_MEM_MASK;
	}

	if (resno < PCI_ROM_RESOURCE) {
		reg = PCI_BASE_ADDRESS_0 + 4 * resno;
	} else if (resno == PCI_ROM_RESOURCE) {

		/*
		 * Apparently some Matrox devices have ROM BARs that read
		 * as zero when disabled, so don't update ROM BARs unless
		 * they're enabled.  See https://lkml.org/lkml/2005/8/30/138.
		 */
		if (!(res->flags & IORESOURCE_ROM_ENABLE))
			return;

		reg = dev->rom_base_reg;
		new |= PCI_ROM_ADDRESS_ENABLE;
	} else
		return;

	/*
	 * We can't update a 64-bit BAR atomically, so when possible,
	 * disable decoding so that a half-updated BAR won't conflict
	 * with another device.
	 */
	disable = (res->flags & IORESOURCE_MEM_64) && !dev->mmio_always_on;
	if (disable) {
		pci_read_config_word(dev, PCI_COMMAND, &cmd);
		pci_write_config_word(dev, PCI_COMMAND,
				      cmd & ~PCI_COMMAND_MEMORY);
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
			dev_err(&dev->dev, "BAR %d: error updating (high %#08x != %#08x)\n",
				resno, new, check);
		}
	}

	if (disable)
		pci_write_config_word(dev, PCI_COMMAND, cmd);
}

void pci_update_resource(struct pci_dev *dev, int resno)
{
	if (resno <= PCI_ROM_RESOURCE)
		pci_std_update_resource(dev, resno);
#ifdef CONFIG_PCI_IOV
	else if (resno >= PCI_IOV_RESOURCES && resno <= PCI_IOV_RESOURCE_END)
		pci_iov_update_resource(dev, resno);
#endif
}

int pci_claim_resource(struct pci_dev *dev, int resource)
{
	struct resource *res = &dev->resource[resource];
	struct resource *root, *conflict;

	if (res->flags & IORESOURCE_UNSET) {
		dev_info(&dev->dev, "can't claim BAR %d %pR: no address assigned\n",
			 resource, res);
		return -EINVAL;
	}

	root = pci_find_parent_resource(dev, res);
	if (!root) {
		dev_info(&dev->dev, "can't claim BAR %d %pR: no compatible bridge window\n",
			 resource, res);
		res->flags |= IORESOURCE_UNSET;
		return -EINVAL;
	}

	conflict = request_resource_conflict(root, res);
	if (conflict) {
		dev_info(&dev->dev, "can't claim BAR %d %pR: address conflict with %s %pR\n",
			 resource, res, conflict->name, conflict);
		res->flags |= IORESOURCE_UNSET;
		return -EBUSY;
	}

	return 0;
}
EXPORT_SYMBOL(pci_claim_resource);

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

/*
 * Generic function that returns a value indicating that the device's
 * original BIOS BAR address was not saved and so is not available for
 * reinstatement.
 *
 * Can be over-ridden by architecture specific code that implements
 * reinstatement functionality rather than leaving it disabled when
 * normal allocation attempts fail.
 */
resource_size_t __weak pcibios_retrieve_fw_addr(struct pci_dev *dev, int idx)
{
	return 0;
}

static int pci_revert_fw_address(struct resource *res, struct pci_dev *dev,
		int resno, resource_size_t size)
{
	struct resource *root, *conflict;
	resource_size_t fw_addr, start, end;

	fw_addr = pcibios_retrieve_fw_addr(dev, resno);
	if (!fw_addr)
		return -ENOMEM;

	start = res->start;
	end = res->end;
	res->start = fw_addr;
	res->end = res->start + size - 1;
	res->flags &= ~IORESOURCE_UNSET;

	root = pci_find_parent_resource(dev, res);
	if (!root) {
		if (res->flags & IORESOURCE_IO)
			root = &ioport_resource;
		else
			root = &iomem_resource;
	}

	dev_info(&dev->dev, "BAR %d: trying firmware assignment %pR\n",
		 resno, res);
	conflict = request_resource_conflict(root, res);
	if (conflict) {
		dev_info(&dev->dev, "BAR %d: %pR conflicts with %s %pR\n",
			 resno, res, conflict->name, conflict);
		res->start = start;
		res->end = end;
		res->flags |= IORESOURCE_UNSET;
		return -EBUSY;
	}
	return 0;
}

static int __pci_assign_resource(struct pci_bus *bus, struct pci_dev *dev,
		int resno, resource_size_t size, resource_size_t align)
{
	struct resource *res = dev->resource + resno;
	resource_size_t min;
	int ret;

	min = (res->flags & IORESOURCE_IO) ? PCIBIOS_MIN_IO : PCIBIOS_MIN_MEM;

	/*
	 * First, try exact prefetching match.  Even if a 64-bit
	 * prefetchable bridge window is below 4GB, we can't put a 32-bit
	 * prefetchable resource in it because pbus_size_mem() assumes a
	 * 64-bit window will contain no 32-bit resources.  If we assign
	 * things differently than they were sized, not everything will fit.
	 */
	ret = pci_bus_alloc_resource(bus, res, size, align, min,
				     IORESOURCE_PREFETCH | IORESOURCE_MEM_64,
				     pcibios_align_resource, dev);
	if (ret == 0)
		return 0;

	/*
	 * If the prefetchable window is only 32 bits wide, we can put
	 * 64-bit prefetchable resources in it.
	 */
	if ((res->flags & (IORESOURCE_PREFETCH | IORESOURCE_MEM_64)) ==
	     (IORESOURCE_PREFETCH | IORESOURCE_MEM_64)) {
		ret = pci_bus_alloc_resource(bus, res, size, align, min,
					     IORESOURCE_PREFETCH,
					     pcibios_align_resource, dev);
		if (ret == 0)
			return 0;
	}

	/*
	 * If we didn't find a better match, we can put any memory resource
	 * in a non-prefetchable window.  If this resource is 32 bits and
	 * non-prefetchable, the first call already tried the only possibility
	 * so we don't need to try again.
	 */
	if (res->flags & (IORESOURCE_PREFETCH | IORESOURCE_MEM_64))
		ret = pci_bus_alloc_resource(bus, res, size, align, min, 0,
					     pcibios_align_resource, dev);

	return ret;
}

static int _pci_assign_resource(struct pci_dev *dev, int resno,
				resource_size_t size, resource_size_t min_align)
{
	struct pci_bus *bus;
	int ret;

	bus = dev->bus;
	while ((ret = __pci_assign_resource(bus, dev, resno, size, min_align))) {
		if (!bus->parent || !bus->self->transparent)
			break;
		bus = bus->parent;
	}

	return ret;
}

int pci_assign_resource(struct pci_dev *dev, int resno)
{
	struct resource *res = dev->resource + resno;
	resource_size_t align, size;
	int ret;

	res->flags |= IORESOURCE_UNSET;
	align = pci_resource_alignment(dev, res);
	if (!align) {
		dev_info(&dev->dev, "BAR %d: can't assign %pR (bogus alignment)\n",
			 resno, res);
		return -EINVAL;
	}

	size = resource_size(res);
	ret = _pci_assign_resource(dev, resno, size, align);

	/*
	 * If we failed to assign anything, let's try the address
	 * where firmware left it.  That at least has a chance of
	 * working, which is better than just leaving it disabled.
	 */
	if (ret < 0) {
		dev_info(&dev->dev, "BAR %d: no space for %pR\n", resno, res);
		ret = pci_revert_fw_address(res, dev, resno, size);
	}

	if (ret < 0) {
		dev_info(&dev->dev, "BAR %d: failed to assign %pR\n", resno,
			 res);
		return ret;
	}

	res->flags &= ~IORESOURCE_UNSET;
	res->flags &= ~IORESOURCE_STARTALIGN;
	dev_info(&dev->dev, "BAR %d: assigned %pR\n", resno, res);
	if (resno < PCI_BRIDGE_RESOURCES)
		pci_update_resource(dev, resno);

	return 0;
}
EXPORT_SYMBOL(pci_assign_resource);

int pci_reassign_resource(struct pci_dev *dev, int resno, resource_size_t addsize,
			resource_size_t min_align)
{
	struct resource *res = dev->resource + resno;
	unsigned long flags;
	resource_size_t new_size;
	int ret;

	flags = res->flags;
	res->flags |= IORESOURCE_UNSET;
	if (!res->parent) {
		dev_info(&dev->dev, "BAR %d: can't reassign an unassigned resource %pR\n",
			 resno, res);
		return -EINVAL;
	}

	/* already aligned with min_align */
	new_size = resource_size(res) + addsize;
	ret = _pci_assign_resource(dev, resno, new_size, min_align);
	if (ret) {
		res->flags = flags;
		dev_info(&dev->dev, "BAR %d: %pR (failed to expand by %#llx)\n",
			 resno, res, (unsigned long long) addsize);
		return ret;
	}

	res->flags &= ~IORESOURCE_UNSET;
	res->flags &= ~IORESOURCE_STARTALIGN;
	dev_info(&dev->dev, "BAR %d: reassigned %pR (expanded by %#llx)\n",
		 resno, res, (unsigned long long) addsize);
	if (resno < PCI_BRIDGE_RESOURCES)
		pci_update_resource(dev, resno);

	return 0;
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

		if (r->flags & IORESOURCE_UNSET) {
			dev_err(&dev->dev, "can't enable device: BAR %d %pR not assigned\n",
				i, r);
			return -EINVAL;
		}

		if (!r->parent) {
			dev_err(&dev->dev, "can't enable device: BAR %d %pR not claimed\n",
				i, r);
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
