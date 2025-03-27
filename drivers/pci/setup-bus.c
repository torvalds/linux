// SPDX-License-Identifier: GPL-2.0
/*
 * Support routines for initializing a PCI subsystem
 *
 * Extruded from code written by
 *      Dave Rusling (david.rusling@reo.mts.dec.com)
 *      David Mosberger (davidm@cs.arizona.edu)
 *	David Miller (davem@redhat.com)
 *
 * Nov 2000, Ivan Kokshaysky <ink@jurassic.park.msu.ru>
 *	     PCI-PCI bridges cleanup, sorted resource allocation.
 * Feb 2002, Ivan Kokshaysky <ink@jurassic.park.msu.ru>
 *	     Converted to allocation in 3 passes, which gives
 *	     tighter packing. Prefetchable range support.
 */

#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/cache.h>
#include <linux/limits.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include "pci.h"

unsigned int pci_flags;
EXPORT_SYMBOL_GPL(pci_flags);

struct pci_dev_resource {
	struct list_head list;
	struct resource *res;
	struct pci_dev *dev;
	resource_size_t start;
	resource_size_t end;
	resource_size_t add_size;
	resource_size_t min_align;
	unsigned long flags;
};

static void free_list(struct list_head *head)
{
	struct pci_dev_resource *dev_res, *tmp;

	list_for_each_entry_safe(dev_res, tmp, head, list) {
		list_del(&dev_res->list);
		kfree(dev_res);
	}
}

/**
 * add_to_list() - Add a new resource tracker to the list
 * @head:	Head of the list
 * @dev:	Device to which the resource belongs
 * @res:	Resource to be tracked
 * @add_size:	Additional size to be optionally added to the resource
 * @min_align:	Minimum memory window alignment
 */
