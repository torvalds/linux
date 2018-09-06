/*
 * pci.c - Low-Level PCI Access in IA-64
 *
 * Derived from bios32.c of i386 tree.
 *
 * (c) Copyright 2002, 2005 Hewlett-Packard Development Company, L.P.
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 * Copyright (C) 2004 Silicon Graphics, Inc.
 *
 * Note: Above list of copyright holders is incomplete...
 */

#include <linux/acpi.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pci-acpi.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/bootmem.h>
#include <linux/export.h>

#include <asm/machvec.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/sal.h>
#include <asm/smp.h>
#include <asm/irq.h>
#include <asm/hw_irq.h>

/*
 * Low-level SAL-based PCI configuration access functions. Note that SAL
 * calls are already serialized (via sal_lock), so we don't need another
 * synchronization mechanism here.
 */

#define PCI_SAL_ADDRESS(seg, bus, devfn, reg)		\
	(((u64) seg << 24) | (bus << 16) | (devfn << 8) | (reg))

/* SAL 3.2 adds support for extended config space. */

#define PCI_SAL_EXT_ADDRESS(seg, bus, devfn, reg)	\
	(((u64) seg << 28) | (bus << 20) | (devfn << 12) | (reg))

int raw_pci_read(unsigned int seg, unsigned int bus, unsigned int devfn,
	      int reg, int len, u32 *value)
{
	u64 addr, data = 0;
	int mode, result;

	if (!value || (seg > 65535) || (bus > 255) || (devfn > 255) || (reg > 4095))
		return -EINVAL;

	if ((seg | reg) <= 255) {
		addr = PCI_SAL_ADDRESS(seg, bus, devfn, reg);
		mode = 0;
	} else if (sal_revision >= SAL_VERSION_CODE(3,2)) {
		addr = PCI_SAL_EXT_ADDRESS(seg, bus, devfn, reg);
		mode = 1;
	} else {
		return -EINVAL;
	}

	result = ia64_sal_pci_config_read(addr, mode, len, &data);
	if (result != 0)
		return -EINVAL;

	*value = (u32) data;
	return 0;
}

int raw_pci_write(unsigned int seg, unsigned int bus, unsigned int devfn,
	       int reg, int len, u32 value)
{
	u64 addr;
	int mode, result;

	if ((seg > 65535) || (bus > 255) || (devfn > 255) || (reg > 4095))
		return -EINVAL;

	if ((seg | reg) <= 255) {
		addr = PCI_SAL_ADDRESS(seg, bus, devfn, reg);
		mode = 0;
	} else if (sal_revision >= SAL_VERSION_CODE(3,2)) {
		addr = PCI_SAL_EXT_ADDRESS(seg, bus, devfn, reg);
		mode = 1;
	} else {
		return -EINVAL;
	}
	result = ia64_sal_pci_config_write(addr, mode, len, value);
	if (result != 0)
		return -EINVAL;
	return 0;
}

static int pci_read(struct pci_bus *bus, unsigned int devfn, int where,
							int size, u32 *value)
{
	return raw_pci_read(pci_domain_nr(bus), bus->number,
				 devfn, where, size, value);
}

static int pci_write(struct pci_bus *bus, unsigned int devfn, int where,
							int size, u32 value)
{
	return raw_pci_write(pci_domain_nr(bus), bus->number,
				  devfn, where, size, value);
}

struct pci_ops pci_root_ops = {
	.read = pci_read,
	.write = pci_write,
};

struct pci_root_info {
	struct acpi_pci_root_info common;
	struct pci_controller controller;
	struct list_head io_resources;
};

static unsigned int new_space(u64 phys_base, int sparse)
{
	u64 mmio_base;
	int i;

	if (phys_base == 0)
		return 0;	/* legacy I/O port space */

	mmio_base = (u64) ioremap(phys_base, 0);
	for (i = 0; i < num_io_spaces; i++)
		if (io_space[i].mmio_base == mmio_base &&
		    io_space[i].sparse == sparse)
			return i;

	if (num_io_spaces == MAX_IO_SPACES) {
		pr_err("PCI: Too many IO port spaces "
			"(MAX_IO_SPACES=%lu)\n", MAX_IO_SPACES);
		return ~0;
	}

	i = num_io_spaces++;
	io_space[i].mmio_base = mmio_base;
	io_space[i].sparse = sparse;

	return i;
}

