// SPDX-License-Identifier: GPL-2.0
/*
 * Primary to Sideband (P2SB) bridge access support
 *
 * Copyright (c) 2017, 2021-2022 Intel Corporation.
 *
 * Authors: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *	    Jonathan Yong <jonathan.yong@intel.com>
 */

#include <linux/bits.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/platform_data/x86/p2sb.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>

#define P2SBC			0xe0
#define P2SBC_HIDE		BIT(8)

#define P2SB_DEVFN_DEFAULT	PCI_DEVFN(31, 1)
#define P2SB_DEVFN_GOLDMONT	PCI_DEVFN(13, 0)
#define SPI_DEVFN_GOLDMONT	PCI_DEVFN(13, 2)

static const struct x86_cpu_id p2sb_cpu_ids[] = {
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_GOLDMONT, P2SB_DEVFN_GOLDMONT),
	{}
};

/*
 * Cache BAR0 of P2SB device functions 0 to 7.
 * TODO: The constant 8 is the number of functions that PCI specification
 *       defines. Same definitions exist tree-wide. Unify this definition and
 *       the other definitions then move to include/uapi/linux/pci.h.
 */
#define NR_P2SB_RES_CACHE 8

struct p2sb_res_cache {
	u32 bus_dev_id;
	struct resource res;
};

static struct p2sb_res_cache p2sb_resources[NR_P2SB_RES_CACHE];

static void p2sb_get_devfn(unsigned int *devfn)
{
	unsigned int fn = P2SB_DEVFN_DEFAULT;
	const struct x86_cpu_id *id;

	id = x86_match_cpu(p2sb_cpu_ids);
	if (id)
		fn = (unsigned int)id->driver_data;

	*devfn = fn;
}

static bool p2sb_valid_resource(const struct resource *res)
{
	return res->flags & ~IORESOURCE_UNSET;
}

/* Copy resource from the first BAR of the device in question */
static void p2sb_read_bar0(struct pci_dev *pdev, struct resource *mem)
{
	struct resource *bar0 = pci_resource_n(pdev, 0);

	/* Make sure we have no dangling pointers in the output */
	memset(mem, 0, sizeof(*mem));

	/*
	 * We copy only selected fields from the original resource.
	 * Because a PCI device will be removed soon, we may not use
	 * any allocated data, hence we may not copy any pointers.
	 */
	mem->start = bar0->start;
	mem->end = bar0->end;
	mem->flags = bar0->flags;
	mem->desc = bar0->desc;
}

static void p2sb_scan_and_cache_devfn(struct pci_bus *bus, unsigned int devfn)
{
	struct p2sb_res_cache *cache = &p2sb_resources[PCI_FUNC(devfn)];
	struct pci_dev *pdev;

	pdev = pci_scan_single_device(bus, devfn);
	if (!pdev)
		return;

	p2sb_read_bar0(pdev, &cache->res);
	cache->bus_dev_id = bus->dev.id;

	pci_stop_and_remove_bus_device(pdev);
}

static int p2sb_scan_and_cache(struct pci_bus *bus, unsigned int devfn)
{
	/* Scan the P2SB device and cache its BAR0 */
	p2sb_scan_and_cache_devfn(bus, devfn);

	/* On Goldmont p2sb_bar() also gets called for the SPI controller */
	if (devfn == P2SB_DEVFN_GOLDMONT)
		p2sb_scan_and_cache_devfn(bus, SPI_DEVFN_GOLDMONT);

	if (!p2sb_valid_resource(&p2sb_resources[PCI_FUNC(devfn)].res))
		return -ENOENT;

	return 0;
}

static struct pci_bus *p2sb_get_bus(struct pci_bus *bus)
{
	static struct pci_bus *p2sb_bus;

	bus = bus ?: p2sb_bus;
	if (bus)
		return bus;

	/* Assume P2SB is on the bus 0 in domain 0 */
	p2sb_bus = pci_find_bus(0, 0);
	return p2sb_bus;
}

static int p2sb_cache_resources(void)
{
	unsigned int devfn_p2sb;
	u32 value = P2SBC_HIDE;
	struct pci_bus *bus;
	u16 class;
	int ret;

	/* Get devfn for P2SB device itself */
	p2sb_get_devfn(&devfn_p2sb);

	bus = p2sb_get_bus(NULL);
	if (!bus)
		return -ENODEV;

	/*
	 * When a device with same devfn exists and its device class is not
	 * PCI_CLASS_MEMORY_OTHER for P2SB, do not touch it.
	 */
	pci_bus_read_config_word(bus, devfn_p2sb, PCI_CLASS_DEVICE, &class);
	if (!PCI_POSSIBLE_ERROR(class) && class != PCI_CLASS_MEMORY_OTHER)
		return -ENODEV;

	/*
	 * Prevent concurrent PCI bus scan from seeing the P2SB device and
	 * removing via sysfs while it is temporarily exposed.
	 */
	pci_lock_rescan_remove();

	/*
	 * The BIOS prevents the P2SB device from being enumerated by the PCI
	 * subsystem, so we need to unhide and hide it back to lookup the BAR.
	 * Unhide the P2SB device here, if needed.
	 */
	pci_bus_read_config_dword(bus, devfn_p2sb, P2SBC, &value);
	if (value & P2SBC_HIDE)
		pci_bus_write_config_dword(bus, devfn_p2sb, P2SBC, 0);

	ret = p2sb_scan_and_cache(bus, devfn_p2sb);

	/* Hide the P2SB device, if it was hidden */
	if (value & P2SBC_HIDE)
		pci_bus_write_config_dword(bus, devfn_p2sb, P2SBC, P2SBC_HIDE);

	pci_unlock_rescan_remove();

	return ret;
}

/**
 * p2sb_bar - Get Primary to Sideband (P2SB) bridge device BAR
 * @bus: PCI bus to communicate with
 * @devfn: PCI slot and function to communicate with
 * @mem: memory resource to be filled in
 *
 * If @bus is NULL, the bus 0 in domain 0 will be used.
 * If @devfn is 0, it will be replaced by devfn of the P2SB device.
 *
 * Caller must provide a valid pointer to @mem.
 *
 * Return:
 * 0 on success or appropriate errno value on error.
 */
int p2sb_bar(struct pci_bus *bus, unsigned int devfn, struct resource *mem)
{
	struct p2sb_res_cache *cache;

	bus = p2sb_get_bus(bus);
	if (!bus)
		return -ENODEV;

	if (!devfn)
		p2sb_get_devfn(&devfn);

	cache = &p2sb_resources[PCI_FUNC(devfn)];
	if (cache->bus_dev_id != bus->dev.id)
		return -ENODEV;

	if (!p2sb_valid_resource(&cache->res))
		return -ENOENT;

	memcpy(mem, &cache->res, sizeof(*mem));
	return 0;
}
EXPORT_SYMBOL_GPL(p2sb_bar);

static int __init p2sb_fs_init(void)
{
	return p2sb_cache_resources();
}

/*
 * pci_rescan_remove_lock() can not be locked in sysfs PCI bus rescan path
 * because of deadlock. To avoid the deadlock, access P2SB devices with the lock
 * at an early step in kernel initialization and cache required resources.
 *
 * We want to run as early as possible. If the P2SB was assigned a bad BAR,
 * we'll need to wait on pcibios_assign_resources() to fix it. So, our list of
 * initcall dependencies looks something like this:
 *
 * ...
 * subsys_initcall (pci_subsys_init)
 * fs_initcall     (pcibios_assign_resources)
 */
fs_initcall_sync(p2sb_fs_init);