static int add_to_list(struct list_head *head, struct pci_dev *dev,
		       struct resource *res, resource_size_t add_size,
		       resource_size_t min_align)
{
	struct pci_dev_resource *tmp;

	tmp = kzalloc(sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	tmp->res = res;
	tmp->dev = dev;
	tmp->start = res->start;
	tmp->end = res->end;
	tmp->flags = res->flags;
	tmp->add_size = add_size;
	tmp->min_align = min_align;

	list_add(&tmp->list, head);

	return 0;
}

static void remove_from_list(struct list_head *head, struct resource *res)
{
	struct pci_dev_resource *dev_res, *tmp;

	list_for_each_entry_safe(dev_res, tmp, head, list) {
		if (dev_res->res == res) {
			list_del(&dev_res->list);
			kfree(dev_res);
			break;
		}
	}
}

static struct pci_dev_resource *res_to_dev_res(struct list_head *head,
					       struct resource *res)
{
	struct pci_dev_resource *dev_res;

	list_for_each_entry(dev_res, head, list) {
		if (dev_res->res == res)
			return dev_res;
	}

	return NULL;
}

static resource_size_t get_res_add_size(struct list_head *head,
					struct resource *res)
{
	struct pci_dev_resource *dev_res;

	dev_res = res_to_dev_res(head, res);
	return dev_res ? dev_res->add_size : 0;
}

static resource_size_t get_res_add_align(struct list_head *head,
					 struct resource *res)
{
	struct pci_dev_resource *dev_res;

	dev_res = res_to_dev_res(head, res);
	return dev_res ? dev_res->min_align : 0;
}

static void restore_dev_resource(struct pci_dev_resource *dev_res)
{
	struct resource *res = dev_res->res;

	res->start = dev_res->start;
	res->end = dev_res->end;
	res->flags = dev_res->flags;
}

static bool pdev_resources_assignable(struct pci_dev *dev)
{
	u16 class = dev->class >> 8, command;

	/* Don't touch classless devices or host bridges or IOAPICs */
	if (class == PCI_CLASS_NOT_DEFINED || class == PCI_CLASS_BRIDGE_HOST)
		return false;

	/* Don't touch IOAPIC devices already enabled by firmware */
	if (class == PCI_CLASS_SYSTEM_PIC) {
		pci_read_config_word(dev, PCI_COMMAND, &command);
		if (command & (PCI_COMMAND_IO | PCI_COMMAND_MEMORY))
			return false;
	}

	return true;
}

/* Sort resources by alignment */
static void pdev_sort_resources(struct pci_dev *dev, struct list_head *head)
{
	struct resource *r;
	int i;

	if (!pdev_resources_assignable(dev))
		return;

	pci_dev_for_each_resource(dev, r, i) {
		const char *r_name = pci_resource_name(dev, i);
		struct pci_dev_resource *dev_res, *tmp;
		resource_size_t r_align;
		struct list_head *n;

		if (r->flags & IORESOURCE_PCI_FIXED)
			continue;

		if (!(r->flags) || r->parent)
			continue;

		r_align = pci_resource_alignment(dev, r);
		if (!r_align) {
			pci_warn(dev, "%s %pR: alignment must not be zero\n",
				 r_name, r);
			continue;
		}

		tmp = kzalloc(sizeof(*tmp), GFP_KERNEL);
		if (!tmp)
			panic("%s: kzalloc() failed!\n", __func__);
		tmp->res = r;
		tmp->dev = dev;

		/* Fallback is smallest one or list is empty */
		n = head;
		list_for_each_entry(dev_res, head, list) {
			resource_size_t align;

			align = pci_resource_alignment(dev_res->dev,
							 dev_res->res);

			if (r_align > align) {
				n = &dev_res->list;
				break;
			}
		}
		/* Insert it just before n */
		list_add_tail(&tmp->list, n);
	}
}

bool pci_resource_is_optional(const struct pci_dev *dev, int resno)
{
	const struct resource *res = pci_resource_n(dev, resno);

	if (pci_resource_is_iov(resno))
		return true;
	if (resno == PCI_ROM_RESOURCE && !(res->flags & IORESOURCE_ROM_ENABLE))
		return true;

	return false;
}

static inline void reset_resource(struct resource *res)
{
	res->start = 0;
	res->end = 0;
	res->flags = 0;
}

/**
 * reassign_resources_sorted() - Satisfy any additional resource requests
 *
 * @realloc_head:	Head of the list tracking requests requiring
 *			additional resources
 * @head:		Head of the list tracking requests with allocated
 *			resources
 *
 * Walk through each element of the realloc_head and try to procure additional
 * resources for the element, provided the element is in the head list.
 */
static void reassign_resources_sorted(struct list_head *realloc_head,
				      struct list_head *head)
{
	struct pci_dev_resource *add_res, *tmp;
	struct pci_dev_resource *dev_res;
	struct pci_dev *dev;
	struct resource *res;
	const char *res_name;
	resource_size_t add_size, align;
	int idx;

	list_for_each_entry_safe(add_res, tmp, realloc_head, list) {
		bool found_match = false;

		res = add_res->res;
		dev = add_res->dev;
		idx = pci_resource_num(dev, res);

		/*
		 * Skip resource that failed the earlier assignment and is
		 * not optional as it would just fail again.
		 */
		if (!res->parent && resource_size(res) &&
		    !pci_resource_is_optional(dev, idx))
			goto out;

		/* Skip this resource if not found in head list */
		list_for_each_entry(dev_res, head, list) {
			if (dev_res->res == res) {
				found_match = true;
				break;
			}
		}
		if (!found_match) /* Just skip */
			continue;

		res_name = pci_resource_name(dev, idx);
		add_size = add_res->add_size;
		align = add_res->min_align;
		if (!res->parent) {
			resource_set_range(res, align,
					   resource_size(res) + add_size);
			if (pci_assign_resource(dev, idx)) {
				pci_dbg(dev,
					"%s %pR: ignoring failure in optional allocation\n",
					res_name, res);
			}
		} else if (add_size > 0) {
			res->flags |= add_res->flags &
				 (IORESOURCE_STARTALIGN|IORESOURCE_SIZEALIGN);
			if (pci_reassign_resource(dev, idx, add_size, align))
				pci_info(dev, "%s %pR: failed to add optional %llx\n",
					 res_name, res,
					 (unsigned long long) add_size);
		}
out:
		list_del(&add_res->list);
		kfree(add_res);
	}
}

/**
 * assign_requested_resources_sorted() - Satisfy resource requests
 *
 * @head:	Head of the list tracking requests for resources
 * @fail_head:	Head of the list tracking requests that could not be
 *		allocated
 * @optional:	Assign also optional resources
 *
 * Satisfy resource requests of each element in the list.  Add requests that
 * could not be satisfied to the failed_list.
 */
static void assign_requested_resources_sorted(struct list_head *head,
					      struct list_head *fail_head,
					      bool optional)
{
	struct pci_dev_resource *dev_res;
	struct resource *res;
	struct pci_dev *dev;
	bool optional_res;
	int idx;

	list_for_each_entry(dev_res, head, list) {
		res = dev_res->res;
		dev = dev_res->dev;
		idx = pci_resource_num(dev, res);
		optional_res = pci_resource_is_optional(dev, idx);

		if (!resource_size(res))
			continue;

		if (!optional && optional_res)
			continue;

		if (pci_assign_resource(dev, idx)) {
			if (fail_head) {
				add_to_list(fail_head, dev, res,
					    0 /* don't care */,
					    0 /* don't care */);
			}
		}
	}
}

static unsigned long pci_fail_res_type_mask(struct list_head *fail_head)
{
	struct pci_dev_resource *fail_res;
	unsigned long mask = 0;

	/* Check failed type */
	list_for_each_entry(fail_res, fail_head, list)
		mask |= fail_res->flags;

	/*
	 * One pref failed resource will set IORESOURCE_MEM, as we can
	 * allocate pref in non-pref range.  Will release all assigned
	 * non-pref sibling resources according to that bit.
	 */
	return mask & (IORESOURCE_IO | IORESOURCE_MEM | IORESOURCE_PREFETCH);
}

static bool pci_need_to_release(unsigned long mask, struct resource *res)
{
	if (res->flags & IORESOURCE_IO)
		return !!(mask & IORESOURCE_IO);

	/* Check pref at first */
	if (res->flags & IORESOURCE_PREFETCH) {
		if (mask & IORESOURCE_PREFETCH)
			return true;
		/* Count pref if its parent is non-pref */
		else if ((mask & IORESOURCE_MEM) &&
			 !(res->parent->flags & IORESOURCE_PREFETCH))
			return true;
		else
			return false;
	}

	if (res->flags & IORESOURCE_MEM)
		return !!(mask & IORESOURCE_MEM);

	return false;	/* Should not get here */
}

/* Return: @true if assignment of a required resource failed. */
static bool pci_required_resource_failed(struct list_head *fail_head)
{
	struct pci_dev_resource *fail_res;

	list_for_each_entry(fail_res, fail_head, list) {
		int idx = pci_resource_num(fail_res->dev, fail_res->res);

		if (!pci_resource_is_optional(fail_res->dev, idx))
			return true;
	}
	return false;
}

static void __assign_resources_sorted(struct list_head *head,
				      struct list_head *realloc_head,
				      struct list_head *fail_head)
{
	/*
	 * Should not assign requested resources at first.  They could be
	 * adjacent, so later reassign can not reallocate them one by one in
	 * parent resource window.
	 *
	 * Try to assign required and any optional resources at beginning
	 * (add_size included). If all required resources were successfully
	 * assigned, get out early. If could not do that, we still try to
	 * assign required at first, then try to reassign some optional
	 * resources.
	 *
	 * Separate three resource type checking if we need to release
	 * assigned resource after requested + add_size try.
	 *
	 *	1. If IO port assignment fails, will release assigned IO
	 *	   port.
	 *	2. If pref MMIO assignment fails, release assigned pref
	 *	   MMIO.  If assigned pref MMIO's parent is non-pref MMIO
	 *	   and non-pref MMIO assignment fails, will release that
	 *	   assigned pref MMIO.
	 *	3. If non-pref MMIO assignment fails or pref MMIO
	 *	   assignment fails, will release assigned non-pref MMIO.
	 */
	LIST_HEAD(save_head);
	LIST_HEAD(local_fail_head);
	LIST_HEAD(dummy_head);
	struct pci_dev_resource *save_res;
	struct pci_dev_resource *dev_res, *tmp_res, *dev_res2;
	struct resource *res;
	struct pci_dev *dev;
	const char *res_name;
	int idx;
	unsigned long fail_type;
	resource_size_t add_align, align;

	if (!realloc_head)
		realloc_head = &dummy_head;

	/* Check if optional add_size is there */
	if (list_empty(realloc_head))
		goto assign;

	/* Save original start, end, flags etc at first */
	list_for_each_entry(dev_res, head, list) {
		if (add_to_list(&save_head, dev_res->dev, dev_res->res, 0, 0)) {
			free_list(&save_head);
			goto assign;
		}
	}

	/* Update res in head list with add_size in realloc_head list */
	list_for_each_entry_safe(dev_res, tmp_res, head, list) {
		res = dev_res->res;

		res->end += get_res_add_size(realloc_head, res);

		/*
		 * There are two kinds of additional resources in the list:
		 * 1. bridge resource  -- IORESOURCE_STARTALIGN
		 * 2. SR-IOV resource  -- IORESOURCE_SIZEALIGN
		 * Here just fix the additional alignment for bridge
		 */
		if (!(res->flags & IORESOURCE_STARTALIGN))
			continue;

		add_align = get_res_add_align(realloc_head, res);

		/*
		 * The "head" list is sorted by alignment so resources with
		 * bigger alignment will be assigned first.  After we
		 * change the alignment of a dev_res in "head" list, we
		 * need to reorder the list by alignment to make it
		 * consistent.
		 */
		if (add_align > res->start) {
			resource_set_range(res, add_align, resource_size(res));

			list_for_each_entry(dev_res2, head, list) {
				align = pci_resource_alignment(dev_res2->dev,
							       dev_res2->res);
				if (add_align > align) {
					list_move_tail(&dev_res->list,
						       &dev_res2->list);
					break;
				}
			}
		}

	}

assign:
	assign_requested_resources_sorted(head, &local_fail_head, true);

	/* All non-optional resources assigned? */
	if (list_empty(&local_fail_head)) {
		/* Remove head list from realloc_head list */
		list_for_each_entry(dev_res, head, list)
			remove_from_list(realloc_head, dev_res->res);
		free_list(&save_head);
		goto out;
	}

	/* Without realloc_head and only optional fails, nothing more to do. */
	if (!pci_required_resource_failed(&local_fail_head) &&
	    list_empty(realloc_head)) {
		list_for_each_entry(save_res, &save_head, list) {
			struct resource *res = save_res->res;

			if (res->parent)
				continue;

			restore_dev_resource(save_res);
		}
		free_list(&local_fail_head);
		free_list(&save_head);
		goto out;
	}

	/* Check failed type */
	fail_type = pci_fail_res_type_mask(&local_fail_head);
	/* Remove not need to be released assigned res from head list etc */
	list_for_each_entry_safe(dev_res, tmp_res, head, list) {
		res = dev_res->res;

		if (res->parent && !pci_need_to_release(fail_type, res)) {
			/* Remove it from realloc_head list */
			remove_from_list(realloc_head, res);
			remove_from_list(&save_head, res);
			list_del(&dev_res->list);
			kfree(dev_res);
		}
	}

	free_list(&local_fail_head);
	/* Release assigned resource */
	list_for_each_entry(dev_res, head, list) {
		res = dev_res->res;
		dev = dev_res->dev;

		if (!res->parent)
			continue;

		idx = pci_resource_num(dev, res);
		res_name = pci_resource_name(dev, idx);
		pci_dbg(dev, "%s %pR: releasing\n", res_name, res);

		release_resource(res);
	}
	/* Restore start/end/flags from saved list */
	list_for_each_entry(save_res, &save_head, list)
		restore_dev_resource(save_res);
	free_list(&save_head);

	/* Satisfy the must-have resource requests */
	assign_requested_resources_sorted(head, NULL, false);

	/* Try to satisfy any additional optional resource requests */
	if (!list_empty(realloc_head))
		reassign_resources_sorted(realloc_head, head);

out:
	/* Reset any failed resource, cannot use fail_head as it can be NULL. */
	list_for_each_entry(dev_res, head, list) {
		res = dev_res->res;
		dev = dev_res->dev;

		if (res->parent)
			continue;

		if (fail_head) {
			add_to_list(fail_head, dev, res,
				    0 /* don't care */,
				    0 /* don't care */);
		}

		reset_resource(res);
	}

	free_list(head);
}

static void pdev_assign_resources_sorted(struct pci_dev *dev,
					 struct list_head *add_head,
					 struct list_head *fail_head)
{
	LIST_HEAD(head);

	pdev_sort_resources(dev, &head);
	__assign_resources_sorted(&head, add_head, fail_head);

}

static void pbus_assign_resources_sorted(const struct pci_bus *bus,
					 struct list_head *realloc_head,
					 struct list_head *fail_head)
{
	struct pci_dev *dev;
	LIST_HEAD(head);

	list_for_each_entry(dev, &bus->devices, bus_list)
		pdev_sort_resources(dev, &head);

	__assign_resources_sorted(&head, realloc_head, fail_head);
}

void pci_setup_cardbus(struct pci_bus *bus)
{
	struct pci_dev *bridge = bus->self;
	struct resource *res;
	struct pci_bus_region region;

	pci_info(bridge, "CardBus bridge to %pR\n",
		 &bus->busn_res);

	res = bus->resource[0];
	pcibios_resource_to_bus(bridge->bus, &region, res);
	if (res->flags & IORESOURCE_IO) {
		/*
		 * The IO resource is allocated a range twice as large as it
		 * would normally need.  This allows us to set both IO regs.
		 */
		pci_info(bridge, "  bridge window %pR\n", res);
		pci_write_config_dword(bridge, PCI_CB_IO_BASE_0,
					region.start);
		pci_write_config_dword(bridge, PCI_CB_IO_LIMIT_0,
					region.end);
	}

	res = bus->resource[1];
	pcibios_resource_to_bus(bridge->bus, &region, res);
	if (res->flags & IORESOURCE_IO) {
		pci_info(bridge, "  bridge window %pR\n", res);
		pci_write_config_dword(bridge, PCI_CB_IO_BASE_1,
					region.start);
		pci_write_config_dword(bridge, PCI_CB_IO_LIMIT_1,
					region.end);
	}

	res = bus->resource[2];
	pcibios_resource_to_bus(bridge->bus, &region, res);
	if (res->flags & IORESOURCE_MEM) {
		pci_info(bridge, "  bridge window %pR\n", res);
		pci_write_config_dword(bridge, PCI_CB_MEMORY_BASE_0,
					region.start);
		pci_write_config_dword(bridge, PCI_CB_MEMORY_LIMIT_0,
					region.end);
	}

	res = bus->resource[3];
	pcibios_resource_to_bus(bridge->bus, &region, res);
	if (res->flags & IORESOURCE_MEM) {
		pci_info(bridge, "  bridge window %pR\n", res);
		pci_write_config_dword(bridge, PCI_CB_MEMORY_BASE_1,
					region.start);
		pci_write_config_dword(bridge, PCI_CB_MEMORY_LIMIT_1,
					region.end);
	}
}
EXPORT_SYMBOL(pci_setup_cardbus);

/*
 * Initialize bridges with base/limit values we have collected.  PCI-to-PCI
 * Bridge Architecture Specification rev. 1.1 (1998) requires that if there
 * are no I/O ports or memory behind the bridge, the corresponding range
 * must be turned off by writing base value greater than limit to the
 * bridge's base/limit registers.
 *
 * Note: care must be taken when updating I/O base/limit registers of
 * bridges which support 32-bit I/O.  This update requires two config space
 * writes, so it's quite possible that an I/O window of the bridge will
 * have some undesirable address (e.g. 0) after the first write.  Ditto
 * 64-bit prefetchable MMIO.
 */
static void pci_setup_bridge_io(struct pci_dev *bridge)
{
	struct resource *res;
	const char *res_name;
	struct pci_bus_region region;
	unsigned long io_mask;
	u8 io_base_lo, io_limit_lo;
	u16 l;
	u32 io_upper16;

	io_mask = PCI_IO_RANGE_MASK;
	if (bridge->io_window_1k)
		io_mask = PCI_IO_1K_RANGE_MASK;

	/* Set up the top and bottom of the PCI I/O segment for this bus */
	res = &bridge->resource[PCI_BRIDGE_IO_WINDOW];
	res_name = pci_resource_name(bridge, PCI_BRIDGE_IO_WINDOW);
	pcibios_resource_to_bus(bridge->bus, &region, res);
	if (res->flags & IORESOURCE_IO) {
		pci_read_config_word(bridge, PCI_IO_BASE, &l);
		io_base_lo = (region.start >> 8) & io_mask;
		io_limit_lo = (region.end >> 8) & io_mask;
		l = ((u16) io_limit_lo << 8) | io_base_lo;
		/* Set up upper 16 bits of I/O base/limit */
		io_upper16 = (region.end & 0xffff0000) | (region.start >> 16);
		pci_info(bridge, "  %s %pR\n", res_name, res);
	} else {
		/* Clear upper 16 bits of I/O base/limit */
		io_upper16 = 0;
		l = 0x00f0;
	}
	/* Temporarily disable the I/O range before updating PCI_IO_BASE */
	pci_write_config_dword(bridge, PCI_IO_BASE_UPPER16, 0x0000ffff);
	/* Update lower 16 bits of I/O base/limit */
	pci_write_config_word(bridge, PCI_IO_BASE, l);
	/* Update upper 16 bits of I/O base/limit */
	pci_write_config_dword(bridge, PCI_IO_BASE_UPPER16, io_upper16);
}

static void pci_setup_bridge_mmio(struct pci_dev *bridge)
{
	struct resource *res;
	const char *res_name;
	struct pci_bus_region region;
	u32 l;

	/* Set up the top and bottom of the PCI Memory segment for this bus */
	res = &bridge->resource[PCI_BRIDGE_MEM_WINDOW];
	res_name = pci_resource_name(bridge, PCI_BRIDGE_MEM_WINDOW);
	pcibios_resource_to_bus(bridge->bus, &region, res);
	if (res->flags & IORESOURCE_MEM) {
		l = (region.start >> 16) & 0xfff0;
		l |= region.end & 0xfff00000;
		pci_info(bridge, "  %s %pR\n", res_name, res);
	} else {
		l = 0x0000fff0;
	}
	pci_write_config_dword(bridge, PCI_MEMORY_BASE, l);
}

static void pci_setup_bridge_mmio_pref(struct pci_dev *bridge)
{
	struct resource *res;
	const char *res_name;
	struct pci_bus_region region;
	u32 l, bu, lu;

	/*
	 * Clear out the upper 32 bits of PREF limit.  If
	 * PCI_PREF_BASE_UPPER32 was non-zero, this temporarily disables
	 * PREF range, which is ok.
	 */
	pci_write_config_dword(bridge, PCI_PREF_LIMIT_UPPER32, 0);

	/* Set up PREF base/limit */
	bu = lu = 0;
	res = &bridge->resource[PCI_BRIDGE_PREF_MEM_WINDOW];
	res_name = pci_resource_name(bridge, PCI_BRIDGE_PREF_MEM_WINDOW);
	pcibios_resource_to_bus(bridge->bus, &region, res);
	if (res->flags & IORESOURCE_PREFETCH) {
		l = (region.start >> 16) & 0xfff0;
		l |= region.end & 0xfff00000;
		if (res->flags & IORESOURCE_MEM_64) {
			bu = upper_32_bits(region.start);
			lu = upper_32_bits(region.end);
		}
		pci_info(bridge, "  %s %pR\n", res_name, res);
	} else {
		l = 0x0000fff0;
	}
	pci_write_config_dword(bridge, PCI_PREF_MEMORY_BASE, l);

	/* Set the upper 32 bits of PREF base & limit */
	pci_write_config_dword(bridge, PCI_PREF_BASE_UPPER32, bu);
	pci_write_config_dword(bridge, PCI_PREF_LIMIT_UPPER32, lu);
}

static void __pci_setup_bridge(struct pci_bus *bus, unsigned long type)
{
	struct pci_dev *bridge = bus->self;

	pci_info(bridge, "PCI bridge to %pR\n",
		 &bus->busn_res);

	if (type & IORESOURCE_IO)
		pci_setup_bridge_io(bridge);

	if (type & IORESOURCE_MEM)
		pci_setup_bridge_mmio(bridge);

	if (type & IORESOURCE_PREFETCH)
		pci_setup_bridge_mmio_pref(bridge);

	pci_write_config_word(bridge, PCI_BRIDGE_CONTROL, bus->bridge_ctl);
}

void __weak pcibios_setup_bridge(struct pci_bus *bus, unsigned long type)
{
}

static void pci_setup_bridge(struct pci_bus *bus)
{
	unsigned long type = IORESOURCE_IO | IORESOURCE_MEM |
				  IORESOURCE_PREFETCH;

	pcibios_setup_bridge(bus, type);
	__pci_setup_bridge(bus, type);
}


int pci_claim_bridge_resource(struct pci_dev *bridge, int i)
{
	if (i < PCI_BRIDGE_RESOURCES || i > PCI_BRIDGE_RESOURCE_END)
		return 0;

	if (pci_claim_resource(bridge, i) == 0)
		return 0;	/* Claimed the window */

	if ((bridge->class >> 8) != PCI_CLASS_BRIDGE_PCI)
		return 0;

	if (!pci_bus_clip_resource(bridge, i))
		return -EINVAL;	/* Clipping didn't change anything */

	switch (i) {
	case PCI_BRIDGE_IO_WINDOW:
		pci_setup_bridge_io(bridge);
		break;
	case PCI_BRIDGE_MEM_WINDOW:
		pci_setup_bridge_mmio(bridge);
		break;
	case PCI_BRIDGE_PREF_MEM_WINDOW:
		pci_setup_bridge_mmio_pref(bridge);
		break;
	default:
		return -EINVAL;
	}

	if (pci_claim_resource(bridge, i) == 0)
		return 0;	/* Claimed a smaller window */

	return -EINVAL;
}

/*
 * Check whether the bridge supports optional I/O and prefetchable memory
 * ranges.  If not, the respective base/limit registers must be read-only
 * and read as 0.
 */
static void pci_bridge_check_ranges(struct pci_bus *bus)
{
	struct pci_dev *bridge = bus->self;
	struct resource *b_res;

	b_res = &bridge->resource[PCI_BRIDGE_MEM_WINDOW];
	b_res->flags |= IORESOURCE_MEM;

	if (bridge->io_window) {
		b_res = &bridge->resource[PCI_BRIDGE_IO_WINDOW];
		b_res->flags |= IORESOURCE_IO;
	}

	if (bridge->pref_window) {
		b_res = &bridge->resource[PCI_BRIDGE_PREF_MEM_WINDOW];
		b_res->flags |= IORESOURCE_MEM | IORESOURCE_PREFETCH;
		if (bridge->pref_64_window) {
			b_res->flags |= IORESOURCE_MEM_64 |
					PCI_PREF_RANGE_TYPE_64;
		}
	}
}

/*
 * Helper function for sizing routines.  Assigned resources have non-NULL
 * parent resource.
 *
 * Return first unassigned resource of the correct type.  If there is none,
 * return first assigned resource of the correct type.  If none of the
 * above, return NULL.
 *
 * Returning an assigned resource of the correct type allows the caller to
 * distinguish between already assigned and no resource of the correct type.
 */
static struct resource *find_bus_resource_of_type(struct pci_bus *bus,
						  unsigned long type_mask,
						  unsigned long type)
{
	struct resource *r, *r_assigned = NULL;

	pci_bus_for_each_resource(bus, r) {
		if (r == &ioport_resource || r == &iomem_resource)
			continue;
		if (r && (r->flags & type_mask) == type && !r->parent)
			return r;
		if (r && (r->flags & type_mask) == type && !r_assigned)
			r_assigned = r;
	}
	return r_assigned;
}

static resource_size_t calculate_iosize(resource_size_t size,
					resource_size_t min_size,
					resource_size_t size1,
					resource_size_t add_size,
					resource_size_t children_add_size,
					resource_size_t old_size,
					resource_size_t align)
{
	if (size < min_size)
		size = min_size;
	if (old_size == 1)
		old_size = 0;
	/*
	 * To be fixed in 2.5: we should have sort of HAVE_ISA flag in the
	 * struct pci_bus.
	 */
#if defined(CONFIG_ISA) || defined(CONFIG_EISA)
	size = (size & 0xff) + ((size & ~0xffUL) << 2);
#endif
	size = size + size1;

	size = max(size, add_size) + children_add_size;
	return ALIGN(max(size, old_size), align);
}

static resource_size_t calculate_memsize(resource_size_t size,
					 resource_size_t min_size,
					 resource_size_t add_size,
					 resource_size_t children_add_size,
					 resource_size_t old_size,
					 resource_size_t align)
{
	if (size < min_size)
		size = min_size;
	if (old_size == 1)
		old_size = 0;

	size = max(size, add_size) + children_add_size;
	return ALIGN(max(size, old_size), align);
}

resource_size_t __weak pcibios_window_alignment(struct pci_bus *bus,
						unsigned long type)
{
	return 1;
}

#define PCI_P2P_DEFAULT_MEM_ALIGN	SZ_1M
#define PCI_P2P_DEFAULT_IO_ALIGN	SZ_4K
#define PCI_P2P_DEFAULT_IO_ALIGN_1K	SZ_1K

static resource_size_t window_alignment(struct pci_bus *bus, unsigned long type)
{
	resource_size_t align = 1, arch_align;

	if (type & IORESOURCE_MEM)
		align = PCI_P2P_DEFAULT_MEM_ALIGN;
	else if (type & IORESOURCE_IO) {
		/*
		 * Per spec, I/O windows are 4K-aligned, but some bridges have
		 * an extension to support 1K alignment.
		 */
		if (bus->self && bus->self->io_window_1k)
			align = PCI_P2P_DEFAULT_IO_ALIGN_1K;
		else
			align = PCI_P2P_DEFAULT_IO_ALIGN;
	}

	arch_align = pcibios_window_alignment(bus, type);
	return max(align, arch_align);
}

/**
 * pbus_size_io() - Size the I/O window of a given bus
 *
 * @bus:		The bus
 * @min_size:		The minimum I/O window that must be allocated
 * @add_size:		Additional optional I/O window
 * @realloc_head:	Track the additional I/O window on this list
 *
 * Sizing the I/O windows of the PCI-PCI bridge is trivial, since these
 * windows have 1K or 4K granularity and the I/O ranges of non-bridge PCI
 * devices are limited to 256 bytes.  We must be careful with the ISA
 * aliasing though.
 */
static void pbus_size_io(struct pci_bus *bus, resource_size_t min_size,
			 resource_size_t add_size,
			 struct list_head *realloc_head)
{
	struct pci_dev *dev;
	struct resource *b_res = find_bus_resource_of_type(bus, IORESOURCE_IO,
							   IORESOURCE_IO);
	resource_size_t size = 0, size0 = 0, size1 = 0;
	resource_size_t children_add_size = 0;
	resource_size_t min_align, align;

	if (!b_res)
		return;

	/* If resource is already assigned, nothing more to do */
	if (b_res->parent)
		return;

	min_align = window_alignment(bus, IORESOURCE_IO);
	list_for_each_entry(dev, &bus->devices, bus_list) {
		struct resource *r;

		pci_dev_for_each_resource(dev, r) {
			unsigned long r_size;

			if (r->parent || !(r->flags & IORESOURCE_IO))
				continue;
			r_size = resource_size(r);

			if (r_size < SZ_1K)
				/* Might be re-aligned for ISA */
				size += r_size;
			else
				size1 += r_size;

			align = pci_resource_alignment(dev, r);
			if (align > min_align)
				min_align = align;

			if (realloc_head)
				children_add_size += get_res_add_size(realloc_head, r);
		}
	}

	size0 = calculate_iosize(size, min_size, size1, 0, 0,
			resource_size(b_res), min_align);

	size1 = size0;
	if (realloc_head && (add_size > 0 || children_add_size > 0)) {
		size1 = calculate_iosize(size, min_size, size1, add_size,
					 children_add_size, resource_size(b_res),
					 min_align);
	}

	if (!size0 && !size1) {
		if (bus->self && (b_res->start || b_res->end))
			pci_info(bus->self, "disabling bridge window %pR to %pR (unused)\n",
				 b_res, &bus->busn_res);
		b_res->flags = 0;
		return;
	}

	resource_set_range(b_res, min_align, size0);
	b_res->flags |= IORESOURCE_STARTALIGN;
	if (bus->self && size1 > size0 && realloc_head) {
		add_to_list(realloc_head, bus->self, b_res, size1-size0,
			    min_align);
		pci_info(bus->self, "bridge window %pR to %pR add_size %llx\n",
			 b_res, &bus->busn_res,
			 (unsigned long long) size1 - size0);
	}
}

static inline resource_size_t calculate_mem_align(resource_size_t *aligns,
						  int max_order)
{
	resource_size_t align = 0;
	resource_size_t min_align = 0;
	int order;

	for (order = 0; order <= max_order; order++) {
		resource_size_t align1 = 1;

		align1 <<= order + __ffs(SZ_1M);

		if (!align)
			min_align = align1;
		else if (ALIGN(align + min_align, min_align) < align1)
			min_align = align1 >> 1;
		align += aligns[order];
	}

	return min_align;
}

/**
 * pbus_upstream_space_available - Check no upstream resource limits allocation
 * @bus:	The bus
 * @mask:	Mask the resource flag, then compare it with type
 * @type:	The type of resource from bridge
 * @size:	The size required from the bridge window
 * @align:	Required alignment for the resource
 *
 * Checks that @size can fit inside the upstream bridge resources that are
 * already assigned.
 *
 * Return: %true if enough space is available on all assigned upstream
 * resources.
 */
static bool pbus_upstream_space_available(struct pci_bus *bus, unsigned long mask,
					  unsigned long type, resource_size_t size,
					  resource_size_t align)
{
	struct resource_constraint constraint = {
		.max = RESOURCE_SIZE_MAX,
		.align = align,
	};
	struct pci_bus *downstream = bus;
	struct resource *r;

	while ((bus = bus->parent)) {
		if (pci_is_root_bus(bus))
			break;

		pci_bus_for_each_resource(bus, r) {
			if (!r || !r->parent || (r->flags & mask) != type)
				continue;

			if (resource_size(r) >= size) {
				struct resource gap = {};

				if (find_resource_space(r, &gap, size, &constraint) == 0) {
					gap.flags = type;
					pci_dbg(bus->self,
						"Assigned bridge window %pR to %pR free space at %pR\n",
						r, &bus->busn_res, &gap);
					return true;
				}
			}

			if (bus->self) {
				pci_info(bus->self,
					 "Assigned bridge window %pR to %pR cannot fit 0x%llx required for %s bridging to %pR\n",
					 r, &bus->busn_res,
					 (unsigned long long)size,
					 pci_name(downstream->self),
					 &downstream->busn_res);
			}

			return false;
		}
	}

	return true;
}

/**
 * pbus_size_mem() - Size the memory window of a given bus
 *
 * @bus:		The bus
 * @mask:		Mask the resource flag, then compare it with type
 * @type:		The type of free resource from bridge
 * @type2:		Second match type
 * @type3:		Third match type
 * @min_size:		The minimum memory window that must be allocated
 * @add_size:		Additional optional memory window
 * @realloc_head:	Track the additional memory window on this list
 *
 * Calculate the size of the bus and minimal alignment which guarantees
 * that all child resources fit in this size.
 *
 * Return -ENOSPC if there's no available bus resource of the desired
 * type.  Otherwise, set the bus resource start/end to indicate the
 * required size, add things to realloc_head (if supplied), and return 0.
 */
static int pbus_size_mem(struct pci_bus *bus, unsigned long mask,
			 unsigned long type, unsigned long type2,
			 unsigned long type3, resource_size_t min_size,
			 resource_size_t add_size,
			 struct list_head *realloc_head)
{
	struct pci_dev *dev;
	resource_size_t min_align, win_align, align, size, size0, size1 = 0;
	resource_size_t aligns[28]; /* Alignments from 1MB to 128TB */
	int order, max_order;
	struct resource *b_res = find_bus_resource_of_type(bus,
					mask | IORESOURCE_PREFETCH, type);
	resource_size_t children_add_size = 0;
	resource_size_t children_add_align = 0;
	resource_size_t add_align = 0;

	if (!b_res)
		return -ENOSPC;

	/* If resource is already assigned, nothing more to do */
	if (b_res->parent)
		return 0;

	memset(aligns, 0, sizeof(aligns));
	max_order = 0;
	size = 0;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		struct resource *r;
		int i;

		pci_dev_for_each_resource(dev, r, i) {
			const char *r_name = pci_resource_name(dev, i);
			resource_size_t r_size;

			if (r->parent || (r->flags & IORESOURCE_PCI_FIXED) ||
			    ((r->flags & mask) != type &&
			     (r->flags & mask) != type2 &&
			     (r->flags & mask) != type3))
				continue;
			r_size = resource_size(r);

			/* Put SRIOV requested res to the optional list */
			if (realloc_head && pci_resource_is_optional(dev, i)) {
				add_align = max(pci_resource_alignment(dev, r), add_align);
				add_to_list(realloc_head, dev, r, 0, 0 /* Don't care */);
				children_add_size += r_size;
				continue;
			}

			/*
			 * aligns[0] is for 1MB (since bridge memory
			 * windows are always at least 1MB aligned), so
			 * keep "order" from being negative for smaller
			 * resources.
			 */
			align = pci_resource_alignment(dev, r);
			order = __ffs(align) - __ffs(SZ_1M);
			if (order < 0)
				order = 0;
			if (order >= ARRAY_SIZE(aligns)) {
				pci_warn(dev, "%s %pR: disabling; bad alignment %#llx\n",
					 r_name, r, (unsigned long long) align);
				r->flags = 0;
				continue;
			}
			size += max(r_size, align);
			/*
			 * Exclude ranges with size > align from calculation of
			 * the alignment.
			 */
			if (r_size <= align)
				aligns[order] += align;
			if (order > max_order)
				max_order = order;

			if (realloc_head) {
				children_add_size += get_res_add_size(realloc_head, r);
				children_add_align = get_res_add_align(realloc_head, r);
				add_align = max(add_align, children_add_align);
			}
		}
	}

	win_align = window_alignment(bus, b_res->flags);
	min_align = calculate_mem_align(aligns, max_order);
	min_align = max(min_align, win_align);
	size0 = calculate_memsize(size, min_size, 0, 0, resource_size(b_res), min_align);

	if (bus->self && size0 &&
	    !pbus_upstream_space_available(bus, mask | IORESOURCE_PREFETCH, type,
					   size0, min_align)) {
		min_align = 1ULL << (max_order + __ffs(SZ_1M));
		min_align = max(min_align, win_align);
		size0 = calculate_memsize(size, min_size, 0, 0, resource_size(b_res), win_align);
		pci_info(bus->self, "bridge window %pR to %pR requires relaxed alignment rules\n",
			 b_res, &bus->busn_res);
	}

	if (realloc_head && (add_size > 0 || children_add_size > 0)) {
		add_align = max(min_align, add_align);
		size1 = calculate_memsize(size, min_size, add_size, children_add_size,
					  resource_size(b_res), add_align);

		if (bus->self && size1 &&
		    !pbus_upstream_space_available(bus, mask | IORESOURCE_PREFETCH, type,
						   size1, add_align)) {
			min_align = 1ULL << (max_order + __ffs(SZ_1M));
			min_align = max(min_align, win_align);
			size1 = calculate_memsize(size, min_size, add_size, children_add_size,
						  resource_size(b_res), win_align);
			pci_info(bus->self,
				 "bridge window %pR to %pR requires relaxed alignment rules\n",
				 b_res, &bus->busn_res);
		}
	}

	if (!size0 && !size1) {
		if (bus->self && (b_res->start || b_res->end))
			pci_info(bus->self, "disabling bridge window %pR to %pR (unused)\n",
				 b_res, &bus->busn_res);
		b_res->flags = 0;
		return 0;
	}

	resource_set_range(b_res, min_align, size0);
	b_res->flags |= IORESOURCE_STARTALIGN;
	if (bus->self && size1 > size0 && realloc_head) {
		add_to_list(realloc_head, bus->self, b_res, size1-size0, add_align);
		pci_info(bus->self, "bridge window %pR to %pR add_size %llx add_align %llx\n",
			   b_res, &bus->busn_res,
			   (unsigned long long) (size1 - size0),
			   (unsigned long long) add_align);
	}
	return 0;
}

