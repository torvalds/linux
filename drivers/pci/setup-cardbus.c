// SPDX-License-Identifier: GPL-2.0
/*
 * Cardbus bridge setup routines.
 */

#include <linux/bitfield.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/sizes.h>
#include <linux/sprintf.h>
#include <linux/types.h>

#include "pci.h"

#define CARDBUS_LATENCY_TIMER		176	/* secondary latency timer */
#define CARDBUS_RESERVE_BUSNR		3

#define DEFAULT_CARDBUS_IO_SIZE		SZ_256
#define DEFAULT_CARDBUS_MEM_SIZE	SZ_64M
/* pci=cbmemsize=nnM,cbiosize=nn can override this */
static unsigned long pci_cardbus_io_size = DEFAULT_CARDBUS_IO_SIZE;
static unsigned long pci_cardbus_mem_size = DEFAULT_CARDBUS_MEM_SIZE;

unsigned long pci_cardbus_resource_alignment(struct resource *res)
{
	if (res->flags & IORESOURCE_IO)
		return pci_cardbus_io_size;
	if (res->flags & IORESOURCE_MEM)
		return pci_cardbus_mem_size;
	return 0;
}

int pci_bus_size_cardbus_bridge(struct pci_bus *bus,
				struct list_head *realloc_head)
{
	struct pci_dev *bridge = bus->self;
	struct resource *b_res;
	resource_size_t b_res_3_size = pci_cardbus_mem_size * 2;
	u16 ctrl;

	b_res = &bridge->resource[PCI_CB_BRIDGE_IO_0_WINDOW];
	if (resource_assigned(b_res))
		goto handle_b_res_1;
	/*
	 * Reserve some resources for CardBus.  We reserve a fixed amount
	 * of bus space for CardBus bridges.
	 */
	resource_set_range(b_res, pci_cardbus_io_size, pci_cardbus_io_size);
	b_res->flags |= IORESOURCE_IO | IORESOURCE_STARTALIGN;
	if (realloc_head) {
		b_res->end -= pci_cardbus_io_size;
		pci_dev_res_add_to_list(realloc_head, bridge, b_res,
					pci_cardbus_io_size,
					pci_cardbus_io_size);
	}

handle_b_res_1:
	b_res = &bridge->resource[PCI_CB_BRIDGE_IO_1_WINDOW];
	if (resource_assigned(b_res))
		goto handle_b_res_2;
	resource_set_range(b_res, pci_cardbus_io_size, pci_cardbus_io_size);
	b_res->flags |= IORESOURCE_IO | IORESOURCE_STARTALIGN;
	if (realloc_head) {
		b_res->end -= pci_cardbus_io_size;
		pci_dev_res_add_to_list(realloc_head, bridge, b_res,
					pci_cardbus_io_size,
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
	if (resource_assigned(b_res))
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
			pci_dev_res_add_to_list(realloc_head, bridge, b_res,
						pci_cardbus_mem_size,
						pci_cardbus_mem_size);
		}

		/* Reduce that to half */
		b_res_3_size = pci_cardbus_mem_size;
	}

handle_b_res_3:
	b_res = &bridge->resource[PCI_CB_BRIDGE_MEM_1_WINDOW];
	if (resource_assigned(b_res))
		goto handle_done;
	resource_set_range(b_res, pci_cardbus_mem_size, b_res_3_size);
	b_res->flags |= IORESOURCE_MEM | IORESOURCE_STARTALIGN;
	if (realloc_head) {
		b_res->end -= b_res_3_size;
		pci_dev_res_add_to_list(realloc_head, bridge, b_res,
					b_res_3_size, pci_cardbus_mem_size);
	}

handle_done:
	return 0;
}