static int add_io_space(struct device *dev, struct pci_root_info *info,
			struct resource_entry *entry)
{
	struct resource_entry *iospace;
	struct resource *resource, *res = entry->res;
	char *name;
	unsigned long base, min, max, base_port;
	unsigned int sparse = 0, space_nr, len;

	len = strlen(info->common.name) + 32;
	iospace = resource_list_create_entry(NULL, len);
	if (!iospace) {
		dev_err(dev, "PCI: No memory for %s I/O port space\n",
			info->common.name);
		return -ENOMEM;
	}

	if (res->flags & IORESOURCE_IO_SPARSE)
		sparse = 1;
	space_nr = new_space(entry->offset, sparse);
	if (space_nr == ~0)
		goto free_resource;

	name = (char *)(iospace + 1);
	min = res->start - entry->offset;
	max = res->end - entry->offset;
	base = __pa(io_space[space_nr].mmio_base);
	base_port = IO_SPACE_BASE(space_nr);
	snprintf(name, len, "%s I/O Ports %08lx-%08lx", info->common.name,
		 base_port + min, base_port + max);

	/*
	 * The SDM guarantees the legacy 0-64K space is sparse, but if the
	 * mapping is done by the processor (not the bridge), ACPI may not
	 * mark it as sparse.
	 */
	if (space_nr == 0)
		sparse = 1;

	resource = iospace->res;
	resource->name  = name;
	resource->flags = IORESOURCE_MEM;
	resource->start = base + (sparse ? IO_SPACE_SPARSE_ENCODING(min) : min);
	resource->end   = base + (sparse ? IO_SPACE_SPARSE_ENCODING(max) : max);
	if (insert_resource(&iomem_resource, resource)) {
		dev_err(dev,
			"can't allocate host bridge io space resource  %pR\n",
			resource);
		goto free_resource;
	}

	entry->offset = base_port;
	res->start = min + base_port;
	res->end = max + base_port;
	resource_list_add_tail(iospace, &info->io_resources);

	return 0;

free_resource:
	resource_list_free_entry(iospace);
	return -ENOSPC;
}

/*
 * An IO port or MMIO resource assigned to a PCI host bridge may be
 * consumed by the host bridge itself or available to its child
 * bus/devices. The ACPI specification defines a bit (Producer/Consumer)
 * to tell whether the resource is consumed by the host bridge itself,
 * but firmware hasn't used that bit consistently, so we can't rely on it.
 *
 * On x86 and IA64 platforms, all IO port and MMIO resources are assumed
 * to be available to child bus/devices except one special case:
 *     IO port [0xCF8-0xCFF] is consumed by the host bridge itself
 *     to access PCI configuration space.
 *
 * So explicitly filter out PCI CFG IO ports[0xCF8-0xCFF].
 */
static bool resource_is_pcicfg_ioport(struct resource *res)
{
	return (res->flags & IORESOURCE_IO) &&
		res->start == 0xCF8 && res->end == 0xCFF;
}

static int pci_acpi_root_prepare_resources(struct acpi_pci_root_info *ci)
{
	struct device *dev = &ci->bridge->dev;
	struct pci_root_info *info;
	struct resource *res;
	struct resource_entry *entry, *tmp;
	int status;

	status = acpi_pci_probe_root_resources(ci);
	if (status > 0) {
		info = container_of(ci, struct pci_root_info, common);
		resource_list_for_each_entry_safe(entry, tmp, &ci->resources) {
			res = entry->res;
			if (res->flags & IORESOURCE_MEM) {
				/*
				 * HP's firmware has a hack to work around a
				 * Windows bug. Ignore these tiny memory ranges.
				 */
				if (resource_size(res) <= 16) {
					resource_list_del(entry);
					insert_resource(&iomem_resource,
							entry->res);
					resource_list_add_tail(entry,
							&info->io_resources);
				}
			} else if (res->flags & IORESOURCE_IO) {
				if (resource_is_pcicfg_ioport(entry->res))
					resource_list_destroy_entry(entry);
				else if (add_io_space(dev, info, entry))
					resource_list_destroy_entry(entry);
			}
		}
	}

	return status;
}

