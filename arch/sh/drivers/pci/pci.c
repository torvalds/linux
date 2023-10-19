// SPDX-License-Identifier: GPL-2.0
/*
 * New-style PCI core.
 *
 * Copyright (c) 2004 - 2009  Paul Mundt
 * Copyright (c) 2002  M. R. Brown
 *
 * Modelled after arch/mips/pci/pci.c:
 *  Copyright (C) 2003, 04 Ralf Baechle (ralf@linux-mips.org)
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/export.h>

unsigned long PCIBIOS_MIN_IO = 0x0000;
unsigned long PCIBIOS_MIN_MEM = 0;

/*
 * The PCI controller list.
 */
static struct pci_channel *hose_head, **hose_tail = &hose_head;

static int pci_initialized;

static void pcibios_scanbus(struct pci_channel *hose)
{
	static int next_busno;
	static int need_domain_info;
	LIST_HEAD(resources);
	struct resource *res;
	resource_size_t offset;
	int i, ret;
	struct pci_host_bridge *bridge;

	bridge = pci_alloc_host_bridge(0);
	if (!bridge)
		return;

	for (i = 0; i < hose->nr_resources; i++) {
		res = hose->resources + i;
		offset = 0;
		if (res->flags & IORESOURCE_DISABLED)
			continue;
		if (res->flags & IORESOURCE_IO)
			offset = hose->io_offset;
		else if (res->flags & IORESOURCE_MEM)
			offset = hose->mem_offset;
		pci_add_resource_offset(&resources, res, offset);
	}

	list_splice_init(&resources, &bridge->windows);
	bridge->dev.parent = NULL;
	bridge->sysdata = hose;
	bridge->busnr = next_busno;
	bridge->ops = hose->pci_ops;
	bridge->swizzle_irq = pci_common_swizzle;
	bridge->map_irq = pcibios_map_platform_irq;

	ret = pci_scan_root_bus_bridge(bridge);
	if (ret) {
		pci_free_host_bridge(bridge);
		return;
	}

	hose->bus = bridge->bus;

	need_domain_info = need_domain_info || hose->index;
	hose->need_domain_info = need_domain_info;

	next_busno = hose->bus->busn_res.end + 1;
	/* Don't allow 8-bit bus number overflow inside the hose -
	   reserve some space for bridges. */
	if (next_busno > 224) {
		next_busno = 0;
		need_domain_info = 1;
	}

	pci_bus_size_bridges(hose->bus);
	pci_bus_assign_resources(hose->bus);
	pci_bus_add_devices(hose->bus);
}

/*
 * This interrupt-safe spinlock protects all accesses to PCI
 * configuration space.
 */
DEFINE_RAW_SPINLOCK(pci_config_lock);
static DEFINE_MUTEX(pci_scan_mutex);

int register_pci_controller(struct pci_channel *hose)
{
	int i;

	for (i = 0; i < hose->nr_resources; i++) {
		struct resource *res = hose->resources + i;

		if (res->flags & IORESOURCE_DISABLED)
			continue;

		if (res->flags & IORESOURCE_IO) {
			if (request_resource(&ioport_resource, res) < 0)
				goto out;
		} else {
			if (request_resource(&iomem_resource, res) < 0)
				goto out;
		}
	}

	*hose_tail = hose;
	hose_tail = &hose->next;

	/*
	 * Do not panic here but later - this might happen before console init.
	 */
	if (!hose->io_map_base) {
		pr_warn("registering PCI controller with io_map_base unset\n");
	}

	/*
	 * Setup the ERR/PERR and SERR timers, if available.
	 */
	pcibios_enable_timers(hose);

	/*
	 * Scan the bus if it is register after the PCI subsystem
	 * initialization.
	 */
	if (pci_initialized) {
		mutex_lock(&pci_scan_mutex);
		pcibios_scanbus(hose);
		mutex_unlock(&pci_scan_mutex);
	}

	return 0;

out:
	for (--i; i >= 0; i--)
		release_resource(&hose->resources[i]);

	pr_warn("Skipping PCI bus scan due to resource conflict\n");
	return -1;
}

