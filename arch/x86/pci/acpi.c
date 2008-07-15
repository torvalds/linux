#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/dmi.h>
#include <asm/numa.h>
#include <asm/e820.h>
#include "pci.h"

struct pci_root_info {
	char *name;
	unsigned int res_num;
	struct resource *res;
	struct pci_bus *bus;
	int busnum;
};

struct gap_info {
	unsigned long gapstart;
	unsigned long gapsize;
};

static acpi_status
resource_to_addr(struct acpi_resource *resource,
			struct acpi_resource_address64 *addr)
{
	acpi_status status;

	status = acpi_resource_to_address64(resource, addr);
	if (ACPI_SUCCESS(status) &&
	    (addr->resource_type == ACPI_MEMORY_RANGE ||
	    addr->resource_type == ACPI_IO_RANGE) &&
	    addr->address_length > 0 &&
	    addr->producer_consumer == ACPI_PRODUCER) {
		return AE_OK;
	}
	return AE_ERROR;
}

static acpi_status
count_resource(struct acpi_resource *acpi_res, void *data)
{
	struct pci_root_info *info = data;
	struct acpi_resource_address64 addr;
	acpi_status status;

	if (info->res_num >= PCI_BUS_NUM_RESOURCES)
		return AE_OK;

	status = resource_to_addr(acpi_res, &addr);
	if (ACPI_SUCCESS(status))
		info->res_num++;
	return AE_OK;
}

static acpi_status
setup_resource(struct acpi_resource *acpi_res, void *data)
{
	struct pci_root_info *info = data;
	struct resource *res;
	struct acpi_resource_address64 addr;
	acpi_status status;
	unsigned long flags;
	struct resource *root;

	if (info->res_num >= PCI_BUS_NUM_RESOURCES)
		return AE_OK;

	status = resource_to_addr(acpi_res, &addr);
	if (!ACPI_SUCCESS(status))
		return AE_OK;

	if (addr.resource_type == ACPI_MEMORY_RANGE) {
		root = &iomem_resource;
		flags = IORESOURCE_MEM;
		if (addr.info.mem.caching == ACPI_PREFETCHABLE_MEMORY)
			flags |= IORESOURCE_PREFETCH;
	} else if (addr.resource_type == ACPI_IO_RANGE) {
		root = &ioport_resource;
		flags = IORESOURCE_IO;
	} else
		return AE_OK;

	res = &info->res[info->res_num];
	res->name = info->name;
	res->flags = flags;
	res->start = addr.minimum + addr.translation_offset;
	res->end = res->start + addr.address_length - 1;
	res->child = NULL;

	if (insert_resource(root, res)) {
		printk(KERN_ERR "PCI: Failed to allocate 0x%lx-0x%lx "
			"from %s for %s\n", (unsigned long) res->start,
			(unsigned long) res->end, root->name, info->name);
	} else {
		info->bus->resource[info->res_num] = res;
		info->res_num++;
	}
	return AE_OK;
}

static void
adjust_transparent_bridge_resources(struct pci_bus *bus)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		int i;
		u16 class = dev->class >> 8;

		if (class == PCI_CLASS_BRIDGE_PCI && dev->transparent) {
			for(i = 3; i < PCI_BUS_NUM_RESOURCES; i++)
				dev->subordinate->resource[i] =
						dev->bus->resource[i - 3];
		}
	}
}

static acpi_status search_gap(struct acpi_resource *resource, void *data)
{
	struct acpi_resource_address64 addr;
	acpi_status status;
	struct gap_info *gap = data;
	unsigned long long start_addr, end_addr;

	status = resource_to_addr(resource, &addr);
	if (ACPI_SUCCESS(status) &&
	    addr.resource_type == ACPI_MEMORY_RANGE &&
	    addr.address_length > gap->gapsize) {
		start_addr = addr.minimum + addr.translation_offset;
		/*
		 * We want space only in the 32bit address range
		 */
		if (start_addr < UINT_MAX) {
			end_addr = start_addr + addr.address_length;
			e820_search_gap(&gap->gapstart, &gap->gapsize,
					start_addr, end_addr);
		}
	}

	return AE_OK;
}

/*
 * Search for a hole in the 32 bit address space for PCI to assign MMIO
 * resources, for hotplug or unconfigured resources.
 * We query the CRS object of the PCI root device to look for possible producer
 * resources in the tree and consider these while calulating the start address
 * for this hole.
 */