static void pci_acpi_root_release_info(struct acpi_pci_root_info *ci)
{
	struct pci_root_info *info;
	struct resource_entry *entry, *tmp;

	info = container_of(ci, struct pci_root_info, common);
	resource_list_for_each_entry_safe(entry, tmp, &info->io_resources) {
		release_resource(entry->res);
		resource_list_destroy_entry(entry);
	}
	kfree(info);
}

static struct acpi_pci_root_ops pci_acpi_root_ops = {
	.pci_ops = &pci_root_ops,
	.release_info = pci_acpi_root_release_info,
	.prepare_resources = pci_acpi_root_prepare_resources,
};

struct pci_bus *pci_acpi_scan_root(struct acpi_pci_root *root)
{
	struct acpi_device *device = root->device;
	struct pci_root_info *info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		dev_err(&device->dev,
			"pci_bus %04x:%02x: ignored (out of memory)\n",
			root->segment, (int)root->secondary.start);
		return NULL;
	}

	info->controller.segment = root->segment;
	info->controller.companion = device;
	info->controller.node = acpi_get_node(device->handle);
	INIT_LIST_HEAD(&info->io_resources);
	return acpi_pci_root_create(root, &pci_acpi_root_ops,
				    &info->common, &info->controller);
}

int pcibios_root_bridge_prepare(struct pci_host_bridge *bridge)
{
	/*
	 * We pass NULL as parent to pci_create_root_bus(), so if it is not NULL
	 * here, pci_create_root_bus() has been called by someone else and
	 * sysdata is likely to be different from what we expect.  Let it go in
	 * that case.
	 */
	if (!bridge->dev.parent) {
		struct pci_controller *controller = bridge->bus->sysdata;
		ACPI_COMPANION_SET(&bridge->dev, controller->companion);
	}
	return 0;
}

void pcibios_fixup_device_resources(struct pci_dev *dev)
{
	int idx;

	if (!dev->bus)
		return;

	for (idx = 0; idx < PCI_BRIDGE_RESOURCES; idx++) {
		struct resource *r = &dev->resource[idx];

		if (!r->flags || r->parent || !r->start)
			continue;

		pci_claim_resource(dev, idx);
	}
}
EXPORT_SYMBOL_GPL(pcibios_fixup_device_resources);

static void pcibios_fixup_bridge_resources(struct pci_dev *dev)
{
	int idx;

	if (!dev->bus)
		return;

	for (idx = PCI_BRIDGE_RESOURCES; idx < PCI_NUM_RESOURCES; idx++) {
		struct resource *r = &dev->resource[idx];

		if (!r->flags || r->parent || !r->start)
			continue;

		pci_claim_bridge_resource(dev, idx);
	}
}

/*
 *  Called after each bus is probed, but before its children are examined.
 */
void pcibios_fixup_bus(struct pci_bus *b)
{
	struct pci_dev *dev;

	if (b->self) {
		pci_read_bridge_bases(b);
		pcibios_fixup_bridge_resources(b->self);
	}
	list_for_each_entry(dev, &b->devices, bus_list)
		pcibios_fixup_device_resources(dev);
	platform_pci_fixup_bus(b);
}

void pcibios_add_bus(struct pci_bus *bus)
{
	acpi_pci_add_bus(bus);
}

void pcibios_remove_bus(struct pci_bus *bus)
{
	acpi_pci_remove_bus(bus);
}

void pcibios_set_master (struct pci_dev *dev)
{
	/* No special bus mastering setup handling */
}

int
pcibios_enable_device (struct pci_dev *dev, int mask)
{
	int ret;

	ret = pci_enable_resources(dev, mask);
	if (ret < 0)
		return ret;

	if (!pci_dev_msi_enabled(dev))
		return acpi_pci_irq_enable(dev);
	return 0;
}

void
pcibios_disable_device (struct pci_dev *dev)
{
	BUG_ON(atomic_read(&dev->enable_cnt));
	if (!pci_dev_msi_enabled(dev))
		acpi_pci_irq_disable(dev);
}

