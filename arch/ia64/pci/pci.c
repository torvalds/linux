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
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/bootmem.h>

#include <asm/machvec.h>
#include <asm/page.h>
#include <asm/system.h>
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

/* Called by ACPI when it finds a new root bus.  */

static struct pci_controller * __devinit
alloc_pci_controller (int seg)
{
	struct pci_controller *controller;

	controller = kzalloc(sizeof(*controller), GFP_KERNEL);
	if (!controller)
		return NULL;

	controller->segment = seg;
	controller->node = -1;
	return controller;
}

struct pci_root_info {
	struct acpi_device *bridge;
	struct pci_controller *controller;
	char *name;
};

static unsigned int
new_space (u64 phys_base, int sparse)
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
		printk(KERN_ERR "PCI: Too many IO port spaces "
			"(MAX_IO_SPACES=%lu)\n", MAX_IO_SPACES);
		return ~0;
	}

	i = num_io_spaces++;
	io_space[i].mmio_base = mmio_base;
	io_space[i].sparse = sparse;

	return i;
}

static u64 __devinit
add_io_space (struct pci_root_info *info, struct acpi_resource_address64 *addr)
{
	struct resource *resource;
	char *name;
	unsigned long base, min, max, base_port;
	unsigned int sparse = 0, space_nr, len;

	resource = kzalloc(sizeof(*resource), GFP_KERNEL);
	if (!resource) {
		printk(KERN_ERR "PCI: No memory for %s I/O port space\n",
			info->name);
		goto out;
	}

	len = strlen(info->name) + 32;
	name = kzalloc(len, GFP_KERNEL);
	if (!name) {
		printk(KERN_ERR "PCI: No memory for %s I/O port space name\n",
			info->name);
		goto free_resource;
	}

	min = addr->minimum;
	max = min + addr->address_length - 1;
	if (addr->info.io.translation_type == ACPI_SPARSE_TRANSLATION)
		sparse = 1;

	space_nr = new_space(addr->translation_offset, sparse);
	if (space_nr == ~0)
		goto free_name;

	base = __pa(io_space[space_nr].mmio_base);
	base_port = IO_SPACE_BASE(space_nr);
	snprintf(name, len, "%s I/O Ports %08lx-%08lx", info->name,
		base_port + min, base_port + max);

	/*
	 * The SDM guarantees the legacy 0-64K space is sparse, but if the
	 * mapping is done by the processor (not the bridge), ACPI may not
	 * mark it as sparse.
	 */
	if (space_nr == 0)
		sparse = 1;

	resource->name  = name;
	resource->flags = IORESOURCE_MEM;
	resource->start = base + (sparse ? IO_SPACE_SPARSE_ENCODING(min) : min);
	resource->end   = base + (sparse ? IO_SPACE_SPARSE_ENCODING(max) : max);
	insert_resource(&iomem_resource, resource);

	return base_port;

free_name:
	kfree(name);
free_resource:
	kfree(resource);
out:
	return ~0;
}

static acpi_status __devinit resource_to_window(struct acpi_resource *resource,
	struct acpi_resource_address64 *addr)
{
	acpi_status status;

	/*
	 * We're only interested in _CRS descriptors that are
	 *	- address space descriptors for memory or I/O space
	 *	- non-zero size
	 *	- producers, i.e., the address space is routed downstream,
	 *	  not consumed by the bridge itself
	 */
	status = acpi_resource_to_address64(resource, addr);
	if (ACPI_SUCCESS(status) &&
	    (addr->resource_type == ACPI_MEMORY_RANGE ||
	     addr->resource_type == ACPI_IO_RANGE) &&
	    addr->address_length &&
	    addr->producer_consumer == ACPI_PRODUCER)
		return AE_OK;

	return AE_ERROR;
}

static acpi_status __devinit
count_window (struct acpi_resource *resource, void *data)
{
	unsigned int *windows = (unsigned int *) data;
	struct acpi_resource_address64 addr;
	acpi_status status;

	status = resource_to_window(resource, &addr);
	if (ACPI_SUCCESS(status))
		(*windows)++;

	return AE_OK;
}