static int __init pcibios_init(void)
{
	struct pci_channel *hose;

	/* Scan all of the recorded PCI controllers.  */
	for (hose = hose_head; hose; hose = hose->next)
		pcibios_scanbus(hose);

	pci_initialized = 1;

	return 0;
}
subsys_initcall(pcibios_init);

/*
 * We need to avoid collisions with `mirrored' VGA ports
 * and other strange ISA hardware, so we always want the
 * addresses to be allocated in the 0x000-0x0ff region
 * modulo 0x400.
 */
resource_size_t pcibios_align_resource(void *data, const struct resource *res,
				resource_size_t size, resource_size_t align)
{
	struct pci_dev *dev = data;
	struct pci_channel *hose = dev->sysdata;
	resource_size_t start = res->start;

	if (res->flags & IORESOURCE_IO) {
		if (start < PCIBIOS_MIN_IO + hose->resources[0].start)
			start = PCIBIOS_MIN_IO + hose->resources[0].start;

		/*
                 * Put everything into 0x00-0xff region modulo 0x400.
		 */
		if (start & 0x300)
			start = (start + 0x3ff) & ~0x3ff;
	}

	return start;
}

static void __init
pcibios_bus_report_status_early(struct pci_channel *hose,
				int top_bus, int current_bus,
				unsigned int status_mask, int warn)
{
	unsigned int pci_devfn;
	u16 status;
	int ret;

	for (pci_devfn = 0; pci_devfn < 0xff; pci_devfn++) {
		if (PCI_FUNC(pci_devfn))
			continue;
		ret = early_read_config_word(hose, top_bus, current_bus,
					     pci_devfn, PCI_STATUS, &status);
		if (ret != PCIBIOS_SUCCESSFUL)
			continue;
		if (status == 0xffff)
			continue;

		early_write_config_word(hose, top_bus, current_bus,
					pci_devfn, PCI_STATUS,
					status & status_mask);
		if (warn)
			pr_cont("(%02x:%02x: %04X) ", current_bus, pci_devfn,
				status);
	}
}

/*
 * We can't use pci_find_device() here since we are
 * called from interrupt context.
 */
static void __ref
pcibios_bus_report_status(struct pci_bus *bus, unsigned int status_mask,
			  int warn)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		u16 status;

		/*
		 * ignore host bridge - we handle
		 * that separately
		 */
		if (dev->bus->number == 0 && dev->devfn == 0)
			continue;

		pci_read_config_word(dev, PCI_STATUS, &status);
		if (status == 0xffff)
			continue;

		if ((status & status_mask) == 0)
			continue;

		/* clear the status errors */
		pci_write_config_word(dev, PCI_STATUS, status & status_mask);

		if (warn)
			pr_cont("(%s: %04X) ", pci_name(dev), status);
	}

	list_for_each_entry(dev, &bus->devices, bus_list)
		if (dev->subordinate)
			pcibios_bus_report_status(dev->subordinate, status_mask, warn);
}

void __ref pcibios_report_status(unsigned int status_mask, int warn)
{
	struct pci_channel *hose;

	for (hose = hose_head; hose; hose = hose->next) {
		if (unlikely(!hose->bus))
			pcibios_bus_report_status_early(hose, hose_head->index,
					hose->index, status_mask, warn);
		else
			pcibios_bus_report_status(hose->bus, status_mask, warn);
	}
}

#ifndef CONFIG_GENERIC_IOMAP

void __iomem *__pci_ioport_map(struct pci_dev *dev,
			       unsigned long port, unsigned int nr)
{
	struct pci_channel *chan = dev->sysdata;

	if (unlikely(!chan->io_map_base)) {
		chan->io_map_base = sh_io_port_base;

		if (pci_domains_supported)
			panic("To avoid data corruption io_map_base MUST be "
			      "set with multiple PCI domains.");
	}

	return (void __iomem *)(chan->io_map_base + port);
}

void pci_iounmap(struct pci_dev *dev, void __iomem *addr)
{
	iounmap(addr);
}
EXPORT_SYMBOL(pci_iounmap);

#endif /* CONFIG_GENERIC_IOMAP */

EXPORT_SYMBOL(PCIBIOS_MIN_IO);
EXPORT_SYMBOL(PCIBIOS_MIN_MEM);