unsigned long pci_cardbus_resource_alignment(struct resource *res)
{
	if (res->flags & IORESOURCE_IO)
		return pci_cardbus_io_size;
	if (res->flags & IORESOURCE_MEM)
		return pci_cardbus_mem_size;
	return 0;
}

static void pci_bus_size_cardbus(struct pci_bus *bus,
				 struct list_head *realloc_head)
{
	struct pci_dev *bridge = bus->self;
	struct resource *b_res;
	resource_size_t b_res_3_size = pci_cardbus_mem_size * 2;
	u16 ctrl;

	b_res = &bridge->resource[PCI_CB_BRIDGE_IO_0_WINDOW];
	if (b_res->parent)
		goto handle_b_res_1;
	/*
	 * Reserve some resources for CardBus.  We reserve a fixed amount
	 * of bus space for CardBus bridges.
	 */
	resource_set_range(b_res, pci_cardbus_io_size, pci_cardbus_io_size);
	b_res->flags |= IORESOURCE_IO | IORESOURCE_STARTALIGN;
	if (realloc_head) {
		b_res->end -= pci_cardbus_io_size;
		add_to_list(realloc_head, bridge, b_res, pci_cardbus_io_size,
			    pci_cardbus_io_size);
	}

handle_b_res_1:
	b_res = &bridge->resource[PCI_CB_BRIDGE_IO_1_WINDOW];
	if (b_res->parent)
		goto handle_b_res_2;
	resource_set_range(b_res, pci_cardbus_io_size, pci_cardbus_io_size);
	b_res->flags |= IORESOURCE_IO | IORESOURCE_STARTALIGN;
	if (realloc_head) {
		b_res->end -= pci_cardbus_io_size;
		add_to_list(realloc_head, bridge, b_res, pci_cardbus_io_size,
			    pci_cardbus_io_size);
	}

handle_b_res_2:
	/* MEM1 must not be pref MMIO */
	pci_read_config_word(bridge, PCI_CB_BRIDGE_CONTROL, &ctrl);
	if (ctrl & PCI_CB_BRIDGE_CTL_PREFETCH_MEM1) {
		ctrl &= ~PCI_CB_BRIDGE_CTL_PREFETCH_MEM1;
		pci_write_config_word(bridge, PCI_CB_BRIDGE_CONTROL, ctrl);
		pci_read_config_word(bridge, PCI_CB_BRIDGE_CONTROL, &ctrl);
	}

