/*
 * Support for Celleb io workarounds
 *
 * (C) Copyright 2006-2007 TOSHIBA CORPORATION
 *
 * This file is based to arch/powerpc/platform/cell/io-workarounds.c
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

#include <linux/of_device.h>
#include <linux/irq.h>

#include <asm/io.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/ppc-pci.h>

#include "pci.h"

#define MAX_CELLEB_PCI_BUS	4

void *celleb_dummy_page_va;

static struct celleb_pci_bus {
	struct pci_controller *phb;
	void (*dummy_read)(struct pci_controller *);
} celleb_pci_busses[MAX_CELLEB_PCI_BUS];

static int celleb_pci_count = 0;

static struct celleb_pci_bus *celleb_pci_find(unsigned long vaddr,
					      unsigned long paddr)
{
	int i, j;
	struct resource *res;

	for (i = 0; i < celleb_pci_count; i++) {
		struct celleb_pci_bus *bus = &celleb_pci_busses[i];
		struct pci_controller *phb = bus->phb;
		if (paddr)
			for (j = 0; j < 3; j++) {
				res = &phb->mem_resources[j];
				if (paddr >= res->start && paddr <= res->end)
					return bus;
			}
		res = &phb->io_resource;
		if (vaddr && vaddr >= res->start && vaddr <= res->end)
			return bus;
	}
	return NULL;
}

static void celleb_io_flush(const PCI_IO_ADDR addr)
{
	struct celleb_pci_bus *bus;
	int token;

	token = PCI_GET_ADDR_TOKEN(addr);

	if (token && token <= celleb_pci_count)
		bus = &celleb_pci_busses[token - 1];
	else {
		unsigned long vaddr, paddr;
		pte_t *ptep;

		vaddr = (unsigned long)PCI_FIX_ADDR(addr);
		if (vaddr < PHB_IO_BASE || vaddr >= PHB_IO_END)
			return;

		ptep = find_linux_pte(init_mm.pgd, vaddr);
		if (ptep == NULL)
			paddr = 0;
		else
			paddr = pte_pfn(*ptep) << PAGE_SHIFT;
		bus = celleb_pci_find(vaddr, paddr);

		if (bus == NULL)
			return;
	}

	if (bus->dummy_read)
		bus->dummy_read(bus->phb);
}

static u8 celleb_readb(const PCI_IO_ADDR addr)
{
	u8 val;
	val = __do_readb(addr);
	celleb_io_flush(addr);
	return val;
}

static u16 celleb_readw(const PCI_IO_ADDR addr)
{
	u16 val;
	val = __do_readw(addr);
	celleb_io_flush(addr);
	return val;
}

static u32 celleb_readl(const PCI_IO_ADDR addr)
{
	u32 val;
	val = __do_readl(addr);
	celleb_io_flush(addr);
	return val;
}

static u64 celleb_readq(const PCI_IO_ADDR addr)
{
	u64 val;
	val = __do_readq(addr);
	celleb_io_flush(addr);
	return val;
}

static u16 celleb_readw_be(const PCI_IO_ADDR addr)
{
	u16 val;
	val = __do_readw_be(addr);
	celleb_io_flush(addr);
	return val;
}

static u32 celleb_readl_be(const PCI_IO_ADDR addr)
{
	u32 val;
	val = __do_readl_be(addr);
	celleb_io_flush(addr);
	return val;
}

static u64 celleb_readq_be(const PCI_IO_ADDR addr)
{
	u64 val;
	val = __do_readq_be(addr);
	celleb_io_flush(addr);
	return val;
}

static void celleb_readsb(const PCI_IO_ADDR addr,
			  void *buf, unsigned long count)
{
	__do_readsb(addr, buf, count);
	celleb_io_flush(addr);
}

static void celleb_readsw(const PCI_IO_ADDR addr,
			  void *buf, unsigned long count)
{
	__do_readsw(addr, buf, count);
	celleb_io_flush(addr);
}

static void celleb_readsl(const PCI_IO_ADDR addr,
			  void *buf, unsigned long count)
{
	__do_readsl(addr, buf, count);
	celleb_io_flush(addr);
}

static void celleb_memcpy_fromio(void *dest,
				 const PCI_IO_ADDR src,
				 unsigned long n)
{
	__do_memcpy_fromio(dest, src, n);
	celleb_io_flush(src);
}

static void __iomem *celleb_ioremap(unsigned long addr,
				     unsigned long size,
				     unsigned long flags)
{
	struct celleb_pci_bus *bus;
	void __iomem *res = __ioremap(addr, size, flags);
	int busno;

	bus = celleb_pci_find(0, addr);
	if (bus != NULL) {
		busno = bus - celleb_pci_busses;
		PCI_SET_ADDR_TOKEN(res, busno + 1);
	}
	return res;
}

static void celleb_iounmap(volatile void __iomem *addr)
{
	return __iounmap(PCI_FIX_ADDR(addr));
}

static struct ppc_pci_io celleb_pci_io __initdata = {
	.readb = celleb_readb,
	.readw = celleb_readw,
	.readl = celleb_readl,
	.readq = celleb_readq,
	.readw_be = celleb_readw_be,
	.readl_be = celleb_readl_be,
	.readq_be = celleb_readq_be,
	.readsb = celleb_readsb,
	.readsw = celleb_readsw,
	.readsl = celleb_readsl,
	.memcpy_fromio = celleb_memcpy_fromio,
};

void __init celleb_pci_add_one(struct pci_controller *phb,
			       void (*dummy_read)(struct pci_controller *))
{
	struct celleb_pci_bus *bus = &celleb_pci_busses[celleb_pci_count];
	struct device_node *np = phb->arch_data;

	if (celleb_pci_count >= MAX_CELLEB_PCI_BUS) {
		printk(KERN_ERR "Too many pci bridges, workarounds"
		       " disabled for %s\n", np->full_name);
		return;
	}

	celleb_pci_count++;

	bus->phb = phb;
	bus->dummy_read = dummy_read;
}

static struct of_device_id celleb_pci_workaround_match[] __initdata = {
	{
		.name = "pci-pseudo",
		.data = fake_pci_workaround_init,
	}, {
		.name = "epci",
		.data = epci_workaround_init,
	}, {
	},
};

int __init celleb_pci_workaround_init(void)
{
	struct pci_controller *phb;
	struct device_node *node;
	const struct  of_device_id *match;
	void (*init_func)(struct pci_controller *);

	celleb_dummy_page_va = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!celleb_dummy_page_va) {
		printk(KERN_ERR "Celleb: dummy read disabled."
			"Alloc celleb_dummy_page_va failed\n");
		return 1;
	}

	list_for_each_entry(phb, &hose_list, list_node) {
		node = phb->arch_data;
		match = of_match_node(celleb_pci_workaround_match, node);

		if (match) {
			init_func = match->data;
			(*init_func)(phb);
		}
	}

	ppc_pci_io = celleb_pci_io;
	ppc_md.ioremap = celleb_ioremap;
	ppc_md.iounmap = celleb_iounmap;

	return 0;
}