/**
 * ia64_pci_get_legacy_mem - generic legacy mem routine
 * @bus: bus to get legacy memory base address for
 *
 * Find the base of legacy memory for @bus.  This is typically the first
 * megabyte of bus address space for @bus or is simply 0 on platforms whose
 * chipsets support legacy I/O and memory routing.  Returns the base address
 * or an error pointer if an error occurred.
 *
 * This is the ia64 generic version of this routine.  Other platforms
 * are free to override it with a machine vector.
 */
char *ia64_pci_get_legacy_mem(struct pci_bus *bus)
{
	return (char *)__IA64_UNCACHED_OFFSET;
}

/**
 * pci_mmap_legacy_page_range - map legacy memory space to userland
 * @bus: bus whose legacy space we're mapping
 * @vma: vma passed in by mmap
 *
 * Map legacy memory space for this device back to userspace using a machine
 * vector to get the base address.
 */
int
pci_mmap_legacy_page_range(struct pci_bus *bus, struct vm_area_struct *vma,
			   enum pci_mmap_state mmap_state)
{
	unsigned long size = vma->vm_end - vma->vm_start;
	pgprot_t prot;
	char *addr;

	/* We only support mmap'ing of legacy memory space */
	if (mmap_state != pci_mmap_mem)
		return -ENOSYS;

	/*
	 * Avoid attribute aliasing.  See Documentation/ia64/aliasing.txt
	 * for more details.
	 */
	if (!valid_mmap_phys_addr_range(vma->vm_pgoff, size))
		return -EINVAL;
	prot = phys_mem_access_prot(NULL, vma->vm_pgoff, size,
				    vma->vm_page_prot);

	addr = pci_get_legacy_mem(bus);
	if (IS_ERR(addr))
		return PTR_ERR(addr);

	vma->vm_pgoff += (unsigned long)addr >> PAGE_SHIFT;
	vma->vm_page_prot = prot;

	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			    size, vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

/**
 * ia64_pci_legacy_read - read from legacy I/O space
 * @bus: bus to read
 * @port: legacy port value
 * @val: caller allocated storage for returned value
 * @size: number of bytes to read
 *
 * Simply reads @size bytes from @port and puts the result in @val.
 *
 * Again, this (and the write routine) are generic versions that can be
 * overridden by the platform.  This is necessary on platforms that don't
 * support legacy I/O routing or that hard fail on legacy I/O timeouts.
 */
int ia64_pci_legacy_read(struct pci_bus *bus, u16 port, u32 *val, u8 size)
{
	int ret = size;

	switch (size) {
	case 1:
		*val = inb(port);
		break;
	case 2:
		*val = inw(port);
		break;
	case 4:
		*val = inl(port);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/**
 * ia64_pci_legacy_write - perform a legacy I/O write
 * @bus: bus pointer
 * @port: port to write
 * @val: value to write
 * @size: number of bytes to write from @val
 *
 * Simply writes @size bytes of @val to @port.
 */
int ia64_pci_legacy_write(struct pci_bus *bus, u16 port, u32 val, u8 size)
{
	int ret = size;

	switch (size) {
	case 1:
		outb(val, port);
		break;
	case 2:
		outw(val, port);
		break;
	case 4:
		outl(val, port);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/**
 * set_pci_cacheline_size - determine cacheline size for PCI devices
 *
 * We want to use the line-size of the outer-most cache.  We assume
 * that this line-size is the same for all CPUs.
 *
 * Code mostly taken from arch/ia64/kernel/palinfo.c:cache_info().
 */
static void __init set_pci_dfl_cacheline_size(void)
{
	unsigned long levels, unique_caches;
	long status;
	pal_cache_config_info_t cci;

	status = ia64_pal_cache_summary(&levels, &unique_caches);
	if (status != 0) {
		pr_err("%s: ia64_pal_cache_summary() failed "
			"(status=%ld)\n", __func__, status);
		return;
	}

	status = ia64_pal_cache_config_info(levels - 1,
				/* cache_type (data_or_unified)= */ 2, &cci);
	if (status != 0) {
		pr_err("%s: ia64_pal_cache_config_info() failed "
			"(status=%ld)\n", __func__, status);
		return;
	}
	pci_dfl_cache_line_size = (1 << cci.pcci_line_size) / 4;
}

static int __init pcibios_init(void)
{
	set_pci_dfl_cacheline_size();
	return 0;
}

subsys_initcall(pcibios_init);