static __devinit acpi_status add_window(struct acpi_resource *res, void *data)
{
	struct pci_root_info *info = data;
	struct pci_window *window;
	struct acpi_resource_address64 addr;
	acpi_status status;
	unsigned long flags, offset = 0;
	struct resource *root;

	/* Return AE_OK for non-window resources to keep scanning for more */
	status = resource_to_window(res, &addr);
	if (!ACPI_SUCCESS(status))
		return AE_OK;

	if (addr.resource_type == ACPI_MEMORY_RANGE) {
		flags = IORESOURCE_MEM;
		root = &iomem_resource;
		offset = addr.translation_offset;
	} else if (addr.resource_type == ACPI_IO_RANGE) {
		flags = IORESOURCE_IO;
		root = &ioport_resource;
		offset = add_io_space(info, &addr);
		if (offset == ~0)
			return AE_OK;
	} else
		return AE_OK;

	window = &info->controller->window[info->controller->windows++];
	window->resource.name = info->name;
	window->resource.flags = flags;
	window->resource.start = addr.minimum + offset;
	window->resource.end = window->resource.start + addr.address_length - 1;
	window->resource.child = NULL;
	window->offset = offset;

	if (insert_resource(root, &window->resource)) {
		dev_err(&info->bridge->dev, "can't allocate %pRt\n",
			&window->resource);
	} else {
		if (offset)
			dev_info(&info->bridge->dev, "host bridge window: %pRt "
				 "(PCI address [%#llx-%#llx])\n",
				 &window->resource,
				 window->resource.start - offset,
				 window->resource.end - offset);
		else
			dev_info(&info->bridge->dev,
				 "host bridge window: %pRt\n",
				 &window->resource);
	}

	return AE_OK;
}

static void __devinit
pcibios_setup_root_windows(struct pci_bus *bus, struct pci_controller *ctrl)
{
	int i, j;

	j = 0;
	for (i = 0; i < ctrl->windows; i++) {
		struct resource *res = &ctrl->window[i].resource;
		/* HP's firmware has a hack to work around a Windows bug.
		 * Ignore these tiny memory ranges */
		if ((res->flags & IORESOURCE_MEM) &&
		    (res->end - res->start < 16))
			continue;
		if (j >= PCI_BUS_NUM_RESOURCES) {
			dev_warn(&bus->dev, "ignoring %pRf (no space)\n", res);
			continue;
		}
		bus->resource[j++] = res;
	}
}

struct pci_bus * __devinit
pci_acpi_scan_root(struct acpi_device *device, int domain, int bus)
{
	struct pci_controller *controller;
	unsigned int windows = 0;
	struct pci_bus *pbus;
	char *name;
	int pxm;

	controller = alloc_pci_controller(domain);
	if (!controller)
		goto out1;

	controller->acpi_handle = device->handle;

	pxm = acpi_get_pxm(controller->acpi_handle);
#ifdef CONFIG_NUMA
	if (pxm >= 0)
		controller->node = pxm_to_node(pxm);
#endif

	acpi_walk_resources(device->handle, METHOD_NAME__CRS, count_window,
			&windows);
	if (windows) {
		struct pci_root_info info;

		controller->window =
			kmalloc_node(sizeof(*controller->window) * windows,
				     GFP_KERNEL, controller->node);
		if (!controller->window)
			goto out2;

		name = kmalloc(16, GFP_KERNEL);
		if (!name)
			goto out3;

		sprintf(name, "PCI Bus %04x:%02x", domain, bus);
		info.bridge = device;
		info.controller = controller;
		info.name = name;
		acpi_walk_resources(device->handle, METHOD_NAME__CRS,
			add_window, &info);
	}
	/*
	 * See arch/x86/pci/acpi.c.
	 * The desired pci bus might already be scanned in a quirk. We
	 * should handle the case here, but it appears that IA64 hasn't
	 * such quirk. So we just ignore the case now.
	 */
	pbus = pci_scan_bus_parented(NULL, bus, &pci_root_ops, controller);

	return pbus;

out3:
	kfree(controller->window);
out2:
	kfree(controller);
out1:
	return NULL;
}