	/* Check whether prefetchable memory is supported by this bridge. */
	pci_read_config_word(bridge, PCI_CB_BRIDGE_CONTROL, &ctrl);
	if (!(ctrl & PCI_CB_BRIDGE_CTL_PREFETCH_MEM0)) {
		ctrl |= PCI_CB_BRIDGE_CTL_PREFETCH_MEM0;
		pci_write_config_word(bridge, PCI_CB_BRIDGE_CONTROL, ctrl);
		pci_read_config_word(bridge, PCI_CB_BRIDGE_CONTROL, &ctrl);
	}

	b_res = &bridge->resource[PCI_CB_BRIDGE_MEM_0_WINDOW];
	if (b_res->parent)
		goto handle_b_res_3;
	/*
	 * If we have prefetchable memory support, allocate two regions.
	 * Otherwise, allocate one region of twice the size.
	 */
	if (ctrl & PCI_CB_BRIDGE_CTL_PREFETCH_MEM0) {
		resource_set_range(b_res, pci_cardbus_mem_size,
				   pci_cardbus_mem_size);
		b_res->flags |= IORESOURCE_MEM | IORESOURCE_PREFETCH |
				    IORESOURCE_STARTALIGN;
		if (realloc_head) {
			b_res->end -= pci_cardbus_mem_size;
			add_to_list(realloc_head, bridge, b_res,
				    pci_cardbus_mem_size, pci_cardbus_mem_size);
		}

		/* Reduce that to half */
		b_res_3_size = pci_cardbus_mem_size;
	}

handle_b_res_3:
	b_res = &bridge->resource[PCI_CB_BRIDGE_MEM_1_WINDOW];
	if (b_res->parent)
		goto handle_done;
	resource_set_range(b_res, pci_cardbus_mem_size, b_res_3_size);
	b_res->flags |= IORESOURCE_MEM | IORESOURCE_STARTALIGN;
	if (realloc_head) {
		b_res->end -= b_res_3_size;
		add_to_list(realloc_head, bridge, b_res, b_res_3_size,
			    pci_cardbus_mem_size);
	}

handle_done:
	;
}

