// SPDX-License-Identifier: GPL-2.0
/*
 * Support routines for initializing a PCI subsystem
 *
 * Extruded from code written by
 *      Dave Rusling (david.rusling@reo.mts.dec.com)
 *      David Mosberger (davidm@cs.arizona.edu)
 *	David Miller (davem@redhat.com)
 *
 * Fixed for multiple PCI buses, 1999 Andrea Arcangeli <andrea@suse.de>
 *
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
	struct resource *res = pci_resource_n(dev, resno);
	const char *res_name = pci_resource_name(dev, resno);

	/* Per SR-IOV spec 3.4.1.11, VF BARs are RO zero */
	if (dev->is_virtfn)
		return;

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
		mask = PCI_ROM_ADDRESS_MASK;
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
		 * they're enabled.  See
		 * https://lore.kernel.org/r/43147B3D.1030309@vc.cvut.cz/
		 * But we must update ROM BAR for buggy devices where even a
		 * disabled ROM can conflict with other BARs.
		 */
		if (!(res->flags & IORESOURCE_ROM_ENABLE) &&
		    !dev->rom_bar_overlap)
			return;

		reg = dev->rom_base_reg;
		if (res->flags & IORESOURCE_ROM_ENABLE)
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
		pci_err(dev, "%s: error updating (%#010x != %#010x)\n",
			res_name, new, check);
	}

	if (res->flags & IORESOURCE_MEM_64) {
		new = region.start >> 16 >> 16;
		pci_write_config_dword(dev, reg + 4, new);
		pci_read_config_dword(dev, reg + 4, &check);
		if (check != new) {
			pci_err(dev, "%s: error updating (high %#010x != %#010x)\n",
				res_name, new, check);
		}
	}

	if (disable)
		pci_write_config_word(dev, PCI_COMMAND, cmd);
}

void pci_update_resource(struct pci_dev *dev, int resno)
{
	if (resno <= PCI_ROM_RESOURCE)
		pci_std_update_resource(dev, resno);
	else if (pci_resource_is_iov(resno))
		pci_iov_update_resource(dev, resno);
}

int pci_claim_resource(struct pci_dev *dev, int resource)
{
	struct resource *res = &dev->resource[resource];
	const char *res_name = pci_resource_name(dev, resource);
	struct resource *root, *conflict;

	if (res->flags & IORESOURCE_UNSET) {
		pci_info(dev, "%s %pR: can't claim; no address assigned\n",
			 res_name, res);
		return -EINVAL;
	}

	/*
	 * If we have a shadow copy in RAM, the PCI device doesn't respond
	 * to the shadow range, so we don't need to claim it, and upstream
	 * bridges don't need to route the range to the device.
	 */
	if (res->flags & IORESOURCE_ROM_SHADOW)
		return 0;

	root = pci_find_parent_resource(dev, res);
	if (!root) {
		pci_info(dev, "%s %pR: can't claim; no compatible bridge window\n",
			 res_name, res);
		res->flags |= IORESOURCE_UNSET;
		return -EINVAL;
	}

	conflict = request_resource_conflict(root, res);
	if (conflict) {
		pci_info(dev, "%s %pR: can't claim; address conflict with %s %pR\n",
			 res_name, res, conflict->name, conflict);
		res->flags |= IORESOURCE_UNSET;
		return -EBUSY;
	}

	return 0;
}
EXPORT_SYMBOL(pci_claim_resource);

void pci_disable_bridge_window(struct pci_dev *dev)
{
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
	const char *res_name = pci_resource_name(dev, resno);

	fw_addr = pcibios_retrieve_fw_addr(dev, resno);
	if (!fw_addr)
		return -ENOMEM;

	start = res->start;
	end = res->end;
	resource_set_range(res, fw_addr, size);
	res->flags &= ~IORESOURCE_UNSET;

	root = pci_find_parent_resource(dev, res);
	if (!root) {
		/*
		 * If dev is behind a bridge, accesses will only reach it
		 * if res is inside the relevant bridge window.
		 */
		if (pci_upstream_bridge(dev))
			return -ENXIO;

		/*
		 * On the root bus, assume the host bridge will forward
		 * everything.
		 */
		if (res->flags & IORESOURCE_IO)
			root = &ioport_resource;
		else
			root = &iomem_resource;
	}

	pci_info(dev, "%s: trying firmware assignment %pR\n", res_name, res);
	conflict = request_resource_conflict(root, res);
	if (conflict) {
		pci_info(dev, "%s %pR: conflicts with %s %pR\n", res_name, res,
			 conflict->name, conflict);
		res->start = start;
		res->end = end;
		res->flags |= IORESOURCE_UNSET;
		return -EBUSY;
	}
	return 0;
}

/*
 * We don't have to worry about legacy ISA devices, so nothing to do here.
 * This is marked as __weak because multiple architectures define it; it should
 * eventually go away.
 */
resource_size_t __weak pcibios_align_resource(void *data,
					      const struct resource *res,
					      resource_size_t size,
					      resource_size_t align)
{
       return res->start;
}