void pcibios_resource_to_bus(struct pci_dev *dev,
		struct pci_bus_region *region, struct resource *res)
{
	struct pci_controller *controller = PCI_CONTROLLER(dev);
	unsigned long offset = 0;
	int i;

	for (i = 0; i < controller->windows; i++) {
		struct pci_window *window = &controller->window[i];
		if (!(window->resource.flags & res->flags))
			continue;
		if (window->resource.start > res->start)
			continue;
		if (window->resource.end < res->end)
			continue;
		offset = window->offset;
		break;
	}

	region->start = res->start - offset;
	region->end = res->end - offset;
}
EXPORT_SYMBOL(pcibios_resource_to_bus);

void pcibios_bus_to_resource(struct pci_dev *dev,
		struct resource *res, struct pci_bus_region *region)
{
	struct pci_controller *controller = PCI_CONTROLLER(dev);
	unsigned long offset = 0;
	int i;

	for (i = 0; i < controller->windows; i++) {
		struct pci_window *window = &controller->window[i];
		if (!(window->resource.flags & res->flags))
			continue;
		if (window->resource.start - window->offset > region->start)
			continue;
		if (window->resource.end - window->offset < region->end)
			continue;
		offset = window->offset;
		break;
	}

	res->start = region->start + offset;
	res->end = region->end + offset;
}
EXPORT_SYMBOL(pcibios_bus_to_resource);

static int __devinit is_valid_resource(struct pci_dev *dev, int idx)
{
	unsigned int i, type_mask = IORESOURCE_IO | IORESOURCE_MEM;
	struct resource *devr = &dev->resource[idx];

	if (!dev->bus)
		return 0;
	for (i=0; i<PCI_BUS_NUM_RESOURCES; i++) {
		struct resource *busr = dev->bus->resource[i];

		if (!busr || ((busr->flags ^ devr->flags) & type_mask))
			continue;
		if ((devr->start) && (devr->start >= busr->start) &&
				(devr->end <= busr->end))
			return 1;
	}
	return 0;
}

static void __devinit
pcibios_fixup_resources(struct pci_dev *dev, int start, int limit)
{
	struct pci_bus_region region;
	int i;

	for (i = start; i < limit; i++) {
		if (!dev->resource[i].flags)
			continue;
		region.start = dev->resource[i].start;
		region.end = dev->resource[i].end;
		pcibios_bus_to_resource(dev, &dev->resource[i], &region);
		if ((is_valid_resource(dev, i)))
			pci_claim_resource(dev, i);
	}
}

void __devinit pcibios_fixup_device_resources(struct pci_dev *dev)
{
	pcibios_fixup_resources(dev, 0, PCI_BRIDGE_RESOURCES);
}
EXPORT_SYMBOL_GPL(pcibios_fixup_device_resources);

static void __devinit pcibios_fixup_bridge_resources(struct pci_dev *dev)
{
	pcibios_fixup_resources(dev, PCI_BRIDGE_RESOURCES, PCI_NUM_RESOURCES);
}

/*
 *  Called after each bus is probed, but before its children are examined.
 */
void __devinit
pcibios_fixup_bus (struct pci_bus *b)
{
	struct pci_dev *dev;

	if (b->self) {
		pci_read_bridge_bases(b);
		pcibios_fixup_bridge_resources(b->self);
	} else {
		pcibios_setup_root_windows(b, b->sysdata);
	}
	list_for_each_entry(dev, &b->devices, bus_list)
		pcibios_fixup_device_resources(dev);
	platform_pci_fixup_bus(b);

	return;
}

void __devinit
pcibios_update_irq (struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);

	/* ??? FIXME -- record old value for shutdown.  */
}