void __pci_bus_size_bridges(struct pci_bus *bus, struct list_head *realloc_head)
{
	struct pci_dev *dev;
	unsigned long mask, prefmask, type2 = 0, type3 = 0;
	resource_size_t additional_io_size = 0, additional_mmio_size = 0,
			additional_mmio_pref_size = 0;
	struct resource *pref;
	struct pci_host_bridge *host;
	int hdr_type, ret;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		struct pci_bus *b = dev->subordinate;
		if (!b)
			continue;

		switch (dev->hdr_type) {
		case PCI_HEADER_TYPE_CARDBUS:
			pci_bus_size_cardbus(b, realloc_head);
			break;

		case PCI_HEADER_TYPE_BRIDGE:
		default:
			__pci_bus_size_bridges(b, realloc_head);
			break;
		}
	}

	/* The root bus? */
	if (pci_is_root_bus(bus)) {
		host = to_pci_host_bridge(bus->bridge);
		if (!host->size_windows)
			return;
		pci_bus_for_each_resource(bus, pref)
			if (pref && (pref->flags & IORESOURCE_PREFETCH))
				break;
		hdr_type = -1;	/* Intentionally invalid - not a PCI device. */
	} else {
		pref = &bus->self->resource[PCI_BRIDGE_PREF_MEM_WINDOW];
		hdr_type = bus->self->hdr_type;
	}

	switch (hdr_type) {
	case PCI_HEADER_TYPE_CARDBUS:
		/* Don't size CardBuses yet */
		break;

	case PCI_HEADER_TYPE_BRIDGE:
		pci_bridge_check_ranges(bus);
		if (bus->self->is_hotplug_bridge) {
			additional_io_size  = pci_hotplug_io_size;
			additional_mmio_size = pci_hotplug_mmio_size;
			additional_mmio_pref_size = pci_hotplug_mmio_pref_size;
		}
		fallthrough;
	default:
		pbus_size_io(bus, realloc_head ? 0 : additional_io_size,
			     additional_io_size, realloc_head);

		/*
		 * If there's a 64-bit prefetchable MMIO window, compute
		 * the size required to put all 64-bit prefetchable
		 * resources in it.
		 */
		mask = IORESOURCE_MEM;
		prefmask = IORESOURCE_MEM | IORESOURCE_PREFETCH;
		if (pref && (pref->flags & IORESOURCE_MEM_64)) {
			prefmask |= IORESOURCE_MEM_64;
			ret = pbus_size_mem(bus, prefmask, prefmask,
				prefmask, prefmask,
				realloc_head ? 0 : additional_mmio_pref_size,
				additional_mmio_pref_size, realloc_head);

			/*
			 * If successful, all non-prefetchable resources
			 * and any 32-bit prefetchable resources will go in
			 * the non-prefetchable window.
			 */
			if (ret == 0) {
				mask = prefmask;
				type2 = prefmask & ~IORESOURCE_MEM_64;
				type3 = prefmask & ~IORESOURCE_PREFETCH;
			}
		}

		/*
		 * If there is no 64-bit prefetchable window, compute the
		 * size required to put all prefetchable resources in the
		 * 32-bit prefetchable window (if there is one).
		 */
		if (!type2) {
			prefmask &= ~IORESOURCE_MEM_64;
			ret = pbus_size_mem(bus, prefmask, prefmask,
				prefmask, prefmask,
				realloc_head ? 0 : additional_mmio_pref_size,
				additional_mmio_pref_size, realloc_head);

			/*
			 * If successful, only non-prefetchable resources
			 * will go in the non-prefetchable window.
			 */
			if (ret == 0)
				mask = prefmask;
			else
				additional_mmio_size += additional_mmio_pref_size;

			type2 = type3 = IORESOURCE_MEM;
		}

		/*
		 * Compute the size required to put everything else in the
		 * non-prefetchable window. This includes:
		 *
		 *   - all non-prefetchable resources
		 *   - 32-bit prefetchable resources if there's a 64-bit
		 *     prefetchable window or no prefetchable window at all
		 *   - 64-bit prefetchable resources if there's no prefetchable
		 *     window at all
		 *
		 * Note that the strategy in __pci_assign_resource() must match
		 * that used here. Specifically, we cannot put a 32-bit
		 * prefetchable resource in a 64-bit prefetchable window.
		 */
		pbus_size_mem(bus, mask, IORESOURCE_MEM, type2, type3,
			      realloc_head ? 0 : additional_mmio_size,
			      additional_mmio_size, realloc_head);
		break;
	}
}