static void pci_setup_gap(acpi_handle *handle)
{
	struct gap_info gap;
	acpi_status status;

	gap.gapstart = 0;
	gap.gapsize = 0x400000;

	status = acpi_walk_resources(handle, METHOD_NAME__CRS,
				     search_gap, &gap);

	if (ACPI_SUCCESS(status)) {
		unsigned long round;

		if (!gap.gapstart) {
			printk(KERN_ERR "ACPI: Warning: Cannot find a gap "
					"in the 32bit address range for PCI\n"
					"ACPI: PCI devices may collide with "
					"hotpluggable memory address range\n");
		}
		/*
		 * Round the gapstart, uses the same logic as in
		 * e820_gap_setup
		 */
		round = 0x100000;
		while ((gap.gapsize >> 4) > round)
			round += round;
		/* Fun with two's complement */
		pci_mem_start = (gap.gapstart + round) & -round;

		printk(KERN_INFO "ACPI: PCI resources should "
				"start at %lx (gap: %lx:%lx)\n",
				pci_mem_start, gap.gapstart, gap.gapsize);
	} else {
		printk(KERN_ERR "ACPI: Error while searching for gap in "
				"the 32bit address range for PCI\n");
	}
}


static void
get_current_resources(struct acpi_device *device, int busnum,
			int domain, struct pci_bus *bus)
{
	struct pci_root_info info;
	size_t size;

	info.bus = bus;
	info.res_num = 0;
	acpi_walk_resources(device->handle, METHOD_NAME__CRS, count_resource,
				&info);
	if (!info.res_num)
		return;

	size = sizeof(*info.res) * info.res_num;
	info.res = kmalloc(size, GFP_KERNEL);
	if (!info.res)
		goto res_alloc_fail;

	info.name = kmalloc(16, GFP_KERNEL);
	if (!info.name)
		goto name_alloc_fail;
	sprintf(info.name, "PCI Bus %04x:%02x", domain, busnum);

	info.res_num = 0;
	acpi_walk_resources(device->handle, METHOD_NAME__CRS, setup_resource,
				&info);
	if (info.res_num)
		adjust_transparent_bridge_resources(bus);

	return;

name_alloc_fail:
	kfree(info.res);
res_alloc_fail:
	return;
}

struct pci_bus * __devinit pci_acpi_scan_root(struct acpi_device *device, int domain, int busnum)
{
	struct pci_bus *bus;
	struct pci_sysdata *sd;
	int node;
#ifdef CONFIG_ACPI_NUMA
	int pxm;
#endif

	if (domain && !pci_domains_supported) {
		printk(KERN_WARNING "PCI: Multiple domains not supported "
		       "(dom %d, bus %d)\n", domain, busnum);
		return NULL;
	}

	node = -1;
#ifdef CONFIG_ACPI_NUMA
	pxm = acpi_get_pxm(device->handle);
	if (pxm >= 0)
		node = pxm_to_node(pxm);
	if (node != -1)
		set_mp_bus_to_node(busnum, node);
	else
		node = get_mp_bus_to_node(busnum);
#endif

	/* Allocate per-root-bus (not per bus) arch-specific data.
	 * TODO: leak; this memory is never freed.
	 * It's arguable whether it's worth the trouble to care.
	 */
	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd) {
		printk(KERN_ERR "PCI: OOM, not probing PCI bus %02x\n", busnum);
		return NULL;
	}

	sd->domain = domain;
	sd->node = node;
	/*
	 * Maybe the desired pci bus has been already scanned. In such case
	 * it is unnecessary to scan the pci bus with the given domain,busnum.
	 */
	bus = pci_find_bus(domain, busnum);
	if (bus) {
		/*
		 * If the desired bus exits, the content of bus->sysdata will
		 * be replaced by sd.
		 */
		memcpy(bus->sysdata, sd, sizeof(*sd));
		kfree(sd);
	} else
		bus = pci_scan_bus_parented(NULL, busnum, &pci_root_ops, sd);

	if (!bus)
		kfree(sd);

#ifdef CONFIG_ACPI_NUMA
	if (bus) {
		if (pxm >= 0) {
			printk(KERN_DEBUG "bus %02x -> pxm %d -> node %d\n",
				busnum, pxm, pxm_to_node(pxm));
		}
	}
#endif

	if (bus && (pci_probe & PCI_USE__CRS))
		get_current_resources(device, busnum, domain, bus);

	pci_setup_gap(device->handle);
	return bus;
}

extern int pci_routeirq;
static int __init pci_acpi_init(void)
{
	struct pci_dev *dev = NULL;

	if (pcibios_scanned)
		return 0;

	if (acpi_noirq)
		return 0;

	printk(KERN_INFO "PCI: Using ACPI for IRQ routing\n");
	acpi_irq_penalty_init();
	pcibios_scanned++;
	pcibios_enable_irq = acpi_pci_irq_enable;
	pcibios_disable_irq = acpi_pci_irq_disable;

	if (pci_routeirq) {
		/*
		 * PCI IRQ routing is set up by pci_enable_device(), but we
		 * also do it here in case there are still broken drivers that
		 * don't use pci_enable_device().
		 */
		printk(KERN_INFO "PCI: Routing PCI interrupts for all devices because \"pci=routeirq\" specified\n");
		for_each_pci_dev(dev)
			acpi_pci_irq_enable(dev);
	}

#ifdef CONFIG_X86_IO_APIC
	if (acpi_ioapic)
		print_IO_APIC();
#endif

	return 0;
}
subsys_initcall(pci_acpi_init);