static int __pci_assign_resource(struct pci_bus *bus, struct pci_dev *dev,
		int resno, resource_size_t size, resource_size_t align)
{
	struct resource *res = pci_resource_n(dev, resno);
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
	struct resource *res = pci_resource_n(dev, resno);
	const char *res_name = pci_resource_name(dev, resno);
	resource_size_t align, size;
	int ret;

	if (res->flags & IORESOURCE_PCI_FIXED)
		return 0;

	res->flags |= IORESOURCE_UNSET;
	align = pci_resource_alignment(dev, res);
	if (!align) {
		pci_info(dev, "%s %pR: can't assign; bogus alignment\n",
			 res_name, res);
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
		pci_info(dev, "%s %pR: can't assign; no space\n", res_name, res);
		ret = pci_revert_fw_address(res, dev, resno, size);
	}

	if (ret < 0) {
		pci_info(dev, "%s %pR: failed to assign\n", res_name, res);
		return ret;
	}

	res->flags &= ~IORESOURCE_UNSET;
	res->flags &= ~IORESOURCE_STARTALIGN;
	pci_info(dev, "%s %pR: assigned\n", res_name, res);
	if (resno < PCI_BRIDGE_RESOURCES)
		pci_update_resource(dev, resno);

	return 0;
}
EXPORT_SYMBOL(pci_assign_resource);

int pci_reassign_resource(struct pci_dev *dev, int resno,
			  resource_size_t addsize, resource_size_t min_align)
{
	struct resource *res = pci_resource_n(dev, resno);
	const char *res_name = pci_resource_name(dev, resno);
	unsigned long flags;
	resource_size_t new_size;
	int ret;

	if (res->flags & IORESOURCE_PCI_FIXED)
		return 0;

	flags = res->flags;
	res->flags |= IORESOURCE_UNSET;
	if (!res->parent) {
		pci_info(dev, "%s %pR: can't reassign; unassigned resource\n",
			 res_name, res);
		return -EINVAL;
	}

	new_size = resource_size(res) + addsize;
	ret = _pci_assign_resource(dev, resno, new_size, min_align);
	if (ret) {
		res->flags = flags;
		pci_info(dev, "%s %pR: failed to expand by %#llx\n",
			 res_name, res, (unsigned long long) addsize);
		return ret;
	}

	res->flags &= ~IORESOURCE_UNSET;
	res->flags &= ~IORESOURCE_STARTALIGN;
	pci_info(dev, "%s %pR: reassigned; expanded by %#llx\n",
		 res_name, res, (unsigned long long) addsize);
	if (resno < PCI_BRIDGE_RESOURCES)
		pci_update_resource(dev, resno);

	return 0;
}

void pci_release_resource(struct pci_dev *dev, int resno)
{
	struct resource *res = pci_resource_n(dev, resno);
	const char *res_name = pci_resource_name(dev, resno);

	if (!res->parent)
		return;

	pci_info(dev, "%s %pR: releasing\n", res_name, res);

	release_resource(res);
	res->end = resource_size(res) - 1;
	res->start = 0;
	res->flags |= IORESOURCE_UNSET;
}
EXPORT_SYMBOL(pci_release_resource);

int pci_resize_resource(struct pci_dev *dev, int resno, int size)
{
	struct resource *res = pci_resource_n(dev, resno);
	struct pci_host_bridge *host;
	int old, ret;
	u32 sizes;
	u16 cmd;

	/* Check if we must preserve the firmware's resource assignment */
	host = pci_find_host_bridge(dev->bus);
	if (host->preserve_config)
		return -ENOTSUPP;

	/* Make sure the resource isn't assigned before resizing it. */
	if (!(res->flags & IORESOURCE_UNSET))
		return -EBUSY;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	if (cmd & PCI_COMMAND_MEMORY)
		return -EBUSY;

	sizes = pci_rebar_get_possible_sizes(dev, resno);
	if (!sizes)
		return -ENOTSUPP;

	if (!(sizes & BIT(size)))
		return -EINVAL;

	old = pci_rebar_get_current_size(dev, resno);
	if (old < 0)
		return old;

	ret = pci_rebar_set_size(dev, resno, size);
	if (ret)
		return ret;

	resource_set_size(res, pci_rebar_size_to_bytes(size));

	/* Check if the new config works by trying to assign everything. */
	if (dev->bus->self) {
		ret = pci_reassign_bridge_resources(dev->bus->self, res->flags);
		if (ret)
			goto error_resize;
	}
	return 0;

error_resize:
	pci_rebar_set_size(dev, resno, old);
	resource_set_size(res, pci_rebar_size_to_bytes(old));
	return ret;
}
EXPORT_SYMBOL(pci_resize_resource);

int pci_enable_resources(struct pci_dev *dev, int mask)
{
	u16 cmd, old_cmd;
	int i;
	struct resource *r;
	const char *r_name;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;

	pci_dev_for_each_resource(dev, r, i) {
		if (!(mask & (1 << i)))
			continue;

		r_name = pci_resource_name(dev, i);

		if (!(r->flags & (IORESOURCE_IO | IORESOURCE_MEM)))
			continue;
		if (pci_resource_is_optional(dev, i))
			continue;

		if (r->flags & IORESOURCE_UNSET) {
			pci_err(dev, "%s %pR: not assigned; can't enable device\n",
				r_name, r);
			return -EINVAL;
		}

		if (!r->parent) {
			pci_err(dev, "%s %pR: not claimed; can't enable device\n",
				r_name, r);
			return -EINVAL;
		}

		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}

	if (cmd != old_cmd) {
		pci_info(dev, "enabling device (%04x -> %04x)\n", old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}