void pci_bus_size_bridges(struct pci_bus *bus)
{
	__pci_bus_size_bridges(bus, NULL);
}
EXPORT_SYMBOL(pci_bus_size_bridges);

static void assign_fixed_resource_on_bus(struct pci_bus *b, struct resource *r)
{
	struct resource *parent_r;
	unsigned long mask = IORESOURCE_IO | IORESOURCE_MEM |
			     IORESOURCE_PREFETCH;

	pci_bus_for_each_resource(b, parent_r) {
		if (!parent_r)
			continue;

		if ((r->flags & mask) == (parent_r->flags & mask) &&
		    resource_contains(parent_r, r))
			request_resource(parent_r, r);
	}
}

/*
 * Try to assign any resources marked as IORESOURCE_PCI_FIXED, as they are
 * skipped by pbus_assign_resources_sorted().
 */
static void pdev_assign_fixed_resources(struct pci_dev *dev)
{
	struct resource *r;

	pci_dev_for_each_resource(dev, r) {
		struct pci_bus *b;

		if (r->parent || !(r->flags & IORESOURCE_PCI_FIXED) ||
		    !(r->flags & (IORESOURCE_IO | IORESOURCE_MEM)))
			continue;

		b = dev->bus;
		while (b && !r->parent) {
			assign_fixed_resource_on_bus(b, r);
			b = b->parent;
		}
	}
}

void __pci_bus_assign_resources(const struct pci_bus *bus,
				struct list_head *realloc_head,
				struct list_head *fail_head)
{
	struct pci_bus *b;
	struct pci_dev *dev;

	pbus_assign_resources_sorted(bus, realloc_head, fail_head);

	list_for_each_entry(dev, &bus->devices, bus_list) {
		pdev_assign_fixed_resources(dev);

		b = dev->subordinate;
		if (!b)
			continue;

		__pci_bus_assign_resources(b, realloc_head, fail_head);

		switch (dev->hdr_type) {
		case PCI_HEADER_TYPE_BRIDGE:
			if (!pci_is_enabled(dev))
				pci_setup_bridge(b);
			break;

		case PCI_HEADER_TYPE_CARDBUS:
			pci_setup_cardbus(b);
			break;

		default:
			pci_info(dev, "not setting up bridge for bus %04x:%02x\n",
				 pci_domain_nr(b), b->number);
			break;
		}
	}
}

void pci_bus_assign_resources(const struct pci_bus *bus)
{
	__pci_bus_assign_resources(bus, NULL, NULL);
}
EXPORT_SYMBOL(pci_bus_assign_resources);

static void pci_claim_device_resources(struct pci_dev *dev)
{
	int i;

	for (i = 0; i < PCI_BRIDGE_RESOURCES; i++) {
		struct resource *r = &dev->resource[i];

		if (!r->flags || r->parent)
			continue;

		pci_claim_resource(dev, i);
	}
}

static void pci_claim_bridge_resources(struct pci_dev *dev)
{
	int i;

	for (i = PCI_BRIDGE_RESOURCES; i < PCI_NUM_RESOURCES; i++) {
		struct resource *r = &dev->resource[i];

		if (!r->flags || r->parent)
			continue;

		pci_claim_bridge_resource(dev, i);
	}
}

static void pci_bus_allocate_dev_resources(struct pci_bus *b)
{
	struct pci_dev *dev;
	struct pci_bus *child;

	list_for_each_entry(dev, &b->devices, bus_list) {
		pci_claim_device_resources(dev);

		child = dev->subordinate;
		if (child)
			pci_bus_allocate_dev_resources(child);
	}
}

static void pci_bus_allocate_resources(struct pci_bus *b)
{
	struct pci_bus *child;

	/*
	 * Carry out a depth-first search on the PCI bus tree to allocate
	 * bridge apertures.  Read the programmed bridge bases and
	 * recursively claim the respective bridge resources.
	 */
	if (b->self) {
		pci_read_bridge_bases(b);
		pci_claim_bridge_resources(b->self);
	}

	list_for_each_entry(child, &b->children, node)
		pci_bus_allocate_resources(child);
}

void pci_bus_claim_resources(struct pci_bus *b)
{
	pci_bus_allocate_resources(b);
	pci_bus_allocate_dev_resources(b);
}
EXPORT_SYMBOL(pci_bus_claim_resources);

static void __pci_bridge_assign_resources(const struct pci_dev *bridge,
					  struct list_head *add_head,
					  struct list_head *fail_head)
{
	struct pci_bus *b;

	pdev_assign_resources_sorted((struct pci_dev *)bridge,
					 add_head, fail_head);

	b = bridge->subordinate;
	if (!b)
		return;

	__pci_bus_assign_resources(b, add_head, fail_head);

	switch (bridge->class >> 8) {
	case PCI_CLASS_BRIDGE_PCI:
		pci_setup_bridge(b);
		break;

	case PCI_CLASS_BRIDGE_CARDBUS:
		pci_setup_cardbus(b);
		break;

	default:
		pci_info(bridge, "not setting up bridge for bus %04x:%02x\n",
			 pci_domain_nr(b), b->number);
		break;
	}
}

#define PCI_RES_TYPE_MASK \
	(IORESOURCE_IO | IORESOURCE_MEM | IORESOURCE_PREFETCH |\
	 IORESOURCE_MEM_64)

static void pci_bridge_release_resources(struct pci_bus *bus,
					 unsigned long type)
{
	struct pci_dev *dev = bus->self;
	struct resource *r;
	unsigned int old_flags;
	struct resource *b_res;
	int idx = 1;

	b_res = &dev->resource[PCI_BRIDGE_RESOURCES];

	/*
	 * 1. If IO port assignment fails, release bridge IO port.
	 * 2. If non pref MMIO assignment fails, release bridge nonpref MMIO.
	 * 3. If 64bit pref MMIO assignment fails, and bridge pref is 64bit,
	 *    release bridge pref MMIO.
	 * 4. If pref MMIO assignment fails, and bridge pref is 32bit,
	 *    release bridge pref MMIO.
	 * 5. If pref MMIO assignment fails, and bridge pref is not
	 *    assigned, release bridge nonpref MMIO.
	 */
	if (type & IORESOURCE_IO)
		idx = 0;
	else if (!(type & IORESOURCE_PREFETCH))
		idx = 1;
	else if ((type & IORESOURCE_MEM_64) &&
		 (b_res[2].flags & IORESOURCE_MEM_64))
		idx = 2;
	else if (!(b_res[2].flags & IORESOURCE_MEM_64) &&
		 (b_res[2].flags & IORESOURCE_PREFETCH))
		idx = 2;
	else
		idx = 1;

	r = &b_res[idx];

	if (!r->parent)
		return;

	/* If there are children, release them all */
	release_child_resources(r);
	if (!release_resource(r)) {
		type = old_flags = r->flags & PCI_RES_TYPE_MASK;
		pci_info(dev, "resource %d %pR released\n",
			 PCI_BRIDGE_RESOURCES + idx, r);
		/* Keep the old size */
		resource_set_range(r, 0, resource_size(r));
		r->flags = 0;

		/* Avoiding touch the one without PREF */
		if (type & IORESOURCE_PREFETCH)
			type = IORESOURCE_PREFETCH;
		__pci_setup_bridge(bus, type);
		/* For next child res under same bridge */
		r->flags = old_flags;
	}
}

enum release_type {
	leaf_only,
	whole_subtree,
};

/*
 * Try to release PCI bridge resources from leaf bridge, so we can allocate
 * a larger window later.
 */
static void pci_bus_release_bridge_resources(struct pci_bus *bus,
					     unsigned long type,
					     enum release_type rel_type)
{
	struct pci_dev *dev;
	bool is_leaf_bridge = true;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		struct pci_bus *b = dev->subordinate;
		if (!b)
			continue;

		is_leaf_bridge = false;

		if ((dev->class >> 8) != PCI_CLASS_BRIDGE_PCI)
			continue;

		if (rel_type == whole_subtree)
			pci_bus_release_bridge_resources(b, type,
						 whole_subtree);
	}

	if (pci_is_root_bus(bus))
		return;

	if ((bus->self->class >> 8) != PCI_CLASS_BRIDGE_PCI)
		return;

	if ((rel_type == whole_subtree) || is_leaf_bridge)
		pci_bridge_release_resources(bus, type);
}

static void pci_bus_dump_res(struct pci_bus *bus)
{
	struct resource *res;
	int i;

	pci_bus_for_each_resource(bus, res, i) {
		if (!res || !res->end || !res->flags)
			continue;

		dev_info(&bus->dev, "resource %d %pR\n", i, res);
	}
}

static void pci_bus_dump_resources(struct pci_bus *bus)
{
	struct pci_bus *b;
	struct pci_dev *dev;


	pci_bus_dump_res(bus);

	list_for_each_entry(dev, &bus->devices, bus_list) {
		b = dev->subordinate;
		if (!b)
			continue;

		pci_bus_dump_resources(b);
	}
}

static int pci_bus_get_depth(struct pci_bus *bus)
{
	int depth = 0;
	struct pci_bus *child_bus;

	list_for_each_entry(child_bus, &bus->children, node) {
		int ret;

		ret = pci_bus_get_depth(child_bus);
		if (ret + 1 > depth)
			depth = ret + 1;
	}

	return depth;
}

/*
 * -1: undefined, will auto detect later
 *  0: disabled by user
 *  1: disabled by auto detect
 *  2: enabled by user
 *  3: enabled by auto detect
 */
enum enable_type {
	undefined = -1,
	user_disabled,
	auto_disabled,
	user_enabled,
	auto_enabled,
};