int
pcibios_enable_device (struct pci_dev *dev, int mask)
{
	int ret;

	ret = pci_enable_resources(dev, mask);
	if (ret < 0)
		return ret;

	if (!dev->msi_enabled)
		return acpi_pci_irq_enable(dev);
	return 0;
}

void
pcibios_disable_device (struct pci_dev *dev)
{
	BUG_ON(atomic_read(&dev->enable_cnt));
	if (!dev->msi_enabled)
		acpi_pci_irq_disable(dev);
}

void
pcibios_align_resource (void *data, struct resource *res,
		        resource_size_t size, resource_size_t align)
{
}

/*
 * PCI BIOS setup, always defaults to SAL interface
 */
char * __init
pcibios_setup (char *str)
{
	return str;
}

int
pci_mmap_page_range (struct pci_dev *dev, struct vm_area_struct *vma,
		     enum pci_mmap_state mmap_state, int write_combine)
{
	unsigned long size = vma->vm_end - vma->vm_start;
	pgprot_t prot;

	/*
	 * I/O space cannot be accessed via normal processor loads and
	 * stores on this platform.
	 */
	if (mmap_state == pci_mmap_io)
		/*
		 * XXX we could relax this for I/O spaces for which ACPI
		 * indicates that the space is 1-to-1 mapped.  But at the
		 * moment, we don't support multiple PCI address spaces and
		 * the legacy I/O space is not 1-to-1 mapped, so this is moot.
		 */
		return -EINVAL;

	if (!valid_mmap_phys_addr_range(vma->vm_pgoff, size))
		return -EINVAL;

	prot = phys_mem_access_prot(NULL, vma->vm_pgoff, size,
				    vma->vm_page_prot);

	/*
	 * If the user requested WC, the kernel uses UC or WC for this region,
	 * and the chipset supports WC, we can use WC. Otherwise, we have to
	 * use the same attribute the kernel uses.
	 */
	if (write_combine &&
	    ((pgprot_val(prot) & _PAGE_MA_MASK) == _PAGE_MA_UC ||
	     (pgprot_val(prot) & _PAGE_MA_MASK) == _PAGE_MA_WC) &&
	    efi_range_is_wc(vma->vm_start, vma->vm_end - vma->vm_start))
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	else
		vma->vm_page_prot = prot;

	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			     vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -EAGAIN;

	return 0;
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
		printk(KERN_ERR "%s: ia64_pal_cache_summary() failed "
			"(status=%ld)\n", __func__, status);
		return;
	}

	status = ia64_pal_cache_config_info(levels - 1,
				/* cache_type (data_or_unified)= */ 2, &cci);
	if (status != 0) {
		printk(KERN_ERR "%s: ia64_pal_cache_config_info() failed "
			"(status=%ld)\n", __func__, status);
		return;
	}
	pci_dfl_cache_line_size = (1 << cci.pcci_line_size) / 4;
}

u64 ia64_dma_get_required_mask(struct device *dev)
{
	u32 low_totalram = ((max_pfn - 1) << PAGE_SHIFT);
	u32 high_totalram = ((max_pfn - 1) >> (32 - PAGE_SHIFT));
	u64 mask;

	if (!high_totalram) {
		/* convert to mask just covering totalram */
		low_totalram = (1 << (fls(low_totalram) - 1));
		low_totalram += low_totalram - 1;
		mask = low_totalram;
	} else {
		high_totalram = (1 << (fls(high_totalram) - 1));
		high_totalram += high_totalram - 1;
		mask = (((u64)high_totalram) << 32) + 0xffffffff;
	}
	return mask;
}
EXPORT_SYMBOL_GPL(ia64_dma_get_required_mask);

u64 dma_get_required_mask(struct device *dev)
{
	return platform_dma_get_required_mask(dev);
}
EXPORT_SYMBOL_GPL(dma_get_required_mask);

static int __init pcibios_init(void)
{
	set_pci_dfl_cacheline_size();
	return 0;
}

subsys_initcall(pcibios_init);
