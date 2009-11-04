#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/dmi.h>
#include <asm/numa.h>
#include <asm/pci_x86.h>

struct pci_root_info {
	struct acpi_device *bridge;
	char *name;
	unsigned int res_num;
	struct resource *res;
	struct pci_bus *bus;
	int busnum;
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

	status = resource_to_addr(acpi_res, &addr);
	if (ACPI_SUCCESS(status))
		info->res_num++;
	return AE_OK;
}

static int
bus_has_transparent_bridge(struct pci_bus *bus)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		u16 class = dev->class >> 8;

		if (class == PCI_CLASS_BRIDGE_PCI && dev->transparent)
			return true;
	}
	return false;
}

static void
align_resource(struct acpi_device *bridge, struct resource *res)
{
	int align = (res->flags & IORESOURCE_MEM) ? 16 : 4;

	/*
	 * Host bridge windows are not BARs, but the decoders on the PCI side
	 * that claim this address space have starting alignment and length
	 * constraints, so fix any obvious BIOS goofs.
	 */
	if (res->start & (align - 1)) {
		dev_printk(KERN_DEBUG, &bridge->dev,
			   "host bridge window %pR invalid; "
			   "aligning start to %d-byte boundary\n", res, align);
		res->start &= ~(align - 1);
	}
	if ((res->end + 1) & (align - 1)) {
		dev_printk(KERN_DEBUG, &bridge->dev,
			   "host bridge window %pR invalid; "
			   "aligning end to %d-byte boundary\n", res, align);
		res->end = roundup(res->end, align) - 1;
	}
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
	int max_root_bus_resources = PCI_BUS_NUM_RESOURCES;
	u64 start, end;

	if (bus_has_transparent_bridge(info->bus))
		max_root_bus_resources -= 3;

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

	start = addr.minimum + addr.translation_offset;
	end = start + addr.address_length - 1;
	if (info->res_num >= max_root_bus_resources) {
		if (pci_probe & PCI_USE__CRS)
			printk(KERN_WARNING "PCI: Failed to allocate "
			       "0x%lx-0x%lx from %s for %s due to _CRS "
			       "returning more than %d resource descriptors\n",
			       (unsigned long) start, (unsigned long) end,
			       root->name, info->name, max_root_bus_resources);
		return AE_OK;
	}

	res = &info->res[info->res_num];
	res->name = info->name;
	res->flags = flags;
	res->start = start;
	res->end = end;
	res->child = NULL;
	align_resource(info->bridge, res);

	if (!(pci_probe & PCI_USE__CRS)) {
		dev_printk(KERN_DEBUG, &info->bridge->dev,
			   "host bridge window %pR (ignored)\n", res);
		return AE_OK;
	}

	if (insert_resource(root, res)) {
		dev_err(&info->bridge->dev,
			"can't allocate host bridge window %pR\n", res);
	} else {
		info->bus->resource[info->res_num] = res;
		info->res_num++;
		if (addr.translation_offset)
			dev_info(&info->bridge->dev, "host bridge window %pR "
				 "(PCI address [%#llx-%#llx])\n",
				 res, res->start - addr.translation_offset,
				 res->end - addr.translation_offset);
		else
			dev_info(&info->bridge->dev,
				 "host bridge window %pR\n", res);
	}
	return AE_OK;
}

static void
get_current_resources(struct acpi_device *device, int busnum,
			int domain, struct pci_bus *bus)
{
	struct pci_root_info info;
	size_t size;

	if (!(pci_probe & PCI_USE__CRS))
		dev_info(&device->dev,
			 "ignoring host bridge windows from ACPI; "
			 "boot with \"pci=use_crs\" to use them\n");

	info.bridge = device;
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
		printk(KERN_WARNING "pci_bus %04x:%02x: "
		       "ignored (multiple domains not supported)\n",
		       domain, busnum);
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
#endif
		node = get_mp_bus_to_node(busnum);

	if (node != -1 && !node_online(node))
		node = -1;

	/* Allocate per-root-bus (not per bus) arch-specific data.
	 * TODO: leak; this memory is never freed.
	 * It's arguable whether it's worth the trouble to care.
	 */
	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd) {
		printk(KERN_WARNING "pci_bus %04x:%02x: "
		       "ignored (out of memory)\n", domain, busnum);
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
	} else {
		bus = pci_create_bus(NULL, busnum, &pci_root_ops, sd);
		if (bus) {
			get_current_resources(device, busnum, domain, bus);
			bus->subordinate = pci_scan_child_bus(bus);
		}
	}

	if (!bus)
		kfree(sd);

	if (bus && node != -1) {
#ifdef CONFIG_ACPI_NUMA
		if (pxm >= 0)
			dev_printk(KERN_DEBUG, &bus->dev,
				   "on NUMA node %d (pxm %d)\n", node, pxm);
#else
		dev_printk(KERN_DEBUG, &bus->dev, "on NUMA node %d\n", node);
#endif
	}

	return bus;
}

int __init pci_acpi_init(void)
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

	return 0;
}