static enum enable_type pci_realloc_enable = undefined;
void __init pci_realloc_get_opt(char *str)
{
	if (!strncmp(str, "off", 3))
		pci_realloc_enable = user_disabled;
	else if (!strncmp(str, "on", 2))
		pci_realloc_enable = user_enabled;
}
static bool pci_realloc_enabled(enum enable_type enable)
{
	return enable >= user_enabled;
}

#if defined(CONFIG_PCI_IOV) && defined(CONFIG_PCI_REALLOC_ENABLE_AUTO)
static int iov_resources_unassigned(struct pci_dev *dev, void *data)
{
	int i;
	bool *unassigned = data;

	for (i = 0; i < PCI_SRIOV_NUM_BARS; i++) {
		struct resource *r = &dev->resource[i + PCI_IOV_RESOURCES];
		struct pci_bus_region region;

		/* Not assigned or rejected by kernel? */
		if (!r->flags)
			continue;

		pcibios_resource_to_bus(dev->bus, &region, r);
		if (!region.start) {
			*unassigned = true;
			return 1; /* Return early from pci_walk_bus() */
		}
	}

	return 0;
}

static enum enable_type pci_realloc_detect(struct pci_bus *bus,
					   enum enable_type enable_local)
{
	bool unassigned = false;
	struct pci_host_bridge *host;

	if (enable_local != undefined)
		return enable_local;

	host = pci_find_host_bridge(bus);
	if (host->preserve_config)
		return auto_disabled;

	pci_walk_bus(bus, iov_resources_unassigned, &unassigned);
	if (unassigned)
		return auto_enabled;

	return enable_local;
}
#else
static enum enable_type pci_realloc_detect(struct pci_bus *bus,
					   enum enable_type enable_local)
{
	return enable_local;
}
#endif

static void adjust_bridge_window(struct pci_dev *bridge, struct resource *res,
				 struct list_head *add_list,
				 resource_size_t new_size)
{
	resource_size_t add_size, size = resource_size(res);

	if (res->parent)
		return;

	if (!new_size)
		return;

	if (new_size > size) {
		add_size = new_size - size;
		pci_dbg(bridge, "bridge window %pR extended by %pa\n", res,
			&add_size);
	} else if (new_size < size) {
		add_size = size - new_size;
		pci_dbg(bridge, "bridge window %pR shrunken by %pa\n", res,
			&add_size);
	} else {
		return;
	}

	resource_set_size(res, new_size);

	/* If the resource is part of the add_list, remove it now */
	if (add_list)
		remove_from_list(add_list, res);
}

static void remove_dev_resource(struct resource *avail, struct pci_dev *dev,
				struct resource *res)
{
	resource_size_t size, align, tmp;

	size = resource_size(res);
	if (!size)
		return;

	align = pci_resource_alignment(dev, res);
	align = align ? ALIGN(avail->start, align) - avail->start : 0;
	tmp = align + size;
	avail->start = min(avail->start + tmp, avail->end + 1);
}

static void remove_dev_resources(struct pci_dev *dev, struct resource *io,
				 struct resource *mmio,
				 struct resource *mmio_pref)
{
	struct resource *res;

	pci_dev_for_each_resource(dev, res) {
		if (resource_type(res) == IORESOURCE_IO) {
			remove_dev_resource(io, dev, res);
		} else if (resource_type(res) == IORESOURCE_MEM) {

			/*
			 * Make sure prefetchable memory is reduced from
			 * the correct resource. Specifically we put 32-bit
			 * prefetchable memory in non-prefetchable window
			 * if there is a 64-bit prefetchable window.
			 *
			 * See comments in __pci_bus_size_bridges() for
			 * more information.
			 */
			if ((res->flags & IORESOURCE_PREFETCH) &&
			    ((res->flags & IORESOURCE_MEM_64) ==
			     (mmio_pref->flags & IORESOURCE_MEM_64)))
				remove_dev_resource(mmio_pref, dev, res);
			else
				remove_dev_resource(mmio, dev, res);
		}
	}
}

#define ALIGN_DOWN_IF_NONZERO(addr, align) \
			((align) ? ALIGN_DOWN((addr), (align)) : (addr))

/*
 * io, mmio and mmio_pref contain the total amount of bridge window space
 * available. This includes the minimal space needed to cover all the
 * existing devices on the bus and the possible extra space that can be
 * shared with the bridges.
 */
static void pci_bus_distribute_available_resources(struct pci_bus *bus,
					    struct list_head *add_list,
					    struct resource io,
					    struct resource mmio,
					    struct resource mmio_pref)
{
	unsigned int normal_bridges = 0, hotplug_bridges = 0;
	struct resource *io_res, *mmio_res, *mmio_pref_res;
	struct pci_dev *dev, *bridge = bus->self;
	resource_size_t io_per_b, mmio_per_b, mmio_pref_per_b, align;

	io_res = &bridge->resource[PCI_BRIDGE_IO_WINDOW];
	mmio_res = &bridge->resource[PCI_BRIDGE_MEM_WINDOW];
	mmio_pref_res = &bridge->resource[PCI_BRIDGE_PREF_MEM_WINDOW];

	/*
	 * The alignment of this bridge is yet to be considered, hence it must
	 * be done now before extending its bridge window.
	 */
	align = pci_resource_alignment(bridge, io_res);
	if (!io_res->parent && align)
		io.start = min(ALIGN(io.start, align), io.end + 1);

	align = pci_resource_alignment(bridge, mmio_res);
	if (!mmio_res->parent && align)
		mmio.start = min(ALIGN(mmio.start, align), mmio.end + 1);

	align = pci_resource_alignment(bridge, mmio_pref_res);
	if (!mmio_pref_res->parent && align)
		mmio_pref.start = min(ALIGN(mmio_pref.start, align),
			mmio_pref.end + 1);

	/*
	 * Now that we have adjusted for alignment, update the bridge window
	 * resources to fill as much remaining resource space as possible.
	 */
	adjust_bridge_window(bridge, io_res, add_list, resource_size(&io));
	adjust_bridge_window(bridge, mmio_res, add_list, resource_size(&mmio));
	adjust_bridge_window(bridge, mmio_pref_res, add_list,
			     resource_size(&mmio_pref));

	/*
	 * Calculate how many hotplug bridges and normal bridges there
	 * are on this bus.  We will distribute the additional available
	 * resources between hotplug bridges.
	 */
	for_each_pci_bridge(dev, bus) {
		if (dev->is_hotplug_bridge)
			hotplug_bridges++;
		else
			normal_bridges++;
	}

	if (!(hotplug_bridges + normal_bridges))
		return;

	/*
	 * Calculate the amount of space we can forward from "bus" to any
	 * downstream buses, i.e., the space left over after assigning the
	 * BARs and windows on "bus".
	 */
	list_for_each_entry(dev, &bus->devices, bus_list) {
		if (!dev->is_virtfn)
			remove_dev_resources(dev, &io, &mmio, &mmio_pref);
	}

	/*
	 * If there is at least one hotplug bridge on this bus it gets all
	 * the extra resource space that was left after the reductions
	 * above.
	 *
	 * If there are no hotplug bridges the extra resource space is
	 * split between non-hotplug bridges. This is to allow possible
	 * hotplug bridges below them to get the extra space as well.
	 */
	if (hotplug_bridges) {
		io_per_b = div64_ul(resource_size(&io), hotplug_bridges);
		mmio_per_b = div64_ul(resource_size(&mmio), hotplug_bridges);
		mmio_pref_per_b = div64_ul(resource_size(&mmio_pref),
					   hotplug_bridges);
	} else {
		io_per_b = div64_ul(resource_size(&io), normal_bridges);
		mmio_per_b = div64_ul(resource_size(&mmio), normal_bridges);
		mmio_pref_per_b = div64_ul(resource_size(&mmio_pref),
					   normal_bridges);
	}

	for_each_pci_bridge(dev, bus) {
		struct resource *res;
		struct pci_bus *b;

		b = dev->subordinate;
		if (!b)
			continue;
		if (hotplug_bridges && !dev->is_hotplug_bridge)
			continue;

		res = &dev->resource[PCI_BRIDGE_IO_WINDOW];

		/*
		 * Make sure the split resource space is properly aligned
		 * for bridge windows (align it down to avoid going above
		 * what is available).
		 */
		align = pci_resource_alignment(dev, res);
		resource_set_size(&io, ALIGN_DOWN_IF_NONZERO(io_per_b, align));

		/*
		 * The x_per_b holds the extra resource space that can be
		 * added for each bridge but there is the minimal already
		 * reserved as well so adjust x.start down accordingly to
		 * cover the whole space.
		 */
		io.start -= resource_size(res);

		res = &dev->resource[PCI_BRIDGE_MEM_WINDOW];
		align = pci_resource_alignment(dev, res);
		resource_set_size(&mmio,
				  ALIGN_DOWN_IF_NONZERO(mmio_per_b,align));
		mmio.start -= resource_size(res);

		res = &dev->resource[PCI_BRIDGE_PREF_MEM_WINDOW];
		align = pci_resource_alignment(dev, res);
		resource_set_size(&mmio_pref,
				  ALIGN_DOWN_IF_NONZERO(mmio_pref_per_b, align));
		mmio_pref.start -= resource_size(res);

		pci_bus_distribute_available_resources(b, add_list, io, mmio,
						       mmio_pref);

		io.start += io.end + 1;
		mmio.start += mmio.end + 1;
		mmio_pref.start += mmio_pref.end + 1;
	}
}

static void pci_bridge_distribute_available_resources(struct pci_dev *bridge,
						      struct list_head *add_list)
{
	struct resource available_io, available_mmio, available_mmio_pref;

	if (!bridge->is_hotplug_bridge)
		return;

	pci_dbg(bridge, "distributing available resources\n");

	/* Take the initial extra resources from the hotplug port */
	available_io = bridge->resource[PCI_BRIDGE_IO_WINDOW];
	available_mmio = bridge->resource[PCI_BRIDGE_MEM_WINDOW];
	available_mmio_pref = bridge->resource[PCI_BRIDGE_PREF_MEM_WINDOW];