void pci_setup_cardbus_bridge(struct pci_bus *bus)
{
	struct pci_dev *bridge = bus->self;
	struct resource *res;
	struct pci_bus_region region;

	pci_info(bridge, "CardBus bridge to %pR\n",
		 &bus->busn_res);

	res = bus->resource[0];
	pcibios_resource_to_bus(bridge->bus, &region, res);
	if (resource_assigned(res) && res->flags & IORESOURCE_IO) {
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
	if (resource_assigned(res) && res->flags & IORESOURCE_IO) {
		pci_info(bridge, "  bridge window %pR\n", res);
		pci_write_config_dword(bridge, PCI_CB_IO_BASE_1,
					region.start);
		pci_write_config_dword(bridge, PCI_CB_IO_LIMIT_1,
					region.end);
	}

	res = bus->resource[2];
	pcibios_resource_to_bus(bridge->bus, &region, res);
	if (resource_assigned(res) && res->flags & IORESOURCE_MEM) {
		pci_info(bridge, "  bridge window %pR\n", res);
		pci_write_config_dword(bridge, PCI_CB_MEMORY_BASE_0,
					region.start);
		pci_write_config_dword(bridge, PCI_CB_MEMORY_LIMIT_0,
					region.end);
	}

	res = bus->resource[3];
	pcibios_resource_to_bus(bridge->bus, &region, res);
	if (resource_assigned(res) && res->flags & IORESOURCE_MEM) {
		pci_info(bridge, "  bridge window %pR\n", res);
		pci_write_config_dword(bridge, PCI_CB_MEMORY_BASE_1,
					region.start);
		pci_write_config_dword(bridge, PCI_CB_MEMORY_LIMIT_1,
					region.end);
	}
}
EXPORT_SYMBOL(pci_setup_cardbus_bridge);

int pci_setup_cardbus(char *str)
{
	if (!strncmp(str, "cbiosize=", 9)) {
		pci_cardbus_io_size = memparse(str + 9, &str);
		return 0;
	} else if (!strncmp(str, "cbmemsize=", 10)) {
		pci_cardbus_mem_size = memparse(str + 10, &str);
		return 0;
	}

	return -ENOENT;
}

int pci_cardbus_scan_bridge_extend(struct pci_bus *bus, struct pci_dev *dev,
				   u32 buses, int max,
				   unsigned int available_buses, int pass)
{
	struct pci_bus *child;
	bool fixed_buses;
	u8 fixed_sec, fixed_sub;
	int next_busnr;
	u32 i, j = 0;

	/*
	 * We need to assign a number to this bus which we always do in the
	 * second pass.
	 */
	if (!pass) {
		/*
		 * Temporarily disable forwarding of the configuration
		 * cycles on all bridges in this bus segment to avoid
		 * possible conflicts in the second pass between two bridges
		 * programmed with overlapping bus ranges.
		 */
		pci_write_config_dword(dev, PCI_PRIMARY_BUS,
				       buses & PCI_SEC_LATENCY_TIMER_MASK);
		return max;
	}

	/* Clear errors */
	pci_write_config_word(dev, PCI_STATUS, 0xffff);

	/* Read bus numbers from EA Capability (if present) */
	fixed_buses = pci_ea_fixed_busnrs(dev, &fixed_sec, &fixed_sub);
	if (fixed_buses)
		next_busnr = fixed_sec;
	else
		next_busnr = max + 1;

	/*
	 * Prevent assigning a bus number that already exists. This can
	 * happen when a bridge is hot-plugged, so in this case we only
	 * re-scan this bus.
	 */
	child = pci_find_bus(pci_domain_nr(bus), next_busnr);
	if (!child) {
		child = pci_add_new_bus(bus, dev, next_busnr);
		if (!child)
			return max;
		pci_bus_insert_busn_res(child, next_busnr, bus->busn_res.end);
	}
	max++;
	if (available_buses)
		available_buses--;

	buses = (buses & PCI_SEC_LATENCY_TIMER_MASK) |
		FIELD_PREP(PCI_PRIMARY_BUS_MASK, child->primary) |
		FIELD_PREP(PCI_SECONDARY_BUS_MASK, child->busn_res.start) |
		FIELD_PREP(PCI_SUBORDINATE_BUS_MASK, child->busn_res.end);

	/*
	 * yenta.c forces a secondary latency timer of 176.
	 * Copy that behaviour here.
	 */
	buses &= ~PCI_SEC_LATENCY_TIMER_MASK;
	buses |= FIELD_PREP(PCI_SEC_LATENCY_TIMER_MASK, CARDBUS_LATENCY_TIMER);

	/* We need to blast all three values with a single write */
	pci_write_config_dword(dev, PCI_PRIMARY_BUS, buses);

	/*
	 * For CardBus bridges, we leave 4 bus numbers as cards with a
	 * PCI-to-PCI bridge can be inserted later.
	 */
	for (i = 0; i < CARDBUS_RESERVE_BUSNR; i++) {
		struct pci_bus *parent = bus;

		if (pci_find_bus(pci_domain_nr(bus), max + i + 1))
			break;

		while (parent->parent) {
			if (!pcibios_assign_all_busses() &&
			    (parent->busn_res.end > max) &&
			    (parent->busn_res.end <= max + i)) {
				j = 1;
			}
			parent = parent->parent;
		}
		if (j) {
			/*
			 * Often, there are two CardBus bridges -- try to
			 * leave one valid bus number for each one.
			 */
			i /= 2;
			break;
		}
	}
	max += i;

	/*
	 * Set subordinate bus number to its real value. If fixed
	 * subordinate bus number exists from EA capability then use it.
	 */
	if (fixed_buses)
		max = fixed_sub;
	pci_bus_update_busn_res_end(child, max);
	pci_write_config_byte(dev, PCI_SUBORDINATE_BUS, max);

	scnprintf(child->name, sizeof(child->name), "PCI CardBus %04x:%02x",
		  pci_domain_nr(bus), child->number);

	pbus_validate_busn(child);

	return max;
}
