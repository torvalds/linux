// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Port for PPC64 David Engebretsen, IBM Corp.
 * Contains common pci routines for ppc64 platform, pSeries and iSeries brands.
 * 
 * Copyright (C) 2003 Anton Blanchard <anton@au.ibm.com>, IBM
 *   Rework, based on alpha PCI code.
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/syscalls.h>
#include <linux/irq.h>
#include <linux/vmalloc.h>
#include <linux/of.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/pci-bridge.h>
#include <asm/byteorder.h>
#include <asm/machdep.h>
#include <asm/ppc-pci.h>

/* pci_io_base -- the base address from which io bars are offsets.
 * This is the lowest I/O base address (so bar values are always positive),
 * and it *must* be the start of ISA space if an ISA bus exists because
 * ISA drivers use hard coded offsets.  If no ISA bus exists nothing
 * is mapped on the first 64K of IO space
 */
unsigned long pci_io_base;
EXPORT_SYMBOL(pci_io_base);

static int __init pcibios_init(void)
{
	struct pci_controller *hose, *tmp;

	printk(KERN_INFO "PCI: Probing PCI hardware\n");

	/* For now, override phys_mem_access_prot. If we need it,g
	 * later, we may move that initialization to each ppc_md
	 */
	ppc_md.phys_mem_access_prot = pci_phys_mem_access_prot;

	/* On ppc64, we always enable PCI domains and we keep domain 0
	 * backward compatible in /proc for video cards
	 */
	pci_add_flags(PCI_ENABLE_PROC_DOMAINS | PCI_COMPAT_DOMAIN_0);

	/* Scan all of the recorded PCI controllers.  */
	list_for_each_entry_safe(hose, tmp, &hose_list, list_node)
		pcibios_scan_phb(hose);

	/* Call common code to handle resource allocation */
	pcibios_resource_survey();

	/* Add devices. */
	list_for_each_entry_safe(hose, tmp, &hose_list, list_node)
		pci_bus_add_devices(hose->bus);

	/* Call machine dependent fixup */
	if (ppc_md.pcibios_fixup)
		ppc_md.pcibios_fixup();

	printk(KERN_DEBUG "PCI: Probing PCI hardware done\n");

	return 0;
}

subsys_initcall_sync(pcibios_init);

int pcibios_unmap_io_space(struct pci_bus *bus)
{
	struct pci_controller *hose;

	WARN_ON(bus == NULL);

	/* If this is not a PHB, we only flush the hash table over
	 * the area mapped by this bridge. We don't play with the PTE
	 * mappings since we might have to deal with sub-page alignments
	 * so flushing the hash table is the only sane way to make sure
	 * that no hash entries are covering that removed bridge area
	 * while still allowing other busses overlapping those pages
	 *
	 * Note: If we ever support P2P hotplug on Book3E, we'll have
	 * to do an appropriate TLB flush here too
	 */
	if (bus->self) {
#ifdef CONFIG_PPC_BOOK3S_64
		struct resource *res = bus->resource[0];
#endif

		pr_debug("IO unmapping for PCI-PCI bridge %s\n",
			 pci_name(bus->self));

#ifdef CONFIG_PPC_BOOK3S_64
		__flush_hash_table_range(res->start + _IO_BASE,
					 res->end + _IO_BASE + 1);
#endif
		return 0;
	}

	/* Get the host bridge */
	hose = pci_bus_to_host(bus);

	pr_debug("IO unmapping for PHB %pOF\n", hose->dn);
	pr_debug("  alloc=0x%p\n", hose->io_base_alloc);

	iounmap(hose->io_base_alloc);
	return 0;
}
EXPORT_SYMBOL_GPL(pcibios_unmap_io_space);

void __iomem *ioremap_phb(phys_addr_t paddr, unsigned long size)
{
	struct vm_struct *area;
	unsigned long addr;

	WARN_ON_ONCE(paddr & ~PAGE_MASK);
	WARN_ON_ONCE(size & ~PAGE_MASK);

	/*
	 * Let's allocate some IO space for that guy. We don't pass VM_IOREMAP
	 * because we don't care about alignment tricks that the core does in
	 * that case.  Maybe we should due to stupid card with incomplete
	 * address decoding but I'd rather not deal with those outside of the
	 * reserved 64K legacy region.
	 */
	area = __get_vm_area_caller(size, VM_IOREMAP, PHB_IO_BASE, PHB_IO_END,
				    __builtin_return_address(0));
	if (!area)
		return NULL;

	addr = (unsigned long)area->addr;
	if (ioremap_page_range(addr, addr + size, paddr,
			pgprot_noncached(PAGE_KERNEL))) {
		vunmap_range(addr, addr + size);
		return NULL;
	}

	return (void __iomem *)addr;
}
EXPORT_SYMBOL_GPL(ioremap_phb);