	pci_bus_distribute_available_resources(bridge->subordinate,
					       add_list, available_io,
					       available_mmio,
					       available_mmio_pref);
}

static bool pci_bridge_resources_not_assigned(struct pci_dev *dev)
{
	const struct resource *r;

	/*
	 * If the child device's resources are not yet assigned it means we
	 * are configuring them (not the boot firmware), so we should be
	 * able to extend the upstream bridge resources in the same way we
	 * do with the normal hotplug case.
	 */
	r = &dev->resource[PCI_BRIDGE_IO_WINDOW];
	if (r->flags && !(r->flags & IORESOURCE_STARTALIGN))
		return false;
	r = &dev->resource[PCI_BRIDGE_MEM_WINDOW];
	if (r->flags && !(r->flags & IORESOURCE_STARTALIGN))
		return false;
	r = &dev->resource[PCI_BRIDGE_PREF_MEM_WINDOW];
	if (r->flags && !(r->flags & IORESOURCE_STARTALIGN))
		return false;

	return true;
}

static void
pci_root_bus_distribute_available_resources(struct pci_bus *bus,
					    struct list_head *add_list)
{
	struct pci_dev *dev, *bridge = bus->self;

	for_each_pci_bridge(dev, bus) {
		struct pci_bus *b;

		b = dev->subordinate;
		if (!b)
			continue;

		/*
		 * Need to check "bridge" here too because it is NULL
		 * in case of root bus.
		 */
		if (bridge && pci_bridge_resources_not_assigned(dev))
			pci_bridge_distribute_available_resources(dev, add_list);
		else
			pci_root_bus_distribute_available_resources(b, add_list);
	}
}

static void pci_prepare_next_assign_round(struct list_head *fail_head,
					  int tried_times,
					  enum release_type rel_type)
{
	struct pci_dev_resource *fail_res;

	pr_info("PCI: No. %d try to assign unassigned res\n", tried_times + 1);

	/*
	 * Try to release leaf bridge's resources that aren't big
	 * enough to contain child device resources.
	 */
	list_for_each_entry(fail_res, fail_head, list) {
		pci_bus_release_bridge_resources(fail_res->dev->bus,
						 fail_res->flags & PCI_RES_TYPE_MASK,
						 rel_type);
	}

	/* Restore size and flags */
	list_for_each_entry(fail_res, fail_head, list) {
		struct resource *res = fail_res->res;
		struct pci_dev *dev = fail_res->dev;
		int idx = pci_resource_num(dev, res);

		restore_dev_resource(fail_res);

		if (!pci_is_bridge(dev))
			continue;

		if (idx >= PCI_BRIDGE_RESOURCES &&
		    idx <= PCI_BRIDGE_RESOURCE_END)
			res->flags = 0;
	}

	free_list(fail_head);
}

/*
 * First try will not touch PCI bridge res.
 * Second and later try will clear small leaf bridge res.
 * Will stop till to the max depth if can not find good one.
 */
void pci_assign_unassigned_root_bus_resources(struct pci_bus *bus)
{
	LIST_HEAD(realloc_head);
	/* List of resources that want additional resources */
	struct list_head *add_list = NULL;
	int tried_times = 0;
	enum release_type rel_type = leaf_only;
	LIST_HEAD(fail_head);
	int pci_try_num = 1;
	enum enable_type enable_local;

	/* Don't realloc if asked to do so */
	enable_local = pci_realloc_detect(bus, pci_realloc_enable);
	if (pci_realloc_enabled(enable_local)) {
		int max_depth = pci_bus_get_depth(bus);

		pci_try_num = max_depth + 1;
		dev_info(&bus->dev, "max bus depth: %d pci_try_num: %d\n",
			 max_depth, pci_try_num);
	}

	while (1) {
		/*
		 * Last try will use add_list, otherwise will try good to
		 * have as must have, so can realloc parent bridge resource
		 */
		if (tried_times + 1 == pci_try_num)
			add_list = &realloc_head;
		/*
		 * Depth first, calculate sizes and alignments of all
		 * subordinate buses.
		 */
		__pci_bus_size_bridges(bus, add_list);

		pci_root_bus_distribute_available_resources(bus, add_list);

		/* Depth last, allocate resources and update the hardware. */
		__pci_bus_assign_resources(bus, add_list, &fail_head);
		if (add_list)
			BUG_ON(!list_empty(add_list));
		tried_times++;

		/* Any device complain? */
		if (list_empty(&fail_head))
			break;

		if (tried_times >= pci_try_num) {
			if (enable_local == undefined) {
				dev_info(&bus->dev,
					 "Some PCI device resources are unassigned, try booting with pci=realloc\n");
			} else if (enable_local == auto_enabled) {
				dev_info(&bus->dev,
					 "Automatically enabled pci realloc, if you have problem, try booting with pci=realloc=off\n");
			}
			free_list(&fail_head);
			break;
		}

		/* Third times and later will not check if it is leaf */
		if (tried_times + 1 > 2)
			rel_type = whole_subtree;

		pci_prepare_next_assign_round(&fail_head, tried_times, rel_type);
	}

	pci_bus_dump_resources(bus);
}

void pci_assign_unassigned_resources(void)
{
	struct pci_bus *root_bus;

	list_for_each_entry(root_bus, &pci_root_buses, node) {
		pci_assign_unassigned_root_bus_resources(root_bus);

		/* Make sure the root bridge has a companion ACPI device */
		if (ACPI_HANDLE(root_bus->bridge))
			acpi_ioapic_add(ACPI_HANDLE(root_bus->bridge));
	}
}

void pci_assign_unassigned_bridge_resources(struct pci_dev *bridge)
{
	struct pci_bus *parent = bridge->subordinate;
	/* List of resources that want additional resources */
	LIST_HEAD(add_list);
	int tried_times = 0;
	LIST_HEAD(fail_head);
	int ret;

	while (1) {
		__pci_bus_size_bridges(parent, &add_list);

		/*
		 * Distribute remaining resources (if any) equally between
		 * hotplug bridges below. This makes it possible to extend
		 * the hierarchy later without running out of resources.
		 */
		pci_bridge_distribute_available_resources(bridge, &add_list);

		__pci_bridge_assign_resources(bridge, &add_list, &fail_head);
		BUG_ON(!list_empty(&add_list));
		tried_times++;

		if (list_empty(&fail_head))
			break;

		if (tried_times >= 2) {
			/* Still fail, don't need to try more */
			free_list(&fail_head);
			break;
		}

		pci_prepare_next_assign_round(&fail_head, tried_times,
					      whole_subtree);
	}

	ret = pci_reenable_device(bridge);
	if (ret)
		pci_err(bridge, "Error reenabling bridge (%d)\n", ret);
	pci_set_master(bridge);
}
EXPORT_SYMBOL_GPL(pci_assign_unassigned_bridge_resources);

int pci_reassign_bridge_resources(struct pci_dev *bridge, unsigned long type)
{
	struct pci_dev_resource *dev_res;
	struct pci_dev *next;
	LIST_HEAD(saved);
	LIST_HEAD(added);
	LIST_HEAD(failed);
	unsigned int i;
	int ret;

	down_read(&pci_bus_sem);

	/* Walk to the root hub, releasing bridge BARs when possible */
	next = bridge;
	do {
		bridge = next;
		for (i = PCI_BRIDGE_RESOURCES; i < PCI_BRIDGE_RESOURCE_END;
		     i++) {
			struct resource *res = &bridge->resource[i];
			const char *res_name = pci_resource_name(bridge, i);

			if ((res->flags ^ type) & PCI_RES_TYPE_MASK)
				continue;

			/* Ignore BARs which are still in use */
			if (res->child)
				continue;

			ret = add_to_list(&saved, bridge, res, 0, 0);
			if (ret)
				goto cleanup;

			pci_info(bridge, "%s %pR: releasing\n", res_name, res);

			if (res->parent)
				release_resource(res);
			res->start = 0;
			res->end = 0;
			break;
		}
		if (i == PCI_BRIDGE_RESOURCE_END)
			break;

		next = bridge->bus ? bridge->bus->self : NULL;
	} while (next);

	if (list_empty(&saved)) {
		up_read(&pci_bus_sem);
		return -ENOENT;
	}

	__pci_bus_size_bridges(bridge->subordinate, &added);
	__pci_bridge_assign_resources(bridge, &added, &failed);
	BUG_ON(!list_empty(&added));

	if (!list_empty(&failed)) {
		ret = -ENOSPC;
		goto cleanup;
	}

	list_for_each_entry(dev_res, &saved, list) {
		/* Skip the bridge we just assigned resources for */
		if (bridge == dev_res->dev)
			continue;

		bridge = dev_res->dev;
		pci_setup_bridge(bridge->subordinate);
	}

	free_list(&saved);
	up_read(&pci_bus_sem);
	return 0;

cleanup:
	/* Restore size and flags */
	list_for_each_entry(dev_res, &failed, list)
		restore_dev_resource(dev_res);
	free_list(&failed);

	/* Revert to the old configuration */
	list_for_each_entry(dev_res, &saved, list) {
		struct resource *res = dev_res->res;

		bridge = dev_res->dev;
		i = pci_resource_num(bridge, res);

		restore_dev_resource(dev_res);

		pci_claim_resource(bridge, i);
		pci_setup_bridge(bridge->subordinate);
	}
	free_list(&saved);
	up_read(&pci_bus_sem);

	return ret;
}

void pci_assign_unassigned_bus_resources(struct pci_bus *bus)
{
	struct pci_dev *dev;
	/* List of resources that want additional resources */
	LIST_HEAD(add_list);

	down_read(&pci_bus_sem);
	for_each_pci_bridge(dev, bus)
		if (pci_has_subordinate(dev))
			__pci_bus_size_bridges(dev->subordinate, &add_list);
	up_read(&pci_bus_sem);
	__pci_bus_assign_resources(bus, &add_list, NULL);
	BUG_ON(!list_empty(&add_list));
}
EXPORT_SYMBOL_GPL(pci_assign_unassigned_bus_resources);