static int pcibios_map_phb_io_space(struct pci_controller *hose)
{
	unsigned long phys_page;
	unsigned long size_page;
	unsigned long io_virt_offset;

	phys_page = ALIGN_DOWN(hose->io_base_phys, PAGE_SIZE);
	size_page = ALIGN(hose->pci_io_size, PAGE_SIZE);

	/* Make sure IO area address is clear */
	hose->io_base_alloc = NULL;

	/* If there's no IO to map on that bus, get away too */
	if (hose->pci_io_size == 0 || hose->io_base_phys == 0)
		return 0;

	/* Let's allocate some IO space for that guy. We don't pass
	 * VM_IOREMAP because we don't care about alignment tricks that
	 * the core does in that case. Maybe we should due to stupid card
	 * with incomplete address decoding but I'd rather not deal with
	 * those outside of the reserved 64K legacy region.
	 */
	hose->io_base_alloc = ioremap_phb(phys_page, size_page);
	if (!hose->io_base_alloc)
		return -ENOMEM;
	hose->io_base_virt = hose->io_base_alloc +
				hose->io_base_phys - phys_page;

	pr_debug("IO mapping for PHB %pOF\n", hose->dn);
	pr_debug("  phys=0x%016llx, virt=0x%p (alloc=0x%p)\n",
		 hose->io_base_phys, hose->io_base_virt, hose->io_base_alloc);
	pr_debug("  size=0x%016llx (alloc=0x%016lx)\n",
		 hose->pci_io_size, size_page);

	/* Fixup hose IO resource */
	io_virt_offset = pcibios_io_space_offset(hose);
	hose->io_resource.start += io_virt_offset;
	hose->io_resource.end += io_virt_offset;

	pr_debug("  hose->io_resource=%pR\n", &hose->io_resource);

	return 0;
}

int pcibios_map_io_space(struct pci_bus *bus)
{
	WARN_ON(bus == NULL);

	/* If this not a PHB, nothing to do, page tables still exist and
	 * thus HPTEs will be faulted in when needed
	 */
	if (bus->self) {
		pr_debug("IO mapping for PCI-PCI bridge %s\n",
			 pci_name(bus->self));
		pr_debug("  virt=0x%016llx...0x%016llx\n",
			 bus->resource[0]->start + _IO_BASE,
			 bus->resource[0]->end + _IO_BASE);
		return 0;
	}

	return pcibios_map_phb_io_space(pci_bus_to_host(bus));
}
EXPORT_SYMBOL_GPL(pcibios_map_io_space);

void pcibios_setup_phb_io_space(struct pci_controller *hose)
{
	pcibios_map_phb_io_space(hose);
}

#define IOBASE_BRIDGE_NUMBER	0
#define IOBASE_MEMORY		1
#define IOBASE_IO		2
#define IOBASE_ISA_IO		3
#define IOBASE_ISA_MEM		4

SYSCALL_DEFINE3(pciconfig_iobase, long, which, unsigned long, in_bus,
			  unsigned long, in_devfn)
{
	struct pci_controller* hose;
	struct pci_bus *tmp_bus, *bus = NULL;
	struct device_node *hose_node;

	/* Argh ! Please forgive me for that hack, but that's the
	 * simplest way to get existing XFree to not lockup on some
	 * G5 machines... So when something asks for bus 0 io base
	 * (bus 0 is HT root), we return the AGP one instead.
	 */
	if (in_bus == 0 && of_machine_is_compatible("MacRISC4")) {
		struct device_node *agp;

		agp = of_find_compatible_node(NULL, NULL, "u3-agp");
		if (agp)
			in_bus = 0xf0;
		of_node_put(agp);
	}

	/* That syscall isn't quite compatible with PCI domains, but it's
	 * used on pre-domains setup. We return the first match
	 */

	list_for_each_entry(tmp_bus, &pci_root_buses, node) {
		if (in_bus >= tmp_bus->number &&
		    in_bus <= tmp_bus->busn_res.end) {
			bus = tmp_bus;
			break;
		}
	}
	if (bus == NULL || bus->dev.of_node == NULL)
		return -ENODEV;

	hose_node = bus->dev.of_node;
	hose = PCI_DN(hose_node)->phb;

	switch (which) {
	case IOBASE_BRIDGE_NUMBER:
		return (long)hose->first_busno;
	case IOBASE_MEMORY:
		return (long)hose->mem_offset[0];
	case IOBASE_IO:
		return (long)hose->io_base_phys;
	case IOBASE_ISA_IO:
		return (long)isa_io_base;
	case IOBASE_ISA_MEM:
		return -EINVAL;
	}

	return -EOPNOTSUPP;
}

#ifdef CONFIG_NUMA
int pcibus_to_node(struct pci_bus *bus)
{
	struct pci_controller *phb = pci_bus_to_host(bus);
	return phb->node;
}
EXPORT_SYMBOL(pcibus_to_node);
#endif

#ifdef CONFIG_PPC_PMAC
int pci_device_from_OF_node(struct device_node *np, u8 *bus, u8 *devfn)
{
	if (!PCI_DN(np))
		return -ENODEV;
	*bus = PCI_DN(np)->busno;
	*devfn = PCI_DN(np)->devfn;
	return 0;
}
#endif
